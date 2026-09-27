#!/usr/bin/env python3
"""Download and generate enroute IFR navigation points CSV for EFIS.

Aggregates:
  1. IFR Enroute Intersections / RNAV Fixes from FlightGear/X-Plane standard navdata (fix.dat).
  2. Radio Navigation Aids (VOR, VOR-DME, NDB, TACAN, DME) from OurAirports & OpenAIP exports.
  3. EFIS Flight Planning Presets (e.g. KUKUS, DODUS, VELIK, OKL, TRZ).

Assigns ISO country codes using Natural Earth boundaries.

Usage:
  python3 scripts/download-enroute-points.py
  python3 scripts/download-enroute-points.py --scope central-europe
  python3 scripts/download-enroute-points.py --country PL,CZ,SK
  python3 scripts/download-enroute-points.py --scope global
"""

from __future__ import annotations

import argparse
import csv
import gzip
import io
import json
import shutil
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

USER_AGENT = "efis-nav/1.0"
CACHE_DIR = Path("/tmp/efis_nav_cache")

URL_FIX_DAT = "https://gitlab.com/flightgear/fgdata/-/raw/next/Navaids/fix.dat.gz"
URL_OURAIRPORTS_NAVAIDS = "https://davidmegginson.github.io/ourairports-data/navaids.csv"
URL_OPENAIP_NAV = "https://storage.openaip.net/openaip-system-exports/{cc}_nav.json"
URL_NATURAL_EARTH = "https://raw.githubusercontent.com/nvkelso/natural-earth-vector/master/geojson/ne_110m_admin_0_countries.geojson"

# OpenAIP numeric navaid type mapping
OPENAIP_TYPE_MAP = {
    0: "VOR",
    1: "DME",
    2: "NDB",
    3: "TACAN",
    4: "VOR-DME",
    6: "VOR",
    7: "VOR-DME",
    8: "VORTAC",
}

CENTRAL_EUROPE_COUNTRIES = {"PL", "CZ", "SK", "DE", "AT", "HU", "CH", "SI", "LT"}

# Essential preset waypoints for EFIS route presets
ESSENTIAL_PRESETS = [
    {
        "ident": "KUKUS",
        "name": "KUKUS IFR ENROUTE FIX",
        "type": "FIX",
        "country": "PL",
        "lat": "50.843300",
        "lon": "17.525000",
        "elevation_ft": "",
        "frequency_khz": "",
        "channel": "",
        "usage": "ENROUTE",
    },
    {
        "ident": "DODUS",
        "name": "DODUS IFR ENROUTE FIX",
        "type": "FIX",
        "country": "PL",
        "lat": "50.518300",
        "lon": "18.291700",
        "elevation_ft": "",
        "frequency_khz": "",
        "channel": "",
        "usage": "ENROUTE",
    },
    {
        "ident": "VELIK",
        "name": "VELIK IFR ENROUTE FIX",
        "type": "FIX",
        "country": "CZ",
        "lat": "50.550000",
        "lon": "15.150000",
        "elevation_ft": "",
        "frequency_khz": "",
        "channel": "",
        "usage": "ENROUTE",
    },
    {
        "ident": "OKL",
        "name": "PRAHA VOR-DME",
        "type": "VOR-DME",
        "country": "CZ",
        "lat": "50.096500",
        "lon": "14.262800",
        "elevation_ft": "1247",
        "frequency_khz": "112600",
        "channel": "73X",
        "usage": "BOTH",
    },
    {
        "ident": "TRZ",
        "name": "TRZEBNICA VOR-DME",
        "type": "VOR-DME",
        "country": "PL",
        "lat": "51.308200",
        "lon": "17.114300",
        "elevation_ft": "830",
        "frequency_khz": "113600",
        "channel": "83X",
        "usage": "BOTH",
    },
    {
        "ident": "MTH",
        "name": "MARATHON NDB",
        "type": "NDB",
        "country": "US",
        "lat": "24.711900",
        "lon": "-81.095300",
        "elevation_ft": "5",
        "frequency_khz": "260",
        "channel": "",
        "usage": "BOTH",
    },
    {
        "ident": "DHP",
        "name": "DOLPHIN VORTAC",
        "type": "VORTAC",
        "country": "US",
        "lat": "25.800000",
        "lon": "-80.349000",
        "elevation_ft": "10",
        "frequency_khz": "113900",
        "channel": "86X",
        "usage": "BOTH",
    },
    {
        "ident": "PAE",
        "name": "PAINE VOR-DME",
        "type": "VOR-DME",
        "country": "US",
        "lat": "47.919800",
        "lon": "-122.278000",
        "elevation_ft": "606",
        "frequency_khz": "110600",
        "channel": "43X",
        "usage": "BOTH",
    },
]


