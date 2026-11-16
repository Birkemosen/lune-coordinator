#!/usr/bin/env python3
"""Fetch hourly weather for a past period from Open-Meteo into the CSV build_history.py reads.

Uses the forecast API with past_days (up to 92 days back), the same model family
Touch and Odin use live. Location comes from the command line; nothing is stored.
"""

import argparse
import csv
import json
import urllib.request
from datetime import datetime, timezone

URL = ("https://api.open-meteo.com/v1/forecast?latitude={lat}&longitude={lon}"
       "&hourly=temperature_2m,windspeed_10m,winddirection_10m,shortwave_radiation,relative_humidity_2m"
       "&windspeed_unit=ms&timezone=UTC&past_days={days}&forecast_days=1")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--lat", type=float, required=True)
    ap.add_argument("--lon", type=float, required=True)
    ap.add_argument("--days", type=int, default=92)
    ap.add_argument("out_csv")
    a = ap.parse_args()
    with urllib.request.urlopen(URL.format(lat=a.lat, lon=a.lon, days=min(92, a.days)), timeout=30) as r:
        h = json.load(r)["hourly"]
    now = datetime.now(timezone.utc)
    n = 0
    with open(a.out_csv, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["time_utc", "temp_c", "wind_ms", "wind_dir_deg", "solar_wm2", "humidity_pct"])
        for i, t in enumerate(h["time"]):
            ts = datetime.fromisoformat(t).replace(tzinfo=timezone.utc)
            if ts > now:
                break
            w.writerow([ts.strftime("%Y-%m-%dT%H:00Z"), h["temperature_2m"][i], h["windspeed_10m"][i],
                        h["winddirection_10m"][i], h["shortwave_radiation"][i], h["relative_humidity_2m"][i]])
            n += 1
    print(f"{n} hours -> {a.out_csv}")


if __name__ == "__main__":
    main()
