"""Build the CommandModule from code - the hard-surface remake (2026-10-13).

  blender -b -P scripts/blender/rh_build_commandmodule.py -- [--out Martians/gen/hs/CommandModule]
      [--size 2048] [--preview-only]

The game's ComputeModule ("Lander Compute Module: a Lander upgrade that shortens
your command lag"). Replaces the image-to-3D CommandModule the director flagged
"weird" on the 2026-10-13 triage: a puffy pink blob - melted panels, a shredded
roof, the antenna a single fused spike, the whole hull tinted in its TintFor
magenta. The DESIGN is kept: the approved identity-anchored Nano Banana sheet,
Martians/gen/objects-redo/CommandModule/cutout/CommandModule_*.png (five views) -
a squat round domed pod in bands (dark-slate skirt, RustBeam band, bone-white
wall panels under a dashed hazard stripe, a ring of framed dark windows, a
panelled conical shell, a rust collar and a windowed cupola), a framed hatch
door, and a tall boxy service module on the hatch's LEFT flank, wrapped where
it meets the pod by a slate strap with RustBeam edges that bends over the shell
into the cupola collar, carrying the antenna mast and two dishes. Only the
construction changes: every part is an exact
primitive, so nothing can melt or shred, and the emissive mask is the status
lights themselves.

Why the module sits on the flank, not the back (revision 1): on the sheet's
back view the hatch frame shows in profile at the right edge and on the right
view module and hatch are a quarter turn apart. With the module behind the
hatch the default view was a symmetric white dome with a cupola - the Lander's
silhouette. On the flank, the default view shows hatch + strapped module +
dishes, which is what separates this asset from the Lander.

Accent (TintFor #F389C4) is TRIM only, placed on surfaces the 54 deg default
camera actually sees: the window-ring frames, the cupola base ring, two ID
plates beside the hatch, a marker on the strap, the module's end-hatch rim and
small markers on the module (measured 5-7 % of the visible surface).
The glow is ONE hue per asset in game (magenta status lights, EmissiveColor ~
(2.2, 0.55, 1.25) linear), so every lit element - the light ring on the collar,
the beacon on the cupola lid, the bar over the hatch, the antenna tips, the
module LEDs - uses the "glow" role whose albedo matches that hue.

Layout facts: built with the hatch at -Y and the service module at -X (pol
azimuth -90), then K.face_camera() turns the hatch to -X, the face the strategy
camera's default view meets (measured 2026-10-13), which puts the module on the
player's left. The window ring is a 14-gon of flat facets so each framed window
sits flush; windows are skipped only on the arc the module covers. Real-world
metres: pod 2.34 m across, cupola lid ~2.03 m, beacon ~2.10 m, antenna tips to
~2.62 m; pod + module make the plan ~2.71 m long (Y after face_camera, 2.50 m
along X), so the visualizer's fit (larger horizontal extent -> 2 m footprint)
is ~0.74x and the pod ends up ~1.73 m wide in a 2 m cell. The pod axis sits
~0.19 m off the bounds centre, away from the module (bounds are centred, the
design is asymmetric). Ground-centred pivot: bounds bottom at z = 0, bounds
centred on XY. Rod-like parts (antenna masts, grab handles, dish booms) are
>= 3 cm radius and pipes >= 2.8 cm at build scale so they stay >= 2 cm after
the fit; the HazYellow step nosing is 3.2 cm tall. Every decal, light and
plate stands >= 4 mm proud of the faceted surface it sits on (hazard stripes
~4 mm after chord sag, the rest 6-11 mm; ID plates are 4-facet arcs so the
chord cannot sag into the wall).
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rh_hs  # noqa: E402

import bmesh  # noqa: E402
import bpy  # noqa: E402
from mathutils import Matrix, Vector  # noqa: E402

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OUT = argv[argv.index("--out") + 1] if "--out" in argv else "Martians/gen/hs/CommandModule"
SIZE = int(argv[argv.index("--size") + 1]) if "--size" in argv else 2048
PREVIEW_ONLY = "--preview-only" in argv
NAME = "CommandModule"

K = rh_hs.Kit(accent_hex="#F389C4")       # TintFor(ComputeModule), art bible 2.3
# Lit role: magenta status light. Albedo matches the in-game glow hue so the
# lights still read as magenta in daylight. Preview emission = the game's
# EmissiveColor normalised, at a strength low enough that review frames show
# magenta instead of clipping to white (the kit's preview default is teal x4).
K.roles["glow"] = dict(albedo="#FF7AC8", metal=0.0, rough=0.25, emit=1, dust=0.0)
K.mats["glow"] = K._preview_material("glow", K.roles["glow"])
_gb = K.mats["glow"].node_tree.nodes["Principled BSDF"]
_gb.inputs["Emission Color"].default_value = (1.0, 0.25, 0.57, 1.0)
_gb.inputs["Emission Strength"].default_value = 1.2

parts = []
add = parts.append

MA = -90.0                                    # service-module azimuth: the hatch's left flank
MOD_HALF_ARC = 52.0                           # +- degrees of pod the module + strap cover


def on_module(phi, half=MOD_HALF_ARC):
    """True for azimuths hidden behind the service module and its strap."""
    return abs(((phi - MA + 180.0) % 360.0) - 180.0) < half


# ------------------------------------------------------------------ helpers
def pol(r, phi, z):
    """Polar -> xyz with phi in degrees measured from the FRONT (-Y), +phi toward +X."""
    a = math.radians(phi)
    return Vector((r * math.sin(a), -r * math.cos(a), z))


def lathe(name, prof, roles, seg=48, phi0=0.0, phi1=None, closed=True, mis=None, phase=0.0):
    """Revolve an (r, z) profile about Z. prof is authored counter-clockwise in the
    (r, z) half-plane (outer wall going up, top going inward) so normals face out.
    phi1=None -> full ring (r = 0 points become poles); otherwise an arc panel
    phi0..phi1 whose two ends are capped with the profile polygon.
    closed=False -> OUTER-ONLY ring: only the listed profile edges are built (no
    closing edge, no caps), for rings whose inner/under faces are buried in
    other parts; winding follows the CCW authoring (verified by the back-face
    ray check). mis: material index per profile edge. Full rings place their
    first vertex at azimuth `phase`."""
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


def panels(name, prof, role, n, gap, r_gap, phase=0.0, seg_per=6, skip=None):
    """n arc panels of one profile around Z with `gap` metres of seam (at radius
    r_gap) between them; panel k is centred on phase + k*360/n. skip(phi) drops
    panels (e.g. ones buried under the service module)."""
    out = []
    step = 360.0 / n
    half_gap = math.degrees(gap / r_gap) / 2.0
    for k in range(n):
        c = phase + k * step
        if skip and skip(((c + 180.0) % 360.0) - 180.0):
            continue
        out.append(lathe("%s%d" % (name, k), prof, [role], seg=seg_per,
                         phi0=c - step / 2 + half_gap, phi1=c + step / 2 - half_gap))
    return out


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


def rotz(ob, phi):
    """Swing a part about Z by phi degrees (pol convention: +phi turns -Y toward +X)."""
    ob.data.transform(Matrix.Rotation(math.radians(phi), 4, "Z"))
    return ob


def patch(name, r0, r1, quad, role):
    """Flat plate between radii r0 < r1 over four (phi, z) corners - narrow stripes."""
    bm = bmesh.new()
    vi = [bm.verts.new(tuple(pol(r0, p, z))) for p, z in quad]
    vo = [bm.verts.new(tuple(pol(r1, p, z))) for p, z in quad]
    bm.faces.new(vo)
    bm.faces.new(list(reversed(vi)))
    for a in range(4):
        b = (a + 1) % 4
        bm.faces.new((vi[a], vi[b], vo[b], vo[a]))
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    return K._obj(name, bm, [role])


def arc_plate(name, r0, r1, phi0, phi1, z0, z1, role, n=4):
    """Curved plate (n facets along phi) between radii r0 < r1: an ID plate that
    follows a faceted wall without its chord sagging into the wall."""
    prof = [(r0, z0), (r1, z0), (r1, z1), (r0, z1)]
    return lathe(name, prof, [role], seg=n, phi0=phi0, phi1=phi1)


def aim(ob, base, axis):
    """Turn a part built about local +Z to `axis`, then move its origin to `base`."""
    rot = Vector((0, 0, 1)).rotation_difference(Vector(axis).normalized()).to_matrix().to_4x4()
    ob.data.transform(Matrix.Translation(Vector(base)) @ rot)
    return ob


def window_unit(name, w, h, proud, chamfer, rim, recess, rim_role, inner_role):
    """Chamfered-corner window frame at the origin facing -Y: back face on y = 0,
    front face at y = -proud, a `rim`-wide frame and a window recessed `recess`.
    The cheap (60-tri) cousin of K.bezel for repeated windows; the octagonal
    corners match the sheet's window frames."""
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
    return K._obj(name, bm, [rim_role, inner_role])


