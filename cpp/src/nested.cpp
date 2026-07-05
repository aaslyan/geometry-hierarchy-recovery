#include "adt/nested.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace adt::hr {

namespace {

long qbin(double v, double q) { return std::lround(v / q); }

// --- Flatten walk ------------------------------------------------------------
// Collect all base-leaf placements (and raw residual rects) produced by a nested
// hierarchy, then expand them through the public flatten(). Expansion above the
// leaf level is pure translation (see nested.hpp): a child at site i sits at
// parent-anchor + (ox + i·sx, oy + i·sy).
struct LeafPlace { int cell; double x, y; int orient; };

void walk_cell(const Nested& h, int ncell, double bx, double by,
               std::vector<LeafPlace>& out);

void walk_item(const Nested& h, const NItem& it, double bx, double by,
               std::vector<LeafPlace>& out) {
  std::vector<char> gap(it.n, 0);
  for (int g : it.gaps) if (g >= 0 && g < it.n) gap[g] = 1;
  for (int i = 0; i < it.n; ++i) {
    if (gap[i]) continue;
    double px = bx + it.ox + i * it.sx;
    double py = by + it.oy + i * it.sy;
    if (it.leaf) out.push_back({it.child, px, py, it.orient});
    else walk_cell(h, it.child, px, py, out);
  }
}

void walk_cell(const Nested& h, int ncell, double bx, double by,
               std::vector<LeafPlace>& out) {
  const NCell& c = h.cells[ncell - 1];
  for (const auto& it : c.body) walk_item(h, it, bx, by, out);
}

}  // namespace

std::vector<Rect> flatten_nested(const Nested& h) {
  std::vector<LeafPlace> leaves;
  for (const auto& t : h.top) {
    if (t.leaf) leaves.push_back({t.cell, t.x, t.y, t.orient});
    else walk_cell(h, t.cell, t.x, t.y, leaves);
  }
  Hierarchy tmp;
  tmp.cells = h.base.cells;
  tmp.residual = h.residual;
  for (const auto& lp : leaves) tmp.top.push_back({lp.cell, lp.x, lp.y, lp.orient});
  return flatten(tmp);
}

