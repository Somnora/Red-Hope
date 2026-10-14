"""Build the WaterPlant from code - the hard-surface remake (2026-10-14).

  blender -b -P scripts/blender/rh_build_waterplant.py -- [--out Martians/gen/hs/WaterPlant]
      [--size 2048] [--preview-only]

The game's Water Plant ("Melts ice into water for the crew, the garden, and the
electrolyzer. Fresh melt restores water quality."). Replaces the image-to-3D
WaterPlant whose tanks-and-pipes identity the director kept on every review but
whose mesh had melted into a pale-blue lump: fused pipes, smeared valves, the
whole hull tinted in its TintFor water blue (Saved/RHLineup/png/
Models2__WaterPlant__v035.png, __v215.png). The DESIGN is kept: the approved
hero reference Martians/gen/batch4/cutout/waterplant.png - TANKS AND PIPES:
big horizontal bone-white tanks with dark blue domed heads on a steel skid,
tied together by thick flanged elbow pipework with handwheel valves, a boxy
pump/heater unit at one end. Only the construction changes: every part is an
exact primitive, so nothing can melt, and the emissive mask is the lit water
in the level gauge and the pump's status strip.

Layout facts:
  - three tanks stacked two-low-one-high (the director's identity brief) on
    slate saddles; the high tank rides tapered slate webs that bite into all
    three shells (the reference's saddle gussets). Tank axes run along X, and
    the PIPE side is built at -Y, then K.face_camera() turns it to -X, the face
    the game's default strategy camera meets (measured 2026-10-13). The default
    view therefore gets the plant broadside - the long tank bodies, the front
    header with its upright handwheels, the elbow drops at the viewer's left
    and the pump at the viewer's right - rather than three tank ends
    foreshortened into a long thin strip.
  - the pipes are the character, so they are THICK (r 0.11 m: ~0.25 m across
    in game) and bend on true circular arcs (bend radius 0.18-0.20 m, inner
    wall radius >= 7 cm). The kit's K.pipe fillets with a quadratic Bezier, whose
    mid-bend radius is only 0.71 x its `bend` - on pipes this fat that pinched
    the inner wall to a crease, so pipe_arc() below keeps the kit's curve-bevel
    construction and swaps in a circular fillet.
  - plumbing: each tank's -X head carries a flanged nozzle, elbows out and drops
    into an end manifold along Y; the manifold rounds the front-left corner into
    the front header that runs the length of the skid into the pump's -X face.
    The low tanks' +X heads couple straight into the pump on short flanged
    spools; the pump's outlet rises off its roof and elbows into the high
    tank's +X head. Four gate valves: the high drop (wheel facing the default
    camera), the manifold and two on the header (wheels flat, so the 54 deg
    camera sees them as rings), plus a small relief valve on the high tank
    (its wheel keeps the stub from reading as a chimney). Every pipe run rests
    on a slate support.
  - palette (art bible 1 + the brief): bone-white tank shells; heads, valve
    bodies and bonnets a dark blue-slate "cap" role (#4A5568 - the reference's
    navy end caps, kept off the accent); DarkSlate skid, saddles, straps, pump;
    pipes and flanges a gunmetal at 0.45 metallic (the kit's 0.75 "pipe" role
    reflects Mars' dark sky and read near-black on the AirFilter).
  - accent (#89C4F9, TintFor WaterPlant) is TRIM: a rim ring at every head
    seam, the handwheels, water colour-code bands on the pipes (the real
    plant convention for "this line carries water"), the level-gauge bezel,
    the status-strip bezel, the H2O letters, the manway ring, the pump roof
    vent frame, the pump end panel rim, one motor band and the housing door
    handle. Ray-cast share of the visible surface: ~8 % from the game camera,
    6-8 % from every lineup yaw (6.9 % of all faces).
  - lit elements use the "glow" role, albedo #7CC8FF = the in-game ice/water
    EmissiveColor (~(0.8, 2.8, 5.0) linear), so they read blue by day too: the
    WATER COLUMN in the level gauge on the front-low tank (a continuous lit bar
    under dark glass = liquid at a level; the gauge is tilted 35 deg up the
    shell so it faces the 54 deg camera almost square-on) and the pump's status
    strip.
  - proportions: ~5.4 m long, ~3.2 m deep, ~3.1 m tall at build scale. The
    visualizer fits the larger horizontal extent (the length) to the 3-cell
    footprint (6 m), ~1.11x, and seats the bounds bottom on the ground.
  - every decal, light, band and bolt stands >= 3 mm proud of its support;
    bands and rims share their tank's facet count and phase so their offset is
    uniform round the shell.
Real-world metres, ground-centred pivot (bounds centred on XY, bottom at z=0).

Round 1 -> 2 (self-review, same day): 21.1k tris (40-sided tanks, closed
rings, double-segment bevels everywhere) cut to ~15.6k (32-sided tanks, bands
built outer-only with explicit winding - their inner face is buried in the
shell -, single-segment bevels on flat plates, top-only sunk bolts); the
0.45-metallic #737880 pipes merged with the slate skid, now #8A9097; pipes
r 0.10 -> 0.11; head rims 8 -> 11 cm; accent was 5.8 % at the game camera and
3.5 % from the back three-quarter, now the pump roof, ends and housing back
carry trim too; header supports ended flush with the C-rail flange underside
(coplanar) and now start inside it.
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
OUT = argv[argv.index("--out") + 1] if "--out" in argv else "Martians/gen/hs/WaterPlant"
SIZE = int(argv[argv.index("--size") + 1]) if "--size" in argv else 2048
PREVIEW_ONLY = "--preview-only" in argv
NAME = "WaterPlant"

K = rh_hs.Kit(accent_hex="#89C4F9")       # TintFor(WaterPlant), art bible 2.3
# Lit role: water-blue glow. Albedo matches the in-game ice/water EmissiveColor
# hue so the gauge still reads blue in daylight; preview emission = that colour
# normalised, low enough that review frames show blue instead of clipping white.
K.roles["glow"] = dict(albedo="#7CC8FF", metal=0.0, rough=0.25, emit=1, dust=0.0)
# Tank heads, valve bodies: the reference's dark navy end caps (a bluer, darker
# DarkSlate - body colour, not accent).
K.roles["cap"] = dict(albedo="#4A5568", metal=0.30, rough=0.50, paint=1, dust=1.0)
# Pipes and flanges: gunmetal, but only half metallic (see docstring).
K.roles["gunmetal"] = dict(albedo="#8A9097", metal=0.40, rough=0.42, dust=0.7)
for _r in ("glow", "cap", "gunmetal"):
    K.mats[_r] = K._preview_material(_r, K.roles[_r])
_gb = K.mats["glow"].node_tree.nodes["Principled BSDF"]
_gb.inputs["Emission Color"].default_value = (0.16, 0.56, 1.0, 1.0)
_gb.inputs["Emission Strength"].default_value = 1.2

parts = []
add = parts.append


# ------------------------------------------------------------ local helpers
def xform(objs, M):
    for o in objs:
        o.data.transform(M)
    return objs


def z_to(axis):
    """4x4 rotation taking local +Z onto `axis`."""
    return Vector((0, 0, 1)).rotation_difference(Vector(axis).normalized()).to_matrix().to_4x4()


def revolve(name, prof, roles, sides, mis=None, loop=True, phase=0.0, recalc=True):
    """Revolve an (r, z) profile about local Z. loop=True closes the profile
    (rings, tori); loop=False with both ends on the axis (r = 0 -> pole) is a
    closed solid too. recalc=False keeps the construction winding, which faces
    OUT for a profile walked with the material on its right (radius up, axial
    z to the right) - used for outer-only bands whose inner face is buried."""
    bm = bmesh.new()
    rings = []
    for (r, z) in prof:
        if r < 1e-6:
            v = bm.verts.new((0.0, 0.0, z))
            rings.append([v] * sides)
        else:
            rings.append([bm.verts.new((r * math.cos(phase + 2 * math.pi * s / sides),
                                        r * math.sin(phase + 2 * math.pi * s / sides), z))
                          for s in range(sides)])
    n = len(prof)
    edges = [(i, i + 1) for i in range(n - 1)] + ([(n - 1, 0)] if loop else [])
    for k, (i, j) in enumerate(edges):
        for s in range(sides):
            t = (s + 1) % sides
            vs = []
            for v in (rings[i][s], rings[i][t], rings[j][t], rings[j][s]):
                if v not in vs:
                    vs.append(v)
            if len(vs) < 3:
                continue
            f = bm.faces.new(vs)
            f.material_index = mis[k] if mis else 0
    if recalc:
        bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    return K._obj(name, bm, roles)


ROT_X = Matrix.Rotation(math.radians(90.0), 4, "Y")    # local Z (axial) -> world +X


def along_x(ob, yc, zc):
    """Carry a part revolved about local Z onto an X-parallel axis at (yc, zc).
    Tanks and their bands go through the same matrix, so with the same side
    count their facets stay parallel (uniform proud offset)."""
    ob.data.transform(Matrix.Translation((0.0, yc, zc)) @ ROT_X)
    return ob


def band_x(name, x0, x1, r_in, r_out, yc, zc, role, sides):
    """Outer-only ring round an X-parallel axis, x0..x1, radii r_in..r_out: two
    side walls and the outer face; the inner face would be buried in the shell."""
    return along_x(revolve(name, [(r_in, x0), (r_out, x0), (r_out, x1), (r_in, x1)], [role],
                           sides, loop=False, recalc=False), yc, zc)


def torus(name, c, axis, R, r, role, major=18, minor=6):
    prof = [(R + r * math.cos(2 * math.pi * k / minor), r * math.sin(2 * math.pi * k / minor))
            for k in range(minor)]
    ob = revolve(name, prof, [role], major)
    ob.data.transform(Matrix.Translation(Vector(c)) @ z_to(axis))
    return ob


def bolt(name, at, normal, r=0.016, h=0.014):
    """Hex bolt head standing h proud of `at` along `normal`, its base sunk 2 mm
    into the support and left open (no buried bottom cap: no coincident faces,
    20 % fewer triangles than K.bolt)."""
    n = Vector(normal).normalized()
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, cap_tris=False, segments=6, radius1=r, radius2=r,
                          depth=h + 0.002)
    bm.normal_update()
    bmesh.ops.delete(bm, geom=[f for f in bm.faces if f.normal.z < -0.9], context="FACES_ONLY")
    M = Matrix.Translation(Vector(at) + n * (h - 0.002) / 2) @ z_to(n)
    bmesh.ops.transform(bm, matrix=M, verts=bm.verts)
    return K._obj(name, bm, ["metal"])


def perp(axis):
    a = Vector(axis).normalized()
    u = a.cross(Vector((0, 0, 1)) if abs(a.z) < 0.9 else Vector((1, 0, 0))).normalized()
    return u, a.cross(u).normalized()


def arc_fillet(pts, rb, n):
    """Polyline with every interior corner replaced by a TRUE circular arc of
    radius rb (clamped so a fillet never eats more than half a segment)."""
    out = [pts[0]]
    for i in range(1, len(pts) - 1):
        a, p, b = pts[i - 1], pts[i], pts[i + 1]
        d1, d2 = (p - a).normalized(), (b - p).normalized()
        th = math.acos(max(-1.0, min(1.0, d1.dot(d2))))
        if th < 1e-4:
            out.append(p)
            continue
        r = rb
        tl = r * math.tan(th / 2)
        lim = min((p - a).length, (b - p).length) / 2
        if tl > lim:
            r *= lim / tl
            tl = lim
            rh_hs.log("pipe fillet clamped to r %.3f at %s" % (r, tuple(round(c, 2) for c in p)))
        s = p - d1 * tl
        nrm = (d2 - d1 * d1.dot(d2)).normalized()
        c = s + nrm * r
        for k in range(n + 1):
            ph = th * k / n
            out.append(c + (s - c) * math.cos(ph) + d1 * r * math.sin(ph))
    out.append(pts[-1])
    return out


def pipe_arc(name, pts, r, role="gunmetal", rb=0.18, sides=16, n=6):
    """K.pipe's curve-bevel tube on a circular-arc path (see docstring)."""
    path = arc_fillet([Vector(p) for p in pts], rb, n)
    cu = bpy.data.curves.new(name + "_c", "CURVE")
    cu.dimensions = "3D"
    sp = cu.splines.new("POLY")
    sp.points.add(len(path) - 1)
    for pt, P in zip(sp.points, path):
        pt.co = (P.x, P.y, P.z, 1.0)
    cu.bevel_mode = "ROUND"
    cu.bevel_depth = r
    cu.bevel_resolution = max(0, sides // 4 - 1)
    cu.use_fill_caps = True
    tmp = bpy.data.objects.new(name + "_tmp", cu)
    bpy.context.scene.collection.objects.link(tmp)
    dg = bpy.context.evaluated_depsgraph_get()
    me = bpy.data.meshes.new_from_object(tmp.evaluated_get(dg))
    bpy.data.objects.remove(tmp)
    bpy.data.curves.remove(cu)
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    me.materials.clear()
    me.materials.append(K.mats[role])
    return ob


def flange(name, p, axis, rp, thick=0.04, nb=6, role="gunmetal"):
    """Flange disc p -> p + axis*thick round a pipe of radius rp, with nb hex
    bolts standing 12 mm proud on its outer (p + axis*thick) face."""
    p, ax = Vector(p), Vector(axis).normalized()
    out = [K.cyl(name, p, p + ax * thick, rp + 0.07, role, sides=16)]
    u, v = perp(ax)
    for k in range(nb):
        a = 2 * math.pi * (k + 0.5) / nb
        q = p + ax * thick + (u * math.cos(a) + v * math.sin(a)) * (rp + 0.045)
        out.append(bolt(name + "_b%d" % k, q, ax))
    return out


def valve(name, c, a, s, rp, wheel_r=0.15):
    """Gate valve on a pipe of radius rp: cap-role body and bonnet, gunmetal end
    flanges, alloy stem, ACCENT handwheel (3 spokes) at the stem top.
    c centre, a pipe axis, s stem direction (perpendicular to a)."""
    c, a, s = Vector(c), Vector(a).normalized(), Vector(s).normalized()
    out = [K.cyl(name + "_body", c - a * 0.15, c + a * 0.15, rp + 0.045, "cap", sides=16)]
    for e in (-1, 1):
        out.append(K.cyl(name + "_fl%d" % e, c + a * e * 0.125, c + a * e * 0.18, rp + 0.07,
                         "gunmetal", sides=16))
    out.append(K.cyl(name + "_bonnet", c, c + s * 0.30, 0.08, "cap", sides=12))
    out.append(K.cyl(name + "_bcap", c + s * 0.27, c + s * 0.32, 0.105, "cap", sides=12))
    out.append(K.cyl(name + "_stem", c + s * 0.31, c + s * 0.45, 0.024, "metal", sides=8))
    wc = c + s * 0.43
    out.append(torus(name + "_wheel", wc, s, wheel_r, 0.027, "accent"))
    out.append(K.cyl(name + "_hub", wc - s * 0.03, wc + s * 0.03, 0.045, "metal", sides=10))
    u, v = a, s.cross(a).normalized()
    for k in range(3):
        ang = 2 * math.pi * k / 3 + math.pi / 6
        d = u * math.cos(ang) + v * math.sin(ang)
        out.append(K.cyl(name + "_spoke%d" % k, wc + d * 0.035, wc + d * (wheel_r - 0.008),
                         0.017, "metal", sides=6))
    return out


def pipe_band(name, p0, p1, rp):
    """Water colour-code band (accent) on a straight pipe run, 12 mm proud."""
    return K.cyl(name, p0, p1, rp + 0.012, "accent", sides=16)


def prism_x(name, prof, x0, x1, role, bevel=0.006):
    """Extrude a closed (y, z) profile along X (C-channel skid rails)."""
    bm = bmesh.new()
    a = [bm.verts.new((x0, y, z)) for y, z in prof]
    b = [bm.verts.new((x1, y, z)) for y, z in prof]
    bm.faces.new(a)
    bm.faces.new(list(reversed(b)))
    n = len(prof)
    for i in range(n):
        j = (i + 1) % n
        bm.faces.new((a[i], a[j], b[j], b[i]))
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    if bevel > 0:
        bmesh.ops.bevel(bm, geom=list(bm.edges), offset=bevel, offset_type="OFFSET",
                        segments=1, profile=0.5, affect="EDGES", clamp_overlap=True)
    return K._obj(name, bm, [role])


def text_block(name, body, centre, size, y_front, y_back, role, offset=0.0, spacing=1.0):
    """Raised lettering on a -Y-facing surface (rh_build_airfilter.py's helper)."""
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


def bezel(name, center, w, h, proud, radius, rim, recess, rim_role, inner_role, facing=-1):
    """K.bezel, refusing rim >= radius - 5 mm (folds / collapses the corners)."""
    assert radius - rim >= 0.005, "%s: bezel rim %.3f must be < radius %.3f - 5 mm" % (
        name, rim, radius)
    return K.bezel(name, center, w, h, proud, radius=radius, rim=rim, recess=recess,
                   rim_role=rim_role, inner_role=inner_role, facing=facing)


def convex_block(name, planes, role, bevel=0.012, seg=2):
    """Closed convex solid = intersection of half-spaces n.p <= d, bevelled
    (rh_build_airfilter.py's helper: one watertight hull, no bevel pinholes)."""
    import itertools
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


def cabinet_planes(x0, x1, y0, y1, z0, z1, ch_v, ch_t):
    """Box with chamfered vertical edges (ch_v) and chamfered top edges (ch_t)."""
    S2 = math.sqrt(0.5)
    P = [((1, 0, 0), x1), ((-1, 0, 0), -x0), ((0, 1, 0), y1), ((0, -1, 0), -y0),
         ((0, 0, 1), z1), ((0, 0, -1), -z0)]
    for ex, ey in ((1, 1), (1, -1), (-1, 1), (-1, -1)):
        cx, cy = (x1 if ex > 0 else -x0), (y1 if ey > 0 else -y0)
        P.append(((ex * S2, ey * S2, 0), S2 * (cx + cy - ch_v)))
    for (nx, ny), lim in (((1, 0), x1), ((-1, 0), -x0), ((0, 1), y1), ((0, -1), -y0)):
        P.append(((nx * S2, ny * S2, S2), S2 * (lim + z1 - ch_t)))
    return P


def turn(objs, deg, axis="Z"):
    """Rotate parts built on a -Y-facing virtual face onto a side (Z +-90) or
    the top (X -90: virtual plane y=-h lands on z=h, in-plane z becomes y)."""
    return xform(objs, Matrix.Rotation(math.radians(deg), 4, axis))


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
    """Surface share per role: all faces, and as seen from the pipe side."""
    tot, front = {}, {}
    for o in objs:
        me = o.data
        for p in me.polygons:
            r = me.materials[p.material_index]["rh_role"]
            tot[r] = tot.get(r, 0.0) + p.area
            front[r] = front.get(r, 0.0) + p.area * max(0.0, -p.normal.y)
    for name, d in (("all faces", tot), ("seen from pipe side", front)):
        s = sum(d.values())
        rh_hs.log("role share (%s): %s" % (name, ", ".join(
            "%s %.1f%%" % (k, 100 * v / s) for k, v in sorted(d.items(), key=lambda kv: -kv[1]))))


# ------------------------------------------------------------------ layout
SINK = 0.003                       # every frame/bezel bites 3 mm into its support
R, HD = 0.62, 0.31                 # tank radius, 2:1 elliptical head depth
XS0, XS1 = -2.05, 0.85             # shell (bone-white) extent along X
TIP0, TIP1 = XS0 - HD, XS1 + HD    # head tips
YL, ZL, ZH = 0.68, 0.92, 2.10      # low tanks at y = +-YL, z = ZL; high tank at y=0, z=ZH
TSIDES = 32                        # tank facet count (bands/rims match it)
SADDLE_X = (-1.45, 0.25)           # saddles, straps, webs
PR = 0.11                          # main pipe radius
XM, ZM = -2.58, 0.40               # end manifold (along Y) x, header/manifold height
YH = -1.56                         # front header y (over the front skid rail)
SKX = 2.62                         # skid half length
SKY0, SKY1 = -1.70, 1.42           # skid outer faces (front, back)
CAB_X0, CAB_X1 = 1.42, 2.52        # pump cabinet x range
CAB_Y0, CAB_Y1, CAB_Z1 = -1.68, 0.30, 1.62
HOUS_Y1, HOUS_Z1 = 1.06, 1.25      # rear motor housing (behind the cabinet)
TANKS = (("front", -YL, ZL), ("back", YL, ZL), ("high", 0.0, ZH))

# ------------------------------------------------------------------ skid
# C-channel rails along X (open side out), a box end beam at each end set 1 cm
# inside the rail ends (no coplanar faces), cross members under the saddles and
# the pump, each biting 1 cm into the rails.
for nm, yo, sgn in (("rail_f", SKY0, 1), ("rail_b", SKY1, -1)):
    yi = yo + sgn * 0.20            # inner face
    yw = yi - sgn * 0.035           # web outer face
    prof = [(yi, 0.0), (yi, 0.24), (yo, 0.24), (yo, 0.20), (yw, 0.20), (yw, 0.04),
            (yo, 0.04), (yo, 0.0)]
    add(prism_x(nm, prof, -SKX, SKX, "slate"))
for sx in (-1, 1):
    xa, xb = sorted((sx * (SKX - 0.01), sx * (SKX - 0.18)))
    add(K.box("endbeam", (xa, SKY0 + 0.01, 0.02), (xb, SKY1 - 0.01, 0.22), "slate", bevel=0.01,
              seg=1))
for cx in SADDLE_X + (1.95,):
    add(K.box("cross", (cx - 0.08, SKY0 + 0.19, 0.03), (cx + 0.08, SKY1 - 0.19, 0.21), "slate",
              bevel=0.008, seg=1))
# HazYellow end caps on the four rail ends (closing the C, 6 mm proud)
for sx in (-1, 1):
    for yo, sgn in ((SKY0, 1), (SKY1, -1)):
        xa, xb = sorted((sx * (SKX - 0.004), sx * (SKX + 0.006)))
        ya, yb = sorted((yo - sgn * 0.004, yo + sgn * 0.204))
        add(K.box("railcap", (xa, ya, 0.005), (xb, yb, 0.235), "hazard", bevel=0.003, seg=1))

# ------------------------------------------------------------------ tanks
NH = 5
head0 = [(R * math.sin(math.pi / 2 * k / NH), XS0 - HD * math.cos(math.pi / 2 * k / NH))
         for k in range(NH + 1)]
head1 = [(R * math.sin(math.pi / 2 * k / NH), XS1 + HD * math.cos(math.pi / 2 * k / NH))
         for k in range(NH, -1, -1)]
TPROF = head0 + head1
TMIS = [1] * NH + [0] + [1] * NH                     # heads = cap, shell = hull
for nm, ty, tz in TANKS:
    add(along_x(revolve("tank_" + nm, TPROF, ["hull", "cap"], TSIDES, mis=TMIS, loop=False),
                ty, tz))
    # accent rims where the dark heads meet the white shell (16 mm proud)
    add(band_x("rim0_" + nm, XS0 - 0.005, XS0 + 0.105, R - 0.03, R + 0.016, ty, tz, "accent",
               TSIDES))
    add(band_x("rim1_" + nm, XS1 - 0.105, XS1 + 0.005, R - 0.03, R + 0.016, ty, tz, "accent",
               TSIDES))
    # slate straps at the saddles (22 mm proud)
    for k, sx in enumerate(SADDLE_X):
        add(band_x("strap%d_%s" % (k, nm), sx - 0.06, sx + 0.06, R - 0.03, R + 0.022, ty, tz,
                   "slate", TSIDES))

# saddles under the low tanks: slate blocks whose horns wrap the shell
for sx in SADDLE_X:
    for ty in (-YL, YL):
        add(K.box("saddle", (sx - 0.09, ty - 0.50, 0.20), (sx + 0.09, ty + 0.50, 0.70), "slate",
                  bevel=0.01, seg=1,
                  chamfers=[(lambda d, m: abs(d.x) > 0.99 and m.z > 0.69, 0.10)]))
    # tapered web carrying the high tank, biting into all three shells
    add(K.box("web", (sx - 0.07, -0.55, 1.05), (sx + 0.07, 0.55, 1.80), "slate", bevel=0.01, seg=1,
              chamfers=[(lambda d, m: abs(d.x) > 0.99 and m.z > 1.79, 0.25),
                        (lambda d, m: abs(d.x) > 0.99 and m.z < 1.06, 0.15)]))

# high tank top: manway (slate ring, hull lid, accent band, bolts) + relief valve
MWX, ZTOP = -0.95, ZH + R
add(K.cyl("manway", (MWX, 0, ZTOP - 0.08), (MWX, 0, ZTOP + 0.08), 0.24, "slate", sides=24))
add(K.cyl("manway_acc", (MWX, 0, ZTOP + 0.02), (MWX, 0, ZTOP + 0.06), 0.252, "accent", sides=24))
add(K.cyl("manway_lid", (MWX, 0, ZTOP + 0.07), (MWX, 0, ZTOP + 0.115), 0.17, "hull", sides=24))
for k in range(8):
    a = 2 * math.pi * (k + 0.5) / 8
    add(bolt("mw_bolt", (MWX + 0.208 * math.cos(a), 0.208 * math.sin(a), ZTOP + 0.08),
               (0, 0, 1), r=0.016, h=0.014))
RVX = 0.40
add(K.cyl("relief_base", (RVX, 0, ZTOP - 0.06), (RVX, 0, ZTOP + 0.06), 0.09, "slate", sides=12))
add(K.cyl("relief_pipe", (RVX, 0, ZTOP + 0.05), (RVX, 0, ZTOP + 0.24), 0.05, "gunmetal", sides=12))
add(K.cyl("relief_cap", (RVX, 0, ZTOP + 0.22), (RVX, 0, ZTOP + 0.30), 0.075, "cap", sides=12))
# a small handwheel on the relief valve, so the stub reads as a valve, not a chimney
add(K.cyl("relief_stem", (RVX, 0, ZTOP + 0.29), (RVX, 0, ZTOP + 0.37), 0.022, "metal", sides=8))
add(torus("relief_wheel", (RVX, 0, ZTOP + 0.36), (0, 0, 1), 0.10, 0.022, "accent", major=16))
for k in range(3):
    _a = 2 * math.pi * k / 3 + math.pi / 6
    _d = Vector((math.cos(_a), math.sin(_a), 0.0))
    add(K.cyl("relief_spoke", Vector((RVX, 0, ZTOP + 0.36)) + _d * 0.015,
              Vector((RVX, 0, ZTOP + 0.36)) + _d * 0.094, 0.016, "metal", sides=6))

# ---------------------------------------------------- level gauge (glow)
# On the front-low tank, tilted 35 deg up the shell so it faces the 54 deg
# strategy camera almost square-on. Built on a flat -Y face at y = -R about a
# local tank axis at the origin, then rolled about X and moved onto the tank.
XG, GW, GH = -0.60, 1.36, 0.22
g = []
g.append(K.box("g_pad", (XG - GW / 2 - 0.05, -R - 0.035, -GH / 2 - 0.04),
               (XG + GW / 2 + 0.05, -R + 0.04, GH / 2 + 0.04), "slate", bevel=0.008, seg=1))
gb, gwy = bezel("g_bezel", (XG, -R - 0.035 + SINK, 0.0), GW, GH, 0.03 + SINK, radius=0.06,
                rim=0.035, recess=0.02, rim_role="accent", inner_role="glass")
g.append(gb)
g.append(K.box("g_water", (XG - GW / 2 + 0.07, gwy - 0.006, -GH / 2 + 0.045),
               (XG + GW / 2 - 0.07, gwy + 0.004, 0.012), "glow", bevel=0.003, seg=1))
for k in range(5):                  # level ticks on the dark upper glass
    tx = XG - 0.48 + 0.24 * k
    g.append(K.box("g_tick", (tx - 0.009, gwy - 0.005, 0.030), (tx + 0.009, gwy + 0.004, 0.068),
                   "metal", bevel=0.0))
for e in (-1, 1):                   # pad bolts
    for ez in (-1, 1):
        g.append(bolt("g_bolt", (XG + e * (GW / 2 + 0.025), -R - 0.035, ez * (GH / 2 + 0.012)),
                        (0, -1, 0), r=0.013, h=0.012))
xform(g, Matrix.Translation((0.0, -YL, ZL)) @ Matrix.Rotation(math.radians(-35.0), 4, "X"))
parts += g

# ------------------------------------------------------------------ pipework
# -X heads: flanged nozzle, elbow out to the end manifold, drop into it.
for nm, ty, tz in TANKS:
    add(K.cyl("noz0_" + nm, (TIP0 + 0.06, ty, tz), (TIP0 - 0.01, ty, tz), PR + 0.03, "gunmetal",
              sides=16, caps=False))
    parts += flange("nfl0_" + nm, (TIP0 + 0.005, ty, tz), (-1, 0, 0), PR)
    add(pipe_arc("drop_" + nm, [(TIP0 + 0.20, ty, tz), (XM, ty, tz), (XM, ty, ZM)], PR, rb=0.18))
    # tee collar where the drop meets the manifold
    parts += flange("tee_" + nm, (XM, ty, ZM + PR + 0.03), (0, 0, 1), PR, nb=4)
# end manifold along Y, round the front-left corner into the front header,
# straight into the pump cabinet's -X face
add(pipe_arc("manifold", [(XM, YL + 0.22, ZM), (XM, YH, ZM), (CAB_X0 + 0.10, YH, ZM)], PR,
             rb=0.20))
parts += flange("mf_blind", (XM, YL + 0.20, ZM), (0, 1, 0), PR, thick=0.045, nb=4)
parts += flange("hdr_in", (CAB_X0 + 0.004, YH, ZM), (-1, 0, 0), PR, thick=0.045, nb=4)
# valves: high drop (wheel toward the default camera), manifold, two on the header
parts += valve("vA", (XM, 0.0, 1.40), (0, 0, 1), (0, -1, 0), PR)
parts += valve("vB", (XM, -1.12, ZM), (0, 1, 0), (0, 0, 1), PR)
parts += valve("vC", (-1.05, YH, ZM), (1, 0, 0), (0, 0, 1), PR)
parts += valve("vD", (0.55, YH, ZM), (1, 0, 0), (0, 0, 1), PR)
# water colour-code bands (accent) on straight runs
add(pipe_band("band_hdr0", (-1.62, YH, ZM), (-1.50, YH, ZM), PR))
add(pipe_band("band_hdr1", (0.05, YH, ZM), (0.17, YH, ZM), PR))
add(pipe_band("band_hdr2", (1.20, YH, ZM), (1.32, YH, ZM), PR))
add(pipe_band("band_hi", (XM, 0.0, 1.78), (XM, 0.0, 1.90), PR))
# supports: slate blocks from the skid steel up into each run
for sx in (-1.90, -0.40, 1.00):
    add(K.box("hsup", (sx - 0.07, YH - 0.07, 0.215), (sx + 0.07, YH + 0.07, ZM - 0.02), "slate",
              bevel=0.008, seg=1))
for sy in (-0.34, 0.36):
    add(K.box("msup", (XM + 0.03, sy - 0.07, 0.20), (XM + 0.13, sy + 0.07, ZM - 0.02), "slate",
              bevel=0.008, seg=1))

# +X heads: low tanks couple straight into the pump on short flanged spools
# (the front one into the cabinet, the back one into the motor housing)
for (nm, ty, tz), xface in zip(TANKS[:2], (CAB_X0, CAB_X0 + 0.02)):
    add(K.cyl("noz1_" + nm, (TIP1 - 0.06, ty, tz), (TIP1 + 0.01, ty, tz), PR + 0.03, "gunmetal",
              sides=16, caps=False))
    parts += flange("nfl1_" + nm, (TIP1 - 0.005, ty, tz), (1, 0, 0), PR)
    add(K.cyl("spool_" + nm, (TIP1 - 0.10, ty, tz), (CAB_X0 + 0.10, ty, tz), PR, "gunmetal",
              sides=16))
    parts += flange("sfl_" + nm, (xface + 0.004, ty, tz), (-1, 0, 0), PR, nb=4)
    add(pipe_band("band_sp_" + nm, (TIP1 + 0.08, ty, tz), (TIP1 + 0.17, ty, tz), PR))
# high tank +X head: the pump outlet rises off the cabinet roof and elbows in
add(K.cyl("noz1_high", (TIP1 - 0.06, 0, ZH), (TIP1 + 0.01, 0, ZH), PR + 0.03, "gunmetal", sides=16,
          caps=False))
parts += flange("nfl1_high", (TIP1 - 0.005, 0, ZH), (1, 0, 0), PR)
ROX = 1.97
add(pipe_arc("outlet", [(ROX, 0.0, CAB_Z1 - 0.10), (ROX, 0.0, ZH), (TIP1 - 0.15, 0.0, ZH)], PR,
             rb=0.20))
parts += flange("ofl", (ROX, 0.0, CAB_Z1 - 0.004), (0, 0, 1), PR, nb=4)
add(pipe_band("band_out", (1.50, 0, ZH), (1.62, 0, ZH), PR))

# ------------------------------------------------------------- pump / heater
add(K.box("deck", (CAB_X0 - 0.02, SKY0 + 0.01, 0.20), (SKX - 0.02, SKY1 - 0.01, 0.29), "slate",
          bevel=0.01, seg=1))
add(convex_block("cabinet", cabinet_planes(CAB_X0, CAB_X1, CAB_Y0, CAB_Y1, 0.27, CAB_Z1, 0.06,
                                           0.10), "slate", bevel=0.012))
add(convex_block("housing", cabinet_planes(CAB_X0 + 0.02, CAB_X1 - 0.04, CAB_Y1 - 0.02, HOUS_Y1,
                                           0.27, HOUS_Z1, 0.05, 0.06), "slate", bevel=0.012))
# motor on the housing roof: slate drum, gunmetal cooling rings, alloy end bell
MZ = HOUS_Z1 + 0.20
add(K.box("motor_foot", (1.62, 0.56, HOUS_Z1 - 0.02), (2.32, 0.80, HOUS_Z1 + 0.10), "slate",
          bevel=0.01, seg=1))
add(K.cyl("motor", (1.60, 0.68, MZ), (2.30, 0.68, MZ), 0.22, "slate", sides=20))
for k in range(4):
    mx = 1.72 + 0.16 * k
    add(K.cyl("mring", (mx - 0.025, 0.68, MZ), (mx + 0.025, 0.68, MZ), 0.24,
              "accent" if k == 3 else "gunmetal", sides=16))
add(K.cyl("motor_bell", (2.29, 0.68, MZ), (2.40, 0.68, MZ), 0.17, "metal", sides=20))
# pump front (-Y, faces the default camera after the turn): status strip, H2O
CF = CAB_Y0                         # cabinet front plane
CXC = (CAB_X0 + CAB_X1) / 2
lb, lwy = bezel("led_bezel", (CXC, CF + SINK, 1.36), 0.86, 0.13, 0.025 + SINK, radius=0.045,
                rim=0.022, recess=0.014, rim_role="accent", inner_role="slate")
add(lb)
add(K.box("led", (CXC - 0.38, lwy - 0.010, 1.36 - 0.024), (CXC + 0.38, lwy + 0.004, 1.36 + 0.024),
          "glow", bevel=0.003, seg=1))
PX0, PX1, PZ0, PZ1 = CXC - 0.28, CXC + 0.28, 0.96, 1.20
PLY = CF - 0.016
add(K.box("placard", (PX0, PLY, PZ0), (PX1, CF + 0.004, PZ1), "rubber", bevel=0.008, seg=1))
for bx in (PX0 + 0.022, PX1 - 0.022):
    for bz in (PZ0 + 0.022, PZ1 - 0.022):
        add(bolt("pl_bolt", (bx, PLY, bz), (0, -1, 0), r=0.009))
add(text_block("h2o", "H2O", (CXC, 0.0, (PZ0 + PZ1) / 2), 0.17, PLY - 0.007, PLY + 0.003, "accent",
               offset=0.004, spacing=1.12))
# hull service door on the front, below the placard
add(K.box("door", (CXC - 0.36, CF - 0.02, 0.40), (CXC + 0.36, CF + 0.004, 0.86), "hull",
          bevel=0.01, seg=1))
for bx in (CXC - 0.33, CXC + 0.33):
    for bz in (0.43, 0.83):
        add(bolt("d_bolt", (bx, CF - 0.02, bz), (0, -1, 0), r=0.012))
add(K.box("door_handle", (CXC + 0.18, CF - 0.05, 0.60), (CXC + 0.28, CF - 0.015, 0.66), "metal",
          bevel=0.008))
# heater exhaust grille on the cabinet roof (slats run along X)
top = []
GX0, GX1, GY0, GY1 = 1.56, 2.38, -1.42, -0.42
gfb, gfy = bezel("vent", ((GX0 + GX1) / 2, -CAB_Z1 + SINK, (GY0 + GY1) / 2), GX1 - GX0,
                 GY1 - GY0, 0.03 + SINK, radius=0.05, rim=0.035, recess=0.02, rim_role="accent",
                 inner_role="rubber")
top.append(gfb)
for k in range(7):
    vz = GY0 + 0.08 + (GY1 - GY0 - 0.16) * k / 6
    top.append(K.box("vslat", (GX0 + 0.04, gfy - 0.012, vz - 0.03), (GX1 - 0.04, gfy + 0.004,
                                                                      vz + 0.03), "gunmetal",
                     bevel=0.0))
parts += turn(top, -90.0, "X")
# +X end: framed hull service panel over a louvre
side = []
sp, swy = bezel("svc", (-0.62, -CAB_X1 + SINK, 0.98), 0.90, 0.62, 0.03 + SINK, radius=0.05,
                rim=0.04, recess=0.02, rim_role="accent", inner_role="hull")
side.append(sp)
side.append(K.box("svc_handle", (-0.75, swy - 0.04, 1.14), (-0.49, swy + 0.004, 1.19), "metal",
                  bevel=0.008))
lv, lvy = bezel("louvre", (-0.62, -CAB_X1 + SINK, 0.47), 0.90, 0.26, 0.025 + SINK, radius=0.045,
                rim=0.03, recess=0.016, rim_role="slate", inner_role="rubber")
side.append(lv)
for k in range(4):                 # slats clear the window walls by ~9 mm
    lz = 0.392 + 0.052 * k
    side.append(K.box("lslat", (-1.03, lvy - 0.010, lz - 0.013), (-0.21, lvy + 0.004, lz + 0.013),
                      "gunmetal", bevel=0.0))
parts += turn(side, 90.0)
# housing back (+Y): framed hull service door, built on a virtual -Y face and
# turned 180 deg (virtual x = -world x)
back = []
HCX = (CAB_X0 + 0.02 + CAB_X1 - 0.04) / 2
bd, bwy = bezel("hdoor", (-HCX, -HOUS_Y1 + SINK, 0.72), 0.78, 0.66, 0.03 + SINK, radius=0.05,
                rim=0.04, recess=0.02, rim_role="slate", inner_role="hull")
back.append(bd)
back.append(K.box("hdoor_handle", (-HCX - 0.30, bwy - 0.04, 0.70), (-HCX - 0.22, bwy + 0.004, 0.86),
                  "accent", bevel=0.008))
for bx in (-HCX - 0.34, -HCX + 0.34):
    for bz in (0.43, 1.01):
        back.append(bolt("hd_bolt", (bx, -HOUS_Y1 - 0.03, bz), (0, -1, 0), r=0.012))
parts += turn(back, 180.0)

role_stats(parts)
_tally = {}
for _o in parts:                           # triangle budget by part family
    _k = _o.name.split("_")[0].rstrip("0123456789.")
    _tally[_k] = _tally.get(_k, 0) + sum(len(p.vertices) - 2 for p in _o.data.polygons)
rh_hs.log("tris by part: %s" % ", ".join("%s %d" % kv for kv in
                                         sorted(_tally.items(), key=lambda kv: -kv[1])))

# ------------------------------------------------------------------ finish
ob = K.finish(parts, NAME)
K.face_camera(ob)                          # pipe side -Y -> -X (default camera)
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