def dish(name, post_top, axis, rad, role="hull"):
    """Parabolic dish (rolled rim, solid shell) on a yoke, with a feed boom and horn.
    The post top is the yoke pivot; the bowl is pushed out along the axis so it
    clears the post."""
    axis = Vector(axis).normalized()
    depth, thick, n = 0.32 * rad, 0.03, 4
    prof = [(rad * i / n, depth * (i / n) ** 2 - thick) for i in range(n + 1)]
    prof.append((rad + 0.018, depth - thick * 0.3))
    prof += [(rad * i / n, depth * (i / n) ** 2) for i in range(n, -1, -1)]
    centre = Vector(post_top) + axis * 0.08
    out = [aim(lathe(name, prof, [role], seg=20), centre, axis)]
    # yoke: hinge block on the post top + a stub into the dish back
    out.append(K.box(name + "_hinge", Vector(post_top) - Vector((0.055, 0.055, 0.055)),
                     Vector(post_top) + Vector((0.055, 0.055, 0.055)), "slate", bevel=0.01, seg=1))
    out.append(K.cyl(name + "_stub", Vector(post_top), centre + axis * 0.01, 0.04, "pipe", sides=10))
    # feed boom + horn (one chunky boom; tripod struts would shred at 0.74x)
    tip = centre + axis * (depth + 0.55 * rad)
    out.append(K.cyl(name + "_boom", centre, tip, 0.03, "pipe", sides=8))
    out.append(K.cyl(name + "_horn", tip - axis * 0.02, tip + axis * 0.07, 0.055, "slate", sides=10))
    return out


