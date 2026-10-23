<#
.SYNOPSIS
  設定、建置並測試 DawLab 教材（Windows，使用 Visual Studio 2022 / Build Tools 內附的 CMake）。

.EXAMPLE
  .\scripts\build.ps1                         # 建置全部純 C++ 章節並跑所有測試
  .\scripts\build.ps1 -Only 02-filter         # 只建置第 02 章
  .\scripts\build.ps1 -Juce                   # 連同第 13、14 章的 JUCE 程式一起建置
  .\scripts\build.ps1 -Only 13-vst-hosting,14-daw-8track -Juce   # 多個章節以逗號分隔
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
# 用 powershell -File 呼叫時，'a,b' 會變成單一字串；這裡統一拆成陣列
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
    throw "找不到 cmake。請安裝 Visual Studio 2022（含 'Desktop development with C++'）或 CMake。"
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
