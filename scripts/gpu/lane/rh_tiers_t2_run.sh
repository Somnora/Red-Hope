#!/usr/bin/env bash
# Tiers furniture -> TRELLIS.2-4B. Resume-safe: an existing non-empty GLB is skipped.
# TRELLIS.2 decimates in-pipeline and bakes PBR directly, so there is no separate
# paint stage and no Blender decimation pass - unlike the Hunyuan3D lane.
# NOTE: output meshes are ORIGIN-CENTRED; ground them (Zmin -> 0) before importing
# into UE or the prop sinks halfway into the deck.
set -u
NS=/lambda/nfs/red-hope-east/red_hope
REPO=$NS/repos/TRELLIS.2
IN=$NS/io/tiers_refs
OUT=$NS/io/tiers_out
TRIS="${1:-8000}"
mkdir -p $OUT $OUT/logs
set -a; . /workspace/ephemeral/hf.env; set +a
export HF_HOME=/lambda/nfs/red-hope-east/hf-cache
export HUGGINGFACE_HUB_CACHE=$HF_HOME/hub
cd "$REPO"
for f in $IN/*.png; do
  n=$(basename "$f" .png)
  if [ -s "$OUT/$n.glb" ]; then echo "=== $n: present, skipped ==="; continue; fi
  echo "=== $n ($(date +%H:%M:%S)) ==="
  if /usr/bin/python3 $NS/scripts/rh_trellis2.py "$f" "$OUT/$n.glb" "$TRIS" > $OUT/logs/${n}.log 2>&1; then
    echo "  OK $(stat -c %s $OUT/$n.glb) bytes"
  else
    echo "  FAILED -> $OUT/logs/${n}.log"; tail -4 $OUT/logs/${n}.log
  fi
done
echo "=== TIERS BATCH COMPLETE $(date +%H:%M:%S) ==="
ls -l $OUT/*.glb 2>/dev/null