def log(msg: str) -> None:
    print(f"[EFIS NAV] {msg}", flush=True)


def fetch_cached_bytes(url: str, filename: str, max_age_days: int = 7) -> bytes:
    """Fetch URL with local file cache to prevent redundant large downloads."""
    CACHE_DIR.mkdir(parents=True, exist_ok=True)
    cache_path = CACHE_DIR / filename
    if cache_path.exists():
        age = (time.time() - cache_path.stat().st_mtime) / 86400.0
        if age < max_age_days and cache_path.stat().st_size > 500:
            return cache_path.read_bytes()

    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    try:
        with urllib.request.urlopen(req, timeout=30) as resp:
            content = resp.read()
            cache_path.write_bytes(content)
            return content
    except Exception as e:
        if cache_path.exists():
            log(f"Warning: Fetching {url} failed ({e}), using cached copy.")
            return cache_path.read_bytes()
        raise


def load_country_boundaries() -> tuple[list[dict], set[str]]:
    """Loads Natural Earth 110m countries for fast point-in-polygon classification."""
    log("Loading country boundaries from Natural Earth (110m)...")
    content = fetch_cached_bytes(URL_NATURAL_EARTH, "ne_110m_countries.geojson")
    geo_data = json.loads(content.decode("utf-8"))

    rings: list[dict] = []
    european_isos: set[str] = set()

    for feature in geo_data.get("features", []):
        props = feature.get("properties", {})
        iso = props.get("ISO_A2") or props.get("ADM0_A3") or ""
        if iso == "-99" or not iso:
            iso = (props.get("ADM0_A3") or "")[:2]
        iso = iso.upper()

        continent = props.get("CONTINENT")
        if continent == "Europe" or iso in {"CY", "TR", "GE", "AM", "AZ"}:
            european_isos.add(iso)

        geom = feature.get("geometry", {})
        gtype = geom.get("type")
        coords = geom.get("coordinates", [])
        polys = []
        if gtype == "Polygon":
            polys.append(coords[0])
        elif gtype == "MultiPolygon":
            for poly in coords:
                polys.append(poly[0])

        for ring in polys:
            if not ring:
                continue
            lons = [p[0] for p in ring]
            lats = [p[1] for p in ring]
            rings.append({
                "iso": iso,
                "name": props.get("NAME", ""),
                "bbox": (min(lats), max(lats), min(lons), max(lons)),
                "ring": ring,
            })

    log(f"Loaded {len(rings)} country rings ({len(european_isos)} European countries identified).")
    return rings, european_isos


def point_in_ring(lat: float, lon: float, ring: list) -> bool:
    """Ray casting algorithm for 2D point-in-polygon."""
    inside = False
    n = len(ring)
    p1x, p1y = ring[0][0], ring[0][1]
    for i in range(1, n + 1):
        p2x, p2y = ring[i % n][0], ring[i % n][1]
        if min(p1y, p2y) < lat <= max(p1y, p2y):
            if lon <= max(p1x, p2x):
                if p1y != p2y:
                    xinters = (lat - p1y) * (p2x - p1x) / (p2y - p1y) + p1x
                if p1x == p2x or lon <= xinters:
                    inside = not inside
        p1x, p1y = p2x, p2y
    return inside


def classify_country(lat: float, lon: float, rings: list[dict]) -> str:
    """Finds ISO country code for coordinates, or empty string if international waters/unknown."""
    for c in rings:
        b = c["bbox"]
        if b[0] <= lat <= b[1] and b[2] <= lon <= b[3]:
            if point_in_ring(lat, lon, c["ring"]):
                return c["iso"]
    return ""


