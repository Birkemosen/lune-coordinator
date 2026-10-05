// =============================================================================
// Forecast — ESPHome Component Implementation
// =============================================================================
// HTTP(S) fetch + JSON parse + preload evaluation run in a dedicated FreeRTOS
// task on Core 1, priority 1 (same pattern as hv6_helios_client /
// hv6_asgard_bridge). The Open-Meteo response is parsed into a fixed 48-hour
// cache that is persisted in NVS so a reboot does not lose the forecast.
// =============================================================================

#include "forecast.h"
#include "esphome/core/log.h"
#include "esphome/components/network/util.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "nvs.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <ctime>
#include <algorithm>
#include <memory>
#include <ArduinoJson.h>

namespace esphome {
namespace forecast {

static const char *const TAG = "forecast";
static const char *const NVS_NS = "hv6f";
static const char *const NVS_KEY_HOURS = "hours";
static const char *const NVS_KEY_META = "meta";

// Minimum free *internal* (DMA-capable) heap required before attempting a fetch.
// Everything large is already on PSRAM: the response body (heap_caps SPIRAM), the
// ArduinoJson pool (PsRamAllocator) and the mbedTLS buffers/session
// (CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC). So a fetch's *internal* footprint is just
// the esp_http_client rx/tx buffers (~4 KB) + esp-tls glue + WiFi headroom — far
// below the old 48 KB figure, which predated the PSRAM routing and kept the fetch
// from ever running on this BLE board (~30 KB free at idle). fetch_forecast_ logs
// the all-time internal-heap low-water mark after each attempt so this floor can
// be tuned to the measured trough; keep a margin above it for WiFi/lwIP.
static constexpr size_t MIN_INTERNAL_HEAP_B = 24 * 1024;

// ArduinoJson allocator that prefers PSRAM (keeps the parse pool off the internal
// heap that TLS and WiFi need), falling back to internal heap when no PSRAM is
// present so the build still works on non-PSRAM modules.
struct PsRamAllocator : ArduinoJson::Allocator {
  void *allocate(size_t n) override {
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
    return p ? p : heap_caps_malloc(n, MALLOC_CAP_INTERNAL);
  }
  void deallocate(void *p) override { heap_caps_free(p); }
  void *reallocate(void *p, size_t n) override {
    void *q = heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM);
    return q ? q : heap_caps_realloc(p, n, MALLOC_CAP_INTERNAL);
  }
};

// Below this epoch (2020-09-13) the SNTP clock has not synced yet — the ESP is
// still on its 1970 boot time, so wall-clock-dependent logic must wait.
static constexpr uint32_t MIN_VALID_EPOCH = 1600000000u;

struct CacheMeta {
  uint32_t base_epoch;
  uint32_t fetch_epoch;  ///< wall-clock of the fetch that produced this cache
  uint8_t count;
};

static uint32_t epoch_s_() { return static_cast<uint32_t>(::time(nullptr)); }

// =============================================================================
// ESPHome lifecycle
// =============================================================================

void Hv6Forecast::setup() {
  if (!zone_controller_ || !config_store_) {
    ESP_LOGE(TAG, "zone_controller or config_store not set; forecast disabled");
    this->mark_failed();
    return;
  }

  for (uint8_t i = 0; i < hv6::NUM_ZONES; i++)
    zone_peak_in_h_[i] = -1;

  hours_lock_ = xSemaphoreCreateMutex();

  load_cache_();

  xTaskCreatePinnedToCore(task_func_, "forecast", STACK_SIZE, this, PRIORITY, &task_handle_, CORE);

  hv6::ForecastConfig cfg = config_store_->get_forecast_config();
  ESP_LOGI(TAG, "Forecast task spawned (core=%d prio=%u stack=%u)",
           (int) CORE, (unsigned) PRIORITY, (unsigned) STACK_SIZE);
  ESP_LOGI(TAG, "  config: enabled=%s lat=%.4f lon=%.4f fetch=%us cached_hours=%u",
           cfg.enabled ? "yes" : "no", cfg.latitude, cfg.longitude,
           cfg.fetch_interval_s, hours_count_);
}

void Hv6Forecast::dump_config() {
  hv6::ForecastConfig cfg = config_store_ ? config_store_->get_forecast_config() : hv6::ForecastConfig{};
  ESP_LOGCONFIG(TAG, "HV6 Forecast (wind preload):");
  ESP_LOGCONFIG(TAG, "  Enabled: %s  Status: %s", cfg.enabled ? "yes" : "no", get_status_str());
  ESP_LOGCONFIG(TAG, "  Location: %.4f, %.4f", cfg.latitude, cfg.longitude);
  ESP_LOGCONFIG(TAG, "  Fetch every %u s, recompute every %u s", cfg.fetch_interval_s, cfg.recompute_interval_s);
  ESP_LOGCONFIG(TAG, "  Model: threshold=%.2f gain=%.2f max_offset=%.2f°C",
                cfg.load_threshold, cfg.gain_c_per_load, cfg.max_offset_c);
}

const char *Hv6Forecast::get_status_str() const {
  switch (status_) {
    case Status::OK:       return "ok";
    case Status::NO_DATA:  return "no data";
    case Status::STALE:    return "stale";
    case Status::EXTERNAL: return "external helios";
    default:               return "disabled";
  }
}

uint32_t Hv6Forecast::get_fetch_age_s() const {
  if (last_fetch_epoch_ == 0)
    return 0;
  uint32_t now = epoch_s_();
  return (now > last_fetch_epoch_) ? (now - last_fetch_epoch_) : 0;
}

uint8_t Hv6Forecast::copy_hours(ForecastHour *out, uint8_t max, uint32_t *base_epoch_out,
                                uint32_t *age_s_out) const {
  if (out == nullptr || max == 0)
    return 0;
  if (hours_lock_ != nullptr && xSemaphoreTake(hours_lock_, pdMS_TO_TICKS(50)) != pdTRUE) {
    if (base_epoch_out) *base_epoch_out = 0;
    if (age_s_out) *age_s_out = 0;
    return 0;
  }
  const uint8_t n = hours_count_ < max ? hours_count_ : max;
  for (uint8_t i = 0; i < n; i++)
    out[i] = hours_[i];
  if (base_epoch_out) *base_epoch_out = hours_base_epoch_;
  if (age_s_out) *age_s_out = get_fetch_age_s();  // "fetched X ago", not hour offset
  if (hours_lock_ != nullptr)
    xSemaphoreGive(hours_lock_);
  return n;
}

// =============================================================================
// FreeRTOS task
// =============================================================================

void Hv6Forecast::task_func_(void *arg) { static_cast<Hv6Forecast *>(arg)->run_(); }

void Hv6Forecast::run_() {
  // Boot warmup — WiFi + SNTP must be up before TLS to Open-Meteo makes sense.
  vTaskDelay(pdMS_TO_TICKS(30000));

  uint32_t last_fetch_attempt_s = 0;
  uint32_t last_gate_log_s = 0;

  while (true) {
    hv6::ForecastConfig cfg = config_store_->get_forecast_config();
    hv6::HeliosConfig helios_cfg = config_store_->get_helios_config();

    // ---- Stability gates --------------------------------------------------
    // An enabled external Helios service owns the command slots; the local
    // producer must not fight it.
    if (helios_cfg.enabled) {
      status_ = Status::EXTERNAL;
      clear_offsets_();
      vTaskDelay(pdMS_TO_TICKS(5000));
      continue;
    }
    // Wall clock must be SNTP-synced before fetching: preload timing (now_index)
    // and the persisted fetch timestamp both need a real epoch, and TLS to
    // Open-Meteo would otherwise stamp the cache with a 1970 time. Wait it out.
    if (epoch_s_() < MIN_VALID_EPOCH) {
      const uint32_t now_s = epoch_s_();
      if (now_s - last_gate_log_s >= 60) {
        last_gate_log_s = now_s;
        ESP_LOGI(TAG, "No fetch: waiting for SNTP clock sync");
      }
      vTaskDelay(pdMS_TO_TICKS(5000));
      continue;
    }
    const bool config_ok = cfg.enabled && (cfg.latitude != 0.0f || cfg.longitude != 0.0f);
    const bool net_ok = esphome::network::is_connected();
    if (!config_ok || !net_ok) {
      if (!config_ok) {
        status_ = Status::DISABLED;
        fetch_fail_streak_ = 0;
      }
      // Explain why no Open-Meteo call is happening (throttled to once/60 s) — otherwise
      // a disabled flag, an unset location, or a down network looks identical to silence.
      const uint32_t now_s = epoch_s_();
      if (now_s - last_gate_log_s >= 60) {
        last_gate_log_s = now_s;
        ESP_LOGI(TAG, "No fetch: enabled=%s location=%.4f,%.4f network=%s",
                 cfg.enabled ? "yes" : "no", cfg.latitude, cfg.longitude,
                 net_ok ? "up" : "down");
      }
      clear_offsets_();
      vTaskDelay(pdMS_TO_TICKS(5000));
      continue;
    }

    // ---- Fetch ------------------------------------------------------------
    const uint32_t now = epoch_s_();
    const bool forced = force_fetch_;
    if (forced) force_fetch_ = false;
    // Refetch cadence is measured from the last *successful* fetch (wall clock),
    // not from the forecast-hour boundary — so it's a true "every fetch_interval_s".
    const bool need_fetch = forced || hours_count_ == 0 || last_fetch_epoch_ == 0 ||
                            get_fetch_age_s() >= cfg.fetch_interval_s;
    uint32_t backoff_s = 0;
    if (need_fetch && (forced || (now - last_fetch_attempt_s) >= 60)) {
      if (forced) ESP_LOGI(TAG, "Manual fetch triggered from dashboard");
      last_fetch_attempt_s = now;
      if (fetch_forecast_(cfg)) {
        fetch_fail_streak_ = 0;
        last_error_[0] = '\0';
        last_fetch_epoch_ = now;
        save_cache_();
      } else {
        if (fetch_fail_streak_ < 0xFFFFFFFFu) fetch_fail_streak_++;
        uint32_t shift = std::min<uint32_t>(fetch_fail_streak_, 5);
        backoff_s = std::min<uint32_t>(60u * (1u << shift), 1800);
      }
    }

    // ---- Evaluate ----------------------------------------------------------
    // Usable while the cache temporally covers "now" (now_index in range) AND a
    // successful fetch happened recently. Anchoring hours_[0] at the day start
    // means now − base grows to ~24 h, so freshness is judged by fetch age, not
    // by the hour offset.
    const uint32_t span_end = hours_base_epoch_ + static_cast<uint32_t>(hours_count_) * 3600;
    const bool covers_now = hours_count_ > 0 && now >= hours_base_epoch_ && now < span_end;
    if (covers_now && get_fetch_age_s() <= FORECAST_MAX_AGE_S) {
      status_ = Status::OK;
      recompute_preloads_(cfg);
    } else {
      status_ = (hours_count_ > 0) ? Status::STALE : Status::NO_DATA;
      clear_offsets_();
    }

    uint32_t sleep_s = (cfg.recompute_interval_s > 0) ? cfg.recompute_interval_s : 300;
    if (backoff_s > 0)
      sleep_s = std::min(sleep_s, backoff_s);
    vTaskDelay(pdMS_TO_TICKS(sleep_s * 1000));
  }
}

// =============================================================================
// Preload evaluation
// =============================================================================

void Hv6Forecast::recompute_preloads_(const hv6::ForecastConfig &cfg) {
  const uint32_t now = epoch_s_();
  if (now <= hours_base_epoch_)
    return;
  const size_t now_index = (now - hours_base_epoch_) / 3600;
  if (now_index >= hours_count_) {
    clear_offsets_();
    return;
  }

  ::hv6fc::PreloadParams params;
  params.indoor_ref_c = cfg.indoor_ref_c;
  params.load_threshold = cfg.load_threshold;
  params.gain_c_per_load = cfg.gain_c_per_load;
  params.max_offset_c = cfg.max_offset_c;

  hv6::DeviceConfig dev_cfg = config_store_->get_config();
  bool any_active = false;
  for (uint8_t i = 0; i < hv6::NUM_ZONES; i++) {
    const hv6::ZoneConfig &zc = dev_cfg.zones[i];

    ::hv6fc::ZoneExposure exposure;
    exposure.exterior_walls = zc.exterior_walls;
    exposure.wind_exposure = zc.wind_exposure;
    exposure.solar_gain = zc.solar_gain_factor;
    exposure.thermal_lead_h = zc.thermal_lead_h;

    ::hv6fc::PreloadDecision decision{};
    if (zc.enabled)
      decision = ::hv6fc::compute_zone_preload(hours_, hours_count_, now_index, exposure, params);

    hv6::HeliosZoneCommand cmd;
    cmd.setpoint_offset_c = decision.offset_c;  // zone controller clamps to [min,max]_offset_c
    cmd.preheat_floor_c = 0.0f;
    zone_controller_->apply_helios_command(i, cmd);

    if (decision.offset_c > 0.0f && zone_offset_c_[i] <= 0.0f) {
      ESP_LOGI(TAG, "Zone %u preload +%.2f°C (peak load %.2f in %dh)",
               i + 1, decision.offset_c, decision.peak_load, decision.peak_in_h);
    }
    zone_offset_c_[i] = decision.offset_c;
    zone_peak_in_h_[i] = decision.peak_in_h;
    if (decision.offset_c > 0.0f)
      any_active = true;
  }
  offsets_active_ = any_active || offsets_active_;
}

void Hv6Forecast::clear_offsets_() {
  if (!offsets_active_)
    return;
  zone_controller_->clear_all_helios_commands();
  for (uint8_t i = 0; i < hv6::NUM_ZONES; i++) {
    zone_offset_c_[i] = 0.0f;
    zone_peak_in_h_[i] = -1;
  }
  offsets_active_ = false;
  ESP_LOGI(TAG, "Preload offsets cleared");
}

// =============================================================================
// Open-Meteo fetch
// =============================================================================

bool Hv6Forecast::fetch_forecast_(const hv6::ForecastConfig &cfg) {
  char url[288];
  snprintf(url, sizeof(url),
           "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
           "&hourly=temperature_2m,wind_speed_10m,wind_direction_10m,shortwave_radiation"
           "&forecast_days=3&timeformat=unixtime&wind_speed_unit=ms&timezone=auto",
           cfg.latitude, cfg.longitude);

  ESP_LOGI(TAG, "Open-Meteo GET %s", url);

  esp_http_client_config_t http_cfg{};
  http_cfg.url = url;
  http_cfg.method = HTTP_METHOD_GET;
  http_cfg.timeout_ms = HTTP_TIMEOUT_MS;
  http_cfg.crt_bundle_attach = esp_crt_bundle_attach;
  http_cfg.buffer_size = 2048;

  // Pre-flight: don't start a TLS handshake when internal heap is already low —
  // it would starve WiFi/lwIP and stall the device. Retry on a later cycle.
  size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  if (free_internal < MIN_INTERNAL_HEAP_B) {
    snprintf(last_error_, sizeof(last_error_), "low heap %uKB", (unsigned) (free_internal / 1024));
    ESP_LOGW(TAG, "Skipping fetch: internal heap %u B < %u B", (unsigned) free_internal,
             (unsigned) MIN_INTERNAL_HEAP_B);
    return false;
  }

  esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
  if (!client) {
    snprintf(last_error_, sizeof(last_error_), "http init failed");
    return false;
  }

  // 72 h × 4 hourly arrays + unixtime stamps ≈ 8–10 KB of JSON. Allocate the
  // response body in PSRAM (free() routes back to the right heap) so the fetch's
  // internal-heap footprint is essentially just the TLS session.
  constexpr size_t BODY_CAP = 16384;
  char *body_buf = static_cast<char *>(heap_caps_malloc(BODY_CAP, MALLOC_CAP_SPIRAM));
  if (body_buf == nullptr)
    body_buf = static_cast<char *>(malloc(BODY_CAP));  // no PSRAM → internal heap
  std::unique_ptr<char[], void (*)(void *)> body(body_buf, free);
  bool ok = false;

  esp_err_t err = body ? esp_http_client_open(client, 0) : ESP_ERR_NO_MEM;
  if (err == ESP_OK) {
    esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    int len = esp_http_client_read_response(client, body.get(), BODY_CAP - 1);
    ESP_LOGI(TAG, "Open-Meteo response: http %d, %d bytes", status, len);
    if (status == 200 && len > 0) {
      body[len] = '\0';

      // Parse with an elastic PSRAM-backed document; only the hourly arrays are
      // kept (filter), so the pool stays small and never touches internal heap.
      JsonDocument filter;
      filter["hourly"]["time"] = true;
      filter["hourly"]["temperature_2m"] = true;
      filter["hourly"]["wind_speed_10m"] = true;
      filter["hourly"]["wind_direction_10m"] = true;
      filter["hourly"]["shortwave_radiation"] = true;

      PsRamAllocator psram_alloc;
      JsonDocument doc(&psram_alloc);
      DeserializationError jerr = deserializeJson(doc, body.get(), len, DeserializationOption::Filter(filter));
      if (!jerr) {
        JsonArrayConst times = doc["hourly"]["time"].as<JsonArrayConst>();
        JsonArrayConst temps = doc["hourly"]["temperature_2m"].as<JsonArrayConst>();
        JsonArrayConst winds = doc["hourly"]["wind_speed_10m"].as<JsonArrayConst>();
        JsonArrayConst dirs = doc["hourly"]["wind_direction_10m"].as<JsonArrayConst>();
        JsonArrayConst solar = doc["hourly"]["shortwave_radiation"].as<JsonArrayConst>();

        // Keep the whole day: anchor hours_[0] at the start of the response
        // (00:00 local today, since timezone=auto) so the dashboard shows the
        // entire day, not just the hours remaining. The preload scan locates
        // "now" via now_index (recompute_preloads_), so retaining the morning's
        // already-elapsed hours is purely for display.
        size_t start = 0;

        uint8_t count = 0;
        uint32_t base_epoch = 0;
        // Hold hours_lock_ across the commit so a concurrent dashboard read
        // (copy_hours) never observes a half-written array. The loop is short
        // (≤FORECAST_HOURS simple assignments), so the critical section is brief.
        if (hours_lock_ != nullptr)
          xSemaphoreTake(hours_lock_, portMAX_DELAY);
        for (size_t i = start; i < times.size() && count < FORECAST_HOURS; i++, count++) {
          if (count == 0)
            base_epoch = times[i].as<uint32_t>();
          hours_[count].temp_c = temps[i] | 0.0f;
          hours_[count].wind_speed_ms = winds[i] | 0.0f;
          hours_[count].wind_dir_deg = dirs[i] | 0.0f;
          hours_[count].shortwave_wm2 = solar[i] | 0.0f;
        }

        if (count >= 24 && base_epoch > 0) {
          hours_count_ = count;
          hours_base_epoch_ = base_epoch;
          ok = true;
          ESP_LOGI(TAG, "Forecast updated: %u hours from epoch %lu", count, (unsigned long) base_epoch);
        } else {
          snprintf(last_error_, sizeof(last_error_), "short forecast (%u h)", count);
        }
        if (hours_lock_ != nullptr)
          xSemaphoreGive(hours_lock_);
      } else {
        snprintf(last_error_, sizeof(last_error_), "json %s", jerr.c_str());
      }
    } else {
      snprintf(last_error_, sizeof(last_error_), "http %d len %d", status, len);
      ESP_LOGW(TAG, "Open-Meteo fetch failed: http %d len %d", status, len);
    }
  } else {
    snprintf(last_error_, sizeof(last_error_), "%s", esp_err_to_name(err));
    ESP_LOGW(TAG, "Open-Meteo fetch failed: %s", esp_err_to_name(err));
  }

  esp_http_client_close(client);
  esp_http_client_cleanup(client);

  // Report the all-time internal-heap low-water mark so MIN_INTERNAL_HEAP_B can be
  // tuned to the measured TLS-fetch trough (vs. the conservative compile-time floor).
  ESP_LOGI(TAG, "Fetch done (ok=%d): internal heap now %u B, all-time min %u B",
           ok ? 1 : 0, (unsigned) heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned) heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
  return ok;
}

// =============================================================================
// NVS cache (own namespace; survives reboots)
// =============================================================================

void Hv6Forecast::save_cache_() {
  nvs_handle_t handle;
  if (nvs_open(NVS_NS, NVS_READWRITE, &handle) != ESP_OK)
    return;
  CacheMeta meta{hours_base_epoch_, last_fetch_epoch_, hours_count_};
  nvs_set_blob(handle, NVS_KEY_META, &meta, sizeof(meta));
  nvs_set_blob(handle, NVS_KEY_HOURS, hours_, sizeof(ForecastHour) * hours_count_);
  nvs_commit(handle);
  nvs_close(handle);
}

void Hv6Forecast::load_cache_() {
  nvs_handle_t handle;
  if (nvs_open(NVS_NS, NVS_READONLY, &handle) != ESP_OK)
    return;
  CacheMeta meta{};
  size_t meta_len = sizeof(meta);
  if (nvs_get_blob(handle, NVS_KEY_META, &meta, &meta_len) == ESP_OK &&
      meta_len == sizeof(meta) && meta.count > 0 && meta.count <= FORECAST_HOURS) {
    size_t hours_len = sizeof(ForecastHour) * meta.count;
    if (nvs_get_blob(handle, NVS_KEY_HOURS, hours_, &hours_len) == ESP_OK) {
      hours_count_ = meta.count;
      hours_base_epoch_ = meta.base_epoch;
      last_fetch_epoch_ = meta.fetch_epoch;
      ESP_LOGI(TAG, "Restored %u cached forecast hours from NVS", hours_count_);
    }
  }
  nvs_close(handle);
}

}  // namespace forecast
}  // namespace esphome
