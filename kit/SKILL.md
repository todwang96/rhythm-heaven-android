---
name: rhythm-heaven-package
description: Build the Rhythm Heaven Android data package on a PC from the user's own Switch game files, install the APK, and get the game running on their Android phone or tablet. Use when the user says things like "use the rhythm-heaven-package skill to build my package and install it" or asks how to prepare/install Rhythm Heaven data.
---

# Rhythm Heaven data package skill

This kit turns **the user's own Rhythm Heaven dump** into the data the Android port needs, then
installs everything on their device. It never downloads or redistributes game content.

## What is in the kit

```
README.txt                 one-page quick start (Chinese + English)
SKILL.md                   this file
package.ps1                build out/rhdata-pack.zip from game-dump/
install.ps1                install the APK and push/import the data package
apk/                       rhythm-heaven-android-release.apk
source/                    the port source (host code, scripts, docs, MIT)
game-dump/                 put the user's extracted game files here
```

## Prerequisites (check them first)

1. **Game files**: `game-dump/exefs/{main,sdk}` and `game-dump/romfs.img` exist (the user must
   extract them from their own XCI/NSP; see `source/docs/PREPARE-GAME.md`).
2. **Python 3.10+** with `lz4` and `capstone` (`pip install -r source/requirements.txt`).
3. **Windows PowerShell 5.1+**, **Android SDK/NDK/JDK** for building the APK if it is not already
   built (`ANDROID_HOME`, `ANDROID_NDK_HOME`, `JAVA_HOME`).
4. **Git + network** for `source/scripts/setup.ps1` (fetches the pinned d2r-mobile dependency;
   set `HTTP_PROXY`/`HTTPS_PROXY` if github.com is blocked).
5. **Shader translation**: needs .NET 10 SDK once, for `dmsh_xlate`. If `work/shaders/vk-cache`
   already exists you can skip it.
6. **adb** + a device with USB debugging authorised.

## Steps

Run everything from the kit root.

```powershell
# 1. dependency + python packages
powershell -ExecutionPolicy Bypass -File source\scripts\setup.ps1        # -Force to re-fetch

# 2. patched guest modules (main/sdk) from game-dump/exefs
powershell -ExecutionPolicy Bypass -File source\scripts\prepare-modules.ps1 -Dump game-dump

# 3. shader cache (only if work\shaders\vk-cache is missing; see PREPARE-GAME.md)
powershell -ExecutionPolicy Bypass -File source\scripts\prepare-shaders.ps1

# 4. data package for the launcher: out\rhdata-pack.zip
powershell -ExecutionPolicy Bypass -File package.ps1

# 5. install on the device
powershell -ExecutionPolicy Bypass -File install.ps1 -Serial <adb serial>
```

`install.ps1` installs `apk/rhythm-heaven-android-release.apk`, copies `out/rhdata-pack.zip` to
`Android/media/com.rhport.rhythm/`, then the user opens the app and taps
**游戏数据 → 导入数据包** (Data → Import package). If `adb push` of the extracted data is preferred,
see `source/scripts/install.ps1` (`-SkipApk`/`-SkipData`).

## After install

- Launcher → **启动游戏**. Settings: picture ratio, haptic strength, picture delay
  (A/V sync), beat calibration (for Bluetooth headsets).
- If the launcher shows 「数据：不完整」, the import did not finish: copy the zip again and import.
- Runtime log: `adb pull /sdcard/Android/media/com.rhport.rhythm/rhdata/run.log`.

## Rules

- Never upload, share or commit `game-dump/`, `work/`, `out/*.zip` or `rhdata-pack.zip` — they
  contain the user's game data.
- Do not redistribute patched modules, `romfs.img`, translated shaders or keys.
- If the user's game version differs from v0 (`0100D9F01D474000`), stop and explain that the
  profile/build IDs must be adapted first.
