// lattice.hpp — lattice-prior dense-array recovery (the 2D fix, paper §7.6/§9).
//
// The base recoverer's seed-and-extend growth fails on a dense array whose tile
// EXTENT exceeds the lattice PITCH: from any seed, a radius large enough to
// capture the whole tile also reaches the abutting neighbour, so growth
// over-covers and the exact-multiset gate reverts the round to nothing (this is
// exactly what happened on the real SKY130 bitcell array in 2D).
//
// This module fixes it by making the lattice DRIVE candidate generation instead
// of only scoring it. It finds a translational axis-aligned lattice (dx, dy) that
// a large fraction of primitives respect, then extracts the fundamental-domain
// tile by ANCHOR-MODULO partition: every rectangle is assigned to the cell its
// min-corner falls in and to a within-cell class. A tile member may extend beyond
// its own cell into the neighbour's domain — that is fine, because members are
// grouped by anchor, not by a growth radius, so a tile packed tighter than its
// own extent is still separated cleanly. Placing the tile at every fully-occupied
// cell reproduces G exactly; non-conforming rectangles fall to residual.
#pragma once

#include <vector>

#include "adt/hierarchy.hpp"
#include "adt/recover.hpp"
#include "adt/types.hpp"

namespace adt::hr {

struct LatticeInfo {
  bool ok = false;
  double dx = 0, dy = 0;   // translational lattice generators
  double ox = 0, oy = 0;   // origin (min anchor)
  int ncols = 0, nrows = 0;
  int tile_members = 0;    // rectangles in one fundamental-domain tile
  int placements = 0;      // fully-occupied lattice cells
  int residual = 0;        // rectangles explained by no tile
};

// Recover a dense array via the lattice prior. Returns a base-style Hierarchy
// (one tile cell + a placement per full cell + residual) with flatten(H) == G
// exact, or an empty Hierarchy (no cells) if no dominant lattice is found.
Hierarchy recover_lattice(const std::vector<Rect>& layout,
                          const RecoverConfig& cfg = {}, LatticeInfo* info = nullptr);

}  // namespace adt::hr
