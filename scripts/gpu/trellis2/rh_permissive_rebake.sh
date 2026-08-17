#!/usr/bin/env bash
# Re-bake the four TRELLIS.2 assets with NO nvdiffrast installed, using the
# permissive UV rasterizer. If this produces GLBs at all, the non-commercial
# dependency is gone in fact and not merely in principle.
set -u
NS=/lambda/nfs/red-hope-east/red_hope
REPO=$NS/repos/TRELLIS.2
OUT=$NS/io/permissive_out
mkdir -p $OUT/logs
set -a; . /workspace/ephemeral/hf.env; set +a
export HF_HOME=/lambda/nfs/red-hope-east/hf-cache
export HUGGINGFACE_HUB_CACHE=$HF_HOME/hub
export RH_PERMISSIVE_RASTER=1
export RH_RASTER_FLIP_Y=${RH_RASTER_FLIP_Y:-0}
cd "$REPO"
run () {  # $1 = name, $2 = reference png
  local n=$1 ref=$2
  [ -s "$OUT/$n.glb" ] && { echo "=== $n present, skipped ==="; return; }
  echo "=== $n ($(date +%H:%M:%S)) ==="
  if /usr/bin/python3 $NS/scripts/rh_trellis2.py "$ref" "$OUT/$n.glb" 8000 > $OUT/logs/$n.log 2>&1; then
    echo "  OK $(stat -c %s $OUT/$n.glb) bytes"
  else
    echo "  FAILED -> $OUT/logs/$n.log"; tail -5 $OUT/logs/$n.log
  fi
}
run crop_vine_3  $NS/io/crop_refs/crop_vine_3.png
run workbench_lg $NS/io/tiers_refs/workbench_lg.png
run workshop     $NS/io/tiers_refs/workshop.png
run infirmary    $NS/io/tiers_refs/infirmary.png
echo "=== PERMISSIVE REBAKE COMPLETE $(date +%H:%M:%S) ==="
