// unify_demo.cpp — demonstrate the central claim shared by both design docs:
// array detection is the LATTICE-ALIGNED SPECIAL CASE of hierarchy recovery.
//
// On a flat array layout we run both algorithms and show they agree:
//   * the array detector finds a tile + step vectors + grid directly;
//   * hierarchy recovery (constrained to one level) recovers the same tile as a
//     cell, and fitting a lattice to its instance anchors reproduces the same
//     dx/dy/grid. Unconstrained, hierarchy recovery instead finds an even more
//     compact nested hierarchy — a live example of §1.3 non-uniqueness.
#include <cstdio>
#include <vector>

#include "adt/detector.hpp"
#include "adt/recover.hpp"
#include "adt/synthetic.hpp"

using namespace adt;

static void unify(const std::string& name, const std::vector<Rect>& rects) {
  std::printf("\n=== %s (%zu rects) ===\n", name.c_str(), rects.size());

  // (A) Array detector — finds the lattice directly.
  DetectionResult det = detect_arrays(rects);
  std::printf("[array detector]      ");
  if (det.arrays.empty()) { std::printf("no array\n"); }
  else {
    const auto& a = det.arrays[0];
    std::printf("dx=%.1f dy=%.1f grid=%dx%d  instances=%d (clean=%d, missing=%d)\n",
                a.dx, a.dy, a.n_cols, a.n_rows, a.n_instances, a.n_clean, a.n_missing);
  }

  // (B) Hierarchy recovery, one level — tile + instances, then fit a lattice.
  hr::RecoverConfig cfg; cfg.max_levels = 1;
  hr::Hierarchy h = hr::recover_hierarchy(rects, cfg);
  int big = 0, best = -1;
  for (const auto& c : h.cells) {
    int inst = 0;
    for (const auto& p : h.top) if (p.cell == c.id) inst++;
    if (inst > best) { best = inst; big = c.id; }
  }
  std::vector<std::array<double, 2>> anchors;
  for (const auto& p : h.top) if (p.cell == big) anchors.push_back({p.x, p.y});
  hr::LatticeFit lf = hr::fit_lattice(anchors);
  std::printf("[hierarchy, 1 level]  cell=%d-rect tile x %d instances; lattice ",
              big ? (int)h.cells[big - 1].leaf_count : 0, (int)anchors.size());
  if (lf.ok)
    std::printf("dx=%.1f dy=%.1f grid=%dx%d  occupied=%d missing=%d  (flatten==G: %s)\n",
                lf.dx, lf.dy, lf.ncols, lf.nrows, lf.occupied, lf.missing,
                h.flatten_matches ? "yes" : "no");
  else
    std::printf("not a single regular lattice\n");

  // (C) Unconstrained hierarchy recovery — the more compact nested view.
  hr::Hierarchy hf = hr::recover_hierarchy(rects);
  std::printf("[hierarchy, full]     %zu cells across %d levels, cost %d vs flat %d "
              "(%.2fx), array-cost %d (%.2fx)  — a different compact view (§1.3)\n",
              hf.cells.size(), hf.levels, hf.hier_cost, hf.flat_leaf_count,
              (double)hf.flat_leaf_count / hf.hier_cost, hf.array_cost,
              (double)hf.flat_leaf_count / hf.array_cost);
}

int main() {
  unify("bauhaus", bauhaus_layout().rects);
  unify("standard_cell", standard_cell_layout().rects);
  std::printf("\nArray detection ≡ hierarchy recovery (1 level) + lattice fit.\n");
  return 0;
}
