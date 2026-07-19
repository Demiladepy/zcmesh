#!/usr/bin/env bash
# Capture --mode both soak.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/core-engine/build"
SECONDS_N="${1:-5}"
RATE="${2:-400}"
ZCM="$ROOT/both-cap.zcm"
CAP_ERR="$ROOT/both-cap-err.txt"
EDGE_ERR="$ROOT/both-edge-err.txt"

EDGE="$BIN/zcmesh_edge"
CAP="$BIN/zcmesh_capture"
INSPECT="$BIN/zcmesh_inspect"
[[ -x "$EDGE" ]] || { echo "missing binaries"; exit 1; }

rm -f "$ZCM" "$CAP_ERR" "$EDGE_ERR"
"$CAP" --listen 127.0.0.1:9970 --out "$ZCM" --seconds "$SECONDS_N" --mode both 2>"$CAP_ERR" &
CAP_PID=$!
sleep 1
"$EDGE" --operator 127.0.0.1:9970 --transport udp \
  --rate "$RATE" --batch 8 --duration $((SECONDS_N - 1)) 2>"$EDGE_ERR" &
EDGE_PID=$!

wait "$CAP_PID" || true
kill "$EDGE_PID" 2>/dev/null || true
wait "$EDGE_PID" 2>/dev/null || true

[[ -s "$ZCM" ]] || { echo BOTH_CAP_FAIL; cat "$CAP_ERR" || true; exit 1; }
"$INSPECT" "$ZCM" --expect-gaps-max 0 --expect-frames-min 50 --expect-last-hop-min-pct 99
echo BOTH_CAP_OK
