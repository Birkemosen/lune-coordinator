#pragma once

#include "coordinator_model.h"
#include "flow_trim.h"
#include "house_demand.h"
#include "odin_comfort.h"
#include "odin_mqtt.h"
#include "odin_physics.h"
#include "odin_plan.h"
#include "plan_source.h"
#include "esphome/components/time/real_time_clock.h"
#include "esphome/core/component.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <string>

namespace esphome {
namespace lune_touch_coordinator {

struct ForecastHourState {
  int64_t timestamp_s{0};
  float temp_c{0.0f};
  float wind_speed_ms{0.0f};
  float wind_dir_deg{0.0f};
  float shortwave_wm2{0.0f};
  float precipitation_mm{0.0f};
  float cloud_cover_pct{0.0f};
};

struct ForecastDecisionState {
  char room_id[32]{};
  char room_name[48]{};
  uint8_t node_index{0};
  uint8_t zone_index{0};
  float comfort_setpoint_c{21.0f};
  uint8_t priority{1};
  float offset_c{0.0f};
  float peak_load{0.0f};
  int8_t peak_in_h{-1};
  uint8_t configured_thermal_lead_h{4};
  uint8_t learned_thermal_lead_h{0};
  uint8_t active_thermal_lead_h{4};
  float timing_scale{0.0f};
  float heuristic_offset_c{0.0f};
  float min_temp_c{0.0f};
  bool horizon_ok{false};
  bool used_horizon{false};
  bool used_thermal_sizing{false};
  uint8_t preload_start_h{0};
  uint8_t preload_end_h{0};
  bool active{false};
  bool odin_timing_bias{false};
  // Slab charge (slab_charge.h): capacity shortfall ahead → store heat first.
  bool charge_episode{false};
  bool charge_now{false};
  bool charge_insufficient{false};
  float charge_store_c{0.0f};
  float charge_deficit_kwh{0.0f};
  float floor_capacity_w{0.0f};
  float peak_loss_w{0.0f};
  int16_t charge_start_in_h{-1};
  int16_t charge_episode_in_h{-1};
  int16_t charge_end_in_h{-1};
};

struct ForecastDispatchSummary {
  uint8_t active{0};
  uint8_t sent{0};
  uint8_t skipped{0};
  uint8_t failed{0};
  uint8_t blocked_stale{0};
  uint8_t blocked_unreachable{0};
  uint8_t blocked_untrusted{0};
};

struct NodeTelemetryState {
  float flow_c{0.0f};
  float return_c{0.0f};
  float avg_valve_pct{0.0f};
  float motor_current_ma{0.0f};
  uint8_t active_zones{0};
  bool has_flow{false};
  bool has_return{false};
  bool has_avg_valve{false};
  bool has_motor_current{false};
  bool drivers_enabled{false};
  bool has_drivers_enabled{false};
  bool motor_fault{false};
  bool has_motor_fault{false};
  // Runtime-only: V6 overview physics_contract (0 = legacy, refuse physics writes).
  uint8_t physics_contract{0};
};

struct EventRecord {
  uint32_t ts_ms{0};
  char level[8]{};
  char source[20]{};
  char message[96]{};
};

struct HeatSourceState {
  // Adapter family. Only "asgard" is implemented today; unknown/empty loads as asgard.
  char type[24]{"asgard"};
  bool enabled{false};
  char host[64]{};
  uint16_t port{80};
  // Asgard entity name (current ESPHome web server addresses entities by name;
  // the old object ids such as temperature_feedback_z1 answer 404).
  char weighted_temperature_variable[64]{"Virtual Thermostat Input z1"};
  // Empty → ESPHome number entity URLs. Placeholders: {host} {port} {entity} {value}.
  char write_url_template[192]{};
  char read_url_template[192]{};
  uint16_t push_interval_s{60};
  // Comfort target → Asgard VT climate (Touch-managed when target_sync_enabled).
  // Default object id matches ESPHome climate "Virtual Thermostat" (not Input z1).
  char climate_entity[64]{"Virtual Thermostat z1"};
  bool target_sync_enabled{true};
  float last_target_written_c{NAN};
  float last_target_confirmed_c{NAN};
  uint32_t last_target_write_ms{0};
  uint32_t target_failure_streak{0};
  // Setpoint Bias trim.
  char bias_entity[64]{};
  char dhw_entity[64]{};
  char legionella_entity[64]{};
  char defrost_entity[64]{};
  // Operator-entered Asgard comfort target (Touch-local diagnostic).
  float declared_target_c{NAN};
  bool has_declared_target{false};
  float declared_target_drift_c{NAN};
  bool declared_target_drift_warn{false};
  bool has_last_push{false};
  char last_status[12]{"unreachable"};
  float last_requested_value_c{NAN};
  float last_confirmed_value_c{NAN};
  int last_http_status{0};
  uint32_t last_write_ms{0};
  uint32_t last_confirmed_ms{0};
  uint32_t last_healthy_physical_ms{0};
  uint32_t failure_count{0};
  uint32_t failure_streak{0};
  // P0-2: alarm when confirmation missing for ≥2 cycles (Overview hard gate).
  bool push_alarm{false};
  char last_error[96]{};
  // Trim diagnostics (mirrored from flow_trim::Controller).
  float trim_bias_c{0.0f};
  float trim_target_bias_c{0.0f};
  bool trim_frozen{false};
  char trim_freeze_reason[24]{"none"};
  uint32_t trim_last_step_ms{0};
  float last_bias_confirmed_c{NAN};
  bool dhw_active{false};
  bool legionella_active{false};
  bool defrost_active{false};
  bool freeze_inputs_known{false};
  // Heat-pump telemetry from Asgard /dashboard/state (read-only, display).
  float hp_feed_c{NAN};
  float hp_return_c{NAN};
  float hp_outside_c{NAN};
  float hp_flow_target_c{NAN};
  float hp_compressor_hz{NAN};
  bool hp_compressor_on{false};
  int8_t hp_operation_mode{-1};
  uint32_t hp_telemetry_ms{0};
};

// Generic heat-source levers (type generic_http). Kept out of HeatSourceState,
// which is copied onto task stacks. Placeholders as for the URL templates;
// {value} = target °C, 1/0 heat request, or curve offset °C.
struct GenericLeverState {
  char target_url_template[192]{};
  char heat_request_url_template[192]{};
  char curve_offset_url_template[192]{};
  float curve_gain{2.0f};
  float curve_max_offset_c{5.0f};
  float last_lever_target_c{NAN};
  int8_t last_lever_request{-1};
  float last_lever_curve_c{NAN};
  uint32_t last_lever_write_ms{0};
  char lever_status[24]{"idle"};
};

// Odin 2.0 comfort-schedule control and Asgard↔Odin link health.
struct OdinControlState {
  // Opt-in: Touch lifts Odin's comfort schedule (sched_ui) temporarily.
  bool enabled{false};
  float max_lift_c{1.5f};
  // Asgard binary sensor that is ON while Asgard forwards to Odin.
  char forwarder_entity[64]{"ODIN Forwarder Active"};
  // Link health (read-only, also when control is disabled).
  bool odin_reachable{false};
  bool forwarder_known{false};
  bool forwarder_active{false};
  // Asgard "ODIN Forwarder Takeover": ON while Odin's commands drive the pump;
  // OFF = Asgard's Auto-Adaptive regulates itself (then the VT target counts).
  char takeover_entity[64]{"ODIN Forwarder Takeover"};
  bool takeover_known{false};
  bool takeover_active{false};
  int32_t telemetry_age_s{-1};
  /// Age of the pump telemetry on MQTT (Asgard → broker), -1 when not subscribed.
  int32_t mqtt_telemetry_age_s{-1};
  float odin_room_c{NAN};
  char link_status[24]{"unknown"};
  uint32_t link_bad_since_ms{0};
  bool link_alarm{false};
  // Physics Odin plans with (from /api/debug).
  float thermal_mass_kwh_per_k{NAN};
  float heat_loss_kw_per_k{NAN};
  uint32_t last_physics_ms{0};
  // Control.
  lune_touch_odin::Lift wanted{};
  lune_touch_odin::Lift applied{};
  float wanted_energy_kwh{0.0f};
  char reason[24]{"none"};
  char last_action[16]{"none"};
  char status[24]{"disabled"};
  uint32_t last_cycle_ms{0};
  uint32_t last_write_ms{0};
  int last_http_status{0};
  char last_error[96]{};
};

struct CirculationPumpState {
  // Satellite ESPHome Alpha2 Go node. Read-only hydronic telemetry; never an actuator.
  bool enabled{false};
  char host[64]{};
  uint16_t port{80};
  uint16_t refresh_interval_s{60};
  uint16_t refresh_interval_idle_s{120};
  uint16_t refresh_interval_active_s{60};
  char flow_entity[48]{"pump_flow"};
  char head_entity[48]{"pump_head_pressure"};
  char power_entity[48]{"pump_power"};
  bool reachable{false};
  bool has_flow{false};
  bool has_head{false};
  bool has_power{false};
  float flow_m3h{NAN};
  float head_m{NAN};
  float power_w{NAN};
  float thermal_kw{NAN};
  bool has_thermal_kw{false};
  char thermal_kw_source[16]{"none"};
  float hydraulic_authority{NAN};
  bool has_hydraulic_authority{false};
  uint8_t consecutive_failures{0};
  uint32_t last_fetch_ms{0};
  uint32_t last_success_ms{0};
  int last_http_status{0};
  char last_error[96]{};
};

struct OdinPlanState {
  // Optional ODIN plan ingestion (Asgard dashboard v1 and/or Odin forecast v2).
  // Read-only advisory data — does not participate in safety-critical room control
  // or physical aggregation.
  bool enabled{false};
  // plan_source: auto|v1|v2 — v2 runtime gated by LUNE_ODIN_FORECAST_V2_ENABLED.
  char plan_source_mode[8]{"auto"};
  char source_id[24]{"none"};
  char odin_version[16]{};
  // Separate Odin host (2.x). When empty, v1 mirrors Asgard heat-source host/port.
  char odin_host[64]{};
  uint16_t odin_port{80};
  char host[64]{};  // effective fetch host (v1: Asgard; v2: Odin)
  uint16_t port{80};
  uint16_t refresh_interval_s{300};
  bool available{false};
  uint8_t current_hour{0};
  uint8_t current_index{0};
  float current_target_c{NAN};
  float current_min_c{NAN};
  float current_max_c{NAN};
  float current_price{NAN};
  float current_planned_heat_kw{NAN};
  int current_operation_mode_raw{-1};
  char current_decision_reason[32]{"unknown"};
  // B7: feature-flagged absorb-arm client (V6 returns 501 until A5 effect).
  bool absorb_arm_enabled{false};
  // v2: arm on energy_cost/modulation (lower priority); default on.
  bool arm_energy_cost_modulation{true};
  // B7: timing-bias of weather preload toward ODIN heat hours (amount-capped).
  bool preload_timing_bias_enabled{true};
  float preload_bias_offset_cap_c{0.5f};  // max bias contribution on top of weather
  float bias_window_planned_kwh{0.0f};
  float bias_window_delivered_kwh{0.0f};
  bool heat_window_active{false};
  float heat_production_horizon[24]{};
  // Odin's band and expected room temperature for the same 24 h (plan graph).
  float band_min_horizon[24]{};
  float band_max_horizon[24]{};
  float expected_temp_horizon[24]{};
  // Odin's planned electricity per hour (kWh) — the only energy figure for DHW /
  // legionella hours, where heat_production (space heat) is 0.
  float energy_horizon[24]{};
  int operation_mode_horizon[24]{};
  char decision_reason_horizon[24][32]{};
  uint8_t heat_production_count{0};
  uint8_t today_start_index{0};
  uint8_t idx_now{0};
  // Plan vs reality for hours ≤ idx_now (history only).
  float plan_heat_past[24]{};
  float actual_prod_past[24]{};  // actual_heat (v1 actual_prod / v2 actual_heat_prod)
  uint8_t past_count{0};
  float last_run_execution_ms{NAN};
  float last_run_evaluated_nodes{NAN};
  float last_run_total_cost{NAN};
  bool plan_stale{false};
  uint32_t last_fetch_ms{0};
  uint32_t last_absorb_arm_ms{0};
  uint32_t last_absorb_disarm_ms{0};
  bool absorb_armed{false};
  char absorb_arm_reason[48]{};
  int last_http_status{0};
  char status[16]{"unavailable"};
  char last_error[96]{};
};

struct OdinMqttState {
  bool enabled{false};
  char host[64]{};
  uint16_t port{1883};
  char username[64]{};
  bool password_set{false};
  char topic_prefix[64]{};
  char hp_id[32]{};
  bool connected{false};
  bool odin_online{false};
  bool odin_online_known{false};
  uint32_t age_status_ms{0};
  uint32_t age_result_ms{0};
  uint32_t age_commands_ms{0};
  uint32_t age_telemetry_ms{0};
  char last_error[96]{};
  bool defrost_active{false};
  bool defrost_known{false};
  bool soft_stop{false};
  float flow_target_c{NAN};
  uint32_t valid_until_unix{0};
  float telemetry_thermal_w{NAN};
  float telemetry_flow_rate{NAN};
  float telemetry_return_temp_c{NAN};
  float telemetry_hp_feed_temp_c{NAN};
  bool http_fallback_active{false};
};

struct OdinPhysicsState {
  bool available{false};
  float heat_loss_kw_per_k{NAN};
  float tau_z1_h{NAN};
  float tau_z2_h{NAN};
  float passive_solar_z1{NAN};
  float passive_solar_z2{NAN};
  bool zone2_active{false};
  bool zone2_warning{false};
  /// False while Odin still plans with generic defaults (no heating data yet).
  bool learned{false};
  bool deviation_hl{false};
  bool deviation_tau{false};
  uint32_t last_fetch_ms{0};
  int last_http_status{0};
  char last_error[96]{};
};

class LuneTouchCoordinator : public esphome::Component {
 public:
  float get_setup_priority() const override { return esphome::setup_priority::AFTER_WIFI; }
  void setup() override;
  void loop() override;
  void dump_config() override;

