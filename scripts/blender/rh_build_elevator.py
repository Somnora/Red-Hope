"""Build the colony ELEVATOR from code - cab + sliding door leaf (2026-10-14).

  blender -b -P scripts/blender/rh_build_elevator.py -- [--out Martians/gen/hs]
      [--part both|cab|door] [--size 2048] [--preview-only]

Two assets from one script. --out is the PARENT folder; each asset is written to
its own subfolder, <out>/RH_ElevatorCab and <out>/RH_ElevatorDoor. --part picks
one of them (default: both, one Blender process). --size is the cab's texture
size; the door leaf bakes at half of it (a 0.62 m leaf does not need 2K).
  RH_ElevatorCab   the car: solid walls, roof machinery, a pocket-door front wall
  RH_ElevatorDoor  ONE sliding door leaf; the game spawns it twice and slides the
                   two leaves open and closed in C++

Why: the director, on the 2026-10-13 triage page: "make sure that there are
elevator doors that open and close and people can get in and out easily, the
opening of the doorframe should be the size of the largest person plus a foot of
padding in every direction". The old RH_Elevator2 was an open lattice tower with
a cab perched on top, and its sliding panels were removed on 2026-08-17 because
"the doors are floating as they open and close... nothing encasing the doors".
The fix is a SOLID cab whose leaves slide INTO POCKETS INSIDE THE FRONT WALL, so
a leaf is always either filling the doorway or hidden in the wall - it can never
look detached. Design source: the elevator brief's geometry contract (below) and
the shipped kit family (AirFilter, CommandModule, humidity): bone-white panels,
dark-slate structural frame, light-alloy and rubber machinery, the function
accent (#F3C459, the shaft/elevator amber) as trim only.

GEOMETRY CONTRACT - the C++ that animates the leaves depends on these numbers.
Metres, Blender axes, Z up, TRUE SCALE (the game does NOT rescale the cab):
  - cab pivot = floor centre: bounds bottom at z = 0, footprint centred on XY 0.
  - exterior footprint X -1.10..+1.10, Y -1.45..+1.45; walls to 3.00 m, roof
    equipment capped at 3.30 m (floors are 4 m apart).
  - doorway on the -X face (it already faces the default camera, so this script
    does NOT call face_camera). Clear opening Y -0.61..+0.61, Z 0.02..2.24
    (1.88 m crew + 0.305 m headroom; 0.55 m shoulders + 2 x 0.305 m, rounded up),
    nothing inside it. A 2 cm metal threshold plate spans it at z 0..0.02.
  - pocket wall: solid X -1.10..-0.94 with a 6 cm cavity X -1.05..-0.99 reaching
    |Y| 0.61..1.30, Z 0.02..2.32 (one pocket each side, plus the 8 cm head slot
    the leaf tops run in). Outer skin and inner skin both have the doorway cut
    through; the cavity is closed everywhere except at the doorway gap.
  - door leaf: 0.62 (Y) x 2.24 (Z) x 0.04 (X), pivot at its bottom centre: X
    -0.02..+0.02, Y -0.31..+0.31, Z 0..2.24; leading (meeting) edge = local -Y,
    but the leaf is symmetric about its own Y centre too (both edges sealed and
    striped, slit centred), so it reads right whichever way Y is mapped.
  - C++ places leaf A at (-1.02, +0.31, 0.02) unrotated and leaf B at
    (-1.02, -0.31, 0.02) turned 180 deg about Z; open = Y +-0.93 (each leaf
    slides 0.62 m into its pocket). Closed, the leaves meet at Y 0 and overlap
    the jambs by 1 cm; open, they sit wholly inside the pockets.

Layout facts:
  - three exterior planes: the slate FRAME (corner posts, top rails, kick rails,
    side stiles) on the footprint planes; the casing / bezels 6 mm inside them;
    the bone-white panel skins 2-3 cm inside them. Nothing is proud of the
    footprint, so the bounds ARE the contract footprint.
  - front: a chunky slate U-casing round the opening (20 cm jambs, 20 cm head)
    with an accent lip on its inner edge and HazYellow chevrons rising toward the
    opening on both jambs; HazYellow nosing on the threshold's outer edge; a lit
    floor indicator (dark window, lit up-arrow, dark down-arrow, accent rim) on
    the viewer's right (-Y) of the casing; a matching lit indicator and a button
    plate inside.
  - the U casings bevel CONVEX edges only: a full bevel put 4-6 mm fillets into
    the clear opening's top corners (round-1 contract check).
  - the pocket cavity is lined in a dark "cavity" role (slate family, darker), so
    the slot that shows at the jambs and head reads as a deep pocket, never as a
    bone-white gap. The leaf runs on the threshold (its bottom sits at z 0.02).
  - interior (clear depth 1.96 m from the inner skin to the back wall, 2.66 m
    wide, 2.88 m to the ceiling): a "tread" floor (slate tone, unpainted, low
    dust - with the painted slate role the bake's cavity rust and dust made it
    read as planks) with gunmetal anti-slip ribs (light-alloy ribs read as a lit
    grill through the door), an accent handrail across the back wall (what the
    default camera sees through the open door), slate kick plates, a slate bench
    on the +Y wall, slate-framed inner window panes (round 1's bare panes read as
    bars hanging in the cab), and a light housing on the ceiling whose underside
    is the warm lit panel.
  - roof (the face the 54 deg game camera sees most): slate rim and a 10 cm
    accent band on all four top rails; a crosshead of two slate channels between
    the guide-rail shoes (C-jaws with rubber liners, accent markers) that sit on
    the side walls; the hoist sheave (r 0.22) between the channels in AMBER cheek
    blocks, with chunky rope stubs and cut-cable ferrules; an access grating in
    front of it; an accent-rimmed service hatch; an inspection box (HazYellow
    stop button) and a lit amber beacon at the front corners, both wired to the
    crosshead by clamped conduits. Round 1 had a slate sheave block, r 0.19, on
    one big plain bone deck: unreadable as machinery from 54 deg.
  - ROPE STUBS vs the 3.30 m cap: the stubs are modelled 0.50 m long (z 2.75..
    3.25 + a 5.5 cm ferrule to 3.30), but only ~0.31 m of each shows above the
    roof deck - the cap leaves no more. To get that much the sheave axle sits low
    (z 3.05, wheel top 3.27); the wheel's lower rim and the rope roots are hidden
    in the ceiling light housing (z 2.74..2.90), which is why that housing exists.
  - side walls: bone-white fields split by the car-frame stile, a narrow dark
    window in an accent rim in each field; back wall: an accent-rimmed louvre
    vent, a bolted access panel with an accent label, a slate mullion. Accent
    measured 6.0-7.2 % of the visible surface from every lineup yaw and from the
    54 deg camera (ray cast, cab alone).
  - one lit role ("glow", albedo #FFD8A0) for the ceiling panel, both floor
    indicators and the beacon lens; in game the cab's single EmissiveColor is
    warm white-amber (~4.5, 3.4, 2.0 linear). The door leaf has no glow.
  - the leaf: slate frame ring (the full 4 cm), light-alloy panel recessed 5 mm,
    a centred dark-glass slit in a rubber gasket, a HazYellow stripe inside each
    vertical edge and a rubber seal along each - symmetric in X (inside and
    outside faces look the same) and in Y (either edge can be the meeting edge).
  - every decal / lens / plate stands >= 4 mm proud (or sits >= 4 mm behind) the
    surface it shares a direction with; parts meet by butting or by >= 3 mm
    interpenetration, never by coplanar overlap.
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bmesh  # noqa: E402
import bpy  # noqa: E402
from mathutils import Matrix, Vector  # noqa: E402

import rh_hs  # noqa: E402

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OUT = argv[argv.index("--out") + 1] if "--out" in argv else "Martians/gen/hs"
SIZE = int(argv[argv.index("--size") + 1]) if "--size" in argv else 2048
PREVIEW_ONLY = "--preview-only" in argv
PART = argv[argv.index("--part") + 1] if "--part" in argv else "both"
assert PART in ("both", "cab", "door"), "--part must be both, cab or door"
CAB, DOOR = "RH_ElevatorCab", "RH_ElevatorDoor"
ACCENT = "#F3C459"                          # Borer / shaft / elevator amber (TintFor family)
DUST = 0.12                                 # underground, lit interior
SINK = 0.003                                # every bezel bites 3 mm into its support

# ------------------------------------------------ geometry contract (C++ owns these)
XF, XB, YW = -1.10, 1.10, 1.45             # footprint: X (doorway at -X), half width Y
ZW, ZCAP = 3.00, 3.30                      # wall top, roof-equipment cap
OY, OZ0, OZ1 = 0.61, 0.02, 2.24            # clear opening: half width, sill, head
PX0, PX1, PY, PZ1 = -1.05, -0.99, 1.30, 2.32   # pocket cavity X, reach |Y|, top
XI = -0.94                                 # inner face of the pocket wall
LEAF_HW, LEAF_H, LEAF_HT = 0.31, 2.24, 0.02    # leaf half width (Y), height, half thickness

# ------------------------------------------------------------- layout (free)
POST = 0.12                                # corner post section
XS = -1.07                                 # front outer-skin face (3 cm inside the frame plane)
YS = 1.43                                  # side skin face |y|
XBS = 1.08                                 # back skin face
XBI = 1.02                                 # back wall inner face
YI = 1.33                                  # side wall inner face |y|
RZ0 = 2.80                                 # top rails z 2.80..3.00
ZCEIL, ZDECK = 2.90, 2.985                 # ceiling (deck underside), deck top
CAS = 0.81                                 # outer casing half width (20 cm jambs)
CAS_Z = 2.44                               # outer casing top
CAS_X = -1.094                             # casing front face (6 mm inside the frame plane)
KICK = 0.16                                # kick rail height

K = None


def new_kit():
    """Fresh scene + role materials (Kit() resets the scene)."""
    global K
    K = rh_hs.Kit(accent_hex=ACCENT)
    # one lit role for the whole cab; albedo = the warm in-game glow so it reads
    # lit in daylight too. Preview emission = EmissiveColor normalised.
    K.roles["glow"] = dict(albedo="#FFD8A0", metal=0.0, rough=0.25, emit=1, dust=0.0)
    K.mats["glow"] = K._preview_material("glow", K.roles["glow"])
    gb = K.mats["glow"].node_tree.nodes["Principled BSDF"]
    gb.inputs["Emission Color"].default_value = (1.0, 0.756, 0.444, 1.0)
    gb.inputs["Emission Strength"].default_value = 1.5
    # the pocket lining: slate family, darker, so the slot reads as depth
    K.roles["cavity"] = dict(albedo="#34373C", metal=0.20, rough=0.70, dust=0.2)
    K.mats["cavity"] = K._preview_material("cavity", K.roles["cavity"])
    # cab floor: slate tone but unpainted (no chipping / cavity rust) and low dust -
    # with the painted slate role the baked floor read as brown planks
    K.roles["tread"] = dict(albedo="#555960", metal=0.35, rough=0.62, dust=0.35)
    K.mats["tread"] = K._preview_material("tread", K.roles["tread"])


# ------------------------------------------------------------------ helpers
AXES = {"x": (1, 2, 0), "y": (0, 2, 1), "z": (0, 1, 2)}   # (u, v, depth) indices


def _pt(axis, u, v, d):
    iu, iv, idd = AXES[axis]
    c = [0.0, 0.0, 0.0]
    c[iu], c[iv], c[idd] = u, v, d
    return c


def _bevel(bm, bevel, seg):
    """Bevel CONVEX edges only: bevelling a concave edge adds a fillet of
    material, which in the U casings put 4-6 mm fillets inside the clear
    opening's top corners (round-1 contract check)."""
    if bevel > 0:
        bmesh.ops.bevel(bm, geom=[e for e in bm.edges if e.is_convex], offset=bevel,
                        offset_type="OFFSET",
                        segments=seg, profile=0.5, affect="EDGES", clamp_overlap=True)
        bmesh.ops.dissolve_degenerate(bm, dist=1e-5, edges=bm.edges[:])
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])


