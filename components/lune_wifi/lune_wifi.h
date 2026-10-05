#pragma once

#include <cstdint>
#include <cstring>
#include <string>

#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/core/preferences.h"
#include "esphome/components/wifi/wifi_component.h"

namespace esphome::lune_wifi {

// ESPHome stores credentials under this fixed key when the YAML has no STA
// ssid (wifi_component.cpp, WiFiComponent::start). Unlike the config-hash key
// it survives OTA, so runtime credentials stay put across firmware updates.
static constexpr uint32_t SAVED_WIFI_HASH = 88491487UL;

enum class SwitchResult : uint8_t { NONE, PENDING, CONNECTED, REVERTED, FAILED };

class LuneWifi;
// Set by the constructor; nullptr when the component is not in the build.
inline LuneWifi *global_lune_wifi = nullptr;  // NOLINT

class LuneWifi : public Component {
 public:
  LuneWifi() { global_lune_wifi = this; }

  // Before WiFi starts, so a seeded network is loaded by WiFiComponent::start().
  float get_setup_priority() const override { return setup_priority::WIFI + 10.0f; }

  void set_seed(const std::string &ssid, const std::string &password) {
    this->seed_ssid_ = ssid;
    this->seed_password_ = password;
  }
  void set_connect_timeout(uint32_t ms) { this->connect_timeout_ms_ = ms; }

  void setup() override { this->seed_if_empty_(); }

  // Switch to a new network. Main loop only: from an HTTP handler, wrap in defer().
  // Returns false when a switch is already running.
  bool request_switch(const std::string &ssid, const std::string &password) {
    if (this->result_ == SwitchResult::PENDING || ssid.empty())
      return false;
    auto *wifi = wifi::global_wifi_component;
    char current[wifi::SSID_BUFFER_SIZE];
    if (wifi->is_connected() && ssid == wifi->wifi_ssid_to(current)) {
      // Same network, possibly a new password: store it, nothing to switch.
      wifi->save_wifi_sta(ssid, password);
      this->target_ssid_ = ssid;
      this->result_ = SwitchResult::CONNECTED;
      return true;
    }
    this->target_ssid_ = ssid;
    this->old_sta_ = wifi->get_sta();
    this->reverting_ = false;
    // Same order as wifi.configure: disable first, otherwise WiFiComponent
    // falls back to the stored STA while still associated with the old one.
    wifi->disable();
    wifi->save_wifi_sta(ssid, password);
    wifi->enable();
    this->deadline_ms_ = millis() + this->connect_timeout_ms_;
    this->result_ = SwitchResult::PENDING;
    ESP_LOGI(TAG, "Switching WiFi to '%s'", ssid.c_str());
    return true;
  }

  void loop() override {
    if (this->result_ != SwitchResult::PENDING)
      return;
    auto *wifi = wifi::global_wifi_component;
    if (wifi->is_connected()) {
      char current[wifi::SSID_BUFFER_SIZE];
      const bool on_target = this->target_ssid_ == wifi->wifi_ssid_to(current);
      this->result_ = on_target ? SwitchResult::CONNECTED
                                : (this->reverting_ ? SwitchResult::REVERTED : SwitchResult::FAILED);
      ESP_LOGI(TAG, "WiFi switch: %s", this->result_label());
      return;
    }
    if (static_cast<int32_t>(millis() - this->deadline_ms_) < 0)
      return;
    if (this->reverting_ || this->old_sta_.get_ssid().empty()) {
      // Nothing to go back to (or the old network is gone too): keep the new
      // credentials saved; the fallback AP stays available for another try.
      this->result_ = SwitchResult::FAILED;
      ESP_LOGW(TAG, "WiFi switch failed");
      return;
    }
    ESP_LOGW(TAG, "'%s' did not connect; reverting to '%s'", this->target_ssid_.c_str(),
             this->old_sta_.get_ssid().c_str());
    wifi->disable();
    wifi->save_wifi_sta(this->old_sta_.get_ssid().c_str(), this->old_sta_.get_password().c_str());
    wifi->enable();
    this->reverting_ = true;
    this->deadline_ms_ = millis() + this->connect_timeout_ms_;
  }

  SwitchResult result() const { return this->result_; }
  const std::string &target_ssid() const { return this->target_ssid_; }
  const char *result_label() const {
    switch (this->result_) {
      case SwitchResult::PENDING:
        return "pending";
      case SwitchResult::CONNECTED:
        return "connected";
      case SwitchResult::REVERTED:
        return "reverted";
      case SwitchResult::FAILED:
        return "failed";
      case SwitchResult::NONE:
      default:
        return "none";
    }
  }

 protected:
  static constexpr const char *TAG = "lune_wifi";

  void seed_if_empty_() {
    if (this->seed_ssid_.empty())
      return;
    auto pref = global_preferences->make_preference<wifi::SavedWifiSettings>(SAVED_WIFI_HASH, true);
    wifi::SavedWifiSettings saved{};
    if (pref.load(&saved) && saved.ssid[0] != '\0')
      return;
    saved = wifi::SavedWifiSettings{};
    std::strncpy(saved.ssid, this->seed_ssid_.c_str(), sizeof(saved.ssid) - 1);
    std::strncpy(saved.password, this->seed_password_.c_str(), sizeof(saved.password) - 1);
    pref.save(&saved);
    global_preferences->sync();
    ESP_LOGI(TAG, "Seeded WiFi credentials for '%s' from build secrets", saved.ssid);
  }

  std::string seed_ssid_;
  std::string seed_password_;
  uint32_t connect_timeout_ms_{30000};
  SwitchResult result_{SwitchResult::NONE};
  std::string target_ssid_;
  wifi::WiFiAP old_sta_;
  bool reverting_{false};
  uint32_t deadline_ms_{0};
};

}  // namespace esphome::lune_wifi