def antenna(name, x, y, z0, z1, r=0.040):
    """Stepped mast: collar, lower section, ring, upper section (>= 3 cm), lit tip."""
    zm = z0 + 0.42 * (z1 - z0)
    return [K.cyl(name + "_collar", (x, y, z0 - 0.02), (x, y, z0 + 0.07), r + 0.022, "slate", sides=10),
            K.cyl(name + "_lo", (x, y, z0), (x, y, zm), r, "pipe", sides=10),
            K.cyl(name + "_ring", (x, y, zm - 0.03), (x, y, zm + 0.03), r + 0.014, "slate", sides=10),
            K.cyl(name + "_hi", (x, y, zm), (x, y, z1), max(0.030, r * 0.8), "pipe", sides=8),
            K.cyl(name + "_tip", (x, y, z1 - 0.01), (x, y, z1 + 0.065), r * 1.2, "glow", sides=10)]


# =================================================================== the pod
R = 1.152                                     # wall panel outer radius
# foot ring + skirt (dark slate, recessed panels); cores are outer-only strips
add(lathe("foot", [(1.17, 0.0), (1.17, 0.035), (1.155, 0.05), (1.10, 0.05)], ["slate"], closed=False))
add(lathe("skirt_core", [(1.12, 0.04), (1.12, 0.36)], ["slate"], seg=32, closed=False))
parts.extend(panels("skirt", [(1.10, 0.07), (1.15, 0.07), (1.15, 0.318), (1.138, 0.33), (1.10, 0.33)],
                    "slate", 10, 0.022, 1.15, phase=18.0, seg_per=4, skip=lambda p: on_module(p, 20.0)))
# bone-white strip, RustBeam band, wall panels, shoulder lip
add(lathe("strip", [(1.12, 0.335), (1.155, 0.335), (1.155, 0.40)], ["hull"], closed=False))
add(lathe("rustband", [(1.15, 0.40), (1.168, 0.40), (1.168, 0.50), (1.14, 0.50)], ["rust"], closed=False))
add(lathe("wall_core", [(1.125, 0.49), (1.125, 0.96)], ["slate"], seg=32, closed=False))
parts.extend(panels("wall", [(1.10, 0.49), (1.145, 0.49), (R, 0.497), (R, 0.953), (1.145, 0.96),
                             (1.10, 0.96)], "hull", 10, 0.018, R, phase=18.0,
                    skip=lambda p: on_module(p, 20.0)))
add(lathe("shoulder", [(R, 0.955), (1.170, 0.972), (1.170, 1.005), (1.15, 1.025), (0.99, 1.025)],
          ["hull"], closed=False))

