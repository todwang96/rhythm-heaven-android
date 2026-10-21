# Fetch the pinned d2r-mobile dependency and apply the Android port patch.
# Usage: powershell -ExecutionPolicy Bypass -File scripts\setup.ps1 [-Force]
param([switch]$Force)
$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$dep = Join-Path $repo ".nitenphone\dependencies\d2r-mobile"
$url = "https://github.com/jhaizhou-ops/d2r-mobile.git"
$commit = "2eb5758771f110cd53082b999d88d242766610da"

if (Test-Path $dep) {
    if (-not $Force) { Write-Host "dependency exists: $dep (use -Force to re-fetch)"; exit 0 }
    Remove-Item -Recurse -Force $dep
}
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dep) | Out-Null
git clone $url $dep
git -C $dep checkout $commit
git -C $dep apply --whitespace=nowarn (Join-Path $repo "patches\d2r-mobile-android.patch")
python -m pip install -r (Join-Path $repo "requirements.txt")
Write-Host "dependency ready: $dep" -ForegroundColor Green
