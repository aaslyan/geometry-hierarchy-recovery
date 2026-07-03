#include "adt/verify.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>

namespace adt {

namespace {
// A coarse signature of a canonicalized tile clip: enough to bucket cells that
// hold the same geometry, tolerant of sub-grid snapping noise.
std::string tile_signature(const MultiPolygon& m) {
  double a = area(m);
  Point lo, hi;
  bg::model::box<Point> env;
  std::size_t npoly = m.size();
  double w = 0, h = 0;
  if (npoly > 0) {
    bg::envelope(m, env);
    w = bg::get<bg::max_corner, 0>(env) - bg::get<bg::min_corner, 0>(env);
    h = bg::get<bg::max_corner, 1>(env) - bg::get<bg::min_corner, 1>(env);
  }
  auto q = [](double v) { return std::lround(v / 0.5); };
  return std::to_string(npoly) + ":" + std::to_string(q(a)) + ":" +
         std::to_string(q(w)) + ":" + std::to_string(q(h));
}
}  // namespace

bool verify_candidate(const MultiPolygon& layout, ArrayCandidate& c,
                      const VerifyParams& params) {
  const double dx = c.dx, dy = c.dy;
  if (dx <= 0 || dy <= 0) return false;

  const double W = c.bbox.x1 - c.bbox.x0;
  const double H = c.bbox.y1 - c.bbox.y0;
  int n_cols = std::max(1L, std::lround(W / dx));
  int n_rows = std::max(1L, std::lround(H / dy));
  c.n_cols = n_cols;
  c.n_rows = n_rows;
  c.n_instances = n_cols * n_rows;

  const double ox = c.bbox.x0, oy = c.bbox.y0;

  // Clip each expected cell, translate back to the origin cell, canonicalize.
  struct Cell { int i, j; MultiPolygon at_origin; };
  std::vector<Cell> cells;
  cells.reserve(c.n_instances);
  std::map<std::string, int> sig_count;
  std::map<std::string, MultiPolygon> sig_repr;
  for (int j = 0; j < n_rows; ++j) {
    for (int i = 0; i < n_cols; ++i) {
      Rect cell{ox + i * dx, oy + j * dy, ox + (i + 1) * dx, oy + (j + 1) * dy};
      MultiPolygon clipped = clip(layout, cell);
      MultiPolygon at_origin = translate(clipped, -i * dx, -j * dy);
      std::string sig = tile_signature(at_origin);
      sig_count[sig]++;
      if (!sig_repr.count(sig)) sig_repr[sig] = at_origin;
      cells.push_back({i, j, std::move(at_origin)});
    }
  }

  // Canonical tile = modal cell content (robust to a defective reference cell).
  std::string best_sig;
  int best_n = -1;
  for (const auto& kv : sig_count)
    if (kv.second > best_n) { best_n = kv.second; best_sig = kv.first; }
  const MultiPolygon& tile = sig_repr[best_sig];

  // Per-instance defect check: missing and extra tracked separately (§6). An
  // instance counts as clean when nothing of its OWN geometry is missing; extra
  // overlaid geometry (routing over a filler/std-cell array) is reported but does
  // not make the instance a structural defect. The pseudo-array acceptance gate
  // is therefore driven by MISSING geometry, so a valid array that simply has
  // routing over it is still certified.
  int n_clean = 0, n_missing = 0, n_extra = 0;
  c.defects.clear();
  for (const auto& cell : cells) {
    double miss = area(difference(tile, cell.at_origin));   // expected − observed
    double ext = area(difference(cell.at_origin, tile));    // observed − expected
    double cx = ox + (cell.i + 0.5) * dx, cy = oy + (cell.j + 0.5) * dy;
    bool has_missing = miss > params.area_tol;
    bool has_extra = ext > params.area_tol;
    if (has_missing) {
      ++n_missing;
      c.defects.push_back({cx, cy, DefectType::Missing, miss});
    } else {
      ++n_clean;
    }
    if (has_extra) {
      ++n_extra;
      if (!has_missing) c.defects.push_back({cx, cy, DefectType::Extra, ext});
    }
  }
  c.n_clean = n_clean;
  c.n_missing = n_missing;
  c.n_extra = n_extra;

  double clean_fraction =
      c.n_instances ? static_cast<double>(n_clean) / c.n_instances : 0.0;
  return clean_fraction >= params.min_clean_fraction;
}

}  // namespace adt
