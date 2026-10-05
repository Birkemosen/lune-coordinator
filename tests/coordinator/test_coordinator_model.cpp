#include "coordinator_model.h"
#include "room_physics.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace lune_touch;

static int g_failures = 0;

static void expect(bool cond, const char *what) {
  if (cond) {
    std::printf("PASS  %s\n", what);
  } else {
    std::printf("FAIL  %s\n", what);
    g_failures++;
  }
}

static CommandRecord command(const char *id, uint32_t now_ms, uint32_t ttl_ms) {
  CommandRecord record{};
  std::strncpy(record.request_id, id, sizeof(record.request_id) - 1);
  std::strncpy(record.source, "forecast", sizeof(record.source) - 1);
  std::strncpy(record.reason, "wind preload", sizeof(record.reason) - 1);
  std::strncpy(record.node_id, "v6-a", sizeof(record.node_id) - 1);
  std::strncpy(record.loop_id, "loop-v6-a-03", sizeof(record.loop_id) - 1);
  record.node_index = 0;
  record.zone_index = 2;
  record.requested_offset_c = 1.25f;
  record.created_at_ms = now_ms;
  record.expires_at_ms = now_ms + ttl_ms;
  return record;
}

static void test_node_staleness() {
  HouseModel model;
  model.set_node_stale_after_ms(300000);

  int ground = model.upsert_node("v6-ground", "lune-v6-ground.local", "192.168.1.51",
                                 "lune-v6", "1.4.0", NodeTrust::TRUSTED);
  expect(ground == 0, "node: first insert returns index 0");
  expect(model.is_node_stale(0, 1000), "node: unseen node is stale");

  expect(model.mark_node_seen(0, 1000), "node: mark seen succeeds");
  expect(!model.is_node_stale(0, 300000), "node: fresh inside stale window");
  expect(model.is_node_stale(0, 302001), "node: stale after window");

  int updated = model.upsert_node("v6-ground", "lune-v6-ground-new.local", "192.168.1.52",
                                  "lune-v6", "1.4.1", NodeTrust::TRUSTED);
  expect(updated == 0 && model.node_count() == 1, "node: upsert updates existing node");
  const PairedNode *node = model.node(0);
  expect(node != nullptr && std::strcmp(node->firmware, "1.4.1") == 0, "node: firmware updated");
  expect(node != nullptr && std::strcmp(node->name, "v6-ground") == 0,
         "node: default friendly name is node id");
  expect(model.update_node_name("v6-ground", "Ground floor manifold"),
         "node: friendly name update succeeds");
  node = model.node(0);
  expect(node != nullptr && std::strcmp(node->name, "Ground floor manifold") == 0,
         "node: friendly name stored");
  expect(model.update_node_metadata(0, "lune-v6", "1.4.2", "192.168.1.60"),
         "node: metadata update succeeds");
  node = model.node(0);
  expect(node != nullptr && std::strcmp(node->firmware, "1.4.2") == 0 &&
             std::strcmp(node->fallback_ip, "192.168.1.60") == 0,
         "node: metadata refresh updates firmware and ip");
  expect(model.update_node_identity(0, "hv6-001122334455"),
         "node: identity update succeeds");
  node = model.node(0);
  expect(node != nullptr && std::strcmp(node->pairing_fingerprint, "hv6-001122334455") == 0,
         "node: identity fingerprint stored");
  expect(!model.update_node_metadata(2, "lune-v6", "bad", "192.168.1.99"),
         "node: metadata rejects missing index");
  expect(!model.update_node_identity(2, "hv6-missing"),
         "node: identity rejects missing index");
}

static void test_node_unreachable_marks_zones_stale() {
  HouseModel model;
  model.upsert_node("v6-a", "a.local", "", "lune-v6", "1.0", NodeTrust::TRUSTED);
  model.bind_zone("living", "Living", 0, 0);
  model.update_zone_live("living", 21.0f, true, 21.0f, true, "heat", true, 1000);
  expect(model.mark_node_seen(0, 1000), "node: reachable before failure");
  expect(model.mark_node_unreachable(0, 2000), "node: mark unreachable succeeds");
  expect(model.is_node_stale(0, 2000), "node: unreachable is stale");
  ResolvedZone living = model.resolve_room("living");
  expect(living.live != nullptr && !living.live->fresh && std::strcmp(living.live->status, "stale") == 0,
         "node: unreachable marks bound zones stale");
}

static void test_node_trust_updates() {
  HouseModel model;
  model.upsert_node("v6-a", "a.local", "", "lune-v6", "1.0", NodeTrust::PAIRED);
  expect(model.node(0) != nullptr && model.node(0)->trust == NodeTrust::PAIRED,
         "trust: node starts paired");
  expect(!model.update_node_trust("v6-a", NodeTrust::TRUSTED),
         "trust: reject trusted promotion without identity");
  expect(model.update_node_identity(0, "hv6-aabbccddeeff"),
         "trust: identity must be stored before promotion");
  expect(model.update_node_trust("v6-a", NodeTrust::TRUSTED),
         "trust: promote identified paired node");
  expect(model.node(0) != nullptr && model.node(0)->trust == NodeTrust::TRUSTED,
         "trust: node promoted to trusted");
  expect(std::strcmp(node_trust_name(model.node(0)->trust), "trusted") == 0,
         "trust: trusted label");
  expect(model.update_node_trust("v6-a", NodeTrust::PAIRED),
         "trust: demote trusted node to paired");
  expect(model.node(0) != nullptr && model.node(0)->trust == NodeTrust::PAIRED,
         "trust: node demoted to paired");
  expect(!model.update_node_trust("missing", NodeTrust::TRUSTED),
         "trust: reject missing node");
}

static void test_zone_registry() {
  HouseModel model;
  int ground = model.upsert_node("v6-ground", "ground.local", "", "lune-v6", "1.4.0", NodeTrust::TRUSTED);
  int first = model.upsert_node("v6-first", "first.local", "", "lune-v6", "1.4.0", NodeTrust::TRUSTED);
  expect(ground == 0 && first == 1, "registry: two V6 nodes inserted");

  expect(model.bind_zone("living", "Living Room", 0, 1), "registry: bind living room");
  expect(model.bind_zone("bath", "Bathroom", 1, 4), "registry: bind bathroom");
  expect(!model.bind_zone("bad", "Bad", 3, 0), "registry: reject missing node");
  expect(!model.bind_zone("bad-zone", "Bad Zone", 0, 6), "registry: reject invalid V6 zone");

  ResolvedZone living = model.resolve_room("living");
  expect(living.node != nullptr && living.binding != nullptr, "registry: resolve bound room");
  const LogicalRoom *living_room = model.room_by_id("living");
  expect(living_room != nullptr && living_room->total_area_m2 == 1.0f &&
             living_room->physical_weight == 1.0f &&
             living_room->include_in_house_temperature,
         "registry: imported room starts with neutral geometry defaults");
  expect(living.binding && living.binding->node_index == 0 && living.binding->zone_index == 1,
         "registry: resolved room maps to node/zone");
  expect(living.binding && living.binding->name_source == ZoneNameSource::TOUCH,
         "registry: manual bind marks name as Touch-owned");
  expect(model.active_zone_count() == 2, "registry: active zone count");
  expect(model.update_zone_comfort("living", 22.0f, 3), "registry: update comfort intent");
  living = model.resolve_room("living");
  expect(living.binding != nullptr && living.binding->comfort_setpoint_c == 22.0f &&
             living.binding->comfort_bias_c == 0.0f && living.binding->priority == 3,
         "registry: comfort intent stored separately");
  expect(model.average_comfort_setpoint_c() > 21.4f && model.average_comfort_setpoint_c() < 21.6f,
         "registry: average comfort setpoint");
  expect(model.update_zone_comfort("living", 22.0f, 3, -0.5f), "registry: update comfort bias");
  living = model.resolve_room("living");
  expect(living.binding != nullptr && living.binding->comfort_bias_c == -0.5f &&
             HouseModel::effective_comfort_setpoint_c(*living.binding) > 21.4f &&
             HouseModel::effective_comfort_setpoint_c(*living.binding) < 21.6f,
         "registry: comfort bias affects effective comfort");
  expect(model.update_zone_live_by_binding(0, 1, 20.5f, true, 20.5f, true, "idle", true, 1000),
         "registry: V6 applied setpoint is reported as live state");
  living = model.resolve_room("living");
  const auto *living_live = model.zone_live(0);
  expect(living.binding != nullptr && living.binding->comfort_setpoint_c == 22.0f &&
             living.binding->comfort_bias_c == -0.5f && living.binding->priority == 3 &&
             living_live != nullptr && living_live->has_setpoint && living_live->setpoint_c == 20.5f,
         "registry: V6 report does not overwrite Touch coordination intent");
  expect(model.update_zone_schedule("living", true, 0x1F, 390, 1290, 20.5f),
         "registry: update schedule");
  living = model.resolve_room("living");
  expect(living.binding != nullptr && living.binding->schedule_enabled &&
             living.binding->schedule_day_mask == 0x1F &&
             living.binding->schedule_start_min == 390 &&
             living.binding->schedule_end_min == 1290 &&
             living.binding->schedule_setpoint_c > 20.4f,
         "registry: schedule stored");
  RoomUpdate atomic{};
  atomic.expected_revision = model.room_revision("living");
  atomic.total_area_m2 = 30.0f;
  atomic.physical_weight = 1.5f;
  atomic.include_in_house_temperature = true;
  atomic.comfort_setpoint_c = 22.0f;
  atomic.comfort_bias_c = -0.5f;
  atomic.priority = 3;
  atomic.schedule_enabled = true;
  atomic.schedule_day_mask = 0x1F;
  atomic.schedule_start_min = 390;
  atomic.schedule_end_min = 1290;
  atomic.schedule_setpoint_c = 20.5f;
  atomic.exterior_walls = 1;
  atomic.wind_exposure = 0.8f;
  atomic.solar_gain = 0.2f;
  atomic.thermal_lead_h = 6;
  atomic.max_offset_c = 1.2f;
  uint32_t updated_revision = 0;
  expect(model.apply_room_update("living", atomic, &updated_revision) == RoomUpdateResult::STORED &&
             updated_revision == atomic.expected_revision + 1,
         "registry: atomic room update stores all room fields with a new revision");
  living = model.resolve_room("living");
  expect(living.binding != nullptr && living.binding->comfort_setpoint_c == 22.0f &&
             living.binding->schedule_setpoint_c == 20.5f && living.binding->wind_exposure == 0.8f,
         "registry: atomic room update propagates to every loop");
  const float before_failed_update = living.binding->comfort_setpoint_c;
  atomic.expected_revision = updated_revision;
  atomic.schedule_start_min = 1320;
  atomic.schedule_end_min = 360;
  expect(model.apply_room_update("living", atomic) == RoomUpdateResult::INVALID &&
             model.resolve_room("living").binding->comfort_setpoint_c == before_failed_update,
         "registry: invalid atomic field leaves the room unchanged");
  atomic.schedule_start_min = 390;
  atomic.schedule_end_min = 1290;
  atomic.expected_revision = updated_revision - 1;
  expect(model.apply_room_update("living", atomic) == RoomUpdateResult::STALE_REVISION,
         "registry: stale atomic room edit is rejected");
  {
    // A disabled, never-initialised schedule (0–0, 0 °C, as imported from V6)
    // must not block edits of other room fields.
    RoomUpdate legacy = atomic;
    legacy.expected_revision = updated_revision;
    legacy.include_in_house_temperature = false;
    legacy.schedule_enabled = false;
    legacy.schedule_day_mask = 0;
    legacy.schedule_start_min = 0;
    legacy.schedule_end_min = 0;
    legacy.schedule_setpoint_c = 0.0f;
    uint32_t legacy_revision = 0;
    expect(model.apply_room_update("living", legacy, &legacy_revision) == RoomUpdateResult::STORED,
           "registry: disabled uninitialised schedule does not reject a room edit");
    const auto r = model.resolve_room("living");
    expect(r.binding != nullptr && r.binding->schedule_start_min == 360 &&
               r.binding->schedule_end_min == 1320 && r.binding->schedule_setpoint_c == 21.0f,
           "registry: disabled schedule normalised to the default window");
    // Restore the enabled schedule for the checks below.
    RoomUpdate restore = atomic;
    restore.expected_revision = legacy_revision;
    expect(model.apply_room_update("living", restore, &updated_revision) == RoomUpdateResult::STORED,
           "registry: schedule restored after legacy check");
    living = model.resolve_room("living");
  }
  float scheduled = 0.0f;
  expect(living.binding != nullptr &&
             HouseModel::scheduled_comfort_setpoint_c(*living.binding, 0, 390, &scheduled) &&
             scheduled > 19.9f && scheduled < 20.1f,
         "registry: schedule resolves with comfort bias");
  expect(living.binding != nullptr &&
             !HouseModel::scheduled_comfort_setpoint_c(*living.binding, 0, 389, &scheduled),
         "registry: schedule inactive before start");
  expect(living.binding != nullptr &&
             !HouseModel::scheduled_comfort_setpoint_c(*living.binding, 0, 1290, &scheduled),
         "registry: schedule inactive at end");
  expect(living.binding != nullptr &&
             !HouseModel::scheduled_comfort_setpoint_c(*living.binding, 6, 420, &scheduled),
         "registry: schedule inactive on masked day");
  expect(!model.update_zone_schedule("living", true, 0, 390, 1290, 20.5f),
         "registry: reject enabled schedule without days");
  expect(!model.update_zone_schedule("living", true, 0x7F, 1290, 390, 20.5f),
         "registry: reject invalid schedule window");

  expect(model.bind_zone("living", "Living Room North", 1, 2), "registry: add second loop to room");
  living = model.resolve_room("living");
  ResolvedRoomLoop living_loops[2]{};
  expect(model.room_count() == 2 && model.resolve_room_loops("living", living_loops, 2) == 2 &&
             living_loops[0].binding && living_loops[1].binding &&
             living_loops[1].binding->node_index == 1 && living_loops[1].binding->zone_index == 2,
         "registry: room retains both loop targets");
}

