# Geometry-First Array Detection in 2D Polygon Layouts via 1D Repetition Hypotheses

**A proposal for automatic detection of step-and-repeat cells in geometric layouts**

*v4 — revised after a third technical review, which independently converged on period-phase clustering and largely confirmed v3. New in this revision: precise open-slab sampling, coordinate snapping ahead of slab construction, primitive/supercell terminology introduced up front instead of "minimal," explicit cluster-output construction (median / circular median), and a scoped-down evaluation recommendation.*

---

## Abstract

Many 2D geometric layouts — integrated circuit masks, tiled facades, printed patterns, PCB arrays — contain regions where a small motif of polygons is repeated across a regular translational lattice, interspersed with non-repeating or irregular content. This document proposes a **geometry-first array detector** that uses fast one-dimensional maximal-repetition discovery purely as a **candidate-generation mechanism** — formalized as clustering/voting over physical period and phase — followed by geometric boundary and equality checks that certify true 2D lattice periodicity. Unlike global-periodicity methods, it detects partial rectangular step-and-repeat regions embedded in irregular layouts; unlike pairwise geometric motif-matching, it avoids quadratic search by reducing candidate discovery to line-wise runs, reserving expensive geometric verification for a small surviving candidate set.

**Framing.** 1D repetition detection generates hypotheses about 2D geometric periodicity; only geometric verification certifies them. The symbolic string encoding is lossy by construction — it is an excellent filter, not proof of periodicity.

---

## 1. Problem Statement

**Input:** A collection of polygons in the plane, each represented as an ordered sequence of edges.

**Goal:** Identify one or more rectangular regions of the layout, each defined by a **tile** and a **step vector pair**, such that translating the tile by integer combinations of the step vectors, within the bounds of the region, reproduces the polygon content of that region (exactly, or within a bounded defect tolerance — §6).

The detector seeks a **primitive** tile — the smallest verified repeating unit — but may report a **supercell** tile instead when primitive recovery is ambiguous (§2.4 formalizes this distinction precisely). "Minimal" is deliberately avoided as the defining property in this document: minimality is not well-posed once supercells, symmetric motifs, and fractured/defective geometry are all in play, and stating the goal that way invites an objection the design doesn't need to take on.

### 1.1 Assumptions (explicit)

- **Axis-aligned, rectilinear (Manhattan) layouts**, with an orthogonal two-vector lattice — not arbitrary oblique lattices.
- Single-layer geometry (multi-layer/multi-class support is future work).
- Polygons may be fractured (one logical shape represented as multiple polygon pieces) — verification must tolerate this (§2.5).
- Exact repetition is the primary target; bounded-defect matching is a defined extension (§6), not the default mode.
- Holes and overlapping polygons are out of scope for the initial version.

The layout is *not* assumed to be globally periodic. Arrays may be partial, multiple distinct arrays may coexist, and non-repeating content should be excluded from any detected region.

### 1.2 Formal Target Definition

A candidate array is a tuple **A = (B, u, v, T)**, where B is a rectangular bounding box, u = (dx, 0) and v = (0, dy) are the lattice vectors, and T is the tile geometry within one fundamental cell. For layout geometry G, the **exact array condition** is:

```
G ∩ B  =  ⋃_{i,j} (T + i·u + j·v) ∩ B      (evaluated after coordinate snapping
                                              and geometric canonicalization — §2.5)
```

This is the condition the algorithm ultimately needs to establish, and the canonicalization qualifier is not incidental — comparing raw, unsnapped, uncanonicalized geometry would reject valid arrays for purely representational reasons (see §2.5). It is important to be precise about what the symbolic stage (§2.1–§2.4) does and does not prove: it never establishes the condition above directly. It establishes only a weaker, necessary condition —

```
for many horizontal slabs y:  S_y(x) is periodic in x with period dx over B_x
for many vertical slabs x:    S_x(y) is periodic in y with period dy over B_y
```

— which is compatible with, but not sufficient for, the full 2D condition. Geometric verification (§2.5) is the step that upgrades this line-wise necessary condition into the full sufficient one.

---

## 2. Algorithm Overview

