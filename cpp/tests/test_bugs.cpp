// test_bugs.cpp — regression tests for the three bugs documented in CLAUDE.md,
// plus a test that the edge-boundary encoding recovers zero-gap abutted geometry
// (the prototype's "known gap"). Minimal self-contained harness, no gtest.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "adt/detector.hpp"
#include "adt/encoding.hpp"
#include "adt/runs.hpp"
#include "adt/slab.hpp"
#include "adt/synthetic.hpp"

using namespace adt;

static int g_failures = 0;
#define CHECK(cond, msg)                                                    \
  do {                                                                      \
    if (!(cond)) {                                                          \
      std::printf("  FAIL: %s  (%s:%d)\n", msg, __FILE__, __LINE__);        \
      ++g_failures;                                                         \
    } else {                                                                \
      std::printf("  ok:   %s\n", msg);                                     \
    }                                                                       \
  } while (0)

// Build a rectangular grid of `cell_w`x`cell_h` tiles on pitch (px,py), with an
// optional set of (col,row) cells omitted.
static std::vector<Rect> grid(int cols, int rows, double cell_w, double cell_h,
                              double px, double py, double ox = 0, double oy = 0,
                              std::vector<std::pair<int, int>> skip = {}) {
  std::vector<Rect> r;
  for (int i = 0; i < cols; ++i)
    for (int j = 0; j < rows; ++j) {
      bool skipped = false;
      for (auto& s : skip)
        if (s.first == i && s.second == j) skipped = true;
      if (skipped) continue;
      double x = ox + i * px, y = oy + j * py;
      r.push_back({x, y, x + cell_w, y + cell_h});
    }
  return r;
}

// ---------------------------------------------------------------------------
// Bug 1: a piece of non-repeating clutter has, by coincidence, the exact same
// width as a real repeated token. It must not corrupt period detection, and
// must be excluded from the array region.
// ---------------------------------------------------------------------------
static void test_bug1_symbol_collision() {
  std::printf("[bug1] symbol collision between unrelated content\n");
  // Real tiles are 10 wide -> occupancy token width 10. Add a clutter rect of
  // width EXACTLY 10, off-lattice to the left of row 0, with an irregular gap
  // (25) to the array so it can't extend the period.
  auto rects = grid(8, 6, 10.0, 10.0, 20.0, 20.0);
  rects.push_back({-35.0, 0.0, -25.0, 10.0});  // width 10 == real token width

  auto res = detect_arrays(rects);
  CHECK(res.n_verified == 1, "exactly one array detected");
  if (res.n_verified >= 1) {
    const auto& c = res.arrays[0];
    CHECK(std::abs(c.dx - 20.0) < 1.0, "dx == true period 20 (not corrupted)");
    CHECK(c.n_cols == 8 && c.n_rows == 6, "grid 8x6 recovered");
    CHECK(c.bbox.x0 > -5.0, "colliding clutter excluded from region (x0 ~ 0)");
    CHECK(c.n_clean == 48, "all 48 instances clean; clutter not counted");
  }
}

// ---------------------------------------------------------------------------
// Bug 2: cell widths [12,18,12,24] — two coincidentally equal — must NOT yield a
// spurious half-period. The true primitive (full 4-cell) period must be found.
// ---------------------------------------------------------------------------
static void test_bug2_spurious_subperiod() {
  std::printf("[bug2] degenerate widths -> spurious sub-period\n");
  const double widths[4] = {12, 18, 12, 24};  // widths[0]==widths[2]
  const double gap = 3.0, row_h = 10.0, py = 16.0;
  const int patterns = 6, rows = 6;
  double period = 0;
  for (double w : widths) period += w + gap;  // 66 + 12 = 78

  std::vector<Rect> rects;
  for (int j = 0; j < rows; ++j) {
    double x = 0, y = j * py;
    for (int p = 0; p < patterns; ++p)
      for (int k = 0; k < 4; ++k) {
        rects.push_back({x, y, x + widths[k], y + row_h});
        x += widths[k] + gap;
      }
  }

  auto res = detect_arrays(rects);
  CHECK(res.n_verified >= 1, "an array detected");
  if (res.n_verified >= 1) {
    const auto& c = res.arrays[0];
    CHECK(std::abs(c.dx - period) < 2.0,
          "dx == full primitive period ~78 (not the ~39 half-period)");
    CHECK(c.dx > 60.0, "dx is NOT a spurious sub-period");
    CHECK(c.n_cols == patterns, "column count matches full-period count");
  }
}

