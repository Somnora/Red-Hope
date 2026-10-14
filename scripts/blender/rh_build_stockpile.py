"""Build the Stockpile from code - the hard-surface remake (2026-10-14).

  blender -b -P scripts/blender/rh_build_stockpile.py -- [--out Martians/gen/hs/Stockpile]
      [--size 2048] [--preview-only]

The game's Stockpile ("Open storage for mined and fabricated solids", a 3 x 3
cell footprint). Replaces the image-to-3D Stockpile flagged on the 2026-10-13
triage: a gold melted heap - crates fused into one puffy lump, the whole thing
tinted in its TintFor amber (Saved/RHLineup/png/Models2__Stockpile__v035.png).
The DESIGN is kept: the approved hero reference
Martians/gen/batch4/cutout/stockpile.png - a STRAPPED BLOCK: bone-white rugged
cargo cases of a few sizes (ribbed lids, corner guards, latches, handles) and a
couple of drums, bound together by amber cargo straps running over the top and
down the sides, on a heavy low platform with chamfered corners, fork pockets and
hazard-stripe edging. Only the construction changes: every case is its own
exact solid with a dark lid seam, so the stack reads as separate crates instead
of one melted mass.

Layout facts:
  - built with the FRONT (fork pockets, the most readable crate faces and the
    strap buckles) at -Y, then K.face_camera() turns it to -X, the face the
    game's default strategy camera meets (measured 2026-10-13).
  - real-world metres: platform 5.6 m square and 0.6 m tall; cases 0.9-1.6 m
    long in tiers 0.52-0.66 m tall; drums 0.56 m across, 0.9 m tall. Overall
    5.75 x 5.75 x 3.02 m (the fork-pocket frames stand 7.5 cm proud); the
    visualizer fits the larger horizontal extent to 6 m, ~1.04x.
  - the stack is STEPPED, not a cube, and rises toward the back so the 54 deg
    camera sees every level's top: a 2x2 drum group front right (top +0.90 m),
    two tiers of cases front left (+1.28 m), a three-tier plateau behind
    (+1.88 m) and a fourth tier of two cases at the back left (+2.40 m).
  - straps are generated from the stack itself: for each lane the strap follows
    the upper envelope of the cases (and drums) under it, rounds the convex
    edges, cuts the concave steps short (a tensioned strap bridges a step's
    foot), and drops into a deck cleat at both ends. Three straps run front to
    back, three left to right; where they cross, the cross strap rides 6 mm over
    the other (no coplanar faces). Case ribs, guards, latches and handles keep
    out of the strap lanes - real rugged cases have strap channels in the lid.
  - accent (#ECC46C, TintFor Stockpile) is TRIM only: the straps (a matte
    "strap" role in the accent colour - webbing does not chip like paint) and
    the platform's hazard-stripe edging (accent role). Cases bone-white with
    slate seams and corner guards, light-alloy latches and buckles; drums and
    platform dark slate.
  - the glow is ONE hue per asset in game (amber, EmissiveColor ~(4.5, 2.2, 0.2)
    linear) and a stockpile is a still structure, so the only lit elements are
    four small amber marker lenses on the platform's corner pods ("glow" role,
    albedo #FFB040 so they read amber in daylight too).
  - hidden cases (inside the stack, every face covered) are a plain core block;
    details are added only where the face they sit on can be seen.
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
OUT = argv[argv.index("--out") + 1] if "--out" in argv else "Martians/gen/hs/Stockpile"
SIZE = int(argv[argv.index("--size") + 1]) if "--size" in argv else 2048
PREVIEW_ONLY = "--preview-only" in argv
NAME = "Stockpile"

K = rh_hs.Kit(accent_hex="#ECC46C")       # TintFor(Stockpile), art bible 2.3
# Lit role: amber marker lenses. Albedo matches the in-game glow hue; preview
# emission is the game's EmissiveColor normalised, kept low so review frames
# show amber instead of clipping white.
K.roles["glow"] = dict(albedo="#FFB040", metal=0.0, rough=0.25, emit=1, dust=0.0)
K.mats["glow"] = K._preview_material("glow", K.roles["glow"])
_gb = K.mats["glow"].node_tree.nodes["Principled BSDF"]
_gb.inputs["Emission Color"].default_value = (1.0, 0.49, 0.045, 1.0)
_gb.inputs["Emission Strength"].default_value = 1.5
# Cargo straps: the accent colour as matte webbing. paint=0 keeps the bake's
# edge chipping (bare grey metal) off the fabric.
K.roles["strap"] = dict(albedo="#ECC46C", metal=0.0, rough=0.70, dust=0.6)
K.mats["strap"] = K._preview_material("strap", K.roles["strap"])

parts = []
add = parts.append
S2 = math.sqrt(0.5)


# ------------------------------------------------------------ local helpers
def convex(name, planes, role, bevel=0.0, seg=1):
    """Closed convex solid = intersection of half-spaces n.p <= d (optionally
    bevelled). One watertight hull however many chamfers meet at a corner -
    stacked bmesh bevels leave pinholes (AirFilter round 1)."""
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


def bezel(name, center, w, h, proud, radius, rim, recess, rim_role, inner_role, facing=-1):
    """K.bezel with its failure modes refused (rim must stay >= 5 mm under the
    corner radius or the window corners fold or collapse - AirFilter round 1)."""
    assert radius - rim >= 0.005, "%s: bezel rim %.3f must be < radius %.3f - 5 mm" % (
        name, rim, radius)
    return K.bezel(name, center, w, h, proud, radius=radius, rim=rim, recess=recess,
                   rim_role=rim_role, inner_role=inner_role, facing=facing)


def lathe_z(name, prof, cx, cy, z0, roles, seg=14, mis=None):
    """Revolve a CLOSED (r, z) profile about the vertical axis through (cx, cy);
    r = 0 points become poles. Authored counter-clockwise (outer wall going up,
    top going inward)."""
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


def prism(name, poly, z0, z1, role):
    """Vertical prism over a convex XY polygon (hazard stripes on the deck)."""
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


def turn(objs, deg):
    """Swing freshly built parts about Z: parts built on the -Y 'virtual face'
    are carried onto another side of the square platform."""
    M = Matrix.Rotation(math.radians(deg), 4, "Z")
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


def role_stats(objs):
    """Surface share per role over all faces (hidden ones included, so the
    visible accent share is higher - the QA raster measures that)."""
    tot = {}
    for o in objs:
        me = o.data
        for p in me.polygons:
            r = me.materials[p.material_index]["rh_role"]
            tot[r] = tot.get(r, 0.0) + p.area
    s = sum(tot.values())
    rh_hs.log("role share (all faces): %s" % ", ".join(
        "%s %.1f%%" % (k, 100 * v / s) for k, v in sorted(tot.items(), key=lambda kv: -kv[1])))
    tris = {}
    for o in objs:
        key = o.name.rstrip("0123456789._-").split("_")[0]
        tris[key] = tris.get(key, 0) + sum(len(p.vertices) - 2 for p in o.data.polygons)
    rh_hs.log("tris by part: %s" % ", ".join(
        "%s %d" % kv for kv in sorted(tris.items(), key=lambda kv: -kv[1])))


# ------------------------------------------------------------------ layout
P = 2.80                                   # platform half size (5.6 m square)
D = 0.60                                   # deck (platform top) height
CH = 0.40                                  # platform vertical corner chamfer
CT = 0.05                                  # platform top-edge chamfer
G = 0.03                                   # gap between neighbouring cases
W = 0.12                                   # strap width
# tier tops
L1, L2, L3, L4 = D + 0.66, D + 1.28, D + 1.88, D + 2.40

# rows (y) of the stack, front (-Y) to back
FR = (-2.15, -0.95)                        # front row: cases left, drums right
MR = (-0.92, 0.58)                         # middle row
BR = (0.61, 2.15)                          # back row
T4Y = (0.85, 2.15)                         # fourth tier (back left), set back

CRATES = []                                # (lo, hi)


def row(y, z0, z1, xs):
    """Cases side by side in one row; an x-span may carry a third item, a
    (y0, y1) override - small setbacks off the strap lanes, so the walls are
    not ruled flat."""
    for span in xs:
        x0, x1 = span[0], span[1]
        ya, yb = span[2] if len(span) > 2 else y
        CRATES.append(((x0, ya, z0), (x1, yb, z1)))


# front-left block: tier 1 is two rows of LONG cases (their long faces, latches
# and seam lines are the front the camera reads), tier 2 three squarer cases
# turned the other way, joints staggered like brickwork
row((-2.15, -1.57), D, L1, [(-2.25, -0.70), (-0.67, 0.85)])
row((-1.54, -0.95), D, L1, [(-2.25, -0.45), (-0.42, 0.85)])
row(FR, L1, L2, [(-2.25, -1.25), (-1.22, -0.15), (-0.12, 0.85, (-2.10, -0.95))])
# middle row, three tiers; the top tier mixes widths (lid ribs along y)
row(MR, D, L1, [(-2.25, -0.80), (-0.77, 0.70), (0.73, 2.25)])
row(MR, L1, L2, [(-2.25, -0.95), (-0.92, 0.60), (0.63, 2.25)])
row(MR, L2, L3, [(-2.25, -1.35), (-1.32, 0.05), (0.08, 1.02, (-0.86, 0.58)), (1.05, 2.25)])
# back row, three tiers
row(BR, D, L1, [(-2.25, -0.60), (-0.57, 0.95), (0.98, 2.25)])
row(BR, L1, L2, [(-2.25, -0.75), (-0.72, 0.85), (0.88, 2.25)])
row(BR, L2, L3, [(-2.25, -1.05), (-1.02, 0.55), (0.58, 1.40), (1.43, 2.25)])
# fourth tier, back left: one case each way
row(T4Y, L3, L4, [(-2.25, -1.20), (-1.17, 0.30)])

# drums: a 2x2 group front right (body r 0.28, rolling hoops / chime r 0.295);
# centres 0.60 / 0.62 m apart so neighbouring chimes never overlap (round 2 had
# them 0.58 apart: the coplanar chime tops z-fought where the drums met)
DR, DRH, DH = 0.28, 0.295, 0.90
DRUMS = [(x, y) for y in (-1.85, -1.25) for x in (1.32, 1.94)]

# strap lanes: (axis the strap runs along, lateral position, uses drums)
LANES = [("y", -1.60, True), ("y", -0.35, True), ("y", 1.65, False),
         ("x", -1.85, True), ("x", -0.20, True), ("x", 1.30, True)]


def occupied(p, pad=0.0):
    x, y, z = p
    if z < D - 1e-6:
        return True
    for (x0, y0, z0), (x1, y1, z1) in CRATES:
        if x0 - pad <= x <= x1 + pad and y0 - pad <= y <= y1 + pad and z0 - pad <= z <= z1 + pad:
            return True
    for cx, cy in DRUMS:
        if (x - cx) ** 2 + (y - cy) ** 2 <= (DRH + pad) ** 2 and D <= z <= D + DH + pad:
            return True
    return False


def free(p):
    return not occupied(p)


def lane_hit(x0, x1, y0, y1, m=0.03, kinds=("x", "y")):
    """True if the XY span x0..x1, y0..y1 overlaps a strap lane (+ margin)."""
    for ax, s, _ in LANES:
        if ax not in kinds:
            continue
        a0, a1 = (x0, x1) if ax == "y" else (y0, y1)
        if a0 < s + W / 2 + m and a1 > s - W / 2 - m:
            return True
    return False


# -------------------------------------------------------------- the platform
# rubber skid band (reads as the shadow gap under the deck), slate body with
# chamfered corners and a chamfered top edge
add(cbox("skid", (-P + 0.10, -P + 0.10, 0.0), (P - 0.10, P - 0.10, 0.11), "rubber",
         vch=CH - 0.06, bevel=0.0))
add(cbox("deck", (-P, -P, 0.08), (P, P, D), "slate", vch=CH, tch=CT, bevel=0.012))

# per-face kit, built on the -Y face and swung onto all four sides: two fork
# pockets (alloy frames, dark recess) and a raised alloy kick plate between them
SINK = 0.003
for k, deg in enumerate((0.0, 90.0, 180.0, 270.0)):
    face = []
    for sx in (-1, 1):
        fb, _ = bezel("fork%d_%d" % (k, sx), (sx * 1.30, -P + SINK, 0.30), 1.10, 0.30,
                      0.075 + SINK, radius=0.06, rim=0.05, recess=0.07, rim_role="metal",
                      inner_role="rubber")
        face.append(fb)
    face.append(cbox("kick%d" % k, (-0.55, -P - 0.022, 0.20), (0.55, -P + SINK, 0.46), "metal",
                     bevel=0.008))
    for bx in (-0.50, 0.50):
        for bz in (0.24, 0.42):
            face.append(K.bolt("kbolt", (bx, -P - 0.022, bz), (0, -1, 0), r=0.016))
    parts += turn(face, deg)

# corner chamfer faces: alloy bumper plates (the platform's corner guards)
for ex in (-1, 1):
    for ey in (-1, 1):
        n = Vector((ex * S2, ey * S2, 0.0))
        c = Vector((ex * (P - CH / 2), ey * (P - CH / 2), 0.33))
        t = Vector((-ey * S2, ex * S2, 0.0))
        bm = bmesh.new()
        bmesh.ops.create_cube(bm, size=1.0)
        for v in bm.verts:
            v.co = Vector((v.co.x * 0.40, v.co.y * 0.045, v.co.z * 0.34))
        bmesh.ops.bevel(bm, geom=list(bm.edges), offset=0.008, offset_type="OFFSET",
                        segments=1, profile=0.5, affect="EDGES", clamp_overlap=True)
        R = Matrix((t, n, (0, 0, 1))).transposed().to_4x4()
        bmesh.ops.transform(bm, matrix=Matrix.Translation(c + n * 0.012) @ R, verts=bm.verts)
        add(K._obj("bumper", bm, ["metal"]))

# corner marker pods on the deck: alloy housing, amber lens (the only glow)
for ex in (-1, 1):
    for ey in (-1, 1):
        cx, cy = ex * 2.47, ey * 2.47
        add(K.cyl("pod", (cx, cy, D - 0.01), (cx, cy, D + 0.055), 0.13, "slate", sides=16))
        add(K.cyl("pod_ring", (cx, cy, D + 0.05), (cx, cy, D + 0.07), 0.105, "metal", sides=16))
        add(K.cyl("lens", (cx, cy, D + 0.065), (cx, cy, D + 0.105), 0.075, "glow", sides=16))

# hazard-stripe edging on the deck margin, all four sides: a rubber backing
# strip with accent diagonal stripes 5 mm proud of it
HB0, HB1 = P - 0.32, P - 0.12              # band inner / outer offset from centre
for k, deg in enumerate((0.0, 90.0, 180.0, 270.0)):
    band = [K.box("hz_back%d" % k, (-2.22, -HB1, D - 0.003), (2.22, -HB0, D + 0.005),
                  "rubber", bevel=0.0)]
    pitch, sw, sl = 0.22, 0.11, HB1 - HB0
    x = -2.20
    while x + sw + sl <= 2.20 + 1e-6:
        band.append(prism("hz%d" % k, [(x, -HB1 + 0.01), (x + sw, -HB1 + 0.01),
                                       (x + sw + sl - 0.02, -HB0 - 0.01),
                                       (x + sl - 0.02, -HB0 - 0.01)],
                          D + 0.001, D + 0.010, "accent"))
        x += pitch
    parts += turn(band, deg)


# ------------------------------------------------------------------ cases
def face_box(name, nax, sgn, plane, a_c, a_half, zlo, zhi, proud, role, sink=0.003,
             bevel=0.0):
    """Box on a vertical face whose outward normal is sgn along axis nax
    (0 = x, 1 = y): from `sink` inside the face to `proud` outside it."""
    oax = 1 - nax
    lo, hi = [0.0, 0.0, zlo], [0.0, 0.0, zhi]
    n0, n1 = sorted((plane - sgn * sink, plane + sgn * proud))
    lo[nax], hi[nax] = n0, n1
    lo[oax], hi[oax] = a_c - a_half, a_c + a_half
    return K.box(name, lo, hi, role, bevel=bevel, seg=1)


def face_free(nax, sgn, plane, a_c, z):
    p = [0.0, 0.0, z]
    p[nax] = plane + sgn * 0.06
    p[1 - nax] = a_c
    return free(p)


def face_lane(nax, a0, a1):
    """A face detail spanning a0..a1 along the face collides with a strap that
    drops down this face: faces normal to y carry the y-lanes, faces normal to
    x carry the x-lanes."""
    if nax == 1:
        return lane_hit(a0, a1, -9, 9, kinds=("y",))
    return lane_hit(-9, 9, a0, a1, kinds=("x",))


def crate(i, lo, hi):
    x0, y0, z0 = lo
    x1, y1, z1 = hi
    h = z1 - z0
    zs = z0 + 0.70 * h                     # lid seam
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    long_x = (x1 - x0) >= (y1 - y0)

    # which faces can be seen (3 x 3 samples just outside each face)
    def exposed(nax, sgn):
        hits = 0
        for fa in (0.15, 0.5, 0.85):
            for fz in (0.15, 0.5, 0.85):
                p = [0.0, 0.0, z0 + fz * h]
                p[nax] = (x1 if sgn > 0 else x0) if nax == 0 else (y1 if sgn > 0 else y0)
                p[nax] += sgn * 0.06
                if nax == 0:
                    p[1] = y0 + fa * (y1 - y0)
                else:
                    p[0] = x0 + fa * (x1 - x0)
                hits += free(p)
        return hits > 0
    sides = {(nax, sgn): exposed(nax, sgn) for nax in (0, 1) for sgn in (-1, 1)}
    top = any(free((x0 + fa * (x1 - x0), y0 + fb * (y1 - y0), z1 + 0.06))
              for fa in (0.15, 0.5, 0.85) for fb in (0.15, 0.5, 0.85))
    if not top and not any(sides.values()):
        # buried: a plain core so seams never show a void
        return [K.box("core%d" % i, (x0 + 0.01, y0 + 0.01, z0), (x1 - 0.01, y1 - 0.01, z1 - 0.01),
                      "slate", bevel=0.0)]
    out = []
    VCH = 0.05
    out.append(cbox("body%d" % i, lo, (x1, y1, zs - 0.012), "hull", vch=VCH))
    ins = 0.018
    out.append(cbox("seam%d" % i, (x0 + ins, y0 + ins, zs - 0.022), (x1 - ins, y1 - ins, zs + 0.022),
                    "slate", vch=VCH - 0.586 * ins))
    out.append(cbox("lid%d" % i, (x0, y0, zs + 0.012), hi, "hull", vch=VCH, tch=0.03))

    if top:
        # lid ribs along the long axis, broken by strap channels
        span = (y1 - y0) if long_x else (x1 - x0)
        for f in (0.27, 0.73):
            c = (y0 if long_x else x0) + f * span
            a0, a1 = ((x0 if long_x else y0) + 0.16, (x1 if long_x else y1) - 0.16)
            run, t = [], a0
            pieces = []
            while t <= a1 + 1e-6:
                if long_x:
                    ok = free((t, c, z1 + 0.04)) and not lane_hit(t, t, c - 0.03, c + 0.03)
                else:
                    ok = free((c, t, z1 + 0.04)) and not lane_hit(c - 0.03, c + 0.03, t, t)
                if ok:
                    run.append(t)
                elif run:
                    pieces.append((run[0], run[-1]))
                    run = []
                t += 0.02
            if run:
                pieces.append((run[0], run[-1]))
            for pa, pb in pieces:
                if pb - pa < 0.14:
                    continue
                if long_x:
                    rlo, rhi = (pa, c - 0.03, z1 - 0.004), (pb, c + 0.03, z1 + 0.024)
                else:
                    rlo, rhi = (c - 0.03, pa, z1 - 0.004), (c + 0.03, pb, z1 + 0.024)
                out.append(K.box("rib%d" % i, rlo, rhi, "hull", bevel=0.0))
        # slate corner guards over the lid's top corners
        for ex in (-1, 1):
            for ey in (-1, 1):
                gx = (x0 - 0.012, x0 + 0.13) if ex < 0 else (x1 - 0.13, x1 + 0.012)
                gy = (y0 - 0.012, y0 + 0.13) if ey < 0 else (y1 - 0.13, y1 + 0.012)
                ccx, ccy = (x0 if ex < 0 else x1), (y0 if ey < 0 else y1)
                if not (free((ccx + ex * 0.05, ccy + ey * 0.05, z1 - 0.07))
                        and free((ccx - ex * 0.06, ccy - ey * 0.06, z1 + 0.04))):
                    continue
                if lane_hit(gx[0], gx[1], gy[0], gy[1], m=0.01):
                    continue
                out.append(cbox("guard%d" % i, (gx[0], gy[0], z1 - 0.15), (gx[1], gy[1], z1 + 0.012),
                                "slate", vch=0.057, tch=0.037, corners=[(ex, ey)],
                                tsides=[(ex, 0), (0, ey)]))

    # side details: latches (long faces), handles (short faces), a stencil plate
    for (nax, sgn), vis in sides.items():
        if not vis:
            continue
        plane = ((x1 if sgn > 0 else x0) if nax == 0 else (y1 if sgn > 0 else y0))
        a_lo, a_hi = (y0, y1) if nax == 0 else (x0, x1)
        length = a_hi - a_lo
        is_long = (nax == 1) == long_x
        if is_long:
            for fr in ((0.18, 0.82), (0.26, 0.74), (0.12, 0.88)):
                spots = [a_lo + f * length for f in fr]
                if all(not face_lane(nax, s - 0.08, s + 0.08) for s in spots):
                    break
            for s in spots:
                if face_lane(nax, s - 0.08, s + 0.08) or not face_free(nax, sgn, plane, s, zs):
                    continue
                out.append(face_box("latch%d" % i, nax, sgn, plane, s, 0.045, zs - 0.10, zs + 0.075,
                                    0.03, "metal"))
            # stencil plate low on the body
            zc = z0 + 0.34 * h
            for s in (a_lo + 0.5 * length, a_lo + 0.32 * length, a_lo + 0.68 * length):
                if not face_lane(nax, s - 0.2, s + 0.2) and face_free(nax, sgn, plane, s, zc):
                    out.append(face_box("label%d" % i, nax, sgn, plane, s, 0.16, zc - 0.065,
                                        zc + 0.065, 0.007, "slate"))
                    break
        else:
            zc = z0 + 0.42 * h
            for s in (a_lo + 0.5 * length, a_lo + 0.3 * length, a_lo + 0.7 * length):
                if not face_lane(nax, s - 0.17, s + 0.17) and face_free(nax, sgn, plane, s, zc):
                    out.append(face_box("hplate%d" % i, nax, sgn, plane, s, 0.15, zc - 0.07, zc + 0.07,
                                        0.008, "slate"))
                    # the bar starts inside the plate, not on the case wall:
                    # no coplanar back faces with the plate
                    out.append(face_box("handle%d" % i, nax, sgn, plane, s, 0.11, zc - 0.022,
                                        zc + 0.022, 0.045, "metal", sink=-0.004))
                    break
    return out


for i, (lo, hi) in enumerate(CRATES):
    parts += crate(i, lo, hi)

# ------------------------------------------------------------------- drums
DPROF = [(0.0, 0.0), (DR, 0.0), (DR, 0.27), (DRH, 0.285), (DRH, 0.315), (DR, 0.33),
         (DR, 0.57), (DRH, 0.585), (DRH, 0.615), (DR, 0.63), (DR, DH - 0.03),
         (DRH - 0.003, DH), (DR - 0.03, DH), (DR - 0.03, DH - 0.02), (0.0, DH - 0.02)]
# grey painted steel (reference), slate rolling hoops and chime: slate-bodied
# drums (round 1) rendered as black holes next to the bone-white cases
K.roles["drum"] = dict(albedo="#8E9194", metal=0.25, rough=0.50, paint=1, dust=1.0)
K.mats["drum"] = K._preview_material("drum", K.roles["drum"])
DMIS = [0, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 0]       # per profile edge: 0 paint, 1 slate
for k, (cx, cy) in enumerate(DRUMS):
    add(lathe_z("drum%d" % k, DPROF, cx, cy, D, ["drum", "slate"], seg=14, mis=DMIS))
    # bungs clear of the cross strap's lane over the front row (|dy| > W/2 + r)
    add(K.cyl("bung%d" % k, (cx + 0.10, cy + 0.14, D + DH - 0.025), (cx + 0.10, cy + 0.14, D + DH + 0.012),
              0.045, "metal", sides=6))
    add(K.cyl("bung%d" % k, (cx - 0.12, cy - 0.15, D + DH - 0.025), (cx - 0.12, cy - 0.15, D + DH + 0.008),
              0.03, "metal", sides=6))


def drum_band(name, rin, rout, z0, z1, role, n=5):
    """Horizontal band round the 2x2 drum group: a rounded rectangle (arcs on
    the four outer quadrants, straight runs between), as a closed loop."""
    xs = sorted({c[0] for c in DRUMS})
    ys = sorted({c[1] for c in DRUMS})
    corners = [((xs[1], ys[0]), -90.0), ((xs[1], ys[1]), 0.0), ((xs[0], ys[1]), 90.0),
               ((xs[0], ys[0]), 180.0)]
    ang = []
    for (cx, cy), a0 in corners:
        for j in range(n + 1):
            a = math.radians(a0 + 90.0 * j / n)
            ang.append((cx, cy, math.cos(a), math.sin(a)))
    bm = bmesh.new()
    ring = []
    for cx, cy, ca, sa in ang:
        ring.append([bm.verts.new((cx + r * ca, cy + r * sa, z)) for r, z in
                     ((rin, z0), (rout, z0), (rout, z1), (rin, z1))])
    m = len(ring)
    for i in range(m):
        a, b = ring[i], ring[(i + 1) % m]
        for k in range(4):
            q = (k + 1) % 4
            bm.faces.new((a[k], b[k], b[q], a[q]))
    bmesh.ops.remove_doubles(bm, verts=bm.verts[:], dist=1e-5)
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    return K._obj(name, bm, [role])


add(drum_band("drum_band", DR - 0.012, DR + 0.006, D + 0.40, D + 0.50, "strap"))
# band buckle where the front run bridges the two front drums
_bx = (DRUMS[0][0] + DRUMS[1][0]) / 2
_by = DRUMS[0][1] - DR - 0.006
add(K.box("band_buckle", (_bx - 0.09, _by - 0.035, D + 0.38), (_bx + 0.09, _by + 0.01, D + 0.52), "metal",
          bevel=0.006, seg=1))


# ------------------------------------------------------------------ straps
def strap_path(axis, s, use_drums):
    """Upper envelope of everything under lane s, as an (u, z) polyline from the
    deck on one side, over the top, to the deck on the other."""
    sup = []
    for (x0, y0, z0), (x1, y1, z1) in CRATES:
        l0, l1 = (x0, x1) if axis == "y" else (y0, y1)
        u0, u1 = (y0, y1) if axis == "y" else (x0, x1)
        if l0 < s + W / 2 and l1 > s - W / 2:
            sup.append((u0, u1, z1))
    if use_drums:
        for cx, cy in DRUMS:
            lc, uc = (cx, cy) if axis == "y" else (cy, cx)
            dmin = max(0.0, abs(lc - s) - W / 2)
            if dmin < DRH:
                c = math.sqrt(DRH ** 2 - dmin ** 2)
                sup.append((uc - c, uc + c, D + DH))
    bps = sorted({u for a, b, _ in sup for u in (a, b)})
    iv = []
    for a, b in zip(bps, bps[1:]):
        m = (a + b) / 2
        hs = [z for u0, u1, z in sup if u0 <= m <= u1]
        iv.append([a, b, max(hs) if hs else None])
    changed = True
    while changed:                         # bridge gaps and narrow dips (< 0.2 m)
        changed = False
        for i in range(1, len(iv) - 1):
            a, b, h = iv[i]
            hl, hr = iv[i - 1][2], iv[i + 1][2]
            if hl is None or hr is None:
                continue
            if (h is None or (h < hl and h < hr)) and b - a < 0.2:
                iv[i][2] = min(hl, hr)
                changed = True
    merged = []
    for a, b, h in iv:
        if merged and abs(merged[-1][2] - h) < 1e-6:
            merged[-1][1] = b
        else:
            merged.append([a, b, h])
    pts = [Vector((merged[0][0], D))]
    for a, b, h in merged:
        pts += [Vector((a, h)), Vector((b, h))]
    pts.append(Vector((merged[-1][1], D)))
    clean = [pts[0]]
    for p in pts[1:]:
        if (p - clean[-1]).length > 1e-5:
            clean.append(p)
    # round convex edges (the strap wraps the lid's chamfer); cut concave steps
    out = [clean[0]]
    for i in range(1, len(clean) - 1):
        a, p, b = clean[i - 1], clean[i], clean[i + 1]
        d1, d2 = (p - a), (b - p)
        cross = d1.x * d2.y - d1.y * d2.x
        t = min(0.05, d1.length / 2, d2.length / 2)
        s0, e0 = p - d1.normalized() * t, p + d2.normalized() * t
        if cross < 0:                      # convex: quadratic-Bezier fillet
            for k in range(4):
                u = k / 3
                out.append((1 - u) ** 2 * s0 + 2 * (1 - u) * u * p + u ** 2 * e0)
        else:                              # concave: tensioned strap bridges the foot
            out += [s0, e0]
    out.append(clean[-1])
    return out


def offset_path(pts, d):
    n = len(pts)
    segn = []
    for i in range(n - 1):
        t = (pts[i + 1] - pts[i]).normalized()
        segn.append(Vector((-t.y, t.x)))
    res = []
    for i in range(n):
        if i == 0:
            res.append(pts[0] + segn[0] * d)
        elif i == n - 1:
            res.append(pts[-1] + segn[-1] * d)
        else:
            m = (segn[i - 1] + segn[i])
            m = m.normalized() if m.length > 1e-6 else segn[i]
            res.append(pts[i] + m * (d / max(0.3, m.dot(segn[i]))))
    return res


def strap(name, axis, s, use_drums, inner, outer):
    path = strap_path(axis, s, use_drums)
    pin, pout = offset_path(path, inner), offset_path(path, outer)
    # sink both ends 12 mm into the deck
    for arr in (pin, pout):
        arr[0] = Vector((arr[0].x, D - 0.012))
        arr[-1] = Vector((arr[-1].x, D - 0.012))

    def P3(lat, uz):
        return (lat, uz.x, uz.y) if axis == "y" else (uz.x, lat, uz.y)
    bm = bmesh.new()
    st = []
    for a, b in zip(pin, pout):
        st.append([bm.verts.new(P3(s - W / 2, a)), bm.verts.new(P3(s + W / 2, a)),
                   bm.verts.new(P3(s + W / 2, b)), bm.verts.new(P3(s - W / 2, b))])
    for i in range(len(st) - 1):
        for k in range(4):
            q = (k + 1) % 4
            bm.faces.new((st[i][k], st[i][q], st[i + 1][q], st[i + 1][k]))
    bm.faces.new(st[0])
    bm.faces.new(list(reversed(st[-1])))
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    return K._obj(name, bm, ["strap"]), path


def lat_box(axis, s, half_lat, u0, u1, z0, z1, role, name, bevel=0.006):
    if axis == "y":
        return K.box(name, (s - half_lat, u0, z0), (s + half_lat, u1, z1), role, bevel=bevel, seg=1)
    return K.box(name, (u0, s - half_lat, z0), (u1, s + half_lat, z1), role, bevel=bevel, seg=1)


# buckle placement per lane: which end's drop carries the ratchet, and at what height
BUCKLE = {("y", -1.60): ("start", D + 0.95), ("y", -0.35): ("start", D + 0.95),
          ("y", 1.65): ("start", L2 + 0.30), ("x", -1.85): ("start", D + 0.95),
          ("x", -0.20): ("end", D + 0.95), ("x", 1.30): ("end", D + 0.95)}
for ax, s, use_drums in LANES:
    inner, outer = (-0.002, 0.016) if ax == "y" else (-0.004, 0.022)   # cross straps ride on top
    ob, path = strap("strap_%s%d" % (ax, int(round(s * 100))), ax, s, use_drums, inner, outer)
    add(ob)
    # deck cleats at both ends (none behind the drums)
    for end, sgn in (("start", -1), ("end", 1)):
        u = path[0].x if end == "start" else path[-1].x
        ua, ub = sorted((u + sgn * 0.005, u + sgn * 0.15))
        foot = [(s + dl, uu, D + 0.03) if ax == "y" else (uu, s + dl, D + 0.03)
                for dl in (-0.12, 0.0, 0.12) for uu in (u + sgn * 0.03, u + sgn * 0.16)]
        if any(occupied(p, pad=0.01) for p in foot):
            continue
        add(cbox("cleat", (s - 0.11, ua, D - 0.01) if ax == "y" else (ua, s - 0.11, D - 0.01),
                 (s + 0.11, ub, D + 0.075) if ax == "y" else (ub, s + 0.11, D + 0.075), "slate",
                 tch=0.025, bevel=0.0))
        ring_u = u + sgn * 0.11
        add(lat_box(ax, s, 0.075, ring_u - 0.025, ring_u + 0.025, D + 0.07, D + 0.10, "metal", "dring",
                    bevel=0.0))
    # ratchet buckle on one vertical drop
    end, zb = BUCKLE[(ax, s)]
    u = path[0].x if end == "start" else path[-1].x
    sgn = -1 if end == "start" else 1
    uo = u + sgn * outer
    ua, ub = sorted((uo - sgn * 0.006, uo + sgn * 0.032))
    add(lat_box(ax, s, W / 2 + 0.018, ua, ub, zb - 0.11, zb + 0.11, "metal", "buckle"))
    uc = uo + sgn * 0.045
    p0 = (s - W / 2 - 0.03, uc, zb + 0.05) if ax == "y" else (uc, s - W / 2 - 0.03, zb + 0.05)
    p1 = (s + W / 2 + 0.03, uc, zb + 0.05) if ax == "y" else (uc, s + W / 2 + 0.03, zb + 0.05)
    add(K.cyl("ratchet", p0, p1, 0.028, "pipe", sides=10))
    rh_hs.log("strap %s %.2f: %d path points, top %.2f m" % (ax, s, len(path), max(p.y for p in path)))

role_stats(parts)

# ------------------------------------------------------------------ finish
ob = K.finish(parts, NAME)
K.face_camera(ob)                          # front (-Y) -> -X (default camera)
recentre(ob)
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
