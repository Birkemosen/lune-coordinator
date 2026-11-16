// Replays Touch's room expectation (room_forecast.h) as the device runs it: 7 days per
// room, global sun only, a daily refit, and ahead each room gets the house's heat by its
// share over the last 72 h (the replay uses the heat the house actually got as the plan).
//
//   eval_room_forecast house.bin rooms.bin
//
// Per room: forecast RMSE at 3, 6, 12 and 24 h for the shown forecast (model or "stays as
// it is", blended by room_forecast.h) next to persistence, and how often the model had
// more than half the weight at 6 h.
#include "history_record.h"
#include "room_forecast.h"

#include <cstdio>
#include <map>
#include <memory>
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

static float room_area(int idx) {   // this install's loop areas (see eval_rooms.cpp)
  static const float a[12] = {18, 12.5f, 28, 32, 37.5f, 14.5f, 21.5f, 21.5f, 6, 24.5f, 24.5f, 24.5f};
  return idx >= 0 && idx < 12 ? a[idx] : 20.0f;
}

int main(int argc, char **argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: %s house.bin rooms.bin\n", argv[0]);
    return 2;
  }
  std::vector<Record> house, rooms;
  if (!load(argv[1], Kind::HOUSE, &house) || !load(argv[2], Kind::ROOM, &rooms)) return 1;
  const size_t n = house.size();
  std::map<uint32_t, size_t> at;
  for (size_t i = 0; i < n; i++) at[house[i].hour_start] = i;
  std::vector<HourIn> base(n);
  float lag = 0;
  for (size_t i = 0; i < n; i++) {
    const Record &r = house[i];
    base[i].out_c = r.get(Kind::HOUSE, H_OUTDOOR_C);
    if (!std::isfinite(base[i].out_c) && i) base[i].out_c = base[i - 1].out_c;
    base[i].sun_wm2 = z(r.get(Kind::HOUSE, H_SOLAR_WM2));
    base[i].wind_ms = z(r.get(Kind::HOUSE, H_WIND_MS));
    base[i].q_kw = z(r.get(Kind::HOUSE, H_HEAT_SH_KWH));
    lag = sun_lag_filter(lag, base[i].sun_wm2);
    base[i].sun_lag_wm2 = lag;
    const int off = r.hour_start < 1792890000u ? 2 : 1;
    base[i].hour_local = static_cast<float>((r.hour_start / 3600 + off) % 24);
  }
  std::map<int, std::vector<float>> temp, heat;
  for (const auto &r : rooms) {
    const auto it = at.find(r.hour_start);
    if (it == at.end()) continue;
    const int idx = static_cast<int>(r.get(Kind::ROOM, R_ROOM_INDEX));
    if (temp[idx].empty()) { temp[idx].assign(n, NAN); heat[idx].assign(n, 0.0f); }
    temp[idx][it->second] = r.get(Kind::ROOM, R_TEMP_C);
    heat[idx][it->second] = z(r.get(Kind::ROOM, R_HEAT_KWH));
  }
  float total_area = 0;
  for (auto &kv : temp) total_area += room_area(kv.first);

  auto fitter = std::make_unique<HouseFitter>();
  const size_t W = ROOM_WINDOW;
  const int HZ[] = {3, 6, 12, 24};
  std::vector<float> track(W + 1), tw(W + 1), hq(W), out(24);
  std::vector<HourIn> win(W), fut(24);
  double ts[4] = {}, tp[4] = {};
  size_t tc[4] = {};
  std::printf("room  share | RMSE shown/persistence  3 h        6 h        12 h       24 h     | model used\n");
  for (auto &kv : temp) {
    const int idx = kv.first;
    const auto &t = kv.second;
    RoomModel m;
    double se[4] = {}, sp[4] = {};
    size_t cnt[4] = {}, used = 0, shown = 0;
    float share = 0;
    for (size_t now = W + 24; now + 25 < n; now++) {
      const size_t from = now - W;
      for (size_t i = 0; i <= W; i++) tw[i] = t[from + i];
      for (size_t i = 0; i < W; i++) { win[i] = base[from + i]; win[i].q_kw = heat[idx][from + i]; }
      if (base[now].hour_local == 3) {   // daily: score yesterday's model, then refit
        for (size_t i = 0; i < W; i++) hq[i] = base[from + i].q_kw;
        score_room(&m, tw.data(), win.data(), hq.data(), share, W, W - 24, track.data());
        fit_room(&m, fitter.get(), tw.data(), win.data(), W, room_area(idx) / total_area);
        double qr = 0, qh = 0;
        for (size_t i = now - 72; i < now; i++) { qr += heat[idx][i]; qh += base[i].q_kw; }
        share = qh > 0.5 ? static_cast<float>(qr / qh) : share;
      }
      if (!m.fitted || !std::isfinite(t[now])) continue;
      for (int k = 0; k < 24; k++) { fut[k] = base[now + k]; fut[k].q_kw = base[now + k].q_kw * share; }
      const bool model = forecast_room(m, tw.data(), win.data(), W, fut.data(), 24, track.data(), out.data());
      used += model && m.weight(6) > 0.5f; shown++;
      for (int j = 0; j < 4; j++) {
        const float meas = t[now + HZ[j]];
        if (!std::isfinite(meas)) continue;
        se[j] += (out[HZ[j] - 1] - meas) * (out[HZ[j] - 1] - meas);
        sp[j] += (t[now] - meas) * (t[now] - meas);
        cnt[j]++;
      }
    }
    std::printf("%4d  %5.3f |", idx, share);
    for (int j = 0; j < 4; j++) {
      std::printf("  %.2f/%.2f", cnt[j] ? std::sqrt(se[j] / cnt[j]) : NAN, cnt[j] ? std::sqrt(sp[j] / cnt[j]) : NAN);
      ts[j] += se[j]; tp[j] += sp[j]; tc[j] += cnt[j];
    }
    std::printf("  | %3.0f %%\n", shown ? 100.0 * used / shown : 0.0);
  }
  std::printf(" all        |");
  for (int j = 0; j < 4; j++) std::printf("  %.2f/%.2f", std::sqrt(ts[j] / tc[j]), std::sqrt(tp[j] / tc[j]));
  std::printf("\n");
  return 0;
}
