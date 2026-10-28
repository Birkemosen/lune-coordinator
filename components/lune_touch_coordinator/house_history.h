#pragma once

// Odin's hourly ledger for the Next heating and Weather sheets. Odin keeps one
// row an hour in GET /dashboard/history?type=hour&from=&to= as
//   [[hour_ts, outside×10, cons×100, prod×100, …, exp_prod×100, exp_room×100,
//     room×100, dhw_cons×100, dhw_prod×100, …, op_mode, …], …]  (-32768 = none)
// The body is ~25 KiB for 7 days, so it is parsed as a stream (any chunking)
// into one slot per hour. Pure C++ — host-testable.

#include <cstddef>
#include <cstdint>

namespace esphome::lune_touch_coordinator::house_history {

static constexpr int16_t NONE = INT16_MIN;
static constexpr size_t MAX_HOURS = 168;
static constexpr int32_t ODIN_NONE = -32768;
static constexpr uint8_t COL_OUT = 1;        // °C × 10
static constexpr uint8_t COL_PROD = 3;       // kWh × 100, heat produced in the hour (space + hot water)
static constexpr uint8_t COL_EXP_PROD = 9;   // kWh × 100, heat the plan had for the hour
static constexpr uint8_t COL_EXP_ROOM = 10;  // °C × 100
static constexpr uint8_t COL_ROOM = 11;      // °C × 100
static constexpr uint8_t COL_DHW_PROD = 13;  // kWh × 100
static constexpr uint8_t COL_MODE = 18;      // 0 off, 1 hot water, 2 heating, …
static constexpr uint8_t COLS = 7;

struct Series {
  uint32_t from_ts{0};   // first slot's hour (UTC); slot i = from_ts + i·3600
  uint8_t n{0};
  int16_t out_x10[MAX_HOURS];
  int16_t room_x100[MAX_HOURS];
  int16_t exp_room_x100[MAX_HOURS];
  int16_t prod_x100[MAX_HOURS];
  int16_t plan_x100[MAX_HOURS];
  int16_t dhw_x100[MAX_HOURS];
  uint8_t mode[MAX_HOURS];
  uint32_t fetched_ms{0};   // 0 = never fetched
  bool valid{false};
};

class Parser {
 public:
  void begin(uint32_t from_ts, uint8_t n) {
    from_ts_ = from_ts;
    n_ = n > MAX_HOURS ? MAX_HOURS : n;
    for (size_t i = 0; i < MAX_HOURS; i++) {
      for (uint8_t c = 0; c < COLS; c++) v_[c][i] = NONE;
      mode_[i] = 255;
    }
    depth_ = col_ = 0;
    in_num_ = neg_ = false;
    val_ = 0;
    rows_ = 0;
    reset_row_();
  }

  void feed(const char *data, size_t len) {
    for (size_t i = 0; i < len; i++) step_(data[i]);
  }

  uint32_t rows() const { return rows_; }

  // Copy into out; returns the number of hours with a room or outside value.
  uint8_t finish(Series *out) const {
    out->from_ts = from_ts_;
    out->n = n_;
    uint8_t filled = 0;
    for (uint8_t i = 0; i < n_; i++) {
      out->out_x10[i] = v_[0][i];
      out->room_x100[i] = v_[1][i];
      out->exp_room_x100[i] = v_[2][i];
      out->prod_x100[i] = v_[3][i];
      out->plan_x100[i] = v_[4][i];
      out->dhw_x100[i] = v_[5][i];
      out->mode[i] = mode_[i];
      if (v_[0][i] != NONE || v_[1][i] != NONE) filled++;
    }
    out->valid = filled >= 2;
    return filled;
  }

 private:
  static int16_t clip_(int32_t v) {
    if (v == ODIN_NONE || v <= -32767 || v > 32767) return NONE;
    return static_cast<int16_t>(v);
  }

  void reset_row_() {
    ts_ = 0;
    for (uint8_t c = 0; c < COLS; c++) row_[c] = ODIN_NONE;
  }

  void end_num_() {
    if (!in_num_) return;
    const int64_t v64 = neg_ ? -val_ : val_;
    const int32_t v = v64 < INT32_MIN ? INT32_MIN : (v64 > INT32_MAX ? INT32_MAX : static_cast<int32_t>(v64));
    if (depth_ == 2) {
      switch (col_) {
        case 0: ts_ = v64 < 0 || v64 > UINT32_MAX ? 0 : static_cast<uint32_t>(v64); break;
        case COL_OUT: row_[0] = v; break;
        case COL_ROOM: row_[1] = v; break;
        case COL_EXP_ROOM: row_[2] = v; break;
        case COL_PROD: row_[3] = v; break;
        case COL_EXP_PROD: row_[4] = v; break;
        case COL_DHW_PROD: row_[5] = v; break;
        case COL_MODE: row_[6] = v; break;
        default: break;
      }
    }
    in_num_ = neg_ = false;
    val_ = 0;
  }

  void end_row_() {
    rows_++;
    if (ts_ >= from_ts_) {
      const uint32_t i = (ts_ - from_ts_) / 3600;
      if (i < n_) {
        // Plausible values only: outside −50 … 50 °C, rooms 0 … 40 °C, energy 0 … 100 kWh.
        if (row_[0] != ODIN_NONE && row_[0] > -500 && row_[0] < 500) v_[0][i] = clip_(row_[0]);
        if (row_[1] != ODIN_NONE && row_[1] > 0 && row_[1] < 4000) v_[1][i] = clip_(row_[1]);
        if (row_[2] != ODIN_NONE && row_[2] > 0 && row_[2] < 4000) v_[2][i] = clip_(row_[2]);
        for (uint8_t c = 3; c <= 5; c++)
          if (row_[c] != ODIN_NONE && row_[c] >= 0 && row_[c] < 10000) v_[c][i] = clip_(row_[c]);
        if (row_[6] >= 0 && row_[6] < 255) mode_[i] = static_cast<uint8_t>(row_[6]);
      }
    }
    reset_row_();
  }

  void step_(char c) {
    if (c >= '0' && c <= '9') {
      in_num_ = true;
      if (val_ < 100000000000LL) val_ = val_ * 10 + (c - '0');
      return;
    }
    if (c == '-') { in_num_ = true; neg_ = true; return; }
    if (c == '.') return;   // Odin sends integers
    end_num_();
    if (c == '[') { depth_++; if (depth_ == 2) { col_ = 0; reset_row_(); } }
    else if (c == ']') { if (depth_ == 2) end_row_(); if (depth_ > 0) depth_--; }
    else if (c == ',' && depth_ == 2) col_++;
  }

  uint32_t from_ts_{0};
  uint8_t n_{0};
  int16_t v_[COLS][MAX_HOURS]{};
  uint8_t mode_[MAX_HOURS]{};
  uint8_t depth_{0};
  uint8_t col_{0};
  bool in_num_{false};
  bool neg_{false};
  int64_t val_{0};
  uint32_t ts_{0};
  int32_t row_[COLS]{};
  uint32_t rows_{0};
};

}  // namespace esphome::lune_touch_coordinator::house_history
