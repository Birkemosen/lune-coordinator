#include "coordinator_model.h"
#include "room_physics.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace lune_touch {

static void copy_text_(char *dest, size_t dest_size, const char *src) {
  if (dest_size == 0)
    return;
  if (src == nullptr)
    src = "";
  std::strncpy(dest, src, dest_size - 1);
  dest[dest_size - 1] = '\0';
}

static bool same_text_(const char *a, const char *b) {
  if (a == nullptr || b == nullptr)
    return false;
  return std::strcmp(a, b) == 0;
}

static void make_loop_id_(char *out, size_t out_size, const char *node_id, size_t zone_index) {
  if (out == nullptr || out_size == 0)
    return;
  std::snprintf(out, out_size, "loop-%s-%02u", node_id != nullptr ? node_id : "",
                static_cast<unsigned>(zone_index + 1));
}

static float clamp_float_(float value, float lo, float hi, float fallback) {
  if (!std::isfinite(value))
    return fallback;
  if (value < lo)
    return lo;
  if (value > hi)
    return hi;
  return value;
}

// Touch comfort at/below the API floor is almost always "never commissioned"
// (0 clamped to 5, or a leftover 5). Prefer the live V6 setpoint when it looks
// like a real comfort value so the house target is not dragged to ~5°C.
static bool comfort_looks_unset_(float comfort_c) {
  return !std::isfinite(comfort_c) || comfort_c <= 5.0f + 1e-3f;
}

static float resolve_room_target_c_(float comfort_c, const EffectiveComfort &effective,
                                    const ZoneLiveState *live) {
  if (!comfort_looks_unset_(comfort_c))
    return effective.setpoint_c;
  if (live != nullptr && live->has_setpoint && std::isfinite(live->setpoint_c) &&
      live->setpoint_c >= 10.0f && live->setpoint_c <= 35.0f)
    return live->setpoint_c;
  if (std::isfinite(comfort_c) && comfort_c >= 5.0f - 1e-3f)
    return effective.setpoint_c;
  return 21.0f;
}

static void seed_comfort_from_live_(ZoneBinding *zone, LogicalRoom *room, float setpoint_c,
                                    bool has_setpoint) {
  if (zone == nullptr || !has_setpoint || !std::isfinite(setpoint_c) || setpoint_c < 10.0f ||
      setpoint_c > 35.0f)
    return;
  if (comfort_looks_unset_(zone->comfort_setpoint_c))
    zone->comfort_setpoint_c = setpoint_c;
  if (room != nullptr && comfort_looks_unset_(room->comfort_setpoint_c))
    room->comfort_setpoint_c = setpoint_c;
}

static float rolling_average_(float current, float sample, uint16_t count) {
  if (count == 0 || current <= 0.0f)
    return sample;
  const uint16_t effective_count = count > 200 ? 200 : count;
  return current + (sample - current) / static_cast<float>(effective_count + 1);
}

LogicalRoom *HouseModel::find_room_(const char *room_id) {
  if (room_id == nullptr || room_id[0] == '\0')
    return nullptr;
  for (size_t i = 0; i < room_count_; i++) {
    if (same_text_(rooms_[i].room_id, room_id))
      return &rooms_[i];
  }
  return nullptr;
}

const LogicalRoom *HouseModel::find_room_(const char *room_id) const {
  return const_cast<HouseModel *>(this)->find_room_(room_id);
}

size_t HouseModel::room_index_(const char *room_id) const {
  if (room_id == nullptr || room_id[0] == '\0')
    return room_count_;
  for (size_t i = 0; i < room_count_; i++) {
    if (same_text_(rooms_[i].room_id, room_id))
      return i;
  }
  return room_count_;
}

LogicalRoom *HouseModel::ensure_room_(const char *room_id, const char *room_name,
                                      ZoneNameSource source) {
  LogicalRoom *existing = find_room_(room_id);
  if (existing != nullptr) {
    if (room_name != nullptr && room_name[0] != '\0')
      copy_text_(existing->room_name, sizeof(existing->room_name), room_name);
    existing->name_source = source;
    existing->enabled = true;
    // Older registries can contain zero geometry because this information was
    // once required during commissioning. Use a neutral equal-weight model so
    // ordinary comfort and schedule changes remain valid after an upgrade.
    if (!std::isfinite(existing->total_area_m2) || existing->total_area_m2 <= 0.0f)
      existing->total_area_m2 = 1.0f;
    if (!std::isfinite(existing->physical_weight) || existing->physical_weight <= 0.0f)
      existing->physical_weight = 1.0f;
    return existing;
  }
  if (room_count_ >= MAX_HOUSE_ROOMS)
    return nullptr;
  LogicalRoom &room = rooms_[room_count_++];
  copy_text_(room.room_id, sizeof(room.room_id), room_id);
  copy_text_(room.room_name, sizeof(room.room_name), room_name);
  room.name_source = source;
  room.enabled = true;
  room.total_area_m2 = 1.0f;
  room.physical_weight = 1.0f;
  // import_state_common_ memsets the room array; do not rely on in-class defaults.
  room.include_in_house_temperature = true;
  room_revisions_[room_count_ - 1] = 1;
  return &room;
}

ZoneBinding *HouseModel::find_binding_by_loop_(const char *loop_id) {
  if (loop_id == nullptr || loop_id[0] == '\0')
    return nullptr;
  for (size_t i = 0; i < zone_count_; i++) {
    if (same_text_(zones_[i].loop_id, loop_id))
      return &zones_[i];
  }
  return nullptr;
}

const ZoneBinding *HouseModel::find_binding_by_loop_(const char *loop_id) const {
  return const_cast<HouseModel *>(this)->find_binding_by_loop_(loop_id);
}

size_t HouseModel::find_binding_index_(size_t node_index, size_t zone_index) const {
  for (size_t i = 0; i < zone_count_; i++) {
    if (zones_[i].node_index == node_index && zones_[i].zone_index == zone_index)
      return i;
  }
  return zone_count_;
}

void HouseModel::ensure_room_sensor_(LogicalRoom *room) {
  if (room == nullptr)
    return;
  if (room->primary_loop_id[0] != '\0') {
    const ZoneBinding *existing = find_binding_by_loop_(room->primary_loop_id);
    if (existing != nullptr && existing->enabled && !existing->is_group_secondary &&
        !existing->unassigned && same_text_(existing->room_id, room->room_id))
      return;
  }
  room->primary_loop_id[0] = '\0';
  for (size_t i = 0; i < zone_count_; i++) {
    if (!zones_[i].enabled || zones_[i].is_group_secondary || zones_[i].unassigned)
      continue;
    if (!same_text_(zones_[i].room_id, room->room_id))
      continue;
    copy_text_(room->primary_loop_id, sizeof(room->primary_loop_id), zones_[i].loop_id);
    return;
  }
}

void HouseModel::detach_binding_from_room_(size_t zone_slot) {
  if (zone_slot >= zone_count_)
    return;
  ZoneBinding &zone = zones_[zone_slot];
  LogicalRoom *room = find_room_(zone.room_id);
  zone.room_id[0] = '\0';
  zone.unassigned = true;
  zone.is_group_secondary = true;
  live_[zone_slot].room_id[0] = '\0';
  if (room != nullptr && same_text_(room->primary_loop_id, zone.loop_id)) {
    room->primary_loop_id[0] = '\0';
    ensure_room_sensor_(room);
  }
}

int HouseModel::upsert_node(const char *node_id, const char *hostname, const char *fallback_ip,
                            const char *model, const char *firmware, NodeTrust trust) {
  if (node_id == nullptr || node_id[0] == '\0')
    return -1;

  for (size_t i = 0; i < node_count_; i++) {
    if (same_text_(nodes_[i].node_id, node_id)) {
      if (nodes_[i].name[0] == '\0')
        copy_text_(nodes_[i].name, sizeof(nodes_[i].name), node_id);
      copy_text_(nodes_[i].hostname, sizeof(nodes_[i].hostname), hostname);
      copy_text_(nodes_[i].fallback_ip, sizeof(nodes_[i].fallback_ip), fallback_ip);
      copy_text_(nodes_[i].model, sizeof(nodes_[i].model), model);
      copy_text_(nodes_[i].firmware, sizeof(nodes_[i].firmware), firmware);
      nodes_[i].trust = trust;
      return static_cast<int>(i);
    }
  }

  if (node_count_ >= MAX_NODES)
    return -1;

  PairedNode &node = nodes_[node_count_];
  copy_text_(node.node_id, sizeof(node.node_id), node_id);
  copy_text_(node.name, sizeof(node.name), node_id);
  copy_text_(node.hostname, sizeof(node.hostname), hostname);
  copy_text_(node.fallback_ip, sizeof(node.fallback_ip), fallback_ip);
  copy_text_(node.model, sizeof(node.model), model);
  copy_text_(node.firmware, sizeof(node.firmware), firmware);
  node.trust = trust;
  node.reachable = false;
  node.last_seen_ms = 0;
  return static_cast<int>(node_count_++);
}

bool HouseModel::update_node_name(const char *node_id, const char *name) {
  if (node_id == nullptr || node_id[0] == '\0' || name == nullptr || name[0] == '\0')
    return false;
  for (size_t i = 0; i < node_count_; i++) {
    if (!same_text_(nodes_[i].node_id, node_id))
      continue;
    copy_text_(nodes_[i].name, sizeof(nodes_[i].name), name);
    return true;
  }
  return false;
}

int HouseModel::update_node_host(const char *node_id, const char *hostname, const char *fallback_ip) {
  if (node_id == nullptr || node_id[0] == '\0')
    return -1;
  const bool has_hostname = hostname != nullptr && hostname[0] != '\0';
  const bool has_ip = fallback_ip != nullptr && fallback_ip[0] != '\0';
  if (!has_hostname && !has_ip)
    return -1;
  for (size_t i = 0; i < node_count_; i++) {
    if (!same_text_(nodes_[i].node_id, node_id))
      continue;
    copy_text_(nodes_[i].hostname, sizeof(nodes_[i].hostname), hostname);
    copy_text_(nodes_[i].fallback_ip, sizeof(nodes_[i].fallback_ip), fallback_ip);
    // Unknown until the next poll proves the new address.
    nodes_[i].reachable = false;
    return static_cast<int>(i);
  }
  return -1;
}

bool HouseModel::remove_node(const char *node_id) {
  if (node_id == nullptr || node_id[0] == '\0')
    return false;

  size_t remove_index = MAX_NODES;
  for (size_t i = 0; i < node_count_; i++) {
    if (same_text_(nodes_[i].node_id, node_id)) {
      remove_index = i;
      break;
    }
  }
  if (remove_index >= node_count_)
    return false;

  for (size_t i = remove_index; i + 1 < node_count_; i++)
    nodes_[i] = nodes_[i + 1];
  nodes_[node_count_ - 1] = {};
  node_count_--;

  for (size_t i = 0; i < zone_count_; i++) {
    if (!zones_[i].enabled)
      continue;
    if (zones_[i].node_index == remove_index) {
      zones_[i].enabled = false;
    } else if (zones_[i].node_index > remove_index) {
      zones_[i].node_index--;
    }
  }
  return true;
}

bool HouseModel::mark_node_seen(size_t node_index, uint32_t now_ms) {
  if (node_index >= node_count_)
    return false;
  nodes_[node_index].last_seen_ms = now_ms;
  nodes_[node_index].reachable = true;
  return true;
}

bool HouseModel::mark_node_unreachable(size_t node_index, uint32_t now_ms) {
  if (node_index >= node_count_)
    return false;
  nodes_[node_index].reachable = false;
  for (size_t i = 0; i < zone_count_; i++) {
    if (!zones_[i].enabled || zones_[i].node_index != node_index)
      continue;
    copy_text_(live_[i].status, sizeof(live_[i].status), "stale");
    live_[i].fresh = false;
    live_[i].updated_at_ms = now_ms;
  }
  return true;
}

bool HouseModel::update_node_metadata(size_t node_index, const char *model, const char *firmware,
                                      const char *fallback_ip) {
  if (node_index >= node_count_)
    return false;
  if (model != nullptr && model[0] != '\0')
    copy_text_(nodes_[node_index].model, sizeof(nodes_[node_index].model), model);
  if (firmware != nullptr && firmware[0] != '\0')
    copy_text_(nodes_[node_index].firmware, sizeof(nodes_[node_index].firmware), firmware);
  if (fallback_ip != nullptr && fallback_ip[0] != '\0')
    copy_text_(nodes_[node_index].fallback_ip, sizeof(nodes_[node_index].fallback_ip), fallback_ip);
  return true;
}

bool HouseModel::update_node_identity(size_t node_index, const char *pairing_fingerprint) {
  if (node_index >= node_count_)
    return false;
  if (pairing_fingerprint != nullptr && pairing_fingerprint[0] != '\0')
    copy_text_(nodes_[node_index].pairing_fingerprint,
               sizeof(nodes_[node_index].pairing_fingerprint), pairing_fingerprint);
  return true;
}

bool HouseModel::update_node_trust(const char *node_id, NodeTrust trust) {
  if (node_id == nullptr || node_id[0] == '\0')
    return false;
  for (size_t i = 0; i < node_count_; i++) {
    if (!same_text_(nodes_[i].node_id, node_id))
      continue;
    if (trust == NodeTrust::TRUSTED && nodes_[i].pairing_fingerprint[0] == '\0')
      return false;
    nodes_[i].trust = trust;
    return true;
  }
  return false;
}

bool HouseModel::is_node_stale(size_t node_index, uint32_t now_ms) const {
  if (node_index >= node_count_)
    return true;
  const PairedNode &node = nodes_[node_index];
  if (!node.reachable || node.last_seen_ms == 0)
    return true;
  if (now_ms < node.last_seen_ms)
    return false;
  return (now_ms - node.last_seen_ms) > node_stale_after_ms_;
}

bool HouseModel::bind_zone(const char *room_id, const char *room_name, size_t node_index, size_t zone_index) {
  return bind_zone_with_source(room_id, room_name, node_index, zone_index, ZoneNameSource::TOUCH);
}

