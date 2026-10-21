# Architecture

## Layers

```
┌──────────────────────────────────────────────────────────────────────────┐
│ android/app (Java, no Gradle)                                            │
│  LauncherActivity  GameActivity  PadView  GlassPanel  Settings           │
├──────────────────────────────────────────────────────────────────────────┤
│ android/src (native host front-end)                                      │
│  app_android.c  → ANativeWindow/Surface, libdmandroid.so entry,          │
│                   configure root dirs, env.txt, data/image mount, haptics│
├──────────────────────────────────────────────────────────────────────────┤
│ Touchable/internal/… (game adaptation runtime, MIT)                      │
│  ss_*.c   services (fs/audio/renderer/vi/touch/…) HLE for Rhythm Heaven  │
│  rh_*.c   game-specific: rh_game, rh_touch, rh_menu, rh_batch (776 lines)│
├──────────────────────────────────────────────────────────────────────────┤
│ .nitenphone/dependencies/d2r-mobile (fetched by scripts/setup.ps1)       │
│  dm/host   loader, HLE, thread slots, save FS  (MIT)                     │
│  dm/gfx/nvn/nvn_vk.c  NVN command stream → Vulkan                        │
│  dm/gfx/tex  texture/swizzle, dm/host/*.c service HLE                    │
│  dm/android  romfs_image.c, services_fs.c, platform clock, haptics hooks │
└──────────────────────────────────────────────────────────────────────────┘
```

The game's own `main`/`sdk` ARM64 modules (rewritten locally into position-independent images) are
mapped and executed natively by `dm/host/loader.c`; `Touchable` supplies the game-specific entry
points and frame loop; `nvn_vk.c` replays the NVN command stream on Vulkan.

## Boot flow (Android host)

1. `GameActivity.onCreate` → `nativeConfigure(dataDir, …)`:
   - loads `rhdata/env.txt` (`DM_*` switches written by the launcher),
   - creates `romfs/`, `modules/`, `shaders/`, `save/`, `temp/`,
   - **RomFS image**: if `rhdata/romfs.img` exists, `romfs_image_setup_checked(img, rhdata/romfs-view)`
     sets `DM_ROMFS` to the view root (directories only); otherwise `DM_ROMFS=rhdata/romfs`,
   - redirects stdout/stderr to `rhdata/run.log`.
2. `nativeStart(surface)` creates the ANativeWindow, starts the `ss_host_run()` thread.
3. Game modules are loaded from `rhdata/modules/*.patched.bin`; shaders from `rhdata/shaders`.
4. Exit code / crash info is written to `run.log`; the Java side restarts the activity on demand.

## Graphics

- `nvn_vk.c` implements the NVN → Vulkan mapping used by d2r-mobile: pipelines, descriptors, render
  targets, present path. Additional switches in this port:
  - `DM_ASPECT_FIT`: 0 = stretch, 1 = 16:9 letterbox (default), 2 = cover-crop.
  - `DM_CROP_TOP` (0..100): vertical bias for cover-crop.
  - `DM_VIDEO_DELAY_MS` / `DM_VIDEO_DELAY_FRAMES`: present delay to align visuals with audio.
  - `DM_RT_SAMPLE_FLIP`: vertical flip for sampled render targets (required here).
  - `DM_PERF`: periodic `RENDER_PROFILE` lines (fps, submits, draws, MiB/frame).
- The game renders 1920×1080 internally; the host upscales to the display (nearest/linear by
  backend), with optional letterboxing.

## Audio

`Touchable`'s renderer (`ss_audio_renderer.c`, ring buffer) feeds `ss_audio_aaudio.c` (AAudio,
performance mode LOW_LATENCY where the device allows it; vivo falls back to a 100 ms deep buffer).
Because the audio output latency differs per device, the launcher offers:
- **picture delay** (`DM_VIDEO_DELAY_MS`, presets 0/30/50/70/100/150/200 ms) — delays presentation to
  match the audible audio (judgements are unaffected),
- **beat calibration** — plays a 120 BPM click track with `AudioTrack`, maps click frames to audible
  time via `AudioTrack.getTimestamp()`, and averages the user's tap offsets to suggest a preset.

## Input

`PadView` (Java) draws and hit-tests the whole touch UI; native input comes from `ss_touch.c`
(HID HLE). Layout:
- left half: sector D-pad (dominant axis, 12 dp dead zone, 30 ms latch, fades after 6 s outside
  gameplay),
- right half: A only (pointer key `-4` for ignored touches on the left half),
- B/X/Y, `+`/`-` buttons; press feedback via an active-key mask,
- global `uiScale = clamp(sqrt(rawW*rawH/(792*360)), 1.0, 1.7)` and dp-based layout.

## Launcher

Plain Android Views with a custom "liquid glass" panel (`GlassPanel`):
- `CENTER_CROP`-aligned blurred backdrop + milky-white tint inside a 32 dp rounded rect;
- AGSL `RuntimeShader` + `RenderEffect` (API 33+) displaces the frosted content in a 36 dp edge band
  (refraction) with a specular rim; the view is fully opaque so rounded corners never show driver
  artifacts;
- data pages import a zip (`romfs/` or `romfs.img`, `modules/`, `shaders/`) into
  `Android/media/com.rhport.rhythm/rhdata/`;
- settings are persisted in `SharedPreferences` and written to `rhdata/env.txt`.

## Data package

`rhdata-pack.zip` contains only content derived from the user's own dump:
`romfs.img` (single raw RomFS image), `modules/*.patched.bin + *.tsv`, `shaders/*`
(~87 files, 3.2 GB, store mode). The launcher extracts it to `rhdata/`, then the native host mounts
the image. No keys and no game container are included.

## Known limitations

- Only the tested v0 build IDs are supported; other versions need profile/rewrite updates.
- RomFS image validation tolerates both "metadata before data" and "data first, metadata last"
  layouts (the latter is what this game's NCA yields).
- Untested: multiplayer/local-play modes, every minigame, controller input.
