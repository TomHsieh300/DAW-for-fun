<#
.SYNOPSIS
  Configure, build and test the DawLab course (Windows; uses the CMake bundled with Visual Studio 2022 / Build Tools).

.EXAMPLE
  .\scripts\build.ps1                         # Build all pure C++ chapters and run every test
  .\scripts\build.ps1 -Only 02-filter         # Build only chapter 02
  .\scripts\build.ps1 -Juce                   # Also build the JUCE programs of chapters 13 and 14
  .\scripts\build.ps1 -Only 13-vst-hosting,14-daw-8track -Juce   # Separate multiple chapters with commas
  .\scripts\build.ps1 -Config Release -NoTest
#>
param(
    [string[]]$Only = @(),
    [string]$BuildDir = "build",
    [ValidateSet("Debug", "Release", "RelWithDebInfo")]
    [string]$Config = "Debug",
    [switch]$Juce,
    [switch]$NoTest,
    [string]$TestLabel = ""
)

$ErrorActionPreference = "Stop"
# With powershell -File, 'a,b' arrives as a single string; split it into an array here
$Only = @($Only | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
$root = Split-Path -Parent $PSScriptRoot

function Find-CMake {
    $cmd = Get-Command cmake -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($vs) {
            $c = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
            if (Test-Path $c) { return $c }
        }
    }
    throw "CMake not found. Install Visual Studio 2022 (with 'Desktop development with C++') or CMake."
}

$cmake = Find-CMake
$ctest = Join-Path (Split-Path $cmake) "ctest.exe"
$buildPath = if ([System.IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $root $BuildDir }

$configureArgs = @("-S", $root, "-B", $buildPath, "-G", "Visual Studio 17 2022", "-A", "x64")
$configureArgs += "-DDAWLAB_ONLY=$($Only -join ';')"
$configureArgs += "-DDAWLAB_WITH_JUCE=$(if ($Juce) { 'ON' } else { 'OFF' })"

Write-Host "==> configure ($buildPath)" -ForegroundColor Cyan
& $cmake @configureArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "==> build ($Config)" -ForegroundColor Cyan
& $cmake --build $buildPath --config $Config --parallel
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if (-not $NoTest) {
    Write-Host "==> test" -ForegroundColor Cyan
    $testArgs = @("--test-dir", $buildPath, "-C", $Config, "--output-on-failure")
    if ($TestLabel) { $testArgs += @("-L", $TestLabel) }
    & $ctest @testArgs
    exit $LASTEXITCODE
}
