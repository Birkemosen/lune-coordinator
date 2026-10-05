#include "plan_source.h"
#include "odin_mqtt.h"
#include "odin_physics.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

using namespace esphome::lune_touch_coordinator;

static std::string read_file(const char *path) {
  std::ifstream in(path);
  assert(in.good());
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

static void test_decision_reason_matrix() {
  using plan_source::DecisionReason;
  assert(plan_source::decision_reason_from_string("thermal_buffer") == DecisionReason::ThermalBuffer);
  assert(plan_source::decision_reason_from_string("thermal_buffer_solar") ==
         DecisionReason::ThermalBufferSolar);
  assert(plan_source::decision_reason_from_string("energy_cost") == DecisionReason::EnergyCost);
  assert(plan_source::decision_reason_from_string("modulation") == DecisionReason::Modulation);
  assert(plan_source::decision_reason_from_string("comfort_hard") == DecisionReason::ComfortHard);
  assert(plan_source::decision_reason_from_string("comfort_soft") == DecisionReason::ComfortSoft);
  assert(plan_source::decision_reason_from_string("blocked_price") == DecisionReason::Blocked);
  assert(plan_source::decision_reason_from_string("unknown") == DecisionReason::Unknown);
  assert(plan_source::decision_reason_from_string(nullptr) == DecisionReason::Unknown);
  assert(plan_source::decision_reason_from_string("not_a_real_reason") == DecisionReason::Unknown);

  assert(plan_source::may_arm_absorb_v2(DecisionReason::ThermalBuffer, true));
  assert(plan_source::may_arm_absorb_v2(DecisionReason::ThermalBufferSolar, false));
  assert(plan_source::may_arm_absorb_v2(DecisionReason::EnergyCost, true));
  assert(!plan_source::may_arm_absorb_v2(DecisionReason::EnergyCost, false));
  assert(plan_source::may_arm_absorb_v2(DecisionReason::Modulation, true));
  assert(!plan_source::may_arm_absorb_v2(DecisionReason::ComfortHard, true));
  assert(!plan_source::may_arm_absorb_v2(DecisionReason::ComfortSoft, true));
  assert(!plan_source::may_arm_absorb_v2(DecisionReason::Blocked, true));
  assert(!plan_source::may_arm_absorb_v2(DecisionReason::Unknown, true));
}

static void test_unified_may_arm() {
  plan_source::PlanView v1{};
  v1.source = plan_source::SourceId::AsgardDashboardV1;
  v1.heat_production[0] = 4.8f;
  v1.operation_mode[0] = 2;
  assert(plan_source::may_arm_absorb(v1, 0, true));
  v1.operation_mode[0] = 1;  // DHW
  assert(!plan_source::may_arm_absorb(v1, 0, true));

  plan_source::PlanView v2{};
  v2.source = plan_source::SourceId::OdinForecastV2;
  v2.has_decision_reason = true;
  v2.operation_mode[0] = 2;
  v2.decision_reason[0] = plan_source::DecisionReason::ThermalBuffer;
  assert(plan_source::may_arm_absorb(v2, 0, true));
  v2.decision_reason[0] = plan_source::DecisionReason::ComfortHard;
  assert(!plan_source::may_arm_absorb(v2, 0, true));
  v2.decision_reason[0] = plan_source::DecisionReason::Unknown;
  assert(!plan_source::may_arm_absorb(v2, 0, true));
  v2.decision_reason[0] = plan_source::DecisionReason::ThermalBuffer;
  v2.operation_mode[0] = 1;
  assert(!plan_source::may_arm_absorb(v2, 0, true));  // DHW still blocks
}

static void test_v1_fixtures() {
  const std::string dash = read_file("tests/fixtures/odin_dashboard_v1.json");
  assert(dash.find("\"success\": true") != std::string::npos ||
         dash.find("\"success\":true") != std::string::npos);
  assert(dash.find("today_start_index") != std::string::npos);
  assert(dash.find("heat_production") != std::string::npos);
  assert(dash.find("operation_mode") != std::string::npos);
  assert(dash.find("actual_prod") != std::string::npos);
  assert(dash.find("last_run") != std::string::npos);
  assert(dash.find("execution_ms") != std::string::npos);

  const std::string debug = read_file("tests/fixtures/odin_debug_v1.json");
  assert(debug.find("used_heat_loss") != std::string::npos);
  assert(debug.find("used_thermal_mass") != std::string::npos);
  assert(debug.find("hl_tm_product") != std::string::npos);

  // Fixture indexing: today_start_index=24, current_hour=13 → idx 37
  uint8_t idx = 0;
  assert(odin_plan::resolve_idx_now(24, 13, &idx) && idx == 37);
}

static void test_v2_gate_disabled() {
  assert(!plan_source::forecast_v2_runtime_enabled());
  plan_source::PlanView view{};
  view.success = true;
  view.has_decision_reason = true;
  view.current_hour = 13;
  view.today_start_index = 24;
  view.current_index = 37;
  for (size_t i = 0; i < odin_plan::HORIZON_HOURS; ++i) {
    view.sched_min[i] = 20.0f;
    view.sched_base[i] = 21.0f;
    view.sched_max[i] = 22.5f;
    view.expected_begin_temp[i] = 21.0f;
    view.expected_end_temp[i] = 21.0f;
    view.prices[i] = 0.2f;
    view.heat_production[i] = 0.0f;
    view.operation_mode[i] = 0;
  }
  // Fail closed while LUNE_ODIN_FORECAST_V2_ENABLED is 0.
  assert(!plan_source::validate_v2_layout(view));
}

static void test_mqtt_helpers() {
  odin_mqtt::StatusState st{};
  assert(odin_mqtt::parse_status_payload("{\"state\":\"offline\"}", &st) && !st.online);
  assert(odin_mqtt::parse_status_payload("{\"online\":true}", &st) && st.online);

  odin_mqtt::CommandState cmd{};
  assert(odin_mqtt::parse_command_hints(
      "{\"soft_stop\":true,\"mode\":2,\"decision_reason\":\"thermal_buffer\"}", &cmd));
  assert(cmd.soft_stop);
  assert(odin_mqtt::should_disarm_from_command(cmd));

  cmd = {};
  assert(odin_mqtt::parse_command_hints(
      "{\"soft_stop\":false,\"mode\":2,\"decision_reason\":\"thermal_buffer\"}", &cmd));
  assert(cmd.heat_mode);
  assert(odin_mqtt::should_arm_from_command(cmd, true));
  cmd.decision_reason = plan_source::DecisionReason::ComfortHard;
  assert(!odin_mqtt::should_arm_from_command(cmd, true));

  odin_mqtt::TelemetryState tel{};
  assert(odin_mqtt::parse_telemetry_defrost("{\"defrost\":true}", &tel) && tel.defrost_active);
  assert(odin_mqtt::arm_ttl_s(1000, 900, 3600) == 100);
  assert(odin_mqtt::arm_ttl_s(1000, 900, 50) == 50);
  assert(odin_mqtt::next_backoff_ms(1000) == 2000);
  assert(odin_mqtt::next_backoff_ms(60000) == 60000);
}

static void test_physics_deviation() {
  assert(odin_physics::deviation_flag(4.0f, 1.0f));
  assert(!odin_physics::deviation_flag(1.5f, 1.0f));
  assert(!odin_physics::deviation_flag(NAN, 1.0f));
}

static void test_v2_fixtures_placeholder() {
  const std::string forecast = read_file("tests/fixtures/odin_forecast_v2.json");
  assert(forecast.find("\"disabled\": true") != std::string::npos ||
         forecast.find("\"disabled\":true") != std::string::npos);
  const std::string mqtt = read_file("tests/fixtures/odin_mqtt_v2_samples.json");
  assert(mqtt.find("retained_messages_proven") != std::string::npos);
  const std::string phys = read_file("tests/fixtures/odin_physics_v2.json");
  assert(phys.find("zone2_active") != std::string::npos);
}

int main() {
  test_decision_reason_matrix();
  test_unified_may_arm();
  test_v1_fixtures();
  test_v2_gate_disabled();
  test_v2_fixtures_placeholder();
  test_mqtt_helpers();
  test_physics_deviation();
  std::puts("Odin plan_source / mqtt / physics tests passed.");
  return 0;
}
