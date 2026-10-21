#!/bin/bash
set -euo pipefail
PROJECT_ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$PROJECT_ROOT"
if [[ -x ../.nitenphone/venv/bin/python ]]; then
    exec ../.nitenphone/venv/bin/python tools/build_host.py "$@"
fi
exec python3 tools/build_host.py "$@"
