<#
.SYNOPSIS
    Build, test and install Simple Arp.

.DESCRIPTION
    Configures (first run or with -Configure), builds Release, runs the headless test
    harness, and copies the VST3 into the system plugin folder. Stops at the first
    failure so a broken build never gets installed.

.EXAMPLE
    .\build.ps1
    .\build.ps1 -Configure
    .\build.ps1 -SkipInstall
#>

[CmdletBinding()]
param(
    [switch]$Configure,
    [switch]$SkipTests,
    [switch]$SkipInstall,
    [ValidateSet('Release', 'Debug')]
    [string]$Config = 'Release'
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot

# CMake is not on PATH on this machine.
$cmake = 'C:\Program Files\CMake\bin\cmake.exe'

if (-not (Test-Path $cmake)) {
    $fromPath = Get-Command cmake -ErrorAction SilentlyContinue
    if ($null -eq $fromPath) {
        throw "CMake not found at '$cmake' and not on PATH. Install it with: winget install Kitware.CMake"
    }
    $cmake = $fromPath.Source
}

$buildDir = Join-Path $root 'build'

# ---------------------------------------------------------------- configure
if ($Configure -or -not (Test-Path (Join-Path $buildDir 'CMakeCache.txt'))) {
    Write-Host '==> Configuring' -ForegroundColor Cyan
    & $cmake -B $buildDir -S $root -G 'Visual Studio 17 2022' -A x64
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
}

# -------------------------------------------------------------------- build
Write-Host "==> Building ($Config)" -ForegroundColor Cyan
& $cmake --build $buildDir --config $Config --parallel
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }

# --------------------------------------------------------------------- test
if (-not $SkipTests) {
    $test = Join-Path $buildDir "ArpTest_artefacts\$Config\ArpTest.exe"

    if (-not (Test-Path $test)) { throw "Test harness not found at $test" }

    Write-Host '==> Testing' -ForegroundColor Cyan
    & $test
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed - not installing.' }
}

# ------------------------------------------------------------------ install
if (-not $SkipInstall) {
    $source = Join-Path $buildDir "SimpleArp_artefacts\$Config\VST3\Simple Arp.vst3"
    $target = 'C:\Program Files\Common Files\VST3\Simple Arp.vst3'

    if (-not (Test-Path $source)) { throw "Built plugin not found at $source" }

    Write-Host '==> Installing' -ForegroundColor Cyan

    try {
        if (Test-Path $target) { Remove-Item $target -Recurse -Force -ErrorAction Stop }
        Copy-Item $source $target -Recurse -Force -ErrorAction Stop
        Write-Host "    $target" -ForegroundColor DarkGray
    }
    catch {
        throw "Could not replace the installed plugin. It is usually locked by a running host - close Cubase and run again.`n$_"
    }

    Write-Host '    Remove and re-add the plugin in your host to pick up GUI changes.' -ForegroundColor DarkGray
}

Write-Host '==> Done' -ForegroundColor Green
