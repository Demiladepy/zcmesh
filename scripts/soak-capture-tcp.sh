#!/usr/bin/env bash
# TCP capture soak: edge tcp → capture --mode tcp → inspect.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/core-engine/build"
SECONDS_N="${1:-5}"
RATE="${2:-400}"
ZCM="$ROOT/tcp-cap-soak.zcm"
CAP_ERR="$ROOT/tcp-cap-err.txt"
EDGE_ERR="$ROOT/tcp-cap-edge-err.txt"

EDGE="$BIN/zcmesh_edge"
CAP="$BIN/zcmesh_capture"
INSPECT="$BIN/zcmesh_inspect"
if [[ ! -x "$EDGE" ]]; then
  echo "missing binaries — build core-engine first"
  exit 1
fi

rm -f "$ZCM" "$CAP_ERR" "$EDGE_ERR"

"$CAP" --listen 127.0.0.1:9950 --out "$ZCM" --seconds "$SECONDS_N" --mode tcp 2>"$CAP_ERR" &
CAP_PID=$!
sleep 1
"$EDGE" --operator 127.0.0.1:9950 --transport tcp \
  --rate "$RATE" --batch 16 --duration $((SECONDS_N - 1)) 2>"$EDGE_ERR" &
EDGE_PID=$!

wait "$CAP_PID" || true
kill "$EDGE_PID" 2>/dev/null || true
wait "$EDGE_PID" 2>/dev/null || true

if [[ ! -s "$ZCM" ]]; then
  echo "TCP_CAP_FAIL: no capture"
  cat "$CAP_ERR" "$EDGE_ERR" || true
  exit 1
fi

"$INSPECT" "$ZCM" --expect-gaps-max 0 --expect-last-hop-min-pct 99
echo TCP_CAP_OK
