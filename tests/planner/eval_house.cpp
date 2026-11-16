// Replays the house model over a recorded history and scores it (planner F1).
//
//   eval_house house.bin [--tau H] [--lambda L] [--warmup-days D]
//
// The learner sees each hour once, in order. From every hour after the warm-up it
// forecasts the house temperature 24 h ahead with the parameters it has at that moment,
// the measured space heating and the observed weather, and the forecast is compared
// with what happened. Per day it also predicts the heat the house needed to follow the
// measured temperature and compares it with the metered heat.
// Targets (docs/planner_history_format.md, F1): 24 h error within ±0.3 K, daily heat
// within 10 %.
#include "house_model.h"
#include "planner.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace lune_planner;

static bool load(const char *path, std::vector<Record> *out) {
  FILE *f = std::fopen(path, "rb");
  if (!f) return false;
  uint8_t hb[HEADER_SIZE], rb[RECORD_SIZE];
  Header h;
  bool ok = std::fread(hb, 1, HEADER_SIZE, f) == HEADER_SIZE && read_header(hb, &h) && h.kind == Kind::HOUSE;
  while (ok && std::fread(rb, 1, RECORD_SIZE, f) == RECORD_SIZE) {
    Record r;
    read_record(h, rb, &r);
    out->push_back(r);
  }
  std::fclose(f);
  return ok && !out->empty();
}

static float g(const Record &r, uint8_t c) { return r.get(Kind::HOUSE, c); }
static float z(float v) { return std::isfinite(v) ? v : 0.0f; }

int main(int argc, char **argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s house.bin [--tau H] [--lambda L] [--warmup-days D]\n", argv[0]);
    return 2;
  }
  float tau = 3.0f, lambda = 0.997f;
  int warmup_days = 7;
  for (int i = 2; i + 1 < argc; i += 2) {
    if (!std::strcmp(argv[i], "--tau")) tau = std::atof(argv[i + 1]);
    else if (!std::strcmp(argv[i], "--lambda")) lambda = std::atof(argv[i + 1]);
    else if (!std::strcmp(argv[i], "--warmup-days")) warmup_days = std::atoi(argv[i + 1]);
  }
  std::vector<Record> h;
  if (!load(argv[1], &h)) {
    std::fprintf(stderr, "cannot read %s\n", argv[1]);
    return 1;
  }

  // Prior: Odin's learned values for this house as a starting point (any sane prior works).
  HouseParams prior;
  prior.ua_kw_per_k = 0.15f;
  prior.c_kwh_per_k = 23.0f;
  prior.tau_h = tau;
  HouseLearner learner(prior, lambda);

  const size_t n = h.size(), warm = static_cast<size_t>(warmup_days) * 24;
  std::vector<float> err24, maxerr;
  for (size_t i = 0; i + 1 < n; i++) {
    const float q = z(g(h[i], H_HEAT_SH_KWH));
    learner.push_heat(q);
    learner.learn(g(h[i], H_ROOM_C), g(h[i + 1], H_ROOM_C), g(h[i], H_OUTDOOR_C), g(h[i], H_SOLAR_WM2),
                  g(h[i], H_WIND_MS));
    if (i < warm || i + 25 >= n || !learner.params().valid) continue;
    // 24 h forecast from the end of hour i.
    const HouseParams p = learner.params();
    float t = g(h[i + 1], H_ROOM_C), qf = learner.q_floor();
    if (!std::isfinite(t)) continue;
    float worst = 0;
    bool ok = true;
    for (size_t k = 1; k <= 24; k++) {
      const Record &r = h[i + k];
      qf = floor_filter(qf, z(g(r, H_HEAT_SH_KWH)), p.tau_h);
      if (!std::isfinite(g(r, H_OUTDOOR_C))) { ok = false; break; }
      t = house_step(p, t, qf, g(r, H_OUTDOOR_C), z(g(r, H_SOLAR_WM2)), z(g(r, H_WIND_MS)));
      const float meas = g(h[i + k + 1 < n ? i + k + 1 : n - 1], H_ROOM_C);
      if (std::isfinite(meas)) worst = std::max(worst, std::fabs(t - meas));
      if (k == 24 && std::isfinite(meas)) err24.push_back(t - meas);
    }
    if (ok) maxerr.push_back(worst);
  }

  // Daily heat: what the final parameters say the house needed vs the metered heat.
  const HouseParams fin = learner.params();
  std::vector<float> day_err;
  for (size_t d0 = warm; d0 + 24 < n; d0 += 24) {
    double need = 0, meter = 0;
    bool ok = true;
    for (size_t i = d0; i < d0 + 24; i++) {
      const float t0 = g(h[i], H_ROOM_C), t1 = g(h[i + 1], H_ROOM_C), out = g(h[i], H_OUTDOOR_C);
      if (!std::isfinite(t0) || !std::isfinite(t1) || !std::isfinite(out)) { ok = false; break; }
      need += house_heat_needed(fin, t0, t1, out, z(g(h[i], H_SOLAR_WM2)), z(g(h[i], H_WIND_MS)));
      meter += z(g(h[i], H_HEAT_SH_KWH));
    }
    if (ok && meter > 5.0) day_err.push_back(static_cast<float>((need - meter) / meter));
  }

  auto rms = [](const std::vector<float> &v) {
    double s = 0;
    for (float x : v) s += x * x;
    return v.empty() ? NAN : std::sqrt(s / v.size());
  };
  auto share = [](const std::vector<float> &v, float lim) {
    size_t k = 0;
    for (float x : v) k += std::fabs(x) <= lim;
    return v.empty() ? NAN : 100.0 * k / v.size();
  };
  std::printf("house model: tau %.1f h, lambda %.4f, %u hours learned\n", tau, lambda, learner.samples());
  std::printf("  UA %.3f kW/K, C %.1f kWh/K (time constant %.0f h), sun %.4f kW per W/m2, wind +%.1f %%/m/s, internal %.2f kW\n",
              fin.ua_kw_per_k, fin.c_kwh_per_k, fin.c_kwh_per_k / fin.ua_kw_per_k, fin.g_sun_kw_per_wm2,
              100.0f * fin.k_wind_per_ms, fin.p_int_kw);
  std::printf("  24 h forecast: %zu windows, RMSE %.2f K, within 0.3 K %.0f %%; whole window within 0.3 K %.0f %%\n",
              err24.size(), rms(err24), share(err24, 0.3f), share(maxerr, 0.3f));
  std::vector<float> ad;
  for (float x : day_err) ad.push_back(std::fabs(x));
  std::sort(ad.begin(), ad.end());
  std::printf("  daily heat: %zu days, median |error| %.0f %%, within 10 %% on %.0f %% of days\n", day_err.size(),
              ad.empty() ? NAN : 100.0 * ad[ad.size() / 2], share(day_err, 0.10f));
  return 0;
}
