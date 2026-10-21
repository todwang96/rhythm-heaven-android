# Supported games

This project adapts each game and version separately.
An NSP of a different game does not become compatible automatically.

| Game | Game ID | Requirements | Status |
| --- | --- | --- | --- |
| Hollow Knight: Silksong | `silksong` | v0 base NSP, plus the v655360 update NSP | `adapted` |
| Rhythm Heaven | `rhythm-heaven` | v0 base NSP | `in-progress` |

The Silksong update is version 1.0.30000.
It supports the menus, gameplay, save data, and touch controls.
It skips the cutscene videos. The full playthrough is not verified.

Rhythm Heaven has test records for many levels and menus.
The full playthrough and the multiplayer mode are not verified.
The software keyboard returns cancel. Audio and fonts still have recorded limits.

Before a build, the launcher checks the module SHA-256 values and Build IDs.
If they do not match, the build stops.
A new game cannot build an app while its status is `not-adapted`.
For the detailed test records, see [Silksong](../internal/games/silksong/README.md) and [Rhythm Heaven](../internal/games/rhythm-heaven/README.md).