def load_openaip_navaids(country_codes: set[str]) -> dict[tuple[str, str], dict]:
    """Downloads OpenAIP navaid details for high-fidelity frequencies, channels, elevations."""
    results: dict[tuple[str, str], dict] = {}
    for cc in sorted(country_codes):
        url = URL_OPENAIP_NAV.format(cc=cc.lower())
        try:
            content = fetch_cached_bytes(url, f"{cc.lower()}_nav.json", max_age_days=14)
            data = json.loads(content.decode("utf-8"))
            for item in data:
                ident = (item.get("identifier") or "").strip().upper()
                if not ident:
                    continue
                coords = (item.get("geometry") or {}).get("coordinates") or []
                if len(coords) < 2:
                    continue
                lon, lat = float(coords[0]), float(coords[1])
                t_int = item.get("type")
                t_str = OPENAIP_TYPE_MAP.get(t_int, "VOR")
                freq_val = (item.get("frequency") or {}).get("value")
                freq_khz = ""
                if freq_val:
                    try:
                        f_flt = float(freq_val)
                        if f_flt < 1000.0 and t_str in {"VOR", "VOR-DME", "VORTAC", "DME"}:
                            freq_khz = str(int(round(f_flt * 1000)))
                        else:
                            freq_khz = str(int(round(f_flt)))
                    except Exception:
                        freq_khz = str(freq_val)

                elev_m = (item.get("elevation") or {}).get("value")
                elev_ft = ""
                if elev_m is not None:
                    try:
                        elev_ft = str(int(round(float(elev_m) * 3.28084)))
                    except Exception:
                        pass

                results[(ident, cc.upper())] = {
                    "ident": ident,
                    "name": (item.get("name") or ident).strip().upper(),
                    "type": t_str,
                    "country": cc.upper(),
                    "lat": f"{lat:.6f}",
                    "lon": f"{lon:.6f}",
                    "elevation_ft": elev_ft,
                    "frequency_khz": freq_khz,
                    "channel": (item.get("channel") or "").strip(),
                    "usage": "BOTH",
                }
        except Exception as e:
            # Country might not have OpenAIP nav export or is offline
            pass
    return results


def load_ourairports_navaids() -> list[dict]:
    """Downloads global navaids from OurAirports (VOR, NDB, TACAN, DME)."""
    log("Loading OurAirports worldwide navaids dataset...")
    content = fetch_cached_bytes(URL_OURAIRPORTS_NAVAIDS, "ourairports_navaids.csv")
    reader = csv.DictReader(io.StringIO(content.decode("utf-8", errors="ignore")))
    navaids: list[dict] = []
    for row in reader:
        ident = (row.get("ident") or "").strip().upper()
        if not ident:
            continue
        try:
            lat = float(row.get("latitude_deg") or 0)
            lon = float(row.get("longitude_deg") or 0)
        except ValueError:
            continue

        raw_type = (row.get("type") or "NAVAID").strip().upper()
        # Standardize type
        if "VOR" in raw_type and "DME" in raw_type:
            nav_type = "VOR-DME"
        elif "VORTAC" in raw_type:
            nav_type = "VORTAC"
        elif "VOR" in raw_type:
            nav_type = "VOR"
        elif "NDB" in raw_type:
            nav_type = "NDB"
        elif "TACAN" in raw_type:
            nav_type = "TACAN"
        elif "DME" in raw_type:
            nav_type = "DME"
        else:
            nav_type = raw_type

        usage = (row.get("usageType") or "").strip().upper()
        if not usage:
            usage = "BOTH"

        navaids.append({
            "ident": ident,
            "name": (row.get("name") or ident).strip().upper(),
            "type": nav_type,
            "country": (row.get("iso_country") or "").strip().upper(),
            "lat": f"{lat:.6f}",
            "lon": f"{lon:.6f}",
            "elevation_ft": (row.get("elevation_ft") or "").strip(),
            "frequency_khz": (row.get("frequency_khz") or "").strip(),
            "channel": (row.get("dme_channel") or "").strip(),
            "usage": usage,
        })
    log(f"Parsed {len(navaids)} radio navaids from OurAirports.")
    return navaids


def load_flightgear_fixes() -> list[tuple[str, float, float]]:
    """Downloads FlightGear / X-Plane standard navdata fix.dat (119k+ fixes)."""
    log("Loading FlightGear global fix.dat (IFR enroute intersections)...")
    content = fetch_cached_bytes(URL_FIX_DAT, "fix.dat.gz")
    buf = io.BytesIO(content)
    with gzip.GzipFile(fileobj=buf) as gz:
        lines = [line.decode("latin-1", errors="ignore").strip() for line in gz if line.strip()]

    fixes: list[tuple[str, float, float]] = []
    for line in lines[2:]:
        parts = line.split()
        if len(parts) >= 3:
            try:
                lat = float(parts[0])
                lon = float(parts[1])
                ident = parts[2].strip().upper()
                fixes.append((ident, lat, lon))
            except ValueError:
                continue

    log(f"Parsed {len(fixes)} IFR intersections/fixes from fix.dat.")
    return fixes


