#pragma once

// Shadow planner settings and its API body (GET /api/lune-touch/v1/planner). The planner
// itself (components/planner/) runs in shadow: it computes and logs, and nothing here
// commands the heat source. Until it runs, plan/rooms/model/history are null.
// Stored as one blob ("planner") in the settings namespace. Pure C++, host-testable.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace esphome::lune_touch_coordinator::planner_settings {

/// Whether Touch's own plan drives the heat source. False while the planner runs in shadow
/// and Odin decides the heat (F0–F4); the takeover (F5) turns it on. One switch for what
/// depends on who heats: the API's "mode", and which plan the lift's room forecasts run on.
inline constexpr bool PLANNER_DRIVES_HEAT = false;
inline const char *mode_name() { return PLANNER_DRIVES_HEAT ? "control" : "shadow"; }

enum class Strategy : uint8_t { COMFORT = 0, WEIGHTED = 1, PRICE = 2 };

static constexpr uint8_t BLOB_VERSION = 1;

struct Settings {
  uint8_t version{BLOB_VERSION};
  Strategy strategy{Strategy::COMFORT};
  float comfort_weight_eur{0.5f};   // EUR per K·h below the band (weighted strategy)
  float min_output_kw{3.2f};
  float max_output_kw{8.0f};
  float max_flow_c{35.0f};
};

inline const char *strategy_name(Strategy s) {
  switch (s) {
    case Strategy::WEIGHTED: return "weighted";
    case Strategy::PRICE: return "price";
    default: return "comfort";
  }
}

inline bool parse_strategy(const char *s, Strategy *out) {
  if (s == nullptr) return false;
  if (std::strcmp(s, "comfort") == 0) { *out = Strategy::COMFORT; return true; }
  if (std::strcmp(s, "weighted") == 0) { *out = Strategy::WEIGHTED; return true; }
  if (std::strcmp(s, "price") == 0) { *out = Strategy::PRICE; return true; }
  return false;
}

inline float clampf_(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Every field kept in its range; min output never above max.
inline Settings sanitize(Settings s) {
  if (static_cast<uint8_t>(s.strategy) > 2) s.strategy = Strategy::COMFORT;
  s.comfort_weight_eur = std::isfinite(s.comfort_weight_eur) ? clampf_(s.comfort_weight_eur, 0.0f, 5.0f) : 0.5f;
  s.min_output_kw = std::isfinite(s.min_output_kw) ? clampf_(s.min_output_kw, 0.0f, 10.0f) : 3.2f;
  s.max_output_kw = std::isfinite(s.max_output_kw) ? clampf_(s.max_output_kw, 1.0f, 16.0f) : 8.0f;
  if (s.min_output_kw > s.max_output_kw) s.min_output_kw = s.max_output_kw;
  s.max_flow_c = std::isfinite(s.max_flow_c) ? clampf_(s.max_flow_c, 25.0f, 55.0f) : 35.0f;
  s.version = BLOB_VERSION;
  return s;
}

inline size_t write_settings_json(const Settings &s, char *buf, size_t cap) {
  const int n = std::snprintf(buf, cap,
                              "{\"strategy\":\"%s\",\"comfort_weight_eur\":%.2f,\"min_output_kw\":%.1f,"
                              "\"max_output_kw\":%.1f,\"max_flow_c\":%.1f}",
                              strategy_name(s.strategy), s.comfort_weight_eur, s.min_output_kw, s.max_output_kw,
                              s.max_flow_c);
  return n < 0 ? 0 : (static_cast<size_t>(n) < cap ? static_cast<size_t>(n) : cap - 1);
}

// The whole GET body while no planner runs: mode, settings, and null for every result.
inline void write_api_json(const Settings &s, char *buf, size_t cap) {
  char set[192];
  write_settings_json(s, set, sizeof(set));
  std::snprintf(buf, cap,
                "{\"mode\":\"%s\",\"running\":false,\"strategy\":\"%s\",\"settings\":%s,"
                "\"solved_ts\":null,\"plan\":null,\"rooms\":null,\"model\":null,\"history\":null}",
                mode_name(), strategy_name(s.strategy), set);
}

}  // namespace esphome::lune_touch_coordinator::planner_settings
