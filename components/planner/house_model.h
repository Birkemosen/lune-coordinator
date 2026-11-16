#pragma once

// House model for the shadow planner: one thermal mass C (kWh/K) losing heat through
// UA · (1 + k_wind · wind) to outdoors, gaining from space heating and the sun:
//
//   C · dT/dt = Q_floor + g_sun · sun + P_int − UA · (1 + k_wind · wind) · (T − T_out)
//
// Q_floor is the space-heating heat after the floor's delay: heat put into the floor
// circuit reaches the room through the screed, so it is filtered with a first-order lag
// of tau_h hours. P_int is the steady internal gain (people, appliances, the hot-water
// tank and pipes). Per hour, divided by C, the model is linear in five features:
//
//   ΔT = a·Q_floor + b·sun − c·(T − T_out) − d·wind·(T − T_out) + e
//   a = 1/C, b = g_sun/C, c = UA/C, d = UA·k_wind/C, e = P_int/C
//
// The learner keeps exponentially weighted normal equations (forgetting `lambda` per
// hour, ~2 weeks memory) with a ridge toward the prior, so it runs in a few hundred
// bytes on Touch and gives the same answer on the host. Pure C++, host-testable.

#include <cmath>
#include <cstdint>

namespace lune_planner {

struct HouseParams {
  float ua_kw_per_k{0.15f};     // heat loss in calm weather
  float c_kwh_per_k{23.0f};     // thermal mass
  float g_sun_kw_per_wm2{0.0f}; // kW of gain per W/m² of global irradiance
  float k_wind_per_ms{0.0f};    // extra loss per m/s of wind, fraction of UA
  float p_int_kw{0.3f};         // steady internal gain
  float tau_h{3.0f};            // floor delay
  bool valid{false};
};

// One hour of the model: temperature at the end of the hour.
inline float house_step(const HouseParams &p, float t_c, float q_floor_kw, float out_c, float sun_wm2,
                        float wind_ms) {
  const float loss = p.ua_kw_per_k * (1.0f + p.k_wind_per_ms * wind_ms) * (t_c - out_c);
  return t_c + (q_floor_kw + p.g_sun_kw_per_wm2 * sun_wm2 + p.p_int_kw - loss) / p.c_kwh_per_k;
}

// Floor delay: first-order lag with time constant tau_h, one-hour steps.
inline float floor_filter(float q_floor_prev_kw, float q_kw, float tau_h) {
  const float k = tau_h > 0.05f ? 1.0f - std::exp(-1.0f / tau_h) : 1.0f;
  return q_floor_prev_kw + (q_kw - q_floor_prev_kw) * k;
}

// Heat needed over an hour to go from t0 to t1 (kWh), the inverse of house_step.
inline float house_heat_needed(const HouseParams &p, float t0_c, float t1_c, float out_c, float sun_wm2,
                               float wind_ms) {
  const float tm = 0.5f * (t0_c + t1_c);
  const float loss = p.ua_kw_per_k * (1.0f + p.k_wind_per_ms * wind_ms) * (tm - out_c);
  return p.c_kwh_per_k * (t1_c - t0_c) + loss - p.g_sun_kw_per_wm2 * sun_wm2 - p.p_int_kw;
}

class HouseLearner {
 public:
  static constexpr int N = 5;

  explicit HouseLearner(const HouseParams &prior, float lambda = 0.997f, float ridge = 30.0f)
      : prior_(prior), lambda_(lambda), ridge_(ridge) {
    reset();
  }

  void reset() {
    for (int i = 0; i < N; i++) {
      b_[i] = 0;
      for (int j = 0; j < N; j++) a_[i][j] = 0;
    }
    q_floor_ = 0;
    samples_ = 0;
    params_ = prior_;
    params_.valid = false;
  }

  float q_floor() const { return q_floor_; }
  uint32_t samples() const { return samples_; }
  const HouseParams &params() const { return params_; }

  // Advance the floor filter by one hour of space-heating heat (call every hour, also
  // when the hour cannot be learned from, so the filter stays continuous).
  float push_heat(float q_kw) {
    q_floor_ = floor_filter(q_floor_, std::isfinite(q_kw) ? q_kw : 0.0f, params_.tau_h);
    return q_floor_;
  }

