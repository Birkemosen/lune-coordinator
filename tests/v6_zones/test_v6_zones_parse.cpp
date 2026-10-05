#include "v6_zones_parse.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace esphome::lune_touch_coordinator::v6_zones_parse;

static int g_failures = 0;

static void expect(bool cond, const char *what) {
  if (cond) {
    std::printf("PASS  %s\n", what);
  } else {
    std::printf("FAIL  %s\n", what);
    g_failures++;
  }
}

static void test_legacy_no_group_info() {
  ZoneFields zone{};
  zone.zone_number = 2;
  zone.has_group_info = false;
  expect(is_group_primary(zone), "legacy: every zone is primary");
  expect(should_auto_propose_room(zone), "legacy: auto-propose room");
}

static void test_grouped_primary_and_secondary() {
  ZoneFields primary{};
  primary.zone_number = 1;
  primary.group_primary_zone = 1;
  primary.has_group_info = true;
  int members[] = {1, 2};
  primary.group_members_mask = members_mask_from_list(members, 2);
  expect(is_group_primary(primary), "grouped: zone 1 is primary of itself");
  expect(should_auto_propose_room(primary), "grouped: propose room for primary");
  expect(primary.group_members_mask == 0x03, "grouped: members mask bits 0+1");

  ZoneFields secondary{};
  secondary.zone_number = 2;
  secondary.group_primary_zone = 1;
  secondary.has_group_info = true;
  expect(!is_group_primary(secondary), "grouped: zone 2 is secondary");
  expect(!should_auto_propose_room(secondary), "grouped: never propose for secondary");
}

static void test_enabled_maps_to_unused() {
  ZoneFields used{};
  used.zone_number = 1;
  used.has_enabled = true;
  used.enabled = true;
  used.status = "OFF";
  used.has_group_info = true;
  used.group_primary_zone = 1;
  expect(should_auto_propose_room(used), "enabled: propose room");
  expect(std::strcmp(live_status_for_zone(used), "idle") == 0, "enabled OFF → idle");

  ZoneFields unused{};
  unused.zone_number = 2;
  unused.has_enabled = true;
  unused.enabled = false;
  unused.status = "OFF";
  unused.has_group_info = true;
  unused.group_primary_zone = 2;
  unused.friendly_name = "";
  expect(!should_auto_propose_room(unused), "disabled empty: no auto room");
  expect(std::strcmp(live_status_for_zone(unused), "unused") == 0, "disabled → unused");

  ZoneFields named_off{};
  named_off.zone_number = 5;
  named_off.has_enabled = true;
  named_off.enabled = false;
  named_off.status = "OFF";
  named_off.has_group_info = true;
  named_off.group_primary_zone = 5;
  named_off.friendly_name = "Pejsestue";
  expect(should_auto_propose_room(named_off), "disabled named: still propose");
  expect(std::strcmp(live_status_for_zone(named_off), "unused") == 0, "disabled named → unused");

  ZoneFields heating{};
  heating.has_enabled = true;
  heating.enabled = true;
  heating.status = "HEATING";
  expect(std::strcmp(live_status_for_zone(heating), "heat") == 0, "HEATING → heat");
}

static void test_heat_demand_mapping() {
  NodeHeatDemandFields fields{};
  fields.present = true;
  fields.critical_zone = 3;
  fields.critical_opening_ratio = 0.95f;
  fields.saturated_s = 1250;
  fields.demanding_zones = 1;
  fields.headroom = false;
  fields.recommendation = "raise";
  auto demand = to_node_heat_demand(fields, 5000);
  expect(demand.fresh && demand.critical_zone == 3, "demand: critical zone");
  expect(demand.recommendation == ::lune_touch::HeatRecommendation::RAISE, "demand: raise");
  expect(parse_recommendation("lower") == ::lune_touch::HeatRecommendation::LOWER,
         "demand: lower parse");
  expect(parse_recommendation("hold") == ::lune_touch::HeatRecommendation::HOLD,
         "demand: hold parse");
}

int main() {
  test_legacy_no_group_info();
  test_grouped_primary_and_secondary();
  test_enabled_maps_to_unused();
  test_heat_demand_mapping();
  if (g_failures > 0) {
    std::printf("%d test(s) FAILED.\n", g_failures);
    return EXIT_FAILURE;
  }
  std::printf("All v6_zones_parse tests passed.\n");
  return EXIT_SUCCESS;
}
