#!/usr/bin/env python3
"""Real-GDS hierarchy-erasure harness (companion proposal 7.1).

Build a genuinely hierarchical GDS with gdstk (real cells + array/single
references, including mirrored rows the way place-and-route mirrors std-cell
rows), write it to a .gds file, then read it back and FLATTEN it with the real
tool -- discarding all cell/instance/array metadata. The flattened rectangles are
written to a text file for the C++ recoverer, and the known construction is the
ground truth. Geometry is synthetic but the GDS format, hierarchy, mirroring, and
flattening are all real; dropping in a real SKY130 cell .gds would use the same
read + flatten + export path.
"""
import sys
import gdstk

LAYER = 0


def build_library(n_gates, n_rows, n_blocks):
    lib = gdstk.Library("erasure")

    # Leaf "gate": four asymmetric rectangles (asymmetric so mirroring is visible).
    gate = lib.new_cell("gate")
    for (x0, y0, x1, y1) in [(0, 0, 6, 16), (8, 0, 14, 6), (8, 9, 14, 16), (0, 18, 14, 21)]:
        gate.add(gdstk.rectangle((x0, y0), (x1, y1), layer=LAYER))

    gate_pitch, row_pitch = 22, 26

    # slice = a horizontal ARRAY reference (AREF) of the gate.
    slc = lib.new_cell("slice")
    slc.add(gdstk.Reference(gate, (0, 0), columns=n_gates, rows=1,
                            spacing=(gate_pitch, 0)))

    # block = n_rows slices stacked, alternate rows mirrored about x (x_reflection),
    # so adjacent rows share a rail -- exactly how std-cell rows are placed.
    block = lib.new_cell("block")
    for j in range(n_rows):
        block.add(gdstk.Reference(slc, (0, j * row_pitch),
                                  x_reflection=(j % 2 == 1)))

    # top = a row of blocks (AREF) plus non-repeating routing clutter.
    bbox = block.bounding_box()
    block_w = bbox[1][0] - bbox[0][0]
    block_pitch = block_w + 14
    top = lib.new_cell("top")
    top.add(gdstk.Reference(block, (0, 0), columns=n_blocks, rows=1,
                            spacing=(block_pitch, 0)))
    # clutter: a few thin non-repeating wires
    for i in range(max(2, n_blocks)):
        x = 10 + i * block_pitch * 0.7
        top.add(gdstk.rectangle((x, -12), (x + 1.3, -12 + 30 + (i % 5) * 6), layer=LAYER))

    gt = {
        "cells": 4,  # gate, slice, block, top
        "gate_rects": 4,
        "gate_instances": n_gates * n_rows * n_blocks,
        "levels": 4,
    }
    return lib, gt


def flatten_to_rects(gds_path):
    """Read the GDS back and flatten it to axis-aligned rectangles."""
    lib = gdstk.read_gds(gds_path)
    top = lib.top_level()[0]
    polys = top.get_polygons(depth=None)  # fully flattened, references expanded
    rects = []
    for p in polys:
        pts = p.points
        xs = [pt[0] for pt in pts]
        ys = [pt[1] for pt in pts]
        rects.append((min(xs), min(ys), max(xs), max(ys)))
    return rects


def main():
    n_gates = int(sys.argv[1]) if len(sys.argv) > 1 else 8
    n_rows = int(sys.argv[2]) if len(sys.argv) > 2 else 6
    n_blocks = int(sys.argv[3]) if len(sys.argv) > 3 else 4
    gds_path = "design.gds"
    rect_path = "flat_gds.txt"

    lib, gt = build_library(n_gates, n_rows, n_blocks)
    lib.write_gds(gds_path)

    rects = flatten_to_rects(gds_path)
    with open(rect_path, "w") as f:
        # header line carries ground truth for the C++ side to echo
        f.write(f"# ground_truth cells={gt['cells']} gate_rects={gt['gate_rects']} "
                f"gate_instances={gt['gate_instances']} levels={gt['levels']} "
                f"flat_rects={len(rects)}\n")
        for (x0, y0, x1, y1) in rects:
            f.write(f"{x0:.4f} {y0:.4f} {x1:.4f} {y1:.4f}\n")

    print(f"wrote {gds_path} (hierarchical) and {rect_path} ({len(rects)} flat rects)")
    print(f"ground truth: {gt['cells']} cells, gate={gt['gate_rects']} rects x "
          f"{gt['gate_instances']} instances, {gt['levels']} levels")


if __name__ == "__main__":
    main()
