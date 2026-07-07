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

static int analyze(const char* name, const std::vector<Rect>& layout, int gt_scale) {
  std::printf("\n=== %s (%zu rects, true motif ~%d rects) ===\n", name,
              layout.size(), gt_scale);
  DropoffCurve curve = dropoff_curve(layout);
  double maxS = 1e-9;
  for (const auto& p : curve.points) maxS = std::max(maxS, p.support);

  // Support = F/R = mean copies per surviving motif. Grow the motif: while ONE
  // pattern still explains ~every instance, support stays high; the moment the
  // neighborhood grows past the cell and fragments, support falls off a cliff.
  std::printf("support = mean copies per surviving motif  (grow, grow, ... DROP):\n");
  std::printf("  %-3s %4s %4s %8s\n", "L", "R", "F", "support");
  for (const auto& p : curve.points) {
    std::printf("  %-3d %4d %4d %8.1f  |", p.size, p.distinct_motifs,
                p.total_occurrences, p.support);
    bar((int)std::lround(p.support * 10), (int)std::lround(maxS * 10));
    if (p.size == curve.elbow_size) std::printf("  <== cell scale (top of the cliff)");
    std::printf("\n");
  }
  std::printf("  detected cell scale = %d rects  (support drops %.1fx right after; "
              "truth ~%d)\n", curve.elbow_size, curve.drop_ratio, gt_scale);

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
  return curve.elbow_size == gt_scale ? 0 : 1;  // the cliff must find the true cell scale
}

// Size as an indication: two motifs with identical ARRANGEMENT but a different
// rectangle size. In the symbol-plus-size string they are distinct; strip the
// size and they collapse into one — showing the physical size is what keeps the
// string a faithful proxy for geometry, not a symbolic coincidence (§5.1, bug 1).
static int size_role() {
  std::vector<Rect> L;
  auto place = [&](double x, double y, double w2) {
    L.push_back({x, y, x + 4, y + 4});          // first rect: always 4x4
    L.push_back({x + 6, y, x + 6 + w2, y + 4});  // second rect: width varies
  };
  for (int i = 0; i < 4; ++i) place(i * 40, 0, 4);    // motif A: second rect 4 wide
  for (int i = 0; i < 4; ++i) place(i * 40, 40, 8);   // motif B: second rect 8 wide

  std::printf("\n=== size as an indication (4 copies of motif A + 4 of motif B) ===\n");
  std::printf("A and B share the same arrangement; only B's second rectangle is wider.\n");
  int aware_R = 0, blind_R = 0;
  for (bool blind : {false, true}) {
    RecoverConfig cfg;
    cfg.size_blind = blind;
    DropoffCurve c = dropoff_curve(L, cfg);
    for (const auto& p : c.points)
      if (p.size == 2) {
        (blind ? blind_R : aware_R) = p.distinct_motifs;
        std::printf("  %-11s L=2: R=%d distinct motif(s), support=%.1f\n",
                    blind ? "size-BLIND" : "size-aware", p.distinct_motifs, p.support);
      }
  }
  std::printf("  --> size-aware keeps A and B apart (R=2); size-blind merges them (R=1,\n"
              "      support inflated). Size in the token is what makes the string a\n"
              "      faithful proxy for geometry, not a symbol-only coincidence.\n");
  return (aware_R == 2 && blind_R == 1) ? 0 : 1;  // size must separate; blindness must merge
}

int main() {
  int bad = 0;
  bad += analyze("nested motif (irregular placement)", nested_motif_layout().rects, 2);
  bad += analyze("defective motif (irregular placement)", defective_motif_layout().rects, 4);
  bad += analyze("dense datapath array", make_datapath(6, 4, 2, false, 4, false).flat, 4);
  bad += size_role();
  std::printf(
      "\nThe drop-off is a clean cell-scale signal for isolated motifs (surrounded\n"
      "by non-repeating content); on a dense lattice sub-units repeat too, so the\n"
      "signal weakens — exactly why §3 frames it as a scale prior, and why MDL +\n"
      "exact verification remain the default. Both are now selectable knobs.\n");
  if (bad) std::printf("\n**FAILURE**: %d drop-off check(s) did not match expectation\n", bad);
  return bad ? 1 : 0;
}
