# Hierarchy Recovery from Flattened Polygon Layouts via Repetition-Driven Compression

**A companion proposal: generalizing array detection to arbitrary repeated-cell discovery**

*v1 — initial formalization*

---

## Relationship to the Array Detection Proposal

This is a distinct, more general proposal, not a revision of the array-detection design (companion document, currently at v4). The relationship between them:

```
hierarchy recovery
  ├── arbitrary repeated cell instances, anywhere in the layout, any placement
  └── array recovery = special case where instances lie on a regular 2D lattice
```

Array detection asks: *is there a rectangular region where one tile repeats on a regular grid?* Hierarchy recovery asks the broader question: *does any geometry repeat anywhere in the layout, on any placement, regular or not?* Every array the first proposal can find is also a valid answer to this broader question — a cell whose instances happen to be lattice-aligned. The reverse isn't true: a hierarchy recovery result generally isn't an array.

**Reusable directly from the array-detection design:** coordinate snapping and arrangement/slab construction, the canonicalized-geometric-equality procedure used for verification, the edge-boundary token encoding, and the overall philosophy that symbolic/string methods generate hypotheses while geometric checks certify them. None of this needs to be reinvented here.

**Sequencing note.** This proposal has substantially more open design surface than the array-detection proposal, which has already converged through three rounds of review to a scoped design with a concrete evaluation plan. Worth considering: get the array detector to a working, benchmarked state first — it's the narrower, closer-to-shippable result — and treat hierarchy recovery as the natural next phase that reuses its canonicalization and verification machinery, rather than developing both simultaneously from a standing start.

---

## Abstract

Given a flat polygon layout, this proposal targets inferring a compact hierarchical representation — a small library of reusable cell definitions plus an instance-placement graph — such that flattening the inferred hierarchy exactly reproduces the original geometry. The approach treats **multi-serialization repeated-substring mining** and **local geometric-shingle hashing** as complementary, non-authoritative candidate generators; reuses the array-detection proposal's **canonicalized geometric equality** check as the certifier; and uses **minimum description length (MDL)** — the representation-size reduction a candidate cell would actually provide — as the objective deciding which repeated substructures get promoted to cells. Array detection falls out as the special case where a discovered cell's instances happen to lie on a uniform lattice.

---

## 1. Problem Statement

### 1.1 Formal Definition

```
Input:  flat polygon geometry G

Output: a hierarchy H = (cell definitions C1..Cm, instance placements I1..Ik, residual geometry R)

such that flatten(H) = G within tolerance ε

minimizing:
  cost(H) = Σ cost(cell_body(Ci)) + Σ cost(instance(Ij)) + cost(residual_geometry(R))
```

The candidate-generation methods in §2 are heuristics for this optimization, exactly as in the array-detection proposal — they never certify a hierarchy on their own. Only `flatten(H) = G`, checked via the same canonicalized-equality procedure used for array verification, does that.

### 1.2 Assumptions and Scope

- Same base assumptions as the array proposal: rectilinear geometry, single layer, tolerance-bounded exact matching as the primary target.
- **Transform scope — translation only, by default, and this needs to be a deliberate decision, not an oversight.** Candidate-cell instances are considered equivalent under translation alone unless stated otherwise. This matters concretely: automated place-and-route flows very commonly mirror alternating standard-cell rows so adjacent rows share a power rail — standard practice, and directly relevant to the SKY130/ORFS evaluation data already selected for the array-detection proposal. Under translation-only canonicalization, a mirrored instance of a cell will *not* be recognized as the same cell — it'll register as unrelated geometry or spawn a separate, spurious candidate. If mirrored/rotated instances matter for the target layouts (likely, given the chosen evaluation data), canonicalization (§2.1) needs to search over the relevant symmetry group — translation plus reflection, at minimum — and instance records need an orientation field, not just a position. Flagging this now as a scope decision to make on purpose, rather than a recall gap to discover mid-evaluation.

### 1.3 Non-Uniqueness of Hierarchy

