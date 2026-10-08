// Host tests for house_balance.h (rough balance between V6 manifolds).
#include <cassert>
#include <cmath>
#include <cstdio>

#include "house_balance.h"

using namespace lune_touch_house_balance;

static bool near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

int main() {
  const Params p{};

  // Length matches V6: 37.5 m² at cc 300 with 3 m leads → 125 + 6 + 2.
  assert(near(loop_length_m({37.5f, 16.0f, 300.0f, 3.0f}), 133.0f, 0.1f));
  // 24.5 m² · 40 W/m² · 0.86 / 5 K ≈ 168.6 l/h.
  assert(near(design_flow_lh({24.5f, 12.0f, 200.0f, 3.0f}, p), 168.6f, 0.2f));

  // Narrower pipe at the same flow and length → much higher drop (~ d^-4.75).
  const float wide = pressure_drop_pa(170.0f, 16.0f, 100.0f);
  const float narrow = pressure_drop_pa(170.0f, 12.0f, 100.0f);
  assert(narrow > 3.5f * wide && narrow < 4.5f * wide);
  assert(pressure_drop_pa(0.0f, 12.0f, 100.0f) == 0.0f);

  // The installation this was written for: ground floor 20×2 cc 300, first floor 16×2 cc 200.
  Loop loops[2 * 6] = {
      {18.0f, 16.0f, 300.0f, 3.0f}, {15.0f, 16.0f, 300.0f, 3.0f}, {28.0f, 16.0f, 300.0f, 3.0f},
      {32.0f, 16.0f, 300.0f, 3.0f}, {37.5f, 16.0f, 300.0f, 3.0f}, {14.5f, 16.0f, 300.0f, 3.0f},
      {21.5f, 12.0f, 200.0f, 3.0f}, {21.5f, 12.0f, 200.0f, 3.0f}, {15.0f, 12.0f, 200.0f, 3.0f},
      {24.5f, 12.0f, 200.0f, 3.0f}, {24.5f, 12.0f, 200.0f, 3.0f}, {24.5f, 12.0f, 200.0f, 3.0f},
  };
  float worst[2], scale[2];
  assert(board_scales(loops, 2, 6, p, worst, scale) == 2);
  std::printf("worst %.1f / %.1f kPa, scale %.2f / %.2f\n", worst[0] / 1000, worst[1] / 1000, scale[0], scale[1]);
  assert(near(worst[0] / 1000.0f, 17.6f, 1.0f));
  assert(near(worst[1] / 1000.0f, 32.1f, 1.5f));
  assert(scale[1] == 1.0f);                      // the demanding board is never throttled
  assert(scale[0] > 0.65f && scale[0] < 0.8f);   // the easy board is

  // Disabled loops and loops without pipe data are ignored.
  loops[4].enabled = false;
  board_scales(loops, 2, 6, p, worst, scale);
  assert(worst[0] < 13000.0f && scale[0] < 0.7f);
  loops[4].enabled = true;

  // One board alone, or no pipe data anywhere: nothing to balance.
  assert(board_scales(loops, 1, 6, p, worst, scale) == 1 && scale[0] == 1.0f);
  Loop blank[2 * 6]{};
  assert(board_scales(blank, 2, 6, p, worst, scale) == 0 && scale[0] == 1.0f && scale[1] == 1.0f);

  // Floor: a tiny board next to a huge one is clamped at min_scale.
  Loop extreme[2 * 1] = {{2.0f, 16.0f, 300.0f, 0.0f}, {60.0f, 12.0f, 150.0f, 10.0f}};
  board_scales(extreme, 2, 1, p, worst, scale);
  assert(scale[0] == p.min_scale && scale[1] == 1.0f);

  std::puts("house_balance: ok");
  return 0;
}
