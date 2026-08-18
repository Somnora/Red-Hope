#!/usr/bin/env bash
# Bake the regenerated 8 crew through TRELLIS.2 - the FIRST batch on the
# real-normals path (RH_KEEP_HIPOLY + rh_bake_normal.py per asset).
# Inputs: alpha-cut hero fronts at $NS/io/crew8/in/<id>.png
# NOTE: local re-rigging of these outputs MUST use RH_TARGET_HEIGHT_M=1.99 (see character-redo-spec).
# Outputs: $NS/io/crew8/out/<id>.glb, <id>_hi.glb, <id>_real_normal.png
set -u
NS=/lambda/nfs/red-hope-east/red_hope
OUT=$NS/io/crew8/out
mkdir -p $OUT/logs
set -a; . /workspace/ephemeral/hf.env; set +a
export HF_HOME=/lambda/nfs/red-hope-east/hf-cache
export HUGGINGFACE_HUB_CACHE=$HF_HOME/hub
export RH_PERMISSIVE_RASTER=1
export RH_KEEP_HIPOLY=1
# Headless Blender on a fresh box needs X11 shims (proof bake, 2026-08-18).
sudo apt-get install -y -q libsm6 libxext6 libxrender1 libxi6 libxxf86vm1 libxfixes3 libgl1 > /dev/null 2>&1 || true
cd "$NS/repos/TRELLIS.2"
for n in bot_lindqvist comms_diallo cook_moreau driver_costa fab_stone rookie_shaw safety_abara vet_kowalski; do
  if [ -s "$OUT/$n.glb" ] && [ -s "$OUT/${n}_real_normal.png" ]; then
    echo "=== $n complete, skipped ==="; continue
  fi
  echo "=== $n ($(date +%H:%M:%S)) ==="
  if [ ! -s "$OUT/$n.glb" ]; then
    if /usr/bin/python3 $NS/scripts/rh_trellis2.py "$NS/io/crew8/in/$n.png" "$OUT/$n.glb" 12000 > $OUT/logs/$n.log 2>&1; then
      echo "  mesh OK $(stat -c %s $OUT/$n.glb) bytes (hi: $(stat -c %s $OUT/${n}_hi.glb 2>/dev/null || echo MISSING))"
    else
      echo "  mesh FAILED -> $OUT/logs/$n.log"; tail -4 $OUT/logs/$n.log; continue
    fi
  fi
  if $NS/tools/blender-4.2.22-linux-x64/blender -b -P $NS/scripts/rh_bake_normal.py -- \
      "$OUT/$n.glb" "$OUT/${n}_hi.glb" "$OUT/${n}_real_normal.png" 2048 0.05 > $OUT/logs/${n}_nrm.log 2>&1; then
    echo "  normal OK $(stat -c %s $OUT/${n}_real_normal.png) bytes"
  else
    echo "  normal FAILED -> $OUT/logs/${n}_nrm.log"; tail -3 $OUT/logs/${n}_nrm.log
  fi
done
echo "CREW8_BAKES_DONE $(date +%H:%M:%S)"
