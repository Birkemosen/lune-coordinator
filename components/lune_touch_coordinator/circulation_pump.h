#pragma once

#include "asgard_adapter.h"
#include "asgard_url.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

// Read-only ESPHome web_server contract for a satellite Alpha2 Go node.
// Touch polls flow/head/power; it must not command the pump or heat source.
namespace esphome::lune_touch_coordinator::circulation_pump {

inline bool valid_entity(const char *entity) {
  return asgard_adapter::valid_entity(entity);
}

inline bool build_sensor_read_url(const char *host, uint16_t port, const char *entity,
                                  char *out, size_t out_len) {
  char encoded[145];
  if (!asgard_url::valid_host(host) || port == 0 || !valid_entity(entity) ||
      !asgard_url::encode_entity_once(entity, encoded, sizeof(encoded)))
    return false;
  const int written = std::snprintf(out, out_len, "http://%s:%u/sensor/%s",
                                    host, static_cast<unsigned>(port), encoded);
  return written >= 0 && static_cast<size_t>(written) < out_len;
}

// ESPHome's web_server addressed entities by object id ("pump_flow") until
// 2025.x; newer builds address them by name ("Pump Flow") and answer 404 to the
// old form. On a 404 Touch retries once with the other form:
//   object id → name: "_" → " ", each word capitalised;  name → object id: lower case, " " → "_".
// Returns false when the alternative would be the same string.
inline bool alternate_entity(const char *entity, char *out, size_t out_len) {
  if (entity == nullptr || out == nullptr || out_len == 0)
    return false;
  bool has_space = false;
  for (const char *c = entity; *c; c++)
    if (*c == ' ') has_space = true;
  size_t i = 0;
  bool word_start = true;
  for (const char *c = entity; *c && i + 1 < out_len; c++, i++) {
    char ch = *c;
    if (has_space) {
      ch = ch == ' ' ? '_' : (ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch - 'A' + 'a') : ch);
    } else {
      if (ch == '_') { ch = ' '; word_start = true; }
      else if (word_start) { if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A'); word_start = false; }
    }
    out[i] = ch;
  }
  out[i] = '\0';
  for (size_t k = 0;; k++) {
    if (out[k] != entity[k]) return true;
    if (out[k] == '\0') return false;
  }
}

inline bool parse_sensor_response(const char *body, float *value) {
  return asgard_adapter::parse_number_response(body, value);
}

// Balance across the buffer tank (gekkekoe: heat going into the tank ≤ heat drawn from it).
// With a parallel tank the flows decide where the water goes:
//   secondary < primary  SHORT   the heat pump's surplus runs through the tank straight back
//                                to the heat pump; its return warms up, it reaches its target
//                                early and cycles. The tank charges instead of the floors.
//   secondary ≈ primary  OK      (100–125 %): all of the heat pump's water goes to the floors;
//                                a little floor return mixed into the supply costs < 1 K.
//   secondary ≫ primary  MIXING  much floor return is drawn back into the supply, so the heat
//                                pump has to run hotter for the same room temperature.
// Only judged while the heat pump's own pump runs for space heating (operation_mode 2):
// during hot water the primary flow goes to the tank, and a stopped primary says nothing.
enum class Mixing : uint8_t { Unknown, Ok, Short, Risk };

/// Below this secondary/primary ratio the heat pump gets its own water back (sensor noise allowed).
constexpr float SHORT_FLOW_RATIO = 0.97f;
/// Above this ratio the supply is diluted enough to matter.
constexpr float MIXING_RISK_RATIO = 1.25f;
/// Below this the primary flow is a start-up/stop transient, not a steady value.
constexpr float MIXING_MIN_PRIMARY_LMIN = 3.0f;
/// How long a measured ratio is shown after the heat pump stops.
constexpr uint32_t MIXING_HOLD_MS = 6UL * 60UL * 60UL * 1000UL;

inline bool mixing_sample_usable(float primary_lmin, float secondary_lmin, bool primary_pump_on,
                                 int operation_mode) {
  return primary_pump_on && operation_mode == 2 && std::isfinite(primary_lmin) &&
         primary_lmin >= MIXING_MIN_PRIMARY_LMIN && std::isfinite(secondary_lmin) && secondary_lmin > 0.0f;
}

inline Mixing mixing_state(float ratio) {
  if (!std::isfinite(ratio) || ratio <= 0.0f)
    return Mixing::Unknown;
  if (ratio < SHORT_FLOW_RATIO)
    return Mixing::Short;
  return ratio > MIXING_RISK_RATIO ? Mixing::Risk : Mixing::Ok;
}

// Recommended secondary flow: a little above the heat pump's flow, so none of its water
// goes back through the tank, and not so far above that the supply is diluted. Rounded up
// to 0.5 l/min, the resolution a pump setting is made in.
constexpr float RECOMMENDED_SECONDARY_SHARE = 1.05f;

inline float recommended_secondary_lmin(float primary_lmin) {
  if (!std::isfinite(primary_lmin) || primary_lmin < MIXING_MIN_PRIMARY_LMIN)
    return NAN;
  return std::ceil(primary_lmin * RECOMMENDED_SECONDARY_SHARE * 2.0f - 1e-3f) / 2.0f;
}

/// Heat carried by a water flow: l/min × ΔT K → kW (c = 4.186 kJ/kg·K). NAN without a
/// usable ΔT (a stopped flow or reversed sensors say nothing).
inline float water_kw(float flow_lmin, float feed_c, float return_c) {
  if (!std::isfinite(flow_lmin) || !std::isfinite(feed_c) || !std::isfinite(return_c) || flow_lmin <= 0.0f)
    return NAN;
  const float dt = feed_c - return_c;
  if (dt <= 0.2f || dt > 30.0f)
    return NAN;
  return flow_lmin / 60.0f * 4.186f * dt;
}

/// Head the pump would need for target_lmin through the same open valves: the system
/// curve is quadratic (H ∝ Q²). This is the constant-pressure setting to start from.
inline float head_for_flow_m(float head_m, float flow_lmin, float target_lmin) {
  if (!std::isfinite(head_m) || !std::isfinite(flow_lmin) || !std::isfinite(target_lmin) ||
      head_m <= 0.0f || flow_lmin < 1.0f || target_lmin <= 0.0f)
    return NAN;
  const float r = target_lmin / flow_lmin;
  return std::round(head_m * r * r * 10.0f) / 10.0f;
}

inline const char *mixing_name(Mixing m) {
  switch (m) {
    case Mixing::Ok: return "ok";
    case Mixing::Short: return "short";
    case Mixing::Risk: return "risk";
    default: return "unknown";
  }
}

}  // namespace esphome::lune_touch_coordinator::circulation_pump
