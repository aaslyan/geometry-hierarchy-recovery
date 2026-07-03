#include "adt/hr_svg.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <vector>

#include "adt/d4.hpp"
#include "adt/recover.hpp"

namespace adt::hr {

namespace {
// Fill + stroke per top-level cell id (1-based). Distinct hue families so a
// super-cell and a standalone leaf cell read differently.
struct Style { const char* fill; const char* stroke; };
const Style kStyles[] = {
    {"#bbdefb", "#1565c0"},  // cell 1 — blue
    {"#c8e6c9", "#2e7d32"},  // cell 2 — green
    {"#ffe0b2", "#ef6c00"},  // cell 3 — orange
    {"#e1bee7", "#6a1b9a"},  // cell 4 — purple
};
const Style& style_for(int cell) { return kStyles[(cell - 1) % 4]; }

struct InstBox { int cell; int depth; Rect bbox; };

// Expand one placement (bbox-min at (x,y), oriented `orient`), recording every
// leaf rect (tagged with the TOP cell id for coloring) and every cell-instance
// bbox (with nesting depth), orientation-aware to match recover.cpp's flatten.
void expand(const Hierarchy& h, int cell, int orient, double x, double y, int depth,
            int top_cell, std::vector<std::pair<Rect, int>>& leaves,
            std::vector<InstBox>& boxes) {
  std::vector<Member> tb;
  for (const auto& m : h.cells[cell - 1].members) tb.push_back(xform_member(m, orient));
  double offx = 1e300, offy = 1e300, mxx = -1e300, mxy = -1e300;
  for (const auto& m : tb) {
    offx = std::min(offx, m.dx); offy = std::min(offy, m.dy);
    mxx = std::max(mxx, m.dx + m.w); mxy = std::max(mxy, m.dy + m.h);
  }
  boxes.push_back({cell, depth, {x, y, x + (mxx - offx), y + (mxy - offy)}});
  for (const auto& m : tb) {
    double px = x + m.dx - offx, py = y + m.dy - offy;
    if (m.cell == 0)
      leaves.push_back({{px, py, px + m.w, py + m.h}, top_cell});
    else
      expand(h, m.cell, m.orient, px, py, depth + 1, top_cell, leaves, boxes);
  }
}
}  // namespace

void write_hierarchy_svg(const std::string& path, const Hierarchy& h,
                         const std::string& title) {
  std::vector<std::pair<Rect, int>> leaves;  // (rect, top cell id) ; id 0 = residual
  std::vector<InstBox> boxes;
  for (const auto& p : h.top)
    expand(h, p.cell, p.orient, p.x, p.y, 0, p.cell, leaves, boxes);
  for (const auto& r : h.residual) leaves.push_back({r, 0});

  double minx = 1e300, miny = 1e300, maxx = -1e300, maxy = -1e300;
  for (const auto& lr : leaves) {
    minx = std::min(minx, lr.first.x0); miny = std::min(miny, lr.first.y0);
    maxx = std::max(maxx, lr.first.x1); maxy = std::max(maxy, lr.first.y1);
  }
  const double margin = 20;
  minx -= margin; miny -= margin; maxx += margin; maxy += margin;
  const double W = maxx - minx, H = maxy - miny;
  auto sx = [&](double x) { return x - minx; };
  auto sy = [&](double y) { return maxy - y; };

  std::ostringstream o;
  o << "<svg xmlns='http://www.w3.org/2000/svg' width='" << (W * 3) << "' height='"
    << (H * 3 + 40) << "' viewBox='0 -40 " << W << " " << (H + 40) << "'>\n";
  o << "<rect x='0' y='-40' width='" << W << "' height='" << (H + 40)
    << "' fill='white'/>\n<style>text{font-family:sans-serif}</style>\n";
  o << "<text x='2' y='-14' font-size='13' fill='#111'>" << title << "  ("
    << h.cells.size() << " cells, " << h.levels << " levels, flatten "
    << (h.flatten_matches ? "== G" : "!= G") << ")</text>\n";

  // Leaf rectangles, tinted by top cell (residual = gray).
  for (const auto& lr : leaves) {
    const Rect& r = lr.first;
    const char* fill = lr.second == 0 ? "#eceff1" : style_for(lr.second).fill;
    const char* stroke = lr.second == 0 ? "#b0bec5" : style_for(lr.second).stroke;
    o << "<rect x='" << sx(r.x0) << "' y='" << sy(r.y1) << "' width='" << r.width()
      << "' height='" << r.height() << "' fill='" << fill << "' stroke='" << stroke
      << "' stroke-width='0.3'/>\n";
  }
  // Cell-instance outlines: deeper nesting = thinner, dashed for sub-cells.
  for (const auto& b : boxes) {
    const Style& st = style_for(b.cell);
    double sw = b.depth == 0 ? 1.4 : 0.7;
    o << "<rect x='" << sx(b.bbox.x0) << "' y='" << sy(b.bbox.y1) << "' width='"
      << b.bbox.width() << "' height='" << b.bbox.height() << "' fill='none' stroke='"
      << st.stroke << "' stroke-width='" << sw << "'"
      << (b.depth > 0 ? " stroke-dasharray='2,1'" : "") << "/>\n";
  }
  // Defect markers: idealized-but-absent members (red dashed box + X).
  for (const auto& d : h.missing_geometry) {
    o << "<rect x='" << sx(d.x0) << "' y='" << sy(d.y1) << "' width='" << d.width()
      << "' height='" << d.height()
      << "' fill='none' stroke='#e53935' stroke-width='0.8' stroke-dasharray='2,1'/>\n";
    o << "<line x1='" << sx(d.x0) << "' y1='" << sy(d.y1) << "' x2='" << sx(d.x1)
      << "' y2='" << sy(d.y0) << "' stroke='#e53935' stroke-width='0.8'/>\n";
    o << "<line x1='" << sx(d.x0) << "' y1='" << sy(d.y0) << "' x2='" << sx(d.x1)
      << "' y2='" << sy(d.y1) << "' stroke='#e53935' stroke-width='0.8'/>\n";
  }

  o << "</svg>\n";
  std::ofstream(path) << o.str();
}

}  // namespace adt::hr
