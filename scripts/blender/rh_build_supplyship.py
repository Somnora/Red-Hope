"""Build the Supply Ship from code - the hard-surface build (2026-10-18).

  blender -b -P scripts/blender/rh_build_supplyship.py -- [--out Martians/gen/hs/SupplyShip]
      [--size 2048] [--preview-only]

The game's supply ship: it brings each quota round's cargo from orbit, burns
down onto its landing pad east of the colony (rh_build_landingpad.py), sits for
about a sol while the cargo comes off, then lifts off again. The DESIGN is the
director-approved concept "Ship C - Cargo frame" (2026-10-17): the blockout
ship_C() in the session's concept rig (rh_concept.py) and its renders shipC.png
/ ships_sheet.png (right panel) - the Lander's capsule language made wide and
open: an octagonal slate deck with the Lander's orange band round its rim,
six cargo containers in two rows of three round a central spine (the read from
above: "a ship full of crates"), a small avionics pod at the front, an engine
cluster underneath and four splayed legs. This script is that blockout finished
in the kit's full detail, in the Lander's family: bone-white panels over a dark
slate core with real 2 cm gaps, the orange band (#C8642E, the kit accent here),
light-alloy legs with round ribbed foot pads, gunmetal bells.

Layout facts:
  - TRUE SCALE: the game spawns it at uniform scale 1 with its pivot on the
    landing pad's top (RHColonyVisualizerSubsystem::SpawnSupplyShip, rest =
    pad bounds Max.Z). Real metres, ground-centred pivot: bounds centred on XY,
    bottom of the feet at z=0. The feet set BOTH horizontal extents (four legs
    on the diagonals, symmetric under 90 deg turns), so the pivot is also the
    deck centre - the script asserts the recentre shift is under 3 mm, which
    fails if the pod (the only one-sided part) ever pokes past the feet.
  - built with the FRONT (the avionics pod) at -Y, then K.face_camera() turns
    it to -X, the face the game's default strategy camera meets (measured
    2026-10-13). Legs on the diagonals (azimuth 45 + k*90 from the front), so
    the default view shows the pod framed between the two front legs.
  - deck: ONE 8-sided lathe, corners on the axes and diagonals (vertex radius
    4.50 m: 9.0 m across the corners, 8.31 m across the flats - the concept's
    orientation, which fits the 2 x 3 container block best). Underside 1.72 m
    above the feet, top 2.14 m. The orange band wraps the rim: 10 cm on the
    wall, the 45 deg top chamfer and a 20 cm stripe on top, so it reads as the
    concept's orange octagon from above. Inside the stripe the deck top is a
    dark (rubber) sub-floor 3 cm down, carrying slate deck plates with real
    2 cm gaps round the cargo bays (front, back, three per side), the spine,
    and two bays where the containers stand on four light-alloy tie-down rails.
    Side plates carry a light tie-down rail with slate cleats and a flush
    hatch (alloy frame, orange lift handle); bolts on the band's top stripe.
  - containers: six, 2.20 x 2.05 x 1.95 m (ISO style): slate corner castings,
    posts and top/bottom rails framing a recessed body in bone-white (hull) or
    cargo amber (#ECC46C, the Stockpile's amber, its own "cargo" role - body
    colour, not accent), vertical wall ribs, transverse roof ribs, a dark
    (rubber) centre strap running fore-aft over the roof and down both ends with
    an alloy ratchet on top, and two-leaf doors with four lock bars on the
    OUTER face (the cargo comes off sideways). Rows at x = +-1.40 (0.60 m
    spine gap), columns at y = -2.28 / 0 / +2.28 (23 cm gaps); colours as the
    concept: bone-white, amber, bone-white in both rows. The back corners reach
    4.01 m out on the 67.5 deg flats (deck edge 4.16): inside the deck.
  - spine: slate box between the rows, 0.42 m wide, rising 0.46 m above the
    deck, two gunmetal umbilical pipes on top in slate clamps, dropping into a
    junction box on the back plate and running forward into a connector block
    on the pod's back.
  - pod (front, centre 4.12 m out): a small Lander - slate skirt on a slate
    cradle that overhangs the front corner on three gussets, six bone-white
    wall panels, a dark glass window band round the front 208 deg (slate sill,
    header, posts and two mullions; bone-white panels behind), the orange band,
    a rounded bone-white shoulder lip, a panelled roof, an alloy hatch ring with
    a lid and handle, two grab rails, a forward sensor head, and the comm mast
    at the back of the roof (the dish on a short arm out along its own aim to
    the front-right, so the mast stays behind the bowl; a crossbar with two
    whips). Pod top 3.91 m, whip tips 5.40 m (the tallest point).
  - legs: the Lander's leg scaled for a heavier ship, built once and swung to
    the diagonals: a slate corner hardpoint over each deck corner (hazard plate,
    the amber nav lamp on top), alloy clevis cheeks with a slate pin, a thick
    alloy sleeve with three slate collars, piston, ball joint, a round alloy
    foot pad (r 0.66) with eight radial ribs, and a knee collar with a V of
    secondary struts to slate clevis blocks under the deck. Pin at r 4.78,
    z 1.80; foot pads centred at r 6.15 on the diagonals (the landing pad's
    touchdown marks sit there). Hazard-yellow stripes: the pad's rim band and
    its top outer annulus alternate hazard / slate every 30 deg.
  - engines: a slate thrust frame under the deck, one large central bell
    (exit 1.67 m across) and four small ones (0.84 m) on the axes at r 1.75,
    gunmetal outside, slate exit lip, gimbal collars.
  - the glow is ONE hue per asset in game (amber, as rh_build_chargepad.py) and
    is driven ONLY during the descent and lift-off burn (EmissiveAmount 0 while
    parked): the "glow" role (albedo #FFB040) is on the bell interiors and
    injector plates and on the four nav lamps at the deck corners - nowhere
    else. No other light on the asset.
  - RCS thruster quads (slate block, hazard plate on top, four gunmetal
    nozzles) on the two side corners; a slate service port with a hazard plate
    on the back corner.
  - palette: deck, frames, spine, castings, hardpoints slate; containers and
    pod bone-white (hull) / cargo amber; legs, rails, frames light alloy
    (metal); bells and pipes gunmetal; strap and sub-floor rubber; window
    glass; hazard on the hardpoints, RCS and foot pads. Accent (#C8642E) is the
    TRIM band: deck rim + top stripe, pod band, hatch handles - measured 5.3-7.0 %
    of first-hit pixels from the 54 deg camera at the default yaw and four
    others (role raster of the preview GLB), inside the art bible's 5-15 %.
  - texture space: faces no camera reaches are not built (container and plate
    bottoms, rib backs, the panels' backs behind the cores) and the faces the
    game camera almost never sees get small UV islands before the fixed-margin
    repack: deck underside 25 %, the dark sub-floor 40 %, bell interiors 35 %,
    the container faces across the 23 cm gaps 30 %.
  - result: 23.3k tris (largest families: container corner castings 2.1k,
    door keepers 2.0k, small bells 1.2k, foot pads 1.1k), 20.5 % of the 2K map used
    after the repack (thousands of small islands, each paying its 6 px margin)
    = ~2.2 cm per texel on the faces the strategy camera sees: 2-3 texels per
    screen pixel at the 100 m read, so 2048 is enough. Emissive mask 0.44 % of
    texels. Overall 10.02 x 10.02 x 5.40 m (the foot pads set the width:
    pad centres r 6.15, outer edge r 6.81).
  - dust 0.12, not the kit's usual 0.22: the ship has just come down from
    orbit (round 1 at 0.22 browned the deck and straps like a building that
    has stood on Mars for years).
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
OUT = argv[argv.index("--out") + 1] if "--out" in argv else "Martians/gen/hs/SupplyShip"
SIZE = int(argv[argv.index("--size") + 1]) if "--size" in argv else 2048
PREVIEW_ONLY = "--preview-only" in argv
NAME = "SupplyShip"

K = rh_hs.Kit(accent_hex="#C8642E")       # the Lander family's orange band (concept "band")
# Lit role: amber, exactly as rh_build_chargepad.py. Albedo matches the in-game
# glow hue; preview emission is the game's EmissiveColor normalised, kept low so
# review frames show amber instead of clipping white.
K.roles["glow"] = dict(albedo="#FFB040", metal=0.0, rough=0.25, emit=1, dust=0.0)
K.mats["glow"] = K._preview_material("glow", K.roles["glow"])
_gb = K.mats["glow"].node_tree.nodes["Principled BSDF"]
_gb.inputs["Emission Color"].default_value = (1.0, 0.49, 0.045, 1.0)
_gb.inputs["Emission Strength"].default_value = 1.5
# Cargo amber: the Stockpile's amber as a container BODY colour (painted steel,
# so it chips like the bone-white ones). Not the accent - the accent is trim.
K.roles["cargo"] = dict(albedo="#ECC46C", metal=0.0, rough=0.55, paint=1, dust=1.0)
K.mats["cargo"] = K._preview_material("cargo", K.roles["cargo"])

parts = []
add = parts.append
SINK = 0.004                               # parts bite this far into their support
S2 = math.sqrt(0.5)
C22, S22 = math.cos(math.radians(22.5)), math.sin(math.radians(22.5))


# ------------------------------------------------------------------ helpers
def pol(r, phi, z=0.0):
    """Polar -> xyz with phi in degrees measured from the FRONT (-Y), +phi toward +X."""
    a = math.radians(phi)
    return Vector((r * math.sin(a), -r * math.cos(a), z))


def lathe(name, prof, roles, seg=48, phi0=0.0, phi1=None, closed=True, mis=None, phase=0.0,
          skip=()):
    """Revolve an (r, z) profile about Z (the Lander's lathe). phi1=None -> full
    ring (r = 0 points become poles); otherwise an arc phi0..phi1 with both ends
    capped. mis: material index per profile edge. skip: profile edges NOT built
    (no camera reaches them)."""
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
            uniq = []
            for v in ring:
                if v not in uniq:
                    uniq.append(v)
            if len(uniq) >= 3:
                f = bm.faces.new(uniq if s else list(reversed(uniq)))
                f.material_index = mis[0] if mis else 0
    if closed:
        bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    return K._obj(name, bm, roles)


def panels(name, prof, role, n, gap, r_gap, phase=0.0, seg_per=6, skip=None, hide=()):
    """n arc panels of one closed profile around Z, `gap` metres of seam (at
    radius r_gap) between them; panel k centred on phase + k*360/n."""
    out = []
    step = 360.0 / n
    half_gap = math.degrees(gap / r_gap) / 2.0
    for k in range(n):
        c = phase + k * step
        if skip and skip(((c + 180.0) % 360.0) - 180.0):
            continue
        out.append(lathe("%s%d" % (name, k), prof, [role], seg=seg_per,
                         phi0=c - step / 2 + half_gap, phi1=c + step / 2 - half_gap, skip=hide))
    return out


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


def bolt(name, at, normal, r=0.016, h=0.012, role="metal"):
    """Hex bolt whose base sits 4 mm INSIDE the surface, standing ~8 mm proud."""
    n = Vector(normal).normalized()
    p0 = Vector(at) - n * 0.004
    return K.cyl(name, p0, p0 + n * h, r, role, sides=6)


def convex(name, planes, role, bevel=0.0, seg=1):
    """Closed convex solid = intersection of half-spaces n.p <= d (optionally
    bevelled). None if the half-spaces leave no volume."""
    pts = []
    for (n1, d1), (n2, d2), (n3, d3) in itertools.combinations(planes, 3):
        M = Matrix((n1, n2, n3))
        if abs(M.determinant()) < 1e-9:
            continue
        p = M.inverted() @ Vector((d1, d2, d3))
        if all(Vector(n).dot(p) <= d + 1e-5 for n, d in planes):
            if all((p - q).length > 1e-4 for q in pts):
                pts.append(p)
    if len(pts) < 4:
        return None
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
    default all four), built as one convex solid (rh_build_chargepad.py)."""
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
                edge = nx * (x1 if nx > 0 else x0) if nx else ny * (y1 if ny > 0 else y0)
                planes.append(((nx * S2, ny * S2, S2), S2 * (edge + z1 - tch)))
    return convex(name, planes, role, bevel, seg)


def drop(ob, *dirs):
    """Delete the faces whose normal points along any of `dirs` (a box's bottom on
    the deck, a rib's back against its wall): never seen, so no texels for them."""
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    bm.normal_update()
    kill = [f for f in bm.faces if any(f.normal.dot(Vector(d)) > 0.99 for d in dirs)]
    bmesh.ops.delete(bm, geom=kill, context="FACES_ONLY")
    bm.to_mesh(ob.data)
    bm.free()
    return ob


def u_rail(name, a, b, height, r, role="metal", up=(0, 0, 1)):
    """Grab rail: two legs rising `height` along `up` from feet a and b (feet sunk
    2 cm into the surface) joined by a bar, filleted corners."""
    a, b, up = Vector(a), Vector(b), Vector(up).normalized()
    pts = [a - up * 0.02, a + up * height, b + up * height, b - up * 0.02]
    return K.pipe(name, pts, r, role, bend=min(0.04, height * 0.4), sides=8, samples=3)


def dish(name, post_top, axis, rad, role="hull"):
    """Parabolic dish on a hinge block with a feed boom and horn (the Lander's)."""
    axis = Vector(axis).normalized()
    depth, thick, n = 0.32 * rad, 0.025, 4
    prof = [(rad * i / n, depth * (i / n) ** 2 - thick) for i in range(n + 1)]
    prof.append((rad + 0.014, depth - thick * 0.3))
    prof += [(rad * i / n, depth * (i / n) ** 2) for i in range(n, -1, -1)]
    centre = Vector(post_top) + axis * 0.07
    out = [aim(lathe(name, prof, [role], seg=16), centre, axis)]
    out.append(K.box(name + "_hinge", Vector(post_top) - Vector((0.05, 0.05, 0.05)),
                     Vector(post_top) + Vector((0.05, 0.05, 0.05)), "slate", bevel=0.008, seg=1))
    tip = centre + axis * (depth + 0.55 * rad)
    out.append(K.cyl(name + "_boom", centre, tip, 0.018, "pipe", sides=8))
    out.append(K.cyl(name + "_horn", tip - axis * 0.015, tip + axis * 0.05, 0.035, "slate", sides=8))
    return out


def flat_bezel(name, at, w, h, z_base, proud, radius, rim, recess, rim_role, inner_role, phi=0.0):
    """K.bezel laid flat facing up at `at`: w along local x, h along local y
    (local -y = pol direction phi), back face at z_base (not built). Returns
    (obj, window z, M) where M carries hatch-local points onto the deck."""
    assert radius - rim >= 0.005, "%s: bezel rim %.3f must be < radius %.3f - 5 mm" % (
        name, rim, radius)
    ob, _ = K.bezel(name, (0.0, 0.0, 0.0), w, h, proud, radius=radius, rim=rim, recess=recess,
                    rim_role=rim_role, inner_role=inner_role, facing=-1)
    M = Matrix.Translation(Vector((at[0], at[1], z_base))) @ Matrix.Rotation(math.radians(phi), 4, "Z")
    ob.data.transform(M @ Matrix.Rotation(math.radians(-90.0), 4, "X"))
    drop(ob, (0, 0, -1))
    return ob, z_base + proud - recess, M


def uv_cover(ob):
    me = ob.data
    uv = me.uv_layers.active.data
    me.calc_loop_triangles()
    a = 0.0
    for lt in me.loop_triangles:
        p = [uv[i].uv for i in lt.loops]
        a += abs((p[1].x - p[0].x) * (p[2].y - p[0].y) - (p[2].x - p[0].x) * (p[1].y - p[0].y)) / 2
    return 100.0 * a


def shrink_faces(ob, pred, factor, label):
    """Scale the UVs of the faces pred(centre, normal, role) picks about their
    common centre: each face keeps its shape, gets `factor` of its texels, and
    becomes its own island for the repack that follows."""
    me = ob.data
    uv = me.uv_layers.active.data
    loops = []
    for p in me.polygons:
        if pred(p.center, p.normal, me.materials[p.material_index]["rh_role"]):
            loops.extend(p.loop_indices)
    if not loops:
        rh_hs.log("shrink %s: no faces" % label)
        return
    c = Vector((0.0, 0.0))
    for li in loops:
        c += uv[li].uv
    c /= len(loops)
    for li in loops:
        uv[li].uv = c + (uv[li].uv - c) * factor
    rh_hs.log("UV of %s scaled to %d %% (%d loops)" % (label, round(100 * factor), len(loops)))


def repack(ob, margin=0.003):
    """Re-pack with a FIXED-fraction margin (6 px at 2048), the Lander's repack."""
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
    """Bounds centred on XY, bottom at z=0 (the visualizer's contract). Returns the shift."""
    vs = [v.co for v in ob.data.vertices]
    lo = Vector((min(v.x for v in vs), min(v.y for v in vs), min(v.z for v in vs)))
    hi = Vector((max(v.x for v in vs), max(v.y for v in vs), max(v.z for v in vs)))
    shift = Vector((-(lo.x + hi.x) / 2, -(lo.y + hi.y) / 2, -lo.z))
    ob.data.transform(Matrix.Translation(shift))
    ob.data.update()
    dims = hi - lo
    rh_hs.log("recentred by %s; dims %s m" % ([round(c, 4) for c in shift],
                                              [round(d, 3) for d in dims]))
    return shift


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


def tri_stats(objs, top=40):
    fam = {}
    for o in objs:
        key = o.name.split(".")[0].rstrip("0123456789-_")
        fam[key] = fam.get(key, 0) + sum(len(p.vertices) - 2 for p in o.data.polygons)
    rh_hs.log("tris by family: %s" % ", ".join(
        "%s %d" % kv for kv in sorted(fam.items(), key=lambda kv: -kv[1])[:top]))


def oct_planes(ap):
    """The deck octagon's eight flats as half-spaces (normals at 22.5 + k*45 deg)."""
    return [((math.sin(math.radians(22.5 + 45.0 * k)), -math.cos(math.radians(22.5 + 45.0 * k)),
              0.0), ap) for k in range(8)]


# ------------------------------------------------------------------ layout
RD = 4.50                                  # deck octagon vertex radius (corners on the axes)
AP = RD * C22                              # 4.157 apothem (8.31 m across the flats)
ZU, ZT = 1.72, 2.14                        # deck underside / deck top above the feet
RF = 4.21                                  # inner edge of the band's top stripe (vertex radius)
AP_IN = RF * C22 - 0.02                    # deck plates stay 2 cm inside the stripe step
ZS = ZT - 0.03                             # the dark sub-floor under the plates
CW, CL, CH = 2.20, 2.05, 1.95              # container: across (x) / fore-aft (y) / tall
ZB = ZT + 0.012                            # container base (on the bay rails)
ROW_X = 1.40                               # container rows at x = +-1.40
COL_Y = (-2.28, 0.0, 2.28)                 # container columns (front -> back)
GAP_Y = (-1.14, 1.14)                      # the 23 cm gaps between columns
POD_Y = -4.12                              # avionics pod centre (front)
ZC = ZT + 0.06                             # pod cradle top
LEG_PHI = (45.0, 135.0, 225.0, 315.0)
P_PIN = (4.78, 1.80)                       # leg pin (r, z)
R_FOOT = 6.15                              # foot pad centre radius (landing pad marks)
FOOT_Z = 0.33                              # foot ball centre
COLOURS = {(-1, 0): "hull", (-1, 1): "cargo", (-1, 2): "hull",
           (1, 0): "hull", (1, 1): "cargo", (1, 2): "hull"}


def in_container(x, y, m=0.0):
    for sx in (-1, 1):
        for cy in COL_Y:
            if (abs(x - sx * ROW_X) < CW / 2 + m) and (abs(y - cy) < CL / 2 + m):
                return True
    return False


# =================================================================== deck
# one 8-sided lathe: slate underside and wall, the orange band on the wall's
# top, the 45 deg chamfer and a 20 cm stripe on top, then a 3 cm step down to
# the dark sub-floor the plates and bays sit on
DECK = [((0.0, ZU), 0), ((RD - 0.07, ZU), 0), ((RD, ZU + 0.07), 0), ((RD, ZT - 0.17), 1),
        ((RD, ZT - 0.07), 1), ((RD - 0.07, ZT), 1), ((RF, ZT), 0), ((RF, ZS), 2), ((0.0, ZS), 0)]
add(lathe("deck", [p for p, _ in DECK], ["slate", "accent", "rubber"], seg=8,
          mis=[m for _, m in DECK]))

# deck plates round the bays: front strip, back strip, three per side
XE = [-4.6, -2.62, -0.28, 0.28, 2.62, 4.6]
YE = [-4.6, -3.42, -1.14, 1.14, 3.42, 4.6]
cells = [(XE[1], XE[4], YE[0], YE[1]), (XE[1], XE[4], YE[4], YE[5])]
for j in (1, 2, 3):
    for i in (0, 4):
        cells.append((XE[i], XE[i + 1], YE[j], YE[j + 1]))
for (x0, x1, y0, y1) in cells:
    pl = convex("plate", [((1, 0, 0), x1 - 0.01), ((-1, 0, 0), -(x0 + 0.01)), ((0, 1, 0), y1 - 0.01),
                          ((0, -1, 0), -(y0 + 0.01)), ((0, 0, 1), ZT), ((0, 0, -1), -(ZS - SINK))]
                + oct_planes(AP_IN), "slate", bevel=0.008, seg=1)
    if pl is not None:
        add(drop(pl, (0, 0, -1)))

# cargo bays: four light-alloy tie-down rails the containers stand on, with
# slate cleats where the rails show between the containers
for sx in (-1, 1):
    for xr in (0.48, 2.32):
        x = sx * xr
        ylim = min(3.38, (AP_IN - xr * S22) / C22)
        add(drop(K.box("bayrail", (x - 0.035, -ylim, ZS - SINK), (x + 0.035, ylim, ZB - 0.002),
                       "metal", bevel=0.004, seg=1), (0, 0, -1)))
        for gy in GAP_Y:
            add(drop(K.box("baycleat", (x - 0.05, gy - 0.06, ZB - 0.006), (x + 0.05, gy + 0.06, ZT + 0.045),
                           "slate", bevel=0.006, seg=1), (0, 0, -1)))

# side plates: tie-down rail with cleats, a flush hatch, bolts
for sx in (-1, 1):
    x = sx * 2.80
    add(drop(K.box("trail", (x - 0.045, -2.55, ZT - SINK), (x + 0.045, 2.55, ZT + 0.025), "metal",
                   bevel=0.005, seg=1), (0, 0, -1)))
    for y in (-1.9, -0.6, 0.6, 1.9):
        add(drop(K.box("tcleat", (x - 0.075, y - 0.07, ZT + 0.015), (x + 0.075, y + 0.07, ZT + 0.05),
                       "slate", bevel=0.008, seg=1), (0, 0, -1)))
    hob, hz, HM = flat_bezel("hatch", (sx * 3.45, 0.0), 0.50, 0.72, ZT - SINK, 0.012 + SINK, 0.05,
                             0.035, 0.006, "metal", "slate")
    add(hob)
    hb = K.box("hhandle", (-0.07, 0.22, hz - 0.003), (0.07, 0.25, hz + 0.012), "accent",
               bevel=0.003, seg=1)
    hb.data.transform(Matrix.Translation((sx * 3.45, 0.0, 0.0)))
    add(hb)
    for bx in (-0.18, 0.18):
        for by in (-0.30, 0.30):
            add(bolt("hbolt", (sx * 3.45 + bx, by, hz), (0, 0, 1), r=0.012, h=0.010))

# bolts on the band's top stripe, two per flat, clear of the pod and the cargo
for k in range(8):
    phn = 22.5 + 45.0 * k
    c = pol(3.99, phn, ZT)
    t = pol(1.0, phn + 90.0)
    for s in (-0.9, 0.9):
        p = c + t * s
        if (p - Vector((0.0, POD_Y, ZT))).length < 0.95 or in_container(p.x, p.y, 0.12):
            continue
        add(bolt("sbolt", p, (0, 0, 1)))

# =================================================================== spine
add(drop(cbox("spine", (-0.21, -3.32, ZS - SINK), (0.21, 3.40, ZT + 0.46), "slate", tch=0.05,
              tsides=[(1, 0), (-1, 0)], bevel=0.006), (0, 0, -1)))
ZP = ZT + 0.505                            # umbilical pipe axis
for sx in (-1, 1):
    add(K.pipe("umb", [(sx * 0.10, -3.40, ZP), (sx * 0.10, 3.62, ZP), (sx * 0.10, 3.62, ZT + 0.20)],
               0.045, "pipe", bend=0.12, sides=10, samples=4))
for y in (-3.0, GAP_Y[0], GAP_Y[1], 3.0):
    add(drop(K.box("clamp", (-0.17, y - 0.05, ZT + 0.44), (0.17, y + 0.05, ZP + 0.065), "slate",
                   bevel=0.008, seg=1), (0, 0, -1)))
add(drop(cbox("junction", (-0.30, 3.48, ZT - SINK), (0.30, 3.80, ZT + 0.30), "slate", vch=0.04,
              tch=0.04, bevel=0.006), (0, 0, -1)))
add(K.box("junction_lid", (-0.22, 3.53, ZT + 0.295), (0.22, 3.75, ZT + 0.31), "metal", bevel=0.003,
          seg=1))
add(K.box("umb_conn", (-0.24, -3.43, ZT + 0.36), (0.24, -3.33, ZT + 0.64), "slate", bevel=0.01, seg=1))


# ============================================================== containers
def container(cx, cy, body, side):
    """One ISO-style container in its envelope (cx +- CW/2, cy +- CL/2, ZB..ZB+CH).
    side = which x face is the outer one (doors)."""
    out = []
    o = out.append
    x0, x1, y0, y1 = cx - CW / 2, cx + CW / 2, cy - CL / 2, cy + CL / 2
    z0, z1 = ZB, ZB + CH
    CC, CZ = 0.16, 0.13                    # corner casting plan size / height
    for ex in (-1, 1):
        for ey in (-1, 1):
            xa = (x0, x0 + CC) if ex < 0 else (x1 - CC, x1)
            ya = (y0, y0 + CC) if ey < 0 else (y1 - CC, y1)
            for top in (False, True):
                za = (z1 - CZ, z1) if top else (z0, z0 + CZ)
                cst = K.box("cast", (xa[0], ya[0], za[0]), (xa[1], ya[1], za[1]), "slate", bevel=0.008,
                            seg=1)
                o(cst if top else drop(cst, (0, 0, -1)))
            # corner post between the castings
            px = (x0 + 0.012, x0 + 0.13) if ex < 0 else (x1 - 0.13, x1 - 0.012)
            py = (y0 + 0.012, y0 + 0.13) if ey < 0 else (y1 - 0.13, y1 - 0.012)
            o(K.box("post", (px[0], py[0], z0 + CZ - 0.01), (px[1], py[1], z1 - CZ + 0.01), "slate",
                    bevel=0.0))
    # top and bottom rails (slate frame round the body)
    for zr in ((z1 - 0.11, z1 - 0.006), (z0 + 0.004, z0 + 0.12)):
        for yy in ((y0 + 0.012, y0 + 0.11), (y1 - 0.11, y1 - 0.012)):
            o(drop(K.box("rail", (x0 + CC - 0.01, yy[0], zr[0]), (x1 - CC + 0.01, yy[1], zr[1]), "slate",
                         bevel=0.0), (0, 0, -1)))
        for xx in ((x0 + 0.012, x0 + 0.11), (x1 - 0.11, x1 - 0.012)):
            o(drop(K.box("rail", (xx[0], y0 + CC - 0.01, zr[0]), (xx[1], y1 - CC + 0.01, zr[1]), "slate",
                         bevel=0.0), (0, 0, -1)))
    # body: recessed 4.5 cm behind the frame, roof 3.5 cm under the top rails
    BI = 0.045
    bx0, bx1, by0, by1 = x0 + BI, x1 - BI, y0 + BI, y1 - BI
    zr0, zr1 = z0 + 0.10, z1 - 0.035
    o(drop(K.box("body", (bx0, by0, zr0), (bx1, by1, zr1), body, bevel=0.008, seg=1), (0, 0, -1)))
    rz0, rz1 = z0 + 0.13, z1 - 0.12        # wall rib span (between the rails)
    # vertical ribs on the two end faces (clear of the strap lane) and the inner face
    for sy, yf in ((-1, by0), (1, by1)):
        for dx in (-0.84, -0.62, -0.40, 0.40, 0.62, 0.84):
            ya, yb = sorted((yf - sy * SINK, yf + sy * 0.022))
            o(drop(K.box("rib", (cx + dx - 0.035, ya, rz0), (cx + dx + 0.035, yb, rz1), body, bevel=0.0),
                   (0, -sy, 0), (0, 0, -1)))
    xin = bx0 if side > 0 else bx1         # the face toward the spine
    sn = -side
    for dy in (-0.74, -0.46, -0.18, 0.18, 0.46, 0.74):
        xa, xb = sorted((xin - sn * SINK, xin + sn * 0.022))
        o(drop(K.box("rib", (xa, cy + dy - 0.035, rz0), (xb, cy + dy + 0.035, rz1), body, bevel=0.0),
               (-sn, 0, 0), (0, 0, -1)))
    # transverse roof ribs, broken by the strap lane
    for dy in (-0.62, -0.30, 0.30, 0.62):
        for xa, xb in ((x0 + 0.13, cx - 0.24), (cx + 0.24, x1 - 0.13)):
            o(drop(K.box("rrib", (xa, cy + dy - 0.045, zr1 - SINK), (xb, cy + dy + 0.045, zr1 + 0.016), body,
                         bevel=0.0), (0, 0, -1)))
    # the dark centre strap: fore-aft over the roof and down both ends, ratchet on top
    SW = 0.17
    o(K.box("strap", (cx - SW, y0 - 0.008, z1 - 0.012), (cx + SW, y1 + 0.008, z1 + 0.006), "rubber",
            bevel=0.0))
    for ya, yb in ((y0 - 0.010, y0 + 0.014), (y1 - 0.014, y1 + 0.010)):
        o(drop(K.box("strapd", (cx - SW, ya, z0 + 0.03), (cx + SW, yb, z1 - 0.006), "rubber", bevel=0.0),
               (0, 0, -1)))
    o(drop(K.box("ratchet", (cx - 0.12, cy + 0.42, z1 + 0.002), (cx + 0.12, cy + 0.66, z1 + 0.034), "metal",
                 bevel=0.006, seg=1), (0, 0, -1)))
    # doors on the outer face: centre seam, horizontal ribs, four lock bars
    xf = bx1 if side > 0 else bx0
    xa, xb = sorted((xf - side * SINK, xf + side * 0.010))
    o(drop(K.box("dseam", (xa, cy - 0.012, rz0), (xb, cy + 0.012, rz1), "slate", bevel=0.0), (-side, 0, 0)))
    for zz in (z0 + 0.55, z0 + 1.00, z0 + 1.45):
        for ya, yb in ((y0 + 0.17, cy - 0.04), (cy + 0.04, y1 - 0.17)):
            xa, xb = sorted((xf - side * SINK, xf + side * 0.014))
            o(drop(K.box("drib", (xa, ya, zz - 0.04), (xb, yb, zz + 0.04), body, bevel=0.0), (-side, 0, 0)))
    xbar = xf + side * 0.038
    for dy in (-0.78, -0.16, 0.16, 0.78):
        o(rod("lockbar", (xbar, cy + dy, rz0 + 0.04), (xbar, cy + dy, rz1 - 0.04), 0.017, "metal", sides=8))
        for zz in (rz0 + 0.06, rz1 - 0.06):
            xa, xb = sorted((xf - side * SINK, xf + side * 0.062))
            o(drop(K.box("keeper", (xa, cy + dy - 0.04, zz - 0.035), (xb, cy + dy + 0.04, zz + 0.035), "slate",
                         bevel=0.004, seg=1), (-side, 0, 0)))
    for dy in (-0.16, 0.16):               # handles on the two inner bars
        ya, yb = sorted((cy + dy, cy + dy + math.copysign(0.11, dy)))
        o(K.box("dhandle", (xbar - 0.012, ya, z0 + 0.86), (xbar + 0.012, yb, z0 + 0.90), "metal",
                bevel=0.0))
    return out


for sx in (-1, 1):
    for j, cy in enumerate(COL_Y):
        parts += container(sx * ROW_X, cy, COLOURS[(sx, j)], sx)

# =================================================================== pod
PC = Vector((0.0, POD_Y, 0.0))
RP, RPC = 0.745, 0.725                     # pod panel outer radius / slate core
pod = []
P = pod.append
P(lathe("cradle", [(0.0, ZT - 0.10), (0.84, ZT - 0.10), (0.86, ZT - 0.08), (0.86, ZT + 0.04),
                   (0.84, ZT + 0.06), (0.0, ZT + 0.06)], ["slate"], seg=32))
P(lathe("pskirt", [(0.70, ZC - 0.01), (0.765, ZC - 0.01), (0.765, ZC + 0.05), (0.75, ZC + 0.105),
                   (0.70, ZC + 0.105)], ["slate"], seg=32, skip=(0, 4)))
P(lathe("pcore", [(RPC, ZC + 0.08), (RPC, ZC + 1.46)], ["slate"], seg=32, closed=False))


def pwall(z0, z1):
    return [(RPC - 0.012, z0), (RP - 0.012, z0), (RP, z0 + 0.012), (RP, z1 - 0.012),
            (RP - 0.012, z1), (RPC - 0.012, z1)]


pod += panels("pwl", pwall(ZC + 0.10, ZC + 0.84), "hull", 6, 0.02, RP, phase=0.0, seg_per=5, hide=(5,))
# window band: dark glass round the front 208 deg, slate sill / header / posts / mullions
WZ0, WZ1 = ZC + 0.86, ZC + 1.14
P(lathe("glass", [(RPC - 0.01, WZ0), (RP - 0.01, WZ0), (RP - 0.01, WZ1), (RPC - 0.01, WZ1)], ["glass"],
        seg=26, phi0=-104.0, phi1=104.0, skip=(3,)))
for nm, za, zb in (("sill", ZC + 0.838, WZ0 + 0.012), ("header", WZ1 - 0.012, ZC + 1.162)):
    P(lathe(nm, [(RPC - 0.01, za), (RP + 0.008, za), (RP + 0.014, za + 0.008), (RP + 0.014, zb - 0.008),
                 (RP + 0.008, zb), (RPC - 0.01, zb)], ["slate"], seg=26, phi0=-108.0, phi1=108.0, skip=(5,)))
for a0, a1 in ((-108.0, -103.5), (103.5, 108.0)):
    P(lathe("wpost", [(RPC - 0.01, WZ0), (RP + 0.012, WZ0), (RP + 0.012, WZ1), (RPC - 0.01, WZ1)], ["slate"],
            seg=1, phi0=a0, phi1=a1, skip=(3,)))
for c in (-36.0, 36.0):
    P(lathe("mullion", [(RPC - 0.01, WZ0), (RP + 0.004, WZ0), (RP + 0.004, WZ1), (RPC - 0.01, WZ1)], ["slate"],
            seg=1, phi0=c - 1.3, phi1=c + 1.3, skip=(3,)))
HG = math.degrees(0.01 / RP)
for a0, a1 in ((108.0 + HG, 180.0 - HG), (180.0 + HG, 252.0 - HG)):
    P(lathe("pwb", pwall(WZ0 - 0.005, WZ1 + 0.005), ["hull"], seg=6, phi0=a0, phi1=a1, skip=(5,)))
# the orange band and the rounded bone-white shoulder lip
P(lathe("pband", [(RPC - 0.01, ZC + 1.17), (RP + 0.014, ZC + 1.17), (RP + 0.014, ZC + 1.305),
                  (RPC - 0.01, ZC + 1.305)], ["accent"], seg=32, skip=(3,)))
P(lathe("plip", [(0.70, ZC + 1.30), (0.768, ZC + 1.30), (0.783, ZC + 1.32), (0.783, ZC + 1.41),
                 (0.765, ZC + 1.44), (0.70, ZC + 1.44)], ["hull"], seg=32, skip=(5,)))
# panelled roof
RA, RB = Vector((0.735, ZC + 1.44)), Vector((0.45, ZC + 1.62))
RT = (RB - RA).normalized()
RN = Vector((-RT.y, RT.x))
if RN.y < 0:
    RN = -RN


def roof_prof(th=0.022, ch=0.010, bite=0.012):
    p, q = RA, RB
    pts = [p - RN * bite, p + RN * (th - ch), p + RN * th + RT * ch, q + RN * th - RT * ch,
           q + RN * (th - ch), q - RN * bite]
    return [(v.x, v.y) for v in pts]


def roof_z(r):
    f = (r - RA.x) / (RB.x - RA.x)
    return RA.y + f * (RB.y - RA.y) + 0.022 / RN.y


_c0, _c1 = RA - RN * 0.02 - RT * 0.03, RB - RN * 0.02 + RT * 0.02
P(lathe("rcore", [(_c0.x, _c0.y), (_c1.x, _c1.y)], ["slate"], seg=32, closed=False))
pod += panels("roof", roof_prof(), "hull", 6, 0.02, 0.6, phase=30.0, seg_per=4, hide=(5,))
P(lathe("pring", [(0.30, ZC + 1.585), (0.45, ZC + 1.585), (0.47, ZC + 1.605), (0.47, ZC + 1.68),
                  (0.45, ZC + 1.70), (0.30, ZC + 1.70)], ["metal"], seg=32))
P(lathe("plid", [(0.0, ZC + 1.62), (0.31, ZC + 1.62), (0.31, ZC + 1.68), (0.295, ZC + 1.69),
                 (0.0, ZC + 1.69)], ["hull"], seg=24, skip=(0,)))
P(lathe("pboss", [(0.0, ZC + 1.68), (0.10, ZC + 1.68), (0.10, ZC + 1.70), (0.085, ZC + 1.712),
                  (0.0, ZC + 1.712)], ["metal"], seg=12, skip=(0,)))
P(u_rail("plid_handle", (-0.11, -0.20, ZC + 1.69), (0.11, -0.20, ZC + 1.69), 0.05, 0.016))
RN3 = lambda phi: RN.x * pol(1.0, phi) + Vector((0.0, 0.0, RN.y))  # noqa: E731
for phi in (-62.0, 62.0):
    rr = 0.60
    P(u_rail("prail", pol(rr, phi - 9.0, roof_z(rr)), pol(rr, phi + 9.0, roof_z(rr)), 0.07, 0.018,
             up=RN3(phi)))
# forward sensor head on the roof, a glass eye looking ahead
_sz = roof_z(0.62)
P(cbox("sensor", (-0.09, -0.70, _sz - 0.04), (0.09, -0.54, _sz + 0.11), "slate", tch=0.03,
       tsides=[(0, -1)], bevel=0.006))
P(K.cyl("sensor_eye", (0.0, -0.690, _sz + 0.04), (0.0, -0.715, _sz + 0.04), 0.045, "glass", sides=12))
# comm mast at the back of the roof: dish to the front-right, crossbar with two whips
MY = 0.56
_mz = roof_z(MY)
P(K.box("mast_base", (-0.09, MY - 0.09, _mz - 0.06), (0.09, MY + 0.09, _mz + 0.08), "slate", bevel=0.01,
        seg=1))
P(rod("mast", (0.0, MY, _mz + 0.05), (0.0, MY, ZC + 2.86), 0.045, "pipe", sides=10))
P(rod("mast_collar", (0.0, MY, ZC + 2.00), (0.0, MY, ZC + 2.07), 0.065, "slate", sides=10))
# the dish sits on a short arm out along its own aim, so the mast stays behind the bowl
_dax = pol(0.75, 55.0) + Vector((0.0, 0.0, 0.66))
_arm = Vector((_dax.x, _dax.y, 0.0)).normalized() * 0.26 + Vector((0.0, MY, ZC + 2.30))
P(rod("dish_arm", (0.0, MY, ZC + 2.30), _arm, 0.03, "pipe", sides=8))
pod += dish("pdish", _arm, _dax, 0.24)
P(K.box("mast_bar", (-0.23, MY - 0.03, ZC + 2.79), (0.23, MY + 0.03, ZC + 2.845), "slate", bevel=0.008,
        seg=1))
P(rod("mast_cap", (0.0, MY, ZC + 2.85), (0.0, MY, ZC + 2.90), 0.055, "slate", sides=10))
for sx in (-1, 1):
    P(rod("whip", (sx * 0.19, MY, ZC + 2.84), (sx * 0.19, MY, ZC + 3.20), 0.013, "pipe", sides=6))
for ob in pod:
    move(ob, PC)
parts += pod
# gussets under the cradle where it overhangs the front corner
for x in (-0.42, 0.0, 0.42):
    yw = -(RD - abs(x) / math.tan(math.radians(67.5)))      # deck wall at this x
    ye = POD_Y - math.sqrt(0.80 ** 2 - x * x)               # under the cradle's rim
    add(obox("gusset", (x, yw + 0.06, ZU + 0.10), (x, ye, ZT - 0.06), 0.10, 0.10, "slate",
             side=(1, 0, 0), bevel=0.006))


# =================================================================== legs
def foot_pad():
    """Round alloy foot pad (the Lander's, x1.35): slate chamfer, a rim band and a
    top outer annulus striped hazard / slate every 30 deg, an alloy dish and boss."""
    prof = [(0.0, 0.0), (0.66, 0.0), (0.66, 0.115), (0.635, 0.132), (0.54, 0.132), (0.216, 0.19),
            (0.216, 0.29), (0.0, 0.29)]
    pad = lathe("lpad", prof, ["metal", "slate", "hazard"], seg=24, mis=[0, 2, 1, 2, 0, 0, 0, 0],
                skip=(0,))                 # bottom: on the ground
    me = pad.data
    for p in me.polygons:
        if p.material_index == 2:
            a = math.degrees(math.atan2(p.center.y, p.center.x)) % 360.0
            if int(a // 30.0) % 2:
                p.material_index = 1
    return pad


def leg(phi):
    """Built for a leg pointing -Y (azimuth 0), then swung to the diagonal."""
    out = []
    o = out.append
    pr, pz = P_PIN
    # slate corner hardpoint over the deck corner: hazard plate, the nav lamp on top
    o(drop(cbox("hardpt", (-0.30, -4.66, ZU - 0.12), (0.30, -4.02, ZT + 0.05), "slate", vch=0.08,
                corners=[(-1, -1), (1, -1)], tch=0.03, tsides=[(0, -1)], bevel=0.008), (0, 1, 0)))
    o(K.box("hp_haz", (-0.16, -4.674, ZT - 0.12), (0.16, -4.655, ZT - 0.04), "hazard", bevel=0.0))
    o(K.cyl("nav_house", (0.0, -4.40, ZT + 0.045), (0.0, -4.40, ZT + 0.10), 0.10, "slate", sides=12))
    o(move(lathe("nav_lens", [(0.0, ZT + 0.095), (0.076, ZT + 0.095), (0.073, ZT + 0.125), (0.058, ZT + 0.152),
                              (0.032, ZT + 0.168), (0.0, ZT + 0.173)], ["glow"], seg=12, skip=(0,)),
             (0.0, -4.40, 0.0)))
    # clevis
    for s in (-1, 1):
        xa, xb = sorted((s * 0.085, s * 0.150))
        o(K.box("lcheek", (xa, -(pr + 0.15), pz - 0.16), (xb, -4.64, pz + 0.16), "metal", bevel=0.0,
                chamfers=[(lambda d, m: abs(d.x) > 0.99 and m.z < pz - 0.12 and m.y < -(pr + 0.1), 0.10)]))
        o(K.cyl("lpinhead", (s * 0.148, -pr, pz), (s * 0.185, -pr, pz), 0.066, "slate", sides=6))
    o(K.cyl("lpin", (-0.16, -pr, pz), (0.16, -pr, pz), 0.045, "slate", sides=8))
    o(K.cyl("leye", (-0.08, -pr, pz), (0.08, -pr, pz), 0.10, "metal", sides=12))
    top = Vector((0.0, -pr, pz))
    foot = Vector((0.0, -R_FOOT, FOOT_Z))
    d = (foot - top).normalized()
    L = (foot - top).length
    s0, s1 = top + d * 0.20, top + d * (0.56 * L)
    o(rod("lneck", top, s0 + d * 0.02, 0.066, "metal", sides=10))
    o(rod("lsleeve", s0, s1, 0.13, "metal", sides=14))
    for f in (0.16, 0.355, 0.545):
        c = top + d * (f * L)
        o(rod("lcollar", c - d * 0.04, c + d * 0.04, 0.156, "slate", sides=12))
    o(rod("lpiston", s1 - d * 0.05, foot - d * 0.07, 0.086, "metal", sides=12))
    o(move(lathe("lball", [(0.0, -0.115)] + [(0.115 * math.cos(math.radians(a)), 0.115 * math.sin(math.radians(a)))
                                            for a in (-60, -30, 0, 30, 60)] + [(0.0, 0.115)],
                 ["metal"], seg=10), foot))
    o(move(foot_pad(), (0.0, -R_FOOT, 0.0)))
    for j in range(8):
        a = math.radians(22.5 + j * 45.0)
        u = Vector((math.cos(a), math.sin(a), 0.0))
        c = Vector((0.0, -R_FOOT, 0.0))
        o(obox("lrib", c + u * 0.19 + Vector((0, 0, 0.21)), c + u * 0.53 + Vector((0, 0, 0.152)), 0.054,
               0.074, "metal", side=(-u.y, u.x, 0.0), bevel=0.0))
    # knee collar with a V of secondary struts to clevis blocks under the deck
    knee = foot - d * (0.26 * L)
    o(rod("lknee", knee - d * 0.08, knee + d * 0.08, 0.12, "slate", sides=12))
    for s in (-1, 1):
        anc = pol(3.55, s * 12.0, ZU - 0.045)
        o(drop(K.box("lclevis", anc - Vector((0.09, 0.09, 0.06)), anc + Vector((0.09, 0.09, 0.06)), "slate",
                     bevel=0.0), (0, 0, 1)))
        o(rod("lstrut", knee, anc, 0.055, "metal", sides=8))
    for ob in out:
        rotz(ob, phi)
    return out


for phi in LEG_PHI:
    parts += leg(phi)

# ================================================================ engines
add(lathe("thrust", [(0.0, 1.40), (0.62, 1.40), (0.70, 1.46), (2.30, ZU - 0.03), (2.30, ZU + 0.01),
                     (0.0, ZU + 0.01)], ["slate"], seg=16, phase=11.25, skip=(4,)))
# bell: gunmetal outside, slate exit lip, lit interior + injector plate (glow)
BIG = [(0.0, 1.43), (0.36, 1.43), (0.44, 1.20), (0.60, 0.90), (0.80, 0.63), (0.835, 0.61),
       (0.835, 0.57), (0.795, 0.57), (0.585, 0.87), (0.425, 1.15), (0.33, 1.35), (0.0, 1.35)]
BIG_MI = [0, 0, 0, 0, 1, 1, 1, 2, 2, 2, 2, 0]
add(lathe("bell", BIG, ["pipe", "slate", "glow"], seg=28, mis=BIG_MI, skip=(0,)))
add(lathe("bellcollar", [(0.34, 1.30), (0.45, 1.30), (0.45, 1.41), (0.34, 1.41)], ["slate"], seg=16,
          skip=(3,)))
SMALL = [(0.0, 1.66), (0.18, 1.66), (0.22, 1.54), (0.30, 1.38), (0.40, 1.24), (0.418, 1.23),
         (0.418, 1.205), (0.397, 1.205), (0.293, 1.36), (0.212, 1.51), (0.165, 1.62), (0.0, 1.62)]
for k in range(4):
    at = pol(1.75, k * 90.0)
    add(move(lathe("sbell%d" % k, SMALL, ["pipe", "slate", "glow"], seg=16, mis=BIG_MI, skip=(0,)), at))
    add(move(lathe("sbellcollar%d" % k, [(0.18, 1.52), (0.25, 1.52), (0.25, 1.63), (0.18, 1.63)], ["slate"],
                   seg=12, skip=(3,)), at))


# ===================================================== RCS quads / service port
def rcs(phi):
    out = []
    o = out.append
    z0, z1 = ZT - 0.30, ZT + 0.10
    o(K.box("rcs_body", (-0.20, -(RD + 0.16), z0), (0.20, -(RD - 0.30), z1), "slate", bevel=0.016, seg=1))
    o(K.box("rcs_haz", (-0.14, -(RD - 0.20), z1 - 0.006), (0.14, -(RD - 0.02), z1 + 0.008), "hazard",
            bevel=0.0))
    noz = [(0.036, 0.0), (0.05, 0.0), (0.076, 0.12), (0.056, 0.12), (0.036, 0.042)]
    yc = -(RD + 0.05)
    for nm, axis, start in (("up", (0, 0, 1), Vector((0.0, yc, z1 - 0.01))),
                            ("dn", (0, 0, -1), Vector((0.0, yc, z0 + 0.01))),
                            ("lt", (-1, 0, 0), Vector((-0.19, yc, (z0 + z1) / 2))),
                            ("rt", (1, 0, 0), Vector((0.19, yc, (z0 + z1) / 2)))):
        o(aim(lathe("rcs_noz_" + nm, noz, ["pipe"], seg=10), start, axis))
    for ob in out:
        rotz(ob, phi)
    return out


for phi in (90.0, 270.0):                  # the two side corners
    parts += rcs(phi)

port = [K.box("port", (-0.32, -(RD + 0.08), ZU + 0.05), (0.32, -(RD - 0.30), ZT - 0.185), "slate",
              bevel=0.012, seg=1),
        K.box("port_haz", (-0.27, -(RD + 0.092), ZT - 0.29), (-0.05, -(RD + 0.074), ZT - 0.21), "hazard",
              bevel=0.0)]
for x in (0.10, 0.22):
    port.append(K.cyl("port_cap", (x, -(RD + 0.07), ZT - 0.25), (x, -(RD + 0.13), ZT - 0.25), 0.05, "metal",
                      sides=10))
for ob in port:
    rotz(ob, 180.0)
parts += port

role_stats(parts)
tri_stats(parts)

# ================================================================== finish
ob = K.finish(parts, NAME)
_gap = lambda c: any(abs(c.y - g) < 0.17 for g in GAP_Y)    # noqa: E731
shrink_faces(ob, lambda c, n, r: n.z < -0.99 and abs(c.z - ZU) < 0.02, 0.25, "deck underside")
shrink_faces(ob, lambda c, n, r: r == "rubber" and n.z > 0.99 and abs(c.z - ZS) < 0.003, 0.40,
             "dark sub-floor")
shrink_faces(ob, lambda c, n, r: r == "glow" and c.z < ZU, 0.35, "bell interiors")
shrink_faces(ob, lambda c, n, r: abs(n.y) > 0.9 and ZT < c.z < ZB + CH + 0.05 and 0.25 < abs(c.x) < 2.55
             and _gap(c), 0.30, "container faces across the gaps")
repack(ob)
K.face_camera(ob)                          # pod (built at -Y) -> -X, the default view
shift = recentre(ob)
assert max(abs(shift.x), abs(shift.y)) < 0.003, \
    "pivot is not the deck centre (shift %s): something pokes past the feet" % shift
assert abs(shift.z) < 1e-4, "the feet are not the lowest point (shift z %.4f)" % shift.z
rmax = max(math.hypot(v.co.x, v.co.y) for v in ob.data.vertices)
ztop = max(v.co.z for v in ob.data.vertices)
rh_hs.log("feet: pad centres at r %.2f on the diagonals; max horizontal radius %.3f m; top %.3f m"
          % (R_FOOT, rmax, ztop))
os.makedirs(OUT, exist_ok=True)
bpy.ops.object.select_all(action="DESELECT")
ob.select_set(True)
bpy.context.view_layer.objects.active = ob
bpy.ops.export_scene.gltf(filepath=os.path.join(OUT, NAME + "_preview.glb"), export_format="GLB",
                          use_selection=True, export_apply=True)
rh_hs.log("preview glb written")
if not PREVIEW_ONLY:
    files = K.bake_and_export(ob, NAME, OUT, size=SIZE, dust=0.12)
    rh_hs.log("files: %s" % files)