namespace {

// Multiset equality of two rectangle lists under quantum q (the §5.4 gate).
bool multiset_equal(const std::vector<Rect>& a, const std::vector<Rect>& b, double q) {
  if (a.size() != b.size()) return false;
  struct RKey {
    long x0, y0, x1, y1;
    bool operator==(const RKey& o) const {
      return x0 == o.x0 && y0 == o.y0 && x1 == o.x1 && y1 == o.y1;
    }
  };
  struct H {
    std::size_t operator()(const RKey& k) const {
      std::size_t h = 1469598103934665603ULL;
      auto mix = [&](long long v) { h ^= (std::size_t)v; h *= 1099511628211ULL; };
      mix(k.x0); mix(k.y0); mix(k.x1); mix(k.y1);
      return h;
    }
  };
  auto key = [&](const Rect& r) {
    return RKey{qbin(r.x0, q), qbin(r.y0, q), qbin(r.x1, q), qbin(r.y1, q)};
  };
  std::unordered_map<RKey, int, H> ca;
  ca.reserve(a.size() * 2);
  for (const auto& r : a) ca[key(r)]++;
  for (const auto& r : b) {
    auto it = ca.find(key(r));
    if (it == ca.end() || it->second == 0) return false;
    it->second--;
  }
  for (const auto& kv : ca) if (kv.second != 0) return false;
  return true;
}

// Containment: every rectangle in `sub` appears in `super` with at least its
// multiplicity (super ⊇ sub). Used for the defect-tolerant G ⊆ flatten check.
bool multiset_contains(const std::vector<Rect>& super, const std::vector<Rect>& sub,
                       double q) {
  struct RKey {
    long x0, y0, x1, y1;
    bool operator==(const RKey& o) const {
      return x0 == o.x0 && y0 == o.y0 && x1 == o.x1 && y1 == o.y1;
    }
  };
  struct H {
    std::size_t operator()(const RKey& k) const {
      std::size_t h = 1469598103934665603ULL;
      auto mix = [&](long long v) { h ^= (std::size_t)v; h *= 1099511628211ULL; };
      mix(k.x0); mix(k.y0); mix(k.x1); mix(k.y1);
      return h;
    }
  };
  auto key = [&](const Rect& r) {
    return RKey{qbin(r.x0, q), qbin(r.y0, q), qbin(r.x1, q), qbin(r.y1, q)};
  };
  std::unordered_map<RKey, int, H> cs;
  cs.reserve(super.size() * 2);
  for (const auto& r : super) cs[key(r)]++;
  for (const auto& r : sub) {
    auto it = cs.find(key(r));
    if (it == cs.end() || it->second == 0) return false;
    it->second--;
  }
  return true;
}

// --- Cell construction helpers ----------------------------------------------
struct BBox { double w = 0, h = 0; };
using BBoxCache = std::map<std::pair<int, int>, BBox>;

// bbox dims of one placed child instance. Nested children carry their stored
// bbox; leaf children are flattened once (D4 may swap w/h) and measured.
BBox child_bbox(const Nested& h, bool leaf, int cell, int orient, BBoxCache& cache) {
  if (!leaf) return {h.cells[cell - 1].w, h.cells[cell - 1].h};
  auto k = std::make_pair(cell, orient);
  auto it = cache.find(k);
  if (it != cache.end()) return it->second;
  Hierarchy tmp;
  tmp.cells = h.base.cells;
  tmp.top.push_back({cell, 0, 0, orient});
  std::vector<Rect> f = flatten(tmp);
  double mnx = 1e300, mny = 1e300, mxx = -1e300, mxy = -1e300;
  for (const auto& r : f) {
    mnx = std::min(mnx, r.x0); mny = std::min(mny, r.y0);
    mxx = std::max(mxx, r.x1); mxy = std::max(mxy, r.y1);
  }
  BBox b{mxx - mnx, mxy - mny};
  cache[k] = b;
  return b;
}

int child_leaf_count(const Nested& h, bool leaf, int cell) {
  return leaf ? h.base.cells[cell - 1].leaf_count : h.cells[cell - 1].leaf_count;
}

// Compute a body's bbox dims and expanded leaf count (offsets are relative to the
// cell anchor, which callers place at the body's min corner, so min → 0).
void measure_body(const Nested& h, const std::vector<NItem>& body, BBoxCache& cache,
                  double& w, double& hh, int& leaf_count) {
  double mnx = 1e300, mny = 1e300, mxx = -1e300, mxy = -1e300;
  leaf_count = 0;
  for (const auto& it : body) {
    BBox cb = child_bbox(h, it.leaf, it.child, it.orient, cache);
    int clc = child_leaf_count(h, it.leaf, it.child);
    std::vector<char> gap(it.n, 0);
    for (int g : it.gaps) if (g >= 0 && g < it.n) gap[g] = 1;
    for (int i = 0; i < it.n; ++i) {
      if (gap[i]) continue;
      double px = it.ox + i * it.sx, py = it.oy + i * it.sy;
      mnx = std::min(mnx, px); mny = std::min(mny, py);
      mxx = std::max(mxx, px + cb.w); mxy = std::max(mxy, py + cb.h);
    }
    leaf_count += it.occupied() * clc;
  }
  w = mxx - mnx; hh = mxy - mny;
}

std::string body_signature(const std::string& kind, const std::vector<NItem>& body,
                           double q) {
  std::string s = kind + "|";
  for (const auto& it : body) {
    s += (it.leaf ? "L" : "C") + std::to_string(it.child) + "." +
         std::to_string(it.orient) + ":" + std::to_string(qbin(it.ox, q)) + "," +
         std::to_string(qbin(it.oy, q)) + "+" + std::to_string(qbin(it.sx, q)) + "," +
         std::to_string(qbin(it.sy, q)) + "x" + std::to_string(it.n) + "/";
    for (int g : it.gaps) s += std::to_string(g) + ",";
    s += ";";
  }
  return s;
}

// Intern (dedup) a nested cell by structural signature so identical slices/blocks
// across the layout share one definition — the array-node canonicalization step.
int intern_cell(Nested& h, const std::string& kind, std::vector<NItem> body,
                int level, double q, BBoxCache& cache,
                std::map<std::string, int>& sig2id) {
  std::string sig = body_signature(kind, body, q);
  auto it = sig2id.find(sig);
  if (it != sig2id.end()) return it->second;
  NCell c;
  c.id = (int)h.cells.size() + 1;
  c.kind = kind;
  c.body = std::move(body);
  c.level = level;
  measure_body(h, c.body, cache, c.w, c.h, c.leaf_count);
  h.cells.push_back(std::move(c));
  sig2id[sig] = h.cells.back().id;
  return h.cells.back().id;
}

// --- One promotion pass along an axis ---------------------------------------
// Group current top placements by content (leaf/cell/orient), bucket each group
// by the perpendicular coordinate, split each bucket into maximal regular runs
// along `axis`, and promote every run of >= 2 instances into one array cell
// (site i at origin + i·step, with any missing sites recorded as gaps). Identical
// runs canonicalize to one shared cell. Placements not covered by a run pass
// through unchanged. Returns true if anything was promoted.
struct ContentKey {
  bool leaf; int cell, orient;
  bool operator<(const ContentKey& o) const {
    return std::make_tuple(leaf, cell, orient) <
           std::make_tuple(o.leaf, o.cell, o.orient);
  }
};

bool promote_axis(Nested& h, const RecoverConfig& cfg, int axis, int level,
                  BBoxCache& cache, std::map<std::string, int>& sig2id) {
  const double q = cfg.quantum, tol = 1.0;
  const int min_run = 2;
  auto axc = [&](const NInst& t) { return axis == 0 ? t.x : t.y; };
  auto prp = [&](const NInst& t) { return axis == 0 ? t.y : t.x; };

  std::vector<NInst> old = h.top;
  const int N = (int)old.size();
  std::vector<char> covered(N, 0);
  std::vector<NInst> made;
  bool changed = false;

  std::map<ContentKey, std::vector<int>> groups;
  for (int i = 0; i < N; ++i)
    groups[{old[i].leaf, old[i].cell, old[i].orient}].push_back(i);

  for (auto& grp : groups) {
    const ContentKey& key = grp.first;

    std::map<long, std::vector<int>> buckets;
    for (int i : grp.second) buckets[qbin(prp(old[i]), tol)].push_back(i);

    for (auto& bk : buckets) {
      std::vector<int>& ids = bk.second;
      std::sort(ids.begin(), ids.end(),
                [&](int a, int b) { return axc(old[a]) < axc(old[b]); });

      // Split into maximal runs: a gap must be an integer multiple of the run's
      // base step (the smallest gap) to stay in the same run; otherwise split.
      std::size_t s = 0;
      while (s < ids.size()) {
        std::size_t e = s + 1;
        double step = 0;
        while (e < ids.size()) {
          double d = axc(old[ids[e]]) - axc(old[ids[e - 1]]);
          if (d <= tol) break;  // coincident: cannot form a lattice
          if (step == 0) {
            step = d;
          } else {
            double m = d / step;
            if (std::abs(m - std::lround(m)) > tol / step) break;  // not on lattice
            if (d < step) step = d;  // a smaller gap refines the base step
          }
          ++e;
        }
        // Run is ids[s..e).
        int occ = (int)(e - s);
        if (occ >= min_run && step > 0) {
          double a0 = axc(old[ids[s]]);
          double perp = prp(old[ids[s]]);
          int n = (int)std::lround((axc(old[ids[e - 1]]) - a0) / step) + 1;
          std::vector<char> present(n, 0);
          for (std::size_t k = s; k < e; ++k) {
            int site = (int)std::lround((axc(old[ids[k]]) - a0) / step);
            if (site >= 0 && site < n) present[site] = 1;
          }
          std::vector<int> gaps;
          for (int si = 0; si < n; ++si) if (!present[si]) gaps.push_back(si);

          NItem item;
          item.leaf = key.leaf;
          item.child = key.cell;
          item.orient = key.orient;
          item.ox = 0; item.oy = 0;
          item.sx = axis == 0 ? step : 0;
          item.sy = axis == 0 ? 0 : step;
          item.n = n;
          item.gaps = gaps;

          std::string kind = key.leaf ? "slice" : (level >= 4 ? "top" : "block");
          int id = intern_cell(h, kind, {item}, level, q, cache, sig2id);

          NInst inst;
          inst.leaf = false;
          inst.cell = id;
          inst.x = axis == 0 ? a0 : perp;
          inst.y = axis == 0 ? perp : a0;
          inst.orient = 0;
          made.push_back(inst);
          for (std::size_t k = s; k < e; ++k) covered[ids[k]] = 1;
          changed = true;
        }
        s = e;
      }
    }
  }

  if (!changed) return false;
  std::vector<NInst> next;
  for (int i = 0; i < N; ++i) if (!covered[i]) next.push_back(old[i]);
  for (auto& m : made) next.push_back(m);
  h.top = std::move(next);
  return true;
}

// Sum body-item cost over all nested cells + top instances + residual + the base
// leaf-cell body sizes (the leaf definitions are real and must be paid for).
int compute_nested_cost(const Nested& h) {
  int cost = 0;
  for (const auto& c : h.base.cells) cost += (int)c.members.size();
  for (const auto& c : h.cells)
    for (const auto& it : c.body) cost += it.cost();
  cost += (int)h.top.size();
  cost += (int)h.residual.size();
  return cost;
}

}  // namespace