1. **Finite scanline construction** — derive a finite, well-defined set of representative row/column lines from the polygon arrangement.
2. **Symbolization and serialization** — encode each scanline as a 1D string carrying both symbolic identity and physical extent.
3. **Per-line repetition detection** — find maximal periodic intervals in each string.
4. **Period-phase reconciliation** — cluster line-wise periodic evidence by physical period and phase to form 2D rectangular hypotheses.
5. **Geometric verification** — certify each hypothesis via canonicalized geometric comparison against the source layout.

---

### 2.1 Finite Scanline Construction

Scanlines are derived from the arrangement induced by polygon vertices, not an arbitrary continuum — with two precision requirements that matter for correctness, not just style:

- **Coordinate snapping before slab construction.** Raw vertex coordinates are first snapped to the tolerance grid (ε, §5.3) and coalesced, so near-duplicate coordinates don't fragment the layout into an excessive number of near-zero-height slabs: `X = unique snapped x-coordinates`, `Y = unique snapped y-coordinates`.
- **Open-slab sampling.** Consecutive coordinates in Y define open horizontal slabs; each non-empty slab is represented by a sample line taken strictly *inside* the open interval (e.g. the midpoint) — never exactly on a slab boundary. Sampling exactly on a boundary is ambiguous, since a boundary coincides with polygon edges, not polygon interiors. For rectilinear polygons, intersection topology is constant for any horizontal line strictly inside the same open slab; horizontal edges lie *on* slab boundaries and are represented directly through the boundary coordinates themselves, not recovered by sampling a line.
- Symmetrically for vertical slabs and columns, using X.

These two rules compose usefully: snapping keeps the slab count from exploding on noisy or near-duplicate coordinates, while open-slab sampling keeps each surviving slab's topology unambiguous once it exists. Neither one alone is sufficient — snapping without open-interval sampling still leaves boundary-sampling ambiguity, and open-interval sampling without snapping still leaves the layout vulnerable to slab explosion from floating-point noise.

