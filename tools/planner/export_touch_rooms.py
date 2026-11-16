#!/usr/bin/env python3
"""Export Touch's rooms (GET /api/lune-touch/v1/zones) as the room metadata the planner uses.

  export_touch_rooms.py http://touch-ip rooms.json

One entry per zone in Touch order (index = node_index × 6 + zone_index, the same index
export_ha.py's room map uses): name, setpoint and schedule, floor area, exterior walls,
wind and solar exposure, the UA prior and what Touch has learned so far.
"""

import argparse
import json
import urllib.request


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("touch_url")
    ap.add_argument("out_json")
    a = ap.parse_args()
    with urllib.request.urlopen(a.touch_url.rstrip("/") + "/api/lune-touch/v1/zones", timeout=20) as r:
        data = json.load(r)["data"]
    rooms = []
    for z in data["zones"]:
        room = z.get("room") or {}
        fc = z.get("forecast") or {}
        sched = z.get("schedule") or {}
        learned = room.get("learned") or {}
        rooms.append({
            "index": z["node_index"] * 6 + z["zone_index"],
            "node_index": z["node_index"], "zone_index": z["zone_index"],
            "name": z.get("name"),
            "setpoint_c": z.get("setpoint_c"),
            "schedule": sched if sched.get("enabled") else None,
            "area_m2": room.get("total_area_m2"),
            "exterior_walls": fc.get("exterior_walls"),   # bit 1 N, 2 E, 4 S, 8 W
            "wind_exposure": fc.get("wind_exposure"),
            "solar_gain": fc.get("solar_gain"),
            "ua_prior_w_per_k": room.get("ua_prior_w_per_k"),
            "ua_effective_w_per_k": room.get("ua_effective_w_per_k"),
            "learned_wind": learned.get("wind"),
            "learned_solar": learned.get("solar"),
            "thermal_lead_h": fc.get("active_thermal_lead_h"),
            "include_in_house": room.get("include_in_house_temperature"),
            "weight": room.get("physical_weight"),
        })
    rooms.sort(key=lambda r: r["index"])
    json.dump({"rooms": rooms}, open(a.out_json, "w"), indent=1, ensure_ascii=False)
    print(f"{len(rooms)} rooms -> {a.out_json}")


if __name__ == "__main__":
    main()
