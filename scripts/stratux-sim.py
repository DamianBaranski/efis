#!/usr/bin/env python3
"""HTTP stand-in for Stratux /getSituation so EFIS can run with --stratux."""

from __future__ import annotations

import argparse
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from math import cos, radians, sin
from threading import Lock
from time import monotonic, sleep
from typing import Any


class Situation:
    def __init__(self, lat: float, lon: float, alt: float) -> None:
        self.lat = lat
        self.lon = lon
        self.alt = alt
        self.pitch = 0.0
        self.roll = 0.0
        self.heading = 0.0
        self.speed = 40.0
        self.lock = Lock()
        self.last = monotonic()

    def tick(self) -> None:
        now = monotonic()
        with self.lock:
            dt = now - self.last
            self.last = now
            lat_rad = radians(self.lat)
            meters_per_deg_lon = max(1000.0, 111320.0 * cos(lat_rad))
            self.lat += (self.speed * cos(radians(self.heading)) * dt) / 111320.0
            self.lon += (self.speed * sin(radians(self.heading)) * dt) / meters_per_deg_lon

    def as_json(self) -> dict[str, Any]:
        with self.lock:
            return {
                "AHRSGyroHeading": self.heading,
                "AHRSMagHeading": self.heading,
                "AHRSPitch": self.pitch,
                "AHRSRoll": self.roll,
                "BaroVerticalSpeed": 0.0,
                "GPSHeightAboveEllipsoid": self.alt,
                "GPSLatitude": self.lat,
                "GPSLongitude": self.lon,
                "GPSVerticalSpeed": 0.0,
            }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=5000)
    parser.add_argument("--lat", type=float, default=50.959167)
    parser.add_argument("--lon", type=float, default=16.770278)
    parser.add_argument("--alt", type=float, default=800.0)
    parser.add_argument("--heading", type=float, default=45.0, help="degrees, 0=north")
    parser.add_argument("--speed", type=float, default=40.0, help="m/s")
    args = parser.parse_args()

    situation = Situation(args.lat, args.lon, args.alt)
    situation.heading = args.heading
    situation.speed = args.speed

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, fmt: str, *log_args: object) -> None:
            return

        def do_GET(self) -> None:
            if self.path.split("?", 1)[0] != "/getSituation":
                self.send_error(404)
                return
            situation.tick()
            body = json.dumps(situation.as_json()).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

    server = ThreadingHTTPServer((args.host, args.port), Handler)
    print(f"Stratux sim http://{args.host}:{args.port}/getSituation")
    print("Run EFIS with: ./efis --stratux")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nstopped")
        sleep(0)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
