// erasure_demo.cpp — run the hierarchy-erasure benchmark (companion §7.1) over a
// small suite of synthetic datapaths and report the §7.4 metrics.
#include <cstdio>

#include "adt/bench.hpp"

using namespace adt::hr;

int main() {
  struct Case { const char* label; int g, r, b; bool mirror; int clutter; };
  Case cases[] = {
      {"flat array (1 block)",     6, 4, 1, false, 0},
      {"nested x2 blocks",         6, 4, 2, false, 4},
      {"nested + mirrored rows",   6, 5, 2, true,  6},
      {"larger + mirrored + clutter", 8, 6, 3, true, 12},
  };

  std::printf("hierarchy-erasure benchmark — build known hierarchy, flatten, "
              "strip, recover, score\n\n");
  std::printf("%-30s %6s %5s %5s %6s %5s %5s %6s %8s %8s  %s\n", "case",
              "rects", "cells", "lvls", "insts", "array", "resid", "defct",
              "hier", "+array", "G⊆flat / leaf");
  for (const auto& c : cases) {
    ErasureDesign d = make_datapath(c.g, c.r, c.b, c.mirror, c.clutter, false);
    ErasureResult res = run_erasure(d);
    std::printf("%-30s %6zu %5d %5d %6d %5d %5d %6d %7.2fx %7.2fx  %s / %s\n",
                c.label, d.flat.size(), res.cells, res.levels, res.top, res.arrays,
                res.residual, res.n_defective, res.compression, res.array_compression,
                res.flatten_ok ? "YES" : "NO ", res.leaf_recovered ? "found" : "-");
  }
  std::printf("\nGround truth per case is the design's own construction "
              "(gate→slice→block→chip). Recovered hierarchies are compact and\n"
              "geometrically exact (G⊆flatten), but need not match the designer's "
              "decomposition — agreement, not accuracy (§1.3). The +array column\n"
              "charges regular top-level placement grids as O(1) array nodes.\n");
  return 0;
}
