#pragma once

// Heat pump and floor circuit for the shadow planner: what a planned output costs and
// which flow temperature delivers it.
//
//   floor circuit:  P ≈ f0 + f1 · (zone feed − room)            kW the floors take
//   buffer:         HP feed ≈ zone feed + b0 + b1 · P            the tank between them
//   heat pump:      COP ≈ c0 + c1 · HP feed + c2 · outdoor + c3 · defrost minutes
//   defrost:        minutes per running hour ≈ d0 + d1 · outdoor (below defrost_below_c, else 0)
//
// COP follows the flow temperature, not the output as such: on 2 Oct–16 Nov 2026 the HP
// feed and outdoor temperature explained 72 % of the hourly COP, output and outdoor 51 %.
// Higher output needs a higher feed through the floor curve and the buffer, which is
// where the part-load cost comes from. Each relation is learned from full space-heating
// hours with exponentially weighted least squares (a few dozen bytes each).
// Defrost: the output reads 0 while the pump defrosts and the electricity keeps running,
// so an hour with defrosts has a lower COP at the same feed. On 13–20 Nov 2026 each
// defrost minute in an hour cost about 0.037 in COP, and defrost took 6.6 − 0.83 · outdoor
// minutes per running hour below 7 °C (none above). With the term the hourly COP error
// fell from 0.28 to 0.13 out of sample.
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
  float c3{-0.037f};                     //       + c3·defrost minutes in the hour
  float d0{6.6f}, d1{-0.83f};            // defrost minutes per running hour = d0 + d1·outdoor
  float defrost_below_c{7.0f};           // no defrost at or above this outdoor temperature
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

// Defrost minutes in an hour the compressor runs throughout, at `out_c`.
inline float defrost_min_for(const HeatPumpParams &p, float out_c) {
  if (!std::isfinite(out_c) || out_c >= p.defrost_below_c) return 0.0f;
  const float m = p.d0 + p.d1 * out_c;
  return m < 0.0f ? 0.0f : (m > 20.0f ? 20.0f : m);
}

inline float cop_at(const HeatPumpParams &p, float hp_feed_c, float out_c, float defrost_min = 0.0f) {
  const float c = p.c0 + p.c1 * hp_feed_c + p.c2 * out_c + p.c3 * defrost_min;
  return c < 1.5f ? 1.5f : (c > 8.0f ? 8.0f : c);
}

// Electricity for an hour at `kw` (kWh), the planner's cost per hour before the price,
// with the defrosts the outdoor temperature brings.
inline float elec_for(const HeatPumpParams &p, float kw, float room_c, float out_c) {
  if (kw <= 0) return 0;
  return kw / cop_at(p, hp_feed_for(p, kw, room_c), out_c, defrost_min_for(p, out_c));
}

// The highest output whose marginal COP (the extra heat per extra electricity of the last
// 0.1 kW) is still at least `frac` of the COP at minimum output, at `out_c`: above it each
// extra kW costs disproportionately more. From the learned floor curve, buffer and COP.
inline float efficient_kw(const HeatPumpParams &p, float out_c, float room_c, float frac) {
  const float dm = defrost_min_for(p, out_c);
  const float ref = frac * cop_at(p, hp_feed_for(p, p.min_kw, room_c), out_c, dm);
  float k = p.min_kw;
  for (float next = k + 0.1f; next <= p.max_kw + 1e-3f; next += 0.1f) {
    const float de = elec_for(p, next, room_c, out_c) - elec_for(p, k, room_c, out_c);
    if (!(de > 0) || (next - k) / de < ref) break;
    k = next;
  }
  return k;
}

class HeatPumpLearner {
 public:
  explicit HeatPumpLearner(const HeatPumpParams &prior = HeatPumpParams{}) : prior_(prior), params_(prior) {}

  // One full space-heating hour (compressor on the whole hour, no hot water) with
  // `defrost_min` minutes of defrost in it. The floor curve and the buffer only learn from
  // hours without defrost: a defrost pulls the feed down for a while.
  void learn(float kw, float elec_kwh, float hp_feed_c, float zone_feed_c, float room_c, float out_c,
             float defrost_min = 0.0f) {
    if (!(kw > 0.5f) || !(elec_kwh > 0.2f) || !std::isfinite(hp_feed_c) || !std::isfinite(zone_feed_c) ||
        !std::isfinite(room_c) || !std::isfinite(out_c) || !std::isfinite(defrost_min))
      return;
    if (defrost_min <= 0.0f) {
      const float xf[2] = {1.0f, zone_feed_c - room_c};
      floor_.add(xf, kw);
      const float xb[2] = {1.0f, kw};
      buffer_.add(xb, hp_feed_c - zone_feed_c);
    }
    const float xc[4] = {1.0f, hp_feed_c, out_c, defrost_min};
    cop_.add(xc, kw / elec_kwh);
    solve_();
  }

  // Defrost minutes in an hour with `run_min` minutes of compressor time (at least half an
  // hour), below defrost_below_c.
  void learn_defrost(float defrost_min, float run_min, float out_c) {
    if (!std::isfinite(defrost_min) || !(run_min >= 30.0f) || !std::isfinite(out_c) || out_c >= prior_.defrost_below_c)
      return;
    const float x[2] = {1.0f, out_c};
    defrost_.add(x, defrost_min * 60.0f / run_min);
    float d[2];
    const float pd[2] = {prior_.d0, prior_.d1};
    if (defrost_.solve(pd, 2.0f, d) && d[1] <= 0.0f) { params_.d0 = d[0]; params_.d1 = d[1]; }
  }

  const HeatPumpParams &params() const { return params_; }
  uint32_t samples() const { return cop_.samples(); }
  uint32_t defrost_samples() const { return defrost_.samples(); }

 private:
  void solve_() {
    HeatPumpParams p = params_;
    float f[2], b[2], c[4];
    const float pf[2] = {prior_.f0, prior_.f1}, pb[2] = {prior_.b0, prior_.b1};
    const float pc[4] = {prior_.c0, prior_.c1, prior_.c2, prior_.c3};
    if (floor_.solve(pf, 2.0f, f) && f[1] > 0.05f) { p.f0 = f[0]; p.f1 = f[1]; }
    if (buffer_.solve(pb, 2.0f, b)) { p.b0 = b[0]; p.b1 = b[1]; }
    if (cop_.solve(pc, 2.0f, c) && c[1] < 0.0f && c[3] <= 0.0f) { p.c0 = c[0]; p.c1 = c[1]; p.c2 = c[2]; p.c3 = c[3]; }
    p.valid = cop_.samples() >= 24;
    params_ = p;
  }

  HeatPumpParams prior_;
  HeatPumpParams params_;
  EwLeastSquares<2> floor_{0.995f};
  EwLeastSquares<2> buffer_{0.995f};
  EwLeastSquares<4> cop_{0.995f};
  EwLeastSquares<2> defrost_{0.995f};
};

}  // namespace lune_planner