static void test_zone_name_sources() {
  HouseModel model;
  model.upsert_node("v6-a", "a.local", "", "lune-v6", "1.0", NodeTrust::TRUSTED);
  expect(model.bind_zone_with_source("v61-z1", "Zone 1", 0, 0, ZoneNameSource::GENERATED),
         "names: generated binding can be created");
  expect(model.update_zone_name_from_v6_by_binding(0, 0, "Living"),
         "names: V6 name replaces generated name");
  ResolvedZone living = model.resolve_room("v61-z1");
  expect(living.binding != nullptr && std::strcmp(living.binding->room_name, "Living") == 0 &&
             living.binding->name_source == ZoneNameSource::V6,
         "names: V6 source stored");
  const LogicalRoom *logical_room = model.room_by_id("v61-z1");
  expect(logical_room != nullptr && std::strcmp(logical_room->room_name, "Living") == 0 &&
             logical_room->name_source == ZoneNameSource::V6,
         "names: logical room follows V6 name");
  expect(model.bind_zone("v61-z1", "Local Living", 0, 0),
         "names: Touch rename succeeds");
  expect(!model.update_zone_name_from_v6_by_binding(0, 0, "Kitchen"),
         "names: V6 does not overwrite Touch override");
  living = model.resolve_room("v61-z1");
  expect(living.binding != nullptr && std::strcmp(living.binding->room_name, "Local Living") == 0 &&
             living.binding->name_source == ZoneNameSource::TOUCH,
         "names: Touch override preserved");
}

static void test_rediscovered_physical_zone_reactivates_binding() {
  HouseModel model;
  model.upsert_node("v6-a", "a.local", "", "lune-v6", "1.0", NodeTrust::TRUSTED);
  expect(model.bind_zone_with_source("legacy-room", "Old name", 0, 0,
                                     ZoneNameSource::GENERATED),
         "rediscovery: initial physical zone can be bound");

  const ResolvedZone initial = model.resolve_room("legacy-room");
  expect(initial.binding != nullptr && initial.binding->loop_id[0] != '\0',
         "rediscovery: initial binding has stable loop identity");
  char loop_id[48]{};
  if (initial.binding != nullptr)
    std::strncpy(loop_id, initial.binding->loop_id, sizeof(loop_id) - 1);
  expect(model.remove_loop(loop_id), "rediscovery: removed loop becomes inactive");
  expect(model.active_zone_count() == 0,
         "rediscovery: inactive loop is excluded from active zones");

  expect(model.bind_zone_with_source("v61-z1", "Kontor", 0, 0, ZoneNameSource::V6),
         "rediscovery: V6 polling reactivates the existing physical loop");
  expect(model.update_zone_live_by_binding(0, 0, 25.5f, true, 21.0f, true,
                                           "IDLE", true, 1000, 0.0f, true),
         "rediscovery: telemetry attaches after reactivation");

  const ResolvedZone restored = model.resolve_room("legacy-room");
  expect(restored.binding != nullptr && restored.live != nullptr &&
             restored.binding->enabled && restored.live->has_temperature &&
             std::fabs(restored.live->temperature_c - 25.5f) < 0.001f,
         "rediscovery: historical room keeps the live V6 temperature");
  expect(restored.binding != nullptr &&
             std::strcmp(restored.binding->room_name, "Kontor") == 0 &&
             restored.binding->name_source == ZoneNameSource::V6,
         "rediscovery: V6 name replaces the generated historical name");
}

static void test_remove_node_remaps_zones() {
  HouseModel model;
  model.upsert_node("v6-a", "a.local", "", "lune-v6", "1.0", NodeTrust::TRUSTED);
  model.upsert_node("v6-b", "b.local", "", "lune-v6", "1.0", NodeTrust::TRUSTED);
  model.upsert_node("v6-c", "c.local", "", "lune-v6", "1.0", NodeTrust::TRUSTED);
  model.bind_zone("living", "Living", 0, 0);
  model.bind_zone("bath", "Bath", 1, 2);
  model.bind_zone("bed", "Bedroom", 2, 3);

  expect(model.remove_node("v6-b"), "registry: remove middle node");
  expect(model.node_count() == 2, "registry: node count after remove");
  expect(model.resolve_room("bath").binding == nullptr, "registry: removed node disables bound rooms");
  ResolvedZone bed = model.resolve_room("bed");
  expect(bed.binding != nullptr && bed.binding->node_index == 1, "registry: later zones remap down");
  expect(!model.remove_node("missing"), "registry: reject missing node remove");
}

static void test_update_node_host_keeps_identity() {
  HouseModel model;
  model.upsert_node("192-168-1-20", "", "192.168.1.20", "lune-v6", "1.0", NodeTrust::TRUSTED);
  model.update_node_identity(0, "hv6-aabbccddeeff");
  model.bind_zone("living", "Living", 0, 0);
  model.mark_node_seen(0, 1000);

  expect(model.update_node_host("192-168-1-20", "", "192.168.1.77") == 0, "host: move by IP");
  const auto *node = model.node(0);
  expect(node != nullptr && std::strcmp(node->fallback_ip, "192.168.1.77") == 0, "host: new IP stored");
  expect(std::strcmp(node->node_id, "192-168-1-20") == 0, "host: node id unchanged");
  expect(node->trust == NodeTrust::TRUSTED, "host: trust kept");
  expect(std::strcmp(node->pairing_fingerprint, "hv6-aabbccddeeff") == 0, "host: fingerprint kept");
  expect(!node->reachable, "host: reachability unknown until next poll");
  expect(model.resolve_room("living").binding != nullptr, "host: zone binding kept");
  expect(model.update_node_host("192-168-1-20", "hv6.local", "") == 0 &&
             std::strcmp(model.node(0)->hostname, "hv6.local") == 0,
         "host: move by hostname");
  expect(model.update_node_host("192-168-1-20", "", "") < 0, "host: reject empty address");
  expect(model.update_node_host("missing", "", "10.0.0.1") < 0, "host: reject unknown node");
  expect(model.node_count() == 1, "host: no second node");
}

