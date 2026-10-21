# Directory migration

The 2026-10-14 migration separated shared runtime code from game adapters.
Its original move list remains in [directory-migration.json](directory-migration.json).
That file is a historical record.

The 2026-10-17 reorganization moves source code into `internal/`.
It moves local work, dependencies, and build data into `.nitenphone/`.
It moves current IPAs into `IPA/<id>/`.
It keeps source packages and keys in `MyGameFiles/`.
Root aliases from the earlier layout are no longer user entry points.
Historical reports can still contain the earlier paths.

Use [the current reorganization record](reorganization.md) for the path map and checks.
Use [workspace maintenance](workspace-maintenance.md) for earlier cleanup records.
