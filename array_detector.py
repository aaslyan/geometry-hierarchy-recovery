"""
array_detector.py

A working prototype of the occupancy-interval channel of the array-detection
design (see the design proposal, v4). This is a deliberately simplified
reference implementation, not a production version:

  - Uses only the occupancy-interval encoding (not the dual edge-boundary
    encoding). This works well for layouts with real gaps between features;
    it degrades for directly-abutted geometry with zero gap, where the
    edge-boundary channel would be needed instead. That limitation is
    surfaced directly by the standard-cell demo, on purpose.

  - Per-row period detection is a straightforward O(n^2) scan over
    candidate token-periods rather than a proper O(n log n) runs
    algorithm (Crochemore-style or otherwise) -- fine at demo scale,
    and swappable for a real runs implementation without changing
    anything downstream.

  - Cross-row reconciliation is a simplified, single-pass version of
    period-phase clustering: sequential greedy grouping of slabs with
    compatible (physical_period, phase, x-overlap), rather than the full
    graph-clustering / lattice-voting formulation. Good enough to
    demonstrate the core principle; the full formulation is the right
    target for a production version.

  - Defect tolerance is implemented directly: once a row's canonical
    period is fixed, every expected tile position across the row's
    full extent is checked, and mismatches are recorded rather than
    aborting the whole run.
"""

from dataclasses import dataclass, field
from typing import List, Tuple, Dict, Optional
import math

Rect = Tuple[float, float, float, float]

EPS = 1e-6


def snap(v: float, grid: float = 0.05) -> float:
    return round(v / grid) * grid


# ---------------------------------------------------------------------------
# 1. Finite scanline (slab) construction, with open-interval sampling
# ---------------------------------------------------------------------------

@dataclass
class Slab:
    y0: float
    y1: float
    y_mid: float
    covered: List[Tuple[float, float]] = field(default_factory=list)  # sorted, merged


def build_slabs(rects: List[Rect]) -> List[Slab]:
    ys = sorted({snap(r[1]) for r in rects} | {snap(r[3]) for r in rects})
    slabs = []
    for y0, y1 in zip(ys, ys[1:]):
        if y1 - y0 < EPS:
            continue
        y_mid = (y0 + y1) / 2.0  # strictly inside the open interval
        intervals = []
        for (x0, y0r, x1, y1r) in rects:
            if y0r < y_mid < y1r:
                intervals.append((x0, x1))
        intervals.sort()
        merged: List[Tuple[float, float]] = []
        for x0, x1 in intervals:
            if merged and x0 <= merged[-1][1] + EPS:
                merged[-1] = (merged[-1][0], max(merged[-1][1], x1))
            else:
                merged.append((x0, x1))
        slabs.append(Slab(y0, y1, y_mid, merged))
    return slabs


# ---------------------------------------------------------------------------
# 2. Tokenization: alternating covered / gap tokens with quantized widths
# ---------------------------------------------------------------------------

Token = Tuple[str, float]  # ('C', width) or ('G', width)


def tokenize(slab: Slab, width_quantum: float = 0.5) -> Tuple[List[Token], List[float]]:
    """Returns (tokens, start_x_of_each_token). No leading/trailing gap tokens
    (edge effects outside the first/last covered interval aren't part of any
    interior period and would just add noise)."""
    tokens: List[Token] = []
    starts: List[float] = []
    cov = slab.covered
    for i, (x0, x1) in enumerate(cov):
        starts.append(x0)
        tokens.append(("C", round((x1 - x0) / width_quantum) * width_quantum))
        if i + 1 < len(cov):
            gx0, gx1 = x1, cov[i + 1][0]
            starts.append(gx0)
            tokens.append(("G", round((gx1 - gx0) / width_quantum) * width_quantum))
    return tokens, starts


# ---------------------------------------------------------------------------
# 3. Per-row period detection (brute-force "runs"), then defect-tolerant scan
# ---------------------------------------------------------------------------

