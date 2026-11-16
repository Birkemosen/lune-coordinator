#pragma once

// Start values for the shadow planner on Touch until it learns on the device (F3).
// They are what the replay fitted for this house on its 2 Oct–16 Nov 2026 history
// (make planner-replay HISTORY=planner-history): the house model with the daily energy
// balance, and the heat pump and floor circuit from full space-heating hours. Another
// house starts from these too and should not trust them until F3 has learned its own.

#include "heat_pump_model.h"
#include "house_model.h"

namespace lune_planner {

inline HouseParams seed_house_params() {
  HouseParams p;
  p.ua_kw_per_k = 0.161f;
  p.ca_kwh_per_k = 13.3f;
  p.cs_kwh_per_k = 30.0f;
  p.h_kw_per_k = 0.70f;
  p.g_kw_per_100wm2 = 0.79f;   // global irradiance
  p.p_int_kw = 0.58f;
  p.kw_per_ms = 0.044f;
  p.p_cos_kw = 0.22f;
  p.p_sin_kw = 0.40f;
  p.valid = true;
  return p;
}

inline HeatPumpParams seed_heat_pump_params() {
  HeatPumpParams p;
  p.f0 = 1.36f;
  p.f1 = 0.441f;
  p.b0 = 1.36f;
  p.b1 = 0.41f;
  p.c0 = 7.49f;
  p.c1 = -0.107f;
  p.c2 = 0.094f;
  p.valid = true;
  return p;
}

}  // namespace lune_planner
