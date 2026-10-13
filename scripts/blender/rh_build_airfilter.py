"""Build the AirFilter from code - the hard-surface remake (2026-10-13).

  blender -b -P scripts/blender/rh_build_airfilter.py -- [--out Martians/gen/hs/AirFilter]
      [--size 2048] [--preview-only]

Replaces the image-to-3D AirFilter the director flagged "weird" on the
2026-10-13 triage (a puffy, melted, teal-tinted blob whose fan had dissolved
into noise). The DESIGN is kept: Martians/gen/airfilter/airfilter.png, the
approved hero reference - an industrial air-handler cabinet: bone-white box
with chamfered vertical edges, a BIG round fan behind a guard grille (visible
blades and hub), a teal LED strip across the top front, an AIR marking, twin
inverted-U pipes on top, a side panel with louvre vents and a recessed service
panel, four short square legs. Only the construction changes: every part is an
exact primitive, the fan guard is real rings and spokes in front of real blades
in a dark tunnel, and the emissive mask is the LED strip and the status lamp.

Layout facts:
  - built with the fan face at -Y, then K.face_camera() turns it to -X, the
    face the game's default strategy camera meets (measured 2026-10-13).
  - the FAN must read from the game's real camera (pawn pitch ~54 deg at the
    default zoom), not just from the low lineup camera. Round 1 failed that:
    4 guard rings at 8 cm pitch stacked into a solid ribbed disc at 54 deg and
    mid-grey blades 7 cm back sank into the black tunnel. Now: 3 rings at 12 cm
    pitch (wire r 15 mm), 5 broad LIGHT blades at low pitch pulled forward to
    ~5 mm behind the spokes, and a light rotor spinner instead of a solid guard
    disc. Measured on the flagged-material render (blades vs guard pixels):
    0.38 at 54 deg with a near perspective camera, 0.56 with a far ortho one
    (round 1: 0.06).
  - HEIGHT <= WIDTH on purpose. The visualizer fits the larger horizontal
    extent (the fan face's width here) to the 2-cell footprint (4 m) and the
    AirFilter mostly stands on a dug floor 4 m tall (RH_Config FloorHeightMeters),
    so the reference's tall cabinet would poke ~1-3 m out of its own pit. This
    one is ~0.98 x its width tall: ~3.9 m in game. The fan keeps >= 55 % of the
    cabinet width, so the identity survives the squatter box. The pipe loops
    take what height is left (0.30 m clear of the top).
  - stance: four square legs whose outer faces sit 1 cm inside the cabinet
    faces, so their full height shows below the box even from 54 deg, with
    hazard kick plates on the front/back faces; a recessed sub-frame in shadow
    ties them (round 1's full-footprint plinth read as "a box on a skid").
  - palette follows the family (art bible 1: bone-white hulls, slate
    machinery): hull cabinet, door and side louvres; DarkSlate fan plate and
    shroud, legs, sub-frame, AIR placard, top exhaust slats, pipes (the
    0.75-metallic gunmetal role read near-black). Light alloy guard and blades
    on a dark shroud and black tunnel is what makes the fan read at thumbnail size.
  - accent (#95EDE7, TintFor AirFilter) is TRIM only, 6-12 % of the visible
    surface from every orbit view (ray-cast): the fan shroud's lip ring, the LED
    bezel, raised AIR letters, the hub ring, the status-lamp ring, and every
    AIR-PATH frame - top exhaust grille, both side louvres, the back filter
    cartridge - so teal reads as "air goes here". The LED sits in a wide slate
    channel so the lit strip never merges with its same-hue bezel.
  - fan nearly centred (reference), AIR placard above its upper-right shoulder,
    status lamp lower right. Viewer's right of the fan face (+X before the
    turn) is the reference side: framed recessed service panel over a louvre
    vent. The other side is a tall intake louvre bank. The back carries the
    pleated filter cartridge (what the machine is FOR) and the pipe returns; the
    top carries an exhaust grille (identity from steep vault cameras) and the
    two pipe roots.
  - lit elements use cell_on, whose albedo matches the asset's TealGlow
    EmissiveColor; the emissive mask is the LED strip and the lamp only.
  - the cabinet is one convex solid (half-space intersection + hull), not a
    chamfered box: stacked bevel ops left two pinholes at the front-top corners.
  - every bezel goes through bezel(), which refuses rim >= radius - 5 mm:
    round 1's back filter frame (rim > radius) folded over itself and its
    service panel (rim == radius) collapsed the window corners to points.
Real-world metres, ground-centred pivot (bounds centred on XY, bottom at z=0).
"""
import itertools
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bmesh  # noqa: E402
import bpy  # noqa: E402
from mathutils import Matrix, Vector  # noqa: E402

