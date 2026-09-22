#!/usr/bin/env bash
## Download FlightGear WS2 terrain into resources/terrain.
## Delegates to scripts/fg_terrain.py.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
exec python3 -u "$ROOT/scripts/fg_terrain.py" download "$@"