bool HouseModel::bind_zone_with_source(const char *room_id, const char *room_name, size_t node_index,
                                       size_t zone_index, ZoneNameSource source) {
  if (room_id == nullptr || room_id[0] == '\0' || node_index >= node_count_ || zone_index >= ZONES_PER_NODE)
    return false;

  for (size_t i = 0; i < zone_count_; i++) {
    if (zones_[i].node_index != node_index || zones_[i].zone_index != zone_index)
      continue;
    ZoneBinding &zone = zones_[i];
    const bool was_disabled = !zone.enabled;
    const bool claimable = was_disabled || zone.unassigned || zone.room_id[0] == '\0';
    if (claimable) {
      // Disabled, unassigned, or empty-room bindings are claimable. This recovers
      // primaries that were detached (e.g. briefly marked secondary) and then
      // kept receiving live telemetry without ever re-attaching a room — the
      // dashboard shows those as "unused" despite a live temperature.
      if (zone.room_id[0] == '\0')
        copy_text_(zone.room_id, sizeof(zone.room_id), room_id);
      if (zone.room_name[0] == '\0' && room_name != nullptr && room_name[0] != '\0')
        copy_text_(zone.room_name, sizeof(zone.room_name), room_name);

      if (source == ZoneNameSource::TOUCH || zone.name_source != ZoneNameSource::TOUCH) {
        if (room_name != nullptr && room_name[0] != '\0')
          copy_text_(zone.room_name, sizeof(zone.room_name), room_name);
        zone.name_source = source;
      }

      LogicalRoom *room = ensure_room_(zone.room_id, zone.room_name, zone.name_source);
      if (room == nullptr)
        return false;
      copy_text_(zone.room_name, sizeof(zone.room_name), room->room_name);
      if (zone.loop_id[0] == '\0')
        make_loop_id_(zone.loop_id, sizeof(zone.loop_id), nodes_[node_index].node_id, zone_index);
      copy_text_(zone.node_id, sizeof(zone.node_id), nodes_[node_index].node_id);
      zone.enabled = true;
      zone.commissioned = true;
      zone.unassigned = false;
      zone.is_group_secondary = false;
      if (room->primary_loop_id[0] == '\0')
        copy_text_(room->primary_loop_id, sizeof(room->primary_loop_id), zone.loop_id);
      if (was_disabled)
        live_[i] = {};
      copy_text_(live_[i].room_id, sizeof(live_[i].room_id), zone.room_id);
      live_[i].is_group_primary = true;
      return true;
    }
    // Polling may rediscover the same binding, but another logical room may
    // never claim this physical valve loop.
    if (!same_text_(zone.room_id, room_id))
      return false;
    if (source == ZoneNameSource::TOUCH || zone.name_source != ZoneNameSource::TOUCH) {
      copy_text_(zone.room_name, sizeof(zone.room_name), room_name);
      zone.name_source = source;
      LogicalRoom *room = ensure_room_(room_id, room_name, source);
      if (room == nullptr)
        return false;
    }
    return true;
  }

  LogicalRoom *room = ensure_room_(room_id, room_name, source);
  if (room == nullptr || zone_count_ >= MAX_HOUSE_ZONES)
    return false;

  const size_t index = zone_count_++;
  ZoneBinding &zone = zones_[index];
  copy_text_(zone.room_id, sizeof(zone.room_id), room_id);
  copy_text_(zone.room_name, sizeof(zone.room_name), room->room_name);
  make_loop_id_(zone.loop_id, sizeof(zone.loop_id), nodes_[node_index].node_id, zone_index);
  copy_text_(zone.node_id, sizeof(zone.node_id), nodes_[node_index].node_id);
  zone.node_index = static_cast<uint8_t>(node_index);
  zone.zone_index = static_cast<uint8_t>(zone_index);
  zone.name_source = source;
  zone.enabled = true;
  zone.commissioned = true;
  zone.unassigned = false;
  zone.is_group_secondary = false;
  if (room->primary_loop_id[0] == '\0')
    copy_text_(room->primary_loop_id, sizeof(room->primary_loop_id), zone.loop_id);
  live_[index] = {};
  copy_text_(live_[index].room_id, sizeof(live_[index].room_id), room_id);
  live_[index].is_group_primary = true;
  return true;
}

bool HouseModel::remove_loop(const char *loop_id) {
  if (loop_id == nullptr || loop_id[0] == '\0')
    return false;
  for (size_t i = 0; i < zone_count_; i++) {
    if (!same_text_(zones_[i].loop_id, loop_id))
      continue;
    LogicalRoom *room = find_room_(zones_[i].room_id);
    zones_[i].enabled = false;
    zones_[i].commissioned = false;
    history_[i] = {};
    live_[i] = {};
    if (room != nullptr && same_text_(room->primary_loop_id, loop_id)) {
      room->primary_loop_id[0] = '\0';
      ensure_room_sensor_(room);
    }
    return true;
  }
  return false;
}

bool HouseModel::create_room(const char *room_id, const char *name) {
  if (room_id == nullptr || room_id[0] == '\0')
    return false;
  if (find_room_(room_id) != nullptr)
    return false;
  LogicalRoom *room = ensure_room_(room_id, name != nullptr && name[0] != '\0' ? name : room_id,
                                   ZoneNameSource::TOUCH);
  return room != nullptr;
}

bool HouseModel::delete_room_if_empty(const char *room_id) {
  const size_t index = room_index_(room_id);
  if (index >= room_count_)
    return false;
  for (size_t i = 0; i < zone_count_; i++) {
    if (zones_[i].enabled && !zones_[i].unassigned && same_text_(zones_[i].room_id, room_id))
      return false;
  }
  for (size_t i = index; i + 1 < room_count_; i++) {
    rooms_[i] = rooms_[i + 1];
    room_revisions_[i] = room_revisions_[i + 1];
  }
  rooms_[room_count_ - 1] = {};
  room_revisions_[room_count_ - 1] = 0;
  room_count_--;
  return true;
}

bool HouseModel::move_group_to_room(const char *loop_id, const char *room_id) {
  return move_group_to_room(loop_id, room_id, nullptr, 0);
}

bool HouseModel::move_group_to_room(const char *loop_id, const char *room_id, char *reject_reason,
                                    size_t reject_capacity) {
  auto set_reject = [&](const char *msg) {
    if (reject_reason != nullptr && reject_capacity > 0 && msg != nullptr) {
      std::strncpy(reject_reason, msg, reject_capacity - 1);
      reject_reason[reject_capacity - 1] = '\0';
    }
  };
  ZoneBinding *binding = find_binding_by_loop_(loop_id);
  LogicalRoom *room = find_room_(room_id);
  if (binding == nullptr || room == nullptr || !binding->enabled || binding->is_group_secondary) {
    set_reject("invalid_group_or_room");
    return false;
  }
  // T3: groups from the same V6 node cannot be composed on Touch.
  for (size_t i = 0; i < zone_count_; i++) {
    if (!zones_[i].enabled || zones_[i].is_group_secondary || zones_[i].unassigned)
      continue;
    if (!same_text_(zones_[i].room_id, room_id))
      continue;
    if (same_text_(zones_[i].loop_id, loop_id))
      continue;
    if (same_text_(zones_[i].node_id, binding->node_id)) {
      set_reject("same_node_merge_on_v6");
      return false;
    }
  }
  LogicalRoom *old_room = find_room_(binding->room_id);
  const bool was_sensor =
      old_room != nullptr && same_text_(old_room->primary_loop_id, binding->loop_id);
  copy_text_(binding->room_id, sizeof(binding->room_id), room->room_id);
  copy_text_(binding->room_name, sizeof(binding->room_name), room->room_name);
  binding->unassigned = false;
  binding->comfort_setpoint_c = room->comfort_setpoint_c;
  binding->comfort_bias_c = room->comfort_bias_c;
  binding->schedule_setpoint_c = room->schedule_setpoint_c;
  binding->schedule_start_min = room->schedule_start_min;
  binding->schedule_end_min = room->schedule_end_min;
  binding->schedule_day_mask = room->schedule_day_mask;
  binding->priority = room->priority;
  binding->schedule_enabled = room->schedule_enabled;
  // Propagate Touch-owned weather onto the binding for forecast consumers.
  binding->wind_exposure = room->wind_exposure;
  binding->solar_gain = room->solar_gain;
  const size_t slot = static_cast<size_t>(binding - zones_);
  copy_text_(live_[slot].room_id, sizeof(live_[slot].room_id), room->room_id);
  if (was_sensor && old_room != nullptr) {
    old_room->primary_loop_id[0] = '\0';
    ensure_room_sensor_(old_room);
  }
  if (room->primary_loop_id[0] == '\0')
    copy_text_(room->primary_loop_id, sizeof(room->primary_loop_id), binding->loop_id);
  const size_t rev_index = room_index_(room_id);
  if (rev_index < room_count_)
    room_revisions_[rev_index]++;
  refresh_room_physics_aggregates(room_id);
  if (old_room != nullptr)
    refresh_room_physics_aggregates(old_room->room_id);
  return true;
}

bool HouseModel::set_room_sensor(const char *room_id, const char *loop_id) {
  LogicalRoom *room = find_room_(room_id);
  const ZoneBinding *binding = find_binding_by_loop_(loop_id);
  if (room == nullptr || binding == nullptr || !binding->enabled || binding->is_group_secondary ||
      binding->unassigned || !same_text_(binding->room_id, room_id))
    return false;
  copy_text_(room->primary_loop_id, sizeof(room->primary_loop_id), binding->loop_id);
  const size_t rev_index = room_index_(room_id);
  if (rev_index < room_count_)
    room_revisions_[rev_index]++;
  return true;
}

bool HouseModel::bind_unassigned_group(size_t node_index, size_t zone_index, const char *name,
                                       ZoneNameSource source) {
  if (node_index >= node_count_ || zone_index >= ZONES_PER_NODE)
    return false;
  const size_t existing = find_binding_index_(node_index, zone_index);
  if (existing < zone_count_) {
    ZoneBinding &zone = zones_[existing];
    zone.enabled = true;
    zone.commissioned = true;
    zone.is_group_secondary = false;
    if (zone.room_id[0] == '\0' || zone.unassigned) {
      zone.unassigned = true;
      zone.room_id[0] = '\0';
      if (name != nullptr && name[0] != '\0')
        copy_text_(zone.room_name, sizeof(zone.room_name), name);
      zone.name_source = source;
    }
    live_[existing].is_group_primary = true;
    return true;
  }
  if (zone_count_ >= MAX_HOUSE_ZONES)
    return false;
  const size_t index = zone_count_++;
  ZoneBinding &zone = zones_[index];
  zone = {};
  make_loop_id_(zone.loop_id, sizeof(zone.loop_id), nodes_[node_index].node_id, zone_index);
  copy_text_(zone.node_id, sizeof(zone.node_id), nodes_[node_index].node_id);
  if (name != nullptr && name[0] != '\0')
    copy_text_(zone.room_name, sizeof(zone.room_name), name);
  zone.node_index = static_cast<uint8_t>(node_index);
  zone.zone_index = static_cast<uint8_t>(zone_index);
  zone.name_source = source;
  zone.enabled = true;
  zone.commissioned = true;
  zone.unassigned = true;
  zone.is_group_secondary = false;
  live_[index] = {};
  live_[index].is_group_primary = true;
  return true;
}

bool HouseModel::set_room_geometry(const char *room_id, float total_area_m2, float physical_weight,
                                   bool include_in_house_temperature) {
  LogicalRoom *room = find_room_(room_id);
  if (room == nullptr || !std::isfinite(total_area_m2) || total_area_m2 < 0.0f ||
      !std::isfinite(physical_weight) || physical_weight < 0.0f)
    return false;
  room->total_area_m2 = total_area_m2;
  // 0 means "default multiplier" — not area.
  room->physical_weight = physical_weight > 0.0f ? physical_weight : 1.0f;
  room->include_in_house_temperature = include_in_house_temperature;
  return true;
}

bool HouseModel::set_loop_served_area(const char *loop_id, float served_area_m2) {
  if (loop_id == nullptr || !std::isfinite(served_area_m2) || served_area_m2 < 0.0f)
    return false;
  for (size_t i = 0; i < zone_count_; i++) {
    if (same_text_(zones_[i].loop_id, loop_id)) {
      zones_[i].served_area_m2 = served_area_m2;
      return true;
    }
  }
  return false;
}

bool HouseModel::update_zone_name_from_v6_by_binding(size_t node_index, size_t zone_index,
                                                     const char *room_name) {
  if (node_index >= node_count_ || zone_index >= ZONES_PER_NODE || room_name == nullptr ||
      room_name[0] == '\0')
    return false;
  for (size_t i = 0; i < zone_count_; i++) {
    if (!zones_[i].enabled || zones_[i].node_index != node_index || zones_[i].zone_index != zone_index)
      continue;
    if (zones_[i].name_source == ZoneNameSource::TOUCH)
      return false;
    copy_text_(zones_[i].room_name, sizeof(zones_[i].room_name), room_name);
    zones_[i].name_source = ZoneNameSource::V6;
    // The dashboard renders logical rooms from the separate room list. Keep
    // that aggregate in sync as well, otherwise a V6 name updates the loop
    // but the UI continues to show the old generated room name.
    LogicalRoom *room = find_room_(zones_[i].room_id);
    bool touch_owned = false;
    if (room != nullptr) {
      for (size_t candidate = 0; candidate < zone_count_; candidate++) {
        if (zones_[candidate].enabled && same_text_(zones_[candidate].room_id, room->room_id) &&
            zones_[candidate].name_source == ZoneNameSource::TOUCH) {
          touch_owned = true;
          break;
        }
      }
    }
    if (room != nullptr && !touch_owned) {
      copy_text_(room->room_name, sizeof(room->room_name), room_name);
      room->name_source = ZoneNameSource::V6;
    }
    return true;
  }
  return false;
}

bool HouseModel::update_zone_forecast_profile_by_binding(size_t node_index, size_t zone_index,
                                                         uint8_t exterior_walls, float wind_exposure,
                                                         float solar_gain, uint8_t thermal_lead_h,
                                                         float max_offset_c,
                                                         bool import_exterior_walls,
                                                         bool import_weather) {
  if (node_index >= node_count_ || zone_index >= ZONES_PER_NODE)
    return false;
  for (size_t i = 0; i < zone_count_; i++) {
    if (!zones_[i].enabled || zones_[i].node_index != node_index || zones_[i].zone_index != zone_index)
      continue;
    if (import_exterior_walls)
      zones_[i].exterior_walls = exterior_walls & 0x0F;
    if (import_weather) {
      zones_[i].wind_exposure = clamp_float_(wind_exposure, 0.0f, 1.0f, zones_[i].wind_exposure);
      zones_[i].solar_gain = clamp_float_(solar_gain, 0.0f, 1.0f, zones_[i].solar_gain);
    }
    zones_[i].thermal_lead_h = thermal_lead_h > 0 ? thermal_lead_h : zones_[i].thermal_lead_h;
    if (zones_[i].thermal_lead_h > 24)
      zones_[i].thermal_lead_h = 24;
    zones_[i].max_offset_c = clamp_float_(max_offset_c, 0.0f, 5.0f, zones_[i].max_offset_c);
    // Mirror weather onto the logical room when Touch is SoT and weather is being set.
    if (import_weather) {
      LogicalRoom *room = find_room_(zones_[i].room_id);
      if (room != nullptr && !room->weather_seeded) {
        room->wind_exposure = zones_[i].wind_exposure;
        room->solar_gain = zones_[i].solar_gain;
      }
    }
    return true;
  }
  return false;
}

