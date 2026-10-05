# LCD Stability Settings (Lune Touch 7B ESP32-S3)

The first Lune Touch prototype must start from the known-stable RGB/LVGL profile
proven on the Waveshare 4.3-inch ESP32-S3 display. The 7B profile keeps the
10-line RGB bounce buffer and enables XIP-from-PSRAM so flash/NVS writes do not
disable external-memory cache while the RGB ISR is refilling bounce buffers.
Do not increase buffers or switch render modes until WiFi, touch, HTTPS forecast
fetches, and V6 polling have passed a soak test.

## Stable Baseline

- ESPHome: `2026.8.0`
- ESP-IDF: ESPHome 2026.8 recommended release (`5.5.5`)
- LVGL: `9.5.0`, supplied by ESPHome's built-in integration
- Build toolchain: ESPHome's supported PlatformIO path with the matching
  pioarduino platform selected by ESPHome
- LVGL render mode: `PARTIAL`
- RGB panel frame buffers: `1`
- Frame buffer location: PSRAM
- RGB bounce buffer: enabled, `LCD_HRES * 10`
- RGB stream recovery: restart in VSYNC enabled
- External-memory cache during flash/NVS writes: XIP from PSRAM enabled
- Active pixel clock: `30MHz`, matching the Waveshare 7B demo. The 7B porch
  totals are 1386 x 661, so refresh is `pclk / 916146`: 20 MHz is ~22 Hz and
  flickers on this RGB panel; 30 MHz is ~33 Hz. ESPHome does not allow 120
  MHz octal PSRAM, so the bus stays at 80 MHz. XIP-from-PSRAM, 64-byte
  cache lines, and chunked dashboard reads are what keep the bounce-buffer
  ISR in budget at this clock.
- LVGL draw buffer target: 10 lines when using the custom ESP-IDF path. ESPHome
  LVGL uses coarser fractional buffering, so the 7B bringup profile must be
  soak-tested separately before increasing UI complexity.
- Flush callback copies only the dirty rectangle via `esp_lcd_panel_draw_bitmap`
- ESPHome releases the LVGL draw buffer only after the RGB driver has
  synchronously copied the dirty rectangle into the panel framebuffer.
- RGB DMA burst: `64`
- LVGL tick: 2 ms periodic timer
- LVGL task: stack `16384`, priority `2`, pinned to core `1`

The active Waveshare 7B hardware profile is documented in
`waveshare_esp32_s3_touch_lcd_7b.md` and encoded in
`packages/display/lvgl_stability.yaml`.

## Memory Rules

- Keep WiFi enabled during display tests.
- Keep `CONFIG_MBEDTLS_DEFAULT_MEM_ALLOC=y`.
- Do not enable `CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC`.
- Keep the internal draw buffer at 10 lines until measured headroom proves a
  larger buffer is safe.
- Keep the RGB bounce buffer at 10 lines until measured headroom proves a larger
  buffer is safe.
- Keep `CONFIG_SPIRAM_XIP_FROM_PSRAM=y` while the framebuffer lives in PSRAM.
- Keep the ESP32-S3 data-cache line at 64 bytes with RGB bounce-buffer mode.
- Keep compiler optimization on `PERF`; the refill ISR is deadline-sensitive.
- Keep RGB restart in VSYNC enabled for recovery from transient DMA underruns.
- Serve the versioned dashboard bundle from 1 KiB internal-RAM staging chunks
  and cache it in the browser. Do not stream a large PROGMEM range directly to
  a slow client.
- Use LVGL's built-in `LV_SYMBOL_*`/Font Awesome glyph range for local-display
  icons. The built-in Montserrat fonts do not contain arbitrary Unicode arrows,
  geometric circles, middle dots, or mathematical minus signs; unsupported
  characters render as rectangles.
- Do not force `bb_invalidate_cache=true`.

## Failure Symptoms

- Touch flicker or visible tearing usually means PSRAM/display traffic is too
  high or the flush-ready path changed.
- `wifi:malloc buffer fail` usually means internal SRAM pressure is too high.
- TLS `ALLOC_FAIL` during forecast/WebSocket-style clients usually means mbedTLS
  was moved into internal memory.

## Bringup Gate

Lune Touch display bringup is not accepted until it survives 24 hours with:

- touch interaction,
- WiFi connected,
- repeated V6 polling,
- HTTPS forecast fetches,
- no display artifacts,
- no task watchdog resets,
- no WiFi malloc failures,
- no coredump.
