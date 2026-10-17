#!/bin/bash
# Pop up the game window WITHOUT the editor - the director's sandbox view.
#
#   bash scripts/unreal/rh_sandbox.sh              # interior: the everything-demo, ride down
#   bash scripts/unreal/rh_sandbox.sh surface      # all buildings on a grid, camera up top
#   bash scripts/unreal/rh_sandbox.sh interior     # same as default, explicit
#   bash scripts/unreal/rh_sandbox.sh build        # a surface habitat, stocked to build, zone, furnish and fit
#   bash scripts/unreal/rh_sandbox.sh crew         # floor -1 furnished: the crew sleep, work, eat and unwind
#   LOW=1 bash scripts/unreal/rh_sandbox.sh        # 1280x720 + 30fps cap for a busy machine
#   SOUND=1 bash scripts/unreal/rh_sandbox.sh      # with sound and the score (the listening test)
#
# Quit with Cmd+Q in the window, or Ctrl+C in this terminal.
#
# This is the same boot the capture harness uses (-game against the real Metal
# renderer), minus the screenshot harvest and the kill. It is NOT the editor:
# no asset browser, no shader-editing machinery, no derived-data churn - the
# whole editor UI layer never loads, which is most of the editor's memory. On
# this project the -game client sits in the low single-digit GB against the
# editor's much larger footprint.
#
# The two -ExecCmds rules are load-bearing and easy to re-break (established
# empirically, recorded in rh_capture.sh):
#   1. NO literal quotes inside -ExecCmds= - the engine then runs NOTHING.
#   2. -ExecCmds must be the LAST argument - FParse reads it to end-of-line,
#      so any switch after it is swallowed as junk.
set -u

PRESET="${1:-interior}"
UE="/Volumes/Unreal/UE_5.8/Engine/Binaries/Mac/UnrealEditor"
PROJ_DIR="/Volumes/Unreal/red_hope/red_hope"
PROJ="$PROJ_DIR/red_hope.uproject"
MAP="/Game/RedHope/Maps/L_Slice"

if [ "${LOW:-0}" = "1" ]; then
  RESX=1280; RESY=720; EXTRA=", t.MaxFPS 30"
else
  RESX=1600; RESY=900; EXTRA=""
fi

case "$PRESET" in
surface)
  # Everything placed, then back up top: the building set in daylight, and a
  # dig designation at the regolith deposit so the robot fleet visibly WORKS -
  # without an order the starting fleet idles in its spawn line, which reads as
  # broken (director, 2026-08-17: "I haven't seen them move around at all").
  CMDS="r.setres ${RESX}x${RESY}w, RH.Demo, RH.ActivateCrop all, RH.Showcase, RH.Floor 0, RH.Dig Regolith_A, RH.SetSpeed 3, RH.Cam 20 0 0.22 0 0$EXTRA"
  ;;
build)
  # Build mode hand-play (2026-10-17): RH.HabDemo's surface habitat (a 2x2
  # module, a corridor, a 1x2 module, an air filter, its bays zoned), enough
  # Struct, Glass and Shielding to build, furnish and fit by hand, a minute at
  # 60x to fill it with air, then 1x over the habitat. The ZONE | FURNISH |
  # WALLS row shows on the surface once a habitat stands.
  CMDS="r.setres ${RESX}x${RESY}w, RH.Demo, RH.ActivateCrop all, RH.HabDemo, RH.AddSolid Lander Struct 3000, RH.AddStock Glass 1000, RH.AddStock Shielding 500, RH.SetSpeed 60, RH.After 25 RH.SetSpeed 1, RH.After 26 RH.Floor 0, RH.After 26.5 RH.Cam 45 35 0.18 30 0$EXTRA"
  ;;
crew)
  # Crew life hand-play (alive pass 2): floor -1's rooms furnished - a
  # bedroom (cots and a bunk), a lounge, desks, a lab bench, a dining room -
  # so the crew sleep in their beds, work at their desks, eat and unwind.
  # rh.CrewForceBeat 1-4 (0 = natural) forces sleep / work / eat / relax.
  F="RH.Furnish Cot -1 0 0 0, RH.Furnish Cot -1 0 2 0, RH.Furnish BunkBed -1 0 8 0, RH.Furnish Lamp -1 0 10 0, RH.Furnish Rug -1 0 11 0, RH.Furnish Plant -1 0 15 0"
  F="$F, RH.Furnish Sofa -1 1 9 0, RH.Furnish Rug -1 1 5 0, RH.Furnish Lamp -1 1 11 0, RH.Furnish Bench -1 1 1 2"
  F="$F, RH.Furnish Desk -1 2 8 0, RH.Furnish Desk -1 2 10 0, RH.Furnish Plant -1 2 0 0, RH.Furnish LabBench -1 3 0 0"
  F="$F, RH.Furnish DiningTable -1 4 5 0, RH.Furnish Chair -1 4 1 2, RH.Furnish Chair -1 4 2 2, RH.Furnish Chair -1 4 13 0, RH.Furnish Chair -1 4 14 0, RH.Furnish Lamp -1 4 3 0"
  CMDS="r.setres ${RESX}x${RESY}w, RH.Demo, RH.ActivateCrop all, RH.AddSolid Lander Struct 3000, $F, RH.SetSpeed 60, RH.After 30 RH.SetSpeed 3, RH.After 31 RH.Floor -1, RH.After 31.5 RH.Cam 15 10 0.08 30 0$EXTRA"
  ;;
interior|*)
  # The everything button rides you down to floor -1: rooms, furniture, crew.
  CMDS="r.setres ${RESX}x${RESY}w, RH.Demo, RH.ActivateCrop all, RH.SetSpeed 3$EXTRA"
  ;;
esac

echo "sandbox: $PRESET at ${RESX}x${RESY} (Cmd+Q or Ctrl+C to quit)"
echo "useful once it is up:  RH.Floor 0|-1   RH.Cam <Xm> <Ym> <zoom 0-1>   RH.SetSpeed 1|3|10   rh.CrewForceBeat 0-4"
# Sound is off by default (a sandbox is usually for looking); SOUND=1 is the
# listening test for the sound pass and the score.
if [ "${SOUND:-0}" = "1" ]; then NOSOUND=""; else NOSOUND="-nosound"; fi
exec "$UE" "$PROJ" "$MAP" -game -windowed -resx="$RESX" -resy="$RESY" \
  $NOSOUND "-ExecCmds=$CMDS"
