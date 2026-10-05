#include "odin_plan.h"

#include <cassert>
#include <cmath>
#include <cstring>
#include <cstdio>

using namespace esphome::lune_touch_coordinator::odin_plan;

static void fill_valid_horizon(Snapshot *plan) {
  for (size_t i = 0; i < HORIZON_HOURS; ++i) {
    plan->sched_min[i] = 20.0f;
    plan->sched_base[i] = 21.0f;
    plan->sched_max[i] = 22.5f;
    plan->expected_begin_temp[i] = 21.0f;
    plan->expected_end_temp[i] = 21.0f;
    plan->prices[i] = 0.27f;
    plan->heat_production[i] = 0.0f;
    plan->actual_prod[i] = NAN;
    plan->operation_mode[i] = 0;
  }
}

int main() {
  assert(to_operation_mode(1) == OperationMode::DHW_ON);
  assert(to_operation_mode(2) == OperationMode::HEAT_ON);
  assert(to_operation_mode(3) == OperationMode::COOL_ON);
  // Odin 2.0: 5 = frost protect (space heat), 6 = legionella (DHW cycle).
  assert(to_operation_mode(5) == OperationMode::HEAT_ON);
  assert(to_operation_mode(6) == OperationMode::DHW_ON);
  assert(!may_arm_absorb(2.0f, 6));
  assert(to_operation_mode(255) == OperationMode::UNAVAILABLE);
  assert(to_operation_mode(4) == OperationMode::OFF);  // undocumented code → off
  assert(std::strcmp(operation_mode_name(to_operation_mode(1)), "DHW on") == 0);
  assert(!may_drive_room_control(to_operation_mode(1)));
  assert(!may_drive_room_control(to_operation_mode(2)));
  assert(!may_drive_room_control(to_operation_mode(3)));
  assert(!may_drive_room_control(to_operation_mode(255)));
  assert(!defrost_state_is_known());

  assert(may_arm_absorb(4.8f, 2));
  assert(!may_arm_absorb(4.8f, 1));   // DHW — never arm
  assert(!may_arm_absorb(0.0f, 2));
  assert(!may_arm_absorb(NAN, 2));

  uint8_t idx = 255;
  assert(resolve_idx_now(24, 13, &idx) && idx == 37);
  assert(resolve_idx_now(12, 5, &idx) && idx == 17);  // shorter history after reboot
  assert(!resolve_idx_now(70, 5, &idx));              // would exceed 72
  assert(!resolve_idx_now(24, 24, &idx));

  Snapshot plan{};
  plan.success = true;
  plan.current_hour = 13;
  plan.today_start_index = 24;
  plan.current_index = 37;
  fill_valid_horizon(&plan);
  assert(validate(plan));
  assert(current_target_c(plan) == 21.0f);

  // Fixture: today_start_index != 24 (shorter retained history).
  plan.today_start_index = 12;
  plan.current_hour = 5;
  plan.current_index = 17;
  assert(validate(plan));

  plan.sched_max[7] = 20.5f;
  assert(!validate(plan));
  assert(!std::isfinite(current_target_c(plan)));
  plan.sched_max[7] = 22.5f;
  plan.expected_end_temp[7] = NAN;
  assert(!validate(plan));
  plan.expected_end_temp[7] = 21.0f;
  plan.current_index = HORIZON_HOURS;
  assert(!validate(plan));

  plan.current_index = 17;
  plan.operation_mode[17] = 0;
  assert(current_hour_is_heat_off(plan));
  plan.operation_mode[17] = 2;
  assert(!current_hour_is_heat_off(plan));
  plan.operation_mode[17] = 1;
  assert(!current_hour_is_heat_off(plan));

  {
    // Odin 2.0 layout (fixtures/odin2_dashboard_odin_2.0.0-23.json): today
    // starts at 24, past slots may be null (NaN); only now→end must be complete.
    Snapshot p{};
    p.success = true;
    p.today_start_index = ODIN2_TODAY_START_INDEX;
    p.current_hour = 14;
    p.current_index = 38;
    for (size_t i = 0; i < HORIZON_HOURS; i++) {
      const bool past_gap = i < 38 && (i % 5 == 0);
      p.sched_min[i] = past_gap ? NAN : 21.5f;
      p.sched_base[i] = past_gap ? NAN : 22.0f;
      p.sched_max[i] = past_gap ? NAN : 23.5f;
      p.expected_begin_temp[i] = past_gap ? NAN : 23.8f;
      p.expected_end_temp[i] = NAN;  // Odin 2.0 has none; not checked
      p.prices[i] = 0.33f;
      p.heat_production[i] = past_gap ? NAN : 0.0f;
    }
    assert(validate_future(p));
    assert(!validate(p));  // the strict v1 rule would reject the gaps
    p.heat_production[50] = NAN;
    assert(!validate_future(p));  // a gap in the future is not a usable plan
    p.heat_production[50] = 0.0f;
    p.current_index = 37;
    assert(!validate_future(p));  // index must match today_start + hour
  }

  std::puts("ODIN plan validation tests passed.");
}
