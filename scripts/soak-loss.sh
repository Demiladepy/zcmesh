#!/usr/bin/env bash
# Loss path: hop --loss-pct → capture .zcm → inspect --expect-gaps-min (Linux CI twin).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/core-engine/build"
LOSS="${1:-10}"
SECONDS_N="${2:-6}"
RATE="${3:-400}"
EXPECT_MIN="${4:-5}"
ZCM="$ROOT/loss-soak.zcm"
CAP_ERR="$ROOT/loss-cap-err.txt"
HOP_ERR="$ROOT/loss-hop-err.txt"
EDGE_ERR="$ROOT/loss-edge-err.txt"

EDGE="$BIN/zcmesh_edge"
HOP="$BIN/zcmesh_hop"
CAP="$BIN/zcmesh_capture"
INSPECT="$BIN/zcmesh_inspect"
if [[ ! -x "$EDGE" ]]; then
  echo "missing binaries — build core-engine first"
  exit 1
fi

rm -f "$ZCM" "$CAP_ERR" "$HOP_ERR" "$EDGE_ERR"

"$CAP" --listen 127.0.0.1:9911 --out "$ZCM" --seconds "$SECONDS_N" --mode udp 2>"$CAP_ERR" &
CAP_PID=$!
sleep 1
"$HOP" --listen 127.0.0.1:9910 --forward 127.0.0.1:9911 \
  --loss-pct "$LOSS" --final 2>"$HOP_ERR" &
HOP_PID=$!
"$EDGE" --operator 127.0.0.1:9911 --transport mesh \
  --hop 127.0.0.1:9910 --rate "$RATE" --batch 8 --duration $((SECONDS_N - 1)) \
  2>"$EDGE_ERR" &
EDGE_PID=$!

wait "$CAP_PID" || true
kill "$EDGE_PID" "$HOP_PID" 2>/dev/null || true
wait "$EDGE_PID" 2>/dev/null || true
wait "$HOP_PID" 2>/dev/null || true

if [[ ! -s "$ZCM" ]]; then
  echo "LOSS_SOAK_FAIL: no capture"
  cat "$CAP_ERR" || true
  exit 1
fi

"$INSPECT" "$ZCM" --expect-gaps-min "$EXPECT_MIN"
echo LOSS_SOAK_OK
