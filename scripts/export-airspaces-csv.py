#!/usr/bin/env python3
"""Export OpenAIP airspace GeoJSON (polygons + limits) to a lightweight CSV.

Reads existing country GeoJSON files (e.g. resources/airspaces/pl_asp.geojson,
cz_asp.geojson) and generates resources/airspaces/airspaces.csv for fast parsing
in both web applications (EFIS flight planner) and native C++ overlays.

Usage:
  python3 scripts/export-airspaces-csv.py
  python3 scripts/export-airspaces-csv.py --country PL,CZ --dest resources/airspaces/airspaces.csv
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import sys
from pathlib import Path

# OpenAIP airspace type mapping
TYPE_NAMES = {
    0: "OTHER",
    1: "RESTRICTED",
    2: "DANGER",
    3: "PROHIBITED",
    4: "CTR",
    5: "TMZ",
    6: "RMZ",
    7: "TMA",
    8: "TRA",
    9: "TSA",
    10: "FIR",
    11: "UIR",
    12: "ADIZ",
    13: "ATZ",
    14: "MATZ",
    15: "AIRWAY",
    16: "MTR",
    17: "ALERT_AREA",
    18: "WARNING_AREA",
    19: "PROTECTED_AREA",
    20: "HTZ",
    21: "GLIDER",
    22: "TRP",
    23: "TIZ",
    24: "TIA",
    25: "MTA",
    26: "CONTROL_AREA",
    27: "AERODROME_TRAFFIC_ZONE",
    28: "DROP_ZONE",
    29: "SPECIAL_RULES_AREA",
    30: "AEROBATIC",
    31: "LOW_ALTITUDE",
    32: "MIL_RESTRICTED",
    33: "MIL_EXERCISE",
}

# OpenAIP ICAO class mapping
CLASS_NAMES = {
    0: "A",
    1: "B",
    2: "C",
    3: "D",
    4: "E",
    5: "F",
    6: "G",
    7: "UNCLASSIFIED",
    8: "OTHER",
}

CSV_FIELDS = [
    "id",
    "country",
    "name",
    "type",
    "type_id",
    "icao_class",
    "floor",
    "ceiling",
    "lower_m",
    "upper_m",
    "min_lat",
    "max_lat",
    "min_lon",
    "max_lon",
    "polygon",
]

DEFAULT_GROUND_M = 151.0


def format_limit(limit: dict | None) -> str:
    """Format an OpenAIP altitude limit into aviation text (e.g. GND, 2500ft, FL95)."""
    if not isinstance(limit, dict):
        return "?"
    val = limit.get("value", 0)
    if val is None:
        return "?"
    unit = limit.get("unit", 1)  # 0=m, 1=ft, 6=FL
    datum = limit.get("referenceDatum", 1)  # 0=GND/AGL, 1=MSL/AMSL, 2=STD
    rounded = int(round(val))

    if unit == 6 or datum == 2:
        return f"FL{rounded:02d}" if rounded < 100 else f"FL{rounded}"
    if datum == 0 and val <= 0.5:
        return "GND"
    if datum == 0:
        return f"{rounded}m AGL" if unit == 0 else f"{rounded}ft AGL"
    return f"{rounded}m" if unit == 0 else f"{rounded}ft"


def limit_to_amsl_m(limit: dict | None, ground_m: float) -> float:
    """Calculate altitude above mean sea level in meters."""
    if not isinstance(limit, dict):
        return ground_m
    val = limit.get("value")
    if val is None:
        return ground_m
    val = float(val)
    unit = limit.get("unit", 1)
    datum = limit.get("referenceDatum", 1)

    if unit == 6 or datum == 2:
        return round(val * 100.0 * 0.3048, 1)

    meters = val if unit == 0 else val * 0.3048
    if datum == 0:
        return round(ground_m + meters, 1)
    return round(meters, 1)


def load_airports(airport_csv_path: Path) -> list[tuple[float, float, float]]:
    """Load airports for ground elevation lookup."""
    airports = []
    if not airport_csv_path.exists():
        return airports
    try:
        with airport_csv_path.open("r", encoding="utf-8") as f:
            reader = csv.DictReader(f)
            for row in reader:
                try:
                    lat = float(row["lat"])
                    lon = float(row["lon"])
                    elev = float(row["elev_m"]) if row.get("elev_m") else DEFAULT_GROUND_M
                    airports.append((lat, lon, elev))
                except (ValueError, KeyError):
                    continue
    except Exception as exc:
        print(f"Warning: could not read {airport_csv_path}: {exc}", file=sys.stderr)
    return airports


def nearest_ground(lat: float, lon: float, airports: list[tuple[float, float, float]]) -> float:
    """Find nearest airport elevation to estimate ground height."""
    if not airports:
        return DEFAULT_GROUND_M
    best_d = 1e18
    best_elev = DEFAULT_GROUND_M
    for alat, alon, elev in airports:
        d = (lat - alat) ** 2 + (lon - alon) ** 2
        if d < best_d:
            best_d = d
            best_elev = elev
    return best_elev


def open_ring(coords: list[list[float]]) -> list[tuple[float, float]]:
    """Convert [lon, lat] pairs to (lat, lon) removing duplicated closing point."""
    pts = []
    for pt in coords:
        if isinstance(pt, list) and len(pt) >= 2 and pt[0] is not None and pt[1] is not None:
            pts.append((float(pt[1]), float(pt[0])))
    while len(pts) >= 2:
        if abs(pts[0][0] - pts[-1][0]) < 1e-7 and abs(pts[0][1] - pts[-1][1]) < 1e-7:
            pts.pop()
        else:
            break
    return pts


def parse_polygons(geometry: dict) -> list[list[tuple[float, float]]]:
    """Extract rings from Polygon or MultiPolygon geometry."""
    rings = []
    if not isinstance(geometry, dict):
        return rings
    g_type = geometry.get("type", "")
    coords = geometry.get("coordinates")
    if not isinstance(coords, list) or not coords:
        return rings

    if g_type == "Polygon":
        if coords and isinstance(coords[0], list):
            ring = open_ring(coords[0])
            if len(ring) >= 3:
                rings.append(ring)
    elif g_type == "MultiPolygon":
        for poly in coords:
            if isinstance(poly, list) and poly and isinstance(poly[0], list):
                ring = open_ring(poly[0])
                if len(ring) >= 3:
                    rings.append(ring)
    return rings


def format_polygon_str(ring: list[tuple[float, float]]) -> str:
    """Format coordinates as semicolon-separated lat,lon pairs."""
    return ";".join(f"{lat:.5f},{lon:.5f}" for lat, lon in ring)


def export_airspaces(
    source_dir: Path,
    dest_csv: Path,
    countries: list[str],
    airports: list[tuple[float, float, float]],
    allowed_types: set[int] | None = None,
) -> int:
    """Convert country GeoJSON files into CSV."""
    rows: list[dict] = []
    seen_ids = set()

    for cc in countries:
        geojson_path = source_dir / f"{cc.lower()}_asp.geojson"
        if not geojson_path.exists():
            print(f"File not found: {geojson_path}", file=sys.stderr)
            continue

        print(f"Reading {geojson_path.name}...")
        try:
            with geojson_path.open("r", encoding="utf-8") as f:
                data = json.load(f)
        except Exception as exc:
            print(f"Error parsing {geojson_path}: {exc}", file=sys.stderr)
            continue

        features = data.get("features", [])
        for feat in features:
            props = feat.get("properties") or {}
            geom = feat.get("geometry") or {}
            type_id = props.get("type")

            if allowed_types is not None and type_id not in allowed_types:
                continue

            rings = parse_polygons(geom)
            if not rings:
                continue

            base_id = str(props.get("_id") or props.get("id") or "")
            name = str(props.get("name") or "AIRSPACE").strip()
            type_name = TYPE_NAMES.get(type_id, str(type_id))
            icao_class = CLASS_NAMES.get(props.get("icaoClass"), "")

            lower_obj = props.get("lowerLimit")
            upper_obj = props.get("upperLimit")
            floor_str = format_limit(lower_obj)
            ceil_str = format_limit(upper_obj)

            for ring_idx, ring in enumerate(rings):
                lats = [pt[0] for pt in ring]
                lons = [pt[1] for pt in ring]
                cent_lat = sum(lats) / len(lats)
                cent_lon = sum(lons) / len(lons)
                ground_m = nearest_ground(cent_lat, cent_lon, airports)

                lower_m = limit_to_amsl_m(lower_obj, ground_m)
                upper_m = limit_to_amsl_m(upper_obj, ground_m)

                row_id = f"{base_id}_{ring_idx}" if len(rings) > 1 else base_id
                if not row_id:
                    row_id = f"{cc}_{len(rows)}"

                rows.append(
                    {
                        "id": row_id,
                        "country": cc.upper(),
                        "name": name,
                        "type": type_name,
                        "type_id": type_id if type_id is not None else "",
                        "icao_class": icao_class,
                        "floor": floor_str,
                        "ceiling": ceil_str,
                        "lower_m": lower_m,
                        "upper_m": upper_m,
                        "min_lat": f"{min(lats):.5f}",
                        "max_lat": f"{max(lats):.5f}",
                        "min_lon": f"{min(lons):.5f}",
                        "max_lon": f"{max(lons):.5f}",
                        "polygon": format_polygon_str(ring),
                    }
                )

    dest_csv.parent.mkdir(parents=True, exist_ok=True)
    with dest_csv.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=CSV_FIELDS)
        writer.writeheader()
        writer.writerows(rows)

    file_size_kb = dest_csv.stat().st_size / 1024.0
    print(f"Wrote {len(rows)} airspace boundary records to {dest_csv} ({file_size_kb:.1f} KB)")
    return len(rows)


def main() -> int:
    repo_root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--country",
        default="PL,CZ",
        help="ISO alpha-2 codes (default: PL,CZ)",
    )
    parser.add_argument(
        "--src",
        default=str(repo_root / "resources" / "airspaces"),
        help="source directory containing GeoJSON files",
    )
    parser.add_argument(
        "--dest",
        default=str(repo_root / "resources" / "airspaces" / "airspaces.csv"),
        help="destination CSV file path",
    )
    parser.add_argument(
        "--airports",
        default=str(repo_root / "resources" / "airports" / "airports.csv"),
        help="airports CSV for ground elevation lookup",
    )
    parser.add_argument(
        "--all-types",
        action="store_true",
        help="export all types including FIR and special areas",
    )
    args = parser.parse_args()

    countries = [c.strip().lower() for c in args.country.split(",") if c.strip()]
    airports = load_airports(Path(args.airports))
    allowed_types = None if args.all_types else None  # export all relevant features

    count = export_airspaces(
        source_dir=Path(args.src),
        dest_csv=Path(args.dest),
        countries=countries,
        airports=airports,
        allowed_types=allowed_types,
    )
    return 0 if count > 0 else 1


if __name__ == "__main__":
    sys.exit(main())
