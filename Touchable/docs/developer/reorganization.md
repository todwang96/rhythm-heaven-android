# Workspace reorganization: 2026-10-17

## User entry points

Use `Touchable.command` at the repository root.
Select a game.
Select a build, installation, or check operation.
Put source packages in `MyGameFiles/<id>/`.
Put `prod.keys` in `MyGameFiles/keys/`.
Read completed packages in `IPA/<id>/`.

## Path map

| Previous location | Current location |
| --- | --- |
| `games/`, `runtime/`, `platform/` | The same directories under `internal/` |
| `tools/`, `tests/`, `patches/`, `requirements.txt` | The same paths under `internal/` |
| Developer documents in `docs/` | `docs/developer/` |
| `work/` | `.nitenphone/work/` |
| Game save directories under `work/<id>/saves/` | `.nitenphone/saves/<id>/`; local compatibility links remain |
| `build/` | `.nitenphone/build/` |
| `build/games/<id>/ipa/` | `IPA/<id>/` |
| `upstream/d2r-mobile/` | `.nitenphone/dependencies/d2r-mobile/` |
| `tools/vendor/*`, `tools/dotnet/` | `.nitenphone/dependencies/` |
| `.venv/` | New `.nitenphone/venv/`; the prior environment remains archived locally |
| `MyGameFiles/RhythmHeaven/` | `MyGameFiles/rhythm-heaven/` |

Application Bundle IDs, phone save namespaces, module hashes, and game compatibility status remain unchanged.
App builds increase to Silksong 9 and Rhythm Heaven 16.
Local settings preserve the existing Bundle IDs.
Raw packages and keys remain unchanged.
Existing apps and test evidence remain retained.
New IPAs are published to the local output directory only after signature and archive checks.
Prior IPAs move to `.nitenphone/archive/ipa/` after a successful replacement.
They are not deleted.

`ROOT` in `internal/tools/workspace.py` is the source root.
`PROJECT_ROOT` is the repository root.
`DEPENDENCIES` is the local dependency directory.
`state_root()` supplies work, build, and save paths.
`NITENPHONE_STATE_DIR` selects an isolated rebuild directory.
The launcher uses this setting for a full rebuild.
Local signing settings stay in the main `.nitenphone/config.json`.
A generated header selects each build's shader chain.
The Xcode project and host generator use that header before the source header.

## Validation

The baseline recorded 104,627 local files and 324 SHA-256 values.
It also recorded 34 passing Python tests.
New tests check input selection, local identity isolation, cache invalidation, invalid IPA rejection, menu input, failed backup handling, and idle phone reconnection.
Shader key tests check code, constants, stage, boundaries, workgroups, and C/Python parity.

Full rebuilds start from user-provided NSP files in new work directories.
They do not reuse the active extracted modules or shader libraries.
Pinned tool dependencies are checked separately in a clean copy.

Detailed results are recorded in `.nitenphone/work/silksong/reports/reorganization-20261017/`.
The Rhythm Heaven record links to the same shared validation.
Build output and stage logs remain under `.nitenphone/rebuilds/`.
First-launch tool logs remain under `.nitenphone/build/tooling/logs/`.

The user confirmed that a Finder double-click opens the game selection menu.
That check used the previous launcher name, `Nitenphone.command`.
See [the rename record](project-rename.md) for the current launcher name.
Interactive terminal checks also exercise game selection, operation selection, and installation.
Desktop automation did not control Finder or Terminal.

| Check | Result |
| --- | --- |
| Python tools | 47 tests passed |
| Native checks | Shader-key C/Python parity and Silksong display test passed |
| Game scripts | Active v655360 viewport and retained v0 world-projection checks passed |
| Clean dependency setup | Passed in a fresh source copy with spaces in its path |
| Fresh NSP builds | Silksong 9 and Rhythm Heaven 16 passed extraction, CPU checks, shader builds, signing, and IPA checks |
| Mac startup | Both games produced frames with isolated saves |
| iPhone installation | Both apps installed through the interactive launcher |
| Save comparison | All 24 checked save-related files matched before and after installation |
| iPhone startup | Both apps produced frame timing reports and remained alive until the check stopped them |

The installed versions are Silksong 9 and Rhythm Heaven 16.
The device was an iPhone 16 Pro Max with iOS 27.2.
The build environment used Xcode 27.0.
The phone checks did not send gameplay input.
They do not establish complete-game success.

Protected-file checks passed 82,768 metadata comparisons and 323 SHA-256 comparisons.
Finder changed one `.DS_Store` file.
That file is excluded from the protected comparison.
Rebuilt dependencies, generated projects, host objects, and rolling logs are outside that comparison.
Original packages, keys, saves, apps, and IPAs remain retained.

Actual signing used the existing local development certificate and compatible profiles.
The new-user automatic signing path passes the selected device to Xcode.
It permits provisioning updates and device registration.
These flags were checked against local `xcodebuild -help`.
A separate Apple account has not been tested.
GitHub publication and distribution licensing remain separate work.

## Clean-build shader fix

A clean Silksong boot exposed missing shader inputs.
The old workspace had retained runtime captures.
Static Unity extraction alone produced 2,376 controls.
The extractor now also reads embedded shader programs from verified NSO images.
It checks module hashes and Build IDs before this step.

The shader map can include a full SHA-256 of the translator inputs.
This identity includes instruction bytes, constant bytes, stage, data boundaries, and compute workgroup size.
It excludes control metadata that the translator does not read.
The runtime can use an exact matching translation when NVN changes that metadata.
Silksong enables this option in its profile.
Rhythm Heaven keeps its existing control-and-code identity.
The upstream backend change is recorded in the pinned project patch.

Silksong builds also run a bounded Mac startup with isolated saves.
New captures are translated before the iPhone build.
A second startup must report no missing shader mappings or libraries.
The launcher stops if the check fails.
Repeated shader builds retain libraries for the same tool chain.

A separate check found a host Makefile failure in paths with spaces.
The host generator now writes relative source paths and unique object rules.
The fixed host built and booted in the clean path with spaces.

A missing `c1_full.tsv` still selects the existing conservative 64 KiB constant-buffer path.
This diagnostic does not indicate a missing shader or a failed build.
Full-game coverage remains limited to the earlier game records.
