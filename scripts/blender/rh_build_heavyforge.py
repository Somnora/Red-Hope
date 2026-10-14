"""Build the HeavyForge (game DefName "Forge") from code - the hard-surface remake (2026-10-14).

  blender -b -P scripts/blender/rh_build_heavyforge.py -- [--out Martians/gen/hs/HeavyForge]
      [--size 2048] [--preview-only]

The Forge: "Smelts regolith and ore into structural materials - the stuff
nearly everything is built from." Replaces the image-to-3D HeavyForge the
director flagged "looks weird. Rebuild in Blender": an orange melted box whose
panels had slumped, a shredded stack on a sagging side deck, the whole hull
tinted in its TintFor orange (Saved/RHLineup/png/Models2__HeavyForge__v035 /
__v215). The DESIGN is kept: the approved identity-anchored sheet,
Martians/gen/objects-redo/HeavyForge/cutout/HeavyForge_*.png (five views) - a
massive chamfered-cube furnace: heavy armoured steel corner pieces wrapping the
front, red-brown painted side panels, a thick framed furnace door with a small
glowing window, a wall-mounted control panel with a small screen, hazard-stripe
bands, pipes running round the base and up one side, a welded heat-exchanger
box on the back, and TWO flanged, elbowed exhaust stacks on top. Only the
construction changes: every part is an exact primitive, so nothing can melt,
and the emissive mask is the furnace window (plus a thin glow line on the
control panel) by construction.

Layout facts:
  - built with the furnace door at -Y, then K.face_camera() turns it to -X, the
    face the game's default strategy camera meets (measured 2026-10-13, pitch
    ~54 deg). The control panel / riser-pipe side is +X before the turn (the
    sheet's side view), i.e. -Y after it: the yaw-145 lineup frame.
  - palette (art bible 1, heavy industry on dark slate): DarkSlate armour,
    core, plinth and heat exchanger - the heavy dark-metal read; RustBeam
    side, top and back panel plates (the sheet's red-brown paint) set proud of
    the slate core so every seam reads as a dark line; gunmetal stacks and
    base pipe rings; light alloy only on small hardware (flanges, clamps,
    handles, window frame, control box) so it pops on the dark body.
  - accent (#F9A050, TintFor Forge) is TRIM only: the lip under the door
    frame, the two top-side hatches (the sheet's orange hatch), heat bands on
    the stacks, small corner markers and the control-panel label. HazYellow is
    the hazard stripes on slate bands (front over the door, both sides).
  - the furnace window is the identity: a light alloy octagonal frame on a
    rust door leaf in a slate frame on an orange lip, its window recessed only
    3.5 cm so the 54 deg camera still sees almost all of the glow. The lit
    role ("glow", albedo #FF8A2A) matches the in-game FurnaceGlow
    EmissiveColor (~(6.0, 1.6, 0.15) linear) so it also reads hot in daylight.
  - stacks: hollow tubes (dark soot inside) that elbow 50 deg toward the back
    as on the sheet, with a bolted flange pair, so the silhouette from the
    default camera is a dark cube with two flanged chimneys and a glowing eye.
Real-world metres, ground-centred pivot (bounds centred on XY, bottom at z=0).
The visualizer fits the larger horizontal extent (the door-to-back depth here,
~3.8 m) to the 4-cell short side (8 m), ~2.1x.
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
OUT = argv[argv.index("--out") + 1] if "--out" in argv else "Martians/gen/hs/HeavyForge"
SIZE = int(argv[argv.index("--size") + 1]) if "--size" in argv else 2048
PREVIEW_ONLY = "--preview-only" in argv
NAME = "HeavyForge"

K = rh_hs.Kit(accent_hex="#F9A050")       # TintFor(Forge), art bible 2.3
# Lit role: furnace glow. Albedo matches the in-game hue so the window reads
# hot even in daylight; preview emission = the EmissiveColor normalised, at a
# strength that shows orange instead of clipping to white.
K.roles["glow"] = dict(albedo="#FF8A2A", metal=0.0, rough=0.25, emit=1, dust=0.0)
K.mats["glow"] = K._preview_material("glow", K.roles["glow"])
_gb = K.mats["glow"].node_tree.nodes["Principled BSDF"]
_gb.inputs["Emission Color"].default_value = (1.0, 0.267, 0.025, 1.0)
_gb.inputs["Emission Strength"].default_value = 1.5
# Big painted panels: dark weathered oxide red-brown (the sheet's painted steel).
# The kit's RustBeam (#B86F4B) baked to flat bright ORANGE over panel-sized areas
# (2026-10-14 art review: "the orange box, just cleaner"), so RustBeam is not used
# here at all; orange is only the furnace glow and small accent trim.
K.roles["oxide"] = dict(albedo="#5E2E22", metal=0.15, rough=0.8, paint=1, dust=1.0)
K.mats["oxide"] = K._preview_material("oxide", K.roles["oxide"])
parts = []
add = parts.append
S2 = math.sqrt(0.5)
SINK = 0.003                               # every plate/frame bites 3 mm into its support


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


def chamfered_box_planes(x0, x1, y0, y1, z0, z1, top=None, vert=None, front_top=None,
                         back_top=None, side_top=None, bottom=None):
    """Half-spaces of an axis box with optional 45 deg chamfers: `top` on all
    four top edges (unless front_top/back_top/side_top override), `vert` on the
    four vertical edges, `bottom` on the four bottom edges."""
    P = [((1, 0, 0), x1), ((-1, 0, 0), -x0), ((0, 1, 0), y1), ((0, -1, 0), -y0),
         ((0, 0, 1), z1), ((0, 0, -1), -z0)]
    ft = front_top if front_top is not None else top
    bt = back_top if back_top is not None else top
    st = side_top if side_top is not None else top
    if ft:
        P.append(((0, -S2, S2), S2 * (-y0 + z1 - ft)))
    if bt:
        P.append(((0, S2, S2), S2 * (y1 + z1 - bt)))
    if st:
        P.append(((S2, 0, S2), S2 * (x1 + z1 - st)))
        P.append(((-S2, 0, S2), S2 * (-x0 + z1 - st)))
    if vert:
        for ex, ey in ((1, 1), (1, -1), (-1, 1), (-1, -1)):
            xe = x1 if ex > 0 else -x0
            ye = y1 if ey > 0 else -y0
            P.append(((ex * S2, ey * S2, 0), S2 * (xe + ye - vert)))
    if bottom:
        P.append(((0, -S2, -S2), S2 * (-y0 - z0 - bottom)))
        P.append(((0, S2, -S2), S2 * (y1 - z0 - bottom)))
        P.append(((S2, 0, -S2), S2 * (x1 - z0 - bottom)))
        P.append(((-S2, 0, -S2), S2 * (-x0 - z0 - bottom)))
    return P


def bezel(name, center, w, h, proud, radius, rim, recess, rim_role, inner_role, facing=-1):
    """K.bezel with its failure modes refused (rim >= radius folds the inset or
    collapses the window corners; see rh_build_airfilter.py)."""
    assert radius - rim >= 0.005, "%s: bezel rim %.3f must be < radius %.3f - 5 mm" % (
        name, rim, radius)
    return K.bezel(name, center, w, h, proud, radius=radius, rim=rim, recess=recess,
                   rim_role=rim_role, inner_role=inner_role, facing=facing)


def oct_frame(name, ctr, y_back, w, h, proud, chamfer, rim, recess, rim_role, inner_role):
    """Chamfered-corner (octagonal) frame facing -Y: back face on y_back, front
    at y_back - proud, a `rim`-wide frame and a window recessed `recess`. The
    sheet's door frame, door leaf and window frame are all this shape."""
    assert chamfer - 0.83 * rim >= 0.02, "%s: chamfer too small for the rim" % name
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    for v in bm.verts:
        v.co = Vector((v.co.x * w, v.co.y * proud - proud / 2.0, v.co.z * h))
    ys = [e for e in bm.edges if abs((e.verts[1].co - e.verts[0].co).normalized().y) > 0.99]
    bmesh.ops.bevel(bm, geom=ys, offset=chamfer, offset_type="OFFSET", segments=1,
                    profile=0.5, affect="EDGES", clamp_overlap=True)
    bm.faces.ensure_lookup_table()
    front = min(bm.faces, key=lambda f: f.calc_center_median().y)
    bmesh.ops.inset_individual(bm, faces=[front], thickness=rim, depth=0.0)
    ext = bmesh.ops.extrude_discrete_faces(bm, faces=[front])
    inner = ext["faces"][0]
    for v in inner.verts:
        v.co.y += recess
    walls = {f for v in inner.verts for f in v.link_faces}
    for f in bm.faces:
        f.material_index = 1 if f in walls else 0
    bmesh.ops.transform(bm, matrix=Matrix.Translation((ctr[0], y_back, ctr[1])), verts=bm.verts)
    return K._obj(name, bm, [rim_role, inner_role]), y_back - proud + recess