@dataclass
class RowRun:
    slab_index: int
    y0: float
    y1: float
    token_period: int
    physical_period: float
    phase: float                       # x_start mod physical_period
    unit_tokens: Tuple[Token, ...]      # canonical one-period token sequence
    x_start: float                      # x where the periodic pattern starts
    x_end: float                        # x where it ends
    n_expected: int                     # number of tile positions expected across [x_start, x_end]
    n_clean: int
    defect_positions: List[float]       # x-starts of mismatched expected positions


def find_row_period(tokens: List[Token], starts: List[float],
                     min_repeats: int = 3) -> Optional[Tuple[int, float]]:
    """Best (token_period, match_fraction) over candidate token-periods,
    preferring the smallest period that clears a match-fraction floor."""
    n = len(tokens)
    if n < min_repeats * 2:
        return None
    best = None
    for p in range(1, n // min_repeats + 1):
        matches = sum(1 for i in range(n - p) if tokens[i] == tokens[i + p])
        total = n - p
        if total <= 0:
            continue
        frac = matches / total
        if frac >= 0.6:
            best = (p, frac)
            break  # smallest qualifying period wins (primitive, not a multiple)
    return best


def analyze_row(slab_index: int, slab: Slab, tokens: List[Token], starts: List[float]
                 ) -> Optional[RowRun]:
    found = find_row_period(tokens, starts)
    if found is None:
        return None
    p_tok, _ = found

    # Canonical unit = majority vote per position within the period, over
    # whichever stretch of the row is internally consistent.
    n = len(tokens)
    votes: List[Dict[Token, int]] = [dict() for _ in range(p_tok)]
    for i, tok in enumerate(tokens):
        d = votes[i % p_tok]
        d[tok] = d.get(tok, 0) + 1
    unit = tuple(max(d.items(), key=lambda kv: kv[1])[0] for d in votes)
    physical_period = round(sum(w for _, w in unit), 3)
    if physical_period <= 0:
        return None

    # Trim leading/trailing tokens that don't belong to the periodic core
    # (e.g. a decorative border rectangle picked up by the same slab).
    # A boundary token index only counts as "inside the core" once a full
    # period-length window starting there is a clean match against the
    # canonical unit; this deliberately does NOT trim internal mismatches
    # (an isolated defect surrounded by good matches on both sides), only
    # contiguous non-matching content at the very start/end of the row.
    matches = [tokens[i] == unit[i % p_tok] for i in range(n)]
    lo = 0
    while lo + p_tok <= n and not all(matches[lo:lo + p_tok]):
        lo += 1
    hi = n
    while hi - p_tok >= 0 and not all(matches[hi - p_tok:hi]):
        hi -= 1
    if lo >= hi:
        lo, hi = 0, n  # no clean full period anywhere -- fall back to everything

    x_start = starts[lo]
    x_end = starts[hi - 1] + tokens[hi - 1][1]
    phase = round(x_start % physical_period, 3)

    # `unit` is indexed by absolute token position (i % p_tok). The scan
    # below re-slices tokens starting fresh from `lo`, so it must compare
    # against `unit` ROTATED to lo's own phase offset -- otherwise every
    # comparison silently checks the right tokens against the wrong
    # rotation of the pattern and fails even when the content is correct.
    aligned_unit = tuple(unit[(lo + k) % p_tok] for k in range(p_tok))

    # Defect-tolerant scan over the trimmed core only: walk every expected
    # tile position and check whether it matches the canonical unit.
    n_positions = max(1, round((x_end - x_start) / physical_period))
    n_clean = 0
    defect_positions = []
    for pos in range(n_positions):
        idxs = range(lo + pos * p_tok, lo + pos * p_tok + p_tok)
        actual = tuple(tokens[i] if i < hi else None for i in idxs)
        if actual == aligned_unit:
            n_clean += 1
        else:
            defect_positions.append(round(x_start + pos * physical_period, 2))

    return RowRun(
        slab_index=slab_index, y0=slab.y0, y1=slab.y1,
        token_period=p_tok, physical_period=physical_period, phase=phase,
        unit_tokens=unit, x_start=x_start, x_end=x_end,
        n_expected=n_positions, n_clean=n_clean, defect_positions=defect_positions,
    )


# ---------------------------------------------------------------------------
# 4. Cross-row reconciliation: group compatible rows into 2D candidates
# ---------------------------------------------------------------------------

@dataclass
class ArrayCandidate:
    x0: float
    y0: float
    x1: float
    y1: float
    dx: float
    dy: float
    n_rows: int
    n_expected_total: int
    n_clean_total: int
    defect_map: List[Tuple[float, float]]  # (x, y) of each defective expected position
    unit_tokens: Tuple[Token, ...]


def dist_mod(a: float, b: float, p: float) -> float:
    d = abs(a - b) % p
    return min(d, p - d)


def reconcile(rows: List[RowRun], eps_period: float = 0.75, eps_phase: float = 0.75,
              min_x_overlap: float = 5.0) -> List[ArrayCandidate]:
    rows_sorted = sorted(rows, key=lambda r: r.y0)
    candidates: List[ArrayCandidate] = []
    i = 0
    while i < len(rows_sorted):
        group = [rows_sorted[i]]
        j = i + 1
        while j < len(rows_sorted):
            prev, cur = group[-1], rows_sorted[j]
            same_period = abs(prev.physical_period - cur.physical_period) <= eps_period
            same_phase = dist_mod(prev.phase, cur.phase, prev.physical_period) <= eps_phase
            overlap = min(prev.x_end, cur.x_end) - max(prev.x_start, cur.x_start)
            if same_period and same_phase and overlap >= min_x_overlap:
                group.append(cur)
                j += 1
            else:
                break
        if len(group) >= 2:
            x0 = max(r.x_start for r in group)
            x1 = min(r.x_end for r in group)
            y0 = group[0].y0
            y1 = group[-1].y1
            dx = sum(r.physical_period for r in group) / len(group)
            ys = [r.y0 for r in group]
            dy = (ys[-1] - ys[0]) / (len(ys) - 1) if len(ys) > 1 else (y1 - y0)
            defect_map = []
            for r in group:
                for dxp in r.defect_positions:
                    if x0 - 1e-6 <= dxp <= x1 + 1e-6:
                        defect_map.append((dxp, (r.y0 + r.y1) / 2))
            n_expected_total = sum(r.n_expected for r in group)
            n_clean_total = sum(r.n_clean for r in group)
            unit = group[len(group) // 2].unit_tokens
            candidates.append(ArrayCandidate(
                x0=x0, y0=y0, x1=x1, y1=y1, dx=dx, dy=dy, n_rows=len(group),
                n_expected_total=n_expected_total, n_clean_total=n_clean_total,
                defect_map=defect_map, unit_tokens=unit,
            ))
        i = j if j > i else i + 1
    return candidates


# ---------------------------------------------------------------------------
# 5. Top-level driver
# ---------------------------------------------------------------------------

def detect_arrays(rects: List[Rect]) -> List[ArrayCandidate]:
    slabs = build_slabs(rects)
    rows: List[RowRun] = []
    for idx, slab in enumerate(slabs):
        tokens, starts = tokenize(slab)
        run = analyze_row(idx, slab, tokens, starts)
        if run is not None:
            rows.append(run)
    return reconcile(rows)


if __name__ == "__main__":
    from synthetic_layouts import bauhaus_layout, standard_cell_layout

    for gen in (bauhaus_layout, standard_cell_layout):
        rects, meta = gen()
        cands = detect_arrays(rects)
        print(f"\n=== {meta['name']} ({len(rects)} rects) ===")
        for c in cands:
            print(f"  region [{c.x0:.1f},{c.y0:.1f}]-[{c.x1:.1f},{c.y1:.1f}] "
                  f"dx={c.dx:.2f} dy={c.dy:.2f} rows={c.n_rows} "
                  f"instances={c.n_expected_total} clean={c.n_clean_total} "
                  f"defects={c.n_expected_total - c.n_clean_total}")
            if c.defect_map:
                print(f"    defect positions: {c.defect_map}")
