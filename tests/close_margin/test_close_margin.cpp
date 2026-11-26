// Host tests for close_margin.h (Touch tightens V6's heat-pump close margin).
#include <cassert>
#include <cmath>
#include <cstdio>

#include "close_margin.h"

using namespace lune_touch_close_margin;

int main() {
  // Nothing known: released.
  Room none[2]{};
  assert(!step(false, none, 2).active);
  assert(step(false, none, 2).coldest == -1);

  // 2026-11-26: ground floor 0.3–0.6 warm, bedroom 1.5 cold → tight margin.
  Room house[4] = {{true, 22.3f, 22.0f}, {true, 22.6f, 22.0f}, {true, 19.5f, 21.0f}, {true, 21.7f, 23.0f}};
  Result r = step(false, house, 4);
  assert(r.active);
  assert(r.coldest == 2);
  assert(std::fabs(r.deficit_k - 1.5f) < 1e-4f);

  // Every room within the band: off, and does not switch on.
  Room warm[2] = {{true, 21.7f, 22.0f}, {true, 22.4f, 22.0f}};
  assert(!step(false, warm, 2).active);
  // Hysteresis: held while the coldest is more than 0.2 below, released within it.
  assert(step(true, warm, 2).active);
  warm[0].temp_c = 21.85f;
  assert(!step(true, warm, 2).active);

  // An invalid (stale) room is ignored.
  Room stale[2] = {{false, 18.0f, 22.0f}, {true, 22.1f, 22.0f}};
  assert(!step(false, stale, 2).active);

  std::printf("close_margin: all tests passed\n");
  return 0;
}