import rh_hs  # noqa: E402

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OUT = argv[argv.index("--out") + 1] if "--out" in argv else "Martians/gen/hs/AirFilter"
SIZE = int(argv[argv.index("--size") + 1]) if "--size" in argv else 2048
PREVIEW_ONLY = "--preview-only" in argv
NAME = "AirFilter"

K = rh_hs.Kit(accent_hex="#95EDE7")       # TintFor(AirFilter), art bible 2.3
# fan blades: LIGHT, low-dust alloy. They sit in a black tunnel under heavy AO,
# so a mid grey (round 1: #8A9098) baked to near-black and the blades vanished
# behind the guard at the game's 54 deg pitch; light blades on black read as
# wedges even through the guard.
K.roles["blade"] = dict(albedo="#B0B5BA", metal=0.25, rough=0.45, dust=0.3)
K.mats["blade"] = K._preview_material("blade", K.roles["blade"])
parts = []
add = parts.append


# ------------------------------------------------------------ local helpers
def convex_block(name, planes, role, bevel=0.012, seg=2):
    """Closed convex solid = intersection of half-spaces n.p <= d, bevelled.
    One watertight hull however many chamfers meet at a corner."""
    pts = []
    for (n1, d1), (n2, d2), (n3, d3) in itertools.combinations(planes, 3):
        M = Matrix((n1, n2, n3))
        if abs(M.determinant()) < 1e-9:
            continue
        p = M.inverted() @ Vector((d1, d2, d3))
        # mathutils is float32: a 1e-7 tolerance silently dropped the corners
        # where the front-top chamfer meets the front (tilted the whole face)
        if all(Vector(n).dot(p) <= d + 1e-5 for n, d in planes):
            if all((p - q).length > 1e-4 for q in pts):
                pts.append(p)
    bm = bmesh.new()
    vs = [bm.verts.new(p) for p in pts]
    rh_hs.log("convex_block %s: %d corner vertices" % (name, len(pts)))
    res = bmesh.ops.convex_hull(bm, input=vs)
    bmesh.ops.delete(bm, geom=res["geom_interior"] + res["geom_unused"], context="VERTS")
    bmesh.ops.dissolve_limit(bm, angle_limit=math.radians(0.5), verts=bm.verts[:],
                             edges=bm.edges[:])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    if bevel > 0:
        bmesh.ops.bevel(bm, geom=list(bm.edges), offset=bevel, offset_type="OFFSET",
                        segments=seg, profile=0.5, affect="EDGES", clamp_overlap=True)
        # where three bevels meet at the chamfered top corners the bevel leaves
        # ~1e-7 m2 slivers (round-1 audit: 4 tris, 3 non-manifold edges)
        bmesh.ops.dissolve_degenerate(bm, dist=1e-4, edges=bm.edges[:])
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    return K._obj(name, bm, [role])


def bezel(name, center, w, h, proud, radius, rim, recess, rim_role, inner_role, facing=-1):
    """K.bezel with its failure modes refused: rim >= radius folds the inset
    over itself at the corners (rim > radius: flipped, z-fighting faces) or
    collapses the window corners to a point (rim == radius: degenerate tris).
    Round 1 shipped both; keep the window corner radius >= 5 mm."""
    assert radius - rim >= 0.005, "%s: bezel rim %.3f must be < radius %.3f - 5 mm" % (
        name, rim, radius)
    return K.bezel(name, center, w, h, proud, radius=radius, rim=rim, recess=recess,
                   rim_role=rim_role, inner_role=inner_role, facing=facing)


