"""Read and write planner history files (components/planner/history_record.h).

One 64-byte header, then 64-byte records: u32 hour start (UTC) + 30 x int16,
-32768 = no value. Column ids and scales mirror history_record.h; keep them in step.
"""

import math
import struct

NONE = -32768
RECORD_SIZE = 64
HEADER_SIZE = 64
MAX_COLS = 30
VERSION = 1
MAGIC = b"LUNH"
KIND_HOUSE = 1
KIND_ROOM = 2

# (name, scale) in column-id order.
HOUSE_COLS = [
    ("heat_sh_kwh", 100), ("heat_dhw_kwh", 100), ("elec_kwh", 100), ("room_c", 100),
    ("outdoor_c", 100), ("wind_ms", 10), ("wind_dir_deg", 1), ("solar_wm2", 1),
    ("price_eur_kwh", 10000), ("sched_sp_c", 100), ("sched_min_c", 100), ("sched_max_c", 100),
    ("run_min", 1), ("starts", 1), ("hp_feed_c", 10), ("hp_return_c", 10),
    ("z1_feed_c", 10), ("z1_return_c", 10), ("flow_target_c", 10), ("flow_target_max_c", 10),
    ("output_max_kw", 100), ("defrost_min", 1), ("dhw_tank_c", 10), ("mode_flags", 1),
    ("room_expected_c", 100), ("coldest_dev_k", 100), ("humidity_pct", 1),
]
ROOM_COLS = [
    ("room_index", 1), ("temp_c", 100), ("target_c", 100), ("valve_pct", 1), ("floor_lag_h", 100),
    ("heat_kwh", 100),
]

MODE_SPACE_HEATING = 1 << 0
MODE_HOT_WATER = 1 << 1
MODE_LEGIONELLA = 1 << 2
MODE_DEFROST = 1 << 3
MODE_FORECAST_PRICE = 1 << 4
MODE_FORECAST_WEATHER = 1 << 5


def cols_for(kind):
    return ROOM_COLS if kind == KIND_ROOM else HOUSE_COLS


def encode(value, scale):
    if value is None or (isinstance(value, float) and not math.isfinite(value)):
        return NONE
    v = round(value * scale)
    if v <= NONE or v > 32767:
        return NONE
    return int(v)


def decode(raw, scale):
    return None if raw == NONE else raw / scale


def write(path, kind, rows):
    """rows: iterable of (hour_start, {name: value}). Every known column is stored."""
    cols = cols_for(kind)
    hdr = bytearray(HEADER_SIZE)
    hdr[0:4] = MAGIC
    struct.pack_into("<HHBB", hdr, 4, VERSION, RECORD_SIZE, kind, len(cols))
    for i in range(len(cols)):
        hdr[12 + i] = i
    with open(path, "wb") as f:
        f.write(hdr)
        for hour_start, values in rows:
            raw = [NONE] * MAX_COLS
            for i, (name, scale) in enumerate(cols):
                raw[i] = encode(values.get(name), scale)
            f.write(struct.pack("<I30h", hour_start, *raw))


def read(path):
    """Returns (kind, [(hour_start, {name: value})]); columns follow the file's header."""
    data = open(path, "rb").read()
    if data[0:4] != MAGIC:
        raise ValueError("not a Lune history file")
    version, size, kind, ncols = struct.unpack_from("<HHBB", data, 4)
    if version != VERSION or size != RECORD_SIZE:
        raise ValueError(f"unsupported version {version} / record size {size}")
    names = cols_for(kind)
    slot_cols = list(data[12:12 + ncols])
    out = []
    for off in range(HEADER_SIZE, len(data) - RECORD_SIZE + 1, RECORD_SIZE):
        hour_start, *raw = struct.unpack_from("<I30h", data, off)
        values = {}
        for slot, col in enumerate(slot_cols):
            if col < len(names):
                name, scale = names[col]
                values[name] = decode(raw[slot], scale)
        out.append((hour_start, values))
    return kind, out
