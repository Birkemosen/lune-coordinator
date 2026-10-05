#pragma once

// Pure helpers for interpreting V6 /api/v1/zones payloads. Host-testable
// without ArduinoJson — callers supply already-extracted field values.

#include "coordinator_model.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace esphome::lune_touch_coordinator::v6_zones_parse {

struct ZoneFields {
  int zone_number{0};           // 1–6
  float temperature_c{0.0f};
  bool has_temperature{false};
  float setpoint_c{0.0f};
  bool has_setpoint{false};
  float valve_pct{0.0f};
  bool has_valve{false};
  const char *status{nullptr};
  const char *friendly_name{nullptr};
  bool fresh{true};
  bool enabled{true};
  bool has_enabled{false};
  // Group membership. group_primary_zone == 0 means "legacy / self".
  int group_primary_zone{0};    // 1–6 of the sync root
  uint8_t group_members_mask{0};
  bool has_group_info{false};
  // Per-zone heat demand (optional).
  const char *demand_state{nullptr};
  float opening_ratio{0.0f};
  bool has_opening_ratio{false};
  uint32_t saturated_s{0};
  bool headroom{false};
  const char *recommendation{nullptr};
  bool has_heat_demand{false};
};

struct NodeHeatDemandFields {
  int critical_zone{0};
  float critical_opening_ratio{0.0f};
  uint32_t saturated_s{0};
  uint8_t demanding_zones{0};
  bool headroom{false};
  const char *recommendation{nullptr};
  bool present{false};
};

inline bool is_group_primary(const ZoneFields &zone) {
  if (!zone.has_group_info)
    return true;  // legacy firmware: every slot is its own group
  if (zone.group_primary_zone <= 0)
    return true;
  return zone.zone_number == zone.group_primary_zone;
}

inline uint8_t members_mask_from_list(const int *members, size_t count) {
  uint8_t mask = 0;
  if (members == nullptr)
    return mask;
  for (size_t i = 0; i < count; i++) {
    if (members[i] >= 1 && members[i] <= 6)
      mask = static_cast<uint8_t>(mask | (1u << (members[i] - 1)));
  }
  return mask;
}

inline ::lune_touch::HeatRecommendation parse_recommendation(const char *name) {
  return ::lune_touch::heat_recommendation_from_name(name);
}

inline ::lune_touch::NodeHeatDemand to_node_heat_demand(const NodeHeatDemandFields &in,
                                                        uint32_t now_ms) {
  ::lune_touch::NodeHeatDemand out{};
  if (!in.present)
    return out;
  out.critical_zone = in.critical_zone > 0 && in.critical_zone <= 6
                          ? static_cast<uint8_t>(in.critical_zone)
                          : 0;
  out.critical_opening_ratio = in.critical_opening_ratio;
  out.saturated_s = in.saturated_s;
  out.demanding_zones = in.demanding_zones;
  out.headroom = in.headroom;
  out.recommendation = parse_recommendation(in.recommendation);
  out.updated_at_ms = now_ms;
  out.fresh = true;
  return out;
}

// Decide whether to auto-propose a room for a discovered zone.
// Group primaries (or legacy zones) create rooms; secondaries never do.
// Disabled V6 outputs stay slot-only (no auto room) unless they already carry a name.
inline bool should_auto_propose_room(const ZoneFields &zone) {
  if (!is_group_primary(zone))
    return false;
  if (zone.has_enabled && !zone.enabled) {
    const char *name = zone.friendly_name;
    return name != nullptr && name[0] != '\0';
  }
  return true;
}

// Map V6 zone payload → Touch live status. V6 `enabled:false` is unused;
// `state`/`status` describe heat call for enabled outputs.
inline const char *live_status_for_zone(const ZoneFields &zone) {
  if (zone.has_enabled && !zone.enabled)
    return "unused";
  const char *raw = zone.status;
  if (raw == nullptr || raw[0] == '\0')
    return "unknown";
  if (std::strcmp(raw, "OFF") == 0 || std::strcmp(raw, "off") == 0 ||
      std::strcmp(raw, "IDLE") == 0 || std::strcmp(raw, "idle") == 0)
    return "idle";
  if (std::strcmp(raw, "HEATING") == 0 || std::strcmp(raw, "heat") == 0 ||
      std::strcmp(raw, "CALL") == 0 || std::strcmp(raw, "call") == 0)
    return "heat";
  if (std::strcmp(raw, "PREHEAT") == 0 || std::strcmp(raw, "preheat") == 0)
    return "preheat";
  if (std::strcmp(raw, "HOLD") == 0 || std::strcmp(raw, "hold") == 0)
    return "hold";
  if (std::strcmp(raw, "STALE") == 0 || std::strcmp(raw, "stale") == 0)
    return "stale";
  if (std::strcmp(raw, "unused") == 0 || std::strcmp(raw, "UNUSED") == 0)
    return "unused";
  return raw;
}

}  // namespace esphome::lune_touch_coordinator::v6_zones_parse