def lathe(name, prof, centre, sides, roles, seg_roles=None):
    """Spin a CLOSED profile [(r, d)] about an axis parallel to Y through
    `centre`; d is the distance toward -Y (the front). seg_roles[j] is the
    material index of profile segment j -> j+1."""
    cx, cy, cz = centre
    bm = bmesh.new()
    rings = []
    for i in range(sides):
        th = 2.0 * math.pi * i / sides
        rings.append([bm.verts.new((cx + r * math.cos(th), cy - d, cz + r * math.sin(th)))
                      for (r, d) in prof])
    n = len(prof)
    for i in range(sides):
        a, b = rings[i], rings[(i + 1) % sides]
        for j in range(n):
            k = (j + 1) % n
            f = bm.faces.new((a[j], b[j], b[k], a[k]))
            f.material_index = seg_roles[j] if seg_roles else 0
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    return K._obj(name, bm, roles)


def torus_y(name, centre, R, r, role, major=40, minor=8):
    """Ring lying in the XZ plane (facing -Y), tube radius r."""
    prof = [(R + r * math.cos(2 * math.pi * k / minor), r * math.sin(2 * math.pi * k / minor))
            for k in range(minor)]
    return lathe(name, prof, centre, major, [role])


def obox(name, ctr, half, rot, role, bevel=0.0, seg=1):
    """Box of half extents `half`, rotated by 3x3 `rot`, centred at `ctr`."""
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    for v in bm.verts:
        v.co = Vector((v.co.x * 2 * half[0], v.co.y * 2 * half[1], v.co.z * 2 * half[2]))
    if bevel > 0:
        bmesh.ops.bevel(bm, geom=list(bm.edges), offset=bevel, offset_type="OFFSET",
                        segments=seg, profile=0.5, affect="EDGES", clamp_overlap=True)
    bmesh.ops.transform(bm, matrix=Matrix.Translation(Vector(ctr)) @ rot.to_4x4(), verts=bm.verts)
    return K._obj(name, bm, [role])


def text_block(name, body, centre, size, y_front, y_back, role, offset=0.0, spacing=1.0):
    """Raised lettering on a -Y-facing surface: the builtin font as one solid
    per glyph (counters stay open), front plane y_front, back y_back. Keep
    `spacing` up when `offset` thickens strokes, or neighbouring glyphs touch
    and their coplanar fronts z-fight."""
    cu = bpy.data.curves.new(name + "_c", "FONT")
    cu.body = body
    cu.size = size
    cu.offset = offset                     # thickens the strokes (bolder read)
    cu.space_character = spacing
    cu.extrude = (y_back - y_front) / 2.0
    cu.resolution_u = 3
    cu.align_x = "CENTER"
    cu.align_y = "CENTER"
    tmp = bpy.data.objects.new(name + "_tmp", cu)
    bpy.context.scene.collection.objects.link(tmp)
    dg = bpy.context.evaluated_depsgraph_get()
    me = bpy.data.meshes.new_from_object(tmp.evaluated_get(dg))
    bpy.data.objects.remove(tmp)
    bpy.data.curves.remove(cu)
    # glyph plane XY (front +Z) -> XZ plane facing -Y, then onto the surface
    vs = [v.co for v in me.vertices]
    cx0 = (min(v.x for v in vs) + max(v.x for v in vs)) / 2
    cz0 = (min(v.y for v in vs) + max(v.y for v in vs)) / 2
    me.transform(Matrix.Translation((centre[0] - cx0, (y_front + y_back) / 2, centre[2] - cz0))
                 @ Matrix.Rotation(math.radians(90.0), 4, "X"))
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    me.materials.clear()
    me.materials.append(K.mats[role])
    return ob


