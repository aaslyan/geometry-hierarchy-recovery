#include "adt/runs.hpp"

#include <algorithm>
#include <cmath>
#include <map>

namespace adt {

namespace {
constexpr double kEps = 1e-6;

// Majority-vote canonical unit of length p over a token stream.
std::vector<Token> majority_unit(const std::vector<Token>& tok, int p) {
  std::vector<std::map<std::int32_t, int>> counts(p);
  std::vector<std::map<std::int32_t, Token>> exemplar(p);
  for (std::size_t i = 0; i < tok.size(); ++i) {
    int r = static_cast<int>(i % p);
    counts[r][tok[i].symbol]++;
    exemplar[r][tok[i].symbol] = tok[i];
  }
  std::vector<Token> unit(p);
  for (int r = 0; r < p; ++r) {
    std::int32_t best_sym = 0;
    int best_n = -1;
    for (const auto& kv : counts[r]) {
      if (kv.second > best_n) { best_n = kv.second; best_sym = kv.first; }
    }
    unit[r] = exemplar[r][best_sym];
  }
  return unit;
}

// Fraction of positions whose symbol matches the majority unit at that residue.
double self_consistency(const std::vector<Token>& tok, const std::vector<Token>& unit,
                        int p) {
  if (tok.empty()) return 0.0;
  int match = 0;
  for (std::size_t i = 0; i < tok.size(); ++i) {
    if (tok[i].symbol == unit[i % p].symbol) ++match;
  }
  return static_cast<double>(match) / static_cast<double>(tok.size());
}

// Compare two sorted interval lists for physical equality within tolerance. Used
// by the defect scan: expected (from the canonical unit) vs observed (clipped to
// the period window). Abutted cells are kept distinct on both sides, so a merge
// can't hide a boundary and a missing cell can't be masked by a neighbor.
bool intervals_match(const std::vector<Interval>& a, const std::vector<Interval>& b,
                     double tol) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (std::abs(a[i].lo - b[i].lo) > tol || std::abs(a[i].hi - b[i].hi) > tol)
      return false;
  }
  return true;
}

// Filled intervals of `view` clipped to [lo, hi), rebased so lo becomes 0. A
// zero-width sliver from a boundary graze is dropped.
std::vector<Interval> clip_rebased(const std::vector<Interval>& view, double lo,
                                   double hi, double tol) {
  std::vector<Interval> out;
  for (const auto& iv : view) {
    double a = std::max(iv.lo, lo), b = std::min(iv.hi, hi);
    if (b - a > tol) out.push_back({a - lo, b - lo});
  }
  std::sort(out.begin(), out.end(),
            [](const Interval& p, const Interval& q) { return p.lo < q.lo; });
  return out;
}
}  // namespace

