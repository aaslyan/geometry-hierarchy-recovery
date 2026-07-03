// synthetic.hpp — the two demo layout generators, ported from
// synthetic_layouts.py. Ground truth is carried in LayoutMeta for reporting
// only; it is never fed to the detector.
#pragma once

#include <string>
#include <vector>

#include "adt/types.hpp"

namespace adt {

struct LayoutMeta {
  std::string name;
  Rect grid_bbox{};
  double pitch_x{}, pitch_y{};
  int cols{}, rows{};
  int n_missing{};
};

struct Layout {
  std::vector<Rect> rects;
  LayoutMeta meta;
};

Layout bauhaus_layout();
Layout standard_cell_layout();

// A zero-gap variant of the standard-cell rows: cells abut with NO gap. The
// occupancy encoding alone merges each row into one covered band and loses all
// structure; this exists to demonstrate the edge-boundary encoding recovering
// the array where occupancy cannot (closes CLAUDE.md's "known gap").
Layout standard_cell_abutted_layout();

}  // namespace adt
