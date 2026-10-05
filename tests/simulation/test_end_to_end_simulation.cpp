#include "authority_lease.h"
#include "coordinator_model.h"
#include "forecast_model.h"
#include "odin_plan.h"

#include <cmath>
#include <cstdio>
#include <cstring>

using namespace lune_touch;

namespace {

struct ScenarioResult {
  const char *authority;
  const char *room_control;
  const char *diagnostic;
};

int failures = 0;

void expect(bool condition, const char *message) {
  if (!condition) {
    std::fprintf(stderr, "FAIL  %s\n", message);
    failures++;
  }
}

void expect_scenario(const char *name, const ScenarioResult &actual,
                     const ScenarioResult &expected) {
  std::printf("SCENARIO %-26s authority=%s room_control=%s diagnostic=%s\n", name,
              actual.authority, actual.room_control, actual.diagnostic);
  const bool matches = std::strcmp(actual.authority, expected.authority) == 0 &&
                       std::strcmp(actual.room_control, expected.room_control) == 0 &&
                       std::strcmp(actual.diagnostic, expected.diagnostic) == 0;
  expect(matches, name);
}

void configure_touch_lease(lv6_authority::Lease *lease, bool degraded = false) {
  lease->configure("house-1", "touch-1");
  lv6_authority::Request request{"house-1", "touch-1", "lease-1", 1, 0, 90000, degraded};
  expect(lease->acquire_or_renew(request, true, 0) == lv6_authority::Result::GRANTED,
         "simulation setup: Touch obtains its V6-A lease");
}

HouseModel two_six_loop_house(float temperature_c = 20.5f) {
  HouseModel model;
  expect(model.upsert_node("v6-a", "v6-a.local", "", "lune-v6", "test", NodeTrust::TRUSTED) == 0,
         "simulation setup: V6-A registered");
  expect(model.upsert_node("v6-b", "v6-b.local", "", "lune-v6", "test", NodeTrust::TRUSTED) == 1,
         "simulation setup: V6-B registered");
  for (size_t node = 0; node < 2; ++node) {
    for (size_t zone = 0; zone < ZONES_PER_NODE; ++zone) {
      char room_id[24];
      char room_name[24];
      std::snprintf(room_id, sizeof(room_id), "room-%c-%u", node == 0 ? 'a' : 'b',
                    static_cast<unsigned>(zone + 1));
      std::snprintf(room_name, sizeof(room_name), "Room %c%u", node == 0 ? 'A' : 'B',
                    static_cast<unsigned>(zone + 1));
      expect(model.bind_zone(room_id, room_name, node, zone), "simulation setup: physical loop bound");
      expect(model.set_room_geometry(room_id, 10.0f, 10.0f, true),
             "simulation setup: room contributes physical area");
      expect(model.update_zone_live_by_binding(node, zone, temperature_c, true, 21.0f, true,
                                               "heat", true, 1000, 50.0f, true),
             "simulation setup: fresh loop telemetry");
    }
  }
  return model;
}

ScenarioResult state_result(lv6_authority::Lease *lease, uint32_t now_ms,
                            const char *room_control, const char *diagnostic) {
  return {lv6_authority::state_name(lease->snapshot(now_ms).state), room_control, diagnostic};
}

void test_touch_reboot() {
  lv6_authority::Lease lease;
  configure_touch_lease(&lease);
  expect(lease.activate_fallback_if_due(120000, true),
         "Touch reboot: V6-A fallback follows expiry plus guard");
  expect_scenario("Touch reboot", state_result(&lease, 120000, "local_safe", "touch_lease_expired"),
                  {"v6_fallback_active", "local_safe", "touch_lease_expired"});
}

void test_v6_a_reboot() {
  lv6_authority::Lease lease;
  configure_touch_lease(&lease);
  lease.reset_after_boot();
  expect_scenario("V6-A reboot", state_result(&lease, 1, "local_safe", "v6_a_reboot_no_lease"),
                  {"no_publisher", "local_safe", "v6_a_reboot_no_lease"});
}

void test_v6_b_disconnect() {
  HouseModel model = two_six_loop_house();
  expect(model.mark_node_unreachable(1, 2000), "V6-B disconnect: node becomes stale");
  const StrategySnapshot house = model.strategy_snapshot();
  expect(!house.has_physical_temperature && std::strcmp(house.quality, "degraded") == 0 &&
             !house.manifolds_healthy,
         "V6-B disconnect: partial manifold data is not published as physical house temperature");
  lv6_authority::Lease lease;
  configure_touch_lease(&lease);
  expect_scenario("V6-B disconnect", state_result(&lease, 1000, "local_safe", "v6_b_stale"),
                  {"touch_normal", "local_safe", "v6_b_stale"});
}

void test_asgard_unavailable() {
  lv6_authority::Lease lease;
  configure_touch_lease(&lease);
  lv6_authority::Request renewal{"house-1", "touch-1", "lease-1", 2, 1000, 90000, true};
  expect(lease.acquire_or_renew(renewal, true, 1000) == lv6_authority::Result::RENEWED,
         "Asgard unavailable: Touch retains its live lease in degraded state");
  expect_scenario("Asgard unavailable", state_result(&lease, 1000, "touch_distribution", "asgard_unreachable"),
                  {"touch_degraded", "touch_distribution", "asgard_unreachable"});
}

void test_weather_unavailable() {
  const StrategySnapshot before = two_six_loop_house().strategy_snapshot();
  const StrategySnapshot after = two_six_loop_house().strategy_snapshot();
  expect(before.has_physical_temperature && after.has_physical_temperature &&
             std::fabs(before.physical_temperature_c - after.physical_temperature_c) < 0.001f,
         "weather unavailable: missing forecast leaves physical aggregation unchanged");
  lv6_authority::Lease lease;
  configure_touch_lease(&lease);
  expect_scenario("Internet/weather unavailable", state_result(&lease, 1000, "touch_distribution", "forecast_unavailable"),
                  {"touch_normal", "touch_distribution", "forecast_unavailable"});
}

void test_dhw_and_defrost() {
  using namespace esphome::lune_touch_coordinator::odin_plan;
  expect(to_operation_mode(1) == OperationMode::DHW_ON && !may_drive_room_control(to_operation_mode(1)),
         "DHW: plan mode cannot fabricate room demand");
  expect(!defrost_state_is_known() && !may_drive_room_control(to_operation_mode(5)),
         "defrost: undocumented state remains unknown and cannot drive rooms");
  lv6_authority::Lease lease;
  configure_touch_lease(&lease);
  expect_scenario("DHW and defrost", state_result(&lease, 1000, "no_room_command", "defrost_unknown"),
                  {"touch_normal", "no_room_command", "defrost_unknown"});
}

void test_cold_wind_and_solar() {
  hv6fc::ForecastHour storm[2] = {{-4.0f, 20.0f, 0.0f, 0.0f}, {-4.0f, 20.0f, 0.0f, 0.0f}};
  hv6fc::ZoneExposure exposed{hv6fc::WALL_NORTH, 1.0f, 0.0f, 2};
  hv6fc::PreloadParams params{};
  params.enable_horizon = false;  // isolate heuristic preload as a distribution input
  const hv6fc::PreloadDecision wind = hv6fc::compute_zone_preload(storm, 2, 0, exposed, params);
  TargetResolverInput input{};
  input.touch_target_c = 21.0f;
  input.command.has_forecast_offset = true;
  input.command.forecast_offset_c = wind.offset_c;
  const TargetResolution wind_target = HouseModel::resolve_target(input);
  expect(wind.offset_c > 0.0f && wind_target.dispatch_target_c > 21.0f &&
             std::strcmp(wind_target.modifier_source, "forecast") == 0,
         "cold wind: bounded forecast modifier is a distribution input, not physical aggregation");
  lv6_authority::Lease lease;
  configure_touch_lease(&lease);
  expect_scenario("Cold wind event", state_result(&lease, 1000, "bounded_forecast", "wind_preload"),
                  {"touch_normal", "bounded_forecast", "wind_preload"});

  hv6fc::ForecastHour sunny[1] = {{15.0f, 10.0f, 0.0f, 800.0f}};
  hv6fc::ZoneExposure solar{hv6fc::WALL_NORTH, 1.0f, 1.0f, 1};
  const hv6fc::PreloadDecision relief = hv6fc::compute_zone_preload(sunny, 1, 0, solar, params);
  expect(relief.offset_c == 0.0f, "solar gain: relief removes unnecessary preload");
  expect_scenario("Solar gain", state_result(&lease, 1000, "no_preload", "solar_relief"),
                  {"touch_normal", "no_preload", "solar_relief"});
}

void test_all_satisfied_and_partial_room_failure() {
  const StrategySnapshot satisfied = two_six_loop_house(21.0f).strategy_snapshot();
  expect(satisfied.has_physical_temperature && satisfied.demand_zones == 0,
         "all satisfied: physical model reports no calling rooms");
  lv6_authority::Lease lease;
  configure_touch_lease(&lease);
  expect_scenario("All rooms satisfied", state_result(&lease, 1000, "no_call", "all_rooms_satisfied"),
                  {"touch_normal", "no_call", "all_rooms_satisfied"});

  expect(room_command_outcome(2, 3) == RoomCommandOutcome::PARTIAL,
         "multi-loop partial failure: result is not silently accepted");
  expect_scenario("Multi-loop partial failure", state_result(&lease, 1000, "partial", "one_loop_failed"),
                  {"touch_normal", "partial", "one_loop_failed"});
}

void test_conflicting_authority() {
  lv6_authority::Lease lease;
  configure_touch_lease(&lease);
  lv6_authority::Request conflicting{"house-1", "touch-1", "lease-2", 2, 1000, 90000, false};
  expect(lease.acquire_or_renew(conflicting, true, 1000) == lv6_authority::Result::CONFLICT,
         "conflicting authority: V6-A latches conflict");
  expect_scenario("Conflicting authority", state_result(&lease, 1000, "local_safe", "authority_conflict"),
                  {"conflict", "local_safe", "authority_conflict"});
}

}  // namespace

int main() {
  test_touch_reboot();
  test_v6_a_reboot();
  test_v6_b_disconnect();
  test_asgard_unavailable();
  test_weather_unavailable();
  test_dhw_and_defrost();
  test_cold_wind_and_solar();
  test_all_satisfied_and_partial_room_failure();
  test_conflicting_authority();
  if (failures != 0) {
    std::fprintf(stderr, "%d end-to-end simulation failures\n", failures);
    return 1;
  }
  std::puts("All end-to-end simulation scenarios passed.");
  return 0;
}
