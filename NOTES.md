# Phase 1 — implementation notes & flagged design decisions

The v4 proposal (§9) explicitly carries several open questions, and a few points
were underspecified once real code hit them. Per `CLAUDE.md` ("flag it explicitly
if something in these docs turns out to be wrong or underspecified"), here is
every place I had to *decide* rather than *transcribe*, so it can be checked
against intent.

## Decisions taken where v4 left a choice open

1. **Reconciliation formulation (v4 §9 Q1: graph clustering vs. lattice voting).**
   Implemented as **connected-component clustering** under the §2.4 compatibility
   predicate (`reconcile.cpp`), the graph view. The Hough/voting view is not
   implemented. The predicate is genuinely pairwise (each pair judged on its own
   period/phase/overlap).

2. **Acceptance statistic (v4 §9 Q2: min-pairwise vs. MST-weakest-edge vs.
   average).** I used neither the average (which v4 warns against) nor MST-weakest
   directly. Instead: form components under the hard predicate, then compute a
   robust cluster center (median period, circular-median phase) and **prune any
   member incompatible with that center**, recomputing once. This kills the
   transitive weak-link chain the §2.4 caution is about, without an MST pass. If
   you specifically want the MST-weakest-edge statistic evaluated, that's a
   drop-in change.

3. **Circular median (v4 §9 Q3 / §2.4).** Implemented as the φ minimizing
   `Σ dist_mod(φ, φᵢ; p)`, searched over the observed phases **plus pairwise
   midpoints (and their antipodes)** as candidates — the "finer search" v4 leaves
   unspecified. Not a closed form, as v4 notes.

4. **Cluster spatial extent.** v4 says the candidate rectangle is "the spatial
   extent of the cluster" without pinning down intersection vs. union. I use the
   **union** of member supports. The intersection collapses the reported rectangle
   to a single row/column whenever member supports vary (a clutter-truncated row,
   a column spanning only part of the array) — verified in testing. The
   compatibility predicate already requires pairwise overlap ≥ `w_min`, so members
   are genuine co-lattice evidence and the union is the honest footprint.

5. **Defect vs. boundary-snapping interaction (v4 §9 Q4 / §2.4 open item).** v4's
   prescription — check against **expected** positions projected from the
   confirmed hypothesis, not observed per-instance coordinates — is implemented
   directly: `runs.cpp` walks `x_start + m·dx`, and `verify.cpp` walks the full
   `(i,j)` lattice. I did **not** implement the strict "tile boundaries ∈
   X_events/Y_events" snapping rule from §2.4; cluster extent + geometric
   verification stand in for it. That rule would tighten boundary placement on
   noisier real data and is the natural next hardening step.

6. **§6 defect taxonomy — "clean" definition.** An instance is counted **clean
   when none of its own geometry is missing**; overlaid *extra* geometry (routing
   over a filler/std-cell array) is reported separately (`n_extra`, orange markers)
   rather than making the instance a structural defect. This is what lets the
   gapped standard-cell demo certify the array *and* still report its 15 routing
   overlays and its 1 genuine missing cell distinctly. The pseudo-array acceptance
   gate keys on missing geometry only.

7. **Primitive/supercell/pseudo-array classification (§2.4).** Primitive vs.
   supercell is decided **symbolically** (a sub-period that near-repeats above
   `supercell_floor` but isn't itself the invariant period ⇒ supercell). The
   geometric confirmation v4 describes ("content is *not* invariant under dx
   alone") is not run as a separate check — verification confirms the chosen tile,
   not the negative. Pseudo-arrays are rejected by the verification clean-fraction
   gate. All three demos classify as primitive, correctly.

8. **§4 candidate scoring is not implemented.** v4 §9 Q5 leaves the
   normalized-product vs. log-weighted-sum choice open; rather than pick one
   speculatively, candidates are accepted by geometric verification and
   de-duplicated by bbox IoU (> 0.5). Ranking matters when many hypotheses
   compete; at demo scale verification alone suffices. Wiring in the §4 score is
   isolated to `detector.cpp`.

9. **Edge-boundary encoding realization (§2.2).** For rectilinear input I encode
   the **raw, unmerged** per-rectangle spans with each filled token keyed on
   `(width, edge-height)`, no gap token between abutted cells. This is the concrete
   interpretation of "every edge characterized by its projection vector"
   specialized to axis-aligned boxes; it's what makes the encoding survive
   zero-gap geometry (two touching cells stay two tokens).

10. **Runs algorithm.** Per-line period detection is the prototype's O(n²)
    self-consistency scan, not a linear-time runs algorithm. v4 §2.3 and the
    prototype both call this swappable without downstream impact; unchanged here.

## Phase 2 — hierarchy recovery: decisions taken

Implemented per the companion proposal (`hierarchy-recovery-proposal-v1.md`),
reusing Phase-1 snapping, Boost canonical-equality, and the edge-encoding idea.
Where the companion doc left surface open (it explicitly has "substantially more
open design surface"), here is what I decided:

1. **Transform scope = full D4 (translation + reflection + rotation).** This is
   the companion §1.2 / §5.2 decision, made on purpose and *implemented*: the 8
   axis-preserving orientations (the GDS orientation set) live in `d4.hpp`.
   Instances carry an `orient` field (§1.2's explicit requirement), grouping is by
   a D4-canonical signature (min over the 8 orientations), matching tries all 8,
   growth and flatten compose orientations through nesting. Demonstrated by
   `oriented_motif_layout`: one motif recovered at all 8 orientations as a single
   cell, `flatten == G` exact. (Reflection is exactly what alternating, power-rail-
   sharing standard-cell rows need.)

2. **Channel B only** (geometric shingles / local hashing). The doc calls B the
   "primary, more structurally sound" generator and A (multi-serialization
   mining) a "cheap but structurally incomplete" supplement (§2.3). A is not
   implemented; worth an ablation (§5 Q1) before adding it.

3. **Seed = k-NN with k=1, completed by growth** (§2.4). Small seed on purpose:
   a large fixed neighborhood would fold per-instance clutter into the signature
   and split true instances apart. Seed-and-extend adds only primitives present
   at a consistent offset around *every* instance, so clutter can't attach.

4. **MDL cost model = token/edge counts** (§2.6 explicitly sanctions this for a
   first pass): `gain = (k-1)·|body| − k·cost_instance − def_overhead`. The knobs
   this exposes — `min_cell_members`, `min_instances`, `cost_instance`,
   `def_overhead`, `gain_min`, `neighborhood_k`, `grow_radius`, `max_levels` — are
   the "non-uniqueness controls" that decide *which* compact hierarchy is chosen.

5. **Selection = greedy weighted set-packing** (§2.7), not ILP. The doc offers
   greedy as the reasonable first pass with global optimization as future work.
   Overlaps are resolved by gain-order; recursion handles proper nesting.

6. **Verification.** Every instance's membership is an exact index match on
   `(cell, orient, snapped-anchor, shape)`, and the whole result is certified by
   `flatten(H) == G` via Boost boolean area-difference (companion §6, the hard
   constraint). For axis-aligned rectangles under D4 this orientation-aware exact
   match plus global flatten-equality *is* the canonicalized-equality check — so a
   separate per-candidate Boost equality pass (§2.5) is redundant here. The one
   case it does not yet cover is **fractured polygons** (one logical shape as
   several rectangles that only match after edge-merging): there, reusing
   `verify.cpp`'s canonicalization before comparison would be needed — flagged.

7. **Non-uniqueness is visible and expected** (§1.3, §2.8). E.g. the recovered
   leaf cell is anchored on its foot rectangle with negative member offsets — a
   perfectly valid decomposition that simply differs from how the generator was
   written. We report *agreement with* ground truth, never *correctness relative
   to* it.

8. **Repeat-length drop-off (§3) — now implemented, as a selectable option.**
   `dropoff_curve()` computes R(L)/F(L) over motif size L (via rtree shingles at
   each size); the cell scale is the L at the top of the sharpest *rise* in the
   distinct-motif count R (grow the motif until its neighborhood fragments — the
   "grow, grow, grow, then suddenly the repeat stops" signal). `Selection::DropOff`
   promotes motifs at that scale instead of by MDL gain (default stays
   `MDLGain`). `dropoff_demo` prints the curve and compares the two criteria.
   Honest finding, matching the design's framing of §3 as a *scale prior* not a
   universal decider: the drop-off is a clean cell-scale signal for **isolated
   motifs** (surrounded by non-repeating content — nested→2, defective→4), but on
   a **dense lattice** sub-units repeat too, so the signal weakens; MDL + exact
   verification remain the default there. On the clean demos both criteria agree.

## Array detection = the lattice-aligned special case (demonstrated)

Both proposals frame array detection as the special case of hierarchy recovery
where a cell's instances lie on a uniform lattice. `unify_demo` / `test_unify`
prove it on our own data: on flat bauhaus, one-level hierarchy recovery recovers
the 2-rect tile at 47 instances, and `fit_lattice` on those instance anchors
reproduces the array detector's result exactly — dx=40, dy=40, 8×6 grid, 1 hole,
`flatten==G`. So **array detection ≡ hierarchy recovery (1 level) + lattice fit.**

Two honest wrinkles, both informative:

- **Non-uniqueness (§1.3), live.** A flat array admits many valid decompositions
  (one tile × N instances, or various nested groupings). Which one recovery lands
  on depends on `max_levels` and the greedy order — "which hierarchy" is a knob,
  not a fact. (Note: an earlier version of this note described greedy MDL building
  a deep *binary-doubling* hierarchy on arrays and called it "more compact." Those
  deep hierarchies were later found to be **over-covering / invalid** — see the
  correctness fixes below — and recovery now recurses only while the tiling stays
  exact, so on a dense array it stays shallow rather than doubling.)

- **Defect tolerance (now implemented).** §6 defect tolerance is ported into
  recovery: after clean cells are established, a rescan accepts PARTIAL instances
  (a strong majority of members present) and records the absent leaf members as a
  defect layer. The correctness constraint becomes `G ⊆ flatten(H)` with the
  defect geometry = `flatten(H) − G` computed by Boost (so clutter overlapping a
  missing member is handled — that overlap is exactly what broke a naive
  wholesale-rect subtraction). Missing *sub-cell references* are never tolerated,
  so nesting stays exact and clean tests are untouched. On standard_cell this
  recovers the defective tile too (matching the array detector's 25-with-1-missing);
  a residual gap on that layout is the multi-phase split (non-uniqueness), not a
  defect-tolerance failure. Cleanly demonstrated by `defective_motif_layout`: 5
  instances recovered incl. one missing a member, `G ⊆ flatten(H)`, defect area 120.

## Real-GDS hierarchy-erasure (companion §7.1) — and correctness fixes it forced

A real GDS round-trip closes the §7.1 loop with actual GDS I/O: `scripts/gds_roundtrip.py`
(gdstk) writes a genuinely hierarchical `.gds` — a leaf `gate` cell, an AREF array
`slice`, a `block` of alternately-mirrored slices (x_reflection, as std-cell rows
are placed), a `top` of blocks, plus routing clutter — then reads it back and
**flattens it with the real tool**, discarding all cell/instance/array metadata.
`build/gds_recover` recovers from the 772 flat rectangles: it reconstructs the
4-rectangle gate cell at ~191/192 instances (one edge gate falls to residual),
clutter as residual, `flatten == G` exact. Dropping in a real SKY130/ORFS `.gds`
uses the identical read → flatten → export path.

**This exposed two genuine correctness bugs** (masked until the GDS's dense mirrored
arrays hit them), now fixed:

1. **Growth over-covering.** In a perfectly periodic region, seed-and-extend would
   add a neighbor's rectangle that is "present at all instances," making adjacent
   cell instances overlap. Fixed by (a) rejecting members that geometrically
   overlap the body and (b) a per-instance *claimed-primitive* set so a member may
   only claim still-free primitives — keeping each cell a proper non-overlapping tile.
2. **Verification was too weak.** The O(n) check had been relaxed to `G ⊆ flatten`,
   which *accepts* over-covering. Replaced with an **exact per-rectangle multiset**
   check: every real rectangle must be reproduced with exactly its multiplicity
   (under-covering = missing geometry; over-covering = overlapping instances =
   invalid tiling); flatten-only rectangles are the genuine defect layer. Still O(n).
3. **Recurse-only-while-valid.** Greedy grouping on a dense periodic region can
   still produce an invalid round; recovery now validates after each round and
   reverts the round that breaks exactness. Isolated nesting recurses fully; dense
   arrays stop early (and are properly the array detector's job).

**Honest correction to earlier numbers.** The pre-fix "5.3–8.7×" hierarchy-recovery
compression figures came from *invalid, over-covering* hierarchies that the weak
check rubber-stamped. The correct, *valid* compression on the same datapaths is
**~3.3–3.8×**, with the leaf gate correctly recovered in every case. (This is the
non-uniqueness/quality frontier: valid deep compression of dense arrays needs
array-node compression, not greedy binary doubling — future work.)

## Performance (toward billions of polygons)

The recovery pipeline was profiled and made linear after a first correctness-focused
pass revealed an O(n²) blowup (a ~1000-rect design took minutes). Phase-by-phase
timing showed the **algorithm was never the bottleneck** — the per-round work
(shingle/group/build/dedup/greedy) was already linear and fast (~6 ms at n=3072).
The entire O(n²) was in the final `flatten==G` **verification**, which built one
giant multipolygon by iterated Boost `union_`.

Fixes applied:
1. **rtree neighbor queries** (`boost::geometry::index::rtree`, as the design
   mandates) for shingle k-NN and growth candidates — replaces the O(n²) per-seed
   distance scan with O(n log n).
2. **Integer-keyed spatial hash** (FNV over `(cell,orient,snapped-anchor)`) instead
   of `std::string` keys in the hot lookup path.
3. **rtree-local, size-capped growth** (`max_cell_members`) so a perfectly periodic
   region can't grow an unbounded cell body.
4. **O(n) rectangle-multiset `flatten==G` check** instead of iterated Boost union:
   recovery groups existing rectangles and reconstructs them exactly (never merges
   or refractures), so `G ⊆ flatten(H)` and the defect area are computed by hashing
   snapped rectangles. This removed the O(n²) entirely.

Result: **~3 µs/rect, linear.** 1.58 M rectangles recover in ~4.8 s single-round
(~30 s with full 8-level recursion) on one core, correctness (`G ⊆ flatten`) intact.

Path to billions (not yet built, but the architecture is ready for it):
- **Tiling + parallelism** — rounds, tiles, and candidates are independent; the
  rtree and per-tile recovery parallelize cleanly, with a stitch pass for motifs
  crossing tile borders.
- **Streaming / out-of-core** for layouts that exceed RAM.
- **Array-node compression** — represent a large lattice of K instances as one
  array node (O(1)) rather than K placements or a log-K binary-doubling tree; this
  is also what fixes the compression drop seen on very large arrays under deep
  recursion (greedy doubling leaves remainders).
- **The array detector's verifier** (`verify.cpp` / `detector.cpp`) still builds a
  layout multipolygon via the same iterated-union pattern — the same O(n) rectangle
  approach applies there and is the next perf item if arrays run at scale.

## One honest limitation found (Phase 1)

**Edge-only encoding is clutter-fragile.** Occupancy leans on gap structure to
stay robust to routing wires crossing an array (the gapped standard-cell demo has
10 crossing wires and detects perfectly). The edge encoding has no gaps to anchor
on, so crossing clutter over **zero-gap** geometry perturbs the token stream and
can push individual rows to spurious periods. The abutted demo therefore uses
*non-crossing* (border) clutter, so it isolates the point it exists to make — edge
encoding recovers directly-abutted geometry occupancy cannot — without implying an
edge-encoding clutter-robustness the design doesn't yet claim. This matches v4 §9
Q9 (occupancy-vs-edge ablation is explicitly still open) and is worth an
experiment before relying on edge-only detection over routed real layouts.