std::string nested_to_json(const Nested& h) {
  auto num = [](double v) {
    long r = std::lround(v);
    if (std::abs(v - r) < 1e-9) return std::to_string(r);
    return std::to_string(v);
  };
  std::string s = "{\n";

  // Base leaf cells (the recovered gate) and synthesized nested cells.
  s += "  \"leaf_cells\": [";
  for (std::size_t i = 0; i < h.base.cells.size(); ++i) {
    const auto& c = h.base.cells[i];
    s += (i ? ", " : "") + std::string("{\"id\": ") + std::to_string(c.id) +
         ", \"rects\": " + std::to_string(c.leaf_count) + "}";
  }
  s += "],\n";

  auto rect_json = [&](const Rect& r) {
    return "[" + num(r.x0) + ", " + num(r.y0) + ", " + num(r.x1) + ", " + num(r.y1) + "]";
  };

  s += "  \"cells\": [\n";
  for (std::size_t i = 0; i < h.cells.size(); ++i) {
    const auto& c = h.cells[i];
    s += "    {\"id\": " + std::to_string(c.id) + ", \"kind\": \"" + c.kind +
         "\", \"level\": " + std::to_string(c.level) + ", \"leaves\": " +
         std::to_string(c.leaf_count) + ", \"bbox\": [" + num(c.w) + ", " + num(c.h) +
         "], \"body\": [";
    for (std::size_t j = 0; j < c.body.size(); ++j) {
      const auto& it = c.body[j];
      s += (j ? ", " : "") + std::string("{\"child\": ") + std::to_string(it.child) +
           ", \"is_leaf\": " + (it.leaf ? "true" : "false") + ", \"orient\": " +
           std::to_string(it.orient) + ", \"origin\": [" + num(it.ox) + ", " + num(it.oy) +
           "], \"step\": [" + num(it.sx) + ", " + num(it.sy) + "], \"n\": " +
           std::to_string(it.n) + ", \"gaps\": [";
      for (std::size_t g = 0; g < it.gaps.size(); ++g)
        s += (g ? ", " : "") + std::to_string(it.gaps[g]);
      s += "]}";
    }
    s += "]}";
    s += (i + 1 < h.cells.size()) ? ",\n" : "\n";
  }
  s += "  ],\n";

  // Top-level instances.
  s += "  \"instances\": [";
  for (std::size_t i = 0; i < h.top.size(); ++i) {
    const auto& t = h.top[i];
    s += (i ? ", " : "") + std::string("{\"cell\": ") + std::to_string(t.cell) +
         ", \"is_leaf\": " + (t.leaf ? "true" : "false") + ", \"orient\": " +
         std::to_string(t.orient) + ", \"pos\": [" + num(t.x) + ", " + num(t.y) + "]}";
  }
  s += "],\n";

  // Actual residual rectangles (not just a count) so downstream tooling can use them.
  s += "  \"residual\": [";
  for (std::size_t i = 0; i < h.residual.size(); ++i)
    s += (i ? ", " : "") + rect_json(h.residual[i]);
  s += "],\n";

  // Defect layer: the idealized missing-member rectangles the base recoverer
  // recorded (flatten(nested) − G under the §6 defect-tolerant semantics).
  s += "  \"defects\": {\"instances\": " + std::to_string(h.base.n_defective) +
       ", \"missing_members\": [";
  for (std::size_t i = 0; i < h.base.missing_geometry.size(); ++i)
    s += (i ? ", " : "") + rect_json(h.base.missing_geometry[i]);
  s += "]},\n";

  double base_r = h.base_cost ? (double)h.flat_leaf_count / h.base_cost : 0.0;
  double nest_r = h.nested_cost ? (double)h.flat_leaf_count / h.nested_cost : 0.0;
  s += "  \"metrics\": {\n";
  s += "    \"flat_leaf_count\": " + std::to_string(h.flat_leaf_count) + ",\n";
  s += "    \"base_cost\": " + std::to_string(h.base_cost) +
       ", \"base_compression\": " + num(base_r) + ",\n";
  s += "    \"nested_cost\": " + std::to_string(h.nested_cost) +
       ", \"nested_compression\": " + num(nest_r) + ",\n";
  s += "    \"levels\": " + std::to_string(h.levels) +
       ", \"slice_cells\": " + std::to_string(h.n_slice) +
       ", \"block_cells\": " + std::to_string(h.n_block) + ",\n";
  s += "    \"matches_base_flatten\": " +
       std::string(h.matches_base_flatten ? "true" : "false") +
       ", \"explains_g\": " + std::string(h.explains_g ? "true" : "false") +
       ", \"defect_rects\": " + std::to_string(h.defect_rects) + "\n";
  s += "  }\n}\n";
  return s;
}

