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
  truth.cs_kwh_per_k = 45.0f;
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

  std::printf("planner house_model: all assertions passed\n");
  return 0;
}
