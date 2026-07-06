#include "adt/lattice.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>

#include "adt/recover.hpp"  // flatten()

namespace adt::hr {

namespace {

long qb(double v, double q) { return std::lround(v / q); }

using ShapeSet = std::map<std::array<long, 4>, int>;  // (x0,y0,w,h) in quanta -> count

// Self-similarity of translating the whole layout by d along `axis`: how many
// rectangles land exactly on a same-shape rectangle. This is what actually
// distinguishes a translational PERIOD (shapes map onto identical shapes) from a
// mirror pitch (they don't) or an intra-tile gap (only a few do) — ranking by raw
// gap frequency picks the small intra-tile gaps instead, which is wrong.
int self_score(const std::vector<Rect>& L, const ShapeSet& S, int axis, double d,
               double q) {
  int s = 0;
  for (const auto& r : L) {
    double x = r.x0 + (axis == 0 ? d : 0), y = r.y0 + (axis == 0 ? 0 : d);
    if (S.count({qb(x, q), qb(y, q), qb(r.width(), q), qb(r.height(), q)})) ++s;
  }
  return s;
}

// Interior-normalized periodicity: of the rectangles that HAVE a translate-by-d
// partner slot within the extent, the fraction that actually land on a same-shape
// rectangle. ~1 for a clean array (only the boundary column/row lacks a partner);
// it drops by the foreign/defect fraction. This is the necessary-condition test —
// a true 2D array must score high on BOTH axes — so it both strengthens the
// hypothesis and gates out spurious lattices. Boundary-robust, unlike self_score/n.
double interior_periodicity(const std::vector<Rect>& L, const ShapeSet& S, int axis,
                            double d, double q) {
  double maxc = -1e300;
  for (const auto& r : L) maxc = std::max(maxc, axis == 0 ? r.x0 : r.y0);
  long expected = 0, hit = 0;
  for (const auto& r : L) {
    double c = axis == 0 ? r.x0 : r.y0;
    if (c + d > maxc + q) continue;  // boundary slot: no partner expected
    ++expected;
    double x = r.x0 + (axis == 0 ? d : 0), y = r.y0 + (axis == 0 ? 0 : d);
    if (S.count({qb(x, q), qb(y, q), qb(r.width(), q), qb(r.height(), q)})) ++hit;
  }
  return expected ? (double)hit / (double)expected : 0.0;
}

// Candidate translational periods along one axis as (step, self_score) pairs,
// ranked by self-similarity. Raw candidates are the distinct same-row (same-
// column) same-shape gaps — which include the true period and its multiples even
// a few shapes apart; scoring by self_score is what separates a real period from
// the mirror pitch or an intra-tile spacing (both have many gaps but low score).
std::vector<std::pair<double, int>> candidate_steps(const std::vector<Rect>& L,
                                                    const ShapeSet& S, int axis,
                                                    double q) {
  std::map<std::array<long, 3>, std::vector<double>> groups;
  for (const auto& r : L) {
    double other = axis == 0 ? r.y0 : r.x0;
    double coord = axis == 0 ? r.x0 : r.y0;
    groups[{qb(r.width(), q), qb(r.height(), q), qb(other, q)}].push_back(coord);
  }
  std::map<long, int> cand;  // step-in-quanta -> raw votes (only to dedup)
  for (auto& g : groups) {
    auto& v = g.second;
    std::sort(v.begin(), v.end());
    for (std::size_t i = 0; i < v.size(); ++i)
      for (std::size_t j = i + 1; j < v.size() && j <= i + 10; ++j) {
        double d = v[j] - v[i];
        if (d > 1.0) cand[qb(d, q)]++;
      }
  }
  std::vector<std::pair<double, int>> scored;  // (step, self_score)
  for (auto& c : cand) {
    double d = c.first * q;
    scored.push_back({d, self_score(L, S, axis, d, q)});
  }
  std::sort(scored.begin(), scored.end(), [](auto& a, auto& b) {
    if (a.second != b.second) return a.second > b.second;  // best score first
    return a.first < b.first;                              // then smallest step
  });
  int best = scored.empty() ? 0 : scored.front().second;
  std::vector<std::pair<double, int>> out;
  for (auto& s : scored) {
    if (s.second < best / 2) break;  // keep only strongly self-similar periods
    out.push_back(s);
    if (out.size() >= 6) break;
  }
  return out;
}

// The FUNDAMENTAL generator: the smallest step whose self-similarity is within
// `frac` of the best. A sub-period (e.g. a mirror pitch) scores lower and is
// rejected; a multiple scores as high but is larger, so "smallest near-best" is
// the true period.
double fundamental(const std::vector<std::pair<double, int>>& scored, double frac) {
  if (scored.empty()) return 0;
  int best = 0;
  for (auto& s : scored) best = std::max(best, s.second);
  double d = scored.front().first;
  for (auto& s : scored)
    if (s.second >= frac * best && s.first < d) d = s.first;
  return d;
}

using CellKey = std::pair<long, long>;  // (i, j)
using PosKey = std::array<long, 4>;     // (x0, y0, w, h) in quanta

// Build an exact tile hierarchy for a fixed lattice (dx, dy). Each rectangle is
// assigned to the cell its min-corner falls in. The tile is taken as the exact
// content of a REFERENCE cell of the MODAL size (the typical full cell, robust to
// clutter and edge cells), and is then placed at every cell that carries all of
// its members translated — anchor-modulo assignment, so a tile that extends past
// its own cell is still matched. flatten(H) == G by construction (covered rects
// are regenerated once each; the rest is residual).
Hierarchy build_for(const std::vector<Rect>& L, double dx, double dy, double q,
                    LatticeInfo& info) {
  Hierarchy empty;
  if (dx <= 0 || dy <= 0) return empty;

  double ox = 1e300, oy = 1e300, mx = -1e300, my = -1e300;
  for (const auto& r : L) {
    ox = std::min(ox, r.x0); oy = std::min(oy, r.y0);
    mx = std::max(mx, r.x0); my = std::max(my, r.y0);
  }
  const double eps = q;
  auto reduce = [&](double v, double o, double d, long& cell, double& rel) {
    double f = v - o;
    cell = (long)std::floor(f / d + 1e-9);
    rel = f - cell * d;
    if (rel >= d - eps) { rel -= d; cell += 1; }
    if (rel < 0) rel = 0;
  };

  std::map<CellKey, std::vector<int>> cell_rects;
  std::vector<std::pair<double, double>> rel(L.size());
  for (std::size_t r = 0; r < L.size(); ++r) {
    long i, j; double rx, ry;
    reduce(L[r].x0, ox, dx, i, rx);
    reduce(L[r].y0, oy, dy, j, ry);
    rel[r] = {rx, ry};
    cell_rects[{i, j}].push_back((int)r);
  }
  if (cell_rects.size() < 2) return empty;

  // Reference cell: one whose rect-count equals the MODE (the typical cell), so a
  // clutter-heavy or edge cell doesn't define the tile.
  std::map<std::size_t, int> size_hist;
  for (auto& kv : cell_rects) size_hist[kv.second.size()]++;
  std::size_t modal = 0; int modal_n = -1;
  for (auto& kv : size_hist) if (kv.second > modal_n) { modal_n = kv.second; modal = kv.first; }
  if (modal < 2) return empty;
  const std::vector<int>* ref = nullptr;
  for (auto& kv : cell_rects) if (kv.second.size() == modal) { ref = &kv.second; break; }

  // Tile = the reference cell's rects, anchored at their min corner.
  struct TM { double rx, ry, w, h; };
  std::vector<TM> tm;
  double ax = 1e300, ay = 1e300;
  for (int r : *ref) {
    tm.push_back({rel[r].first, rel[r].second, L[r].width(), L[r].height()});
    ax = std::min(ax, rel[r].first); ay = std::min(ay, rel[r].second);
  }
  const int M = (int)tm.size();

  // Multiset (count-aware) matching: real flattened geometry has coincident
  // rectangles (e.g. SRAM bitlines/wordlines shared between abutting cells), so a
  // position maps to a COUNT, not a single rect. A placement is accepted only when
  // every member it needs is still available; those copies are then consumed. This
  // keeps a disjoint, non-overlapping tiling and makes flatten(H) == G exact by
  // construction (consumed copies are regenerated once each; leftovers are residual).
  auto pk = [&](double x, double y, double w, double hh) {
    return PosKey{qb(x, q), qb(y, q), qb(w, q), qb(hh, q)};
  };
  auto hashfn = [](const PosKey& k) {
    std::size_t h = 1469598103934665603ULL;
    for (long v : k) { h ^= (std::size_t)v; h *= 1099511628211ULL; }
    return h;
  };
  std::unordered_map<PosKey, int, decltype(hashfn)> avail(L.size() * 2, hashfn);
  std::unordered_map<PosKey, Rect, decltype(hashfn)> rep(L.size() * 2, hashfn);
  for (const auto& r : L) {
    PosKey k = pk(r.x0, r.y0, r.width(), r.height());
    avail[k]++; rep[k] = r;
  }

  Hierarchy h;
  int placements = 0;
  for (auto& kv : cell_rects) {
    long i = kv.first.first, j = kv.first.second;
    double cx = ox + i * dx, cy = oy + j * dy;
    // Required multiset for this placement.
    std::unordered_map<PosKey, int, decltype(hashfn)> need(M * 2, hashfn);
    for (const auto& m : tm) need[pk(cx + m.rx, cy + m.ry, m.w, m.h)]++;
    bool ok = true;
    for (auto& n : need) { auto it = avail.find(n.first); if (it == avail.end() || it->second < n.second) { ok = false; break; } }
    if (!ok) continue;
    for (auto& n : need) avail[n.first] -= n.second;  // consume
    h.top.push_back({1, cx + ax, cy + ay, 0});
    ++placements;
  }
  if (placements < 2) return empty;

  CellDef tile;
  tile.id = 1; tile.level = 1; tile.leaf_count = M;
  tile.members.resize(M);
  double tmx = -1e300, tmy = -1e300;
  for (int k = 0; k < M; ++k) {
    Member m; m.cell = 0; m.orient = 0;
    m.dx = tm[k].rx - ax; m.dy = tm[k].ry - ay; m.w = tm[k].w; m.h = tm[k].h;
    tile.members[k] = m;
    tmx = std::max(tmx, m.dx + m.w); tmy = std::max(tmy, m.dy + m.h);
  }
  tile.w = tmx; tile.h = tmy;
  h.cells.push_back(tile);

  for (auto& kv : avail)
    for (int c = 0; c < kv.second; ++c) h.residual.push_back(rep[kv.first]);

  h.flat_leaf_count = (int)L.size();
  int cost = (int)h.top.size() + (int)h.residual.size();
  for (const auto& c : h.cells) cost += (int)c.members.size();
  h.hier_cost = cost; h.array_cost = cost; h.levels = 1; h.flatten_matches = true;

  info.ok = true; info.dx = dx; info.dy = dy; info.ox = ox; info.oy = oy;
  info.ncols = (int)std::lround((mx - ox) / dx) + 1;
  info.nrows = (int)std::lround((my - oy) / dy) + 1;
  info.tile_members = M; info.placements = placements;
  info.residual = (int)h.residual.size();
  return h;
}

bool multiset_equal(const std::vector<Rect>& a, const std::vector<Rect>& b, double q) {
  if (a.size() != b.size()) return false;
  std::map<std::array<long, 4>, int> c;
  for (const auto& r : a) c[{qb(r.x0, q), qb(r.y0, q), qb(r.x1, q), qb(r.y1, q)}]++;
  for (const auto& r : b) {
    auto k = std::array<long, 4>{qb(r.x0, q), qb(r.y0, q), qb(r.x1, q), qb(r.y1, q)};
    auto it = c.find(k);
    if (it == c.end() || it->second == 0) return false;
    it->second--;
  }
  for (auto& kv : c) if (kv.second) return false;
  return true;
}

}  // namespace

Hierarchy recover_lattice(const std::vector<Rect>& layout, const RecoverConfig& cfg,
                          LatticeInfo* info) {
  LatticeInfo local;
  LatticeInfo& out = info ? *info : local;
  out = LatticeInfo{};
  if (layout.size() < 8) return {};

  const double q = cfg.quantum;
  const double gate = cfg.min_periodicity;
  const bool dbg = std::getenv("ADT_LAT") != nullptr;
  ShapeSet S;
  for (const auto& r : layout) S[{qb(r.x0, q), qb(r.y0, q), qb(r.width(), q), qb(r.height(), q)}]++;

  auto sx = candidate_steps(layout, S, 0, q);
  auto sy = candidate_steps(layout, S, 1, q);
  if (sx.empty() || sy.empty()) return {};

  // --- Two-axis periodicity gate (the necessary-condition cross-check). --------
  // A 2D array forces 1D periodicity on BOTH axes (§3). Score the fundamental
  // generator of each axis: if the geometry does not translate onto itself under
  // both dx and dy (up to boundary + a tolerated foreign/defect fraction), it is
  // not a 2D array and we decline — cheaply, without any tiling. This is also what
  // lets us *claim* an array once both scores are high: two full 1D translation-
  // invariances imply invariance under the whole lattice group.
  double dx0 = fundamental(sx, 0.85), dy0 = fundamental(sy, 0.85);
  double px = interior_periodicity(layout, S, 0, dx0, q);
  double py = interior_periodicity(layout, S, 1, dy0, q);
  if (dbg)
    std::fprintf(stderr, "[lattice] n=%zu fundamental dx=%.1f (px=%.2f) dy=%.1f (py=%.2f) gate=%.2f\n",
                 layout.size(), dx0, px, dy0, py, gate);
  if (px < gate || py < gate) return {};  // fails the necessary condition -> not an array

  auto accept = [&](Hierarchy&& h, LatticeInfo li, bool fast) {
    li.periodicity_x = px; li.periodicity_y = py; li.fast_path = fast;
    out = li;
    return std::move(h);
  };
  auto covers = [&](const LatticeInfo& li) {
    return (int)layout.size() - li.residual >= (int)(0.3 * layout.size());
  };

  // --- Fast path: both axes are strongly periodic, so tile at the fundamental
  // generators and verify once — no Cartesian combo search. ---------------------
  {
    LatticeInfo li;
    Hierarchy h = build_for(layout, dx0, dy0, q, li);
    if (!h.cells.empty() && multiset_equal(flatten(h), layout, q) && covers(li)) {
      if (dbg) std::fprintf(stderr, "[lattice] FAST PATH: tile=%d place=%d resid=%d\n",
                            li.tile_members, li.placements, li.residual);
      return accept(std::move(h), li, true);
    }
  }

  // --- Fallback: the fundamental tiling was thin (e.g. a partial/edge crop);
  // search the candidate combos for the tiling that covers the most, tie-broken
  // toward more placements (smaller, more reusable tile). The array-ness decision
  // was already made by the gate above; this only chooses the representation. ---
  Hierarchy best; LatticeInfo best_info; int best_covered = -1, best_place = -1;
  for (auto& cx : sx) {
    for (auto& cy : sy) {
      LatticeInfo li;
      Hierarchy h = build_for(layout, cx.first, cy.first, q, li);
      if (h.cells.empty() || !multiset_equal(flatten(h), layout, q)) continue;
      int covered = (int)layout.size() - li.residual;
      if (dbg)
        std::fprintf(stderr, "[lattice]  dx=%.1f dy=%.1f -> tile=%d place=%d resid=%d covered=%d\n",
                     cx.first, cy.first, li.tile_members, li.placements, li.residual, covered);
      if (covered > best_covered ||
          (covered == best_covered && li.placements > best_place)) {
        best = h; best_info = li; best_covered = covered; best_place = li.placements;
      }
    }
  }
  if (best.cells.empty() || !covers(best_info)) return {};
  return accept(std::move(best), best_info, false);
}

}  // namespace adt::hr