# hazard stripes along the top of the wall (RustBeam on bone white), off the module arc
STRIPE_Z0, STRIPE_Z1 = 0.862, 0.930
pitch = math.degrees(0.092 / R)
wdeg, slant = math.degrees(0.046 / R), math.degrees(0.040 / R)
for sgn, phi_end in ((-1, 46.0), (1, 100.0)):
    phi = 23.0
    k = 0
    while phi < phi_end:
        p = sgn * phi
        q = [(p, STRIPE_Z0), (p + sgn * wdeg, STRIPE_Z0), (p + sgn * (wdeg + slant), STRIPE_Z1),
             (p + sgn * slant, STRIPE_Z1)]
        add(patch("haz%d_%d" % (sgn, k), R - 0.003, R + 0.005, q, "rust"))
        phi += pitch
        k += 1

# ID plates beside the hatch (accent trim), curved so they stay >= 5 mm proud
for sgn in (-1, 1):
    p = sgn * 30.0
    add(arc_plate("idplate%d" % sgn, R - 0.003, R + 0.008, p - 6.5, p + 6.5, 0.62, 0.75, "accent"))

# window ring: a 14-gon slate band (one flat facet per window), slate sill and
# posts, accent-framed dark windows standing out to r ~1.08 so the 54 deg camera
# sees them under the short shell lip
NWIN = 14
WSTEP = 360.0 / NWIN
BAND_AP = 1.03                                # facet apothem
BAND_RC = BAND_AP / math.cos(math.radians(WSTEP / 2))
BAND_Z0, BAND_Z1 = 1.02, 1.345
add(lathe("winband", [(BAND_RC, BAND_Z0), (BAND_RC, BAND_Z1)], ["slate"], seg=NWIN,
          closed=False, phase=-WSTEP / 2))
add(lathe("sill", [(1.095, 1.018), (1.095, 1.046), (0.99, 1.046)], ["slate"], closed=False))
WIN_W, WIN_H, WIN_PROUD, WIN_Z = 0.34, 0.20, 0.05, 1.185
for k in range(NWIN):
    phi = k * WSTEP
    phw = ((phi + 180.0) % 360.0) - 180.0
    if on_module(phw):
        continue
    wu = window_unit("win%d" % k, WIN_W, WIN_H, WIN_PROUD, chamfer=0.04, rim=0.032, recess=0.026,
                     rim_role="accent", inner_role="glass")
    wu.data.transform(Matrix.Translation((0.0, -(BAND_AP - 0.003), WIN_Z)))
    add(rotz(wu, phi))
for k in range(NWIN):                         # posts on the facet corners between windows
    phi = (k + 0.5) * WSTEP
    phw = ((phi + 180.0) % 360.0) - 180.0
    if on_module(phw, 45.0):
        continue
    add(obox("post%d" % k, pol(BAND_RC - 0.045, phi, BAND_Z0 + 0.02), pol(BAND_RC - 0.045, phi, BAND_Z1),
             0.06, 0.10, "slate", side=pol(1.0, phi + 90.0, 0.0)))

# upper shell: a straight conical frustum of 8 bone-white panels with a short
# lip (2.5 cm past the window frames), over an outer-only slate core
add(lathe("shell_core", [(1.09, 1.34), (1.09, 1.37), (0.80, 1.545), (0.70, 1.545)], ["slate"],
          seg=32, closed=False))
SHELL = [(0.93, 1.335), (1.085, 1.335), (1.105, 1.35), (1.105, 1.372), (1.085, 1.392),
         (0.845, 1.555), (0.80, 1.565), (0.76, 1.55)]
parts.extend(panels("shell", SHELL, "hull", 8, 0.022, 1.0, phase=0.0, seg_per=6,
                    skip=lambda p: on_module(p, 10.0)))

# collar: slate deck, rust collar with the magenta light ring, brackets
add(lathe("deck", [(0.835, 1.545), (0.835, 1.57), (0.82, 1.582), (0.64, 1.582)], ["slate"],
          closed=False))
add(lathe("collar", [(0.665, 1.53), (0.665, 1.655), (0.655, 1.667), (0.50, 1.667)], ["rust"],
          closed=False))
add(lathe("lightring", [(0.66, 1.612), (0.676, 1.612), (0.676, 1.648), (0.66, 1.648)], ["glow"],
          closed=False))
