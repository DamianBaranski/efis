#!/usr/bin/env bash
# Point resources/terrain at an existing FlightGear/TerraSync Terrain folder.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
exec python3 "$ROOT/scripts/fg_terrain.py" link "$@"