bool HouseModel::update_zone_forecast_profile(const char *room_id, uint8_t exterior_walls,
                                              float wind_exposure, float solar_gain,
                                              uint8_t thermal_lead_h, float max_offset_c) {
  if (room_id == nullptr || room_id[0] == '\0')
    return false;
  // Touch UI edits: walls go to V6 via write-through; weather stays on the room.
  LogicalRoom *room = find_room_(room_id);
  if (room != nullptr) {
    room->wind_exposure = clamp_float_(wind_exposure, 0.0f, 1.0f, room->wind_exposure);
    room->solar_gain = clamp_float_(solar_gain, 0.0f, 1.0f, room->solar_gain);
    room->weather_seeded = true;
  }
  bool any = false;
  for (size_t i = 0; i < zone_count_; i++) {
    if (!same_text_(zones_[i].room_id, room_id))
      continue;
    // import_exterior_walls=true stores the local pending value until V6 confirms;
    // import_weather=true syncs binding from room SoT.
    if (update_zone_forecast_profile_by_binding(zones_[i].node_index, zones_[i].zone_index,
                                                exterior_walls, wind_exposure, solar_gain,
                                                thermal_lead_h, max_offset_c, true, true))
      any = true;
  }
  return any;
}

bool HouseModel::seed_room_weather_from_v6(const char *room_id, float wind_exposure,
                                           float solar_gain) {
  LogicalRoom *room = find_room_(room_id);
  if (room == nullptr)
    return false;
  if (room->weather_seeded)
    return false;
  room->wind_exposure = clamp_float_(wind_exposure, 0.0f, 1.0f, 0.5f);
  room->solar_gain = clamp_float_(solar_gain, 0.0f, 1.0f, 0.3f);
  room->weather_seeded = true;
  for (size_t i = 0; i < zone_count_; i++) {
    if (!zones_[i].enabled || !same_text_(zones_[i].room_id, room_id))
      continue;
    zones_[i].wind_exposure = room->wind_exposure;
    zones_[i].solar_gain = room->solar_gain;
  }
  return true;
}

bool HouseModel::set_room_weather(const char *room_id, float wind_exposure, float solar_gain) {
  LogicalRoom *room = find_room_(room_id);
  if (room == nullptr || !std::isfinite(wind_exposure) || !std::isfinite(solar_gain) ||
      wind_exposure < 0.0f || wind_exposure > 1.0f || solar_gain < 0.0f || solar_gain > 1.0f)
    return false;
  room->wind_exposure = wind_exposure;
  room->solar_gain = solar_gain;
  room->weather_seeded = true;
  for (size_t i = 0; i < zone_count_; i++) {
    if (!zones_[i].enabled || !same_text_(zones_[i].room_id, room_id))
      continue;
    zones_[i].wind_exposure = wind_exposure;
    zones_[i].solar_gain = solar_gain;
  }
  return true;
}

bool HouseModel::apply_v6_physics_mirror(size_t node_index, size_t zone_index,
                                         uint8_t exterior_walls, float area_m2,
                                         const FloorMirror &floor, float ua_prior,
                                         float ua_learned, float ua_effective,
                                         float ua_confidence, uint16_t ua_observed_days,
                                         uint32_t v6_revision, const char *ble_mac,
                                         const char *sensor_id, const char *group_id,
                                         bool *conflict_out) {
  if (conflict_out != nullptr)
    *conflict_out = false;
  for (size_t i = 0; i < zone_count_; i++) {
    if (!zones_[i].enabled || zones_[i].node_index != node_index ||
        zones_[i].zone_index != zone_index)
      continue;
    ZoneBinding &z = zones_[i];
    // Conflict: Touch pending local walls edit vs V6 change between polls.
    // When walls_from_v6 is already true and values diverge after a Touch edit
    // that hasn't been acknowledged, V6 wins.
    const bool had_v6 = z.walls_from_v6;
    const uint8_t prev_walls = z.exterior_walls;
    if (had_v6 && (prev_walls != (exterior_walls & 0x0F)) && z.physics_conflict) {
      // already flagged
    } else if (had_v6 && prev_walls != (exterior_walls & 0x0F)) {
      // Detect if room still has pending Touch walls that differ — treat as conflict.
      LogicalRoom *room = find_room_(z.room_id);
      if (room != nullptr && !room->walls_migrated &&
          (room->exterior_walls_union & 0x0F) != 0 &&
          (room->exterior_walls_union & 0x0F) != (exterior_walls & 0x0F) &&
          (room->exterior_walls_union & 0x0F) == prev_walls) {
        z.physics_conflict = true;
        if (conflict_out != nullptr)
          *conflict_out = true;
      }
    }
    z.exterior_walls = exterior_walls & 0x0F;
    z.walls_from_v6 = true;
    if (std::isfinite(area_m2) && area_m2 > 0.0f)
      z.v6_area_m2 = area_m2;
    z.floor = floor;
    if (std::isfinite(ua_prior) && ua_prior > 0.0f)
      z.ua_prior_w_per_k = ua_prior;
    if (std::isfinite(ua_learned) && ua_learned > 0.0f)
      z.ua_learned_w_per_k = ua_learned;
    if (std::isfinite(ua_effective) && ua_effective > 0.0f)
      z.ua_effective_w_per_k = ua_effective;
    else if (std::isfinite(ua_prior) && ua_prior > 0.0f)
      z.ua_effective_w_per_k = ua_prior;
    if (std::isfinite(ua_confidence))
      z.ua_confidence = clamp_float_(ua_confidence, 0.0f, 1.0f, z.ua_confidence);
    z.ua_observed_days = ua_observed_days;
    z.v6_data_revision = v6_revision;
    if (ble_mac != nullptr)
      copy_text_(z.ble_mac, sizeof(z.ble_mac), ble_mac);
    if (sensor_id != nullptr)
      copy_text_(z.sensor_id, sizeof(z.sensor_id), sensor_id);
    if (group_id != nullptr)
      copy_text_(z.group_id, sizeof(z.group_id), group_id);
    refresh_room_physics_aggregates(z.room_id);
    return true;
  }
  return false;
}

bool HouseModel::refresh_room_physics_aggregates(const char *room_id) {
  LogicalRoom *room = find_room_(room_id);
  if (room == nullptr)
    return false;
  float ua_prior = 0.0f;
  float ua_learned = 0.0f;
  float ua_eff = 0.0f;
  float area = 0.0f;
  float conf = 0.0f;
  uint16_t days = 0;
  uint8_t walls = 0;
  bool floor_unset = false;
  bool conflict = false;
  bool any_primary = false;
  char ble_ref[24]{};
  bool ble_mismatch = false;
  size_t node_bits = 0;
  size_t group_count = 0;

  for (size_t i = 0; i < zone_count_; i++) {
    if (!zones_[i].enabled || zones_[i].is_group_secondary || zones_[i].unassigned)
      continue;
    if (!same_text_(zones_[i].room_id, room_id))
      continue;
    any_primary = true;
    group_count++;
    walls |= zones_[i].exterior_walls & 0x0F;
    if (zones_[i].v6_area_m2 > 0.0f)
      area += zones_[i].v6_area_m2;
    ua_prior += zones_[i].ua_prior_w_per_k > 0.0f ? zones_[i].ua_prior_w_per_k : 0.0f;
    ua_learned += zones_[i].ua_learned_w_per_k > 0.0f ? zones_[i].ua_learned_w_per_k : 0.0f;
    ua_eff += zones_[i].ua_effective_w_per_k > 0.0f ? zones_[i].ua_effective_w_per_k
                                                    : (zones_[i].ua_prior_w_per_k > 0.0f
                                                           ? zones_[i].ua_prior_w_per_k
                                                           : 0.0f);
    if (zones_[i].ua_confidence > conf)
      conf = zones_[i].ua_confidence;
    if (zones_[i].ua_observed_days > days)
      days = zones_[i].ua_observed_days;
    if (zones_[i].floor.unset ||
        !physics::floor_provisioned(zones_[i].floor.slab_type, zones_[i].floor.covering))
      floor_unset = true;
    if (zones_[i].physics_conflict)
      conflict = true;
    if (zones_[i].node_index < MAX_NODES)
      node_bits |= (size_t{1} << zones_[i].node_index);
    const char *mac = zones_[i].ble_mac[0] != '\0' ? zones_[i].ble_mac : zones_[i].sensor_id;
    if (mac != nullptr && mac[0] != '\0') {
      if (ble_ref[0] == '\0')
        copy_text_(ble_ref, sizeof(ble_ref), mac);
      else if (!same_text_(ble_ref, mac))
        ble_mismatch = true;
    }
  }
  if (!any_primary)
    return true;

  room->exterior_walls_union = walls;
  room->ua_prior_w_per_k = ua_prior;
  room->ua_learned_w_per_k = ua_learned;
  room->ua_effective_w_per_k = ua_eff;
  room->ua_confidence = conf;
  room->ua_observed_days = days;
  room->floor_unset = floor_unset;
  room->physics_conflict = conflict;
  // BLE mismatch only matters for cross-manifold rooms (≥2 distinct nodes).
  size_t distinct_nodes = 0;
  for (size_t b = 0; b < MAX_NODES; b++) {
    if (node_bits & (size_t{1} << b))
      distinct_nodes++;
  }
  room->ble_sensor_mismatch = distinct_nodes >= 2 && ble_mismatch;
  if (area > 0.0f)
    room->total_area_m2 = area;
  // Drive house weighting from V6 effective UA when present.
  if (ua_eff > 0.0f)
    room->ua_w_per_k = ua_eff;
  (void)group_count;
  return true;
}

bool HouseModel::refresh_all_room_physics_aggregates() {
  bool any = false;
  for (size_t i = 0; i < room_count_; i++)
    any = refresh_room_physics_aggregates(rooms_[i].room_id) || any;
  return any;
}

bool HouseModel::mark_walls_migrated(const char *room_id) {
  LogicalRoom *room = find_room_(room_id);
  if (room == nullptr)
    return false;
  room->walls_migrated = true;
  return true;
}

bool HouseModel::walls_need_migration(const char *room_id) const {
  const LogicalRoom *room = find_room_(room_id);
  if (room == nullptr || room->walls_migrated)
    return false;
  // Need migration when Touch has a non-zero walls union that isn't yet confirmed from V6.
  if (room->exterior_walls_union == 0)
    return false;
  for (size_t i = 0; i < zone_count_; i++) {
    if (!zones_[i].enabled || zones_[i].is_group_secondary || !same_text_(zones_[i].room_id, room_id))
      continue;
    if (!zones_[i].walls_from_v6)
      return true;
  }
  return false;
}

bool HouseModel::ua_prior_fully_provisioned(char *reason, size_t reason_capacity) const {
  auto set_reason = [&](const char *msg) {
    if (reason != nullptr && reason_capacity > 0 && msg != nullptr) {
      std::strncpy(reason, msg, reason_capacity - 1);
      reason[reason_capacity - 1] = '\0';
    }
  };
  size_t included = 0;
  for (size_t i = 0; i < room_count_; i++) {
    const LogicalRoom &room = rooms_[i];
    if (!room.include_in_house_temperature)
      continue;
    included++;
    bool has_primary = false;
    for (size_t z = 0; z < zone_count_; z++) {
      if (zones_[z].enabled && !zones_[z].is_group_secondary && !zones_[z].unassigned &&
          same_text_(zones_[z].room_id, room.room_id)) {
        has_primary = true;
        break;
      }
    }
    if (!has_primary) {
      set_reason("missing_rooms");
      return false;
    }
    if (room.floor_unset) {
      set_reason("floor_unset");
      return false;
    }
    if (!(room.ua_prior_w_per_k > 0.0f) && !(room.total_area_m2 > 0.0f)) {
      set_reason("missing_rooms");
      return false;
    }
  }
  if (included == 0) {
    set_reason("missing_rooms");
    return false;
  }
  if (!(odin_heat_loss_w_per_k_ > 0.0f)) {
    set_reason("odin_unavailable");
    return false;
  }
  set_reason("ok");
  return true;
}

float HouseModel::ua_prior_sum_included_w_per_k() const {
  float sum = 0.0f;
  for (size_t i = 0; i < room_count_; i++) {
    if (!rooms_[i].include_in_house_temperature)
      continue;
    if (rooms_[i].ua_prior_w_per_k > 0.0f)
      sum += rooms_[i].ua_prior_w_per_k;
    else if (rooms_[i].total_area_m2 > 0.0f)
      sum += geometry_ua_w_per_k(rooms_[i].total_area_m2);
  }
  return sum;
}

float HouseModel::c_slab_sum_included_kwh_per_k() const {
  float sum = 0.0f;
  for (size_t i = 0; i < zone_count_; i++) {
    if (!zones_[i].enabled || zones_[i].is_group_secondary || zones_[i].unassigned)
      continue;
    const LogicalRoom *room = find_room_(zones_[i].room_id);
    if (room == nullptr || !room->include_in_house_temperature)
      continue;
    if (zones_[i].floor.c_slab_kwh_per_k > 0.0f)
      sum += zones_[i].floor.c_slab_kwh_per_k;
    else if (zones_[i].v6_area_m2 > 0.0f) {
      const float cpm = physics::c_slab_per_m2(zones_[i].floor.slab_type,
                                               zones_[i].floor.active_thickness_cm);
      sum += zones_[i].v6_area_m2 * cpm;
    }
  }
  return sum;
}

float HouseModel::area_sum_included_m2() const {
  float sum = 0.0f;
  for (size_t i = 0; i < room_count_; i++) {
    if (!rooms_[i].include_in_house_temperature)
      continue;
    if (rooms_[i].total_area_m2 > 0.0f)
      sum += rooms_[i].total_area_m2;
  }
  return sum;
}

