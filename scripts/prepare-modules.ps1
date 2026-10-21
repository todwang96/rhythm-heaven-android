# Prepare the patched guest modules (main / sdk) from YOUR OWN game dump.
# Usage: powershell -ExecutionPolicy Bypass -File scripts\prepare-modules.ps1 [-Dump <dir>]
param([string]$Dump = "")
$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
if (-not $Dump) { $Dump = Join-Path $repo "game-dump" }
$work = Join-Path $repo "work\rhythm-heaven"
$hostDir = Join-Path $work "host"
$outDir = Join-Path $work "patched-android"
New-Item -ItemType Directory -Force -Path $hostDir, $outDir | Out-Null

$header = @(
    "/* Generated from Touchable/internal/games/rhythm-heaven/game.json. */",
    "#pragma once",
    '#define NITEN_GAME_ID "rhythm-heaven"',
    '#define NITEN_GAME_NAME "Rhythm Heaven"',
    '#define NITEN_GAME_PRODUCT "RhythmHeaven"',
    '#define NITEN_GAME_MODULES {"main","sdk"}',
    "#define NITEN_GAME_TITLE_ID 0x0100d9f01d474000ull"
)
Set-Content -Encoding ascii (Join-Path $hostDir "niten_game_config.h") -Value $header

$env:DM_TARGET = "android"
$env:DM_RULESET = "native"
$env:DM_PRIVATE_GATES = "0"
$env:DM_INDIRECT_TRAPS = "1"
foreach ($module in @("main", "sdk")) {
    $nso = Join-Path $Dump "exefs\$module"
    if (-not (Test-Path $nso)) { throw "missing ExeFS module: $nso (see docs/PREPARE-GAME.md)" }
    Write-Host "patch $module ..."
    python (Join-Path $repo "tools\patch_nso.py") $nso $outDir $module
}
Write-Host "modules ready: $outDir" -ForegroundColor Green
