"""Blender headless: auto-rig a Hunyuan A-pose colonist and bake Walk + Idle
animation clips, exporting a skinned GLB that UE Interchange imports as a
SkeletalMesh with AnimSequences.

The meshes are clean standing bipeds (feet at Z0, facing +X after our import
convention... actually facing is unknown; the walk plays in place and the crew
visualizer yaws the actor, so facing only affects which axis legs swing on -
we detect the facing axis as the mesh's SMALLER horizontal extent = depth).

Armature: 13 bones fit procedurally from bounds proportions (pelvis, spine,
chest, head, thigh/shin/foot L+R, upperarm/forearm L+R). Automatic weights,
envelope fallback. Walk = 24f loop of thigh/shin/arm swings + pelvis bob;
Idle = 48f subtle sway.

Usage: blender --background --python rig_colonist.py -- <in.glb> <out.glb>
"""
import bpy, sys, math
from mathutils import Vector

argv = sys.argv[sys.argv.index("--") + 1:]
src, dst = argv[0], argv[1]

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=src)

# --- join everything into one mesh, apply transforms ---
meshes = [o for o in bpy.data.objects if o.type == "MESH"]
bpy.ops.object.select_all(action="DESELECT")
for o in meshes:
    o.select_set(True)
bpy.context.view_layer.objects.active = meshes[0]
if len(meshes) > 1:
    bpy.ops.object.join()
body = bpy.context.view_layer.objects.active
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)

# --- RH_TARGET_HEIGHT_M: normalize the mesh height BEFORE the armature is
# fitted (2026-08-18, the slenderman bug). TRELLIS.2 meshes arrive ~1.0 units
# tall while the shipped crew skeletons were fitted on ~1.99-unit meshes.
# UE's reimport-in-place KEEPS the existing Skeleton asset, and animation
# retargeting then stretches a short mesh to the old bone spacing: the
# director's "extremely and terrifyingly large... slenderman" botanist,
# measured as 100 cm imported bounds vs the roster's 199 cm. Scaling here -
# before bone fitting - makes the new armature's spacing match the shipped
# skeleton, which is what the 12 re-rigged-from-199cm-sources proved works.
_th = float(__import__("os").environ.get("RH_TARGET_HEIGHT_M", "0") or 0)
if _th > 0:
    _bb = [body.matrix_world @ Vector(c) for c in body.bound_box]
    _H = max(v.z for v in _bb) - min(v.z for v in _bb)
    if _H > 1e-4 and abs(_H - _th) > 1e-3:
        _f = _th / _H
        body.scale = (_f, _f, _f)
        bpy.ops.object.select_all(action="DESELECT")
        body.select_set(True)
        bpy.context.view_layer.objects.active = body
        bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
        print("[rig] height normalized: %.3f -> %.2f m (x%.3f)" % (_H, _th, _f), flush=True)

# --- proportions from bounds ---
bb = [body.matrix_world @ Vector(c) for c in body.bound_box]
zmin = min(v.z for v in bb); zmax = max(v.z for v in bb)
H = zmax - zmin
xs = [v.x for v in bb]; ys = [v.y for v in bb]
cx = (min(xs) + max(xs)) / 2.0; cy = (min(ys) + max(ys)) / 2.0
wx = max(xs) - min(xs); wy = max(ys) - min(ys)
# wider horizontal axis = the shoulder line; legs/arms sit along it
side = Vector((1, 0, 0)) if wx >= wy else Vector((0, 1, 0))
W = max(wx, wy)
Z = lambda f: zmin + H * f
C = Vector((cx, cy, 0))

def bpos(fz, lateral=0.0):
    return C + side * lateral + Vector((0, 0, Z(fz)))

arm_data = bpy.data.armatures.new("RH_Skel")
rig = bpy.data.objects.new("RH_Rig", arm_data)
bpy.context.collection.objects.link(rig)
bpy.context.view_layer.objects.active = rig
bpy.ops.object.mode_set(mode="EDIT")

def add_bone(name, head, tail, parent=None):
    b = arm_data.edit_bones.new(name)
    b.head = head; b.tail = tail
    if parent:
        b.parent = arm_data.edit_bones[parent]
    return b