bool HouseModel::compute_house_calibration(float *u_base_out, float *u_wall_out,
                                           float *c_struct_out, char *reason,
                                           size_t reason_capacity) const {
  char gate[32]{};
  if (!ua_prior_fully_provisioned(gate, sizeof(gate))) {
    if (reason != nullptr && reason_capacity > 0) {
      std::strncpy(reason, gate, reason_capacity - 1);
      reason[reason_capacity - 1] = '\0';
    }
    return false;
  }
  const auto cal = physics::calibrate_house(
      odin_heat_loss_w_per_k_ / 1000.0f, odin_tau_h_, ua_prior_sum_included_w_per_k(),
      c_slab_sum_included_kwh_per_k(), area_sum_included_m2());
  if (reason != nullptr && reason_capacity > 0) {
    std::strncpy(reason, cal.reason, reason_capacity - 1);
    reason[reason_capacity - 1] = '\0';
  }
  if (!cal.ok)
    return false;
  if (u_base_out != nullptr)
    *u_base_out = cal.u_base;
  if (u_wall_out != nullptr)
    *u_wall_out = cal.u_wall;
  if (c_struct_out != nullptr)
    *c_struct_out = cal.c_struct;
  return true;
}

void HouseModel::apply_house_calibration(float u_base, float u_wall, float c_struct) {
  house_u_base_ = u_base;
  house_u_wall_ = u_wall;
  house_c_struct_ = c_struct;
  house_calibrated_ = true;
}

float HouseModel::required_flow_temp_c(float ua_eff, float area_m2, float r_m2k_per_w,
                                       float t_target_c, float t_out_c, float design_dt_c) {
  if (!std::isfinite(ua_eff) || ua_eff <= 0.0f || !std::isfinite(area_m2) || area_m2 <= 0.0f ||
      !std::isfinite(t_target_c) || !std::isfinite(t_out_c))
    return t_target_c;
  const float dT = t_target_c - t_out_c;
  if (dT <= 0.0f)
    return t_target_c;
  const float Q = ua_eff * dT;  // W
  const float R = (std::isfinite(r_m2k_per_w) && r_m2k_per_w > 0.0f) ? r_m2k_per_w : 0.05f;
  const float t_surface = t_target_c + (Q * R) / area_m2;
  const float dt = (std::isfinite(design_dt_c) && design_dt_c > 0.0f) ? design_dt_c : 5.0f;
  return t_surface + dt * 0.5f;
}

bool HouseModel::learn_ua_from_delivery(const char *room_id, float outdoor_c, float *ua_out,
                                        float *confidence_out, uint16_t *days_out,
                                        uint32_t now_epoch_s) {
  LogicalRoom *room = find_room_(room_id);
  if (room == nullptr || !std::isfinite(outdoor_c))
    return false;
  const ZoneBinding *primary = find_binding_by_loop_(room->primary_loop_id);
  if (primary == nullptr)
    return false;
  size_t live_index = find_binding_index_(primary->node_index, primary->zone_index);
  if (live_index >= zone_count_)
    return false;
  const ZoneLiveState &live = live_[live_index];
  if (!live.fresh || !live.has_temperature || !std::isfinite(live.temperature_c))
    return false;
  const float dT = live.temperature_c - outdoor_c;
  // Contract §5.1: house-average ΔT ≥ 8 K for a qualifying day.
  if (dT < 8.0f)
    return false;
  if (!(room->delivered_kwh_today > 0.05f))
    return false;
  const float hours = 12.0f;
  const float mean_w = (room->delivered_kwh_today * 1000.0f) / hours;
  const float ua_est = mean_w / dT;
  if (!std::isfinite(ua_est) || ua_est < 1.0f || ua_est > 500.0f)
    return false;
  const float prior = room->ua_prior_w_per_k > 0.0f ? room->ua_prior_w_per_k
                                                    : geometry_ua_w_per_k(room->total_area_m2);
  if (!physics::ua_learned_in_band(ua_est, prior))
    return false;
  uint16_t days = room->ua_observed_days;
  if (days < 60)
    days = static_cast<uint16_t>(days + 1);
  const float conf = physics::ua_confidence(days, 0.05f);
  const float prev_conf = room->ua_confidence;
  const float prev_learned = room->ua_learned_w_per_k;
  uint32_t last_write = 0;
  for (size_t i = 0; i < zone_count_; i++) {
    if (!zones_[i].enabled || zones_[i].is_group_secondary ||
        !same_text_(zones_[i].room_id, room_id))
      continue;
    if (ua_learned_last_write_epoch_s_[i] > last_write)
      last_write = ua_learned_last_write_epoch_s_[i];
  }
  const bool first = !(prev_learned > 0.0f);
  const float alpha = first ? 1.0f : 0.35f;
  const float blended = first ? ua_est : (prev_learned * (1.0f - alpha) + ua_est * alpha);
  if (!physics::should_write_ua_learned(blended, prev_learned, conf, prev_conf, now_epoch_s,
                                        last_write))
    return false;
  room->ua_learned_w_per_k = blended;
  room->ua_confidence = conf;
  room->ua_observed_days = days;
  room->ua_effective_w_per_k = physics::ua_effective_w_per_k(prior, blended, conf, days);
  room->ua_w_per_k = room->ua_effective_w_per_k;
  for (size_t i = 0; i < zone_count_; i++) {
    if (!zones_[i].enabled || zones_[i].is_group_secondary ||
        !same_text_(zones_[i].room_id, room_id))
      continue;
    const float share = (zones_[i].v6_area_m2 > 0.0f && room->total_area_m2 > 0.0f)
                            ? zones_[i].v6_area_m2 / room->total_area_m2
                            : 1.0f;
    zones_[i].ua_learned_w_per_k = blended * share;
    zones_[i].ua_confidence = conf;
    zones_[i].ua_observed_days = days;
    const float loop_prior =
        zones_[i].ua_prior_w_per_k > 0.0f ? zones_[i].ua_prior_w_per_k : prior * share;
    zones_[i].ua_effective_w_per_k =
        physics::ua_effective_w_per_k(loop_prior, zones_[i].ua_learned_w_per_k, conf, days);
    if (now_epoch_s != 0)
      ua_learned_last_write_epoch_s_[i] = now_epoch_s;
  }
  if (ua_out != nullptr)
    *ua_out = blended;
  if (confidence_out != nullptr)
    *confidence_out = conf;
  if (days_out != nullptr)
    *days_out = days;
  bump_weights_revision(now_epoch_s != 0 ? now_epoch_s * 1000UL : 0);
  return true;
}

bool HouseModel::update_zone_comfort(const char *room_id, float comfort_setpoint_c, uint8_t priority,
                                     float comfort_bias_c) {
  LogicalRoom *room = find_room_(room_id);
  if (room == nullptr)
    return false;
  room->comfort_setpoint_c = clamp_float_(comfort_setpoint_c, 5.0f, 35.0f,
                                          room->comfort_setpoint_c);
  room->comfort_bias_c = clamp_float_(comfort_bias_c, -3.0f, 3.0f, room->comfort_bias_c);
  room->priority = priority > 3 ? 3 : priority;
  bool updated = false;
  for (size_t i = 0; i < zone_count_; i++) {
    if (!zones_[i].enabled || !same_text_(zones_[i].room_id, room_id))
      continue;
    zones_[i].comfort_setpoint_c = room->comfort_setpoint_c;
    zones_[i].comfort_bias_c = room->comfort_bias_c;
    zones_[i].priority = room->priority;
    updated = true;
  }
  return updated;
}

bool HouseModel::update_zone_schedule(const char *room_id, bool enabled, uint8_t day_mask,
                                      uint16_t start_min, uint16_t end_min, float setpoint_c) {
  LogicalRoom *room = find_room_(room_id);
  if (room == nullptr)
    return false;
  if (start_min > 1439 || end_min > 1440 || start_min >= end_min)
    return false;
  day_mask &= 0x7F;
  if (enabled && day_mask == 0)
    return false;
  room->schedule_enabled = enabled;
  room->schedule_day_mask = day_mask;
  room->schedule_start_min = start_min;
  room->schedule_end_min = end_min;
  room->schedule_setpoint_c = clamp_float_(setpoint_c, 5.0f, 35.0f, room->schedule_setpoint_c);
  bool updated = false;
  for (size_t i = 0; i < zone_count_; i++) {
    if (!zones_[i].enabled || !same_text_(zones_[i].room_id, room_id))
      continue;
    zones_[i].schedule_enabled = room->schedule_enabled;
    zones_[i].schedule_day_mask = room->schedule_day_mask;
    zones_[i].schedule_start_min = room->schedule_start_min;
    zones_[i].schedule_end_min = room->schedule_end_min;
    zones_[i].schedule_setpoint_c = room->schedule_setpoint_c;
    updated = true;
  }
  return updated;
}

RoomUpdateResult HouseModel::apply_room_update(const char *room_id, const RoomUpdate &update,
                                               uint32_t *new_revision) {
  const size_t room_index = room_index_(room_id);
  if (room_index >= room_count_)
    return RoomUpdateResult::NOT_FOUND;
  if (update.expected_revision != room_revisions_[room_index])
    return RoomUpdateResult::STALE_REVISION;
  // A disabled schedule is not validated: rooms imported from V6 can carry an
  // uninitialised window (0–0, 0 °C), which used to reject every room edit
  // ("invalid room update"). Normalise it to the default window instead.
  RoomUpdate checked = update;
  if (!checked.schedule_enabled) {
    if (checked.schedule_start_min > 1439 || checked.schedule_end_min > 1440 ||
        checked.schedule_start_min >= checked.schedule_end_min) {
      checked.schedule_start_min = 360;
      checked.schedule_end_min = 1320;
    }
    if (!std::isfinite(checked.schedule_setpoint_c) || checked.schedule_setpoint_c < 5.0f ||
        checked.schedule_setpoint_c > 35.0f)
      checked.schedule_setpoint_c = 21.0f;
  }
  return apply_checked_room_update_(room_index, room_id, checked, new_revision);
}

RoomUpdateResult HouseModel::apply_checked_room_update_(size_t room_index, const char *room_id,
                                                        const RoomUpdate &update,
                                                        uint32_t *new_revision) {
  if (!std::isfinite(update.total_area_m2) || update.total_area_m2 <= 0.0f ||
      !std::isfinite(update.physical_weight) || update.physical_weight < 0.0f ||
      update.physical_weight > 10.0f ||
      !std::isfinite(update.comfort_setpoint_c) || update.comfort_setpoint_c < 5.0f ||
      update.comfort_setpoint_c > 35.0f || !std::isfinite(update.comfort_bias_c) ||
      update.comfort_bias_c < -3.0f || update.comfort_bias_c > 3.0f ||
      !std::isfinite(update.schedule_setpoint_c) || update.schedule_setpoint_c < 5.0f ||
      update.schedule_setpoint_c > 35.0f || update.priority > 3 || update.schedule_start_min > 1439 ||
      update.schedule_end_min > 1440 || update.schedule_start_min >= update.schedule_end_min ||
      (update.schedule_enabled && (update.schedule_day_mask & 0x7F) == 0) ||
      !std::isfinite(update.wind_exposure) || update.wind_exposure < 0.0f || update.wind_exposure > 1.0f ||
      !std::isfinite(update.solar_gain) || update.solar_gain < 0.0f || update.solar_gain > 1.0f ||
      update.thermal_lead_h == 0 || update.thermal_lead_h > 24 || !std::isfinite(update.max_offset_c) ||
      update.max_offset_c < 0.0f || update.max_offset_c > 5.0f)
    return RoomUpdateResult::INVALID;
  bool has_loop = false;
  for (size_t i = 0; i < zone_count_; i++)
    has_loop = has_loop || (zones_[i].enabled && same_text_(zones_[i].room_id, room_id));
  if (!has_loop)
    return RoomUpdateResult::NOT_FOUND;

  // All validation is complete before mutating the room or any of its loops.
  LogicalRoom &room = rooms_[room_index];
  room.total_area_m2 = update.total_area_m2;
  room.physical_weight = update.physical_weight > 0.0f ? update.physical_weight : 1.0f;
  room.include_in_house_temperature = update.include_in_house_temperature;
  room.comfort_setpoint_c = update.comfort_setpoint_c;
  room.comfort_bias_c = update.comfort_bias_c;
  room.priority = update.priority;
  room.schedule_enabled = update.schedule_enabled;
  room.schedule_day_mask = update.schedule_day_mask & 0x7F;
  room.schedule_start_min = update.schedule_start_min;
  room.schedule_end_min = update.schedule_end_min;
  room.schedule_setpoint_c = update.schedule_setpoint_c;
  room.wind_exposure = update.wind_exposure;
  room.solar_gain = update.solar_gain;
  room.weather_seeded = true;
  for (size_t i = 0; i < zone_count_; i++) {
    ZoneBinding &zone = zones_[i];
    if (!zone.enabled || !same_text_(zone.room_id, room_id))
      continue;
    zone.comfort_setpoint_c = room.comfort_setpoint_c;
    zone.comfort_bias_c = room.comfort_bias_c;
    zone.priority = room.priority;
    zone.schedule_enabled = room.schedule_enabled;
    zone.schedule_day_mask = room.schedule_day_mask;
    zone.schedule_start_min = room.schedule_start_min;
    zone.schedule_end_min = room.schedule_end_min;
    zone.schedule_setpoint_c = room.schedule_setpoint_c;
    // Walls: store pending Touch value; write-through to V6 happens in coordinator.
    // Once walls_from_v6, poll will refresh — but UI edit updates pending mirror.
    zone.exterior_walls = update.exterior_walls & 0x0F;
    zone.wind_exposure = update.wind_exposure;
    zone.solar_gain = update.solar_gain;
    zone.thermal_lead_h = update.thermal_lead_h;
    zone.max_offset_c = update.max_offset_c;
  }
  room.exterior_walls_union = update.exterior_walls & 0x0F;
  if (room_revisions_[room_index] < UINT32_MAX)
    room_revisions_[room_index]++;
  bump_weights_revision(0);
  if (new_revision != nullptr)
    *new_revision = room_revisions_[room_index];
  return RoomUpdateResult::STORED;
}

uint32_t HouseModel::room_revision(const char *room_id) const {
  const size_t index = room_index_(room_id);
  return index < room_count_ ? room_revisions_[index] : 0;
}

