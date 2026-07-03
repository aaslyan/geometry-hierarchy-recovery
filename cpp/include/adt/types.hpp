// types.hpp — core value types for the array detector.
//
// Everything here is deliberately plain-old-data. Geometry-heavy helpers that
// pull in Boost live in geometry.hpp; this header stays lightweight so the
// symbolic pipeline (slab/encoding/runs/reconcile) can include it without
// dragging Boost into every translation unit.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace adt {

// Axis-aligned rectangle. The whole project is rectilinear/Manhattan by scope
// (see the v4 proposal, §1.1), so a rectangle is the only primitive we need.
struct Rect {
  double x0{}, y0{}, x1{}, y1{};
  double width() const { return x1 - x0; }
  double height() const { return y1 - y0; }
};

// Which of the two §2.2 encodings produced a given symbolic stream. We keep it
// on every downstream record so cross-encoding reconciliation can note where a
// vote came from without changing its (physical) compatibility logic.
enum class Encoding : std::uint8_t { Occupancy, EdgeBoundary };

inline const char* to_string(Encoding e) {
  return e == Encoding::Occupancy ? "occupancy" : "edge";
}

// A token is a symbol plus its physical length. The proposal (§2.2 "Physical
// period, not just symbolic period") is explicit that tolerance binning can let
// a symbolic period survive while the physical lattice drifts, so every token
// must carry both. `symbol` is an interned id within a single slab's stream;
// comparisons are only ever made within one stream, never across slabs.
struct Token {
  std::int32_t symbol{};  // interned symbol id (encoding-specific)
  double length{};        // physical extent along the scan axis
  bool operator==(const Token& o) const { return symbol == o.symbol; }
  bool operator!=(const Token& o) const { return symbol != o.symbol; }
};

}  // namespace adt
