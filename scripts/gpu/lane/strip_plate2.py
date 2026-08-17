"""Blender headless: remove a FUSED floor plate by bisecting just above it.
For props whose plate is welded to the legs (loose-part separation can't drop
it): cut the whole mesh at z = zmin + frac*H and delete everything below. Leg
bottoms become flat cuts sitting on the floor - invisible at game scale.

Usage: blender --background --python strip_plate2.py -- <in.glb> <out.glb> [frac=0.035] [tris=6000]
"""
import bpy, sys
from mathutils import Vector

argv = sys.argv[sys.argv.index("--") + 1:]
src, dst = argv[0], argv[1]
frac = float(argv[2]) if len(argv) > 2 else 0.035
tris = int(argv[3]) if len(argv) > 3 else 6000

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=src)
meshes = [o for o in bpy.data.objects if o.type == "MESH"]
bpy.ops.object.select_all(action="DESELECT")
for o in meshes:
    o.select_set(True)
bpy.context.view_layer.objects.active = meshes[0]
if len(meshes) > 1:
    bpy.ops.object.join()
body = bpy.context.view_layer.objects.active
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)

bb = [body.matrix_world @ Vector(c) for c in body.bound_box]
zmin = min(v.z for v in bb); zmax = max(v.z for v in bb)
cut_z = zmin + (zmax - zmin) * frac

bpy.ops.object.mode_set(mode="EDIT")
bpy.ops.mesh.select_all(action="SELECT")
bpy.ops.mesh.bisect(plane_co=(0, 0, cut_z), plane_no=(0, 0, 1),
                    clear_inner=True, use_fill=False)
bpy.ops.object.mode_set(mode="OBJECT")

# re-seat on Z0
bb2 = [body.matrix_world @ Vector(c) for c in body.bound_box]
body.location.z -= min(v.z for v in bb2)
bpy.ops.object.transform_apply(location=True)

total = len(body.data.polygons)
if total > tris:
    m = body.modifiers.new("dec", "DECIMATE")
    m.ratio = max(0.05, tris / float(total))
    bpy.ops.object.modifier_apply(modifier=m.name)

bpy.ops.export_scene.gltf(filepath=dst, export_format="GLB", export_yup=True, export_apply=True)
print(f"[strip2] cut at {frac:.3f}H -> {dst} ({len(body.data.polygons)} tris)", flush=True)
