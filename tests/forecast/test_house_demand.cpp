// Host tests for house_demand.h (demand-led Asgard comfort target).
#include "house_demand.h"

#include <cassert>
#include <cmath>
#include <cstdio>

using lune_touch_house::demand_led_target;

static bool near(float a, float b) { return std::fabs(a - b) < 0.001f; }

int main() {
  // No room behind and nothing charging → the real target, unchanged.
  assert(near(demand_led_target(22.0f, 22.1f, 0.0f, 0.0f), 22.0f));
  // A deficit inside the comfort band does not lift.
  assert(near(demand_led_target(22.0f, 22.1f, 0.25f, 0.0f), 22.0f));

  // Warm ground floor pulls the weighted average to 22.4 (above target), but the
  // upper floor lags 1.0 °C → target lifted to keep the heat pump calling.
  const float t = demand_led_target(22.0f, 22.4f, 1.0f, 0.0f);
  assert(near(t, 23.4f));

  // Lift is capped at max_uplift above the real target.
  assert(near(demand_led_target(22.0f, 22.8f, 2.0f, 0.0f), 23.5f));

  // A slab-charge window lifts even when no room is behind yet.
  assert(near(demand_led_target(22.0f, 21.9f, 0.0f, 1.0f), 22.9f));

  // Unknown house temperature → never lift (no blind demand).
  assert(near(demand_led_target(22.0f, NAN, 1.0f, 1.0f), 22.0f));

  // Hysteresis + minimum hold.
  {
    using lune_touch_house::DemandHold;
    using lune_touch_house::DemandParams;
    DemandParams p{};
    DemandHold hold{};
    const uint32_t min = 60UL * 1000UL;
    assert(hold.update(0.2f, 0, p) == 0.0f);          // inside band → idle
    assert(near(hold.update(0.5f, 1 * min, p), 0.5f)); // engages
    // Room nearly caught up: still above release → keeps calling at ≥ band.
    assert(hold.update(0.15f, 5 * min, p) > p.band_c);
    // Below release but held < 30 min → still on.
    assert(hold.update(0.0f, 10 * min, p) > p.band_c);
    // After the hold → releases.
    assert(hold.update(0.0f, 40 * min, p) == 0.0f);
    assert(!hold.engaged);
    // Held demand lifts the target (fed back as the deficit).
    assert(demand_led_target(22.0f, 22.0f, hold.update(0.6f, 50 * min, p), 0.0f) > 22.0f);
    assert(near(lune_touch_house::required_lift_c(0.4f, 1.0f), 1.0f));
    assert(lune_touch_house::required_lift_c(NAN, -1.0f) == 0.0f);
  }

  // Generic levers: one demand, three forms.
  {
    using namespace lune_touch_house;
    // Average at target, upper floor 1 °C behind (held) → request + curve shift.
    Levers l = generic_levers(22.0f, 22.0f, 1.0f);
    assert(l.heat_request && near(l.curve_offset_c, 2.0f) && near(l.target_c, 23.0f));
    // Satisfied house, nothing held → no request, no shift, real target.
    l = generic_levers(22.0f, 22.1f, 0.0f);
    assert(!l.heat_request && l.curve_offset_c == 0.0f && near(l.target_c, 22.0f));
    // House itself 0.5 °C low → request and shift even without a lagging room.
    l = generic_levers(22.0f, 21.5f, 0.0f);
    assert(l.heat_request && near(l.curve_offset_c, 1.0f));
    // Capped shift.
    assert(near(generic_levers(22.0f, 19.0f, 0.0f).curve_offset_c, 5.0f));
    // Unknown temperature → no blind demand.
    l = generic_levers(22.0f, NAN, 1.0f);
    assert(!l.heat_request && l.curve_offset_c == 0.0f);
  }

  std::puts("house_demand: all assertions passed");
  return 0;
}