  void set_node_stale_after_ms(uint32_t stale_after_ms) {
    node_stale_after_ms_ = stale_after_ms;
  }
  void set_time(esphome::time::RealTimeClock *time) { time_ = time; }

  void write_overview_json(char *buffer, size_t capacity) const;
  void write_nodes_json(char *buffer, size_t capacity) const;
  void write_node_scan_json(char *buffer, size_t capacity) const;
  void write_zones_json(char *buffer, size_t capacity) const;
  void write_rooms_json(char *buffer, size_t capacity) const;
  void write_zone_comfort_chart_json(const char *room_id, char *buffer, size_t capacity) const;
  void write_strategy_json(char *buffer, size_t capacity) const;
  void write_forecast_json(char *buffer, size_t capacity) const;
  void write_commands_json(char *buffer, size_t capacity) const;
  void write_diagnostics_json(char *buffer, size_t capacity) const;
  void write_settings_json(char *buffer, size_t capacity) const;
  void write_heat_source_json(char *buffer, size_t capacity) const;
  void write_circulation_json(char *buffer, size_t capacity) const;
  void write_events_json(char *buffer, size_t capacity) const;
  std::string house_summary_text() const;
  std::string zone_line_text(uint8_t row) const;
  std::string zone_line_meta_text(uint8_t row) const;
  std::string forecast_summary_text() const;
  std::string forecast_decision_text(uint8_t row) const;
  std::string command_summary_text() const;
  std::string heating_summary_text() const;
  std::string alarm_summary_text() const;
  std::string display_refresh_token() const;
  std::string display_header_text() const;
  bool display_manifold_visible(uint8_t node_index) const;
  uint8_t display_zone_mask(uint8_t node_index) const;
  uint8_t display_node_zone_count(uint8_t node_index) const;
  bool display_zone_slot_used(uint8_t node_index, uint8_t zone_index) const;
  uint8_t display_manifold_page() const;
  uint8_t display_manifold_page_count() const;
  bool display_set_manifold_page(uint8_t page);
  std::string display_manifold_text(uint8_t node_index) const;
  std::string display_zone_cell_text(uint8_t node_index, uint8_t zone_index) const;
  std::string display_zone_name_text(uint8_t node_index, uint8_t zone_index) const;
  std::string display_zone_temperature_text(uint8_t node_index, uint8_t zone_index) const;
  std::string display_zone_setpoint_text(uint8_t node_index, uint8_t zone_index) const;
  uint8_t display_zone_valve_pct(uint8_t node_index, uint8_t zone_index) const;
  uint32_t display_zone_status_color(uint8_t node_index, uint8_t zone_index) const;
  std::string display_zone_status_icon(uint8_t node_index, uint8_t zone_index) const;
  std::string display_controller_flow_text(uint8_t node_index) const;
  std::string display_controller_return_text(uint8_t node_index) const;
  bool display_circulation_visible() const;
  /// Status dots on the wall display: 0 not set up, 1 ok, 2 warning, 3 fault.
  uint8_t display_asgard_status() const;
  uint8_t display_odin_status() const;
  /// Heat-pump flow → return from Asgard ("45,0° → 41,0°"), empty when unknown.
  std::string display_heat_pump_temps_text() const;
  /// "Asgard" for the Asgard adapter, otherwise the generic heat-source name.
  std::string display_heat_source_name() const;
  std::string display_circulation_flow_text() const;
  std::string display_circulation_head_text() const;
  std::string display_circulation_power_text() const;
  std::string display_circulation_status_text() const;
  uint32_t display_circulation_status_color() const;
  std::string display_heat_source_text() const;
  std::string display_forecast_text() const;
  uint8_t display_forecast_visible_hours() const;
  std::string display_forecast_hour_label(uint8_t index) const;
  std::string display_forecast_hour_temp(uint8_t index) const;
  std::string display_forecast_hour_wind(uint8_t index) const;
  uint8_t display_forecast_hour_sky(uint8_t index) const;
  uint32_t display_forecast_hour_color(uint8_t index) const;
  std::string display_forecast_age_text() const;
  uint32_t display_forecast_age_color() const;
  bool display_problem_visible() const;
  std::string display_problem_text() const;
  std::string display_selected_room_text() const;
  std::string display_selected_room_name() const;
  std::string display_zone_detail_meta_text() const;
  std::string display_wifi_text() const;
  uint32_t display_wifi_color() const;
  uint8_t display_wifi_bars() const;
  uint8_t display_enabled_zone_count() const;
  uint8_t display_calling_zone_count() const;
  // Overview zone tile: 0=empty, 1=off, 2=idle, 3=calling, 4=fault.
  uint8_t display_zone_overview_state(uint8_t node_index, uint8_t zone_index) const;
  uint8_t display_visible_node_count() const;
  uint8_t display_selected_zone_ordinal() const;
  bool display_step_selected_zone(int8_t delta);
  bool display_select_controller(uint8_t node_index);
  bool display_select_zone(uint8_t node_index, uint8_t zone_index);
  bool display_select_room(uint8_t row);
  bool display_adjust_primary_target(float delta_c);
  bool display_boost_primary_room();
  bool display_away_primary_room();
  float display_house_target_c() const;
  float display_house_temperature_c() const;
  bool display_adjust_house_target(float delta_c);
  uint8_t display_active_page() const;
  bool display_set_active_page(uint8_t page);
  uint8_t display_return_page() const;
  bool display_heat_source_enabled() const;
  bool display_set_heat_source_enabled(bool enabled);
  bool display_weather_compensation_enabled() const;
  bool display_set_weather_compensation_enabled(bool enabled);
  std::string display_idle_timeout_text() const;
  bool display_cycle_idle_timeout();
  std::string display_system_fact_text(uint8_t index) const;
  bool display_override_active() const;
  std::string display_override_text() const;
  bool display_set_selected_target(float value_c);
  bool display_reset_selected_motor_fault();
  uint8_t display_selected_node_index() const { return display_selected_node_index_; }
  uint8_t display_selected_zone_index() const { return display_selected_zone_index_; }

