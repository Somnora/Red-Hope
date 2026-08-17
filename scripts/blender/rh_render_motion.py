"""Render a rigged GLB at chosen animation frames. Blender headless, no GPU.

    blender --background --python scripts/blender/rh_render_motion.py -- \
        <rigged.glb> <out_prefix> <action> <frame,frame,...> [px]

Built for the 2026-08-17 weld test: a skinning defect is invisible in a bind-pose
render and only appears once a joint bends, so the comparison has to be made in
MOTION at identical frames with an identical camera and light rig.
"""
import sys
import math

import bpy
import mathutils

argv = sys.argv[sys.argv.index("--") + 1:]
src, prefix, action_name = argv[0], argv[1], argv[2]
frames = [int(f) for f in argv[3].split(",")]
px = int(argv[4]) if len(argv) > 4 else 700

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=src)

rig = next((o for o in bpy.context.scene.objects if o.type == "ARMATURE"), None)
if rig is None:
    raise SystemExit("no armature in %s" % src)
act = bpy.data.actions.get(action_name)
if act is None:
    raise SystemExit("no action %r; have %s" % (action_name, [a.name for a in bpy.data.actions]))
if rig.animation_data is None:
    rig.animation_data_create()
rig.animation_data.action = act

meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
mn = mathutils.Vector((1e9,) * 3)
mx = mathutils.Vector((-1e9,) * 3)
for o in meshes:
    for c in o.bound_box:
        w = o.matrix_world @ mathutils.Vector(c)
        mn = mathutils.Vector((min(mn[i], w[i]) for i in range(3)))
        mx = mathutils.Vector((max(mx[i], w[i]) for i in range(3)))
ctr = (mn + mx) / 2
size = max((mx - mn)[i] for i in range(3)) or 1.0

cam_d = bpy.data.cameras.new("c")
cam = bpy.data.objects.new("c", cam_d)
bpy.context.scene.collection.objects.link(cam)
bpy.context.scene.camera = cam
# Three-quarter front, slightly low: the view that shows hips, knees and elbows
# bending at once, which is where a rigid nearest-bone bind splits open.
cam.location = ctr + mathutils.Vector((size * 1.05, -size * 1.35, size * 0.15))
d = ctr - cam.location
cam.rotation_euler = d.to_track_quat("-Z", "Y").to_euler()
cam_d.lens = 70

key = bpy.data.lights.new("k", type="SUN")
key.energy = 4.0
ko = bpy.data.objects.new("k", key)
bpy.context.scene.collection.objects.link(ko)
ko.rotation_euler = (math.radians(60), 0, math.radians(35))
fill = bpy.data.lights.new("f", type="SUN")
fill.energy = 1.1
fo = bpy.data.objects.new("f", fill)
bpy.context.scene.collection.objects.link(fo)
fo.rotation_euler = (math.radians(72), 0, math.radians(-135))
w = bpy.data.worlds.new("w")
bpy.context.scene.world = w
w.use_nodes = True
w.node_tree.nodes["Background"].inputs[0].default_value = (0.20, 0.20, 0.22, 1)

sc = bpy.context.scene
sc.render.engine = "BLENDER_EEVEE"
sc.render.resolution_x = sc.render.resolution_y = px
sc.render.film_transparent = False
for f in frames:
    sc.frame_set(f)
    sc.render.filepath = "%s_f%02d.png" % (prefix, f)
    bpy.ops.render.render(write_still=True)
    print("[motion] rendered frame %d" % f, flush=True)