for k in range(6):
    phi = k * 60.0
    if on_module(phi, 25.0):
        continue
    tang = pol(1.0, phi + 90.0, 0.0)
    add(obox("bracket%d" % k, pol(0.665, phi, 1.594), pol(0.815, phi, 1.594), 0.06, 0.05, "slate",
             side=tang))

# cupola: accent base ring, octagonal glass frustum, mullions, domed cap, lid, beacon
add(lathe("cup_base", [(0.612, 1.655), (0.612, 1.722), (0.598, 1.736), (0.40, 1.736)], ["accent"],
          closed=False))
add(lathe("cup_glass", [(0.0, 1.705), (0.555, 1.705), (0.475, 1.92), (0.0, 1.92)], ["glass"],
          seg=8, phase=22.5))
for k in range(8):
    phi = 22.5 + k * 45.0
    add(obox("mullion%d" % k, pol(0.555, phi, 1.707), pol(0.475, phi, 1.918), 0.075, 0.05, "hull",
             side=pol(1.0, phi + 90.0, 0.0)))
add(lathe("cup_cap", [(0.0, 1.905), (0.50, 1.905), (0.512, 1.92), (0.50, 1.945), (0.45, 1.975),
                      (0.30, 1.995), (0.0, 1.995)], ["hull"], seg=32))
add(lathe("lid", [(0.0, 1.989), (0.24, 1.989), (0.25, 1.999), (0.25, 2.02), (0.24, 2.03),
                  (0.0, 2.03)], ["slate"], seg=24))
add(lathe("beacon", [(0.0, 2.025), (0.075, 2.025), (0.075, 2.057), (0.055, 2.085), (0.0, 2.095)],
          ["glow"], seg=16))

# ============================================================ hatch (front)
DOOR_Y, DPROUD, DW, DH, DZ, DRIM = -1.07, 0.175, 0.76, 0.94, 0.50, 0.085
frame, wy = K.bezel("doorframe", (0.0, DOOR_Y, DZ), DW, DH, DPROUD, radius=0.15, rim=DRIM,
                    recess=0.06, rim_role="slate", inner_role="pipe")
add(frame)
FRONT = DOOR_Y - DPROUD                       # frame front face
ow, oh = DW / 2 - DRIM, DH / 2 - DRIM          # opening half extents
leaf_front = wy - 0.022
add(K.box("leaf", (-ow + 0.025, leaf_front, DZ - oh + 0.015), (ow - 0.025, wy + 0.004, DZ + oh - 0.015),
          "hull", bevel=0.008, chamfers=[(lambda d, m: abs(d.y) > 0.99, 0.06)]))
# X brace over the lower leaf, latch plate over the crossing, window above
for i, (xa, xb) in enumerate(((-0.205, 0.205), (0.205, -0.205))):
    yc = leaf_front - 0.008 - 0.003 * i
    add(obox("brace%d" % i, (xa, yc, 0.19), (xb, yc, 0.61), 0.05, 0.02, "hull", side=(0, 0, 1)))
add(K.box("latch", (-0.065, leaf_front - 0.032, 0.34), (0.065, leaf_front + 0.002, 0.46), "metal",
          bevel=0.008))
dwin, _ = K.bezel("doorwin", (0.0, leaf_front + 0.002, 0.755), 0.30, 0.15, 0.024, radius=0.045,
                  rim=0.03, recess=0.012, rim_role="slate", inner_role="glass")
add(dwin)
for s in (-1, 1):                              # grab handles on the frame sides (r 3 cm)
    add(K.pipe("handle%d" % s, [(s * (DW / 2 - 0.01), -1.165, 0.34), (s * (DW / 2 + 0.08), -1.165, 0.34),
                                (s * (DW / 2 + 0.08), -1.165, 0.66), (s * (DW / 2 - 0.01), -1.165, 0.66)],
                      0.030, "metal", bend=0.05, sides=8, samples=3))
    for z in (0.30, 0.70):
        add(K.bolt("dbolt", (s * (DW / 2 - DRIM / 2), FRONT + 0.002, z), (0, -1, 0)))
# magenta status bar over the hatch
add(K.box("doorlight", (-0.17, FRONT - 0.006, DZ + DH / 2 - 0.064), (0.17, FRONT + 0.004, DZ + DH / 2 - 0.026),
          "glow", bevel=0.003, seg=1))
