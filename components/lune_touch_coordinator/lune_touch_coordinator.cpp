#include "lune_touch_coordinator.h"
#include "lune_design_tokens.h"
#include "asgard_adapter.h"
#include "asgard_confirmation.h"
#include "circulation_pump.h"
#include "flow_trim.h"
#include "forecast_timeline.h"
#include "forecast_model.h"
#include "slab_charge.h"
#include "house_demand.h"
#include "room_physics.h"
#include "v6_zones_parse.h"
#include "esphome/components/network/util.h"
#include "esphome/components/wifi/wifi_component.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/core/version.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/idf_additions.h"
#include <nvs.h>
#include <nvs_flash.h>
#include <ArduinoJson.h>
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdarg>
#include <ctime>
#include <cstdio>
#include <cstring>
#include <new>
#include <fcntl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

namespace esphome {
namespace lune_touch_coordinator {

static const char *const TAG = "lune_touch";
static const char *const TOUCH_NAMESPACE = "touch";
static const char *const WEATHER_NAMESPACE = "weather";
static const char *const LEDGER_NAMESPACE = "ledger";
static const char *const SETTINGS_NAMESPACE = "touch_settings";
static const char *const IDENTITY_NAMESPACE = "touch_identity";
static const char *const TOUCH_REGISTRY_PARTITION = "touchreg";

static bool heat_source_type_supported_(const char *type) {
  return type != nullptr &&
         (std::strcmp(type, "asgard") == 0 || std::strcmp(type, "generic_http") == 0);
}

static bool heat_source_type_is_asgard_(const char *type) {
  return type != nullptr && std::strcmp(type, "asgard") == 0;
}

static void normalize_heat_source_type_(char *type, size_t capacity) {
  if (type == nullptr || capacity == 0)
    return;
  if (!heat_source_type_supported_(type)) {
    std::strncpy(type, "asgard", capacity - 1);
    type[capacity - 1] = '\0';
  }
}

static asgard_adapter::Config make_asgard_config_(const HeatSourceState &source) {
  asgard_adapter::Config config{};
  config.host = source.host;
  config.port = source.port;
  config.physical_temperature_entity = source.weighted_temperature_variable;
  config.write_url_template = source.write_url_template;
  config.read_url_template = source.read_url_template;
  config.climate_entity = source.climate_entity;
  config.bias_entity = source.bias_entity;
  config.dhw_entity = source.dhw_entity;
  config.legionella_entity = source.legionella_entity;
  config.defrost_entity = source.defrost_entity;
  return config;
}

namespace {

static constexpr uint32_t OTA_SLOT_BYTES = 0x640000;
static constexpr uint32_t FORECAST_CACHE_MAGIC = 0x4C544657;  // LTFW
static constexpr uint32_t PRELOAD_LEARN_MAGIC = 0x4C54504C;  // LTPL
static constexpr uint16_t PRELOAD_LEARN_VERSION = 1;
static constexpr float LOAD_THRESHOLD = 1.0f;
static constexpr float GAIN_C_PER_LOAD = 0.5f;
static constexpr float COMFORT_BAND_C = 0.5f;
static constexpr uint16_t FORECAST_CACHE_VERSION =
    lune_touch_forecast_timeline::PERSISTED_FORECAST_CACHE_VERSION;
static constexpr size_t REGISTRY_CHUNK_SIZE = 3072;
static constexpr size_t REGISTRY_CHUNK_COUNT =
    (sizeof(::lune_touch::PersistedState) + REGISTRY_CHUNK_SIZE - 1) / REGISTRY_CHUNK_SIZE;
static constexpr const char *REGISTRY_CHUNK_KEYS[] = {
    "registry_a", "registry_b", "registry_c", "registry_d",
    "registry_e", "registry_f", "registry_g", "registry_h",
};
static_assert(REGISTRY_CHUNK_COUNT <= sizeof(REGISTRY_CHUNK_KEYS) / sizeof(REGISTRY_CHUNK_KEYS[0]),
              "Touch registry chunk key list is too short");

// Command records are expired on reboot by design.  Keep only a short recent
// history in NVS so the human-readable ledger does not consume the space
// needed by the coordinator registry.
static constexpr size_t PERSISTED_LEDGER_RECORDS = 4;
static bool touch_registry_partition_ready_ = false;
static bool dedicated_touch_registry_valid_ = false;

// LVGL's built-in Montserrat fonts include the Font Awesome symbols below,
// but not arbitrary Unicode arrows, geometric circles, or middle dots. Keep
// these UTF-8 sequences independent of lvgl.h so the coordinator model remains
// buildable in the host-side tests.
static constexpr const char DISPLAY_ICON_OK[] = "\xEF\x80\x8C";       // U+F00C
static constexpr const char DISPLAY_ICON_CLOSE[] = "\xEF\x80\x8D";    // U+F00D
static constexpr const char DISPLAY_ICON_RIGHT[] = "\xEF\x81\x94";    // U+F054
static constexpr const char DISPLAY_ICON_WARNING[] = "\xEF\x81\xB1";  // U+F071
static constexpr const char DISPLAY_ICON_UP[] = "\xEF\x81\xB7";       // U+F077
static constexpr const char DISPLAY_ICON_DOWN[] = "\xEF\x81\xB8";     // U+F078
static constexpr const char DISPLAY_ICON_WIND[] = "\xEF\x81\xB4";     // U+F074
static constexpr const char DISPLAY_ICON_BARS[] = "\xEF\x83\x89";     // U+F0C9

bool display_idle_timeout_allowed_(uint32_t timeout_s) {
  return timeout_s == 0 || timeout_s == 60 || timeout_s == 120 || timeout_s == 300;
}

uint32_t display_hash_mix_(uint32_t hash, uint32_t value) {
  hash ^= value;
  hash *= 16777619UL;
  return hash;
}

uint32_t display_hash_text_(uint32_t hash, const char *value) {
  if (value == nullptr)
    return display_hash_mix_(hash, 0);
  while (*value != '\0')
    hash = display_hash_mix_(hash, static_cast<uint8_t>(*value++));
  return hash;
}

uint32_t display_hash_float_(uint32_t hash, float value) {
  if (!std::isfinite(value))
    return display_hash_mix_(hash, 0x7FC00000UL);
  return display_hash_mix_(hash, static_cast<uint32_t>(std::lround(value * 10.0f)));
}

void append_form_component_(std::string &out, const char *value) {
  static constexpr char HEX[] = "0123456789ABCDEF";
  if (value == nullptr)
    return;
  for (const unsigned char *p = reinterpret_cast<const unsigned char *>(value); *p != '\0'; ++p) {
    const unsigned char c = *p;
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
      out.push_back(static_cast<char>(c));
    } else {
      out.push_back('%');
      out.push_back(HEX[(c >> 4) & 0x0F]);
      out.push_back(HEX[c & 0x0F]);
    }
  }
}

bool json_object_to_form_(const char *payload, std::string &form) {
  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, payload != nullptr ? payload : "{}");
  if (error || !doc.is<JsonObject>())
    return false;

  form.clear();
  for (JsonPairConst pair : doc.as<JsonObjectConst>()) {
    if (!form.empty())
      form.push_back('&');
    append_form_component_(form, pair.key().c_str());
    form.push_back('=');

    JsonVariantConst value = pair.value();
    if (value.is<JsonObject>() || value.is<JsonArray>())
      return false;
    if (value.is<const char *>()) {
      append_form_component_(form, value.as<const char *>());
    } else {
      std::string scalar;
      serializeJson(value, scalar);
      append_form_component_(form, scalar.c_str());
    }
  }
  return true;
}

void init_touch_registry_partition_() {
  const esp_err_t err = nvs_flash_init_partition(TOUCH_REGISTRY_PARTITION);
  if (err == ESP_OK) {
    touch_registry_partition_ready_ = true;
    ESP_LOGI(TAG, "Using dedicated Touch registry NVS partition");
    return;
  }
  // Older partition tables do not contain touchreg yet. Keep the compatibility
  // path operational until the one-time USB partition-table install is done.
  touch_registry_partition_ready_ = false;
  ESP_LOGW(TAG, "Dedicated Touch registry partition unavailable (%s); using legacy NVS",
           esp_err_to_name(err));
}

esp_err_t open_touch_registry_nvs_(nvs_open_mode_t mode, nvs_handle_t *handle) {
  if (touch_registry_partition_ready_)
    return nvs_open_from_partition(TOUCH_REGISTRY_PARTITION, TOUCH_NAMESPACE, mode, handle);
  return nvs_open(TOUCH_NAMESPACE, mode, handle);
}
struct PersistedLedgerCompact {
  uint32_t magic{::lune_touch::PERSISTED_LEDGER_MAGIC};
  uint16_t version{1};
  uint16_t reserved{0};
  uint32_t boot_id{0};
  uint32_t count{0};
  ::lune_touch::CommandRecord records[PERSISTED_LEDGER_RECORDS]{};
};

template <typename State>
bool load_registry_chunks_for_(nvs_handle_t handle, State *state) {
  if (state == nullptr)
    return false;
  size_t lengths[sizeof(REGISTRY_CHUNK_KEYS) / sizeof(REGISTRY_CHUNK_KEYS[0])]{};
  size_t total = 0;
  size_t chunk_count = 0;
  while (total < sizeof(*state) && chunk_count < sizeof(REGISTRY_CHUNK_KEYS) / sizeof(REGISTRY_CHUNK_KEYS[0])) {
    const size_t i = chunk_count++;
    lengths[i] = 0;
    if (nvs_get_blob(handle, REGISTRY_CHUNK_KEYS[i], nullptr, &lengths[i]) != ESP_OK ||
        lengths[i] == 0 || lengths[i] > REGISTRY_CHUNK_SIZE ||
        total + lengths[i] > sizeof(*state))
      return false;
    total += lengths[i];
  }
  if (total != sizeof(*state) || chunk_count == 0)
    return false;

  uint8_t *destination = reinterpret_cast<uint8_t *>(state);
  size_t offset = 0;
  for (size_t i = 0; i < chunk_count; i++) {
    size_t length = lengths[i];
    if (nvs_get_blob(handle, REGISTRY_CHUNK_KEYS[i], destination + offset, &length) != ESP_OK ||
        length != lengths[i])
      return false;
    offset += length;
  }
  return true;
}

bool load_registry_chunks_(nvs_handle_t handle, ::lune_touch::PersistedState *state) {
  return load_registry_chunks_for_(handle, state);
}

void cleanup_legacy_touch_registry_() {
  if (!touch_registry_partition_ready_ || !dedicated_touch_registry_valid_)
    return;

  nvs_handle_t legacy_handle;
  const esp_err_t open_err = nvs_open(TOUCH_NAMESPACE, NVS_READWRITE, &legacy_handle);
  if (open_err == ESP_ERR_NVS_NOT_FOUND)
    return;
  if (open_err != ESP_OK) {
    ESP_LOGW(TAG, "Could not open legacy Touch registry for cleanup: %s",
             esp_err_to_name(open_err));
    return;
  }
  const esp_err_t erase_err = nvs_erase_all(legacy_handle);
  const esp_err_t commit_err = erase_err == ESP_OK ? nvs_commit(legacy_handle) : erase_err;
  nvs_close(legacy_handle);
  if (commit_err == ESP_OK)
    ESP_LOGI(TAG, "Removed migrated Touch registry from default NVS");
  else if (commit_err != ESP_ERR_NVS_NOT_FOUND)
    ESP_LOGW(TAG, "Could not remove legacy Touch registry: %s", esp_err_to_name(commit_err));
}

struct PersistedForecastCache {
  uint32_t magic{FORECAST_CACHE_MAGIC};
  uint16_t version{FORECAST_CACHE_VERSION};
  uint16_t reserved{0};
  uint8_t hours_count{0};
  uint8_t reserved2[3]{};
  int64_t fetch_epoch_s{0};
  char provider_timezone[48]{};
  float min_temp_c{0.0f};
  float max_wind_ms{0.0f};
  float peak_wind_dir_deg{0.0f};
  float max_solar_wm2{0.0f};
  ForecastHourState hours[72]{};
};

const char *ota_state_name_(esp_ota_img_states_t state) {
  switch (state) {
    case ESP_OTA_IMG_NEW:
      return "new";
    case ESP_OTA_IMG_PENDING_VERIFY:
      return "pending_verify";
    case ESP_OTA_IMG_VALID:
      return "valid";
    case ESP_OTA_IMG_INVALID:
      return "invalid";
    case ESP_OTA_IMG_ABORTED:
      return "aborted";
    case ESP_OTA_IMG_UNDEFINED:
    default:
      return "undefined";
  }
}

bool appendf_(char *buffer, size_t capacity, size_t &offset, const char *fmt, ...) {
  if (buffer == nullptr || offset >= capacity)
    return false;
  va_list args;
  va_start(args, fmt);
  const int written = vsnprintf(buffer + offset, capacity - offset, fmt, args);
  va_end(args);
  if (written < 0)
    return false;
  if (static_cast<size_t>(written) >= capacity - offset) {
    offset = capacity;
    return false;
  }
  offset += static_cast<size_t>(written);
  return true;
}

bool entity_number_(JsonDocument &doc, const char *key, float *out) {
  if (out == nullptr || key == nullptr)
    return false;
  JsonVariant value = doc[key]["value"];
  if (value.isNull())
    value = doc[key];
  if (value.isNull())
    return false;
  *out = value | 0.0f;
  return true;
}

const char *entity_state_(JsonDocument &doc, const char *key) {
  if (key == nullptr)
    return nullptr;
  const char *state = doc[key]["state"].as<const char *>();
  if (state == nullptr)
    state = doc[key]["value"].as<const char *>();
  return state;
}

bool state_is_on_(const char *state) {
  return state == nullptr || std::strcmp(state, "on") == 0 || std::strcmp(state, "ON") == 0 ||
         std::strcmp(state, "true") == 0 || std::strcmp(state, "1") == 0;
}

void nullable_float_(char *out, size_t out_len, bool has_value, float value) {
  if (out == nullptr || out_len == 0)
    return;
  if (has_value && std::isfinite(value))
    snprintf(out, out_len, "%.1f", value);
  else
    std::strncpy(out, "null", out_len - 1);
  out[out_len - 1] = '\0';
}

void json_float_token_(char *out, size_t out_len, float value, unsigned decimals) {
  if (out == nullptr || out_len == 0)
    return;
  if (!std::isfinite(value)) {
    std::strncpy(out, "null", out_len - 1);
    out[out_len - 1] = '\0';
    return;
  }
  const char *format = decimals >= 6 ? "%.6f" : decimals == 3 ? "%.3f" :
                       decimals == 2 ? "%.2f" : decimals == 0 ? "%.0f" : "%.1f";
  snprintf(out, out_len, format, value);
  out[out_len - 1] = '\0';
}

// HTTP/TLS parsers expect ordinary byte-addressable memory. Keep their response
// buffers in the internal heap whenever possible; PSRAM remains a fallback for
// the larger legacy V6 snapshot and forecast responses.
char *alloc_http_body_(size_t capacity) {
  if (capacity == 0)
    return nullptr;
  char *body = static_cast<char *>(heap_caps_malloc(capacity, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (body == nullptr)
    body = static_cast<char *>(heap_caps_malloc(capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  return body;
}

// Large per-call scratch buffers: neither on a task stack (overflow) nor in
// .bss (internal RAM that TLS handshakes need). Allocated once in PSRAM.
template <typename T> T *psram_scratch_(size_t count) {
  void *p = heap_caps_calloc(count, sizeof(T), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (p == nullptr)
    p = heap_caps_calloc(count, sizeof(T), MALLOC_CAP_8BIT);
  return static_cast<T *>(p);
}

// Plain-HTTP bodies for the Odin / Asgard integrations (up to 16 KiB, several
// tasks at once): PSRAM first, so internal RAM stays free for TLS handshakes
// (forecast over HTTPS). Byte-addressable, no DMA involved.
char *alloc_psram_body_(size_t capacity) {
  if (capacity == 0)
    return nullptr;
  char *body = static_cast<char *>(heap_caps_malloc(capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (body == nullptr)
    body = static_cast<char *>(heap_caps_malloc(capacity, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  return body;
}

// Large registry blobs (~16 KiB) must not live in .dram0.bss — prefer PSRAM.
::lune_touch::PersistedState *alloc_persisted_state_() {
  void *p = heap_caps_malloc(sizeof(::lune_touch::PersistedState),
                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (p == nullptr)
    p = heap_caps_malloc(sizeof(::lune_touch::PersistedState),
                         MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  return static_cast<::lune_touch::PersistedState *>(p);
}

void format_ipv4_(uint32_t host_order, char *out, size_t out_len) {
  if (out == nullptr || out_len == 0)
    return;
  snprintf(out, out_len, "%u.%u.%u.%u",
           static_cast<unsigned>((host_order >> 24) & 0xffu),
           static_cast<unsigned>((host_order >> 16) & 0xffu),
           static_cast<unsigned>((host_order >> 8) & 0xffu),
           static_cast<unsigned>(host_order & 0xffu));
  out[out_len - 1] = '\0';
}

// Canonical V6 pairing id is "hv6-" + lowercase hex of the MAC digits.
// Older Touch builds (and the legacy /state ingest) stored the raw MAC with
// colons; treat that as the same identity and rewrite to canonical form.
// Rebranded V6 firmware reports "lv6-<hex>"; it is the same identity, so the
// prefix is dropped before the hex digits are read (stored ids stay "hv6-").
bool is_ipv4_text_(const char *text) {
  if (text == nullptr || text[0] == '\0')
    return false;
  int parts = 0;
  const char *p = text;
  while (true) {
    if (!std::isdigit(static_cast<unsigned char>(*p)))
      return false;
    int value = 0;
    int digits = 0;
    while (std::isdigit(static_cast<unsigned char>(*p))) {
      value = value * 10 + (*p - '0');
      if (++digits > 3 || value > 255)
        return false;
      p++;
    }
    parts++;
    if (*p == '\0')
      return parts == 4;
    if (*p != '.' || parts == 4)
      return false;
    p++;
  }
}

bool is_hostname_text_(const char *text) {
  if (text == nullptr || text[0] == '\0' || std::strlen(text) > 63)
    return false;
  for (const char *p = text; *p != '\0'; p++) {
    const unsigned char c = static_cast<unsigned char>(*p);
    if (!std::isalnum(c) && c != '-' && c != '.')
      return false;
  }
  return true;
}

void canonicalize_pairing_fingerprint_(const char *raw, char *out, size_t out_len) {
  if (out == nullptr || out_len == 0)
    return;
  out[0] = '\0';
  if (raw == nullptr || raw[0] == '\0' || out_len < 5)
    return;
  if (std::strncmp(raw, "hv6-", 4) == 0) {
    std::strncpy(out, raw, out_len - 1);
    out[out_len - 1] = '\0';
    return;
  }
  if (std::strncmp(raw, "lv6-", 4) == 0)
    raw += 4;
  size_t off = 0;
  off += static_cast<size_t>(snprintf(out + off, out_len - off, "hv6-"));
  for (const char *p = raw; *p != '\0' && off + 1 < out_len; ++p) {
    char c = *p;
    if (c >= 'A' && c <= 'F')
      c = static_cast<char>(c - 'A' + 'a');
    if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))
      out[off++] = c;
  }
  out[off] = '\0';
  if (std::strcmp(out, "hv6-") == 0)
    out[0] = '\0';
}

bool pairing_fingerprints_match_(const char *a, const char *b) {
  if (a == nullptr || b == nullptr || a[0] == '\0' || b[0] == '\0')
    return false;
  if (std::strcmp(a, b) == 0)
    return true;
  char ca[24]{};
  char cb[24]{};
  canonicalize_pairing_fingerprint_(a, ca, sizeof(ca));
  canonicalize_pairing_fingerprint_(b, cb, sizeof(cb));
  return ca[0] != '\0' && std::strcmp(ca, cb) == 0;
}

bool sta_ipv4_info_(uint32_t *ip_host, uint32_t *mask_host) {
  if (ip_host == nullptr || mask_host == nullptr)
    return false;
  esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  if (netif == nullptr)
    return false;
  esp_netif_ip_info_t info{};
  if (esp_netif_get_ip_info(netif, &info) != ESP_OK || info.ip.addr == 0)
    return false;
  *ip_host = ntohl(info.ip.addr);
  *mask_host = ntohl(info.netmask.addr);
  return *mask_host != 0;
}

// Non-blocking TCP/80 handshake for a small IP batch. Closed ports fail fast
// so a /24 sweep stays inside the dashboard scan wait window.
void probe_tcp80_batch_(const uint32_t *ips, size_t count, uint32_t timeout_ms, bool *open_out) {
  static constexpr size_t kMaxBatch = 8;
  if (ips == nullptr || open_out == nullptr || count == 0)
    return;
  if (count > kMaxBatch)
    count = kMaxBatch;
  int fds[kMaxBatch];
  for (size_t i = 0; i < count; i++) {
    open_out[i] = false;
    fds[i] = -1;
    const int fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (fd < 0)
      continue;
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
      ::close(fd);
      continue;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(80);
    addr.sin_addr.s_addr = htonl(ips[i]);
    const int rc = ::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
    if (rc == 0) {
      open_out[i] = true;
      ::close(fd);
      continue;
    }
    if (errno != EINPROGRESS) {
      ::close(fd);
      continue;
    }
    fds[i] = fd;
  }
  // Keep waiting until every socket has settled or the deadline passes: one
  // select() returns as soon as any host answers (a refusal counts), which
  // used to abandon the rest of the batch before they connected.
  bool pending[kMaxBatch]{};
  for (size_t i = 0; i < count; i++)
    pending[i] = fds[i] >= 0;
  const uint32_t start = esphome::millis();
  while (true) {
    fd_set wait;
    FD_ZERO(&wait);
    int max_fd = -1;
    for (size_t i = 0; i < count; i++) {
      if (!pending[i])
        continue;
      FD_SET(fds[i], &wait);
      if (fds[i] > max_fd)
        max_fd = fds[i];
    }
    const uint32_t elapsed = esphome::millis() - start;
    if (max_fd < 0 || elapsed >= timeout_ms)
      break;
    const uint32_t left = timeout_ms - elapsed;
    timeval tv{};
    tv.tv_sec = static_cast<time_t>(left / 1000);
    tv.tv_usec = static_cast<suseconds_t>((left % 1000) * 1000);
    if (::select(max_fd + 1, nullptr, &wait, nullptr, &tv) <= 0)
      break;
    for (size_t i = 0; i < count; i++) {
      if (!pending[i] || !FD_ISSET(fds[i], &wait))
        continue;
      pending[i] = false;
      int err = 0;
      socklen_t err_len = sizeof(err);
      if (::getsockopt(fds[i], SOL_SOCKET, SO_ERROR, &err, &err_len) == 0 && err == 0)
        open_out[i] = true;
    }
  }
  for (size_t i = 0; i < count; i++) {
    if (fds[i] >= 0)
      ::close(fds[i]);
  }
}

bool current_schedule_time_(esphome::time::RealTimeClock *time, uint8_t *day_index,
                            uint16_t *minute_of_day) {
  if (time == nullptr)
    return false;
  const auto now = time->now();
  if (!now.is_valid())
    return false;
  if (day_index != nullptr)
    *day_index = now.day_of_week == 1 ? 6 : static_cast<uint8_t>(now.day_of_week - 2);
  if (minute_of_day != nullptr)
    *minute_of_day = static_cast<uint16_t>(now.hour) * 60 + now.minute;
  return true;
}

int64_t current_epoch_s_() {
  const int64_t epoch_s = static_cast<int64_t>(::time(nullptr));
  return epoch_s >= lune_touch_forecast_timeline::MIN_VALID_EPOCH_S ? epoch_s : 0;
}

size_t forecast_display_start_(const ForecastHourState *hours, uint8_t count) {
  if (hours == nullptr || count == 0)
    return lune_touch_forecast_timeline::NO_INDEX;
  int64_t timestamps[72]{};
  for (uint8_t i = 0; i < count && i < 72; i++)
    timestamps[i] = hours[i].timestamp_s;
  return lune_touch_forecast_timeline::first_index_at_or_after(timestamps, count, current_epoch_s_());
}

bool forecast_display_hour_(const ForecastHourState *hours, uint8_t count, uint8_t index,
                            ForecastHourState *out) {
  const size_t start = forecast_display_start_(hours, count);
  if (out == nullptr || start == lune_touch_forecast_timeline::NO_INDEX)
    return false;
  const size_t source = start + index;
  if (source >= count)
    return false;
  *out = hours[source];
  return std::isfinite(out->temp_c);
}

std::string forecast_hour_clock_label_(int64_t timestamp_s, esphome::time::RealTimeClock *clock) {
  char buffer[8] = "--:--";
  if (timestamp_s <= 0)
    return buffer;
  if (clock != nullptr) {
    const auto now = clock->now();
    if (now.is_valid()) {
      const int64_t delta_h = (timestamp_s - static_cast<int64_t>(now.timestamp)) / 3600;
      int hour = static_cast<int>(now.hour) + static_cast<int>(delta_h);
      hour %= 24;
      if (hour < 0)
        hour += 24;
      std::snprintf(buffer, sizeof(buffer), "%02d:00", hour);
      return buffer;
    }
  }
  const time_t raw = static_cast<time_t>(timestamp_s);
  struct tm local {};
  if (localtime_r(&raw, &local) != nullptr)
    std::snprintf(buffer, sizeof(buffer), "%02d:00", local.tm_hour);
  return buffer;
}

void stamp_command_timing_(::lune_touch::CommandRecord *record, uint32_t now_ms, uint32_t ttl_s) {
  if (record == nullptr)
    return;
  record->created_at_ms = now_ms;
  record->expires_at_ms = now_ms + ttl_s * 1000UL;
  const int64_t now_epoch_s = current_epoch_s_();
  if (now_epoch_s > 0) {
    record->created_at_epoch_s = now_epoch_s;
    record->expires_at_epoch_s = now_epoch_s + static_cast<int64_t>(ttl_s);
  }
}

void stamp_command_target_(::lune_touch::CommandRecord *record,
                           const ::lune_touch::PairedNode *node,
                           const ::lune_touch::ZoneBinding *binding) {
  if (record == nullptr || node == nullptr || binding == nullptr)
    return;
  std::strncpy(record->room_id, binding->room_id, sizeof(record->room_id) - 1);
  std::strncpy(record->node_id, node->node_id, sizeof(record->node_id) - 1);
  std::strncpy(record->loop_id, binding->loop_id, sizeof(record->loop_id) - 1);
}

const char *cache_validation_error_(lune_touch_forecast_timeline::CacheValidation validation) {
  switch (validation) {
    case lune_touch_forecast_timeline::CacheValidation::EXPIRED:
      return "cache_expired";
    case lune_touch_forecast_timeline::CacheValidation::UNALIGNABLE:
      return "cache_unalignable";
    case lune_touch_forecast_timeline::CacheValidation::FRESH:
    default:
      return "restored_cache";
  }
}

void legacy_zone_status_(const char *raw, bool enabled, bool has_temp, float temp,
                         bool has_setpoint, float setpoint, char *out, size_t out_len) {
  if (out == nullptr || out_len == 0)
    return;
  const char *status = "idle";
  if (!enabled) {
    status = "unused";
  } else if (raw != nullptr && raw[0] != '\0') {
    if (std::strcmp(raw, "HEATING") == 0 || std::strcmp(raw, "heat") == 0 ||
        std::strcmp(raw, "CALL") == 0 || std::strcmp(raw, "call") == 0) {
      status = "heat";
    } else if (std::strcmp(raw, "PREHEAT") == 0 || std::strcmp(raw, "preheat") == 0) {
      status = "preheat";
    } else if (std::strcmp(raw, "HOLD") == 0 || std::strcmp(raw, "hold") == 0) {
      status = "hold";
    } else if (std::strcmp(raw, "STALE") == 0 || std::strcmp(raw, "stale") == 0) {
      status = "stale";
    }
  } else if (has_temp && has_setpoint && temp < setpoint - 0.2f) {
    status = "call";
  }
  std::strncpy(out, status, out_len - 1);
  out[out_len - 1] = '\0';
}

struct ZoneBindingV3 {
  char room_id[32]{};
  char room_name[48]{};
  uint8_t node_index{0};
  uint8_t zone_index{0};
  uint8_t exterior_walls{0};
  float wind_exposure{0.5f};
  float solar_gain{0.3f};
  uint8_t thermal_lead_h{4};
  float max_offset_c{1.5f};
  float comfort_setpoint_c{21.0f};
  uint8_t priority{1};
  bool enabled{false};
};

struct PairedNodeV5 {
  char node_id[24]{};
  char hostname[64]{};
  char fallback_ip[16]{};
  char model[24]{};
  char firmware[24]{};
  ::lune_touch::NodeTrust trust{::lune_touch::NodeTrust::UNPAIRED};
  uint32_t last_seen_ms{0};
  bool reachable{false};
};

struct PairedNodeV8 {
  char node_id[24]{};
  char hostname[64]{};
  char fallback_ip[16]{};
  char model[24]{};
  char firmware[24]{};
  char pairing_fingerprint[24]{};
  ::lune_touch::NodeTrust trust{::lune_touch::NodeTrust::UNPAIRED};
  uint32_t last_seen_ms{0};
  bool reachable{false};
};

struct PersistedStateV3 {
  uint32_t magic{::lune_touch::PERSISTED_STATE_MAGIC};
  uint16_t version{::lune_touch::PERSISTED_STATE_VERSION_V3};
  uint16_t reserved{0};
  uint32_t node_count{0};
  uint32_t zone_count{0};
  PairedNodeV5 nodes[::lune_touch::MAX_NODES]{};
  ZoneBindingV3 zones[::lune_touch::MAX_HOUSE_ZONES]{};
};

struct ZoneBindingV4 {
  char room_id[32]{};
  char room_name[48]{};
  uint8_t node_index{0};
  uint8_t zone_index{0};
  uint8_t exterior_walls{0};
  float wind_exposure{0.5f};
  float solar_gain{0.3f};
  uint8_t thermal_lead_h{4};
  float max_offset_c{1.5f};
  float comfort_setpoint_c{21.0f};
  float comfort_bias_c{0.0f};
  uint8_t priority{1};
  bool enabled{false};
};

struct ZoneBindingV7 {
  char room_id[32]{};
  char room_name[48]{};
  uint8_t node_index{0};
  uint8_t zone_index{0};
  uint8_t exterior_walls{0};
  float wind_exposure{0.5f};
  float solar_gain{0.3f};
  uint8_t thermal_lead_h{4};
  float max_offset_c{1.5f};
  float comfort_setpoint_c{21.0f};
  float comfort_bias_c{0.0f};
  float schedule_setpoint_c{21.0f};
  uint16_t schedule_start_min{360};
  uint16_t schedule_end_min{1320};
  uint8_t schedule_day_mask{0x7F};
  uint8_t priority{1};
  bool enabled{false};
  bool schedule_enabled{false};
};

struct ZoneBindingV8 {
  char room_id[32]{};
  char room_name[48]{};
  uint8_t node_index{0};
  uint8_t zone_index{0};
  uint8_t exterior_walls{0};
  float wind_exposure{0.5f};
  float solar_gain{0.3f};
  uint8_t thermal_lead_h{4};
  float max_offset_c{1.5f};
  float comfort_setpoint_c{21.0f};
  float comfort_bias_c{0.0f};
  float schedule_setpoint_c{21.0f};
  uint16_t schedule_start_min{360};
  uint16_t schedule_end_min{1320};
  uint8_t schedule_day_mask{0x7F};
  uint8_t priority{1};
  uint16_t thermal_samples{0};
  float learned_heat_gain_c_per_h{0.0f};
  float learned_cool_loss_c_per_h{0.0f};
  bool enabled{false};
  bool schedule_enabled{false};
};

struct PersistedStateV4 {
  uint32_t magic{::lune_touch::PERSISTED_STATE_MAGIC};
  uint16_t version{::lune_touch::PERSISTED_STATE_VERSION_V4};
  uint16_t reserved{0};
  uint32_t node_count{0};
  uint32_t zone_count{0};
  PairedNodeV5 nodes[::lune_touch::MAX_NODES]{};
  ZoneBindingV4 zones[::lune_touch::MAX_HOUSE_ZONES]{};
};

struct PersistedStateV5 {
  uint32_t magic{::lune_touch::PERSISTED_STATE_MAGIC};
  uint16_t version{::lune_touch::PERSISTED_STATE_VERSION_V5};
  uint16_t reserved{0};
  uint32_t node_count{0};
  uint32_t zone_count{0};
  PairedNodeV5 nodes[::lune_touch::MAX_NODES]{};
  ZoneBindingV7 zones[::lune_touch::MAX_HOUSE_ZONES]{};
};

struct PersistedStateV6 {
  uint32_t magic{::lune_touch::PERSISTED_STATE_MAGIC};
  uint16_t version{::lune_touch::PERSISTED_STATE_VERSION_V6};
  uint16_t reserved{0};
  uint32_t node_count{0};
  uint32_t zone_count{0};
  PairedNodeV8 nodes[::lune_touch::MAX_NODES]{};
  ZoneBindingV7 zones[::lune_touch::MAX_HOUSE_ZONES]{};
};

struct PersistedStateV7 {
  uint32_t magic{::lune_touch::PERSISTED_STATE_MAGIC};
  uint16_t version{::lune_touch::PERSISTED_STATE_VERSION_V7};
  uint16_t reserved{0};
  uint32_t node_count{0};
  uint32_t zone_count{0};
  PairedNodeV8 nodes[::lune_touch::MAX_NODES]{};
  ZoneBindingV7 zones[::lune_touch::MAX_HOUSE_ZONES]{};
  ::lune_touch::ZoneHistory histories[::lune_touch::MAX_HOUSE_ZONES]{};
};

struct PersistedStateV8 {
  uint32_t magic{::lune_touch::PERSISTED_STATE_MAGIC};
  uint16_t version{::lune_touch::PERSISTED_STATE_VERSION_V8};
  uint16_t reserved{0};
  uint32_t node_count{0};
  uint32_t zone_count{0};
  PairedNodeV8 nodes[::lune_touch::MAX_NODES]{};
  ZoneBindingV8 zones[::lune_touch::MAX_HOUSE_ZONES]{};
  ::lune_touch::ZoneHistory histories[::lune_touch::MAX_HOUSE_ZONES]{};
};

struct ZoneBindingV9 {
  char room_id[32]{}; char room_name[48]{}; uint8_t node_index{0}; uint8_t zone_index{0};
  uint8_t exterior_walls{0}; float wind_exposure{0.5f}; float solar_gain{0.3f};
  uint8_t thermal_lead_h{4}; float max_offset_c{1.5f}; float comfort_setpoint_c{21.0f};
  float comfort_bias_c{0.0f}; float schedule_setpoint_c{21.0f}; uint16_t schedule_start_min{360};
  uint16_t schedule_end_min{1320}; uint8_t schedule_day_mask{0x7F}; uint8_t priority{1};
  ::lune_touch::ZoneNameSource name_source{::lune_touch::ZoneNameSource::GENERATED};
  uint16_t thermal_samples{0}; float learned_heat_gain_c_per_h{0.0f};
  float learned_cool_loss_c_per_h{0.0f}; bool enabled{false}; bool schedule_enabled{false};
};
struct PersistedStateV9 {
  uint32_t magic{::lune_touch::PERSISTED_STATE_MAGIC}; uint16_t version{::lune_touch::PERSISTED_STATE_VERSION_V9};
  uint16_t reserved{0}; uint32_t node_count{0}; uint32_t zone_count{0};
  ::lune_touch::PairedNode nodes[::lune_touch::MAX_NODES]{};
  ZoneBindingV9 zones[::lune_touch::MAX_HOUSE_ZONES]{};
  ::lune_touch::ZoneHistory histories[::lune_touch::MAX_HOUSE_ZONES]{};
};

struct ZoneBindingV10 {
  char room_id[32]{}; char room_name[48]{}; char loop_id[48]{};
  uint8_t node_index{0}; uint8_t zone_index{0}; uint8_t exterior_walls{0};
  float wind_exposure{0.5f}; float solar_gain{0.3f}; uint8_t thermal_lead_h{4};
  float max_offset_c{1.5f}; float comfort_setpoint_c{21.0f}; float comfort_bias_c{0.0f};
  float schedule_setpoint_c{21.0f}; uint16_t schedule_start_min{360};
  uint16_t schedule_end_min{1320}; uint8_t schedule_day_mask{0x7F}; uint8_t priority{1};
  ::lune_touch::ZoneNameSource name_source{::lune_touch::ZoneNameSource::GENERATED};
  uint16_t thermal_samples{0}; float learned_heat_gain_c_per_h{0.0f};
  float learned_cool_loss_c_per_h{0.0f}; bool enabled{false}; bool schedule_enabled{false};
};
struct PersistedStateV10 {
  uint32_t magic{::lune_touch::PERSISTED_STATE_MAGIC};
  uint16_t version{::lune_touch::PERSISTED_STATE_VERSION_V10}; uint16_t reserved{0};
  uint32_t node_count{0}; uint32_t zone_count{0};
  ::lune_touch::PairedNode nodes[::lune_touch::MAX_NODES]{};
  ZoneBindingV10 zones[::lune_touch::MAX_HOUSE_ZONES]{};
  ::lune_touch::ZoneHistory histories[::lune_touch::MAX_HOUSE_ZONES]{};
};

// v13 ZoneBinding lacked unassigned / is_group_secondary (appended in v14).
struct ZoneBindingV13 {
  char room_id[32]{};
  char room_name[48]{};
  char loop_id[48]{};
  char node_id[24]{};
  uint8_t node_index{0};
  uint8_t zone_index{0};
  float served_area_m2{0.0f};
  bool commissioned{true};
  uint8_t exterior_walls{0};
  float wind_exposure{0.5f};
  float solar_gain{0.3f};
  uint8_t thermal_lead_h{4};
  float max_offset_c{1.5f};
  float comfort_setpoint_c{21.0f};
  float comfort_bias_c{0.0f};
  float schedule_setpoint_c{21.0f};
  uint16_t schedule_start_min{360};
  uint16_t schedule_end_min{1320};
  uint8_t schedule_day_mask{0x7F};
  uint8_t priority{1};
  ::lune_touch::ZoneNameSource name_source{::lune_touch::ZoneNameSource::GENERATED};
  uint16_t thermal_samples{0};
  float learned_heat_gain_c_per_h{0.0f};
  float learned_cool_loss_c_per_h{0.0f};
  bool enabled{false};
  bool schedule_enabled{false};
};

// v14 ZoneBinding = v13 + unassigned flags (pre-v15 physics mirror).
struct ZoneBindingV14 {
  char room_id[32]{};
  char room_name[48]{};
  char loop_id[48]{};
  char node_id[24]{};
  uint8_t node_index{0};
  uint8_t zone_index{0};
  float served_area_m2{0.0f};
  bool commissioned{true};
  uint8_t exterior_walls{0};
  float wind_exposure{0.5f};
  float solar_gain{0.3f};
  uint8_t thermal_lead_h{4};
  float max_offset_c{1.5f};
  float comfort_setpoint_c{21.0f};
  float comfort_bias_c{0.0f};
  float schedule_setpoint_c{21.0f};
  uint16_t schedule_start_min{360};
  uint16_t schedule_end_min{1320};
  uint8_t schedule_day_mask{0x7F};
  uint8_t priority{1};
  ::lune_touch::ZoneNameSource name_source{::lune_touch::ZoneNameSource::GENERATED};
  uint16_t thermal_samples{0};
  float learned_heat_gain_c_per_h{0.0f};
  float learned_cool_loss_c_per_h{0.0f};
  bool enabled{false};
  bool schedule_enabled{false};
  bool unassigned{false};
  bool is_group_secondary{false};
};

// LogicalRoom layout through v14 (weather/physics aggregates added in v15).
struct LogicalRoomV14 {
  char room_id[32]{};
  char room_name[48]{};
  char primary_loop_id[48]{};
  float total_area_m2{1.0f};
  float physical_weight{1.0f};
  float ua_w_per_k{0.0f};
  float thermal_mass_kwh_per_k{0.0f};
  float delivered_kwh_today{0.0f};
  float comfort_setpoint_c{21.0f};
  float comfort_bias_c{0.0f};
  float schedule_setpoint_c{21.0f};
  uint16_t schedule_start_min{360};
  uint16_t schedule_end_min{1320};
  uint8_t schedule_day_mask{0x7F};
  uint8_t priority{1};
  ::lune_touch::ZoneNameSource name_source{::lune_touch::ZoneNameSource::GENERATED};
  bool enabled{false};
  bool include_in_house_temperature{true};
  bool schedule_enabled{false};
};

struct PersistedStateV13 {
  uint32_t magic{::lune_touch::PERSISTED_STATE_MAGIC};
  uint16_t version{::lune_touch::PERSISTED_STATE_VERSION_V13};
  uint16_t reserved{0};
  uint32_t node_count{0};
  uint32_t zone_count{0};
  uint32_t room_count{0};
  ::lune_touch::PairedNode nodes[::lune_touch::MAX_NODES]{};
  LogicalRoomV14 rooms[::lune_touch::MAX_HOUSE_ROOMS]{};
  ZoneBindingV13 zones[::lune_touch::MAX_HOUSE_ZONES]{};
};

struct PersistedStateV14 {
  uint32_t magic{::lune_touch::PERSISTED_STATE_MAGIC};
  uint16_t version{::lune_touch::PERSISTED_STATE_VERSION_V14};
  uint16_t reserved{0};
  uint32_t node_count{0};
  uint32_t zone_count{0};
  uint32_t room_count{0};
  ::lune_touch::PairedNode nodes[::lune_touch::MAX_NODES]{};
  LogicalRoomV14 rooms[::lune_touch::MAX_HOUSE_ROOMS]{};
  ZoneBindingV14 zones[::lune_touch::MAX_HOUSE_ZONES]{};
};

void copy_zone_binding_v13_(::lune_touch::ZoneBinding &dest, const ZoneBindingV13 &src) {
  dest = {};
  std::strncpy(dest.room_id, src.room_id, sizeof(dest.room_id) - 1);
  std::strncpy(dest.room_name, src.room_name, sizeof(dest.room_name) - 1);
  std::strncpy(dest.loop_id, src.loop_id, sizeof(dest.loop_id) - 1);
  std::strncpy(dest.node_id, src.node_id, sizeof(dest.node_id) - 1);
  dest.node_index = src.node_index;
  dest.zone_index = src.zone_index;
  dest.served_area_m2 = src.served_area_m2;
  dest.commissioned = src.commissioned;
  dest.exterior_walls = src.exterior_walls;
  dest.wind_exposure = src.wind_exposure;
  dest.solar_gain = src.solar_gain;
  dest.thermal_lead_h = src.thermal_lead_h;
  dest.max_offset_c = src.max_offset_c;
  dest.comfort_setpoint_c = src.comfort_setpoint_c;
  dest.comfort_bias_c = src.comfort_bias_c;
  dest.schedule_setpoint_c = src.schedule_setpoint_c;
  dest.schedule_start_min = src.schedule_start_min;
  dest.schedule_end_min = src.schedule_end_min;
  dest.schedule_day_mask = src.schedule_day_mask;
  dest.priority = src.priority;
  dest.name_source = src.name_source;
  dest.thermal_samples = src.thermal_samples;
  dest.learned_heat_gain_c_per_h = src.learned_heat_gain_c_per_h;
  dest.learned_cool_loss_c_per_h = src.learned_cool_loss_c_per_h;
  dest.enabled = src.enabled;
  dest.schedule_enabled = src.schedule_enabled;
  dest.unassigned = false;
  dest.is_group_secondary = false;
  dest.floor.unset = true;
}

void copy_zone_binding_v14_(::lune_touch::ZoneBinding &dest, const ZoneBindingV14 &src) {
  dest = {};
  std::strncpy(dest.room_id, src.room_id, sizeof(dest.room_id) - 1);
  std::strncpy(dest.room_name, src.room_name, sizeof(dest.room_name) - 1);
  std::strncpy(dest.loop_id, src.loop_id, sizeof(dest.loop_id) - 1);
  std::strncpy(dest.node_id, src.node_id, sizeof(dest.node_id) - 1);
  dest.node_index = src.node_index;
  dest.zone_index = src.zone_index;
  dest.served_area_m2 = src.served_area_m2;
  dest.commissioned = src.commissioned;
  dest.exterior_walls = src.exterior_walls;
  dest.wind_exposure = src.wind_exposure;
  dest.solar_gain = src.solar_gain;
  dest.thermal_lead_h = src.thermal_lead_h;
  dest.max_offset_c = src.max_offset_c;
  dest.comfort_setpoint_c = src.comfort_setpoint_c;
  dest.comfort_bias_c = src.comfort_bias_c;
  dest.schedule_setpoint_c = src.schedule_setpoint_c;
  dest.schedule_start_min = src.schedule_start_min;
  dest.schedule_end_min = src.schedule_end_min;
  dest.schedule_day_mask = src.schedule_day_mask;
  dest.priority = src.priority;
  dest.name_source = src.name_source;
  dest.thermal_samples = src.thermal_samples;
  dest.learned_heat_gain_c_per_h = src.learned_heat_gain_c_per_h;
  dest.learned_cool_loss_c_per_h = src.learned_cool_loss_c_per_h;
  dest.enabled = src.enabled;
  dest.schedule_enabled = src.schedule_enabled;
  dest.unassigned = src.unassigned;
  dest.is_group_secondary = src.is_group_secondary;
  dest.floor.unset = true;
}

void copy_logical_room_v14_(::lune_touch::LogicalRoom &dest, const LogicalRoomV14 &src) {
  std::memset(&dest, 0, sizeof(dest));
  std::strncpy(dest.room_id, src.room_id, sizeof(dest.room_id) - 1);
  std::strncpy(dest.room_name, src.room_name, sizeof(dest.room_name) - 1);
  std::strncpy(dest.primary_loop_id, src.primary_loop_id, sizeof(dest.primary_loop_id) - 1);
  dest.total_area_m2 = src.total_area_m2;
  dest.physical_weight = src.physical_weight > 0.0f ? src.physical_weight : 1.0f;
  dest.ua_w_per_k = src.ua_w_per_k;
  dest.thermal_mass_kwh_per_k = src.thermal_mass_kwh_per_k;
  dest.delivered_kwh_today = src.delivered_kwh_today;
  dest.comfort_setpoint_c = src.comfort_setpoint_c;
  dest.comfort_bias_c = src.comfort_bias_c;
  dest.schedule_setpoint_c = src.schedule_setpoint_c;
  dest.schedule_start_min = src.schedule_start_min;
  dest.schedule_end_min = src.schedule_end_min;
  dest.schedule_day_mask = src.schedule_day_mask;
  dest.priority = src.priority;
  dest.name_source = src.name_source;
  dest.enabled = src.enabled;
  dest.include_in_house_temperature = src.include_in_house_temperature;
  dest.schedule_enabled = src.schedule_enabled;
  dest.wind_exposure = 0.5f;
  dest.solar_gain = 0.3f;
  dest.weather_seeded = false;
}

// v12 LogicalRoom lacked UA / delivered-energy fields (appended in v13).
struct LogicalRoomV12 {
  char room_id[32]{};
  char room_name[48]{};
  char primary_loop_id[48]{};
  float total_area_m2{1.0f};
  float physical_weight{1.0f};
  float comfort_setpoint_c{21.0f};
  float comfort_bias_c{0.0f};
  float schedule_setpoint_c{21.0f};
  uint16_t schedule_start_min{360};
  uint16_t schedule_end_min{1320};
  uint8_t schedule_day_mask{0x7F};
  uint8_t priority{1};
  ::lune_touch::ZoneNameSource name_source{::lune_touch::ZoneNameSource::GENERATED};
  bool enabled{false};
  bool include_in_house_temperature{true};
  bool schedule_enabled{false};
};

struct PersistedStateV12 {
  uint32_t magic{::lune_touch::PERSISTED_STATE_MAGIC};
  uint16_t version{::lune_touch::PERSISTED_STATE_VERSION_V12};
  uint16_t reserved{0};
  uint32_t node_count{0};
  uint32_t zone_count{0};
  uint32_t room_count{0};
  ::lune_touch::PairedNode nodes[::lune_touch::MAX_NODES]{};
  LogicalRoomV12 rooms[::lune_touch::MAX_HOUSE_ROOMS]{};
  ZoneBindingV13 zones[::lune_touch::MAX_HOUSE_ZONES]{};
};

void copy_logical_room_v12_(::lune_touch::LogicalRoom &dest, const LogicalRoomV12 &src) {
  std::memset(&dest, 0, sizeof(dest));
  std::strncpy(dest.room_id, src.room_id, sizeof(dest.room_id) - 1);
  std::strncpy(dest.room_name, src.room_name, sizeof(dest.room_name) - 1);
  std::strncpy(dest.primary_loop_id, src.primary_loop_id, sizeof(dest.primary_loop_id) - 1);
  dest.total_area_m2 = src.total_area_m2;
  // v12 often stored area-like weights; treat ≤0 as default multiplier 1.0.
  dest.physical_weight = src.physical_weight > 0.0f ? src.physical_weight : 1.0f;
  dest.ua_w_per_k = 0.0f;
  dest.thermal_mass_kwh_per_k = 0.0f;
  dest.delivered_kwh_today = 0.0f;
  dest.comfort_setpoint_c = src.comfort_setpoint_c;
  dest.comfort_bias_c = src.comfort_bias_c;
  dest.schedule_setpoint_c = src.schedule_setpoint_c;
  dest.schedule_start_min = src.schedule_start_min;
  dest.schedule_end_min = src.schedule_end_min;
  dest.schedule_day_mask = src.schedule_day_mask;
  dest.priority = src.priority;
  dest.name_source = src.name_source;
  dest.enabled = src.enabled;
  dest.include_in_house_temperature = src.include_in_house_temperature;
  dest.schedule_enabled = src.schedule_enabled;
}

// v11 used the current node/room/zone records but persisted runtime-only
// temperature history as a fixed array. Keep this wire shape solely to migrate
// existing NVS data; v12 deliberately omits histories from the registry.
struct PersistedStateV11 {
  uint32_t magic{::lune_touch::PERSISTED_STATE_MAGIC};
  uint16_t version{::lune_touch::PERSISTED_STATE_VERSION_V11};
  uint16_t reserved{0};
  uint32_t node_count{0};
  uint32_t zone_count{0};
  uint32_t room_count{0};
  ::lune_touch::PairedNode nodes[::lune_touch::MAX_NODES]{};
  LogicalRoomV12 rooms[::lune_touch::MAX_HOUSE_ROOMS]{};
  ::lune_touch::ZoneBinding zones[::lune_touch::MAX_HOUSE_ZONES]{};
  ::lune_touch::ZoneHistory histories[::lune_touch::MAX_HOUSE_ZONES]{};
};

void copy_legacy_zone_v9_(::lune_touch::ZoneBinding &dest, const ZoneBindingV9 &src,
                          const ::lune_touch::PairedNode *node) {
  std::strncpy(dest.room_id, src.room_id, sizeof(dest.room_id) - 1);
  std::strncpy(dest.room_name, src.room_name, sizeof(dest.room_name) - 1);
  dest.node_index=src.node_index; dest.zone_index=src.zone_index; dest.exterior_walls=src.exterior_walls;
  dest.wind_exposure=src.wind_exposure; dest.solar_gain=src.solar_gain; dest.thermal_lead_h=src.thermal_lead_h;
  dest.max_offset_c=src.max_offset_c; dest.comfort_setpoint_c=src.comfort_setpoint_c;
  dest.comfort_bias_c=src.comfort_bias_c; dest.schedule_setpoint_c=src.schedule_setpoint_c;
  dest.schedule_start_min=src.schedule_start_min; dest.schedule_end_min=src.schedule_end_min;
  dest.schedule_day_mask=src.schedule_day_mask; dest.priority=src.priority; dest.name_source=src.name_source;
  dest.thermal_samples=src.thermal_samples; dest.learned_heat_gain_c_per_h=src.learned_heat_gain_c_per_h;
  dest.learned_cool_loss_c_per_h=src.learned_cool_loss_c_per_h; dest.enabled=src.enabled;
  dest.schedule_enabled=src.schedule_enabled;
  if (node != nullptr && node->node_id[0] != '\0')
    snprintf(dest.loop_id, sizeof(dest.loop_id), "loop-%s-%02u", node->node_id,
             static_cast<unsigned>(src.zone_index + 1));
}

void copy_legacy_zone_v10_(::lune_touch::ZoneBinding &dest, const ZoneBindingV10 &src) {
  std::strncpy(dest.room_id, src.room_id, sizeof(dest.room_id) - 1);
  std::strncpy(dest.room_name, src.room_name, sizeof(dest.room_name) - 1);
  std::strncpy(dest.loop_id, src.loop_id, sizeof(dest.loop_id) - 1);
  dest.node_index = src.node_index; dest.zone_index = src.zone_index;
  dest.exterior_walls = src.exterior_walls; dest.wind_exposure = src.wind_exposure;
  dest.solar_gain = src.solar_gain; dest.thermal_lead_h = src.thermal_lead_h;
  dest.max_offset_c = src.max_offset_c; dest.comfort_setpoint_c = src.comfort_setpoint_c;
  dest.comfort_bias_c = src.comfort_bias_c; dest.schedule_setpoint_c = src.schedule_setpoint_c;
  dest.schedule_start_min = src.schedule_start_min; dest.schedule_end_min = src.schedule_end_min;
  dest.schedule_day_mask = src.schedule_day_mask; dest.priority = src.priority;
  dest.name_source = src.name_source; dest.thermal_samples = src.thermal_samples;
  dest.learned_heat_gain_c_per_h = src.learned_heat_gain_c_per_h;
  dest.learned_cool_loss_c_per_h = src.learned_cool_loss_c_per_h;
  dest.enabled = src.enabled; dest.schedule_enabled = src.schedule_enabled; dest.commissioned = src.enabled;
}

bool populate_missing_loop_ids_(::lune_touch::PersistedState *state) {
  if (state == nullptr)
    return false;
  bool changed = false;
  for (size_t i = 0; i < state->zone_count; i++) {
    ::lune_touch::ZoneBinding &zone = state->zones[i];
    if (!zone.enabled || zone.loop_id[0] != '\0')
      continue;
    if (zone.node_index >= state->node_count || state->nodes[zone.node_index].node_id[0] == '\0') {
      // An unidentified legacy loop is not safe to command; retain it as disabled history.
      zone.enabled = false;
      changed = true;
      continue;
    }
    std::snprintf(zone.loop_id, sizeof(zone.loop_id), "loop-%s-%02u",
                  state->nodes[zone.node_index].node_id,
                  static_cast<unsigned>(zone.zone_index + 1));
    changed = true;
  }
  for (size_t i = 0; i < state->zone_count; i++) {
    ::lune_touch::ZoneBinding &zone = state->zones[i];
    if (zone.node_index < state->node_count && state->nodes[zone.node_index].node_id[0] != '\0' &&
        zone.node_id[0] == '\0') {
      std::strncpy(zone.node_id, state->nodes[zone.node_index].node_id, sizeof(zone.node_id) - 1);
      changed = true;
    }
  }
  return changed;
}

void copy_legacy_node_(::lune_touch::PairedNode &dest, const PairedNodeV5 &src) {
  std::strncpy(dest.node_id, src.node_id, sizeof(dest.node_id) - 1);
  std::strncpy(dest.name, src.node_id, sizeof(dest.name) - 1);
  std::strncpy(dest.hostname, src.hostname, sizeof(dest.hostname) - 1);
  std::strncpy(dest.fallback_ip, src.fallback_ip, sizeof(dest.fallback_ip) - 1);
  std::strncpy(dest.model, src.model, sizeof(dest.model) - 1);
  std::strncpy(dest.firmware, src.firmware, sizeof(dest.firmware) - 1);
  dest.trust = src.trust;
  dest.last_seen_ms = src.last_seen_ms;
  dest.reachable = src.reachable;
}

void copy_legacy_node_(::lune_touch::PairedNode &dest, const PairedNodeV8 &src) {
  std::strncpy(dest.node_id, src.node_id, sizeof(dest.node_id) - 1);
  std::strncpy(dest.name, src.node_id, sizeof(dest.name) - 1);
  std::strncpy(dest.hostname, src.hostname, sizeof(dest.hostname) - 1);
  std::strncpy(dest.fallback_ip, src.fallback_ip, sizeof(dest.fallback_ip) - 1);
  std::strncpy(dest.model, src.model, sizeof(dest.model) - 1);
  std::strncpy(dest.firmware, src.firmware, sizeof(dest.firmware) - 1);
  std::strncpy(dest.pairing_fingerprint, src.pairing_fingerprint,
               sizeof(dest.pairing_fingerprint) - 1);
  dest.trust = src.trust;
  dest.last_seen_ms = src.last_seen_ms;
  dest.reachable = src.reachable;
}

void copy_legacy_zone_(::lune_touch::ZoneBinding &dest, const ZoneBindingV7 &src) {
  std::strncpy(dest.room_id, src.room_id, sizeof(dest.room_id) - 1);
  std::strncpy(dest.room_name, src.room_name, sizeof(dest.room_name) - 1);
  dest.node_index = src.node_index;
  dest.zone_index = src.zone_index;
  dest.exterior_walls = src.exterior_walls;
  dest.wind_exposure = src.wind_exposure;
  dest.solar_gain = src.solar_gain;
  dest.thermal_lead_h = src.thermal_lead_h;
  dest.max_offset_c = src.max_offset_c;
  dest.comfort_setpoint_c = src.comfort_setpoint_c;
  dest.comfort_bias_c = src.comfort_bias_c;
  dest.schedule_setpoint_c = src.schedule_setpoint_c;
  dest.schedule_start_min = src.schedule_start_min;
  dest.schedule_end_min = src.schedule_end_min;
  dest.schedule_day_mask = src.schedule_day_mask;
  dest.priority = src.priority;
  dest.enabled = src.enabled;
  dest.schedule_enabled = src.schedule_enabled;
  dest.name_source = ::lune_touch::ZoneNameSource::TOUCH;
}

void copy_legacy_zone_(::lune_touch::ZoneBinding &dest, const ZoneBindingV8 &src) {
  std::strncpy(dest.room_id, src.room_id, sizeof(dest.room_id) - 1);
  std::strncpy(dest.room_name, src.room_name, sizeof(dest.room_name) - 1);
  dest.node_index = src.node_index;
  dest.zone_index = src.zone_index;
  dest.exterior_walls = src.exterior_walls;
  dest.wind_exposure = src.wind_exposure;
  dest.solar_gain = src.solar_gain;
  dest.thermal_lead_h = src.thermal_lead_h;
  dest.max_offset_c = src.max_offset_c;
  dest.comfort_setpoint_c = src.comfort_setpoint_c;
  dest.comfort_bias_c = src.comfort_bias_c;
  dest.schedule_setpoint_c = src.schedule_setpoint_c;
  dest.schedule_start_min = src.schedule_start_min;
  dest.schedule_end_min = src.schedule_end_min;
  dest.schedule_day_mask = src.schedule_day_mask;
  dest.priority = src.priority;
  dest.thermal_samples = src.thermal_samples;
  dest.learned_heat_gain_c_per_h = src.learned_heat_gain_c_per_h;
  dest.learned_cool_loss_c_per_h = src.learned_cool_loss_c_per_h;
  dest.enabled = src.enabled;
  dest.schedule_enabled = src.schedule_enabled;
  dest.name_source = ::lune_touch::ZoneNameSource::TOUCH;
}

const char *zone_name_source_name_(::lune_touch::ZoneNameSource source) {
  switch (source) {
    case ::lune_touch::ZoneNameSource::V6:
      return "v6";
    case ::lune_touch::ZoneNameSource::TOUCH:
      return "touch";
    case ::lune_touch::ZoneNameSource::GENERATED:
    default:
      return "generated";
  }
}

// Append "[4,5]" for mask bits (1-based V6 zone numbers). Returns false on truncate.
bool append_group_members_json_(char *buffer, size_t capacity, size_t &offset, uint8_t members_mask,
                                uint8_t fallback_zone_1based) {
  if (!appendf_(buffer, capacity, offset, "["))
    return false;
  bool first = true;
  uint8_t emitted = 0;
  for (uint8_t z = 1; z <= 6; z++) {
    if ((members_mask & static_cast<uint8_t>(1u << (z - 1))) == 0)
      continue;
    if (!appendf_(buffer, capacity, offset, "%s%u", first ? "" : ",", static_cast<unsigned>(z)))
      return false;
    first = false;
    emitted++;
  }
  if (emitted == 0 && fallback_zone_1based >= 1 && fallback_zone_1based <= 6) {
    if (!appendf_(buffer, capacity, offset, "%u", static_cast<unsigned>(fallback_zone_1based)))
      return false;
  }
  return appendf_(buffer, capacity, offset, "]");
}

// Resolve the logical room label for a physical slot, following V6 sync-group
// secondaries to their primary so merged loops share one room name.
const char *display_binding_room_label_(const ::lune_touch::HouseModel &model,
                                        const ::lune_touch::ZoneBinding *binding,
                                        const ::lune_touch::ZoneLiveState *live) {
  if (binding == nullptr)
    return "Zone";
  if (live != nullptr && binding->is_group_secondary && live->group_primary_zone >= 1 &&
      live->group_primary_zone <= 6) {
    const size_t primary_slot = static_cast<size_t>(live->group_primary_zone - 1);
    for (size_t i = 0; i < model.zone_count(); i++) {
      const auto *candidate = model.zone(i);
      if (candidate == nullptr || !candidate->enabled || candidate->is_group_secondary)
        continue;
      if (candidate->node_index != binding->node_index || candidate->zone_index != primary_slot)
        continue;
      if (candidate->room_name[0] != '\0')
        return candidate->room_name;
      if (candidate->room_id[0] != '\0')
        return candidate->room_id;
      break;
    }
  }
  if (binding->room_name[0] != '\0')
    return binding->room_name;
  if (binding->room_id[0] != '\0')
    return binding->room_id;
  return "Zone";
}

uint8_t group_member_count_(uint8_t members_mask) {
  uint8_t count = 0;
  for (uint8_t z = 0; z < 6; z++) {
    if ((members_mask & static_cast<uint8_t>(1u << z)) != 0)
      count++;
  }
  return count;
}

struct PersistedPreloadLearning {
  uint32_t magic{PRELOAD_LEARN_MAGIC};
  uint16_t version{PRELOAD_LEARN_VERSION};
  uint16_t count{0};
  struct Entry {
    char room_id[32]{};
    float gain_scale{1.0f};
    int8_t lead_bias_h{0};
    uint8_t reserved[3]{};
  } zones[::lune_touch::MAX_HOUSE_ZONES]{};
};

void json_escape_(const char *src, char *out, size_t out_len) {
  if (out_len == 0)
    return;
  if (src == nullptr)
    src = "";
  size_t off = 0;
  for (const char *p = src; *p != '\0' && off + 1 < out_len; ++p) {
    const char c = *p;
    if ((c == '"' || c == '\\') && off + 2 < out_len) {
      out[off++] = '\\';
      out[off++] = c;
    } else if (static_cast<unsigned char>(c) < 0x20) {
      out[off++] = ' ';
    } else {
      out[off++] = c;
    }
  }
  out[off] = '\0';
}

void format_circulation_json_(const CirculationPumpState &source, uint32_t now_ms,
                              uint32_t stale_ms, char *buffer, size_t capacity) {
  char host[128];
  char flow_entity[96];
  char head_entity[96];
  char power_entity[96];
  char error[192];
  char flow[16];
  char head[16];
  char power[16];
  char thermal[16];
  char authority[16];
  char thermal_source[32];
  json_escape_(source.host, host, sizeof(host));
  json_escape_(source.flow_entity, flow_entity, sizeof(flow_entity));
  json_escape_(source.head_entity, head_entity, sizeof(head_entity));
  json_escape_(source.power_entity, power_entity, sizeof(power_entity));
  json_escape_(source.last_error, error, sizeof(error));
  json_escape_(source.thermal_kw_source, thermal_source, sizeof(thermal_source));
  json_float_token_(flow, sizeof(flow), source.has_flow ? source.flow_m3h : NAN, 2);
  json_float_token_(head, sizeof(head), source.has_head ? source.head_m : NAN, 2);
  json_float_token_(power, sizeof(power), source.has_power ? source.power_w : NAN, 1);
  json_float_token_(thermal, sizeof(thermal), source.has_thermal_kw ? source.thermal_kw : NAN, 2);
  json_float_token_(authority, sizeof(authority),
                    source.has_hydraulic_authority ? source.hydraulic_authority : NAN, 2);
  const bool any = source.has_flow || source.has_head || source.has_power;
  const uint32_t fresh_ms = source.last_success_ms != 0 ? source.last_success_ms : source.last_fetch_ms;
  const bool fresh = any && fresh_ms != 0 && now_ms - fresh_ms <= stale_ms;
  const char *status = "unconfigured";
  if (!source.enabled)
    status = source.host[0] != '\0' ? "disabled" : "unconfigured";
  else if (source.last_fetch_ms == 0)
    status = "pending";
  else if (fresh)
    status = "available";
  else if (any)
    status = "stale";
  else
    status = "unreachable";
  const unsigned long age_s = source.last_fetch_ms == 0 ? 0UL :
      static_cast<unsigned long>((now_ms - source.last_fetch_ms) / 1000UL);
  const float flow_lmin = source.has_flow && std::isfinite(source.flow_m3h)
                              ? source.flow_m3h * (1000.0f / 60.0f)
                              : NAN;
  char flow_lmin_tok[16];
  json_float_token_(flow_lmin_tok, sizeof(flow_lmin_tok), flow_lmin, 1);
  snprintf(buffer, capacity,
           "{\"enabled\":%s,\"host\":\"%s\",\"port\":%u,\"refresh_interval_s\":%u,"
           "\"refresh_interval_idle_s\":%u,\"refresh_interval_active_s\":%u,"
           "\"flow_entity\":\"%s\",\"head_entity\":\"%s\",\"power_entity\":\"%s\","
           "\"reachable\":%s,\"fresh\":%s,\"status\":\"%s\",\"http_status\":%d,\"age_s\":%lu,"
           "\"flow_m3h\":%s,\"flow_l_min\":%s,\"head_m\":%s,\"power_w\":%s,"
           "\"thermal_kw\":%s,\"thermal_kw_source\":\"%s\","
           "\"hydraulic_authority\":%s,\"consecutive_failures\":%u,\"last_error\":\"%s\"}",
           source.enabled ? "true" : "false", host, static_cast<unsigned>(source.port),
           static_cast<unsigned>(source.refresh_interval_s),
           static_cast<unsigned>(source.refresh_interval_idle_s),
           static_cast<unsigned>(source.refresh_interval_active_s), flow_entity, head_entity,
           power_entity, source.reachable ? "true" : "false", fresh ? "true" : "false",
           status, source.last_http_status, age_s, flow, flow_lmin_tok, head, power, thermal,
           thermal_source, authority, static_cast<unsigned>(source.consecutive_failures), error);
}

enum class CirculationUiStatus : uint8_t { Hidden, Waiting, Ok, Stale, Offline };

CirculationUiStatus circulation_ui_status_(const CirculationPumpState &source, uint32_t now_ms,
                                           uint32_t stale_ms) {
  if (!source.enabled || source.host[0] == '\0')
    return CirculationUiStatus::Hidden;
  const bool any = source.has_flow || source.has_head || source.has_power;
  const bool fresh = any && source.last_fetch_ms != 0 && now_ms - source.last_fetch_ms <= stale_ms;
  if (source.last_fetch_ms == 0)
    return CirculationUiStatus::Waiting;
  if (fresh)
    return CirculationUiStatus::Ok;
  if (any)
    return CirculationUiStatus::Stale;
  return CirculationUiStatus::Offline;
}

uint8_t wifi_display_bars_() {
  if (!esphome::network::is_connected())
    return 0;
  if (esphome::wifi::global_wifi_component == nullptr)
    return 3;
  const int rssi = static_cast<int>(esphome::wifi::global_wifi_component->wifi_rssi());
  if (rssi <= -100)
    return 3;
  if (rssi >= -50)
    return 5;
  if (rssi >= -60)
    return 4;
  if (rssi >= -67)
    return 3;
  if (rssi >= -75)
    return 2;
  return 1;
}

}  // namespace

void LuneTouchCoordinator::setup() {
  state_lock_ = xSemaphoreCreateMutex();
  heat_source_probe_done_ = xSemaphoreCreateBinary();
  init_touch_registry_partition_();
  boot_id_ = esp_random();
  if (boot_id_ == 0)
    boot_id_ = 1;
  ledger_.set_boot_id(boot_id_);
  model_.set_node_stale_after_ms(node_stale_after_ms_);
  // Compact the historical command ledger before migrating/saving the larger
  // coordinator registry; both namespaces share the small NVS partition.
  load_ledger_();
  const bool loaded_registry = load_registry_();
  cleanup_legacy_touch_registry_();
  load_forecast_settings_();
  load_preload_learning_();
  load_settings_();
  load_odin_control_();
  load_price_settings_();
  {
    // PSRAM: tariff caches, two days of quarter-hour spot samples and the
    // hourly breakdown (~10 KiB) never sit in internal RAM or on a stack.
    void *mem = heap_caps_calloc(1, sizeof(PriceRuntime), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (mem == nullptr)
      mem = heap_caps_calloc(1, sizeof(PriceRuntime), MALLOC_CAP_8BIT);
    price_rt_ = mem != nullptr ? new (mem) PriceRuntime() : nullptr;
    // Re-assert Odin's manual price mode once per boot while enabled.
    if (price_.cfg.enabled)
      price_.source_pending = 1;
  }
  ensure_automatic_identity_();
  std::snprintf(authority_lease_id_, sizeof(authority_lease_id_), "touch-%08lx",
                static_cast<unsigned long>(boot_id_));
  load_forecast_cache_();
  log_event_("info", "boot", "coordinator ready");
  if (!loaded_registry)
    ESP_LOGI(TAG, "No persisted Touch registry; waiting for dashboard pairing");
  ESP_LOGI(TAG, "Lune Touch coordinator model ready");
  ESP_LOGI(TAG, "  stale_after=%ums max_nodes=%u ledger_capacity=%u",
           static_cast<unsigned>(node_stale_after_ms_),
           static_cast<unsigned>(::lune_touch::MAX_NODES),
           static_cast<unsigned>(::lune_touch::LEDGER_CAPACITY));
  start_worker_tasks_();
}

void LuneTouchCoordinator::kick_task_(TaskHandle_t handle) const {
  if (handle != nullptr)
    xTaskNotifyGive(handle);
}

void LuneTouchCoordinator::start_worker_tasks_() {
  // Each integration runs on its own task so a hung HTTPS forecast fetch, LAN
  // sweep, or Asgard timeout cannot stall V6 polling or sibling integrations.
  // Stacks go in PSRAM: after LVGL/WiFi bring-up, internal DRAM often cannot
  // fit a 12–20 KiB task stack (seen as poll_generation stuck at 0).
  auto create = [](TaskFunction_t fn, const char *name, uint32_t stack_bytes, void *arg,
                   UBaseType_t priority, TaskHandle_t *handle) -> bool {
    BaseType_t ok = xTaskCreatePinnedToCoreWithCaps(
        fn, name, stack_bytes, arg, priority, handle, POLL_CORE,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ok != pdPASS) {
      ESP_LOGW(TAG, "PSRAM stack failed for %s; trying internal DRAM", name);
      ok = xTaskCreatePinnedToCore(fn, name, stack_bytes, arg, priority, handle, POLL_CORE);
    }
    if (ok != pdPASS) {
      ESP_LOGE(TAG, "Failed to start task %s (stack=%u, free_int=%u largest_int=%u free_psram=%u)",
               name, static_cast<unsigned>(stack_bytes),
               static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
               static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
               static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
      if (handle != nullptr)
        *handle = nullptr;
      return false;
    }
    ESP_LOGI(TAG, "Started task %s (stack=%u)", name, static_cast<unsigned>(stack_bytes));
    return true;
  };

  // Poll first — V6 reachability / authority proposals depend on it.
  if (!create(poll_task_func_, "lune_touch_poll", POLL_STACK_SIZE, this, POLL_PRIORITY,
              &poll_task_handle_)) {
    ESP_LOGE(TAG, "V6 poll task unavailable — using ESPHome loop fallback");
    log_event_("error", "poll", "poll task unavailable; loop fallback");
  } else {
    log_event_("info", "poll", "poll task started");
  }
  create(forecast_task_func_, "lune_touch_fcst", FORECAST_STACK_SIZE, this, INTEGRATION_PRIORITY,
         &forecast_task_handle_);
  create(odin_task_func_, "lune_touch_odin", INTEGRATION_STACK_SIZE, this, INTEGRATION_PRIORITY,
         &odin_task_handle_);
  create(circulation_task_func_, "lune_touch_circ", INTEGRATION_STACK_SIZE, this, INTEGRATION_PRIORITY,
         &circulation_task_handle_);
  create(heat_source_task_func_, "lune_touch_heat", INTEGRATION_STACK_SIZE, this, INTEGRATION_PRIORITY,
         &heat_source_task_handle_);
  create(lan_scan_task_func_, "lune_touch_lan", INTEGRATION_STACK_SIZE, this, INTEGRATION_PRIORITY,
         &lan_scan_task_handle_);
}

void LuneTouchCoordinator::loop() {
  const uint32_t now = esphome::millis();
  // Stack headroom per task (bytes left), logged every second for the first
  // 3 minutes after boot and then every 5 minutes: finds a task that is about
  // to overflow before it takes the device down.
  {
    static uint32_t last_wm_ms = 0;
    const uint32_t period = now < 180000UL ? 1000UL : 300000UL;
    if (now - last_wm_ms >= period) {
      last_wm_ms = now;
      auto wm = [](TaskHandle_t h) -> int {
        return h == nullptr ? -1 : static_cast<int>(uxTaskGetStackHighWaterMark(h));
      };
      static TaskHandle_t httpd = nullptr;
      if (httpd == nullptr)
        httpd = xTaskGetHandle("httpd");
      ESP_LOGI(TAG, "stack free: httpd=%d poll=%d fcst=%d odin=%d circ=%d heat=%d lan=%d heap_int=%u",
               wm(httpd), wm(poll_task_handle_), wm(forecast_task_handle_), wm(odin_task_handle_),
               wm(circulation_task_handle_), wm(heat_source_task_handle_), wm(lan_scan_task_handle_),
               static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
    }
  }
  maybe_run_poll_from_loop_(now);
  maybe_run_integrations_from_loop_(now);

  if ((int32_t) (now - last_ledger_expire_ms_) < 5000)
    return;
  last_ledger_expire_ms_ = now;

  if (!take_state_lock_(10))
    return;
  const size_t expired = ledger_.expire_pending(now, current_epoch_s_());
  give_state_lock_();
  if (expired > 0)
    save_ledger_();
}

void LuneTouchCoordinator::dump_config() {
  ESP_LOGCONFIG(TAG, "Lune Touch Coordinator:");
  ESP_LOGCONFIG(TAG, "  Node stale after: %u ms", static_cast<unsigned>(node_stale_after_ms_));
  ESP_LOGCONFIG(TAG, "  V6 endpoints: /api/v1/overview, /zones, /events");
  ESP_LOGCONFIG(TAG, "  Command path: expiring coordinator commands, clamped by Lune V6");
  ESP_LOGCONFIG(TAG, "  Heat source: %s:%u / %s every %us",
                heat_source_.host, static_cast<unsigned>(heat_source_.port),
                heat_source_.weighted_temperature_variable,
                static_cast<unsigned>(heat_source_.push_interval_s));
  ESP_LOGCONFIG(TAG, "  Circulation: %s:%u flow=%s head=%s power=%s every %us",
                circulation_.host[0] != '\0' ? circulation_.host : "(unset)",
                static_cast<unsigned>(circulation_.port), circulation_.flow_entity,
                circulation_.head_entity, circulation_.power_entity,
                static_cast<unsigned>(circulation_.refresh_interval_s));
}

bool LuneTouchCoordinator::take_state_lock_(uint32_t timeout_ms) const {
  return state_lock_ == nullptr || xSemaphoreTake(state_lock_, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void LuneTouchCoordinator::give_state_lock_() const {
  if (state_lock_ != nullptr)
    xSemaphoreGive(state_lock_);
}

void LuneTouchCoordinator::log_event_(const char *level, const char *source, const char *message) {
  if (!take_state_lock_(5))
    return;
  EventRecord &event = events_[event_next_];
  event.ts_ms = esphome::millis();
  std::strncpy(event.level, level != nullptr && level[0] != '\0' ? level : "info",
               sizeof(event.level) - 1);
  event.level[sizeof(event.level) - 1] = '\0';
  std::strncpy(event.source, source != nullptr && source[0] != '\0' ? source : "touch",
               sizeof(event.source) - 1);
  event.source[sizeof(event.source) - 1] = '\0';
  std::strncpy(event.message, message != nullptr && message[0] != '\0' ? message : "-",
               sizeof(event.message) - 1);
  event.message[sizeof(event.message) - 1] = '\0';
  event_next_ = (event_next_ + 1) % EVENT_CAPACITY;
  if (event_count_ < EVENT_CAPACITY)
    event_count_++;
  give_state_lock_();
}

void LuneTouchCoordinator::poll_task_func_(void *arg) {
  static_cast<LuneTouchCoordinator *>(arg)->poll_task_();
}

void LuneTouchCoordinator::forecast_task_func_(void *arg) {
  static_cast<LuneTouchCoordinator *>(arg)->forecast_task_();
}

void LuneTouchCoordinator::odin_task_func_(void *arg) {
  static_cast<LuneTouchCoordinator *>(arg)->odin_task_();
}

void LuneTouchCoordinator::circulation_task_func_(void *arg) {
  static_cast<LuneTouchCoordinator *>(arg)->circulation_task_();
}

void LuneTouchCoordinator::heat_source_task_func_(void *arg) {
  static_cast<LuneTouchCoordinator *>(arg)->heat_source_task_();
}

void LuneTouchCoordinator::lan_scan_task_func_(void *arg) {
  static_cast<LuneTouchCoordinator *>(arg)->lan_scan_task_();
}

void LuneTouchCoordinator::forecast_task_() {
  // The TLS task: Open-Meteo forecast and the energy-price fetch/push share it,
  // so two HTTPS handshakes never compete for internal RAM.
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (!esphome::network::is_connected())
      continue;
    bool do_forecast = true;
    bool do_price = false;
    if (take_state_lock_(100)) {
      do_forecast = forecast_task_fetch_pending_;
      forecast_task_fetch_pending_ = false;
      do_price = price_.run_pending || price_.source_pending != 0;
      give_state_lock_();
    }
    if (do_forecast) {
      char response[384];
      perform_forecast_fetch_(response, sizeof(response));
    }
    if (do_price)
      run_price_cycle_();
  }
}

void LuneTouchCoordinator::run_mqtt_followups_() {
  if (mqtt_refetch_pending_) {
    mqtt_refetch_pending_ = false;
    fetch_odin_plan_();
  }
  const uint8_t action = mqtt_absorb_action_;
  mqtt_absorb_action_ = 0;
  if (action == 1)
    maybe_dispatch_absorb_arm_();
  else if (action == 2)
    maybe_dispatch_absorb_disarm_(mqtt_absorb_reason_[0] != '\0' ? mqtt_absorb_reason_ : "mqtt");
}

void LuneTouchCoordinator::odin_task_() {
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (!esphome::network::is_connected())
      continue;
    run_mqtt_followups_();
    fetch_odin_plan_();
    ensure_odin_mqtt_client_();
    // MQTT loss must never block core control: degrade to HTTP /api/forecast poll.
    if (odin_mqtt_.enabled && !odin_mqtt_.connected)
      odin_mqtt_.http_fallback_active = true;
    if (odin_plan_.odin_host[0] != '\0') {
      fetch_odin_physics_();
      run_odin_control_();
    }
  }
}

void LuneTouchCoordinator::circulation_task_() {
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (!esphome::network::is_connected()) {
      if (take_state_lock_(100)) {
        // Typically right after boot, before Wi-Fi is up: retry in ~30 s
        // instead of a full refresh interval (up to 5 min of "unreachable").
        const uint32_t interval_ms = static_cast<uint32_t>(circulation_.refresh_interval_s) * 1000UL;
        circulation_.last_fetch_ms = esphome::millis() - (interval_ms > 30000UL ? interval_ms - 30000UL : 0);
        circulation_.last_http_status = 0;
        circulation_.reachable = false;
        std::strncpy(circulation_.last_error, "network_offline",
                     sizeof(circulation_.last_error) - 1);
        circulation_.last_error[sizeof(circulation_.last_error) - 1] = '\0';
        give_state_lock_();
      }
      continue;
    }
    fetch_circulation_pump_();
  }
}

void LuneTouchCoordinator::heat_source_task_() {
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (!esphome::network::is_connected())
      continue;

    HeatSourceProbeKind probe = HeatSourceProbeKind::NONE;
    if (take_state_lock_(100)) {
      probe = heat_source_probe_kind_;
      heat_source_probe_kind_ = HeatSourceProbeKind::NONE;
      give_state_lock_();
    }
    if (probe == HeatSourceProbeKind::TEST_READ) {
      heat_source_probe_ok_ =
          run_heat_source_test_read_(heat_source_probe_response_, sizeof(heat_source_probe_response_));
      if (heat_source_probe_done_ != nullptr)
        xSemaphoreGive(heat_source_probe_done_);
      continue;
    }
    if (probe == HeatSourceProbeKind::TEST_PUSH) {
      heat_source_probe_ok_ =
          run_heat_source_test_push_(heat_source_probe_response_, sizeof(heat_source_probe_response_));
      if (heat_source_probe_done_ != nullptr)
        xSemaphoreGive(heat_source_probe_done_);
      continue;
    }

    bool push_due = false;
    bool lease_held = false;
    bool reseed = false;
    bool asgard_type = true;
    if (take_state_lock_(100)) {
      const uint32_t now = esphome::millis();
      push_due = heat_source_push_requested_ ||
                 (heat_source_.enabled && heat_source_.host[0] != '\0' &&
                  (heat_source_.last_write_ms == 0 ||
                   now - heat_source_.last_write_ms >=
                       static_cast<uint32_t>(heat_source_.push_interval_s) * 1000UL));
      heat_source_push_requested_ = false;
      lease_held = authority_expires_at_ms_ != 0 &&
                   static_cast<int32_t>(now - authority_expires_at_ms_) < 0;
      reseed = trim_reseed_pending_;
      asgard_type = heat_source_type_is_asgard_(heat_source_.type);
      give_state_lock_();
    }
    if (!push_due)
      continue;
    if (asgard_type) {
      poll_asgard_freeze_inputs_();
      poll_asgard_telemetry_();
    }
    if (lease_held)
      push_weighted_temperature_();
    if (asgard_type)
      sync_comfort_target_();
    else
      push_generic_levers_();
    if (asgard_type && lease_held) {
      if (reseed) {
        HeatSourceState source{};
        if (take_state_lock_(100)) {
          source = heat_source_;
          give_state_lock_();
        }
        float bias = NAN;
        const asgard_adapter::Config cfg = make_asgard_config_(source);
        char url[448]{};
        if (asgard_adapter::build_bias_read_url(cfg, url, sizeof(url))) {
          char body[384]{};
          int status = 0;
          if (fetch_json_(url, body, sizeof(body), &status) &&
              asgard_adapter::parse_number_response(body, &bias) && take_state_lock_(100)) {
            flow_trim_.reseed_from_readback(bias);
            heat_source_.last_bias_confirmed_c = bias;
            heat_source_.trim_bias_c = flow_trim_.bias_c;
            trim_reseed_pending_ = false;
            give_state_lock_();
            log_event_("info", "heat_source", "trim reseeded from Asgard bias");
          }
        }
      }
      step_flow_trim_();
    }
  }
}

void LuneTouchCoordinator::lan_scan_task_() {
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (!esphome::network::is_connected())
      continue;
    discover_v6_on_lan_();
  }
}

void LuneTouchCoordinator::poll_task_() {
  // An explicit dashboard scan must wake the task even during the normal
  // boot grace period; a plain vTaskDelay would leave the user waiting the
  // full boot delay before any V6 names could be fetched.
  ESP_LOGI(TAG, "V6 poll task entered");
  ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(POLL_BOOT_DELAY_MS));
  while (true) {
    // Heartbeat first — even when offline — so /nodes can distinguish a dead
    // poll task (generation stuck at 0) from a temporary network outage.
    poll_generation_++;

    if (esphome::network::is_connected()) {
      bool forecast_due = false;
      bool odin_due = false;
      bool circulation_due = false;
      bool heat_due = false;
      bool lan_scan_due = false;
      bool lease_due = false;
      bool price_due = false;
      int price_ymd = 0;
      uint16_t price_minute = 0;
      const bool price_clock_ok = local_today_(&price_ymd, &price_minute);

      if (take_state_lock_(100)) {
        const uint32_t now = esphome::millis();
        const bool has_location = std::isfinite(forecast_latitude_) && std::isfinite(forecast_longitude_) &&
                                  (std::fabs(forecast_latitude_) >= 0.0001f ||
                                   std::fabs(forecast_longitude_) >= 0.0001f);
        const bool initial_fetch_due = has_location && forecast_last_fetch_ms_ == 0;
        const bool boot_refresh_due = has_location && forecast_boot_refresh_pending_;
        const bool interval_fetch_due =
            has_location && forecast_last_fetch_ms_ != 0 &&
            now - forecast_last_fetch_ms_ >= FORECAST_AUTO_FETCH_INTERVAL_MS;
        // The forecast is the only TLS fetch. At boot, V6 polls, the registry
        // save and the Odin fetches compete for internal RAM; wait 2 min so a
        // TLS handshake never lands in that burst (the cache covers the gap).
        static constexpr uint32_t FORECAST_BOOT_DELAY_MS = 120000UL;
        const bool boot_settled = now >= FORECAST_BOOT_DELAY_MS;
        forecast_due = boot_settled && (forecast_fetch_requested_ || initial_fetch_due ||
                                        boot_refresh_due || interval_fetch_due);
        if (forecast_due) {
          forecast_fetch_requested_ = false;
          forecast_boot_refresh_pending_ = false;
        }

        odin_due = odin_plan_active_() &&
                   (odin_plan_.last_fetch_ms == 0 ||
                    now - odin_plan_.last_fetch_ms >=
                        static_cast<uint32_t>(odin_plan_.refresh_interval_s) * 1000UL);
        // Odin link health / comfort control runs whenever an Odin host is set.
        odin_due = odin_due || (odin_plan_.odin_host[0] != '\0' &&
                                (odin_control_.last_cycle_ms == 0 ||
                                 now - odin_control_.last_cycle_ms >= 300000UL));

        circulation_due = circulation_poll_requested_ ||
                          (circulation_.enabled && circulation_.host[0] != '\0' &&
                           (circulation_.last_fetch_ms == 0 ||
                            now - circulation_.last_fetch_ms >=
                                static_cast<uint32_t>(circulation_.refresh_interval_s) * 1000UL));
        circulation_poll_requested_ = false;

        heat_due = heat_source_push_requested_ ||
                   (heat_source_.enabled && heat_source_.host[0] != '\0' &&
                    (heat_source_.last_write_ms == 0 ||
                     now - heat_source_.last_write_ms >=
                         static_cast<uint32_t>(heat_source_.push_interval_s) * 1000UL));

        lan_scan_due = lan_scan_requested_;
        lan_scan_requested_ = false;
        // A paired V6 that stopped answering has most likely moved to a new IP;
        // the LAN scan follows it by fingerprint. At most every 30 min, and not
        // in the first 3 min after boot while nodes are still being polled.
        if (!lan_scan_due && now > 3UL * 60UL * 1000UL &&
            (auto_lan_scan_last_ms_ == 0 || now - auto_lan_scan_last_ms_ >= 30UL * 60UL * 1000UL)) {
          for (size_t i = 0; i < model_.node_count(); i++) {
            const auto *node = model_.node(i);
            if (node != nullptr && node->trust != ::lune_touch::NodeTrust::UNPAIRED &&
                node->pairing_fingerprint[0] != '\0' && model_.is_node_stale(i, now)) {
              lan_scan_due = true;
              auto_lan_scan_last_ms_ = now;
              ESP_LOGI(TAG, "Node %s unreachable; scanning LAN for a moved V6", node->node_id);
              break;
            }
          }
        }
        node_refresh_requested_ = false;

        lease_due = authority_last_renew_ms_ == 0 || now - authority_last_renew_ms_ >= 30000UL;
        if (forecast_due)
          forecast_task_fetch_pending_ = true;

        // Energy price → Odin: decide here (cheap), fetch/push on the TLS task.
        if (!price_.run_pending) {
          const lune_touch_price::Due due = lune_touch_price::push_due(
              price_.cfg.enabled, price_.push_requested, price_clock_ok, price_ymd, price_minute,
              price_.sched);
          if (due != lune_touch_price::Due::NONE &&
              (boot_settled || due == lune_touch_price::Due::REQUEST)) {
            price_.run_pending = true;
            price_.due = due;
            price_.push_requested = false;
          }
        }
        if (price_.source_pending != 0 && boot_settled &&
            (price_.source_last_try_ms == 0 || now - price_.source_last_try_ms >= 300000UL))
          price_due = true;
        price_due = price_due || price_.run_pending;
        give_state_lock_();
      }

      // V6 node poll is the critical path and must never wait on weather,
      // Asgard, circulation, or LAN discovery HTTP.
      poll_once_();
      if (lease_due)
        renew_authority_lease_();

      if (circulation_due)
        schedule_circulation_fetch_();
      if (odin_due)
        kick_task_(odin_task_handle_);
      if (heat_due)
        kick_task_(heat_source_task_handle_);
      if (lan_scan_due)
        kick_task_(lan_scan_task_handle_);
      if (forecast_due || price_due)
        kick_task_(forecast_task_handle_);
    } else {
      ESP_LOGW(TAG, "V6 poll skipped: network offline (generation=%lu)",
               static_cast<unsigned long>(poll_generation_));
    }
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(POLL_INTERVAL_MS));
  }
}

void LuneTouchCoordinator::maybe_run_poll_from_loop_(uint32_t now) {
  // Prefer a real poll worker. Creating it here (after Wi-Fi) retries when boot
  // heap was too fragmented for the initial attempt.
  if (poll_task_handle_ == nullptr && esphome::network::is_connected()) {
    static uint32_t last_create_try_ms = 0;
    if (last_create_try_ms == 0 || now - last_create_try_ms >= 10000) {
      last_create_try_ms = now;
      const BaseType_t ok = xTaskCreatePinnedToCoreWithCaps(
          poll_task_func_, "lune_touch_poll", POLL_STACK_SIZE, this, POLL_PRIORITY,
          &poll_task_handle_, POLL_CORE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      if (ok == pdPASS) {
        log_event_("info", "poll", "poll task started (deferred)");
        ESP_LOGI(TAG, "Started deferred poll task on PSRAM stack");
        return;
      }
      poll_task_handle_ = nullptr;
    }
  }

  if (poll_task_handle_ != nullptr) {
    if (poll_generation_ != 0)
      return;
    if (static_cast<int32_t>(now - POLL_BOOT_DELAY_MS) < 8000)
      return;
    xTaskNotifyGive(poll_task_handle_);
    return;
  }

  // Last resort only: never run multi-second HTTP from the LVGL/ESPHome loop
  // more than once per interval, and keep it off the critical path when a
  // worker exists.
  if (last_poll_ms_ != 0 && now - last_poll_ms_ < POLL_INTERVAL_MS)
    return;
  poll_generation_++;
  last_poll_ms_ = now;
  if (!esphome::network::is_connected()) {
    ESP_LOGW(TAG, "V6 poll (loop fallback) skipped: network offline");
    return;
  }
  ESP_LOGW(TAG, "V6 poll via ESPHome loop fallback (no worker)");
  poll_once_();
}

void LuneTouchCoordinator::schedule_circulation_fetch_() {
  if (circulation_task_handle_ != nullptr) {
    xTaskNotifyGive(circulation_task_handle_);
    return;
  }
  if (!esphome::network::is_connected()) {
    if (take_state_lock_(100)) {
      circulation_.last_fetch_ms = esphome::millis();
      circulation_.last_http_status = 0;
      circulation_.reachable = false;
      std::strncpy(circulation_.last_error, "network_offline",
                   sizeof(circulation_.last_error) - 1);
      circulation_.last_error[sizeof(circulation_.last_error) - 1] = '\0';
      give_state_lock_();
    }
    return;
  }
  fetch_circulation_pump_();
}

void LuneTouchCoordinator::maybe_run_integrations_from_loop_(uint32_t now) {
  bool circulation_due = false;
  uint32_t last_fetch_ms = 0;
  if (take_state_lock_(50)) {
    last_fetch_ms = circulation_.last_fetch_ms;
    circulation_due = circulation_.enabled && circulation_.host[0] != '\0' &&
                      (circulation_poll_requested_ || last_fetch_ms == 0 ||
                       now - last_fetch_ms >=
                           static_cast<uint32_t>(circulation_.refresh_interval_s) * 1000UL);
    if (circulation_due)
      circulation_poll_requested_ = false;
    give_state_lock_();
  }
  if (!circulation_due)
    return;

  // Prefer the dedicated worker. If it never produces a fetch result, run
  // inline so the UI cannot stay stuck on status=pending forever.
  if (circulation_task_handle_ != nullptr) {
    xTaskNotifyGive(circulation_task_handle_);
    if (last_fetch_ms != 0 || now < POLL_BOOT_DELAY_MS + 15000UL)
      return;
    ESP_LOGW(TAG, "Circulation worker silent; fetching from ESPHome loop");
    if (esphome::network::is_connected())
      fetch_circulation_pump_();
    else if (take_state_lock_(100)) {
      circulation_.last_fetch_ms = now;
      circulation_.last_http_status = 0;
      circulation_.reachable = false;
      std::strncpy(circulation_.last_error, "network_offline",
                   sizeof(circulation_.last_error) - 1);
      circulation_.last_error[sizeof(circulation_.last_error) - 1] = '\0';
      give_state_lock_();
    }
    return;
  }
  schedule_circulation_fetch_();
}

bool LuneTouchCoordinator::read_asgard_number_(const HeatSourceState &source, float *value,
                                               char *error, size_t error_capacity) {
  if (value == nullptr || error == nullptr || error_capacity == 0)
    return false;
  *value = NAN;
  error[0] = '\0';
  const asgard_adapter::Config adapter_config = make_asgard_config_(source);
  char url[448];
  if (!asgard_adapter::build_physical_read_url(adapter_config, url, sizeof(url))) {
    std::snprintf(error, error_capacity, "invalid read endpoint");
    return false;
  }
  for (uint8_t attempt = 0; attempt < asgard_confirmation::MAX_READ_ATTEMPTS; ++attempt) {
    char body[384];
    int status = 0;
    if (esphome::network::is_connected() && fetch_json_(url, body, sizeof(body), &status)) {
      if (asgard_adapter::parse_number_response(body, value)) {
        return true;
      }
      std::snprintf(error, error_capacity, "read value missing");
    } else {
      std::snprintf(error, error_capacity, status > 0 ? "read http %d" : "read unreachable", status);
    }
    if (attempt + 1 < asgard_confirmation::MAX_READ_ATTEMPTS)
      vTaskDelay(pdMS_TO_TICKS(asgard_confirmation::readback_backoff_ms(attempt)));
  }
  return false;
}

bool LuneTouchCoordinator::push_weighted_temperature_() {
  // z1 gate: Odin 2.0 dropout rejection catches unusable values, but a plausible
  // default such as 24.00 still slips through. Keep this gate — do not remove as
  // "redundant" after Odin learns physics itself (U5).
  static constexpr uint32_t COVERAGE_GRACE_MS = 300000;
  HeatSourceState source;
  ::lune_touch::StrategySnapshot strategy;
  if (!take_state_lock_(100))
    return false;
  source = heat_source_;
  strategy = model_.strategy_snapshot();
  give_state_lock_();

  char error[sizeof(heat_source_.last_error)]{};
  bool ok = false;
  // Local gates (coverage / config) must not look like Asgard unreachable.
  asgard_confirmation::Status confirmation_status = asgard_confirmation::Status::BLOCKED;
  float confirmed_value = NAN;
  float value_to_write = NAN;
  int write_http_status = 0;
  if (!source.enabled) {
    std::strncpy(error, "disabled", sizeof(error) - 1);
  } else if (!heat_source_type_supported_(source.type)) {
    std::strncpy(error, "unsupported heat source type", sizeof(error) - 1);
  } else if (source.host[0] == '\0') {
    std::strncpy(error, "address missing", sizeof(error) - 1);
  } else if (strategy.has_physical_temperature && std::isfinite(strategy.physical_temperature_c)) {
    value_to_write = strategy.physical_temperature_c;
    if (authority_smooth_first_write_ && std::isfinite(authority_last_fallback_value_c_)) {
      value_to_write = std::clamp(value_to_write, authority_last_fallback_value_c_ - 0.5f,
                                  authority_last_fallback_value_c_ + 0.5f);
    }
  } else if (std::isfinite(source.last_confirmed_value_c) && source.last_healthy_physical_ms != 0 &&
             esphome::millis() - source.last_healthy_physical_ms <= COVERAGE_GRACE_MS) {
    value_to_write = source.last_confirmed_value_c;
    std::strncpy(error, "coverage grace", sizeof(error) - 1);
  } else {
    std::strncpy(error, "coverage degraded", sizeof(error) - 1);
  }
  if (!std::isfinite(value_to_write)) {
    // No healthy or safely held physical signal; do not publish a partial-house average.
  } else if (!esphome::network::is_connected()) {
    std::strncpy(error, "network down", sizeof(error) - 1);
  } else {
    char url[448];
    const asgard_adapter::Config adapter_config = make_asgard_config_(source);
    if (!asgard_adapter::build_physical_write_url(adapter_config, value_to_write, url, sizeof(url))) {
      std::strncpy(error, "invalid endpoint", sizeof(error) - 1);
    } else {
    esp_http_client_config_t http_cfg{};
    http_cfg.url = url;
    http_cfg.method = HTTP_METHOD_POST;
    http_cfg.timeout_ms = HTTP_TIMEOUT_MS;
    http_cfg.disable_auto_redirect = true;
    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (client == nullptr) {
      std::strncpy(error, "http init failed", sizeof(error) - 1);
      confirmation_status = asgard_confirmation::Status::UNREACHABLE;
    } else {
      esp_http_client_set_post_field(client, "", 0);
      const esp_err_t err = esp_http_client_perform(client);
      const int status = err == ESP_OK ? esp_http_client_get_status_code(client) : 0;
      write_http_status = status;
      const bool write_ok = asgard_adapter::request_succeeded(err == ESP_OK, status);
      if (!write_ok) {
        confirmation_status = asgard_confirmation::Status::UNREACHABLE;
        if (err == ESP_OK)
          snprintf(error, sizeof(error), "http %d", status);
        else
          snprintf(error, sizeof(error), "http %s", esp_err_to_name(err));
      } else {
        confirmation_status = asgard_confirmation::Status::SENT;
        if (read_asgard_number_(source, &confirmed_value, error, sizeof(error))) {
          if (asgard_confirmation::values_match(value_to_write, confirmed_value)) {
            confirmation_status = asgard_confirmation::Status::CONFIRMED;
            ok = true;
            error[0] = '\0';
          } else {
            confirmation_status = asgard_confirmation::Status::MISMATCH;
            std::snprintf(error, sizeof(error), "requested %.2f, read %.2f",
                          value_to_write, confirmed_value);
          }
        }
      }
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
    }
    }
  }

  if (take_state_lock_(100)) {
    char previous_copy[12];
    std::strncpy(previous_copy, heat_source_.last_status, sizeof(previous_copy) - 1);
    previous_copy[sizeof(previous_copy) - 1] = '\0';
    heat_source_.has_last_push = true;
    std::strncpy(heat_source_.last_status, asgard_confirmation::status_name(confirmation_status),
                 sizeof(heat_source_.last_status) - 1);
    heat_source_.last_status[sizeof(heat_source_.last_status) - 1] = '\0';
    heat_source_.last_write_ms = esphome::millis();
    heat_source_.last_http_status = write_http_status;
    if (std::isfinite(value_to_write))
      heat_source_.last_requested_value_c = value_to_write;
    if (std::isfinite(confirmed_value))
      heat_source_.last_confirmed_value_c = confirmed_value;
    if (ok) {
      if (strategy.has_physical_temperature)
        heat_source_.last_healthy_physical_ms = esphome::millis();
      heat_source_.last_confirmed_ms = esphome::millis();
      heat_source_.failure_streak = 0;
      heat_source_.push_alarm = false;
      heat_source_.last_error[0] = '\0';
      authority_smooth_first_write_ = false;
    } else {
      // Coverage / config holds are intentional — do not treat as delivery failures.
      if (confirmation_status != asgard_confirmation::Status::BLOCKED) {
        if (heat_source_.failure_count < UINT32_MAX)
          heat_source_.failure_count++;
        if (heat_source_.failure_streak < UINT32_MAX)
          heat_source_.failure_streak++;
        heat_source_.push_alarm = heat_source_.failure_streak >= 2;
      }
      std::strncpy(heat_source_.last_error, error, sizeof(heat_source_.last_error) - 1);
      heat_source_.last_error[sizeof(heat_source_.last_error) - 1] = '\0';
    }
    const bool status_changed = std::strcmp(previous_copy, heat_source_.last_status) != 0;
    const bool alarm_transition =
        status_changed &&
        (confirmation_status == asgard_confirmation::Status::MISMATCH ||
         confirmation_status == asgard_confirmation::Status::UNREACHABLE ||
         (!ok && heat_source_.push_alarm));
    give_state_lock_();
    if (alarm_transition) {
      char event[128];
      std::snprintf(event, sizeof(event), "z1 push %s → %s (streak≥2 corrupts Odin HL learning)",
                    previous_copy[0] != '\0' ? previous_copy : "none",
                    asgard_confirmation::status_name(confirmation_status));
      log_event_("error", "heat_source", event);
    }
  }
  log_event_(ok ? "info" : "warn", "heat_source",
             ok ? "weighted temperature confirmed" : error);
  return ok;
}

bool LuneTouchCoordinator::poll_asgard_telemetry_() {
  char host[64]{};
  uint16_t port = 80;
  if (!take_state_lock_(100))
    return false;
  std::strncpy(host, heat_source_.host, sizeof(host) - 1);
  port = heat_source_.port == 0 ? 80 : heat_source_.port;
  give_state_lock_();
  if (host[0] == '\0')
    return false;
  static constexpr size_t CAP = 6144;
  char *body = alloc_psram_body_(CAP);
  if (body == nullptr)
    return false;
  char url[128];
  std::snprintf(url, sizeof(url), "http://%s:%u/dashboard/state", host, static_cast<unsigned>(port));
  int status = 0;
  bool ok = fetch_json_(url, body, CAP, &status);
  float feed = NAN, ret = NAN, outside = NAN, target = NAN, hz = NAN;
  bool comp = false;
  int mode = -1;
  if (ok) {
    JsonDocument filter;
    for (const char *k : {"hp_feed_temp", "hp_return_temp", "outside_temp", "z1_flow_temp_target",
                          "compressor_frequency", "status_compressor", "operation_mode"})
      filter[k] = true;
    JsonDocument doc;
    ok = !deserializeJson(doc, body, DeserializationOption::Filter(filter));
    if (ok) {
      auto f = [&](const char *k) { return doc[k].isNull() ? NAN : doc[k].as<float>(); };
      feed = f("hp_feed_temp");
      ret = f("hp_return_temp");
      outside = f("outside_temp");
      target = f("z1_flow_temp_target");
      hz = f("compressor_frequency");
      comp = doc["status_compressor"] | false;
      mode = doc["operation_mode"] | -1;
      ok = std::isfinite(feed) || std::isfinite(ret);
    }
  }
  heap_caps_free(body);
  if (ok && take_state_lock_(100)) {
    heat_source_.hp_feed_c = feed;
    heat_source_.hp_return_c = ret;
    heat_source_.hp_outside_c = outside;
    heat_source_.hp_flow_target_c = target;
    heat_source_.hp_compressor_hz = hz;
    heat_source_.hp_compressor_on = comp;
    heat_source_.hp_operation_mode = static_cast<int8_t>(mode);
    heat_source_.hp_telemetry_ms = esphome::millis();
    give_state_lock_();
  }
  return ok;
}

std::string LuneTouchCoordinator::display_heat_pump_temps_text() const {
  if (!take_state_lock_(50))
    return "";
  const HeatSourceState &h = heat_source_;
  const bool fresh = h.hp_telemetry_ms != 0 && esphome::millis() - h.hp_telemetry_ms < 300000UL;
  char buf[48] = "";
  if (fresh && std::isfinite(h.hp_feed_c) && std::isfinite(h.hp_return_c)) {
    std::snprintf(buf, sizeof(buf), "%.1f° → %.1f°", static_cast<double>(h.hp_feed_c),
                  static_cast<double>(h.hp_return_c));
    for (char *p = buf; *p != '\0'; ++p)
      if (*p == '.')
        *p = ',';
  }
  give_state_lock_();
  return buf;
}

bool LuneTouchCoordinator::poll_asgard_freeze_inputs_() {
  HeatSourceState source;
  if (!take_state_lock_(100))
    return false;
  source = heat_source_;
  give_state_lock_();
  if (source.host[0] == '\0')
    return false;

  auto read_entity = [&](const char *entity, bool *on, bool *known) {
    *known = false;
    *on = false;
    if (entity == nullptr || entity[0] == '\0')
      return;
    char url[448]{};
    if (!asgard_url::build_binary_sensor_url(source.host, source.port, entity, url, sizeof(url)))
      return;
    char body[384]{};
    int status = 0;
    if (!fetch_json_(url, body, sizeof(body), &status))
      return;
    if (asgard_adapter::parse_binary_state(body, on))
      *known = true;
  };

  bool dhw = false, legio = false, defrost = false;
  bool dhw_k = false, legio_k = false, defrost_k = false;
  read_entity(source.dhw_entity, &dhw, &dhw_k);
  read_entity(source.legionella_entity, &legio, &legio_k);
  read_entity(source.defrost_entity, &defrost, &defrost_k);

  if (take_state_lock_(100)) {
    heat_source_.dhw_active = dhw;
    heat_source_.legionella_active = legio;
    heat_source_.defrost_active = defrost;
    heat_source_.freeze_inputs_known = dhw_k && legio_k && defrost_k &&
                                       source.dhw_entity[0] != '\0' &&
                                       source.legionella_entity[0] != '\0' &&
                                       source.defrost_entity[0] != '\0';
    give_state_lock_();
  }
  return true;
}

bool LuneTouchCoordinator::sync_comfort_target_() {
  HeatSourceState source;
  ::lune_touch::StrategySnapshot strategy;
  if (!take_state_lock_(100))
    return false;
  source = heat_source_;
  strategy = model_.strategy_snapshot();
  if (time_ != nullptr) {
    auto now = time_->now();
    if (now.is_valid()) {
      const uint8_t day = static_cast<uint8_t>(now.day_of_week == 7 ? 0 : now.day_of_week);
      const uint16_t minute = static_cast<uint16_t>(now.hour * 60 + now.minute);
      strategy = model_.strategy_snapshot(true, day, minute);
    }
  }
  // Largest slab charge among rooms whose charge window is open now.
  float charge_store_c = 0.0f;
  for (size_t i = 0; i < forecast_decision_count_; i++)
    if (forecast_decisions_[i].charge_now)
      charge_store_c = std::max(charge_store_c, forecast_decisions_[i].charge_store_c);
  give_state_lock_();

  // The route does not depend on target sync: with Odin in control, demand
  // goes to Odin's schedule whether or not the VT target is synchronised.
  auto set_route = [&](bool target_sync_possible) {
    if (take_state_lock_(100)) {
      demand_route_ = odin_in_control_() ? (odin_control_.enabled ? "odin_schedule" : "none")
                                         : (target_sync_possible ? "virtual_thermostat" : "none");
      if (!target_sync_possible)
        demand_target_uplift_c_ = 0.0f;
      give_state_lock_();
    }
  };
  if (!source.target_sync_enabled || source.climate_entity[0] == '\0') {
    update_demand_hold_(strategy, charge_store_c);
    set_route(false);
    return false;
  }
  if (!strategy.has_house_target || !std::isfinite(strategy.house_target_c)) {
    update_demand_hold_(strategy, charge_store_c);
    set_route(false);
    return false;
  }

  // Demand-led target: keep the virtual thermostat calling while the room that
  // lags most is behind, or a slab-charge window is open — even when a warm
  // floor has pulled the weighted average to target (house_demand.h).
  // With Odin in control the VT target does not drive heating (Odin plans from
  // its own comfort schedule), so the real target is written unlifted and the
  // demand goes to Odin's schedule instead (run_odin_control_).
  const float physical = strategy.has_physical_temperature ? strategy.physical_temperature_c : NAN;
  const float held = update_demand_hold_(strategy, charge_store_c);
  bool odin_route = false;
  bool odin_schedule = false;
  if (take_state_lock_(100)) {
    odin_route = odin_in_control_();
    odin_schedule = odin_control_.enabled;
    give_state_lock_();
  }
  const float led = odin_route ? strategy.house_target_c
                               : lune_touch_house::demand_led_target(strategy.house_target_c, physical,
                                                                     held, charge_store_c);
  if (take_state_lock_(100)) {
    demand_target_uplift_c_ = std::isfinite(led) ? led - strategy.house_target_c : 0.0f;
    demand_route_ = odin_route ? (odin_schedule ? "odin_schedule" : "none") : "virtual_thermostat";
    give_state_lock_();
  }
  const float target = asgard_adapter::round_tenths(led);
  if (std::isfinite(source.last_target_confirmed_c) &&
      std::fabs(source.last_target_confirmed_c - target) < 0.05f)
    return true;

  const asgard_adapter::Config cfg = make_asgard_config_(source);
  char url[448]{};
  if (!asgard_adapter::build_climate_target_write_url(cfg, target, url, sizeof(url)))
    return false;
  esp_http_client_config_t http_cfg{};
  http_cfg.url = url;
  http_cfg.method = HTTP_METHOD_POST;
  http_cfg.timeout_ms = HTTP_TIMEOUT_MS;
  http_cfg.disable_auto_redirect = true;
  esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
  if (client == nullptr)
    return false;
  esp_http_client_set_post_field(client, "", 0);
  const esp_err_t err = esp_http_client_perform(client);
  const int status = err == ESP_OK ? esp_http_client_get_status_code(client) : 0;
  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  bool ok = asgard_adapter::request_succeeded(err == ESP_OK, status);
  float confirmed = NAN;
  if (ok) {
    char read_url[448]{};
    if (asgard_adapter::build_climate_target_read_url(cfg, read_url, sizeof(read_url))) {
      char body[384]{};
      int read_status = 0;
      if (fetch_json_(read_url, body, sizeof(body), &read_status))
        asgard_adapter::parse_climate_target(body, &confirmed);
    }
    if (std::isfinite(confirmed) && std::fabs(confirmed - target) > 0.15f)
      ok = false;
  }
  if (take_state_lock_(100)) {
    heat_source_.last_target_written_c = target;
    heat_source_.last_target_write_ms = esphome::millis();
    if (ok) {
      heat_source_.last_target_confirmed_c = std::isfinite(confirmed) ? confirmed : target;
      heat_source_.target_failure_streak = 0;
    } else {
      heat_source_.target_failure_streak++;
    }
    give_state_lock_();
  }
  log_event_(ok ? "info" : "warn", "heat_source",
             ok ? "comfort target confirmed" : "comfort target sync failed");
  return ok;
}

// ---------------------------------------------------------------------------
// House demand routing: hold, Odin comfort schedule, generic levers
// ---------------------------------------------------------------------------

namespace {

bool http_post_body_(const char *url, const char *body, const char *content_type, uint32_t timeout_ms,
                     int *status_out) {
  esp_http_client_config_t cfg{};
  cfg.url = url;
  cfg.method = HTTP_METHOD_POST;
  cfg.timeout_ms = timeout_ms;
  cfg.disable_auto_redirect = true;
  esp_http_client_handle_t client = esp_http_client_init(&cfg);
  if (client == nullptr)
    return false;
  if (content_type != nullptr)
    esp_http_client_set_header(client, "Content-Type", content_type);
  esp_http_client_set_post_field(client, body != nullptr ? body : "",
                                 body != nullptr ? static_cast<int>(std::strlen(body)) : 0);
  const esp_err_t err = esp_http_client_perform(client);
  const int status = err == ESP_OK ? esp_http_client_get_status_code(client) : 0;
  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  if (status_out != nullptr)
    *status_out = status;
  return err == ESP_OK && status >= 200 && status < 300;
}

// Odin's /dashboard/set takes the schedule as a JSON *string* value.
size_t json_string_literal_(const char *src, char *out, size_t cap) {
  if (out == nullptr || cap < 3 || src == nullptr)
    return 0;
  size_t off = 0;
  out[off++] = '"';
  for (const char *p = src; *p != '\0'; ++p) {
    const bool esc = *p == '"' || *p == '\\';
    if (off + (esc ? 2 : 1) + 2 > cap)
      return 0;
    if (esc)
      out[off++] = '\\';
    out[off++] = *p;
  }
  out[off++] = '"';
  out[off] = '\0';
  return off;
}

}  // namespace

float LuneTouchCoordinator::update_demand_hold_(const ::lune_touch::StrategySnapshot &strategy,
                                                float charge_store_c) {
  (void) charge_store_c;  // charge windows are already time-boxed; only the lag is held
  const float required =
      strategy.has_physical_temperature ? lune_touch_house::required_lift_c(strategy.driver_deficit_c, 0.0f)
                                        : 0.0f;
  float held = 0.0f;
  if (take_state_lock_(100)) {
    held = demand_hold_.update(required, esphome::millis());
    demand_held_c_ = held;
    give_state_lock_();
  }
  return held;
}

bool LuneTouchCoordinator::odin_in_control_() const {
  // Caller holds the state lock. Odin decides heating when Touch drives its
  // schedule, or when Asgard reports that it forwards to Odin.
  return odin_control_.enabled || (odin_control_.forwarder_known && odin_control_.forwarder_active);
}

lune_touch_odin::Lift LuneTouchCoordinator::compute_odin_lift_(uint8_t now_hour, float *energy_kwh,
                                                               const char **reason) {
  lune_touch_odin::LiftParams params{};
  float thermal_mass = NAN;
  float held = 0.0f;
  float energy = 0.0f;
  int start = INT16_MAX;
  int end = -1;
  if (take_state_lock_(100)) {
    params.max_lift_c = odin_control_.max_lift_c;
    thermal_mass = odin_control_.thermal_mass_kwh_per_k;
    if (!std::isfinite(thermal_mass) || thermal_mass <= 0.5f) {
      const auto strategy = model_.strategy_snapshot();
      thermal_mass = strategy.thermal_mass_total_kwh_per_k;
    }
    held = demand_held_c_;
    // Slab charge: sum the shortfall of each room once (a room may span zones).
    for (size_t i = 0; i < forecast_decision_count_; i++) {
      const auto &d = forecast_decisions_[i];
      if (!d.charge_episode || d.charge_end_in_h <= 0)
        continue;
      bool seen = false;
      for (size_t j = 0; j < i && !seen; j++)
        seen = forecast_decisions_[j].charge_episode && forecast_decisions_[j].charge_end_in_h > 0 &&
               std::strcmp(forecast_decisions_[j].room_id, d.room_id) == 0;
      if (!seen)
        energy += d.charge_deficit_kwh;
      start = std::min<int>(start, d.charge_start_in_h);
      end = std::max<int>(end, d.charge_end_in_h);
    }
    give_state_lock_();
  }
  lune_touch_odin::Lift charge{};
  if (end > 0)
    charge = lune_touch_odin::make_lift(now_hour, start, end,
                                        lune_touch_odin::lift_for_energy(energy, thermal_mass, params),
                                        params);
  // A room that lags (held demand): shift the band for the next two hours so
  // Odin's next solve plans heat now. Re-evaluated every cycle.
  lune_touch_odin::Lift lag{};
  if (held > 0.0f)
    lag = lune_touch_odin::make_lift(now_hour, 0, 2, held, params);
  if (energy_kwh != nullptr)
    *energy_kwh = charge.active() ? energy : 0.0f;
  if (charge.active() && lag.active()) {
    if (reason != nullptr)
      *reason = "slab_charge+room_lag";
    // Charge windows start now or later; the lag starts now. Merge from now.
    const int charge_end_rel = static_cast<int>((charge.start_hour + 24 - now_hour) % 24) + charge.hours;
    return lune_touch_odin::make_lift(now_hour, 0, std::max(2, charge_end_rel),
                                      std::max(charge.lift_c, lag.lift_c), params);
  }
  if (charge.active()) {
    if (reason != nullptr)
      *reason = "slab_charge";
    return charge;
  }
  if (lag.active()) {
    if (reason != nullptr)
      *reason = "room_lag";
    return lag;
  }
  if (reason != nullptr)
    *reason = "none";
  return lune_touch_odin::Lift{};
}

bool LuneTouchCoordinator::post_odin_json_(const char *path, const char *body, int *status) {
  char host[64]{};
  uint16_t port = 80;
  if (!take_state_lock_(100))
    return false;
  std::strncpy(host, odin_plan_.odin_host, sizeof(host) - 1);
  port = odin_plan_.odin_port == 0 ? 80 : odin_plan_.odin_port;
  give_state_lock_();
  if (host[0] == '\0')
    return false;
  char url[192];
  std::snprintf(url, sizeof(url), "http://%s:%u%s", host, static_cast<unsigned>(port), path);
  return http_post_body_(url, body, "application/json", 8000, status);
}

void LuneTouchCoordinator::run_odin_control_() {
  static constexpr uint32_t CYCLE_MS = 240000UL;           // ≥4 min between cycles
  static constexpr uint32_t PHYSICS_MS = 30UL * 60000UL;   // Odin's physics change slowly
  static constexpr uint32_t MIN_LIFT_WRITE_MS = 20UL * 60000UL;
  static constexpr int32_t TELEMETRY_STALE_S = 900;
  static constexpr uint32_t LINK_ALARM_MS = 10UL * 60000UL;

  char odin_host[64]{};
  uint16_t odin_port = 80;
  char asgard_host[64]{};
  uint16_t asgard_port = 80;
  char forwarder_entity[64]{};
  bool asgard = false;
  bool enabled = false;
  uint32_t last_physics_ms = 0;
  uint32_t last_write_ms = 0;
  const uint32_t now_ms = esphome::millis();
  if (!take_state_lock_(100))
    return;
  if (odin_control_.last_cycle_ms != 0 && now_ms - odin_control_.last_cycle_ms < CYCLE_MS) {
    give_state_lock_();
    return;
  }
  odin_control_.last_cycle_ms = now_ms;
  std::strncpy(odin_host, odin_plan_.odin_host, sizeof(odin_host) - 1);
  odin_port = odin_plan_.odin_port == 0 ? 80 : odin_plan_.odin_port;
  asgard = heat_source_.enabled && heat_source_type_is_asgard_(heat_source_.type) &&
           heat_source_.host[0] != '\0';
  std::strncpy(asgard_host, heat_source_.host, sizeof(asgard_host) - 1);
  asgard_port = heat_source_.port == 0 ? 80 : heat_source_.port;
  std::strncpy(forwarder_entity, odin_control_.forwarder_entity, sizeof(forwarder_entity) - 1);
  enabled = odin_control_.enabled;
  last_physics_ms = odin_control_.last_physics_ms;
  last_write_ms = odin_control_.last_write_ms;
  give_state_lock_();
  if (odin_host[0] == '\0')
    return;

  // 1. Asgard → Odin forwarder (the link Odin learns through).
  bool forwarder_known = false;
  bool forwarder_active = false;
  if (asgard && forwarder_entity[0] != '\0') {
    char url[256]{};
    if (asgard_url::build_binary_sensor_url(asgard_host, asgard_port, forwarder_entity, url, sizeof(url))) {
      char body[384]{};
      int status = 0;
      if (fetch_json_(url, body, sizeof(body), &status))
        forwarder_known = asgard_adapter::parse_binary_state(body, &forwarder_active);
    }
  }

  // 1b. Who drives the pump right now: Odin (takeover) or Asgard itself.
  bool takeover_known = false;
  bool takeover_active = false;
  char takeover_entity[64]{};
  if (take_state_lock_(100)) {
    std::strncpy(takeover_entity, odin_control_.takeover_entity, sizeof(takeover_entity) - 1);
    give_state_lock_();
  }
  if (asgard && takeover_entity[0] != '\0') {
    char turl[256]{};
    if (asgard_url::build_binary_sensor_url(asgard_host, asgard_port, takeover_entity, turl, sizeof(turl))) {
      char tbody[384]{};
      int tstatus = 0;
      if (fetch_json_(turl, tbody, sizeof(tbody), &tstatus))
        takeover_known = asgard_adapter::parse_binary_state(tbody, &takeover_active);
    }
  }

  // 2. Odin state: telemetry age, room temperature and the comfort schedule.
  static constexpr size_t STATE_CAP = 12288;
  char *body = alloc_psram_body_(STATE_CAP);
  if (body == nullptr)
    return;
  char url[192];
  std::snprintf(url, sizeof(url), "http://%s:%u/dashboard/state", odin_host, static_cast<unsigned>(odin_port));
  int status = 0;
  const bool reachable = fetch_json_(url, body, STATE_CAP, &status, 6000);
  int32_t telemetry_age_s = -1;
  float odin_room = NAN;
  bool have_schedule = false;
  lune_touch_odin::Profile remote{};
  if (reachable) {
    JsonDocument filter;
    filter["telemetry_ts"] = true;
    filter["room_z1_current"] = true;
    filter["sched_ui"] = true;
    JsonDocument doc;
    if (!deserializeJson(doc, body, DeserializationOption::Filter(filter))) {
      const double ts = doc["telemetry_ts"] | 0.0;
      const int64_t now_epoch = current_epoch_s_();
      if (ts > 0.0 && now_epoch > 0)
        telemetry_age_s = static_cast<int32_t>(std::max<int64_t>(0, now_epoch - static_cast<int64_t>(ts)));
      if (!doc["room_z1_current"].isNull())
        odin_room = doc["room_z1_current"].as<float>();
      const char *sched = doc["sched_ui"] | "";
      have_schedule = lune_touch_odin::parse_profile(sched, &remote);
    }
  }

  // 3. Physics Odin plans with (thermal mass → °C per kWh of lift).
  float thermal_mass = NAN;
  float heat_loss = NAN;
  bool physics_fetched = false;
  if (reachable && (last_physics_ms == 0 || now_ms - last_physics_ms >= PHYSICS_MS)) {
    std::snprintf(url, sizeof(url), "http://%s:%u/api/debug", odin_host, static_cast<unsigned>(odin_port));
    int dstatus = 0;
    if (fetch_json_(url, body, STATE_CAP, &dstatus, 6000)) {
      JsonDocument filter;
      filter["latest_response"]["used_thermal_mass"] = true;
      filter["latest_response"]["used_heat_loss"] = true;
      JsonDocument doc;
      if (!deserializeJson(doc, body, DeserializationOption::Filter(filter))) {
        thermal_mass = doc["latest_response"]["used_thermal_mass"] | NAN;
        heat_loss = doc["latest_response"]["used_heat_loss"] | NAN;
        physics_fetched = true;
      }
    }
  }
  heap_caps_free(body);

  // 4. Link health. With MQTT enabled, the pump telemetry stream (Asgard →
  // broker) is a second, independent freshness signal next to Odin's own
  // telemetry_ts: the effective age is the fresher of the two, and a dead
  // stream on a connected client is caught even when Odin's HTTP is fine.
  int32_t mqtt_age_s = -1;
  if (take_state_lock_(100)) {
    const auto &age = odin_mqtt_client_.ages[static_cast<size_t>(odin_mqtt::StreamId::Telemetry)];
    if (odin_mqtt_.enabled && odin_mqtt_client_.connected && age.ever_seen) {
      // A message may arrive (MQTT task) after now_ms was taken: clamp at 0.
      const int32_t delta = static_cast<int32_t>(now_ms - age.last_message_ms);
      mqtt_age_s = delta > 0 ? delta / 1000 : 0;
    }
    give_state_lock_();
  }
  if (mqtt_age_s >= 0 && (telemetry_age_s < 0 || mqtt_age_s < telemetry_age_s))
    telemetry_age_s = mqtt_age_s;
  const char *link = "ok";
  if (!reachable)
    link = "odin_unreachable";
  else if (forwarder_known && !forwarder_active)
    link = "forwarder_off";
  else if (telemetry_age_s > TELEMETRY_STALE_S)
    link = "telemetry_stale";
  else if (!std::isfinite(odin_room))
    link = "no_room_temperature";
  bool raise_alarm = false;
  bool clear_alarm = false;
  char alarm_text[96]{};
  if (take_state_lock_(100)) {
    odin_control_.odin_reachable = reachable;
    odin_control_.forwarder_known = forwarder_known;
    odin_control_.forwarder_active = forwarder_active;
    odin_control_.takeover_known = takeover_known;
    odin_control_.takeover_active = takeover_active;
    odin_control_.telemetry_age_s = telemetry_age_s;
    odin_control_.mqtt_telemetry_age_s = mqtt_age_s;
    odin_control_.odin_room_c = odin_room;
    odin_control_.last_http_status = status;
    std::strncpy(odin_control_.link_status, link, sizeof(odin_control_.link_status) - 1);
    if (std::strcmp(link, "ok") == 0) {
      clear_alarm = odin_control_.link_alarm;
      odin_control_.link_alarm = false;
      odin_control_.link_bad_since_ms = 0;
    } else {
      if (odin_control_.link_bad_since_ms == 0)
        odin_control_.link_bad_since_ms = now_ms;
      if (!odin_control_.link_alarm && now_ms - odin_control_.link_bad_since_ms >= LINK_ALARM_MS) {
        odin_control_.link_alarm = true;
        raise_alarm = true;
        std::snprintf(alarm_text, sizeof(alarm_text), "Odin link: %s (Odin is not learning)", link);
      }
    }
    if (physics_fetched) {
      odin_control_.last_physics_ms = now_ms;
      if (std::isfinite(thermal_mass) && thermal_mass > 0.0f)
        odin_control_.thermal_mass_kwh_per_k = thermal_mass;
      if (std::isfinite(heat_loss) && heat_loss > 0.0f)
        odin_control_.heat_loss_kw_per_k = heat_loss;
    }
    give_state_lock_();
  }
  if (raise_alarm)
    log_event_("warn", "odin", alarm_text);
  if (clear_alarm)
    log_event_("info", "odin", "Odin link restored");

  // 5. Comfort-schedule lift.
  uint8_t now_hour = 0;
  bool clock_ok = false;
  if (time_ != nullptr) {
    auto now = time_->now();
    if (now.is_valid()) {
      now_hour = static_cast<uint8_t>(now.hour);
      clock_ok = true;
    }
  }
  float energy = 0.0f;
  const char *reason = "none";
  lune_touch_odin::Lift want{};
  if (enabled && clock_ok)
    want = compute_odin_lift_(now_hour, &energy, &reason);

  lune_touch_odin::OwnerState owner{};
  if (!take_state_lock_(100))
    return;
  owner = odin_owner_;
  odin_control_.wanted = want;
  odin_control_.wanted_energy_kwh = energy;
  std::strncpy(odin_control_.reason, reason, sizeof(odin_control_.reason) - 1);
  give_state_lock_();

  const char *control_status = "idle";
  if (!enabled && !owner.applied) {
    control_status = "disabled";
  } else if (!reachable) {
    control_status = "odin_unreachable";
  } else if (!have_schedule) {
    control_status = "schedule_unreadable";
  } else if (enabled && !clock_ok) {
    control_status = "clock_invalid";
  }
  if (std::strcmp(control_status, "idle") != 0) {
    if (take_state_lock_(100)) {
      std::strncpy(odin_control_.status, control_status, sizeof(odin_control_.status) - 1);
      // Transient right after boot (SNTP not synced yet) or a short Odin
      // outage: retry in ~30 s instead of waiting a full 5 min cycle.
      if (std::strcmp(control_status, "disabled") != 0)
        odin_control_.last_cycle_ms = now_ms - 270000UL;
      give_state_lock_();
    }
    return;
  }

  // Disabled while a lift is on Odin → want nothing, so the step restores.
  const lune_touch_odin::StepResult r = lune_touch_odin::step(owner, remote, enabled ? want : lune_touch_odin::Lift{});
  bool wrote = false;
  bool write_ok = false;
  int write_status = 0;
  char error[96]{};
  const bool rate_limited = r.action == lune_touch_odin::Action::WRITE_LIFT && last_write_ms != 0 &&
                            now_ms - last_write_ms < MIN_LIFT_WRITE_MS && owner.applied;
  if ((r.action == lune_touch_odin::Action::WRITE_LIFT || r.action == lune_touch_odin::Action::RESTORE) &&
      !rate_limited) {
    // Off the odin task stack (only this task writes Odin's schedule).
    static char *sched = psram_scratch_<char>(640);
    static char *literal = psram_scratch_<char>(768);
    static char *payload = psram_scratch_<char>(896);
    if (sched == nullptr || literal == nullptr || payload == nullptr ||
        lune_touch_odin::format_profile(r.profile, sched, 640) == 0 ||
        json_string_literal_(sched, literal, 768) == 0) {
      std::strncpy(error, "schedule too large", sizeof(error) - 1);
    } else {
      std::snprintf(payload, 896, "{\"key\":\"sched_ui\",\"value\":%s}", literal);
      wrote = true;
      write_ok = post_odin_json_("/dashboard/set", payload, &write_status);
      if (write_ok) {
        int solve_status = 0;
        post_odin_json_("/api/solve", "{}", &solve_status);  // re-plan now, not at hh:59
      } else {
        std::snprintf(error, sizeof(error), write_status > 0 ? "set http %d" : "set unreachable",
                      write_status);
      }
    }
  }
  if (write_ok)
    lune_touch_odin::commit(owner, r, want);

  const bool persist = write_ok || r.action == lune_touch_odin::Action::ADOPT_USER ||
                       r.action == lune_touch_odin::Action::YIELD;
  if (take_state_lock_(100)) {
    odin_owner_ = owner;
    odin_control_.applied = owner.applied ? owner.lift : lune_touch_odin::Lift{};
    if (wrote) {
      odin_control_.last_http_status = write_status;
      if (write_ok)
        odin_control_.last_write_ms = now_ms;
    }
    std::strncpy(odin_control_.last_action, lune_touch_odin::action_name(r.action),
                 sizeof(odin_control_.last_action) - 1);
    const char *st = owner.yielded       ? "yielded"
                     : rate_limited      ? "rate_limited"
                     : (wrote && !write_ok) ? "write_failed"
                     : owner.applied     ? "lifted"
                     : enabled           ? "watching"
                                         : "disabled";
    std::strncpy(odin_control_.status, st, sizeof(odin_control_.status) - 1);
    std::strncpy(odin_control_.last_error, error, sizeof(odin_control_.last_error) - 1);
    give_state_lock_();
  }
  if (persist)
    save_odin_control_();
  if (write_ok && r.action == lune_touch_odin::Action::WRITE_LIFT) {
    char msg[96];
    std::snprintf(msg, sizeof(msg), "Odin comfort +%.1f C %02u:00 for %u h (%s)",
                  static_cast<double>(want.lift_c), static_cast<unsigned>(want.start_hour),
                  static_cast<unsigned>(want.hours), reason);
    log_event_("info", "odin", msg);
  } else if (write_ok && r.action == lune_touch_odin::Action::RESTORE) {
    log_event_("info", "odin", "Odin comfort schedule restored");
  } else if (r.action == lune_touch_odin::Action::YIELD) {
    log_event_("warn", "odin", "Odin schedule edited by user; Touch lift paused");
  } else if (wrote && !write_ok) {
    log_event_("warn", "odin", "Odin schedule write failed");
  }
}

void LuneTouchCoordinator::load_odin_control_() {
  nvs_handle_t handle;
  if (nvs_open(SETTINGS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK)
    return;
  uint8_t u8 = 0;
  if (nvs_get_u8(handle, "odc_en", &u8) == ESP_OK)
    odin_control_.enabled = u8 != 0;
  float f = NAN;
  size_t blob_len = sizeof(f);
  if (nvs_get_blob(handle, "odc_maxlift", &f, &blob_len) == ESP_OK && std::isfinite(f))
    odin_control_.max_lift_c = std::clamp(f, 0.3f, 3.0f);
  size_t len = sizeof(odin_control_.forwarder_entity);
  nvs_get_str(handle, "odc_fwd_ent", odin_control_.forwarder_entity, &len);
  char *sched = psram_scratch_<char>(640);
  if (sched == nullptr) {
    nvs_close(handle);
    return;
  }
  len = 640;
  if (nvs_get_str(handle, "odc_user", sched, &len) == ESP_OK)
    odin_owner_.has_user = lune_touch_odin::parse_profile(sched, &odin_owner_.user);
  len = 640;
  if (nvs_get_str(handle, "odc_written", sched, &len) == ESP_OK &&
      lune_touch_odin::parse_profile(sched, &odin_owner_.written)) {
    uint8_t applied = 0;
    if (nvs_get_u8(handle, "odc_applied", &applied) == ESP_OK)
      odin_owner_.applied = applied != 0;
  }
  uint8_t lift[3]{};
  len = sizeof(lift);
  if (nvs_get_blob(handle, "odc_lift", lift, &len) == ESP_OK && len == sizeof(lift)) {
    odin_owner_.lift.start_hour = lift[0] % 24;
    odin_owner_.lift.hours = lift[1];
    odin_owner_.lift.lift_c = static_cast<float>(lift[2]) / 10.0f;
  }
  if (nvs_get_u8(handle, "odc_yield", &u8) == ESP_OK)
    odin_owner_.yielded = u8 != 0;
  odin_control_.applied = odin_owner_.applied ? odin_owner_.lift : lune_touch_odin::Lift{};
  // Generic levers.
  len = sizeof(generic_levers_.target_url_template);
  nvs_get_str(handle, "gl_turl", generic_levers_.target_url_template, &len);
  len = sizeof(generic_levers_.heat_request_url_template);
  nvs_get_str(handle, "gl_rurl", generic_levers_.heat_request_url_template, &len);
  len = sizeof(generic_levers_.curve_offset_url_template);
  nvs_get_str(handle, "gl_curl", generic_levers_.curve_offset_url_template, &len);
  float curve[2]{};
  blob_len = sizeof(curve);
  if (nvs_get_blob(handle, "gl_curve", curve, &blob_len) == ESP_OK && blob_len == sizeof(curve)) {
    if (std::isfinite(curve[0]))
      generic_levers_.curve_gain = std::clamp(curve[0], 0.0f, 10.0f);
    if (std::isfinite(curve[1]))
      generic_levers_.curve_max_offset_c = std::clamp(curve[1], 0.0f, 15.0f);
  }
  heap_caps_free(sched);
  nvs_close(handle);
}

void LuneTouchCoordinator::save_odin_control_() {
  OdinControlState control{};
  lune_touch_odin::OwnerState owner{};
  GenericLeverState levers{};
  if (!take_state_lock_(100))
    return;
  control = odin_control_;
  owner = odin_owner_;
  levers = generic_levers_;
  give_state_lock_();
  static char *user = psram_scratch_<char>(640);
  static char *written = psram_scratch_<char>(640);
  if (user == nullptr || written == nullptr)
    return;
  user[0] = '\0';
  written[0] = '\0';
  if (owner.has_user)
    lune_touch_odin::format_profile(owner.user, user, 640);
  if (owner.applied)
    lune_touch_odin::format_profile(owner.written, written, 640);
  const uint8_t lift[3]{owner.lift.start_hour, owner.lift.hours,
                        static_cast<uint8_t>(std::clamp(std::lround(owner.lift.lift_c * 10.0f), 0L, 255L))};
  const float curve[2]{levers.curve_gain, levers.curve_max_offset_c};
  nvs_handle_t handle;
  if (nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK)
    return;
  esp_err_t err = nvs_set_u8(handle, "odc_en", control.enabled ? 1 : 0);
  if (err == ESP_OK) err = nvs_set_blob(handle, "odc_maxlift", &control.max_lift_c, sizeof(float));
  if (err == ESP_OK) err = nvs_set_str(handle, "odc_fwd_ent", control.forwarder_entity);
  if (err == ESP_OK) err = nvs_set_str(handle, "odc_user", user);
  if (err == ESP_OK) err = nvs_set_str(handle, "odc_written", written);
  if (err == ESP_OK) err = nvs_set_u8(handle, "odc_applied", owner.applied ? 1 : 0);
  if (err == ESP_OK) err = nvs_set_blob(handle, "odc_lift", lift, sizeof(lift));
  if (err == ESP_OK) err = nvs_set_u8(handle, "odc_yield", owner.yielded ? 1 : 0);
  if (err == ESP_OK) err = nvs_set_str(handle, "gl_turl", levers.target_url_template);
  if (err == ESP_OK) err = nvs_set_str(handle, "gl_rurl", levers.heat_request_url_template);
  if (err == ESP_OK) err = nvs_set_str(handle, "gl_curl", levers.curve_offset_url_template);
  if (err == ESP_OK) err = nvs_set_blob(handle, "gl_curve", curve, sizeof(curve));
  if (err == ESP_OK)
    err = nvs_commit(handle);
  nvs_close(handle);
  if (err != ESP_OK)
    ESP_LOGW(TAG, "odin control settings not saved: %s", esp_err_to_name(err));
}

bool LuneTouchCoordinator::push_generic_levers_() {
  static constexpr uint32_t REFRESH_MS = 10UL * 60000UL;
  HeatSourceState source{};
  GenericLeverState levers{};
  ::lune_touch::StrategySnapshot strategy;
  float charge_store_c = 0.0f;
  if (!take_state_lock_(100))
    return false;
  source = heat_source_;
  levers = generic_levers_;
  strategy = model_.strategy_snapshot();
  for (size_t i = 0; i < forecast_decision_count_; i++)
    if (forecast_decisions_[i].charge_now)
      charge_store_c = std::max(charge_store_c, forecast_decisions_[i].charge_store_c);
  give_state_lock_();
  const float held = update_demand_hold_(strategy, charge_store_c);
  if (!source.enabled || source.host[0] == '\0')
    return false;
  const bool any = levers.target_url_template[0] != '\0' || levers.heat_request_url_template[0] != '\0' ||
                   levers.curve_offset_url_template[0] != '\0';
  if (!any)
    return false;
  if (!strategy.has_house_target || !strategy.has_physical_temperature) {
    if (take_state_lock_(100)) {
      std::strncpy(generic_levers_.lever_status, "no_house_signal", sizeof(generic_levers_.lever_status) - 1);
      give_state_lock_();
    }
    return false;
  }
  lune_touch_house::LeverParams lp{};
  lp.curve_gain = levers.curve_gain;
  lp.curve_max_offset_c = levers.curve_max_offset_c;
  // Charge windows count as demand too (max with the held lag).
  const float demand = std::max(held, charge_store_c);
  const auto out = lune_touch_house::generic_levers(strategy.house_target_c, strategy.physical_temperature_c,
                                                     demand, lp);
  const uint32_t now_ms = esphome::millis();
  const bool refresh = levers.last_lever_write_ms == 0 || now_ms - levers.last_lever_write_ms >= REFRESH_MS;
  bool all_ok = true;
  bool wrote = false;
  auto write = [&](const char *tmpl, float value) {
    char url[448]{};
    if (!asgard_url::expand_url_template(tmpl, source.host, source.port == 0 ? 80 : source.port, "", value,
                                         true, url, sizeof(url)))
      return false;
    int status = 0;
    wrote = true;
    return http_post_body_(url, "", nullptr, HTTP_TIMEOUT_MS, &status);
  };
  const float target = asgard_adapter::round_tenths(out.target_c);
  if (levers.target_url_template[0] != '\0' && std::isfinite(target) &&
      (refresh || !std::isfinite(levers.last_lever_target_c) || std::fabs(levers.last_lever_target_c - target) >= 0.05f)) {
    if (write(levers.target_url_template, target))
      levers.last_lever_target_c = target;
    else
      all_ok = false;
  }
  const int8_t request = out.heat_request ? 1 : 0;
  if (levers.heat_request_url_template[0] != '\0' && (refresh || levers.last_lever_request != request)) {
    if (write(levers.heat_request_url_template, static_cast<float>(request)))
      levers.last_lever_request = request;
    else
      all_ok = false;
  }
  if (levers.curve_offset_url_template[0] != '\0' &&
      (refresh || !std::isfinite(levers.last_lever_curve_c) ||
       std::fabs(levers.last_lever_curve_c - out.curve_offset_c) >= 0.1f)) {
    if (write(levers.curve_offset_url_template, out.curve_offset_c))
      levers.last_lever_curve_c = out.curve_offset_c;
    else
      all_ok = false;
  }
  if (take_state_lock_(100)) {
    generic_levers_.last_lever_target_c = levers.last_lever_target_c;
    generic_levers_.last_lever_request = levers.last_lever_request;
    generic_levers_.last_lever_curve_c = levers.last_lever_curve_c;
    if (wrote && all_ok)
      generic_levers_.last_lever_write_ms = now_ms;
    std::strncpy(generic_levers_.lever_status, !wrote ? "unchanged" : all_ok ? "written" : "write_failed",
                 sizeof(generic_levers_.lever_status) - 1);
    demand_target_uplift_c_ = std::isfinite(out.target_c) ? out.target_c - strategy.house_target_c : 0.0f;
    demand_route_ = "generic";
    give_state_lock_();
  }
  if (wrote && !all_ok)
    log_event_("warn", "heat_source", "generic heat-source lever write failed");
  return all_ok;
}

bool LuneTouchCoordinator::step_flow_trim_() {
  using namespace flow_trim;
  HeatSourceState source;
  ::lune_touch::StrategySnapshot strategy;
  OdinPlanState odin;
  uint32_t now_ms = 0;
  bool lease_held = false;
  ::lune_touch::HeatRecommendation room_recs[::lune_touch::MAX_HOUSE_ROOMS]{};
  bool room_fresh[::lune_touch::MAX_HOUSE_ROOMS]{};
  size_t room_count = 0;
  bool forecast_on_critical = false;

  if (!take_state_lock_(100))
    return false;
  source = heat_source_;
  strategy = model_.strategy_snapshot();
  odin = odin_plan_;
  now_ms = esphome::millis();
  lease_held = authority_expires_at_ms_ != 0 &&
               static_cast<int32_t>(now_ms - authority_expires_at_ms_) < 0;
  room_count = model_.room_count();
  for (size_t i = 0; i < room_count && i < ::lune_touch::MAX_HOUSE_ROOMS; i++) {
    const auto *room = model_.room(i);
    if (room == nullptr || !room->include_in_house_temperature) {
      room_fresh[i] = false;
      continue;
    }
    const auto demand = model_.room_heat_demand(room->room_id);
    room_recs[i] = demand.recommendation;
    room_fresh[i] = demand.any_fresh;
  }
  for (size_t n = 0; n < model_.node_count(); n++) {
    const auto *nd = model_.node_heat_demand(n);
    if (nd == nullptr || !nd->fresh || nd->critical_zone == 0)
      continue;
    const auto resolved = ledger_.resolve_command_offset(
        static_cast<uint8_t>(n), static_cast<uint8_t>(nd->critical_zone - 1), now_ms);
    if (resolved.has_forecast_offset) {
      forecast_on_critical = true;
      break;
    }
  }
  give_state_lock_();

  if (!lease_held)
    return false;

  FreezeInputs freeze{};
  freeze.dhw = source.freeze_inputs_known
                   ? (source.dhw_active ? FreezeInputs::Tri::TRUE : FreezeInputs::Tri::FALSE)
                   : FreezeInputs::Tri::UNKNOWN;
  freeze.legionella =
      source.freeze_inputs_known
          ? (source.legionella_active ? FreezeInputs::Tri::TRUE : FreezeInputs::Tri::FALSE)
          : FreezeInputs::Tri::UNKNOWN;
  freeze.defrost =
      source.freeze_inputs_known
          ? (source.defrost_active ? FreezeInputs::Tri::TRUE : FreezeInputs::Tri::FALSE)
          : FreezeInputs::Tri::UNKNOWN;
  freeze.odin_plan_available = odin.enabled && odin.available && !odin.plan_stale;
  freeze.odin_heat_off = freeze.odin_plan_available && odin.current_operation_mode_raw == 0;
  freeze.house_quality_healthy = std::strcmp(strategy.quality, "healthy") == 0;
  freeze.forecast_offset_on_critical = forecast_on_critical;

  HouseDemand demand{};
  demand.recommendation = Controller::aggregate_rooms(room_recs, room_fresh, room_count);
  for (size_t i = 0; i < room_count; i++) {
    if (room_fresh[i]) {
      demand.any_fresh = true;
      demand.contributing_rooms++;
    }
  }

  StepResult result{};
  bool should_write = false;
  float write_bias = 0.0f;
  if (take_state_lock_(100)) {
    result = flow_trim_.step(demand, freeze, now_ms);
    heat_source_.trim_bias_c = flow_trim_.bias_c;
    heat_source_.trim_target_bias_c = result.target_bias_c;
    heat_source_.trim_frozen = result.frozen;
    std::strncpy(heat_source_.trim_freeze_reason, freeze_reason_name(result.freeze_reason),
                 sizeof(heat_source_.trim_freeze_reason) - 1);
    if (result.stepped)
      heat_source_.trim_last_step_ms = now_ms;
    should_write = result.stepped && flow_trim_.should_write_asgard() && source.bias_entity[0] != '\0';
    write_bias = flow_trim_.bias_c;
    give_state_lock_();
  }

  if (!should_write)
    return result.stepped || result.frozen;

  const asgard_adapter::Config cfg = make_asgard_config_(source);
  char url[448]{};
  if (!asgard_adapter::build_bias_write_url(cfg, write_bias, url, sizeof(url)))
    return false;
  esp_http_client_config_t http_cfg{};
  http_cfg.url = url;
  http_cfg.method = HTTP_METHOD_POST;
  http_cfg.timeout_ms = HTTP_TIMEOUT_MS;
  http_cfg.disable_auto_redirect = true;
  esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
  if (client == nullptr)
    return false;
  esp_http_client_set_post_field(client, "", 0);
  const esp_err_t err = esp_http_client_perform(client);
  const int status = err == ESP_OK ? esp_http_client_get_status_code(client) : 0;
  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  const bool ok = asgard_adapter::request_succeeded(err == ESP_OK, status);
  if (ok) {
    float confirmed = NAN;
    char read_url[448]{};
    if (asgard_adapter::build_bias_read_url(cfg, read_url, sizeof(read_url))) {
      char body[384]{};
      int rs = 0;
      if (fetch_json_(read_url, body, sizeof(body), &rs))
        asgard_adapter::parse_number_response(body, &confirmed);
    }
    if (take_state_lock_(100)) {
      if (std::isfinite(confirmed)) {
        flow_trim_.last_confirmed_bias_c = confirmed;
        heat_source_.last_bias_confirmed_c = confirmed;
      }
      give_state_lock_();
      save_settings_();
    }
  }
  log_event_(ok ? "info" : "warn", "heat_source", ok ? "setpoint bias written" : "bias write failed");
  return ok;
}

bool LuneTouchCoordinator::odin_plan_active_() const {
  if (!odin_plan_.enabled)
    return false;
  const auto mode = plan_source::mode_from_name(odin_plan_.plan_source_mode);
  const bool want_v2 = plan_source::forecast_v2_runtime_enabled() &&
                       (mode == plan_source::PlanSourceMode::V2 ||
                        (mode == plan_source::PlanSourceMode::Auto && odin_plan_.odin_host[0] != '\0'));
  if (want_v2)
    return odin_plan_.odin_host[0] != '\0' && odin_plan_.host[0] != '\0';
  if (odin_plan_.odin_host[0] != '\0')
    return odin_plan_.host[0] != '\0';
  return heat_source_.enabled && heat_source_type_is_asgard_(heat_source_.type) &&
         heat_source_.host[0] != '\0' && odin_plan_.host[0] != '\0';
}

void LuneTouchCoordinator::clear_odin_plan_live_state_() {
  odin_plan_.available = false;
  odin_plan_.heat_window_active = false;
  odin_plan_.plan_stale = false;
  odin_plan_.heat_production_count = 0;
  odin_plan_.past_count = 0;
  odin_plan_.current_planned_heat_kw = NAN;
  odin_plan_.current_operation_mode_raw = -1;
  std::strncpy(odin_plan_.current_decision_reason, "unknown",
               sizeof(odin_plan_.current_decision_reason) - 1);
  std::strncpy(odin_plan_.source_id, "none", sizeof(odin_plan_.source_id) - 1);
  std::strncpy(odin_plan_.status, "unavailable", sizeof(odin_plan_.status) - 1);
  odin_plan_.status[sizeof(odin_plan_.status) - 1] = '\0';
}

void LuneTouchCoordinator::sync_odin_plan_from_heat_source_() {
  const auto mode = plan_source::mode_from_name(odin_plan_.plan_source_mode);
  const bool prefer_v2 = plan_source::forecast_v2_runtime_enabled() &&
                         (mode == plan_source::PlanSourceMode::V2 ||
                          (mode == plan_source::PlanSourceMode::Auto && odin_plan_.odin_host[0] != '\0'));
  if (prefer_v2 && odin_plan_.odin_host[0] != '\0') {
    std::strncpy(odin_plan_.host, odin_plan_.odin_host, sizeof(odin_plan_.host) - 1);
    odin_plan_.host[sizeof(odin_plan_.host) - 1] = '\0';
    odin_plan_.port = odin_plan_.odin_port == 0 ? 80 : odin_plan_.odin_port;
    std::strncpy(odin_plan_.source_id, plan_source::source_id_name(plan_source::SourceId::OdinForecastV2),
                 sizeof(odin_plan_.source_id) - 1);
  } else if (odin_plan_.odin_host[0] != '\0') {
    // Odin 2.x serves the same 72-slot plan at /dashboard/odin itself (Asgard no
    // longer does). Layout differences are handled in fetch_odin_plan_().
    std::strncpy(odin_plan_.host, odin_plan_.odin_host, sizeof(odin_plan_.host) - 1);
    odin_plan_.host[sizeof(odin_plan_.host) - 1] = '\0';
    odin_plan_.port = odin_plan_.odin_port == 0 ? 80 : odin_plan_.odin_port;
    std::strncpy(odin_plan_.source_id, "odin2_dashboard", sizeof(odin_plan_.source_id) - 1);
  } else if (heat_source_type_is_asgard_(heat_source_.type) && heat_source_.host[0] != '\0') {
    std::strncpy(odin_plan_.host, heat_source_.host, sizeof(odin_plan_.host) - 1);
    odin_plan_.host[sizeof(odin_plan_.host) - 1] = '\0';
    odin_plan_.port = heat_source_.port == 0 ? 80 : heat_source_.port;
    std::strncpy(odin_plan_.source_id,
                 plan_source::source_id_name(plan_source::SourceId::AsgardDashboardV1),
                 sizeof(odin_plan_.source_id) - 1);
  } else {
    odin_plan_.enabled = false;
    odin_plan_.host[0] = '\0';
    std::strncpy(odin_plan_.source_id, "none", sizeof(odin_plan_.source_id) - 1);
  }
  if (!odin_plan_active_())
    clear_odin_plan_live_state_();
}

bool LuneTouchCoordinator::odin_defrost_blocks_arm_() const {
  // Hard gate: MQTT telemetry defrost when known; else Asgard HTTP freeze input.
  if (odin_mqtt_.defrost_known)
    return odin_mqtt_.defrost_active;
  return heat_source_.defrost_active;
}

bool LuneTouchCoordinator::fetch_odin_plan_() {
  OdinPlanState settings;
  if (!take_state_lock_(100))
    return false;
  sync_odin_plan_from_heat_source_();
  if (!odin_plan_active_()) {
    give_state_lock_();
    return false;
  }
  settings = odin_plan_;
  give_state_lock_();
  if (!settings.enabled || settings.host[0] == '\0')
    return false;

  char error[sizeof(settings.last_error)]{};
  int status = 0;
  odin_plan::Snapshot plan{};
  static float plan_energy[odin_plan::HORIZON_HOURS];
  bool has_energy = false;
  float plan_last_run_exec_ms = NAN;
  float plan_last_run_nodes = NAN;
  float plan_last_run_cost = NAN;
  bool plan_has_last_run = false;
  static constexpr size_t PLAN_BODY_CAP = 16384;
  char *body = alloc_psram_body_(PLAN_BODY_CAP);
  if (body == nullptr)
    return false;
  const bool odin2 = std::strcmp(settings.source_id, "odin2_dashboard") == 0;
  uint8_t local_hour = 0;
  bool clock_ok = false;
  if (time_ != nullptr) {
    auto now = time_->now();
    if (now.is_valid()) {
      local_hour = static_cast<uint8_t>(now.hour);
      clock_ok = true;
    }
  }
  char url[192];
  const bool use_v2 = plan_source::forecast_v2_runtime_enabled() &&
                      std::strcmp(settings.source_id, "odin_forecast_v2") == 0;
  if (use_v2) {
    // v2 adapter remains runtime-disabled until U3 fixture gate lifts
    // LUNE_ODIN_FORECAST_V2_ENABLED. This branch is unreachable while the gate
    // is closed; kept for the activation path.
    std::snprintf(url, sizeof(url), "http://%s:%u/api/forecast", settings.host,
                  static_cast<unsigned>(settings.port));
  } else {
    std::snprintf(url, sizeof(url), "http://%s:%u/dashboard/odin", settings.host,
                  static_cast<unsigned>(settings.port));
  }
  const bool fetched = fetch_json_(url, body, PLAN_BODY_CAP, &status, 6000);
  if (!fetched) {
    std::snprintf(error, sizeof(error), status > 0 ? "http %d" : "unreachable", status);
  } else {
    JsonDocument doc;
    const DeserializationError json_error = deserializeJson(doc, body);
    JsonObjectConst response = doc.as<JsonObjectConst>();
    auto copy_float_array = [](JsonVariantConst source, float *destination) {
      JsonArrayConst values = source.as<JsonArrayConst>();
      if (values.isNull() || values.size() != odin_plan::HORIZON_HOURS)
        return false;
      for (size_t i = 0; i < odin_plan::HORIZON_HOURS; ++i) {
        const float value = values[i].as<float>();
        if (!std::isfinite(value))
          return false;
        destination[i] = value;
      }
      return true;
    };
    auto copy_operation_mode_array = [](JsonVariantConst source, int *destination) {
      JsonArrayConst values = source.as<JsonArrayConst>();
      if (values.isNull() || values.size() != odin_plan::HORIZON_HOURS)
        return false;
      for (size_t i = 0; i < odin_plan::HORIZON_HOURS; ++i) {
        const float value = values[i].as<float>();
        if (!std::isfinite(value))
          return false;
        destination[i] = static_cast<int>(std::lround(value));
      }
      return true;
    };
    auto copy_nullable_float_array = [](JsonVariantConst source, float *destination) {
      JsonArrayConst values = source.as<JsonArrayConst>();
      if (values.isNull() || values.size() != odin_plan::HORIZON_HOURS)
        return false;
      for (size_t i = 0; i < odin_plan::HORIZON_HOURS; ++i) {
        if (values[i].isNull()) {
          destination[i] = NAN;
          continue;
        }
        const float value = values[i].as<float>();
        destination[i] = std::isfinite(value) ? value : NAN;
      }
      return true;
    };
    int current_hour = response["current_hour"] | -1;
    int today_start_index = response["today_start_index"] | -1;
    if (odin2 && current_hour < 0 && today_start_index < 0 && clock_ok) {
      current_hour = local_hour;
      today_start_index = odin_plan::ODIN2_TODAY_START_INDEX;
    }
    uint8_t idx_now = 0;
    plan.success = !json_error && (response["success"] | false) && current_hour >= 0 &&
                   current_hour < 24 && today_start_index >= 0 &&
                   odin_plan::resolve_idx_now(static_cast<uint8_t>(today_start_index),
                                              static_cast<uint8_t>(current_hour), &idx_now);
    plan.current_hour = plan.success ? static_cast<uint8_t>(current_hour) : 0;
    plan.today_start_index = plan.success ? static_cast<uint8_t>(today_start_index) : 0;
    plan.current_index = plan.success ? idx_now : 0;
    if (odin2) {
      // Past slots may be null; only the plan from now on must be complete.
      // Odin 2.0 has no expected_end_temp: end of hour = begin of the next.
      plan.success = plan.success &&
                     copy_nullable_float_array(response["sched_base"], plan.sched_base) &&
                     copy_nullable_float_array(response["sched_min"], plan.sched_min) &&
                     copy_nullable_float_array(response["sched_max"], plan.sched_max) &&
                     copy_nullable_float_array(response["expected_begin_temp"], plan.expected_begin_temp) &&
                     copy_nullable_float_array(response["prices"], plan.prices) &&
                     copy_nullable_float_array(response["heat_production"], plan.heat_production);
      if (plan.success) {
        JsonArrayConst modes = response["operation_mode"].as<JsonArrayConst>();
        plan.success = !modes.isNull() && modes.size() == odin_plan::HORIZON_HOURS;
        for (size_t i = 0; plan.success && i < odin_plan::HORIZON_HOURS; ++i)
          plan.operation_mode[i] = modes[i].isNull() ? 255 : modes[i].as<int>();
        for (size_t i = 0; i < odin_plan::HORIZON_HOURS; ++i)
          plan.expected_end_temp[i] = i + 1 < odin_plan::HORIZON_HOURS ? plan.expected_begin_temp[i + 1]
                                                                       : plan.expected_begin_temp[i];
        plan.success = plan.success && odin_plan::validate_future(plan);
      }
    } else
    plan.success = plan.success &&
                   copy_float_array(response["sched_base"], plan.sched_base) &&
                   copy_float_array(response["sched_min"], plan.sched_min) &&
                   copy_float_array(response["sched_max"], plan.sched_max) &&
                   copy_float_array(response["expected_begin_temp"], plan.expected_begin_temp) &&
                   copy_float_array(response["expected_end_temp"], plan.expected_end_temp) &&
                   copy_float_array(response["prices"], plan.prices) &&
                   copy_float_array(response["heat_production"], plan.heat_production) &&
                   copy_operation_mode_array(response["operation_mode"], plan.operation_mode) &&
                   odin_plan::validate(plan);
    has_energy = plan.success && copy_nullable_float_array(response["energy_consumption"], plan_energy);
    plan.has_actual_prod = plan.success &&
                           (copy_nullable_float_array(response["actual_heat_prod"], plan.actual_prod) ||
                            copy_nullable_float_array(response["actual_prod"], plan.actual_prod));
    JsonObjectConst last_run = response["last_run"].as<JsonObjectConst>();
    if (!last_run.isNull()) {
      plan_last_run_exec_ms = last_run["execution_ms"] | NAN;
      plan_last_run_nodes = last_run["evaluated_nodes"] | NAN;
      plan_last_run_cost = last_run["total_cost"] | NAN;
      plan_has_last_run = std::isfinite(plan_last_run_exec_ms);
    }
    if (!plan.success)
      std::strncpy(error, json_error ? "invalid json" : (odin2 ? "invalid Odin 2 plan" : "invalid Asgard ODIN plan"),
                   sizeof(error) - 1);
  }
  heap_caps_free(body);

  const bool valid = fetched && plan.success;
  if (take_state_lock_(100)) {
    const uint32_t prev_fetch_ms = odin_plan_.last_fetch_ms;
    odin_plan_.last_fetch_ms = esphome::millis();
    odin_plan_.last_http_status = status;
    odin_plan_.available = valid;
    if (valid) {
      odin_plan_.current_hour = plan.current_hour;
      odin_plan_.current_index = plan.current_index;
      odin_plan_.today_start_index = plan.today_start_index;
      odin_plan_.idx_now = plan.current_index;
      odin_plan_.current_target_c = plan.sched_base[plan.current_index];
      odin_plan_.current_min_c = plan.sched_min[plan.current_index];
      odin_plan_.current_max_c = plan.sched_max[plan.current_index];
      odin_plan_.current_price = plan.prices[plan.current_index];
      odin_plan_.current_planned_heat_kw = plan.heat_production[plan.current_index];
      odin_plan_.current_operation_mode_raw = plan.operation_mode[plan.current_index];
      odin_plan_.heat_window_active =
          odin_plan::may_arm_absorb(plan.heat_production[plan.current_index],
                                    plan.operation_mode[plan.current_index]);
      std::strncpy(odin_plan_.current_decision_reason, "unknown",
                   sizeof(odin_plan_.current_decision_reason) - 1);
      odin_plan_.heat_production_count = 0;
      for (size_t h = 0; h < 24 && (plan.current_index + h) < odin_plan::HORIZON_HOURS; h++) {
        odin_plan_.heat_production_horizon[h] = plan.heat_production[plan.current_index + h];
        odin_plan_.band_min_horizon[h] = plan.sched_min[plan.current_index + h];
        odin_plan_.band_max_horizon[h] = plan.sched_max[plan.current_index + h];
        odin_plan_.expected_temp_horizon[h] = plan.expected_begin_temp[plan.current_index + h];
        odin_plan_.energy_horizon[h] = has_energy ? plan_energy[plan.current_index + h] : NAN;
        odin_plan_.operation_mode_horizon[h] = plan.operation_mode[plan.current_index + h];
        std::strncpy(odin_plan_.decision_reason_horizon[h], "unknown",
                     sizeof(odin_plan_.decision_reason_horizon[h]) - 1);
        odin_plan_.heat_production_count++;
      }
      // History ≤ idx_now only — never feed future plan slots into past comparison.
      odin_plan_.past_count = 0;
      const size_t hist_start =
          plan.current_index >= 23 ? static_cast<size_t>(plan.current_index) - 23 : 0;
      for (size_t i = hist_start; i <= plan.current_index && odin_plan_.past_count < 24; i++) {
        odin_plan_.plan_heat_past[odin_plan_.past_count] = plan.heat_production[i];
        odin_plan_.actual_prod_past[odin_plan_.past_count] =
            plan.has_actual_prod ? plan.actual_prod[i] : NAN;
        odin_plan_.past_count++;
      }
      if (plan_has_last_run) {
        const bool same =
            std::isfinite(odin_plan_.last_run_execution_ms) &&
            std::fabs(odin_plan_.last_run_execution_ms - plan_last_run_exec_ms) < 0.01f &&
            std::fabs(odin_plan_.last_run_evaluated_nodes - plan_last_run_nodes) < 0.01f &&
            std::fabs(odin_plan_.last_run_total_cost - plan_last_run_cost) < 0.001f;
        odin_plan_.plan_stale = same && prev_fetch_ms != 0;
        odin_plan_.last_run_execution_ms = plan_last_run_exec_ms;
        odin_plan_.last_run_evaluated_nodes = plan_last_run_nodes;
        odin_plan_.last_run_total_cost = plan_last_run_cost;
      } else {
        odin_plan_.plan_stale = false;
      }
      if (odin_plan_.heat_window_active) {
        odin_plan_.bias_window_planned_kwh =
            std::fmax(0.0f, plan.heat_production[plan.current_index]);
      }
      std::strncpy(odin_plan_.status, "available", sizeof(odin_plan_.status) - 1);
      odin_plan_.last_error[0] = '\0';
    } else {
      std::strncpy(odin_plan_.status, "invalid", sizeof(odin_plan_.status) - 1);
      std::strncpy(odin_plan_.last_error, error, sizeof(odin_plan_.last_error) - 1);
      odin_plan_.last_error[sizeof(odin_plan_.last_error) - 1] = '\0';
    }
    give_state_lock_();
  }
  log_event_(valid ? "info" : "warn", "odin_plan", valid ? "ODIN plan refreshed" : error);
  if (valid)
    maybe_dispatch_absorb_arm_();
  return valid;
}

bool LuneTouchCoordinator::fetch_circulation_pump_() {
  CirculationPumpState settings;
  if (!take_state_lock_(100))
    return false;
  settings = circulation_;
  give_state_lock_();
  if (!settings.enabled || settings.host[0] == '\0')
    return false;

  struct Reading {
    const char *entity;
    float *value;
    bool *has_value;
  };
  float flow = NAN;
  float head = NAN;
  float power = NAN;
  bool has_flow = false;
  bool has_head = false;
  bool has_power = false;
  Reading readings[] = {
      {settings.flow_entity, &flow, &has_flow},
      {settings.head_entity, &head, &has_head},
      {settings.power_entity, &power, &has_power},
  };
  char error[sizeof(settings.last_error)]{};
  int last_status = 0;
  for (Reading &reading : readings) {
    char url[320];
    if (!circulation_pump::build_sensor_read_url(settings.host, settings.port, reading.entity,
                                                 url, sizeof(url))) {
      if (error[0] == '\0')
        std::snprintf(error, sizeof(error), "invalid %s endpoint", reading.entity);
      continue;
    }
    char body[384];
    int status = 0;
    if (esphome::network::is_connected() && fetch_json_(url, body, sizeof(body), &status)) {
      last_status = status;
      if (circulation_pump::parse_sensor_response(body, reading.value)) {
        *reading.has_value = true;
        continue;
      }
      if (error[0] == '\0')
        std::snprintf(error, sizeof(error), "%s value missing", reading.entity);
    } else {
      last_status = status;
      if (error[0] == '\0') {
        if (status > 0)
          std::snprintf(error, sizeof(error), "http %d", status);
        else if (std::strstr(settings.host, ".local") != nullptr)
          std::snprintf(error, sizeof(error), "unreachable (.local DNS? try IP)");
        else
          std::snprintf(error, sizeof(error), "unreachable");
      }
    }
  }

  const bool any = has_flow || has_head || has_power;
  if (take_state_lock_(100)) {
    circulation_.last_fetch_ms = esphome::millis();
    circulation_.last_http_status = last_status;
    circulation_.reachable = any;
    if (has_flow) {
      circulation_.has_flow = true;
      circulation_.flow_m3h = flow;
    }
    if (has_head) {
      circulation_.has_head = true;
      circulation_.head_m = head;
    }
    if (has_power) {
      circulation_.has_power = true;
      circulation_.power_w = power;
    }
    // Combine pump flow with V6 manifold ΔT when available: kW = m³/h × ΔT × 1.163
    float flow_c_sum = 0.0f;
    float return_c_sum = 0.0f;
    size_t temp_nodes = 0;
    for (size_t i = 0; i < model_.node_count() && i < ::lune_touch::MAX_NODES; i++) {
      if (node_telemetry_[i].has_flow && node_telemetry_[i].has_return &&
          std::isfinite(node_telemetry_[i].flow_c) && std::isfinite(node_telemetry_[i].return_c)) {
        flow_c_sum += node_telemetry_[i].flow_c;
        return_c_sum += node_telemetry_[i].return_c;
        temp_nodes++;
      }
    }
    circulation_.has_thermal_kw = false;
    std::strncpy(circulation_.thermal_kw_source, "none", sizeof(circulation_.thermal_kw_source) - 1);
    if (has_flow && std::isfinite(flow) && temp_nodes > 0) {
      const float flow_c = flow_c_sum / static_cast<float>(temp_nodes);
      const float return_c = return_c_sum / static_cast<float>(temp_nodes);
      const float dT = flow_c - return_c;
      if (std::isfinite(dT) && dT > 0.05f) {
        circulation_.thermal_kw = flow * dT * 1.163f;
        circulation_.has_thermal_kw = true;
        std::strncpy(circulation_.thermal_kw_source,
                     temp_nodes > 1 ? "estimated" : "measured",
                     sizeof(circulation_.thermal_kw_source) - 1);
      }
    }
    circulation_.has_hydraulic_authority = false;
    if (has_flow && has_head && std::isfinite(flow) && std::isfinite(head) && flow > 0.05f) {
      // Normalize: high head at low flow ⇒ low hydraulic authority (few loops open).
      const float head_norm = std::fmin(1.0f, head / 10.0f);
      const float flow_norm = std::fmin(1.0f, flow / 2.0f);
      circulation_.hydraulic_authority = std::fmax(0.0f, flow_norm * (1.0f - 0.6f * head_norm));
      circulation_.has_hydraulic_authority = true;
    }
    if (any) {
      circulation_.last_success_ms = circulation_.last_fetch_ms;
      circulation_.last_error[0] = '\0';
      circulation_.consecutive_failures = 0;
      const bool any_calling = model_.calling_zone_count() > 0;
      const uint16_t want = any_calling ? circulation_.refresh_interval_active_s
                                        : circulation_.refresh_interval_idle_s;
      if (want >= 10 && want <= 3600)
        circulation_.refresh_interval_s = want;
      if (circulation_.has_thermal_kw && std::isfinite(circulation_.thermal_kw) &&
          circulation_.thermal_kw > 0.0f && circulation_.last_success_ms != 0) {
        static uint32_t last_energy_ms = 0;
        if (last_energy_ms != 0 && circulation_.last_fetch_ms > last_energy_ms) {
          const float hours =
              static_cast<float>(circulation_.last_fetch_ms - last_energy_ms) / 3600000.0f;
          const float kwh = circulation_.thermal_kw * hours;
          model_.allocate_delivered_kwh(kwh, circulation_.last_fetch_ms);
          if (odin_plan_.heat_window_active || odin_plan_.preload_timing_bias_enabled)
            odin_plan_.bias_window_delivered_kwh += kwh;
          // T2: UA learning writeback when confidence/relative change warrants it (§5.2).
          if (model_.has_outdoor_temp()) {
            const float t_out = model_.outdoor_temp_c();
            const uint32_t now_epoch =
                static_cast<uint32_t>(circulation_.last_fetch_ms / 1000UL);
            for (size_t ri = 0; ri < model_.room_count(); ri++) {
              const auto *room = model_.room(ri);
              if (room == nullptr || !room->include_in_house_temperature)
                continue;
              float ua = 0.0f;
              float conf = 0.0f;
              uint16_t days = 0;
              if (!model_.learn_ua_from_delivery(room->room_id, t_out, &ua, &conf, &days,
                                                 now_epoch))
                continue;
              for (size_t zi = 0; zi < model_.zone_count(); zi++) {
                const auto *z = model_.zone(zi);
                if (z == nullptr || !z->enabled || z->is_group_secondary ||
                    std::strcmp(z->room_id, room->room_id) != 0)
                  continue;
                const auto *node = model_.node(z->node_index);
                if (node == nullptr || std::strcmp(node->firmware, "mock") == 0)
                  continue;
                if (!node_supports_physics_writes_(*node))
                  continue;
                char payload[192];
                std::snprintf(payload, sizeof(payload),
                              "{\"ua_w_per_k\":%.2f,\"confidence\":%.3f,\"observed_days\":%u,"
                              "\"ts_epoch_s\":%lu}",
                              z->ua_learned_w_per_k, conf, static_cast<unsigned>(days),
                              static_cast<unsigned long>(now_epoch));
                char url[192];
                const char *host =
                    node->hostname[0] != '\0' ? node->hostname : node->fallback_ip;
                if (host == nullptr || host[0] == '\0')
                  continue;
                std::snprintf(url, sizeof(url), "http://%s/api/v1/zones/%u/ua-learned", host,
                              static_cast<unsigned>(z->zone_index + 1));
                char body[256];
                int status = 0;
                if (post_json_(url, payload, body, sizeof(body), &status, authority_shared_key_)) {
                  char event[96];
                  std::snprintf(event, sizeof(event), "ua-learned %s/z%u %.1f W/K conf %.2f",
                                room->room_id, static_cast<unsigned>(z->zone_index + 1),
                                z->ua_learned_w_per_k, conf);
                  log_event_("info", "physics", event);
                }
              }
            }
          }
        }
        last_energy_ms = circulation_.last_fetch_ms;
      }
    } else {
      std::strncpy(circulation_.last_error, error, sizeof(circulation_.last_error) - 1);
      circulation_.last_error[sizeof(circulation_.last_error) - 1] = '\0';
      if (circulation_.consecutive_failures < 250)
        circulation_.consecutive_failures++;
      // BLE/GENIair backoff: double interval up to idle default on failures.
      uint32_t backed = static_cast<uint32_t>(circulation_.refresh_interval_s) * 2u;
      if (backed < circulation_.refresh_interval_idle_s)
        backed = circulation_.refresh_interval_idle_s;
      if (backed > 3600)
        backed = 3600;
      circulation_.refresh_interval_s = static_cast<uint16_t>(backed);
    }
    give_state_lock_();
  }
  if (!any)
    log_event_("warn", "circulation", error[0] != '\0' ? error : "unreachable");
  return any;
}

void LuneTouchCoordinator::poll_once_() {
  ::lune_touch::PairedNode nodes[::lune_touch::MAX_NODES]{};
  size_t count = 0;
  // Registry export can hold the lock during NVS; wait longer than dashboard
  // refreshes so paired nodes are not silently skipped.
  if (!take_state_lock_(500)) {
    ESP_LOGW(TAG, "V6 poll skipped: coordinator busy");
    return;
  }
  count = model_.node_count();
  if (count > ::lune_touch::MAX_NODES)
    count = ::lune_touch::MAX_NODES;
  for (size_t i = 0; i < count; i++) {
    const auto *node = model_.node(i);
    if (node != nullptr)
      nodes[i] = *node;
  }
  give_state_lock_();

  const uint32_t now = esphome::millis();
  last_poll_ms_ = now;
  bool any_success = false;
  for (size_t i = 0; i < count; i++) {
    if (nodes[i].hostname[0] == '\0' && nodes[i].fallback_ip[0] == '\0')
      continue;
    if (std::strcmp(nodes[i].firmware, "mock") == 0)
      continue;
    // Cheap gate first: skip the full poll while V6's control picture is
    // unchanged and the last full poll is recent.
    static constexpr uint32_t FULL_POLL_MAX_MS = 60000UL;
    const int summary = poll_node_summary_(i, nodes[i]);
    if (summary == 1 && node_last_full_poll_ms_[i] != 0 &&
        now - node_last_full_poll_ms_[i] < FULL_POLL_MAX_MS) {
      if (take_state_lock_(100)) {
        model_.mark_node_seen(i, now);
        give_state_lock_();
      }
      node_summary_skips_[i]++;
      any_success = true;
      poll_success_count_++;
      continue;
    }
    const bool overview_ok = poll_node_overview_(i, nodes[i], now);
    const bool zones_ok = poll_node_zones_(i, nodes[i], now);
    if (overview_ok && zones_ok)
      node_last_full_poll_ms_[i] = now;
    if (!overview_ok && !zones_ok) {
      poll_fail_count_++;
      snprintf(last_poll_error_, sizeof(last_poll_error_), "poll failed");
      if (take_state_lock_(100)) {
        model_.mark_node_unreachable(i, now);
        give_state_lock_();
      }
    } else {
      any_success = true;
      poll_success_count_++;
      last_poll_error_[0] = '\0';
    }
  }
  if (any_success && learning_dirty_ &&
      (last_learning_save_ms_ == 0 ||
       static_cast<int32_t>(now - last_learning_save_ms_) >= static_cast<int32_t>(LEARNING_SAVE_INTERVAL_MS))) {
    save_registry_();
    if (preload_learning_dirty_) {
      save_preload_learning_();
      preload_learning_dirty_ = false;
    }
    learning_dirty_ = false;
    last_learning_save_ms_ = now;
  }
}

int LuneTouchCoordinator::poll_node_summary_(size_t node_index, const ::lune_touch::PairedNode &node) {
  static constexpr uint32_t UNSUPPORTED_RETRY_MS = 3600000UL;
  if (node_index >= ::lune_touch::MAX_NODES)
    return -1;
  const uint32_t now = esphome::millis();
  if (node_summary_support_[node_index] == 2) {
    if (now - node_summary_retry_ms_[node_index] < UNSUPPORTED_RETRY_MS)
      return -1;
    node_summary_support_[node_index] = 0;  // firmware may have been updated
  }
  const char *host = node.hostname[0] != '\0' ? node.hostname : node.fallback_ip;
  if (host[0] == '\0')
    return -1;
  char url[192];
  if (node_summary_has_rev_[node_index])
    std::snprintf(url, sizeof(url), "http://%s/api/v1/summary?since=%08lx", host,
                  static_cast<unsigned long>(node_summary_rev_[node_index]));
  else
    std::snprintf(url, sizeof(url), "http://%s/api/v1/summary", host);
  char body[384]{};
  int status = 0;
  if (!fetch_json_(url, body, sizeof(body), &status, V6_HTTP_TIMEOUT_MS)) {
    if (status == 404) {
      node_summary_support_[node_index] = 2;
      node_summary_retry_ms_[node_index] = now;
    }
    return -1;
  }
  JsonDocument doc;
  if (deserializeJson(doc, body))
    return -1;
  JsonVariantConst data = doc["data"];
  const char *rev_text = data["rev"] | "";
  char *end = nullptr;
  const unsigned long rev = std::strtoul(rev_text, &end, 16);
  if (end == rev_text)
    return -1;
  node_summary_support_[node_index] = 1;
  const bool had = node_summary_has_rev_[node_index];
  node_summary_rev_[node_index] = static_cast<uint32_t>(rev);
  node_summary_has_rev_[node_index] = true;
  const bool changed = data["changed"] | true;
  return had && !changed ? 1 : 0;
}

bool LuneTouchCoordinator::poll_node_overview_(size_t node_index, const ::lune_touch::PairedNode &node, uint32_t now_ms) {
  const char *hosts[2]{};
  size_t host_count = 0;
  if (node.hostname[0] != '\0')
    hosts[host_count++] = node.hostname;
  if (node.fallback_ip[0] != '\0' &&
      (host_count == 0 || std::strcmp(node.fallback_ip, hosts[0]) != 0))
    hosts[host_count++] = node.fallback_ip;
  if (host_count == 0)
    return false;

  constexpr size_t BODY_CAP = 4096;
  char *body = alloc_http_body_(BODY_CAP);
  if (body == nullptr) {
    note_node_poll_failure_(node_index, "overview no_body_heap");
    return false;
  }

  int last_status = 0;
  for (size_t h = 0; h < host_count; h++) {
    char url[160];
    snprintf(url, sizeof(url), "http://%s/api/v1/overview", hosts[h]);

    int status = 0;
    if (!fetch_json_(url, body, BODY_CAP, &status, V6_HTTP_TIMEOUT_MS)) {
      // One retry absorbs roam-scan EAGAIN / truncated chunk frames.
      if (!fetch_json_(url, body, BODY_CAP, &status, V6_HTTP_TIMEOUT_MS)) {
        last_status = status;
        ESP_LOGD(TAG, "V6 overview poll failed for %s via %s (%d)", node.node_id, hosts[h], status);
        continue;
      }
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
      ESP_LOGW(TAG, "V6 overview JSON parse failed for %s via %s: %s", node.node_id, hosts[h], err.c_str());
      note_node_poll_failure_(node_index, "overview invalid_json");
      continue;
    }
    JsonVariant data = doc["data"];
    if (data.isNull())
      data = doc;
    const char *model = data["node"]["model"].as<const char *>();
    const char *firmware = data["node"]["firmware"].as<const char *>();
    const char *ip = data["node"]["ip"].as<const char *>();
    // Copy out of the document immediately — MemberProxy pointers are not
    // safe to strcmp against the paired registry across later doc access.
    char live_fingerprint[24]{};
    {
      const char *fp = data["pairing"]["fingerprint"] | "";
      if (fp[0] == '\0')
        fp = data["node"]["pairing_fingerprint"] | "";
      std::strncpy(live_fingerprint, fp, sizeof(live_fingerprint) - 1);
    }
    const char *pairing_fingerprint =
        live_fingerprint[0] != '\0' ? live_fingerprint : nullptr;
    // V6 reports hv6-unknown / lv6-unknown while the MAC text sensor is empty
    // (Wi-Fi roam), and weak links can yield non-fingerprint strings. Neither
    // is a swap. Both the hv6- and the rebranded lv6- prefix are accepted.
    const bool live_usable =
        pairing_fingerprint != nullptr &&
        (std::strncmp(pairing_fingerprint, "hv6-", 4) == 0 ||
         std::strncmp(pairing_fingerprint, "lv6-", 4) == 0) &&
        std::strcmp(pairing_fingerprint + 3, "-unknown") != 0;
    if (node.pairing_fingerprint[0] != '\0' && !live_usable) {
      ESP_LOGD(TAG, "V6 identity pending for %s via %s (live='%s')", node.node_id, hosts[h],
               pairing_fingerprint != nullptr ? pairing_fingerprint : "");
      note_node_poll_failure_(node_index, "overview identity_pending");
      continue;
    }
    if (node.pairing_fingerprint[0] != '\0' && live_usable &&
        !pairing_fingerprints_match_(node.pairing_fingerprint, pairing_fingerprint)) {
      ESP_LOGW(TAG, "V6 identity mismatch for %s via %s (stored='%s' live='%s')",
               node.node_id, hosts[h], node.pairing_fingerprint, pairing_fingerprint);
      if (take_state_lock_(100)) {
        model_.mark_node_unreachable(node_index, now_ms);
        give_state_lock_();
      }
      note_node_poll_failure_(node_index, "overview identity_mismatch");
      continue;
    }

    // V6 owns the consent boundary for coordinator control. Touch mirrors the
    // approval reported by the node instead of promoting itself from the UI.
    const bool approval_reported = !data["coordination"].isNull();
    const bool control_approved = data["coordination"]["control_approved"] | false;
    const char *approved_installation = data["coordination"]["installation_id"] | "";
    const char *approved_coordinator = data["coordination"]["coordinator_id"] | "";

    const bool identity_matches = control_approved && install_id_[0] != '\0' &&
        authority_coordinator_id_[0] != '\0' &&
        std::strcmp(approved_installation, install_id_) == 0 &&
        std::strcmp(approved_coordinator, authority_coordinator_id_) == 0;
    if (!take_state_lock_(100)) {
      heap_caps_free(body);
      return false;
    }
    model_.update_node_metadata(node_index, model, firmware, ip);
    model_.update_node_identity(node_index, pairing_fingerprint);
    {
      uint8_t contract = 0;
      if (!data["physics_contract"].isNull())
        contract = static_cast<uint8_t>(data["physics_contract"] | 0);
      else if (!data["node"]["physics_contract"].isNull())
        contract = static_cast<uint8_t>(data["node"]["physics_contract"] | 0);
      if (node_index < ::lune_touch::MAX_NODES)
        node_telemetry_[node_index].physics_contract = contract;
    }
    if (approval_reported) {
      model_.update_node_trust(node.node_id, identity_matches
          ? ::lune_touch::NodeTrust::TRUSTED
          : ::lune_touch::NodeTrust::PAIRED);
    }
    model_.mark_node_seen(node_index, now_ms);
    if (node_index < ::lune_touch::MAX_NODES) {
      NodeTelemetryState &telemetry = node_telemetry_[node_index];
      if (!data["manifold"]["flow_c"].isNull()) {
        telemetry.flow_c = data["manifold"]["flow_c"] | 0.0f;
        telemetry.has_flow = true;
      }
      if (!data["manifold"]["return_c"].isNull()) {
        telemetry.return_c = data["manifold"]["return_c"] | 0.0f;
        telemetry.has_return = true;
      }
      if (!data["avg_valve_pct"].isNull()) {
        telemetry.avg_valve_pct = data["avg_valve_pct"] | 0.0f;
        telemetry.has_avg_valve = true;
      }
      telemetry.active_zones = static_cast<uint8_t>(std::min(255, data["active_zones"] | 0));
      if (!data["motor"]["drivers_enabled"].isNull()) {
        telemetry.drivers_enabled = data["motor"]["drivers_enabled"] | false;
        telemetry.has_drivers_enabled = true;
      }
      if (!data["motor"]["fault"].isNull()) {
        telemetry.motor_fault = data["motor"]["fault"] | false;
        telemetry.has_motor_fault = true;
      }
      if (!data["motor"]["current_ma"].isNull()) {
        telemetry.motor_current_ma = data["motor"]["current_ma"] | 0.0f;
        telemetry.has_motor_current = true;
      }
    }
    give_state_lock_();
    if (!identity_matches)
      propose_authority_to_node_(node_index, node, hosts[h], now_ms);
    note_node_poll_success_(node_index, hosts[h]);
    heap_caps_free(body);
    return true;
  }

  char reason[80];
  snprintf(reason, sizeof(reason), "overview failed status=%d", last_status);
  note_node_poll_failure_(node_index, reason);
  heap_caps_free(body);
  return false;
}

bool LuneTouchCoordinator::propose_authority_to_node_(size_t node_index,
                                                       const ::lune_touch::PairedNode &node,
                                                       const char *preferred_host,
                                                       uint32_t now_ms) {
  if (node_index >= ::lune_touch::MAX_NODES || preferred_host == nullptr || preferred_host[0] == '\0')
    return false;
  const uint32_t last = authority_proposal_last_ms_[node_index];
  if (last != 0 && static_cast<int32_t>(now_ms - last) < 30000)
    return false;

  char installation_id[32]{};
  char coordinator_id[32]{};
  char shared_key[64]{};
  char coordinator_name[32]{};
  char site_label[48]{};
  if (!take_state_lock_(100))
    return false;
  std::strncpy(installation_id, install_id_, sizeof(installation_id) - 1);
  std::strncpy(coordinator_id, authority_coordinator_id_, sizeof(coordinator_id) - 1);
  std::strncpy(shared_key, authority_shared_key_, sizeof(shared_key) - 1);
  std::strncpy(coordinator_name, coordinator_name_, sizeof(coordinator_name) - 1);
  std::strncpy(site_label, site_label_, sizeof(site_label) - 1);
  give_state_lock_();
  if (installation_id[0] == '\0' || std::strcmp(installation_id, "unassigned") == 0 ||
      coordinator_id[0] == '\0' || std::strlen(shared_key) < 16) {
    log_event_("warn", "authority", "proposal skipped: touch identity incomplete");
    return false;
  }

  // Stamp after identity checks so a missing key does not burn the 30s budget.
  authority_proposal_last_ms_[node_index] = now_ms;

  char escaped_name[72]{};
  char escaped_site[104]{};
  json_escape_(coordinator_name, escaped_name, sizeof(escaped_name));
  json_escape_(site_label, escaped_site, sizeof(escaped_site));
  char payload[384]{};
  std::snprintf(payload, sizeof(payload),
                "{\"installation_id\":\"%s\",\"coordinator_id\":\"%s\","
                "\"shared_key\":\"%s\",\"name\":\"%s\",\"site\":\"%s\"}",
                installation_id, coordinator_id, shared_key, escaped_name, escaped_site);
  char url[192]{};
  char response[256]{};
  int status = 0;
  std::snprintf(url, sizeof(url), "http://%s/api/v1/authority/proposal", preferred_host);
  const bool sent = post_json_(url, payload, response, sizeof(response), &status);
  if (sent) {
    ESP_LOGI(TAG, "Sent connection proposal to %s via %s", node.node_id, preferred_host);
    char event[112];
    snprintf(event, sizeof(event), "proposal sent to %s", node.node_id);
    log_event_("info", "authority", event);
  } else {
    ESP_LOGW(TAG, "Connection proposal to %s via %s failed (HTTP %d)", node.node_id,
             preferred_host, status);
    char event[112];
    snprintf(event, sizeof(event), "proposal to %s failed HTTP %d", node.node_id, status);
    log_event_("warn", "authority", event);
    char failure[48];
    snprintf(failure, sizeof(failure), "proposal failed HTTP %d", status);
    note_node_poll_failure_(node_index, failure);
  }
  return sent;
}

bool LuneTouchCoordinator::poll_node_zones_(size_t node_index, const ::lune_touch::PairedNode &node, uint32_t now_ms) {
  const char *hosts[2]{};
  size_t host_count = 0;
  if (node.hostname[0] != '\0')
    hosts[host_count++] = node.hostname;
  if (node.fallback_ip[0] != '\0' &&
      (host_count == 0 || std::strcmp(node.fallback_ip, hosts[0]) != 0))
    hosts[host_count++] = node.fallback_ip;
  if (host_count == 0)
    return false;

  // The v1 zones document includes a forecast object for every valve.  The
  // earlier 4 KiB buffer silently truncated valid six-zone responses; the
  // truncated JSON then fell through to the legacy /state endpoint and
  // recreated generated room names.
  constexpr size_t V1_BODY_CAP = 12288;
  char *v1_body = alloc_http_body_(V1_BODY_CAP);
  if (v1_body == nullptr) {
    note_node_poll_failure_(node_index, "zones no_body_heap");
    return false;
  }

  int last_status = 0;
  for (size_t h = 0; h < host_count; h++) {
    char url[160];
    snprintf(url, sizeof(url), "http://%s/api/v1/zones", hosts[h]);

    int status = 0;
    if (!fetch_json_(url, v1_body, V1_BODY_CAP, &status, V6_HTTP_TIMEOUT_MS) &&
        !fetch_json_(url, v1_body, V1_BODY_CAP, &status, V6_HTTP_TIMEOUT_MS)) {
      last_status = status;
      ESP_LOGD(TAG, "V6 zones poll failed for %s via %s (%d)", node.node_id, hosts[h], status);
      continue;
    }
    if (!ingest_v6_zones_(node_index, v1_body, now_ms)) {
      ESP_LOGW(TAG, "V6 zones poll returned unusable data for %s via %s", node.node_id, hosts[h]);
      last_status = status;
      note_node_poll_failure_(node_index, "zones invalid_json");
      continue;
    }
    note_node_poll_success_(node_index, hosts[h]);
    // Best-effort groups poll (contract §10); zones fallback already applied.
    poll_node_groups_(node_index, node);
    heap_caps_free(v1_body);
    return true;
  }

  heap_caps_free(v1_body);

  // Only fall back to legacy /state when the v1 zones route is confirmed
  // missing (404). status=0 (EAGAIN/CONNECT) must not trigger the heavy
  // snapshot fetch — that path previously overwrote the pairing fingerprint
  // with the raw MAC and permanently failed identity checks.
  if (last_status != 404) {
    char reason[80];
    snprintf(reason, sizeof(reason), "zones failed status=%d", last_status);
    note_node_poll_failure_(node_index, reason);
    return false;
  }

  for (size_t h = 0; h < host_count; h++) {
    char url[160];
    snprintf(url, sizeof(url), "http://%s/api/v1/state", hosts[h]);

    constexpr size_t LEGACY_BODY_CAP = 14336;
    char *body = alloc_http_body_(LEGACY_BODY_CAP);
    if (body == nullptr)
      continue;

    int status = 0;
    const bool fetched = fetch_json_(url, body, LEGACY_BODY_CAP, &status);
    if (fetched && ingest_v6_legacy_state_(node_index, node, body, now_ms)) {
      heap_caps_free(body);
      note_node_poll_success_(node_index, hosts[h]);
      return true;
    }
    last_status = status;
    heap_caps_free(body);
  }

  char reason[80];
  snprintf(reason, sizeof(reason), "zones failed status=%d", last_status);
  note_node_poll_failure_(node_index, reason);
  return false;
}

void LuneTouchCoordinator::note_node_poll_success_(size_t node_index, const char *host) {
  if (node_index >= ::lune_touch::MAX_NODES)
    return;
  std::strncpy(node_last_success_host_[node_index], host != nullptr ? host : "",
               sizeof(node_last_success_host_[node_index]) - 1);
  node_last_success_host_[node_index][sizeof(node_last_success_host_[node_index]) - 1] = '\0';
  node_last_failure_[node_index][0] = '\0';
}

void LuneTouchCoordinator::note_node_poll_failure_(size_t node_index, const char *reason) {
  if (node_index >= ::lune_touch::MAX_NODES)
    return;
  const char *next_reason = reason != nullptr ? reason : "poll failed";
  const bool changed = std::strcmp(node_last_failure_[node_index], next_reason) != 0;
  std::strncpy(node_last_failure_[node_index], reason != nullptr ? reason : "poll failed",
               sizeof(node_last_failure_[node_index]) - 1);
  node_last_failure_[node_index][sizeof(node_last_failure_[node_index]) - 1] = '\0';
  if (changed) {
    char message[112];
    snprintf(message, sizeof(message), "node %u %s", static_cast<unsigned>(node_index), next_reason);
    log_event_("warn", "poll", message);
  }
}

bool LuneTouchCoordinator::fetch_json_(const char *url, char *body, size_t body_capacity, int *status_code) {
  return fetch_json_(url, body, body_capacity, status_code, HTTP_TIMEOUT_MS);
}

bool LuneTouchCoordinator::fetch_json_(const char *url, char *body, size_t body_capacity, int *status_code,
                                       uint32_t timeout_ms) {
  if (body == nullptr || body_capacity == 0 || url == nullptr)
    return false;
  body[0] = '\0';
  if (status_code != nullptr)
    *status_code = 0;

  struct FetchCtx {
    char *body;
    size_t capacity;
    size_t length;
  };
  FetchCtx ctx{body, body_capacity, 0};

  esp_http_client_config_t cfg{};
  cfg.url = url;
  cfg.method = HTTP_METHOD_GET;
  cfg.timeout_ms = timeout_ms == 0 ? HTTP_TIMEOUT_MS : timeout_ms;
  cfg.disable_auto_redirect = true;
  cfg.user_data = &ctx;
  cfg.event_handler = [](esp_http_client_event_t *evt) -> esp_err_t {
    if (evt == nullptr || evt->user_data == nullptr)
      return ESP_OK;
    auto *fetch = static_cast<FetchCtx *>(evt->user_data);
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data != nullptr && evt->data_len > 0) {
      const size_t room = fetch->capacity > fetch->length + 1 ? fetch->capacity - fetch->length - 1 : 0;
      const size_t copy = std::min(room, static_cast<size_t>(evt->data_len));
      if (copy > 0) {
        std::memcpy(fetch->body + fetch->length, evt->data, copy);
        fetch->length += copy;
        fetch->body[fetch->length] = '\0';
      }
    }
    return ESP_OK;
  };

  esp_http_client_handle_t client = esp_http_client_init(&cfg);
  if (client == nullptr)
    return false;

  // perform() applies timeout_ms to connect + transfer; open()/read_response()
  // has been observed to stall indefinitely on some IDF builds when the peer
  // is unreachable on the LAN (AP client isolation, etc.).
  const esp_err_t err = esp_http_client_perform(client);
  const int status = err == ESP_OK ? esp_http_client_get_status_code(client) : 0;
  if (status_code != nullptr)
    *status_code = status;
  // Silent truncation used to look like a successful GET; the truncated JSON
  // then either failed parse or (worse) partially parsed into wrong fields.
  const bool truncated = ctx.length >= body_capacity - 1;
  const bool ok = err == ESP_OK && status == 200 && ctx.length > 0 && !truncated;
  if (err != ESP_OK)
    ESP_LOGD(TAG, "HTTP GET failed: %s", esp_err_to_name(err));
  else if (truncated)
    ESP_LOGW(TAG, "HTTP GET truncated at %u bytes for %s",
             static_cast<unsigned>(ctx.length), url);

  esp_http_client_cleanup(client);
  return ok;
}

bool LuneTouchCoordinator::post_json_(const char *url, const char *payload, char *body,
                                      size_t body_capacity, int *status_code,
                                      const char *authority_key) {
  if (body == nullptr || body_capacity == 0 || url == nullptr)
    return false;
  body[0] = '\0';
  if (status_code != nullptr)
    *status_code = 0;

  esp_http_client_config_t cfg{};
  cfg.url = url;
  cfg.method = HTTP_METHOD_POST;
  cfg.timeout_ms = HTTP_TIMEOUT_MS;
  cfg.disable_auto_redirect = true;

  esp_http_client_handle_t client = esp_http_client_init(&cfg);
  if (client == nullptr)
    return false;

  // ESPHome's web_server_idf only parses application/x-www-form-urlencoded into
  // request args. application/json goes through handleBody(), which V6 does not
  // implement — so JSON POSTs arrive with an empty body and look like invalid
  // proposals. Keep JSON at the call site, encode form on the wire.
  std::string form_body;
  if (!json_object_to_form_(payload, form_body)) {
    ESP_LOGW(TAG, "Unable to encode POST payload for %s", url);
    esp_http_client_cleanup(client);
    return false;
  }
  const char *post_body = form_body.c_str();
  const size_t post_length = std::strlen(post_body);
  esp_http_client_set_header(client, "Content-Type", "application/x-www-form-urlencoded");
  if (authority_key != nullptr && authority_key[0] != '\0')
    esp_http_client_set_header(client, "X-Lune-Authority-Key", authority_key);

  bool ok = false;
  // esp_http_client_open() is the streaming API: its write_len argument
  // becomes Content-Length and replaces any earlier set_post_field length.
  // Opening with zero therefore sent an empty request even though a post
  // field had been configured. Write the complete form body explicitly.
  esp_err_t err = esp_http_client_open(client, static_cast<int>(post_length));
  if (err == ESP_OK) {
    size_t written = 0;
    while (written < post_length) {
      const int count = esp_http_client_write(client, post_body + written,
                                              static_cast<int>(post_length - written));
      if (count <= 0) {
        err = ESP_FAIL;
        break;
      }
      written += static_cast<size_t>(count);
    }
    if (err == ESP_OK) {
      esp_http_client_fetch_headers(client);
      const int status = esp_http_client_get_status_code(client);
      if (status_code != nullptr)
        *status_code = status;
      const int len = esp_http_client_read_response(client, body, body_capacity - 1);
      if (len > 0)
        body[len] = '\0';
      if (status >= 200 && status < 300 && len > 0)
        ok = true;
    }
  } else {
    ESP_LOGD(TAG, "HTTP POST failed: %s", esp_err_to_name(err));
  }

  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  return ok;
}

bool LuneTouchCoordinator::renew_authority_lease_() {
  char leader_id[32]{};
  char installation_id[32]{};
  char coordinator_id[32]{};
  char shared_key[64]{};
  char host[64]{};
  char lease_id[32]{};
  uint32_t sequence = 0;
  bool degraded = true;
  bool leader_selected = false;
  if (!take_state_lock_(100))
    return false;
  std::strncpy(leader_id, authority_leader_node_id_, sizeof(leader_id) - 1);
  std::strncpy(installation_id, install_id_, sizeof(installation_id) - 1);
  std::strncpy(coordinator_id, authority_coordinator_id_, sizeof(coordinator_id) - 1);
  std::strncpy(shared_key, authority_shared_key_, sizeof(shared_key) - 1);
  std::strncpy(lease_id, authority_lease_id_, sizeof(lease_id) - 1);
  if (leader_id[0] != '\0') {
    for (size_t index = 0; index < model_.node_count(); index++) {
      const auto *node = model_.node(index);
      if (node != nullptr && std::strcmp(node->node_id, leader_id) == 0) {
        std::strncpy(host, node->hostname[0] != '\0' ? node->hostname : node->fallback_ip,
                     sizeof(host) - 1);
        break;
      }
    }
  }
  if (host[0] == '\0' && model_.node_count() > 0) {
    const auto *node = model_.node(0);
    if (node != nullptr) {
      std::strncpy(host, node->hostname[0] != '\0' ? node->hostname : node->fallback_ip,
                   sizeof(host) - 1);
      if (host[0] != '\0') {
        std::strncpy(authority_leader_node_id_, node->node_id,
                     sizeof(authority_leader_node_id_) - 1);
        authority_leader_node_id_[sizeof(authority_leader_node_id_) - 1] = '\0';
        std::strncpy(leader_id, authority_leader_node_id_, sizeof(leader_id) - 1);
        leader_selected = true;
      }
    }
  }
  // V6 rejects any sequence ≤ the last one it accepted (replay protection), and
  // it keeps that number across a Touch reboot. A RAM counter restarting at 1
  // therefore locked Touch out ("replayed_sequence") until each V6 rebooted.
  // Unix time is monotonic across reboots; the counter only covers the seconds
  // before SNTP is valid and renewals within the same second.
  {
    const int64_t epoch = current_epoch_s_();
    sequence = authority_sequence_ + 1;
    if (epoch > 0 && static_cast<uint32_t>(epoch) > sequence)
      sequence = static_cast<uint32_t>(epoch);
    authority_sequence_ = sequence;  // advance even if this renewal is refused
  }
  const auto strategy = model_.strategy_snapshot();
  degraded = !strategy.has_physical_temperature;
  char control_mode[16]{};
  std::strncpy(control_mode, v6_control_mode_, sizeof(control_mode) - 1);
  give_state_lock_();
  if (leader_selected)
    save_settings_();
  if (host[0] == '\0' || installation_id[0] == '\0' || coordinator_id[0] == '\0' ||
      shared_key[0] == '\0') {
    if (take_state_lock_(100)) {
      std::strncpy(authority_state_, "no_publisher", sizeof(authority_state_) - 1);
      std::strncpy(authority_reason_, "authority configuration incomplete", sizeof(authority_reason_) - 1);
      authority_expires_at_ms_ = 0;
      give_state_lock_();
    }
    return false;
  }
  char url[192];
  char payload[384];
  char response[384];
  std::snprintf(url, sizeof(url), "http://%s/api/v1/authority/lease", host);
  if (control_mode[0] != '\0' && std::strcmp(control_mode, "local") != 0) {
    std::snprintf(payload, sizeof(payload),
                  "{\"installation_id\":\"%s\",\"coordinator_id\":\"%s\",\"lease_id\":\"%s\","
                  "\"sequence\":%lu,\"issued_ms\":%lu,\"duration_ms\":90000,\"degraded\":%s,"
                  "\"control_mode\":\"%s\"}",
                  installation_id, coordinator_id, lease_id, static_cast<unsigned long>(sequence),
                  static_cast<unsigned long>(esphome::millis()), degraded ? "true" : "false",
                  control_mode);
  } else {
    std::snprintf(payload, sizeof(payload),
                  "{\"installation_id\":\"%s\",\"coordinator_id\":\"%s\",\"lease_id\":\"%s\","
                  "\"sequence\":%lu,\"issued_ms\":%lu,\"duration_ms\":90000,\"degraded\":%s}",
                  installation_id, coordinator_id, lease_id, static_cast<unsigned long>(sequence),
                  static_cast<unsigned long>(esphome::millis()), degraded ? "true" : "false");
  }
  post_json_(url, payload, response, sizeof(response), nullptr, shared_key);
  JsonDocument doc;
  // A 409 is an expected, observable recovery-pending response. Keep its
  // state/reason instead of presenting a reachable V6 as an outage.
  const bool parsed = response[0] != '\0' && !deserializeJson(doc, response);
  const char *result = parsed ? doc["data"]["result"] | "" : "";
  const char *state = parsed ? doc["data"]["state"] | "no_publisher" : "no_publisher";
  const char *reason = parsed ? doc["data"]["reason"] | "lease renewal failed" : "lease renewal failed";
  const uint32_t remaining_s = parsed ? doc["data"]["lease_remaining_s"] | 0U : 0U;
  const bool lease_granted = parsed &&
      (std::strcmp(result, "granted") == 0 || std::strcmp(result, "renewed") == 0);
  if (take_state_lock_(100)) {
    const bool was_recovery = std::strcmp(authority_state_, "touch_recovery_pending") == 0;
    authority_last_renew_ms_ = esphome::millis();
    std::strncpy(authority_state_, state, sizeof(authority_state_) - 1);
    std::strncpy(authority_reason_, reason, sizeof(authority_reason_) - 1);
    if (lease_granted) {
      authority_sequence_ = sequence;
      authority_expires_at_ms_ = esphome::millis() + remaining_s * 1000UL;
      authority_generation_ = doc["data"]["generation"] | 0U;
      authority_last_fallback_value_c_ = doc["data"]["last_fallback_value_c"] | NAN;
      authority_last_asgard_value_c_ = doc["data"]["last_asgard_value_c"] | NAN;
      authority_v6_local_zones_ = doc["data"]["local_zones"] | 0U;
      authority_v6_peer_zones_ = doc["data"]["peer_zones"] | 0U;
      std::strncpy(authority_v6_peer_status_, doc["data"]["peer_status"] | "unknown",
                   sizeof(authority_v6_peer_status_) - 1);
      authority_v6_peer_status_[sizeof(authority_v6_peer_status_) - 1] = '\0';
      const char *echoed = doc["data"]["control_mode"] | "";
      if (echoed[0] != '\0') {
        std::strncpy(v6_control_mode_effective_, echoed, sizeof(v6_control_mode_effective_) - 1);
        v6_control_mode_effective_[sizeof(v6_control_mode_effective_) - 1] = '\0';
      } else {
        std::strncpy(v6_control_mode_effective_, control_mode[0] != '\0' ? control_mode : "local",
                     sizeof(v6_control_mode_effective_) - 1);
      }
      if (was_recovery) {
        authority_smooth_first_write_ = std::isfinite(authority_last_fallback_value_c_);
        trim_reseed_pending_ = true;
      }
    } else {
      authority_expires_at_ms_ = 0;
    }
    give_state_lock_();
  }
  log_event_(lease_granted ? "info" : (parsed ? "info" : "warn"), "authority",
             parsed ? result : "lease renewal failed");
  // Whole-house authority: every trusted V6 gets the same lease (id + sequence),
  // so each one knows Touch balances the house (no reactive absorb, Touch's
  // control_mode, Touch commands cleared if the lease lapses).
  fan_out_lease_(leader_id, payload, shared_key);
  return lease_granted;
}

void LuneTouchCoordinator::dispatch_charge_arms_() {
  struct Item {
    ::lune_touch::PairedNode node{};
    uint8_t node_index{0};
    uint8_t zone_index{0};
    bool arm{false};
    uint32_t ttl_s{0};
    char room_id[32]{};
  };
  // ~7 KB: never on the task stack. Combined with the HTTP calls below it
  // overflowed the forecast task as soon as a charge window opened (boot loop).
  // Only the forecast task calls this, so a static buffer is safe.
  static Item *items = psram_scratch_<Item>(::lune_touch::MAX_HOUSE_ZONES);
  if (items == nullptr)
    return;
  for (size_t i = 0; i < ::lune_touch::MAX_HOUSE_ZONES; i++)
    items[i] = Item{};
  size_t n = 0;
  bool wanted[::lune_touch::MAX_NODES][6]{};
  const uint32_t now = esphome::millis();
  if (!take_state_lock_(100))
    return;
  for (size_t i = 0; i < forecast_decision_count_ && n < ::lune_touch::MAX_HOUSE_ZONES; i++) {
    const ForecastDecisionState &d = forecast_decisions_[i];
    if (!d.charge_now || d.node_index >= ::lune_touch::MAX_NODES || d.zone_index >= 6)
      continue;
    wanted[d.node_index][d.zone_index] = true;
    const auto *node = model_.node(d.node_index);
    if (node == nullptr || node->trust != ::lune_touch::NodeTrust::TRUSTED)
      continue;
    // Re-arm every 30 min while the window is open (V6 caps TTL at 2 h).
    if (charge_armed_[d.node_index][d.zone_index] &&
        now - charge_last_arm_ms_[d.node_index][d.zone_index] < 30UL * 60UL * 1000UL)
      continue;
    Item &it = items[n++];
    it.node = *node;
    it.node_index = d.node_index;
    it.zone_index = d.zone_index;
    it.arm = true;
    const int32_t hours_left = d.charge_end_in_h > 0 ? d.charge_end_in_h : 1;
    it.ttl_s = static_cast<uint32_t>(std::clamp<int32_t>(hours_left * 3600, 600, 7200));
    std::strncpy(it.room_id, d.room_id, sizeof(it.room_id) - 1);
  }
  // Windows that closed: disarm.
  for (size_t ni = 0; ni < ::lune_touch::MAX_NODES && ni < model_.node_count(); ni++) {
    for (uint8_t zi = 0; zi < 6 && n < ::lune_touch::MAX_HOUSE_ZONES; zi++) {
      if (!charge_armed_[ni][zi] || wanted[ni][zi])
        continue;
      const auto *node = model_.node(ni);
      if (node == nullptr)
        continue;
      Item &it = items[n++];
      it.node = *node;
      it.node_index = static_cast<uint8_t>(ni);
      it.zone_index = zi;
      it.arm = false;
    }
  }
  give_state_lock_();

  bool any = false;
  for (size_t k = 0; k < n; k++) {
    const Item &it = items[k];
    ::lune_touch::CommandRecord record{};
    snprintf(record.request_id, sizeof(record.request_id), "sc-%lu-%02u",
             static_cast<unsigned long>(now), static_cast<unsigned>(k));
    std::strncpy(record.source, "slab_charge", sizeof(record.source) - 1);
    std::strncpy(record.reason, it.arm ? "wind/cold shortfall ahead" : "charge window closed",
                 sizeof(record.reason) - 1);
    record.node_index = it.node_index;
    record.zone_index = it.zone_index;
    stamp_command_timing_(&record, now, it.arm ? it.ttl_s : 0);
    const bool sent = it.arm
        ? send_v6_absorb_arm_(it.node, it.zone_index, it.ttl_s, &record, "slab_charge")
        : send_v6_absorb_disarm_(it.node, it.zone_index, &record, "slab_charge_end");
    const bool accepted = sent && record.result == ::lune_touch::CommandResult::ACCEPTED;
    if (take_state_lock_(100)) {
      if (it.arm && accepted) {
        charge_armed_[it.node_index][it.zone_index] = true;
        charge_last_arm_ms_[it.node_index][it.zone_index] = now;
      } else if (!it.arm && accepted) {
        charge_armed_[it.node_index][it.zone_index] = false;
      }
      ledger_.append(record);
      give_state_lock_();
    }
    any = true;
  }
  if (any) {
    save_ledger_();
    log_event_("info", "slab_charge", "slab charge arms updated");
  }
}

void LuneTouchCoordinator::fan_out_lease_(const char *leader_id, const char *payload,
                                         const char *shared_key) {
  struct Target { size_t index; char host[64]; };
  Target targets[::lune_touch::MAX_NODES]{};
  size_t count = 0;
  if (!take_state_lock_(100))
    return;
  for (size_t i = 0; i < model_.node_count() && i < ::lune_touch::MAX_NODES; i++) {
    const auto *node = model_.node(i);
    if (node == nullptr || node->trust != ::lune_touch::NodeTrust::TRUSTED)
      continue;
    if (leader_id != nullptr && std::strcmp(node->node_id, leader_id) == 0) {
      node_lease_state_[i] = authority_expires_at_ms_ != 0 ? 1 : 2;
      continue;
    }
    const char *h = node->hostname[0] != '\0' ? node->hostname : node->fallback_ip;
    if (h[0] == '\0')
      continue;
    targets[count].index = i;
    std::strncpy(targets[count].host, h, sizeof(targets[count].host) - 1);
    count++;
  }
  give_state_lock_();
  for (size_t t = 0; t < count; t++) {
    char url[192];
    char response[384];
    response[0] = '\0';
    std::snprintf(url, sizeof(url), "http://%s/api/v1/authority/lease", targets[t].host);
    post_json_(url, payload, response, sizeof(response), nullptr, shared_key);
    JsonDocument doc;
    const bool parsed = response[0] != '\0' && !deserializeJson(doc, response);
    const char *result = parsed ? doc["data"]["result"] | "" : "";
    const bool granted = parsed &&
        (std::strcmp(result, "granted") == 0 || std::strcmp(result, "renewed") == 0);
    if (take_state_lock_(100)) {
      node_lease_state_[targets[t].index] = granted ? 1 : 2;
      give_state_lock_();
    }
    if (!granted)
      ESP_LOGW(TAG, "Lease fan-out to %s not granted (%s)", targets[t].host,
               parsed ? result : "no response");
  }
}

bool LuneTouchCoordinator::ingest_v6_zones_(size_t node_index, const char *body, uint32_t now_ms) {
  if (body == nullptr || body[0] == '\0')
    return false;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    ESP_LOGW(TAG, "V6 zones JSON parse failed: %s", err.c_str());
    return false;
  }

  JsonArray zones = doc["data"]["zones"].as<JsonArray>();
  if (zones.isNull())
    zones = doc["zones"].as<JsonArray>();
  if (node_index < ::lune_touch::MAX_NODES) {
    // Prefer a renamed board ("Lune V6" is the factory default), then its location.
    const char *dev_name = doc["data"]["device_name"] | "";
    const char *dev_place = doc["data"]["device_location"] | "";
    const bool named = dev_name[0] != '\0' && std::strcmp(dev_name, "Lune V6") != 0;
    const char *label = named ? dev_name : dev_place;
    std::strncpy(node_device_name_[node_index], label, sizeof(node_device_name_[node_index]) - 1);
    node_device_name_[node_index][sizeof(node_device_name_[node_index]) - 1] = '\0';
  }
  if (zones.isNull())
    return false;

  // Optional node-level heat_demand summary.
  JsonVariant heat_demand_obj = doc["data"]["heat_demand"];
  if (heat_demand_obj.isNull())
    heat_demand_obj = doc["heat_demand"];
  if (!heat_demand_obj.isNull()) {
    using namespace esphome::lune_touch_coordinator::v6_zones_parse;
    NodeHeatDemandFields fields{};
    fields.present = true;
    fields.critical_zone = heat_demand_obj["critical_zone"] | 0;
    fields.critical_opening_ratio = heat_demand_obj["critical_opening_ratio"] | 0.0f;
    fields.saturated_s = heat_demand_obj["saturated_s"] | 0U;
    fields.demanding_zones = static_cast<uint8_t>(heat_demand_obj["demanding_zones"] | 0);
    fields.headroom = heat_demand_obj["headroom"] | false;
    fields.recommendation = heat_demand_obj["recommendation"].as<const char *>();
    if (take_state_lock_(100)) {
      model_.update_node_heat_demand(node_index, to_node_heat_demand(fields, now_ms));
      give_state_lock_();
    }
  }

  if (!take_state_lock_(100))
    return false;

  size_t updated = 0;
  bool weather_boost_changed = false;
  for (JsonObject zone : zones) {
    using namespace esphome::lune_touch_coordinator::v6_zones_parse;
    ZoneFields fields{};
    int zone_number = zone["zone"] | 0;
    if (zone_number <= 0)
      zone_number = static_cast<int>(updated) + 1;
    if (zone_number < 1 || zone_number > static_cast<int>(::lune_touch::ZONES_PER_NODE))
      continue;
    fields.zone_number = zone_number;

    fields.has_temperature = !zone["temperature_c"].isNull();
    fields.has_setpoint = !zone["setpoint_c"].isNull();
    fields.has_valve = !zone["valve_pct"].isNull();
    fields.temperature_c = fields.has_temperature ? (zone["temperature_c"] | 0.0f) : 0.0f;
    fields.setpoint_c = fields.has_setpoint ? (zone["setpoint_c"] | 0.0f) : 0.0f;
    fields.valve_pct = fields.has_valve ? (zone["valve_pct"] | 0.0f) : 0.0f;
    fields.status = zone["state"].as<const char *>();
    if (fields.status == nullptr)
      fields.status = zone["status"] | "unknown";
    if (!zone["enabled"].isNull()) {
      fields.has_enabled = true;
      fields.enabled = zone["enabled"] | false;
    }
    fields.friendly_name = zone["friendly_name"].as<const char *>();
    if (fields.friendly_name == nullptr || fields.friendly_name[0] == '\0')
      fields.friendly_name = zone["name"].as<const char *>();
    fields.fresh = zone["fresh"] | true;
    const char *live_status = live_status_for_zone(fields);
    const bool live_fresh = (fields.has_enabled && !fields.enabled) ? false : fields.fresh;

    // Group membership (absent → legacy one-room-per-slot).
    if (!zone["group_primary"].isNull()) {
      fields.has_group_info = true;
      fields.group_primary_zone = zone["group_primary"] | zone_number;
      JsonArray members = zone["group_members"].as<JsonArray>();
      if (!members.isNull()) {
        int member_buf[6]{};
        size_t member_count = 0;
        for (JsonVariant m : members) {
          if (member_count >= 6)
            break;
          member_buf[member_count++] = m | 0;
        }
        fields.group_members_mask = members_mask_from_list(member_buf, member_count);
      }
    }

    if (!zone["opening_ratio"].isNull() || !zone["recommendation"].isNull() ||
        !zone["saturated_s"].isNull()) {
      fields.has_heat_demand = true;
      fields.demand_state = live_status;
      fields.opening_ratio = zone["opening_ratio"] | 0.0f;
      fields.has_opening_ratio = !zone["opening_ratio"].isNull();
      fields.saturated_s = zone["saturated_s"] | 0U;
      fields.headroom = zone["headroom"] | false;
      fields.recommendation = zone["recommendation"].as<const char *>();
    }

    const size_t zone_index = static_cast<size_t>(zone_number - 1);
    const char *v6_name = fields.friendly_name;
    if (v6_name != nullptr && v6_name[0] != '\0')
      model_.update_zone_name_from_v6_by_binding(node_index, zone_index, v6_name);

    const bool primary = is_group_primary(fields);
    bool stored = model_.update_zone_live_by_binding(
        node_index, zone_index, fields.temperature_c, fields.has_temperature, fields.setpoint_c,
        fields.has_setpoint, live_status, live_fresh, now_ms, fields.valve_pct, fields.has_valve);

    // Always attempt room attachment for proposable primaries — not only when the
    // binding is missing. An existing unassigned/empty-room slot otherwise keeps
    // receiving live telemetry forever while the UI shows "unused".
    if (should_auto_propose_room(fields)) {
      char room_id[32];
      char room_name[48];
      snprintf(room_id, sizeof(room_id), "v6%u-z%u", static_cast<unsigned>(node_index + 1),
               static_cast<unsigned>(zone_number));
      snprintf(room_name, sizeof(room_name), "%s",
               v6_name != nullptr && v6_name[0] != '\0' ? v6_name : room_id);
      if (model_.bind_zone_with_source(room_id, room_name, node_index, zone_index,
                                       v6_name != nullptr && v6_name[0] != '\0'
                                           ? ::lune_touch::ZoneNameSource::V6
                                           : ::lune_touch::ZoneNameSource::GENERATED)) {
        stored = model_.update_zone_live_by_binding(
                     node_index, zone_index, fields.temperature_c, fields.has_temperature,
                     fields.setpoint_c, fields.has_setpoint, live_status, live_fresh, now_ms,
                     fields.valve_pct, fields.has_valve) ||
                 stored;
      }
    } else if (!stored && !primary) {
      // Secondary member: track telemetry without creating a room.
      model_.bind_unassigned_group(node_index, zone_index, v6_name,
                                   v6_name != nullptr && v6_name[0] != '\0'
                                       ? ::lune_touch::ZoneNameSource::V6
                                       : ::lune_touch::ZoneNameSource::GENERATED);
      stored = model_.update_zone_live_by_binding(
          node_index, zone_index, fields.temperature_c, fields.has_temperature,
          fields.setpoint_c, fields.has_setpoint, live_status, live_fresh, now_ms,
          fields.valve_pct, fields.has_valve);
    }

    if (fields.has_group_info) {
      model_.update_zone_group_membership(node_index, zone_index,
                                          static_cast<uint8_t>(fields.group_primary_zone),
                                          fields.group_members_mask, primary);
    }
    if (fields.has_heat_demand) {
      model_.update_zone_heat_demand(
          node_index, zone_index, fields.demand_state, fields.opening_ratio, fields.saturated_s,
          fields.headroom, parse_recommendation(fields.recommendation), fields.fresh);
    }

    if (stored)
      updated++;

    JsonVariant forecast = zone["forecast"];
    const float wind_exposure = forecast["wind_exposure"] | zone["wind_exposure"] | 0.5f;
    const float solar_gain = forecast["solar_gain"] | zone["solar_gain"] | 0.3f;
    const uint8_t thermal_lead_h = forecast["thermal_lead_h"] | zone["thermal_lead_h"] | 4;
    const float max_offset_c = forecast["max_offset_c"] | zone["max_offset_c"] | 1.5f;
    if (!weather_max_boost_configured_ && std::isfinite(max_offset_c) && max_offset_c > 0.0f) {
      const float clamped = std::max(0.0f, std::min(5.0f, max_offset_c));
      weather_max_boost_c_ = weather_max_boost_seeded_from_v6_
                                 ? std::min(weather_max_boost_c_, clamped)
                                 : clamped;
      weather_max_boost_seeded_from_v6_ = true;
      weather_boost_changed = true;
    }
    // T1/T4: never reimport wind/solar after Touch is SoT. Seed once if needed.
    // thermal_lead / max_offset still refreshed from V6 for preload sizing.
    model_.update_zone_forecast_profile_by_binding(node_index, zone_index, 0, wind_exposure,
                                                   solar_gain, thermal_lead_h, max_offset_c,
                                                   false /* walls via physics mirror */,
                                                   false /* weather: Touch-owned */);
    for (size_t zi = 0; zi < model_.zone_count(); zi++) {
      const auto *zb = model_.zone(zi);
      if (zb == nullptr || !zb->enabled || zb->node_index != node_index ||
          zb->zone_index != zone_index)
        continue;
      model_.seed_room_weather_from_v6(zb->room_id, wind_exposure, solar_gain);
      break;
    }

    // T1: mirror V6 physics (walls/area/floor/UA). Graceful when fields absent.
    {
      uint8_t walls = 0;
      if (!zone["exterior_walls"].isNull())
        walls = static_cast<uint8_t>(zone["exterior_walls"] | 0);
      else if (!forecast["exterior_walls"].isNull())
        walls = static_cast<uint8_t>(forecast["exterior_walls"] | 0);
      float area_m2 = 0.0f;
      if (!zone["area_m2"].isNull())
        area_m2 = zone["area_m2"] | 0.0f;
      else if (!zone["settings"]["area_m2"].isNull())
        area_m2 = zone["settings"]["area_m2"] | 0.0f;
      ::lune_touch::FloorMirror floor{};
      floor.unset = true;
      std::strncpy(floor.slab_type, "unset", sizeof(floor.slab_type) - 1);
      std::strncpy(floor.covering, "unset", sizeof(floor.covering) - 1);
      JsonVariant floor_obj = zone["floor"];
      if (!floor_obj.isNull()) {
        floor.unset = floor_obj["unset"] | false;
        const char *slab = floor_obj["slab_type"].as<const char *>();
        const char *cover = floor_obj["covering"].as<const char *>();
        if (slab != nullptr)
          std::strncpy(floor.slab_type, slab, sizeof(floor.slab_type) - 1);
        if (cover != nullptr)
          std::strncpy(floor.covering, cover, sizeof(floor.covering) - 1);
        floor.active_thickness_cm = floor_obj["active_thickness_cm"] | 0.0f;
        if (!floor_obj["r_override_m2k_per_w"].isNull())
          floor.r_override_m2k_per_w = floor_obj["r_override_m2k_per_w"] | -1.0f;
        floor.c_slab_kwh_per_k = floor_obj["c_slab_kwh_per_k"] | floor_obj["c_kwh_per_k"] | 0.0f;
        floor.r_m2k_per_w = floor_obj["r_m2k_per_w"] | 0.0f;
        if (!(floor.r_m2k_per_w > 0.0f))
          floor.r_m2k_per_w = ::lune_touch::physics::effective_r_m2k_per_w(
              floor.covering, floor.r_override_m2k_per_w);
        if (!(floor.c_slab_kwh_per_k > 0.0f)) {
          const float area = area_m2 > 0.0f ? area_m2 : 0.0f;
          const float cpm = ::lune_touch::physics::c_slab_per_m2(floor.slab_type,
                                                                floor.active_thickness_cm);
          floor.c_slab_kwh_per_k = area * cpm;
        }
        if (!::lune_touch::physics::floor_provisioned(floor.slab_type, floor.covering))
          floor.unset = true;
      }
      const float ua_prior = zone["ua_prior_w_per_k"] | zone["ua_w_per_k"] | 0.0f;
      const float ua_learned = zone["ua_learned_w_per_k"] | 0.0f;
      const float ua_eff = zone["ua_effective_w_per_k"] | ua_prior;
      const float ua_conf =
          zone["ua_learned_confidence"] | zone["ua_confidence"] | 0.0f;
      const uint16_t ua_days = static_cast<uint16_t>(
          zone["ua_learned_observed_days"] | zone["ua_observed_days"] | 0);
      const uint32_t v6_rev = zone["revision"] | zone["data_revision"] | 0U;
      const char *ble = zone["ble_mac"].as<const char *>();
      const char *sid = zone["sensor_id"].as<const char *>();
      const char *gid = zone["group_id"].as<const char *>();
      bool conflict = false;
      if (walls != 0 || area_m2 > 0.0f || !floor_obj.isNull() || ua_prior > 0.0f ||
          ua_learned > 0.0f || (ble != nullptr && ble[0] != '\0') ||
          (sid != nullptr && sid[0] != '\0')) {
        model_.apply_v6_physics_mirror(node_index, zone_index, walls, area_m2, floor, ua_prior,
                                       ua_learned, ua_eff, ua_conf, ua_days, v6_rev,
                                       ble != nullptr ? ble : "", sid != nullptr ? sid : "",
                                       gid != nullptr ? gid : "", &conflict);
        if (conflict)
          learning_dirty_ = true;  // force event emit path to notice
      }
    }
  }
  if (updated > 0) {
    model_.mark_node_seen(node_index, now_ms);
    learning_dirty_ = true;
  }
  give_state_lock_();
  if (weather_boost_changed)
    save_settings_();
  maybe_migrate_walls_to_v6_();
  return updated > 0;
}

bool LuneTouchCoordinator::ingest_v6_legacy_state_(size_t node_index, const ::lune_touch::PairedNode &node,
                                                   const char *body, uint32_t now_ms) {
  if (body == nullptr || body[0] == '\0')
    return false;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    ESP_LOGW(TAG, "V6 legacy state JSON parse failed: %s", err.c_str());
    return false;
  }

  if (!take_state_lock_(100))
    return false;

  const char *firmware = entity_state_(doc, "text_sensor-firmware_version");
  const char *mac = entity_state_(doc, "text_sensor-mac_address");
  char identity[24]{};
  canonicalize_pairing_fingerprint_(mac, identity, sizeof(identity));
  model_.update_node_metadata(node_index, "lune-v6", firmware, nullptr);
  if (identity[0] != '\0')
    model_.update_node_identity(node_index, identity);
  NodeTelemetryState &telemetry = node_telemetry_[node_index];
  if (entity_number_(doc, "sensor-manifold_flow_temperature", &telemetry.flow_c))
    telemetry.has_flow = true;
  if (entity_number_(doc, "sensor-manifold_return_temperature", &telemetry.return_c))
    telemetry.has_return = true;
  telemetry.drivers_enabled = state_is_on_(entity_state_(doc, "switch-motor_drivers_enabled"));
  telemetry.has_drivers_enabled = true;
  telemetry.motor_fault = false;
  telemetry.has_motor_fault = true;

  size_t updated = 0;
  float valve_sum = 0.0f;
  size_t valve_count = 0;
  uint8_t active_zones = 0;
  for (size_t zone_number = 1; zone_number <= ::lune_touch::ZONES_PER_NODE; zone_number++) {
    char temp_key[40];
    char setpoint_key[40];
    char state_key[40];
    char enabled_key[40];
    char valve_key[40];
    char fault_key[48];
    snprintf(temp_key, sizeof(temp_key), "sensor-zone_%u_temperature", static_cast<unsigned>(zone_number));
    snprintf(setpoint_key, sizeof(setpoint_key), "number-zone_%u_setpoint", static_cast<unsigned>(zone_number));
    snprintf(state_key, sizeof(state_key), "text_sensor-zone_%u_state", static_cast<unsigned>(zone_number));
    snprintf(enabled_key, sizeof(enabled_key), "switch-zone_%u_enabled", static_cast<unsigned>(zone_number));
    snprintf(valve_key, sizeof(valve_key), "sensor-zone_%u_valve_pct", static_cast<unsigned>(zone_number));
    snprintf(fault_key, sizeof(fault_key), "text_sensor-motor_%u_last_fault", static_cast<unsigned>(zone_number));

    float temp = 0.0f;
    float setpoint = 0.0f;
    float valve_pct = 0.0f;
    const bool has_temp = entity_number_(doc, temp_key, &temp);
    const bool has_setpoint = entity_number_(doc, setpoint_key, &setpoint);
    const bool has_valve = entity_number_(doc, valve_key, &valve_pct);
    if (has_valve) {
      valve_sum += valve_pct;
      valve_count++;
      if (valve_pct > 0.5f)
        active_zones++;
    }
    const char *fault = entity_state_(doc, fault_key);
    if (fault != nullptr && fault[0] != '\0' && std::strcmp(fault, "NONE") != 0 &&
        std::strcmp(fault, "none") != 0 && std::strcmp(fault, "OK") != 0 &&
        std::strcmp(fault, "ok") != 0) {
      telemetry.motor_fault = true;
    }
    const bool enabled = state_is_on_(entity_state_(doc, enabled_key));
    if (!has_temp && !has_setpoint)
      continue;

    char status[16];
    legacy_zone_status_(entity_state_(doc, state_key), enabled, has_temp, temp, has_setpoint, setpoint,
                        status, sizeof(status));

    const size_t zone_index = zone_number - 1;
    bool stored = model_.update_zone_live_by_binding(node_index, zone_index, temp, has_temp,
                                                     setpoint, has_setpoint, status,
                                                     enabled && has_temp, now_ms, valve_pct, has_valve);
    if (!stored) {
      char room_id[32];
      char room_name[48];
      snprintf(room_id, sizeof(room_id), "v6%u-z%u", static_cast<unsigned>(node_index + 1),
               static_cast<unsigned>(zone_number));
      snprintf(room_name, sizeof(room_name), "%s Z%u",
               node.node_id[0] != '\0' ? node.node_id : "V6",
               static_cast<unsigned>(zone_number));
      if (model_.bind_zone_with_source(room_id, room_name, node_index, zone_index,
                                       ::lune_touch::ZoneNameSource::GENERATED)) {
        stored = model_.update_zone_live_by_binding(node_index, zone_index, temp, has_temp,
                                                    setpoint, has_setpoint, status,
                                                    enabled && has_temp, now_ms, valve_pct, has_valve);
      }
    }
    if (stored)
      updated++;
  }
  if (valve_count > 0) {
    telemetry.avg_valve_pct = valve_sum / static_cast<float>(valve_count);
    telemetry.has_avg_valve = true;
    telemetry.active_zones = active_zones;
  }

  if (updated > 0) {
    model_.mark_node_seen(node_index, now_ms);
    learning_dirty_ = true;
  }
  give_state_lock_();
  return updated > 0;
}

bool LuneTouchCoordinator::fetch_open_meteo_(float latitude, float longitude, char *error, size_t error_len,
                                             uint8_t *hours_count, float *min_temp_c, float *max_wind_ms,
                                             float *peak_wind_dir_deg, float *max_solar_wm2,
                                             char *provider_timezone, size_t provider_timezone_capacity,
                                             ForecastHourState *hours_out, size_t hours_capacity) {
  if (error != nullptr && error_len > 0)
    error[0] = '\0';
  if (hours_count != nullptr)
    *hours_count = 0;
  if (provider_timezone != nullptr && provider_timezone_capacity > 0)
    provider_timezone[0] = '\0';

  char url[384];
  snprintf(url, sizeof(url),
           "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
           "&hourly=temperature_2m,wind_speed_10m,wind_direction_10m,shortwave_radiation,"
           "precipitation,cloud_cover"
           "&forecast_days=3&timeformat=unixtime&wind_speed_unit=ms&timezone=auto",
           latitude, longitude);

  esp_http_client_config_t cfg{};
  cfg.url = url;
  cfg.method = HTTP_METHOD_GET;
  cfg.timeout_ms = 8000;
  cfg.disable_auto_redirect = true;
  cfg.crt_bundle_attach = esp_crt_bundle_attach;
  cfg.buffer_size = 2048;

  constexpr size_t BODY_CAP = 24576;
  char *body = alloc_http_body_(BODY_CAP);
  if (body == nullptr) {
    snprintf(error, error_len, "no_body_heap");
    return false;
  }

  esp_http_client_handle_t client = esp_http_client_init(&cfg);
  if (client == nullptr) {
    heap_caps_free(body);
    snprintf(error, error_len, "http_init_failed");
    return false;
  }

  bool ok = false;
  esp_err_t err = esp_http_client_open(client, 0);
  if (err == ESP_OK) {
    esp_http_client_fetch_headers(client);
    const int status = esp_http_client_get_status_code(client);
    const int len = esp_http_client_read_response(client, body, BODY_CAP - 1);
    if (status == 200 && len > 0) {
      body[len] = '\0';

      JsonDocument filter;
      filter["timezone"] = true;
      filter["hourly"]["time"] = true;
      filter["hourly"]["temperature_2m"] = true;
      filter["hourly"]["wind_speed_10m"] = true;
      filter["hourly"]["wind_direction_10m"] = true;
      filter["hourly"]["shortwave_radiation"] = true;
      filter["hourly"]["precipitation"] = true;
      filter["hourly"]["cloud_cover"] = true;

      JsonDocument doc;
      DeserializationError jerr = deserializeJson(doc, body, len, DeserializationOption::Filter(filter));
      if (!jerr) {
        JsonArray times = doc["hourly"]["time"].as<JsonArray>();
        JsonArray temps = doc["hourly"]["temperature_2m"].as<JsonArray>();
        JsonArray winds = doc["hourly"]["wind_speed_10m"].as<JsonArray>();
        JsonArray dirs = doc["hourly"]["wind_direction_10m"].as<JsonArray>();
        JsonArray solar = doc["hourly"]["shortwave_radiation"].as<JsonArray>();
        JsonArray precip = doc["hourly"]["precipitation"].as<JsonArray>();
        JsonArray clouds = doc["hourly"]["cloud_cover"].as<JsonArray>();
        const char *timezone = doc["timezone"] | "";
        const size_t count = temps.size();
        if (times.size() == 0) {
          snprintf(error, error_len, "missing_hourly_time");
        } else if (!lune_touch_forecast_timeline::hourly_arrays_match(
                       times.size(), temps.size(), winds.size(), dirs.size(), solar.size(),
                       precip.size(), clouds.size())) {
          snprintf(error, error_len, "hourly_arrays_mismatch");
        } else if (!lune_touch_forecast_timeline::provider_timezone_valid(timezone)) {
          snprintf(error, error_len, "missing_timezone");
        } else if (count >= 24 && hours_out != nullptr && hours_capacity >= 24) {
          float min_temp = temps[0] | 0.0f;
          float max_wind = 0.0f;
          float wind_dir = 0.0f;
          float max_solar = 0.0f;
          uint8_t kept = 0;
          int64_t timestamps[72]{};
          for (size_t i = 0; i < count && kept < 72; i++, kept++) {
            const int64_t timestamp_s = times[i] | 0LL;
            const float temp = temps[i] | 0.0f;
            const float wind = winds[i] | 0.0f;
            const float dir = dirs[i] | 0.0f;
            const float sun = solar[i] | 0.0f;
            const float rain = precip[i] | 0.0f;
            const float cover = clouds[i] | 0.0f;
            if (kept >= hours_capacity)
              break;
            timestamps[kept] = timestamp_s;
            hours_out[kept] = {timestamp_s, temp, wind, dir, sun, rain, cover};
            if (temp < min_temp)
              min_temp = temp;
            if (wind > max_wind) {
              max_wind = wind;
              wind_dir = dir;
            }
            if (sun > max_solar)
              max_solar = sun;
          }
          if (!lune_touch_forecast_timeline::timestamps_are_consecutive_hours(timestamps, kept)) {
            snprintf(error, error_len, "hourly_time_unalignable");
          } else {
            if (hours_count != nullptr)
              *hours_count = kept;
            if (min_temp_c != nullptr)
              *min_temp_c = min_temp;
            if (max_wind_ms != nullptr)
              *max_wind_ms = max_wind;
            if (peak_wind_dir_deg != nullptr)
              *peak_wind_dir_deg = wind_dir;
            if (max_solar_wm2 != nullptr)
              *max_solar_wm2 = max_solar;
            if (provider_timezone != nullptr && provider_timezone_capacity > 0) {
              std::strncpy(provider_timezone, timezone, provider_timezone_capacity - 1);
              provider_timezone[provider_timezone_capacity - 1] = '\0';
            }
            ok = true;
          }
        } else {
          snprintf(error, error_len, "short_forecast");
        }
      } else {
        snprintf(error, error_len, "json_%s", jerr.c_str());
      }
    } else {
      snprintf(error, error_len, "http_%d_len_%d", status, len);
    }
  } else {
    snprintf(error, error_len, "%s", esp_err_to_name(err));
  }

  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  heap_caps_free(body);
  return ok;
}

void LuneTouchCoordinator::ensure_preload_learning_defaults_() {
  if (preload_gain_scale_init_)
    return;
  for (size_t i = 0; i < ::lune_touch::MAX_HOUSE_ZONES; i++) {
    preload_gain_scale_[i] = 1.0f;
    preload_lead_bias_h_[i] = 0;
  }
  preload_gain_scale_init_ = true;
}

bool LuneTouchCoordinator::load_preload_learning_() {
  ensure_preload_learning_defaults_();
  nvs_handle_t handle = 0;
  if (nvs_open(WEATHER_NAMESPACE, NVS_READONLY, &handle) != ESP_OK)
    return false;
  PersistedPreloadLearning blob{};
  size_t len = sizeof(blob);
  const esp_err_t err = nvs_get_blob(handle, "preload_lrn", &blob, &len);
  nvs_close(handle);
  if (err != ESP_OK || len != sizeof(blob) || blob.magic != PRELOAD_LEARN_MAGIC ||
      blob.version != PRELOAD_LEARN_VERSION)
    return false;
  for (uint16_t i = 0; i < blob.count && i < ::lune_touch::MAX_HOUSE_ZONES; i++) {
    if (blob.zones[i].room_id[0] == '\0')
      continue;
    for (size_t z = 0; z < model_.zone_count(); z++) {
      const auto *zone = model_.zone(z);
      if (zone == nullptr || std::strcmp(zone->room_id, blob.zones[i].room_id) != 0)
        continue;
      preload_gain_scale_[z] = std::isfinite(blob.zones[i].gain_scale)
                                   ? std::fmax(0.5f, std::fmin(1.5f, blob.zones[i].gain_scale))
                                   : 1.0f;
      preload_lead_bias_h_[z] =
          blob.zones[i].lead_bias_h < 0
              ? 0
              : (blob.zones[i].lead_bias_h > 8 ? 8 : blob.zones[i].lead_bias_h);
      break;
    }
  }
  return true;
}

bool LuneTouchCoordinator::save_preload_learning_() {
  ensure_preload_learning_defaults_();
  PersistedPreloadLearning blob{};
  blob.count = 0;
  for (size_t z = 0; z < model_.zone_count() && blob.count < ::lune_touch::MAX_HOUSE_ZONES; z++) {
    const auto *zone = model_.zone(z);
    if (zone == nullptr || zone->room_id[0] == '\0')
      continue;
    auto &entry = blob.zones[blob.count++];
    std::strncpy(entry.room_id, zone->room_id, sizeof(entry.room_id) - 1);
    entry.gain_scale = preload_gain_scale_[z];
    entry.lead_bias_h = preload_lead_bias_h_[z];
  }
  nvs_handle_t handle = 0;
  if (nvs_open(WEATHER_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK)
    return false;
  const esp_err_t err = nvs_set_blob(handle, "preload_lrn", &blob, sizeof(blob));
  if (err == ESP_OK)
    nvs_commit(handle);
  nvs_close(handle);
  return err == ESP_OK;
}

void LuneTouchCoordinator::apply_preload_outcome_learning_() {
  ensure_preload_learning_defaults_();
  uint8_t day_index = 0;
  uint16_t minute_of_day = 0;
  const bool time_valid = current_schedule_time_(time_, &day_index, &minute_of_day);
  bool changed = false;
  for (size_t i = 0; i < forecast_decision_count_; i++) {
    const ForecastDecisionState &decision = forecast_decisions_[i];
    if (!decision.active)
      continue;
    size_t zone_index = ::lune_touch::MAX_HOUSE_ZONES;
    for (size_t z = 0; z < model_.zone_count(); z++) {
      const auto *zone = model_.zone(z);
      if (zone != nullptr && std::strcmp(zone->room_id, decision.room_id) == 0) {
        zone_index = z;
        break;
      }
    }
    if (zone_index >= ::lune_touch::MAX_HOUSE_ZONES)
      continue;
    const auto *zone = model_.zone(zone_index);
    const auto *live = model_.zone_live(zone_index);
    if (zone == nullptr || live == nullptr || !live->fresh || !live->has_temperature)
      continue;
    const auto effective =
        ::lune_touch::HouseModel::effective_comfort(*zone, time_valid, day_index, minute_of_day);
    hv6fc::PreloadOutcome outcome{};
    outcome.preload_was_active = true;
    const float band_lo = effective.setpoint_c - COMFORT_BAND_C;
    const float band_hi = effective.setpoint_c + COMFORT_BAND_C + decision.offset_c;
    if (live->temperature_c < band_lo)
      outcome.undershoot_c = band_lo - live->temperature_c;
    if (live->temperature_c > band_hi)
      outcome.overshoot_c = live->temperature_c - band_hi;
    if (outcome.undershoot_c <= 0.0f && outcome.overshoot_c <= 0.0f)
      continue;
    const hv6fc::LearningUpdate update = hv6fc::update_preload_learning(
        preload_gain_scale_[zone_index], preload_lead_bias_h_[zone_index], outcome);
    if (std::fabs(update.gain_scale - preload_gain_scale_[zone_index]) > 0.001f ||
        update.lead_bias_h != preload_lead_bias_h_[zone_index]) {
      preload_gain_scale_[zone_index] = update.gain_scale;
      preload_lead_bias_h_[zone_index] = update.lead_bias_h;
      changed = true;
    }
  }
  if (changed) {
    preload_learning_dirty_ = true;
    learning_dirty_ = true;
  }
}

void LuneTouchCoordinator::recompute_forecast_decisions_() {
  forecast_decision_count_ = 0;
  if (forecast_hours_count_ == 0)
    return;
  ensure_preload_learning_defaults_();

  int64_t timestamps[72]{};
  for (uint8_t i = 0; i < forecast_hours_count_; i++)
    timestamps[i] = forecast_hours_[i].timestamp_s;
  const size_t now_index = lune_touch_forecast_timeline::first_index_at_or_after(
      timestamps, forecast_hours_count_, current_epoch_s_());
  // A forecast with no wall-clock alignment must never create a preload command.
  if (now_index == lune_touch_forecast_timeline::NO_INDEX)
    return;

  uint8_t day_index = 0;
  uint16_t minute_of_day = 0;
  const bool time_valid = current_schedule_time_(time_, &day_index, &minute_of_day);

  hv6fc::ForecastHour model_hours[72]{};
  const size_t hour_count = std::min<size_t>(forecast_hours_count_, 72);
  for (size_t i = 0; i < hour_count; i++) {
    model_hours[i].temp_c = forecast_hours_[i].temp_c;
    model_hours[i].wind_speed_ms = forecast_hours_[i].wind_speed_ms;
    model_hours[i].wind_dir_deg = forecast_hours_[i].wind_dir_deg;
    model_hours[i].shortwave_wm2 = forecast_hours_[i].shortwave_wm2;
  }

  for (size_t i = 0; i < model_.zone_count() &&
                     forecast_decision_count_ < ::lune_touch::MAX_HOUSE_ZONES; i++) {
    const auto *zone = model_.zone(i);
    const auto *live = model_.zone_live(i);
    if (zone == nullptr || !zone->enabled)
      continue;

    ForecastDecisionState &out = forecast_decisions_[forecast_decision_count_++];
    std::strncpy(out.room_id, zone->room_id, sizeof(out.room_id) - 1);
    out.room_id[sizeof(out.room_id) - 1] = '\0';
    std::strncpy(out.room_name, zone->room_name, sizeof(out.room_name) - 1);
    out.room_name[sizeof(out.room_name) - 1] = '\0';
    out.node_index = zone->node_index;
    out.zone_index = zone->zone_index;
    const auto effective = ::lune_touch::HouseModel::effective_comfort(*zone, time_valid,
                                                                       day_index, minute_of_day);
    out.comfort_setpoint_c = effective.setpoint_c;
    out.priority = zone->priority;
    out.configured_thermal_lead_h = zone->thermal_lead_h;
    out.learned_thermal_lead_h = ::lune_touch::HouseModel::learned_thermal_lead_h(*zone);
    const uint8_t house_active_lead = ::lune_touch::HouseModel::active_thermal_lead_h(*zone);
    out.active_thermal_lead_h = house_active_lead;
    if (live == nullptr || !live->fresh)
      continue;

    hv6fc::ZoneExposure exposure{};
    // T4: walls from V6 union on the logical room; wind/solar from Touch room SoT.
    const auto *room = model_.room_by_id(zone->room_id);
    exposure.exterior_walls =
        room != nullptr && room->exterior_walls_union != 0 ? room->exterior_walls_union
                                                          : zone->exterior_walls;
    exposure.wind_exposure = room != nullptr ? room->wind_exposure : zone->wind_exposure;
    exposure.solar_gain = room != nullptr ? room->solar_gain : zone->solar_gain;
    exposure.thermal_lead_h = house_active_lead;

    hv6fc::PreloadParams params{};
    params.indoor_ref_c = out.comfort_setpoint_c;
    params.load_threshold = LOAD_THRESHOLD;
    params.gain_c_per_load = GAIN_C_PER_LOAD;
    params.max_offset_c = weather_max_boost_c_;
    params.priority_gain =
        1.0f + 0.1f * static_cast<float>(out.priority > 0 ? out.priority - 1 : 0);
    params.learned_heat_gain_c_per_h = zone->learned_heat_gain_c_per_h;
    params.learned_cool_loss_c_per_h = zone->learned_cool_loss_c_per_h;
    params.thermal_samples = zone->thermal_samples;
    params.learned_gain_scale =
        preload_gain_scale_[i] >= 0.5f ? preload_gain_scale_[i] : 1.0f;
    params.lead_bias_h = preload_lead_bias_h_[i] < 0 ? 0 : preload_lead_bias_h_[i];
    params.scheduled_setpoint_c = out.comfort_setpoint_c;
    params.comfort_band_c = COMFORT_BAND_C;
    params.enable_horizon = true;
    if (live->has_temperature && std::isfinite(live->temperature_c)) {
      params.has_current_temp = true;
      params.current_temp_c = live->temperature_c;
    }

    const hv6fc::PreloadDecision decision =
        hv6fc::compute_zone_preload(model_hours, hour_count, now_index, exposure, params);

    out.offset_c = decision.offset_c;
    out.peak_load = decision.peak_load;
    out.peak_in_h = decision.peak_in_h;
    out.timing_scale = decision.timing_scale;
    out.heuristic_offset_c = decision.heuristic_offset_c;
    out.min_temp_c = decision.min_temp_c;
    out.horizon_ok = decision.horizon_ok;
    out.used_horizon = decision.used_horizon;
    out.used_thermal_sizing = decision.used_thermal_sizing;
    out.preload_start_h = decision.preload_start_h;
    out.preload_end_h = decision.preload_end_h;
    if (decision.active_lead_h > 0)
      out.active_thermal_lead_h = decision.active_lead_h;
    out.active = decision.offset_c > 0.01f;
    apply_odin_timing_bias_(&out);

    // Slab charge: room-level wind/cold shortfall against floor capacity.
    {
      lune_touch_charge::Hour ch[72]{};
      for (size_t k = 0; k < hour_count; k++) {
        ch[k].temp_c = model_hours[k].temp_c;
        ch[k].wind_ms = model_hours[k].wind_speed_ms;
        ch[k].wind_dir_deg = model_hours[k].wind_dir_deg;
        ch[k].shortwave_wm2 = model_hours[k].shortwave_wm2;
      }
      lune_touch_charge::Room room_in{};
      room_in.area_m2 = zone->served_area_m2 > 0.0f ? zone->served_area_m2 : zone->v6_area_m2;
      room_in.ua_w_per_k = zone->ua_effective_w_per_k > 0.0f ? zone->ua_effective_w_per_k
                                                             : zone->ua_prior_w_per_k;
      if (!zone->floor.unset && zone->floor.r_m2k_per_w > 0.0f)
        room_in.floor_r_m2k_per_w = zone->floor.r_m2k_per_w;
      if (!zone->floor.unset && zone->floor.c_slab_kwh_per_k > 0.0f)
        room_in.c_slab_kwh_per_k = zone->floor.c_slab_kwh_per_k;
      room_in.exterior_walls = exposure.exterior_walls;
      room_in.wind_exposure = exposure.wind_exposure;
      room_in.solar_gain = exposure.solar_gain;
      room_in.setpoint_c = out.comfort_setpoint_c;
      if (params.has_current_temp)
        room_in.temp_c = params.current_temp_c;
      lune_touch_charge::Params cp{};
      cp.max_store_c = weather_max_boost_c_ > 0.0f ? weather_max_boost_c_ : 1.5f;
      const auto c = lune_touch_charge::plan(ch, hour_count, now_index, room_in, cp);
      out.charge_episode = c.episode;
      out.charge_now = c.charge_now;
      out.charge_insufficient = c.insufficient;
      out.charge_store_c = c.store_c;
      out.charge_deficit_kwh = c.deficit_kwh;
      out.floor_capacity_w = c.floor_capacity_w;
      out.peak_loss_w = c.peak_loss_w;
      out.charge_start_in_h = c.start_in_h;
      out.charge_episode_in_h = c.episode_in_h;
      out.charge_end_in_h = c.episode_end_in_h;
      if (c.charge_now) {
        // Raise the setpoint enough to store the shortfall (never above the
        // house max boost); absorb arm keeps the valve open while warm.
        out.offset_c = std::max(out.offset_c, std::min(c.store_c, cp.max_store_c));
        out.active = out.offset_c > 0.01f;
      }
    }
  }

  apply_preload_outcome_learning_();
}

ForecastDispatchSummary LuneTouchCoordinator::dispatch_forecast_commands_() {
  struct DispatchItem {
    ForecastDecisionState decision{};
    ::lune_touch::PairedNode node{};
    char preferred_host[64]{};
  };

  ForecastDispatchSummary summary{};
  // ~11 KB (decision + node copy per zone): off the forecast task stack. With
  // the slab-charge fields it no longer fit next to the HTTP calls and crashed
  // the Touch in a boot loop. Only the forecast task calls this.
  static DispatchItem *items = psram_scratch_<DispatchItem>(::lune_touch::MAX_HOUSE_ZONES);
  if (items == nullptr)
    return summary;
  for (size_t i = 0; i < ::lune_touch::MAX_HOUSE_ZONES; i++)
    items[i] = DispatchItem{};
  size_t item_count = 0;
  const uint32_t now = esphome::millis();
  bool ledger_changed = false;

  if (!take_state_lock_(100))
    return summary;
  for (size_t i = 0; i < forecast_decision_count_ && item_count < ::lune_touch::MAX_HOUSE_ZONES; i++) {
    const ForecastDecisionState &decision = forecast_decisions_[i];
    if (!decision.active || decision.offset_c <= 0.01f)
      continue;
    summary.active++;
    const auto target = model_.resolve_room(decision.room_id);
    if (target.node == nullptr || target.binding == nullptr)
      continue;
    if (ledger_.has_recent_similar(
            decision.odin_timing_bias ? "odin_timing_bias" : "forecast", target.node->node_id,
            target.binding->loop_id, decision.offset_c, now, FORECAST_COMMAND_DEDUPE_MS,
            FORECAST_COMMAND_EPSILON_C, current_epoch_s_())) {
      summary.skipped++;
      continue;
    }
    auto append_blocked_record = [&](::lune_touch::CommandResult result, const char *reason) {
      ::lune_touch::CommandRecord record{};
      snprintf(record.request_id, sizeof(record.request_id), "fb-%lu-%02u",
               static_cast<unsigned long>(now), static_cast<unsigned>(i));
      std::strncpy(record.source, decision.odin_timing_bias ? "odin_timing_bias" : "forecast",
                   sizeof(record.source) - 1);
      std::strncpy(record.reason, reason, sizeof(record.reason) - 1);
      record.node_index = decision.node_index;
      record.zone_index = decision.zone_index;
      record.requested_offset_c = decision.offset_c;
      record.accepted_offset_c = 0.0f;
      stamp_command_timing_(&record, now, 0);
      const auto target = model_.resolve_room(decision.room_id);
      stamp_command_target_(&record, target.node, target.binding);
      record.result = result;
      ledger_.append(record);
      ledger_changed = true;
    };
    const auto *node = model_.node(decision.node_index);
    if (node == nullptr || (!node->reachable && std::strcmp(node->firmware, "mock") != 0)) {
      summary.blocked_unreachable++;
      summary.skipped++;
      append_blocked_record(::lune_touch::CommandResult::BLOCKED_UNREACHABLE,
                            "forecast blocked: node unreachable");
      continue;
    }
    if (node->trust != ::lune_touch::NodeTrust::TRUSTED) {
      summary.blocked_untrusted++;
      summary.skipped++;
      append_blocked_record(::lune_touch::CommandResult::BLOCKED_UNTRUSTED,
                            "forecast blocked: node untrusted");
      continue;
    }
    if (model_.is_node_stale(decision.node_index, now)) {
      summary.blocked_stale++;
      summary.skipped++;
      append_blocked_record(::lune_touch::CommandResult::BLOCKED_STALE,
                            "forecast blocked: node stale");
      continue;
    }
    items[item_count].decision = decision;
    items[item_count].node = *node;
    if (decision.node_index < ::lune_touch::MAX_NODES) {
      std::strncpy(items[item_count].preferred_host, node_last_success_host_[decision.node_index],
                   sizeof(items[item_count].preferred_host) - 1);
      items[item_count].preferred_host[sizeof(items[item_count].preferred_host) - 1] = '\0';
    }
    item_count++;
  }
  give_state_lock_();

  for (size_t i = 0; i < item_count; i++) {
    const ForecastDecisionState &decision = items[i].decision;
    const ::lune_touch::PairedNode &node = items[i].node;

    ::lune_touch::CommandRecord record{};
    snprintf(record.request_id, sizeof(record.request_id), "fc-%lu-%02u",
             static_cast<unsigned long>(now), static_cast<unsigned>(i));
    std::strncpy(record.source, decision.odin_timing_bias ? "odin_timing_bias" : "forecast",
                 sizeof(record.source) - 1);
    if (decision.odin_timing_bias)
      snprintf(record.reason, sizeof(record.reason), "odin heat window + weather peak in %dh",
               static_cast<int>(decision.peak_in_h));
    else
      snprintf(record.reason, sizeof(record.reason), "weather peak in %dh",
               static_cast<int>(decision.peak_in_h));
    record.node_index = decision.node_index;
    record.zone_index = decision.zone_index;
    record.requested_offset_c = decision.offset_c;
    stamp_command_timing_(&record, now, FORECAST_COMMAND_TTL_S);
    const auto target = model_.resolve_room(decision.room_id);
    stamp_command_target_(&record, target.node, target.binding);
    record.result = ::lune_touch::CommandResult::PENDING;

    ::lune_touch::CommandRecord final_record = record;
    const bool sent = send_v6_setpoint_command_(node, decision.zone_index, record,
                                                FORECAST_COMMAND_TTL_S, &final_record,
                                                items[i].preferred_host);
    if (!sent)
      final_record.result = ::lune_touch::CommandResult::FAILED;

    if (final_record.result == ::lune_touch::CommandResult::ACCEPTED ||
        final_record.result == ::lune_touch::CommandResult::PENDING) {
      summary.sent++;
    } else {
      summary.failed++;
    }
    if (take_state_lock_(100)) {
      ledger_.append(final_record);
      give_state_lock_();
    } else {
      ledger_.append(final_record);
    }
    ledger_changed = true;
  }

  if (ledger_changed)
    save_ledger_();

  if (take_state_lock_(100)) {
    last_forecast_dispatch_ = summary;
    give_state_lock_();
  }
  return summary;
}

void LuneTouchCoordinator::apply_odin_timing_bias_(ForecastDecisionState *decision) {
  if (decision == nullptr || !odin_plan_active_() || !odin_plan_.preload_timing_bias_enabled ||
      !odin_plan_.available)
    return;
  if (!odin_plan_.heat_window_active || decision->offset_c <= 0.01f)
    return;
  const float cap = std::fmax(0.0f, odin_plan_.preload_bias_offset_cap_c);
  // Weather preload already sized the offset; while ODIN runs space heat, allow a
  // small additional bias (≤ cap) without exceeding the house weather boost ceiling.
  if (cap > 0.0f) {
    const float boosted = std::fmin(weather_max_boost_c_, decision->offset_c + cap);
    decision->offset_c = boosted;
  }
  decision->odin_timing_bias = true;
  decision->active = decision->offset_c > 0.01f;
}

void LuneTouchCoordinator::maybe_dispatch_absorb_arm_() {
  if (!odin_plan_active_() || !odin_plan_.absorb_arm_enabled || !odin_plan_.available)
    return;
  if (odin_defrost_blocks_arm_()) {
    maybe_dispatch_absorb_disarm_("defrost");
    return;
  }
  if (odin_mqtt_.odin_online_known && !odin_mqtt_.odin_online) {
    maybe_dispatch_absorb_disarm_("odin_offline");
    return;
  }
  const uint32_t now = esphome::millis();
  if (odin_plan_.last_absorb_arm_ms != 0 && now - odin_plan_.last_absorb_arm_ms < 30UL * 60UL * 1000UL)
    return;
  bool upcoming = odin_plan_.heat_window_active;
  char reason_buf[48]{"odin_plan heat window"};
  if (odin_plan_.current_decision_reason[0] != '\0' &&
      std::strcmp(odin_plan_.current_decision_reason, "unknown") != 0) {
    std::snprintf(reason_buf, sizeof(reason_buf), "%s", odin_plan_.current_decision_reason);
  }
  for (uint8_t h = 0; !upcoming && h < odin_plan_.heat_production_count && h < 2; h++) {
    plan_source::PlanView view{};
    view.source = std::strcmp(odin_plan_.source_id, "odin_forecast_v2") == 0
                      ? plan_source::SourceId::OdinForecastV2
                      : plan_source::SourceId::AsgardDashboardV1;
    view.heat_production[0] = odin_plan_.heat_production_horizon[h];
    view.operation_mode[0] = odin_plan_.operation_mode_horizon[h];
    view.has_decision_reason = view.source == plan_source::SourceId::OdinForecastV2;
    view.decision_reason[0] =
        plan_source::decision_reason_from_string(odin_plan_.decision_reason_horizon[h]);
    if (plan_source::may_arm_absorb(view, 0, odin_plan_.arm_energy_cost_modulation)) {
      upcoming = true;
      if (view.has_decision_reason)
        std::snprintf(reason_buf, sizeof(reason_buf), "%s",
                      plan_source::decision_reason_name(view.decision_reason[0]));
    }
  }
  if (!upcoming)
    return;

  bool any = false;
  for (size_t zi = 0; zi < model_.zone_count(); zi++) {
    const auto *zone = model_.zone(zi);
    if (zone == nullptr || !zone->enabled || !zone->commissioned)
      continue;
    const auto *node = model_.node(zone->node_index);
    if (node == nullptr || node->trust != ::lune_touch::NodeTrust::TRUSTED)
      continue;
    ::lune_touch::CommandRecord record{};
    snprintf(record.request_id, sizeof(record.request_id), "aa-%lu-%02u",
             static_cast<unsigned long>(now), static_cast<unsigned>(zi));
    std::strncpy(record.source, "absorb_arm", sizeof(record.source) - 1);
    std::strncpy(record.reason, reason_buf, sizeof(record.reason) - 1);
    record.node_index = zone->node_index;
    record.zone_index = zone->zone_index;
    record.requested_offset_c = 0.0f;
    stamp_command_timing_(&record, now, 3600);
    const auto target = model_.resolve_room(zone->room_id);
    stamp_command_target_(&record, target.node, target.binding);
    ::lune_touch::CommandRecord final_record = record;
    send_v6_absorb_arm_(*node, zone->zone_index, 3600, &final_record, reason_buf);
    ledger_.append(final_record);
    any = true;
  }
  if (any) {
    odin_plan_.last_absorb_arm_ms = now;
    odin_plan_.absorb_armed = true;
    std::strncpy(odin_plan_.absorb_arm_reason, reason_buf, sizeof(odin_plan_.absorb_arm_reason) - 1);
    save_ledger_();
    log_event_("info", "absorb_arm", "absorb-arm sent (Odin heat window)");
  }
}

void LuneTouchCoordinator::maybe_dispatch_absorb_disarm_(const char *reason) {
  if (!odin_plan_.absorb_armed && !odin_plan_.absorb_arm_enabled)
    return;
  const uint32_t now = esphome::millis();
  if (odin_plan_.last_absorb_disarm_ms != 0 && now - odin_plan_.last_absorb_disarm_ms < 1000UL)
    return;
  const char *why = (reason != nullptr && reason[0] != '\0') ? reason : "disarm";
  bool any = false;
  for (size_t zi = 0; zi < model_.zone_count(); zi++) {
    const auto *zone = model_.zone(zi);
    if (zone == nullptr || !zone->enabled || !zone->commissioned)
      continue;
    const auto *node = model_.node(zone->node_index);
    if (node == nullptr || node->trust != ::lune_touch::NodeTrust::TRUSTED)
      continue;
    ::lune_touch::CommandRecord record{};
    snprintf(record.request_id, sizeof(record.request_id), "ad-%lu-%02u",
             static_cast<unsigned long>(now), static_cast<unsigned>(zi));
    std::strncpy(record.source, "absorb_arm", sizeof(record.source) - 1);
    std::strncpy(record.reason, why, sizeof(record.reason) - 1);
    record.node_index = zone->node_index;
    record.zone_index = zone->zone_index;
    stamp_command_timing_(&record, now, 0);
    const auto target = model_.resolve_room(zone->room_id);
    stamp_command_target_(&record, target.node, target.binding);
    ::lune_touch::CommandRecord final_record = record;
    send_v6_absorb_disarm_(*node, zone->zone_index, &final_record, why);
    ledger_.append(final_record);
    any = true;
  }
  odin_plan_.last_absorb_disarm_ms = now;
  odin_plan_.absorb_armed = false;
  std::strncpy(odin_plan_.absorb_arm_reason, why, sizeof(odin_plan_.absorb_arm_reason) - 1);
  if (any)
    save_ledger_();
  log_event_("info", "absorb_arm", why);
}

bool LuneTouchCoordinator::send_v6_absorb_arm_(const ::lune_touch::PairedNode &node,
                                               uint8_t zone_index, uint32_t ttl_s,
                                               ::lune_touch::CommandRecord *result,
                                               const char *decision_reason) {
  if (result == nullptr)
    return false;
  *result = *result;
  if (!esphome::network::is_connected()) {
    result->result = ::lune_touch::CommandResult::FAILED;
    return false;
  }
  const char *hosts[2]{};
  size_t host_count = 0;
  if (node.hostname[0] != '\0')
    hosts[host_count++] = node.hostname;
  if (node.fallback_ip[0] != '\0' &&
      (host_count == 0 || std::strcmp(node.fallback_ip, hosts[0]) != 0))
    hosts[host_count++] = node.fallback_ip;
  if (host_count == 0) {
    result->result = ::lune_touch::CommandResult::FAILED;
    return false;
  }

  char request_id[64];
  json_escape_(result->request_id, request_id, sizeof(request_id));
  const char *reason =
      (decision_reason != nullptr && decision_reason[0] != '\0') ? decision_reason
                                                                : "odin_plan heat window";
  char reason_esc[96];
  json_escape_(reason, reason_esc, sizeof(reason_esc));
  char payload[384];
  // V6 authenticates commands with key + UTC timestamp + nonce (setpoint-command alike).
  snprintf(payload, sizeof(payload),
           "{\"ttl_s\":%lu,\"reason\":\"%s\",\"decision_reason\":\"%s\","
           "\"source\":\"absorb_arm\",\"auth_timestamp_s\":%lld,\"auth_nonce\":\"%s\"}",
           static_cast<unsigned long>(ttl_s), reason_esc, reason_esc,
           static_cast<long long>(current_epoch_s_()), request_id);

  for (size_t i = 0; i < host_count; i++) {
    char url[320];
    snprintf(url, sizeof(url), "http://%s/api/v1/zones/%u/absorb-arm", hosts[i],
             static_cast<unsigned>(zone_index + 1));
    char body[512];
    int status = 0;
    const bool ok = post_json_(url, payload, body, sizeof(body), &status, authority_shared_key_);
    if (status == 501) {
      result->result = ::lune_touch::CommandResult::REJECTED;
      std::strncpy(result->reason, "v6 not_implemented (501)", sizeof(result->reason) - 1);
      return true;
    }
    if (ok && status >= 200 && status < 300) {
      result->result = ::lune_touch::CommandResult::ACCEPTED;
      result->accepted_offset_c = 0.0f;
      return true;
    }
    ESP_LOGD(TAG, "V6 absorb-arm via %s status=%d", hosts[i], status);
  }
  result->result = ::lune_touch::CommandResult::FAILED;
  return false;
}

bool LuneTouchCoordinator::send_v6_absorb_disarm_(const ::lune_touch::PairedNode &node,
                                                  uint8_t zone_index,
                                                  ::lune_touch::CommandRecord *result,
                                                  const char *reason) {
  if (result == nullptr)
    return false;
  if (!esphome::network::is_connected()) {
    result->result = ::lune_touch::CommandResult::FAILED;
    return false;
  }
  const char *hosts[2]{};
  size_t host_count = 0;
  if (node.hostname[0] != '\0')
    hosts[host_count++] = node.hostname;
  if (node.fallback_ip[0] != '\0' &&
      (host_count == 0 || std::strcmp(node.fallback_ip, hosts[0]) != 0))
    hosts[host_count++] = node.fallback_ip;
  if (host_count == 0) {
    result->result = ::lune_touch::CommandResult::FAILED;
    return false;
  }
  char request_id[64];
  json_escape_(result->request_id, request_id, sizeof(request_id));
  const char *why = (reason != nullptr && reason[0] != '\0') ? reason : "disarm";
  char reason_esc[96];
  json_escape_(why, reason_esc, sizeof(reason_esc));
  char payload[320];
  snprintf(payload, sizeof(payload),
           "{\"reason\":\"%s\",\"decision_reason\":\"%s\",\"source\":\"absorb_arm\","
           "\"auth_timestamp_s\":%lld,\"auth_nonce\":\"%s\"}",
           reason_esc, reason_esc, static_cast<long long>(current_epoch_s_()), request_id);
  for (size_t i = 0; i < host_count; i++) {
    char url[320];
    snprintf(url, sizeof(url), "http://%s/api/v1/zones/%u/absorb-disarm", hosts[i],
             static_cast<unsigned>(zone_index + 1));
    char body[512];
    int status = 0;
    const bool ok = post_json_(url, payload, body, sizeof(body), &status, authority_shared_key_);
    if (status == 501) {
      result->result = ::lune_touch::CommandResult::REJECTED;
      std::strncpy(result->reason, "v6 not_implemented (501)", sizeof(result->reason) - 1);
      return true;
    }
    if (ok && status >= 200 && status < 300) {
      result->result = ::lune_touch::CommandResult::ACCEPTED;
      return true;
    }
  }
  result->result = ::lune_touch::CommandResult::FAILED;
  return false;
}

void LuneTouchCoordinator::url_encode_(const char *src, char *out, size_t out_len) const {
  if (out_len == 0)
    return;
  if (src == nullptr)
    src = "";
  static const char HEX[] = "0123456789ABCDEF";
  size_t off = 0;
  for (const unsigned char *p = reinterpret_cast<const unsigned char *>(src);
       *p != '\0' && off + 1 < out_len; ++p) {
    const unsigned char c = *p;
    const bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') || c == '-' || c == '_' ||
                      c == '.' || c == '~';
    if (safe) {
      out[off++] = static_cast<char>(c);
    } else if (off + 3 < out_len) {
      out[off++] = '%';
      out[off++] = HEX[(c >> 4) & 0x0F];
      out[off++] = HEX[c & 0x0F];
    } else {
      break;
    }
  }
  out[off] = '\0';
}

bool LuneTouchCoordinator::send_v6_zone_setpoint_(const ::lune_touch::PairedNode &node,
                                                  uint8_t zone_index, float setpoint_c) {
  const char *hosts[2]{};
  size_t host_count = 0;
  if (node.hostname[0] != '\0')
    hosts[host_count++] = node.hostname;
  if (node.fallback_ip[0] != '\0' &&
      (host_count == 0 || std::strcmp(node.fallback_ip, hosts[0]) != 0))
    hosts[host_count++] = node.fallback_ip;
  char payload[64];
  snprintf(payload, sizeof(payload), "{\"setpoint_c\":%.1f}", setpoint_c);
  for (size_t i = 0; i < host_count; i++) {
    char url[192];
    char body[256];
    int status = 0;
    snprintf(url, sizeof(url), "http://%s/api/v1/zones/%u/setpoint", hosts[i],
             static_cast<unsigned>(zone_index + 1));
    if (post_json_(url, payload, body, sizeof(body), &status))
      return true;
    ESP_LOGD(TAG, "V6 setpoint update failed via %s (%d)", hosts[i], status);
  }
  return false;
}

bool LuneTouchCoordinator::node_supports_physics_writes_(const ::lune_touch::PairedNode &node) const {
  for (size_t i = 0; i < model_.node_count() && i < ::lune_touch::MAX_NODES; i++) {
    const auto *n = model_.node(i);
    if (n != nullptr && std::strcmp(n->node_id, node.node_id) == 0)
      return node_telemetry_[i].physics_contract == ::lune_touch::physics::CONTRACT_VERSION;
  }
  return false;
}

bool LuneTouchCoordinator::send_v6_zone_physics_(const ::lune_touch::PairedNode &node,
                                                 uint8_t zone_index, uint32_t expected_revision,
                                                 uint8_t exterior_walls, float area_m2,
                                                 const char *slab_type, float active_thickness_cm,
                                                 const char *covering) {
  if (!node_supports_physics_writes_(node)) {
    ESP_LOGW(TAG, "Refuse physics write to %s (physics_contract missing/legacy)", node.node_id);
    return false;
  }
  const char *hosts[2]{};
  size_t host_count = 0;
  if (node.hostname[0] != '\0')
    hosts[host_count++] = node.hostname;
  if (node.fallback_ip[0] != '\0' &&
      (host_count == 0 || std::strcmp(node.fallback_ip, hosts[0]) != 0))
    hosts[host_count++] = node.fallback_ip;
  char payload[320];
  std::snprintf(payload, sizeof(payload),
                "{\"expected_revision\":%lu,\"area_m2\":%.2f,\"exterior_walls\":%u,"
                "\"slab_type\":\"%s\",\"active_thickness_cm\":%.1f,\"covering\":\"%s\"}",
                static_cast<unsigned long>(expected_revision), area_m2 > 0.0f ? area_m2 : 0.0f,
                static_cast<unsigned>(exterior_walls & 0x0F),
                slab_type != nullptr && slab_type[0] != '\0' ? slab_type : "unset",
                std::isfinite(active_thickness_cm) ? active_thickness_cm : 0.0f,
                covering != nullptr && covering[0] != '\0' ? covering : "unset");
  for (size_t i = 0; i < host_count; i++) {
    char url[192];
    char body[256];
    int status = 0;
    std::snprintf(url, sizeof(url), "http://%s/api/v1/zones/%u/physics", hosts[i],
                  static_cast<unsigned>(zone_index + 1));
    if (post_json_(url, payload, body, sizeof(body), &status, authority_shared_key_))
      return true;
    ESP_LOGD(TAG, "V6 zone physics write failed via %s (%d)", hosts[i], status);
  }
  return false;
}

bool LuneTouchCoordinator::send_v6_house_physics_(const ::lune_touch::PairedNode &node, float u_base,
                                                  float u_wall, float c_struct,
                                                  uint32_t calibrated_at_epoch_s) {
  if (!node_supports_physics_writes_(node))
    return false;
  const char *hosts[2]{};
  size_t host_count = 0;
  if (node.hostname[0] != '\0')
    hosts[host_count++] = node.hostname;
  if (node.fallback_ip[0] != '\0' &&
      (host_count == 0 || std::strcmp(node.fallback_ip, hosts[0]) != 0))
    hosts[host_count++] = node.fallback_ip;
  char payload[192];
  std::snprintf(payload, sizeof(payload),
                "{\"u_base\":%.3f,\"u_wall\":%.3f,\"c_struct\":%.3f,\"source\":\"calibrated\","
                "\"calibrated_at_epoch_s\":%lu}",
                u_base, u_wall, c_struct, static_cast<unsigned long>(calibrated_at_epoch_s));
  for (size_t i = 0; i < host_count; i++) {
    char url[192];
    char body[256];
    int status = 0;
    std::snprintf(url, sizeof(url), "http://%s/api/v1/physics/house", hosts[i]);
    if (post_json_(url, payload, body, sizeof(body), &status, authority_shared_key_))
      return true;
  }
  return false;
}

bool LuneTouchCoordinator::send_v6_zone_setting_(const ::lune_touch::PairedNode &node,
                                                 uint8_t zone_index, const char *kind,
                                                 const char *key, const char *value) {
  const char *hosts[2]{};
  size_t host_count = 0;
  if (node.hostname[0] != '\0')
    hosts[host_count++] = node.hostname;
  if (node.fallback_ip[0] != '\0' &&
      (host_count == 0 || std::strcmp(node.fallback_ip, hosts[0]) != 0))
    hosts[host_count++] = node.fallback_ip;
  char escaped_value[96];
  json_escape_(value != nullptr ? value : "", escaped_value, sizeof(escaped_value));
  char payload[192];
  if (std::strcmp(kind, "number") == 0)
    snprintf(payload, sizeof(payload), "{\"key\":\"%s\",\"value\":%s,\"zone\":%u}",
             key, escaped_value, static_cast<unsigned>(zone_index + 1));
  else
    snprintf(payload, sizeof(payload), "{\"key\":\"%s\",\"value\":\"%s\",\"zone\":%u}",
             key, escaped_value, static_cast<unsigned>(zone_index + 1));
  for (size_t i = 0; i < host_count; i++) {
    char url[192];
    char body[256];
    int status = 0;
    snprintf(url, sizeof(url), "http://%s/api/v1/settings/%s", hosts[i], kind);
    if (post_json_(url, payload, body, sizeof(body), &status))
      return true;
    ESP_LOGD(TAG, "V6 setting %s failed via %s (%d)", key, hosts[i], status);
  }
  return false;
}

bool LuneTouchCoordinator::send_v6_setpoint_command_(const ::lune_touch::PairedNode &node, uint8_t zone_index,
                                                     const ::lune_touch::CommandRecord &request,
                                                     uint32_t ttl_s, ::lune_touch::CommandRecord *result,
                                                     const char *preferred_host) {
  if (result == nullptr)
    return false;
  *result = request;

  if (!esphome::network::is_connected())
    return false;

  const char *hosts[3]{};
  size_t host_count = 0;
  auto add_host = [&](const char *host) {
    if (host == nullptr || host[0] == '\0' || host_count >= 3)
      return;
    for (size_t i = 0; i < host_count; i++) {
      if (std::strcmp(hosts[i], host) == 0)
        return;
    }
    hosts[host_count++] = host;
  };
  add_host(preferred_host);
  add_host(node.hostname);
  add_host(node.fallback_ip);
  if (host_count == 0)
    return false;

  char request_id[64];
  char source[64];
  char reason[128];
  json_escape_(request.request_id, request_id, sizeof(request_id));
  json_escape_("lune-touch", source, sizeof(source));
  json_escape_(request.reason, reason, sizeof(reason));

  char payload[512];
  const int64_t auth_timestamp_s = current_epoch_s_();
  snprintf(payload, sizeof(payload),
           "{\"request_id\":\"%s\",\"source\":\"%s\",\"reason\":\"%s\","
           "\"setpoint_offset_c\":%.2f,\"ttl_s\":%lu,\"auth_timestamp_s\":%lld,"
           "\"auth_nonce\":\"%s\"}",
           request_id, source, reason, request.requested_offset_c,
           static_cast<unsigned long>(ttl_s), static_cast<long long>(auth_timestamp_s), request_id);

  for (size_t i = 0; i < host_count; i++) {
    char url[320];
    snprintf(url, sizeof(url), "http://%s/api/v1/zones/%u/setpoint-command",
             hosts[i], static_cast<unsigned>(zone_index + 1));

    char body[512];
    int status = 0;
    if (!post_json_(url, payload, body, sizeof(body), &status, authority_shared_key_)) {
      ESP_LOGD(TAG, "V6 setpoint command failed via %s (%d)", hosts[i], status);
      continue;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
      ESP_LOGW(TAG, "V6 setpoint command JSON parse failed via %s: %s", hosts[i], err.c_str());
      continue;
    }

    JsonVariant data = doc["data"];
    const char *result_name = data["result"].as<const char *>();
    if (result_name == nullptr)
      result_name = doc["result"] | "";
    const bool accepted = std::strcmp(result_name, "accepted") == 0 ||
                          (result_name[0] == '\0' && doc["ok"] == true);
    result->result = accepted ? ::lune_touch::CommandResult::ACCEPTED : ::lune_touch::CommandResult::REJECTED;
    if (!data["accepted_offset_c"].isNull())
      result->accepted_offset_c = data["accepted_offset_c"] | request.requested_offset_c;
    else if (!data["requested_offset_c"].isNull())
      result->accepted_offset_c = data["requested_offset_c"] | request.requested_offset_c;
    else
      result->accepted_offset_c = request.requested_offset_c;
    result->clamp_applied = data["clamp_applied"] | false;
    const uint32_t expires_at = data["expires_at_ms"] | 0UL;
    if (expires_at != 0)
      result->expires_at_ms = expires_at;
    return true;
  }
  return false;
}

bool LuneTouchCoordinator::load_registry_() {
  // Prefer PSRAM: sizeof(PersistedState) grew with physics mirroring and must
  // not permanently occupy internal DRAM (.bss).
  static ::lune_touch::PersistedState *state_ptr = nullptr;
  if (state_ptr == nullptr)
    state_ptr = alloc_persisted_state_();
  if (state_ptr == nullptr) {
    ESP_LOGE(TAG, "No heap for Touch registry load (%u bytes)",
             static_cast<unsigned>(sizeof(::lune_touch::PersistedState)));
    return false;
  }
  ::lune_touch::PersistedState &state = *state_ptr;
  std::memset(&state, 0, sizeof(state));
  state.magic = ::lune_touch::PERSISTED_STATE_MAGIC;
  state.version = ::lune_touch::PERSISTED_STATE_VERSION;

  // Prefer the dedicated partition when it is installed. Current format is v13;
  // v12/v11 migration is read from legacy shapes below.
  if (touch_registry_partition_ready_) {
    nvs_handle_t dedicated_handle;
    if (nvs_open_from_partition(TOUCH_REGISTRY_PARTITION, TOUCH_NAMESPACE, NVS_READONLY,
                                &dedicated_handle) == ESP_OK) {
      if (load_registry_chunks_(dedicated_handle, &state)) {
        nvs_close(dedicated_handle);
        if (state.magic == ::lune_touch::PERSISTED_STATE_MAGIC &&
            state.version == ::lune_touch::PERSISTED_STATE_VERSION) {
          const bool registry_migrated = populate_missing_loop_ids_(&state);
          if (!model_.import_state(state)) {
            ESP_LOGW(TAG, "Ignoring incompatible dedicated Touch registry");
            return false;
          }
          ESP_LOGI(TAG, "Loaded dedicated Touch registry: nodes=%u zones=%u",
                   static_cast<unsigned>(model_.node_count()),
                   static_cast<unsigned>(model_.zone_count()));
          dedicated_touch_registry_valid_ = true;
          if (registry_migrated)
            save_registry_();
          return true;
        }
      } else {
        nvs_close(dedicated_handle);
      }
      // Attempt v12 dedicated blob (same chunk layout, smaller LogicalRoom).
      static PersistedStateV12 legacy_v12;
      std::memset(&legacy_v12, 0, sizeof(legacy_v12));
      if (nvs_open_from_partition(TOUCH_REGISTRY_PARTITION, TOUCH_NAMESPACE, NVS_READONLY,
                                  &dedicated_handle) == ESP_OK) {
        if (load_registry_chunks_for_(dedicated_handle, &legacy_v12) &&
            legacy_v12.magic == ::lune_touch::PERSISTED_STATE_MAGIC &&
            legacy_v12.version == ::lune_touch::PERSISTED_STATE_VERSION_V12 &&
            legacy_v12.node_count <= ::lune_touch::MAX_NODES &&
            legacy_v12.room_count <= ::lune_touch::MAX_HOUSE_ROOMS &&
            legacy_v12.zone_count <= ::lune_touch::MAX_HOUSE_ZONES) {
          nvs_close(dedicated_handle);
          std::memset(&state, 0, sizeof(state));
          state.magic = ::lune_touch::PERSISTED_STATE_MAGIC;
          state.version = ::lune_touch::PERSISTED_STATE_VERSION;
          state.node_count = legacy_v12.node_count;
          state.room_count = legacy_v12.room_count;
          state.zone_count = legacy_v12.zone_count;
          for (size_t i = 0; i < legacy_v12.node_count; i++)
            state.nodes[i] = legacy_v12.nodes[i];
          for (size_t i = 0; i < legacy_v12.room_count; i++)
            copy_logical_room_v12_(state.rooms[i], legacy_v12.rooms[i]);
          for (size_t i = 0; i < legacy_v12.zone_count; i++)
            copy_zone_binding_v13_(state.zones[i], legacy_v12.zones[i]);
          const bool registry_migrated = populate_missing_loop_ids_(&state);
          if (!model_.import_state(state)) {
            ESP_LOGW(TAG, "Ignoring incompatible dedicated v12 Touch registry");
            return false;
          }
          ESP_LOGI(TAG, "Migrated dedicated Touch registry from v12");
          dedicated_touch_registry_valid_ = true;
          save_registry_();
          (void)registry_migrated;
          return true;
        }
        nvs_close(dedicated_handle);
      }
      // Attempt v13 dedicated blob (ZoneBinding without unassigned flags).
      static PersistedStateV13 legacy_v13;
      std::memset(&legacy_v13, 0, sizeof(legacy_v13));
      if (nvs_open_from_partition(TOUCH_REGISTRY_PARTITION, TOUCH_NAMESPACE, NVS_READONLY,
                                  &dedicated_handle) == ESP_OK) {
        if (load_registry_chunks_for_(dedicated_handle, &legacy_v13) &&
            legacy_v13.magic == ::lune_touch::PERSISTED_STATE_MAGIC &&
            legacy_v13.version == ::lune_touch::PERSISTED_STATE_VERSION_V13 &&
            legacy_v13.node_count <= ::lune_touch::MAX_NODES &&
            legacy_v13.room_count <= ::lune_touch::MAX_HOUSE_ROOMS &&
            legacy_v13.zone_count <= ::lune_touch::MAX_HOUSE_ZONES) {
          nvs_close(dedicated_handle);
          std::memset(&state, 0, sizeof(state));
          state.magic = ::lune_touch::PERSISTED_STATE_MAGIC;
          state.version = ::lune_touch::PERSISTED_STATE_VERSION;
          state.node_count = legacy_v13.node_count;
          state.room_count = legacy_v13.room_count;
          state.zone_count = legacy_v13.zone_count;
          for (size_t i = 0; i < legacy_v13.node_count; i++)
            state.nodes[i] = legacy_v13.nodes[i];
          for (size_t i = 0; i < legacy_v13.room_count; i++)
            copy_logical_room_v14_(state.rooms[i], legacy_v13.rooms[i]);
          for (size_t i = 0; i < legacy_v13.zone_count; i++)
            copy_zone_binding_v13_(state.zones[i], legacy_v13.zones[i]);
          const bool registry_migrated = populate_missing_loop_ids_(&state);
          if (!model_.import_state(state)) {
            ESP_LOGW(TAG, "Ignoring incompatible dedicated v13 Touch registry");
            return false;
          }
          ESP_LOGI(TAG, "Migrated dedicated Touch registry from v13");
          dedicated_touch_registry_valid_ = true;
          save_registry_();
          (void)registry_migrated;
          return true;
        }
        nvs_close(dedicated_handle);
      }
      // Attempt v14 dedicated blob (pre-physics-ownership LogicalRoom/ZoneBinding).
      static PersistedStateV14 legacy_v14;
      std::memset(&legacy_v14, 0, sizeof(legacy_v14));
      if (nvs_open_from_partition(TOUCH_REGISTRY_PARTITION, TOUCH_NAMESPACE, NVS_READONLY,
                                  &dedicated_handle) == ESP_OK) {
        if (load_registry_chunks_for_(dedicated_handle, &legacy_v14) &&
            legacy_v14.magic == ::lune_touch::PERSISTED_STATE_MAGIC &&
            legacy_v14.version == ::lune_touch::PERSISTED_STATE_VERSION_V14 &&
            legacy_v14.node_count <= ::lune_touch::MAX_NODES &&
            legacy_v14.room_count <= ::lune_touch::MAX_HOUSE_ROOMS &&
            legacy_v14.zone_count <= ::lune_touch::MAX_HOUSE_ZONES) {
          nvs_close(dedicated_handle);
          std::memset(&state, 0, sizeof(state));
          state.magic = ::lune_touch::PERSISTED_STATE_MAGIC;
          state.version = ::lune_touch::PERSISTED_STATE_VERSION;
          state.node_count = legacy_v14.node_count;
          state.room_count = legacy_v14.room_count;
          state.zone_count = legacy_v14.zone_count;
          for (size_t i = 0; i < legacy_v14.node_count; i++)
            state.nodes[i] = legacy_v14.nodes[i];
          for (size_t i = 0; i < legacy_v14.room_count; i++)
            copy_logical_room_v14_(state.rooms[i], legacy_v14.rooms[i]);
          for (size_t i = 0; i < legacy_v14.zone_count; i++)
            copy_zone_binding_v14_(state.zones[i], legacy_v14.zones[i]);
          // Seed room weather from legacy per-loop values during migration.
          for (size_t i = 0; i < state.zone_count; i++) {
            for (size_t r = 0; r < state.room_count; r++) {
              if (std::strcmp(state.rooms[r].room_id, state.zones[i].room_id) != 0)
                continue;
              if (!state.rooms[r].weather_seeded) {
                state.rooms[r].wind_exposure = state.zones[i].wind_exposure;
                state.rooms[r].solar_gain = state.zones[i].solar_gain;
                state.rooms[r].weather_seeded = true;
                state.rooms[r].exterior_walls_union = state.zones[i].exterior_walls;
              }
              break;
            }
          }
          const bool registry_migrated = populate_missing_loop_ids_(&state);
          if (!model_.import_state(state)) {
            ESP_LOGW(TAG, "Ignoring incompatible dedicated v14 Touch registry");
            return false;
          }
          ESP_LOGI(TAG, "Migrated dedicated Touch registry from v14");
          dedicated_touch_registry_valid_ = true;
          save_registry_();
          (void)registry_migrated;
          return true;
        }
        nvs_close(dedicated_handle);
      }
    }
    std::memset(&state, 0, sizeof(state));
    state.magic = ::lune_touch::PERSISTED_STATE_MAGIC;
    state.version = ::lune_touch::PERSISTED_STATE_VERSION;
  }

  nvs_handle_t handle;
  const esp_err_t open_err = nvs_open(TOUCH_NAMESPACE, NVS_READONLY, &handle);
  if (open_err != ESP_OK) {
    ESP_LOGI(TAG, "Touch registry unavailable: nvs_open(%s)=%s", TOUCH_NAMESPACE,
             esp_err_to_name(open_err));
    return false;
  }

  bool registry_migrated = false;
  static PersistedStateV11 legacy_chunked;
  std::memset(&legacy_chunked, 0, sizeof(legacy_chunked));
  if (load_registry_chunks_for_(handle, &legacy_chunked)) {
    nvs_close(handle);
    if (legacy_chunked.magic != ::lune_touch::PERSISTED_STATE_MAGIC ||
        legacy_chunked.version != ::lune_touch::PERSISTED_STATE_VERSION_V11 ||
        legacy_chunked.node_count > ::lune_touch::MAX_NODES ||
        legacy_chunked.room_count > ::lune_touch::MAX_HOUSE_ROOMS ||
        legacy_chunked.zone_count > ::lune_touch::MAX_HOUSE_ZONES) {
      ESP_LOGW(TAG, "Ignoring incompatible v11 chunked Touch registry");
      return false;
    }
    state.node_count = legacy_chunked.node_count;
    state.room_count = legacy_chunked.room_count;
    state.zone_count = legacy_chunked.zone_count;
    for (size_t i = 0; i < legacy_chunked.node_count; i++)
      state.nodes[i] = legacy_chunked.nodes[i];
    for (size_t i = 0; i < legacy_chunked.room_count; i++)
      copy_logical_room_v12_(state.rooms[i], legacy_chunked.rooms[i]);
    for (size_t i = 0; i < legacy_chunked.zone_count; i++)
      state.zones[i] = legacy_chunked.zones[i];
    registry_migrated = true;
    ESP_LOGI(TAG, "Migrated chunked Touch registry from v11; runtime history reset");
  }
  if (registry_migrated) {
    registry_migrated = populate_missing_loop_ids_(&state) || registry_migrated;
    if (!model_.import_state(state)) {
      ESP_LOGW(TAG, "Ignoring incompatible v11 chunked Touch registry");
      return false;
    }
    ESP_LOGI(TAG, "Loaded migrated Touch registry: nodes=%u zones=%u",
             static_cast<unsigned>(model_.node_count()),
             static_cast<unsigned>(model_.zone_count()));
    if (touch_registry_partition_ready_)
      ESP_LOGI(TAG, "Copying migrated registry to dedicated Touch NVS");
    save_registry_();
    return true;
  }
  if (load_registry_chunks_(handle, &state)) {
    nvs_close(handle);
    if (state.magic != ::lune_touch::PERSISTED_STATE_MAGIC ||
        state.version > ::lune_touch::PERSISTED_STATE_VERSION) {
      ESP_LOGW(TAG, "Ignoring chunked Touch registry magic/version mismatch (magic=0x%08lx version=%u)",
               static_cast<unsigned long>(state.magic), static_cast<unsigned>(state.version));
      return false;
    }
    if (state.version != ::lune_touch::PERSISTED_STATE_VERSION) {
      state.version = ::lune_touch::PERSISTED_STATE_VERSION;
      registry_migrated = true;
    }
    registry_migrated = populate_missing_loop_ids_(&state) || registry_migrated;
    if (!model_.import_state(state)) {
      ESP_LOGW(TAG, "Ignoring incompatible chunked Touch registry blob");
      return false;
    }
    ESP_LOGI(TAG, "Loaded chunked Touch registry: nodes=%u zones=%u",
             static_cast<unsigned>(model_.node_count()),
             static_cast<unsigned>(model_.zone_count()));
    if (registry_migrated || touch_registry_partition_ready_) {
      if (touch_registry_partition_ready_)
        ESP_LOGI(TAG, "Copying legacy Touch registry to dedicated NVS");
      save_registry_();
    }
    return true;
  }

  size_t len = 0;
  esp_err_t err = nvs_get_blob(handle, "registry", nullptr, &len);
  if (err != ESP_OK) {
    nvs_close(handle);
    ESP_LOGI(TAG, "Touch registry not stored yet: %s", esp_err_to_name(err));
    return false;
  }

  if (len == sizeof(state)) {
    err = nvs_get_blob(handle, "registry", &state, &len);
    nvs_close(handle);
    if (err != ESP_OK)
      return false;
    // Some releases changed only the schema marker after an append-only
    // change. Keep the registry when the binary layout is still identical;
    // otherwise a normal OTA update looks like a factory reset to the user.
    if (state.magic != ::lune_touch::PERSISTED_STATE_MAGIC ||
        state.version > ::lune_touch::PERSISTED_STATE_VERSION) {
      ESP_LOGW(TAG, "Ignoring Touch registry magic/version mismatch (magic=0x%08lx version=%u)",
               static_cast<unsigned long>(state.magic), static_cast<unsigned>(state.version));
      return false;
    }
    if (state.version != ::lune_touch::PERSISTED_STATE_VERSION) {
      ESP_LOGI(TAG, "Migrating compatible Touch registry version %u to %u",
               static_cast<unsigned>(state.version),
               static_cast<unsigned>(::lune_touch::PERSISTED_STATE_VERSION));
      state.version = ::lune_touch::PERSISTED_STATE_VERSION;
      registry_migrated = true;
    }
  } else if (len == sizeof(PersistedStateV11)) {
    registry_migrated = true;
    static PersistedStateV11 legacy;
    std::memset(&legacy, 0, sizeof(legacy));
    err = nvs_get_blob(handle, "registry", &legacy, &len);
    nvs_close(handle);
    if (err != ESP_OK || legacy.magic != ::lune_touch::PERSISTED_STATE_MAGIC ||
        legacy.version != ::lune_touch::PERSISTED_STATE_VERSION_V11 ||
        legacy.node_count > ::lune_touch::MAX_NODES || legacy.room_count > ::lune_touch::MAX_HOUSE_ROOMS ||
        legacy.zone_count > ::lune_touch::MAX_HOUSE_ZONES)
      return false;
    state.node_count = legacy.node_count;
    state.room_count = legacy.room_count;
    state.zone_count = legacy.zone_count;
    for (size_t i = 0; i < legacy.node_count; i++)
      state.nodes[i] = legacy.nodes[i];
    for (size_t i = 0; i < legacy.room_count; i++)
      copy_logical_room_v12_(state.rooms[i], legacy.rooms[i]);
    for (size_t i = 0; i < legacy.zone_count; i++)
      state.zones[i] = legacy.zones[i];
    ESP_LOGI(TAG, "Migrated Touch registry from v11; runtime history reset");
  } else if (len == sizeof(PersistedStateV10)) {
    registry_migrated = true;
    static PersistedStateV10 legacy;
    std::memset(&legacy, 0, sizeof(legacy));
    err = nvs_get_blob(handle, "registry", &legacy, &len);
    nvs_close(handle);
    if (err != ESP_OK || legacy.magic != ::lune_touch::PERSISTED_STATE_MAGIC ||
        legacy.version != ::lune_touch::PERSISTED_STATE_VERSION_V10 ||
        legacy.node_count > ::lune_touch::MAX_NODES || legacy.zone_count > ::lune_touch::MAX_HOUSE_ZONES)
      return false;
    state.node_count = legacy.node_count;
    state.zone_count = legacy.zone_count;
    for (size_t i = 0; i < legacy.node_count; i++)
      state.nodes[i] = legacy.nodes[i];
    for (size_t i = 0; i < legacy.zone_count; i++) {
      copy_legacy_zone_v10_(state.zones[i], legacy.zones[i]);
    }
    ESP_LOGI(TAG, "Migrated Touch registry from v10 to v11");
  } else if (len == sizeof(PersistedStateV9)) {
    registry_migrated = true;
    static PersistedStateV9 legacy;
    std::memset(&legacy, 0, sizeof(legacy));
    err = nvs_get_blob(handle, "registry", &legacy, &len);
    nvs_close(handle);
    if (err != ESP_OK || legacy.magic != ::lune_touch::PERSISTED_STATE_MAGIC ||
        legacy.version != ::lune_touch::PERSISTED_STATE_VERSION_V9 ||
        legacy.node_count > ::lune_touch::MAX_NODES || legacy.zone_count > ::lune_touch::MAX_HOUSE_ZONES)
      return false;
    state.node_count = legacy.node_count; state.zone_count = legacy.zone_count;
    for (size_t i = 0; i < legacy.node_count; i++) state.nodes[i] = legacy.nodes[i];
    for (size_t i = 0; i < legacy.zone_count; i++) {
      const auto *node = legacy.zones[i].node_index < legacy.node_count ?
          &legacy.nodes[legacy.zones[i].node_index] : nullptr;
      copy_legacy_zone_v9_(state.zones[i], legacy.zones[i], node);
    }
    ESP_LOGI(TAG, "Migrated Touch registry from v9 to v10");
  } else if (len == sizeof(PersistedStateV8)) {
    registry_migrated = true;
    static PersistedStateV8 legacy;
    std::memset(&legacy, 0, sizeof(legacy));
    err = nvs_get_blob(handle, "registry", &legacy, &len);
    nvs_close(handle);
    if (err != ESP_OK || legacy.magic != ::lune_touch::PERSISTED_STATE_MAGIC ||
        legacy.version != ::lune_touch::PERSISTED_STATE_VERSION_V8 ||
        legacy.node_count > ::lune_touch::MAX_NODES ||
        legacy.zone_count > ::lune_touch::MAX_HOUSE_ZONES) {
      return false;
    }
    state.node_count = legacy.node_count;
    state.zone_count = legacy.zone_count;
    for (size_t i = 0; i < legacy.node_count; i++)
      copy_legacy_node_(state.nodes[i], legacy.nodes[i]);
    for (size_t i = 0; i < legacy.zone_count; i++) {
      copy_legacy_zone_(state.zones[i], legacy.zones[i]);
    }
    ESP_LOGI(TAG, "Migrated Touch registry from v8 to v9");
  } else if (len == sizeof(PersistedStateV7)) {
    registry_migrated = true;
    static PersistedStateV7 legacy;
    std::memset(&legacy, 0, sizeof(legacy));
    err = nvs_get_blob(handle, "registry", &legacy, &len);
    nvs_close(handle);
    if (err != ESP_OK || legacy.magic != ::lune_touch::PERSISTED_STATE_MAGIC ||
        legacy.version != ::lune_touch::PERSISTED_STATE_VERSION_V7 ||
        legacy.node_count > ::lune_touch::MAX_NODES ||
        legacy.zone_count > ::lune_touch::MAX_HOUSE_ZONES) {
      return false;
    }
    state.node_count = legacy.node_count;
    state.zone_count = legacy.zone_count;
    for (size_t i = 0; i < legacy.node_count; i++)
      copy_legacy_node_(state.nodes[i], legacy.nodes[i]);
    for (size_t i = 0; i < legacy.zone_count; i++) {
      copy_legacy_zone_(state.zones[i], legacy.zones[i]);
    }
    ESP_LOGI(TAG, "Migrated Touch registry from v7 to v9");
  } else if (len == sizeof(PersistedStateV6)) {
    registry_migrated = true;
    static PersistedStateV6 legacy;
    std::memset(&legacy, 0, sizeof(legacy));
    err = nvs_get_blob(handle, "registry", &legacy, &len);
    nvs_close(handle);
    if (err != ESP_OK || legacy.magic != ::lune_touch::PERSISTED_STATE_MAGIC ||
        legacy.version != ::lune_touch::PERSISTED_STATE_VERSION_V6 ||
        legacy.node_count > ::lune_touch::MAX_NODES ||
        legacy.zone_count > ::lune_touch::MAX_HOUSE_ZONES) {
      return false;
    }
    state.node_count = legacy.node_count;
    state.zone_count = legacy.zone_count;
    for (size_t i = 0; i < legacy.node_count; i++)
      copy_legacy_node_(state.nodes[i], legacy.nodes[i]);
    for (size_t i = 0; i < legacy.zone_count; i++)
      copy_legacy_zone_(state.zones[i], legacy.zones[i]);
    ESP_LOGI(TAG, "Migrated Touch registry from v6 to v9");
  } else if (len == sizeof(PersistedStateV5)) {
    registry_migrated = true;
    static PersistedStateV5 legacy;
    std::memset(&legacy, 0, sizeof(legacy));
    err = nvs_get_blob(handle, "registry", &legacy, &len);
    nvs_close(handle);
    if (err != ESP_OK || legacy.magic != ::lune_touch::PERSISTED_STATE_MAGIC ||
        legacy.version != ::lune_touch::PERSISTED_STATE_VERSION_V5 ||
        legacy.node_count > ::lune_touch::MAX_NODES ||
        legacy.zone_count > ::lune_touch::MAX_HOUSE_ZONES) {
      return false;
    }
    state.node_count = legacy.node_count;
    state.zone_count = legacy.zone_count;
    for (size_t i = 0; i < legacy.node_count; i++)
      copy_legacy_node_(state.nodes[i], legacy.nodes[i]);
    for (size_t i = 0; i < legacy.zone_count; i++)
      copy_legacy_zone_(state.zones[i], legacy.zones[i]);
    ESP_LOGI(TAG, "Migrated Touch registry from v5 to v9");
  } else if (len == sizeof(PersistedStateV4)) {
    registry_migrated = true;
    static PersistedStateV4 legacy;
    std::memset(&legacy, 0, sizeof(legacy));
    err = nvs_get_blob(handle, "registry", &legacy, &len);
    nvs_close(handle);
    if (err != ESP_OK || legacy.magic != ::lune_touch::PERSISTED_STATE_MAGIC ||
        legacy.version != ::lune_touch::PERSISTED_STATE_VERSION_V4 ||
        legacy.node_count > ::lune_touch::MAX_NODES ||
        legacy.zone_count > ::lune_touch::MAX_HOUSE_ZONES) {
      return false;
    }
    state.node_count = legacy.node_count;
    state.zone_count = legacy.zone_count;
    for (size_t i = 0; i < legacy.node_count; i++)
      copy_legacy_node_(state.nodes[i], legacy.nodes[i]);
    for (size_t i = 0; i < legacy.zone_count; i++) {
      state.zones[i].node_index = legacy.zones[i].node_index;
      state.zones[i].zone_index = legacy.zones[i].zone_index;
      state.zones[i].exterior_walls = legacy.zones[i].exterior_walls;
      state.zones[i].wind_exposure = legacy.zones[i].wind_exposure;
      state.zones[i].solar_gain = legacy.zones[i].solar_gain;
      state.zones[i].thermal_lead_h = legacy.zones[i].thermal_lead_h;
      state.zones[i].max_offset_c = legacy.zones[i].max_offset_c;
      state.zones[i].comfort_setpoint_c = legacy.zones[i].comfort_setpoint_c;
      state.zones[i].comfort_bias_c = legacy.zones[i].comfort_bias_c;
      state.zones[i].schedule_setpoint_c = legacy.zones[i].comfort_setpoint_c;
      state.zones[i].priority = legacy.zones[i].priority;
      state.zones[i].name_source = ::lune_touch::ZoneNameSource::TOUCH;
      state.zones[i].enabled = legacy.zones[i].enabled;
      state.zones[i].schedule_enabled = false;
      std::strncpy(state.zones[i].room_id, legacy.zones[i].room_id,
                   sizeof(state.zones[i].room_id) - 1);
      std::strncpy(state.zones[i].room_name, legacy.zones[i].room_name,
                   sizeof(state.zones[i].room_name) - 1);
    }
    ESP_LOGI(TAG, "Migrated Touch registry from v4 to v9");
  } else if (len == sizeof(PersistedStateV3)) {
    registry_migrated = true;
    static PersistedStateV3 legacy;
    std::memset(&legacy, 0, sizeof(legacy));
    err = nvs_get_blob(handle, "registry", &legacy, &len);
    nvs_close(handle);
    if (err != ESP_OK || legacy.magic != ::lune_touch::PERSISTED_STATE_MAGIC ||
        legacy.version != ::lune_touch::PERSISTED_STATE_VERSION_V3 ||
        legacy.node_count > ::lune_touch::MAX_NODES ||
        legacy.zone_count > ::lune_touch::MAX_HOUSE_ZONES) {
      return false;
    }
    state.node_count = legacy.node_count;
    state.zone_count = legacy.zone_count;
    for (size_t i = 0; i < legacy.node_count; i++)
      copy_legacy_node_(state.nodes[i], legacy.nodes[i]);
    for (size_t i = 0; i < legacy.zone_count; i++) {
      state.zones[i].node_index = legacy.zones[i].node_index;
      state.zones[i].zone_index = legacy.zones[i].zone_index;
      state.zones[i].exterior_walls = legacy.zones[i].exterior_walls;
      state.zones[i].wind_exposure = legacy.zones[i].wind_exposure;
      state.zones[i].solar_gain = legacy.zones[i].solar_gain;
      state.zones[i].thermal_lead_h = legacy.zones[i].thermal_lead_h;
      state.zones[i].max_offset_c = legacy.zones[i].max_offset_c;
      state.zones[i].comfort_setpoint_c = legacy.zones[i].comfort_setpoint_c;
      state.zones[i].comfort_bias_c = 0.0f;
      state.zones[i].schedule_setpoint_c = legacy.zones[i].comfort_setpoint_c;
      state.zones[i].priority = legacy.zones[i].priority;
      state.zones[i].name_source = ::lune_touch::ZoneNameSource::TOUCH;
      state.zones[i].enabled = legacy.zones[i].enabled;
      state.zones[i].schedule_enabled = false;
      std::strncpy(state.zones[i].room_id, legacy.zones[i].room_id,
                   sizeof(state.zones[i].room_id) - 1);
      std::strncpy(state.zones[i].room_name, legacy.zones[i].room_name,
                   sizeof(state.zones[i].room_name) - 1);
    }
    ESP_LOGI(TAG, "Migrated Touch registry from v3 to v9");
  } else {
    nvs_close(handle);
    ESP_LOGW(TAG, "Ignoring Touch registry with unsupported blob size: %u (expected %u)",
             static_cast<unsigned>(len), static_cast<unsigned>(sizeof(state)));
    return false;
  }

  registry_migrated = populate_missing_loop_ids_(&state) || registry_migrated;

  if (!model_.import_state(state)) {
    ESP_LOGW(TAG, "Ignoring incompatible Touch registry blob (version=%u nodes=%u zones=%u rooms=%u)",
             static_cast<unsigned>(state.version), static_cast<unsigned>(state.node_count),
             static_cast<unsigned>(state.zone_count), static_cast<unsigned>(state.room_count));
    return false;
  }
  ESP_LOGI(TAG, "Loaded Touch registry: nodes=%u zones=%u",
           static_cast<unsigned>(model_.node_count()),
           static_cast<unsigned>(model_.zone_count()));
  if (registry_migrated || touch_registry_partition_ready_) {
    if (touch_registry_partition_ready_)
      ESP_LOGI(TAG, "Copying legacy Touch registry to dedicated NVS");
    save_registry_();
  }
  return true;
}

void LuneTouchCoordinator::save_registry_() {
  static bool registry_namespace_ready = false;
  if (!take_state_lock_(250)) {
    ESP_LOGW(TAG, "Could not lock registry for save");
    return;
  }
  static ::lune_touch::PersistedState *state_ptr = nullptr;
  if (state_ptr == nullptr)
    state_ptr = alloc_persisted_state_();
  if (state_ptr == nullptr) {
    ESP_LOGE(TAG, "No heap for Touch registry save (%u bytes)",
             static_cast<unsigned>(sizeof(::lune_touch::PersistedState)));
    give_state_lock_();
    return;
  }
  ::lune_touch::PersistedState &state = *state_ptr;
  std::memset(&state, 0, sizeof(state));
  if (!model_.export_state(&state)) {
    give_state_lock_();
    return;
  }

  nvs_handle_t handle;
  const esp_err_t open_err = open_touch_registry_nvs_(NVS_READWRITE, &handle);
  if (open_err != ESP_OK) {
    ESP_LOGE(TAG, "Could not open Touch registry for save: %s", esp_err_to_name(open_err));
    give_state_lock_();
    return;
  }
  // The legacy fallback partition may contain a large single blob. Remove it
  // before writing the chunked registry; the in-memory model remains intact if
  // a write still fails and the error is explicit in the monitor log.
  // Clear stale single-blob/chunk remnants once per boot before the first
  // successful chunked write. This handles interrupted migrations where no
  // legacy key remains but old chunk entries still consume NVS pages. Normal
  // subsequent saves overwrite the four chunks in place. WiFi, weather,
  // settings, and ledger use separate namespaces.
  if (!registry_namespace_ready) {
    const esp_err_t erase_err = nvs_erase_all(handle);
    if (erase_err != ESP_OK && erase_err != ESP_ERR_NVS_NOT_FOUND) {
      ESP_LOGE(TAG, "Could not clear Touch registry namespace: %s", esp_err_to_name(erase_err));
      nvs_close(handle);
      give_state_lock_();
      return;
    }
    const esp_err_t erase_commit = nvs_commit(handle);
    if (erase_commit != ESP_OK) {
      ESP_LOGE(TAG, "Could not clear legacy Touch registry blob: %s", esp_err_to_name(erase_commit));
      nvs_close(handle);
      give_state_lock_();
      return;
    }
  }
  esp_err_t set_err = ESP_OK;
  const uint8_t *source = reinterpret_cast<const uint8_t *>(&state);
  for (size_t i = 0; i < REGISTRY_CHUNK_COUNT; i++) {
    const size_t offset = i * REGISTRY_CHUNK_SIZE;
    const size_t length = std::min(REGISTRY_CHUNK_SIZE, sizeof(state) - offset);
    set_err = nvs_set_blob(handle, REGISTRY_CHUNK_KEYS[i], source + offset, length);
    if (set_err != ESP_OK) {
      ESP_LOGE(TAG, "Touch registry chunk %u/%u failed (%u bytes): %s",
               static_cast<unsigned>(i + 1), static_cast<unsigned>(REGISTRY_CHUNK_COUNT),
               static_cast<unsigned>(length), esp_err_to_name(set_err));
      nvs_stats_t stats{};
      const char *partition_name = touch_registry_partition_ready_ ? TOUCH_REGISTRY_PARTITION : nullptr;
      if (nvs_get_stats(partition_name, &stats) == ESP_OK)
        ESP_LOGE(TAG, "NVS usage: used=%u free=%u total=%u namespaces=%u",
                 static_cast<unsigned>(stats.used_entries), static_cast<unsigned>(stats.free_entries),
                 static_cast<unsigned>(stats.total_entries), static_cast<unsigned>(stats.namespace_count));
      break;
    }
  }
  const esp_err_t commit_err = set_err == ESP_OK ? nvs_commit(handle) : set_err;
  if (commit_err != ESP_OK)
    ESP_LOGE(TAG, "Could not save chunked Touch registry (%u bytes in %u chunks): %s",
             static_cast<unsigned>(sizeof(state)), static_cast<unsigned>(REGISTRY_CHUNK_COUNT),
             esp_err_to_name(commit_err));
  else
    ESP_LOGD(TAG, "Saved chunked Touch registry (%u bytes in %u chunks)",
             static_cast<unsigned>(sizeof(state)), static_cast<unsigned>(REGISTRY_CHUNK_COUNT));
  if (commit_err == ESP_OK)
    registry_namespace_ready = true;
  if (commit_err == ESP_OK && touch_registry_partition_ready_)
    dedicated_touch_registry_valid_ = true;
  nvs_close(handle);
  give_state_lock_();
}

void LuneTouchCoordinator::load_ledger_() {
  nvs_handle_t handle;
  if (nvs_open(LEDGER_NAMESPACE, NVS_READONLY, &handle) != ESP_OK)
    return;

  PersistedLedgerCompact compact{};
  size_t compact_len = sizeof(compact);
  if (nvs_get_blob(handle, "recent4", &compact, &compact_len) == ESP_OK &&
      compact_len == sizeof(compact) && compact.magic == ::lune_touch::PERSISTED_LEDGER_MAGIC &&
      compact.version == 1 && compact.count <= PERSISTED_LEDGER_RECORDS) {
    static ::lune_touch::PersistedLedger state;
    std::memset(&state, 0, sizeof(state));
    state.magic = ::lune_touch::PERSISTED_LEDGER_MAGIC;
    state.version = ::lune_touch::PERSISTED_LEDGER_VERSION;
    state.boot_id = compact.boot_id;
    state.count = compact.count;
    state.next = compact.count % ::lune_touch::LEDGER_CAPACITY;
    for (size_t i = 0; i < compact.count; i++)
      state.records[i] = compact.records[i];
    nvs_close(handle);
    if (ledger_.import_state(state, boot_id_, current_epoch_s_()))
      ESP_LOGI(TAG, "Loaded compact Touch command ledger: records=%u",
               static_cast<unsigned>(ledger_.count()));
    return;
  }

  static ::lune_touch::PersistedLedger state;
  std::memset(&state, 0, sizeof(state));
  size_t len = sizeof(state);
  const esp_err_t err = nvs_get_blob(handle, "recent", &state, &len);
  nvs_close(handle);
  if (err != ESP_OK || len != sizeof(state))
    return;
  if (!ledger_.import_state(state, boot_id_, current_epoch_s_())) {
    ESP_LOGW(TAG, "Ignoring incompatible Touch command ledger blob");
    return;
  }
  ESP_LOGI(TAG, "Loaded Touch command ledger: records=%u",
           static_cast<unsigned>(ledger_.count()));
  // Migrate the old large ledger blob to the compact form on the next save.
  save_ledger_();
}

void LuneTouchCoordinator::save_ledger_() {
  static ::lune_touch::PersistedLedger state;
  std::memset(&state, 0, sizeof(state));
  if (!ledger_.export_state(&state))
    return;

  PersistedLedgerCompact compact{};
  compact.boot_id = state.boot_id;
  compact.count = std::min<size_t>(state.count, PERSISTED_LEDGER_RECORDS);
  const size_t start = state.count < ::lune_touch::LEDGER_CAPACITY
                           ? (state.count > compact.count ? state.count - compact.count : 0)
                           : (state.next + ::lune_touch::LEDGER_CAPACITY - compact.count) %
                                 ::lune_touch::LEDGER_CAPACITY;
  for (size_t i = 0; i < compact.count; i++) {
    const size_t source_index = (start + i) % ::lune_touch::LEDGER_CAPACITY;
    compact.records[i] = state.records[source_index];
  }

  nvs_handle_t handle;
  if (nvs_open(LEDGER_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK)
    return;
  // The previous ledger was a large single blob. Clear the ledger namespace
  // only during that migration; normal saves overwrite the compact blob.
  size_t legacy_len = 0;
  const bool has_legacy_ledger = nvs_get_blob(handle, "recent", nullptr, &legacy_len) == ESP_OK;
  if (has_legacy_ledger) {
    const esp_err_t erase_err = nvs_erase_all(handle);
    if (erase_err != ESP_OK && erase_err != ESP_ERR_NVS_NOT_FOUND) {
      ESP_LOGE(TAG, "Could not clear Touch ledger namespace: %s", esp_err_to_name(erase_err));
      nvs_close(handle);
      return;
    }
    const esp_err_t erase_commit = nvs_commit(handle);
    if (erase_commit != ESP_OK) {
      ESP_LOGE(TAG, "Could not clear legacy Touch command ledger: %s", esp_err_to_name(erase_commit));
      nvs_close(handle);
      return;
    }
  }
  const esp_err_t set_err = nvs_set_blob(handle, "recent4", &compact, sizeof(compact));
  const esp_err_t commit_err = set_err == ESP_OK ? nvs_commit(handle) : set_err;
  if (commit_err != ESP_OK)
    ESP_LOGE(TAG, "Could not save compact Touch command ledger: %s", esp_err_to_name(commit_err));
  nvs_close(handle);
}

void LuneTouchCoordinator::load_forecast_settings_() {
  nvs_handle_t handle;
  if (nvs_open(WEATHER_NAMESPACE, NVS_READONLY, &handle) != ESP_OK)
    return;
  int32_t lat_e6 = 0;
  int32_t lon_e6 = 0;
  if (nvs_get_i32(handle, "lat_e6", &lat_e6) == ESP_OK)
    forecast_latitude_ = static_cast<float>(lat_e6) / 1000000.0f;
  if (nvs_get_i32(handle, "lon_e6", &lon_e6) == ESP_OK)
    forecast_longitude_ = static_cast<float>(lon_e6) / 1000000.0f;
  size_t mode_len = sizeof(forecast_location_mode_);
  nvs_get_str(handle, "mode", forecast_location_mode_, &mode_len);
  nvs_close(handle);
}

void LuneTouchCoordinator::save_forecast_settings_(float latitude, float longitude, const char *mode) {
  nvs_handle_t handle;
  if (nvs_open(WEATHER_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK)
    return;
  nvs_set_i32(handle, "lat_e6", static_cast<int32_t>(latitude * 1000000.0f));
  nvs_set_i32(handle, "lon_e6", static_cast<int32_t>(longitude * 1000000.0f));
  nvs_set_str(handle, "mode", mode != nullptr && mode[0] != '\0' ? mode : "manual");
  nvs_commit(handle);
  nvs_close(handle);
}

void LuneTouchCoordinator::load_forecast_cache_() {
  nvs_handle_t handle;
  if (nvs_open(WEATHER_NAMESPACE, NVS_READONLY, &handle) != ESP_OK)
    return;
  static PersistedForecastCache cache;
  std::memset(&cache, 0, sizeof(cache));
  size_t len = sizeof(cache);
  const esp_err_t err = nvs_get_blob(handle, "cache", &cache, &len);
  nvs_close(handle);
  if (err != ESP_OK || len != sizeof(cache) || cache.magic != FORECAST_CACHE_MAGIC ||
      !lune_touch_forecast_timeline::cache_version_supported(cache.version) || cache.hours_count == 0 ||
      cache.hours_count > 72) {
    return;
  }
  int64_t timestamps[72]{};
  for (uint8_t i = 0; i < cache.hours_count; i++)
    timestamps[i] = cache.hours[i].timestamp_s;
  const auto validation = lune_touch_forecast_timeline::validate_cache(
      cache.fetch_epoch_s, cache.provider_timezone, timestamps, cache.hours_count, current_epoch_s_(),
      FORECAST_CACHE_MAX_AGE_S);
  if (validation != lune_touch_forecast_timeline::CacheValidation::FRESH) {
    std::strncpy(forecast_status_, "stale", sizeof(forecast_status_) - 1);
    forecast_status_[sizeof(forecast_status_) - 1] = '\0';
    std::strncpy(forecast_last_error_, cache_validation_error_(validation),
                 sizeof(forecast_last_error_) - 1);
    forecast_last_error_[sizeof(forecast_last_error_) - 1] = '\0';
    clear_forecast_cache_();
    return;
  }
  forecast_hours_count_ = cache.hours_count;
  for (uint8_t i = 0; i < forecast_hours_count_; i++)
    forecast_hours_[i] = cache.hours[i];
  forecast_min_temp_c_ = cache.min_temp_c;
  forecast_max_wind_ms_ = cache.max_wind_ms;
  forecast_peak_wind_dir_deg_ = cache.peak_wind_dir_deg;
  forecast_max_solar_wm2_ = cache.max_solar_wm2;
  forecast_fetch_epoch_s_ = cache.fetch_epoch_s;
  std::strncpy(forecast_provider_timezone_, cache.provider_timezone,
               sizeof(forecast_provider_timezone_) - 1);
  forecast_provider_timezone_[sizeof(forecast_provider_timezone_) - 1] = '\0';
  forecast_last_fetch_ms_ = esphome::millis();
  forecast_cache_restored_ = true;
  forecast_boot_refresh_pending_ = true;
  std::strncpy(forecast_status_, "cached", sizeof(forecast_status_) - 1);
  forecast_status_[sizeof(forecast_status_) - 1] = '\0';
  std::strncpy(forecast_last_error_, "restored_cache", sizeof(forecast_last_error_) - 1);
  forecast_last_error_[sizeof(forecast_last_error_) - 1] = '\0';
  recompute_forecast_decisions_();
  ESP_LOGI(TAG, "Restored Touch forecast cache: hours=%u", static_cast<unsigned>(forecast_hours_count_));
}

void LuneTouchCoordinator::save_forecast_cache_() {
  static PersistedForecastCache cache;
  std::memset(&cache, 0, sizeof(cache));
  cache.magic = FORECAST_CACHE_MAGIC;
  cache.version = FORECAST_CACHE_VERSION;
  if (!take_state_lock_(100))
    return;
  cache.hours_count = forecast_hours_count_;
  cache.fetch_epoch_s = forecast_fetch_epoch_s_;
  std::strncpy(cache.provider_timezone, forecast_provider_timezone_,
               sizeof(cache.provider_timezone) - 1);
  cache.provider_timezone[sizeof(cache.provider_timezone) - 1] = '\0';
  cache.min_temp_c = forecast_min_temp_c_;
  cache.max_wind_ms = forecast_max_wind_ms_;
  cache.peak_wind_dir_deg = forecast_peak_wind_dir_deg_;
  cache.max_solar_wm2 = forecast_max_solar_wm2_;
  for (uint8_t i = 0; i < forecast_hours_count_ && i < 72; i++)
    cache.hours[i] = forecast_hours_[i];
  give_state_lock_();
  if (cache.hours_count == 0)
    return;

  nvs_handle_t handle;
  if (nvs_open(WEATHER_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK)
    return;
  if (nvs_set_blob(handle, "cache", &cache, sizeof(cache)) == ESP_OK)
    nvs_commit(handle);
  nvs_close(handle);
}

void LuneTouchCoordinator::clear_forecast_cache_() {
  nvs_handle_t handle;
  if (nvs_open(WEATHER_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK)
    return;
  nvs_erase_key(handle, "cache");
  nvs_commit(handle);
  nvs_close(handle);
}

void LuneTouchCoordinator::load_settings_() {
  nvs_handle_t handle;
  if (nvs_open(SETTINGS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
    size_t len = sizeof(coordinator_name_);
    nvs_get_str(handle, "name", coordinator_name_, &len);
    len = sizeof(install_id_);
    nvs_get_str(handle, "install_id", install_id_, &len);
    len = sizeof(site_label_);
    nvs_get_str(handle, "site", site_label_, &len);
    len = sizeof(install_mode_);
    nvs_get_str(handle, "mode", install_mode_, &len);
    uint8_t asgard_enabled = asgard_enabled_ ? 1 : 0;
    if (nvs_get_u8(handle, "asgard_en", &asgard_enabled) == ESP_OK)
      asgard_enabled_ = asgard_enabled != 0;
    len = sizeof(asgard_mode_);
    nvs_get_str(handle, "asgard_mode", asgard_mode_, &len);
    len = sizeof(authority_leader_node_id_);
    nvs_get_str(handle, "auth_leader", authority_leader_node_id_, &len);
    len = sizeof(authority_coordinator_id_);
    nvs_get_str(handle, "auth_coord", authority_coordinator_id_, &len);
    len = sizeof(authority_shared_key_);
    nvs_get_str(handle, "auth_key", authority_shared_key_, &len);
    uint8_t heat_source_enabled = heat_source_.enabled ? 1 : 0;
    if (nvs_get_u8(handle, "hs_en", &heat_source_enabled) == ESP_OK)
      heat_source_.enabled = heat_source_enabled != 0;
    len = sizeof(heat_source_.type);
    if (nvs_get_str(handle, "hs_type", heat_source_.type, &len) != ESP_OK)
      heat_source_.type[0] = '\0';
    normalize_heat_source_type_(heat_source_.type, sizeof(heat_source_.type));
    len = sizeof(heat_source_.host);
    nvs_get_str(handle, "hs_host", heat_source_.host, &len);
    uint16_t heat_source_port = heat_source_.port;
    if (nvs_get_u16(handle, "hs_port", &heat_source_port) == ESP_OK)
      heat_source_.port = heat_source_port;
    len = sizeof(heat_source_.weighted_temperature_variable);
    nvs_get_str(handle, "hs_temp_var", heat_source_.weighted_temperature_variable, &len);
    len = sizeof(heat_source_.write_url_template);
    nvs_get_str(handle, "hs_wurl", heat_source_.write_url_template, &len);
    len = sizeof(heat_source_.read_url_template);
    nvs_get_str(handle, "hs_rurl", heat_source_.read_url_template, &len);
    uint16_t heat_source_interval = heat_source_.push_interval_s;
    if (nvs_get_u16(handle, "hs_interval", &heat_source_interval) == ESP_OK)
      heat_source_.push_interval_s = heat_source_interval;
    float declared_blob = NAN;
    size_t declared_len = sizeof(declared_blob);
    if (nvs_get_blob(handle, "hs_decl_c", &declared_blob, &declared_len) == ESP_OK &&
        declared_len == sizeof(declared_blob) && std::isfinite(declared_blob) &&
        declared_blob >= 5.0f && declared_blob <= 35.0f) {
      heat_source_.declared_target_c = declared_blob;
      heat_source_.has_declared_target = true;
    }
    len = sizeof(heat_source_.climate_entity);
    nvs_get_str(handle, "hs_climate", heat_source_.climate_entity, &len);
    if (heat_source_type_is_asgard_(heat_source_.type) && heat_source_.climate_entity[0] == '\0') {
      std::strncpy(heat_source_.climate_entity, "Virtual Thermostat z1",
                   sizeof(heat_source_.climate_entity) - 1);
      heat_source_.climate_entity[sizeof(heat_source_.climate_entity) - 1] = '\0';
    }
    uint8_t target_sync = heat_source_.target_sync_enabled ? 1 : 0;
    if (nvs_get_u8(handle, "hs_target_en", &target_sync) == ESP_OK)
      heat_source_.target_sync_enabled = target_sync != 0;
    else if (heat_source_type_is_asgard_(heat_source_.type) && heat_source_.host[0] != '\0')
      // Migrate: Asgard installs without an explicit sync flag get comfort→VT on.
      heat_source_.target_sync_enabled = true;
    uint8_t odin_enabled = 0;
    if (nvs_get_u8(handle, "odin_en", &odin_enabled) == ESP_OK) {
      odin_plan_.enabled = odin_enabled != 0;
    } else if (heat_source_type_is_asgard_(heat_source_.type) && heat_source_.host[0] != '\0') {
      // Migrate: ODIN was previously always-on for configured Asgard installs.
      odin_plan_.enabled = true;
    }
    uint8_t absorb_arm = 0;
    if (nvs_get_u8(handle, "odin_absorb", &absorb_arm) == ESP_OK)
      odin_plan_.absorb_arm_enabled = absorb_arm != 0;
    uint8_t arm_ecm = 1;
    if (nvs_get_u8(handle, "odin_arm_ecm", &arm_ecm) == ESP_OK)
      odin_plan_.arm_energy_cost_modulation = arm_ecm != 0;
    len = sizeof(odin_plan_.plan_source_mode);
    nvs_get_str(handle, "odin_src", odin_plan_.plan_source_mode, &len);
    len = sizeof(odin_plan_.odin_host);
    nvs_get_str(handle, "odin_host", odin_plan_.odin_host, &len);
    uint16_t odin_port = odin_plan_.odin_port;
    if (nvs_get_u16(handle, "odin_port", &odin_port) == ESP_OK)
      odin_plan_.odin_port = odin_port;
    uint8_t mqtt_en = 0;
    if (nvs_get_u8(handle, "odin_mqtt_en", &mqtt_en) == ESP_OK)
      odin_mqtt_.enabled = mqtt_en != 0;
    len = sizeof(odin_mqtt_.host);
    nvs_get_str(handle, "odin_mqtt_h", odin_mqtt_.host, &len);
    uint16_t mqtt_port = odin_mqtt_.port;
    if (nvs_get_u16(handle, "odin_mqtt_p", &mqtt_port) == ESP_OK)
      odin_mqtt_.port = mqtt_port;
    len = sizeof(odin_mqtt_.username);
    nvs_get_str(handle, "odin_mqtt_u", odin_mqtt_.username, &len);
    len = sizeof(odin_mqtt_password_);
    if (nvs_get_str(handle, "odin_mqtt_pw", odin_mqtt_password_, &len) == ESP_OK)
      odin_mqtt_.password_set = odin_mqtt_password_[0] != '\0';
    len = sizeof(odin_mqtt_.topic_prefix);
    nvs_get_str(handle, "odin_mqtt_tp", odin_mqtt_.topic_prefix, &len);
    len = sizeof(odin_mqtt_.hp_id);
    nvs_get_str(handle, "odin_mqtt_hp", odin_mqtt_.hp_id, &len);
    len = sizeof(heat_source_.bias_entity);
    nvs_get_str(handle, "hs_bias_ent", heat_source_.bias_entity, &len);
    len = sizeof(heat_source_.dhw_entity);
    nvs_get_str(handle, "hs_dhw_ent", heat_source_.dhw_entity, &len);
    len = sizeof(heat_source_.legionella_entity);
    nvs_get_str(handle, "hs_legio_ent", heat_source_.legionella_entity, &len);
    len = sizeof(heat_source_.defrost_entity);
    nvs_get_str(handle, "hs_defrost_ent", heat_source_.defrost_entity, &len);
    char trim_mode_buf[16]{};
    len = sizeof(trim_mode_buf);
    if (nvs_get_str(handle, "trim_mode", trim_mode_buf, &len) == ESP_OK)
      flow_trim_.mode = flow_trim::mode_from_name(trim_mode_buf);
    float trim_bias_blob = 0.0f;
    size_t trim_bias_len = sizeof(trim_bias_blob);
    if (nvs_get_blob(handle, "trim_bias", &trim_bias_blob, &trim_bias_len) == ESP_OK &&
        trim_bias_len == sizeof(trim_bias_blob) && std::isfinite(trim_bias_blob)) {
      flow_trim_.bias_c = trim_bias_blob;
      heat_source_.trim_bias_c = trim_bias_blob;
    }
    len = sizeof(v6_control_mode_);
    nvs_get_str(handle, "v6_ctrl_mode", v6_control_mode_, &len);
    char weighting_buf[8]{};
    len = sizeof(weighting_buf);
    if (nvs_get_str(handle, "house_weight", weighting_buf, &len) == ESP_OK)
      model_.set_house_weighting(::lune_touch::house_weighting_from_name(weighting_buf));
    uint8_t circulation_enabled = circulation_.enabled ? 1 : 0;
    if (nvs_get_u8(handle, "circ_en", &circulation_enabled) == ESP_OK)
      circulation_.enabled = circulation_enabled != 0;
    len = sizeof(circulation_.host);
    nvs_get_str(handle, "circ_host", circulation_.host, &len);
    uint16_t circulation_port = circulation_.port;
    if (nvs_get_u16(handle, "circ_port", &circulation_port) == ESP_OK)
      circulation_.port = circulation_port;
    len = sizeof(circulation_.flow_entity);
    nvs_get_str(handle, "circ_flow", circulation_.flow_entity, &len);
    len = sizeof(circulation_.head_entity);
    nvs_get_str(handle, "circ_head", circulation_.head_entity, &len);
    len = sizeof(circulation_.power_entity);
    nvs_get_str(handle, "circ_pwr", circulation_.power_entity, &len);
    float max_boost_c = weather_max_boost_c_;
    len = sizeof(max_boost_c);
    if (nvs_get_blob(handle, "weather_boost", &max_boost_c, &len) == ESP_OK &&
        len == sizeof(max_boost_c) && std::isfinite(max_boost_c)) {
      weather_max_boost_c_ = std::max(0.0f, std::min(5.0f, max_boost_c));
      weather_max_boost_configured_ = true;
      weather_max_boost_seeded_from_v6_ = true;
      if (weather_max_boost_c_ > 0.05f)
        weather_max_boost_resume_c_ = weather_max_boost_c_;
    }
    uint16_t display_idle_timeout_s = display_idle_timeout_s_;
    if (nvs_get_u16(handle, "disp_idle", &display_idle_timeout_s) == ESP_OK &&
        display_idle_timeout_allowed_(display_idle_timeout_s))
      display_idle_timeout_s_ = display_idle_timeout_s;
    nvs_close(handle);
  }
  heat_source_.port = std::max<uint16_t>(1, heat_source_.port);
  heat_source_.push_interval_s = std::max<uint16_t>(5, heat_source_.push_interval_s);
  circulation_.port = std::max<uint16_t>(1, circulation_.port);
  circulation_.refresh_interval_s = 300;
  if (circulation_.flow_entity[0] == '\0')
    std::strncpy(circulation_.flow_entity, "pump_flow", sizeof(circulation_.flow_entity) - 1);
  if (circulation_.head_entity[0] == '\0')
    std::strncpy(circulation_.head_entity, "pump_head_pressure", sizeof(circulation_.head_entity) - 1);
  if (circulation_.power_entity[0] == '\0')
    std::strncpy(circulation_.power_entity, "pump_power", sizeof(circulation_.power_entity) - 1);
  sync_odin_plan_from_heat_source_();

  // Security identity is mirrored into the dedicated Touch partition. This
  // keeps V6 approval stable even if the small default NVS is crowded by an
  // older registry after an OTA migration.
  identity_persistence_needs_sync_ = touch_registry_partition_ready_;
  if (touch_registry_partition_ready_) {
    nvs_handle_t identity_handle;
    if (nvs_open_from_partition(TOUCH_REGISTRY_PARTITION, IDENTITY_NAMESPACE, NVS_READONLY,
                                &identity_handle) == ESP_OK) {
      char persisted_install_id[sizeof(install_id_)]{};
      char persisted_coordinator_id[sizeof(authority_coordinator_id_)]{};
      char persisted_key[sizeof(authority_shared_key_)]{};
      size_t install_len = sizeof(persisted_install_id);
      size_t coordinator_len = sizeof(persisted_coordinator_id);
      size_t key_len = sizeof(persisted_key);
      const bool complete =
          nvs_get_str(identity_handle, "install_id", persisted_install_id, &install_len) == ESP_OK &&
          nvs_get_str(identity_handle, "coord_id", persisted_coordinator_id, &coordinator_len) == ESP_OK &&
          nvs_get_str(identity_handle, "shared_key", persisted_key, &key_len) == ESP_OK &&
          persisted_install_id[0] != '\0' && persisted_coordinator_id[0] != '\0' &&
          std::strlen(persisted_key) >= 16;
      nvs_close(identity_handle);
      if (complete) {
        std::strncpy(install_id_, persisted_install_id, sizeof(install_id_) - 1);
        std::strncpy(authority_coordinator_id_, persisted_coordinator_id,
                     sizeof(authority_coordinator_id_) - 1);
        std::strncpy(authority_shared_key_, persisted_key, sizeof(authority_shared_key_) - 1);
        identity_persistence_needs_sync_ = false;
        ESP_LOGI(TAG, "Loaded OTA-stable Touch installation identity");
      }
    }
  }
}

void LuneTouchCoordinator::ensure_automatic_identity_() {
  const bool missing_install = install_id_[0] == '\0' || std::strcmp(install_id_, "unassigned") == 0;
  const bool missing_key = authority_shared_key_[0] == '\0';
  const bool untouched_defaults = missing_install && missing_key;
  bool changed = false;

  if (missing_install) {
    const uint32_t a = esp_random();
    const uint32_t b = esp_random();
    std::snprintf(install_id_, sizeof(install_id_), "lune-%08lx%08lx",
                  static_cast<unsigned long>(a), static_cast<unsigned long>(b));
    changed = true;
  }
  if (authority_coordinator_id_[0] == '\0' ||
      (untouched_defaults && std::strcmp(authority_coordinator_id_, "lune-touch") == 0)) {
    std::snprintf(authority_coordinator_id_, sizeof(authority_coordinator_id_), "touch-%08lx",
                  static_cast<unsigned long>(esp_random()));
    changed = true;
  }
  if (missing_key) {
    static constexpr char HEX[] = "0123456789abcdef";
    size_t off = 0;
    while (off + 8 < sizeof(authority_shared_key_)) {
      const uint32_t value = esp_random();
      for (int shift = 28; shift >= 0 && off + 1 < sizeof(authority_shared_key_); shift -= 4)
        authority_shared_key_[off++] = HEX[(value >> shift) & 0x0F];
      if (off >= 48)
        break;
    }
    authority_shared_key_[off] = '\0';
    changed = true;
  }

  if (!changed && !identity_persistence_needs_sync_)
    return;
  save_settings_();
  ESP_LOGI(TAG, changed ? "Generated and persisted Touch installation identity"
                        : "Migrated Touch installation identity to dedicated NVS");
}

void LuneTouchCoordinator::save_settings_() {
  nvs_handle_t handle;
  const esp_err_t settings_open = nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &handle);
  if (settings_open == ESP_OK) {
    esp_err_t err = nvs_set_str(handle, "name", coordinator_name_);
    if (err == ESP_OK) err = nvs_set_str(handle, "install_id", install_id_);
    if (err == ESP_OK) err = nvs_set_str(handle, "site", site_label_);
    if (err == ESP_OK) err = nvs_set_str(handle, "mode", install_mode_);
    if (err == ESP_OK) err = nvs_set_u8(handle, "asgard_en", asgard_enabled_ ? 1 : 0);
    if (err == ESP_OK) err = nvs_set_str(handle, "asgard_mode", asgard_mode_);
    if (err == ESP_OK) err = nvs_set_str(handle, "auth_leader", authority_leader_node_id_);
    if (err == ESP_OK) err = nvs_set_str(handle, "auth_coord", authority_coordinator_id_);
    if (err == ESP_OK) err = nvs_set_str(handle, "auth_key", authority_shared_key_);
    if (err == ESP_OK) err = nvs_set_u8(handle, "hs_en", heat_source_.enabled ? 1 : 0);
    if (err == ESP_OK) err = nvs_set_str(handle, "hs_type", heat_source_.type);
    if (err == ESP_OK) err = nvs_set_str(handle, "hs_host", heat_source_.host);
    if (err == ESP_OK) err = nvs_set_u16(handle, "hs_port", heat_source_.port);
    if (err == ESP_OK) err = nvs_set_str(handle, "hs_temp_var", heat_source_.weighted_temperature_variable);
    if (err == ESP_OK) err = nvs_set_str(handle, "hs_wurl", heat_source_.write_url_template);
    if (err == ESP_OK) err = nvs_set_str(handle, "hs_rurl", heat_source_.read_url_template);
    if (err == ESP_OK) err = nvs_set_u16(handle, "hs_interval", heat_source_.push_interval_s);
    if (err == ESP_OK) {
      if (heat_source_.has_declared_target && std::isfinite(heat_source_.declared_target_c))
        err = nvs_set_blob(handle, "hs_decl_c", &heat_source_.declared_target_c,
                           sizeof(heat_source_.declared_target_c));
      else
        nvs_erase_key(handle, "hs_decl_c");
    }
    if (err == ESP_OK) err = nvs_set_str(handle, "hs_climate", heat_source_.climate_entity);
    if (err == ESP_OK) err = nvs_set_u8(handle, "hs_target_en", heat_source_.target_sync_enabled ? 1 : 0);
    if (err == ESP_OK) err = nvs_set_u8(handle, "odin_en", odin_plan_.enabled ? 1 : 0);
    if (err == ESP_OK) err = nvs_set_u8(handle, "odin_absorb", odin_plan_.absorb_arm_enabled ? 1 : 0);
    if (err == ESP_OK)
      err = nvs_set_u8(handle, "odin_arm_ecm", odin_plan_.arm_energy_cost_modulation ? 1 : 0);
    if (err == ESP_OK) err = nvs_set_str(handle, "odin_src", odin_plan_.plan_source_mode);
    if (err == ESP_OK) err = nvs_set_str(handle, "odin_host", odin_plan_.odin_host);
    if (err == ESP_OK) err = nvs_set_u16(handle, "odin_port", odin_plan_.odin_port);
    if (err == ESP_OK) err = nvs_set_u8(handle, "odin_mqtt_en", odin_mqtt_.enabled ? 1 : 0);
    if (err == ESP_OK) err = nvs_set_str(handle, "odin_mqtt_h", odin_mqtt_.host);
    if (err == ESP_OK) err = nvs_set_u16(handle, "odin_mqtt_p", odin_mqtt_.port);
    if (err == ESP_OK) err = nvs_set_str(handle, "odin_mqtt_u", odin_mqtt_.username);
    if (err == ESP_OK) err = nvs_set_str(handle, "odin_mqtt_pw", odin_mqtt_password_);
    if (err == ESP_OK) err = nvs_set_str(handle, "odin_mqtt_tp", odin_mqtt_.topic_prefix);
    if (err == ESP_OK) err = nvs_set_str(handle, "odin_mqtt_hp", odin_mqtt_.hp_id);
    if (err == ESP_OK) err = nvs_set_str(handle, "hs_bias_ent", heat_source_.bias_entity);
    if (err == ESP_OK) err = nvs_set_str(handle, "hs_dhw_ent", heat_source_.dhw_entity);
    if (err == ESP_OK) err = nvs_set_str(handle, "hs_legio_ent", heat_source_.legionella_entity);
    if (err == ESP_OK) err = nvs_set_str(handle, "hs_defrost_ent", heat_source_.defrost_entity);
    if (err == ESP_OK) err = nvs_set_str(handle, "trim_mode", flow_trim::mode_name(flow_trim_.mode));
    if (err == ESP_OK)
      err = nvs_set_blob(handle, "trim_bias", &flow_trim_.bias_c, sizeof(flow_trim_.bias_c));
    if (err == ESP_OK) err = nvs_set_str(handle, "v6_ctrl_mode", v6_control_mode_);
    if (err == ESP_OK)
      err = nvs_set_str(handle, "house_weight",
                        ::lune_touch::house_weighting_name(model_.house_weighting()));
    if (err == ESP_OK) err = nvs_set_u8(handle, "circ_en", circulation_.enabled ? 1 : 0);
    if (err == ESP_OK) err = nvs_set_str(handle, "circ_host", circulation_.host);
    if (err == ESP_OK) err = nvs_set_u16(handle, "circ_port", circulation_.port);
    if (err == ESP_OK) err = nvs_set_str(handle, "circ_flow", circulation_.flow_entity);
    if (err == ESP_OK) err = nvs_set_str(handle, "circ_head", circulation_.head_entity);
    if (err == ESP_OK) err = nvs_set_str(handle, "circ_pwr", circulation_.power_entity);
    if (err == ESP_OK)
      err = nvs_set_blob(handle, "weather_boost", &weather_max_boost_c_, sizeof(weather_max_boost_c_));
    if (err == ESP_OK)
      err = nvs_set_u16(handle, "disp_idle", display_idle_timeout_s_);
    if (err == ESP_OK)
      err = nvs_commit(handle);
    nvs_close(handle);
    if (err != ESP_OK)
      ESP_LOGE(TAG, "Could not persist Touch settings: %s", esp_err_to_name(err));
  } else {
    ESP_LOGE(TAG, "Could not open Touch settings NVS: %s", esp_err_to_name(settings_open));
  }

  if (!touch_registry_partition_ready_)
    return;
  nvs_handle_t identity_handle;
  const esp_err_t identity_open = nvs_open_from_partition(
      TOUCH_REGISTRY_PARTITION, IDENTITY_NAMESPACE, NVS_READWRITE, &identity_handle);
  if (identity_open != ESP_OK) {
    ESP_LOGE(TAG, "Could not open dedicated Touch identity NVS: %s", esp_err_to_name(identity_open));
    return;
  }
  esp_err_t identity_err = nvs_set_str(identity_handle, "install_id", install_id_);
  if (identity_err == ESP_OK)
    identity_err = nvs_set_str(identity_handle, "coord_id", authority_coordinator_id_);
  if (identity_err == ESP_OK)
    identity_err = nvs_set_str(identity_handle, "shared_key", authority_shared_key_);
  if (identity_err == ESP_OK)
    identity_err = nvs_commit(identity_handle);
  nvs_close(identity_handle);
  if (identity_err == ESP_OK)
    identity_persistence_needs_sync_ = false;
  else
    ESP_LOGE(TAG, "Could not persist OTA-stable Touch identity: %s", esp_err_to_name(identity_err));
}

void LuneTouchCoordinator::make_node_id_(const char *hostname, const char *fallback_ip, char *out, size_t out_len) const {
  if (out_len == 0)
    return;
  const char *source = (hostname != nullptr && hostname[0] != '\0') ? hostname : fallback_ip;
  if (source == nullptr || source[0] == '\0')
    source = "lune-v6";
  size_t off = 0;
  for (const char *p = source; *p != '\0' && off + 1 < out_len; ++p) {
    const char c = *p;
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
      out[off++] = c;
    } else if (c == '-' || c == '_') {
      out[off++] = c;
    } else if (c == '.' && off + 1 < out_len) {
      out[off++] = '-';
    }
  }
  out[off] = '\0';
  if (out[0] == '\0')
    std::strncpy(out, "lune-v6", out_len - 1);
  out[out_len - 1] = '\0';
}

void LuneTouchCoordinator::seed_mock_house_() {
  if (model_.node_count() > 0)
    return;
  int a = model_.upsert_node("v6-a", "lune-v6-a.local", "192.168.1.51", "lune-v6", "mock", ::lune_touch::NodeTrust::TRUSTED);
  int b = model_.upsert_node("v6-b", "lune-v6-b.local", "192.168.1.52", "lune-v6", "mock", ::lune_touch::NodeTrust::TRUSTED);
  int c = model_.upsert_node("v6-c", "lune-v6-c.local", "192.168.1.53", "lune-v6", "mock", ::lune_touch::NodeTrust::PAIRED);
  if (a >= 0) model_.update_node_identity(a, "hv6-mock-a");
  if (b >= 0) model_.update_node_identity(b, "hv6-mock-b");
  if (c >= 0) model_.update_node_identity(c, "hv6-mock-c");
  if (a >= 0) model_.mark_node_seen(a, esphome::millis());
  if (b >= 0) model_.mark_node_seen(b, esphome::millis());

  const char *rooms[18] = {
      "Living", "Kitchen", "Bath", "Hall", "Office", "Bedroom",
      "Guest", "Utility", "Laundry", "Workshop", "Pantry", "Landing",
      "Kids west", "Kids east", "Ensuite", "Basement", "Garage", "Spare"};
  static const float temps[18] = {21.3f,20.9f,22.2f,20.1f,20.8f,19.4f,19.8f,18.9f,18.7f,17.6f,18.1f,20.3f,20.5f,20.0f,21.8f,NAN,12.4f,NAN};
  static const float setpoints[18] = {21.0f,21.0f,22.5f,20.0f,21.0f,19.5f,20.0f,19.0f,18.5f,18.0f,18.0f,20.0f,20.5f,20.5f,22.0f,18.0f,12.0f,NAN};
  static const char *states[18] = {"heat","idle","call","hold","idle","preheat","idle","heat","idle","call","idle","hold","idle","heat","call","stale","idle","unused"};
  for (size_t i = 0; i < 18; i++) {
    const size_t node = i / ::lune_touch::ZONES_PER_NODE;
    char id[16];
    snprintf(id, sizeof(id), "room-%02u", static_cast<unsigned>(i + 1));
    model_.bind_zone(id, rooms[i], node, i % ::lune_touch::ZONES_PER_NODE);
    model_.update_zone_live(id, temps[i], !std::isnan(temps[i]), setpoints[i], !std::isnan(setpoints[i]),
                            states[i], strcmp(states[i], "stale") != 0 && strcmp(states[i], "unused") != 0,
                            esphome::millis());
  }

  ::lune_touch::CommandRecord record{};
  std::strncpy(record.request_id, "mock-forecast-1", sizeof(record.request_id) - 1);
  std::strncpy(record.source, "forecast", sizeof(record.source) - 1);
  std::strncpy(record.reason, "wind preload", sizeof(record.reason) - 1);
  record.node_index = 0;
  record.zone_index = 0;
  record.requested_offset_c = 0.4f;
  record.accepted_offset_c = 0.4f;
  stamp_command_timing_(&record, esphome::millis(), 45UL * 60UL);
  const auto mock_target = model_.resolve_room("room-01");
  stamp_command_target_(&record, mock_target.node, mock_target.binding);
  record.result = ::lune_touch::CommandResult::ACCEPTED;
  ledger_.append(record);
  save_ledger_();
  save_registry_();
}

bool LuneTouchCoordinator::add_node(const char *node_id, const char *hostname, const char *fallback_ip,
                                    const char *pairing_fingerprint, char *response, size_t capacity) {
  char generated_id[24]{};
  if (node_id == nullptr || node_id[0] == '\0') {
    make_node_id_(hostname, fallback_ip, generated_id, sizeof(generated_id));
    node_id = generated_id;
  }
  // PairedNode::node_id is the canonical identity used by all subsequent
  // /nodes/{id}/... routes.  Reject overlong caller-supplied ids instead of
  // silently truncating them in HouseModel::upsert_node(); silent truncation
  // leaves the UI holding an id that later trust/profile/remove calls cannot
  // resolve and surfaces as the misleading node_not_found error.
  if (std::strlen(node_id) >= sizeof(::lune_touch::PairedNode{}.node_id)) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"node_id_too_long\"}");
    return false;
  }
  for (const char *p = node_id; *p != '\0'; ++p) {
    const unsigned char c = static_cast<unsigned char>(*p);
    if (!std::isalnum(c) && c != '-' && c != '_') {
      snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"node_id_invalid\"}");
      return false;
    }
  }
  if ((hostname == nullptr || hostname[0] == '\0') && (fallback_ip == nullptr || fallback_ip[0] == '\0')) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"hostname_or_ip_required\"}");
    return false;
  }
  if (!take_state_lock_(250)) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"coordinator_busy\"}");
    return false;
  }
  // Same V6 (fingerprint) at a new address: move the paired node instead of
  // adding a second one, so trust and zone bindings survive a DHCP change.
  if (pairing_fingerprint != nullptr && pairing_fingerprint[0] != '\0') {
    for (size_t i = 0; i < model_.node_count(); i++) {
      const auto *existing = model_.node(i);
      if (existing == nullptr || existing->pairing_fingerprint[0] == '\0' ||
          !pairing_fingerprints_match_(existing->pairing_fingerprint, pairing_fingerprint))
        continue;
      char existing_id[sizeof(existing->node_id)];
      std::strncpy(existing_id, existing->node_id, sizeof(existing_id) - 1);
      existing_id[sizeof(existing_id) - 1] = '\0';
      rehome_node_locked_(i, hostname, fallback_ip);
      give_state_lock_();
      save_registry_();
      char id_esc[48];
      json_escape_(existing_id, id_esc, sizeof(id_esc));
      snprintf(response, capacity, "{\"result\":\"moved\",\"node_id\":\"%s\",\"node_index\":%u}",
               id_esc, static_cast<unsigned>(i));
      char event[112];
      snprintf(event, sizeof(event), "node %s moved to a new address", existing_id);
      log_event_("info", "commissioning", event);
      kick_task_(poll_task_handle_);
      return true;
    }
  }
  const int index = model_.upsert_node(node_id, hostname, fallback_ip, "lune-v6", "unknown", ::lune_touch::NodeTrust::PAIRED);
  if (index < 0) {
    give_state_lock_();
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"node_registry_full\"}");
    return false;
  }
  char canonical_fp[24]{};
  canonicalize_pairing_fingerprint_(pairing_fingerprint, canonical_fp, sizeof(canonical_fp));
  model_.update_node_identity(static_cast<size_t>(index),
                              canonical_fp[0] != '\0' ? canonical_fp : pairing_fingerprint);
  const auto *stored_node = model_.node(static_cast<size_t>(index));
  char canonical_node_id[sizeof(::lune_touch::PairedNode{}.node_id)]{};
  std::strncpy(canonical_node_id, stored_node != nullptr ? stored_node->node_id : node_id,
               sizeof(canonical_node_id) - 1);
  give_state_lock_();
  save_registry_();
  char node_id_esc[48];
  char pairing_fingerprint_esc[48];
  json_escape_(canonical_node_id, node_id_esc, sizeof(node_id_esc));
  json_escape_(pairing_fingerprint != nullptr ? pairing_fingerprint : "",
               pairing_fingerprint_esc, sizeof(pairing_fingerprint_esc));
  snprintf(response, capacity, "{\"result\":\"stored\",\"node_id\":\"%s\",\"node_index\":%d,"
           "\"pairing_fingerprint\":\"%s\"}",
           node_id_esc, index, pairing_fingerprint_esc);
  char event[112];
  snprintf(event, sizeof(event), "paired node %s", canonical_node_id);
  log_event_("info", "commissioning", event);
  // Wake the poll task immediately so the authority proposal is not stuck
  // behind the boot delay / 15s interval. V6 only shows "ready to connect"
  // after it receives POST /authority/proposal.
  kick_task_(poll_task_handle_);
  return true;
}

bool LuneTouchCoordinator::scan_node_candidate(const char *hostname, const char *fallback_ip,
                                               char *response, size_t capacity) {
  if ((hostname == nullptr || hostname[0] == '\0') &&
      (fallback_ip == nullptr || fallback_ip[0] == '\0')) {
    write_node_scan_json(response, capacity);
    return true;
  }

  char generated_id[24]{};
  make_node_id_(hostname, fallback_ip, generated_id, sizeof(generated_id));

  const char *hosts[2]{};
  size_t host_count = 0;
  if (hostname != nullptr && hostname[0] != '\0')
    hosts[host_count++] = hostname;
  if (fallback_ip != nullptr && fallback_ip[0] != '\0' &&
      (host_count == 0 || std::strcmp(fallback_ip, hosts[0]) != 0))
    hosts[host_count++] = fallback_ip;
  const char *primary_host = host_count > 0 ? hosts[0] : "";
  char host_esc[128];
  char ip_esc[48];
  char id_esc[48];
  json_escape_(primary_host, host_esc, sizeof(host_esc));
  json_escape_(fallback_ip != nullptr ? fallback_ip : "", ip_esc, sizeof(ip_esc));
  json_escape_(generated_id, id_esc, sizeof(id_esc));

  if (!esphome::network::is_connected()) {
    snprintf(response, capacity,
             "{\"scan\":\"probe\",\"discovery\":\"manual_probe\",\"found\":[{\"id\":\"%s\","
             "\"hostname\":\"%s\",\"ip\":\"%s\",\"reachable\":false,\"stale\":true,"
             "\"source\":\"manual_probe\",\"error\":\"network_offline\"}]}",
             id_esc, host_esc, ip_esc);
    return false;
  }

  constexpr size_t PROBE_BODY_CAP = 3072;
  char *body = alloc_http_body_(PROBE_BODY_CAP);
  if (body == nullptr) {
    snprintf(response, capacity,
             "{\"scan\":\"probe\",\"discovery\":\"manual_probe\",\"found\":[{\"id\":\"%s\","
             "\"hostname\":\"%s\",\"ip\":\"%s\",\"reachable\":false,\"stale\":true,"
             "\"source\":\"manual_probe\",\"error\":\"no_body_heap\"}]}",
             id_esc, host_esc, ip_esc);
    return false;
  }
  int status = 0;
  int last_status = 0;
  const char *success_host = nullptr;
  JsonDocument doc;
  bool parsed = false;
  for (size_t i = 0; i < host_count; i++) {
    char url[192];
    snprintf(url, sizeof(url), "http://%s/api/v1/overview", hosts[i]);
    if (!fetch_json_(url, body, PROBE_BODY_CAP, &status)) {
      last_status = status;
      ESP_LOGD(TAG, "V6 probe failed via %s (%d)", hosts[i], status);
      continue;
    }
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
      last_status = status;
      ESP_LOGW(TAG, "V6 probe JSON parse failed via %s: %s", hosts[i], err.c_str());
      doc.clear();
      continue;
    }
    success_host = hosts[i];
    parsed = true;
    break;
  }
  if (!parsed) {
    snprintf(response, capacity,
             "{\"scan\":\"probe\",\"discovery\":\"manual_probe\",\"found\":[{\"id\":\"%s\","
             "\"hostname\":\"%s\",\"ip\":\"%s\",\"reachable\":false,\"stale\":true,"
             "\"source\":\"manual_probe\",\"http_status\":%d,\"error\":\"probe_failed\"}]}",
             id_esc, host_esc, ip_esc, last_status);
    heap_caps_free(body);
    return false;
  }

  JsonVariant data = doc["data"];
  if (data.isNull())
    data = doc;
  const char *model = data["node"]["model"] | "lune-v6";
  const char *firmware = data["node"]["firmware"] | "unknown";
  const char *pairing_fingerprint = data["pairing"]["fingerprint"].as<const char *>();
  if (pairing_fingerprint == nullptr || pairing_fingerprint[0] == '\0')
    pairing_fingerprint = data["node"]["pairing_fingerprint"] | "";
  const char *reported_ip = data["node"]["ip"].as<const char *>();
  if (reported_ip == nullptr || reported_ip[0] == '\0')
    reported_ip = fallback_ip != nullptr ? fallback_ip : "";
  char model_esc[32];
  char firmware_esc[64];
  char pairing_fingerprint_esc[48];
  char reported_ip_esc[48];
  json_escape_(model, model_esc, sizeof(model_esc));
  json_escape_(firmware, firmware_esc, sizeof(firmware_esc));
  json_escape_(pairing_fingerprint, pairing_fingerprint_esc, sizeof(pairing_fingerprint_esc));
  json_escape_(reported_ip, reported_ip_esc, sizeof(reported_ip_esc));
  json_escape_(success_host != nullptr ? success_host : primary_host, host_esc, sizeof(host_esc));

  snprintf(response, capacity,
           "{\"scan\":\"probe\",\"discovery\":\"manual_probe\",\"found\":[{\"id\":\"%s\","
           "\"hostname\":\"%s\",\"ip\":\"%s\",\"model\":\"%s\",\"firmware\":\"%s\","
           "\"pairing_fingerprint\":\"%s\","
           "\"reachable\":true,\"stale\":false,\"source\":\"manual_probe\",\"http_status\":%d}]}",
           id_esc, host_esc, reported_ip_esc, model_esc, firmware_esc,
           pairing_fingerprint_esc, status);
  heap_caps_free(body);
  return true;
}

bool LuneTouchCoordinator::scan_registered_nodes(char *response, size_t capacity) {
  if (take_state_lock_(100)) {
    node_refresh_requested_ = true;
    give_state_lock_();
  }
  if (poll_task_handle_ != nullptr)
    xTaskNotifyGive(poll_task_handle_);
  // Return immediately. Network probing belongs to the coordinator task, not
  // the HTTP worker; otherwise concurrent overview/nodes/zones requests fail.
  write_node_scan_json(response, capacity);
  return esphome::network::is_connected();
}

bool LuneTouchCoordinator::request_lan_scan(char *response, size_t capacity) {
  if (take_state_lock_(100)) {
    lan_scan_requested_ = true;
    node_refresh_requested_ = true;
    give_state_lock_();
  }
  kick_task_(lan_scan_task_handle_);
  if (poll_task_handle_ != nullptr)
    xTaskNotifyGive(poll_task_handle_);
  write_node_scan_json(response, capacity);
  return esphome::network::is_connected();
}

bool LuneTouchCoordinator::probe_v6_overview_(const char *host, uint32_t timeout_ms, char *model,
                                              size_t model_len, char *firmware, size_t firmware_len,
                                              char *fingerprint, size_t fingerprint_len,
                                              char *reported_ip, size_t reported_ip_len) {
  if (host == nullptr || host[0] == '\0')
    return false;
  if (model != nullptr && model_len > 0)
    model[0] = '\0';
  if (firmware != nullptr && firmware_len > 0)
    firmware[0] = '\0';
  if (fingerprint != nullptr && fingerprint_len > 0)
    fingerprint[0] = '\0';
  if (reported_ip != nullptr && reported_ip_len > 0)
    reported_ip[0] = '\0';

  constexpr size_t BODY_CAP = 2048;
  char *body = alloc_http_body_(BODY_CAP);
  if (body == nullptr)
    return false;
  char url[192];
  snprintf(url, sizeof(url), "http://%s/api/v1/overview", host);
  int status = 0;
  const bool fetched = fetch_json_(url, body, BODY_CAP, &status, timeout_ms);
  bool ok = false;
  if (fetched) {
    JsonDocument doc;
    if (deserializeJson(doc, body) == DeserializationError::Ok) {
      JsonVariant data = doc["data"];
      if (data.isNull())
        data = doc;
      const char *found_model = data["node"]["model"] | "";
      if (std::strcmp(found_model, "lune-v6") == 0) {
        ok = true;
        if (model != nullptr && model_len > 0) {
          std::strncpy(model, found_model, model_len - 1);
          model[model_len - 1] = '\0';
        }
        const char *found_fw = data["node"]["firmware"] | "unknown";
        if (firmware != nullptr && firmware_len > 0) {
          std::strncpy(firmware, found_fw, firmware_len - 1);
          firmware[firmware_len - 1] = '\0';
        }
        const char *found_fp = data["pairing"]["fingerprint"].as<const char *>();
        if (found_fp == nullptr || found_fp[0] == '\0')
          found_fp = data["node"]["pairing_fingerprint"] | "";
        if (fingerprint != nullptr && fingerprint_len > 0) {
          std::strncpy(fingerprint, found_fp, fingerprint_len - 1);
          fingerprint[fingerprint_len - 1] = '\0';
        }
        const char *found_ip = data["node"]["ip"] | host;
        if (reported_ip != nullptr && reported_ip_len > 0) {
          std::strncpy(reported_ip, found_ip, reported_ip_len - 1);
          reported_ip[reported_ip_len - 1] = '\0';
        }
      }
    }
  }
  heap_caps_free(body);
  return ok;
}

void LuneTouchCoordinator::discover_v6_on_lan_() {
  if (take_state_lock_(100)) {
    lan_candidate_count_ = 0;
    std::strncpy(lan_discovery_, "lan_probe", sizeof(lan_discovery_) - 1);
    lan_discovery_[sizeof(lan_discovery_) - 1] = '\0';
    give_state_lock_();
  }

  LanScanCandidate found[MAX_LAN_CANDIDATES]{};
  uint8_t found_count = 0;
  uint32_t self_ip = 0;
  uint32_t mask = 0;
  if (!esphome::network::is_connected() || !sta_ipv4_info_(&self_ip, &mask)) {
    ESP_LOGW(TAG, "LAN scan skipped: no STA IPv4");
    return;
  }
  // Never walk a prefix wider than /24 from this S3; a /16 would miss the
  // dashboard wait window. mDNS is intentionally not used.
  if (__builtin_popcount(mask) < 24)
    mask = 0xFFFFFF00u;
  const uint32_t network = self_ip & mask;
  const uint32_t broadcast = network | ~mask;
  ESP_LOGI(TAG, "LAN scan probing %u.%u.%u.%u/%u for V6 HTTP",
           static_cast<unsigned>((network >> 24) & 0xffu),
           static_cast<unsigned>((network >> 16) & 0xffu),
           static_cast<unsigned>((network >> 8) & 0xffu),
           static_cast<unsigned>(network & 0xffu),
           static_cast<unsigned>(__builtin_popcount(mask)));

  uint32_t batch_ips[LAN_CONNECT_BATCH];
  size_t batch_count = 0;
  auto flush_batch = [&]() {
    if (batch_count == 0)
      return;
    bool open[LAN_CONNECT_BATCH]{};
    probe_tcp80_batch_(batch_ips, batch_count, LAN_CONNECT_TIMEOUT_MS, open);
    for (size_t i = 0; i < batch_count && found_count < MAX_LAN_CANDIDATES; i++) {
      if (!open[i])
        continue;
      char ip_str[16];
      format_ipv4_(batch_ips[i], ip_str, sizeof(ip_str));
      LanScanCandidate candidate{};
      if (!probe_v6_overview_(ip_str, LAN_HTTP_TIMEOUT_MS, candidate.model, sizeof(candidate.model),
                              candidate.firmware, sizeof(candidate.firmware),
                              candidate.pairing_fingerprint, sizeof(candidate.pairing_fingerprint),
                              candidate.ip, sizeof(candidate.ip)))
        continue;
      if (candidate.ip[0] == '\0')
        std::strncpy(candidate.ip, ip_str, sizeof(candidate.ip) - 1);
      make_node_id_(nullptr, candidate.ip, candidate.id, sizeof(candidate.id));
      std::strncpy(candidate.hostname, candidate.ip, sizeof(candidate.hostname) - 1);
      found[found_count++] = candidate;
      ESP_LOGI(TAG, "LAN scan found V6 at %s (%s)", candidate.ip, candidate.firmware);
    }
    batch_count = 0;
  };

  for (uint32_t host = network + 1; host < broadcast; host++) {
    if (host == self_ip)
      continue;
    batch_ips[batch_count++] = host;
    if (batch_count == static_cast<size_t>(LAN_CONNECT_BATCH))
      flush_batch();
    if (found_count >= MAX_LAN_CANDIDATES)
      break;
  }
  if (found_count < MAX_LAN_CANDIDATES)
    flush_batch();

  bool healed = false;
  if (take_state_lock_(250)) {
    // A paired V6 that answers with its own fingerprint at a different IP has
    // moved (DHCP, new router or WiFi password): follow it. Polling can't, since
    // the stale address no longer answers or answers as another device.
    for (uint8_t c = 0; c < found_count; c++) {
      if (found[c].pairing_fingerprint[0] == '\0')
        continue;
      for (size_t i = 0; i < model_.node_count(); i++) {
        const auto *node = model_.node(i);
        if (node == nullptr || node->pairing_fingerprint[0] == '\0' ||
            !pairing_fingerprints_match_(node->pairing_fingerprint, found[c].pairing_fingerprint))
          continue;
        const bool hostname_is_ip = node->hostname[0] == '\0' || is_ipv4_text_(node->hostname);
        if (std::strcmp(node->fallback_ip, found[c].ip) == 0 &&
            (!hostname_is_ip || node->hostname[0] == '\0' || std::strcmp(node->hostname, found[c].ip) == 0))
          break;  // already there
        char hostname[sizeof(node->hostname)];
        std::strncpy(hostname, hostname_is_ip ? "" : node->hostname, sizeof(hostname) - 1);
        hostname[sizeof(hostname) - 1] = '\0';
        ESP_LOGI(TAG, "LAN scan: node %s moved to %s", node->node_id, found[c].ip);
        healed |= rehome_node_locked_(i, hostname, found[c].ip);
        break;
      }
    }
    lan_candidate_count_ = found_count;
    for (uint8_t i = 0; i < found_count; i++)
      lan_candidates_[i] = found[i];
    std::strncpy(lan_discovery_, "lan_probe", sizeof(lan_discovery_) - 1);
    lan_discovery_[sizeof(lan_discovery_) - 1] = '\0';
    give_state_lock_();
  }
  ESP_LOGI(TAG, "LAN scan finished, %u V6 candidate(s)", static_cast<unsigned>(found_count));
  if (healed) {
    save_registry_();
    log_event_("info", "commissioning", "LAN scan updated a moved node's address");
    kick_task_(poll_task_handle_);
  }
}

bool LuneTouchCoordinator::set_node_trust(const char *node_id, ::lune_touch::NodeTrust trust,
                                          const char *confirmation, char *response,
                                          size_t capacity) {
  if (trust == ::lune_touch::NodeTrust::UNPAIRED) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"invalid_trust\"}");
    return false;
  }
  if (!take_state_lock_(100)) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"coordinator_busy\"}");
    return false;
  }
  const ::lune_touch::PairedNode *target = nullptr;
  for (size_t i = 0; i < model_.node_count(); i++) {
    const auto *node = model_.node(i);
    if (node != nullptr && node_id != nullptr && std::strcmp(node->node_id, node_id) == 0) {
      target = node;
      break;
    }
  }
  if (target == nullptr) {
    give_state_lock_();
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"node_not_found\"}");
    return false;
  }
  if (trust == ::lune_touch::NodeTrust::TRUSTED && target->pairing_fingerprint[0] == '\0') {
    give_state_lock_();
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"identity_required\"}");
    return false;
  }
  if (trust == ::lune_touch::NodeTrust::TRUSTED &&
      (confirmation == nullptr ||
       !pairing_fingerprints_match_(confirmation, target->pairing_fingerprint))) {
    give_state_lock_();
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"fingerprint_confirmation_required\"}");
    return false;
  }
  if (!model_.update_node_trust(node_id, trust)) {
    give_state_lock_();
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"node_not_found\"}");
    return false;
  }
  give_state_lock_();
  save_registry_();
  char node_id_esc[48];
  json_escape_(node_id != nullptr ? node_id : "", node_id_esc, sizeof(node_id_esc));
  snprintf(response, capacity, "{\"result\":\"stored\",\"node_id\":\"%s\",\"trust\":\"%s\"}",
           node_id_esc, ::lune_touch::node_trust_name(trust));
  char event[112];
  snprintf(event, sizeof(event), "node %s trust %s",
           node_id != nullptr ? node_id : "", ::lune_touch::node_trust_name(trust));
  log_event_("info", "commissioning", event);
  return true;
}

bool LuneTouchCoordinator::set_node_profile(const char *node_id, const char *name,
                                            char *response, size_t capacity) {
  if (node_id == nullptr || node_id[0] == '\0' || name == nullptr || name[0] == '\0') {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"name_required\"}");
    return false;
  }
  if (!take_state_lock_(100)) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"coordinator_busy\"}");
    return false;
  }
  const bool updated = model_.update_node_name(node_id, name);
  give_state_lock_();
  if (!updated) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"node_not_found\"}");
    return false;
  }
  save_registry_();
  char node_id_esc[48];
  char name_esc[80];
  json_escape_(node_id, node_id_esc, sizeof(node_id_esc));
  json_escape_(name, name_esc, sizeof(name_esc));
  snprintf(response, capacity, "{\"result\":\"stored\",\"node_id\":\"%s\",\"name\":\"%s\"}",
           node_id_esc, name_esc);
  char event[112];
  snprintf(event, sizeof(event), "renamed node %s", node_id);
  log_event_("info", "commissioning", event);
  return true;
}

bool LuneTouchCoordinator::rehome_node_locked_(size_t index, const char *hostname,
                                               const char *fallback_ip) {
  const auto *node = model_.node(index);
  if (node == nullptr || index >= ::lune_touch::MAX_NODES)
    return false;
  char node_id[sizeof(node->node_id)];
  std::strncpy(node_id, node->node_id, sizeof(node_id) - 1);
  node_id[sizeof(node_id) - 1] = '\0';
  if (model_.update_node_host(node_id, hostname, fallback_ip) < 0)
    return false;
  // A remembered host would keep commands going to the old address.
  node_last_success_host_[index][0] = '\0';
  return true;
}

bool LuneTouchCoordinator::set_node_host(const char *node_id, const char *host, char *response,
                                         size_t capacity) {
  if (node_id == nullptr || node_id[0] == '\0') {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"node_not_found\"}");
    return false;
  }
  const bool ip = is_ipv4_text_(host);
  if (!ip && !is_hostname_text_(host)) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"invalid_host\"}");
    return false;
  }
  if (!take_state_lock_(250)) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"coordinator_busy\"}");
    return false;
  }
  int index = -1;
  for (size_t i = 0; i < model_.node_count(); i++) {
    const auto *node = model_.node(i);
    if (node != nullptr && std::strcmp(node->node_id, node_id) == 0) {
      index = static_cast<int>(i);
      break;
    }
  }
  bool stored = false;
  if (index >= 0) {
    const auto *node = model_.node(static_cast<size_t>(index));
    // An IP replaces both fields (a stale hostname would be tried first); a
    // hostname keeps the last known IP as fallback — the poll refreshes it and
    // the fingerprint check rejects a host that answers as another V6.
    char keep_ip[sizeof(node->fallback_ip)];
    std::strncpy(keep_ip, node->fallback_ip, sizeof(keep_ip) - 1);
    keep_ip[sizeof(keep_ip) - 1] = '\0';
    stored = ip ? rehome_node_locked_(static_cast<size_t>(index), "", host)
                : rehome_node_locked_(static_cast<size_t>(index), host, keep_ip);
  }
  give_state_lock_();
  if (!stored) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"node_not_found\"}");
    return false;
  }
  save_registry_();
  char node_id_esc[48];
  char host_esc[136];
  json_escape_(node_id, node_id_esc, sizeof(node_id_esc));
  json_escape_(host, host_esc, sizeof(host_esc));
  snprintf(response, capacity, "{\"result\":\"stored\",\"node_id\":\"%s\",\"host\":\"%s\"}",
           node_id_esc, host_esc);
  char event[128];
  snprintf(event, sizeof(event), "node %s moved to %s", node_id, host);
  log_event_("info", "commissioning", event);
  kick_task_(poll_task_handle_);
  return true;
}

bool LuneTouchCoordinator::remove_node(const char *node_id, const char *confirmation,
                                       char *response, size_t capacity) {
  if (node_id == nullptr || node_id[0] == '\0' ||
      confirmation == nullptr || std::strcmp(confirmation, node_id) != 0) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"confirmation_required\"}");
    return false;
  }
  if (!take_state_lock_(250)) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"coordinator_busy\"}");
    return false;
  }
  const bool removed = model_.remove_node(node_id);
  give_state_lock_();
  if (!removed) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"node_not_found\"}");
    return false;
  }
  save_registry_();
  char node_id_esc[48];
  json_escape_(node_id != nullptr ? node_id : "", node_id_esc, sizeof(node_id_esc));
  snprintf(response, capacity, "{\"result\":\"removed\",\"node_id\":\"%s\"}", node_id_esc);
  char event[112];
  snprintf(event, sizeof(event), "removed node %s", node_id != nullptr ? node_id : "");
  log_event_("warn", "commissioning", event);
  return true;
}

bool LuneTouchCoordinator::reset_registry(const char *confirmation, char *response, size_t capacity) {
  if (confirmation == nullptr || std::strcmp(confirmation, "reset-registry") != 0) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"confirmation_required\"}");
    return false;
  }
  if (!take_state_lock_(100)) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"coordinator_busy\"}");
    return false;
  }
  model_ = {};
  model_.set_node_stale_after_ms(node_stale_after_ms_);
  ledger_ = {};
  std::memset(node_last_success_host_, 0, sizeof(node_last_success_host_));
  std::memset(node_last_failure_, 0, sizeof(node_last_failure_));
  last_poll_error_[0] = '\0';
  forecast_decision_count_ = 0;
  last_forecast_dispatch_ = {};
  give_state_lock_();

  nvs_handle_t handle;
  if (open_touch_registry_nvs_(NVS_READWRITE, &handle) == ESP_OK) {
    nvs_erase_all(handle);
    nvs_commit(handle);
    nvs_close(handle);
  }
  if (nvs_open(LEDGER_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
    nvs_erase_all(handle);
    nvs_commit(handle);
    nvs_close(handle);
  }
  snprintf(response, capacity,
           "{\"result\":\"reset\",\"registry\":\"cleared\",\"ledger\":\"cleared\","
           "\"forecast_location\":\"kept\"}");
  log_event_("warn", "recovery", "registry and ledger reset");
  return true;
}

bool LuneTouchCoordinator::bind_room(const char *room_id, const char *room_name, size_t node_index, size_t zone_index,
                                     char *response, size_t capacity) {
  if (!take_state_lock_(250)) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"coordinator_busy\"}");
    return false;
  }
  const bool bound = model_.bind_zone(room_id, room_name, node_index, zone_index);
  give_state_lock_();
  if (!bound) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"invalid_zone_binding\"}");
    return false;
  }
  save_registry_();
  char room_id_esc[48];
  json_escape_(room_id != nullptr ? room_id : "", room_id_esc, sizeof(room_id_esc));
  snprintf(response, capacity,
           "{\"result\":\"stored\",\"room_id\":\"%s\",\"node_index\":%u,\"zone_index\":%u}",
           room_id_esc, static_cast<unsigned>(node_index), static_cast<unsigned>(zone_index));
  char event[112];
  snprintf(event, sizeof(event), "mapped %s to node %u zone %u",
           room_id != nullptr ? room_id : "", static_cast<unsigned>(node_index),
           static_cast<unsigned>(zone_index));
  log_event_("info", "zones", event);
  return true;
}

bool LuneTouchCoordinator::set_zone_comfort(const char *room_id, float comfort_setpoint_c, uint8_t priority,
                                            float comfort_bias_c, char *response, size_t capacity) {
  if (!std::isfinite(comfort_setpoint_c) || comfort_setpoint_c < 5.0f || comfort_setpoint_c > 35.0f) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"invalid_comfort_setpoint\"}");
    return false;
  }
  if (!std::isfinite(comfort_bias_c)) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"invalid_comfort_bias\"}");
    return false;
  }
  if (!take_state_lock_(250)) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"coordinator_busy\"}");
    return false;
  }
  ::lune_touch::ResolvedRoomLoop loops[::lune_touch::MAX_HOUSE_ZONES]{};
  const size_t loop_count = model_.resolve_room_loops(room_id, loops, ::lune_touch::MAX_HOUSE_ZONES);
  if (loop_count == 0) {
    give_state_lock_();
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"room_not_mapped\"}");
    return false;
  }
  uint8_t target_node_indexes[::lune_touch::MAX_HOUSE_ZONES]{};
  uint8_t target_zones[::lune_touch::MAX_HOUSE_ZONES]{};
  for (size_t i = 0; i < loop_count; i++) {
    if (loops[i].node == nullptr || loops[i].binding == nullptr) {
      give_state_lock_();
      snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"room_not_mapped\"}");
      return false;
    }
    target_node_indexes[i] = loops[i].binding->node_index;
    target_zones[i] = loops[i].binding->zone_index;
    const bool mock = std::strcmp(loops[i].node->firmware, "mock") == 0;
    if (!mock && (!loops[i].node->reachable ||
                  loops[i].node->trust != ::lune_touch::NodeTrust::TRUSTED)) {
      give_state_lock_();
      snprintf(response, capacity,
               "{\"result\":\"rejected\",\"error\":\"v6_control_unavailable\"}");
      return false;
    }
  }
  give_state_lock_();
  for (size_t i = 0; i < loop_count; i++) {
    if (!take_state_lock_(100)) {
      snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"coordinator_busy\"}");
      return false;
    }
    const auto *node = model_.node(target_node_indexes[i]);
    if (node == nullptr) {
      give_state_lock_();
      snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"room_not_mapped\"}");
      return false;
    }
    const ::lune_touch::PairedNode target_node = *node;
    give_state_lock_();
    if (std::strcmp(target_node.firmware, "mock") != 0 &&
        !send_v6_zone_setpoint_(target_node, target_zones[i], comfort_setpoint_c)) {
      snprintf(response, capacity,
               "{\"result\":\"rejected\",\"error\":\"v6_setpoint_failed\","
               "\"updated_loops\":%u,\"required_loops\":%u}",
               static_cast<unsigned>(i), static_cast<unsigned>(loop_count));
      return false;
    }
  }
  if (!take_state_lock_(250)) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"coordinator_busy\"}");
    return false;
  }
  const bool updated = model_.update_zone_comfort(room_id, comfort_setpoint_c, priority, comfort_bias_c);
  if (updated)
    recompute_forecast_decisions_();
  give_state_lock_();
  if (!updated) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"room_not_mapped\"}");
    return false;
  }
  save_registry_();
  const auto resolved = model_.resolve_room(room_id);
  const float stored = resolved.binding != nullptr ? resolved.binding->comfort_setpoint_c : comfort_setpoint_c;
  const float stored_bias = resolved.binding != nullptr ? resolved.binding->comfort_bias_c : comfort_bias_c;
  const float effective = resolved.binding != nullptr
                              ? ::lune_touch::HouseModel::effective_comfort_setpoint_c(*resolved.binding)
                              : stored + stored_bias;
  const uint8_t stored_priority = resolved.binding != nullptr ? resolved.binding->priority : priority;
  char room_id_esc[48];
  json_escape_(room_id != nullptr ? room_id : "", room_id_esc, sizeof(room_id_esc));
  snprintf(response, capacity,
           "{\"result\":\"stored\",\"room_id\":\"%s\",\"comfort_setpoint_c\":%.1f,"
           "\"comfort_bias_c\":%.1f,\"effective_setpoint_c\":%.1f,\"priority\":%u,"
           "\"synced_loops\":%u}",
           room_id_esc, stored, stored_bias, effective,
           static_cast<unsigned>(stored_priority), static_cast<unsigned>(loop_count));
  char event[112];
  snprintf(event, sizeof(event), "comfort %s %.1f C bias %.1f P%u",
           room_id != nullptr ? room_id : "", stored, stored_bias,
           static_cast<unsigned>(stored_priority));
  log_event_("info", "zones", event);
  return true;
}

bool LuneTouchCoordinator::set_zone_schedule(const char *room_id, bool enabled, uint8_t day_mask,
                                             uint16_t start_min, uint16_t end_min, float setpoint_c,
                                             char *response, size_t capacity) {
  if (!std::isfinite(setpoint_c)) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"invalid_schedule_setpoint\"}");
    return false;
  }
  if (start_min > 1439 || end_min > 1440 || start_min >= end_min) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"invalid_schedule_window\"}");
    return false;
  }
  if (enabled && (day_mask & 0x7F) == 0) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"invalid_schedule_days\"}");
    return false;
  }
  if (!take_state_lock_(250)) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"coordinator_busy\"}");
    return false;
  }
  const bool updated = model_.update_zone_schedule(room_id, enabled, day_mask, start_min, end_min, setpoint_c);
  give_state_lock_();
  if (!updated) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"room_not_mapped\"}");
    return false;
  }
  save_registry_();
  const auto resolved = model_.resolve_room(room_id);
  const auto *zone = resolved.binding;
  char room_id_esc[48];
  json_escape_(room_id != nullptr ? room_id : "", room_id_esc, sizeof(room_id_esc));
  snprintf(response, capacity,
           "{\"result\":\"stored\",\"room_id\":\"%s\",\"schedule\":{\"enabled\":%s,"
           "\"day_mask\":%u,\"start_min\":%u,\"end_min\":%u,\"setpoint_c\":%.1f}}",
           room_id_esc,
           zone != nullptr && zone->schedule_enabled ? "true" : "false",
           static_cast<unsigned>(zone != nullptr ? zone->schedule_day_mask : day_mask),
           static_cast<unsigned>(zone != nullptr ? zone->schedule_start_min : start_min),
           static_cast<unsigned>(zone != nullptr ? zone->schedule_end_min : end_min),
           zone != nullptr ? zone->schedule_setpoint_c : setpoint_c);
  char event[112];
  snprintf(event, sizeof(event), "schedule %s %s %u-%u %.1f C",
           room_id != nullptr ? room_id : "",
           zone != nullptr && zone->schedule_enabled ? "on" : "off",
           static_cast<unsigned>(zone != nullptr ? zone->schedule_start_min : start_min),
           static_cast<unsigned>(zone != nullptr ? zone->schedule_end_min : end_min),
           zone != nullptr ? zone->schedule_setpoint_c : setpoint_c);
  log_event_("info", "zones", event);
  return true;
}

bool LuneTouchCoordinator::set_zone_forecast_profile(const char *room_id, uint8_t exterior_walls,
                                                     float wind_exposure, float solar_gain,
                                                     uint8_t thermal_lead_h, float max_offset_c,
                                                     char *response, size_t capacity) {
  if (!std::isfinite(wind_exposure) || !std::isfinite(solar_gain)) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"invalid_forecast_profile\"}");
    return false;
  }
  if (!take_state_lock_(250)) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"coordinator_busy\"}");
    return false;
  }
  const auto target = model_.resolve_room(room_id);
  if (target.node == nullptr || target.binding == nullptr) {
    give_state_lock_();
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"room_not_mapped\"}");
    return false;
  }
  const ::lune_touch::PairedNode target_node = *target.node;
  const uint8_t target_zone = target.binding->zone_index;
  const uint32_t expected_rev = target.binding->v6_data_revision;
  const float area_m2 = target.binding->v6_area_m2;
  char slab[24]{};
  char covering[24]{};
  float thickness = target.binding->floor.active_thickness_cm;
  std::strncpy(slab, target.binding->floor.slab_type, sizeof(slab) - 1);
  std::strncpy(covering, target.binding->floor.covering, sizeof(covering) - 1);
  give_state_lock_();
  if (std::strcmp(target_node.firmware, "mock") != 0) {
    // T4: Touch owns wind/solar — do not push them to V6.
    // Walls write-through via contract physics endpoint when supported.
    if (node_supports_physics_writes_(target_node)) {
      if (!send_v6_zone_physics_(target_node, target_zone, expected_rev, exterior_walls, area_m2,
                                 slab, thickness, covering)) {
        ESP_LOGW(TAG, "V6 physics write-through failed for zone %u",
                 static_cast<unsigned>(target_zone + 1));
      }
    } else {
      char lead[16];
      snprintf(lead, sizeof(lead), "%u", static_cast<unsigned>(thermal_lead_h));
      char walls[16];
      snprintf(walls, sizeof(walls), "%u", static_cast<unsigned>(exterior_walls & 0x0F));
      const bool pushed =
          send_v6_zone_setting_(target_node, target_zone, "number", "zone_exterior_walls", walls) &&
          send_v6_zone_setting_(target_node, target_zone, "number", "zone_thermal_lead_h", lead);
      if (!pushed) {
        ESP_LOGW(TAG, "V6 walls/lead write-through failed for zone %u (no physics_contract)",
                 static_cast<unsigned>(target_zone + 1));
      }
    }
  }
  if (!take_state_lock_(250)) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"coordinator_busy\"}");
    return false;
  }
  const bool updated = model_.update_zone_forecast_profile(room_id, exterior_walls, wind_exposure, solar_gain,
                                                           thermal_lead_h, max_offset_c);
  if (updated)
    recompute_forecast_decisions_();
  give_state_lock_();
  if (!updated) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"room_not_mapped\"}");
    return false;
  }
  save_registry_();
  const auto resolved = model_.resolve_room(room_id);
  const auto *zone = resolved.binding;
  char room_id_esc[48];
  json_escape_(room_id != nullptr ? room_id : "", room_id_esc, sizeof(room_id_esc));
  snprintf(response, capacity,
           "{\"result\":\"stored\",\"room_id\":\"%s\",\"forecast\":{\"exterior_walls\":%u,"
           "\"wind_exposure\":%.2f,\"solar_gain\":%.2f,\"thermal_lead_h\":%u,"
           "\"max_offset_c\":%.2f}}",
           room_id_esc,
           static_cast<unsigned>(zone != nullptr ? zone->exterior_walls : (exterior_walls & 0x0F)),
           zone != nullptr ? zone->wind_exposure : wind_exposure,
           zone != nullptr ? zone->solar_gain : solar_gain,
           static_cast<unsigned>(zone != nullptr ? zone->thermal_lead_h : thermal_lead_h),
           zone != nullptr ? zone->max_offset_c : max_offset_c);
  char event[112];
  snprintf(event, sizeof(event), "forecast profile %s walls %u lead %u",
           room_id != nullptr ? room_id : "",
           static_cast<unsigned>(zone != nullptr ? zone->exterior_walls : (exterior_walls & 0x0F)),
           static_cast<unsigned>(zone != nullptr ? zone->thermal_lead_h : thermal_lead_h));
  log_event_("info", "forecast", event);
  return true;
}

bool LuneTouchCoordinator::update_room_atomically(const char *room_id,
                                                  const ::lune_touch::RoomUpdate &update,
                                                  char *response, size_t capacity) {
  if (!take_state_lock_(250)) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"coordinator_busy\"}");
    return false;
  }
  uint32_t revision = 0;
  const auto result = model_.apply_room_update(room_id, update, &revision);
  if (result == ::lune_touch::RoomUpdateResult::STORED)
    recompute_forecast_decisions_();
  give_state_lock_();
  if (result != ::lune_touch::RoomUpdateResult::STORED) {
    const char *error = result == ::lune_touch::RoomUpdateResult::STALE_REVISION
                            ? "stale_revision"
                            : result == ::lune_touch::RoomUpdateResult::NOT_FOUND
                                  ? "room_not_mapped" : "invalid_room_update";
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"%s\"}", error);
    return false;
  }
  if (take_state_lock_(100)) {
    model_.bump_weights_revision(esphome::millis());
    give_state_lock_();
  }
  save_registry_();
  char room_id_esc[48];
  json_escape_(room_id, room_id_esc, sizeof(room_id_esc));
  snprintf(response, capacity,
           "{\"result\":\"stored\",\"room\":{\"room_id\":\"%s\",\"revision\":%lu,"
           "\"comfort_setpoint_c\":%.1f,\"comfort_bias_c\":%.1f,\"priority\":%u,"
           "\"schedule\":{\"enabled\":%s,\"day_mask\":%u,\"start_min\":%u,\"end_min\":%u,\"setpoint_c\":%.1f},"
           "\"forecast\":{\"exterior_walls\":%u,\"wind_exposure\":%.2f,\"solar_gain\":%.2f,\"thermal_lead_h\":%u,\"max_offset_c\":%.2f},"
           "\"geometry\":{\"total_area_m2\":%.2f,\"physical_weight\":%.2f,\"include_in_house_temperature\":%s}}}",
           room_id_esc, static_cast<unsigned long>(revision), update.comfort_setpoint_c,
           update.comfort_bias_c, static_cast<unsigned>(update.priority),
           update.schedule_enabled ? "true" : "false", static_cast<unsigned>(update.schedule_day_mask & 0x7F),
           static_cast<unsigned>(update.schedule_start_min), static_cast<unsigned>(update.schedule_end_min),
           update.schedule_setpoint_c, static_cast<unsigned>(update.exterior_walls & 0x0F),
           update.wind_exposure, update.solar_gain, static_cast<unsigned>(update.thermal_lead_h),
           update.max_offset_c, update.total_area_m2,
           update.physical_weight > 0.0f ? update.physical_weight : 1.0f,
           update.include_in_house_temperature ? "true" : "false");
  log_event_("info", "weights", "UA weights revised after room update");
  log_event_("info", "zones", "atomic room configuration stored");
  return true;
}

bool LuneTouchCoordinator::create_room(const char *room_id, const char *name, char *response,
                                       size_t capacity) {
  if (room_id == nullptr || room_id[0] == '\0') {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"room_id_required\"}");
    return false;
  }
  if (!take_state_lock_(100)) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"coordinator_busy\"}");
    return false;
  }
  const bool ok = model_.create_room(room_id, name);
  give_state_lock_();
  if (!ok) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"create_failed\"}");
    return false;
  }
  save_registry_();
  char id_esc[48];
  json_escape_(room_id, id_esc, sizeof(id_esc));
  snprintf(response, capacity, "{\"result\":\"stored\",\"room_id\":\"%s\"}", id_esc);
  return true;
}

bool LuneTouchCoordinator::move_group_to_room(const char *node_id, uint8_t zone_1based,
                                              const char *room_id, char *response, size_t capacity) {
  if (node_id == nullptr || room_id == nullptr || zone_1based < 1 || zone_1based > 6) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"invalid_group\"}");
    return false;
  }
  if (!take_state_lock_(100)) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"coordinator_busy\"}");
    return false;
  }
  const char *loop_id = nullptr;
  for (size_t i = 0; i < model_.zone_count(); i++) {
    const auto *zone = model_.zone(i);
    if (zone != nullptr && std::strcmp(zone->node_id, node_id) == 0 &&
        zone->zone_index + 1 == zone_1based) {
      loop_id = zone->loop_id;
      break;
    }
  }
  char reject[48]{};
  const bool ok =
      loop_id != nullptr && model_.move_group_to_room(loop_id, room_id, reject, sizeof(reject));
  give_state_lock_();
  if (!ok) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"%s\"}",
             reject[0] != '\0' ? reject : "move_failed");
    return false;
  }
  save_registry_();
  snprintf(response, capacity, "{\"result\":\"stored\"}");
  return true;
}

bool LuneTouchCoordinator::poll_node_groups_(size_t node_index, const ::lune_touch::PairedNode &node) {
  if (!node_supports_physics_writes_(node))
    return false;  // older V6: zones group_primary/members already ingested
  const char *hosts[2]{};
  size_t host_count = 0;
  if (node.hostname[0] != '\0')
    hosts[host_count++] = node.hostname;
  if (node.fallback_ip[0] != '\0' &&
      (host_count == 0 || std::strcmp(node.fallback_ip, hosts[0]) != 0))
    hosts[host_count++] = node.fallback_ip;
  constexpr size_t BODY_CAP = 4096;
  char *body = alloc_http_body_(BODY_CAP);
  if (body == nullptr)
    return false;
  for (size_t h = 0; h < host_count; h++) {
    char url[160];
    std::snprintf(url, sizeof(url), "http://%s/api/v1/groups", hosts[h]);
    int status = 0;
    if (!fetch_json_(url, body, BODY_CAP, &status, V6_HTTP_TIMEOUT_MS))
      continue;
    JsonDocument doc;
    if (deserializeJson(doc, body))
      continue;
    JsonVariant groups = doc["groups"];
    if (groups.isNull())
      groups = doc["data"]["groups"];
    if (groups.isNull() || !groups.is<JsonArray>()) {
      heap_caps_free(body);
      return true;
    }
    if (!take_state_lock_(100)) {
      heap_caps_free(body);
      return false;
    }
    for (JsonObject g : groups.as<JsonArray>()) {
      const char *primary = g["primary_loop"].as<const char *>();
      if (primary == nullptr || primary[0] == '\0')
        continue;
      int zone_1based = 0;
      for (const char *p = primary; *p != '\0'; ++p) {
        if (*p >= '1' && *p <= '6' && (p[1] == '\0' || p[1] < '0' || p[1] > '9'))
          zone_1based = *p - '0';
      }
      if (zone_1based < 1)
        continue;
      const bool include = g["include_in_house_temperature"] | true;
      JsonArray sensors = g["sensor_ids"].as<JsonArray>();
      const char *mac = nullptr;
      if (!sensors.isNull() && sensors.size() > 0)
        mac = sensors[0].as<const char *>();
      for (size_t zi = 0; zi < model_.zone_count(); zi++) {
        const auto *z = model_.zone(zi);
        if (z == nullptr || z->node_index != node_index || z->zone_index + 1 != zone_1based)
          continue;
        if (mac != nullptr && mac[0] != '\0') {
          model_.apply_v6_physics_mirror(
              z->node_index, z->zone_index, z->exterior_walls, z->v6_area_m2, z->floor,
              z->ua_prior_w_per_k, z->ua_learned_w_per_k, z->ua_effective_w_per_k,
              z->ua_confidence, z->ua_observed_days, z->v6_data_revision, mac,
              z->sensor_id, z->group_id, nullptr);
        }
        const auto *room = model_.room_by_id(z->room_id);
        if (room != nullptr && !include) {
          model_.set_room_geometry(room->room_id, room->total_area_m2, room->physical_weight,
                                   false);
        }
        break;
      }
    }
    model_.refresh_all_room_physics_aggregates();
    give_state_lock_();
    heap_caps_free(body);
    return true;
  }
  heap_caps_free(body);
  return false;
}

void LuneTouchCoordinator::maybe_migrate_walls_to_v6_() {
  // One-shot: push Touch-stored exterior walls to every loop in rooms still needing migration.
  struct Pending {
    ::lune_touch::PairedNode node;
    uint8_t zone_index{0};
    uint32_t rev{0};
    uint8_t walls{0};
    float area{0.0f};
    char slab[24]{};
    char covering[24]{};
    float thickness{0.0f};
    char room_id[32]{};
  };
  Pending pending[::lune_touch::MAX_HOUSE_ZONES];
  size_t count = 0;
  char rooms_done[::lune_touch::MAX_HOUSE_ROOMS][32]{};
  size_t rooms_done_n = 0;
  if (!take_state_lock_(100))
    return;
  for (size_t ri = 0; ri < model_.room_count() && count < ::lune_touch::MAX_HOUSE_ZONES; ri++) {
    const auto *room = model_.room(ri);
    if (room == nullptr || !model_.walls_need_migration(room->room_id))
      continue;
    const uint8_t walls = room->exterior_walls_union;
    for (size_t zi = 0; zi < model_.zone_count() && count < ::lune_touch::MAX_HOUSE_ZONES; zi++) {
      const auto *z = model_.zone(zi);
      if (z == nullptr || !z->enabled || z->is_group_secondary ||
          std::strcmp(z->room_id, room->room_id) != 0)
        continue;
      const auto *node = model_.node(z->node_index);
      if (node == nullptr || !node_supports_physics_writes_(*node))
        continue;
      pending[count].node = *node;
      pending[count].zone_index = z->zone_index;
      pending[count].rev = z->v6_data_revision;
      pending[count].walls = walls;
      pending[count].area = z->v6_area_m2 > 0.0f ? z->v6_area_m2 : room->total_area_m2;
      std::strncpy(pending[count].slab, z->floor.slab_type, sizeof(pending[count].slab) - 1);
      std::strncpy(pending[count].covering, z->floor.covering,
                   sizeof(pending[count].covering) - 1);
      pending[count].thickness = z->floor.active_thickness_cm;
      std::strncpy(pending[count].room_id, room->room_id, sizeof(pending[count].room_id) - 1);
      count++;
    }
  }
  give_state_lock_();
  for (size_t i = 0; i < count; i++) {
    if (send_v6_zone_physics_(pending[i].node, pending[i].zone_index, pending[i].rev,
                              pending[i].walls, pending[i].area, pending[i].slab,
                              pending[i].thickness, pending[i].covering)) {
      bool already = false;
      for (size_t r = 0; r < rooms_done_n; r++) {
        if (std::strcmp(rooms_done[r], pending[i].room_id) == 0) {
          already = true;
          break;
        }
      }
      if (!already && rooms_done_n < ::lune_touch::MAX_HOUSE_ROOMS) {
        std::strncpy(rooms_done[rooms_done_n], pending[i].room_id,
                     sizeof(rooms_done[rooms_done_n]) - 1);
        rooms_done_n++;
      }
    }
  }
  if (rooms_done_n > 0 && take_state_lock_(100)) {
    for (size_t r = 0; r < rooms_done_n; r++)
      model_.mark_walls_migrated(rooms_done[r]);
    give_state_lock_();
    save_registry_();
    log_event_("info", "physics", "walls migrated to V6");
  }
}

void LuneTouchCoordinator::maybe_calibrate_house_physics_() {
  float u_base = 0.0f, u_wall = 0.0f, c_struct = 0.0f;
  char reason[40]{};
  if (!take_state_lock_(100))
    return;
  if (model_.house_calibrated()) {
    give_state_lock_();
    return;
  }
  const bool ready =
      model_.compute_house_calibration(&u_base, &u_wall, &c_struct, reason, sizeof(reason));
  ::lune_touch::PairedNode nodes[::lune_touch::MAX_NODES];
  size_t node_count = 0;
  for (size_t i = 0; i < model_.node_count() && node_count < ::lune_touch::MAX_NODES; i++) {
    const auto *n = model_.node(i);
    if (n != nullptr && node_supports_physics_writes_(*n))
      nodes[node_count++] = *n;
  }
  give_state_lock_();
  if (!ready) {
    if (std::strcmp(reason, "inconsistent_physics") == 0)
      log_event_("warn", "physics", "house calibration blocked: inconsistent_physics");
    return;
  }
  const uint32_t epoch = static_cast<uint32_t>(esphome::millis() / 1000UL);
  bool any = false;
  for (size_t i = 0; i < node_count; i++) {
    if (send_v6_house_physics_(nodes[i], u_base, u_wall, c_struct, epoch))
      any = true;
  }
  if (any && take_state_lock_(100)) {
    model_.apply_house_calibration(u_base, u_wall, c_struct);
    give_state_lock_();
    log_event_("info", "physics", "house u_base/u_wall/c_struct calibrated");
  }
}

bool LuneTouchCoordinator::sync_room_ble_sensor(const char *room_id, char *response,
                                                size_t capacity) {
  return sync_room_ble_mac_(room_id, response, capacity);
}

bool LuneTouchCoordinator::sync_room_ble_mac_(const char *room_id, char *response,
                                              size_t capacity) {
  if (room_id == nullptr || room_id[0] == '\0') {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"invalid_room\"}");
    return false;
  }
  if (!take_state_lock_(100)) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"coordinator_busy\"}");
    return false;
  }
  const auto *room = model_.room_by_id(room_id);
  if (room == nullptr) {
    give_state_lock_();
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"room_not_found\"}");
    return false;
  }
  const ::lune_touch::ZoneBinding *primary = nullptr;
  for (size_t i = 0; i < model_.zone_count(); i++) {
    const auto *z = model_.zone(i);
    if (z != nullptr && std::strcmp(z->loop_id, room->primary_loop_id) == 0) {
      primary = z;
      break;
    }
  }
  char mac[24]{};
  if (primary != nullptr) {
    if (primary->ble_mac[0] != '\0')
      std::strncpy(mac, primary->ble_mac, sizeof(mac) - 1);
    else
      std::strncpy(mac, primary->sensor_id, sizeof(mac) - 1);
  }
  struct Target {
    ::lune_touch::PairedNode node;
    uint8_t zone_index{0};
  } targets[::lune_touch::MAX_HOUSE_ZONES];
  size_t tcount = 0;
  if (mac[0] == '\0') {
    give_state_lock_();
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"no_primary_mac\"}");
    return false;
  }
  for (size_t i = 0; i < model_.zone_count() && tcount < ::lune_touch::MAX_HOUSE_ZONES; i++) {
    const auto *z = model_.zone(i);
    if (z == nullptr || !z->enabled || z->is_group_secondary ||
        std::strcmp(z->room_id, room_id) != 0)
      continue;
    if (primary != nullptr && z->node_index == primary->node_index)
      continue;
    const auto *node = model_.node(z->node_index);
    if (node == nullptr || !node_supports_physics_writes_(*node))
      continue;
    targets[tcount].node = *node;
    targets[tcount].zone_index = z->zone_index;
    tcount++;
  }
  give_state_lock_();
  size_t ok = 0;
  for (size_t i = 0; i < tcount; i++) {
    // Authority write of room-temperature binding MAC onto peer V6 groups.
    if (send_v6_zone_setting_(targets[i].node, targets[i].zone_index, "text",
                              "zone_ble_mac", mac))
      ok++;
  }
  if (ok == 0) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"sync_failed\"}");
    return false;
  }
  snprintf(response, capacity, "{\"result\":\"stored\",\"synced\":%u,\"mac\":\"%s\"}",
           static_cast<unsigned>(ok), mac);
  log_event_("info", "physics", "BLE MAC synced across manifolds");
  return true;
}

bool LuneTouchCoordinator::set_room_sensor(const char *room_id, const char *node_id,
                                           uint8_t zone_1based, char *response, size_t capacity) {
  if (room_id == nullptr || node_id == nullptr || zone_1based < 1 || zone_1based > 6) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"invalid_sensor\"}");
    return false;
  }
  if (!take_state_lock_(100)) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"coordinator_busy\"}");
    return false;
  }
  const char *loop_id = nullptr;
  for (size_t i = 0; i < model_.zone_count(); i++) {
    const auto *zone = model_.zone(i);
    if (zone != nullptr && std::strcmp(zone->node_id, node_id) == 0 &&
        zone->zone_index + 1 == zone_1based) {
      loop_id = zone->loop_id;
      break;
    }
  }
  const bool ok = loop_id != nullptr && model_.set_room_sensor(room_id, loop_id);
  give_state_lock_();
  if (!ok) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"sensor_failed\"}");
    return false;
  }
  save_registry_();
  snprintf(response, capacity, "{\"result\":\"stored\"}");
  return true;
}

bool LuneTouchCoordinator::delete_room_if_empty(const char *room_id, char *response,
                                                size_t capacity) {
  if (!take_state_lock_(100)) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"coordinator_busy\"}");
    return false;
  }
  const bool ok = model_.delete_room_if_empty(room_id);
  give_state_lock_();
  if (!ok) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"room_not_empty\"}");
    return false;
  }
  save_registry_();
  snprintf(response, capacity, "{\"result\":\"deleted\"}");
  return true;
}

bool LuneTouchCoordinator::queue_setpoint_command(const char *room_id, float requested_offset_c, uint32_t ttl_s,
                                                  const char *reason, char *response, size_t capacity) {
  if (!std::isfinite(requested_offset_c)) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"invalid_offset\"}");
    return false;
  }
  if (ttl_s < 60)
    ttl_s = 60;
  if (ttl_s > 21600)
    ttl_s = 21600;

  const uint32_t now = esphome::millis();
  ::lune_touch::ResolvedRoomLoop loops[::lune_touch::MAX_HOUSE_ZONES]{};
  if (!take_state_lock_(100)) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"coordinator_busy\"}");
    return false;
  }
  const size_t loop_count = model_.resolve_room_loops(room_id, loops, ::lune_touch::MAX_HOUSE_ZONES);
  if (loop_count == 0) {
    give_state_lock_();
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"room_not_mapped\"}");
    return false;
  }
  give_state_lock_();
  size_t accepted = 0;
  char command_id[20]{};
  snprintf(command_id, sizeof(command_id), "room-%08lx", static_cast<unsigned long>(now));
  size_t off = 0;
  appendf_(response, capacity, off, "{\"command_id\":\"%s\",\"room_id\":\"%s\",\"loops\":[",
           command_id, room_id);
  for (size_t i = 0; i < loop_count; i++) {
    const auto &target = loops[i];
    ::lune_touch::CommandRecord final_record{};
    if (target.node != nullptr && target.binding != nullptr) {
      std::strncpy(final_record.request_id, command_id, sizeof(final_record.request_id) - 1);
      std::strncpy(final_record.source, "dashboard", sizeof(final_record.source) - 1);
      std::strncpy(final_record.reason, reason != nullptr && reason[0] != '\0' ? reason : "dashboard command",
                   sizeof(final_record.reason) - 1);
      final_record.node_index = target.binding->node_index;
      final_record.zone_index = target.binding->zone_index;
      final_record.requested_offset_c = requested_offset_c;
      stamp_command_timing_(&final_record, now, ttl_s);
      stamp_command_target_(&final_record, target.node, target.binding);
      const bool mock = std::strcmp(target.node->firmware, "mock") == 0;
      if (!target.node->reachable && !mock)
        final_record.result = ::lune_touch::CommandResult::BLOCKED_UNREACHABLE;
      else if (target.node->trust != ::lune_touch::NodeTrust::TRUSTED)
        final_record.result = ::lune_touch::CommandResult::BLOCKED_UNTRUSTED;
      else if (model_.is_node_stale(target.binding->node_index, now) && !mock)
        final_record.result = ::lune_touch::CommandResult::BLOCKED_STALE;
      else {
        char host[64]{};
        std::strncpy(host, node_last_success_host_[target.binding->node_index], sizeof(host) - 1);
        if (!send_v6_setpoint_command_(*target.node, target.binding->zone_index, final_record, ttl_s,
                                       &final_record, host))
          final_record.result = ::lune_touch::CommandResult::FAILED;
      }
    }
    if (final_record.result == ::lune_touch::CommandResult::ACCEPTED ||
        final_record.result == ::lune_touch::CommandResult::PENDING)
      accepted++;
    ledger_.append(final_record);
    appendf_(response, capacity, off,
             "%s{\"loop_id\":\"%s\",\"node_id\":\"%s\",\"zone_index\":%u,"
             "\"result\":\"%s\",\"accepted_offset_c\":%.2f}",
             i == 0 ? "" : ",", final_record.loop_id,
             final_record.node_id, static_cast<unsigned>(final_record.zone_index),
             ::lune_touch::command_result_name(final_record.result), final_record.accepted_offset_c);
  }
  const auto outcome = ::lune_touch::room_command_outcome(accepted, loop_count);
  appendf_(response, capacity, off, "],\"result\":\"%s\",\"ttl_s\":%lu,"
           "\"partial_application\":%s}",
           ::lune_touch::room_command_outcome_name(outcome),
           static_cast<unsigned long>(ttl_s),
           outcome == ::lune_touch::RoomCommandOutcome::PARTIAL ? "true" : "false");
  save_ledger_();
  log_event_(outcome == ::lune_touch::RoomCommandOutcome::ACCEPTED ? "info" : "warn",
             "commands", outcome == ::lune_touch::RoomCommandOutcome::PARTIAL
                             ? "room command partially applied; child results retained"
                             : "room command failed");
  // V6 commands cannot be rolled back safely across a node outage. A partial
  // application is explicit, persisted per loop, and never presented as room success.
  return outcome == ::lune_touch::RoomCommandOutcome::ACCEPTED;
}

bool LuneTouchCoordinator::request_motor_action(const char *room_id, const char *action,
                                                const char *confirmation, char *response,
                                                size_t capacity) {
  const char *v6_command = nullptr;
  const char *requested_action = action != nullptr ? action : "";
  if (std::strcmp(requested_action, "reset_fault") == 0) {
    v6_command = "motor_reset_fault";
  } else if (std::strcmp(requested_action, "reset_learned") == 0) {
    v6_command = "motor_reset_learned_factors";
  } else if (std::strcmp(requested_action, "relearn") == 0) {
    v6_command = "motor_reset_and_relearn";
  } else {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"invalid_motor_action\"}");
    return false;
  }
  if ((std::strcmp(requested_action, "reset_learned") == 0 ||
       std::strcmp(requested_action, "relearn") == 0) &&
      (confirmation == nullptr || std::strcmp(confirmation, requested_action) != 0)) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"confirmation_required\"}");
    return false;
  }

  const uint32_t now = esphome::millis();
  ::lune_touch::PairedNode target_node{};
  uint8_t target_node_index = 0;
  uint8_t target_zone = 0;
  bool target_stale = true;
  char preferred_host[64]{};
  if (!take_state_lock_(100)) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"coordinator_busy\"}");
    return false;
  }
  const auto resolved = model_.resolve_room(room_id);
  if (resolved.node == nullptr || resolved.binding == nullptr) {
    give_state_lock_();
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"room_not_mapped\"}");
    return false;
  }
  target_node = *resolved.node;
  target_node_index = resolved.binding->node_index;
  target_zone = resolved.binding->zone_index;
  target_stale = model_.is_node_stale(target_node_index, now);
  if (target_node_index < ::lune_touch::MAX_NODES) {
    std::strncpy(preferred_host, node_last_success_host_[target_node_index],
                 sizeof(preferred_host) - 1);
    preferred_host[sizeof(preferred_host) - 1] = '\0';
  }
  give_state_lock_();

  const bool is_mock_node = std::strcmp(target_node.firmware, "mock") == 0;
  const char *result = "accepted";
  const char *error = "";
  if (!target_node.reachable && !is_mock_node) {
    result = "blocked_unreachable";
    error = "node_unreachable";
  } else if (target_node.trust != ::lune_touch::NodeTrust::TRUSTED) {
    result = "blocked_untrusted";
    error = "node_not_trusted";
  } else if (target_stale && !is_mock_node) {
    result = "blocked_stale";
    error = "node_stale";
  } else if (!is_mock_node) {
    if (!esphome::network::is_connected()) {
      result = "failed";
      error = "network_offline";
    } else {
      const char *hosts[3]{};
      size_t host_count = 0;
      auto add_host = [&](const char *host) {
        if (host == nullptr || host[0] == '\0' || host_count >= 3)
          return;
        for (size_t i = 0; i < host_count; i++) {
          if (std::strcmp(hosts[i], host) == 0)
            return;
        }
        hosts[host_count++] = host;
      };
      add_host(preferred_host);
      add_host(target_node.hostname);
      add_host(target_node.fallback_ip);
      if (host_count == 0) {
        result = "failed";
        error = "missing_host";
      } else {
        char payload[128];
        snprintf(payload, sizeof(payload), "{\"command\":\"%s\",\"zone\":%u}",
                 v6_command, static_cast<unsigned>(target_zone + 1));
        bool accepted = false;
        bool rejected = false;
        for (size_t i = 0; i < host_count; i++) {
          char url[288];
          snprintf(url, sizeof(url), "http://%s/api/v1/commands", hosts[i]);
          char body[384];
          int status = 0;
          if (!post_json_(url, payload, body, sizeof(body), &status)) {
            ESP_LOGD(TAG, "V6 motor command failed via %s (%d)", hosts[i], status);
            continue;
          }
          JsonDocument doc;
          const DeserializationError err = deserializeJson(doc, body);
          if (err || doc["ok"] == false) {
            rejected = true;
            ESP_LOGW(TAG, "V6 motor command rejected via %s", hosts[i]);
            continue;
          }
          accepted = true;
          break;
        }
        if (!accepted) {
          result = "failed";
          error = rejected ? "v6_rejected" : "post_failed";
        }
      }
    }
  }

  char action_esc[32];
  char command_esc[48];
  char target_node_esc[48];
  char error_esc[48];
  json_escape_(action != nullptr ? action : "", action_esc, sizeof(action_esc));
  json_escape_(v6_command, command_esc, sizeof(command_esc));
  json_escape_(target_node.node_id, target_node_esc, sizeof(target_node_esc));
  json_escape_(error, error_esc, sizeof(error_esc));
  snprintf(response, capacity,
           "{\"result\":\"%s\",\"action\":\"%s\",\"v6_command\":\"%s\","
           "\"target_node\":\"%s\",\"zone_index\":%u,\"error\":\"%s\"}",
           result, action_esc, command_esc, target_node_esc,
           static_cast<unsigned>(target_zone), error_esc);
  char event[112];
  snprintf(event, sizeof(event), "motor %s %s", action != nullptr ? action : "", result);
  log_event_(std::strcmp(result, "accepted") == 0 ? "info" : "warn", "recovery", event);
  return true;
}

bool LuneTouchCoordinator::set_forecast_location(float latitude, float longitude, const char *mode,
                                                 char *response, size_t capacity) {
  if (!std::isfinite(latitude) || !std::isfinite(longitude) ||
      latitude < -90.0f || latitude > 90.0f || longitude < -180.0f || longitude > 180.0f ||
      (std::fabs(latitude) < 0.0001f && std::fabs(longitude) < 0.0001f)) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"invalid_location\"}");
    return false;
  }
  if (!take_state_lock_(100)) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"coordinator_busy\"}");
    return false;
  }
  char saved_mode[sizeof(forecast_location_mode_)]{};
  forecast_latitude_ = latitude;
  forecast_longitude_ = longitude;
  std::strncpy(forecast_location_mode_, mode != nullptr && mode[0] != '\0' ? mode : "manual",
               sizeof(forecast_location_mode_) - 1);
  forecast_location_mode_[sizeof(forecast_location_mode_) - 1] = '\0';
  std::strncpy(saved_mode, forecast_location_mode_, sizeof(saved_mode) - 1);
  std::strncpy(forecast_status_, "stale", sizeof(forecast_status_) - 1);
  forecast_status_[sizeof(forecast_status_) - 1] = '\0';
  forecast_last_error_[0] = '\0';
  forecast_last_fetch_ms_ = 0;
  forecast_fetch_epoch_s_ = 0;
  forecast_provider_timezone_[0] = '\0';
  forecast_fetch_requested_ = true;
  forecast_task_fetch_pending_ = true;
  forecast_hours_count_ = 0;
  forecast_decision_count_ = 0;
  forecast_cache_restored_ = false;
  forecast_boot_refresh_pending_ = false;
  last_forecast_dispatch_ = {};
  give_state_lock_();
  save_forecast_settings_(latitude, longitude, saved_mode);
  clear_forecast_cache_();
  kick_task_(forecast_task_handle_);
  snprintf(response, capacity,
           "{\"result\":\"saved\",\"latitude\":%.6f,\"longitude\":%.6f,\"status\":\"stale\","
           "\"fetch_pending\":true}",
           latitude, longitude);
  log_event_("info", "forecast", "location updated");
  return true;
}

bool LuneTouchCoordinator::set_weather_settings(float max_boost_c, char *response, size_t capacity) {
  if (!std::isfinite(max_boost_c)) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"invalid_max_boost\"}");
    return false;
  }
  const float stored = std::max(0.0f, std::min(5.0f, max_boost_c));
  if (!take_state_lock_(100)) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"coordinator_busy\"}");
    return false;
  }
  weather_max_boost_c_ = stored;
  weather_max_boost_configured_ = true;
  weather_max_boost_seeded_from_v6_ = true;
  recompute_forecast_decisions_();
  give_state_lock_();
  save_settings_();
  snprintf(response, capacity, "{\"result\":\"saved\",\"weather\":{\"max_boost_c\":%.1f}}", stored);
  log_event_("info", "settings", "weather settings updated");
  return true;
}

bool LuneTouchCoordinator::set_settings(const char *coordinator_name, const char *install_id,
                                        const char *site_label, const char *install_mode,
                                        bool has_asgard_enabled, bool asgard_enabled,
                                        const char *asgard_mode, const char *authority_leader_node_id,
                                        const char *authority_coordinator_id, const char *authority_shared_key,
                                        bool has_display_idle_timeout, uint32_t display_idle_timeout_s,
                                        char *response, size_t capacity) {
  if ((coordinator_name == nullptr || coordinator_name[0] == '\0') &&
      (install_id == nullptr || install_id[0] == '\0') &&
      (site_label == nullptr || site_label[0] == '\0') &&
      (install_mode == nullptr || install_mode[0] == '\0') &&
      !has_asgard_enabled &&
      (asgard_mode == nullptr || asgard_mode[0] == '\0') &&
      (authority_leader_node_id == nullptr || authority_leader_node_id[0] == '\0') &&
      (authority_coordinator_id == nullptr || authority_coordinator_id[0] == '\0') &&
      (authority_shared_key == nullptr || authority_shared_key[0] == '\0') &&
      !has_display_idle_timeout) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"settings_required\"}");
    return false;
  }
  if (has_display_idle_timeout && !display_idle_timeout_allowed_(display_idle_timeout_s)) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"invalid_display_idle_timeout\"}");
    return false;
  }
  if (install_mode != nullptr && install_mode[0] != '\0' &&
      std::strcmp(install_mode, "commissioning") != 0 &&
      std::strcmp(install_mode, "active") != 0 &&
      std::strcmp(install_mode, "service") != 0) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"invalid_install_mode\"}");
    return false;
  }
  if (asgard_mode != nullptr && asgard_mode[0] != '\0' &&
      std::strcmp(asgard_mode, "advisory") != 0 &&
      std::strcmp(asgard_mode, "disabled") != 0) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"invalid_asgard_mode\"}");
    return false;
  }
  if (!take_state_lock_(100)) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"coordinator_busy\"}");
    return false;
  }
  if (coordinator_name != nullptr && coordinator_name[0] != '\0') {
    std::strncpy(coordinator_name_, coordinator_name, sizeof(coordinator_name_) - 1);
    coordinator_name_[sizeof(coordinator_name_) - 1] = '\0';
  }
  if (install_id != nullptr && install_id[0] != '\0') {
    std::strncpy(install_id_, install_id, sizeof(install_id_) - 1);
    install_id_[sizeof(install_id_) - 1] = '\0';
  }
  if (site_label != nullptr && site_label[0] != '\0') {
    std::strncpy(site_label_, site_label, sizeof(site_label_) - 1);
    site_label_[sizeof(site_label_) - 1] = '\0';
  }
  if (install_mode != nullptr && install_mode[0] != '\0') {
    std::strncpy(install_mode_, install_mode, sizeof(install_mode_) - 1);
    install_mode_[sizeof(install_mode_) - 1] = '\0';
  }
  if (has_asgard_enabled)
    asgard_enabled_ = asgard_enabled;
  if (asgard_mode != nullptr && asgard_mode[0] != '\0') {
    std::strncpy(asgard_mode_, asgard_mode, sizeof(asgard_mode_) - 1);
    asgard_mode_[sizeof(asgard_mode_) - 1] = '\0';
    if (std::strcmp(asgard_mode_, "disabled") == 0)
      asgard_enabled_ = false;
  }
  if (authority_leader_node_id != nullptr && authority_leader_node_id[0] != '\0')
    std::strncpy(authority_leader_node_id_, authority_leader_node_id, sizeof(authority_leader_node_id_) - 1);
  if (authority_coordinator_id != nullptr && authority_coordinator_id[0] != '\0')
    std::strncpy(authority_coordinator_id_, authority_coordinator_id, sizeof(authority_coordinator_id_) - 1);
  if (authority_shared_key != nullptr && authority_shared_key[0] != '\0')
    std::strncpy(authority_shared_key_, authority_shared_key, sizeof(authority_shared_key_) - 1);
  if ((authority_leader_node_id != nullptr && authority_leader_node_id[0] != '\0') ||
      (authority_coordinator_id != nullptr && authority_coordinator_id[0] != '\0') ||
      (authority_shared_key != nullptr && authority_shared_key[0] != '\0')) {
    authority_expires_at_ms_ = 0;
    std::strncpy(authority_state_, "no_publisher", sizeof(authority_state_) - 1);
    std::strncpy(authority_reason_, "authority settings changed", sizeof(authority_reason_) - 1);
  }
  if (has_display_idle_timeout)
    display_idle_timeout_s_ = static_cast<uint16_t>(display_idle_timeout_s);
  save_settings_();
  give_state_lock_();
  snprintf(response, capacity, "{\"result\":\"saved\",\"install_mode\":\"%s\","
           "\"asgard_enabled\":%s,\"asgard_mode\":\"%s\",\"authority_configured\":%s,"
           "\"display\":{\"idle_timeout_s\":%u}}",
           install_mode_, asgard_enabled_ ? "true" : "false", asgard_mode_,
           authority_leader_node_id_[0] && authority_coordinator_id_[0] && authority_shared_key_[0] ? "true" : "false",
           static_cast<unsigned>(display_idle_timeout_s_));
  log_event_("info", "settings", "coordinator settings updated");
  return true;
}

bool LuneTouchCoordinator::set_heat_source_settings(
    bool has_enabled, bool enabled, const char *host, uint16_t port,
    const char *weighted_temperature_variable, uint16_t push_interval_s,
    char *response, size_t capacity, bool has_write_url, const char *write_url_template,
    bool has_read_url, const char *read_url_template, bool has_declared_target,
    float declared_target_c, const char *climate_entity, bool has_target_sync,
    bool target_sync_enabled, const char *bias_entity, const char *dhw_entity,
    const char *legionella_entity, const char *defrost_entity, const char *trim_mode,
    const char *v6_control_mode, const char *house_weighting, const char *type,
    bool has_odin_plan, bool odin_plan_enabled, bool has_absorb_arm,
    bool absorb_arm_enabled, bool has_arm_energy_cost_modulation,
    bool arm_energy_cost_modulation, const char *plan_source_mode,
    const char *odin_host, uint16_t odin_port, bool has_mqtt, bool mqtt_enabled,
    const char *mqtt_host, uint16_t mqtt_port, const char *mqtt_username,
    const char *mqtt_password, const char *mqtt_topic_prefix, const char *mqtt_hp_id) {
  if (!has_enabled && (host == nullptr || host[0] == '\0') && port == 0 &&
      (weighted_temperature_variable == nullptr || weighted_temperature_variable[0] == '\0') &&
      push_interval_s == 0 && !has_write_url && !has_read_url &&
      (climate_entity == nullptr || climate_entity[0] == '\0') && !has_target_sync &&
      (bias_entity == nullptr || bias_entity[0] == '\0') &&
      (trim_mode == nullptr || trim_mode[0] == '\0') &&
      (v6_control_mode == nullptr || v6_control_mode[0] == '\0') &&
      (house_weighting == nullptr || house_weighting[0] == '\0') &&
      (type == nullptr || type[0] == '\0') && !has_odin_plan) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"settings_required\"}");
    return false;
  }
  if (type != nullptr && type[0] != '\0' && !heat_source_type_supported_(type)) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"unsupported_heat_source_type\"}");
    return false;
  }
  if (type != nullptr && std::strcmp(type, "generic_http") == 0) {
    if (!has_write_url || !has_read_url || write_url_template == nullptr ||
        write_url_template[0] == '\0' || read_url_template == nullptr ||
        read_url_template[0] == '\0') {
      snprintf(response, capacity,
               "{\"result\":\"rejected\",\"error\":\"generic_http_requires_url_templates\"}");
      return false;
    }
  }
  if ((host != nullptr && std::strpbrk(host, " /?#") != nullptr) ||
      (push_interval_s != 0 && (push_interval_s < 5 || push_interval_s > 3600))) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"invalid_heat_source_settings\"}");
    return false;
  }
  if (weighted_temperature_variable != nullptr && weighted_temperature_variable[0] != '\0') {
    for (const char *p = weighted_temperature_variable; *p != '\0'; ++p) {
      const unsigned char c = static_cast<unsigned char>(*p);
      if (c < 0x20 || c > 0x7e) {
        snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"invalid_weighted_temperature_variable\"}");
        return false;
      }
    }
  }
  if (has_write_url &&
      !asgard_url::valid_url_template(write_url_template != nullptr ? write_url_template : "", true)) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"invalid_write_url_template\"}");
    return false;
  }
  if (has_read_url &&
      !asgard_url::valid_url_template(read_url_template != nullptr ? read_url_template : "", false)) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"invalid_read_url_template\"}");
    return false;
  }
  if (!take_state_lock_(100)) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"coordinator_busy\"}");
    return false;
  }
  if (has_enabled)
    heat_source_.enabled = enabled;
  if (type != nullptr && type[0] != '\0') {
    std::strncpy(heat_source_.type, type, sizeof(heat_source_.type) - 1);
    heat_source_.type[sizeof(heat_source_.type) - 1] = '\0';
  }
  normalize_heat_source_type_(heat_source_.type, sizeof(heat_source_.type));
  if (host != nullptr && host[0] != '\0') {
    std::strncpy(heat_source_.host, host, sizeof(heat_source_.host) - 1);
    heat_source_.host[sizeof(heat_source_.host) - 1] = '\0';
  }
  if (port != 0)
    heat_source_.port = port;
  if (weighted_temperature_variable != nullptr && weighted_temperature_variable[0] != '\0') {
    std::strncpy(heat_source_.weighted_temperature_variable, weighted_temperature_variable,
                 sizeof(heat_source_.weighted_temperature_variable) - 1);
    heat_source_.weighted_temperature_variable[sizeof(heat_source_.weighted_temperature_variable) - 1] = '\0';
  }
  if (push_interval_s != 0)
    heat_source_.push_interval_s = push_interval_s;
  if (has_write_url) {
    if (write_url_template == nullptr || write_url_template[0] == '\0') {
      heat_source_.write_url_template[0] = '\0';
    } else {
      std::strncpy(heat_source_.write_url_template, write_url_template,
                   sizeof(heat_source_.write_url_template) - 1);
      heat_source_.write_url_template[sizeof(heat_source_.write_url_template) - 1] = '\0';
    }
  }
  if (has_read_url) {
    if (read_url_template == nullptr || read_url_template[0] == '\0') {
      heat_source_.read_url_template[0] = '\0';
    } else {
      std::strncpy(heat_source_.read_url_template, read_url_template,
                   sizeof(heat_source_.read_url_template) - 1);
      heat_source_.read_url_template[sizeof(heat_source_.read_url_template) - 1] = '\0';
    }
  }
  if (has_declared_target) {
    if (std::isfinite(declared_target_c) && declared_target_c >= 5.0f && declared_target_c <= 35.0f) {
      heat_source_.declared_target_c = declared_target_c;
      heat_source_.has_declared_target = true;
    } else {
      heat_source_.has_declared_target = false;
      heat_source_.declared_target_c = NAN;
    }
  }
  auto copy_entity = [](char *dest, size_t dest_len, const char *src) {
    if (src == nullptr)
      return;
    std::strncpy(dest, src, dest_len - 1);
    dest[dest_len - 1] = '\0';
  };
  if (climate_entity != nullptr && climate_entity[0] != '\0')
    copy_entity(heat_source_.climate_entity, sizeof(heat_source_.climate_entity), climate_entity);
  if (has_target_sync)
    heat_source_.target_sync_enabled = target_sync_enabled;
  if (bias_entity != nullptr)
    copy_entity(heat_source_.bias_entity, sizeof(heat_source_.bias_entity), bias_entity);
  if (dhw_entity != nullptr)
    copy_entity(heat_source_.dhw_entity, sizeof(heat_source_.dhw_entity), dhw_entity);
  if (legionella_entity != nullptr)
    copy_entity(heat_source_.legionella_entity, sizeof(heat_source_.legionella_entity),
                legionella_entity);
  if (defrost_entity != nullptr)
    copy_entity(heat_source_.defrost_entity, sizeof(heat_source_.defrost_entity), defrost_entity);
  if (trim_mode != nullptr && trim_mode[0] != '\0')
    flow_trim_.mode = flow_trim::mode_from_name(trim_mode);
  if (!heat_source_type_is_asgard_(heat_source_.type)) {
    // Generic HTTP only publishes physical temperature; disable Asgard-only writers.
    heat_source_.target_sync_enabled = false;
    heat_source_.climate_entity[0] = '\0';
    heat_source_.bias_entity[0] = '\0';
    heat_source_.dhw_entity[0] = '\0';
    heat_source_.legionella_entity[0] = '\0';
    heat_source_.defrost_entity[0] = '\0';
    flow_trim_.mode = flow_trim::Mode::OFF;
    odin_plan_.enabled = false;
  } else {
    if (heat_source_.climate_entity[0] == '\0')
      copy_entity(heat_source_.climate_entity, sizeof(heat_source_.climate_entity),
                  "Virtual Thermostat z1");
    if (has_odin_plan)
      odin_plan_.enabled = odin_plan_enabled;
  }
  if (has_absorb_arm)
    odin_plan_.absorb_arm_enabled = absorb_arm_enabled;
  if (has_arm_energy_cost_modulation)
    odin_plan_.arm_energy_cost_modulation = arm_energy_cost_modulation;
  if (plan_source_mode != nullptr && plan_source_mode[0] != '\0') {
    const auto mode = plan_source::mode_from_name(plan_source_mode);
    std::strncpy(odin_plan_.plan_source_mode, plan_source::mode_name(mode),
                 sizeof(odin_plan_.plan_source_mode) - 1);
  }
  if (odin_host != nullptr) {
    std::strncpy(odin_plan_.odin_host, odin_host, sizeof(odin_plan_.odin_host) - 1);
    odin_plan_.odin_host[sizeof(odin_plan_.odin_host) - 1] = '\0';
  }
  if (odin_port != 0)
    odin_plan_.odin_port = odin_port;
  if (has_mqtt)
    odin_mqtt_.enabled = mqtt_enabled;
  if (mqtt_host != nullptr) {
    std::strncpy(odin_mqtt_.host, mqtt_host, sizeof(odin_mqtt_.host) - 1);
    odin_mqtt_.host[sizeof(odin_mqtt_.host) - 1] = '\0';
  }
  if (mqtt_port != 0)
    odin_mqtt_.port = mqtt_port;
  if (mqtt_username != nullptr) {
    std::strncpy(odin_mqtt_.username, mqtt_username, sizeof(odin_mqtt_.username) - 1);
    odin_mqtt_.username[sizeof(odin_mqtt_.username) - 1] = '\0';
  }
  if (mqtt_password != nullptr) {
    if (mqtt_password[0] == '\0') {
      odin_mqtt_password_[0] = '\0';
      odin_mqtt_.password_set = false;
    } else {
      std::strncpy(odin_mqtt_password_, mqtt_password, sizeof(odin_mqtt_password_) - 1);
      odin_mqtt_password_[sizeof(odin_mqtt_password_) - 1] = '\0';
      odin_mqtt_.password_set = true;
    }
  }
  if (mqtt_topic_prefix != nullptr) {
    std::strncpy(odin_mqtt_.topic_prefix, mqtt_topic_prefix, sizeof(odin_mqtt_.topic_prefix) - 1);
    odin_mqtt_.topic_prefix[sizeof(odin_mqtt_.topic_prefix) - 1] = '\0';
  }
  if (mqtt_hp_id != nullptr) {
    std::strncpy(odin_mqtt_.hp_id, mqtt_hp_id, sizeof(odin_mqtt_.hp_id) - 1);
    odin_mqtt_.hp_id[sizeof(odin_mqtt_.hp_id) - 1] = '\0';
  }
  sync_odin_plan_from_heat_source_();
  if (v6_control_mode != nullptr && v6_control_mode[0] != '\0') {
    if (std::strcmp(v6_control_mode, "local") == 0 ||
        std::strcmp(v6_control_mode, "heat_pump") == 0 ||
        std::strcmp(v6_control_mode, "normal") == 0) {
      std::strncpy(v6_control_mode_, v6_control_mode, sizeof(v6_control_mode_) - 1);
      v6_control_mode_[sizeof(v6_control_mode_) - 1] = '\0';
    }
  }
  if (house_weighting != nullptr && house_weighting[0] != '\0')
    model_.set_house_weighting(::lune_touch::house_weighting_from_name(house_weighting));
  // Keep the legacy fields coherent for existing API clients.
  asgard_enabled_ = heat_source_.enabled;
  std::strncpy(asgard_mode_, heat_source_.enabled ? "advisory" : "disabled", sizeof(asgard_mode_) - 1);
  asgard_mode_[sizeof(asgard_mode_) - 1] = '\0';
  save_settings_();
  // Apply MQTT changes now: the Odin task rebuilds the client when they differ.
  kick_task_(odin_task_handle_);
  char escaped_host[128];
  char escaped_variable[96];
  char escaped_write[256];
  char escaped_read[256];
  char escaped_type[48];
  json_escape_(heat_source_.host, escaped_host, sizeof(escaped_host));
  json_escape_(heat_source_.weighted_temperature_variable, escaped_variable, sizeof(escaped_variable));
  json_escape_(heat_source_.write_url_template, escaped_write, sizeof(escaped_write));
  json_escape_(heat_source_.read_url_template, escaped_read, sizeof(escaped_read));
  json_escape_(heat_source_.type, escaped_type, sizeof(escaped_type));
  snprintf(response, capacity,
           "{\"result\":\"saved\",\"heat_source\":{\"type\":\"%s\",\"enabled\":%s,\"host\":\"%s\",\"port\":%u,"
           "\"weighted_temperature_variable\":\"%s\",\"push_interval_s\":%u,"
           "\"write_url_template\":\"%s\",\"read_url_template\":\"%s\",\"odin_plan_enabled\":%s}}",
           escaped_type, heat_source_.enabled ? "true" : "false", escaped_host,
           static_cast<unsigned>(heat_source_.port), escaped_variable,
           static_cast<unsigned>(heat_source_.push_interval_s), escaped_write, escaped_read,
           odin_plan_.enabled ? "true" : "false");
  give_state_lock_();
  log_event_("info", "heat_source", "heat source settings updated");
  return true;
}

bool LuneTouchCoordinator::request_heat_source_push(char *response, size_t capacity) {
  if (!take_state_lock_(100)) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"coordinator_busy\"}");
    return false;
  }
  if (!heat_source_.enabled || heat_source_.host[0] == '\0') {
    give_state_lock_();
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"heat_source_not_ready\"}");
    return false;
  }
  heat_source_push_requested_ = true;
  give_state_lock_();
  kick_task_(heat_source_task_handle_);
  snprintf(response, capacity, "{\"result\":\"queued\"}");
  return true;
}

bool LuneTouchCoordinator::queue_heat_source_probe_(HeatSourceProbeKind kind, char *response,
                                                    size_t capacity) {
  if (response == nullptr || capacity == 0)
    return false;
  if (heat_source_task_handle_ == nullptr || heat_source_probe_done_ == nullptr) {
    snprintf(response, capacity,
             "{\"result\":\"rejected\",\"error\":\"heat_source_task_unavailable\"}");
    return false;
  }
  if (!take_state_lock_(100)) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"coordinator_busy\"}");
    return false;
  }
  if (heat_source_probe_kind_ != HeatSourceProbeKind::NONE) {
    give_state_lock_();
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"probe_busy\"}");
    return false;
  }
  heat_source_probe_kind_ = kind;
  heat_source_probe_response_[0] = '\0';
  heat_source_probe_ok_ = false;
  give_state_lock_();
  while (xSemaphoreTake(heat_source_probe_done_, 0) == pdTRUE) {
  }
  kick_task_(heat_source_task_handle_);
  if (xSemaphoreTake(heat_source_probe_done_, pdMS_TO_TICKS(12000)) != pdTRUE) {
    if (take_state_lock_(50)) {
      if (heat_source_probe_kind_ == kind)
        heat_source_probe_kind_ = HeatSourceProbeKind::NONE;
      give_state_lock_();
    }
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"probe_timeout\"}");
    return false;
  }
  std::strncpy(response, heat_source_probe_response_, capacity - 1);
  response[capacity - 1] = '\0';
  return heat_source_probe_ok_;
}

bool LuneTouchCoordinator::request_heat_source_test_read(char *response, size_t capacity) {
  return queue_heat_source_probe_(HeatSourceProbeKind::TEST_READ, response, capacity);
}

bool LuneTouchCoordinator::run_heat_source_test_read_(char *response, size_t capacity) {
  HeatSourceState source;
  if (!take_state_lock_(100)) {
    snprintf(response, capacity,
             "{\"result\":\"rejected\",\"action\":\"read\",\"error\":\"coordinator_busy\"}");
    return false;
  }
  source = heat_source_;
  give_state_lock_();
  const asgard_adapter::Config adapter_config = make_asgard_config_(source);
  char url[448]{};
  char escaped_url[512]{};
  char escaped_host[128]{};
  char escaped_entity[96]{};
  char escaped_template[256]{};
  json_escape_(source.host, escaped_host, sizeof(escaped_host));
  json_escape_(source.weighted_temperature_variable, escaped_entity, sizeof(escaped_entity));
  json_escape_(source.read_url_template, escaped_template, sizeof(escaped_template));
  const bool built = source.host[0] != '\0' &&
                     asgard_adapter::build_physical_read_url(adapter_config, url, sizeof(url));
  json_escape_(url, escaped_url, sizeof(escaped_url));
  if (source.host[0] == '\0') {
    snprintf(response, capacity,
             "{\"result\":\"rejected\",\"action\":\"read\",\"url\":\"\",\"host\":\"%s\",\"port\":%u,"
             "\"entity\":\"%s\",\"read_url_template\":\"%s\",\"error\":\"address_missing\"}",
             escaped_host, static_cast<unsigned>(source.port), escaped_entity, escaped_template);
    return false;
  }
  if (!esphome::network::is_connected()) {
    snprintf(response, capacity,
             "{\"result\":\"rejected\",\"action\":\"read\",\"url\":\"%s\",\"host\":\"%s\",\"port\":%u,"
             "\"entity\":\"%s\",\"read_url_template\":\"%s\",\"error\":\"network_down\"}",
             escaped_url, escaped_host, static_cast<unsigned>(source.port), escaped_entity,
             escaped_template);
    return false;
  }
  if (!built) {
    snprintf(response, capacity,
             "{\"result\":\"rejected\",\"action\":\"read\",\"url\":\"\",\"host\":\"%s\",\"port\":%u,"
             "\"entity\":\"%s\",\"read_url_template\":\"%s\",\"error\":\"invalid_read_endpoint\"}",
             escaped_host, static_cast<unsigned>(source.port), escaped_entity, escaped_template);
    return false;
  }
  char body[384]{};
  int status = 0;
  float value = NAN;
  char error[96]{};
  bool ok = false;
  if (fetch_json_(url, body, sizeof(body), &status)) {
    if (asgard_adapter::parse_number_response(body, &value)) {
      ok = true;
    } else {
      std::strncpy(error, "read value missing", sizeof(error) - 1);
    }
  } else {
    std::snprintf(error, sizeof(error), status > 0 ? "read http %d" : "read unreachable", status);
  }
  char escaped_error[192];
  char value_token[16];
  json_escape_(error, escaped_error, sizeof(escaped_error));
  json_float_token_(value_token, sizeof(value_token), value, 2);
  snprintf(response, capacity,
           "{\"result\":\"%s\",\"action\":\"read\",\"url\":\"%s\",\"host\":\"%s\",\"port\":%u,"
           "\"entity\":\"%s\",\"read_url_template\":\"%s\",\"http_status\":%d,\"value_c\":%s,"
           "\"error\":\"%s\"}",
           ok ? "ok" : "failed", escaped_url, escaped_host, static_cast<unsigned>(source.port),
           escaped_entity, escaped_template, status, value_token, escaped_error);
  return ok;
}

bool LuneTouchCoordinator::request_heat_source_test_push(char *response, size_t capacity) {
  return queue_heat_source_probe_(HeatSourceProbeKind::TEST_PUSH, response, capacity);
}

bool LuneTouchCoordinator::run_heat_source_test_push_(char *response, size_t capacity) {
  HeatSourceState before;
  ::lune_touch::StrategySnapshot strategy;
  if (!take_state_lock_(100)) {
    snprintf(response, capacity,
             "{\"result\":\"rejected\",\"action\":\"push\",\"error\":\"coordinator_busy\"}");
    return false;
  }
  before = heat_source_;
  strategy = model_.strategy_snapshot();
  give_state_lock_();
  float preview_value = NAN;
  if (strategy.has_physical_temperature && std::isfinite(strategy.physical_temperature_c))
    preview_value = strategy.physical_temperature_c;
  else if (strategy.has_temperature_preview && std::isfinite(strategy.temperature_preview_c))
    preview_value = strategy.temperature_preview_c;
  else if (std::isfinite(before.last_confirmed_value_c))
    preview_value = before.last_confirmed_value_c;
  const asgard_adapter::Config adapter_config = make_asgard_config_(before);
  char write_url[448]{};
  char read_url[448]{};
  if (std::isfinite(preview_value))
    asgard_adapter::build_physical_write_url(adapter_config, preview_value, write_url, sizeof(write_url));
  asgard_adapter::build_physical_read_url(adapter_config, read_url, sizeof(read_url));
  char escaped_write[512]{};
  char escaped_read[512]{};
  char escaped_host[128]{};
  char escaped_entity[96]{};
  char escaped_write_tmpl[256]{};
  char escaped_read_tmpl[256]{};
  char preview_token[16];
  json_escape_(write_url, escaped_write, sizeof(escaped_write));
  json_escape_(read_url, escaped_read, sizeof(escaped_read));
  json_escape_(before.host, escaped_host, sizeof(escaped_host));
  json_escape_(before.weighted_temperature_variable, escaped_entity, sizeof(escaped_entity));
  json_escape_(before.write_url_template, escaped_write_tmpl, sizeof(escaped_write_tmpl));
  json_escape_(before.read_url_template, escaped_read_tmpl, sizeof(escaped_read_tmpl));
  json_float_token_(preview_token, sizeof(preview_token), preview_value, 2);
  if (!before.enabled || before.host[0] == '\0') {
    snprintf(response, capacity,
             "{\"result\":\"rejected\",\"action\":\"push\",\"write_url\":\"%s\",\"read_url\":\"%s\","
             "\"host\":\"%s\",\"port\":%u,\"entity\":\"%s\",\"write_url_template\":\"%s\","
             "\"read_url_template\":\"%s\",\"preview_value_c\":%s,\"error\":\"heat_source_not_ready\"}",
             escaped_write, escaped_read, escaped_host, static_cast<unsigned>(before.port),
             escaped_entity, escaped_write_tmpl, escaped_read_tmpl, preview_token);
    return false;
  }
  const bool ok = push_weighted_temperature_();
  HeatSourceState after;
  if (take_state_lock_(50)) {
    after = heat_source_;
    give_state_lock_();
  } else {
    after = before;
  }
  if (std::isfinite(after.last_requested_value_c)) {
    asgard_adapter::build_physical_write_url(adapter_config, after.last_requested_value_c, write_url,
                                             sizeof(write_url));
    json_escape_(write_url, escaped_write, sizeof(escaped_write));
  }
  char escaped_error[192];
  char requested[16];
  char confirmed[16];
  json_escape_(after.last_error, escaped_error, sizeof(escaped_error));
  json_float_token_(requested, sizeof(requested), after.last_requested_value_c, 2);
  json_float_token_(confirmed, sizeof(confirmed), after.last_confirmed_value_c, 2);
  snprintf(response, capacity,
           "{\"result\":\"%s\",\"action\":\"push\",\"write_url\":\"%s\",\"read_url\":\"%s\","
           "\"host\":\"%s\",\"port\":%u,\"entity\":\"%s\",\"write_url_template\":\"%s\","
           "\"read_url_template\":\"%s\",\"preview_value_c\":%s,\"http_status\":%d,\"status\":\"%s\","
           "\"requested_value_c\":%s,\"confirmed_value_c\":%s,\"error\":\"%s\"}",
           ok ? "ok" : "failed", escaped_write, escaped_read, escaped_host,
           static_cast<unsigned>(before.port), escaped_entity, escaped_write_tmpl, escaped_read_tmpl,
           preview_token, after.last_http_status, after.last_status, requested, confirmed,
           escaped_error);
  return ok;
}

bool LuneTouchCoordinator::set_circulation_settings(
    bool has_enabled, bool enabled, const char *host, uint16_t port,
    const char *flow_entity, const char *head_entity, const char *power_entity,
    char *response, size_t capacity) {
  if (!has_enabled && (host == nullptr || host[0] == '\0') && port == 0 &&
      (flow_entity == nullptr || flow_entity[0] == '\0') &&
      (head_entity == nullptr || head_entity[0] == '\0') &&
      (power_entity == nullptr || power_entity[0] == '\0')) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"settings_required\"}");
    return false;
  }
  if (host != nullptr && host[0] != '\0' && std::strpbrk(host, " /?#") != nullptr) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"invalid_circulation_settings\"}");
    return false;
  }
  if ((flow_entity != nullptr && flow_entity[0] != '\0' && !circulation_pump::valid_entity(flow_entity)) ||
      (head_entity != nullptr && head_entity[0] != '\0' && !circulation_pump::valid_entity(head_entity)) ||
      (power_entity != nullptr && power_entity[0] != '\0' && !circulation_pump::valid_entity(power_entity))) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"invalid_circulation_entity\"}");
    return false;
  }
  if (!take_state_lock_(100)) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"coordinator_busy\"}");
    return false;
  }
  if (has_enabled)
    circulation_.enabled = enabled;
  if (host != nullptr && host[0] != '\0') {
    std::strncpy(circulation_.host, host, sizeof(circulation_.host) - 1);
    circulation_.host[sizeof(circulation_.host) - 1] = '\0';
  }
  if (port != 0)
    circulation_.port = port;
  if (flow_entity != nullptr && flow_entity[0] != '\0') {
    std::strncpy(circulation_.flow_entity, flow_entity, sizeof(circulation_.flow_entity) - 1);
    circulation_.flow_entity[sizeof(circulation_.flow_entity) - 1] = '\0';
  }
  if (head_entity != nullptr && head_entity[0] != '\0') {
    std::strncpy(circulation_.head_entity, head_entity, sizeof(circulation_.head_entity) - 1);
    circulation_.head_entity[sizeof(circulation_.head_entity) - 1] = '\0';
  }
  if (power_entity != nullptr && power_entity[0] != '\0') {
    std::strncpy(circulation_.power_entity, power_entity, sizeof(circulation_.power_entity) - 1);
    circulation_.power_entity[sizeof(circulation_.power_entity) - 1] = '\0';
  }
  circulation_.refresh_interval_s = 300;
  if (circulation_.enabled && circulation_.host[0] != '\0')
    circulation_poll_requested_ = true;
  save_settings_();
  char escaped_host[128];
  char escaped_flow[96];
  char escaped_head[96];
  char escaped_power[96];
  json_escape_(circulation_.host, escaped_host, sizeof(escaped_host));
  json_escape_(circulation_.flow_entity, escaped_flow, sizeof(escaped_flow));
  json_escape_(circulation_.head_entity, escaped_head, sizeof(escaped_head));
  json_escape_(circulation_.power_entity, escaped_power, sizeof(escaped_power));
  snprintf(response, capacity,
           "{\"result\":\"saved\",\"circulation\":{\"enabled\":%s,\"host\":\"%s\",\"port\":%u,"
           "\"refresh_interval_s\":%u,\"flow_entity\":\"%s\",\"head_entity\":\"%s\","
           "\"power_entity\":\"%s\"}}",
           circulation_.enabled ? "true" : "false", escaped_host,
           static_cast<unsigned>(circulation_.port),
           static_cast<unsigned>(circulation_.refresh_interval_s), escaped_flow, escaped_head,
           escaped_power);
  give_state_lock_();
  if (poll_task_handle_ != nullptr)
    xTaskNotifyGive(poll_task_handle_);
  schedule_circulation_fetch_();
  log_event_("info", "circulation", "circulation settings updated");
  return true;
}

bool LuneTouchCoordinator::request_circulation_refresh(char *response, size_t capacity) {
  if (!take_state_lock_(100)) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"coordinator_busy\"}");
    return false;
  }
  if (!circulation_.enabled || circulation_.host[0] == '\0') {
    give_state_lock_();
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"circulation_not_ready\"}");
    return false;
  }
  circulation_poll_requested_ = true;
  give_state_lock_();
  schedule_circulation_fetch_();
  snprintf(response, capacity, "{\"result\":\"queued\"}");
  return true;
}

bool LuneTouchCoordinator::request_forecast_fetch(char *response, size_t capacity) {
  float latitude = 0.0f;
  float longitude = 0.0f;
  if (take_state_lock_(100)) {
    latitude = forecast_latitude_;
    longitude = forecast_longitude_;
  } else {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"coordinator_busy\"}");
    return false;
  }

  if (!std::isfinite(latitude) || !std::isfinite(longitude) ||
      (std::fabs(latitude) < 0.0001f && std::fabs(longitude) < 0.0001f)) {
    std::strncpy(forecast_status_, "needs_location", sizeof(forecast_status_) - 1);
    forecast_status_[sizeof(forecast_status_) - 1] = '\0';
    std::strncpy(forecast_last_error_, "location_required", sizeof(forecast_last_error_) - 1);
    forecast_last_error_[sizeof(forecast_last_error_) - 1] = '\0';
    forecast_fetch_requested_ = false;
    give_state_lock_();
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"location_required\"}");
    return false;
  }
  if (!esphome::network::is_connected()) {
    std::strncpy(forecast_status_, "offline", sizeof(forecast_status_) - 1);
    forecast_status_[sizeof(forecast_status_) - 1] = '\0';
    std::strncpy(forecast_last_error_, "network_offline", sizeof(forecast_last_error_) - 1);
    forecast_last_error_[sizeof(forecast_last_error_) - 1] = '\0';
    forecast_fetch_requested_ = false;
    give_state_lock_();
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"network_offline\"}");
    return false;
  }

  forecast_fetch_requested_ = true;
  forecast_task_fetch_pending_ = true;
  std::strncpy(forecast_status_, "queued", sizeof(forecast_status_) - 1);
  forecast_status_[sizeof(forecast_status_) - 1] = '\0';
  forecast_last_error_[0] = '\0';
  give_state_lock_();
  kick_task_(forecast_task_handle_);
  snprintf(response, capacity, "{\"result\":\"queued\",\"status\":\"queued\"}");
  log_event_("info", "forecast", "fetch queued");
  return true;
}

bool LuneTouchCoordinator::estimate_forecast_location(char *response, size_t capacity) {
  if (response == nullptr || capacity == 0)
    return false;
  if (!esphome::network::is_connected()) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"network_offline\"}");
    return false;
  }

  // Browser dashboards are served over plain HTTP, so they cannot call HTTPS
  // geo APIs directly (mixed content). Touch proxies a coarse public-IP lookup.
  constexpr size_t BODY_CAP = 1536;
  char *body = alloc_http_body_(BODY_CAP);
  if (body == nullptr) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"no_body_heap\"}");
    return false;
  }

  struct FetchCtx {
    char *body;
    size_t capacity;
    size_t length;
  };
  FetchCtx ctx{body, BODY_CAP, 0};

  esp_http_client_config_t cfg{};
  cfg.url = "https://get.geojs.io/v1/ip/geo.json";
  cfg.method = HTTP_METHOD_GET;
  cfg.timeout_ms = 5000;
  cfg.disable_auto_redirect = true;
  cfg.crt_bundle_attach = esp_crt_bundle_attach;
  cfg.user_data = &ctx;
  cfg.event_handler = [](esp_http_client_event_t *evt) -> esp_err_t {
    if (evt == nullptr || evt->user_data == nullptr)
      return ESP_OK;
    auto *fetch = static_cast<FetchCtx *>(evt->user_data);
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data != nullptr && evt->data_len > 0) {
      const size_t room = fetch->capacity > fetch->length + 1 ? fetch->capacity - fetch->length - 1 : 0;
      const size_t copy = std::min(room, static_cast<size_t>(evt->data_len));
      if (copy > 0) {
        std::memcpy(fetch->body + fetch->length, evt->data, copy);
        fetch->length += copy;
        fetch->body[fetch->length] = '\0';
      }
    }
    return ESP_OK;
  };

  esp_http_client_handle_t client = esp_http_client_init(&cfg);
  if (client == nullptr) {
    heap_caps_free(body);
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"http_init_failed\"}");
    return false;
  }

  const esp_err_t err = esp_http_client_perform(client);
  const int status = err == ESP_OK ? esp_http_client_get_status_code(client) : 0;
  esp_http_client_cleanup(client);
  if (err != ESP_OK || status != 200 || ctx.length == 0) {
    heap_caps_free(body);
    snprintf(response, capacity,
             "{\"result\":\"rejected\",\"error\":\"lookup_failed\",\"http_status\":%d}", status);
    return false;
  }

  JsonDocument doc;
  if (deserializeJson(doc, body, ctx.length)) {
    heap_caps_free(body);
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"invalid_lookup_json\"}");
    return false;
  }
  heap_caps_free(body);

  float latitude = doc["latitude"].as<float>();
  float longitude = doc["longitude"].as<float>();
  if ((!std::isfinite(latitude) || !std::isfinite(longitude)) &&
      (doc["latitude"].is<const char *>() || doc["longitude"].is<const char *>())) {
    if (doc["latitude"].is<const char *>())
      latitude = std::strtof(doc["latitude"].as<const char *>(), nullptr);
    if (doc["longitude"].is<const char *>())
      longitude = std::strtof(doc["longitude"].as<const char *>(), nullptr);
  }

  if (!std::isfinite(latitude) || !std::isfinite(longitude) ||
      latitude < -90.0f || latitude > 90.0f || longitude < -180.0f || longitude > 180.0f ||
      (std::fabs(latitude) < 0.0001f && std::fabs(longitude) < 0.0001f)) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"coordinates_unavailable\"}");
    return false;
  }

  char city[48]{};
  char country[48]{};
  const char *city_raw = doc["city"].as<const char *>();
  const char *country_raw = doc["country"].as<const char *>();
  if (city_raw != nullptr)
    json_escape_(city_raw, city, sizeof(city));
  if (country_raw != nullptr)
    json_escape_(country_raw, country, sizeof(country));

  snprintf(response, capacity,
           "{\"result\":\"ok\",\"source\":\"network\",\"latitude\":%.6f,\"longitude\":%.6f,"
           "\"city\":\"%s\",\"country\":\"%s\"}",
           latitude, longitude, city, country);
  log_event_("info", "forecast", "network location estimated");
  return true;
}

bool LuneTouchCoordinator::perform_forecast_fetch_(char *response, size_t capacity) {
  float latitude = 0.0f;
  float longitude = 0.0f;
  if (take_state_lock_(100)) {
    latitude = forecast_latitude_;
    longitude = forecast_longitude_;
    std::strncpy(forecast_status_, "fetching", sizeof(forecast_status_) - 1);
    forecast_status_[sizeof(forecast_status_) - 1] = '\0';
    give_state_lock_();
  } else {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"coordinator_busy\"}");
    return false;
  }

  if (!std::isfinite(latitude) || !std::isfinite(longitude) ||
      (std::fabs(latitude) < 0.0001f && std::fabs(longitude) < 0.0001f)) {
    if (take_state_lock_(100)) {
      std::strncpy(forecast_status_, "needs_location", sizeof(forecast_status_) - 1);
      forecast_status_[sizeof(forecast_status_) - 1] = '\0';
      std::strncpy(forecast_last_error_, "location_required", sizeof(forecast_last_error_) - 1);
      forecast_last_error_[sizeof(forecast_last_error_) - 1] = '\0';
      forecast_fetch_requested_ = false;
      give_state_lock_();
    }
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"location_required\"}");
    return false;
  }
  if (!esphome::network::is_connected()) {
    if (take_state_lock_(100)) {
      std::strncpy(forecast_status_, "offline", sizeof(forecast_status_) - 1);
      forecast_status_[sizeof(forecast_status_) - 1] = '\0';
      std::strncpy(forecast_last_error_, "network_offline", sizeof(forecast_last_error_) - 1);
      forecast_last_error_[sizeof(forecast_last_error_) - 1] = '\0';
      forecast_fetch_requested_ = false;
      give_state_lock_();
    }
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"network_offline\"}");
    return false;
  }

  const int64_t fetch_epoch_s = current_epoch_s_();
  if (fetch_epoch_s == 0) {
    if (take_state_lock_(100)) {
      std::strncpy(forecast_status_, "time_unaligned", sizeof(forecast_status_) - 1);
      forecast_status_[sizeof(forecast_status_) - 1] = '\0';
      std::strncpy(forecast_last_error_, "time_unavailable", sizeof(forecast_last_error_) - 1);
      forecast_last_error_[sizeof(forecast_last_error_) - 1] = '\0';
      give_state_lock_();
    }
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"time_unavailable\"}");
    return false;
  }

  char error[96]{};
  char provider_timezone[48]{};
  uint8_t hours = 0;
  ForecastHourState fetched_hours[72]{};
  float min_temp = 0.0f;
  float max_wind = 0.0f;
  float wind_dir = 0.0f;
  float max_solar = 0.0f;
  const bool ok = fetch_open_meteo_(latitude, longitude, error, sizeof(error),
                                    &hours, &min_temp, &max_wind, &wind_dir, &max_solar,
                                    provider_timezone, sizeof(provider_timezone), fetched_hours, 72);

  if (take_state_lock_(100)) {
    forecast_last_fetch_ms_ = esphome::millis();
    if (ok) {
      std::strncpy(forecast_status_, "ok", sizeof(forecast_status_) - 1);
      forecast_status_[sizeof(forecast_status_) - 1] = '\0';
      forecast_last_error_[0] = '\0';
      forecast_hours_count_ = hours;
      for (uint8_t i = 0; i < hours && i < 72; i++)
        forecast_hours_[i] = fetched_hours[i];
      forecast_min_temp_c_ = min_temp;
      forecast_max_wind_ms_ = max_wind;
      forecast_peak_wind_dir_deg_ = wind_dir;
      forecast_max_solar_wm2_ = max_solar;
      forecast_fetch_epoch_s_ = fetch_epoch_s;
      std::strncpy(forecast_provider_timezone_, provider_timezone,
                   sizeof(forecast_provider_timezone_) - 1);
      forecast_provider_timezone_[sizeof(forecast_provider_timezone_) - 1] = '\0';
      forecast_cache_restored_ = false;
      recompute_forecast_decisions_();
    } else {
      std::strncpy(forecast_status_, "error", sizeof(forecast_status_) - 1);
      forecast_status_[sizeof(forecast_status_) - 1] = '\0';
      std::strncpy(forecast_last_error_, error[0] != '\0' ? error : "fetch_failed",
                   sizeof(forecast_last_error_) - 1);
      forecast_last_error_[sizeof(forecast_last_error_) - 1] = '\0';
      forecast_decision_count_ = 0;
    }
    give_state_lock_();
  }

  ForecastDispatchSummary dispatch{};
  if (ok) {
    dispatch = dispatch_forecast_commands_();
    dispatch_charge_arms_();
  }
  if (ok)
    save_forecast_cache_();

  if (!ok) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"status\":\"error\",\"error\":\"%s\"}",
             error[0] != '\0' ? error : "fetch_failed");
    log_event_("warn", "forecast", error[0] != '\0' ? error : "fetch_failed");
    return false;
  }
  snprintf(response, capacity,
           "{\"result\":\"fetched\",\"status\":\"ok\",\"hours\":%u,\"min_temp_c\":%.1f,"
           "\"max_wind_ms\":%.1f,\"peak_wind_dir_deg\":%.0f,"
           "\"commands\":{\"active\":%u,\"sent\":%u,\"skipped\":%u,\"failed\":%u,"
           "\"blocked_stale\":%u,\"blocked_unreachable\":%u,\"blocked_untrusted\":%u}}",
           static_cast<unsigned>(hours), min_temp, max_wind, wind_dir,
           static_cast<unsigned>(dispatch.active), static_cast<unsigned>(dispatch.sent),
           static_cast<unsigned>(dispatch.skipped), static_cast<unsigned>(dispatch.failed),
           static_cast<unsigned>(dispatch.blocked_stale),
           static_cast<unsigned>(dispatch.blocked_unreachable),
           static_cast<unsigned>(dispatch.blocked_untrusted));
  log_event_("info", "forecast", "fetch completed");
  return true;
}

std::string LuneTouchCoordinator::house_summary_text() const {
  if (!take_state_lock_(50))
    return "coordinator busy";
  const uint32_t now = esphome::millis();
  size_t stale_nodes = 0;
  size_t trusted_nodes = 0;
  size_t ready_trusted_nodes = 0;
  for (size_t i = 0; i < model_.node_count(); i++) {
    const auto *node = model_.node(i);
    if (node == nullptr)
      continue;
    const bool stale = model_.is_node_stale(i, now);
    if (stale)
      stale_nodes++;
    if (node->trust == ::lune_touch::NodeTrust::TRUSTED) {
      trusted_nodes++;
      if (node->reachable && !stale)
        ready_trusted_nodes++;
    }
  }
  const char *authority_label = "No publisher";
  if (std::strcmp(authority_state_, "touch_normal") == 0) authority_label = "Touch normal";
  else if (std::strcmp(authority_state_, "touch_degraded") == 0) authority_label = "Touch degraded";
  else if (std::strcmp(authority_state_, "v6_fallback_pending") == 0) authority_label = "V6-A fallback pending";
  else if (std::strcmp(authority_state_, "v6_fallback_active") == 0) authority_label = "V6-A fallback active";
  else if (std::strcmp(authority_state_, "touch_recovery_pending") == 0) authority_label = "Recovery pending";
  else if (std::strcmp(authority_state_, "conflict") == 0) authority_label = "Conflict";
  char buffer[160];
  snprintf(buffer, sizeof(buffer), "%u zones · %u heating · %u/%u manifolds ready · %s%s",
           static_cast<unsigned>(model_.active_zone_count()),
           static_cast<unsigned>(model_.calling_zone_count()),
           static_cast<unsigned>(ready_trusted_nodes),
           static_cast<unsigned>(trusted_nodes),
           authority_label, stale_nodes > 0 ? " · stale connection" : "");
  give_state_lock_();
  return buffer;
}

std::string LuneTouchCoordinator::zone_line_text(uint8_t row) const {
  if (!take_state_lock_(50))
    return "zone data busy";
  const ::lune_touch::ZoneBinding *zone = nullptr;
  size_t zone_index = 0;
  size_t active_index = 0;
  for (size_t i = 0; i < model_.zone_count(); i++) {
    const auto *candidate = model_.zone(i);
    if (candidate == nullptr || !candidate->enabled)
      continue;
    if (active_index == row) {
      zone = candidate;
      zone_index = i;
      break;
    }
    active_index++;
  }
  if (zone == nullptr || !zone->enabled) {
    give_state_lock_();
    char empty[32];
    snprintf(empty, sizeof(empty), "Zone %u", static_cast<unsigned>(row + 1));
    return empty;
  }

  const uint32_t now = esphome::millis();
  (void) now;
  const auto *live = model_.zone_live(zone_index);
  (void) live;
  const char *name = zone->room_name[0] != '\0' ? zone->room_name : zone->room_id;

  char buffer[128];
  snprintf(buffer, sizeof(buffer), "%.40s", name);
  give_state_lock_();
  return buffer;
}

std::string LuneTouchCoordinator::zone_line_meta_text(uint8_t row) const {
  if (!take_state_lock_(50))
    return "--.- / --.- C";
  const ::lune_touch::ZoneBinding *zone = nullptr;
  size_t zone_index = 0;
  size_t active_index = 0;
  for (size_t i = 0; i < model_.zone_count(); i++) {
    const auto *candidate = model_.zone(i);
    if (candidate == nullptr || !candidate->enabled)
      continue;
    if (active_index == row) {
      zone = candidate;
      zone_index = i;
      break;
    }
    active_index++;
  }
  if (zone == nullptr || !zone->enabled) {
    give_state_lock_();
    return "Waiting for V6";
  }

  const uint32_t now = esphome::millis();
  const auto *live = model_.zone_live(zone_index);
  const auto offset = ledger_.resolve_command_offset(zone->node_index, zone->zone_index, now);
  uint8_t day_index = 0;
  uint16_t minute_of_day = 0;
  const bool time_valid = current_schedule_time_(time_, &day_index, &minute_of_day);
  const auto effective = ::lune_touch::HouseModel::effective_comfort(*zone, time_valid,
                                                                     day_index, minute_of_day);
  const float learned_offset =
      ::lune_touch::HouseModel::learned_comfort_offset_c(*zone, live, effective.setpoint_c);
  ::lune_touch::TargetResolverInput target_input{};
  target_input.fallback_base_target_c = zone->comfort_setpoint_c;
  target_input.touch_target_c = effective.setpoint_c;
  target_input.command = offset;
  target_input.learned_modifier_c = learned_offset;
  target_input.learned_confident = learned_offset > 0.0f;
  const auto target = ::lune_touch::HouseModel::resolve_target(target_input);
  char temp[16];
  if (live != nullptr && live->has_temperature)
    snprintf(temp, sizeof(temp), "%.1f", live->temperature_c);
  else
    snprintf(temp, sizeof(temp), "--.-");
  char buffer[48];
  snprintf(buffer, sizeof(buffer), "%s / %.1f C", temp, target.dispatch_target_c);
  give_state_lock_();
  return buffer;
}

std::string LuneTouchCoordinator::forecast_summary_text() const {
  if (!take_state_lock_(50))
    return "forecast busy";
  char buffer[112];
  const int64_t now_epoch_s = current_epoch_s_();
  const unsigned long fetch_age_min =
      forecast_fetch_epoch_s_ == 0 || now_epoch_s < forecast_fetch_epoch_s_
          ? 0UL
          : static_cast<unsigned long>((now_epoch_s - forecast_fetch_epoch_s_) / 60);
  snprintf(buffer, sizeof(buffer), "Forecast %s%s · %u hours · updated %lu min ago",
           forecast_status_, forecast_fetch_requested_ ? " pending" : "",
           static_cast<unsigned>(forecast_hours_count_),
           fetch_age_min);
  give_state_lock_();
  return buffer;
}

std::string LuneTouchCoordinator::forecast_decision_text(uint8_t row) const {
  if (!take_state_lock_(50))
    return "forecast decisions busy";
  char buffer[144];
  if (forecast_decision_count_ == 0) {
    snprintf(buffer, sizeof(buffer), "No active preload");
    give_state_lock_();
    return buffer;
  }

  size_t decision_index = row;
  if (decision_index >= forecast_decision_count_)
    decision_index = forecast_decision_count_ - 1;
  const auto &decision = forecast_decisions_[decision_index];
  const char *name = decision.room_name[0] != '\0' ? decision.room_name : decision.room_id;
  snprintf(buffer, sizeof(buffer), "%s · %s %+.1f C · peak in %d h",
           name, decision.active ? "preload" : "watching", decision.offset_c,
           static_cast<int>(decision.peak_in_h));
  give_state_lock_();
  return buffer;
}

std::string LuneTouchCoordinator::command_summary_text() const {
  if (!take_state_lock_(50))
    return "commands busy";
  const uint32_t now = esphome::millis();
  const auto *active = ledger_.latest_active(now, current_epoch_s_());
  if (active != nullptr) {
    const uint32_t remaining_s = active->expires_at_ms > now ? (active->expires_at_ms - now) / 1000UL : 0UL;
    const float accepted_display =
        active->result == ::lune_touch::CommandResult::ACCEPTED ? active->accepted_offset_c : active->requested_offset_c;
    char buffer[128];
    snprintf(buffer, sizeof(buffer), "%s V6 %u/Z%u %.1f->%.1f C %lum %s",
             active->source, static_cast<unsigned>(active->node_index + 1),
             static_cast<unsigned>(active->zone_index + 1), active->requested_offset_c,
             accepted_display, static_cast<unsigned long>((remaining_s + 59UL) / 60UL),
             active->clamp_applied ? "clamped" : ::lune_touch::command_result_name(active->result));
    give_state_lock_();
    return buffer;
  }
  const size_t accepted = ledger_.count_result(::lune_touch::CommandResult::ACCEPTED);
  const size_t failed = ledger_.count_result(::lune_touch::CommandResult::FAILED);
  const size_t rejected = ledger_.count_result(::lune_touch::CommandResult::REJECTED);
  const size_t blocked = ledger_.count_blocked();
  const size_t clamped = ledger_.count_clamped();
  char buffer[96];
  snprintf(buffer, sizeof(buffer), "%u accepted / %u blocked / %u clamped / %u failed / %u rejected",
           static_cast<unsigned>(accepted),
           static_cast<unsigned>(blocked),
           static_cast<unsigned>(clamped),
           static_cast<unsigned>(failed),
           static_cast<unsigned>(rejected));
  give_state_lock_();
  return buffer;
}

std::string LuneTouchCoordinator::heating_summary_text() const {
  if (!take_state_lock_(50))
    return "heating state busy";
  const auto operation_mode = odin_plan::to_operation_mode(odin_plan_.current_operation_mode_raw);
  const char *mode = odin_plan::operation_mode_name(operation_mode);
  char buffer[144];
  snprintf(buffer, sizeof(buffer), "Heat source %s / Plan %s / %s",
           heat_source_.enabled ? heat_source_.last_status : "disabled",
           odin_plan_.available ? mode : "plan unavailable",
           odin_plan_.available && operation_mode == odin_plan::OperationMode::DHW_ON ? "DHW active" : "DHW unknown");
  give_state_lock_();
  return buffer;
}

std::string LuneTouchCoordinator::alarm_summary_text() const {
  if (!take_state_lock_(50))
    return "alarms busy";
  size_t motor_faults = 0;
  for (size_t i = 0; i < model_.node_count(); i++) {
    if (node_telemetry_[i].has_motor_fault && node_telemetry_[i].motor_fault)
      motor_faults++;
  }
  char buffer[112];
  if (std::strcmp(authority_state_, "conflict") == 0)
    snprintf(buffer, sizeof(buffer), "Alarm: authority conflict");
  else if (motor_faults > 0)
    snprintf(buffer, sizeof(buffer), "Alarm: %u motor fault%s", static_cast<unsigned>(motor_faults), motor_faults == 1 ? "" : "s");
  else if (poll_fail_count_ > 0)
    snprintf(buffer, sizeof(buffer), "Warning: %lu poll failures", static_cast<unsigned long>(poll_fail_count_));
  else
    snprintf(buffer, sizeof(buffer), "No active alarms");
  give_state_lock_();
  return buffer;
}

std::string LuneTouchCoordinator::display_refresh_token() const {
  if (!take_state_lock_(50))
    return display_refresh_token_cache_;

  uint32_t hash = 2166136261UL;
  hash = display_hash_text_(hash, authority_state_);
  hash = display_hash_text_(hash, forecast_status_);
  hash = display_hash_float_(hash, forecast_min_temp_c_);
  hash = display_hash_float_(hash, forecast_max_wind_ms_);
  hash = display_hash_mix_(hash, static_cast<uint32_t>(forecast_decision_count_));
  if (forecast_decision_count_ > 0) {
    const auto &decision = forecast_decisions_[0];
    hash = display_hash_text_(hash, decision.room_name);
    hash = display_hash_float_(hash, decision.offset_c);
    hash = display_hash_mix_(hash, static_cast<uint32_t>(decision.peak_in_h));
  }
  hash = display_hash_mix_(hash, heat_source_.enabled ? 1U : 0U);
  hash = display_hash_float_(hash, weather_max_boost_c_);
  hash = display_hash_mix_(hash, display_idle_timeout_s_);
  hash = display_hash_mix_(hash, heat_source_.has_last_push ? 1U : 0U);
  hash = display_hash_mix_(hash, heat_source_.failure_streak);
  hash = display_hash_text_(hash, heat_source_.last_status);
  hash = display_hash_float_(hash, heat_source_.last_requested_value_c);
  hash = display_hash_float_(hash, heat_source_.last_confirmed_value_c);
  hash = display_hash_float_(hash, heat_source_.hp_feed_c);
  hash = display_hash_float_(hash, heat_source_.hp_return_c);
  hash = display_hash_text_(hash, odin_control_.link_status);
  hash = display_hash_mix_(hash, circulation_.enabled && circulation_.host[0] != '\0' ? 1U : 0U);
  hash = display_hash_mix_(hash, circulation_.reachable ? 1U : 0U);
  hash = display_hash_float_(hash, circulation_.has_flow ? circulation_.flow_m3h : NAN);
  hash = display_hash_float_(hash, circulation_.has_head ? circulation_.head_m : NAN);
  hash = display_hash_float_(hash, circulation_.has_power ? circulation_.power_w : NAN);
  const bool circulation_any =
      circulation_.has_flow || circulation_.has_head || circulation_.has_power;
  const bool circulation_fresh =
      circulation_any && circulation_.last_fetch_ms != 0 &&
      esphome::millis() - circulation_.last_fetch_ms <= CIRCULATION_STALE_MS;
  hash = display_hash_mix_(hash, circulation_.last_fetch_ms == 0 ? 0U : 1U);
  hash = display_hash_mix_(hash, circulation_fresh ? 1U : 0U);

  // The local panel renders four manifolds. Changes on additional registered
  // nodes belong in the browser and must not wake the physical display.
  const size_t visible_node_count = std::min<size_t>(4, model_.node_count());
  const uint8_t display_page_count = std::max<uint8_t>(1, static_cast<uint8_t>((visible_node_count + 1) / 2));
  hash = display_hash_mix_(hash, std::min<uint8_t>(display_manifold_page_, display_page_count - 1));
  hash = display_hash_mix_(hash, display_active_page_);
  hash = display_hash_mix_(hash, display_selected_node_index_);
  hash = display_hash_mix_(hash, display_selected_zone_index_);
  hash = display_hash_mix_(hash, static_cast<uint32_t>(forecast_hours_count_));
  hash = display_hash_mix_(hash, forecast_last_fetch_ms_ == 0
                                     ? 0xFFFFFFFFu
                                     : static_cast<uint32_t>((esphome::millis() - forecast_last_fetch_ms_) / 60000UL));
  if (time_ != nullptr) {
    const auto now = time_->now();
    if (now.is_valid()) {
      hash = display_hash_mix_(hash, static_cast<uint32_t>(now.hour) * 60U + now.minute);
    }
  }
  hash = display_hash_mix_(hash, wifi_display_bars_());
  for (size_t node_index = 0; node_index < visible_node_count; node_index++) {
    const auto *node = model_.node(node_index);
    if (node == nullptr)
      continue;
    hash = display_hash_text_(hash, node->node_id);
    hash = display_hash_text_(hash, node->name);
    hash = display_hash_mix_(hash, node->reachable ? 1U : 0U);
    hash = display_hash_mix_(hash, model_.is_node_stale(node_index, esphome::millis()) ? 1U : 0U);
    const auto &telemetry = node_telemetry_[node_index];
    hash = display_hash_float_(hash, telemetry.has_flow ? telemetry.flow_c : NAN);
    hash = display_hash_float_(hash, telemetry.has_return ? telemetry.return_c : NAN);
    hash = display_hash_mix_(hash, telemetry.has_motor_fault && telemetry.motor_fault ? 1U : 0U);
  }
  for (size_t zone_index = 0; zone_index < model_.zone_count(); zone_index++) {
    const auto *zone = model_.zone(zone_index);
    const auto *live = model_.zone_live(zone_index);
    if (zone == nullptr || zone->node_index >= visible_node_count)
      continue;
    hash = display_hash_mix_(hash, zone->node_index);
    hash = display_hash_mix_(hash, zone->zone_index);
    hash = display_hash_mix_(hash, zone->enabled ? 1U : 0U);
    hash = display_hash_text_(hash, zone->room_name);
    hash = display_hash_float_(hash, zone->comfort_setpoint_c);
    if (live != nullptr) {
      hash = display_hash_float_(hash, live->has_temperature ? live->temperature_c : NAN);
      hash = display_hash_float_(hash, live->has_setpoint ? live->setpoint_c : NAN);
      // The panel shows valve demand in five 20% steps. Hash the rendered
      // level rather than every fractional motor report to avoid no-op redraw
      // passes while a valve moves within the same visible interval.
      const uint32_t valve_level = !live->has_valve || !std::isfinite(live->valve_pct)
                                       ? 0xFFFFFFFFUL
                                       : (live->valve_pct <= 0.0f
                                              ? 0U
                                              : static_cast<uint32_t>(std::min(5, (static_cast<int>(live->valve_pct) + 19) / 20)));
      hash = display_hash_mix_(hash, valve_level);
      hash = display_hash_text_(hash, live->status);
      hash = display_hash_mix_(hash, live->fresh ? 1U : 0U);
    }
  }

  give_state_lock_();

  char token[16];
  snprintf(token, sizeof(token), "%08lx", static_cast<unsigned long>(hash));
  std::strncpy(display_refresh_token_cache_, token, sizeof(display_refresh_token_cache_) - 1);
  display_refresh_token_cache_[sizeof(display_refresh_token_cache_) - 1] = '\0';
  return token;
}

bool LuneTouchCoordinator::request_display_wake(char *response, size_t capacity) {
  display_wake_requested_ = true;
  display_diag_.wake_requests++;
  snprintf(response, capacity, "{\"result\":\"accepted\"}");
  return true;
}

bool LuneTouchCoordinator::consume_display_wake_request() {
  if (!display_wake_requested_)
    return false;
  display_wake_requested_ = false;
  return true;
}

uint32_t LuneTouchCoordinator::display_idle_timeout_ms() const {
  return static_cast<uint32_t>(display_idle_timeout_s_) * 1000U;
}

std::string LuneTouchCoordinator::display_header_text() const {
  if (!take_state_lock_(50))
    return "Status unavailable";
  const auto strategy = model_.strategy_snapshot();
  size_t ready_nodes = 0;
  for (size_t i = 0; i < model_.node_count(); i++) {
    const auto *node = model_.node(i);
    if (node != nullptr && node->reachable && !model_.is_node_stale(i, esphome::millis()))
      ready_nodes++;
  }
  char buffer[112];
  if (strategy.has_physical_temperature) {
    snprintf(buffer, sizeof(buffer), "%s %.1f C  |  %u heating  |  %u/%u manifolds", DISPLAY_ICON_OK,
             strategy.physical_temperature_c,
             static_cast<unsigned>(model_.calling_zone_count()),
             static_cast<unsigned>(ready_nodes),
             static_cast<unsigned>(model_.node_count()));
  } else if (model_.node_count() > 0) {
    snprintf(buffer, sizeof(buffer), "%s --.- C  |  %u heating  |  %u/%u manifolds", DISPLAY_ICON_WARNING,
             static_cast<unsigned>(model_.calling_zone_count()),
             static_cast<unsigned>(ready_nodes),
             static_cast<unsigned>(model_.node_count()));
  } else {
    snprintf(buffer, sizeof(buffer), "Waiting for manifolds");
  }
  give_state_lock_();
  return buffer;
}

bool LuneTouchCoordinator::display_manifold_visible(uint8_t node_index) const {
  if (!take_state_lock_(50))
    return false;
  const bool visible = node_index < model_.node_count() && model_.node(node_index) != nullptr;
  give_state_lock_();
  return visible;
}

uint8_t LuneTouchCoordinator::display_zone_mask(uint8_t node_index) const {
  if (!take_state_lock_(50))
    return 0;
  const auto *node = model_.node(node_index);
  // Every V6 owns six physical outputs. The Touch panel mirrors all six and
  // never derives its topology from optional room bindings.
  const uint8_t mask = node == nullptr ? 0 : 0x3F;
  give_state_lock_();
  return mask;
}

uint8_t LuneTouchCoordinator::display_node_zone_count(uint8_t node_index) const {
  if (!take_state_lock_(50))
    return 0;
  uint8_t count = 0;
  for (size_t i = 0; i < model_.zone_count(); i++) {
    const auto *zone = model_.zone(i);
    if (zone != nullptr && zone->enabled && zone->node_index == node_index)
      count++;
  }
  if (count == 0 && model_.node(node_index) != nullptr)
    count = static_cast<uint8_t>(::lune_touch::ZONES_PER_NODE);
  give_state_lock_();
  return count;
}

bool LuneTouchCoordinator::display_zone_slot_used(uint8_t node_index, uint8_t zone_index) const {
  if (!take_state_lock_(50))
    return false;
  bool any_enabled = false;
  bool this_enabled = false;
  for (size_t i = 0; i < model_.zone_count(); i++) {
    const auto *zone = model_.zone(i);
    if (zone == nullptr || !zone->enabled || zone->node_index != node_index)
      continue;
    any_enabled = true;
    if (zone->zone_index == zone_index)
      this_enabled = true;
  }
  const bool used = this_enabled ||
      (!any_enabled && zone_index < ::lune_touch::ZONES_PER_NODE &&
       model_.node(node_index) != nullptr);
  give_state_lock_();
  return used;
}

uint8_t LuneTouchCoordinator::display_manifold_page_count() const {
  if (!take_state_lock_(50))
    return 1;
  const size_t visible_node_count = std::min<size_t>(4, model_.node_count());
  const uint8_t count = std::max<uint8_t>(1, static_cast<uint8_t>((visible_node_count + 1) / 2));
  give_state_lock_();
  return count;
}

uint8_t LuneTouchCoordinator::display_manifold_page() const {
  if (!take_state_lock_(50))
    return 0;
  const size_t visible_node_count = std::min<size_t>(4, model_.node_count());
  const uint8_t count = std::max<uint8_t>(1, static_cast<uint8_t>((visible_node_count + 1) / 2));
  const uint8_t page = std::min<uint8_t>(display_manifold_page_, count - 1);
  give_state_lock_();
  return page;
}

bool LuneTouchCoordinator::display_set_manifold_page(uint8_t page) {
  if (!take_state_lock_(50))
    return false;
  const size_t visible_node_count = std::min<size_t>(4, model_.node_count());
  const uint8_t count = std::max<uint8_t>(1, static_cast<uint8_t>((visible_node_count + 1) / 2));
  if (page >= count || page == display_manifold_page_) {
    give_state_lock_();
    return false;
  }
  display_manifold_page_ = page;
  give_state_lock_();
  return true;
}

std::string LuneTouchCoordinator::display_manifold_text(uint8_t node_index) const {
  if (!take_state_lock_(50))
    return "Manifold busy";
  const auto *node = model_.node(node_index);
  if (node == nullptr) {
    give_state_lock_();
    return "Not connected";
  }

  size_t heating = 0;
  for (size_t i = 0; i < model_.zone_count(); i++) {
    const auto *zone = model_.zone(i);
    const auto *live = model_.zone_live(i);
    if (zone == nullptr || !zone->enabled || zone->node_index != node_index)
      continue;
    if (live != nullptr && (std::strcmp(live->status, "heat") == 0 ||
                            std::strcmp(live->status, "call") == 0 ||
                            std::strcmp(live->status, "preheat") == 0))
      heating++;
  }

  const auto &telemetry = node_telemetry_[node_index];
  char flow[16] = "--.- C";
  char ret[16] = "--.- C";
  if (telemetry.has_flow)
    snprintf(flow, sizeof(flow), "%.1f C", telemetry.flow_c);
  if (telemetry.has_return)
    snprintf(ret, sizeof(ret), "%.1f C", telemetry.return_c);
  const bool stale = model_.is_node_stale(node_index, esphome::millis());
  const char *state = !node->reachable || stale ? "Offline" : heating > 0 ? "Heating" : "Ready";
  const char *state_icon = !node->reachable || stale ? DISPLAY_ICON_WARNING : DISPLAY_ICON_OK;
  const char *name = node->name[0] != '\0' ? node->name : node->node_id;
  char buffer[144];
  snprintf(buffer, sizeof(buffer), "%.17s\n%s %s\n%s %s  %s %s", name, state_icon, state,
           DISPLAY_ICON_UP, flow, DISPLAY_ICON_DOWN, ret);
  give_state_lock_();
  return buffer;
}

std::string LuneTouchCoordinator::display_zone_cell_text(uint8_t node_index, uint8_t physical_zone_index) const {
  if (!take_state_lock_(50))
    return "Zone busy";
  const auto *node = model_.node(node_index);
  if (node == nullptr) {
    give_state_lock_();
    return "";
  }

  const ::lune_touch::ZoneBinding *binding = nullptr;
  const ::lune_touch::ZoneLiveState *live = nullptr;
  for (size_t i = 0; i < model_.zone_count(); i++) {
    const auto *candidate = model_.zone(i);
    if (candidate != nullptr && candidate->enabled && candidate->node_index == node_index &&
        candidate->zone_index == physical_zone_index) {
      binding = candidate;
      live = model_.zone_live(i);
      break;
    }
  }

  char buffer[112];
  if (binding == nullptr) {
    snprintf(buffer, sizeof(buffer), "Waiting\n%s --.- C\n%s --.- C", DISPLAY_ICON_WARNING,
             DISPLAY_ICON_RIGHT);
    give_state_lock_();
    return buffer;
  }

  const char *name = display_binding_room_label_(model_, binding, live);
  char temperature[16] = "--.- C";
  if (live != nullptr && live->has_temperature)
    snprintf(temperature, sizeof(temperature), "%.1f C", live->temperature_c);

  char setpoint[16] = "--.- C";
  if (live != nullptr && live->has_setpoint)
    snprintf(setpoint, sizeof(setpoint), "%.1f C", live->setpoint_c);
  else if (std::isfinite(binding->comfort_setpoint_c))
    snprintf(setpoint, sizeof(setpoint), "%.1f C", binding->comfort_setpoint_c);
  snprintf(buffer, sizeof(buffer), "%.12s\n%s %s\n%s %s", name, DISPLAY_ICON_OK, temperature,
           DISPLAY_ICON_RIGHT, setpoint);
  give_state_lock_();
  return buffer;
}

std::string LuneTouchCoordinator::display_zone_name_text(uint8_t node_index,
                                                          uint8_t physical_zone_index) const {
  if (!take_state_lock_(50))
    return "Zone";
  const ::lune_touch::ZoneBinding *binding = nullptr;
  const ::lune_touch::ZoneLiveState *live = nullptr;
  for (size_t i = 0; i < model_.zone_count(); i++) {
    const auto *candidate = model_.zone(i);
    if (candidate != nullptr && candidate->enabled && candidate->node_index == node_index &&
        candidate->zone_index == physical_zone_index) {
      binding = candidate;
      live = model_.zone_live(i);
      break;
    }
  }
  char buffer[40];
  if (binding == nullptr) {
    std::snprintf(buffer, sizeof(buffer), "Zone %u",
                  static_cast<unsigned>(physical_zone_index + 1));
  } else {
    const char *name = display_binding_room_label_(model_, binding, live);
    const uint8_t members = live != nullptr ? group_member_count_(live->group_members_mask) : 0;
    // Multi-loop V6 sync group: keep the shared room name on every member tile.
    // Slot identity stays in the Z-label elsewhere (Z4 / Z5).
    (void) members;
    size_t out = 0;
    const size_t limit = sizeof(buffer) - 1;
    for (size_t i = 0; name[i] != '\0' && out + 1 < limit; ) {
      const unsigned char lead = static_cast<unsigned char>(name[i]);
      size_t width = 1;
      if ((lead & 0x80) == 0)
        width = 1;
      else if ((lead & 0xE0) == 0xC0)
        width = 2;
      else if ((lead & 0xF0) == 0xE0)
        width = 3;
      else if ((lead & 0xF8) == 0xF0)
        width = 4;
      if (out + width >= limit)
        break;
      for (size_t n = 0; n < width && name[i] != '\0'; n++)
        buffer[out++] = name[i++];
    }
    buffer[out] = '\0';
  }
  give_state_lock_();
  return buffer;
}

std::string LuneTouchCoordinator::display_zone_temperature_text(
    uint8_t node_index, uint8_t physical_zone_index) const {
  if (!take_state_lock_(50))
    return "--.- C";
  char buffer[20] = "--.- C";
  for (size_t i = 0; i < model_.zone_count(); i++) {
    const auto *zone = model_.zone(i);
    const auto *live = model_.zone_live(i);
    if (zone == nullptr || live == nullptr || !zone->enabled || zone->node_index != node_index ||
        zone->zone_index != physical_zone_index)
      continue;
    if (live->has_temperature)
      std::snprintf(buffer, sizeof(buffer), "%.1f C", live->temperature_c);
    break;
  }
  give_state_lock_();
  return buffer;
}

std::string LuneTouchCoordinator::display_zone_setpoint_text(
    uint8_t node_index, uint8_t physical_zone_index) const {
  if (!take_state_lock_(50))
    return "--.- C";
  char buffer[20] = "--.- C";
  for (size_t i = 0; i < model_.zone_count(); i++) {
    const auto *zone = model_.zone(i);
    const auto *live = model_.zone_live(i);
    if (zone == nullptr || !zone->enabled || zone->node_index != node_index ||
        zone->zone_index != physical_zone_index)
      continue;
    if (live != nullptr && live->has_setpoint)
      std::snprintf(buffer, sizeof(buffer), "%.1f C", live->setpoint_c);
    else if (std::isfinite(zone->comfort_setpoint_c))
      std::snprintf(buffer, sizeof(buffer), "%.1f C", zone->comfort_setpoint_c);
    break;
  }
  give_state_lock_();
  return buffer;
}

uint8_t LuneTouchCoordinator::display_zone_valve_pct(uint8_t node_index, uint8_t physical_zone_index) const {
  if (!take_state_lock_(50))
    return 0;
  uint8_t result = 0;
  for (size_t i = 0; i < model_.zone_count(); i++) {
    const auto *zone = model_.zone(i);
    const auto *live = model_.zone_live(i);
    if (zone == nullptr || live == nullptr || !zone->enabled || zone->node_index != node_index ||
        zone->zone_index != physical_zone_index || !live->has_valve)
      continue;
    result = static_cast<uint8_t>(std::lround(std::max(0.0f, std::min(100.0f, live->valve_pct))));
    break;
  }
  give_state_lock_();
  return result;
}

uint32_t LuneTouchCoordinator::display_zone_status_color(uint8_t node_index,
                                                         uint8_t physical_zone_index) const {
  if (!take_state_lock_(50))
    return lune::tokens::kFaint;
  const auto *node = model_.node(node_index);
  if (node == nullptr || !node->reachable || model_.is_node_stale(node_index, esphome::millis())) {
    give_state_lock_();
    return lune::tokens::kFaint;
  }
  if (node_index < ::lune_touch::MAX_NODES && node_telemetry_[node_index].has_motor_fault &&
      node_telemetry_[node_index].motor_fault) {
    give_state_lock_();
    return lune::tokens::kDanger;
  }
  const ::lune_touch::ZoneBinding *binding = nullptr;
  const ::lune_touch::ZoneLiveState *live = nullptr;
  for (size_t i = 0; i < model_.zone_count(); i++) {
    const auto *zone = model_.zone(i);
    if (zone != nullptr && zone->enabled && zone->node_index == node_index &&
        zone->zone_index == physical_zone_index) {
      binding = zone;
      live = model_.zone_live(i);
      break;
    }
  }
  uint32_t color = lune::tokens::kFaint;
  if (live == nullptr || !live->fresh)
    color = live == nullptr ? lune::tokens::kFaint : lune::tokens::kDanger;
  else if (std::strcmp(live->status, "fault") == 0)
    color = lune::tokens::kDanger;
  else if (std::strcmp(live->status, "heat") == 0 || std::strcmp(live->status, "call") == 0 ||
           std::strcmp(live->status, "preheat") == 0)
    color = lune::tokens::kAccent;
  else {
    float temperature = NAN;
    float setpoint = NAN;
    if (live->has_temperature)
      temperature = live->temperature_c;
    if (live->has_setpoint)
      setpoint = live->setpoint_c;
    else if (binding != nullptr)
      setpoint = binding->comfort_setpoint_c;
    if (std::isfinite(temperature) && std::isfinite(setpoint) &&
        std::fabs(temperature - setpoint) <= 0.5f)
      color = lune::tokens::kOk;
    else
      color = lune::tokens::kFaint;
  }
  give_state_lock_();
  return color;
}

std::string LuneTouchCoordinator::display_zone_status_icon(uint8_t node_index,
                                                           uint8_t physical_zone_index) const {
  const uint32_t color = display_zone_status_color(node_index, physical_zone_index);
  if (color == lune::tokens::kAccent)
    return DISPLAY_ICON_BARS;
  if (color == lune::tokens::kOk)
    return DISPLAY_ICON_OK;
  if (color == lune::tokens::kDanger)
    return DISPLAY_ICON_WARNING;
  return "";
}

std::string LuneTouchCoordinator::display_controller_flow_text(uint8_t node_index) const {
  if (!take_state_lock_(50))
    return "--.-";
  if (node_index >= model_.node_count() || model_.node(node_index) == nullptr) {
    give_state_lock_();
    return "--.-";
  }
  const auto &telemetry = node_telemetry_[node_index];
  char buffer[12] = "--.-";
  if (telemetry.has_flow)
    std::snprintf(buffer, sizeof(buffer), "%.1f", telemetry.flow_c);
  give_state_lock_();
  return buffer;
}

std::string LuneTouchCoordinator::display_controller_return_text(uint8_t node_index) const {
  if (!take_state_lock_(50))
    return "--.-";
  if (node_index >= model_.node_count() || model_.node(node_index) == nullptr) {
    give_state_lock_();
    return "--.-";
  }
  const auto &telemetry = node_telemetry_[node_index];
  char buffer[12] = "--.-";
  if (telemetry.has_return)
    std::snprintf(buffer, sizeof(buffer), "%.1f", telemetry.return_c);
  give_state_lock_();
  return buffer;
}

uint8_t LuneTouchCoordinator::display_asgard_status() const {
  if (!take_state_lock_(50))
    return 0;
  uint8_t st = 0;
  if (heat_source_.enabled && heat_source_.host[0] != '\0') {
    if (!heat_source_.has_last_push)
      st = 2;  // waiting (e.g. no Touch lease yet)
    else if (std::strcmp(heat_source_.last_status, "unreachable") == 0)
      st = 3;
    else if (heat_source_.push_alarm || std::strcmp(heat_source_.last_status, "confirmed") != 0)
      st = 2;
    else
      st = 1;
  }
  give_state_lock_();
  return st;
}

uint8_t LuneTouchCoordinator::display_odin_status() const {
  if (!take_state_lock_(50))
    return 0;
  uint8_t st = 0;
  if (odin_plan_.odin_host[0] != '\0') {
    const char *link = odin_control_.link_status;
    if (std::strcmp(link, "ok") == 0)
      st = 1;
    else if (std::strcmp(link, "odin_unreachable") == 0)
      st = 3;
    else if (std::strcmp(link, "unknown") == 0)
      st = 0;
    else
      st = 2;  // forwarder off / stale telemetry / no room temperature
  }
  give_state_lock_();
  return st;
}

std::string LuneTouchCoordinator::display_heat_source_name() const {
  if (!take_state_lock_(50))
    return "Asgard";
  const bool asgard = heat_source_type_is_asgard_(heat_source_.type);
  give_state_lock_();
  return asgard ? "Asgard" : "Varmekilde";
}

bool LuneTouchCoordinator::display_circulation_visible() const {
  if (!take_state_lock_(50))
    return false;
  const bool visible = circulation_.enabled && circulation_.host[0] != '\0';
  give_state_lock_();
  return visible;
}

std::string LuneTouchCoordinator::display_circulation_flow_text() const {
  if (!take_state_lock_(50))
    return "--.-";
  char buffer[12] = "--.-";
  if (circulation_.has_flow && std::isfinite(circulation_.flow_m3h))
    std::snprintf(buffer, sizeof(buffer), "%.1f", circulation_.flow_m3h * (1000.0f / 60.0f));
  give_state_lock_();
  return buffer;
}

std::string LuneTouchCoordinator::display_circulation_head_text() const {
  if (!take_state_lock_(50))
    return "--.-";
  char buffer[12] = "--.-";
  if (circulation_.has_head && std::isfinite(circulation_.head_m))
    std::snprintf(buffer, sizeof(buffer), "%.1f", circulation_.head_m);
  give_state_lock_();
  return buffer;
}

std::string LuneTouchCoordinator::display_circulation_power_text() const {
  if (!take_state_lock_(50))
    return "--";
  char buffer[12] = "--";
  if (circulation_.has_power && std::isfinite(circulation_.power_w))
    std::snprintf(buffer, sizeof(buffer), "%.0f", circulation_.power_w);
  give_state_lock_();
  return buffer;
}

std::string LuneTouchCoordinator::display_circulation_status_text() const {
  if (!take_state_lock_(50))
    return "Wait";
  const CirculationUiStatus status =
      circulation_ui_status_(circulation_, esphome::millis(), CIRCULATION_STALE_MS);
  give_state_lock_();
  switch (status) {
    case CirculationUiStatus::Ok:
      return "OK";
    case CirculationUiStatus::Stale:
      return "Stale";
    case CirculationUiStatus::Offline:
      return "Off";
    case CirculationUiStatus::Waiting:
    case CirculationUiStatus::Hidden:
    default:
      return "Wait";
  }
}

uint32_t LuneTouchCoordinator::display_circulation_status_color() const {
  if (!take_state_lock_(50))
    return lune::tokens::kMuted;
  const CirculationUiStatus status =
      circulation_ui_status_(circulation_, esphome::millis(), CIRCULATION_STALE_MS);
  give_state_lock_();
  switch (status) {
    case CirculationUiStatus::Ok:
      return lune::tokens::kOk;
    case CirculationUiStatus::Stale:
    case CirculationUiStatus::Offline:
      return lune::tokens::kWarn;
    case CirculationUiStatus::Waiting:
    case CirculationUiStatus::Hidden:
    default:
      return lune::tokens::kMuted;
  }
}

std::string LuneTouchCoordinator::display_heat_source_text() const {
  if (!take_state_lock_(50))
    return "Heat source busy";
  const HeatSourceState source = heat_source_;
  give_state_lock_();

  char signal[16] = "--.- C";
  char confirmed[16] = "--.- C";
  if (std::isfinite(source.last_requested_value_c))
    snprintf(signal, sizeof(signal), "%.1f C", source.last_requested_value_c);
  if (std::isfinite(source.last_confirmed_value_c))
    snprintf(confirmed, sizeof(confirmed), "%.1f C", source.last_confirmed_value_c);
  const bool healthy = source.enabled && source.has_last_push && source.failure_streak == 0;
  const char *state = !source.enabled ? "Off" : healthy ? "On" : "Needs attention";
  const char *state_icon = !source.enabled ? DISPLAY_ICON_CLOSE : healthy ? DISPLAY_ICON_OK : DISPLAY_ICON_WARNING;
  char buffer[128];
  snprintf(buffer, sizeof(buffer), "%s %s | sent %s | confirmed %s", state_icon, state, signal, confirmed);
  return buffer;
}

std::string LuneTouchCoordinator::display_forecast_text() const {
  if (!take_state_lock_(50))
    return "Forecast busy";
  char decision[80] = "No preload";
  if (forecast_decision_count_ > 0) {
    const auto &item = forecast_decisions_[0];
    const char *name = item.room_name[0] != '\0' ? item.room_name : item.room_id;
    snprintf(decision, sizeof(decision), "%.18s  %+.1f C | %d h", name, item.offset_c,
             static_cast<int>(item.peak_in_h));
  }
  char buffer[128];
  snprintf(buffer, sizeof(buffer), "%s %.1f C | %s %.1f m/s | %s", DISPLAY_ICON_DOWN,
           forecast_min_temp_c_, DISPLAY_ICON_WIND, forecast_max_wind_ms_, decision);
  give_state_lock_();
  return buffer;
}

uint8_t LuneTouchCoordinator::display_forecast_visible_hours() const {
  if (!take_state_lock_(50))
    return 0;
  const size_t start = forecast_display_start_(forecast_hours_, forecast_hours_count_);
  uint8_t count = 0;
  if (start != lune_touch_forecast_timeline::NO_INDEX)
    count = static_cast<uint8_t>(std::min<size_t>(12, forecast_hours_count_ - start));
  give_state_lock_();
  return count;
}

std::string LuneTouchCoordinator::display_forecast_hour_label(uint8_t index) const {
  if (!take_state_lock_(50))
    return "--";
  ForecastHourState hour{};
  const bool ok = forecast_display_hour_(forecast_hours_, forecast_hours_count_, index, &hour);
  const int64_t timestamp_s = hour.timestamp_s;
  esphome::time::RealTimeClock *clock = time_;
  give_state_lock_();
  if (!ok)
    return "--";
  return forecast_hour_clock_label_(timestamp_s, clock);
}

std::string LuneTouchCoordinator::display_forecast_hour_temp(uint8_t index) const {
  if (!take_state_lock_(50))
    return "--";
  ForecastHourState hour{};
  const bool ok = forecast_display_hour_(forecast_hours_, forecast_hours_count_, index, &hour);
  give_state_lock_();
  if (!ok)
    return "--";
  char buffer[12];
  std::snprintf(buffer, sizeof(buffer), "%+.0f", hour.temp_c);
  return buffer;
}

std::string LuneTouchCoordinator::display_forecast_hour_wind(uint8_t index) const {
  if (!take_state_lock_(50))
    return "--";
  ForecastHourState hour{};
  const bool ok = forecast_display_hour_(forecast_hours_, forecast_hours_count_, index, &hour);
  give_state_lock_();
  if (!ok)
    return "--";
  static const char *kPoints[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  const char *dir = "--";
  if (std::isfinite(hour.wind_dir_deg)) {
    float wrapped = std::fmod(hour.wind_dir_deg, 360.0f);
    if (wrapped < 0.0f)
      wrapped += 360.0f;
    dir = kPoints[static_cast<int>(std::lround(wrapped / 45.0f)) % 8];
  }
  const int speed = std::isfinite(hour.wind_speed_ms)
                        ? static_cast<int>(std::lround(std::max(0.0f, hour.wind_speed_ms)))
                        : 0;
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "%s %d", dir, speed);
  return buffer;
}

uint8_t LuneTouchCoordinator::display_forecast_hour_sky(uint8_t index) const {
  if (!take_state_lock_(50))
    return 0;
  ForecastHourState hour{};
  const bool ok = forecast_display_hour_(forecast_hours_, forecast_hours_count_, index, &hour);
  const float solar = hour.shortwave_wm2;
  const float precip = hour.precipitation_mm;
  const float cover = hour.cloud_cover_pct;
  const float temp = hour.temp_c;
  give_state_lock_();
  if (!ok)
    return 0;
  if (std::isfinite(precip) && precip >= 0.2f)
    return (std::isfinite(temp) && temp <= 0.5f) ? 6 : 5;
  const bool night = !std::isfinite(solar) || solar < 20.0f;
  float clouds = cover;
  if (!std::isfinite(clouds)) {
    if (night)
      clouds = 0.0f;
    else if (solar < 120.0f)
      clouds = 90.0f;
    else if (solar < 350.0f)
      clouds = 45.0f;
    else
      clouds = 10.0f;
  }
  if (clouds < 25.0f)
    return night ? 0 : 3;
  if (clouds < 70.0f)
    return night ? 4 : 2;
  return 1;
}

uint32_t LuneTouchCoordinator::display_forecast_hour_color(uint8_t index) const {
  if (!take_state_lock_(50))
    return lune::tokens::kFaint;
  ForecastHourState hour{};
  const bool ok = forecast_display_hour_(forecast_hours_, forecast_hours_count_, index, &hour);
  give_state_lock_();
  if (!ok)
    return lune::tokens::kFaint;
  if (hour.temp_c < 0.0f)
    return lune::tokens::kInfo;
  if (hour.temp_c >= 10.0f)
    return lune::tokens::kAccent;
  return lune::tokens::kFg;
}

std::string LuneTouchCoordinator::display_forecast_age_text() const {
  if (!take_state_lock_(50))
    return "--";
  const uint32_t fetched = forecast_last_fetch_ms_;
  give_state_lock_();
  if (fetched == 0)
    return "--";
  const uint32_t age_s = (esphome::millis() - fetched) / 1000UL;
  char buffer[8];
  if (age_s < 60)
    std::snprintf(buffer, sizeof(buffer), "1m");
  else if (age_s < 120UL * 60UL)
    std::snprintf(buffer, sizeof(buffer), "%um", static_cast<unsigned>(age_s / 60UL));
  else
    std::snprintf(buffer, sizeof(buffer), "%uh", static_cast<unsigned>(age_s / 3600UL));
  return buffer;
}

uint32_t LuneTouchCoordinator::display_forecast_age_color() const {
  if (!take_state_lock_(50))
    return lune::tokens::kFaint;
  const uint32_t fetched = forecast_last_fetch_ms_;
  give_state_lock_();
  if (fetched == 0)
    return lune::tokens::kFaint;
  const uint32_t age_ms = esphome::millis() - fetched;
  if (age_ms >= FORECAST_AUTO_FETCH_INTERVAL_MS)
    return lune::tokens::kWarn;
  return lune::tokens::kInfo;
}

bool LuneTouchCoordinator::display_problem_visible() const {
  if (!take_state_lock_(50))
    return false;
  bool visible = std::strcmp(authority_state_, "conflict") == 0 ||
                 (heat_source_.enabled && heat_source_.failure_streak > 0);
  for (size_t node_index = 0; !visible && node_index < model_.node_count(); node_index++) {
    const auto *node = model_.node(node_index);
    const auto &telemetry = node_telemetry_[node_index];
    visible = node == nullptr || !node->reachable || model_.is_node_stale(node_index, esphome::millis()) ||
              (telemetry.has_motor_fault && telemetry.motor_fault);
  }
  for (size_t zone_index = 0; !visible && zone_index < model_.zone_count(); zone_index++) {
    const auto *zone = model_.zone(zone_index);
    const auto *live = model_.zone_live(zone_index);
    visible = zone != nullptr && zone->enabled &&
              (live == nullptr || !live->fresh || std::strcmp(live->status, "fault") == 0);
  }
  give_state_lock_();
  return visible;
}

std::string LuneTouchCoordinator::display_problem_text() const {
  if (!take_state_lock_(50))
    return "Status unavailable";
  size_t offline_nodes = 0;
  size_t motor_faults = 0;
  size_t stale_zones = 0;
  for (size_t node_index = 0; node_index < model_.node_count(); node_index++) {
    const auto *node = model_.node(node_index);
    if (node == nullptr || !node->reachable || model_.is_node_stale(node_index, esphome::millis()))
      offline_nodes++;
    const auto &telemetry = node_telemetry_[node_index];
    if (telemetry.has_motor_fault && telemetry.motor_fault)
      motor_faults++;
  }
  for (size_t zone_index = 0; zone_index < model_.zone_count(); zone_index++) {
    const auto *zone = model_.zone(zone_index);
    const auto *live = model_.zone_live(zone_index);
    if (zone != nullptr && zone->enabled &&
        (live == nullptr || !live->fresh || std::strcmp(live->status, "fault") == 0))
      stale_zones++;
  }
  char buffer[160];
  if (std::strcmp(authority_state_, "conflict") == 0)
    snprintf(buffer, sizeof(buffer), "%s Control conflict | open Diagnostics in the web interface",
             DISPLAY_ICON_WARNING);
  else if (motor_faults > 0)
    snprintf(buffer, sizeof(buffer), "%s %u motor fault%s | check the affected manifold",
             DISPLAY_ICON_WARNING, static_cast<unsigned>(motor_faults), motor_faults == 1 ? "" : "s");
  else if (offline_nodes > 0)
    snprintf(buffer, sizeof(buffer), "%s %u manifold%s offline | heating continues locally",
             DISPLAY_ICON_WARNING, static_cast<unsigned>(offline_nodes), offline_nodes == 1 ? "" : "s");
  else if (heat_source_.enabled && heat_source_.failure_streak > 0)
    snprintf(buffer, sizeof(buffer), "%s Heat source connection needs attention", DISPLAY_ICON_WARNING);
  else if (stale_zones > 0)
    snprintf(buffer, sizeof(buffer), "%s %u zone sensor%s need attention", DISPLAY_ICON_WARNING,
             static_cast<unsigned>(stale_zones), stale_zones == 1 ? "" : "s");
  else
    buffer[0] = '\0';
  give_state_lock_();
  return buffer;
}

std::string LuneTouchCoordinator::display_selected_room_text() const {
  if (!take_state_lock_(50))
    return "Zone data busy";
  const ::lune_touch::ZoneBinding *selected = nullptr;
  const ::lune_touch::ZoneLiveState *selected_live = nullptr;
  size_t selected_index = 0;
  for (size_t i = 0; i < model_.zone_count(); i++) {
    const auto *candidate = model_.zone(i);
    if (candidate != nullptr && candidate->enabled &&
        candidate->node_index == display_selected_node_index_ &&
        candidate->zone_index == display_selected_zone_index_) {
      selected = candidate;
      selected_live = model_.zone_live(i);
      selected_index = i;
      break;
    }
  }
  if (selected == nullptr) {
    give_state_lock_();
    return "Select a zone";
  }

  uint8_t day_index = 0;
  uint16_t minute_of_day = 0;
  const bool time_valid = current_schedule_time_(time_, &day_index, &minute_of_day);
  // Comfort / target always come from the sync-group primary when this slot is a secondary.
  const ::lune_touch::ZoneBinding *comfort_zone = selected;
  size_t comfort_index = selected_index;
  if (selected->is_group_secondary && selected_live != nullptr &&
      selected_live->group_primary_zone >= 1 && selected_live->group_primary_zone <= 6) {
    const size_t primary_slot = static_cast<size_t>(selected_live->group_primary_zone - 1);
    for (size_t i = 0; i < model_.zone_count(); i++) {
      const auto *candidate = model_.zone(i);
      if (candidate == nullptr || !candidate->enabled || candidate->is_group_secondary)
        continue;
      if (candidate->node_index == selected->node_index && candidate->zone_index == primary_slot) {
        comfort_zone = candidate;
        comfort_index = i;
        break;
      }
    }
  }
  const auto effective = ::lune_touch::HouseModel::effective_comfort(
      *comfort_zone, time_valid, day_index, minute_of_day);
  const auto *live = model_.zone_live(selected_index);
  const char *name = display_binding_room_label_(model_, selected, selected_live);
  const char *status = live != nullptr && live->fresh ? live->status : "sensor unavailable";
  char temperature[16];
  if (live != nullptr && live->has_temperature)
    snprintf(temperature, sizeof(temperature), "%.1f C", live->temperature_c);
  else
    snprintf(temperature, sizeof(temperature), "--.- C");
  char buffer[128];
  const uint8_t members = selected_live != nullptr
                              ? group_member_count_(selected_live->group_members_mask)
                              : 0;
  if (members > 1) {
    snprintf(buffer, sizeof(buffer), "%s (Z%u sync)  %s  Target %.1f C  %s",
             name, static_cast<unsigned>(selected->zone_index + 1), temperature,
             effective.setpoint_c, status);
  } else {
    snprintf(buffer, sizeof(buffer), "%s  %s  Target %.1f C  %s",
             name, temperature, effective.setpoint_c, status);
  }
  (void) comfort_index;
  give_state_lock_();
  return buffer;
}

std::string LuneTouchCoordinator::display_selected_room_name() const {
  if (!take_state_lock_(50))
    return "Zone";
  for (size_t i = 0; i < model_.zone_count(); i++) {
    const auto *candidate = model_.zone(i);
    const auto *live = model_.zone_live(i);
    if (candidate == nullptr || !candidate->enabled ||
        candidate->node_index != display_selected_node_index_ ||
        candidate->zone_index != display_selected_zone_index_)
      continue;
    const char *name = display_binding_room_label_(model_, candidate, live);
    char buffer[40];
    std::snprintf(buffer, sizeof(buffer), "%.28s", name);
    for (size_t i = 0; buffer[i] != '\0'; ) {
      unsigned char *p = reinterpret_cast<unsigned char *>(&buffer[i]);
      if (p[0] >= 'a' && p[0] <= 'z') {
        p[0] = static_cast<unsigned char>(p[0] - 'a' + 'A');
        i++;
        continue;
      }
      if (p[0] == 0xC3 && p[1] != 0) {
        switch (p[1]) {
          case 0xA6: p[1] = 0x86; break;  // æ -> Æ
          case 0xB8: p[1] = 0x98; break;  // ø -> Ø
          case 0xA5: p[1] = 0x85; break;  // å -> Å
          case 0xA4: p[1] = 0x84; break;  // ä -> Ä
          case 0xB6: p[1] = 0x96; break;  // ö -> Ö
          case 0xBC: p[1] = 0x9C; break;  // ü -> Ü
          default: break;
        }
        i += 2;
        continue;
      }
      if ((p[0] & 0x80) == 0)
        i += 1;
      else if ((p[0] & 0xE0) == 0xC0)
        i += 2;
      else if ((p[0] & 0xF0) == 0xE0)
        i += 3;
      else
        i += 1;
    }
    give_state_lock_();
    return buffer;
  }
  char fallback[16];
  std::snprintf(fallback, sizeof(fallback), "ZONE %u",
                static_cast<unsigned>(display_selected_zone_index_ + 1));
  give_state_lock_();
  return fallback;
}

std::string LuneTouchCoordinator::display_zone_detail_meta_text() const {
  if (!take_state_lock_(50))
    return "--:--";
  char clock[8] = "--:--";
  if (time_ != nullptr) {
    const auto now = time_->now();
    if (now.is_valid())
      std::snprintf(clock, sizeof(clock), "%02u:%02u", static_cast<unsigned>(now.hour),
                    static_cast<unsigned>(now.minute));
  }
  give_state_lock_();
  return clock;
}

uint8_t LuneTouchCoordinator::display_wifi_bars() const {
  return wifi_display_bars_();
}

std::string LuneTouchCoordinator::display_wifi_text() const {
  switch (wifi_display_bars_()) {
    case 5:
    case 4:
      return "WiFi OK";
    case 3:
      return "WiFi ok";
    case 2:
    case 1:
      return "WiFi weak";
    default:
      return "WiFi off";
  }
}

uint32_t LuneTouchCoordinator::display_wifi_color() const {
  switch (wifi_display_bars_()) {
    case 5:
      return lune::tokens::kOk;
    case 4:
    case 3:
      return lune::tokens::kFg;
    default:
      return lune::tokens::kWarn;
  }
}

uint8_t LuneTouchCoordinator::display_enabled_zone_count() const {
  if (!take_state_lock_(50))
    return 0;
  uint8_t count = 0;
  for (size_t i = 0; i < model_.zone_count(); i++) {
    const auto *candidate = model_.zone(i);
    if (candidate != nullptr && candidate->enabled)
      count++;
  }
  give_state_lock_();
  return count;
}

uint8_t LuneTouchCoordinator::display_calling_zone_count() const {
  if (!take_state_lock_(50))
    return 0;
  const uint8_t count =
      static_cast<uint8_t>(std::min<size_t>(255, model_.calling_zone_count()));
  give_state_lock_();
  return count;
}

uint8_t LuneTouchCoordinator::display_zone_overview_state(uint8_t node_index,
                                                         uint8_t physical_zone_index) const {
  // 0=empty, 1=off, 2=idle, 3=calling, 4=fault
  if (!take_state_lock_(50))
    return 0;
  bool any_enabled = false;
  bool this_enabled = false;
  for (size_t i = 0; i < model_.zone_count(); i++) {
    const auto *zone = model_.zone(i);
    if (zone == nullptr || !zone->enabled || zone->node_index != node_index)
      continue;
    any_enabled = true;
    if (zone->zone_index == physical_zone_index)
      this_enabled = true;
  }
  const bool slot_used = this_enabled ||
      (!any_enabled && physical_zone_index < ::lune_touch::ZONES_PER_NODE &&
       model_.node(node_index) != nullptr);
  if (!slot_used) {
    give_state_lock_();
    return 0;
  }

  const auto *node = model_.node(node_index);
  if (node == nullptr || !node->reachable || model_.is_node_stale(node_index, esphome::millis())) {
    give_state_lock_();
    return 4;
  }

  const ::lune_touch::ZoneLiveState *live = nullptr;
  bool enabled = false;
  for (size_t i = 0; i < model_.zone_count(); i++) {
    const auto *zone = model_.zone(i);
    if (zone != nullptr && zone->node_index == node_index &&
        zone->zone_index == physical_zone_index) {
      enabled = zone->enabled;
      live = model_.zone_live(i);
      break;
    }
  }
  if (!enabled) {
    give_state_lock_();
    return 1;
  }
  if (live == nullptr) {
    give_state_lock_();
    return 2;
  }
  if (!live->fresh || std::strcmp(live->status, "fault") == 0) {
    give_state_lock_();
    return 4;
  }
  if (std::strcmp(live->status, "off") == 0) {
    give_state_lock_();
    return 1;
  }
  if (std::strcmp(live->status, "heat") == 0 || std::strcmp(live->status, "call") == 0 ||
      std::strcmp(live->status, "preheat") == 0) {
    give_state_lock_();
    return 3;
  }
  give_state_lock_();
  return 2;
}

uint8_t LuneTouchCoordinator::display_visible_node_count() const {
  if (!take_state_lock_(50))
    return 0;
  const uint8_t count = static_cast<uint8_t>(std::min<size_t>(4, model_.node_count()));
  give_state_lock_();
  return count;
}

uint8_t LuneTouchCoordinator::display_selected_zone_ordinal() const {
  if (!take_state_lock_(50))
    return 0;
  uint8_t ordinal = 0;
  for (size_t i = 0; i < model_.zone_count(); i++) {
    const auto *candidate = model_.zone(i);
    if (candidate == nullptr || !candidate->enabled)
      continue;
    if (candidate->node_index == display_selected_node_index_ &&
        candidate->zone_index == display_selected_zone_index_) {
      give_state_lock_();
      return ordinal;
    }
    ordinal++;
  }
  give_state_lock_();
  return 0;
}

bool LuneTouchCoordinator::display_step_selected_zone(int8_t delta) {
  if (delta == 0 || !take_state_lock_(50))
    return false;
  const size_t node_count = std::min<size_t>(4, model_.node_count());
  if (node_count == 0) {
    give_state_lock_();
    return false;
  }
  const int zones_per_node = static_cast<int>(::lune_touch::ZONES_PER_NODE);
  const int total = static_cast<int>(node_count) * zones_per_node;
  int current = static_cast<int>(display_selected_node_index_) * zones_per_node +
                static_cast<int>(display_selected_zone_index_);
  if (display_selected_node_index_ >= node_count ||
      display_selected_zone_index_ >= ::lune_touch::ZONES_PER_NODE)
    current = 0;
  int next = (current + static_cast<int>(delta)) % total;
  if (next < 0)
    next += total;
  display_selected_node_index_ = static_cast<uint8_t>(next / zones_per_node);
  display_selected_zone_index_ = static_cast<uint8_t>(next % zones_per_node);
  const bool changed = next != current;
  give_state_lock_();
  return changed;
}

bool LuneTouchCoordinator::display_select_controller(uint8_t node_index) {
  const uint8_t zone = display_selected_zone_index_ < ::lune_touch::ZONES_PER_NODE
                           ? display_selected_zone_index_
                           : 0;
  return display_select_zone(node_index, zone);
}

bool LuneTouchCoordinator::display_select_zone(uint8_t node_index, uint8_t physical_zone_index) {
  if (physical_zone_index >= ::lune_touch::ZONES_PER_NODE || !take_state_lock_(50))
    return false;
  if (node_index >= model_.node_count() || model_.node(node_index) == nullptr) {
    give_state_lock_();
    return false;
  }
  display_selected_node_index_ = node_index;
  display_selected_zone_index_ = physical_zone_index;
  give_state_lock_();
  return true;
}

bool LuneTouchCoordinator::display_select_room(uint8_t row) {
  if (!take_state_lock_(50))
    return false;
  size_t active_count = 0;
  for (size_t i = 0; i < model_.zone_count(); i++) {
    const auto *candidate = model_.zone(i);
    if (candidate == nullptr || !candidate->enabled)
      continue;
    if (active_count == row) {
      display_selected_node_index_ = candidate->node_index;
      display_selected_zone_index_ = candidate->zone_index;
      give_state_lock_();
      return true;
    }
    active_count++;
  }
  give_state_lock_();
  return false;
}

bool LuneTouchCoordinator::display_adjust_primary_target(float delta_c) {
  if (!std::isfinite(delta_c) || !take_state_lock_(100))
    return false;
  const ::lune_touch::ZoneBinding *binding = nullptr;
  for (size_t i = 0; i < model_.zone_count(); i++) {
    const auto *candidate = model_.zone(i);
    if (candidate != nullptr && candidate->enabled &&
        candidate->node_index == display_selected_node_index_ &&
        candidate->zone_index == display_selected_zone_index_) {
      binding = candidate;
      break;
    }
  }
  if (binding == nullptr) {
    give_state_lock_();
    return false;
  }
  char room_id[sizeof(binding->room_id)]{};
  std::strncpy(room_id, binding->room_id, sizeof(room_id) - 1);
  const float setpoint = binding->comfort_setpoint_c + delta_c;
  const uint8_t priority = binding->priority;
  const float bias = binding->comfort_bias_c;
  give_state_lock_();
  char response[192]{};
  return set_zone_comfort(room_id, setpoint, priority, bias, response, sizeof(response));
}

bool LuneTouchCoordinator::display_boost_primary_room() {
  if (!take_state_lock_(100))
    return false;
  char room_id[32]{};
  for (size_t i = 0; i < model_.zone_count(); i++) {
    const auto *candidate = model_.zone(i);
    if (candidate != nullptr && candidate->enabled &&
        candidate->node_index == display_selected_node_index_ &&
        candidate->zone_index == display_selected_zone_index_) {
      std::strncpy(room_id, candidate->room_id, sizeof(room_id) - 1);
      break;
    }
  }
  give_state_lock_();
  if (room_id[0] == '\0')
    return false;
  char response[320]{};
  return queue_setpoint_command(room_id, 0.5f, 2700, "local display boost", response, sizeof(response));
}

bool LuneTouchCoordinator::display_away_primary_room() {
  if (!take_state_lock_(100))
    return false;
  char room_id[32]{};
  for (size_t i = 0; i < model_.zone_count(); i++) {
    const auto *candidate = model_.zone(i);
    if (candidate != nullptr && candidate->enabled &&
        candidate->node_index == display_selected_node_index_ &&
        candidate->zone_index == display_selected_zone_index_) {
      std::strncpy(room_id, candidate->room_id, sizeof(room_id) - 1);
      break;
    }
  }
  give_state_lock_();
  if (room_id[0] == '\0')
    return false;
  char response[320]{};
  return queue_setpoint_command(room_id, -2.0f, 21600, "local display away", response, sizeof(response));
}

float LuneTouchCoordinator::display_house_target_c() const {
  if (!take_state_lock_(50))
    return NAN;
  const auto strategy = model_.strategy_snapshot();
  const float value = strategy.has_house_target ? strategy.house_target_c : NAN;
  give_state_lock_();
  return value;
}

float LuneTouchCoordinator::display_house_temperature_c() const {
  if (!take_state_lock_(50))
    return NAN;
  const auto strategy = model_.strategy_snapshot();
  const float value = strategy.has_physical_temperature ? strategy.physical_temperature_c : NAN;
  give_state_lock_();
  return value;
}

bool LuneTouchCoordinator::display_adjust_house_target(float delta_c) {
  if (!std::isfinite(delta_c) || !take_state_lock_(100))
    return false;
  struct RoomAdjust {
    char room_id[32]{};
    float setpoint_c{0.0f};
    uint8_t priority{0};
    float bias_c{0.0f};
  };
  RoomAdjust rooms[::lune_touch::MAX_HOUSE_ZONES]{};
  size_t room_count = 0;
  for (size_t i = 0; i < model_.zone_count() && room_count < ::lune_touch::MAX_HOUSE_ZONES; i++) {
    const auto *candidate = model_.zone(i);
    if (candidate == nullptr || !candidate->enabled || candidate->room_id[0] == '\0')
      continue;
    std::strncpy(rooms[room_count].room_id, candidate->room_id, sizeof(rooms[room_count].room_id) - 1);
    rooms[room_count].setpoint_c = candidate->comfort_setpoint_c + delta_c;
    rooms[room_count].priority = candidate->priority;
    rooms[room_count].bias_c = candidate->comfort_bias_c;
    room_count++;
  }
  give_state_lock_();
  if (room_count == 0)
    return false;
  bool any = false;
  char response[192]{};
  for (size_t i = 0; i < room_count; i++) {
    if (set_zone_comfort(rooms[i].room_id, rooms[i].setpoint_c, rooms[i].priority, rooms[i].bias_c,
                         response, sizeof(response)))
      any = true;
  }
  return any;
}

uint8_t LuneTouchCoordinator::display_active_page() const {
  return display_active_page_;
}

uint8_t LuneTouchCoordinator::display_return_page() const {
  return display_return_page_;
}

bool LuneTouchCoordinator::display_set_active_page(uint8_t page) {
  if (page != DISPLAY_PAGE_HOME && page != DISPLAY_PAGE_ZONE_DETAIL &&
      page != DISPLAY_PAGE_SYSTEM)
    page = DISPLAY_PAGE_HOME;
  if (!take_state_lock_(50))
    return false;
  if (page == display_active_page_) {
    give_state_lock_();
    return false;
  }
  if (page == DISPLAY_PAGE_ZONE_DETAIL && display_active_page_ != DISPLAY_PAGE_ZONE_DETAIL)
    display_return_page_ = display_active_page_;
  display_active_page_ = page;
  give_state_lock_();
  return true;
}

bool LuneTouchCoordinator::display_heat_source_enabled() const {
  if (!take_state_lock_(50))
    return false;
  const bool enabled = heat_source_.enabled;
  give_state_lock_();
  return enabled;
}

bool LuneTouchCoordinator::display_set_heat_source_enabled(bool enabled) {
  char response[192]{};
  return set_heat_source_settings(true, enabled, nullptr, 0, nullptr, 0, response, sizeof(response));
}

bool LuneTouchCoordinator::display_weather_compensation_enabled() const {
  if (!take_state_lock_(50))
    return false;
  const bool enabled = weather_max_boost_c_ > 0.05f;
  give_state_lock_();
  return enabled;
}

bool LuneTouchCoordinator::display_set_weather_compensation_enabled(bool enabled) {
  float value = 1.5f;
  if (!take_state_lock_(50))
    return false;
  if (!enabled) {
    if (weather_max_boost_c_ > 0.05f)
      weather_max_boost_resume_c_ = weather_max_boost_c_;
    value = 0.0f;
  } else if (weather_max_boost_resume_c_ > 0.05f) {
    value = weather_max_boost_resume_c_;
  }
  give_state_lock_();
  char response[192]{};
  return set_weather_settings(value, response, sizeof(response));
}

std::string LuneTouchCoordinator::display_idle_timeout_text() const {
  if (!take_state_lock_(50))
    return "1 min";
  const uint16_t timeout_s = display_idle_timeout_s_;
  give_state_lock_();
  if (timeout_s == 0)
    return "Always on";
  if (timeout_s == 120)
    return "2 min";
  if (timeout_s == 300)
    return "5 min";
  return "1 min";
}

bool LuneTouchCoordinator::display_cycle_idle_timeout() {
  if (!take_state_lock_(50))
    return false;
  const uint16_t current = display_idle_timeout_s_;
  give_state_lock_();
  uint32_t next = 60;
  if (current == 60)
    next = 120;
  else if (current == 120)
    next = 300;
  else if (current == 300)
    next = 0;
  char response[192]{};
  return set_settings(nullptr, nullptr, nullptr, nullptr, false, false, nullptr, nullptr, nullptr,
                      nullptr, true, next, response, sizeof(response));
}

std::string LuneTouchCoordinator::display_system_fact_text(uint8_t index) const {
  if (!take_state_lock_(50))
    return "Unavailable";
  char buffer[96]{};
  switch (index) {
    case 0: {
      const auto strategy = model_.strategy_snapshot();
      if (strategy.has_physical_temperature)
        snprintf(buffer, sizeof(buffer), "House\n%.1f C", strategy.physical_temperature_c);
      else
        snprintf(buffer, sizeof(buffer), "House\n--.- C");
      break;
    }
    case 1: {
      if (!heat_source_.enabled)
        snprintf(buffer, sizeof(buffer), "Heat source\nDisabled");
      else if (heat_source_.has_last_push && heat_source_.failure_streak == 0)
        snprintf(buffer, sizeof(buffer), "Heat source\nOn %.1f C", heat_source_.last_confirmed_value_c);
      else if (heat_source_.failure_streak > 0)
        snprintf(buffer, sizeof(buffer), "Heat source\nFault");
      else
        snprintf(buffer, sizeof(buffer), "Heat source\nIdle");
      break;
    }
    case 2: {
      size_t ready = 0;
      for (size_t i = 0; i < model_.node_count(); i++) {
        const auto *node = model_.node(i);
        if (node != nullptr && node->reachable && !model_.is_node_stale(i, esphome::millis()))
          ready++;
      }
      snprintf(buffer, sizeof(buffer), "Controllers\n%u / %u", static_cast<unsigned>(ready),
               static_cast<unsigned>(model_.node_count()));
      break;
    }
    case 3:
      snprintf(buffer, sizeof(buffer), "Forecast\n%s", forecast_status_);
      break;
    case 4: {
      const unsigned long uptime_s = esphome::millis() / 1000UL;
      snprintf(buffer, sizeof(buffer), "Uptime\n%luh %lum", uptime_s / 3600UL, (uptime_s / 60UL) % 60UL);
      break;
    }
    case 5:
#ifdef ESPHOME_PROJECT_VERSION
      snprintf(buffer, sizeof(buffer), "Firmware\n%s", ESPHOME_PROJECT_VERSION);
#else
      snprintf(buffer, sizeof(buffer), "Firmware\n%s", coordinator_name_);
#endif
      break;
    default:
      snprintf(buffer, sizeof(buffer), "--");
      break;
  }
  give_state_lock_();
  return buffer;
}

bool LuneTouchCoordinator::display_override_active() const {
  if (!take_state_lock_(50))
    return false;
  const int64_t now_epoch_s = current_epoch_s_();
  const auto *active = ledger_.latest_active(esphome::millis(), now_epoch_s);
  const bool present = active != nullptr && std::strstr(active->reason, "local display") != nullptr;
  give_state_lock_();
  return present;
}

std::string LuneTouchCoordinator::display_override_text() const {
  if (!take_state_lock_(50))
    return "";
  const int64_t now_epoch_s = current_epoch_s_();
  const auto *active = ledger_.latest_active(esphome::millis(), now_epoch_s);
  char buffer[96]{};
  if (active == nullptr || std::strstr(active->reason, "local display") == nullptr) {
    give_state_lock_();
    return "";
  }
  const char *kind = active->requested_offset_c >= 0.0f ? "Boost" : "Away";
  snprintf(buffer, sizeof(buffer), "%s active  %.1f C  %s", kind, active->accepted_offset_c,
           active->room_id);
  give_state_lock_();
  return buffer;
}

bool LuneTouchCoordinator::display_set_selected_target(float value_c) {
  if (!std::isfinite(value_c) || value_c < 5.0f || value_c > 35.0f || !take_state_lock_(100))
    return false;
  char room_id[32]{};
  uint8_t priority = 0;
  float bias = 0.0f;
  for (size_t i = 0; i < model_.zone_count(); i++) {
    const auto *candidate = model_.zone(i);
    if (candidate != nullptr && candidate->enabled &&
        candidate->node_index == display_selected_node_index_ &&
        candidate->zone_index == display_selected_zone_index_) {
      std::strncpy(room_id, candidate->room_id, sizeof(room_id) - 1);
      priority = candidate->priority;
      bias = candidate->comfort_bias_c;
      break;
    }
  }
  give_state_lock_();
  if (room_id[0] == '\0')
    return false;
  char response[192]{};
  return set_zone_comfort(room_id, value_c, priority, bias, response, sizeof(response));
}

bool LuneTouchCoordinator::display_reset_selected_motor_fault() {
  if (!take_state_lock_(100))
    return false;
  char room_id[32]{};
  for (size_t i = 0; i < model_.zone_count(); i++) {
    const auto *candidate = model_.zone(i);
    if (candidate != nullptr && candidate->enabled &&
        candidate->node_index == display_selected_node_index_ &&
        candidate->zone_index == display_selected_zone_index_) {
      std::strncpy(room_id, candidate->room_id, sizeof(room_id) - 1);
      break;
    }
  }
  give_state_lock_();
  if (room_id[0] == '\0')
    return false;
  char response[320]{};
  return request_motor_action(room_id, "reset_fault", nullptr, response, sizeof(response));
}

void LuneTouchCoordinator::write_overview_json(char *buffer, size_t capacity) const {
  size_t stale_nodes = 0;
  const uint32_t now = esphome::millis();
  for (size_t i = 0; i < model_.node_count(); i++) {
    if (model_.is_node_stale(i, now))
      stale_nodes++;
  }
  const auto *latest = ledger_.latest();
  const auto strategy = model_.strategy_snapshot();
  char house_temp[16];
  char house_target[16];
  char authority[32];
  char authority_state[40];
  // Prefer healthy physical; else last-known (stale boards) so the dashboard
  // does not blank while push stays blocked on coverage.
  const float display_temp =
      strategy.has_physical_temperature
          ? strategy.physical_temperature_c
          : (strategy.has_last_known_temperature
                 ? strategy.last_known_temperature_c
                 : (strategy.has_temperature_preview ? strategy.temperature_preview_c : NAN));
  json_float_token_(house_temp, sizeof(house_temp), display_temp, 1);
  json_float_token_(house_target, sizeof(house_target),
                    strategy.has_house_target ? strategy.house_target_c : NAN, 1);
  json_escape_("Touch", authority, sizeof(authority));
  json_escape_(authority_state_, authority_state, sizeof(authority_state));
  snprintf(buffer, capacity,
           "{\"summary\":{\"zones\":%u,\"nodes\":%u,\"calling\":%u,\"stale_nodes\":%u,"
           "\"comfort_avg_c\":%.1f,\"forecast_status\":\"%s\",\"latest_command\":\"%s\"},"
           "\"house_temp_c\":%s,\"house_target_c\":%s,"
           "\"calling_rooms\":%u,\"expected_manifolds\":%u,\"contributing_manifolds\":%u,"
           "\"coverage_ratio\":%.3f,\"authority\":\"%s\",\"authority_state\":\"%s\"}",
           static_cast<unsigned>(model_.active_zone_count()),
           static_cast<unsigned>(model_.node_count()),
           static_cast<unsigned>(model_.calling_zone_count()),
           static_cast<unsigned>(stale_nodes),
           model_.average_comfort_setpoint_c(),
           forecast_status_,
           latest != nullptr ? ::lune_touch::command_result_name(latest->result) : "none",
           house_temp, house_target,
           static_cast<unsigned>(model_.calling_zone_count()),
           static_cast<unsigned>(strategy.expected_manifolds),
           static_cast<unsigned>(strategy.contributing_manifolds),
           strategy.coverage_ratio, authority, authority_state);
}

void LuneTouchCoordinator::write_nodes_json(char *buffer, size_t capacity) const {
  size_t off = 0;
  appendf_(buffer, capacity, off,
           "{\"poll_generation\":%lu,\"poll_task\":%s,\"last_poll_ms\":%lu,\"nodes\":[",
           static_cast<unsigned long>(poll_generation_),
           poll_task_handle_ != nullptr ? "true" : "false",
           static_cast<unsigned long>(last_poll_ms_));
  bool first = true;
  for (size_t i = 0; i < model_.node_count(); i++) {
    const auto *node = model_.node(i);
    if (node == nullptr)
      continue;
    char success_host[96];
    char failure[112];
    char pairing_fingerprint[48];
    char node_id_esc[48];
    char node_name_esc[80];
    char hostname_esc[96];
    char ip_esc[48];
    char model_esc[40];
    char firmware_esc[48];
    char avg_temp[16] = "null";
    char avg_setpoint[16] = "null";
    char flow_c[16];
    char return_c[16];
    char avg_valve_pct[16];
    char motor_current_ma[16];
    size_t mapped_zones = 0;
    size_t fresh_zones = 0;
    size_t calling_zones = 0;
    float temp_sum = 0.0f;
    size_t temp_count = 0;
    float setpoint_sum = 0.0f;
    size_t setpoint_count = 0;
    for (size_t z = 0; z < model_.zone_count(); z++) {
      const auto *zone = model_.zone(z);
      const auto *live = model_.zone_live(z);
      if (zone == nullptr || !zone->enabled || zone->node_index != i)
        continue;
      mapped_zones++;
      if (live != nullptr && live->fresh)
        fresh_zones++;
      if (live != nullptr &&
          (std::strcmp(live->status, "heat") == 0 || std::strcmp(live->status, "call") == 0 ||
           std::strcmp(live->status, "preheat") == 0))
        calling_zones++;
      if (live != nullptr && live->has_temperature) {
        temp_sum += live->temperature_c;
        temp_count++;
      }
      if (live != nullptr && live->has_setpoint) {
        setpoint_sum += live->setpoint_c;
        setpoint_count++;
      }
    }
    if (temp_count > 0)
      snprintf(avg_temp, sizeof(avg_temp), "%.1f", temp_sum / static_cast<float>(temp_count));
    if (setpoint_count > 0)
      snprintf(avg_setpoint, sizeof(avg_setpoint), "%.1f", setpoint_sum / static_cast<float>(setpoint_count));
    const NodeTelemetryState &telemetry = node_telemetry_[i];
    nullable_float_(flow_c, sizeof(flow_c), telemetry.has_flow, telemetry.flow_c);
    nullable_float_(return_c, sizeof(return_c), telemetry.has_return, telemetry.return_c);
    nullable_float_(avg_valve_pct, sizeof(avg_valve_pct), telemetry.has_avg_valve, telemetry.avg_valve_pct);
    nullable_float_(motor_current_ma, sizeof(motor_current_ma), telemetry.has_motor_current, telemetry.motor_current_ma);
    json_escape_(node_last_success_host_[i], success_host, sizeof(success_host));
    json_escape_(node_last_failure_[i], failure, sizeof(failure));
    json_escape_(node->pairing_fingerprint, pairing_fingerprint, sizeof(pairing_fingerprint));
    json_escape_(node->node_id, node_id_esc, sizeof(node_id_esc));
    json_escape_(node->name[0] != '\0' ? node->name : node->node_id, node_name_esc, sizeof(node_name_esc));
    json_escape_(node->hostname, hostname_esc, sizeof(hostname_esc));
    json_escape_(node->fallback_ip, ip_esc, sizeof(ip_esc));
    json_escape_(node->model, model_esc, sizeof(model_esc));
    json_escape_(node->firmware, firmware_esc, sizeof(firmware_esc));
    char device_name_esc[96];
    json_escape_(i < ::lune_touch::MAX_NODES ? node_device_name_[i] : "", device_name_esc, sizeof(device_name_esc));
    if (!appendf_(buffer, capacity, off,
                  "%s{\"id\":\"%s\",\"name\":\"%s\",\"device_name\":\"%s\",\"lease\":\"%s\",\"poll\":{\"summary\":\"%s\",\"skipped\":%lu},\"hostname\":\"%s\",\"ip\":\"%s\",\"model\":\"%s\","
                  "\"firmware\":\"%s\",\"reachable\":%s,\"trust\":%u,\"trust_label\":\"%s\","
                  "\"pairing_fingerprint\":\"%s\",\"last_seen_ms\":%lu,"
                  "\"last_success_host\":\"%s\",\"last_failure\":\"%s\","
                  "\"health\":{\"imported_zones\":%u,\"mapped_zones\":%u,\"fresh_zones\":%u,\"stale_zones\":%u,"
                  "\"calling_zones\":%u,\"avg_temp_c\":%s,\"avg_setpoint_c\":%s},"
                  "\"runtime\":{\"active_zones\":%u,\"avg_valve_pct\":%s,"
                  "\"flow_c\":%s,\"return_c\":%s,\"drivers_enabled\":%s,"
                  "\"motor_fault\":%s,\"motor_current_ma\":%s}}",
                  first ? "" : ",", node_id_esc, node_name_esc, device_name_esc,
                  i < ::lune_touch::MAX_NODES
                      ? (node_lease_state_[i] == 1 ? "granted" : (node_lease_state_[i] == 2 ? "refused" : "none"))
                      : "none",
                  i < ::lune_touch::MAX_NODES
                      ? (node_summary_support_[i] == 1 ? "yes" : (node_summary_support_[i] == 2 ? "no" : "unknown"))
                      : "unknown",
                  static_cast<unsigned long>(i < ::lune_touch::MAX_NODES ? node_summary_skips_[i] : 0),
                  hostname_esc, ip_esc, model_esc,
                  firmware_esc, node->reachable ? "true" : "false",
                  static_cast<unsigned>(node->trust), ::lune_touch::node_trust_name(node->trust),
                  pairing_fingerprint,
                  static_cast<unsigned long>(node->last_seen_ms),
                  success_host, failure,
                  static_cast<unsigned>(mapped_zones),
                  static_cast<unsigned>(mapped_zones),
                  static_cast<unsigned>(fresh_zones),
                  static_cast<unsigned>(mapped_zones > fresh_zones ? mapped_zones - fresh_zones : 0),
                  static_cast<unsigned>(calling_zones), avg_temp, avg_setpoint,
                  static_cast<unsigned>(telemetry.active_zones), avg_valve_pct, flow_c, return_c,
                  telemetry.has_drivers_enabled && telemetry.drivers_enabled ? "true" : "false",
                  telemetry.has_motor_fault && telemetry.motor_fault ? "true" : "false",
                  motor_current_ma))
      break;
    first = false;
  }
  appendf_(buffer, capacity, off, "]}");
}

void LuneTouchCoordinator::write_node_scan_json(char *buffer, size_t capacity) const {
  const uint32_t now = esphome::millis();
  const bool pending = node_refresh_requested_ || lan_scan_requested_;
  const bool lan = lan_scan_requested_ || std::strcmp(lan_discovery_, "lan_probe") == 0;
  size_t off = 0;
  appendf_(buffer, capacity, off,
           "{\"scan\":\"%s\",\"discovery\":\"%s\","
           "\"poll_generation\":%lu,\"poll_pending\":%s,"
           "\"found\":[",
           lan ? "lan" : "known_nodes",
           lan ? "lan_probe" : "manual_or_known_nodes",
           static_cast<unsigned long>(poll_generation_),
           pending ? "true" : "false");
  // The scan endpoint only queues work. Keep the response fast and expose
  // the generation the UI should wait beyond before fetching names.
  // The pending flag is conservative because the coordinator may consume the
  // notification immediately after the response starts being assembled.
  bool first = true;
  for (size_t i = 0; i < model_.node_count(); i++) {
    const auto *node = model_.node(i);
    if (node == nullptr)
      continue;
    const bool stale = model_.is_node_stale(i, now);
    char pairing_fingerprint[48];
    char node_id_esc[48];
    char node_name_esc[80];
    char hostname_esc[96];
    char ip_esc[48];
    char model_esc[40];
    char firmware_esc[48];
    json_escape_(node->pairing_fingerprint, pairing_fingerprint, sizeof(pairing_fingerprint));
    json_escape_(node->node_id, node_id_esc, sizeof(node_id_esc));
    json_escape_(node->name[0] != '\0' ? node->name : node->node_id, node_name_esc, sizeof(node_name_esc));
    json_escape_(node->hostname, hostname_esc, sizeof(hostname_esc));
    json_escape_(node->fallback_ip, ip_esc, sizeof(ip_esc));
    json_escape_(node->model, model_esc, sizeof(model_esc));
    json_escape_(node->firmware, firmware_esc, sizeof(firmware_esc));
    if (!appendf_(buffer, capacity, off,
                  "%s{\"id\":\"%s\",\"name\":\"%s\",\"hostname\":\"%s\",\"ip\":\"%s\",\"model\":\"%s\","
                  "\"firmware\":\"%s\",\"pairing_fingerprint\":\"%s\","
                  "\"reachable\":%s,\"stale\":%s,\"source\":\"known_node\"}",
                  first ? "" : ",", node_id_esc, node_name_esc, hostname_esc, ip_esc,
                  model_esc, firmware_esc, pairing_fingerprint,
                  node->reachable ? "true" : "false",
                  stale ? "true" : "false"))
      break;
    first = false;
  }
  for (uint8_t i = 0; i < lan_candidate_count_ && i < MAX_LAN_CANDIDATES; i++) {
    const auto &candidate = lan_candidates_[i];
    bool duplicate = false;
    for (size_t n = 0; n < model_.node_count(); n++) {
      const auto *node = model_.node(n);
      if (node == nullptr)
        continue;
      if (candidate.pairing_fingerprint[0] != '\0' &&
          std::strcmp(node->pairing_fingerprint, candidate.pairing_fingerprint) == 0) {
        duplicate = true;
        break;
      }
      if (candidate.ip[0] != '\0' && std::strcmp(node->fallback_ip, candidate.ip) == 0) {
        duplicate = true;
        break;
      }
    }
    if (duplicate)
      continue;
    char pairing_fingerprint[48];
    char node_id_esc[48];
    char hostname_esc[96];
    char ip_esc[48];
    char model_esc[40];
    char firmware_esc[48];
    json_escape_(candidate.pairing_fingerprint, pairing_fingerprint, sizeof(pairing_fingerprint));
    json_escape_(candidate.id, node_id_esc, sizeof(node_id_esc));
    json_escape_(candidate.hostname, hostname_esc, sizeof(hostname_esc));
    json_escape_(candidate.ip, ip_esc, sizeof(ip_esc));
    json_escape_(candidate.model, model_esc, sizeof(model_esc));
    json_escape_(candidate.firmware, firmware_esc, sizeof(firmware_esc));
    if (!appendf_(buffer, capacity, off,
                  "%s{\"id\":\"%s\",\"hostname\":\"%s\",\"ip\":\"%s\",\"model\":\"%s\","
                  "\"firmware\":\"%s\",\"pairing_fingerprint\":\"%s\","
                  "\"reachable\":true,\"stale\":false,\"source\":\"lan_probe\"}",
                  first ? "" : ",", node_id_esc, hostname_esc, ip_esc, model_esc,
                  firmware_esc, pairing_fingerprint))
      break;
    first = false;
  }
  appendf_(buffer, capacity, off, "]}");
}

void LuneTouchCoordinator::write_rooms_json(char *buffer, size_t capacity) const {
  size_t off = 0;
  if (buffer == nullptr || capacity == 0)
    return;
  buffer[0] = '\0';
  appendf_(buffer, capacity, off, "{\"rooms\":[");
  bool first_room = true;
  for (size_t room_index = 0; room_index < model_.room_count(); room_index++) {
    const auto *room = model_.room(room_index);
    if (room == nullptr || !room->enabled)
      continue;
    ::lune_touch::ResolvedRoomLoop loops[::lune_touch::MAX_HOUSE_ZONES]{};
    const size_t loop_count =
        model_.resolve_room_loops(room->room_id, loops, ::lune_touch::MAX_HOUSE_ZONES);
    const auto demand = model_.room_heat_demand(room->room_id);
    char room_id_esc[48];
    char name_esc[80];
    json_escape_(room->room_id, room_id_esc, sizeof(room_id_esc));
    json_escape_(room->room_name, name_esc, sizeof(name_esc));
    appendf_(buffer, capacity, off,
             "%s{\"room_id\":\"%s\",\"name\":\"%s\",\"area_m2\":%.1f,\"include_in_house\":%s,"
             "\"revision\":%lu,\"groups\":[",
             first_room ? "" : ",", room_id_esc, name_esc, room->total_area_m2,
             room->include_in_house_temperature ? "true" : "false",
             static_cast<unsigned long>(model_.room_revision(room->room_id)));
    first_room = false;
    bool first_group = true;
    for (size_t i = 0; i < loop_count; i++) {
      if (loops[i].binding == nullptr)
        continue;
      char node_esc[48];
      json_escape_(loops[i].binding->node_id, node_esc, sizeof(node_esc));
      const auto *loop_live = loops[i].live;
      const uint8_t members_mask = loop_live != nullptr ? loop_live->group_members_mask : 0;
      const uint8_t zone_1based = static_cast<uint8_t>(loops[i].binding->zone_index + 1);
      if (!appendf_(buffer, capacity, off, "%s{\"node_id\":\"%s\",\"zone\":%u,\"members\":",
                    first_group ? "" : ",", node_esc, static_cast<unsigned>(zone_1based)))
        break;
      if (!append_group_members_json_(buffer, capacity, off, members_mask, zone_1based))
        break;
      if (!appendf_(buffer, capacity, off, "}"))
        break;
      first_group = false;
    }
    appendf_(buffer, capacity, off, "],\"sensor\":");
    bool sensor_written = false;
    for (size_t i = 0; i < loop_count; i++) {
      if (loops[i].binding != nullptr &&
          std::strcmp(loops[i].binding->loop_id, room->primary_loop_id) == 0) {
        char node_esc[48];
        json_escape_(loops[i].binding->node_id, node_esc, sizeof(node_esc));
        appendf_(buffer, capacity, off, "{\"node_id\":\"%s\",\"zone\":%u}", node_esc,
                 static_cast<unsigned>(loops[i].binding->zone_index + 1));
        sensor_written = true;
        break;
      }
    }
    if (!sensor_written)
      appendf_(buffer, capacity, off, "null");
    appendf_(buffer, capacity, off,
             ",\"heat_demand\":{\"recommendation\":\"%s\",\"opening_ratio\":%.2f,"
             "\"saturated_s\":%lu,\"any_fresh\":%s},"
             "\"ble_sensor_mismatch\":%s,\"floor_unset\":%s,\"physics_conflict\":%s,"
             "\"ua_effective_w_per_k\":%.1f}",
             ::lune_touch::heat_recommendation_name(demand.recommendation), demand.opening_ratio,
             static_cast<unsigned long>(demand.saturated_s), demand.any_fresh ? "true" : "false",
             room->ble_sensor_mismatch ? "true" : "false",
             room->floor_unset ? "true" : "false",
             room->physics_conflict ? "true" : "false",
             room->ua_effective_w_per_k);
  }
  appendf_(buffer, capacity, off, "],\"unassigned\":[");
  bool first_unassigned = true;
  for (size_t i = 0; i < model_.zone_count(); i++) {
    const auto *zone = model_.zone(i);
    if (zone == nullptr || !zone->enabled || !zone->unassigned || zone->is_group_secondary)
      continue;
    char node_esc[48];
    char name_esc[80];
    json_escape_(zone->node_id, node_esc, sizeof(node_esc));
    json_escape_(zone->room_name[0] != '\0' ? zone->room_name : zone->loop_id, name_esc,
                 sizeof(name_esc));
    appendf_(buffer, capacity, off, "%s{\"node_id\":\"%s\",\"zone\":%u,\"name\":\"%s\"}",
             first_unassigned ? "" : ",", node_esc, static_cast<unsigned>(zone->zone_index + 1),
             name_esc);
    first_unassigned = false;
  }
  appendf_(buffer, capacity, off, "]}");
}

void LuneTouchCoordinator::write_zones_json(char *buffer, size_t capacity) const {
  size_t off = 0;
  uint8_t day_index = 0;
  uint16_t minute_of_day = 0;
  const bool time_valid = current_schedule_time_(time_, &day_index, &minute_of_day);
  appendf_(buffer, capacity, off, "{\"count\":%u,\"rooms\":[", static_cast<unsigned>(model_.room_count()));
  bool first_room = true;
  for (size_t room_index = 0; room_index < model_.room_count(); room_index++) {
    const auto *room = model_.room(room_index);
    if (room == nullptr || !room->enabled)
      continue;
    // Count loops without a stack-side ResolvedRoomLoop[MAX_HOUSE_ZONES] —
    // write_zones_json runs on the httpd task where stack is tight.
    size_t loop_count = 0;
    for (size_t zi = 0; zi < model_.zone_count(); zi++) {
      const auto *z = model_.zone(zi);
      if (z == nullptr || !z->enabled || !z->commissioned || z->is_group_secondary || z->unassigned)
        continue;
      if (std::strcmp(z->room_id, room->room_id) == 0)
        loop_count++;
    }
    char room_id_esc[48];
    char room_name_esc[80];
    json_escape_(room->room_id, room_id_esc, sizeof(room_id_esc));
    json_escape_(room->room_name, room_name_esc, sizeof(room_name_esc));
    appendf_(buffer, capacity, off, "%s{\"room_id\":\"%s\",\"name\":\"%s\","
             "\"loop_count\":%u,\"area_m2\":%.1f,\"physical_weight\":%.2f,"
             "\"ua_w_per_k\":%.1f,\"delivered_kwh_today\":%.2f,"
             "\"include_in_house_temperature\":%s}",
             first_room ? "" : ",", room_id_esc, room_name_esc,
             static_cast<unsigned>(loop_count), room->total_area_m2, room->physical_weight,
             room->ua_w_per_k > 0.0f ? room->ua_w_per_k : room->total_area_m2,
             room->delivered_kwh_today,
             room->include_in_house_temperature ? "true" : "false");
    first_room = false;
  }
  appendf_(buffer, capacity, off, "],\"zones\":[");
  bool first = true;
  for (size_t i = 0; i < model_.zone_count() && i < ::lune_touch::MAX_HOUSE_ZONES; i++) {
    const auto *zone = model_.zone(i);
    const auto *live = model_.zone_live(i);
    const auto *history = model_.zone_history(i);
    const auto *room = model_.room_by_id(zone != nullptr ? zone->room_id : "");
    if (zone == nullptr)
      continue;
    char temp_buf[16];
    char sp_buf[16];
    char valve_buf[16];
    char hist_avg_buf[16];
    char hist_min_buf[16];
    char hist_max_buf[16];
    char hist_delta_buf[16];
    if (live != nullptr && live->has_temperature) snprintf(temp_buf, sizeof(temp_buf), "%.1f", live->temperature_c); else std::strncpy(temp_buf, "null", sizeof(temp_buf));
    if (live != nullptr && live->has_setpoint) snprintf(sp_buf, sizeof(sp_buf), "%.1f", live->setpoint_c); else std::strncpy(sp_buf, "null", sizeof(sp_buf));
    if (live != nullptr && live->has_valve) snprintf(valve_buf, sizeof(valve_buf), "%.1f", live->valve_pct); else std::strncpy(valve_buf, "null", sizeof(valve_buf));
    if (history != nullptr && history->has_temperature) {
      snprintf(hist_avg_buf, sizeof(hist_avg_buf), "%.2f", history->average_temperature_c);
      snprintf(hist_min_buf, sizeof(hist_min_buf), "%.2f", history->min_temperature_c);
      snprintf(hist_max_buf, sizeof(hist_max_buf), "%.2f", history->max_temperature_c);
    } else {
      std::strncpy(hist_avg_buf, "null", sizeof(hist_avg_buf));
      std::strncpy(hist_min_buf, "null", sizeof(hist_min_buf));
      std::strncpy(hist_max_buf, "null", sizeof(hist_max_buf));
    }
    if (history != nullptr && history->has_delta)
      snprintf(hist_delta_buf, sizeof(hist_delta_buf), "%.2f", history->last_delta_c_per_h);
    else
      std::strncpy(hist_delta_buf, "null", sizeof(hist_delta_buf));
    temp_buf[sizeof(temp_buf) - 1] = '\0';
    sp_buf[sizeof(sp_buf) - 1] = '\0';
    valve_buf[sizeof(valve_buf) - 1] = '\0';
    hist_avg_buf[sizeof(hist_avg_buf) - 1] = '\0';
    hist_min_buf[sizeof(hist_min_buf) - 1] = '\0';
    hist_max_buf[sizeof(hist_max_buf) - 1] = '\0';
    hist_delta_buf[sizeof(hist_delta_buf) - 1] = '\0';
    const char *status = live != nullptr && live->status[0] != '\0' ? live->status : (zone->enabled ? "unknown" : "unused");
    char room_id_esc[48];
    char room_name_esc[80];
    char status_esc[32];
    json_escape_(zone->room_id, room_id_esc, sizeof(room_id_esc));
    json_escape_(zone->room_name, room_name_esc, sizeof(room_name_esc));
    json_escape_(status, status_esc, sizeof(status_esc));
    const auto effective = ::lune_touch::HouseModel::effective_comfort(*zone, time_valid,
                                                                       day_index, minute_of_day);
    const uint32_t now_ms = esphome::millis();
    const auto command_resolution =
        ledger_.resolve_command_offset(zone->node_index, zone->zone_index, now_ms);
    const float learned_offset_c =
        ::lune_touch::HouseModel::learned_comfort_offset_c(*zone, live, effective.setpoint_c);
    ::lune_touch::TargetResolverInput target_input{};
    target_input.fallback_base_target_c = zone->comfort_setpoint_c;
    target_input.touch_target_c = effective.setpoint_c;
    target_input.command = command_resolution;
    target_input.learned_modifier_c = learned_offset_c;
    target_input.learned_confident = learned_offset_c > 0.0f;
    const auto target = ::lune_touch::HouseModel::resolve_target(target_input);
    const float resolved_target_c = target.dispatch_target_c;
    const float thermal_confidence = zone->thermal_samples >= 24 ? 1.0f :
        static_cast<float>(zone->thermal_samples) / 24.0f;
    const uint8_t learned_thermal_lead_h = ::lune_touch::HouseModel::learned_thermal_lead_h(*zone);
    const uint8_t active_thermal_lead_h = ::lune_touch::HouseModel::active_thermal_lead_h(*zone);
    if (!appendf_(buffer, capacity, off,
                  "%s{\"room_id\":\"%s\",\"name\":\"%s\",\"name_source\":\"%s\","
                  "\"node_index\":%u,\"zone_index\":%u,"
                  "\"room\":{\"revision\":%lu,\"total_area_m2\":%.2f,\"physical_weight\":%.2f,"
                  "\"ua_w_per_k\":%.1f,\"ua_prior_w_per_k\":%.1f,\"ua_learned_w_per_k\":%.1f,"
                  "\"ua_effective_w_per_k\":%.1f,\"ua_confidence\":%.2f,\"ua_observed_days\":%u,"
                  "\"delivered_kwh_today\":%.2f,\"include_in_house_temperature\":%s,"
                  "\"wind_exposure\":%.2f,\"solar_gain\":%.2f,\"weather_seeded\":%s,"
                  "\"exterior_walls_union\":%u,\"floor_unset\":%s,\"physics_conflict\":%s,"
                  "\"ble_sensor_mismatch\":%s,\"ownership\":{\"walls\":\"v6\",\"area\":\"v6\","
                  "\"floor\":\"v6\",\"weather\":\"touch\"}},"
                  "\"temperature_c\":%s,\"setpoint_c\":%s,\"status\":\"%s\",\"fresh\":%s,"
                  "\"valve_pct\":%s,\"updated_at_ms\":%lu,\"comfort\":{\"setpoint_c\":%.1f,"
                  "\"bias_c\":%.1f,\"effective_setpoint_c\":%.1f,\"effective_source\":\"%s\","
                  "\"schedule_active\":%s,\"time_valid\":%s,\"priority\":%u},"
                  "\"resolver\":{\"fallback_base_target_c\":%.1f,\"touch_available\":%s,"
                  "\"base_setpoint_c\":%.1f,\"base_source\":\"%s\","
                  "\"manual_offset_c\":%.2f,\"forecast_offset_c\":%.2f,"
                  "\"learned_offset_c\":%.2f,\"command_offset_c\":%.2f,"
                  "\"command_source\":\"%s\",\"pre_v6_target_c\":%.1f,"
                  "\"target_setpoint_c\":%.1f},"
                  "\"schedule\":{\"enabled\":%s,\"day_mask\":%u,\"start_min\":%u,"
                  "\"end_min\":%u,\"setpoint_c\":%.1f},"
                  "\"history\":{\"samples\":%lu,\"calling_samples\":%lu,"
                  "\"avg_temp_c\":%s,\"min_temp_c\":%s,\"max_temp_c\":%s,"
                  "\"last_delta_c_per_h\":%s},"
                  "\"thermal_model\":{\"samples\":%u,\"heat_gain_c_per_h\":%.3f,"
                  "\"cool_loss_c_per_h\":%.3f,\"confidence\":%.2f},"
                  "\"forecast\":{\"exterior_walls\":%u,"
                  "\"wind_exposure\":%.2f,\"solar_gain\":%.2f,\"thermal_lead_h\":%u,"
                  "\"learned_thermal_lead_h\":%u,\"active_thermal_lead_h\":%u,"
                  "\"max_offset_c\":%.2f,\"preload_gain_scale\":%.2f,\"preload_lead_bias_h\":%d,"
                  "\"ownership\":{\"walls\":\"v6\",\"weather\":\"touch\"}}",
                  first ? "" : ",", room_id_esc, room_name_esc,
                  zone_name_source_name_(zone->name_source),
                  static_cast<unsigned>(zone->node_index), static_cast<unsigned>(zone->zone_index),
                  static_cast<unsigned long>(model_.room_revision(zone->room_id)),
                  room != nullptr ? room->total_area_m2 : 0.0f,
                  room != nullptr ? room->physical_weight : 1.0f,
                  room != nullptr
                      ? (room->ua_effective_w_per_k > 0.0f
                             ? room->ua_effective_w_per_k
                             : (room->ua_w_per_k > 0.0f ? room->ua_w_per_k : room->total_area_m2))
                      : 0.0f,
                  room != nullptr ? room->ua_prior_w_per_k : 0.0f,
                  room != nullptr ? room->ua_learned_w_per_k : 0.0f,
                  room != nullptr ? room->ua_effective_w_per_k : 0.0f,
                  room != nullptr ? room->ua_confidence : 0.0f,
                  static_cast<unsigned>(room != nullptr ? room->ua_observed_days : 0),
                  room != nullptr ? room->delivered_kwh_today : 0.0f,
                  room == nullptr || room->include_in_house_temperature ? "true" : "false",
                  room != nullptr ? room->wind_exposure : zone->wind_exposure,
                  room != nullptr ? room->solar_gain : zone->solar_gain,
                  room != nullptr && room->weather_seeded ? "true" : "false",
                  static_cast<unsigned>(room != nullptr ? room->exterior_walls_union
                                                        : zone->exterior_walls),
                  room != nullptr && room->floor_unset ? "true" : "false",
                  room != nullptr && room->physics_conflict ? "true" : "false",
                  room != nullptr && room->ble_sensor_mismatch ? "true" : "false",
                  temp_buf, sp_buf, status_esc, live != nullptr && live->fresh && zone->enabled ? "true" : "false",
                  valve_buf,
                  static_cast<unsigned long>(live != nullptr ? live->updated_at_ms : 0),
                  zone->comfort_setpoint_c, zone->comfort_bias_c,
                  effective.setpoint_c,
                  effective.source,
                  effective.schedule_active ? "true" : "false",
                  effective.time_valid ? "true" : "false",
                  static_cast<unsigned>(zone->priority),
                  target.fallback_base_target_c, target.touch_available ? "true" : "false",
                  target.base_target_c, target.base_source,
                  target.manual_modifier_c, target.distribution_modifier_c,
                  target.learned_modifier_c,
                  target.manual_modifier_c + target.distribution_modifier_c,
                  target.modifier_source, target.pre_v6_target_c,
                  resolved_target_c,
                  zone->schedule_enabled ? "true" : "false",
                  static_cast<unsigned>(zone->schedule_day_mask),
                  static_cast<unsigned>(zone->schedule_start_min),
                  static_cast<unsigned>(zone->schedule_end_min),
                  zone->schedule_setpoint_c,
                  static_cast<unsigned long>(history != nullptr ? history->samples : 0),
                  static_cast<unsigned long>(history != nullptr ? history->calling_samples : 0),
                  hist_avg_buf, hist_min_buf, hist_max_buf, hist_delta_buf,
                  static_cast<unsigned>(zone->thermal_samples),
                  zone->learned_heat_gain_c_per_h,
                  zone->learned_cool_loss_c_per_h,
                  thermal_confidence,
                  static_cast<unsigned>(room != nullptr && room->exterior_walls_union != 0
                                            ? room->exterior_walls_union
                                            : zone->exterior_walls),
                  room != nullptr ? room->wind_exposure : zone->wind_exposure,
                  room != nullptr ? room->solar_gain : zone->solar_gain,
                  static_cast<unsigned>(zone->thermal_lead_h),
                  static_cast<unsigned>(learned_thermal_lead_h),
                  static_cast<unsigned>(active_thermal_lead_h),
                  zone->max_offset_c,
                  preload_gain_scale_[i] >= 0.5f ? preload_gain_scale_[i] : 1.0f,
                  static_cast<int>(preload_lead_bias_h_[i])))
      break;
    // V6 sync-group membership (merged loops share one logical room on V6).
    {
      const uint8_t zone_1based = static_cast<uint8_t>(zone->zone_index + 1);
      const uint8_t members_mask = live != nullptr ? live->group_members_mask : 0;
      const uint8_t primary_zone = live != nullptr && live->group_primary_zone >= 1
                                       ? live->group_primary_zone
                                       : zone_1based;
      const bool is_secondary = zone->is_group_secondary;
      if (!appendf_(buffer, capacity, off,
                    ",\"unassigned\":%s,\"is_group_secondary\":%s,"
                    "\"group_primary_zone\":%u,\"group_members\":",
                    zone->unassigned ? "true" : "false",
                    is_secondary ? "true" : "false",
                    static_cast<unsigned>(primary_zone)))
        break;
      if (!append_group_members_json_(buffer, capacity, off, members_mask, zone_1based))
        break;
    }
    // Intentionally omit comfort_chart series here: full horizon arrays for every
    // zone overflow the dashboard response envelope (HTTP 507). Clients
    // fetch GET /zones/{room_id}/comfort-chart for the open zone only.
    if (!appendf_(buffer, capacity, off, "}"))
      break;
    first = false;
  }
  appendf_(buffer, capacity, off, "]}");
}

void LuneTouchCoordinator::append_zone_comfort_chart_json_(char *buffer, size_t capacity, size_t &off,
                                                            size_t zone_index) const {
  if (buffer == nullptr || capacity == 0 || zone_index >= model_.zone_count()) {
    appendf_(buffer, capacity, off,
             "\"comfort_chart\":{\"hours\":0,\"expected_temp_c\":[],\"scheduled_setpoint_c\":[],"
             "\"comfort_min_c\":[],\"comfort_max_c\":[],\"preload_active\":[],\"actual_temp_c\":null}");
    return;
  }
  const auto *zone = model_.zone(zone_index);
  const auto *live = model_.zone_live(zone_index);
  if (zone == nullptr || forecast_hours_count_ == 0) {
    appendf_(buffer, capacity, off,
             "\"comfort_chart\":{\"hours\":0,\"expected_temp_c\":[],\"scheduled_setpoint_c\":[],"
             "\"comfort_min_c\":[],\"comfort_max_c\":[],\"preload_active\":[],\"actual_temp_c\":null}");
    return;
  }

  int64_t timestamps[72]{};
  for (uint8_t i = 0; i < forecast_hours_count_ && i < 72; i++)
    timestamps[i] = forecast_hours_[i].timestamp_s;
  const size_t now_index = lune_touch_forecast_timeline::first_index_at_or_after(
      timestamps, forecast_hours_count_, current_epoch_s_());
  if (now_index == lune_touch_forecast_timeline::NO_INDEX) {
    appendf_(buffer, capacity, off,
             "\"comfort_chart\":{\"hours\":0,\"expected_temp_c\":[],\"scheduled_setpoint_c\":[],"
             "\"comfort_min_c\":[],\"comfort_max_c\":[],\"preload_active\":[],\"actual_temp_c\":null}");
    return;
  }

  uint8_t day_index = 0;
  uint16_t minute_of_day = 0;
  const bool time_valid = current_schedule_time_(time_, &day_index, &minute_of_day);
  const auto effective =
      ::lune_touch::HouseModel::effective_comfort(*zone, time_valid, day_index, minute_of_day);

  hv6fc::ForecastHour model_hours[72]{};
  const size_t hour_count = std::min<size_t>(forecast_hours_count_, 72);
  for (size_t i = 0; i < hour_count; i++) {
    model_hours[i].temp_c = forecast_hours_[i].temp_c;
    model_hours[i].wind_speed_ms = forecast_hours_[i].wind_speed_ms;
    model_hours[i].wind_dir_deg = forecast_hours_[i].wind_dir_deg;
    model_hours[i].shortwave_wm2 = forecast_hours_[i].shortwave_wm2;
  }

  const uint8_t house_active_lead = ::lune_touch::HouseModel::active_thermal_lead_h(*zone);
  hv6fc::ZoneExposure exposure{};
  exposure.exterior_walls = zone->exterior_walls;
  exposure.wind_exposure = zone->wind_exposure;
  exposure.solar_gain = zone->solar_gain;
  exposure.thermal_lead_h = house_active_lead;

  hv6fc::PreloadParams params{};
  params.indoor_ref_c = effective.setpoint_c;
  params.load_threshold = LOAD_THRESHOLD;
  params.gain_c_per_load = GAIN_C_PER_LOAD;
  params.max_offset_c = weather_max_boost_c_;
  params.priority_gain =
      1.0f + 0.1f * static_cast<float>(zone->priority > 0 ? zone->priority - 1 : 0);
  params.learned_heat_gain_c_per_h = zone->learned_heat_gain_c_per_h;
  params.learned_cool_loss_c_per_h = zone->learned_cool_loss_c_per_h;
  params.thermal_samples = zone->thermal_samples;
  params.learned_gain_scale =
      zone_index < ::lune_touch::MAX_HOUSE_ZONES && preload_gain_scale_[zone_index] >= 0.5f
          ? preload_gain_scale_[zone_index]
          : 1.0f;
  params.lead_bias_h =
      zone_index < ::lune_touch::MAX_HOUSE_ZONES && preload_lead_bias_h_[zone_index] > 0
          ? preload_lead_bias_h_[zone_index]
          : 0;
  params.scheduled_setpoint_c = effective.setpoint_c;
  params.comfort_band_c = COMFORT_BAND_C;
  params.enable_horizon = true;
  if (live != nullptr && live->fresh && live->has_temperature && std::isfinite(live->temperature_c)) {
    params.has_current_temp = true;
    params.current_temp_c = live->temperature_c;
  }

  const hv6fc::PreloadDecision decision =
      hv6fc::compute_zone_preload(model_hours, hour_count, now_index, exposure, params);
  hv6fc::PreloadSeries series{};
  hv6fc::fill_preload_series(model_hours, hour_count, now_index, exposure, params, decision, &series);

  appendf_(buffer, capacity, off, "\"comfort_chart\":{\"hours\":%u,\"offset_c\":%.2f,"
                                  "\"peak_in_h\":%d,\"preload_start_h\":%u,\"preload_end_h\":%u,"
                                  "\"horizon_ok\":%s,\"expected_temp_c\":[",
           static_cast<unsigned>(series.count), decision.offset_c, static_cast<int>(decision.peak_in_h),
           static_cast<unsigned>(decision.preload_start_h),
           static_cast<unsigned>(decision.preload_end_h), decision.horizon_ok ? "true" : "false");
  for (uint8_t i = 0; i < series.count; i++)
    appendf_(buffer, capacity, off, "%s%.2f", i ? "," : "", series.expected_temp_c[i]);
  appendf_(buffer, capacity, off, "],\"scheduled_setpoint_c\":[");
  for (uint8_t i = 0; i < series.count; i++)
    appendf_(buffer, capacity, off, "%s%.2f", i ? "," : "", series.scheduled_setpoint_c[i]);
  appendf_(buffer, capacity, off, "],\"comfort_min_c\":[");
  for (uint8_t i = 0; i < series.count; i++)
    appendf_(buffer, capacity, off, "%s%.2f", i ? "," : "", series.comfort_min_c[i]);
  appendf_(buffer, capacity, off, "],\"comfort_max_c\":[");
  for (uint8_t i = 0; i < series.count; i++)
    appendf_(buffer, capacity, off, "%s%.2f", i ? "," : "", series.comfort_max_c[i]);
  appendf_(buffer, capacity, off, "],\"preload_active\":[");
  for (uint8_t i = 0; i < series.count; i++)
    appendf_(buffer, capacity, off, "%s%s", i ? "," : "", series.preload_active[i] ? "true" : "false");
  if (live != nullptr && live->has_temperature && std::isfinite(live->temperature_c))
    appendf_(buffer, capacity, off, "],\"actual_temp_c\":%.2f}", live->temperature_c);
  else
    appendf_(buffer, capacity, off, "],\"actual_temp_c\":null}");
}

void LuneTouchCoordinator::write_zone_comfort_chart_json(const char *room_id, char *buffer,
                                                          size_t capacity) const {
  if (buffer == nullptr || capacity == 0)
    return;
  size_t off = 0;
  size_t zone_index = ::lune_touch::MAX_HOUSE_ZONES;
  char room_esc[48];
  json_escape_(room_id != nullptr ? room_id : "", room_esc, sizeof(room_esc));
  if (room_id != nullptr && room_id[0] != '\0') {
    for (size_t i = 0; i < model_.zone_count(); i++) {
      const auto *zone = model_.zone(i);
      if (zone != nullptr && std::strcmp(zone->room_id, room_id) == 0) {
        zone_index = i;
        break;
      }
    }
  }
  appendf_(buffer, capacity, off, "{\"room_id\":\"%s\",", room_esc);
  append_zone_comfort_chart_json_(buffer, capacity, off, zone_index);
  appendf_(buffer, capacity, off, "}");
}

void LuneTouchCoordinator::write_strategy_json(char *buffer, size_t capacity) const {
  if (buffer == nullptr || capacity == 0)
    return;
  uint8_t day_index = 0;
  uint16_t minute_of_day = 0;
  const bool schedule_time_valid = current_schedule_time_(time_, &day_index, &minute_of_day);
  const auto strategy = model_.strategy_snapshot(schedule_time_valid, day_index, minute_of_day);
  uint8_t schedule_active = 0;
  uint8_t schedule_driver_priority = 0;
  float schedule_driver_setpoint = 0.0f;
  char schedule_driver_room_id_raw[32]{};
  char schedule_driver_room_name_raw[48]{};
  if (schedule_time_valid) {
    for (size_t i = 0; i < model_.zone_count(); i++) {
      const auto *zone = model_.zone(i);
      if (zone == nullptr)
        continue;
      const auto effective = ::lune_touch::HouseModel::effective_comfort(*zone, true,
                                                                         day_index, minute_of_day);
      if (!effective.schedule_active)
        continue;
      schedule_active++;
      if (zone->priority >= schedule_driver_priority) {
        schedule_driver_priority = zone->priority;
        schedule_driver_setpoint = effective.setpoint_c;
        std::strncpy(schedule_driver_room_id_raw, zone->room_id,
                     sizeof(schedule_driver_room_id_raw) - 1);
        std::strncpy(schedule_driver_room_name_raw, zone->room_name,
                     sizeof(schedule_driver_room_name_raw) - 1);
      }
    }
  }
  char driver_room_id[64];
  char driver_room_name[96];
  char schedule_driver_room_id[64];
  char schedule_driver_room_name[96];
  char asgard_mode[32];
  char physical_temperature[16];
  char comfort_average[16];
  char comfort_demand[16];
  char driver_deficit[16];
  char schedule_setpoint[16];
  char house_target[16];
  char weighted_temperature[16];
  char index_room_id[64];
  char index_room_name[96];
  json_escape_(strategy.driver_room_id, driver_room_id, sizeof(driver_room_id));
  json_escape_(strategy.driver_room_name, driver_room_name, sizeof(driver_room_name));
  json_escape_(schedule_driver_room_id_raw, schedule_driver_room_id, sizeof(schedule_driver_room_id));
  json_escape_(schedule_driver_room_name_raw, schedule_driver_room_name, sizeof(schedule_driver_room_name));
  json_escape_(asgard_mode_, asgard_mode, sizeof(asgard_mode));
  json_escape_(strategy.index_room_id, index_room_id, sizeof(index_room_id));
  json_escape_(strategy.index_room_name, index_room_name, sizeof(index_room_name));
  json_float_token_(physical_temperature, sizeof(physical_temperature), strategy.physical_temperature_c, 2);
  json_float_token_(comfort_average, sizeof(comfort_average), strategy.comfort_average_c, 2);
  json_float_token_(comfort_demand, sizeof(comfort_demand), strategy.comfort_demand_c, 2);
  json_float_token_(driver_deficit, sizeof(driver_deficit), strategy.driver_deficit_c, 2);
  json_float_token_(schedule_setpoint, sizeof(schedule_setpoint), schedule_driver_setpoint, 1);
  json_float_token_(house_target, sizeof(house_target), strategy.house_target_c, 2);
  json_float_token_(weighted_temperature, sizeof(weighted_temperature), strategy.physical_temperature_c, 2);

  char implausible_esc[96];
  json_escape_(strategy.implausible_room_name, implausible_esc, sizeof(implausible_esc));
  snprintf(buffer, capacity,
           "{\"physical\":{\"has_temperature\":%s,\"temperature_c\":%s,"
           "\"contributing_rooms\":%u,\"missing_rooms\":%u,\"contributing_area_m2\":%.1f,\"missing_area_m2\":%.1f,\"coverage_ratio\":%.3f,\"quality\":\"%s\",\"expected_manifolds\":%u,\"contributing_manifolds\":%u,"
           "\"room_temp_spread_c\":%.2f,\"spread_healthy\":%s},"
           "\"weighting\":{\"basis\":\"%s\",\"ua_total_w_per_k\":%.1f,\"thermal_mass_total_kwh_per_k\":%.2f,"
           "\"hl_tm_product_h\":%.2f,\"house_tau_h\":%.2f,\"passive_solar_gain_factor\":%.3f,"
           "\"weights_revision\":%lu,\"weights_updated_at_ms\":%lu},"
           "\"comfort\":{\"average_c\":%s,"
           "\"demand_c\":%s,\"demand_zones\":%u},\"driver\":{\"room_id\":\"%s\","
           "\"name\":\"%s\",\"deficit_c\":%s,\"priority\":%u},"
           "\"schedule\":{\"time_valid\":%s,\"active_zones\":%u,"
           "\"driver_room_id\":\"%s\",\"driver_name\":\"%s\","
           "\"driver_setpoint_c\":%s,\"driver_priority\":%u},"
           "\"house_target\":{\"available\":%s,\"value_c\":%s,\"source\":\"%s\",\"contributing_area_m2\":%.1f},"
           "\"physical_house_temperature_c\":%s,\"house_comfort_target_c\":%s,"
           "\"quality\":{\"status\":\"%s\",\"contributing_room_count\":%u,\"contributing_area_m2\":%.1f,"
           "\"missing_area_m2\":%.1f,\"excluded_area_m2\":%.1f,\"coverage_ratio\":%.3f,"
           "\"expected_manifolds\":%u,\"contributing_manifolds\":%u,"
           "\"spread_c\":%.1f,\"spread_warn\":%s,\"implausible_rooms\":%u,\"implausible_room\":\"%s\"},"
           "\"weighted_temperature\":{\"source\":\"%s\",\"available\":%s,\"value_c\":%s,\"contributing_rooms\":%u,\"deprecated\":true},"
           "\"heat_source\":{\"enabled\":%s,\"mode\":\"%s\"},"
           "\"asgard_odin\":{\"enabled\":%s,\"physical_signal\":\"ua_or_area_weighted_house_temp\","
           "\"comfort_signal\":\"separate_weighted_demand\",\"mode\":\"%s\"},"
           "\"ua_calibration\":{\"status\":\"%s\",\"reason\":\"%s\","
           "\"odin_heat_loss_w_per_k\":%.1f,\"odin_tau_h\":%.1f,\"ua_prior_sum_w_per_k\":%.1f,"
           "\"u_base\":%.3f,\"u_wall\":%.3f,\"c_struct\":%.3f,\"calibrated\":%s},"
           "\"index_room\":{\"room_id\":\"%s\",\"name\":\"%s\",\"flow_req_c\":%.1f,\"reason\":\"%s\"}}",
           strategy.has_physical_temperature ? "true" : "false",
           physical_temperature,
           static_cast<unsigned>(strategy.contributing_rooms),
           static_cast<unsigned>(strategy.missing_rooms),
           strategy.contributing_area_m2,
           strategy.missing_area_m2,
           strategy.coverage_ratio,
           strategy.quality,
           static_cast<unsigned>(strategy.expected_manifolds),
           static_cast<unsigned>(strategy.contributing_manifolds),
           strategy.room_temp_spread_c,
           strategy.spread_healthy ? "true" : "false",
           strategy.weighting_basis,
           strategy.ua_total_w_per_k,
           strategy.thermal_mass_total_kwh_per_k,
           strategy.hl_tm_product,
           strategy.hl_tm_product,
           strategy.passive_solar_gain_factor,
           static_cast<unsigned long>(strategy.weights_revision),
           static_cast<unsigned long>(strategy.weights_updated_at_ms),
           comfort_average,
           comfort_demand,
           static_cast<unsigned>(strategy.demand_zones),
           driver_room_id,
           driver_room_name,
           driver_deficit,
           static_cast<unsigned>(strategy.driver_priority),
           schedule_time_valid ? "true" : "false",
           static_cast<unsigned>(schedule_active),
           schedule_driver_room_id,
           schedule_driver_room_name,
           schedule_setpoint,
           static_cast<unsigned>(schedule_driver_priority),
           strategy.has_house_target ? "true" : "false",
           house_target,
           strategy.house_target_source,
           strategy.target_contributing_area_m2,
           strategy.has_physical_temperature ? physical_temperature : "null",
           strategy.has_house_target ? house_target : "null",
           strategy.quality,
           static_cast<unsigned>(strategy.contributing_rooms),
           strategy.contributing_area_m2,
           strategy.missing_area_m2,
           strategy.excluded_area_m2,
           strategy.coverage_ratio,
           static_cast<unsigned>(strategy.expected_manifolds),
           static_cast<unsigned>(strategy.contributing_manifolds),
           static_cast<double>(strategy.room_temp_spread_c), strategy.spread_healthy ? "false" : "true",
           static_cast<unsigned>(strategy.implausible_rooms), implausible_esc,
           strategy.weighting_basis[0] == 'u' ? "physical_ua_weighted" : "physical_area_weighted",
           strategy.has_physical_temperature ? "true" : "false",
           weighted_temperature,
           static_cast<unsigned>(strategy.contributing_rooms),
           heat_source_.enabled ? "true" : "false",
           heat_source_.enabled ? "active" : "disabled",
           odin_plan_.enabled ? "true" : "false",
           asgard_mode,
           strategy.ua_calibration_status,
           strategy.ua_calibration_reason,
           strategy.odin_heat_loss_w_per_k,
           strategy.odin_tau_h,
           strategy.ua_prior_sum_w_per_k,
           strategy.house_u_base,
           strategy.house_u_wall,
           strategy.house_c_struct,
           strategy.house_calibrated ? "true" : "false",
           index_room_id,
           index_room_name,
           strategy.index_flow_req_c,
           strategy.index_reason);
}

void LuneTouchCoordinator::write_settings_json(char *buffer, size_t capacity) const {
  char name[64];
  char install_id[64];
  char site[96];
  char mode[32];
  char authority_leader[64];
  char authority_coordinator[64];
  char authority_state[48];
  char authority_reason[96];
  char authority_peer_status[32];
  char authority_last_fallback[16];
  char authority_last_asgard[16];
  char weather_max_boost[16];
  char control_mode[24];
  char control_mode_eff[24];
  json_escape_(coordinator_name_, name, sizeof(name));
  json_escape_(install_id_, install_id, sizeof(install_id));
  json_escape_(site_label_, site, sizeof(site));
  json_escape_(install_mode_, mode, sizeof(mode));
  json_escape_(authority_leader_node_id_, authority_leader, sizeof(authority_leader));
  json_escape_(authority_coordinator_id_, authority_coordinator, sizeof(authority_coordinator));
  json_escape_(authority_state_, authority_state, sizeof(authority_state));
  json_escape_(authority_reason_, authority_reason, sizeof(authority_reason));
  json_escape_(authority_v6_peer_status_, authority_peer_status, sizeof(authority_peer_status));
  json_escape_(v6_control_mode_, control_mode, sizeof(control_mode));
  json_escape_(v6_control_mode_effective_, control_mode_eff, sizeof(control_mode_eff));
  json_float_token_(authority_last_fallback, sizeof(authority_last_fallback), authority_last_fallback_value_c_, 2);
  json_float_token_(authority_last_asgard, sizeof(authority_last_asgard), authority_last_asgard_value_c_, 2);
  json_float_token_(weather_max_boost, sizeof(weather_max_boost), weather_max_boost_c_, 1);
  snprintf(buffer, capacity,
           "{\"coordinator\":{\"name\":\"%s\",\"install_id\":\"%s\","
           "\"site_label\":\"%s\",\"install_mode\":\"%s\"},"
           "\"authority\":{\"leader_node_id\":\"%s\",\"coordinator_id\":\"%s\","
           "\"authentication_configured\":%s,\"state\":\"%s\",\"reason\":\"%s\","
           "\"generation\":%lu,\"lease_remaining_s\":%lu,"
           "\"control_mode\":\"%s\",\"control_mode_effective\":\"%s\",\"v6_sync\":{"
           "\"last_fallback_value_c\":%s,\"last_asgard_value_c\":%s,"
           "\"local_zones\":%u,\"peer_zones\":%u,\"peer_status\":\"%s\"}},"
           "\"weather\":{\"max_boost_c\":%s},\"display\":{\"idle_timeout_s\":%u}}",
           name, install_id, site, mode, authority_leader, authority_coordinator,
           authority_shared_key_[0] != '\0' ? "true" : "false", authority_state, authority_reason,
           static_cast<unsigned long>(authority_generation_),
           authority_expires_at_ms_ == 0 ? 0UL : static_cast<unsigned long>((authority_expires_at_ms_ - esphome::millis()) / 1000UL),
           control_mode, control_mode_eff,
           authority_last_fallback, authority_last_asgard,
           static_cast<unsigned>(authority_v6_local_zones_), static_cast<unsigned>(authority_v6_peer_zones_), authority_peer_status,
           weather_max_boost, static_cast<unsigned>(display_idle_timeout_s_));
}

void LuneTouchCoordinator::write_heat_source_json(char *buffer, size_t capacity) const {
  // Large snapshots live off the httpd stack (single dashboard request task),
  // in PSRAM rather than internal .bss.
  static HeatSourceState *source_p = psram_scratch_<HeatSourceState>(1);
  static CirculationPumpState *circulation_p = psram_scratch_<CirculationPumpState>(1);
  static ::lune_touch::StrategySnapshot *strategy_p = psram_scratch_<::lune_touch::StrategySnapshot>(1);
  if (source_p == nullptr || circulation_p == nullptr || strategy_p == nullptr) {
    std::snprintf(buffer, capacity, "{}");
    return;
  }
  HeatSourceState &source = *source_p;
  CirculationPumpState &circulation = *circulation_p;
  ::lune_touch::StrategySnapshot &strategy = *strategy_p;
  source = HeatSourceState{};
  circulation = CirculationPumpState{};
  strategy = ::lune_touch::StrategySnapshot{};
  char trim_mode_raw[16]{"shadow"};
  char control_mode_raw[16]{"local"};
  char control_mode_eff_raw[16]{"local"};
  bool odin_plan_enabled_flag = false;
  bool absorb_arm_enabled_flag = false;
  char plan_source_mode_raw[8]{"auto"};
  char odin_host_raw[64]{};
  uint16_t odin_port_raw = 80;
  if (take_state_lock_(50)) {
    source = heat_source_;
    circulation = circulation_;
    strategy = model_.strategy_snapshot();
    odin_plan_enabled_flag = odin_plan_.enabled;
    absorb_arm_enabled_flag = odin_plan_.absorb_arm_enabled;
    std::strncpy(plan_source_mode_raw, odin_plan_.plan_source_mode, sizeof(plan_source_mode_raw) - 1);
    std::strncpy(odin_host_raw, odin_plan_.odin_host, sizeof(odin_host_raw) - 1);
    odin_port_raw = odin_plan_.odin_port;
    std::strncpy(trim_mode_raw, flow_trim::mode_name(flow_trim_.mode), sizeof(trim_mode_raw) - 1);
    std::strncpy(control_mode_raw, v6_control_mode_, sizeof(control_mode_raw) - 1);
    std::strncpy(control_mode_eff_raw, v6_control_mode_effective_, sizeof(control_mode_eff_raw) - 1);
    give_state_lock_();
  }
  char host[128];
  char type[48];
  char variable[96];
  char write_url[256];
  char read_url[256];
  char error[192];
  char target_blocker[128];
  char operating_state_blocker[128];
  char weighted_temperature[16];
  char preview_setpoint[16];
  char requested_value[16];
  char confirmed_value[16];
  json_escape_(source.host, host, sizeof(host));
  json_escape_(source.type, type, sizeof(type));
  json_escape_(source.weighted_temperature_variable, variable, sizeof(variable));
  json_escape_(source.write_url_template, write_url, sizeof(write_url));
  json_escape_(source.read_url_template, read_url, sizeof(read_url));
  json_escape_(source.last_error, error, sizeof(error));
  const asgard_adapter::Config adapter_config = make_asgard_config_(source);
  const asgard_adapter::Compatibility adapter_compatibility =
      asgard_adapter::compatibility(adapter_config);
  json_escape_(adapter_compatibility.target_blocker, target_blocker, sizeof(target_blocker));
  json_escape_(adapter_compatibility.operating_state_blocker, operating_state_blocker,
               sizeof(operating_state_blocker));
  json_float_token_(weighted_temperature, sizeof(weighted_temperature), strategy.physical_temperature_c, 2);
  char preview_temperature[16];
  json_float_token_(preview_temperature, sizeof(preview_temperature),
                    strategy.temperature_preview_c, 2);
  // Prefer the setpoint currently reported by V6. This is the useful value
  // during commissioning, when Touch's local room comfort fields may still
  // contain their default/clamped value. Once live data is unavailable, use
  // the area-weighted Touch target or the configured comfort average.
  const bool has_preview_setpoint = strategy.has_setpoint_preview || strategy.has_house_target ||
                                    (std::isfinite(strategy.comfort_average_c) &&
                                     strategy.comfort_average_c >= 5.0f &&
                                     strategy.comfort_average_c <= 35.0f);
  const float preview_setpoint_c = strategy.has_setpoint_preview
                                       ? strategy.setpoint_preview_c
                                       : strategy.has_house_target
                                             ? strategy.house_target_c
                                             : strategy.comfort_average_c;
  json_float_token_(preview_setpoint, sizeof(preview_setpoint), preview_setpoint_c, 2);
  json_float_token_(requested_value, sizeof(requested_value), source.last_requested_value_c, 2);
  json_float_token_(confirmed_value, sizeof(confirmed_value), source.last_confirmed_value_c, 2);
  char declared_target[16];
  char house_target_tok[16];
  char drift_tok[16];
  char climate_entity[96];
  char bias_entity[96];
  char dhw_entity[96];
  char legio_entity[96];
  char defrost_entity[96];
  char trim_mode[16];
  char trim_reason[48];
  char trim_bias[16];
  char trim_target[16];
  char bias_confirmed[16];
  char control_mode[24];
  char control_mode_eff[24];
  char target_written[16];
  char target_confirmed[16];
  json_float_token_(declared_target, sizeof(declared_target),
                    source.has_declared_target ? source.declared_target_c : NAN, 2);
  json_float_token_(target_written, sizeof(target_written), source.last_target_written_c, 2);
  json_float_token_(target_confirmed, sizeof(target_confirmed), source.last_target_confirmed_c, 2);
  json_float_token_(house_target_tok, sizeof(house_target_tok),
                    strategy.has_house_target ? strategy.house_target_c : NAN, 2);
  float drift = NAN;
  bool drift_warn = false;
  if (source.has_declared_target && strategy.has_house_target &&
      std::isfinite(source.declared_target_c) && std::isfinite(strategy.house_target_c)) {
    drift = strategy.house_target_c - source.declared_target_c;
    drift_warn = std::fabs(drift) > 0.5f;
  }
  json_float_token_(drift_tok, sizeof(drift_tok), drift, 2);
  json_escape_(source.climate_entity, climate_entity, sizeof(climate_entity));
  json_escape_(source.bias_entity, bias_entity, sizeof(bias_entity));
  json_escape_(source.dhw_entity, dhw_entity, sizeof(dhw_entity));
  json_escape_(source.legionella_entity, legio_entity, sizeof(legio_entity));
  json_escape_(source.defrost_entity, defrost_entity, sizeof(defrost_entity));
  json_escape_(trim_mode_raw, trim_mode, sizeof(trim_mode));
  json_escape_(source.trim_freeze_reason, trim_reason, sizeof(trim_reason));
  json_float_token_(trim_bias, sizeof(trim_bias), source.trim_bias_c, 2);
  json_float_token_(trim_target, sizeof(trim_target), source.trim_target_bias_c, 2);
  json_float_token_(bias_confirmed, sizeof(bias_confirmed), source.last_bias_confirmed_c, 2);
  json_escape_(control_mode_raw, control_mode, sizeof(control_mode));
  json_escape_(control_mode_eff_raw, control_mode_eff, sizeof(control_mode_eff));
  char plan_source_esc[16];
  char odin_host_esc[128];
  json_escape_(plan_source_mode_raw, plan_source_esc, sizeof(plan_source_esc));
  json_escape_(odin_host_raw, odin_host_esc, sizeof(odin_host_esc));
  const uint32_t now = esphome::millis();
  const uint32_t write_age_s = source.last_write_ms == 0 ? 0 : (now - source.last_write_ms) / 1000UL;
  const uint32_t confirmation_age_s = source.last_confirmed_ms == 0 ? 0 :
                                      (now - source.last_confirmed_ms) / 1000UL;
  const uint32_t target_write_age_s = source.last_target_write_ms == 0 ? 0 :
                                      (now - source.last_target_write_ms) / 1000UL;
  const bool hp_ok = source.hp_telemetry_ms != 0 && now - source.hp_telemetry_ms < 300000UL;
  const unsigned long hp_age_s = source.hp_telemetry_ms == 0 ? 0UL : (now - source.hp_telemetry_ms) / 1000UL;
  char hp_feed[16], hp_ret[16], hp_out[16], hp_target[16], hp_hz[16];
  json_float_token_(hp_feed, sizeof(hp_feed), source.hp_feed_c, 1);
  json_float_token_(hp_ret, sizeof(hp_ret), source.hp_return_c, 1);
  json_float_token_(hp_out, sizeof(hp_out), source.hp_outside_c, 1);
  json_float_token_(hp_target, sizeof(hp_target), source.hp_flow_target_c, 1);
  json_float_token_(hp_hz, sizeof(hp_hz), source.hp_compressor_hz, 0);
  // Keep off the httpd stack — this writer runs under the dashboard request task.
  static char circulation_json[1024];
  format_circulation_json_(circulation, now, CIRCULATION_STALE_MS, circulation_json,
                           sizeof(circulation_json));
  snprintf(buffer, capacity,
           "{\"type\":\"%s\",\"enabled\":%s,\"host\":\"%s\",\"port\":%u,"
           "\"weighted_temperature_variable\":\"%s\",\"push_interval_s\":%u,"
           "\"write_url_template\":\"%s\",\"read_url_template\":\"%s\","
           "\"climate_entity\":\"%s\",\"target_sync_enabled\":%s,\"odin_plan_enabled\":%s,\"absorb_arm_enabled\":%s,\"plan_source\":\"%s\",\"odin_host\":\"%s\",\"odin_port\":%u,\"v2_runtime_enabled\":%s,"
           "\"bias_entity\":\"%s\",\"dhw_entity\":\"%s\",\"legionella_entity\":\"%s\","
           "\"defrost_entity\":\"%s\",\"v6_control_mode\":\"%s\",\"v6_control_mode_effective\":\"%s\","
           "\"compatibility\":{\"physical_temperature\":\"%s\",\"target_sync\":\"%s\","
           "\"bias_sync\":\"%s\",\"operating_state\":\"%s\",\"target_blocker\":\"%s\","
           "\"operating_state_blocker\":\"%s\"},"
           "\"declared_target\":{\"available\":%s,\"value_c\":%s},"
           "\"house_target\":{\"available\":%s,\"value_c\":%s},"
           "\"house_comfort_target_c\":%s,\"physical_house_temperature_c\":%s,"
           "\"target_drift\":{\"value_c\":%s,\"warn\":%s},"
           "\"weighted_temperature\":{\"available\":%s,\"value_c\":%s,\"contributing_rooms\":%u,\"deprecated\":true},"
           "\"send_preview\":{\"available\":%s,\"value_c\":%s,\"zones\":%u,"
           "\"target_setpoint_c\":%s,\"target_available\":%s,\"mode\":\"%s\"},"
           "\"push\":{\"has_result\":%s,\"status\":\"%s\",\"alarm\":%s,\"http_status\":%d,\"requested_value_c\":%s,"
           "\"confirmed_value_c\":%s,\"write_age_s\":%u,\"confirmation_age_s\":%u,"
           "\"failure_count\":%u,\"failure_streak\":%u,\"last_error\":\"%s\"},"
           "\"trim\":{\"mode\":\"%s\",\"bias_c\":%s,\"target_bias_c\":%s,\"frozen\":%s,"
           "\"freeze_reason\":\"%s\",\"last_confirmed_bias_c\":%s,\"freeze_inputs_known\":%s,"
           "\"dhw_active\":%s,\"legionella_active\":%s,\"defrost_active\":%s},"
           "\"target_sync\":{\"last_written_c\":%s,\"last_confirmed_c\":%s,\"failure_streak\":%u,\"write_age_s\":%u,\"demand_uplift_c\":%.2f},"
           "\"heat_pump\":{\"available\":%s,\"feed_c\":%s,\"return_c\":%s,\"outside_c\":%s,"
           "\"flow_target_c\":%s,\"compressor_on\":%s,\"compressor_hz\":%s,\"operation_mode\":%d,\"age_s\":%lu},"
           "\"circulation\":%s}",
           type, source.enabled ? "true" : "false", host, static_cast<unsigned>(source.port), variable,
           static_cast<unsigned>(source.push_interval_s), write_url, read_url,
           climate_entity, source.target_sync_enabled ? "true" : "false",
           odin_plan_enabled_flag ? "true" : "false",
           absorb_arm_enabled_flag ? "true" : "false", plan_source_esc, odin_host_esc,
           static_cast<unsigned>(odin_port_raw),
           plan_source::forecast_v2_runtime_enabled() ? "true" : "false",
           bias_entity, dhw_entity, legio_entity, defrost_entity, control_mode, control_mode_eff,
           asgard_adapter::capability_status_name(adapter_compatibility.physical_temperature),
           asgard_adapter::capability_status_name(adapter_compatibility.target_sync),
           asgard_adapter::capability_status_name(adapter_compatibility.bias_sync),
           asgard_adapter::capability_status_name(adapter_compatibility.operating_state), target_blocker,
           operating_state_blocker,
           source.has_declared_target ? "true" : "false", declared_target,
           strategy.has_house_target ? "true" : "false", house_target_tok,
           strategy.has_house_target ? house_target_tok : "null",
           strategy.has_physical_temperature ? weighted_temperature : "null",
           drift_tok, drift_warn ? "true" : "false",
           strategy.has_physical_temperature ? "true" : "false",
           weighted_temperature, static_cast<unsigned>(strategy.contributing_zones),
           strategy.has_temperature_preview ? "true" : "false", preview_temperature,
           static_cast<unsigned>(strategy.preview_zones),
           preview_setpoint, has_preview_setpoint ? "true" : "false",
           source.enabled ? "active" : "disabled_preview",
           source.has_last_push ? "true" : "false", source.last_status,
           source.push_alarm ? "true" : "false", source.last_http_status,
           requested_value, confirmed_value,
           static_cast<unsigned>(write_age_s), static_cast<unsigned>(confirmation_age_s),
           static_cast<unsigned>(source.failure_count), static_cast<unsigned>(source.failure_streak), error,
           trim_mode, trim_bias, trim_target, source.trim_frozen ? "true" : "false",
           trim_reason, bias_confirmed, source.freeze_inputs_known ? "true" : "false",
           source.dhw_active ? "true" : "false", source.legionella_active ? "true" : "false",
           source.defrost_active ? "true" : "false",
           target_written, target_confirmed,
           static_cast<unsigned>(source.target_failure_streak),
           static_cast<unsigned>(target_write_age_s), demand_target_uplift_c_,
           hp_ok ? "true" : "false", hp_feed, hp_ret, hp_out, hp_target,
           source.hp_compressor_on ? "true" : "false", hp_hz, static_cast<int>(source.hp_operation_mode),
           static_cast<unsigned long>(hp_age_s),
           circulation_json);
}

void LuneTouchCoordinator::write_circulation_json(char *buffer, size_t capacity) const {
  CirculationPumpState source;
  if (take_state_lock_(50)) {
    source = circulation_;
    give_state_lock_();
  }
  format_circulation_json_(source, esphome::millis(), CIRCULATION_STALE_MS, buffer, capacity);
}

void LuneTouchCoordinator::write_forecast_json(char *buffer, size_t capacity) const {
  size_t off = 0;
  // ~2.5 KiB: off the httpd stack (PSRAM, single dashboard request task).
  static OdinPlanState *odin_plan_p = psram_scratch_<OdinPlanState>(1);
  if (odin_plan_p == nullptr) {
    std::snprintf(buffer, capacity, "{}");
    return;
  }
  OdinPlanState &odin_plan = *odin_plan_p;
  odin_plan = OdinPlanState{};
  bool odin_active = false;
  if (take_state_lock_(50)) {
    odin_plan = odin_plan_;
    odin_active = odin_plan_active_();
    give_state_lock_();
  }
  char forecast_last_error[128];
  char forecast_location_mode[32];
  char forecast_provider_timezone[64];
  char odin_status[24];
  char odin_error[128];
  json_escape_(forecast_last_error_, forecast_last_error, sizeof(forecast_last_error));
  json_escape_(forecast_location_mode_, forecast_location_mode, sizeof(forecast_location_mode));
  json_escape_(forecast_provider_timezone_, forecast_provider_timezone, sizeof(forecast_provider_timezone));
  json_escape_(odin_plan.status, odin_status, sizeof(odin_status));
  json_escape_(odin_plan.last_error, odin_error, sizeof(odin_error));
  const int64_t now_epoch_s = current_epoch_s_();
  const unsigned long fetch_age_s =
      forecast_fetch_epoch_s_ == 0 || now_epoch_s < forecast_fetch_epoch_s_
          ? 0UL
          : static_cast<unsigned long>(now_epoch_s - forecast_fetch_epoch_s_);
  const unsigned long odin_age_s = odin_plan.last_fetch_ms == 0 ? 0UL :
      static_cast<unsigned long>((esphome::millis() - odin_plan.last_fetch_ms) / 1000UL);
  const bool odin_fresh = odin_active && odin_plan.available && odin_plan.last_fetch_ms != 0 &&
      esphome::millis() - odin_plan.last_fetch_ms <= ODIN_PLAN_STALE_MS;
  int64_t timestamps[72]{};
  for (uint8_t i = 0; i < forecast_hours_count_; i++)
    timestamps[i] = forecast_hours_[i].timestamp_s;
  const size_t decision_start_index = lune_touch_forecast_timeline::first_index_at_or_after(
      timestamps, forecast_hours_count_, now_epoch_s);
  char weather_max_boost[16];
  char odin_target[16];
  char odin_min[16];
  char odin_max[16];
  char odin_price[16];
  char odin_heat[16];
  char forecast_min_temp[16];
  char forecast_max_wind[16];
  char forecast_peak_dir[16];
  char forecast_max_solar[16];
  json_float_token_(weather_max_boost, sizeof(weather_max_boost), weather_max_boost_c_, 1);
  json_float_token_(odin_target, sizeof(odin_target), odin_plan.current_target_c, 2);
  json_float_token_(odin_min, sizeof(odin_min), odin_plan.current_min_c, 2);
  json_float_token_(odin_max, sizeof(odin_max), odin_plan.current_max_c, 2);
  json_float_token_(odin_price, sizeof(odin_price), odin_plan.current_price, 3);
  json_float_token_(odin_heat, sizeof(odin_heat), odin_plan.current_planned_heat_kw, 2);
  char odin_source_esc[48];
  char odin_plan_source_esc[16];
  char odin_version_esc[32];
  char odin_decision_esc[64];
  char odin_arm_reason_esc[96];
  json_escape_(odin_plan.source_id, odin_source_esc, sizeof(odin_source_esc));
  json_escape_(odin_plan.plan_source_mode, odin_plan_source_esc, sizeof(odin_plan_source_esc));
  json_escape_(odin_plan.odin_version, odin_version_esc, sizeof(odin_version_esc));
  json_escape_(odin_plan.current_decision_reason, odin_decision_esc, sizeof(odin_decision_esc));
  json_escape_(odin_plan.absorb_arm_reason, odin_arm_reason_esc, sizeof(odin_arm_reason_esc));
  json_float_token_(forecast_min_temp, sizeof(forecast_min_temp), forecast_min_temp_c_, 1);
  json_float_token_(forecast_max_wind, sizeof(forecast_max_wind), forecast_max_wind_ms_, 1);
  json_float_token_(forecast_peak_dir, sizeof(forecast_peak_dir), forecast_peak_wind_dir_deg_, 0);
  json_float_token_(forecast_max_solar, sizeof(forecast_max_solar), forecast_max_solar_wm2_, 0);
  appendf_(buffer, capacity, off,
           "{\"status\":\"%s\",\"location\":{\"mode\":\"%s\",\"latitude\":%.6f,\"longitude\":%.6f},"
           "\"fetch_pending\":%s,\"last_fetch_age_s\":%lu,"
           "\"weather\":{\"max_boost_c\":%s},"
           "\"odin_plan\":{\"enabled\":%s,\"active\":%s,\"source\":\"%s\",\"plan_source\":\"%s\",\"odin_version\":\"%s\",\"available\":%s,"
           "\"fresh\":%s,\"status\":\"%s\",\"http_status\":%d,\"age_s\":%lu,"
           "\"current_hour\":%u,\"current_index\":%u,\"target_c\":%s,\"min_c\":%s,\"max_c\":%s,"
           "\"price\":%s,\"planned_heat_kw\":%s,\"operation_mode_raw\":%d,"
           "\"heat_window_active\":%s,\"absorb_arm_enabled\":%s,\"preload_timing_bias_enabled\":%s,"
           "\"preload_bias_offset_cap_c\":%.2f,\"bias_window_planned_kwh\":%.2f,"
           "\"bias_window_delivered_kwh\":%.2f,\"today_start_index\":%u,\"idx_now\":%u,"
           "\"plan_stale\":%s,\"decision_reason\":\"%s\",\"absorb_armed\":%s,\"absorb_arm_reason\":\"%s\","
           "\"arm_energy_cost_modulation\":%s,\"v2_runtime_enabled\":%s,"
           "\"applies_valve_commands\":false,\"last_error\":\"%s\"},"
           "\"cache\":{\"hours\":%u,\"min_temp_c\":%s,"
           "\"max_wind_ms\":%s,\"peak_wind_dir_deg\":%s,\"max_solar_wm2\":%s,"
           "\"fetch_epoch_s\":%lld,\"provider_timezone\":\"%s\",\"decision_start_index\":%d,"
           "\"restored\":%s},"
           "\"last_error\":\"%s\",\"commands\":{\"active\":%u,\"sent\":%u,\"skipped\":%u,\"failed\":%u,"
           "\"blocked_stale\":%u,\"blocked_unreachable\":%u,\"blocked_untrusted\":%u},"
           "\"hours\":[",
           forecast_status_, forecast_location_mode, forecast_latitude_, forecast_longitude_,
           forecast_fetch_requested_ ? "true" : "false",
           fetch_age_s,
           weather_max_boost,
           odin_plan.enabled ? "true" : "false", odin_active ? "true" : "false",
           odin_source_esc, odin_plan_source_esc, odin_version_esc,
           (odin_active && odin_plan.available) ? "true" : "false",
           odin_fresh ? "true" : "false", odin_status, odin_plan.last_http_status, odin_age_s,
           static_cast<unsigned>(odin_plan.current_hour), static_cast<unsigned>(odin_plan.current_index), odin_target,
           odin_min, odin_max, odin_price,
           odin_heat, odin_plan.current_operation_mode_raw,
           odin_plan.heat_window_active ? "true" : "false",
           odin_plan.absorb_arm_enabled ? "true" : "false",
           odin_plan.preload_timing_bias_enabled ? "true" : "false",
           odin_plan.preload_bias_offset_cap_c,
           odin_plan.bias_window_planned_kwh,
           odin_plan.bias_window_delivered_kwh,
           static_cast<unsigned>(odin_plan.today_start_index),
           static_cast<unsigned>(odin_plan.idx_now),
           odin_plan.plan_stale ? "true" : "false",
           odin_decision_esc,
           odin_plan.absorb_armed ? "true" : "false",
           odin_arm_reason_esc,
           odin_plan.arm_energy_cost_modulation ? "true" : "false",
           plan_source::forecast_v2_runtime_enabled() ? "true" : "false",
           odin_error,
           static_cast<unsigned>(forecast_hours_count_), forecast_min_temp, forecast_max_wind,
           forecast_peak_dir, forecast_max_solar,
           static_cast<long long>(forecast_fetch_epoch_s_), forecast_provider_timezone,
           decision_start_index == lune_touch_forecast_timeline::NO_INDEX
               ? -1
               : static_cast<int>(decision_start_index),
           forecast_cache_restored_ ? "true" : "false", forecast_last_error,
           static_cast<unsigned>(last_forecast_dispatch_.active),
           static_cast<unsigned>(last_forecast_dispatch_.sent),
           static_cast<unsigned>(last_forecast_dispatch_.skipped),
           static_cast<unsigned>(last_forecast_dispatch_.failed),
           static_cast<unsigned>(last_forecast_dispatch_.blocked_stale),
           static_cast<unsigned>(last_forecast_dispatch_.blocked_unreachable),
           static_cast<unsigned>(last_forecast_dispatch_.blocked_untrusted));
  for (size_t i = 0; i < forecast_hours_count_ && i < 72 && off + 140 < capacity; i++) {
    const ForecastHourState &h = forecast_hours_[i];
    appendf_(buffer, capacity, off,
             "%s{\"h\":%u,\"timestamp_s\":%lld,\"temp_c\":%.1f,\"wind_ms\":%.1f,"
             "\"wind_dir_deg\":%.0f,\"solar_wm2\":%.0f,\"precip_mm\":%.1f,\"cloud_pct\":%.0f}",
             i ? "," : "", static_cast<unsigned>(i), static_cast<long long>(h.timestamp_s),
             h.temp_c, h.wind_speed_ms,
             h.wind_dir_deg, h.shortwave_wm2, h.precipitation_mm, h.cloud_cover_pct);
  }
  appendf_(buffer, capacity, off, "],\"decisions\":[");
  for (size_t i = 0; i < forecast_decision_count_ && off + 420 < capacity; i++) {
    const ForecastDecisionState &d = forecast_decisions_[i];
    char room_id[48];
    char room_name[80];
    json_escape_(d.room_id, room_id, sizeof(room_id));
    json_escape_(d.room_name, room_name, sizeof(room_name));
    appendf_(buffer, capacity, off,
             "%s{\"room_id\":\"%s\",\"name\":\"%s\",\"node_index\":%u,\"zone_index\":%u,"
             "\"comfort_setpoint_c\":%.1f,\"priority\":%u,\"offset_c\":%.2f,"
             "\"peak_load\":%.2f,\"peak_in_h\":%d,\"configured_thermal_lead_h\":%u,"
             "\"learned_thermal_lead_h\":%u,\"active_thermal_lead_h\":%u,"
             "\"timing_scale\":%.2f,\"heuristic_offset_c\":%.2f,\"min_temp_c\":%.2f,"
             "\"horizon_ok\":%s,\"used_horizon\":%s,\"used_thermal_sizing\":%s,"
             "\"preload_start_h\":%u,\"preload_end_h\":%u,\"active\":%s,\"odin_timing_bias\":%s,"
             "\"charge\":{\"episode\":%s,\"now\":%s,\"insufficient\":%s,\"store_c\":%.2f,"
             "\"deficit_kwh\":%.2f,\"floor_capacity_w\":%.0f,\"peak_loss_w\":%.0f,"
             "\"start_in_h\":%d,\"episode_in_h\":%d,\"end_in_h\":%d}}",
             i ? "," : "", room_id, room_name, static_cast<unsigned>(d.node_index),
             static_cast<unsigned>(d.zone_index), d.comfort_setpoint_c,
             static_cast<unsigned>(d.priority), d.offset_c, d.peak_load,
             static_cast<int>(d.peak_in_h),
             static_cast<unsigned>(d.configured_thermal_lead_h),
             static_cast<unsigned>(d.learned_thermal_lead_h),
             static_cast<unsigned>(d.active_thermal_lead_h), d.timing_scale,
             d.heuristic_offset_c, d.min_temp_c, d.horizon_ok ? "true" : "false",
             d.used_horizon ? "true" : "false", d.used_thermal_sizing ? "true" : "false",
             static_cast<unsigned>(d.preload_start_h), static_cast<unsigned>(d.preload_end_h),
             d.active ? "true" : "false", d.odin_timing_bias ? "true" : "false",
             d.charge_episode ? "true" : "false", d.charge_now ? "true" : "false",
             d.charge_insufficient ? "true" : "false", d.charge_store_c, d.charge_deficit_kwh,
             d.floor_capacity_w, d.peak_loss_w, static_cast<int>(d.charge_start_in_h),
             static_cast<int>(d.charge_episode_in_h), static_cast<int>(d.charge_end_in_h));
  }
  // Cap plan-vs-reality to the last 12 history hours so the envelope stays under
  // the dashboard response budget (hours already dominate the payload).
  appendf_(buffer, capacity, off, "],\"plan_vs_reality\":[");
  const uint8_t pvr_start =
      odin_plan.past_count > 12 ? static_cast<uint8_t>(odin_plan.past_count - 12) : 0;
  bool pvr_first = true;
  for (uint8_t i = pvr_start; i < odin_plan.past_count && off + 96 < capacity; i++) {
    char planned[16];
    char actual[16];
    char delta[16];
    json_float_token_(planned, sizeof(planned), odin_plan.plan_heat_past[i], 2);
    json_float_token_(actual, sizeof(actual), odin_plan.actual_prod_past[i], 2);
    float dlt = NAN;
    if (std::isfinite(odin_plan.plan_heat_past[i]) && std::isfinite(odin_plan.actual_prod_past[i]))
      dlt = odin_plan.actual_prod_past[i] - odin_plan.plan_heat_past[i];
    json_float_token_(delta, sizeof(delta), dlt, 2);
    appendf_(buffer, capacity, off,
             "%s{\"h\":%u,\"planned_kw\":%s,\"actual_kw\":%s,\"delta_kw\":%s}",
             pvr_first ? "" : ",", static_cast<unsigned>(i), planned, actual, delta);
    pvr_first = false;
  }
  appendf_(buffer, capacity, off, "]}");
}

void LuneTouchCoordinator::write_commands_json(char *buffer, size_t capacity) const {
  size_t off = 0;
  appendf_(buffer, capacity, off, "{\"commands\":[");
  bool first = true;
  for (size_t i = 0; i < ledger_.count(); i++) {
    const auto *record = ledger_.at(i);
    if (record == nullptr)
      continue;
    char request_id[32];
    char source[32];
    char reason[96];
    char room_id[48];
    char room_name[80];
    json_escape_(record->request_id, request_id, sizeof(request_id));
    json_escape_(record->source, source, sizeof(source));
    json_escape_(record->reason, reason, sizeof(reason));
    json_escape_(record->room_id, room_id, sizeof(room_id));
    json_escape_(record->room_id, room_name, sizeof(room_name));
    if (!appendf_(buffer, capacity, off,
                  "%s{\"request_id\":\"%s\",\"source\":\"%s\",\"reason\":\"%s\","
                  "\"room_id\":\"%s\",\"name\":\"%s\",\"node_id\":\"%s\",\"loop_id\":\"%s\","
                  "\"node_index\":%u,\"zone_index\":%u,"
                  "\"requested_offset_c\":%.2f,"
                  "\"accepted_offset_c\":%.2f,\"created_at_ms\":%lu,\"expires_at_ms\":%lu,"
                  "\"created_at_epoch_s\":%lld,\"expires_at_epoch_s\":%lld,\"boot_id\":%lu,"
                  "\"result\":\"%s\",\"clamp_applied\":%s}",
                  first ? "" : ",", request_id, source, reason, room_id, room_name,
                  record->node_id, record->loop_id,
                  static_cast<unsigned>(record->node_index), static_cast<unsigned>(record->zone_index),
                  record->requested_offset_c, record->accepted_offset_c,
                  static_cast<unsigned long>(record->created_at_ms),
                  static_cast<unsigned long>(record->expires_at_ms),
                  static_cast<long long>(record->created_at_epoch_s),
                  static_cast<long long>(record->expires_at_epoch_s),
                  static_cast<unsigned long>(record->boot_id),
                  ::lune_touch::command_result_name(record->result),
                  record->clamp_applied ? "true" : "false"))
      break;
    first = false;
  }
  appendf_(buffer, capacity, off, "]}");
}

void LuneTouchCoordinator::write_events_json(char *buffer, size_t capacity) const {
  size_t off = 0;
  appendf_(buffer, capacity, off, "{\"events\":[");
  if (take_state_lock_(50)) {
    for (size_t i = 0; i < event_count_; i++) {
      const size_t idx = (event_next_ + EVENT_CAPACITY - 1 - i) % EVENT_CAPACITY;
      const EventRecord &event = events_[idx];
      char level[16];
      char source[32];
      char message[128];
      json_escape_(event.level, level, sizeof(level));
      json_escape_(event.source, source, sizeof(source));
      json_escape_(event.message, message, sizeof(message));
      if (!appendf_(buffer, capacity, off,
                    "%s{\"ts_ms\":%lu,\"level\":\"%s\",\"source\":\"%s\","
                    "\"message\":\"%s\"}",
                    i ? "," : "", static_cast<unsigned long>(event.ts_ms),
                    level, source, message))
        break;
    }
    give_state_lock_();
  }
  appendf_(buffer, capacity, off, "]}");
}

void LuneTouchCoordinator::write_diagnostics_json(char *buffer, size_t capacity) const {
  uint8_t day_index = 0;
  uint16_t minute_of_day = 0;
  const bool schedule_time_valid = current_schedule_time_(time_, &day_index, &minute_of_day);
  const auto strategy = model_.strategy_snapshot(schedule_time_valid, day_index, minute_of_day);
  const auto learning = model_.learning_snapshot();
  const uint32_t now_ms = esphome::millis();
  size_t paired_nodes = 0;
  size_t trusted_nodes = 0;
  size_t reachable_nodes = 0;
  size_t reachable_trusted_nodes = 0;
  size_t stale_nodes = 0;
  size_t trusted_stale_nodes = 0;
  size_t identity_missing_nodes = 0;
  size_t trusted_identity_missing_nodes = 0;
  for (size_t i = 0; i < model_.node_count(); i++) {
    const auto *node = model_.node(i);
    if (node == nullptr)
      continue;
    const bool node_stale = model_.is_node_stale(i, now_ms);
    if (node->trust == ::lune_touch::NodeTrust::PAIRED)
      paired_nodes++;
    if (node->trust == ::lune_touch::NodeTrust::TRUSTED)
      trusted_nodes++;
    if ((node->trust == ::lune_touch::NodeTrust::PAIRED ||
         node->trust == ::lune_touch::NodeTrust::TRUSTED) &&
        node->pairing_fingerprint[0] == '\0') {
      identity_missing_nodes++;
      if (node->trust == ::lune_touch::NodeTrust::TRUSTED)
        trusted_identity_missing_nodes++;
    }
    if (node->reachable)
      reachable_nodes++;
    if (node->trust == ::lune_touch::NodeTrust::TRUSTED && node->reachable && !node_stale)
      reachable_trusted_nodes++;
    if (node_stale) {
      stale_nodes++;
      if (node->trust == ::lune_touch::NodeTrust::TRUSTED)
        trusted_stale_nodes++;
    }
  }
  size_t fresh_zones = 0;
  for (size_t i = 0; i < model_.zone_count(); i++) {
    const auto *zone = model_.zone(i);
    const auto *live = model_.zone_live(i);
    if (zone != nullptr && zone->enabled && live != nullptr && live->fresh)
      fresh_zones++;
  }
  const size_t pending_commands = ledger_.count_result(::lune_touch::CommandResult::PENDING);
  const size_t accepted_commands = ledger_.count_result(::lune_touch::CommandResult::ACCEPTED);
  const size_t rejected_commands = ledger_.count_result(::lune_touch::CommandResult::REJECTED);
  const size_t failed_commands = ledger_.count_result(::lune_touch::CommandResult::FAILED);
  const size_t expired_commands = ledger_.count_result(::lune_touch::CommandResult::EXPIRED);
  const size_t blocked_stale_commands = ledger_.count_result(::lune_touch::CommandResult::BLOCKED_STALE);
  const size_t blocked_unreachable_commands = ledger_.count_result(::lune_touch::CommandResult::BLOCKED_UNREACHABLE);
  const size_t blocked_untrusted_commands = ledger_.count_result(::lune_touch::CommandResult::BLOCKED_UNTRUSTED);
  const size_t blocked_commands = blocked_stale_commands + blocked_unreachable_commands + blocked_untrusted_commands;
  size_t clamped_commands = 0;
  for (size_t i = 0; i < ledger_.count(); i++) {
    const auto *record = ledger_.at(i);
    if (record != nullptr && record->clamp_applied)
      clamped_commands++;
  }
  const size_t bound_zones = model_.active_zone_count();
  const size_t stale_zones = model_.stale_zone_count();
  const bool has_forecast_location = std::isfinite(forecast_latitude_) && std::isfinite(forecast_longitude_) &&
                                     (std::fabs(forecast_latitude_) >= 0.0001f ||
                                      std::fabs(forecast_longitude_) >= 0.0001f);
  const bool ready_for_commands = reachable_trusted_nodes > 0 && trusted_identity_missing_nodes == 0 &&
                                  bound_zones > 0 && fresh_zones > 0;
  const bool ready_for_forecast = ready_for_commands && has_forecast_location;
  const char *next_action = "ready";
  if (model_.node_count() == 0)
    next_action = "add_node";
  else if (trusted_identity_missing_nodes > 0 || (trusted_nodes == 0 && identity_missing_nodes > 0))
    next_action = "verify_node_identity";
  else if (trusted_nodes == 0)
    next_action = "trust_node";
  else if (reachable_trusted_nodes == 0)
    next_action = "fix_node_poll";
  else if (bound_zones == 0)
    next_action = "review_v6_zones";
  else if (fresh_zones == 0)
    next_action = "wait_for_fresh_zone_poll";
  else if (!has_forecast_location)
    next_action = "set_forecast_location";

  diagnostics_blockers_[0] = '\0';
  size_t blockers_off = 0;
  size_t blockers_count = 0;
  auto append_blocker = [&](const char *scope, const char *target, const char *reason, const char *action) {
    if (blockers_count >= 8 || blockers_off + 128 >= sizeof(diagnostics_blockers_))
      return;
    char scope_esc[24];
    char target_esc[48];
    char reason_esc[40];
    char action_esc[40];
    json_escape_(scope, scope_esc, sizeof(scope_esc));
    json_escape_(target, target_esc, sizeof(target_esc));
    json_escape_(reason, reason_esc, sizeof(reason_esc));
    json_escape_(action, action_esc, sizeof(action_esc));
    if (appendf_(diagnostics_blockers_, sizeof(diagnostics_blockers_), blockers_off,
                 "%s{\"scope\":\"%s\",\"target\":\"%s\",\"reason\":\"%s\",\"action\":\"%s\"}",
                 blockers_count ? "," : "", scope_esc, target_esc, reason_esc, action_esc))
      blockers_count++;
  };
  if (model_.node_count() == 0)
    append_blocker("system", "coordinator", "no_nodes", "add_node");
  for (size_t i = 0; i < model_.node_count(); i++) {
    const auto *node = model_.node(i);
    if (node == nullptr)
      continue;
    const bool node_stale = model_.is_node_stale(i, now_ms);
    const char *target = node->node_id[0] != '\0' ? node->node_id : node->hostname;
    if ((node->trust == ::lune_touch::NodeTrust::PAIRED ||
         node->trust == ::lune_touch::NodeTrust::TRUSTED) &&
        node->pairing_fingerprint[0] == '\0') {
      append_blocker("node", target, "identity_missing", "verify_node_identity");
    } else if (node->trust == ::lune_touch::NodeTrust::PAIRED) {
      append_blocker("node", target, "not_trusted", "trust_node");
    } else if (node->trust == ::lune_touch::NodeTrust::TRUSTED && !node->reachable) {
      append_blocker("node", target, "unreachable", "fix_node_poll");
    } else if (node->trust == ::lune_touch::NodeTrust::TRUSTED && node_stale) {
      append_blocker("node", target, "stale", "fix_node_poll");
    }
  }
  if (trusted_nodes == 0 && identity_missing_nodes == 0 && model_.node_count() > 0)
    append_blocker("system", "coordinator", "no_trusted_nodes", "trust_node");
  if (bound_zones == 0)
    append_blocker("zones", "v6_manifolds", "no_imported_zones", "review_v6_zones");
  else if (fresh_zones == 0)
    append_blocker("zones", "registry", "no_fresh_zone_telemetry", "wait_for_fresh_zone_poll");
  if (!has_forecast_location)
    append_blocker("forecast", "location", "location_missing", "set_forecast_location");

  const esp_partition_t *running_partition = esp_ota_get_running_partition();
  const char *running_label = running_partition != nullptr ? running_partition->label : "unknown";
  const uint32_t running_size = running_partition != nullptr ? running_partition->size : 0;
  const uint8_t running_subtype = running_partition != nullptr ? running_partition->subtype : 0;
  esp_ota_img_states_t ota_state = ESP_OTA_IMG_UNDEFINED;
  if (running_partition != nullptr)
    esp_ota_get_state_partition(running_partition, &ota_state);
  char driver_room_id[64];
  char last_poll_error[112];
  char forecast_last_error[128];
  char ota_label[32];
  json_escape_(strategy.driver_room_id, driver_room_id, sizeof(driver_room_id));
  json_escape_(last_poll_error_, last_poll_error, sizeof(last_poll_error));
  json_escape_(forecast_last_error_, forecast_last_error, sizeof(forecast_last_error));
  json_escape_(running_label, ota_label, sizeof(ota_label));
  uint32_t self_ip = 0;
  uint32_t self_mask = 0;
  char ip_buf[20] = "";
  if (sta_ipv4_info_(&self_ip, &self_mask))
    format_ipv4_(self_ip, ip_buf, sizeof(ip_buf));
  char ip_esc[24];
  json_escape_(ip_buf, ip_esc, sizeof(ip_esc));
  char ssid_buf[64] = "";
  char mac_buf[24] = "";
  char version_buf[48] = "";
  if (esphome::wifi::global_wifi_component != nullptr) {
    const auto &ssid = esphome::wifi::global_wifi_component->wifi_ssid();
    if (!ssid.empty()) {
      std::strncpy(ssid_buf, ssid.c_str(), sizeof(ssid_buf) - 1);
      ssid_buf[sizeof(ssid_buf) - 1] = '\0';
    }
  }
  {
    char mac_pretty[esphome::MAC_ADDRESS_PRETTY_BUFFER_SIZE] = {};
    esphome::get_mac_address_pretty_into_buffer(mac_pretty);
    std::strncpy(mac_buf, mac_pretty, sizeof(mac_buf) - 1);
    mac_buf[sizeof(mac_buf) - 1] = '\0';
  }
#ifdef ESPHOME_PROJECT_VERSION
  std::strncpy(version_buf, ESPHOME_PROJECT_VERSION, sizeof(version_buf) - 1);
  version_buf[sizeof(version_buf) - 1] = '\0';
#endif
  char esphome_buf[24] = "";
#ifdef ESPHOME_VERSION
  std::strncpy(esphome_buf, ESPHOME_VERSION, sizeof(esphome_buf) - 1);
  esphome_buf[sizeof(esphome_buf) - 1] = '\0';
#endif
  char ssid_esc[72];
  char mac_esc[32];
  char version_esc[56];
  char esphome_esc[32];
  json_escape_(ssid_buf, ssid_esc, sizeof(ssid_esc));
  json_escape_(mac_buf, mac_esc, sizeof(mac_esc));
  json_escape_(version_buf, version_esc, sizeof(version_esc));
  json_escape_(esphome_buf, esphome_esc, sizeof(esphome_esc));
  CirculationPumpState circulation;
  if (take_state_lock_(50)) {
    circulation = circulation_;
    give_state_lock_();
  }
  char circulation_json[768];
  format_circulation_json_(circulation, now_ms, CIRCULATION_STALE_MS, circulation_json,
                           sizeof(circulation_json));

  snprintf(buffer, capacity,
           "{\"heap\":\"watching\",\"nodes\":%u,\"zones\":%u,\"ledger\":%u,"
           "\"screen\":\"sidebar-views\",\"api\":\"/api/lune-touch/v1\","
           "\"ownership\":{\"odin\":\"heat_pump_timing_prices_weather_solar_compressor_dhw\","
           "\"touch\":\"logical_rooms_distribution_schedules_house_signals_asgard_normal\","
           "\"v6\":\"local_safe_heating_and_explicit_fallback_only\","
           "\"touch_prices\":\"read_only\"},"
           "\"command_results\":{\"pending\":%u,\"accepted\":%u,\"rejected\":%u,"
           "\"failed\":%u,\"expired\":%u,\"blocked\":%u,\"blocked_stale\":%u,"
           "\"blocked_unreachable\":%u,\"blocked_untrusted\":%u,\"clamped\":%u},"
           "\"polling\":{\"last_poll_ms\":%lu,\"success\":%lu,\"fail\":%lu,\"last_error\":\"%s\"},"
           "\"commissioning\":{\"paired_nodes\":%u,\"trusted_nodes\":%u,"
           "\"reachable_nodes\":%u,\"reachable_trusted_nodes\":%u,"
           "\"stale_nodes\":%u,\"trusted_stale_nodes\":%u,\"identity_missing_nodes\":%u,"
           "\"bound_zones\":%u,"
           "\"fresh_zones\":%u,\"stale_zones\":%u,\"ready_for_commands\":%s,"
           "\"ready_for_forecast\":%s,\"next_action\":\"%s\",\"blockers\":[%s]},"
           "\"ota\":{\"running_label\":\"%s\",\"running_subtype\":%u,"
           "\"running_slot_size\":%lu,\"configured_slot_size\":%lu,"
           "\"state\":\"%s\",\"pending_verify\":%s},"
           "\"strategy\":{\"has_physical_temperature\":%s,\"physical_temperature_c\":%.2f,"
           "\"comfort_demand_c\":%.2f,\"driver_room\":\"%s\"},"
           "\"learning\":{\"zones_with_history\":%lu,\"total_samples\":%lu,"
           "\"total_calling_samples\":%lu,\"calling_ratio\":%.3f,"
           "\"zones_with_delta\":%lu,\"warming_zones\":%lu,\"cooling_zones\":%lu,"
           "\"average_delta_c_per_h\":%.3f},"
           "\"forecast\":{\"status\":\"%s\",\"fetch_pending\":%s,"
           "\"last_fetch_age_s\":%lu,\"last_error\":\"%s\"},"
           "\"forecast_commands\":{\"active\":%u,\"sent\":%u,\"skipped\":%u,\"failed\":%u,"
           "\"blocked_stale\":%u,\"blocked_unreachable\":%u,\"blocked_untrusted\":%u},"
           "\"network\":{\"ip\":\"%s\",\"ssid\":\"%s\",\"mac\":\"%s\",\"version\":\"%s\",\"esphome\":\"%s\",\"uptime_s\":%lu},"
           "\"circulation\":%s}",
           static_cast<unsigned>(model_.node_count()),
           static_cast<unsigned>(model_.zone_count()),
           static_cast<unsigned>(ledger_.count()),
           static_cast<unsigned>(pending_commands),
           static_cast<unsigned>(accepted_commands),
           static_cast<unsigned>(rejected_commands),
           static_cast<unsigned>(failed_commands),
           static_cast<unsigned>(expired_commands),
           static_cast<unsigned>(blocked_commands),
           static_cast<unsigned>(blocked_stale_commands),
           static_cast<unsigned>(blocked_unreachable_commands),
           static_cast<unsigned>(blocked_untrusted_commands),
           static_cast<unsigned>(clamped_commands),
           static_cast<unsigned long>(last_poll_ms_),
           static_cast<unsigned long>(poll_success_count_),
           static_cast<unsigned long>(poll_fail_count_),
           last_poll_error,
           static_cast<unsigned>(paired_nodes),
           static_cast<unsigned>(trusted_nodes),
           static_cast<unsigned>(reachable_nodes),
           static_cast<unsigned>(reachable_trusted_nodes),
           static_cast<unsigned>(stale_nodes),
           static_cast<unsigned>(trusted_stale_nodes),
           static_cast<unsigned>(identity_missing_nodes),
           static_cast<unsigned>(bound_zones),
           static_cast<unsigned>(fresh_zones),
           static_cast<unsigned>(stale_zones),
           ready_for_commands ? "true" : "false",
           ready_for_forecast ? "true" : "false",
           next_action,
           diagnostics_blockers_,
           ota_label,
           static_cast<unsigned>(running_subtype),
           static_cast<unsigned long>(running_size),
           static_cast<unsigned long>(OTA_SLOT_BYTES),
           ota_state_name_(ota_state),
           ota_state == ESP_OTA_IMG_PENDING_VERIFY ? "true" : "false",
           strategy.has_physical_temperature ? "true" : "false",
           strategy.physical_temperature_c,
           strategy.comfort_demand_c,
           driver_room_id,
           static_cast<unsigned long>(learning.zones_with_history),
           static_cast<unsigned long>(learning.total_samples),
           static_cast<unsigned long>(learning.total_calling_samples),
           learning.calling_ratio,
           static_cast<unsigned long>(learning.zones_with_delta),
           static_cast<unsigned long>(learning.warming_zones),
           static_cast<unsigned long>(learning.cooling_zones),
           learning.average_delta_c_per_h,
           forecast_status_,
           forecast_fetch_requested_ ? "true" : "false",
           forecast_last_fetch_ms_ == 0 ? 0UL : static_cast<unsigned long>((esphome::millis() - forecast_last_fetch_ms_) / 1000UL),
           forecast_last_error,
           static_cast<unsigned>(last_forecast_dispatch_.active),
           static_cast<unsigned>(last_forecast_dispatch_.sent),
           static_cast<unsigned>(last_forecast_dispatch_.skipped),
           static_cast<unsigned>(last_forecast_dispatch_.failed),
           static_cast<unsigned>(last_forecast_dispatch_.blocked_stale),
           static_cast<unsigned>(last_forecast_dispatch_.blocked_unreachable),
           static_cast<unsigned>(last_forecast_dispatch_.blocked_untrusted),
           ip_esc,
           ssid_esc,
           mac_esc,
           version_esc,
           esphome_esc,
           static_cast<unsigned long>(now_ms / 1000UL),
           circulation_json);
  // Wall-display idle diagnostics (fed by the display's 50 ms interval).
  const size_t len = std::strlen(buffer);
  if (len > 0 && buffer[len - 1] == '}' && len + 200 < capacity) {
    const uint32_t reset_age_s = display_diag_.last_activity_reset_ms == 0
                                     ? 0
                                     : (now_ms - display_diag_.last_activity_reset_ms) / 1000UL;
    std::snprintf(buffer + len - 1, capacity - len + 1,
                  ",\"display\":{\"known\":%s,\"awake\":%s,\"paused\":%s,\"inactive_s\":%lu,"
                  "\"idle_timeout_s\":%u,\"activity_resets\":%lu,\"last_activity_reset_age_s\":%lu,"
                  "\"sleeps\":%lu,\"wakes\":%lu,\"wake_requests\":%lu}}",
                  display_diag_.known ? "true" : "false", display_diag_.awake ? "true" : "false",
                  display_diag_.paused ? "true" : "false",
                  static_cast<unsigned long>(display_diag_.inactive_ms / 1000UL),
                  static_cast<unsigned>(display_idle_timeout_s_),
                  static_cast<unsigned long>(display_diag_.activity_resets),
                  static_cast<unsigned long>(reset_age_s), static_cast<unsigned long>(display_diag_.sleeps),
                  static_cast<unsigned long>(display_diag_.wakes),
                  static_cast<unsigned long>(display_diag_.wake_requests));
  }
}

void LuneTouchCoordinator::note_display_state(bool awake, bool paused, uint32_t inactive_ms) {
  // Lock-free: written only from the display interval, read by diagnostics.
  if (display_diag_.known) {
    // LVGL's inactivity counter dropping means an input event (touch) or an
    // explicit wake reset it.
    if (inactive_ms + 1000UL < display_diag_.inactive_ms) {
      display_diag_.activity_resets++;
      display_diag_.last_activity_reset_ms = esphome::millis();
    }
    if (display_diag_.awake && !awake)
      display_diag_.sleeps++;
    if (!display_diag_.awake && awake)
      display_diag_.wakes++;
  }
  display_diag_.known = true;
  display_diag_.awake = awake;
  display_diag_.paused = paused;
  display_diag_.inactive_ms = inactive_ms;
}

void LuneTouchCoordinator::note_mqtt_stream_(odin_mqtt::StreamId stream, uint32_t now_ms) {
  const size_t idx = static_cast<size_t>(stream);
  if (idx >= static_cast<size_t>(odin_mqtt::StreamId::Count))
    return;
  odin_mqtt_client_.ages[idx].ever_seen = true;
  odin_mqtt_client_.ages[idx].last_message_ms = now_ms;
}

void LuneTouchCoordinator::process_odin_mqtt_command_(uint32_t now_ms) {
  const auto &cmd = odin_mqtt_client_.command;
  odin_mqtt_.soft_stop = cmd.soft_stop;
  odin_mqtt_.flow_target_c = cmd.flow_target_c;
  odin_mqtt_.valid_until_unix = cmd.valid_until_unix;
  if (odin_mqtt::should_disarm_from_command(cmd)) {
    std::strncpy(mqtt_absorb_reason_, cmd.soft_stop ? "soft_stop" : "mode_not_heat",
                 sizeof(mqtt_absorb_reason_) - 1);
    mqtt_absorb_action_ = 2;
    kick_task_(odin_task_handle_);
    return;
  }
  if (odin_mqtt::should_arm_from_command(cmd, odin_plan_.arm_energy_cost_modulation) &&
      !odin_defrost_blocks_arm_()) {
    std::strncpy(odin_plan_.current_decision_reason,
                 plan_source::decision_reason_name(cmd.decision_reason),
                 sizeof(odin_plan_.current_decision_reason) - 1);
    odin_plan_.heat_window_active = true;
    mqtt_absorb_action_ = 1;
    kick_task_(odin_task_handle_);
  }
  (void) now_ms;
}

void LuneTouchCoordinator::process_odin_mqtt_telemetry_(uint32_t now_ms) {
  const auto &tel = odin_mqtt_client_.telemetry;
  if (tel.defrost_known) {
    odin_mqtt_.defrost_known = true;
    odin_mqtt_.defrost_active = tel.defrost_active;
    heat_source_.defrost_active = tel.defrost_active;
    if (tel.defrost_active) {
      std::strncpy(mqtt_absorb_reason_, "defrost", sizeof(mqtt_absorb_reason_) - 1);
      mqtt_absorb_action_ = 2;
      kick_task_(odin_task_handle_);
    }
  }
  odin_mqtt_.telemetry_thermal_w = tel.thermal_w;
  odin_mqtt_.telemetry_flow_rate = tel.flow_rate;
  odin_mqtt_.telemetry_return_temp_c = tel.return_temp_c;
  odin_mqtt_.telemetry_hp_feed_temp_c = tel.hp_feed_temp_c;
  (void) now_ms;
}

bool LuneTouchCoordinator::fetch_odin_physics_() {
  if (odin_plan_.odin_host[0] == '\0')
    return false;
  char url[192];
  std::snprintf(url, sizeof(url), "http://%s:%u/api/physics", odin_plan_.odin_host,
                static_cast<unsigned>(odin_plan_.odin_port == 0 ? 80 : odin_plan_.odin_port));
  static char body[2048];
  int status = 0;
  if (!fetch_json_(url, body, sizeof(body), &status)) {
    if (take_state_lock_(100)) {
      odin_physics_.available = false;
      odin_physics_.last_http_status = status;
      std::strncpy(odin_physics_.last_error, status > 0 ? "http error" : "unreachable",
                   sizeof(odin_physics_.last_error) - 1);
      give_state_lock_();
    }
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    if (take_state_lock_(100)) {
      odin_physics_.available = false;
      std::strncpy(odin_physics_.last_error, "invalid json", sizeof(odin_physics_.last_error) - 1);
      give_state_lock_();
    }
    return false;
  }
  JsonObjectConst root = doc.as<JsonObjectConst>();
  odin_physics::PhysicsSnapshot snap{};
  snap.success = true;
  // Odin 2.0 (fw 2.0.0-23) answers {"values":{"raw-hl-tm":τ,"raw-heat":kWh,
  // "raw-run":h,"raw-solar-factor":…}}; heat loss itself is only in /api/debug
  // (used_heat_loss, read by run_odin_control_). Older shapes kept as fallback.
  JsonObjectConst values = root["values"].as<JsonObjectConst>();
  bool learned = true;
  if (!values.isNull()) {
    snap.tau_z1_h = values["raw-hl-tm"] | NAN;
    snap.tau_z2_h = values["raw-hl-tm-z2"] | NAN;
    snap.passive_solar_z1 = values["raw-solar-factor"] | NAN;
    snap.passive_solar_z2 = values["raw-solar-factor-z2"] | NAN;
    const float heat = values["raw-heat"] | 0.0f;
    const float run = values["raw-run"] | 0.0f;
    // Before Odin has heating data it plans with generic defaults — never feed
    // those into Touch's model as if they were learned.
    learned = heat > 0.0f && run > 0.0f;
    float used_hl = NAN;
    if (take_state_lock_(100)) {
      used_hl = odin_control_.heat_loss_kw_per_k;
      give_state_lock_();
    }
    snap.heat_loss_kw_per_k = learned ? used_hl : NAN;
  } else {
    snap.heat_loss_kw_per_k = root["heat_loss"] | root["used_heat_loss"] | NAN;
    snap.tau_z1_h = root["tau_z1"] | root["hl_tm_product"] | NAN;
    snap.tau_z2_h = root["tau_z2"] | NAN;
    snap.passive_solar_z1 = root["passive_solar_z1"] | root["passive_solar_gain_factor"] | NAN;
    snap.passive_solar_z2 = root["passive_solar_z2"] | NAN;
    snap.zone2_active = root["zone2_active"] | false;
  }
  const auto strategy = model_.strategy_snapshot();
  const float touch_hl = strategy.ua_total_w_per_k > 0.0f ? strategy.ua_total_w_per_k / 1000.0f : NAN;
  const float touch_tau = strategy.hl_tm_product;
  if (learned && std::isfinite(snap.heat_loss_kw_per_k) && snap.heat_loss_kw_per_k > 0.0f)
    model_.set_odin_heat_loss_w_per_k(snap.heat_loss_kw_per_k * 1000.0f);
  if (learned && std::isfinite(snap.tau_z1_h) && snap.tau_z1_h > 0.0f)
    model_.set_odin_tau_h(snap.tau_z1_h);
  if (!take_state_lock_(100))
    return false;
  odin_physics_.learned = learned;
  odin_physics_.available = true;
  odin_physics_.heat_loss_kw_per_k = snap.heat_loss_kw_per_k;
  odin_physics_.tau_z1_h = snap.tau_z1_h;
  odin_physics_.tau_z2_h = snap.tau_z2_h;
  odin_physics_.passive_solar_z1 = snap.passive_solar_z1;
  odin_physics_.passive_solar_z2 = snap.passive_solar_z2;
  odin_physics_.zone2_active = snap.zone2_active;
  // Warn when Odin reports zone2 without two hydraulic circuits configured in Touch.
  odin_physics_.zone2_warning = snap.zone2_active && model_.zone_count() < 2;
  odin_physics_.deviation_hl = odin_physics::deviation_flag(snap.heat_loss_kw_per_k, touch_hl);
  odin_physics_.deviation_tau = odin_physics::deviation_flag(snap.tau_z1_h, touch_tau);
  odin_physics_.last_fetch_ms = esphome::millis();
  odin_physics_.last_error[0] = '\0';
  give_state_lock_();
  maybe_calibrate_house_physics_();
  return true;
}

void LuneTouchCoordinator::write_odin_mqtt_json(char *buffer, size_t capacity) const {
  OdinMqttState mqtt{};
  if (take_state_lock_(50)) {
    mqtt = odin_mqtt_;
    give_state_lock_();
  }
  const uint32_t now = esphome::millis();
  auto age_s = [&](uint32_t last_ms) -> unsigned long {
    if (last_ms == 0)
      return 0;
    return static_cast<unsigned long>((now - last_ms) / 1000UL);
  };
  char host[128];
  char user[128];
  char prefix[128];
  char hp[64];
  char err[192];
  json_escape_(mqtt.host, host, sizeof(host));
  json_escape_(mqtt.username, user, sizeof(user));
  json_escape_(mqtt.topic_prefix, prefix, sizeof(prefix));
  json_escape_(mqtt.hp_id, hp, sizeof(hp));
  json_escape_(mqtt.last_error, err, sizeof(err));
  char flow[16];
  char thermal[16];
  char flow_rate[16];
  char ret[16];
  char feed[16];
  json_float_token_(flow, sizeof(flow), mqtt.flow_target_c, 2);
  json_float_token_(thermal, sizeof(thermal), mqtt.telemetry_thermal_w, 0);
  json_float_token_(flow_rate, sizeof(flow_rate), mqtt.telemetry_flow_rate, 3);
  json_float_token_(ret, sizeof(ret), mqtt.telemetry_return_temp_c, 2);
  json_float_token_(feed, sizeof(feed), mqtt.telemetry_hp_feed_temp_c, 2);
  // Never include mqtt password in any read endpoint.
  std::snprintf(
      buffer, capacity,
      "{\"enabled\":%s,\"connected\":%s,\"host\":\"%s\",\"port\":%u,\"username\":\"%s\","
      "\"password_set\":%s,\"topic_prefix\":\"%s\",\"hp_id\":\"%s\","
      "\"odin_status\":{\"known\":%s,\"online\":%s},"
      "\"ages_s\":{\"status\":%lu,\"result\":%lu,\"commands\":%lu,\"telemetry\":%lu},"
      "\"defrost\":{\"known\":%s,\"active\":%s},\"soft_stop\":%s,\"flow_target_c\":%s,"
      "\"valid_until_unix\":%lu,\"http_fallback_active\":%s,"
      "\"telemetry\":{\"thermal_w\":%s,\"flow_rate\":%s,\"return_temp_c\":%s,\"hp_feed_temp_c\":%s},"
      "\"last_error\":\"%s\",\"publishes\":false}",
      mqtt.enabled ? "true" : "false", mqtt.connected ? "true" : "false", host,
      static_cast<unsigned>(mqtt.port), user, mqtt.password_set ? "true" : "false", prefix, hp,
      mqtt.odin_online_known ? "true" : "false", mqtt.odin_online ? "true" : "false",
      age_s(mqtt.age_status_ms), age_s(mqtt.age_result_ms), age_s(mqtt.age_commands_ms),
      age_s(mqtt.age_telemetry_ms), mqtt.defrost_known ? "true" : "false",
      mqtt.defrost_active ? "true" : "false", mqtt.soft_stop ? "true" : "false", flow,
      static_cast<unsigned long>(mqtt.valid_until_unix),
      mqtt.http_fallback_active ? "true" : "false", thermal, flow_rate, ret, feed, err);
}

void LuneTouchCoordinator::write_plan_json(char *buffer, size_t capacity) const {
  // Snapshot under the lock; format outside it. Hours are relative to now
  // (index 0 = the current local hour), 24 h ahead.
  static constexpr size_t H = 24;
  struct RoomPlan {
    char room_id[32];
    char name[48];
    bool preload;
    uint8_t pre_from, pre_to;
    float offset_c;
    bool charge;
    int16_t ch_from, ch_to;
    float store_c;
    bool insufficient;
  };
  static RoomPlan rooms[::lune_touch::MAX_HOUSE_ZONES];
  size_t room_count = 0;
  static OdinPlanState *odin_p = psram_scratch_<OdinPlanState>(1);
  if (odin_p == nullptr) {
    snprintf(buffer, capacity, "{\"available\":false}");
    return;
  }
  OdinPlanState &odin = *odin_p;
  odin = OdinPlanState{};
  lune_touch_odin::Lift applied{};
  lune_touch_odin::Lift wanted{};
  bool control_enabled = false;
  ::lune_touch::StrategySnapshot strategy{};
  if (!take_state_lock_(50)) {
    snprintf(buffer, capacity, "{\"available\":false}");
    return;
  }
  odin = odin_plan_;
  applied = odin_control_.applied;
  wanted = odin_control_.wanted;
  control_enabled = odin_control_.enabled;
  strategy = model_.strategy_snapshot();
  for (size_t i = 0; i < forecast_decision_count_ && room_count < ::lune_touch::MAX_HOUSE_ZONES; i++) {
    const auto &d = forecast_decisions_[i];
    bool seen = false;
    for (size_t j = 0; j < room_count && !seen; j++)
      seen = std::strcmp(rooms[j].room_id, d.room_id) == 0;
    if (seen)
      continue;
    RoomPlan &r = rooms[room_count++];
    std::strncpy(r.room_id, d.room_id, sizeof(r.room_id) - 1);
    r.room_id[sizeof(r.room_id) - 1] = '\0';
    std::strncpy(r.name, d.room_name[0] != '\0' ? d.room_name : d.room_id, sizeof(r.name) - 1);
    r.name[sizeof(r.name) - 1] = '\0';
    r.preload = d.active && d.offset_c > 0.01f && d.preload_end_h > d.preload_start_h;
    r.pre_from = d.preload_start_h;
    r.pre_to = d.preload_end_h;
    r.offset_c = d.offset_c;
    r.charge = d.charge_episode && d.charge_end_in_h > 0;
    r.ch_from = static_cast<int16_t>(std::max<int>(0, d.charge_start_in_h));
    r.ch_to = d.charge_end_in_h;
    r.store_c = d.charge_store_c;
    r.insufficient = d.charge_insufficient;
  }
  give_state_lock_();

  uint8_t hour = 0;
  bool clock_ok = false;
  if (time_ != nullptr) {
    auto now = time_->now();
    if (now.is_valid()) {
      hour = static_cast<uint8_t>(now.hour);
      clock_ok = true;
    }
  }
  const bool odin_ok = odin.available && odin.heat_production_count > 0;
  size_t off = 0;
  appendf_(buffer, capacity, off, "{\"available\":true,\"clock\":%s,\"start_hour\":%u,\"hours\":%u,",
           clock_ok ? "true" : "false", static_cast<unsigned>(hour), static_cast<unsigned>(H));
  char tt[16], tc[16];
  json_float_token_(tt, sizeof(tt), strategy.has_house_target ? strategy.house_target_c : NAN, 1);
  json_float_token_(tc, sizeof(tc), strategy.has_physical_temperature ? strategy.physical_temperature_c : NAN, 2);
  appendf_(buffer, capacity, off, "\"house\":{\"target_c\":%s,\"temp_c\":%s},", tt, tc);
  appendf_(buffer, capacity, off, "\"odin\":{\"available\":%s,\"control\":%s,", odin_ok ? "true" : "false",
           control_enabled ? "true" : "false");
  auto arr = [&](const char *key, const float *v, int dec) {
    appendf_(buffer, capacity, off, "\"%s\":[", key);
    for (size_t h = 0; h < H; h++) {
      char tok[16];
      json_float_token_(tok, sizeof(tok), odin_ok && h < odin.heat_production_count ? v[h] : NAN, dec);
      appendf_(buffer, capacity, off, "%s%s", h ? "," : "", tok);
    }
    appendf_(buffer, capacity, off, "],");
  };
  arr("heat_kw", odin.heat_production_horizon, 2);
  arr("band_min_c", odin.band_min_horizon, 1);
  arr("band_max_c", odin.band_max_horizon, 1);
  arr("expected_c", odin.expected_temp_horizon, 2);
  arr("energy_kwh", odin.energy_horizon, 2);
  appendf_(buffer, capacity, off, "\"mode\":[");
  for (size_t h = 0; h < H; h++)
    appendf_(buffer, capacity, off, "%s%d", h ? "," : "",
             odin_ok && h < odin.heat_production_count ? odin.operation_mode_horizon[h] : -1);
  // Touch's lift of Odin's comfort band, per hour from now (applied, else wanted).
  const lune_touch_odin::Lift &lift = applied.active() ? applied : wanted;
  appendf_(buffer, capacity, off, "],\"lift_applied\":%s,\"lift_c\":[", applied.active() ? "true" : "false");
  for (size_t h = 0; h < H; h++) {
    const uint8_t wall = static_cast<uint8_t>((hour + h) % 24);
    const float v = clock_ok && lune_touch_odin::hour_in_lift(lift, wall) ? lift.lift_c : 0.0f;
    appendf_(buffer, capacity, off, "%s%.1f", h ? "," : "", static_cast<double>(v));
  }
  appendf_(buffer, capacity, off, "]},\"rooms\":[");
  for (size_t i = 0; i < room_count && off + 256 < capacity; i++) {
    const RoomPlan &r = rooms[i];
    char id[64], name[96];
    json_escape_(r.room_id, id, sizeof(id));
    json_escape_(r.name, name, sizeof(name));
    appendf_(buffer, capacity, off, "%s{\"room_id\":\"%s\",\"name\":\"%s\",\"preload\":", i ? "," : "", id, name);
    if (r.preload)
      appendf_(buffer, capacity, off, "{\"from\":%u,\"to\":%u,\"offset_c\":%.1f}", static_cast<unsigned>(r.pre_from),
               static_cast<unsigned>(r.pre_to), static_cast<double>(r.offset_c));
    else
      appendf_(buffer, capacity, off, "null");
    appendf_(buffer, capacity, off, ",\"charge\":");
    if (r.charge)
      appendf_(buffer, capacity, off, "{\"from\":%d,\"to\":%d,\"store_c\":%.1f,\"insufficient\":%s}", r.ch_from,
               r.ch_to, static_cast<double>(r.store_c), r.insufficient ? "true" : "false");
    else
      appendf_(buffer, capacity, off, "null");
    appendf_(buffer, capacity, off, "}");
  }
  appendf_(buffer, capacity, off, "]}");
}

void LuneTouchCoordinator::write_heat_source_control_json(char *buffer, size_t capacity) const {
  OdinControlState c{};
  GenericLeverState g{};
  bool has_user = false;
  bool yielded = false;
  const char *route = "none";
  float held = 0.0f;
  float uplift = 0.0f;
  char type[24]{};
  if (take_state_lock_(50)) {
    c = odin_control_;
    g = generic_levers_;
    has_user = odin_owner_.has_user;
    yielded = odin_owner_.yielded;
    route = demand_route_;
    held = demand_held_c_;
    uplift = demand_target_uplift_c_;
    std::strncpy(type, heat_source_.type, sizeof(type) - 1);
    give_state_lock_();
  }
  const uint32_t now = esphome::millis();
  char fwd[96], err[192], turl[256], rurl[256], curl[256];
  json_escape_(c.forwarder_entity, fwd, sizeof(fwd));
  json_escape_(c.last_error, err, sizeof(err));
  json_escape_(g.target_url_template, turl, sizeof(turl));
  json_escape_(g.heat_request_url_template, rurl, sizeof(rurl));
  json_escape_(g.curve_offset_url_template, curl, sizeof(curl));
  char room[16], tm[16], hl[16], lt[16], lc[16];
  json_float_token_(room, sizeof(room), c.odin_room_c, 2);
  json_float_token_(tm, sizeof(tm), c.thermal_mass_kwh_per_k, 2);
  json_float_token_(hl, sizeof(hl), c.heat_loss_kw_per_k, 3);
  json_float_token_(lt, sizeof(lt), g.last_lever_target_c, 1);
  json_float_token_(lc, sizeof(lc), g.last_lever_curve_c, 1);
  char telemetry_age[16];
  if (c.telemetry_age_s >= 0)
    std::snprintf(telemetry_age, sizeof(telemetry_age), "%ld", static_cast<long>(c.telemetry_age_s));
  else
    std::strcpy(telemetry_age, "null");
  char mqtt_age[16];
  if (c.mqtt_telemetry_age_s >= 0)
    std::snprintf(mqtt_age, sizeof(mqtt_age), "%ld", static_cast<long>(c.mqtt_telemetry_age_s));
  else
    std::strcpy(mqtt_age, "null");
  const uint32_t write_age_s = c.last_write_ms == 0 ? 0 : (now - c.last_write_ms) / 1000UL;
  const uint32_t bad_s = c.link_bad_since_ms == 0 ? 0 : (now - c.link_bad_since_ms) / 1000UL;
  std::snprintf(
      buffer, capacity,
      "{\"demand\":{\"route\":\"%s\",\"held_c\":%.2f,\"target_uplift_c\":%.2f},"
      "\"odin\":{\"enabled\":%s,\"max_lift_c\":%.1f,\"forwarder_entity\":\"%s\",\"status\":\"%s\","
      "\"reason\":\"%s\",\"last_action\":\"%s\",\"user_schedule_known\":%s,\"yielded\":%s,"
      "\"wanted\":{\"active\":%s,\"start_hour\":%u,\"hours\":%u,\"lift_c\":%.1f,\"energy_kwh\":%.2f},"
      "\"applied\":{\"active\":%s,\"start_hour\":%u,\"hours\":%u,\"lift_c\":%.1f},"
      "\"thermal_mass_kwh_per_k\":%s,\"heat_loss_kw_per_k\":%s,"
      "\"last_write_age_s\":%u,\"last_http_status\":%d,\"last_error\":\"%s\"},"
      "\"link\":{\"status\":\"%s\",\"alarm\":%s,\"bad_for_s\":%u,\"odin_reachable\":%s,"
      "\"forwarder_known\":%s,\"forwarder_active\":%s,\"takeover\":%s,\"telemetry_age_s\":%s,"
      "\"mqtt_telemetry_age_s\":%s,\"source\":\"%s\",\"odin_room_c\":%s},"
      "\"generic\":{\"applies\":%s,\"target_url_template\":\"%s\",\"heat_request_url_template\":\"%s\","
      "\"curve_offset_url_template\":\"%s\",\"curve_gain\":%.2f,\"curve_max_offset_c\":%.1f,"
      "\"last_target_c\":%s,\"last_heat_request\":%s,\"last_curve_offset_c\":%s,\"status\":\"%s\"}}",
      route, static_cast<double>(held), static_cast<double>(uplift),
      c.enabled ? "true" : "false", static_cast<double>(c.max_lift_c), fwd, c.status, c.reason,
      c.last_action, has_user ? "true" : "false", yielded ? "true" : "false",
      c.wanted.active() ? "true" : "false", static_cast<unsigned>(c.wanted.start_hour),
      static_cast<unsigned>(c.wanted.hours), static_cast<double>(c.wanted.lift_c),
      static_cast<double>(c.wanted_energy_kwh),
      c.applied.active() ? "true" : "false", static_cast<unsigned>(c.applied.start_hour),
      static_cast<unsigned>(c.applied.hours), static_cast<double>(c.applied.lift_c), tm, hl,
      static_cast<unsigned>(write_age_s), c.last_http_status, err,
      c.link_status, c.link_alarm ? "true" : "false", static_cast<unsigned>(bad_s),
      c.odin_reachable ? "true" : "false", c.forwarder_known ? "true" : "false",
      c.forwarder_active ? "true" : "false",
      c.takeover_known ? (c.takeover_active ? "true" : "false") : "null", telemetry_age, mqtt_age,
      c.mqtt_telemetry_age_s >= 0 ? "mqtt+http" : "http", room,
      heat_source_type_is_asgard_(type) ? "false" : "true", turl, rurl, curl,
      static_cast<double>(g.curve_gain), static_cast<double>(g.curve_max_offset_c), lt,
      g.last_lever_request < 0 ? "null" : (g.last_lever_request ? "true" : "false"), lc, g.lever_status);
}

bool LuneTouchCoordinator::set_heat_source_control(bool has_odin_enabled, bool odin_enabled,
                                                   float odin_max_lift_c, const char *forwarder_entity,
                                                   const char *target_url_template,
                                                   const char *heat_request_url_template,
                                                   const char *curve_offset_url_template, float curve_gain,
                                                   float curve_max_offset_c, char *response,
                                                   size_t capacity) {
  if (std::isfinite(odin_max_lift_c) && (odin_max_lift_c < 0.3f || odin_max_lift_c > 3.0f)) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"max_lift_out_of_range\"}");
    return false;
  }
  if ((std::isfinite(curve_gain) && (curve_gain < 0.0f || curve_gain > 10.0f)) ||
      (std::isfinite(curve_max_offset_c) && (curve_max_offset_c < 0.0f || curve_max_offset_c > 15.0f))) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"curve_out_of_range\"}");
    return false;
  }
  for (const char *t : {target_url_template, heat_request_url_template, curve_offset_url_template}) {
    if (t != nullptr && t[0] != '\0' && !asgard_url::valid_url_template(t, true)) {
      snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"invalid_lever_url_template\"}");
      return false;
    }
  }
  if (forwarder_entity != nullptr && forwarder_entity[0] != '\0' &&
      !asgard_adapter::valid_entity(forwarder_entity)) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"invalid_forwarder_entity\"}");
    return false;
  }
  if (!take_state_lock_(100)) {
    snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"coordinator_busy\"}");
    return false;
  }
  bool turned_on = false;
  if (has_odin_enabled) {
    turned_on = odin_enabled && !odin_control_.enabled;
    odin_control_.enabled = odin_enabled;
    odin_control_.last_cycle_ms = 0;  // act on the next odin task kick
  }
  if (std::isfinite(odin_max_lift_c))
    odin_control_.max_lift_c = odin_max_lift_c;
  auto copy = [](char *dst, size_t len, const char *src) {
    if (src == nullptr)
      return;
    std::strncpy(dst, src, len - 1);
    dst[len - 1] = '\0';
  };
  copy(odin_control_.forwarder_entity, sizeof(odin_control_.forwarder_entity), forwarder_entity);
  copy(generic_levers_.target_url_template, sizeof(generic_levers_.target_url_template), target_url_template);
  copy(generic_levers_.heat_request_url_template, sizeof(generic_levers_.heat_request_url_template),
       heat_request_url_template);
  copy(generic_levers_.curve_offset_url_template, sizeof(generic_levers_.curve_offset_url_template),
       curve_offset_url_template);
  if (std::isfinite(curve_gain))
    generic_levers_.curve_gain = curve_gain;
  if (std::isfinite(curve_max_offset_c))
    generic_levers_.curve_max_offset_c = curve_max_offset_c;
  generic_levers_.last_lever_write_ms = 0;  // re-send all levers
  const bool no_host = odin_plan_.odin_host[0] == '\0';
  give_state_lock_();
  save_odin_control_();
  kick_task_(odin_task_handle_);
  if (turned_on)
    log_event_("info", "odin", "Odin comfort control enabled");
  const int prefix = std::snprintf(response, capacity, "{\"result\":\"saved\",\"control\":");
  if (prefix > 0 && static_cast<size_t>(prefix) + 2 < capacity) {
    write_heat_source_control_json(response + prefix, capacity - static_cast<size_t>(prefix) - 1);
    const size_t len = std::strlen(response);
    if (len + 1 < capacity) {
      response[len] = '}';
      response[len + 1] = '\0';
    }
  }
  if (turned_on && no_host) {
    // Still saved; the UI shows the hint.
    log_event_("warn", "odin", "Odin comfort control needs an Odin host");
  }
  return true;
}

void LuneTouchCoordinator::write_odin_physics_json(char *buffer, size_t capacity) const {
  OdinPhysicsState phys{};
  ::lune_touch::StrategySnapshot strategy{};
  if (take_state_lock_(50)) {
    phys = odin_physics_;
    strategy = model_.strategy_snapshot();
    give_state_lock_();
  }
  char err[192];
  json_escape_(phys.last_error, err, sizeof(err));
  char ohl[16], ot1[16], ot2[16], os1[16], os2[16];
  char thl[16], ttau[16];
  json_float_token_(ohl, sizeof(ohl), phys.heat_loss_kw_per_k, 4);
  json_float_token_(ot1, sizeof(ot1), phys.tau_z1_h, 2);
  json_float_token_(ot2, sizeof(ot2), phys.tau_z2_h, 2);
  json_float_token_(os1, sizeof(os1), phys.passive_solar_z1, 3);
  json_float_token_(os2, sizeof(os2), phys.passive_solar_z2, 3);
  json_float_token_(thl, sizeof(thl),
                    strategy.ua_total_w_per_k > 0.0f ? strategy.ua_total_w_per_k / 1000.0f : NAN, 4);
  json_float_token_(ttau, sizeof(ttau), strategy.hl_tm_product, 2);
  char tua[16];
  json_float_token_(tua, sizeof(tua), strategy.ua_total_w_per_k, 1);
  std::snprintf(
      buffer, capacity,
      "{\"available\":%s,\"odin\":{\"heat_loss_kw_per_k\":%s,\"tau_z1_h\":%s,\"tau_z2_h\":%s,"
      "\"passive_solar_z1\":%s,\"passive_solar_z2\":%s,\"zone2_active\":%s,\"learned\":%s},"
      "\"touch\":{\"heat_loss_kw_per_k\":%s,\"tau_h\":%s,\"ua_total_w_per_k\":%s},"
      "\"flags\":{\"deviation_hl\":%s,\"deviation_tau\":%s,\"zone2_warning\":%s},"
      "\"note\":\"hl_tm_product is tau in hours; Touch does not export physics to Odin\","
      "\"last_error\":\"%s\"}",
      phys.available ? "true" : "false", ohl, ot1, ot2, os1, os2,
      phys.zone2_active ? "true" : "false", phys.learned ? "true" : "false", thl, ttau, tua,
      phys.deviation_hl ? "true" : "false", phys.deviation_tau ? "true" : "false",
      phys.zone2_warning ? "true" : "false", err);
}

// ---------------------------------------------------------------------------
// Energy price → Odin (energy_price.h). Fetch over HTTPS from Energi Data
// Service, compute the all-in consumer price, push €/kWh to Odin 2.0.
// Runs on the forecast (TLS) task; the poll task only decides when.
// ---------------------------------------------------------------------------

namespace {
constexpr const char *EDS_BASE = "https://api.energidataservice.dk/dataset/";
// Two local days of quarter-hour spot with three columns is ~19 KB; DataHub
// answers (8 records, selected columns) stay under 5 KB.
constexpr size_t PRICE_BODY_CAP = 32768;
constexpr uint32_t EDS_CALL_GAP_MS = 1200;  // the public API rate-limits bursts

struct PsramBody {
  char *ptr;
  explicit PsramBody(size_t capacity) : ptr(alloc_psram_body_(capacity)) {}
  ~PsramBody() {
    if (ptr != nullptr)
      heap_caps_free(ptr);
  }
  PsramBody(const PsramBody &) = delete;
  PsramBody &operator=(const PsramBody &) = delete;
};

void copy_str_(char *dst, size_t cap, const char *src) {
  if (dst == nullptr || cap == 0)
    return;
  std::strncpy(dst, src != nullptr ? src : "", cap - 1);
  dst[cap - 1] = '\0';
}
}  // namespace

bool LuneTouchCoordinator::local_today_(int *ymd, uint16_t *minute_of_day) const {
  if (time_ == nullptr || ymd == nullptr || minute_of_day == nullptr)
    return false;
  const auto now = time_->now();
  if (!now.is_valid())
    return false;
  *ymd = lune_touch_price::make_ymd(now.year, now.month, now.day_of_month);
  *minute_of_day = static_cast<uint16_t>(now.hour * 60 + now.minute);
  return true;
}

bool LuneTouchCoordinator::fetch_https_(const char *url, char *body, size_t capacity, int *status_out,
                                        char *error, size_t error_len) {
  if (error != nullptr && error_len > 0)
    error[0] = '\0';
  if (status_out != nullptr)
    *status_out = 0;
  if (body == nullptr || capacity < 2) {
    std::snprintf(error, error_len, "no_body_heap");
    return false;
  }
  body[0] = '\0';
  esp_http_client_config_t cfg{};
  cfg.url = url;
  cfg.method = HTTP_METHOD_GET;
  cfg.timeout_ms = 10000;
  cfg.disable_auto_redirect = true;
  cfg.crt_bundle_attach = esp_crt_bundle_attach;
  cfg.buffer_size = 2048;
  cfg.buffer_size_tx = 1024;  // long query strings (DataHub column list)
  esp_http_client_handle_t client = esp_http_client_init(&cfg);
  if (client == nullptr) {
    std::snprintf(error, error_len, "http_init_failed");
    return false;
  }
  esp_http_client_set_header(client, "Accept", "application/json");
  bool ok = false;
  const esp_err_t err = esp_http_client_open(client, 0);
  if (err == ESP_OK) {
    esp_http_client_fetch_headers(client);
    const int status = esp_http_client_get_status_code(client);
    if (status_out != nullptr)
      *status_out = status;
    const int len = esp_http_client_read_response(client, body, static_cast<int>(capacity - 1));
    if (len >= 0)
      body[len] = '\0';
    if (status != 200)
      std::snprintf(error, error_len, "http_%d", status);
    else if (len <= 0)
      std::snprintf(error, error_len, "empty_body");
    else if (static_cast<size_t>(len) >= capacity - 1)
      std::snprintf(error, error_len, "body_too_large");
    else
      ok = true;
  } else {
    std::snprintf(error, error_len, "%s", esp_err_to_name(err));
  }
  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  return ok;
}

bool LuneTouchCoordinator::fetch_datahub_series_(const char *gln, const char *codes_json, char *body,
                                                 size_t capacity, char *error, size_t error_len) {
  char filter[128];
  std::snprintf(filter, sizeof(filter), "{\"GLN_Number\":[\"%s\"],\"ChargeTypeCode\":[%s]}", gln, codes_json);
  char encoded[256];
  url_encode_(filter, encoded, sizeof(encoded));
  char url[640];
  size_t off = 0;
  appendf_(url, sizeof(url), off, "%sDatahubPricelist?filter=%s&columns=ChargeTypeCode,ValidFrom,ValidTo",
           EDS_BASE, encoded);
  for (unsigned h = 1; h <= lune_touch_price::HOURS; h++)
    appendf_(url, sizeof(url), off, ",Price%u", h);
  if (!appendf_(url, sizeof(url), off, "&sort=ValidFrom%%20desc&limit=%u",
                static_cast<unsigned>(lune_touch_price::MAX_TARIFF_RECORDS))) {
    std::snprintf(error, error_len, "url_too_long");
    return false;
  }
  int status = 0;
  return fetch_https_(url, body, capacity, &status, error, error_len);
}

void LuneTouchCoordinator::fail_price_cycle_(const char *error) {
  if (take_state_lock_(100)) {
    price_.sched.attempt_ok = false;
    copy_str_(price_.last_error, sizeof(price_.last_error), error);
    give_state_lock_();
  }
  char msg[96];
  std::snprintf(msg, sizeof(msg), "price push failed: %s", error != nullptr ? error : "unknown");
  log_event_("warn", "prices", msg);
}

void LuneTouchCoordinator::run_price_cycle_() {
  using namespace lune_touch_price;
  Config cfg{};
  bool run = false;
  uint8_t source = 0;
  Due due = Due::NONE;
  char odin_host[64]{};
  int today = 0;
  uint16_t minute = 0;
  const bool clock_ok = local_today_(&today, &minute);
  if (!take_state_lock_(200))
    return;
  cfg = price_.cfg;
  run = price_.run_pending;
  price_.run_pending = false;
  due = price_.due;
  source = price_.source_pending;
  copy_str_(odin_host, sizeof(odin_host), odin_plan_.odin_host);
  if (run) {
    // Stamp the attempt first: the poll task must not queue a second run
    // while this one is still fetching.
    if (clock_ok) {
      price_.sched.attempt_ymd = today;
      price_.sched.attempt_min = minute;
    }
    price_.sched.attempt_ok = false;
    price_.last_attempt_epoch = current_epoch_s_();
    copy_str_(price_.last_reason, sizeof(price_.last_reason), due_name(due));
  }
  give_state_lock_();

  // 1) Odin's price mode: "api" while Touch pushes, "energy_charts" after.
  if (source != 0) {
    const char *value = source == 1 ? "api" : "energy_charts";
    bool ok = false;
    int status = 0;
    if (odin_host[0] != '\0') {
      char body[72];
      std::snprintf(body, sizeof(body), "{\"key\":\"price_source\",\"value\":\"%s\"}", value);
      ok = post_odin_json_("/dashboard/set", body, &status);
      char msg[80];
      if (ok)
        std::snprintf(msg, sizeof(msg), "Odin price source set to %s", value);
      else
        std::snprintf(msg, sizeof(msg), "Odin price source write failed (HTTP %d)", status);
      log_event_(ok ? "info" : "warn", "prices", msg);
    }
    if (take_state_lock_(100)) {
      const uint32_t now_ms = esphome::millis();
      price_.source_last_try_ms = now_ms == 0 ? 1 : now_ms;
      if (ok) {
        if (price_.source_pending == source)
          price_.source_pending = 0;
        copy_str_(price_.odin_source, sizeof(price_.odin_source), value);
      }
      give_state_lock_();
    }
    if (run && source == 1 && !ok) {
      fail_price_cycle_(odin_host[0] == '\0' ? "no_odin_host" : "odin_unreachable");
      return;
    }
  }
  if (!run || !cfg.enabled)
    return;
  if (!clock_ok) {
    fail_price_cycle_("clock_invalid");
    return;
  }
  if (odin_host[0] == '\0') {
    fail_price_cycle_("no_odin_host");
    return;
  }
  if (cfg.area == Area::OFF) {
    fail_price_cycle_("spot_area_off");
    return;
  }
  if (price_rt_ == nullptr) {
    fail_price_cycle_("no_memory");
    return;
  }
  PriceRuntime &rt = *price_rt_;
  PsramBody body(PRICE_BODY_CAP);
  if (body.ptr == nullptr) {
    fail_price_cycle_("no_body_heap");
    return;
  }
  const int tomorrow = add_days(today, 1);
  char error[48];
  char err[40];

  // 2) Spot, today 00:00 → day after tomorrow 00:00 (local), 15-min records.
  {
    char start[12], end[12];
    format_ymd(today, start, sizeof(start));
    format_ymd(add_days(today, 2), end, sizeof(end));
    char url[384];
    std::snprintf(url, sizeof(url),
                  "%sDayAheadPrices?start=%sT00:00&end=%sT00:00"
                  "&filter=%%7B%%22PriceArea%%22%%3A%%5B%%22%s%%22%%5D%%7D"
                  "&columns=TimeDK,DayAheadPriceDKK,DayAheadPriceEUR&sort=TimeDK%%20asc&limit=400",
                  EDS_BASE, start, end, area_name(cfg.area));
    int status = 0;
    if (!fetch_https_(url, body.ptr, PRICE_BODY_CAP, &status, err, sizeof(err))) {
      std::snprintf(error, sizeof(error), "spot_%s", err);
      fail_price_cycle_(error);
      return;
    }
  }
  const size_t samples = parse_spot_records(body.ptr, std::strlen(body.ptr), rt.samples, MAX_SPOT_SAMPLES);
  const SpotDay spot0 = aggregate_spot_day(rt.samples, samples, today);
  const SpotDay spot1 = aggregate_spot_day(rt.samples, samples, tomorrow);
  const float fx = spot_fx(rt.samples, samples);
  if (!spot0.complete) {
    fail_price_cycle_("spot_incomplete");
    return;
  }
  if (take_state_lock_(100)) {
    price_.last_fetch_ok_ms = esphome::millis();
    give_state_lock_();
  }

  // 3) Grid tariff: DataHub price list, the user's schedule, or none.
  float grid0[HOURS]{};
  float grid1[HOURS]{};
  bool grid1_ok = true;
  bool grid_cache = false;
  if (cfg.grid_source == GridSource::SCHEDULE) {
    if (!expand_schedule(cfg.grid_schedule, grid0)) {
      fail_price_cycle_("grid_schedule_empty");
      return;
    }
    std::memcpy(grid1, grid0, sizeof(grid0));
  } else if (cfg.grid_source == GridSource::DATAHUB) {
    char key[48];
    std::snprintf(key, sizeof(key), "%s/%s", cfg.grid_gln, cfg.grid_code);
    char codes[32];
    std::snprintf(codes, sizeof(codes), "\"%s\"", cfg.grid_code);
    vTaskDelay(pdMS_TO_TICKS(EDS_CALL_GAP_MS));
    const bool fetched = fetch_datahub_series_(cfg.grid_gln, codes, body.ptr, PRICE_BODY_CAP, err, sizeof(err));
    if (fetched && parse_tariff_records(body.ptr, std::strlen(body.ptr), cfg.grid_code, &rt.scratch) > 0) {
      rt.grid = rt.scratch;
      rt.have_grid = true;
      copy_str_(rt.grid_key, sizeof(rt.grid_key), key);
    } else {
      if (fetched)
        copy_str_(err, sizeof(err), "no_records");
      grid_cache = true;
    }
    if (!rt.have_grid || std::strcmp(rt.grid_key, key) != 0) {
      std::snprintf(error, sizeof(error), "grid_%s", err);
      fail_price_cycle_(error);
      return;
    }
    if (!tariff_for_day(rt.grid, today, grid0)) {
      fail_price_cycle_("grid_no_tariff_today");
      return;
    }
    grid1_ok = tariff_for_day(rt.grid, tomorrow, grid1);
  }

  // 4) Energinet transmission + system tariff: DataHub or a fixed value.
  float en0[HOURS]{};
  float en1[HOURS]{};
  bool en1_ok = true;
  bool en_cache = false;
  if (cfg.energinet_source == EnerginetSource::FIXED) {
    for (size_t h = 0; h < HOURS; h++)
      en0[h] = en1[h] = cfg.energinet_fixed_dkk;
  } else {
    char codes[32];
    std::snprintf(codes, sizeof(codes), "\"%s\",\"%s\"", ENERGINET_TRANSMISSION_CODE, ENERGINET_SYSTEM_CODE);
    vTaskDelay(pdMS_TO_TICKS(EDS_CALL_GAP_MS));
    const bool fetched = fetch_datahub_series_(ENERGINET_GLN, codes, body.ptr, PRICE_BODY_CAP, err, sizeof(err));
    const size_t len = fetched ? std::strlen(body.ptr) : 0;
    if (fetched && parse_tariff_records(body.ptr, len, ENERGINET_TRANSMISSION_CODE, &rt.scratch) > 0 &&
        parse_tariff_records(body.ptr, len, ENERGINET_SYSTEM_CODE, &rt.scratch2) > 0) {
      rt.energinet_transmission = rt.scratch;
      rt.energinet_system = rt.scratch2;
      rt.have_energinet = true;
    } else {
      if (fetched)
        copy_str_(err, sizeof(err), "no_records");
      en_cache = true;
    }
    if (!rt.have_energinet) {
      std::snprintf(error, sizeof(error), "energinet_%s", err);
      fail_price_cycle_(error);
      return;
    }
    float sys[HOURS];
    if (!tariff_for_day(rt.energinet_transmission, today, en0) ||
        !tariff_for_day(rt.energinet_system, today, sys)) {
      fail_price_cycle_("energinet_no_tariff_today");
      return;
    }
    for (size_t h = 0; h < HOURS; h++)
      en0[h] += sys[h];
    en1_ok = tariff_for_day(rt.energinet_transmission, tomorrow, en1) &&
             tariff_for_day(rt.energinet_system, tomorrow, sys);
    if (en1_ok)
      for (size_t h = 0; h < HOURS; h++)
        en1[h] += sys[h];
  }

  // 5) All-in price → €/kWh array → Odin.
  rt.next_today = build_day(today, spot0.dkk_kwh, grid0, en0, cfg);
  rt.next_tomorrow = spot1.complete && grid1_ok && en1_ok ? build_day(tomorrow, spot1.dkk_kwh, grid1, en1, cfg)
                                                           : DayPrices{};
  float eur[2 * HOURS];
  const size_t count = build_odin_array(rt.next_today, rt.next_tomorrow, fx, eur, 2 * HOURS);
  if (count < HOURS) {
    fail_price_cycle_("price_build_failed");
    return;
  }
  if (format_odin_payload(eur, count, rt.payload, sizeof(rt.payload)) == 0) {
    fail_price_cycle_("payload_too_large");
    return;
  }
  int odin_status = 0;
  const bool pushed = post_odin_json_("/api/data/prices", rt.payload, &odin_status);
  if (take_state_lock_(200)) {
    rt.today = rt.next_today;
    rt.tomorrow = rt.next_tomorrow;
    price_.fx = fx;
    price_.grid_from_cache = grid_cache;
    price_.energinet_from_cache = en_cache;
    if (pushed) {
      price_.sched.pushed_ymd = today;
      price_.sched.pushed_tomorrow = count >= 2 * HOURS;
      price_.sched.attempt_ok = true;
      price_.last_push_ok_epoch = current_epoch_s_();
      price_.hours_pushed = static_cast<uint8_t>(count);
      price_.last_error[0] = '\0';
    }
    give_state_lock_();
  }
  if (!pushed) {
    std::snprintf(error, sizeof(error), "odin_http_%d", odin_status);
    fail_price_cycle_(error);
    return;
  }
  char msg[96];
  std::snprintf(msg, sizeof(msg), "prices pushed to Odin: %u h (%s)%s", static_cast<unsigned>(count),
                due_name(due), grid_cache || en_cache ? ", cached tariff" : "");
  log_event_("info", "prices", msg);
}

void LuneTouchCoordinator::load_price_settings_() {
  using namespace lune_touch_price;
  price_.cfg = Config{};
  price_.cfg.grid_schedule = default_grid_schedule();
  nvs_handle_t handle;
  if (nvs_open(SETTINGS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK)
    return;
  Config &cfg = price_.cfg;
  uint8_t u8 = 0;
  if (nvs_get_u8(handle, "pr_en", &u8) == ESP_OK)
    cfg.enabled = u8 != 0;
  if (nvs_get_u8(handle, "pr_area", &u8) == ESP_OK && u8 <= 2)
    cfg.area = static_cast<Area>(u8);
  if (nvs_get_u8(handle, "pr_gsrc", &u8) == ESP_OK && u8 <= 2)
    cfg.grid_source = static_cast<GridSource>(u8);
  if (nvs_get_u8(handle, "pr_esrc", &u8) == ESP_OK && u8 <= 1)
    cfg.energinet_source = static_cast<EnerginetSource>(u8);
  char text[24];
  size_t len = sizeof(text);
  if (nvs_get_str(handle, "pr_gln", text, &len) == ESP_OK && valid_gln(text))
    copy_str_(cfg.grid_gln, sizeof(cfg.grid_gln), text);
  len = sizeof(text);
  if (nvs_get_str(handle, "pr_gcode", text, &len) == ESP_OK && valid_charge_code(text))
    copy_str_(cfg.grid_code, sizeof(cfg.grid_code), text);
  float nums[4]{};
  len = sizeof(nums);
  if (nvs_get_blob(handle, "pr_nums", nums, &len) == ESP_OK && len == sizeof(nums)) {
    if (std::isfinite(nums[0])) cfg.energinet_fixed_dkk = nums[0];
    if (std::isfinite(nums[1])) cfg.elafgift_dkk = nums[1];
    if (std::isfinite(nums[2])) cfg.markup_dkk = nums[2];
    if (std::isfinite(nums[3])) cfg.vat_pct = nums[3];
  }
  char *sched = psram_scratch_<char>(640);
  if (sched != nullptr) {
    len = 640;
    Schedule parsed{};
    if (nvs_get_str(handle, "pr_gsched", sched, &len) == ESP_OK && parse_schedule(sched, &parsed))
      cfg.grid_schedule = parsed;
    heap_caps_free(sched);
  }
  nvs_close(handle);
}

void LuneTouchCoordinator::save_price_settings_() {
  lune_touch_price::Config cfg{};
  if (!take_state_lock_(100))
    return;
  cfg = price_.cfg;
  give_state_lock_();
  static char *sched = psram_scratch_<char>(640);
  if (sched == nullptr)
    return;
  sched[0] = '\0';
  lune_touch_price::format_schedule(cfg.grid_schedule, sched, 640);
  const float nums[4]{cfg.energinet_fixed_dkk, cfg.elafgift_dkk, cfg.markup_dkk, cfg.vat_pct};
  nvs_handle_t handle;
  if (nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK)
    return;
  esp_err_t err = nvs_set_u8(handle, "pr_en", cfg.enabled ? 1 : 0);
  if (err == ESP_OK) err = nvs_set_u8(handle, "pr_area", static_cast<uint8_t>(cfg.area));
  if (err == ESP_OK) err = nvs_set_u8(handle, "pr_gsrc", static_cast<uint8_t>(cfg.grid_source));
  if (err == ESP_OK) err = nvs_set_u8(handle, "pr_esrc", static_cast<uint8_t>(cfg.energinet_source));
  if (err == ESP_OK) err = nvs_set_str(handle, "pr_gln", cfg.grid_gln);
  if (err == ESP_OK) err = nvs_set_str(handle, "pr_gcode", cfg.grid_code);
  if (err == ESP_OK) err = nvs_set_blob(handle, "pr_nums", nums, sizeof(nums));
  if (err == ESP_OK) err = nvs_set_str(handle, "pr_gsched", sched);
  if (err == ESP_OK) err = nvs_commit(handle);
  nvs_close(handle);
  if (err != ESP_OK)
    ESP_LOGW(TAG, "Price settings save failed: %s", esp_err_to_name(err));
}

bool LuneTouchCoordinator::set_price_settings(const PriceSettingsUpdate &u, char *response, size_t capacity) {
  using namespace lune_touch_price;
  auto reject = [&](const char *code) {
    std::snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"%s\"}", code);
    return false;
  };
  auto given = [](const char *s) { return s != nullptr && s[0] != '\0'; };
  Area area{};
  GridSource grid{};
  EnerginetSource energinet{};
  if (given(u.area) && !parse_area(u.area, &area))
    return reject("invalid_area");
  if (given(u.grid_source) && !parse_grid_source(u.grid_source, &grid))
    return reject("invalid_grid_source");
  if (given(u.energinet_source) && !parse_energinet_source(u.energinet_source, &energinet))
    return reject("invalid_energinet_source");
  if (given(u.grid_gln) && !valid_gln(u.grid_gln))
    return reject("invalid_gln");
  if (given(u.grid_code) && !valid_charge_code(u.grid_code))
    return reject("invalid_charge_code");
  Schedule schedule{};
  if (given(u.grid_schedule) && !parse_schedule(u.grid_schedule, &schedule))
    return reject("invalid_schedule");
  auto bad = [](float v, float lo, float hi) { return !std::isnan(v) && !(v >= lo && v <= hi); };
  if (bad(u.energinet_fixed_dkk, -1.0f, 5.0f) || bad(u.elafgift_dkk, 0.0f, 5.0f) ||
      bad(u.markup_dkk, -1.0f, 5.0f) || bad(u.vat_pct, 0.0f, 50.0f))
    return reject("invalid_value");

  bool was_enabled = false;
  bool enabled = false;
  if (!take_state_lock_(200))
    return reject("busy");
  Config &cfg = price_.cfg;
  was_enabled = cfg.enabled;
  if (u.has_enabled) cfg.enabled = u.enabled;
  if (given(u.area)) cfg.area = area;
  if (given(u.grid_source)) cfg.grid_source = grid;
  if (given(u.energinet_source)) cfg.energinet_source = energinet;
  if (given(u.grid_gln)) copy_str_(cfg.grid_gln, sizeof(cfg.grid_gln), u.grid_gln);
  if (given(u.grid_code)) copy_str_(cfg.grid_code, sizeof(cfg.grid_code), u.grid_code);
  if (given(u.grid_schedule)) cfg.grid_schedule = schedule;
  if (!std::isnan(u.energinet_fixed_dkk)) cfg.energinet_fixed_dkk = u.energinet_fixed_dkk;
  if (!std::isnan(u.elafgift_dkk)) cfg.elafgift_dkk = u.elafgift_dkk;
  if (!std::isnan(u.markup_dkk)) cfg.markup_dkk = u.markup_dkk;
  if (!std::isnan(u.vat_pct)) cfg.vat_pct = u.vat_pct;
  enabled = cfg.enabled;
  if (enabled != was_enabled) {
    price_.source_pending = enabled ? 1 : 2;
    price_.source_last_try_ms = 0;
  }
  // New settings take effect with a fresh push (next poll pass, ≤ 15 s).
  price_.push_requested = enabled;
  if (!enabled) {
    price_.sched = SchedulerState{};
    price_.hours_pushed = 0;
  }
  give_state_lock_();
  save_price_settings_();
  log_event_("info", "prices",
             enabled == was_enabled ? "price settings saved"
                                    : (enabled ? "price push to Odin enabled" : "price push to Odin disabled"));
  std::snprintf(response, capacity, "{\"result\":\"saved\",\"enabled\":%s,\"push_queued\":%s}",
                enabled ? "true" : "false", enabled ? "true" : "false");
  return true;
}

bool LuneTouchCoordinator::request_price_push(char *response, size_t capacity) {
  bool enabled = false;
  bool host = false;
  if (!take_state_lock_(200)) {
    std::snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"busy\"}");
    return false;
  }
  enabled = price_.cfg.enabled;
  host = odin_plan_.odin_host[0] != '\0';
  if (enabled && host)
    price_.push_requested = true;
  give_state_lock_();
  if (!enabled || !host) {
    std::snprintf(response, capacity, "{\"result\":\"rejected\",\"error\":\"%s\"}",
                  enabled ? "no_odin_host" : "disabled");
    return false;
  }
  kick_task_(poll_task_handle_);
  std::snprintf(response, capacity, "{\"result\":\"queued\"}");
  return true;
}

void LuneTouchCoordinator::write_prices_json(char *buffer, size_t capacity) const {
  using namespace lune_touch_price;
  if (buffer == nullptr || capacity == 0)
    return;
  int today = 0;
  uint16_t minute = 0;
  const bool clock_ok = local_today_(&today, &minute);
  if (!take_state_lock_(100)) {
    std::snprintf(buffer, capacity, "{\"available\":false}");
    return;
  }
  const PriceState &p = price_;
  const Config &c = p.cfg;
  const uint32_t now_ms = esphome::millis();
  char sched[640];
  format_schedule(c.grid_schedule, sched, sizeof(sched));
  if (sched[0] == '\0')
    copy_str_(sched, sizeof(sched), "[]");
  const char *state = !c.enabled                                    ? "disabled"
                      : p.run_pending                                ? "running"
                      : (p.sched.attempt_ymd != 0 && !p.sched.attempt_ok && p.last_error[0] != '\0')
                          ? "error"
                      : (clock_ok && p.sched.pushed_ymd == today)    ? "ok"
                                                                     : "waiting";
  char err[96];
  json_escape_(p.last_error, err, sizeof(err));
  size_t off = 0;
  appendf_(buffer, capacity, off,
           "{\"available\":true,\"enabled\":%s,\"area\":\"%s\","
           "\"grid\":{\"source\":\"%s\",\"gln\":\"%s\",\"code\":\"%s\",\"schedule\":%s},"
           "\"energinet\":{\"source\":\"%s\",\"fixed_dkk\":%.4f},"
           "\"elafgift_dkk\":%.4f,\"markup_dkk\":%.4f,\"vat_pct\":%.2f,\"odin_host_set\":%s,",
           c.enabled ? "true" : "false", area_name(c.area), grid_source_name(c.grid_source), c.grid_gln,
           c.grid_code, sched, energinet_source_name(c.energinet_source),
           static_cast<double>(c.energinet_fixed_dkk), static_cast<double>(c.elafgift_dkk),
           static_cast<double>(c.markup_dkk), static_cast<double>(c.vat_pct),
           odin_plan_.odin_host[0] != '\0' ? "true" : "false");
  char fetch_age[16] = "null";
  if (p.last_fetch_ok_ms != 0)
    std::snprintf(fetch_age, sizeof(fetch_age), "%lu", static_cast<unsigned long>((now_ms - p.last_fetch_ok_ms) / 1000UL));
  char fx[16];
  json_float_token_(fx, sizeof(fx), p.fx, 6);
  appendf_(buffer, capacity, off,
           "\"status\":{\"state\":\"%s\",\"reason\":\"%s\",\"odin_source\":\"%s\",\"source_pending\":%s,"
           "\"last_fetch_age_s\":%s,\"last_push_epoch\":%lld,\"last_attempt_epoch\":%lld,"
           "\"hours_pushed\":%u,\"fx\":%s,\"grid_from_cache\":%s,\"energinet_from_cache\":%s,"
           "\"last_error\":\"%s\"},",
           state, p.last_reason, p.odin_source, p.source_pending != 0 ? "true" : "false", fetch_age,
           static_cast<long long>(p.last_push_ok_epoch), static_cast<long long>(p.last_attempt_epoch),
           static_cast<unsigned>(p.hours_pushed), fx, p.grid_from_cache ? "true" : "false",
           p.energinet_from_cache ? "true" : "false", err);
  auto write_day = [&](const char *name, const DayPrices *d) {
    if (d == nullptr || !d->valid) {
      appendf_(buffer, capacity, off, "\"%s\":null", name);
      return;
    }
    char date[12];
    format_ymd(d->ymd, date, sizeof(date));
    appendf_(buffer, capacity, off, "\"%s\":{\"date\":\"%s\"", name, date);
    const struct {
      const char *key;
      const float *v;
    } series[] = {{"spot", d->spot}, {"grid", d->grid}, {"energinet", d->energinet}, {"total", d->total}};
    for (const auto &s : series) {
      appendf_(buffer, capacity, off, ",\"%s\":[", s.key);
      for (size_t h = 0; h < HOURS; h++)
        appendf_(buffer, capacity, off, "%s%.4f", h ? "," : "", static_cast<double>(s.v[h]));
      appendf_(buffer, capacity, off, "]");
    }
    appendf_(buffer, capacity, off, "}");
  };
  // Breakdown only for the current local day (yesterday's "tomorrow" after midnight).
  const DayPrices *d_today = nullptr;
  const DayPrices *d_tomorrow = nullptr;
  if (price_rt_ != nullptr && clock_ok) {
    if (price_rt_->today.valid && price_rt_->today.ymd == today) {
      d_today = &price_rt_->today;
      d_tomorrow = &price_rt_->tomorrow;
    } else if (price_rt_->tomorrow.valid && price_rt_->tomorrow.ymd == today) {
      d_today = &price_rt_->tomorrow;
    }
  }
  write_day("today", d_today);
  appendf_(buffer, capacity, off, ",");
  write_day("tomorrow", d_tomorrow);
  appendf_(buffer, capacity, off, "}");
  give_state_lock_();
}

}  // namespace lune_touch_coordinator
}  // namespace esphome
