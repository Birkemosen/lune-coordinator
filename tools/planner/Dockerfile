# Planner replay in a container. Build from the repo root:
#   docker build -f tools/planner/Dockerfile -t lune-planner .
# Replay a history directory (house.bin, optional rooms.bin):
#   docker run --rm -v "$PWD/planner-history:/data" lune-planner
# Build history from CSVs first (same tools as on the host):
#   docker run --rm -v "$PWD/export:/data" lune-planner build --asgard /data/asgard.csv \
#       --odin /data/odin.csv --rooms /data/rooms.csv --weather /data/weather.csv --out-dir /data
# Never used for control: the container only reads recorded history.
FROM debian:bookworm-slim

RUN apt-get update \
 && apt-get install -y --no-install-recommends g++ python3 \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /lune
COPY components/planner components/planner
COPY tests/planner tests/planner
COPY tools/planner tools/planner

RUN g++ -std=c++17 -O2 -Wall -Wextra -I components/planner tests/planner/test_history_record.cpp -o /usr/local/bin/test-record \
 && g++ -std=c++17 -O2 -Wall -Wextra -I components/planner tests/planner/test_planner_context.cpp -o /usr/local/bin/test-context \
 && g++ -std=c++17 -O2 -Wall -Wextra -I components/planner tests/planner/replay.cpp -o /usr/local/bin/planner-replay \
 && test-record && test-context

COPY tools/planner/docker-entrypoint.sh /usr/local/bin/docker-entrypoint.sh
ENTRYPOINT ["/usr/local/bin/docker-entrypoint.sh"]
