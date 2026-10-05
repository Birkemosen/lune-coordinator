#include "flow_trim.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace esphome::lune_touch_coordinator::flow_trim;
using ::lune_touch::HeatRecommendation;

static int g_failures = 0;

static void expect(bool cond, const char *what) {
  if (cond)
    std::printf("PASS  %s\n", what);
  else {
    std::printf("FAIL  %s\n", what);
    g_failures++;
  }
}

static FreezeInputs all_clear() {
  FreezeInputs f;
  f.dhw = FreezeInputs::Tri::FALSE;
  f.legionella = FreezeInputs::Tri::FALSE;
  f.defrost = FreezeInputs::Tri::FALSE;
  f.odin_plan_available = true;
  f.odin_heat_off = false;
  f.house_quality_healthy = true;
  f.forecast_offset_on_critical = false;
  return f;
}

static void test_rate_limit_and_clamp() {
  Controller c;
  c.mode = Mode::ACTIVE;
  HouseDemand demand;
  demand.any_fresh = true;
  demand.contributing_rooms = 2;
  demand.recommendation = HeatRecommendation::RAISE;
  auto freeze = all_clear();

  auto r1 = c.step(demand, freeze, 1000);
  expect(r1.stepped && std::fabs(r1.target_bias_c - 0.1f) < 0.001f, "trim: first raise +0.1");
  auto r2 = c.step(demand, freeze, 1000 + 60 * 1000);  // 1 min later
  expect(!r2.stepped && std::fabs(c.bias_c - 0.1f) < 0.001f, "trim: rate-limited inside 30 min");
  auto r3 = c.step(demand, freeze, 1000 + STEP_INTERVAL_MS);
  expect(r3.stepped && std::fabs(c.bias_c - 0.2f) < 0.001f, "trim: steps after 30 min");

  c.bias_c = 0.95f;
  c.last_step_ms = 0;
  auto r4 = c.step(demand, freeze, 9000000);
  expect(r4.stepped && std::fabs(c.bias_c - 1.0f) < 0.001f, "trim: clamps at +1.0");
  c.last_step_ms = 0;
  auto r5 = c.step(demand, freeze, 9000000 + STEP_INTERVAL_MS);
  expect(!r5.stepped && std::fabs(c.bias_c - 1.0f) < 0.001f, "trim: no step at clamp");
}

static void test_aggregate_and_lower() {
  HeatRecommendation recs[] = {HeatRecommendation::HOLD, HeatRecommendation::RAISE,
                               HeatRecommendation::LOWER};
  bool fresh[] = {true, true, true};
  expect(Controller::aggregate_rooms(recs, fresh, 3) == HeatRecommendation::RAISE,
         "trim: any raise wins");
  HeatRecommendation all_lower[] = {HeatRecommendation::LOWER, HeatRecommendation::LOWER};
  bool fresh2[] = {true, true};
  expect(Controller::aggregate_rooms(all_lower, fresh2, 2) == HeatRecommendation::LOWER,
         "trim: all lower");
}

static void test_freeze_reasons() {
  Controller c;
  c.mode = Mode::ACTIVE;
  HouseDemand demand{HeatRecommendation::RAISE, true, 1};
  auto freeze = all_clear();
  freeze.dhw = FreezeInputs::Tri::TRUE;
  auto r = c.step(demand, freeze, 1000);
  expect(r.frozen && r.freeze_reason == FreezeReason::DHW && !r.stepped, "trim: freeze DHW");
  expect(std::fabs(c.bias_c) < 0.001f, "trim: DHW does not advance bias");

  freeze = all_clear();
  freeze.dhw = FreezeInputs::Tri::UNKNOWN;
  r = c.step(demand, freeze, 1000);
  expect(r.frozen && r.freeze_reason == FreezeReason::FREEZE_INPUT_UNKNOWN,
         "trim: unknown freeze input holds");

  freeze = all_clear();
  freeze.odin_heat_off = true;
  r = c.step(demand, freeze, 1000);
  expect(r.frozen && r.freeze_reason == FreezeReason::HEAT_OFF, "trim: Odin OFF freezes");

  freeze = all_clear();
  freeze.house_quality_healthy = false;
  r = c.step(demand, freeze, 1000);
  expect(r.frozen && r.freeze_reason == FreezeReason::DEGRADED_QUALITY, "trim: degraded quality");

  freeze = all_clear();
  freeze.forecast_offset_on_critical = true;
  r = c.step(demand, freeze, 1000);
  expect(r.frozen && r.freeze_reason == FreezeReason::FORECAST_CRITICAL,
         "trim: forecast on critical");
}

static void test_reseed_and_shadow() {
  Controller c;
  c.mode = Mode::SHADOW;
  c.reseed_from_readback(0.0f);
  expect(c.has_confirmed && std::fabs(c.bias_c) < 0.001f, "trim: reseed from 0");
  expect(!c.should_write_asgard(), "trim: shadow does not write");
  c.mode = Mode::ACTIVE;
  expect(c.should_write_asgard(), "trim: active writes");
  c.reseed_from_readback(2.5f);
  expect(std::fabs(c.bias_c - 1.0f) < 0.001f, "trim: reseed clamps oversized readback");
}

int main() {
  test_rate_limit_and_clamp();
  test_aggregate_and_lower();
  test_freeze_reasons();
  test_reseed_and_shadow();
  if (g_failures > 0) {
    std::printf("%d test(s) FAILED.\n", g_failures);
    return EXIT_FAILURE;
  }
  std::printf("All flow_trim tests passed.\n");
  return EXIT_SUCCESS;
}
