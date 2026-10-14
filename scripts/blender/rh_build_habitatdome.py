"""Build the HabitatDome from code - the hard-surface remake (2026-10-14).

  blender -b -P scripts/blender/rh_build_habitatdome.py -- [--out Martians/gen/hs/HabitatDome]
      [--size 2048] [--preview-only]

The game's Habitat ("A surface habitat module."). The director's verdict on the
image-to-3D HabitatDome was "looks good. could be a lot better though": the
design was liked, the build was not - a geodesic dome of smudgy, blobby
triangular panels (Saved/RHLineup/png/Models2__HabitatDome__v035/__v215.png).
So the DESIGN is kept and only the construction changes. Design source: the
approved five-view sheet Martians/gen/objects-redo/HabitatDome/cutout/
HabitatDome_{three_quarter,front,left,right,back}.png - a bone-white panelled
dome framed by chunky brick-red structural ribs into large smooth pentagon /
hexagon panels riveted along their frames, round portholes in red rims, a
cylindrical airlock module with a round hatch end sticking out of one side, a
low bolted base ring and a collar on the crown.

How it is built (everything exact, nothing generated):
  - the rib network is a spherical truncated icosahedron (12 pentagons, 20
    hexagons - the football pattern the sheet shows), generated here from an
    icosahedron with one vertex at the zenith: a pentagon crowns the dome and
    carries the collar, five pentagons ring it at 26.6 deg elevation, hexagons
    fill between. The sphere is cut 6 deg ABOVE its equator: the dome is a
    spherical cap (sphere r 4.06 m, base r 4.04 m, crown 3.64 m), so the lower
    wall is near-vertical like the sheet, and the cut leaves the middle-band
    hexagons as the trapezoids that sit on the base ring in the sheet's front
    view; the ring-band pentagons' bottom edges run along the ring top.
  - panels are the faces of that pattern, inset from the rib lines in a
    gnomonic (great-circle-straight) projection, clipped at the base ring along
    the horizontal circle (not a chord), each ONE smooth slab (no internal
    seams: the old asset's geodesic triangles are exactly what the director
    disliked) with two rivets along every rib edge; a 12 mm shadow gap to the
    dark slate core shell outlines each panel inside its frame. Ribs are 20 cm
    beams standing 12 cm off the panels with a 7 cm
    channel splitting their top into two rails - the sheet's double red frame
    line where two panel frames meet - following the great circles; a
    chamfered brick node boss covers every junction, so three beams never meet
    in a ragged pile. Ribs are trimmed where they run into the base ring or
    under the airlock flange, so every beam end is buried.
  - portholes are real: a red rim ring standing 10 cm proud on a white bolted
    flange, a dark tunnel wall and the glass sunk 9 cm below the lip; 4 of 9
    glow warm (lived-in at night), the rest are dark glass.
  - UVs: after K.finish, the hidden slate core's islands (29 % of the atlas,
    seen only through the seams) are shrunk and everything is re-packed with a
    FRACTION margin (a true 6 px gap); the kit's default SCALED margin left
    the ~3000 small islands mostly gap. Visible texel area ~1.9x.
  - round 1 -> 5 (preview reviews): white node bosses read as polka dots
    against the sheet's continuous red frame -> red; single-web ribs read as
    flat straps -> double-rail channel; 25k tris -> 16.2k (prism bolts with no
    buried caps, coarser hidden core, rivets only on panels without portholes,
    1-segment bevels on small boxes); the HAB-01 marking sat on a rib at 144
    deg -> moved to the back hexagon at 180; a 1.24 m door (1.5 m in game,
    shorter than the 1.8 m crew) -> tube r 0.95, door 1.40 m (~1.7 m in game).
  - round 6 (orchestrator art review): the kit's RustBeam read orange-brown
    and sank into the Mars ground -> own "brick" role (#8A3A2E) for every
    structural red part; the recessed fan seams inside each panel brought back
    the old geodesic-triangle look -> one smooth slab per panel, rivets along
    the rib edges only (140 rivets, 5-sided heads).

Layout facts:
  - built with the AIRLOCK at -Y (it is the identity: no other building has a
    hatch tube), then K.face_camera() turns it to -X, the face the game's
    default strategy camera meets (measured 2026-10-13). The airlock grows out
    of the front pentagon, whose ribs frame its conforming brick saddle flange;
    pentagon portholes sit at +-72 / +-144 deg and hexagon portholes at +-36,
    +-108 and 180 deg, so the default view has a porthole either side of it.
  - airlock: hull tube r 0.95 m (axis 1.30 m up) on a slate saddle with two
    legs, two-tread step with HazYellow nosings, white (accent) end ring and
    band, recessed slate hatch face, white-framed octagonal door (leaf 0.82 x
    1.40 m) with a lit porthole, hinges, latch, keypad with a lit LED,
    HazYellow grab handles (r 3 cm) either side, and the amber door light
    housing on the tube top behind the end ring (lens to the front over the
    ring, strip on top, so it reads from the 54 deg camera).
  - service details at the base, both in the default view: the umbilical /
    conduit box (viewer's right, +50 deg before the turn) whose two conduits
    cross the base ring into the dome, and an exhaust vent with a louvre and a
    duct (viewer's left, -50 deg).
  - fit: the game scales the larger horizontal extent (the airlock axis, 9.98
    m: base ring back to the step nose) to 12 m (6 x 8 footprint, min-side
    fit, x1.20), so in game the dome is ~10.5 m across the base ring, ~4.6 m
    to the collar top and the door leaf ~1.7 m tall.
  - palette (art bible 1-2): BoneWhite hull panels and tube, DarkSlate core,
    machinery and steps, brick red (#8A3A2E, the sheet's frame colour, darker
    and redder than RustBeam) ribs / node bosses / porthole rims / base ring /
    collar / saddle flange, light-alloy
    bolts, rivets, hinges. Accent (#F3F3F3, TintFor Habitat white) is TRIM:
    porthole bolt flanges, the airlock end ring, band and door frame, the
    collar lip and lid ring, the HAB-01 markings and the service-box labels -
    5.0-7.6 % of the ray-cast visible surface from every review camera.
    Lit role "lit" (#FFC070) matches the asset's one amber EmissiveColor
    (~(4.5, 2.2, 0.2) linear) so lit windows read warm in daylight too; the
    emissive mask is lit portholes, the door window, the door lamp and LEDs.
Real-world metres, ground-centred pivot (bounds centred on XY, bottom at z=0).
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
OUT = argv[argv.index("--out") + 1] if "--out" in argv else "Martians/gen/hs/HabitatDome"
SIZE = int(argv[argv.index("--size") + 1]) if "--size" in argv else 2048
PREVIEW_ONLY = "--preview-only" in argv
NAME = "HabitatDome"

K = rh_hs.Kit(accent_hex="#F3F3F3")       # TintFor(Habitat): white trim
# Lit role: warm amber windows and lamps. Albedo matches the in-game glow hue so
# lit portholes still read warm in daylight; preview emission = the game's
# EmissiveColor normalised, low enough that review frames show amber, not white.
K.roles["lit"] = dict(albedo="#FFC070", metal=0.0, rough=0.25, emit=1, dust=0.0)
K.mats["lit"] = K._preview_material("lit", K.roles["lit"])
_lb = K.mats["lit"].node_tree.nodes["Principled BSDF"]
_lb.inputs["Emission Color"].default_value = (1.0, 0.49, 0.044, 1.0)
_lb.inputs["Emission Strength"].default_value = 1.2
# Structural red: the sheet's deep brick-red frame. The kit's RustBeam (#B86F4B)
# read orange-brown and sank into the orange Mars ground (orchestrator review,
# round 6); ribs, node bosses, porthole rims, base ring, collar and the airlock
# saddle flange use this instead. Less dust than paint so the red stays red.
K.roles["brick"] = dict(albedo="#8A3A2E", metal=0.2, rough=0.6, dust=0.6)
K.mats["brick"] = K._preview_material("brick", K.roles["brick"])

parts = []
add = parts.append

# ------------------------------------------------------------------ layout
RS = 4.06                                  # dome sphere radius
PHI_CUT = math.radians(6.0)                # the sphere is cut 6 deg above its equator
ZC = RS * math.sin(PHI_CUT)                # sphere centre sits ZC below the ground
O = Vector((0.0, 0.0, -ZC))
RB = RS * math.cos(PHI_CUT)                # dome base radius (4.038)
Z_TOP = RS - ZC                            # crown height (3.636)
Z_CLIP = 0.24                              # panels and ribs end inside the base ring
S_CLIP = (Z_CLIP + ZC) / RS
PANEL_T, BURY = 0.035, 0.025               # panel top above the core; burial below it
RIB_HALF, RIB_GAP = 0.10, 0.012
# rib cross-section (w, h): a 20 cm beam standing 12 cm off the panels, its top
# split into two rails by a 7 cm channel - the sheet's double red frame line
# where two panel frames meet. Traversed left-bottom -> over -> right-bottom.
RIB_PROF = [(-0.10, -BURY), (-0.10, 0.10), (-0.085, 0.118), (-0.045, 0.118), (-0.035, 0.07),
            (0.035, 0.07), (0.045, 0.118), (0.085, 0.118), (0.10, 0.10), (0.10, -BURY)]
HUB_R, HUB_H = 0.15, 0.15                  # node boss over every rib junction (brick)
# airlock (axis along -Y at height AX_Z)
AX_Z, TUBE_R, FLANGE_R = 1.30, 0.95, 1.13
SINK = 0.003


def log(msg):
    rh_hs.log(msg)


# ------------------------------------------------------------------ helpers
def pol_dir(az, el):
    """Unit vector at azimuth az (deg, from -Y toward +X) and elevation el (rad)."""
    a = math.radians(az)
    return Vector((math.cos(el) * math.sin(a), -math.cos(el) * math.cos(a), math.sin(el)))


def az_of(u):
    return math.degrees(math.atan2(u.x, -u.y))


def surf(u, h=0.0):
    """Point h metres above the dome sphere along unit direction u."""
    return O + u * (RS + h)


def frame(c):
    """Right-handed tangent frame at c: e1 horizontal (toward +X on the front),
    e2 up the dome, e1 x e2 = c."""
    if abs(c.z) > 0.999:
        e1 = Vector((1.0, 0.0, 0.0))
    else:
        e1 = Vector((0.0, 0.0, 1.0)).cross(c).normalized()
    return e1, c.cross(e1)


def slerp(a, b, t):
    om = a.angle(b)
    if om < 1e-9:
        return a.copy()
    return (a * math.sin((1 - t) * om) + b * math.sin(t * om)) / math.sin(om)


def orient(f, desired, mi=0):
    f.normal_update()
    if f.normal.dot(desired) < 0:
        f.normal_flip()
    f.material_index = mi
    return f


def lathe(name, prof, centre, axis, b1, sides, roles, mis=None, closed=False, phase=0.0):
    """Revolve an (r, a) profile about `axis` through `centre`; a runs along the
    axis, r along cos(t) b1 + sin(t) (axis x b1). Authoring convention (as in
    rh_build_commandmodule): the outside is on the RIGHT of the direction of
    travel (outer wall going up, top going inward; closed profiles CCW) - every
    face is checked against that normal. r = 0 points become poles.
    mis: material index per profile edge."""
    A = Vector(axis).normalized()
    B1 = Vector(b1).normalized()
    B2 = A.cross(B1)
    C = Vector(centre)
    bm = bmesh.new()
    ts = [phase + 2.0 * math.pi * s / sides for s in range(sides)]
    rings = []
    for r, a in prof:
        if r < 1e-6:
            v = bm.verts.new(C + A * a)
            rings.append([v] * sides)
        else:
            rings.append([bm.verts.new(C + (B1 * math.cos(t) + B2 * math.sin(t)) * r + A * a)
                          for t in ts])
    n = len(prof)
    edges = [(i, i + 1) for i in range(n - 1)] + ([(n - 1, 0)] if closed else [])
    for k, (i, j) in enumerate(edges):
        dr, da = prof[j][0] - prof[i][0], prof[j][1] - prof[i][1]
        for s in range(sides):
            s1 = (s + 1) % sides
            vs = []
            for v in (rings[i][s], rings[i][s1], rings[j][s1], rings[j][s]):
                if v not in vs:
                    vs.append(v)
            if len(vs) < 3:
                continue
            tm = (ts[s] + ts[s] + 2.0 * math.pi / sides) / 2.0
            radial = B1 * math.cos(tm) + B2 * math.sin(tm)
            orient(bm.faces.new(vs), radial * da - A * dr, mis[k] if mis else 0)
    return K._obj(name, bm, roles)


def lathe_z(name, prof, roles, sides=48, mis=None, closed=False, x=0.0, y=0.0, phase=0.0):
    return lathe(name, prof, (x, y, 0.0), (0, 0, 1), (0, -1, 0), sides, roles, mis, closed, phase)


def lathe_y(name, prof, roles, sides=40, mis=None, closed=False, x=0.0, z=AX_Z):
    """About an axis parallel to Y through (x, *, z); a = distance toward -Y."""
    return lathe(name, prof, (x, 0.0, z), (0, -1, 0), (1, 0, 0), sides, roles, mis, closed)


def rotz(objs, deg):
    M = Matrix.Rotation(math.radians(deg), 4, "Z")
    for o in objs:
        o.data.transform(M)
    return objs


def obox(name, ctr, half, rot, role, bevel=0.0, seg=1):
    """Box of half extents `half`, rotated by 3x3 `rot`, centred at `ctr`."""
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    for v in bm.verts:
        v.co = Vector((v.co.x * 2 * half[0], v.co.y * 2 * half[1], v.co.z * 2 * half[2]))
    if bevel > 0:
        bmesh.ops.bevel(bm, geom=list(bm.edges), offset=bevel, offset_type="OFFSET",
                        segments=seg, profile=0.5, affect="EDGES", clamp_overlap=True)
    bmesh.ops.transform(bm, matrix=Matrix.Translation(Vector(ctr)) @ rot.to_4x4(), verts=bm.verts)
    return K._obj(name, bm, [role])


def prism(name, at, normal, r, h, role="metal", sides=6):
    """Bolt head / node boss: an n-gon prism from `at` along `normal`, top cap
    only (the base is always buried in its support). 16 tris for a hex bolt,
    vs 20 for K.bolt."""
    n = Vector(normal).normalized()
    b1 = n.cross(Vector((0, 0, 1)) if abs(n.z) < 0.9 else Vector((1, 0, 0))).normalized()
    b2 = n.cross(b1)
    at = Vector(at)
    bm = bmesh.new()
    lo, hi = [], []
    for k in range(sides):
        t = 2 * math.pi * k / sides
        d = b1 * math.cos(t) + b2 * math.sin(t)
        lo.append(bm.verts.new(at + d * r))
        hi.append(bm.verts.new(at + d * r + n * h))
    for k in range(sides):
        k1 = (k + 1) % sides
        t = 2 * math.pi * (k + 0.5) / sides
        orient(bm.faces.new([lo[k], lo[k1], hi[k1], hi[k]]), b1 * math.cos(t) + b2 * math.sin(t))
    orient(bm.faces.new(hi), n)
    return K._obj(name, bm, [role])


def bezel(name, center, w, h, proud, radius, rim, recess, rim_role, inner_role, facing=-1):
    """K.bezel refusing rim >= radius - 5 mm (folded / collapsed window corners)."""
    assert radius - rim >= 0.005, "%s: rim %.3f must be < radius %.3f - 5 mm" % (name, rim, radius)
    return K.bezel(name, center, w, h, proud, radius=radius, rim=rim, recess=recess,
                   rim_role=rim_role, inner_role=inner_role, facing=facing)


def dome_text(name, body, c, size, role="accent", proud=0.009, spacing=1.05):
    """Raised lettering wrapped onto the dome: the builtin font, triangulated,
    centred on direction c, each vertex carried onto the sphere through the
    panel's gnomonic plane, its back buried 2 cm into the panel slab and its
    face standing `proud` off the panel top (>= 3 mm off any facet)."""
    cu = bpy.data.curves.new(name + "_c", "FONT")
    cu.body = body
    cu.size = size
    cu.offset = 0.004
    cu.space_character = spacing
    cu.extrude = 0.5
    cu.resolution_u = 2
    cu.align_x = "CENTER"
    cu.align_y = "CENTER"
    tmp = bpy.data.objects.new(name + "_tmp", cu)
    bpy.context.scene.collection.objects.link(tmp)
    dg = bpy.context.evaluated_depsgraph_get()
    me = bpy.data.meshes.new_from_object(tmp.evaluated_get(dg))
    bpy.data.objects.remove(tmp)
    bpy.data.curves.remove(cu)
    bm = bmesh.new()
    bm.from_mesh(me)
    bpy.data.meshes.remove(me)
    bmesh.ops.triangulate(bm, faces=bm.faces[:])
    xs = [v.co.x for v in bm.verts]
    ys = [v.co.y for v in bm.verts]
    cx0, cy0 = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2
    e1, e2 = frame(c)
    for v in bm.verts:
        x, y, z = v.co.x - cx0, v.co.y - cy0, v.co.z
        u = (c + (e1 * x + e2 * y) / (RS + PANEL_T)).normalized()
        h = PANEL_T + proud if z > 0 else PANEL_T - 0.02
        v.co = surf(u, h)
    bm.normal_update()
    return K._obj(name, bm, [role])


def recentre(ob):
    """Bounds centred on XY, bottom at z=0 (the visualizer's contract)."""
    vs = [v.co for v in ob.data.vertices]
    lo = Vector((min(v.x for v in vs), min(v.y for v in vs), min(v.z for v in vs)))
    hi = Vector((max(v.x for v in vs), max(v.y for v in vs), max(v.z for v in vs)))
    ob.data.transform(Matrix.Translation(Vector((-(lo.x + hi.x) / 2, -(lo.y + hi.y) / 2, -lo.z))))
    ob.data.update()
    dims = hi - lo
    log("recentred by %s; dims %s; height / max horizontal = %.3f" % (
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
    log("role share (all faces, %.1f m2): %s" % (s, ", ".join(
        "%s %.1f%%" % (k, 100 * v / s) for k, v in sorted(tot.items(), key=lambda kv: -kv[1]))))


# ================================================ the pattern: spherical TI
def icosahedron():
    e = math.atan(0.5)
    V = [Vector((0.0, 0.0, 1.0))]
    V += [pol_dir(72.0 * k, e) for k in range(5)]
    V += [pol_dir(36.0 + 72.0 * k, -e) for k in range(5)]
    V.append(Vector((0.0, 0.0, -1.0)))
    dmin = min((V[i] - V[j]).length for i in range(12) for j in range(i + 1, 12))
    nb = {i: [j for j in range(12) if j != i and abs((V[i] - V[j]).length - dmin) < 1e-5]
          for i in range(12)}
    F = sorted({tuple(sorted((i, j, k))) for i in range(12) for j in nb[i] for k in nb[i]
                if k in nb[j]})
    return V, nb, F


IV, INB, IF = icosahedron()


def ti_vert(i, j):
    """TI vertex one third of the way from icosa vertex i toward j (on the sphere)."""
    return (IV[i] * (2.0 / 3.0) + IV[j] * (1.0 / 3.0)).normalized()


def ccw(c, pts):
    e1, e2 = frame(c)
    return sorted(pts, key=lambda p: math.atan2(p.dot(e2), p.dot(e1)))


FACES = []
for i in range(12):                       # pentagons round each icosa vertex
    pts = ccw(IV[i], [ti_vert(i, j) for j in INB[i]])
    FACES.append(dict(kind="pent", c=IV[i].copy(), pts=pts))
for (a, b, c) in IF:                      # hexagons on each icosa face
    cen = (IV[a] + IV[b] + IV[c]).normalized()
    pts = [ti_vert(a, b), ti_vert(b, a), ti_vert(b, c), ti_vert(c, b), ti_vert(c, a), ti_vert(a, c)]
    FACES.append(dict(kind="hex", c=cen, pts=ccw(cen, pts)))

EDGES = {}
for f in FACES:
    n = len(f["pts"])
    for k in range(n):
        a, b = f["pts"][k], f["pts"][(k + 1) % n]
        key = tuple(sorted((tuple(round(x, 5) for x in a), tuple(round(x, 5) for x in b))))
        EDGES[key] = (a, b)
TIVERTS = {}
for a, b in EDGES.values():
    for p in (a, b):
        TIVERTS[tuple(round(x, 5) for x in p)] = p
log("pattern: %d faces, %d edges, %d vertices" % (len(FACES), len(EDGES), len(TIVERTS)))


def under_flange(p, margin=0.0):
    """True if world point p lies inside the airlock saddle flange's footprint."""
    return p.y < -1.0 and math.hypot(p.x, p.z - AX_Z) < FLANGE_R + margin


# ------------------------------------------------------------ 2D polygon ops
def offset_poly(pts, offs):
    """CCW polygon, edge i (pts[i] -> pts[i+1]) moved inward by offs[i]."""
    n = len(pts)
    lines = []
    for i in range(n):
        a, b = pts[i], pts[(i + 1) % n]
        d = (b - a).normalized()
        lines.append((a + Vector((-d.y, d.x)) * offs[i], d))
    out = []
    for i in range(n):
        if offs[i - 1] == 0.0 and offs[i] == 0.0:
            out.append(pts[i].copy())
            continue
        p1, d1 = lines[i - 1]
        p2, d2 = lines[i]
        cr = d1.x * d2.y - d1.y * d2.x
        if abs(cr) < 1e-6:
            out.append(p2.copy())
        else:
            df = p2 - p1
            out.append(p1 + d1 * ((df.x * d2.y - df.y * d2.x) / cr))
    return out


def poly_centroid(pts):
    a2, cx, cy = 0.0, 0.0, 0.0
    n = len(pts)
    for i in range(n):
        p, q = pts[i], pts[(i + 1) % n]
        cr = p.x * q.y - q.x * p.y
        a2 += cr
        cx += (p.x + q.x) * cr
        cy += (p.y + q.y) * cr
    return Vector((cx / (3 * a2), cy / (3 * a2)))


def seg_dist(p, a, b):
    ab = b - a
    t = max(0.0, min(1.0, (p - a).dot(ab) / max(ab.length_squared, 1e-12)))
    return (a + ab * t - p).length


def resample(chain, step):
    """Polyline -> points at equal arc spacing <= step, endpoints kept."""
    L = [0.0]
    for a, b in zip(chain, chain[1:]):
        L.append(L[-1] + (b - a).length)
    m = max(1, int(math.ceil(L[-1] / step)))
    out, j = [], 0
    for k in range(m + 1):
        s = L[-1] * k / m
        while j < len(chain) - 2 and L[j + 1] < s:
            j += 1
        seg = L[j + 1] - L[j]
        t = 0.0 if seg < 1e-12 else (s - L[j]) / seg
        out.append(chain[j].lerp(chain[j + 1], min(1.0, max(0.0, t))))
    return out


# ================================================================== panels
NR = 3                                    # radial rings per panel slab
PORTS = []                                # (centre dir, flange radius, lit, kind)
RIVETS = []                               # (panel, unit dir) rivet spots along rib edges
LIT_AZ = {36, -72, 144, 180}              # lit portholes (azimuth, rounded)


def build_panel(f, idx):
    c = f["c"]
    e1, e2 = frame(c)

    def gn(p):
        d = p.dot(c)
        return Vector((p.dot(e1) / d, p.dot(e2) / d))

    def ungn(g):
        return (c + e1 * g.x + e2 * g.y).normalized()

    G = [gn(p) for p in f["pts"]]
    n = len(G)
    delta = (RIB_HALF + RIB_GAP) / RS
    offs = []
    for i in range(n):
        a, b = G[i], G[(i + 1) % n]
        d = (b - a).normalized()
        t = -Vector((-d.y, d.x)).dot(a)          # origin -> edge line (gnomonic)
        offs.append(t - math.tan(math.atan(t) - delta))
    Q = offset_poly(G, offs)
    # densify, lift to the sphere, clip at the base-ring line (with the clip arc
    # following the horizontal circle, not a great circle)
    dense = []
    for i in range(n):
        a, b = Q[i], Q[(i + 1) % n]
        m = max(1, int(math.ceil((b - a).length / 0.008)))
        for k in range(m):
            dense.append((ungn(a.lerp(b, k / m)), "corner" if k == 0 else ""))
    out = []
    N = len(dense)
    for i in range(N):
        (p, pf), (q, qf) = dense[i], dense[(i + 1) % N]
        fp, fq = p.z - S_CLIP, q.z - S_CLIP
        if fp >= 0:
            out.append((p, pf))
        if (fp >= 0) != (fq >= 0):
            x = p.lerp(q, fp / (fp - fq))
            x.z = S_CLIP
            x = x.normalized()
            out.append((x, "exit" if fp >= 0 else "enter"))
    if len(out) < 3:
        return None
    # arc along the cut from each exit to the following entry
    loop = []
    for i, (p, flag) in enumerate(out):
        loop.append((p, "corner" if flag in ("corner", "exit", "enter") else "", flag))
        if flag == "exit":
            q = next(out[(i + k) % len(out)][0] for k in range(1, len(out) + 1)
                     if out[(i + k) % len(out)][1] == "enter")
            a0, a1 = az_of(p), az_of(q)
            da = (a1 - a0 + 180.0) % 360.0 - 180.0
            m = max(1, int(math.ceil(abs(da) / 0.5)))
            for k in range(1, m):
                loop.append((pol_dir(a0 + da * k / m, math.asin(S_CLIP)), "", ""))
    zmax = max(surf(p).z for p, _, _ in loop)
    if zmax - Z_CLIP < 0.25:
        return None                         # a sliver under the base ring
    # cut corners within 25 cm of a true corner are not seam corners
    true_c = [p for p, c_, fl in loop if fl == "corner"]
    corners = []
    for i, (p, c_, fl) in enumerate(loop):
        if c_ and fl in ("exit", "enter") and any((p - t).length * RS < 0.25 for t in true_c):
            c_ = ""
        if c_:
            corners.append(i)
    P2 = [gn(p) for p, _, _ in loop]
    apex = poly_centroid(P2)
    inr = min(seg_dist(apex, P2[i], P2[(i + 1) % len(P2)]) for i in range(len(P2)))
    capex = ungn(apex)
    # ONE smooth slab per panel (the sheet's panels are plain polygons framed
    # by the ribs; round 5's recessed fan seams brought back the old asset's
    # geodesic-triangle look). Boundary resampled per edge so the polygon
    # corners stay exact; a radial grid from the centroid, NR rings.
    bm = bmesh.new()
    step = 0.55 / RS
    bloop = []
    for k in range(len(corners)):
        i0, i1 = corners[k], corners[(k + 1) % len(corners)]
        chain = [P2[j % len(P2)] for j in range(i0, i1 + (len(P2) if i1 <= i0 else 0) + 1)]
        chain = resample(chain, step)
        bloop += chain[:-1]
        # rivets along the rib edge: two per edge, 7 cm in from the panel edge,
        # none along the base-ring cut
        if ungn(chain[len(chain) // 2]).z < S_CLIP + 0.004:
            continue
        L = [0.0]
        for a, b in zip(chain, chain[1:]):
            L.append(L[-1] + (b - a).length)
        for frac in (1.0 / 3.0, 2.0 / 3.0):
            sv = L[-1] * frac
            jj = max(0, min(len(chain) - 2, next(x for x in range(len(L) - 1) if L[x + 1] >= sv)))
            a, b = chain[jj], chain[jj + 1]
            pt = a.lerp(b, (sv - L[jj]) / max(1e-12, L[jj + 1] - L[jj]))
            d = (b - a).normalized()
            RIVETS.append((idx, ungn(pt + Vector((-d.y, d.x)) * (0.07 / RS))))
    M = len(bloop)
    rings = [[apex]] + [[apex.lerp(q, r / NR) for q in bloop] for r in range(1, NR + 1)]
    top = [[bm.verts.new(surf(ungn(g), PANEL_T)) for g in ring] for ring in rings]
    for r in range(NR):
        for j in range(M):
            j1 = (j + 1) % M
            if r == 0:
                vs = [top[0][0], top[1][j], top[1][j1]]
            else:
                vs = [top[r][j], top[r][j1], top[r + 1][j1], top[r + 1][j]]
            orient(bm.faces.new(vs), ungn(rings[max(r, 1)][j]))
    # wall round the boundary, down into the core
    low = [bm.verts.new(surf(ungn(g), -BURY)) for g in bloop]
    for j in range(M):
        j1 = (j + 1) % M
        out2 = (bloop[j] + bloop[j1]) / 2 - apex
        orient(bm.faces.new([top[NR][j], top[NR][j1], low[j1], low[j]]), e1 * out2.x + e2 * out2.y)
    ob = K._obj("panel%d" % idx, bm, ["hull"])
    # what this panel carries
    el = math.degrees(math.asin(max(-1.0, min(1.0, c.z))))
    az = int(round(az_of(c))) if el < 80 else 0
    if az == -180:
        az = 180
    if f["kind"] == "pent" and 20 < el < 35 and az != 0:
        PORTS.append((capex, min(0.56, inr * RS - 0.10), az in LIT_AZ, "pent"))
    elif f["kind"] == "hex" and 5 < el < 15:
        PORTS.append((capex, min(0.47, inr * RS - 0.09), az in LIT_AZ, "hex"))
    return ob


npan = 0
for idx, f in enumerate(FACES):
    ob = build_panel(f, idx)
    if ob is not None:
        add(ob)
        npan += 1
log("panels kept: %d; portholes: %d" % (npan, len(PORTS)))

# ---------------------------------------------------------------- core shell
NEL = 10
core = [(RS * math.cos(PHI_CUT + (math.pi / 2 - PHI_CUT) * k / NEL),
         RS * math.sin(PHI_CUT + (math.pi / 2 - PHI_CUT) * k / NEL) - ZC) for k in range(NEL)]
core.append((0.0, Z_TOP))
add(lathe_z("core", core, ["slate"], sides=32))


# ==================================================================== ribs
def valid(u):
    return u.z >= S_CLIP and not under_flange(surf(u), -0.04)


def rib(name, A, B):
    """Stepped beam along the great circle A -> B, trimmed to the valid runs."""
    NS = 40
    ok = [valid(slerp(A, B, k / NS)) for k in range(NS + 1)]
    out = []
    k = 0
    while k <= NS:
        if not ok[k]:
            k += 1
            continue
        k0 = k
        while k + 1 <= NS and ok[k + 1]:
            k += 1
        k1 = k

        def refine(t_in, t_out):
            for _ in range(12):
                tm = (t_in + t_out) / 2
                if valid(slerp(A, B, tm)):
                    t_in = tm
                else:
                    t_out = tm
            return t_in

        ta = k0 / NS if k0 == 0 else refine(k0 / NS, (k0 - 1) / NS)
        tb = k1 / NS if k1 == NS else refine(k1 / NS, (k1 + 1) / NS)
        L = (tb - ta) * A.angle(B) * RS
        if L > 0.15:
            nseg = max(1, int(math.ceil(L / 0.56)))
            us = [slerp(A, B, ta + (tb - ta) * s / nseg) for s in range(nseg + 1)]
            out.append(beam(name, us))
        k += 1
    return out


def beam(name, us):
    bm = bmesh.new()
    secs = []
    for s, u in enumerate(us):
        T = (us[min(s + 1, len(us) - 1)] - us[max(s - 1, 0)]).normalized()
        S = u.cross(T).normalized()
        secs.append(([bm.verts.new(surf(u, h) + S * w) for (w, h) in RIB_PROF], u, S))
    for s in range(len(secs) - 1):
        (va, ua, Sa), (vb, ub, Sb) = secs[s], secs[s + 1]
        um, Sm = (ua + ub).normalized(), (Sa + Sb).normalized()
        for j in range(len(RIB_PROF) - 1):
            dw = RIB_PROF[j + 1][0] - RIB_PROF[j][0]
            dh = RIB_PROF[j + 1][1] - RIB_PROF[j][1]
            orient(bm.faces.new([va[j], va[j + 1], vb[j + 1], vb[j]]), Sm * (-dh) + um * dw)
    return K._obj(name, bm, ["brick"])


nrib = 0
for k, (a, b) in enumerate(EDGES.values()):
    for ob in rib("rib%d" % k, a, b):
        add(ob)
        nrib += 1
nhub = 0
for k, u in enumerate(TIVERTS.values()):
    if u.z >= S_CLIP + 0.004 and not under_flange(surf(u), -0.02):
        e1, _ = frame(u)
        add(lathe("hub%d" % k, [(HUB_R, -0.02), (HUB_R, HUB_H - 0.03), (HUB_R - 0.03, HUB_H),
                                (0.0, HUB_H)], surf(u), u, e1, 6, ["brick"]))
        nhub += 1
log("rib runs %d, node bosses %d" % (nrib, nhub))

# ================================================================ portholes
for k, (c, rf, lit, kind) in enumerate(PORTS):
    e1, e2 = frame(c)
    base = surf(c, PANEL_T)
    ri = rf - (0.16 if kind == "pent" else 0.14)   # glass radius
    rm = rf - 0.07                                  # raised ring outer radius
    # white bolt flange (accent trim) round a red raised rim, dark tunnel wall
    prof = [(rf, -0.07), (rf, 0.04), (rm, 0.05), (rm, 0.085),
            (rm - 0.02, 0.105), (ri + 0.02, 0.105), (ri, 0.085), (ri, -0.02)]
    add(lathe("port%d" % k, prof, base, c, e1, 16, ["accent", "brick", "slate"],
              mis=[0, 0, 1, 1, 1, 1, 2]))
    # the glass: one disc 9 cm below the lip (its edge buried in the tunnel wall)
    add(lathe("glass%d" % k, [(ri + 0.004, 0.014), (0.0, 0.014)], base, c, e1, 16,
              ["lit" if lit else "glass"]))
    nb = 6 if kind == "pent" else 4
    for j in range(nb):
        t = 2 * math.pi * (j + 0.5) / nb
        p = base + (e1 * math.cos(t) + e2 * math.sin(t)) * ((rf + rm) / 2) + c * 0.035
        add(prism("pbolt", p, c, 0.022, 0.024))

# ------------------------------------------------- HAB-01 markings (labels)
# on the upper hexagons above-left of the airlock (default view) and on the
# opposite side, painted-on-style raised letters in the white accent
for k, az in enumerate((-36.0, 180.0)):
    add(dome_text("hab%d" % k, "HAB-01", pol_dir(az, math.radians(49.0)), 0.40))

# ------------------------------------------------------------------ rivets
nriv = 0
for k, (pi, u) in enumerate(RIVETS):
    if under_flange(surf(u), 0.05):
        continue
    if any(u.angle(c) * RS < rf + 0.05 for c, rf, _, _ in PORTS):
        continue
    # base 2.5 cm under the panel's vertex level: the slab facets sag <= 1.5 cm
    add(prism("rivet", surf(u, PANEL_T - 0.025), u, 0.028, 0.04, sides=5))
    nriv += 1
log("rivets %d" % nriv)

# =============================================================== base ring
# low bolted ring, as the sheet: a foot flange flaring 32 cm out, a band, a
# chamfered top tucked into the dome (panels and ribs end inside it)
add(lathe_z("basering", [(RB + 0.32, 0.0), (RB + 0.32, 0.05), (RB + 0.16, 0.07), (RB + 0.14, 0.09),
                         (RB + 0.14, 0.235), (RB + 0.11, 0.27), (3.97, 0.29)], ["brick"], sides=56))
for k in range(32):
    az = (k + 0.5) * 11.25
    azw = (az + 180.0) % 360.0 - 180.0
    if abs(azw) < 16 or abs(abs(azw) - 50) < 9:
        continue                            # under the steps / service boxes
    n = pol_dir(az, 0.0)
    add(prism("rbolt", n * (RB + 0.132) + Vector((0, 0, 0.165)), n, 0.026, 0.026))

# ================================================================== collar
# brick collar with a white lip (trim the 54 deg camera sees from every side)
add(lathe_z("collar", [(0.80, Z_TOP - 0.14), (0.80, Z_TOP + 0.17), (0.775, Z_TOP + 0.20),
                       (0.665, Z_TOP + 0.20), (0.64, Z_TOP + 0.175), (0.64, Z_TOP + 0.10)],
            ["brick", "accent"], sides=32, mis=[0, 1, 1, 1, 0]))
add(lathe_z("lid", [(0.645, Z_TOP + 0.10), (0.48, Z_TOP + 0.10)], ["slate"], sides=32))
add(lathe_z("lidring", [(0.485, Z_TOP + 0.08), (0.485, Z_TOP + 0.13), (0.465, Z_TOP + 0.145),
                        (0.40, Z_TOP + 0.145), (0.38, Z_TOP + 0.13), (0.38, Z_TOP + 0.10)],
            ["accent"], sides=24))
add(lathe_z("hatch", [(0.385, Z_TOP + 0.09), (0.385, Z_TOP + 0.115), (0.0, Z_TOP + 0.115)],
            ["hull"], sides=24))
add(K.box("hatch_bar", (-0.20, -0.035, Z_TOP + 0.11), (0.20, 0.035, Z_TOP + 0.15), "metal",
          bevel=0.008, seg=1))
for k in range(8):
    t = 2 * math.pi * (k + 0.5) / 8
    add(prism("cbolt", (0.72 * math.cos(t), 0.72 * math.sin(t), Z_TOP + 0.195), (0, 0, 1), 0.024,
               0.024))

# ================================================================= airlock
# saddle flange: conforming to the dome sphere, 16 cm proud, round the tube root
FS = 48
fprof = [(FLANGE_R, -0.02), (FLANGE_R, 0.13), (FLANGE_R - 0.03, 0.16), (TUBE_R - 0.005, 0.16)]


def fpt(rho, h, t):
    x, z = rho * math.cos(t), AX_Z + rho * math.sin(t)
    return Vector((x, -math.sqrt((RS + h) ** 2 - x * x - (z + ZC) ** 2), z))


bm = bmesh.new()
ring = [[bm.verts.new(fpt(r, h, 2 * math.pi * s / FS)) for s in range(FS)] for r, h in fprof]
for j in range(len(fprof) - 1):
    dr, dh = fprof[j + 1][0] - fprof[j][0], fprof[j + 1][1] - fprof[j][1]
    for s in range(FS):
        s1 = (s + 1) % FS
        t = 2 * math.pi * (s + 0.5) / FS
        p = fpt(fprof[j][0], fprof[j][1], t)
        nrm = (p - O).normalized()
        rad = Vector((math.cos(t), 0.0, math.sin(t)))
        orient(bm.faces.new([ring[j][s], ring[j][s1], ring[j + 1][s1], ring[j + 1][s]]),
               rad * dh - nrm * dr)
add(K._obj("flange", bm, ["brick"]))
for k in range(12):
    t = 2 * math.pi * (k + 0.5) / 12
    p = fpt(FLANGE_R - 0.085, 0.16, t)
    add(prism("fbolt", p - (p - O).normalized() * 0.006, (p - O).normalized(), 0.026, 0.03))

# tube: hull with a seam groove, slate saddle strap, white band, white end
# ring, recessed slate hatch face. Sized for the crew: the door leaf is 1.40 m
# at build scale, ~1.7 m after the game's ~1.2x fit.
TR = TUBE_R
ER, EI = TR + 0.12, TR - 0.10              # end ring outer / inner radius
A_END = 5.14                               # end ring front face (a = -y)
tube = [(TR, 3.0), (TR, 4.10), (TR - 0.015, 4.115), (TR - 0.015, 4.135),
        (TR, 4.15), (TR, 4.56), (TR + 0.03, 4.575), (TR + 0.03, 4.69), (TR, 4.705),
        (TR, 4.95), (ER, 4.95), (ER, 5.11), (ER - 0.03, A_END), (EI + 0.03, A_END), (EI, 5.11),
        (EI, 5.05), (0.0, 5.05)]
tmis = [0, 0, 0, 0, 0, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 2]
add(lathe_y("tube", tube, ["hull", "accent", "slate"], sides=32, mis=tmis))
add(lathe_y("strap", [(TR - 0.005, 4.28), (TR + 0.05, 4.29), (TR + 0.05, 4.43), (TR - 0.005, 4.44)],
            ["slate"], sides=32))
for s in (-1, 1):                          # saddle legs, feet, cross beam
    x0, x1 = sorted((s * 0.33, s * 0.47))
    add(K.box("leg", (x0, -4.43, 0.02), (x1, -4.29, 0.47), "slate", bevel=0.012, seg=1))
    add(K.box("foot", (x0 - 0.03, -4.46, 0.0), (x1 + 0.03, -4.26, 0.035), "rubber", bevel=0.008, seg=1))
add(K.box("crossbeam", (-0.35, -4.40, 0.12), (0.35, -4.32, 0.20), "slate", bevel=0.01, seg=1))
# steps: two treads with HazYellow nosings (>= 5 mm proud); the back of tread 2
# bites 1 cm into the end ring
add(K.box("tread1", (-0.58, -5.62, 0.0), (0.58, -5.13, 0.20), "slate", bevel=0.012, seg=1))
add(K.box("tread2", (-0.58, -5.40, 0.18), (0.58, -5.13, 0.40), "slate", bevel=0.012, seg=1))
add(K.box("nose1", (-0.56, -5.626, 0.16), (0.56, -5.58, 0.205), "hazard", bevel=0.004, seg=1))
add(K.box("nose2", (-0.56, -5.406, 0.36), (0.56, -5.36, 0.405), "hazard", bevel=0.004, seg=1))
# door: white frame (bezel), octagonal hull leaf, lit window, hinges, latch
HF = -5.05                                 # hatch face plane (y)
DW, DH = 1.00, 1.56                        # frame outer size (corner circle stays inside EI)
fr, wy = bezel("doorframe", (0.0, HF + SINK, AX_Z), DW, DH, 0.05 + SINK, radius=0.24,
               rim=0.07, recess=0.03, rim_role="accent", inner_role="slate")
add(fr)
LF = -5.095                                # leaf front plane
LW, LH = DW / 2 - 0.09, DH / 2 - 0.08      # leaf half size: 2 cm gap inside the frame
add(K.box("leaf", (-LW, LF, AX_Z - LH), (LW, wy + 0.002, AX_Z + LH), "hull", bevel=0.008,
          chamfers=[(lambda d, m: abs(d.y) > 0.99, 0.12)], seg=1))
WZ = AX_Z + 0.38                           # door window centre
add(lathe_y("doorwin", [(0.15, -LF - 0.004), (0.15, -LF + 0.022), (0.13, -LF + 0.03),
                        (0.105, -LF + 0.03), (0.10, -LF + 0.02), (0.10, -LF)],
            ["metal"], sides=20, z=WZ))
add(K.cyl("doorglass", (0.0, LF + 0.004, WZ), (0.0, LF - 0.006, WZ), 0.104, "lit", sides=20))
for z in (AX_Z - 0.44, AX_Z + 0.44):
    add(K.box("hinge", (-LW - 0.06, -5.118, z - 0.07), (-LW + 0.07, LF + 0.004, z + 0.07), "metal",
              bevel=0.006, seg=1))
add(K.box("latch", (LW - 0.17, -5.135, AX_Z - 0.20), (LW - 0.12, -5.112, AX_Z + 0.10), "metal",
          bevel=0.008, seg=1))
for z in (AX_Z - 0.16, AX_Z + 0.06):
    add(K.box("latch_post", (LW - 0.165, -5.114, z - 0.02), (LW - 0.125, LF + 0.004, z + 0.02), "metal",
              bevel=0.004, seg=1))
# keypad with a lit LED (viewer's right of the door)
KX = DW / 2 + 0.03
add(K.box("keypad", (KX, -5.085, AX_Z - 0.05), (KX + 0.16, HF + 0.003, AX_Z + 0.17), "slate",
          bevel=0.006, seg=1))
add(K.box("keyled", (KX + 0.04, -5.091, AX_Z + 0.10), (KX + 0.12, -5.080, AX_Z + 0.14), "lit",
          bevel=0.002, seg=1))
# HazYellow grab handles (r 3 cm) on the end ring face, either side of the door
HX = (ER + EI) / 2 - 0.03
for s in (-1, 1):
    x = s * HX
    add(K.pipe("handrail%d" % s, [(x, -5.12, AX_Z - 0.36), (x, -5.27, AX_Z - 0.36),
                                   (x, -5.27, AX_Z + 0.36), (x, -5.12, AX_Z + 0.36)],
               0.03, "hazard", bend=0.07, sides=8, samples=3))
for k in range(12):                        # end ring bolts
    t = 2 * math.pi * k / 12
    rb = (ER + EI) / 2 + 0.005
    add(prism("ebolt", (rb * math.cos(t), -A_END + 0.006, AX_Z + rb * math.sin(t)),
               (0, -1, 0), 0.026, 0.03))
# door light: slate housing on the tube top just behind the end ring (biting
# 2 cm into it), amber lens to the front over the ring, amber strip on its top
add(K.box("lamp_house", (-0.21, -4.97, AX_Z + TR - 0.06), (0.21, -4.74, AX_Z + ER + 0.14), "slate",
          bevel=0.012, seg=1))
add(K.box("lamp_lens", (-0.15, -4.976, AX_Z + ER + 0.02), (0.15, -4.96, AX_Z + ER + 0.10), "lit",
          bevel=0.004, seg=1))
add(K.box("lamp_strip", (-0.15, -4.95, AX_Z + ER + 0.13), (0.15, -4.84, AX_Z + ER + 0.146), "lit",
          bevel=0.003, seg=1))

# ========================================================= service details
R0 = RB + 0.34                             # just outside the base ring foot
svc = []
sadd = svc.append
# umbilical / conduit box (built at -Y, swung to +50 deg: viewer's right)
sadd(K.box("cb_plinth", (-0.46, -(R0 + 0.56), 0.0), (0.46, -(RB + 0.25), 0.06), "slate", bevel=0.01, seg=1))
sadd(K.box("cb_body", (-0.42, -(R0 + 0.52), 0.05), (0.42, -R0, 0.95), "slate", bevel=0.015, seg=1))
sadd(K.box("cb_lid", (-0.46, -(R0 + 0.56), 0.93), (0.46, -(R0 - 0.03), 0.99), "slate", bevel=0.012, seg=1))
sadd(K.box("cb_door", (-0.34, -(R0 + 0.545), 0.16), (0.34, -(R0 + 0.515), 0.80), "hull", bevel=0.008, seg=1))
sadd(K.box("cb_haz", (-0.40, -(R0 + 0.527), 0.835), (0.40, -(R0 + 0.515), 0.895), "hazard",
           bevel=0.003, seg=1))
sadd(K.box("cb_label", (-0.18, -(R0 + 0.553), 0.62), (0.18, -(R0 + 0.540), 0.73), "accent",
           bevel=0.003, seg=1))
sadd(K.box("cb_handle", (0.22, -(R0 + 0.575), 0.38), (0.27, -(R0 + 0.540), 0.58), "metal", bevel=0.008, seg=1))
sadd(K.box("cb_led", (-0.28, -(R0 + 0.552), 0.30), (-0.22, -(R0 + 0.540), 0.34), "lit", bevel=0.002,
           seg=1))
for x, z, r, role in ((0.10, 0.70, 0.075, "slate"), (-0.20, 0.52, 0.045, "pipe")):
    sadd(K.cyl("conduit", (x, -(R0 + 0.02), z), (x, -3.70, z), r, role, sides=12))
    sadd(K.cyl("gland", (x, -(R0 + 0.03), z), (x, -(R0 - 0.06), z), r + 0.035, "metal", sides=12))
    sadd(K.cyl("entry", (x, -3.98, z), (x, -3.72, z), r + 0.05, "slate", sides=12))
rotz(svc, 50.0)
parts += svc
# exhaust vent (built at -Y, swung to -50 deg: viewer's left)
svc = []
sadd = svc.append
sadd(K.box("v_plinth", (-0.50, -(R0 + 0.50), 0.0), (0.50, -(RB + 0.25), 0.06), "slate", bevel=0.01, seg=1))
sadd(K.box("v_body", (-0.45, -(R0 + 0.46), 0.05), (0.45, -R0, 0.66), "slate", bevel=0.015, seg=1))
sadd(K.box("v_hood", (-0.49, -(R0 + 0.52), 0.64), (0.49, -(R0 - 0.04), 0.73), "hull", bevel=0.012,
           chamfers=[(lambda d, m: abs(d.x) > 0.99 and m.y < -(R0 + 0.3) and m.z > 0.7, 0.05)], seg=1))
vb, vwy = bezel("v_louvre", (0.0, -(R0 + 0.46) + SINK, 0.34), 0.74, 0.44, 0.03 + SINK, radius=0.045,
                rim=0.03, recess=0.02, rim_role="metal", inner_role="rubber")
sadd(vb)
rot = Matrix.Rotation(math.radians(35.0), 3, "X")
for k in range(4):
    sadd(obox("v_slat%d" % k, (0.0, vwy - 0.008, 0.21 + 0.087 * k), (0.335, 0.018, 0.009), rot, "metal",
              bevel=0.002))
sadd(K.box("v_duct", (-0.26, -(R0 + 0.02), 0.36), (0.26, -3.80, 0.62), "slate", bevel=0.012, seg=1))
sadd(K.box("v_label", (-0.20, -(R0 + 0.467), 0.585), (0.20, -(R0 + 0.455), 0.635), "accent",
           bevel=0.003, seg=1))
rotz(svc, -50.0)
parts += svc

role_stats(parts)


def tri_report(objs):
    """Triangles by part family (name without digits / suffixes)."""
    fam = {}
    for o in objs:
        key = o.name.split(".")[0].rstrip("0123456789-_")
        fam[key] = fam.get(key, 0) + sum(len(p.vertices) - 2 for p in o.data.polygons)
    log("tris by family: %s" % ", ".join("%s %d" % kv for kv in sorted(fam.items(), key=lambda kv: -kv[1])))


tri_report(parts)

# ------------------------------------------------------------------ finish
ob = K.finish(parts, NAME)


def shrink_hidden_uvs(ob, factor=0.15):
    """The slate core shell is ~30 % of all surface but shows only through the
    24 mm panel seams; smart-project packs by area, so it ate ~30 % of the
    atlas. Scale its UV islands down and re-pack: the visible surfaces get
    that texel density back."""
    me = ob.data
    slate = [i for i, m in enumerate(me.materials) if m["rh_role"] == "slate"]
    hidden = set()
    for p in me.polygons:
        if p.material_index in slate:
            d = p.center - O
            if RS - 0.035 < d.length < RS + 0.002 and abs(d.normalized().dot(p.normal)) > 0.9:
                hidden.add(p.index)
    def share():
        uv = me.uv_layers.active.data      # re-fetch: edit mode rewrites the layer
        tot = hid = 0.0
        for p in me.polygons:
            pts = [uv[li].uv for li in p.loop_indices]
            a2 = abs(sum(pts[k].x * pts[k - 1].y - pts[k - 1].x * pts[k].y for k in range(len(pts))))
            tot += a2
            hid += a2 if p.index in hidden else 0.0
        return 100.0 * hid / max(tot, 1e-12), tot / 2.0

    before, used0 = share()
    uv = me.uv_layers.active.data
    loops = [li for pi in hidden for li in me.polygons[pi].loop_indices]
    c = sum((uv[li].uv for li in loops), Vector((0.0, 0.0))) / max(1, len(loops))
    for li in loops:
        uv[li].uv = c + (uv[li].uv - c) * factor
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.select_all(action="SELECT")
    # FRACTION: a true 0.003 (6 px at 2048) gap. The default SCALED margin grows
    # with island size and, over ~3000 small islands (bolt faces, rib strips),
    # left the atlas mostly gaps: measured ~2x the visible texel density this way.
    bpy.ops.uv.pack_islands(rotate=True, margin=0.003, margin_method="FRACTION")
    bpy.ops.object.mode_set(mode="OBJECT")
    after, used1 = share()
    log("hidden core UV share %.1f %% -> %.1f %% (%d faces); atlas used %.0f %% -> %.0f %%"
        % (before, after, len(hidden), 100 * used0, 100 * used1))


shrink_hidden_uvs(ob)
K.face_camera(ob)                          # airlock (built at -Y) -> -X, the default view
recentre(ob)
os.makedirs(OUT, exist_ok=True)
bpy.ops.object.select_all(action="DESELECT")
ob.select_set(True)
bpy.context.view_layer.objects.active = ob
bpy.ops.export_scene.gltf(filepath=os.path.join(OUT, NAME + "_preview.glb"), export_format="GLB",
                          use_selection=True, export_apply=True)
log("preview glb written")
if not PREVIEW_ONLY:
    files = K.bake_and_export(ob, NAME, OUT, size=SIZE, dust=0.22)
    log("files: %s" % files)