def prism(name, poly, axis, d0, d1, role, bevel=0.0, seg=1):
    """Extrude a simple 2D polygon [(u, v)] along `axis` from d0 to d1. For axis
    x, (u, v) = (y, z); y: (x, z); z: (x, y). Concave outlines (the U casings)
    are fine: one ngon cap each end, bevelled as one solid."""
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
    """Rectangular ring (outer (u0, u1, v0, v1) minus inner) extruded along axis:
    one watertight solid, so frames have no butt seams at their corners."""
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


# Parts built on a -Y-facing virtual face (K.bezel's convention) are carried to
# any face of the cab by one rotation. BUILD[face](final point) is where to
# build so that the rotation lands the part on the final point.
ROT = {"-y": Matrix.Identity(4),
       "-x": Matrix.Rotation(math.radians(-90.0), 4, "Z"),
       "+y": Matrix.Rotation(math.radians(180.0), 4, "Z"),
       "+x": Matrix.Rotation(math.radians(90.0), 4, "Z"),
       "-z": Matrix.Rotation(math.radians(90.0), 4, "X"),
       "+z": Matrix.Rotation(math.radians(-90.0), 4, "X")}
BUILD = {"-y": lambda f: (f[0], f[1], f[2]),
         "-x": lambda f: (-f[1], f[0], f[2]),
         "+y": lambda f: (-f[0], -f[1], f[2]),
         "+x": lambda f: (f[1], -f[0], f[2]),
         "-z": lambda f: (f[0], f[2], -f[1]),
         "+z": lambda f: (f[0], -f[2], f[1])}
