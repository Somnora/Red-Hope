"""rh_crew_preview - contact sheet for a built walker: rest pose + clip frames.

  blender -b Martians/gen/crew_mpfb/<face>/RH_Walker_<face>.blend \
      -P scripts/blender/rh_crew_preview.py -- --out <dir> [--px 420]

Renders (EEVEE, the lineup renderer's fixed key+fill light) the rest pose from
the front and three-quarter, then Walk frames 1/7/13/19 from the side, and one
frame each of Idle, WorkBench, WorkLab, Dig and Plant from the three-quarter,
into <out>/<name>__<label>.png. Judge motion from these frames, not from the
keyframe tables.
"""
import math
import os
import sys

import bpy
import mathutils

argv = sys.argv[sys.argv.index("--") + 1:]
OUT = argv[argv.index("--out") + 1]
PX = int(argv[argv.index("--px") + 1]) if "--px" in argv else 420
os.makedirs(OUT, exist_ok=True)
sc = bpy.context.scene
rig = next(o for o in sc.objects if o.type == "ARMATURE")
body = next(o for o in sc.objects if o.type == "MESH")
name = body.name

engines = [i.identifier for i in sc.render.bl_rna.properties["engine"].enum_items]
sc.render.engine = "BLENDER_EEVEE_NEXT" if "BLENDER_EEVEE_NEXT" in engines else "BLENDER_EEVEE"
sc.render.resolution_x, sc.render.resolution_y = PX, int(PX * 1.5)
sc.view_settings.view_transform = "Standard"
for nm, e, rot in (("sun", 3.0, (52, 14, 40)), ("fill", 1.3, (66, -10, -135))):
    l = bpy.data.objects.new(nm, bpy.data.lights.new(nm, "SUN"))
    l.data.energy = e
    l.rotation_euler = tuple(math.radians(r) for r in rot)
    sc.collection.objects.link(l)
sc.world = bpy.data.worlds.new("w")
sc.world.use_nodes = True
sc.world.node_tree.nodes["Background"].inputs[0].default_value = (0.24, 0.24, 0.26, 1)
bpy.ops.mesh.primitive_plane_add(size=20, location=(0, 0, 0))
g = bpy.data.materials.new("g"); g.use_nodes = True
g.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.16, 0.16, 0.17, 1)
g.node_tree.nodes["Principled BSDF"].inputs["Roughness"].default_value = 1.0
bpy.context.object.data.materials.append(g)
cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
sc.collection.objects.link(cam)
sc.camera = cam
H = body.dimensions.z


def shoot(label, yaw, action=None, frame=1, elev=8.0, dist=3.4):
    if action:
        rig.animation_data.action = bpy.data.actions[action]
    else:
        rig.animation_data.action = None
        for pb in rig.pose.bones:
            pb.rotation_quaternion = (1, 0, 0, 0)
            pb.location = (0, 0, 0)
    sc.frame_set(frame)
    c = mathutils.Vector((0, 0, H * 0.52))
    e = math.radians(elev)
    cam.location = (c.x + dist * math.cos(math.radians(yaw)) * math.cos(e),
                    c.y - dist * math.sin(math.radians(yaw)) * math.cos(e),
                    c.z + dist * math.sin(e))
    cam.rotation_euler = (c - cam.location).to_track_quat("-Z", "Y").to_euler()
    sc.render.filepath = os.path.join(OUT, "%s__%s.png" % (name, label))
    bpy.ops.render.render(write_still=True)


shoot("rest_front", 90)
shoot("rest_34", 60)
for f in (1, 7, 13, 19):
    shoot("walk_f%02d" % f, 0, "Walk", f)
for act, f in (("Idle", 25), ("WorkBench", 7), ("WorkLab", 13), ("Dig", 9), ("Plant", 9)):
    shoot(act.lower(), 60, act, f)
print("PREVIEW done", OUT)
