#!/usr/bin/env python3
"""Build simplified European country rings from Natural Earth 110m (CC0)."""

from __future__ import annotations

import json
import math
import os
import urllib.request
from pathlib import Path

SRC = "https://raw.githubusercontent.com/nvkelso/natural-earth-vector/master/geojson/ne_110m_admin_0_countries.geojson"
CACHE = Path("/tmp/ne_110m_admin_0_countries.geojson")
LON0, LON1 = -24.5, 44.5
LAT0, LAT1 = 34.2, 72.0
ISO_OVERRIDE = {"FRA": "FR", "NOR": "NO", "KOS": "XK"}


def load() -> dict:
    if not CACHE.exists() or CACHE.stat().st_size < 1000:
        urllib.request.urlretrieve(SRC, CACHE)
    with CACHE.open() as handle:
        return json.load(handle)


def iso_of(props: dict) -> str:
    a3 = str(props.get("ADM0_A3") or "")
    a2 = str(props.get("ISO_A2") or "")
    if a2 and a2 != "-99" and len(a2) == 2:
        return a2
    return ISO_OVERRIDE.get(a3, a3[:2] if a3 else "")


def want(props: dict) -> bool:
    iso = iso_of(props)
    if iso in {"TR", "CY"}:
        return True
    return props.get("CONTINENT") == "Europe"


def rings_of(geom: dict) -> list[list[tuple[float, float]]]:
    kind = geom["type"]
    coords = geom["coordinates"]
    polys = [coords] if kind == "Polygon" else coords
    out = []
    for poly in polys:
        if not poly:
            continue
        # Exterior only — schematic manager, lakes stay land.
        ring = [(float(x), float(y)) for x, y in poly[0]]
        if len(ring) >= 2 and ring[0] == ring[-1]:
            ring = ring[:-1]
        if len(ring) >= 3:
            out.append(ring)
    return out


def inside(p: tuple[float, float], edge: str) -> bool:
    x, y = p
    if edge == "left":
        return x >= LON0
    if edge == "right":
        return x <= LON1
    if edge == "bottom":
        return y >= LAT0
    return y <= LAT1


def intersect(a: tuple[float, float], b: tuple[float, float], edge: str) -> tuple[float, float]:
    ax, ay = a
    bx, by = b
    dx, dy = bx - ax, by - ay
    if edge == "left":
        t = 0 if dx == 0 else (LON0 - ax) / dx
        return (LON0, ay + t * dy)
    if edge == "right":
        t = 0 if dx == 0 else (LON1 - ax) / dx
        return (LON1, ay + t * dy)
    if edge == "bottom":
        t = 0 if dy == 0 else (LAT0 - ay) / dy
        return (ax + t * dx, LAT0)
    t = 0 if dy == 0 else (LAT1 - ay) / dy
    return (ax + t * dx, LAT1)


def clip_ring(ring: list[tuple[float, float]]) -> list[tuple[float, float]]:
    out = ring
    for edge in ("left", "right", "bottom", "top"):
        if not out:
            return []
        inp = out
        out = []
        prev = inp[-1]
        for cur in inp:
            if inside(cur, edge):
                if not inside(prev, edge):
                    out.append(intersect(prev, cur, edge))
                out.append(cur)
            elif inside(prev, edge):
                out.append(intersect(prev, cur, edge))
            prev = cur
    # drop duplicates
    cleaned: list[tuple[float, float]] = []
    for pt in out:
        if not cleaned or math.hypot(pt[0] - cleaned[-1][0], pt[1] - cleaned[-1][1]) > 1e-6:
            cleaned.append(pt)
    if len(cleaned) >= 2 and math.hypot(cleaned[0][0] - cleaned[-1][0], cleaned[0][1] - cleaned[-1][1]) <= 1e-6:
        cleaned.pop()
    return cleaned if len(cleaned) >= 3 else []


def area(ring: list[tuple[float, float]]) -> float:
    acc = 0.0
    n = len(ring)
    for i in range(n):
        x1, y1 = ring[i]
        x2, y2 = ring[(i + 1) % n]
        acc += x1 * y2 - x2 * y1
    return 0.5 * acc


def ensure_ccw(ring: list[tuple[float, float]]) -> list[tuple[float, float]]:
    return ring if area(ring) > 0 else list(reversed(ring))


def point_in_tri(p, a, b, c) -> bool:
    def sign(p1, p2, p3):
        return (p1[0] - p3[0]) * (p2[1] - p3[1]) - (p2[0] - p3[0]) * (p1[1] - p3[1])

    b1 = sign(p, a, b) < 0
    b2 = sign(p, b, c) < 0
    b3 = sign(p, c, a) < 0
    return b1 == b2 == b3


def is_convex(a, b, c) -> bool:
    return (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]) > 1e-12


def triangulate(ring: list[tuple[float, float]]) -> list[tuple[int, int, int]]:
    try:
        import numpy as np
        import mapbox_earcut as earcut
    except ImportError:
        return triangulate_ears(ring)
    ring = ensure_ccw(ring)
    verts = np.array(ring, dtype=np.float64)
    rings = np.array([len(ring)], dtype=np.uint32)
    idx = earcut.triangulate_float64(verts, rings)
    return [(int(idx[i]), int(idx[i + 1]), int(idx[i + 2])) for i in range(0, len(idx), 3)]


