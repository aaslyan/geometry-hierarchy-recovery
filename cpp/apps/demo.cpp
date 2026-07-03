// demo.cpp — run array detection on both demo datasets (plus the abutted
// zero-gap variant) and emit the §6/§7 report and §8 visualizations.
#include <cstdio>
#include <string>

#include "adt/detector.hpp"
#include "adt/svg.hpp"
#include "adt/synthetic.hpp"

using namespace adt;

static void report(const std::string& name, const std::vector<Rect>& rects,
                   const DetectionResult& res) {
  std::printf("\n=== %s (%zu rects) ===\n", name.c_str(), rects.size());
  // Candidate-reduction funnel (§7.5): N symbolic runs >> M clusters >> K arrays.
  std::printf("  funnel: symbolic_runs=%d  clusters=%d  verified_arrays=%d\n",
              res.n_symbolic_runs, res.n_clusters, res.n_verified);
  for (const auto& c : res.arrays) {
    std::printf(
        "  region [%.1f,%.1f]-[%.1f,%.1f]  dx=%.2f dy=%.2f  grid=%dx%d  "
        "class=%s\n",
        c.bbox.x0, c.bbox.y0, c.bbox.x1, c.bbox.y1, c.dx, c.dy, c.n_cols,
        c.n_rows,
        c.classification == Classification::Primitive
            ? "primitive"
            : ("supercell(" + std::to_string(c.supercell_k) + ")").c_str());
    std::printf("    instances=%d  clean=%d  missing=%d  extra=%d\n",
                c.n_instances, c.n_clean, c.n_missing, c.n_extra);
    for (const auto& d : c.defects) {
      std::printf("      %s at (%.1f,%.1f)  area=%.1f\n",
                  d.type == DefectType::Missing ? "MISSING" : "extra  ", d.x,
                  d.y, d.area);
    }
  }
}

int main() {
  struct Demo { Layout (*gen)(); std::string svg; };
  Demo demos[] = {
      {bauhaus_layout, "bauhaus.svg"},
      {standard_cell_layout, "standard_cell.svg"},
      {standard_cell_abutted_layout, "standard_cell_abutted.svg"},
  };
  for (const auto& d : demos) {
    Layout L = d.gen();
    DetectionResult res = detect_arrays(L.rects);
    report(L.meta.name, L.rects, res);
    write_svg(d.svg, L.rects, res, L.meta.name);
    std::printf("  wrote %s\n", d.svg.c_str());
  }
  return 0;
}
