# Build the APK. The native library is rebuilt unless -SkipNative is given.
# Usage: powershell -ExecutionPolicy Bypass -File scripts\build.ps1 [-Release] [-SkipNative]
param([switch]$Release, [switch]$SkipNative)
$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$args = @("build_apk.py")
if ($Release) { $args += "--release" }
if ($SkipNative) { $args += "--skip-native" }
Push-Location (Join-Path $repo "android")
try { python @args } finally { Pop-Location }
