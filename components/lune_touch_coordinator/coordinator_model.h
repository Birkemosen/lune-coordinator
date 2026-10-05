#pragma once

#include <cstddef>
#include <cstdint>

namespace lune_touch {

static constexpr size_t MAX_NODES = 4;
static constexpr size_t ZONES_PER_NODE = 6;
static constexpr size_t MAX_HOUSE_ZONES = MAX_NODES * ZONES_PER_NODE;
static constexpr size_t MAX_HOUSE_ROOMS = MAX_HOUSE_ZONES;
static constexpr size_t LEDGER_CAPACITY = 32;
static constexpr uint32_t PERSISTED_STATE_MAGIC = 0x4C544348;  // LTCH
static constexpr uint16_t PERSISTED_STATE_VERSION = 15;
static constexpr uint16_t PERSISTED_STATE_VERSION_V14 = 14;
static constexpr uint16_t PERSISTED_STATE_VERSION_V13 = 13;
static constexpr uint16_t PERSISTED_STATE_VERSION_V12 = 12;
static constexpr uint16_t PERSISTED_STATE_VERSION_V11 = 11;
static constexpr uint16_t PERSISTED_STATE_VERSION_V10 = 10;
static constexpr uint16_t PERSISTED_STATE_VERSION_V9 = 9;
static constexpr uint16_t PERSISTED_STATE_VERSION_V8 = 8;
static constexpr uint16_t PERSISTED_STATE_VERSION_V7 = 7;
static constexpr uint16_t PERSISTED_STATE_VERSION_V6 = 6;
static constexpr uint16_t PERSISTED_STATE_VERSION_V5 = 5;
static constexpr uint16_t PERSISTED_STATE_VERSION_V4 = 4;
static constexpr uint16_t PERSISTED_STATE_VERSION_V3 = 3;
static constexpr uint32_t PERSISTED_LEDGER_MAGIC = 0x4C544C47;  // LTLG
static constexpr uint16_t PERSISTED_LEDGER_VERSION = 3;

enum class NodeTrust : uint8_t {
  UNPAIRED = 0,
  PAIRED = 1,
  TRUSTED = 2,
};

enum class CommandResult : uint8_t {
  PENDING = 0,
  ACCEPTED = 1,
  REJECTED = 2,
  EXPIRED = 3,
  BLOCKED_STALE = 4,
  BLOCKED_UNREACHABLE = 5,
  BLOCKED_UNTRUSTED = 6,
  FAILED = 7,
};

enum class ZoneNameSource : uint8_t {
  GENERATED = 0,
  V6 = 1,
  TOUCH = 2,
};

// Physical house aggregation weight basis (contract prefers area).
enum class HouseWeighting : uint8_t {
  AREA = 0,
  UA = 1,
};

enum class HeatRecommendation : uint8_t {
  LOWER = 0,
  HOLD = 1,
  RAISE = 2,
};

inline const char *heat_recommendation_name(HeatRecommendation value) {
  switch (value) {
    case HeatRecommendation::RAISE: return "raise";
    case HeatRecommendation::LOWER: return "lower";
    case HeatRecommendation::HOLD:
    default: return "hold";
  }
}

inline HeatRecommendation heat_recommendation_from_name(const char *name) {
  if (name != nullptr) {
    if (name[0] == 'r' || name[0] == 'R') return HeatRecommendation::RAISE;
    if (name[0] == 'l' || name[0] == 'L') return HeatRecommendation::LOWER;
  }
  return HeatRecommendation::HOLD;
}

inline const char *house_weighting_name(HouseWeighting value) {
  return value == HouseWeighting::UA ? "ua" : "area";
}

inline HouseWeighting house_weighting_from_name(const char *name) {
  if (name != nullptr && (name[0] == 'u' || name[0] == 'U'))
    return HouseWeighting::UA;
  return HouseWeighting::AREA;
}

struct PairedNode {
  char node_id[24]{};
  char name[48]{};
  char hostname[64]{};
  char fallback_ip[16]{};
  char model[24]{};
  char firmware[24]{};
  char pairing_fingerprint[24]{};
  NodeTrust trust{NodeTrust::UNPAIRED};
  uint32_t last_seen_ms{0};
  bool reachable{false};
};

// Floor construction mirrored from V6 (physics contract v1). Touch edits write through; V6 is SoT.
// Keep this compact — it is replicated across MAX_HOUSE_ZONES in DRAM + NVS blobs.
struct FloorMirror {
  char slab_type[16]{"unset"};       // cast_concrete | screed | …
  char covering[20]{"unset"};        // tile_stone | parquet_laminate | …
  float active_thickness_cm{0.0f};
  float r_override_m2k_per_w{-1.0f};  // <0 → no override
  float c_slab_kwh_per_k{0.0f};
  float r_m2k_per_w{0.0f};
  bool unset{true};
};

struct ZoneBinding {
  char room_id[32]{};
  char room_name[48]{};
  char loop_id[48]{};
  char node_id[24]{};
  uint8_t node_index{0};
  uint8_t zone_index{0};
  float served_area_m2{0.0f};
  bool commissioned{true};
  // V6-owned exterior walls (N=1|E=2|S=4|W=8). Mirrored from poll; UI write-through.
  uint8_t exterior_walls{0};
  // Legacy per-loop weather — kept for NVS compat; SoT is LogicalRoom (T4).
  float wind_exposure{0.5f};
  float solar_gain{0.3f};
  uint8_t thermal_lead_h{4};
  float max_offset_c{1.5f};
  float comfort_setpoint_c{21.0f};
  float comfort_bias_c{0.0f};
  float schedule_setpoint_c{21.0f};
  uint16_t schedule_start_min{360};
  uint16_t schedule_end_min{1320};
  uint8_t schedule_day_mask{0x7F};
  uint8_t priority{1};
  ZoneNameSource name_source{ZoneNameSource::GENERATED};
  uint16_t thermal_samples{0};
  float learned_heat_gain_c_per_h{0.0f};
  float learned_cool_loss_c_per_h{0.0f};
  bool enabled{false};
  bool schedule_enabled{false};
  // True when this binding is a discovered group-primary not yet assigned to a room.
  bool unassigned{false};
  // True when V6 reports this zone as a sync-group secondary (not a group_primary).
  // Secondaries never own rooms and are skipped by aggregation and dispatch.
  bool is_group_secondary{false};
  // --- v15: V6 physics mirror (re-polled; also persisted so offline UI works) ---
  float v6_area_m2{0.0f};
  FloorMirror floor{};
  float ua_prior_w_per_k{0.0f};
  float ua_learned_w_per_k{0.0f};
  float ua_effective_w_per_k{0.0f};
  float ua_confidence{0.0f};
  uint16_t ua_observed_days{0};
  uint32_t v6_data_revision{0};
  bool physics_conflict{false};
  bool walls_from_v6{false};
  // Runtime-mirrored identity (also persisted for offline UI; keep short).
  char ble_mac[18]{};
  char sensor_id[18]{};
  char group_id[16]{};
};

struct LogicalRoom {
  char room_id[32]{};
  char room_name[48]{};
  // Designated sensor group loop (group-primary). Alias: sensor_loop_id.
  char primary_loop_id[48]{};
  // Imported V6 loops begin as equally weighted logical rooms. Touch can
  // refine these values later, but normal target/schedule edits must not
  // depend on commissioning hidden geometry first.
  float total_area_m2{1.0f};
  // Dimensionless override multiplier on computed UA (default 1.0). Not area.
  float physical_weight{1.0f};
  // Aggregated effective UA from V6 groups [W/K]. 0 → area geometry prior.
  float ua_w_per_k{0.0f};
  float thermal_mass_kwh_per_k{0.0f};
  float delivered_kwh_today{0.0f};
  float comfort_setpoint_c{21.0f};
  float comfort_bias_c{0.0f};
  float schedule_setpoint_c{21.0f};
  uint16_t schedule_start_min{360};
  uint16_t schedule_end_min{1320};
  uint8_t schedule_day_mask{0x7F};
  uint8_t priority{1};
  ZoneNameSource name_source{ZoneNameSource::GENERATED};
  bool enabled{false};
  bool include_in_house_temperature{true};
  bool schedule_enabled{false};
  // --- v15: Touch-owned weather (T4) + migration flags ---
  float wind_exposure{0.5f};
  float solar_gain{0.3f};
  bool weather_seeded{false};
  bool walls_migrated{false};
  // Union of member-loop exterior_walls (from V6 mirrors).
  uint8_t exterior_walls_union{0};
  // Aggregated V6 UA triad for the logical room (sum of groups).
  float ua_prior_w_per_k{0.0f};
  float ua_learned_w_per_k{0.0f};
  float ua_effective_w_per_k{0.0f};
  float ua_confidence{0.0f};
  uint16_t ua_observed_days{0};
  bool physics_conflict{false};
  bool ble_sensor_mismatch{false};
  bool floor_unset{false};
};

inline const char *sensor_loop_id(const LogicalRoom &room) { return room.primary_loop_id; }

struct ZoneLiveState {
  char room_id[32]{};
  float temperature_c{0.0f};
  float setpoint_c{0.0f};
  float valve_pct{0.0f};
  char status[16]{"unknown"};
  uint32_t updated_at_ms{0};
  bool has_temperature{false};
  bool has_setpoint{false};
  bool has_valve{false};
  bool fresh{false};
  // Runtime-only V6 group membership (not persisted; V6 owns grouping).
  uint8_t group_primary_zone{0};  // 1–6; 0 = unknown / treat as self
  bool is_group_primary{true};
  uint8_t group_members_mask{0};
  // Runtime-only per-zone heat-demand fields from V6.
  char demand_state[16]{"unknown"};
  float opening_ratio{0.0f};
  uint32_t saturated_s{0};
  bool headroom{false};
  HeatRecommendation recommendation{HeatRecommendation::HOLD};
  bool heat_demand_fresh{false};
};

struct NodeHeatDemand {
  uint8_t critical_zone{0};  // 1–6; 0 = none
  float critical_opening_ratio{0.0f};
  uint32_t saturated_s{0};
  uint8_t demanding_zones{0};
  bool headroom{false};
  HeatRecommendation recommendation{HeatRecommendation::HOLD};
  uint32_t updated_at_ms{0};
  bool fresh{false};
};

struct RoomHeatDemand {
  HeatRecommendation recommendation{HeatRecommendation::HOLD};
  float opening_ratio{0.0f};
  uint32_t saturated_s{0};
  uint8_t demanding_groups{0};
  bool any_fresh{false};
};

struct ZoneHistory {
  uint32_t samples{0};
  uint32_t calling_samples{0};
  uint32_t first_sample_ms{0};
  uint32_t last_sample_ms{0};
  float min_temperature_c{0.0f};
  float max_temperature_c{0.0f};
  float average_temperature_c{0.0f};
  float last_temperature_c{0.0f};
  float last_delta_c_per_h{0.0f};
  bool has_temperature{false};
  bool has_delta{false};
};

struct LearningSnapshot {
  uint32_t zones_with_history{0};
  uint32_t total_samples{0};
  uint32_t total_calling_samples{0};
  uint32_t zones_with_delta{0};
  uint32_t warming_zones{0};
  uint32_t cooling_zones{0};
  float calling_ratio{0.0f};
  float average_delta_c_per_h{0.0f};
};

struct EffectiveComfort {
  float setpoint_c{21.0f};
  char source[12]{"comfort"};
  bool time_valid{false};
  bool schedule_active{false};
};

struct ResolvedZone {
  const PairedNode *node{nullptr};
  const ZoneBinding *binding{nullptr};
  const ZoneLiveState *live{nullptr};
};

struct ResolvedRoomLoop {
  const PairedNode *node{nullptr};
  const ZoneBinding *binding{nullptr};
  const ZoneLiveState *live{nullptr};
};

struct StrategySnapshot {
  bool has_physical_temperature{false};
  float physical_temperature_c{0.0f};
  // Last-known weighted house temperature from included rooms, including stale
  // sensors. Used for dashboard display when the safety-gated physical aggregate
  // is unavailable (boards briefly unreachable). Never used for heat push.
  bool has_last_known_temperature{false};
  float last_known_temperature_c{0.0f};
  // Diagnostic-only value from fresh primary zone sensors. This may be
  // available while the safety-gated physical aggregate is unavailable (for
  // example during commissioning before room geometry is configured).
  bool has_temperature_preview{false};
  float temperature_preview_c{0.0f};
  size_t preview_zones{0};
  bool has_setpoint_preview{false};
  float setpoint_preview_c{0.0f};
  size_t setpoint_preview_zones{0};
  bool has_house_target{false};
  float house_target_c{0.0f};
  float target_contributing_area_m2{0.0f};
  char house_target_source[12]{"none"};
  float comfort_average_c{0.0f};
  float comfort_demand_c{0.0f};
  size_t contributing_zones{0};
  size_t contributing_rooms{0};
  size_t missing_rooms{0};
  float contributing_area_m2{0.0f};
  float missing_area_m2{0.0f};
  float excluded_area_m2{0.0f};
  float expected_area_m2{0.0f};
  float coverage_ratio{0.0f};
  bool coverage_healthy{false};
  size_t expected_manifolds{0};
  size_t contributing_manifolds{0};
  bool manifolds_healthy{false};
  char quality[16]{"no_coverage"};
  char weighting_basis[8]{"area"};  // "ua" | "area" | "mixed"
  float ua_total_w_per_k{0.0f};
  float thermal_mass_total_kwh_per_k{0.0f};
  // House time constant τ [h] (Odin field name `hl_tm_product`). NOT kWh/K.
  // Odin: TM = HL × τ; Touch: τ = TM / HL when both known.
  float hl_tm_product{0.0f};
  float passive_solar_gain_factor{0.0f};
  float room_temp_spread_c{0.0f};
  /// Advisory: room spread ≤ max_room_spread_c. Never blocks the house signal.
  bool spread_healthy{true};
  /// Rooms left out as sensor faults (outside 5–35 °C or > 8 °C from the median).
  uint8_t implausible_rooms{0};
  char implausible_room_name[48]{};
  uint32_t weights_revision{0};
  uint32_t weights_updated_at_ms{0};
  size_t demand_zones{0};
  char driver_room_id[32]{};
  char driver_room_name[48]{};
  float driver_deficit_c{0.0f};
  uint8_t driver_priority{0};
  // T2: Odin UA-prior calibration status.
  char ua_calibration_status[16]{"uncalibrated"};  // calibrated | uncalibrated | blocked
  char ua_calibration_reason[32]{"unknown"};
  float odin_heat_loss_w_per_k{0.0f};
  float odin_tau_h{0.0f};
  float ua_prior_sum_w_per_k{0.0f};
  float house_u_base{0.5f};
  float house_u_wall{0.4f};
  float house_c_struct{0.06f};
  bool house_calibrated{false};
  // T5: index room (highest required feed temperature).
  char index_room_id[32]{};
  char index_room_name[48]{};
  float index_flow_req_c{0.0f};
  char index_reason[16]{"none"};  // high_r | high_ua_per_m2 | none
};

struct CommandRecord {
  char request_id[20]{};
  char source[20]{};
  char reason[64]{};
  char room_id[32]{};
  char node_id[24]{};
  char loop_id[48]{};
  uint8_t node_index{0};
  uint8_t zone_index{0};
  float requested_offset_c{0.0f};
  float accepted_offset_c{0.0f};
  uint32_t created_at_ms{0};
  uint32_t expires_at_ms{0};
  int64_t created_at_epoch_s{0};
  int64_t expires_at_epoch_s{0};
  uint32_t boot_id{0};
  CommandResult result{CommandResult::PENDING};
  bool clamp_applied{false};
};

struct PersistedState {
  uint32_t magic{PERSISTED_STATE_MAGIC};
  uint16_t version{PERSISTED_STATE_VERSION};
  uint16_t reserved{0};
  uint32_t node_count{0};
  uint32_t zone_count{0};
  uint32_t room_count{0};
  PairedNode nodes[MAX_NODES]{};
  LogicalRoom rooms[MAX_HOUSE_ROOMS]{};
  ZoneBinding zones[MAX_HOUSE_ZONES]{};
};

struct PersistedLedger {
  uint32_t magic{PERSISTED_LEDGER_MAGIC};
  uint16_t version{PERSISTED_LEDGER_VERSION};
  uint16_t reserved{0};
  uint32_t boot_id{0};
  uint32_t next{0};
  uint32_t count{0};
  CommandRecord records[LEDGER_CAPACITY]{};
};

struct CommandOffsetResolution {
  bool has_manual_offset{false};
  bool has_forecast_offset{false};
  float manual_offset_c{0.0f};
  float forecast_offset_c{0.0f};
  float command_offset_c{0.0f};
  char command_source[20]{"none"};
};

// Pure Touch-side target calculation. The final V6 value can be further
// constrained by its independent local safety clamp and is reported separately.
struct TargetResolverInput {
  float fallback_base_target_c{21.0f};
  float touch_target_c{21.0f};
  bool touch_available{true};
  CommandOffsetResolution command{};
  float learned_modifier_c{0.0f};
  bool learned_confident{false};
  float dispatch_min_c{5.0f};
  float dispatch_max_c{35.0f};
};

struct TargetResolution {
  float fallback_base_target_c{21.0f};
  float base_target_c{21.0f};
  float manual_modifier_c{0.0f};
  float distribution_modifier_c{0.0f};
  float learned_modifier_c{0.0f};
  float pre_v6_target_c{21.0f};
  float dispatch_target_c{21.0f};
  bool touch_available{true};
  char base_source[16]{"touch"};
  char modifier_source[20]{"none"};
};

struct RoomUpdate {
  uint32_t expected_revision{0};
  float total_area_m2{0.0f};
  float physical_weight{0.0f};
  bool include_in_house_temperature{true};
  float comfort_setpoint_c{21.0f};
  float comfort_bias_c{0.0f};
  uint8_t priority{1};
  bool schedule_enabled{false};
  uint8_t schedule_day_mask{0x7F};
  uint16_t schedule_start_min{360};
  uint16_t schedule_end_min{1320};
  float schedule_setpoint_c{21.0f};
  uint8_t exterior_walls{0};
  float wind_exposure{0.5f};
  float solar_gain{0.3f};
  uint8_t thermal_lead_h{4};
  float max_offset_c{1.5f};
};

enum class RoomUpdateResult : uint8_t { STORED, NOT_FOUND, STALE_REVISION, INVALID };

enum class RoomCommandOutcome : uint8_t { ACCEPTED, PARTIAL, FAILED };
inline RoomCommandOutcome room_command_outcome(size_t accepted, size_t required) {
  return accepted == required && required > 0 ? RoomCommandOutcome::ACCEPTED :
         accepted > 0 ? RoomCommandOutcome::PARTIAL : RoomCommandOutcome::FAILED;
}
inline const char *room_command_outcome_name(RoomCommandOutcome value) {
  return value == RoomCommandOutcome::ACCEPTED ? "accepted" :
         value == RoomCommandOutcome::PARTIAL ? "partial" : "failed";
}

class HouseModel {
 public:
  void set_node_stale_after_ms(uint32_t value) { node_stale_after_ms_ = value; }
  void set_house_weighting(HouseWeighting value) { house_weighting_ = value; }
  HouseWeighting house_weighting() const { return house_weighting_; }
  void set_minimum_area_coverage(float value) {
    minimum_area_coverage_ = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
  }
  void set_allow_degraded_manifolds(bool value) { allow_degraded_manifolds_ = value; }
  void set_expected_manifolds(size_t value) { configured_expected_manifolds_ = value; }
  void set_min_contributing_rooms(size_t value) { min_contributing_rooms_ = value; }
  void set_min_contributing_ua_ratio(float value) {
    min_contributing_ua_ratio_ = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
  }
  void set_max_room_spread_c(float value) {
    max_room_spread_c_ = (value < 0.0f) ? 4.0f : value;
  }
  void set_allow_spread_override(bool value) { allow_spread_override_ = value; }
  size_t expected_manifolds_configured() const { return configured_expected_manifolds_; }
  uint32_t weights_revision() const { return weights_revision_; }
  uint32_t weights_updated_at_ms() const { return weights_updated_at_ms_; }
  bool bump_weights_revision(uint32_t now_ms);
  uint32_t node_stale_after_ms() const { return node_stale_after_ms_; }
  // Distribute measured thermal energy across open valves by weight (B4 → B1).
  bool allocate_delivered_kwh(float total_kwh, uint32_t now_ms);

