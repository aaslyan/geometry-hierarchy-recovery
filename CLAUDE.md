# CLAUDE.md — Repeat-Cell / Hierarchy-Recovery Project

This file is project context for Claude Code. Read it fully before writing any code.

## What this project is

Two related algorithms for detecting repeated structure in flat 2D polygon layouts
(IC/GDS-style layouts, but also decorative/artistic tilings):

1. **Array detection** — find rectangular regions where a tile repeats on a regular
   2D lattice, even when most of the layout isn't a perfect array (clutter, borders,
   non-repeating content, and localized defects are all expected).
2. **Hierarchy recovery** — the general case: find *any* repeated geometry anywhere
   in the layout, not just lattice-aligned instances, and infer a compact hierarchy
   (cell definitions + instance placements) via a minimum-description-length objective.

Array detection is a special case of hierarchy recovery (a cell whose instances happen
to lie on a uniform lattice). **Build array detection first** — it's the narrower,
better-specified, closer-to-shippable target. Hierarchy recovery reuses its
canonicalization and verification machinery directly; don't start both from scratch
in parallel.

## Design documents — read these before writing code

These are the source of truth for algorithm behavior. Don't re-derive the design;
implement what's specified, and flag it explicitly if something in these docs turns
out to be wrong or underspecified once real code hits it.

- `repeat-cell-detection-proposal-v4.md` — the array-detection design. This is the
  final version after three rounds of technical review; ignore v1–v3 of the same
  document (superseded, kept only for history).
- `hierarchy-recovery-proposal-v1.md` — the hierarchy-recovery design, explicitly
  framed as a companion/superset of the above.

Both documents include: formal problem statements, an explicit assumptions section
(read this — axis-aligned/rectilinear/single-layer scope, translation-only by
default), complexity analysis, known open questions, and an evaluation plan.

## Library choice: Boost.Geometry, not CGAL

Boost.Geometry is the primary library:
- Polygon/box primitives, boolean set ops (union/intersection/difference) for the
  defect computation (`missing = expected − observed`, etc.) and the canonicalized-
  equality verification step both designs specify.
- `boost::geometry::index::rtree` for the local-neighborhood spatial queries the
  hierarchy-recovery design's geometric-shingle channel needs.
- Permissive license, header-only, lighter weight than CGAL.

CGAL was considered and deliberately not chosen as the primary dependency: its
`Arrangement_2` package is the more general and more powerful tool, but it's built
for arbitrary planar subdivisions with exact-kernel arithmetic, and this project's
scope is explicitly rectilinear/axis-aligned. Reach for CGAL only if a genuine
non-rectilinear case shows up later — don't add it preemptively.

Target language: C++ (matches the intended production context — this is meant to
eventually run on real GDS-scale layouts).

## Reference implementation: read, don't blindly port

`array_detector.py` and `synthetic_layouts.py` are a working, bug-fixed Python
prototype of the array-detection design's core mechanism (occupancy-interval
channel only — the edge-boundary channel from the design isn't implemented in this
prototype; see "Known gaps" below). It exists to validate the algorithm cheaply
before committing to a C++ implementation, and it found three real bugs in the
process — all three are documented below because they're exactly the kind of thing
that will resurface in a C++ port if not deliberately guarded against. **Write unit
tests for all three before considering Phase 1 done.**

### Bug 1: symbol collision between unrelated content

A decorative border rectangle's width accidentally equalled a real motif token's
width, causing the border to be silently recognized as if it were repeated content.
This is not a one-off fluke — it's the general "edge-level symbol collision" problem
the design doc's Known Design Constraints section calls out explicitly. **Test case:**
construct a layout where a piece of non-repeating clutter has, by coincidence, the
exact same width as a real repeated token, and verify it's correctly excluded (or at
least doesn't silently corrupt period detection).

### Bug 2: degenerate/repeated widths create a spurious sub-period

Four cell widths where two happened to be equal (e.g. `[12, 18, 12, 24]`) created a
token sequence that legitimately satisfies a naive match-fraction threshold at half
the true period, because "smallest period that clears the threshold" isn't the same
as "true primitive period" once the content has internal coincidental symmetry.
**Test case:** a repeating unit with an internal repeated sub-value; verify the
detector finds the *true* primitive period (or the design's primitive/supercell/
pseudo-array classification correctly identifies the smaller match as spurious),
not just the shortest period that happens to pass a threshold.