def blade(name, centre, phi, stations, th, role):
    """One fan blade lofted through radial stations (rho, half_chord, pitch_deg,
    sweep); axis -Y. Thickness th (m)."""
    cx, cy, cz = centre
    er = Vector((math.cos(phi), 0.0, math.sin(phi)))
    et = Vector((-math.sin(phi), 0.0, math.cos(phi)))
    ea = Vector((0.0, -1.0, 0.0))
    bm = bmesh.new()
    secs = []
    for rho, hw, pitch, sweep in stations:
        p = math.radians(pitch)
        chord = et * math.cos(p) + ea * math.sin(p)
        nrm = -et * math.sin(p) + ea * math.cos(p)
        c = Vector((cx, cy, cz)) + er * rho + et * sweep
        secs.append([bm.verts.new(c + chord * s * hw + nrm * q * th / 2)
                     for s, q in ((-1, -1), (1, -1), (1, 1), (-1, 1))])
    for a, b in zip(secs, secs[1:]):
        for i in range(4):
            j = (i + 1) % 4
            bm.faces.new((a[i], a[j], b[j], b[i]))
    bm.faces.new(secs[0])
    bm.faces.new(list(reversed(secs[-1])))
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    return K._obj(name, bm, [role])


def turn(objs, deg, axis="Z"):
    """Rotate freshly built parts about a world axis: parts built on a
    -Y-facing 'virtual face' are carried onto a side (Z +-90) or the top
    (X -90: virtual plane y=-h lands on z=h, in-plane z becomes y)."""
    M = Matrix.Rotation(math.radians(deg), 4, axis)
    for o in objs:
        o.data.transform(M)
    return objs


def recentre(ob):
    """Bounds centred on XY, bottom at z=0 (the visualizer's contract)."""
    vs = [v.co for v in ob.data.vertices]
    lo = Vector((min(v.x for v in vs), min(v.y for v in vs), min(v.z for v in vs)))
    hi = Vector((max(v.x for v in vs), max(v.y for v in vs), max(v.z for v in vs)))
    ob.data.transform(Matrix.Translation(Vector((-(lo.x + hi.x) / 2, -(lo.y + hi.y) / 2, -lo.z))))
    ob.data.update()
    dims = hi - lo
    rh_hs.log("recentred by %s; dims %s; height / max horizontal = %.3f" % (
        [round(-(lo.x + hi.x) / 2, 3), round(-(lo.y + hi.y) / 2, 3), round(-lo.z, 3)],
        [round(d, 3) for d in dims], dims.z / max(dims.x, dims.y)))
    return ob


def bolts_rect(out, prefix, xs, zs, y, facing=-1, r=0.014):
    for bx in xs:
        for bz in zs:
            out.append(K.bolt(prefix, (bx, y, bz), (0, facing, 0), r=r))


def louvre(prefix, centre, w, h, nslat, rim_role="hull", inner_role="rubber",
           slat_role="hull", proud=0.03, recess=0.02):
    """Framed louvre vent on a -Y face (plane y = centre[1]): rim, dark window,
    slats that shed downward. Slats stand only ~7 mm past the rim, so a side
    vent barely widens the fitted model. The frame is sunk SINK into the face."""
    out = []
    rim = 0.035
    bz, wy = bezel(prefix + "_frame", (centre[0], centre[1] + SINK, centre[2]), w, h,
                   proud + SINK, radius=0.045, rim=rim, recess=recess, rim_role=rim_role,
                   inner_role=inner_role)
    out.append(bz)
    cx, _, cz = centre
    ih = h - 2 * rim
    pitch = ih / nslat
    rot = Matrix.Rotation(math.radians(35.0), 3, "X")
    for k in range(nslat):           # slats 18 mm thick (rule 6: >= 15 mm)
        zc = cz - ih / 2 + pitch * (k + 0.5)
        out.append(obox("%s_slat%d" % (prefix, k), (cx, wy - 0.008, zc),
                        (w / 2 - rim + 0.004, 0.018, 0.009), rot, slat_role, bevel=0.002))
    return out


def role_stats(objs):
    """Surface share per role: all faces, and as seen from the fan side (face
    area - occluded faces count too, so the visible-pixel share is higher)."""
    tot, front = {}, {}
    for o in objs:
        me = o.data
        for p in me.polygons:
            m = me.materials[p.material_index]
            r = m["rh_role"]
            tot[r] = tot.get(r, 0.0) + p.area
            front[r] = front.get(r, 0.0) + p.area * max(0.0, -p.normal.y)
    for name, d in (("all faces", tot), ("seen from fan side", front)):
        s = sum(d.values())
        rh_hs.log("role share (%s): %s" % (name, ", ".join(
            "%s %.1f%%" % (k, 100 * v / s) for k, v in sorted(d.items(), key=lambda kv: -kv[1]))))
    acc = 100 * tot.get("accent", 0.0) / sum(tot.values())
    if acc < 2.0:
        rh_hs.log("WARNING accent is %.1f %% of all faces (< 2 %%): too little trim" % acc)


