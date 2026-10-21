# Build the importable data package (romfs.img + modules + shaders) for the launcher.
# Usage: powershell -ExecutionPolicy Bypass -File scripts\pack-data.ps1 [-Dump <dir>] [-Out <zip>]
param([string]$Dump = "", [string]$Out = "")
$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
if (-not $Dump) { $Dump = Join-Path $repo "game-dump" }
$env:RH_GAME_DUMP = $Dump
$packArgs = @()
if ($Out) { $packArgs += @("-Out", $Out) }
Push-Location (Join-Path $repo "android")
try { & (Join-Path $repo "android\make_data_pack.ps1") @packArgs } finally { Pop-Location }
