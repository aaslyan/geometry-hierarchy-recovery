// recover.hpp — hierarchy recovery driver (companion proposal §2).
//
// Channel B (geometric shingles) → seed-and-extend growth → canonical-equality
// verification → MDL scoring → greedy set-packing selection → replace & recurse.
// The knobs below are the "non-uniqueness controls" the design implies: because a
// flat layout admits many valid hierarchies (§1.3), these decide WHICH compact
// hierarchy we land on (minimum cell size, minimum repeat count, how aggressively
// gain must clear zero, how deep to recurse).
#pragma once

#include <vector>

#include "adt/hierarchy.hpp"
#include "adt/types.hpp"

namespace adt::hr {

struct RecoverConfig {
  double quantum = 0.5;      // coordinate/shape quantization for signatures
  int neighborhood_k = 1;    // Channel-B seed size (seed + k nearest); small on
                             // purpose — seed-and-extend growth completes the body
  double grow_radius = 60.0; // seed-and-extend search radius around a motif
  int max_cell_members = 256;// cap on cell body size — bounds growth so a perfectly
                             // periodic region can't grow an unbounded cell (perf)

  // MDL / selection knobs (companion §2.6–§2.7). These are the tunables your
  // "min cell / min rate" intuition maps onto:
  int min_cell_members = 2;  // smallest body worth promoting to a cell (tokens)
  int min_instances = 2;     // a motif must recur at least this many times
  double cost_instance = 1.0;// per-placement reference cost
  double def_overhead = 1.0; // fixed per-cell definition overhead
  double gain_min = 0.0;     // promote only if MDL gain exceeds this

  int max_levels = 6;        // recursion depth cap (companion §2.8)

  // Defect tolerance (companion §6). After clean cells are established, a rescan
  // accepts PARTIAL instances (a missing member or two), recording the gaps as a
  // defect layer. Only leaf members may be missing — a missing sub-cell reference
  // is never tolerated, so nested structure stays exact.
  bool defect_tolerant = true;
  double defect_min_support = 0.66;  // fraction of members that must be present
  int defect_max_missing = 2;        // cap on missing members per instance
};

Hierarchy recover_hierarchy(const std::vector<Rect>& layout,
                            const RecoverConfig& cfg = {});

// Expand a hierarchy back to flat leaf rectangles (companion §6 flatten(H)).
std::vector<Rect> flatten(const Hierarchy& h);

// Fit a regular 2D axis-aligned lattice to a set of instance anchors. This is
// the bridge that makes "array detection = the lattice-aligned special case of
// hierarchy recovery" concrete: recover a cell's instances, fit a lattice, and
// its (dx, dy, cols, rows) should match what the array detector finds directly.
struct LatticeFit {
  bool ok = false;          // do all anchors lie on a single regular lattice?
  double ox = 0, oy = 0;    // lattice origin
  double dx = 0, dy = 0;    // step vectors
  int ncols = 0, nrows = 0; // grid dimensions
  int occupied = 0;         // lattice sites actually filled
  int missing = 0;          // ncols*nrows - occupied
};
LatticeFit fit_lattice(const std::vector<std::array<double, 2>>& anchors,
                       double tol = 1.0);

}  // namespace adt::hr
