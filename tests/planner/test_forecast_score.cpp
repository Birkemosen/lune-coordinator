// Host tests for planner/forecast_score.h.
#include "forecast_score.h"

#include <cassert>
#include <cmath>
#include <cstdio>

using namespace lune_planner;

static bool near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

int main() {
  // Pending forecasts: scored when their hour comes, the first forecast for a target kept.
  static PendingForecasts<32> pf;
  ScoreSet sc;
  const uint32_t h0 = 1795200000u - 1795200000u % 3600u;
  float plan[24];
  for (int k = 0; k < 24; k++) plan[k] = 22.0f + 0.1f * (k + 1);   // end of hour k = h0 + (k + 1) h
  pf.put_plan(h0, plan, 24, 22.0f);
  float later[24];
  for (int k = 0; k < 24; k++) later[k] = 30.0f;
  pf.put_plan(h0, later, 24, 25.0f);   // a re-plan in the same hour does not replace them
  pf.score(h0 + 3600u, 22.0f, &sc);    // the 1 h forecast: 22.1 against 22.0
  assert(sc.h[0].count == 1 && near(sc.h[0].bias(), 0.1f, 1e-4f) && near(sc.h[0].persistence_rmse(), 0.0f, 1e-4f));
  pf.score(h0 + 3600u, 22.0f, &sc);    // already scored: nothing more
  assert(sc.h[0].count == 1);
  pf.score(h0 + 6u * 3600u, 22.3f, &sc);   // 6 h: 22.6 against 22.3, persistence 22.0
  assert(sc.h[2].count == 1 && near(sc.h[2].rmse(), 0.3f, 1e-4f) && near(sc.h[2].persistence_rmse(), 0.3f, 1e-4f));
  pf.score(h0 + 3u * 3600u, NAN, &sc);    // no measurement: freed, not scored
  assert(sc.h[1].count == 0);
  pf.put(1, h0 + 3u * 3600u, 21.0f, 21.0f);
  pf.score(h0 + 3u * 3600u, 21.5f, &sc);
  assert(sc.h[1].count == 1);

  // The house model re-run on what happened: exact on hours the model itself made, and
  // better than persistence while the house moves.
  HouseParams p;
  p.ua_kw_per_k = 0.18f; p.ca_kwh_per_k = 20; p.cs_kwh_per_k = 20; p.h_kw_per_k = 2; p.p_int_kw = 0.5f;
  const size_t n = 96;
  float t[n + 1], track[n + 1];
  HourIn in[n];
  HouseState st{21.0f, 23.0f};
  // Settle the floors first, so the tracked floor (from 2 K above the first hour) agrees.
  for (int k = 0; k < 400; k++) { HourIn x; x.out_c = 5; x.q_kw = 2.5f; st = house_step(p, st, x); }
  for (size_t i = 0; i < n; i++) {
    t[i] = st.t_c;
    in[i] = HourIn{};
    in[i].out_c = 5.0f;
    in[i].hour_local = static_cast<float>(i % 24);
    in[i].q_kw = (i % 24) < 8 ? 7.0f : 0.0f;   // a charge every night
    st = house_step(p, st, in[i]);
  }
  t[n] = st.t_c;
  ScoreSet hs;
  score_house_hour(p, t, in, n, track, &hs);
  for (int j = 0; j < SCORE_N; j++) {
    std::printf("house model h%-2d err %.3f persistence %.3f\n", SCORE_H[j], hs.h[j].rmse(), hs.h[j].persistence_rmse());
    assert(hs.h[j].count == 1 && hs.h[j].rmse() < 0.05f);
  }
  assert(hs.h[3].persistence_rmse() > hs.h[3].rmse());
  // Too short a window: nothing scored.
  ScoreSet none;
  score_house_hour(p, t, in, 30, track, &none);
  assert(none.h[0].count == 0);

  std::printf("planner forecast_score: all assertions passed\n");
  return 0;
}
