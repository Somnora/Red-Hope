"""Blender headless turntable preview of a mesh -> PNG contact sheet (3 angles).
Run: blender --background --python render_preview.py -- <mesh.glb> <out.png>
Neutral studio light + clay material so geometry/silhouette reads clearly."""
import bpy, sys, math, mathutils

argv = sys.argv[sys.argv.index("--") + 1:]
in_glb, out_png = argv[0], argv[1]
keep_material = len(argv) > 2 and argv[2] == "textured"  # keep baked texture vs clay

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=in_glb)
meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
bpy.ops.object.select_all(action="DESELECT")
for o in meshes: o.select_set(True)
bpy.context.view_layer.objects.active = meshes[0]
if len(meshes) > 1: bpy.ops.object.join()
obj = bpy.context.view_layer.objects.active

# frame the object
bpy.context.view_layer.update()
coords = [obj.matrix_world @ v.co for v in obj.data.vertices]
xs=[c.x for c in coords]; ys=[c.y for c in coords]; zs=[c.z for c in coords]
center = mathutils.Vector(((min(xs)+max(xs))/2,(min(ys)+max(ys))/2,(min(zs)+max(zs))/2))
size = max(max(xs)-min(xs), max(ys)-min(ys), max(zs)-min(zs)) or 1.0

# clay material so untextured geometry reads (skip when previewing the baked texture)
if not keep_material:
    mat = bpy.data.materials.new("clay"); mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get("Principled BSDF")
    bsdf.inputs["Base Color"].default_value = (0.8,0.8,0.82,1)
    bsdf.inputs["Roughness"].default_value = 0.55
    obj.data.materials.clear(); obj.data.materials.append(mat)

# world light
world = bpy.data.worlds.new("w"); bpy.context.scene.world = world
world.use_nodes = True
world.node_tree.nodes["Background"].inputs["Strength"].default_value = 1.0
# key light
sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun","SUN"))
sun.data.energy = 4.0; sun.rotation_euler = (math.radians(55), 0, math.radians(35))
bpy.context.scene.collection.objects.link(sun)

# camera
cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
bpy.context.scene.collection.objects.link(cam)
bpy.context.scene.camera = cam

scene = bpy.context.scene
# Cycles + CUDA: renders reliably headless (EEVEE needs a GL context this box lacks).
scene.render.engine = "CYCLES"
prefs = bpy.context.preferences.addons["cycles"].preferences
prefs.compute_device_type = "CUDA"
prefs.get_devices()
for d in prefs.devices:
    d.use = d.type in ("CUDA", "OPTIX")
scene.cycles.device = "GPU"
scene.cycles.samples = 48
scene.render.film_transparent = False
scene.render.resolution_x = 512; scene.render.resolution_y = 768

angles = [("front",0),("three_quarter",40),("side",90)]
tiles = []
for name, deg in angles:
    a = math.radians(deg)
    dist = size*2.2
    cam.location = center + mathutils.Vector((math.sin(a)*dist, -math.cos(a)*dist, size*0.35))
    d = (center - cam.location).normalized()
    cam.rotation_euler = d.to_track_quat("-Z","Y").to_euler()
    p = out_png.replace(".png", f"_{name}.png")
    scene.render.filepath = p
    bpy.ops.render.render(write_still=True)
    tiles.append(p)
    print(f"[preview] rendered {name} -> {p}", flush=True)

# stitch the 3 tiles side by side
try:
    from PIL import Image
    imgs = [Image.open(t) for t in tiles]
    W = sum(i.width for i in imgs); H = max(i.height for i in imgs)
    sheet = Image.new("RGB",(W,H),(30,30,34))
    x=0
    for im in imgs: sheet.paste(im,(x,0)); x+=im.width
    sheet.save(out_png)
    print(f"[preview] contact sheet -> {out_png} ({W}x{H})", flush=True)
except Exception as e:
    print(f"[preview] stitch skipped ({e}); tiles at {tiles}", flush=True)
