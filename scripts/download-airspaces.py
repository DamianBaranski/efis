#!/usr/bin/env python3
"""Download OpenAIP airspace GeoJSON (polygons + floor/ceiling) by country.

No API key. Same public exports as download-airports.py.

Example:
  python3 scripts/download-airspaces.py --country PL,CZ
"""

from __future__ import annotations

import argparse
import json
import sys
import urllib.error
import urllib.request
from pathlib import Path

USER_AGENT = "efis-openaip/1.0"
EXPORT_URL = "https://storage.openaip.net/openaip-system-exports/{cc}_asp.geojson"

# OpenAIP airspace type enum (core API).
TYPE_NAMES = {
    1: "restricted",
    2: "danger",
    3: "prohibited",
    4: "CTR",
    5: "TMZ",
    6: "RMZ",
    7: "TMA",
    13: "ATZ",
    14: "MATZ",
}


def log(message: str) -> None:
    print(message, flush=True)


def fetch_bytes(url: str) -> bytes:
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    try:
        with urllib.request.urlopen(request, timeout=120) as response:
            return response.read()
    except urllib.error.HTTPError as exc:
        raise SystemExit(f"HTTP {exc.code} {url}") from exc
    except (urllib.error.URLError, TimeoutError, OSError) as exc:
        raise SystemExit(f"failed {url}: {exc}") from exc


def parse_countries(value: str) -> list[str]:
    codes = []
    for part in value.replace(";", ",").split(","):
        code = part.strip().lower()
        if code:
            codes.append(code)
    return codes


def summarize(payload: dict) -> None:
    features = payload.get("features") or []
    kept = 0
    samples: list[str] = []
    for feature in features:
        props = feature.get("properties") or {}
        kind = props.get("type")
        if kind not in TYPE_NAMES:
            continue
        kept += 1
        if len(samples) < 8:
            lower = props.get("lowerLimit") or {}
            upper = props.get("upperLimit") or {}
            samples.append(
                f"{TYPE_NAMES[kind]} {props.get('name')} "
                f"{lower.get('value')}{lower.get('unit')}/{lower.get('referenceDatum')} -> "
                f"{upper.get('value')}{upper.get('unit')}/{upper.get('referenceDatum')}"
            )
    log(f"  features={len(features)} drawable={kept}")
    for line in samples:
        log(f"  {line}")


def main() -> int:
    repo_root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--country", default="PL,CZ", help="ISO alpha-2 codes, comma-separated")
    parser.add_argument(
        "--dest",
        default=str(repo_root / "resources" / "airspaces"),
        help="output directory",
    )
    args = parser.parse_args()

    countries = parse_countries(args.country)
    if not countries:
        print("no countries given", file=sys.stderr)
        return 1

    dest = Path(args.dest)
    dest.mkdir(parents=True, exist_ok=True)
    for country in countries:
        url = EXPORT_URL.format(cc=country)
        log(f"fetch {url}")
        raw = fetch_bytes(url)
        payload = json.loads(raw.decode("utf-8"))
        if payload.get("type") != "FeatureCollection":
            print(f"unexpected export format for {country}", file=sys.stderr)
            return 1
        path = dest / f"{country}_asp.geojson"
        path.write_bytes(raw)
        log(f"wrote {path} ({len(raw)} bytes)")
        summarize(payload)
    return 0


if __name__ == "__main__":
    sys.exit(main())
