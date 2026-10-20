// =============================================================================
// Solar hold-off — lower a sunny room's target before the sun arrives
// (pure C++, host-testable)
// =============================================================================
// Rooms with large glazing (upper-floor roof windows, a big west window) warm
// 3–6 K on a sunny afternoon without any heat. If the floor was charged that
// morning, the sun lands on top of it and the room overshoots by 2 K or more.
// Odin regulates the weighted house temperature and cannot keep one room's
// floor cool; this is per room, so Touch sends a negative setpoint offset to
// the rooms that will get sun, a few hours ahead, and V6 throttles them.
//
// The offset scales with the room's solar factor (Touch room setting, 0..1),
// the forecast irradiance and how close the sun is. Light floors (timber,
// laminate) store little, so a short lead is enough. A room that is already
// cold is left alone: a cloudy forecast miss must not leave it cold too.
// Wind/cold preload and slab charge win over this (the caller decides).
// =============================================================================

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace lune_touch_solar {

struct Params {
  float start_wm2 = 250.0f;   // irradiance (GHI) where hold-off begins
  float full_wm2 = 600.0f;    // irradiance for the full hold-off
  float max_offset_c = 1.5f;  // offset at solar factor 1.0, full sun, now
  uint8_t lead_h = 3;         // how far ahead the sun counts
  // Rooms at or below the default solar factor (0.3) get no hold-off: it acts only
  // where someone has said the room gets real sun.
  float min_solar_gain = 0.35f;
  float cold_margin_c = 0.3f;   // room this far below target: no hold-off
};

struct Decision {
  float offset_c = 0.0f;   // <= 0; 0 = no hold-off
  float peak_wm2 = 0.0f;   // strongest irradiance in the window
  int8_t peak_in_h = -1;   // hours until that peak (0 = this hour)
  bool active = false;
};

/// ghi[i]: forecast global horizontal irradiance (W/m²) for hour i; now = index of
/// the current hour. temp_c may be NAN (unknown), then the cold check is skipped.
inline Decision plan(const float *ghi, size_t count, size_t now, float solar_gain,
                     float temp_c, float setpoint_c, const Params &p = Params{}) {
  Decision d{};
  if (ghi == nullptr || now >= count || !(solar_gain >= p.min_solar_gain))
    return d;
  if (std::isfinite(temp_c) && temp_c < setpoint_c - p.cold_margin_c)
    return d;
  // Strongest sun in [now, now + lead_h], weighted by how close it is: the sun
  // now counts fully, the sun at the far edge of the window a third.
  float best = 0.0f;
  for (size_t i = now; i < count && i <= now + p.lead_h; i++) {
    const float span = static_cast<float>(p.full_wm2 - p.start_wm2);
    const float strength = std::clamp((ghi[i] - p.start_wm2) / (span > 1.0f ? span : 1.0f), 0.0f, 1.0f);
    const float h = static_cast<float>(i - now);
    const float timing = p.lead_h > 0 ? 1.0f - (2.0f / 3.0f) * (h / static_cast<float>(p.lead_h)) : 1.0f;
    const float score = strength * timing;
    if (ghi[i] > d.peak_wm2) {
      d.peak_wm2 = ghi[i];
      d.peak_in_h = static_cast<int8_t>(h);
    }
    best = std::max(best, score);
  }
  const float gain = std::clamp(solar_gain, 0.0f, 1.0f);
  d.offset_c = -std::min(p.max_offset_c, p.max_offset_c * gain * best);
  if (d.offset_c > -0.05f)
    d.offset_c = 0.0f;
  d.active = d.offset_c < 0.0f;
  return d;
}

}  // namespace lune_touch_solar
