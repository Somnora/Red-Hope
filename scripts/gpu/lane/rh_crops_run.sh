#!/usr/bin/env bash
# Red Hope crops: reference PNG -> shape GLB -> painted GLB, via Hunyuan3D 2.1.
# Resume-safe: an existing non-empty output is skipped, so a rerun costs nothing
# for work already done and the batch can be interrupted without losing a stage.
# Optional $1 filters to one crop id (pilot before committing the whole batch).
set -u
NS=/lambda/nfs/red-hope-east/red_hope
REPO=$NS/repos/Hunyuan3D-2.1
IN=$NS/io/crop_refs
OUT=$NS/io/crops_out
FILTER="${1:-}"
mkdir -p $OUT/shape $OUT/painted $OUT/logs
source $HOME/rh3d-hy3d/bin/activate
export HF_HOME=/lambda/nfs/red-hope-east/hf-cache HF_HUB_ENABLE_HF_TRANSFER=0 HF_HUB_DISABLE_XET=1
export HY3DGEN_MODELS=$NS/weights-extra/hy3dgen CUDA_HOME=/usr/local/cuda
for f in $IN/*.png; do
  n=$(basename "$f" .png)
  [ -n "$FILTER" ] && [ "$n" != "$FILTER" ] && continue
  if [ ! -s "$OUT/shape/$n.glb" ]; then
    echo "=== SHAPE $n ($(date +%H:%M:%S)) ==="
    if python $NS/scripts/rh_shape.py "$REPO" "$f" "$OUT/shape/$n.glb" > $OUT/logs/${n}_shape.log 2>&1; then
      echo "  shape OK  $(stat -c %s $OUT/shape/$n.glb 2>/dev/null) bytes"
    else
      echo "  SHAPE FAILED -> $OUT/logs/${n}_shape.log"; tail -5 $OUT/logs/${n}_shape.log; continue
    fi
  else
    echo "=== SHAPE $n: already present, skipped ==="
  fi
  if [ ! -s "$OUT/painted/$n.glb" ]; then
    echo "=== PAINT $n ($(date +%H:%M:%S)) ==="
    if python $NS/scripts/rh_paint.py "$REPO" "$OUT/shape/$n.glb" "$f" "$OUT/painted/$n.glb" 6 512 > $OUT/logs/${n}_paint.log 2>&1; then
      echo "  paint OK  $(stat -c %s $OUT/painted/$n.glb 2>/dev/null) bytes"
    else
      echo "  PAINT FAILED -> $OUT/logs/${n}_paint.log"; tail -5 $OUT/logs/${n}_paint.log
    fi
  else
    echo "=== PAINT $n: already present, skipped ==="
  fi
done
echo "=== CROPS BATCH COMPLETE $(date +%H:%M:%S) ==="
