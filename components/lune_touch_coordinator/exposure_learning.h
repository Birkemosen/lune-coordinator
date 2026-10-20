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
// Both are clamped to 0..1, the range the forecast model uses, and are only
// reported once enough informative hours (sunny / windy) have been seen.
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

struct Sample {
  float dtdt_c_per_h = 0.0f;
  float temp_c = 0.0f;
  float out_c = 0.0f;
  float wind = 0.0f;       // alignment × speed / 10
  float ghi_wm2 = 0.0f;
  float valve = 0.0f;      // 0..1
  float supply_c = NAN;    // NAN = no supply reading (treated as no heat)
};

struct State {
  float theta[N]{};
  float p[N][N]{};
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

inline void features(const Sample &x, float phi[N]) {
  const float dt = x.temp_c - x.out_c;
  const float heat = std::isfinite(x.supply_c) ? std::clamp(x.valve, 0.0f, 1.0f) * std::max(0.0f, x.supply_c - x.temp_c) / 10.0f : 0.0f;
  phi[0] = -dt;
  phi[1] = -dt * std::max(0.0f, x.wind);
  phi[2] = std::max(0.0f, x.ghi_wm2) / 800.0f;
  phi[3] = heat;
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
  if (!usable(x))
    return false;
  float phi[N];
  features(x, phi);
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

inline Learned result(const State &s) {
  Learned r{};
  const float a = s.theta[0], b = s.theta[1], c = s.theta[2];
  if (s.solar_n >= SOLAR_MIN_SAMPLES && std::isfinite(c)) {
    r.solar_valid = true;
    r.solar = std::clamp(c / SOLAR_REF_C_PER_H, 0.0f, 1.0f);
  }
  if (s.wind_n >= WIND_MIN_SAMPLES && a > 1e-4f && std::isfinite(b)) {
    r.wind_valid = true;
    r.wind = std::clamp(b / a, 0.0f, 1.0f);
  }
  return r;
}

}  // namespace lune_touch_learn
