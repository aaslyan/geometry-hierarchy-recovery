#include "adt/detector.hpp"

#include <algorithm>

#include "adt/encoding.hpp"
#include "adt/geometry.hpp"
#include "adt/slab.hpp"

namespace adt {

namespace {
// Analyze every slab of one pass under both §2.2 encodings, returning the runs
// plus their generic (scan, normal) projection for clustering. For horizontal
// slabs the scan axis is x; for vertical (axis_swapped) slabs the run's scan
// axis is y — but the RowRun fields carry through identically, because
// build_slabs already remapped the geometry, so the projection is the same.
void run_pass(const std::vector<Rect>& rects, bool axis_swapped,
              const DetectorConfig& cfg, std::vector<RowRun>& runs,
              std::vector<LineRun>& lines) {
  auto slabs = build_slabs(rects, axis_swapped, cfg.grid);
  for (int s = 0; s < static_cast<int>(slabs.size()); ++s) {
    const Slab& slab = slabs[s];
    TokenStream occ = encode_occupancy(slab, cfg.width_quantum);
    TokenStream edge =
        encode_edge_boundary(slab, cfg.width_quantum, cfg.height_quantum);
    // Occupancy compares against merged covered intervals; edge against raw
    // spans (so abutted cells stay distinct — the whole point of §2.2's pair).
    struct Attempt { const TokenStream* ts; const std::vector<Interval>* view; };
    for (const Attempt& a : {Attempt{&occ, &slab.covered}, Attempt{&edge, &slab.spans}}) {
      auto run = analyze_row(s, slab, *a.ts, *a.view, cfg.run);
      if (!run) continue;
      int idx = static_cast<int>(runs.size());
      runs.push_back(*run);
      LineRun lr;
      lr.period = run->dx;
      lr.phase = run->phase;
      lr.scan_lo = run->x_start;
      lr.scan_hi = run->x_end;
      lr.norm_lo = run->y0;
      lr.norm_hi = run->y1;
      lr.run_index = idx;
      lines.push_back(lr);
    }
  }
}

// Intersection-over-union of two axis-aligned boxes.
double iou(const Rect& a, const Rect& b) {
  double ix = std::min(a.x1, b.x1) - std::max(a.x0, b.x0);
  double iy = std::min(a.y1, b.y1) - std::max(a.y0, b.y0);
  if (ix <= 0 || iy <= 0) return 0.0;
  double inter = ix * iy;
  double ua = a.width() * a.height() + b.width() * b.height() - inter;
  return ua > 0 ? inter / ua : 0.0;
}

// Both encodings (and multiple compatible cluster pairs) can certify the same
// physical array. Keep the strongest representative per overlapping group.
std::vector<ArrayCandidate> dedupe(std::vector<ArrayCandidate> in) {
  std::sort(in.begin(), in.end(), [](const ArrayCandidate& a, const ArrayCandidate& b) {
    if (a.n_clean != b.n_clean) return a.n_clean > b.n_clean;
    return a.n_instances > b.n_instances;
  });
  std::vector<ArrayCandidate> out;
  for (auto& c : in) {
    bool dup = false;
    for (const auto& k : out)
      if (iou(c.bbox, k.bbox) > 0.5) { dup = true; break; }
    if (!dup) out.push_back(std::move(c));
  }
  return out;
}
}  // namespace

DetectionResult detect_arrays(const std::vector<Rect>& rects,
                              const DetectorConfig& cfg) {
  DetectionResult result;

  std::vector<RowRun> h_runs, v_runs;
  std::vector<LineRun> h_lines, v_lines;
  run_pass(rects, /*axis_swapped=*/false, cfg, h_runs, h_lines);
  run_pass(rects, /*axis_swapped=*/true, cfg, v_runs, v_lines);
  result.n_symbolic_runs = static_cast<int>(h_runs.size() + v_runs.size());

  auto h_clusters = cluster_runs(h_lines, cfg.reconcile);
  auto v_clusters = cluster_runs(v_lines, cfg.reconcile);
  result.n_clusters =
      static_cast<int>(h_clusters.size() + v_clusters.size());

  auto candidates =
      intersect_passes(h_clusters, v_clusters, h_runs, v_runs, cfg.reconcile);

  MultiPolygon layout = rects_to_multipolygon(rects);
  std::vector<ArrayCandidate> verified;
  for (auto& c : candidates)
    if (verify_candidate(layout, c, cfg.verify)) verified.push_back(c);

  result.arrays = dedupe(std::move(verified));
  result.n_verified = static_cast<int>(result.arrays.size());
  return result;
}

}  // namespace adt
