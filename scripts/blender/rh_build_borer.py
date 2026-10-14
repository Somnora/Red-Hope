"""Build the Borer from code - the hard-surface remake (2026-10-14).

  blender -b -P scripts/blender/rh_build_borer.py -- [--out Martians/gen/hs/Borer]
      [--size 2048] [--preview-only]

The game's Borer ("Digs the shaft and carves out habitat floors through solid
rock"). Replaces the image-to-3D Borer: a gold, melted excavator whose tracks
had fused into puffy slabs, whose bucket teeth had shredded and whose whole
hull was tinted in its TintFor amber (Saved/RHLineup/png/Models2__Borer__v035
and __v215). The DESIGN is kept: the approved hero reference
Martians/gen/borer/cutout/borer.png - a heavy tracked excavator: two crawler
tracks with ribbed track pads round an idler and a sprocket, a slewing upper
house (glazed cab at the front-left, a side box at the front-right, engine
housing with louvres and an exhaust stack, a heavy counterweight tail), a
banana boom and a stick on pinned joints with their hydraulic cylinders and
hoses, and a toothed bucket. The digging arm is why the director kept this
design. Only the construction changes: every part is an exact primitive, the
track pads are a real ribbed belt, and the emissive mask is the lights.

Layout facts:
  - built with the ARM at -Y (the machine's front), then K.face_camera() turns
    it to -X, the face the game's default strategy camera meets (measured
    2026-10-13). The cab sits on the operator's left of the boom (+X before the
    turn), so the player sees it on the RIGHT of the arm, as in the reference.
  - PARKED AT AN ANGLE (PARK_YAW = -28 deg, the whole machine, tracks too).
    Round 1 pointed the arm straight at the camera: from the game's 54 deg
    pitch the boom's banana profile collapsed into a white column with a box
    under it and the digging arm - the reason the design was kept - did not
    read. Turned 28 deg, the default camera meets the boom's flank with its
    hazard bands, the cab's windscreen and door side, the toothed bucket and the
    cab-side track: the reference's own three-quarter composition. The arm
    still sits on the camera side. Free in scale: the larger horizontal extent
    stays 5.77 m (5.79 m straight) because the arm tip is narrow. A house SLEW
    was tried on paper and rejected: a slewed arm drops the bucket onto its own
    track front unless the reach grows ~0.5 m.
  - footprint: 3 x 3 cells; the visualizer fits the LARGER horizontal extent
    to 6 m. An arm stretched out flat would shrink the machine to a toy, so the
    arm is PARKED: boom raised, stick hanging near vertical, bucket curled with
    its mouth up and its teeth just in front of the house, between the tracks.
    The arm reaches 23 % of the track length past the track fronts (logged as
    "reach"), so the plan is 5.77 x 4.82 m after the yaw and the fit is ~1.04x.
  - palette follows the family (art bible 1): bone-white house, hood, cab, boom
    and stick; DarkSlate deck, counterweight, cab roof, undercarriage frames; a
    darker bare-steel "track" role for the belts; a RustBeam bucket (the
    reference's rust-brown bucket - in slate it merged with the dark tracks
    right behind it); gunmetal cylinder barrels, light-alloy rods, pins, teeth
    and hubs; black rubber hoses.
  - accent (#F3C459, TintFor Borer, mining amber) is TRIM only: the house belt
    line (the reference's amber pinstripe, wrapped round the whole house), the
    cab stripe at the sill, diagonal hazard bands on the boom and stick sides,
    the counterweight chevrons and flank panels, the grab rails and door grab
    handles (one yellow on the machine: the accent stands in for HazYellow),
    the hood-grille rim, the work-light housings, the MINE letters and the
    beacon's base ring. Ray-cast: 5.7-7.8 % of the visible surface from the
    four lineup views and the 54 deg game camera (3.9 % of ALL faces: the
    track belts' inner faces and the house undersides dilute that figure).
  - ONE glow hue per asset in game (amber, EmissiveColor ~(4.5, 2.2, 0.2)
    linear), so every lit element - the rotating-beacon dome on the cab roof,
    the two roof work lights, the two rear lamps - uses the "glow" role whose
    albedo (#FFB040) matches that hue and reads amber in daylight too.
  - every decal (stripes, letters, lenses, bolts) stands >= 4 mm proud of the
    plane it sits on and bites >= 2 mm into it; parts that meet overlap instead
    of sharing a face (the cab, side box and engine housing stand 2 cm inside
    the house skirt, the counterweight overhangs it by 1.5 cm, the bucket
    shell's outer face sits 6 mm inside its side plates' rims). Faces buried in
    another part (deck top, undersides of cab, box, engine, hood, counterweight)
    are dropped so they take no texture space.
Real-world metres, ground-centred pivot (bounds centred on XY, bottom at z=0).
"""
import bisect
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
OUT = argv[argv.index("--out") + 1] if "--out" in argv else "Martians/gen/hs/Borer"
SIZE = int(argv[argv.index("--size") + 1]) if "--size" in argv else 2048
PREVIEW_ONLY = "--preview-only" in argv
NAME = "Borer"

K = rh_hs.Kit(accent_hex="#F3C459")       # TintFor(Borer), art bible 2.3
# Lit role: amber work/beacon light. Albedo matches the in-game glow hue; the
# preview emission is that EmissiveColor normalised, kept low so review frames
# show amber instead of clipping to white (the kit's preview default is teal).
K.roles["glow"] = dict(albedo="#FFB040", metal=0.0, rough=0.25, emit=1, dust=0.0)
K.mats["glow"] = K._preview_material("glow", K.roles["glow"])
_gb = K.mats["glow"].node_tree.nodes["Principled BSDF"]
_gb.inputs["Emission Color"].default_value = (1.0, 0.49, 0.044, 1.0)
_gb.inputs["Emission Strength"].default_value = 1.2
# Track belts: bare, unpainted steel a step darker than DarkSlate, so the
# crawlers separate from the slate frames they wrap (no paint = no chipping).
K.roles["track"] = dict(albedo="#4A4D53", metal=0.45, rough=0.62, dust=1.0)
K.mats["track"] = K._preview_material("track", K.roles["track"])

low = []                                   # undercarriage (does not slew)
up = []                                    # house + arm (slews about Z by SLEW)
SLEW = 0.0                                 # degrees; house slew relative to the tracks
PARK_YAW = -28.0                           # degrees; the whole machine parked at an angle
SINK = 0.003                               # every frame/bezel bites 3 mm into its support
S2 = math.sqrt(0.5)


# =================================================================== helpers
def open_bottom(bm):
    """Drop the flat underside of a part that stands buried in another one: it
    can never be seen, but it would take its share of the 2K texture."""
    zmin = min(v.co.z for v in bm.verts)
    dead = [f for f in bm.faces if f.normal.z < -0.999 and
            all(abs(v.co.z - zmin) < 1e-5 for v in f.verts)]
    bmesh.ops.delete(bm, geom=dead, context="FACES_ONLY")