def oct_plate(name, ctr, y0, y1, w, h, chamfer, role, bevel=0.008):
    """Solid octagonal plate (door leaf, lip) between y0 > y1 (y1 the front)."""
    cx, cz = ctr
    return K.box(name, (cx - w / 2, y1, cz - h / 2), (cx + w / 2, y0, cz + h / 2), role,
                 bevel=bevel, seg=1, chamfers=[(lambda d, m: abs(d.y) > 0.99, chamfer)])


def tube(name, pts, r_out, r_in, role_out, role_in, bend=0.25, sides=16, samples=8):
    """Hollow tube swept along a filleted polyline (parallel-transport frames):
    outer wall, inner wall, an annular lip at both ends. The open start is
    buried in a flange; the inner wall carries `role_in` (soot)."""
    path = rh_hs._fillet([Vector(p) for p in pts], bend, samples)
    n = len(path)
    T = []
    for i in range(n):
        if i == 0:
            t = path[1] - path[0]
        elif i == n - 1:
            t = path[-1] - path[-2]
        else:
            t = path[i + 1] - path[i - 1]
        T.append(t.normalized())
    ref = Vector((1, 0, 0)) if abs(T[0].x) < 0.9 else Vector((0, 1, 0))
    N = (ref - T[0] * ref.dot(T[0])).normalized()
    bm = bmesh.new()
    outer, inner = [], []
    for i, P in enumerate(path):
        if i > 0:
            ax = T[i - 1].cross(T[i])
            if ax.length > 1e-8:
                N = Matrix.Rotation(T[i - 1].angle(T[i]), 3, ax.normalized()) @ N
            N = (N - T[i] * N.dot(T[i])).normalized()
        B = T[i].cross(N)
        ring_o, ring_i = [], []
        for k in range(sides):
            a = 2 * math.pi * k / sides
            d = N * math.cos(a) + B * math.sin(a)
            ring_o.append(bm.verts.new(P + d * r_out))
            ring_i.append(bm.verts.new(P + d * r_in))
        outer.append(ring_o)
        inner.append(ring_i)
    for i in range(n - 1):
        for k in range(sides):
            k1 = (k + 1) % sides
            f = bm.faces.new((outer[i][k], outer[i][k1], outer[i + 1][k1], outer[i + 1][k]))
            f.material_index = 0
            f = bm.faces.new((inner[i][k1], inner[i][k], inner[i + 1][k], inner[i + 1][k1]))
            f.material_index = 1
    for k in range(sides):
        k1 = (k + 1) % sides
        f = bm.faces.new((outer[-1][k], outer[-1][k1], inner[-1][k1], inner[-1][k]))
        f.material_index = 0
        f = bm.faces.new((outer[0][k1], outer[0][k], inner[0][k], inner[0][k1]))
        f.material_index = 0
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    return K._obj(name, bm, [role_out, role_in])


