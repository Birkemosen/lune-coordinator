// Host tests for comfort_signal.h.
#include <cassert>
#include <cmath>
#include <cstdio>

#include "comfort_signal.h"

using namespace lune_touch_comfort;
static bool near(float a, float b, float t) { return std::fabs(a - b) <= t; }

int main() {
  // Night of 20-21 Oct: ground floor warm, upper floor cold. The signal is the plain
  // UA-weighted mean (+0.07): Stue's lag is handled by V6 (flow to Stue) and, if its
  // loop is saturated, by lifting Odin's schedule, not by faking the temperature.
  //            Kontor Hobby Brygg Køkk Pejs  Bad  Joseph Laura Toil  Stue  Sove
  const float dev[] = {0.2f, -0.7f, 1.0f, 0.8f, 1.1f, 0.9f, -0.1f, -0.2f, 0.3f, -1.3f, -0.7f};
  const float w[]   = {18, 15, 28, 32, 37.5f, 14.5f, 21.5f, 21.5f, 15, 49, 24.5f};
  const size_t n = sizeof(dev) / sizeof(dev[0]);
  float avg = 0, ws = 0;
  for (size_t i = 0; i < n; i++) { avg += w[i] * dev[i]; ws += w[i]; }
  avg /= ws;
  const float d = deviation(dev, w, n);
  std::printf("weighted mean %+.3f  signal %+.3f\n", avg, d);
  assert(near(d, avg, 1e-4f));

  // 21 Oct evening: every room warm -> the full surplus (Touch showed 23.6, Asgard got 22.2).
  const float warm[] = {1.7f, 0.4f, 2.2f, 1.6f, 1.6f, 2.0f, 2.0f, 1.5f, 2.2f, 2.4f, 1.3f, 0.5f};
  const float ww[] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
  assert(deviation(warm, ww, 12) > 1.5f);
  // Symmetric: a cold room counts exactly as much as a warm one of the same weight.
  const float sym[] = {1.0f, -1.0f};
  const float w2[] = {1, 1};
  assert(near(deviation(sym, w2, 2), 0.0f, 1e-6f));
  // Weights: the leakier room counts more.
  const float two[] = {1.0f, 0.0f};
  const float w3[] = {3, 1};
  assert(near(deviation(two, w3, 2), 0.75f, 1e-6f));
  // Missing values and zero weights are skipped; no rooms -> NAN.
  const float gap[] = {NAN, 0.5f};
  const float w4[] = {1, 0};
  assert(std::isnan(deviation(gap, w4, 2)));
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
