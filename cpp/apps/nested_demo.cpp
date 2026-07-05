// nested_demo.cpp — mixed array/hierarchy extraction (hybrid method).
//
// SEPARATE from erasure_demo: this drives recover_nested(), which promotes the
// base recoverer's flat placement list into a genuinely nested hierarchy
// (gate → slice → block → top) and reports nested compression + gate/slice/block
// agreement, with the same exact flatten==G gate. The base placement-list and
// array-node numbers (erasure_demo) are unchanged.
//
//   nested_demo               run the synthetic datapath suite
//   nested_demo <flat.txt>    run on a flattened rectangle dump (e.g. flat_gds.txt)
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "adt/bench.hpp"
#include "adt/nested.hpp"

using namespace adt::hr;
using adt::Rect;

static void report(const char* label, const Nested& h) {
  std::printf("%-30s flat=%d  base=%d (%.2fx)  nested=%d (%.2fx)  "
              "cells=%zu lvls=%d  slice=%d block=%d  top=%zu resid=%zu  "
              "==base:%s  G⊆:%s  defect_rects=%d\n",
              label, h.flat_leaf_count, h.base_cost,
              h.base_cost ? (double)h.flat_leaf_count / h.base_cost : 0.0,
              h.nested_cost,
              h.nested_cost ? (double)h.flat_leaf_count / h.nested_cost : 0.0,
              h.cells.size(), h.levels, h.n_slice, h.n_block, h.top.size(),
              h.residual.size(), h.matches_base_flatten ? "YES" : "NO",
              h.explains_g ? "YES" : "NO", h.defect_rects);
}

// Print the synthesized cell tree: each intermediate cell as an array reference
// to its child (leaf gate or a lower nested cell).
static void dump_structure(const Nested& h) {
  std::printf("  base leaf cells: %zu (gate = %d rects)\n", h.base.cells.size(),
              h.base.cells.empty() ? 0 : h.base.cells[0].leaf_count);
  for (const auto& c : h.cells) {
    std::printf("  %-6s #%d (lvl %d, %d leaves): ", c.kind.c_str(), c.id, c.level,
                c.leaf_count);
    for (const auto& it : c.body) {
      const char* cn = it.leaf ? "gate" : "cell";
      std::printf("%s%s%d x%d%s  ", it.leaf ? "" : "", cn, it.child, it.n,
                  it.gaps.empty() ? "" : "(+gaps)");
    }
    std::printf("\n");
  }
}

static std::vector<Rect> read_flat(const std::string& path, std::string& header) {
  std::vector<Rect> rects;
  std::ifstream in(path);
  if (!in) return rects;
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    if (line[0] == '#') { if (header.empty()) header = line; continue; }
    std::istringstream ss(line);
    Rect r;
    if (ss >> r.x0 >> r.y0 >> r.x1 >> r.y1) rects.push_back(r);
  }
  return rects;
}

int main(int argc, char** argv) {
  if (argc > 1) {
    std::string header;
    std::vector<Rect> rects = read_flat(argv[1], header);
    if (rects.empty()) {
      std::fprintf(stderr, "no rectangles read from %s\n", argv[1]);
      return 1;
    }
    // Real PDK geometry lives at a different scale than the synthetic integer
    // datapaths, so allow the two scale-sensitive knobs to be overridden:
    //   nested_demo <flat.txt> [quantum] [grow_radius]
    // quantum   — coordinate/shape snap (must resolve real feature sizes);
    // grow_radius — seed-and-extend reach (should span one repeated tile).
    RecoverConfig cfg;
    if (argc > 2) cfg.quantum = std::atof(argv[2]);
    if (argc > 3) cfg.grow_radius = std::atof(argv[3]);
    std::printf("mixed array/hierarchy extraction — real flattened GDS\n");
    if (!header.empty()) std::printf("%s\n", header.c_str());
    std::printf("flattened: %zu rectangles  (quantum=%.4g grow_radius=%.4g)\n\n",
                rects.size(), cfg.quantum, cfg.grow_radius);
    Nested h = recover_nested(rects, cfg);
    report(argv[1], h);
    std::printf("\n");
    dump_structure(h);
    std::ofstream js("recovered_nested.json");
    js << nested_to_json(h);
    std::printf("\nwrote recovered_nested.json\n");
    return h.matches_base_flatten ? 0 : 2;
  }

  struct Case { const char* label; int g, r, b; bool mirror; int clutter; };
  Case cases[] = {
      {"flat array (1 block)",         6, 4, 1, false, 0},
      {"nested x2 blocks",             6, 4, 2, false, 4},
      {"nested + mirrored rows",       6, 5, 2, true,  6},
      {"larger + mirrored + clutter",  8, 6, 3, true, 12},
  };

  std::printf("mixed array/hierarchy extraction — promote flat placements into "
              "nested gate->slice->block->top cells\n\n");
  for (const auto& c : cases) {
    ErasureDesign d = make_datapath(c.g, c.r, c.b, c.mirror, c.clutter, false);
    Nested h = recover_nested(d.flat);
    report(c.label, h);
  }
  std::printf("\nNested cost = base leaf-cell bodies + Σ array-item costs + top "
              "instances + residual; each array item costs one reference plus one\n"
              "exception per missing site. ==base: flatten(nested) reproduces the "
              "base recoverer's geometry exactly; G⊆: every input rectangle is\n"
              "explained (== G exactly when defect_rects=0; on a tolerated defect "
              "the surplus is the idealized missing member). Agreement, not\n"
              "accuracy (§1.3): a gate dropped to residual by base recovery yields "
              "a shorter slice/block variant rather than a forced match.\n");
  std::printf("\nStructure of the last case:\n");
  ErasureDesign d = make_datapath(8, 6, 3, true, 12, false);
  dump_structure(recover_nested(d.flat));
  return 0;
}