UNBUILD_SIGN = {"-y": 1, "-x": 1, "+y": -1, "+x": -1, "-z": 1, "+z": -1}


def fbezel(name, face, c, w, h, proud, radius, rim, recess, rim_role, inner_role):
    """K.bezel on any face of the cab. c = final centre ON the surface plane;
    w runs along the face's horizontal axis (Y on X faces, X on Y faces, X on
    the ceiling/roof), h along its vertical one (Z on walls, Y on ceiling/roof).
    Returns (object, window plane as a final coordinate on the normal axis)."""
    assert radius - rim >= 0.005, "%s: bezel rim %.3f must be < radius %.3f - 5 mm" % (
        name, rim, radius)
    b = BUILD[face](c)
    ob, wy = K.bezel(name, (b[0], b[1] + SINK, b[2]), w, h, proud + SINK, radius=radius,
                     rim=rim, recess=recess, rim_role=rim_role, inner_role=inner_role)
    ob.data.transform(ROT[face])
    return ob, UNBUILD_SIGN[face] * wy


def lathe(name, prof, centre, axis, sides, roles, seg_roles=None, phase=0.0):
    """Spin a CLOSED profile [(r, a)] about `axis` through `centre` (a = offset
    along the axis). r = 0 points become poles."""
    bm = bmesh.new()
    rings = []
    for (r, a) in prof:
        if r < 1e-6:
            v = bm.verts.new((0.0, 0.0, a))
            rings.append([v] * sides)
        else:
            rings.append([bm.verts.new((r * math.cos(phase + 2 * math.pi * i / sides),
                                        r * math.sin(phase + 2 * math.pi * i / sides), a))
                          for i in range(sides)])
    n = len(prof)
    for j in range(n):
        k = (j + 1) % n
        for i in range(sides):
            i1 = (i + 1) % sides
            vs = []
            for v in (rings[j][i], rings[j][i1], rings[k][i1], rings[k][i]):
                if v not in vs:
                    vs.append(v)
            if len(vs) < 3:
                continue
            f = bm.faces.new(vs)
            f.material_index = seg_roles[j] if seg_roles else 0
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    rot = Vector((0, 0, 1)).rotation_difference(Vector(axis).normalized()).to_matrix().to_4x4()
    bmesh.ops.transform(bm, matrix=Matrix.Translation(Vector(centre)) @ rot, verts=bm.verts)
    return K._obj(name, bm, roles)


