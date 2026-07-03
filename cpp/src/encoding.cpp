#include "adt/encoding.hpp"

#include <cmath>
#include <map>
#include <utility>

namespace adt {

namespace {
// Quantize a length to an integer number of quanta. This is the tolerance
// binning §2.2 warns about: it makes symbols robust to sub-quantum noise, which
// is exactly why the physical length must be carried alongside the symbol.
long qbin(double v, double quantum) {
  return std::lround(v / quantum);
}

// Interns (kind, a, b) triples to stable symbol ids. Filled kinds get positive
// ids, gaps negative, per the header's sign convention. The map is per-stream:
// symbols are only ever compared within one slab's token sequence.
class Interner {
 public:
  std::int32_t filled(long a, long b = 0) {
    return get(std::make_tuple(1, a, b), pos_, +1);
  }
  std::int32_t gap(long a) {
    return get(std::make_tuple(0, a, 0L), neg_, -1);
  }

 private:
  using Key = std::tuple<int, long, long>;
  std::int32_t get(const Key& k, std::int32_t& counter, int sign) {
    auto it = table_.find(k);
    if (it != table_.end()) return it->second;
    std::int32_t id = sign * (++counter);
    table_.emplace(k, id);
    return id;
  }
  std::map<Key, std::int32_t> table_;
  std::int32_t pos_{0}, neg_{0};
};
}  // namespace

TokenStream encode_occupancy(const Slab& slab, double width_quantum) {
  TokenStream ts;
  ts.encoding = Encoding::Occupancy;
  Interner in;
  const auto& cov = slab.covered;
  for (std::size_t i = 0; i < cov.size(); ++i) {
    double w = cov[i].width();
    ts.starts.push_back(cov[i].lo);
    ts.tokens.push_back({in.filled(qbin(w, width_quantum)),
                         qbin(w, width_quantum) * width_quantum});
    if (i + 1 < cov.size()) {
      double g = cov[i + 1].lo - cov[i].hi;
      ts.starts.push_back(cov[i].hi);
      ts.tokens.push_back({in.gap(qbin(g, width_quantum)),
                           qbin(g, width_quantum) * width_quantum});
    }
  }
  return ts;
}

TokenStream encode_edge_boundary(const Slab& slab, double width_quantum,
                                 double height_quantum) {
  TokenStream ts;
  ts.encoding = Encoding::EdgeBoundary;
  Interner in;
  const auto& spans = slab.spans;
  for (std::size_t i = 0; i < spans.size(); ++i) {
    double w = spans[i].width();
    long wb = qbin(w, width_quantum);
    long hb = qbin(slab.span_heights[i], height_quantum);
    // Symbol keyed on BOTH width and edge-height: this is what lets the edge
    // encoding tell two abutted cells of different height apart even when a
    // width-only view would conflate them.
    ts.starts.push_back(spans[i].lo);
    ts.tokens.push_back({in.filled(wb, hb), wb * width_quantum});
    if (i + 1 < spans.size()) {
      double g = spans[i + 1].lo - spans[i].hi;
      long gb = qbin(g, width_quantum);
      if (gb > 0) {  // abutted cells (gb == 0) leave NO gap token — boundary is
                     // carried by the two distinct filled tokens instead.
        ts.starts.push_back(spans[i].hi);
        ts.tokens.push_back({in.gap(gb), gb * width_quantum});
      }
    }
  }
  return ts;
}

}  // namespace adt
