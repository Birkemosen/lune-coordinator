// =============================================================================
// House demand — demand-led Asgard comfort target (pure C++, host-testable)
// =============================================================================
// Asgard's virtual thermostat compares the weighted house temperature with the
// house target. In a house with unequal floors (a warm concrete ground floor
// and a cold, wind-exposed timber upper floor) the weighted average can reach
// target while the upper floor still lags — and the heat pump stops exactly when
// that floor needs it, or before its slab could be charged for a storm.
//
// The measured house temperature (temperature_feedback) is NOT altered: Odin
// learns house physics from it. Instead the comfort TARGET sent to the virtual
// thermostat is lifted just enough that it keeps calling while the most
// deficient room is behind (or a slab-charge window is open), bounded by
// `max_uplift_c` above the real house target. This is an honest demand signal:
// Odin plans the extra heat that is really needed. See
// docs/house_balancing_and_weather.md.
// =============================================================================

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace lune_touch_house {

struct DemandParams {
  /// Deficits within the comfort band do not lift the target.
  float band_c = 0.3f;
  /// Never lift the target more than this above the real house target.
  float max_uplift_c = 1.5f;
  /// Once lifting, keep going until the need falls below this (hysteresis).
  float release_c = 0.1f;
  /// …and for at least this long, so the heat source is not cycled.
  uint32_t min_hold_ms = 30UL * 60UL * 1000UL;
};

/// Required lift (°C) from the lagging room and any open slab-charge window.
inline float required_lift_c(float driver_deficit_c, float charge_store_c) {
  const float deficit = std::isfinite(driver_deficit_c) ? std::max(0.0f, driver_deficit_c) : 0.0f;
  const float charge = std::isfinite(charge_store_c) ? std::max(0.0f, charge_store_c) : 0.0f;
  return std::max(deficit, charge);
}

/// Hysteresis + minimum hold on the demand signal. Engages above `band_c`,
/// releases below `release_c` after `min_hold_ms`. While engaged it reports at
/// least the band, so a room that is almost caught up keeps the call alive
/// instead of toggling the heat source on and off around the threshold.
struct DemandHold {
  bool engaged = false;
  uint32_t since_ms = 0;

  float update(float required_c, uint32_t now_ms, const DemandParams &p = DemandParams{}) {
    const float req = std::isfinite(required_c) ? std::max(0.0f, required_c) : 0.0f;
    if (!engaged) {
      if (req > p.band_c) {
        engaged = true;
        since_ms = now_ms;
        return req;
      }
      return 0.0f;
    }
    const bool held_long_enough = static_cast<uint32_t>(now_ms - since_ms) >= p.min_hold_ms;
    if (req < p.release_c && held_long_enough) {
      engaged = false;
      return 0.0f;
    }
    return std::max(req, p.band_c + 0.01f);
  }
};

/// Target to send to the virtual thermostat.
/// house_target_c   real weighted house target
/// physical_c       measured weighted house temperature (NaN → no lift)
/// driver_deficit_c deficit of the room that lags most (target − temp, ≥0)
/// charge_store_c   largest planned slab charge among rooms charging now (°C)
inline float demand_led_target(float house_target_c, float physical_c, float driver_deficit_c,
                               float charge_store_c, const DemandParams &p = DemandParams{}) {
  if (!std::isfinite(house_target_c))
    return house_target_c;
  if (!std::isfinite(physical_c))
    return house_target_c;
  const float deficit = std::isfinite(driver_deficit_c) ? std::max(0.0f, driver_deficit_c) : 0.0f;
  const float charge = std::isfinite(charge_store_c) ? std::max(0.0f, charge_store_c) : 0.0f;
  const float required = std::max(deficit, charge);
  if (required <= p.band_c)
    return house_target_c;
  // Keep the target `required` above the measured house temperature so the
  // relay stays on while the critical room catches up / charges.
  const float lifted = std::max(house_target_c, physical_c + required);
  return std::min(lifted, house_target_c + p.max_uplift_c);
}

// ---------------------------------------------------------------------------
// Heat-source levers — one house demand, translated to what a source accepts.
// ---------------------------------------------------------------------------
// Most generic heat sources (boilers, many heat pumps without a room sensor)
// regulate on return temperature or a weather-compensation curve. They cannot
// use a room target. Touch therefore exposes the same house demand as:
//   target_c        for sources with a room-thermostat input (demand-led target)
//   heat_request    for simple on/off inputs (relay / OpenTherm CH enable)
//   curve_offset_c  for curve/return-temperature sources (parallel curve shift)

struct LeverParams {
  /// Flow-curve shift per °C of room deficit (°C flow / °C room).
  float curve_gain = 2.0f;
  /// Never shift the curve more than this (°C).
  float curve_max_offset_c = 5.0f;
  /// Heat request also when the house itself is this far below target.
  float request_below_c = 0.2f;
};

struct Levers {
  float target_c = NAN;
  bool heat_request = false;
  float curve_offset_c = 0.0f;
};

/// `held_required_c` is the DemandHold output (0 when no lift is engaged).
inline Levers generic_levers(float house_target_c, float physical_c, float held_required_c,
                             const LeverParams &lp = LeverParams{},
                             const DemandParams &dp = DemandParams{}) {
  Levers out{};
  out.target_c = demand_led_target(house_target_c, physical_c, held_required_c, 0.0f, dp);
  if (!std::isfinite(house_target_c) || !std::isfinite(physical_c))
    return out;
  const float house_gap = house_target_c - physical_c;
  const float need = std::max(std::max(0.0f, held_required_c), std::max(0.0f, house_gap));
  out.heat_request = held_required_c > 0.0f || house_gap > lp.request_below_c;
  out.curve_offset_c =
      std::round(std::min(lp.curve_max_offset_c, lp.curve_gain * need) * 10.0f) / 10.0f;
  return out;
}

}  // namespace lune_touch_house