def main() -> int:
    parser = argparse.ArgumentParser(description="Download and export enroute IFR points CSV for EFIS.")
    parser.add_argument(
        "--scope",
        choices=["europe", "central-europe", "global", "custom"],
        default="europe",
        help="Geographic scope: 'europe' (default), 'central-europe', 'global', or 'custom'.",
    )
    parser.add_argument(
        "--country",
        default="",
        help="Comma-separated country ISO codes to filter by (e.g. PL,CZ,SK,DE,AT).",
    )
    parser.add_argument(
        "--bbox",
        default="",
        help="Bounding box min_lat,max_lat,min_lon,max_lon to filter waypoints.",
    )
    parser.add_argument(
        "--output",
        default="resources/navigation/enroute_points.csv",
        help="Path for destination CSV file.",
    )
    parser.add_argument(
        "--no-poc-copy",
        action="store_true",
        help="Do not copy output to poc/enroute_points.csv.",
    )
    parser.add_argument(
        "--include-fixes",
        action="store_true",
        default=True,
        help="Include 5-letter RNAV IFR enroute intersections (default: True).",
    )
    parser.add_argument(
        "--include-navaids",
        action="store_true",
        default=True,
        help="Include VOR, NDB, TACAN, DME radio navaids (default: True).",
    )

    args = parser.parse_args()

    # Determine filter countries
    selected_countries: set[str] = set()
    if args.country:
        selected_countries = {c.strip().upper() for c in args.country.split(",") if c.strip()}
    elif args.scope == "central-europe":
        selected_countries = CENTRAL_EUROPE_COUNTRIES

    # Custom bbox if given
    bbox_filter = None
    if args.bbox:
        parts = [float(x.strip()) for x in args.bbox.split(",")]
        bbox_filter = (parts[0], parts[1], parts[2], parts[3])

    # European bounding box rough filter for fast spatial screening (lat 34-72, lon -25 to 45)
    def in_europe_bounds(lat: float, lon: float) -> bool:
        return 34.0 <= lat <= 72.0 and -25.0 <= lon <= 45.0

    def in_ce_bounds(lat: float, lon: float) -> bool:
        return 45.0 <= lat <= 56.5 and 5.0 <= lon <= 25.5

    # 1. Load boundaries
    country_rings, european_isos = load_country_boundaries()

    # Target country filter
    if args.scope == "europe" and not selected_countries:
        selected_countries = european_isos

    # 2. Load OpenAIP navaids
    target_openaip_countries = selected_countries if selected_countries else {"PL", "CZ", "SK", "DE", "AT", "HU"}
    openaip_navaids = load_openaip_navaids(target_openaip_countries)
    log(f"Enriched {len(openaip_navaids)} navaids with high-resolution OpenAIP parameters.")

    # 3. Load OurAirports navaids
    navaids_all = load_ourairports_navaids() if args.include_navaids else []

    # 4. Load FlightGear fixes
    fixes_all = load_flightgear_fixes() if args.include_fixes else []

    # Final merged points map: key = (ident, round(lat, 2), round(lon, 2))
    records_dict: dict[tuple[str, int, int], dict] = {}

    def add_record(rec: dict) -> None:
        try:
            lat = float(rec["lat"])
            lon = float(rec["lon"])
        except (ValueError, KeyError):
            return

        ident = rec.get("ident", "").strip().upper()
        # Deduplicate colocated stations with same identifier within ~1.1 km
        key = (ident, int(round(lat * 100)), int(round(lon * 100)))

        if key in records_dict:
            existing = records_dict[key]
            # Prioritize VOR-DME / VORTAC over plain VOR or DME
            if "DME" in rec.get("type", "") and "DME" not in existing.get("type", ""):
                existing["type"] = rec["type"]
            # Merge missing fields
            for field in ["frequency_khz", "channel", "elevation_ft", "country"]:
                if not existing.get(field) and rec.get(field):
                    existing[field] = rec[field]
            return

        records_dict[key] = dict(rec)

    # 1. Add essential presets first
    for p in ESSENTIAL_PRESETS:
        add_record(p)

    # 2. Add OpenAIP official navaids
    log("Processing OpenAIP official AIP navaids...")
    for (ident, cc), nav in openaip_navaids.items():
        lat = float(nav["lat"])
        lon = float(nav["lon"])
        iso = nav["country"]
        if bbox_filter:
            if not (bbox_filter[0] <= lat <= bbox_filter[1] and bbox_filter[2] <= lon <= bbox_filter[3]):
                continue
        elif args.scope == "central-europe":
            if not (iso in CENTRAL_EUROPE_COUNTRIES or in_ce_bounds(lat, lon)):
                continue
        elif args.scope == "europe":
            if not (iso in european_isos or in_europe_bounds(lat, lon)):
                continue
        add_record(nav)

    # 3. Add OurAirports Navaids (fills in other European & worldwide stations)
    log("Processing and filtering OurAirports Radio Navaids...")
    for nav in navaids_all:
        lat = float(nav["lat"])
        lon = float(nav["lon"])
        iso = nav["country"]

        # Scope filters
        if bbox_filter:
            if not (bbox_filter[0] <= lat <= bbox_filter[1] and bbox_filter[2] <= lon <= bbox_filter[3]):
                continue
        elif args.scope == "central-europe":
            if not (iso in CENTRAL_EUROPE_COUNTRIES or in_ce_bounds(lat, lon)):
                continue
        elif args.scope == "europe":
            if not (iso in european_isos or in_europe_bounds(lat, lon)):
                continue

        add_record(nav)

    # Process Fixes / Intersections
    log("Processing and classifying IFR Enroute Fixes...")
    classified_fixes_count = 0
    for ident, lat, lon in fixes_all:
        if bbox_filter:
            if not (bbox_filter[0] <= lat <= bbox_filter[1] and bbox_filter[2] <= lon <= bbox_filter[3]):
                continue
        elif args.scope == "central-europe":
            if not in_ce_bounds(lat, lon):
                continue
        elif args.scope == "europe":
            if not in_europe_bounds(lat, lon):
                continue

        iso = classify_country(lat, lon, country_rings)

        if selected_countries and iso and iso not in selected_countries:
            continue

        rec = {
            "ident": ident,
            "name": f"{ident} FIX",
            "type": "FIX",
            "country": iso,
            "lat": f"{lat:.6f}",
            "lon": f"{lon:.6f}",
            "elevation_ft": "",
            "frequency_khz": "",
            "channel": "",
            "usage": "ENROUTE",
        }
        add_record(rec)
        classified_fixes_count += 1

    log(f"Included {classified_fixes_count} IFR fixes.")

    records = list(records_dict.values())

    # Sort records by country, ident
    records.sort(key=lambda r: (r.get("country") or "ZZ", r.get("ident") or "", r.get("type") or ""))

    # Statistics
    stats_types: dict[str, int] = {}
    stats_countries: dict[str, int] = {}
    for r in records:
        t = r["type"]
        c = r["country"] or "INTL"
        stats_types[t] = stats_types.get(t, 0) + 1
        stats_countries[c] = stats_countries.get(c, 0) + 1

    log(f"Total enroute navigation points ready: {len(records)}")
    log(f"Types breakdown: {stats_types}")
    top_countries = sorted(stats_countries.items(), key=lambda x: -x[1])[:10]
    log(f"Top 10 countries: {top_countries}")

    # Write output CSV
    repo_root = Path(__file__).resolve().parent.parent
    out_path = Path(args.output)
    if not out_path.is_absolute():
        out_path = repo_root / out_path
    out_path.parent.mkdir(parents=True, exist_ok=True)

    csv_columns = [
        "ident",
        "name",
        "type",
        "country",
        "lat",
        "lon",
        "elevation_ft",
        "frequency_khz",
        "channel",
        "usage",
    ]

    with open(out_path, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=csv_columns)
        writer.writeheader()
        for r in records:
            writer.writerow(r)

    file_size_kb = out_path.stat().st_size / 1024.0
    log(f"Saved {len(records)} points to {out_path} ({file_size_kb:.1f} KB)")

    # Optionally copy to poc/
    if not args.no_poc_copy:
        poc_path = repo_root / "poc" / "enroute_points.csv"
        poc_path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(out_path, poc_path)
        log(f"Copied to {poc_path}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
