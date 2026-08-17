"""Blender headless: shrink a textured game.glb for a strategic-camera crowd.
Resizes every embedded image to <=1024 and decimates to a lower tri target,
then re-exports. The baked PBR maps are the file-size bulk, so texture resize
is the real win. CPU-only (no GPU) so it runs alongside a GPU gen job.

Usage: blender --background --python optimize_glb.py -- <in.glb> <out.glb> [tris=9000] [tex=1024]
"""
import bpy, sys, os

argv = sys.argv[sys.argv.index("--") + 1:]
src, dst = argv[0], argv[1]
tris = int(argv[2]) if len(argv) > 2 else 9000
texmax = int(argv[3]) if len(argv) > 3 else 1024

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=src)

# --- decimate all meshes to the tri budget (collapse ratio from current count) ---
total = sum(len(o.data.polygons) for o in bpy.data.objects if o.type == "MESH")
if total > tris:
    ratio = max(0.05, tris / float(total))
    for o in bpy.data.objects:
        if o.type == "MESH":
            m = o.modifiers.new("dec", "DECIMATE")
            m.ratio = ratio
            bpy.context.view_layer.objects.active = o
            bpy.ops.object.modifier_apply(modifier=m.name)

# --- downscale every image datablock ---
for img in bpy.data.images:
    if img.size[0] > texmax or img.size[1] > texmax:
        w, h = img.size
        scale = texmax / float(max(w, h))
        img.scale(max(1, int(w * scale)), max(1, int(h * scale)))

os.makedirs(os.path.dirname(dst) or ".", exist_ok=True)
bpy.ops.export_scene.gltf(filepath=dst, export_format="GLB",
                          export_yup=True, export_apply=True)
final = sum(len(o.data.polygons) for o in bpy.data.objects if o.type == "MESH")
print(f"[opt] {os.path.basename(src)} -> {os.path.basename(dst)} ({final} tris, tex<= {texmax})", flush=True)