def stripes(prefix, x0, x1, z0, z1, y_face, role="hazard", width=0.075, pitch=0.17,
            slant=0.10, proud=0.006):
    """Diagonal hazard stripes on a -Y-facing band (plane y_face): one thin
    parallelogram prism per stripe, `proud` metres off the band, fully inside
    [x0, x1]. Returns the parts."""
    out = []
    x = x0
    k = 0
    while x + width + slant <= x1 + 1e-6:
        bm = bmesh.new()
        quad = [(x, z0), (x + width, z0), (x + width + slant, z1), (x + slant, z1)]
        fr = [bm.verts.new((qx, y_face - proud, qz)) for qx, qz in quad]
        bk = [bm.verts.new((qx, y_face + SINK, qz)) for qx, qz in quad]
        bm.faces.new(fr)
        bm.faces.new(list(reversed(bk)))
        for a in range(4):
            b = (a + 1) % 4
            bm.faces.new((fr[a], bk[a], bk[b], fr[b]))
        bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
        out.append(K._obj("%s%d" % (prefix, k), bm, [role]))
        x += pitch
        k += 1
    return out


def turn(objs, deg, axis="Z"):
    """Rotate parts built on a -Y-facing virtual face onto a side (Z +-90:
    +90 carries plane y=-h to x=+h, virtual x -> world y; -90 carries it to
    x=-h, virtual x -> world -y)."""
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


def bolt(name, at, normal, r=0.016, h=0.010):
    """Hex bolt sunk SINK into its surface (no coplanar underside)."""
    n = Vector(normal).normalized()
    return K.bolt(name, Vector(at) - n * SINK, n, r=r, h=h + SINK)


def bolts(out, prefix, pts, y, facing=-1, r=0.016, h=0.010):
    for bx, bz in pts:
        out.append(bolt(prefix, (bx, y, bz), (0, facing, 0), r=r, h=h))


def rect_pts(x0, x1, z0, z1):
    return [(x0, z0), (x1, z0), (x0, z1), (x1, z1)]


def role_stats(objs):
    """Surface share per role: all faces, and as seen from the door side."""
    tot, front = {}, {}
    for o in objs:
        me = o.data
        for p in me.polygons:
            r = me.materials[p.material_index]["rh_role"]
            tot[r] = tot.get(r, 0.0) + p.area
            front[r] = front.get(r, 0.0) + p.area * max(0.0, -p.normal.y)
    for name, d in (("all faces", tot), ("seen from door side", front)):
        s = sum(d.values())
        rh_hs.log("role share (%s): %s" % (name, ", ".join(
            "%s %.1f%%" % (k, 100 * v / s) for k, v in sorted(d.items(), key=lambda kv: -kv[1]))))


# ------------------------------------------------------------------ layout
# Built door at -Y. Plinth, slate core, front armour, rust panels proud of the core.
PX, PY, PZ = 1.62, 1.72, 0.40              # plinth half extents (x, y) and top
CX, CY, CZ = 1.45, 1.55, 2.90              # slate core half extents and top
CT, CV = 0.18, 0.10                        # core chamfers: top edges, vertical edges
AX, AY0, AY1, AZ = 1.50, -1.62, -0.85, 2.96  # front armour: half width, front, back, top
AFT, AV = 0.42, 0.20                       # armour chamfers: front-top, front vertical
AST = AX + AZ - (CX + CZ - CT) - 0.05 * math.sqrt(2)   # side-top: 5 cm proud of the core's
PP = 0.03                                  # rust panel thickness (proud of the core)
YF = AY0                                   # armour front plane (door face)

