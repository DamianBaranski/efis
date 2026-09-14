#!/usr/bin/env bash
# Download FlightGear WS2 terrain (.btg.gz) into resources/terrain.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
exec python3 -u "$ROOT/scripts/fg_terrain.py" download "$@"
