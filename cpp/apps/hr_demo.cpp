// hr_demo.cpp — recover a hierarchy from a flat, irregularly-placed, nested
// layout and report cells / instances / nesting / compression / flatten-equality.
#include <cstdio>
#include <vector>

#include "adt/hr_svg.hpp"
#include "adt/recover.hpp"
#include "adt/synthetic_hier.hpp"

using namespace adt;
using namespace adt::hr;

// How many times each cell is instantiated in the fully-flattened hierarchy.
static std::vector<int> usage_counts(const Hierarchy& h) {
  std::vector<int> u(h.cells.size() + 1, 0);
  for (const auto& p : h.top) u[p.cell]++;
  for (int id = (int)h.cells.size(); id >= 1; --id)
    for (const auto& m : h.cells[id - 1].members)
      if (m.cell >= 1) u[m.cell] += u[id];
  return u;
}

static void report(const Hierarchy& h, const HierLayout& L);

int main() {
  HierLayout L = nested_motif_layout();
  Hierarchy h = recover_hierarchy(L.rects);
  report(h, L);
  write_hierarchy_svg("hierarchy.svg", h, L.name);
  std::printf("wrote hierarchy.svg\n\n");

  HierLayout O = oriented_motif_layout();
  Hierarchy ho = recover_hierarchy(O.rects);
  report(ho, O);
  write_hierarchy_svg("hierarchy_oriented.svg", ho, O.name);
  std::printf("wrote hierarchy_oriented.svg\n\n");

  HierLayout D = defective_motif_layout();
  Hierarchy hd = recover_hierarchy(D.rects);
  report(hd, D);
  write_hierarchy_svg("hierarchy_defective.svg", hd, D.name);
  std::printf("wrote hierarchy_defective.svg\n");
  return 0;
}

static void report(const Hierarchy& h, const HierLayout& L) {
  auto usage = usage_counts(h);

  std::printf("=== %s (%d leaf rects) ===\n", L.name.c_str(), h.flat_leaf_count);
  std::printf("recovered %zu cell definition(s) across %d level(s):\n",
              h.cells.size(), h.levels);
  for (const auto& c : h.cells) {
    std::printf("  cell #%d (level %d): %zu members, %d leaf rects, %d total instances\n",
                c.id, c.level, c.members.size(), c.leaf_count, usage[c.id]);
    for (const auto& m : c.members) {
      if (m.cell == 0)
        std::printf("      leaf  rect  +(%.0f,%.0f) %.0fx%.0f\n", m.dx, m.dy, m.w, m.h);
      else
        std::printf("      ref   cell#%d +(%.0f,%.0f)\n", m.cell, m.dx, m.dy);
    }
  }
  std::printf("top-level placements: %zu    residual leaf rects: %zu\n",
              h.top.size(), h.residual.size());
  std::printf("compression: flat=%d  ->  hierarchical=%d   (ratio %.2fx)\n",
              h.flat_leaf_count, h.hier_cost,
              (double)h.flat_leaf_count / h.hier_cost);
  std::printf("G explained by flatten(H): %s", h.flatten_matches ? "YES" : "NO");
  if (h.n_defective > 0)
    std::printf("  (%d defective instance(s), defect area %.1f)", h.n_defective,
                h.missing_area);
  std::printf("\n");

  int oc[8] = {0};
  for (const auto& p : h.top) oc[p.orient]++;
  std::printf("top-placement orientations:");
  for (int i = 0; i < 8; ++i) if (oc[i]) std::printf(" o%d=%d", i, oc[i]);
  std::printf("\nground truth: leaf-motif instances=%d, super-cell instances=%d, residual=%d\n",
              L.gt_leaf_motif_instances, L.gt_super_instances, L.gt_residual);
}