# ------------------------------------------------------------------ layout
SINK = 0.003                               # every frame/bezel bites 3 mm into its support
WH, DH = 0.95, 0.62                        # half extents of the cabinet (x, y)
ZB, TOP = 0.25, 1.65                       # cabinet underside, top
YF = -DH                                   # front face plane
CH_V, CH_FT, CH_BT = 0.08, 0.09, 0.05      # chamfers: vertical, front-top, back-top


# ------------------------------------------------------- stance: feet, legs
# Four square legs at the corners, their outer faces 1 cm inside the cabinet
# faces, so the full 0.22 m of leg shows below the cabinet even from the game's
# 54 deg pitch (round 1 hid them under a full-footprint plinth: "box on a
# skid"). A recessed slate sub-frame ties them together well back in shadow.
LX0, LX1, LY0, LY1 = 0.65, 0.87, 0.38, 0.60   # leg extents (abs x, abs y)
for sx in (-1, 1):
    for sy in (-1, 1):
        xa, xb = sorted((sx * LX0, sx * LX1))
        ya, yb = sorted((sy * LY0, sy * LY1))
        add(K.box("foot", (xa - 0.012, ya - 0.012, 0.0), (xb + 0.012, yb + 0.012, 0.035),
                  "rubber", bevel=0.008))
        add(K.box("leg", (xa, ya, 0.03), (xb, yb, ZB + 0.012), "slate", bevel=0.012))
        # hazard kick plate on the outward (front / back) face of each leg
        yf = sy * LY1
        add(K.box("hazard", (xa + 0.025, min(yf - sy * 0.002, yf + sy * 0.007), 0.075),
                  (xb - 0.025, max(yf - sy * 0.002, yf + sy * 0.007), 0.145), "hazard",
                  bevel=0.002, seg=1))
add(K.box("subframe", (-0.80, -0.44, 0.12), (0.80, 0.44, ZB + 0.008), "slate", bevel=0.01))

# ----------------------------------------------------------------- cabinet
S2 = math.sqrt(0.5)
cab_planes = [((1, 0, 0), WH), ((-1, 0, 0), WH), ((0, 1, 0), DH), ((0, -1, 0), DH),
              ((0, 0, 1), TOP), ((0, 0, -1), -ZB),
              ((0, -S2, S2), S2 * (DH + TOP - CH_FT)),     # front-top chamfer
              ((0, S2, S2), S2 * (DH + TOP - CH_BT))]      # back-top chamfer
for ex, ey in ((1, 1), (1, -1), (-1, 1), (-1, -1)):        # vertical chamfers
    cab_planes.append(((ex * S2, ey * S2, 0), S2 * (WH + DH - CH_V)))
add(convex_block("cabinet", cab_planes, "hull", bevel=0.012))

# ---------------------------------------------------------- LED strip (glow)
# The accent bezel and the lit strip are nearly the same teal, so a wide DARK
# slate channel (~2 cm above and below, ~5 cm at the ends) keeps the strip
# reading as its own lit element even with the power off.
LED_Z = 1.478
bz, wy = bezel("led_bezel", (0.0, YF + SINK, LED_Z), 1.50, 0.13, 0.025 + SINK,
               radius=0.045, rim=0.022, recess=0.014, rim_role="accent", inner_role="slate")
add(bz)
add(K.box("led", (-0.68, wy - 0.010, LED_Z - 0.024), (0.68, wy + 0.004, LED_Z + 0.024),
          "cell_on", bevel=0.003, seg=1))

# ---------------------------------------------------------------- door panel
DOOR_Y = YF - 0.022                        # door front plane
add(K.box("door", (-0.84, DOOR_Y, 0.285), (0.84, YF + 0.004, 1.40), "hull", bevel=0.012))
front = []
bolts_rect(front, "dbolt", (-0.80, 0.80), (0.325, 1.36), DOOR_Y)
parts += front