  // Geometry prior: ~1 W/(K·m²) for slab rooms until UA is learned.
  static float geometry_ua_w_per_k(float area_m2);
  static float room_aggregation_weight(const LogicalRoom &room, bool *used_ua);
  float room_aggregation_weight(const LogicalRoom &room, bool *used_ua, HouseWeighting weighting) const;
  RoomUpdateResult apply_checked_room_update_(size_t room_index, const char *room_id,
                                              const RoomUpdate &update, uint32_t *new_revision);
  static bool thermal_rate_sample_acceptable(float delta_c, float hours, float tau_h);
  bool set_zone_thermal_model(const char *room_id, float heat_gain_c_per_h, float cool_loss_c_per_h,
                              uint16_t samples, float tau_h);

  int upsert_node(const char *node_id, const char *hostname, const char *fallback_ip,
                  const char *model, const char *firmware, NodeTrust trust);
  bool remove_node(const char *node_id);
  bool mark_node_seen(size_t node_index, uint32_t now_ms);
  bool mark_node_unreachable(size_t node_index, uint32_t now_ms);
  bool update_node_metadata(size_t node_index, const char *model, const char *firmware,
                            const char *fallback_ip);
  bool update_node_identity(size_t node_index, const char *pairing_fingerprint);
  bool update_node_trust(const char *node_id, NodeTrust trust);
  bool update_node_name(const char *node_id, const char *name);
  bool is_node_stale(size_t node_index, uint32_t now_ms) const;