static void test_logical_room_multiple_loops() {
  HouseModel model;
  model.upsert_node("v6-a", "a.local", "", "lune-v6", "1.0", NodeTrust::TRUSTED);
  model.upsert_node("v6-b", "b.local", "", "lune-v6", "1.0", NodeTrust::TRUSTED);
  expect(model.bind_zone("living", "Living", 0, 0), "multi-loop: first loop binds room");
  expect(model.bind_zone("living", "Living", 0, 1), "multi-loop: second local loop binds room");
  expect(model.bind_zone("living", "Living", 1, 2), "multi-loop: second-node loop binds room");
  expect(model.room_count() == 1 && model.zone_count() == 3,
         "multi-loop: 48m2 room appears once while retaining three loops");
  expect(!model.bind_zone("kitchen", "Kitchen", 0, 1),
         "multi-loop: duplicate physical loop assignment rejected");
  expect(model.set_room_geometry("living", 48.0f, 48.0f, true),
         "multi-loop: room area and physical aggregation settings stored");
  expect(model.set_loop_served_area("loop-v6-a-01", 16.0f) &&
             model.set_loop_served_area("loop-v6-a-02", 16.0f) &&
             model.set_loop_served_area("loop-v6-b-03", 16.0f),
         "multi-loop: served area is stored per physical loop");
  ResolvedRoomLoop loops[3]{};
  expect(model.resolve_room_loops("living", loops, 3) == 3 && loops[2].node != nullptr &&
             std::strcmp(loops[2].node->node_id, "v6-b") == 0,
         "multi-loop: room resolves all loops across V6 nodes");
  expect(model.remove_loop("loop-v6-a-01"), "multi-loop: remove one loop");
  expect(model.room_count() == 1 && model.resolve_room_loops("living", loops, 3) == 2,
         "multi-loop: removing a loop preserves the logical room");
}

static void test_persisted_state_roundtrip() {
  HouseModel model;
  model.upsert_node("v6-a", "a.local", "192.168.1.51", "lune-v6", "1.0", NodeTrust::TRUSTED);
  model.update_node_name("v6-a", "Ground manifold");
  model.update_node_identity(0, "hv6-aabbccddeeff");
  model.bind_zone("living", "Living", 0, 4);
  model.update_zone_comfort("living", 21.8f, 2, 0.4f);
  model.update_zone_schedule("living", true, 0x7F, 360, 1320, 21.2f);
  model.update_zone_live("living", 21.4f, true, 21.0f, true, "heat", true, 1234);
  model.update_zone_live("living", 21.6f, true, 21.0f, true, "heat", true, 3601234);

  PersistedState state{};
  expect(model.export_state(&state), "persist: export succeeds");
  expect(state.magic == PERSISTED_STATE_MAGIC && state.version == PERSISTED_STATE_VERSION,
         "persist: magic and version set");
  state.rooms[0].total_area_m2 = 0.0f;
  state.rooms[0].physical_weight = 0.0f;

  HouseModel restored;
  expect(restored.import_state(state), "persist: import succeeds");
  expect(restored.node_count() == 1 && restored.zone_count() == 1, "persist: counts restored");
  expect(restored.room(0) != nullptr && restored.room(0)->total_area_m2 == 1.0f &&
             restored.room(0)->physical_weight == 1.0f,
         "persist: legacy zero geometry is upgraded to neutral defaults");
  ResolvedZone living = restored.resolve_room("living");
  expect(living.node != nullptr && std::strcmp(living.node->hostname, "a.local") == 0,
         "persist: node fields restored");
  expect(living.node != nullptr && std::strcmp(living.node->name, "Ground manifold") == 0,
         "persist: node friendly name restored");
  expect(living.node != nullptr && std::strcmp(living.node->pairing_fingerprint, "hv6-aabbccddeeff") == 0,
         "persist: node identity restored");
  expect(living.binding != nullptr && living.binding->zone_index == 4,
         "persist: zone binding restored");
  expect(living.binding != nullptr && std::strcmp(living.binding->loop_id, "loop-v6-a-05") == 0,
         "persist: stable loop identity restored");
  expect(living.binding != nullptr && living.binding->name_source == ZoneNameSource::TOUCH,
         "persist: zone name source restored");
  expect(living.binding != nullptr && living.binding->comfort_setpoint_c > 21.7f &&
             living.binding->comfort_bias_c > 0.3f && living.binding->priority == 2,
         "persist: comfort intent restored");
  expect(living.binding != nullptr && living.binding->schedule_enabled &&
             living.binding->schedule_start_min == 360 &&
             living.binding->schedule_end_min == 1320 &&
             living.binding->schedule_setpoint_c > 21.1f,
         "persist: schedule restored");
  expect(living.binding != nullptr && living.binding->thermal_samples == 1 &&
             living.binding->learned_heat_gain_c_per_h > 0.15f &&
             living.binding->learned_heat_gain_c_per_h < 0.25f,
         "persist: thermal model restored");
  expect(living.live != nullptr && !living.live->fresh && std::strcmp(living.live->status, "unknown") == 0,
         "persist: live state is runtime-only");
  const ZoneHistory *living_history = restored.zone_history(0);
  expect(living_history != nullptr && !living_history->has_temperature &&
             living_history->samples == 0 && living_history->calling_samples == 0,
         "persist: runtime learning history resets on reboot");

  state.magic = 0;
  expect(!restored.import_state(state), "persist: reject invalid magic");
}

static void test_zone_live_state() {
  HouseModel model;
  model.upsert_node("v6-a", "a.local", "", "lune-v6", "1.0", NodeTrust::TRUSTED);
  model.bind_zone("living", "Living", 0, 0);
  model.bind_zone("bath", "Bath", 0, 1);

  expect(model.update_zone_live("living", 20.9f, true, 21.5f, true, "heat", true, 5000),
         "live: update mapped room");
  expect(model.update_zone_live("bath", 19.0f, true, 20.0f, true, "stale", false, 5000,
                                42.5f, true),
         "live: update stale room");
  expect(!model.update_zone_live("missing", 0.0f, false, 0.0f, false, "idle", false, 5000),
         "live: reject unknown room");
  expect(model.calling_zone_count() == 1, "live: calling zone count");
  expect(model.stale_zone_count() == 1, "live: stale zone count");
  const ZoneHistory *living_history = model.zone_history(0);
  expect(living_history != nullptr && living_history->samples == 1 &&
             living_history->calling_samples == 1,
         "history: first fresh sample recorded");
  expect(model.update_zone_live("living", 21.9f, true, 21.5f, true, "idle", true, 3605000),
         "history: second fresh sample update");
  living_history = model.zone_history(0);
  expect(living_history != nullptr && living_history->samples == 2 &&
             living_history->calling_samples == 1,
         "history: tracks samples and calling samples");
  expect(living_history != nullptr && living_history->min_temperature_c > 20.8f &&
             living_history->max_temperature_c > 21.8f &&
             living_history->average_temperature_c > 21.3f &&
             living_history->average_temperature_c < 21.5f,
         "history: tracks temperature range and average");
  expect(living_history != nullptr && living_history->has_delta &&
             living_history->last_delta_c_per_h > 0.9f &&
             living_history->last_delta_c_per_h < 1.1f,
         "history: tracks latest temperature rate");
  const LearningSnapshot learning = model.learning_snapshot();
  expect(learning.zones_with_history == 1 && learning.total_samples == 2,
         "learning: summarizes zone samples");
  expect(learning.total_calling_samples == 1 && learning.calling_ratio > 0.49f &&
             learning.calling_ratio < 0.51f,
         "learning: summarizes calling ratio");
  expect(learning.zones_with_delta == 1 && learning.warming_zones == 1 &&
             learning.cooling_zones == 0 && learning.average_delta_c_per_h > 0.9f,
         "learning: summarizes temperature rate");
  const ZoneBinding *living_binding = model.zone(0);
  expect(living_binding != nullptr && living_binding->thermal_samples == 0,
         "thermal: idle warming does not count as a rate sample");
  expect(living_binding != nullptr && living_binding->learned_heat_gain_c_per_h == 0.0f &&
             living_binding->learned_cool_loss_c_per_h == 0.0f,
         "thermal: ignores idle warming as passive noise");
  expect(model.update_zone_live("living", 21.75f, true, 21.5f, true, "idle", true, 7205000),
         "thermal: passive cooling sample update");
  living_binding = model.zone(0);
  expect(living_binding != nullptr && living_binding->thermal_samples == 1 &&
             living_binding->learned_cool_loss_c_per_h > 0.10f &&
             living_binding->learned_cool_loss_c_per_h < 0.20f,
         "thermal: learns passive cooling rate");
  expect(model.update_zone_live("living", 21.95f, true, 21.5f, true, "heat", true, 10805000),
         "thermal: warming sample update");
  living_binding = model.zone(0);
  expect(living_binding != nullptr && living_binding->thermal_samples == 2 &&
             living_binding->learned_heat_gain_c_per_h > 0.15f &&
             living_binding->learned_heat_gain_c_per_h < 0.25f,
         "thermal: learns heat gain rate");
  expect(living_binding != nullptr &&
             HouseModel::learned_thermal_lead_h(*living_binding) == 0 &&
             HouseModel::active_thermal_lead_h(*living_binding) == living_binding->thermal_lead_h,
         "thermal: waits for enough samples before lead tuning");

  HouseModel slow_model;
  slow_model.upsert_node("v6-slow", "slow.local", "", "lune-v6", "1.0", NodeTrust::TRUSTED);
  slow_model.bind_zone("slow", "Slow slab", 0, 0);
  slow_model.update_zone_forecast_profile("slow", 0x01, 0.8f, 0.2f, 4, 1.5f);
  for (uint8_t i = 0; i < 7; i++) {
    const uint32_t ts = static_cast<uint32_t>(i) * 3600000UL;
    const float temp = 18.0f + static_cast<float>(i) * 0.12f;
    slow_model.update_zone_live("slow", temp, true, 21.0f, true, "heat", true, ts);
  }
  const ZoneBinding *slow_binding = slow_model.zone(0);
  expect(slow_binding != nullptr && HouseModel::learned_thermal_lead_h(*slow_binding) >= 8,
         "thermal: slow learned heat gain extends lead");
  expect(slow_binding != nullptr &&
             HouseModel::active_thermal_lead_h(*slow_binding) >=
                 HouseModel::learned_thermal_lead_h(*slow_binding),
         "thermal: active lead includes learned lead");

  HouseModel fast_model;
  fast_model.upsert_node("v6-fast", "fast.local", "", "lune-v6", "1.0", NodeTrust::TRUSTED);
  fast_model.bind_zone("fast", "Fast room", 0, 0);
  fast_model.update_zone_forecast_profile("fast", 0x01, 0.8f, 0.2f, 10, 1.5f);
  for (uint8_t i = 0; i < 7; i++) {
    const uint32_t ts = static_cast<uint32_t>(i) * 3600000UL;
    const float temp = 18.0f + static_cast<float>(i) * 1.2f;
    fast_model.update_zone_live("fast", temp, true, 21.0f, true, "heat", true, ts);
  }
  const ZoneBinding *fast_binding = fast_model.zone(0);
  expect(fast_binding != nullptr && HouseModel::active_thermal_lead_h(*fast_binding) == 10,
         "thermal: learned lead never shortens configured lead");

  ResolvedZone living = model.resolve_room("living");
  expect(living.live != nullptr && living.live->has_temperature && living.live->temperature_c > 21.8f,
         "live: resolve includes snapshot");
  ResolvedZone bath = model.resolve_room("bath");
  expect(bath.live != nullptr && bath.live->has_valve && bath.live->valve_pct > 42.4f &&
             bath.live->valve_pct < 42.6f,
         "live: tracks valve position");
  expect(model.update_zone_live_by_binding(0, 1, 22.0f, true, 22.5f, true, "call", true,
                                           6000, 125.0f, true),
         "live: update by node/zone binding");
  bath = model.resolve_room("bath");
  expect(bath.live != nullptr && std::strcmp(bath.live->status, "call") == 0,
         "live: binding update changes mapped room");
  expect(bath.live != nullptr && bath.live->has_valve && bath.live->valve_pct == 100.0f,
         "live: clamps valve position");
  expect(!model.update_zone_live_by_binding(2, 1, 0.0f, false, 0.0f, false, "idle", false, 6000),
         "live: reject unmapped binding");
  expect(model.update_zone_comfort("bath", 80.0f, 9, -8.0f), "live: comfort clamp update");
  bath = model.resolve_room("bath");
  expect(bath.binding != nullptr && bath.binding->comfort_setpoint_c == 35.0f &&
             bath.binding->comfort_bias_c == -3.0f && bath.binding->priority == 3,
         "live: comfort clamp stored");
  expect(!model.update_zone_comfort("missing", 21.0f, 1), "live: reject missing comfort room");
}

