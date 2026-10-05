# Falsifying absurd `heat_gain` → zero preload

## Hypothesis

When `thermal_model.heat_gain_c_per_h` is on the order of **~37 °C/h** (a probe
jump divided by a short Δt), plant-horizon simulation believes any zone can
reach an arbitrary offset in one hour. It therefore picks the **smallest**
offset that holds the comfort band — often **0** — and Weather shows
“No preload is needed” even with a wind peak ahead.

An office already **1.2 °C above setpoint** can alone suppress preload; that
must be separated from the absurd-gain path.

## Field procedure (device)

1. Open the zone JSON (`GET /zones` or zone detail) and note
   `thermal_model.heat_gain_c_per_h` and whether a weather peak exists in
   `GET /forecast` decisions.
2. If gain ≫ 0.5 °C/h, set it to **0.2** for one zone via
   `POST /zones/{room_id}/thermal-model` with
   `{"heat_gain_c_per_h": 0.2, "cool_loss_c_per_h": <unchanged or 0.12>, "samples": 12}`
   (endpoint added with A1).
3. Trigger forecast recompute (`POST /forecast/fetch` or wait for the task).
4. **Report:**
   - Did an active preload decision appear for that zone?
   - Was the room still above setpoint at decision time?

### Interpretation

| Result | Meaning |
| --- | --- |
| Preload window appears after gain=0.2 | Absurd gain was suppressing horizon → A1 is P0-critical |
| Still no preload, room above setpoint | Overshoot alone explains it; A1 still needed to stop bad learning |
| Still no preload, room at/below setpoint, storm ahead | Horizon/heuristic bug elsewhere — dig before blaming gain |

## Host falsification (automated)

`tests/forecast/test_forecast_model.cpp` includes
`test_absurd_heat_gain_suppresses_horizon` which asserts that thermal-confident
params with `learned_heat_gain_c_per_h = 37` collapse the horizon offset toward
zero relative to a realistic 0.2 °C/h gain under the same storm forecast.

After A1, `thermal_confident_` rejects gains outside the τ-consistent band, so
horizon falls back to default rates and the absurd path cannot silence preload.
