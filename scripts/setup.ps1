# Fetch the pinned d2r-mobile dependency and apply the Android port patch.
# Usage: powershell -ExecutionPolicy Bypass -File scripts\setup.ps1 [-Force]
# If github.com is blocked on your network, set HTTP_PROXY/HTTPS_PROXY first,
# e.g. $env:HTTPS_PROXY = 'http://127.0.0.1:7890'
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

Write-Host "[1/3] clone $url"
git clone $url $dep
if ($LASTEXITCODE -ne 0) { throw "git clone failed (network? set HTTP_PROXY/HTTPS_PROXY and retry)" }
Write-Host "[2/3] checkout $commit"
git -C $dep checkout $commit
if ($LASTEXITCODE -ne 0) { throw "git checkout failed" }
Write-Host "[3/3] apply patches\d2r-mobile-android.patch"
git -C $dep apply --whitespace=nowarn (Join-Path $repo "patches\d2r-mobile-android.patch")
if ($LASTEXITCODE -ne 0) { throw "patch failed to apply" }

$python = "python"
if (-not (Get-Command $python -ErrorAction SilentlyContinue)) { $python = "py" }
& $python -c "import lz4, capstone" 2>$null
if ($LASTEXITCODE -ne 0) {
    Write-Host "[deps] installing Python requirements (lz4, capstone)"
    & $python -m pip install -r (Join-Path $repo "requirements.txt")
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "pip install failed; install lz4 and capstone manually (needed by tools/patch_nso.py)"
    }
}
Write-Host "dependency ready: $dep" -ForegroundColor Green