static void test_effective_comfort_resolver() {
  HouseModel model;
  model.upsert_node("v6-a", "a.local", "", "lune-v6", "1.0", NodeTrust::TRUSTED);
  model.bind_zone("living", "Living", 0, 0);
  model.update_zone_comfort("living", 20.0f, 2, 0.4f);
  model.update_zone_schedule("living", true, 0x01, 360, 540, 22.0f);
  const auto *living = model.resolve_room("living").binding;
  expect(living != nullptr, "effective: mapped room exists");

  EffectiveComfort no_time = HouseModel::effective_comfort(*living, false, 0, 420);
  expect(no_time.setpoint_c > 20.3f && no_time.setpoint_c < 20.5f &&
             std::strcmp(no_time.source, "comfort") == 0 && !no_time.schedule_active,
         "effective: comfort used without valid time");

  EffectiveComfort active = HouseModel::effective_comfort(*living, true, 0, 420);
  expect(active.setpoint_c > 22.3f && active.setpoint_c < 22.5f &&
             std::strcmp(active.source, "schedule") == 0 && active.schedule_active,
         "effective: schedule overrides comfort inside window");

  EffectiveComfort outside = HouseModel::effective_comfort(*living, true, 0, 600);
  expect(outside.setpoint_c > 20.3f && outside.setpoint_c < 20.5f &&
             std::strcmp(outside.source, "comfort") == 0 && !outside.schedule_active,
         "effective: comfort used outside schedule window");

  ZoneBinding slow_zone = *living;
  slow_zone.max_offset_c = 1.2f;
  slow_zone.thermal_samples = 12;
  slow_zone.learned_heat_gain_c_per_h = 0.18f;
  ZoneLiveState live{};
  live.fresh = true;
  live.has_temperature = true;
  live.temperature_c = 19.2f;
  float learned = HouseModel::learned_comfort_offset_c(slow_zone, &live, 21.0f);
  expect(learned > 0.39f && learned < 0.41f,
         "effective: learned offset helps slow under-heated rooms");

  slow_zone.thermal_samples = 11;
  expect(HouseModel::learned_comfort_offset_c(slow_zone, &live, 21.0f) == 0.0f,
         "effective: learned offset waits for enough samples");
  slow_zone.thermal_samples = 12;
  slow_zone.learned_heat_gain_c_per_h = 0.5f;
  expect(HouseModel::learned_comfort_offset_c(slow_zone, &live, 21.0f) == 0.0f,
         "effective: learned offset does not boost fast rooms");
  slow_zone.learned_heat_gain_c_per_h = 0.18f;
  live.fresh = false;
  expect(HouseModel::learned_comfort_offset_c(slow_zone, &live, 21.0f) == 0.0f,
         "effective: learned offset requires fresh live temperature");
}

static void test_room_spread_is_not_a_fault() {
  // Two floors on a sunny day: 22.7 °C downstairs, 27.3 °C upstairs — both real.
  HouseModel model;
  model.upsert_node("v6-a", "a.local", "", "lune-v6", "1.0", NodeTrust::TRUSTED);
  model.bind_zone("hobby", "Hobbyrum", 0, 0);
  model.bind_zone("kitchen", "Kitchen", 0, 1);
  model.bind_zone("loft", "Stue Repo", 0, 2);
  model.set_room_geometry("hobby", 15.0f, 0.0f, true);
  model.set_room_geometry("kitchen", 30.0f, 0.0f, true);
  model.set_room_geometry("loft", 25.0f, 0.0f, true);
  model.update_zone_live("hobby", 22.7f, true, 22.0f, true, "idle", true, 1000);
  model.update_zone_live("kitchen", 24.1f, true, 22.0f, true, "idle", true, 1000);
  model.update_zone_live("loft", 27.3f, true, 22.0f, true, "idle", true, 1000);
  StrategySnapshot st = model.strategy_snapshot();
  expect(st.has_physical_temperature && std::strcmp(st.quality, "healthy") == 0 &&
             !st.spread_healthy && st.implausible_rooms == 0,
         "spread: 4.6 °C between real rooms warns but still publishes");

  // A broken probe (85 °C) is left out, the rest still publishes.
  model.update_zone_live("loft", 85.0f, true, 22.0f, true, "idle", true, 2000);
  st = model.strategy_snapshot();
  expect(st.implausible_rooms == 1 && std::strcmp(st.implausible_room_name, "Stue Repo") == 0 &&
             st.physical_temperature_c < 24.0f,
         "spread: implausible reading excluded from the house average");
}