The recovered hierarchy is not, and should not be presented as, "the" original hierarchy. The string `ABABABAB` flattens identically whether the source hierarchy was one cell `AB` repeated 4 times, alternating separate cells `A`/`B`, or one cell `ABAB` repeated twice — all are equally valid decompositions of the same flat geometry, among others. The target is therefore:

> infer a compact, geometrically valid hierarchy

not

> recover the original hierarchy

If evaluation compares against a known source hierarchy (§6), report that comparison as *agreement with*, not *correctness relative to* — a well-formed inferred hierarchy can legitimately disagree with a designer's chosen decomposition while flattening to exactly the same geometry.

---

## 2. Algorithm Overview

1. Canonicalize and tokenize the flat geometry.
2. Generate candidate cell bodies via two complementary channels: multi-serialization repeated-substring mining, and geometric shingles / local hashing.
3. Grow small seed candidates into the maximal common geometric neighborhood shared by their instances.
4. Geometrically verify each candidate (same canonicalized-equality check as array verification).
5. Score surviving candidates by compression gain (MDL).
6. Select a non-conflicting subset maximizing total gain.
7. Replace selected instances with cell references; recurse on the compressed representation to discover nested hierarchy.
8. Output cell definitions, instance placements, and residual geometry.

---

### 2.1 Canonicalization and Tokenization

