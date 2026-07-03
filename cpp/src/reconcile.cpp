#include "adt/reconcile.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace adt {

namespace {
constexpr double kEps = 1e-6;

double overlap(double lo1, double hi1, double lo2, double hi2) {
  return std::min(hi1, hi2) - std::max(lo1, lo2);
}

double median(std::vector<double> v) {
  if (v.empty()) return 0.0;
  std::sort(v.begin(), v.end());
  std::size_t n = v.size();
  return (n % 2) ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

// Circular median (§2.4): the phase minimizing Σ dist_mod(φ, φᵢ; p). No closed
// form because phase wraps mod p, so we evaluate each observed φᵢ (and their
// pairwise midpoints, which can beat any single sample) as a candidate.
double circular_median(const std::vector<double>& phases, double p) {
  if (phases.empty()) return 0.0;
  if (p <= kEps) return phases.front();
  std::vector<double> cands = phases;
  for (std::size_t i = 0; i < phases.size(); ++i)
    for (std::size_t j = i + 1; j < phases.size(); ++j) {
      double m = 0.5 * (phases[i] + phases[j]);
      cands.push_back(std::fmod(m, p));
      cands.push_back(std::fmod(m + 0.5 * p, p));  // the antipodal midpoint too
    }
  double best = phases.front(), best_cost = 1e300;
  for (double c : cands) {
    double cost = 0.0;
    for (double ph : phases) cost += dist_mod(c, ph, p);
    if (cost < best_cost) { best_cost = cost; best = c; }
  }
  return best;
}

struct DSU {
  std::vector<int> p;
  explicit DSU(int n) : p(n) { std::iota(p.begin(), p.end(), 0); }
  int find(int x) { return p[x] == x ? x : (p[x] = find(p[x])); }
  void unite(int a, int b) { p[find(a)] = find(b); }
};
}  // namespace

double dist_mod(double a, double b, double p) {
  if (p <= kEps) return std::abs(a - b);
  double d = std::fmod(std::abs(a - b), p);
  if (d < 0) d += p;
  return std::min(d, p - d);
}

std::vector<LineCluster> cluster_runs(const std::vector<LineRun>& runs,
                                      const ReconcileParams& params) {
  const int n = static_cast<int>(runs.size());
  std::vector<LineCluster> out;
  if (n == 0) return out;

  // --- Connected components under the pairwise compatibility predicate. Each
  // pair is judged on its OWN periods/phase/overlap, so the graph structure is
  // genuinely pairwise; the weak-link risk is handled by center-pruning below. -
  DSU dsu(n);
  auto compatible = [&](const LineRun& a, const LineRun& b) {
    if (std::abs(a.period - b.period) > params.eps_period) return false;
    double p = 0.5 * (a.period + b.period);
    if (dist_mod(a.phase, b.phase, p) > params.eps_phase) return false;
    if (overlap(a.scan_lo, a.scan_hi, b.scan_lo, b.scan_hi) < params.w_min)
      return false;
    return true;
  };
  for (int i = 0; i < n; ++i)
    for (int j = i + 1; j < n; ++j)
      if (compatible(runs[i], runs[j])) dsu.unite(i, j);

  std::vector<std::vector<int>> comps(n);
  for (int i = 0; i < n; ++i) comps[dsu.find(i)].push_back(i);

  for (auto& comp : comps) {
    if (static_cast<int>(comp.size()) < params.min_members) continue;

    // Robust center from all members, then prune members incompatible with that
    // center and recompute once. This is what stops a drifting transitive chain
    // (A~B~C but A≁C) from being accepted wholesale (§2.4 weak-link caution).
    auto build = [&](const std::vector<int>& members) {
      std::vector<double> periods, phases;
      for (int idx : members) {
        periods.push_back(runs[idx].period);
        phases.push_back(runs[idx].phase);
      }
      double per = median(periods);
      double ph = circular_median(phases, per);
      return std::pair<double, double>{per, ph};
    };
    auto [cper, cph] = build(comp);
    std::vector<int> kept;
    for (int idx : comp) {
      if (std::abs(runs[idx].period - cper) <= params.eps_period &&
          dist_mod(runs[idx].phase, cph, cper) <= params.eps_phase)
        kept.push_back(idx);
    }
    if (static_cast<int>(kept.size()) < params.min_members) continue;
    std::tie(cper, cph) = build(kept);

    // Spatial extent = the region the cluster's evidence COVERS (union of member
    // supports), not the region they all share (intersection). The compatibility
    // predicate already guarantees members pairwise-overlap by w_min, so members
    // are genuine co-lattice evidence; the intersection would wrongly collapse
    // the reported rectangle to a single row/column when supports vary (a
    // truncated row, a column that only spans part of the array, etc.).
    LineCluster lc;
    lc.period = cper;
    lc.phase = cph;
    lc.scan_lo = 1e300;
    lc.scan_hi = -1e300;
    lc.norm_lo = 1e300;
    lc.norm_hi = -1e300;
    for (int idx : kept) {
      lc.scan_lo = std::min(lc.scan_lo, runs[idx].scan_lo);
      lc.scan_hi = std::max(lc.scan_hi, runs[idx].scan_hi);
      lc.norm_lo = std::min(lc.norm_lo, runs[idx].norm_lo);
      lc.norm_hi = std::max(lc.norm_hi, runs[idx].norm_hi);
      lc.run_indices.push_back(runs[idx].run_index);
    }
    lc.n_members = static_cast<int>(kept.size());
    out.push_back(std::move(lc));
  }
  return out;
}

std::vector<ArrayCandidate> intersect_passes(
    const std::vector<LineCluster>& h_clusters,
    const std::vector<LineCluster>& v_clusters,
    const std::vector<RowRun>& h_runs, const std::vector<RowRun>& v_runs,
    const ReconcileParams& params) {
  std::vector<ArrayCandidate> out;
  for (const auto& h : h_clusters) {
    for (const auto& v : v_clusters) {
      // Horizontal: scan = X-support, norm = Y-extent.
      // Vertical:   scan = Y-support, norm = X-extent.
      double x0 = std::max(h.scan_lo, v.norm_lo);
      double x1 = std::min(h.scan_hi, v.norm_hi);
      double y0 = std::max(h.norm_lo, v.scan_lo);
      double y1 = std::min(h.norm_hi, v.scan_hi);
      if (x1 - x0 < std::max(params.w_min, h.period) ||
          y1 - y0 < std::max(params.w_min, v.period))
        continue;

      ArrayCandidate c;
      c.bbox = {x0, y0, x1, y1};
      c.dx = h.period;
      c.dy = v.period;
      c.phase_x = h.phase;
      c.phase_y = v.phase;
      // Carry the strongest classification seen among the horizontal members.
      for (int ri : h.run_indices) {
        if (h_runs[ri].classification == Classification::Supercell) {
          c.classification = Classification::Supercell;
          c.supercell_k = std::max(c.supercell_k, h_runs[ri].supercell_k);
        }
      }
      out.push_back(std::move(c));
    }
  }
  return out;
}

}  // namespace adt
