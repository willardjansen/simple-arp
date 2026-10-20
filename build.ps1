<#
.SYNOPSIS
    Build, test and install Simple Arp.

.DESCRIPTION
    Configures (first run or with -Configure), builds Release, runs the headless test
    harness, and copies the VST3 and CLAP into the system plugin folders. Stops at the first
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
    # The VST3 for Cubase and Live; the CLAP for Bitwig and other CLAP hosts.
    $installs = @(
        @{ Source = "VST3\Simple Arp.vst3"; Target = 'C:\Program Files\Common Files\VST3\Simple Arp.vst3' },
        @{ Source = "CLAP\Simple Arp.clap"; Target = 'C:\Program Files\Common Files\CLAP\Simple Arp.clap' }
    )

    Write-Host '==> Installing' -ForegroundColor Cyan

    foreach ($install in $installs) {
        $source = Join-Path $buildDir "SimpleArp_artefacts\$Config\$($install.Source)"
        $target = $install.Target

        if (-not (Test-Path $source)) { throw "Built plugin not found at $source" }

        try {
            if (Test-Path $target) { Remove-Item $target -Recurse -Force -ErrorAction Stop }
            New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
            Copy-Item $source $target -Recurse -Force -ErrorAction Stop
            Write-Host "    $target" -ForegroundColor DarkGray
        }
        catch {
            # Two causes: a host has it open, or the folder is still admin-only (the CLAP
            # folder is, out of the box).
            throw "Could not install $target. Either a host has it open (close Cubase or Bitwig), or the folder needs a one-time grant from an admin PowerShell:`n    icacls `"$(Split-Path $target)`" /grant `"*S-1-5-32-545:(OI)(CI)F`"`n$_"
        }
    }

    Write-Host '    Remove and re-add the plugin in your host to pick up GUI changes.' -ForegroundColor DarkGray
}

Write-Host '==> Done' -ForegroundColor Green