bool HouseModel::update_zone_live(const char *room_id, float temperature_c, bool has_temperature,
                                  float setpoint_c, bool has_setpoint, const char *status,
                                  bool fresh, uint32_t now_ms, float valve_pct, bool has_valve) {
  if (room_id == nullptr || room_id[0] == '\0')
    return false;
  for (size_t i = 0; i < zone_count_; i++) {
    if (!same_text_(zones_[i].room_id, room_id))
      continue;
    copy_text_(live_[i].room_id, sizeof(live_[i].room_id), room_id);
    live_[i].temperature_c = temperature_c;
    live_[i].setpoint_c = setpoint_c;
    live_[i].valve_pct = clamp_float_(valve_pct, 0.0f, 100.0f, 0.0f);
    live_[i].has_temperature = has_temperature;
    live_[i].has_setpoint = has_setpoint;
    live_[i].has_valve = has_valve;
    copy_text_(live_[i].status, sizeof(live_[i].status), status != nullptr && status[0] != '\0' ? status : "unknown");
    live_[i].fresh = fresh;
    live_[i].updated_at_ms = now_ms;
    seed_comfort_from_live_(&zones_[i], find_room_(zones_[i].room_id), setpoint_c, has_setpoint);
    record_zone_history_(i, temperature_c, live_[i].status, fresh && has_temperature, now_ms);
    return true;
  }
  return false;
}

bool HouseModel::update_zone_live_by_binding(size_t node_index, size_t zone_index,
                                             float temperature_c, bool has_temperature,
                                             float setpoint_c, bool has_setpoint, const char *status,
                                             bool fresh, uint32_t now_ms, float valve_pct, bool has_valve) {
  for (size_t i = 0; i < zone_count_; i++) {
    if (!zones_[i].enabled || zones_[i].node_index != node_index || zones_[i].zone_index != zone_index)
      continue;
    copy_text_(live_[i].room_id, sizeof(live_[i].room_id), zones_[i].room_id);
    live_[i].temperature_c = temperature_c;
    live_[i].setpoint_c = setpoint_c;
    live_[i].valve_pct = clamp_float_(valve_pct, 0.0f, 100.0f, 0.0f);
    live_[i].has_temperature = has_temperature;
    live_[i].has_setpoint = has_setpoint;
    live_[i].has_valve = has_valve;
    copy_text_(live_[i].status, sizeof(live_[i].status), status != nullptr && status[0] != '\0' ? status : "unknown");
    live_[i].fresh = fresh;
    live_[i].updated_at_ms = now_ms;
    seed_comfort_from_live_(&zones_[i], find_room_(zones_[i].room_id), setpoint_c, has_setpoint);
    record_zone_history_(i, temperature_c, live_[i].status, fresh && has_temperature, now_ms);
    return true;
  }
  return false;
}

bool HouseModel::update_zone_group_membership(size_t node_index, size_t zone_index,
                                              uint8_t group_primary_zone_1based, uint8_t members_mask,
                                              bool is_primary) {
  const size_t i = find_binding_index_(node_index, zone_index);
  if (i >= zone_count_ || !zones_[i].enabled)
    return false;
  live_[i].group_primary_zone = group_primary_zone_1based;
  live_[i].group_members_mask = members_mask;
  live_[i].is_group_primary = is_primary;
  zones_[i].is_group_secondary = !is_primary;
  if (!is_primary) {
    detach_binding_from_room_(i);
  } else if (zones_[i].room_id[0] == '\0') {
    zones_[i].unassigned = true;
  }
  return true;
}

bool HouseModel::update_zone_heat_demand(size_t node_index, size_t zone_index, const char *demand_state,
                                         float opening_ratio, uint32_t saturated_s, bool headroom,
                                         HeatRecommendation recommendation, bool fresh) {
  const size_t i = find_binding_index_(node_index, zone_index);
  if (i >= zone_count_ || !zones_[i].enabled)
    return false;
  copy_text_(live_[i].demand_state, sizeof(live_[i].demand_state),
             demand_state != nullptr && demand_state[0] != '\0' ? demand_state : "unknown");
  live_[i].opening_ratio = std::isfinite(opening_ratio) ? opening_ratio : 0.0f;
  live_[i].saturated_s = saturated_s;
  live_[i].headroom = headroom;
  live_[i].recommendation = recommendation;
  live_[i].heat_demand_fresh = fresh;
  return true;
}

bool HouseModel::update_node_heat_demand(size_t node_index, const NodeHeatDemand &demand) {
  if (node_index >= MAX_NODES)
    return false;
  node_heat_demand_[node_index] = demand;
  return true;
}

RoomHeatDemand HouseModel::room_heat_demand(const char *room_id) const {
  RoomHeatDemand out{};
  if (room_id == nullptr || room_id[0] == '\0')
    return out;
  for (size_t i = 0; i < zone_count_; i++) {
    if (!zones_[i].enabled || zones_[i].unassigned || zones_[i].is_group_secondary)
      continue;
    if (!same_text_(zones_[i].room_id, room_id))
      continue;
    if (!live_[i].heat_demand_fresh)
      continue;
    out.any_fresh = true;
    if (live_[i].recommendation > out.recommendation)
      out.recommendation = live_[i].recommendation;
    if (live_[i].opening_ratio > out.opening_ratio)
      out.opening_ratio = live_[i].opening_ratio;
    if (live_[i].saturated_s > out.saturated_s)
      out.saturated_s = live_[i].saturated_s;
    if (live_[i].demand_state[0] == 'D' || live_[i].demand_state[0] == 'd' ||
        std::strcmp(live_[i].demand_state, "DEMAND") == 0 ||
        std::strcmp(live_[i].demand_state, "demand") == 0)
      out.demanding_groups++;
  }
  return out;
}

const NodeHeatDemand *HouseModel::node_heat_demand(size_t node_index) const {
  if (node_index >= node_count_ || node_index >= MAX_NODES)
    return nullptr;
  return &node_heat_demand_[node_index];
}

size_t HouseModel::unassigned_group_count() const {
  size_t count = 0;
  for (size_t i = 0; i < zone_count_; i++) {
    if (zones_[i].enabled && zones_[i].unassigned && !zones_[i].is_group_secondary)
      count++;
  }
  return count;
}

ResolvedZone HouseModel::resolve_room(const char *room_id) const {
  if (room_id == nullptr)
    return {};
  const LogicalRoom *room = find_room_(room_id);
  if (room != nullptr && room->primary_loop_id[0] != '\0') {
    for (size_t i = 0; i < zone_count_; i++) {
      if (zones_[i].enabled && !zones_[i].is_group_secondary &&
          same_text_(zones_[i].loop_id, room->primary_loop_id))
        return {node(zones_[i].node_index), &zones_[i], &live_[i]};
    }
  }
  for (size_t i = 0; i < zone_count_; i++) {
    if (zones_[i].enabled && !zones_[i].is_group_secondary && !zones_[i].unassigned &&
        same_text_(zones_[i].room_id, room_id)) {
      return {node(zones_[i].node_index), &zones_[i], &live_[i]};
    }
  }
  return {};
}

size_t HouseModel::resolve_room_loops(const char *room_id, ResolvedRoomLoop *out, size_t capacity) const {
  if (room_id == nullptr || out == nullptr || capacity == 0 || find_room_(room_id) == nullptr)
    return 0;
  size_t count = 0;
  for (size_t i = 0; i < zone_count_ && count < capacity; i++) {
    if (!zones_[i].enabled || !zones_[i].commissioned || zones_[i].is_group_secondary ||
        zones_[i].unassigned || !same_text_(zones_[i].room_id, room_id))
      continue;
    out[count++] = {node(zones_[i].node_index), &zones_[i], &live_[i]};
  }
  return count;
}

size_t HouseModel::active_zone_count() const {
  size_t total = 0;
  for (size_t i = 0; i < zone_count_; i++) {
    if (zones_[i].enabled)
      total++;
  }
  return total;
}

size_t HouseModel::calling_zone_count() const {
  size_t total = 0;
  for (size_t i = 0; i < zone_count_; i++) {
    if (!zones_[i].enabled)
      continue;
    if (same_text_(live_[i].status, "heat") || same_text_(live_[i].status, "call") ||
        same_text_(live_[i].status, "preheat"))
      total++;
  }
  return total;
}

size_t HouseModel::stale_zone_count() const {
  size_t total = 0;
  for (size_t i = 0; i < zone_count_; i++) {
    if (zones_[i].enabled && (!live_[i].fresh || same_text_(live_[i].status, "stale")))
      total++;
  }
  return total;
}

float HouseModel::average_comfort_setpoint_c() const {
  float sum = 0.0f;
  size_t count = 0;
  for (size_t i = 0; i < room_count_; i++) {
    if (!rooms_[i].enabled)
      continue;
    float value = rooms_[i].comfort_setpoint_c + rooms_[i].comfort_bias_c;
    if (comfort_looks_unset_(rooms_[i].comfort_setpoint_c))
      value = 21.0f;
    else
      value = clamp_float_(value, 5.0f, 35.0f, 21.0f);
    sum += value;
    count++;
  }
  return count > 0 ? sum / static_cast<float>(count) : 0.0f;
}

float HouseModel::effective_comfort_setpoint_c(const ZoneBinding &zone) {
  return effective_comfort(zone, false, 0, 0).setpoint_c;
}

EffectiveComfort HouseModel::effective_comfort(const ZoneBinding &zone, bool time_valid,
                                               uint8_t day_index, uint16_t minute_of_day) {
  EffectiveComfort result{};
  result.time_valid = time_valid;
  result.setpoint_c = clamp_float_(zone.comfort_setpoint_c + zone.comfort_bias_c, 5.0f, 35.0f,
                                   zone.comfort_setpoint_c);
  copy_text_(result.source, sizeof(result.source), "comfort");
  float scheduled = 0.0f;
  if (time_valid && scheduled_comfort_setpoint_c(zone, day_index, minute_of_day, &scheduled)) {
    result.setpoint_c = scheduled;
    result.schedule_active = true;
    copy_text_(result.source, sizeof(result.source), "schedule");
  }
  return result;
}

bool HouseModel::scheduled_comfort_setpoint_c(const ZoneBinding &zone, uint8_t day_index,
                                              uint16_t minute_of_day, float *out) {
  if (!zone.enabled || !zone.schedule_enabled || day_index > 6 || minute_of_day > 1439)
    return false;
  if ((zone.schedule_day_mask & (1u << day_index)) == 0)
    return false;
  if (minute_of_day < zone.schedule_start_min || minute_of_day >= zone.schedule_end_min)
    return false;
  if (out != nullptr)
    *out = clamp_float_(zone.schedule_setpoint_c + zone.comfort_bias_c, 5.0f, 35.0f,
                        zone.schedule_setpoint_c);
  return true;
}

float HouseModel::learned_comfort_offset_c(const ZoneBinding &zone, const ZoneLiveState *live,
                                           float base_setpoint_c) {
  if (live == nullptr || !live->fresh || !live->has_temperature ||
      !std::isfinite(live->temperature_c) || !std::isfinite(base_setpoint_c))
    return 0.0f;
  if (zone.thermal_samples < 12 || !std::isfinite(zone.learned_heat_gain_c_per_h) ||
      zone.learned_heat_gain_c_per_h < 0.05f || zone.learned_heat_gain_c_per_h > 0.35f)
    return 0.0f;

  const float deficit_c = base_setpoint_c - live->temperature_c;
  if (deficit_c < 0.4f)
    return 0.0f;

  const float cap_c = clamp_float_(zone.max_offset_c * 0.35f, 0.0f, 0.5f, 0.0f);
  return clamp_float_((deficit_c - 0.2f) * 0.25f, 0.0f, cap_c, 0.0f);
}

TargetResolution HouseModel::resolve_target(const TargetResolverInput &input) {
  TargetResolution result{};
  const float lower = std::isfinite(input.dispatch_min_c) ? input.dispatch_min_c : 5.0f;
  const float upper = std::isfinite(input.dispatch_max_c) && input.dispatch_max_c >= lower
                          ? input.dispatch_max_c : 35.0f;
  result.fallback_base_target_c = clamp_float_(input.fallback_base_target_c, lower, upper, 21.0f);
  result.touch_available = input.touch_available;
  result.base_target_c = input.touch_available
                             ? clamp_float_(input.touch_target_c, lower, upper, result.fallback_base_target_c)
                             : result.fallback_base_target_c;
  copy_text_(result.base_source, sizeof(result.base_source),
             input.touch_available ? "touch" : "fallback");
  if (input.command.has_manual_offset) {
    result.manual_modifier_c = input.command.manual_offset_c;
    copy_text_(result.modifier_source, sizeof(result.modifier_source), "manual");
  } else if (input.command.has_forecast_offset) {
    result.distribution_modifier_c = input.command.forecast_offset_c;
    copy_text_(result.modifier_source, sizeof(result.modifier_source), "forecast");
  }
  if (input.learned_confident)
    result.learned_modifier_c = input.learned_modifier_c;
  result.pre_v6_target_c = result.base_target_c + result.manual_modifier_c +
                           result.distribution_modifier_c + result.learned_modifier_c;
  result.dispatch_target_c = clamp_float_(result.pre_v6_target_c, lower, upper, result.base_target_c);
  return result;
}

uint8_t HouseModel::learned_thermal_lead_h(const ZoneBinding &zone) {
  if (zone.thermal_samples < 6 || !std::isfinite(zone.learned_heat_gain_c_per_h) ||
      zone.learned_heat_gain_c_per_h < 0.05f)
    return 0;

  const float hours_to_gain_1c = std::ceil(1.0f / zone.learned_heat_gain_c_per_h);
  if (!std::isfinite(hours_to_gain_1c) || hours_to_gain_1c < 1.0f)
    return 1;
  if (hours_to_gain_1c > 24.0f)
    return 24;
  return static_cast<uint8_t>(hours_to_gain_1c);
}

uint8_t HouseModel::active_thermal_lead_h(const ZoneBinding &zone) {
  const uint8_t configured = zone.thermal_lead_h == 0 ? 4 : zone.thermal_lead_h;
  const uint8_t learned = learned_thermal_lead_h(zone);
  const uint8_t active = learned > configured ? learned : configured;
  return active > 24 ? 24 : active;
}

float HouseModel::geometry_ua_w_per_k(float area_m2) {
  if (!std::isfinite(area_m2) || area_m2 <= 0.0f)
    return 1.0f;
  return area_m2;  // 1 W/(K·m²) geometry prior until UA is learned
}

float HouseModel::room_aggregation_weight(const LogicalRoom &room, bool *used_ua) {
  // Legacy static path prefers UA when learned (pre-v14 behaviour).
  const float mult =
      (std::isfinite(room.physical_weight) && room.physical_weight > 0.0f) ? room.physical_weight
                                                                          : 1.0f;
  if (std::isfinite(room.ua_w_per_k) && room.ua_w_per_k > 0.0f) {
    if (used_ua != nullptr)
      *used_ua = true;
    return room.ua_w_per_k * mult;
  }
  if (used_ua != nullptr)
    *used_ua = false;
  return geometry_ua_w_per_k(room.total_area_m2) * mult;
}

