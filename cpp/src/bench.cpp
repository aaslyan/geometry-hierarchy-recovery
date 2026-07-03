#include "adt/bench.hpp"

#include <algorithm>
#include <random>

#include "adt/d4.hpp"
#include "adt/recover.hpp"

namespace adt::hr {

namespace {
// Transform a rect list by D4 orientation g, normalize its bbox-min to origin,
// then translate so the bbox-min lands at (x, y) — the same convention the
// recoverer uses, so a built instance and a recovered instance line up exactly.
std::vector<Rect> place(const std::vector<Rect>& src, double x, double y, int g) {
  std::vector<Rect> t;
  double mnx = 1e300, mny = 1e300;
  for (const auto& r : src) {
    double ax, ay, bx, by;
    apply_pt(g, r.x0, r.y0, ax, ay);
    apply_pt(g, r.x1, r.y1, bx, by);
    Rect q{std::min(ax, bx), std::min(ay, by), std::max(ax, bx), std::max(ay, by)};
    t.push_back(q);
    mnx = std::min(mnx, q.x0);
    mny = std::min(mny, q.y0);
  }
  std::vector<Rect> out;
  for (const auto& q : t)
    out.push_back({x + q.x0 - mnx, y + q.y0 - mny, x + q.x1 - mnx, y + q.y1 - mny});
  return out;
}

// Leaf "gate": 4 rectangles, deliberately asymmetric so orientation is meaningful
// (four members lets a single missing rect be tolerated as a defect).
std::vector<Rect> gate_rects() {
  return {{0, 0, 6, 16}, {8, 0, 14, 6}, {8, 9, 14, 16}, {0, 18, 14, 21}};
}
void append(std::vector<Rect>& a, const std::vector<Rect>& b) {
  a.insert(a.end(), b.begin(), b.end());
}
}  // namespace

ErasureDesign make_datapath(int n_gates, int n_rows, int n_blocks, bool mirror,
                            int clutter, bool defect) {
  const double gate_pitch = 22, row_pitch = 26, block_pitch = 0;  // block_pitch set below
  ErasureDesign d;
  d.name = "datapath";
  d.gt_leaf_rects = (int)gate_rects().size();
  d.gt_levels = 4;
  d.gt_leaf_instances = n_gates * n_rows * n_blocks;  // defect drops a MEMBER, not an instance

  // slice = a horizontal array of gates.
  std::vector<Rect> slice;
  for (int i = 0; i < n_gates; ++i) append(slice, place(gate_rects(), i * gate_pitch, 0, 0));

  // block = n_rows slices stacked, alternate rows mirrored about the x-axis
  // (orientation 5, flip-y) so adjacent rows "share a rail" — standard practice.
  std::vector<Rect> block;
  for (int j = 0; j < n_rows; ++j) {
    int o = (mirror && (j % 2)) ? 5 : 0;
    append(block, place(slice, 0, j * row_pitch, o));
  }

  // chip = n_blocks blocks in a row.
  double bw = 0;
  for (const auto& r : block) bw = std::max(bw, r.x1);
  double bp = bw + 14;  // block pitch with a gutter
  std::vector<Rect> chip;
  for (int k = 0; k < n_blocks; ++k) append(chip, place(block, k * bp, 0, 0));

  d.flat = chip;

  // Optional defect: drop ONE rectangle of one interior gate (a missing member),
  // leaving a defective-but-recognizable instance for defect tolerance to catch.
  if (defect && (int)d.flat.size() > 6) d.flat.erase(d.flat.begin() + 6);

  // Non-repeating routing clutter → residual.
  std::mt19937 rng(11);
  std::uniform_real_distribution<double> u(0, 1);
  double maxx = 0, maxy = 0;
  for (const auto& r : d.flat) { maxx = std::max(maxx, r.x1); maxy = std::max(maxy, r.y1); }
  for (int c = 0; c < clutter; ++c) {
    double x = u(rng) * maxx, y = u(rng) * maxy;
    d.flat.push_back({x, y, x + 1.3, y + 8 + u(rng) * 30});
  }
  return d;
}

ErasureResult run_erasure(const ErasureDesign& d) {
  Hierarchy h = recover_hierarchy(d.flat);
  ErasureResult r;
  r.flatten_ok = h.flatten_matches;
  r.cells = (int)h.cells.size();
  r.levels = h.levels;
  r.top = (int)h.top.size();
  r.residual = (int)h.residual.size();
  r.n_defective = h.n_defective;
  r.defect_area = h.missing_area;
  r.flat_leaf = h.flat_leaf_count;
  r.hier_cost = h.hier_cost;
  r.compression = h.hier_cost ? (double)h.flat_leaf_count / h.hier_cost : 0;
  for (const auto& c : h.cells)
    if (c.leaf_count == d.gt_leaf_rects) r.leaf_recovered = true;
  return r;
}

}  // namespace adt::hr
