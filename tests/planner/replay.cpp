// Planner replay: runs a planner hour by hour over a recorded history file.
//
//   replay house.bin [rooms.bin]
//
// F0 runs the NullPlanner and reports what the history holds: span, gaps, column
// coverage and the metered totals a real planner will be measured against. Exit code
// 1 when the file cannot be read or the future leaks into the context.
#include "planner.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <vector>

using namespace lune_planner;

static bool load(const char *path, Header *h, std::vector<Record> *out) {
  FILE *f = std::fopen(path, "rb");
  if (!f) return false;
  uint8_t hb[HEADER_SIZE];
  bool ok = std::fread(hb, 1, HEADER_SIZE, f) == HEADER_SIZE && read_header(hb, h);
  uint8_t rb[RECORD_SIZE];
  while (ok && std::fread(rb, 1, RECORD_SIZE, f) == RECORD_SIZE) {
    Record r;
    read_record(*h, rb, &r);
    out->push_back(r);
  }
  std::fclose(f);
  return ok;
}

static const char *stamp(uint32_t t) {
  static char buf[32];
  const time_t tt = t;
  std::strftime(buf, sizeof buf, "%Y-%m-%d %H:00Z", std::gmtime(&tt));
  return buf;
}

static const char *HOUSE_NAMES[H_COL_COUNT] = {
    "heat_sh_kwh", "heat_dhw_kwh", "elec_kwh", "room_c", "outdoor_c", "wind_ms", "wind_dir_deg",
    "solar_wm2", "price_eur_kwh", "sched_sp_c", "sched_min_c", "sched_max_c", "run_min", "starts",
    "hp_feed_c", "hp_return_c", "z1_feed_c", "z1_return_c", "flow_target_c", "flow_target_max_c",
    "output_max_kw", "defrost_min", "dhw_tank_c", "mode_flags", "room_expected_c", "coldest_dev_k",
    "humidity_pct", "sun_s_wm2", "sun_w_wm2", "sun_e_wm2"};

int main(int argc, char **argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s house.bin [rooms.bin]\n", argv[0]);
    return 2;
  }
  Header h;
  std::vector<Record> hist;
  if (!load(argv[1], &h, &hist) || h.kind != Kind::HOUSE || hist.empty()) {
    std::fprintf(stderr, "cannot read house history %s\n", argv[1]);
    return 1;
  }

  // Span and gaps (missing whole hours).
  size_t gaps = 0, gap_hours = 0;
  for (size_t i = 1; i < hist.size(); i++) {
    const uint32_t step = hist[i].hour_start - hist[i - 1].hour_start;
    if (step != 3600) {
      gaps++;
      gap_hours += step / 3600 - 1;
    }
  }
  std::printf("house history: %zu hours, %s", hist.size(), stamp(hist.front().hour_start));
  std::printf(" .. %s, %zu gaps (%zu hours missing)\n", stamp(hist.back().hour_start), gaps, gap_hours);

  // Coverage per column.
  std::printf("coverage:\n");
  for (uint8_t c = 0; c < H_COL_COUNT; c++) {
    size_t have = 0;
    for (const auto &r : hist) have += r.has(c);
    std::printf("  %-18s %5.1f %%\n", HOUSE_NAMES[c], 100.0 * have / hist.size());
  }

  // Metered totals: what a planner's plan will be compared with.
  double heat = 0, dhw = 0, elec = 0, cost = 0;
  for (const auto &r : hist) {
    const float sh = r.get(Kind::HOUSE, H_HEAT_SH_KWH), hw = r.get(Kind::HOUSE, H_HEAT_DHW_KWH);
    const float el = r.get(Kind::HOUSE, H_ELEC_KWH), pr = r.get(Kind::HOUSE, H_PRICE_EUR_KWH);
    if (std::isfinite(sh)) heat += sh;
    if (std::isfinite(hw)) dhw += hw;
    if (std::isfinite(el)) {
      elec += el;
      if (std::isfinite(pr)) cost += el * pr;
    }
  }
  std::printf("metered: space heating %.0f kWh, hot water %.0f kWh, electricity %.0f kWh, cost %.2f EUR\n",
              heat, dhw, elec, cost);

  // Replay with the NullPlanner; check that no measured future value reaches it.
  NullPlanner planner;
  Context ctx;
  Plan plan;
  size_t leaks = 0, plans = 0;
  for (size_t now = 0; now + 1 < hist.size(); now++) {
    build_context(hist.data(), hist.size(), now, &ctx);
    for (uint8_t i = 0; i < ctx.n_ahead; i++)
      for (uint8_t c = 0; c < MAX_COLS; c++)
        if (!forecastable(c) && c != H_MODE_FLAGS && ctx.ahead[i].v[c] != NONE) leaks++;
    planner.plan(ctx, &plan);
    plans++;
  }
  std::printf("replay: %zu plans, %zu leaked future values\n", plans, leaks);

  if (argc > 2) {
    Header rh;
    std::vector<Record> rooms;
    if (!load(argv[2], &rh, &rooms) || rh.kind != Kind::ROOM) {
      std::fprintf(stderr, "cannot read room history %s\n", argv[2]);
      return 1;
    }
    int max_index = -1;
    for (const auto &r : rooms) {
      const float idx = r.get(Kind::ROOM, R_ROOM_INDEX);
      if (std::isfinite(idx) && idx > max_index) max_index = static_cast<int>(idx);
    }
    std::printf("room history: %zu room-hours, %d rooms, %s", rooms.size(), max_index + 1,
                stamp(rooms.front().hour_start));
    std::printf(" .. %s\n", stamp(rooms.back().hour_start));
  }
  return leaks == 0 ? 0 : 1;
}