Nested recover_nested(const std::vector<Rect>& layout, const RecoverConfig& cfg) {
  Nested h;
  h.base = recover_hierarchy(layout, cfg);
  h.flat_leaf_count = h.base.flat_leaf_count;
  h.base_cost = h.base.hier_cost;
  h.residual = h.base.residual;

  // The promotion only REGROUPS base placements, so the invariant is that the
  // nested view reproduces the base recoverer's own idealized geometry exactly.
  // (For clean layouts flatten(base) == G; with a tolerated defect the base
  // over-produces the idealized missing member and carries it in its defect
  // layer — the nested view inherits that layer unchanged, so we verify against
  // flatten(base), not raw G.)
  std::vector<Rect> ref = flatten(h.base);

  // Start from the base placement list wrapped as top-level leaf instances.
  for (const auto& p : h.base.top)
    h.top.push_back({true, p.cell, p.x, p.y, p.orient});

  // Alternate promotion axes (X: rows → slices; Y: stacks → blocks; X: → top),
  // certifying flatten equality after each pass and reverting a pass that breaks
  // it (should not happen by construction — the gate guards against bugs).
  BBoxCache cache;
  std::map<std::string, int> sig2id;
  int idle = 0, level = 2, axis = 0;
  while (idle < 2 && level <= cfg.max_levels + 2) {
    // Snapshot the hierarchy AND the interning side-tables. intern_cell mutates
    // sig2id (and cache) as it creates cells; if this pass is rejected below and
    // h is rolled back, the side-tables must roll back too — otherwise a later
    // pass could hit a stale signature and return a cell id that no longer exists
    // in h.cells (a dangling reference the flatten walk would read out of bounds).
    Nested snap = h;
    std::map<std::string, int> sig_snap = sig2id;
    BBoxCache cache_snap = cache;
    bool ch = promote_axis(h, cfg, axis, level, cache, sig2id);
    if (ch) {
      std::vector<Rect> flat = flatten_nested(h);
      if (!multiset_equal(flat, ref, cfg.quantum)) {
        h = std::move(snap);  // revert: this pass broke exactness
        sig2id = std::move(sig_snap);
        cache = std::move(cache_snap);
        ch = false;
      }
    }
    if (ch) { level++; idle = 0; } else { idle++; }
    axis = 1 - axis;
  }

  h.levels = 0;
  for (const auto& c : h.cells) h.levels = std::max(h.levels, c.level);
  for (const auto& c : h.cells) {
    bool refs_leaf = false, refs_cell = false;
    for (const auto& it : c.body) (it.leaf ? refs_leaf : refs_cell) = true;
    if (refs_leaf && !refs_cell) h.n_slice++;
    if (refs_cell) h.n_block++;
  }

  h.nested_cost = compute_nested_cost(h);
  std::vector<Rect> flat = flatten_nested(h);
  h.matches_base_flatten = multiset_equal(flat, ref, cfg.quantum);
  h.explains_g = multiset_contains(flat, layout, cfg.quantum);
  h.defect_rects = (int)flat.size() - (int)layout.size();
  return h;
}

}  // namespace adt::hr
