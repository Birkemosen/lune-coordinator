import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome.components.esp32 import include_builtin_idf_component

DEPENDENCIES = ["network"]
AUTO_LOAD = ["json"]  # forecast_model parses Open-Meteo JSON with ArduinoJson

CONF_ZONE_CONTROLLER_ID = "zone_controller_id"
CONF_CONFIG_STORE_ID = "config_store_id"

forecast_ns = cg.esphome_ns.namespace("forecast")
hv6_ns = cg.esphome_ns.namespace("hv6")
Hv6Forecast = forecast_ns.class_("Hv6Forecast", cg.Component)
Hv6ZoneController = hv6_ns.class_("Hv6ZoneController", cg.Component)
Hv6ConfigStore = hv6_ns.class_("Hv6ConfigStore", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_ID): cv.declare_id(Hv6Forecast),
        cv.Required(CONF_ZONE_CONTROLLER_ID): cv.use_id(Hv6ZoneController),
        cv.Required(CONF_CONFIG_STORE_ID): cv.use_id(Hv6ConfigStore),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    zone_ctrl = await cg.get_variable(config[CONF_ZONE_CONTROLLER_ID])
    cg.add(var.set_zone_controller(zone_ctrl))

    config_store = await cg.get_variable(config[CONF_CONFIG_STORE_ID])
    cg.add(var.set_config_store(config_store))

    # esp_http_client over TLS (esp_crt_bundle lives in mbedtls); nvs_flash for
    # the forecast cache. All used directly from a FreeRTOS task on Core 1.
    include_builtin_idf_component("esp_http_client")
    include_builtin_idf_component("mbedtls")
    include_builtin_idf_component("nvs_flash")
