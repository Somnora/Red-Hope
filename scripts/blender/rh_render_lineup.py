"""Render a folder of GLBs to QA frames: two views each, one Blender process.

  blender -b -P scripts/blender/rh_render_lineup.py -- \
      --in-dir <glbs> --out-dir <pngs> [--px 640] [--views 35,215]

The batch form of rh_render_sheet.py, for the model-triage lineup that feeds
the keep / fix / remake review (GLBs from scripts/unreal/rh_export_lineup.py,
i.e. the meshes that actually ship). Same instrument on purpose - low
three-quarter camera that sees under the object, ground plane so floaters and
baked plates read, key + fill sun and a lifted world so faces turned away from
the key still show their albedo (the 2026-08-17 lesson: a QA light that makes
good assets look broken invites the wrong fix). Lighting and camera distance are
fixed relative to each object's bounds, so frames are comparable across assets.

Two yaws per asset, front-three-quarter and back-three-quarter, because the
wonky half of a generated mesh is usually the side the reference sheet never
showed. Output: <out-dir>/<stem>__v<yaw>.png, plus <out-dir>/lineup.json with
tris, dimensions (m) and the frame list per asset.
"""
import argparse
import json
import math
import os
import sys

import bpy
import mathutils

argv = sys.argv[sys.argv.index("--") + 1:]
ap = argparse.ArgumentParser()
ap.add_argument("--in-dir", required=True)
ap.add_argument("--out-dir", required=True)
ap.add_argument("--px", type=int, default=640)
ap.add_argument("--views", default="35,215")
a = ap.parse_args(argv)
yaws = [float(v) for v in a.views.split(",")]
os.makedirs(a.out_dir, exist_ok=True)


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    engines = [i.identifier for i in scene.render.bl_rna.properties["engine"].enum_items]
    scene.render.engine = "BLENDER_EEVEE_NEXT" if "BLENDER_EEVEE_NEXT" in engines else "BLENDER_EEVEE"
    scene.render.resolution_x = a.px
    scene.render.resolution_y = a.px
    scene.render.film_transparent = False
    scene.view_settings.view_transform = "Standard"
    return scene


def stage(scene, src):
    bpy.ops.import_scene.gltf(filepath=src)
    meshes = [o for o in scene.objects if o.type == "MESH"]
    if not meshes:
        return None
    deps = bpy.context.evaluated_depsgraph_get()
    coords, tris = [], 0
    for o in meshes:
        ev = o.evaluated_get(deps)
        me = ev.to_mesh()
        coords += [o.matrix_world @ v.co for v in me.vertices]
        me.calc_loop_triangles()
        tris += len(me.loop_triangles)
        ev.to_mesh_clear()
    lo = mathutils.Vector((min(c.x for c in coords), min(c.y for c in coords), min(c.z for c in coords)))
    hi = mathutils.Vector((max(c.x for c in coords), max(c.y for c in coords), max(c.z for c in coords)))
    centre = (lo + hi) / 2
    dims = hi - lo
    size = max(dims) or 1.0

    bpy.ops.mesh.primitive_plane_add(size=size * 60, location=(centre.x, centre.y, lo.z))
    pm = bpy.data.materials.new("ground")
    pm.use_nodes = True
    bsdf = pm.node_tree.nodes["Principled BSDF"]
    bsdf.inputs["Base Color"].default_value = (0.16, 0.16, 0.17, 1)
    # Matte: a glossy ground mirrors the key sun into a hotspot that reads as glare.
    bsdf.inputs["Roughness"].default_value = 1.0
    bpy.context.object.data.materials.append(pm)

    for name, energy, rot in (("sun", 3.2, (52, 14, 40)), ("fill", 1.4, (66, -10, -135))):
        ld = bpy.data.lights.new(name, type="SUN")
        ld.energy = energy
        lo_ = bpy.data.objects.new(name, ld)
        scene.collection.objects.link(lo_)
        lo_.rotation_euler = tuple(math.radians(r) for r in rot)
    scene.world = bpy.data.worlds.new("w")
    scene.world.use_nodes = True
    bg = scene.world.node_tree.nodes["Background"]
    bg.inputs[0].default_value = (0.22, 0.22, 0.24, 1)
    bg.inputs[1].default_value = 1.0

    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    scene.collection.objects.link(cam)
    scene.camera = cam
    return {"centre": centre, "lo": lo, "size": size, "tris": tris,
            "dims_m": [round(d, 3) for d in dims], "cam": cam}


def shoot(scene, st, yaw, dst):
    ang = math.radians(28.0)
    d = st["size"] * 2.0
    c, cam = st["centre"], st["cam"]
    cam.location = (c.x + d * math.cos(math.radians(yaw)) * math.cos(ang),
                    c.y - d * math.sin(math.radians(yaw)) * math.cos(ang),
                    st["lo"].z + d * math.sin(ang))
    cam.rotation_euler = (c - cam.location).to_track_quat("-Z", "Y").to_euler()
    scene.render.filepath = dst
    bpy.ops.render.render(write_still=True)


index = {}
files = sorted(f for f in os.listdir(a.in_dir) if f.lower().endswith(".glb"))
for i, f in enumerate(files, 1):
    stem = f[:-4]
    scene = reset()
    err = "no mesh"
    try:
        st = stage(scene, os.path.join(a.in_dir, f))
    except Exception as e:  # one bad GLB must not sink the batch
        st, err = None, str(e)
    if not st:
        index[stem] = {"error": err}
        print("SKIP %s" % f)
        continue
    frames = []
    for yaw in yaws:
        name = "%s__v%03d.png" % (stem, int(yaw))
        shoot(scene, st, yaw, os.path.join(a.out_dir, name))
        frames.append(name)
    index[stem] = {"tris": st["tris"], "dims_m": st["dims_m"], "frames": frames}
    print("RENDERED %d/%d %s" % (i, len(files), stem))

with open(os.path.join(a.out_dir, "lineup.json"), "w") as fh:
    json.dump(index, fh, indent=1)
print("LINEUP DONE %d assets" % len(index))
