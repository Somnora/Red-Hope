"""Build the IceDrill from code - the hard-surface remake (2026-10-14).

  blender -b -P scripts/blender/rh_build_icedrill.py -- [--out Martians/gen/hs/IceDrill]
      [--size 2048] [--preview-only]

The game's IceDrill ("Drills subsurface ice. Must sit on an ice deposit. The
first link of the water chain."), a 3 x 3 cell building. Replaces the
image-to-3D IceDrill: a pale-teal melted rig whose auger had fused into the
mast and whose whole hull was tinted in its function colour. The DESIGN is
kept: Martians/gen/icedrill/cutout/icedrill.png, the approved hero reference -
a tracked drilling rig: two crawler tracks with chunky pads, a bone-white
machine house with a cab at the front right and an "ICE" marking, and a TALL
vertical drill mast at the front with guide rails, a rotary drill head and a
spiral auger running down into the ground, a cyan stripe up the mast, braces
and hydraulic cylinders from the house to the mast. Only the construction
changes: every part is an exact primitive, so nothing melts or fuses, the
auger is a real helix in front of a dark web, and the emissive mask is the
lights themselves.

Layout facts:
  - built with the mast / auger at -Y, then K.face_camera() turns it to -X,
    the face the game's default strategy camera meets (measured 2026-10-13).
    The cab (+X before the turn) lands on the player's right, as in the
    reference; the ICE nose block (-X) on the left.
  - the plan is the crawler: tracks 5.6 m long, 4.1 m over the track pads; the
    auger casing pokes ~0.5 m past the track fronts, nothing sticks out
    sideways, so the fit (larger horizontal extent -> 6 m) is ~0.96x and the
    rig keeps real-machine proportions. The mast is ~9.7 m tall on purpose:
    from the 54 deg default camera a vertical metre projects to 0.59 of a
    metre of depth, so a short mast would sit inside the house's outline on
    screen; at 9.7 m the crown clears the counterweight and the mast reads as
    a column standing out of the machine.
  - the mast is the identity and is built chunky: 0.9 x 0.55 m box spine,
    guide rails r 45 mm, kelly bar r 0.10 m, auger flights 45 mm thick at
    0.34 m radius and 0.70 m pitch (round 1's 0.55 m stacked into a ribbed
    column at 54 deg). Its front is a DARK slate web between the rail
    standoffs, so the steel auger reads against it; the glow strips run up the
    white flanges outside the rails in slate channels, broken at the two
    section bands. The back carries a slate rope trunk with cleats (round 1's
    back was a blank 9 m white slab).
  - palette follows the family (art bible 1): bone-white house, cab, nose
    block, mast spine, drill-drive drum; DarkSlate deck, car body, track
    frames, counterweight, mast web, cylinder barrels; gunmetal track pads;
    light alloy rails, rods, wheels; HazYellow deck nosing, mast-foot kick
    plates, roof handrail and cab grab rail.
  - accent (#BCECF9, TintFor IceDrill, ice cyan) is TRIM only: the raised ICE
    letters, diamonds and plate rims (the front plate sits on the nose
    block's 45 deg chamfer, square-on to the 54 deg default camera), two mast
    section bands, the crown band, the drill-drive ring, the ground casing,
    the guide-yoke lip, the windshield and door-window rims, the vent and
    roof-grille rims, the deck fascia, the track side stripes and the
    counterweight band. Measured: 5.4 % of all faces; 5.6-6.6 % of the
    first-hit pixels from the game camera and all four lineup yaws (round 1:
    2.7 % / 3.4 %, so frames, the counterweight band and the track stripes
    went to accent).
  - the glow is ONE hue per asset in game (IceGlow, EmissiveColor ~ (0.8, 2.8,
    5.0) linear), so every lit element - the two mast strips, the cab roof
    light bar, the cab's under-windshield bar and corner lamp, the crown beacon -
    uses the "glow" role whose albedo (#8FD8FF) matches that hue.
  - budget: ~15k tris. Round 1 was 25.5k: each track pad is now ONE
    10-point profile prism with its grouser built in (36 tris, was 88 as two
    bevelled boxes), rollers are plain discs, slats and strips unbevelled.
  - no part floats: the track pads ride on a continuous chain loop that the
    idler and sprocket bite into; the auger hangs from the drill drive on the
    rails and enters a ground casing; every decal, light and plate stands
    >= 4 mm proud of the face it sits on and bites >= 3 mm into it.
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
OUT = argv[argv.index("--out") + 1] if "--out" in argv else "Martians/gen/hs/IceDrill"
SIZE = int(argv[argv.index("--size") + 1]) if "--size" in argv else 2048
PREVIEW_ONLY = "--preview-only" in argv
NAME = "IceDrill"

K = rh_hs.Kit(accent_hex="#BCECF9")       # TintFor(IceDrill), art bible 2.3
# Lit role: ice-blue light. Albedo matches the in-game IceGlow hue so the
# strips still read as ice-blue in daylight. Preview emission = the game's
# EmissiveColor normalised, low enough that review frames show the hue instead
# of clipping to white (the kit's preview default is teal x4).
K.roles["glow"] = dict(albedo="#8FD8FF", metal=0.0, rough=0.25, emit=1, dust=0.0)
K.mats["glow"] = K._preview_material("glow", K.roles["glow"])
_gb = K.mats["glow"].node_tree.nodes["Principled BSDF"]
_gb.inputs["Emission Color"].default_value = (0.16, 0.56, 1.0, 1.0)
_gb.inputs["Emission Strength"].default_value = 1.4
# Auger steel: a mid-light worn steel, darker than the rails' alloy so the
# flights separate from the guide rails, lighter than the slate web behind.
K.roles["steel"] = dict(albedo="#A2A7AD", metal=0.45, rough=0.38, dust=0.6)
K.mats["steel"] = K._preview_material("steel", K.roles["steel"])

parts = []
add = parts.append
S2 = math.sqrt(0.5)
SINK = 0.004                               # plates / frames bite this far into their support


# ------------------------------------------------------------ local helpers
def convex_block(name, planes, role, bevel=0.012, seg=2):
    """Closed convex solid = intersection of half-spaces n.p <= d, bevelled
    (rh_build_airfilter's helper: one watertight hull however many chamfers
    meet at a corner)."""
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


def box_planes(x0, x1, y0, y1, z0, z1):
    return [((1, 0, 0), x1), ((-1, 0, 0), -x0), ((0, 1, 0), y1), ((0, -1, 0), -y0),
            ((0, 0, 1), z1), ((0, 0, -1), -z0)]


def bezel(name, center, w, h, proud, radius, rim, recess, rim_role, inner_role, facing=-1):
    """K.bezel with its failure modes refused (rim >= radius - 5 mm folds or
    collapses the window corners - the AirFilter's round-1 incident)."""
    assert radius - rim >= 0.005, "%s: bezel rim %.3f must be < radius %.3f - 5 mm" % (
        name, rim, radius)
    return K.bezel(name, center, w, h, proud, radius=radius, rim=rim, recess=recess,
                   rim_role=rim_role, inner_role=inner_role, facing=facing)


def lathe(name, prof, centre, roles, sides=24, axis=(0, 0, 1), mis=None, phase=0.0):
    """Revolve a CLOSED (r, h) profile about `axis` through `centre` (h runs
    along the axis). r = 0 points become poles. mis[j] = material index of the
    profile edge j -> j+1."""
    bm = bmesh.new()
    rings = []
    for (r, h) in prof:
        if r < 1e-6:
            v = bm.verts.new((0.0, 0.0, h))
            rings.append([v] * sides)
        else:
            rings.append([bm.verts.new((r * math.cos(phase + 2 * math.pi * i / sides),
                                        r * math.sin(phase + 2 * math.pi * i / sides), h))
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
            f.material_index = mis[j] if mis else 0
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    rot = Vector((0, 0, 1)).rotation_difference(Vector(axis).normalized()).to_matrix().to_4x4()
    bmesh.ops.transform(bm, matrix=Matrix.Translation(Vector(centre)) @ rot, verts=bm.verts)
    return K._obj(name, bm, roles)


def wheel(name, centre, r, hw, role, hub_r, hub_d=0.04, hub_role="metal", sides=20):
    """Disc about the X axis (a track wheel / sheave) with a proud hub each side."""
    prof = [(0.0, -hw - hub_d), (hub_r, -hw - hub_d), (hub_r, -hw), (r, -hw), (r, hw),
            (hub_r, hw), (hub_r, hw + hub_d), (0.0, hw + hub_d)]
    return lathe(name, prof, centre, [role, hub_role], sides=sides, axis=(1, 0, 0),
                 mis=[1, 1, 0, 0, 0, 1, 1, 1])


def beam(name, p0, p1, w, d, role, side=(0, 0, 1), bevel=0.01, seg=1):
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


def frame_box(name, ctr, axes, half, role, bevel=0.008, seg=1):
    """Box of half extents `half` along the orthonormal `axes` (columns), centred at ctr."""
    ob = K.box(name, tuple(-h for h in half), tuple(half), role, bevel=bevel, seg=seg)
    ax, ay, az = axes
    ob.data.transform(Matrix(((ax.x, ay.x, az.x, ctr.x), (ax.y, ay.y, az.y, ctr.y),
                              (ax.z, ay.z, az.z, ctr.z), (0, 0, 0, 1))))
    return ob


def prism(name, pts_xz, y_front, y_back, role):
    """Flat polygon (x, z) extruded between y_front and y_back (front faces -Y)."""
    bm = bmesh.new()
    fr = [bm.verts.new((x, y_front, z)) for x, z in pts_xz]
    bk = [bm.verts.new((x, y_back, z)) for x, z in pts_xz]
    bm.faces.new(fr)
    bm.faces.new(list(reversed(bk)))
    n = len(pts_xz)
    for i in range(n):
        j = (i + 1) % n
        bm.faces.new((fr[i], fr[j], bk[j], bk[i]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    return K._obj(name, bm, [role])


def text_block(name, body, centre, size, y_front, y_back, role, offset=0.0, spacing=1.0):
    """Raised lettering on a -Y-facing surface (rh_build_airfilter's helper):
    builtin font, one solid per glyph, front plane y_front, back y_back."""
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


def place(objs, M):
    """Apply a 4x4 matrix to freshly built parts (mesh data, world origin)."""
    for o in objs:
        o.data.transform(M)
    return objs


def bolts_rect(out, prefix, xs, zs, y, facing=-1, r=0.014):
    for bx in xs:
        for bz in zs:
            out.append(K.bolt(prefix, (bx, y + facing * -0.001, bz), (0, facing, 0), r=r))


def louvre(prefix, centre, w, h, nslat, rim_role="slate", inner_role="rubber",
           slat_role="metal", proud=0.03, recess=0.02):
    """Framed louvre vent on a -Y face (plane y = centre[1]), sunk SINK into it."""
    out = []
    rim = 0.035
    bz, wy = bezel(prefix + "_frame", (centre[0], centre[1] + SINK, centre[2]), w, h,
                   proud + SINK, radius=0.045, rim=rim, recess=recess, rim_role=rim_role,
                   inner_role=inner_role)
    out.append(bz)
    cx, _, cz = centre
    ih = h - 2 * rim
    pitch = ih / nslat
    rot = Matrix.Rotation(math.radians(35.0), 4, "X")
    for k in range(nslat):
        zc = cz - ih / 2 + pitch * (k + 0.5)
        sl = K.box("%s_slat%d" % (prefix, k), (-(w / 2 - rim + 0.004), -0.018, -0.009),
                   (w / 2 - rim + 0.004, 0.018, 0.009), slat_role, bevel=0.0)
        sl.data.transform(Matrix.Translation((cx, wy - 0.008, zc)) @ rot)
        out.append(sl)
    return out


def hyd_cylinder(name, p0, p1, rb, rr, frac=0.56):
    """Hydraulic cylinder p0 -> p1: slate barrel, gland ring, alloy rod, eye blocks."""
    p0, p1 = Vector(p0), Vector(p1)
    d = p1 - p0
    out = [K.cyl(name + "_barrel", p0 + d * 0.02, p0 + d * frac, rb, "slate", sides=14),
           K.cyl(name + "_gland", p0 + d * (frac - 0.02), p0 + d * (frac + 0.015), rb + 0.018,
                 "pipe", sides=14),
           K.cyl(name + "_rod", p0 + d * (frac - 0.05), p1 - d.normalized() * 0.05, rr, "metal",
                 sides=12),
           K.cyl(name + "_cap", p0 - d.normalized() * 0.04, p0 + d * 0.035, rb + 0.012, "slate",
                 sides=14)]
    return out


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
    """Surface share per role (all faces) and as seen square-on from the front."""
    tot, front = {}, {}
    for o in objs:
        me = o.data
        for p in me.polygons:
            r = me.materials[p.material_index]["rh_role"]
            tot[r] = tot.get(r, 0.0) + p.area
            front[r] = front.get(r, 0.0) + p.area * max(0.0, -p.normal.y)
    for name, d in (("all faces", tot), ("seen from the mast side", front)):
        s = sum(d.values())
        rh_hs.log("role share (%s): %s" % (name, ", ".join(
            "%s %.1f%%" % (k, 100 * v / s) for k, v in sorted(d.items(), key=lambda kv: -kv[1]))))
    tris = {}
    for o in objs:
        k = o.name.split(".")[0].rstrip("0123456789_")
        tris[k] = tris.get(k, 0) + sum(len(p.vertices) - 2 for p in o.data.polygons)
    rh_hs.log("tris by part: %s" % ", ".join(
        "%s %d" % kv for kv in sorted(tris.items(), key=lambda kv: -kv[1])[:24]))


# ================================================================== layout
# Before face_camera: front (mast, auger) at -Y, cab at +X, ICE nose block at -X.
TX, TW = 1.64, 0.86                        # track centre |x|, pad width
TY0, TY1 = -2.25, 2.25                     # idler (front) / sprocket (rear) axle y
RB, PT, GH = 0.46, 0.09, 0.04              # belt radius to pad centre, pad thickness, grouser
ZA = RB + PT / 2 + GH                      # axle height (grouser tips on z = 0)
CORE_IN = RB - PT / 2 - 0.035              # chain loop inner radius
CORE_OUT = RB - PT / 2 + 0.012             # chain loop outer radius (bites 12 mm into the pads)

MX0, MX1 = -0.45, 0.45                     # mast spine x
MF, MB = -2.40, -1.85                      # mast front / back face y
MZ0, MZ1 = 0.45, 9.00                      # mast spine z
AX, AY = 0.0, -2.80                        # auger axis
RAIL_X, RAIL_Y, RAIL_R = 0.24, -2.47, 0.045
DECK_Z0, DECK_Z1 = 1.20, 1.42
BAND_Z = (3.30, 6.60)                      # mast section bands


# ================================================================ crawler
def loop_frame(s, R):
    """Point, tangent, outward normal at arc length s on the track loop (YZ plane)."""
    L = TY1 - TY0
    A = math.pi * R
    s = s % (2 * L + 2 * A)
    if s < L:                                       # bottom run, going +Y
        return Vector((0, TY0 + s, ZA - R)), Vector((0, 1, 0)), Vector((0, 0, -1))
    s -= L
    if s < A:                                       # rear arc, bottom -> top
        a = -math.pi / 2 + s / R
        n = Vector((0, math.cos(a), math.sin(a)))
        return Vector((0, TY1, ZA)) + n * R, Vector((0, -math.sin(a), math.cos(a))), n
    s -= A
    if s < L:                                       # top run, going -Y
        return Vector((0, TY1 - s, ZA + R)), Vector((0, -1, 0)), Vector((0, 0, 1))
    s -= L
    a = math.pi / 2 + s / R                         # front arc, top -> bottom
    n = Vector((0, math.cos(a), math.sin(a)))
    return Vector((0, TY0, ZA)) + n * R, Vector((0, -math.sin(a), math.cos(a))), n


def pad_prism(name, c, t, n, L):
    """One track pad WITH its grouser bar: a 10-point profile in the (t, n)
    plane extruded across the track (36 tris; round 1's bevelled pad + grouser
    boxes cost 88)."""
    T, G, g, e = PT / 2, GH, 0.034, 0.016
    prof = [(-L, -T), (L, -T), (L, T - e), (L - e, T), (g, T), (0.6 * g, T + G),
            (-0.6 * g, T + G), (-g, T), (-L + e, T), (-L, T - e)]
    bm = bmesh.new()
    rows = []
    for x in (c.x - TW / 2, c.x + TW / 2):
        rows.append([bm.verts.new(Vector((x, c.y, c.z)) + t * a + n * b) for a, b in prof])
    m = len(prof)
    bm.faces.new(rows[0])
    bm.faces.new(list(reversed(rows[1])))
    for i in range(m):
        j = (i + 1) % m
        bm.faces.new((rows[0][i], rows[0][j], rows[1][j], rows[1][i]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    return K._obj(name, bm, ["pipe"])


def chain_loop(name, cx, role):
    """Continuous chain loop under the pads: one closed band the wheels bite into."""
    L = TY1 - TY0
    A = math.pi * RB
    ss = [L + A * i / 12 for i in range(13)] + [2 * L + A + A * i / 12 for i in range(13)]
    hw = 0.20                                # narrow: it only carries the pads
    bm = bmesh.new()
    secs = []
    for s in ss:
        p, t, n = loop_frame(s, RB)
        pi = p + n * (CORE_IN - RB)
        po = p + n * (CORE_OUT - RB)
        secs.append([bm.verts.new((cx - hw, pi.y, pi.z)), bm.verts.new((cx + hw, pi.y, pi.z)),
                     bm.verts.new((cx + hw, po.y, po.z)), bm.verts.new((cx - hw, po.y, po.z))])
    for a, b in zip(secs, secs[1:] + secs[:1]):
        for k in range(4):
            k1 = (k + 1) % 4
            bm.faces.new((a[k], a[k1], b[k1], b[k]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    return K._obj(name, bm, [role])


def track(sx):
    cx = sx * TX
    out = []
    P = 2 * (TY1 - TY0) + 2 * math.pi * RB
    npad = 36
    pitch = P / npad
    X = Vector((1, 0, 0))
    for k in range(npad):
        p, t, n = loop_frame((k + 0.5) * pitch, RB)
        out.append(pad_prism("pad", Vector((cx, p.y, p.z)), t, n, pitch / 2 - 0.022))
    out.append(chain_loop("chain", cx, "rubber"))
    # idler (front) and drive sprocket (rear) bite 1 cm into the chain loop
    for y, nm in ((TY0, "idler"), (TY1, "sprocket")):
        out.append(wheel(nm, (cx, y, ZA), CORE_IN + 0.01, 0.25, "slate", 0.17, hub_d=0.05, sides=16))
    # track frame (slate), road rollers below it, carrier rollers above
    out.append(K.box("tframe", (cx - 0.21, TY0 + 0.2, ZA - 0.24), (cx + 0.21, TY1 - 0.2, ZA + 0.2),
                     "slate", bevel=0.02, seg=1))
    for y in (-1.6, -0.8, 0.0, 0.8, 1.6):
        out.append(K.cyl("roller", (cx - 0.17, y, ZA - CORE_IN + 0.11), (cx + 0.17, y, ZA - CORE_IN + 0.11),
                         0.12, "metal", sides=10))
    for y in (-1.1, 1.1):
        out.append(K.cyl("carrier", (cx - 0.12, y, ZA + CORE_IN - 0.09), (cx + 0.12, y, ZA + CORE_IN - 0.09),
                         0.10, "metal", sides=10))
    # outer side plate with bolts and a pair of lifting lugs (the reference's
    # side frame), inside the pad width so it never widens the plan
    xa, xb = sorted((cx + sx * 0.205, cx + sx * 0.29))
    out.append(K.box("tplate", (xa, -1.78, ZA - 0.22), (xb, 1.78, ZA + 0.17), "slate", bevel=0.015,
                     seg=1))
    xo = cx + sx * 0.29
    for y in (-1.6, 0.0, 1.6):
        for z in (ZA - 0.16, ZA + 0.12):
            out.append(K.bolt("tbolt", (xo - sx * 0.002, y, z), (sx, 0, 0), r=0.018))
    xa, xb = sorted((xo - sx * 0.004, xo + sx * 0.010))
    out.append(K.box("tstripe", (xa, -1.68, ZA - 0.05), (xb, 1.68, ZA + 0.05), "accent",
                     bevel=0.003, seg=1))
    return out


for sx in (-1, 1):
    parts += track(sx)

# car body between the track frames (bites 2 cm into each), slew ring on top
add(K.box("carbody", (-(TX - 0.19), -1.30, 0.48), (TX - 0.19, 1.30, 0.90), "slate", bevel=0.02, seg=1))
add(lathe("slew", [(0.0, 0.86), (1.12, 0.86), (1.12, 1.08), (1.16, 1.10), (1.16, DECK_Z0 + 0.03),
                   (0.0, DECK_Z0 + 0.03)], (0.0, 0.30, 0.0), ["slate", "pipe"], sides=40,
          mis=[0, 0, 1, 1, 0, 0]))

# ================================================================== house
add(K.box("deck", (-1.96, -1.95, DECK_Z0), (1.96, 2.85, DECK_Z1), "slate", bevel=0.02))
# accent fascia along the deck's flanks (trim: the "machine colour" line)
for sx in (-1, 1):
    xa, xb = sorted((sx * 1.955, sx * 1.972))
    add(K.box("fascia", (xa, -1.75, DECK_Z0 + 0.05), (xb, 2.65, DECK_Z1 - 0.05), "accent",
              bevel=0.004, seg=1))
# hazard nosing along the deck's front edge either side of the mast
for xa, xb in ((-1.85, -0.70), (0.68, 1.85)):
    add(K.box("nosing", (xa, -1.962, DECK_Z0 + 0.04), (xb, -1.944, DECK_Z1 - 0.06), "hazard",
              bevel=0.003, seg=1))

# engine house: one convex solid, side-top and front-top chamfers
HX, HY0, HY1, HZ = 1.92, -0.30, 2.25, 3.05
hp = box_planes(-HX, HX, HY0, HY1, DECK_Z1 - 0.02, HZ)
for sx in (-1, 1):
    hp.append(((sx * S2, 0, S2), S2 * (HX + HZ - 0.15)))
hp.append(((0, -S2, S2), S2 * (-HY0 + HZ - 0.12)))
add(convex_block("house", hp, "hull", bevel=0.015))

# counterweight (slate), rounded back corners, hazard band, bolts
CY0, CY1, CZ = 2.18, 2.95, 2.80
cp = box_planes(-1.88, 1.88, CY0, CY1, DECK_Z1 - 0.04, CZ)
for sx in (-1, 1):
    cp.append(((sx * S2, S2, 0), S2 * (1.88 + CY1 - 0.35)))
cp.append(((0, S2, S2), S2 * (CY1 + CZ - 0.15)))
add(convex_block("counterweight", cp, "slate", bevel=0.02))
add(K.box("cw_band", (-1.45, CY1 - 0.004, 2.16), (1.45, CY1 + 0.012, 2.38), "accent",
          bevel=0.004, seg=1))
for x in (-1.3, -0.45, 0.45, 1.3):
    add(K.bolt("cwbolt", (x, CY1 - 0.001, 1.70), (0, 1, 0), r=0.03, h=0.02))

# nose block (front-left): 45 deg front-top chamfer that carries the ICE plate
NX0, NX1, NY0, NY1, NZ, NCH = -1.90, -0.62, -1.90, -0.20, 2.75, 0.60
npl = box_planes(NX0, NX1, NY0, NY1, DECK_Z1 - 0.02, NZ)
npl.append(((0, -S2, S2), S2 * (-NY0 + NZ - NCH)))
npl.append(((-S2, -S2, 0), S2 * (-NX0 - NY0 - 0.12)))
add(convex_block("nose", npl, "hull", bevel=0.015))

# cab (front-right): front-top chamfer, chamfered front verticals
CX0, CX1, CBY0, CBY1, CBZ = 0.60, 1.90, -2.05, -0.20, 3.25
cbp = box_planes(CX0, CX1, CBY0, CBY1, DECK_Z1 - 0.02, CBZ)
cbp.append(((0, -S2, S2), S2 * (-CBY0 + CBZ - 0.22)))
for sx, xe in ((-1, CX0), (1, CX1)):
    cbp.append(((sx * S2, -S2, 0), S2 * (sx * xe - CBY0 - 0.10)))
add(convex_block("cab", cbp, "hull", bevel=0.015))

# ---------------------------------------------------- ICE plates (accent)
def ice_plate(prefix, w, h, size):
    """Accent-rimmed slate plate with raised ICE letters and an ice diamond,
    built at the origin facing -Y (back face SINK behind y = 0)."""
    pl, yf = bezel(prefix + "_plate", (0.0, SINK, 0.0), w, h, 0.026 + SINK, radius=0.055, rim=0.035,
                   recess=0.008, rim_role="accent", inner_role="slate")
    out = [pl]
    tx = -0.11 * w
    out.append(text_block(prefix + "_txt", "ICE", (tx, 0, 0.0), size, yf - 0.010, yf + 0.003,
                          "accent", offset=0.006, spacing=1.12))
    dx = 0.33 * w
    dh = 0.32 * h
    out.append(prism(prefix + "_dia", [(dx, dh), (dx + 0.62 * dh, 0), (dx, -dh), (dx - 0.62 * dh, 0)],
                     yf - 0.010, yf + 0.003, "accent"))
    # inner diamond: a slate facet in the accent diamond (the reference's ice crystal)
    out.append(prism(prefix + "_dia_in", [(dx, 0.45 * dh), (dx + 0.28 * dh, 0), (dx, -0.45 * dh),
                                          (dx - 0.28 * dh, 0)], yf - 0.015, yf - 0.006, "slate"))
    return out


# front plate on the nose chamfer, square-on to the 54 deg default camera
ch_c = Vector(((NX0 + 0.06 + NX1) / 2, NY0 + NCH / 2, NZ - NCH / 2))
parts += place(ice_plate("ice_f", 1.02, 0.56, 0.32),
               Matrix.Translation(ch_c) @ Matrix.Rotation(math.radians(-45.0), 4, "X"))
# side plate on the nose block's -X face (the reference's flank marking)
parts += place(ice_plate("ice_s", 1.10, 0.50, 0.31),
               Matrix.Translation((NX0, -1.02, 1.98)) @ Matrix.Rotation(math.radians(-90.0), 4, "Z"))

# ------------------------------------------------------------------- cab
# windshield, under-windshield light bar, corner lamp
ws, _ = bezel("windshield", (1.25, CBY0 + SINK, 2.50), 1.00, 0.98, 0.03 + SINK, radius=0.09,
              rim=0.055, recess=0.02, rim_role="accent", inner_role="glass")
add(ws)
add(K.box("cab_ledbox", (0.95, CBY0 - 0.026, 1.78), (1.55, CBY0 + SINK, 1.91), "slate",
          bevel=0.006, seg=1))
add(K.box("cab_led", (1.00, CBY0 - 0.036, 1.815), (1.50, CBY0 - 0.020, 1.875), "glow",
          bevel=0.003, seg=1))
add(K.box("cab_lamp", (1.66, CBY0 - 0.020, 1.60), (1.78, CBY0 + SINK, 1.69), "glow",
          bevel=0.004, seg=1))
# roof light bar: slate housing, glow bar standing proud of it front and top
add(K.box("roofbar_box", (0.85, -1.80, CBZ - 0.03), (1.65, -1.56, CBZ + 0.08), "slate",
          bevel=0.01, seg=1))
add(K.box("roofbar", (0.90, -1.815, CBZ + 0.01), (1.60, -1.62, CBZ + 0.11), "glow",
          bevel=0.004, seg=1))
# side door with window on the +X face (built on a virtual -Y face, turned +90)
side = []
dr, dwy = bezel("cab_door", (-1.10, -CX1 + SINK, 2.22), 0.80, 1.50, 0.025 + SINK, radius=0.07,
                rim=0.05, recess=0.012, rim_role="slate", inner_role="hull")
side.append(dr)
dw, _ = bezel("cab_doorwin", (-1.10, dwy + SINK, 2.62), 0.56, 0.52, 0.02 + SINK, radius=0.06,
              rim=0.035, recess=0.012, rim_role="accent", inner_role="glass")
side.append(dw)
side.append(K.box("cab_handle", (-0.80, dwy - 0.04, 2.10), (-0.74, dwy + SINK, 2.30), "metal",
                  bevel=0.008, seg=1))
parts += place(side, Matrix.Rotation(math.radians(90.0), 4, "Z"))
# grab rail beside the door (r 30 mm)
add(K.pipe("cab_rail", [(CX1 - 0.01, -0.48, 1.70), (CX1 + 0.09, -0.48, 1.70),
                        (CX1 + 0.09, -0.48, 2.70), (CX1 - 0.01, -0.48, 2.70)], 0.030, "hazard",
           bend=0.05, sides=8, samples=3))

# --------------------------------------------------------------- roof kit
top = louvre("grille", (-0.95, -HZ, 1.15), 1.25, 1.05, 7, rim_role="accent", inner_role="rubber",
             slat_role="metal", proud=0.03, recess=0.02)
parts += place(top, Matrix.Rotation(math.radians(-90.0), 4, "X"))
add(K.box("intake", (0.70, 0.85, HZ - 0.02), (1.45, 1.75, HZ + 0.30), "slate", bevel=0.02, seg=1))
for k in range(4):
    y = 0.98 + k * 0.21
    add(K.box("intake_slat%d" % k, (0.76, y - 0.04, HZ + 0.29), (1.39, y + 0.04, HZ + 0.325),
              "metal", bevel=0.006, seg=1))
add(K.cyl("stack_base", (-1.45, 1.95, HZ - 0.02), (-1.45, 1.95, HZ + 0.08), 0.15, "slate", sides=16))
add(K.pipe("stack", [(-1.45, 1.95, HZ + 0.04), (-1.45, 1.95, HZ + 0.78), (-1.45, 2.20, HZ + 0.78)],
           0.085, "pipe", bend=0.10, sides=16))
# HazYellow handrail along the roof's left edge (r 30 mm)
add(K.pipe("roof_rail", [(-1.66, 0.05, HZ - 0.07), (-1.66, 0.05, HZ + 0.70),
                         (-1.66, 2.05, HZ + 0.70), (-1.66, 2.05, HZ - 0.07)], 0.030, "hazard",
           bend=0.08, sides=8, samples=3))
add(K.cyl("roof_post", (-1.66, 1.05, HZ - 0.07), (-1.66, 1.05, HZ + 0.70), 0.028, "hazard", sides=8))

# ------------------------------------------------------- house flanks / back
# -X flank: two service doors and a louvre vent
side = []
for k, (yc, w) in enumerate(((0.30, 0.90), (1.25, 0.90))):
    db, dwy = bezel("svc%d" % k, (-yc, -HX + SINK, 2.12), w, 1.05, 0.02 + SINK, radius=0.05,
                    rim=0.035, recess=0.010, rim_role="slate", inner_role="hull")
    side.append(db)
    side.append(K.box("svc_h%d" % k, (-yc - 0.10, dwy - 0.035, 2.42), (-yc + 0.10, dwy + SINK, 2.48),
                      "metal", bevel=0.008, seg=1))
side += louvre("flank_vent", (-1.98, -HX, 2.20), 0.32, 0.95, 6, rim_role="accent")
parts += place(side, Matrix.Rotation(math.radians(-90.0), 4, "Z"))
# +X flank behind the cab: big engine louvre
side = louvre("engine_vent", (1.05, -HX, 2.20), 1.30, 0.90, 7, rim_role="accent")
parts += place(side, Matrix.Rotation(math.radians(90.0), 4, "Z"))

# ============================================================== mast group
# mast mount block on the deck, front frame under it to the car body, mast foot
add(K.box("mast_mount", (-0.40, MB - 0.05, DECK_Z1 - 0.04), (0.40, -1.10, 2.35), "slate",
          bevel=0.02, seg=1))
add(K.box("front_frame", (-0.36, MB - 0.10, 0.47), (0.36, -1.20, DECK_Z0 + 0.04), "slate",
          bevel=0.02, seg=1))
add(K.box("mast_foot", (-0.55, MF - 0.10, 0.34), (0.55, MB + 0.07, 0.52), "slate", bevel=0.02, seg=1))
for xa, xb in ((-0.50, -0.28), (0.28, 0.50)):
    add(K.box("foot_haz", (xa, MF - 0.107, 0.37), (xb, MF - 0.096, 0.49), "hazard",
              bevel=0.003, seg=1))
# winch drum behind the mast mount (the hoist that lifts the drill string)
add(lathe("winch", [(0.0, -0.50), (0.36, -0.50), (0.36, -0.43), (0.27, -0.41), (0.27, 0.41),
                    (0.36, 0.43), (0.36, 0.50), (0.0, 0.50)], (0.0, -0.70, 1.82),
          ["slate", "metal"], sides=20, axis=(1, 0, 0), mis=[0, 0, 0, 1, 0, 0, 0, 0]))
for sx in (-1, 1):
    xa, xb = sorted((sx * 0.46, sx * 0.58))
    add(K.box("winch_ped", (xa, -0.98, DECK_Z1 - 0.03), (xb, -0.42, 1.92), "slate", bevel=0.015, seg=1))

# mast spine (bone white), section bands (accent), crown
add(K.box("mast", (MX0, MF, MZ0), (MX1, MB, MZ1), "hull", bevel=0.02, seg=2))
for z in BAND_Z:
    add(K.box("mast_band", (MX0 - 0.02, MF - 0.028, z - 0.10), (MX1 + 0.02, MB + 0.02, z + 0.10),
              "accent", bevel=0.008, seg=1))
# dark web between the rails (what the auger reads against), rail standoffs, rails
add(K.box("web", (-0.17, MF - 0.013, 0.60), (0.17, MF + SINK, MZ1 - 0.10), "slate",
          bevel=0.004, seg=1))
for sx in (-1, 1):
    xa, xb = sorted((sx * 0.19, sx * 0.29))
    add(K.box("standoff", (xa, MF - 0.05, 0.55), (xb, MF + SINK, MZ1 - 0.08), "slate", bevel=0.008,
              seg=1))
    add(K.cyl("rail", (sx * RAIL_X, RAIL_Y, 0.53), (sx * RAIL_X, RAIL_Y, MZ1 - 0.04), RAIL_R, "metal",
              sides=12))
# glow strips up both flanges, in slate channels, broken at the bands
segs = [(1.66, BAND_Z[0] - 0.17), (BAND_Z[0] + 0.17, BAND_Z[1] - 0.17), (BAND_Z[1] + 0.17, MZ1 - 0.30)]
for sx in (-1, 1):
    xa, xb = sorted((sx * 0.33, sx * 0.425))
    ga, gb = sorted((sx * 0.345, sx * 0.41))
    for k, (z0, z1) in enumerate(segs):
        add(K.box("strip_ch", (xa, MF - 0.015, z0 - 0.03), (xb, MF + SINK, z1 + 0.03), "slate",
                  bevel=0.0))
        add(K.box("strip", (ga, MF - 0.023, z0), (gb, MF - 0.011, z1), "glow", bevel=0.0))
# rope channel up the mast back (slate), so the back is not a blank slab
add(K.box("rope_trunk", (-0.17, MB - SINK, 0.70), (0.17, MB + 0.07, MZ1 - 0.10), "slate",
          bevel=0.01, seg=1))
for z in (2.4, 4.6, 7.0, 8.4):
    add(K.box("trunk_cleat", (-0.21, MB + 0.06, z - 0.05), (0.21, MB + 0.10, z + 0.05), "pipe",
              bevel=0.008, seg=1))
# crown: hull head with accent band, two sheaves on a pin, beacon
add(K.box("crown", (-0.62, -3.08, MZ1 - 0.05), (0.62, MB + 0.05, MZ1 + 0.35), "hull", bevel=0.025))
add(K.box("crown_band", (-0.635, -3.095, MZ1 + 0.04), (0.635, MB + 0.065, MZ1 + 0.20), "accent",
          bevel=0.006, seg=1))
for sx in (-1, 1):
    add(lathe("sheave", [(0.0, -0.06), (0.30, -0.06), (0.30, -0.035), (0.23, 0.0), (0.30, 0.035),
                         (0.30, 0.06), (0.0, 0.06)], (sx * 0.20, -2.55, MZ1 + 0.42),
              ["metal"], sides=20, axis=(1, 0, 0)))
    xa, xb = sorted((sx * 0.30, sx * 0.40))
    add(K.box("cheek", (xa, -2.80, MZ1 + 0.30), (xb, -2.30, MZ1 + 0.55), "slate", bevel=0.015, seg=1))
add(K.cyl("sheave_pin", (-0.42, -2.55, MZ1 + 0.42), (0.42, -2.55, MZ1 + 0.42), 0.055, "pipe",
          sides=12))
add(lathe("beacon", [(0.0, MZ1 + 0.33), (0.11, MZ1 + 0.33), (0.11, MZ1 + 0.40), (0.0, MZ1 + 0.40)],
          (0.0, -2.05, 0.0), ["slate"], sides=16))
add(lathe("beacon_lamp", [(0.0, MZ1 + 0.39), (0.08, MZ1 + 0.39), (0.08, MZ1 + 0.45),
                          (0.05, MZ1 + 0.49), (0.0, MZ1 + 0.50)], (0.0, -2.05, 0.0), ["glow"],
          sides=16))

# drill drive (rotary head): carriage on the rails, bone-white drum, accent
# ring, hydraulic motors, kelly bar up to the crown
HZ0, HZ1 = 4.92, 6.02
add(K.box("carriage", (-0.36, -2.64, HZ0 + 0.04), (0.36, RAIL_Y - 0.01, HZ1 - 0.02), "slate",
          bevel=0.02, seg=1))
add(lathe("drive", [(0.0, HZ0 - 0.06), (0.22, HZ0 - 0.06), (0.30, HZ0), (0.38, HZ0 + 0.06),
                    (0.38, HZ1 - 0.26), (0.30, HZ1 - 0.18), (0.30, HZ1 - 0.08), (0.20, HZ1 - 0.04),
                    (0.20, HZ1 + 0.03), (0.0, HZ1 + 0.03)], (AX, AY, 0.0), ["hull", "slate"],
          sides=28, mis=[1, 1, 1, 0, 0, 1, 1, 1, 1, 1]))
add(lathe("drive_ring", [(0.37, 5.28), (0.40, 5.28), (0.40, 5.40), (0.37, 5.40)], (AX, AY, 0.0),
          ["accent"], sides=28))
for sx in (-1, 1):
    add(K.cyl("motor", (sx * 0.40, AY + 0.05, HZ0 + 0.10), (sx * 0.40, AY + 0.05, HZ0 + 0.66), 0.11,
              "slate", sides=14))
    add(K.cyl("motor_cap", (sx * 0.40, AY + 0.05, HZ0 + 0.66), (sx * 0.40, AY + 0.05, HZ0 + 0.72),
              0.085, "pipe", sides=14))
add(K.cyl("kelly", (AX, AY, HZ1), (AX, AY, MZ1 - 0.03), 0.10, "pipe", sides=8))

# auger: shaft, helical flights, guide yoke, ground casing
add(K.cyl("shaft", (AX, AY, 0.12), (AX, AY, HZ0), 0.10, "steel", sides=12))


def helix_flight(name, z0, z1, r_in, r_out, pitch, th, role, steps=28, phase=0.0):
    """Thick helicoid (a screw flight) about the auger axis; vertical thickness th."""
    turns = (z1 - z0) / pitch
    n = max(2, int(round(turns * steps)))
    bm = bmesh.new()
    secs = []
    for i in range(n + 1):
        t = i / n
        z = z0 + (z1 - z0) * t
        a = phase + 2 * math.pi * turns * t
        c, s = math.cos(a), math.sin(a)
        secs.append([bm.verts.new((AX + r * c, AY + r * s, z + dz))
                     for r, dz in ((r_in, -th / 2), (r_out, -th / 2), (r_out, th / 2),
                                   (r_in, th / 2))])
    for a, b in zip(secs, secs[1:]):
        for k in range(4):
            k1 = (k + 1) % 4
            bm.faces.new((a[k], b[k], b[k1], a[k1]))
    bm.faces.new(secs[0])
    bm.faces.new(list(reversed(secs[-1])))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    return K._obj(name, bm, [role])


add(helix_flight("flight", 0.36, 4.62, 0.09, 0.34, 0.70, 0.045, "steel", steps=26))
# guide yoke round the auger (hull ring, accent lip), arms back to the mast
add(lathe("yoke", [(0.37, 1.34), (0.46, 1.34), (0.46, 1.56), (0.37, 1.56)], (AX, AY, 0.0),
          ["hull"], sides=28))
add(lathe("yoke_lip", [(0.455, 1.40), (0.475, 1.40), (0.475, 1.50), (0.455, 1.50)], (AX, AY, 0.0),
          ["accent"], sides=28))
for sx in (-1, 1):
    xa, xb = sorted((sx * 0.37, sx * 0.48))
    add(K.box("yoke_arm", (xa, AY, 1.36), (xb, MF + 0.03, 1.54), "slate", bevel=0.012))
# ground casing the auger drills through (slate, accent band, dark throat)
add(lathe("casing", [(0.0, 0.0), (0.50, 0.0), (0.50, 0.06), (0.41, 0.08), (0.41, 0.30), (0.31, 0.30),
                     (0.31, 0.25), (0.0, 0.25)], (AX, AY, 0.0), ["slate", "rubber", "accent"],
          sides=24, mis=[0, 0, 0, 2, 2, 1, 1, 1]))
for k in range(6):
    a = 2 * math.pi * (k + 0.5) / 6
    add(K.bolt("cbolt", (AX + 0.46 * math.cos(a), AY + 0.46 * math.sin(a), 0.062), (0, 0, 1),
               r=0.02, h=0.02))

# raking cylinders: an A-frame from the house roof to the mast flanks
for sx in (-1, 1):
    p0 = Vector((sx * 0.95, 0.10, HZ + 0.10))
    p1 = Vector((sx * 0.53, -2.05, 6.40))
    xa, xb = sorted((sx * 0.84, sx * 1.06))
    add(K.box("ram_base", (xa, -0.05, HZ - 0.02), (xb, 0.25, HZ + 0.20), "slate", bevel=0.015, seg=1))
    xa, xb = sorted((sx * (MX1 - 0.01), sx * (MX1 + 0.13)))
    add(K.box("ram_clevis", (xa, -2.20, 6.24), (xb, -1.92, 6.56), "slate", bevel=0.015, seg=1))
    parts += hyd_cylinder("ram", p0, p1, 0.12, 0.065)

role_stats(parts)

# ================================================================== finish
ob = K.finish(parts, NAME)
K.face_camera(ob)                          # mast face -Y -> -X (default camera)
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
