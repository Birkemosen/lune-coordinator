#include "asgard_adapter.h"

#include <cassert>
#include <cstring>

using namespace esphome::lune_touch_coordinator::asgard_adapter;

class FakeTransport final : public Transport {
 public:
  bool get(const char *url, char *body, size_t body_len, int *status) override {
    std::strncpy(last_url, url, sizeof(last_url) - 1);
    if (response_body[0] != '\0')
      std::snprintf(body, body_len, "%s", response_body);
    else
      std::snprintf(body, body_len, "{\"value\":21.25}");
    *status = 200;
    return true;
  }
  bool post_empty(const char *url, int *status) override {
    std::strncpy(last_url, url, sizeof(last_url) - 1);
    *status = 200;
    return true;
  }
  char last_url[320]{};
  char response_body[384]{};
};

int main() {
  Config config{"asgard.local", 80, "Virtual Thermostat Input z1"};
  Compatibility compat = compatibility(config);
  assert(compat.physical_temperature == CapabilityStatus::READY);
  assert(compat.target_sync == CapabilityStatus::UNCONFIGURED);
  assert(compat.operating_state == CapabilityStatus::UNSUPPORTED);
  char url[320];
  assert(build_physical_write_url(config, 21.0f, url, sizeof(url)));
  assert(std::strcmp(url, "http://asgard.local:80/number/Virtual%20Thermostat%20Input%20z1/set?value=21.00") == 0);
  assert(build_physical_read_url(config, url, sizeof(url)));
  Config custom{
      "asgard.local", 80, "Virtual Thermostat Input z1",
      "http://{host}:{port}/custom/{entity}?v={value}",
      "http://{host}:{port}/custom/{entity}",
  };
  assert(build_physical_write_url(custom, 18.25f, url, sizeof(url)));
  assert(std::strcmp(url, "http://asgard.local:80/custom/Virtual%20Thermostat%20Input%20z1?v=18.25") == 0);
  assert(build_physical_read_url(custom, url, sizeof(url)));
  assert(std::strcmp(url, "http://asgard.local:80/custom/Virtual%20Thermostat%20Input%20z1") == 0);
  Config generic_http{"heat.local", 80, "", "http://{host}:{port}/api/temp?t={value}",
                      "http://{host}:{port}/api/temp"};
  compat = compatibility(generic_http);
  assert(compat.physical_temperature == CapabilityStatus::READY);
  assert(build_physical_write_url(generic_http, 19.5f, url, sizeof(url)));
  assert(std::strcmp(url, "http://heat.local:80/api/temp?t=19.50") == 0);
  assert(build_physical_read_url(generic_http, url, sizeof(url)));
  assert(std::strcmp(url, "http://heat.local:80/api/temp") == 0);
  FakeTransport fake;
  float value = NAN;
  int status = 0;
  assert(read_number(fake, url, &value, &status));
  assert(value == 21.25f && status == 200);
  assert(parse_number_response("{\"state\":\"20.50\"}", &value) && value == 20.5f);
  assert(!parse_number_response("{\"state\":\"unknown\"}", &value));

  config.climate_entity = "virtual_thermostat";
  compat = compatibility(config);
  assert(compat.target_sync == CapabilityStatus::READY);
  assert(build_climate_target_write_url(config, 21.25f, url, sizeof(url)));
  assert(std::strcmp(url, "http://asgard.local:80/climate/virtual_thermostat/set?target_temperature=21.3") == 0 ||
         std::strcmp(url, "http://asgard.local:80/climate/virtual_thermostat/set?target_temperature=21.2") == 0 ||
         std::strcmp(url, "http://asgard.local:80/climate/virtual_thermostat/set?target_temperature=21.25") == 0);
  // %.1f rounds 21.25 → implementation-defined; use exact tenth.
  assert(build_climate_target_write_url(config, round_tenths(21.24f), url, sizeof(url)));
  assert(std::strcmp(url, "http://asgard.local:80/climate/virtual_thermostat/set?target_temperature=21.2") == 0);
  assert(build_climate_target_read_url(config, url, sizeof(url)));
  assert(std::strcmp(url, "http://asgard.local:80/climate/virtual_thermostat") == 0);
  assert(parse_climate_target("{\"target_temperature\":21.5}", &value) && value == 21.5f);

  config.bias_entity = "setpoint_bias";
  compat = compatibility(config);
  assert(compat.bias_sync == CapabilityStatus::READY);
  assert(build_bias_write_url(config, 0.3f, url, sizeof(url)));
  assert(std::strstr(url, "/number/setpoint_bias/set?value=0.30") != nullptr);

  bool on = false;
  assert(parse_binary_state("{\"state\":\"ON\"}", &on) && on);
  assert(parse_binary_state("{\"state\":\"OFF\"}", &on) && !on);
  assert(std::fabs(round_tenths(21.24f) - 21.2f) < 0.001f);
  assert(std::fabs(round_tenths(21.26f) - 21.3f) < 0.001f);

  std::puts("Asgard adapter tests passed.");
}
