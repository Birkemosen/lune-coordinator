// Runs Touch's daily refit (hour_log.h) over a recorded history, as the device would see
// it day by day, starting from the seed (planner F3).
//
//   eval_learn house.bin
//
// Each line is one daily refit: what the energy balance and the hourly fit made of the
// days so far, and the heat pump from the full space-heating hours so far.
#include "hour_log.h"
#include "seed_params.h"

#include <cstdio>
#include <memory>
#include <vector>

using namespace lune_planner;

static int dk_hour(uint32_t ts) {
  const int off = ts < 1792890000u ? 2 : 1;   // Denmark: summer time until 25 Oct 2026 01:00 UTC
  return static_cast<int>((ts / 3600 + off) % 24);
}

int main(int argc, char **argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s house.bin\n", argv[0]);
    return 2;
  }
  FILE *f = std::fopen(argv[1], "rb");
  if (!f) return 1;
  uint8_t hb[HEADER_SIZE], rb[RECORD_SIZE];
  Header h;
  if (std::fread(hb, 1, HEADER_SIZE, f) != HEADER_SIZE || !read_header(hb, &h) || h.kind != Kind::HOUSE) return 1;
  std::vector<Record> recs;
  while (std::fread(rb, 1, RECORD_SIZE, f) == RECORD_SIZE) {
    Record r;
    read_record(h, rb, &r);
    recs.push_back(r);
  }
  std::fclose(f);
  if (recs.empty()) return 1;

  auto st = std::make_unique<HistoryStore<24 * 60>>();
  st->clear();
  const HouseParams seed = seed_house_params();
  const HeatPumpParams pseed = seed_heat_pump_params();
  auto w = std::make_unique<LearnScratch>(seed);
  std::printf("refit at          days  hrs  E D  rmse   UA     Ca    Cs    H     sun   P     wind%%  | hp h  f1    c0    COP(32,5)  floor-room\n");
  size_t k = 0;
  for (uint32_t now = recs.front().hour_start + 14 * 86400u; now <= recs.back().hour_start + 3600u; now += 86400u) {
    while (k < recs.size() && recs[k].hour_start < now) st->seed(recs[k++]);
    HouseParams hp;
    HeatPumpParams pp;
    LearnReport rep;
    learn_house(*st, now, seed, dk_hour, w.get(), &hp, &rep);
    learn_heat_pump(*st, now, pseed, &pp, &rep);
    const float fl = floor_now(*st, now, hp, dk_hour, w.get());
    const Record *last = st->at(now - 3600u);
    const float room = last ? last->get(Kind::HOUSE, H_ROOM_C) : NAN;
    const time_t tt = now;
    char when[20];
    std::strftime(when, sizeof(when), "%Y-%m-%d %H:%M", std::gmtime(&tt));
    std::printf("%s  %4u  %3u  %d %d  %.3f  %.3f  %4.1f  %4.1f  %.2f  %.2f  %.2f  %4.1f   | %4u  %.3f  %.2f  %.2f       %+.2f\n", when,
                rep.days, rep.hours, rep.energy, rep.dynamics, rep.rmse_k, hp.ua_kw_per_k, hp.ca_kwh_per_k, hp.cs_kwh_per_k,
                hp.h_kw_per_k, hp.g_kw_per_100wm2, hp.p_int_kw, 100 * hp.kw_per_ms, rep.hp_hours, pp.f1, pp.c0,
                cop_at(pp, 32.0f, 5.0f), fl - room);
  }
  std::printf("seed                                  %.3f  %4.1f  %4.1f  %.2f  %.2f  %.2f  %4.1f   |       %.3f  %.2f  %.2f\n",
              seed.ua_kw_per_k, seed.ca_kwh_per_k, seed.cs_kwh_per_k, seed.h_kw_per_k, seed.g_kw_per_100wm2, seed.p_int_kw,
              100 * seed.kw_per_ms, pseed.f1, pseed.c0, cop_at(pseed, 32.0f, 5.0f));
  return 0;
}