hipw = W * 0.11
shw = W * 0.20
add_bone("pelvis", bpos(0.50), bpos(0.58))
add_bone("spine",  bpos(0.58), bpos(0.72), "pelvis")
add_bone("chest",  bpos(0.72), bpos(0.82), "spine")
add_bone("head",   bpos(0.82), bpos(0.97), "chest")
for sgn, s in ((1, "L"), (-1, "R")):
    add_bone(f"thigh.{s}", bpos(0.50,  sgn * hipw), bpos(0.27, sgn * hipw), "pelvis")
    add_bone(f"shin.{s}",  bpos(0.27,  sgn * hipw), bpos(0.05, sgn * hipw), f"thigh.{s}")
    add_bone(f"foot.{s}",  bpos(0.05,  sgn * hipw), bpos(0.01, sgn * hipw * 1.15), f"shin.{s}")
    add_bone(f"upperarm.{s}", bpos(0.78, sgn * shw), bpos(0.62, sgn * shw * 1.35), "chest")
    add_bone(f"forearm.{s}",  bpos(0.62, sgn * shw * 1.35), bpos(0.46, sgn * shw * 1.5), f"upperarm.{s}")

# --- ALIGN EVERY BONE'S ROLL to the anatomical swing plane (2026-08-18) ---
# The clips key fore-aft swings on local X, which is only the side axis for
# bones Blender happens to give that roll - true for the straight-down legs,
# FALSE for the diagonal arm bones, whose default rolls tilt each bone's local
# X differently. Keying axis 0 on those rotated the shoulder and the elbow in
# two different world planes - the director's "the elbow and the hands are
# articulating in different directions than the elbow and the shoulder".
# Setting local Z = side x bonedir makes local X the side-axis projection for
# EVERY bone (X = Y x Z = d x (side x d) = side - d(d.side)), so all arm
# segments swing in the same fore-aft plane. For vertical legs this reproduces
# Blender's default roll exactly, so existing leg motion is bit-identical.
for eb in arm_data.edit_bones:
    d = (eb.tail - eb.head).normalized()
    z_target = side.cross(d)
    if z_target.length > 1e-4:
        eb.align_roll(z_target.normalized())
bpy.ops.object.mode_set(mode="OBJECT")

# --- WELD BEFORE BIND (2026-08-17: this is the fix, not an option) ---
# glTF cannot store per-loop UVs or split normals, so it duplicates a vertex at
# every UV seam and shading split. The Hunyuan crew arrive as 53,885 vertices
# over 18,000 triangles - 2.99 per triangle, essentially every vertex split.
#
# Binding in that state DESTROYS THE SKIN, measured not guessed: with the mesh
# unwelded the heat solver fails on all of it and the backstop below hard-assigns
# 53,885 of 53,885 vertices to a single nearest bone each - zero blending
# anywhere, every joint a rigid boundary. Welding first drops it to 9,002 real
# vertices, the heat solver succeeds completely, and the backstop assigns ZERO.
# In motion the difference is a slab of hip geometry tearing off the body and
# hanging in the air (docs/qa/2026-08-17/qa-weld-motion-ab.jpg) - the director's
# "sometimes you can see through parts of their body", reproduced deterministically.
#
# The weld must happen HERE, after import and before the bind. Doing it in an
# intermediate GLB does not work: the exporter re-splits the vertices on the way
# out, so the file arrives unwelded again. What matters is that each real vertex
# is ONE vertex while weights are computed; the exporter may then duplicate it
# freely, because the copies carry identical weights and cannot travel apart.
# Blender keeps UVs and custom normals per loop, so this costs no fidelity.
_before_weld = len(body.data.vertices)
bpy.context.view_layer.objects.active = body
body.select_set(True)
bpy.ops.object.mode_set(mode="EDIT")
bpy.ops.mesh.select_all(action="SELECT")
# Threshold is env-tunable because two of the crew (fab_stone, vet_kowalski)
# still failed the heat solver at the default: their duplicate vertices are
# NEAR-coincident rather than exactly coincident, so the shells stayed
# disconnected and the backstop again claimed 100%. A larger merge joins them.
# Keep it small - this is for joining vertices that should already be one, not
# for simplifying geometry.
bpy.ops.mesh.remove_doubles(threshold=float(__import__("os").environ.get("RH_WELD_DIST", "0.00005")))
bpy.ops.object.mode_set(mode="OBJECT")
body.select_set(False)
print("[rig] welded before bind: %d -> %d verts" % (_before_weld, len(body.data.vertices)), flush=True)

# --- bind: automatic weights, then GUARANTEE full coverage ---
# Hunyuan meshes carry disconnected shells (armor plates) that defeat the heat
# solver ("Bone Heat Weighting failed"), leaving verts weightless - the glTF
# exporter then drops the skin entirely and the armature degrades to empties.
# So: try auto weights, then hard-assign every still-weightless vertex to its
# nearest bone segment. Every vertex weighted => a real skin exports.
bpy.ops.object.select_all(action="DESELECT")
body.select_set(True)
rig.select_set(True)
bpy.context.view_layer.objects.active = rig
try:
    bpy.ops.object.parent_set(type="ARMATURE_AUTO")
    print("[rig] auto-weight pass done", flush=True)
