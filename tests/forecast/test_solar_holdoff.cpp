// Host tests for solar_holdoff.h (lower a sunny room's target before the sun).
#include <cassert>
#include <cmath>
#include <cstdio>

#include "solar_holdoff.h"

using namespace lune_touch_solar;

static bool near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

int main() {
  // 06..17 local: sun builds to 700 W/m² at noon.
  const float ghi[12] = {0, 20, 120, 280, 450, 600, 700, 650, 500, 300, 100, 0};

  // A sunny upper-floor room (solar factor 0.6) at target, three hours before noon.
  Decision d = plan(ghi, 12, 3, 0.6f, 23.0f, 23.0f);
  assert(d.active && d.offset_c < 0.0f);
  assert(d.peak_wm2 == 700.0f && d.peak_in_h == 3);
  // Strongest weighted hour is 600 W/m² at +2 h (strength 1, timing 5/9): -1.5×0.6×5/9 = -0.5.
  assert(near(d.offset_c, -0.5f, 0.02f));

  // At noon the full sun is now: -1.5 × 0.6 = -0.9.
  d = plan(ghi, 12, 6, 0.6f, 23.0f, 23.0f);
  assert(near(d.offset_c, -0.9f, 0.02f));

  // A room with less glazing gets less; the default factor (0.3) gets none.
  assert(near(plan(ghi, 12, 6, 0.4f, 22.0f, 22.0f).offset_c, -0.6f, 0.02f));
  assert(!plan(ghi, 12, 6, 0.3f, 22.0f, 22.0f).active);

  // Overcast or night: nothing.
  const float grey[6] = {80, 120, 150, 200, 180, 90};
  assert(!plan(grey, 6, 0, 1.0f, 22.0f, 22.0f).active);
  assert(!plan(ghi, 12, 0, 1.0f, 22.0f, 22.0f).active);   // 06–09: sun below 250 W/m²

  // A room already below target - margin is left alone (cloud risk).
  assert(!plan(ghi, 12, 6, 0.6f, 22.6f, 23.0f).active);
  // Unknown temperature: no cold check.
  assert(plan(ghi, 12, 6, 0.6f, NAN, 23.0f).active);

  // Never beyond max_offset_c, whatever the factor.
  assert(plan(ghi, 12, 6, 5.0f, 22.0f, 22.0f).offset_c >= -1.5f - 1e-4f);

  std::puts("solar_holdoff: ok");
  return 0;
}