static void test_strategy_snapshot() {
  HouseModel model;
  model.upsert_node("v6-a", "a.local", "", "lune-v6", "1.0", NodeTrust::TRUSTED);
  model.bind_zone("living", "Living", 0, 0);
  model.bind_zone("bath", "Bath", 0, 1);
  model.set_room_geometry("living", 20.0f, 0.0f, true);
  model.set_room_geometry("bath", 20.0f, 0.0f, true);
  model.update_zone_live("living", 20.0f, true, 21.0f, true, "heat", true, 1000);
  model.update_zone_live("bath", 22.0f, true, 21.0f, true, "idle", true, 1000);

  StrategySnapshot strategy = model.strategy_snapshot();
  expect(strategy.has_physical_temperature, "strategy: has physical temperature");
  expect(strategy.contributing_rooms == 2 && strategy.contributing_area_m2 == 40.0f,
         "strategy: equal-area rooms contribute once each");
  expect(strategy.coverage_healthy && strategy.coverage_ratio > 0.99f,
         "strategy: complete area coverage is healthy");
  expect(strategy.physical_temperature_c > 20.9f && strategy.physical_temperature_c < 21.1f,
         "strategy: equal-area physical average");
  expect(strategy.has_temperature_preview && strategy.preview_zones == 2 &&
             strategy.temperature_preview_c > 20.9f && strategy.temperature_preview_c < 21.1f,
         "strategy: fresh sensor preview follows primary zones");
  expect(strategy.has_setpoint_preview && strategy.setpoint_preview_zones == 2 &&
             strategy.setpoint_preview_c > 20.9f && strategy.setpoint_preview_c < 21.1f,
         "strategy: live V6 setpoint preview follows primary zones");
  model.set_room_geometry("bath", 48.0f, 0.0f, true);
  strategy = model.strategy_snapshot();
  expect(strategy.physical_temperature_c > 21.3f && strategy.physical_temperature_c < 21.5f,
         "strategy: 20m2 plus 48m2 area weighting");
  const float full_coverage_temperature_c = strategy.physical_temperature_c;
  model.update_zone_comfort("living", 21.0f, 3);
  model.update_zone_comfort("bath", 21.0f, 0);
  const float before_priority = strategy.physical_temperature_c;
  strategy = model.strategy_snapshot();
  expect(std::fabs(strategy.physical_temperature_c - before_priority) < 0.001f,
         "strategy: comfort priority cannot affect physical temperature");
  model.bind_zone("bath", "Bath", 0, 2);
  model.update_zone_live_by_binding(0, 2, 10.0f, true, 21.0f, true, "idle", true, 1000);
  strategy = model.strategy_snapshot();
  expect(strategy.contributing_rooms == 2 && strategy.physical_temperature_c > 21.3f &&
             strategy.physical_temperature_c < 21.5f,
         "strategy: multi-loop room is counted once through its primary sensor");
  const float physical_before_schedule = strategy.physical_temperature_c;
  expect(model.update_zone_schedule("living", true, 0x01, 0, 1440, 24.0f),
         "strategy: schedule target update succeeds");
  strategy = model.strategy_snapshot(true, 0, 720);
  expect(strategy.has_house_target && strategy.house_target_c > 21.8f &&
             strategy.house_target_c < 22.1f && std::strcmp(strategy.house_target_source, "mixed") == 0,
         "strategy: area-weighted house target includes schedule intent");
  expect(std::fabs(strategy.physical_temperature_c - physical_before_schedule) < 0.001f,
         "strategy: schedule changes target without changing physical temperature");
  model.set_room_geometry("bath", 48.0f, 0.0f, false);
  strategy = model.strategy_snapshot();
  expect(strategy.contributing_rooms == 1 && strategy.physical_temperature_c > 19.9f &&
             strategy.physical_temperature_c < 20.1f,
         "strategy: excluded room is omitted");
  model.set_room_geometry("bath", 48.0f, 0.0f, true);
  model.update_zone_live("bath", 22.0f, true, 21.0f, true, "stale", false, 2000);
  strategy = model.strategy_snapshot();
  expect(strategy.missing_rooms == 1 && strategy.missing_area_m2 == 48.0f,
         "strategy: stale room reports missing area");
  expect(!strategy.coverage_healthy && !strategy.has_physical_temperature &&
             strategy.coverage_ratio < 0.75f,
         "strategy: below-threshold half-house average is degraded");
  model.update_zone_live("bath", 22.0f, true, 21.0f, true, "idle", true, 3000);
  strategy = model.strategy_snapshot();
  expect(strategy.coverage_healthy && strategy.has_physical_temperature &&
             std::fabs(strategy.physical_temperature_c - full_coverage_temperature_c) < 0.001f,
         "strategy: recovery returns healthy data without a temperature step");

  HouseModel coverage_boundary;
  coverage_boundary.upsert_node("v6-a", "a", "", "v6", "1", NodeTrust::TRUSTED);
  coverage_boundary.bind_zone("large", "Large", 0, 0);
  coverage_boundary.bind_zone("small", "Small", 0, 1);
  coverage_boundary.set_room_geometry("large", 74.0f, 0.0f, true);
  coverage_boundary.set_room_geometry("small", 26.0f, 0.0f, true);
  coverage_boundary.update_zone_live("large", 20.0f, true, 21.0f, true, "heat", true, 1000);
  coverage_boundary.update_zone_live("small", 20.0f, true, 21.0f, true, "stale", false, 1000);
  strategy = coverage_boundary.strategy_snapshot();
  expect(strategy.coverage_ratio > 0.739f && strategy.coverage_ratio < 0.741f &&
             !strategy.coverage_healthy,
         "strategy: 74 percent coverage is below the configured threshold");
  coverage_boundary.update_zone_live("small", 20.0f, true, 21.0f, true, "idle", true, 2000);
  strategy = coverage_boundary.strategy_snapshot();
  expect(strategy.coverage_ratio > 0.99f && strategy.coverage_healthy,
         "strategy: full coverage is above the configured threshold");

  HouseModel two_manifolds;
  two_manifolds.upsert_node("v6-a", "a", "", "v6", "1", NodeTrust::TRUSTED);
  two_manifolds.upsert_node("v6-b", "b", "", "v6", "1", NodeTrust::TRUSTED);
  two_manifolds.bind_zone("a-room", "A", 0, 0);
  two_manifolds.bind_zone("b-room", "B", 1, 0);
  two_manifolds.set_room_geometry("a-room", 50.0f, 0.0f, true);
  two_manifolds.set_room_geometry("b-room", 50.0f, 0.0f, true);
  two_manifolds.update_zone_live("a-room", 20.0f, true, 21.0f, true, "heat", true, 1000);
  two_manifolds.update_zone_live("b-room", 20.0f, true, 21.0f, true, "stale", false, 1000);
  strategy = two_manifolds.strategy_snapshot();
  expect(!strategy.manifolds_healthy && !strategy.has_physical_temperature,
         "strategy: two-manifold installation rejects a single-manifold average");
  two_manifolds.set_minimum_area_coverage(0.5f);
  two_manifolds.set_min_contributing_ua_ratio(0.5f);
  two_manifolds.set_min_contributing_rooms(1);
  two_manifolds.set_allow_degraded_manifolds(true);
  strategy = two_manifolds.strategy_snapshot();
  expect(strategy.manifolds_healthy && strategy.has_physical_temperature,
         "strategy: explicit degraded-manifold mode permits configured single-manifold coverage");

  HouseModel one_of_six;
  one_of_six.set_min_contributing_rooms(2);
  one_of_six.set_expected_manifolds(1);
  one_of_six.upsert_node("v6-a", "a", "", "v6", "1", NodeTrust::TRUSTED);
  for (int i = 0; i < 6; i++) {
    char id[16];
    std::snprintf(id, sizeof(id), "r%d", i);
    one_of_six.bind_zone(id, id, 0, static_cast<size_t>(i));
    one_of_six.set_room_geometry(id, 10.0f, 1.0f, true);
  }
  one_of_six.update_zone_live("r0", 23.3f, true, 21.0f, true, "heat", true, 1000);
  strategy = one_of_six.strategy_snapshot();
  expect(strategy.contributing_rooms == 1 && strategy.missing_rooms == 5 &&
             !strategy.coverage_healthy && !strategy.has_physical_temperature,
         "strategy: one of six configured rooms is not healthy coverage");

  HouseModel single_room;
  // Default min_contributing_rooms is 2; a one-room install must still publish.
  single_room.set_expected_manifolds(1);
  single_room.upsert_node("v6-a", "a", "", "v6", "1", NodeTrust::TRUSTED);
  single_room.bind_zone("only", "Only", 0, 0);
  single_room.set_room_geometry("only", 20.0f, 1.0f, true);
  single_room.update_zone_live("only", 22.8f, true, 21.0f, true, "heat", true, 1000);
  strategy = single_room.strategy_snapshot();
  expect(strategy.contributing_rooms == 1 && strategy.missing_rooms == 0 &&
             strategy.coverage_healthy && strategy.has_physical_temperature &&
             std::fabs(strategy.physical_temperature_c - 22.8f) < 0.01f,
         "strategy: single included room is healthy whole-house coverage");

  model.update_zone_live("bath", 22.0f, true, 21.0f, true, "stale", false, 2000);
  model.update_zone_live("living", 20.0f, true, 21.0f, true, "stale", false, 2000);
  strategy = model.strategy_snapshot();
  expect(!strategy.has_physical_temperature && strategy.contributing_rooms == 0,
         "strategy: no valid rooms has no physical temperature");
}

static void test_zone_forecast_profile() {
  HouseModel model;
  model.upsert_node("v6-a", "a.local", "", "lune-v6", "1.0", NodeTrust::TRUSTED);
  model.bind_zone("living", "Living", 0, 0);

  expect(model.update_zone_forecast_profile_by_binding(0, 0, 0x05, 0.8f, 0.2f, 8, 1.25f),
         "forecast profile: update mapped binding");
  ResolvedZone living = model.resolve_room("living");
  expect(living.binding != nullptr && living.binding->exterior_walls == 0x05,
         "forecast profile: exterior walls stored");
  expect(living.binding != nullptr && living.binding->wind_exposure > 0.79f &&
             living.binding->solar_gain < 0.21f && living.binding->thermal_lead_h == 8,
         "forecast profile: factors stored");
  expect(living.binding != nullptr && living.binding->max_offset_c > 1.24f,
         "forecast profile: max offset stored");

  expect(model.update_zone_forecast_profile_by_binding(0, 0, 0x0A, 0.7f, 0.4f, 7, 1.5f,
                                                       false),
         "forecast profile: V6 telemetry can update legacy factors without owning walls");
  living = model.resolve_room("living");
  expect(living.binding != nullptr && living.binding->exterior_walls == 0x05 &&
             living.binding->wind_exposure > 0.69f && living.binding->wind_exposure < 0.71f,
         "forecast profile: Touch-owned exterior walls survive V6 refresh");

  expect(model.update_zone_forecast_profile_by_binding(0, 0, 0xFF, 2.0f, -1.0f, 80, 9.0f),
         "forecast profile: clamps out-of-range values");
  living = model.resolve_room("living");
  expect(living.binding != nullptr && living.binding->exterior_walls == 0x0F &&
             living.binding->wind_exposure == 1.0f && living.binding->solar_gain == 0.0f &&
             living.binding->thermal_lead_h == 24 && living.binding->max_offset_c == 5.0f,
         "forecast profile: clamped values stored");
  expect(model.update_zone_forecast_profile("living", 0x03, 0.35f, 0.65f, 6, 1.1f),
         "forecast profile: update mapped room");
  living = model.resolve_room("living");
  expect(living.binding != nullptr && living.binding->exterior_walls == 0x03 &&
             living.binding->wind_exposure > 0.34f && living.binding->wind_exposure < 0.36f &&
             living.binding->solar_gain > 0.64f && living.binding->solar_gain < 0.66f &&
             living.binding->thermal_lead_h == 6 && living.binding->max_offset_c > 1.09f &&
             living.binding->max_offset_c < 1.11f,
         "forecast profile: room update stored");
  // B5: N=1 E=2 S=4 W=8 → north|west = 9, not wall count 2.
  expect(model.update_zone_forecast_profile("living", 0x09, 0.5f, 0.3f, 4, 1.0f),
         "forecast profile: north|west bitmask accepted");
  living = model.resolve_room("living");
  expect(living.binding != nullptr && living.binding->exterior_walls == 9,
         "forecast profile: north|west serializes as 9 not 2");
  expect(!model.update_zone_forecast_profile("missing", 0x01, 0.5f, 0.3f, 4, 1.0f),
         "forecast profile: reject missing room");
  expect(!model.update_zone_forecast_profile_by_binding(2, 0, 0, 0.5f, 0.3f, 4, 1.0f),
         "forecast profile: reject missing node");
}

