// geometry.hpp — Boost.Geometry glue for the rectilinear boolean set-ops the
// design leans on for verification (§2.5) and defect computation (§6:
// `missing = expected − observed`, `extra = observed − expected`).
//
// We model everything as a multi_polygon of axis-aligned boxes. That keeps the
// whole pipeline in one exact-enough numeric world and lets union / difference /
// intersection do the heavy lifting instead of hand-rolled interval algebra.
#pragma once

#include <boost/geometry.hpp>
#include <boost/geometry/geometries/geometries.hpp>
#include <boost/geometry/geometries/point_xy.hpp>

#include <vector>

#include "adt/types.hpp"

namespace adt {
namespace bg = boost::geometry;

using Point = bg::model::d2::point_xy<double>;
using Polygon = bg::model::polygon<Point, /*ClockWise=*/false>;  // ccw, closed
using MultiPolygon = bg::model::multi_polygon<Polygon>;

// A rectangle as a single ccw-closed ring. Boost wants the ring explicitly
// closed (first point repeated) and correctly oriented; `correct` guarantees
// both regardless of how we listed the corners.
inline Polygon rect_to_polygon(const Rect& r) {
  Polygon p;
  bg::append(p.outer(), Point(r.x0, r.y0));
  bg::append(p.outer(), Point(r.x1, r.y0));
  bg::append(p.outer(), Point(r.x1, r.y1));
  bg::append(p.outer(), Point(r.x0, r.y1));
  bg::append(p.outer(), Point(r.x0, r.y0));
  bg::correct(p);
  return p;
}

// Union a bag of rectangles into a canonicalized multipolygon. This is the
// "merge coincident edges / dissolve unnecessary boundaries" step of §2.5's
// canonicalization: two abutted input rects come out as one dissolved polygon,
// which is exactly what makes fractured-vs-merged comparison work.
inline MultiPolygon rects_to_multipolygon(const std::vector<Rect>& rects) {
  MultiPolygon acc;
  for (const auto& r : rects) {
    if (r.width() <= 0 || r.height() <= 0) continue;
    MultiPolygon next;
    bg::union_(acc, rect_to_polygon(r), next);
    acc = std::move(next);
  }
  return acc;
}

inline MultiPolygon difference(const MultiPolygon& a, const MultiPolygon& b) {
  MultiPolygon out;
  bg::difference(a, b, out);
  return out;
}

inline MultiPolygon intersection(const MultiPolygon& a, const MultiPolygon& b) {
  MultiPolygon out;
  bg::intersection(a, b, out);
  return out;
}

inline double area(const MultiPolygon& m) { return bg::area(m); }

// Clip a rectangle region out of a multipolygon.
inline MultiPolygon clip(const MultiPolygon& m, const Rect& box) {
  MultiPolygon out;
  bg::intersection(m, rect_to_polygon(box), out);
  return out;
}

// Translate a multipolygon by (dx, dy).
inline MultiPolygon translate(const MultiPolygon& m, double dx, double dy) {
  MultiPolygon out;
  bg::strategy::transform::translate_transformer<double, 2, 2> t(dx, dy);
  bg::transform(m, out, t);
  return out;
}

}  // namespace adt