# ----------------------------------------------------------------------- fan
# Depth stack (d = metres in front of the plate, toward the viewer):
#   tunnel back 0 | blades 0.056-0.125 | hub spinner to 0.150, nut 0.162
#   | spokes 0.129-0.159 | rings 0.135-0.165 | shroud lip 0.160 | hub ring 0.173.
# Round 1 failed the identity at the real camera: 4 rings at 8 cm pitch
# stacked into a solid ribbed disc at 54 deg and the blades sat 7 cm back in
# the black tunnel. Now 3 rings at 12 cm pitch, 5 broad light blades pulled
# forward to within ~5 mm of the guard, low blade pitch (flatter = more face).
FX, FZ = -0.10, 0.85                       # fan centre (nearly centred, as the reference)
PH = 0.55                                  # mounting plate half size
PL_Y = DOOR_Y - 0.025                      # plate front plane
RI, RO, DEPTH = 0.455, 0.525, 0.16         # shroud inner/outer radius, depth (fan 55 % of width)
add(K.box("fan_plate", (FX - PH, PL_Y, FZ - PH), (FX + PH, DOOR_Y + 0.004, FZ + PH), "slate",
          bevel=0.008, chamfers=[(lambda d, m: abs(d.y) > 0.99, 0.20)]))
for ex in (-1, 1):
    for ez in (-1, 1):
        add(K.bolt("pbolt", (FX + ex * 0.42, PL_Y, FZ + ez * 0.42), (0, -1, 0), r=0.02))
# shroud: slate tunnel and outer collar, ACCENT front lip ring (chamfers + face)
add(lathe("shroud", [(RI, -0.004), (RI, DEPTH - 0.014), (RI + 0.014, DEPTH),
                     (RO - 0.025, DEPTH), (RO, DEPTH - 0.025), (RO, -0.004)],
          (FX, PL_Y, FZ), 56, ["slate", "accent"], seg_roles=[0, 1, 1, 1, 0, 0]))
# dark tunnel back, hub, blades
add(K.cyl("fan_back", (FX, PL_Y + 0.002, FZ), (FX, PL_Y - 0.004, FZ), RI + 0.008, "rubber",
          sides=32))
# rotor hub: a light spinner (blade alloy) with a chamfered nose, centre nut
# plugging its axis - the "hub" of the brief, part of the rotor, not the guard
add(lathe("fan_hub", [(0.02, -0.004), (0.10, -0.004), (0.10, 0.118), (0.08, 0.148),
                      (0.02, 0.150)], (FX, PL_Y, FZ), 24, ["blade"]))
add(K.cyl("hub_nut", (FX, PL_Y - 0.10, FZ), (FX, PL_Y - 0.162, FZ), 0.035, "metal", sides=6))
NBLADE = 5                                 # 5 broad blades: bigger wedges at thumbnail size
BLADE_D = 0.088                            # blade disc centre, in front of the plate
for k in range(NBLADE):                    # root corners r 0.093 < hub; tips r 0.434 < RI
    add(blade("blade%d" % k, (FX, PL_Y - BLADE_D, FZ), 2 * math.pi * k / NBLADE + 0.35,
              [(0.075, 0.060, 24, 0.0), (0.25, 0.105, 16, 0.025), (0.40, 0.120, 11, 0.05)],
              0.016, "blade"))
# guard: spokes (a cross, as in the reference) just behind the rings, 3 open
# rings, accent centre ring round the spinner. Wire r 15 mm (~3 cm in game);
# round 1's solid centre disc hid the hub and 6 % of the rotor.
GR = 0.015                                 # guard tube radius
for k in range(4):
    a = math.pi * k / 2
    d = Vector((math.cos(a), 0, math.sin(a)))
    p0 = Vector((FX, PL_Y - 0.144, FZ)) + d * 0.112
    p1 = Vector((FX, PL_Y - 0.144, FZ)) + d * (RI + 0.008)
    add(K.cyl("spoke%d" % k, p0, p1, GR, "metal", sides=8))
for k, R in enumerate((0.19, 0.31, 0.43)):
    add(torus_y("ring%d" % k, (FX, PL_Y - 0.150, FZ), R, GR, "metal", major=48))