static void test_command_ledger() {
  CommandLedger ledger;
  ledger.append(command("cmd-1", 1000, 5000));
  ledger.append(command("cmd-2", 2000, 10000));

  expect(ledger.count() == 2, "ledger: append increments count");
  expect(ledger.count_result(CommandResult::PENDING) == 2, "ledger: pending count");
  expect(ledger.expire_pending(5999) == 0, "ledger: no early expiry");
  expect(ledger.expire_pending(6000) == 1, "ledger: first command expires at deadline");
  expect(ledger.count_result(CommandResult::EXPIRED) == 1, "ledger: expired count");
  expect(std::strcmp(command_result_name(CommandResult::EXPIRED), "expired") == 0,
         "ledger: result name");
  expect(std::strcmp(command_result_name(CommandResult::BLOCKED_STALE), "blocked_stale") == 0 &&
             std::strcmp(command_result_name(CommandResult::BLOCKED_UNREACHABLE), "blocked_unreachable") == 0 &&
             std::strcmp(command_result_name(CommandResult::BLOCKED_UNTRUSTED), "blocked_untrusted") == 0,
         "ledger: blocked result names");
  expect(std::strcmp(command_result_name(CommandResult::FAILED), "failed") == 0,
         "ledger: failed result name");

  CommandRecord accepted = command("cmd-3", 3000, 10000);
  accepted.result = CommandResult::ACCEPTED;
  accepted.accepted_offset_c = 1.0f;
  accepted.clamp_applied = true;
  ledger.append(accepted);
  CommandRecord failed = command("cmd-failed", 3200, 10000);
  failed.result = CommandResult::FAILED;
  ledger.append(failed);
  expect(ledger.count_result(CommandResult::FAILED) == 1, "ledger: failed count");
  expect(ledger.count_clamped() == 1, "ledger: clamped count");

  const CommandRecord *latest = ledger.latest();
  expect(latest != nullptr && std::strcmp(latest->request_id, "cmd-failed") == 0, "ledger: latest record");
  expect(ledger.at(2) && ledger.at(2)->clamp_applied && ledger.at(2)->accepted_offset_c == 1.0f,
         "ledger: clamp result preserved");

  CommandRecord forecast = command("cmd-4", 100000, 3600000);
  std::strncpy(forecast.source, "forecast", sizeof(forecast.source) - 1);
  forecast.node_index = 1;
  forecast.zone_index = 3;
  std::strncpy(forecast.node_id, "v6-b", sizeof(forecast.node_id) - 1);
  std::strncpy(forecast.loop_id, "loop-v6-b-04", sizeof(forecast.loop_id) - 1);
  forecast.requested_offset_c = 0.5f;
  forecast.accepted_offset_c = 0.5f;
  forecast.result = CommandResult::ACCEPTED;
  ledger.append(forecast);
  const CommandRecord *active_latest = ledger.latest_active(110000);
  expect(active_latest != nullptr && std::strcmp(active_latest->request_id, "cmd-4") == 0,
         "ledger: latest active command skips inactive failures");
  expect(ledger.has_recent_similar("forecast", "v6-b", "loop-v6-b-04", 0.52f, 110000, 1800000, 0.05f),
         "ledger: detects recent similar forecast command");
  expect(!ledger.has_recent_similar("forecast", "v6-c", "loop-v6-c-04", 0.52f, 110000, 1800000, 0.05f),
         "ledger: reordered index cannot suppress a different physical loop");
  expect(!ledger.has_recent_similar("forecast", "v6-b", "loop-v6-b-04", 0.7f, 110000, 1800000, 0.05f),
         "ledger: allows materially different forecast offset");
  expect(!ledger.has_recent_similar("dashboard", "v6-b", "loop-v6-b-04", 0.52f, 110000, 1800000, 0.05f),
         "ledger: source separates forecast from dashboard");
  expect(!ledger.has_recent_similar("forecast", "v6-b", "loop-v6-b-04", 0.52f, 2000000, 1800000, 0.05f),
         "ledger: old similar command no longer blocks");

  float active_offset = 0.0f;
  expect(ledger.active_offset_for("forecast", 1, 3, 110000, &active_offset) &&
             active_offset > 0.49f && active_offset < 0.51f,
         "ledger: active offset by source");
  expect(!ledger.active_offset_for("forecast", 1, 3, 3700000, &active_offset),
         "ledger: expired active offset ignored");
  expect(ledger.latest_active(4000000) == nullptr,
         "ledger: latest active command ignores expired accepted commands");

  CommandOffsetResolution resolved = ledger.resolve_command_offset(1, 3, 110000);
  expect(resolved.has_forecast_offset && !resolved.has_manual_offset &&
             resolved.command_offset_c > 0.49f && resolved.command_offset_c < 0.51f &&
             std::strcmp(resolved.command_source, "forecast") == 0,
         "resolver: forecast offset used when manual is absent");

  CommandRecord manual = command("cmd-manual", 120000, 3600000);
  std::strncpy(manual.source, "dashboard", sizeof(manual.source) - 1);
  manual.node_index = 1;
  manual.zone_index = 3;
  manual.requested_offset_c = 0.9f;
  manual.accepted_offset_c = 0.8f;
  manual.result = CommandResult::ACCEPTED;
  ledger.append(manual);
  resolved = ledger.resolve_command_offset(1, 3, 130000);
  expect(resolved.has_manual_offset && resolved.has_forecast_offset &&
             resolved.manual_offset_c > 0.79f && resolved.manual_offset_c < 0.81f &&
             resolved.forecast_offset_c > 0.49f && resolved.forecast_offset_c < 0.51f &&
             resolved.command_offset_c > 0.79f && resolved.command_offset_c < 0.81f &&
             std::strcmp(resolved.command_source, "manual") == 0,
         "resolver: manual offset wins over forecast");

  resolved = ledger.resolve_command_offset(1, 3, 4000000);
  expect(!resolved.has_manual_offset && !resolved.has_forecast_offset &&
             resolved.command_offset_c == 0.0f &&
             std::strcmp(resolved.command_source, "none") == 0,
         "resolver: expired offsets resolve to none");
  expect(room_command_outcome(2, 2) == RoomCommandOutcome::ACCEPTED &&
             std::strcmp(room_command_outcome_name(room_command_outcome(2, 2)), "accepted") == 0,
         "room command: every required loop must accept for room success");
  expect(room_command_outcome(1, 2) == RoomCommandOutcome::PARTIAL &&
             room_command_outcome(0, 2) == RoomCommandOutcome::FAILED,
         "room command: one-node failure is deterministic partial application");

  TargetResolverInput target_input{};
  target_input.fallback_base_target_c = 18.0f;
  target_input.touch_target_c = 21.0f;
  target_input.touch_available = true;
  target_input.command = resolved;
  target_input.command.has_manual_offset = true;
  target_input.command.manual_offset_c = 1.0f;
  target_input.command.has_forecast_offset = true;
  target_input.command.forecast_offset_c = 0.6f;
  target_input.learned_modifier_c = 0.3f;
  target_input.learned_confident = true;
  TargetResolution target = HouseModel::resolve_target(target_input);
  expect(target.manual_modifier_c == 1.0f && target.distribution_modifier_c == 0.0f &&
             target.learned_modifier_c == 0.3f && target.dispatch_target_c > 22.29f &&
             target.dispatch_target_c < 22.31f,
         "resolver: manual wins without stacking forecast and learned remains bounded");
  target_input.touch_available = false;
  target_input.command = {};
  target_input.learned_modifier_c = 0.0f;
  target = HouseModel::resolve_target(target_input);
  expect(std::strcmp(target.base_source, "fallback") == 0 && target.dispatch_target_c == 18.0f,
         "resolver: stale Touch uses persistent fallback base");
  target_input.touch_available = true;
  target_input.touch_target_c = 24.0f;
  target_input.dispatch_max_c = 25.0f;
  target_input.command.has_manual_offset = true;
  target_input.command.manual_offset_c = 10.0f;
  target = HouseModel::resolve_target(target_input);
  expect(target.pre_v6_target_c >= 33.9f && target.dispatch_target_c == 25.0f,
         "resolver: dispatch envelope clamps before V6 safety clamp");

  CommandRecord blocked = command("cmd-blocked", 5000000, 1000);
  blocked.result = CommandResult::BLOCKED_UNTRUSTED;
  ledger.append(blocked);
  expect(ledger.count_blocked() == 1, "ledger: blocked count");
}

static void test_ledger_ring_capacity() {
  CommandLedger ledger;
  for (size_t i = 0; i < LEDGER_CAPACITY + 5; i++) {
    char id[20];
    std::snprintf(id, sizeof(id), "cmd-%02u", static_cast<unsigned>(i));
    ledger.append(command(id, static_cast<uint32_t>(i * 1000), 10000));
  }

  expect(ledger.count() == LEDGER_CAPACITY, "ledger: ring count capped");
  const CommandRecord *latest = ledger.latest();
  expect(latest != nullptr && std::strcmp(latest->request_id, "cmd-36") == 0,
         "ledger: latest survives wrap");
}

static void test_ledger_persisted_state_roundtrip() {
  CommandLedger ledger;
  ledger.set_boot_id(0x101U);
  CommandRecord first = command("cmd-a", 1000, 5000);
  first.result = CommandResult::ACCEPTED;
  ledger.append(first);
  ledger.append(command("cmd-b", 2000, 5000));

  PersistedLedger state{};
  expect(ledger.export_state(&state), "ledger persist: export succeeds");
  expect(state.magic == PERSISTED_LEDGER_MAGIC && state.version == PERSISTED_LEDGER_VERSION,
         "ledger persist: magic and version set");
  expect(state.boot_id == 0x101U, "ledger persist: current boot id set");

  CommandLedger restored;
  expect(restored.import_state(state, 0x101U), "ledger persist: same-boot import succeeds");
  expect(restored.count() == 2, "ledger persist: count restored");
  expect(restored.count_result(CommandResult::ACCEPTED) == 1, "ledger persist: result restored");
  const CommandRecord *latest = restored.latest();
  expect(latest != nullptr && std::strcmp(latest->request_id, "cmd-b") == 0,
         "ledger persist: latest restored");

  state.version = 99;
  expect(!restored.import_state(state, 0x101U), "ledger persist: reject invalid version");
}

static void test_ledger_reboot_safe_expiry() {
  CommandLedger ledger;
  ledger.set_boot_id(0xA11CEU);
  CommandRecord active = command("cmd-active", 1000, 60000);
  active.result = CommandResult::ACCEPTED;
  active.created_at_epoch_s = 1735689600;
  active.expires_at_epoch_s = 1735689660;
  ledger.append(active);
  expect(ledger.latest_active(2000) != nullptr, "ledger expiry: command active within one boot");

  PersistedLedger state{};
  expect(ledger.export_state(&state), "ledger expiry: export before reboot succeeds");
  CommandLedger rebooted;
  expect(rebooted.import_state(state, 0xB007U, 1735689610),
         "ledger expiry: reboot imports historical records");
  expect(rebooted.count_result(CommandResult::EXPIRED) == 1 && rebooted.latest_active(2000) == nullptr,
         "ledger expiry: reboot before deadline expires prior-boot command");

  CommandLedger invalid_clock_reboot;
  expect(invalid_clock_reboot.import_state(state, 0xC10CU, 0),
         "ledger expiry: invalid-clock reboot imports historical records");
  expect(invalid_clock_reboot.latest_active(1) == nullptr,
         "ledger expiry: invalid-clock reboot still expires prior-boot command");

  CommandLedger wrapping;
  wrapping.set_boot_id(0xD00DU);
  wrapping.append(command("cmd-wrap", UINT32_MAX - 100U, 200U));
  expect(wrapping.expire_pending(50U) == 0, "ledger expiry: millisecond wrap does not expire early");
  expect(wrapping.expire_pending(100U) == 1, "ledger expiry: millisecond wrap expires at deadline");

  state.version = 1;
  expect(!rebooted.import_state(state, 0xB007U),
         "ledger expiry: old persisted ledger version is safely invalidated");
}

