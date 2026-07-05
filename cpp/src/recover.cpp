#include "adt/recover.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <boost/geometry/index/rtree.hpp>

#include "adt/d4.hpp"
#include "adt/geometry.hpp"

namespace adt::hr {

namespace bgi = boost::geometry::index;

namespace {

long qbin(double v, double q) { return std::lround(v / q); }

// --- Spatial index over primitive anchors (Boost rtree). Neighbor queries for
// shingles and growth go through this instead of an O(n^2) scan — the design
// mandates the rtree for exactly this local-neighborhood work. -----------------
using RValue = std::pair<Point, int>;
using RTree = bgi::rtree<RValue, bgi::quadratic<16>>;

double cx(const Prim& p) { return 0.5 * (p.box.x0 + p.box.x1); }
double cy(const Prim& p) { return 0.5 * (p.box.y0 + p.box.y1); }

RTree build_rtree(const std::vector<Prim>& prims) {
  std::vector<RValue> items;
  items.reserve(prims.size());
  for (int i = 0; i < (int)prims.size(); ++i)
    items.emplace_back(Point(cx(prims[i]), cy(prims[i])), i);
  return RTree(items.begin(), items.end());  // packing constructor: O(n log n)
}

// --- Exact-match index keyed on (cell, orientation, snapped anchor). Integer key
// with an FNV-style hash — no per-lookup string construction. ------------------
struct IKey {
  int cell, orient;
  long qx, qy;
  bool operator==(const IKey& o) const {
    return cell == o.cell && orient == o.orient && qx == o.qx && qy == o.qy;
  }
};
struct IKeyHash {
  std::size_t operator()(const IKey& k) const {
    std::size_t h = 1469598103934665603ULL;
    auto mix = [&](long long v) { h ^= (std::size_t)v; h *= 1099511628211ULL; };
    mix(k.cell); mix(k.orient); mix(k.qx); mix(k.qy);
    return h;
  }
};

// A snapped rectangle as a hashable key — used for the O(n) flatten==G check.
struct RKey {
  long x0, y0, x1, y1;
  bool operator==(const RKey& o) const {
    return x0 == o.x0 && y0 == o.y0 && x1 == o.x1 && y1 == o.y1;
  }
};
struct RKeyHash {
  std::size_t operator()(const RKey& k) const {
    std::size_t h = 1469598103934665603ULL;
    auto mix = [&](long long v) { h ^= (std::size_t)v; h *= 1099511628211ULL; };
    mix(k.x0); mix(k.y0); mix(k.x1); mix(k.y1);
    return h;
  }
};

struct Index {
  std::unordered_map<IKey, int, IKeyHash> map;
  double q = 0.5;
  void build(const std::vector<Prim>& prims, double quantum) {
    q = quantum;
    map.clear();
    map.reserve(prims.size() * 2);
    for (int i = 0; i < (int)prims.size(); ++i)
      map[{prims[i].cell, prims[i].orient, qbin(ax(prims[i]), q),
           qbin(ay(prims[i]), q)}] = i;
  }
  int at(int cell, int orient, double x, double y) const {
    auto it = map.find({cell, orient, qbin(x, q), qbin(y, q)});
    return it == map.end() ? -1 : it->second;
  }
};

Member member_of(const Prim& p, double ax0, double ay0) {
  return {p.cell, ax(p) - ax0, ay(p) - ay0, p.box.width(), p.box.height(), p.orient};
}

std::vector<Member> transform_body(const std::vector<Member>& b, int g) {
  std::vector<Member> out;
  out.reserve(b.size());
  for (const auto& m : b) out.push_back(xform_member(m, g));
  return out;
}

void normalize(std::vector<Member>& ms) {
  double mnx = 1e300, mny = 1e300;
  for (const auto& m : ms) { mnx = std::min(mnx, m.dx); mny = std::min(mny, m.dy); }
  for (auto& m : ms) { m.dx -= mnx; m.dy -= mny; }
}

std::string signature(std::vector<Member> ms, double q) {
  std::sort(ms.begin(), ms.end(), [&](const Member& a, const Member& b) {
    return std::make_tuple(a.cell, a.orient, qbin(a.dx, q), qbin(a.dy, q),
                           qbin(a.w, q), qbin(a.h, q)) <
           std::make_tuple(b.cell, b.orient, qbin(b.dx, q), qbin(b.dy, q),
                           qbin(b.w, q), qbin(b.h, q));
  });
  std::string s;
  for (const auto& m : ms)
    s += std::to_string(m.cell) + "." + std::to_string(m.orient) + ":" +
         std::to_string(qbin(m.dx, q)) + "," + std::to_string(qbin(m.dy, q)) + "|" +
         std::to_string(qbin(m.w, q)) + "x" + std::to_string(qbin(m.h, q)) + ";";
  return s;
}

// D4-invariant signature: the lexicographically smallest signature over all 8
// orientations of the normalized member set. Two neighborhoods that are the same
// motif up to translation + reflection/rotation hash identically.
std::string canonical_sig(const std::vector<Member>& ms, double q) {
  std::string best;
  for (int g = 0; g < 8; ++g) {
    std::vector<Member> t = transform_body(ms, g);
    normalize(t);
    std::string s = signature(t, q);
    if (g == 0 || s < best) best = s;
  }
  return best;
}

bool same_member(const Member& a, const Member& b, double q) {
  return a.cell == b.cell && a.orient == b.orient && qbin(a.dx, q) == qbin(b.dx, q) &&
         qbin(a.dy, q) == qbin(b.dy, q) && qbin(a.w, q) == qbin(b.w, q) &&
         qbin(a.h, q) == qbin(b.h, q);
}

bool shape_ok(const Prim& p, const Member& m, double q) {
  return qbin(p.box.width(), q) == qbin(m.w, q) &&
         qbin(p.box.height(), q) == qbin(m.h, q);
}

struct Occ { int o = 0; double tx = 0, ty = 0; };

// Find the orientation + translation that places body B0 (defined in occ-0's
// frame, member 0 = the seed) so its seed lands on prim `sj` and every member is
// present in the layout. Returns false if no orientation matches.
bool find_orient(const std::vector<Member>& B0, const std::vector<Prim>& prims,
                 int sj, const Index& idx, double q, Occ& out) {
  for (int o = 0; o < 8; ++o) {
    Member seed_t = xform_member(B0[0], o);
    if (qbin(seed_t.w, q) != qbin(prims[sj].box.width(), q) ||
        qbin(seed_t.h, q) != qbin(prims[sj].box.height(), q))
      continue;
    double tx = ax(prims[sj]) - seed_t.dx, ty = ay(prims[sj]) - seed_t.dy;
    bool ok = true;
    for (const auto& m : B0) {
      Member mt = xform_member(m, o);
      int j = idx.at(mt.cell, mt.orient, tx + mt.dx, ty + mt.dy);
      if (j < 0 || !shape_ok(prims[j], mt, q)) { ok = false; break; }
    }
    if (ok) { out = {o, tx, ty}; return true; }
  }
  return false;
}

// Seed-and-extend (§2.4), orientation-aware: add any primitive present at a
// consistent transformed offset around EVERY instance. Candidate prims come from
// an rtree range query around the seed (not an O(n) scan of the whole layout),
// and the body is capped so a perfectly-periodic region can't grow a cell without
// bound — that bound is what keeps growth near-linear at scale.
void grow(std::vector<Member>& B0, const std::vector<Occ>& occ,
          const std::vector<Prim>& prims, const Index& idx, const RTree& tree,
          const RecoverConfig& cfg) {
  double a0x = occ[0].tx, a0y = occ[0].ty, R = cfg.grow_radius;
  std::vector<RValue> local;
  bg::model::box<Point> qb(Point(a0x - R - 32, a0y - R - 32),
                           Point(a0x + R + 32, a0y + R + 32));
  tree.query(bgi::intersects(qb), std::back_inserter(local));

  // Every primitive already claimed by the body across ALL instances. A new
  // member may only claim primitives that are still free — otherwise adding it
  // would make two instances of the cell overlap, which is what let growth
  // over-extend across a dense periodic region and produce an over-covering
  // (invalid) cell. This keeps the cell a proper, non-overlapping tile.
  std::unordered_set<int> claimed;
  auto claim_body = [&]() {
    claimed.clear();
    for (const auto& m : B0)
      for (const auto& oc : occ) {
        Member mt = xform_member(m, oc.o);
        int j = idx.at(mt.cell, mt.orient, oc.tx + mt.dx, oc.ty + mt.dy);
        if (j >= 0) claimed.insert(j);
      }
  };
  claim_body();

  bool added = true;
  while (added && (int)B0.size() < cfg.max_cell_members) {
    added = false;
    for (const auto& lv : local) {
      const Prim& p = prims[lv.second];
      double rx = ax(p) - a0x, ry = ay(p) - a0y;
      if (std::abs(rx) > R || std::abs(ry) > R) continue;
      Member c{p.cell, rx, ry, p.box.width(), p.box.height(), p.orient};
      bool present = false;
      for (const auto& m : B0)
        if (same_member(m, c, cfg.quantum)) { present = true; break; }
      if (present) continue;

      // Present at every instance, mapping to a DISTINCT, still-free primitive.
      std::vector<int> js;
      bool all = true;
      for (const auto& oc : occ) {
        Member mt = xform_member(c, oc.o);
        int j = idx.at(mt.cell, mt.orient, oc.tx + mt.dx, oc.ty + mt.dy);
        if (j < 0 || !shape_ok(prims[j], mt, cfg.quantum) || claimed.count(j)) {
          all = false;
          break;
        }
        js.push_back(j);
      }
      if (all) {
        B0.push_back(c);
        for (int j : js) claimed.insert(j);
        added = true;
        if ((int)B0.size() >= cfg.max_cell_members) break;
      }
    }
  }
}

struct Candidate {
  std::vector<Member> body;
  std::vector<Occ> occ;
  std::vector<std::vector<int>> covered;
  std::vector<Rect> boxes;  // placed instance bbox per occurrence
  double gain = 0;
};

// k nearest neighbors of `seed` via the rtree (O(log n + k)), replacing the old
// O(n) per-seed scan. Neighbor order is irrelevant — the signature sorts members.
std::vector<int> shingle_neighbors(int seed, const std::vector<Prim>& prims,
                                   const RTree& tree, int k) {
  std::vector<RValue> res;
  tree.query(bgi::nearest(Point(cx(prims[seed]), cy(prims[seed])), k + 1),
             std::back_inserter(res));
  std::vector<int> out;
  for (const auto& v : res) {
    if (v.second == seed) continue;
    out.push_back(v.second);
    if ((int)out.size() >= k) break;
  }
  return out;
}

// Defect-tolerant rescan (companion §6): find PARTIAL instances of an
// already-established cell among still-unconsumed prims. An instance is accepted
// if a strong majority of its members are present at a consistent orientation +
// position; the absent LEAF members are recorded as `missing_geometry` so the
// idealized flatten minus that layer still equals G exactly. A missing sub-cell
// reference aborts the match (nesting stays exact), and cells with fewer than 3
// members are skipped (a "partial" 2-member cell is just a coincidence).
void defect_rescan(int cell_id, const std::vector<Member>& body,
                   const std::vector<Prim>& prims, const Index& idx,
                   std::vector<char>& consumed, const RecoverConfig& cfg,
                   std::vector<Prim>& out, Hierarchy& hier) {
  const int bn = (int)body.size();
  // Partial matching is only trustworthy for cells with >= 4 members: with a
  // 3-member cell, "one missing" leaves just two present rectangles, and two
  // rectangles coincidentally landing at two member offsets is common enough
  // (near clutter, at array edges) to fabricate phantom instances. Requiring at
  // least three present members makes a spurious match far less likely while
  // still catching genuine missing-member defects on real cells.
  if (bn < 4) return;
  const int need = std::max(3, (int)std::ceil(cfg.defect_min_support * bn));

  for (int i = 0; i < (int)prims.size(); ++i) {
    if (consumed[i]) continue;
    bool placed = false;
    for (int r = 0; r < bn && !placed; ++r) {
      for (int o = 0; o < 8 && !placed; ++o) {
        Member mr = xform_member(body[r], o);
        if (!shape_ok(prims[i], mr, cfg.quantum)) continue;
        double tx = ax(prims[i]) - mr.dx, ty = ay(prims[i]) - mr.dy;

        std::vector<int> present;
        std::vector<Rect> missing;
        bool conflict = false;
        double mnx = 1e300, mny = 1e300, mxx = -1e300, mxy = -1e300;
        for (const auto& m : body) {
          Member mt = xform_member(m, o);
          double axm = tx + mt.dx, aym = ty + mt.dy;
          mnx = std::min(mnx, axm); mny = std::min(mny, aym);
          mxx = std::max(mxx, axm + mt.w); mxy = std::max(mxy, aym + mt.h);
          int j = idx.at(mt.cell, mt.orient, axm, aym);
          if (j >= 0 && shape_ok(prims[j], mt, cfg.quantum)) {
            if (consumed[j]) { conflict = true; break; }  // overlaps a placed instance
            present.push_back(j);
          } else if (mt.cell != 0) {
            conflict = true; break;                        // missing a sub-cell ref: not tolerated
          } else {
            missing.push_back({axm, aym, axm + mt.w, aym + mt.h});
          }
        }
        if (conflict) continue;
        int np = (int)present.size();
        if (np >= need && np < bn && (int)missing.size() <= cfg.defect_max_missing) {
          for (int j : present) consumed[j] = 1;
          out.push_back({cell_id, {mnx, mny, mxx, mxy}, o});
          for (const auto& mm : missing) hier.missing_geometry.push_back(mm);
          hier.n_defective++;
          placed = true;
        }
      }
    }
  }
}

// Repeat-length drop-off curve (companion §3) over a primitive list: for each
// motif size L, how many distinct size-L neighborhoods recur, and their total
// occurrences. F(L) falls off a cliff past the true cell scale; the elbow is the
// size at the top of the sharpest drop.
DropoffCurve compute_curve(const std::vector<Prim>& prims, const RTree& tree,
                           const RecoverConfig& cfg) {
  DropoffCurve curve;
  const int n = (int)prims.size();
  const int maxL = std::min(n, cfg.dropoff_max_size);
  for (int L = 1; L <= maxL; ++L) {
    std::map<std::string, int> counts;
    for (int i = 0; i < n; ++i) {
      std::vector<Member> sh{member_of(prims[i], ax(prims[i]), ay(prims[i]))};
      for (int j : shingle_neighbors(i, prims, tree, L - 1))
        sh.push_back(member_of(prims[j], ax(prims[i]), ay(prims[i])));
      if ((int)sh.size() < L) continue;  // not enough neighbors at this size
      counts[canonical_sig(sh, cfg.quantum)]++;
    }
    int R = 0, F = 0;
    for (const auto& kv : counts)
      if (kv.second >= cfg.min_instances) { ++R; F += kv.second; }
    curve.points.push_back({L, R, F});
    if (F == 0) break;  // no motif of this size recurs — past the cliff
  }
  // The cell scale is the size just before the distinct-motif count R(L) jumps:
  // up to the cell, all instances share one canonical neighborhood (R small);
  // past it, neighborhoods reach into non-repeating surroundings and fragment
  // into many distinct motifs (R rises sharply). The elbow is the L at the top of
  // that rise — "grow until the motif suddenly stops being the same everywhere."
  // (For a dense lattice this signal is weak — sub-units repeat too — which is
  // exactly why §3 is a scale prior, not a universal decider; MDL wins there.)
  int best_rise = 0;
  for (std::size_t i = 0; i + 1 < curve.points.size(); ++i) {
    int rise = curve.points[i + 1].distinct_motifs - curve.points[i].distinct_motifs;
    if (rise > best_rise) { best_rise = rise; curve.elbow_size = curve.points[i].size; }
  }
  if (curve.elbow_size == 0 && !curve.points.empty())
    curve.elbow_size = curve.points.front().size;
  return curve;
}

std::vector<Prim> round(const std::vector<Prim>& prims, const RecoverConfig& cfg,
                        Hierarchy& hier, int level, bool& changed) {
  changed = false;
  const int n = (int)prims.size();
  Index idx;
  idx.build(prims, cfg.quantum);
  RTree tree = build_rtree(prims);

  const bool PROF = std::getenv("ADT_PROF") != nullptr;
  auto clk = [] { return std::chrono::steady_clock::now(); };
  auto ms = [](auto a, auto b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
  };
  auto t0 = clk();

  // --- Channel B: D4-canonical shingle signature per seed, grouped. -----------
  std::map<std::string, std::vector<int>> groups;
  for (int i = 0; i < n; ++i) {
    std::vector<Member> sh{member_of(prims[i], ax(prims[i]), ay(prims[i]))};
    for (int j : shingle_neighbors(i, prims, tree, cfg.neighborhood_k))
      sh.push_back(member_of(prims[j], ax(prims[i]), ay(prims[i])));
    groups[canonical_sig(sh, cfg.quantum)].push_back(i);
  }

  auto t1 = clk();
  // --- Build / grow / verify / score one candidate per qualifying group. ------
  std::vector<Candidate> cands;
  for (auto& g : groups) {
    if ((int)g.second.size() < cfg.min_instances) continue;
    int seed0 = g.second[0];
    std::vector<Member> B0{member_of(prims[seed0], ax(prims[seed0]), ay(prims[seed0]))};
    for (int j : shingle_neighbors(seed0, prims, tree, cfg.neighborhood_k))
      B0.push_back(member_of(prims[j], ax(prims[seed0]), ay(prims[seed0])));

    std::vector<Occ> occ;
    for (int s : g.second) {
      Occ o;
      if (find_orient(B0, prims, s, idx, cfg.quantum, o)) occ.push_back(o);
    }
    if ((int)occ.size() < cfg.min_instances) continue;

    grow(B0, occ, prims, idx, tree, cfg);

    // covered prims + placed bbox per instance.
    std::vector<std::vector<int>> covered;
    std::vector<Rect> boxes;
    bool bad = false;
    for (const auto& oc : occ) {
      std::vector<int> cov;
      double mnx = 1e300, mny = 1e300, mxx = -1e300, mxy = -1e300;
      for (const auto& m : B0) {
        Member mt = xform_member(m, oc.o);
        int j = idx.at(mt.cell, mt.orient, oc.tx + mt.dx, oc.ty + mt.dy);
        if (j < 0) { bad = true; break; }
        cov.push_back(j);
        mnx = std::min(mnx, oc.tx + mt.dx); mny = std::min(mny, oc.ty + mt.dy);
        mxx = std::max(mxx, oc.tx + mt.dx + mt.w); mxy = std::max(mxy, oc.ty + mt.dy + mt.h);
      }
      if (bad) break;
      covered.push_back(std::move(cov));
      boxes.push_back({mnx, mny, mxx, mxy});
    }
    if (bad) continue;

    int k = (int)occ.size(), s = (int)B0.size();
    if (k < cfg.min_instances || s < cfg.min_cell_members) continue;
    double gain = (k - 1) * (double)s - k * cfg.cost_instance - cfg.def_overhead;
    if (cfg.selection == Selection::MDLGain && gain <= cfg.gain_min) continue;

    cands.push_back({B0, occ, std::move(covered), std::move(boxes), gain});
  }

  auto t2 = clk();
  // --- Dedup candidates covering the same primitive set. ----------------------
  std::map<std::string, int> best;
  for (int i = 0; i < (int)cands.size(); ++i) {
    std::vector<int> all;
    for (const auto& c : cands[i].covered) all.insert(all.end(), c.begin(), c.end());
    std::sort(all.begin(), all.end());
    std::string k;
    for (int v : all) k += std::to_string(v) + ",";
    auto it = best.find(k);
    if (it == best.end() || cands[i].gain > cands[it->second].gain) best[k] = i;
  }
  std::vector<Candidate> uniq;
  for (auto& kv : best) uniq.push_back(cands[kv.second]);

  auto t3 = clk();
  // --- Selection: order candidates, then greedy weighted set-packing (§2.7). ---
  // MDLGain orders by description-length reduction. DropOff orders by proximity to
  // the repeat-length cliff (§3) — the primitive cell scale — so the cell at the
  // drop-off is taken ahead of a higher-gain supercell.
  int elbow = 0;
  if (cfg.selection == Selection::DropOff)
    elbow = compute_curve(prims, tree, cfg).elbow_size;
  std::sort(uniq.begin(), uniq.end(), [&](const Candidate& a, const Candidate& b) {
    if (cfg.selection == Selection::DropOff) {
      int da = std::abs((int)a.body.size() - elbow);
      int db = std::abs((int)b.body.size() - elbow);
      if (da != db) return da < db;  // closest to the cliff scale wins
      return a.occ.size() * a.body.size() > b.occ.size() * b.body.size();  // coverage
    }
    return a.gain > b.gain;
  });
  std::vector<char> used(n, 0), consumed(n, 0);
  std::vector<Prim> out;
  std::vector<std::pair<int, std::vector<Member>>> created;  // (cell id, body) this round

  for (auto& c : uniq) {
    std::vector<int> keep;
    for (int i = 0; i < (int)c.occ.size(); ++i) {
      bool free = true;
      for (int p : c.covered[i]) if (used[p]) { free = false; break; }
      if (free) keep.push_back(i);
    }
    int k2 = (int)keep.size(), s = (int)c.body.size();
    if (k2 < cfg.min_instances) continue;
    if (cfg.selection == Selection::MDLGain) {
      double gain2 = (k2 - 1) * (double)s - k2 * cfg.cost_instance - cfg.def_overhead;
      if (gain2 <= cfg.gain_min) continue;  // DropOff gates on scale, not gain
    }

    int new_id = (int)hier.cells.size() + 1;
    CellDef def;
    def.id = new_id;
    def.members = c.body;
    def.level = level;
    double mnx = 1e300, mny = 1e300, mxx = -1e300, mxy = -1e300;
    int lc = 0;
    for (const auto& m : c.body) {
      mnx = std::min(mnx, m.dx); mny = std::min(mny, m.dy);
      mxx = std::max(mxx, m.dx + m.w); mxy = std::max(mxy, m.dy + m.h);
      lc += (m.cell == 0) ? 1 : hier.cells[m.cell - 1].leaf_count;
    }
    def.w = mxx - mnx; def.h = mxy - mny;
    def.leaf_count = lc;
    hier.cells.push_back(def);

    for (int i : keep) {
      for (int p : c.covered[i]) { used[p] = 1; consumed[p] = 1; }
      out.push_back({new_id, c.boxes[i], c.occ[i].o});
    }
    created.push_back({new_id, c.body});
    changed = true;
  }

  // Defect-tolerant rescan: recognize partial instances of the cells just
  // established, absorbing their defects into the defect layer instead of
  // leaving whole (nearly-complete) instances stranded in residual.
  if (cfg.defect_tolerant)
    for (auto& cb : created)
      defect_rescan(cb.first, cb.second, prims, idx, consumed, cfg, out, hier);

  for (int i = 0; i < n; ++i) if (!consumed[i]) out.push_back(prims[i]);
  if (PROF) {
    auto t4 = clk();
    std::fprintf(stderr,
                 "  round n=%d: shingle=%.1f build=%.1f dedup=%.1f greedy+rescan=%.1f "
                 "groups=%zu cands=%zu\n",
                 n, ms(t0, t1), ms(t1, t2), ms(t2, t3), ms(t3, t4), groups.size(),
                 cands.size());
  }
  return out;
}

// Expand a cell instance placed with its bbox-min at (bx, by), oriented `orient`.
void expand(const Hierarchy& h, int cell, int orient, double bx, double by,
            std::vector<Rect>& out) {
  std::vector<Member> tb = transform_body(h.cells[cell - 1].members, orient);
  double offx = 1e300, offy = 1e300;
  for (const auto& m : tb) { offx = std::min(offx, m.dx); offy = std::min(offy, m.dy); }
  for (const auto& m : tb) {
    double x = bx + m.dx - offx, y = by + m.dy - offy;
    if (m.cell == 0) out.push_back({x, y, x + m.w, y + m.h});
    else expand(h, m.cell, m.orient, x, y, out);
  }
}

}  // namespace

std::vector<Rect> flatten(const Hierarchy& h) {
  std::vector<Rect> out;
  for (const auto& p : h.top) expand(h, p.cell, p.orient, p.x, p.y, out);
  for (const auto& r : h.residual) out.push_back(r);
  return out;
}

LatticeFit fit_lattice(const std::vector<std::array<double, 2>>& anchors, double tol) {
  LatticeFit f;
  if (anchors.size() < 2) return f;

  // Distinct, sorted coordinate values along each axis; the step is the smallest
  // positive gap, and the lattice is valid only if every gap is an integer
  // multiple of that step (a regular grid, possibly with holes).
  auto axis_step = [&](std::vector<double> v, double& origin, double& step) {
    std::sort(v.begin(), v.end());
    std::vector<double> uniq;
    for (double x : v)
      if (uniq.empty() || x - uniq.back() > tol) uniq.push_back(x);
    origin = uniq.front();
    step = 0;
    for (std::size_t i = 1; i < uniq.size(); ++i)
      if (double d = uniq[i] - uniq[i - 1]; step == 0 || d < step) step = d;
    if (step <= 0) return false;
    for (std::size_t i = 1; i < uniq.size(); ++i) {
      double m = (uniq[i] - uniq[i - 1]) / step;
      if (std::abs(m - std::lround(m)) > tol / step) return false;
    }
    return true;
  };

  std::vector<double> xs, ys;
  for (const auto& a : anchors) { xs.push_back(a[0]); ys.push_back(a[1]); }
  bool okx = axis_step(xs, f.ox, f.dx), oky = axis_step(ys, f.oy, f.dy);
  if (!okx || !oky) return f;

  double mxx = f.ox, mxy = f.oy;
  for (const auto& a : anchors) { mxx = std::max(mxx, a[0]); mxy = std::max(mxy, a[1]); }
  f.ncols = (int)std::lround((mxx - f.ox) / f.dx) + 1;
  f.nrows = (int)std::lround((mxy - f.oy) / f.dy) + 1;

  // Every anchor must land on an integer lattice site.
  std::set<std::pair<int, int>> sites;
  for (const auto& a : anchors) {
    double ci = (a[0] - f.ox) / f.dx, ri = (a[1] - f.oy) / f.dy;
    if (std::abs(ci - std::lround(ci)) > tol / f.dx ||
        std::abs(ri - std::lround(ri)) > tol / f.dy)
      return f;
    sites.insert({(int)std::lround(ci), (int)std::lround(ri)});
  }
  f.occupied = (int)sites.size();
  f.missing = f.ncols * f.nrows - f.occupied;
  f.ok = true;
  return f;
}

DropoffCurve dropoff_curve(const std::vector<Rect>& layout, const RecoverConfig& cfg) {
  std::vector<Prim> prims;
  prims.reserve(layout.size());
  for (const auto& r : layout) prims.push_back({0, r, 0});
  RTree tree = build_rtree(prims);
  return compute_curve(prims, tree, cfg);
}

namespace {
struct PlacementGroupKey {
  int cell = 0;
  int orient = 0;
  bool operator<(const PlacementGroupKey& o) const {
    return std::make_pair(cell, orient) < std::make_pair(o.cell, o.orient);
  }
};

struct OneDRun {
  std::vector<int> ids;
  double origin = 0;
  double step = 0;
  int count = 0;
  int missing = 0;
};

bool regular_1d_run(const std::vector<int>& ids, const std::vector<double>& coord,
                    double tol, OneDRun& out) {
  if (ids.size() < 2) return false;
  std::vector<int> sorted = ids;
  std::sort(sorted.begin(), sorted.end(), [&](int a, int b) {
    return coord[a] < coord[b];
  });

  double step = 0;
  for (std::size_t i = 1; i < sorted.size(); ++i) {
    double d = coord[sorted[i]] - coord[sorted[i - 1]];
    if (d <= tol) continue;
    if (step == 0 || d < step) step = d;
  }
  if (step <= 0) return false;

  for (std::size_t i = 1; i < sorted.size(); ++i) {
    double d = coord[sorted[i]] - coord[sorted[i - 1]];
    double m = d / step;
    if (std::abs(m - std::lround(m)) > tol / step) return false;
  }

  int count = (int)std::lround((coord[sorted.back()] - coord[sorted.front()]) / step) + 1;
  out.ids = std::move(sorted);
  out.origin = coord[out.ids.front()];
  out.step = step;
  out.count = count;
  out.missing = count - (int)out.ids.size();
  return true;
}

std::vector<OneDRun> split_regular_1d_runs(const std::vector<int>& ids,
                                           const std::vector<double>& coord,
                                           double tol, int min_instances) {
  std::vector<int> sorted = ids;
  std::sort(sorted.begin(), sorted.end(), [&](int a, int b) {
    return coord[a] < coord[b];
  });

  std::vector<OneDRun> runs;
  std::vector<int> cur;
  double step = 0;
  for (int id : sorted) {
    if (cur.empty()) {
      cur.push_back(id);
      continue;
    }
    double d = coord[id] - coord[cur.back()];
    bool split = false;
    if (d > tol) {
      if (step == 0) step = d;
      else {
        double m = d / step;
        split = std::abs(m - std::lround(m)) > tol / step;
      }
    }
    if (split) {
      OneDRun run;
      if ((int)cur.size() >= min_instances && regular_1d_run(cur, coord, tol, run))
        runs.push_back(std::move(run));
      cur.clear();
      step = 0;
    }
    cur.push_back(id);
  }
  OneDRun run;
  if ((int)cur.size() >= min_instances && regular_1d_run(cur, coord, tol, run))
    runs.push_back(std::move(run));
  return runs;
}
}

void build_array_nodes(Hierarchy& hier, const RecoverConfig& cfg) {
  hier.arrays.clear();
  hier.array_cost = hier.hier_cost;
  if (hier.top.empty() || cfg.min_array_instances <= 0) return;

  std::map<PlacementGroupKey, std::vector<int>> groups;
  for (int i = 0; i < (int)hier.top.size(); ++i)
    groups[{hier.top[i].cell, hier.top[i].orient}].push_back(i);

  std::vector<char> covered(hier.top.size(), 0);
  int array_ref_cost = 0;
  int covered_refs = 0;
  for (const auto& kv : groups) {
    const auto& ids = kv.second;
    if ((int)ids.size() < cfg.min_array_instances) continue;

    std::vector<std::array<double, 2>> anchors;
    anchors.reserve(ids.size());
    for (int id : ids) anchors.push_back({hier.top[id].x, hier.top[id].y});
    LatticeFit lf = fit_lattice(anchors);
    if (!lf.ok || lf.occupied != (int)ids.size()) continue;

    // Cost one array reference plus explicit missing-site exceptions. This is a
    // first-pass model; a future bitmap/run-length model can refine sparse cases.
    int node_cost = 1 + lf.missing;
    if (node_cost >= (int)ids.size()) continue;

    hier.arrays.push_back({kv.first.cell, kv.first.orient, lf.ox, lf.oy, lf.dx,
                           lf.dy, lf.ncols, lf.nrows, lf.occupied, lf.missing});
    array_ref_cost += node_cost;
    covered_refs += (int)ids.size();
    for (int id : ids) covered[id] = 1;
  }

  // If a whole group is not one lattice, recover regular 1D rows/columns. This
  // catches dense datapaths where block gutters break one global pitch but each
  // slice row is still an array.
  const double tol = 1.0;
  for (const auto& kv : groups) {
    std::vector<int> open;
    for (int id : kv.second) if (!covered[id]) open.push_back(id);
    if ((int)open.size() < cfg.min_array_instances) continue;

    std::vector<double> xs(hier.top.size()), ys(hier.top.size());
    for (int id : open) { xs[id] = hier.top[id].x; ys[id] = hier.top[id].y; }

    std::map<long, std::vector<int>> by_row;
    for (int id : open) by_row[qbin(hier.top[id].y, tol)].push_back(id);
    for (const auto& row : by_row) {
      for (const auto& run : split_regular_1d_runs(row.second, xs, tol,
                                                   cfg.min_array_instances)) {
        int node_cost = 1 + run.missing;
        if (node_cost >= (int)run.ids.size()) continue;
        int first = run.ids.front();
        hier.arrays.push_back({kv.first.cell, kv.first.orient, run.origin,
                               hier.top[first].y, run.step, 0.0, run.count, 1,
                               (int)run.ids.size(), run.missing});
        array_ref_cost += node_cost;
        covered_refs += (int)run.ids.size();
        for (int id : run.ids) covered[id] = 1;
      }
    }

    open.clear();
    for (int id : kv.second) if (!covered[id]) open.push_back(id);
    if ((int)open.size() < cfg.min_array_instances) continue;

    std::map<long, std::vector<int>> by_col;
    for (int id : open) by_col[qbin(hier.top[id].x, tol)].push_back(id);
    for (const auto& col : by_col) {
      for (const auto& run : split_regular_1d_runs(col.second, ys, tol,
                                                   cfg.min_array_instances)) {
        int node_cost = 1 + run.missing;
        if (node_cost >= (int)run.ids.size()) continue;
        int first = run.ids.front();
        hier.arrays.push_back({kv.first.cell, kv.first.orient, hier.top[first].x,
                               run.origin, 0.0, run.step, 1, run.count,
                               (int)run.ids.size(), run.missing});
        array_ref_cost += node_cost;
        covered_refs += (int)run.ids.size();
        for (int id : run.ids) covered[id] = 1;
      }
    }
  }

  if (hier.arrays.empty()) return;

  int body_cost = 0;
  for (const auto& c : hier.cells) body_cost += (int)c.members.size();
  int uncovered_refs = (int)hier.top.size() - covered_refs;
  hier.array_cost = body_cost + (int)hier.residual.size() + uncovered_refs + array_ref_cost;
}

Hierarchy recover_hierarchy(const std::vector<Rect>& layout, const RecoverConfig& cfg) {
  Hierarchy hier;
  hier.flat_leaf_count = (int)layout.size();

  const double q = cfg.quantum;
  auto rkey = [&](const Rect& r) {
    return RKey{qbin(r.x0, q), qbin(r.y0, q), qbin(r.x1, q), qbin(r.y1, q)};
  };
  std::unordered_map<RKey, int, RKeyHash> gcount;
  gcount.reserve(layout.size() * 2);
  for (const auto& r : layout) gcount[rkey(r)]++;

  // Exact-count validity of a candidate primitive state against G: every REAL
  // rectangle reproduced with exactly its multiplicity (under-covering = missing
  // geometry, over-covering = overlapping instances / an invalid tiling), with
  // flatten-only rectangles being the genuine defect layer. O(n) via hashing —
  // this is what replaced the O(n^2) iterated Boost union. Recovery groups and
  // reconstructs existing rectangles exactly (never merges/refractures), so a set
  // comparison is sound; fractured polygons will need candidate-level canonical
  // equality (verify.cpp), but the global check stays a set comparison.
  auto check_state = [&](const std::vector<Prim>& cur, double& defect) {
    Hierarchy tmp;
    tmp.cells = hier.cells;
    for (const auto& p : cur) {
      if (is_leaf(p)) tmp.residual.push_back(p.box);
      else tmp.top.push_back({p.cell, ax(p), ay(p), p.orient});
    }
    std::vector<Rect> flat = flatten(tmp);
    std::unordered_map<RKey, int, RKeyHash> fcount;
    fcount.reserve(flat.size() * 2);
    for (const auto& r : flat) fcount[rkey(r)]++;
    bool valid = true;
    for (const auto& kv : gcount) {
      auto it = fcount.find(kv.first);
      if (it == fcount.end() || it->second != kv.second) { valid = false; break; }
    }
    defect = 0;
    for (const auto& kv : fcount)
      if (!gcount.count(kv.first))
        defect += kv.second * (kv.first.x1 - kv.first.x0) *
                  (kv.first.y1 - kv.first.y0) * q * q;
    return valid;
  };

  std::vector<Prim> prims;
  prims.reserve(layout.size());
  for (const auto& r : layout) prims.push_back({0, r, 0});
  double defect = 0;

  // Recurse ONLY while the hierarchy stays exact. Greedy grouping on a dense
  // periodic region can produce an over-covering (invalid) round; rather than
  // emit an invalid hierarchy we revert that round and stop. Isolated nesting
  // recurses fully; dense arrays stop early (and are the array detector's job).
  for (int level = 1; level <= cfg.max_levels; ++level) {
    std::size_t snap_cells = hier.cells.size();
    int snap_def = hier.n_defective;
    std::size_t snap_miss = hier.missing_geometry.size();
    bool changed = false;
    std::vector<Prim> next = round(prims, cfg, hier, level, changed);
    if (!changed) break;
    double d;
    if (!check_state(next, d)) {  // this round broke exactness — undo it
      hier.cells.resize(snap_cells);
      hier.n_defective = snap_def;
      hier.missing_geometry.resize(snap_miss);
      break;
    }
    prims = std::move(next);
    hier.levels = level;
    defect = d;
  }

  for (const auto& p : prims) {
    if (is_leaf(p)) hier.residual.push_back(p.box);
    else hier.top.push_back({p.cell, ax(p), ay(p), p.orient});
  }
  int cost = (int)hier.top.size() + (int)hier.residual.size();
  for (const auto& c : hier.cells) cost += (int)c.members.size();
  hier.hier_cost = cost;
  hier.array_cost = cost;
  hier.flatten_matches = true;  // only valid rounds were kept
  hier.missing_area = defect;
  build_array_nodes(hier, cfg);
  return hier;
}

}  // namespace adt::hr
