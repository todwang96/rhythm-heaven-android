# Building

All steps run on Windows (PowerShell). The APK itself is plain `aapt2 + javac + d8 + apksigner`
made by `android/build_apk.py` — no Gradle required.

## 1. Toolchain

| What | Where |
|---|---|
| Android SDK (platform 35/36, build-tools 35/36) | `ANDROID_HOME` (or `ANDROID_SDK_ROOT`) |
| Android NDK r28 | `ANDROID_NDK_HOME` / `ANDROID_NDK_ROOT`, or `DM_CLANG` pointing at `clang.exe` directly |
| JDK 17 | `JAVA_HOME` |
| Python 3.10+ | `pip install -r requirements.txt` (done by `scripts/setup.ps1`) |
| hactool/nstool | extract game files (see `PREPARE-GAME.md`) |
| .NET 10 SDK | only for shader translation (`dmsh_xlate`) |

`android/build.py` finds the NDK clang automatically from `ANDROID_NDK_HOME`,
`ANDROID_NDK_ROOT`, or `$ANDROID_HOME/ndk/*` (newest first). `android/build_apk.py` picks the newest
build-tools and platform it finds in the SDK.

Extra environment variables:

| Variable | Meaning |
|---|---|
| `RH_WORK` | prepared-data directory (default `work/rhythm-heaven`) |
| `RH_GAME_DUMP` | your game dump directory (default `game-dump`) |
| `RH_SHADERS` | Vulkan shader cache (default `work/shaders/vk-cache`) |
| `DM_DEPENDENCY` | d2r-mobile checkout (default `.nitenphone/dependencies/d2r-mobile`) |
| `DM_CLANG` | explicit `clang.exe` |
| `DM_JAVA_HOME` | explicit JDK |

## 2. Commands

```powershell
# fetch d2r-mobile @ pinned commit + apply patches/d2r-mobile-android.patch
powershell -ExecutionPolicy Bypass -File scripts\setup.ps1

# after docs/PREPARE-GAME.md (game-dump/exefs + work/shaders/vk-cache exist)
powershell -ExecutionPolicy Bypass -File scripts\prepare-modules.ps1
powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -Release
powershell -ExecutionPolicy Bypass -File scripts\install.ps1 -Serial <adb serial>
```

Outputs:

- `android/build/libdmandroid.so` — native host library (arm64-v8a).
- `android/out/rhythm-heaven-android-release.apk` — release APK (~1.5 MB, version 1.0.0).
- `android/out/rhdata-pack.zip` — data package after `scripts\pack-data.ps1`.

Debug builds (with `-g`, debuggable manifest, `MicRecActivity` exported) are produced by
`python android/build_apk.py` without `--release`.

## 3. Device data layout

```
/sdcard/Android/media/com.rhport.rhythm/rhdata/
├── romfs.img          raw RomFS image (single file, preferred; ~3.2 GB)
├── romfs-view/        created at runtime: directory tree only (via romfs_image.c)
├── romfs/             alternative: extracted tree (fallback when romfs.img is absent)
├── modules/           <name>.patched.bin + *.tsv from prepare-modules.ps1
├── shaders/           work/shaders/vk-cache (shader_index.bin, *.spk, *.vkmeta)
├── env.txt            written by the launcher settings
└── save/, temp/, save.work/, run.log
```

`scripts\install.ps1` and `push_data.py` push `romfs.img` by default; pass `--tree` to
`push_data.py` to push the extracted `romfs/` tree instead.

## 4. First launch

1. Install the APK; it opens the launcher.
2. Empty data shows 「数据：不完整」. Either push data with `scripts\install.ps1`, or copy a
   `rhdata-pack.zip` to `Android/media/com.rhport.rhythm/` and use
   「游戏数据 → 导入数据包」 in the launcher.
3. Tap 「启动游戏」.

Env switches understood by the host (written into `rhdata/env.txt` by the launcher):
`DM_RT_SAMPLE_FLIP=1`, `DM_ASPECT_FIT` (0 stretch / 1 16:9 letterbox / 2 cover crop),
`DM_CROP_TOP` (0..100), `DM_HAPTIC_GAIN` (0..4, default 2.25), `DM_VIDEO_DELAY_MS` (0..200),
`DM_ROMFS_IMAGE=0` to force the directory RomFS mode.

## 5. Troubleshooting

- `STOP shader cache path (0)` — `rhdata/shaders` is missing; rerun `prepare-shaders`.
- «缺少游戏数据» page — romfs/modules/shaders incomplete; use the data page or push again.
- `ROMFS image setup failed` in logcat — the `romfs.img` is not a raw RomFS image (use the
  extraction command in `PREPARE-GAME.md`); the game then falls back to `rhdata/romfs/`.
- Runtime log: `rhdata/run.log` (stdout/stderr redirected), plus `adb logcat -s RHPORT`.
- Uninstalling the app deletes `Android/media/com.rhport.rhythm/rhdata` (re-import the package).
