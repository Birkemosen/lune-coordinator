// Host tests for exposure_learning.h: recover solar and wind factors from a
// simulated room that is heated, cooled by wind and warmed by the sun.
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "exposure_learning.h"

using namespace lune_touch_learn;

static float rnd(unsigned &seed) {   // 0..1, deterministic
  seed = seed * 1103515245u + 12345u;
  return static_cast<float>((seed >> 8) & 0xFFFF) / 65535.0f;
}

// True room: a = 0.02 /h, wind doubles the loss at w = 1 (b = 0.6·a), the sun
// adds 1.6 °C/h at 800 W/m² (solar factor 0.8), heat d = 0.5 °C/h per unit.
static Sample simulate(unsigned &seed, float b_over_a, float c, float noise) {
  Sample x{};
  x.temp_c = 20.0f + 4.0f * rnd(seed);
  x.out_c = -2.0f + 14.0f * rnd(seed);
  x.wind = rnd(seed) < 0.4f ? 1.2f * rnd(seed) : 0.1f * rnd(seed);
  const bool day = rnd(seed) < 0.5f;
  x.ghi_wm2 = day ? 700.0f * rnd(seed) : 0.0f;
  x.valve = rnd(seed);
  x.supply_c = 27.0f + 10.0f * rnd(seed);
  const float a = 0.02f, b = b_over_a * a, d = 0.5f;
  float phi[N];
  features(x, phi);
  x.dtdt_c_per_h = a * phi[0] + b * phi[1] + c * phi[2] + d * phi[3] + noise * (rnd(seed) - 0.5f);
  return x;
}

int main() {
  // Learns sun 0.8 and wind 0.6 from mixed hours with heating on and off.
  {
    State s{};
    unsigned seed = 7;
    for (int i = 0; i < 600; i++)
      update(s, simulate(seed, 0.6f, 1.6f, 0.05f));
    const Learned r = result(s);
    std::printf("solar %.2f (n=%u) wind %.2f (n=%u)\n", r.solar, s.solar_n, r.wind, s.wind_n);
    assert(r.solar_valid && std::fabs(r.solar - 0.8f) < 0.08f);
    assert(r.wind_valid && std::fabs(r.wind - 0.6f) < 0.12f);
  }
  // A room the sun never reaches: solar ~0.
  {
    State s{};
    unsigned seed = 11;
    for (int i = 0; i < 600; i++)
      update(s, simulate(seed, 0.2f, 0.0f, 0.05f));
    const Learned r = result(s);
    assert(r.solar_valid && r.solar < 0.08f);
    assert(r.wind_valid && std::fabs(r.wind - 0.2f) < 0.12f);
  }
  // Too few informative hours: nothing reported yet.
  {
    State s{};
    unsigned seed = 3;
    for (int i = 0; i < 15; i++)
      update(s, simulate(seed, 0.6f, 1.6f, 0.05f));
    const Learned r = result(s);
    assert(!r.solar_valid && !r.wind_valid);
  }
  // Implausible jumps (a window opened, a sensor glitch) are rejected.
  {
    State s{};
    Sample x{};
    x.temp_c = 22; x.out_c = 5; x.dtdt_c_per_h = -6.0f;
    assert(!update(s, x));
    x.dtdt_c_per_h = 0.1f; x.out_c = 21.5f;   // indoor ~ outdoor: no information on loss
    assert(!update(s, x));
  }
  std::puts("exposure_learning: ok");
  return 0;
}
