// nonuniqueness_demo.cpp — non-uniqueness of hierarchy, demonstrated (§1.3, §2.4).
//
// A flat set of rectangles admits MANY geometrically valid hierarchies: different
// cell decompositions that all flatten back to exactly the same geometry. This
// demo makes that concrete instead of asserting it. Part 1 builds, by hand, the
// whole family of valid hierarchies for one row of repeated motifs (group the unit
// in 1s, 2s, 3s, …, and nest them) and verifies every one flattens to G exactly.
// Part 2 shows the *engine* landing on different valid hierarchies for one real
// datapath depending only on the objective/knobs. The point: "which hierarchy" is
// a choice governed by a stated cost, not a unique truth — agreement, not accuracy.
#include <algorithm>
#include <array>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "adt/bench.hpp"
#include "adt/hierarchy.hpp"
#include "adt/lattice.hpp"
#include "adt/nested.hpp"
#include "adt/recover.hpp"

using namespace adt::hr;
using adt::Rect;

static bool multiset_equal(const std::vector<Rect>& a, const std::vector<Rect>& b) {
  if (a.size() != b.size()) return false;
  auto key = [](const Rect& r) {
    auto q = [](double v) { return (long)std::lround(v * 2); };  // 0.5 quantum
    return std::array<long, 4>{q(r.x0), q(r.y0), q(r.x1), q(r.y1)};
  };
  std::map<std::array<long, 4>, int> c;
  for (const auto& r : a) c[key(r)]++;
  for (const auto& r : b) { auto it = c.find(key(r)); if (it == c.end() || !it->second) return false; it->second--; }
  for (auto& kv : c) if (kv.second) return false;
  return true;
}

static int cost_of(const Hierarchy& h) {
  int c = (int)h.top.size() + (int)h.residual.size();
  for (const auto& cell : h.cells) c += (int)cell.members.size();
  return c;
}

// The flat geometry G: K copies of `unit` in a row at pitch p.
static std::vector<Rect> row_layout(const std::vector<Rect>& unit, int K, double p) {
  std::vector<Rect> g;
  for (int i = 0; i < K; ++i)
    for (const auto& r : unit)
      g.push_back({r.x0 + i * p, r.y0, r.x1 + i * p, r.y1});
  return g;
}

// One-level hierarchy: a cell of `g` units, instantiated K/g times.
static Hierarchy grouped(const std::vector<Rect>& unit, int K, int g, double p) {
  Hierarchy h;
  CellDef c; c.id = 1; c.level = 1;
  for (int u = 0; u < g; ++u)
    for (const auto& r : unit) {
      Member m; m.cell = 0; m.dx = r.x0 + u * p; m.dy = r.y0;
      m.w = r.width(); m.h = r.height(); m.orient = 0;
      c.members.push_back(m);
    }
  c.leaf_count = g * (int)unit.size();
  h.cells.push_back(c);
  for (int j = 0; j < K / g; ++j) { Placement pl; pl.cell = 1; pl.x = j * g * p; pl.y = 0; h.top.push_back(pl); }
  h.flat_leaf_count = K * (int)unit.size();
  h.levels = 1;
  return h;
}

// Two-level hierarchy: an inner cell of `gi` units, an outer cell of `go` inner
// references, instantiated K/(gi*go) times.
static Hierarchy nested(const std::vector<Rect>& unit, int K, int gi, int go, double p) {
  Hierarchy h;
  CellDef in; in.id = 1; in.level = 1;
  double iw = (gi - 1) * p, ih = 0;
  for (int u = 0; u < gi; ++u)
    for (const auto& r : unit) {
      Member m; m.cell = 0; m.dx = r.x0 + u * p; m.dy = r.y0;
      m.w = r.width(); m.h = r.height(); m.orient = 0;
      in.members.push_back(m);
      iw = std::max(iw, m.dx + m.w); ih = std::max(ih, m.dy + m.h);
    }
  in.w = iw; in.h = ih; in.leaf_count = gi * (int)unit.size();

  CellDef out; out.id = 2; out.level = 2;
  for (int b = 0; b < go; ++b) {
    Member m; m.cell = 1; m.dx = b * gi * p; m.dy = 0; m.w = in.w; m.h = in.h; m.orient = 0;
    out.members.push_back(m);
  }
  out.leaf_count = go * in.leaf_count;

  h.cells = {in, out};
  for (int j = 0; j < K / (gi * go); ++j) { Placement pl; pl.cell = 2; pl.x = j * gi * go * p; pl.y = 0; h.top.push_back(pl); }
  h.flat_leaf_count = K * (int)unit.size();
  h.levels = 2;
  return h;
}

