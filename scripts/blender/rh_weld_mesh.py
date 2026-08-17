"""Weld a GLB's coincident vertices. Blender headless, no GPU.

    blender --background --python scripts/blender/rh_weld_mesh.py -- <in.glb> <out.glb> [dist]

WHY: the Hunyuan crew meshes ship at 18,000 tris carrying ~53,860 vertices -
2.99 verts per triangle, i.e. every triangle owning its own three vertices and
sharing none. That is a live suspect for the director's "you can see through
parts of their body" during MOTION, because rig_colonist.py's bind step has a
nearest-bone backstop that hard-assigns any vertex the heat solver leaves
weightless: two COINCIDENT vertices can therefore receive different treatment -
one smoothly blended across a joint, its twin pinned 100% to one bone - and
they then travel to different places as the armature moves. The surface splits
along that seam. Welding removes the duplicates, so there is nothing to diverge.

Safe for UVs and shading: Blender stores UVs and custom split normals PER LOOP
(face corner), not per vertex, so merging coincident vertices preserves UV
islands and any authored hard edges. The merge distance is deliberately tiny -
this joins vertices that are already at the same position, it does not simplify.
"""
import sys

import bpy

argv = sys.argv[sys.argv.index("--") + 1:]
src, dst = argv[0], argv[1]
dist = float(argv[2]) if len(argv) > 2 else 0.00005

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=src)

before_v = before_t = 0
after_v = after_t = 0
for o in [o for o in bpy.context.scene.objects if o.type == "MESH"]:
    before_v += len(o.data.vertices)
    before_t += len(o.data.loop_triangles) or sum(len(p.vertices) - 2 for p in o.data.polygons)
    bpy.context.view_layer.objects.active = o
    o.select_set(True)
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.mesh.remove_doubles(threshold=dist)
    bpy.ops.object.mode_set(mode="OBJECT")
    o.data.calc_loop_triangles()
    after_v += len(o.data.vertices)
    after_t += len(o.data.loop_triangles)
    o.select_set(False)

print("[weld] verts %d -> %d (%.1f%% removed), tris %d -> %d"
      % (before_v, after_v, 100.0 * (before_v - after_v) / max(before_v, 1), before_t, after_t),
      flush=True)
print("[weld] v/t %.2f -> %.2f" % (before_v / max(before_t, 1), after_v / max(after_t, 1)), flush=True)

bpy.ops.export_scene.gltf(filepath=dst, export_format="GLB")
