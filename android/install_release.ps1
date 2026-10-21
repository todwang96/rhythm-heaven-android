# 节奏天国 Android 正式版一键安装：装 APK + 推数据包（modules / shaders / romfs）。
# 用电脑 USB 连上手机/平板后运行：
#   powershell -ExecutionPolicy Bypass -File install_release.ps1
#   powershell -ExecutionPolicy Bypass -File install_release.ps1 -Serial bd1d7527
#   powershell ... -SkipData     # 只装 APK（数据已在设备上）
#   powershell ... -SkipApk      # 只补数据
param(
    [string]$Serial = "",
    [string]$Apk = "",
    [switch]$SkipApk,
    [switch]$SkipData
)

$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path      # rh-port/android
$port = Split-Path -Parent $here                             # rh-port
$adb = if ($env:ADB) { $env:ADB } else { "adb" }
if (-not (Test-Path $adb)) { $adb = "adb" }
if (-not $Apk) { $Apk = Join-Path $here "out\rhythm-heaven-android-release.apk" }
if (-not (Test-Path $Apk)) {
    Write-Host "找不到 APK：$Apk" -ForegroundColor Red
    Write-Host "先构建：powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -Release" -ForegroundColor Yellow
    exit 1
}

# 1) 选设备
$lines = & $adb devices | Select-Object -Skip 1 | Where-Object { $_ -match "\tdevice$" }
$devices = @($lines | ForEach-Object { ($_ -split "\t")[0] })
if ($devices.Count -eq 0) { Write-Host "没有检测到已授权的设备（adb devices）" -ForegroundColor Red; exit 1 }
if (-not $Serial) {
    if ($devices.Count -gt 1) {
        Write-Host "检测到多台设备，请用 -Serial 指定：" -ForegroundColor Yellow
        $devices | ForEach-Object { Write-Host "  $_" }
        exit 1
    }
    $Serial = $devices[0]
}
Write-Host "目标设备：$Serial" -ForegroundColor Cyan

# 2) 装 APK
if (-not $SkipApk) {
    Write-Host "[1/2] 安装 APK：$Apk" -ForegroundColor Cyan
    & $adb -s $Serial install -r $Apk | Select-Object -Last 2
    if ($LASTEXITCODE -ne 0) { Write-Host "APK 安装失败" -ForegroundColor Red; exit 1 }
} else {
    Write-Host "[1/2] 跳过 APK（-SkipApk）" -ForegroundColor DarkGray
}

# 3) 推数据（modules + shaders + romfs，3.4GB 约 1 分钟）
if (-not $SkipData) {
    Write-Host "[2/2] 推送数据包（modules / shaders / romfs）..." -ForegroundColor Cyan
    $py = "python"
    $env:PYTHONIOENCODING = "utf-8"
    & $py (Join-Path $here "push_data.py") --serial $Serial
    if ($LASTEXITCODE -ne 0) { Write-Host "数据推送失败" -ForegroundColor Red; exit 1 }
} else {
    Write-Host "[2/2] 跳过数据（-SkipData）" -ForegroundColor DarkGray
}

# 4) 汇总
Write-Host ""
Write-Host "完成。设备上的数据目录：" -ForegroundColor Green
& $adb -s $Serial shell "du -sh /sdcard/Android/media/com.rhport.rhythm/rhdata/romfs /sdcard/Android/media/com.rhport.rhythm/rhdata/modules /sdcard/Android/media/com.rhport.rhythm/rhdata/shaders 2>/dev/null"
Write-Host "现在可以在设备上启动「节奏天国」了。" -ForegroundColor Green
