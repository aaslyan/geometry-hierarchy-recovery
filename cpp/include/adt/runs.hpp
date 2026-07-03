// runs.hpp — per-line repetition detection (§2.3) + defect-tolerant scan (§6).
//
// This is where two of the three documented bugs are fixed by construction:
//
//   Bug 2 (spurious sub-period from coincidentally-equal widths): the period is
//   the smallest one whose majority-vote canonical unit actually REPRODUCES the
//   row above a strict self-consistency floor — not merely the smallest one that
//   clears a loose match-fraction. A half-period that only matches because two
//   of four cell widths happen to be equal scores well below the strict floor
//   and is rejected; the true primitive is selected. The loose floor is retained
//   only to spot a genuine supercell (content near-repeats at d but is truly
//   invariant only at k·d).
//
//   Bug 3 (phase desync across a trim boundary or a defect): the defect scan
//   walks EXPECTED physical positions x_start + m·dx and compares each one's
//   actual filled geometry — looked up by physical x-window, never by token
//   index — against the canonical unit expressed in physical offsets. A missing
//   instance therefore shows up as a defect at its correct x, and every position
//   after it is still addressed by physical coordinate, so nothing downstream of
//   a defect shifts. Token-index bookkeeping is never load-bearing across a gap.
#pragma once

#include <optional>
#include <vector>

#include "adt/encoding.hpp"
#include "adt/slab.hpp"
#include "adt/types.hpp"

namespace adt {

enum class Classification { Primitive, Supercell };

struct RowRun {
  int slab_index{};
  Encoding encoding{};
  double y0{}, y1{}, y_mid{};

  int token_period{};
  double dx{};      // physical period
  double phase{};   // x_start mod dx
  double x_start{}, x_end{};

  Classification classification{Classification::Primitive};
  int supercell_k{1};

  int n_expected{};
  int n_clean{};
  std::vector<double> defect_positions;  // physical x of each defective position

  // Canonical unit as physical covered sub-intervals within [0, dx), phased to
  // begin at x_start. Expected geometry at position m is these offsets shifted
  // by x_start + m·dx — the representation that makes the bug-3 fix possible.
  std::vector<Interval> unit_covered;
};

struct RunParams {
  int min_repeats = 3;
  double loose_floor = 0.55;       // "there is *some* periodicity here"
  double consistency_slack = 0.05; // how far below the best consistency still counts
                                   // as "the same period" (relative, not absolute)
  double supercell_floor = 0.80;   // a sub-period must NEAR-repeat this well to imply
                                   // the chosen period is a supercell (else primitive)
  double match_tol = 0.25;         // physical tolerance for expected-vs-observed
};

// Analyze one slab under one encoding. `filled_view` is the physical geometry
// the defect scan compares against — merged `slab.covered` for the occupancy
// encoding, raw `slab.spans` for edge-boundary (so abutted cells stay distinct).
std::optional<RowRun> analyze_row(int slab_index, const Slab& slab,
                                  const TokenStream& ts,
                                  const std::vector<Interval>& filled_view,
                                  const RunParams& params = {});

}  // namespace adt
