"""rh_furniture - the shared bench kit for the tier-room furniture props (2026-10-14).

  import rh_furniture as F
  K = F.make_kit()
  B = F.Bench(K, parts, L=2.30, D=0.80, TH=0.90)
  ... prop-specific parts on B's anchors ...
  F.finalize(K, parts, NAME, OUT, SIZE, PREVIEW_ONLY)

Used by rh_build_infirmary.py, rh_build_lab_full.py, rh_build_workbench_lg.py
and rh_build_workshop.py. The four "tier" room props (the furniture each room
gains at its higher tiers) shipped as image-to-3D meshes with melted thin parts,
broken tool shapes and smudged backs; the approved references
(Martians/gen/tiers-redo/<name>/raw/<name>_three_quarter.png) all draw ONE
bench, dressed four ways. This module is that bench, so the four stay one
family by construction:

  - a BoneWhite top in separate tiles (dark seams between them), every tile's
    edge carrying the thin yellow stripe (the family's accent trim), wrapped
    round all four sides because a room prop is seen from every side;
  - a backsplash along the back edge, raised hull panels with corner bolts on
    BOTH its faces, small lit cyan indicator switches; an angled cheek at the
    left end (the references' raised end piece);
  - a RustBeam apron under the top, bolted;
  - a pipe frame (round tube, as every reference draws it): legs with rubber
    feet, an upper rail ring tight under the apron and a lower rail ring under
    the shelf, a fitting collar on every leg at every rail and on every rail
    end, so the joints read as clamped pipe;
  - a BoneWhite lower shelf tray (lipped) and a slate end panel at the left end;
  - small cyan display units (hull housing, dark glass, lit screen) and lit
    indicator switches - the second half of the family trim.

Props are not buildings (scripts/blender/rh_hs.py's buildings face the camera):
the game drops one prop per room cell, scales it UNIFORMLY so its larger
horizontal extent is 2.5 m, turns each cell 0/90/180/270 deg and sees it from
above at ~54 deg. So: real-world metres with the long side near 2.5 m (scale
~1.0: bench top 0.90 m, human-scale cues true), no face_camera, every side
finished, bounds bottom at z = 0 and centred on XY. Built with the long axis
on X, the working front at -Y (the old meshes' orientation: they replace them
in place), the cheek end at -X.

Accent: the kit's accent slot is the yellow edge stripe (ACCENT_HEX); the lit
screens are the "screen" role, albedo SCREEN_HEX (#7FE3D8) so they read cyan in
daylight too; in game every lit element glows the asset's one EmissiveColor,
teal. Both are trim. Interior props get light dust (bake dust=0.12) and
interior wear (art bible 2.1: 0.15 interiors; 0.2 here, the approved references
are visibly worked).
"""
import math
import os
import sys

import bmesh
import bpy
from mathutils import Matrix, Vector

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rh_hs  # noqa: E402

ACCENT_HEX = "#E4C744"      # the references' edge stripe: HazYellow, a touch muted
SCREEN_HEX = "#7FE3D8"      # lit display albedo (addendum); in-game glow is TealGlow
SINK = 0.003                # every applied plate bites 3 mm into its support
DUST = 0.12                 # interior props: light dust (addendum)
WEAR = 0.20


def log(msg):
    rh_hs.log(msg)


def args(default_out):
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    out = argv[argv.index("--out") + 1] if "--out" in argv else default_out
    size = int(argv[argv.index("--size") + 1]) if "--size" in argv else 2048
    return out, size, "--preview-only" in argv


def add_role(K, name, spec):
    K.roles[name] = spec
    K.mats[name] = K._preview_material(name, spec)


def make_kit():
    K = rh_hs.Kit(accent_hex=ACCENT_HEX)
    add_role(K, "screen", dict(albedo=SCREEN_HEX, metal=0.0, rough=0.25, emit=1, dust=0.0))
    # the references' apron: an oxide red-brown, darker than RustBeam paint
    add_role(K, "apron", dict(albedo="#80473A", metal=0.15, rough=0.62, paint=1, dust=1.0))
    # preview emission at a strength that shows cyan, not white, in review frames
    b = K.mats["screen"].node_tree.nodes["Principled BSDF"]
    b.inputs["Emission Color"].default_value = (*rh_hs.hex_lin(SCREEN_HEX), 1.0)
    b.inputs["Emission Strength"].default_value = 1.5
    return K


# ------------------------------------------------------------------ primitives
def place(objs, M):
    """Apply a 4x4 matrix to freshly built parts (mesh data, world = identity)."""
    for o in objs:
        o.data.transform(M)
    return objs


