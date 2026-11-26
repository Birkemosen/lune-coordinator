#pragma once

// Shadow planner interface. Each hour the planner sees the measured history up to
// and including the current hour, and for the next HORIZON_H hours only what a live
// planner could know: forecast weather, prices and the comfort schedule. It returns a
// 48-hour plan. It never commands anything (docs/planner_history_format.md).
// Pure C++, host-testable; the replay runs the same code over recorded history.

#include <cmath>
#include <cstddef>
#include <cstdint>

#include "history_record.h"

namespace lune_planner {

static constexpr uint8_t HORIZON_H = 48;

// Columns a live planner has for future hours. Everything else is hidden.
inline bool forecastable(uint8_t col) {
  switch (col) {
    case H_OUTDOOR_C: case H_WIND_MS: case H_WIND_DIR_DEG: case H_SOLAR_WM2: case H_HUMIDITY_PCT:
    case H_SUN_S_WM2: case H_SUN_W_WM2: case H_SUN_E_WM2:
    case H_PRICE_EUR_KWH: case H_SCHED_SP_C: case H_SCHED_MIN_C: case H_SCHED_MAX_C:
      return true;
    default:
      return false;
  }
}

inline Record mask_future(const Record &r) {
  Record out;
  out.hour_start = r.hour_start;
  for (uint8_t c = 0; c < MAX_COLS; c++)
    if (forecastable(c)) out.v[c] = r.v[c];
  out.v[H_MODE_FLAGS] = MODE_FORECAST_WEATHER;
  return out;
}

struct Context {
  const Record *past{nullptr};     // measured hours, oldest first; past[n_past - 1] is now
  size_t n_past{0};
  Record ahead[HORIZON_H];         // masked future hours (hour_start always set)
  uint8_t n_ahead{0};
};

struct Plan {
  uint32_t from_hour{0};                 // hour_start of slot 0 (the hour after now)
  uint8_t n{0};
  float heat_kw[HORIZON_H];              // planned space-heating output
  float flow_target_c[HORIZON_H];        // flow target the planner would send
  float room_expected_c[HORIZON_H];      // expected house temperature at hour end
  uint8_t dhw[HORIZON_H];                // 1 = hot water, 2 = legionella
  float cost_eur{NAN};                   // planned electricity cost over the horizon

  void clear(uint32_t from) {
    from_hour = from;
    n = 0;
    cost_eur = NAN;
    for (uint8_t i = 0; i < HORIZON_H; i++) {
      heat_kw[i] = flow_target_c[i] = room_expected_c[i] = NAN;
      dhw[i] = 0;
    }
  }
};

class Planner {
 public:
  virtual ~Planner() = default;
  virtual void plan(const Context &ctx, Plan *out) = 0;
};

// F0 placeholder: plans nothing, so the replay can be checked end to end.
class NullPlanner : public Planner {
 public:
  void plan(const Context &ctx, Plan *out) override {
    out->clear(ctx.n_ahead ? ctx.ahead[0].hour_start : 0);
    out->n = ctx.n_ahead;
  }
};

// Build the context for hour index `now` of a contiguous history.
inline void build_context(const Record *hist, size_t n, size_t now, Context *ctx) {
  ctx->past = hist;
  ctx->n_past = now + 1;
  ctx->n_ahead = 0;
  for (size_t i = now + 1; i < n && ctx->n_ahead < HORIZON_H; i++)
    ctx->ahead[ctx->n_ahead++] = mask_future(hist[i]);
}

}  // namespace lune_planner
