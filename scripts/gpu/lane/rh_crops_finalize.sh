#!/usr/bin/env bash
# Painted OBJ -> GLB -> decimated game-ready GLB -> textured preview PNG.
# The paint stage emits OBJ+MTL+PBR jpgs, not GLB, so the conversion is a real
# step and not a formality. Resume-safe at every stage.
# $1 = triangle budget (art bible: ~8k for props).
set -u
NS=/lambda/nfs/red-hope-east/red_hope
B=$NS/bin/blender
P=$NS/io/crops_out/painted
G=$NS/io/crops_out/glb
F=$NS/io/crops_out/final
V=$NS/io/crops_out/preview
L=$NS/io/crops_out/logs
mkdir -p $G $F $V $L
TRIS="${1:-8000}"
for o in $P/*.obj; do
  n=$(basename "$o" .obj)
  [ -s "$G/$n.glb" ] || $B --background --python $NS/scripts/obj2glb.py -- "$o" "$G/$n.glb" > $L/${n}_obj2glb.log 2>&1
  [ -s "$F/$n.glb" ] || $B --background --python $NS/scripts/mesh_cleanup.py -- "$G/$n.glb" "$F/$n.glb" $TRIS > $L/${n}_cleanup.log 2>&1
  [ -s "$V/$n.png" ] || $B --background --python $NS/scripts/render_preview.py -- "$F/$n.glb" "$V/$n.png" textured > $L/${n}_preview.log 2>&1
  printf "%-14s glb=%-9s final=%-9s png=%s\n" "$n" \
    "$(stat -c %s $G/$n.glb 2>/dev/null || echo MISSING)" \
    "$(stat -c %s $F/$n.glb 2>/dev/null || echo MISSING)" \
    "$(stat -c %s $V/$n.png 2>/dev/null || echo MISSING)"
done
echo "=== FINALIZE COMPLETE $(date +%H:%M:%S) ==="
