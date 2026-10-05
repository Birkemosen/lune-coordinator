#pragma once

#include "asgard_url.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

// The coordinator owns the physical signal and authority policy.  This adapter owns
// only the documented Asgard display-entity contract so paths and response parsing do
// not leak into that policy.
namespace esphome::lune_touch_coordinator::asgard_adapter {

enum class CapabilityStatus : uint8_t {
  READY,
  UNCONFIGURED,
  UNSUPPORTED,
};

inline const char *capability_status_name(CapabilityStatus status) {
  switch (status) {
    case CapabilityStatus::READY: return "ready";
    case CapabilityStatus::UNCONFIGURED: return "unconfigured";
    case CapabilityStatus::UNSUPPORTED: return "unsupported";
  }
  return "unknown";
}

struct Config {
  const char *host{nullptr};
  uint16_t port{80};
  const char *physical_temperature_entity{nullptr};
  // Empty/null → ESPHome `/number/<entity>/set?value=` and `/number/<entity>`.
  const char *write_url_template{nullptr};
  const char *read_url_template{nullptr};
  // Optional virtual-thermostat climate entity for comfort target writes.
  const char *climate_entity{nullptr};
  // Optional Auto-Adaptive Setpoint Bias number entity.
  const char *bias_entity{nullptr};
  // Optional freeze binary sensors (DHW / Legionella / defrost).
  const char *dhw_entity{nullptr};
  const char *legionella_entity{nullptr};
  const char *defrost_entity{nullptr};
};

struct Compatibility {
  CapabilityStatus physical_temperature{CapabilityStatus::UNCONFIGURED};
  CapabilityStatus target_sync{CapabilityStatus::UNSUPPORTED};
  CapabilityStatus bias_sync{CapabilityStatus::UNCONFIGURED};
  CapabilityStatus operating_state{CapabilityStatus::UNSUPPORTED};
  const char *target_blocker{"target synchronization not configured"};
  const char *operating_state_blocker{"external operating-state ingestion intentionally unsupported"};
};

inline bool valid_entity(const char *entity) {
  if (entity == nullptr || entity[0] == '\0') return false;
  for (const char *p = entity; *p != '\0'; ++p) {
    const unsigned char c = static_cast<unsigned char>(*p);
    if (c < 0x20 || c > 0x7e) return false;
  }
  return true;
}

inline Compatibility compatibility(const Config &config) {
  Compatibility result;
  const bool host_ok = asgard_url::valid_host(config.host) && config.port != 0;
  const bool entity_ok = valid_entity(config.physical_temperature_entity);
  const bool templates_ok =
      config.write_url_template != nullptr && config.write_url_template[0] != '\0' &&
      config.read_url_template != nullptr && config.read_url_template[0] != '\0' &&
      asgard_url::valid_url_template(config.write_url_template, true) &&
      asgard_url::valid_url_template(config.read_url_template, false);
  result.physical_temperature =
      host_ok && (entity_ok || templates_ok) ? CapabilityStatus::READY
                                             : CapabilityStatus::UNCONFIGURED;
  if (asgard_url::valid_host(config.host) && config.port != 0 && valid_entity(config.climate_entity)) {
    result.target_sync = CapabilityStatus::READY;
    result.target_blocker = "";
  } else {
    result.target_sync = CapabilityStatus::UNCONFIGURED;
    result.target_blocker = "climate entity not configured";
  }
  result.bias_sync =
      asgard_url::valid_host(config.host) && config.port != 0 && valid_entity(config.bias_entity)
          ? CapabilityStatus::READY
          : CapabilityStatus::UNCONFIGURED;
  return result;
}

inline bool build_physical_write_url(const Config &config, float value, char *out, size_t out_len) {
  if (compatibility(config).physical_temperature != CapabilityStatus::READY) return false;
  if (config.write_url_template != nullptr && config.write_url_template[0] != '\0') {
    return asgard_url::valid_url_template(config.write_url_template, true) &&
           asgard_url::expand_url_template(config.write_url_template, config.host, config.port,
                                           config.physical_temperature_entity != nullptr
                                               ? config.physical_temperature_entity
                                               : "",
                                           value, true, out, out_len);
  }
  if (!valid_entity(config.physical_temperature_entity)) return false;
  return asgard_url::build_number_url(config.host, config.port, config.physical_temperature_entity,
                                      value, out, out_len);
}

inline bool build_physical_read_url(const Config &config, char *out, size_t out_len) {
  if (compatibility(config).physical_temperature != CapabilityStatus::READY) return false;
  if (config.read_url_template != nullptr && config.read_url_template[0] != '\0') {
    return asgard_url::valid_url_template(config.read_url_template, false) &&
           asgard_url::expand_url_template(config.read_url_template, config.host, config.port,
                                           config.physical_temperature_entity != nullptr
                                               ? config.physical_temperature_entity
                                               : "",
                                           0.0f, false, out, out_len);
  }
  if (!valid_entity(config.physical_temperature_entity)) return false;
  return asgard_url::build_number_read_url(config.host, config.port, config.physical_temperature_entity,
                                           out, out_len);
}

inline bool build_climate_target_write_url(const Config &config, float target_c, char *out,
                                           size_t out_len) {
  if (compatibility(config).target_sync != CapabilityStatus::READY) return false;
  return asgard_url::build_climate_target_url(config.host, config.port, config.climate_entity,
                                              target_c, out, out_len);
}

inline bool build_climate_target_read_url(const Config &config, char *out, size_t out_len) {
  if (compatibility(config).target_sync != CapabilityStatus::READY) return false;
  return asgard_url::build_climate_read_url(config.host, config.port, config.climate_entity, out,
                                            out_len);
}

inline bool build_bias_write_url(const Config &config, float bias_c, char *out, size_t out_len) {
  if (compatibility(config).bias_sync != CapabilityStatus::READY) return false;
  return asgard_url::build_number_url(config.host, config.port, config.bias_entity, bias_c, out,
                                      out_len);
}

inline bool build_bias_read_url(const Config &config, char *out, size_t out_len) {
  if (compatibility(config).bias_sync != CapabilityStatus::READY) return false;
  return asgard_url::build_number_read_url(config.host, config.port, config.bias_entity, out,
                                           out_len);
}

inline bool request_succeeded(bool transport_ok, int status_code) {
  return asgard_url::request_succeeded(transport_ok, status_code);
}

// A narrow transport seam keeps host tests independent of ESPHome HTTP while making
// endpoint discovery/readback behavior testable with a fake adapter transport.
class Transport {
 public:
  virtual ~Transport() = default;
  virtual bool get(const char *url, char *body, size_t body_len, int *status) = 0;
  virtual bool post_empty(const char *url, int *status) = 0;
};

inline bool parse_number_response(const char *body, float *value) {
  if (body == nullptr || value == nullptr) return false;
  *value = NAN;
  for (const char *key : {"\"value\"", "\"state\""}) {
    const char *field = std::strstr(body, key);
    if (field == nullptr) continue;
    const char *colon = std::strchr(field + std::strlen(key), ':');
    if (colon == nullptr) continue;
    const char *start = colon + 1;
    while (*start == ' ' || *start == '\t' || *start == '\"') ++start;
    char *end = nullptr;
    const float parsed = std::strtof(start, &end);
    if (end != start && std::isfinite(parsed)) {
      *value = parsed;
      return true;
    }
  }
  return false;
}

inline bool parse_climate_target(const char *body, float *value) {
  if (body == nullptr || value == nullptr) return false;
  *value = NAN;
  for (const char *key : {"\"target_temperature\"", "\"temperature\""}) {
    const char *field = std::strstr(body, key);
    if (field == nullptr) continue;
    const char *colon = std::strchr(field + std::strlen(key), ':');
    if (colon == nullptr) continue;
    const char *start = colon + 1;
    while (*start == ' ' || *start == '\t' || *start == '\"') ++start;
    char *end = nullptr;
    const float parsed = std::strtof(start, &end);
    if (end != start && std::isfinite(parsed)) {
      *value = parsed;
      return true;
    }
  }
  return false;
}

// Parse ESPHome binary_sensor JSON: {"state":"ON"} / {"value":true} / "off".
inline bool parse_binary_state(const char *body, bool *on) {
  if (body == nullptr || on == nullptr) return false;
  *on = false;
  for (const char *key : {"\"state\"", "\"value\""}) {
    const char *field = std::strstr(body, key);
    if (field == nullptr) continue;
    const char *colon = std::strchr(field + std::strlen(key), ':');
    if (colon == nullptr) continue;
    const char *start = colon + 1;
    while (*start == ' ' || *start == '\t') ++start;
    if (*start == '\"') {
      ++start;
      if (std::strncmp(start, "ON", 2) == 0 || std::strncmp(start, "on", 2) == 0 ||
          std::strncmp(start, "true", 4) == 0 || std::strncmp(start, "1", 1) == 0) {
        *on = true;
        return true;
      }
      if (std::strncmp(start, "OFF", 3) == 0 || std::strncmp(start, "off", 3) == 0 ||
          std::strncmp(start, "false", 5) == 0 || std::strncmp(start, "0", 1) == 0) {
        *on = false;
        return true;
      }
      return false;
    }
    if (*start == 't' || *start == 'T' || *start == '1') {
      *on = true;
      return true;
    }
    if (*start == 'f' || *start == 'F' || *start == '0') {
      *on = false;
      return true;
    }
  }
  return false;
}

inline bool read_number(Transport &transport, const char *url, float *value, int *status) {
  if (value == nullptr || status == nullptr || url == nullptr) return false;
  char body[384]{};
  *status = 0;
  return transport.get(url, body, sizeof(body), status) &&
         asgard_url::request_succeeded(true, *status) && parse_number_response(body, value);
}

inline bool read_climate_target(Transport &transport, const char *url, float *value, int *status) {
  if (value == nullptr || status == nullptr || url == nullptr) return false;
  char body[384]{};
  *status = 0;
  return transport.get(url, body, sizeof(body), status) &&
         asgard_url::request_succeeded(true, *status) && parse_climate_target(body, value);
}

inline bool read_binary_state(Transport &transport, const char *url, bool *on, int *status) {
  if (on == nullptr || status == nullptr || url == nullptr) return false;
  char body[384]{};
  *status = 0;
  return transport.get(url, body, sizeof(body), status) &&
         asgard_url::request_succeeded(true, *status) && parse_binary_state(body, on);
}

inline float round_tenths(float value) {
  if (!std::isfinite(value)) return value;
  return std::round(value * 10.0f) / 10.0f;
}

}  // namespace esphome::lune_touch_coordinator::asgard_adapter
