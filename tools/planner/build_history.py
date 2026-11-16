#!/usr/bin/env python3
"""Build planner history (house.bin, rooms.bin, house.csv) from exported data.

Inputs (all UTC, see docs/planner_history_format.md):
  --asgard   per-minute Asgard CSV (export_ha.py or the Odin issue data pack)
  --odin     Odin hourly CSV (decode_odin_hourly.py): metered heat/elec, price, schedule
  --rooms    optional per-minute room CSV: time_utc,room_index,temp_c,target_c,valve_pct
  --weather  optional hourly weather CSV (fetch_weather.py)
  --room-meta optional rooms.json (export_touch_rooms.py): fills a room's target from its
             setpoint when the room CSV has none (rooms without an enabled schedule)

Heat is split into space heating and hot water by Asgard's operation mode and scaled
to Odin's metered heat for the hour when Odin has it.
"""

import argparse
import csv
import json
import os
import sys
from collections import defaultdict
from datetime import datetime, timezone

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lune_history as lh  # noqa: E402

# Asgard CSV header -> short name. Accepts the data-pack headers and export_ha.py's.
ASGARD_ALIASES = {
    "compressor_hz": "hz", "operation_mode": "mode", "output_kw": "kw",
    "hp_feed_c (THW1)": "hp_feed", "hp_feed_c": "hp_feed",
    "hp_return_c (THW2)": "hp_ret", "hp_return_c": "hp_ret", "hp_flow_lpm": "flow",
    "z1_flow_setpoint_c (Odin cmd)": "target", "z1_flow_setpoint_c": "target",
    "z1_feed_c (THW6)": "z1_feed", "z1_feed_c": "z1_feed",
    "z1_return_c (THW7)": "z1_ret", "z1_return_c": "z1_ret",
    "ds18_zone_feed_c": "ds_feed", "ds18_zone_return_c": "ds_ret",
    "outdoor_c": "out", "defrost": "defrost", "room_c (virtual thermostat)": "room", "room_c": "room",
    "dhw_tank_c": "dhw_tank",
}


def parse_time(s):
    t = datetime.fromisoformat(s.strip().replace("Z", "+00:00"))
    if t.tzinfo is None:
        t = t.replace(tzinfo=timezone.utc)
    return int(t.timestamp())


def num(s):
    try:
        v = float(s)
    except (TypeError, ValueError):
        return None
    return v


def mean(xs):
    xs = [x for x in xs if x is not None]
    return sum(xs) / len(xs) if xs else None


def read_asgard(path):
    hours = defaultdict(list)
    with open(path, newline="") as f:
        r = csv.DictReader(f)
        for row in r:
            m = {ASGARD_ALIASES[k]: v for k, v in row.items() if k in ASGARD_ALIASES}
            t = parse_time(row["time_utc"])
            hours[t - t % 3600].append(m)
    return hours


def aggregate_asgard(minutes, prev_running):
    heat_sh = heat_dhw = 0.0
    run_min = starts = defrost_min = 0
    feed, ret, zf, zr, targets, kws = [], [], [], [], [], []
    flags = 0
    running = prev_running
    for m in minutes:
        hz = num(m.get("hz")) or 0.0
        kw = num(m.get("kw")) or 0.0
        mode = m.get("mode", "")
        on = hz > 0
        if on and not running:
            starts += 1
        running = on
        if mode == "Heating Water":
            heat_dhw += kw / 60
            flags |= lh.MODE_HOT_WATER
        elif mode == "Legionella":
            heat_dhw += kw / 60
            flags |= lh.MODE_LEGIONELLA
        else:
            heat_sh += kw / 60
        if m.get("defrost") == "on":
            defrost_min += 1
            flags |= lh.MODE_DEFROST
        if on and mode == "Space Heating":
            run_min += 1
            flags |= lh.MODE_SPACE_HEATING
            feed.append(num(m.get("hp_feed")))
            ret.append(num(m.get("hp_ret")))
            # A probe on the zone pipes beats the controller's 0.5 °C thermistor.
            zf.append(num(m.get("ds_feed")) if num(m.get("ds_feed")) is not None else num(m.get("z1_feed")))
            zr.append(num(m.get("ds_ret")) if num(m.get("ds_ret")) is not None else num(m.get("z1_ret")))
        if num(m.get("target")) is not None:
            targets.append(num(m.get("target")))
        kws.append(kw)
    last_tank = None
    for m in reversed(minutes):
        if num(m.get("dhw_tank")) is not None:
            last_tank = num(m.get("dhw_tank"))
            break
    return {
        "_heat_sh": heat_sh, "_heat_dhw": heat_dhw, "run_min": run_min, "starts": starts,
        "defrost_min": defrost_min, "mode_flags": flags,
        "hp_feed_c": mean(feed), "hp_return_c": mean(ret), "z1_feed_c": mean(zf), "z1_return_c": mean(zr),
        "flow_target_c": mean(targets), "flow_target_max_c": max(targets) if targets else None,
        "output_max_kw": max(kws) if kws else None, "dhw_tank_c": last_tank,
        "_room": mean([num(m.get("room")) for m in minutes]), "_out": mean([num(m.get("out")) for m in minutes]),
    }, running


def read_hourly_csv(path, time_col):
    out = {}
    with open(path, newline="") as f:
        for row in csv.DictReader(f):
            t = parse_time(row[time_col])
            out[t - t % 3600] = {k: num(v) for k, v in row.items() if k != time_col}
    return out