  bool bind_zone(const char *room_id, const char *room_name, size_t node_index, size_t zone_index);
  bool bind_zone_with_source(const char *room_id, const char *room_name, size_t node_index,
                             size_t zone_index, ZoneNameSource source);
  // Discover a group-primary without assigning a room (unassigned state).
  bool bind_unassigned_group(size_t node_index, size_t zone_index, const char *name,
                             ZoneNameSource source);
  bool remove_loop(const char *loop_id);
  // Room table: create / merge / sensor / delete.
  bool create_room(const char *room_id, const char *name);
  bool delete_room_if_empty(const char *room_id);
  bool move_group_to_room(const char *loop_id, const char *room_id);
  bool set_room_sensor(const char *room_id, const char *loop_id);
  bool set_room_geometry(const char *room_id, float total_area_m2, float physical_weight,
                         bool include_in_house_temperature);
  bool set_loop_served_area(const char *loop_id, float served_area_m2);
  bool update_zone_name_from_v6_by_binding(size_t node_index, size_t zone_index,
                                           const char *room_name);
  bool update_zone_forecast_profile_by_binding(size_t node_index, size_t zone_index,
                                               uint8_t exterior_walls, float wind_exposure,
                                               float solar_gain, uint8_t thermal_lead_h,
                                               float max_offset_c,
                                               bool import_exterior_walls = true,
                                               bool import_weather = true);
  bool update_zone_forecast_profile(const char *room_id, uint8_t exterior_walls,
                                    float wind_exposure, float solar_gain,
                                    uint8_t thermal_lead_h, float max_offset_c);
  // T1: seed Touch weather once from V6; thereafter never reimport.
  bool seed_room_weather_from_v6(const char *room_id, float wind_exposure, float solar_gain);
  bool set_room_weather(const char *room_id, float wind_exposure, float solar_gain);
  // T1: apply V6 physics mirror for a loop (walls/area/floor/UA). Detect conflicts.
  bool apply_v6_physics_mirror(size_t node_index, size_t zone_index, uint8_t exterior_walls,
                               float area_m2, const FloorMirror &floor, float ua_prior,
                               float ua_learned, float ua_effective, float ua_confidence,
                               uint16_t ua_observed_days, uint32_t v6_revision,
                               const char *ble_mac, const char *sensor_id, const char *group_id,
                               bool *conflict_out = nullptr);
  // Recompute room aggregates (UA sum, walls union, floor_unset, BLE mismatch).
  bool refresh_room_physics_aggregates(const char *room_id);
  bool refresh_all_room_physics_aggregates();
  // Same-node groups cannot be merged on Touch (T3) — returns false with reason.
  bool move_group_to_room(const char *loop_id, const char *room_id, char *reject_reason,
                          size_t reject_capacity);
  // Mark walls migrated after one-shot write-through to V6.
  bool mark_walls_migrated(const char *room_id);
  bool walls_need_migration(const char *room_id) const;
  // House UA prior coverage for Odin calibration gate (contract §8).
  bool ua_prior_fully_provisioned(char *reason, size_t reason_capacity) const;
  float ua_prior_sum_included_w_per_k() const;
  float c_slab_sum_included_kwh_per_k() const;
  float area_sum_included_m2() const;
  void set_odin_heat_loss_w_per_k(float value) { odin_heat_loss_w_per_k_ = value; }
  void set_odin_tau_h(float value) { odin_tau_h_ = value; }
  float odin_heat_loss_w_per_k() const { return odin_heat_loss_w_per_k_; }
  float odin_tau_h() const { return odin_tau_h_; }
  float house_u_base() const { return house_u_base_; }
  float house_u_wall() const { return house_u_wall_; }
  float house_c_struct() const { return house_c_struct_; }
  bool house_calibrated() const { return house_calibrated_; }
  // Compute Odin house calibration; does not write V6. Returns false if blocked.
  bool compute_house_calibration(float *u_base_out, float *u_wall_out, float *c_struct_out,
                                 char *reason, size_t reason_capacity) const;
  void apply_house_calibration(float u_base, float u_wall, float c_struct);
  // T2: regress delivered energy vs (T_room − T_out). Returns true when a V6 write is warranted.
  bool learn_ua_from_delivery(const char *room_id, float outdoor_c, float *ua_out,
                              float *confidence_out, uint16_t *days_out,
                              uint32_t now_epoch_s = 0);
  // T5 index-room helpers.
  static float required_flow_temp_c(float ua_eff, float area_m2, float r_m2k_per_w,
                                    float t_target_c, float t_out_c, float design_dt_c = 5.0f);
  void set_outdoor_temp_c(float value) {
    outdoor_temp_c_ = value;
    has_outdoor_temp_ = true;
  }
  float outdoor_temp_c() const { return outdoor_temp_c_; }
  bool has_outdoor_temp() const { return has_outdoor_temp_; }
  bool update_zone_comfort(const char *room_id, float comfort_setpoint_c, uint8_t priority,
                           float comfort_bias_c = 0.0f);
  bool update_zone_schedule(const char *room_id, bool enabled, uint8_t day_mask,
                            uint16_t start_min, uint16_t end_min, float setpoint_c);
  RoomUpdateResult apply_room_update(const char *room_id, const RoomUpdate &update,
                                     uint32_t *new_revision = nullptr);
  uint32_t room_revision(const char *room_id) const;
  bool update_zone_live(const char *room_id, float temperature_c, bool has_temperature,
                        float setpoint_c, bool has_setpoint, const char *status,
                        bool fresh, uint32_t now_ms, float valve_pct = 0.0f,
                        bool has_valve = false);
  bool update_zone_live_by_binding(size_t node_index, size_t zone_index,
                                   float temperature_c, bool has_temperature,
                                   float setpoint_c, bool has_setpoint, const char *status,
                                   bool fresh, uint32_t now_ms, float valve_pct = 0.0f,
                                   bool has_valve = false);
  // Apply V6 group membership for a zone binding. Secondary members are detached from rooms.
  bool update_zone_group_membership(size_t node_index, size_t zone_index,
                                    uint8_t group_primary_zone_1based, uint8_t members_mask,
                                    bool is_primary);
  bool update_zone_heat_demand(size_t node_index, size_t zone_index, const char *demand_state,
                               float opening_ratio, uint32_t saturated_s, bool headroom,
                               HeatRecommendation recommendation, bool fresh);
  bool update_node_heat_demand(size_t node_index, const NodeHeatDemand &demand);
  RoomHeatDemand room_heat_demand(const char *room_id) const;
  const NodeHeatDemand *node_heat_demand(size_t node_index) const;
  // Count of group-primary bindings with unassigned==true.
  size_t unassigned_group_count() const;
  ResolvedZone resolve_room(const char *room_id) const;
  size_t resolve_room_loops(const char *room_id, ResolvedRoomLoop *out, size_t capacity) const;
  size_t active_zone_count() const;
  size_t calling_zone_count() const;
  size_t stale_zone_count() const;
  float average_comfort_setpoint_c() const;
  static float effective_comfort_setpoint_c(const ZoneBinding &zone);
  static EffectiveComfort effective_comfort(const ZoneBinding &zone, bool time_valid,
                                            uint8_t day_index, uint16_t minute_of_day);
  static float learned_comfort_offset_c(const ZoneBinding &zone, const ZoneLiveState *live,
                                        float base_setpoint_c);
  static TargetResolution resolve_target(const TargetResolverInput &input);
  static uint8_t learned_thermal_lead_h(const ZoneBinding &zone);
  static uint8_t active_thermal_lead_h(const ZoneBinding &zone);
  StrategySnapshot strategy_snapshot(bool time_valid, uint8_t day_index,
                                     uint16_t minute_of_day) const;
  static bool scheduled_comfort_setpoint_c(const ZoneBinding &zone, uint8_t day_index,
                                           uint16_t minute_of_day, float *out);
  StrategySnapshot strategy_snapshot() const;
  LearningSnapshot learning_snapshot() const;