# ------------------------------------------------------------------- plinth
add(convex_block("plinth", chamfered_box_planes(-PX, PX, -PY, PY, 0.0, PZ, top=0.08,
                                                vert=0.22), "slate", bevel=0.012))
# accent corner plates on the plinth's four chamfered corners
for ex in (1, -1):
    for ey in (1, -1):
        n = Vector((ex * S2, ey * S2, 0.0))
        t = Vector((-ey * S2, ex * S2, 0.0))
        d0 = S2 * (PX + PY - 0.22)
        add(convex_block("pcorner", [(tuple(n), d0 + 0.008), (tuple(-n), -(d0 - 0.04)),
                                     (tuple(t), 0.10 + t.dot(n * d0)),
                                     (tuple(-t), 0.10 - t.dot(n * d0)),
                                     ((0, 0, 1), 0.30), ((0, 0, -1), -0.06)], "accent",
                         bevel=0.004, seg=1))

# ------------------------------------------------------------- core + armour
add(convex_block("core", chamfered_box_planes(-CX, CX, -CY, CY, PZ - 0.01, CZ, top=CT,
                                              vert=CV), "slate", bevel=0.012))
arm = chamfered_box_planes(-AX, AX, AY0, AY1, PZ - 0.02, AZ, front_top=AFT, side_top=AST,
                           back_top=0)
for ex in (1, -1):                         # front vertical chamfers only
    arm.append(((ex * S2, -S2, 0), S2 * (AX - AY0 - AV)))
# the three chamfers meet at the front-top corners: cut that corner square-on
for ex in (1, -1):
    n = Vector((ex, -1, 1)).normalized()
    arm.append((tuple(n), n.dot(Vector((ex * (AX - 0.20), AY0 + 0.20, AZ - 0.18)))))
add(convex_block("armour", arm, "slate", bevel=0.016))

# weld beads dividing the armour cap into three (the sheet's front cap)
for x in (-0.62, 0.62):
    add(K.pipe("weld%d" % (x > 0), [(x, YF, 2.505), (x, YF, AZ - AFT),
                                     (x, YF + AFT, AZ), (x, AY1 - 0.02, AZ)],
               0.022, "slate", bend=0.05, sides=8, samples=3))

# ------------------------------------------------- front: hazard band, markers
front = []
HB_Z0, HB_Z1 = 2.27, 2.50
front.append(K.box("hband", (-1.24, YF - 0.022, HB_Z0), (1.24, YF + SINK, HB_Z1), "slate",
                   bevel=0.006, seg=1))
front += stripes("hstripe", -1.19, 1.19, HB_Z0 + 0.025, HB_Z1 - 0.025, YF - 0.022,
                 width=0.095, pitch=0.20, slant=0.11)
for sx in (-1, 1):                         # small accent markers on the armour columns
    front.append(K.box("fmark", (sx * 1.20 - 0.05, YF - 0.012, 1.94),
                       (sx * 1.20 + 0.05, YF + SINK, 2.20), "accent", bevel=0.004, seg=1))
bolts(front, "fbolt", [(sx * 1.24, z) for sx in (-1, 1) for z in (0.62, 1.20, 1.55)], YF)
parts += front

# ------------------------------------------------------------- furnace door
# Depth stack from the armour face: accent lip -> slate frame -> rust leaf ->
# light alloy window frame -> glow. Window recessed only 3.5 cm (54 deg camera).
DZ = 1.34
LIP_W, LIP_H = 1.94, 1.78
FR_W, FR_H, FR_RIM = 1.78, 1.62, 0.15
add(oct_plate("door_lip", (0.0, DZ), YF + SINK, YF - 0.06, LIP_W, LIP_H, 0.30, "accent"))
frame, fwin = oct_frame("door_frame", (0.0, DZ), YF - 0.06 + SINK, FR_W, FR_H, 0.12, 0.24,
                        FR_RIM, 0.05, "slate", "slate")
add(frame)
FR_FRONT = YF - 0.06 + SINK - 0.12          # door frame front plane
LEAF_W, LEAF_H = FR_W - 2 * FR_RIM - 0.06, FR_H - 2 * FR_RIM - 0.06
LEAF_F = YF - 0.24                         # leaf front plane, 6 cm proud of the frame
add(oct_plate("door_leaf", (0.0, DZ), fwin + SINK, LEAF_F, LEAF_W, LEAF_H, 0.17, "oxide",
              bevel=0.012))
WZ = DZ + 0.10
win, wy = oct_frame("win_frame", (0.0, WZ), LEAF_F + SINK, 0.96, 0.84, 0.055, 0.16, 0.10,
                    0.035, "metal", "glow")
