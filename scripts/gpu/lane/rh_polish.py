# Headless Blender polish pass for Red Hope game GLBs.
# - Weld exact-duplicate vertices (distance 1e-6: only the split verts the
#   pipeline's flat-shaded export produced; UVs live on loops and survive).
# - Smooth shading by angle (30 deg) so curved hulls light smoothly while
#   hard edges stay crisp.
# - Renders a before/after preview pair (same camera) and exports
#   <name>_polished.glb. Geometry/UVs/textures are otherwise untouched.
# Usage: blender -b --python rh_polish.py -- input.glb out_dir
import bpy, sys, os, json, math
from mathutils import Vector

argv = sys.argv[sys.argv.index("--") + 1:]
glb_path, out_dir = argv[0], argv[1]
name = os.path.basename(glb_path).replace("_game.glb", "").replace(".glb", "")
os.makedirs(os.path.join(out_dir, "polished"), exist_ok=True)
os.makedirs(os.path.join(out_dir, "previews"), exist_ok=True)

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=glb_path)
meshes = [o for o in bpy.data.objects if o.type == 'MESH']

def stats():
    v = sum(len(o.data.vertices) for o in meshes)
    t = 0
    for o in meshes:
        o.data.calc_loop_triangles()
        t += len(o.data.loop_triangles)
    return v, t

def setup_render(tag):
    scene = bpy.context.scene
    # bounds of everything
    mins = Vector((1e9,) * 3); maxs = Vector((-1e9,) * 3)
    for o in meshes:
        for c in o.bound_box:
            w = o.matrix_world @ Vector(c)
            mins = Vector(map(min, mins, w)); maxs = Vector(map(max, maxs, w))
    center = (mins + maxs) / 2
    diag = (maxs - mins).length or 1.0
    # camera: 3/4 view
    cam_data = bpy.data.cameras.new("Cam")
    cam = bpy.data.objects.new("Cam", cam_data)
    scene.collection.objects.link(cam)
    off = Vector((1.0, -1.2, 0.7)).normalized() * diag * 1.6
    cam.location = center + off
    d = center - cam.location
    cam.rotation_euler = d.to_track_quat('-Z', 'Y').to_euler()
    scene.camera = cam
    # light: sun + warm ambient
    sun_data = bpy.data.lights.new("Sun", 'SUN')
    sun_data.energy = 4.0
    sun = bpy.data.objects.new("Sun", sun_data)
    scene.collection.objects.link(sun)
    sun.rotation_euler = (math.radians(50), math.radians(15), math.radians(30))
    world = bpy.data.worlds.new("W")
    world.use_nodes = True
    bg = world.node_tree.nodes["Background"]
    bg.inputs[0].default_value = (0.35, 0.28, 0.24, 1.0)  # warm mars ambient
    bg.inputs[1].default_value = 0.6
    scene.world = world
    scene.render.engine = 'CYCLES'
    scene.cycles.samples = 24
    prefs = bpy.context.preferences.addons.get('cycles')
    if prefs:
        cp = prefs.preferences
        try:
            cp.compute_device_type = 'CUDA'
            cp.get_devices()
            for dev in cp.devices:
                dev.use = True
            scene.cycles.device = 'GPU'
        except Exception:
            scene.cycles.device = 'CPU'
    scene.render.resolution_x = 512
    scene.render.resolution_y = 512
    scene.render.filepath = os.path.join(out_dir, "previews", f"{name}_{tag}.png")
    bpy.ops.render.render(write_still=True)
    # cleanup rig for next pass
    bpy.data.objects.remove(cam); bpy.data.objects.remove(sun)

v0, t0 = stats()
setup_render("before")

# --- the polish ---
for o in meshes:
    bpy.context.view_layer.objects.active = o
    o.select_set(True)
bpy.ops.object.mode_set(mode='EDIT')
bpy.ops.mesh.select_all(action='SELECT')
bpy.ops.mesh.remove_doubles(threshold=0.000001)  # exact duplicates only
bpy.ops.object.mode_set(mode='OBJECT')
try:
    bpy.ops.object.shade_auto_smooth(angle=math.radians(30))
except Exception:
    try:
        bpy.ops.object.shade_smooth_by_angle(angle=math.radians(30))
    except Exception:
        bpy.ops.object.shade_smooth()

v1, t1 = stats()
setup_render("after")

out_glb = os.path.join(out_dir, "polished", f"{name}_polished.glb")
bpy.ops.export_scene.gltf(filepath=out_glb, export_format='GLB')

print("POLISH " + json.dumps({
    "file": os.path.basename(glb_path), "verts_before": v0, "verts_after": v1,
    "tris_before": t0, "tris_after": t1,
    "out": os.path.basename(out_glb),
}))
