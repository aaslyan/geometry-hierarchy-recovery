#!/usr/bin/env python3
"""Flatten a hierarchical GDS into the rectangle text format used by gds_recover.

This is the real-design counterpart to gds_roundtrip.py:
  input hierarchical .gds -> flatten with gdstk -> export axis-aligned rectangles.

The recoverer currently handles single-layer axis-aligned rectangles, so this
script reports and skips non-rectangular flattened polygons. Use --layer to crop
one GDS layer; otherwise all rectangular polygons are exported as one geometry
pool.
"""
import argparse
import math
import sys
from collections import Counter

import gdstk


def polygon_area(points):
    area = 0.0
    n = len(points)
    for i in range(n):
        x0, y0 = points[i]
        x1, y1 = points[(i + 1) % n]
        area += x0 * y1 - x1 * y0
    return abs(area) * 0.5


def as_axis_rect(poly, tol):
    pts = poly.points
    xs = [float(p[0]) for p in pts]
    ys = [float(p[1]) for p in pts]
    x0, x1 = min(xs), max(xs)
    y0, y1 = min(ys), max(ys)
    if x1 - x0 <= tol or y1 - y0 <= tol:
        return None

    ux = []
    uy = []
    for x in xs:
        if not any(abs(x - v) <= tol for v in ux):
            ux.append(x)
    for y in ys:
        if not any(abs(y - v) <= tol for v in uy):
            uy.append(y)
    if len(ux) != 2 or len(uy) != 2:
        return None

    box_area = (x1 - x0) * (y1 - y0)
    if abs(polygon_area(pts) - box_area) > max(tol, tol * box_area):
        return None
    return x0, y0, x1, y1


def ref_target_name(ref):
    if ref.cell is not None:
        return ref.cell.name
    return ref.cell_name


def repetition_count(ref):
    rep = ref.repetition
    if rep is None:
        return 1
    # gdstk.Repetition exposes columns/rows for rectangular repetitions.
    cols = getattr(rep, "columns", 1) or 1
    rows = getattr(rep, "rows", 1) or 1
    return int(cols) * int(rows)


def source_hierarchy_stats(lib):
    cells = list(lib.cells)
    refs = []
    for cell in cells:
        for ref in cell.references:
            refs.append((cell.name, ref_target_name(ref), repetition_count(ref)))
    top = lib.top_level()
    return cells, refs, top


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("gds", help="input hierarchical GDS")
    ap.add_argument("-o", "--out", default="flat_gds.txt",
                    help="output rectangle text file for build/gds_recover")
    ap.add_argument("--top", help="top cell name; defaults to the first top-level cell")
    ap.add_argument("--layer", type=int, help="only export this GDS layer")
    ap.add_argument("--datatype", type=int, help="only export this GDS datatype")
    ap.add_argument("--tol", type=float, default=1e-6,
                    help="rectangle recognition tolerance")
    args = ap.parse_args()

    lib = gdstk.read_gds(args.gds)
    cells, refs, tops = source_hierarchy_stats(lib)
    if not tops:
        raise SystemExit("no top-level cells found")
    if args.top:
        matches = [c for c in cells if c.name == args.top]
        if not matches:
            raise SystemExit(f"top cell not found: {args.top}")
        top = matches[0]
    else:
        top = tops[0]

    polys = top.get_polygons(depth=None)
    rects = []
    skipped = Counter()
    layer_counts = Counter()
    for p in polys:
        layer = getattr(p, "layer", 0)
        datatype = getattr(p, "datatype", 0)
        layer_counts[(layer, datatype)] += 1
        if args.layer is not None and layer != args.layer:
            skipped["other_layer"] += 1
            continue
        if args.datatype is not None and datatype != args.datatype:
            skipped["other_datatype"] += 1
            continue
        r = as_axis_rect(p, args.tol)
        if r is None:
            skipped["non_rect"] += 1
            continue
        rects.append(r)

    with open(args.out, "w") as f:
        f.write(
            f"# source_gds={args.gds} top={top.name} source_cells={len(cells)} "
            f"source_refs={len(refs)} flat_polygons={len(polys)} "
            f"flat_rects={len(rects)} skipped_non_rect={skipped['non_rect']}\n"
        )
        for x0, y0, x1, y1 in rects:
            f.write(f"{x0:.6f} {y0:.6f} {x1:.6f} {y1:.6f}\n")

    print(f"source: {args.gds}")
    print(f"top: {top.name}")
    print(f"source hierarchy: {len(cells)} cells, {len(refs)} direct reference records")
    if refs:
        expanded_refs = sum(n for _, _, n in refs)
        print(f"source references incl. array repetitions: {expanded_refs}")
    print(f"flattened polygons: {len(polys)}")
    print(f"exported rectangles: {len(rects)} -> {args.out}")
    if skipped:
        print("skipped:", ", ".join(f"{k}={v}" for k, v in sorted(skipped.items())))
    if layer_counts:
        common = ", ".join(
            f"({ly},{dt})={n}" for (ly, dt), n in layer_counts.most_common(8)
        )
        print(f"layer/datatype counts: {common}")

    if not rects:
        print("warning: no rectangles exported; try --layer/--datatype or fracture polygons",
              file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
