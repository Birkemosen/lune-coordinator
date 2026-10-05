#pragma once

#include <cstddef>
#include <cstdint>

namespace lune_touch_forecast_timeline {

constexpr int64_t MIN_VALID_EPOCH_S = 1700000000LL;
constexpr size_t NO_INDEX = static_cast<size_t>(-1);
constexpr uint16_t PERSISTED_FORECAST_CACHE_VERSION = 3;

enum class CacheValidation : uint8_t {
  FRESH = 0,
  EXPIRED,
  UNALIGNABLE,
};

inline bool provider_timezone_valid(const char *timezone) {
  return timezone != nullptr && timezone[0] != '\0';
}

inline bool cache_version_supported(uint16_t version) {
  return version == PERSISTED_FORECAST_CACHE_VERSION;
}

inline bool hourly_arrays_match(size_t times, size_t temperatures, size_t wind_speeds,
                                size_t wind_directions, size_t shortwave, size_t precipitation,
                                size_t cloud_cover) {
  return times > 0 && times == temperatures && times == wind_speeds &&
         times == wind_directions && times == shortwave && times == precipitation &&
         times == cloud_cover;
}

// Open-Meteo's `timeformat=unixtime` hourly series is consecutive UTC hours.
// UTC epochs deliberately avoid local daylight-saving gaps and repeats.
inline bool timestamps_are_consecutive_hours(const int64_t *timestamps, size_t count) {
  if (timestamps == nullptr || count == 0 || timestamps[0] < MIN_VALID_EPOCH_S)
    return false;
  for (size_t i = 1; i < count; i++) {
    if (timestamps[i] != timestamps[i - 1] + 3600)
      return false;
  }
  return true;
}

inline size_t first_index_at_or_after(const int64_t *timestamps, size_t count, int64_t epoch_s) {
  if (!timestamps_are_consecutive_hours(timestamps, count) || epoch_s < MIN_VALID_EPOCH_S)
    return NO_INDEX;
  for (size_t i = 0; i < count; i++) {
    if (timestamps[i] >= epoch_s)
      return i;
  }
  return NO_INDEX;
}

inline CacheValidation validate_cache(int64_t fetch_epoch_s, const char *provider_timezone,
                                      const int64_t *timestamps, size_t count, int64_t now_epoch_s,
                                      uint32_t max_age_s) {
  if (fetch_epoch_s < MIN_VALID_EPOCH_S || now_epoch_s < MIN_VALID_EPOCH_S ||
      !provider_timezone_valid(provider_timezone) ||
      !timestamps_are_consecutive_hours(timestamps, count)) {
    return CacheValidation::UNALIGNABLE;
  }
  if (now_epoch_s < fetch_epoch_s || now_epoch_s - fetch_epoch_s > static_cast<int64_t>(max_age_s) ||
      first_index_at_or_after(timestamps, count, now_epoch_s) == NO_INDEX) {
    return CacheValidation::EXPIRED;
  }
  return CacheValidation::FRESH;
}

}  // namespace lune_touch_forecast_timeline
