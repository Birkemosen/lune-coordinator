// Replays the two-mass model per room (planner F1).
//
//   eval_rooms house.bin rooms.bin [--window-days D] [--warmup-days D]
//
// Each room gets its share of the house's space heating (rooms.bin heat_kwh: valve ×
// loop area × (feed − room)) and the house's weather, and the same model and daily
// refit as the house. Reported per room: fitted loss and floor mass, and forecast RMSE
// at 3, 6 and 12 h next to persistence. "chosen" is what a live planner would use: each
// day, per room and horizon, the model or persistence, whichever was better over the
// previous 7 days.
#include "house_model.h"
#include "planner.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <vector>

using namespace lune_planner;

static bool load(const char *path, Kind kind, std::vector<Record> *out) {
  FILE *f = std::fopen(path, "rb");
  if (!f) return false;
  uint8_t hb[HEADER_SIZE], rb[RECORD_SIZE];
  Header h;
  bool ok = std::fread(hb, 1, HEADER_SIZE, f) == HEADER_SIZE && read_header(hb, &h) && h.kind == kind;
  while (ok && std::fread(rb, 1, RECORD_SIZE, f) == RECORD_SIZE) {
    Record r;
    read_record(h, rb, &r);
    out->push_back(r);
  }
  std::fclose(f);
  return ok && !out->empty();
}

static float z(float v) { return std::isfinite(v) ? v : 0.0f; }

// Loop areas of this install's zones (Touch /zones loop_area_m2), m². A real planner
// reads them from Touch; the replay keeps them here to stay free of personal data files.
// Exterior walls per zone (Touch forecast.exterior_walls: 1 N, 2 E, 4 S, 8 W).
static int room_walls(int idx) {
  static const int w[12] = {9, 3, 0, 4, 12, 8, 3, 9, 8, 12, 12, 6};
  return idx >= 0 && idx < 12 ? w[idx] : 0;
}

static float room_area(int idx) {
  static const float a[12] = {18, 12.5f, 28, 32, 37.5f, 14.5f, 21.5f, 21.5f, 6, 24.5f, 24.5f, 24.5f};
  return idx >= 0 && idx < 12 ? a[idx] : 20.0f;
}

