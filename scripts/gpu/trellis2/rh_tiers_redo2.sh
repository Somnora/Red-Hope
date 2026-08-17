#!/usr/bin/env bash
# The two Tiers pieces that the first TRELLIS.2 pass could not build, re-run
# from references designed for what single-image-to-3D can actually reconstruct:
# no glass, no detached volumes, no open cavities. See Martians/gen/tiers_roster.json.
#
# nvdiffrast is NOT installed on this box (bootstrap ran with RH_NO_NVDIFFRAST=1),
# so the permissive rasterizer is not an option here - it is the only renderer.
set -u
NS=/lambda/nfs/Somnora-East/red_hope
REPO=$NS/repos/TRELLIS.2
IN=$NS/io/tiers_refs2
OUT=$NS/io/tiers_out2
mkdir -p $OUT/logs
set -a; . /workspace/ephemeral/hf.env; set +a
export HF_HOME=$NS/hf-cache
export HUGGINGFACE_HUB_CACHE=$HF_HOME/hub
export RH_PERMISSIVE_RASTER=1
export RH_RASTER_FLIP_Y=0
cd "$REPO"
for n in chemtable_lg lab_full; do
  if [ -s "$OUT/$n.glb" ]; then echo "=== $n present, skipped ==="; continue; fi
  echo "=== $n ($(date +%H:%M:%S)) ==="
  if /usr/bin/python3 $NS/scripts/rh_trellis2.py "$IN/$n.png" "$OUT/$n.glb" 8000 \
       > $OUT/logs/$n.log 2>&1; then
    echo "  OK $(stat -c %s $OUT/$n.glb) bytes"
  else
    echo "  FAILED -> $OUT/logs/$n.log"; tail -6 $OUT/logs/$n.log
  fi
done
echo "=== TIERS REDO2 COMPLETE $(date +%H:%M:%S) ==="
ls -l $OUT/*.glb 2>/dev/null
