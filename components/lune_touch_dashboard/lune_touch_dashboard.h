#pragma once

#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/core/component.h"
#include "esphome/core/progmem.h"
#include "../lune_touch_coordinator/lune_touch_coordinator.h"
#include <ArduinoJson.h>
#include <esp_http_server.h>
#include <string>
#if defined(USE_ESP32)
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#endif

#ifdef LUNE_TOUCH_HAS_UI
extern const uint8_t LUNE_TOUCH_UI_CSS_DATA[] PROGMEM;
extern const size_t LUNE_TOUCH_UI_CSS_SIZE;
extern const uint8_t LUNE_TOUCH_UI_JS_DATA[] PROGMEM;
extern const size_t LUNE_TOUCH_UI_JS_SIZE;
extern const uint8_t LUNE_TOUCH_UI_EN_DATA[] PROGMEM;
extern const size_t LUNE_TOUCH_UI_EN_SIZE;
extern const uint8_t LUNE_TOUCH_UI_DA_DATA[] PROGMEM;
extern const size_t LUNE_TOUCH_UI_DA_SIZE;
#endif

namespace esphome {
namespace lune_touch_dashboard {

struct ApiRequest {
  AsyncWebServerRequest *async{nullptr};
  httpd_req_t *raw{nullptr};
  const char *query{nullptr};
  const char *form_body{nullptr};
  const JsonDocument *json_body{nullptr};
};

class LuneTouchDashboard : public Component, public AsyncWebHandler {
 public:
  void setup() override;
  float get_setup_priority() const override { return setup_priority::WIFI - 1.0f; }

  void set_web_server_base(web_server_base::WebServerBase *base) { base_ = base; }
  void set_coordinator(lune_touch_coordinator::LuneTouchCoordinator *coordinator) {
    coordinator_ = coordinator;
  }

  bool canHandle(AsyncWebServerRequest *request) const override;
  void handleRequest(AsyncWebServerRequest *request) override;
  bool isRequestHandlerTrivial() const override { return false; }

 protected:
  void handle_root_(AsyncWebServerRequest *request);
  void handle_ui_asset_(AsyncWebServerRequest *request, const char *url);
  void handle_v1_(AsyncWebServerRequest *request, const char *path);
  void send_text_(AsyncWebServerRequest *request, int code, const char *content_type,
                  const char *body, bool cors = false, const char *cache_control = nullptr);
  void send_gzip_chunked_(AsyncWebServerRequest *request, const char *content_type,
                          const uint8_t *data, size_t length, const char *cache_control);
  void send_json_(AsyncWebServerRequest *request, const char *body);
  void send_ok_(AsyncWebServerRequest *request, const char *data = "{}");
  void send_error_(AsyncWebServerRequest *request, int code, const char *err_code, const char *message);
  void send_write_result_(AsyncWebServerRequest *request, bool accepted, int failure_code = 400);
  void handle_v1_post_(ApiRequest &api, const char *path);
  void send_json_(ApiRequest &api, const char *body);
  void send_ok_(ApiRequest &api, const char *data = "{}");
  void send_error_(ApiRequest &api, int code, const char *err_code, const char *message);
  void send_write_result_(ApiRequest &api, bool accepted, int failure_code = 400);
  esp_err_t handle_raw_post_(httpd_req_t *request);
  void write_wifi_json_(char *out, size_t capacity);
  static esp_err_t raw_post_handler_(httpd_req_t *request);
  bool lock_api_buffers_(uint32_t timeout_ms = 2000);
  void unlock_api_buffers_();

  web_server_base::WebServerBase *base_{nullptr};
  lune_touch_coordinator::LuneTouchCoordinator *coordinator_{nullptr};
  // Allocated from PSRAM in setup() — too large for dram0 BSS on ESP32-S3.
  // Sized for multi-controller houses (merged rooms + dense zone objects).
  // 16 KiB truncated mid-string once a second controller landed (~position 16432).
  static constexpr size_t DATA_BUF_SIZE = 32768;
  static constexpr size_t RESPONSE_BUF_SIZE = 33856;  // data + v1 envelope
  char *data_buf_{nullptr};
  char *response_buf_{nullptr};
  // Shared buffers: browser refresh fires many parallel GETs; without a lock
  // concurrent handlers corrupt each other's JSON mid-string.
#if defined(USE_ESP32)
  SemaphoreHandle_t api_buf_lock_{nullptr};
#endif
};

}  // namespace lune_touch_dashboard
}  // namespace esphome