add(torus_y("hub_ring", (FX, PL_Y - 0.155, FZ), 0.122, 0.018, "accent", major=32))

# ------------------------------------------- AIR: slate placard, raised letters
PX0, PX1, PZ0, PZ1 = 0.49, 0.82, 1.14, 1.38
PLAC_Y = DOOR_Y - 0.014                    # placard front plane
add(K.box("placard", (PX0, PLAC_Y, PZ0), (PX1, DOOR_Y + 0.004, PZ1), "slate", bevel=0.008))
plac = []
bolts_rect(plac, "plbolt", (PX0 + 0.02, PX1 - 0.02), (PZ0 + 0.018, PZ1 - 0.018), PLAC_Y,
           r=0.009)                        # corner bolts clear the letters by >= 1 cm
parts += plac
add(text_block("air", "AIR", ((PX0 + PX1) / 2, 0.0, (PZ0 + PZ1) / 2), 0.18,
               PLAC_Y - 0.007, PLAC_Y + 0.003, "accent", offset=0.005, spacing=1.15))

# status lamp (the second lit element), lower right
add(K.cyl("lamp_ring", (0.66, DOOR_Y + 0.004, 0.50), (0.66, DOOR_Y - 0.016, 0.50), 0.055,
          "accent", sides=16))
add(K.cyl("lamp", (0.66, DOOR_Y - 0.010, 0.50), (0.66, DOOR_Y - 0.022, 0.50), 0.035,
          "cell_on", sides=16))

# ------------------------------------- top: exhaust grille (slate, accent rim)
# The steep vault cameras see mostly the top, so its grille frame carries trim.
GX, GY, GW, GD = -0.32, -0.02, 0.92, 0.78  # grille centre (x, y) and size on the top
top = louvre("exhaust", (GX, -TOP, GY), GW, GD, 8, rim_role="accent",
             inner_role="rubber", slat_role="slate", proud=0.028, recess=0.02)
GB = 0.026                                 # corner bolts sit on the rim's rounded corner,
for bx in (GX - GW / 2 + GB, GX + GW / 2 - GB):   # clear of the window (round 1 overhung it)
    for by in (GY - GD / 2 + GB, GY + GD / 2 - GB):
        top.append(K.bolt("gbolt", (bx, -(TOP + 0.028), by), (0, -1, 0), r=0.012))
parts += turn(top, -90.0, "X")

# ---------------------------------------------- pipes: twin inverted-U loops
# Loops stand 0.30 m clear of the top (round 1: 0.26 m, read as two knobs from
# the low front view); RISE is capped so the overall height stays under the
# width (the 4 m vault). DarkSlate, not gunmetal: the 0.75-metallic pipe role
# reflects Mars' dark sky and read near-black.
PIPE_X, PIPE_YT, PIPE_YB = (0.36, 0.64), 0.12, 0.80
RISE, PIPE_R, ZIN = 0.22, 0.08, 1.12
for k, px in enumerate(PIPE_X):
    add(K.cyl("pflange%d" % k, (px, PIPE_YT, TOP - 0.01), (px, PIPE_YT, TOP + 0.022), 0.125,
              "slate", sides=16))
    add(K.cyl("pnut%d" % k, (px, PIPE_YT, TOP + 0.022), (px, PIPE_YT, TOP + 0.066), 0.112,
              "metal", sides=6))
    add(K.cyl("pcollar%d" % k, (px, PIPE_YT, TOP + 0.066), (px, PIPE_YT, TOP + 0.08), 0.094,
              "metal", sides=16))
    add(K.pipe("pipe%d" % k, [(px, PIPE_YT, TOP - 0.03), (px, PIPE_YT, TOP + RISE),
                              (px, PIPE_YB, TOP + RISE), (px, PIPE_YB, ZIN),
                              (px, DH - 0.04, ZIN)], PIPE_R, "slate", bend=0.12, sides=16))
    # back-face flange and nut where the pipe returns
    add(K.cyl("bflange%d" % k, (px, DH + 0.010, ZIN), (px, DH + 0.046, ZIN), 0.12, "slate",
              sides=16))
    add(K.cyl("bnut%d" % k, (px, DH + 0.046, ZIN), (px, DH + 0.09, ZIN), 0.105, "metal",
              sides=6))
    # clamp ring, 1 cm taller than the bracket each way (no coplanar faces)
    add(K.cyl("pclamp%d" % k, (px, PIPE_YB, 1.32), (px, PIPE_YB, 1.40), 0.10, "hull",
              sides=16))
