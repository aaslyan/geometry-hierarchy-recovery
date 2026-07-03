// test_hierarchy.cpp — regression tests for hierarchy recovery: exact flatten
// equality (the hard correctness constraint, companion §6), recovery of a leaf
// motif placed at IRREGULAR positions (beyond array detection), and recovery of
// NESTED structure (a super-cell built from the leaf cell — arrays inside arrays).
#include <cstdio>
#include <vector>

#include "adt/recover.hpp"
#include "adt/synthetic_hier.hpp"

using namespace adt;
using namespace adt::hr;

static int g_failures = 0;
#define CHECK(cond, msg)                                              \
  do {                                                               \
    if (!(cond)) { std::printf("  FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); ++g_failures; } \
    else std::printf("  ok:   %s\n", msg);                          \
  } while (0)

static std::vector<int> usage_counts(const Hierarchy& h) {
  std::vector<int> u(h.cells.size() + 1, 0);
  for (const auto& p : h.top) u[p.cell]++;
  for (int id = (int)h.cells.size(); id >= 1; --id)
    for (const auto& m : h.cells[id - 1].members)
      if (m.cell >= 1) u[m.cell] += u[id];
  return u;
}

int main() {
  HierLayout L = nested_motif_layout();
  Hierarchy h = recover_hierarchy(L.rects);
  auto usage = usage_counts(h);

  std::printf("[hierarchy] nested-motif recovery\n");
  CHECK(h.flatten_matches, "flatten(H) == G exactly (hard correctness constraint)");
  CHECK(h.cells.size() == 2, "exactly two cell definitions recovered (leaf + super)");

  // Cell #1 = leaf motif L: two leaf rectangles, discovered at level 1.
  const CellDef* leaf = nullptr;
  const CellDef* super = nullptr;
  for (const auto& c : h.cells) {
    if (c.level == 1) leaf = &c;
    if (c.level == 2) super = &c;
  }
  CHECK(leaf != nullptr, "a level-1 leaf cell exists");
  CHECK(super != nullptr, "a level-2 nested super-cell exists (arrays inside arrays)");

  if (leaf) {
    bool all_leaf = true;
    for (const auto& m : leaf->members) all_leaf &= (m.cell == 0);
    CHECK(leaf->members.size() == 2 && all_leaf, "leaf cell = 2 raw rectangles");
    CHECK(leaf->leaf_count == 2, "leaf cell flattens to 2 rects");
    CHECK(usage[leaf->id] == L.gt_leaf_motif_instances,
          "leaf motif recovered at all 13 (irregular) placements");
  }
  if (super) {
    bool all_ref_leaf = true;
    for (const auto& m : super->members) all_ref_leaf &= (leaf && m.cell == leaf->id);
    CHECK(super->members.size() == 3 && all_ref_leaf,
          "super-cell = 3 references to the leaf cell (nesting)");
    CHECK(super->leaf_count == 6, "super-cell flattens to 6 rects");
    CHECK(usage[super->id] == L.gt_super_instances,
          "super-cell recovered at all 3 placements");
  }

  CHECK((int)h.residual.size() == L.gt_residual, "3 clutter rects left as residual");
  CHECK(h.hier_cost < h.flat_leaf_count, "hierarchy is smaller than the flat layout");

  // --- Reflection + rotation (D4): one motif in 8 distinct orientations. ------
  std::printf("[hierarchy] mirrored/rotated (D4) recovery\n");
  HierLayout O = oriented_motif_layout();
  Hierarchy ho = recover_hierarchy(O.rects);
  CHECK(ho.flatten_matches, "flatten(H) == G exactly with mixed orientations");
  CHECK(ho.cells.size() == 1, "all orientations collapse to ONE cell definition");
  CHECK((int)ho.top.size() == O.gt_leaf_motif_instances,
        "every oriented instance recovered (8 placements)");
  int distinct_orients = 0, seen[8] = {0};
  for (const auto& p : ho.top)
    if (!seen[p.orient]) { seen[p.orient] = 1; ++distinct_orients; }
  CHECK(distinct_orients == 8, "instances span all 8 D4 orientations, tagged correctly");
  CHECK((int)ho.residual.size() == O.gt_residual, "clutter left as residual");

  // --- Defect tolerance: one instance missing a member (companion §6). --------
  std::printf("[hierarchy] defect-tolerant recovery\n");
  HierLayout D = defective_motif_layout();
  Hierarchy hd = recover_hierarchy(D.rects);
  CHECK(hd.flatten_matches, "G is fully explained: G subset of flatten(H)");
  CHECK(hd.cells.size() == 1, "one motif cell recovered");
  CHECK((int)hd.top.size() == D.gt_leaf_motif_instances,
        "all 5 instances recovered, including the defective one");
  CHECK(hd.n_defective == 1, "exactly one instance flagged defective");
  CHECK(hd.missing_area > 1.0, "the missing member is recorded as defect area");

  std::printf("\n%s (%d failure%s)\n", g_failures == 0 ? "ALL PASSED" : "FAILURES",
              g_failures, g_failures == 1 ? "" : "s");
  return g_failures == 0 ? 0 : 1;
}
