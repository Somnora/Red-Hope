"""Build the Landing Pad from code - the hard-surface build (2026-10-18).

  blender -b -P scripts/blender/rh_build_landingpad.py -- [--out Martians/gen/hs/LandingPad]
      [--size 2048] [--preview-only]

The supply ship's landing pad, east of the colony: the ship (rh_build_supplyship.py)
burns down onto it, sits for about a sol while the cargo comes off, and lifts
off; the pad stays behind, empty, until the next ship. The DESIGN is the pad
of the director-approved concept "Ship C - Cargo frame" (2026-10-17): the
blockout landing_pad() in the session's concept rig (rh_concept.py) and its
render shipC.png - a low round dark pad with a hazard-yellow ring near the
rim, an inner touchdown ring and bone-white touchdown marks - finished in the
kit's full detail and moved so the marks sit exactly under the ship's feet.

Layout facts:
  - TRUE SCALE, real metres, ground-centred pivot (bounds centred on XY,
    bottom at z=0). Round, 15.2 m across (r 7.60), sitting on the ground.
  - THE PAD'S TOP IS WHERE THE SHIP STANDS: the game rests the ship's pivot at
    the pad's bounding-box Max.Z (RHColonyVisualizerSubsystem::ShipPadTopCm).
    So the highest parts are the four touchdown plates the feet land on, at
    exactly z = 0.150 m, and nothing stands higher: the light bezels and drain
    grate frames stop at 0.150, every bolt below it. The script asserts it.
  - slab: ONE base lathe (96 segments): ground chamfer, wall, a 45 deg top
    chamfer, a 23 cm slate rim at z 0.144, then a 2 cm step down to the dark
    (rubber) gap floor. On it sit the deck slabs, each a lathe arc 2.4 cm thick
    with 5 mm chamfers, so every seam is a real 2 cm gap: a gunmetal blast plate
    in the centre (r 1.50, a ring of 16 bolts), 8 slabs in ring A (r 1.52-3.70)
    and 16 in ring B (r 3.72-7.28), ring B centred on the ship's leg diagonals
    so each touchdown plate sits in the middle of one slab.
  - markings are flush paint zones of the slab tops (no decal can z-fight):
    the hazard-yellow ring r 6.96-7.24 near the rim and the inner hazard
    touchdown ring r 3.00-3.25 (the concept's), broken only by the slab seams.
  - touchdown marks: on the diagonals at r 6.15, the ship's foot radius - a
    raised plate (r 0.86, 6 mm proud: the pad's top) painted as a target, a
    bone-white ring r 0.70-0.86 round a slate field with a bone-white centre;
    the ship's foot pads (r 0.66) stand inside the ring, so with the ship down
    each foot shows a white ring round it. Three bone-white alignment ticks per
    mark (inward and both sides) and a bone-white chevron at r 4.3 on each
    diagonal pointing out to its mark.
  - four drain grates (alloy frame, rubber well, eight alloy bars) on the axes
    at r 5.30, between the legs; eight edge lights in slate bezels on the rim
    (r 7.42, at 22.5 + k*45 deg) and sixteen rim bolts between them.
  - the glow is ONE hue per asset (amber, as rh_build_chargepad.py): only the
    eight edge-light windows use the "glow" role (albedo #FFB040); in game they
    glow softly all the time (PoweredState 1).
  - scorch: the engines fire at the centre, so the BaseColor is darkened toward
    a warm soot (#221C19) after the kit's compose - a dense core under the bells
    (r < ~2.4 m), a wider halo fading out by r ~5.6 m broken by broad radial
    blast streaks and blotches, and a few streak fingers reaching toward the
    rim (round 1's finer streaks read as an artificial sunburst); soot is also rougher and less metallic in
    the MetallicRoughness map. The scorch field is baked from object space in
    the same UVs and applied through a hook on the kit's PNG writer, so the
    GLB embeds the scorched maps directly.
  - palette: slabs and rim slate, blast plate gunmetal (pipe), gaps rubber,
    marks / ticks / chevrons bone-white (hull), rings hazard, grate frames and
    bars light alloy (metal), edge-light windows glow. The kit accent (#C8642E,
    the ship family's orange) is not used on the pad.
  - texture space: the slab bottoms, the base's underside and the plates'
    undersides are not built; the gap floor (seen only through 2 cm seams)
    keeps 15 % of its texels.
  - result: 7.8k tris (slabs 2.5k, marks 1.3k, edge lights 1.1k, base 1.1k),
    36 % of the 2K map used, ~1.2-1.6 cm per texel; emissive mask 0.24 %.
  - dust 0.12 (the kit's usual 0.22 turned the whole slate pad brown in round
    1 and lost the "dark pad" read against the ground).
Real-world metres, ground-centred pivot (bounds centred on XY, bottom at z=0).
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bmesh  # noqa: E402
import bpy  # noqa: E402
import numpy as np  # noqa: E402
from mathutils import Matrix, Vector  # noqa: E402

import rh_hs  # noqa: E402

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OUT = argv[argv.index("--out") + 1] if "--out" in argv else "Martians/gen/hs/LandingPad"
SIZE = int(argv[argv.index("--size") + 1]) if "--size" in argv else 2048
PREVIEW_ONLY = "--preview-only" in argv
NAME = "LandingPad"

K = rh_hs.Kit(accent_hex="#C8642E")       # the ship family's orange (not used on the pad)
# Lit role: amber, exactly as rh_build_chargepad.py.
K.roles["glow"] = dict(albedo="#FFB040", metal=0.0, rough=0.25, emit=1, dust=0.0)
K.mats["glow"] = K._preview_material("glow", K.roles["glow"])
_gb = K.mats["glow"].node_tree.nodes["Principled BSDF"]
_gb.inputs["Emission Color"].default_value = (1.0, 0.49, 0.045, 1.0)
_gb.inputs["Emission Strength"].default_value = 1.5

parts = []
add = parts.append
SINK = 0.004


# ------------------------------------------------------------------ helpers
def pol(r, phi, z=0.0):
    """Polar -> xyz with phi in degrees from -Y, +phi toward +X (the ship's convention)."""
    a = math.radians(phi)
    return Vector((r * math.sin(a), -r * math.cos(a), z))


def lathe(name, prof, roles, seg=48, phi0=0.0, phi1=None, closed=True, mis=None, phase=0.0,
          skip=()):
    """Revolve an (r, z) profile about Z (the Lander's lathe; see rh_build_supplyship.py)."""
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
            f = bm.faces.new(ring if s else list(reversed(ring)))
            f.material_index = mis[0] if mis else 0
    if closed:
        bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    return K._obj(name, bm, roles)


def rotz(ob, phi):
    ob.data.transform(Matrix.Rotation(math.radians(phi), 4, "Z"))
    return ob


def move(ob, d):
    ob.data.transform(Matrix.Translation(Vector(d)))
    return ob


def prism(name, poly, z0, z1, role):
    """Vertical prism over a convex XY polygon (bottom not built: it sits in a slab)."""
    area = sum(poly[i][0] * poly[(i + 1) % len(poly)][1] - poly[(i + 1) % len(poly)][0] * poly[i][1]
               for i in range(len(poly)))
    if area < 0:                           # wind counter-clockwise: top up, sides out
        poly = list(reversed(poly))
    bm = bmesh.new()
    lo = [bm.verts.new((x, y, z0)) for x, y in poly]
    hi = [bm.verts.new((x, y, z1)) for x, y in poly]
    bm.faces.new(hi)
    n = len(poly)
    for i in range(n):
        j = (i + 1) % n
        bm.faces.new((lo[i], lo[j], hi[j], hi[i]))
    return K._obj(name, bm, [role])


def drop(ob, *dirs):
    bm = bmesh.new()
    bm.from_mesh(ob.data)
    bm.normal_update()
    kill = [f for f in bm.faces if any(f.normal.dot(Vector(d)) > 0.99 for d in dirs)]
    bmesh.ops.delete(bm, geom=kill, context="FACES_ONLY")
    bm.to_mesh(ob.data)
    bm.free()
    return ob


def flat_bezel(name, at, w, h, z_base, proud, radius, rim, recess, rim_role, inner_role, phi=0.0):
    """K.bezel laid flat facing up at `at`, w along the local tangential x, h along
    the local radial y (local -y = pol direction phi), back face at z_base (not
    built). Returns (obj, window z, M) with M carrying local points onto the pad."""
    assert radius - rim >= 0.005, "%s: bezel rim %.3f must be < radius %.3f - 5 mm" % (
        name, rim, radius)
    ob, _ = K.bezel(name, (0.0, 0.0, 0.0), w, h, proud, radius=radius, rim=rim, recess=recess,
                    rim_role=rim_role, inner_role=inner_role, facing=-1)
    M = Matrix.Translation(Vector((at[0], at[1], z_base))) @ Matrix.Rotation(math.radians(phi), 4, "Z")
    ob.data.transform(M @ Matrix.Rotation(math.radians(-90.0), 4, "X"))
    drop(ob, (0, 0, -1))
    return ob, z_base + proud - recess, M


def shrink_faces(ob, pred, factor, label):
    """Scale the UVs of the faces pred(centre, normal, role) picks (see the ship)."""
    me = ob.data
    uv = me.uv_layers.active.data
    loops = []
    for p in me.polygons:
        if pred(p.center, p.normal, me.materials[p.material_index]["rh_role"]):
            loops.extend(p.loop_indices)
    if not loops:
        return
    c = Vector((0.0, 0.0))
    for li in loops:
        c += uv[li].uv
    c /= len(loops)
    for li in loops:
        uv[li].uv = c + (uv[li].uv - c) * factor
    rh_hs.log("UV of %s scaled to %d %% (%d loops)" % (label, round(100 * factor), len(loops)))


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
    vs = [v.co for v in ob.data.vertices]
    lo = Vector((min(v.x for v in vs), min(v.y for v in vs), min(v.z for v in vs)))
    hi = Vector((max(v.x for v in vs), max(v.y for v in vs), max(v.z for v in vs)))
    shift = Vector((-(lo.x + hi.x) / 2, -(lo.y + hi.y) / 2, -lo.z))
    ob.data.transform(Matrix.Translation(shift))
    ob.data.update()
    rh_hs.log("recentred by %s; dims %s m" % ([round(c, 4) for c in shift],
                                              [round(d, 3) for d in hi - lo]))
    return shift


def role_stats(objs):
    tot, tris = {}, {}
    for o in objs:
        me = o.data
        for p in me.polygons:
            r = me.materials[p.material_index]["rh_role"]
            tot[r] = tot.get(r, 0.0) + p.area
        key = o.name.split(".")[0].rstrip("0123456789-_")
        tris[key] = tris.get(key, 0) + sum(len(p.vertices) - 2 for p in me.polygons)
    s = sum(tot.values())
    rh_hs.log("role share (all faces): %s" % ", ".join(
        "%s %.1f%%" % (k, 100 * v / s) for k, v in sorted(tot.items(), key=lambda kv: -kv[1])))
    rh_hs.log("tris by family: %s" % ", ".join(
        "%s %d" % kv for kv in sorted(tris.items(), key=lambda kv: -kv[1])))


# ------------------------------------------------------------------ layout
R_PAD = 7.60                               # pad radius (15.2 m across)
ZTOP = 0.150                               # touchdown plates: the pad's highest point
ZF = 0.144                                 # slab tops and rim
ZG = 0.124                                 # the dark gap floor (base slab top)
R_RIM = 7.30                               # inner edge of the base's rim
R_FOOT = 6.15                              # rh_build_supplyship.py: foot pads on the diagonals
LEGS = (45.0, 135.0, 225.0, 315.0)
CH = 0.005                                 # slab edge chamfer

# =================================================================== base
BASE = [((0.0, 0.0), 0), ((R_PAD - 0.03, 0.0), 0), ((R_PAD, 0.03), 0), ((R_PAD, ZF - 0.069), 0),
        ((R_PAD - 0.069, ZF), 0), ((R_RIM, ZF), 0), ((R_RIM, ZG), 1), ((0.0, ZG), 0)]
add(lathe("base", [p for p, _ in BASE], ["slate", "rubber"], seg=96, mis=[m for _, m in BASE],
          skip=(0,)))                      # underside: on the ground


# ================================================================== slabs
def slab_prof(r0, r1, zones):
    """Closed (r, z) profile of a slab r0..r1 (outer wall up, top inward, inner
    wall down; the last edge, the bottom sunk under the gap floor, is not built).
    zones: [(ra, rb, mi)] painted flush on the top; elsewhere mi 0. -> (pts, mis)."""
    zb = ZG - SINK
    pts = [(r1, zb), (r1, ZF - CH), (r1 - CH, ZF)]
    mis = [0, 0]
    stops = sorted({b for za, zr, _ in zones for b in (za, zr) if r0 + CH < b < r1 - CH},
                   reverse=True) + [r0 + CH]
    cur = r1 - CH
    for b in stops:
        mid = (cur + b) / 2
        mis.append(next((m for za, zr, m in zones if za <= mid <= zr), 0))
        pts.append((b, ZF))
        cur = b
    pts += [(r0, ZF - CH), (r0, zb)]
    mis += [0, 0, 0]
    return pts, mis


def slab_ring(name, r0, r1, zones, n, phase, seg_per, roles):
    pts, mis = slab_prof(r0, r1, zones)
    step = 360.0 / n
    half_gap = math.degrees(0.02 / ((r0 + r1) / 2)) / 2
    out = []
    for k in range(n):
        c = phase + k * step
        out.append(lathe("%s%d" % (name, k), pts, roles, seg=seg_per, phi0=c - step / 2 + half_gap,
                         phi1=c + step / 2 - half_gap, mis=mis, skip=(len(pts) - 1,)))
    return out


ROLES = ["slate", "hull", "hazard"]
parts += slab_ring("ringA", 1.52, 3.70, [(3.00, 3.25, 2)], 8, 0.0, 8, ROLES)
parts += slab_ring("ringB", 3.72, 7.28, [(6.96, 7.24, 2)], 16, 0.0, 6, ROLES)
# gunmetal blast plate in the centre, a ring of bolts
add(lathe("blast", [(1.50, ZG - SINK), (1.50, ZF - CH), (1.50 - CH, ZF), (0.0, ZF), (0.0, ZG - SINK)],
          ["pipe"], seg=48, skip=(4,)))
for k in range(16):
    p = pol(1.38, 11.25 + 22.5 * k)
    add(K.cyl("pbolt", (p.x, p.y, ZF - 0.004), (p.x, p.y, ZTOP - 0.001), 0.026, "metal", sides=6))

# ===================================================== touchdown marks
MARK = [(0.86, ZF - 0.006), (0.86, ZTOP - 0.004), (0.856, ZTOP), (0.70, ZTOP), (0.25, ZTOP),
        (0.0, ZTOP), (0.0, ZF - 0.006)]
for phi in LEGS:
    mk = [lathe("mark", MARK, ["slate", "hull"], seg=32, mis=[0, 0, 1, 0, 1, 0, 0], skip=(6,))]
    # ticks: inward and both sides (an outward one would cross the hazard ring)
    for (x0, y0, x1, y1) in ((-0.04, 0.92, 0.04, 1.12), (0.92, -0.04, 1.10, 0.04),
                             (-1.10, -0.04, -0.92, 0.04)):
        mk.append(prism("tick", [(x0, y0), (x1, y0), (x1, y1), (x0, y1)], ZF - 0.006, ZTOP, "hull"))
    for ob in mk:
        move(ob, (0.0, -R_FOOT, 0.0))
        rotz(ob, phi)
    parts += mk
    # chevron at r 4.30 pointing out to the mark (two halves meeting on the axis;
    # its tip stops 0.38 m short of the mark's inward tick)
    a, b, w, t = 4.30 + 0.22, 4.30 - 0.10, 0.32, 0.13
    for sgn in (-1, 1):
        poly = [(0.0, -a), (sgn * w, -b), (sgn * w, -b - t), (0.0, -a - t)]
        parts.append(rotz(prism("chev", poly, ZF - 0.006, ZTOP, "hull"), phi))

# =========================================================== drain grates
for phi in (0.0, 90.0, 180.0, 270.0):
    at = pol(5.30, phi)
    gob, gz, GM = flat_bezel("grate", (at.x, at.y), 1.00, 0.50, ZF - SINK, ZTOP - (ZF - SINK), 0.05,
                             0.035, 0.004, "metal", "rubber", phi=phi)
    add(gob)
    for i in range(8):
        x = -0.385 + 0.11 * i
        bar = K.box("gbar", (x - 0.014, -0.205, gz - 0.002), (x + 0.014, 0.205, gz + 0.0025), "metal",
                    bevel=0.0)
        bar.data.transform(GM @ Matrix.Translation((0.0, 0.0, -(ZF - SINK))))
        add(drop(bar, (0, 0, -1)))

# ========================================================= edge lights
for k in range(8):
    phi = 22.5 + 45.0 * k
    at = pol(7.415, phi)
    lob, _, _ = flat_bezel("light", (at.x, at.y), 0.56, 0.20, ZF - SINK, ZTOP - (ZF - SINK), 0.06,
                           0.045, 0.003, "slate", "glow", phi=phi)
    add(lob)
for k in range(16):
    p = pol(7.415, 11.25 + 22.5 * k)
    add(K.cyl("rbolt", (p.x, p.y, ZF - 0.004), (p.x, p.y, ZF + 0.004), 0.03, "metal", sides=6))

role_stats(parts)

# ================================================================== finish
ob = K.finish(parts, NAME)
shrink_faces(ob, lambda c, n, r: r == "rubber" and n.z > 0.99 and abs(c.z - ZG) < 0.003, 0.15,
             "gap floor")
repack(ob)
K.face_camera(ob)                          # (the pad is 4-fold symmetric: no visible change)
shift = recentre(ob)
assert max(abs(shift.x), abs(shift.y)) < 0.002 and abs(shift.z) < 1e-4, "pad not centred: %s" % shift
ztop = max(v.co.z for v in ob.data.vertices)
assert abs(ztop - ZTOP) < 1e-4, "pad top %.4f != %.3f: the ship would float or sink" % (ztop, ZTOP)
rh_hs.log("pad top (where the ship's feet stand) %.4f m; touchdown marks at r %.2f on the diagonals"
          % (ztop, R_FOOT))
os.makedirs(OUT, exist_ok=True)
bpy.ops.object.select_all(action="DESELECT")
ob.select_set(True)
bpy.context.view_layer.objects.active = ob
bpy.ops.export_scene.gltf(filepath=os.path.join(OUT, NAME + "_preview.glb"), export_format="GLB",
                          use_selection=True, export_apply=True)
rh_hs.log("preview glb written")


# ================================================================== scorch
def scorch_material(img):
    """Emission = scorch amount from object space: a smooth core round the
    engines, radial streaks and blotches, a few fingers reaching outward."""
    m = bpy.data.materials.new("scorch_bake")
    m.use_nodes = True
    nt = m.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    L = nt.links.new

    def node(kind, **kw):
        n = nt.nodes.new(kind)
        for k, v in kw.items():
            setattr(n, k, v)
        return n

    def op(operation, *ins, clamp=False):
        n = node("ShaderNodeMath", operation=operation, use_clamp=clamp)
        for i, v in enumerate(ins):
            if isinstance(v, (int, float)):
                n.inputs[i].default_value = v
            else:
                L(n.inputs[i], v)
        return n.outputs[0]

    out = node("ShaderNodeOutputMaterial")
    em = node("ShaderNodeEmission")
    L(out.inputs["Surface"], em.outputs["Emission"])
    tc = node("ShaderNodeTexCoord")
    sep = node("ShaderNodeSeparateXYZ")
    L(sep.inputs[0], tc.outputs["Object"])
    flat = node("ShaderNodeCombineXYZ")
    L(flat.inputs["X"], sep.outputs["X"])
    L(flat.inputs["Y"], sep.outputs["Y"])
    rad = node("ShaderNodeVectorMath", operation="LENGTH")
    L(rad.inputs[0], flat.outputs[0])
    R = rad.outputs["Value"]
    dirn = node("ShaderNodeVectorMath", operation="NORMALIZE")
    L(dirn.inputs[0], flat.outputs[0])
    dsc = node("ShaderNodeVectorMath", operation="SCALE")
    L(dsc.inputs[0], dirn.outputs[0])
    dsc.inputs["Scale"].default_value = 2.8
    rz = node("ShaderNodeCombineXYZ")
    L(rz.inputs["Z"], op("MULTIPLY", R, 0.30))
    sv = node("ShaderNodeVectorMath", operation="ADD")
    L(sv.inputs[0], dsc.outputs[0])
    L(sv.inputs[1], rz.outputs[0])
    streak = node("ShaderNodeTexNoise")
    streak.inputs["Scale"].default_value = 1.0
    streak.inputs["Detail"].default_value = 2.0
    L(streak.inputs["Vector"], sv.outputs[0])
    blot = node("ShaderNodeTexNoise")
    blot.inputs["Scale"].default_value = 0.45
    blot.inputs["Detail"].default_value = 2.0
    L(blot.inputs["Vector"], tc.outputs["Object"])
    core = node("ShaderNodeMapRange", interpolation_type="SMOOTHSTEP")
    core.inputs["From Min"].default_value = 0.8
    core.inputs["From Max"].default_value = 5.6
    core.inputs["To Min"].default_value = 1.0
    core.inputs["To Max"].default_value = 0.0
    L(core.inputs["Value"], R)
    inner = node("ShaderNodeMapRange", interpolation_type="SMOOTHSTEP")
    inner.inputs["From Min"].default_value = 0.3
    inner.inputs["From Max"].default_value = 2.4
    inner.inputs["To Min"].default_value = 1.0
    inner.inputs["To Max"].default_value = 0.0
    L(inner.inputs["Value"], R)
    fing = node("ShaderNodeMapRange")
    fing.inputs["From Min"].default_value = 2.5
    fing.inputs["From Max"].default_value = 7.0
    fing.inputs["To Min"].default_value = 1.0
    fing.inputs["To Max"].default_value = 0.0
    L(fing.inputs["Value"], R)
    st = streak.outputs["Fac"]
    # core * clamp(0.5 + 1.3 (streak - 0.5)) * (0.7 + 0.6 blot), and streak
    # fingers reaching outward: fing * clamp(4 (streak - 0.58)) * 0.75
    t1 = op("MULTIPLY_ADD", op("SUBTRACT", st, 0.5), 1.3, 0.5, clamp=True)
    t2 = op("MULTIPLY_ADD", blot.outputs["Fac"], 0.6, 0.7)
    s_core = op("MULTIPLY", op("MULTIPLY", core.outputs["Result"], t1), t2)
    s_fing = op("MULTIPLY", fing.outputs["Result"], op("MULTIPLY", op("SUBTRACT", st, 0.58), 4.0,
                                                      clamp=True))
    s_in = op("MULTIPLY", inner.outputs["Result"], op("MULTIPLY_ADD", blot.outputs["Fac"], 0.3, 0.7))
    s = op("MAXIMUM", op("MAXIMUM", s_core, s_in), op("MULTIPLY", s_fing, 0.75), clamp=True)
    L(em.inputs["Color"], s)
    tex = node("ShaderNodeTexImage")
    tex.image = img
    for n in nt.nodes:
        n.select = False
    tex.select = True
    nt.nodes.active = tex
    return m


def bake_scorch(ob, size):
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"
    sc.cycles.device = "CPU"
    sc.cycles.samples = 1
    sc.render.bake.margin = 8
    sc.render.bake.use_clear = True
    img = bpy.data.images.new("%s_scorch" % NAME, size, size, alpha=False, float_buffer=True)
    img.colorspace_settings.name = "Non-Color"
    m = scorch_material(img)
    originals = [s.material for s in ob.material_slots]
    for s in ob.material_slots:
        s.material = m
    bpy.ops.object.select_all(action="DESELECT")
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.object.bake(type="EMIT")
    a = np.empty(size * size * 4, dtype=np.float32)
    img.pixels.foreach_get(a)
    for s, mat in zip(ob.material_slots, originals):
        s.material = mat
    S = np.clip(a.reshape(size, size, 4)[..., 0:1], 0.0, 1.0)
    rh_hs.log("baked scorch: mean %.3f, %.1f %% of texels > 0.5" % (S.mean(), 100 * (S > 0.5).mean()))
    return S


if not PREVIEW_ONLY:
    SCORCH = bake_scorch(ob, SIZE)
    SOOT = np.array(rh_hs.hex_lin("#221C19"))
    _kit_save = rh_hs._save_png

    def _save_scorched(arr, path):
        """Hook on the kit's PNG writer: soot into BaseColor, rougher and less
        metallic soot into MetallicRoughness, before they are written and embedded."""
        k = 0.85 * SCORCH
        if path.endswith("_BaseColor.png"):
            a = np.clip(arr, 0.0, 1.0)
            lin = np.where(a <= 0.04045, a / 12.92, ((a + 0.055) / 1.055) ** 2.4)
            lin = lin * (1.0 - k) + SOOT * k
            arr = rh_hs.lin_to_srgb_np(lin)
        elif path.endswith("_MetallicRoughness.png"):
            arr = arr.copy()
            arr[..., 1:2] = np.clip(arr[..., 1:2] + 0.15 * k, 0.0, 1.0)
            arr[..., 2:3] = arr[..., 2:3] * (1.0 - 0.5 * k)
        return _kit_save(arr, path)

    rh_hs._save_png = _save_scorched
    files = K.bake_and_export(ob, NAME, OUT, size=SIZE, dust=0.12)
    rh_hs._save_png = _kit_save
    rh_hs.log("files: %s" % files)