static void test_command_stable_target_identity() {
  CommandLedger ledger;
  ledger.set_boot_id(0x515U);
  CommandRecord record = command("cmd-stable", 1000, 60000);
  std::strncpy(record.room_id, "room-living", sizeof(record.room_id) - 1);
  std::strncpy(record.node_id, "v6-a", sizeof(record.node_id) - 1);
  std::strncpy(record.loop_id, "loop-v6-a-03", sizeof(record.loop_id) - 1);
  record.node_index = 0;
  record.zone_index = 2;
  ledger.append(record);

  HouseModel model;
  model.upsert_node("v6-a", "a", "", "v6", "test", NodeTrust::TRUSTED);
  model.upsert_node("v6-b", "b", "", "v6", "test", NodeTrust::TRUSTED);
  model.bind_zone("room-living", "Living", 0, 2);
  expect(model.remove_node("v6-a"), "command identity: remove first node");
  const CommandRecord *stored = ledger.latest();
  expect(stored != nullptr && std::strcmp(stored->node_id, "v6-a") == 0 &&
             std::strcmp(stored->loop_id, "loop-v6-a-03") == 0,
         "command identity: remove does not retarget history");
  expect(model.bind_zone("room-living", "Living", 0, 1), "command identity: rebind room");
  const ResolvedZone rebound = model.resolve_room("room-living");
  expect(rebound.binding != nullptr && std::strcmp(rebound.binding->loop_id, "loop-v6-b-02") == 0,
         "command identity: rebind assigns the new physical loop identity");
  expect(std::strcmp(stored->room_id, "room-living") == 0 && stored->zone_index == 2,
         "command identity: rebind does not rewrite target");
  expect(model.remove_node("v6-b"), "command identity: remove original loop host");
  expect(std::strcmp(stored->loop_id, "loop-v6-a-03") == 0,
         "command identity: removed loop remains historical target");

  PersistedLedger state{};
  ledger.export_state(&state);
  state.version = 2;
  CommandLedger incompatible;
  expect(!incompatible.import_state(state, 0x515U),
         "command identity: index-targeted ledger v2 is safely invalidated");
}

static void test_house_signal_contract_examples() {
  HouseModel model;
  model.set_house_weighting(HouseWeighting::AREA);
  model.set_min_contributing_rooms(1);
  model.set_minimum_area_coverage(0.5f);
  model.set_allow_degraded_manifolds(true);
  model.upsert_node("v6-a", "a.local", "", "lune-v6", "1.0", NodeTrust::TRUSTED);
  model.bind_zone("room-living", "Living", 0, 2);  // zone 3
  model.bind_zone("room-living", "Living", 0, 3);  // zone 4 — second loop, same room
  model.bind_zone("room-bedroom", "Bedroom", 0, 0);
  model.set_room_geometry("room-living", 48.0f, 1.0f, true);
  model.set_room_geometry("room-bedroom", 20.0f, 1.0f, true);
  model.set_room_sensor("room-living", model.zone(0)->loop_id);
  model.update_zone_comfort("room-living", 21.5f, 1);
  model.update_zone_comfort("room-bedroom", 20.5f, 1);
  model.update_zone_live("room-living", 21.0f, true, 21.5f, true, "idle", true, 1000);
  model.update_zone_live("room-bedroom", 20.0f, true, 20.5f, true, "idle", true, 1000);

  StrategySnapshot snap = model.strategy_snapshot();
  expect(snap.has_physical_temperature, "contract: publishable physical temp");
  expect(std::fabs(snap.physical_temperature_c - 20.71f) < 0.02f,
         "contract: living+bedroom area-weighted 20.71");
  expect(snap.has_house_target && std::fabs(snap.house_target_c - 21.21f) < 0.02f,
         "contract: house comfort target 21.21");
  // Two living loops must not double-count area.
  expect(std::fabs(snap.contributing_area_m2 - 68.0f) < 0.1f,
         "contract: living contributes once (not 96 m2)");

  model.bind_zone("room-guest", "Guest", 0, 1);
  model.set_room_geometry("room-guest", 12.0f, 1.0f, false);
  model.update_zone_live("room-guest", 19.0f, true, 19.0f, true, "idle", true, 1000);
  snap = model.strategy_snapshot();
  expect(std::fabs(snap.physical_temperature_c - 20.71f) < 0.02f,
         "contract: excluded guest leaves physical unchanged");
  expect(std::fabs(snap.excluded_area_m2 - 12.0f) < 0.1f, "contract: excluded_area_m2 = 12");

  const float before_priority = snap.physical_temperature_c;
  model.update_zone_comfort("room-living", 21.5f, 3);
  snap = model.strategy_snapshot();
  expect(std::fabs(snap.physical_temperature_c - before_priority) < 0.001f,
         "contract: priority change is invariant for physical temp");

  // Stale sensor → missing area, not fabricated.
  model.update_zone_live("room-bedroom", 20.0f, true, 20.5f, true, "idle", false, 2000);
  snap = model.strategy_snapshot();
  expect(snap.missing_area_m2 > 19.0f, "contract: stale bedroom counted as missing");
  expect(snap.contributing_rooms == 1, "contract: only living contributes when bedroom stale");
}

static void test_room_group_merge_and_sensor() {
  HouseModel model;
  model.upsert_node("v6-a", "a.local", "", "lune-v6", "1.0", NodeTrust::TRUSTED);
  model.upsert_node("v6-b", "b.local", "", "lune-v6", "1.0", NodeTrust::TRUSTED);
  expect(model.bind_zone("room-a", "A", 0, 0), "merge: bind room-a");
  expect(model.bind_zone("room-b", "B", 1, 1), "merge: bind room-b");
  const char *loop_b = model.zone(1)->loop_id;
  expect(model.create_room("room-living", "Living"), "merge: create living");
  expect(model.move_group_to_room(model.zone(0)->loop_id, "room-living"), "merge: move a→living");
  expect(model.move_group_to_room(loop_b, "room-living"), "merge: move b→living");
  expect(model.zone(0) != nullptr && std::strcmp(model.zone(0)->room_id, "room-living") == 0,
         "merge: group a now in living");
  expect(model.zone(1) != nullptr && std::strcmp(model.zone(1)->room_id, "room-living") == 0,
         "merge: group b now in living");
  expect(model.delete_room_if_empty("room-a"), "merge: empty room-a deleted");
  expect(model.delete_room_if_empty("room-b"), "merge: empty room-b deleted");
  expect(model.set_room_sensor("room-living", loop_b), "merge: sensor set to b");
  const LogicalRoom *living = model.room_by_id("room-living");
  expect(living != nullptr && std::strcmp(sensor_loop_id(*living), loop_b) == 0,
         "merge: sensor_loop_id is b");
  ResolvedRoomLoop loops[4]{};
  expect(model.resolve_room_loops("room-living", loops, 4) == 2,
         "merge: living owns two group-primary loops");
}

static void test_unassigned_primary_can_be_reclaimed() {
  HouseModel model;
  model.upsert_node("v6-a", "a.local", "", "lune-v6", "1.0", NodeTrust::TRUSTED);
  expect(model.bind_zone("v61-z1", "Kontor", 0, 0), "reclaim: initial bind");
  expect(model.update_zone_live_by_binding(0, 0, 22.9f, true, 22.0f, true, "idle", true, 1000,
                                           20.0f, true),
         "reclaim: live temperature attached");
  // Simulate a primary that was detached (briefly secondary) and left unassigned
  // while telemetry continued to flow — the live Controllers UI shows "unused".
  expect(model.update_zone_group_membership(0, 0, 2, 0x03, false),
         "reclaim: incorrectly marked secondary detaches room");
  expect(model.zone(0) != nullptr && model.zone(0)->room_id[0] == '\0' &&
             model.zone(0)->unassigned,
         "reclaim: binding has empty room_id after detach");
  expect(model.update_zone_group_membership(0, 0, 1, 0x01, true),
         "reclaim: corrected back to primary");
  expect(model.zone(0) != nullptr && model.zone(0)->room_id[0] == '\0' &&
             model.zone(0)->unassigned && !model.zone(0)->is_group_secondary,
         "reclaim: primary again but still unassigned (stuck state)");
  expect(model.update_zone_live_by_binding(0, 0, 22.9f, true, 22.0f, true, "idle", true, 2000,
                                           20.0f, true),
         "reclaim: live updates still succeed without a room");

  expect(model.bind_zone_with_source("v61-z1", "Kontor", 0, 0, ZoneNameSource::V6),
         "reclaim: bind claims the unassigned primary");
  expect(model.zone(0) != nullptr && std::strcmp(model.zone(0)->room_id, "v61-z1") == 0 &&
             !model.zone(0)->unassigned && !model.zone(0)->is_group_secondary,
         "reclaim: room_id restored");
  const ResolvedZone restored = model.resolve_room("v61-z1");
  expect(restored.binding != nullptr && restored.live != nullptr &&
             restored.live->has_temperature &&
             std::fabs(restored.live->temperature_c - 22.9f) < 0.001f,
         "reclaim: live temperature preserved through reclaim");
}

static void test_group_secondary_excluded() {
  HouseModel model;
  model.set_min_contributing_rooms(1);
  model.set_minimum_area_coverage(0.5f);
  model.set_allow_degraded_manifolds(true);
  model.upsert_node("v6-a", "a.local", "", "lune-v6", "1.0", NodeTrust::TRUSTED);
  model.bind_zone("living", "Living", 0, 0);
  model.bind_zone("living", "Living", 0, 1);  // will become secondary
  model.set_room_geometry("living", 40.0f, 1.0f, true);
  model.update_zone_live_by_binding(0, 0, 21.0f, true, 21.0f, true, "idle", true, 1000);
  model.update_zone_live_by_binding(0, 1, 19.0f, true, 21.0f, true, "idle", true, 1000);
  expect(model.update_zone_group_membership(0, 0, 1, 0x03, true), "group: primary marked");
  expect(model.update_zone_group_membership(0, 1, 1, 0x03, false), "group: secondary marked");
  expect(model.zone(1) != nullptr && model.zone(1)->is_group_secondary && model.zone(1)->unassigned,
         "group: secondary detached and unassigned");
  ResolvedRoomLoop loops[4]{};
  expect(model.resolve_room_loops("living", loops, 4) == 1, "group: only primary dispatched");
  expect(model.bind_unassigned_group(0, 2, "Hall", ZoneNameSource::V6), "group: unassigned propose");
  expect(model.unassigned_group_count() >= 1, "group: unassigned count");
  model.update_zone_heat_demand(0, 0, "DEMAND", 0.95f, 1200, false, HeatRecommendation::RAISE, true);
  RoomHeatDemand demand = model.room_heat_demand("living");
  expect(demand.any_fresh && demand.recommendation == HeatRecommendation::RAISE,
         "group: room heat demand raise");
}

