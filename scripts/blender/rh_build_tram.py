"""Build the colony TRAM from code - one car + the articulation joint (2026-10-26, D6d visual pass).

  blender -b -P scripts/blender/rh_build_tram.py -- [--out Martians/gen/hs]
      [--part both|car|joint] [--size 2048] [--preview-only]

Two assets from one script, as rh_build_elevator.py. --out is the PARENT folder;
each asset is written to its own subfolder, <out>/RH_TramCar and <out>/RH_TramJoint.
--part picks one (default both, one Blender process). --size is the car's texture
size; the joint bakes at a quarter of it.
  RH_TramCar    ONE car: its cab at +X, its joint face at -X. The game draws two
                per line and turns the second 180 deg, so the train is double-
                ended (it shuttles; the flip at each stop never shows).
  RH_TramJoint  the rubber concertina between the cars' joint faces.

Why: the director, hand-play 2026-10-25: "The tram looks way unfinished design-
wise." The old tram was a procedural glowing translucent pill (15 x 3.6 m) that
sank 40 cm through the floor, overhung the tunnel end by 2.5 m and snapped 90 deg
at a corner. Design: docs/qa/2026-10-25/visual-pass-design.md section 1, with the
DIRECTOR ANSWER Q-B (a): the wait colour shows on the destination boards ONLY.

GEOMETRY CONTRACT - RHTramVisualizerSubsystem.cpp places the parts by these
numbers. Metres, Blender axes, Z up, TRUE SCALE (the game does not rescale):
  - car pivot = the car's centre on the ground: bounds centred on XY 0, tyre
    soles at z = 0. Car X -2.855..+2.855 (5.71 m: 5.6 m of body, the bumper and
    the joint collar), Y -1.33..+1.33 (2.6 m body + 3 cm door frames), roof 3.00,
    roof pods to 3.18, antenna tip 3.20.
  - the joint face is the car's -X end (x = -2.855). The joint is 0.60 m between
    the two cars' joint faces, so the assembled train is 2 x 5.71 + 0.60 = 12.02 m.
  - bogie centres at car x = +-2.00 (4.0 m apart): the visualizer stands each
    car on two points of the tunnel chain this far apart, so a car is the chord
    between its bogies and the cars bend through an L corner.
  - floor 0.35 m up; each side has one paired door, clear opening 1.22 x 2.24 m
    (x -0.91..+0.31, z 0.35..2.59): the elevator's doorway contract, the 1.88 m
    crew plus a foot all round. Asserted off the BUILT door frame, lip and sill
    (doorway()), not off the constants.
  - the joint pivot = the joint centre on the ground (z 0 is the floor plane,
    not its bounds bottom: the bellows hang between the cars' collars). The
    bellows run on to x +-0.45, 15 cm into each car's collar (hidden there on
    straight track), so the outside of a bend shows bellows, not a gap.

Layout facts:
  - the shell is ONE loft of rounded-rect sections (rounded roof shoulders, small
    bottom radius), so the body and the cab are one smooth surface. The nose's
    sections shrink in plan (rounded cab corners) and drop their roof (a raked,
    wrap-around windscreen); faces are painted by role on the loft (paint()).
  - the camera sees a tram from 55-80 deg above (the floor view), so the ROOF
    carries the read: bone-white roof, a light-alloy HVAC pod with two fans, a
    ribbed battery pod, the lit warm glazing along both roof shoulders (the
    cantrail lights), an amber band at each cab, the dark windscreens, the
    destination boards on the shoulders and above the windscreen.
  - palette (art bible): body hull bone-white; underframe, skirts, window and
    door frames, bogies, collars slate; pods light alloy; tyres and bellows
    rubber; cab windscreen dark glass. Accent (#F3C459, the shaft and elevator
    amber, so lifts and trams read as one transit family) is TRIM only: the belt
    stripe under the windows, the door edges, the bumper bar, the roof bands.
    Accent is 3.2 % of ALL faces (role_stats: the underframe, bogies, collar and
    the inside faces count there) but 6.0-6.4 % of the VISIBLE surface, inside
    the art bible's 5-15 %: ray cast from 8 yaws, car alone, 2026-10-26 - on the
    preview GLB (120 x 120 rays a view) 6.4 / 6.4 / 6.3 % at pitch 15 / 55 /
    75 deg (75 = the floor camera); visible_share() repeats the 55 and 75 deg
    casts (80 x 80) on every build, logs them (6.4 / 6.0 %) and asserts 5-15 %.
  - ONE glow hue, warm white, four strengths through the emissive mask: lamps
    (headlamps, roof markers, door buttons) 1.0, the passenger windows 0.55 (lit
    from inside), the cantrail glazing on the roof shoulders 0.30, the
    destination boards 1.0. The cantrail was split from the windows (0.55) to
    calm the floor camera's read underground, but at 0.30 it still tonemaps to
    near white there (about 229, 217, 187 in the -14 bench frames, as the
    windows): two light strips along the roof shoulders. Left as is for the
    director with the underground hull-darkness question (2026-10-26 review).
  - the destination boards are their own role AND their own material slot
    ("RH_TramCar_Board"): the bake puts them in the car's one atlas, the export
    then gives their faces a second material, and the import gives that slot a
    child MI the game tints per line (teal, orange, red: the line's wait;
    orange, not the lifts' amber, which is this livery's own trim).
  - budget: car ~9k tris (7.8k built), joint ~600 (676 built).
  - every decal / lens / plate stands >= 4 mm proud of its surface; parts meet by
    butting or by >= 3 mm interpenetration, never by coplanar overlap.

IMPORT (one Unreal process at a time; then rh_mesh_stats.py in a FRESH process).
The tram's settings are NOT the kit defaults - a default import burns the
windows out (glow 1.6), has no LODs and no board slot (the game then warns and
the boards stop showing the wait colour):
  RH_NAME=RH_TramCar RH_DIR=$PWD/Martians/gen/hs/RH_TramCar \
  RH_DEST=/Game/RedHope/Art/Models2/TramCar RH_GLOW=4.5,3.4,2.0 RH_GLOW_AMT=0.6 \
  RH_LODS=0.35@0.25,0.10@0.08 RH_CHILD_SLOT=Board RH_REPORT=<txt> \
    UnrealEditor-Cmd $PWD/red_hope.uproject -run=pythonscript \
    -script=$PWD/scripts/unreal/rh_import_hs.py -unattended -nosound -stdout
  RH_NAME=RH_TramJoint RH_DIR=$PWD/Martians/gen/hs/RH_TramJoint \
  RH_DEST=/Game/RedHope/Art/Models2/TramJoint RH_GLOW=4.5,3.4,2.0 RH_GLOW_AMT=0 \
  RH_REPORT=<txt> UnrealEditor-Cmd ... (as above)
Delete Models2/TramCar/RH_TramCar (and .../TramJoint/RH_TramJoint) before a
re-import: Interchange's replace-in-place left the BaseColor and MR textures on
an older bake once (2026-10-26) - every asset's AssetImportData FileMD5 must be
the GLB's. Interchange also leaves Materials/RH_TramCar(_Board), unreferenced
M_GLTF instances, as every Models2 import does.
"""
import hashlib
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bmesh  # noqa: E402
import bpy  # noqa: E402
from mathutils import Matrix, Vector  # noqa: E402