def clip_v(poly, vlo, vhi):
    """Clip a convex polygon [(u, v)] to vlo <= v <= vhi (Sutherland-Hodgman)."""
    def clip(pts, keep, val):
        out = []
        for i in range(len(pts)):
            a, b = pts[i], pts[(i + 1) % len(pts)]
            if keep(a):
                out.append(a)
            if keep(a) != keep(b):
                t = (val - a[1]) / (b[1] - a[1])
                out.append((a[0] + (b[0] - a[0]) * t, val))
        return out
    p = clip(poly, lambda q: q[1] >= vlo, vlo)
    if len(p) < 3:
        return []
    p = clip(p, lambda q: q[1] <= vhi, vhi)
    return p if len(p) >= 3 else []


def chevrons(prefix, ua, ub, z0, z1, sgn, x0, x1, pitch=0.13, t=0.062):
    """45 deg HazYellow stripes on a jamb face |y| = ua..ub (sgn = side), rising
    toward the opening so the pair reads as a chevron pointing up at the head."""
    out = []
    span = ub - ua
    z = z0 - span - t
    k = 0
    while z < z1:
        poly = clip_v([(ub, z), (ub, z + t), (ua, z + t + span), (ua, z + span)], z0, z1)
        if poly:
            out.append(prism("%s%d" % (prefix, k), [(sgn * u, v) for u, v in poly], "x",
                             x0, x1, "hazard"))
        z += pitch
        k += 1
    return out


def bolts(out, prefix, pts, normal, r=0.013, h=0.010):
    n = Vector(normal)
    for p in pts:   # base 2 mm inside the surface, head 8 mm proud
        out.append(K.bolt(prefix, Vector(p) - n * 0.002, normal, r=r, h=h))


def check_bounds(ob, lo, hi, tol=0.001):
    vs = [v.co for v in ob.data.vertices]
    blo = [min(v[i] for v in vs) for i in range(3)]
    bhi = [max(v[i] for v in vs) for i in range(3)]
    rh_hs.log("%s bounds lo %s hi %s" % (ob.name, [round(c, 4) for c in blo],
                                          [round(c, 4) for c in bhi]))
    bad = [(i, blo[i], bhi[i]) for i in range(3)
           if abs(blo[i] - lo[i]) > tol or abs(bhi[i] - hi[i]) > tol]
    assert not bad, "%s breaks the geometry contract bounds: %s" % (ob.name, bad)


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


