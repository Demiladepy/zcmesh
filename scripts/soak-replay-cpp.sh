#!/usr/bin/env bash
# C++ capture → replay → recapture roundtrip.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/core-engine/build"
SECONDS_N="${1:-4}"
RATE="${2:-400}"
ZCM1="$ROOT/replay-src.zcm"
ZCM2="$ROOT/replay-dst.zcm"

EDGE="$BIN/zcmesh_edge"
CAP="$BIN/zcmesh_capture"
REPLAY="$BIN/zcmesh_replay"
INSPECT="$BIN/zcmesh_inspect"
[[ -x "$EDGE" ]] || { echo "missing binaries"; exit 1; }

rm -f "$ZCM1" "$ZCM2"
"$CAP" --listen 127.0.0.1:9980 --out "$ZCM1" --seconds "$SECONDS_N" --mode udp 2>"$ROOT/replay-cpp-cap1-err.txt" &
CAP1=$!
sleep 1
"$EDGE" --operator 127.0.0.1:9980 --transport udp \
  --rate "$RATE" --batch 8 --duration $((SECONDS_N - 1)) 2>"$ROOT/replay-cpp-edge-err.txt" &
EDGE_PID=$!
wait "$CAP1" || true
kill "$EDGE_PID" 2>/dev/null || true
wait "$EDGE_PID" 2>/dev/null || true

[[ -s "$ZCM1" ]] || { echo REPLAY_CPP_FAIL: src; exit 1; }
"$INSPECT" "$ZCM1" --expect-gaps-max 0 --expect-frames-min 50

"$CAP" --listen 127.0.0.1:9981 --out "$ZCM2" --seconds $((SECONDS_N + 2)) --mode tcp \
  2>"$ROOT/replay-cpp-cap2-err.txt" &
CAP2=$!
sleep 1
"$REPLAY" --in "$ZCM1" --target 127.0.0.1:9981 --transport tcp --rate 800 \
  2>"$ROOT/replay-cpp-replay-err.txt" || true
wait "$CAP2" || true
kill "$CAP2" 2>/dev/null || true

[[ -s "$ZCM2" ]] || { echo REPLAY_CPP_FAIL: dst; exit 1; }
"$INSPECT" "$ZCM2" --expect-gaps-max 0 --expect-frames-min 50
echo REPLAY_CPP_OK
