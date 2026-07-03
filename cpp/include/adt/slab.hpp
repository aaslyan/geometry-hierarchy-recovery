// slab.hpp — finite scanline construction (§2.1).
//
// A horizontal slab is an open y-interval (y0, y1) between consecutive snapped
// vertex y-coordinates. Inside that interval every horizontal sample line meets
// the same set of rectangles, so one representative line at the midpoint fully
// characterizes the slab. We keep both the merged covered intervals (for the
// occupancy encoding) and the raw per-rectangle spans (for the edge-boundary
// encoding, which must see internal boundaries the merge would dissolve).
#pragma once

#include <vector>

#include "adt/types.hpp"

namespace adt {

// Snap a coordinate to the tolerance grid before slab construction (§2.1). This
// is what keeps near-duplicate coordinates from fragmenting the layout into a
// blizzard of near-zero-height slabs.
double snap(double v, double grid = 0.05);

struct Interval {
  double lo{}, hi{};
  double width() const { return hi - lo; }
};

struct Slab {
  double y0{}, y1{}, y_mid{};
  std::vector<Interval> covered;  // merged, sorted — for occupancy encoding
  std::vector<Interval> spans;    // raw per-rect spans, sorted by lo — for edge encoding
  std::vector<double> span_heights;  // height of the rect each `spans[i]` came from
};

// Build horizontal slabs from a rectilinear layout. `axis_swapped == true`
// builds *vertical* slabs instead, by treating x as the scan-normal axis — used
// for the vertical reconciliation pass without duplicating this code.
std::vector<Slab> build_slabs(const std::vector<Rect>& rects,
                              bool axis_swapped = false, double grid = 0.05);

}  // namespace adt
