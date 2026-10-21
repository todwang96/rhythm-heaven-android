# Install the APK and put the data package on the device.
# Usage: powershell -ExecutionPolicy Bypass -File install.ps1 [-Serial <adb serial>] [-Apk path] [-Package path]
param([string]$Serial = "", [string]$Apk = "", [string]$Package = "")
$ErrorActionPreference = "Stop"
$kit = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $Apk) { $Apk = Join-Path $kit "apk\rhythm-heaven-android-release.apk" }
if (-not $Package) { $Package = Join-Path $kit "out\rhdata-pack.zip" }
if (-not (Test-Path $Apk)) { throw "APK missing: $Apk" }
if (-not (Test-Path $Package)) { throw "data package missing: $Package (run package.ps1 first)" }

$adb = if ($env:ADB) { $env:ADB } else { "adb" }
if (-not (Get-Command $adb -ErrorAction SilentlyContinue)) { throw "adb not found; add platform-tools to PATH or set `$env:ADB" }
$target = @()
if ($Serial) { $target = @("-s", $Serial) }

Write-Host "[1/2] install APK"
& $adb @target install -r $Apk
if ($LASTEXITCODE -ne 0) { throw "adb install failed" }

Write-Host "[2/2] push data package"
& $adb @target shell "mkdir -p /sdcard/Android/media/com.rhport.rhythm"
& $adb @target push $Package "/sdcard/Android/media/com.rhport.rhythm/rhdata-pack.zip"
if ($LASTEXITCODE -ne 0) { throw "adb push failed" }

Write-Host ""
Write-Host "done. On the device:" -ForegroundColor Green
Write-Host "  1. open Rhythm Heaven Android"
Write-Host "  2. 游戏数据 -> 导入数据包  (Data -> Import package)"
Write-Host "  3. wait for the import to finish, go back, tap 启动游戏"
Write-Host ""
Write-Host "Runtime log: adb pull /sdcard/Android/media/com.rhport.rhythm/rhdata/run.log"