  enum DisplayPage : uint8_t {
    DISPLAY_PAGE_HOME = 0,
    DISPLAY_PAGE_ZONES = 1,
    DISPLAY_PAGE_ZONE_DETAIL = 2,
    DISPLAY_PAGE_SYSTEM = 3,
    DISPLAY_PAGE_SERVICE = 4,
  };

  bool add_node(const char *node_id, const char *hostname, const char *fallback_ip,
                const char *pairing_fingerprint, char *response, size_t capacity);
  bool scan_registered_nodes(char *response, size_t capacity);
  bool request_lan_scan(char *response, size_t capacity);
  bool scan_node_candidate(const char *hostname, const char *fallback_ip,
                           char *response, size_t capacity);
  bool set_node_trust(const char *node_id, ::lune_touch::NodeTrust trust,
                      const char *confirmation, char *response, size_t capacity);
  bool set_node_profile(const char *node_id, const char *name, char *response, size_t capacity);
  bool remove_node(const char *node_id, const char *confirmation, char *response, size_t capacity);
  bool reset_registry(const char *confirmation, char *response, size_t capacity);
  bool bind_room(const char *room_id, const char *room_name, size_t node_index, size_t zone_index,
                 char *response, size_t capacity);
  bool set_zone_comfort(const char *room_id, float comfort_setpoint_c, uint8_t priority,
                        float comfort_bias_c, char *response, size_t capacity);
  bool set_zone_schedule(const char *room_id, bool enabled, uint8_t day_mask,
                         uint16_t start_min, uint16_t end_min, float setpoint_c,
                         char *response, size_t capacity);
  bool set_zone_forecast_profile(const char *room_id, uint8_t exterior_walls,
                                 float wind_exposure, float solar_gain,
                                 uint8_t thermal_lead_h, float max_offset_c,
                                 char *response, size_t capacity);
  bool update_room_atomically(const char *room_id, const ::lune_touch::RoomUpdate &update,
                              char *response, size_t capacity);
  bool create_room(const char *room_id, const char *name, char *response, size_t capacity);
  bool move_group_to_room(const char *node_id, uint8_t zone_1based, const char *room_id,
                          char *response, size_t capacity);
  bool sync_room_ble_sensor(const char *room_id, char *response, size_t capacity);
  bool set_room_sensor(const char *room_id, const char *node_id, uint8_t zone_1based,
                       char *response, size_t capacity);
  bool delete_room_if_empty(const char *room_id, char *response, size_t capacity);
  bool queue_setpoint_command(const char *room_id, float requested_offset_c, uint32_t ttl_s,
                              const char *reason, char *response, size_t capacity);
  bool request_motor_action(const char *room_id, const char *action, const char *confirmation,
                            char *response, size_t capacity);
  bool set_forecast_location(float latitude, float longitude, const char *mode,
                             char *response, size_t capacity);
  bool set_weather_settings(float max_boost_c, char *response, size_t capacity);
  bool request_forecast_fetch(char *response, size_t capacity);
  bool estimate_forecast_location(char *response, size_t capacity);
  bool set_heat_source_settings(bool has_enabled, bool enabled, const char *host, uint16_t port,
                                const char *weighted_temperature_variable, uint16_t push_interval_s,
                                char *response, size_t capacity, bool has_write_url = false,
                                const char *write_url_template = nullptr, bool has_read_url = false,
                                const char *read_url_template = nullptr,
                                bool has_declared_target = false, float declared_target_c = NAN,
                                const char *climate_entity = nullptr, bool has_target_sync = false,
                                bool target_sync_enabled = false, const char *bias_entity = nullptr,
                                const char *dhw_entity = nullptr, const char *legionella_entity = nullptr,
                                const char *defrost_entity = nullptr, const char *trim_mode = nullptr,
                                const char *v6_control_mode = nullptr,
                                const char *house_weighting = nullptr,
                                const char *type = nullptr, bool has_odin_plan = false,
                                bool odin_plan_enabled = false, bool has_absorb_arm = false,
                                bool absorb_arm_enabled = false,
                                bool has_arm_energy_cost_modulation = false,
                                bool arm_energy_cost_modulation = true,
                                const char *plan_source_mode = nullptr,
                                const char *odin_host = nullptr, uint16_t odin_port = 0,
                                bool has_mqtt = false, bool mqtt_enabled = false,
                                const char *mqtt_host = nullptr, uint16_t mqtt_port = 0,
                                const char *mqtt_username = nullptr,
                                const char *mqtt_password = nullptr,
                                const char *mqtt_topic_prefix = nullptr,
                                const char *mqtt_hp_id = nullptr);
  void write_odin_mqtt_json(char *buffer, size_t capacity) const;
  void write_odin_physics_json(char *buffer, size_t capacity) const;
  void write_heat_source_control_json(char *buffer, size_t capacity) const;
  /// Touch's heating plan for the next 24 h (dashboard plan graph).
  void write_plan_json(char *buffer, size_t capacity) const;
  /// Odin comfort control + generic levers. Null strings / NAN = unchanged.
  bool set_heat_source_control(bool has_odin_enabled, bool odin_enabled, float odin_max_lift_c,
                               const char *forwarder_entity, const char *target_url_template,
                               const char *heat_request_url_template,
                               const char *curve_offset_url_template, float curve_gain,
                               float curve_max_offset_c, char *response, size_t capacity);
  bool request_heat_source_push(char *response, size_t capacity);
  bool request_heat_source_test_read(char *response, size_t capacity);
  bool request_heat_source_test_push(char *response, size_t capacity);
  bool set_circulation_settings(bool has_enabled, bool enabled, const char *host, uint16_t port,
                                const char *flow_entity, const char *head_entity,
                                const char *power_entity, char *response, size_t capacity);
  bool request_circulation_refresh(char *response, size_t capacity);
  bool set_settings(const char *coordinator_name, const char *install_id,
                    const char *site_label, const char *install_mode,
                    bool has_asgard_enabled, bool asgard_enabled,
                    const char *asgard_mode, const char *authority_leader_node_id,
                    const char *authority_coordinator_id, const char *authority_shared_key,
                    bool has_display_idle_timeout, uint32_t display_idle_timeout_s,
                    char *response, size_t capacity);
  bool request_display_wake(char *response, size_t capacity);
  bool consume_display_wake_request();
  /// Display idle diagnostics: called from the LVGL 50 ms interval.
  void note_display_state(bool awake, bool paused, uint32_t inactive_ms);
  uint32_t display_idle_timeout_ms() const;

