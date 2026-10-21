# Preparing your own game files

This project never ships game content. You need your own Rhythm Heaven dump and keys. The commands
below are examples, not a download source. The tested version is **Switch v0**,
title ID `0100D9F01D474000` (keys with keygen 21 / SDK 21.4.5; `hactool` from 2020 cannot read it —
use a recent hactool or `nstool`).

## 0. What you need

```
Rhythm Heaven v0 (XCI or NSP)   # your copy
prod.keys                        # from your own console
hactool / nstool                 # extraction
```

Keep `prod.keys` outside this repository (e.g. `D:\keys\prod_clean.keys`). Never commit it.

> Some `prod.keys` dumps contain malformed lines (e.g. 17-byte `mariko_master_kek_source_*`) and
> old hactool refuses to start. Filter to valid 32-hex entries into a clean keyset first.

## 1. Extract the Program NCA

XCI:

```powershell
hactool -k <keys> -t xci --secure=secure_dir RhythmHeaven.xci
hactool -k <keys> -t nca --info secure_dir\*.nca   # find "Content Type: Program"
```

NSP:

```powershell
hactool -k <keys> -t pfs0 --outdir=nsp_out RhythmHeaven.nsp
# then `hactool -k <keys> -t nca --info` each .nca and keep the Program one
```

Expected for v0: the Program NCA is ~3.43 GB (`0xCC930000` content size).

## 2. Extract ExeFS and the raw RomFS image

```powershell
$nca = "secure_dir\e5c29032516440081d67d515d8bf9397.nca"   # your Program NCA
hactool -k <keys> -t nca --exefsdir=game-dump\exefs $nca
hactool -k <keys> -t nca --romfs=game-dump\romfs.img $nca
```

Result:

```
game-dump/
├── exefs/
│   ├── main          # NSO, ~10 MB
│   ├── sdk           # NSO, ~6 MB
│   ├── rtld
│   └── main.npdm
└── romfs.img         # raw RomFS image, 3,408,967,208 bytes for v0
```

`romfs.img` is mounted by `d2r-mobile/dm/android/romfs_image.c`; if you prefer an extracted tree you
can also run `hactool -t romfs --outdir=game-dump\romfs game-dump\romfs.img` and use
`push_data.py --tree` instead.

## 3. Patch the modules

```powershell
powershell -ExecutionPolicy Bypass -File scripts\prepare-modules.ps1 -Dump game-dump
```

This runs `tools/patch_nso.py` for `main` and `sdk` with:

```
DM_TARGET=android DM_RULESET=native DM_PRIVATE_GATES=0 DM_INDIRECT_TRAPS=1
```

and writes `work/rhythm-heaven/patched-android/`:

```
main.patched.bin  main.cuts.tsv  main.gates.tsv  main.sites.tsv
sdk.patched.bin   sdk.cuts.tsv   sdk.gates.tsv   sdk.sites.tsv
main.fixups.tsv   main.indirect.tsv  main.obs.tsv  main.params.tsv  main.veneers.tsv
...
```

It also generates `work/rhythm-heaven/host/niten_game_config.h` (game ID, module list, title id).

> Module build IDs must match `Touchable/internal/games/rhythm-heaven/game.json`. A different game
> version will not run without adapting the game profile.

## 4. Translate the shaders

Rhythm Heaven has a single `shader/shader_NX.bnsh` under RomFS (35 shader stages). The pipeline is
the Touchable one:

1. **Extract** the shader binaries with
   `Touchable/internal/tools/extract_shaders.py` (see `Touchable/docs/quick-start.md`) — this
   produces `work/shaders/capture/<key>.control` and `.code`.
2. **Translate** Maxwell → SPIR-V with `dmsh_xlate` from `d2r-mobile/dm/gfx/tools`
   (needs .NET 10 SDK; `dotnet publish` as described in `d2r-mobile/dm/gfx/README.md`), writing
   `work/shaders/spv/<key>.spv` and `.res.json`.
3. **Build the runtime cache**:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\prepare-shaders.ps1
```

Output: `work/shaders/vk-cache/` (~72 files: `shader_index.bin`, `*.spk`, `*.vkmeta`). The host
loads this directory; `android/push_data.py` pushes it to the device as `rhdata/shaders/`.

## 5. Build and run

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -Release
powershell -ExecutionPolicy Bypass -File scripts\install.ps1 -Serial <serial>
```

Or make a data package for the launcher:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\pack-data.ps1
```

## Notes

- With a valid setup the runtime log prints `ROMFS image mounted: ... (3408967208 bytes)`.
- `rhdata/romfs/` (tree mode) and `rhdata/romfs.img` can both exist; the image wins. Set
  `DM_ROMFS_IMAGE=0` in `rhdata/env.txt` to force tree mode.
- Don't redistribute the extracted/patched files: `game-dump/`, `work/` and `.nitenphone/` are
  git-ignored on purpose.
