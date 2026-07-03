#include "adt/slab.hpp"

#include <algorithm>
#include <cmath>
#include <set>

namespace adt {

namespace {
constexpr double kEps = 1e-6;

// One rectangle as seen on the scan axis: [lo,hi] is its extent along the scan
// direction, [nlo,nhi] its extent along the normal (the slab-stacking) axis,
// and `height` the length of its edges parallel to the normal axis (used as the
// edge-boundary symbol). For horizontal slabs the scan axis is x; when
// axis_swapped we scan y instead, so the roles of the two axes flip.
struct ScanRect {
  double lo, hi;    // extent along scan axis
  double nlo, nhi;  // extent along normal (slab) axis
  double height;    // edge length along the normal axis
};

ScanRect to_scan(const Rect& r, bool axis_swapped) {
  if (!axis_swapped) return {r.x0, r.x1, r.y0, r.y1, r.height()};
  // Vertical pass: scan along y, stack slabs along x.
  return {r.y0, r.y1, r.x0, r.x1, r.width()};
}
}  // namespace

double snap(double v, double grid) { return std::round(v / grid) * grid; }

std::vector<Slab> build_slabs(const std::vector<Rect>& rects, bool axis_swapped,
                              double grid) {
  std::vector<ScanRect> sr;
  sr.reserve(rects.size());
  for (const auto& r : rects) sr.push_back(to_scan(r, axis_swapped));

  // Snapped, unique coordinates along the normal axis define the slab cuts.
  std::set<double> cuts;
  for (const auto& s : sr) {
    cuts.insert(snap(s.nlo, grid));
    cuts.insert(snap(s.nhi, grid));
  }
  std::vector<double> ys(cuts.begin(), cuts.end());

  std::vector<Slab> slabs;
  for (std::size_t k = 0; k + 1 < ys.size(); ++k) {
    double y0 = ys[k], y1 = ys[k + 1];
    if (y1 - y0 < kEps) continue;
    double mid = 0.5 * (y0 + y1);  // strictly inside the open interval

    // Collect the raw spans of every rect whose normal extent straddles `mid`.
    std::vector<Interval> spans;
    std::vector<double> heights;
    for (const auto& s : sr) {
      if (s.nlo < mid && mid < s.nhi) {
        spans.push_back({s.lo, s.hi});
        heights.push_back(s.height);
      }
    }
    if (spans.empty()) continue;

    // Sort spans (and their heights in lock-step) by scan-axis position.
    std::vector<std::size_t> order(spans.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(),
              [&](std::size_t a, std::size_t b) { return spans[a].lo < spans[b].lo; });

    Slab slab;
    slab.y0 = y0;
    slab.y1 = y1;
    slab.y_mid = mid;
    slab.spans.reserve(spans.size());
    slab.span_heights.reserve(spans.size());
    for (std::size_t idx : order) {
      slab.spans.push_back(spans[idx]);
      slab.span_heights.push_back(heights[idx]);
    }

    // Merge into covered intervals for the occupancy encoding.
    for (const auto& iv : slab.spans) {
      if (!slab.covered.empty() && iv.lo <= slab.covered.back().hi + kEps) {
        slab.covered.back().hi = std::max(slab.covered.back().hi, iv.hi);
      } else {
        slab.covered.push_back(iv);
      }
    }
    slabs.push_back(std::move(slab));
  }
  return slabs;
}

}  // namespace adt
