# Build the PC-side kit zip: kit template + prebuilt APK + source snapshot.
# Usage: powershell -ExecutionPolicy Bypass -File scripts\make-kit.ps1 [-Version 0.1.0] [-Apk <path>]
param([string]$Version = "0.1.0", [string]$Apk = "", [string]$Out = "")
$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$kitDir = Join-Path $repo "kit"
if (-not (Test-Path $kitDir)) { throw "kit\ template missing" }
if (-not $Apk) { $Apk = Join-Path $repo "android\out\rhythm-heaven-android-release.apk" }
if (-not (Test-Path $Apk)) { throw "APK missing: $Apk (build it with scripts\build.ps1 -Release)" }
if (-not $Out) {
    New-Item -ItemType Directory -Force -Path (Join-Path $repo "out") | Out-Null
    $Out = Join-Path $repo ("out\rhythm-heaven-kit-{0}.zip" -f $Version)
}
$stage = Join-Path ([System.IO.Path]::GetTempPath()) ("rh-kit-" + $PID)
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force -Path $stage | Out-Null

Write-Host "[1/4] copy kit template"
Copy-Item (Join-Path $kitDir "*") $stage -Recurse -Force
New-Item -ItemType Directory -Force -Path (Join-Path $stage "apk") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $stage "source") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $stage "out") | Out-Null

Write-Host "[2/4] add APK"
Copy-Item $Apk (Join-Path $stage "apk\rhythm-heaven-android-release.apk") -Force

Write-Host "[3/4] snapshot source (git archive HEAD)"
$sourceZip = Join-Path $stage "source.zip"
git -C $repo archive --format=zip --prefix=source/ -o $sourceZip HEAD
if ($LASTEXITCODE -ne 0) { throw "git archive failed" }
Expand-Archive -Path $sourceZip -DestinationPath $stage -Force
Remove-Item $sourceZip -Force
Set-Content -Encoding ascii -Path (Join-Path $stage "VERSION.txt") -Value ("rhythm-heaven-kit " + $Version)

Write-Host "[4/4] zip"
if (Test-Path $Out) { Remove-Item $Out -Force }
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zipOut = [System.IO.Compression.ZipFile]::Open($Out, [System.IO.Compression.ZipArchiveMode]::Create)
try {
    $root = (Resolve-Path $stage).Path
    Get-ChildItem $stage -Recurse -File | ForEach-Object {
        $rel = $_.FullName.Substring($root.Length + 1).Replace("\", "/")
        $entry = $zipOut.CreateEntry($rel, [System.IO.Compression.CompressionLevel]::Optimal)
        $entry.LastWriteTime = $_.LastWriteTime
        $in = [System.IO.File]::OpenRead($_.FullName)
        try {
            $outStream = $entry.Open()
            try { $in.CopyTo($outStream) } finally { $outStream.Dispose() }
        } finally { $in.Dispose() }
    }
} finally { $zipOut.Dispose() }
Remove-Item -Recurse -Force $stage

$size = (Get-Item $Out).Length
Write-Host ("kit ready: {0}  ({1:N2} MB)" -f $Out, ($size / 1MB)) -ForegroundColor Green
