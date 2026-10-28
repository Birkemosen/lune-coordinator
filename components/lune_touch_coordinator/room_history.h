#pragma once

// Room history for the room sheet: one bucket an hour per room for the last
// 7 days (mean temperature, last target, mean valve opening). The V6 poll feeds
// a sample a minute; a closed hour lands in the Store, which is one POD block so
// it can be written to flash as is and survive a reboot. Pure C++ — host-testable.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace esphome::lune_touch_coordinator::room_history {

static constexpr int16_t NONE = INT16_MIN;
static constexpr uint8_t NO_VALVE = 255;
static constexpr size_t MAX_ROOMS = 24;
static constexpr size_t HOURS = 168;
static constexpr uint32_t MAGIC = 0x4C524831;   // "LRH1"

struct Store {
  uint32_t magic;
  uint32_t newest_hour;   // unix_s / 3600 of the newest closed bucket; 0 = none yet
  char room_id[MAX_ROOMS][32];
  int16_t temp_x10[MAX_ROOMS][HOURS];
  int16_t target_x10[MAX_ROOMS][HOURS];
  uint8_t valve[MAX_ROOMS][HOURS];
};

class Recorder {
 public:
  /// Use `store` (owned by the caller, typically PSRAM). A store that does not
  /// carry MAGIC is cleared.
  void attach(Store *store) {
    store_ = store;
    if (store_ != nullptr && store_->magic != MAGIC)
      clear();
    reset_acc_(0);
  }

  void clear() {
    if (store_ == nullptr)
      return;
    std::memset(store_, 0, sizeof(Store));
    store_->magic = MAGIC;
    for (size_t r = 0; r < MAX_ROOMS; r++)
      blank_room_(r);
  }

  /// One sample for a room at unix time now_s. Returns true when an hour was closed
  /// by this call (the caller saves the store).
  bool sample(const char *room_id, float temp_c, bool has_temp, float target_c, bool has_target,
              float valve_pct, bool has_valve, uint32_t now_s) {
    if (store_ == nullptr || room_id == nullptr || room_id[0] == '\0' || now_s < 3600)
      return false;
    const bool closed = tick(now_s);
    const int slot = claim_(room_id);
    if (slot < 0)
      return closed;
    Acc &a = acc_[slot];
    if (has_temp && std::isfinite(temp_c) && temp_c > -40.0f && temp_c < 60.0f) {
      a.sum_t += static_cast<int32_t>(std::lround(temp_c * 10.0f));
      a.n_t++;
    }
    if (has_target && std::isfinite(target_c) && target_c > 0.0f && target_c < 40.0f)
      a.target = static_cast<int16_t>(std::lround(target_c * 10.0f));
    if (has_valve && std::isfinite(valve_pct)) {
      const float v = valve_pct < 0.0f ? 0.0f : (valve_pct > 100.0f ? 100.0f : valve_pct);
      a.sum_v += static_cast<uint32_t>(std::lround(v));
      a.n_v++;
    }
    return closed;
  }

  /// Close the running hour when now_s is in a later one. Returns true when it did.
  bool tick(uint32_t now_s) {
    if (store_ == nullptr)
      return false;
    const uint32_t hour = now_s / 3600;
    if (acc_hour_ == 0 || hour < acc_hour_) {   // first sample, or the clock went back
      reset_acc_(hour);
      return false;
    }
    if (hour == acc_hour_)
      return false;
    close_(acc_hour_);
    reset_acc_(hour);
    return true;
  }

  /// n hourly points for a room, the last one the hour in progress. Missing → NONE / NO_VALVE.
  /// Returns false when the room has no slot.
  bool series(const char *room_id, uint32_t now_s, size_t n, int16_t *temp_x10, int16_t *target_x10,
              uint8_t *valve) const {
    if (store_ == nullptr || room_id == nullptr || n == 0)
      return false;
    const int slot = find_(room_id);
    for (size_t k = 0; k < n; k++) {
      temp_x10[k] = target_x10[k] = NONE;
      valve[k] = NO_VALVE;
    }
    if (slot < 0)
      return false;
    const uint32_t now_hour = now_s / 3600;
    for (size_t k = 0; k < n; k++) {
      const uint32_t back = static_cast<uint32_t>(n - 1 - k);
      if (back > now_hour)
        continue;
      const uint32_t hour = now_hour - back;
      if (hour == acc_hour_) {
        const Acc &a = acc_[slot];
        if (a.n_t) temp_x10[k] = static_cast<int16_t>(round_div_(a.sum_t, a.n_t));
        target_x10[k] = a.target;
        if (a.n_v) valve[k] = static_cast<uint8_t>((a.sum_v + a.n_v / 2) / a.n_v);
      } else if (stored_(hour)) {
        const size_t i = hour % HOURS;
        temp_x10[k] = store_->temp_x10[slot][i];
        target_x10[k] = store_->target_x10[slot][i];
        valve[k] = store_->valve[slot][i];
      }
    }
    return true;
  }

