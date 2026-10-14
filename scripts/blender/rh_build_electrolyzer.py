"""Build the Electrolyzer from code - the hard-surface remake (2026-10-14).

  blender -b -P scripts/blender/rh_build_electrolyzer.py -- [--out Martians/gen/hs/Electrolyzer]
      [--size 2048] [--preview-only]

Game text: "Splits water into breathable oxygen and hydrogen fuel. A power hog by
design - feed it well." Replaces the image-to-3D Electrolyzer, which turned the
approved design into a purple melted blob (Saved/RHLineup/png/
Models2__Electrolyzer__v035.png: puffy bottles, a smeared readout, the whole
hull tinted in its TintFor violet). The DESIGN is kept: the approved hero
reference Martians/gen/electrolyzer/cutout/electrolyzer.png - a bone-white
equipment cabinet with chamfered edges and a tall vertical violet readout on its
front, flanked by FOUR tall grey gas cylinders with domed tops (two O2 with a
blue band, two H2 with a red band, strap bands, bracketed to the cabinet),
inverted-U pipes with a violet stripe running from each cylinder valve into the
cabinet top, everything on a heavy slotted pallet with feet. Only the
construction changes: every part is an exact primitive, so nothing can melt,
and the emissive mask is the readout and the status LEDs themselves.

Layout facts:
  - built with the readout face at -Y, then K.face_camera() turns it to -X, the
    face the game's default strategy camera meets (measured 2026-10-13). The O2
    pair stands on the viewer's left (-X before the turn), the H2 pair on the
    right, as on the reference; each pair is a front and a back bottle.
  - plan 2.90 x 2.20 m (1.32 : 1, the pallet sets both extents), height ~3.1 m.
    The visualizer fits the larger horizontal extent to the footprint (min-side
    fit; see the note in the final report on what that footprint is) and seats
    the bounds bottom on the ground, so only proportions matter. The reference
    is TALLER (~1.4 x its pallet length, measured on the sheet); the cylinders
    and cabinet were kept tall (bottles ~3.8 x their diameter) and the U-pipes
    took the cut, so the asset stays a squat-enough block from the 54 deg
    strategy camera without losing the bottle read.
  - palette (art bible 1): bone-white hull cabinet and top lid; DarkSlate lower
    plinth, strap bands, brackets, base cups, top heat vent and the pallet; a
    mid-grey "tank" role for the bottles (darker and cooler than the hull, so
    cabinet and bottles separate - the reference's grey cylinders); light-alloy
    pipes with slate sleeves. Round 1 had a slate top plate: from the 54 deg
    strategy camera (which sees mostly the top) it read as a black slab; the
    reference's top is light, so the lid is hull now, framed in violet, with a
    small slate heat vent between the four inlets.
  - O2 / H2 colour bands are full rings 28 cm tall, 14 mm proud of the bottle
    (rule 7: >= 3 mm), with raised bone-white "O2" / "H2" glyphs (subscript 2)
    turned 15 deg outward from the front / back. Round 1's 22 cm bands were thin
    from 54 deg; the colour bands must read at gameplay zoom, the letters are a
    close-zoom bonus.
  - accent (#C4A0F3, TintFor Electrolyzer) is TRIM only - the "gas path":
    the readout bezel, the stripe along every U-pipe (the reference's violet
    piping, r 28 mm, 18 mm proud), the bottle valve collars, the cabinet inlet
    flanges, the top lid rim, a seam band round the cabinet where the hull meets
    the slate plinth, the side vent frames and the back service-panel frame and
    handle. Measured first-hit raster share: 6.1 % from the default camera
    (yaw 180, pitch 54), 5.0-5.2 % from the lineup orbit, 4.7 % straight down
    (the slate pallet dominates the top view); 10.7 % of all faces. Round 2
    framed the side vents and back panel in violet: the back three-quarter was
    at 4.1 %.
  - lit role "glow" (#B58CFF) matches the in-game violet EmissiveColor (~2.6,
    1.3, 5.0 linear): the readout's lit fill (a level, lit up to 55 %, over a
    dim violet-grey track with tick marks) and three status LEDs beside it. The
    bezel is 27 x 136 cm (round 1: 24 x 130) with an 11 cm wide fill, so the
    readout survives the 54 deg foreshortening and a 128 px thumbnail.
    The readout window is wide dark glass, so the lit fill never merges with its
    same-hue bezel.
  - "power hog": a slate junction box with a hazard band and a thick black
    power conduit on the back face, running down into a socket on the pallet.
  - pallet: one tray solid (rim + recessed deck, 8 cm) with fork tunnels cut
    by exact booleans - two through the long sides, one through the short
    sides, 11 cm tall - on six slate feet, hazard plates on the front corners.
  - no coplanar faces: decals and plates stand >= 3 mm proud; the violet
    stripes stop 1 cm short of their pipe ends (round 1 had their end caps
    coplanar with the pipe caps inside the valve / nut); the lid sits 5 mm
    above the rim plate's buried underside.
  - ~14.4k triangles: strap and gas bands are plain 3-segment rings (their 4 mm
    chamfers were invisible and cost 40 % more), side-vent slats unbevelled.
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
OUT = argv[argv.index("--out") + 1] if "--out" in argv else "Martians/gen/hs/Electrolyzer"
SIZE = int(argv[argv.index("--size") + 1]) if "--size" in argv else 2048
PREVIEW_ONLY = "--preview-only" in argv
NAME = "Electrolyzer"
TAU = 2.0 * math.pi

K = rh_hs.Kit(accent_hex="#C4A0F3")       # TintFor(Electrolyzer), art bible 2.3
# Runtime roles (this asset only):
#  tank     - the gas bottles: painted steel, mid grey, cooler and darker than the
#             BoneWhite cabinet so the two masses separate at gameplay zoom
#  o2 / h2  - the gas colour bands (blue = oxygen, red = hydrogen), painted
#  glow     - lit violet readout / LEDs; albedo matches the in-game EmissiveColor
#  cell_dim - the unlit part of the readout: visibly lighter than the black glass
for _name, _spec in (
        ("tank", dict(albedo="#8E939A", metal=0.35, rough=0.45, paint=1, dust=0.9)),
        ("o2", dict(albedo="#3567B0", metal=0.10, rough=0.50, paint=1, dust=0.6)),
        ("h2", dict(albedo="#B83A32", metal=0.10, rough=0.50, paint=1, dust=0.6)),
        ("glow", dict(albedo="#B58CFF", metal=0.0, rough=0.25, emit=1, dust=0.0)),
        ("cell_dim", dict(albedo="#3E3852", metal=0.0, rough=0.30, dust=0.0))):
    K.roles[_name] = _spec
    K.mats[_name] = K._preview_material(_name, _spec)
# preview emission = the game's EmissiveColor normalised, low enough that review
# frames show violet instead of clipping to white (the kit default is teal x4)
_gb = K.mats["glow"].node_tree.nodes["Principled BSDF"]
_gb.inputs["Emission Color"].default_value = (0.52, 0.26, 1.0, 1.0)
_gb.inputs["Emission Strength"].default_value = 1.4

parts = []


def add(o):
    parts.append(o)
    return o


# ------------------------------------------------------------ local helpers
def convex_block(name, planes, role, bevel=0.012, seg=2):
    """Closed convex solid = intersection of half-spaces n.p <= d, bevelled.
    One watertight hull however many chamfers meet at a corner (AirFilter)."""
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


def cabinet_planes(wh, dh, zb, top, ch_v, ch_st=None, ch_ft=None, ch_bt=None):
    s2 = math.sqrt(0.5)
    pl = [((1, 0, 0), wh), ((-1, 0, 0), wh), ((0, 1, 0), dh), ((0, -1, 0), dh),
          ((0, 0, 1), top), ((0, 0, -1), -zb)]
    for ex, ey in ((1, 1), (1, -1), (-1, 1), (-1, -1)):
        pl.append(((ex * s2, ey * s2, 0), s2 * (wh + dh - ch_v)))
    if ch_st:
        for ex in (1, -1):
            pl.append(((ex * s2, 0, s2), s2 * (wh + top - ch_st)))
    if ch_ft:
        pl.append(((0, -s2, s2), s2 * (dh + top - ch_ft)))
    if ch_bt:
        pl.append(((0, s2, s2), s2 * (dh + top - ch_bt)))
    return pl


def bezel(name, center, w, h, proud, radius, rim, recess, rim_role, inner_role, facing=-1):
    """K.bezel with its failure modes refused (rim >= radius - 5 mm folds or
    collapses the window corners - AirFilter round 1)."""
    assert radius - rim >= 0.005, "%s: bezel rim %.3f must be < radius %.3f - 5 mm" % (
        name, rim, radius)
    return K.bezel(name, center, w, h, proud, radius=radius, rim=rim, recess=recess,
                   rim_role=rim_role, inner_role=inner_role, facing=facing)


def zlathe(name, prof, roles, centre=(0.0, 0.0), sides=28, seg_roles=None, closed=False,
           cap_top=False):
    """Revolve an (r, z) profile about a vertical axis through `centre`. Trace it
    so the solid is on the left going outward/upward (outer wall up, tops inward)
    and normals face out. Open profiles end inside a neighbouring part (no
    buried caps taking UV space); closed=True joins last -> first."""
    cx, cy = centre
    bm = bmesh.new()
    rings = []
    for (r, z) in prof:
        if r < 1e-6:
            v = bm.verts.new((cx, cy, z))
            rings.append([v] * sides)
        else:
            rings.append([bm.verts.new((cx + r * math.cos(TAU * k / sides),
                                        cy + r * math.sin(TAU * k / sides), z))
                          for k in range(sides)])
    nseg = len(prof) if closed else len(prof) - 1
    for i in range(nseg):
        a, b = rings[i], rings[(i + 1) % len(prof)]
        for k in range(sides):
            k2 = (k + 1) % sides
            vs = []
            for v in (a[k], a[k2], b[k2], b[k]):
                if v not in vs:
                    vs.append(v)
            if len(vs) < 3:
                continue
            f = bm.faces.new(vs)
            f.material_index = seg_roles[i] if seg_roles else 0
    if cap_top and prof[-1][0] > 1e-6:
        f = bm.faces.new(rings[-1])
        f.material_index = seg_roles[-1] if seg_roles else 0
    if closed:
        bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    return K._obj(name, bm, roles)


def ring_band(name, centre, r_in, r_out, z0, z1, role, sides=28):
    """Open-profile band round a vertical cylinder: starts and ends at r_in
    (inside the wall it hugs), outer face r_out. No edge chamfers: at 4 mm they
    are invisible at game scale and cost 40 % more triangles."""
    return zlathe(name, [(r_in, z0), (r_out, z0), (r_out, z1), (r_in, z1)], [role],
                  centre=centre, sides=sides)


def text_block(name, body, centre, size, y_front, y_back, role, offset=0.0, spacing=1.0):
    """Raised lettering on a -Y-facing plane (AirFilter): builtin font, one solid
    per glyph, front plane y_front, back y_back, centred on (centre.x, centre.z)."""
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


def xbounds(ob):
    xs = [v.co.x for v in ob.data.vertices]
    return min(xs), max(xs)


def place(objs, M):
    for o in objs:
        o.data.transform(M)
    return objs


def turn(objs, deg, axis="Z"):
    return place(objs, Matrix.Rotation(math.radians(deg), 4, axis))


def boolean_cut(ob, cutters, role):
    """Exact boolean DIFFERENCE of every cutter from ob, applied; the cutters are
    deleted. The result is single-role (every face gets `role`)."""
    for c in cutters:
        m = ob.modifiers.new("cut_" + c.name, "BOOLEAN")
        m.operation = "DIFFERENCE"
        m.solver = "EXACT"
        m.object = c
    dg = bpy.context.evaluated_depsgraph_get()
    me = bpy.data.meshes.new_from_object(ob.evaluated_get(dg))
    ob.modifiers.clear()
    old = ob.data
    ob.data = me
    bpy.data.meshes.remove(old)
    for c in cutters:
        cm = c.data
        bpy.data.objects.remove(c)
        bpy.data.meshes.remove(cm)
    me.materials.clear()
    me.materials.append(K.mats[role])
    for p in me.polygons:
        p.material_index = 0
    return ob


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
    """Surface share per role: all faces, and weighted toward the readout side."""
    tot, front = {}, {}
    for o in objs:
        me = o.data
        for p in me.polygons:
            r = me.materials[p.material_index]["rh_role"]
            tot[r] = tot.get(r, 0.0) + p.area
            front[r] = front.get(r, 0.0) + p.area * max(0.0, -p.normal.y)
    for name, d in (("all faces", tot), ("seen from readout side", front)):
        s = sum(d.values())
        rh_hs.log("role share (%s): %s" % (name, ", ".join(
            "%s %.1f%%" % (k, 100 * v / s) for k, v in sorted(d.items(), key=lambda kv: -kv[1]))))


# ------------------------------------------------------------------ layout
SINK = 0.003                               # frames/bezels bite 3 mm into their support
PX, PY = 1.45, 1.10                        # pallet half extents (x, y)
Z_TRAY0, Z_TRAY1, Z_DECK = 0.10, 0.36, 0.28  # tray underside, rim top, recessed deck
RIM = 0.15                                 # tray rim width
CW, CD = 0.53, 0.75                        # cabinet half extents (x, y)
Z_SEAM, TOP = 0.80, 2.60                   # hull cabinet underside, top
YF = -CD                                   # cabinet front plane
CYL_X, CYL_Y, RC = 0.88, 0.45, 0.30        # bottle centres (+-x, +-y), radius
Z_WALL1, H_DOME = 2.31, 0.24               # top of the straight wall, dome height
PIPE_Z, PIPE_R, BEND = 3.03, 0.065, 0.16   # U-pipe crown centreline, radius, bend
INLET_X = 0.22                             # cabinet-top pipe inlets at (+-INLET_X, +-CYL_Y)
STRIPE_D, STRIPE_R = 0.055, 0.028          # violet stripe: offset from pipe axis, radius
Z_VALVE0, Z_VALVE1 = 2.60, 2.68            # bottle valve body
Z_TOPPLATE = 2.645                         # top plate upper face
GB0, GB1 = 1.47, 1.75                      # O2 / H2 colour band
GZ = (GB0 + GB1) / 2                       # label centre height

# ======================================================== pallet / skid base
# One tray solid: outer block, top face inset by the rim width and pushed down
# to the deck, every edge bevelled; then fork tunnels cut by exact booleans.
bm = bmesh.new()
bmesh.ops.create_cube(bm, size=1.0)
for v in bm.verts:
    v.co = Vector((v.co.x * 2 * PX, v.co.y * 2 * PY, Z_TRAY0 + (v.co.z + 0.5) * (Z_TRAY1 - Z_TRAY0)))
bm.faces.ensure_lookup_table()
top_f = max(bm.faces, key=lambda f: f.calc_center_median().z)
bmesh.ops.inset_individual(bm, faces=[top_f], thickness=RIM, depth=0.0)
ext = bmesh.ops.extrude_discrete_faces(bm, faces=[top_f])
for v in ext["faces"][0].verts:
    v.co.z = Z_DECK
bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
bmesh.ops.bevel(bm, geom=list(bm.edges), offset=0.012, offset_type="OFFSET", segments=2,
                profile=0.5, affect="EDGES", clamp_overlap=True)
tray = K._obj("tray", bm, ["slate"])
ZT0, ZT1 = 0.14, 0.25                      # fork tunnel floor / ceiling (deck slab 3 cm)
cutters = [K.box("cutY%d" % s, (min(s * 0.45, s * 0.95), -PY - 0.2, ZT0),
                 (max(s * 0.45, s * 0.95), PY + 0.2, ZT1), "slate", bevel=0.0)
           for s in (-1, 1)]
cutters.append(K.box("cutX", (-PX - 0.2, -0.55, ZT0), (PX + 0.2, 0.55, ZT1), "slate", bevel=0.0))
add(boolean_cut(tray, cutters, "slate"))
# feet: four corners + one under the middle of each long side, 1.5 cm into the tray
for fx0, fx1 in ((-1.42, -1.10), (-0.22, 0.22), (1.10, 1.42)):
    for sy in (-1, 1):
        ya, yb = sorted((sy * 0.80, sy * 1.07))
        add(K.box("foot", (fx0, ya, 0.0), (fx1, yb, Z_TRAY0 + 0.015), "slate", bevel=0.015))
# hazard plates on the front corners of the rim face (6 mm proud)
for sx in (-1, 1):
    xa, xb = sorted((sx * 1.12, sx * 1.40))
    add(K.box("haz", (xa, -PY - 0.006, 0.275), (xb, -PY + 0.004, 0.335), "hazard",
              bevel=0.002, seg=1))

# ============================================================ the cabinet
# hull upper body: chamfered vertical edges, big side-top chamfers, small
# front-top and back-top chamfers (the reference's faceted top)
add(convex_block("cabinet", cabinet_planes(CW, CD, Z_SEAM, TOP, 0.07, ch_st=0.14, ch_ft=0.05,
                                           ch_bt=0.10), "hull", bevel=0.012))
# slate plinth below it (inset 3 cm), from the deck
add(convex_block("plinth", cabinet_planes(CW - 0.03, CD - 0.03, Z_DECK - 0.01, Z_SEAM + 0.02, 0.08),
                 "slate", bevel=0.01))
# violet seam band where hull meets plinth (8 mm proud of the hull)
add(convex_block("seam", cabinet_planes(CW + 0.008, CD + 0.008, Z_SEAM - 0.035, Z_SEAM + 0.045,
                                        0.07), "accent", bevel=0.004, seg=1))

# ----- readout: violet bezel, dark glass, lit fill over a dim track, ticks
RZ, RW_, RH = 1.62, 0.27, 1.36
bz, wy = bezel("readout_bezel", (0.0, YF + SINK, RZ), RW_, RH, 0.03 + SINK, radius=0.07,
               rim=0.04, recess=0.02, rim_role="accent", inner_role="glass")
add(bz)
IN_LO, IN_HI = RZ - RH / 2 + 0.04, RZ + RH / 2 - 0.04      # window z range
LEVEL = 0.55
z_fill = IN_LO + 0.03 + LEVEL * (IN_HI - IN_LO - 0.06)
add(K.box("readout_fill", (-0.055, wy - 0.007, IN_LO + 0.03), (0.055, wy + 0.003, z_fill), "glow",
          bevel=0.003, seg=1))
add(K.box("readout_track", (-0.040, wy - 0.0035, z_fill - 0.02), (0.040, wy + 0.003, IN_HI - 0.03),
          "cell_dim", bevel=0.002, seg=1))
for k in range(6):                         # tick marks beside the track
    zt = z_fill + 0.06 + k * (IN_HI - 0.08 - z_fill - 0.06) / 5
    add(K.box("tick", (-0.086, wy - 0.004, zt - 0.009), (-0.058, wy + 0.003, zt + 0.009), "hull",
              bevel=0.002, seg=1))
# status LEDs on a slate plate to the right of the readout top
add(K.box("led_plate", (0.19, YF - 0.012, 1.92), (0.30, YF + 0.004, 2.25), "slate", bevel=0.006))
for k in range(3):
    zl = 2.17 - k * 0.09
    add(K.box("led", (0.215, YF - 0.018, zl - 0.022), (0.275, YF - 0.008, zl + 0.022), "glow",
              bevel=0.003, seg=1))
for bx in (-0.40, 0.40):                   # corner screws (reference)
    for bzz in (0.90, 2.42):
        add(K.bolt("fbolt", (bx, YF + 0.001, bzz), (0, -1, 0), r=0.016))

# ----- plinth front: louvred intake, slate frame, dark window, hull slats
PF = -(CD - 0.03)                          # plinth front plane
lv, lwy = bezel("intake", (0.0, PF + SINK, 0.545), 0.72, 0.30, 0.025 + SINK, radius=0.045,
                rim=0.035, recess=0.02, rim_role="slate", inner_role="rubber")
add(lv)
for k in range(4):                         # slats 18 mm thick (rule 6), 4 mm into the walls
    zc = 0.545 - 0.115 + 0.0575 * (k + 0.5)
    o = K.box("islat", (-0.329, -0.018, -0.009), (0.329, 0.018, 0.009), "hull", bevel=0.002, seg=1)
    o.data.transform(Matrix.Translation((0.0, lwy - 0.006, zc)) @ Matrix.Rotation(math.radians(35.0), 4, "X"))
    add(o)

# ----- top: violet rim plate under a slate plate, four violet inlet flanges
add(K.box("top_rim", (-0.38, -0.66, TOP - 0.01), (0.38, 0.62, TOP + 0.025), "accent", bevel=0.006,
          seg=1))
add(K.box("top_plate", (-0.35, -0.63, TOP - 0.005), (0.35, 0.59, Z_TOPPLATE), "hull", bevel=0.008))
# heat vent between the inlets (a power hog runs hot): slate box, dark slots
add(K.box("top_vent", (-0.085, -0.30, Z_TOPPLATE - 0.01), (0.085, 0.30, Z_TOPPLATE + 0.04), "slate",
          bevel=0.01))
for k in range(5):
    yv = -0.22 + k * 0.11
    add(K.box("vent_slot", (-0.06, yv - 0.022, Z_TOPPLATE + 0.03), (0.06, yv + 0.022,
                                                                     Z_TOPPLATE + 0.044),
              "rubber", bevel=0.003, seg=1))
for sx in (-1, 1):
    for sy in (-1, 1):
        p = (sx * INLET_X, sy * CYL_Y)
        add(K.cyl("inlet_flange", (*p, Z_TOPPLATE - 0.008), (*p, Z_TOPPLATE + 0.022), 0.115, "accent",
                  sides=20))
        add(K.cyl("inlet_nut", (*p, Z_TOPPLATE + 0.02), (*p, Z_TOPPLATE + 0.075), 0.10, "metal",
                  sides=6))

# ----- sides: a narrow louvre in the gap between each pair of bottles
for sx in (-1, 1):
    side = []
    sb, swy = bezel("svent", (0.0, -CW + SINK, 1.66), 0.22, 0.92, 0.022 + SINK, radius=0.04,
                    rim=0.03, recess=0.018, rim_role="accent", inner_role="rubber")
    side.append(sb)
    for k in range(7):
        zc = 1.66 - 0.43 + (0.86 / 7) * (k + 0.5)
        o = K.box("sslat", (-0.084, -0.018, -0.009), (0.084, 0.018, 0.009), "hull", bevel=0.0,
                  seg=1)
        o.data.transform(Matrix.Translation((0.0, swy - 0.005, zc)) @
                         Matrix.Rotation(math.radians(35.0), 4, "X"))
        side.append(o)
    parts += turn(side, 90.0 if sx > 0 else -90.0)

# ----- back: service panel with a violet handle, power junction box + conduit
back = []
bp, bwy = bezel("svc_panel", (0.0, CD - SINK, 1.95), 0.70, 0.70, 0.03 + SINK, radius=0.05,
                rim=0.04, recess=0.02, rim_role="accent", inner_role="hull", facing=1)
back.append(bp)
hb, _ = bezel("svc_handle", (0.0, bwy - SINK, 2.18), 0.26, 0.08, 0.03 + SINK, radius=0.035,
              rim=0.024, recess=0.018, rim_role="accent", inner_role="rubber", facing=1)
back.append(hb)
for bx in (-0.33, 0.33):                    # on the rim's rounded corners, clear of the window
    for bzz in (1.62, 2.28):
        back.append(K.bolt("bbolt", (bx, CD + 0.03, bzz), (0, 1, 0), r=0.013))
JX0, JX1, JZ0, JZ1 = -0.36, 0.06, 0.98, 1.44
back.append(K.box("jbox", (JX0, CD - 0.01, JZ0), (JX1, CD + 0.15, JZ1), "slate", bevel=0.015))
back.append(K.box("jhaz", (JX0 + 0.03, CD + 0.146, JZ1 - 0.11), (JX1 - 0.03, CD + 0.156, JZ1 - 0.04),
                  "hazard", bevel=0.002, seg=1))
for bx in (JX0 + 0.04, JX1 - 0.04):
    back.append(K.bolt("jbolt", (bx, CD + 0.15, JZ0 + 0.05), (0, 1, 0), r=0.013))
CX = -0.15                                  # power conduit: box -> down -> socket on the rim
back.append(K.pipe("conduit", [(CX, CD + 0.075, JZ0 + 0.03), (CX, CD + 0.075, 0.40),
                               (CX, 1.03, 0.40)], 0.055, "rubber", bend=0.12, sides=12))
back.append(K.box("socket", (CX - 0.10, 0.97, Z_TRAY1 - 0.01), (CX + 0.10, 1.08, 0.46), "slate",
                  bevel=0.012))
parts += back

# ============================================================ gas bottles
ZC0 = Z_DECK + 0.01                         # bottle wall starts inside the cup
DOME_T = [math.radians(a) for a in (0, 15, 30, 45, 60, 72)]
for sx in (-1, 1):
    gas = "o2" if sx < 0 else "h2"
    for sy in (-1, 1):
        c = (sx * CYL_X, sy * CYL_Y)
        # base cup on the deck, open profile ending inside the bottle wall
        add(zlathe("cup", [(RC + 0.035, Z_DECK - 0.01), (RC + 0.035, 0.375), (RC + 0.015, 0.395),
                           (RC - 0.005, 0.395)], ["slate"], centre=c))
        # bottle: straight wall + elliptical dome, open top hidden in the neck
        prof = [(RC - 0.015, ZC0), (RC, ZC0 + 0.015), (RC, Z_WALL1)]
        prof += [(RC * math.cos(t), Z_WALL1 + H_DOME * math.sin(t)) for t in DOME_T[1:]]
        add(zlathe("bottle", prof, ["tank"], centre=c))
        # strap bands (slate) and the gas colour band
        for zb in (0.92, 1.96):
            add(ring_band("strap", c, RC - 0.005, RC + 0.02, zb - 0.04, zb + 0.04, "slate"))
        add(ring_band("gasband", c, RC - 0.005, RC + 0.014, GB0, GB1, gas))
        # brackets tying the bottle to the cabinet side at the strap heights
        for zb in (0.92, 1.96):
            xa, xb = sorted((sx * (CW - 0.02), sx * (CYL_X - RC + 0.03)))
            add(K.box("bracket", (xa, c[1] - 0.06, zb - 0.045), (xb, c[1] + 0.06, zb + 0.045), "slate",
                      bevel=0.008))
        # valve stack: slate neck, hex valve, violet collar
        add(K.cyl("neck", (*c, 2.50), (*c, Z_VALVE0 + 0.01), 0.11, "slate", sides=20))
        add(K.cyl("valve", (*c, Z_VALVE0), (*c, Z_VALVE1), 0.10, "metal", sides=6))
        add(K.cyl("vcollar", (*c, Z_VALVE1 - 0.005), (*c, Z_VALVE1 + 0.04), 0.09, "accent",
                  sides=16))
        # inverted-U pipe from the valve over to the cabinet inlet, violet stripe
        # along its outer side, slate sleeves at the four bend tangent points
        xo, xi = sx * CYL_X, sx * INLET_X
        y = c[1]
        add(K.pipe("upipe", [(xo, y, Z_VALVE0 + 0.04), (xo, y, PIPE_Z), (xi, y, PIPE_Z),
                             (xi, y, Z_TOPPLATE + 0.055)], PIPE_R, "metal", bend=BEND, sides=16))
        d = STRIPE_D
        add(K.pipe("stripe", [(xo + sx * d, y, Z_VALVE0 + 0.05), (xo + sx * d, y, PIPE_Z + d),
                              (xi - sx * d, y, PIPE_Z + d), (xi - sx * d, y, Z_TOPPLATE + 0.045)],
                   STRIPE_R, "accent", bend=BEND + d, sides=8))
        for p0, p1 in (((xo, y, PIPE_Z - BEND - 0.03), (xo, y, PIPE_Z - BEND + 0.03)),
                       ((xi, y, PIPE_Z - BEND - 0.03), (xi, y, PIPE_Z - BEND + 0.03)),
                       ((xo - sx * (BEND - 0.03), y, PIPE_Z), (xo - sx * (BEND + 0.03), y, PIPE_Z)),
                       ((xi + sx * (BEND - 0.03), y, PIPE_Z), (xi + sx * (BEND + 0.03), y, PIPE_Z))):
            add(K.cyl("sleeve", p0, p1, 0.088, "slate", sides=14))
        # raised glyphs on the band's outward quarter: O2 / H2 with subscript 2
        # (built facing -Y at the origin, then swung to face out of the pair)
        RB = RC + 0.014
        main = text_block("lbl_" + gas, "O" if gas == "o2" else "H", (0.0, 0.0, GZ + 0.018), 0.175,
                          -(RB + 0.008), -(RB - 0.03), "hull", offset=0.004)
        mx0, mx1 = xbounds(main)
        sub = text_block("lbl_sub", "2", (0.0, 0.0, GZ - 0.04), 0.10, -(RB + 0.008), -(RB - 0.03),
                         "hull", offset=0.003)
        sx0, sx1 = xbounds(sub)
        sub.data.transform(Matrix.Translation((mx1 + 0.012 - sx0, 0.0, 0.0)))
        shift = -(mx0 + (mx1 + 0.012 + (sx1 - sx0))) / 2           # centre the pair
        # 15 deg outward: central enough for the 54 deg default camera (30 deg put
        # the front glyphs on the bottles' silhouette edges), still read in 3/4
        ang = (-15.0 if sx < 0 else 15.0) if sy < 0 else (-165.0 if sx < 0 else 165.0)
        place([main, sub], Matrix.Translation((c[0], c[1], 0.0)) @
              Matrix.Rotation(math.radians(ang), 4, "Z") @ Matrix.Translation((shift, 0.0, 0.0)))
        add(main)
        add(sub)

role_stats(parts)
_tri = {}
for _o in parts:
    _k = _o.name.split(".")[0]
    _tri[_k] = _tri.get(_k, 0) + sum(len(p.vertices) - 2 for p in _o.data.polygons)
rh_hs.log("tris by part: %s" % ", ".join("%s %d" % kv for kv in sorted(_tri.items(), key=lambda kv: -kv[1])[:14]))

# ------------------------------------------------------------------ finish
ob = K.finish(parts, NAME)
K.face_camera(ob)                          # readout face -Y -> -X (default camera)
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