  const PairedNode *node(size_t index) const;
  const LogicalRoom *room(size_t index) const;
  const LogicalRoom *room_by_id(const char *room_id) const;
  const ZoneBinding *zone(size_t index) const;
  const ZoneLiveState *zone_live(size_t index) const;
  const ZoneHistory *zone_history(size_t index) const;
  size_t node_count() const { return node_count_; }
  size_t zone_count() const { return zone_count_; }
  size_t room_count() const { return room_count_; }
  bool export_state(PersistedState *out) const;
  bool import_state(const PersistedState &state);
  // Accept v13/v14 blobs (identical layout minus newer fields defaulted).
  bool import_state_v13(const PersistedState &state);
  bool import_state_v14(const PersistedState &state);

 private:
  uint32_t node_stale_after_ms_{300000};
  HouseWeighting house_weighting_{HouseWeighting::UA};
  float minimum_area_coverage_{0.75f};
  bool allow_degraded_manifolds_{false};
  size_t configured_expected_manifolds_{0};  // 0 → derive from serving nodes (legacy)
  size_t min_contributing_rooms_{2};
  float min_contributing_ua_ratio_{0.75f};
  float max_room_spread_c_{4.0f};
  /// A room this far from the median is treated as a sensor fault.
  float outlier_from_median_c_{8.0f};
  static constexpr float PLAUSIBLE_MIN_C = 5.0f;
  static constexpr float PLAUSIBLE_MAX_C = 35.0f;
  bool allow_spread_override_{false};
  uint32_t weights_revision_{1};
  uint32_t weights_updated_at_ms_{0};
  float odin_heat_loss_w_per_k_{0.0f};
  float odin_tau_h_{0.0f};
  float outdoor_temp_c_{10.0f};
  bool has_outdoor_temp_{false};
  float house_u_base_{0.5f};
  float house_u_wall_{0.4f};
  float house_c_struct_{0.06f};
  bool house_calibrated_{false};
  uint32_t ua_learned_last_write_epoch_s_[MAX_HOUSE_ZONES]{};
  PairedNode nodes_[MAX_NODES]{};
  LogicalRoom rooms_[MAX_HOUSE_ROOMS]{};
  ZoneBinding zones_[MAX_HOUSE_ZONES]{};
  ZoneLiveState live_[MAX_HOUSE_ZONES]{};
  ZoneHistory history_[MAX_HOUSE_ZONES]{};
  NodeHeatDemand node_heat_demand_[MAX_NODES]{};
  uint32_t room_revisions_[MAX_HOUSE_ROOMS]{};  // runtime optimistic-concurrency revisions
  size_t node_count_{0};
  size_t zone_count_{0};
  size_t room_count_{0};

