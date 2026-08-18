"""Decimate a GLB to a target triangle count. Blender headless, no GPU.

    blender --background --python scripts/blender/rh_decimate.py -- <in.glb> <out.glb> <target_tris>

Must run BEFORE rigging. Decimating an already-skinned mesh destroys the weight
distribution the bind computed - the collapse picks arbitrary survivors - so the
order is always decimate, then weld, then bind.
"""
import sys

import bpy

argv = sys.argv[sys.argv.index("--") + 1:]
src, dst, target = argv[0], argv[1], int(argv[2])

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
body.data.calc_loop_triangles()
before = len(body.data.loop_triangles)

if before > target:
    m = body.modifiers.new("dec", "DECIMATE")
    m.decimate_type = "COLLAPSE"
    m.ratio = float(target) / float(before)
    bpy.ops.object.modifier_apply(modifier=m.name)
body.data.calc_loop_triangles()
after = len(body.data.loop_triangles)
print("[decimate] %d -> %d tris (target %d)" % (before, after, target), flush=True)

bpy.ops.export_scene.gltf(filepath=dst, export_format="GLB")