std::optional<RowRun> analyze_row(int slab_index, const Slab& slab,
                                  const TokenStream& ts,
                                  const std::vector<Interval>& filled_view,
                                  const RunParams& params) {
  const auto& tok = ts.tokens;
  const auto& starts = ts.starts;
  const int n = static_cast<int>(tok.size());
  if (n < params.min_repeats * 2) return std::nullopt;

  // --- Bug 2 fix: choose the period by self-consistency RELATIVE to the best
  // achievable, not an absolute floor. A true period P reproduces the row almost
  // perfectly; a spurious sub-period p | P (created by coincidentally-equal cell
  // widths) reproduces it strictly worse. An absolute floor fails here because
  // gap tokens dilute the mismatch — [12,18,12,24] at the half-period still
  // scores ~0.875, which any fixed floor low enough to tolerate a real defect
  // would wave through. Comparing to max_cons is scale-free: it works whether the
  // best achievable is 1.0 (clean) or 0.9 (one defective instance in the row). --
  const int max_p = n / params.min_repeats;
  std::vector<double> cons(max_p + 1, 0.0);
  std::vector<std::vector<Token>> units(max_p + 1);
  double max_cons = 0.0;
  for (int p = 1; p <= max_p; ++p) {
    units[p] = majority_unit(tok, p);
    cons[p] = self_consistency(tok, units[p], p);
    max_cons = std::max(max_cons, cons[p]);
  }
  if (max_cons < params.loose_floor) return std::nullopt;

  // Primitive = smallest period within `slack` of the best consistency. Multiples
  // k·P score just as high but are larger, so the smallest wins → primitive, not
  // a supercell-of-the-primitive; genuine sub-periods score below and are cut.
  double threshold = std::max(params.loose_floor, max_cons - params.consistency_slack);
  int p_strict = -1;
  for (int p = 1; p <= max_p; ++p)
    if (cons[p] >= threshold) { p_strict = p; break; }
  if (p_strict < 0) return std::nullopt;
  const int p_tok = p_strict;
  std::vector<Token> unit = units[p_tok];

  // Supercell (§2.4): a proper divisor d | p_tok that near-repeats (clears the
  // loose floor) but is NOT itself invariant enough to be the primitive means the
  // larger tile is genuinely necessary → supercell(p_tok / d).
  Classification classification = Classification::Primitive;
  int supercell_k = 1;
  for (int d = 1; d < p_tok; ++d) {
    if (p_tok % d == 0 && cons[d] >= params.supercell_floor && cons[d] < threshold) {
      classification = Classification::Supercell;
      supercell_k = p_tok / d;
      break;
    }
  }

  double physical_period = 0.0;
  for (const auto& t : unit) physical_period += t.length;
  if (physical_period <= kEps) return std::nullopt;

  // --- Trim leading/trailing non-periodic content (e.g. a border rectangle that
  // landed in this slab). A boundary index is "inside the core" only once a full
  // period window starting there matches the canonical unit. Internal defects are
  // deliberately NOT trimmed — only contiguous mismatch at the very ends. --------
  std::vector<char> matches(n);
  for (int i = 0; i < n; ++i)
    matches[i] = (tok[i].symbol == unit[i % p_tok].symbol) ? 1 : 0;
  auto window_clean = [&](int s) {
    for (int k = 0; k < p_tok; ++k)
      if (!matches[s + k]) return false;
    return true;
  };
  int lo = 0, hi = n;
  while (lo + p_tok <= n && !window_clean(lo)) ++lo;
  while (hi - p_tok >= 0 && !window_clean(hi - p_tok)) --hi;
  if (lo >= hi) { lo = 0; hi = n; }  // no clean full period — fall back to all

  double x_start = starts[lo];
  double x_end = starts[hi - 1] + tok[hi - 1].length;
  double phase = std::fmod(x_start, physical_period);
  if (phase < 0) phase += physical_period;

  // Canonical unit as physical covered offsets, PHASED to begin at x_start
  // (token index lo). Building this rotation once, in physical terms, is what
  // replaces the prototype's fragile per-scan token-index rotation.
  std::vector<Interval> unit_covered;
  {
    double off = 0.0;
    for (int k = 0; k < p_tok; ++k) {
      const Token& t = unit[(lo + k) % p_tok];
      if (is_filled(t)) unit_covered.push_back({off, off + t.length});
      off += t.length;
    }
  }

  // --- Bug 3 fix: defect scan over EXPECTED physical positions. Each position's
  // observed geometry is looked up by physical x-window from `filled_view`, never
  // by token index, so a missing instance is reported at its true x and nothing
  // after it desyncs. ----------------------------------------------------------
  int n_positions = std::max(1L, std::lround((x_end - x_start) / physical_period));
  int n_clean = 0;
  std::vector<double> defects;
  for (int m = 0; m < n_positions; ++m) {
    double base = x_start + m * physical_period;
    std::vector<Interval> expected;
    expected.reserve(unit_covered.size());
    for (const auto& iv : unit_covered) expected.push_back({iv.lo, iv.hi});
    std::vector<Interval> observed =
        clip_rebased(filled_view, base, base + physical_period, params.match_tol);
    if (intervals_match(expected, observed, params.match_tol)) ++n_clean;
    else defects.push_back(base);
  }

  RowRun run;
  run.slab_index = slab_index;
  run.encoding = ts.encoding;
  run.y0 = slab.y0;
  run.y1 = slab.y1;
  run.y_mid = slab.y_mid;
  run.token_period = p_tok;
  run.dx = physical_period;
  run.phase = phase;
  run.x_start = x_start;
  run.x_end = x_end;
  run.classification = classification;
  run.supercell_k = supercell_k;
  run.n_expected = n_positions;
  run.n_clean = n_clean;
  run.defect_positions = std::move(defects);
  run.unit_covered = std::move(unit_covered);
  return run;
}

}  // namespace adt
