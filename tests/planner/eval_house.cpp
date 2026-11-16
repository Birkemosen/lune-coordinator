// Replays the house model over a recorded history and scores it (planner F1).
//
//   eval_house house.bin [--window-days D] [--warmup-days D]
//
// Once a day the model is refitted on the last `window` days it has seen. From every
// hour of the following day it forecasts the house temperature with those parameters,
// the measured space heating and the observed weather, and the forecast is compared
// with what happened, by horizon, next to persistence (temperature stays as it is).
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
    std::fprintf(stderr, "usage: %s house.bin [--window-days D] [--warmup-days D]\n", argv[0]);
    return 2;
  }
  int window_days = 14, warmup_days = 10;
  for (int i = 2; i + 1 < argc; i += 2) {
    if (!std::strcmp(argv[i], "--window-days")) window_days = std::atoi(argv[i + 1]);
    else if (!std::strcmp(argv[i], "--warmup-days")) warmup_days = std::atoi(argv[i + 1]);
  }
  std::vector<Record> h;
  if (!load(argv[1], &h)) {
    std::fprintf(stderr, "cannot read %s\n", argv[1]);
    return 1;
  }
  const size_t n = h.size();
  std::vector<float> t(n);
  std::vector<HourIn> in(n);
  for (size_t i = 0; i < n; i++) {
    t[i] = g(h[i], H_ROOM_C);
    in[i].q_kw = z(g(h[i], H_HEAT_SH_KWH));
    in[i].out_c = g(h[i], H_OUTDOOR_C);
    in[i].sun_wm2 = z(g(h[i], H_SOLAR_WM2));
    in[i].wind_ms = z(g(h[i], H_WIND_MS));
    // Local hour (Denmark: UTC+1, +2 in summer time; the replay span starts in summer time).
    const uint32_t ts = h[i].hour_start;
    const int off = ts < 1792890000u ? 2 : 1;   // 25 Oct 2026 01:00 UTC
    in[i].hour_local = static_cast<float>((ts / 3600 + off) % 24);
  }
  // Outdoor gaps: carry the last value so the simulation can run.
  for (size_t i = 1; i < n; i++)
    if (!std::isfinite(in[i].out_c)) in[i].out_c = in[i - 1].out_c;

  static HouseFitter fitter;
  HouseParams p;   // defaults are the prior
  const int HZ[] = {1, 3, 6, 12, 24};
  double se[5] = {}, sp[5] = {};
  size_t cnt[5] = {};
  size_t fits = 0;
  const size_t win = static_cast<size_t>(window_days) * 24;
  for (size_t day = static_cast<size_t>(warmup_days) * 24; day + 25 < n; day += 24) {
    const size_t from = day > win ? day - win : 0;
    fitter.fit(&t[from], &in[from], day - from, &p);
    fits++;
    // Floor track up to each forecast start, using the window's start as the anchor.
    std::vector<float> ts(day + 24 - from);
    track_floor(p, &t[from], &in[from], ts.size(), t[from] + 2.0f, ts.data());
    for (size_t s = day; s < day + 24 && s + 25 < n; s++) {
      if (!std::isfinite(t[s])) continue;
      HouseState st{t[s], ts[s - from]};
      for (int k = 1, j = 0; k <= 24; k++) {
        st = house_step(p, st, in[s + k - 1]);
        if (k == HZ[j]) {
          const float m = t[s + k];
          if (std::isfinite(m)) {
            se[j] += (st.t_c - m) * (st.t_c - m);
            sp[j] += (t[s] - m) * (t[s] - m);
            cnt[j]++;
          }
          j++;
        }
      }
    }
  }
  std::printf("house model (2 masses): %zu daily fits on %d-day windows; last fit:\n", fits, window_days);
  std::printf("  UA %.3f kW/K, rooms %.1f kWh/K, floors %.1f kWh/K, floor->rooms %.2f kW/K,\n"
              "  sun %.2f kW per 100 W/m2, internal %.2f kW (daily %+.2f/%+.2f), wind +%.1f %%/m/s, fit RMSE 1-24 h %.2f K\n",
              p.ua_kw_per_k, p.ca_kwh_per_k, p.cs_kwh_per_k, p.h_kw_per_k, p.g_kw_per_100wm2, p.p_int_kw, p.p_cos_kw, p.p_sin_kw,
              100.0f * p.kw_per_ms, fitter.rmse());
  std::printf("  forecast RMSE by horizon (model / persistence):\n");
  for (int j = 0; j < 5; j++)
    std::printf("    h+%-2d %.2f / %.2f K  (%zu)\n", HZ[j], cnt[j] ? std::sqrt(se[j] / cnt[j]) : NAN,
                cnt[j] ? std::sqrt(sp[j] / cnt[j]) : NAN, cnt[j]);
  return 0;
}