# threshold step with a HazYellow nosing (3 cm tall, 2 cm deep)
add(K.box("step", (-0.34, FRONT - 0.07, 0.0), (0.34, DOOR_Y - 0.02, 0.07), "slate", bevel=0.012))
add(K.box("stepedge", (-0.32, FRONT - 0.082, 0.042), (0.32, FRONT - 0.062, 0.074), "hazard",
          bevel=0.003, seg=1))

# ====================================== service module (left flank, azimuth MA)
# Built in a local frame with its axis along +Y (pol 180) like a back module,
# then the whole group is swung round to MA. Local -X is the side the default
# camera sees.
mod = []
madd = mod.append
BX, BY0, BY1, BZ, BCH = 0.60, 0.84, 1.48, 1.55, 0.22


def top_y_edges(zhi, offset):
    return (lambda d, m, zhi=zhi: abs(d.y) > 0.99 and m.z > zhi - 0.01, offset)


madd(K.box("plinth", (-BX - 0.04, BY0 + 0.10, 0.0), (BX + 0.04, BY1 + 0.04, 0.16), "slate", bevel=0.015))
madd(K.box("module", (-BX, BY0, 0.10), (BX, BY1, BZ), "hull", bevel=0.015,
           chamfers=[top_y_edges(BZ, BCH)]))
# strap where module meets pod: RustBeam edge, slate band, RustBeam edge
madd(K.box("rustA", (-BX - 0.07, 0.88, 0.0), (BX + 0.07, 0.92, 1.60), "rust", bevel=0.008,
           chamfers=[top_y_edges(1.60, 0.28)]))
madd(K.box("strap", (-BX - 0.11, 0.92, 0.0), (BX + 0.11, 1.14, 1.64), "slate", bevel=0.012,
           chamfers=[top_y_edges(1.64, 0.30)]))
madd(K.box("rustB", (-BX - 0.07, 1.14, 0.0), (BX + 0.07, 1.18, 1.60), "rust", bevel=0.008,
           chamfers=[top_y_edges(1.60, 0.28)]))
# the strap bends over the shell into the collar (the sheet's L-strap): a slate
# bridge on the deck, butted into the strap and the collar, tucked 7 mm under
# the light ring, with RustBeam edges along both sides
madd(K.box("strap_bridge", (-0.18, 0.64, 1.55), (0.18, 0.935, 1.605), "slate", bevel=0.01, seg=1))
for s in (-1, 1):
    xa, xb = sorted((s * 0.17, s * 0.21))
    madd(K.box("bridge_rust%d" % s, (xa, 0.66, 1.555), (xb, 0.925, 1.595), "rust", bevel=0.006, seg=1))
# strap furniture on the camera side (local -X): accent marker + slate box
madd(K.box("strap_mark", (-BX - 0.118, 0.96, 1.02), (-BX - 0.104, 1.10, 1.16), "accent", bevel=0.003, seg=1))
madd(K.box("strap_box", (-BX - 0.15, 0.97, 0.52), (-BX - 0.10, 1.09, 0.80), "slate", bevel=0.008, seg=1))
for k in range(3):                            # glow LEDs on the strap box
    z = 0.58 + k * 0.07
    madd(K.box("strap_led%d" % k, (-BX - 0.156, 1.00, z - 0.016), (-BX - 0.146, 1.06, z + 0.016), "glow",
               bevel=0.002, seg=1))
# end face (+Y local): panel, hatch, vent, LEDs, marker, pipes
bp, _ = K.bezel("bk_panel", (0.0, BY1, 1.10), 0.80, 0.34, 0.026, radius=0.04, rim=0.035,
                recess=0.012, rim_role="metal", inner_role="hull", facing=1)
madd(bp)
bh, _ = K.bezel("bk_hatch", (-0.22, BY1, 0.48), 0.38, 0.56, 0.03, radius=0.05, rim=0.035,
                recess=0.014, rim_role="accent", inner_role="hull", facing=1)
madd(bh)
madd(K.box("bk_vent", (0.10, BY1 - 0.005, 0.24), (0.42, BY1 + 0.03, 0.58), "slate", bevel=0.01, seg=1))
for k in range(6):
    z = 0.29 + k * 0.05
    madd(K.box("bk_slat%d" % k, (0.12, BY1 + 0.025, z - 0.014), (0.40, BY1 + 0.042, z + 0.014), "metal",
               bevel=0.003, seg=1))
