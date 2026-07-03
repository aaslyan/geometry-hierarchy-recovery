// svg.hpp — render a layout with detected arrays overlaid, for visual QA
// (proposal §8 figures: layout, detected regions outlined, defects marked).
#pragma once

#include <string>
#include <vector>

#include "adt/detector.hpp"
#include "adt/types.hpp"

namespace adt {

// Write an SVG showing every rectangle, each certified array's bounding box and
// lattice, and a marker at every defective instance. Layout y is flipped so the
// image reads the same way as the coordinate system.
void write_svg(const std::string& path, const std::vector<Rect>& rects,
               const DetectionResult& result, const std::string& title);

}  // namespace adt
