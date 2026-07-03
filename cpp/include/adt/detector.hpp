// detector.hpp — top-level array-detection driver (§2 pipeline).
#pragma once

#include <vector>

#include "adt/reconcile.hpp"
#include "adt/runs.hpp"
#include "adt/types.hpp"
#include "adt/verify.hpp"

namespace adt {

struct DetectorConfig {
  RunParams run;
  ReconcileParams reconcile;
  VerifyParams verify;
  double grid = 0.05;
  double width_quantum = 0.5;
  double height_quantum = 0.5;
};

struct DetectionResult {
  std::vector<ArrayCandidate> arrays;
  // Funnel counts for the §7.5 candidate-reduction report (N ≫ M ≫ K).
  int n_symbolic_runs{};   // N: per-line runs found (both passes, both encodings)
  int n_clusters{};        // M: reconciled period-phase clusters (both passes)
  int n_verified{};        // K: certified arrays after geometric verification
};

DetectionResult detect_arrays(const std::vector<Rect>& rects,
                              const DetectorConfig& cfg = {});

}  // namespace adt