This gives a finite, well-defined row/column encoding set, and gives candidate tile boundaries a natural home in arrangement coordinates (used in §2.4's boundary-alignment rule).

---

### 2.2 Symbolization and Serialization

Two complementary encodings:

**Edge-boundary encoding.** Every edge is characterized by its projection vector (Δx, Δy). Edges sharing the same (Δx, Δy) within tolerance ε get the same symbol from Σ_edge. Gaps between polygons are quantized by distance into Σ_gap. Good for detecting repeated *boundary structure*.

**Occupancy-interval encoding.** For each canonical sample line, record the sequence of filled x-intervals and empty gaps: `[covered width, gap width, covered width, ...]`. Preserves more of the actual geometry than edge-vector symbols; generally the stronger encoding for verifying repeated filled/empty content.

**Physical period, not just symbolic period.** Each token carries both a symbol and a physical length, because the encoding is not automatically metric-preserving — tolerance binning can let a symbolic period survive while the underlying physical lattice drifts (e.g. `gap10 ≈ gap11` under loose ε). Every detected run therefore carries both symbolic and physical fields (§2.3), and reconciliation (§2.4) operates on the physical fields.

---

### 2.3 Per-Line Repetition Detection

Each row/column string is processed with a **runs / maximal-repetition algorithm** (e.g. Crochemore-style partitioning, or any modern linear-time runs algorithm — the proposal is not tied to one specific implementation) to enumerate maximal periodic intervals, structurally avoiding the combinatorial blow-up of naive substring enumeration.

Each detected run is recorded as:

```
run = (
  symbolic_start, symbolic_period, symbolic_length,
  physical_start_x, physical_period_dx, physical_extent
)
```

`physical_period_dx` is the run's **primitive** physical period — the shortest physical distance over which the run's content repeats — not yet reconciled against any other line. §2.4 lifts this raw record into a reconciliation record.

---

### 2.4 Period-Phase Reconciliation

**Core correction, confirmed independently across two rounds of review:** rows within a tile are *not* expected to have identical content — a row crossing the top of a rectangle can legitimately look different from a row crossing its middle. What must agree is each row's own internal periodicity, matched against other rows on **physical step and phase**, not symbol content. Reconciliation is a clustering problem over physical period, modular phase, and spatial support — not an ad hoc row-by-row alignment search.

**Reconciliation record.** Each §2.3 run is lifted into:

```
R_r = (row_id, x_start, x_end, dx, φ, support_interval, signature)
```

where `dx` carries over from `physical_period_dx`, and the run's phase is

```
φ = x_start mod dx
```

using modular distance `dist_mod(a, b; p) = min(|a - b|, p - |a - b|)`.

**Compatibility.** Two runs R_i, R_j are compatible if:

```
|dx_i - dx_j| ≤ ε_dx
dist_mod(φ_i, φ_j; dx) ≤ ε_phase
overlap([x_start_i, x_end_i], [x_start_j, x_end_j]) ≥ w_min
```

This replaces "try shifts and see what fits" with a defined equivalence relation — instead of asking *what shift aligns these two lines*, the question becomes *do these two runs belong to the same period/phase class*. The same construction applies symmetrically to vertical runs, producing vertical period-phase votes (dy, ψ).

**Two equivalent formalizations.**

- **Clustering / voting.** Runs are clustered in (dx, φ, x-support, y-support) space (horizontal) and (dy, ψ, y-support, x-support) space (vertical) — structurally a Hough-style voting scheme, where local periodic evidence votes for global lattice structure. A 2D hypothesis is formed where a horizontally-supported cluster and a vertically-supported cluster overlap spatially and induce a consistent rectangle.
- **Graph clustering.** Equivalently, each run is a node; an edge connects compatible runs, weighted by `w(i,j) = α·period_agreement + β·phase_agreement + γ·support_overlap + δ·signature_consistency`. Maximal connected components or high-weight subgraphs define candidate groups.

These are two views of the same idea, and framing it this way turns "greedy vs. global extension" from a vague open question into a concrete, benchmarkable comparison: greedy extension, connected components, weighted graph clustering, and area-maximizing selection become four directly comparable strategies rather than one implemented heuristic and three hand-waved alternatives.

**Cluster output construction.** Once a cluster is accepted, its representative hypothesis is built from robust statistics over its supporting runs, not any single run:

```
B  = candidate rectangle (spatial extent of the cluster)
dx = median(dx over supporting horizontal runs)
dy = median(dy over supporting vertical runs)
φx = circular_median(φ over supporting horizontal runs; period = dx)
φy = circular_median(ψ over supporting vertical runs; period = dy)
```

Using the median (rather than, say, an arbitrary run's value) makes the estimate robust to any single noisy run. Worth flagging before implementation: a **circular median** has no simple closed form the way a linear median does, because phase wraps modulo the period — the natural definition is the φ that minimizes `Σᵢ dist_mod(φ, φᵢ; p)` over the supporting runs, found by evaluating each observed φᵢ as a candidate minimizer (or a finer search). This is a small but real implementation task, not a one-line library call, and should be scoped as such rather than assumed free.

**A caution on acceptance criteria.** Using *average* edge weight (or average pairwise compatibility) across a component as the accept/reject threshold is risky: a long chain of just-barely-compatible runs can average out to "pass" even though individual pairs within it aren't good evidence of the same lattice — a transitive weak-link problem (A~B and B~C compatible doesn't imply A~C is a good match). A safer statistic is either (a) requiring every pairwise weight within the accepted component to clear a floor, or (b) using the weakest edge on the component's minimum spanning tree as the acceptance statistic, rather than the average.

**Primitive, supercell, or pseudo-array.** Rather than combining every row's period via LCM by default, clusters are classified:

- **Primitive array** — the cluster agrees on physical period dx (or dy), and geometric verification (§2.5) confirms content is invariant under that period itself.
- **Supercell array** — a subset of runs supports `k·dx` for a small integer k, admitted as a supercell only when k is small, repeat count remains sufficient, and verification confirms the larger tile is actually necessary (content is *not* invariant under dx alone).
- **Pseudo-array** — period doesn't relate to any candidate primitive by a small integer ratio, or verification fails at every candidate period: a coincidental match, discarded before reaching §6.

LCM is used only to combine rows already classified as evidence for a genuine supercell — never as the default combination rule for arbitrary period sets.

**Boundary alignment (exact rule).** Let `X_events` and `Y_events` be the sorted, snapped, unique polygon-vertex x- and y-coordinates from §2.1. A candidate tile's boundaries must satisfy `x0, x1 ∈ X_events` and `y0, y1 ∈ Y_events`, and the step vectors must map event coordinates back onto event coordinates (`x + dx ∈ X_events` for repeated boundary x-values, symmetrically for y). This prevents the string algorithm from choosing a tile boundary that falls mid-polygon.

**Open interaction still to resolve:** this exact snapping rule is correct for a perfect instance, but once defect tolerance (§6) is in play, a missing or extra polygon locally changes the set of event coordinates for that one instance. Applied naively, every defective instance would also fail boundary alignment for the wrong reason (its own defect) rather than the reason the check is meant to catch (structural misalignment of the lattice itself). The fix is to snap against the **expected** event coordinates — projected from the confirmed tile hypothesis — rather than the **observed** per-instance event coordinates.

**Extension strategy.** Candidate groups are extended (row by row, or via cluster/component membership above) while compatibility holds, forming the rectangle's extent in the orthogonal direction. As noted above, which extension strategy performs best is now a concrete experimental question rather than an open-ended one.

The horizontal and vertical passes are intersected here to produce a small set of 2D rectangular hypotheses, each carrying a hypothesized tile, physical dimensions, step vectors, and a primitive/supercell/pseudo-array classification.

---

### 2.5 Geometric Verification

Verification target is **geometric equality after canonicalization**, not raw polygon-list identity — the same logical geometry can be represented by different polygon decompositions (fractured vs. merged), and naive object-level comparison would reject valid arrays for representational reasons alone.

1. Clip original geometry to the candidate rectangle.
2. Clip the hypothesized tile region.
3. Translate tile copies across the step vectors to reconstruct the full rectangle.
4. **Canonicalize** both the reconstruction and the original clip: snap coordinates to a tolerance grid; normalize polygon winding/orientation; merge coincident edges / dissolve unnecessary boundaries; sort rings/segments into canonical order.
5. Compare canonicalized reconstruction against canonicalized original. Only matching hypotheses are accepted as confirmed array regions.

This is the most computationally expensive stage and is deliberately placed last, after cheaper symbolic and reconciliation stages have pruned the hypothesis set.

---

## 3. Complexity Summary

| Stage | Cost |
|---|---|
| Scanline construction (incl. snapping) | O(V log V) in polygon vertices V |
| Symbolization / serialization | O(E) in total edges E |
| Per-line repetition (runs algorithm) | O(n log n) or better, per line of length n |
| Period-phase reconciliation | Dominated by pairwise compatibility checks across candidate runs (graph clustering) or vote-space clustering cost (voting formulation) |
| Verification | O(tile size × instance count) per surviving hypothesis |

---

## 4. Candidate Scoring

Before verification, hypotheses should be ranked rather than accepted in discovery order:

```
score(C) = covered_area(C) × repeat_count(C) × period_consistency(C)
           × phase_consistency(C) × boundary_alignment(C) × support_density(C)

period_consistency = exp(-variance(dx_rows) / σ_dx²)
phase_consistency  = exp(-circular_variance(φ_rows) / σ_φ²)
boundary_alignment = 1 if boundaries lie on arrangement coordinates, else penalty
support_density    = supporting_slabs / total_slabs_inside_candidate
```

**A concern worth flagging before implementing this as written:** `covered_area` and `repeat_count` are unnormalized and can range into the thousands, while the other four factors are bounded in [0,1]. In a straight product, the ranking will be dominated almost entirely by `covered_area × repeat_count` — the difference between a period_consistency of 0.9 and 0.99 barely moves a product that also contains a term in the thousands, so the consistency terms become close to decorative rather than load-bearing. Two fixes: (a) normalize `covered_area` and `repeat_count` onto comparable [0,1] scales before multiplying, or (b) move to a log-domain weighted sum, `log(score) = Σ wᵢ · log(factorᵢ)`, which avoids the "one bad factor" over-sensitivity of a raw product, lets weights be tuned independently, and reuses the same weight vocabulary (α, β, γ, δ) as the graph edge-weight formula in §2.4 rather than introducing a second, inconsistent scheme.

---

## 5. Known Design Constraints

1. **Edge-level symbol granularity.** Symbols represent individual edges, not whole polygon boundaries, so unrelated edge collections can coincidentally produce identical symbolic sequences. This is why verification (§2.5) is mandatory, not optional — the symbolic stage is a filter, never proof.
2. **Boundary/defect interaction.** See the open item at the end of §2.4 — exact event-coordinate snapping and defect tolerance (§6) need to be reconciled so they don't fight each other.
3. **Tolerance handling.** ε (coordinate snapping, §2.1), ε_dx, ε_phase (period/phase matching, §2.4), and the canonicalization tolerance used during verification (§2.5) all need empirical characterization against real layout noise; not yet formalized.
4. **Circular median computation.** Needs a defined procedure (§2.4), not an assumed library primitive.

---

## 6. Defect-Tolerant Extension

Moving from **exact array detection** to **array-with-defects detection** is likely the most practically valuable extension. It applies to confirmed primitive or supercell arrays (§2.4) — pseudo-arrays are discarded before this stage is reached.

For each expected tile instance within a confirmed candidate rectangle, compute:

- `missing geometry = expected − observed`
- `extra geometry = observed − expected`
- `perturbed geometry` = present but shifted/resized beyond tolerance

Report structure:

```
array region:
  step = (dx, dy)
  tile bbox = ...
  classification = primitive | supercell(k)
  instances = m × n
  clean instances = ...
  defective instances = ...
  defect map = [(i, j, type, area)]
```

This turns the algorithm from a binary recognizer into a layout-analysis tool that can flag defects within an otherwise-regular array.

---

## 7. Evaluation Plan

Nothing below has been executed yet — this is a proposed plan, not reported results.

### 7.1 Benchmark Construction: Hierarchy-Erasure Recovery

The cleanest way to get ground truth for free: start from a layout with **known** hierarchical/repeated structure, flatten it to raw polygons (discarding hierarchy, cell-instance names, and array metadata), and ask the detector to recover the array regions purely from geometry.

1. Start from hierarchical layout data with known array instances (synthetic, or real hierarchical GDS/OASIS).
2. Flatten to raw polygons.
3. Strip all cell names, instance names, hierarchy, and array-reference metadata.
4. Run the detector.
5. Compare detected regions against the original (now-hidden) instance placement.

This directly tests the algorithm's actual claim — recovering array structure from geometry alone — and the ground truth comes from the layout's own hierarchy, so nothing needs to be hand-labeled.

### 7.2 Stress Test Suite

A synthetic generator (place a motif M as an m×n array, add clutter, then flatten) should cover at minimum:

| Case | Description |
|---|---|
| 1 | Perfect array |
| 2 | Array with missing instances |
| 3 | Array with extra local polygons |
| 4 | Same period, different motif nearby (period/content decoupling) |
| 5 | Two adjacent arrays, same dx, different dy |
| 6 | Nested array / genuine supercell structure |
| 7 | Repeated cells mixed with irregular routing/connective geometry |
| 8 | Almost-periodic but not actually periodic (false-positive stress test) |

### 7.3 Candidate Data Sources — and a Scoping Recommendation

Verified candidate sources:

- **SkyWater SKY130 PDK** — an open-source 130nm process design kit, providing several foundry-supplied digital standard-cell libraries spanning different cell heights and speed/leakage tradeoffs, plus IO cell libraries. Standard-cell rows and filler-cell insertion naturally produce strong repeated structure embedded in irregular routing.
- **OpenROAD-flow-scripts (ORFS)** — a fully open-source, autonomous RTL-to-GDS flow (synthesis through GDSII generation, no human in the loop), usable with SKY130 to generate complete flattened layouts of varying size.
- **FreePDK45 / Nangate-style open standard-cell libraries** — an older but widely recognized academic benchmark; useful as a classic baseline rather than the primary real-world claim.
- **ALIGN** — an open-source flow translating SPICE netlists into GDSII layouts for analog circuits via automatic hierarchy detection. Analog layouts (repeated transistor fingers, matched-device arrays, capacitor arrays, guard rings) are a harder, more interesting test case than digital standard-cell rows.
- **Open-hardware Gerber layouts** — usable if polygon extraction tooling is available for the specific project; treat as project-dependent rather than a guaranteed source.
- **PCB image datasets (e.g. DeepPCB) — secondary only, with a caveat.** These are raster image pairs with bounding-box defect annotations, not native vector/Gerber polygon geometry, so they don't fit a geometry-first method directly. Useful only as a rasterized-comparison baseline, not a primary test case.

**Scoping recommendation:** the algorithm doesn't need all of the above to make its case. Three is enough: (1) the synthetic stress suite (§7.2) for controlled accuracy, (2) hierarchy-erasure recovery on flattened real or generated GDS (§7.1) for realistic ground truth, and (3) one real open-source layout crop (SKY130/ORFS) for visual, qualitative credibility. Trying to cover every listed source risks spreading evaluation effort thin instead of making any one result solid.

### 7.4 Metrics

- Region IoU against labeled ground-truth array rectangles
- Tile width/height error, step-vector error
- Precision/recall of detected array regions
- False-candidate count *before* geometric verification (measures how well the symbolic + reconciliation stages prune)
- Runtime by stage, memory usage
- Defect detection precision/recall (once §6 is implemented)
- **Percentage of total layout area explained by detected arrays** — e.g. "63% of polygon area explained by 12 arrays; 4.2% flagged defective; 32.8% left as non-array geometry."

### 7.5 Reporting: The Candidate-Reduction Funnel

```
raw possible boxes:       (combinatorially large / infeasible to enumerate directly)
symbolic runs found:      N
period-phase clusters:    M
verified arrays:          K
```

`N ≫ M ≫ K` is the strongest evidence that each stage is doing real pruning work before the expensive verification step is reached.

---

## 8. Suggested Figures (for eventual write-up)

1. **Synthetic controlled array** — simple motif array with added clutter; demonstrate clean recovery against known ground truth.
2. **Real flattened-layout crop** (e.g. SKY130/ORFS) — detected standard-cell/filler/repeated regions overlaid on a real, irregular layout.
3. **Defect-tolerant array** — same array with one missing tile and one extra polygon, reporting e.g. "94/96 clean instances, 1 missing, 1 extra."

---

## 9. Status and Open Questions

An initial implementation of the core pipeline (§2, pre-period-phase-reconciliation design) has been built and run against real layout data, with results indicating the method identifies repeating array regions efficiently at the scale tested. It has not yet been benchmarked against labeled ground truth using the plan in §7.

Open questions carried into the next iteration:

1. Graph clustering vs. lattice voting (§2.4) — which formulation is cheaper/more robust at production scale is untested; now a concretely benchmarkable comparison rather than a vague question.
2. Acceptance statistic for candidate components — minimum pairwise weight vs. MST-weakest-edge (§2.4) needs to be chosen and validated, not just average edge weight.
3. Circular median computation method (§2.4) needs to be selected and implemented — not assumed to exist as a library primitive.
4. Boundary-snapping vs. defect-tolerance interaction (§2.4, §6) needs to be implemented against *expected* rather than *observed* event coordinates.
5. Scoring formula normalization (§4) — normalized product vs. log-domain weighted sum needs to be decided before the scoring stage is implemented.
6. Whether coordinate snapping + open-slab sampling (§2.1) behave well together on real noisy production coordinates, or need further tuning, is untested outside of the reasoning given here.
7. Multi-layer / multi-class geometry support.
8. Formal ε, ε_dx, ε_phase characterization against real layout noise.
9. Whether occupancy-interval encoding alone suffices, or edge-boundary encoding remains necessary as a complementary signal (§2.2) — worth an ablation once both are implemented.

---

## 10. Contribution Statement

We propose a geometry-first array detector that uses string maximal-repetition algorithms only as a scalable candidate-generation mechanism, formalized as clustering (or equivalently, Hough-style voting) over physical period and phase. Unlike global periodicity methods, the algorithm detects partial rectangular step-and-repeat regions embedded in irregular layouts. Unlike purely geometric pairwise-matching approaches, it avoids quadratic motif search by reducing candidate discovery to line-wise runs, then performing exact geometric verification — geometric equality after canonicalization — only on a small surviving candidate set.

---

## Data Source References

- SkyWater SKY130 PDK documentation: https://skywater-pdk.readthedocs.io/en/main/contents.html
- OpenROAD-flow-scripts documentation: https://openroad-flow-scripts.readthedocs.io/en/latest/
- OpenROAD Project (background/history): https://en.wikipedia.org/wiki/OpenROAD_Project
- ALIGN (analog layout automation): https://align-analoglayout.github.io/ALIGN-public/
- DeepPCB dataset (secondary/raster baseline only): https://github.com/tangsanli5201/DeepPCB

---

*Prepared as a design proposal for internal review and discussion. v4 revisions: precise open-slab sampling and coordinate snapping ahead of slab construction (§2.1), primitive/supercell terminology introduced in the problem statement instead of "minimal" (§1), canonicalization made explicit in the formal target condition (§1.2), reconciliation records and cluster-output construction via median/circular-median made explicit (§2.4), circular-median computation flagged as a real implementation task, hierarchy-erasure recovery named as the central benchmark with a 3-source scoping recommendation (§7).*
