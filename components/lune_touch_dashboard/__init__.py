import gzip
from pathlib import Path

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import web_server_base
from esphome.components.esp32 import add_extra_script
from esphome.const import CONF_ID
from esphome.core import CORE

DEPENDENCIES = ["web_server_base", "network"]
AUTO_LOAD = ["web_server_base"]

CONF_WEB_SERVER_BASE_ID = "web_server_base_id"
CONF_COORDINATOR_ID = "coordinator_id"
CONF_UI_DIST = "ui_dist"

lune_touch_dashboard_ns = cg.esphome_ns.namespace("lune_touch_dashboard")
lune_touch_coordinator_ns = cg.esphome_ns.namespace("lune_touch_coordinator")

LuneTouchDashboard = lune_touch_dashboard_ns.class_("LuneTouchDashboard", cg.Component)
LuneTouchCoordinator = lune_touch_coordinator_ns.class_("LuneTouchCoordinator", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_ID): cv.declare_id(LuneTouchDashboard),
        cv.GenerateID(CONF_WEB_SERVER_BASE_ID): cv.use_id(web_server_base.WebServerBase),
        cv.Required(CONF_COORDINATOR_ID): cv.use_id(LuneTouchCoordinator),
        cv.Optional(CONF_UI_DIST): cv.directory,
        # Legacy alias — ignored once ui_dist is set; kept so old YAML does not break mid-migrate.
        cv.Optional("dashboard_js"): cv.file_,
    }
).extend(cv.COMPONENT_SCHEMA)


def _embed_gzip_file(symbol: str, file_path: Path) -> None:
    """Embed a pre-compressed .gz file (or gzip a text asset) as PROGMEM bytes."""
    raw = Path(file_path).read_bytes()
    if str(file_path).endswith(".gz"):
        compressed = raw
    else:
        compressed = gzip.compress(raw, compresslevel=9)
    bytes_str = ", ".join(str(b) for b in compressed)
    cg.add_global(
        cg.RawExpression(f"const uint8_t {symbol}_DATA[{len(compressed)}] PROGMEM = {{{bytes_str}}};")
    )
    cg.add_global(cg.RawExpression(f"const size_t {symbol}_SIZE = {len(compressed)};"))


async def to_code(config):
    # Probing and commanding a V6 node perform a bounded outbound HTTP request
    # from the dashboard request handler.  ESP-IDF's default httpd task stack is
    # too small for that path (the request parser, HTTP client and JSON response
    # handling can be active at once), which otherwise manifests as a reboot
    # exactly when commissioning or calling a manifold.  Keep the sizing fix
    # coupled to the dashboard component so every Touch build gets it.
    add_extra_script(
        "pre",
        "lune_touch_patch_httpd_stack.py",
        Path(__file__).parent / "lune_touch_patch_httpd_stack.py",
    )
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    wsb = await cg.get_variable(config[CONF_WEB_SERVER_BASE_ID])
    cg.add(var.set_web_server_base(wsb))

    coordinator = await cg.get_variable(config[CONF_COORDINATOR_ID])
    cg.add(var.set_coordinator(coordinator))

    if CONF_UI_DIST in config:
        dist = Path(CORE.relative_config_path(config[CONF_UI_DIST]))
        _embed_gzip_file("LUNE_TOUCH_UI_CSS", dist / "lune-ui.css.gz")
        _embed_gzip_file("LUNE_TOUCH_UI_JS", dist / "ui.js.gz")
        _embed_gzip_file("LUNE_TOUCH_UI_EN", dist / "en" / "index.html.gz")
        _embed_gzip_file("LUNE_TOUCH_UI_DA", dist / "da" / "index.html.gz")
        cg.add_define("LUNE_TOUCH_HAS_UI")