### Bug 3: phase-alignment bug between the canonical unit and the defect scan (the important one)

This is the one worth reading carefully, because it's a direct, concrete instance of
something the design doc flagged only as a theoretical concern. The proposal's
"Open interaction to resolve" (array-detection doc, boundary alignment vs. defect
tolerance) warns that boundary handling needs to work against *expected* coordinates
projected from the confirmed hypothesis, not *observed* per-instance coordinates —
otherwise defects and boundary logic fight each other. The prototype's bug is a
sibling of exactly that problem: the canonical repeating unit is indexed by absolute
token position, but after trimming non-periodic content from a row's edges, the
defect-scanning loop re-sliced tokens starting fresh from the trim boundary without
accounting for that boundary's own phase offset — so it compared the right tokens
against a rotated (mismatched) version of the pattern, and a *second*, related issue:
once a real defect changes local token count, naive token-index-based scanning
desyncs from physical-position-based expected-tile counting for the rest of that row,
producing defects reported at the wrong x-position entirely.

**Do not reproduce this class of bug in the C++ version.** The design doc's own
prescription is the fix: defect/boundary checks should walk *expected* positions
computed from the confirmed physical period and phase (start_x + k·dx for integer k),
not token indices, and compare each expected position's actual content against the
canonical unit independently — token-index bookkeeping should never be load-bearing
across a defect. **Test case:** a row with a genuine missing instance in the middle;
verify defects after the missing instance are still attributed to the correct
physical x-position, not shifted.

### Known gaps in the prototype (not bugs, just unimplemented)

- Only the occupancy-interval encoding is implemented, not the dual edge-boundary
  encoding from the design. This works fine for layouts with real gaps between
  features (the Bauhaus demo) but degrades for directly-abutted geometry with zero
  gap (realistic standard-cell rows) — the standard-cell demo uses small explicit
  gaps specifically to route around this, which is itself informative: it's a live
  demonstration of why the design specifies *two* complementary encodings rather
  than one. The C++ version should implement both.
- Cross-row reconciliation is a simplified greedy sequential grouping, not the full
  graph-clustering / lattice-voting formulation from §2.4 of the design doc.
- No primitive/supercell/pseudo-array classification yet — everything found is
  treated as a flat "array."

## Phased roadmap

1. **Array detector, C++/Boost.Geometry.** Port the validated mechanism from the
   Python prototype, but fix (don't replicate) the three bugs above, and implement
   both encodings, not just occupancy-interval. Full period-phase reconciliation
   (§2.4 of the design doc) rather than the simplified greedy grouper.
2. **Demo datasets.** Two styles, both with realistic imperfection (clutter,
   non-repeating borders, at least one deliberate defect) — not just clean synthetic
   grids:
   - A "math art" / decorative style (Bauhaus-ish geometric motif grid — see the
     Python prototype's `bauhaus_layout()` for the existing starting point).
   - An industrial/standard-cell style (see `standard_cell_layout()`), and
     eventually a real flattened crop from SKY130/OpenROAD-flow-scripts per the
     design doc's evaluation plan.
3. **Defect-tolerant reporting**, matching the design doc's §6/7 report structure
   (clean/defective instance counts, defect map).
4. **Hierarchy recovery.** Only after (1)–(3) are solid. Reuses canonicalization
   and verification from the array detector.
5. **Real GDS evaluation** per the design doc's hierarchy-erasure benchmark
   (flatten a known hierarchical layout, strip metadata, recover, compare).
6. **Papers.** Not before there's real, benchmarked output to report on both
   algorithms — don't start writing these speculatively.

## What "done" looks like for Phase 1

A C++ program that: takes a layout (start with the same synthetic generators,
ported), detects array regions with correct dx/dy/tile/instance-count, correctly
reports defects at the right physical positions, passes unit tests for all three
bugs above, and renders a visualization (region overlay + defect markers) for both
demo datasets.
