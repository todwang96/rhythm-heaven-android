# 生成启动器导入用的数据包：romfs/ + modules/ + shaders/ 打成一个 zip。
# 用法：powershell -ExecutionPolicy Bypass -File make_data_pack.ps1 [-Out <zip路径>] [-Compress]
# 默认不压缩（store）：打包/导入都快很多，体积约等于原始大小。
param(
    [string]$Out = "",
    [switch]$Compress,
    [string]$RomfsImage = "",
    [switch]$Tree
)
$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path   # rh-port/android
$port = Split-Path -Parent $here
$gameDump = if ($env:RH_GAME_DUMP) { $env:RH_GAME_DUMP } else { Join-Path $port "game-dump" }
if (-not $RomfsImage) { $RomfsImage = Join-Path $gameDump "romfs.img" }
$romfs   = Join-Path $gameDump "romfs"
$work    = if ($env:RH_WORK) { $env:RH_WORK } else { Join-Path $port "work\rhythm-heaven" }
$modules = Join-Path $work "patched-android"
$shaders = if ($env:RH_SHADERS) { $env:RH_SHADERS } else { Join-Path $port "work\shaders\vk-cache" }
if (-not $Out) { $Out = Join-Path $here "out\rhdata-pack.zip" }
if (Test-Path $Out) { Remove-Item $Out -Force }
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Out) | Out-Null

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

$level = if ($Compress) { [System.IO.Compression.CompressionLevel]::Optimal } else { [System.IO.Compression.CompressionLevel]::NoCompression }
$zip = [System.IO.Compression.ZipFile]::Open($Out, [System.IO.Compression.ZipArchiveMode]::Create)

$sw = [System.Diagnostics.Stopwatch]::StartNew()
$count = 0
$bytes = [long]0
$buf = New-Object byte[] (1MB)

function Add-Tree($srcDir, $arcPrefix) {
    if (-not (Test-Path $srcDir)) { Write-Host "跳过（不存在）：$srcDir" -ForegroundColor Yellow; return }
    $files = Get-ChildItem -Path $srcDir -Recurse -File
    $script:total += $files.Count
    foreach ($f in $files) {
        $rel = $f.FullName.Substring($srcDir.Length).TrimStart('\', '/').Replace('\', '/')
        $entryName = "$arcPrefix/$rel"
        $entry = $zip.CreateEntry($entryName, $level)
        $entry.LastWriteTime = $f.LastWriteTime
        $in = [System.IO.File]::OpenRead($f.FullName)
        try {
            $outStream = $entry.Open()
            try {
                while (($n = $in.Read($buf, 0, $buf.Length)) -gt 0) {
                    $outStream.Write($buf, 0, $n)
                    $script:bytes += $n
                }
            } finally { $outStream.Dispose() }
        } finally { $in.Dispose() }
        $script:count++
        if (($script:count % 500) -eq 0) {
            Write-Host ("  {0}/{1} 个文件, {2:N2} GB, {3:N0}s" -f $script:count, $script:total, ($script:bytes/1GB), $sw.Elapsed.TotalSeconds)
        }
    }
}

$script:total = 0
if ((Test-Path $RomfsImage) -and -not $Tree) {
    Write-Host "[1/3] 单文件 romfs.img（$RomfsImage）..." -ForegroundColor Cyan
    $fi = Get-Item $RomfsImage
    $script:total += 1
    $entry = $zip.CreateEntry("romfs.img", $level)
    $entry.LastWriteTime = $fi.LastWriteTime
    $in = [System.IO.File]::OpenRead($fi.FullName)
    try {
        $outStream = $entry.Open()
        try {
            while (($n = $in.Read($buf, 0, $buf.Length)) -gt 0) {
                $outStream.Write($buf, 0, $n)
                $script:bytes += $n
            }
        } finally { $outStream.Dispose() }
    } finally { $in.Dispose() }
    $script:count++
    Write-Host ("  romfs.img {0:N2} GB" -f ($fi.Length/1GB))
} else {
    Write-Host "[1/3] romfs 目录（$romfs）..." -ForegroundColor Cyan
    Add-Tree $romfs "romfs"
}
Write-Host "[2/3] modules ..." -ForegroundColor Cyan
$modDir = Join-Path $port "out\modpack-tmp"
if (Test-Path $modDir) { Remove-Item $modDir -Recurse -Force }
New-Item -ItemType Directory -Force -Path $modDir | Out-Null
Get-ChildItem $modules -File | Where-Object { $_.Name -match '\.patched\.bin$' -or $_.Name -match '\.tsv$' } |
    ForEach-Object { Copy-Item $_.FullName (Join-Path $modDir $_.Name) }
Add-Tree $modDir "modules"
Remove-Item $modDir -Recurse -Force
Write-Host "[3/3] shaders ..." -ForegroundColor Cyan
Add-Tree $shaders "shaders"

$zip.Dispose()
$sw.Stop()
$size = (Get-Item $Out).Length
Write-Host ""
Write-Host ("完成：{0}  ({1:N2} GB, {2:N0}s)" -f $Out, ($size/1GB), $sw.Elapsed.TotalSeconds) -ForegroundColor Green
Write-Host "把该 zip 放到设备的 Android/media/com.rhport.rhythm/ 目录（USB 拷贝），" -ForegroundColor Yellow
Write-Host "或用 adb push 到 /sdcard/Android/media/com.rhport.rhythm/rhdata-pack.zip，然后在启动器里点「导入数据包」。" -ForegroundColor Yellow
