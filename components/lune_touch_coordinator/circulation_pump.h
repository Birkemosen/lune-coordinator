#pragma once

#include "asgard_adapter.h"
#include "asgard_url.h"

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

inline bool parse_sensor_response(const char *body, float *value) {
  return asgard_adapter::parse_number_response(body, value);
}

}  // namespace esphome::lune_touch_coordinator::circulation_pump
