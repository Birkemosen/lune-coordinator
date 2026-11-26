// Host tests for lune_touch_coordinator/planner_settings.h (GET /planner body, settings).
#include "planner_settings.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace esphome::lune_touch_coordinator::planner_settings;

int main() {
  // --- strategy names round-trip --------------------------------------------------
  Strategy s;
  assert(parse_strategy("comfort", &s) && s == Strategy::COMFORT);
  assert(parse_strategy("weighted", &s) && s == Strategy::WEIGHTED);
  assert(parse_strategy("price", &s) && s == Strategy::PRICE);
  assert(!parse_strategy("cheap", &s) && !parse_strategy(nullptr, &s));
  assert(std::strcmp(strategy_name(Strategy::WEIGHTED), "weighted") == 0);

  // --- sanitize keeps every field in range ------------------------------------------
  Settings x;
  x.comfort_weight_eur = 9.0f;
  x.min_output_kw = 6.0f;
  x.max_output_kw = 4.5f;   // below min: min follows
  x.max_flow_c = NAN;
  const Settings c = sanitize(x);
  assert(c.comfort_weight_eur == 5.0f);
  assert(c.max_output_kw == 4.5f && c.min_output_kw == 4.5f);
  assert(c.max_flow_c == 35.0f);
  Settings bad;
  bad.strategy = static_cast<Strategy>(7);
  assert(sanitize(bad).strategy == Strategy::COMFORT);

  // --- the API body: shadow mode, settings, null results ------------------------------
  char buf[512];
  write_api_json(Settings{}, buf, sizeof(buf));
  assert(std::strstr(buf, "\"mode\":\"shadow\"") != nullptr);
  assert(std::strstr(buf, "\"running\":false") != nullptr);
  assert(std::strstr(buf, "\"strategy\":\"comfort\"") != nullptr);
  assert(std::strstr(buf, "\"max_output_kw\":8.0") != nullptr);
  assert(std::strstr(buf, "\"plan\":null") != nullptr && std::strstr(buf, "\"history\":null") != nullptr);

  std::printf("planner settings: all assertions passed\n");
  return 0;
}