def triangulate_ears(ring: list[tuple[float, float]]) -> list[tuple[int, int, int]]:
    ring = ensure_ccw(ring)
    idx = list(range(len(ring)))
    tris: list[tuple[int, int, int]] = []
    guard = 0
    while len(idx) > 3 and guard < 10000:
        guard += 1
        n = len(idx)
        clipped = False
        for i in range(n):
            i0 = idx[(i - 1) % n]
            i1 = idx[i]
            i2 = idx[(i + 1) % n]
            a, b, c = ring[i0], ring[i1], ring[i2]
            if not is_convex(a, b, c):
                continue
            ear = True
            for j in idx:
                if j in (i0, i1, i2):
                    continue
                if point_in_tri(ring[j], a, b, c):
                    ear = False
                    break
            if not ear:
                continue
            tris.append((i0, i1, i2))
            del idx[i]
            clipped = True
            break
        if not clipped:
            break
    if len(idx) == 3:
        tris.append((idx[0], idx[1], idx[2]))
    return tris


def emit_cpp(countries: list[dict], dest_h: Path, dest_cc: Path) -> None:
    verts: list[tuple[float, float]] = []
    rings_meta = []
    countries_meta = []
    tris_all: list[int] = []
    for country in countries:
        ring0 = len(rings_meta)
        tri0 = len(tris_all) // 3
        n_ok = 0
        for ring in country["rings"]:
            v0 = len(verts)
            verts.extend(ring)
            local = triangulate(ring)
            if len(local) < 1:
                continue
            for a, b, c in local:
                tris_all.extend((v0 + a, v0 + b, v0 + c))
            rings_meta.append((v0, len(ring)))
            n_ok += 1
        if n_ok == 0:
            continue
        countries_meta.append(
            {
                "iso": country["iso"],
                "name": country["name"],
                "ring0": ring0,
                "nRings": n_ok,
                "tri0": tri0,
                "nTris": len(tris_all) // 3 - tri0,
            }
        )

    h = """#ifndef EUROPE_BORDERS_H
#define EUROPE_BORDERS_H

/// Simplified European country rings from Natural Earth 110m (public domain).
struct EuropeRing
{
    int vertex0;
    int count;
};

struct EuropeCountry
{
    const char *iso;
    const char *name;
    int ring0;
    int rings;
    int tri0;
    int tris;
};

extern const float kEuropeLonLat[];
extern const unsigned kEuropeIndex[];
extern const EuropeRing kEuropeRings[];
extern const EuropeCountry kEuropeCountries[];
extern const int kEuropeVertexCount;
extern const int kEuropeIndexCount;
extern const int kEuropeRingCount;
extern const int kEuropeCountryCount;
extern const float kEuropeLon0;
extern const float kEuropeLon1;
extern const float kEuropeLat0;
extern const float kEuropeLat1;

#endif
"""
    cc = [
        '#include "europe_borders.h"',
        "",
        f"const float kEuropeLon0 = {LON0}f;",
        f"const float kEuropeLon1 = {LON1}f;",
        f"const float kEuropeLat0 = {LAT0}f;",
        f"const float kEuropeLat1 = {LAT1}f;",
        f"const int kEuropeVertexCount = {len(verts)};",
        f"const int kEuropeIndexCount = {len(tris_all)};",
        f"const int kEuropeRingCount = {len(rings_meta)};",
        f"const int kEuropeCountryCount = {len(countries_meta)};",
        "",
        "const float kEuropeLonLat[] = {",
    ]
    line = "   "
    for i, (lon, lat) in enumerate(verts):
        line += f" {lon:.5f}f,{lat:.5f}f,"
        if (i + 1) % 4 == 0:
            cc.append(line)
            line = "   "
    if line.strip():
        cc.append(line)
    cc.append("};")
    cc.append("")
    cc.append("const unsigned kEuropeIndex[] = {")
    line = "   "
    for i, idx in enumerate(tris_all):
        line += f" {idx},"
        if (i + 1) % 12 == 0:
            cc.append(line)
            line = "   "
    if line.strip():
        cc.append(line)
    cc.append("};")
    cc.append("")
    cc.append("const EuropeRing kEuropeRings[] = {")
    for v0, n in rings_meta:
        cc.append(f"    {{{v0}, {n}}},")
    cc.append("};")
    cc.append("")
    cc.append("const EuropeCountry kEuropeCountries[] = {")
    for c in countries_meta:
        name = c["name"].replace("\\", "\\\\").replace('"', '\\"')
        cc.append(
            f'    {{"{c["iso"]}", "{name}", {c["ring0"]}, {c["nRings"]}, {c["tri0"]}, {c["nTris"]}}},'
        )
    cc.append("};")
    cc.append("")
    dest_h.write_text(h)
    dest_cc.write_text("\n".join(cc) + "\n")
    print(
        f"countries={len(countries_meta)} verts={len(verts)} tris={len(tris_all)//3} -> {dest_h} {dest_cc}"
    )


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    data = load()
    countries = []
    for feat in data["features"]:
        props = feat["properties"]
        if not want(props):
            continue
        iso = iso_of(props)
        name = str(props.get("NAME") or props.get("ADMIN") or iso)
        if "N. Cyprus" in name or str(props.get("ADM0_A3")) == "CNM":
            continue
        clipped = []
        for ring in rings_of(feat["geometry"]):
            got = clip_ring(ring)
            if got:
                clipped.append(got)
        if not clipped:
            continue
        countries.append({"iso": iso, "name": name, "rings": clipped})
    countries.sort(key=lambda c: c["iso"])
    emit_cpp(countries, root / "src" / "europe_borders.h", root / "src" / "europe_borders.cpp")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