# =================================================================== the cab
def build_cab():
    new_kit()
    P = []
    add = P.append

    # ------------------------------------------------ car frame (slate, on the footprint)
    for sx in (-1, 1):
        for sy in (-1, 1):
            xa, xb = sorted((sx * (XB - POST), sx * XB))
            ya, yb = sorted((sy * (YW - POST), sy * YW))
            add(K.box("post", (xa, ya, 0.0), (xb, yb, ZW), "slate", bevel=0.012, seg=2))
    # top rails 6 mm inside the frame plane, an accent inlay band flush with the posts
    BZ0, BZ1 = 2.85, 2.95                   # 10 cm band: the cab's main trim line
    add(K.box("rail_f", (XF + 0.006, -YI, RZ0), (XF + POST, YI, ZW), "slate", bevel=0.008))
    add(K.box("band_f", (XF, -YI, BZ0), (XF + 0.012, YI, BZ1), "accent", bevel=0.003, seg=1))
    add(K.box("rail_b", (XBI, -YI, RZ0), (XB - 0.006, YI, ZW), "slate", bevel=0.008))
    add(K.box("band_b", (XB - 0.012, -YI, BZ0), (XB, YI, BZ1), "accent", bevel=0.003, seg=1))
    for s in (-1, 1):
        ya, yb = sorted((s * YI, s * (YW - 0.006)))
        add(K.box("rail_s", (XF + POST, ya, RZ0), (XB - POST, yb, ZW), "slate", bevel=0.008))
        ya, yb = sorted((s * (YW - 0.012), s * YW))
        add(K.box("band_s", (XF + POST, ya, BZ0), (XB - POST, yb, BZ1), "accent", bevel=0.003,
                  seg=1))
        # car-frame stile down the middle of each side wall (crosshead to floor)
        ya, yb = sorted((s * (YW - 0.04), s * YW))
        add(K.box("stile", (-0.10, ya, 0.0), (0.10, yb, RZ0), "slate", bevel=0.008))
        # kick rails either side of the stile
        for xa, xb in ((XF + POST, -0.10), (0.10, XB - POST)):
            add(K.box("kick_s", (xa, ya, 0.0), (xb, yb, KICK), "slate", bevel=0.006))
        # front kick rails between post and casing
        ya, yb = sorted((s * CAS, s * YI))
        add(K.box("kick_f", (XF, ya, 0.0), (XF + 0.038, yb, KICK), "slate", bevel=0.006))
    add(K.box("kick_b", (XB - 0.038, -YI, 0.0), (XB, YI, KICK), "slate", bevel=0.006))

    # ----------------------------------------------- side and back walls, deck, floor
    for s in (-1, 1):
        ya, yb = sorted((s * YI, s * YS))
        add(K.box("side_wall", (XF + POST, ya, 0.004), (XB - POST, yb, RZ0), "hull", bevel=0.006))
    add(K.box("back_wall", (XBI, -YI, 0.004), (XBS, YI, RZ0), "hull", bevel=0.006))
    add(K.box("deck", (XF + POST, -YI, ZCEIL), (XBI, YI, ZDECK), "hull", bevel=0.006))
    # floor plate (slate) flush with the threshold; its underside 2 mm up so it
    # never shares the z = 0 plane with the walls it butts against
    add(K.box("floor", (-0.92, -YI, 0.002), (XBI, YI, OZ0), "tread", bevel=0.0))
    for s in (-1, 1):
        ya, yb = sorted((s * OY, s * YI))
        add(K.box("floor_strip", (XI, ya, 0.002), (-0.92, yb, OZ0), "tread", bevel=0.0))
    # anti-slip ribs (4 mm proud) running toward the door; gunmetal, not light
    # alloy - round 2's light ribs read as a lit grill through the open door
    for k in range(-10, 11):
        y = k * 0.12
        add(K.box("rib", (-0.86, y - 0.011, 0.016), (0.97, y + 0.011, 0.024), "pipe",
                  bevel=0.0))
    # threshold: metal plate across the opening, HazYellow nosing on its outer edge
    add(K.box("threshold", (-1.07, -OY, 0.0), (-0.91, OY, OZ0), "metal", bevel=0.0))
    add(K.box("threshold_nose", (XF, -OY, 0.0), (-1.07, OY, OZ0), "hazard", bevel=0.0))

    # ------------------------------------------------------------ the pocket wall
    # outer skin: full width minus the casing notch; its pocket-side face is dark
    skin = prism("skin_out", [(-YI, 0.0), (-CAS, 0.0), (-CAS, CAS_Z), (CAS, CAS_Z), (CAS, 0.0),
                              (YI, 0.0), (YI, RZ0), (-YI, RZ0)], "x", XS, PX0, "hull", bevel=0.004)
    add(paint(skin, "cavity", lambda n, c: n.x > 0.9))
    # outer casing: chunky slate U round the opening (its back face lines the cavity)
    U = [(-CAS, 0.0), (-OY, 0.0), (-OY, OZ1), (OY, OZ1), (OY, 0.0), (CAS, 0.0), (CAS, CAS_Z),
         (-CAS, CAS_Z)]
    cas = prism("casing", U, "x", CAS_X, PX0, "slate", bevel=0.006)
    add(paint(cas, "cavity", lambda n, c: n.x > 0.9))
    # accent lip on the casing's inner edge (2 mm back from the opening edge)
    LIP = 0.035
    add(prism("casing_lip", [(-OY - LIP, 0.0), (-OY - 0.002, 0.0), (-OY - 0.002, OZ1 + 0.002),
                             (OY + 0.002, OZ1 + 0.002), (OY + 0.002, 0.0), (OY + LIP, 0.0),
                             (OY + LIP, OZ1 + LIP), (-OY - LIP, OZ1 + LIP)],
              "x", XF, CAS_X + 0.006, "accent", bevel=0.002))
    # hazard chevrons on both jambs, rising toward the opening
    for s in (-1, 1):
        P.extend(chevrons("chev%d_" % s, OY + LIP + 0.02, CAS - 0.018, 0.06, OZ1 - 0.04, s,
                          XF, CAS_X + 0.006))
    # inner skin: full width minus the doorway; pocket face dark, jamb reveal slate
    sk_in = prism("skin_in", [(-YI, 0.0), (-OY, 0.0), (-OY, OZ1), (OY, OZ1), (OY, 0.0), (YI, 0.0),
                              (YI, ZCEIL), (-YI, ZCEIL)], "x", PX1, XI, "hull", bevel=0.004)
    paint(sk_in, "cavity", lambda n, c: n.x < -0.9)
    paint(sk_in, "slate", lambda n, c: (abs(n.y) > 0.9 and abs(abs(c.y) - OY) < 0.01)
          or (n.z < -0.9 and abs(c.z - OZ1) < 0.01))
    add(sk_in)
    # cavity closures: pocket ends, sills under the pockets, the top over the head slot
    for s in (-1, 1):
        ya, yb = sorted((s * PY, s * YI))
        add(K.box("pkt_end", (PX0, ya, 0.0), (PX1, yb, RZ0), "cavity", bevel=0.0))
        ya, yb = sorted((s * OY, s * PY))
        add(K.box("pkt_sill", (PX0, ya, 0.0), (PX1, yb, OZ0), "cavity", bevel=0.0))
    add(K.box("pkt_top", (PX0, -PY, PZ1), (PX1, PY, RZ0), "cavity", bevel=0.0))
    # inner casing: slate U, 3 cm proud of the inner skin
    add(prism("casing_in", [(-0.75, 0.0), (-OY, 0.0), (-OY, OZ1), (OY, OZ1), (OY, 0.0), (0.75, 0.0),
                            (0.75, 2.36), (-0.75, 2.36)], "x", XI, -0.91, "slate", bevel=0.005))

    # ----------------------------------------------- front furniture (outside)
    front = []
    # lit floor indicator, viewer's right (-Y) of the casing at eye level
    IY, IZ = -1.07, 1.60
    ind, wx = fbezel("ind_out", "-x", (XS, IY, IZ), 0.18, 0.30, 0.03, radius=0.04, rim=0.028,
                     recess=0.014, rim_role="accent", inner_role="glass")
    front.append(ind)
    up = [(IY - 0.045, IZ + 0.035), (IY + 0.045, IZ + 0.035), (IY, IZ + 0.09)]
    dn = [(IY - 0.045, IZ - 0.035), (IY, IZ - 0.09), (IY + 0.045, IZ - 0.035)]
    front.append(prism("ind_up", up, "x", wx - 0.005, wx + 0.003, "glow"))
    front.append(prism("ind_dn", dn, "x", wx - 0.005, wx + 0.003, "metal"))
    bolts(front, "fbolt", [(XS, y, z) for y in (-1.27, -0.87, 0.87, 1.27) for z in (0.26, 2.70)],
          (-1, 0, 0))
    P.extend(front)

    # ------------------------------------------------ side walls: windows, bolts
    for s in (-1, 1):
        face = "+y" if s > 0 else "-y"
        for xc in (-0.54, 0.54):
            w, _ = fbezel("win", face, (xc, s * YS, 1.72), 0.18, 0.92, YW - YS, radius=0.05,
                          rim=0.03, recess=0.016, rim_role="accent", inner_role="glass")
            add(w)
            # inside: a slate frame 1 cm proud of the wall and a dark pane 5 mm proud
            # (round 1's bare pane read as a bar hanging in the cab, seen through the door)
            ya, yb = sorted((s * (YI + 0.003), s * (YI - 0.010)))
            add(rect_frame("win_in_frame", "y", ya, yb, (xc - 0.08, xc + 0.08, 1.26, 2.18),
                           (xc - 0.05, xc + 0.05, 1.29, 2.15), "slate", bevel=0.003))
            ya, yb = sorted((s * (YI + 0.003), s * (YI - 0.005)))
            add(K.box("win_in", (xc - 0.05, ya, 1.29), (xc + 0.05, yb, 2.15), "glass", bevel=0.0))
        bolts(P, "sbolt", [(x, s * YS, z) for x in (-0.92, -0.16, 0.16, 0.92) for z in (0.26, 2.70)],
              (0, s, 0))

    # ------------------------------------------- back wall: vent, access panel, mullion
    add(K.box("mullion_b", (XB - 0.038, -0.08, KICK), (XB, 0.08, RZ0), "slate", bevel=0.006))
    vent, vx = fbezel("vent_b", "+x", (XBS, -0.66, 2.30), 0.62, 0.40, XB - XBS, radius=0.05,
                      rim=0.035, recess=0.014, rim_role="accent", inner_role="rubber")
    add(vent)
    for k in range(5):
        z = 2.30 - 0.14 + k * 0.07
        add(K.box("vent_slat", (vx - 0.003, -0.93, z - 0.013), (vx + 0.010, -0.39, z + 0.013),
                  "metal", bevel=0.003, seg=1))
    pan, px = fbezel("panel_b", "+x", (XBS, 0.66, 1.30), 0.70, 1.70, 0.016, radius=0.05,
                     rim=0.035, recess=0.008, rim_role="slate", inner_role="hull")
    add(pan)
    add(K.box("panel_label", (px - 0.003, 0.48, 1.94), (px + 0.006, 0.84, 2.00), "accent",
              bevel=0.002, seg=1))
    bolts(P, "bbolt", [(XBS, y, z) for y in (-1.27, -0.14, 0.14, 1.27) for z in (0.26, 2.70)],
          (1, 0, 0))

    # --------------------------------------------------------------------- roof
    # crosshead: two slate channels back to back, the sheave runs between them
    for s in (-1, 1):
        xa, xb = sorted((s * 0.08, s * 0.17))
        add(K.box("crosshead", (xa, -1.36, 2.975), (xb, 1.36, 3.09), "slate", bevel=0.008))
    # sheave block: cheek blocks over the channels, axle bosses, the grooved wheel
    ZC, RS = 3.05, 0.22                      # wheel top 3.27; lower rim hidden in the housing
    for s in (-1, 1):
        xa, xb = sorted((s * 0.074, s * 0.18))
        add(K.box("cheek", (xa, -0.13, 2.98), (xb, 0.13, 3.16), "accent", bevel=0.01,
                  chamfers=[(lambda d, m: abs(d.x) > 0.99 and m.z > 3.15, 0.04)]))
        add(K.cyl("axle", (s * 0.17, 0.0, ZC), (s * 0.215, 0.0, ZC), 0.055, "metal", sides=16))
        add(K.cyl("axle_nut", (s * 0.21, 0.0, ZC), (s * 0.232, 0.0, ZC), 0.035, "accent", sides=6))
    RG = RS - 0.022                          # groove bottom radius
    wheel = [(0.05, -0.05), (RS - 0.05, -0.05), (RS - 0.04, -0.064), (RS - 0.008, -0.064),
             (RS, -0.056), (RG, -0.032), (RS - 0.006, -0.008), (RS - 0.006, 0.008), (RG, 0.032),
             (RS, 0.056), (RS - 0.008, 0.064), (RS - 0.04, 0.064), (RS - 0.05, 0.05), (0.05, 0.05)]
    add(lathe("sheave", wheel, (0.0, 0.0, ZC), (1, 0, 0), 28, ["metal"]))
    add(K.cyl("hub", (-0.09, 0.0, ZC), (0.09, 0.0, ZC), 0.055, "metal", sides=16))
    # rope stubs: chunky (r 2.4 cm), in the grooves, cut-cable ferrules at the cap
    RR = 0.024
    for sx in (-1, 1):
        for sy in (-1, 1):
            x, y = sx * 0.032, sy * (RG + RR)
            add(K.cyl("rope", (x, y, 2.75), (x, y, 3.25), RR, "pipe", sides=10))
            add(K.cyl("ferrule", (x, y, 3.245), (x, y, ZCAP), 0.033, "metal", sides=12))
    # guide-rail shoes on the side walls (C-jaws, rubber liners, accent marker)
    for s in (-1, 1):
        ya, yb = sorted((s * 1.20, s * 1.36))
        add(K.box("shoe_base", (-0.18, ya, 2.98), (0.18, yb, 3.16), "slate", bevel=0.01))
        add(K.box("shoe_mark", (-0.12, min(s * 1.24, s * 1.32), 3.155),
                  (0.12, max(s * 1.24, s * 1.32), 3.166), "accent", bevel=0.002, seg=1))
        ya, yb = sorted((s * 1.355, s * 1.440))
        for t in (-1, 1):
            xa, xb = sorted((t * 0.035, t * 0.15))
            add(K.box("shoe_jaw", (xa, ya, 2.995), (xb, yb, 3.14), "metal", bevel=0.008))
            xa, xb = sorted((t * 0.029, t * 0.041))
            add(K.box("shoe_liner", (xa, min(s * 1.365, s * 1.438), 3.005),
                      (xb, max(s * 1.365, s * 1.438), 3.13), "rubber", bevel=0.002, seg=1))
    # service hatch (accent rim, bone lid, handle, hinges), back-left of the roof
    add(rect_frame("hatch_frame", "z", 2.975, 3.035, (0.32, 0.86, 0.40, 0.98),
                   (0.37, 0.81, 0.45, 0.93), "accent", bevel=0.006))
    add(K.box("hatch_lid", (0.372, 0.452, 2.97), (0.808, 0.928, 3.024), "hull", bevel=0.006))
    add(K.pipe("hatch_handle", [(0.52, 0.69, 3.015), (0.52, 0.69, 3.07), (0.66, 0.69, 3.07),
                                (0.66, 0.69, 3.015)], 0.025, "metal", bend=0.03, sides=8, samples=3))
    for y0 in (0.50, 0.80):
        add(K.box("hatch_hinge", (0.845, y0, 2.98), (0.90, y0 + 0.08, 3.055), "slate", bevel=0.006,
                  seg=1))
    # inspection box with a HazYellow stop button, front-right of the roof
    add(K.box("insp_box", (-0.86, -1.22, 2.975), (-0.58, -0.92, 3.13), "slate", bevel=0.01,
              chamfers=[(lambda d, m: abs(d.y) > 0.99 and m.x < -0.85 and m.z > 3.12, 0.04)]))
    add(K.cyl("insp_stop", (-0.70, -1.07, 3.12), (-0.70, -1.07, 3.158), 0.04, "hazard", sides=16))
    add(K.box("insp_label", (-0.866, -1.18, 2.995), (-0.854, -0.96, 3.065), "accent", bevel=0.002,
              seg=1))
    # amber warning beacon, front-left corner of the roof (lit lens)
    BX, BY = -0.78, 1.08
    add(K.cyl("beacon_base", (BX, BY, 2.975), (BX, BY, 3.05), 0.085, "slate", sides=16))
    add(K.cyl("beacon_ring", (BX, BY, 3.045), (BX, BY, 3.077), 0.075, "accent", sides=16))
    add(lathe("beacon_lens", [(0.0, 3.07), (0.058, 3.07), (0.058, 3.14), (0.045, 3.175),
                              (0.0, 3.186)], (BX, BY, 0.0), (0, 0, 1), 16, ["glow"]))
    add(K.cyl("beacon_cap", (BX, BY, 3.178), (BX, BY, 3.20), 0.03, "slate", sides=12))
    # access grating in front of the sheave (where a technician stands): slate
    # frame, dark well, light-alloy bars running toward the camera
    add(rect_frame("grating_frame", "z", 2.978, 2.998, (-0.66, -0.24, -0.50, 0.50),
                   (-0.63, -0.27, -0.47, 0.47), "slate", bevel=0.004))
    add(K.box("grating_well", (-0.632, -0.472, 2.980), (-0.268, 0.472, 2.989), "rubber", bevel=0.0))
    for k in range(12):
        y = -0.44 + k * 0.08
        add(K.box("grating_bar", (-0.632, y - 0.011, 2.983), (-0.268, y + 0.011, 2.994), "metal",
                  bevel=0.0))
    # conduits: inspection box and beacon wired to the crosshead, clamped to the deck
    for y, x0 in ((-1.07, -0.585), (1.08, BX + 0.08)):
        add(K.cyl("conduit", (x0, y, 3.005), (-0.165, y, 3.005), 0.024, "pipe", sides=10))
        add(K.box("conduit_clamp", (-0.43, y - 0.035, 2.98), (-0.39, y + 0.035, 3.034), "slate",
                  bevel=0.004, seg=1))

    # ----------------------------------------------------------------- interior
    # ceiling light housing (hides the sheave's lower rim + rope roots); its
    # underside is the warm lit panel
    add(K.box("light_housing", (-0.42, -0.60, 2.74), (0.42, 0.60, ZCEIL + 0.005), "hull",
              bevel=0.01))
    lp, _ = fbezel("ceiling_light", "-z", (0.0, 0.0, 2.74), 0.72, 1.08, 0.02, radius=0.05,
                   rim=0.035, recess=0.008, rim_role="slate", inner_role="glow")
    add(lp)
    # kick plates
    add(K.box("ikick_b", (XBI - 0.015, -YI, 0.0), (XBI + 0.005, YI, KICK), "slate", bevel=0.004,
              seg=1))
    for s in (-1, 1):
        ya, yb = sorted((s * (YI - 0.015), s * (YI + 0.005)))
        add(K.box("ikick_s", (XI, ya, 0.0), (XBI - 0.015, yb, KICK), "slate", bevel=0.004, seg=1))
    # back-wall handrail (accent), ends buried in the wall, one slate standoff
    HZ = 0.95
    add(K.pipe("handrail", [(XBI + 0.005, -1.12, HZ), (0.95, -1.12, HZ), (0.95, 1.12, HZ),
                            (XBI + 0.005, 1.12, HZ)], 0.025, "accent", bend=0.06, sides=12,
               samples=4))
    add(K.box("rail_standoff", (0.955, -0.03, HZ - 0.07), (XBI + 0.004, 0.03, HZ - 0.01), "slate",
              bevel=0.004, seg=1))
    # bench on the +Y wall
    add(K.box("bench_seat", (-0.20, 0.93, 0.42), (0.70, YI + 0.004, 0.48), "slate", bevel=0.008))
    add(K.box("bench_pad", (-0.17, 0.96, 0.474), (0.67, YI - 0.004, 0.52), "rubber", bevel=0.01))
    for x0 in (-0.15, 0.60):
        add(K.box("bench_leg", (x0, 0.97, 0.01), (x0 + 0.05, YI + 0.004, 0.425), "slate",
                  bevel=0.005, seg=1))
    # lit floor indicator + button plate inside, beside the doorway (-Y side)
    ii, _ = fbezel("ind_in", "+x", (XI, -0.98, 1.50), 0.15, 0.24, 0.025, radius=0.035, rim=0.025,
                   recess=0.01, rim_role="slate", inner_role="glow")
    add(ii)
    add(K.box("btn_plate", (XI - 0.003, -1.04, 1.10), (XI + 0.012, -0.92, 1.32), "slate",
              bevel=0.004, seg=1))
    for z in (1.15, 1.21, 1.27):
        add(K.cyl("btn", (XI + 0.010, -0.98, z), (XI + 0.019, -0.98, z), 0.017, "metal", sides=10))

    role_stats(P, CAB)
    ob = K.finish(P, CAB)
    check_bounds(ob, (XF, -YW, 0.0), (XB, YW, ZCAP))
    return ob


