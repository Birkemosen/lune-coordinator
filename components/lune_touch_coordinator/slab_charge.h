// =============================================================================
// Slab charge — capacity-aware wind/cold pre-charging (pure C++, host-testable)
// =============================================================================
// Why: a wind-exposed room (e.g. a light timber-frame upper floor) can lose
// more heat than its floor heating can deliver once wind and cold peak. If the
// room is not pre-heated beforehand — typically because a sunny afternoon kept
// it warm and the valve closed — it lags and cannot catch up. A setpoint offset
// alone does not help: a sun-warmed room is already above setpoint + offset, so
// V6 trims the valve and the slab never charges.
//
// What: per room and forecast hour, compare the expected heat loss (UA × ΔT,
// raised by wind on the exposed walls, minus solar gain) with the floor's
// maximum output (EN 1264 simplified: q = ΔT_water-room / R). When loss exceeds
// output, the shortfall energy must already be stored in the slab. The model
// returns how much (as °C above setpoint), when charging must start and when
// the deficit episode ends. Touch then (a) raises the setpoint offset and
// (b) arms per-zone absorb on V6 so the valve stays open while the room is
// warm. See docs/house_balancing_and_weather.md.
//
// Odin models house heat demand from weather implicitly but not per-room wind
// exposure; this model only covers what Odin cannot see (room-level wind/cold
// shortfall against floor capacity).
// =============================================================================

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace lune_touch_charge {

struct Hour {
  float temp_c = 0.0f;         ///< Outdoor temperature
  float wind_ms = 0.0f;        ///< Wind speed
  float wind_dir_deg = 0.0f;   ///< Meteorological: direction the wind comes FROM
  float shortwave_wm2 = 0.0f;  ///< Global horizontal irradiance
};

struct Room {
  float area_m2 = 0.0f;           ///< Heated floor area served by the loop(s)
  float ua_w_per_k = 0.0f;        ///< Effective envelope loss (W/K), calm conditions
  float floor_r_m2k_per_w = 0.0f; ///< Covering + slab resistance above the pipes (0 → default)
  float c_slab_kwh_per_k = 0.0f;  ///< Storage mass of slab + room (0 → area default)
  uint8_t exterior_walls = 0;     ///< N=1|E=2|S=4|W=8
  float wind_exposure = 0.5f;     ///< 0..1 facade exposure (shelter, terrain)
  float solar_gain = 0.3f;        ///< 0..1 passive solar gain through glazing
  float setpoint_c = 21.0f;
  float temp_c = NAN;             ///< Current room temperature (NaN = unknown)
};

struct Params {
  /// Mean water temperature the floor can count on during a cold spell (°C).
  float water_mean_c = 30.0f;
  /// Floor-surface-to-room resistance (EN 1264 ≈ 1/10.8 W/m²K).
  float surface_r_m2k_per_w = 0.093f;
  /// Fallback covering + slab-above-pipe resistance when V6 has no floor data.
  float default_floor_r_m2k_per_w = 0.10f;
  /// Extra infiltration/convection loss per unit exposure at 10 m/s head-on
  /// (0.6 → +60 % for a fully exposed facade). Calibration constant.
  float wind_loss_factor = 0.6f;
  /// Share of horizontal irradiance on the floor area that reaches the room.
  float solar_aperture = 0.10f;
  /// Storage when V6 reports no slab mass: kWh/K per m² (≈ 60 mm screed + room).
  float default_c_kwh_per_k_m2 = 0.008f;
  /// Never plan more than this above setpoint (comfort ceiling).
  float max_store_c = 1.5f;
  /// Safety margin before the deficit begins (hours).
  float margin_h = 1.0f;
  /// Shortfall below this energy is ignored (kWh) — noise, not an episode.
  float min_deficit_kwh = 0.15f;
  size_t horizon_h = 24;
};

struct Decision {
  bool episode = false;       ///< A capacity shortfall lies ahead within the horizon
  bool charge_now = false;    ///< Charging window is open now
  bool insufficient = false;  ///< Even max_store_c cannot cover the deficit
  float floor_capacity_w = 0.0f;
  float peak_loss_w = 0.0f;
  float deficit_kwh = 0.0f;   ///< Shortfall energy of the first episode
  float store_c = 0.0f;       ///< Planned slab charge above setpoint (°C)
  float charge_hours = 0.0f;  ///< Hours of spare floor power needed to store it
  int16_t start_in_h = -1;    ///< Hours until charging must start (≤0 = now)
  int16_t episode_in_h = -1;  ///< Hours until the shortfall begins
  int16_t episode_end_in_h = -1;  ///< Hours until it ends (exclusive)
};

/// Best cos(wind source − wall normal) over the exterior walls, floored at 0.
inline float wind_alignment(uint8_t walls, float wind_dir_deg) {
  static constexpr float kNormals[4] = {0.0f, 90.0f, 180.0f, 270.0f};  // N E S W
  float best = 0.0f;
  for (int i = 0; i < 4; i++) {
    if ((walls & (1u << i)) == 0)
      continue;
    const float d = (wind_dir_deg - kNormals[i]) * 3.14159265f / 180.0f;
    best = std::max(best, std::cos(d));
  }
  return best;
}

/// Floor output at full water temperature (W). EN 1264 simplified.
inline float floor_capacity_w(const Room &room, const Params &p) {
  const float r_floor = room.floor_r_m2k_per_w > 0.0f ? room.floor_r_m2k_per_w
                                                      : p.default_floor_r_m2k_per_w;
  const float dt = p.water_mean_c - room.setpoint_c;
  if (!(dt > 0.0f) || !(room.area_m2 > 0.0f))
    return 0.0f;
  return room.area_m2 * dt / (r_floor + p.surface_r_m2k_per_w);
}

/// Expected room heat loss for one hour at setpoint (W), wind-raised, solar-relieved.
inline float hour_loss_w(const Hour &h, const Room &room, const Params &p) {
  const float dt = room.setpoint_c - h.temp_c;
  if (!(dt > 0.0f) || !(room.ua_w_per_k > 0.0f))
    return 0.0f;
  const float wind = std::max(0.0f, h.wind_ms) / 10.0f;
  const float wind_mult = 1.0f + p.wind_loss_factor * std::clamp(room.wind_exposure, 0.0f, 1.0f) *
                                     wind_alignment(room.exterior_walls, h.wind_dir_deg) * wind;
  const float solar = std::clamp(room.solar_gain, 0.0f, 1.0f) * std::max(0.0f, h.shortwave_wm2) *
                      room.area_m2 * p.solar_aperture;
  return std::max(0.0f, room.ua_w_per_k * dt * wind_mult - solar);
}

/// Plan slab charging for the first capacity shortfall within the horizon.
/// `now_index` is the forecast hour that contains "now".
inline Decision plan(const Hour *hours, size_t count, size_t now_index, const Room &room,
                     const Params &p) {
  Decision d{};
  if (hours == nullptr || now_index >= count || !(room.area_m2 > 0.0f) || !(room.ua_w_per_k > 0.0f))
    return d;
  const float cap = floor_capacity_w(room, p);
  d.floor_capacity_w = cap;
  if (!(cap > 0.0f))
    return d;
  const size_t end = std::min(count, now_index + p.horizon_h);

  // First contiguous episode where loss exceeds capacity.
  size_t ep_start = end, ep_end = end;
  float deficit_wh = 0.0f;
  for (size_t i = now_index; i < end; i++) {
    const float loss = hour_loss_w(hours[i], room, p);
    d.peak_loss_w = std::max(d.peak_loss_w, loss);
    if (loss > cap) {
      if (ep_start == end)
        ep_start = i;
      deficit_wh += loss - cap;
      ep_end = i + 1;
    } else if (ep_start != end) {
      break;
    }
  }
  d.deficit_kwh = deficit_wh / 1000.0f;
  if (ep_start == end || d.deficit_kwh < p.min_deficit_kwh)
    return d;

  d.episode = true;
  d.episode_in_h = static_cast<int16_t>(ep_start - now_index);
  d.episode_end_in_h = static_cast<int16_t>(ep_end - now_index);

  // Store the shortfall in the slab: ΔT = E / C, capped at the comfort ceiling.
  const float c = room.c_slab_kwh_per_k > 0.0f ? room.c_slab_kwh_per_k
                                               : room.area_m2 * p.default_c_kwh_per_k_m2;
  float store = d.deficit_kwh / std::max(c, 0.05f);
  if (store > p.max_store_c) {
    d.insufficient = true;
    store = p.max_store_c;
  }
  d.store_c = store;

  // Charging uses the floor's spare power in the hours before the episode
  // (walking back from its start). Hours until the stored energy is covered.
  const float need_wh = store * std::max(c, 0.05f) * 1000.0f;
  float got_wh = 0.0f;
  size_t k = ep_start;
  while (k > 0 && got_wh < need_wh && (ep_start - k) < p.horizon_h) {
    k--;
    const float spare = cap - hour_loss_w(hours[k], room, p);
    got_wh += std::max(0.0f, spare);
  }
  d.charge_hours = static_cast<float>(ep_start - k);
  const float start_h = static_cast<float>(ep_start) - d.charge_hours - p.margin_h -
                        static_cast<float>(now_index);
  d.start_in_h = static_cast<int16_t>(std::floor(start_h));
  d.charge_now = d.start_in_h <= 0 && d.episode_end_in_h > 0;
  return d;
}

}  // namespace lune_touch_charge
