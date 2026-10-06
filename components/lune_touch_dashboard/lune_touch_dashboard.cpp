#include "lune_touch_dashboard.h"

#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include <ArduinoJson.h>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <limits>
#if defined(USE_ESP32)
#include <esp_heap_caps.h>
#endif
#include "esphome/components/wifi/wifi_component.h"
#ifdef USE_LUNE_WIFI
#include "esphome/components/lune_wifi/lune_wifi.h"
#endif
#ifdef USE_CAPTIVE_PORTAL
#include "esphome/components/captive_portal/captive_portal.h"
#endif

namespace esphome {
namespace lune_touch_dashboard {

static const char *const TAG = "lune_touch_dashboard";
static constexpr const char API_PREFIX[] = "/api/lune-touch/v1";
static constexpr size_t API_PREFIX_LEN = sizeof(API_PREFIX) - 1;
static constexpr const char CORS_ALLOW_METHODS[] = "GET, POST, OPTIONS";
static constexpr const char CORS_ALLOW_HEADERS[] = "Content-Type";
// Keep flash reads brief. The RGB framebuffer is in PSRAM and both memories
// share the SPI bus on ESP32-S3, so sending a large PROGMEM range directly can
// starve the RGB bounce-buffer refill while a browser loads the dashboard.
static constexpr size_t STATIC_CHUNK_SIZE = 1024;

namespace {

// appendf_ truncates by filling the buffer and null-terminating; the result is
// still C-string-safe but no longer valid JSON (often mid-string). Reject that
// before wrapping it in the v1 envelope so the browser never sees unterminated JSON.
bool json_payload_complete_(const char *data) {
  if (data == nullptr || data[0] == '\0')
    return false;
  size_t end = std::strlen(data);
  while (end > 0) {
    const unsigned char c = static_cast<unsigned char>(data[end - 1]);
    if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
      break;
    --end;
  }
  if (end == 0)
    return false;
  const char first = data[0];
  const char last = data[end - 1];
  return (first == '{' && last == '}') || (first == '[' && last == ']');
}

constexpr const char kBuffersMissingJson[] =
    "{\"ok\":false,\"version\":\"v1\",\"ts_ms\":0,\"error\":{\"code\":\"unavailable\",\"message\":\"Dashboard buffers unavailable\"}}";

int hex_to_nibble(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

std::string url_decode(const char *value, size_t len) {
  std::string out;
  out.reserve(len);
  for (size_t i = 0; i < len; i++) {
    if (value[i] == '+') {
      out.push_back(' ');
    } else if (value[i] == '%' && i + 2 < len) {
      const int high = hex_to_nibble(value[i + 1]);
      const int low = hex_to_nibble(value[i + 2]);
      if (high >= 0 && low >= 0) {
        out.push_back(static_cast<char>((high << 4) | low));
        i += 2;
      } else {
        out.push_back(value[i]);
      }
    } else {
      out.push_back(value[i]);
    }
  }
  return out;
}

bool query_value_from(const char *query, const char *name, std::string *out) {
  if (query == nullptr || name == nullptr || out == nullptr)
    return false;
  const size_t name_len = strlen(name);
  const char *cursor = query;
  while (*cursor != '\0') {
    const char *part_end = strchr(cursor, '&');
    if (part_end == nullptr)
      part_end = cursor + strlen(cursor);
    const char *equals = static_cast<const char *>(memchr(cursor, '=', part_end - cursor));
    const size_t key_len = equals ? static_cast<size_t>(equals - cursor) : static_cast<size_t>(part_end - cursor);
    if (key_len == name_len && strncmp(cursor, name, name_len) == 0) {
      if (equals == nullptr)
        *out = "";
      else
        *out = url_decode(equals + 1, static_cast<size_t>(part_end - equals - 1));
      return true;
    }
    cursor = *part_end == '&' ? part_end + 1 : part_end;
  }
  return false;
}

bool path_equals(const char *url, const char *path) {
  if (url == nullptr || path == nullptr)
    return false;
  const size_t path_len = strlen(path);
  return strncmp(url, path, path_len) == 0 && (url[path_len] == '\0' || url[path_len] == '?' || url[path_len] == '/');
}

bool is_ui_asset_url(const char *url) {
  return path_equals(url, "/lune-ui.css") || path_equals(url, "/ui.js") || path_equals(url, "/en") ||
         path_equals(url, "/da") || path_equals(url, "/en/") || path_equals(url, "/da/") ||
         path_equals(url, "/dashboard") || path_equals(url, "/dashboard/");
}

bool lang_is_da(const char *cookie, const char *accept_language) {
  if (cookie != nullptr) {
    const char *c = strstr(cookie, "lune_lang=");
    if (c != nullptr && strncmp(c + 10, "da", 2) == 0)
      return true;
    if (c != nullptr && strncmp(c + 10, "en", 2) == 0)
      return false;
  }
  if (accept_language != nullptr) {
    const char *da = strstr(accept_language, "da");
    const char *en = strstr(accept_language, "en");
    if (da != nullptr && (en == nullptr || da < en))
      return true;
  }
  return false;
}

std::string api_arg(const ApiRequest &api, const char *name) {
  if (api.async != nullptr)
    return api.async->arg(name);
  std::string value;
  if (query_value_from(api.form_body, name, &value))
    return value;
  if (query_value_from(api.query, name, &value))
    return value;
  return {};
}

bool parse_float_arg(const ApiRequest &api, const char *name, float *out) {
  const std::string value = api_arg(api, name);
  if (value.empty())
    return false;
  char *end = nullptr;
  const float parsed = strtof(value.c_str(), &end);
  if (end == value.c_str() || *end != '\0')
    return false;
  *out = parsed;
  return true;
}

bool json_get_str(const JsonDocument *doc, const char *field, char *out, size_t out_len) {
  if (doc == nullptr || field == nullptr || out == nullptr || out_len == 0)
    return false;
  JsonVariantConst value = (*doc)[field];
  if (!value.is<const char *>())
    return false;
  strncpy(out, value.as<const char *>(), out_len - 1);
  out[out_len - 1] = '\0';
  return true;
}

bool json_text_get_str(const char *body, const char *field, char *out, size_t out_len) {
  if (body == nullptr || body[0] == '\0')
    return false;
  JsonDocument doc;
  if (deserializeJson(doc, body))
    return false;
  return json_get_str(&doc, field, out, out_len);
}

bool json_get_float(const JsonDocument *doc, const char *field, float *out) {
  if (doc == nullptr || field == nullptr || out == nullptr)
    return false;
  JsonVariantConst value = (*doc)[field];
  if (!value.is<float>() && !value.is<int>() && !value.is<unsigned>())
    return false;
  *out = value.as<float>();
  return true;
}

bool json_get_uint(const JsonDocument *doc, const char *field, uint32_t *out) {
  if (doc == nullptr || field == nullptr || out == nullptr)
    return false;
  JsonVariantConst value = (*doc)[field];
  if (value.is<bool>()) {
    *out = value.as<bool>() ? 1U : 0U;
    return true;
  }
  if (!value.is<unsigned>() && !value.is<int>() && !value.is<float>())
    return false;
  const float parsed = value.as<float>();
  if (parsed < 0.0f)
    return false;
  *out = static_cast<uint32_t>(parsed);
  return true;
}

bool parse_float_param(const ApiRequest &api, const JsonDocument *body, const char *name, float *out) {
  if (parse_float_arg(api, name, out))
    return true;
  return json_get_float(body, name, out);
}

bool parse_uint_arg(const ApiRequest &api, const char *name, uint32_t *out) {
  const std::string value = api_arg(api, name);
  if (value.empty())
    return false;
  char *end = nullptr;
  const unsigned long parsed = strtoul(value.c_str(), &end, 10);
  if (end == value.c_str() || *end != '\0')
    return false;
  *out = static_cast<uint32_t>(parsed);
  return true;
}

bool parse_uint_param(const ApiRequest &api, const JsonDocument *body, const char *name, uint32_t *out) {
  if (parse_uint_arg(api, name, out))
    return true;
  return json_get_uint(body, name, out);
}

bool parse_size_param(const ApiRequest &api, const JsonDocument *body, const char *name, size_t *out) {
  uint32_t value = 0;
  if (!parse_uint_param(api, body, name, &value))
    return false;
  *out = static_cast<size_t>(value);
  return true;
}

void parse_text_param(const ApiRequest &api, const JsonDocument *body, const char *name,
                      char *out, size_t out_len) {
  if (out_len == 0)
    return;
  const std::string arg = api_arg(api, name);
  if (!arg.empty()) {
    strncpy(out, arg.c_str(), out_len - 1);
    out[out_len - 1] = '\0';
    return;
  }
  if (json_get_str(body, name, out, out_len))
    return;
  out[0] = '\0';
}

bool parse_node_trust_param(const ApiRequest &api, const JsonDocument *body,
                            ::lune_touch::NodeTrust *out) {
  char trust[20];
  parse_text_param(api, body, "trust", trust, sizeof(trust));
  if (strcmp(trust, "trusted") == 0 || strcmp(trust, "2") == 0) {
    *out = ::lune_touch::NodeTrust::TRUSTED;
    return true;
  }
  if (strcmp(trust, "paired") == 0 || strcmp(trust, "1") == 0) {
    *out = ::lune_touch::NodeTrust::PAIRED;
    return true;
  }
  return false;
}

bool extract_middle_segment(const char *path, const char *prefix, const char *suffix,
                            char *out, size_t out_len) {
  if (path == nullptr || prefix == nullptr || suffix == nullptr || out_len == 0)
    return false;
  const size_t prefix_len = strlen(prefix);
  const size_t suffix_len = strlen(suffix);
  const size_t path_len = strlen(path);
  if (strncmp(path, prefix, prefix_len) != 0 || path_len < prefix_len + suffix_len)
    return false;
  if (strcmp(path + path_len - suffix_len, suffix) != 0)
    return false;
  const size_t segment_len = path_len - prefix_len - suffix_len;
  if (segment_len == 0 || segment_len >= out_len)
    return false;
  memcpy(out, path + prefix_len, segment_len);
  out[segment_len] = '\0';
  return true;
}

const char *http_status_line(int code) {
  switch (code) {
    case 200:
      return "200 OK";
    case 204:
      return "204 No Content";
    case 400:
      return "400 Bad Request";
    case 404:
      return "404 Not Found";
    case 405:
      return "405 Method Not Allowed";
    case 409:
      return "409 Conflict";
    case 411:
      return "411 Length Required";
    case 413:
      return "413 Payload Too Large";
    case 503:
      return "503 Service Unavailable";
    case 507:
      return "507 Insufficient Storage";
    default:
      return "500 Internal Server Error";
  }
}

bool content_type_contains(httpd_req_t *request, const char *needle) {
  if (request == nullptr || needle == nullptr)
    return false;
  const size_t len = httpd_req_get_hdr_value_len(request, "Content-Type");
  if (len == 0)
    return false;
  std::string value;
  value.resize(len + 1);
  if (httpd_req_get_hdr_value_str(request, "Content-Type", &value[0], value.size()) != ESP_OK)
    return false;
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value.find(needle) != std::string::npos;
}

char first_non_space(const std::string &body) {
  for (char c : body) {
    if (!std::isspace(static_cast<unsigned char>(c)))
      return c;
  }
  return '\0';
}

void add_cors_headers(AsyncWebServerResponse *response) {
  if (response == nullptr)
    return;
  response->addHeader("Access-Control-Allow-Origin", "*");
  response->addHeader("Access-Control-Allow-Methods", CORS_ALLOW_METHODS);
  response->addHeader("Access-Control-Allow-Headers", CORS_ALLOW_HEADERS);
}

void add_cors_headers(httpd_req_t *request) {
  if (request == nullptr)
    return;
  httpd_resp_set_hdr(request, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(request, "Access-Control-Allow-Methods", CORS_ALLOW_METHODS);
  httpd_resp_set_hdr(request, "Access-Control-Allow-Headers", CORS_ALLOW_HEADERS);
}

}  // namespace

static const char DASHBOARD_FALLBACK_HTML[] =
    "<!doctype html><html><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>Lune Touch</title></head><body>"
    "<p>Lune Touch UI is not embedded. Run <code>make touch-ui</code> and rebuild.</p>"
    "</body></html>";

void LuneTouchDashboard::setup() {
  if (base_ == nullptr) {
    ESP_LOGE(TAG, "web_server_base is null; dashboard handler not registered");
    return;
  }
#if defined(USE_ESP32)
  api_buf_lock_ = xSemaphoreCreateMutex();
  if (api_buf_lock_ == nullptr) {
    ESP_LOGE(TAG, "Failed to create API buffer mutex");
    return;
  }
  auto alloc_buf = [](size_t n) -> char * {
    void *p = heap_caps_calloc(1, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (p == nullptr)
      p = heap_caps_calloc(1, n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    return static_cast<char *>(p);
  };
  data_buf_ = alloc_buf(DATA_BUF_SIZE);
  response_buf_ = alloc_buf(RESPONSE_BUF_SIZE);
  if (data_buf_ == nullptr || response_buf_ == nullptr) {
    ESP_LOGE(TAG, "Failed to allocate dashboard buffers (data=%p response=%p free_psram=%u)",
             static_cast<void *>(data_buf_), static_cast<void *>(response_buf_),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
    return;
  }
#else
  data_buf_ = static_cast<char *>(calloc(1, DATA_BUF_SIZE));
  response_buf_ = static_cast<char *>(calloc(1, RESPONSE_BUF_SIZE));
  if (data_buf_ == nullptr || response_buf_ == nullptr) {
    ESP_LOGE(TAG, "Failed to allocate dashboard buffers");
    return;
  }
#endif
  base_->init();
  base_->add_handler(this);
#if USE_ESP32
  // web_server_base::init() can leave a live AsyncWebServer object whose
  // httpd_start() failed (common under LVGL heap pressure). Retry once and
  // surface the miss — otherwise port 80 stays closed while we log success.
  if (base_->get_server() != nullptr && base_->get_server()->get_server() == nullptr) {
    ESP_LOGW(TAG, "HTTP listener missing after init (free heap=%u); retrying begin()",
             static_cast<unsigned>(esp_get_free_heap_size()));
    base_->get_server()->begin();
  }
  if (base_->get_server() != nullptr && base_->get_server()->get_server() != nullptr) {
    auto *server = base_->get_server()->get_server();
    httpd_unregister_uri_handler(server, "", HTTP_POST);
    const httpd_uri_t handler_post = {
        .uri = "",
        .method = HTTP_POST,
        .handler = LuneTouchDashboard::raw_post_handler_,
        .user_ctx = this,
    };
    if (httpd_register_uri_handler(server, &handler_post) != ESP_OK)
      ESP_LOGW(TAG, "Failed to register raw API POST handler");
    ESP_LOGI(TAG, "Dashboard endpoints registered: /, /dashboard.js, /api/lune-touch/v1/*");
  } else {
    ESP_LOGE(TAG, "HTTP server not listening — dashboard unreachable on port 80 (free heap=%u)",
             static_cast<unsigned>(esp_get_free_heap_size()));
  }
#else
  ESP_LOGI(TAG, "Dashboard endpoints registered: /, /dashboard.js, /api/lune-touch/v1/*");
#endif
}

bool LuneTouchDashboard::lock_api_buffers_(uint32_t timeout_ms) {
#if defined(USE_ESP32)
  if (api_buf_lock_ == nullptr)
    return false;
  return xSemaphoreTake(api_buf_lock_, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
#else
  (void) timeout_ms;
  return true;
#endif
}

void LuneTouchDashboard::unlock_api_buffers_() {
#if defined(USE_ESP32)
  if (api_buf_lock_ != nullptr)
    xSemaphoreGive(api_buf_lock_);
#endif
}

void LuneTouchDashboard::write_wifi_json_(char *out, size_t capacity) {
  JsonDocument doc;
  auto *wifi = wifi::global_wifi_component;
  char ssid[wifi::SSID_BUFFER_SIZE]{};
  if (wifi != nullptr && wifi->is_connected())
    wifi->wifi_ssid_to(ssid);
  doc["ssid"] = ssid;
  doc["connected"] = wifi != nullptr && wifi->is_connected();
  doc["ap_active"] = wifi != nullptr && wifi->is_ap_active();
#ifdef USE_LUNE_WIFI
  if (auto *lw = lune_wifi::global_lune_wifi) {
    doc["switch"] = lw->result_label();
    doc["target_ssid"] = lw->target_ssid();
  }
#endif
  if (serializeJson(doc, out, capacity) >= capacity && capacity > 2)
    std::snprintf(out, capacity, "{}");
}

bool LuneTouchDashboard::canHandle(AsyncWebServerRequest *request) const {
  char url_buf[AsyncWebServerRequest::URL_BUF_SIZE];
  auto url = request->url_to(url_buf);
#ifdef USE_CAPTIVE_PORTAL
  // In the fallback AP the stock captive portal (network list + password form)
  // must win "/"; it is registered after this handler.
  if (url == "/" && captive_portal::global_captive_portal != nullptr &&
      captive_portal::global_captive_portal->is_active())
    return false;
#endif
  if (url == "/" || is_ui_asset_url(url.c_str()))
    return true;
  return strncmp(url.c_str(), API_PREFIX, API_PREFIX_LEN) == 0 &&
         (url.c_str()[API_PREFIX_LEN] == '/' || url.c_str()[API_PREFIX_LEN] == '\0');
}

void LuneTouchDashboard::handleRequest(AsyncWebServerRequest *request) {
  char url_buf[AsyncWebServerRequest::URL_BUF_SIZE];
  auto url = request->url_to(url_buf);
  if (url == "/" || url == "/dashboard" || url == "/dashboard/") {
    handle_root_(request);
    return;
  }
  if (is_ui_asset_url(url.c_str())) {
    handle_ui_asset_(request, url.c_str());
    return;
  }
  if (strncmp(url.c_str(), API_PREFIX, API_PREFIX_LEN) == 0) {
    const char *path = url.c_str() + API_PREFIX_LEN;
    handle_v1_(request, *path ? path : "/");
    return;
  }
  send_text_(request, 404, "text/plain", "Not found");
}

void LuneTouchDashboard::handle_root_(AsyncWebServerRequest *request) {
#ifdef LUNE_TOUCH_HAS_UI
  optional<std::string> cookie = request != nullptr ? request->get_header("Cookie") : optional<std::string>{};
  optional<std::string> accept =
      request != nullptr ? request->get_header("Accept-Language") : optional<std::string>{};
  const bool da = lang_is_da(cookie.has_value() ? cookie->c_str() : nullptr,
                             accept.has_value() ? accept->c_str() : nullptr);
  if (da) {
    send_gzip_chunked_(request, "text/html; charset=utf-8", LUNE_TOUCH_UI_DA_DATA, LUNE_TOUCH_UI_DA_SIZE,
                       "no-store, no-cache, max-age=0, must-revalidate");
  } else {
    send_gzip_chunked_(request, "text/html; charset=utf-8", LUNE_TOUCH_UI_EN_DATA, LUNE_TOUCH_UI_EN_SIZE,
                       "no-store, no-cache, max-age=0, must-revalidate");
  }
#else
  send_text_(request, 200, "text/html; charset=utf-8", DASHBOARD_FALLBACK_HTML, false,
             "no-store, no-cache, max-age=0, must-revalidate");
#endif
}

void LuneTouchDashboard::handle_ui_asset_(AsyncWebServerRequest *request, const char *url) {
#ifdef LUNE_TOUCH_HAS_UI
  if (path_equals(url, "/lune-ui.css")) {
    send_gzip_chunked_(request, "text/css; charset=utf-8", LUNE_TOUCH_UI_CSS_DATA, LUNE_TOUCH_UI_CSS_SIZE,
                       "public, max-age=31536000, immutable");
    return;
  }
  if (path_equals(url, "/ui.js")) {
    send_gzip_chunked_(request, "application/javascript; charset=utf-8", LUNE_TOUCH_UI_JS_DATA,
                       LUNE_TOUCH_UI_JS_SIZE, "public, max-age=31536000, immutable");
    return;
  }
  if (path_equals(url, "/da") || path_equals(url, "/da/")) {
    send_gzip_chunked_(request, "text/html; charset=utf-8", LUNE_TOUCH_UI_DA_DATA, LUNE_TOUCH_UI_DA_SIZE,
                       "no-store, no-cache, max-age=0, must-revalidate");
    return;
  }
  if (path_equals(url, "/en") || path_equals(url, "/en/") || path_equals(url, "/dashboard") ||
      path_equals(url, "/dashboard/")) {
    send_gzip_chunked_(request, "text/html; charset=utf-8", LUNE_TOUCH_UI_EN_DATA, LUNE_TOUCH_UI_EN_SIZE,
                       "no-store, no-cache, max-age=0, must-revalidate");
    return;
  }
#endif
  send_text_(request, 404, "text/plain", "UI asset not configured");
}

void LuneTouchDashboard::send_text_(AsyncWebServerRequest *request, int code, const char *content_type,
                                    const char *body, bool cors, const char *cache_control) {
  if (request == nullptr)
    return;
  httpd_req_t *raw = *request;
  httpd_resp_set_status(raw, http_status_line(code));
  httpd_resp_set_type(raw, content_type);
  httpd_resp_set_hdr(raw, "Connection", "close");
  if (cache_control != nullptr)
    httpd_resp_set_hdr(raw, "Cache-Control", cache_control);
  if (cors)
    add_cors_headers(raw);
  httpd_resp_send(raw, body != nullptr ? body : "", HTTPD_RESP_USE_STRLEN);
}

void LuneTouchDashboard::send_gzip_chunked_(AsyncWebServerRequest *request, const char *content_type,
                                            const uint8_t *data, size_t length,
                                            const char *cache_control) {
  if (request == nullptr || data == nullptr)
    return;
  httpd_req_t *raw = *request;
  httpd_resp_set_status(raw, http_status_line(200));
  httpd_resp_set_type(raw, content_type);
  httpd_resp_set_hdr(raw, "Content-Encoding", "gzip");
  httpd_resp_set_hdr(raw, "Connection", "close");
  if (cache_control != nullptr)
    httpd_resp_set_hdr(raw, "Cache-Control", cache_control);

  // Stage each short flash read in internal SRAM before handing it to the
  // socket. This prevents a slow TCP client from keeping a PROGMEM range busy
  // while the LCD ISR needs the shared flash/PSRAM bus.
  uint8_t chunk[STATIC_CHUNK_SIZE];
  size_t offset = 0;
  while (offset < length) {
    const size_t to_send = std::min(STATIC_CHUNK_SIZE, length - offset);
    memcpy(chunk, data + offset, to_send);
    if (httpd_resp_send_chunk(raw, reinterpret_cast<const char *>(chunk), to_send) != ESP_OK)
      return;
    offset += to_send;
    // Give the display task and RGB refill path a scheduling point between
    // network chunks. The added load latency is negligible for the compressed
    // dashboard and avoids one sustained bus burst.
    delay(1);
  }
  httpd_resp_send_chunk(raw, nullptr, 0);
}

void LuneTouchDashboard::send_json_(AsyncWebServerRequest *request, const char *body) {
  send_text_(request, 200, "application/json", body, true, "no-cache");
}

void LuneTouchDashboard::send_ok_(AsyncWebServerRequest *request, const char *data) {
  if (response_buf_ == nullptr) {
    send_text_(request, 503, "application/json", kBuffersMissingJson, true, "no-cache");
    return;
  }
  if (!json_payload_complete_(data)) {
    send_error_(request, 507, "response_too_large", "Dashboard response too large");
    return;
  }
  const unsigned long ts_ms = static_cast<unsigned long>(esphome::millis());
  const int written = snprintf(response_buf_, RESPONSE_BUF_SIZE,
                               "{\"ok\":true,\"version\":\"v1\",\"ts_ms\":%lu,\"data\":%s}",
                               ts_ms, data);
  if (written < 0 || static_cast<size_t>(written) >= RESPONSE_BUF_SIZE) {
    send_error_(request, 507, "response_too_large", "Dashboard response too large");
    return;
  }
  send_json_(request, response_buf_);
}

void LuneTouchDashboard::send_error_(AsyncWebServerRequest *request, int code, const char *err_code, const char *message) {
  if (response_buf_ == nullptr) {
    send_text_(request, code, "application/json", kBuffersMissingJson, true, "no-cache");
    return;
  }
  const unsigned long ts_ms = static_cast<unsigned long>(esphome::millis());
  snprintf(response_buf_, RESPONSE_BUF_SIZE,
           "{\"ok\":false,\"version\":\"v1\",\"ts_ms\":%lu,\"error\":{\"code\":\"%s\",\"message\":\"%s\"}}",
           ts_ms, err_code, message);
  send_text_(request, code, "application/json", response_buf_, true, "no-cache");
}

void LuneTouchDashboard::send_write_result_(AsyncWebServerRequest *request, bool accepted, int failure_code) {
  if (accepted) {
    send_ok_(request, data_buf_);
    return;
  }
  char err_code[64];
  if (!json_text_get_str(data_buf_, "error", err_code, sizeof(err_code)))
    strncpy(err_code, "rejected", sizeof(err_code) - 1);
  err_code[sizeof(err_code) - 1] = '\0';
  send_error_(request, failure_code, err_code, err_code);
}

void LuneTouchDashboard::send_json_(ApiRequest &api, const char *body) {
  if (api.async != nullptr) {
    send_json_(api.async, body);
    return;
  }
  if (api.raw == nullptr)
    return;
  httpd_resp_set_status(api.raw, http_status_line(200));
  httpd_resp_set_type(api.raw, "application/json");
  httpd_resp_set_hdr(api.raw, "Cache-Control", "no-cache");
  httpd_resp_set_hdr(api.raw, "Connection", "close");
  add_cors_headers(api.raw);
  httpd_resp_send(api.raw, body, HTTPD_RESP_USE_STRLEN);
}

void LuneTouchDashboard::send_ok_(ApiRequest &api, const char *data) {
  if (api.async != nullptr) {
    send_ok_(api.async, data);
    return;
  }
  if (response_buf_ == nullptr) {
    send_error_(api, 503, "unavailable", "Dashboard buffers unavailable");
    return;
  }
  if (!json_payload_complete_(data)) {
    send_error_(api, 507, "response_too_large", "Dashboard response too large");
    return;
  }
  const unsigned long ts_ms = static_cast<unsigned long>(esphome::millis());
  const int written = snprintf(response_buf_, RESPONSE_BUF_SIZE,
                               "{\"ok\":true,\"version\":\"v1\",\"ts_ms\":%lu,\"data\":%s}",
                               ts_ms, data);
  if (written < 0 || static_cast<size_t>(written) >= RESPONSE_BUF_SIZE) {
    send_error_(api, 507, "response_too_large", "Dashboard response too large");
    return;
  }
  send_json_(api, response_buf_);
}

void LuneTouchDashboard::send_error_(ApiRequest &api, int code, const char *err_code, const char *message) {
  if (api.async != nullptr) {
    send_error_(api.async, code, err_code, message);
    return;
  }
  if (api.raw == nullptr)
    return;
  if (response_buf_ == nullptr) {
    httpd_resp_set_status(api.raw, http_status_line(code));
    httpd_resp_set_type(api.raw, "application/json");
    httpd_resp_set_hdr(api.raw, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(api.raw, "Connection", "close");
    add_cors_headers(api.raw);
    httpd_resp_send(api.raw, kBuffersMissingJson, HTTPD_RESP_USE_STRLEN);
    return;
  }
  const unsigned long ts_ms = static_cast<unsigned long>(esphome::millis());
  snprintf(response_buf_, RESPONSE_BUF_SIZE,
           "{\"ok\":false,\"version\":\"v1\",\"ts_ms\":%lu,\"error\":{\"code\":\"%s\",\"message\":\"%s\"}}",
           ts_ms, err_code, message);
  httpd_resp_set_status(api.raw, http_status_line(code));
  httpd_resp_set_type(api.raw, "application/json");
  httpd_resp_set_hdr(api.raw, "Cache-Control", "no-cache");
  httpd_resp_set_hdr(api.raw, "Connection", "close");
  add_cors_headers(api.raw);
  httpd_resp_send(api.raw, response_buf_, HTTPD_RESP_USE_STRLEN);
}

void LuneTouchDashboard::send_write_result_(ApiRequest &api, bool accepted, int failure_code) {
  if (api.async != nullptr) {
    send_write_result_(api.async, accepted, failure_code);
    return;
  }
  if (accepted) {
    send_ok_(api, data_buf_);
    return;
  }
  char err_code[64];
  if (!json_text_get_str(data_buf_, "error", err_code, sizeof(err_code)))
    strncpy(err_code, "rejected", sizeof(err_code) - 1);
  err_code[sizeof(err_code) - 1] = '\0';
  send_error_(api, failure_code, err_code, err_code);
}

void LuneTouchDashboard::handle_v1_(AsyncWebServerRequest *request, const char *path) {
  if (request->method() == HTTP_OPTIONS) {
    send_text_(request, 204, "text/plain", "", true);
    return;
  }
  if (data_buf_ == nullptr || response_buf_ == nullptr) {
    send_error_(request, 503, "unavailable", "Dashboard buffers unavailable");
    return;
  }
  if (!lock_api_buffers_()) {
    send_error_(request, 503, "busy", "Dashboard API busy");
    return;
  }
  struct ApiBufGuard {
    LuneTouchDashboard *self;
    ~ApiBufGuard() { self->unlock_api_buffers_(); }
  } guard{this};

  if (request->method() == HTTP_GET) {
    if (strcmp(path, "/overview") == 0 || strcmp(path, "/") == 0) {
      if (coordinator_)
        coordinator_->write_overview_json(data_buf_, DATA_BUF_SIZE);
      else
        snprintf(data_buf_, DATA_BUF_SIZE, "{}");
      send_ok_(request, data_buf_);
      return;
    }
    if (strcmp(path, "/wifi") == 0) {
      write_wifi_json_(data_buf_, DATA_BUF_SIZE);
      send_ok_(request, data_buf_);
      return;
    }
    if (strcmp(path, "/nodes/scan") == 0) {
      if (coordinator_)
        coordinator_->write_node_scan_json(data_buf_, DATA_BUF_SIZE);
      else
        snprintf(data_buf_, DATA_BUF_SIZE, "{\"scan\":\"lan\",\"discovery\":\"not_run\",\"found\":[]}");
      send_ok_(request, data_buf_);
      return;
    }
    if (strcmp(path, "/nodes") == 0) {
      if (coordinator_)
        coordinator_->write_nodes_json(data_buf_, DATA_BUF_SIZE);
      else
        snprintf(data_buf_, DATA_BUF_SIZE, "{\"nodes\":[]}");
      send_ok_(request, data_buf_);
      return;
    }
    if (strcmp(path, "/zones") == 0) {
      if (coordinator_)
        coordinator_->write_zones_json(data_buf_, DATA_BUF_SIZE);
      else
        snprintf(data_buf_, DATA_BUF_SIZE, "{\"count\":0,\"zones\":[]}");
      send_ok_(request, data_buf_);
      return;
    }
    if (strcmp(path, "/rooms") == 0) {
      if (coordinator_)
        coordinator_->write_rooms_json(data_buf_, DATA_BUF_SIZE);
      else
        snprintf(data_buf_, DATA_BUF_SIZE, "{\"rooms\":[],\"unassigned\":[]}");
      send_ok_(request, data_buf_);
      return;
    }
    {
      char room_id[48];
      if (extract_middle_segment(path, "/zones/", "/comfort-chart", room_id, sizeof(room_id))) {
        if (coordinator_)
          coordinator_->write_zone_comfort_chart_json(room_id, data_buf_, DATA_BUF_SIZE);
        else
          snprintf(data_buf_, DATA_BUF_SIZE,
                   "{\"room_id\":\"\",\"comfort_chart\":{\"hours\":0,\"expected_temp_c\":[],"
                   "\"scheduled_setpoint_c\":[],\"comfort_min_c\":[],\"comfort_max_c\":[],"
                   "\"preload_active\":[],\"actual_temp_c\":null}}");
        send_ok_(request, data_buf_);
        return;
      }
    }
    if (strcmp(path, "/strategy") == 0) {
      if (coordinator_)
        coordinator_->write_strategy_json(data_buf_, DATA_BUF_SIZE);
      else
        snprintf(data_buf_, DATA_BUF_SIZE, "{}");
      send_ok_(request, data_buf_);
      return;
    }
    if (strcmp(path, "/forecast") == 0) {
      if (coordinator_)
        coordinator_->write_forecast_json(data_buf_, DATA_BUF_SIZE);
      else
        snprintf(data_buf_, DATA_BUF_SIZE, "{}");
      send_ok_(request, data_buf_);
      return;
    }
    if (strcmp(path, "/commands") == 0) {
      if (coordinator_)
        coordinator_->write_commands_json(data_buf_, DATA_BUF_SIZE);
      else
        snprintf(data_buf_, DATA_BUF_SIZE, "{\"commands\":[]}");
      send_ok_(request, data_buf_);
      return;
    }
    if (strcmp(path, "/events") == 0) {
      if (coordinator_)
        coordinator_->write_events_json(data_buf_, DATA_BUF_SIZE);
      else
        snprintf(data_buf_, DATA_BUF_SIZE, "{\"events\":[]}");
      send_ok_(request, data_buf_);
      return;
    }
    if (strcmp(path, "/diagnostics") == 0) {
      if (coordinator_)
        coordinator_->write_diagnostics_json(data_buf_, DATA_BUF_SIZE);
      else
        snprintf(data_buf_, DATA_BUF_SIZE, "{}");
      send_ok_(request, data_buf_);
      return;
    }
    if (strcmp(path, "/settings") == 0) {
      if (coordinator_)
        coordinator_->write_settings_json(data_buf_, DATA_BUF_SIZE);
      else
        snprintf(data_buf_, DATA_BUF_SIZE, "{}");
      send_ok_(request, data_buf_);
      return;
    }
    if (strcmp(path, "/heat-source") == 0) {
      if (coordinator_)
        coordinator_->write_heat_source_json(data_buf_, DATA_BUF_SIZE);
      else
        snprintf(data_buf_, DATA_BUF_SIZE, "{}");
      send_ok_(request, data_buf_);
      return;
    }
    if (strcmp(path, "/circulation") == 0) {
      if (coordinator_)
        coordinator_->write_circulation_json(data_buf_, DATA_BUF_SIZE);
      else
        snprintf(data_buf_, DATA_BUF_SIZE, "{}");
      send_ok_(request, data_buf_);
      return;
    }
    if (strcmp(path, "/plan") == 0) {
      if (coordinator_)
        coordinator_->write_plan_json(data_buf_, DATA_BUF_SIZE);
      else
        snprintf(data_buf_, DATA_BUF_SIZE, "{}");
      send_ok_(request, data_buf_);
      return;
    }
    if (strcmp(path, "/heat-source/control") == 0) {
      if (coordinator_)
        coordinator_->write_heat_source_control_json(data_buf_, DATA_BUF_SIZE);
      else
        snprintf(data_buf_, DATA_BUF_SIZE, "{}");
      send_ok_(request, data_buf_);
      return;
    }
    if (strcmp(path, "/odin/mqtt") == 0) {
      if (coordinator_)
        coordinator_->write_odin_mqtt_json(data_buf_, DATA_BUF_SIZE);
      else
        snprintf(data_buf_, DATA_BUF_SIZE, "{}");
      send_ok_(request, data_buf_);
      return;
    }
    if (strcmp(path, "/prices") == 0) {
      if (coordinator_)
        coordinator_->write_prices_json(data_buf_, DATA_BUF_SIZE);
      else
        snprintf(data_buf_, DATA_BUF_SIZE, "{}");
      send_ok_(request, data_buf_);
      return;
    }
    if (strcmp(path, "/odin/physics") == 0) {
      if (coordinator_)
        coordinator_->write_odin_physics_json(data_buf_, DATA_BUF_SIZE);
      else
        snprintf(data_buf_, DATA_BUF_SIZE, "{}");
      send_ok_(request, data_buf_);
      return;
    }
    send_error_(request, 404, "unknown_route", "Unknown route");
    return;
  }

  if (request->method() != HTTP_POST) {
    send_error_(request, 405, "method_not_allowed", "Use GET or POST");
    return;
  }

  if (coordinator_ == nullptr) {
    send_error_(request, 503, "coordinator_unavailable", "Coordinator unavailable");
    return;
  }

  const std::string body_str = request->arg("plain");
  JsonDocument body_doc;
  const JsonDocument *body = nullptr;
  if (!body_str.empty() && (first_non_space(body_str) == '{' || first_non_space(body_str) == '[')) {
    const DeserializationError err = deserializeJson(body_doc, body_str.c_str());
    if (err) {
      send_error_(request, 400, "invalid_json", "Request body is not valid JSON");
      return;
    }
    body = &body_doc;
  }

  ApiRequest api;
  api.async = request;
  api.json_body = body;
  handle_v1_post_(api, path);
}

void LuneTouchDashboard::handle_v1_post_(ApiRequest &api, const char *path) {
  // Caller must hold api_buf_lock_ (handle_v1_ or handle_raw_post_).
  if (data_buf_ == nullptr || response_buf_ == nullptr) {
    send_error_(api, 503, "unavailable", "Dashboard buffers unavailable");
    return;
  }
  if (strcmp(path, "/nodes/scan") == 0) {
    char hostname[80];
    char ip[24];
    parse_text_param(api, api.json_body, "hostname", hostname, sizeof(hostname));
    parse_text_param(api, api.json_body, "ip", ip, sizeof(ip));
    if (hostname[0] != '\0' || ip[0] != '\0')
      coordinator_->scan_node_candidate(hostname, ip, data_buf_, DATA_BUF_SIZE);
    else
      coordinator_->request_lan_scan(data_buf_, DATA_BUF_SIZE);
    send_ok_(api, data_buf_);
  } else if (strcmp(path, "/nodes/refresh") == 0) {
    coordinator_->scan_registered_nodes(data_buf_, DATA_BUF_SIZE);
    send_ok_(api, data_buf_);
  } else if (strcmp(path, "/nodes") == 0) {
    char node_id[32];
    char hostname[80];
    char ip[24];
    char pairing_fingerprint[32];
    parse_text_param(api, api.json_body, "node_id", node_id, sizeof(node_id));
    parse_text_param(api, api.json_body, "hostname", hostname, sizeof(hostname));
    parse_text_param(api, api.json_body, "ip", ip, sizeof(ip));
    parse_text_param(api, api.json_body, "pairing_fingerprint", pairing_fingerprint, sizeof(pairing_fingerprint));
    const bool accepted = coordinator_->add_node(node_id, hostname, ip, pairing_fingerprint,
                                                 data_buf_, DATA_BUF_SIZE);
    send_write_result_(api, accepted, 400);
  } else if (strstr(path, "/trust") != nullptr) {
    char node_id[32]{};
    if (!extract_middle_segment(path, "/nodes/", "/trust", node_id, sizeof(node_id))) {
      send_error_(api, 404, "unknown_route", "Unknown node trust route");
      return;
    }
    ::lune_touch::NodeTrust trust = ::lune_touch::NodeTrust::PAIRED;
    if (!parse_node_trust_param(api, api.json_body, &trust)) {
      send_error_(api, 400, "missing_param", "trust must be paired or trusted");
      return;
    }
    char confirm[32];
    parse_text_param(api, api.json_body, "confirm", confirm, sizeof(confirm));
    const bool accepted = coordinator_->set_node_trust(node_id, trust, confirm,
                                                       data_buf_, DATA_BUF_SIZE);
    send_write_result_(api, accepted, 404);
  } else if (strncmp(path, "/nodes/", 7) == 0 && strstr(path, "/host") != nullptr) {
    char node_id[32]{};
    if (!extract_middle_segment(path, "/nodes/", "/host", node_id, sizeof(node_id))) {
      send_error_(api, 404, "unknown_route", "Unknown node host route");
      return;
    }
    char host[72]{};
    parse_text_param(api, api.json_body, "host", host, sizeof(host));
    const bool accepted = coordinator_->set_node_host(node_id, host, data_buf_, DATA_BUF_SIZE);
    send_write_result_(api, accepted, 400);
  } else if (strstr(path, "/profile") != nullptr) {
    char node_id[32]{};
    if (!extract_middle_segment(path, "/nodes/", "/profile", node_id, sizeof(node_id))) {
      send_error_(api, 404, "unknown_route", "Unknown node profile route");
      return;
    }
    char name[64];
    parse_text_param(api, api.json_body, "name", name, sizeof(name));
    const bool accepted = coordinator_->set_node_profile(node_id, name, data_buf_, DATA_BUF_SIZE);
    send_write_result_(api, accepted, 404);
  } else if (strstr(path, "/remove") != nullptr) {
    char node_id[32]{};
    if (!extract_middle_segment(path, "/nodes/", "/remove", node_id, sizeof(node_id))) {
      send_error_(api, 404, "unknown_route", "Unknown node remove route");
      return;
    }
    char confirm[32];
    parse_text_param(api, api.json_body, "confirm", confirm, sizeof(confirm));
    const bool accepted = coordinator_->remove_node(node_id, confirm, data_buf_, DATA_BUF_SIZE);
    send_write_result_(api, accepted, 404);
  } else if (strstr(path, "/room") != nullptr) {
    char room_id[40]{};
    if (!extract_middle_segment(path, "/zones/", "/room", room_id, sizeof(room_id))) {
      send_error_(api, 404, "unknown_route", "Unknown atomic room route");
      return;
    }
    ::lune_touch::RoomUpdate update{};
    uint32_t value = 0;
    bool valid = parse_uint_param(api, api.json_body, "expected_revision", &update.expected_revision) &&
        parse_float_param(api, api.json_body, "total_area_m2", &update.total_area_m2) &&
        parse_float_param(api, api.json_body, "physical_weight", &update.physical_weight) &&
        parse_uint_param(api, api.json_body, "include_in_house_temperature", &value);
    update.include_in_house_temperature = value != 0;
    valid = valid && parse_float_param(api, api.json_body, "comfort_setpoint_c", &update.comfort_setpoint_c) &&
        parse_float_param(api, api.json_body, "comfort_bias_c", &update.comfort_bias_c) &&
        parse_uint_param(api, api.json_body, "priority", &value);
    update.priority = static_cast<uint8_t>(value);
    valid = valid && parse_uint_param(api, api.json_body, "schedule_enabled", &value);
    update.schedule_enabled = value != 0;
    valid = valid && parse_uint_param(api, api.json_body, "schedule_day_mask", &value);
    update.schedule_day_mask = static_cast<uint8_t>(value);
    valid = valid && parse_uint_param(api, api.json_body, "schedule_start_min", &value);
    update.schedule_start_min = static_cast<uint16_t>(value);
    valid = valid && parse_uint_param(api, api.json_body, "schedule_end_min", &value);
    update.schedule_end_min = static_cast<uint16_t>(value);
    valid = valid && parse_float_param(api, api.json_body, "schedule_setpoint_c", &update.schedule_setpoint_c) &&
        parse_uint_param(api, api.json_body, "exterior_walls", &value);
    update.exterior_walls = static_cast<uint8_t>(value);
    valid = valid && parse_float_param(api, api.json_body, "wind_exposure", &update.wind_exposure) &&
        parse_float_param(api, api.json_body, "solar_gain", &update.solar_gain) &&
        parse_uint_param(api, api.json_body, "thermal_lead_h", &value);
    update.thermal_lead_h = static_cast<uint8_t>(value);
    valid = valid && parse_float_param(api, api.json_body, "max_offset_c", &update.max_offset_c);
    if (!valid) {
      send_error_(api, 400, "missing_param", "Complete atomic room update is required");
      return;
    }
    const bool accepted = coordinator_->update_room_atomically(room_id, update, data_buf_, DATA_BUF_SIZE);
    send_write_result_(api, accepted, 409);
  } else if (strstr(path, "/setpoint-command") != nullptr) {
    char room_id[40]{};
    if (!extract_middle_segment(path, "/zones/", "/setpoint-command", room_id, sizeof(room_id))) {
      send_error_(api, 404, "unknown_route", "Unknown zone command route");
      return;
    }
    float offset = 0.0f;
    if (!parse_float_param(api, api.json_body, "offset_c", &offset) &&
        !parse_float_param(api, api.json_body, "requested_offset_c", &offset)) {
      send_error_(api, 400, "missing_param", "offset_c is required");
      return;
    }
    uint32_t ttl_s = 2700;
    parse_uint_param(api, api.json_body, "ttl_s", &ttl_s);
    char reason[80];
    parse_text_param(api, api.json_body, "reason", reason, sizeof(reason));
    const bool accepted = coordinator_->queue_setpoint_command(room_id, offset, ttl_s, reason, data_buf_, DATA_BUF_SIZE);
    send_write_result_(api, accepted, 400);
  } else if (strstr(path, "/motor-action") != nullptr) {
    char room_id[40]{};
    if (!extract_middle_segment(path, "/zones/", "/motor-action", room_id, sizeof(room_id))) {
      send_error_(api, 404, "unknown_route", "Unknown zone motor action route");
      return;
    }
    char action[24];
    parse_text_param(api, api.json_body, "action", action, sizeof(action));
    if (action[0] == '\0') {
      send_error_(api, 400, "missing_param", "action is required");
      return;
    }
    char confirm[24];
    parse_text_param(api, api.json_body, "confirm", confirm, sizeof(confirm));
    const bool accepted = coordinator_->request_motor_action(room_id, action, confirm,
                                                             data_buf_, DATA_BUF_SIZE);
    send_write_result_(api, accepted, 400);
  } else if (strstr(path, "/schedule") != nullptr) {
    char room_id[40]{};
    if (!extract_middle_segment(path, "/zones/", "/schedule", room_id, sizeof(room_id))) {
      send_error_(api, 404, "unknown_route", "Unknown zone schedule route");
      return;
    }
    uint32_t enabled = 0;
    uint32_t day_mask = 0x7F;
    uint32_t start_min = 360;
    uint32_t end_min = 1320;
    float setpoint = 21.0f;
    parse_uint_param(api, api.json_body, "enabled", &enabled);
    parse_uint_param(api, api.json_body, "day_mask", &day_mask);
    parse_uint_param(api, api.json_body, "start_min", &start_min);
    parse_uint_param(api, api.json_body, "end_min", &end_min);
    if (!parse_float_param(api, api.json_body, "setpoint_c", &setpoint) &&
        !parse_float_param(api, api.json_body, "comfort_setpoint_c", &setpoint)) {
      send_error_(api, 400, "missing_param", "setpoint_c is required");
      return;
    }
    const bool accepted = coordinator_->set_zone_schedule(room_id, enabled != 0,
                                                          static_cast<uint8_t>(day_mask),
                                                          static_cast<uint16_t>(start_min),
                                                          static_cast<uint16_t>(end_min),
                                                          setpoint, data_buf_, DATA_BUF_SIZE);
    send_write_result_(api, accepted, 400);
  } else if (strstr(path, "/forecast-profile") != nullptr) {
    char room_id[40]{};
    if (!extract_middle_segment(path, "/zones/", "/forecast-profile", room_id, sizeof(room_id))) {
      send_error_(api, 404, "unknown_route", "Unknown zone forecast profile route");
      return;
    }
    uint32_t exterior_walls = 0;
    uint32_t thermal_lead_h = 4;
    float wind_exposure = 0.5f;
    float solar_gain = 0.3f;
    float max_offset_c = std::numeric_limits<float>::quiet_NaN();
    parse_uint_param(api, api.json_body, "exterior_walls", &exterior_walls);
    parse_uint_param(api, api.json_body, "thermal_lead_h", &thermal_lead_h);
    parse_float_param(api, api.json_body, "wind_exposure", &wind_exposure);
    parse_float_param(api, api.json_body, "solar_gain", &solar_gain);
    parse_float_param(api, api.json_body, "max_offset_c", &max_offset_c);
    const bool accepted = coordinator_->set_zone_forecast_profile(
        room_id, static_cast<uint8_t>(exterior_walls), wind_exposure, solar_gain,
        static_cast<uint8_t>(thermal_lead_h), max_offset_c, data_buf_, DATA_BUF_SIZE);
    send_write_result_(api, accepted, 400);
  } else if (strstr(path, "/comfort") != nullptr) {
    char room_id[40]{};
    if (!extract_middle_segment(path, "/zones/", "/comfort", room_id, sizeof(room_id))) {
      send_error_(api, 404, "unknown_route", "Unknown zone comfort route");
      return;
    }
    float comfort = 0.0f;
    if (!parse_float_param(api, api.json_body, "comfort_setpoint_c", &comfort) &&
        !parse_float_param(api, api.json_body, "setpoint_c", &comfort)) {
      send_error_(api, 400, "missing_param", "comfort_setpoint_c is required");
      return;
    }
    uint32_t priority = 1;
    parse_uint_param(api, api.json_body, "priority", &priority);
    float bias = 0.0f;
    if (!parse_float_param(api, api.json_body, "comfort_bias_c", &bias))
      parse_float_param(api, api.json_body, "bias_c", &bias);
    const bool accepted = coordinator_->set_zone_comfort(room_id, comfort, static_cast<uint8_t>(priority),
                                                         bias, data_buf_, DATA_BUF_SIZE);
    send_write_result_(api, accepted, 400);
  } else if (strcmp(path, "/forecast/settings") == 0) {
    float latitude = 0.0f;
    float longitude = 0.0f;
    if (!parse_float_param(api, api.json_body, "latitude", &latitude) ||
        !parse_float_param(api, api.json_body, "longitude", &longitude)) {
      send_error_(api, 400, "missing_param", "latitude and longitude are required");
      return;
    }
    char source[24];
    parse_text_param(api, api.json_body, "source", source, sizeof(source));
    const bool accepted = coordinator_->set_forecast_location(latitude, longitude, source, data_buf_, DATA_BUF_SIZE);
    send_write_result_(api, accepted, 400);
  } else if (strcmp(path, "/weather/settings") == 0) {
    float max_boost_c = 1.5f;
    if (!parse_float_param(api, api.json_body, "max_boost_c", &max_boost_c)) {
      send_error_(api, 400, "missing_param", "max_boost_c is required");
      return;
    }
    const bool accepted = coordinator_->set_weather_settings(max_boost_c, data_buf_, DATA_BUF_SIZE);
    send_write_result_(api, accepted, 400);
  } else if (strcmp(path, "/heat-source/settings") == 0) {
    // Optional text fields: absent = unchanged (nullptr); present (even "") = set.
    // Without this, every UI save wiped the Odin host, MQTT credentials and the
    // Asgard bias/freeze entities, because parse_text_param() yields "".
    auto present = [&](const char *key) {
      return !api_arg(api, key).empty() ||
             (api.json_body != nullptr && !(*api.json_body)[key].isNull());
    };
    char host[80];
    char weighted_temperature_variable[64];
    char write_url_template[192];
    char read_url_template[192];
    uint32_t enabled = 0;
    uint32_t port = 0;
    uint32_t push_interval_s = 0;
    parse_text_param(api, api.json_body, "host", host, sizeof(host));
    parse_text_param(api, api.json_body, "weighted_temperature_variable",
                     weighted_temperature_variable, sizeof(weighted_temperature_variable));
    const bool has_enabled = parse_uint_param(api, api.json_body, "enabled", &enabled);
    parse_uint_param(api, api.json_body, "port", &port);
    parse_uint_param(api, api.json_body, "push_interval_s", &push_interval_s);
    const bool has_write_url =
        !api_arg(api, "write_url_template").empty() ||
        (api.json_body != nullptr && !(*api.json_body)["write_url_template"].isNull());
    const bool has_read_url =
        !api_arg(api, "read_url_template").empty() ||
        (api.json_body != nullptr && !(*api.json_body)["read_url_template"].isNull());
    if (has_write_url)
      parse_text_param(api, api.json_body, "write_url_template", write_url_template,
                       sizeof(write_url_template));
    else
      write_url_template[0] = '\0';
    if (has_read_url)
      parse_text_param(api, api.json_body, "read_url_template", read_url_template,
                       sizeof(read_url_template));
    else
      read_url_template[0] = '\0';
    float declared_target_c = NAN;
    const bool has_declared_target = parse_float_param(api, api.json_body, "declared_target_c",
                                                       &declared_target_c);
    char climate_entity[64]{};
    char bias_entity[64]{};
    char dhw_entity[64]{};
    char legionella_entity[64]{};
    char defrost_entity[64]{};
    char trim_mode[16]{};
    char v6_control_mode[16]{};
    char house_weighting[8]{};
    char heat_source_type[24]{};
    parse_text_param(api, api.json_body, "type", heat_source_type, sizeof(heat_source_type));
    parse_text_param(api, api.json_body, "climate_entity", climate_entity, sizeof(climate_entity));
    parse_text_param(api, api.json_body, "bias_entity", bias_entity, sizeof(bias_entity));
    parse_text_param(api, api.json_body, "dhw_entity", dhw_entity, sizeof(dhw_entity));
    parse_text_param(api, api.json_body, "legionella_entity", legionella_entity,
                     sizeof(legionella_entity));
    parse_text_param(api, api.json_body, "defrost_entity", defrost_entity, sizeof(defrost_entity));
    parse_text_param(api, api.json_body, "trim_mode", trim_mode, sizeof(trim_mode));
    parse_text_param(api, api.json_body, "v6_control_mode", v6_control_mode, sizeof(v6_control_mode));
    parse_text_param(api, api.json_body, "house_weighting", house_weighting, sizeof(house_weighting));
    uint32_t target_sync = 0;
    const bool has_target_sync = parse_uint_param(api, api.json_body, "target_sync_enabled",
                                                  &target_sync);
    uint32_t odin_plan_enabled = 0;
    const bool has_odin_plan = parse_uint_param(api, api.json_body, "odin_plan_enabled",
                                                &odin_plan_enabled);
    uint32_t absorb_arm_enabled = 0;
    const bool has_absorb_arm = parse_uint_param(api, api.json_body, "absorb_arm_enabled",
                                                 &absorb_arm_enabled);
    uint32_t arm_energy_cost_modulation = 1;
    const bool has_arm_ecm = parse_uint_param(api, api.json_body, "arm_energy_cost_modulation",
                                              &arm_energy_cost_modulation);
    char plan_source_mode[8]{};
    char odin_host[64]{};
    parse_text_param(api, api.json_body, "plan_source", plan_source_mode, sizeof(plan_source_mode));
    parse_text_param(api, api.json_body, "odin_host", odin_host, sizeof(odin_host));
    uint32_t odin_port = 0;
    parse_uint_param(api, api.json_body, "odin_port", &odin_port);
    uint32_t mqtt_enabled = 0;
    const bool has_mqtt = parse_uint_param(api, api.json_body, "mqtt_enabled", &mqtt_enabled);
    char mqtt_host[64]{};
    char mqtt_username[64]{};
    char mqtt_password[64]{};
    char mqtt_topic_prefix[64]{};
    char mqtt_hp_id[32]{};
    parse_text_param(api, api.json_body, "mqtt_host", mqtt_host, sizeof(mqtt_host));
    parse_text_param(api, api.json_body, "mqtt_username", mqtt_username, sizeof(mqtt_username));
    parse_text_param(api, api.json_body, "mqtt_password", mqtt_password, sizeof(mqtt_password));
    parse_text_param(api, api.json_body, "mqtt_topic_prefix", mqtt_topic_prefix,
                     sizeof(mqtt_topic_prefix));
    parse_text_param(api, api.json_body, "mqtt_hp_id", mqtt_hp_id, sizeof(mqtt_hp_id));
    uint32_t mqtt_port = 0;
    parse_uint_param(api, api.json_body, "mqtt_port", &mqtt_port);
    if (port > 65535 || push_interval_s > 65535) {
      send_error_(api, 400, "invalid_param", "port or push_interval_s is outside range");
      return;
    }
    const bool accepted = coordinator_->set_heat_source_settings(
        has_enabled, enabled != 0, host, static_cast<uint16_t>(port), weighted_temperature_variable,
        static_cast<uint16_t>(push_interval_s), data_buf_, DATA_BUF_SIZE, has_write_url,
        write_url_template, has_read_url, read_url_template, has_declared_target, declared_target_c,
        climate_entity, has_target_sync, target_sync != 0,
        present("bias_entity") ? bias_entity : nullptr, present("dhw_entity") ? dhw_entity : nullptr,
        present("legionella_entity") ? legionella_entity : nullptr,
        present("defrost_entity") ? defrost_entity : nullptr, trim_mode, v6_control_mode, house_weighting,
        heat_source_type, has_odin_plan, odin_plan_enabled != 0, has_absorb_arm,
        absorb_arm_enabled != 0, has_arm_ecm, arm_energy_cost_modulation != 0, plan_source_mode,
        present("odin_host") ? odin_host : nullptr,
        static_cast<uint16_t>(odin_port > 65535 ? 0 : odin_port), has_mqtt, mqtt_enabled != 0,
        present("mqtt_host") ? mqtt_host : nullptr,
        static_cast<uint16_t>(mqtt_port > 65535 ? 0 : mqtt_port),
        present("mqtt_username") ? mqtt_username : nullptr,
        present("mqtt_password") ? mqtt_password : nullptr,
        present("mqtt_topic_prefix") ? mqtt_topic_prefix : nullptr,
        present("mqtt_hp_id") ? mqtt_hp_id : nullptr);
    send_write_result_(api, accepted, 400);
  } else if (strcmp(path, "/rooms") == 0) {
    char room_id[48]{};
    char name[64]{};
    parse_text_param(api, api.json_body, "room_id", room_id, sizeof(room_id));
    parse_text_param(api, api.json_body, "name", name, sizeof(name));
    const bool accepted = coordinator_->create_room(room_id, name, data_buf_, DATA_BUF_SIZE);
    send_write_result_(api, accepted, 400);
  } else if ([&]() {
               char room_id[48]{};
               return extract_middle_segment(path, "/rooms/", "/groups", room_id, sizeof(room_id));
             }()) {
    char room_id[48]{};
    extract_middle_segment(path, "/rooms/", "/groups", room_id, sizeof(room_id));
    char node_id[48]{};
    uint32_t zone = 0;
    parse_text_param(api, api.json_body, "node_id", node_id, sizeof(node_id));
    parse_uint_param(api, api.json_body, "zone", &zone);
    const bool accepted =
        coordinator_->move_group_to_room(node_id, static_cast<uint8_t>(zone), room_id, data_buf_,
                                         DATA_BUF_SIZE);
    send_write_result_(api, accepted, 400);
  } else if ([&]() {
               char room_id[48]{};
               return extract_middle_segment(path, "/rooms/", "/sync-ble", room_id, sizeof(room_id));
             }()) {
    char room_id[48]{};
    extract_middle_segment(path, "/rooms/", "/sync-ble", room_id, sizeof(room_id));
    const bool accepted =
        coordinator_->sync_room_ble_sensor(room_id, data_buf_, DATA_BUF_SIZE);
    send_write_result_(api, accepted, 400);
  } else if ([&]() {
               char room_id[48]{};
               return extract_middle_segment(path, "/rooms/", "/sensor", room_id, sizeof(room_id));
             }()) {
    char room_id[48]{};
    extract_middle_segment(path, "/rooms/", "/sensor", room_id, sizeof(room_id));
    char node_id[48]{};
    uint32_t zone = 0;
    parse_text_param(api, api.json_body, "node_id", node_id, sizeof(node_id));
    parse_uint_param(api, api.json_body, "zone", &zone);
    const bool accepted =
        coordinator_->set_room_sensor(room_id, node_id, static_cast<uint8_t>(zone), data_buf_,
                                      DATA_BUF_SIZE);
    send_write_result_(api, accepted, 400);
  } else if ([&]() {
               char room_id[48]{};
               return extract_middle_segment(path, "/rooms/", "/remove", room_id, sizeof(room_id));
             }()) {
    char room_id[48]{};
    extract_middle_segment(path, "/rooms/", "/remove", room_id, sizeof(room_id));
    const bool accepted = coordinator_->delete_room_if_empty(room_id, data_buf_, DATA_BUF_SIZE);
    send_write_result_(api, accepted, 400);
  } else if (strcmp(path, "/circulation/settings") == 0) {
    char host[80];
    char flow_entity[64];
    char head_entity[64];
    char power_entity[64];
    uint32_t enabled = 0;
    uint32_t port = 0;
    parse_text_param(api, api.json_body, "host", host, sizeof(host));
    parse_text_param(api, api.json_body, "flow_entity", flow_entity, sizeof(flow_entity));
    parse_text_param(api, api.json_body, "head_entity", head_entity, sizeof(head_entity));
    parse_text_param(api, api.json_body, "power_entity", power_entity, sizeof(power_entity));
    const bool has_enabled = parse_uint_param(api, api.json_body, "enabled", &enabled);
    parse_uint_param(api, api.json_body, "port", &port);
    if (port > 65535) {
      send_error_(api, 400, "invalid_param", "port is outside range");
      return;
    }
    const bool accepted = coordinator_->set_circulation_settings(
        has_enabled, enabled != 0, host, static_cast<uint16_t>(port), flow_entity, head_entity,
        power_entity, data_buf_, DATA_BUF_SIZE);
    send_write_result_(api, accepted, 400);
  } else if (strcmp(path, "/circulation/refresh") == 0) {
    const bool accepted = coordinator_->request_circulation_refresh(data_buf_, DATA_BUF_SIZE);
    send_write_result_(api, accepted, 400);
  } else if (strcmp(path, "/heat-source/test-read") == 0) {
    if (coordinator_)
      coordinator_->request_heat_source_test_read(data_buf_, DATA_BUF_SIZE);
    else
      snprintf(data_buf_, DATA_BUF_SIZE, "{\"result\":\"rejected\",\"error\":\"unavailable\"}");
    send_ok_(api, data_buf_);
  } else if (strcmp(path, "/heat-source/test-push") == 0) {
    if (coordinator_)
      coordinator_->request_heat_source_test_push(data_buf_, DATA_BUF_SIZE);
    else
      snprintf(data_buf_, DATA_BUF_SIZE, "{\"result\":\"rejected\",\"error\":\"unavailable\"}");
    send_ok_(api, data_buf_);
  } else if (strcmp(path, "/heat-source/control") == 0) {
    // Odin comfort-schedule control + generic heat-source levers. Absent keys
    // are unchanged; an empty template string clears that lever.
    auto present = [&](const char *key) {
      return !api_arg(api, key).empty() ||
             (api.json_body != nullptr && !(*api.json_body)[key].isNull());
    };
    uint32_t odin_enabled = 0;
    const bool has_odin_enabled = parse_uint_param(api, api.json_body, "odin_enabled", &odin_enabled);
    float max_lift_c = NAN;
    parse_float_param(api, api.json_body, "odin_max_lift_c", &max_lift_c);
    float curve_gain = NAN;
    parse_float_param(api, api.json_body, "curve_gain", &curve_gain);
    float curve_max = NAN;
    parse_float_param(api, api.json_body, "curve_max_offset_c", &curve_max);
    char forwarder[64]{};
    char target_tmpl[192]{};
    char request_tmpl[192]{};
    char curve_tmpl[192]{};
    const bool has_forwarder = present("forwarder_entity");
    const bool has_target = present("target_url_template");
    const bool has_request = present("heat_request_url_template");
    const bool has_curve = present("curve_offset_url_template");
    if (has_forwarder)
      parse_text_param(api, api.json_body, "forwarder_entity", forwarder, sizeof(forwarder));
    if (has_target)
      parse_text_param(api, api.json_body, "target_url_template", target_tmpl, sizeof(target_tmpl));
    if (has_request)
      parse_text_param(api, api.json_body, "heat_request_url_template", request_tmpl, sizeof(request_tmpl));
    if (has_curve)
      parse_text_param(api, api.json_body, "curve_offset_url_template", curve_tmpl, sizeof(curve_tmpl));
    const bool accepted = coordinator_->set_heat_source_control(
        has_odin_enabled, odin_enabled != 0, max_lift_c, has_forwarder ? forwarder : nullptr,
        has_target ? target_tmpl : nullptr, has_request ? request_tmpl : nullptr,
        has_curve ? curve_tmpl : nullptr, curve_gain, curve_max, data_buf_, DATA_BUF_SIZE);
    send_write_result_(api, accepted, 400);
  } else if (strcmp(path, "/prices/settings") == 0) {
    // Energy price → Odin. Absent keys are unchanged. grid_schedule is a JSON
    // string like Odin's sched_ui ([{"h":0,"v":0.077},...]); a JSON body may
    // also send it as an array.
    lune_touch_coordinator::PriceSettingsUpdate update;
    uint32_t enabled = 0;
    update.has_enabled = parse_uint_param(api, api.json_body, "enabled", &enabled);
    update.enabled = enabled != 0;
    char area[8]{};
    char grid_source[16]{};
    char grid_gln[24]{};
    char grid_code[32]{};
    char energinet_source[16]{};
    char schedule[768]{};
    parse_text_param(api, api.json_body, "area", area, sizeof(area));
    parse_text_param(api, api.json_body, "grid_source", grid_source, sizeof(grid_source));
    parse_text_param(api, api.json_body, "grid_gln", grid_gln, sizeof(grid_gln));
    parse_text_param(api, api.json_body, "grid_code", grid_code, sizeof(grid_code));
    parse_text_param(api, api.json_body, "energinet_source", energinet_source, sizeof(energinet_source));
    parse_text_param(api, api.json_body, "grid_schedule", schedule, sizeof(schedule));
    if (schedule[0] == '\0' && api.json_body != nullptr && (*api.json_body)["grid_schedule"].is<JsonArrayConst>())
      serializeJson((*api.json_body)["grid_schedule"], schedule, sizeof(schedule));
    update.area = area;
    update.grid_source = grid_source;
    update.grid_gln = grid_gln;
    update.grid_code = grid_code;
    update.energinet_source = energinet_source;
    update.grid_schedule = schedule;
    parse_float_param(api, api.json_body, "energinet_fixed_dkk", &update.energinet_fixed_dkk);
    parse_float_param(api, api.json_body, "elafgift_dkk", &update.elafgift_dkk);
    parse_float_param(api, api.json_body, "markup_dkk", &update.markup_dkk);
    parse_float_param(api, api.json_body, "vat_pct", &update.vat_pct);
    const bool accepted = coordinator_->set_price_settings(update, data_buf_, DATA_BUF_SIZE);
    send_write_result_(api, accepted, 400);
  } else if (strcmp(path, "/prices/push") == 0) {
    const bool accepted = coordinator_->request_price_push(data_buf_, DATA_BUF_SIZE);
    send_write_result_(api, accepted, 400);
  } else if (strcmp(path, "/heat-source/push") == 0 || strcmp(path, "/heat-source/test") == 0) {
    const bool accepted = coordinator_->request_heat_source_push(data_buf_, DATA_BUF_SIZE);
    send_write_result_(api, accepted, 400);
  } else if (strcmp(path, "/settings") == 0) {
    char coordinator_name[40];
    char install_id[40];
    char site_label[64];
    char install_mode[24];
    char asgard_mode[24];
    char authority_leader_node_id[40];
    char authority_coordinator_id[40];
    char authority_shared_key[72];
    uint32_t asgard_enabled = 0;
    parse_text_param(api, api.json_body, "name", coordinator_name, sizeof(coordinator_name));
    parse_text_param(api, api.json_body, "install_id", install_id, sizeof(install_id));
    parse_text_param(api, api.json_body, "site_label", site_label, sizeof(site_label));
    parse_text_param(api, api.json_body, "install_mode", install_mode, sizeof(install_mode));
    parse_text_param(api, api.json_body, "asgard_mode", asgard_mode, sizeof(asgard_mode));
    parse_text_param(api, api.json_body, "authority_leader_node_id", authority_leader_node_id, sizeof(authority_leader_node_id));
    parse_text_param(api, api.json_body, "authority_coordinator_id", authority_coordinator_id, sizeof(authority_coordinator_id));
    parse_text_param(api, api.json_body, "authority_shared_key", authority_shared_key, sizeof(authority_shared_key));
    const bool has_asgard_enabled = parse_uint_param(api, api.json_body, "asgard_enabled", &asgard_enabled);
    uint32_t display_idle_timeout_s = 0;
    const bool has_display_idle_timeout =
        parse_uint_param(api, api.json_body, "display_idle_timeout_s", &display_idle_timeout_s);
    const bool accepted = coordinator_->set_settings(coordinator_name, install_id, site_label,
                                                     install_mode, has_asgard_enabled,
                                                     asgard_enabled != 0, asgard_mode, authority_leader_node_id,
                                                     authority_coordinator_id, authority_shared_key,
                                                     has_display_idle_timeout, display_idle_timeout_s,
                                                     data_buf_, DATA_BUF_SIZE);
    send_write_result_(api, accepted, 400);
  } else if (strcmp(path, "/wifi") == 0) {
#ifdef USE_LUNE_WIFI
    char ssid[33]{};
    char password[65]{};
    parse_text_param(api, api.json_body, "ssid", ssid, sizeof(ssid));
    parse_text_param(api, api.json_body, "password", password, sizeof(password));
    auto *lw = lune_wifi::global_lune_wifi;
    if (ssid[0] == '\0') {
      send_error_(api, 400, "invalid_ssid", "SSID is required");
    } else if (lw == nullptr || lw->result() == lune_wifi::SwitchResult::PENDING) {
      send_error_(api, 409, "busy", "A WiFi change is already running");
    } else {
      // Reply first: the switch drops this connection. WiFi calls belong on the main loop.
      std::string s_ssid(ssid), s_password(password);
      this->defer([lw, s_ssid, s_password]() { lw->request_switch(s_ssid, s_password); });
      write_wifi_json_(data_buf_, DATA_BUF_SIZE);
      send_ok_(api, data_buf_);
    }
#else
    send_error_(api, 501, "unsupported", "Runtime WiFi is not in this build");
#endif
  } else if (strcmp(path, "/display/wake") == 0) {
    const bool accepted = coordinator_->request_display_wake(data_buf_, DATA_BUF_SIZE);
    send_write_result_(api, accepted, 400);
  } else if (strcmp(path, "/forecast/fetch") == 0) {
    const bool accepted = coordinator_->request_forecast_fetch(data_buf_, DATA_BUF_SIZE);
    send_write_result_(api, accepted, 400);
  } else if (strcmp(path, "/forecast/estimate-location") == 0) {
    const bool accepted = coordinator_->estimate_forecast_location(data_buf_, DATA_BUF_SIZE);
    send_write_result_(api, accepted, 400);
  } else if (strcmp(path, "/recovery/reset-registry") == 0) {
    char confirm[32];
    parse_text_param(api, api.json_body, "confirm", confirm, sizeof(confirm));
    const bool accepted = coordinator_->reset_registry(confirm, data_buf_, DATA_BUF_SIZE);
    send_write_result_(api, accepted, 400);
  } else if (strncmp(path, "/zones/", 7) == 0) {
    const char *room_id = path + 7;
    if (room_id[0] == '\0' || strchr(room_id, '/') != nullptr) {
      send_error_(api, 404, "unknown_route", "Unknown zone route");
      return;
    }
    send_error_(api, 410, "zone_managed_on_v6",
                "Physical zones are imported automatically and configured on their V6 manifold");
  } else {
    send_error_(api, 404, "unknown_route", "Unknown route");
  }
}

namespace {
// AsyncWebServer::request_post_handler is protected; a using-declaration in a
// derived type exposes it so non-API POSTs can go back to ESPHome.
struct EsphomePostHandler : web_server_idf::AsyncWebServer {
  using web_server_idf::AsyncWebServer::request_post_handler;
};
}  // namespace

esp_err_t LuneTouchDashboard::raw_post_handler_(httpd_req_t *request) {
  auto *dashboard = static_cast<LuneTouchDashboard *>(request->user_ctx);
  if (dashboard == nullptr) {
    httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Dashboard unavailable");
    return ESP_OK;
  }
  // This handler replaces ESPHome's catch-all POST. Everything outside the API
  // (browser OTA POST /update, other AsyncWebHandlers) goes back to ESPHome's
  // dispatcher, which expects its AsyncWebServer in user_ctx.
  if (strncmp(request->uri, API_PREFIX, API_PREFIX_LEN) != 0 &&
      dashboard->base_ != nullptr && dashboard->base_->get_server() != nullptr) {
    request->user_ctx = dashboard->base_->get_server();
    return EsphomePostHandler::request_post_handler(request);
  }
  return dashboard->handle_raw_post_(request);
}

esp_err_t LuneTouchDashboard::handle_raw_post_(httpd_req_t *request) {
  ApiRequest api;
  api.raw = request;

  if (data_buf_ == nullptr || response_buf_ == nullptr) {
    send_error_(api, 503, "unavailable", "Dashboard buffers unavailable");
    return ESP_OK;
  }
  if (!lock_api_buffers_()) {
    send_error_(api, 503, "busy", "Dashboard API busy");
    return ESP_OK;
  }
  struct ApiBufGuard {
    LuneTouchDashboard *self;
    ~ApiBufGuard() { self->unlock_api_buffers_(); }
  } guard{this};

  const char *query_start = strchr(request->uri, '?');
  const size_t raw_path_len = query_start == nullptr ? strlen(request->uri)
                                                     : static_cast<size_t>(query_start - request->uri);
  const std::string decoded_path = url_decode(request->uri, raw_path_len);
  if (strncmp(decoded_path.c_str(), API_PREFIX, API_PREFIX_LEN) != 0 ||
      (decoded_path.c_str()[API_PREFIX_LEN] != '/' && decoded_path.c_str()[API_PREFIX_LEN] != '\0')) {
    send_error_(api, 404, "unknown_route", "Unknown POST route");
    return ESP_OK;
  }

  std::string query;
  if (query_start != nullptr)
    query = query_start + 1;
  api.query = query.empty() ? nullptr : query.c_str();

  if (coordinator_ == nullptr) {
    send_error_(api, 503, "coordinator_unavailable", "Coordinator unavailable");
    return ESP_OK;
  }

  static constexpr size_t MAX_POST_BODY = 4096;
  if (request->content_len > MAX_POST_BODY) {
    send_error_(api, 413, "payload_too_large", "POST body is too large");
    return ESP_OK;
  }

  std::string body;
  body.resize(request->content_len);
  size_t received = 0;
  while (received < request->content_len) {
    const int ret = httpd_req_recv(request, &body[received], request->content_len - received);
    if (ret <= 0) {
      send_error_(api, 400, "body_read_failed", "Could not read request body");
      return ESP_OK;
    }
    received += static_cast<size_t>(ret);
  }

  JsonDocument body_doc;
  const char lead = first_non_space(body);
  const bool wants_json = content_type_contains(request, "application/json") || lead == '{' || lead == '[';
  if (!body.empty() && wants_json) {
    const DeserializationError err = deserializeJson(body_doc, body.c_str());
    if (err) {
      send_error_(api, 400, "invalid_json", "Request body is not valid JSON");
      return ESP_OK;
    }
    api.json_body = &body_doc;
  } else if (!body.empty()) {
    api.form_body = body.c_str();
  }

  const char *path = decoded_path.c_str() + API_PREFIX_LEN;
  handle_v1_post_(api, *path ? path : "/");
  return ESP_OK;
}

}  // namespace lune_touch_dashboard
}  // namespace esphome
