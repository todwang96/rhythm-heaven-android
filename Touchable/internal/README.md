# Touchable internal source

Use this directory for development.
Use the top-level `Touchable.command` for normal builds.

| Directory | Purpose |
| --- | --- |
| `games/` | Game manifests, profiles, adapters, assets, and game tests |
| `runtime/common/` | Shared Switch services and host runtime |
| `platform/ios/` | Shared iPhone app layer and CPU probe |
| `tools/` | Extraction, rewriting, shaders, build, signing, and installation |
| `tests/` | Python tool tests and C runtime checks |
| `patches/` | Changes to pinned upstream revisions |

Read [the project summary](../docs/developer/project-summary.md).
Use [the agent workflow](../docs/developer/agent-workflow.md).
Run commands from the repository root:

```sh
.nitenphone/venv/bin/python internal/tools/games.py check
.nitenphone/venv/bin/python -m unittest discover -s internal/tests -p 'test_*.py' -v
```

`tools/workspace.py` defines source paths and local data paths.
`ROOT` is this source directory.
`PROJECT_ROOT` is the repository root.
`state_root()` selects local work and build paths.
`DEPENDENCIES` selects the pinned local tool sources.