add(win)
door = []
lw, lh = LEAF_W / 2 - 0.07, LEAF_H / 2 - 0.07
bolts(door, "lbolt", [(-lw + 0.10, DZ - lh), (lw - 0.10, DZ - lh), (-lw, DZ),
                      (lw, DZ), (-lw + 0.10, DZ + lh), (lw - 0.10, DZ + lh)], LEAF_F, r=0.02)
fb = FR_W / 2 - FR_RIM / 2
bolts(door, "frbolt", [(-fb, DZ - 0.45), (fb, DZ - 0.45), (-fb, DZ + 0.45), (fb, DZ + 0.45)],
      FR_FRONT, r=0.02)
# heavy latch bar across the lower leaf
door.append(K.box("latch", (-0.40, LEAF_F - 0.05, DZ - 0.47), (0.40, LEAF_F + SINK, DZ - 0.37),
                  "metal", bevel=0.012))
for sx in (-1, 1):
    door.append(K.box("latch_blk", (sx * 0.48 - 0.07, LEAF_F - 0.07, DZ - 0.50),
                      (sx * 0.48 + 0.07, LEAF_F + SINK, DZ - 0.34), "slate", bevel=0.012))
parts += door
# big vertical grab handles either side of the door (the sheet's D-handles)
for sx in (-1, 1):
    hx = sx * 1.13
    for z in (0.92, 1.78):
        add(K.box("hmount", (hx - 0.08, YF - 0.04, z - 0.08), (hx + 0.08, YF + SINK, z + 0.08),
                  "slate", bevel=0.01))
    add(K.pipe("handle%d" % (sx > 0), [(hx, YF - 0.02, 0.92), (hx, YF - 0.20, 0.92),
                                       (hx, YF - 0.20, 1.78), (hx, YF - 0.02, 1.78)],
               0.045, "metal", bend=0.09, sides=10, samples=4))

# ----------------------------------------------------- sides (virtual -Y face)
# Side face plane y = -CX on the virtual face; virtual x = s * world y.
SV = -CX
SIDE_Y0, SIDE_Y1 = AY1 + 0.03, CY - CV - 0.04       # world y span of the side panels
LOW_Z0, LOW_Z1 = PZ + 0.04, 1.56
BND_Z0, BND_Z1 = 1.60, 1.86
UP_Z0, UP_Z1 = 1.90, CZ - CT - 0.02
HATCH_Y0, HATCH_Y1 = AY1 + 0.05, -0.26              # accent hatch (world y)
HATCH_Z0 = 2.25                                     # hatch bottom: the upper third only
SPLIT_LOW = 0.30                                    # lower-row seam (world y)


def vspan(s, wy0, wy1):
    a, b = s * wy0, s * wy1
    return min(a, b), max(a, b)


def side_kit(s):
    out = []
    pv = SV - PP                                    # panel front plane (virtual)
    # lower row: front + rear panels
    for wy0, wy1 in ((SIDE_Y0, SPLIT_LOW - 0.02), (SPLIT_LOW + 0.02, SIDE_Y1)):
        x0, x1 = vspan(s, wy0, wy1)
        out.append(K.box("sp_low", (x0, pv, LOW_Z0), (x1, SV + SINK, LOW_Z1), "oxide",
                         bevel=0.01))
        bolts(out, "spb", rect_pts(x0 + 0.05, x1 - 0.05, LOW_Z0 + 0.05, LOW_Z1 - 0.05), pv,
              r=0.014)
    # hazard band (slate) with HazYellow stripes
    x0, x1 = vspan(s, SIDE_Y0, SIDE_Y1)
    out.append(K.box("sp_band", (x0, pv - 0.005, BND_Z0), (x1, SV + SINK, BND_Z1), "slate",
                     bevel=0.006, seg=1))
    out += stripes("sstripe", x0 + 0.06, x1 - 0.06, BND_Z0 + 0.03, BND_Z1 - 0.03, pv - 0.005,
                   width=0.095, pitch=0.20, slant=0.11)
    # upper row: small panel under the accent hatch, long panel behind it
    x0, x1 = vspan(s, SIDE_Y0, HATCH_Y1)
    out.append(K.box("sp_uf", (x0, pv, UP_Z0), (x1, SV + SINK, HATCH_Z0 - 0.04), "oxide",
                     bevel=0.01))
    x0, x1 = vspan(s, HATCH_Y1 + 0.04, SIDE_Y1)
    out.append(K.box("sp_up", (x0, pv, UP_Z0), (x1, SV + SINK, UP_Z1), "oxide", bevel=0.01))
    bolts(out, "spb", rect_pts(x0 + 0.05, x1 - 0.05, UP_Z0 + 0.05, UP_Z1 - 0.05), pv, r=0.014)
    # rear lower panel: bolted inspection plate
    cx = s * 1.06
    out.append(K.box("insp_rim", (cx - 0.32, pv - 0.012, 0.66), (cx + 0.32, pv + SINK + 0.002, 1.34),
                     "accent", bevel=0.006, seg=1))  # 4 cm accent border
    out.append(K.box("insp", (cx - 0.28, pv - 0.025, 0.70), (cx + 0.28, pv + SINK, 1.30), "oxide",
                     bevel=0.012))
    bolts(out, "ipb", rect_pts(cx - 0.23, cx + 0.23, 0.75, 1.25), pv - 0.025, r=0.016)
    if s > 0:
        out += control_panel(s, pv)
    else:
        # intake louvre on the plain side's front lower panel
        lx = s * -0.30
        bz, lwy = bezel("louvre", (lx, pv + SINK, 1.00), 0.60, 0.62, 0.04 + SINK, radius=0.05,
                        rim=0.04, recess=0.025, rim_role="accent", inner_role="rubber")
        out.append(bz)
        rot = Matrix.Rotation(math.radians(35.0), 3, "X")
        for k in range(6):
            z = 1.00 - 0.27 + 0.54 * (k + 0.5) / 6
            bm = bmesh.new()
            bmesh.ops.create_cube(bm, size=1.0)
            for v in bm.verts:
                v.co = rot @ Vector((v.co.x * 0.528, v.co.y * 0.036, v.co.z * 0.018))
            bmesh.ops.translate(bm, vec=Vector((lx, lwy - 0.008, z)), verts=bm.verts)
            out.append(K._obj("lslat%d" % k, bm, ["metal"]))
    return turn(out, 90.0 if s > 0 else -90.0)


