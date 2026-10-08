// =============================================================================
// House balance — rough hydraulic balance between V6 manifolds (pure C++, host-testable)
// =============================================================================
// Each V6 balances its own loops (the most demanding loop on the board = 1.0).
// Nothing balances the boards against each other: two manifolds on one pump see
// the same pressure, so a board with short, wide loops (20 mm PEX, cc 300) takes
// flow from a board with long, narrow loops (16 mm PEX, cc 200).
//
// Touch estimates, from what V6 reports (area, pipe inner diameter, spacing,
// lead length), the pressure each loop needs to carry its design flow, and scales
// every board by sqrt(board's worst loop / house's worst loop). The board with
// the most demanding loop stays at 1.0; V6's own split inside a board is kept.
//
// Deliberately rough: no valve or manifold resistance, no riser, one design
// heat flux. It only removes the systematic advantage of the "easy" manifold.
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
};

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

/// Board scales for `boards` boards of `per_board` loops each (loops[b * per_board + z]).
/// Writes each board's worst-loop pressure to worst_pa[b] and its scale to scale[b].
/// A board without modelled loops gets 1.0. Returns the number of modelled boards;
/// with fewer than two there is nothing to balance and every scale is 1.0.
inline size_t board_scales(const Loop *loops, size_t boards, size_t per_board, const Params &p,
                           float *worst_pa, float *scale) {
  float house = 0.0f;
  size_t modelled_boards = 0;
  for (size_t b = 0; b < boards; b++) {
    float worst = 0.0f;
    for (size_t z = 0; z < per_board; z++)
      worst = std::max(worst, required_pa(loops[b * per_board + z], p));
    worst_pa[b] = worst;
    scale[b] = 1.0f;
    if (worst > 0.0f) {
      modelled_boards++;
      house = std::max(house, worst);
    }
  }
  if (modelled_boards < 2)
    return modelled_boards;
  for (size_t b = 0; b < boards; b++) {
    if (worst_pa[b] > 0.0f)
      scale[b] = std::clamp(std::sqrt(worst_pa[b] / house), p.min_scale, 1.0f);
  }
  return modelled_boards;
}

}  // namespace lune_touch_house_balance
