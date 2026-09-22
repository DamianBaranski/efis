#!/usr/bin/env bash
## Point resources/terrain at an existing FlightGear Terrain folder.
## Delegates to scripts/fg_terrain.py.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
exec python3 "$ROOT/scripts/fg_terrain.py" link "$@"