except Exception as e:
    print("[rig] auto weights threw, parenting plain:", e, flush=True)
    bpy.ops.object.parent_set(type="ARMATURE_NAME")

# nearest-bone backstop for weightless verts
bone_segs = []
for b in rig.data.bones:
    hd = rig.matrix_world @ b.head_local
    tl = rig.matrix_world @ b.tail_local
    grp = body.vertex_groups.get(b.name) or body.vertex_groups.new(name=b.name)
    bone_segs.append((grp, hd, tl))

def seg_dist(p, a, bpt):
    ab = bpt - a
    t = max(0.0, min(1.0, ab.dot(p - a) / max(ab.length_squared, 1e-8)))
    return (a + ab * t - p).length

# SOFT backstop (2026-08-17). This used to assign 1.0 to the single nearest
# bone, which is rigid binding: two vertices either side of a joint snap to
# DIFFERENT bones and the surface tears open as the joint bends. Welding fixes
# most meshes by letting the heat solver succeed, but it cannot fix them all -
# fab_stone and vet_kowalski carry ~25% of their geometry in detached shells
# (74 and 64 connected components, largest holding only ~76%, against a healthy
# mesh's 94%), and Blender's heat weighting is all-or-nothing per object, so it
# fails for the whole mesh and every vertex lands here.
#
# So the fallback now blends over the K nearest bone segments by inverse
# distance instead of picking one. A detached pouch still rides its nearest
# bone almost rigidly, because that bone dominates the blend; a vertex near a
# joint gets a real mix of both bones and deforms smoothly. The failure mode
# degrades from "geometry tears off the body" to "slightly soft weighting".
K_NEAREST = 3
# Relative cutoff (2026-08-18): a candidate bone only joins the blend when its
# segment distance is within CUTOFF x the nearest bone's. Without it, a BELLY
# vertex - nearest to the spine but with the upper-arm segment passing not
# much further away - handed the arm ~25% of the torso surface, and the belly
# stretched with every arm swing (the director's "alien"). A true joint vertex
# sits near-equidistant between its two bones and keeps the blend; a torso
# vertex is 2-3x closer to the spine than to any arm and now binds clean.
# Inverse-SQUARE weighting further concentrates the blend on the winner.
REL_CUTOFF = 1.6
fixed = 0
mw = body.matrix_world
for v in body.data.vertices:
    if sum(g.weight for g in v.groups) > 1e-4:
        continue
    p = mw @ v.co
    ranked = sorted(((seg_dist(p, s[1], s[2]), s[0]) for s in bone_segs), key=lambda t: t[0])[:K_NEAREST]
    ranked = [(d, grp) for d, grp in ranked if d <= ranked[0][0] * REL_CUTOFF]
    inv = [(1.0 / max(d * d, 1e-6), grp) for d, grp in ranked]
    total = sum(w for w, _ in inv) or 1.0
    for w, grp in inv:
        grp.add([v.index], w / total, "REPLACE")
    fixed += 1
weightless = sum(1 for v in body.data.vertices if sum(g.weight for g in v.groups) <= 1e-4)
print(f"[rig] backstop assigned {fixed} verts; weightless now {weightless}", flush=True)

# --- animation helpers ---
FPS = 24
scene = bpy.context.scene
scene.render.fps = FPS
bpy.context.view_layer.objects.active = rig
bpy.ops.object.mode_set(mode="POSE")
for pb in rig.pose.bones:
    pb.rotation_mode = "XYZ"

# Legs point straight down: local Y runs along the bone; swinging fore/aft is a
# rotation about the bone's local axis perpendicular to both bone dir and the
# side axis. With bones built in the side/Z plane, local X is fore-aft for
# vertical bones regardless of which world axis is "side".
def key_pose(action_frames, bone, axis, degrees_by_frame):
    pb = rig.pose.bones[bone]
    for f, deg in degrees_by_frame.items():
        rot = [0.0, 0.0, 0.0]
        rot[axis] = math.radians(deg)
        pb.rotation_euler = rot
        pb.keyframe_insert("rotation_euler", frame=f)

def make_action(name):
    act = bpy.data.actions.new(name)
    rig.animation_data_create()
    rig.animation_data.action = act
    return act