def control_panel(s, pv):
    """Wall-mounted control box on the +X side's front lower panel: light alloy
    box with a slanted face, small screen, buttons, two knobs, a thin glow line
    and an accent label; conduits drop from it into the plinth."""
    out = []
    cx, cz = s * -0.34, 0.83                       # ~1.75 m in game: crew height (scale cue)
    W, H, D = 0.66, 0.50, 0.15
    out.append(K.box("cbox", (cx - W / 2, pv - D, cz - H / 2), (cx + W / 2, pv + SINK, cz + H / 2),
                     "metal", bevel=0.014, chamfers=[(lambda d, m: abs(d.x) > 0.99 and
                                                      m.y < pv - D + 0.01 and m.z > cz, 0.06)]))
    yfc = pv - D                                   # box front plane
    sb, swy = bezel("cscreen", (cx - 0.12, yfc + SINK, cz + 0.02), 0.30, 0.22, 0.018 + SINK,
                    radius=0.035, rim=0.026, recess=0.010, rim_role="slate", inner_role="cell_off")
    out.append(sb)
    out.append(K.box("cglow", (cx - 0.25, yfc - 0.008, cz - 0.150), (cx + 0.01, yfc + SINK,
                                                                     cz - 0.124), "glow",
                     bevel=0.003, seg=1))
    for k in range(3):                             # buttons under the glow line
        bx = cx - 0.22 + 0.09 * k
        out.append(K.box("cbtn", (bx - 0.03, yfc - 0.016, cz - 0.215), (bx + 0.03, yfc + SINK,
                                                                       cz - 0.172),
                         "slate", bevel=0.004, seg=1))
    for k, kz in enumerate((cz + 0.08, cz - 0.10)):   # two knobs on the right
        out.append(K.cyl("cknob%d" % k, (cx + 0.19, yfc + SINK, kz), (cx + 0.19, yfc - 0.035, kz),
                         0.048, "slate", sides=12))
        out.append(K.box("cknob_tick%d" % k, (cx + 0.182, yfc - 0.045, kz), (cx + 0.198,
                                                                            yfc - 0.029, kz + 0.04),
                         "metal", bevel=0.0))
    out.append(K.box("clabel", (cx + 0.05, yfc - 0.006, cz - 0.232), (cx + 0.29, yfc + SINK,
                                                                     cz - 0.200),
                     "accent", bevel=0.003, seg=1))
    for k, dx in enumerate((-0.20, -0.08, 0.04)):   # conduits into the plinth
        out.append(K.pipe("cond%d" % k, [(cx + dx, pv - 0.06, cz - H / 2 + 0.02),
                                         (cx + dx, pv - 0.06, PZ - 0.04)],
                          0.030, "pipe", sides=8))
    return out


for s in (1, -1):
    parts += side_kit(s)

# accent hatches wrapping the top-side chamfer at the front of each side (the
# sheet's orange hatch), with a dark grab slot
HX_OUT = CX + 0.055                                  # 2.5 cm proud of the panels
for s in (1, -1):
    hp = [((s, 0, 0), HX_OUT), ((-s, 0, 0), -(CX - 0.25)), ((0, 1, 0), HATCH_Y1),
          ((0, -1, 0), -HATCH_Y0), ((0, 0, 1), CZ + 0.035), ((0, 0, -1), -HATCH_Z0),
          ((s * S2, 0, S2), S2 * (CX + CZ - CT + 0.035 * math.sqrt(2)))]
    add(convex_block("hatch", hp, "oxide", bevel=0.012))
    x0, x1 = sorted((s * (HX_OUT - 0.004), s * (HX_OUT + 0.008)))
    add(K.box("hlip", (x0, HATCH_Y0 + 0.02, HATCH_Z0 + 0.012), (x1, HATCH_Y1 - 0.02,
                                                                  HATCH_Z0 + 0.055),
              "accent", bevel=0.003, seg=1))         # thin accent lip, not a slab
    add(K.box("hslot", (x0, HATCH_Y0 + 0.08, HATCH_Z0 + 0.075), (x1, HATCH_Y1 - 0.08,
                                                                  HATCH_Z0 + 0.175),
              "rubber", bevel=0.004, seg=1))

