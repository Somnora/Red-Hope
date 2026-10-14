"""rh_agri - shared builders for the code-built agri / utility room props (2026-10-14).

Used by rh_build_planter_wet.py, rh_build_tank.py and rh_build_crop_tall.py, on
top of the hard-surface kit (rh_hs.py). The 2026-10-13 triage verdicts on these
props were "back wall breaking apart" (planter_wet), "needs a little polish"
(tank) and "the crops look weird" (crop_tall_2/3: flat paper cards). The fix
for all of them is the kit's: exact primitives, and for the plants, real
closed solids - M_RH_Master is ONE-SIDED, so a single-plane leaf (what the
image-to-3D cards were) vanishes from behind and reads as paper from the side.

What lives here (each asset script keeps its own layout):
  cli / add_role / add_screen_role      argument parsing and runtime roles
  convex_block / oct_planes / bezel     closed hard-surface solids (the
  sweep_ring / revolve / prism          AirFilter recipes, plus a profile swept
                                        round a chamfered-rectangle outline and
                                        a surface of revolution about any axis)
  Soup + stalk / blade / ear / leaf     hundreds of plant parts accumulated in
                                        ONE bmesh (hundreds of bpy objects make
                                        finish()'s join crawl); every part is a
                                        closed solid wound outward BY
                                        CONSTRUCTION (no normal recalculation,
                                        which guesses wrong on 3 mm thin shells)
  crop_box                              the stage-1 crop_tall planter box,
                                        measured from the shipping stage-1 GLB
                                        and rebuilt so stages 2/3 swap in place
  recentre / role_shares / export       pivot contract, accent check, preview
                                        + bake/export

Stage-1 box, measured 2026-10-14 by ray-casting
Martians/gen/crops-redo/final/crop_tall_1.glb (the imported source; identical
in frame and size to Saved/RHLineup/glb/Agri__crop_tall_1.glb, so the UE round
trip keeps the frame): plan 1.194 x 1.993 m (long axis Y), outer wall to
z 0.330, a 45-deg rim chamfer up to a flat rim top at z 0.380 (0.045-0.080 in
from the wall), an inner chamfer down to the soil at z 0.344, 0.105 in from the
wall; vertical corner chamfers with 0.10 m legs; four corner feet 0.035 tall
(the middle is an open arch: |y| < 0.50 on the long sides, |x| < 0.26 on the
ends) with a 2.2 cm plinth inset; a 2 cm recessed band low on each long side
(|y| < 0.50, z 0.04-0.165) and a recessed notch low on each end (|x| < 0.26,
z 0.04-0.10); one cyan screen high on EACH long face at the -Y end
(y -0.77..-0.57); two drip lines along Y at x ~ +-0.18. The game fits the
larger horizontal extent to 2.5 m (RHColonyVisualizerSubsystem: S = 250 cm /
max extent), so the rebuild is that box x 1.2545 = 1.498 x 2.500 m and every
plant stays inside the box footprint (anything poking out would grow the
bounds and shrink the box relative to stage 1).
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

SCREEN_HEX = "#7FE3D8"          # family display cyan (props addendum); in-game glow is teal
TEAL_GLOW_HEX = "#29FFDB"       # art bible 2.2 TealGlow (preview emission only)


# ------------------------------------------------------------------- setup
def cli(default_out):
    """--out DIR --size N --preview-only, exactly like rh_build_airfilter.py,
    plus the raw argv tail for asset-specific flags."""
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    out = argv[argv.index("--out") + 1] if "--out" in argv else default_out
    size = int(argv[argv.index("--size") + 1]) if "--size" in argv else 2048
    return argv, out, size, "--preview-only" in argv


def add_role(K, name, albedo, metal=0.0, rough=0.5, dust=1.0, paint=False, emit=False):
    spec = dict(albedo=albedo, metal=metal, rough=rough, dust=dust)
    if paint:
        spec["paint"] = 1
    if emit:
        spec["emit"] = 1
    K.roles[name] = spec
    K.mats[name] = K._preview_material(name, spec)
    return name


def add_screen_role(K, name="screen"):
    """Lit display/lamp role: albedo = the family's display cyan so it still
    reads as a screen in daylight; the emissive mask comes from emit=1. Preview
    emission is TealGlow at a strength that does not clip to white."""
    add_role(K, name, SCREEN_HEX, rough=0.25, dust=0.0, emit=True)
    b = K.mats[name].node_tree.nodes["Principled BSDF"]
    b.inputs["Emission Color"].default_value = (*rh_hs.hex_lin(TEAL_GLOW_HEX), 1.0)
    b.inputs["Emission Strength"].default_value = 1.6
    return name


# ------------------------------------------------------- hard-surface solids
def convex_block(K, name, planes, role, bevel=0.012, seg=2):
    """Closed convex solid = intersection of half-spaces n.p <= d, bevelled
    (the AirFilter recipe: one watertight hull however many chamfers meet)."""
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


S2 = math.sqrt(0.5)


def oct_planes(x0, x1, y0, y1, z0, z1, ch=0.0, ch_x0y0=None, ch_x1y0=None, ch_x0y1=None,
               ch_x1y1=None):
    """Half-spaces of an axis-aligned box x0..x1, y0..y1, z0..z1 whose vertical
    edges are chamfered by `ch` (legs, metres); per-corner overrides allowed."""
    pl = [((1, 0, 0), x1), ((-1, 0, 0), -x0), ((0, 1, 0), y1), ((0, -1, 0), -y0),
          ((0, 0, 1), z1), ((0, 0, -1), -z0)]
    for (sx, sy, c) in ((1, 1, ch_x1y1), (-1, 1, ch_x0y1), (1, -1, ch_x1y0), (-1, -1, ch_x0y0)):
        c = ch if c is None else c
        if c > 0:
            xe = x1 if sx > 0 else -x0
            ye = y1 if sy > 0 else -y0
            pl.append(((sx * S2, sy * S2, 0), S2 * (xe + ye - c)))
    return pl


def bezel(K, name, center, w, h, proud, radius, rim, recess, rim_role, inner_role, facing=-1):
    """K.bezel with its failure modes refused (rim >= radius - 5 mm folds the
    inset over itself or collapses the window corners - AirFilter round 1)."""
    assert radius - rim >= 0.005, "%s: bezel rim %.3f must be < radius %.3f - 5 mm" % (
        name, rim, radius)
    return K.bezel(name, center, w, h, proud, radius=radius, rim=rim, recess=recess,
                   rim_role=rim_role, inner_role=inner_role, facing=facing)


def place(objs, M):
    """Apply a 4x4 to freshly built parts' mesh data (parts built on a virtual
    -Y face at the origin, then carried onto their real face)."""
    for o in objs:
        o.data.transform(M)
    return objs


def offset_poly(poly, d):
    """Inward (d > 0) offset of a convex CCW polygon [(x, y)] by d, mitred."""
    n = len(poly)
    lines = []
    for i in range(n):
        a, b = Vector(poly[i]), Vector(poly[(i + 1) % n])
        e = (b - a).normalized()
        nrm = Vector((-e.y, e.x))                 # left normal = inward for CCW
        lines.append((nrm, nrm.dot(a) + d))
    out = []
    for i in range(n):
        (n0, c0), (n1, c1) = lines[i - 1], lines[i]
        det = n0.x * n1.y - n0.y * n1.x
        out.append(Vector(((c0 * n1.y - c1 * n0.y) / det, (n0.x * c1 - n1.x * c0) / det)))
    return out


def oct_outline(hx, hy, ch):
    """CCW chamfered-rectangle outline centred on the origin."""
    return [(hx, -hy + ch), (hx, hy - ch), (hx - ch, hy), (-hx + ch, hy), (-hx, hy - ch),
            (-hx, -hy + ch), (-hx + ch, -hy), (hx - ch, -hy)]


def sweep_ring(K, name, outline, prof, roles, seg_roles=None):
    """Closed profile [(inset d, z)] swept round a convex CCW outline: each
    profile point is the outline offset inward by d at height z. A rim cap /
    coaming ring in one watertight shell. seg_roles[j] = material index of
    profile edge j -> j+1."""
    bm = bmesh.new()
    rings = []
    for d, z in prof:
        rings.append([bm.verts.new((p.x, p.y, z)) for p in offset_poly(outline, d)])
    n, m = len(outline), len(prof)
    for j in range(m):
        k = (j + 1) % m
        for i in range(n):
            i2 = (i + 1) % n
            f = bm.faces.new((rings[j][i], rings[j][i2], rings[k][i2], rings[k][i]))
            f.material_index = seg_roles[j] if seg_roles else 0
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    return K._obj(name, bm, roles)


def sweep_open(K, name, path, prof, role):
    """Closed profile [(inset d, z)] swept along an OPEN polyline that follows
    a convex CCW outline (inset measured to the polyline's left), mitred at the
    interior corners, capped at both ends. For trim that wraps one corner of a
    rim (the crop box's red corner caps)."""
    path = [Vector(p) for p in path]
    n = len(path)
    nrm = []
    for i in range(n - 1):
        e = (path[i + 1] - path[i]).normalized()
        nrm.append(Vector((-e.y, e.x)))
    bm = bmesh.new()
    rings = []
    for i in range(n):
        if i == 0 or i == n - 1:
            nn = nrm[0] if i == 0 else nrm[-1]
            ring = [bm.verts.new((path[i].x + nn.x * d, path[i].y + nn.y * d, z)) for d, z in prof]
        else:
            n0, n1 = nrm[i - 1], nrm[i]
            m = (n0 + n1).normalized()
            k = 1.0 / max(m.dot(n0), 0.2)            # mitre length factor
            ring = [bm.verts.new((path[i].x + m.x * d * k, path[i].y + m.y * d * k, z)) for d, z in prof]
        rings.append(ring)
    m_ = len(prof)
    for i in range(n - 1):
        a, b = rings[i], rings[i + 1]
        for j in range(m_):
            j2 = (j + 1) % m_
            bm.faces.new((a[j], a[j2], b[j2], b[j]))
    bm.faces.new(rings[0])
    bm.faces.new(list(reversed(rings[-1])))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    return K._obj(name, bm, [role])


def revolve(K, name, prof, roles, sides=32, origin=(0, 0, 0), axis=(0, 0, 1), closed=True,
            seg_roles=None, phase=0.0, ref=None):
    """Surface of revolution of a profile [(r, t)] about `axis` through `origin`
    (t = distance along the axis). r = 0 points become poles. closed=True joins
    the last profile point back to the first (rings, bands); pole-to-pole
    profiles (tank shells, domes) pass closed=False. Normals are recalculated
    (every revolve here is one closed shell)."""
    ax = Vector(axis).normalized()
    ref = Vector(ref) if ref else (Vector((0, 0, 1)) if abs(ax.z) < 0.9 else Vector((1, 0, 0)))
    u = (ref - ax * ref.dot(ax)).normalized()
    w = ax.cross(u)
    o = Vector(origin)
    bm = bmesh.new()
    rings = []
    for r, t in prof:
        if r < 1e-7:
            v = bm.verts.new(o + ax * t)
            rings.append([v] * sides)
        else:
            rings.append([bm.verts.new(o + ax * t + (u * math.cos(a) + w * math.sin(a)) * r)
                          for a in (phase + 2 * math.pi * s / sides for s in range(sides))])
    m = len(prof)
    edges = [(j, j + 1) for j in range(m - 1)] + ([(m - 1, 0)] if closed else [])
    for k, (i, j) in enumerate(edges):
        for s in range(sides):
            s1 = (s + 1) % sides
            vs = []
            for v in (rings[i][s], rings[i][s1], rings[j][s1], rings[j][s]):
                if v not in vs:
                    vs.append(v)
            if len(vs) < 3:
                continue
            f = bm.faces.new(vs)
            f.material_index = seg_roles[k] if seg_roles else 0
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    return K._obj(name, bm, roles)


def prism(K, name, poly, a0, a1, role, axis="X"):
    """Extrude a simple polygon (2D, in the plane normal to `axis`) from a0 to
    a1 along that axis. axis X: poly is [(y, z)]; axis Y: [(x, z)]."""
    bm = bmesh.new()

    def P(p, a):
        return (a, p[0], p[1]) if axis == "X" else (p[0], a, p[1])
    lo = [bm.verts.new(P(p, a0)) for p in poly]
    hi = [bm.verts.new(P(p, a1)) for p in poly]
    bm.faces.new(lo)
    bm.faces.new(list(reversed(hi)))
    n = len(poly)
    for i in range(n):
        j = (i + 1) % n
        bm.faces.new((lo[i], lo[j], hi[j], hi[i]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    return K._obj(name, bm, [role])


def torus(K, name, centre, axis, R, r, role, major=32, minor=8):
    prof = [(R + r * math.cos(2 * math.pi * k / minor), r * math.sin(2 * math.pi * k / minor))
            for k in range(minor)]
    return revolve(K, name, prof, [role], sides=major, origin=centre, axis=axis)


def handwheel(K, name, centre, axis, R, role_rim, role_hub, spokes=3, rim_r=0.013):
    """Valve handwheel: rim torus, `spokes` spokes, hub; rim tube >= 2.4 cm thick."""
    ax = Vector(axis).normalized()
    ref = Vector((0, 0, 1)) if abs(ax.z) < 0.9 else Vector((1, 0, 0))
    u = (ref - ax * ref.dot(ax)).normalized()
    w = ax.cross(u)
    c = Vector(centre)
    out = [torus(K, name + "_rim", c, ax, R, rim_r, role_rim, major=28, minor=6)]
    for k in range(spokes):
        a = 2 * math.pi * k / spokes + math.pi / 2
        d = u * math.cos(a) + w * math.sin(a)
        out.append(K.cyl(name + "_spoke%d" % k, c + d * 0.02, c + d * (R + 0.002), 0.010,
                         role_rim, sides=6))
    out.append(K.cyl(name + "_hub", c - ax * 0.02, c + ax * 0.03, 0.026, role_hub, sides=10))
    return out


# ------------------------------------------------------------------ plants
class Soup:
    """Many small closed solids in ONE bmesh, one material slot per role.
    Every primitive below winds its faces outward by construction: rings are
    counter-clockwise about the path tangent t (frame u, w = t x u, so
    u x w = t), side quads run (a_i, a_i+1, b_i+1, b_i) from ring a to the
    next ring b, the start cap is the reversed first ring, the end cap the last
    ring, and a single-point ring becomes a pole fan."""

    def __init__(self, roles):
        self.bm = bmesh.new()
        self.roles = list(roles)
        self.idx = {r: i for i, r in enumerate(self.roles)}
        self.parts = 0

    def _f(self, vs, role):
        f = self.bm.faces.new(vs)
        f.material_index = self.idx[role]
        return f

    def tube(self, rings, seg_roles, cap_role=None):
        """rings: list of lists of Vectors (a 1-point list = pole). seg_roles[j]
        is the role of the band between ring j and j+1."""
        bm = self.bm
        R = [[bm.verts.new(p) for p in ring] for ring in rings]
        for j in range(len(R) - 1):
            a, b, role = R[j], R[j + 1], seg_roles[j]
            if len(a) == 1 and len(b) == 1:
                continue
            if len(b) == 1:
                n = len(a)
                for i in range(n):
                    self._f((a[i], a[(i + 1) % n], b[0]), role)
            elif len(a) == 1:
                n = len(b)
                for i in range(n):
                    self._f((b[(i + 1) % n], b[i], a[0]), role)
            else:
                n = len(a)
                for i in range(n):
                    i2 = (i + 1) % n
                    self._f((a[i], a[i2], b[i2], b[i]), role)
        if len(R[0]) > 2:
            self._f(list(reversed(R[0])), cap_role or seg_roles[0])
        if len(R[-1]) > 2:
            self._f(R[-1], cap_role or seg_roles[-1])
        self.parts += 1

    def antiprism(self, rings, seg_roles):
        """Spindle of square rings, each rotated 45 deg from the last (the ring
        list must already carry that rotation), poles at both ends: the
        zig-zag facets read as a wheat ear's spikelets."""
        bm = self.bm
        R = [[bm.verts.new(p) for p in ring] for ring in rings]
        for j in range(len(R) - 1):
            a, b, role = R[j], R[j + 1], seg_roles[j]
            if len(b) == 1:
                for i in range(len(a)):
                    self._f((a[i], a[(i + 1) % len(a)], b[0]), role)
            elif len(a) == 1:
                for i in range(len(b)):
                    self._f((b[(i + 1) % len(b)], b[i], a[0]), role)
            else:
                n = len(a)
                for i in range(n):
                    i2 = (i + 1) % n
                    self._f((a[i], a[i2], b[i]), role)
                    self._f((a[i2], b[i2], b[i]), role)
        self.parts += 1

    def obj(self, K, name):
        return K._obj(name, self.bm, self.roles)


def bezier(p0, p1, p2, p3, n):
    p0, p1, p2, p3 = (Vector(p) for p in (p0, p1, p2, p3))
    out = []
    for k in range(n + 1):
        t = k / n
        s = 1 - t
        out.append(s ** 3 * p0 + 3 * s * s * t * p1 + 3 * s * t * t * p2 + t ** 3 * p3)
    return out


def tangents(pts):
    out = []
    for i in range(len(pts)):
        a = pts[max(i - 1, 0)]
        b = pts[min(i + 1, len(pts) - 1)]
        out.append((b - a).normalized())
    return out


def frame(t, hint):
    """(u, w) with u ~ hint, perpendicular to t, and u x w = t."""
    u = hint - t * hint.dot(t)
    if u.length < 1e-6:
        u = Vector((1, 0, 0)) - t * t.x
        if u.length < 1e-6:
            u = Vector((0, 1, 0)) - t * t.y
    u.normalize()
    return u, t.cross(u)


def stalk(soup, pts, r, roles, phase=0.0, r_top=None):
    """Triangular-section stem along pts (radius r tapering to r_top), closed
    at both ends. roles: one per segment."""
    r_top = r if r_top is None else r_top
    ts = tangents(pts)
    rings = []
    n = len(pts)
    for i, (p, t) in enumerate(zip(pts, ts)):
        u, w = frame(t, Vector((math.cos(phase), math.sin(phase), 0.0)))
        rr = r + (r_top - r) * i / (n - 1)
        rings.append([p + (u * math.cos(a) + w * math.sin(a)) * rr
                      for a in (2 * math.pi * k / 3 for k in range(3))])
    soup.tube(rings, roles)


def blade(soup, pts, half_widths, keel, roles):
    """Cereal leaf blade: triangular section (two shallow roof faces over a
    flat underside: right edge, keel, left edge) along pts, its upper side
    facing up; a half-width of 0 makes that station a point (the tip)."""
    ts = tangents(pts)
    rings = []
    for p, t, hw in zip(pts, ts, half_widths):
        if hw <= 1e-6:
            rings.append([p])
            continue
        across = Vector((-t.y, t.x, 0.0))
        if across.length < 1e-6:
            across = Vector((1, 0, 0))
        u, w = frame(t, across)
        if w.z < 0:                               # upper side up
            u, w = frame(t, -across)
        k = min(keel, hw * 0.6)
        rings.append([p + u * hw, p + w * k, p - u * hw])
    soup.tube(rings, roles)


def broad_leaf(soup, pts, half_widths, cup, thick, roles):
    """Broad leaf (chard / lettuce): a cupped chevron section - right edge,
    upper midrib, left edge, lower midrib - so the top is a shallow V channel
    and the underside a parallel V `thick` below it. Closed; tip = point."""
    ts = tangents(pts)
    rings = []
    for p, t, hw in zip(pts, ts, half_widths):
        if hw <= 1e-6:
            rings.append([p])
            continue
        across = Vector((-t.y, t.x, 0.0))
        if across.length < 1e-6:
            across = Vector((1, 0, 0))
        u, w = frame(t, across)
        if w.z < 0:
            u, w = frame(t, -across)
        c = -cup * hw
        rings.append([p + u * hw, p + w * c, p - u * hw, p + w * (c - thick)])
    soup.tube(rings, roles)


def ear(soup, base, axis, length, radius, role, rings_at=(0.22, 0.5, 0.78),
        prof=(0.85, 1.0, 0.7), twist=0.0):
    """Wheat ear: square rings alternately turned 45 deg (antiprism), poles at
    both ends - a closed spindle whose zig-zag facets read as spikelets."""
    ax = Vector(axis).normalized()
    u, w = frame(ax, Vector((math.cos(twist), math.sin(twist), 0.0)))
    b = Vector(base)
    rings = [[b]]
    for j, (s, k) in enumerate(zip(rings_at, prof)):
        c = b + ax * (length * s)
        ph = (math.pi / 4) * (j % 2)
        rings.append([c + (u * math.cos(ph + a) + w * math.sin(ph + a)) * radius * k
                      for a in (math.pi / 2 * q for q in range(4))])
    rings.append([b + ax * length])
    # antiprism() wants every band's second ring turned +45 deg from its first.
    # Ring q (0-based) sits at phase (q % 2) * 45 deg, so ring q is re-indexed
    # by q // 2 steps of 90 deg to keep that true all the way up.
    fixed = [rings[0]]
    for q, r in enumerate(rings[1:-1]):
        s = q // 2
        fixed.append(r[s:] + r[:s])
    fixed.append(rings[-1])
    soup.antiprism(fixed, [role] * (len(fixed) - 1))


# ------------------------------------------------------------- crop box
# Stage-1 measurements x 1.2545 (see module docstring). Long axis Y.
CB_S = 2.5 / 1.9928
CB_HX, CB_HY = 0.749, 1.250           # outer wall half extents (plan 1.498 x 2.500)
CB_CH = 0.125                         # vertical corner chamfer legs
CB_WALL = 0.414                       # outer wall top (rim chamfer starts)
CB_RIM = 0.477                        # flat rim top
CB_SOIL = 0.432                       # soil surface
CB_SOIL_IN = 0.130                    # soil edge: inset from the outer wall
CB_FOOT = 0.044                       # foot height; the middle is an open arch
CB_SKIN = 0.025                       # panel skin thickness = recess depth
CB_POST = 0.20                        # corner post extent along each face
CB_LOW_Y = 0.627                      # long-side lower recess: |y| < this
CB_LOW_Z = 0.207                      #   ... below this
CB_END_X = 0.326                      # end-face lower notch: |x| < this
CB_END_Z = 0.125                      #   ... below this
CB_SCREEN_Y = -0.84                   # screens: both long faces, -Y end
CB_SCREEN_Z = 0.358
CB_DRIP_X = 0.22                      # drip lines along Y
LIM_X, LIM_Y = CB_HX - 0.012, CB_HY - 0.012   # plants stay inside the footprint


def crop_box(K, parts, trim="accent", screen="screen", soil="soil"):
    """The stage-1 crop_tall planter box, rebuilt from primitives. White hull
    panels on a dark slate core (the seams and the low recesses read dark),
    corner posts with RED trim plates on their chamfers, red feet, a hull rim
    cap, a cyan screen high on each long face at the -Y end, soil, drip lines.
    Returns nothing; appends to parts."""
    add = parts.append
    hx, hy, ch = CB_HX, CB_HY, CB_CH
    sk = CB_SKIN
    # dark core (the recesses and every panel seam show it)
    core_ch = ch - sk * (2 - math.sqrt(2))
    add(convex_block(K, "cb_core", oct_planes(-hx + sk, hx - sk, -hy + sk, hy - sk, 0.040, 0.425,
                                              ch=core_ch), "slate", bevel=0.006, seg=1))
    # four corner posts: square blocks wrapping each chamfer, from just above
    # the inset plinth (stage 1's whole bottom band is inset) to the wall top
    pa = CB_POST
    for sx in (-1, 1):
        for sy in (-1, 1):
            x0, x1 = sorted((sx * (hx - pa), sx * hx))
            y0, y1 = sorted((sy * (hy - pa), sy * hy))
            kw = {("ch_x1y1" if sx > 0 and sy > 0 else "ch_x0y1" if sy > 0 else
                   "ch_x1y0" if sx > 0 else "ch_x0y0"): ch}
            add(convex_block(K, "cb_post", oct_planes(x0, x1, y0, y1, CB_FOOT - 0.002, CB_WALL, ch=0.0, **kw),
                             "hull", bevel=0.008, seg=1))
            # red trim plate on the post's chamfer face, 6 mm proud
            n = Vector((sx * S2, sy * S2, 0.0))
            cxy = Vector((sx * (hx - ch / 2), sy * (hy - ch / 2), 0.0))
            wv = Vector((-n.y, n.x, 0.0))
            plate_w = ch * math.sqrt(2) - 0.05
            R = Matrix((wv, -n, Vector((0.0, 0.0, 1.0)))).transposed()   # columns: along, -out, up
            pl = K.box("cb_trim", (-plate_w / 2, -0.006, CB_FOOT + 0.030), (plate_w / 2, 0.004,
                                                                           CB_WALL - 0.035),
                       trim, bevel=0.003, seg=1)
            pl.data.transform(Matrix.Translation(cxy) @ R.to_4x4())
            add(pl)
            # red foot under each corner block
            fx0, fx1 = sorted((sx * CB_END_X, sx * (hx - 0.028)))
            fy0, fy1 = sorted((sy * CB_LOW_Y, sy * (hy - 0.028)))
            fkw = {k: ch - 0.028 * (2 - math.sqrt(2)) for k in kw}
            add(convex_block(K, "cb_foot", oct_planes(fx0, fx1, fy0, fy1, 0.0, 0.048, ch=0.0, **fkw),
                             trim, bevel=0.006, seg=1))
    # long faces (x = +-hx): upper band, lower end panels (open recess between)
    gap = 0.004
    yb = hy - pa - gap
    for sx in (-1, 1):
        xa, xb = sorted((sx * (hx - sk - 0.005), sx * hx))
        add(K.box("cb_lband", (xa, -yb, CB_LOW_Z + gap), (xb, yb, CB_WALL), "hull", bevel=0.006, seg=1))
        for sy in (-1, 1):
            ya, yb2 = sorted((sy * (CB_LOW_Y + gap), sy * yb))
            inner = ya if sy > 0 else yb2
            add(K.box("cb_lpanel", (xa, ya, CB_FOOT), (xb, yb2, CB_LOW_Z), "hull", bevel=0.006, seg=1,
                      chamfers=[(lambda d, m, inner=inner: abs(d.x) > 0.99 and abs(m.y - inner) < 0.002
                                 and m.z > CB_LOW_Z - 0.002, 0.07)]))
    # end faces (y = +-hy): upper band, lower side panels, centre notch
    xb_ = hx - pa - gap
    for sy in (-1, 1):
        ya, yb2 = sorted((sy * (hy - sk - 0.005), sy * hy))
        add(K.box("cb_eband", (-xb_, ya, CB_END_Z + gap), (xb_, yb2, CB_WALL), "hull", bevel=0.006, seg=1))
        for sx in (-1, 1):
            xa, xb = sorted((sx * (CB_END_X + gap), sx * xb_))
            add(K.box("cb_epanel", (xa, ya, CB_FOOT), (xb, yb2, CB_END_Z), "hull", bevel=0.006, seg=1))
    # rim cap: a coaming ring whose outer chamfer starts AT the wall plane
    # (stage 1 has no lip: a proud lip would grow the plan bounds past 2.5 m and
    # shrink the whole box in game), flat top, inner chamfer down past the soil
    # surface (the soil edge hides under it). Its foot slants into the panels
    # so no face lies in the panel plane.
    prof = [(0.012, 0.402), (0.0, CB_WALL + 0.004), (0.050, CB_RIM), (0.100, CB_RIM),
            (0.140, 0.418), (0.140, 0.402)]
    add(sweep_ring(K, "cb_rim", oct_outline(hx, hy, ch), prof, ["hull"]))
    # red corner caps wrapping the rim at each corner (the reference's corner
    # brackets; stage 1's red corners): a 4 mm shell over the rim's outer
    # chamfer and flat top, 0.13 m along each face either side of the corner
    # chamfer. Seen from above, where the vertical corner plates are not. The
    # outer edge stops 1 mm inside the wall plane so the plan bounds stay 2.5 m.
    cap = [(0.001, CB_WALL + 0.0115), (0.0481, CB_RIM + 0.004), (0.100, CB_RIM + 0.004),
           (0.100, CB_RIM - 0.007), (0.052, CB_RIM - 0.007), (0.006, CB_WALL + 0.004)]
    ol = oct_outline(hx, hy, ch)
    LC = 0.13
    for k in range(4):                          # corner chamfer = outline edge 1, 3, 5, 7
        a, b = Vector(ol[2 * k + 1]), Vector(ol[(2 * k + 2) % 8])
        before = Vector(ol[2 * k])              # start of the side leading into the corner
        after = Vector(ol[(2 * k + 3) % 8])     # end of the side leaving it
        p0 = a + (before - a).normalized() * LC
        p3 = b + (after - b).normalized() * LC
        add(sweep_open(K, "cb_cornercap", [p0, a, b, p3], cap, trim))
    # soil: one slab under the rim's inner chamfer
    si = 0.112
    add(convex_block(K, "cb_soil", oct_planes(-hx + si, hx - si, -hy + si, hy - si, 0.395, CB_SOIL,
                                              ch=max(ch - si * (2 - math.sqrt(2)), 0.01)),
                     soil, bevel=0.0))
    # drip lines along Y, half sunk in the soil, ends buried in the rim
    for sx in (-1, 1):
        x = sx * CB_DRIP_X
        add(K.cyl("cb_drip", (x, -(hy - 0.105), CB_SOIL + 0.002), (x, hy - 0.105, CB_SOIL + 0.002),
                  0.012, "metal", sides=8))
    # cyan screens: slate bezel + lit cell, high on each long face at the -Y end
    for sx in (-1, 1):
        unit = []
        bz, wy = bezel(K, "cb_scr_bezel", (0.0, 0.003, 0.0), 0.27, 0.080, 0.015, radius=0.022,
                       rim=0.014, recess=0.006, rim_role="slate", inner_role="slate")
        unit.append(bz)
        unit.append(K.box("cb_scr", (-0.105, wy - 0.004, -0.024), (0.105, wy + 0.002, 0.024),
                          screen, bevel=0.002, seg=1))
        rot = Matrix.Rotation(math.radians(90.0 * sx), 4, "Z")
        place(unit, Matrix.Translation((sx * hx, CB_SCREEN_Y, CB_SCREEN_Z)) @ rot)
        parts.extend(unit)
    # bolts on the corner posts' long-face strips (2 per post), sunk 2 mm so
    # the bolt's inner cap is not coplanar with the post face. None on the end
    # faces: anything proud of y = +-hy grows the plan bounds (see rim cap).
    for sx in (-1, 1):
        for sy in (-1, 1):
            yb0 = sy * (hy - pa + 0.035)
            for z in (0.10, CB_WALL - 0.06):
                add(K.bolt("cb_bolt", (sx * (hx - 0.002), yb0, z), (sx, 0, 0), r=0.011, h=0.008))


# ------------------------------------------------------------------ finish
def recentre(ob):
    """Bounds centred on XY, bottom at z = 0 (the visualizer's contract)."""
    vs = [v.co for v in ob.data.vertices]
    lo = Vector((min(v.x for v in vs), min(v.y for v in vs), min(v.z for v in vs)))
    hi = Vector((max(v.x for v in vs), max(v.y for v in vs), max(v.z for v in vs)))
    ob.data.transform(Matrix.Translation(Vector((-(lo.x + hi.x) / 2, -(lo.y + hi.y) / 2, -lo.z))))
    ob.data.update()
    dims = hi - lo
    rh_hs.log("recentred by %s; dims %s m" % (
        [round(-(lo.x + hi.x) / 2, 4), round(-(lo.y + hi.y) / 2, 4), round(-lo.z, 4)],
        [round(d, 3) for d in dims]))
    return ob


def _uv_islands(bm, uv):
    """Faces grouped into UV islands (edge-adjacent with matching UVs)."""
    bm.faces.ensure_lookup_table()
    seen, islands = set(), []
    for f in bm.faces:
        if f.index in seen:
            continue
        stack, isl = [f], []
        seen.add(f.index)
        while stack:
            g = stack.pop()
            isl.append(g)
            for l in g.loops:
                for l2 in l.edge.link_loops:
                    h = l2.face
                    if h.index in seen or h is g:
                        continue
                    a0, a1 = l[uv].uv, l.link_loop_next[uv].uv
                    b0, b1 = l2[uv].uv, l2.link_loop_next[uv].uv
                    if ((a0 - b1).length < 1e-6 and (a1 - b0).length < 1e-6) or \
                            ((a0 - b0).length < 1e-6 and (a1 - b1).length < 1e-6):
                        seen.add(h.index)
                        stack.append(h)
        islands.append(isl)
    return islands


def _pack(ob):
    bpy.ops.object.select_all(action="DESELECT")
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.select_all(action="SELECT")
    bpy.ops.uv.pack_islands(rotate=True, margin=0.003)
    bpy.ops.object.mode_set(mode="OBJECT")


def _island_widths(bm, uv, islands, size):
    import numpy as np
    out = []
    for isl in islands:
        P = np.array([tuple(l[uv].uv) for f in isl for l in f.loops])
        Q = P - P.mean(0)
        _, V = np.linalg.eigh(Q.T @ Q)
        pr = Q @ V[:, 0]
        out.append((pr.max() - pr.min()) * size)
    return out


def _visible_faces(bm, n=150):
    """Indices of faces any orthographic ray reaches from 49 directions on the
    upper hemisphere (top-down plus 4 elevations x 12 azimuths)."""
    import numpy as np
    from mathutils.bvhtree import BVHTree
    bm.faces.ensure_lookup_table()
    tree = BVHTree.FromBMesh(bm)
    vs = np.array([tuple(v.co) for v in bm.verts])
    lo, hi = vs.min(0), vs.max(0)
    cen = Vector(((lo + hi) / 2).tolist())
    rad = float(np.linalg.norm(hi - lo))
    seen = set()
    dirs = [Vector((0, 0, -1))]
    for el in (15.0, 35.0, 55.0, 75.0):
        for k in range(12):
            az = math.radians(30.0 * k + (15.0 if el in (35.0, 75.0) else 0.0))
            e = math.radians(el)
            dirs.append(Vector((-math.cos(az) * math.cos(e), -math.sin(az) * math.cos(e), -math.sin(e))))
    for d in dirs:
        u = d.cross(Vector((0, 0, 1)))
        u = u.normalized() if u.length > 1e-6 else Vector((1, 0, 0))
        w = u.cross(d).normalized()
        for i in range(n):
            for j in range(n):
                p = cen + u * ((i / (n - 1) - 0.5) * rad) + w * ((j / (n - 1) - 0.5) * rad) - d * rad * 2
                hit = tree.ray_cast(p, d)
                if hit[2] is not None:
                    seen.add(hit[2])
    return seen


def plant_uv(ob, plant_roles, size=2048, share=0.5, hidden_scale=0.25, margin=0.0015):
    """Re-unwrap after finish() so hundreds of thin plant solids bake cleanly.

    finish()'s smart-project cuts every plant solid into 3-16 islands (a
    triangular stalk is 3 islands, each one 1 cm face) and packs them at the
    box's texel density with a 6 px margin round EACH island: ~3,800 islands
    whose margins eat the map, stalk faces 2-3 texels wide, and slivers with
    no texel centre baked nothing - BLACK stalks (first crop bake, 2026-10-14:
    2.5 % of the stalk area). Here each plant solid becomes ONE island, its
    vertices projected onto the solid's own principal plane (length x width;
    the faces of a thin solid overlap in that plane, which is harmless for
    flat-coloured plant roles), scaled so the plants get `share` of the UV
    area; box islands no orbit ray reaches shrink by `hidden_scale`; all is
    packed with a tighter margin. Plant colour is per-role flat (+ the bake's
    noise and AO), so overlap costs nothing visible."""
    import numpy as np
    me = ob.data
    roles = [m["rh_role"] for m in me.materials]
    bm = bmesh.new()
    bm.from_mesh(me)
    bm.faces.ensure_lookup_table()
    bm.verts.ensure_lookup_table()
    uv = bm.loops.layers.uv.active
    vis = _visible_faces(bm)
    is_plant = [roles[f.material_index] in plant_roles for f in bm.faces]
    # plant solids = vertex-connected components of plant faces
    comp = {}
    ncomp = 0
    for f in bm.faces:
        if not is_plant[f.index] or f.index in comp:
            continue
        stack = [f]
        comp[f.index] = ncomp
        while stack:
            g = stack.pop()
            for v in g.verts:
                for h in v.link_faces:
                    if is_plant[h.index] and h.index not in comp:
                        comp[h.index] = ncomp
                        stack.append(h)
        ncomp += 1
    groups = [[] for _ in range(ncomp)]
    for fi, ci in comp.items():
        groups[ci].append(bm.faces[fi])
    # project each solid onto its principal plane (metres)
    plant_area = 0.0
    for faces in groups:
        verts = list({v for f in faces for v in f.verts})
        P = np.array([tuple(v.co) for v in verts])
        c = P.mean(0)
        Q = P - c
        _, V = np.linalg.eigh(Q.T @ Q)
        e1, e2 = V[:, 2], V[:, 1]
        uvmap = {v: (float(q @ e1), float(q @ e2)) for v, q in zip(verts, Q)}
        for f in faces:
            for l in f.loops:
                l[uv].uv = uvmap[l.vert]
        a1, a2 = Q @ e1, Q @ e2
        plant_area += (a1.max() - a1.min()) * (a2.max() - a2.min())
    # box: current UV area of visible / hidden faces, then shrink the hidden
    box_islands = [isl for isl in _uv_islands(bm, uv) if not is_plant[isl[0].index]]
    vis_uv = hid_uv = 0.0
    for isl in box_islands:
        loops = [l for f in isl for l in f.loops]
        area = sum(f.calc_area() for f in isl)
        uva = 0.0
        for f in isl:
            p = [l[uv].uv for l in f.loops]
            for k in range(1, len(p) - 1):
                uva += abs((p[k] - p[0]).cross(p[k + 1] - p[0])) / 2
        if any(f.index in vis for f in isl):
            vis_uv += uva
        else:
            hid_uv += uva
            c = sum((l[uv].uv for l in loops), Vector((0.0, 0.0))) / len(loops)
            for l in loops:
                l[uv].uv = c + (l[uv].uv - c) * hidden_scale
    box_uv = vis_uv + hid_uv * hidden_scale ** 2
    # scale the plant islands so they take `share` of the (pre-pack) UV area
    s_ = math.sqrt(share / (1 - share) * box_uv / max(plant_area, 1e-9))
    for faces in groups:
        for f in faces:
            for l in f.loops:
                l[uv].uv = l[uv].uv * s_
    gidx = [[f.index for f in faces] for faces in groups]
    n_box = len(box_islands)
    n_hid = sum(1 for isl in box_islands if not any(f.index in vis for f in isl))
    bm.to_mesh(me)
    bm.free()
    bpy.ops.object.select_all(action="DESELECT")
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.select_all(action="SELECT")
    bpy.ops.uv.pack_islands(rotate=True, margin=margin)
    bpy.ops.object.mode_set(mode="OBJECT")
    # report: plant island widths in texels (minor-axis extent)
    bm = bmesh.new()
    bm.from_mesh(me)
    uv = bm.loops.layers.uv.active
    bm.faces.ensure_lookup_table()
    widths = []
    for idx in gidx:
        P = np.array([tuple(l[uv].uv) for fi in idx for l in bm.faces[fi].loops])
        Q = P - P.mean(0)
        _, V = np.linalg.eigh(Q.T @ Q)
        pr = Q @ V[:, 0]
        widths.append((pr.max() - pr.min()) * size)
    bm.free()
    widths = np.array(widths)
    rh_hs.log("plant uv: %d plant solids -> 1 island each, %d box islands (%d hidden shrunk x%.2f); "
              "plant island width px min %.1f, 5th pct %.1f, median %.1f"
              % (ncomp, n_box, n_hid, hidden_scale, widths.min(), np.percentile(widths, 5), np.median(widths)))
    return ob


def role_shares(ob, groups=None):
    """Surface share per role on the finished object (all faces). groups:
    {label: set(roles)} for extra sub-totals (e.g. the box only)."""
    me = ob.data
    tot = {}
    for p in me.polygons:
        r = me.materials[p.material_index]["rh_role"]
        tot[r] = tot.get(r, 0.0) + p.area
    s = sum(tot.values())
    rh_hs.log("role share (all faces): %s" % ", ".join(
        "%s %.1f%%" % (k, 100 * v / s) for k, v in sorted(tot.items(), key=lambda kv: -kv[1])))
    for label, rs in (groups or {}).items():
        sub = sum(v for k, v in tot.items() if k in rs)
        if sub > 0:
            rh_hs.log("  %s: accent %.1f%% of %.2f m2" % (label, 100 * tot.get("accent", 0) / sub, sub))
    return tot


def export(K, ob, name, out, preview_only, size, wear=0.18, dust=0.12):
    os.makedirs(out, exist_ok=True)
    bpy.ops.object.select_all(action="DESELECT")
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.export_scene.gltf(filepath=os.path.join(out, name + "_preview.glb"), export_format="GLB",
                              use_selection=True, export_apply=True)
    rh_hs.log("preview glb written")
    if not preview_only:
        files = K.bake_and_export(ob, name, out, size=size, wear=wear, dust=dust)
        rh_hs.log("files: %s" % files)