  // Learn from one hour: temperature t0 → t1 with the floor heat already pushed.
  // Hours with a missing value are skipped.
  void learn(float t0_c, float t1_c, float out_c, float sun_wm2, float wind_ms) {
    if (!std::isfinite(t0_c) || !std::isfinite(t1_c) || !std::isfinite(out_c)) return;
    if (!std::isfinite(sun_wm2)) sun_wm2 = 0;
    if (!std::isfinite(wind_ms)) wind_ms = 0;
    const float dt = t1_c - t0_c;
    if (std::fabs(dt) > 1.5f) return;   // a sensor jump, not physics
    const float d = t0_c - out_c;
    // Features scaled to similar magnitude: kW, sun/100, K/10, wind·K/100, 1.
    const float x[N] = {q_floor_, sun_wm2 / 100.0f, -d / 10.0f, -wind_ms * d / 100.0f, 1.0f};
    for (int i = 0; i < N; i++) {
      b_[i] = lambda_ * b_[i] + x[i] * dt;
      for (int j = 0; j < N; j++) a_[i][j] = lambda_ * a_[i][j] + x[i] * x[j];
    }
    samples_++;
    solve_();
  }

 private:
  // Prior coefficients in the scaled feature space.
  void prior_theta_(float th[N]) const {
    const float c = prior_.c_kwh_per_k;
    th[0] = 1.0f / c;
    th[1] = prior_.g_sun_kw_per_wm2 * 100.0f / c;
    th[2] = prior_.ua_kw_per_k * 10.0f / c;
    th[3] = prior_.ua_kw_per_k * prior_.k_wind_per_ms * 100.0f / c;
    th[4] = prior_.p_int_kw / c;
  }

  void solve_() {
    float m[N][N + 1];
    float th0[N];
    prior_theta_(th0);
    // Ridge toward the prior, scaled per feature so each is pulled with equal weight
    // relative to its typical size: (A + r·D)θ = b + r·D·θ0.
    for (int i = 0; i < N; i++) {
      const float di = ridge_ * scale_[i];
      for (int j = 0; j < N; j++) m[i][j] = a_[i][j] + (i == j ? di : 0.0f);
      m[i][N] = b_[i] + di * th0[i];
    }
    for (int col = 0; col < N; col++) {
      int piv = col;
      for (int r = col + 1; r < N; r++)
        if (std::fabs(m[r][col]) > std::fabs(m[piv][col])) piv = r;
      if (std::fabs(m[piv][col]) < 1e-12f) return;
      if (piv != col)
        for (int k = 0; k <= N; k++) { const float t = m[col][k]; m[col][k] = m[piv][k]; m[piv][k] = t; }
      for (int r = 0; r < N; r++) {
        if (r == col) continue;
        const float f = m[r][col] / m[col][col];
        for (int k = col; k <= N; k++) m[r][k] -= f * m[col][k];
      }
    }
    float th[N];
    for (int i = 0; i < N; i++) th[i] = m[i][N] / m[i][i];
    // Physical bounds: positive mass, loss and wind effect; sun may not be negative.
    if (!(th[0] > 1e-3f) || !(th[2] > 1e-4f)) return;
    HouseParams p = params_;
    p.c_kwh_per_k = 1.0f / th[0];
    p.ua_kw_per_k = th[2] / 10.0f * p.c_kwh_per_k;
    p.g_sun_kw_per_wm2 = std::fmax(0.0f, th[1] / 100.0f * p.c_kwh_per_k);
    p.k_wind_per_ms = std::fmax(0.0f, th[3] / 100.0f * p.c_kwh_per_k / p.ua_kw_per_k);
    p.p_int_kw = std::fmax(0.0f, th[4] * p.c_kwh_per_k);
    if (p.c_kwh_per_k < 2.0f || p.c_kwh_per_k > 200.0f || p.ua_kw_per_k < 0.02f || p.ua_kw_per_k > 2.0f) return;
    p.valid = samples_ >= 72;
    params_ = p;
  }

  // Typical feature variance (kW², (sun/100)², (K/10)², (wind·K/100)², 1) for the ridge.
  static constexpr float scale_[N] = {4.0f, 1.0f, 2.0f, 1.0f, 1.0f};

  HouseParams prior_;
  HouseParams params_;
  float lambda_;
  float ridge_;
  float a_[N][N];
  float b_[N];
  float q_floor_{0};
  uint32_t samples_{0};
};

}  // namespace lune_planner
