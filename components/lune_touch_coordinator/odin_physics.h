#pragma once

#include <cmath>
#include <cstdint>

namespace esphome::lune_touch_coordinator::odin_physics {

// Odin 2.0 GET /api/physics — read-only display beside Touch UA/TM estimates (U5).
// B8 is reversed: Odin learns itself; Touch must not export physics advice to Odin.

struct PhysicsSnapshot {
  bool success{false};
  float heat_loss_kw_per_k{NAN};       // HL
  float tau_z1_h{NAN};                 // time constant hours (hl_tm_product semantics)
  float tau_z2_h{NAN};
  float passive_solar_z1{NAN};
  float passive_solar_z2{NAN};
  bool zone2_active{false};
};

inline bool deviation_flag(float odin_value, float touch_value, float ratio = 2.0f) {
  if (!std::isfinite(odin_value) || !std::isfinite(touch_value))
    return false;
  if (odin_value <= 0.0f || touch_value <= 0.0f)
    return false;
  const float hi = odin_value > touch_value ? odin_value : touch_value;
  const float lo = odin_value > touch_value ? touch_value : odin_value;
  return hi > lo * ratio;
}

}  // namespace esphome::lune_touch_coordinator::odin_physics
