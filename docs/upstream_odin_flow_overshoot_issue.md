# Upstream issue draft: flow overshoot during Odin heating blocks

Copy the title and body below into a new GitHub issue:

**Repo:** https://github.com/gekkekoe/esphome-ecodan-hp/issues/new

Part of the Odin 2.0 beta test; covers both Odin and Asgard. Background on how Touch, V6, Asgard and Odin share
the flow temperature: [`lune_whole_house_flow_temperature.md`](lune_whole_house_flow_temperature.md).

---

**Title:** `[Odin 2.0 beta] Heat pump runs ~9 K above the commanded flow temperature during Odin heating blocks, and Odin doesn't see it`

## Setup

- Odin 2.0 beta (ODIN Engine v2), with Asgard as the heat pump interface — the questions below cover both
- Mitsubishi Ecodan via Asgard; zone 1 underfloor heating; flow rate ~16 l/min
- Asgard: `operating_mode_z1 = Heat Flow Temperature`, `room_temp_source_z1 = Asgard Virtual Thermostat`, `auto_adaptive_control_enabled = true`, `maximum_heating_flow_temp = 42`, `minimum_heating_flow_temp = 24`,
  `min_compressor_on_time = 5`, `thermostat_hysteresis_z1 = 0.5`
- Odin plans and drives heating via the Asgard MQTT forwarder (`hp/hp1/commands/setpoint`)
- Lune Touch pushes the measured, weighted house temperature to Asgard and can raise Odin's comfort band by up to +0.3 °C when a room lags
- Bidding zone DK1, outdoor 12–17 °C

## What happens (7 Oct, block planned 13:00–16:00)

Odin's plan for the block is ≈ 5.2 kW heat (≈ 0.8 kWh electricity per hour). Odin sends a flow command every 5 minutes:

```
13:19  cmd flow=33.7°C mode=2 … feed=33.0 … room=22.8 out=15.0 plan=5.20kWh
13:24  cmd flow=33.4°C mode=2 … feed=33.0 …
13:29  cmd flow=33.7°C mode=2 … feed=33.5 …
13:34  cmd flow=34.2°C mode=2 … feed=33.5 …
```

Asgard at the same time:

| Time | Compressor | `hp_feed_temp` | Return | `flow_z1_setpoint` | `flow_z1_current` |
|---|---|---|---|---|---|
| 13:04 | on, 26 Hz | 24.5 | 23.0 | 25.6 | |
| 13:10 | 62 Hz | 32.0 | 24.0 | 27.8 | |
| 13:25 | 62 Hz | 36.5 | 27.0 | 29.8 | |
| 13:31 | 62 Hz | 42.5 | ~32 | 33.7 | 33.4 |
| 13:34 (after cmd) | 62 Hz | 42.5 | | **34.2** | |
| 13:36 | 62 Hz | 43.0 | | 34.2 | |

## Findings

1. Odin's flow command reaches Asgard within seconds: `flow_z1_setpoint` follows each command (33.7 → 34.2).
2. The heat pump does not hold that target. The compressor goes to 62 Hz within minutes and stays there, and the feed rises to 42.5–43 °C. That is ~9 K above the target and at the configured 42 °C max.
3. Odin doesn't see the overshoot. It logs `feed=33.5` while Asgard reports `hp_feed_temp = 42.5–43.0`. Odin's value matches Asgard's `flow_z1_current` (33.4), which seems to mirror the setpoint rather than measure the water.
4. Zone 1 is in Heat Flow Temperature mode, so the Ecodan should regulate to `flow_z1_setpoint`, yet it runs at full speed and overshoots.
5. Actual heat delivered is ≈ 11 kW (16 l/min, ΔT ≈ 10 K), about twice the 5.2 kW in Odin's plan. Meanwhile the house is 22.8 °C against a comfort target of 21.6 °C, and no room is calling for heat.

## Why it matters

Measured COP, last 7 days, space heating, outdoor 15–18 °C (Asgard energy counters):

| Flow | COP |
|---|---|
| 28–32 °C | 5.9 |
| 32–36 °C | 5.4 |
| 36–40 °C | 5.5 |
| 40–44 °C | 4.2 |

At ~42 °C the same 15 kWh of heat costs ≈ 3.6 kWh of electricity, against ≈ 2.5 kWh at ~30 °C (~30 % more). That cancels much of the gain from moving heating into the cheapest hours. The planner seems to assume COP ≈ 6.5 (5.2 kW heat per 0.8 kWh).

## Questions – Odin

1. Which value does Odin use as the actual flow temperature, the heat pump feed (`hp_feed_temp`) or the zone flow value (`flow_z1_current`)? If it's the zone value, Odin can't see that the pump runs ~9 K above the command.
2. Should Odin correct when the measured feed is far above the command (lower the target, reduce the block, or stop), or does it rely on Asgard/Ecodan to hold the target?
3. Can a planned block be limited in power or flow temperature? For example, deliver the planned 5.2 kW instead of the pump's maximum, or cap flow at ~32–34 °C and spread the block over more cheap hours.
4. Does the planner model COP as a function of flow temperature, or use a fixed COP?

## Questions – Asgard

5. When Odin commands a flow temperature, how is it applied to the Ecodan? Directly as the zone 1 flow setpoint, or through Asgard's auto-adaptive algorithm?
6. In Heat Flow Temperature mode with `flow_z1_setpoint` at 33–34 °C, why does the Ecodan run at 62 Hz and reach 42–43 °C? Is this the Ecodan's own behaviour (e.g. minimum output, start-up or low flow rate), or does auto-adaptive raise the demand? Would disabling auto-adaptive while Odin drives the block make the Ecodan hold the commanded flow?
7. `flow_z1_current` reads 33.4 while `hp_feed_temp` reads 42.5. What does `flow_z1_current` represent, and should the forwarder send `hp_feed_temp` to Odin as the actual feed?

## Data available

Minute data from Asgard `/dashboard/history` (7 days), `/dashboard/state` snapshots taken every 15 s around a command, and Odin's `/api/logs`.
