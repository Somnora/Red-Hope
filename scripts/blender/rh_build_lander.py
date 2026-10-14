"""Build the Lander from code - the hard-surface remake (2026-10-14).

  blender -b -P scripts/blender/rh_build_lander.py -- [--out Martians/gen/hs/Lander]
      [--size 2048] [--preview-only]

The game's Lander ("Your first foothold on Mars. Trickles power and relays every
order you give to the fleet."). On the 2026-10-13 triage the director liked the
Lander's clean silhouette ("looks good") but marked it "needs visual polish":
the shipped image-to-3D mesh renders as smeared chrome with melted patches. So
the DESIGN is kept and only made finer. Design source, the approved reference:
Martians/gen/batch4/cutout/lander2.png - a squat bone-white cylindrical capsule
with a rounded shoulder lip, a sloped deck and a panelled dome capped by a
raised hatch ring with grab rails; a flat hatch housing breaking the wall,
carrying a framed side hatch and a red-brown stripe under the lip; panel lines
and rivets; four splayed landing legs (clevis bracket on the wall, thick
telescoping primary strut, secondary struts to the underside, round ribbed foot
pads); engine bells underneath. Added for the game text: small RCS thruster
quads on the flanks and a comm dish plus a whip antenna on the deck ("relays
every order"), all chunky enough to survive the downscale.

Layout facts:
  - built with the hatch at -Y (pol azimuth 0), then K.face_camera() turns it to
    -X, the face the game's default strategy camera meets (measured 2026-10-13).
    The legs stand on the diagonals (azimuth 45 + k*90), so the default view
    shows the hatch framed between the two front legs, and the four bells hang
    between the legs (azimuth k*90).
  - FIT: the visualizer squeezes the Lander's footprint box to 7.8 m x 5 m and
    fits the model's LARGER horizontal extent to the min side, 5 m. The foot
    pads set that extent, so the legs splay modestly (pad centres at r 2.65 m,
    1.33x the capsule radius, the reference's ratio) and, being on the
    diagonals, only reach 0.71x of that along X/Y: the 4.0 m capsule keeps ~85 %
    of the fitted width (~4.25 m in game; model 4.72 x 4.72 x 4.04 m, so the
    game's uniform scale is ~1.06).
  - proportions follow the reference (capsule radius = 1): clearance under the
    body ~0.43, wall ~0.75, deck + dome + hatch ring ~0.6 (round 2 was squatter:
    wall 0.68, dome 0.46). The crew hatch is 0.8 x 1.0 m (round 3's 0.76 x 0.9
    read as a service door beside 1.8 m crew).
  - palette (art bible 1): BoneWhite hull panels over a DarkSlate core, so every
    panel seam is a real 2 cm dark gap (crisp at thumbnail size, nothing to
    smear); light alloy legs and pads, slate brackets and underside, gunmetal
    engine bells. The TintFor accent (#ECECF2) is near-white, so it plays the
    bright TRIM the reference shows: the rounded shoulder lip, the hatch
    housing's cap and the top hatch ring (6 % of all faces, 10-13 % of the
    visible surface from every orbit view and from the 54 deg default camera,
    ray-cast). The dome base ring is light alloy like the reference's slotted
    grey ring (in accent it pushed the 54 deg share to 18 %). Colour trim is the
    kit's RustBeam, exactly as the reference: the band under the lip (wrapping
    the hatch housing as a stripe), the hatch frame and a marker low on the
    door; plus small HazYellow plates on the leg brackets and the RCS quads.
  - legs are real strut geometry, built once at azimuth 0 and swung: a light
    clevis (round 1's slate bracket read as a dark blob) with cheeks and pin,
    rod-end neck, thick sleeve with three collars, piston, knee collar with a V
    of secondary struts to clevis blocks under the heat shield, ball joint, and
    a round alloy pad with a slate rim band and eight radial ribs.
  - comms: dish on the viewer's right shoulder with its bowl turned to the
    front-right, so the default camera looks INTO it (round 2 faced it away),
    and a whip antenna on the left-back with the amber beacon.
  - glow: ONE hue per asset in game (amber, EmissiveColor ~(4.5, 2.2, 0.2)
    linear), so every lit element uses the "glow" role, albedo #FFB040: the hatch
    status bar, the antenna beacon, four landing lights on the skirt and a
    position light on each RCS quad.
  - texture space: faces no camera reaches are never built (panel backs behind
    the slate core, the pads' ground-contact bottoms, the shield top inside the
    capsule - lathe(skip=...)), and repack() re-packs the kit's unwrap with a
    fixed 6 px margin: 24 % -> 34 % of the 2K map used, on ~24 % less surface,
    i.e. ~1.9x the texel area on every visible face.
  - budget ~16.7k tris (hero tier; 4 legs of real struts are ~4k of it). Every
    decal stands >= 4 mm proud; the cap/lip and stripe/band undersides are kept
    >= 4 mm apart (round-3 audit found both coplanar); K.box chamfer + bevel
    slivers on the door leaf are dissolved (clean()).
Real-world metres, ground-centred pivot (bounds centred on XY, bottom at z=0):
capsule body r 2.0 m, body bottom 0.85 m above the pads, top hatch ring ~3.56 m,
antenna beacon ~4.04 m.
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
OUT = argv[argv.index("--out") + 1] if "--out" in argv else "Martians/gen/hs/Lander"
SIZE = int(argv[argv.index("--size") + 1]) if "--size" in argv else 2048
PREVIEW_ONLY = "--preview-only" in argv
NAME = "Lander"

K = rh_hs.Kit(accent_hex="#ECECF2")       # TintFor(Lander), art bible 2.3
# Lit role: amber landing / beacon / status lights. Albedo matches the in-game
# glow hue so the lamps read amber in daylight too; the preview emission is the
# game's EmissiveColor normalised, low enough that review frames stay amber.
K.roles["glow"] = dict(albedo="#FFB040", metal=0.0, rough=0.25, emit=1, dust=0.0)
K.mats["glow"] = K._preview_material("glow", K.roles["glow"])
_gb = K.mats["glow"].node_tree.nodes["Principled BSDF"]
_gb.inputs["Emission Color"].default_value = (1.0, 0.49, 0.045, 1.0)
_gb.inputs["Emission Strength"].default_value = 1.2

parts = []
add = parts.append
SINK = 0.003                                # frames/bezels bite 3 mm into their support


# ------------------------------------------------------------------ helpers
def pol(r, phi, z):
    """Polar -> xyz with phi in degrees measured from the FRONT (-Y), +phi toward +X."""
    a = math.radians(phi)
    return Vector((r * math.sin(a), -r * math.cos(a), z))


def lathe(name, prof, roles, seg=48, phi0=0.0, phi1=None, closed=True, mis=None, phase=0.0,
          skip=()):
    """Revolve an (r, z) profile about Z (CommandModule's lathe). Profiles are
    authored counter-clockwise in the (r, z) half-plane (outer wall going up,
    top going inward) so outer-only rings (closed=False) face out. phi1=None ->
    full ring (r = 0 points become poles); otherwise an arc panel phi0..phi1 with
    both ends capped by the profile polygon. mis: material index per edge.
    skip: profile edges NOT built because no camera can reach them (a panel's
    back behind the slate core, a pad's ground-contact bottom): their texture
    space goes to faces that show. Normals still come out right - the shell is
    open only on its hidden side, so recalc_face_normals keeps it outward."""
    full = phi1 is None
    span = 360.0 if full else (phi1 - phi0)
    start = phase if full else phi0
    nring = seg if full else seg + 1
    bm = bmesh.new()
    rings = []
    for (r, z) in prof:
        if full and r < 1e-6:
            v = bm.verts.new((0.0, 0.0, z))
            rings.append([v] * nring)
        else:
            rings.append([bm.verts.new(tuple(pol(r, start + span * s / seg, z)))
                          for s in range(nring)])
    npt = len(prof)
    edges = [(i, i + 1) for i in range(npt - 1)] + ([(npt - 1, 0)] if closed else [])
    for k, (i, j) in enumerate(edges):
        if k in skip:
            continue
        for s in range(seg):
            s1 = (s + 1) % nring if full else s + 1
            vs = []
            for v in (rings[i][s], rings[i][s1], rings[j][s1], rings[j][s]):
                if v not in vs:
                    vs.append(v)
            if len(vs) < 3:
                continue
            f = bm.faces.new(vs)
            f.material_index = mis[k] if mis else 0
    if not full and closed:
        for s in (0, nring - 1):
            ring = [rings[i][s] for i in range(npt)]
            bm.faces.new(ring if s else list(reversed(ring)))
    if closed:
        bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    return K._obj(name, bm, roles)


def panels(name, prof, role, n, gap, r_gap, phase=0.0, seg_per=6, skip=None, hide=()):
    """n arc panels of one closed profile around Z, `gap` metres of seam (at
    radius r_gap) between them; panel k centred on phase + k*360/n. skip(phi)
    drops panels (e.g. ones buried in the hatch housing). Returns (objs, centres)."""
    out, centres = [], []
    step = 360.0 / n
    half_gap = math.degrees(gap / r_gap) / 2.0
    for k in range(n):
        c = phase + k * step
        cw = ((c + 180.0) % 360.0) - 180.0
        if skip and skip(cw):
            continue
        out.append(lathe("%s%d" % (name, k), prof, [role], seg=seg_per,
                         phi0=c - step / 2 + half_gap, phi1=c + step / 2 - half_gap, skip=hide))
        centres.append((c - step / 2 + half_gap, c + step / 2 - half_gap))
    return out, centres


def rotz(ob, phi):
    """Swing a part about Z by phi degrees (pol convention: +phi turns -Y toward +X)."""
    ob.data.transform(Matrix.Rotation(math.radians(phi), 4, "Z"))
    return ob


def move(ob, d):
    ob.data.transform(Matrix.Translation(Vector(d)))
    return ob


def aim(ob, base, axis):
    """Turn a part built about local +Z to `axis`, then move its origin to `base`."""
    rot = Vector((0, 0, 1)).rotation_difference(Vector(axis).normalized()).to_matrix().to_4x4()
    ob.data.transform(Matrix.Translation(Vector(base)) @ rot)
    return ob


def obox(name, p0, p1, w, d, role, side=(0, 0, 1), bevel=0.006, seg=1):
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


def rod(name, p0, p1, r, role, sides=12):
    return K.cyl(name, p0, p1, r, role, sides=sides)


def bolt(name, at, normal, r=0.014, h=0.012, role="metal"):
    """Hex bolt/rivet whose base sits 4 mm INSIDE the surface (no coplanar base
    cap over a faceted wall), standing ~8 mm proud."""
    n = Vector(normal).normalized()
    p0 = Vector(at) - n * 0.004
    return K.cyl(name, p0, p0 + n * h, r, role, sides=6)


def convex_block(name, planes, role, bevel=0.012, seg=2):
    """Closed convex solid = intersection of half-spaces n.p <= d, bevelled
    (AirFilter's cabinet helper: one watertight hull however many chamfers meet)."""
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


def bezel(name, center, w, h, proud, radius, rim, recess, rim_role, inner_role, facing=-1):
    """K.bezel with its failure modes refused (AirFilter round-1 lesson): keep
    the window corner radius >= 5 mm."""
    assert radius - rim >= 0.005, "%s: bezel rim %.3f must be < radius %.3f - 5 mm" % (
        name, rim, radius)
    return K.bezel(name, center, w, h, proud, radius=radius, rim=rim, recess=recess,
                   rim_role=rim_role, inner_role=inner_role, facing=facing)


def u_rail(name, a, b, height, r, role="metal", up=(0, 0, 1)):
    """Grab rail: two legs rising `height` along `up` from feet a and b (feet sunk
    2 cm into the surface) joined by a bar, filleted corners."""
    a, b, up = Vector(a), Vector(b), Vector(up).normalized()
    pts = [a - up * 0.02, a + up * height, b + up * height, b - up * 0.02]
    return K.pipe(name, pts, r, role, bend=min(0.05, height * 0.4), sides=8, samples=3)


def dish(name, post_top, axis, rad, role="hull"):
    """Parabolic dish (rolled rim, solid shell) on a hinge block, with one chunky
    feed boom and horn (CommandModule's dish)."""
    axis = Vector(axis).normalized()
    depth, thick, n = 0.32 * rad, 0.03, 4
    prof = [(rad * i / n, depth * (i / n) ** 2 - thick) for i in range(n + 1)]
    prof.append((rad + 0.018, depth - thick * 0.3))
    prof += [(rad * i / n, depth * (i / n) ** 2) for i in range(n, -1, -1)]
    centre = Vector(post_top) + axis * 0.08
    out = [aim(lathe(name, prof, [role], seg=20), centre, axis)]
    out.append(K.box(name + "_hinge", Vector(post_top) - Vector((0.06, 0.06, 0.06)),
                     Vector(post_top) + Vector((0.06, 0.06, 0.06)), "slate", bevel=0.01, seg=1))
    out.append(K.cyl(name + "_stub", Vector(post_top), centre + axis * 0.01, 0.045, "pipe",
                     sides=10))
    tip = centre + axis * (depth + 0.55 * rad)
    out.append(K.cyl(name + "_boom", centre, tip, 0.03, "pipe", sides=8))
    out.append(K.cyl(name + "_horn", tip - axis * 0.02, tip + axis * 0.07, 0.055, "slate",
                     sides=10))
    return out


def clean(ob):
    """Collapse the sub-0.1 mm slivers a chamfer + all-edge bevel leaves at the
    chamfered corners of a K.box (round-2 audit: 48 zero-area faces on the door)."""
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    bmesh.ops.dissolve_degenerate(bm, dist=1e-4, edges=bm.edges[:])
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    bm.to_mesh(ob.data)
    bm.free()
    return ob


def uv_cover(ob):
    me = ob.data
    uv = me.uv_layers.active.data
    me.calc_loop_triangles()
    a = 0.0
    for lt in me.loop_triangles:
        p = [uv[i].uv for i in lt.loops]
        a += abs((p[1].x - p[0].x) * (p[2].y - p[0].y) - (p[2].x - p[0].x) * (p[1].y - p[0].y)) / 2
    return 100.0 * a


def repack(ob, margin=0.003):
    """Re-pack the kit's unwrap with a FIXED-fraction margin (0.003 = 6 px at
    2048, the padding the kit means). The kit's default SCALED margin, spread
    over ~2,000 tiny islands (rivets, ribs, struts), left the Lander using 24 %
    of its texture; this takes it to ~36 %."""
    before = uv_cover(ob)
    bpy.ops.object.select_all(action="DESELECT")
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.select_all(action="SELECT")
    bpy.ops.uv.pack_islands(rotate=True, margin=margin, margin_method="FRACTION",
                            shape_method="CONCAVE")
    bpy.ops.object.mode_set(mode="OBJECT")
    rh_hs.log("UV re-pack: %.1f %% -> %.1f %% of the texture used" % (before, uv_cover(ob)))


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
    tot = {}
    for o in objs:
        me = o.data
        for p in me.polygons:
            r = me.materials[p.material_index]["rh_role"]
            tot[r] = tot.get(r, 0.0) + p.area
    s = sum(tot.values())
    rh_hs.log("role share (all faces): %s" % ", ".join(
        "%s %.1f%%" % (k, 100 * v / s) for k, v in sorted(tot.items(), key=lambda kv: -kv[1])))


def tri_stats(objs, top=60):
    """Triangles per part family (name up to the first digit/dot) - the budget log."""
    fam = {}
    for o in objs:
        key = o.name.split(".")[0].rstrip("0123456789-_")
        fam[key] = fam.get(key, 0) + sum(len(p.vertices) - 2 for p in o.data.polygons)
    rh_hs.log("tris by family: %s" % ", ".join(
        "%s %d" % kv for kv in sorted(fam.items(), key=lambda kv: -kv[1])[:top]))


# ------------------------------------------------------------------ layout
# Heights follow the reference's proportions (capsule radius RC = 1): ground
# clearance under the body ~0.43, wall ~0.75, deck + dome + hatch ring ~0.6.
RC = 2.0                                   # capsule wall outer radius
R_CORE = 1.975                             # slate core behind the wall panels
ZB = 0.85                                  # body underside (heat shield)
Z_SK = 1.07                                # skirt top / wall start
LOW = (1.09, 1.64)                         # lower panel tier
UPP = (1.665, 2.20)                        # upper panel tier
BAND = (2.22, 2.36)                        # RustBeam band under the lip
LIP_Z0 = BAND[1] - 0.009                   # rounded shoulder lip (bites 9 mm over the band)
LIP_Z1 = LIP_Z0 + 0.204
DECK_A, DECK_B = (1.955, LIP_Z1 + 0.005), (1.46, LIP_Z1 + 0.17)   # sloped deck line (r, z)
RING_Z0 = DECK_B[1] - 0.035                # dome base ring bottom
DOME_ZC, DOME_A, DOME_B = RING_Z0 + 0.135, 1.36, 0.76   # dome ellipse: centre z, radial, vertical
HOUSE_HW, HOUSE_Y = 0.58, -2.10            # hatch housing half width, front plane
HOUSE_PHI = math.degrees(math.asin(HOUSE_HW / RC)) + 1.0   # azimuth it covers
LEG_PHI = (45.0, 135.0, 225.0, 315.0)
R_FOOT = 2.65                              # foot pad centre radius
P_TOP = (2.22, 1.72)                       # primary strut pin (r, z)
P_FOOT = (R_FOOT, 0.245)                   # foot ball (r, z)
S2 = math.sqrt(0.5)


def under_housing(phi, pad=0.0):
    return abs(phi) < HOUSE_PHI + pad


# ================================================================ underside
# heat shield, skirt, engine mount and four bells between the legs
add(lathe("shield", [(0.0, ZB - 0.02), (1.80, ZB - 0.02), (1.82, ZB), (1.82, ZB + 0.025),
                     (0.0, ZB + 0.025)], ["slate"], seg=40, skip=(3,)))   # top: inside
add(lathe("skirt", [(1.76, ZB - 0.005), (1.81, ZB - 0.005), (RC + 0.005, Z_SK - 0.02),
                    (RC + 0.005, Z_SK), (R_CORE - 0.01, Z_SK), (1.76, ZB + 0.03)], ["hull"],
          seg=40))
add(lathe("mount", [(0.0, 0.66), (1.20, 0.66), (1.24, 0.68), (1.46, ZB - 0.01), (1.46, ZB),
                    (0.0, ZB)], ["slate"], seg=32, skip=(4,)))       # top: in the shield
# bell: gunmetal skirt, slate exit band, sooty (rubber) throat - one lathe
BELL = [(0.115, 0.71), (0.17, 0.55), (0.296, 0.355), (0.318, 0.35), (0.318, 0.31), (0.28, 0.31),
        (0.17, 0.50), (0.095, 0.71)]
for k in range(4):
    phi = k * 90.0
    b = lathe("bell%d" % k, BELL, ["pipe", "slate", "rubber"], seg=12,
              mis=[0, 0, 1, 1, 1, 2, 2, 2])
    add(move(b, pol(0.98, phi, 0.0)))
    collar = lathe("bellcollar%d" % k, [(0.10, 0.60), (0.165, 0.60), (0.165, 0.70),
                                        (0.10, 0.70)], ["slate"], seg=10)
    add(move(collar, pol(0.98, phi, 0.0)))
# landing lights on the skirt chamfer, aimed out and down
SK_N = Vector((0.724, -0.69))              # skirt surface normal (r, z), outward-down
for phi in (32.0, -32.0, 148.0, -148.0):
    base = pol(1.905, phi, 0.958)          # 9 mm inside the skirt surface
    out = pol(1.0, phi, 0.0)
    nrm = (Vector((out.x, out.y, 0.0)) * SK_N.x + Vector((0, 0, SK_N.y))).normalized()
    add(K.cyl("lamp_house", base - nrm * 0.02, base + nrm * 0.05, 0.075, "slate", sides=8))
    add(K.cyl("lamp", base + nrm * 0.045, base + nrm * 0.062, 0.052, "glow", sides=8))

# ===================================================================== wall
add(lathe("wall_core", [(R_CORE, Z_SK - 0.02), (R_CORE, LIP_Z0 + 0.02)], ["slate"], seg=48,
          closed=False))
WALL_PROF = lambda z0, z1: [(R_CORE - 0.012, z0), (RC - 0.012, z0), (RC, z0 + 0.012),
                            (RC, z1 - 0.012), (RC - 0.012, z1), (R_CORE - 0.012, z1)]
# (the panels the housing half-covers stay: their outer slivers close the wall)
# panel backs (profile edge 5) sit behind the core: never built
low, low_arcs = panels("wlow", WALL_PROF(*LOW), "hull", 10, 0.022, RC, phase=18.0, seg_per=4,
                       hide=(5,))
upp, upp_arcs = panels("wupp", WALL_PROF(*UPP), "hull", 10, 0.022, RC, phase=0.0, seg_per=4,
                       hide=(5,))
parts += low + upp
# rivets on the panel corners the housing and the leg brackets leave clear: the
# top row of the upper tier and the bottom row of the lower (tier seam clean)
for arcs, rows in ((low_arcs, (LOW[0] + 0.04,)), (upp_arcs, (UPP[1] - 0.04,))):
    for (a0, a1) in arcs:
        for phi in (a0 + 1.4, a1 - 1.4):
            pw = ((phi + 180.0) % 360.0) - 180.0
            if under_housing(pw, 1.5) or any(abs(((pw - lp + 180) % 360) - 180) < 7.5
                                              for lp in LEG_PHI):
                continue
            for z in rows:
                add(bolt("rivet", pol(RC, phi, z), pol(1.0, phi, 0.0), r=0.016))
# RustBeam band under the lip (the housing carries it on as its own stripe)
add(lathe("band", [(R_CORE - 0.01, BAND[0]), (RC + 0.016, BAND[0]), (RC + 0.016, BAND[1]),
                   (R_CORE - 0.01, BAND[1])], ["rust"], seg=48, skip=(3,)))   # back: behind core
# rounded shoulder lip (accent: the bright rim of the reference)
add(lathe("lip", [(1.93, LIP_Z0), (2.035, LIP_Z0), (2.07, LIP_Z0 + 0.04), (2.078, LIP_Z0 + 0.094),
                  (2.065, LIP_Z0 + 0.149), (2.03, LIP_Z0 + 0.189), (1.97, LIP_Z1),
                  (1.93, LIP_Z1)], ["accent"], seg=48))

# ===================================================================== deck
A, B = Vector(DECK_A), Vector(DECK_B)
T = (B - A).normalized()
N = Vector((-T.y, T.x))                    # outward-up normal of the deck line
if N.y < 0:
    N = -N
N3 = lambda phi: N.x * pol(1.0, phi, 0.0) + Vector((0.0, 0.0, N.y))


def deck_prof(a=0.0, b=1.0, th=0.022, ch=0.012, bite=0.012):
    """Closed profile of a panel lying on the deck line between fractions a..b."""
    p, q = A.lerp(B, a), A.lerp(B, b)
    pts = [p - N * bite, p + N * (th - ch), p + N * th + T * ch, q + N * th - T * ch,
           q + N * (th - ch), q - N * bite]
    return [(v.x, v.y) for v in pts]


def deck_z(r):
    """Height of the deck panels' outer surface at radius r."""
    f = (r - A.x) / (B.x - A.x)
    return A.y + f * (B.y - A.y) + 0.022 / N.y


_c0, _c1 = A - N * 0.02 - T * 0.03, B - N * 0.02 + T * 0.02
add(lathe("deck_core", [(_c0.x, _c0.y), (_c1.x, _c1.y)], ["slate"], seg=48, closed=False))
deck, deck_arcs = panels("deck", deck_prof(), "hull", 12, 0.022, 1.7, phase=0.0, seg_per=4,
                         hide=(5,))
parts += deck
for (a0, a1) in deck_arcs:                 # rivets near the outer corners of each deck panel
    for phi in (a0 + 1.5, a1 - 1.5):
        p = A.lerp(B, 0.14) + N * 0.022
        add(bolt("drivet", pol(p.x, phi, p.y), N3(phi), r=0.016))
# deck access panel with a grab handle over the hatch (reference: top of the housing)
add(lathe("deck_hatch", deck_prof(0.22, 0.62, th=0.045, ch=0.015, bite=0.004), ["hull"], seg=4,
          phi0=-8.0, phi1=8.0, skip=(5,)))
h0 = A.lerp(B, 0.36) + N * 0.045
add(u_rail("deck_handle", pol(h0.x, -4.0, h0.y), pol(h0.x, 4.0, h0.y), 0.07, 0.025, up=N3(0.0)))

# ===================================================================== dome
def E(t, off=0.0):
    t = math.radians(t)
    return ((DOME_A + off) * math.cos(t), DOME_ZC + (DOME_B + off) * math.sin(t))


add(lathe("dome_core", [E(t, -0.018) for t in list(range(-6, 60, 8)) + [62]], ["slate"],
          seg=24, closed=False))


def dome_prof(t0, t1, th=0.024, n=4):
    pts = [E(t0, -0.014), E(t0, th - 0.010), E(t0 + 0.8, th)]
    pts += [E(t0 + 0.8 + (t1 - t0 - 1.6) * i / n, th) for i in range(1, n)]
    pts += [E(t1 - 0.8, th), E(t1, th - 0.010), E(t1, -0.014)]
    return pts                             # inner chord sags into the hollow: hidden


DLP, DUP = dome_prof(3.0, 31.5, n=3), dome_prof(33.5, 58.0, n=2)
dl, _ = panels("dome_lo", DLP, "hull", 10, 0.022, 1.25, phase=18.0, seg_per=4,
               hide=(len(DLP) - 1,))      # inner chord: behind the core
du, _ = panels("dome_up", DUP, "hull", 10, 0.020, 0.95, phase=0.0, seg_per=3,
               hide=(len(DUP) - 1,))
parts += dl + du
# dome base ring (light alloy, as the reference's grey slotted ring - in accent
# it pushed the bright trim to 18 % of the 54 deg view) with slate slots, top
# hatch ring (accent), lid
R0 = RING_Z0
add(lathe("dome_ring", [(1.30, R0), (1.475, R0), (1.50, R0 + 0.02), (1.50, R0 + 0.135),
                        (1.48, R0 + 0.157), (1.30, R0 + 0.157)], ["metal"], seg=48))
for k in range(12):
    phi = 15.0 + k * 30.0
    add(rotz(K.box("ring_slot", (-0.06, -1.508, R0 + 0.06), (0.06, -1.485, R0 + 0.095), "slate",
                   bevel=0.0), phi))
TOP_Z = DOME_ZC + DOME_B * math.sin(math.acos(0.72 / DOME_A))
add(lathe("top_ring", [(0.56, TOP_Z - 0.06), (0.74, TOP_Z - 0.06), (0.765, TOP_Z - 0.035),
                       (0.765, TOP_Z + 0.06), (0.745, TOP_Z + 0.085), (0.56, TOP_Z + 0.085)],
          ["accent"], seg=40))
add(lathe("lid", [(0.0, TOP_Z), (0.57, TOP_Z), (0.57, TOP_Z + 0.06), (0.55, TOP_Z + 0.07),
                  (0.0, TOP_Z + 0.07)], ["hull"], seg=32, skip=(0,)))     # underside: inside
add(lathe("lid_seam", [(0.30, TOP_Z + 0.06), (0.32, TOP_Z + 0.06), (0.32, TOP_Z + 0.078),
                       (0.30, TOP_Z + 0.078)], ["slate"], seg=16))
add(lathe("lid_boss", [(0.0, TOP_Z + 0.06), (0.17, TOP_Z + 0.06), (0.17, TOP_Z + 0.09),
                       (0.15, TOP_Z + 0.105), (0.0, TOP_Z + 0.105)], ["metal"], seg=16))
add(u_rail("lid_handle", (-0.14, -0.40, TOP_Z + 0.07), (0.14, -0.40, TOP_Z + 0.07), 0.06, 0.025))
# four grab rails round the hatch ring (the reference's U handles)
for k in range(4):
    phi = 45.0 + k * 90.0
    rr = 0.86
    zz = DOME_ZC + (DOME_B + 0.024) * math.sin(math.acos(rr / (DOME_A + 0.024)))
    add(u_rail("rail%d" % k, pol(rr, phi - 7.0, zz), pol(rr, phi + 7.0, zz), 0.10, 0.026))

# =========================================================== hatch housing
CH = 0.09                                  # housing vertical-edge chamfer
HZ0 = 0.93                                 # housing bottom
# flat front at HOUSE_Y, chamfered vertical front edges and a 10 cm bottom-front
# chamfer (the reference's clipped lower corners); back buried in the wall, top
# buried in the cap
hp = [((1, 0, 0), HOUSE_HW), ((-1, 0, 0), HOUSE_HW), ((0, -1, 0), -HOUSE_Y), ((0, 1, 0), -1.75),
      ((0, 0, 1), LIP_Z0 + 0.02), ((0, 0, -1), -HZ0),
      ((S2, -S2, 0), S2 * (HOUSE_HW - HOUSE_Y - CH)), ((-S2, -S2, 0), S2 * (HOUSE_HW - HOUSE_Y - CH)),
      ((0, -S2, -S2), S2 * (-HOUSE_Y - HZ0 - 0.10))]
add(convex_block("housing", hp, "hull", bevel=0.012))
# rust stripe wrapping the housing front + chamfers (continues the band)
sp = [((1, 0, 0), HOUSE_HW + 0.006), ((-1, 0, 0), HOUSE_HW + 0.006),
      ((0, -1, 0), -HOUSE_Y + 0.006), ((0, 1, 0), -1.80),
      ((0, 0, 1), BAND[1] - 0.02), ((0, 0, -1), -(BAND[0] - 0.004)),
      ((S2, -S2, 0), S2 * (HOUSE_HW - HOUSE_Y + 0.012 - CH)),
      ((-S2, -S2, 0), S2 * (HOUSE_HW - HOUSE_Y + 0.012 - CH))]
add(convex_block("stripe", sp, "rust", bevel=0.004, seg=1))
# housing cap: the shoulder lip carried across the housing (accent)
cp = [((1, 0, 0), HOUSE_HW + 0.02), ((-1, 0, 0), HOUSE_HW + 0.02),
      ((0, -1, 0), -HOUSE_Y + 0.025), ((0, 1, 0), -1.85),
      ((0, 0, 1), LIP_Z1 - 0.008), ((0, 0, -1), -(LIP_Z0 - 0.006)),   # underside 6 mm below
                                                                     # the lip's (z-fight)
      ((S2, -S2, 0), S2 * (HOUSE_HW - HOUSE_Y + 0.045 - CH - 0.01)),
      ((-S2, -S2, 0), S2 * (HOUSE_HW - HOUSE_Y + 0.045 - CH - 0.01)),
      ((0, -S2, S2), S2 * (-HOUSE_Y + 0.025 + LIP_Z1 - 0.008 - 0.07))]
add(convex_block("house_cap", cp, "accent", bevel=0.012))

# the hatch: RustBeam frame, recessed bone-white door, small window, handles.
# 0.8 x 1.0 m (round 3: 0.76 x 0.9 read as a service door beside 1.8 m crew)
HZ, HW_, HH = 1.58, 0.80, 1.00
frame, wy = bezel("hatch_frame", (0.0, HOUSE_Y + SINK, HZ), HW_, HH, 0.05 + SINK, radius=0.12,
                  rim=0.07, recess=0.032, rim_role="rust", inner_role="hull")
add(frame)
door_front = wy - 0.016
DW2, DH2 = HW_ / 2 - 0.09, HH / 2 - 0.08    # door leaf half extents (2 cm / 1 cm clear of the rim)
add(clean(K.box("door_leaf", (-DW2, door_front, HZ - DH2), (DW2, wy + 0.004, HZ + DH2), "hull",
                bevel=0.01, chamfers=[(lambda d, m: abs(d.y) > 0.99, 0.05)])))
dwin, _ = bezel("door_win", (0.0, door_front + SINK, HZ + 0.23), 0.30, 0.17, 0.022 + SINK,
                radius=0.05, rim=0.03, recess=0.012, rim_role="slate", inner_role="glass")
add(dwin)
add(K.box("door_latch", (0.12, door_front - 0.035, HZ - 0.17), (0.20, door_front + 0.004,
                                                                 HZ + 0.02), "metal",
          bevel=0.008))
add(K.box("hatch_mark", (-0.10, door_front - 0.008, HZ - DH2 + 0.05),
          (0.10, door_front + 0.004, HZ - DH2 + 0.085), "rust",
          bevel=0.003, seg=1))              # the reference's red marker low on the door
for s in (-1, 1):                          # grab handles beside the frame
    x = s * 0.44
    add(u_rail("hatch_rail%d" % s, (x, HOUSE_Y, HZ - 0.30), (x, HOUSE_Y, HZ + 0.30), 0.07, 0.026,
               up=(0, -1, 0)))
# amber status bar over the hatch in a slate surround
SZ = HZ + HH / 2 + 0.07
add(K.box("status_house", (-0.19, HOUSE_Y - 0.014, SZ - 0.045), (0.19, HOUSE_Y + 0.004, SZ + 0.045),
          "slate", bevel=0.006, seg=1))
add(K.box("status", (-0.15, HOUSE_Y - 0.022, SZ - 0.024), (0.15, HOUSE_Y - 0.010, SZ + 0.024),
          "glow", bevel=0.003, seg=1))

# ==================================================================== legs
# Built for a leg pointing -Y (azimuth 0), then swung to the diagonals.
def leg(phi):
    out = []
    o = out.append
    pz = P_TOP[1]
    # light clevis like the reference (round 1's slate bracket read as a dark blob)
    o(K.box("lmount", (-0.20, -(RC + 0.07), pz - 0.26), (0.20, -(R_CORE - 0.01), pz + 0.34), "hull",
            bevel=0.012, seg=1))
    o(K.box("lhaz", (-0.09, -(RC + 0.078), pz + 0.235), (0.09, -(RC + 0.066), pz + 0.285),
            "hazard", bevel=0.0))
    for s in (-1, 1):
        xa, xb = sorted((s * 0.075, s * 0.125))
        o(K.box("lcheek", (xa, -(P_TOP[0] + 0.11), pz - 0.14), (xb, -(RC + 0.06), pz + 0.16),
                "metal", bevel=0.0,
                chamfers=[(lambda d, m: abs(d.x) > 0.99 and m.z < pz - 0.12 and m.y < -2.2,
                           0.08)]))
        o(K.cyl("lpinhead", (s * 0.12, -P_TOP[0], pz), (s * 0.15, -P_TOP[0], pz), 0.05, "slate",
                sides=6))
    o(K.cyl("lpin", (-0.13, -P_TOP[0], pz), (0.13, -P_TOP[0], pz), 0.034, "slate", sides=8))
    top = Vector((0.0, -P_TOP[0], pz))
    foot = Vector((0.0, -P_FOOT[0], P_FOOT[1]))
    d = (foot - top).normalized()
    L = (foot - top).length
    o(K.cyl("leye", (-0.07, -P_TOP[0], pz), (0.07, -P_TOP[0], pz), 0.075, "metal", sides=12))
    # primary strut: rod-end neck out of the clevis, thick sleeve with collars
    # (starting below the cheeks), slimmer piston down to the foot ball
    s0, s1 = top + d * 0.17, top + d * (0.56 * L)
    o(rod("lneck", top, s0 + d * 0.02, 0.05, "metal", sides=8))
    o(rod("lsleeve", s0, s1, 0.098, "metal", sides=12))
    for f in (0.135, 0.34, 0.545):
        c = top + d * (f * L)
        o(rod("lcollar", c - d * 0.03, c + d * 0.03, 0.118, "slate", sides=10))
    o(rod("lpiston", s1 - d * 0.04, foot - d * 0.05, 0.064, "metal", sides=10))
    # foot: ball joint on a ribbed round pad
    o(lathe("lball", [(0.0, -0.085)] + [(0.085 * math.cos(math.radians(a)),
                                          0.085 * math.sin(math.radians(a)))
                                         for a in (-60, -30, 0, 30, 60)] + [(0.0, 0.085)],
            ["metal"], seg=8))
    out[-1].data.transform(Matrix.Translation(foot))
    # pad: alloy dish, slate rim band (defines the disc from the 54 deg camera)
    pad = lathe("lpad", [(0.0, 0.0), (0.49, 0.0), (0.49, 0.085), (0.47, 0.098), (0.40, 0.098),
                         (0.16, 0.14), (0.16, 0.215), (0.0, 0.215)], ["metal", "slate"], seg=20,
                mis=[0, 1, 1, 1, 0, 0, 0, 0], skip=(0,))    # bottom: on the ground
    o(move(pad, (0.0, -R_FOOT, 0.0)))
    for j in range(8):
        a = math.radians(22.5 + j * 45.0)
        u = Vector((math.cos(a), math.sin(a), 0.0))
        c = Vector((0.0, -R_FOOT, 0.0))
        p0 = c + u * 0.14 + Vector((0, 0, 0.155))
        p1 = c + u * 0.43 + Vector((0, 0, 0.112))
        o(obox("lrib", p0, p1, 0.04, 0.055, "metal", side=(-u.y, u.x, 0.0), bevel=0.0))
    # secondary struts: knee collar on the primary -> clevis blocks under the shield
    knee = foot - d * (0.24 * L)
    o(rod("lknee", knee - d * 0.06, knee + d * 0.06, 0.09, "slate", sides=10))
    for s in (-1, 1):
        anc = pol(1.60, s * 30.0, ZB - 0.035)
        o(K.box("lclevis", anc - Vector((0.07, 0.07, 0.05)), anc + Vector((0.07, 0.07, 0.04)),
                "slate", bevel=0.0))
        o(rod("lstrut", knee, anc, 0.046, "metal", sides=8))
    for ob in out:
        rotz(ob, phi)
    return out


for phi in LEG_PHI:
    parts += leg(phi)

# ======================================================= RCS thruster quads
def rcs(phi):
    out = []
    o = out.append
    z0, z1 = UPP[1] - 0.38, UPP[1] - 0.12
    o(K.box("rcs_body", (-0.14, -(RC + 0.10), z0), (0.14, -(R_CORE - 0.01), z1), "slate",
            bevel=0.014, seg=1))
    o(K.box("rcs_haz", (-0.10, -(RC + 0.106), z1 - 0.045), (0.10, -(RC + 0.094), z1 - 0.02),
            "hazard", bevel=0.0))
    o(K.cyl("rcs_light", (0.0, -(RC + 0.096), z0 + 0.07), (0.0, -(RC + 0.112), z0 + 0.07), 0.026,
            "glow", sides=10))
    c = Vector((0.0, -(RC + 0.05), (z0 + z1) / 2))
    noz = [(0.026, 0.0), (0.036, 0.0), (0.054, 0.085), (0.04, 0.085), (0.026, 0.03)]
    for name, axis, start in (("up", (0, 0, 1), c + Vector((0, 0, (z1 - z0) / 2 - 0.01))),
                              ("dn", (0, 0, -1), c - Vector((0, 0, (z1 - z0) / 2 - 0.01))),
                              ("lt", (-1, 0, 0), c + Vector((-0.13, 0, 0))),
                              ("rt", (1, 0, 0), c + Vector((0.13, 0, 0)))):
        n = lathe("rcs_noz_" + name, noz, ["pipe"], seg=8)
        o(aim(n, start, axis))
    for ob in out:
        rotz(ob, phi)
    return out


for phi in (90.0, -90.0):                  # the flanks (the back is never the view)
    parts += rcs(phi)

# ============================================== comms: dish + whip antenna
# Dish on the viewer's right shoulder, bowl turned toward the front-right so the
# default camera looks INTO it (round 1 faced it away: a grey disc from behind).
DPHI, DR = 108.0, 1.74
dz = deck_z(DR)
add(K.box("dish_base", pol(DR, DPHI, 0) + Vector((-0.13, -0.13, dz - 0.08)),
          pol(DR, DPHI, 0) + Vector((0.13, 0.13, dz + 0.10)), "slate", bevel=0.012, seg=1))
post_top = pol(DR, DPHI, dz + 0.60)
add(K.cyl("dish_post", pol(DR, DPHI, dz), post_top, 0.05, "pipe", sides=10))
add(K.cyl("dish_postcollar", pol(DR, DPHI, dz + 0.08), pol(DR, DPHI, dz + 0.16), 0.07, "slate",
          sides=10))
parts += dish("dish", post_top, pol(0.64, 62.0, 0.0) + Vector((0, 0, 0.77)), 0.36)

APHI, AR = -122.0, 1.72
az = deck_z(AR)
ax, ay = pol(AR, APHI, 0).x, pol(AR, APHI, 0).y
Z_TIP = TOP_Z + 0.50
add(K.box("ant_base", (ax - 0.11, ay - 0.11, az - 0.08), (ax + 0.11, ay + 0.11, az + 0.10), "slate",
          bevel=0.012, seg=1))
zm = az + 0.45 * (Z_TIP - az)
add(K.cyl("ant_lo", (ax, ay, az), (ax, ay, zm), 0.042, "pipe", sides=10))
add(K.cyl("ant_ring", (ax, ay, zm - 0.03), (ax, ay, zm + 0.03), 0.058, "slate", sides=10))
add(K.cyl("ant_hi", (ax, ay, zm), (ax, ay, Z_TIP), 0.032, "pipe", sides=8))
add(K.cyl("ant_tip", (ax, ay, Z_TIP - 0.01), (ax, ay, Z_TIP + 0.07), 0.048, "glow", sides=10))

role_stats(parts)
tri_stats(parts)

# ================================================================== finish
ob = K.finish(parts, NAME)
repack(ob)
K.face_camera(ob)                          # hatch (built at -Y) -> -X, the default view
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
