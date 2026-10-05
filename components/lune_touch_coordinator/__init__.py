import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome.components import time
from esphome.components.esp32 import include_builtin_idf_component

DEPENDENCIES = ["network"]
AUTO_LOAD = ["json"]

CONF_NODE_STALE_AFTER = "node_stale_after"
CONF_TIME_ID = "time_id"

lune_touch_ns = cg.esphome_ns.namespace("lune_touch_coordinator")
LuneTouchCoordinator = lune_touch_ns.class_("LuneTouchCoordinator", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_ID): cv.declare_id(LuneTouchCoordinator),
        cv.Optional(CONF_NODE_STALE_AFTER, default="300s"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_TIME_ID): cv.use_id(time.RealTimeClock),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_node_stale_after_ms(config[CONF_NODE_STALE_AFTER]))
    if CONF_TIME_ID in config:
        time_var = await cg.get_variable(config[CONF_TIME_ID])
        cg.add(var.set_time(time_var))

    include_builtin_idf_component("esp_http_client")
    include_builtin_idf_component("esp_lcd")
    include_builtin_idf_component("espcoredump")
    include_builtin_idf_component("mbedtls")
    include_builtin_idf_component("mqtt")
    include_builtin_idf_component("nvs_flash")
