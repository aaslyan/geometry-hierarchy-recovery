#include "adt/synthetic.hpp"

#include <random>
#include <set>

namespace adt {

Layout bauhaus_layout() {
  Layout L;
  auto& rects = L.rects;

  const int cols = 8, rows = 6;
  const double pitch = 40.0;
  const double origin_x = 60.0, origin_y = 60.0;
  const std::set<std::pair<int, int>> skip = {{3, 2}};  // one deliberate defect

  for (int c = 0; c < cols; ++c) {
    for (int r = 0; r < rows; ++r) {
      if (skip.count({c, r})) continue;
      double tx = origin_x + c * pitch;
      double ty = origin_y + r * pitch;
      rects.push_back({tx + 4, ty + 4, tx + 24, ty + 36});   // tall bar
      rects.push_back({tx + 28, ty + 4, tx + 36, ty + 12});  // small square
    }
  }

  double gx0 = origin_x, gy0 = origin_y;
  double gx1 = origin_x + cols * pitch, gy1 = origin_y + rows * pitch;

  // Non-repeating decorative border (clutter). Widths deliberately avoid the
  // motif's token widths so the border can't masquerade as repeated content.
  rects.push_back({gx0 - 45, gy0 - 30, gx1 + 45, gy0 - 10});
  rects.push_back({gx0 - 45, gy1 + 10, gx1 + 45, gy1 + 30});
  rects.push_back({gx0 - 45, gy0 - 30, gx0 - 10, gy1 + 30});
  rects.push_back({gx1 + 10, gy0 - 30, gx1 + 45, gy1 + 30});
  rects.push_back({gx0 - 22, gy0 - 22, gx0 - 16, gy0 - 16});
  rects.push_back({gx1 + 16, gy1 + 16, gx1 + 22, gy1 + 22});
  rects.push_back({gx0 + 55, gy1 + 14, gx0 + 145, gy1 + 26});

  L.meta = {"bauhaus_grid", {gx0, gy0, gx1, gy1}, pitch, pitch, cols, rows, 1};
  return L;
}

namespace {
Layout make_standard_cell(double gap, const std::string& name,
                          bool crossing_clutter) {
  Layout L;
  auto& rects = L.rects;

  const double row_height = 20.0;
  const double row_pitch = 26.0;
  const int n_rows = 5;
  const double cell_widths[4] = {12.0, 18.0, 22.0, 27.0};  // four DISTINCT widths
  const int n_repeats = 20;
  const double origin_x = 40.0, origin_y = 40.0;
  const int row_missing_row = 2, row_missing_rep = 9;  // one deliberate defect

  std::vector<Rect> row_extents;
  for (int r = 0; r < n_rows; ++r) {
    double x = origin_x;
    double y0 = origin_y + r * row_pitch;
    double y1 = y0 + row_height;
    for (int rep = 0; rep < n_repeats; ++rep) {
      double w = cell_widths[rep % 4];
      if (r == row_missing_row && rep == row_missing_rep) {
        x += w + gap;  // leave a hole exactly where a cell would sit
        continue;
      }
      rects.push_back({x, y0, x + w, y1});
      x += w + gap;
    }
    row_extents.push_back({origin_x, y0, x - gap, y1});
  }

  double gx1 = 0;
  for (const auto& re : row_extents) gx1 = std::max(gx1, re.x1);
  double max_y = origin_y + n_rows * row_pitch + 10;

  if (crossing_clutter) {
    // Routing clutter: thin vertical wires at irregular positions, crossing the
    // rows. Fixed width 1.2 is far from any real cell width, so it stays its own
    // distinct token. The gapped occupancy encoding shrugs this off.
    std::mt19937 rng(7);
    std::uniform_real_distribution<double> u01(0.0, 1.0);
    auto uni = [&](double lo, double hi) { return lo + (hi - lo) * u01(rng); };
    for (int k = 0; k < 10; ++k) {
      double rx = uni(origin_x, gx1 + 20 - 3);
      double ry = uni(origin_y - 15, max_y);
      rects.push_back({rx, ry, rx + 1.2, ry + uni(15, 45)});
    }
  } else {
    // Non-crossing border clutter. Crossing routing over ZERO-gap cells corrupts
    // the edge-only stream (there are no gaps to anchor the period), so the
    // abutted variant keeps clutter clear of the rows: it isolates the point it
    // exists to make — that the edge-boundary encoding recovers directly-abutted
    // geometry the occupancy encoding cannot — without conflating it with an
    // edge-encoding clutter-robustness claim the design doesn't yet support.
    rects.push_back({origin_x - 30, origin_y - 14, gx1 + 30, origin_y - 6});
    rects.push_back({origin_x - 30, max_y + 4, gx1 + 30, max_y + 12});
    rects.push_back({origin_x - 26, origin_y - 4, origin_x - 18, max_y});
  }
  L.meta = {name, {origin_x, origin_y, gx1, origin_y + (n_rows - 1) * row_pitch + row_height},
            0, row_pitch, n_repeats, n_rows, 1};
  return L;
}
}  // namespace

Layout standard_cell_layout() {
  return make_standard_cell(2.0, "standard_cell_rows", /*crossing_clutter=*/true);
}
Layout standard_cell_abutted_layout() {
  return make_standard_cell(0.0, "standard_cell_abutted", /*crossing_clutter=*/false);
}

}  // namespace adt
