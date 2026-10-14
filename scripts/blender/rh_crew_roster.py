"""rh_crew_roster - the whole crew side by side at TRUE scale, one image.

  blender -b -P scripts/blender/rh_crew_roster.py -- --in Martians/gen/crew_mpfb --out <png>
      [--yaw 90] [--rows 2] [--part N] [--px 2400]
  --part N renders only the Nth of --rows chunks, as one row (no overlap).

Imports every <in>/<face>/RH_Walker_<face>.glb in its rest pose, lays the
figures out in rows (crew order of RHCrewVisualizerSubsystem's face list), adds
1.5 m and 2.0 m guide lines and a name + height label under each figure, and
renders one frame with the lineup renderer's fixed key + fill light. The point is the director's brief: different sizes,
proportions from one world, one drawing style - judged together, not one by one.
"""
import json
import math
import os
import sys

import bpy

argv = sys.argv[sys.argv.index("--") + 1:]
SRC = argv[argv.index("--in") + 1]
OUT = argv[argv.index("--out") + 1]
YAW = float(argv[argv.index("--yaw") + 1]) if "--yaw" in argv else 90.0
ROWS = int(argv[argv.index("--rows") + 1]) if "--rows" in argv else 2
PX = int(argv[argv.index("--px") + 1]) if "--px" in argv else 2400

ORDER = ["cmdr_vale", "eng_ruiz", "geo_okafor", "bot_lindqvist", "med_haddad", "tech_park", "pilot_reyes",
         "quart_bello", "res_novak", "fab_stone", "comms_diallo", "hydro_mensah", "reactor_ito", "rookie_shaw",
         "vet_kowalski", "safety_abara", "driver_costa", "survey_khan", "cook_moreau", "xeno_adeyemi"]

bpy.ops.wm.read_factory_settings(use_empty=True)
faces = [f for f in ORDER if os.path.isfile(os.path.join(SRC, f, "RH_Walker_%s.glb" % f))]
if "--part" in argv:            # render one chunk of the roster as a single row
    n = int(argv[argv.index("--part") + 1])
    size = math.ceil(len(faces) / ROWS)
    faces = faces[n * size:(n + 1) * size]
    ROWS = 1
per_row = math.ceil(len(faces) / ROWS)
SPACING, ROW_GAP = 1.15, 3.2
for i, f in enumerate(faces):
    row, col = divmod(i, per_row)
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=os.path.join(SRC, f, "RH_Walker_%s.glb" % f))
    x = (col - (per_row - 1) / 2.0) * SPACING
    for o in set(bpy.data.objects) - before:
        if o.type == "ARMATURE" and o.animation_data:
            o.animation_data.action = None
        if o.parent is None:
            o.location.x += x
            o.location.y += row * ROW_GAP
    rep = os.path.join(SRC, f, "report.json")
    h = json.load(open(rep)).get("height_m") if os.path.isfile(rep) else None
    # upright card 1.2 m in front, raised so both lines clear the floor: the
    # camera's tilt then draws it just under the feet
    bpy.ops.object.text_add(location=(x, row * ROW_GAP - 1.2, 0.13), rotation=(math.radians(90), 0, 0))
    t = bpy.context.object
    t.data.body = f.replace("_", " ") + ("\n%.2f m" % h if h else "")
    t.data.align_x = "CENTER"
    t.data.size = 0.1
sc = bpy.context.scene
bpy.ops.mesh.primitive_plane_add(size=80)
g = bpy.data.materials.new("g"); g.use_nodes = True
g.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.16, 0.16, 0.17, 1)
g.node_tree.nodes["Principled BSDF"].inputs["Roughness"].default_value = 1.0
bpy.context.object.data.materials.append(g)
width = per_row * SPACING
# the guides stand 0.45 m behind the figures and the camera looks down at
# (cam z - target z) / 12 m, so drop them by that much to read true at the heads
lift = 0.45 * (1.6 + ROWS * 1.4 - 0.9) / 12.0
for row in range(ROWS):
    for z in (1.5, 2.0):
        bpy.ops.mesh.primitive_cube_add(size=1, location=(0, row * ROW_GAP + 0.45, z - lift))
        c = bpy.context.object
        c.scale = (width + 0.6, 0.004, 0.004)
for nm, e, rot in (("sun", 3.0, (52, 14, 40)), ("fill", 1.3, (66, -10, -135))):
    l = bpy.data.objects.new(nm, bpy.data.lights.new(nm, "SUN"))
    l.data.energy = e
    l.rotation_euler = tuple(math.radians(r) for r in rot)
    sc.collection.objects.link(l)
sc.world = bpy.data.worlds.new("w")
sc.world.use_nodes = True
sc.world.node_tree.nodes["Background"].inputs[0].default_value = (0.24, 0.24, 0.26, 1)
cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
sc.collection.objects.link(cam)
sc.camera = cam
cam.data.type = "ORTHO"
cam.data.ortho_scale = width + 1.0
ang = math.radians(YAW)
cy = (ROWS - 1) * ROW_GAP / 2.0
cam.location = (12.0 * math.cos(ang), cy - 12.0 * math.sin(ang), 1.6 + ROWS * 1.4)
target = (0.0, cy, 0.9)
from mathutils import Vector  # noqa: E402
cam.rotation_euler = (Vector(target) - cam.location).to_track_quat("-Z", "Y").to_euler()
engines = [i.identifier for i in sc.render.bl_rna.properties["engine"].enum_items]
sc.render.engine = "BLENDER_EEVEE_NEXT" if "BLENDER_EEVEE_NEXT" in engines else "BLENDER_EEVEE"
sc.render.resolution_x = PX
sc.render.resolution_y = int(PX * (0.32 + 0.22 * ROWS))
sc.view_settings.view_transform = "Standard"
sc.render.filepath = OUT
bpy.ops.render.render(write_still=True)
print("ROSTER", len(faces), "figures ->", OUT)