def hull_solid(K, name, pts, role, bevel=0.0, seg=1):
    """Closed convex solid from points (convex hull, coplanar tris merged)."""
    bm = bmesh.new()
    vs = [bm.verts.new(Vector(p)) for p in pts]
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


def prism_x(K, name, x0, x1, yz, role, bevel=0.0, seg=1):
    """Convex YZ outline extruded from x0 to x1."""
    return hull_solid(K, name, [(x, y, z) for x in (x0, x1) for (y, z) in yz], role, bevel, seg)


def prism_y(K, name, y0, y1, xz, role, bevel=0.0, seg=1):
    """Convex XZ outline extruded from y0 to y1."""
    return hull_solid(K, name, [(x, y, z) for y in (y0, y1) for (x, z) in xz], role, bevel, seg)


def obox(K, name, p0, p1, w, d, role, side=(0, 0, 1), bevel=0.004, seg=1):
    """Box of width w (along `side`, orthogonalised) and depth d running p0 -> p1."""
    p0, p1 = Vector(p0), Vector(p1)
    ax = p1 - p0
    length = ax.length
    ax.normalize()
    sd = Vector(side)
    sd = (sd - ax * sd.dot(ax)).normalized()
    nr = ax.cross(sd)
    ob = K.box(name, (-w / 2, -d / 2, 0.0), (w / 2, d / 2, length), role, bevel=bevel, seg=seg)
    ob.data.transform(Matrix(((sd.x, nr.x, ax.x, p0.x), (sd.y, nr.y, ax.y, p0.y),
                              (sd.z, nr.z, ax.z, p0.z), (0, 0, 0, 1))))
    return ob


def lathe_z(K, name, prof, cx, cy, roles, seg=24, mis=None):
    """Revolve a CLOSED (r, z) profile about the vertical axis through (cx, cy).
    r = 0 points become poles. mis[j] = material index of profile edge j."""
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


def tub(K, name, lo, hi, rim, depth, role, inner_role=None, bevel=0.006):
    """Box whose top is a recessed tray: rim `rim` wide, cavity `depth` deep
    (shelf trays, parts bins, drawers' pulls). Cavity walls + floor get inner_role."""
    lo, hi = Vector(lo), Vector(hi)
    size, ctr = hi - lo, (lo + hi) / 2
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    for v in bm.verts:
        v.co = Vector((v.co.x * size.x, v.co.y * size.y, v.co.z * size.z)) + ctr
    if bevel > 0:
        bmesh.ops.bevel(bm, geom=list(bm.edges), offset=bevel, offset_type="OFFSET",
                        segments=1, profile=0.5, affect="EDGES", clamp_overlap=True)
    bm.faces.ensure_lookup_table()
    top = max((f for f in bm.faces if f.normal.z > 0.99), key=lambda f: f.calc_area())
    bmesh.ops.inset_individual(bm, faces=[top], thickness=rim, depth=0.0)
    ext = bmesh.ops.extrude_discrete_faces(bm, faces=[top])
    inner = ext["faces"][0]
    for v in inner.verts:
        v.co.z -= depth
    walls = {f for v in inner.verts for f in v.link_faces}
    roles = [role] if inner_role is None else [role, inner_role]
    for f in bm.faces:
        f.material_index = 1 if (inner_role and f in walls) else 0
    return K._obj(name, bm, roles)


def bolts(K, out, prefix, pts, normal, r=0.010, h=0.007):
    """Hex bolt heads, each sunk 1 mm into the face it sits on."""
    n = Vector(normal).normalized()
    for p in pts:
        out.append(K.bolt(prefix, Vector(p) - n * 0.001, n, r=r, h=h + 0.001))


