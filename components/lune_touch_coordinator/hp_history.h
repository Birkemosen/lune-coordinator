#pragma once

// Heat-pump flow/return history for the Heat sheet. Asgard keeps one row a
// minute in GET /dashboard/history?type=min&from=&to= as
//   [[unix_s, feed×10, return×10, …, flags, …], …]   (-32768 = no value)
// flags (column 10): bit 6–9 = operation mode (1 = hot water, 6 = legionella).
// The body is ~114 KiB for 24 h, so it is parsed as a stream (any chunking)
// and averaged into fixed buckets. Pure C++ — host-testable.

#include <cstddef>
#include <cstdint>

namespace esphome::lune_touch_coordinator::hp_history {

static constexpr int16_t NONE = INT16_MIN;
static constexpr uint8_t MAX_BUCKETS = 96;
static constexpr int32_t ASGARD_NONE = -32768;
static constexpr uint8_t COL_FLAGS = 10;
/// Heat produced today, kWh x 10; Asgard resets it at local midnight.
static constexpr uint8_t COL_PROD = 12;
static constexpr uint8_t MODE_HOT_WATER = 1;
static constexpr uint8_t MODE_LEGIONELLA = 6;
// Bucket marks (any minute in the bucket).
static constexpr uint8_t MARK_DHW = 1;
static constexpr uint8_t MARK_LEGIONELLA = 2;

// One averaged series: buckets [from_ts + i·step_s, from_ts + (i+1)·step_s).
struct Series {
  uint32_t from_ts{0};
  uint16_t step_s{0};
  uint8_t n{0};
  int16_t feed_x10[MAX_BUCKETS];
  int16_t return_x10[MAX_BUCKETS];
  uint8_t marks[MAX_BUCKETS];   // MARK_* bits
  uint16_t heat_x10[MAX_BUCKETS];   // heat produced in the bucket, kWh x 10
  uint32_t fetched_ms{0};   // 0 = never fetched
  bool valid{false};
};

class Bucketer {
 public:
  void begin(uint32_t from_ts, uint16_t step_s, uint8_t n) {
    from_ts_ = from_ts;
    step_s_ = step_s == 0 ? 1 : step_s;
    n_ = n > MAX_BUCKETS ? MAX_BUCKETS : n;
    for (uint8_t i = 0; i < MAX_BUCKETS; i++) {
      sum_f_[i] = sum_r_[i] = 0;
      cnt_f_[i] = cnt_r_[i] = 0;
      marks_[i] = 0;
      heat_[i] = 0;
    }
    prod_ = last_prod_ = ASGARD_NONE;
    depth_ = 0;
    col_ = 0;
    in_num_ = false;
    neg_ = false;
    val_ = 0;
    ts_ = 0;
    feed_ = ret_ = ASGARD_NONE;
    flags_ = -1;
    rows_ = 0;
    max_ts_ = 0;
  }

  // Feed any slice of the body.
  void feed(const char *data, size_t len) {
    for (size_t i = 0; i < len; i++) step_(data[i]);
  }

  uint32_t rows() const { return rows_; }
  /// Newest row timestamp seen (0 = none). Used to detect a clock offset.
  uint32_t max_ts() const { return max_ts_; }

  // Write the averages into out; returns the number of buckets with data.
  uint8_t finish(Series *out) const {
    out->from_ts = from_ts_;
    out->step_s = step_s_;
    out->n = n_;
    uint8_t filled = 0;
    for (uint8_t i = 0; i < n_; i++) {
      out->feed_x10[i] = cnt_f_[i] ? static_cast<int16_t>(round_div_(sum_f_[i], cnt_f_[i])) : NONE;
      out->return_x10[i] = cnt_r_[i] ? static_cast<int16_t>(round_div_(sum_r_[i], cnt_r_[i])) : NONE;
      out->marks[i] = marks_[i];
      out->heat_x10[i] = heat_[i];
      if (cnt_f_[i] || cnt_r_[i]) filled++;
    }
    out->valid = filled >= 2;
    return filled;
  }

 private:
  static int32_t round_div_(int32_t s, uint16_t c) {
    return s >= 0 ? (s + c / 2) / c : -((-s + c / 2) / c);
  }

  void end_num_() {
    if (!in_num_) return;
    const int64_t v = neg_ ? -val_ : val_;
    if (depth_ == 2) {
      if (col_ == 0) ts_ = v < 0 || v > UINT32_MAX ? 0 : static_cast<uint32_t>(v);
      else if (col_ == 1) feed_ = static_cast<int32_t>(v);
      else if (col_ == 2) ret_ = static_cast<int32_t>(v);
      else if (col_ == COL_FLAGS) flags_ = static_cast<int32_t>(v);
      else if (col_ == COL_PROD) prod_ = static_cast<int32_t>(v);
    }
    in_num_ = false;
    neg_ = false;
    val_ = 0;
  }

