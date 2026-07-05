// bench.hpp — the hierarchy-erasure benchmark (companion proposal §7.1).
//
// Build a design with KNOWN hierarchy (a leaf "gate" → a horizontal array
// "slice" → a vertical stack of alternately-mirrored slices "block" → a row of
// blocks "chip"), flatten it to raw rectangles (discarding all cell/instance/
// array metadata — the "erasure"), then ask the recoverer to reconstruct a
// compact hierarchy from geometry alone and score it. Ground truth comes from
// the design's own construction, so nothing is hand-labelled.
#pragma once

#include <string>
#include <vector>

#include "adt/types.hpp"

namespace adt::hr {

struct ErasureDesign {
  std::vector<Rect> flat;    // G — the flattened, metadata-stripped geometry
  std::string name;
  int gt_levels = 0;         // designer's hierarchy depth (gate/slice/block/chip)
  int gt_leaf_instances = 0; // total leaf-gate instantiations
  int gt_leaf_rects = 0;     // rectangles in one leaf gate
};

// n_gates per slice, n_rows (slices) per block, n_blocks per chip. `mirror`
// alternates row orientation (like std-cell rows sharing rails); `clutter` adds
// that many non-repeating routing rectangles; `defect` omits one gate.
ErasureDesign make_datapath(int n_gates, int n_rows, int n_blocks, bool mirror,
                            int clutter, bool defect);

struct ErasureResult {
  bool flatten_ok = false;    // G ⊆ flatten(H): all real geometry explained
  bool leaf_recovered = false;// a cell matching the ground-truth gate was found
  int cells = 0, levels = 0, top = 0, arrays = 0, residual = 0, n_defective = 0;
  int flat_leaf = 0, hier_cost = 0, array_cost = 0;
  double compression = 0, array_compression = 0, defect_area = 0;
};

ErasureResult run_erasure(const ErasureDesign& d);

}  // namespace adt::hr
