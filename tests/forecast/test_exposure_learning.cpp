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
  features(x, valve_heat(x), phi);
  x.dtdt_c_per_h = a * phi[0] + b * phi[1] + c * phi[2] + d * phi[3] + noise * (rnd(seed) - 0.5f);
  return x;
}


// A room on a floor slab, simulated in 6-minute steps: the valve heats the slab, the
// slab heats the room, the room loses UA·(1 + f·w)·(T − Tout). The sensor reads in
// 0.1 °C steps and the learner sees one sample an hour, as on the device.
struct SlabRoom {
  float ua_w_k, c_room_kwh_k, c_slab_kwh_k, h_slab_w_k, h_valve_w_k, wind_f;
};

struct SlabRun {
  Learned learned;
  float raw_ratio;   // theta1 / theta0 as the old result() read it
};

static SlabRun run_slab(const SlabRoom &r, float lag_h, unsigned seed, int hours) {
  State s{};
  float t = 22.0f, slab = 24.0f, out = 4.0f, wind_ms = 3.0f, align = 0.6f;
  bool open = false;
  Sample start{};
  bool have = false;
  auto quant = [](float v) { return std::round(v * 10.0f) / 10.0f; };
  for (int h = 0; h < hours; h++) {
    // Weather for the hour: a daily swing, and windy spells of 1–2 days.
    out = 3.0f + 4.0f * std::sin(static_cast<float>(h) * 0.2618f) + 2.0f * (rnd(seed) - 0.5f);
    if (h % 30 == 0) {
      wind_ms = rnd(seed) < 0.4f ? 7.0f + 6.0f * rnd(seed) : 1.0f + 3.0f * rnd(seed);
      align = 0.3f + 0.7f * rnd(seed);
    }
    const float supply = 31.0f + 3.0f * rnd(seed);
    // The hour's sample starts here.
    Sample x{};
    x.temp_c = quant(t);
    x.out_c = out;
    x.wind = align * wind_ms / 10.0f;
    x.ghi_wm2 = 0.0f;
    x.valve = open ? 1.0f : 0.0f;
    x.supply_c = supply;
    x.lag_h = lag_h;
    if (have) {
      start.dtdt_c_per_h = x.temp_c - start.temp_c;
      update(s, start);
    }
    start = x;
    have = true;
    for (int k = 0; k < 10; k++) {   // 6-minute physics steps
      const float dt_h = 0.1f;
      if (t < 21.9f) open = true;
      if (t > 22.3f) open = false;
      const float q_in = open ? r.h_valve_w_k * std::max(0.0f, supply - slab) : 0.0f;
      const float q_sr = r.h_slab_w_k * (slab - t);
      const float q_loss = r.ua_w_k * (1.0f + r.wind_f * x.wind) * (t - out);
      slab += (q_in - q_sr) / 1000.0f * dt_h / r.c_slab_kwh_k;
      t += (q_sr - q_loss) / 1000.0f * dt_h / r.c_room_kwh_k;
    }
  }
  const float a_prior = r.ua_w_k / 1000.0f / (r.c_room_kwh_k + r.c_slab_kwh_k);
  return {result(s, a_prior), s.theta[0] > 1e-4f ? s.theta[1] / s.theta[0] : INFINITY};
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
  // A windward door and roof window: 10 m/s more than doubles the loss (Laura). The
  // factor is no longer held at 1.0; it stops at WIND_MAX.
  {
    State s{};
    unsigned seed = 5;
    for (int i = 0; i < 600; i++)
      update(s, simulate(seed, 1.8f, 0.4f, 0.05f));
    const Learned r = result(s);
    std::printf("windward room: wind %.2f (n=%u)\n", r.wind, s.wind_n);
    assert(r.wind_valid && std::fabs(r.wind - 1.8f) < 0.25f);
    State t{};
    seed = 9;
    for (int i = 0; i < 600; i++)
      update(t, simulate(seed, 6.0f, 0.4f, 0.05f));
    assert(result(t).wind_valid && result(t).wind == WIND_MAX);
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
  // Heavy ground-floor room (Pejsestue: UA 30.7 W/K, C ≈ 6 kWh/K, floor lag ~2 h) with a
  // true wind factor of 0.3. Read the old way (heat of the hour, no physics floor, no
  // shrinking) the fit is unusable; with the guards it lands near the truth or below.
  {
    const SlabRoom pejs{30.7f, 3.8f, 2.2f, 375.0f, 250.0f, 0.3f};
    const SlabRun old_way = run_slab(pejs, 0.0f, 21, 1200);
    const SlabRun guarded = run_slab(pejs, 2.0f, 21, 1200);
    std::printf("heavy room: old b/a %.2f, guarded wind %.2f (valid %d)\n", old_way.raw_ratio,
                guarded.learned.wind, guarded.learned.wind_valid);
    assert(guarded.learned.wind_valid);
    assert(guarded.learned.wind < 0.7f);
    assert(guarded.learned.wind < WIND_MAX);
  }
  // No wind effect at all in a heavy room: it must not read as a windy room.
  {
    const SlabRoom still{30.7f, 3.8f, 2.2f, 375.0f, 250.0f, 0.0f};
    const SlabRun guarded = run_slab(still, 2.0f, 33, 1200);
    std::printf("heavy room, no wind effect: wind %.2f\n", guarded.learned.wind);
    assert(guarded.learned.wind < 0.4f);
  }
  // Light upstairs room with a windward door (Laura: UA 17.6 W/K, C ≈ 1.5 kWh/K, true
  // factor 1.5): the guards keep a strong, clear wind effect.
  {
    const SlabRoom laura{17.6f, 1.0f, 0.5f, 250.0f, 150.0f, 1.5f};
    const SlabRun guarded = run_slab(laura, 2.0f, 44, 1200);
    std::printf("light windward room: wind %.2f\n", guarded.learned.wind);
    assert(guarded.learned.wind_valid);
    assert(guarded.learned.wind > 1.0f && guarded.learned.wind < WIND_MAX);   // hourly data: ±30 %
  }
  // The device's state on 2026-11-26 for Kontor: a fitted to ~0 by the slab, so b/a hit
  // the clamp (2.0). With the physics a the same fit reads as a modest factor.
  {
    State s{};
    reset(s);
    s.theta[0] = 0.0003f;   // a: the slab's heat made the room look loss-free
    s.theta[1] = 0.0015f;   // b
    s.wind_n = 176;
    s.n = 400;
    s.resid_var = 2.0e-3f;
    for (int i = 0; i < N; i++)
      for (int j = 0; j < N; j++)
        s.p[i][j] = i == j ? 1.0e-4f : 0.0f;
    assert(result(s).wind == WIND_MAX);              // as before: no physics known
    const float a_prior = 14.7f / 1000.0f / 2.88f;    // UA / C for Kontor (V6)
    const Learned r = result(s, a_prior);
    std::printf("Kontor's fit with the physics a: wind %.2f\n", r.wind);
    assert(r.wind_valid && r.wind < 0.5f);
  }
  std::puts("exposure_learning: ok");
  return 0;
}
