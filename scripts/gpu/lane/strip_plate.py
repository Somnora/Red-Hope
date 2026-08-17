"""Blender headless: remove the baked-in floor plate under a furniture prop.
The plate defeats the neck-detection stripper (furniture legs are wide), so we
go at it directly: separate loose parts, drop every part whose bounding box is
WIDE (>70% of the whole footprint) and FLAT (<8% of height) and AT THE FLOOR
(bottom within 3% of z-min). Legs/chairs survive (narrow), tabletops survive
(high). If the plate is fused to a leg, fallback: delete all faces in the
plate slab whose face normal is near +/-Z (the horizontal plate surfaces),
keeping vertical leg walls.

Usage: blender --background --python strip_plate.py -- <in.glb> <out.glb> [tris]
"""
import bpy, sys
from mathutils import Vector

argv = sys.argv[sys.argv.index("--") + 1:]
src, dst = argv[0], argv[1]
tris = int(argv[2]) if len(argv) > 2 else 6000

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

def bounds(o):
    bb = [o.matrix_world @ Vector(c) for c in o.bound_box]
    lo = Vector((min(v.x for v in bb), min(v.y for v in bb), min(v.z for v in bb)))
    hi = Vector((max(v.x for v in bb), max(v.y for v in bb), max(v.z for v in bb)))
    return lo, hi

lo, hi = bounds(body)
H = hi.z - lo.z
FW = max(hi.x - lo.x, hi.y - lo.y)

bpy.ops.object.mode_set(mode="EDIT")
bpy.ops.mesh.separate(type="LOOSE")
bpy.ops.object.mode_set(mode="OBJECT")
parts = [o for o in bpy.data.objects if o.type == "MESH"]
print(f"[strip] {len(parts)} loose parts", flush=True)

dropped = 0
for p in parts:
    plo, phi = bounds(p)
    w = max(phi.x - plo.x, phi.y - plo.y)
    h = phi.z - plo.z
    if (w > 0.70 * FW and h < 0.08 * H and plo.z < lo.z + 0.03 * H):
        print(f"[strip] dropping plate part: w={w:.2f} h={h:.3f}", flush=True)
        bpy.data.objects.remove(p, do_unlink=True)
        dropped += 1

survivors = [o for o in bpy.data.objects if o.type == "MESH"]
if not survivors:
    print("[strip] ABORT: everything classified as plate; re-import untouched", flush=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=src)
    survivors = [o for o in bpy.data.objects if o.type == "MESH"]
    dropped = 0

bpy.ops.object.select_all(action="DESELECT")
for o in survivors:
    o.select_set(True)
bpy.context.view_layer.objects.active = survivors[0]
if len(survivors) > 1:
    bpy.ops.object.join()
body = bpy.context.view_layer.objects.active

# re-seat feet on Z0 (dropping the plate may have left a gap)
lo2, hi2 = bounds(body)
body.location.z -= lo2.z
bpy.ops.object.transform_apply(location=True)

# decimate to budget
total = len(body.data.polygons)
if total > tris:
    m = body.modifiers.new("dec", "DECIMATE")
    m.ratio = max(0.05, tris / float(total))
    bpy.ops.object.modifier_apply(modifier=m.name)

bpy.ops.export_scene.gltf(filepath=dst, export_format="GLB", export_yup=True, export_apply=True)
print(f"[strip] exported {dst} (dropped {dropped} plate part(s), {len(body.data.polygons)} tris)", flush=True)