# WALK: 24-frame loop. Contact at 1 and 13, passing at 7 and 19.
make_action("Walk")
SW = 17.0   # thigh swing (toned down: rigid weights shear at big angles)
KN = 24.0   # knee bend on the swinging leg
AR = 12.0   # arm counter-swing
key_pose(None, "thigh.L", 0, {1: -SW, 7: 4,  13: SW,  19: 4,  25: -SW})
key_pose(None, "thigh.R", 0, {1: SW,  7: 4,  13: -SW, 19: 4,  25: SW})
key_pose(None, "shin.L",  0, {1: 8,   7: KN, 13: 4,   19: 6,  25: 8})
key_pose(None, "shin.R",  0, {1: 4,   7: 6,  13: 8,   19: KN, 25: 4})
key_pose(None, "upperarm.L", 0, {1: AR,  13: -AR, 25: AR})
key_pose(None, "upperarm.R", 0, {1: -AR, 13: AR,  25: -AR})
key_pose(None, "forearm.L",  0, {1: 10, 13: -6, 25: 10})
key_pose(None, "forearm.R",  0, {1: -6, 13: 10, 25: -6})
key_pose(None, "spine", 2, {1: 3, 13: -3, 25: 3})   # counter-twist
pb = rig.pose.bones["pelvis"]
for f, dz in {1: 0.0, 7: -H * 0.008, 13: 0.0, 19: -H * 0.008, 25: 0.0}.items():
    pb.location = (0, 0, dz)
    pb.keyframe_insert("location", frame=f)
walk = rig.animation_data.action
walk.use_frame_range = True
walk.frame_start, walk.frame_end = 1, 25

# IDLE: 48-frame subtle breath/sway.
make_action("Idle")
key_pose(None, "chest", 0, {1: 0, 25: 2.2, 49: 0})
key_pose(None, "head",  0, {1: 0, 25: -1.5, 49: 0})
key_pose(None, "upperarm.L", 2, {1: 0, 25: 2.0, 49: 0})
key_pose(None, "upperarm.R", 2, {1: 0, 25: -2.0, 49: 0})
idle = rig.animation_data.action
idle.use_frame_range = True
idle.frame_start, idle.frame_end = 1, 49

# WORKBENCH: hammering - lean over the bench, right arm rises and strikes,
# left arm braced steady; a readable "guy working at a bench" from above.
make_action("WorkBench")
key_pose(None, "spine", 0, {1: 8, 25: 8})                       # lean in, held
key_pose(None, "upperarm.R", 0, {1: -30, 7: -62, 13: -18, 25: -30})  # raise, strike
key_pose(None, "forearm.R",  0, {1: 25, 7: 55, 13: 12, 25: 25})
key_pose(None, "upperarm.L", 0, {1: -35, 25: -35})              # braced on the bench
key_pose(None, "head", 0, {1: 6, 25: 6})                        # eyes on the work
bench = rig.animation_data.action
bench.use_frame_range = True
bench.frame_start, bench.frame_end = 1, 25

# LAB: pouring between vessels - both forearms up, alternating tips with a
# small torso shift, then a button-press beat; reads as careful benchwork.
make_action("WorkLab")
key_pose(None, "spine", 0, {1: 5, 49: 5})
key_pose(None, "upperarm.R", 0, {1: -45, 13: -50, 25: -45, 37: -58, 49: -45})
key_pose(None, "forearm.R",  2, {1: 0, 13: -35, 25: 0, 37: 8, 49: 0})   # tip the beaker
key_pose(None, "upperarm.L", 0, {1: -42, 25: -46, 49: -42})
key_pose(None, "forearm.L",  2, {1: 0, 25: 30, 37: 0, 49: 0})           # receiving vessel
key_pose(None, "head", 2, {1: -4, 25: 4, 49: -4})                       # glance between them
lab = rig.animation_data.action
lab.use_frame_range = True
lab.frame_start, lab.frame_end = 1, 49

# DIG: crouch and drive a tool down repeatedly - shovel/drill at the ground.
make_action("Dig")
key_pose(None, "spine", 0, {1: 18, 33: 18})              # bent over the work
key_pose(None, "thigh.L", 0, {1: 22, 33: 22})            # knees bent (crouch)
key_pose(None, "thigh.R", 0, {1: 22, 33: 22})
key_pose(None, "shin.L", 0, {1: -30, 33: -30})
key_pose(None, "shin.R", 0, {1: -30, 33: -30})
key_pose(None, "upperarm.L", 0, {1: -20, 9: -55, 17: -12, 33: -20})  # raise + thrust down
key_pose(None, "upperarm.R", 0, {1: -22, 9: -58, 17: -14, 33: -22})
key_pose(None, "forearm.L", 0, {1: 20, 9: 45, 17: 10, 33: 20})
key_pose(None, "forearm.R", 0, {1: 22, 9: 48, 17: 12, 33: 22})
dig = rig.animation_data.action
dig.use_frame_range = True; dig.frame_start, dig.frame_end = 1, 33

