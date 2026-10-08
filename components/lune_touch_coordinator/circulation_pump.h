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

// Mixing in the separator: when the secondary pump moves more water than the heat pump,
// return water is drawn back into the supply and the zones get a cooler flow, so the
// heat pump has to run hotter for the same room temperature. Only judged while the
// heat pump's own pump runs for space heating (operation_mode 2): during hot water the
// primary flow goes to the tank, and a stopped primary says nothing about the ratio.
enum class Mixing : uint8_t { Unknown, Ok, Risk };

/// Secondary may exceed primary by this much before it counts as mixing (sensor noise).
constexpr float MIXING_RISK_RATIO = 1.05f;
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
  return ratio > MIXING_RISK_RATIO ? Mixing::Risk : Mixing::Ok;
}

inline const char *mixing_name(Mixing m) {
  return m == Mixing::Risk ? "risk" : m == Mixing::Ok ? "ok" : "unknown";
}

}  // namespace esphome::lune_touch_coordinator::circulation_pump
