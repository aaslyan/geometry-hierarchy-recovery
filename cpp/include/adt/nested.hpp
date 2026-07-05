// nested.hpp — mixed array/hierarchy extraction (hybrid method).
//
// This is a SEPARATE, additive method that sits on top of the existing
// recover_hierarchy() pipeline; it does not replace it. The base recoverer finds
// repeated *leaf* cells and lays their instances out as a flat placement list
// (plus the compressed array-node *view* of §7.3). This module turns that flat
// placement list into a genuinely NESTED hierarchy — gate → slice → block → top —
// by promoting regular runs of placements into intermediate cells whose bodies
// may themselves contain array references. Every promotion is certified by the
// same exact rectangle-multiset flatten==G gate the base recoverer uses.
//
// Design choice (isolation): nested cells are always instantiated at orientation
// 0. D4 mirroring is baked into DISTINCT cell bodies (a mirrored row is a
// different slice cell, its gate members carrying the mirrored leaf orientation),
// so no orientation composition is needed above the leaf level — expansion above
// leaves is pure translation. This keeps the module self-contained and lets it
// reuse the public flatten() for leaf expansion without duplicating the D4 math.
#pragma once

#include <string>
#include <vector>

#include "adt/hierarchy.hpp"
#include "adt/recover.hpp"
#include "adt/types.hpp"

namespace adt::hr {

// One item in a nested cell body: either a single child instance (n == 1) or a
// regular 1D array of a child (n > 1) at origin (ox,oy) with step (sx,sy),
// relative to the parent cell's anchor (its bbox min-corner). A child is either a
// base leaf CellDef (leaf == true, `child` is its id, `orient` its D4 code) or
// another nested cell (leaf == false, `child` an NCell id, orient always 0).
struct NItem {
  bool leaf = true;
  int child = 0;
  int orient = 0;
  double ox = 0, oy = 0;   // anchor of site 0 relative to the parent anchor
  double sx = 0, sy = 0;   // array step (0,0 when n == 1)
  int n = 1;               // number of sites along the run
  std::vector<int> gaps;   // missing site indices in [0,n) (empty = fully occupied)

  int occupied() const { return n - (int)gaps.size(); }
  // Cost: one reference plus one explicit exception per missing site (§7.3 model).
  int cost() const { return 1 + (int)gaps.size(); }
};

// A synthesized intermediate cell (slice / block / pair / top).
struct NCell {
  int id = 0;
  std::string kind;
  std::vector<NItem> body;
  double w = 0, h = 0;     // bbox dims, anchor at min corner
  int leaf_count = 0;      // fully-expanded leaf-rectangle count
  int level = 0;           // promotion level it was created at (>= 2)
};

// A top-level instance: a nested cell (leaf == false) or a leftover base leaf
// cell that was never grouped into a run (leaf == true).
struct NInst {
  bool leaf = true;
  int cell = 0;
  double x = 0, y = 0;     // bbox min-corner of the placed instance
  int orient = 0;
};

struct Nested {
  Hierarchy base;                // leaf cells + defect layer (from recover_hierarchy)
  std::vector<NCell> cells;      // synthesized intermediate cells (indexed by id-1)
  std::vector<NInst> top;        // top-level instances
  std::vector<Rect> residual;    // leaf rectangles explained by no cell

  int levels = 0;                // nested levels synthesized above the leaves

  // Reporting.
  int flat_leaf_count = 0;       // |G|
  int base_cost = 0;             // placement-list cost of the base recoverer (§7.3)
  int nested_cost = 0;           // Σ body-item costs + top instances + residual

  // Correctness. The promotion only REGROUPS the base recoverer's placements, so
  // the primary guarantee is that the nested view reproduces the base's own
  // idealized geometry EXACTLY. For clean layouts flatten(base) == G, so this is
  // also flatten(nested) == G. With a tolerated defect the base over-produces the
  // idealized missing member; the nested view inherits that layer, so against the
  // raw input we can only claim CONTAINMENT (G ⊆ flatten(nested)) with the surplus
  // reported as the defect layer — exactly the base recoverer's §6 semantics.
  bool matches_base_flatten = false;  // flatten(nested) multiset == flatten(base)
  bool explains_g = false;            // G ⊆ flatten(nested): every input rect reproduced
  int defect_rects = 0;               // |flatten(nested)| − |G| (idealized missing members)

  // Agreement metrics (plan §7): did we synthesize slice-like / block-like cells?
  int n_slice = 0;               // cells whose body is a single array of a leaf
  int n_block = 0;               // cells whose body references other nested cells
};

// Recover a nested hierarchy. Runs recover_hierarchy() internally (unchanged),
// then promotes regular placement runs into intermediate cells.
Nested recover_nested(const std::vector<Rect>& layout, const RecoverConfig& cfg = {});

// Expand a nested hierarchy back to flat leaf rectangles (reuses public flatten
// for leaf-cell expansion).
std::vector<Rect> flatten_nested(const Nested& h);

// Serialize the recovered hierarchy as JSON so the result is inspectable and
// reusable downstream without scraping stdout. Schema (plan §8): `leaf_cells`,
// `cells` (each with a `body` of array items carrying child/orient/origin/step/
// count/gaps — arrays are represented as body items, not a separate section),
// `instances`, `residual` (actual rectangles), `defects` (idealized missing-member
// rectangles), and `metrics`.
std::string nested_to_json(const Nested& h);

}  // namespace adt::hr
