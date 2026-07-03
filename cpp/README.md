# Array Detector — Phase 1 (C++ / Boost.Geometry)

C++ implementation of the array-detection design in
`../repeat-cell-detection-proposal-v4.md`. Ports the validated mechanism from the
Python prototype (`../array_detector.py`) while **fixing** the three bugs
documented in `../CLAUDE.md`, and implements the parts the prototype left as
"known gaps": both §2.2 encodings and the real §2.4 period-phase reconciliation.

## Build & run

```sh
cmake -S .. -B ../build -DCMAKE_BUILD_TYPE=Release   # from this dir, or -S . from repo root
cmake --build ../build -j
../build/test_bugs      # Phase 1 regression tests (three bugs + the closed gap)
../build/test_hierarchy # Phase 2 regression tests (flatten==G, irregular, nesting)
../build/demo           # Phase 1: three array demos, writes *.svg
../build/hr_demo        # Phase 2: hierarchy recovery, writes hierarchy*.svg
../build/unify_demo     # array detection == hierarchy recovery (1 level) + lattice
```

Requires Boost.Geometry headers (`brew install boost`). No other dependencies;
the visualizer emits SVG directly. Rasterize with e.g. `rsvg-convert -z2 x.svg -o x.png`.

## Pipeline (maps 1:1 to the proposal's §2)

| File | Stage |
|---|---|
| `slab.{hpp,cpp}` | §2.1 finite scanlines: coordinate snapping + open-slab sampling |
| `encoding.{hpp,cpp}` | §2.2 both encodings — occupancy-interval (merged) **and** edge-boundary (raw, height-aware) |
| `runs.{hpp,cpp}` | §2.3 per-line repetition + §6 defect-tolerant scan |
| `reconcile.{hpp,cpp}` | §2.4 compatibility predicate, connected-component clustering, median/circular-median outputs, horizontal×vertical 2D intersection |
| `verify.{hpp,cpp}` | §2.5 canonicalized geometric verification + §6 missing/extra defect map (Boost boolean ops) |
| `detector.{hpp,cpp}` | top-level driver + candidate dedup + §7.5 funnel counts |
| `svg.{hpp,cpp}` | §8 visualization |
| `synthetic.{hpp,cpp}` | the two demo generators (ported from `synthetic_layouts.py`) |

## How the three documented bugs are prevented

- **Bug 1 (symbol collision).** Non-periodic edge content is trimmed against the
  canonical unit, and geometric verification (§2.5) is mandatory — a coincidental
  single-token width match can neither form a period nor survive verification.
  Test: `test_bug1_symbol_collision`.
- **Bug 2 (spurious sub-period from equal widths).** The period is chosen by
  self-consistency *relative to the best achievable* (`consistency_slack`), not an
  absolute floor. `[12,18,12,24]` scores ~0.875 at the half-period vs ~1.0 at the
  true period, so the true primitive wins. An absolute floor can't do this because
  gap tokens dilute the mismatch. Test: `test_bug2_spurious_subperiod`.
- **Bug 3 (phase desync across trim/defect).** The defect scan walks *expected
  physical positions* `x_start + m·dx`, looking up each one's geometry by physical
  x-window — never by token index. A missing instance is reported at its true x and
  nothing after it desyncs. Tests: `test_bug3_defect_phase` (end-to-end) and
  `test_bug3_row_scan_direct` (the row-scan mechanism itself).

## Phase 2 — hierarchy recovery (companion proposal)

Generalizes array detection to arbitrary repeated-cell discovery at *irregular*
placements, with nested cells (arrays-inside-arrays). Reuses Phase-1 snapping,
Boost canonical-equality, and the edge-encoding idea, exactly as the companion
doc's "reuse directly" list prescribes.

| File | Stage (companion §) |
|---|---|
| `hierarchy.{hpp}` | H = cell defs + instance placements + residual (§1.1) |
| `recover.{hpp,cpp}` | Channel-B shingles (§2.3) → seed-and-extend growth (§2.4) → MDL scoring (§2.6) → greedy set-packing (§2.7) → replace & recurse (§2.8); `flatten(H)==G` check (§6) |
| `synthetic_hier.{hpp,cpp}` | nested-motif demo (irregular placement + 2-level nesting) |
| `hr_svg.{hpp,cpp}` | hierarchy visualization |

| `d4.hpp` | dihedral group D4 — reflection + rotation support (§1.2 / §5.2) |

`hr_demo` runs three datasets: (1) a leaf motif at 13 irregular positions + a
super-cell at 3 positions (2 levels, 1.93× compression), (2) the same motif at all
8 D4 orientations recovered as one cell, and (3) a defect-tolerant case (one
instance missing a member). Transform scope is **full D4** (translation +
reflection + rotation); fractured polygons remain the open verification case.

`erasure_demo` runs the synthetic hierarchy-erasure benchmark (§7.1). `unify_demo`
demonstrates array detection ≡ one-level recovery + lattice fit.

**Real GDS** (`scripts/gds_roundtrip.py` + `build/gds_recover`): writes a real
hierarchical `.gds` with gdstk, flattens it with the real tool, strips metadata,
and recovers — closing the §7.1 loop on genuine GDS I/O. Requires
`python -m venv .venv && .venv/bin/pip install gdstk`, then
`.venv/bin/python scripts/gds_roundtrip.py 8 6 4 && ./build/gds_recover flat_gds.txt`.

Recovery is O(n) linear (~3 µs/rect; ~1.5M rects/5s single-round) after the
performance pass; hierarchies are certified by an exact O(n) rectangle-multiset
`flatten==G` check. See `NOTES.md` for the full performance and correctness story.

See `NOTES.md` (repo root) for every design decision taken where the proposals
left choices open, and the honest limitations found in both phases.
