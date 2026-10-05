#include "circulation_pump.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace esphome::lune_touch_coordinator::circulation_pump;

int main() {
  char url[320];
  assert(build_sensor_read_url("alpha2go.local", 80, "pump_flow", url, sizeof(url)));
  assert(std::strcmp(url, "http://alpha2go.local:80/sensor/pump_flow") == 0);
  assert(build_sensor_read_url("alpha2go.local", 80, "pump_head_pressure", url, sizeof(url)));
  assert(std::strcmp(url, "http://alpha2go.local:80/sensor/pump_head_pressure") == 0);
  assert(build_sensor_read_url("192.168.1.90", 80, "pump_power", url, sizeof(url)));
  assert(std::strcmp(url, "http://192.168.1.90:80/sensor/pump_power") == 0);
  assert(build_sensor_read_url("alpha2go.local", 80, "Pump Flow", url, sizeof(url)));
  assert(std::strstr(url, "Pump%20Flow") != nullptr);
  assert(!build_sensor_read_url("bad host", 80, "pump_flow", url, sizeof(url)));
  assert(!build_sensor_read_url("alpha2go.local", 0, "pump_flow", url, sizeof(url)));
  assert(!build_sensor_read_url("alpha2go.local", 80, "", url, sizeof(url)));

  float value = NAN;
  assert(parse_sensor_response("{\"id\":\"sensor-pump_flow\",\"value\":0.84,\"state\":\"0.84 m3/h\"}",
                               &value));
  assert(std::fabs(value - 0.84f) < 0.0001f);
  assert(parse_sensor_response("{\"value\":3.10}", &value) && std::fabs(value - 3.10f) < 0.0001f);
  assert(parse_sensor_response("{\"state\":\"22.4\"}", &value) && std::fabs(value - 22.4f) < 0.0001f);
  assert(!parse_sensor_response("{\"state\":\"unknown\"}", &value));
  std::puts("Circulation pump tests passed.");
}
