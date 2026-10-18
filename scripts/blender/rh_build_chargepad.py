"""Build the Charging Pad from code - the hard-surface build (2026-10-17).

  blender -b -P scripts/blender/rh_build_chargepad.py -- [--out Martians/gen/hs/ChargePad]
      [--size 2048] [--preview-only]

The game's Charging Pad ("where robots refuel - fast, one at a time"), a 2 x 2
cell footprint. A 1.8 m humanoid robot walks onto the centre and stands there
to charge. The DESIGN is the director-approved concept "Pad C - Induction
disc" (2026-10-17): the blockout pad_C() in the session's concept rig
(rh_concept.py) and its renders padC.png / concepts_sheet.png (top right
panel) - a round dark deck with a hazard-yellow rim and slate notch marks, two
concentric amber coil rings round a light-alloy contact plate, and a slim
bone-white service mast at the back edge with an amber beacon and a cable
reel at its foot. This script is that blockout finished in the kit's full
detail: every part an exact primitive, the coil rings, beacon and status strip
lit by construction, so the emissive mask is the lights themselves.

Layout facts:
  - built with the FRONT (where a robot walks on: three accent tread strips)
    at -Y and the mast at the back (+Y side), then K.face_camera() turns the
    front to -X, the face the game's default strategy camera meets (measured
    2026-10-13). The mast stands 29 deg off the back axis (build angle 119 deg),
    as in the concept, so from the default camera it is not hidden behind - and
    does not hide - a robot standing on the centre.
  - the game fits the LARGER horizontal extent to the 4.0 m footprint, so
    nothing may leave the deck circle: deck radius 1.98 m (3.96 m across), and
    the mast foot (max r 1.735), the reel (max r 1.63) and the junction box all
    sit inside the hazard rim (r 1.752-1.98). The script asserts the deck
    radius is the largest radius in the mesh. Overall ~3.96 x 3.96 x 3.23 m.
  - deck: ONE lathe solid, 0.205 m tall at the rim, field at 0.18 m, a 45 deg
    chamfered top edge. The coil rings are channels turned into that profile
    (gunmetal lip, rubber walls, a lit floor 14 mm below the field), so the rings
    are inset by construction - no decal can z-fight: outer ring lit 0.798-0.964,
    inner ring 0.541-0.659 (16.6 / 11.8 cm wide - the concept's widths, kept so
    the rings still read as rings from the far strategy camera). Narrow gunmetal
    retaining clips cross them (8 outer, 6 inner) for the up-close coil read;
    round 1 had 12 + 8 wider clips and the rings read as a segmented dartboard.
  - rim: hazard band r 1.768-1.922, 2.5 cm proud of the field, wrapping the
    chamfer; eight slate chevrons on it at 45 deg intervals, pointing inward
    (24 cm wide, 4 cm arms - round 1's 15 cm chevrons were hairlines at 54 deg).
  - contact plate: light alloy, 0.97 m across, 2 cm proud, 36 real radial grooves
    (5 mm deep, 1.2 cm wide at mid radius) cut in a fluted mesh, a plain rim band with a ring of 16 bolts,
    and a slate centre boss with an alloy cap - the spot the robot stands on.
  - deck detail (the outer field band, r 0.98-1.75): seven radial rubber panel
    seams, three flush hatches (kit bezel: alloy frame, slate lid, accent lift
    handle, corner bolts), a slatted vent, three accent tread strips marking the
    walk-on edge, a bolt ring near the rim and one between the coil rings, eight
    drain slots on the deck wall, and the cable trench: an accent cable
    protector on a rubber lining from the mast's junction box to a slate end cap
    at the outer coil ring.
  - mast (r 1.60): slate foot plate + bolts, a safety-yellow (accent) base
    sleeve and HV junction box (alloy lid),
    bone-white column r 0.085 -> 0.075 in two sections with a slate collar and
    accent band at 1.9 m, a five-cell amber status strip in a slate channel
    facing the centre (2.15-2.75 m), a slate head with an accent band, a domed
    amber beacon lens under a three-bar alloy guard. Top at ~3.23 m.
  - cable reel at the mast's foot (build angle 141 deg, r 1.40), axle radial so
    the hub faces the centre / the default camera: slate plinth and cheek plates,
    slate flanges with accent rims, the rubber charging cable coiled on the drum
    as a real helix that leaves the drum's mast side and loops up into a
    connector holstered on the mast (accent cup on a slate bracket, alloy body,
    bone-white grip, rubber boot). Hub caps with bolts, a crank with an accent
    knob. No cable lies across the deck - it is an induction pad.
  - palette: deck, rim marks, mast collar/head, reel slate; mast column
    bone-white (hull); contact plate, hatch frames, vent slats light alloy
    (metal); cable, seams, channel walls rubber; rim hazard. Accent (#F2BF26,
    TintFor ChargePad) is TRIM only: cable protector, walk-on treads, mast base
    sleeve, junction box, reel flange rims, holster cup, mast bands, hatch
    handles, crank knob. Measured ~2.5 % of first-hit pixels from the game
    camera and four 54 deg yaws (role raster of the preview GLB) - under the
    art bible's 5 % on purpose: the hazard rim, the same yellow family, is
    already ~18 % of those pixels and the amber rings ~10 %, so accent stays
    on trim instead of being pushed onto larger parts to reach 5 % (open
    question for the director).
  - the glow is ONE hue per asset in game (amber, as rh_build_stockpile.py):
    both coil rings, the beacon lens and the status strip use the "glow" role,
    albedo #FFB040 so they read amber in daylight too. The teal cell_on role is
    not used anywhere on this asset.
  - the deck's underside is never seen (it sits on the ground) but is ~32 % of
    the surface; its UV island is shrunk to 5 % and the atlas repacked, so the
    visible faces get ~1.5x the texel area (~1.2x linear density).
  - budget: 11.5k tris (deck lathe 2.9k at 72 segments, mast 1.5k, reel 1.4k,
    cable helix 1.0k, fluted plate 1.0k); emissive mask ~3.1 % of texels.
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
OUT = argv[argv.index("--out") + 1] if "--out" in argv else "Martians/gen/hs/ChargePad"
SIZE = int(argv[argv.index("--size") + 1]) if "--size" in argv else 2048
PREVIEW_ONLY = "--preview-only" in argv
NAME = "ChargePad"

K = rh_hs.Kit(accent_hex="#F2BF26")       # TintFor(ChargePad), art bible 2.3
# Lit role: amber. Albedo matches the in-game glow hue; preview emission is the
# game's EmissiveColor normalised, kept low so review frames show amber
# instead of clipping white (as rh_build_stockpile.py).
K.roles["glow"] = dict(albedo="#FFB040", metal=0.0, rough=0.25, emit=1, dust=0.0)
K.mats["glow"] = K._preview_material("glow", K.roles["glow"])
_gb = K.mats["glow"].node_tree.nodes["Principled BSDF"]
_gb.inputs["Emission Color"].default_value = (1.0, 0.49, 0.045, 1.0)
_gb.inputs["Emission Strength"].default_value = 1.5

parts = []
add = parts.append
S2 = math.sqrt(0.5)


# ------------------------------------------------------------ local helpers
def convex(name, planes, role, bevel=0.0, seg=1):
    """Closed convex solid = intersection of half-spaces n.p <= d (optionally
    bevelled). One watertight hull however many chamfers meet at a corner."""
    pts = []
    for (n1, d1), (n2, d2), (n3, d3) in itertools.combinations(planes, 3):
        M = Matrix((n1, n2, n3))
        if abs(M.determinant()) < 1e-9:
            continue
        p = M.inverted() @ Vector((d1, d2, d3))
        if all(Vector(n).dot(p) <= d + 1e-5 for n, d in planes):
            if all((p - q).length > 1e-4 for q in pts):
                pts.append(p)
    bm = bmesh.new()
    vs = [bm.verts.new(p) for p in pts]
    res = bmesh.ops.convex_hull(bm, input=vs)
    bmesh.ops.delete(bm, geom=res["geom_interior"] + res["geom_unused"], context="VERTS")
    bmesh.ops.dissolve_limit(bm, angle_limit=math.radians(0.5), verts=bm.verts[:],
                             edges=bm.edges[:])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    if bevel > 0:
        bmesh.ops.bevel(bm, geom=list(bm.edges), offset=bevel, offset_type="OFFSET",
                        segments=seg, profile=0.5, affect="EDGES", clamp_overlap=True)
        bmesh.ops.dissolve_degenerate(bm, dist=1e-4, edges=bm.edges[:])
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    return K._obj(name, bm, [role])


def cbox(name, lo, hi, role, vch=0.0, tch=0.0, corners=None, tsides=None, bevel=0.0, seg=1):
    """Box lo..hi with vertical-edge chamfers `vch` (at `corners` [(ex, ey)],
    default all four) and top-edge chamfers `tch` (on `tsides` [(nx, ny)],
    default all four), built as one convex solid."""
    x0, y0, z0 = lo
    x1, y1, z1 = hi
    planes = [((1, 0, 0), x1), ((-1, 0, 0), -x0), ((0, 1, 0), y1), ((0, -1, 0), -y0),
              ((0, 0, 1), z1), ((0, 0, -1), -z0)]
    if vch > 0:
        for ex in (-1, 1):
            for ey in (-1, 1):
                if corners is None or (ex, ey) in corners:
                    cx_ = x1 if ex > 0 else x0
                    cy_ = y1 if ey > 0 else y0
                    planes.append(((ex * S2, ey * S2, 0), S2 * (ex * cx_ + ey * cy_ - vch)))
    if tch > 0:
        for nx, ny in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            if tsides is None or (nx, ny) in tsides:
                if nx:
                    edge = nx * (x1 if nx > 0 else x0)
                else:
                    edge = ny * (y1 if ny > 0 else y0)
                planes.append(((nx * S2, ny * S2, S2), S2 * (edge + z1 - tch)))
    return convex(name, planes, role, bevel, seg)


def lathe_z(name, prof, cx, cy, z0, roles, seg=14, mis=None):
    """Revolve a CLOSED (r, z) profile about the vertical axis through (cx, cy);
    r = 0 points become poles. mis[j] = material index of profile edge j."""
    bm = bmesh.new()
    rings = []
    for (r, z) in prof:
        if r < 1e-6:
            v = bm.verts.new((cx, cy, z0 + z))
            rings.append([v] * seg)
        else:
            rings.append([bm.verts.new((cx + r * math.cos(2 * math.pi * s / seg),
                                        cy + r * math.sin(2 * math.pi * s / seg), z0 + z))
                          for s in range(seg)])
    n = len(prof)
    for j in range(n):
        k = (j + 1) % n
        for s in range(seg):
            s1 = (s + 1) % seg
            vs = []
            for v in (rings[j][s], rings[j][s1], rings[k][s1], rings[k][s]):
                if v not in vs:
                    vs.append(v)
            if len(vs) < 3:
                continue
            f = bm.faces.new(vs)
            f.material_index = mis[j] if mis else 0
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    return K._obj(name, bm, roles)


def lathe_axis(name, prof, origin, axis, roles, seg=24, mis=None):
    """lathe_z about an arbitrary axis through `origin` (profile z runs along axis)."""
    ob = lathe_z(name, prof, 0.0, 0.0, 0.0, roles, seg=seg, mis=mis)
    rot = Vector((0, 0, 1)).rotation_difference(Vector(axis).normalized()).to_matrix().to_4x4()
    ob.data.transform(Matrix.Translation(Vector(origin)) @ rot)
    return ob


def prism(name, poly, z0, z1, role):
    """Vertical prism over a convex XY polygon."""
    bm = bmesh.new()
    lo = [bm.verts.new((x, y, z0)) for x, y in poly]
    hi = [bm.verts.new((x, y, z1)) for x, y in poly]
    bm.faces.new(list(reversed(lo)))
    bm.faces.new(hi)
    n = len(poly)
    for i in range(n):
        j = (i + 1) % n
        bm.faces.new((lo[i], lo[j], hi[j], hi[i]))
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    return K._obj(name, bm, [role])


def tube(name, pts, r, role, sides=8):
    """Circle of `sides` swept along a polyline (parallel-transport frames, capped)."""
    pts = [Vector(p) for p in pts]
    n = len(pts)
    tans = []
    for i in range(n):
        if i == 0:
            t = pts[1] - pts[0]
        elif i == n - 1:
            t = pts[-1] - pts[-2]
        else:
            t = (pts[i + 1] - pts[i]).normalized() + (pts[i] - pts[i - 1]).normalized()
        tans.append(t.normalized())
    ref = Vector((0, 0, 1)) if abs(tans[0].z) < 0.9 else Vector((1, 0, 0))
    nrm = (ref - tans[0] * ref.dot(tans[0])).normalized()
    bm = bmesh.new()
    rings = []
    for i, p in enumerate(pts):
        if i > 0:
            nrm = tans[i - 1].rotation_difference(tans[i]) @ nrm
            nrm = (nrm - tans[i] * nrm.dot(tans[i])).normalized()
        b = tans[i].cross(nrm)
        rings.append([bm.verts.new(p + r * (math.cos(2 * math.pi * k / sides) * nrm
                                            + math.sin(2 * math.pi * k / sides) * b))
                      for k in range(sides)])
    for a, b in zip(rings, rings[1:]):
        for k in range(sides):
            k1 = (k + 1) % sides
            bm.faces.new((a[k], a[k1], b[k1], b[k]))
    bm.faces.new(list(reversed(rings[0])))
    bm.faces.new(rings[-1])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    return K._obj(name, bm, [role])


def pol(r, deg, z=0.0):
    a = math.radians(deg)
    return Vector((r * math.cos(a), r * math.sin(a), z))


def xf(deg, at=(0.0, 0.0, 0.0)):
    return Matrix.Translation(Vector(at)) @ Matrix.Rotation(math.radians(deg), 4, "Z")


def place(objs, deg=0.0, at=(0.0, 0.0, 0.0)):
    """Carry parts built in a local frame (x = radial / axial, z up) onto the
    deck: rotate about Z by `deg`, then translate by `at`."""
    if not isinstance(objs, (list, tuple)):
        objs = [objs]
    M = xf(deg, at)
    for o in objs:
        o.data.transform(M)
    return list(objs)


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


def role_stats(objs):
    """Surface share per role over all faces (the hidden underside included)."""
    tot = {}
    for o in objs:
        me = o.data
        for p in me.polygons:
            r = me.materials[p.material_index]["rh_role"]
            tot[r] = tot.get(r, 0.0) + p.area
    s = sum(tot.values())
    rh_hs.log("role share (all faces): %s" % ", ".join(
        "%s %.1f%%" % (k, 100 * v / s) for k, v in sorted(tot.items(), key=lambda kv: -kv[1])))
    vis = {}
    for o in objs:
        me = o.data
        for p in me.polygons:
            if p.normal.z < -0.99 and p.center.z < 0.01:
                continue
            r = me.materials[p.material_index]["rh_role"]
            vis[r] = vis.get(r, 0.0) + p.area
    sv = sum(vis.values())
    rh_hs.log("role share (underside excluded): %s" % ", ".join(
        "%s %.1f%%" % (k, 100 * v / sv) for k, v in sorted(vis.items(), key=lambda kv: -kv[1])))
    tris = {}
    for o in objs:
        key = o.name.rstrip("0123456789._-").split("_")[0]
        tris[key] = tris.get(key, 0) + sum(len(p.vertices) - 2 for p in o.data.polygons)
    rh_hs.log("tris by part: %s" % ", ".join(
        "%s %d" % kv for kv in sorted(tris.items(), key=lambda kv: -kv[1])))


def shrink_underside_uv(ob, factor=0.05, margin=0.003):
    """The deck's underside sits on the ground and is never seen: shrink its UV
    island about its centre and repack, so visible faces get the texels."""
    me = ob.data
    uv = me.uv_layers.active.data
    loops = [li for p in me.polygons if p.normal.z < -0.99 and p.center.z < 0.01
             for li in p.loop_indices]
    if not loops:
        return
    c = Vector((0.0, 0.0))
    for li in loops:
        c += uv[li].uv
    c /= len(loops)
    for li in loops:
        uv[li].uv = c + (uv[li].uv - c) * factor
    bpy.context.scene.tool_settings.use_uv_select_sync = True
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.pack_islands(rotate=True, margin=margin)
    bpy.ops.object.mode_set(mode="OBJECT")
    rh_hs.log("underside UV shrunk to %d %% (%d loops) and atlas repacked"
              % (round(100 * factor), len(loops)))


# ------------------------------------------------------------------ layout
R = 1.98                                   # deck radius (3.96 m across)
ZF = 0.180                                 # deck field height
ZR = 0.205                                 # hazard rim top
ZG = 0.166                                 # lit coil floor (14 mm below the field)
ZP = 0.200                                 # contact plate top
SEG = 72                                   # deck lathe segments
SINK = 0.004                               # plates bite this far into their support
FRONT = 270.0                              # walk-on edge (-Y before face_camera)
TH_M, RM = 119.0, 1.60                     # mast: build angle (deg), radius
TH_R, RR = 141.0, 1.40                     # cable reel centre
AZ = 0.60                                  # reel axle height
RIM_IN = 1.752                             # inner foot of the rim step

# ------------------------------------------------------------------- deck
# roles: 0 slate, 1 hazard, 2 rubber, 3 pipe, 4 glow; one material per profile edge
DECK = [
    ((0.000, 0.000), 0),            # underside (hidden)
    ((R - 0.025, 0.000), 0),        # foot chamfer
    ((R, 0.025), 0),                # wall
    ((R, 0.150), 1),                # 45 deg top chamfer (the rim wraps the edge)
    ((R - 0.045, 0.195), 1),        # rim outer round
    ((R - 0.058, ZR), 1),           # rim top
    ((1.768, ZR), 1),               # rim inner round
    ((1.757, ZR - 0.008), 2),       # rim step (rubber gasket line)
    ((RIM_IN, ZF), 0),              # outer field
    ((0.982, ZF), 3),               # outer coil: lip
    ((0.970, ZF - 0.007), 2),       #   wall
    ((0.964, ZG), 4),               #   lit floor
    ((0.798, ZG), 2),               #   wall
    ((0.792, ZF - 0.007), 3),       #   lip
    ((0.780, ZF), 0),               # field between the coils
    ((0.677, ZF), 3),               # inner coil: lip
    ((0.665, ZF - 0.007), 2),       #   wall
    ((0.659, ZG), 4),               #   lit floor
    ((0.541, ZG), 2),               #   wall
    ((0.535, ZF - 0.007), 3),       #   lip
    ((0.523, ZF), 0),               # inner field (under the contact plate)
    ((0.000, ZF), 0),
]
add(lathe_z("deck", [p for p, _ in DECK], 0.0, 0.0, 0.0,
            ["slate", "hazard", "rubber", "pipe", "glow"], seg=SEG, mis=[m for _, m in DECK]))

# coil retaining clips across the lit rings (gunmetal, 4 mm proud of the field)
for k in range(8):
    add(place(K.box("clip", (0.786, -0.013, ZG - 0.003), (0.976, 0.013, ZF + 0.004), "pipe",
                    bevel=0.0), 22.5 + 45.0 * k)[0])
for k in range(6):
    add(place(K.box("clip", (0.530, -0.011, ZG - 0.003), (0.671, 0.011, ZF + 0.004), "pipe",
                    bevel=0.0), 60.0 * k)[0])

# rim chevrons: slate inlays on the hazard band, pointing at the centre
for k in range(8):
    tip_a, tip_b, u_out, slope = 1.784, 1.842, 1.908, 1.0
    va, vb = (u_out - tip_a) / slope, (u_out - tip_b) / slope
    for sv in (-1, 1):
        poly = [(tip_a, 0.0), (tip_b, 0.0), (u_out, sv * vb), (u_out, sv * va)]
        add(place(prism("chev", poly, ZR - 0.004, ZR + 0.004, "slate"), 45.0 * k)[0])

# drain slots on the deck wall, centred on wall faces (angles 2.5 + 5k)
for k in range(8):
    add(place(K.box("slot", (R * math.cos(math.radians(2.5)) - 0.004, -0.085, 0.055),
                    (R * math.cos(math.radians(2.5)) + 0.003, 0.085, 0.115), "rubber",
                    bevel=0.0), 22.5 + 45.0 * k)[0])

# radial panel seams across the outer field (none where the mast stands)
SEAMS = [22.5, 67.5, 157.5, 202.5, 247.5, 292.5, 337.5]
for a in SEAMS:
    add(place(K.box("seam", (0.990, -0.005, ZF - 0.004), (RIM_IN - 0.006, 0.005, ZF + 0.0025),
                    "rubber", bevel=0.0), a)[0])

# bolt rings: near the rim, and in the band between the coil rings
for k in range(24):
    a = 15.0 * k
    if abs(((a - TH_M + 180.0) % 360.0) - 180.0) < 8.0:
        continue
    add(K.bolt("fbolt", pol(1.715, a, ZF - 0.001), (0, 0, 1), r=0.013, h=0.010))
for k in range(12):
    add(K.bolt("mbolt", pol(0.7285, 30.0 * k, ZF - 0.001), (0, 0, 1), r=0.010, h=0.008))


# -------------------------------------------------------- contact plate
def fluted_plate(name, r_in, r_out, r_top, r_side, z_top, depth, z_bot, n, half_w, role):
    """Disc with n radial grooves cut into its top (a real pocket each: floor,
    two side walls, two end walls), a plain rim band, a 45 deg edge chamfer."""
    bm = bmesh.new()
    angs = []
    for k in range(n):
        a = 2 * math.pi * (k + 0.5) / n
        angs += [a - half_w, a + half_w]
    m = len(angs)

    def ring(r, z):
        return [bm.verts.new((r * math.cos(a), r * math.sin(a), z)) for a in angs]
    In, Ou = ring(r_in, z_top), ring(r_out, z_top)
    Il, Ol = ring(r_in, z_top - depth), ring(r_out, z_top - depth)
    O2 = ring(r_top, z_top)
    O3 = ring(r_side, z_top - (r_side - r_top))
    O4 = ring(r_side, z_bot)
    cT = bm.verts.new((0.0, 0.0, z_top))
    cB = bm.verts.new((0.0, 0.0, z_bot))
    F = bm.faces.new
    for j in range(m):
        j1 = (j + 1) % m
        if j % 2 == 0:                     # groove pocket
            F((Il[j], Ol[j], Ol[j1], Il[j1]))
            F((In[j], Il[j], Ol[j], Ou[j]))
            F((In[j1], Ou[j1], Ol[j1], Il[j1]))
            F((Ou[j], Ol[j], Ol[j1], Ou[j1]))
            F((In[j], In[j1], Il[j1], Il[j]))
        else:                              # land between grooves
            F((In[j], Ou[j], Ou[j1], In[j1]))
        F((Ou[j], O2[j], O2[j1], Ou[j1]))
        F((O2[j], O3[j], O3[j1], O2[j1]))
        F((O3[j], O4[j], O4[j1], O3[j1]))
        F((cT, In[j], In[j1]))
        F((cB, O4[j1], O4[j]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    return K._obj(name, bm, [role])


add(fluted_plate("plate", 0.170, 0.420, 0.477, 0.485, ZP, 0.005, ZF - 0.003, 36, 0.020, "metal"))
for k in range(16):
    add(K.bolt("pbolt", pol(0.452, 11.25 + 22.5 * k, ZP - 0.001), (0, 0, 1), r=0.011, h=0.008))
# centre boss: slate ring, alloy cap, six bolts
add(lathe_z("boss", [(0.0, ZP - 0.003), (0.150, ZP - 0.003), (0.150, ZP + 0.004),
                     (0.143, ZP + 0.011), (0.098, ZP + 0.011), (0.098, ZP + 0.014),
                     (0.0, ZP + 0.016)], 0.0, 0.0, 0.0, ["slate", "metal"], seg=24,
            mis=[0, 0, 0, 0, 1, 1, 1]))
for k in range(6):
    add(K.bolt("bbolt", pol(0.122, 30.0 + 60.0 * k, ZP + 0.010), (0, 0, 1), r=0.009, h=0.006))


# ------------------------------------------------------- hatches / vent
def bezel_flat(name, theta, r, w, h, proud, radius, rim, recess, rim_role, inner_role):
    """K.bezel laid flat in the deck at polar (r, theta): w runs tangentially, h
    radially, back face SINK below the field. Returns (obj, window z, M) where M
    carries hatch-local (x tangential, y radial-out, z up) points to the deck."""
    ob, _ = K.bezel(name, (0.0, 0.0, 0.0), w, h, proud, radius=radius, rim=rim, recess=recess,
                    rim_role=rim_role, inner_role=inner_role, facing=-1)
    M = xf(theta - 90.0, pol(r, theta, ZF - SINK))
    ob.data.transform(M @ Matrix.Rotation(math.radians(-90.0), 4, "X"))
    return ob, ZF - SINK + proud - recess, M


def hatch(theta, r=1.36, w=0.44, h=0.30):
    ob, zw, M = bezel_flat("hatch", theta, r, w, h, 0.010 + SINK, 0.05, 0.035, 0.006,
                           "metal", "slate")
    out = [ob]
    hb = K.box("hhandle", (-0.06, -0.105, zw - 0.003), (0.06, -0.075, zw + 0.008), "accent",
               bevel=0.003, seg=1)
    hb.data.transform(M @ Matrix.Translation((0, 0, -(ZF - SINK))))
    out.append(hb)
    for bx in (-0.155, 0.155):
        for by in (-0.085, 0.085):
            b = K.bolt("hbolt", (bx, by, zw - 0.001), (0, 0, 1), r=0.010, h=0.006)
            b.data.transform(M @ Matrix.Translation((0, 0, -(ZF - SINK))))
            out.append(b)
    return out


for a in (0.0, 180.0, 315.0):
    parts += hatch(a)

# slatted vent for the coil electronics
_vent, _zw, _M = bezel_flat("vent", 45.0, 1.36, 0.40, 0.26, 0.016 + SINK, 0.05, 0.035, 0.010,
                            "slate", "rubber")
add(_vent)
for sy in (-0.06, -0.02, 0.02, 0.06):
    s = K.box("slat", (-0.163, sy - 0.011, _zw - 0.003), (0.163, sy + 0.011, _zw + 0.007), "metal",
              bevel=0.0)
    s.data.transform(_M @ Matrix.Translation((0, 0, -(ZF - SINK))))
    add(s)

# walk-on tread plates at the front edge
for u0 in (1.12, 1.32, 1.52):
    add(place(cbox("tread", (u0, -0.24, ZF - 0.003), (u0 + 0.065, 0.24, ZF + 0.008), "accent",
                   tch=0.004), FRONT)[0])

# ----------------------------------------------------------- cable trench
# accent cable protector on a rubber lining, mast junction box -> coil ring
trench = [
    K.box("tr_lining", (1.020, -0.095, ZF - 0.003), (1.305, 0.095, ZF + 0.004), "rubber", bevel=0.0),
    cbox("tr_cover", (1.035, -0.075, ZF - 0.003), (1.300, 0.075, ZF + 0.026), "accent",
         tch=0.018, tsides=[(0, 1), (0, -1)]),
    cbox("tr_cap", (0.985, -0.100, ZF - 0.003), (1.048, 0.100, ZF + 0.032), "slate",
         vch=0.015, tch=0.012),
]
for u in (1.09, 1.17, 1.25):
    trench.append(K.bolt("tbolt", (u, 0.0, ZF + 0.025), (0, 0, 1), r=0.011, h=0.007))
parts += place(trench, TH_M)

# ------------------------------------------------------------------- mast
M_AT = pol(RM, TH_M)
mast = [
    cbox("m_foot", (RM - 0.13, -0.13, ZF - SINK), (RM + 0.13, 0.13, ZF + 0.032), "slate",
         vch=0.03, tch=0.008),
    cbox("m_jbox", (1.300, -0.100, ZF - SINK), (1.490, 0.100, 0.360), "accent",
         vch=0.02, tch=0.02, bevel=0.006),
    cbox("m_jlid", (1.320, -0.080, 0.354), (1.470, 0.080, 0.366), "metal", tch=0.004),
]
for bx in (RM - 0.09, RM + 0.095):
    for by in (-0.095, 0.095):
        mast.append(K.bolt("m_fbolt", (bx, by, ZF + 0.031), (0, 0, 1), r=0.014, h=0.012))
# status strip: slate channel facing the centre, five lit cells
mast.append(K.box("m_strip", (RM - 0.095, -0.032, 2.120), (RM - 0.050, 0.032, 2.780), "slate",
                  bevel=0.006, seg=1))
for i in range(5):
    z0 = 2.150 + 0.125 * i
    mast.append(K.box("m_cell", (RM - 0.099, -0.020, z0), (RM - 0.093, 0.020, z0 + 0.100), "glow",
                      bevel=0.0))
parts += place(mast, TH_M)

add(lathe_z("m_sleeve", [(0.0, ZF + 0.028), (0.125, ZF + 0.028), (0.125, 0.470), (0.100, 0.505),
                         (0.0, 0.505)], M_AT.x, M_AT.y, 0.0, ["accent"], seg=16))
add(K.cyl("m_column", (M_AT.x, M_AT.y, 0.500), (M_AT.x, M_AT.y, 1.870), 0.085, "hull", sides=16))
add(lathe_z("m_collar", [(0.0, 1.845), (0.098, 1.845), (0.112, 1.859), (0.112, 1.882),
                         (0.116, 1.886), (0.116, 1.922), (0.112, 1.926), (0.112, 1.948),
                         (0.096, 1.962), (0.0, 1.962)], M_AT.x, M_AT.y, 0.0, ["slate", "accent"],
            seg=16, mis=[0, 0, 0, 1, 1, 1, 0, 0, 0, 0]))
add(K.cyl("m_column2", (M_AT.x, M_AT.y, 1.955), (M_AT.x, M_AT.y, 2.975), 0.075, "hull", sides=16))
add(lathe_z("m_head", [(0.0, 2.965), (0.085, 2.965), (0.110, 2.990), (0.110, 3.020), (0.114, 3.024),
                       (0.114, 3.046), (0.110, 3.050), (0.110, 3.065), (0.098, 3.077),
                       (0.0, 3.077)], M_AT.x, M_AT.y, 0.0, ["slate", "accent"], seg=16,
            mis=[0, 0, 0, 1, 1, 1, 0, 0, 0, 0]))
add(lathe_z("m_lens", [(0.0, 3.070), (0.085, 3.070), (0.085, 3.130), (0.070, 3.175), (0.040, 3.200),
                       (0.0, 3.208)], M_AT.x, M_AT.y, 0.0, ["glow"], seg=16))
add(K.cyl("m_lenscap", (M_AT.x, M_AT.y, 3.202), (M_AT.x, M_AT.y, 3.226), 0.032, "metal", sides=10))
for k in range(3):
    a = 30.0 + 120.0 * k
    guard = [pol(0.097, a, 3.070), pol(0.097, a, 3.150), pol(0.028, a, 3.214)]
    guard = rh_hs._fillet(guard, 0.04, 3)
    add(tube("m_guard", [p + Vector((M_AT.x, M_AT.y, 0.0)) for p in guard], 0.007, "metal", sides=6))

# --------------------------------------------------------------- holster
C_AT = pol(RR, TH_R)
_h = (C_AT - M_AT)
_h.z = 0.0
_h.normalize()
TH_H = math.degrees(math.atan2(_h.y, _h.x))  # mast -> reel
HOL = M_AT + _h * 0.19                     # holster cup axis
add(K.cyl("h_clamp", (M_AT.x, M_AT.y, 1.060), (M_AT.x, M_AT.y, 1.150), 0.094, "slate", sides=16))
parts += place(cbox("h_bracket", (0.075, -0.028, 1.075), (0.150, 0.028, 1.135), "slate",
                    bevel=0.005), TH_H, M_AT)
add(lathe_z("h_cup", [(0.0, 1.060), (0.050, 1.060), (0.058, 1.070), (0.058, 1.185), (0.052, 1.190),
                      (0.045, 1.190), (0.045, 1.140), (0.0, 1.140)], HOL.x, HOL.y, 0.0, ["accent"],
            seg=14))
add(lathe_z("h_plug", [(0.0, 1.130), (0.040, 1.130), (0.040, 1.215), (0.050, 1.225), (0.050, 1.310),
                       (0.040, 1.325), (0.032, 1.330), (0.026, 1.385), (0.0, 1.390)],
            HOL.x, HOL.y, 0.0, ["metal", "hull", "rubber"], seg=14,
            mis=[0, 0, 0, 1, 1, 1, 2, 2, 2]))

# ------------------------------------------------------------- cable reel
# local frame: x = axle (radial out from the pad centre), y = tangential, z up
reel = [
    cbox("r_plinth", (-0.200, -0.210, ZF - SINK), (0.200, 0.210, 0.250), "slate",
         vch=0.04, tch=0.015, bevel=0.005),
    K.cyl("r_axle", (-0.200, 0.0, AZ), (0.200, 0.0, AZ), 0.030, "pipe", sides=10),
    K.cyl("r_core", (-0.117, 0.0, AZ), (0.117, 0.0, AZ), 0.200, "rubber", sides=24),
]
for sx in (-1, 1):
    xa, xb = sorted((sx * 0.150, sx * 0.178))
    reel.append(cbox("r_cheek", (xa, -0.085, 0.240), (xb, 0.085, AZ + 0.090), "slate",
                     tch=0.05, tsides=[(0, 1), (0, -1)], bevel=0.004))
    reel.append(lathe_axis("r_flange", [(0.031, -0.011), (0.262, -0.011), (0.288, -0.004),
                                        (0.288, 0.004), (0.262, 0.011), (0.031, 0.011)],
                           (sx * 0.128, 0.0, AZ), (1, 0, 0), ["slate", "accent"], seg=24,
                           mis=[0, 1, 1, 1, 0, 0]))
    reel.append(lathe_axis("r_hub", [(0.0, 0.0), (0.062, 0.0), (0.062, 0.014), (0.050, 0.026),
                                     (0.0, 0.026)], (sx * 0.176, 0.0, AZ), (sx, 0, 0), ["metal"],
                           seg=16))
    for k in range(4):
        a = math.radians(45.0 + 90.0 * k)
        reel.append(K.bolt("r_hbolt", (sx * 0.201, 0.034 * math.cos(a), AZ + 0.034 * math.sin(a)),
                           (sx, 0, 0), r=0.008, h=0.006))
# crank on the centre-facing hub, accent knob
reel.append(K.box("r_crank", (-0.218, -0.014, AZ - 0.012), (-0.200, 0.014, AZ + 0.150), "slate",
                  bevel=0.004, seg=1))
reel.append(K.cyl("r_knob", (-0.218, 0.0, AZ + 0.130), (-0.272, 0.0, AZ + 0.130), 0.016, "accent",
                  sides=10))
parts += place(reel, TH_R, C_AT)

# the charging cable: one tube - a helix on the drum (outer layer, over the
# packed core), leaving the drum's mast side going up, looping over into the
# holstered connector's boot
RH, TR = 0.213, 0.021                      # helix radius, cable radius
A0, A1 = -0.096, 0.096
turns = (A1 - A0) / 0.044
nh = int(turns * 14)
Mr = xf(TH_R, C_AT)
pts = []
for i in range(nh + 1):
    s = i / nh
    phi = math.pi + 2 * math.pi * turns * (1.0 - s)    # ends at phi = pi heading up
    pts.append(Mr @ Vector((A0 + (A1 - A0) * s, RH * math.cos(phi), AZ + RH * math.sin(phi))))
hol_local = Mr.inverted() @ Vector((HOL.x, HOL.y, 0.0))
ctrl = [Vector((A1, -RH, AZ)),
        Vector((A1, -RH - 0.010, AZ + 0.200)),
        Vector((hol_local.x + 0.02, hol_local.y + 0.10, 1.420)),
        Vector((hol_local.x, hol_local.y + 0.03, 1.500)),
        Vector((hol_local.x, hol_local.y, 1.440)),
        Vector((hol_local.x, hol_local.y, 1.380))]
ctrl = rh_hs._fillet(ctrl, 0.07, 5)
pts += [Mr @ p for p in ctrl[1:]]
add(tube("cable", pts, TR, "rubber", sides=6))

role_stats(parts)

# ------------------------------------------------------------------ finish
ob = K.finish(parts, NAME)
shrink_underside_uv(ob)
K.face_camera(ob)                          # front (-Y) -> -X (default camera)
recentre(ob)
rmax = max(math.hypot(v.co.x, v.co.y) for v in ob.data.vertices)
rh_hs.log("max horizontal radius %.4f m (deck R %.3f)" % (rmax, R))
assert rmax <= R + 0.004, "a part leaves the deck circle (r %.3f > %.3f)" % (rmax, R)
os.makedirs(OUT, exist_ok=True)
bpy.ops.object.select_all(action="DESELECT")
ob.select_set(True)
bpy.context.view_layer.objects.active = ob
bpy.ops.export_scene.gltf(filepath=os.path.join(OUT, NAME + "_preview.glb"), export_format="GLB",
                          use_selection=True, export_apply=True)
rh_hs.log("preview glb written")
if not PREVIEW_ONLY:
    files = K.bake_and_export(ob, NAME, OUT, size=SIZE, dust=0.22)
    rh_hs.log("files: %s" % files)
