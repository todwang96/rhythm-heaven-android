# Workspace maintenance

Use these rules for local maintenance.
Keep game behavior and application identity unchanged.

The current layout and new build checks are in [the reorganization record](reorganization.md).
The dated retention sections below describe earlier maintenance results.

## Source history

The root Git repository was created on 2026-10-16.
Commit `970c3c7` records the source before maintenance.
The commit uses the local automation identity `Codex <codex@local>`.
No remote was added.
The upstream repositories remain separate.

Track source, configuration, documentation, tests, artwork, and patches.
Keep local data and signing material outside Git.
Check the index before each commit:

```sh
git status --short
git diff --stat
git diff --check
git diff --cached --name-only
```

## Current records

Use `game.json` for application identity, build number, and active paths.
Use `profile.json` for module identity and runtime options.
Use each game's `README.md` for current behavior and limits.
Keep earlier build evidence in a history file or a dated report.
Retain the original evidence when a later result replaces a limit.

## Protected files

Preserve these files during cleanup by default.
An explicit user request can retire old build outputs.
Record that exception before deletion.
Original packages, keys, and saves remain protected.

Default protected files:

- Original game packages and keys in `MyGameFiles/`.
- All saves, including host fixtures and phone backups.
- All `.app` bundles, including unsigned test builds.
- Existing IPA files and their reports and checksums.
- Signing files and provisioning profiles.
- Active and prior content, rewritten modules, and shader chains.
- Test reports, logs, screenshots, and compatibility links.
- Upstream edits and pinned patches.

Do not remove a complete `.nitenphone/build/` or `.nitenphone/work/` directory.
Do not follow symbolic links during cleanup.
Stop if a build or game process is using a cleanup target.

## Rebuildable files

Inventory each target before deletion.
Check that it contains no protected file.
Record its restore command.
Use these sources for reconstruction:

| Target | Reconstruction source |
| --- | --- |
| Xcode `Intermediates.noindex`, module caches, SDK caches, and compilation caches | The generated project, source, active modules, shader libraries, and installed Xcode |
| `.nitenphone/build/games/<id>/host/obj/` | `bash internal/tools/build_host.sh --game <id>` |
| `.nitenphone/build/tooling/cache/dm/verify/*.usr.json` | `structck.py` regenerates disassembly caches from the input module and LLVM version |
| The pinned .NET SDK download archive | `internal/tools/bootstrap_shader.sh` records the download URL and SHA-512 |
| A transfer package's expanded staging directory | Its retained ZIP, after a full file-set and SHA-256 comparison |
| Source `__pycache__/` directories | The source files and the local Python interpreter |
| Source `.DS_Store` files | Finder recreates local view metadata |

Preserve transfer ZIP files, checksums, and package reports.
Remove a staging directory only when every regular file matches its ZIP entry.
Keep a staging directory if it contains a save, app, key, package, or unique file.

After deletion, compare protected paths with the pre-cleanup inventory.
Check file sizes, modification times, modes, and symbolic-link targets.
Use SHA-256 for original packages, keys, saves, module inputs, shader inputs, and signing records.
Keep every deletion target outside the protected paths.
Check that game manifests and upstream patches retain their expected values.
Run the configuration checks for both games.

## First maintenance: 2026-10-16–17

The [Silksong maintenance report](../../.nitenphone/work/silksong/reports/workspace-cleanup-20261016/README.md) contains the shared inventory and deletion record.
The [Rhythm Heaven maintenance report](../../.nitenphone/work/rhythm-heaven/reports/workspace-cleanup-20261016/README.md) records that game's scope.
The reports include the measured directory reduction and validation limits.
No Mac or iPhone gameplay test is part of this maintenance task.

Maintenance removed 105 reviewed targets.
Their directory usage totals 8.49 GiB.
Project directory usage fell from 179.03 GiB to 170.59 GiB after new audit records.
During that first task, all 19 app bundles and 22 IPA files were preserved.
The protected-file comparison has no errors.
Both game configuration checks and all 34 Python tool tests pass.

## Latest build retention: 2026-10-17

The user then requested removal of old builds and retention of only the latest versions.
This request permits removal of the old signed apps and IPA files that the earlier cleanup preserved.
This exception applies to the recorded build outputs.
It does not permit removal of original game files, keys, saves, active modules, or test evidence.

| Game | Latest local app | Latest IPA |
| --- | --- | --- |
| Silksong | Build 8, `.nitenphone/build/games/silksong/ios/Build/Products/Debug-iphoneos/Silksong.app` | `.nitenphone/build/games/silksong/ipa/Silksong-1.0.30000-build8.ipa` |
| Rhythm Heaven | Build 15, `.nitenphone/build/games/rhythm-heaven/ios/Build/Products/Debug-iphoneos/RhythmHeaven.app` | `.nitenphone/build/games/rhythm-heaven/ipa/RhythmHeaven-build15-extra-menu-touch.ipa` |

The Silksong app came from `ios-pool-tracking`.
It includes the later shared fixes and is unsigned.
The retained Silksong IPA is the signed phone build 8 package.
These two outputs have different source snapshots.
The Rhythm Heaven app came from `ios-build15` and retains its signature.
Moving these builds to `ios/` keeps the default generator and packaging paths usable.
No new app build or signing operation was run.

Old IPA reports and checksums are under each game's `logs/retired-builds-20261017/ipa/`.
Old Xcode logs and Derived Data metadata are under `logs/retired-builds-20261017/`.
Both old build 4 Mac transfer ZIP files were retired.
Their package reports and checksums remain under the Rhythm Heaven log archive.
Old artifact paths in history files describe the files used at that time.
They do not indicate that those artifacts are still present.

The current CPU probe remains in `.nitenphone/build/probes/ios/`.
Legacy app aliases now point to current outputs where an equivalent exists.
The obsolete icon-check alias was removed.

See [the shared retention report](../../.nitenphone/work/silksong/reports/build-retention-20261017/README.md).
See [the Rhythm Heaven retention record](../../.nitenphone/work/rhythm-heaven/reports/build-retention-20261017/README.md).

This later retention task retired 43 output targets, including 20 old IPAs.
Project directory usage fell from 170.62 GiB to 40.06 GiB.
The protected comparison passed for 95,854 records.
Both latest IPA checksums and both app executable hashes passed after cleanup.
Both game configuration checks passed.
No gameplay test or device installation was run.
