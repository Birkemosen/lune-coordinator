// Host tests for comfort_signal.h, with the night of 20–21 Oct 2026 as the case.
#include <cassert>
#include <cmath>
#include <cstdio>

#include "comfort_signal.h"

using namespace lune_touch_comfort;
static bool near(float a, float b, float t) { return std::fabs(a - b) <= t; }

int main() {
  // 22:00: ground floor warm, upper floor cold; Stue (with Stue Repo, group mean) −1.3.
  //            Kontor Hobby Brygg Køkk Pejs  Bad  Joseph Laura Toil  Stue  Sove
  const float dev[] = {0.2f, -0.7f, 1.0f, 0.8f, 1.1f, 0.9f, -0.1f, -0.2f, 0.3f, -1.3f, -0.7f};
  const float w[]   = {18, 15, 28, 32, 37.5f, 14.5f, 21.5f, 21.5f, 15, 49, 24.5f};
  const size_t n = sizeof(dev) / sizeof(dev[0]);
  // Plain weighted average: above target, so the heat pump stayed off.
  float avg = 0, ws = 0;
  for (size_t i = 0; i < n; i++) { avg += w[i] * dev[i]; ws += w[i]; }
  avg /= ws;
  assert(avg > 0.0f);
  // Signal: Stue is 0.8 beyond the tolerance; one room pulls at most 0.5.
  const float d = deviation(dev, w, n);
  std::printf("avg %+.2f  signal %+.2f\n", avg, d);
  assert(near(d, -0.5f, 0.01f));
  // Without the cap Stue would pull the full 0.8.
  Params uncapped;
  uncapped.max_pull_c = 10.0f;
  assert(near(deviation(dev, w, n, uncapped), -0.8f, 0.01f));
  // A whole house below target is not capped (the mean itself).
  const float cold[] = {-1.0f, -1.0f};
  const float wc[] = {1, 1};
  assert(near(deviation(cold, wc, 2), -1.0f, 0.001f));

  // All rooms within ±0.5: the asymmetric mean alone (surplus at 30 %).
  const float dev2[] = {0.4f, -0.3f};
  const float w2[] = {1, 1};
  assert(near(deviation(dev2, w2, 2), (-0.3f + 0.12f) / 2, 0.001f));
  // Warm house: still reports above target, just less (it can stop heating).
  const float dev3[] = {1.0f, 0.5f};
  assert(deviation(dev3, w2, 2) > 0.0f);
  // A tiny room (< 5 % of the weight) cannot pull the house alone.
  const float dev4[] = {0.0f, -3.0f};
  const float w4[] = {100, 2};
  assert(deviation(dev4, w4, 2) > -0.1f);
  // No rooms: NAN.
  assert(std::isnan(deviation(dev, w, 0)));
  // Slew: 0.1 °C per 5 min, both ways; no previous value passes through.
  assert(near(slew(22.3f, 20.7f, 300000), 22.2f, 0.001f));
  assert(near(slew(22.3f, 20.7f, 60000), 22.28f, 0.001f));
  assert(near(slew(22.0f, 22.05f, 300000), 22.05f, 0.001f));
  assert(near(slew(21.0f, 22.0f, 600000), 21.2f, 0.001f));
  assert(near(slew(NAN, 21.3f, 0), 21.3f, 0.001f));
  std::puts("comfort_signal: ok");
  return 0;
}
