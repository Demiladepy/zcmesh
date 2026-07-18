#!/usr/bin/env bash
# C++-only preferred-hop recovery (Linux CI twin).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
BIN="$ROOT/core-engine/build"
PHASE="${1:-3}"
RATE="${2:-400}"
ZCM="$ROOT/prefhop-cpp.zcm"
CAP_ERR="$ROOT/prefhop-cpp-cap-err.txt"
HOP0_ERR="$ROOT/prefhop-cpp-hop0-err.txt"
HOP1_ERR="$ROOT/prefhop-cpp-hop1-err.txt"
EDGE_ERR="$ROOT/prefhop-cpp-edge-err.txt"

EDGE="$BIN/zcmesh_edge"
HOP="$BIN/zcmesh_hop"
CTL="$BIN/zcmesh_ctl"
CAP="$BIN/zcmesh_capture"
if [[ ! -x "$EDGE" || ! -x "$CTL" ]]; then
  echo "missing binaries — build core-engine first"
  exit 1
fi

rm -f "$ZCM" "$CAP_ERR" "$HOP0_ERR" "$HOP1_ERR" "$EDGE_ERR"
CAP_SEC=$((PHASE * 3 + 3))
EDGE_DUR=$((PHASE * 3 + 1))

"$CAP" --listen 127.0.0.1:9930 --out "$ZCM" --seconds "$CAP_SEC" --mode udp 2>"$CAP_ERR" &
CAP_PID=$!
sleep 1
"$HOP" --listen 127.0.0.1:9928 --forward 127.0.0.1:9930 --final 2>"$HOP0_ERR" &
HOP0_PID=$!
"$HOP" --listen 127.0.0.1:9929 --forward 127.0.0.1:9930 --final 2>"$HOP1_ERR" &
HOP1_PID=$!
"$EDGE" --operator 127.0.0.1:9930 --transport mesh \
  --hop 127.0.0.1:9928 --hop 127.0.0.1:9929 --control 127.0.0.1:9897 \
  --rate "$RATE" --batch 4 --duration "$EDGE_DUR" --print-stats-sec 1 \
  2>"$EDGE_ERR" &
EDGE_PID=$!

sleep "$PHASE"
"$CTL" --target 127.0.0.1:9897 --node-id 1 --skip 1
sleep "$PHASE"
"$CTL" --target 127.0.0.1:9897 --node-id 1 --clear
RECOVER_SLEEP=$PHASE
if [[ "$RECOVER_SLEEP" -lt 2 ]]; then RECOVER_SLEEP=2; fi
sleep "$RECOVER_SLEEP"

wait "$CAP_PID" || true
kill "$EDGE_PID" "$HOP0_PID" "$HOP1_PID" 2>/dev/null || true
wait "$EDGE_PID" 2>/dev/null || true
wait "$HOP0_PID" 2>/dev/null || true
wait "$HOP1_PID" 2>/dev/null || true

tail -n 12 "$EDGE_ERR" || true

saw_demote=0
saw_recover=0
seen_hop1=0
while IFS= read -r line; do
  if [[ "$line" =~ hop=([0-9]+)[[:space:]]+route_fail=[0-9]+[[:space:]]+hop_ok=([0-9]+)/([0-9]+)/ ]]; then
    h="${BASH_REMATCH[1]}"
    ok0="${BASH_REMATCH[2]}"
    ok1="${BASH_REMATCH[3]}"
    if [[ "$h" == "1" && "$ok1" -gt 0 ]]; then
      seen_hop1=1
      saw_demote=1
    fi
    if [[ "$seen_hop1" == "1" && "$h" == "0" && "$ok0" -gt 0 ]]; then
      saw_recover=1
    fi
  fi
done < <(grep -E 'hop=[0-9]+' "$EDGE_ERR" || true)

ctrl_ok=0
grep -q 'ctrl SET_SKIP' "$EDGE_ERR" && grep -q 'ctrl CLEAR' "$EDGE_ERR" && ctrl_ok=1 || true
cap_ok=0
[[ -s "$ZCM" ]] && cap_ok=1

echo "demote=$saw_demote recover=$saw_recover ctrl=$ctrl_ok cap=$cap_ok"
if [[ "$saw_demote" == "1" && "$saw_recover" == "1" && "$ctrl_ok" == "1" && "$cap_ok" == "1" ]]; then
  echo PREFHOP_CPP_OK
  exit 0
fi
echo PREFHOP_CPP_FAIL
exit 1
