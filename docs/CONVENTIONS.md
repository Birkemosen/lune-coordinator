# Lune Touch Firmware Conventions

These rules are load-bearing for the ESP32-S3 display/WiFi memory profile.

## Thread Safety

- All LVGL widget calls must hold `lvgl_api_lock`.
- Task-to-UI updates go through `ui_event_queue`; events are posted
  non-blocking.
- Blocking I2C writes, including display backlight changes, must not run on the
  LVGL task.
- Network clients run outside the LVGL task and publish UI state through events.

## NVS Namespaces

| Namespace | Owner | Contents |
|---|---|---|
| `wifi` | WiFi manager | SSID and password |
| `touch` | coordinator | identity, paired V6 nodes, zone registry |
| `ui` | UI shell | onboarding and display settings |
| `weather` | forecast service | forecast cache metadata |
| `ledger` | command service | recent command outcomes |

## Init Order

1. Config/NVS init
2. Coordinator model init
3. LCD/LVGL init
4. Persisted UI restore
5. WiFi init
6. V6 discovery and polling
7. Weather fetch

## Safety Boundary

Lune Touch may recommend and send expiring coordinator commands. Lune V6 must
validate, clamp, expire, and report every command locally, and must continue
safe conservative control when Touch is offline.
