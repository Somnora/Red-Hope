#!/bin/bash
# Package a standalone, double-clickable Mac build of The Red Hope.
#
#   bash scripts/unreal/rh_package.sh [out_dir=/Volumes/Unreal/red_hope/Packaged] [config=Development]
#
# Builds the game target, cooks, stages, paks and archives. The .app lands in
# <out_dir>/Mac/. The editor must not be running (UBT and the cooker share the
# editor binaries) and nothing else heavy should be: on an 8 GB machine the
# cook and its shader compilation want the memory to themselves.
#
# What the cook includes is set in Config/DefaultGame.ini
# [/Script/UnrealEd.ProjectPackagingSettings]: /Game/RedHope is cooked whole
# (the game loads most art by path), L_Slice is the map, and Notices/ is
# staged beside the executable as loose files (licence notices).
#
# Log: <out_dir>/rh_package.log. Judge on "BUILD SUCCESSFUL" and the .app,
# not the exit code alone.
set -u

OUT="${1:-/Volumes/Unreal/red_hope/Packaged}"
CONFIG="${2:-Development}"
UAT="/Volumes/Unreal/UE_5.8/Engine/Build/BatchFiles/RunUAT.sh"
PROJ="/Volumes/Unreal/red_hope/red_hope/red_hope.uproject"
mkdir -p "$OUT"

"$UAT" BuildCookRun -project="$PROJ" -platform=Mac -clientconfig="$CONFIG" \
  -build -cook -stage -pak -archive -archivedirectory="$OUT" \
  -unattended -utf8output -nop4 -nocompileeditor -skipbuildeditor \
  > "$OUT/rh_package.log" 2>&1
STATUS=$?

# UE 5.8 (modern Xcode mode) assembles the complete app - executable, the TBB
# dylibs under Contents/UE/red_hope/Binaries, the paks - at
# Saved/StagedBuilds/Mac/red_hope.app, but -archive copies Binaries/Mac/
# red_hope.app, which holds the bare executable only: it dies at launch on
# "Library not loaded: @rpath/libtbb.12.dylib" (first packaged build,
# 2026-10-15). Replace the archived copy with the staged one.
STAGED="/Volumes/Unreal/red_hope/red_hope/Saved/StagedBuilds/Mac/red_hope.app"
if [ "$STATUS" -eq 0 ] && [ -d "$STAGED/Contents/UE" ]; then
  rm -rf "$OUT/Mac/red_hope.app"
  ditto "$STAGED" "$OUT/Mac/red_hope.app"
fi

echo "exit: $STATUS"
grep -E "BUILD SUCCESSFUL|BUILD FAILED|AutomationTool exiting with ExitCode" "$OUT/rh_package.log" | tail -3
ls -d "$OUT"/Mac/*.app 2>/dev/null
du -sh "$OUT"/Mac/*.app 2>/dev/null
