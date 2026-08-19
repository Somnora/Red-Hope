"""Beginner paint kit: export an asset's albedo, a UV-wireframe guide, and a
4-angle preview so the director can repaint in any image editor.

  /Applications/Blender.app/Contents/MacOS/Blender --background \
      --python scripts/blender/rh_paint_kit.py -- <asset.glb> <out_dir>

Outputs into <out_dir>:
  <name>_albedo.png   the paint itself - edit THIS (keep the resolution)
  <name>_uvguide.png  same image with the mesh's UV wireframe drawn over it,
                      so you can see which patch of paint lands where
  <name>_preview.png  4-yaw EEVEE turnaround of the asset as a reference

The UV guide is rasterized in pure numpy/CPU Bresenham - uv.export_layout
needs the GPU module and fails headless (learned 2026-08-18).
Return trip: scripts/unreal/rh_paint_return.py puts the edited albedo back
onto the exact texture asset the model's material actually binds.

RECREATED 2026-08-18: the original was written to a session scratchpad and
referenced by commit 1f68ffe's message without ever being added to git.
Scripts the artifact/docs point at belong IN the repo - that is the lesson.
"""
import os
import sys

import bpy
import numpy as np

argv = sys.argv[sys.argv.index("--") + 1:]
GLB, OUT = os.path.abspath(argv[0]), os.path.abspath(argv[1])
NAME = os.path.splitext(os.path.basename(GLB))[0]
os.makedirs(OUT, exist_ok=True)

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=GLB)
meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
if not meshes:
    raise SystemExit("no meshes in %s" % GLB)

# ---- albedo: follow the Principled BSDF's Base Color link to its image ----
albedo_img = None
for ob in meshes:
    for slot in ob.material_slots:
        mat = slot.material
        if not (mat and mat.use_nodes):
            continue
        for node in mat.node_tree.nodes:
            if node.type == "BSDF_PRINCIPLED":
                for link in mat.node_tree.links:
                    if (link.to_node == node and link.to_socket.name == "Base Color"
                            and link.from_node.type == "TEX_IMAGE" and link.from_node.image):
                        albedo_img = link.from_node.image
        if albedo_img:
            break
    if albedo_img:
        break
if not albedo_img:
    raise SystemExit("no Base Color image found")

albedo_path = os.path.join(OUT, "%s_albedo.png" % NAME)
albedo_img.file_format = "PNG"
albedo_img.filepath_raw = albedo_path
albedo_img.save()
W, H = albedo_img.size
print("albedo: %s (%dx%d)" % (albedo_path, W, H))

# ---- UV guide: dim the albedo, draw every polygon's UV loop in yellow ----
px = np.array(albedo_img.pixels[:], dtype=np.float32).reshape(H, W, 4)
guide = px.copy()
guide[..., :3] *= 0.45
mask = np.zeros((H, W), dtype=bool)


def draw_line(u0, v0, u1, v1):
    x0, y0 = int(u0 * (W - 1)), int((1 - v0) * (H - 1))
    x1, y1 = int(u1 * (W - 1)), int((1 - v1) * (H - 1))
    dx, dy = abs(x1 - x0), -abs(y1 - y0)
    sx, sy = (1 if x0 < x1 else -1), (1 if y0 < y1 else -1)
    err = dx + dy
    while True:
        if 0 <= x0 < W and 0 <= y0 < H:
            mask[y0, x0] = True
        if x0 == x1 and y0 == y1:
            break
        e2 = 2 * err
        if e2 >= dy:
            err += dy
            x0 += sx
        if e2 <= dx:
            err += dx
            y0 += sy


for ob in meshes:
    me = ob.data
    uvs = me.uv_layers.active
    if not uvs:
        continue
    for poly in me.polygons:
        loops = list(poly.loop_indices)
        for i, li in enumerate(loops):
            a = uvs.data[li].uv
            b = uvs.data[loops[(i + 1) % len(loops)]].uv
            draw_line(a.x, a.y, b.x, b.y)

guide[mask, 0] = np.minimum(1.0, guide[mask, 0] * 0.35 + 0.65)
guide[mask, 1] = np.minimum(1.0, guide[mask, 1] * 0.35 + 0.60)
guide[mask, 2] *= 0.25
guide[..., 3] = 1.0
gimg = bpy.data.images.new("%s_uvguide" % NAME, width=W, height=H, alpha=True)
gimg.pixels = guide.ravel().tolist()
guide_path = os.path.join(OUT, "%s_uvguide.png" % NAME)
gimg.file_format = "PNG"
gimg.filepath_raw = guide_path
gimg.save()
print("uvguide: %s" % guide_path)

# ---- preview: EEVEE 4-yaw turnaround, stitched horizontally ----
scene = bpy.context.scene
for eng in ("BLENDER_EEVEE", "BLENDER_EEVEE_NEXT"):
    try:
        scene.render.engine = eng
        break
    except TypeError:
        continue
SIDE = 768
scene.render.resolution_x = scene.render.resolution_y = SIDE
scene.render.film_transparent = False

world = bpy.data.worlds.new("kit_world")
world.use_nodes = True
world.node_tree.nodes["Background"].inputs[0].default_value = (0.55, 0.55, 0.55, 1.0)
world.node_tree.nodes["Background"].inputs[1].default_value = 1.0
scene.world = world

xs, ys, zs = [], [], []
for ob in meshes:
    for corner in ob.bound_box:
        wc = ob.matrix_world @ __import__("mathutils").Vector(corner)
        xs.append(wc.x)
        ys.append(wc.y)
        zs.append(wc.z)
center = ((max(xs) + min(xs)) / 2, (max(ys) + min(ys)) / 2, (max(zs) + min(zs)) / 2)
span = max(max(xs) - min(xs), max(ys) - min(ys), max(zs) - min(zs))
dist = span * 2.1

tgt = bpy.data.objects.new("kit_target", None)
tgt.location = center
scene.collection.objects.link(tgt)
cam = bpy.data.objects.new("kit_cam", bpy.data.cameras.new("kit_cam"))
scene.collection.objects.link(cam)
scene.camera = cam
tr = cam.constraints.new("TRACK_TO")
tr.target = tgt
sun = bpy.data.objects.new("kit_sun", bpy.data.lights.new("kit_sun", "SUN"))
sun.data.energy = 3.5
sun.rotation_euler = (0.9, 0.2, 0.6)
scene.collection.objects.link(sun)

import math
tiles = []
for i, yaw in enumerate((0, 90, 180, 270)):
    a = math.radians(yaw)
    cam.location = (center[0] + dist * math.sin(a),
                    center[1] - dist * math.cos(a),
                    center[2] + dist * 0.45)
    fp = os.path.join(OUT, "_kit_yaw%d.png" % i)
    scene.render.filepath = fp
    bpy.ops.render.render(write_still=True)
    img = bpy.data.images.load(fp)
    tiles.append(np.array(img.pixels[:], dtype=np.float32).reshape(SIDE, SIDE, 4))
    bpy.data.images.remove(img)
    os.remove(fp)

sheet = np.concatenate(tiles, axis=1)
simg = bpy.data.images.new("%s_preview" % NAME, width=SIDE * 4, height=SIDE, alpha=True)
simg.pixels = sheet.ravel().tolist()
prev_path = os.path.join(OUT, "%s_preview.png" % NAME)
simg.file_format = "PNG"
simg.filepath_raw = prev_path
simg.save()
print("preview: %s" % prev_path)
print("PAINT_KIT_DONE %s" % NAME)
