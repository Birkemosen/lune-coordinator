// Learns the heat pump and floor circuit from a recorded history (planner F1) and shows
// what the planner would conclude: flow temperature and COP per planned output.
//
//   eval_heat_pump house.bin
#include "heat_pump_model.h"
#include "planner.h"

#include <cstdio>
#include <vector>

using namespace lune_planner;

int main(int argc, char **argv) {
  if (argc < 2) { std::fprintf(stderr, "usage: %s house.bin\n", argv[0]); return 2; }
  FILE *f = std::fopen(argv[1], "rb");
  if (!f) return 1;
  uint8_t hb[HEADER_SIZE], rb[RECORD_SIZE];
  Header h;
  if (std::fread(hb, 1, HEADER_SIZE, f) != HEADER_SIZE || !read_header(hb, &h)) return 1;
  HeatPumpLearner l;
  size_t used = 0;
  double se = 0;
  while (std::fread(rb, 1, RECORD_SIZE, f) == RECORD_SIZE) {
    Record r;
    read_record(h, rb, &r);
    auto g = [&](uint8_t c) { return r.get(Kind::HOUSE, c); };
    const float run = g(H_RUN_MIN), dhw = g(H_HEAT_DHW_KWH);
    if (!(run >= 57) || (std::isfinite(dhw) && dhw >= 0.05f)) continue;
    const float kw = g(H_HEAT_SH_KWH), el = g(H_ELEC_KWH);
    // Score the prediction made before learning the hour.
    if (l.params().valid && kw > 0.5f && el > 0.2f && std::isfinite(g(H_HP_FEED_C))) {
      const float pred = cop_at(l.params(), g(H_HP_FEED_C), g(H_OUTDOOR_C));
      se += (pred - kw / el) * (pred - kw / el);
      used++;
    }
    l.learn(kw, el, g(H_HP_FEED_C), g(H_Z1_FEED_C), g(H_ROOM_C), g(H_OUTDOOR_C));
  }
  std::fclose(f);
  const HeatPumpParams &p = l.params();
  std::printf("heat pump: %u full space-heating hours learned\n", l.samples());
  std::printf("  floor  kW = %.2f + %.3f * (zone feed - room)\n", p.f0, p.f1);
  std::printf("  buffer HP feed - zone feed = %.2f + %.2f * kW\n", p.b0, p.b1);
  std::printf("  COP = %.2f %+.3f * HP feed %+.3f * outdoor   (next-hour RMSE %.2f over %zu hours)\n", p.c0, p.c1,
              p.c2, used ? std::sqrt(se / used) : NAN, used);
  std::printf("  at 22 C room, 5 C outdoor:\n    kW   zone feed  HP feed  COP   kWh el per kWh heat\n");
  for (float kw = 3.0f; kw <= 9.01f; kw += 1.0f) {
    const float zf = zone_feed_for(p, kw, 22.0f), hf = hp_feed_for(p, kw, 22.0f);
    std::printf("    %.0f    %5.1f     %5.1f   %.2f  %.3f\n", kw, zf, hf, cop_at(p, hf, 5.0f),
                elec_for(p, kw, 22.0f, 5.0f) / kw);
  }
  return 0;
}