def read_rooms(path):
    acc = defaultdict(lambda: defaultdict(list))   # (hour, room) -> field -> values
    with open(path, newline="") as f:
        for row in csv.DictReader(f):
            t = parse_time(row["time_utc"])
            key = (t - t % 3600, int(float(row["room_index"])))
            for k in ("temp_c", "target_c", "valve_pct"):
                acc[key][k].append(num(row.get(k)))
    rooms = defaultdict(dict)
    for (hour, idx), fields in acc.items():
        rooms[hour][idx] = {k: mean(v) for k, v in fields.items()}
    return rooms


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--asgard", required=True)
    ap.add_argument("--odin")
    ap.add_argument("--rooms")
    ap.add_argument("--weather")
    ap.add_argument("--room-meta")
    ap.add_argument("--out-dir", required=True)
    a = ap.parse_args()

    asg = read_asgard(a.asgard)
    odin = read_hourly_csv(a.odin, "hour_start_utc") if a.odin else {}
    weather = read_hourly_csv(a.weather, "time_utc") if a.weather else {}
    rooms = read_rooms(a.rooms) if a.rooms else {}
    meta = {}
    if a.room_meta:
        meta = {r["index"]: r for r in json.load(open(a.room_meta))["rooms"]}
        for per_hour in rooms.values():
            for idx, r in per_hour.items():
                m = meta.get(idx)
                if r.get("target_c") is None and m and not m.get("schedule") and m.get("setpoint_c") is not None:
                    r["target_c"] = m["setpoint_c"]

    hours = sorted(set(asg) | set(odin))
    house_rows, room_rows = [], []
    running = False
    for h in hours:
        v, running = aggregate_asgard(asg.get(h, []), running)
        o = odin.get(h, {})
        w = weather.get(h, {})
        sh, dhw = v.pop("_heat_sh"), v.pop("_heat_dhw")
        metered = o.get("heat_kwh")
        if metered is not None and sh + dhw > 0.05:
            f = metered / (sh + dhw)
            sh, dhw = sh * f, dhw * f
        elif metered is not None:
            sh, dhw = metered, 0.0
        v["heat_sh_kwh"], v["heat_dhw_kwh"] = sh, dhw
        v["elec_kwh"] = o.get("elec_kwh")
        room, out = v.pop("_room"), v.pop("_out")
        v["room_c"] = o.get("room_actual_c") if o.get("room_actual_c") is not None else room
        v["room_expected_c"] = o.get("room_expected_c")
        v["outdoor_c"] = w.get("temp_c") if w.get("temp_c") is not None else (
            o.get("outdoor_c") if o.get("outdoor_c") is not None else out)
        v["wind_ms"], v["wind_dir_deg"] = w.get("wind_ms"), w.get("wind_dir_deg")
        v["solar_wm2"], v["humidity_pct"] = w.get("solar_wm2"), w.get("humidity_pct")
        v["price_eur_kwh"] = o.get("price_eur_kwh")
        v["sched_sp_c"], v["sched_min_c"], v["sched_max_c"] = o.get("sched_setpoint_c"), o.get("sched_min_c"), o.get("sched_max_c")
        devs = [r["temp_c"] - r["target_c"] for r in rooms.get(h, {}).values()
                if r.get("temp_c") is not None and r.get("target_c") is not None]
        v["coldest_dev_k"] = min(devs) if devs else None
        house_rows.append((h, v))
        # Split the hour's space heating over the rooms by what their loops can take:
        # valve opening × loop area × (zone feed − room). Needs the room metadata.
        hr = rooms.get(h, {})
        feed = v.get("z1_feed_c") if v.get("z1_feed_c") is not None else v.get("hp_feed_c")
        weights = {}
        if a.room_meta and feed is not None:
            for idx, r in hr.items():
                m = meta.get(idx) or {}
                area = m.get("loop_area_m2") or m.get("area_m2") or 0
                if r.get("valve_pct") is not None and r.get("temp_c") is not None:
                    weights[idx] = max(0.0, r["valve_pct"]) / 100 * area * max(0.0, feed - r["temp_c"])
        wsum = sum(weights.values())
        for idx, r in sorted(hr.items()):
            heat = sh * weights[idx] / wsum if wsum > 0 and idx in weights else (0.0 if sh < 0.05 else None)
            room_rows.append((h, {"room_index": idx, "temp_c": r.get("temp_c"), "target_c": r.get("target_c"),
                                  "valve_pct": r.get("valve_pct"), "heat_kwh": heat}))

    os.makedirs(a.out_dir, exist_ok=True)
    lh.write(os.path.join(a.out_dir, "house.bin"), lh.KIND_HOUSE, house_rows)
    if room_rows:
        lh.write(os.path.join(a.out_dir, "rooms.bin"), lh.KIND_ROOM, room_rows)
    names = [n for n, _ in lh.HOUSE_COLS]
    with open(os.path.join(a.out_dir, "house.csv"), "w", newline="") as f:
        wr = csv.writer(f)
        wr.writerow(["hour_start_utc"] + names)
        for h, v in house_rows:
            wr.writerow([datetime.fromtimestamp(h, timezone.utc).strftime("%Y-%m-%dT%H:00Z")] +
                        ["" if v.get(n) is None else round(v[n], 4) for n in names])
    print(f"{len(house_rows)} house hours, {len(room_rows)} room hours -> {a.out_dir}")


if __name__ == "__main__":
    main()
