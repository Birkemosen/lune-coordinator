#pragma once

#include "plan_source.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace esphome::lune_touch_coordinator::odin_mqtt {

// Read-only MQTT client surface against Odin's embedded broker (U2).
// Touch never publishes on this broker.

static constexpr size_t MAX_PAYLOAD = 4096;
static constexpr uint32_t HTTP_FALLBACK_INTERVAL_MS = 5UL * 60UL * 1000UL;
static constexpr uint32_t STREAM_STALE_MS = 10UL * 60UL * 1000UL;

enum class StreamId : uint8_t { Status = 0, SolverResult = 1, Commands = 2, Telemetry = 3, Count = 4 };

inline const char *stream_name(StreamId id) {
  switch (id) {
    case StreamId::Status: return "status";
    case StreamId::SolverResult: return "result";
    case StreamId::Commands: return "commands";
    case StreamId::Telemetry: return "telemetry";
    default: return "unknown";
  }
}

struct StreamAge {
  uint32_t last_message_ms{0};
  bool ever_seen{false};
  char last_error[64]{};
};

struct StatusState {
  bool online{false};
  bool known{false};
};

struct CommandState {
  bool valid{false};
  bool soft_stop{false};
  bool heat_mode{false};
  plan_source::DecisionReason decision_reason{plan_source::DecisionReason::Unknown};
  float flow_target_c{NAN};
  uint32_t valid_until_unix{0};
};

struct TelemetryState {
  bool valid{false};
  bool defrost_active{false};
  bool defrost_known{false};
  bool dhw_active{false};
  bool dhw_known{false};
  bool legionella_active{false};
  bool legionella_known{false};
  float thermal_w{NAN};
  float flow_rate{NAN};
  float return_temp_c{NAN};
  float hp_feed_temp_c{NAN};
};

struct ClientConfig {
  bool enabled{false};
  char host[64]{};
  uint16_t port{1883};
  char username[64]{};
  // Password lives in NVS only; never serialize into GET responses.
  char password[64]{};
  bool password_set{false};
  char topic_prefix[64]{};  // HP MQTT prefix from Odin config (not hardcoded)
  char hp_id[32]{};
};

struct ClientState {
  ClientConfig config{};
  bool connected{false};
  uint32_t reconnect_backoff_ms{1000};
  uint32_t last_connect_attempt_ms{0};
  uint32_t last_error_ms{0};
  char last_error[96]{};
  StreamAge ages[static_cast<size_t>(StreamId::Count)]{};
  StatusState status{};
  CommandState command{};
  TelemetryState telemetry{};
  // Retained messages: assume unsupported until proven (U2).
  bool retained_proven{false};
};

inline uint32_t next_backoff_ms(uint32_t current) {
  const uint32_t next = current < 1000 ? 1000 : current * 2;
  return next > 60000 ? 60000 : next;
}

inline bool stream_fresh(const StreamAge &age, uint32_t now_ms, uint32_t stale_ms = STREAM_STALE_MS) {
  return age.ever_seen && age.last_message_ms != 0 && (now_ms - age.last_message_ms) <= stale_ms;
}

// odin/status payload: {"state":"online"|"offline"} or {"online":true|false}
inline bool parse_status_payload(const char *json, StatusState *out) {
  if (json == nullptr || out == nullptr)
    return false;
  if (std::strstr(json, "\"offline\"") != nullptr || std::strstr(json, "\"online\":false") != nullptr ||
      std::strstr(json, "\"online\": false") != nullptr) {
    out->online = false;
    out->known = true;
    return true;
  }
  if (std::strstr(json, "\"online\"") != nullptr || std::strstr(json, "\"online\":true") != nullptr ||
      std::strstr(json, "\"online\": true") != nullptr) {
    out->online = true;
    out->known = true;
    return true;
  }
  return false;
}

// Minimal command parse for soft_stop / mode / decision_reason / valid_until / flow_target.
// Full JSON lives in firmware; this helper is for host tests and fail-closed defaults.
inline bool parse_command_hints(const char *json, CommandState *out) {
  if (json == nullptr || out == nullptr || std::strlen(json) > MAX_PAYLOAD)
    return false;
  CommandState cmd{};
  cmd.valid = true;
  if (std::strstr(json, "\"soft_stop\":true") != nullptr ||
      std::strstr(json, "\"soft_stop\": true") != nullptr)
    cmd.soft_stop = true;
  // Heat mode: string "heat" / "heating" or numeric mode == 2
  if (std::strstr(json, "\"heat\"") != nullptr || std::strstr(json, "\"heating\"") != nullptr ||
      std::strstr(json, "\"mode\":2") != nullptr || std::strstr(json, "\"mode\": 2") != nullptr)
    cmd.heat_mode = true;
  if (const char *p = std::strstr(json, "\"decision_reason\"")) {
    const char *q = std::strchr(p, ':');
    if (q != nullptr) {
      while (*q == ':' || *q == ' ' || *q == '\"')
        ++q;
      char buf[48]{};
      size_t n = 0;
      while (*q && *q != '\"' && *q != ',' && *q != '}' && n + 1 < sizeof(buf))
        buf[n++] = *q++;
      cmd.decision_reason = plan_source::decision_reason_from_string(buf);
    }
  }
  *out = cmd;
  return true;
}

inline bool parse_telemetry_defrost(const char *json, TelemetryState *out) {
  if (json == nullptr || out == nullptr || std::strlen(json) > MAX_PAYLOAD)
    return false;
  if (std::strstr(json, "\"defrost\":true") != nullptr ||
      std::strstr(json, "\"defrost\": true") != nullptr ||
      std::strstr(json, "\"status_defrost\":true") != nullptr ||
      std::strstr(json, "\"status_defrost\": true") != nullptr) {
    out->defrost_active = true;
    out->defrost_known = true;
    out->valid = true;
    return true;
  }
  if (std::strstr(json, "\"defrost\":false") != nullptr ||
      std::strstr(json, "\"defrost\": false") != nullptr ||
      std::strstr(json, "\"status_defrost\":false") != nullptr ||
      std::strstr(json, "\"status_defrost\": false") != nullptr) {
    out->defrost_active = false;
    out->defrost_known = true;
    out->valid = true;
    return true;
  }
  return false;
}

// Arm TTL from command validity window, capped by local loft (seconds).
inline uint32_t arm_ttl_s(uint32_t valid_until_unix, uint32_t now_unix, uint32_t loft_s) {
  if (valid_until_unix <= now_unix || loft_s == 0)
    return 0;
  const uint32_t remain = valid_until_unix - now_unix;
  return remain < loft_s ? remain : loft_s;
}

inline bool should_disarm_from_command(const CommandState &cmd) {
  return cmd.valid && (cmd.soft_stop || !cmd.heat_mode);
}

inline bool should_arm_from_command(const CommandState &cmd, bool arm_energy_cost_modulation) {
  if (!cmd.valid || cmd.soft_stop || !cmd.heat_mode)
    return false;
  return plan_source::may_arm_absorb_v2(cmd.decision_reason, arm_energy_cost_modulation);
}

}  // namespace esphome::lune_touch_coordinator::odin_mqtt
