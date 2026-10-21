# Rhythm Heaven Android

Runs the **original ARM64 Switch modules of Rhythm Heaven** (节奏天国 / リズム天国) natively on Android:
the game's own code executes on the device, the NVN graphics layer is reimplemented on top of
**Vulkan**, audio goes through **AAudio**, and touch controls + a liquid-glass launcher are rebuilt
for phones and tablets.

This repository contains **only the host/port code and tools — no game content**. It is a
"bring your own game files" project, in the same spirit as
[d2r-android](https://github.com/sydeEvans/d2r-android). You need your own copy of the game
(Switch v0, title ID `0100D9F01D474000`) and your own `prod.keys` to extract it.

> Not affiliated with Nintendo. Rhythm Heaven is a trademark of Nintendo. Use only with game files
> you legally own.

## Status

- Playable on Android 15/16 devices with a recent Adreno GPU (tested on a vivo phone and a
  Xiaomi Pad 7 Pro; 60 fps in menus, stage intro and gameplay scenes measured so far).
- Touch controls: sector D-pad on the left, A/B/X/Y + `+`/`-` on the right, press feedback,
  auto-hiding D-pad outside gameplay.
- Launcher: full-screen key-art background (placeholder art in this repo — put your own image in),
  right-side liquid-glass panel, data-pack import, aspect ratio / haptic strength / picture-delay /
  beat-calibration settings.
- Data: RomFS is read from a **single `romfs.img`** (raw RomFS image) instead of ~7000 files;
  modules and shaders are prepared locally and never redistributed.
- Picture delay (A/V sync) presets + tap-along beat calibration for Bluetooth headsets.
- Not yet: automated game-file preparation for every game version, full level-by-level QA,
  non-Windows build scripts.

## Requirements

| Component | Version |
|---|---|
| OS | Windows 10/11 (build scripts are PowerShell; porting them to macOS/Linux is appreciated) |
| Python | 3.10+ |
| Android SDK | platform 35/36 + build-tools 35/36 |
| Android NDK | r28 (`ANDROID_NDK_HOME`) |
| JDK | 17 (`JAVA_HOME`) |
| Game extraction | [hactool](https://github.com/SciresM/hactool) or [nstool](https://github.com/jakcron/nstool) |
| Shader translation | .NET 10 SDK (for `dmsh_xlate` inside d2r-mobile) |
| Game dump | XCI/NSP of Rhythm Heaven v0 + `prod.keys` |

## Quick start

```powershell
git clone https://github.com/<you>/rhythm-heaven-android
cd rhythm-heaven-android

# 1) fetch the pinned d2r-mobile dependency and apply the Android patch
powershell -ExecutionPolicy Bypass -File scripts\setup.ps1

# 2) put YOUR extracted game files under game-dump\
#    game-dump\exefs\main, game-dump\exefs\sdk, game-dump\romfs.img
#    (see docs/PREPARE-GAME.md for the extraction + shader commands)

# 3) prepare modules, build the APK, install
powershell -ExecutionPolicy Bypass -File scripts\prepare-modules.ps1
powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -Release
powershell -ExecutionPolicy Bypass -File scripts\install.ps1
```

To build an importable data package for the launcher (romfs.img + modules + shaders):

```powershell
powershell -ExecutionPolicy Bypass -File scripts\pack-data.ps1
```

## Repository layout

| Path | Contents |
|---|---|
| `android/` | Android host: native frontend (`src/`, `compat/`), Java UI + launcher (`app/`), build scripts |
| `Touchable/` | Game adaptation runtime (vendored from [Touchable](https://github.com/MZhao-ouo/Touchable), MIT) |
| `patches/` | `d2r-mobile-android.patch`: Android platform layer + Vulkan backend changes against the pinned dependency |
| `tools/` | NSO rewriter and shader-cache tools (see `NOTICE`) |
| `scripts/` | setup / prepare / build / pack / install helpers |
| `docs/` | build, game-preparation and architecture notes |
| `game-dump/`, `work/`, `.nitenphone/` | **not committed** — your game files, prepared data, fetched dependency |

## How it works (short)

```
Rhythm Heaven main/sdk (ARM64 Switch modules, rewritten locally)
        │  HLE: nn:: services, audio, input, filesystem
        ▼
d2r-mobile host layer  (loader, HLE, save FS, time)
        │  NVN command stream
        ▼
nvn_vk.c  →  Vulkan (Adreno)          AAudio ← ss_audio_*     Android input ← PadView
```

- The Switch modules are rewritten with `tools/patch_nso.py` into position-independent images that
  the host loader maps and runs natively; no emulation of the CPU.
- RomFS is mounted through `romfs_image.c`: a single raw RomFS image is exposed as a read-only
  directory tree; file payloads are read with `pread` from the image.
- `nvn_vk.c` translates NVN draw/pipeline state to Vulkan, including the present path, with
  optional picture delay for A/V sync.
- The launcher and in-game touch controls are plain Android Views (no Gradle/Compose), built with
  `aapt2 + javac + d8`.

## Legal

This project does not contain, and must not be used to distribute, game ROMs, decrypted NCA/RomFS
content, `prod.keys`, or patched game binaries. You must own the game. No warranty of any kind;
see `LICENSE`.

## Credits

- [Touchable](https://github.com/MZhao-ouo/Touchable) (MIT) — game adaptation runtime.
- [d2r-mobile](https://github.com/jhaizhou-ops/d2r-mobile) (MIT) — HLE host layer, Vulkan NVN backend.
- and the Ryujinx/dmsh_xlate projects used for shader translation.

See `NOTICE` for details. This port's own code is MIT (see `LICENSE`).