float HouseModel::room_aggregation_weight(const LogicalRoom &room, bool *used_ua,
                                          HouseWeighting weighting) const {
  const float mult =
      (std::isfinite(room.physical_weight) && room.physical_weight > 0.0f) ? room.physical_weight
                                                                          : 1.0f;
  if (weighting == HouseWeighting::UA && std::isfinite(room.ua_w_per_k) && room.ua_w_per_k > 0.0f) {
    if (used_ua != nullptr)
      *used_ua = true;
    return room.ua_w_per_k * mult;
  }
  if (used_ua != nullptr)
    *used_ua = false;
  return geometry_ua_w_per_k(room.total_area_m2) * mult;
}

bool HouseModel::thermal_rate_sample_acceptable(float delta_c, float hours, float tau_h) {
  if (!std::isfinite(delta_c) || !std::isfinite(hours) || hours <= 0.0f)
    return false;
  // Reject short Δt — probe reconnect jumps look like 30+ °C/h.
  static constexpr float MIN_RATE_WINDOW_H = 0.5f;   // 30 minutes
  static constexpr float MAX_RATE_WINDOW_H = 1.0f;   // prefer ≤ 60 min pairs for one step
  if (hours < MIN_RATE_WINDOW_H)
    return false;
  (void)MAX_RATE_WINDOW_H;
  float tau = tau_h;
  if (!std::isfinite(tau) || tau < 8.0f)
    tau = 8.0f;
  if (tau > 80.0f)
    tau = 80.0f;
  // Max |ΔT| plausible over `hours` ≈ hours / τ * driving_span; use ~5 K drive.
  const float max_delta = (hours / tau) * 5.0f + 0.15f;
  return std::fabs(delta_c) <= max_delta;
}

bool HouseModel::bump_weights_revision(uint32_t now_ms) {
  weights_revision_++;
  if (weights_revision_ == 0)
    weights_revision_ = 1;
  weights_updated_at_ms_ = now_ms;
  return true;
}

bool HouseModel::allocate_delivered_kwh(float total_kwh, uint32_t now_ms) {
  if (!std::isfinite(total_kwh) || total_kwh <= 0.0f)
    return false;
  (void)now_ms;
  float weight_sum = 0.0f;
  float weights[MAX_HOUSE_ROOMS]{};
  for (size_t i = 0; i < room_count_ && i < MAX_HOUSE_ROOMS; i++) {
    if (!rooms_[i].enabled || !rooms_[i].include_in_house_temperature)
      continue;
    float valve_share = 0.0f;
    size_t loops = 0;
    for (size_t z = 0; z < zone_count_; z++) {
      if (!zones_[z].enabled || !same_text_(zones_[z].room_id, rooms_[i].room_id))
        continue;
      loops++;
      if (live_[z].fresh && live_[z].has_valve && live_[z].valve_pct > 1.0f)
        valve_share += live_[z].valve_pct;
    }
    if (loops == 0 || valve_share < 1.0f)
      continue;
    bool used_ua = false;
    weights[i] = room_aggregation_weight(rooms_[i], &used_ua, house_weighting_) * (valve_share / 100.0f);
    (void)used_ua;
    weight_sum += weights[i];
  }
  if (weight_sum <= 0.0f)
    return false;
  for (size_t i = 0; i < room_count_ && i < MAX_HOUSE_ROOMS; i++) {
    if (weights[i] <= 0.0f)
      continue;
    rooms_[i].delivered_kwh_today += total_kwh * (weights[i] / weight_sum);
  }
  return true;
}

bool HouseModel::set_zone_thermal_model(const char *room_id, float heat_gain_c_per_h,
                                        float cool_loss_c_per_h, uint16_t samples, float tau_h) {
  if (room_id == nullptr || room_id[0] == '\0')
    return false;
  (void)tau_h;
  if (!std::isfinite(heat_gain_c_per_h) || heat_gain_c_per_h < 0.05f || heat_gain_c_per_h > 0.35f)
    return false;
  if (!std::isfinite(cool_loss_c_per_h) || cool_loss_c_per_h < 0.02f || cool_loss_c_per_h > 0.40f)
    return false;
  bool any = false;
  for (size_t i = 0; i < zone_count_; i++) {
    if (!zones_[i].enabled || !same_text_(zones_[i].room_id, room_id))
      continue;
    zones_[i].learned_heat_gain_c_per_h = heat_gain_c_per_h;
    zones_[i].learned_cool_loss_c_per_h = cool_loss_c_per_h;
    zones_[i].thermal_samples = samples;
    any = true;
  }
  return any;
}

StrategySnapshot HouseModel::strategy_snapshot() const {
  return strategy_snapshot(false, 0, 0);
}

StrategySnapshot HouseModel::strategy_snapshot(bool time_valid, uint8_t day_index,
                                               uint16_t minute_of_day) const {
  StrategySnapshot snapshot{};
  snapshot.weights_revision = weights_revision_;
  snapshot.weights_updated_at_ms = weights_updated_at_ms_;

  float weighted_temp_sum = 0.0f;
  float weighted_demand_sum = 0.0f;
  float weighted_target_sum = 0.0f;
  float weight_sum = 0.0f;
  float stale_temp_sum = 0.0f;
  float stale_weight_sum = 0.0f;
  float target_weight_sum = 0.0f;
  float preview_temperature_sum = 0.0f;
  size_t preview_count = 0;
  float preview_setpoint_sum = 0.0f;
  size_t preview_setpoint_count = 0;
  float best_weighted_deficit = 0.0f;
  float comfort_sum = 0.0f;
  size_t comfort_count = 0;
  float temp_min = 0.0f;
  float temp_max = 0.0f;
  bool have_temp_range = false;
  size_t ua_weight_rooms = 0;
  size_t area_weight_rooms = 0;
  float solar_weight_sum = 0.0f;
  float solar_gain_sum = 0.0f;
  bool contributing_nodes[MAX_NODES]{};

  if (configured_expected_manifolds_ > 0) {
    snapshot.expected_manifolds = configured_expected_manifolds_;
  } else {
    for (size_t node_index = 0; node_index < node_count_; node_index++) {
      bool serves_included_room = false;
      for (size_t loop_index = 0; loop_index < zone_count_; loop_index++) {
        if (zones_[loop_index].enabled && zones_[loop_index].commissioned &&
            zones_[loop_index].node_index == node_index) {
          const LogicalRoom *room = find_room_(zones_[loop_index].room_id);
          serves_included_room = room != nullptr && room->include_in_house_temperature;
          if (serves_included_room)
            break;
        }
      }
      if (serves_included_room)
        snapshot.expected_manifolds++;
    }
  }

  // Median of fresh, plausible room temperatures. A room far from it is a
  // sensor fault (0 / −127 / 85 °C, unplugged probe), not a sunny upper floor:
  // real houses easily spread > 4 °C between floors, so spread alone must never
  // block the house signal — only implausible readings are left out.
  float median_c = NAN;
  {
    float temps[MAX_HOUSE_ROOMS];
    size_t n = 0;
    for (size_t room_index = 0; room_index < room_count_ && n < MAX_HOUSE_ROOMS; room_index++) {
      const LogicalRoom &room = rooms_[room_index];
      if (!room.include_in_house_temperature || !room.enabled)
        continue;
      for (size_t loop_index = 0; loop_index < zone_count_; loop_index++) {
        const ZoneBinding &zb = zones_[loop_index];
        if (!zb.enabled || !zb.commissioned || zb.is_group_secondary || zb.unassigned ||
            !same_text_(zb.room_id, room.room_id) || !same_text_(zb.loop_id, room.primary_loop_id))
          continue;
        const ZoneLiveState &lv = live_[loop_index];
        if (lv.fresh && lv.has_temperature && std::isfinite(lv.temperature_c) &&
            lv.temperature_c >= PLAUSIBLE_MIN_C && lv.temperature_c <= PLAUSIBLE_MAX_C)
          temps[n++] = lv.temperature_c;
        break;
      }
    }
    if (n > 0) {
      std::sort(temps, temps + n);
      median_c = (n % 2 == 1) ? temps[n / 2] : 0.5f * (temps[n / 2 - 1] + temps[n / 2]);
    }
  }

  for (size_t room_index = 0; room_index < room_count_; room_index++) {
    const LogicalRoom &room = rooms_[room_index];
    // Excluded rooms appear only in excluded_area_m2 (contract).
    if (!room.include_in_house_temperature) {
      const float excluded_weight =
          std::isfinite(room.total_area_m2) && room.total_area_m2 > 0.0f ? room.total_area_m2 : 0.0f;
      snapshot.excluded_area_m2 += excluded_weight;
      continue;
    }

    bool used_ua = false;
    const float weight = room_aggregation_weight(room, &used_ua, house_weighting_);
    if (!std::isfinite(weight) || weight <= 0.0f)
      continue;
    if (used_ua)
      ua_weight_rooms++;
    else
      area_weight_rooms++;

    snapshot.expected_area_m2 += weight;
    snapshot.ua_total_w_per_k += used_ua ? room.ua_w_per_k * (room.physical_weight > 0.0f ? room.physical_weight : 1.0f)
                                         : geometry_ua_w_per_k(room.total_area_m2) *
                                               (room.physical_weight > 0.0f ? room.physical_weight : 1.0f);
    if (std::isfinite(room.thermal_mass_kwh_per_k) && room.thermal_mass_kwh_per_k > 0.0f)
      snapshot.thermal_mass_total_kwh_per_k += room.thermal_mass_kwh_per_k;

    const ZoneBinding *primary = nullptr;
    const ZoneLiveState *live = nullptr;
    for (size_t loop_index = 0; loop_index < zone_count_; loop_index++) {
      if (zones_[loop_index].enabled && zones_[loop_index].commissioned &&
          !zones_[loop_index].is_group_secondary && !zones_[loop_index].unassigned &&
          same_text_(zones_[loop_index].room_id, room.room_id) &&
          same_text_(zones_[loop_index].loop_id, room.primary_loop_id)) {
        primary = &zones_[loop_index];
        live = &live_[loop_index];
        break;
      }
    }

    if (primary != nullptr) {
      const EffectiveComfort effective = effective_comfort(*primary, time_valid,
                                                           day_index, minute_of_day);
      const float target_c =
          resolve_room_target_c_(primary->comfort_setpoint_c, effective, live);
      const bool used_live = comfort_looks_unset_(primary->comfort_setpoint_c) &&
                             live != nullptr && live->has_setpoint &&
                             std::isfinite(live->setpoint_c) && live->setpoint_c >= 10.0f;
      comfort_sum += target_c;
      comfort_count++;
      solar_weight_sum += weight;
      solar_gain_sum += room.solar_gain * weight;
      weighted_target_sum += target_c * weight;
      target_weight_sum += weight;
      snapshot.target_contributing_area_m2 += weight;
      if (snapshot.house_target_source[0] == '\0' || std::strcmp(snapshot.house_target_source, "none") == 0)
        copy_text_(snapshot.house_target_source, sizeof(snapshot.house_target_source),
                   used_live ? "live" : effective.source);
      else if (std::strcmp(snapshot.house_target_source, effective.source) != 0 &&
               std::strcmp(snapshot.house_target_source, "live") != 0)
        copy_text_(snapshot.house_target_source, sizeof(snapshot.house_target_source), "mixed");
    }

    if (live != nullptr && live->has_temperature && std::isfinite(live->temperature_c)) {
      // Fresh or stale: keep a display average so the dashboard does not blank
      // when boards are briefly unreachable.
      stale_temp_sum += live->temperature_c * weight;
      stale_weight_sum += weight;
      if (live->fresh) {
        preview_temperature_sum += live->temperature_c;
        preview_count++;
      }
    }
    if (live != nullptr && live->fresh && live->has_setpoint &&
        std::isfinite(live->setpoint_c) && live->setpoint_c >= 5.0f &&
        live->setpoint_c <= 35.0f) {
      preview_setpoint_sum += live->setpoint_c;
      preview_setpoint_count++;
    }

    // Disabled or unbound room = missing coverage, not absence of requirement.
    // Stale sensors also count as missing for push safety, but last-known temps
    // above still feed the display aggregate.
    if (!room.enabled || primary == nullptr || live == nullptr || !live->fresh ||
        !live->has_temperature || !std::isfinite(live->temperature_c)) {
      snapshot.missing_rooms++;
      snapshot.missing_area_m2 += weight;
      continue;
    }
    // Implausible reading (sensor fault): leave it out, count it as missing so
    // the coverage gate still decides whether the rest is enough.
    if (live->temperature_c < PLAUSIBLE_MIN_C || live->temperature_c > PLAUSIBLE_MAX_C ||
        (std::isfinite(median_c) && std::fabs(live->temperature_c - median_c) > outlier_from_median_c_)) {
      snapshot.missing_rooms++;
      snapshot.missing_area_m2 += weight;
      snapshot.implausible_rooms++;
      if (snapshot.implausible_room_name[0] == '\0')
        copy_text_(snapshot.implausible_room_name, sizeof(snapshot.implausible_room_name), room.room_name);
      continue;
    }

    const EffectiveComfort effective = effective_comfort(*primary, time_valid,
                                                         day_index, minute_of_day);
    const float target_c =
        resolve_room_target_c_(primary->comfort_setpoint_c, effective, live);
    weighted_temp_sum += live->temperature_c * weight;
    weight_sum += weight;
    snapshot.contributing_zones++;
    snapshot.contributing_rooms++;
    snapshot.contributing_area_m2 += weight;
    if (primary->node_index < MAX_NODES)
      contributing_nodes[primary->node_index] = true;

    if (!have_temp_range) {
      temp_min = live->temperature_c;
      temp_max = live->temperature_c;
      have_temp_range = true;
    } else {
      if (live->temperature_c < temp_min)
        temp_min = live->temperature_c;
      if (live->temperature_c > temp_max)
        temp_max = live->temperature_c;
    }

    const float deficit = target_c - live->temperature_c;
    if (deficit > 0.0f) {
      weighted_demand_sum += deficit * weight;
      snapshot.demand_zones++;
      const float weighted_deficit = deficit * weight;
      if (weighted_deficit > best_weighted_deficit) {
        best_weighted_deficit = weighted_deficit;
        snapshot.driver_deficit_c = deficit;
        snapshot.driver_priority = room.priority;
        copy_text_(snapshot.driver_room_id, sizeof(snapshot.driver_room_id), room.room_id);
        copy_text_(snapshot.driver_room_name, sizeof(snapshot.driver_room_name), room.room_name);
      }
    }
  }

  if (house_weighting_ == HouseWeighting::UA && ua_weight_rooms > 0 && area_weight_rooms == 0)
    copy_text_(snapshot.weighting_basis, sizeof(snapshot.weighting_basis), "ua");
  else if (house_weighting_ == HouseWeighting::UA && ua_weight_rooms > 0)
    copy_text_(snapshot.weighting_basis, sizeof(snapshot.weighting_basis), "mixed");
  else
    copy_text_(snapshot.weighting_basis, sizeof(snapshot.weighting_basis), "area");

  if (solar_weight_sum > 0.0f)
    snapshot.passive_solar_gain_factor = solar_gain_sum / solar_weight_sum;
  // Odins `hl_tm_product` is the house time constant τ [h], not a capacity.
  // Odin computes TM = HL × τ. Mirror that: τ = TM / HL when both are known.
  if (snapshot.ua_total_w_per_k > 0.0f && snapshot.thermal_mass_total_kwh_per_k > 0.0f) {
    const float hl_kw_per_k = snapshot.ua_total_w_per_k / 1000.0f;
    if (hl_kw_per_k > 1.0e-6f)
      snapshot.hl_tm_product = snapshot.thermal_mass_total_kwh_per_k / hl_kw_per_k;
  }

  if (comfort_count > 0)
    snapshot.comfort_average_c = comfort_sum / static_cast<float>(comfort_count);
  if (preview_count > 0) {
    snapshot.has_temperature_preview = true;
    snapshot.temperature_preview_c = preview_temperature_sum / static_cast<float>(preview_count);
    snapshot.preview_zones = preview_count;
  }
  if (preview_setpoint_count > 0) {
    snapshot.has_setpoint_preview = true;
    snapshot.setpoint_preview_c = preview_setpoint_sum / static_cast<float>(preview_setpoint_count);
    snapshot.setpoint_preview_zones = preview_setpoint_count;
  }
  if (target_weight_sum > 0.0f) {
    snapshot.has_house_target = true;
    snapshot.house_target_c = weighted_target_sum / target_weight_sum;
  }
  if (weight_sum > 0.0f) {
    snapshot.physical_temperature_c = weighted_temp_sum / weight_sum;
    snapshot.comfort_demand_c = weighted_demand_sum / weight_sum;
  }
  if (stale_weight_sum > 0.0f) {
    snapshot.has_last_known_temperature = true;
    snapshot.last_known_temperature_c = stale_temp_sum / stale_weight_sum;
    // When coverage is degraded, still expose last-known as the physical reading
    // for display/diagnostics. has_physical_temperature stays safety-gated below.
    if (weight_sum <= 0.0f)
      snapshot.physical_temperature_c = snapshot.last_known_temperature_c;
  }
  if (have_temp_range) {
    snapshot.room_temp_spread_c = temp_max - temp_min;
    snapshot.spread_healthy =
        allow_spread_override_ || snapshot.contributing_rooms < 2 ||
        snapshot.room_temp_spread_c <= max_room_spread_c_;
  }
  if (snapshot.expected_area_m2 > 0.0f) {
    snapshot.coverage_ratio = snapshot.contributing_area_m2 / snapshot.expected_area_m2;
  }
  const bool ratio_ok = snapshot.coverage_ratio >= minimum_area_coverage_ &&
                        snapshot.coverage_ratio >= min_contributing_ua_ratio_;
  // min_contributing_rooms applies across multi-room houses; a single included
  // room that is fresh and weighted is whole-house coverage for that install.
  const size_t expected_rooms = snapshot.contributing_rooms + snapshot.missing_rooms;
  const size_t required_rooms =
      expected_rooms == 0
          ? min_contributing_rooms_
          : (expected_rooms < min_contributing_rooms_ ? expected_rooms : min_contributing_rooms_);
  const bool rooms_ok =
      expected_rooms > 0 && snapshot.contributing_rooms >= required_rooms;
  // spread_healthy is advisory only (reported as a warning).
  snapshot.coverage_healthy = ratio_ok && rooms_ok && snapshot.expected_area_m2 > 0.0f;

  for (size_t node_index = 0; node_index < node_count_; node_index++) {
    if (contributing_nodes[node_index])
      snapshot.contributing_manifolds++;
  }
  snapshot.manifolds_healthy = allow_degraded_manifolds_ || snapshot.expected_manifolds < 2 ||
                               snapshot.contributing_manifolds >= snapshot.expected_manifolds;
  if (snapshot.coverage_healthy && snapshot.manifolds_healthy) {
    snapshot.has_physical_temperature = true;
    copy_text_(snapshot.quality, sizeof(snapshot.quality), "healthy");
  } else if (weight_sum > 0.0f) {
    copy_text_(snapshot.quality, sizeof(snapshot.quality), "degraded");
  } else {
    copy_text_(snapshot.quality, sizeof(snapshot.quality), "no_coverage");
  }

  // T2: UA-prior calibration status against Odin HL (contract §8).
  snapshot.ua_prior_sum_w_per_k = ua_prior_sum_included_w_per_k();
  snapshot.odin_heat_loss_w_per_k = odin_heat_loss_w_per_k_;
  snapshot.odin_tau_h = odin_tau_h_;
  snapshot.house_u_base = house_u_base_;
  snapshot.house_u_wall = house_u_wall_;
  snapshot.house_c_struct = house_c_struct_;
  snapshot.house_calibrated = house_calibrated_;
  char cal_reason[32]{};
  float ub = 0.0f, uw = 0.0f, cs = 0.0f;
  if (house_calibrated_) {
    copy_text_(snapshot.ua_calibration_status, sizeof(snapshot.ua_calibration_status),
               "calibrated");
    copy_text_(snapshot.ua_calibration_reason, sizeof(snapshot.ua_calibration_reason), "ok");
  } else if (compute_house_calibration(&ub, &uw, &cs, cal_reason, sizeof(cal_reason))) {
    // Ready to write — status stays uncalibrated until V6 write succeeds.
    copy_text_(snapshot.ua_calibration_status, sizeof(snapshot.ua_calibration_status),
               "uncalibrated");
    copy_text_(snapshot.ua_calibration_reason, sizeof(snapshot.ua_calibration_reason),
               "ready");
  } else {
    copy_text_(snapshot.ua_calibration_status, sizeof(snapshot.ua_calibration_status),
               std::strcmp(cal_reason, "inconsistent_physics") == 0 ? "blocked" : "uncalibrated");
    copy_text_(snapshot.ua_calibration_reason, sizeof(snapshot.ua_calibration_reason),
               cal_reason[0] != '\0' ? cal_reason : "unknown");
  }

  // T5: index room = highest required flow temperature.
  float best_flow = -1.0e9f;
  const float t_out = has_outdoor_temp_ ? outdoor_temp_c_ : 0.0f;
  for (size_t room_index = 0; room_index < room_count_; room_index++) {
    const LogicalRoom &room = rooms_[room_index];
    if (!room.include_in_house_temperature || !room.enabled)
      continue;
    float area = room.total_area_m2 > 0.0f ? room.total_area_m2 : 1.0f;
    float ua = room.ua_effective_w_per_k > 0.0f
                   ? room.ua_effective_w_per_k
                   : (room.ua_w_per_k > 0.0f ? room.ua_w_per_k : geometry_ua_w_per_k(area));
    float r = 0.05f;
    float r_area = 0.0f;
    for (size_t z = 0; z < zone_count_; z++) {
      if (!zones_[z].enabled || zones_[z].is_group_secondary ||
          !same_text_(zones_[z].room_id, room.room_id))
        continue;
      const float a = zones_[z].v6_area_m2 > 0.0f ? zones_[z].v6_area_m2 : 0.0f;
      if (a > 0.0f && std::isfinite(zones_[z].floor.r_m2k_per_w) &&
          zones_[z].floor.r_m2k_per_w > 0.0f) {
        r = (r_area <= 0.0f) ? zones_[z].floor.r_m2k_per_w
                             : (r * r_area + zones_[z].floor.r_m2k_per_w * a) / (r_area + a);
        r_area += a;
      }
    }
    const float t_target = room.comfort_setpoint_c;
    const float flow = required_flow_temp_c(ua, area, r, t_target, t_out);
    if (flow > best_flow) {
      best_flow = flow;
      copy_text_(snapshot.index_room_id, sizeof(snapshot.index_room_id), room.room_id);
      copy_text_(snapshot.index_room_name, sizeof(snapshot.index_room_name), room.room_name);
      snapshot.index_flow_req_c = flow;
      const float ua_per_m2 = ua / area;
      // Attribute: high covering R vs high UA density.
      if (r >= 0.12f)
        copy_text_(snapshot.index_reason, sizeof(snapshot.index_reason), "high_r");
      else if (ua_per_m2 >= 1.5f)
        copy_text_(snapshot.index_reason, sizeof(snapshot.index_reason), "high_ua_per_m2");
      else
        copy_text_(snapshot.index_reason, sizeof(snapshot.index_reason), "high_ua_per_m2");
    }
  }
  if (best_flow < -1.0e8f)
    copy_text_(snapshot.index_reason, sizeof(snapshot.index_reason), "none");

  return snapshot;
}