# ---------------------------------------------------------------------- top
TOPZ = CZ + PP
TX = CX - CT - 0.03                                  # top panels stop short of the chamfers
for y0, y1 in ((AY1 + 0.03, 0.20), (0.24, CY - CT - 0.03)):
    add(K.box("tp", (-TX, y0, CZ - SINK), (TX, y1, TOPZ), "slate", bevel=0.01))
top = []
for y0, y1 in ((AY1 + 0.03, 0.20), (0.24, CY - CT - 0.03)):
    for bx in (-TX + 0.05, TX - 0.05):
        for by in (y0 + 0.05, y1 - 0.05):
            top.append(bolt("tbolt", (bx, by, TOPZ), (0, 0, 1), r=0.016))
parts += top
# bolted rust charging hatch on the front top plate: the red-brown panel read
# from the default camera, which sees the top more than any face
add(K.box("tcharge_rim", (-0.82, -0.66, TOPZ - SINK - 0.002), (0.82, 0.08, TOPZ + 0.018), "accent",
          bevel=0.008, seg=1))                       # 4 cm accent border round the hatch
add(K.box("tcharge", (-0.78, -0.62, TOPZ - SINK), (0.78, 0.04, TOPZ + 0.035), "oxide",
          bevel=0.012))
add(K.box("tcharge_grip", (-0.22, -0.56, TOPZ + 0.03), (0.22, -0.48, TOPZ + 0.075), "metal",
          bevel=0.01))
tch = []
for bx in (-0.72, -0.25, 0.25, 0.72):
    for by in (-0.56, -0.02):
        if abs(bx) < 0.3 and by < -0.3:
            continue                                 # the grip sits there
        tch.append(bolt("tcbolt", (bx, by, TOPZ + 0.035), (0, 0, 1), r=0.018))
parts += tch

# exhaust stacks: base flange, lower tube, accent heat band, bolted flange pair,
# hollow upper tube that elbows 50 deg toward the back
STX, STY, SR, SRI = 0.50, 0.66, 0.195, 0.155
for sx in (-1, 1):
    x = sx * STX
    add(K.cyl("sbase", (x, STY, TOPZ - 0.01), (x, STY, TOPZ + 0.05), 0.32, "metal", sides=20))
    add(K.cyl("slow", (x, STY, TOPZ + 0.02), (x, STY, 3.42), SR, "pipe", sides=16))
    add(K.cyl("sband", (x, STY, 3.08), (x, STY, 3.30), SR + 0.012, "accent", sides=16))
    add(K.cyl("sflA", (x, STY, 3.40), (x, STY, 3.475), 0.29, "metal", sides=20))
    add(K.cyl("sflB", (x, STY, 3.465), (x, STY, 3.54), 0.28, "metal", sides=20))
    for k in range(8):
        a = 2 * math.pi * (k + 0.5) / 8
        add(bolt("sfbolt", (x + 0.24 * math.cos(a), STY + 0.24 * math.sin(a), 3.54),
                 (0, 0, 1), r=0.022, h=0.022))
    add(K.cyl("ssoot", (x, STY, 3.51), (x, STY, 3.62), SRI + 0.004, "rubber", sides=16))
    tilt = math.radians(50.0)
    d = Vector((0.0, math.sin(tilt), math.cos(tilt)))
    p_bend = Vector((x, STY, 3.92))
    p_end = p_bend + d * 0.45
    add(tube("stack%d" % (sx > 0), [(x, STY, 3.50), tuple(p_bend), tuple(p_end)],
             SR, SRI, "pipe", "rubber", bend=0.22, sides=16, samples=8))
    # chunky mouth lip: a short wider collar, its bore 3 mm wider than the stack's
    add(tube("slip%d" % (sx > 0), [tuple(p_end - d * 0.06), tuple(p_end + d * 0.012)],
             SR + 0.022, SRI + 0.003, "accent", "rubber", bend=0.0, sides=16, samples=1))

# riser conduits: up the +X side from the plinth, over the chamfer, across the
# top into the right stack (the sheet's side pipes)
for k, wy in enumerate((0.56, 0.70)):
    rx = CX + PP + 0.045
    add(K.pipe("riser%d" % k, [(rx, wy, PZ - 0.04), (rx, wy, 2.70),
                               (CX - 0.15, wy, TOPZ + 0.045), (STX + 0.10, wy, TOPZ + 0.045)],
               0.045, "pipe", bend=0.16, sides=10, samples=5))
for z in (1.05, 2.25):
    add(K.box("rclamp", (CX + PP - 0.01, 0.49, z - 0.04), (CX + PP + 0.11, 0.77, z + 0.04),
              "accent", bevel=0.008))

