"""
synthetic_layouts.py

Two synthetic 2D layouts, both represented as flat lists of axis-aligned
rectangles (x0, y0, x1, y1), used to demonstrate the array-detection
algorithm on genuinely different structural styles:

  - bauhaus_layout(): a decorative, "math-art" style grid of a repeating
    two-part motif, with a non-repeating decorative border and one
    deliberately missing motif (a defect), in the spirit of the original
    motivating example: arrays that are regular in the middle but not
    globally perfect.

  - standard_cell_layout(): a synthetic industrial-style layout of
    standard-cell-like rows: cells of varying width, repeating in a fixed
    pattern within each row, plus routing-style clutter rectangles that
    are NOT part of any periodic structure, plus one missing cell (again,
    a defect) in one row.

Each generator returns (rects, meta) where meta carries ground-truth
information (used only for printing/validation, never fed to the detector).
"""

from dataclasses import dataclass, field
from typing import List, Tuple, Dict, Any

Rect = Tuple[float, float, float, float]


def bauhaus_layout() -> Tuple[List[Rect], Dict[str, Any]]:
    rects: List[Rect] = []

    # --- Repeating motif: a tall bar + an offset small square, non-overlapping ---
    # Tile footprint: 40 wide x 40 tall, motif occupies a sub-region of it.
    cols, rows = 8, 6
    pitch = 40.0
    origin_x, origin_y = 60.0, 60.0

    skip = {(3, 2)}  # (col, row) tile deliberately omitted -> defect

    for c in range(cols):
        for r in range(rows):
            if (c, r) in skip:
                continue
            tx = origin_x + c * pitch
            ty = origin_y + r * pitch
            # Rect A: tall bar
            rects.append((tx + 4, ty + 4, tx + 24, ty + 36))
            # Rect B: small square, offset to the right, only in the lower part
            rects.append((tx + 28, ty + 4, tx + 36, ty + 12))

    grid_x0 = origin_x
    grid_y0 = origin_y
    grid_x1 = origin_x + cols * pitch
    grid_y1 = origin_y + rows * pitch

    # --- Non-repeating decorative border (clutter): varied sizes, not on the pitch ---
    # Widths deliberately avoid {4, 8, 20} -- the token widths used by the
    # motif itself -- so border content doesn't accidentally masquerade as
    # a real repeated token when it lands in the same row slabs as the grid.
    border = [
        (grid_x0 - 45, grid_y0 - 30, grid_x1 + 45, grid_y0 - 10),   # bottom strip
        (grid_x0 - 45, grid_y1 + 10, grid_x1 + 45, grid_y1 + 30),  # top strip
        (grid_x0 - 45, grid_y0 - 30, grid_x0 - 10, grid_y1 + 30),  # left strip (width 35)
        (grid_x1 + 10, grid_y0 - 30, grid_x1 + 45, grid_y1 + 30),  # right strip (width 35)
        (grid_x0 - 22, grid_y0 - 22, grid_x0 - 16, grid_y0 - 16),  # corner ornament (below grid, no row overlap)
        (grid_x1 + 16, grid_y1 + 16, grid_x1 + 22, grid_y1 + 22),  # corner ornament (above grid, no row overlap)
        (grid_x0 + 55, grid_y1 + 14, grid_x0 + 145, grid_y1 + 26),  # title bar (above grid, no row overlap)
    ]
    rects.extend(border)

    meta = {
        "name": "bauhaus_grid",
        "grid_bbox": (grid_x0, grid_y0, grid_x1, grid_y1),
        "pitch": (pitch, pitch),
        "cols": cols,
        "rows": rows,
        "missing_tiles": skip,
    }
    return rects, meta


def standard_cell_layout() -> Tuple[List[Rect], Dict[str, Any]]:
    rects: List[Rect] = []

    row_height = 20.0
    row_pitch = 26.0     # includes inter-row channel
    n_rows = 5
    gap = 2.0            # small explicit gap between abutted cells
    cell_widths = [12.0, 18.0, 22.0, 27.0]  # four DISTINCT widths -- repeated
    # widths (e.g. [12, 18, 12, 24]) create a spurious half-period that
    # passes a naive match-fraction threshold; distinct widths avoid that.
    n_repeats = 20       # enough repeats of the 4-cell pattern to be detectable
    origin_x, origin_y = 40.0, 40.0

    row_missing = {2: 9}  # row index -> repeat index of a deliberately omitted cell

    row_extents = []
    for r in range(n_rows):
        x = origin_x
        y0 = origin_y + r * row_pitch
        y1 = y0 + row_height
        rep = 0
        while rep < n_repeats:
            w = cell_widths[rep % len(cell_widths)]
            if row_missing.get(r) == rep:
                x += w + gap  # leave a hole exactly where a cell would sit
                rep += 1
                continue
            rects.append((x, y0, x + w, y1))
            x += w + gap
            rep += 1
        row_extents.append((origin_x, y0, x - gap, y1))

    # --- Routing clutter: thin vertical wires at irregular x-positions, crossing
    # multiple rows. Fixed width 1.2 is far from any real cell width, so it
    # shows up as its own distinct token rather than accidentally colliding
    # with real content -- irregular clutter that's cleanly distinguishable,
    # not irregular clutter that happens to be ambiguous with the signal.
    import random
    rnd = random.Random(7)
    max_x = max(re[2] for re in row_extents) + 20
    max_y = origin_y + n_rows * row_pitch + 10
    for _ in range(10):
        rx = rnd.uniform(origin_x, max_x - 3)
        ry = rnd.uniform(origin_y - 15, max_y)
        rects.append((rx, ry, rx + 1.2, ry + rnd.uniform(15, 45)))

    meta = {
        "name": "standard_cell_rows",
        "row_extents": row_extents,
        "row_height": row_height,
        "row_pitch": row_pitch,
        "cell_widths": cell_widths,
        "gap": gap,
        "row_missing": row_missing,
    }
    return rects, meta


if __name__ == "__main__":
    for gen in (bauhaus_layout, standard_cell_layout):
        rects, meta = gen()
        print(meta["name"], "->", len(rects), "rectangles")