def ribbon(K, name, pts, width, depth, role, M=None, miter_max=2.5):
    """A flat strip of constant `width` along a 2-D polyline pts [(u, v)] in the
    local uv plane, extruded `depth` along local -w (toward a -Y viewer when M
    maps (u, v, w) -> (x, z, -y)). One closed solid, so a zigzag trace has no
    overlapping segment boxes (coplanar faces). M: 4x4 local -> world."""
    P = [Vector((u, v)) for u, v in pts]
    n = len(P)
    left, right = [], []
    for i in range(n):
        if i == 0:
            d = (P[1] - P[0]).normalized()
            m, k = Vector((-d.y, d.x)), 1.0
        elif i == n - 1:
            d = (P[-1] - P[-2]).normalized()
            m, k = Vector((-d.y, d.x)), 1.0
        else:
            d1 = (P[i] - P[i - 1]).normalized()
            d2 = (P[i + 1] - P[i]).normalized()
            n1, n2 = Vector((-d1.y, d1.x)), Vector((-d2.y, d2.x))
            m = (n1 + n2).normalized()
            k = min(miter_max, 1.0 / max(1e-3, m.dot(n1)))
        left.append(P[i] + m * (width / 2) * k)
        right.append(P[i] - m * (width / 2) * k)
    outline = left + list(reversed(right))
    bm = bmesh.new()
    front = [bm.verts.new((p.x, p.y, 0.0)) for p in outline]
    back = [bm.verts.new((p.x, p.y, depth)) for p in outline]
    fa = bm.faces.new(front)
    bm.faces.new(list(reversed(back)))
    m_ = len(outline)
    for i in range(m_):
        j = (i + 1) % m_
        bm.faces.new((front[i], front[j], back[j], back[i]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    # concave caps -> triangles (ear clipping), so the export is exact
    caps = [f for f in bm.faces if len(f.verts) > 4]
    bmesh.ops.triangulate(bm, faces=caps)
    if M is not None:
        bmesh.ops.transform(bm, matrix=M, verts=bm.verts)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    return K._obj(name, bm, [role])


# local (u, v, w) -> world (x, z, -y): a uv drawing on a -Y-facing plane, w toward +Y
UV_ON_FRONT = Matrix(((1, 0, 0, 0), (0, 0, 1, 0), (0, 1, 0, 0), (0, 0, 0, 1)))


def grid(xs, zs):
    return [(x, z) for x in xs for z in zs]


# ---------------------------------------------------------------- the bench
class Bench:
    """The family bench. Anchors for prop scripts:
      z_top (TH)          bench surface
      y_front, y_back     top's front / back face planes (+-D/2)
      bs_y, bs_z          backsplash front face plane, backsplash top height
      bs_mid_y            backsplash centre line (y) - where screen units sit
      bs_panel_y          front plane of the raised panels on the backsplash
      shelves             [(x0, x1, floor_z)] tray floors (what sits on them)
      legs_x, ly, R       leg axes (x list, +-ly) and tube radius
      z_ab                apron underside
    """

    def __init__(self, K, parts, L=2.30, D=0.80, TH=0.90, top_t=0.06, apron_h=0.13,
                 BH=0.15, BT=0.12, R=0.028, legs_x=None, ly=None, shelf_z=0.32,
                 shelves=None, seams=None, cheek=True, end_panel=True, apron_gaps=(),
                 bs_panels=None, upper_side_rails=True, open_front=(), lite=False):
        """open_front: [(leg xa, leg xb)] bays with no front upper rail (a machine
        bay, e.g. the workshop's drill press); pair it with apron_gaps.
        lite: a triangle-lean frame for many-legged benches (10-sided tubes and
        leg collars, 8-sided rail-end collars); same silhouette at room zoom."""
        TS, LCS, RCS = (10, 10, 8) if lite else (12, 12, 12)
        self.K, self.parts = K, parts
        add = parts.append
        self.L, self.D, self.TH = L, D, TH
        x0, x1 = -L / 2, L / 2
        yf, yb = -D / 2, D / 2
        self.y_front, self.y_back = yf, yb
        self.z_top = TH
        ztb = TH - top_t                         # tile underside
        z_ab = ztb - apron_h                     # apron underside
        self.z_ab = z_ab
        self.R = R
        lx = L / 2 - 0.17
        self.legs_x = legs_x = list(legs_x) if legs_x else [-lx, lx]
        self.ly = ly = ly if ly is not None else D / 2 - 0.09
        if shelves is None:
            shelves = [(legs_x[0], legs_x[-1], shelf_z)]
        self.shelves = shelves
        CW = 0.07 if cheek else 0.0              # cheek thickness (x)
        self.cheek_w = CW

        # ---------------------------------------------------------- top
        # slate sub-slab: shows as dark seams between the tiles
        add(K.box("subslab", (x0 + 0.012, yf + 0.012, ztb + 0.006), (x1 - 0.012, yb - 0.012,
                                                                       TH - 0.016),
                  "slate", bevel=0.0))
        if seams is None:
            seams = [x0 + 0.30 * L, x0 + 0.65 * L]
        edges = [x0] + list(seams) + [x1]
        GAP = 0.004
        self.tiles = []
        for i in range(len(edges) - 1):
            a = edges[i] + (GAP if i > 0 else 0.0)
            b = edges[i + 1] - (GAP if i < len(edges) - 2 else 0.0)
            add(K.box("tile%d" % i, (a, yf, ztb), (b, yb, TH), "hull", bevel=0.012, seg=2))
            self.tiles.append((a, b))
            # yellow edge stripe on the tile's front and back faces (4 mm proud)
            for sy, yy in ((-1, yf), (1, yb)):
                ya, yb2 = sorted((yy - sy * 0.002, yy + sy * 0.004))
                add(K.box("stripe%d_%d" % (i, sy), (a + 0.03, ya, TH - top_t + 0.013),
                          (b - 0.03, yb2, TH - 0.015), "accent", bevel=0.002, seg=1))
        # stripe on both end faces
        for sx, xx in ((-1, x0), (1, x1)):
            xa, xb = sorted((xx - sx * 0.002, xx + sx * 0.004))
            add(K.box("stripe_end%d" % sx, (xa, yf + 0.03, TH - top_t + 0.013), (xb, yb - 0.03,
                                                                                TH - 0.015),
                      "accent", bevel=0.002, seg=1))

        # ---------------------------------------------------- backsplash
        self.bs_y = bs_y = yb - BT                  # front face plane
        self.bs_z = bs_z = TH + BH
        self.bs_mid_y = yb - BT / 2
        ch = 0.045                                  # front-top chamfer
        add(prism_x(K, "backsplash", x0 + CW - 0.01 if cheek else x0 + 0.008, x1 - 0.008,
                    [(bs_y, TH - 0.008), (yb - 0.008, TH - 0.008), (yb - 0.008, bs_z),
                     (bs_y + ch, bs_z), (bs_y, bs_z - ch)], "hull", bevel=0.008, seg=2))
        if cheek:
            # the references' raised end piece: high at the back, low at the front
            add(prism_x(K, "cheek", x0 - 0.004, x0 + CW,
                        [(yf - 0.004, TH - 0.008), (yb - 0.004, TH - 0.008), (yb - 0.004, bs_z),
                         (bs_y + 0.02, bs_z), (yf + 0.06, TH + 0.065), (yf - 0.004, TH + 0.05)],
                        "hull", bevel=0.010, seg=2))
        # raised panels on both faces of the backsplash, bolted at the corners
        if bs_panels is None:                       # two panels, 12 mm seam between
            xa, xb = x0 + CW + 0.03, x1 - 0.03
            xm = (xa + xb) / 2
            bs_panels = [(xa, xm - 0.006), (xm + 0.006, xb)]
        self.bs_panels = bs_panels
        PT = 0.012
        self.bs_panel_y = bs_y - PT
        pz0, pz1 = TH + 0.022, bs_z - ch - 0.010
        self.bs_panel_z = (pz0, pz1)
        for k, (a, b) in enumerate(bs_panels):
            add(K.box("bsp%d" % k, (a, bs_y - PT, pz0), (b, bs_y + SINK, pz1), "hull",
                      bevel=0.005, seg=1))
            bolts(K, parts, "bspb", [(x, bs_y - PT, z) for x, z in
                                     grid((a + 0.022, b - 0.022), (pz0 + 0.02, pz1 - 0.02))],
                  (0, -1, 0), r=0.008)
        # back face: full-length raised panels (the back is seen as often as the front)
        yb_face = yb - 0.008
        bz0, bz1 = TH + 0.02, bs_z - 0.022
        nb = max(2, int(round((x1 - x0) / 0.8)))
        bx_edges = [x0 + 0.04 + (x1 - x0 - 0.08) * k / nb for k in range(nb + 1)]
        for k in range(nb):
            a, b = bx_edges[k] + 0.006, bx_edges[k + 1] - 0.006
            add(K.box("bbp%d" % k, (a, yb_face - SINK, bz0), (b, yb_face + PT, bz1), "hull",
                      bevel=0.005, seg=1))
            bolts(K, parts, "bbpb", [(x, yb_face + PT, z) for x, z in
                                     grid((a + 0.022, b - 0.022), (bz0 + 0.02, bz1 - 0.02))],
                  (0, 1, 0), r=0.008)

        # ------------------------------------------------------- apron
        segs = [(x0 + 0.05, x1 - 0.05)]
        for g0, g1 in apron_gaps:
            new = []
            for a, b in segs:
                if g1 <= a or g0 >= b:
                    new.append((a, b))
                    continue
                if g0 > a:
                    new.append((a, g0))
                if g1 < b:
                    new.append((g1, b))
            segs = new
        self.apron_segs = segs
        ay0, ay1 = yf + 0.035, yb - 0.035
        for k, (a, b) in enumerate(segs):
            add(K.box("apron%d" % k, (a, ay0, z_ab), (b, ay1, ztb + 0.006), "apron",
                      bevel=0.006, seg=1))
            n = max(2, int(round((b - a) / 0.45)) + 1)
            xs = [a + 0.035 + (b - a - 0.07) * i / (n - 1) for i in range(n)]
            for sy, yy in ((-1, ay0), (1, ay1)):
                bolts(K, parts, "apb", [(x, yy, z_ab + apron_h / 2) for x in xs], (0, sy, 0),
                      r=0.009)
        for sx, xx in ((-1, segs[0][0]), (1, segs[-1][1])):
            bolts(K, parts, "apbe", [(xx, y, z_ab + apron_h / 2) for y in (ay0 + 0.06, ay1 - 0.06)],
                  (sx, 0, 0), r=0.009)

        # ------------------------------------------------- pipe frame
        z_ur = z_ab - R + 0.004                     # upper rail axis: tight under the apron
        self.z_ur = z_ur
        CR, CH = R + 0.008, 0.085                   # leg collar radius, height
        RC, RCL = R + 0.006, 0.035                  # rail-end collar radius, length
        leg_top = z_ab + 0.006
        rail_levels = {}                            # leg x -> list of rail z
        for x in legs_x:
            rail_levels.setdefault(x, []).append(z_ur)
        lower = []
        for (sa, sb, sz) in shelves:
            zl = sz - 0.016 - R + 0.004             # lower rail axis: tight under the tray
            lower.append((sa, sb, zl))
            for x in legs_x:
                if sa - 1e-6 <= x <= sb + 1e-6:
                    rail_levels.setdefault(x, []).append(zl)
        for x in legs_x:
            for sy in (-1, 1):
                y = sy * ly
                add(K.cyl("leg", (x, y, 0.025), (x, y, leg_top), R, "slate", sides=TS))
                add(K.cyl("foot", (x, y, 0.0), (x, y, 0.045), R + 0.006, "rubber", sides=TS))
                for z in rail_levels[x]:
                    add(K.cyl("lcollar", (x, y, z - CH / 2), (x, y, z + CH / 2), CR, "pipe",
                              sides=LCS))

        inner = set(legs_x[1:-1])

        def rail(p0, p1):
            p0, p1 = Vector(p0), Vector(p1)
            d = (p1 - p0).normalized()
            # at a middle leg two collinear rails meet: stop each half a radius
            # short of the leg axis so their buried end caps never coincide
            def at_inner(p):                        # mathutils is float32: compare with a tolerance
                return abs(d.x) > 0.99 and any(abs(p.x - xi) < 1e-4 for xi in inner)
            a = p0 + d * (R * 0.5) if at_inner(p0) else p0
            b = p1 - d * (R * 0.5) if at_inner(p1) else p1
            add(K.cyl("rail", a, b, R, "slate", sides=TS))
            for p, s in ((p0, 1), (p1, -1)):
                c0 = p + d * s * (CR - 0.004)
                add(K.cyl("rcollar", c0, c0 + d * s * RCL, RC, "pipe", sides=RCS))

        # upper ring: front/back rails between neighbouring legs, side rails
        def is_open(a, b):
            return any(abs(a - oa) < 1e-6 and abs(b - ob) < 1e-6 for oa, ob in open_front)

        for i in range(len(legs_x) - 1):
            for sy in (-1, 1):
                if sy < 0 and is_open(legs_x[i], legs_x[i + 1]):
                    continue
                rail((legs_x[i], sy * ly, z_ur), (legs_x[i + 1], sy * ly, z_ur))
        for x in (legs_x if upper_side_rails else (legs_x[0], legs_x[-1])):
            rail((x, -ly, z_ur), (x, ly, z_ur))
        # lower rings
        for (sa, sb, zl) in lower:
            xs = [x for x in legs_x if sa - 1e-6 <= x <= sb + 1e-6]
            for i in range(len(xs) - 1):
                for sy in (-1, 1):
                    rail((xs[i], sy * ly, zl), (xs[i + 1], sy * ly, zl))
            for x in (xs[0], xs[-1]):
                rail((x, -ly, zl), (x, ly, zl))

        # ------------------------------------------------- shelf trays
        for k, (sa, sb, sz) in enumerate(shelves):
            add(tub(K, "shelf%d" % k, (sa - 0.004, -ly - 0.004, sz - 0.016),
                    (sb + 0.004, ly + 0.004, sz + 0.022), 0.022, 0.022, "hull", bevel=0.006))

        # ------------------------------------------------- end panel (left)
        if end_panel:
            ex = legs_x[0]
            ez0, ez1 = shelves[0][2] - 0.11, z_ur - R - 0.012
            add(K.box("endpanel", (ex - 0.011, -ly + R - 0.004, ez0), (ex + 0.011, ly - R + 0.004,
                                                                         ez1),
                      "slate", bevel=0.004, seg=1))
            pz0, pz1 = shelves[0][2] + 0.035, ez1 - 0.035   # plate clear of the lower rail
            for sx in (-1, 1):                      # apron-coloured inset plate on both faces
                xx = ex + sx * 0.011
                xa, xb = sorted((xx - sx * SINK, xx + sx * 0.010))
                add(K.box("endplate%d" % sx, (xa, -ly + 0.09, pz0),
                          (xb, ly - 0.09, pz1), "apron", bevel=0.004, seg=1))
                bolts(K, parts, "epb", [(xx + sx * 0.010, y, z) for y, z in
                                        grid((-ly + 0.11, ly - 0.11), (pz0 + 0.02, pz1 - 0.02))],
                      (sx, 0, 0), r=0.008)

    # ----------------------------------------------------------- dressing
    def switch(self, x, z=None, w=0.045, h=0.075):
        """Lit indicator switch on a backsplash panel: slate plate, lit window."""
        K, add = self.K, self.parts.append
        y = self.bs_panel_y
        z = z if z is not None else (self.bs_panel_z[0] + self.bs_panel_z[1]) / 2
        add(K.box("sw_plate", (x - w / 2, y - 0.012, z - h / 2), (x + w / 2, y + SINK, z + h / 2),
                  "slate", bevel=0.004, seg=1))
        add(K.box("sw_lit", (x - w / 2 + 0.010, y - 0.016, z - h / 2 + 0.024),
                  (x + w / 2 - 0.010, y - 0.010, z + h / 2 - 0.010), "screen", bevel=0.002,
                  seg=1))
        add(K.box("sw_btn", (x - w / 2 + 0.012, y - 0.020, z - h / 2 + 0.008),
                  (x + w / 2 - 0.012, y - 0.010, z - h / 2 + 0.019), "metal", bevel=0.002,
                  seg=1))

    def keypad(self, x, z=None, w=0.12, h=0.085):
        """Slate keypad plate with a lit readout and a 2x3 grid of keys."""
        K, add = self.K, self.parts.append
        y = self.bs_panel_y
        z = z if z is not None else (self.bs_panel_z[0] + self.bs_panel_z[1]) / 2
        add(K.box("kp_plate", (x - w / 2, y - 0.012, z - h / 2), (x + w / 2, y + SINK, z + h / 2),
                  "slate", bevel=0.004, seg=1))
        add(K.box("kp_lit", (x - w / 2 + 0.010, y - 0.016, z - h / 2 + 0.012),
                  (x - w / 2 + 0.040, y - 0.010, z + h / 2 - 0.010), "screen", bevel=0.0))
        for i in range(2):
            for j in range(3):
                kx = x - w / 2 + 0.058 + i * 0.030
                kz = z - h / 2 + 0.018 + j * 0.024
                add(K.box("kp_key", (kx - 0.010, y - 0.019, kz - 0.008),
                          (kx + 0.010, y - 0.010, kz + 0.008), "metal", bevel=0.0))

    def screen_unit(self, x, w=0.30, h=0.13, tilt=16.0, y=None, base_z=None, stand=0.035,
                    lite=False):
        """Small cyan display on the backsplash top: hull wedge housing, dark glass,
        lit screen, slate stand. Built at the origin facing -Y, tilted back, placed.
        lite: the small inset parts (glass, lit, LEDs, back plate) unbevelled."""
        K = self.K
        bv = 0.0 if lite else 0.003
        bl = 0.0 if lite else 0.002
        objs = []
        o = objs.append
        z0 = stand
        o(K.box("su_stand", (-0.05, 0.0, -0.012), (0.05, 0.05, z0 + 0.01), "slate",
                bevel=0.0 if lite else 0.006, seg=1))
        o(prism_x(K, "su_house", -w / 2, w / 2,
                  [(-0.012, z0), (0.075, z0), (0.075, z0 + h * 0.80), (0.035, z0 + h),
                   (-0.012, z0 + h)], "hull", bevel=0.010, seg=1 if lite else 2))
        gl0, gl1 = z0 + 0.016, z0 + h - 0.016
        o(K.box("su_glass", (-w / 2 + 0.018, -0.016, gl0), (w / 2 - 0.018, -0.012 + 0.004, gl1),
                "glass", bevel=bv, seg=1))
        o(K.box("su_lit", (-w / 2 + 0.030, -0.020, gl0 + 0.012), (w / 2 - 0.075, -0.014,
                                                                    gl1 - 0.012),
                "screen", bevel=bl, seg=1))
        for k in range(3):                                   # status column
            zz = gl0 + 0.016 + k * (gl1 - gl0 - 0.032) / 2
            o(K.box("su_led", (w / 2 - 0.062, -0.020, zz - 0.008), (w / 2 - 0.032, -0.014,
                                                                     zz + 0.008),
                    "screen", bevel=bl, seg=1))
        # back: slate service plate + bolts (seen from behind the bench)
        o(K.box("su_back", (-w / 2 + 0.03, 0.075 - SINK, z0 + 0.02), (w / 2 - 0.03, 0.084,
                                                                       z0 + h * 0.70),
                "slate", bevel=bv, seg=1))
        rot = Matrix.Rotation(math.radians(-tilt), 4, "X")
        yy = self.bs_mid_y - 0.025 if y is None else y
        zz = self.bs_z if base_z is None else base_z
        place(objs, Matrix.Translation((x, yy, zz)) @ rot)
        self.parts.extend(objs)
        return objs


# ---------------------------------------------------------------- props kit
def wrench(K, name, top, length, role="metal", facing=-1, head=0.034, t=0.018, bevel=0.003,
           ring_sides=12):
    """Chunky combination spanner hanging box-end-up from `top` (x, y, z): flat in
    XZ, t thick in Y. Box-end ring head at the top, handle, open jaw at the bottom
    (U opening downward, a gap of 0.6 x head). Proportions read as a spanner at
    room zoom: handle 0.5 x head wide, ring r 0.72 x head, jaw 1.5 x head wide."""
    x, y, z = top
    out = []
    hw = head * 0.50
    rr = head * 0.72
    zr = z - rr                                       # ring centre
    zj = z - length + head * 0.9                      # jaw bridge centre
    # the parts overlap at the joints, so each gets its own thickness: no two
    # coplanar faces (the handle is the thinnest, the ring and jaw the thickest)
    th, tp = t * 0.55, t * 0.78                       # >= 2 mm steps between the three
    out.append(K.box(name + "_h", (x - hw / 2, y - th / 2, zj), (x + hw / 2, y + th / 2, zr),
                     role, bevel=bevel, seg=1))
    out.append(K.cyl(name + "_ring", (x, y - t / 2, zr), (x, y + t / 2, zr), rr, role,
                     sides=ring_sides))
    jw = head * 1.5
    out.append(K.box(name + "_jb", (x - jw / 2, y - t / 2, zj - 0.006), (x + jw / 2, y + t / 2,
                                                                          zj + head * 0.45),
                     role, bevel=bevel, seg=1))
    for s in (-1, 1):
        xa, xb = sorted((s * (jw / 2 - 0.003), s * (jw / 2 - head * 0.45)))
        out.append(K.box(name + "_jp", (x + xa, y - tp / 2, zj - head * 0.9), (x + xb, y + tp / 2,
                                                                                 zj),
                         role, bevel=bevel, seg=1))
    return out


def hammer(K, name, top, length, facing=-1):
    """Hammer hanging head-up from a hook: rust (wood-red) handle, steel head."""
    x, y, z = top
    out = [K.box(name + "_head", (x - 0.06, y - 0.018, z - 0.036), (x + 0.06, y + 0.018, z),
                 "metal", bevel=0.005, seg=1),
           K.box(name + "_handle", (x - 0.013, y - 0.011, z - length), (x + 0.013, y + 0.011,
                                                                         z - 0.03),
                 "rust", bevel=0.004, seg=1),
           K.box(name + "_claw", (x + 0.055, y - 0.014, z - 0.03), (x + 0.085, y + 0.014,
                                                                     z - 0.012),
                 "metal", bevel=0.004, seg=1)]
    return out


def screwdriver(K, name, top, length):
    x, y, z = top
    hl = length * 0.42
    return [K.cyl(name + "_hd", (x, y, z), (x, y, z - hl), 0.017, "rust", sides=8),
            K.cyl(name + "_sh", (x, y, z - hl + 0.005), (x, y, z - length), 0.008, "metal",
                  sides=6)]


def parts_bin(K, name, lo, hi, role="slate", fill_role="metal"):
    """Open-front parts bin (a stepped tub) with a heap of parts inside."""
    lo, hi = Vector(lo), Vector(hi)
    out = [tub(K, name, lo, hi, 0.012, (hi.z - lo.z) * 0.55, role, inner_role="rubber",
               bevel=0.004)]
    # sloped scoop front, lower than the back (the bins' read)
    # parts heap: a low hull of small blocks rising above the floor
    cx, cy = (lo.x + hi.x) / 2, (lo.y + hi.y) / 2
    fz = hi.z - (hi.z - lo.z) * 0.55
    sx, sy = (hi.x - lo.x) / 2 - 0.02, (hi.y - lo.y) / 2 - 0.02
    out.append(hull_solid(K, name + "_heap",
                          [(cx - sx, cy - sy, fz - 0.004), (cx + sx, cy - sy, fz - 0.004),
                           (cx - sx, cy + sy, fz - 0.004), (cx + sx, cy + sy, fz - 0.004),
                           (cx - sx * 0.5, cy, hi.z - 0.006), (cx + sx * 0.4, cy + sy * 0.2,
                                                                hi.z - 0.012)],
                          fill_role, bevel=0.003))
    return out


def drawer_front(K, out, name, x0, x1, z0, z1, y, label=True, facing=-1):
    """Raised drawer front on a face at plane y (outward facing*Y), with a
    recessed slate pull and a metal label plate."""
    f = facing
    ya, yb = sorted((y - f * SINK, y + f * 0.018))
    out.append(K.box(name, (x0, ya, z0), (x1, yb, z1), "hull", bevel=0.006, seg=1))
    yf = y + f * 0.018
    cx = (x0 + x1) / 2
    pw = min(0.12, (x1 - x0) * 0.45)
    zt = z1 - 0.022
    pa, pb = sorted((yf - f * SINK, yf + f * 0.016))
    out.append(K.box(name + "_pull", (cx - pw / 2, pa, zt - 0.022), (cx + pw / 2, pb, zt),
                     "slate", bevel=0.004, seg=1))
    if label and (z1 - z0) > 0.09:
        la, lb = sorted((yf - f * SINK, yf + f * 0.004))
        lw = min(0.10, (x1 - x0) * 0.4)
        zc = (z0 + zt - 0.03) / 2
        out.append(K.box(name + "_lbl", (cx - lw / 2, la, zc - 0.016), (cx + lw / 2, lb,
                                                                        zc + 0.016),
                         "metal", bevel=0.002, seg=1))


# ---------------------------------------------------------------- finishing
def recentre(ob):
    """Bounds centred on XY, bottom at z=0 (the visualizer's contract)."""
    vs = [v.co for v in ob.data.vertices]
    lo = Vector((min(v.x for v in vs), min(v.y for v in vs), min(v.z for v in vs)))
    hi = Vector((max(v.x for v in vs), max(v.y for v in vs), max(v.z for v in vs)))
    ob.data.transform(Matrix.Translation(Vector((-(lo.x + hi.x) / 2, -(lo.y + hi.y) / 2,
                                                 -lo.z))))
    ob.data.update()
    d = hi - lo
    log("recentred by %s; dims %s m; larger horizontal extent %.3f m (game scale %.3f)" % (
        [round(-(lo.x + hi.x) / 2, 3), round(-(lo.y + hi.y) / 2, 3), round(-lo.z, 3)],
        [round(c, 3) for c in d], max(d.x, d.y), 2.5 / max(d.x, d.y)))
    return ob


def role_stats(objs):
    tot = {}
    for o in objs:
        me = o.data
        for p in me.polygons:
            r = me.materials[p.material_index]["rh_role"]
            tot[r] = tot.get(r, 0.0) + p.area
    s = sum(tot.values())
    log("role share (all faces): %s" % ", ".join(
        "%s %.1f%%" % (k, 100 * v / s) for k, v in sorted(tot.items(), key=lambda kv: -kv[1])))


def finalize(K, parts, name, out, size, preview_only, island_margin=0.002, pack_margin=0.003):
    """Margins: the kit's defaults; a prop made of hundreds of small parts (the
    workshop) passes tighter ones, or margins round its tiny islands eat the atlas."""
    role_stats(parts)
    ob = K.finish(parts, name, island_margin=island_margin, pack_margin=pack_margin)
    recentre(ob)
    os.makedirs(out, exist_ok=True)
    bpy.ops.object.select_all(action="DESELECT")
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.export_scene.gltf(filepath=os.path.join(out, name + "_preview.glb"),
                              export_format="GLB", use_selection=True, export_apply=True)
    log("preview glb written")
    if not preview_only:
        files = K.bake_and_export(ob, name, out, size=size, dust=DUST, wear=WEAR)
        log("files: %s" % files)
    return ob
