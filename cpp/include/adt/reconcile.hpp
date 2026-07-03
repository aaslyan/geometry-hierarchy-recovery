// reconcile.hpp — period-phase reconciliation (§2.4).
//
// This replaces the prototype's greedy sequential grouper with the design's
// actual formulation:
//
//   * A defined compatibility predicate over PHYSICAL period, modular phase, and
//     spatial support (not "try shifts and see what fits").
//   * Connected-component clustering under that predicate, then a center-pruning
//     pass so a transitive chain of weak links can't drag incompatible runs into
//     a cluster (the §2.4 "weak-link" caution).
//   * Robust cluster outputs: median period, circular-median phase.
//   * A genuine VERTICAL pass, intersected with the horizontal pass, to recover
//     the 2D lattice (dx, dy). The prototype only ran horizontally and faked dy
//     from slab spacing; here dy comes from real vertical periodicity votes.
#pragma once

#include <vector>

#include "adt/runs.hpp"
#include "adt/types.hpp"

namespace adt {

// A run projected onto generic (scan, normal) axes so one clustering routine
// serves both passes. Horizontal: scan = x, normal = y. Vertical: scan = y,
// normal = x.
struct LineRun {
  double period{};
  double phase{};
  double scan_lo{}, scan_hi{};  // periodic support along the scan axis
  double norm_lo{}, norm_hi{};  // slab extent along the normal axis
  int run_index{};              // back-reference into the owning RowRun vector
};

// One reconciled 1D cluster: robust period/phase plus its spatial footprint.
struct LineCluster {
  double period{};
  double phase{};
  double scan_lo{}, scan_hi{};  // common periodic support (intersection)
  double norm_lo{}, norm_hi{};  // covered extent along the normal axis (union)
  int n_members{};
  std::vector<int> run_indices;
};

struct ReconcileParams {
  double eps_period = 0.75;
  double eps_phase = 0.75;
  double w_min = 5.0;   // minimum scan-axis overlap for compatibility
  int min_members = 2;  // minimum runs to accept a cluster
};

double dist_mod(double a, double b, double p);

// Cluster a set of line runs into reconciled period-phase clusters.
std::vector<LineCluster> cluster_runs(const std::vector<LineRun>& runs,
                                      const ReconcileParams& params = {});

// §6 defect taxonomy: missing = expected−observed (the instance's own geometry
// is incomplete — a real structural defect); extra = observed−expected (routing
// or clutter laid over an otherwise-complete instance).
enum class DefectType { Missing, Extra };

struct Defect {
  double x{}, y{};  // instance center
  DefectType type{DefectType::Missing};
  double area{};
};

// A 2D array hypothesis formed by intersecting a horizontal and a vertical
// cluster. Instance counts / defect map / tile are filled later by verification.
struct ArrayCandidate {
  Rect bbox{};
  double dx{}, dy{};
  double phase_x{}, phase_y{};
  Classification classification{Classification::Primitive};
  int supercell_k{1};

  // Filled by verify.cpp (§2.5 / §6):
  int n_cols{}, n_rows{};
  int n_instances{};
  int n_clean{};    // instances with no MISSING geometry (extra overlay still counts as clean)
  int n_missing{};  // instances with missing geometry (structural defects)
  int n_extra{};    // instances carrying extra/overlaid geometry
  std::vector<Defect> defects;
};

// Intersect horizontal and vertical clusters into 2D candidates. `h_runs` /
// `v_runs` are the underlying RowRun vectors, used to carry classification.
std::vector<ArrayCandidate> intersect_passes(
    const std::vector<LineCluster>& h_clusters,
    const std::vector<LineCluster>& v_clusters,
    const std::vector<RowRun>& h_runs, const std::vector<RowRun>& v_runs,
    const ReconcileParams& params = {});

}  // namespace adt
