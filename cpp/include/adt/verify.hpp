// verify.hpp — geometric verification (§2.5) and defect map (§6).
//
// Symbolic reconciliation only establishes a NECESSARY condition (line-wise
// periodicity). This stage upgrades a 2D hypothesis to a certified array by
// canonicalized geometric comparison, and — since it's already clipping every
// expected instance — computes the §6 defect map in the same pass via
// `missing = expected − observed` / `extra = observed − expected`.
//
// Robust to a defective reference cell: the canonical tile is the MODAL cell
// content across all instances, not cell (0,0) blindly, so one missing/extra
// polygon can't define the tile everyone else is measured against.
#pragma once

#include "adt/geometry.hpp"
#include "adt/reconcile.hpp"

namespace adt {

struct VerifyParams {
  double area_tol = 0.5;            // per-instance symmetric-difference area slack
  double min_clean_fraction = 0.5; // below this, the hypothesis is a pseudo-array
};

// Verify and annotate a candidate in place. Returns true if certified (enough
// instances match the canonical tile), false if rejected as a pseudo-array.
bool verify_candidate(const MultiPolygon& layout, ArrayCandidate& c,
                      const VerifyParams& params = {});

}  // namespace adt
