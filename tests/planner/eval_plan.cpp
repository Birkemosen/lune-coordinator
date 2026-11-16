// Replays the planner against what Odin did (planner F2).
//
//   eval_plan house.bin [--target C] [--band K] [--strategy comfort|weighted|price]
//
// Closed loop in the model: from the first day's measured state, a model house is run
// day after day with the planner's heat, and a second with the heat Odin delivered. Each
// midnight the models are refitted on the measured history before it, the planner plans
// 48 h from its own house's state with the observed weather and prices, and the first
// 24 h are run. Both houses go through the same models, so the comparison is like for
// like: electricity cost, heat, and K·h below the floor. Odin's metered cost is shown
// for reference, and how far Odin's model house drifts from the measured one shows the
// model's bias.
#include "planner.h"
#include "solver.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace lune_planner;

int main(int argc, char **argv) {
  if (argc < 2) { std::fprintf(stderr, "usage: %s house.bin [--target C] [--band K] [--strategy s]\n", argv[0]); return 2; }
  float target = 22.0f, band = 0.5f;
  Strategy strat = Strategy::COMFORT;
  for (int i = 2; i + 1 < argc; i += 2) {
    if (!std::strcmp(argv[i], "--target")) target = std::atof(argv[i + 1]);
    else if (!std::strcmp(argv[i], "--band")) band = std::atof(argv[i + 1]);
    else if (!std::strcmp(argv[i], "--strategy"))
      strat = !std::strcmp(argv[i + 1], "price") ? Strategy::PRICE : !std::strcmp(argv[i + 1], "weighted") ? Strategy::WEIGHTED : Strategy::COMFORT;
  }
  FILE *f = std::fopen(argv[1], "rb");
  if (!f) return 1;
  uint8_t hb[HEADER_SIZE], rb[RECORD_SIZE];
  Header h;
  if (std::fread(hb, 1, HEADER_SIZE, f) != HEADER_SIZE || !read_header(hb, &h)) return 1;
  std::vector<Record> H;
  while (std::fread(rb, 1, RECORD_SIZE, f) == RECORD_SIZE) { Record r; read_record(h, rb, &r); H.push_back(r); }
  std::fclose(f);
  const size_t n = H.size();
  auto g = [&](size_t i, uint8_t c) { return H[i].get(Kind::HOUSE, c); };
  auto z = [](float v) { return std::isfinite(v) ? v : 0.0f; };
  std::vector<float> t(n);
  std::vector<HourIn> in(n);
  for (size_t i = 0; i < n; i++) {
    t[i] = g(i, H_ROOM_C);
    in[i].q_kw = z(g(i, H_HEAT_SH_KWH));
    in[i].out_c = std::isfinite(g(i, H_OUTDOOR_C)) ? g(i, H_OUTDOOR_C) : (i ? in[i - 1].out_c : 8.0f);
    in[i].sun_wm2 = z(g(i, H_SOLAR_WM2));
    in[i].wind_ms = z(g(i, H_WIND_MS));
    const int off = H[i].hour_start < 1792890000u ? 2 : 1;
    in[i].hour_local = static_cast<float>((H[i].hour_start / 3600 + off) % 24);
    in[i].sun_lag_wm2 = i ? sun_lag_filter(in[i - 1].sun_lag_wm2, in[i].sun_wm2) : 0.0f;
  }

  static HouseFitter fitter;
  HouseParams hp;
  EnergyBalance eb;
  size_t eb_to = 0;   // days fed to the energy balance end before this hour
  HeatPumpLearner hpl;
  size_t learned_to = 0;
  double plan_cost = 0, odin_cost = 0, odin_meter = 0, plan_heat = 0, odin_heat = 0, plan_below = 0, odin_below = 0;
  int days = 0;
  HouseState sp{NAN, NAN}, so{NAN, NAN};   // planner's and Odin's model houses
  double drift = 0;
  SolverSettings cfg;
  cfg.strategy = strat;
  cfg.room_c = target;
  std::printf("day        plan kWh  EUR   below Kh | Odin kWh  EUR (model)  EUR (metered)  below Kh\n");
  for (size_t d = 10 * 24; d + PLAN_H < n; d += 24) {
    size_t s = d;
    while (s < d + 24 && static_cast<int>(in[s].hour_local) != 0) s++;
    if (s + PLAN_H >= n || !std::isfinite(t[s])) continue;
    // Learn the heat pump from the full heating hours before s.
    for (; learned_to < s; learned_to++) {
      const size_t i = learned_to;
      if (g(i, H_RUN_MIN) >= 57 && !(z(g(i, H_HEAT_DHW_KWH)) >= 0.05f))
        hpl.learn(g(i, H_HEAT_SH_KWH), g(i, H_ELEC_KWH), g(i, H_HP_FEED_C), g(i, H_Z1_FEED_C), g(i, H_ROOM_C), g(i, H_OUTDOOR_C));
    }
    const size_t from = s > 14 * 24 ? s - 14 * 24 : 0;
    // Whole days before s into the energy balance, then the dynamics with UA, P and sun fixed.
    for (size_t k = eb_to; k + 24 <= s; k++) {
      if (static_cast<int>(in[k].hour_local) != 0) continue;
      eb.add_day(&t[k], &in[k]);
      eb_to = k + 1;
    }
    const bool energy = eb.apply(&hp);
    fitter.fit(&t[from], &in[from], s - from, &hp, 6, 30,
               energy ? (1u << HouseFitter::UA) | (1u << HouseFitter::PINT) | (1u << HouseFitter::SUN) | (1u << HouseFitter::WIND) : 0u);
    std::vector<float> ts(s - from + 1);
    track_floor(hp, &t[from], &in[from], ts.size(), t[from] + 2.0f, ts.data());
    if (!std::isfinite(sp.t_c)) sp = so = HouseState{t[s], ts[s - from]};
    const HouseState start = sp;
    PlanHour ph[PLAN_H];
    for (int k = 0; k < PLAN_H; k++) {
      ph[k].weather = in[s + k];
      ph[k].price_eur_kwh = g(s + k, H_PRICE_EUR_KWH);
      ph[k].band_min_c = target - band;
      ph[k].band_max_c = target + 1.0f;
    }
    HeatPlan plan;
    solve_plan(hp, hpl.params(), start, ph, PLAN_H, cfg, &plan);
    // Odin's actual heat for the same 48 h through the same models.
    HeatPlan odin;
    odin.n = PLAN_H;
    for (int k = 0; k < PLAN_H; k++) odin.kw[k] = in[s + k].q_kw;
    simulate_plan(hp, so, ph, &odin);
    finish_plan(hpl.params(), ph, target, &odin);
    // Keep the first 24 h of both.
    double pc = 0, oc = 0, om = 0, phh = 0, oh = 0, pb = 0, ob = 0;
    for (int k = 0; k < 24; k++) {
      pc += plan.elec_kwh[k] * z(ph[k].price_eur_kwh);
      oc += odin.elec_kwh[k] * z(ph[k].price_eur_kwh);
      const float el = g(s + k, H_ELEC_KWH), dh = z(g(s + k, H_HEAT_DHW_KWH));
      if (std::isfinite(el)) om += std::fmax(0.0f, el - dh / 2.85f) * z(ph[k].price_eur_kwh);
      phh += plan.kw[k]; oh += odin.kw[k];
      if (plan.t_end_c[k] < target - band) pb += target - band - plan.t_end_c[k];
      if (odin.t_end_c[k] < target - band) ob += target - band - odin.t_end_c[k];
    }
    // Advance both model houses by the 24 h that were run.
    for (int k = 0; k < 24; k++) {
      HourIn a = in[s + k], b = in[s + k];
      a.q_kw = plan.kw[k];
      sp = house_step(hp, sp, a);
      so = house_step(hp, so, b);
    }
    if (std::isfinite(t[s + 24])) drift += so.t_c - t[s + 24];
    char date[16];
    const time_t tt = H[s].hour_start;
    std::strftime(date, sizeof date, "%Y-%m-%d", std::gmtime(&tt));
    std::printf("%s  %6.1f  %5.2f  %5.1f   | %6.1f   %5.2f       %5.2f        %5.1f\n", date, phh, pc, pb, oh, oc, om, ob);
    plan_cost += pc; odin_cost += oc; odin_meter += om; plan_heat += phh; odin_heat += oh; plan_below += pb; odin_below += ob;
    days++;
  }
  std::printf("total %d days: planner %.0f kWh %.2f EUR %.1f Kh below | Odin %.0f kWh %.2f EUR (model) %.2f EUR (metered) %.1f Kh below\n",
              days, plan_heat, plan_cost, plan_below, odin_heat, odin_cost, odin_meter, odin_below);
  std::printf("planner vs Odin (same model): %+.0f %% cost; Odin's model house ends %.2f K from the measured on average\n",
              100.0 * (plan_cost / odin_cost - 1.0), days ? drift / days : NAN);
  std::printf("last model: UA %.3f kW/K, P %.2f kW, sun %.2f kW/100 W/m2, Ca %.1f, Cs %.1f, H %.2f (energy balance on %u days)\n",
              hp.ua_kw_per_k, hp.p_int_kw, hp.g_kw_per_100wm2, hp.ca_kwh_per_k, hp.cs_kwh_per_k, hp.h_kw_per_k, eb.days());
  return 0;
}