 protected:
  bool load_registry_();
  void save_registry_();
  void load_ledger_();
  void save_ledger_();
  void load_forecast_settings_();
  void save_forecast_settings_(float latitude, float longitude, const char *mode);
  void load_forecast_cache_();
  void save_forecast_cache_();
  void clear_forecast_cache_();
  void load_settings_();
  void save_settings_();
  void ensure_automatic_identity_();
  bool propose_authority_to_node_(size_t node_index, const ::lune_touch::PairedNode &node,
                                  const char *preferred_host, uint32_t now_ms);
  void seed_mock_house_();
  void make_node_id_(const char *hostname, const char *fallback_ip, char *out, size_t out_len) const;
  bool take_state_lock_(uint32_t timeout_ms = 100) const;
  void give_state_lock_() const;
  static void poll_task_func_(void *arg);
  static void forecast_task_func_(void *arg);
  static void odin_task_func_(void *arg);
  static void circulation_task_func_(void *arg);
  static void heat_source_task_func_(void *arg);
  static void lan_scan_task_func_(void *arg);
  void poll_task_();
  void forecast_task_();
  void odin_task_();
  void circulation_task_();
  void heat_source_task_();
  void lan_scan_task_();
  void kick_task_(TaskHandle_t handle) const;
  void start_worker_tasks_();
  void maybe_run_poll_from_loop_(uint32_t now);
  void maybe_run_integrations_from_loop_(uint32_t now);
  void schedule_circulation_fetch_();
  void poll_once_();
  void discover_v6_on_lan_();
  bool probe_v6_overview_(const char *host, uint32_t timeout_ms, char *model, size_t model_len,
                          char *firmware, size_t firmware_len, char *fingerprint,
                          size_t fingerprint_len, char *reported_ip, size_t reported_ip_len);
  bool perform_forecast_fetch_(char *response, size_t capacity);
  bool poll_node_overview_(size_t node_index, const ::lune_touch::PairedNode &node, uint32_t now_ms);
  bool poll_node_zones_(size_t node_index, const ::lune_touch::PairedNode &node, uint32_t now_ms);
  void note_node_poll_success_(size_t node_index, const char *host);
  void note_node_poll_failure_(size_t node_index, const char *reason);
  bool fetch_json_(const char *url, char *body, size_t body_capacity, int *status_code);
  bool fetch_json_(const char *url, char *body, size_t body_capacity, int *status_code,
                   uint32_t timeout_ms);
  bool post_json_(const char *url, const char *payload, char *body, size_t body_capacity,
                  int *status_code, const char *authority_key = nullptr);
  bool ingest_v6_zones_(size_t node_index, const char *body, uint32_t now_ms);
  bool ingest_v6_legacy_state_(size_t node_index, const ::lune_touch::PairedNode &node,
                               const char *body, uint32_t now_ms);
  bool fetch_open_meteo_(float latitude, float longitude, char *error, size_t error_len,
                         uint8_t *hours_count, float *min_temp_c, float *max_wind_ms,
                         float *peak_wind_dir_deg, float *max_solar_wm2,
                         char *provider_timezone, size_t provider_timezone_capacity,
                         ForecastHourState *hours_out, size_t hours_capacity);
  void recompute_forecast_decisions_();
  void apply_preload_outcome_learning_();
  void ensure_preload_learning_defaults_();
  bool load_preload_learning_();
  bool save_preload_learning_();
  ForecastDispatchSummary dispatch_forecast_commands_();
  void append_zone_comfort_chart_json_(char *buffer, size_t capacity, size_t &off,
                                       size_t zone_index) const;
  bool send_v6_setpoint_command_(const ::lune_touch::PairedNode &node, uint8_t zone_index,
                                 const ::lune_touch::CommandRecord &request,
                                 uint32_t ttl_s, ::lune_touch::CommandRecord *result,
                                 const char *preferred_host = nullptr);
  bool send_v6_zone_setpoint_(const ::lune_touch::PairedNode &node, uint8_t zone_index,
                              float setpoint_c);
  bool send_v6_zone_setting_(const ::lune_touch::PairedNode &node, uint8_t zone_index,
                             const char *kind, const char *key, const char *value);
  bool node_supports_physics_writes_(const ::lune_touch::PairedNode &node) const;
  bool send_v6_zone_physics_(const ::lune_touch::PairedNode &node, uint8_t zone_index,
                             uint32_t expected_revision, uint8_t exterior_walls, float area_m2,
                             const char *slab_type, float active_thickness_cm,
                             const char *covering);
  bool send_v6_house_physics_(const ::lune_touch::PairedNode &node, float u_base, float u_wall,
                              float c_struct, uint32_t calibrated_at_epoch_s);
  bool poll_node_groups_(size_t node_index, const ::lune_touch::PairedNode &node);
  void maybe_migrate_walls_to_v6_();
  void maybe_calibrate_house_physics_();
  bool sync_room_ble_mac_(const char *room_id, char *response, size_t capacity);
  bool read_asgard_number_(const HeatSourceState &source, float *value, char *error,
                           size_t error_capacity);
  bool fetch_odin_plan_();
  bool odin_plan_active_() const;
  void sync_odin_plan_from_heat_source_();
  void clear_odin_plan_live_state_();
  bool fetch_circulation_pump_();
  bool push_weighted_temperature_();
  bool sync_comfort_target_();
  bool step_flow_trim_();
  bool poll_asgard_freeze_inputs_();
  bool renew_authority_lease_();
  // Asgard probe helpers. Must run on heat_source_task_ — never on the httpd
  // task (esp_http_client + large URL buffers overflow the ~12 KiB httpd stack).
  enum class HeatSourceProbeKind : uint8_t { NONE = 0, TEST_READ, TEST_PUSH };
  bool queue_heat_source_probe_(HeatSourceProbeKind kind, char *response, size_t capacity);
  bool run_heat_source_test_read_(char *response, size_t capacity);
  bool run_heat_source_test_push_(char *response, size_t capacity);
  void apply_odin_timing_bias_(ForecastDecisionState *decision);
  void maybe_dispatch_absorb_arm_();
  void maybe_dispatch_absorb_disarm_(const char *reason);
  bool send_v6_absorb_arm_(const ::lune_touch::PairedNode &node, uint8_t zone_index, uint32_t ttl_s,
                           ::lune_touch::CommandRecord *result, const char *decision_reason = nullptr);
  bool send_v6_absorb_disarm_(const ::lune_touch::PairedNode &node, uint8_t zone_index,
                              ::lune_touch::CommandRecord *result, const char *reason);
  void note_mqtt_stream_(odin_mqtt::StreamId stream, uint32_t now_ms);
  void process_odin_mqtt_command_(uint32_t now_ms);
  void process_odin_mqtt_telemetry_(uint32_t now_ms);
  bool fetch_odin_physics_();
  bool odin_defrost_blocks_arm_() const;
  void ensure_odin_mqtt_client_();
 public:
  void on_odin_mqtt_connected_(uint32_t now_ms);
  void on_odin_mqtt_disconnected_(uint32_t now_ms);
  void on_odin_mqtt_error_(const char *message, uint32_t now_ms);
  void on_odin_mqtt_data_(const char *topic, const char *payload, uint32_t now_ms);
 private:
  void url_encode_(const char *src, char *out, size_t out_len) const;
  void log_event_(const char *level, const char *source, const char *message);