# HAUL: carry a load - both arms cradled forward, small labouring sway.
make_action("Haul")
key_pose(None, "spine", 0, {1: 6, 33: 6})                # lean back slightly under load
key_pose(None, "upperarm.L", 0, {1: -58, 33: -58})       # arms forward, holding
key_pose(None, "upperarm.R", 0, {1: -58, 33: -58})
key_pose(None, "forearm.L", 0, {1: 55, 33: 55})          # forearms up (cradle)
key_pose(None, "forearm.R", 0, {1: 55, 33: 55})
key_pose(None, "chest", 2, {1: 2, 17: -2, 33: 2})        # trudge sway
haul = rig.animation_data.action
haul.use_frame_range = True; haul.frame_start, haul.frame_end = 1, 33

# CHARGE: docked robot at rest on the pad - arms down, a slow status pulse.
make_action("Charge")
key_pose(None, "upperarm.L", 0, {1: -4, 49: -4})
key_pose(None, "upperarm.R", 0, {1: -4, 49: -4})
key_pose(None, "chest", 0, {1: 0, 25: 1.4, 49: 0})       # faint breathe/hum
key_pose(None, "head", 0, {1: 0, 25: 1.0, 49: 0})
charge = rig.animation_data.action
charge.use_frame_range = True; charge.frame_start, charge.frame_end = 1, 49

# PLANT: deep crouch, tend the soil - reach down and pat in rows.
make_action("Plant")
key_pose(None, "spine", 0, {1: 24, 33: 24})
key_pose(None, "thigh.L", 0, {1: 34, 33: 34})            # deep knee bend
key_pose(None, "thigh.R", 0, {1: 34, 33: 34})
key_pose(None, "shin.L", 0, {1: -46, 33: -46})
key_pose(None, "shin.R", 0, {1: -46, 33: -46})
key_pose(None, "upperarm.L", 0, {1: -40, 17: -52, 33: -40})   # small reaching pats
key_pose(None, "upperarm.R", 0, {1: -40, 25: -52, 33: -40})
key_pose(None, "forearm.L", 0, {1: 30, 17: 44, 33: 30})
key_pose(None, "forearm.R", 0, {1: 30, 25: 44, 33: 30})
plant = rig.animation_data.action
plant.use_frame_range = True; plant.frame_start, plant.frame_end = 1, 33

# REPAIR: reach up and turn a wrench - one arm working overhead, one braced.
make_action("Repair")
key_pose(None, "spine", 0, {1: -4, 33: -4})              # look up at the work
key_pose(None, "head", 0, {1: -8, 33: -8})
key_pose(None, "upperarm.R", 0, {1: 55, 33: 55})         # right arm up to the panel
key_pose(None, "forearm.R", 2, {1: 0, 9: 40, 17: 0, 25: 40, 33: 0})  # wrench cranking
key_pose(None, "upperarm.L", 0, {1: 20, 33: 20})         # left braced
key_pose(None, "forearm.L", 0, {1: 30, 33: 30})
repair = rig.animation_data.action
repair.use_frame_range = True; repair.frame_start, repair.frame_end = 1, 33

# OPERATE: work a control panel - both hands at chest, alternating button jabs.
make_action("Operate")
key_pose(None, "spine", 0, {1: 4, 33: 4})
key_pose(None, "upperarm.L", 0, {1: -48, 33: -48})       # hands up at a console
key_pose(None, "upperarm.R", 0, {1: -48, 33: -48})
key_pose(None, "forearm.L", 0, {1: 60, 9: 72, 17: 60, 33: 60})   # left jab
key_pose(None, "forearm.R", 0, {1: 60, 21: 72, 29: 60, 33: 60})  # right jab (offset)
operate = rig.animation_data.action
operate.use_frame_range = True; operate.frame_start, operate.frame_end = 1, 33

# keep all actions alive through export
for a in (walk, idle, bench, lab, dig, haul, charge, plant, repair, operate):
    a.use_fake_user = True

bpy.ops.object.mode_set(mode="OBJECT")
scene.frame_start, scene.frame_end = 1, 49

bpy.ops.export_scene.gltf(
    filepath=dst, export_format="GLB", export_yup=True,
    export_animations=True, export_animation_mode="ACTIONS",
    export_skins=True, export_apply=False)
print(f"[rig] exported {dst} with actions: {[a.name for a in bpy.data.actions]}", flush=True)
