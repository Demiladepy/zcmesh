#!/usr/bin/env bash
# Serial 2-hop: edge → hop0 → hop1(--final) → capture → inspect.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/core-engine/build"
SECONDS_N="${1:-6}"
RATE="${2:-400}"
ZCM="$ROOT/serial-hop.zcm"
CAP_ERR="$ROOT/serial-cap-err.txt"
HOP0_ERR="$ROOT/serial-hop0-err.txt"
HOP1_ERR="$ROOT/serial-hop1-err.txt"
EDGE_ERR="$ROOT/serial-edge-err.txt"

EDGE="$BIN/zcmesh_edge"
HOP="$BIN/zcmesh_hop"
CAP="$BIN/zcmesh_capture"
INSPECT="$BIN/zcmesh_inspect"
if [[ ! -x "$EDGE" ]]; then
  echo "missing binaries — build core-engine first"
  exit 1
fi

rm -f "$ZCM" "$CAP_ERR" "$HOP0_ERR" "$HOP1_ERR" "$EDGE_ERR"

"$CAP" --listen 127.0.0.1:9940 --out "$ZCM" --seconds "$SECONDS_N" --mode udp 2>"$CAP_ERR" &
CAP_PID=$!
sleep 1
"$HOP" --listen 127.0.0.1:9939 --forward 127.0.0.1:9940 --final 2>"$HOP1_ERR" &
HOP1_PID=$!
"$HOP" --listen 127.0.0.1:9938 --forward 127.0.0.1:9939 2>"$HOP0_ERR" &
HOP0_PID=$!
EDGE_DUR=$((SECONDS_N - 1))
"$EDGE" --operator 127.0.0.1:9940 --transport mesh \
  --hop 127.0.0.1:9938 --rate "$RATE" --batch 8 --duration "$EDGE_DUR" \
  2>"$EDGE_ERR" &
EDGE_PID=$!

wait "$CAP_PID" || true
kill "$EDGE_PID" "$HOP0_PID" "$HOP1_PID" 2>/dev/null || true
wait "$EDGE_PID" 2>/dev/null || true
wait "$HOP0_PID" 2>/dev/null || true
wait "$HOP1_PID" 2>/dev/null || true

if [[ ! -s "$ZCM" ]]; then
  echo "SERIAL_HOP_FAIL: no capture"
  cat "$CAP_ERR" "$EDGE_ERR" || true
  exit 1
fi

"$INSPECT" "$ZCM" --expect-gaps-max 0 --expect-hop-idx 2 --expect-last-hop-min-pct 99
echo SERIAL_HOP_OK
