#!/usr/bin/env python3
"""Export Home Assistant history into the per-minute CSVs build_history.py reads.

  HA_URL=http://ha:8123 HA_TOKEN=… export_ha.py --map ha_entities.json \
      --from 2026-10-02 --to 2026-11-16 --asgard asgard.csv --rooms rooms.csv

The map (see ha_entities.example.json) names the entity behind each Asgard column and
each room. The token is read from the environment and never written anywhere.
"""

import argparse
import bisect
import csv
import json
import os
import urllib.parse
import urllib.request
from datetime import datetime, timedelta, timezone


def fetch(url, token, entities, start, end):
    q = urllib.parse.urlencode({
        "end_time": end.isoformat(), "filter_entity_id": ",".join(entities),
        "minimal_response": "", "no_attributes": "",
    })
    req = urllib.request.Request(f"{url}/api/history/period/{start.isoformat()}?{q}",
                                 headers={"Authorization": f"Bearer {token}"})
    with urllib.request.urlopen(req, timeout=300) as r:
        data = json.load(r)
    series = {}
    for s in data:
        if not s:
            continue
        ts = [datetime.fromisoformat(x["last_changed"]).timestamp() for x in s]
        vs = [x["state"] for x in s]
        series[s[0]["entity_id"]] = (ts, vs)
    return series


def at(series, entity, t):
    if entity not in series:
        return ""
    ts, vs = series[entity]
    i = bisect.bisect_right(ts, t) - 1
    v = vs[i] if i >= 0 else ""
    return "" if v in ("unknown", "unavailable") else v


def day(s):
    return datetime.fromisoformat(s).replace(tzinfo=timezone.utc)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--map", required=True)
    ap.add_argument("--from", dest="start", required=True)
    ap.add_argument("--to", dest="end", required=True)
    ap.add_argument("--asgard")
    ap.add_argument("--rooms")
    a = ap.parse_args()
    url, token = os.environ["HA_URL"].rstrip("/"), os.environ["HA_TOKEN"]
    m = json.load(open(a.map))
    start, end = day(a.start), day(a.end)

    if a.asgard:
        cols = m["asgard"]
        series = {}
        t = start
        while t < end:   # a week per request keeps responses small
            series_part = fetch(url, token, list(cols.values()), t, min(end, t + timedelta(days=7)))
            for e, (ts, vs) in series_part.items():
                old = series.get(e, ([], []))
                series[e] = (old[0] + ts, old[1] + vs)
            t += timedelta(days=7)
        with open(a.asgard, "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["time_utc"] + list(cols))
            for ts in range(int(start.timestamp()), int(end.timestamp()), 60):
                w.writerow([datetime.fromtimestamp(ts, timezone.utc).strftime("%Y-%m-%dT%H:%MZ")] +
                           [at(series, e, ts) for e in cols.values()])

    if a.rooms:
        rooms = m["rooms"]
        ents = [e for r in rooms for e in (r.get("temp"), r.get("target"), r.get("valve")) if e]
        series = fetch(url, token, ents, start, end)
        with open(a.rooms, "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["time_utc", "room_index", "temp_c", "target_c", "valve_pct"])
            for ts in range(int(start.timestamp()), int(end.timestamp()), 300):
                stamp = datetime.fromtimestamp(ts, timezone.utc).strftime("%Y-%m-%dT%H:%MZ")
                for r in rooms:
                    w.writerow([stamp, r["index"], at(series, r.get("temp"), ts),
                                at(series, r.get("target"), ts), at(series, r.get("valve"), ts)])


if __name__ == "__main__":
    main()
