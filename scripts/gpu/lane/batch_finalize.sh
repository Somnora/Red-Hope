#!/usr/bin/env bash
# Blender finalize for every *_textured.obj in the batch outbox:
# obj -> glb (embed texture) -> decimate 18k keeping UVs -> textured preview tiles.
set -u
NS=/lambda/nfs/red-hope-east/red_hope
BL=$NS/bin/blender
OUT=$NS/io/batch_out
mkdir -p "$OUT"
for obj in "$NS"/io/outbox/*_textured.obj; do
  [ -e "$obj" ] || continue
  base=$(basename "$obj" _textured.obj)
  echo "=== finalize $base ==="
  "$BL" --background --python "$NS/scripts/obj2glb.py" -- \
    "$obj" "$OUT/${base}_textured.glb" 2>&1 | grep -a "\[obj2glb\]"
  "$BL" --background --python "$NS/scripts/mesh_cleanup.py" -- \
    "$OUT/${base}_textured.glb" "$OUT/${base}_game.glb" 18000 2>&1 | grep -a "\[cleanup\] exported"
  "$BL" --background --python "$NS/scripts/render_preview.py" -- \
    "$OUT/${base}_game.glb" "$OUT/${base}_preview.png" textured 2>&1 | grep -a "\[preview\] rendered"
done
echo "BATCH FINALIZE COMPLETE"
ls -la "$OUT"/*_game.glb 2>/dev/null
