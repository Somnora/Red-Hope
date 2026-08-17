#!/bin/bash
# Pop up the game window WITHOUT the editor - the director's sandbox view.
#
#   bash scripts/unreal/rh_sandbox.sh              # interior: the everything-demo, ride down
#   bash scripts/unreal/rh_sandbox.sh surface      # all buildings on a grid, camera up top
#   bash scripts/unreal/rh_sandbox.sh interior     # same as default, explicit
#   LOW=1 bash scripts/unreal/rh_sandbox.sh        # 1280x720 + 30fps cap for a busy machine
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
  # Everything placed, then back up top: the building set in daylight.
  CMDS="r.setres ${RESX}x${RESY}w, RH.Demo, RH.ActivateCrop all, RH.Showcase, RH.Floor 0, RH.SetSpeed 3, RH.Cam 20 0 0.22 0 0$EXTRA"
  ;;
interior|*)
  # The everything button rides you down to floor -1: rooms, furniture, crew.
  CMDS="r.setres ${RESX}x${RESY}w, RH.Demo, RH.ActivateCrop all, RH.SetSpeed 3$EXTRA"
  ;;
esac

echo "sandbox: $PRESET at ${RESX}x${RESY} (Cmd+Q or Ctrl+C to quit)"
echo "useful once it is up:  RH.Floor 0|-1   RH.Cam <Xm> <Ym> <zoom 0-1>   RH.SetSpeed 1|3|10"
exec "$UE" "$PROJ" "$MAP" -game -windowed -resx="$RESX" -resy="$RESY" \
  -nosound "-ExecCmds=$CMDS"
