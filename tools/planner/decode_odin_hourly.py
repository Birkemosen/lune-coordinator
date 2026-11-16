#!/usr/bin/env python3
"""Decode Odin's GET /api/migrate/hourly/export into a CSV build_history.py reads.

Odin's file: 64-byte header, then 64-byte records (u32 epoch + 30 x int16, -32768 =
none). The epoch is the hour's START in Odin's local wall-clock time, written as if it
were UTC (matched against Asgard's measured output: -2 h in summer time, -1 h in winter
time for Denmark). The CSV gives the real UTC hour start. Only the columns whose meaning
is confirmed against Asgard's measurements are decoded.
"""

import argparse
import csv
import struct
from datetime import datetime, timezone
from zoneinfo import ZoneInfo

NONE = -32768
# (csv name, Odin column, scale)
COLUMNS = [
    ("heat_kwh", 2, 0.01), ("elec_kwh", 1, 0.01), ("room_actual_c", 9, 0.01),
    ("room_expected_c", 10, 0.01), ("outdoor_c", 15, 0.01), ("price_eur_kwh", 14, 0.0001),
    ("sched_setpoint_c", 18, 0.01), ("sched_min_c", 19, 0.01), ("sched_max_c", 20, 0.01),
]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("export", help="file saved from /api/migrate/hourly/export")
    ap.add_argument("out_csv")
    ap.add_argument("--tz", default="Europe/Copenhagen", help="Odin's configured time zone")
    a = ap.parse_args()
    tz = ZoneInfo(a.tz)
    data = open(a.export, "rb").read()
    rows = []
    for off in range(64, len(data) - 63, 64):
        ts, *f = struct.unpack_from("<I30h", data, off)
        if ts:
            # Local wall clock written as UTC -> real UTC. In the repeated hour at the end
            # of summer time the first occurrence is taken (fold=0).
            local = datetime.fromtimestamp(ts, timezone.utc).replace(tzinfo=tz)
            rows.append((int(local.timestamp()), f))
    rows.sort()
    with open(a.out_csv, "w", newline="") as out:
        w = csv.writer(out)
        w.writerow(["hour_start_utc"] + [c[0] for c in COLUMNS])
        for ts, f in rows:
            w.writerow([datetime.fromtimestamp(ts, timezone.utc).strftime("%Y-%m-%dT%H:00Z")] +
                       ["" if f[col] == NONE else round(f[col] * s, 4) for _, col, s in COLUMNS])
    print(f"{len(rows)} hours -> {a.out_csv}")


if __name__ == "__main__":
    main()
