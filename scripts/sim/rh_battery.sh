#!/bin/bash
# Full headless self-test battery: the baseline plus all 24 suites.
#
#   bash scripts/sim/rh_battery.sh [sols]
#
# Env:
#   RH_BATTERY_LOGDIR  where per-suite logs land
#                      (default <proj>/Saved/RHBattery)
#
# MUST be bash. zsh does not word-split an unquoted "$ARGS", which silently
# collapses "-sols=10 -agri" into a single argument - so every suite run turns
# into a plain baseline and the battery reports 25 greens while having tested
# one thing 25 times.
#
# JUDGE ON THE COUNTS, NEVER ON EXIT CODES. Every run here exits nonzero
# because of a benign port-8000 HttpListener bind that has nothing to do with
# the sim. The two numbers that mean anything are the LogRedHopeSim error count
# and whether the commandlet reported completion; a suite that dies early shows
# completed=0 rather than a failing status.
#
# Promoted out of session scratchpad 2026-08-15: this had been rebuilt by hand
# in three separate sessions, and a regression harness that lives in /private/tmp
# is one reboot away from not existing.
set -u

SOLS="${1:-10}"
UE="/Volumes/Unreal/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd"
PROJ_DIR="/Volumes/Unreal/red_hope/red_hope"
PROJ="$PROJ_DIR/red_hope.uproject"
LOGDIR="${RH_BATTERY_LOGDIR:-$PROJ_DIR/Saved/RHBattery}"
rm -rf "$LOGDIR"; mkdir -p "$LOGDIR"

SUITES="crew habitat vault borer rooms garden luxury hopedrive greenhouse water
         growth discovery trade earth solidarity covert espionage preemptive
         crisis alive ladder ending tiers agri"

FAILED=0

run_one() {
  local name="$1"; shift
  "$UE" "$PROJ" -run=RHSim "$@" -unattended -nosound -stdout > "$LOGDIR/$name.log" 2>&1
  local errs done_line
  errs=$(grep -c "LogRedHopeSim: Error" "$LOGDIR/$name.log")
  done_line=$(grep -c "Execution of commandlet took" "$LOGDIR/$name.log")
  echo "$name errors=$errs completed=$done_line"
  if [ "$errs" != "0" ] || [ "$done_line" = "0" ]; then
    FAILED=$((FAILED + 1))
  fi
}

echo "=== baseline (no suite switch), $SOLS sols ==="
run_one baseline "-sols=$SOLS"
echo "=== 24 suites ==="
for s in $SUITES; do
  run_one "$s" "-sols=$SOLS" "-$s"
done

echo "=== BATTERY COMPLETE: $FAILED suite(s) not green ==="
echo "logs: $LOGDIR"
exit 0