  const Store *store() const { return store_; }

 private:
  struct Acc {
    int32_t sum_t{0};
    uint16_t n_t{0};
    int16_t target{NONE};
    uint32_t sum_v{0};
    uint16_t n_v{0};
  };

  static int32_t round_div_(int32_t s, uint16_t c) {
    return s >= 0 ? (s + c / 2) / c : -((-s + c / 2) / c);
  }

  bool stored_(uint32_t hour) const {
    return store_->newest_hour != 0 && hour <= store_->newest_hour && store_->newest_hour - hour < HOURS;
  }

  void blank_room_(size_t r) {
    for (size_t i = 0; i < HOURS; i++) {
      store_->temp_x10[r][i] = NONE;
      store_->target_x10[r][i] = NONE;
      store_->valve[r][i] = NO_VALVE;
    }
  }

  void reset_acc_(uint32_t hour) {
    acc_hour_ = hour;
    for (size_t r = 0; r < MAX_ROOMS; r++)
      acc_[r] = Acc{};
  }

  void close_(uint32_t hour) {
    // Hours the Touch missed (off, no clock) between the newest bucket and this one stay empty.
    if (store_->newest_hour != 0 && hour > store_->newest_hour) {
      uint32_t gap = hour - store_->newest_hour - 1;
      if (gap > HOURS) gap = HOURS;
      for (uint32_t g = 1; g <= gap; g++) {
        const size_t i = (hour - g) % HOURS;
        for (size_t r = 0; r < MAX_ROOMS; r++) {
          store_->temp_x10[r][i] = store_->target_x10[r][i] = NONE;
          store_->valve[r][i] = NO_VALVE;
        }
      }
    } else if (store_->newest_hour != 0 && hour <= store_->newest_hour) {
      return;   // already closed (clock jitter)
    }
    const size_t i = hour % HOURS;
    for (size_t r = 0; r < MAX_ROOMS; r++) {
      const Acc &a = acc_[r];
      store_->temp_x10[r][i] = a.n_t ? static_cast<int16_t>(round_div_(a.sum_t, a.n_t)) : NONE;
      store_->target_x10[r][i] = a.target;
      store_->valve[r][i] = a.n_v ? static_cast<uint8_t>((a.sum_v + a.n_v / 2) / a.n_v) : NO_VALVE;
    }
    store_->newest_hour = hour;
  }

  int find_(const char *room_id) const {
    for (size_t r = 0; r < MAX_ROOMS; r++)
      if (store_->room_id[r][0] != '\0' && std::strncmp(store_->room_id[r], room_id, 31) == 0)
        return static_cast<int>(r);
    return -1;
  }

  bool slot_empty_(size_t r) const {
    for (size_t i = 0; i < HOURS; i++)
      if (store_->temp_x10[r][i] != NONE || store_->valve[r][i] != NO_VALVE)
        return false;
    return acc_[r].n_t == 0 && acc_[r].n_v == 0;
  }

  // The room's slot; a new room takes a free slot, or one with no data left in the week.
  int claim_(const char *room_id) {
    const int found = find_(room_id);
    if (found >= 0)
      return found;
    int free_slot = -1;
    for (size_t r = 0; r < MAX_ROOMS && free_slot < 0; r++)
      if (store_->room_id[r][0] == '\0')
        free_slot = static_cast<int>(r);
    for (size_t r = 0; r < MAX_ROOMS && free_slot < 0; r++)
      if (slot_empty_(r))
        free_slot = static_cast<int>(r);
    if (free_slot < 0)
      return -1;
    std::strncpy(store_->room_id[free_slot], room_id, 31);
    store_->room_id[free_slot][31] = '\0';
    blank_room_(static_cast<size_t>(free_slot));
    acc_[free_slot] = Acc{};
    return free_slot;
  }

  Store *store_{nullptr};
  uint32_t acc_hour_{0};
  Acc acc_[MAX_ROOMS]{};
};

}  // namespace esphome::lune_touch_coordinator::room_history
