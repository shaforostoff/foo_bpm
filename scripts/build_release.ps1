<#
.SYNOPSIS
    Builds foo_bpm in the Release configuration and packages it as an
    installable .fb2k-component in dist\.

.DESCRIPTION
    Pulls the build prerequisites if they are not there yet, drives MSBuild
    over foo_bpm.sln (Release|Win32 - the only configuration the project
    defines) and zips the resulting DLL into

        dist\foo_dsp_bpm.fb2k-component
          foo_bpm.dll

    The foobar2000 SDK and WTL are fetched by scripts\get_sdk.ps1 into
    external\, so a fresh checkout builds with nothing but Visual Studio
    installed. Run that script by hand only when you want -Force.

    The DLL name inside the archive is fixed: foo_bpm.cpp calls
    VALIDATE_COMPONENT_FILENAME("foo_bpm.dll") and foobar2000 refuses to load
    the component under any other name. The archive name is free-form, so
    -Name only changes the file you hand out.

    This is a 32 bit component built against the 2011-03-11 SDK, so it loads in
    foobar2000 1.x and in the 32 bit builds of 2.x.

.PARAMETER Name
    Base name of the .fb2k-component file. Default: foo_dsp_bpm.

.PARAMETER Toolset
    MSVC platform toolset, e.g. v142. Default: the newest one installed.

    This is always passed to MSBuild, never left alone: the SDK's own projects
    and kiss_fft.vcxproj pin PlatformToolset to v100 (Visual Studio 2010),
    which no current Visual Studio can install, so the build only gets off the
    ground with the pin overridden.

.PARAMETER Clean
    Rebuild from scratch instead of building incrementally.

.EXAMPLE
    .\scripts\build_release.ps1

.EXAMPLE
    .\scripts\build_release.ps1 -Clean -Name foo_bpm
#>

[CmdletBinding()]
param(
    [string] $Name = 'foo_dsp_bpm',
    [string] $Toolset = '',
    [switch] $Clean
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root       = Split-Path -Parent $PSScriptRoot
$solution   = Join-Path $root 'foo_bpm.sln'
$distDir    = Join-Path $root 'dist'
$component  = Join-Path $distDir "$Name.fb2k-component"

# --- prerequisites ----------------------------------------------------------
# foo_bpm.sln and foo_bpm.vcxproj reference the SDK and WTL under external\;
# get_sdk.ps1 puts them there. It is a no-op once the stamp files are in place,
# so this costs one Test-Path on every later build.
if (-not (Test-Path (Join-Path $root 'external\foobar2000_sdk\foobar2000\SDK\foobar2000.h')) -or
    -not (Test-Path (Join-Path $root 'external\wtl\Include\atlapp.h'))) {
    Write-Host "`n=== Prerequisites ===" -ForegroundColor Cyan
    & (Join-Path $PSScriptRoot 'get_sdk.ps1')
}

function Find-MSBuild {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $found = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild `
                            -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
        if ($found) { return $found }
    }
    $onPath = Get-Command msbuild.exe -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }
    throw 'MSBuild not found. Install Visual Studio with the C++ workload, or run this from a Developer PowerShell.'
}

# The vcxproj files predate the toolset they will actually be built with, so
# work out the newest installed one and override their v100 pin with it.
function Find-DefaultToolset {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $install = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
                              -property installationPath | Select-Object -First 1
        if ($install) {
            $newest = Get-ChildItem (Join-Path $install 'VC\Tools\MSVC') -Directory -ErrorAction SilentlyContinue |
                          Sort-Object { [version] $_.Name } -Descending | Select-Object -First 1
            if ($newest) {
                $v = [version] $newest.Name
                switch ("{0}.{1}" -f $v.Major, $v.Minor) {
                    '14.1'  { return 'v141' }
                    '14.2'  { return 'v142' }
                    default { return 'v143' }   # 14.3 and 14.4 are both v143
                }
            }
        }
    }
    return 'v143'
}

$msbuild = Find-MSBuild
if (-not $Toolset) { $Toolset = Find-DefaultToolset }
Write-Host "MSBuild: $msbuild" -ForegroundColor DarkGray
Write-Host "Toolset: $Toolset" -ForegroundColor DarkGray

# --- build ------------------------------------------------------------------
# Release|Win32 is the only configuration foo_bpm.vcxproj defines; the solution
# maps its x64 entries onto Win32 as well.
Write-Host "`n=== Building foo_bpm (Release|Win32) ===" -ForegroundColor Cyan
# Build the foo_bpm target rather than the whole solution: that pulls in the
# five SDK libraries and kiss_fft through the project references, and leaves
# out kiss_fft_test, whose C++98 custom allocator no longer satisfies
# std::vector and which has nothing to do with the component.
$targets = if ($Clean) { 'foo_bpm:Rebuild' } else { 'foo_bpm' }
$msbuildArgs = @($solution,
                 "/t:$targets",
                 '/p:Configuration=Release',
                 '/p:Platform=Win32',
                 "/p:PlatformToolset=$Toolset",
                 "/p:ForceImportBeforeCppTargets=$(Join-Path $PSScriptRoot 'external.props')",
                 '/m',
                 '/nologo',
                 '/v:minimal')

& $msbuild @msbuildArgs
if ($LASTEXITCODE -ne 0) { throw "MSBuild failed with exit code $LASTEXITCODE" }

# --- locate the DLL ---------------------------------------------------------
# The projects leave OutDir at its default, which for Win32 is
# $(SolutionDir)$(Configuration)\ - but fall back to a search so a local
# .user.props that moves it does not break packaging.
$dll = Join-Path $root 'Release\foo_bpm.dll'
if (-not (Test-Path $dll)) {
    $dll = Get-ChildItem $root -Recurse -Filter 'foo_bpm.dll' -File -ErrorAction SilentlyContinue |
               Where-Object { $_.FullName -notmatch '\(Debug|dist|external)\' } |
               Sort-Object LastWriteTime -Descending |
               Select-Object -First 1 -ExpandProperty FullName
}
if (-not $dll) { throw 'Build reported success but foo_bpm.dll was not found.' }

# --- package ----------------------------------------------------------------
Write-Host "`n=== Package ===" -ForegroundColor Cyan
New-Item -ItemType Directory -Force $distDir | Out-Null
if (Test-Path $component) { Remove-Item -Force $component }

# Compress-Archive insists on a .zip destination, so build one and rename it -
# a .fb2k-component is a plain zip.
$zip = Join-Path $distDir "$Name.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path $dll -DestinationPath $zip -CompressionLevel Optimal
Move-Item $zip $component

$built = Get-Item $dll
Write-Host ("  foo_bpm.dll  {0:N0} bytes  ({1:yyyy-MM-dd HH:mm})" -f $built.Length, $built.LastWriteTime) -ForegroundColor DarkGray
Write-Host ("  {0}  ({1:N0} bytes)" -f $component, (Get-Item $component).Length) -ForegroundColor Green

Write-Host @"

To install: drag the .fb2k-component file onto foobar2000, or use
File > Preferences > Components > Install...
"@ -ForegroundColor Yellow