  static constexpr uint32_t POLL_INTERVAL_MS = 15000;
  static constexpr uint32_t POLL_BOOT_DELAY_MS = 3000;
  static constexpr uint32_t HTTP_TIMEOUT_MS = 2500;
  // Weak IoT Wi-Fi + chunked V6 zones (up to ~8 KiB) needs more than the
  // default GET budget; EAGAIN / INCOMPLETE_DATA dominate otherwise.
  static constexpr uint32_t V6_HTTP_TIMEOUT_MS = 6000;
  static constexpr uint32_t LAN_CONNECT_TIMEOUT_MS = 70;
  static constexpr uint32_t LAN_HTTP_TIMEOUT_MS = 500;
  static constexpr int LAN_CONNECT_BATCH = 8;
  static constexpr size_t MAX_LAN_CANDIDATES = ::lune_touch::MAX_NODES;
  // V6 zones ingest + model updates + NVS registry save peak at ~11.2 KiB
  // (measured 2026-10-04); 12 KiB overflowed. Stack lives in PSRAM.
  static constexpr uint32_t POLL_STACK_SIZE = 20480;
  // HTTPS + ArduinoJson need headroom; stack lives in PSRAM via WithCaps.
  static constexpr uint32_t FORECAST_STACK_SIZE = 16384;
  // Keep large per-call buffers in PSRAM (psram_scratch_), not on these stacks:
  // bigger stacks cost internal RAM that TLS handshakes need.
  static constexpr uint32_t INTEGRATION_STACK_SIZE = 12288;
  static constexpr UBaseType_t POLL_PRIORITY = 2;
  static constexpr UBaseType_t INTEGRATION_PRIORITY = 1;
  // Run beside the ESPHome loop on APP_CPU. Core 0 hosts Wi-Fi/lwIP; pinning
  // the V6 poller there previously left poll_generation stuck at 0 under load.
  static constexpr BaseType_t POLL_CORE = 1;
  static constexpr uint32_t LEARNING_SAVE_INTERVAL_MS = 10UL * 60UL * 1000UL;
  static constexpr uint32_t FORECAST_COMMAND_TTL_S = 4500;
  static constexpr uint32_t FORECAST_COMMAND_DEDUPE_MS = 30UL * 60UL * 1000UL;
  static constexpr uint32_t FORECAST_AUTO_FETCH_INTERVAL_MS = 60UL * 60UL * 1000UL;
  static constexpr uint32_t ODIN_PLAN_STALE_MS = 20UL * 60UL * 1000UL;
  static constexpr uint32_t CIRCULATION_STALE_MS = 15UL * 60UL * 1000UL;
  static constexpr uint32_t FORECAST_CACHE_MAX_AGE_S = 2UL * 60UL * 60UL;
  static constexpr float FORECAST_COMMAND_EPSILON_C = 0.05f;
  static constexpr size_t EVENT_CAPACITY = 64;