static void test_persisted_state_v13_migration() {
  HouseModel model;
  model.upsert_node("v6-a", "a.local", "", "lune-v6", "1.0", NodeTrust::TRUSTED);
  model.bind_zone("living", "Living", 0, 0);
  PersistedState state{};
  expect(model.export_state(&state), "persist: export v15");
  expect(state.version == PERSISTED_STATE_VERSION, "persist: version is 15");
  state.version = PERSISTED_STATE_VERSION_V13;
  state.zones[0].unassigned = true;  // corrupt; migration resets
  state.zones[0].is_group_secondary = true;
  HouseModel imported;
  expect(imported.import_state_v13(state), "persist: import v13 succeeds");
  expect(imported.zone(0) != nullptr && !imported.zone(0)->unassigned &&
             !imported.zone(0)->is_group_secondary,
         "persist: v13 migration clears new flags");
  expect(imported.import_state(state) == false, "persist: v13 rejected by v15 importer");
}

static void test_weather_seed_survives_v6_refresh() {
  HouseModel model;
  model.upsert_node("v6-a", "a.local", "", "lune-v6", "1.0", NodeTrust::TRUSTED);
  model.bind_zone("living", "Living", 0, 0);
  expect(model.seed_room_weather_from_v6("living", 0.8f, 0.2f), "weather: first seed succeeds");
  expect(!model.seed_room_weather_from_v6("living", 0.1f, 0.9f), "weather: second seed rejected");
  const LogicalRoom *room = model.room_by_id("living");
  expect(room != nullptr && room->weather_seeded && room->wind_exposure > 0.79f &&
             room->solar_gain < 0.21f,
         "weather: Touch values retained after rejected re-seed");
  // Poll-style refresh must not import weather.
  expect(model.update_zone_forecast_profile_by_binding(0, 0, 0x05, 0.1f, 0.9f, 4, 1.5f, false,
                                                       false),
         "weather: poll refresh without importing weather");
  room = model.room_by_id("living");
  expect(room != nullptr && room->wind_exposure > 0.79f && room->solar_gain < 0.21f,
         "weather: room SoT survives V6-style refresh");
}

static void test_same_node_merge_rejected() {
  HouseModel model;
  model.upsert_node("v6-a", "a.local", "", "lune-v6", "1.0", NodeTrust::TRUSTED);
  model.upsert_node("v6-b", "b.local", "", "lune-v6", "1.0", NodeTrust::TRUSTED);
  model.bind_zone("living", "Living", 0, 0);
  model.bind_unassigned_group(0, 1, "Bath", ZoneNameSource::V6);
  model.bind_unassigned_group(1, 0, "Hall", ZoneNameSource::V6);
  const ZoneBinding *bath = nullptr;
  const ZoneBinding *hall = nullptr;
  for (size_t i = 0; i < model.zone_count(); i++) {
    const ZoneBinding *z = model.zone(i);
    if (z == nullptr)
      continue;
    if (z->zone_index == 1 && z->node_index == 0)
      bath = z;
    if (z->zone_index == 0 && z->node_index == 1)
      hall = z;
  }
  expect(bath != nullptr && hall != nullptr, "same-node: bindings present");
  char reason[48]{};
  expect(!model.move_group_to_room(bath->loop_id, "living", reason, sizeof(reason)),
         "same-node: merge on same V6 rejected");
  expect(std::strcmp(reason, "same_node_merge_on_v6") == 0, "same-node: reject reason");
  reason[0] = '\0';
  expect(model.move_group_to_room(hall->loop_id, "living", reason, sizeof(reason)),
         "cross-node: merge accepted");
}

static void test_ua_physics_mirror_and_index_room() {
  HouseModel model;
  model.upsert_node("v6-a", "a.local", "", "lune-v6", "1.0", NodeTrust::TRUSTED);
  model.bind_zone("living", "Living", 0, 0);
  FloorMirror floor{};
  floor.unset = false;
  std::strncpy(floor.slab_type, "cast_concrete", sizeof(floor.slab_type) - 1);
  std::strncpy(floor.covering, "carpet", sizeof(floor.covering) - 1);
  floor.r_m2k_per_w = 0.18f;
  floor.c_slab_kwh_per_k = 1.2f;
  expect(model.apply_v6_physics_mirror(0, 0, 0x05, 21.5f, floor, 18.0f, 0.0f, 18.0f, 0.0f, 0, 1,
                                       "AA:BB:CC:DD:EE:FF", "", "g1", nullptr),
         "physics: mirror applied");
  const LogicalRoom *room = model.room_by_id("living");
  expect(room != nullptr && room->ua_prior_w_per_k > 17.0f && room->exterior_walls_union == 0x05 &&
             !room->floor_unset && room->total_area_m2 > 21.0f,
         "physics: room aggregates from V6");
  model.set_outdoor_temp_c(-5.0f);
  model.set_odin_heat_loss_w_per_k(0.0f);
  char reason[32]{};
  expect(!model.ua_prior_fully_provisioned(reason, sizeof(reason)),
         "calibration: blocked without Odin");
  expect(std::strcmp(reason, "odin_unavailable") == 0, "calibration: odin reason");
  model.set_odin_heat_loss_w_per_k(20.0f);
  expect(model.ua_prior_fully_provisioned(reason, sizeof(reason)), "calibration: full coverage");
  const auto snap = model.strategy_snapshot();
  expect(std::strcmp(snap.ua_calibration_reason, "ready") == 0 ||
             std::strcmp(snap.ua_calibration_status, "uncalibrated") == 0,
         "calibration: ready to write before apply");
  float ub = 0, uw = 0, cs = 0;
  expect(model.compute_house_calibration(&ub, &uw, &cs, reason, sizeof(reason)),
         "calibration: compute succeeds");
  model.apply_house_calibration(ub, uw, cs);
  const auto snap2 = model.strategy_snapshot();
  expect(std::strcmp(snap2.ua_calibration_status, "calibrated") == 0, "calibration: status");
  expect(snap2.index_room_id[0] != '\0' && snap2.index_flow_req_c > 0.0f,
         "index room: computed");
  expect(std::strcmp(snap2.index_reason, "high_r") == 0, "index room: high covering R");
}

static void test_room_physics_contract_vectors() {
  using namespace physics;
  // Contract §12 vector 1
  const float c1 = c_slab_per_m2("cast_concrete", 0.0f);
  expect(std::fabs(c1 - 0.0464f) < 0.0002f, "vec1: c_slab_per_m2");
  const float ua1 = ua_prior_w_per_k(21.5f, 0x03);
  expect(std::fabs(ua1 - 27.95f) < 0.05f, "vec1: ua_prior");
  const float cz1 = c_zone_kwh_per_k(21.5f, c1);
  expect(std::fabs(cz1 - 2.288f) < 0.01f, "vec1: c_zone");
  const float tau1 = tau_prior_h(cz1, ua1);
  expect(std::fabs(tau1 - 81.8f) < 0.5f, "vec1: tau > 80 is valid");
  // Vector 2 — τ well above old 80 h cap
  const float c2 = c_slab_per_m2("screed", 0.0f);
  const float ua2 = ua_prior_w_per_k(12.0f, 0);
  const float cz2 = c_zone_kwh_per_k(12.0f, c2);
  const float tau2 = tau_prior_h(cz2, ua2);
  expect(std::fabs(ua2 - 6.0f) < 0.05f, "vec2: ua_prior");
  expect(tau2 > 80.0f, "vec2: tau above legacy absolute cap");
  // Vector 5 — thickness ignored for dry_plates
  bool ignored = false;
  const float c5 = c_slab_per_m2("dry_plates", 5.0f, &ignored);
  expect(ignored && std::fabs(c5 - 0.005f) < 0.0001f, "vec5: thickness_ignored");
  // unset covering → parquet_laminate R
  expect(std::fabs(covering_r_m2k_per_w("unset") - 0.080f) < 0.001f,
         "covering unset → parquet_laminate");
  // Calibration clamp stop (§8 / §12)
  const auto blocked = calibrate_house(0.225f, 50.0f, 53.45f, 1.466f, 48.5f);
  expect(!blocked.ok && std::strcmp(blocked.reason, "inconsistent_physics") == 0,
         "calibration: scale > 4.0 blocks write");
  // Confidence formula (§5.1 examples)
  expect(std::fabs(ua_confidence(13, 0.05f) - 0.59f) < 0.02f, "conf: 13d cv0.05");
  expect(std::fabs(ua_confidence(16, 0.20f) - 0.61f) < 0.02f, "conf: 16d cv0.20");
  expect(should_write_ua_learned(30.0f, 0.0f, 0.7f, 0.0f, 1000, 0), "ua-learned: first write");
  expect(!should_write_ua_learned(30.3f, 30.0f, 0.7f, 0.7f, 1000, 900),
         "ua-learned: skip small change within 7d");
  expect(should_write_ua_learned(35.0f, 30.0f, 0.7f, 0.5f, 1000, 900),
         "ua-learned: write on confidence gate cross");
}

int main() {
  test_node_staleness();
  test_update_node_host_keeps_identity();
  test_node_unreachable_marks_zones_stale();
  test_node_trust_updates();
  test_zone_registry();
  test_zone_name_sources();
  test_rediscovered_physical_zone_reactivates_binding();
  test_remove_node_remaps_zones();
  test_logical_room_multiple_loops();
  test_persisted_state_roundtrip();
  test_zone_live_state();
  test_effective_comfort_resolver();
  test_room_spread_is_not_a_fault();
  test_strategy_snapshot();
  test_zone_forecast_profile();
  test_command_ledger();
  test_ledger_ring_capacity();
  test_ledger_persisted_state_roundtrip();
  test_ledger_reboot_safe_expiry();
  test_command_stable_target_identity();
  test_house_signal_contract_examples();
  test_room_group_merge_and_sensor();
  test_unassigned_primary_can_be_reclaimed();
  test_group_secondary_excluded();
  test_persisted_state_v13_migration();
  test_weather_seed_survives_v6_refresh();
  test_same_node_merge_rejected();
  test_ua_physics_mirror_and_index_room();
  test_room_physics_contract_vectors();

  if (g_failures > 0) {
    std::printf("%d test(s) FAILED.\n", g_failures);
    return EXIT_FAILURE;
  }
  std::printf("All tests passed.\n");
  return EXIT_SUCCESS;
}
