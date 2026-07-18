#!/usr/bin/env bash
# C++-only TCP→mesh failover soak (Linux CI twin of soak-failover-cpp.ps1).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/core-engine/build"
SECONDS_N="${1:-5}"
RATE="${2:-400}"
ZCM="$ROOT/failover-cpp.zcm"
CAP_ERR="$ROOT/failover-cpp-cap-err.txt"
HOP_ERR="$ROOT/failover-cpp-hop-err.txt"
EDGE_ERR="$ROOT/failover-cpp-edge-err.txt"

EDGE="$BIN/zcmesh_edge"
HOP="$BIN/zcmesh_hop"
CAP="$BIN/zcmesh_capture"
if [[ ! -x "$EDGE" ]]; then
  echo "missing $EDGE — build core-engine first"
  exit 1
fi

rm -f "$ZCM" "$CAP_ERR" "$HOP_ERR" "$EDGE_ERR"

"$CAP" --listen 127.0.0.1:9920 --out "$ZCM" --seconds "$SECONDS_N" --mode udp \
  2>"$CAP_ERR" &
CAP_PID=$!
sleep 1
"$HOP" --listen 127.0.0.1:9919 --forward 127.0.0.1:9920 --final 2>"$HOP_ERR" &
HOP_PID=$!
"$EDGE" --operator 127.0.0.1:9920 --transport auto --hop 127.0.0.1:9919 \
  --rate "$RATE" --batch 8 --duration $((SECONDS_N - 1)) --print-stats-sec 1 \
  2>"$EDGE_ERR" &
EDGE_PID=$!

wait "$CAP_PID" || true
kill "$EDGE_PID" "$HOP_PID" 2>/dev/null || true
wait "$EDGE_PID" 2>/dev/null || true
wait "$HOP_PID" 2>/dev/null || true

tail -n 5 "$EDGE_ERR" || true
tail -n 2 "$CAP_ERR" || true

if grep -qE 'mesh_failover=[1-9]' "$EDGE_ERR" \
  && [[ -s "$ZCM" ]] \
  && grep -qE 'captured ok=[1-9]' "$CAP_ERR"; then
  echo FAILOVER_CPP_OK
  exit 0
fi
echo FAILOVER_CPP_FAIL
exit 1
