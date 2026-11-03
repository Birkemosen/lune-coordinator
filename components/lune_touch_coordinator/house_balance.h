// =============================================================================
// House balance — rough hydraulic balance between V6 manifolds (pure C++, host-testable)
// =============================================================================
// Each V6 balances its own loops (the most demanding loop on the board = 1.0).
// Nothing balances the boards against each other: two manifolds on one pump see
// the same pressure, so a board with short, wide loops (20 mm PEX, cc 300) takes
// flow from a board with long, narrow loops (16 mm PEX, cc 200).
//
// Each board gets a need, and every board is scaled by its need / the house's
// largest need, so the board with the most demanding loop stays at 1.0; V6's own
// split inside a board is kept.
//
//   measured: once Touch has measured every enabled loop's full flow (valve_probe.h),
//     a loop's need is its heat demand (V6's UA, W/K) per litre of full flow, and a
//     board's need is its largest loop need. V6 uses the same shares inside the board.
//   model: until then, from what V6 reports (area, pipe inner diameter, spacing,
//     lead length): the pressure each loop needs to carry its design flow, and the
//     board's need is sqrt(worst loop pressure), as flow goes with sqrt(pressure).
//     Deliberately rough: no valve or manifold resistance, no riser, one design
//     heat flux. It only removes the systematic advantage of the "easy" manifold.
//
// A weight per board (WEIGHT_MIN..WEIGHT_MAX %, 100 = as computed) moves heat
// between floors that the model cannot see: ground contact, internal gains, sun.
// Opt-in (heat-source settings); V6 reverts to 1.0 when Touch stops sending.
// =============================================================================

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace lune_touch_house_balance {

struct Loop {
  float area_m2{0.0f};
  float inner_mm{0.0f};     // pipe inner diameter
  float spacing_mm{0.0f};   // pipe spacing (cc)
  float supply_m{2.0f};     // one-way lead from the manifold
  bool enabled{true};
  float ua_w_k{0.0f};       // V6's heat demand (UA_effective)
  float full_lmin{0.0f};    // measured full flow; 0 = not measured
};

enum class Basis : unsigned char { None, Model, Measured };

constexpr int WEIGHT_MIN = 50;
constexpr int WEIGHT_MAX = 150;
constexpr int WEIGHT_STEP = 5;

inline int clamp_weight(int pct) {
  pct = std::clamp(pct, WEIGHT_MIN, WEIGHT_MAX);
  return (pct + WEIGHT_STEP / 2) / WEIGHT_STEP * WEIGHT_STEP;
}

struct Params {
  float heat_flux_w_m2 = 40.0f;  // design heat flux, same for every loop
  float delta_t_k = 5.0f;        // design flow/return difference
  float min_scale = 0.4f;        // never throttle a whole board below this
};

/// Loop length in metres — the same formula as V6 (calculate_pipe_length_m_).
inline float loop_length_m(const Loop &l) {
  const float spacing_m = l.spacing_mm > 0.0f ? l.spacing_mm / 1000.0f : 0.20f;
  return l.area_m2 / spacing_m + 2.0f * l.supply_m + 2.0f;
}

/// Design flow in l/h: q = P / (c·ΔT), with 0.86 = 3600 / 4186.
inline float design_flow_lh(const Loop &l, const Params &p) {
  return l.area_m2 * p.heat_flux_w_m2 * 0.86f / p.delta_t_k;
}

/// Friction pressure drop (Pa) of water at ~35 °C: laminar below Re 2300, else Blasius.
inline float pressure_drop_pa(float flow_lh, float inner_mm, float length_m) {
  if (!(flow_lh > 0.0f) || !(inner_mm > 0.0f) || !(length_m > 0.0f))
    return 0.0f;
  constexpr float kRho = 994.0f, kNu = 0.72e-6f, kPi = 3.14159265f;
  const float d = inner_mm / 1000.0f;
  const float v = (flow_lh / 3.6e6f) / (kPi * d * d / 4.0f);
  const float re = v * d / kNu;
  const float f = re < 2300.0f ? 64.0f / re : 0.316f / std::pow(re, 0.25f);
  return f * (length_m / d) * kRho * v * v / 2.0f;
}

/// True when the loop has the data to be modelled.
inline bool modelled(const Loop &l) {
  return l.enabled && l.area_m2 > 0.0f && l.inner_mm > 0.0f;
}

/// Pressure (Pa) the loop needs to carry its design flow; 0 when not modelled.
inline float required_pa(const Loop &l, const Params &p) {
  return modelled(l) ? pressure_drop_pa(design_flow_lh(l, p), l.inner_mm, loop_length_m(l)) : 0.0f;
}

/// True when the loop has a measured full flow and a heat demand.
inline bool measured(const Loop &l) {
  return l.enabled && l.full_lmin > 0.0f && l.ua_w_k > 0.0f;
}

/// Heat demand per litre of full flow (W/K per l/min); 0 when not measured.
inline float measured_need(const Loop &l) {
  return measured(l) ? l.ua_w_k / l.full_lmin : 0.0f;
}

/// Measured when every enabled loop on every board with loops is measured.
inline bool all_measured(const Loop *loops, size_t n) {
  size_t enabled = 0;
  for (size_t k = 0; k < n; k++) {
    if (!loops[k].enabled || !(loops[k].area_m2 > 0.0f || loops[k].full_lmin > 0.0f))
      continue;
    enabled++;
    if (!measured(loops[k]))
      return false;
  }
  return enabled > 0;
}

/// Board scales for `boards` boards of `per_board` loops each (loops[b * per_board + z]).
/// Writes each board's need to need[b] (model: worst-loop pressure in Pa; measured:
/// W/K per l/min) and its scale to scale[b]. `weight_pct` (nullable) holds each board's
/// weight. A board without a need gets 1.0. Returns the number of boards with a need
/// (fewer than two: nothing to balance, every scale 1.0) and the basis in `basis`.
inline size_t board_scales(const Loop *loops, size_t boards, size_t per_board, const Params &p,
                           float *need, float *scale, const int *weight_pct = nullptr,
                           Basis *basis = nullptr) {
  const bool use_measured = all_measured(loops, boards * per_board);
  float house = 0.0f;
  size_t with_need = 0;
  float flow_need[16]{};  // need as a flow ratio (sqrt of pressure in the model) × weight
  for (size_t b = 0; b < boards && b < 16; b++) {
    float worst = 0.0f;
    for (size_t z = 0; z < per_board; z++) {
      const Loop &l = loops[b * per_board + z];
      worst = std::max(worst, use_measured ? measured_need(l) : required_pa(l, p));
    }
    need[b] = worst;
    scale[b] = 1.0f;
    if (worst > 0.0f) {
      with_need++;
      const float w = weight_pct != nullptr ? static_cast<float>(clamp_weight(weight_pct[b])) / 100.0f : 1.0f;
      flow_need[b] = (use_measured ? worst : std::sqrt(worst)) * w;
      house = std::max(house, flow_need[b]);
    }
  }
  if (basis != nullptr)
    *basis = with_need == 0 ? Basis::None : use_measured ? Basis::Measured : Basis::Model;
  if (with_need < 2)
    return with_need;
  for (size_t b = 0; b < boards && b < 16; b++) {
    if (flow_need[b] > 0.0f)
      scale[b] = std::clamp(flow_need[b] / house, p.min_scale, 1.0f);
  }
  return with_need;
}

}  // namespace lune_touch_house_balance
