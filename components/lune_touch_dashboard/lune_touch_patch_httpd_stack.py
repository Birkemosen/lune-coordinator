"""Tune ESP-IDF HTTP server stack for the Lune Touch dashboard.

The stock ESPHome bump (+256 on the IDF default) is too small for Touch's
dashboard handlers (large /zones + /forecast JSON writers). A 16 KiB stack
can fail httpd_start() after LVGL has fragmented internal RAM — port 80 then
never listens, while the dashboard still logs "endpoints registered".

8 KiB was a middle ground until multi-controller zone payloads landed; that
size now overflows the httpd task (serial: "stack overflow in task httpd").
Use 12 KiB: enough for the writers, still more likely to start than 16 KiB.
"""

from pathlib import Path


Import("env")

project_dir = Path(env.subst("$PROJECT_DIR"))
source = project_dir / "src/esphome/components/web_server_idf/web_server_idf.cpp"
text = source.read_text(encoding="utf-8")

STACK_SIZE = 12288
stack_line = f"  config.stack_size = {STACK_SIZE};"

replacements = [
    ("  config.stack_size = config.stack_size + 256;", stack_line),
    ("  config.stack_size = 16384;", stack_line),
    ("  config.stack_size = 8192;", stack_line),
]

changed = False
for old, new in replacements:
    if old in text:
        text = text.replace(old, new, 1)
        changed = True
        break

if stack_line not in text:
    raise RuntimeError(f"Unexpected ESPHome HTTP server source; cannot size {source}")

fail_needle = "  if (httpd_start(&this->server_, &config) == ESP_OK) {"
fail_patch = (
    "  const esp_err_t httpd_err = httpd_start(&this->server_, &config);\n"
    "  if (httpd_err != ESP_OK) {\n"
    '    ESP_LOGE(TAG, "httpd_start(port=%u, stack=%u) failed: %s",\n'
    "             static_cast<unsigned>(config.server_port),\n"
    "             static_cast<unsigned>(config.stack_size), esp_err_to_name(httpd_err));\n"
    "  }\n"
    "  if (httpd_err == ESP_OK) {"
)
if fail_needle in text and "httpd_start(port=%u" not in text:
    text = text.replace(fail_needle, fail_patch, 1)
    changed = True

if changed:
    source.write_text(text, encoding="utf-8")
