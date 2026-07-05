# Mixed Array-Hierarchy Extraction Plan

> **Implementation status.** This is the ORIGINAL design plan; the sections below
> still read as forward-looking ("future work"), but a first pass is now
> implemented in `cpp/{include/adt/nested.hpp, src/nested.cpp}` (driver
> `recover_nested`, demo `nested_demo`, tests `test_nested`). Status against the
> §"Implementation Order":
>
> - **Done:** (1) array expansion in `flatten` (nested array items expand exactly);
>   (2) array nodes promoted to first-class *nested cells* that participate in
>   later passes; (3) structural signatures / canonicalization (`body_signature`
>   + `intern_cell` dedup); (4) repeated-run grouping (`promote_axis`); (5) exact
>   verification after each pass with rollback (against `flatten(base)`); (6)
>   source-aware `gate/slice/block/top` agreement metrics (in `test_nested`);
>   (7) JSON output (`nested_to_json`, with real residual/defect geometry).
> - **Partial / simplified:** nested cells are placed at orientation 0, with D4
>   mirroring baked into distinct cell bodies rather than composed orientations;
>   mirrored stacks currently yield interleaved per-orientation blocks rather than
>   a single period-2 block cell.
> - **Not yet:** (8) reconstructed hierarchical-GDS re-emission + external
>   `flatten(orig)==flatten(recovered)`; real ORFS/SKY130 source-aware evaluation.
>
> Treat the "future work" phrasing in §1 and §8 below as the original framing, not
> the current state.

## Goal

Recover source-like hierarchy from flattened geometry by making array detection
and hierarchy recovery feed each other.

Target progression:

```text
flat rectangles
-> leaf cells
-> array nodes over leaf-cell placements
-> higher-level cells containing array nodes
-> nested hierarchy
```

For the synthetic GDS benchmark, the target is:

```text
gate -> slice -> block -> top
```

rather than only:

```text
gate + top-level row arrays
```

## Core Idea

Array detection and hierarchy recovery should not be separate endpoints. They
should be a mixed recursive process:

1. Recover repeated leaf cells.
2. Fit array nodes over placements of those cells.
3. Treat array nodes as first-class primitives.
4. Recover repeated groups of leaf refs, cell refs, array refs, and residual.
5. Promote those groups into higher-level cells.
6. Verify every promotion by exact flatten equality.

The research claim after this becomes:

> Array detection and hierarchy recovery are mutually recursive: recovered cells
> create array candidates, recovered arrays create higher-level hierarchy
> candidates, and exact flatten equality certifies every promotion.

## 1. Make Array Nodes First-Class

Current state: array nodes are a reporting/compression view over `top`
placements.

Change:

- Add array nodes as real recoverable primitives.
- Represent each node with:
  - referenced `cell`
  - D4 `orient`
  - origin
  - `dx`, `dy`
  - row/column counts
  - occupied/missing pattern
- Keep exact flatten expansion for each array node.

The ordinary placement-list representation should remain available for debugging,
but array nodes must participate in later recovery rounds.

## 2. Canonicalize Array Nodes

Create a signature for comparing array nodes independent of absolute position.

Candidate signature:

```text
array_sig = (
  referenced_cell_signature,
  orientation policy,
  dx, dy,
  ncols, nrows,
  occupancy/missing mask,
  normalized bbox
)
```

This lets two equivalent rows or slices match even if they are placed at
different coordinates.

Open choices:

- Whether mirrored rows should canonicalize to the same array signature or remain
  orientation-distinct until a higher-level D4 match.
- Whether sparse occupancy should be encoded as a bitmap, missing-site list, or
  run-length structure.
- How much pitch tolerance to allow before verification.

## 3. Promote Repeated Array Nodes Into Cells

After leaf recovery finds many row arrays, search for repeated groups of array
nodes.

Example:

```text
row array of gates = slice
repeated stack of slices = block
repeated blocks = top
```

Algorithm:

1. Treat each array node as a primitive.
2. Run geometric shingle / seed-and-grow over array-node primitives.
3. Grow candidate cells only if offsets and array signatures match across
   occurrences.
4. Score candidates with MDL.
5. Verify the chosen promotion by exact flatten equality.

This should recover intermediate cells such as `slice` and `block` instead of
leaving all array nodes at top level.

## 4. Recurse On Mixed Primitives

The working representation should support:

```text
leaf rectangles
cell refs
array refs
residual geometry
defect geometry
```

Each round can promote repeated structures made from any combination of these.

Examples:

- A cell body can contain raw rectangles.
- A cell body can contain references to lower-level cells.
- A cell body can contain array references.
- A higher-level cell can contain a mix of arrays, single refs, and residual
  local geometry.

## 5. Add Exact Verification For Mixed Hierarchy

Every candidate promotion must satisfy:

```text
flatten(H) - defect_layer == original_flat_geometry
```

Implementation requirement:

- Extend `flatten(H)` so array refs expand exactly.
- Keep the rectangle-multiset check as the global correctness gate.
- Reject any promotion round that under-covers or over-covers real geometry.
- Treat defect geometry explicitly, not as silent mismatch.

This preserves the lesson from the over-covering bug: containment is not enough.

## 6. Improve The Cost Model

Current cost is simple:

```text
body members + placements + residual
```

Mixed hierarchy needs a cost model that distinguishes:

- leaf rectangle cost
- cell definition cost
- single instance cost
- array instance cost
- missing-site cost
- residual cost
- defect cost

Array refs should be much cheaper than hundreds of placements, but sparse arrays
should not be free.

Suggested first-pass cost:

```text
cost(H) =
  sum(cell_body_members)
  + n_single_instances * cost_instance
  + n_array_nodes * cost_array
  + n_missing_sites * cost_missing_site
  + n_residual_rects * cost_residual
  + n_defects * cost_defect
```

Keep all weights configurable.

## 7. Add Hierarchy Agreement Metrics

Compression alone is not enough. Add benchmark metrics that compare recovered
structure against known source hierarchy where available.

Metrics:

- source cell count vs recovered cell count
- source hierarchy depth vs recovered depth
- source leaf cell recovered?
- source array refs recovered?
- source intermediate cells recovered?
- exact flatten equality?
- residual count
- defect count
- placement-list compression
- array-node compression
- nested-hierarchy compression

For synthetic GDS:

```text
gate recovered: yes/no
slice recovered: yes/no
block recovered: yes/no
top-level array recovered: yes/no
```

Report agreement, not accuracy, because valid decompositions are non-unique.

## 8. Add JSON Output

Make the tool functional, not just demo-oriented.

Output schema should include:

```json
{
  "cells": [],
  "instances": [],
  "arrays": [],
  "residual": [],
  "defects": [],
  "metrics": {}
}
```

This enables:

- review without reading stdout
- visualization
- downstream conversion
- regression tests over structure
- comparison against source hierarchy metadata

## 9. Add Hierarchical GDS Re-Emission

After JSON output works, emit reconstructed hierarchical GDS:

```text
recovered.gds
```

Then verify externally:

```text
flatten(original.gds) == flatten(recovered.gds)
```

This would be a strong artifact result because it proves the recovered hierarchy
is usable by normal layout tooling.

## 10. Test Progression

Use datasets in this order:

1. Synthetic `gate -> slice -> block -> top`.
2. Same with mirrored rows.
3. Same with missing cell/member defects.
4. Same with routing clutter.
5. Same with fractured rectangles.
6. ORFS/SKY130 small real design crop.
7. Larger real open design.

Each stage should preserve exact flatten equality before moving to the next.

## Implementation Order

1. Add array-node expansion to `flatten(H)` as real hierarchy content.
2. Promote the current array-node view into actual primitives.
3. Add mixed primitive signatures.
4. Implement repeated-array-node grouping.
5. Add exact verification after each promotion round.
6. Add metrics for `gate`, `slice`, and `block` agreement.
7. Add JSON output.
8. Add reconstructed GDS output.

## Risks And Open Questions

- **Non-uniqueness:** A flat layout may admit several valid nested hierarchies.
  The system must report agreement and compression, not claim designer intent.
- **Sparse arrays:** Missing-site encoding needs a principled cost so sparse
  grids do not look artificially cheap.
- **Mirroring:** Alternating standard-cell rows may need D4-aware array
  canonicalization and parent-cell orientation composition.
- **Fracturing:** Different rectangle decompositions of the same logical polygon
  still require canonical geometry equality in the recovery path.
- **Selection:** Greedy set-packing may fail once array nodes and cell refs mix.
  ILP or local-search selection may become worthwhile.
- **Scale:** Tiling/stitching must preserve arrays and cells crossing tile
  boundaries.

## Expected Outcome

The current system can recover:

```text
gate + top-level row arrays
```

The mixed extractor should recover:

```text
gate -> slice -> block -> top
```

with exact flatten equality and improved compression. This is the step that turns
array-node compression into true hierarchy extraction.