LearningSnapshot HouseModel::learning_snapshot() const {
  LearningSnapshot snapshot;
  float delta_sum = 0.0f;

  for (size_t i = 0; i < zone_count_; i++) {
    if (!zones_[i].enabled)
      continue;
    const ZoneHistory &history = history_[i];
    if (!history.has_temperature || history.samples == 0)
      continue;

    snapshot.zones_with_history++;
    snapshot.total_samples += history.samples;
    snapshot.total_calling_samples += history.calling_samples;

    if (history.has_delta) {
      snapshot.zones_with_delta++;
      delta_sum += history.last_delta_c_per_h;
      if (history.last_delta_c_per_h > 0.05f)
        snapshot.warming_zones++;
      else if (history.last_delta_c_per_h < -0.05f)
        snapshot.cooling_zones++;
    }
  }

  if (snapshot.total_samples > 0) {
    snapshot.calling_ratio =
        static_cast<float>(snapshot.total_calling_samples) / static_cast<float>(snapshot.total_samples);
  }
  if (snapshot.zones_with_delta > 0) {
    snapshot.average_delta_c_per_h = delta_sum / static_cast<float>(snapshot.zones_with_delta);
  }
  return snapshot;
}

const PairedNode *HouseModel::node(size_t index) const {
  return index < node_count_ ? &nodes_[index] : nullptr;
}

const LogicalRoom *HouseModel::room(size_t index) const {
  return index < room_count_ ? &rooms_[index] : nullptr;
}

const LogicalRoom *HouseModel::room_by_id(const char *room_id) const {
  return find_room_(room_id);
}

const ZoneBinding *HouseModel::zone(size_t index) const {
  return index < zone_count_ ? &zones_[index] : nullptr;
}

const ZoneLiveState *HouseModel::zone_live(size_t index) const {
  return index < zone_count_ ? &live_[index] : nullptr;
}

const ZoneHistory *HouseModel::zone_history(size_t index) const {
  return index < zone_count_ ? &history_[index] : nullptr;
}

void HouseModel::record_zone_history_(size_t zone_index, float temperature_c, const char *status,
                                      bool fresh, uint32_t now_ms) {
  if (zone_index >= zone_count_ || !fresh || !std::isfinite(temperature_c))
    return;

  ZoneHistory &history = history_[zone_index];
  const bool calling = same_text_(status, "heat") || same_text_(status, "call") ||
                       same_text_(status, "preheat");
  if (!history.has_temperature) {
    history.samples = 1;
    history.calling_samples = calling ? 1 : 0;
    history.first_sample_ms = now_ms;
    history.last_sample_ms = now_ms;
    history.min_temperature_c = temperature_c;
    history.max_temperature_c = temperature_c;
    history.average_temperature_c = temperature_c;
    history.last_temperature_c = temperature_c;
    history.last_delta_c_per_h = 0.0f;
    history.has_temperature = true;
    history.has_delta = false;
    return;
  }

  if (now_ms > history.last_sample_ms) {
    const float hours = static_cast<float>(now_ms - history.last_sample_ms) / 3600000.0f;
    if (hours > 0.0f) {
      const float delta_c = temperature_c - history.last_temperature_c;
      history.last_delta_c_per_h = delta_c / hours;
      history.has_delta = true;
      ZoneBinding &zone = zones_[zone_index];
      float tau_h = 24.0f;
      if (std::isfinite(zone.learned_heat_gain_c_per_h) && zone.learned_heat_gain_c_per_h >= 0.05f &&
          zone.learned_heat_gain_c_per_h <= 0.35f)
        tau_h = clamp_float_(1.0f / zone.learned_heat_gain_c_per_h, 8.0f, 80.0f, 24.0f);

      // A1: rate learning only on 30–60+ min windows; reject probe jumps vs τ prior.
      if (thermal_rate_sample_acceptable(delta_c, hours, tau_h)) {
        const uint16_t previous_samples = zone.thermal_samples;
        const float rate = history.last_delta_c_per_h;
        if (calling && rate > 0.02f && rate <= 0.35f) {
          zone.learned_heat_gain_c_per_h =
              rolling_average_(zone.learned_heat_gain_c_per_h, rate, previous_samples);
          if (zone.thermal_samples < 65535)
            zone.thermal_samples++;
        } else if (!calling && rate < -0.02f && rate >= -0.40f) {
          zone.learned_cool_loss_c_per_h =
              rolling_average_(zone.learned_cool_loss_c_per_h, -rate, previous_samples);
          if (zone.thermal_samples < 65535)
            zone.thermal_samples++;
        }
      }
    }
  }
  history.samples++;
  if (calling)
    history.calling_samples++;
  if (temperature_c < history.min_temperature_c)
    history.min_temperature_c = temperature_c;
  if (temperature_c > history.max_temperature_c)
    history.max_temperature_c = temperature_c;
  history.average_temperature_c +=
      (temperature_c - history.average_temperature_c) / static_cast<float>(history.samples);
  history.last_temperature_c = temperature_c;
  history.last_sample_ms = now_ms;
}

bool HouseModel::export_state(PersistedState *out) const {
  if (out == nullptr)
    return false;
  std::memset(out, 0, sizeof(*out));
  out->magic = PERSISTED_STATE_MAGIC;
  out->version = PERSISTED_STATE_VERSION;
  out->node_count = static_cast<uint32_t>(node_count_);
  out->zone_count = static_cast<uint32_t>(zone_count_);
  out->room_count = static_cast<uint32_t>(room_count_);
  for (size_t i = 0; i < node_count_; i++)
    out->nodes[i] = nodes_[i];
  for (size_t i = 0; i < room_count_; i++)
    out->rooms[i] = rooms_[i];
  for (size_t i = 0; i < zone_count_; i++) {
    out->zones[i] = zones_[i];
  }
  return true;
}

