#!/bin/sh
# `build …` runs build_history.py; anything else replays /data (or the given directory).
set -e
if [ "$1" = "build" ]; then
  shift
  exec python3 /lune/tools/planner/build_history.py "$@"
fi
dir="${1:-/data}"
if [ -f "$dir/rooms.bin" ]; then
  exec planner-replay "$dir/house.bin" "$dir/rooms.bin"
fi
exec planner-replay "$dir/house.bin"