def convex_block(name, planes, role, bevel=0.012, seg=2, buried_bottom=False):
    """Closed convex solid = intersection of half-spaces n.p <= d, bevelled
    (rh_build_airfilter's helper: one watertight hull however many chamfers).
    buried_bottom: the part stands >= 1 cm deep in another; drop its underside."""
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
    if buried_bottom:
        open_bottom(bm)
    return K._obj(name, bm, [role])


def hull_solid(name, pts, role):
    """Convex hull of a point cloud (teeth, wedges)."""
    bm = bmesh.new()
    vs = [bm.verts.new(Vector(p)) for p in pts]
    res = bmesh.ops.convex_hull(bm, input=vs)
    bmesh.ops.delete(bm, geom=res["geom_interior"] + res["geom_unused"], context="VERTS")
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    return K._obj(name, bm, [role])


_AX = {"x": 0, "y": 1, "z": 2}


def prism(name, poly, a0, a1, role, axis="x", bevel=0.0, seg=1, buried_bottom=False):
    """Extrude a simple 2D polygon (may be concave) from a0 to a1 along `axis`.
    poly is (y, z) for axis x, (x, z) for y, (x, y) for z. Only the outline
    edges of the two caps are bevelled (rounded plate edges)."""
    ai = _AX[axis]

    def P(u, v, a):
        p = [u, v]
        p.insert(ai, a)
        return p

    bm = bmesh.new()
    A = [bm.verts.new(P(u, v, a0)) for (u, v) in poly]
    B = [bm.verts.new(P(u, v, a1)) for (u, v) in poly]
    bm.faces.new(A)
    bm.faces.new(B)
    n = len(poly)
    for i in range(n):
        j = (i + 1) % n
        bm.faces.new((A[i], A[j], B[j], B[i]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    if bevel > 0:
        caps = [e for e in bm.edges if abs(e.verts[0].co[ai] - e.verts[1].co[ai]) < 1e-7]
        bmesh.ops.bevel(bm, geom=caps, offset=bevel, offset_type="OFFSET", segments=seg,
                        profile=0.5, affect="EDGES", clamp_overlap=True)
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    if buried_bottom:
        open_bottom(bm)
    return K._obj(name, bm, [role])


def box_open(name, lo, hi, role, bevel=0.01, seg=2):
    """K.box whose underside is buried in the part it stands on."""
    ob = K.box(name, lo, hi, role, bevel=bevel, seg=seg)
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    open_bottom(bm)
    bm.to_mesh(ob.data)
    bm.free()
    return ob


def bolt(name, at, normal, r=0.014):
    """Hex bolt whose base sits 2 mm INSIDE the plate (the kit's bolt base is
    coplanar with the plate: flagged as a z-fight candidate on round 1)."""
    n = Vector(normal).normalized()
    return K.bolt(name, Vector(at) - n * 0.002, n, r=r, h=0.010)


def lathe_z(name, prof, centre, roles, seg=24, mis=None):
    """Spin a closed (r, z) profile about a vertical axis through centre (x, y)."""
    cx, cy = centre
    bm = bmesh.new()
    rings = []
    for (r, z) in prof:
        if r < 1e-6:
            v = bm.verts.new((cx, cy, z))
            rings.append([v] * seg)
        else:
            rings.append([bm.verts.new((cx + r * math.cos(2 * math.pi * s / seg),
                                        cy + r * math.sin(2 * math.pi * s / seg), z))
                          for s in range(seg)])
    n = len(prof)
    for k in range(n):
        i, j = k, (k + 1) % n
        for s in range(seg):
            s1 = (s + 1) % seg
            vs = []
            for v in (rings[i][s], rings[i][s1], rings[j][s1], rings[j][s]):
                if v not in vs:
                    vs.append(v)
            if len(vs) >= 3:
                f = bm.faces.new(vs)
                f.material_index = mis[k] if mis else 0
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    return K._obj(name, bm, roles)


def frame_matrix(origin, n, u):
    """Virtual build frame -> surface: virtual -Y face (plane y=0) lands on the
    surface through `origin` with outward normal n; virtual +Z goes to u."""
    n = Vector(n).normalized()
    u = Vector(u).normalized()
    ey = -n
    ex = ey.cross(u)
    return Matrix.Translation(Vector(origin)) @ Matrix((ex, ey, u)).transposed().to_4x4()


def place(objs, origin, n, u):
    M = frame_matrix(origin, n, u)
    for o in objs:
        o.data.transform(M)
    return objs


def bezel(name, center, w, h, proud, radius, rim, recess, rim_role, inner_role, facing=-1):
    """K.bezel with its failure mode refused (rim >= radius folds the inset)."""
    assert radius - rim >= 0.005, "%s: bezel rim %.3f must be < radius %.3f - 5 mm" % (
        name, rim, radius)
    return K.bezel(name, center, w, h, proud, radius=radius, rim=rim, recess=recess,
                   rim_role=rim_role, inner_role=inner_role, facing=facing)


def clip_rect(poly, x0, x1, z0, z1):
    """Sutherland-Hodgman clip of a 2D polygon to an axis-aligned rectangle."""
    def clip(pts, inside, inter):
        out = []
        for i in range(len(pts)):
            cur, prev = pts[i], pts[i - 1]
            if inside(cur):
                if not inside(prev):
                    out.append(inter(prev, cur))
                out.append(cur)
            elif inside(prev):
                out.append(inter(prev, cur))
        return out

    def ix(xc):
        return lambda a, b: (xc, a[1] + (b[1] - a[1]) * (xc - a[0]) / (b[0] - a[0]))

    def iz(zc):
        return lambda a, b: (a[0] + (b[0] - a[0]) * (zc - a[1]) / (b[1] - a[1]), zc)

    pts = list(poly)
    for inside, inter in ((lambda p: p[0] >= x0, ix(x0)), (lambda p: p[0] <= x1, ix(x1)),
                          (lambda p: p[1] >= z0, iz(z0)), (lambda p: p[1] <= z1, iz(z1))):
        if not pts:
            break
        pts = clip(pts, inside, inter)
    return pts


def hazard_panel(prefix, w, h, nstripe, slant, back="slate", stripe="accent"):
    """Diagonal hazard band built on the virtual -Y face (plane y = 0): a backing
    plate 6 mm proud (3 mm sunk) and parallelogram stripes 5 mm proud of the
    plate. Returns the parts; carry them onto a surface with place()."""
    out = [K.box(prefix + "_plate", (-w / 2, -0.006, -h / 2), (w / 2, SINK, h / 2), back,
                 bevel=0.003, seg=1)]
    pitch = w / nstripe
    sw = pitch * 0.5
    m = 0.012                                  # stripes stop 12 mm inside the plate edge
    for k in range(-3, nstripe + 3):
        x0 = -w / 2 + k * pitch
        para = [(x0, -h / 2), (x0 + sw, -h / 2), (x0 + sw + slant, h / 2), (x0 + slant, h / 2)]
        poly = clip_rect(para, -w / 2 + m, w / 2 - m, -h / 2 + m, h / 2 - m)
        if len(poly) < 3:
            continue
        area = 0.5 * abs(sum(poly[i][0] * poly[i - 1][1] - poly[i - 1][0] * poly[i][1]
                             for i in range(len(poly))))
        if area < 4e-4:                        # drop slivers (< 2 x 2 cm)
            continue
        out.append(prism("%s_s%d" % (prefix, k), poly, -0.011, -0.004, stripe, axis="y"))
    return out


def text_block(name, body, centre, size, y_front, y_back, role, offset=0.0, spacing=1.0):
    """Raised lettering on a -Y-facing plane (rh_build_airfilter's helper)."""
    cu = bpy.data.curves.new(name + "_c", "FONT")
    cu.body = body
    cu.size = size
    cu.offset = offset
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


def louvre(prefix, w, h, nslat, rim_role="slate", inner_role="rubber", slat_role="metal",
           proud=0.03, recess=0.02):
    """Framed louvre vent on the virtual -Y face (plane y = 0)."""
    rim = 0.035
    out = []
    bz, wy = bezel(prefix + "_frame", (0.0, SINK, 0.0), w, h, proud + SINK, radius=0.045,
                   rim=rim, recess=recess, rim_role=rim_role, inner_role=inner_role)
    out.append(bz)
    ih = h - 2 * rim
    pitch = ih / nslat
    rot = Matrix.Rotation(math.radians(35.0), 4, "X")
    for k in range(nslat):
        zc = -ih / 2 + pitch * (k + 0.5)
        sl = K.box("%s_slat%d" % (prefix, k), (-(w / 2 - rim + 0.004), -0.018, -0.009),
                   (w / 2 - rim + 0.004, 0.018, 0.009), slat_role, bevel=0.002, seg=1)
        sl.data.transform(Matrix.Translation((0.0, wy - 0.008, zc)) @ rot)
        out.append(sl)
    return out


def window(prefix, w, h, rim=0.035, radius=0.05):
    """Dark glazed window in a slate frame on the virtual -Y face."""
    bz, _ = bezel(prefix, (0.0, SINK, 0.0), w, h, 0.028 + SINK, radius=radius, rim=rim,
                  recess=0.016, rim_role="slate", inner_role="glass")
    return [bz]


# ---------------------------------------------------------- 2D (y, z) maths
def V2(p):
    return Vector((p[0], p[1]))


def rnorm(d):
    """Right normal of a 2D (y, z) direction: the beam's TOP side here."""
    return Vector((d.y, -d.x))


def beam_outline(pts, hs, end_seg=6):
    """Side outline (y, z) of a plate beam along centreline pts with half
    heights hs: mitred at interior points, round ends. Returns (outline,
    top points, bottom points)."""
    P = [V2(p) for p in pts]
    n = len(P)
    D = [(P[i + 1] - P[i]).normalized() for i in range(n - 1)]
    top, bot = [], []
    for i in range(n):
        if i == 0:
            m, s = rnorm(D[0]), hs[0]
        elif i == n - 1:
            m, s = rnorm(D[-1]), hs[-1]
        else:
            m = (rnorm(D[i - 1]) + rnorm(D[i])).normalized()
            s = hs[i] / m.dot(rnorm(D[i]))
        top.append(P[i] + m * s)
        bot.append(P[i] - m * s)
    out = list(top)
    d, r, h = D[-1], rnorm(D[-1]), hs[-1]
    for k in range(1, end_seg):
        a = math.pi * k / end_seg
        out.append(P[-1] + r * h * math.cos(a) + d * h * math.sin(a))
    out += list(reversed(bot))
    d, r, h = D[0], rnorm(D[0]), hs[0]
    for k in range(1, end_seg):
        a = math.pi * k / end_seg
        out.append(P[0] - r * h * math.cos(a) - d * h * math.sin(a))
    return [(q.x, q.y) for q in out], top, bot


def beam(name, pts, hs, x0, x1, role, bevel=0.015):
    poly, top, bot = beam_outline(pts, hs)
    return prism(name, poly, x0, x1, role, axis="x", bevel=bevel, seg=2), top, bot


def pin(name, yz, xc, half, r, role="metal", sides=12):
    return K.cyl(name, (xc - half, yz[0], yz[1]), (xc + half, yz[0], yz[1]), r, role, sides=sides)


def hyd(prefix, p0, p1, rb, rr, frac=0.56, xc=0.0, eye_half=0.07):
    """Hydraulic cylinder p0 -> p1 (2D y, z at x = xc): gunmetal barrel with a
    light gland collar, light-alloy rod, pin eyes along X at both ends."""
    a = Vector((xc, p0[0], p0[1]))
    b = Vector((xc, p1[0], p1[1]))
    ax = (b - a).normalized()
    L = (b - a).length
    pm = a + ax * (L * frac)
    out = [K.cyl(prefix + "_barrel", a + ax * 0.04, pm, rb, "pipe", sides=14),
           K.cyl(prefix + "_gland", pm - ax * 0.05, pm + ax * 0.02, rb * 0.82, "metal", sides=14),
           K.cyl(prefix + "_rod", pm - ax * 0.10, b - ax * 0.03, rr, "metal", sides=10),
           K.cyl(prefix + "_eye0", a - Vector((eye_half, 0, 0)), a + Vector((eye_half, 0, 0)),
                 rb * 0.85, "pipe", sides=12),
           K.cyl(prefix + "_eye1", b - Vector((eye_half, 0, 0)), b + Vector((eye_half, 0, 0)),
                 rr * 1.5, "pipe", sides=12)]
    return out


# ------------------------------------------------------------ crawler track
def hull2d(points):
    pts = sorted(set((round(p[0], 6), round(p[1], 6)) for p in points))

    def cross(o, a, b):
        return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])

    lower, upper = [], []
    for p in pts:
        while len(lower) >= 2 and cross(lower[-2], lower[-1], p) <= 0:
            lower.pop()
        lower.append(p)
    for p in reversed(pts):
        while len(upper) >= 2 and cross(upper[-2], upper[-1], p) <= 0:
            upper.pop()
        upper.append(p)
    return lower[:-1] + upper[:-1]             # CCW in (y, z)


