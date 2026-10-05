#include "forecast_timeline.h"

#include <cstdio>
#include <cstdlib>

using namespace lune_touch_forecast_timeline;

namespace {

int failures = 0;

void expect(bool condition, const char *description) {
  if (condition) {
    std::printf("PASS  %s\n", description);
  } else {
    std::printf("FAIL  %s\n", description);
    failures++;
  }
}

void test_fetch_at_0005_and_1230() {
  constexpr int64_t midnight = 1735689600;  // 2025-01-01T00:00:00Z
  int64_t hours[48]{};
  for (size_t i = 0; i < 48; i++)
    hours[i] = midnight + static_cast<int64_t>(i) * 3600;

  expect(first_index_at_or_after(hours, 48, midnight + 5 * 60) == 1,
         "00:05 starts at the next forecast hour, not midnight");
  expect(first_index_at_or_after(hours, 48, midnight + 12 * 3600 + 30 * 60) == 13,
         "12:30 starts at 13:00, not midnight");
}

void test_day_rollover_and_dst() {
  constexpr int64_t midnight = 1735689600;
  int64_t rollover[4] = {midnight + 22 * 3600, midnight + 23 * 3600,
                          midnight + 24 * 3600, midnight + 25 * 3600};
  expect(first_index_at_or_after(rollover, 4, midnight + 23 * 3600 + 45 * 60) == 2,
         "day rollover chooses the next UTC hour");

  // Europe/Copenhagen DST transitions change local labels, not UTC hourly spacing.
  int64_t spring_forward[4] = {1743292800, 1743296400, 1743300000, 1743303600};
  int64_t fall_back[4] = {1729987200, 1729990800, 1729994400, 1729998000};
  expect(timestamps_are_consecutive_hours(spring_forward, 4),
         "DST forward remains consecutive in Unix time");
  expect(timestamps_are_consecutive_hours(fall_back, 4),
         "DST backward remains consecutive in Unix time");
  expect(first_index_at_or_after(spring_forward, 4, spring_forward[1] + 30 * 60) == 2,
         "DST forward aligns from the current/next epoch hour");
  expect(first_index_at_or_after(fall_back, 4, fall_back[1] + 30 * 60) == 2,
         "DST backward aligns from the current/next epoch hour");
}

void test_reboot_cache_validation() {
  constexpr int64_t fetch_epoch = 1735689600;
  int64_t hours[72]{};
  for (size_t i = 0; i < 72; i++)
    hours[i] = fetch_epoch + static_cast<int64_t>(i) * 3600;

  expect(validate_cache(fetch_epoch, "Europe/Copenhagen", hours, 72,
                        fetch_epoch + 90 * 60, 2 * 3600) == CacheValidation::FRESH,
         "reboot restores a fresh, alignable cache");
  expect(validate_cache(fetch_epoch, "Europe/Copenhagen", hours, 72,
                        fetch_epoch + 2 * 3600 + 1, 2 * 3600) == CacheValidation::EXPIRED,
         "reboot rejects an expired cache");
  expect(validate_cache(fetch_epoch, "", hours, 72, fetch_epoch + 60, 2 * 3600) ==
             CacheValidation::UNALIGNABLE,
         "reboot rejects a cache without provider timezone");
  expect(!cache_version_supported(1) && cache_version_supported(PERSISTED_FORECAST_CACHE_VERSION),
         "reboot safely invalidates the previous forecast-cache schema");
}

void test_missing_or_mismatched_time_arrays() {
  expect(!hourly_arrays_match(0, 72, 72, 72, 72, 72, 72), "missing hourly time array is rejected");
  expect(!hourly_arrays_match(71, 72, 72, 72, 72, 72, 72), "mismatched hourly time array is rejected");
  expect(!hourly_arrays_match(72, 72, 72, 72, 72, 71, 72), "mismatched precipitation array is rejected");
  expect(hourly_arrays_match(72, 72, 72, 72, 72, 72, 72), "matching hourly arrays are accepted");
}

}  // namespace

int main() {
  test_fetch_at_0005_and_1230();
  test_day_rollover_and_dst();
  test_reboot_cache_validation();
  test_missing_or_mismatched_time_arrays();
  if (failures != 0) {
    std::printf("%d test(s) FAILED.\n", failures);
    return EXIT_FAILURE;
  }
  std::printf("All timeline tests passed.\n");
  return EXIT_SUCCESS;
}
