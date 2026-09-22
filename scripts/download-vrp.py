#!/usr/bin/env python3
"""Download OpenAIP visual reporting points (VRP / rpp) by country.

No API key. Same public daily exports as download-airspaces.py.

Example:
  python3 scripts/download-vrp.py --country PL,CZ
"""

from __future__ import annotations

import argparse
import json
import sys
import urllib.error
import urllib.request
from pathlib import Path

USER_AGENT = "efis-openaip/1.0"
EXPORT_URL = "https://storage.openaip.net/openaip-system-exports/{cc}_rpp.json"


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


def lat_lon(item: dict) -> tuple[float, float] | None:
    coords = ((item.get("geometry") or {}).get("coordinates")) or []
    if not isinstance(coords, list) or len(coords) < 2:
        return None
    return float(coords[1]), float(coords[0])


def summarize(payload: list) -> None:
    compulsory = 0
    samples: list[str] = []
    for item in payload:
        if item.get("compulsory"):
            compulsory += 1
        if len(samples) < 8:
            ll = lat_lon(item)
            elev = (item.get("elevation") or {}).get("value")
            flag = "C" if item.get("compulsory") else "O"
            where = f"{ll[0]:.4f},{ll[1]:.4f}" if ll else "?"
            samples.append(f"{flag} {item.get('name')} {where} {elev}m")
    log(f"  points={len(payload)} compulsory={compulsory} optional={len(payload) - compulsory}")
    for line in samples:
        log(f"  {line}")


def main() -> int:
    repo_root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--country", default="PL,CZ", help="ISO alpha-2 codes, comma-separated")
    parser.add_argument(
        "--dest",
        default=str(repo_root / "resources" / "vrp"),
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
        if not isinstance(payload, list):
            print(f"unexpected export format for {country}", file=sys.stderr)
            return 1
        path = dest / f"{country}_rpp.json"
        path.write_bytes(raw)
        log(f"wrote {path} ({len(raw)} bytes)")
        summarize(payload)
    return 0


if __name__ == "__main__":
    sys.exit(main())