  uint32_t node_stale_after_ms_{300000};
  esphome::time::RealTimeClock *time_{nullptr};
  mutable SemaphoreHandle_t state_lock_{nullptr};
  TaskHandle_t poll_task_handle_{nullptr};
  TaskHandle_t forecast_task_handle_{nullptr};
  TaskHandle_t odin_task_handle_{nullptr};
  TaskHandle_t circulation_task_handle_{nullptr};
  TaskHandle_t heat_source_task_handle_{nullptr};
  TaskHandle_t lan_scan_task_handle_{nullptr};
  bool node_refresh_requested_{false};
  bool lan_scan_requested_{false};
  char lan_discovery_[24]{"not_run"};
  uint8_t lan_candidate_count_{0};
  struct LanScanCandidate {
    char id[24]{};
    char hostname[64]{};
    char ip[16]{};
    char model[16]{};
    char firmware[32]{};
    char pairing_fingerprint[24]{};
  };
  LanScanCandidate lan_candidates_[MAX_LAN_CANDIDATES]{};
  uint32_t last_ledger_expire_ms_{0};
  uint32_t boot_id_{0};
  uint32_t last_learning_save_ms_{0};
  uint32_t last_poll_ms_{0};
  // Monotonically increases after each coordinator poll pass.  The dashboard
  // uses this to wait for the asynchronous scan-triggered poll to complete
  // instead of guessing with a fixed sleep.
  uint32_t poll_generation_{0};
  uint32_t poll_success_count_{0};
  uint32_t poll_fail_count_{0};
  bool learning_dirty_{false};
  bool preload_learning_dirty_{false};
  float preload_gain_scale_[::lune_touch::MAX_HOUSE_ZONES]{};
  int8_t preload_lead_bias_h_[::lune_touch::MAX_HOUSE_ZONES]{};
  bool preload_gain_scale_init_{false};
  char last_poll_error_[80]{};
  char node_last_success_host_[::lune_touch::MAX_NODES][64]{};
  char node_last_failure_[::lune_touch::MAX_NODES][80]{};
  // Label reported by the V6 itself (/api/v1/zones device_name/location). Runtime
  // only — not persisted; the UI shows it when the board has no Touch-side name.
  char node_device_name_[::lune_touch::MAX_NODES][48]{};
  // Lease fan-out: every trusted V6 holds the same Touch lease (whole-house
  // authority), not only the leader. 0 = not sent, 1 = granted, 2 = refused.
  uint8_t node_lease_state_[::lune_touch::MAX_NODES]{};
  /// GET /api/v1/summary gate per node: 0 unknown, 1 supported, 2 unsupported
  /// (older V6 firmware; re-probed hourly). Full /overview + /zones only when the
  /// control fingerprint changed or the last full poll is ≥ 60 s old.
  uint8_t node_summary_support_[::lune_touch::MAX_NODES]{};
  uint32_t node_summary_rev_[::lune_touch::MAX_NODES]{};
  bool node_summary_has_rev_[::lune_touch::MAX_NODES]{};
  uint32_t node_summary_retry_ms_[::lune_touch::MAX_NODES]{};
  uint32_t node_last_full_poll_ms_[::lune_touch::MAX_NODES]{};
  uint32_t node_summary_skips_[::lune_touch::MAX_NODES]{};
  /// 1 = unchanged, 0 = changed, -1 = failed / unsupported.
  int poll_node_summary_(size_t node_index, const ::lune_touch::PairedNode &node);
  /// Sends the leader's lease payload to every other trusted node.
  void fan_out_lease_(const char *leader_id, const char *payload, const char *shared_key);
  /// Per-zone absorb arm/disarm for slab charge windows (re-arms every 30 min).
  void dispatch_charge_arms_();
  uint32_t charge_last_arm_ms_[::lune_touch::MAX_NODES][6]{};
  bool charge_armed_[::lune_touch::MAX_NODES][6]{};
  /// Current lift of the Asgard comfort target above the real house target (°C).
  float demand_target_uplift_c_{0.0f};
  /// Where house demand goes: "virtual_thermostat" | "odin_schedule" | "generic" | "none".
  const char *demand_route_{"none"};
  /// Hysteresis/hold on the house demand signal (lagging room / slab charge).
  lune_touch_house::DemandHold demand_hold_{};
  float demand_held_c_{0.0f};
  OdinControlState odin_control_{};
  GenericLeverState generic_levers_{};
  lune_touch_odin::OwnerState odin_owner_{};
  /// Odin 2.0: link health, physics and comfort-schedule lift (odin task).
  void run_odin_control_();
  bool odin_in_control_() const;
  float update_demand_hold_(const ::lune_touch::StrategySnapshot &strategy, float charge_store_c);
  lune_touch_odin::Lift compute_odin_lift_(uint8_t now_hour, float *energy_kwh, const char **reason);
  bool post_odin_json_(const char *path, const char *body, int *status);
  void load_odin_control_();
  void save_odin_control_();
  /// Generic heat source: write target / heat request / curve offset templates.
  bool push_generic_levers_();
  /// Asgard /dashboard/state → heat-pump flow/return/outdoor/compressor.
  bool poll_asgard_telemetry_();
  NodeTelemetryState node_telemetry_[::lune_touch::MAX_NODES]{};
  EventRecord events_[EVENT_CAPACITY]{};
  size_t event_next_{0};
  size_t event_count_{0};
  ::lune_touch::HouseModel model_{};
  ::lune_touch::CommandLedger ledger_{};
  float forecast_latitude_{0.0f};
  float forecast_longitude_{0.0f};
  char forecast_location_mode_[16]{"manual"};
  char coordinator_name_[32]{"Lune Touch"};
  uint16_t display_idle_timeout_s_{60};
  bool display_wake_requested_{false};
  struct DisplayDiag {
    bool known{false};
    bool awake{true};
    bool paused{false};
    uint32_t inactive_ms{0};
    uint32_t activity_resets{0};
    uint32_t last_activity_reset_ms{0};
    uint32_t sleeps{0};
    uint32_t wakes{0};
    uint32_t wake_requests{0};
  } display_diag_{};
  mutable char display_refresh_token_cache_[16]{"initial"};
  uint8_t display_manifold_page_{0};
  uint8_t display_active_page_{DISPLAY_PAGE_HOME};
  uint8_t display_return_page_{DISPLAY_PAGE_HOME};
  uint8_t display_selected_node_index_{0};
  uint8_t display_selected_zone_index_{0};
  char install_id_[32]{"unassigned"};
  char site_label_[48]{"House"};
  char install_mode_[16]{"commissioning"};
  char authority_leader_node_id_[32]{};
  char authority_coordinator_id_[32]{"lune-touch"};
  char authority_shared_key_[64]{};
  bool identity_persistence_needs_sync_{false};
  uint32_t authority_proposal_last_ms_[::lune_touch::MAX_NODES]{};
  char authority_lease_id_[32]{};
  char authority_state_[24]{"no_publisher"};
  char authority_reason_[48]{"boot"};
  uint32_t authority_sequence_{0};
  uint32_t authority_last_renew_ms_{0};
  uint32_t authority_expires_at_ms_{0};
  uint32_t authority_generation_{0};
  float authority_last_fallback_value_c_{NAN};
  float authority_last_asgard_value_c_{NAN};
  uint8_t authority_v6_local_zones_{0};
  uint8_t authority_v6_peer_zones_{0};
  char authority_v6_peer_status_[16]{"unknown"};
  bool authority_smooth_first_write_{false};
  bool asgard_enabled_{true};
  char asgard_mode_[16]{"advisory"};
  HeatSourceState heat_source_{};
  bool heat_source_push_requested_{false};
  HeatSourceProbeKind heat_source_probe_kind_{HeatSourceProbeKind::NONE};
  SemaphoreHandle_t heat_source_probe_done_{nullptr};
  char heat_source_probe_response_[1536]{};
  bool heat_source_probe_ok_{false};
  flow_trim::Controller flow_trim_{};
  char v6_control_mode_[16]{"local"};  // local | heat_pump | normal
  char v6_control_mode_effective_[16]{"local"};
  bool trim_reseed_pending_{false};
  CirculationPumpState circulation_{};
  bool circulation_poll_requested_{false};
  float weather_max_boost_c_{1.5f};
  float weather_max_boost_resume_c_{1.5f};
  bool weather_max_boost_configured_{false};
  bool weather_max_boost_seeded_from_v6_{false};
  uint32_t forecast_last_fetch_ms_{0};
  int64_t forecast_fetch_epoch_s_{0};
  char forecast_provider_timezone_[48]{};
  char forecast_status_[16]{"stale"};
  char forecast_last_error_[96]{};
  mutable char diagnostics_blockers_[768]{};
  bool forecast_fetch_requested_{false};
  bool forecast_boot_refresh_pending_{false};
  bool forecast_cache_restored_{false};
  uint8_t forecast_hours_count_{0};
  ForecastHourState forecast_hours_[72]{};
  float forecast_min_temp_c_{0.0f};
  float forecast_max_wind_ms_{0.0f};
  float forecast_peak_wind_dir_deg_{0.0f};
  float forecast_max_solar_wm2_{0.0f};
  ForecastDecisionState forecast_decisions_[::lune_touch::MAX_HOUSE_ZONES]{};
  size_t forecast_decision_count_{0};
  ForecastDispatchSummary last_forecast_dispatch_{};
  OdinPlanState odin_plan_{};
  OdinMqttState odin_mqtt_{};
  // Password held only in RAM/NVS — never copied into odin_mqtt_ GET JSON.
  char odin_mqtt_password_[64]{};
  OdinPhysicsState odin_physics_{};
  odin_mqtt::ClientState odin_mqtt_client_{};
  // MQTT callbacks run on esp-mqtt's own small task: they only record state and
  // request follow-up work, which the odin task performs (HTTP, plan fetch).
  volatile bool mqtt_refetch_pending_{false};
  volatile uint8_t mqtt_absorb_action_{0};  // 0 none, 1 arm, 2 disarm
  char mqtt_absorb_reason_[24]{};
  void run_mqtt_followups_();
};

}  // namespace lune_touch_coordinator
}  // namespace esphome
