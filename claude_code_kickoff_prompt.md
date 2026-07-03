# Claude Code kickoff prompt

Copy the block below as your first message to Claude Code, after setting up the
project folder (instructions above the line).

---

## Before you start Claude Code

1. Create a project folder, e.g. `repeat-cell-detection/`.
2. Copy into it:
   - `CLAUDE.md`
   - `repeat-cell-detection-proposal-v4.md`
   - `hierarchy-recovery-proposal-v1.md`
   - `array_detector.py`
   - `synthetic_layouts.py`
3. `cd` into the folder and start Claude Code there (so it picks up `CLAUDE.md`
   automatically).

## The prompt to paste

```
Read CLAUDE.md fully, then read repeat-cell-detection-proposal-v4.md end to end
before writing any code. Skim array_detector.py and synthetic_layouts.py to
understand the validated reference mechanism and the three documented bugs.

We're starting Phase 1 only: a C++ implementation of the array-detection design,
using Boost.Geometry (not CGAL — see CLAUDE.md for why). Set up the project
structure, add Boost.Geometry as a dependency, and port the core mechanism from
the Python prototype -- but fix, don't reproduce, the three bugs documented in
CLAUDE.md. In particular:

- Implement both the occupancy-interval AND edge-boundary encodings from the
  design doc's §2.2, not just occupancy-interval (the prototype only has the
  latter, and CLAUDE.md explains why that's a real gap, not a stylistic choice).
- Implement defect/boundary checks by walking EXPECTED physical positions
  (start_x + k*dx) and comparing each one's actual content independently, not by
  re-slicing token indices across a trim boundary or across a defect -- that's
  exactly what caused bug 3.
- Implement the actual period-phase reconciliation from §2.4 (physical period +
  phase clustering, with the compatibility predicate as specified), not the
  simplified greedy sequential grouper the Python prototype uses.

Write unit tests for all three documented bugs as regression tests before moving
on. Then port both synthetic demo generators (bauhaus_layout and
standard_cell_layout) to produce the same two demo datasets, run detection on
both, and produce a visualization (original layout, detected regions outlined,
defects marked) for each.

Stop and show me the results before starting Phase 2 (hierarchy recovery) or
anything involving real GDS data -- I want to look at working, correct Phase 1
output first.
```
