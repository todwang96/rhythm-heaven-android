# Build out\rhdata-pack.zip for the Rhythm Heaven Android launcher.
# Usage: powershell -ExecutionPolicy Bypass -File package.ps1 [-Dump game-dump] [-Out out\rhdata-pack.zip]
param([string]$Dump = "", [string]$Out = "")
$ErrorActionPreference = "Stop"
$kit = Split-Path -Parent $MyInvocation.MyCommand.Path
$src = Join-Path $kit "source"
if (-not (Test-Path $src)) { throw "source\ is missing - unzip the kit completely" }
if (-not $Dump) { $Dump = Join-Path $kit "game-dump" }
if (-not $Out) {
    New-Item -ItemType Directory -Force -Path (Join-Path $kit "out") | Out-Null
    $Out = Join-Path $kit "out\rhdata-pack.zip"
}

if (-not (Test-Path (Join-Path $Dump "romfs.img")) -and
    -not (Test-Path (Join-Path $Dump "romfs"))) {
    throw "missing game data under $Dump (need romfs.img or romfs\); see README.txt"
}
$work = Join-Path $src "work\rhythm-heaven"
$shaders = Join-Path $src "work\shaders\vk-cache"
if (-not (Test-Path (Join-Path $work "patched-android"))) {
    throw "patched modules missing: run  powershell -File source\scripts\prepare-modules.ps1 -Dump game-dump"
}
if (-not (Test-Path $shaders)) {
    throw "shader cache missing: run  powershell -File source\scripts\prepare-shaders.ps1  (see source\docs\PREPARE-GAME.md)"
}

$env:RH_GAME_DUMP = (Resolve-Path $Dump).Path
$env:RH_WORK = $work
$env:RH_SHADERS = $shaders
Write-Host "building data package -> $Out" -ForegroundColor Cyan
& (Join-Path $src "android\make_data_pack.ps1") -Out $Out
if (-not (Test-Path $Out)) { throw "data package was not produced" }

$size = (Get-Item $Out).Length
Write-Host ""
Write-Host ("data package ready: {0}  ({1:N2} GB)" -f $Out, ($size / 1GB)) -ForegroundColor Green
Write-Host "next:"
Write-Host "  * install.ps1 -Serial <adb serial>   (install APK + push package)"
Write-Host "  * or copy the zip to Android/media/com.rhport.rhythm/ and use"
Write-Host "    the launcher: 游戏数据 -> 导入数据包"
