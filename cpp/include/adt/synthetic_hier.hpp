// synthetic_hier.hpp — demo layout for hierarchy recovery. Unlike the array
// demos, instances are placed at IRREGULAR (non-lattice) positions, and there is
// a genuine two-level nesting (a super-cell built from the leaf motif), so the
// recovered hierarchy exercises exactly what array detection cannot: arbitrary
// placement and arrays-inside-arrays.
#pragma once

#include <string>
#include <vector>

#include "adt/types.hpp"

namespace adt::hr {

struct HierLayout {
  std::vector<Rect> rects;
  std::string name;
  // Ground truth (for reporting only, never fed to the recoverer):
  int gt_leaf_motif_instances = 0;  // total placements of the leaf motif L
  int gt_super_instances = 0;       // placements of the super-cell T
  int gt_residual = 0;              // clutter rectangles
};

// Leaf motif L = 2 rectangles; super-cell T = 3 L's in a fixed scalene cluster.
// T is placed at several irregular spots, L at several more, plus clutter.
HierLayout nested_motif_layout();

// Same leaf motif, but instances appear in several DIFFERENT D4 orientations
// (mirrored / rotated), like alternating standard-cell rows. Exercises the
// reflection+rotation extension: recovery must find ONE cell with instances that
// differ only by orientation.
HierLayout oriented_motif_layout();

// A 3-rectangle motif at irregular positions where ONE instance is missing a
// rectangle (a defect) and clutter overlays part of another. Exercises defect-
// tolerant recovery: every instance recovered, the missing member recorded, and
// G ⊆ flatten(H).
HierLayout defective_motif_layout();

}  // namespace adt::hr
