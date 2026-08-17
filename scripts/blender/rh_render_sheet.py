"""Render a GLB to a PNG for asset QA. Textured, three-quarter, neutral studio.

  blender -b -P scripts/blender/rh_render_sheet.py -- --in a.glb --out a.png [--px 480]

Judge assets from FRAMES, not from statistics over their atlases. That rule was
learned twice on this project: a planter was ranked defective from its texture
histogram and turned out fine, and a "white-splotch" score on the crew turned out
to be measuring how much white clothing a character wore.

Deliberately EEVEE and a fixed low-ish sun: Cycles spends minutes compiling
kernels on a cold machine, and a QA contact sheet does not need path tracing. The
light is fixed so a batch is comparable frame to frame - the one thing that has
repeatedly cost a wrong diagnosis here is an A/B where the lighting moved.

The camera sits low enough to see UNDER the object, because that is where a baked
ground plate hides.
"""
import argparse
import math
import sys

import bpy
import mathutils

argv = sys.argv[sys.argv.index("--") + 1:]
ap = argparse.ArgumentParser()
ap.add_argument("--in", dest="src", required=True)
ap.add_argument("--out", dest="dst", required=True)
ap.add_argument("--px", type=int, default=480)
a = ap.parse_args(argv)

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=a.src)

meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
if not meshes:
    sys.exit("no mesh in %s" % a.src)
bpy.ops.object.select_all(action="DESELECT")
for o in meshes:
    o.select_set(True)
bpy.context.view_layer.objects.active = meshes[0]
if len(meshes) > 1:
    bpy.ops.object.join()
obj = bpy.context.view_layer.objects.active

bpy.context.view_layer.update()
coords = [obj.matrix_world @ v.co for v in obj.data.vertices]
xs = [c.x for c in coords]; ys = [c.y for c in coords]; zs = [c.z for c in coords]
centre = mathutils.Vector(((min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2,
                           (min(zs) + max(zs)) / 2))
size = max(max(xs) - min(xs), max(ys) - min(ys), max(zs) - min(zs)) or 1.0

# Ground plane, so a floating object or a baked plate reads immediately.
bpy.ops.mesh.primitive_plane_add(size=size * 12,
                                 location=(centre.x, centre.y, min(zs)))
plane = bpy.context.object
pm = bpy.data.materials.new("ground")
pm.use_nodes = True
pm.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.16, 0.16, 0.17, 1)
plane.data.materials.append(pm)

cam_data = bpy.data.cameras.new("cam")
cam = bpy.data.objects.new("cam", cam_data)
bpy.context.scene.collection.objects.link(cam)
# Low three-quarter: 28 degrees above the horizon sees under a worktop.
ang = math.radians(28.0)
d = size * 2.6
cam.location = (centre.x + d * math.cos(math.radians(35)),
                centre.y - d * math.sin(math.radians(35)),
                min(zs) + d * math.sin(ang))
direction = centre - cam.location
cam.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()
bpy.context.scene.camera = cam

sun_data = bpy.data.lights.new("sun", type="SUN")
sun_data.energy = 3.2
sun = bpy.data.objects.new("sun", sun_data)
bpy.context.scene.collection.objects.link(sun)
sun.rotation_euler = (math.radians(52), math.radians(14), math.radians(40))

bpy.context.scene.world = bpy.data.worlds.new("w")
bpy.context.scene.world.use_nodes = True
bpy.context.scene.world.node_tree.nodes["Background"].inputs[0].default_value = (0.05, 0.05, 0.06, 1)
bpy.context.scene.world.node_tree.nodes["Background"].inputs[1].default_value = 1.0

scene = bpy.context.scene
scene.render.engine = "BLENDER_EEVEE_NEXT" if "BLENDER_EEVEE_NEXT" in \
    [i.identifier for i in scene.render.bl_rna.properties["engine"].enum_items] else "BLENDER_EEVEE"
scene.render.resolution_x = a.px
scene.render.resolution_y = a.px
scene.render.film_transparent = False
scene.render.filepath = a.dst
bpy.ops.render.render(write_still=True)
print("RENDERED %s" % a.dst)
