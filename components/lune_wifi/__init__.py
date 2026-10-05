"""Runtime WiFi credentials for Lune firmware.

The shipped YAML has no STA ssid/password, so ESPHome keeps saved credentials
under its fixed preference key and they survive OTA. This component:

* seeds that key once from build secrets (``seed_ssid`` / ``seed_password``)
  when nothing is saved yet, so a device that is upgraded from a build with
  compiled-in credentials stays on its network, and
* switches network at runtime for the web UI, reverting to the previous
  network when the new one does not connect (same flow as ``wifi.configure``).
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

DEPENDENCIES = ["wifi"]

CONF_SEED_SSID = "seed_ssid"
CONF_SEED_PASSWORD = "seed_password"
CONF_CONNECT_TIMEOUT = "connect_timeout"

lune_wifi_ns = cg.esphome_ns.namespace("lune_wifi")
LuneWifi = lune_wifi_ns.class_("LuneWifi", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_ID): cv.declare_id(LuneWifi),
        cv.Optional(CONF_SEED_SSID, default=""): cv.All(cv.string, cv.Length(max=32)),
        cv.Optional(CONF_SEED_PASSWORD, default=""): cv.All(cv.string, cv.Length(max=64)),
        cv.Optional(CONF_CONNECT_TIMEOUT, default="30s"): cv.positive_time_period_milliseconds,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    cg.add_define("USE_LUNE_WIFI")
    await cg.register_component(var, config)
    if config[CONF_SEED_SSID]:
        cg.add(var.set_seed(config[CONF_SEED_SSID], config[CONF_SEED_PASSWORD]))
    cg.add(var.set_connect_timeout(config[CONF_CONNECT_TIMEOUT]))
