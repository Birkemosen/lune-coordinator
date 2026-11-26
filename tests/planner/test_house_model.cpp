// Host tests for planner/house_model.h: the fitter recovers a known house.
#include "house_model.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace lune_planner;

int main() {
  // A synthetic house driven by blocks of heat, a daily outdoor swing, sun and wind.
  HouseParams truth;
  truth.ua_kw_per_k = 0.17f;
  truth.ca_kwh_per_k = 9.0f;
  truth.cs_kwh_per_k = 20.0f;
  truth.h_kw_per_k = 1.4f;
  truth.g_kw_per_100wm2 = 0.6f;
  truth.p_int_kw = 0.4f;
  truth.kw_per_ms = 0.03f;
  truth.p_cos_kw = 0.2f;
  truth.p_sin_kw = -0.1f;
  const size_t n = 24 * 14;
  std::vector<HourIn> in(n);
  std::vector<float> t(n + 1);
  HouseState s{22.0f, 25.0f};
  t[0] = s.t_c;
  for (size_t i = 0; i < n; i++) {
    const int hr = static_cast<int>(i % 24);
    in[i].hour_local = static_cast<float>(hr);
    in[i].out_c = 6.0f + 3.0f * std::sin((hr - 9) * 0.2618f);
    in[i].sun_wm2 = hr >= 9 && hr <= 15 ? 250.0f * std::sin((hr - 8) * 0.39f) : 0.0f;
    in[i].wind_ms = 3.0f + 3.0f * std::sin(i * 0.05f);
    in[i].q_kw = (i / 6) % 3 == 0 ? 6.0f : 0.0f;   // 6 h on, 12 h off
    s = house_step(truth, s, in[i]);
    t[i + 1] = s.t_c;
  }

  // Start the fit from the defaults (the prior) and let it find the house.
  static HouseFitter fitter;
  HouseParams p;
  fitter.fit(t.data(), in.data(), n, &p);
  std::printf("fit: UA %.3f Ca %.1f Cs %.1f H %.2f g %.2f P %.2f kw %.3f, RMSE %.3f K\n", p.ua_kw_per_k,
              p.ca_kwh_per_k, p.cs_kwh_per_k, p.h_kw_per_k, p.g_kw_per_100wm2, p.p_int_kw, p.kw_per_ms, fitter.rmse());
  assert(p.valid);
  assert(fitter.rmse() < 0.05f);                        // reproduces the trajectories
  assert(std::fabs(p.ua_kw_per_k - truth.ua_kw_per_k) < 0.03f);
  assert(std::fabs(p.cs_kwh_per_k - truth.cs_kwh_per_k) / truth.cs_kwh_per_k < 0.3f);

  // Too little data is refused.
  HouseParams q;
  assert(!fitter.fit(t.data(), in.data(), 48, &q));

  // The energy balance learns the extra loss in cold weather from the days below
  // COLD_REF_C, and leaves it at 0 while every day is milder.
  {
    HouseParams house;
    house.ua_kw_per_k = 0.16f; house.kw_per_ms = 0.05f; house.cold_per_k = 0.04f;
    house.p_int_kw = 0.5f; house.g_kw_per_100wm2 = 0.8f;
    auto day = [&](float out_mean, float wind, EnergyBalance *eb) {
      float td[25];
      HourIn hi[24];
      for (int h = 0; h < 24; h++) {
        td[h] = 22.0f;
        hi[h] = HourIn{};
        hi[h].out_c = out_mean + 2.0f * std::sin((h - 9) * 0.2618f);
        hi[h].wind_ms = wind;
        hi[h].sun_wm2 = h >= 9 && h <= 15 ? 80.0f : 0.0f;
        hi[h].q_kw = house.ua_kw_per_k * loss_factor(house, hi[h].out_c, wind) * (td[h] - hi[h].out_c) -
                     house.p_int_kw - house.g_kw_per_100wm2 * hi[h].sun_wm2 / 100.0f;
      }
      td[24] = 22.0f;
      eb->add_day(td, hi);
    };
    EnergyBalance cold, mild;
    for (int d = 0; d < 40; d++) {
      const float wind = 2.0f + (d % 5);
      day(12.0f - 0.35f * d, wind, &cold);   // 12 °C down to −2 °C
      day(10.0f + (d % 6), wind, &mild);     // never below 8 °C
    }
    HouseParams pc, pm;
    assert(cold.apply(&pc) && mild.apply(&pm));
    std::printf("balance: cold UA %.3f kw %.3f kc %.3f P %.2f; mild kc %.3f\n", pc.ua_kw_per_k, pc.kw_per_ms,
                pc.cold_per_k, pc.p_int_kw, pm.cold_per_k);
    assert(std::fabs(pc.cold_per_k - house.cold_per_k) < 0.01f);
    assert(std::fabs(pc.ua_kw_per_k - house.ua_kw_per_k) < 0.015f);
    assert(pm.cold_per_k < 0.005f);
    // A day with a few unlogged hours counts, scaled up; one with too many does not.
    EnergyBalance gaps;
    for (int d = 0; d < 10; d++) {
      float td[25];
      HourIn hi[24];
      for (int h = 0; h < 24; h++) {
        td[h] = (h >= 9 && h < 9 + (d < 5 ? 3 : 5)) ? NAN : 22.0f;   // 3 h or 5 h lost to a restart
        hi[h] = HourIn{};
        hi[h].out_c = 2.0f;
        hi[h].q_kw = std::isfinite(td[h]) ? 4.0f : 0.0f;
      }
      td[24] = 22.0f;
      gaps.add_day(td, hi);
    }
    assert(gaps.days() == 5);
    // The loss now: 1 + wind + cold below COLD_REF_C only.
    assert(std::fabs(loss_factor(house, 10.0f, 0.0f) - 1.0f) < 1e-6f);
    assert(std::fabs(loss_factor(house, -2.0f, 4.0f) - (1.0f + 0.2f + 0.4f)) < 1e-5f);
  }

  std::printf("planner house_model: all assertions passed\n");
  return 0;
}
