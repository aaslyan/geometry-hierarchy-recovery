#include "adt/svg.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace adt {

namespace {
const char* kColors[] = {"#1f77b4", "#2ca02c", "#9467bd", "#ff7f0e", "#17becf"};
}

void write_svg(const std::string& path, const std::vector<Rect>& rects,
               const DetectionResult& result, const std::string& title) {
  double minx = 1e300, miny = 1e300, maxx = -1e300, maxy = -1e300;
  for (const auto& r : rects) {
    minx = std::min(minx, r.x0);
    miny = std::min(miny, r.y0);
    maxx = std::max(maxx, r.x1);
    maxy = std::max(maxy, r.y1);
  }
  if (rects.empty()) { minx = miny = 0; maxx = maxy = 100; }
  const double margin = 20.0;
  minx -= margin; miny -= margin; maxx += margin; maxy += margin;
  const double W = maxx - minx, H = maxy - miny;

  // Flip y: layout-y up -> svg-y down.
  auto sx = [&](double x) { return x - minx; };
  auto sy = [&](double y) { return maxy - y; };

  std::ostringstream o;
  o << "<svg xmlns='http://www.w3.org/2000/svg' width='" << (W * 3)
    << "' height='" << (H * 3 + 40) << "' viewBox='0 -40 " << W << " " << (H + 40)
    << "'>\n";
  o << "<style>text{font-family:sans-serif;}</style>\n";
  o << "<text x='2' y='-14' font-size='14' fill='#111'>" << title << "  ("
    << rects.size() << " rects, " << result.n_verified << " array"
    << (result.n_verified == 1 ? "" : "s") << ")</text>\n";
  o << "<rect x='0' y='" << (-40) << "' width='" << W << "' height='" << (H + 40)
    << "' fill='white'/>\n";

  // All layout rectangles.
  for (const auto& r : rects) {
    o << "<rect x='" << sx(r.x0) << "' y='" << sy(r.y1) << "' width='" << r.width()
      << "' height='" << r.height()
      << "' fill='#cfd8dc' stroke='#607d8b' stroke-width='0.3'/>\n";
  }

  // Detected arrays: bbox outline, lattice lines, defect markers.
  for (std::size_t a = 0; a < result.arrays.size(); ++a) {
    const auto& c = result.arrays[a];
    const char* col = kColors[a % 5];
    o << "<rect x='" << sx(c.bbox.x0) << "' y='" << sy(c.bbox.y1) << "' width='"
      << (c.bbox.x1 - c.bbox.x0) << "' height='" << (c.bbox.y1 - c.bbox.y0)
      << "' fill='none' stroke='" << col << "' stroke-width='1.2'/>\n";
    for (int i = 1; i < c.n_cols; ++i) {
      double x = c.bbox.x0 + i * c.dx;
      o << "<line x1='" << sx(x) << "' y1='" << sy(c.bbox.y0) << "' x2='" << sx(x)
        << "' y2='" << sy(c.bbox.y1) << "' stroke='" << col
        << "' stroke-width='0.2' stroke-dasharray='1,1'/>\n";
    }
    for (int j = 1; j < c.n_rows; ++j) {
      double y = c.bbox.y0 + j * c.dy;
      o << "<line x1='" << sx(c.bbox.x0) << "' y1='" << sy(y) << "' x2='"
        << sx(c.bbox.x1) << "' y2='" << sy(y) << "' stroke='" << col
        << "' stroke-width='0.2' stroke-dasharray='1,1'/>\n";
    }
    for (const auto& d : c.defects) {
      double cx = sx(d.x), cy = sy(d.y), rr = std::min(c.dx, c.dy) * 0.3;
      // Missing (structural) = red X; extra (overlay) = orange square outline.
      if (d.type == DefectType::Missing) {
        o << "<line x1='" << (cx - rr) << "' y1='" << (cy - rr) << "' x2='"
          << (cx + rr) << "' y2='" << (cy + rr)
          << "' stroke='#e53935' stroke-width='0.9'/>\n";
        o << "<line x1='" << (cx - rr) << "' y1='" << (cy + rr) << "' x2='"
          << (cx + rr) << "' y2='" << (cy - rr)
          << "' stroke='#e53935' stroke-width='0.9'/>\n";
      } else {
        o << "<rect x='" << (cx - rr) << "' y='" << (cy - rr) << "' width='"
          << (2 * rr) << "' height='" << (2 * rr)
          << "' fill='none' stroke='#fb8c00' stroke-width='0.7'/>\n";
      }
    }
  }

  o << "</svg>\n";

  std::ofstream f(path);
  f << o.str();
}

}  // namespace adt
