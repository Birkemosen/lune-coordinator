// Host tests for planner/planner.h: what a planner may see of the future.
#include "planner.h"

#include <cassert>
#include <cstdio>
#include <vector>

using namespace lune_planner;

int main() {
  // 60 measured hours with every column filled.
  std::vector<Record> hist(60);
  for (size_t i = 0; i < hist.size(); i++) {
    hist[i].hour_start = 1794787200u + static_cast<uint32_t>(i) * 3600u;
    for (uint8_t c = 0; c < H_COL_COUNT; c++) hist[i].v[c] = static_cast<int16_t>(100 + c);
  }

  Context ctx;
  build_context(hist.data(), hist.size(), 5, &ctx);
  assert(ctx.n_past == 6 && ctx.past[5].hour_start == hist[5].hour_start);
  assert(ctx.n_ahead == HORIZON_H);
  assert(ctx.ahead[0].hour_start == hist[6].hour_start);

  // Forecastable columns pass, measured ones are hidden.
  const Record &next = ctx.ahead[0];
  assert(next.v[H_OUTDOOR_C] == hist[6].v[H_OUTDOOR_C]);
  assert(next.v[H_PRICE_EUR_KWH] == hist[6].v[H_PRICE_EUR_KWH]);
  assert(next.v[H_SCHED_MIN_C] == hist[6].v[H_SCHED_MIN_C]);
  assert(next.v[H_HEAT_SH_KWH] == NONE);
  assert(next.v[H_ROOM_C] == NONE);
  assert(next.v[H_FLOW_TARGET_C] == NONE);
  assert(next.v[H_ROOM_EXPECTED_C] == NONE);
  assert(next.v[H_MODE_FLAGS] == MODE_FORECAST_WEATHER);

  // Near the end of the history the horizon shrinks.
  build_context(hist.data(), hist.size(), 50, &ctx);
  assert(ctx.n_ahead == 9);

  NullPlanner p;
  Plan plan;
  p.plan(ctx, &plan);
  assert(plan.n == 9 && plan.from_hour == hist[51].hour_start);

  std::printf("planner context: all assertions passed\n");
  return 0;
}
