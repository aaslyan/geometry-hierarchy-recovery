// test_unify.cpp — array detection is the lattice-aligned special case of
// hierarchy recovery. On flat bauhaus, one-level recovery + lattice fit must
// reproduce the array detector's dx/dy/grid, and account for every tile site.
#include <cstdio>
#include <cmath>
#include <vector>

#include "adt/detector.hpp"
#include "adt/recover.hpp"
#include "adt/synthetic.hpp"

using namespace adt;

static int g_failures = 0;
#define CHECK(cond, msg)                                                     \
  do {                                                                       \
    if (!(cond)) { std::printf("  FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); ++g_failures; } \
    else std::printf("  ok:   %s\n", msg);                                   \
  } while (0)

int main() {
  std::printf("[unify] array detection == hierarchy recovery (1 level) + lattice\n");
  auto rects = bauhaus_layout().rects;

  DetectionResult det = detect_arrays(rects);
  CHECK(det.arrays.size() == 1, "array detector finds one array");

  hr::RecoverConfig cfg; cfg.max_levels = 1;
  hr::Hierarchy h = hr::recover_hierarchy(rects, cfg);
  CHECK(h.flatten_matches, "one-level recovery flattens back to G exactly");
  CHECK(h.cells.size() == 1, "one-level recovery finds one tile cell");

  int big = 0, best = -1;
  for (const auto& c : h.cells) {
    int inst = 0;
    for (const auto& p : h.top) if (p.cell == c.id) inst++;
    if (inst > best) { best = inst; big = c.id; }
  }
  std::vector<std::array<double, 2>> anchors;
  for (const auto& p : h.top) if (p.cell == big) anchors.push_back({p.x, p.y});
  hr::LatticeFit lf = hr::fit_lattice(anchors);
  CHECK(lf.ok, "recovered tile instances lie on a single regular lattice");

  if (!det.arrays.empty() && lf.ok) {
    const auto& a = det.arrays[0];
    CHECK(std::abs(lf.dx - a.dx) < 1.0, "lattice dx matches array detector dx");
    CHECK(std::abs(lf.dy - a.dy) < 1.0, "lattice dy matches array detector dy");
    CHECK(lf.ncols == a.n_cols && lf.nrows == a.n_rows,
          "lattice grid matches array detector grid");
    CHECK(lf.occupied + lf.missing == a.n_instances,
          "occupied + missing sites == array instance count");
    CHECK(lf.missing == a.n_missing, "lattice hole count matches array missing count");
  }

  std::printf("\n%s (%d failure%s)\n", g_failures == 0 ? "ALL PASSED" : "FAILURES",
              g_failures, g_failures == 1 ? "" : "s");
  return g_failures == 0 ? 0 : 1;
}
