// =============================================================================
// Comfort signal — the house temperature Touch sends to the heat pump
// (pure C++, host-testable)
// =============================================================================
// Odin learns the house (heat loss, mass) from this value and brakes on it
// (2.0.1-03: room above the expected end-of-hour temperature -> minimum ΔT), so
// it must be honest: the UA-weighted mean of each room's deviation from its own
// target, surplus and deficit in full.
//
//   signal = house target + deviation
//
// It no longer carries the rooms' comfort needs. On 21 Oct a cold room pulled it
// 1.6 °C down (flow target to 42 °C) and damped surplus (30 %) hid a house 2 °C too
// warm. Instead:
//   - V6 moves the flow to a room below target and trims the warm ones;
//   - a room that still lags with its loop near fully open (V6 RAISE) lifts
//     Odin's comfort schedule (odin_comfort.h, house_demand.h) — the target, not
//     the measurement.
// The written value moves at most SLEW_C_PER_5MIN (slew()), so a change of source
// or a sensor coming back cannot step Odin.
// =============================================================================

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace lune_touch_comfort {

/// dev[i] = room temperature − room target (°C), w[i] = the room's weight (> 0).
/// Returns the weighted mean deviation to add to the house target; NAN without rooms.
inline float deviation(const float *dev, const float *w, size_t n) {
  float wsum = 0.0f, sum = 0.0f;
  for (size_t i = 0; i < n; i++) {
    if (!std::isfinite(dev[i]) || !(w[i] > 0.0f))
      continue;
    wsum += w[i];
    sum += w[i] * dev[i];
  }
  return wsum > 0.0f ? sum / wsum : NAN;
}

/// Largest change of the written signal per 5 minutes (°C).
static constexpr float SLEW_C_PER_5MIN = 0.1f;

/// Move `prev` toward `target` by at most SLEW_C_PER_5MIN per 5 minutes of
/// `elapsed_ms`. Without a previous value the target is taken as is.
inline float slew(float prev, float target, uint32_t elapsed_ms) {
  if (!std::isfinite(prev) || !std::isfinite(target))
    return target;
  const float max_step = SLEW_C_PER_5MIN * static_cast<float>(elapsed_ms) / 300000.0f;
  return prev + std::clamp(target - prev, -max_step, max_step);
}

}  // namespace lune_touch_comfort
