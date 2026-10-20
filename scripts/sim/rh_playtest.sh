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
#         comforts; from the third ship on every ship also carries Spare Parts -
#         the last quota rung repeats with bills x QuotaRepeatBillMul, D1a, so
#         the bot fills and launches one ship per round and logs a ROUND line
#         per landing: opened / met / launched / landed sol and the cargo)
#
# When Ice_A runs low the bot also plays the water expansion: survey far north,
# chain pylons out to Ice_B, drill it and build a Water Plant beside the drill
# (marks: "ice running low", "Ice_B found", "grid reaches Ice_B", "second Ice
# Drill running", "Water Plant beside Ice_B").
#
# Windowless (-nullrhi) on a fixed time step (-benchmark -fps=10): every frame
# advances 0.1 s of game time however fast the machine runs it, so 30 sols
# (~37 minutes of 16x play) take about two minutes, boot included. RH.Playtest quits the game
# itself at the sol limit, or when the colony empties.
#
# Balance sweeps: RH_PLAYTEST_EXTRA is passed to the game before -ExecCmds, e.g.
#   RH_PLAYTEST_EXTRA="-RHConfig=StarterSolarPacks=9,StarterBatteryPacks=3,QuotaRepeatBillMul=1.5"
# (config-row overrides, and -RHQuotas=<row> for any dormant quota row; both
# log loudly).
#
# City layer (C5b): RH_PLAYTEST_DISTRICTS=1 adds the districts option - the bot paints a
# Homes district over floor -1 once it rates (and over a rated surface habitat, if it has one),
# carves and rates floor -2 for a Civic district, and the timeline gains GROWTH lines (each
# project's start and end, one a sol) and a REPORT block of what the city built:
#   RH_PLAYTEST_DISTRICTS=1 bash scripts/sim/rh_playtest.sh 110 c5b_full guide full
# RH_PLAYTEST_NOGROWTH=1 (with the above) is the control: the same painting and the same
# second floor, but the city's growth is switched off, so the difference is the growth alone.
#
# RH_PLAYTEST_LIFESUPPORT=1 (with districts) is the player's answer to the City panel's "the settlers are
# ready once oxygen production can carry N more" (C5d): up to three more Electrolyzers, one every eight
# sols while the Big Arrival waits on oxygen production. Off by default, so the runs stay comparable.
#
# Checkpoints: RH_PLAYTEST_SAVE=<Slot> saves the colony at the sol limit (which
# may be fractional, e.g. 28.35 = just after sunrise on sol 28) to
# Saved/SaveGames/RH_<Slot>.sav (a "[Playtest] SAVED" line confirms it). The
# hand-play checkpoints are made this way by scripts/unreal/rh_checkpoints.sh.
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
  "-ExecCmds=RH.Playtest $SOLS $MODE $CARGO ${RH_PLAYTEST_SAVE:-} ${RH_PLAYTEST_DISTRICTS:+districts=1} ${RH_PLAYTEST_NOGROWTH:+nogrowth=1} ${RH_PLAYTEST_LIFESUPPORT:+lifesupport=1}" > "$LOG" 2>&1 &
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
