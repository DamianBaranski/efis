#!/usr/bin/env python3
"""Build simplified European country rings from Natural Earth 110m (CC0)."""

from __future__ import annotations

import json
import math
import urllib.request
from pathlib import Path

SRC = "https://raw.githubusercontent.com/nvkelso/natural-earth-vector/master/geojson/ne_110m_admin_0_countries.geojson"
CACHE = Path("/tmp/ne_110m_admin_0_countries.geojson")
LON0, LON1 = -24.5, 44.5
LAT0, LAT1 = 34.2, 72.0
ISO_OVERRIDE = {"FRA": "FR", "NOR": "NO", "KOS": "XK"}
MIN_AREA = 1e-4


def load() -> dict:
    """Natural Earth 110m countries as GeoJSON."""
    if not CACHE.exists() or CACHE.stat().st_size < 1000:
        urllib.request.urlretrieve(SRC, CACHE)
    with CACHE.open() as handle:
        return json.load(handle)


def iso_of(props: dict) -> str:
    """ISO code from the feature properties."""
    a3 = str(props.get("ADM0_A3") or "")
    a2 = str(props.get("ISO_A2") or "")
    if a2 and a2 != "-99" and len(a2) == 2:
        return a2
    return ISO_OVERRIDE.get(a3, a3[:2] if a3 else "")


def want(props: dict) -> bool:
    """True for a European country the chart keeps."""
    iso = iso_of(props)
    if iso in {"TR", "CY"}:
        return True
    return props.get("CONTINENT") == "Europe"


def ring_coords(coords) -> list[tuple[float, float]]:
    """Longitude and latitude pairs of one ring."""
    ring = [(float(x), float(y)) for x, y in coords]
    if len(ring) >= 2 and ring[0] == ring[-1]:
        ring = ring[:-1]
    return ring


def iter_polys(geom):
    """Polygon parts of a geometry, including MultiPolygon members."""
    if geom is None or geom.is_empty:
        return
    kind = geom.geom_type
    if kind == "Polygon":
        if geom.area >= MIN_AREA:
            yield geom
    elif kind == "MultiPolygon":
        for part in geom.geoms:
            yield from iter_polys(part)
    elif kind == "GeometryCollection":
        for part in geom.geoms:
            yield from iter_polys(part)


def clip_feature(geom: dict):
    """Rings that remain after the chart longitude window."""
    from shapely.geometry import box, shape
    from shapely.validation import make_valid

    poly = make_valid(shape(geom))
    poly = make_valid(poly.intersection(box(LON0, LAT0, LON1, LAT1)))
    return list(iter_polys(poly))


def ensure_ccw(ring: list[tuple[float, float]]) -> list[tuple[float, float]]:
    """Ring wound counter-clockwise."""
    acc = 0.0
    n = len(ring)
    for i in range(n):
        x1, y1 = ring[i]
        x2, y2 = ring[(i + 1) % n]
        acc += x1 * y2 - x2 * y1
    return ring if acc > 0 else list(reversed(ring))


def triangulate(ring: list[tuple[float, float]]) -> list[tuple[int, int, int]]:
    """Triangle indexes for one ring. No holes."""
    import numpy as np
    import mapbox_earcut as earcut

    ring = ensure_ccw(ring)
    if len(ring) < 3:
        return []
    verts = np.array(ring, dtype=np.float64)
    rings = np.array([len(ring)], dtype=np.uint32)
    idx = earcut.triangulate_float64(verts, rings)
    tris = []
    for i in range(0, len(idx), 3):
        a, b, c = int(idx[i]), int(idx[i + 1]), int(idx[i + 2])
        if a == b or b == c or a == c:
            continue
        tris.append((a, b, c))
    return tris


def append_mesh(verts: list[tuple[float, float]], tris: list[int], ring: list[tuple[float, float]]) -> int:
    """Append one ring to the shared vertex and index lists."""
    local = triangulate(ring)
    if not local:
        return 0
    v0 = len(verts)
    verts.extend(ring)
    for a, b, c in local:
        tris.extend((v0 + a, v0 + b, v0 + c))
    return len(ring)


