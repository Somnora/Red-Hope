#!/bin/bash
# The first-hour playtest: a scripted player walks the guide's path in the
# real game (live robots, agent band at 16x) and logs a timeline.
#
#   bash scripts/sim/rh_playtest.sh [sols=30] [label=playtest] [rush|guide] [lean|full]
#
# rush  = every opening order at once (all kits, pad, dig, Forge, survey)
# guide = the first-hour guide's order: power + the Charging Pad first, the
#         dig and the Forge only once the pad stands
# lean  = the crew ship carries the Crew Pod alone (full = plus soil, seeds,
#         comforts - the data ladder has two quotas, so it is the last ship)
#
# Windowless (-nullrhi) on a fixed time step (-benchmark -fps=10): every frame
# advances 0.1 s of game time however fast the machine runs it, so 30 sols
# (~37 minutes of 16x play) take about two minutes, boot included. RH.Playtest quits the game
# itself when the first crew has lived a sol, or at the sol limit.
#
# Balance sweeps: RH_PLAYTEST_EXTRA is passed to the game before -ExecCmds, e.g.
#   RH_PLAYTEST_EXTRA="-RHConfig=StarterSolarPacks=9,StarterBatteryPacks=3 -RHQuotas=Q3"
# (config-row overrides and dormant quota activation; both log loudly).
#
# Output: Saved/RHPlaytest/<label>.log (full) and <label>.txt (the [Playtest]
# lines - the timeline, per-sol vitals and the REPORT block).
#
# MUST be bash, and -ExecCmds must be the LAST argument with no literal quotes
# (see scripts/unreal/rh_capture.sh for why).
set -u

SOLS="${1:-30}"
LABEL="${2:-playtest}"
MODE="${3:-rush}"
CARGO="${4:-full}"
TIMEOUT="${RH_PLAYTEST_TIMEOUT:-1800}"
UE="/Volumes/Unreal/UE_5.8/Engine/Binaries/Mac/UnrealEditor"
PROJ_DIR="/Volumes/Unreal/red_hope/red_hope"
OUT="$PROJ_DIR/Saved/RHPlaytest"
mkdir -p "$OUT"
LOG="$OUT/$LABEL.log"

"$UE" "$PROJ_DIR/red_hope.uproject" /Game/RedHope/Maps/L_Slice -game -nullrhi \
  -benchmark -fps=10 -unattended -nosound -stdout ${RH_PLAYTEST_EXTRA:-} \
  "-ExecCmds=RH.Playtest $SOLS $MODE $CARGO" > "$LOG" 2>&1 &
PID=$!

# Wait for the game to quit itself; kill it only past the timeout.
for ((i = 0; i < TIMEOUT; i++)); do
  kill -0 "$PID" 2>/dev/null || break
  sleep 1
done
if kill -0 "$PID" 2>/dev/null; then
  echo "timeout after ${TIMEOUT}s - killing"
  kill "$PID"
fi

grep -h "\[Playtest\]" "$LOG" | sed 's/^.*\[Playtest\]/[Playtest]/' > "$OUT/$LABEL.txt"
echo "playtest lines: $(wc -l < "$OUT/$LABEL.txt")  sim errors: $(grep -c 'LogRedHopeSim: Error' "$LOG")"
echo "timeline: $OUT/$LABEL.txt"
