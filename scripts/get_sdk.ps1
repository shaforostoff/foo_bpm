<#
.SYNOPSIS
    Downloads, unpacks and patches the build prerequisites into external\.

.DESCRIPTION
    foo_bpm.sln needs two things that are not in the repository:

        external\foobar2000_sdk\   foobar2000 SDK 2011-03-11
        external\wtl\              WTL 10.01

    Both are fetched from their upstream homes, checked against a pinned
    SHA256, unpacked here and - for the SDK - patched for modern MSVC.
    scripts\build_release.ps1 calls this script on its own when either is
    missing, so you normally never need to run it by hand.

    Nothing but PowerShell is required: both are plain .zip archives, so no
    7-Zip, CMake or git is involved.

    The SDK is pinned at 2011-03-11 because that is the release this component
    was written against, and the one foobar2000.org still serves at a stable
    URL. WTL is a separate download because the SDK's ATLHelpers.h includes WTL
    headers but does not ship them.

    WTL is 10.01 rather than the 8.0 the original project files named: WTL 8.0
    calls ATL::AtlGetCommCtrlVersion, which Microsoft removed from ATL long
    ago, so it cannot compile against any toolset you can install today. WTL 10
    is the same library with that surface fixed.

    The SDK patches below are the two places where the 2011 sources rely on
    pre-C++11 compiler behaviour. Each one carries the reason it is needed and
    refuses to apply unless it matches the pinned SDK exactly, so a changed
    upstream fails loudly instead of silently building something else. Two
    further incompatibilities are handled without touching the SDK at all, by
    the shim headers in scripts\compat\.

.PARAMETER Destination
    Where to unpack. Default: external\ next to this repository.

.PARAMETER Force
    Re-download, re-unpack and re-patch even if everything is already there.

.EXAMPLE
    .\scripts\get_sdk.ps1

.EXAMPLE
    .\scripts\get_sdk.ps1 -Force
#>

