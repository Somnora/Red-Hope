"""Strip skinning from a GLB, leaving mesh only. Blender headless, no GPU.

    blender --background --python scripts/blender/rh_strip_rig.py -- <in.glb> <out.glb>

For re-rigging a character whose ORIGINAL source mesh is gone and whose only
surviving copy is the already-rigged asset exported back out of UE (the crew's
"missing-8 faces", 2026-08-17). rig_colonist.py joins all MESH objects and binds
a fresh 13-bone armature, so a leftover skin does not merely waste space - the
mesh arrives still parented to the old armature, still carrying an Armature
modifier and a full set of vertex groups whose names COLLIDE with the new rig's
bones (same script, same names). The new bind would then be layered on stale
weights.

So: delete every armature, drop armature modifiers, clear vertex groups, and
apply transforms so the mesh sits in world space exactly where the skinned
version had it. What comes out is a plain posed-at-rest mesh, which is what the
rigger expects.
"""
import sys

import bpy

argv = sys.argv[sys.argv.index("--") + 1:]
src, dst = argv[0], argv[1]

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=src)

meshes = [o for o in bpy.data.objects if o.type == "MESH"]
arms = [o for o in bpy.data.objects if o.type == "ARMATURE"]

for o in meshes:
    # keep world placement when the parent armature disappears
    o.matrix_world = o.matrix_world.copy()
    o.parent = None
    for m in list(o.modifiers):
        if m.type == "ARMATURE":
            o.modifiers.remove(m)
    o.vertex_groups.clear()

bpy.ops.object.select_all(action="DESELECT")
for a in arms:
    a.select_set(True)
if arms:
    bpy.ops.object.delete()

bpy.ops.object.select_all(action="DESELECT")
for o in meshes:
    o.select_set(True)
if meshes:
    bpy.context.view_layer.objects.active = meshes[0]
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)

print("[strip] %d mesh(es) kept, %d armature(s) removed, vertex groups cleared"
      % (len(meshes), len(arms)), flush=True)

bpy.ops.export_scene.gltf(filepath=dst, export_format="GLB")