import rh_hs  # noqa: E402

with open(os.path.abspath(__file__), "rb") as _f:      # which text built these outputs (the log ties them)
    rh_hs.log("rh_build_tram.py sha256 %s" % hashlib.sha256(_f.read()).hexdigest())

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OUT = argv[argv.index("--out") + 1] if "--out" in argv else "Martians/gen/hs"
SIZE = int(argv[argv.index("--size") + 1]) if "--size" in argv else 2048
PREVIEW_ONLY = "--preview-only" in argv
PART = argv[argv.index("--part") + 1] if "--part" in argv else "both"
assert PART in ("both", "car", "joint"), "--part must be both, car or joint"
CAR, JOINT = "RH_TramCar", "RH_TramJoint"
ACCENT = "#F3C459"                          # shaft / elevator amber: one transit family
DUST = 0.12                                 # underground, swept tunnels

# ------------------------------------------------ geometry contract (C++ owns these)
XJ, XN = -2.855, 2.855                     # joint face, bumper tip
HW = 1.30                                  # body half width (door frames to 1.33)
ZB, ZT = 0.30, 3.00                        # shell bottom, roof
BOGIE_X = 2.00                             # bogie centres at +-BOGIE_X
JOINT_HALF = 0.30                          # car joint face to the joint centre
ZFLOOR = 0.35                              # floor / door sill
DX0, DX1, DZ1 = -0.91, 0.31, 2.59          # clear doorway x, head z (sill ZFLOOR)

# ------------------------------------------------------------- layout (free)
R_TOP, R_BOT = 0.38, 0.08                  # roof shoulder and bottom radii
BELT = (1.05, 1.19)                        # the amber belt stripe, painted on the shell
WS_Z = 1.50                                # the windscreen's bottom edge, all round the nose
ROWS = BELT + (WS_Z,)                      # side rows every shell section carries
WIN_Z = (1.26, 2.30)                       # window band glass
GLAZE_X = (-2.55, 0.95)                    # cantrail glazing on the roof shoulders
SIDEBOARD_X = (0.95, 1.40)                 # shoulder destination boards
# nose sections: (x, half width, roof z, shoulder radius). The plan rounds from
# 2.10 to the tip and the roof drops: the top faces between sections are the
# raked, wrap-around windscreen (54 -> 80 deg from horizontal), its bottom edge
# the WS_Z row on every section, so the glass ends on one clean line.
NOSE = [(1.45, 1.30, 3.00, 0.38), (1.85, 1.30, 3.00, 0.38), (2.10, 1.29, 2.98, 0.38),
        (2.30, 1.25, 2.72, 0.36), (2.48, 1.17, 2.36, 0.32), (2.62, 1.06, 2.02, 0.27),
        (2.72, 0.94, 1.78, 0.20), (2.78, 0.84, 1.62, 0.08)]
XTIP = NOSE[-1][0]                         # the shell's nose cap; the bumper reaches XN

K = None


def new_kit():
    """Fresh scene + role materials (Kit() resets the scene)."""
    global K
    K = rh_hs.Kit(accent_hex=ACCENT)
    warm = (1.0, 0.80, 0.56, 1.0)          # EmissiveColor normalised (preview only)
    # lamps: albedo = the warm in-game glow so it reads lit in daylight too
    K.roles["glow"] = dict(albedo="#FFD8A0", metal=0.0, rough=0.25, emit=1, dust=0.0)
    # passenger windows: dark warm glass lit from inside at half the lamps' strength
    K.roles["window"] = dict(albedo="#3B3226", metal=0.0, rough=0.10, emit=0.55, dust=0.0)
    # cantrail glazing on the roof shoulders: the same glass, lit lower (the floor camera's main read)
    K.roles["cantrail"] = dict(albedo="#3B3226", metal=0.0, rough=0.10, emit=0.30, dust=0.0)
    # destination boards: a light neutral face, fully lit; the game tints its slot
    K.roles["board"] = dict(albedo="#E4E0D6", metal=0.0, rough=0.30, emit=1, dust=0.0)
    for r, strength in (("glow", 1.5), ("window", 0.8), ("cantrail", 0.45), ("board", 1.5)):
        K.mats[r] = K._preview_material(r, K.roles[r])
        b = K.mats[r].node_tree.nodes["Principled BSDF"]
        b.inputs["Emission Color"].default_value = warm
        b.inputs["Emission Strength"].default_value = strength


