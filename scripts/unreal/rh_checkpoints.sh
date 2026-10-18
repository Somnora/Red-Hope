#!/bin/bash
# Hand-play checkpoints: real games in progress, saved part-way through, so
# the crew phase can be played by hand without the 3.5 hours at 1x it takes to
# reach it. The RH.Playtest bot plays each game from a fresh start (live
# robots, player orders only, the guide's order) and saves it at the sol
# limit; rh_sandbox.sh midgame | late then loads the save in a normal window.
#
#   bash scripts/unreal/rh_checkpoints.sh [midgame|late|all=all] [--force]
#
# midgame  RH_Midgame.sav  sol 28.35, just after sunrise, lean cargo (the
#          crew ship brought the Crew Pod alone). Four crew in the vault, no
#          garden yet, Q3 nearly met and the ship's manifest still empty: the
#          next cargo is the player's call. Ice_A runs low about two sols on.
# late     RH_Late.sav     sol 62.35, just after sunrise, full cargo. Pop 6,
#          two born on Mars, the garden producing, the water expansion to
#          Ice_B done, round 5 of the repeating ladder open, Hope at 100.
#
# Each save has a stamp beside it (RH_<Slot>.stamp) recording what made it:
# the committed Source, docs/data, Config and Content trees, any uncommitted
# change under them, and the compiled modules' build times. A missing or
# stale save is re-made (about 2 minutes for midgame, 5 for late); a current
# one is kept. Saves are strict about their format, so a stale one would
# refuse to load rather than play an old game.
#
# MUST be bash. Never run it while the editor target is compiling (the
# playtest boots the game binary).
set -u

WHICH="${1:-all}"
FORCE="${2:-}"
[ "$WHICH" = "--force" ] && { WHICH="all"; FORCE="--force"; }
PROJ_DIR="/Volumes/Unreal/red_hope/red_hope"
SAVES="$PROJ_DIR/Saved/SaveGames"
mkdir -p "$SAVES"

# What a save depends on. A change to any of it re-makes the save.
fingerprint() {
  {
    git -C "$PROJ_DIR" rev-parse HEAD:Source HEAD:docs/data HEAD:Config HEAD:Content
    git -C "$PROJ_DIR" status --porcelain -- Source docs/data Config Content
    stat -f '%m %N' "$PROJ_DIR"/Binaries/Mac/libUnrealEditor-RedHope*.dylib
    echo "$*"
  } 2>&1 | shasum | cut -c1-16
}

# make <Slot> <Sols> <lean|full>
make_checkpoint() {
  local SLOT="$1" SOLS="$2" CARGO="$3"
  local SAV="$SAVES/RH_$SLOT.sav" STAMP="$SAVES/RH_$SLOT.stamp"
  local FP
  FP="$(fingerprint "$SLOT $SOLS guide $CARGO")"
  if [ "$FORCE" != "--force" ] && [ -f "$SAV" ] && [ "$(cat "$STAMP" 2>/dev/null)" = "$FP" ]; then
    echo "checkpoint $SLOT: current (sol $SOLS, $CARGO cargo)"
    return 0
  fi
  echo "checkpoint $SLOT: making it - the bot plays to sol $SOLS ($CARGO cargo)..."
  rm -f "$SAV" "$STAMP"
  RH_PLAYTEST_SAVE="$SLOT" bash "$PROJ_DIR/scripts/sim/rh_playtest.sh" "$SOLS" "checkpoint_$SLOT" guide "$CARGO" | sed 's/^/  /'
  if [ -f "$SAV" ] && grep -q "\[Playtest\] SAVED slot '$SLOT'" "$PROJ_DIR/Saved/RHPlaytest/checkpoint_$SLOT.txt"; then
    echo "$FP" > "$STAMP"
    echo "checkpoint $SLOT: saved ($(grep -h "SAVED slot" "$PROJ_DIR/Saved/RHPlaytest/checkpoint_$SLOT.txt" | sed 's/.*SAVED //'))"
  else
    echo "checkpoint $SLOT: FAILED - no save written; see Saved/RHPlaytest/checkpoint_$SLOT.log"
    return 1
  fi
}

STATUS=0
case "$WHICH" in
midgame) make_checkpoint Midgame 28.35 lean || STATUS=1 ;;
late)    make_checkpoint Late 62.35 full || STATUS=1 ;;
all)
  make_checkpoint Midgame 28.35 lean || STATUS=1
  make_checkpoint Late 62.35 full || STATUS=1
  ;;
*) echo "usage: rh_checkpoints.sh [midgame|late|all] [--force]"; exit 2 ;;
esac
exit $STATUS
