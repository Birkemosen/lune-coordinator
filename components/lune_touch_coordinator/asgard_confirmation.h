#pragma once

#include <cmath>
#include <cstdint>

namespace esphome::lune_touch_coordinator::asgard_confirmation {

enum class Status : uint8_t { SENT, CONFIRMED, MISMATCH, UNREACHABLE, BLOCKED };

constexpr const char *status_name(Status status) {
  switch (status) {
    case Status::SENT: return "sent";
    case Status::CONFIRMED: return "confirmed";
    case Status::MISMATCH: return "mismatch";
    case Status::UNREACHABLE: return "unreachable";
    case Status::BLOCKED: return "blocked";
  }
  return "unreachable";
}

constexpr uint8_t MAX_READ_ATTEMPTS = 3;
constexpr uint32_t readback_backoff_ms(uint8_t failed_attempt) {
  return failed_attempt == 0 ? 250U : 500U;
}

inline bool values_match(float requested, float confirmed) {
  return std::isfinite(requested) && std::isfinite(confirmed) &&
         std::fabs(requested - confirmed) <= 0.05f;
}

}  // namespace esphome::lune_touch_coordinator::asgard_confirmation