  LogicalRoom *find_room_(const char *room_id);
  const LogicalRoom *find_room_(const char *room_id) const;
  size_t room_index_(const char *room_id) const;
  LogicalRoom *ensure_room_(const char *room_id, const char *room_name, ZoneNameSource source);
  ZoneBinding *find_binding_by_loop_(const char *loop_id);
  const ZoneBinding *find_binding_by_loop_(const char *loop_id) const;
  size_t find_binding_index_(size_t node_index, size_t zone_index) const;
  void detach_binding_from_room_(size_t zone_slot);
  void ensure_room_sensor_(LogicalRoom *room);
  bool import_state_common_(const PersistedState &state, uint16_t accepted_version);

  void record_zone_history_(size_t zone_index, float temperature_c, const char *status,
                            bool fresh, uint32_t now_ms);
};

class CommandLedger {
 public:
  void set_boot_id(uint32_t boot_id) { boot_id_ = boot_id; }
  uint32_t boot_id() const { return boot_id_; }
  bool append(const CommandRecord &record);
  size_t expire_pending(uint32_t now_ms, int64_t now_epoch_s = 0);
  size_t count() const { return count_; }
  size_t count_result(CommandResult result) const;
  size_t count_clamped() const;
  size_t count_blocked() const;
  bool has_recent_similar(const char *source, const char *node_id, const char *loop_id,
                          float requested_offset_c, uint32_t now_ms,
                          uint32_t min_interval_ms, float epsilon_c,
                          int64_t now_epoch_s = 0) const;
  bool active_offset_for(const char *source, uint8_t node_index, uint8_t zone_index,
                         uint32_t now_ms, float *offset_c, int64_t now_epoch_s = 0) const;
  CommandOffsetResolution resolve_command_offset(uint8_t node_index, uint8_t zone_index,
                                                 uint32_t now_ms, int64_t now_epoch_s = 0) const;
  const CommandRecord *latest() const;
  const CommandRecord *latest_active(uint32_t now_ms, int64_t now_epoch_s = 0) const;
  const CommandRecord *at(size_t index) const;
  bool export_state(PersistedLedger *out) const;
  bool import_state(const PersistedLedger &state, uint32_t current_boot_id = 0,
                    int64_t now_epoch_s = 0);

 private:
  CommandRecord records_[LEDGER_CAPACITY]{};
  size_t next_{0};
  size_t count_{0};
  uint32_t boot_id_{0};
};

const char *command_result_name(CommandResult result);
const char *node_trust_name(NodeTrust trust);

}  // namespace lune_touch
