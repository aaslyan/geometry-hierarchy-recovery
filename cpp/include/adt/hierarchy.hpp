// hierarchy.hpp — types for hierarchy recovery (companion proposal §1.1).
//
// A hierarchy H = (cell definitions, top-level instance placements, residual
// geometry) such that flatten(H) == G within tolerance. Cells may reference
// smaller cells (nested hierarchy, §2.8), which is how "arrays inside arrays"
// are represented: a super-cell whose members are themselves cell references.
#pragma once

#include <string>
#include <vector>

#include "adt/types.hpp"

namespace adt::hr {

// A primitive in the working representation of one recursion level. `cell == 0`
// is a leaf rectangle; `cell >= 1` is a reference to CellDef with that id, placed
// so its body-anchor sits at box's min-corner. `box` is the primitive's bbox
// (the rectangle itself for a leaf; the placed cell's bbox for a reference) — the
// anchor used for all translation-invariant comparison is (box.x0, box.y0).
struct Prim {
  int cell = 0;
  Rect box{};
  int orient = 0;  // D4 orientation of a cell reference (0 for leaf rectangles)
};
inline bool is_leaf(const Prim& p) { return p.cell == 0; }
inline double ax(const Prim& p) { return p.box.x0; }
inline double ay(const Prim& p) { return p.box.y0; }

// One member of a cell body, expressed RELATIVE to the cell's anchor so the same
// cell placed anywhere produces the same member list (translation invariance).
struct Member {
  int cell = 0;      // 0 = leaf rectangle, >=1 = nested sub-cell
  double dx = 0, dy = 0;  // member anchor relative to cell anchor
  double w = 0, h = 0;    // member bbox dimensions
  int orient = 0;    // D4 orientation of a sub-cell reference (0 for leaf)
};

struct CellDef {
  int id = 0;
  std::vector<Member> members;  // may include sub-cell references (nesting)
  double w = 0, h = 0;          // body bounding box
  int leaf_count = 0;           // fully-expanded leaf-rectangle count
  int level = 0;                // recursion level it was discovered at (1 = leaves)
};

struct Placement {
  int cell = 0;      // which CellDef
  double x = 0, y = 0;  // translation of this top-level instance
  int orient = 0;    // D4 orientation of this instance
};

// Compact top-level representation for placements of the same cell/orientation
// that lie on a regular lattice. The ordinary `top` placements remain the
// authoritative expansion path; array nodes are an additional compressed view.
struct ArrayNode {
  int cell = 0;
  int orient = 0;
  double ox = 0, oy = 0;
  double dx = 0, dy = 0;
  int ncols = 0, nrows = 0;
  int occupied = 0;
  int missing = 0;
};

struct Hierarchy {
  std::vector<CellDef> cells;      // indexed by id-1
  std::vector<Placement> top;      // top-level instances
  std::vector<ArrayNode> arrays;    // compact lattice view over top placements
  std::vector<Rect> residual;      // leaf rectangles explained by no cell
  int levels = 0;

  // Defect layer (companion §6, ported from the array detector). The authoritative
  // defect geometry is `flatten(H) − G` computed with Boost, so clutter overlapping
  // a missing member is handled correctly; `missing_geometry` keeps the rescan's
  // idealized member rects for visualization. Correctness constraint: G ⊆ flatten(H)
  // (every real polygon is explained; idealized-but-absent geometry is a defect).
  std::vector<Rect> missing_geometry;  // idealized missing members (for display)
  double missing_area = 0;             // area of flatten(H) − G  (true defect area)
  int n_defective = 0;                 // instances recognized despite missing members

  // Reporting (companion §6):
  int flat_leaf_count = 0;         // |G| in leaf rectangles
  int hier_cost = 0;               // Σ body sizes + top instances + residual
  int array_cost = 0;              // same, but lattice top placements cost O(1)
  bool flatten_matches = false;    // (flatten(H) − missing) == G within tolerance
};

}  // namespace adt::hr
