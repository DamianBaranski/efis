#!/usr/bin/env python3
"""Download OSM aeroway=runway centerlines and write resources/airports/osm_runways.csv.

OpenAIP and OurAirports usually only have the airport reference point. OSM ways
are traced on imagery, so L/R parallels have distinct endpoints.

Example:
  python3 scripts/download-osm-runways.py
"""

from __future__ import annotations

import argparse
import csv
import math
import sys
import time
import urllib.error
import urllib.request
import xml.etree.ElementTree as ET
from pathlib import Path

USER_AGENT = "efis-osm-runways/1.0"
OSM_MAP = "https://api.openstreetmap.org/api/0.6/map?bbox={minlon},{minlat},{maxlon},{maxlat}"
CSV_FIELDS = ["icao", "ref", "lat", "lon", "heading_deg", "length_m", "width_m", "lat1", "lon1", "lat2", "lon2"]


def log(message: str) -> None:
    print(message, flush=True)


def haversine_m(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
    r = 6371000.0
    p1, p2 = math.radians(lat1), math.radians(lat2)
    dlat = p2 - p1
    dlon = math.radians(lon2 - lon1)
    a = math.sin(dlat / 2) ** 2 + math.cos(p1) * math.cos(p2) * math.sin(dlon / 2) ** 2
    return 2 * r * math.asin(min(1.0, math.sqrt(a)))


def heading_deg(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
    p1, p2 = math.radians(lat1), math.radians(lat2)
    dlon = math.radians(lon2 - lon1)
    x = math.sin(dlon) * math.cos(p2)
    y = math.cos(p1) * math.sin(p2) - math.sin(p1) * math.cos(p2) * math.cos(dlon)
    return (math.degrees(math.atan2(x, y)) + 360.0) % 360.0


def unique_airports(airports_csv: Path) -> list[tuple[str, float, float]]:
    seen: dict[str, tuple[float, float]] = {}
    with airports_csv.open(encoding="utf-8") as handle:
        for row in csv.DictReader(handle):
            icao = (row.get("icao") or "").strip().upper()
            if len(icao) != 4 or icao in seen:
                continue
            try:
                seen[icao] = (float(row["lat"]), float(row["lon"]))
            except (KeyError, ValueError):
                continue
    return sorted(seen.items(), key=lambda item: item[0])


def fetch_bbox(lat: float, lon: float, pad_deg: float) -> str:
    url = OSM_MAP.format(
        minlon=f"{lon - pad_deg:.5f}",
        minlat=f"{lat - pad_deg:.5f}",
        maxlon=f"{lon + pad_deg:.5f}",
        maxlat=f"{lat + pad_deg:.5f}",
    )
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(request, timeout=40) as response:
        return response.read().decode("utf-8")


def parse_runways(xml_text: str, icao: str) -> list[dict[str, str]]:
    root = ET.fromstring(xml_text)
    nodes = {
        node.get("id"): (float(node.get("lat")), float(node.get("lon")))
        for node in root.findall("node")
        if node.get("lat") and node.get("lon")
    }
    rows = []
    for way in root.findall("way"):
        tags = {tag.get("k"): tag.get("v") for tag in way.findall("tag")}
        if tags.get("aeroway") != "runway":
            continue
        coords = []
        for nd in way.findall("nd"):
            ref = nd.get("ref")
            if ref in nodes:
                coords.append(nodes[ref])
        if len(coords) < 2:
            continue
        lat1, lon1 = coords[0]
        lat2, lon2 = coords[-1]
        length = haversine_m(lat1, lon1, lat2, lon2)
        if length < 50.0:
            continue
        width = tags.get("width") or ""
        try:
            width_m = float(width)
        except ValueError:
            width_m = 50.0
        if width_m < 5.0:
            width_m = 50.0
        heading = heading_deg(lat1, lon1, lat2, lon2)
        if heading >= 180.0:
            heading -= 180.0
            lat1, lon1, lat2, lon2 = lat2, lon2, lat1, lon1
        rows.append(
            {
                "icao": icao,
                "ref": tags.get("ref") or "",
                "lat": f"{(lat1 + lat2) * 0.5:.8f}",
                "lon": f"{(lon1 + lon2) * 0.5:.8f}",
                "heading_deg": f"{heading:.2f}",
                "length_m": f"{length:.1f}",
                "width_m": f"{width_m:.1f}",
                "lat1": f"{lat1:.8f}",
                "lon1": f"{lon1:.8f}",
                "lat2": f"{lat2:.8f}",
                "lon2": f"{lon2:.8f}",
            }
        )
    return rows


def main() -> int:
    repo_root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--airports",
        default=str(repo_root / "resources" / "airports" / "airports.csv"),
    )
    parser.add_argument(
        "--dest",
        default=str(repo_root / "resources" / "airports" / "osm_runways.csv"),
    )
    parser.add_argument("--pad-deg", type=float, default=0.012, help="bbox half-size around ARP")
    parser.add_argument("--icao", default="", help="optional comma-separated ICAO filter")
    parser.add_argument("--sleep", type=float, default=0.4)
    args = parser.parse_args()

    airports = unique_airports(Path(args.airports))
    if args.icao:
        wanted = {code.strip().upper() for code in args.icao.split(",") if code.strip()}
        airports = [item for item in airports if item[0] in wanted]
    if not airports:
        print("no ICAO airports to query", file=sys.stderr)
        return 1

    rows: list[dict[str, str]] = []
    fail = 0
    for icao, (lat, lon) in airports:
        log(f"OSM {icao} {lat:.5f} {lon:.5f}")
        try:
            xml_text = fetch_bbox(lat, lon, args.pad_deg)
            found = parse_runways(xml_text, icao)
            log(f"  runways {len(found)}")
            rows.extend(found)
        except (urllib.error.URLError, TimeoutError, OSError, ET.ParseError) as exc:
            log(f"  failed {exc}")
            fail += 1
        time.sleep(args.sleep)

    dest = Path(args.dest)
    dest.parent.mkdir(parents=True, exist_ok=True)
    with dest.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=CSV_FIELDS)
        writer.writeheader()
        writer.writerows(rows)
    log(f"wrote {dest} runways={len(rows)} fail={fail}")
    return 0 if rows else 1


if __name__ == "__main__":
    sys.exit(main())
