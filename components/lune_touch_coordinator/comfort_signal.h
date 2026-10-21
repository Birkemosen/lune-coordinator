// =============================================================================
// Comfort signal — the house temperature Touch sends to the heat pump
// (pure C++, host-testable)
// =============================================================================
// The weighted average of the rooms hides cold rooms behind warm ones: a warm
// ground floor (+1 °C) and a cold upper floor (−1 °C) average to "at target",
// and the heat pump never starts. A warm room cannot give heat to a cold one,
// and V6 already closes it, so its surplus says little about what the house
// needs. The signal therefore counts each room against its own target:
//
//   deficit counts fully, surplus at SURPLUS_WEIGHT (0.3)
//   a room more than TOLERANCE_C (0.5 °C) below its target pulls the house at
//   least (deficit − tolerance) below the house target, at most MAX_PULL_C (0.5)
//
// The heat pump's optimiser (Odin) reads the signal as the house temperature: a
// 1.6 °C step on 21 Oct sent its flow target to the 42 °C maximum. So one cold
// room pulls at most MAX_PULL_C, and the written value moves at most
// SLEW_C_PER_5MIN (slew()).
//
// Only rooms with at least MIN_SHARE of the weight can pull alone, so one small
// room with a wrong target cannot run the heat pump.
//
//   signal = house target + deviation
// =============================================================================

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace lune_touch_comfort {

struct Params {
  float surplus_weight = 0.3f;
  float tolerance_c = 0.5f;
  float min_share = 0.05f;
  float max_pull_c = 0.5f;
};

/// Largest change of the written signal per 5 minutes (°C).
static constexpr float SLEW_C_PER_5MIN = 0.1f;

/// dev[i] = room temperature − room target (°C), w[i] = the room's weight (> 0).
/// Returns the house deviation to add to the house target; NAN without rooms.
inline float deviation(const float *dev, const float *w, size_t n, const Params &p = Params{}) {
  float wsum = 0.0f, sum = 0.0f;
  for (size_t i = 0; i < n; i++) {
    if (!std::isfinite(dev[i]) || !(w[i] > 0.0f))
      continue;
    wsum += w[i];
    sum += w[i] * (dev[i] < 0.0f ? dev[i] : p.surplus_weight * dev[i]);
  }
  if (!(wsum > 0.0f))
    return NAN;
  float out = sum / wsum;
  for (size_t i = 0; i < n; i++) {
    if (!std::isfinite(dev[i]) || !(w[i] > 0.0f) || w[i] / wsum < p.min_share)
      continue;
    if (dev[i] < -p.tolerance_c)
      out = std::min(out, std::max(dev[i] + p.tolerance_c, -p.max_pull_c));
  }
  return out;
}

/// Move `prev` toward `target` by at most SLEW_C_PER_5MIN per 5 minutes of
/// `elapsed_ms`. Without a previous value the target is taken as is.
inline float slew(float prev, float target, uint32_t elapsed_ms) {
  if (!std::isfinite(prev) || !std::isfinite(target))
    return target;
  const float max_step = SLEW_C_PER_5MIN * static_cast<float>(elapsed_ms) / 300000.0f;
  return prev + std::clamp(target - prev, -max_step, max_step);
}

}  // namespace lune_touch_comfort
