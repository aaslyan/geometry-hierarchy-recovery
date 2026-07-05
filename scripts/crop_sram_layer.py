#!/usr/bin/env python3
"""Crop a small window of a hierarchical GDS array on ONE layer to the flat
rectangle text format used by nested_demo / gds_recover.

Motivation: a full sky130 SRAM flattens to millions of polygons across many
layers; the recoverer is single-layer / axis-aligned-rectangle. This selects the
references of one array cell whose origin lands inside a spatial window, rebuilds
just those into a temporary top cell, flattens it, and exports the rectangles of a
single (layer, datatype) — a genuine crop of real PDK geometry with real mirrored
placement, small enough to recover and inspect.

Usage:
  crop_sram_layer.py in.gds --cell ARRAY_CELL --layer L --datatype D \
      --win X0 Y0 X1 Y1 [--scale S] -o out.txt
"""
import argparse
import sys
from collections import Counter

import gdstk


def is_axis_rect(poly, tol):
    xs = [float(x) for x, y in poly.points]
    ys = [float(y) for x, y in poly.points]
    x0, x1 = min(xs), max(xs)
    y0, y1 = min(ys), max(ys)
    if x1 - x0 <= tol or y1 - y0 <= tol:
        return None
    ux = sorted({round(x, 4) for x in xs})
    uy = sorted({round(y, 4) for y in ys})
    if len(ux) != 2 or len(uy) != 2:
        return None
    return x0, y0, x1, y1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("gds")
    ap.add_argument("--cell", required=True, help="array cell to crop from")
    ap.add_argument("--layer", type=int, required=True)
    ap.add_argument("--datatype", type=int, required=True)
    ap.add_argument("--win", type=float, nargs=4, metavar=("X0", "Y0", "X1", "Y1"),
                    required=True, help="keep refs with origin inside this window (um)")
    ap.add_argument("--scale", type=float, default=1.0,
                    help="multiply all coordinates by this (e.g. 100 to work in "
                         "centi-microns so the default recoverer quantum resolves "
                         "sub-micron features)")
    ap.add_argument("--tol", type=float, default=1e-4)
    ap.add_argument("-o", "--out", default="flat_sram_crop.txt")
    args = ap.parse_args()

    lib = gdstk.read_gds(args.gds)
    byname = {c.name: c for c in lib.cells}
    if args.cell not in byname:
        raise SystemExit(f"cell not found: {args.cell}")
    arr = byname[args.cell]

    x0, y0, x1, y1 = args.win
    sel = [r for r in arr.references
           if x0 <= r.origin[0] <= x1 and y0 <= r.origin[1] <= y1]
    if not sel:
        raise SystemExit("no references in window")

    orient = Counter((round(r.rotation, 3), bool(r.x_reflection)) for r in sel)
    kinds = Counter(r.cell.name if r.cell else r.cell_name for r in sel)

    crop = gdstk.Cell("crop")
    for r in sel:
        crop.add(gdstk.Reference(r.cell, origin=r.origin, rotation=r.rotation,
                                 x_reflection=r.x_reflection,
                                 magnification=r.magnification))

    polys = crop.get_polygons(depth=None)
    rects = []
    kept_layer = 0
    for p in polys:
        if p.layer != args.layer or p.datatype != args.datatype:
            continue
        kept_layer += 1
        r = is_axis_rect(p, args.tol)
        if r is None:
            continue
        s = args.scale
        rects.append((r[0] * s, r[1] * s, r[2] * s, r[3] * s))

    with open(args.out, "w") as f:
        f.write(
            f"# source_gds={args.gds} cell={args.cell} layer={args.layer}/{args.datatype} "
            f"win={x0},{y0},{x1},{y1} scale={args.scale} refs_in_window={len(sel)} "
            f"cell_types={len(kinds)} orientations={len(orient)} "
            f"layer_polys={kept_layer} flat_rects={len(rects)}\n"
        )
        for r in rects:
            f.write(f"{r[0]:.4f} {r[1]:.4f} {r[2]:.4f} {r[3]:.4f}\n")

    print(f"cropped {len(sel)} bitcell refs ({dict(kinds)}) in window {args.win}")
    print(f"orientations present (rot,xreflect): {dict(orient)}")
    print(f"layer {args.layer}/{args.datatype}: {kept_layer} polys -> {len(rects)} rects "
          f"(scale {args.scale}) -> {args.out}")
    return 0 if rects else 2


if __name__ == "__main__":
    raise SystemExit(main())
