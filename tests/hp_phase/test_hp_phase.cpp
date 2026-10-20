// Host tests for hp_phase.h (valves follow the heat pump).
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "hp_phase.h"

using namespace lune_touch_hp_phase;

int main() {
  State s{};
  Input in{};
  // No fresh heat pump data: say nothing.
  assert(step(s, in, 1000) == Phase::Unknown);

  in.telemetry_fresh = true;
  in.operation_mode = 2;
  in.compressor_on = true;
  assert(step(s, in, 1000) == Phase::Running);
  // Compressor stops (defrost / pause): still running for 5 minutes.
  in.compressor_on = false;
  in.water_pump_on = false;
  assert(step(s, in, 1000 + 4 * 60 * 1000) == Phase::Running);
  assert(step(s, in, 1000 + 6 * 60 * 1000) == Phase::Idle);
  // Hot water with the compressor on is idle for the house loops.
  in.operation_mode = 1;
  in.compressor_on = true;
  State s2{};
  assert(step(s2, in, 1000) == Phase::Idle);

  // Odin plans heat this hour: prestart (keep the loops open between cycles).
  in.compressor_on = false;
  in.plan_known = true;
  in.plan_heat_now_kw = 4.2f;
  in.plan_mode_now = 2;
  assert(step(s2, in, 2000) == Phase::Prestart);
  // DHW planned this hour is not space heat.
  in.plan_mode_now = 1;
  assert(step(s2, in, 2000) == Phase::Idle);
  // Next hour space heat: prestart only in the last 10 minutes.
  in.plan_heat_now_kw = 0.0f;
  in.plan_heat_next_kw = 3.0f;
  in.plan_mode_next = 2;
  in.minute = 45;
  assert(step(s2, in, 3000) == Phase::Idle);
  in.minute = 52;
  assert(step(s2, in, 3000) == Phase::Prestart);
  in.plan_mode_next = 5;   // frost protect is space heat too
  assert(step(s2, in, 3000) == Phase::Prestart);
  in.plan_known = false;
  assert(step(s2, in, 3000) == Phase::Idle);

  assert(std::strcmp(name(Phase::Prestart), "prestart") == 0);
  std::puts("hp_phase: ok");
  return 0;
}