# ------------------------------------------------------------------ helpers
AXES = {"x": (1, 2, 0), "y": (0, 2, 1), "z": (0, 1, 2)}   # (u, v, depth) indices


def _pt(axis, u, v, d):
    iu, iv, idd = AXES[axis]
    c = [0.0, 0.0, 0.0]
    c[iu], c[iv], c[idd] = u, v, d
    return c


def _bevel(bm, bevel, seg):
    """Bevel CONVEX edges only (rh_build_elevator.py: a concave bevel fills corners)."""
    if bevel > 0:
        bmesh.ops.bevel(bm, geom=[e for e in bm.edges if e.is_convex], offset=bevel,
                        offset_type="OFFSET", segments=seg, profile=0.5, affect="EDGES",
                        clamp_overlap=True)
        bmesh.ops.dissolve_degenerate(bm, dist=1e-5, edges=bm.edges[:])
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])


def prism(name, poly, axis, d0, d1, role, bevel=0.0, seg=1):
    """Extrude a simple 2D polygon [(u, v)] along `axis` from d0 to d1 (x: (y, z);
    y: (x, z); z: (x, y)). Concave outlines (the door's U frame) are fine."""
    bm = bmesh.new()
    a = [bm.verts.new(_pt(axis, u, v, d0)) for u, v in poly]
    b = [bm.verts.new(_pt(axis, u, v, d1)) for u, v in poly]
    bm.faces.new(a)
    bm.faces.new(list(reversed(b)))
    n = len(poly)
    for i in range(n):
        j = (i + 1) % n
        bm.faces.new((a[i], a[j], b[j], b[i]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    _bevel(bm, bevel, seg)
    return K._obj(name, bm, [role])


def rect_frame(name, axis, d0, d1, outer, inner, role, bevel=0.0, seg=1):
    """Rectangular ring (outer (u0, u1, v0, v1) minus inner) extruded along axis."""
    O = [(outer[0], outer[2]), (outer[1], outer[2]), (outer[1], outer[3]), (outer[0], outer[3])]
    I = [(inner[0], inner[2]), (inner[1], inner[2]), (inner[1], inner[3]), (inner[0], inner[3])]
    bm = bmesh.new()
    oa = [bm.verts.new(_pt(axis, u, v, d0)) for u, v in O]
    ob = [bm.verts.new(_pt(axis, u, v, d1)) for u, v in O]
    ia = [bm.verts.new(_pt(axis, u, v, d0)) for u, v in I]
    ib = [bm.verts.new(_pt(axis, u, v, d1)) for u, v in I]
    for i in range(4):
        j = (i + 1) % 4
        bm.faces.new((oa[i], oa[j], ia[j], ia[i]))
        bm.faces.new((ob[j], ob[i], ib[i], ib[j]))
        bm.faces.new((oa[j], oa[i], ob[i], ob[j]))
        bm.faces.new((ia[i], ia[j], ib[j], ib[i]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    _bevel(bm, bevel, seg)
    return K._obj(name, bm, [role])


def ring(x, w, zb, zt, r, rb, rows=ROWS, ntop=4, nbot=2):
    """One rounded-rect cross-section at x, as 3D points in a fixed order (every
    section of a loft has the same count): bottom-right corner, the right side's
    rows, the right shoulder, the left shoulder, the left side's rows, the
    bottom-left corner."""
    pts = []
    for k in range(nbot + 1):
        a = math.radians(-90.0 + 90.0 * k / nbot)
        pts.append((w - rb + rb * math.cos(a), zb + rb + rb * math.sin(a)))
    side = [z for z in rows if zb + rb + 0.01 < z < zt - r - 0.01]
    assert len(side) == len(rows), "ring at x %.2f: a row falls outside the side" % x
    pts += [(w, z) for z in side]
    for k in range(ntop + 1):
        a = math.radians(90.0 * k / ntop)
        pts.append((w - r + r * math.cos(a), zt - r + r * math.sin(a)))
    pts += [(-y, z) for (y, z) in reversed(pts)]
    return [(x, y, z) for (y, z) in pts]


def loft(name, sections, role, caps=(True, True)):
    """Skin consecutive sections (same point count) with quads; ngon caps."""
    bm = bmesh.new()
    rings = [[bm.verts.new(p) for p in s] for s in sections]
    n = len(sections[0])
    for a, b in zip(rings, rings[1:]):
        for i in range(n):
            j = (i + 1) % n
            bm.faces.new((a[i], a[j], b[j], b[i]))
    if caps[0]:
        bm.faces.new(rings[0])
    if caps[1]:
        bm.faces.new(list(reversed(rings[-1])))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    return K._obj(name, bm, [role])


def paint(ob, role, pred):
    """Give the faces matching pred(normal, centre) the material `role`."""
    me = ob.data
    names = [m.name for m in me.materials]
    if K.mats[role].name not in names:
        me.materials.append(K.mats[role])
        names.append(K.mats[role].name)
    idx = names.index(K.mats[role].name)
    for p in me.polygons:
        if pred(p.normal, p.center):
            p.material_index = idx
    return ob


def shoulder_deg(n):
    """Where a face sits round the shoulder: 0 deg on the side, 90 on the roof."""
    return math.degrees(math.atan2(n.z, max(1e-6, abs(n.y))))


def bezel_x(name, x, cy, cz, w, h, proud, radius, rim, recess, rim_role, inner_role):
    """K.bezel (built facing -Y) carried onto a +X face: the face plane at x."""
    ob, wy = K.bezel(name, (-cy, -x + 0.003, cz), w, h, proud + 0.003, radius=radius, rim=rim,
                     recess=recess, rim_role=rim_role, inner_role=inner_role)
    ob.data.transform(Matrix.Rotation(math.radians(90.0), 4, "Z"))
    return ob, -wy


def on_slope(ob, x, z, ang):
    """Turn a part built about the origin (its outward face +X) by ang about -Y,
    so +X becomes (cos, 0, sin)(ang) - a face leaning back - and move it to (x, z)."""
    ob.data.transform(Matrix.Rotation(-ang, 4, "Y"))
    ob.data.transform(Matrix.Translation(Vector((x, 0.0, z))))
    return ob


def nose_at(x):
    """(half width, roof z, shoulder r) of the nose loft at x, interpolated."""
    for (xa, wa, za, ra), (xb, wb, zb, rb) in zip(NOSE, NOSE[1:]):
        if xa <= x <= xb:
            t = (x - xa) / (xb - xa)
            return wa + (wb - wa) * t, za + (zb - za) * t, ra + (rb - ra) * t
    return NOSE[-1][1:]


def role_stats(objs, label):
    tot = {}
    for o in objs:
        me = o.data
        for p in me.polygons:
            r = me.materials[p.material_index]["rh_role"]
            tot[r] = tot.get(r, 0.0) + p.area
    s = sum(tot.values())
    rh_hs.log("%s role share (all faces): %s" % (label, ", ".join(
        "%s %.1f%%" % (k, 100 * v / s) for k, v in sorted(tot.items(), key=lambda kv: -kv[1]))))


def bounds(ob):
    vs = [v.co for v in ob.data.vertices]
    lo = Vector([min(v[i] for v in vs) for i in range(3)])
    hi = Vector([max(v[i] for v in vs) for i in range(3)])
    rh_hs.log("%s bounds lo %s hi %s" % (ob.name, [round(c, 4) for c in lo], [round(c, 4) for c in hi]))
    return lo, hi


def doorway(frame, lip, sill, label):
    """The clear opening measured off the BUILT parts: the jambs' inner faces (the
    frame and its amber lip, at mid height), the head's underside, the sill's top.
    Asserted against the 1.22 x 2.24 m contract, so a layout edit cannot shrink it."""
    xm, zm = (DX0 + DX1) / 2, (ZFLOOR + DZ1) / 2
    jamb, lintel = [], []
    for ob in (frame, lip):
        me = ob.data
        for p in me.polygons:
            co = [me.vertices[i].co for i in p.vertices]
            if min(c.z for c in co) < zm < max(c.z for c in co):      # a face crossing mid height: a jamb
                jamb += co
            if min(c.x for c in co) < xm < max(c.x for c in co):      # a face crossing the centre line: the head
                lintel += co
    left = max(c.x for c in jamb if c.x < xm)
    right = min(c.x for c in jamb if c.x > xm)
    head = min(c.z for c in lintel if c.z > zm)
    floor = max(v.co.z for v in sill.data.vertices)
    w, h = right - left, head - floor
    rh_hs.log("doorway %s: %.3f x %.3f m clear (x %.3f..%.3f, z %.3f..%.3f)" % (label, w, h, left, right, floor, head))
    assert w >= 1.219 and h >= 2.239, "doorway %s %.3f x %.3f m, under the 1.22 x 2.24 m contract" % (label, w, h)
    assert abs(floor - ZFLOOR) < 0.005, "doorway %s: the sill top %.3f is not the floor %.2f" % (label, floor, ZFLOOR)


def visible_share(ob, pitches=(55.0, 75.0), yaws=8, n=80):
    """Each role's share of the VISIBLE surface: rays from `yaws` directions round
    the car at each pitch (degrees down from horizontal), an n x n grid over the
    car's bounding sphere, the first face hit counted by its role. The floor
    camera sees a tram from 55-80 deg. Asserts the accent is 5-15 % at each pitch
    (art bible: accent covers 5-15 % of the visible surface). Read only."""
    me = ob.data
    vs = [v.co for v in me.vertices]
    lo = Vector([min(v[i] for v in vs) for i in range(3)])
    hi = Vector([max(v[i] for v in vs) for i in range(3)])
    c, rad = (lo + hi) / 2, (hi - lo).length
    for pitch in pitches:
        tot = {}
        for k in range(yaws):
            yaw = math.radians(360.0 * k / yaws)
            d = Vector((-math.cos(yaw) * math.cos(math.radians(pitch)), -math.sin(yaw) * math.cos(math.radians(pitch)),
                        -math.sin(math.radians(pitch))))
            u = d.cross(Vector((0.0, 0.0, 1.0))).normalized()
            v = u.cross(d).normalized()
            for i in range(n):
                for j in range(n):
                    o = c - d * rad * 2 + u * ((i / (n - 1) - 0.5) * rad) + v * ((j / (n - 1) - 0.5) * rad)
                    hit, _loc, _nrm, idx = ob.ray_cast(o, d)
                    if hit:
                        r = me.materials[me.polygons[idx].material_index]["rh_role"]
                        tot[r] = tot.get(r, 0) + 1
        s = sum(tot.values())
        rh_hs.log("%s visible share at pitch %.0f deg: %s" % (ob.name, pitch, ", ".join(
            "%s %.1f%%" % (r, 100.0 * m / s) for r, m in sorted(tot.items(), key=lambda kv: -kv[1]))))
        acc = 100.0 * tot.get("accent", 0) / max(1, s)
        assert 5.0 <= acc <= 15.0, "accent %.1f %% of the visible surface at pitch %.0f, outside 5-15 %%" % (acc, pitch)


def export_preview(ob, name):
    out = os.path.join(OUT, name)
    os.makedirs(out, exist_ok=True)
    bpy.ops.object.select_all(action="DESELECT")
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.export_scene.gltf(filepath=os.path.join(out, name + "_preview.glb"),
                              export_format="GLB", use_selection=True, export_apply=True)
    rh_hs.log("preview glb written: %s" % out)
    return out


# =================================================================== the car
def build_car():
    new_kit()
    P = []
    add = P.append

    # ----------------------------------------------------------------- the shell
    xs = [-2.70, GLAZE_X[0], -1.10, 0.50, GLAZE_X[1], SIDEBOARD_X[1]]
    secs = [ring(x, HW, ZB, ZT, R_TOP, R_BOT) for x in xs]
    secs += [ring(x, w, ZB, zt, r, R_BOT) for (x, w, zt, r) in NOSE]
    shell = loft("shell", secs, "hull")
    # belt stripe: the side band between the two belt rows, nose sides included
    # (not the nose cap: one ngon whose centre sits in the band; front_belt crosses it)
    paint(shell, "accent", lambda n, c: (abs(n.y) > 0.3 or 0.3 < n.x < 0.95) and BELT[0] < c.z < BELT[1])
    # cantrail glazing and the shoulder boards: the middle of the shoulder arc
    paint(shell, "cantrail", lambda n, c: 25.0 < shoulder_deg(n) < 65.0 and abs(n.x) < 0.2
          and GLAZE_X[0] < c.x < GLAZE_X[1])
    paint(shell, "board", lambda n, c: 25.0 < shoulder_deg(n) < 65.0 and abs(n.x) < 0.2
          and SIDEBOARD_X[0] < c.x < SIDEBOARD_X[1])
    # the windscreen: every nose face leaning forward above the cap
    paint(shell, "glass", lambda n, c: c.x > 2.0 and n.x > 0.25 and c.z > WS_Z)
    add(shell)
    # a slate brow under the windscreen, across the cap's top edge
    w_tip = NOSE[-1][1]
    add(K.box("ws_sill", (XTIP - 0.06, -w_tip + 0.04, WS_Z - 0.08), (XTIP + 0.012, w_tip - 0.04, WS_Z - 0.005),
              "slate", bevel=0.01, seg=1))

    # ------------------------------------------------- joint end: the slate collar
    add(loft("collar", [ring(x, 1.22, 0.36, 2.92, 0.32, 0.06) for x in (XJ + 0.006, -2.69)], "slate"))
    # the bellows' mounting gasket, 6 mm proud of the collar: the joint face itself
    add(loft("gasket", [ring(x, 1.17, 0.41, 2.87, 0.28, 0.05) for x in (XJ, XJ + 0.012)], "rubber"))

    # -------------------------------------------------------- underframe, skirts
    add(K.box("underframe", (-1.32, -1.10, 0.14), (1.32, 1.10, ZB + 0.03), "slate", bevel=0.02, seg=1))
    for s in (-1, 1):
        ya, yb = sorted((s * (HW - 0.05), s * (HW - 0.01)))
        add(K.box("skirt", (-1.34, ya, 0.10), (1.34, yb, ZB + 0.06), "slate", bevel=0.012, seg=1))
        # rear (joint-end) skirt stub behind the bogie
        add(K.box("skirt_end", (-2.70, ya, 0.16), (-2.64, yb, ZB + 0.06), "slate", bevel=0.008, seg=1))
        # equipment boxes on the skirt, between the bogies
        ya, yb = sorted((s * (HW - 0.012), s * (HW + 0.004)))
        for x0, x1 in ((-1.10, -0.55), (0.65, 1.15)):
            add(K.box("equip", (x0, ya, 0.15), (x1, yb, 0.27), "pipe", bevel=0.008, seg=1))

    # ------------------------------------------------------------------- bogies
    # rubber-tyred, narrow track: the front bogie's tyres stay inside the nose's
    # rounded plan (w 1.06 at x 2.62)
    WR, WY0, WWID = 0.20, 0.86, 0.14
    for bx in (-BOGIE_X, BOGIE_X):
        add(K.box("bogie", (bx - 0.60, -0.78, 0.15), (bx + 0.60, 0.78, 0.34), "slate", bevel=0.03, seg=2))
        add(K.box("bolster", (bx - 0.14, -1.02, 0.24), (bx + 0.14, 1.02, 0.38), "slate", bevel=0.02, seg=1))
        for ax in (bx - 0.40, bx + 0.40):
            add(K.cyl("axle", (ax, -WY0, WR), (ax, WY0, WR), 0.05, "pipe", sides=10))
            for s in (-1, 1):
                y0, y1 = sorted((s * WY0, s * (WY0 + WWID)))
                add(K.cyl("tyre", (ax, y0, WR), (ax, y1, WR), WR, "rubber", sides=20))
                face = s * (WY0 + WWID)
                add(K.cyl("hub", (ax, face - s * 0.01, WR), (ax, face + s * 0.02, WR), 0.12, "metal", sides=14))
                add(K.cyl("hubcap", (ax, face + s * 0.015, WR), (ax, face + s * 0.035, WR), 0.05, "slate", sides=10))
        for s in (-1, 1):
            # dampers: a gunmetal strut from the bogie frame up to the bolster
            add(K.pipe("damper", [(bx - 0.17, s * 0.80, 0.22), (bx - 0.05, s * 0.80, 0.37)], 0.026,
                       "pipe", bend=0.0, sides=8, samples=1))
            add(K.box("spring", (bx + 0.04, s * 0.80 - 0.05, 0.30), (bx + 0.16, s * 0.80 + 0.05, 0.40),
                      "rubber", bevel=0.02, seg=1))

    # -------------------------------------------------------------- side windows
    for s in (-1, 1):
        def yspan(a, b):
            return sorted((s * a, s * b))
        # two runs of the band: joint end to the door, door to the cab's side window
        for (x0, x1, mull) in ((-2.58, -1.10, (-1.84,)), (0.48, 2.02, (1.04, 1.47))):
            ya, yb = yspan(HW - 0.004, HW + 0.016)
            add(rect_frame("win_frame", "y", ya, yb, (x0, x1, WIN_Z[0] - 0.05, WIN_Z[1] + 0.05),
                           (x0 + 0.05, x1 - 0.05, WIN_Z[0], WIN_Z[1]), "slate", bevel=0.006))
            ya, yb = yspan(HW - 0.004, HW + 0.008)
            add(K.box("win", (x0 + 0.045, ya, WIN_Z[0] - 0.005), (x1 - 0.045, yb, WIN_Z[1] + 0.005),
                      "window", bevel=0.0))
            for xm in mull:
                ya, yb = yspan(HW - 0.004, HW + 0.018)
                wide = 0.05 if abs(xm - 1.47) > 0.01 else 0.09     # the cab partition pillar
                add(K.box("mullion", (xm - wide / 2, ya, WIN_Z[0]), (xm + wide / 2, yb, WIN_Z[1]),
                          "slate", bevel=0.004, seg=1))
        # cab side: a slate camera pod on a short arm (the mirror), in the 1.33 m envelope
        ya, yb = yspan(HW - 0.05, HW + 0.03)
        add(K.box("cam_pod", (2.06, ya, 2.16), (2.20, yb, 2.30), "slate", bevel=0.012, seg=1))
        ya, yb = yspan(HW + 0.0, HW + 0.032)
        add(K.box("cam_lens", (2.20, ya, 2.20), (2.212, yb, 2.26), "glass", bevel=0.0))

    # ---------------------------------------------------------------------- doors
    door_parts = []                         # (frame, lip, sill) a side: doorway() measures them
    for s in (-1, 1):
        def yspan(a, b):
            return sorted((s * a, s * b))
        # slate U frame round the clear opening, 15 cm jambs, 10 cm head, 3 cm proud
        U = [(DX0 - 0.15, ZB), (DX0, ZB), (DX0, DZ1), (DX1, DZ1), (DX1, ZB), (DX1 + 0.15, ZB),
             (DX1 + 0.15, DZ1 + 0.10), (DX0 - 0.15, DZ1 + 0.10)]
        ya, yb = yspan(HW - 0.004, HW + 0.03)
        frame = prism("door_frame", U, "y", ya, yb, "slate", bevel=0.008)
        add(frame)
        # amber lip on the frame's inner edge, as the elevator's casing (one transit family)
        LIP = 0.035
        lip = [(DX0 - LIP, ZB + 0.02), (DX0 - 0.002, ZB + 0.02), (DX0 - 0.002, DZ1 + 0.002),
               (DX1 + 0.002, DZ1 + 0.002), (DX1 + 0.002, ZB + 0.02), (DX1 + LIP, ZB + 0.02),
               (DX1 + LIP, DZ1 + LIP), (DX0 - LIP, DZ1 + LIP)]
        ya, yb = yspan(HW + 0.02, HW + 0.034)
        lip_ob = prism("door_lip", lip, "y", ya, yb, "accent", bevel=0.002)
        add(lip_ob)
        # two light-alloy leaves meeting at the opening's centre line, 1.6 cm proud
        xm = (DX0 + DX1) / 2
        for (x0, x1) in ((DX0, xm - 0.006), (xm + 0.006, DX1)):
            ya, yb = yspan(HW - 0.004, HW + 0.016)
            add(K.box("leaf", (x0, ya, ZFLOOR + 0.005), (x1, yb, DZ1 - 0.005), "metal", bevel=0.006, seg=1))
            # tall leaf window in a rubber gasket
            ya, yb = yspan(HW + 0.010, HW + 0.022)
            add(rect_frame("leaf_gasket", "y", ya, yb, (x0 + 0.08, x1 - 0.08, 1.16, 2.36),
                           (x0 + 0.105, x1 - 0.105, 1.185, 2.335), "rubber", bevel=0.003))
            ya, yb = yspan(HW + 0.010, HW + 0.019)
            add(K.box("leaf_win", (x0 + 0.104, ya, 1.184), (x1 - 0.104, yb, 2.336), "window", bevel=0.0))
        # rubber seam between the leaves, amber edge strips either side of it
        ya, yb = yspan(HW - 0.004, HW + 0.019)
        add(K.box("seam", (xm - 0.007, ya, ZFLOOR + 0.01), (xm + 0.007, yb, DZ1 - 0.01), "rubber", bevel=0.0))
        for e in (-1, 1):
            x0, x1 = sorted((xm + e * 0.012, xm + e * 0.05))
            ya, yb = yspan(HW + 0.010, HW + 0.022)
            add(K.box("door_edge", (x0, ya, ZFLOOR + 0.05), (x1, yb, DZ1 - 0.05), "accent", bevel=0.003, seg=1))
        # sill plate with a HazYellow nosing, and the lit door button on the frame. The sill's
        # outer face sits 4 mm inside the frame's (HW + 0.03); the nosing spans the opening only
        # (butting the jambs) and stands 4 mm proud of the frame, so no two faces are coplanar.
        ya, yb = yspan(HW - 0.06, HW + 0.026)
        sill = K.box("sill", (DX0 - 0.02, ya, ZB + 0.02), (DX1 + 0.02, yb, ZFLOOR), "metal", bevel=0.004, seg=1)
        add(sill)
        door_parts.append((frame, lip_ob, sill))
        ya, yb = yspan(HW + 0.018, HW + 0.034)
        add(K.box("sill_nose", (DX0, ya, ZB + 0.03), (DX1, yb, ZFLOOR - 0.004), "hazard", bevel=0.002, seg=1))
        ya, yb = yspan(HW + 0.026, HW + 0.034)
        add(K.box("door_btn", (DX1 + 0.05, ya, 1.10), (DX1 + 0.10, yb, 1.20), "glow", bevel=0.0))

    # ----------------------------------------------------------------- the cab front
    # the cap face is the plane x = XTIP: y +-w_tip, z ZB up to the windscreen
    for s in (-1, 1):
        lamp, _ = bezel_x("headlamp", XTIP, s * 0.50, 0.84, 0.36, 0.18, 0.03, radius=0.07, rim=0.035,
                          recess=0.012, rim_role="slate", inner_role="glow")
        add(lamp)
    add(K.box("front_belt", (XTIP - 0.01, -w_tip + 0.02, BELT[0]), (XTIP + 0.008, w_tip - 0.02, BELT[1]),
              "accent", bevel=0.003, seg=1))
    # bumper / anti-climber, amber bar on its face
    add(K.box("bumper", (XTIP - 0.12, -0.80, 0.24), (XN - 0.016, 0.80, 0.52), "slate", bevel=0.03, seg=2))
    add(K.box("bumper_bar", (XN - 0.02, -0.64, 0.33), (XN, 0.64, 0.43), "accent", bevel=0.004, seg=1))
    # front destination board: a slate housing on the windscreen's top, the lit board on it
    xa, wa, za, _ = NOSE[2]
    xb, wb, zb2, _ = NOSE[3]
    ang = math.atan2(xb - xa, za - zb2)                    # lean back from vertical
    xm, zm = (xa + xb) / 2, (za + zb2) / 2
    housing = K.box("board_housing", (-0.02, -0.80, -0.13), (0.03, 0.80, 0.13), "slate", bevel=0.012, seg=1)
    add(on_slope(housing, xm, zm, ang))
    board = K.box("board_front", (0.026, -0.72, -0.085), (0.036, 0.72, 0.085), "board", bevel=0.0)
    add(on_slope(board, xm, zm, ang))
    # a wiper resting at the bottom of the windscreen
    _, z0, _ = nose_at(2.70)
    _, z1, _ = nose_at(2.56)
    add(K.pipe("wiper", [(2.712, -0.30, z0 + 0.03), (2.575, 0.18, z1 + 0.035)], 0.012, "pipe", bend=0.0,
               sides=6, samples=1))

    # ------------------------------------------------------------------- the roof
    # HVAC pod (two fans under guards) toward the joint end, a ribbed battery pod
    # toward the cab; the roof rails and pods stay in light alloy so they read.
    add(K.box("hvac", (-2.30, -0.66, ZT - 0.02), (-0.62, 0.66, 3.16), "metal", bevel=0.035, seg=2))
    for fx in (-1.88, -1.04):
        add(K.cyl("fan_ring", (fx, 0.0, 3.155), (fx, 0.0, 3.172), 0.27, "slate", sides=20))
        add(K.cyl("fan", (fx, 0.0, 3.168), (fx, 0.0, 3.176), 0.235, "rubber", sides=20))
        for a in (0.0, 60.0, 120.0):
            bar = K.box("fan_guard", (-0.24, -0.012, 3.172), (0.24, 0.012, 3.182), "metal", bevel=0.0)
            bar.data.transform(Matrix.Rotation(math.radians(a), 4, "Z"))
            bar.data.transform(Matrix.Translation(Vector((fx, 0.0, 0.0))))
            add(bar)
        add(K.cyl("fan_hub", (fx, 0.0, 3.17), (fx, 0.0, 3.184), 0.05, "slate", sides=10))
    for s in (-1, 1):
        ya, yb = sorted((s * 0.655, s * 0.672))
        for k in range(6):
            x0 = -2.16 + k * 0.25
            add(K.box("louvre", (x0, ya, 3.03), (x0 + 0.18, yb, 3.11), "slate", bevel=0.004, seg=1))
    add(K.box("battery", (-0.32, -0.56, ZT - 0.02), (1.12, 0.56, 3.14), "metal", bevel=0.03, seg=2))
    for k in range(6):                      # the ribs stop at x 0.82: the hazard label sits clear of them
        x0 = -0.22 + k * 0.20
        add(K.box("rib", (x0, -0.575, 3.02), (x0 + 0.04, 0.575, 3.15), "slate", bevel=0.006, seg=1))
    add(K.box("batt_label", (0.92, -0.30, 3.135), (1.06, 0.30, 3.146), "hazard", bevel=0.002, seg=1))
    # amber band across the roof at the cab, and the roof marker lamps beyond it
    add(K.box("roof_band", (1.56, -(HW - R_TOP) - 0.02, ZT - 0.01), (1.76, HW - R_TOP + 0.02, ZT + 0.008),
              "accent", bevel=0.003, seg=1))
    for s in (-1, 1):
        # amber gutter trim where the roof meets each shoulder: the car's outline from above
        ya, yb = sorted((s * (HW - R_TOP - 0.005), s * (HW - R_TOP + 0.04)))
        add(K.box("gutter", (-2.62, ya, ZT - 0.012), (1.52, yb, ZT + 0.007), "accent", bevel=0.003, seg=1))
        add(K.box("marker", (1.92, s * 0.62 - 0.07, ZT - 0.01), (2.02, s * 0.62 + 0.07, ZT + 0.03),
                  "glow", bevel=0.006, seg=1))
    # a slate roof walkway strip down the middle, and the antenna
    add(K.box("walkway", (-0.60, -0.16, ZT - 0.01), (-0.34, 0.16, ZT + 0.012), "slate", bevel=0.004, seg=1))
    add(K.cyl("antenna_base", (1.36, -0.42, ZT - 0.01), (1.36, -0.42, ZT + 0.03), 0.045, "slate", sides=10))
    add(K.cyl("antenna", (1.36, -0.42, ZT + 0.03), (1.36, -0.42, 3.17), 0.012, "pipe", sides=6))
    add(K.cyl("antenna_tip", (1.36, -0.42, 3.17), (1.36, -0.42, 3.20), 0.02, "metal", sides=8))

    for k, (frame, lip_ob, sill) in enumerate(door_parts):
        doorway(frame, lip_ob, sill, "side %d" % k)
    role_stats(P, CAR)
    ob = K.finish(P, CAR)
    lo, hi = bounds(ob)
    shift = Vector((-(lo.x + hi.x) / 2, -(lo.y + hi.y) / 2, -lo.z))
    assert shift.length < 0.01, "%s is off its contract pivot by %s" % (CAR, shift)
    ob.data.transform(Matrix.Translation(shift))
    ob.data.update()
    lo, hi = bounds(ob)
    dims = hi - lo
    # the assembled train: two cars nose to tail through the joint
    train = (2 * dims.x + 2 * JOINT_HALF, dims.y, dims.z)
    rh_hs.log("car %.3f x %.3f x %.3f m; the train %.3f x %.3f x %.3f m"
              % (dims.x, dims.y, dims.z, train[0], train[1], train[2]))
    assert abs(hi.x - XN) < 0.01 and abs(lo.x - XJ) < 0.01, "car X %s..%s, contract %s..%s" % (lo.x, hi.x, XJ, XN)
    assert 11.6 <= train[0] <= 12.4 and 2.5 <= train[1] <= 2.7 and 2.9 <= train[2] <= 3.3, \
        "unexpected train size %s" % (train,)
    visible_share(ob)
    return ob


# ============================================================== the joint
def build_joint():
    new_kit()
    P = []
    folds = 8
    x0, x1 = -0.42, 0.42
    secs = []
    for k in range(2 * folds + 1):
        x = x0 + (x1 - x0) * k / (2 * folds)
        out = k % 2 == 0
        w, zb, zt = (1.17, 0.42, 2.90) if out else (1.11, 0.48, 2.84)
        secs.append(ring(x, w, zb, zt, 0.30, 0.06, rows=(1.10, 1.70), ntop=3, nbot=1))
    P.append(loft("bellows", secs, "rubber", caps=(False, False)))
    # slate collars at both ends, inside the cars' own collars on straight track
    for xa, xb in ((-0.45, x0 + 0.004), (x1 - 0.004, 0.45)):
        P.append(loft("jcollar", [ring(x, 1.20, 0.39, 2.91, 0.31, 0.06, rows=(1.10, 1.70), ntop=3, nbot=1)
                                  for x in (xa, xb)], "slate"))
    # the walkway plate under the bellows: what the gangway floor rides on
    P.append(K.box("plate", (-0.40, -0.62, 0.36), (0.40, 0.62, 0.42), "slate", bevel=0.01, seg=1))
    role_stats(P, JOINT)
    ob = K.finish(P, JOINT)
    lo, hi = bounds(ob)
    assert abs(lo.x + 0.45) < 0.005 and abs(hi.x - 0.45) < 0.005 and abs(lo.y + hi.y) < 0.005, \
        "%s off its contract: %s %s" % (JOINT, lo, hi)
    return ob


def split_board_slot(ob, name, outdir, board_faces):
    """Give the board faces a second material and re-export: the import maps that
    slot to a child MI of the car's MI (the same baked atlas: the boards' texels
    are in it), which the game tints per line. The slot's GLB material is a plain
    colour on purpose: a copy of the baked one made Interchange import a second
    BaseColor and MetallicRoughness (RH_TramCar_BaseColor1, 2026-10-26)."""
    board = bpy.data.materials.new(name + "_Board")
    board.use_nodes = True
    board.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (*rh_hs.hex_lin("#E4E0D6"), 1.0)
    ob.data.materials.append(board)
    for p in ob.data.polygons:
        p.material_index = 1 if p.index in board_faces else 0
    glb = os.path.join(outdir, "%s.glb" % name)
    bpy.ops.object.select_all(action="DESELECT")
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.export_scene.gltf(filepath=glb, export_format="GLB", use_selection=True, export_apply=True)
    rh_hs.log("board slot: %d faces -> %s; re-exported %s" % (len(board_faces), board.name, glb))


# ===================================================================== main
if PART in ("both", "car"):
    car = build_car()
    out = export_preview(car, CAR)
    if not PREVIEW_ONLY:
        roles = [m["rh_role"] for m in car.data.materials]
        bidx = roles.index("board")
        faces = {p.index for p in car.data.polygons if p.material_index == bidx}
        files = K.bake_and_export(car, CAR, out, size=SIZE, dust=DUST)
        split_board_slot(car, CAR, out, faces)
        rh_hs.log("files: %s" % files)
if PART in ("both", "joint"):
    joint = build_joint()
    out = export_preview(joint, JOINT)
    if not PREVIEW_ONLY:
        files = K.bake_and_export(joint, JOINT, out, size=max(512, SIZE // 4), dust=DUST)
        rh_hs.log("files: %s" % files)
