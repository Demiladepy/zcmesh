#!/usr/bin/env bash
# Edge --file → capture → inspect --expect-raws.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/core-engine/build"
RATE="${1:-200}"
CAP_SEC="${2:-4}"
SAMPLES="$ROOT/fixtures/samples.txt"
ZCM="$ROOT/file-source.zcm"
CAP_ERR="$ROOT/file-cap-err.txt"
EDGE_ERR="$ROOT/file-edge-err.txt"

EDGE="$BIN/zcmesh_edge"
CAP="$BIN/zcmesh_capture"
INSPECT="$BIN/zcmesh_inspect"
if [[ ! -x "$EDGE" ]]; then
  echo "missing binaries — build core-engine first"
  exit 1
fi

RAWS=$(grep -E '^-?[0-9]+$' "$SAMPLES" | paste -sd, -)
COUNT=$(grep -cE '^-?[0-9]+$' "$SAMPLES" || true)
rm -f "$ZCM" "$CAP_ERR" "$EDGE_ERR"

"$CAP" --listen 127.0.0.1:9960 --out "$ZCM" --seconds "$CAP_SEC" --mode udp 2>"$CAP_ERR" &
CAP_PID=$!
sleep 1
"$EDGE" --operator 127.0.0.1:9960 --transport udp \
  --file "$SAMPLES" --rate "$RATE" --batch 1 --duration $((CAP_SEC - 1)) \
  2>"$EDGE_ERR" &
EDGE_PID=$!

wait "$CAP_PID" || true
kill "$EDGE_PID" 2>/dev/null || true
wait "$EDGE_PID" 2>/dev/null || true

if [[ ! -s "$ZCM" ]]; then
  echo "FILE_SOURCE_FAIL: no capture"
  cat "$CAP_ERR" "$EDGE_ERR" || true
  exit 1
fi

"$INSPECT" "$ZCM" --expect-gaps-max 0 --expect-frames-min "$COUNT" --expect-raws "$RAWS"
echo FILE_SOURCE_OK