for k, x in enumerate((0.16, 0.26, 0.36)):
    madd(K.box("bk_led%d" % k, (x - 0.03, BY1 - 0.004, 0.655), (x + 0.03, BY1 + 0.008, 0.69), "glow",
               bevel=0.002, seg=1))
madd(K.box("bk_mark", (0.10, BY1 - 0.004, 0.73), (0.42, BY1 + 0.008, 0.77), "accent", bevel=0.002, seg=1))
for x in (0.48, 0.555):                       # pipes hug the end face (<= 6 cm proud)
    madd(K.pipe("bk_pipe%d" % int(x * 100), [(x, BY1 - 0.04, 1.02), (x, BY1 + 0.03, 1.02),
                                              (x, BY1 + 0.03, 0.20), (x, BY1 - 0.04, 0.20)],
                0.028, "pipe", bend=0.06, sides=8, samples=3))
# side vents (+-X local) with an accent marker above each
for s in (-1, 1):
    x0, x1 = sorted((s * (BX - 0.005), s * (BX + 0.028)))
    madd(K.box("sv_frame%d" % s, (x0, 1.22, 0.36), (x1, 1.44, 0.86), "slate", bevel=0.01, seg=1))
    for k in range(7):
        z = 0.42 + k * 0.064
        xa, xb = sorted((s * (BX + 0.02), s * (BX + 0.042)))
        madd(K.box("sv_slat%d_%d" % (s, k), (xa, 1.24, z - 0.017), (xb, 1.42, z + 0.017), "metal",
                   bevel=0.003, seg=1))
    xa, xb = sorted((s * (BX - 0.004), s * (BX + 0.008)))
    madd(K.box("sv_mark%d" % s, (xa, 1.22, 0.92), (xb, 1.44, 0.97), "accent", bevel=0.002, seg=1))
    # upper side panel (a raised plate like the sheet's module side)
    xa, xb = sorted((s * (BX - 0.005), s * (BX + 0.022)))
    madd(K.box("sv_plate%d" % s, (xa, 1.21, 1.02), (xb, 1.45, 1.26), "hull", bevel=0.008, seg=1))

# top: mast box, two antennas, two dishes (the sheet's comms cluster)
madd(K.box("mastbox", (-0.14, 1.20, BZ - 0.02), (0.14, 1.44, BZ + 0.20), "hull", bevel=0.014, seg=1))
madd(K.box("mastbox_side", (0.14, 1.25, BZ - 0.02), (0.26, 1.40, BZ + 0.12), "slate", bevel=0.01, seg=1))
madd(K.box("mast_mark", (-0.148, 1.25, BZ + 0.12), (-0.136, 1.39, BZ + 0.16), "accent", bevel=0.002, seg=1))
mod.extend(antenna("antA", -0.05, 1.28, BZ + 0.20, 2.55, r=0.042))
mod.extend(antenna("antB", 0.06, 1.37, BZ + 0.20, 2.25, r=0.038))
madd(K.cyl("dishA_post", (-0.44, 1.22, BZ - 0.12), (-0.44, 1.22, BZ + 0.24), 0.045, "pipe", sides=10))
mod.extend(dish("dishA", (-0.44, 1.22, BZ + 0.24), (-0.62, -0.05, 0.78), 0.31))
madd(K.cyl("dishB_post", (0.44, 1.22, BZ - 0.12), (0.44, 1.22, BZ + 0.20), 0.045, "pipe", sides=10))
mod.extend(dish("dishB", (0.44, 1.22, BZ + 0.20), (0.60, 0.10, 0.80), 0.27))

for o in mod:                                 # swing the group from pol 180 to MA
    rotz(o, MA - 180.0)
parts.extend(mod)

# ===================================================================== finish
ob = K.finish(parts, NAME)
K.face_camera(ob)                              # hatch (built at -Y) -> -X, the default view
# ground-centred pivot: bounds bottom at z = 0, bounds centred on XY
xs = [v.co.x for v in ob.data.vertices]
ys = [v.co.y for v in ob.data.vertices]
zs = [v.co.z for v in ob.data.vertices]
ob.data.transform(Matrix.Translation((-(min(xs) + max(xs)) / 2, -(min(ys) + max(ys)) / 2, -min(zs))))
ob.data.update()
rh_hs.log("final dims %s m (x, y, z), %d tris"
          % ([round(max(xs) - min(xs), 3), round(max(ys) - min(ys), 3), round(max(zs) - min(zs), 3)],
             sum(len(p.vertices) - 2 for p in ob.data.polygons)))
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
