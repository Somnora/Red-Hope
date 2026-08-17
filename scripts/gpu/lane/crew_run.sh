#!/usr/bin/env bash
set -u
NS=/lambda/nfs/red-hope-east/red_hope
REPO=$NS/repos/Hunyuan3D-2.1
BL=$NS/bin/blender
IN=$NS/io/crew_in
OUT=$NS/io/crew_out
source $HOME/rh3d-hy3d/bin/activate
export RH3D_NS=$NS
export HF_HOME=/lambda/nfs/red-hope-east/hf-cache HF_HUB_ENABLE_HF_TRANSFER=0 HF_HUB_DISABLE_XET=1
export HY3DGEN_MODELS=$NS/weights-extra/hy3dgen CUDA_HOME=/usr/local/cuda
mkdir -p "$OUT"
echo "[crew] shape+paint start $(date)"
python $NS/scripts/rh_batch.py "$REPO" "$IN" "$OUT"
echo "[crew] finalize start $(date)"
for obj in "$OUT"/*_textured.obj; do
  [ -e "$obj" ] || continue
  base=$(basename "$obj" _textured.obj)
  echo "[crew] finalize $base"
  "$BL" --background --python "$NS/scripts/obj2glb.py"      -- "$obj" "$OUT/${base}_textured.glb"       2>&1 | grep -a "\[obj2glb\]"
  "$BL" --background --python "$NS/scripts/mesh_cleanup.py" -- "$OUT/${base}_textured.glb" "$OUT/${base}_game.glb" 18000 2>&1 | grep -a "\[cleanup\]"
  "$BL" --background --python "$NS/scripts/render_preview.py" -- "$OUT/${base}_game.glb" "$OUT/${base}_preview.png" textured 2>&1 | grep -a "\[preview\]"
done
echo "[crew] COMPLETE $(date)"
ls -la "$OUT"/*_game.glb 2>/dev/null