bool HouseModel::import_state_v13(const PersistedState &state) {
  return import_state_common_(state, PERSISTED_STATE_VERSION_V13);
}

bool HouseModel::import_state_v14(const PersistedState &state) {
  return import_state_common_(state, PERSISTED_STATE_VERSION_V14);
}

bool HouseModel::import_state(const PersistedState &state) {
  return import_state_common_(state, PERSISTED_STATE_VERSION);
}

bool HouseModel::import_state_common_(const PersistedState &state, uint16_t accepted_version) {
  if (state.magic != PERSISTED_STATE_MAGIC || state.version != accepted_version)
    return false;
  if (state.node_count > MAX_NODES || state.zone_count > MAX_HOUSE_ZONES ||
      state.room_count > MAX_HOUSE_ROOMS)
    return false;

  std::memset(nodes_, 0, sizeof(nodes_));
  std::memset(rooms_, 0, sizeof(rooms_));
  std::memset(zones_, 0, sizeof(zones_));
  std::memset(live_, 0, sizeof(live_));
  std::memset(history_, 0, sizeof(history_));
  std::memset(node_heat_demand_, 0, sizeof(node_heat_demand_));
  std::memset(room_revisions_, 0, sizeof(room_revisions_));
  node_count_ = state.node_count;
  zone_count_ = state.zone_count;
  room_count_ = state.room_count;
  for (size_t i = 0; i < node_count_; i++)
    nodes_[i] = state.nodes[i];
  for (size_t i = 0; i < node_count_; i++) {
    if (nodes_[i].name[0] == '\0')
      copy_text_(nodes_[i].name, sizeof(nodes_[i].name), nodes_[i].node_id);
  }
  for (size_t i = 0; i < room_count_; i++) {
    rooms_[i] = state.rooms[i];
    if (!std::isfinite(rooms_[i].total_area_m2) || rooms_[i].total_area_m2 <= 0.0f)
      rooms_[i].total_area_m2 = 1.0f;
    if (!std::isfinite(rooms_[i].physical_weight) || rooms_[i].physical_weight <= 0.0f)
      rooms_[i].physical_weight = 1.0f;
    // Legacy v12 equated physical_weight with area_m2 — treat as multiplier 1.0.
    if (rooms_[i].total_area_m2 > 2.0f &&
        std::fabs(rooms_[i].physical_weight - rooms_[i].total_area_m2) < 0.05f)
      rooms_[i].physical_weight = 1.0f;
    // v14→v15: weather fields appended; zeroed defaults mean not seeded yet.
    if (accepted_version < PERSISTED_STATE_VERSION) {
      if (!std::isfinite(rooms_[i].wind_exposure) || rooms_[i].wind_exposure < 0.0f ||
          rooms_[i].wind_exposure > 1.0f)
        rooms_[i].wind_exposure = 0.5f;
      if (!std::isfinite(rooms_[i].solar_gain) || rooms_[i].solar_gain < 0.0f ||
          rooms_[i].solar_gain > 1.0f)
        rooms_[i].solar_gain = 0.3f;
      // Leave weather_seeded false so first poll can seed from V6 or from zone bindings.
    }
    room_revisions_[i] = 1;
  }
  for (size_t i = 0; i < zone_count_; i++) {
    zones_[i] = state.zones[i];
    // v13 blobs lack unassigned/is_group_secondary — defaults from memset/copy are fine
    // for zeroed trailing bytes; force safe defaults when migrating older layouts.
    if (accepted_version < PERSISTED_STATE_VERSION_V14) {
      zones_[i].unassigned = false;
      zones_[i].is_group_secondary = false;
    }
    if (accepted_version < PERSISTED_STATE_VERSION) {
      zones_[i].floor = {};
      zones_[i].floor.unset = true;
      zones_[i].walls_from_v6 = false;
      zones_[i].physics_conflict = false;
    }
    copy_text_(live_[i].room_id, sizeof(live_[i].room_id), zones_[i].room_id);
    copy_text_(live_[i].status, sizeof(live_[i].status), "unknown");
    live_[i].is_group_primary = !zones_[i].is_group_secondary;
    if (zones_[i].node_index >= node_count_) {
      zones_[i].enabled = false;
      history_[i] = {};
      continue;
    }
    if (zones_[i].node_id[0] == '\0')
      copy_text_(zones_[i].node_id, sizeof(zones_[i].node_id), nodes_[zones_[i].node_index].node_id);
    if (zones_[i].room_id[0] != '\0' && find_room_(zones_[i].room_id) == nullptr) {
      LogicalRoom *room = ensure_room_(zones_[i].room_id, zones_[i].room_name, zones_[i].name_source);
      if (room == nullptr)
        return false;
      room->comfort_setpoint_c = zones_[i].comfort_setpoint_c;
      room->comfort_bias_c = zones_[i].comfort_bias_c;
      room->schedule_setpoint_c = zones_[i].schedule_setpoint_c;
      room->schedule_start_min = zones_[i].schedule_start_min;
      room->schedule_end_min = zones_[i].schedule_end_min;
      room->schedule_day_mask = zones_[i].schedule_day_mask;
      room->priority = zones_[i].priority;
      room->schedule_enabled = zones_[i].schedule_enabled;
      copy_text_(room->primary_loop_id, sizeof(room->primary_loop_id), zones_[i].loop_id);
    }
    // Migrate per-loop weather onto room if room not yet seeded.
    LogicalRoom *room = find_room_(zones_[i].room_id);
    if (room != nullptr && !room->weather_seeded && accepted_version < PERSISTED_STATE_VERSION) {
      room->wind_exposure = zones_[i].wind_exposure;
      room->solar_gain = zones_[i].solar_gain;
      room->weather_seeded = true;
      room->exterior_walls_union |= zones_[i].exterior_walls & 0x0F;
    }
  }
  refresh_all_room_physics_aggregates();
  return true;
}

bool CommandLedger::append(const CommandRecord &record) {
  CommandRecord stored = record;
  if (stored.boot_id == 0)
    stored.boot_id = boot_id_;
  records_[next_] = stored;
  next_ = (next_ + 1) % LEDGER_CAPACITY;
  if (count_ < LEDGER_CAPACITY)
    count_++;
  return true;
}

namespace {

bool command_expired_(const CommandRecord &record, uint32_t now_ms, int64_t now_epoch_s) {
  if (now_epoch_s > 0 && record.expires_at_epoch_s > 0)
    return now_epoch_s >= record.expires_at_epoch_s;
  return record.expires_at_ms != 0 &&
         static_cast<int32_t>(now_ms - record.expires_at_ms) >= 0;
}

}  // namespace

size_t CommandLedger::expire_pending(uint32_t now_ms, int64_t now_epoch_s) {
  size_t expired = 0;
  for (size_t i = 0; i < count_; i++) {
    CommandRecord &record = records_[i];
    if ((record.result == CommandResult::PENDING || record.result == CommandResult::ACCEPTED) &&
        command_expired_(record, now_ms, now_epoch_s)) {
      record.result = CommandResult::EXPIRED;
      expired++;
    }
  }
  return expired;
}

size_t CommandLedger::count_result(CommandResult result) const {
  size_t total = 0;
  for (size_t i = 0; i < count_; i++) {
    if (records_[i].result == result)
      total++;
  }
  return total;
}

size_t CommandLedger::count_clamped() const {
  size_t total = 0;
  for (size_t i = 0; i < count_; i++) {
    if (records_[i].clamp_applied)
      total++;
  }
  return total;
}

size_t CommandLedger::count_blocked() const {
  return count_result(CommandResult::BLOCKED_STALE) +
         count_result(CommandResult::BLOCKED_UNREACHABLE) +
         count_result(CommandResult::BLOCKED_UNTRUSTED);
}

bool CommandLedger::has_recent_similar(const char *source, const char *node_id, const char *loop_id,
                                       float requested_offset_c, uint32_t now_ms,
                                       uint32_t min_interval_ms, float epsilon_c,
                                       int64_t now_epoch_s) const {
  if (source == nullptr || source[0] == '\0' || node_id == nullptr || node_id[0] == '\0' ||
      loop_id == nullptr || loop_id[0] == '\0')
    return false;
  for (size_t i = 0; i < count_; i++) {
    const CommandRecord &record = records_[i];
    if (!same_text_(record.source, source))
      continue;
    if (!same_text_(record.node_id, node_id) || !same_text_(record.loop_id, loop_id))
      continue;
    if (record.result != CommandResult::PENDING && record.result != CommandResult::ACCEPTED)
      continue;
    if (command_expired_(record, now_ms, now_epoch_s))
      continue;
    if (min_interval_ms > 0 && static_cast<int32_t>(now_ms - record.created_at_ms) > static_cast<int32_t>(min_interval_ms))
      continue;
    if (std::fabs(record.requested_offset_c - requested_offset_c) <= epsilon_c)
      return true;
  }
  return false;
}

bool CommandLedger::active_offset_for(const char *source, uint8_t node_index, uint8_t zone_index,
                                      uint32_t now_ms, float *offset_c, int64_t now_epoch_s) const {
  if (source == nullptr || source[0] == '\0')
    return false;
  const CommandRecord *best = nullptr;
  for (size_t i = 0; i < count_; i++) {
    const CommandRecord &record = records_[i];
    if (!same_text_(record.source, source))
      continue;
    if (record.node_index != node_index || record.zone_index != zone_index)
      continue;
    if (record.result != CommandResult::PENDING && record.result != CommandResult::ACCEPTED)
      continue;
    if (command_expired_(record, now_ms, now_epoch_s))
      continue;
    if (best == nullptr || static_cast<int32_t>(record.created_at_ms - best->created_at_ms) > 0)
      best = &record;
  }
  if (best == nullptr)
    return false;
  if (offset_c != nullptr)
    *offset_c = best->result == CommandResult::ACCEPTED ? best->accepted_offset_c : best->requested_offset_c;
  return true;
}

CommandOffsetResolution CommandLedger::resolve_command_offset(uint8_t node_index,
                                                              uint8_t zone_index,
                                                              uint32_t now_ms,
                                                              int64_t now_epoch_s) const {
  CommandOffsetResolution resolution{};
  resolution.has_manual_offset =
      active_offset_for("dashboard", node_index, zone_index, now_ms, &resolution.manual_offset_c,
                        now_epoch_s);
  resolution.has_forecast_offset =
      active_offset_for("forecast", node_index, zone_index, now_ms, &resolution.forecast_offset_c,
                        now_epoch_s);
  if (resolution.has_manual_offset) {
    resolution.command_offset_c = resolution.manual_offset_c;
    copy_text_(resolution.command_source, sizeof(resolution.command_source), "manual");
  } else if (resolution.has_forecast_offset) {
    resolution.command_offset_c = resolution.forecast_offset_c;
    copy_text_(resolution.command_source, sizeof(resolution.command_source), "forecast");
  }
  return resolution;
}

const CommandRecord *CommandLedger::latest() const {
  if (count_ == 0)
    return nullptr;
  const size_t latest_index = (next_ + LEDGER_CAPACITY - 1) % LEDGER_CAPACITY;
  return &records_[latest_index];
}

const CommandRecord *CommandLedger::latest_active(uint32_t now_ms, int64_t now_epoch_s) const {
  for (size_t n = 0; n < count_; n++) {
    const size_t index = (next_ + LEDGER_CAPACITY - 1 - n) % LEDGER_CAPACITY;
    const CommandRecord &record = records_[index];
    if (record.result != CommandResult::PENDING && record.result != CommandResult::ACCEPTED)
      continue;
    if (command_expired_(record, now_ms, now_epoch_s))
      continue;
    return &record;
  }
  return nullptr;
}

const CommandRecord *CommandLedger::at(size_t index) const {
  return index < count_ ? &records_[index] : nullptr;
}

bool CommandLedger::export_state(PersistedLedger *out) const {
  if (out == nullptr)
    return false;
  std::memset(out, 0, sizeof(*out));
  out->magic = PERSISTED_LEDGER_MAGIC;
  out->version = PERSISTED_LEDGER_VERSION;
  out->boot_id = boot_id_;
  out->next = static_cast<uint32_t>(next_);
  out->count = static_cast<uint32_t>(count_);
  for (size_t i = 0; i < LEDGER_CAPACITY; i++)
    out->records[i] = records_[i];
  return true;
}

bool CommandLedger::import_state(const PersistedLedger &state, uint32_t current_boot_id,
                                 int64_t now_epoch_s) {
  if (state.magic != PERSISTED_LEDGER_MAGIC || state.version != PERSISTED_LEDGER_VERSION)
    return false;
  if (state.next >= LEDGER_CAPACITY || state.count > LEDGER_CAPACITY)
    return false;
  std::memset(records_, 0, sizeof(records_));
  next_ = state.next;
  count_ = state.count;
  boot_id_ = current_boot_id;
  for (size_t i = 0; i < LEDGER_CAPACITY; i++)
    records_[i] = state.records[i];
  for (size_t i = 0; i < count_; i++) {
    CommandRecord &record = records_[i];
    if (record.result != CommandResult::PENDING && record.result != CommandResult::ACCEPTED)
      continue;
    if (current_boot_id == 0 || record.boot_id == 0 || record.boot_id != current_boot_id ||
        command_expired_(record, 0, now_epoch_s)) {
      record.result = CommandResult::EXPIRED;
    }
  }
  return true;
}

const char *command_result_name(CommandResult result) {
  switch (result) {
    case CommandResult::ACCEPTED:
      return "accepted";
    case CommandResult::REJECTED:
      return "rejected";
    case CommandResult::EXPIRED:
      return "expired";
    case CommandResult::BLOCKED_STALE:
      return "blocked_stale";
    case CommandResult::BLOCKED_UNREACHABLE:
      return "blocked_unreachable";
    case CommandResult::BLOCKED_UNTRUSTED:
      return "blocked_untrusted";
    case CommandResult::FAILED:
      return "failed";
    default:
      return "pending";
  }
}

const char *node_trust_name(NodeTrust trust) {
  switch (trust) {
    case NodeTrust::UNPAIRED:
      return "unpaired";
    case NodeTrust::PAIRED:
      return "paired";
    case NodeTrust::TRUSTED:
      return "trusted";
  }
  return "unknown";
}

}  // namespace lune_touch
