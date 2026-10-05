#pragma once

// Lune Room Physics Contract v1 — pure helpers shared by Touch host tests and
// firmware. Normative source: shared/contracts/lune_room_physics_v1.md

#include <cmath>
#include <cstdint>
#include <cstring>

namespace lune_touch {
namespace physics {

inline constexpr uint8_t CONTRACT_VERSION = 1;

inline constexpr float U_BASE_DEFAULT = 0.5f;
inline constexpr float U_WALL_DEFAULT = 0.4f;
inline constexpr float C_STRUCT_DEFAULT = 0.06f;
inline constexpr float VOLUMETRIC_C_KWH_PER_M3K = 0.58f;

inline constexpr float UA_LEARNED_CONFIDENCE_GATE = 0.60f;
inline constexpr uint16_t UA_LEARNED_MIN_DAYS = 7;
inline constexpr float UA_LEARNED_REL_CHANGE = 0.10f;
inline constexpr uint32_t UA_LEARNED_MIN_INTERVAL_S = 7u * 24u * 3600u;

inline constexpr float CALIB_SCALE_MIN = 0.25f;
inline constexpr float CALIB_SCALE_MAX = 4.0f;
inline constexpr float C_STRUCT_MIN = 0.0f;
inline constexpr float C_STRUCT_MAX = 0.30f;

inline uint8_t popcount_walls(uint8_t walls) {
  uint8_t n = 0;
  uint8_t w = walls & 0x0F;
  while (w != 0) {
    n = static_cast<uint8_t>(n + (w & 1u));
    w = static_cast<uint8_t>(w >> 1);
  }
  return n;
}

inline float ua_prior_w_per_k(float area_m2, uint8_t exterior_walls, float u_base = U_BASE_DEFAULT,
                              float u_wall = U_WALL_DEFAULT) {
  if (!(area_m2 > 0.0f) || !std::isfinite(area_m2))
    return 0.0f;
  const float ub = std::isfinite(u_base) ? u_base : U_BASE_DEFAULT;
  const float uw = std::isfinite(u_wall) ? u_wall : U_WALL_DEFAULT;
  return area_m2 * (ub + uw * static_cast<float>(popcount_walls(exterior_walls)));
}

inline bool slab_is_volumetric(const char *slab_type) {
  return slab_type != nullptr &&
         (std::strcmp(slab_type, "cast_concrete") == 0 || std::strcmp(slab_type, "screed") == 0);
}

inline bool slab_is_fixed(const char *slab_type) {
  return slab_type != nullptr &&
         (std::strcmp(slab_type, "dry_plates") == 0 || std::strcmp(slab_type, "timber_joists") == 0);
}

inline bool floor_provisioned(const char *slab_type, const char *covering) {
  if (slab_type == nullptr || covering == nullptr)
    return false;
  if (slab_type[0] == '\0' || covering[0] == '\0')
    return false;
  if (std::strcmp(slab_type, "unset") == 0 || std::strcmp(covering, "unset") == 0)
    return false;
  return true;
}

// Effective slab used for priors when unset → cast_concrete 8 cm.
inline float default_thickness_cm(const char *slab_type) {
  if (slab_type == nullptr || std::strcmp(slab_type, "unset") == 0 ||
      std::strcmp(slab_type, "cast_concrete") == 0)
    return 8.0f;
  if (std::strcmp(slab_type, "screed") == 0)
    return 5.0f;
  return 0.0f;
}

inline float c_slab_per_m2(const char *slab_type, float active_thickness_cm,
                           bool *thickness_ignored = nullptr) {
  if (thickness_ignored != nullptr)
    *thickness_ignored = false;
  const char *slab = (slab_type == nullptr || slab_type[0] == '\0' ||
                      std::strcmp(slab_type, "unset") == 0)
                         ? "cast_concrete"
                         : slab_type;
  if (std::strcmp(slab, "dry_plates") == 0) {
    if (thickness_ignored != nullptr && std::isfinite(active_thickness_cm) &&
        active_thickness_cm > 0.0f)
      *thickness_ignored = true;
    return 0.005f;
  }
  if (std::strcmp(slab, "timber_joists") == 0) {
    if (thickness_ignored != nullptr && std::isfinite(active_thickness_cm) &&
        active_thickness_cm > 0.0f)
      *thickness_ignored = true;
    return 0.008f;
  }
  float thick = active_thickness_cm;
  if (!std::isfinite(thick) || thick <= 0.0f)
    thick = default_thickness_cm(slab);
  return VOLUMETRIC_C_KWH_PER_M3K * thick / 100.0f;
}

inline float covering_r_m2k_per_w(const char *covering) {
  // unset / unknown → parquet_laminate (contract §3.3).
  if (covering == nullptr || covering[0] == '\0' || std::strcmp(covering, "unset") == 0 ||
      std::strcmp(covering, "parquet_laminate") == 0)
    return 0.080f;
  if (std::strcmp(covering, "tile_stone") == 0)
    return 0.015f;
  if (std::strcmp(covering, "vinyl_linoleum") == 0)
    return 0.030f;
  if (std::strcmp(covering, "carpet") == 0)
    return 0.120f;
  return 0.080f;
}

inline float absorb_headroom_k(const char *covering) {
  if (covering == nullptr || covering[0] == '\0' || std::strcmp(covering, "unset") == 0 ||
      std::strcmp(covering, "parquet_laminate") == 0)
    return 1.8f;
  if (std::strcmp(covering, "tile_stone") == 0)
    return 3.0f;
  if (std::strcmp(covering, "vinyl_linoleum") == 0)
    return 2.4f;
  if (std::strcmp(covering, "carpet") == 0)
    return 1.2f;
  return 1.8f;
}

inline float effective_r_m2k_per_w(const char *covering, float r_override) {
  if (std::isfinite(r_override) && r_override >= 0.0f && r_override <= 0.25f)
    return r_override;
  return covering_r_m2k_per_w(covering);
}

inline float c_zone_kwh_per_k(float area_m2, float c_slab_per_m2_v, float c_struct = C_STRUCT_DEFAULT) {
  if (!(area_m2 > 0.0f))
    return 0.0f;
  const float cs = std::isfinite(c_struct) ? c_struct : C_STRUCT_DEFAULT;
  return area_m2 * (c_slab_per_m2_v + cs);
}

inline float tau_prior_h(float c_zone, float ua_effective_w_per_k) {
  if (!(ua_effective_w_per_k > 0.0f) || !(c_zone > 0.0f))
    return NAN;
  return c_zone / (ua_effective_w_per_k / 1000.0f);
}

inline float ua_confidence(uint16_t qualifying_days, float cv) {
  float day_factor = static_cast<float>(qualifying_days) / 21.0f;
  if (day_factor > 1.0f)
    day_factor = 1.0f;
  float cv_factor = 1.0f - cv;
  if (cv_factor < 0.0f)
    cv_factor = 0.0f;
  if (cv_factor > 1.0f)
    cv_factor = 1.0f;
  return day_factor * cv_factor;
}

inline bool ua_learned_in_band(float ua_learned, float ua_prior) {
  if (!(ua_prior > 0.0f) || !std::isfinite(ua_learned) || !(ua_learned > 0.0f))
    return false;
  return ua_learned >= ua_prior * 0.2f && ua_learned <= ua_prior * 5.0f;
}

inline bool use_learned_ua(float ua_learned, float confidence, uint16_t observed_days,
                           float ua_prior) {
  return std::isfinite(ua_learned) && ua_learned > 0.0f && confidence >= UA_LEARNED_CONFIDENCE_GATE &&
         observed_days >= UA_LEARNED_MIN_DAYS && ua_learned_in_band(ua_learned, ua_prior);
}

inline float ua_effective_w_per_k(float ua_prior, float ua_learned, float confidence,
                                  uint16_t observed_days, float weight_override = 1.0f) {
  float source = ua_prior;
  if (use_learned_ua(ua_learned, confidence, observed_days, ua_prior))
    source = ua_learned;
  float w = std::isfinite(weight_override) && weight_override > 0.0f ? weight_override : 1.0f;
  if (w < 0.25f)
    w = 0.25f;
  if (w > 4.0f)
    w = 4.0f;
  return source * w;
}

// Returns false when a write should be skipped (too soon / relative change too small).
inline bool should_write_ua_learned(float new_ua, float current_ua, float new_conf, float prev_conf,
                                    uint32_t now_epoch_s, uint32_t last_write_epoch_s) {
  if (!std::isfinite(new_ua) || !(new_ua > 0.0f))
    return false;
  const bool first = !(current_ua > 0.0f);
  const bool crossed_gate =
      prev_conf < UA_LEARNED_CONFIDENCE_GATE && new_conf >= UA_LEARNED_CONFIDENCE_GATE;
  float rel = 0.0f;
  if (current_ua > 0.0f)
    rel = std::fabs(new_ua - current_ua) / current_ua;
  const bool big_enough = first || rel >= UA_LEARNED_REL_CHANGE || crossed_gate;
  if (!big_enough)
    return false;
  if (!first && last_write_epoch_s != 0 && now_epoch_s >= last_write_epoch_s &&
      (now_epoch_s - last_write_epoch_s) < UA_LEARNED_MIN_INTERVAL_S && !crossed_gate)
    return false;
  return true;
}

struct HouseCalibration {
  bool ok{false};
  char reason[40]{"unknown"};
  float scale{1.0f};
  float u_base{U_BASE_DEFAULT};
  float u_wall{U_WALL_DEFAULT};
  float c_struct{C_STRUCT_DEFAULT};
};

inline HouseCalibration calibrate_house(float odin_hl_kw_per_k, float odin_tau_h,
                                        float ua_prior_sum_w_per_k, float c_slab_sum_kwh_per_k,
                                        float area_sum_m2) {
  HouseCalibration out;
  if (!(odin_hl_kw_per_k > 0.0f) || !(ua_prior_sum_w_per_k > 0.0f)) {
    std::strncpy(out.reason, "odin_unavailable", sizeof(out.reason) - 1);
    return out;
  }
  const float s = (odin_hl_kw_per_k * 1000.0f) / ua_prior_sum_w_per_k;
  if (!(s >= CALIB_SCALE_MIN && s <= CALIB_SCALE_MAX)) {
    std::strncpy(out.reason, "inconsistent_physics", sizeof(out.reason) - 1);
    out.scale = s;
    return out;
  }
  out.scale = s;
  out.u_base = s * U_BASE_DEFAULT;
  out.u_wall = s * U_WALL_DEFAULT;

  if (std::isfinite(odin_tau_h) && odin_tau_h > 0.0f && area_sum_m2 > 0.0f) {
    const float tm_odin = odin_hl_kw_per_k * odin_tau_h;
    const float c_struct = (tm_odin - c_slab_sum_kwh_per_k) / area_sum_m2;
    if (!(c_struct >= C_STRUCT_MIN && c_struct <= C_STRUCT_MAX)) {
      std::strncpy(out.reason, "inconsistent_physics", sizeof(out.reason) - 1);
      out.c_struct = c_struct;
      return out;
    }
    out.c_struct = c_struct;
  } else {
    out.c_struct = C_STRUCT_DEFAULT;
  }
  out.ok = true;
  std::strncpy(out.reason, "ok", sizeof(out.reason) - 1);
  return out;
}

}  // namespace physics
}  // namespace lune_touch
