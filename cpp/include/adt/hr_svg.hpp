// hr_svg.hpp — visualize a recovered hierarchy: leaf rectangles tinted by the
// top-level cell they belong to, each cell instance outlined (super-cells bold,
// nested sub-cells thinner), residual clutter in gray. Makes the nesting and the
// irregular placement visible at a glance.
#pragma once

#include <string>

#include "adt/hierarchy.hpp"

namespace adt::hr {

void write_hierarchy_svg(const std::string& path, const Hierarchy& h,
                         const std::string& title);

}  // namespace adt::hr
