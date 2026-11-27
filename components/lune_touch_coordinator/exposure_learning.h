// =============================================================================
// Exposure learning — a room's solar and wind factors from its own data
// (pure C++, host-testable)
// =============================================================================
// Each hour, per room, one sample of
//
//   dT/dt = -a·(T-Tout) - b·(T-Tout)·w + c·(G/800) + d·v·(Ts-T)/10
//
//   T     room temperature, Tout outdoor temperature
//   w     wind on the room's facades: alignment × speed / 10 m/s
//   G     global irradiance (W/m²)
//   v     valve opening 0..1, Ts manifold supply temperature
//
// is fitted by recursive least squares with forgetting. Keeping the floor's
// heat as its own term means sun and wind are learned while heating runs too.
//
//   solar factor = c / SOLAR_REF_C_PER_H   (1.0 = the room rises 2 °C/h in full sun)
//   wind factor  = b / a                   (1.0 = 10 m/s on the facade doubles the loss)
//
// The solar factor is clamped to 0..1, the wind factor to 0..WIND_MAX: a room with a
// door and a roof window on the windward side can lose well over twice as much at
// 10 m/s (Laura, 1. sal, 2026-11-13: 0.23 °C/h at 8 m/s against the 0.12 its UA
// predicts, with the floor still giving heat; the old 0..1 clamp held it at 1.0).
// Both are reported once enough informative hours (sunny / windy) have been seen.
// effective wind exposure = wind factor / slab_charge WIND_LOSS_FACTOR (same scale as
// the manual presets).
//
// Heavy floors (2026-11-27): on the ground floor (cast concrete, tile, C ≈ 6 kWh/K
// against UA ≈ 30 W/K) the room moves ~0.005 °C/h per K, far below the sensor's
// 0.1 °C step, and the slab keeps giving heat for hours after the valve closes. The
// fit's a went to ~0 and b/a to the clamp in four of six rooms. Three guards:
//   - the floor's heat enters through a first-order lag of the floor response time
//     (Sample::lag_h, V6's floor_lag_h), not the valve opening of the hour;
//   - b/a uses at least the physics a (UA / C, result()'s a_prior): unmodelled slab
//     heat only ever biases the fitted a low, so a smaller fitted a is not believed;
//   - b is shrunk by its own uncertainty, b·b²/(b² + σb²), so a wind effect the data
//     cannot separate from noise reads as small, not as the clamp.
// =============================================================================

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace lune_touch_learn {

static constexpr int N = 4;                    // a, b, c, d
static constexpr float SOLAR_REF_C_PER_H = 2.0f;
static constexpr float FORGET = 0.998f;        // ~500 h memory
static constexpr uint16_t SOLAR_MIN_SAMPLES = 20;   // hours with G >= 200 W/m²
static constexpr uint16_t WIND_MIN_SAMPLES = 30;    // hours with w >= 0.3
static constexpr float SOLAR_SAMPLE_WM2 = 200.0f;
static constexpr float WIND_SAMPLE = 0.3f;
static constexpr float WIND_MAX = 2.0f;        // extra loss at 10 m/s head-on: at most ×3 (the fit is noisy)

struct Sample {
  float dtdt_c_per_h = 0.0f;
  float temp_c = 0.0f;
  float out_c = 0.0f;
  float wind = 0.0f;       // alignment × speed / 10
  float ghi_wm2 = 0.0f;
  float valve = 0.0f;      // 0..1
  float supply_c = NAN;    // NAN = no supply reading (treated as no heat)
  float lag_h = 0.0f;      // floor response time; 0 = the heat acts within the hour
  float dt_h = 1.0f;       // hours since the previous sample
};

struct State {
  float theta[N]{};
  float p[N][N]{};
  float heat_f = 0.0f;     // the floor's heat after the lag
  float resid_var = 0.0f;  // running mean of the squared prediction error
  bool heat_init = false;
  uint16_t solar_n = 0;
  uint16_t wind_n = 0;
  uint16_t n = 0;
  bool init = false;
};

struct Learned {
  bool solar_valid = false;
  float solar = 0.0f;
  bool wind_valid = false;
  float wind = 0.0f;
};

inline void reset(State &s) {
  s = State{};
  for (int i = 0; i < N; i++)
    s.p[i][i] = 100.0f;
  s.init = true;
}

/// Heat the floor is given this hour (valve × supply above room, per 10 K).
inline float valve_heat(const Sample &x) {
  return std::isfinite(x.supply_c) ? std::clamp(x.valve, 0.0f, 1.0f) * std::max(0.0f, x.supply_c - x.temp_c) / 10.0f : 0.0f;
}

/// `heat` is the floor's heat reaching the room (valve_heat after the lag).
inline void features(const Sample &x, float heat, float phi[N]) {
  const float dt = x.temp_c - x.out_c;
  phi[0] = -dt;
  phi[1] = -dt * std::max(0.0f, x.wind);
  phi[2] = std::max(0.0f, x.ghi_wm2) / 800.0f;
  phi[3] = heat;
}

/// Advances the floor lag by one sample and returns the heat reaching the room.
inline float lag_heat(State &s, const Sample &x) {
  const float h = valve_heat(x);
  if (!s.heat_init || !(x.lag_h > 0.0f)) {
    s.heat_f = h;
    s.heat_init = true;
    return h;
  }
  const float dt = std::clamp(std::isfinite(x.dt_h) ? x.dt_h : 1.0f, 0.1f, 3.0f);
  s.heat_f += (h - s.heat_f) * (1.0f - std::exp(-dt / x.lag_h));
  return s.heat_f;
}

/// A sample is usable when it is physically plausible (no window open, no
/// sensor jump): |dT/dt| under 4 °C/h and a real indoor/outdoor difference.
inline bool usable(const Sample &x) {
  return std::isfinite(x.dtdt_c_per_h) && std::isfinite(x.temp_c) && std::isfinite(x.out_c) &&
         std::fabs(x.dtdt_c_per_h) < 4.0f && (x.temp_c - x.out_c) > 2.0f;
}

/// One RLS step. Returns false when the sample was rejected.
inline bool update(State &s, const Sample &x) {
  if (!s.init)
    reset(s);
  const float heat = lag_heat(s, x);   // the lag runs on every hour, used or not
  if (!usable(x))
    return false;
  float phi[N];
  features(x, heat, phi);
  float pphi[N]{};
  for (int i = 0; i < N; i++)
    for (int j = 0; j < N; j++)
      pphi[i] += s.p[i][j] * phi[j];
  float denom = FORGET;
  for (int i = 0; i < N; i++)
    denom += phi[i] * pphi[i];
  if (!(denom > 1e-6f))
    return false;
  float err = x.dtdt_c_per_h;
  for (int i = 0; i < N; i++)
    err -= s.theta[i] * phi[i];
  // Prediction error variance: a plain mean over the first 100 samples, then the
  // fit's own forgetting.
  const float w = s.n < 100 ? 1.0f / static_cast<float>(s.n + 1) : 1.0f - FORGET;
  s.resid_var += w * (err * err - s.resid_var);
  float k[N];
  for (int i = 0; i < N; i++)
    k[i] = pphi[i] / denom;
  for (int i = 0; i < N; i++)
    s.theta[i] += k[i] * err;
  for (int i = 0; i < N; i++)
    for (int j = 0; j < N; j++)
      s.p[i][j] = (s.p[i][j] - k[i] * pphi[j]) / FORGET;
  if (s.n < 65535) s.n++;
  if (x.ghi_wm2 >= SOLAR_SAMPLE_WM2 && s.solar_n < 65535) s.solar_n++;
  if (x.wind >= WIND_SAMPLE && s.wind_n < 65535) s.wind_n++;
  return true;
}

/// a_prior: the room's loss rate from physics, UA / C in 1/h (0 = unknown).
inline Learned result(const State &s, float a_prior = 0.0f) {
  Learned r{};
  const float a = std::max(s.theta[0], a_prior > 0.0f ? a_prior : 0.0f);
  const float c = s.theta[2];
  // Shrink b by its uncertainty (σb² = residual variance × P[1][1]).
  const float b_raw = s.theta[1];
  const float var_b = std::max(0.0f, s.resid_var * s.p[1][1]);
  const float b = b_raw > 0.0f ? b_raw * b_raw * b_raw / (b_raw * b_raw + var_b) : 0.0f;
  if (s.solar_n >= SOLAR_MIN_SAMPLES && std::isfinite(c)) {
    r.solar_valid = true;
    r.solar = std::clamp(c / SOLAR_REF_C_PER_H, 0.0f, 1.0f);
  }
  if (s.wind_n >= WIND_MIN_SAMPLES && a > 1e-4f && std::isfinite(b)) {
    r.wind_valid = true;
    r.wind = std::clamp(b / a, 0.0f, WIND_MAX);
  }
  return r;
}

}  // namespace lune_touch_learn
