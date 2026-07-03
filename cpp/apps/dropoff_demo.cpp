// dropoff_demo.cpp — the repeat-length drop-off (companion §3) as a controlling
// option. Prints the drop-off curve for a layout, marks the cliff (the natural
// cell scale — "grow the motif until the repeat count suddenly drops"), and then
// compares the two promotion criteria: MDL gain vs. drop-off scale.
#include <algorithm>
#include <cstdio>
#include <vector>

#include "adt/bench.hpp"
#include "adt/recover.hpp"
#include "adt/synthetic_hier.hpp"

using namespace adt;
using namespace adt::hr;

static void bar(int v, int mx) {
  int n = mx ? v * 34 / mx : 0;
  for (int i = 0; i < n; ++i) std::putchar('#');
}

static void analyze(const char* name, const std::vector<Rect>& layout, int gt_scale) {
  std::printf("\n=== %s (%zu rects, true motif ~%d rects) ===\n", name,
              layout.size(), gt_scale);
  DropoffCurve curve = dropoff_curve(layout);
  int maxR = 1;
  for (const auto& p : curve.points) maxR = std::max(maxR, p.distinct_motifs);

  std::printf("repeat-length drop-off (R = distinct recurring motifs of size L):\n");
  for (const auto& p : curve.points) {
    std::printf("  L=%-2d R=%-3d F=%-4d |", p.size, p.distinct_motifs,
                p.total_occurrences);
    bar(p.distinct_motifs, maxR);
    if (p.size == curve.elbow_size) std::printf("  <== drop-off (cell scale)");
    std::printf("\n");
  }
  std::printf("  detected cell scale = %d  (grow until the motif fragments)\n",
              curve.elbow_size);

  std::printf("  selection comparison:\n");
  for (auto sel : {Selection::MDLGain, Selection::DropOff}) {
    RecoverConfig cfg;
    cfg.selection = sel;
    Hierarchy h = recover_hierarchy(layout, cfg);
    int smallest = 1 << 30;
    for (const auto& c : h.cells) smallest = std::min(smallest, c.leaf_count);
    if (h.cells.empty()) smallest = 0;
    std::printf("    %-8s -> %zu cell(s), smallest=%d leaf-rects, %d level(s), "
                "compression %.2fx, flatten=%s\n",
                sel == Selection::MDLGain ? "MDL" : "DropOff", h.cells.size(),
                smallest, h.levels, (double)h.flat_leaf_count / h.hier_cost,
                h.flatten_matches ? "exact" : "NO");
  }
}

int main() {
  analyze("nested motif (irregular placement)", nested_motif_layout().rects, 2);
  analyze("defective motif (irregular placement)", defective_motif_layout().rects, 4);
  analyze("dense datapath array", make_datapath(6, 4, 2, false, 4, false).flat, 4);
  std::printf(
      "\nThe drop-off is a clean cell-scale signal for isolated motifs (surrounded\n"
      "by non-repeating content); on a dense lattice sub-units repeat too, so the\n"
      "signal weakens — exactly why §3 frames it as a scale prior, and why MDL +\n"
      "exact verification remain the default. Both are now selectable knobs.\n");
  return 0;
}
