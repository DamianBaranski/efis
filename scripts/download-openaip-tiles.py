#!/usr/bin/env python3
"""Prefetch OpenAIP overlay plus a street/dark/satellite basemap into resources/openaip/cache."""

from __future__ import annotations

import argparse
import math
import os
import sys
import urllib.error
import urllib.request
from pathlib import Path

OPENAIP_BASE_URL = "https://api.tiles.openaip.net/api/data"
USER_AGENT = "efis-openaip/1.0"
STYLES = {
    "voyager": "osm",
    "dark": "dark",
    "satellite": "satellite",
}


def log(message: str) -> None:
    print(message, flush=True)


def load_key(repo_root: Path, env_name: str, filename: str) -> str:
    env = os.environ.get(env_name, "").strip()
    if env:
        return env
    key_path = repo_root / "resources" / "openaip" / filename
    if key_path.is_file():
        return key_path.read_text(encoding="utf-8").strip().splitlines()[0].strip()
    return ""


def save_style(repo_root: Path, style: str) -> None:
    path = repo_root / "resources" / "openaip" / "basemap.style"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(style + "\n", encoding="utf-8")


def lat_lon_to_tile(lat: float, lon: float, zoom: int) -> tuple[int, int]:
    n = 2 ** zoom
    x = int((lon + 180.0) / 360.0 * n)
    lat_rad = math.radians(lat)
    y = int((1.0 - math.log(math.tan(lat_rad) + 1.0 / math.cos(lat_rad)) / math.pi) / 2.0 * n)
    max_index = n - 1
    return max(0, min(x, max_index)), max(0, min(y, max_index))


def fetch_url(url: str, headers: dict[str, str]) -> bytes | None:
    request = urllib.request.Request(url, headers=headers)
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            if response.status == 204:
                log(f"empty {url}")
                return None
            return response.read()
    except urllib.error.HTTPError as exc:
        log(f"HTTP {exc.code} {url}")
    except (urllib.error.URLError, TimeoutError, OSError) as exc:
        log(f"failed {url}: {exc}")
    return None


def save_tile(dest: Path, layer: str, z: int, x: int, y: int, data: bytes) -> None:
    target = dest / layer / str(z) / str(x) / f"{y}.png"
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(data)


def tile_exists(dest: Path, layer: str, z: int, x: int, y: int) -> bool:
    target = dest / layer / str(z) / str(x) / f"{y}.png"
    return target.is_file() and target.stat().st_size > 0


def download_openaip(key: str, dest: Path, layer: str, z: int, x: int, y: int) -> bool:
    if tile_exists(dest, layer, z, x, y):
        log(f"skip {layer}/{z}/{x}/{y}.png")
        return True
    url = f"{OPENAIP_BASE_URL}/{layer}/{z}/{x}/{y}.png"
    log(f"fetch {url}")
    data = fetch_url(
        url,
        {
            "User-Agent": USER_AGENT,
            "x-openaip-api-key": key,
        },
    )
    if not data:
        return False
    save_tile(dest, layer, z, x, y, data)
    return True


def download_basemap(dest: Path, style: str, carto_key: str, z: int, x: int, y: int) -> bool:
    layer = STYLES[style]
    if tile_exists(dest, layer, z, x, y):
        log(f"skip {layer}/{z}/{x}/{y}.png")
        return True
    if style == "satellite":
        url = f"https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}"
        log(f"fetch satellite/{z}/{x}/{y}.png")
        data = fetch_url(url, {"User-Agent": USER_AGENT})
    else:
        sub = chr(ord("a") + ((x + y) & 3))
        carto_style = "dark_all" if style == "dark" else "rastertiles/voyager"
        url = f"https://{sub}.basemaps.cartocdn.com/{carto_style}/{z}/{x}/{y}.png?key={carto_key}"
        log(f"fetch {style}/{z}/{x}/{y}.png")
        data = fetch_url(url, {"User-Agent": USER_AGENT})
    if not data:
        return False
    save_tile(dest, layer, z, x, y, data)
    return True


def main() -> int:
    repo_root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lat", type=float, required=True)
    parser.add_argument("--lon", type=float, required=True)
    parser.add_argument("--zoom", type=int, default=None, help="tile zoom (default 13, or 16 for satellite)")
    parser.add_argument("--radius", type=int, default=7, help="tiles around the center tile")
    parser.add_argument("--layer", default="openaip", help="OpenAIP overlay layer name")
    parser.add_argument(
        "--style",
        choices=sorted(STYLES),
        default="voyager",
        help="basemap: voyager (Carto streets), dark (Carto Dark Matter), satellite (Esri imagery)",
    )
    parser.add_argument("--dest", default=str(repo_root / "resources" / "openaip" / "cache"))
    parser.add_argument("--no-basemap", action="store_true", help="skip street/dark/satellite tiles")
    parser.add_argument("--no-openaip", action="store_true", help="skip OpenAIP overlay tiles")
    args = parser.parse_args()
    zoom = args.zoom if args.zoom is not None else (16 if args.style == "satellite" else 13)

    dest = Path(args.dest)
    cx, cy = lat_lon_to_tile(args.lat, args.lon, zoom)
    log(f"center tile {zoom}/{cx}/{cy} style={args.style}")

    key = ""
    carto_key = ""
    if not args.no_openaip:
        key = load_key(repo_root, "OPENAIP_API_KEY", "api.key")
        if not key:
            print("missing OpenAIP API key: set OPENAIP_API_KEY or resources/openaip/api.key", file=sys.stderr)
            return 1
    if not args.no_basemap and args.style != "satellite":
        carto_key = load_key(repo_root, "CARTO_API_KEY", "carto.key")
        if not carto_key:
            print("missing Carto API key: set CARTO_API_KEY or resources/openaip/carto.key", file=sys.stderr)
            return 1
    if not args.no_basemap:
        save_style(repo_root, args.style)

    ok = 0
    fail = 0
    for dy in range(-args.radius, args.radius + 1):
        for dx in range(-args.radius, args.radius + 1):
            tx = cx + dx
            ty = cy + dy
            if not args.no_basemap:
                if download_basemap(dest, args.style, carto_key, zoom, tx, ty):
                    ok += 1
                else:
                    fail += 1
            if not args.no_openaip:
                if download_openaip(key, dest, args.layer, zoom, tx, ty):
                    ok += 1
                else:
                    fail += 1
    log(f"done ok={ok} fail={fail}")
    return 0 if fail == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
