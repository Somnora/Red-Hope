#!/bin/bash
# The click test: boot red_hope with a REAL renderer and the title up, then press the deck and menu buttons with real
# Slate pointer events (RH.ClickTest, Source/RedHope/Private/RHClickTest.cpp) and assert each effect. A button that
# cannot be reached, or a click that leaks into the world, fails loudly: one "CLICKTEST <name> PASS|FAIL <why>" line
# per step in the log, then "CLICKTEST TOTAL" and "CLICKTEST RESULT PASS|FAIL".
#
# Usage: bash rh_clicktest.sh [label] [timeout_s]
# Env:   RH_LOG_DIR  where the run log lands (default <proj>/Saved/RHCapture/logs)
#        RH_CLICKTEST_PRE  console commands run before RH.ClickTest, comma-separated (D5 depth: "RH.BoreNow 14" bores the colony past
#                          -11 first, so shaft rows scroll out of the strip and Fit lift and Tunnel must still be reached)
#        RH_CLICKTEST_RES  window size WxH (default 1600x900; use 1440x860 on a 1470x956-point display, where 1600x900 is clamped
#                          and the early deck steps fail on reach - the official size on this machine, docs/qa-boot-card.md)
# Exit:  0 only when the run printed "CLICKTEST RESULT PASS" (a missing verdict, a hang or any FAIL is non-zero).
#
# Run it after every UI commit and at every gate (docs/qa-boot-card.md). One Unreal process at a time.
#
# Same -ExecCmds rules as rh_capture.sh: -ExecCmds is the LAST argument and is not quoted. -RHTitle forces the title
# up (an -unattended boot skips it otherwise), which the title steps need.

set -u

LABEL="${1:-clicktest}"
TIMEOUT="${2:-240}"

UE="/Volumes/Unreal/UE_5.8/Engine/Binaries/Mac/UnrealEditor"
PROJ_DIR="/Volumes/Unreal/red_hope/red_hope"
PROJ="$PROJ_DIR/red_hope.uproject"
MAP="/Game/RedHope/Maps/L_Slice"
LOGDIR="${RH_LOG_DIR:-$PROJ_DIR/Saved/RHCapture/logs}"
mkdir -p "$LOGDIR"
LOG="$LOGDIR/$LABEL.log"

PRE="${RH_CLICKTEST_PRE:-}"
RES="${RH_CLICKTEST_RES:-1600x900}"
CMDS="r.setres ${RES}w, RH.Demo, ${PRE:+$PRE, }RH.After 3 RH.ClickTest"

echo "=== click test '$LABEL' ==="
"$UE" "$PROJ" "$MAP" -game -windowed -resx=${RES%x*} -resy=${RES#*x} \
  -unattended -nosound -stdout -RHTitle \
  "-ExecCmds=$CMDS" > "$LOG" 2>&1 &
PID=$!

START=$SECONDS
while [ $((SECONDS - START)) -lt "$TIMEOUT" ]; do
  if ! kill -0 "$PID" 2>/dev/null; then break; fi
  if grep -q 'CLICKTEST RESULT' "$LOG" 2>/dev/null; then sleep 2; break; fi
  sleep 2
done
ELAPSED=$((SECONDS - START))

if kill -0 "$PID" 2>/dev/null; then
  kill "$PID" 2>/dev/null
  for _ in 1 2 3 4 5 6; do
    kill -0 "$PID" 2>/dev/null || break
    sleep 1
  done
  kill -9 "$PID" 2>/dev/null
fi
wait "$PID" 2>/dev/null

echo "--- click test ---"
grep -E 'CLICKTEST |RHClick: (REACH|UNREACHABLE)' "$LOG" | sed -E 's/^\[[^]]*\]\[ *[0-9]+\]//'
echo "--- report ---"
echo "elapsed:        ${ELAPSED}s"
echo "log:            $LOG"
if grep -q 'CLICKTEST RESULT PASS' "$LOG"; then
  echo "CLICKTEST VERDICT: PASS"
  exit 0
fi
if grep -q 'CLICKTEST RESULT FAIL' "$LOG"; then
  echo "CLICKTEST VERDICT: FAIL ($(grep -c 'CLICKTEST .* FAIL ' "$LOG") step(s) failed)"
else
  echo "CLICKTEST VERDICT: FAIL (no verdict in the log - the run died or timed out)"
fi
exit 1
