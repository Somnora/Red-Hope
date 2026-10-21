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
# With districts, the bot also grows the colony's life support with the city (C5e): once a sol it reads what
# the City panel says the next settler ships or the Big Arrival would need - oxygen, water and food production -
# and builds what is missing through the same player orders: Electrolyzers, Water Plants and Ice Drills, a garden
# floor (-3: bored, carved, filtered, zoned Gardening Station, planted from the soil and seeds the ships bring),
# and the solar kits and batteries all of that draws. A LIFESUPPORT line a sol logs its books. Runs without
# districts are unchanged. RH_PLAYTEST_LIFESUPPORT=0 switches it off (the C5d bot: one Electrolyzer, two
# garden cells), e.g. for the growth-off control.
#
# With districts, the bot also plays the living city (C6c): once the colony has Struct to spare beyond the open quota's bill it
# lays roads (a spine past the industry cluster, a spur to the shaft head, a road across the solar fields to the Ice Drill on
# Ice_A, with a relay pylon where the grid's discs do not meet), orders a Cargo Rover when freight tasks are open on the road,
# plans the garden for the Big Arrival's wave once it is in sight (free beds or Hope's streak), and fills ships with Solar Array
# kits instead of Spare Parts Crates. ROADS and LIVING lines log it; the REPORT block lists roads, rovers, freight and the end
# books. RH_PLAYTEST_ROADS=0 switches all of it off and gives the C6a bot back exactly (line-identical).
#
# Outposts (W1, distant ice): with roads on, the bot also opens the first outpost deposit (Ice_C, 720 m out): it surveys the hint the
# guide gives once Ice_A and Ice_B are found, lays a pylon chain 70 m apart beside the road line, the road in stretches of at most 120 m
# a sol, drills the ice and orders a Cargo Rover; every order obeys the C6c Struct rule, so it waits when Struct is short and says so.
# OUTPOST lines log it, SHIPLEG lines give each ship's Water room, and the REPORT block adds the outpost line and the bar (water made
# at the end, outpost freight, Water room per ship after the first load). RH_PLAYTEST_OUTPOST=0 switches the program off.
#
# Night storage before rovers: with the living city, each ship's Battery Bank packs are sized to carry the night - the machines' mean
# load plus the garden's grow lights, over 12 sol-hours of dark - and loaded ahead of the Cargo Rover; a rover that does not fit beside
# them waits for the next ship. LIVING lines log it and the REPORT block adds a "night storage" line (sol-hours the bank stood empty,
# grow lights dark). RH_PLAYTEST_NIGHTBANK=0 switches the rule off.
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
  "-ExecCmds=RH.Playtest $SOLS $MODE $CARGO ${RH_PLAYTEST_SAVE:-} ${RH_PLAYTEST_DISTRICTS:+districts=1} ${RH_PLAYTEST_NOGROWTH:+nogrowth=1} ${RH_PLAYTEST_LIFESUPPORT:+lifesupport=$RH_PLAYTEST_LIFESUPPORT} ${RH_PLAYTEST_ROADS:+roads=$RH_PLAYTEST_ROADS} ${RH_PLAYTEST_OUTPOST:+outpost=$RH_PLAYTEST_OUTPOST} ${RH_PLAYTEST_NIGHTBANK:+nightbank=$RH_PLAYTEST_NIGHTBANK}" > "$LOG" 2>&1 &
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