int main(int argc, char **argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s house.bin rooms.bin [--window-days D] [--warmup-days D]\n", argv[0]);
    return 2;
  }
  int window_days = 14, warmup_days = 10;
  for (int i = 3; i + 1 < argc; i += 2) {
    if (!std::strcmp(argv[i], "--window-days")) window_days = std::atoi(argv[i + 1]);
    else if (!std::strcmp(argv[i], "--warmup-days")) warmup_days = std::atoi(argv[i + 1]);
  }
  std::vector<Record> house, rooms;
  if (!load(argv[1], Kind::HOUSE, &house) || !load(argv[2], Kind::ROOM, &rooms)) {
    std::fprintf(stderr, "cannot read history\n");
    return 1;
  }
  const size_t n = house.size();
  std::map<uint32_t, size_t> hour_index;
  for (size_t i = 0; i < n; i++) hour_index[house[i].hour_start] = i;
  std::vector<HourIn> base(n);
  for (size_t i = 0; i < n; i++) {
    const Record &r = house[i];
    base[i].out_c = r.get(Kind::HOUSE, H_OUTDOOR_C);
    if (!std::isfinite(base[i].out_c) && i) base[i].out_c = base[i - 1].out_c;
    base[i].sun_wm2 = z(r.get(Kind::HOUSE, H_SOLAR_WM2));
    base[i].wind_ms = z(r.get(Kind::HOUSE, H_WIND_MS));
    // nb_c (neighbours) stays NAN: the measured house average would leak the future
    // into a forecast; a live planner would take it from the house model.
    const int off = r.hour_start < 1792890000u ? 2 : 1;
    base[i].hour_local = static_cast<float>((r.hour_start / 3600 + off) % 24);
  }
  // Per room series aligned to the house hours.
  std::map<int, std::vector<float>> temp, heat;
  for (const auto &r : rooms) {
    const auto it = hour_index.find(r.hour_start);
    if (it == hour_index.end()) continue;
    const int idx = static_cast<int>(r.get(Kind::ROOM, R_ROOM_INDEX));
    auto &t = temp[idx];
    auto &q = heat[idx];
    if (t.empty()) { t.assign(n, NAN); q.assign(n, 0.0f); }
    t[it->second] = r.get(Kind::ROOM, R_TEMP_C);
    q[it->second] = z(r.get(Kind::ROOM, R_HEAT_KWH));
  }

  static HouseFitter fitter;
  const int HZ[] = {3, 6, 12};
  const size_t win = static_cast<size_t>(window_days) * 24;
  std::printf("room  UA W/K  floors kWh/K  rooms kWh/K  sun  late sun | RMSE model/persistence  3 h        6 h        12 h\n");
  double tse[3] = {}, tsp[3] = {}, tsc[3] = {};
  size_t tcnt[3] = {};
  for (auto &kv : temp) {
    const int idx = kv.first;
    const auto &t = kv.second;
    std::vector<HourIn> in = base;
    const int walls = room_walls(idx);
    for (size_t i = 0; i < n; i++) {
      in[i].q_kw = heat[idx][i];
      // The sun on the room's own facades; a room with none gets the global value.
      const Record &r = house[i];
      float sun = 0;
      if (walls & 4) sun += z(r.get(Kind::HOUSE, H_SUN_S_WM2));
      if (walls & 8) sun += z(r.get(Kind::HOUSE, H_SUN_W_WM2));
      if (walls & 2) sun += z(r.get(Kind::HOUSE, H_SUN_E_WM2));
      in[i].sun_wm2 = (walls & 14) ? sun : base[i].sun_wm2;
      // Upstairs lies under a mansard roof of black glazed tiles; adding the global sun for
      // its flat top did not improve any room (2 Oct-16 Nov), so the walls' sun is used and
      // the roof's delay is left to the late-sun term.
      in[i].sun_lag_wm2 = i ? sun_lag_filter(in[i - 1].sun_lag_wm2, in[i].sun_wm2) : 0.0f;
    }
    // The house's defaults scaled to the room's share of the floor area.
    const float size = room_area(idx) / 280.0f;
    HouseParams p;
    p.ua_kw_per_k *= size; p.ca_kwh_per_k *= size; p.cs_kwh_per_k *= size; p.h_kw_per_k *= size;
    p.g_kw_per_100wm2 *= size; p.p_int_kw *= size;
    static HouseFitter rf;
    rf = HouseFitter(p, 0.01f, size);
    double se[3] = {}, sp[3] = {}, sc[3] = {};
    size_t cnt[3] = {};
    std::vector<double> day_se[3], day_sp[3];
    for (size_t day = static_cast<size_t>(warmup_days) * 24; day + 25 < n; day += 24) {
      const size_t from = day > win ? day - win : 0;
      rf.fit(&t[from], &in[from], day - from, &p);
      std::vector<float> ts(day + 24 - from);
      track_floor(p, &t[from], &in[from], ts.size(), std::isfinite(t[from]) ? t[from] + 2.0f : 22.0f, ts.data());
      // Choose per horizon from the last 7 days' errors (before this day).
      bool use_model[3];
      for (int j = 0; j < 3; j++) {
        double a = 0, b = 0;
        for (size_t k = day_se[j].size() > 7 ? day_se[j].size() - 7 : 0; k < day_se[j].size(); k++) {
          a += day_se[j][k]; b += day_sp[j][k];
        }
        use_model[j] = day_se[j].empty() || a <= b;
      }
      double dse[3] = {}, dsp[3] = {};
      for (size_t s = day; s < day + 24 && s + 13 < n; s++) {
        if (!std::isfinite(t[s])) continue;
        const float t0 = t[s] - (s ? p.sensor_sun_k * in[s - 1].sun_wm2 / 100.0f : 0.0f);
        HouseState st{t0, ts[s - from]};
        for (int k = 1, j = 0; k <= 12; k++) {
          st = house_step(p, st, in[s + k - 1]);
          if (k == HZ[j]) {
            const float m = t[s + k];
            if (std::isfinite(m)) {
              const float shown = sensor_reading(p, st.t_c, in[s + k - 1]);
            const double em = (shown - m) * (shown - m), ep = (t[s] - m) * (t[s] - m);
            se[j] += em; sp[j] += ep; sc[j] += use_model[j] ? em : ep; dse[j] += em; dsp[j] += ep; cnt[j]++;
            }
            j++;
          }
        }
      }
      for (int j = 0; j < 3; j++) { day_se[j].push_back(dse[j]); day_sp[j].push_back(dsp[j]); }
    }
    std::printf("%4d  %6.1f  %12.1f  %11.1f  %4.2f %8.2f |", idx, 1000.0f * p.ua_kw_per_k, p.cs_kwh_per_k, p.ca_kwh_per_k,
                p.g_kw_per_100wm2, p.g_lag_kw_per_100wm2);
    for (int j = 0; j < 3; j++) {
      std::printf("  %.2f/%.2f", cnt[j] ? std::sqrt(se[j] / cnt[j]) : NAN, cnt[j] ? std::sqrt(sp[j] / cnt[j]) : NAN);
      tse[j] += se[j]; tsp[j] += sp[j]; tsc[j] += sc[j]; tcnt[j] += cnt[j];
    }
    std::printf("\n");
  }
  std::printf(" all                                                 |");
  for (int j = 0; j < 3; j++) std::printf("  %.2f/%.2f", std::sqrt(tse[j] / tcnt[j]), std::sqrt(tsp[j] / tcnt[j]));
  std::printf("\n chosen per room (model or persistence, by the last 7 days)  |");
  for (int j = 0; j < 3; j++) std::printf("  %.2f     ", std::sqrt(tsc[j] / tcnt[j]));
  std::printf("\n");
  (void)fitter;
  return 0;
}
