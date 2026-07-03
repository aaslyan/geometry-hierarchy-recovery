#include "adt/synthetic_hier.hpp"

#include <algorithm>
#include <cmath>

#include "adt/d4.hpp"

namespace adt::hr {

namespace {
// Leaf motif L placed with its anchor at (ox, oy): a tall bar + a small foot.
void place_L(std::vector<Rect>& out, double ox, double oy) {
  out.push_back({ox + 0, oy + 0, ox + 8, oy + 20});    // bar
  out.push_back({ox + 10, oy + 0, ox + 18, oy + 8});   // foot
}

// Super-cell T = three L's in a fixed SCALENE arrangement (distinct pairwise
// distances, so nearest-neighbor seeding is unambiguous), anchor at (ox, oy).
void place_T(std::vector<Rect>& out, double ox, double oy) {
  place_L(out, ox + 0, oy + 0);
  place_L(out, ox + 34, oy + 6);
  place_L(out, ox + 12, oy + 34);
}
}  // namespace

HierLayout nested_motif_layout() {
  HierLayout L;
  L.name = "nested_motif";

  // Three super-cells at irregular positions (each contains 3 leaf motifs).
  const double T_at[][2] = {{60, 60}, {300, 90}, {180, 300}};
  for (auto& p : T_at) place_T(L.rects, p[0], p[1]);

  // Four standalone leaf motifs at further irregular positions.
  const double L_at[][2] = {{430, 300}, {70, 430}, {380, 430}, {470, 70}};
  for (auto& p : L_at) place_L(L.rects, p[0], p[1]);

  // Clutter: non-repeating rectangles of distinct sizes → residual geometry.
  L.rects.push_back({250, 210, 268, 226});
  L.rects.push_back({150, 200, 159, 246});
  L.rects.push_back({410, 180, 430, 188});

  L.gt_super_instances = 3;
  L.gt_leaf_motif_instances = 3 * 3 + 4;  // 9 inside super-cells + 4 standalone
  L.gt_residual = 3;
  return L;
}

namespace {
// The leaf motif's two rectangles relative to a local origin (before orienting).
const Rect kLbase[2] = {{0, 0, 8, 20}, {10, 0, 18, 8}};

// Place the leaf motif transformed by D4 orientation `g`, with its bbox-min at
// (ox, oy) — the same bbox-min convention the recoverer uses.
void place_L_oriented(std::vector<Rect>& out, double ox, double oy, int g) {
  double mnx = 1e300, mny = 1e300;
  Rect tr[2];
  for (int i = 0; i < 2; ++i) {
    double x0, y0, x1, y1;
    apply_pt(g, kLbase[i].x0, kLbase[i].y0, x0, y0);
    apply_pt(g, kLbase[i].x1, kLbase[i].y1, x1, y1);
    tr[i] = {std::min(x0, x1), std::min(y0, y1), std::max(x0, x1), std::max(y0, y1)};
    mnx = std::min(mnx, tr[i].x0); mny = std::min(mny, tr[i].y0);
  }
  for (int i = 0; i < 2; ++i)
    out.push_back({ox + tr[i].x0 - mnx, oy + tr[i].y0 - mny,
                   ox + tr[i].x1 - mnx, oy + tr[i].y1 - mny});
}
}  // namespace

HierLayout oriented_motif_layout() {
  HierLayout L;
  L.name = "oriented_motif";
  // Same motif at irregular positions in 8 different orientations.
  const double at[][3] = {{60, 60, 0},   {200, 80, 4},  {340, 70, 1},
                          {90, 220, 2},  {260, 240, 5}, {420, 210, 3},
                          {150, 380, 6}, {330, 400, 7}};
  for (auto& p : at) place_L_oriented(L.rects, p[0], p[1], (int)p[2]);

  // Clutter → residual.
  L.rects.push_back({250, 150, 268, 166});
  L.rects.push_back({430, 350, 439, 396});

  L.gt_leaf_motif_instances = 8;
  L.gt_super_instances = 0;
  L.gt_residual = 2;
  return L;
}

namespace {
// 4-rectangle motif M relative to a local origin (four members so a single
// missing rectangle is a recognizable defect, not a coincidence).
const Rect kM[4] = {{0, 0, 10, 10}, {12, 0, 18, 20}, {0, 12, 8, 18}, {12, 22, 20, 28}};
void place_M(std::vector<Rect>& out, double ox, double oy, int skip) {
  for (int i = 0; i < 4; ++i)
    if (i != skip)
      out.push_back({ox + kM[i].x0, oy + kM[i].y0, ox + kM[i].x1, oy + kM[i].y1});
}
}  // namespace

HierLayout defective_motif_layout() {
  HierLayout L;
  L.name = "defective_motif";
  const double at[][2] = {{40, 40}, {200, 60}, {120, 200}, {330, 150}, {60, 320}};
  for (int i = 0; i < 5; ++i)
    place_M(L.rects, at[i][0], at[i][1], /*skip member 1 of*/ i == 2 ? 1 : -1);
  // Clutter overlaying nothing critical → residual.
  L.rects.push_back({300, 320, 318, 332});

  L.gt_leaf_motif_instances = 5;  // 4 clean + 1 defective (missing one member)
  L.gt_super_instances = 0;
  L.gt_residual = 1;
  return L;
}

}  // namespace adt::hr
