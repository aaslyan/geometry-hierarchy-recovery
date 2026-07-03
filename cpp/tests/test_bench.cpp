// test_bench.cpp — hierarchy-erasure benchmark invariants (companion §7.1/§7.4).
// Across a range of configs, recovery must exactly explain the flattened geometry
// (G ⊆ flatten), recover the leaf gate, and compress.
#include <cstdio>

#include "adt/bench.hpp"

using namespace adt::hr;

static int g_failures = 0;
#define CHECK(cond, msg)                                                     \
  do {                                                                       \
    if (!(cond)) { std::printf("  FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); ++g_failures; } \
    else std::printf("  ok:   %s\n", msg);                                   \
  } while (0)

int main() {
  std::printf("[bench] hierarchy-erasure recovery invariants\n");
  struct C { const char* l; int g, r, b; bool m; int c; };
  C cs[] = {
      {"flat array", 6, 4, 1, false, 0},
      {"nested", 6, 4, 2, false, 4},
      {"mirrored", 6, 5, 2, true, 6},
  };
  for (const auto& c : cs) {
    ErasureDesign d = make_datapath(c.g, c.r, c.b, c.m, c.c, false);
    ErasureResult r = run_erasure(d);
    char buf[128];
    std::snprintf(buf, sizeof buf, "%s: G subset flatten(H)", c.l);
    CHECK(r.flatten_ok, buf);
    std::snprintf(buf, sizeof buf, "%s: leaf gate recovered", c.l);
    CHECK(r.leaf_recovered, buf);
    std::snprintf(buf, sizeof buf, "%s: compresses (>2x)", c.l);
    CHECK(r.compression > 2.0, buf);
  }
  std::printf("\n%s (%d failure%s)\n", g_failures == 0 ? "ALL PASSED" : "FAILURES",
              g_failures, g_failures == 1 ? "" : "s");
  return g_failures == 0 ? 0 : 1;
}
