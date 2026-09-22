#!/usr/bin/env python3
"""Copy the lightweight EFIS assets that fit in an APK.

Skips FlightGear tiles, BTG landclass textures, and the OpenAIP tile cache.
Push those onto the device afterwards:

  adb push resources/terrain /sdcard/Android/data/com.efis.app/files/resources/terrain
  adb push resources/textures/btg /sdcard/Android/data/com.efis.app/files/resources/textures/btg
"""

from __future__ import annotations

import shutil
import sys
from pathlib import Path

INCLUDE_DIRS = [
    "shader",
    "resources/fonts",
    "resources/textures/ui",
    "resources/textures/skybox",
    "resources/airports",
    "resources/airspaces",
    "resources/vrp",
    "resources/obstacles",
]
INCLUDE_FILES = [
    "resources/textures/unknown.png",
]


def copy_tree(src: Path, dest: Path, manifest: list[str], prefix: str) -> None:
    """Copy files under src into dest and record each relative path."""
    if not src.exists():
        return
    dest.mkdir(parents=True, exist_ok=True)
    for item in sorted(src.rglob("*")):
        if item.is_dir() or item.name.startswith("."):
            continue
        rel = item.relative_to(src)
        target = dest / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(item, target)
        manifest.append(f"{prefix}/{rel.as_posix()}")


def main() -> int:
    """Replace the APK asset tree with the lightweight resources. Returns 0."""
    repo = Path(__file__).resolve().parent.parent
    dest_root = repo / "android" / "app" / "src" / "main" / "assets"
    if dest_root.exists():
        shutil.rmtree(dest_root)
    dest_root.mkdir(parents=True)

    manifest: list[str] = []
    for rel in INCLUDE_DIRS:
        copy_tree(repo / rel, dest_root / rel, manifest, rel)
    for rel in INCLUDE_FILES:
        src = repo / rel
        if not src.exists():
            continue
        target = dest_root / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, target)
        manifest.append(rel)

    (dest_root / "asset-manifest.txt").write_text("\n".join(manifest) + "\n", encoding="utf-8")
    print(f"packed {len(manifest)} assets into {dest_root}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