// ---------------------------------------------------------------------------
// Bug 3: a genuine missing instance in the middle of the grid. Defects after the
// hole must be attributed to the correct physical x, not shifted, and no false
// defects may appear downstream of the hole.
// ---------------------------------------------------------------------------
static void test_bug3_defect_phase() {
  std::printf("[bug3] phase attribution across a mid-grid defect\n");
  // 8x6 grid, tile 10 wide on pitch 20. Remove cell (col=3, row=2).
  auto rects = grid(8, 6, 10.0, 10.0, 20.0, 20.0, 0, 0, {{3, 2}});

  auto res = detect_arrays(rects);
  CHECK(res.n_verified == 1, "array still detected despite the hole");
  if (res.n_verified >= 1) {
    const auto& c = res.arrays[0];
    CHECK(c.n_instances == 48, "48 expected instances");
    CHECK(c.n_clean == 47, "exactly 47 clean (no desync-induced false defects)");
    CHECK(c.defects.size() == 1, "exactly one defect reported");
    if (c.defects.size() == 1) {
      CHECK(c.defects[0].type == DefectType::Missing, "defect is MISSING type");
      // The defect must be attributed to the correct INSTANCE (col 3, row 2) —
      // i.e. fall inside that lattice cell — not shifted to a later column by a
      // token-index desync. Lattice cell 3 spans x∈(60,80), row 2 y∈(40,60).
      double x = c.defects[0].x, y = c.defects[0].y;
      int col = static_cast<int>((x - c.bbox.x0) / c.dx);
      int row = static_cast<int>((y - c.bbox.y0) / c.dy);
      CHECK(col == 3, "defect attributed to col 3 (not shifted right)");
      CHECK(row == 2, "defect attributed to row 2");
    }
  }
}

// ---------------------------------------------------------------------------
// Bug 3, direct: exercise analyze_row's expected-physical-position scan, which
// is where the original desync bug lived. A row of 8 tiles with the middle one
// (index 3) missing must report its single defect at the correct physical x, and
// nothing after it may desync.
// ---------------------------------------------------------------------------
static void test_bug3_row_scan_direct() {
  std::printf("[bug3-direct] analyze_row expected-position scan\n");
  Slab slab;
  slab.y0 = 0; slab.y1 = 10; slab.y_mid = 5;
  for (int i = 0; i < 8; ++i) {
    if (i == 3) continue;  // middle instance missing
    slab.covered.push_back({i * 20.0, i * 20.0 + 10.0});
    slab.spans.push_back({i * 20.0, i * 20.0 + 10.0});
    slab.span_heights.push_back(10.0);
  }
  TokenStream ts = encode_occupancy(slab);
  auto run = analyze_row(0, slab, ts, slab.covered);
  CHECK(run.has_value(), "period found for a row with a mid-row hole");
  if (run) {
    CHECK(std::abs(run->dx - 20.0) < 0.6, "physical period dx == 20");
    CHECK(run->n_expected == 8, "8 expected positions across the row");
    CHECK(run->defect_positions.size() == 1, "exactly one defect position");
    if (run->defect_positions.size() == 1)
      CHECK(std::abs(run->defect_positions[0] - 60.0) < 0.6,
            "defect at physical x=60 (start of instance 3), not shifted");
  }
}

// ---------------------------------------------------------------------------
// Closed "known gap": directly-abutted (zero-gap) cells. Occupancy merges each
// row into one band and loses all structure; the edge-boundary encoding must
// still recover the array.
// ---------------------------------------------------------------------------
static void test_abutted_edge_encoding() {
  std::printf("[gap] zero-gap abutted cells recovered via edge encoding\n");
  auto L = standard_cell_abutted_layout();
  auto res = detect_arrays(L.rects);
  CHECK(res.n_verified >= 1, "abutted standard-cell array detected");
}

int main() {
  test_bug1_symbol_collision();
  test_bug2_spurious_subperiod();
  test_bug3_defect_phase();
  test_bug3_row_scan_direct();
  test_abutted_edge_encoding();
  std::printf("\n%s (%d failure%s)\n", g_failures == 0 ? "ALL PASSED" : "FAILURES",
              g_failures, g_failures == 1 ? "" : "s");
  return g_failures == 0 ? 0 : 1;
}
