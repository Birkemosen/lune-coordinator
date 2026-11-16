# Planner history format

The shadow planner (`components/planner/`) reads and writes one record per hour. The
same records are the replay input on the host and in Docker, and later the planner's
log in Touch flash. Nothing in this format commands a heat pump.

## Why Odin's layout

Odin's `/api/migrate/hourly/export` and Asgard's minute history use the same idea: a
Unix time and a row of scaled integers, with -32768 for "no value". It is compact (64
bytes an hour, 14 days ≈ 21 KB), easy to read on an ESP32 and easy to compare with
Odin's own export. Three things are changed:

| Odin | Lune |
| --- | --- |
| The epoch is the hour's start in local wall-clock time, written as if it were UTC | The epoch is the start of the hour in real UTC |
| No header; column meaning is known only from firmware | A 64-byte header names the file kind and the column in each slot |
| Scales implicit | A fixed scale per column, listed below and in `history_record.h` |

## File

```
header   64 bytes   "LUNH" | u16 version=1 | u16 record_size=64 | u8 kind | u8 ncols | u16 0 | u8 cols[30] | pad
record   64 bytes   u32 hour_start (UTC) | int16 v[30]    (little-endian, -32768 = none)
```

`kind` 1 = house (one record per hour), 2 = room (one record per room per hour).
`cols[i]` is the column id stored in slot `i`. A reader maps by id, so a writer may
store fewer columns or a different order, and a new column never breaks an old reader.
Ids are never reused.

## House columns

| Id | Name | Unit × scale | Source in the replay |
| --- | --- | --- | --- |
| 0 | heat_sh_kwh | kWh × 100 | Asgard output by mode, scaled to Odin's metered heat |
| 1 | heat_dhw_kwh | kWh × 100 | same, hot water and legionella minutes |
| 2 | elec_kwh | kWh × 100 | Odin hourly export |
| 3 | room_c | °C × 100 | Odin's room (the virtual thermostat input), else Asgard |
| 4 | outdoor_c | °C × 100 | Open-Meteo, else Odin, else Asgard |
| 5 | wind_ms | m/s × 10 | Open-Meteo |
| 6 | wind_dir_deg | ° | Open-Meteo (direction the wind comes from) |
| 7 | solar_wm2 | W/m² | Open-Meteo shortwave radiation |
| 8 | price_eur_kwh | EUR/kWh × 10000 | Odin (pushed by Touch, incl. Danish grid tariffs) |
| 9–11 | sched_sp_c, sched_min_c, sched_max_c | °C × 100 | Odin's comfort schedule |
| 12 | run_min | min | compressor on, space heating |
| 13 | starts | count | compressor 0 → on |
| 14–15 | hp_feed_c, hp_return_c | °C × 10 | THW1/THW2 while running |
| 16–17 | z1_feed_c, z1_return_c | °C × 10 | zone probe, else THW6/THW7, while running |
| 18–19 | flow_target_c, flow_target_max_c | °C × 10 | commanded zone-1 flow setpoint, mean / max |
| 20 | output_max_kw | kW × 100 | highest reported output |
| 21 | defrost_min | min | |
| 22 | dhw_tank_c | °C × 10 | tank at hour end |
| 23 | mode_flags | bits | 1 heating, 2 hot water, 4 legionella, 8 defrost, 16 forecast price, 32 forecast weather |
| 24 | room_expected_c | °C × 100 | the running planner's expected house temperature (Odin today) |
| 25 | coldest_dev_k | K × 100 | coldest room − its target |
| 26 | humidity_pct | % | Open-Meteo |

## Room columns

| Id | Name | Unit × scale |
| --- | --- | --- |
| 0 | room_index | Touch room index |
| 1 | temp_c | °C × 100 |
| 2 | target_c | °C × 100 |
| 3 | valve_pct | % |
| 4 | floor_lag_h | h × 100 |

## Building a history

```
python3 tools/planner/decode_odin_hourly.py hourly_export.bin odin.csv
python3 tools/planner/fetch_weather.py --lat … --lon … --days 60 weather.csv
HA_URL=… HA_TOKEN=… python3 tools/planner/export_ha.py --map ha_entities.json \
    --from 2026-10-02 --to 2026-11-16 --asgard asgard.csv --rooms rooms.csv
make planner-history ASGARD=asgard.csv ODIN=odin.csv ROOMS=rooms.csv WEATHER=weather.csv
make planner-replay            # or: make planner-docker && docker run --rm -v $PWD/planner-history:/data lune-planner
```

`planner-history/` is git-ignored: it holds the house's own data. The HA token is read
from the environment only.

## What the planner may see

`build_context()` gives the planner every measured hour up to now, and for the next 48
hours only outdoor temperature, wind, sun, humidity, price and the comfort schedule
(`forecastable()`). In the replay the future weather is the observed weather, so replay
results are an upper bound on what forecast errors allow; the replay reports any
measured value that leaks into the future.

## Known gaps (F0, 2 Oct–16 Nov 2026)

- Room targets are not in Home Assistant history, so `target_c` and `coldest_dev_k` are
  empty. F1 takes them from Touch.
- The hot-water tank temperature was not exported for the first build (`dhw_tank_c`).
- Odin's export has 1,042 of 1,091 hours; the rest have no metered electricity or price. The repeated hour when summer time ends (25 Oct 2026, 02:00 local) appears once.
