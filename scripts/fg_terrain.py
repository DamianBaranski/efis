#!/usr/bin/env python3
"""Download or link FlightGear WS2 terrain tiles for EFIS."""

from __future__ import annotations

import argparse
import hashlib
import math
import os
import sys
import urllib.error
import urllib.request
from pathlib import Path
from typing import Iterable

USER_AGENT = "efis-terrain-script/1.0"


def log(message: str) -> None:
    """Print a progress line to stdout."""
    print(message, flush=True)


def log_err(message: str) -> None:
    """Print a progress line to stderr."""
    print(message, file=sys.stderr, flush=True)


DEFAULT_MIRRORS = [
    "https://de3mirror.flightgear.org/ws2/Terrain",
    "https://us1mirror.flightgear.org/terrasync/ws2/Terrain",
    "https://download.flightgear.org/ws2/Terrain",
]

DEFAULT_SOURCES = [
    Path.home() / ".fgfs/TerraSync/Terrain",
    Path.home() / ".fgfs/Scenery/Terrain",
    Path("/usr/share/games/flightgear/Scenery/Terrain"),
    Path("/usr/share/flightgear/data/Scenery/Terrain"),
]


def toward_zero_int(value: float) -> int:
    """Truncate toward zero. Tile indexes use this, not floor."""
    return int(value)


def origin_10deg(value: float) -> int:
    """Ten-degree directory origin that contains value, in degrees."""
    origin = toward_zero_int(value / 10)
    if value < 0 and origin * 10 != value:
        origin -= 1
    return origin * 10


def tile_names(lat: float, lon: float) -> tuple[str, str]:
    """Return (10-degree folder, 1-degree folder) matching Bucket::generateTilePath()."""
    top_lon = origin_10deg(lon)
    top_lat = origin_10deg(lat)
    main_lon = toward_zero_int(lon)
    main_lat = toward_zero_int(lat)

    hem = "e" if top_lon >= 0 else "w"
    pole = "n" if top_lat >= 0 else "s"
    block = f"{hem}{abs(top_lon):03d}{pole}{abs(top_lat):02d}"
    cell = f"{hem}{abs(main_lon):03d}{pole}{abs(main_lat):02d}"
    return block, cell


def cells_in_radius(lat: float, lon: float, radius_deg: float) -> list[tuple[str, str]]:
    """Tile names whose centers fall inside radius_deg of the point."""
    lat_min = lat - radius_deg
    lat_max = lat + radius_deg
    lon_min = lon - radius_deg
    lon_max = lon + radius_deg
    cells: dict[tuple[str, str], None] = {}
    lat_start = math.floor(lat_min)
    lat_end = math.floor(lat_max)
    lon_start = math.floor(lon_min)
    lon_end = math.floor(lon_max)
    lat_i = lat_start
    while lat_i <= lat_end:
        lon_i = lon_start
        while lon_i <= lon_end:
            cells[tile_names(float(lat_i) + 0.5, float(lon_i) + 0.5)] = None
            lon_i += 1
        lat_i += 1
    return list(cells)


def parse_dirindex(text: str) -> tuple[list[str], list[tuple[str, str, int]]]:
    """File names and sizes listed in a FlightGear dirindex."""
    directories: list[str] = []
    files: list[tuple[str, str, int]] = []
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith("version:") or line.startswith("path:"):
            continue
        if line.startswith("d:"):
            parts = line.split(":")
            if len(parts) >= 2 and parts[1]:
                directories.append(parts[1])
            continue
        if line.startswith("f:"):
            parts = line.split(":")
            if len(parts) < 4:
                continue
            name, digest, size_s = parts[1], parts[2], parts[3]
            try:
                size = int(size_s)
            except ValueError:
                size = 0
            files.append((name, digest, size))
    return directories, files


def sha1_file(path: Path) -> str:
    """SHA-1 hex digest of path."""
    digest = hashlib.sha1()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def fetch_bytes(url: str, timeout: int = 60) -> bytes:
    """Response body, or an exception on HTTP failure."""
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(request, timeout=timeout) as response:
        return response.read()


def download_to_file(url: str, dest: Path, timeout: int = 180) -> None:
    """Write url to dest. Replaces a partial file."""
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(request, timeout=timeout) as response, dest.open("wb") as handle:
        while True:
            chunk = response.read(1024 * 1024)
            if not chunk:
                break
            handle.write(chunk)


def fetch_dirindex(mirrors: Iterable[str], rel_path: str) -> tuple[str, str]:
    """dirindex.xml body from the first mirror that answers."""
    last_error: Exception | None = None
    suffix = "" if not rel_path else rel_path.strip("/") + "/"
    for base in mirrors:
        url = f"{base.rstrip('/')}/{suffix}.dirindex"
        log(f"fetching {url}")
        try:
            return fetch_bytes(url, timeout=60).decode("utf-8", "replace"), base.rstrip("/")
        except (urllib.error.URLError, urllib.error.HTTPError, TimeoutError, OSError) as exc:
            last_error = exc
            log_err(f"mirror miss {url}: {exc}")
    raise RuntimeError(f"could not fetch dirindex for {rel_path or '/'}: {last_error}")


def is_scenery_btg(name: str) -> bool:
    """True for a terrain .btg.gz name, not an airport ICAO mesh."""
    return name.endswith(".btg.gz")


