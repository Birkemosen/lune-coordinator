#!/usr/bin/env python3
"""Fetch hourly weather for a past period from Open-Meteo into the CSV build_history.py reads.

Uses the forecast API with past_days (up to 92 days back), the same model family
Touch and Odin use live. Location comes from the command line; nothing is stored.

Besides global irradiance it writes the sun on a vertical facade facing N, E, S and W
(poa_*_wm2), from direct-normal and diffuse irradiance and the sun's position: a room
with a large west window gets its afternoon sun from poa_w, not from the global value.
Open-Meteo's hourly radiation is the mean of the preceding hour, so the sun's position
is taken at the middle of that hour.
"""

import argparse
import csv
import json
import math
import urllib.request
from datetime import datetime, timedelta, timezone

URL = ("https://api.open-meteo.com/v1/forecast?latitude={lat}&longitude={lon}"
       "&hourly=temperature_2m,windspeed_10m,winddirection_10m,shortwave_radiation,relative_humidity_2m,"
       "direct_normal_irradiance,diffuse_radiation"
       "&windspeed_unit=ms&timezone=UTC&past_days={days}&forecast_days=1")


def sun_position(t, lat, lon):
    """Solar altitude and azimuth (degrees, azimuth from north, clockwise) at UTC time t."""
    doy = t.timetuple().tm_yday
    hour = t.hour + t.minute / 60
    g = 2 * math.pi / 365 * (doy - 1 + (hour - 12) / 24)
    decl = (0.006918 - 0.399912 * math.cos(g) + 0.070257 * math.sin(g) - 0.006758 * math.cos(2 * g)
            + 0.000907 * math.sin(2 * g) - 0.002697 * math.cos(3 * g) + 0.00148 * math.sin(3 * g))
    eqt = 229.18 * (0.000075 + 0.001868 * math.cos(g) - 0.032077 * math.sin(g)
                    - 0.014615 * math.cos(2 * g) - 0.040849 * math.sin(2 * g))
    tst = hour * 60 + eqt + 4 * lon
    ha = math.radians(tst / 4 - 180)
    la = math.radians(lat)
    cz = math.sin(la) * math.sin(decl) + math.cos(la) * math.cos(decl) * math.cos(ha)
    zen = math.acos(max(-1.0, min(1.0, cz)))
    az = math.degrees(math.atan2(math.sin(ha), math.cos(ha) * math.sin(la) - math.tan(decl) * math.cos(la))) + 180
    return 90 - math.degrees(zen), az % 360


def facade_sun(dni, dhi, ghi, alt, az, facing, albedo=0.2):
    """Irradiance on a vertical surface facing `facing` degrees (isotropic sky)."""
    if dni is None or dhi is None or ghi is None:
        return None
    direct = 0.0
    if alt > 0:
        direct = dni * max(0.0, math.cos(math.radians(alt)) * math.cos(math.radians(az - facing)))
    return round(direct + dhi / 2 + albedo * ghi / 2, 1)


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
        w.writerow(["time_utc", "temp_c", "wind_ms", "wind_dir_deg", "solar_wm2", "humidity_pct",
                    "poa_n_wm2", "poa_e_wm2", "poa_s_wm2", "poa_w_wm2"])
        for i, t in enumerate(h["time"]):
            ts = datetime.fromisoformat(t).replace(tzinfo=timezone.utc)
            if ts > now:
                break
            # Open-Meteo labels the hour by its end; the CSV gives the hour start.
            start = ts - timedelta(hours=1)
            alt, az = sun_position(start + timedelta(minutes=30), a.lat, a.lon)
            dni, dhi, ghi = h["direct_normal_irradiance"][i], h["diffuse_radiation"][i], h["shortwave_radiation"][i]
            w.writerow([start.strftime("%Y-%m-%dT%H:00Z"), h["temperature_2m"][i], h["windspeed_10m"][i],
                        h["winddirection_10m"][i], ghi, h["relative_humidity_2m"][i]] +
                       [facade_sun(dni, dhi, ghi, alt, az, f) for f in (0, 90, 180, 270)])
            n += 1
    print(f"{n} hours -> {a.out_csv}")


if __name__ == "__main__":
    main()
