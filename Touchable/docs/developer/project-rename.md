# Project rename: 2026-10-17

The project name is Touchable.
The previous name was Nitenphone.
Use `Touchable.command` at the repository root.
The menu and current launcher instructions use the new name.
New private Mac transfer folders and ZIP names use the `Touchable-` prefix.
Existing transfer archives retain their original names.

The user's README introduction and game table remain unchanged.
Only the project heading and launcher filename change in that file.

## Compatibility

This rename does not require an application or save migration.

| Item | Retained value |
| --- | --- |
| Silksong Bundle ID | `com.mmmz.NitenphoneProbe` |
| Rhythm Heaven Bundle ID | `com.mmmz.NitenphoneSilksong` |
| Product names | `Silksong`, `RhythmHeaven` |
| App builds | Silksong 9, Rhythm Heaven 16 |
| Phone save paths | `Documents/Saves/Silksong`, `Documents/Saves/RhythmHeaven` |
| Local state | `.nitenphone/` |
| Local signing settings | `.nitenphone/config.json` |
| Local saves and phone backups | `.nitenphone/saves/<id>/` |
| Isolated state setting | `NITENPHONE_STATE_DIR` |

Keep the existing workspace directory in place.
Generated projects and local reports can contain absolute paths.
The Python environment also uses its current absolute path.
The workspace directory does not need to match the project name.

Internal code identifiers retain their existing names.
These include `niten_game_*`, `NITENPHONE_SWITCH_RUNTIME`, and generated configuration fields.
Existing Bundle ID prefixes, CPU probe names, fallback font names, and runtime diagnostic labels also remain.
They are compatibility identifiers.
Do not replace them as a project branding change.

Game manifests, profiles, application sources, signed apps, and IPA files are unchanged by this rename.
No device installation is needed.

## Validation

Run the renamed launcher through the game menu.
Run environment checks for both games.
Run the existing Python tests.
Check local document links and shell syntax.
Compare the README against its pre-rename copy.
Compare application identities, local signing settings, saved files, and output metadata.

Detailed evidence is local in `.nitenphone/work/silksong/reports/rename-touchable-20261017/`.
These checks cover the project entry points.
They do not add gameplay coverage.

The renamed launcher passed an interactive Terminal check.
It displayed the Touchable heading, both games, and the operation menu.
Cancellation and the final window prompt exited successfully.
Both games passed the launcher's environment check.
All 47 Python tests passed.
Document checks covered 28 Markdown files, 93 local links, and 36 shell blocks.
Both generated transfer READMEs used Touchable and retained the configured Bundle ID.

The README matched its pre-rename copy after only the heading and launcher replacements.
All 725 recorded SHA-256 comparisons passed.
All 725 protected file metadata comparisons passed.
The two input/output directory READMEs were excluded from the metadata comparison because their launcher instructions changed.
Application sources, manifests, profiles, and upstream patches were unchanged.
This task did not rebuild or install an app.
