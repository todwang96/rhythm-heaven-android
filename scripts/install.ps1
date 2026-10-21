# Install the APK and push data to a connected device.
# Usage: powershell -ExecutionPolicy Bypass -File scripts\install.ps1 [-Serial <adb serial>] [-SkipApk] [-SkipData]
param([string]$Serial = "", [switch]$SkipApk, [switch]$SkipData)
$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$env:RH_GAME_DUMP = Join-Path $repo "game-dump"
$args = @()
if ($Serial) { $args += @("-Serial", $Serial) }
if ($SkipApk) { $args += "-SkipApk" }
if ($SkipData) { $args += "-SkipData" }
Push-Location (Join-Path $repo "android")
try { & (Join-Path $repo "android\install_release.ps1") @args } finally { Pop-Location }