# ============================================================== the door leaf
def build_door():
    new_kit()
    P = []
    add = P.append
    HW, H, HT = LEAF_HW, LEAF_H, LEAF_HT
    YL = HW - 0.017                         # frame's edge faces |y|; the seals cover YL..HW
    # Symmetric about X AND about the leaf's own Y centre (2026-10-14 spec change:
    # the leaf must look right whichever way the import maps Blender Y), so both
    # vertical edges carry a seal and a hazard stripe and the slit is centred.
    # slate frame ring, the full 4 cm thickness (kick rail = its 15 cm bottom bar)
    add(rect_frame("leaf_frame", "x", -HT, HT, (-YL, YL, 0.0, H), (-0.25, 0.25, 0.15, 2.17),
                   "slate", bevel=0.004, seg=2))
    # light-alloy panel, 5 mm recessed in the frame, its edges buried 1 cm in it
    add(K.box("leaf_panel", (-0.015, -0.26, 0.14), (0.015, 0.26, 2.18), "metal", bevel=0.0))
    for s in (-1, 1):
        # rubber seal along each vertical edge, 3 mm into the frame
        ya, yb = sorted((s * HW, s * (YL - 0.003)))
        add(K.box("leaf_seal", (-0.014, ya, 0.0), (0.014, yb, H), "rubber", bevel=0.004))
        # HazYellow stripe inside each edge, one solid proud of both faces (4 mm)
        ya, yb = sorted((s * 0.206, s * 0.236))
        add(K.box("leaf_hazard", (-0.019, ya, 0.16), (0.019, yb, 2.16), "hazard",
                  bevel=0.002, seg=1))
    # centred window slit: rubber gasket ring + dark glass, both faces
    add(rect_frame("leaf_gasket", "x", -0.0198, 0.0198, (-0.06, 0.06, 1.02, 1.92),
                   (-0.042, 0.042, 1.04, 1.90), "rubber", bevel=0.003))
    add(K.box("leaf_glass", (-0.0185, -0.042, 1.04), (0.0185, 0.042, 1.90), "glass", bevel=0.0))
    role_stats(P, DOOR)
    ob = K.finish(P, DOOR)
    check_bounds(ob, (-HT, -HW, 0.0), (HT, HW, H))
    return ob


# ===================================================================== main
if PART in ("both", "cab"):
    cab = build_cab()
    out = export_preview(cab, CAB)
    if not PREVIEW_ONLY:
        files = K.bake_and_export(cab, CAB, out, size=SIZE, dust=DUST)
        rh_hs.log("files: %s" % files)
if PART in ("both", "door"):
    door = build_door()
    out = export_preview(door, DOOR)
    if not PREVIEW_ONLY:
        files = K.bake_and_export(door, DOOR, out, size=max(512, SIZE // 2), dust=DUST)
        rh_hs.log("files: %s" % files)
