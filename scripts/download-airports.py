#!/usr/bin/env python3
"""Download OpenAIP airport/runway lists (including small fields) into a CSV.

Uses the public country export, which includes civil airfields, ultralight sites,
and unnamed strips. No API key required.

Example:
  python3 scripts/download-airports.py --country PL --check EPMR,EPWS
"""

from __future__ import annotations

import argparse
import csv
import json
import sys
import urllib.error
import urllib.request
from pathlib import Path

USER_AGENT = "efis-openaip/1.0"
EXPORT_URL = "https://storage.openaip.net/openaip-system-exports/{cc}_apt.json"

AIRPORT_TYPES = {
    0: "airport",
    1: "glider_site",
    2: "airfield_civil",
    3: "international",
    4: "heliport_military",
    5: "military",
    6: "ultralight",
    7: "heliport_civil",
    8: "closed",
    9: "ifr",
    10: "water",
    11: "landing_strip",
    12: "agricultural",
    13: "altiport",
}

SURFACE_TYPES = {
    0: "asphalt",
    1: "concrete",
    2: "grass",
    3: "sand",
    4: "water",
    5: "grit",
    6: "ice",
    7: "snow",
    8: "clay",
    9: "earth",
    10: "gravel",
    11: "dirt",
}

CSV_FIELDS = [
    "icao",
    "name",
    "country",
    "type",
    "lat",
    "lon",
    "elev_m",
    "runway",
    "heading_deg",
    "length_m",
    "width_m",
    "surface",
]


def log(message: str) -> None:
    print(message, flush=True)


def fetch_json(url: str) -> list | dict:
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    try:
        with urllib.request.urlopen(request, timeout=60) as response:
            return json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as exc:
        raise SystemExit(f"HTTP {exc.code} {url}") from exc
    except (urllib.error.URLError, TimeoutError, OSError) as exc:
        raise SystemExit(f"failed {url}: {exc}") from exc


def measure(obj: dict | None, key: str) -> str:
    if not obj:
        return ""
    value = obj.get(key) or {}
    if not isinstance(value, dict) or value.get("value") is None:
        return ""
    return str(value["value"])


def airport_rows(airport: dict) -> list[dict]:
    coords = (airport.get("geometry") or {}).get("coordinates") or [None, None]
    lon, lat = coords[0], coords[1]
    elevation = airport.get("elevation") or {}
    icao = (airport.get("icaoCode") or "").strip()
    base = {
        "icao": icao,
        "name": airport.get("name") or "",
        "country": airport.get("country") or "",
        "type": AIRPORT_TYPES.get(airport.get("type"), str(airport.get("type", ""))),
        "lat": f"{lat:.8f}" if isinstance(lat, (int, float)) else "",
        "lon": f"{lon:.8f}" if isinstance(lon, (int, float)) else "",
        "elev_m": "" if elevation.get("value") is None else str(elevation["value"]),
    }

    runways = airport.get("runways") or []
    rows = []
    for runway in runways:
        surface = runway.get("surface") or {}
        composite = surface.get("mainComposite")
        rows.append(
            {
                **base,
                "runway": runway.get("designator") or "",
                "heading_deg": "" if runway.get("trueHeading") is None else str(runway["trueHeading"]),
                "length_m": measure(runway.get("dimension"), "length"),
                "width_m": measure(runway.get("dimension"), "width"),
                "surface": SURFACE_TYPES.get(composite, "" if composite is None else str(composite)),
            }
        )
    return rows


def parse_countries(value: str) -> list[str]:
    codes = []
    for part in value.replace(";", ",").split(","):
        code = part.strip().upper()
        if code:
            codes.append(code)
    return codes


def main() -> int:
    repo_root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--country",
        default="PL",
        help="ISO alpha-2 country codes, comma-separated (default PL)",
    )
    parser.add_argument(
        "--dest",
        default=str(repo_root / "resources" / "airports" / "airports.csv"),
        help="output CSV path",
    )
    parser.add_argument(
        "--check",
        default="EPMR,EPWS",
        help="ICAO codes that must be present (comma-separated)",
    )
    args = parser.parse_args()

    countries = parse_countries(args.country)
    if not countries:
        print("no countries given", file=sys.stderr)
        return 1

    rows: list[dict] = []
    seen_icao: set[str] = set()
    airport_count = 0
    for country in countries:
        url = EXPORT_URL.format(cc=country.lower())
        log(f"fetch {url}")
        payload = fetch_json(url)
        if not isinstance(payload, list):
            print(f"unexpected export format for {country}", file=sys.stderr)
            return 1
        airport_count += len(payload)
        for airport in payload:
            icao = (airport.get("icaoCode") or "").strip().upper()
            if icao:
                seen_icao.add(icao)
            rows.extend(airport_rows(airport))
        log(f"{country}: {len(payload)} airports")

    dest = Path(args.dest)
    dest.parent.mkdir(parents=True, exist_ok=True)
    with dest.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=CSV_FIELDS)
        writer.writeheader()
        writer.writerows(rows)
    log(f"wrote {dest} airports={airport_count} runways={len(rows)}")

    required = [code.strip().upper() for code in args.check.split(",") if code.strip()]
    missing = [code for code in required if code not in seen_icao]
    for code in required:
        hits = [row for row in rows if row["icao"] == code]
        if hits:
            rwy = ", ".join(f"{row['runway']} {row['length_m']}m" for row in hits)
            log(f"check {code} OK ({hits[0]['name']}; {rwy})")
        else:
            log(f"check {code} MISSING")
    if missing:
        print("missing ICAO: " + ", ".join(missing), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