[CmdletBinding()]
param(
    [string] $Destination = '',
    [switch] $Force
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = Split-Path -Parent $PSScriptRoot
if (-not $Destination) { $Destination = Join-Path $root 'external' }

# Old PowerShell defaults to TLS 1.0, which neither host accepts any more.
[Net.ServicePointManager]::SecurityProtocol =
    [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12

$dependencies = @(
    [pscustomobject] @{
        Name    = 'foobar2000 SDK'
        Version = '2011-03-11'
        Dir     = 'foobar2000_sdk'
        Archive = 'SDK-2011-03-11.zip'
        Url     = 'https://www.foobar2000.org/files/SDK-2011-03-11.zip'
        Sha256  = '1EBE2616BD3E1B26DA052C9EFF67B3A7528C05B03F7945D7F7202F1846BDE078'
        # Unpacks with no wrapper folder: foobar2000\ and pfc\ sit at the root.
        Expect  = @('foobar2000\SDK\foobar2000.h',
                    'foobar2000\shared\shared.lib',
                    'pfc\pfc.vcxproj')
        Patches = @(
            [pscustomobject] @{
                File = 'pfc\list.h'
                Why  = 'copy ctor cannot go through operator= under C++11'
                # list_base_t has no usable copy assignment once C++11 rules
                # apply, so the implicit operator= of every list_impl_t is
                # deleted and the copy constructor - which assigns to itself -
                # stops compiling. add_items into the empty new object copies
                # exactly what that assignment used to.
                Old  = 'list_impl_t(const list_impl_t<T,t_storage> & p_source) { *this = p_source; }'
                New  = 'list_impl_t(const list_impl_t<T,t_storage> & p_source) { this->add_items(p_source); }'
            },
            [pscustomobject] @{
                File = 'foobar2000\SDK\guids.cpp'
                Why  = 'service GUID definitions must survive COMDAT pruning'
                # Every class_guid here is __declspec(selectany) and referenced
                # nowhere inside this file, so a current MSVC discards all of
                # them and the component fails to link with ~35 unresolved
                # externals. guids.cpp is the one translation unit meant to
                # define them, so plain definitions are what belongs here; the
                # handful of GUIDs defined in headers keep selectany.
                Old  = 'FOOGUIDDECL const GUID hasher_md5::class_guid ='
                New  = "#undef FOOGUIDDECL`r`n#define FOOGUIDDECL`r`n`r`nFOOGUIDDECL const GUID hasher_md5::class_guid ="
            }
        )
    },
    [pscustomobject] @{
        Name    = 'WTL'
        Version = '10.01'
        Dir     = 'wtl'
        Archive = 'WTL10_01_Release.zip'
        Url     = 'https://downloads.sourceforge.net/project/wtl/WTL%2010/WTL%2010.01%20Release/WTL10_01_Release.zip'
        Sha256  = '1A62EA728D088C7C5C7CFC76DB445E5C9F04923F92CADCF27B5D7678D85826A2'
        Expect  = @('Include\atlapp.h')
        Patches = @()
    }
)

function Get-Sha256([string] $path) {
    (Get-FileHash -Path $path -Algorithm SHA256).Hash.ToUpperInvariant()
}

New-Item -ItemType Directory -Force $Destination | Out-Null

foreach ($dep in $dependencies) {
    $dir     = Join-Path $Destination $dep.Dir
    $archive = Join-Path $Destination $dep.Archive
    $stamp   = Join-Path $dir (".{0}-{1}.stamp" -f $dep.Dir, $dep.Version)

    if ((Test-Path $stamp) -and -not $Force) {
        Write-Host ("{0} {1} already unpacked in {2}" -f $dep.Name, $dep.Version, $dir) -ForegroundColor DarkGray
        continue
    }

    # --- fetch --------------------------------------------------------------
    $haveArchive = $false
    if (Test-Path $archive) {
        if ((Get-Sha256 $archive) -eq $dep.Sha256) {
            $haveArchive = $true
            Write-Host "Reusing $archive" -ForegroundColor DarkGray
        }
        else {
            Write-Host "Discarding $archive (checksum mismatch)" -ForegroundColor Yellow
            Remove-Item -Force $archive
        }
    }

    if (-not $haveArchive) {
        Write-Host ("Downloading {0}" -f $dep.Url) -ForegroundColor Cyan
        $progress = $ProgressPreference
        $ProgressPreference = 'SilentlyContinue'    # an order of magnitude faster
        try {
            # PowerShell's default User-Agent looks like a browser, and SourceForge
            # answers those with its "your download will start shortly" HTML page
            # rather than the file. Ask as a plain downloader instead.
            Invoke-WebRequest -Uri $dep.Url -OutFile $archive -UseBasicParsing -UserAgent 'curl/8.4.0'
        }
        catch {
            if (Test-Path $archive) { Remove-Item -Force $archive }
            throw ("Failed to download {0}: {1}`nFetch {2} by hand, drop it in {3}, and re-run." -f
                   $dep.Name, $_.Exception.Message, $dep.Url, $Destination)
        }
        finally { $ProgressPreference = $progress }

        $got = Get-Sha256 $archive
        if ($got -ne $dep.Sha256) {
            Remove-Item -Force $archive
            throw ("{0}: checksum mismatch.`n  expected {1}`n  got      {2}`nThe download was corrupted, or upstream changed the file." -f
                   $dep.Name, $dep.Sha256, $got)
        }
    }

    # --- unpack -------------------------------------------------------------
    # Into a scratch directory first, so a half-finished or half-patched unpack
    # can never be mistaken for a usable SDK.
    $tmp = Join-Path $Destination (".unpack-" + $dep.Dir)
    if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }
    New-Item -ItemType Directory -Force $tmp | Out-Null

    Write-Host ("Unpacking into {0}" -f $dir) -ForegroundColor Cyan
    try {
        Expand-Archive -Path $archive -DestinationPath $tmp -Force
    }
    catch {
        Remove-Item -Recurse -Force $tmp
        throw ("Could not unpack {0}: {1}" -f $archive, $_.Exception.Message)
    }

    foreach ($need in $dep.Expect) {
        if (-not (Test-Path (Join-Path $tmp $need))) {
            Remove-Item -Recurse -Force $tmp
            throw ("Unexpected archive layout in {0}: {1} is missing" -f $dep.Archive, $need)
        }
    }

    # --- patch --------------------------------------------------------------
    # ISO-8859-1 maps every byte to one character and back, so the parts of a
    # file we do not touch are written out exactly as they came out of the zip.
    $latin1 = [System.Text.Encoding]::GetEncoding(28591)
    foreach ($patch in $dep.Patches) {
        $file = Join-Path $tmp $patch.File
        if (-not (Test-Path $file)) {
            Remove-Item -Recurse -Force $tmp
            throw ("Cannot patch {0}: the file is not in {1}" -f $patch.File, $dep.Archive)
        }

        $text = $latin1.GetString([System.IO.File]::ReadAllBytes($file))
        $hits = ([regex]::Matches($text, [regex]::Escape($patch.Old))).Count
        if ($hits -ne 1) {
            Remove-Item -Recurse -Force $tmp
            throw ("Cannot patch {0}: expected the text to appear once, found it {1} times.`nThe pinned {2} is not what this patch was written against." -f
                   $patch.File, $hits, $dep.Name)
        }

        [System.IO.File]::WriteAllBytes($file, $latin1.GetBytes($text.Replace($patch.Old, $patch.New)))
        Write-Host ("  patched {0} - {1}" -f $patch.File, $patch.Why) -ForegroundColor DarkGray
    }

    if (Test-Path $dir) { Remove-Item -Recurse -Force $dir }
    Move-Item $tmp $dir
    Set-Content -Path $stamp -Value @($dep.Version, $dep.Url) -Encoding utf8

    Write-Host ("{0} {1} ready in {2}" -f $dep.Name, $dep.Version, $dir) -ForegroundColor Green
}
