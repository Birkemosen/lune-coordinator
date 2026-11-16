#!/usr/bin/env python3
"""Seed Touch's shadow-planner history with recorded hours (POST /planner/history).

  seed_touch.py http://touch-ip planner-history/house.bin [--days 60]

Sends the last `days` of house.bin, 30 records a request in the default column order.
Touch takes only hours it does not have yet, so a seed never overwrites its own hours,
and refits on the next poll.
"""

import argparse
import json
import struct
import time
import urllib.request

import lune_history as lh

BATCH = 30   # 30 × 128 hex characters fits Touch's 4 KiB POST body


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("touch_url")
    ap.add_argument("house_bin")
    ap.add_argument("--days", type=int, default=60)
    a = ap.parse_args()
    kind, rows = lh.read(a.house_bin)
    if kind != lh.KIND_HOUSE:
        raise SystemExit("not a house history")
    newest = max(h for h, _ in rows)
    rows = [(h, v) for h, v in rows if h > newest - a.days * 86400]
    url = a.touch_url.rstrip("/") + "/api/lune-touch/v1/planner/history"
    taken = 0
    for i in range(0, len(rows), BATCH):
        hexs = ""
        for hour_start, values in rows[i:i + BATCH]:
            raw = [lh.encode(values.get(name), scale) for name, scale in lh.HOUSE_COLS]
            hexs += struct.pack("<I30h", hour_start, *raw).hex()
        req = urllib.request.Request(url, data=json.dumps({"hex": hexs}).encode(), method="POST",
                                     headers={"Content-Type": "application/json"})
        with urllib.request.urlopen(req, timeout=20) as r:
            taken += json.load(r)["data"].get("seeded", 0)
        time.sleep(0.2)
    print(f"{len(rows)} hours sent, {taken} taken")


if __name__ == "__main__":
    main()
