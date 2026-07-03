// d4.hpp — the dihedral group D4 (8 axis-preserving orientations) for hierarchy
// recovery under reflection + rotation (companion proposal §1.2 / §5.2).
//
// For rectilinear (Manhattan) geometry the only rigid transforms that keep
// rectangles axis-aligned are the 8 elements of D4: rotations by 0/90/180/270°
// each optionally composed with a mirror — exactly the GDS orientation set
// (angle 0/90/180/270 × mirror-about-X). An instance therefore carries an
// orientation index 0..7 in addition to its position.
#pragma once

#include <algorithm>
#include <cmath>

#include "adt/hierarchy.hpp"

namespace adt::hr {

// (x,y) -> (a*x + b*y, c*x + d*y). The 8 signed permutation matrices.
struct Mat { int a, b, c, d; };
inline const Mat kD4[8] = {
    {1, 0, 0, 1},    // 0: identity
    {0, -1, 1, 0},   // 1: rot90 ccw
    {-1, 0, 0, -1},  // 2: rot180
    {0, 1, -1, 0},   // 3: rot270 ccw
    {-1, 0, 0, 1},   // 4: mirror across y-axis (flip x)
    {1, 0, 0, -1},   // 5: mirror across x-axis (flip y)
    {0, 1, 1, 0},    // 6: reflect across main diagonal (transpose)
    {0, -1, -1, 0},  // 7: reflect across anti-diagonal
};

inline void apply_pt(int g, double x, double y, double& ox, double& oy) {
  const Mat& m = kD4[g];
  ox = m.a * x + m.b * y;
  oy = m.c * x + m.d * y;
}

// Group composition: index of kD4[g] * kD4[h] (apply h then g).
inline int compose(int g, int h) {
  const Mat& A = kD4[g];
  const Mat& B = kD4[h];
  Mat M{A.a * B.a + A.b * B.c, A.a * B.b + A.b * B.d,
        A.c * B.a + A.d * B.c, A.c * B.b + A.d * B.d};
  for (int i = 0; i < 8; ++i)
    if (kD4[i].a == M.a && kD4[i].b == M.b && kD4[i].c == M.c && kD4[i].d == M.d)
      return i;
  return 0;
}

// Transform a member rectangle by g. For an axis-aligned rect the transform maps
// it to another axis-aligned rect; we return the new min-corner offset and dims
// (w/h swap under the rotations/diagonal reflections). A sub-cell reference's own
// orientation composes with g so nested orientation propagates correctly.
inline Member xform_member(const Member& m, int g) {
  double x0, y0, x1, y1;
  apply_pt(g, m.dx, m.dy, x0, y0);
  apply_pt(g, m.dx + m.w, m.dy + m.h, x1, y1);
  Member r;
  r.cell = m.cell;
  r.dx = std::min(x0, x1);
  r.dy = std::min(y0, y1);
  r.w = std::abs(x1 - x0);
  r.h = std::abs(y1 - y0);
  r.orient = (m.cell == 0) ? 0 : compose(g, m.orient);
  return r;
}

}  // namespace adt::hr