class Loop:
    """Closed CCW polyline in (y, z), sampled by arc length."""

    def __init__(self, pts):
        self.p = [V2(q) for q in pts]
        self.cum = [0.0]
        n = len(self.p)
        for i in range(n):
            self.cum.append(self.cum[-1] + (self.p[(i + 1) % n] - self.p[i]).length)
        self.L = self.cum[-1]

    def at(self, s):
        s %= self.L
        i = min(bisect.bisect_right(self.cum, s) - 1, len(self.p) - 1)
        a, b = self.p[i], self.p[(i + 1) % len(self.p)]
        seg = self.cum[i + 1] - self.cum[i]
        return a.lerp(b, (s - self.cum[i]) / seg if seg > 0 else 0.0)

    def normal(self, s, eps=0.03):
        t = (self.at(s + eps) - self.at(s - eps)).normalized()
        return Vector((t.y, -t.x))             # outward for a CCW loop


def track_belt(name, x0, x1, loop, pitch=0.22, t_in=0.04, t_out=0.035, pad=0.05):
    """The crawler belt as ONE closed ribbed solid: a band round the loop whose
    outer face steps up into a raised shoe every pitch (trapezoid section)."""
    N = max(8, round(loop.L / pitch))
    p = loop.L / N
    prof = ((0.0, 0), (0.34, 0), (0.44, 1), (0.90, 1))
    bm = bmesh.new()
    rows = []
    for i in range(N):
        for f, hi in prof:
            s = (i + f) * p
            P = loop.at(s)
            nr = loop.normal(s)
            o = P + nr * (t_out + pad * hi)
            q = P - nr * t_in
            rows.append((bm.verts.new((x0, o.x, o.y)), bm.verts.new((x1, o.x, o.y)),
                         bm.verts.new((x0, q.x, q.y)), bm.verts.new((x1, q.x, q.y))))
    M = len(rows)
    for k in range(M):
        a, b = rows[k], rows[(k + 1) % M]
        bm.faces.new((a[0], b[0], b[1], a[1]))
        bm.faces.new((a[2], a[3], b[3], b[2]))
        bm.faces.new((a[0], a[2], b[2], b[0]))
        bm.faces.new((a[1], b[1], b[3], a[3]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    rh_hs.log("track %s: loop %.2f m, %d shoes at %.3f m pitch" % (name, loop.L, N, p))
    return K._obj(name, bm, ["track"])


# ======================================================== UNDERCARRIAGE
# Belt path = convex hull of the idler (front, -Y), the sprocket (rear) and two
# end track rollers; the bottom run sits 8.5 cm up so the shoes touch z = 0.
IDL = (-1.85, 0.52, 0.38)
SPR = (1.85, 0.52, 0.38)
RLR = 0.12
dense = []
for (cy, cz, r) in (IDL, SPR, (-1.38, 0.085 + RLR, RLR), (1.38, 0.085 + RLR, RLR)):
    for k in range(96):
        a = 2 * math.pi * k / 96
        dense.append((cy + r * math.cos(a), cz + r * math.sin(a)))
LOOP = Loop(hull2d(dense))
BELT_IN, BELT_OUT = 0.90, 1.50                 # belt |x| range: 0.60 m shoes, 2.4 m gauge
FR_IN, FR_OUT = 1.00, 1.40                     # track frame |x| range (inside the belt)
TRACK_FRONT = IDL[0] - IDL[2] - 0.085           # shoe tips at the front
TRACK_LEN = 2 * abs(TRACK_FRONT)
FRAME_POLY = [(-1.86, 0.36), (-1.62, 0.21), (1.62, 0.21), (1.86, 0.36), (1.86, 0.70),
              (1.62, 0.785), (-1.62, 0.785), (-1.86, 0.70)]
for sx in (-1, 1):
    xa, xb = sorted((sx * BELT_IN, sx * BELT_OUT))
    low.append(track_belt("belt%d" % sx, xa, xb, LOOP))
    fa, fb = sorted((sx * FR_IN, sx * FR_OUT))
    low.append(prism("tframe%d" % sx, FRAME_POLY, fa, fb, "slate", bevel=0.02, seg=2))
    # raised guard plate on the outer frame face, bolted
    ga, gb = sorted((sx * (FR_OUT - 0.004), sx * (FR_OUT + 0.018)))
    low.append(prism("tguard%d" % sx, [(-1.30, 0.33), (1.30, 0.33), (1.30, 0.66), (-1.30, 0.66)],
                     ga, gb, "slate", bevel=0.008))
    for by in (-1.18, -0.40, 0.40, 1.18):
        for bz in (0.37, 0.62):
            low.append(bolt("tbolt", (sx * (FR_OUT + 0.018), by, bz), (sx, 0, 0), r=0.016))
    # idler (front) and sprocket (rear) wheels with alloy hubs
    ia, ib = sorted((sx * 0.98, sx * 1.44))
    low.append(K.cyl("idler%d" % sx, (ia, IDL[0], IDL[1]), (ib, IDL[0], IDL[1]), 0.30, "pipe", sides=20))
    low.append(K.cyl("sprocket%d" % sx, (ia, SPR[0], SPR[1]), (ib, SPR[0], SPR[1]), 0.31, "slate", sides=11))
    for (cy, cz), hr in (((IDL[0], IDL[1]), 0.13), ((SPR[0], SPR[1]), 0.16)):
        ha, hb = sorted((sx * 1.43, sx * 1.475))
        low.append(K.cyl("hub%d" % sx, (ha, cy, cz), (hb, cy, cz), hr, "metal", sides=12))
    # track rollers on the bottom run, carrier rollers on the top run
    ra, rb = sorted((sx * 1.03, sx * 1.37))
    for ry in (-1.20, -0.60, 0.0, 0.60, 1.20):
        low.append(K.cyl("roller%d" % sx, (ra, ry, 0.225), (rb, ry, 0.225), 0.10, "pipe", sides=12))
    for ry in (-0.70, 0.70):
        low.append(K.cyl("carrier%d" % sx, (ra, ry, 0.795), (rb, ry, 0.795), 0.065, "pipe", sides=10))
# car body between the frames + the slew ring under the house
low.append(convex_block("carbody", [((1, 0, 0), 1.02), ((-1, 0, 0), 1.02), ((0, 1, 0), 0.78),
                                    ((0, -1, 0), 0.78), ((0, 0, 1), 0.84), ((0, 0, -1), -0.32),
                                    ((0, S2, -S2), S2 * (0.78 - 0.32 - 0.16)),
                                    ((0, -S2, -S2), S2 * (0.78 - 0.32 - 0.16))],
                        "slate", bevel=0.02))
# (radius < the belts' inner edge at |x| 0.90: round 1's 0.98 m ring cut into the belts)
low.append(K.cyl("slew", (0, 0, 0.82), (0, 0, 1.075), 0.84, "pipe", sides=32))
low.append(K.cyl("slew_lip", (0, 0, 0.90), (0, 0, 0.96), 0.87, "slate", sides=32))

# ================================================================ HOUSE
# Plan outline of the house skirt (x, y), CCW: square front with small
# chamfers, rounded tail where the counterweight sits.
HX, HF, HB, HR = 1.42, -1.50, 2.36, 0.46       # half width, front y, back y, tail radius
outline = [(-HX + 0.08, HF), (HX - 0.08, HF), (HX, HF + 0.08)]
for k in range(0, 7):
    a = math.radians(90.0 * k / 6)
    outline.append((HX - HR + HR * math.cos(a), HB - HR + HR * math.sin(a)))
for k in range(0, 7):
    a = math.radians(90.0 + 90.0 * k / 6)
    outline.append((-HX + HR + HR * math.cos(a), HB - HR + HR * math.sin(a)))
outline.append((-HX, HF + 0.08))


def offset_outline(poly, d):
    """Offset a convex CCW (x, y) outline outward by d (mitred)."""
    n = len(poly)
    out = []
    for i in range(n):
        a, p, b = Vector(poly[i - 1]), Vector(poly[i]), Vector(poly[(i + 1) % n])
        n1 = Vector(((p - a).y, -(p - a).x)).normalized()
        n2 = Vector(((b - p).y, -(b - p).x)).normalized()
        m = (n1 + n2).normalized()
        out.append(tuple(p + m * (d / m.dot(n1))))
    return out


def band_z(name, poly, z0, z1, t_out, t_in, role):
    """A belt-line ring round a convex CCW outline: t_out proud, t_in sunk."""
    A = offset_outline(poly, t_out)
    B = offset_outline(poly, -t_in)
    bm = bmesh.new()
    rows = [(bm.verts.new((a[0], a[1], z0)), bm.verts.new((a[0], a[1], z1)),
             bm.verts.new((b[0], b[1], z0)), bm.verts.new((b[0], b[1], z1))) for a, b in zip(A, B)]
    M = len(rows)
    for k in range(M):
        a, b = rows[k], rows[(k + 1) % M]
        bm.faces.new((a[0], b[0], b[1], a[1]))
        bm.faces.new((a[2], a[3], b[3], b[2]))
        bm.faces.new((a[0], a[2], b[2], b[0]))
        bm.faces.new((a[1], b[1], b[3], a[3]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    return K._obj(name, bm, [role])


DECK_Z0, SKIRT_Z0, SKIRT_Z1 = 1.06, 1.25, 1.50
deck = prism("deck", offset_outline(outline, -0.035), DECK_Z0, SKIRT_Z0 + 0.01, "slate",
             axis="z", bevel=0.015)
_bm = bmesh.new()                              # its top is buried in the skirt: drop it
_bm.from_mesh(deck.data)
bmesh.ops.delete(_bm, geom=[f for f in _bm.faces if f.normal.z > 0.999 and
                            all(abs(v.co.z - (SKIRT_Z0 + 0.01)) < 1e-5 for v in f.verts)],
                 context="FACES_ONLY")
_bm.to_mesh(deck.data)
_bm.free()
up.append(deck)
up.append(prism("skirt", outline, SKIRT_Z0, SKIRT_Z1, "hull", axis="z", bevel=0.015))
# the amber belt line of the reference, wrapped round the whole house
up.append(band_z("beltline", outline, 1.36, 1.45, 0.006, 0.003, "accent"))

# ---------------------------------------------------------------- the cab
CX0, CX1, CY0, CY1, CZ0, CZ1 = 0.34, 1.40, -1.48, -0.20, SKIRT_Z1 - 0.01, 2.88
TILT = math.radians(8.0)                       # windscreen leans back above z = 1.80
NF = Vector((0, -math.cos(TILT), math.sin(TILT)))
DF = NF.dot(Vector((0, CY0, 1.80)))
UF = Vector((0, math.sin(TILT), math.cos(TILT)))    # "up" along the windscreen


def front_y(z):
    """y of the tilted cab front at height z."""
    return -(DF - math.sin(TILT) * z) / math.cos(TILT)


cab_planes = [((1, 0, 0), CX1), ((-1, 0, 0), -CX0), ((0, 1, 0), CY1), ((0, -1, 0), -CY0),
              ((0, 0, 1), CZ1), ((0, 0, -1), -CZ0), (tuple(NF), DF),
              ((S2, -S2, 0), S2 * (CX1 - CY0 - 0.07)), ((-S2, -S2, 0), S2 * (-CX0 - CY0 - 0.05))]
up.append(convex_block("cab", cab_planes, "hull", bevel=0.02, buried_bottom=True))
# windscreen on the tilted front, a lower front window under it
ws = window("windscreen", 0.86, 0.86, rim=0.04, radius=0.06)
up += place(ws, (0.87, front_y(2.30), 2.30), NF, UF)
cab_plan = [(CX0 + 0.05, CY0), (CX1 - 0.07, CY0), (CX1, CY0 + 0.07), (CX1, CY1 - 0.06),
            (CX0, CY1 - 0.06), (CX0, CY0 + 0.05)]
up.append(band_z("cabstripe", cab_plan, 1.70, 1.775, 0.006, 0.003, "accent"))
# +X side (outboard): door window + rear quarter window, door handle
up += place(window("door_win", 0.54, 0.78), (CX1, -0.98, 2.30), (1, 0, 0), (0, 0, 1))
up += place(window("quarter_win", 0.30, 0.78), (CX1, -0.47, 2.30), (1, 0, 0), (0, 0, 1))
up.append(K.box("door_handle", (CX1 - 0.004, -0.80, 1.86), (CX1 + 0.025, -0.68, 1.90), "metal",
                bevel=0.006, seg=1))
for k, gy in enumerate((-1.33, -0.27)):        # amber grab handles either side of the door
    up.append(K.pipe("grab%d" % k, [(CX1 - 0.01, gy, 1.84), (CX1 + 0.07, gy, 1.84),
                                    (CX1 + 0.07, gy, 2.62), (CX1 - 0.01, gy, 2.62)],
                     0.026, "accent", bend=0.05, sides=8, samples=3))
# -X side (boom side) window and the back window over the engine housing
up += place(window("boom_win", 0.86, 0.70), (CX0, -0.86, 2.30), (-1, 0, 0), (0, 0, 1))
up += place(window("back_win", 0.80, 0.40, rim=0.03, radius=0.045), (0.87, CY1, 2.58),
            (0, 1, 0), (0, 0, 1))
# roof: slate plate with a short visor, beacon at the rear, work lights at the front
RZ0, RZ1 = 2.86, 2.94
up.append(K.box("roof", (CX0 - 0.03, front_y(CZ1) - 0.09, RZ0), (CX1 + 0.03, CY1 + 0.04, RZ1),
                "slate", bevel=0.015))
for k, wx in enumerate((0.52, 1.22)):
    hy0 = front_y(CZ1) - 0.05
    up.append(K.box("wl_house%d" % k, (wx - 0.09, hy0, RZ1 - 0.01), (wx + 0.09, hy0 + 0.13, RZ1 + 0.11),
                    "accent", bevel=0.012, seg=1))
    up.append(K.box("wl_lens%d" % k, (wx - 0.068, hy0 - 0.006, RZ1 + 0.012),
                    (wx + 0.068, hy0 + 0.004, RZ1 + 0.088), "glow", bevel=0.003, seg=1))
BCN = (1.16, -0.46)
up.append(K.cyl("beacon_base", (BCN[0], BCN[1], RZ1 - 0.01), (BCN[0], BCN[1], RZ1 + 0.045), 0.12,
                "slate", sides=20))
up.append(K.cyl("beacon_ring", (BCN[0], BCN[1], RZ1 + 0.04), (BCN[0], BCN[1], RZ1 + 0.075), 0.108,
                "accent", sides=20))
up.append(lathe_z("beacon", [(0.0, RZ1 + 0.07), (0.088, RZ1 + 0.07), (0.088, RZ1 + 0.16),
                             (0.065, RZ1 + 0.205), (0.0, RZ1 + 0.22)], BCN, ["glow"], seg=20))

# ------------------------------------------------- side box (front right)
BX0, BX1, BY0, BY1, BZ1 = -1.40, -0.58, -1.48, -0.25, 1.98
up.append(convex_block("sidebox", [((1, 0, 0), BX1), ((-1, 0, 0), -BX0), ((0, 1, 0), BY1),
                                   ((0, -1, 0), -BY0), ((0, 0, 1), BZ1),
                                   ((0, 0, -1), -(SKIRT_Z1 - 0.01)),
                                   ((-S2, -S2, 0), S2 * (-BX0 - BY0 - 0.06))],
                       "hull", bevel=0.02, buried_bottom=True))
# MINE placard on its front (the reference's marking, facing the default camera)
PLX, PLZ, PLW, PLH = -0.99, 1.75, 0.66, 0.30
up.append(K.box("placard", (PLX - PLW / 2, BY0 - 0.014, PLZ - PLH / 2),
                (PLX + PLW / 2, BY0 + 0.004, PLZ + PLH / 2), "slate", bevel=0.008))
up.append(text_block("mine", "MINE", (PLX, 0.0, PLZ), 0.20, BY0 - 0.021, BY0 - 0.011, "accent",
                     offset=0.006, spacing=1.12))
for bx in (PLX - PLW / 2 + 0.025, PLX + PLW / 2 - 0.025):
    for bz in (PLZ - PLH / 2 + 0.025, PLZ + PLH / 2 - 0.025):
        up.append(bolt("plbolt", (bx, BY0 - 0.014, bz), (0, -1, 0), r=0.010))
# grab rail along its outboard top edge (r 2.8 cm)
up.append(K.pipe("rail_box", [(-1.32, -1.38, BZ1 - 0.02), (-1.32, -1.38, BZ1 + 0.30),
                              (-1.32, -0.42, BZ1 + 0.30), (-1.32, -0.42, BZ1 - 0.02)],
                 0.028, "accent", bend=0.08, sides=8, samples=3))

# --------------------------------------------------------- engine housing
EY0, EY1, EZ1 = -0.30, 1.56, 2.20
up.append(convex_block("engine", [((1, 0, 0), 1.40), ((-1, 0, 0), 1.40), ((0, 1, 0), EY1),
                                  ((0, -1, 0), -EY0), ((0, 0, 1), EZ1),
                                  ((0, 0, -1), -(SKIRT_Z1 - 0.01)),
                                  ((S2, 0, S2), S2 * (1.40 + EZ1 - 0.07)),
                                  ((-S2, 0, S2), S2 * (1.40 + EZ1 - 0.07))],
                       "hull", bevel=0.015, buried_bottom=True))
# raised bone-white hood with an exhaust grille, exhaust stack, air-cleaner can
HZ1 = EZ1 + 0.12
up.append(box_open("hood", (-1.12, 0.02, EZ1 - 0.02), (1.08, 1.44, HZ1), "hull", bevel=0.02))
grille = louvre("hood_grille", 1.30, 0.86, 7, rim_role="accent", inner_role="rubber",
                slat_role="metal", proud=0.025, recess=0.02)
up += place(grille, (-0.02, 0.76, HZ1), (0, 0, 1), (0, 1, 0))
up.append(K.cyl("exh_base", (-0.86, 0.28, HZ1 - 0.01), (-0.86, 0.28, HZ1 + 0.06), 0.10, "slate", sides=14))
up.append(K.pipe("exhaust", [(-0.86, 0.28, HZ1 + 0.03), (-0.86, 0.28, HZ1 + 0.52),
                             (-0.86, 0.14, HZ1 + 0.64)],
                 0.062, "pipe", bend=0.10, sides=12))
up.append(K.cyl("aircleaner", (0.86, 0.26, HZ1 - 0.01), (0.86, 0.26, HZ1 + 0.26), 0.15, "hull", sides=16))
up.append(K.cyl("ac_cap", (0.86, 0.26, HZ1 + 0.25), (0.86, 0.26, HZ1 + 0.31), 0.165, "slate", sides=16))
# side louvres (engine doors) on both flanks
up += place(louvre("lv_r", 1.00, 0.50, 6), (1.40, 0.80, 1.83), (1, 0, 0), (0, 0, 1))
up += place(louvre("lv_l", 1.00, 0.50, 6), (-1.40, 0.80, 1.83), (-1, 0, 0), (0, 0, 1))
# grab rail along the engine top, outboard on the box side
up.append(K.pipe("rail_eng", [(-1.30, -0.10, EZ1 - 0.04), (-1.30, -0.10, EZ1 + 0.24),
                              (-1.30, 1.30, EZ1 + 0.24), (-1.30, 1.30, EZ1 - 0.04)],
                 0.028, "accent", bend=0.08, sides=8, samples=3))

# ------------------------------------------------------------ counterweight
cw_poly = [p for p in offset_outline(outline, 0.015) if p[1] > 1.40]
cw_poly = [(cw_poly[0][0], 1.48)] + cw_poly + [(cw_poly[-1][0], 1.48)]
CWZ0, CWZ1 = SKIRT_Z1 - 0.02, 2.14
up.append(prism("counterweight", cw_poly, CWZ0, CWZ1, "slate", axis="z", bevel=0.03, seg=2,
                buried_bottom=True))
# hazard chevrons and two amber rear lamps on the flat of the tail
CWY = HB + 0.015
chev = hazard_panel("chevrons", 1.30, 0.26, 7, 0.16)
up += place(chev, (0.0, CWY, 1.80), (0, 1, 0), (0, 0, 1))
for sx in (-1, 1):                             # hazard panels on both flanks of the tail
    up += place(hazard_panel("cw_flank%d" % sx, 0.36, 0.26, 3, 0.14),
                (sx * (HX + 0.015), 1.69, 1.80), (sx, 0, 0), (0, 0, 1))
for sx in (-1, 1):
    lx = sx * 0.84
    up.append(K.box("rl_house", (lx - 0.07, CWY - 0.004, 1.71), (lx + 0.07, CWY + 0.025, 1.89),
                    "slate", bevel=0.008, seg=1))
    up.append(K.box("rl_lens", (lx - 0.05, CWY + 0.020, 1.73), (lx + 0.05, CWY + 0.031, 1.87),
                    "glow", bevel=0.003, seg=1))

# ================================================================== ARM
XC = -0.12                                     # boom plane (operator's right of the cab)
BW, SW = 0.23, 0.17                            # boom / stick half widths
B0 = Vector((-1.05, 1.74))                     # boom foot pin (y, z)
A1, L1 = math.radians(57.0), 1.90              # boom lower leg: rises forward
A2, L2 = math.radians(-28.0), 1.00             # boom nose: dips forward
D1 = Vector((-math.cos(A1), math.sin(A1)))
D2 = Vector((-math.cos(A2), math.sin(A2)))
KN = B0 + D1 * L1                              # boom knee
B1 = KN + D2 * L2                              # boom tip pin
THETA = math.radians(5.0)                      # stick hangs 5 deg back of vertical
DS = Vector((math.sin(THETA), -math.cos(THETA)))   # down the stick
RS = rnorm(DS)                                 # stick "top" = its front face
BUCKET_DEPTH, BUCKET_FLOOR = 0.88, 0.07        # bucket floor sits 7 cm off the ground
LS = (B1.y - (BUCKET_FLOOR + BUCKET_DEPTH * math.cos(THETA))) / math.cos(THETA)
S1 = B1 + DS * LS                              # bucket pin
HEEL = B1 - DS * 0.55                          # stick heel (stick-cylinder pin)
rh_hs.log("arm: knee %s, boom tip %s, stick %.2f m, bucket pin %s" % (
    [round(c, 2) for c in KN], [round(c, 2) for c in B1], LS, [round(c, 2) for c in S1]))
arm = []
aadd = arm.append

# boom: one bent plate beam, deepest at the knee
boom, btop, bbot = beam("boom", [B0, KN, B1], [0.20, 0.30, 0.19], XC - BW, XC + BW, "hull")
aadd(boom)
# boom foot lugs on the skirt top, pin through
FOOT = [(B0.x - 0.30, SKIRT_Z1 - 0.01), (B0.x + 0.32, SKIRT_Z1 - 0.01),
        (B0.x + 0.22, B0.y + 0.06), (B0.x + 0.06, B0.y + 0.20), (B0.x - 0.12, B0.y + 0.20),
        (B0.x - 0.25, B0.y + 0.04)]
for sx in (-1, 1):
    la, lb = sorted((XC + sx * (BW + 0.012), XC + sx * (BW + 0.075)))
    aadd(prism("foot_lug%d" % sx, FOOT, la, lb, "slate", bevel=0.01))
aadd(pin("foot_pin", B0, XC, BW + 0.11, 0.075))
# boom cylinders: a pair, outboard of the boom, from a block on the skirt front
BCX = BW + 0.105                               # cylinder plane offset from XC
BC0 = Vector((-1.40, SKIRT_Z1 + 0.13))
t_bc = 1.00
hb = 0.20 + 0.10 * (t_bc / L1)
BC1 = B0 + D1 * t_bc - rnorm(D1) * (hb - 0.02)
aadd(K.box("bc_block", (XC - BCX + 0.115, -1.47, SKIRT_Z1 - 0.01),
           (XC + BCX - 0.115, -1.30, BC0.y + 0.07), "slate", bevel=0.015))
aadd(pin("bc_pin0", BC0, XC, BCX + 0.10, 0.055))
aadd(pin("bc_pin1", BC1, XC, BCX + 0.10, 0.055))
for sx in (-1, 1):
    arm.extend(hyd("boomcyl%d" % sx, BC0, BC1, 0.092, 0.055, frac=0.60, xc=XC + sx * BCX,
                   eye_half=0.06))
# stick: plate beam heel -> boom tip -> bucket pin, narrower than the boom
stick, stop, sbot = beam("stick", [HEEL, B1, S1], [0.17, 0.21, 0.15], XC - SW, XC + SW, "hull")
aadd(stick)
aadd(pin("tip_pin", B1, XC, BW + 0.05, 0.085))
aadd(pin("heel_pin", HEEL, XC, SW + 0.05, 0.065))
# stick cylinder on top of the boom: lug on the knee, rod to the heel
KT = btop[1]                                   # knee top (mitred)
MK = (rnorm(D1) + rnorm(D2)).normalized()
C0 = KT + MK * 0.20
lug = [tuple(KT - D1 * 0.34 - MK * 0.04), tuple(KT + D2 * 0.30 - MK * 0.04),
       tuple(C0 + D2 * 0.09), tuple(C0 + MK * 0.085), tuple(C0 - D2 * 0.09)]
aadd(prism("knee_lug", lug, XC - 0.085, XC + 0.085, "slate", bevel=0.01))
aadd(pin("knee_pin", C0, XC, 0.13, 0.05))
arm.extend(hyd("stickcyl", C0, HEEL, 0.098, 0.058, frac=0.58, xc=XC, eye_half=0.10))
# bucket cylinder along the stick's front face, from a lug below the heel
BK0 = B1 - DS * 0.10 + RS * 0.33
RE = S1 - DS * 0.45 + RS * 0.32                # rod end / linkage pin
LSP = S1 - DS * 0.40                           # stick link pin
LBP = S1 + RS * 0.30                           # bucket link pin
slug = [tuple(B1 - DS * 0.28 + RS * 0.16), tuple(B1 + DS * 0.16 + RS * 0.16),
        tuple(BK0 + DS * 0.08), tuple(BK0 + RS * 0.075), tuple(BK0 - DS * 0.08)]
aadd(prism("stick_lug", slug, XC - 0.07, XC + 0.07, "slate", bevel=0.01))
aadd(pin("bk_pin0", BK0, XC, 0.11, 0.045))
arm.extend(hyd("bucketcyl", BK0, RE, 0.085, 0.05, frac=0.58, xc=XC, eye_half=0.09))
# linkage: side links (rod end -> stick) outside the stick, dog-bones to the bucket
for sx in (-1, 1):
    la, lb = sorted((XC + sx * (SW + 0.012), XC + sx * (SW + 0.052)))
    aadd(prism("hlink%d" % sx, beam_outline([RE, LSP], [0.065, 0.065])[0], la, lb, "slate",
               bevel=0.006))
    la, lb = sorted((XC + sx * (SW + 0.065), XC + sx * (SW + 0.105)))
    aadd(prism("dogbone%d" % sx, beam_outline([RE, LBP], [0.07, 0.07])[0], la, lb, "slate",
               bevel=0.006))
aadd(pin("re_pin", RE, XC, SW + 0.12, 0.045))
aadd(pin("ls_pin", LSP, XC, SW + 0.06, 0.05))

# ---------------------------------------------------------------- bucket
# Authored in the stick frame (u down the stick, v toward its front) about the
# bucket pin: a U-scoop curled shut, mouth up, teeth at the back lip.
def L2W(u, v):
    p = S1 + DS * u + RS * v
    return (p.x, p.y)


QB = [(-0.06, 0.40), (0.25, 0.45), (0.56, 0.39), (0.79, 0.19), (BUCKET_DEPTH, -0.12),
      (0.86, -0.43), (0.72, -0.71), (0.48, -0.91), (0.20, -1.00)]
BKW = 0.50                                     # bucket half width (outer)
# RustBeam, as the reference's rust-brown bucket: in slate it merged with the
# dark tracks right behind it (round 2, yaw 145)
BUCKET_ROLE = "rust"


def offset_path(Q, t, centre):
    out = []
    for i, q in enumerate(Q):
        a = V2(Q[max(i - 1, 0)])
        b = V2(Q[min(i + 1, len(Q) - 1)])
        tg = (b - a).normalized()
        nr = Vector((tg.y, -tg.x))
        if nr.dot(V2(centre) - V2(q)) < 0:
            nr = -nr
        out.append(tuple(V2(q) + nr * t))
    return out


QO = offset_path(QB, 0.006, (0.35, -0.30))    # shell outer face 6 mm inside the plate rims
QI = offset_path(QB, 0.056, (0.35, -0.30))
shell = [L2W(*q) for q in QO] + [L2W(*q) for q in reversed(QI)]
aadd(prism("bkt_shell", shell, XC - BKW + 0.02, XC + BKW - 0.02, BUCKET_ROLE, bevel=0.008))
side_poly = [L2W(*q) for q in QB] + [L2W(0.02, -0.70), L2W(-0.10, -0.20), L2W(-0.15, 0.28)]
for sx in (-1, 1):
    pa, pb = sorted((XC + sx * (BKW - 0.045), XC + sx * BKW))
    aadd(prism("bkt_side%d" % sx, side_poly, pa, pb, BUCKET_ROLE, bevel=0.01))
aadd(pin("bkt_pin", S1, XC, BKW + 0.035, 0.08))
aadd(pin("bkt_lpin", LBP, XC, SW + 0.12, 0.05))
# wear strips across the outer floor of the shell (light alloy)
for k, (ua, va, ub, vb) in enumerate(((0.80, 0.16, 0.87, -0.06), (0.88, -0.24, 0.84, -0.47))):
    pa, pb = V2((ua, va)), V2((ub, vb))
    tg = (pb - pa).normalized()
    nr = Vector((tg.y, -tg.x))
    if nr.dot(V2((0.35, -0.30)) - pa) > 0:
        nr = -nr
    strip = [pa - nr * 0.01, pb - nr * 0.01, pb + nr * 0.022, pa + nr * 0.022]
    aadd(prism("wear%d" % k, [L2W(q.x, q.y) for q in strip], XC - BKW + 0.04, XC + BKW - 0.04,
               "metal", bevel=0.005))
# cutting edge + 5 teeth on the lip, pointing up out of the mouth
LIP = V2(QB[-1])
TD = (LIP - V2(QB[-2])).normalized()           # tooth axis (u, v)
TN = Vector((TD.y, -TD.x))                     # tooth thickness axis
edge = [LIP - TD * 0.10 - TN * 0.035, LIP + TD * 0.015 - TN * 0.035,
        LIP + TD * 0.015 + TN * 0.035, LIP - TD * 0.10 + TN * 0.035]
aadd(prism("bkt_edge", [L2W(q.x, q.y) for q in edge], XC - BKW + 0.01, XC + BKW - 0.01, "metal",
           bevel=0.006))
for k, tx in enumerate((-0.36, -0.18, 0.0, 0.18, 0.36)):
    pts = []
    for (s, hw, ht) in ((-0.07, 0.062, 0.045), (0.25, 0.034, 0.016)):
        c = LIP + TD * s
        for sw_ in (-1, 1):
            for st in (-1, 1):
                q = c + TN * (st * ht)
                y, z = L2W(q.x, q.y)
                pts.append((XC + tx + sw_ * hw, y, z))
    aadd(hull_solid("tooth%d" % k, pts, "metal"))

# ------------------------------------------------- hoses along the boom top
for sx in (-1, 1):
    hx = XC + sx * (BW - 0.035)
    off = 0.018                                # hose centre 1.8 cm over the top: it bites in 1 cm
    hp = [(hx, B0.x + 0.10, B0.y - 0.20)]
    for t, h in ((0.25, 0.22), (1.30, 0.27)):
        q = B0 + D1 * t + rnorm(D1) * (h + off)
        hp.append((hx, q.x, q.y))
    q = KT + MK * off
    hp.append((hx, q.x, q.y))
    q = KN + D2 * 0.80 + rnorm(D2) * (0.21 + off)
    hp.append((hx, q.x, q.y))
    q = B1 - DS * 0.20 - RS * (0.21 + off)     # down onto the stick's back
    hp.append((hx, q.x, q.y))
    q = B1 + DS * 0.35 - RS * (0.20 + off)
    hp.append((hx, q.x, q.y))
    aadd(K.pipe("hose%d" % sx, hp, 0.028, "rubber", bend=0.16, sides=8, samples=4))

# ----------------------------------------- hazard bands on boom and stick sides
def side_band(prefix, centre_yz, along, w, h, n, slant, half_x):
    """Hazard band on both flat sides of a plate beam (normal +-X)."""
    out = []
    for sx in (-1, 1):
        parts_ = hazard_panel("%s%d" % (prefix, sx), w, h, n, slant)
        place(parts_, (XC + sx * half_x, centre_yz.x, centre_yz.y), (sx, 0, 0),
              (0, along.x, along.y))
        out += parts_
    return out


arm += side_band("hz_boom", B0 + D1 * 1.12, D1, 0.36, 1.06, 5, 0.22, BW)
arm += side_band("hz_nose", KN + D2 * 0.50, -D2, 0.30, 0.52, 3, 0.18, BW)
arm += side_band("hz_stick", B1 + DS * 0.85, -DS, 0.27, 0.80, 4, 0.18, SW)

up += arm

# ================================================================== SLEW
if abs(SLEW) > 1e-6:
    R = Matrix.Rotation(math.radians(SLEW), 4, "Z")
    for o in up:
        o.data.transform(R)
parts = low + up


# ------------------------------------------------------------------ report
def extents(objs):
    vs = [o.matrix_world @ v.co for o in objs for v in o.data.vertices]
    return (Vector((min(v.x for v in vs), min(v.y for v in vs), min(v.z for v in vs))),
            Vector((max(v.x for v in vs), max(v.y for v in vs), max(v.z for v in vs))))


lo_a, hi_a = extents(arm)
rh_hs.log("reach: arm front y %.3f, track front y %.3f -> %.2f m past the tracks = %.1f %% "
          "of the %.2f m track" % (lo_a.y, TRACK_FRONT, TRACK_FRONT - lo_a.y,
                                    100 * (TRACK_FRONT - lo_a.y) / TRACK_LEN, TRACK_LEN))


def role_stats(objs):
    tot = {}
    for o in objs:
        me = o.data
        for p in me.polygons:
            r = me.materials[p.material_index]["rh_role"]
            tot[r] = tot.get(r, 0.0) + p.area
    s = sum(tot.values())
    big = sorted(((sum(p.area for p in o.data.polygons), o.name) for o in objs), reverse=True)[:8]
    rh_hs.log("largest parts (m2): %s" % ", ".join("%s %.1f" % (n, a) for a, n in big))
    rh_hs.log("role share (all faces, %.1f m2): %s" % (s, ", ".join(
        "%s %.1f%%" % (k, 100 * v / s) for k, v in sorted(tot.items(), key=lambda kv: -kv[1]))))


role_stats(parts)


def recentre(ob):
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


# ------------------------------------------------------------------ finish
# Parked at an angle: the arm stays on the -Y (camera) side, but the machine is
# turned PARK_YAW about Z so the default camera meets the boom's flank and the
# cab instead of the arm end-on (see the docstring).
if abs(PARK_YAW) > 1e-6:
    R = Matrix.Rotation(math.radians(PARK_YAW), 4, "Z")
    for o in parts:
        o.data.transform(R)
ob = K.finish(parts, NAME)
K.face_camera(ob)                              # arm (built at -Y) -> -X, the default view
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
