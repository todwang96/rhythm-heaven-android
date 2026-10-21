# Add a game

Each game has its own configuration, adapter code, build folder, and save folder.
A new configuration does not make a game run automatically. Each game needs its own checks and adaptation.

## Create the configuration

```sh
.nitenphone/venv/bin/python internal/tools/games.py list
.nitenphone/venv/bin/python internal/tools/games.py new other-game --name "Other Game" --product-name OtherGame --bundle-id com.example.nitenphone.othergame --title-id 0100000000000000
.nitenphone/venv/bin/python internal/tools/games.py check
```

Replace the example Title ID with the real ID of the game.
`new` only creates the configuration and empty folders in `internal/games/other-game/`.
It does not copy the Silksong code, assets, saves, or signing.
The initial status is `not-adapted`. This status permits extraction and analysis. It prevents builds and runs.
The initial system versions are iOS 26 and macOS 26. Lower versions need separate verification.
Before it creates a configuration, the tool checks the app name, Bundle ID, and save name. If there is a conflict, it does not create the game folder.

## Put the files in place

| Path | Content |
| --- | --- |
| `internal/games/<id>/game.json` | App name, Bundle ID, source file lists, and adapters |
| `internal/games/<id>/profile.json` | Title ID, module hashes, and tool versions |
| `internal/games/<id>/runtime/` | Runtime adapter code for the game |
| `internal/games/<id>/ios/` | Icons and artwork for the game |
| `internal/games/<id>/tests/` | Verification for the game |
| `internal/runtime/common/` | Service implementations that more than one game uses |
| `internal/platform/ios/` | Shared iPhone app layer |
| `.nitenphone/work/<id>/base/` | Local ExeFS and RomFS |
| `.nitenphone/work/<id>/patched/` | Rewritten modules |
| `.nitenphone/work/<id>/shaders/` | Shaders for the game |
| `.nitenphone/work/<id>/reports/` | Analysis results |
| `.nitenphone/work/<id>/saves/<name>/` | Local test saves |
| `.nitenphone/work/<id>/temp/` | Local temporary files |
| `.nitenphone/build/games/<id>/` | Builds, logs, generated projects, and IPA files |

Keep local game files and keys in `MyGameFiles/`.
Do not put keys, extracted assets, or saves in `internal/games/`.
Each app uses its own Bundle ID and save name. Silksong keeps its existing ID and iPhone save path.

## Extract and analyze

```sh
.nitenphone/venv/bin/python internal/tools/prepare_game.py inspect MyGameFiles/other-game/base.nsp --game other-game
.nitenphone/venv/bin/python internal/tools/prepare_game.py extract MyGameFiles/other-game/base.nsp --game other-game --keys MyGameFiles/keys/prod.keys
.nitenphone/venv/bin/python internal/tools/analyze_game.py --game other-game
```

The extraction tool checks the Title ID of the Program. It refuses to write a different game into this folder.
The analysis report lists the modules and imported symbols. Identical symbol names do not prove ABI or system service compatibility.
Module analysis also works for games that do not use Unity.

Game updates are cumulative. You only need the latest update package. `extract-update` merges the update RomFS onto the base game. It verifies each block against the IVFC hash tree that the update contains:

```sh
.nitenphone/venv/bin/python internal/tools/prepare_game.py extract-update MyGameFiles/other-game/base.nsp MyGameFiles/other-game/update.nsp \
  --game other-game --keys MyGameFiles/keys/prod.keys --out .nitenphone/work/other-game/v65536
```

In `game.json`, set `content_dir` to the output folder name. Set `patched_dir` to a new rewrite folder name. If you omit them, the values are `base` and `patched`. An update changes the module hashes and fixed addresses. You must record them again.

## Add an adapter

1. Record the hash and Build ID of each original NSO module.
2. Set `runtime_modules` in `profile.json` and `module_names` in `game.json`. The two lists must be equal. Do not run the Switch `rtld` module.
3. Add the load, system service, graphics, and input adaptation that the game needs.
4. Implement `niten_game_install` and `niten_game_host_call`. Use the module hashes to guard changes at fixed addresses.
5. Fill in `runtime_sources` and `ios_sources`. These paths are relative to the game folder. Fill in `common_runtime_sources` and `common_ios_sources`. These paths are relative to the project root.
6. Add the code rewrite and shader procedure for this game. The strings in `adapters` identify the actual implementations. When you add an implementation, also update the dispatch in the tools.
7. Check graphics, input, audio, haptics, saves, exit, and save reload.
8. After the tests pass, change the status to `adapted`. Then run `internal/tools/games.py check --game other-game`.

The Silksong fixed-address adapter is only for Silksong. Do not copy it to bypass the checks for a new game.
`unity-switch` and `unity-nvn` are the current Unity shader procedures.
They need confirmation of the Unity version and the NVN asset layout of the game.
In `profile.json`, `shader_assets` and `shader_asset_globs` can specify the Data files to scan.
`profile.environment` can set runtime options. The keys and values must be strings.
The app and `run_host.py` set `DM_NVN_TBDR=0` by default. This is the simple path: each render pass loads all attachments and stores all attachments. First confirm that the picture is the same as on the default path. You can use `DM_NVN_RULER=1` to compare screenshots in the same process. Then you can set `DM_NVN_TBDR=1` here. This enables the render pass optimization, and uniform updates do not split the pass. Silksong has it enabled.
`DM_NVN_AMBIENT_BARS=0.8` enables the live soft glow in the letterbox border. 0 disables it. Rhythm Heaven has it enabled.
`SS_DISPLAY_HZ=120` makes the game run at 120 frames per second on a 120 Hz screen. The default is 60. Set it only when the game measures game time in real time. First, on the Mac host, compare the gameplay at 60 Hz and 120 Hz with the same steps. A game that measures game time in frames (for example, Rhythm Heaven) becomes faster. On the iPhone, the app goes back to 60 frames per second automatically when the screen drops to 60 Hz. Silksong has it enabled.
The workspace sets the game asset, save, shader, input, and temporary paths. This field cannot override them.

## Build an adapted game

These commands need an implemented adapter that passed its checks.

```sh
.nitenphone/venv/bin/python internal/tools/rewrite_game.py --game silksong
.nitenphone/venv/bin/python internal/tools/extract_shaders.py --game silksong
.nitenphone/venv/bin/python internal/tools/build_shaders.py --game silksong --ios
bash internal/tools/build_host.sh --game silksong
.nitenphone/venv/bin/python internal/tools/prepare_ios.py --game silksong
```

The current tools select `silksong` by default.
The analysis tool still accepts the extraction path as a positional argument.
The rewrite tool still accepts the old `--game <extraction path>` form. This form selects the Silksong adapter.
The new form is `--game <game ID> --input <extraction path>`.
The extraction and rewrite tools refuse to overwrite existing output.

The project reorganization keeps the existing game assets, iPhone app IDs, and save paths.
Old development outputs stay through migration records or compatibility links.
