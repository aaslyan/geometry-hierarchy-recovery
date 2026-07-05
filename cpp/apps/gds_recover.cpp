// gds_recover.cpp — recover a hierarchy from GDS geometry that was flattened and
// stripped of metadata (companion proposal 7.1, real-GDS path). Reads the
// rectangle dump produced by scripts/gds_roundtrip.py, recovers, and scores
// against the ground truth carried in the file header.
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "adt/hr_svg.hpp"
#include "adt/recover.hpp"

using namespace adt;
using namespace adt::hr;

int main(int argc, char** argv) {
  std::string path = argc > 1 ? argv[1] : "flat_gds.txt";
  std::ifstream in(path);
  if (!in) { std::fprintf(stderr, "cannot open %s\n", path.c_str()); return 1; }

  int gt_cells = 0, gt_gate_rects = 0, gt_instances = 0, gt_levels = 0;
  std::vector<Rect> rects;
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    if (line[0] == '#') {
      // parse "key=value" tokens from the ground-truth header
      std::istringstream ss(line);
      std::string tok;
      while (ss >> tok) {
        auto eq = tok.find('=');
        if (eq == std::string::npos) continue;
        std::string k = tok.substr(0, eq);
        int v = std::atoi(tok.substr(eq + 1).c_str());
        if (k == "cells") gt_cells = v;
        else if (k == "gate_rects") gt_gate_rects = v;
        else if (k == "gate_instances") gt_instances = v;
        else if (k == "levels") gt_levels = v;
      }
      continue;
    }
    std::istringstream ss(line);
    Rect r;
    if (ss >> r.x0 >> r.y0 >> r.x1 >> r.y1) rects.push_back(r);
  }

  RecoverConfig cfg;
  if (argc > 2) cfg.max_levels = std::atoi(argv[2]);
  Hierarchy h = recover_hierarchy(rects, cfg);

  // How many times each cell is instantiated in the flattened hierarchy.
  std::vector<int> usage(h.cells.size() + 1, 0);
  for (const auto& p : h.top) usage[p.cell]++;
  for (int id = (int)h.cells.size(); id >= 1; --id)
    for (const auto& m : h.cells[id - 1].members)
      if (m.cell >= 1) usage[m.cell] += usage[id];

  int gate_like = 0, gate_usage = 0;
  for (const auto& c : h.cells)
    if (c.leaf_count == gt_gate_rects) { gate_like++; gate_usage += usage[c.id]; }

  std::printf("=== real-GDS hierarchy-erasure recovery ===\n");
  std::printf("flattened GDS: %zu rectangles\n", rects.size());
  if (gt_cells || gt_gate_rects || gt_instances || gt_levels)
    std::printf("GROUND TRUTH : %d cells, gate=%d rects x %d instances, %d levels\n",
                gt_cells, gt_gate_rects, gt_instances, gt_levels);
  else
    std::printf("SOURCE       : no synthetic ground-truth header; reporting recovery only\n");
  std::printf("RECOVERED    : %zu cells across %d levels, %zu top placements, "
              "%zu residual\n",
              h.cells.size(), h.levels, h.top.size(), h.residual.size());
  std::printf("               compression %.2fx (flat %d -> hier %d)\n",
              (double)h.flat_leaf_count / h.hier_cost, h.flat_leaf_count, h.hier_cost);
  std::printf("               array-node compression %.2fx (flat %d -> array-hier %d, "
              "%zu array node%s)\n",
              (double)h.flat_leaf_count / h.array_cost, h.flat_leaf_count, h.array_cost,
              h.arrays.size(), h.arrays.size() == 1 ? "" : "s");
  std::printf("               G subset of flatten(H): %s%s\n",
              h.flatten_matches ? "YES (exact)" : "NO",
              h.n_defective ? "  (defects present)" : "");
  if (gt_gate_rects) {
    std::printf("AGREEMENT    : gate-sized cell (%d rects) recovered: %s",
                gt_gate_rects, gate_like ? "YES" : "no");
    if (gate_like)
      std::printf("  (%d instances vs %d in ground truth)", gate_usage, gt_instances);
    std::printf("\n(recovered hierarchy is a compact, geometrically exact "
                "decomposition; agreement, not accuracy -- HR 1.3)\n");
  } else {
    std::printf("RECOVERY     : compact, geometrically exact decomposition inferred "
                "from flat rectangles\n");
  }

  write_hierarchy_svg("hierarchy_gds.svg", h, "gds_erasure");
  std::printf("wrote hierarchy_gds.svg\n");
  return 0;
}
