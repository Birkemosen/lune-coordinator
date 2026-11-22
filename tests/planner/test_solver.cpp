// Host tests for planner/solver.h.
#include "solver.h"

#include <cassert>
#include <cmath>
#include <cstdio>

using namespace lune_planner;

static void day(PlanHour h[PLAN_H], float out, float cheap_from, float cheap_to) {
  for (int i = 0; i < PLAN_H; i++) {
    h[i] = PlanHour{};
    h[i].weather.out_c = out;
    h[i].weather.hour_local = static_cast<float>(i % 24);
    const int hr = i % 24;
    h[i].price_eur_kwh = (hr >= cheap_from && hr < cheap_to) ? 0.15f : 0.40f;
    h[i].band_min_c = 21.5f;
    h[i].band_max_c = 23.0f;
  }
}

int main() {
  HouseParams hp;            // defaults: a house losing ~2.5 kW at 22 °C / 8 °C
  HeatPumpParams pump;
  HouseState start{22.0f, 24.0f};
  PlanHour h[PLAN_H];
  day(h, 5.0f, 0, 6);
  SolverSettings cfg;
  HeatPlan p;

  // Comfort first: no hour ends below the floor, no hour above the top.
  solve_plan(hp, pump, start, h, PLAN_H, cfg, &p);
  float tmin = 99, tmax = -99;
  for (int i = 0; i < PLAN_H; i++) { tmin = std::fmin(tmin, p.t_end_c[i]); tmax = std::fmax(tmax, p.t_end_c[i]); }
  std::printf("comfort: %.1f kWh, %.2f EUR, %.2f..%.2f C, below %.2f Kh\n", p.heat_kwh, p.cost_eur, tmin, tmax, p.below_kh);
  assert(p.below_kh < 0.05f && tmax <= 23.1f);   // a step may overshoot the top slightly
  // Every hour is off or within the pump's range.
  for (int i = 0; i < PLAN_H; i++) assert(p.kw[i] == 0 || (p.kw[i] >= pump.min_kw - 1e-3f && p.kw[i] <= pump.max_kw + 1e-3f));
  // Cheap hours carry more of the heat than their share of the day.
  float cheap = 0;
  for (int i = 0; i < PLAN_H; i++) if (i % 24 < 6) cheap += p.kw[i];
  assert(cheap > p.heat_kwh * 6.0f / 24.0f);
  // Planned zone feed follows the floor curve.
  for (int i = 0; i < PLAN_H; i++)
    if (p.kw[i] > 0) assert(std::fabs(p.zone_feed_c[i] - zone_feed_for(pump, p.kw[i], cfg.room_c)) < 1e-3f);

  // Price first is never dearer than comfort first and stays within its dip; weighted
  // lies between the two.
  SolverSettings pc = cfg;
  pc.strategy = Strategy::PRICE;
  HeatPlan q;
  solve_plan(hp, pump, start, h, PLAN_H, pc, &q);
  float qmin = 99;
  for (int i = 0; i < PLAN_H; i++) qmin = std::fmin(qmin, q.t_end_c[i]);
  std::printf("price:   %.1f kWh, %.2f EUR, min %.2f C, below %.2f Kh\n", q.heat_kwh, q.cost_eur, qmin, q.below_kh);
  assert(q.cost_eur <= p.cost_eur + 1e-3f && qmin >= 21.0f - 0.02f && q.heat_kwh > 0);
  SolverSettings wc = cfg;
  wc.strategy = Strategy::WEIGHTED;
  wc.penalty_eur_per_kh = 0.3f;
  HeatPlan w;
  solve_plan(hp, pump, start, h, PLAN_H, wc, &w);
  std::printf("weighted:%.1f kWh, %.2f EUR, below %.2f Kh\n", w.heat_kwh, w.cost_eur, w.below_kh);
  assert(w.cost_eur >= q.cost_eur - 1e-3f && w.cost_eur <= p.cost_eur + 1e-3f);

  // Sun ahead: a cold night before a sunny late morning. With the hold, the plan lets the
  // house sag (at most sun_hold_k) before the sun and puts in less heat; without it, the
  // plan charges to hold the floor all night.
  {
    PlanHour s[PLAN_H];
    day(s, 2.0f, 0, 6);
    for (int i = 0; i < PLAN_H; i++) {
      const int hr = i % 24;
      s[i].weather.sun_wm2 = hr >= 9 && hr <= 14 ? 600.0f : 0.0f;
      s[i].band_min_c = 22.0f;
      s[i].band_max_c = 24.0f;
    }
    HouseParams sh = hp;
    sh.g_kw_per_100wm2 = 1.5f;   // a sunny house
    const HouseState st{22.1f, 22.6f};
    SolverSettings off = cfg, on = cfg;
    off.sun_hold_k = 0.0f;
    HeatPlan a, b;
    solve_plan(sh, pump, st, s, 12, off, &a);
    solve_plan(sh, pump, st, s, 12, on, &b);
    float relief = 0, bmin = 99;
    for (int i = 0; i < b.n; i++) { relief = std::fmax(relief, b.relief_k[i]); bmin = std::fmin(bmin, b.t_end_c[i]); }
    std::printf("sun hold: %.1f kWh without, %.1f kWh with (relief %.2f K, min %.2f C)\n", a.heat_kwh, b.heat_kwh, relief, bmin);
    assert(relief > 0.0f && relief <= on.sun_hold_k + 1e-4f);
    assert(b.heat_kwh < a.heat_kwh);
    assert(bmin >= 22.0f - on.sun_hold_k - 0.05f);
    for (int i = 0; i < a.n; i++) assert(a.relief_k[i] == 0.0f);
  }

  // Catching up: the house 0.8 K below a floor that holds for 7 h, on a flat price day (22
  // Nov 2026). Without the catch-up, the plan front-loads high output at a poor COP; with
  // it, the first hours run lower and the plan costs less. Above the floor it changes nothing.
  {
    PlanHour c[PLAN_H];
    day(c, 3.0f, 0, 0);
    for (int i = 0; i < PLAN_H; i++) {
      c[i].price_eur_kwh = 0.30f;
      c[i].band_min_c = i < 7 ? 22.8f : 21.5f;
      c[i].band_max_c = i < 7 ? 23.8f : 22.5f;
    }
    HouseParams ch = hp;
    ch.ca_kwh_per_k = 14.0f;
    ch.cs_kwh_per_k = 30.0f;
    const HouseState st{22.0f, 24.4f};
    SolverSettings off = cfg, on = cfg;
    off.catchup_cop_frac = 0.0f;
    HeatPlan a, b;
    solve_plan(ch, pump, st, c, PLAN_H, off, &a);
    solve_plan(ch, pump, st, c, PLAN_H, on, &b);
    std::printf("catch-up: first hour %.1f kW without, %.1f kW with; %.2f vs %.2f EUR\n", a.kw[0], b.kw[0], a.cost_eur, b.cost_eur);
    assert(b.kw[0] < a.kw[0]);
    assert(b.cost_eur < a.cost_eur);
    assert(b.catchup_k[0] > 0.0f && b.catchup_k[0] <= 0.8f + 1e-4f);
    for (int i = 0; i < a.n; i++) assert(a.catchup_k[i] == 0.0f);
    for (int i = 7; i < b.n; i++) assert(b.catchup_k[i] == 0.0f);   // the floor below the house: nothing to catch up
    HeatPlan d;
    solve_plan(ch, pump, HouseState{23.0f, 24.4f}, c, PLAN_H, on, &d);
    for (int i = 0; i < d.n; i++) assert(d.catchup_k[i] == 0.0f);
  }

  // Flow controller: on plan, the curve's feed; overdelivery trims down, never past bounds.
  FlowController fc;
  const float base = fc.update(pump, 4.5f, 4.5f, 22.0f);
  assert(std::fabs(base - zone_feed_for(pump, 4.5f, 22.0f)) < 1e-3f);
  float t = base;
  for (int k = 0; k < 20; k++) t = fc.update(pump, 4.5f, 10.0f, 22.0f);   // the 16 Nov runaway
  assert(t < base && t >= base - 3.0f - 1e-3f);
  assert(std::isnan(fc.update(pump, 0.0f, 0.0f, 22.0f)));               // no plan, no target

  std::printf("planner solver: all assertions passed\n");
  return 0;
}