def download_cell(
    dest_root: Path,
    block: str,
    cell: str,
    mirrors: list[str],
    dry_run: bool,
) -> tuple[int, int]:
    """Download one tile into resources/terrain. Skips a file that is already present."""
    rel = f"{block}/{cell}"
    text, mirror = fetch_dirindex(mirrors, rel)
    _, files = parse_dirindex(text)
    wanted = [item for item in files if is_scenery_btg(item[0])]
    if not wanted:
        log(f"no .btg.gz files in {rel} on {mirror}")
        return 0, 0

    out_dir = dest_root / block / cell
    downloaded = 0
    skipped = 0
    total = len(wanted)
    for index, (name, digest, size) in enumerate(wanted, start=1):
        target = out_dir / name
        if target.exists() and target.stat().st_size == size and sha1_file(target) == digest:
            skipped += 1
            log(f"[{index}/{total}] skip {name}")
            continue
        url = f"{mirror}/{rel}/{name}"
        log(f"[{index}/{total}] {'would download' if dry_run else 'download'} {name} ({size} bytes)")
        if dry_run:
            downloaded += 1
            continue
        out_dir.mkdir(parents=True, exist_ok=True)
        tmp = target.with_name(target.name + ".part")
        try:
            download_to_file(url, tmp)
            actual = sha1_file(tmp)
            if digest and actual != digest:
                tmp.unlink(missing_ok=True)
                log_err(f"sha1 mismatch for {name}: {actual} != {digest}")
                continue
            tmp.replace(target)
            downloaded += 1
        except (urllib.error.URLError, urllib.error.HTTPError, TimeoutError, OSError) as exc:
            tmp.unlink(missing_ok=True)
            log_err(f"failed {name}: {exc}")
    return downloaded, skipped


def cmd_download(args: argparse.Namespace) -> int:
    """Download every tile named by the command line. Returns 0 when all are present."""
    dest = Path(args.dest).resolve()
    mirrors = [args.base_url] if args.base_url else list(DEFAULT_MIRRORS)
    cells = cells_in_radius(args.lat, args.lon, args.radius_deg)
    if not cells:
        log_err("no terrain cells in range")
        return 1
    log(f"destination: {dest}")
    log("cells: " + ", ".join(f"{block}/{cell}" for block, cell in cells))
    total_dl = 0
    total_skip = 0
    failed_cells = 0
    for block, cell in cells:
        log(f"tile {block}/{cell}")
        try:
            downloaded, skipped = download_cell(dest, block, cell, mirrors, args.dry_run)
        except Exception as exc:
            log_err(f"cell {block}/{cell} failed: {exc}")
            failed_cells += 1
            continue
        total_dl += downloaded
        total_skip += skipped
    log(f"done: downloaded={total_dl} skipped={total_skip} failed_cells={failed_cells}")
    log(f"EFIS path: {dest} (run the binary from build/ so ../resources/terrain resolves)")
    return 1 if failed_cells else 0


def find_existing_terrain(explicit: str | None) -> Path | None:
    """First FlightGear Terrain directory that exists, or None."""
    if explicit:
        path = Path(explicit).expanduser().resolve()
        return path if path.is_dir() else None
    for candidate in DEFAULT_SOURCES:
        if candidate.is_dir():
            return candidate.resolve()
    return None


def cmd_link(args: argparse.Namespace) -> int:
    """Symlink resources/terrain at an existing Terrain tree. Returns 0 on success."""
    dest = Path(args.dest).resolve()
    source = find_existing_terrain(args.source)
    if source is None:
        print("no FlightGear Terrain folder found. Pass --source /path/to/Terrain", file=sys.stderr)
        print("looked in:", file=sys.stderr)
        for candidate in DEFAULT_SOURCES:
            print(f"  {candidate}", file=sys.stderr)
        return 1
    dest.parent.mkdir(parents=True, exist_ok=True)
    if dest.exists() or dest.is_symlink():
        if dest.is_symlink() or dest.is_dir():
            if dest.resolve() == source:
                print(f"already linked: {dest} -> {source}")
                return 0
            print(f"{dest} already exists; remove it first or choose another --dest", file=sys.stderr)
            return 1
    print(f"link {dest} -> {source}")
    if not args.dry_run:
        dest.symlink_to(source, target_is_directory=True)
    return 0


def build_parser() -> argparse.ArgumentParser:
    """Command line for download and link."""
    repo_root = Path(__file__).resolve().parent.parent
    default_dest = repo_root / "resources" / "terrain"

    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)

    download = sub.add_parser("download", help="download WS2 .btg.gz tiles from TerraSync mirrors")
    download.add_argument("--lat", type=float, required=True, help="center latitude")
    download.add_argument("--lon", type=float, required=True, help="center longitude")
    download.add_argument("--radius-deg", type=float, default=0.0, help="half-size of the box in degrees (0 = the 1x1 cell containing lat/lon)")
    download.add_argument("--dest", default=str(default_dest), help="EFIS terrain directory")
    download.add_argument("--base-url", help="override TerraSync Terrain base URL")
    download.add_argument("--dry-run", action="store_true")
    download.set_defaults(func=cmd_download)

    link = sub.add_parser("link", help="symlink an existing FlightGear Terrain tree into EFIS")
    link.add_argument("--source", help="existing Terrain directory (auto-detected if omitted)")
    link.add_argument("--dest", default=str(default_dest), help="EFIS terrain directory")
    link.add_argument("--dry-run", action="store_true")
    link.set_defaults(func=cmd_link)
    return parser


def main(argv: list[str] | None = None) -> int:
    """Run download or link. Returns the command status."""
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(line_buffering=True)
        sys.stderr.reconfigure(line_buffering=True)
    parser = build_parser()
    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
