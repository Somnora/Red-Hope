#!/bin/bash
# Boot red_hope with a REAL renderer, run console commands, harvest the
# screenshots RH.Snapshot writes, then shut the run down. This is how art gets
# verified with pixels instead of log receipts.
#
# Usage: bash rh_capture.sh <label> "<execcmds>" [expect_n] [timeout_s] [resx] [resy]
#   <execcmds> must contain at least one RH.Snapshot call.
#   expect_n   how many PNGs to wait for before shutting down (default 1)
# Env:
#   RH_SHOT_DIR  where harvested PNGs land   (default <proj>/Saved/RHCapture/shots)
#   RH_LOG_DIR   where run logs land         (default <proj>/Saved/RHCapture/logs)
#
# MUST be bash, and two details below are load-bearing.
#
# 1. -ExecCmds quoting. Established empirically, and it is NOT the form the UE
#    docs imply:
#      -ExecCmds="RH.Demo, RH.Cmd arg"   LITERAL quotes -> engine runs NOTHING.
#      -ExecCmds=RH.Demo, RH.Cmd arg     one shell-quoted arg -> works.
#    ParseExecCmdsFromCommandLine calls FParse::Value with
#    bShouldStopOnSeparator = false, so the value runs to END OF LINE.
# 2. Therefore -ExecCmds must be the LAST argument, or every switch after it is
#    swallowed into the final command as junk arguments.
#
# Judge runs on the harvested PNGs and the report block, never on exit codes: a
# benign port-8000 HttpListener bind makes them unreliable in this project.
#
# Screenshots come out at the GameUserSettings resolution, not -resx/-resy, so
# put `r.setres <W>x<H>w` first in the command list to force the frame size.

set -u

LABEL="${1:?usage: rh_capture.sh <label> \"<execcmds>\" [expect_n] [timeout_s] [resx] [resy]}"
CMDS="${2:?missing execcmds}"
EXPECT="${3:-1}"
TIMEOUT="${4:-400}"
RESX="${5:-1600}"
RESY="${6:-900}"

UE="/Volumes/Unreal/UE_5.8/Engine/Binaries/Mac/UnrealEditor"
PROJ_DIR="/Volumes/Unreal/red_hope/red_hope"
PROJ="$PROJ_DIR/red_hope.uproject"
MAP="/Game/RedHope/Maps/L_Slice"
SAVED="$PROJ_DIR/Saved/Screenshots"

OUTDIR="${RH_SHOT_DIR:-$PROJ_DIR/Saved/RHCapture/shots}"
LOGDIR="${RH_LOG_DIR:-$PROJ_DIR/Saved/RHCapture/logs}"
mkdir -p "$OUTDIR" "$LOGDIR"
LOG="$LOGDIR/$LABEL.log"

# Clean slate so the harvest is unambiguous about which run produced what.
rm -rf "$SAVED"

echo "=== capture '$LABEL' ==="
echo "cmds: $CMDS"
echo "expect: $EXPECT png(s), timeout ${TIMEOUT}s, ${RESX}x${RESY}"

"$UE" "$PROJ" "$MAP" -game -windowed -resx="$RESX" -resy="$RESY" \
  -unattended -nosound -stdout \
  "-ExecCmds=$CMDS" > "$LOG" 2>&1 &
PID=$!

count_png() { find "$SAVED" -name '*.png' 2>/dev/null | wc -l | tr -d ' '; }

START=$SECONDS
EARLY_EXIT=0
while [ $((SECONDS - START)) -lt "$TIMEOUT" ]; do
  if ! kill -0 "$PID" 2>/dev/null; then
    EARLY_EXIT=1
    break
  fi
  if [ "$(count_png)" -ge "$EXPECT" ]; then
    sleep 4   # grace: let the last PNG finish flushing to disk
    break
  fi
  sleep 2
done
ELAPSED=$((SECONDS - START))
GOT=$(count_png)

# Shut down politely, then insist.
if kill -0 "$PID" 2>/dev/null; then
  kill "$PID" 2>/dev/null
  for _ in 1 2 3 4 5 6; do
    kill -0 "$PID" 2>/dev/null || break
    sleep 1
  done
  kill -9 "$PID" 2>/dev/null
fi
wait "$PID" 2>/dev/null

HARVEST=0
if [ "$GOT" -gt 0 ]; then
  IDX=0
  while IFS= read -r f; do
    cp "$f" "$OUTDIR/$(printf '%s_%02d.png' "$LABEL" "$IDX")"
    IDX=$((IDX + 1))
  done < <(find "$SAVED" -name '*.png' | sort)
  HARVEST=$IDX
fi

echo "--- report ---"
echo "elapsed:        ${ELAPSED}s"
echo "early exit:     $EARLY_EXIT"
echo "png produced:   $GOT (harvested $HARVEST -> $OUTDIR/${LABEL}_NN.png)"
echo "execcmds fired: $(grep -c 'RH.Demo\]' "$LOG")"
echo "snapshot log:   $(grep -c 'Snapshot requested' "$LOG")"
echo "sim errors:     $(grep -c 'LogRedHopeSim: Error' "$LOG")"
echo "usage warnings: $(grep -ci 'is not compatible with\|missing usage flag\|bUsedWithSkeletalMesh' "$LOG")"
echo "max frame:      $(grep -oE '^\[[0-9.-]+[^]]*\]\[ *[0-9]+\]' "$LOG" | grep -oE '\[ *[0-9]+\]$' | tr -d '[] ' | sort -n | tail -1)"
echo "log:            $LOG"
