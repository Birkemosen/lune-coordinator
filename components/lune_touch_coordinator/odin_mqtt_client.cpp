// Read-only esp-mqtt client for Odin 2.0 (U2).
// Touch never publishes on Odin's broker.

#include "lune_touch_coordinator.h"
#include "esphome/core/log.h"
#include <cstdio>
#include <cstring>
#include <string>

#if defined(USE_ESP32)
#include "mqtt_client.h"
#endif

namespace esphome {
namespace lune_touch_coordinator {

static const char *const MQTT_TAG = "lune_odin_mqtt";

#if defined(USE_ESP32)
static esp_mqtt_client_handle_t g_odin_mqtt_handle = nullptr;

static void odin_mqtt_event_handler(void *handler_args, esp_event_base_t /*base*/, int32_t event_id,
                                    void *event_data) {
  auto *self = static_cast<LuneTouchCoordinator *>(handler_args);
  if (self == nullptr || event_data == nullptr)
    return;
  const auto *event = static_cast<esp_mqtt_event_handle_t>(event_data);
  const uint32_t now = esphome::millis();
  switch (static_cast<esp_mqtt_event_id_t>(event_id)) {
    case MQTT_EVENT_CONNECTED:
      self->on_odin_mqtt_connected_(now);
      break;
    case MQTT_EVENT_DISCONNECTED:
      self->on_odin_mqtt_disconnected_(now);
      break;
    case MQTT_EVENT_DATA:
      if (event->topic != nullptr && event->data != nullptr && event->data_len > 0 &&
          event->data_len <= static_cast<int>(odin_mqtt::MAX_PAYLOAD)) {
        std::string topic(event->topic, event->topic_len);
        std::string payload(event->data, event->data_len);
        self->on_odin_mqtt_data_(topic.c_str(), payload.c_str(), now);
      }
      break;
    case MQTT_EVENT_ERROR:
      self->on_odin_mqtt_error_("mqtt_error", now);
      break;
    default:
      break;
  }
}
#endif

void LuneTouchCoordinator::on_odin_mqtt_connected_(uint32_t now_ms) {
  odin_mqtt_.connected = true;
  odin_mqtt_.http_fallback_active = false;
  odin_mqtt_.last_error[0] = '\0';
  odin_mqtt_client_.connected = true;
  odin_mqtt_client_.reconnect_backoff_ms = 1000;
  // Bootstrap plan from HTTP on (re)connect — retained messages assumed
  // unsupported. Never fetch here: this runs on esp-mqtt's small task, and the
  // plan fetch (snapshot + HTTP + JSON) overflowed it and crashed the Touch in
  // a boot loop as soon as MQTT connected (2026-10-04). The odin task does it.
  mqtt_refetch_pending_ = true;
  kick_task_(odin_task_handle_);
  (void) now_ms;
  ESP_LOGI(MQTT_TAG, "connected (subscribe-only; no publish)");
#if defined(USE_ESP32)
  if (g_odin_mqtt_handle == nullptr)
    return;
  char topic[160];
  std::snprintf(topic, sizeof(topic), "odin/status");
  esp_mqtt_client_subscribe(g_odin_mqtt_handle, topic, 0);
  if (odin_mqtt_.hp_id[0] != '\0') {
    std::snprintf(topic, sizeof(topic), "odin/solver/result/%s", odin_mqtt_.hp_id);
    esp_mqtt_client_subscribe(g_odin_mqtt_handle, topic, 0);
  }
  if (odin_mqtt_.topic_prefix[0] != '\0') {
    std::snprintf(topic, sizeof(topic), "%s/commands/setpoint", odin_mqtt_.topic_prefix);
    esp_mqtt_client_subscribe(g_odin_mqtt_handle, topic, 0);
    std::snprintf(topic, sizeof(topic), "%s/telemetry", odin_mqtt_.topic_prefix);
    esp_mqtt_client_subscribe(g_odin_mqtt_handle, topic, 0);
  }
#endif
}

void LuneTouchCoordinator::on_odin_mqtt_disconnected_(uint32_t now_ms) {
  odin_mqtt_.connected = false;
  odin_mqtt_.http_fallback_active = true;
  odin_mqtt_client_.connected = false;
  odin_mqtt_client_.reconnect_backoff_ms =
      odin_mqtt::next_backoff_ms(odin_mqtt_client_.reconnect_backoff_ms);
  // Losing the broker says nothing about Odin: MQTT is an accelerator, the
  // plan is still fetched over HTTP. Only Odin's own retained "offline"
  // status (below) marks the plan unusable. Odin's online state is unknown now.
  if (take_state_lock_(50)) {
    odin_mqtt_.odin_online_known = false;
    give_state_lock_();
  }
  (void) now_ms;
}

void LuneTouchCoordinator::on_odin_mqtt_error_(const char *message, uint32_t now_ms) {
  if (message != nullptr)
    std::strncpy(odin_mqtt_.last_error, message, sizeof(odin_mqtt_.last_error) - 1);
  odin_mqtt_.last_error[sizeof(odin_mqtt_.last_error) - 1] = '\0';
  odin_mqtt_client_.last_error_ms = now_ms;
}

void LuneTouchCoordinator::on_odin_mqtt_data_(const char *topic, const char *payload,
                                              uint32_t now_ms) {
  if (topic == nullptr || payload == nullptr)
    return;
  if (std::strstr(topic, "odin/status") != nullptr) {
    odin_mqtt::StatusState st{};
    if (odin_mqtt::parse_status_payload(payload, &st)) {
      odin_mqtt_.odin_online = st.online;
      odin_mqtt_.odin_online_known = st.known;
      odin_mqtt_.age_status_ms = now_ms;
      note_mqtt_stream_(odin_mqtt::StreamId::Status, now_ms);
      if (st.known && !st.online) {
        odin_plan_.plan_stale = true;
        odin_plan_.available = false;
        std::strncpy(mqtt_absorb_reason_, "odin_offline", sizeof(mqtt_absorb_reason_) - 1);
        mqtt_absorb_action_ = 2;
        kick_task_(odin_task_handle_);
      }
    }
    return;
  }
  if (std::strstr(topic, "/commands/setpoint") != nullptr) {
    odin_mqtt::CommandState cmd{};
    if (odin_mqtt::parse_command_hints(payload, &cmd)) {
      odin_mqtt_client_.command = cmd;
      odin_mqtt_.age_commands_ms = now_ms;
      note_mqtt_stream_(odin_mqtt::StreamId::Commands, now_ms);
      process_odin_mqtt_command_(now_ms);
    }
    return;
  }
  if (std::strstr(topic, "/telemetry") != nullptr) {
    odin_mqtt::TelemetryState tel = odin_mqtt_client_.telemetry;
    odin_mqtt::parse_telemetry_defrost(payload, &tel);
    odin_mqtt_client_.telemetry = tel;
    odin_mqtt_.age_telemetry_ms = now_ms;
    note_mqtt_stream_(odin_mqtt::StreamId::Telemetry, now_ms);
    process_odin_mqtt_telemetry_(now_ms);
    return;
  }
  if (std::strstr(topic, "odin/solver/result") != nullptr) {
    odin_mqtt_.age_result_ms = now_ms;
    note_mqtt_stream_(odin_mqtt::StreamId::SolverResult, now_ms);
  }
}

void LuneTouchCoordinator::ensure_odin_mqtt_client_() {
#if defined(USE_ESP32)
  // esp-mqtt copies broker, credentials and (via on-connect subscribe) topics
  // when the client starts. Rebuild it whenever the saved settings differ from
  // what it was started with, or MQTT was switched off — otherwise a rotated
  // password or new broker only takes effect after a reboot.
  static std::string g_started_with;
  std::string wanted;
  if (odin_mqtt_.enabled && odin_mqtt_.host[0] != '\0') {
    char port[8];
    std::snprintf(port, sizeof(port), "%u", static_cast<unsigned>(odin_mqtt_.port));
    wanted.append(odin_mqtt_.host).append(1, '\x1f').append(port).append(1, '\x1f')
        .append(odin_mqtt_.username).append(1, '\x1f')
        .append(odin_mqtt_.password_set ? odin_mqtt_password_ : "").append(1, '\x1f')
        .append(odin_mqtt_.topic_prefix).append(1, '\x1f').append(odin_mqtt_.hp_id);
  }
  if (g_odin_mqtt_handle != nullptr && wanted != g_started_with) {
    ESP_LOGI(MQTT_TAG, wanted.empty() ? "MQTT disabled; stopping client"
                                      : "MQTT settings changed; restarting client");
    esp_mqtt_client_stop(g_odin_mqtt_handle);
    esp_mqtt_client_destroy(g_odin_mqtt_handle);
    g_odin_mqtt_handle = nullptr;
    on_odin_mqtt_disconnected_(esphome::millis());
    odin_mqtt_client_.reconnect_backoff_ms = 1000;
  }
  if (wanted.empty() || g_odin_mqtt_handle != nullptr)
    return;
  g_started_with = wanted;
  esp_mqtt_client_config_t cfg{};
  char uri[128];
  std::snprintf(uri, sizeof(uri), "mqtt://%s:%u", odin_mqtt_.host,
                static_cast<unsigned>(odin_mqtt_.port == 0 ? 1883 : odin_mqtt_.port));
  cfg.broker.address.uri = uri;
  if (odin_mqtt_.username[0] != '\0')
    cfg.credentials.username = odin_mqtt_.username;
  if (odin_mqtt_.password_set && odin_mqtt_password_[0] != '\0')
    cfg.credentials.authentication.password = odin_mqtt_password_;
  // Subscribe only — never set a will publish or outbound topics.
  g_odin_mqtt_handle = esp_mqtt_client_init(&cfg);
  if (g_odin_mqtt_handle == nullptr) {
    on_odin_mqtt_error_("mqtt_init_failed", esphome::millis());
    return;
  }
  esp_mqtt_client_register_event(g_odin_mqtt_handle, MQTT_EVENT_ANY, odin_mqtt_event_handler, this);
  esp_mqtt_client_start(g_odin_mqtt_handle);
#endif
}

}  // namespace lune_touch_coordinator
}  // namespace esphome