  void end_row_() {
    rows_++;
    if (ts_ > max_ts_) max_ts_ = ts_;
    // Heat since the previous row. The counter is daily: a drop is midnight, and
    // the new value is what was produced since then.
    int32_t heat = 0;
    if (prod_ != ASGARD_NONE && prod_ >= 0 && prod_ < 100000) {
      if (last_prod_ != ASGARD_NONE)
        heat = prod_ >= last_prod_ ? prod_ - last_prod_ : prod_;
      if (heat > 500) heat = 0;   // > 50 kWh between two minute rows: a glitch, not heat
      last_prod_ = prod_;
    }
    if (ts_ >= from_ts_) {
      const uint32_t b = (ts_ - from_ts_) / step_s_;
      if (b < n_ && heat > 0 && heat_[b] < 60000) heat_[b] = static_cast<uint16_t>(heat_[b] + heat);
      if (b < n_) {
        // Plausible water temperatures only (−20 … 90 °C).
        if (feed_ != ASGARD_NONE && feed_ > -200 && feed_ < 900) { sum_f_[b] += feed_; cnt_f_[b]++; }
        if (ret_ != ASGARD_NONE && ret_ > -200 && ret_ < 900) { sum_r_[b] += ret_; cnt_r_[b]++; }
        if (flags_ >= 0) {
          const uint8_t mode = static_cast<uint8_t>((flags_ >> 6) & 0x0F);
          if (mode == MODE_HOT_WATER) marks_[b] |= MARK_DHW;
          else if (mode == MODE_LEGIONELLA) marks_[b] |= MARK_LEGIONELLA;
        }
      }
    }
    col_ = 0;
    ts_ = 0;
    feed_ = ret_ = ASGARD_NONE;
    flags_ = -1;
    prod_ = ASGARD_NONE;
  }

  void step_(char c) {
    if (c >= '0' && c <= '9') {
      in_num_ = true;
      if (val_ < 100000000000LL) val_ = val_ * 10 + (c - '0');
      return;
    }
    if (c == '-') { in_num_ = true; neg_ = true; return; }
    if (c == '.') return;   // Asgard sends integers; a fraction would only scale ×10 wrongly, so ignore the dot
    end_num_();
    if (c == '[') { depth_++; if (depth_ == 2) { col_ = 0; ts_ = 0; feed_ = ret_ = ASGARD_NONE; flags_ = -1; } }
    else if (c == ']') { if (depth_ == 2) end_row_(); if (depth_ > 0) depth_--; }
    else if (c == ',' && depth_ == 2) col_++;
  }

  uint32_t from_ts_{0};
  uint16_t step_s_{1};
  uint8_t n_{0};
  int32_t sum_f_[MAX_BUCKETS]{};
  int32_t sum_r_[MAX_BUCKETS]{};
  uint16_t cnt_f_[MAX_BUCKETS]{};
  uint16_t cnt_r_[MAX_BUCKETS]{};
  uint8_t marks_[MAX_BUCKETS]{};
  uint8_t depth_{0};
  uint8_t col_{0};
  bool in_num_{false};
  bool neg_{false};
  int64_t val_{0};
  uint32_t ts_{0};
  int32_t feed_{ASGARD_NONE};
  int32_t ret_{ASGARD_NONE};
  int32_t flags_{-1};
  int32_t prod_{ASGARD_NONE};
  int32_t last_prod_{ASGARD_NONE};
  uint16_t heat_[MAX_BUCKETS]{};
  uint32_t rows_{0};
  uint32_t max_ts_{0};
};

// Asgard has served history timestamps either as true UTC or as local time written as
// epoch (2026-10-07.01: two hours ahead in CEST). Given the newest row and the true time,
// return the offset to add to a UTC window, rounded to 15 minutes; 0 when it looks like UTC.
inline int32_t clock_offset_s(uint32_t newest_row_ts, uint32_t now_utc) {
  if (newest_row_ts == 0) return 0;
  const int32_t diff = static_cast<int32_t>(newest_row_ts - now_utc);
  if (diff < 600 || diff > 14 * 3600) return 0;   // within 10 min of now (or absurd) → UTC
  return ((diff + 450) / 900) * 900;
}

}  // namespace esphome::lune_touch_coordinator::hp_history