def emit_cpp(countries: list[dict], land_rings: list[list[tuple[float, float]]], dest_h: Path, dest_cc: Path) -> None:
    """Write europe_borders.h and europe_borders.cpp."""
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
            n = append_mesh(verts, tris_all, ring)
            if n < 3:
                continue
            rings_meta.append((v0, n))
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

    land_verts: list[tuple[float, float]] = []
    land_tris: list[int] = []
    for ring in land_rings:
        append_mesh(land_verts, land_tris, ring)

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
extern const float kEuropeLandLonLat[];
extern const unsigned kEuropeLandIndex[];
extern const EuropeRing kEuropeRings[];
extern const EuropeCountry kEuropeCountries[];
extern const int kEuropeVertexCount;
extern const int kEuropeIndexCount;
extern const int kEuropeLandVertexCount;
extern const int kEuropeLandIndexCount;
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
        f"const int kEuropeLandVertexCount = {len(land_verts)};",
        f"const int kEuropeLandIndexCount = {len(land_tris)};",
        f"const int kEuropeRingCount = {len(rings_meta)};",
        f"const int kEuropeCountryCount = {len(countries_meta)};",
        "",
    ]

    def emit_floats(name: str, pts: list[tuple[float, float]]) -> None:
        cc.append(f"const float {name}[] = {{")
        line = "   "
        for i, (lon, lat) in enumerate(pts):
            line += f" {lon:.5f}f,{lat:.5f}f,"
            if (i + 1) % 4 == 0:
                cc.append(line)
                line = "   "
        if line.strip():
            cc.append(line)
        cc.append("};")
        cc.append("")

    def emit_idx(name: str, idx: list[int]) -> None:
        cc.append(f"const unsigned {name}[] = {{")
        line = "   "
        for i, value in enumerate(idx):
            line += f" {value},"
            if (i + 1) % 12 == 0:
                cc.append(line)
                line = "   "
        if line.strip():
            cc.append(line)
        cc.append("};")
        cc.append("")

    emit_floats("kEuropeLonLat", verts)
    emit_idx("kEuropeIndex", tris_all)
    emit_floats("kEuropeLandLonLat", land_verts)
    emit_idx("kEuropeLandIndex", land_tris)
    cc.append("const EuropeRing kEuropeRings[] = {")
    for v0, n in rings_meta:
        cc.append(f"    {{{v0}, {n}}},")
    cc.append("};")
    cc.append("")
    cc.append("const EuropeCountry kEuropeCountries[] = {")
    for country in countries_meta:
        name = country["name"].replace("\\", "\\\\").replace('"', '\\"')
        cc.append(
            f'    {{"{country["iso"]}", "{name}", {country["ring0"]}, {country["nRings"]}, {country["tri0"]}, {country["nTris"]}}},'
        )
    cc.append("};")
    cc.append("")
    dest_h.write_text(h)
    dest_cc.write_text("\n".join(cc) + "\n")
    print(
        f"countries={len(countries_meta)} verts={len(verts)} tris={len(tris_all)//3} "
        f"landVerts={len(land_verts)} landTris={len(land_tris)//3} -> {dest_h} {dest_cc}"
    )


def main() -> int:
    """Regenerate the border tables. Returns 0."""
    from shapely.ops import unary_union
    from shapely.validation import make_valid

    root = Path(__file__).resolve().parent.parent
    data = load()
    countries = []
    all_polys = []
    for feat in data["features"]:
        props = feat["properties"]
        if not want(props):
            continue
        iso = iso_of(props)
        name = str(props.get("NAME") or props.get("ADMIN") or iso)
        if "N. Cyprus" in name or str(props.get("ADM0_A3")) == "CNM":
            continue
        polys = clip_feature(feat["geometry"])
        rings = []
        for poly in polys:
            ring = ring_coords(poly.exterior.coords)
            if len(ring) >= 3:
                rings.append(ring)
                all_polys.append(poly)
        if not rings:
            continue
        countries.append({"iso": iso, "name": name, "rings": rings})
    countries.sort(key=lambda c: c["iso"])
    land = make_valid(unary_union(all_polys))
    land_rings = [ring_coords(poly.exterior.coords) for poly in iter_polys(land)]
    land_rings = [ring for ring in land_rings if len(ring) >= 3]
    emit_cpp(countries, land_rings, root / "src" / "europe_borders.h", root / "src" / "europe_borders.cpp")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
