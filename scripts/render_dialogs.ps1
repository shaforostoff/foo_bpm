<#
.SYNOPSIS
    Draws foo_rubato's dialogs to PNG so a layout can be looked at without
    foobar2000 running.

.DESCRIPTION
    The component's dialogs - the preferences page, the results window, the
    manual tap dialog - cannot be opened without foobar2000, so for a long time
    they were only ever compiled. Two layout faults reached a release that way.
    A dialog template is only a resource, though, and the dialog manager will
    build one from any process, so they can be drawn from the built DLL alone.

    This writes one PNG per dialog into build\dialogs\ and reports any label
    that does not fit the control drawn around it. The same check runs as the
    `dialog_labels` CTest case, without the images; this script is for when you
    want to see them.

    What it cannot show is anything the component fills in at run time: the
    results window's columns and rows, the combo box items, the contents of
    edit controls, which check is set. Geometry and the text baked into the
    template are what is being looked at.

.PARAMETER Arch
    Which build tree to read. Default: x64.

.PARAMETER Configuration
    Which configuration to read. Default: Release.

.PARAMETER Font
    Redraw with this font face instead of the template's own, to ask what a
    host restyling the page would do. Use knowing what it means: the dialog
    manager sizes every control in units derived from the template's FONT, so a
    wider face reports healthy labels as clipped. Needs -FontPoints.

.PARAMETER FontPoints
    Point size for -Font.

.PARAMETER Show
    Open the output folder afterwards.

.EXAMPLE
    .\scripts\render_dialogs.ps1

.EXAMPLE
    .\scripts\render_dialogs.ps1 -Arch x86 -Show

.EXAMPLE
    .\scripts\render_dialogs.ps1 -Font 'Segoe UI' -FontPoints 9
#>

[CmdletBinding()]
param(
    [ValidateSet('x86', 'x64')]
    [string] $Arch = 'x64',
    [string] $Configuration = 'Release',
    [string] $Font = '',
    [int]    $FontPoints = 0,
    [switch] $Show
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root  = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root "build\$Arch"
$dll   = Join-Path $build "foo_rubato\$Configuration\foo_rubato.dll"
$tool  = Join-Path $build "dialog_test\$Configuration\dialog_test.exe"
$out   = Join-Path $build 'dialogs'

foreach ($needed in @($dll, $tool)) {
    if (-not (Test-Path $needed)) {
        throw "$needed is not there. Build it first:`n" +
              "    cmake -S `"$root`" -B `"$build`" -A $Arch`n" +
              "    cmake --build `"$build`" --config $Configuration"
    }
}

if (-not (Test-Path $out)) {
    New-Item -ItemType Directory -Force $out | Out-Null
}

$argv = @($dll, '--out', $out)
if ($Font -ne '') {
    if ($FontPoints -le 0) { throw '-Font needs -FontPoints.' }
    $argv += @('--font', $Font, [string] $FontPoints)
}

& $tool @argv
$code = $LASTEXITCODE

if ($Show) { Start-Process $out }

switch ($code) {
    0 { Write-Host "`nEvery label fits." -ForegroundColor Green }
    77 { Write-Host "`nNo desktop to draw on - nothing was rendered." -ForegroundColor Yellow }
    1 { Write-Host "`nSomething is clipped. See above." -ForegroundColor Red }
    default { Write-Host "`ndialog_test failed (exit $code)." -ForegroundColor Red }
}

exit $code