# ---------------------------------------------------------------------- back
back = []
BYF = -(CY)                                          # virtual: back face built at -Y,
bp = -(CY + PP)                                      # then turned 180 deg
for x0, x1, z0, z1 in ((-1.32, -0.78, PZ + 0.04, CZ - CT - 0.02),
                       (0.78, 1.32, PZ + 0.04, CZ - CT - 0.02),
                       (-0.74, 0.74, 2.54, CZ - CT - 0.02),
                       (-0.74, 0.74, PZ + 0.04, 0.96)):
    back.append(K.box("bpanel", (x0, bp, z0), (x1, BYF + SINK, z1), "oxide", bevel=0.01))
    bolts(back, "bpb", rect_pts(x0 + 0.05, x1 - 0.05, z0 + 0.05, z1 - 0.05), bp, r=0.014)
parts += turn(back, 180.0)
# heat-exchanger box (slate, chamfered) with a T weld, pipes down to the ring
EX_Y1 = CY + 0.31
add(convex_block("exch", chamfered_box_planes(-0.72, 0.72, CY - 0.05, EX_Y1, 1.00, 2.50,
                                              back_top=0.14, vert=None) +
                 [((S2, S2, 0), S2 * (0.72 + EX_Y1 - 0.12)), ((-S2, S2, 0), S2 * (0.72 + EX_Y1 - 0.12)),
                  ((0, S2, -S2), S2 * (EX_Y1 - 1.00 - 0.08))], "slate", bevel=0.014))
add(K.pipe("eweldH", [(-0.58, EX_Y1, 1.78), (0.58, EX_Y1, 1.78)], 0.018, "slate", sides=8))
add(K.pipe("eweldV", [(0.0, EX_Y1, 1.10), (0.0, EX_Y1, 1.78)], 0.018, "slate", sides=8))
for x in (-0.16, 0.16):                              # header loops (the sheet's back view)
    add(K.pipe("eheader", [(x, CY + 0.10, 2.44), (x, CY + 0.10, 2.70), (x, CY - 0.12, 2.86)],
               0.038, "pipe", bend=0.10, sides=10, samples=4))
ex_b = []
for sx in (-1, 1):                                   # small accent ID plates on the exchanger
    x0, x1 = sorted((sx * 0.12, sx * 0.48))
    ex_b.append(K.box("eplate", (x0, -EX_Y1 - 0.012, 2.02), (x1, -EX_Y1 + SINK, 2.24), "accent",
                      bevel=0.004, seg=1))
bolts(ex_b, "ebolt", rect_pts(-0.56, 0.56, 1.12, 2.30) + [(-0.56, 1.70), (0.56, 1.70)],
      -EX_Y1, r=0.018)
parts += turn(ex_b, 180.0)
RING_O = 0.07                                        # base ring offset from the plinth faces
RING_R = 0.05
RZ = (0.11, 0.28)
for x in (-0.52, -0.30, 0.30, 0.52):
    add(K.pipe("epipe", [(x, CY + 0.17, 1.04), (x, CY + 0.17, 0.62),
                         (x, PY + RING_O, 0.52), (x, PY + RING_O, RZ[1] + 0.02)],
               0.042, "pipe", bend=0.08, sides=10, samples=4))
    add(K.cyl("ecollar", (x, CY + 0.17, 0.86), (x, CY + 0.17, 0.95), 0.058, "accent", sides=10))

# ---------------------------------------------------- base: double pipe ring
RX, RY = PX + RING_O, PY + RING_O
for k, z in enumerate(RZ):
    add(K.pipe("ring%d" % k, [(-0.95, -RY, z), (-RX, -RY, z), (-RX, RY, z), (RX, RY, z),
                              (RX, -RY, z), (-0.85, -RY, z)], RING_R, "pipe", bend=0.28,
               sides=10, samples=6))
add(K.cyl("coupling", (-1.03, -RY, RZ[0]), (-0.77, -RY, RZ[0]), RING_R + 0.022, "metal", sides=12))
add(K.cyl("coupling", (-1.03, -RY, RZ[1]), (-0.77, -RY, RZ[1]), RING_R + 0.022, "metal", sides=12))
CL = RING_O + RING_R + 0.025                         # clamp reach past the plinth face
for x in (-1.15, 1.15, 0.0):                         # front / back clamps
    for sy in (-1, 1):
        if sy < 0 and x == 0.0:
            continue                                 # keep the front centre clean
        y0, y1 = sorted((sy * (PY - 0.01), sy * (PY + CL)))
        add(K.box("rclampF", (x - 0.045, y0, 0.03), (x + 0.045, y1, 0.36), "metal", bevel=0.01))
for y in (-1.05, 0.0, 1.05):                         # side clamps
    for sx in (-1, 1):
        x0, x1 = sorted((sx * (PX - 0.01), sx * (PX + CL)))
        add(K.box("rclampS", (x0, y - 0.045, 0.03), (x1, y + 0.045, 0.36), "metal", bevel=0.01))

role_stats(parts)

# ------------------------------------------------------------------ finish
ob = K.finish(parts, NAME)
K.face_camera(ob)                          # furnace door -Y -> -X (default camera)
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
