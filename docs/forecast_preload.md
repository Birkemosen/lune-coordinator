# Forecast Preload (wind-aware, coordinator-owned)

`forecast` / `forecast_model` is the weather-forecast-driven per-zone preheating
producer for the Lune Touch / Mini coordinator. It exists because a Mitsubishi Ecodan +
Odin optimizer plans the *whole house's* heat as a single signal, but a real house loses
heat unevenly: depending on wind direction and speed, only one or two rooms may lag
during a winter storm. Odin cannot route heat to a specific facade; the Lune coordinator
can, while each Lune V6 keeps validating and clamping the resulting commands locally.

## How it works

1. **Fetch.** A coordinator task pulls a
   48 h Open-Meteo forecast over HTTPS every `fetch_interval_s` (default 1 h): hourly
   temperature, wind speed, wind direction, shortwave radiation, and the provider's hourly
   Unix timestamps. Decisioning starts at the first provider hour at or after wall-clock
   time.

2. **Per-zone weather load.** Each forecast hour is reduced to a dimensionless load per zone
   ([forecast_model.h](../components/forecast/forecast_model.h)):

   ```
   wind_alignment = max over the zone's exterior walls of cos(wind_dir − wall_normal), ≥ 0
   load = wind_exposure × wind_alignment × (wind_speed / 10 m/s)
            × max(0, indoor_ref − outdoor) / 10 K
            − solar_gain × (shortwave / 800 W/m²),  floored at 0
   ```

3. **Heuristic 2.0 preload.** Scan the active lead window (configured thermal lead, optionally
   extended by learned heat-gain lead and closed-loop lead bias — never shortened below
   configured). Above `load_threshold`, compute a base offset
   `(peak − threshold) × gain × priority × learned_gain_scale`, optionally raised by thermal
   sizing from learned cool-loss / heat-gain. Cap at `max_offset_c` / house `max_boost_c`,
   then apply **timing scale** (lower far from peak, full near peak, floor 0.4) and **taper**
   after the peak as load falls.

4. **Plant horizon.** When enabled, simulate zone temperature over the lead window and choose
   the smallest offset that keeps predicted temp within the comfort band
   (`scheduled ± comfort_band_c`). Falls back to the heuristic if learning is thin.

5. **Closed-loop learning.** While preload is active, compare live temperature to the comfort
   band. Undershoot raises gain scale (and may extend lead bias); overshoot eases gain. Values
   persist in NVS (`weather/preload_lrn`) and never shorten configured lead.

6. **Apply.** Touch sends each active offset through the V6 expiring `setpoint-command` path
   with the usual clamp, dedupe, and reachability gates.

## Comfort / preload chart

Zone JSON includes a `comfort_chart` block (expected / scheduled / comfort min·max /
preload_active series) for the dashboard SVG. Weather forecast chart draws amber preload
window bars from active decisions. This is model-predictive preload visualization — not a
second heat-pump controller.

## Configuration

| Field | Default | Meaning |
|---|---|---|
| `load_threshold` | 1.0 | Load units before preload kicks in |
| `gain_c_per_load` | 0.5 | °C offset per load unit above threshold |
| `max_offset_c` / `weather.max_boost_c` | 1.5 | Model / house cap |

**Cap interaction:** normal Touch forecast decisions set `params.max_offset_c` from
house-level `weather.max_boost_c`. Per-zone `max_offset_c` remains imported V6
legacy metadata for diagnostics only. The house cap wins for preload commands;
V6 may still clamp further locally.

When ODIN timing bias is enabled, the combined weather+bias offset is also capped
by `preload_bias_offset_cap_c` (see `odin_plan_ingestion.md`).

| `indoor_ref_c` | comfort setpoint | Cold term reference |
| per-zone `thermal_lead_h` | 4 | Hours of slab charging before a load peak |
| `comfort_band_c` | 0.5 | ± around scheduled for horizon / chart |

## Slab charge (capacity shortfall)

Preload raises a setpoint ahead of a load peak. For rooms whose heat loss in a cold
storm exceeds what the floor can deliver (wind-exposed timber floors), Touch also plans
**slab charge**: it stores the shortfall in the slab beforehand and arms per-zone absorb
on V6 so the valve stays open even when sun has kept the room warm. Model, formulas and
reasoning: [`house_balancing_and_weather.md`](house_balancing_and_weather.md)
(`slab_charge.h`, tests in `tests/forecast/test_slab_charge.cpp`).

## Testing

```bash
make test-forecast   # or: make test from the repo root
```