static int row(const char* name, const Hierarchy& h, const std::vector<Rect>& G) {
  int smallest = 1 << 30;
  for (const auto& c : h.cells) smallest = std::min(smallest, c.leaf_count);
  bool exact = multiset_equal(flatten(h), G);
  std::printf("  %-26s %5zu %6d %10d %10zu %6d   %s\n", name, h.cells.size(), h.levels,
              smallest, h.top.size(), cost_of(h), exact ? "exact" : "**MISMATCH**");
  return exact ? 0 : 1;
}

int main() {
  std::printf("Non-uniqueness of hierarchy: one flat layout, many valid decompositions\n");
  std::printf("(every row below flattens to EXACTLY the same rectangles)\n");

  // --- Part 1: the combinatorial family, built and verified by hand. ----------
  const std::vector<Rect> unit = {{0, 0, 4, 10}, {5, 0, 9, 4}, {5, 6, 9, 10}};  // 3-rect glyph
  const int K = 12;
  const double p = 12;
  std::vector<Rect> G = row_layout(unit, K, p);

  std::printf("\nPart 1 — a row of %d unit motifs (%zu rectangles), grouped every which way:\n", K, G.size());
  std::printf("  %-26s %5s %6s %10s %10s %6s   %s\n", "hierarchy", "cells", "levels",
              "tile(leaf)", "instances", "cost", "flatten=G");
  int bad = 0;
  for (int g : {1, 2, 3, 4, 6, 12}) {
    char nm[48]; std::snprintf(nm, sizeof nm, "flat: %d-unit cell x%d", g, K / g);
    bad += row(nm, grouped(unit, K, g, p), G);
  }
  bad += row("nested: (2-unit)->x3, x2", nested(unit, K, 2, 3, p), G);
  bad += row("nested: (3-unit)->x2, x2", nested(unit, K, 3, 2, p), G);
  std::printf("  --> all exact; costs span a wide range. \"Which is THE hierarchy?\" has no\n"
              "      unique answer — the minimum-cost one is a choice of objective, not a fact.\n");

  // --- Part 2: the engine's own choices on one real datapath. ------------------
  ErasureDesign d = make_datapath(6, 5, 2, true, 6, false);
  std::printf("\nPart 2 — the recoverer's own choices on one datapath (%zu rectangles):\n", d.flat.size());
  std::printf("  %-26s %5s %6s %10s %8s %6s   %s\n", "method", "cells", "levels", "instances",
              "residual", "cost", "flatten=G");
  auto line = [&](const char* name, std::size_t cells, int levels, std::size_t inst,
                  std::size_t resid, int cost, bool exact) {
    std::printf("  %-26s %5zu %6d %10zu %8zu %6d   %s\n", name, cells, levels, inst,
                resid, cost, exact ? "exact" : "NO");
  };
  RecoverConfig mdl; mdl.selection = Selection::MDLGain;
  Hierarchy hm = recover_hierarchy(d.flat, mdl);
  line("recover (flat, MDL)", hm.cells.size(), hm.levels, hm.top.size(),
       hm.residual.size(), hm.hier_cost, hm.flatten_matches);
  bad += !hm.flatten_matches;
  Nested nh = recover_nested(d.flat);
  line("recover_nested (deep)", nh.base.cells.size() + nh.cells.size(), nh.levels,
       nh.top.size(), nh.residual.size(), nh.nested_cost, nh.matches_base_flatten);
  bad += !nh.matches_base_flatten;
  LatticeInfo li; Hierarchy hl = recover_lattice(d.flat, {}, &li);
  line("recover_lattice (array)", hl.cells.size(), hl.levels, hl.top.size(),
       hl.residual.size(), hl.hier_cost, hl.flatten_matches);
  bad += !(hl.cells.empty() || hl.flatten_matches);
  std::printf("  --> the same flat geometry, three structurally different valid hierarchies:\n"
              "      a flat gate list, a deep gate->slice->block->top nesting, and a single\n"
              "      arrayed super-tile with the block gutters as residual. A lower cost is not\n"
              "      \"more correct\" — all are exact. Which one you get is the objective, not a\n"
              "      fact about the layout (§1.3 agreement, not accuracy).\n");
  if (bad) std::printf("\n**FAILURE**: %d hierarchy(ies) did not flatten to G exactly\n", bad);
  return bad ? 1 : 0;
}
