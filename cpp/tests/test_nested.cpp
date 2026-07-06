// test_nested.cpp — mixed array/hierarchy extraction invariants (hybrid method).
//
// Two kinds of check:
//   (A) GENERAL invariants over a range of datapaths — the nested view reproduces
//       the base recoverer's geometry exactly, explains all input geometry, and
//       compresses better than the base placement list.
//   (B) SOURCE-AWARE agreement on a clean synthetic datapath with KNOWN structure
//       (gate pitch/count, block row count/pitch, block count) — proving we
//       recovered the designer's gate→slice→block→top, not just *some* pair/group
//       cells that happen to compress. This is the reviewer's ask: agreement
//       against ground-truth structure, not more compression numbers.
#include <cmath>
#include <cstdio>
#include <vector>

#include "adt/bench.hpp"
#include "adt/lattice.hpp"
#include "adt/nested.hpp"
#include "adt/recover.hpp"

using namespace adt::hr;
using adt::Rect;

static int g_failures = 0;
#define CHECK(cond, msg)                                                     \
  do {                                                                       \
    if (!(cond)) { std::printf("  FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); ++g_failures; } \
    else std::printf("  ok:   %s\n", msg);                                   \
  } while (0)

// --- Source-aware structural queries ----------------------------------------
// A "slice" is a cell whose body is a single array of the leaf gate along X.
static bool has_slice(const Nested& h, int gate_count, double pitch) {
  for (const auto& c : h.cells) {
    if (c.body.size() != 1) continue;
    const NItem& it = c.body[0];
    if (it.leaf && it.n == gate_count && it.gaps.empty() &&
        std::abs(it.sx - pitch) < 0.5 && std::abs(it.sy) < 0.5)
      return true;
  }
  return false;
}
// A "block" is a cell whose body is a single array of a SLICE cell along Y.
static bool has_block(const Nested& h, int row_count, double pitch) {
  for (const auto& c : h.cells) {
    if (c.body.size() != 1) continue;
    const NItem& it = c.body[0];
    if (it.leaf || it.n != row_count || !it.gaps.empty()) continue;
    if (std::abs(it.sy - pitch) > 0.5 || std::abs(it.sx) > 0.5) continue;
    const NCell& child = h.cells[it.child - 1];
    if (child.body.size() == 1 && child.body[0].leaf) return true;  // child is a slice
  }
  return false;
}
// A "top" is a cell whose body is a single array of a BLOCK cell along X.
static bool has_top(const Nested& h, int block_count) {
  for (const auto& c : h.cells) {
    if (c.body.size() != 1) continue;
    const NItem& it = c.body[0];
    if (it.leaf || it.n != block_count) continue;
    if (std::abs(it.sx) < 0.5 || std::abs(it.sy) > 0.5) continue;  // horizontal run
    const NCell& child = h.cells[it.child - 1];
    if (child.body.size() != 1 || child.body[0].leaf) continue;    // child is a block
    const NCell& gc = h.cells[child.body[0].child - 1];
    if (gc.body.size() == 1 && gc.body[0].leaf) return true;       // grandchild is a slice
  }
  return false;
}

int main() {
  const double gate_pitch = 22, row_pitch = 26;  // must match bench.cpp make_datapath

  // ---- (A) General invariants ----------------------------------------------
  std::printf("[nested] general invariants\n");
  struct C { const char* l; int g, r, b; bool m; int c; bool defect; };
  C cs[] = {
      {"flat array",      6, 4, 1, false, 0,  false},
      {"nested",          6, 4, 2, false, 4,  false},
      {"mirrored",        6, 5, 2, true,  6,  false},
      {"mirror+clutter",  8, 6, 3, true,  12, false},
      {"defective",       6, 5, 2, true,  6,  true},
  };
  for (const auto& c : cs) {
    ErasureDesign d = make_datapath(c.g, c.r, c.b, c.m, c.c, c.defect);
    Nested h = recover_nested(d.flat);
    char buf[160];

    // The nested view must reproduce the base recoverer's own geometry exactly.
    std::snprintf(buf, sizeof buf, "%s: flatten(nested) == flatten(base) (exact)", c.l);
    CHECK(h.matches_base_flatten, buf);

    // And it must explain every input rectangle (G subset flatten(nested)).
    std::snprintf(buf, sizeof buf, "%s: G subset flatten(nested)", c.l);
    CHECK(h.explains_g, buf);

    // Clean layouts flatten exactly to G; a tolerated defect leaves the idealized
    // missing member as the surplus defect layer (defect_rects > 0).
    if (c.defect) {
      std::snprintf(buf, sizeof buf, "%s: defect surplus present (defect_rects>0)", c.l);
      CHECK(h.defect_rects > 0, buf);
      std::snprintf(buf, sizeof buf, "%s: defect recorded by base (n_defective>0)", c.l);
      CHECK(h.base.n_defective > 0, buf);
    } else {
      std::snprintf(buf, sizeof buf, "%s: flatten(nested) == G exactly (no surplus)", c.l);
      CHECK(h.defect_rects == 0, buf);
    }

    std::snprintf(buf, sizeof buf, "%s: leaf gate recovered (4-rect cell)", c.l);
    bool gate = !h.base.cells.empty() && h.base.cells[0].leaf_count == d.gt_leaf_rects;
    CHECK(gate, buf);

    std::snprintf(buf, sizeof buf, "%s: nested cost beats base placement-list", c.l);
    CHECK(h.nested_cost < h.base_cost, buf);
  }

  // ---- (B) Source-aware agreement on a clean, known datapath ----------------
  // 6 gates/slice, 4 slices/block, 3 blocks — no mirror, no clutter, no defect,
  // so the recovered structure should match the designer's construction exactly.
  std::printf("\n[nested] source-aware agreement (clean 6x4x3 datapath)\n");
  {
    const int NG = 6, NR = 4, NB = 3;
    ErasureDesign d = make_datapath(NG, NR, NB, false, 0, false);
    Nested h = recover_nested(d.flat);

    CHECK(h.matches_base_flatten && h.defect_rects == 0,
          "clean: flatten(nested) == G exactly");
    CHECK(h.levels == 4, "clean: recovered 4 levels (gate/slice/block/top)");

    char buf[160];
    std::snprintf(buf, sizeof buf,
                  "clean: SLICE recovered = gate x%d at pitch %.0f", NG, gate_pitch);
    CHECK(has_slice(h, NG, gate_pitch), buf);

    std::snprintf(buf, sizeof buf,
                  "clean: BLOCK recovered = slice x%d at pitch %.0f", NR, row_pitch);
    CHECK(has_block(h, NR, row_pitch), buf);

    std::snprintf(buf, sizeof buf, "clean: TOP recovered = block x%d", NB);
    CHECK(has_top(h, NB), buf);

    // Reject the degenerate "any pair compresses" reading: no run shorter than the
    // true gate row should masquerade as the slice. The recovered slice must span
    // the full gate count, and the block the full row count (checked above), so a
    // 2-gate or 2-slice coincidence cell alone would NOT satisfy these.
    CHECK(!has_slice(h, 2, gate_pitch) || NG == 2,
          "clean: no spurious 2-gate slice stands in for the real one");
  }

  // ---- (C) Lattice-prior dense 2D array (the 2D fix) ------------------------
  // A dense array whose motif ABUTS its cell (a full-width and full-height bar
  // reach the cell edge, extent == pitch). Radius-based seed-and-grow reaches the
  // neighbour from any seed; the lattice prior assigns by anchor-modulo and
  // recovers the tile exactly. This directly exercises recover_lattice.
  std::printf("\n[nested] lattice-prior dense 2D array\n");
  {
    const std::vector<Rect> motif = {
        {0, 0, 10, 2},   // full-width bottom bar (x1 == pitch)
        {0, 3, 2, 10},   // full-height left bar (y1 == pitch)
        {3, 4, 8, 9},    // interior block
        {4, 2, 9, 3},    // small distinct rect
    };
    const int COLS = 6, ROWS = 5;
    const double px = 10, py = 10;
    std::vector<Rect> L;
    for (int i = 0; i < COLS; ++i)
      for (int j = 0; j < ROWS; ++j)
        for (const auto& m : motif)
          L.push_back({m.x0 + i * px, m.y0 + j * py, m.x1 + i * px, m.y1 + j * py});

    LatticeInfo info;
    Hierarchy h = recover_lattice(L, {}, &info);
    CHECK(info.ok, "lattice: found a lattice on the dense abutting array");
    CHECK(std::abs(info.dx - px) < 0.6 && std::abs(info.dy - py) < 0.6,
          "lattice: recovered dx,dy == pitch");
    CHECK(info.tile_members == (int)motif.size(), "lattice: tile == one motif");
    CHECK(info.placements == COLS * ROWS, "lattice: one placement per cell");
    CHECK(info.residual == 0, "lattice: no residual on a perfect array");
    CHECK(flatten(h).size() == L.size(), "lattice: flatten(H) reproduces |G| exactly");

    // End-to-end: recover_nested falls back to the lattice prior and nests it.
    Nested nh = recover_nested(L);
    CHECK(nh.matches_base_flatten && nh.explains_g && nh.defect_rects == 0,
          "lattice+nested: flatten(nested) == G exact");
    CHECK(nh.nested_cost < nh.flat_leaf_count, "lattice+nested: compresses the array");
  }

  std::printf("\n%s (%d failure%s)\n", g_failures == 0 ? "ALL PASSED" : "FAILURES",
              g_failures, g_failures == 1 ? "" : "s");
  return g_failures == 0 ? 0 : 1;
}