Flat geometry is first canonicalized: coordinates snapped to the tolerance grid (reusing the array-detection proposal's construction directly), polygons normalized, and a spatial index built for the local-neighborhood queries used in §2.3.

Geometry is then tokenized into a *relative*, translation-invariant stream rather than absolute coordinates — this is what lets the same cell, instantiated at two different positions, produce the same token sequence:

```
POLY_BEGIN
MOVE dx dy
EDGE +x 10
EDGE +y 5
EDGE -x 10
EDGE -y 5
POLY_END
GAP dx dy
POLY_BEGIN
...
```

Each `MOVE`/`GAP` records a displacement relative to the previous primitive; each `EDGE` records direction and length — the same edge-boundary encoding as the array-detection proposal (§2.2 there), deliberately reused rather than reinvented.

One caveat worth stating plainly: relative displacement between primitives is itself a function of serialization order, so the same physical cell can produce different token streams under different traversal orders. This is exactly why candidate generation (§2.2–§2.3) doesn't rely on a single serialization.

---

### 2.2 Candidate Generation, Channel A: Multi-Serialization Repeated-Substring Mining

Serialize the tokenized layout under several different traversal orders — x-major, y-major, Morton/Z-order, Hilbert-curve order, and per-connected-component order are all reasonable choices — and mine each resulting string for long repeated substrings (occurrence count ≥ 2, length ≥ some `L_min`). A motif appearing as a repeated substring under more than one serialization is a stronger candidate than one found under only one.

**Tool correction relative to the array-detection proposal.** Crochemore-style *runs* algorithms find periodic, contiguous repetition (`ABCABCABC`) — the right tool when instances are adjacent on a lattice, which is why runs are the right fit for array detection. Cell instances here are not generally adjacent (`ABC … ABC … ABC`, with unrelated content between occurrences) — that's a **repeated substring**, not a periodic run, and calls for a different tool family: suffix arrays/trees for maximal repeated substrings, or grammar-induction methods (Sequitur, Re-Pair) that build a compact grammar directly from repeated structure and are natively aligned with the MDL objective in §1.1, rather than needing a separate scoring pass bolted on afterward.

---

### 2.3 Candidate Generation, Channel B: Geometric Shingles / Local Hashing

Independent of any global serialization, generate local geometric "shingles" directly from the spatial index: small groups of nearby polygons, connected components, or fixed-radius neighborhoods around each polygon. Canonicalize each shingle (§2.1's procedure) and hash it. Shingles that hash identically (or within tolerance) across multiple locations are candidate motifs — found without ever depending on a linear traversal order.

**Why this channel is the more structurally sound of the two, not just a supplement.** Channel A's recall is fundamentally serialization-order-dependent: a repeated motif is only findable if it happens to be contiguous under one of the tried orders. Multiple serializations catch more cases than one, but this is a finite patch on an open-ended problem — there's no guarantee any fixed, small set of global orderings makes every true repeated motif contiguous, since clutter arranged unluckily around a specific instance can defeat all of them at once for that instance. Channel B doesn't have this limitation: it never depends on a global order, only on local spatial neighborhoods, so its recall doesn't degrade based on what's happening elsewhere in the layout. In practice, Channel A should be treated as a cheap, useful, but structurally incomplete source of candidates, and Channel B as the primary, more reliable mechanism — not the other way around.

---

### 2.4 Motif Growth (Seed-and-Extend)

Small repeated seeds (from either channel) are grown geometrically: for each occurrence of a seed, attempt to add neighboring polygons present, in a geometrically consistent position, around *every* occurrence simultaneously. Growth stops when no further addition holds across all instances. This mirrors seed-and-extend strategies from sequence alignment — start from a small, reliable match, then greedily extend as far as the evidence supports — and matters because real cells are often not discoverable as one complete long substring on the first pass; the reliable signal is usually a smaller core that needs to be grown into the full cell body.

---

### 2.5 Geometric Verification

Reuses the array-detection proposal's canonicalized-equality procedure directly: snap, normalize orientation (extended to the full relevant symmetry group per §1.2's scope decision), merge coincident edges, sort into canonical order, compare. A candidate is a true repeat only if every instance is geometrically equal to the canonical body after this procedure — the string/shingle match is a hypothesis, not a proof, exactly as in the array proposal.

---

### 2.6 Compression-Gain Scoring (MDL)

For a candidate cell body M occurring k times:

```
gain(M) = (k - 1) × cost_flat_body(M) - cost_define_cell(M) - k × cost_instance
```

(A precision fix relative to the informal version of this formula circulated in review: the instance-reference term has to scale with k — `k × cost_instance` — since every one of the k occurrences needs its own reference; a single flat `instance_overhead` term understates the cost for any k > 1. For a first-pass approximation, token or edge counts are a reasonable stand-in for both `cost_flat_body` and `cost_define_cell`.)

Only candidates with `gain(M) > 0` are worth promoting to cells at all; §2.7 chooses a non-conflicting subset of positive-gain candidates maximizing total gain.

---

### 2.7 Candidate Selection

Overlapping/nested candidates are expected — `ABCD`, `BCD`, `ABC`, and `ABCDABCD` can all be simultaneously valid repeated substrings. Selection is a weighted set-packing problem: choose candidates maximizing `Σ gain(Cᵢ)` subject to `cover(Cᵢ) ∩ cover(Cⱼ)` being empty or properly nested for every selected pair.

Worth being precise that this is *not* the same as classical weighted interval scheduling, even though it resembles it at a glance: interval scheduling has an efficient exact DP solution because its "cover" sets are 1D intervals with a total order. Here `cover(C)` is an arbitrary set of 2D primitives with no natural total order, so that DP doesn't transfer. The honest options are a real (NP-hard in general) weighted set-packing formulation solved via ILP for small/medium candidate counts, or a greedy "sort by gain, take the best, remove conflicts, repeat" heuristic as a practical first pass — with no general optimality guarantee, but a reasonable starting point, with global optimization left as explicit future work.

---

### 2.8 Recursive Hierarchy Construction

After replacing the selected instances of round-1 candidates with cell references, the compressed representation (cell references + residual geometry) is re-tokenized and passed back through §2.2–§2.7, discovering cells built from smaller cells. This nesting matters: real layout hierarchies are nested by construction — a datapath built from bit-slices built from gates, for instance — not flat collections of same-level cells, so a single non-recursive pass will under-recover structure that only becomes visible once lower-level repetition has already been factored out.

Worth flagging explicitly, tying back to §1.3: because both the selection step (greedy, order-dependent) and this recursive step operate on whatever partial compression the previous level produced, the final output hierarchy is itself just one member of a family of valid hierarchies, not uniquely determined by the input geometry — a different tie-break in greedy selection at level 1 can lead to a different, equally valid, hierarchy at level 2 and beyond. This isn't a bug to fix; it's the non-uniqueness from §1.3 compounding across recursion levels, and should be documented as expected behavior rather than presented as if the algorithm converges to one canonical answer.

---

## 3. The Repeat-Length "Drop-off" as a Scale Prior

For repeated substrings/shingles, define, over candidate length L:

```
R(L) = number of distinct repeated substrings of length ≥ L
F(L) = total occurrence count summed over those substrings
C(L) = total compression gain (§2.6) summed over those substrings
```

The expected shape — many short repeats, progressively fewer at longer lengths, a sharp drop past the layout's real "cell scale" — is a real and useful empirical tendency in structured, repetitive data, and the elbow in these curves is a reasonable way to pick a starting `L_min` or get a first sense of a layout's natural motif scale. But it's a diagnostic, not a decision procedure: which candidates actually become cells should be driven by geometric verification (§2.5) and compression gain (§2.6), not by position on this curve alone. The decision rule in one sentence: *does replacing this repeated geometry with a cell reduce total description length while preserving exact flattened geometry* — the drop-off curve narrows the search, it doesn't answer the question.

---

## 4. Complexity Summary (rough, first-pass)

| Stage | Cost |
|---|---|
| Canonicalization / tokenization | O(E) in total edges E |
| Multi-serialization mining (Channel A) | O(s · n log n) for s serializations of length n |
| Geometric shingle hashing (Channel B) | O(E · w) for shingle window size w, via the spatial index |
| Motif growth | O(candidates × avg. instance count × avg. growth steps) |
| Geometric verification | O(tile size × instance count) per candidate |
| Compression scoring | O(candidates), given verified candidates |
| Selection (greedy) | O(candidates log candidates); ILP formulation is problem-size-dependent, not polynomial in general |
| Recursion | Per-level cost as above, repeated over levels; representation shrinks each level |

---

## 5. Known Design Constraints and Open Questions

1. **Channel A / Channel B reliance balance** (§2.3) — untested how much Channel A actually contributes once Channel B is implemented; possibly droppable or reducible after an ablation.
2. **Rotation/reflection scope** (§1.2) — needs to be decided before implementation, not discovered as a recall gap while evaluating on mirrored standard-cell rows.
3. **Selection optimality** (§2.7) — greedy vs. ILP tradeoff untested at realistic candidate counts.
4. **Cost model** (§2.6) is first-pass token/edge counting; doesn't yet distinguish geometrically simple vs. complex bodies of equal token count, which may not be equally "worth" promoting to a cell.
5. **Hierarchy non-determinism across recursion levels** (§2.8) — worth deciding whether this is acceptable (and documented) or whether a canonicalization/tie-break rule should be imposed so two runs on the same input produce the same output.

---

## 6. Evaluation Plan

Same hierarchy-erasure benchmark story as the array-detection proposal, extended for this more general setting:

```
original hierarchical GDS/OASIS
  → flatten
flat polygons
  → this algorithm
inferred hierarchy H
  → flatten(H)
must equal original flat polygons, within tolerance ε
```

Metrics:

- **Flattened-equality** — `flatten(H) = G` must hold; this is a hard correctness constraint, not an approximate score.
- Compression ratio (flat size vs. `cost(H)`).
- Cell count, instance count.
- Agreement with original hierarchy where available — reported as *agreement*, not *accuracy*, per §1.3.
- Runtime by stage.

The same three-source scoping recommendation from the array-detection proposal applies here too: a synthetic stress suite, hierarchy-erasure recovery on flattened real/generated layouts, and one real crop for qualitative credibility, rather than spreading effort across every possible dataset.

---

## 7. Contribution Statement

We propose a hierarchy-recovery method that generalizes lattice-based array detection to arbitrary repeated-cell discovery, using multi-serialization repeated-substring mining and geometric-shingle local hashing as complementary, non-authoritative candidate generators, geometric canonicalized-equality checking — shared with the array-detection design — as the certifier, and minimum-description-length compression gain as the objective deciding which repeated substructures are promoted to cells. Because a flat layout generally admits multiple valid hierarchical decompositions, the method targets a compact, geometrically valid hierarchy rather than claiming to recover a designer's original one, and reports agreement with known source hierarchies, where available, accordingly.

---

*Prepared as a design proposal for internal review and discussion, companion to the array-detection proposal (v4). Evaluation data sources are shared with that proposal — see its Data Source References section.*