# bracket tying the descending legs to the back face
add(K.box("pbracket", (PIPE_X[0] - 0.05, DH + 0.010, 1.33), (PIPE_X[1] + 0.05, PIPE_YB, 1.39),
          "slate", bevel=0.008))

# ---------------------------------------------------------------- back face
back = []
back.append(K.box("back_panel", (-0.80, DH - 0.004, 0.33), (0.80, DH + 0.018, 1.45), "hull",
                  bevel=0.01))
bolts_rect(back, "bbolt", (-0.76, 0.76), (0.37, 1.41), DH + 0.018, facing=1)
parts += back
# pleated filter cartridge in an ACCENT-rimmed frame (what the machine is FOR)
FCX, FCZ, FW, FH = -0.26, 0.80, 0.92, 0.56
fb, fwy = bezel("filter_frame", (FCX, DH + 0.018 - SINK, FCZ), FW, FH, 0.03 + SINK,
                radius=0.05, rim=0.04, recess=0.02, rim_role="accent", inner_role="rubber",
                facing=1)
add(fb)
NPLEAT = 19
for k in range(NPLEAT):
    fx = FCX - 0.38 + 0.76 * k / (NPLEAT - 1)
    add(K.box("pleat%d" % k, (fx - 0.009, fwy - 0.003, FCZ - 0.2), (fx + 0.009, fwy + 0.014,
                                                                     FCZ + 0.2), "metal",
              bevel=0.0))
add(K.box("filter_pull", (FCX - 0.16, fwy + 0.010, FCZ - 0.02), (FCX + 0.16, DH + 0.07,
                                                                 FCZ + 0.02), "hull",
          bevel=0.008))

# ----------------------------------------- sides (built on a virtual -Y face)
SY = -WH                                   # virtual face plane for the side kits
# viewer's right (+X): slate frame round a RECESSED hull service panel, over a
# louvre vent (a dark recess read as a screen in the first pass)
side = []
sb, swy = bezel("svc_panel", (0.0, SY + SINK, 1.08), 0.86, 0.66, 0.035 + SINK, radius=0.05,
                rim=0.04, recess=0.025, rim_role="slate", inner_role="hull")
side.append(sb)
hb, _ = bezel("svc_handle", (0.0, swy + SINK, 1.28), 0.32, 0.09, 0.032 + SINK,
              radius=0.035, rim=0.024, recess=0.022, rim_role="accent", inner_role="rubber")
side.append(hb)
# bolts on the 4 cm rim, on its rounded corners' centre line: clear of the
# window edge and of the outer corner arc (r 0.05)
bolts_rect(side, "sbolt", (-0.405, 0.405), (0.775, 1.385), SY - 0.035, r=0.012)
side += louvre("svc_louvre", (0.0, SY, 0.53), 0.86, 0.36, 5, rim_role="accent")
parts += turn(side, 90.0)
# viewer's left (-X): tall intake louvre bank
side = louvre("intake", (0.0, SY, 0.92), 0.86, 1.08, 11, rim_role="accent")
bolts_rect(side, "ibolt", (-0.48, 0.48), (0.40, 1.44), SY)
parts += turn(side, -90.0)

role_stats(parts)

# ------------------------------------------------------------------ finish
ob = K.finish(parts, NAME)
K.face_camera(ob)                          # fan face -Y -> -X (default camera)
recentre(ob)
os.makedirs(OUT, exist_ok=True)
bpy.ops.object.select_all(action="DESELECT")
ob.select_set(True)
bpy.ops.export_scene.gltf(filepath=os.path.join(OUT, NAME + "_preview.glb"), export_format="GLB",
                          use_selection=True, export_apply=True)
rh_hs.log("preview glb written")
if not PREVIEW_ONLY:
    files = K.bake_and_export(ob, NAME, OUT, size=SIZE, dust=0.22)
    rh_hs.log("files: %s" % files)
