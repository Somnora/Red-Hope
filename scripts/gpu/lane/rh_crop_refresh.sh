#!/usr/bin/env bash
# Prop refresh through TRELLIS.2. Half the list per box, so the bake phase runs
# in parallel instead of serially. $1 = suffix (A or B).
# nvdiffrast is NOT installed here (RH_NO_NVDIFFRAST=1 at bootstrap), so the
# permissive rasterizer is the only renderer, not a fallback.
set -u
S="${1:?usage: rh_props_refresh.sh <A|B>}"
NS=/lambda/nfs/red-hope-east/red_hope
REPO=$NS/repos/TRELLIS.2
IN=$NS/io/crop_refs_$S
OUT=$NS/io/crop_out_$S
mkdir -p $OUT/logs
set -a; . /workspace/ephemeral/hf.env; set +a
export HF_HOME=$NS/hf-cache HUGGINGFACE_HUB_CACHE=$NS/hf-cache/hub
export RH_PERMISSIVE_RASTER=1 RH_RASTER_FLIP_Y=0
cd "$REPO"
for f in $IN/*.png; do
  [ -e "$f" ] || { echo "no inputs in $IN"; break; }
  n=$(basename "$f" .png)
  [ -s "$OUT/$n.glb" ] && { echo "=== $n present, skipped ==="; continue; }
  echo "=== $n ($(date +%H:%M:%S)) ==="
  if /usr/bin/python3 $NS/scripts/rh_trellis2.py "$f" "$OUT/$n.glb" 8000 > $OUT/logs/$n.log 2>&1; then
    echo "  OK $(stat -c %s $OUT/$n.glb) bytes"
  else
    echo "  FAILED -> $OUT/logs/$n.log"; tail -5 $OUT/logs/$n.log
  fi
done
echo "=== CROP REFRESH $S COMPLETE $(date +%H:%M:%S) ==="
ls -l $OUT/*.glb 2>/dev/null | wc -l
