Rhythm Heaven Android - PC data package kit / PC 端数据包工具包
================================================================

This kit builds the data package for the Rhythm Heaven Android port from YOUR OWN game dump,
then installs it on your phone/tablet. It contains no game data.

本工具包用你自己合法拥有的游戏文件，在电脑上生成 Android 移植版所需的数据包，
并把它装到手机/平板上。工具包本身不含任何游戏内容。

Contents / 内容
---------------
  README.txt       this file
  SKILL.md         instructions for AI agents (Claude Code etc.)
  package.ps1      build out\rhdata-pack.zip from game-dump\
  install.ps1      install APK + get the data package onto the device
  apk\             rhythm-heaven-android-release.apk (no game data)
  source\          the port source code (MIT) - scripts, docs, android host
  game-dump\       put YOUR extracted game files here

What you need / 需要准备
------------------------
  * Your own Rhythm Heaven v0 (Switch, title ID 0100D9F01D474000) XCI/NSP + prod.keys
  * hactool or nstool to extract it (see source\docs\PREPARE-GAME.md)
  * Windows 10/11, Python 3.10+ (pip install -r source\requirements.txt)
  * Android SDK + NDK r28 + JDK 17 only if you want to rebuild the APK
  * .NET 10 SDK only for shader translation (skip if work\shaders\vk-cache already exists)
  * adb + a device with USB debugging

Quick start / 快速开始
----------------------
  1. Extract your game into game-dump\ :
       game-dump\exefs\main
       game-dump\exefs\sdk
       game-dump\romfs.img          (raw RomFS image; see PREPARE-GAME.md)

  2. PowerShell, in this folder:

       powershell -ExecutionPolicy Bypass -File source\scripts\setup.ps1
       powershell -ExecutionPolicy Bypass -File source\scripts\prepare-modules.ps1 -Dump game-dump
       powershell -ExecutionPolicy Bypass -File source\scripts\prepare-shaders.ps1   # if needed
       powershell -ExecutionPolicy Bypass -File package.ps1
       powershell -ExecutionPolicy Bypass -File install.ps1 -Serial <adb serial>

  3. On the device: open the app -> 游戏数据 -> 导入数据包 (Data -> Import package),
     then tap 启动游戏.

Notes / 注意
------------
  * Do not share game-dump\, work\ or rhdata-pack.zip - they contain your game data.
  * The port supports the tested v0 build only; other versions need profile updates.
  * If github.com is blocked, set HTTP_PROXY/HTTPS_PROXY before running setup.ps1.
  * Full documentation: source\docs\ (BUILD.md, PREPARE-GAME.md, ARCHITECTURE.md).
