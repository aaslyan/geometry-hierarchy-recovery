// encoding.hpp — symbolization and serialization (§2.2).
//
// Two complementary encodings, each turning one slab into a metric-preserving
// token stream (every token carries a physical length, per §2.2's "physical
// period, not just symbolic period"):
//
//   Occupancy-interval — MERGED covered intervals. Filled tokens carry the
//     quantized covered width; gap tokens the quantized empty width. Strong when
//     features have real gaps between them.
//
//   Edge-boundary — RAW per-rectangle spans, NOT merged, with each filled
//     token's symbol derived from (width, edge-height). This is the encoding
//     that survives directly-abutted (zero-gap) geometry: two touching cells
//     that occupancy merges into one wide token stay as two distinct tokens
//     here, so the repeated boundary structure is still visible. (CLAUDE.md's
//     "known gap" — the prototype only had occupancy — is closed by this.)
//
// Symbol convention shared by both: filled tokens get POSITIVE interned ids,
// gap tokens NEGATIVE. Downstream code uses the sign to tell "is this token
// filled geometry or empty space" without carrying an extra flag.
#pragma once

#include <vector>

#include "adt/slab.hpp"
#include "adt/types.hpp"

namespace adt {

inline bool is_filled(const Token& t) { return t.symbol > 0; }
inline bool is_gap(const Token& t) { return t.symbol < 0; }

struct TokenStream {
  Encoding encoding{};
  std::vector<Token> tokens;
  std::vector<double> starts;  // physical scan-axis start of each token
};

// No leading/trailing gap tokens: edge effects outside the first/last filled
// span aren't part of any interior period and would just add noise (matches the
// prototype's tokenize()).
TokenStream encode_occupancy(const Slab& slab, double width_quantum = 0.5);
TokenStream encode_edge_boundary(const Slab& slab, double width_quantum = 0.5,
                                 double height_quantum = 0.5);

}  // namespace adt
