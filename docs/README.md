# Lune Touch — documentation

Everything about Lune Touch (and Lune Mini), the house coordinator for one or more Lune V6 boards: use, API,
house control with Asgard/Odin, the wall display and testing. The chip is the file name; the link says what the
document is for.

**Lune documentation:** [Lune V6](https://github.com/Birkemosen/lune/blob/main/docs/README.md) · **Lune Touch** (this page) · [Design system](https://github.com/Birkemosen/lune-design-system/blob/main/docs/README.md)

---

**Start here**

- `README` [What Lune Touch and Lune Mini are, build and flash](../README.md)
- `touch-ui/README` [The web UI on the Lune design system](../web/touch-ui/README.md)

**Use and setup**

- `Manual` [Installer and operator manual](Manual.md) — the `?` help in the UI links here

**API**

- `api_v1` [Lune Touch API v1](api_v1.md)
- `absorb_arm_fixtures` [Absorb-arm / timing-bias fixtures shared with V6](absorb_arm_fixtures.md)

**House control (Touch + V6 + Asgard/Odin)**

- `house_balancing_and_weather` [Who decides what: balancing, absorb and weather](house_balancing_and_weather.md)
- `lune_whole_house_flow_temperature` [Keeping the heat pump's flow temperature low and steady](lune_whole_house_flow_temperature.md)
- `forecast_preload` [Weather forecast preload per zone](forecast_preload.md)
- `odin_plan_ingestion` [Reading Odin's heating plan](odin_plan_ingestion.md)
- `falsify_heat_gain_preload` [Guarding against absurd heat gain in preload](falsify_heat_gain_preload.md)
- `lune_house_signal_contract` [House signal: physical temperature and comfort target to Asgard](lune_house_signal_contract.md)
- `lune_asgard_authority_state_machine` [Who may write the house temperature to Asgard](lune_asgard_authority_state_machine.md)

**Wall display (LVGL)**

- `display/README` [Wall display handoff](display/README.md)
- `display/SCREENS` [Screens, sizes and widget tree](display/SCREENS.md)
- `display/DESIGN-display` [Design rules for the wall (from the design system, section 13)](display/DESIGN-display.md)
- `display/PROMPT` [Prompt used to build the screens](display/PROMPT.md)
- `LCD_STABILITY` [LCD stability settings for the 7B board](LCD_STABILITY.md)
- `waveshare_esp32_s3_touch_lcd_7b` [Waveshare ESP32-S3-Touch-LCD-7B notes](waveshare_esp32_s3_touch_lcd_7b.md)
- `CONVENTIONS` [Firmware conventions for the display/Wi-Fi memory profile](CONVENTIONS.md)

**Testing**

- `field_validation` [Field validation checklist (needs real hardware)](field_validation.md)
- `simulation` [End-to-end simulation](../tests/simulation/README.md)

**Upstream issues (Asgard / Odin)**

- `upstream_odin_flow_overshoot_issue` [Odin 2.0 beta: flow overshoot during heating blocks](upstream_odin_flow_overshoot_issue.md)
- `upstream_odin_api_plan_issue` [Versioned Odin plan API](upstream_odin_api_plan_issue.md)
- `upstream_asgard_vt_staleness_issue` [Staleness timeout on Asgard's Virtual Thermostat Input](upstream_asgard_vt_staleness_issue.md)

**Product family**

- `lune_brand_architecture` [Birkemosen product architecture](lune_brand_architecture.md)
