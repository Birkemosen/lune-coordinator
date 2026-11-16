#pragma once

// Heat pump and floor circuit for the shadow planner: what a planned output costs and
// which flow temperature delivers it.
//
//   floor circuit:  P ≈ f0 + f1 · (zone feed − room)            kW the floors take
//   buffer:         HP feed ≈ zone feed + b0 + b1 · P            the tank between them
//   heat pump:      COP ≈ c0 + c1 · HP feed + c2 · outdoor
//
// COP follows the flow temperature, not the output as such: on 2 Oct–16 Nov 2026 the HP
// feed and outdoor temperature explained 72 % of the hourly COP, output and outdoor 51 %.
// Higher output needs a higher feed through the floor curve and the buffer, which is
// where the part-load cost comes from. Each relation is learned from full space-heating
// hours with exponentially weighted least squares (a few dozen bytes each).
// Pure C++, host-testable.

#include <cmath>
#include <cstdint>

namespace lune_planner {

// y ≈ a·x0 + b·x1 + c·x2 (x0 = 1 for an intercept), exponentially weighted.
template <int N> class EwLeastSquares {
 public:
  explicit EwLeastSquares(float lambda = 0.995f) : lambda_(lambda) {}
  void add(const float x[N], float y) {
    for (int i = 0; i < N; i++) {
      b_[i] = lambda_ * b_[i] + x[i] * y;
      for (int j = 0; j < N; j++) a_[i][j] = lambda_ * a_[i][j] + x[i] * x[j];
    }
    n_++;
  }
  uint32_t samples() const { return n_; }
  // Solve with a small ridge toward `prior`; false while singular.
  bool solve(const float prior[N], float ridge, float out[N]) const {
    double m[N][N + 1];
    for (int i = 0; i < N; i++) {
      for (int j = 0; j < N; j++) m[i][j] = a_[i][j] + (i == j ? ridge : 0.0);
      m[i][N] = b_[i] + ridge * prior[i];
    }
    for (int c = 0; c < N; c++) {
      int p = c;
      for (int r = c + 1; r < N; r++)
        if (std::fabs(m[r][c]) > std::fabs(m[p][c])) p = r;
      if (std::fabs(m[p][c]) < 1e-12) return false;
      if (p != c)
        for (int k = 0; k <= N; k++) { const double t = m[c][k]; m[c][k] = m[p][k]; m[p][k] = t; }
      for (int r = 0; r < N; r++) {
        if (r == c) continue;
        const double f = m[r][c] / m[c][c];
        for (int k = c; k <= N; k++) m[r][k] -= f * m[c][k];
      }
    }
    for (int i = 0; i < N; i++) out[i] = static_cast<float>(m[i][N] / m[i][i]);
    return true;
  }

 private:
  float lambda_;
  double a_[N][N]{};
  double b_[N]{};
  uint32_t n_{0};
};

struct HeatPumpParams {
  float f0{1.0f}, f1{0.45f};             // floor: kW = f0 + f1·(zone feed − room)
  float b0{2.0f}, b1{0.3f};              // buffer: HP feed − zone feed = b0 + b1·kW
  float c0{7.5f}, c1{-0.105f}, c2{0.09f};  // COP = c0 + c1·HP feed + c2·outdoor
  float min_kw{3.2f}, max_kw{8.0f};
  bool valid{false};
};

// Zone feed the floors need to take `kw` with the rooms at `room_c`.
inline float zone_feed_for(const HeatPumpParams &p, float kw, float room_c) {
  return room_c + (kw - p.f0) / (p.f1 > 0.05f ? p.f1 : 0.05f);
}

inline float hp_feed_for(const HeatPumpParams &p, float kw, float room_c) {
  return zone_feed_for(p, kw, room_c) + p.b0 + p.b1 * kw;
}

inline float cop_at(const HeatPumpParams &p, float hp_feed_c, float out_c) {
  const float c = p.c0 + p.c1 * hp_feed_c + p.c2 * out_c;
  return c < 1.5f ? 1.5f : (c > 8.0f ? 8.0f : c);
}

// Electricity for an hour at `kw` (kWh), the planner's cost per hour before the price.
inline float elec_for(const HeatPumpParams &p, float kw, float room_c, float out_c) {
  if (kw <= 0) return 0;
  return kw / cop_at(p, hp_feed_for(p, kw, room_c), out_c);
}

class HeatPumpLearner {
 public:
  explicit HeatPumpLearner(const HeatPumpParams &prior = HeatPumpParams{}) : prior_(prior), params_(prior) {}

  // One full space-heating hour (compressor on the whole hour, no hot water).
  void learn(float kw, float elec_kwh, float hp_feed_c, float zone_feed_c, float room_c, float out_c) {
    if (!(kw > 0.5f) || !(elec_kwh > 0.2f) || !std::isfinite(hp_feed_c) || !std::isfinite(zone_feed_c) ||
        !std::isfinite(room_c) || !std::isfinite(out_c))
      return;
    const float xf[2] = {1.0f, zone_feed_c - room_c};
    floor_.add(xf, kw);
    const float xb[2] = {1.0f, kw};
    buffer_.add(xb, hp_feed_c - zone_feed_c);
    const float xc[3] = {1.0f, hp_feed_c, out_c};
    cop_.add(xc, kw / elec_kwh);
    solve_();
  }

  const HeatPumpParams &params() const { return params_; }
  uint32_t samples() const { return cop_.samples(); }

 private:
  void solve_() {
    HeatPumpParams p = params_;
    float f[2], b[2], c[3];
    const float pf[2] = {prior_.f0, prior_.f1}, pb[2] = {prior_.b0, prior_.b1};
    const float pc[3] = {prior_.c0, prior_.c1, prior_.c2};
    if (floor_.solve(pf, 2.0f, f) && f[1] > 0.05f) { p.f0 = f[0]; p.f1 = f[1]; }
    if (buffer_.solve(pb, 2.0f, b)) { p.b0 = b[0]; p.b1 = b[1]; }
    if (cop_.solve(pc, 2.0f, c) && c[1] < 0.0f) { p.c0 = c[0]; p.c1 = c[1]; p.c2 = c[2]; }
    p.valid = cop_.samples() >= 24;
    params_ = p;
  }

  HeatPumpParams prior_;
  HeatPumpParams params_;
  EwLeastSquares<2> floor_{0.995f};
  EwLeastSquares<2> buffer_{0.995f};
  EwLeastSquares<3> cop_{0.995f};
};

}  // namespace lune_planner
