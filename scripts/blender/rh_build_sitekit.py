"""Build the CONSTRUCTION SITE kit from code - pad tile, scaffold bay, lamp mast, crate (2026-10-26, visual pass item 2).

  blender -b -P scripts/blender/rh_build_sitekit.py -- [--out Martians/gen/hs]
      [--part all|pad|scaffold|mast|crate] [--size 1024] [--preview-only]

Four small assets from one script, as rh_build_elevator.py and rh_build_tram.py. --out is the PARENT folder;
each asset is written to its own subfolder, <out>/RH_SitePad, <out>/RH_SiteScaffold, <out>/RH_SiteMast and
<out>/RH_SiteCrate. --part picks one (default all, one Blender process). --size is every asset's texture size.
  RH_SitePad       one 2 x 2 m foundation tile: four slate deck plates, anchor bolts, and a ring of hazard-chevron
                   curb round it that only shows on the pad's edge (see the contract)
  RH_SiteScaffold  one 2 x 1.2 x 2 m scaffold bay: light-alloy standards, ledgers, transoms and a facade brace,
                   a slate plank deck, hazard toe boards. Bays stand side by side and stack in courses.
  RH_SiteMast      the site's work-light mast: a slate ballast block, a two-stage alloy pole, a crossbar with two
                   amber work lamps aimed at the site and an amber beacon on top. They are the kit's only glow.
  RH_SiteCrate     a 1 m cargo crate in the Supply Ship's container language: slate frame, a cargo-amber lid and
                   sides with bone-white ends, a dark strap over the lid.

Why: the director, hand-play 2026-10-25: "The solar array 'in progress' state looks unfinished." Every site was an
engine cube slab with four hazard posts and a sphere on a stick. Design: docs/qa/2026-10-25/visual-pass-design.md
section 2, with the DIRECTOR ANSWER Q-A (a): a site shows its building's own model rising inside the scaffold, on
EVERY building. RHColonyVisualizerSubsystem draws all sites with one HISM per part (4 draws in all).

GEOMETRY CONTRACT - RHColonyVisualizerSubsystem.cpp (the site kit block) places the parts by these numbers. Metres,
Blender axes, Z up, TRUE SCALE (the game never rescales a part), no face_camera() (the game turns each part):
  - RH_SitePad: pivot = the TILE centre on the ground. Deck X/Y -1..+1 (2.0 x 2.0 m), plate tops at z 0.10, a base
    slab down to z -0.20 (it hides relief under the corners). The curb is a RING OUTSIDE the tile, 0.25 m wide: on
    each side an arm (x or y +-1.02..+-1.25, chevron top at z 0.064) on an apron (top 0.02) that fills the first 2 cm,
    and at each corner a block and apron 5 mm lower and 3 mm inset. On an interior side the arm lies wholly inside
    the neighbouring tile (under its plates and in its base slab), and a corner always lies under a neighbour's arm
    or plate when it has one, so only the pad's outer edge shows a curb - on all four sides of a pad one cell wide -
    and no two tiles' faces share a plane. The game never turns a tile. Bounds -1.25..1.25 on X and Y (asserted).
    The ring is 25 cm of ground marking past the footprint, 6 cm tall.
  - RH_SiteScaffold: pivot = the bay's centre on the ground, bounds X -1..+1, Y -0.6..+0.6, Z 0..2 (asserted). The
    OUTER face is -Y (brace, toe board, guard rails); the inner face +Y is open toward the building. Bays abut along
    X (the ledgers run the full 2.00 m, so neighbours' ledgers meet end to end) and stack every 2.00 m (a standard's
    top meets the next course's base plate). 1.2 m deep, not the design's 2 m: composed at 2 m (a 2 x 3 Solar
    site, rendered from the floor camera's 60 deg) the plank ring was wider than the pad and outweighed the
    building. The game stands the bay's inner face 0.30 m off the pad edge, so the pad's chevron curb shows in the
    gap, and leaves out any bay whose ground is already taken (a building's footprint, a prop, furniture, a wall).
  - RH_SiteMast: pivot = the ballast block's centre on the ground, bounds centred on XY (asserted to 3 mm). The
    lamps face +X, 12 deg down (so the strategy camera, 55-75 deg down, sees them), and an amber beacon tops the
    crossbar: the game yaws +X toward the site's centre. Surface sites only (it is 4.7 m; a floor is 4 m).
  - RH_SiteCrate: pivot = the crate's bottom centre, 1.00 x 1.00 m, 1.00 m to the frame top (the lugs and the
    strap's ratchet stand 1.6 cm proud; asserted). Crates stack every 1.00 m. Its lid and +-Y walls are cargo amber,
    its +-X end panels bone-white, so a stack reads amber from above and turned 0 / 90 deg mixes the two colours.

Layout facts:
  - palette (art bible): planks, crate frame and lamp housings slate; the pad's plates, base and curb "deck" (the
    same DarkSlate, nearly dielectric, baked with dust 0.10: a flat 2 m plate in the slate role's metal 0.30 and dust
    0.30 read pale tan in daylight, lighter than the ground); tubes and the mast pole light alloy (metal); ballast and
    crate ends bone-white (hull); crate lid and sides cargo amber (#E2A052: the Supply Ship's container body colour
    #ECC46C pulled toward orange, which in game rendered the same as the HazYellow accent - body, not accent); cable
    and strap rubber. Accent is HazYellow (#EDCE3F), as TRIM only:
    the pad's chevrons, the scaffold's two toe boards, the ballast's band and the pole's joint collar, the crate's
    lift points (top corner castings and end rails) and side data plates. visible_share() ray-casts each asset from
    8 yaws at the floor camera's 55 and 75 deg and asserts the accent is 5-15 % of the visible surface (built:
    pad 13.6 / 14.3 % for one tile - interior tiles in game show none -, scaffold 13.7 / 7.0 %, mast 8.3 / 5.7 %, crate
    8.0 / 8.7 %; the mast's glow 1.1 / 1.3 %, the crate's cargo amber 49 / 57 %).
  - ONE glow hue, amber, on the mast's two lamp lenses and its beacon only (the "glow" role); the pad, scaffold and
    crate import with EmissiveAmount 0.
  - budgets (spec): pad ~400 tris, scaffold ~1.5k, mast ~1.5k, crate ~800. The pad's curb ring (four arms, not
    the old L's two) takes it past 400; its assert allows 650. Built 596 / 656 / 1228 / 804. The bay
    is lean on purpose (it repeats ~10-80 times a site); its tubes are 60 mm, not 48, so they hold a pixel at the
    strategy distance. Textures 1024 (a 2 m tile at 2K would be ~1 mm a texel, far past what the camera resolves).
  - every decal / lens / plate stands >= 4 mm proud of its surface; parts meet by butting or by >= 3 mm
    interpenetration, never by coplanar overlap.

IMPORT (one Unreal process at a time; then rh_mesh_stats.py in a FRESH process). One call per part:
  RH_NAME=RH_SitePad RH_DIR=$PWD/Martians/gen/hs/RH_SitePad RH_DEST=/Game/RedHope/Art/Models2/SiteKit \
  RH_GLOW=3.2,2.2,0.35 RH_GLOW_AMT=0 RH_REPORT=<txt> \
    UnrealEditor-Cmd $PWD/red_hope.uproject -run=pythonscript \
    -script=$PWD/scripts/unreal/rh_import_hs.py -unattended -nosound -stdout
  (the same for RH_SiteCrate; RH_SiteScaffold with RH_LODS=0.40@0.12, RH_SiteMast with RH_LODS=0.40@0.12 and
  RH_GLOW_AMT=1.0). The pad and the crate carry NO reduced LOD: UE's 40 % reduction of the pad (236 tris) dropped the
  4 mm chevron strips and the plate joints and re-triangulated the bevelled plates into a dark triangle on each, from
  the bench zoom (0.25) out to the opening zoom (0.45), and the crate's lost its bone-white ends and strap (checked
  2026-10-27 with foliage.ForceLOD 0 against 1 - the HISM's LOD switch; r.ForceLOD does not reach it). The bay and
  the mast at 40 % were indistinguishable from LOD0 at both zooms. Delete
  Models2/SiteKit/<Name> before a re-import (the tram's lesson: Interchange's replace-in-place once kept an older
  bake's textures). M_RH_Master carries the ISM usage flag the HISMs need (the dff3b1a trap).
"""
import hashlib
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bmesh  # noqa: E402
import bpy  # noqa: E402
from mathutils import Matrix, Vector  # noqa: E402

import rh_hs  # noqa: E402

with open(os.path.abspath(__file__), "rb") as _f:      # which text built these outputs (the log ties them)
    rh_hs.log("rh_build_sitekit.py sha256 %s" % hashlib.sha256(_f.read()).hexdigest())

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OUT = argv[argv.index("--out") + 1] if "--out" in argv else "Martians/gen/hs"
SIZE = int(argv[argv.index("--size") + 1]) if "--size" in argv else 1024
PREVIEW_ONLY = "--preview-only" in argv
PART = argv[argv.index("--part") + 1] if "--part" in argv else "all"
assert PART in ("all", "pad", "scaffold", "mast", "crate"), "--part must be all, pad, scaffold, mast or crate"
PAD, BAY, MAST, CRATE = "RH_SitePad", "RH_SiteScaffold", "RH_SiteMast", "RH_SiteCrate"
ACCENT = "#EDCE3F"                          # HazYellow: the site's one accent, trim only

# ------------------------------------------------ geometry contract (C++ owns these)
TILE = 2.0                                  # pad tile and scaffold bay: one 2 m footprint cell
PLATE_Z = 0.10                              # pad plate tops
CURB = 0.25                                 # the curb ring's reach past the tile, on every side
BAY_H = 2.0                                 # one scaffold course
BAY_D = 1.2                                 # a bay's depth, outer face to inner face

K = None


def new_kit():
    """Fresh scene + role materials (Kit() resets the scene)."""
    global K
    K = rh_hs.Kit(accent_hex=ACCENT)
    # the work lamps: amber, as rh_build_chargepad.py / rh_build_supplyship.py (albedo = the in-game hue)
    K.roles["glow"] = dict(albedo="#FFB040", metal=0.0, rough=0.25, emit=1, dust=0.0)
    K.mats["glow"] = K._preview_material("glow", K.roles["glow"])
    gb = K.mats["glow"].node_tree.nodes["Principled BSDF"]
    gb.inputs["Emission Color"].default_value = (1.0, 0.49, 0.045, 1.0)
    gb.inputs["Emission Strength"].default_value = 1.5
    # cargo amber: the Supply Ship's container body colour pulled toward orange (painted steel, so it chips like the
    # bone-white). The ship's own #ECC46C (h 0.115) rendered in game the same hue and value as the HazYellow accent
    # (#EDCE3F, h 0.137) beside it (the 2026-10-27 art review sampled crate lids at (213,175,67) against a toe board's
    # (208,175,11)), so a crate stack read as one hazard-yellow block; #E2A052 (h 0.096) keeps the warm cargo read
    # and parts from the accent.
    K.roles["cargo"] = dict(albedo="#E2A052", metal=0.0, rough=0.55, paint=1, dust=1.0)
    K.mats["cargo"] = K._preview_material("cargo", K.roles["cargo"])
    # the pad's deck: DarkSlate, as the "slate" role, but nearly dielectric. The slate role's metal 0.30 mirrors the
    # tan sky on a flat 2 m plate, and with the dust bake it read pale tan in daylight - lighter than the regolith (the
    # 2026-10-27 review: pad centres (209,157,96) against the planks' (118,85,58)). The pad bakes with little dust too.
    K.roles["deck"] = dict(albedo="#595D65", metal=0.08, rough=0.72, paint=1, dust=1.0)
    K.mats["deck"] = K._preview_material("deck", K.roles["deck"])


# ------------------------------------------------------------------ helpers
def flat(name, poly, z, role):
    """One upward-facing polygon [(x, y)] at height z (chevrons and plates painted on a top face)."""
    bm = bmesh.new()
    f = bm.faces.new([bm.verts.new((x, y, z)) for x, y in poly])
    f.normal_update()
    if f.normal.z < 0:
        f.normal_flip()
    return K._obj(name, bm, [role])


def clip(poly, axis, lo, hi):
    """Clip a convex polygon [(x, y)] to lo <= p[axis] <= hi (Sutherland-Hodgman)."""
    def cut(pts, keep, val):
        out = []
        for i in range(len(pts)):
            a, b = pts[i], pts[(i + 1) % len(pts)]
            if keep(a):
                out.append(a)
            if keep(a) != keep(b):
                t = (val - a[axis]) / (b[axis] - a[axis])
                q = [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t]
                q[axis] = val
                out.append(tuple(q))
        return out
    p = cut(poly, lambda q: q[axis] >= lo, lo)
    if len(p) < 3:
        return []
    p = cut(p, lambda q: q[axis] <= hi, hi)
    return p if len(p) >= 3 else []


def chevron_strip(prefix, u0, u1, v0, v1, z, along, pitch=0.30, t=0.15):
    """45 deg HazYellow stripes over the rectangle u0..u1 (along `along`, "x" or "y") x v0..v1, as flat quads at z."""
    out = []
    span = v1 - v0
    u = u0 - span - t
    k = 0
    while u < u1:
        poly = clip([(u, v0), (u + t, v0), (u + t + span, v1), (u + span, v1)], 0, u0, u1)
        if poly:
            pts = poly if along == "x" else [(v, w) for w, v in poly]
            out.append(flat("%s%d" % (prefix, k), pts, z, "accent"))
        u += pitch
        k += 1
    return out


def tube(name, p0, p1, r, role="metal", sides=8, caps=False):
    return K.cyl(name, p0, p1, r, role, sides=sides, caps=caps)


def role_stats(objs, label):
    tot = {}
    for o in objs:
        me = o.data
        for p in me.polygons:
            r = me.materials[p.material_index]["rh_role"]
            tot[r] = tot.get(r, 0.0) + p.area
    s = sum(tot.values())
    rh_hs.log("%s role share (all faces): %s" % (label, ", ".join(
        "%s %.1f%%" % (k, 100 * v / s) for k, v in sorted(tot.items(), key=lambda kv: -kv[1]))))


def bounds(ob):
    vs = [v.co for v in ob.data.vertices]
    lo = Vector([min(v[i] for v in vs) for i in range(3)])
    hi = Vector([max(v[i] for v in vs) for i in range(3)])
    rh_hs.log("%s bounds lo %s hi %s" % (ob.name, [round(c, 4) for c in lo], [round(c, 4) for c in hi]))
    return lo, hi


def check_bounds(ob, lo_want, hi_want, tol=0.003):
    lo, hi = bounds(ob)
    bad = [(i, round(lo[i], 4), round(hi[i], 4)) for i in range(3)
           if abs(lo[i] - lo_want[i]) > tol or abs(hi[i] - hi_want[i]) > tol]
    assert not bad, "%s breaks its contract bounds %s..%s: %s" % (ob.name, lo_want, hi_want, bad)
    d = hi - lo
    rh_hs.log("%s %.3f x %.3f x %.3f m (contract holds)" % (ob.name, d.x, d.y, d.z))


def tris(ob):
    return sum(len(p.vertices) - 2 for p in ob.data.polygons)


def visible_share(ob, pitches=(55.0, 75.0), yaws=8, n=80):
    """Each role's share of the VISIBLE surface (rh_build_tram.py's method): rays from `yaws` directions round the
    asset at each pitch (degrees down from horizontal), an n x n grid over its bounding sphere, the first face hit
    counted by its role. Asserts the accent is 5-15 % at each pitch (art bible). Read only."""
    me = ob.data
    vs = [v.co for v in me.vertices]
    lo = Vector([min(v[i] for v in vs) for i in range(3)])
    hi = Vector([max(v[i] for v in vs) for i in range(3)])
    c, rad = (lo + hi) / 2, (hi - lo).length
    for pitch in pitches:
        tot = {}
        for k in range(yaws):
            yaw = math.radians(360.0 * k / yaws + 22.5)
            d = Vector((-math.cos(yaw) * math.cos(math.radians(pitch)), -math.sin(yaw) * math.cos(math.radians(pitch)),
                        -math.sin(math.radians(pitch))))
            u = d.cross(Vector((0.0, 0.0, 1.0))).normalized()
            v = u.cross(d).normalized()
            for i in range(n):
                for j in range(n):
                    o = c - d * rad * 2 + u * ((i / (n - 1) - 0.5) * rad) + v * ((j / (n - 1) - 0.5) * rad)
                    hit, _loc, _nrm, idx = ob.ray_cast(o, d)
                    if hit:
                        r = me.materials[me.polygons[idx].material_index]["rh_role"]
                        tot[r] = tot.get(r, 0) + 1
        s = sum(tot.values())
        rh_hs.log("%s visible share at pitch %.0f deg: %s" % (ob.name, pitch, ", ".join(
            "%s %.1f%%" % (r, 100.0 * m / s) for r, m in sorted(tot.items(), key=lambda kv: -kv[1]))))
        acc = 100.0 * tot.get("accent", 0) / max(1, s)
        assert 5.0 <= acc <= 15.0, "%s: accent %.1f %% of the visible surface at pitch %.0f, outside 5-15 %%" % (
            ob.name, acc, pitch)


def export_preview(ob, name):
    out = os.path.join(OUT, name)
    os.makedirs(out, exist_ok=True)
    bpy.ops.object.select_all(action="DESELECT")
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    bpy.ops.export_scene.gltf(filepath=os.path.join(out, name + "_preview.glb"),
                              export_format="GLB", use_selection=True, export_apply=True)
    rh_hs.log("preview glb written: %s" % out)
    return out


def finish(P, name, budget):
    role_stats(P, name)
    ob = K.finish(P, name)
    n = tris(ob)
    rh_hs.log("%s: %d tris (budget about %d)" % (name, n, budget))
    assert n <= budget * 1.25, "%s is %d tris, over its ~%d budget" % (name, n, budget)
    return ob


# =================================================================== the pad tile
def build_pad():
    new_kit()
    P = []
    add = P.append
    h = TILE / 2
    # base slab: under the plates (it shows in the 2 cm plate gaps) and 20 cm into the ground
    add(K.box("base", (-h, -h, -0.20), (h, h, 0.04), "deck", bevel=0.0))
    # four 0.98 m deck plates, 2 cm gaps (1 cm to the tile edge, so neighbouring tiles keep the same gap)
    for sx in (-1, 1):
        for sy in (-1, 1):
            xa, xb = sorted((sx * 0.01, sx * 0.99))
            ya, yb = sorted((sy * 0.01, sy * 0.99))
            add(K.box("plate", (xa, ya, 0.035), (xb, yb, PLATE_Z), "deck", bevel=0.012, seg=1))
    # anchor bolts: one at each plate's outer corner, a hex nut 1 cm proud of the plate
    for sx in (-1, 1):
        for sy in (-1, 1):
            add(K.cyl("bolt", (sx * 0.88, sy * 0.88, PLATE_Z - 0.003), (sx * 0.88, sy * 0.88, PLATE_Z + 0.012), 0.028,
                      "metal", sides=6))
    # The curb RING (contract): a raised chevron strip past every edge of the tile on a low apron. Each side's arm
    # is cut into two 0.98 m lengths at the plate seams (0 and +-1): on an interior tile the arm runs under the
    # neighbour, and an arm under the neighbour's own seams would show a sliver of chevron in them. The four corner
    # pieces (apron and raised block) stand 5 mm LOWER and 3 mm inset: wherever a neighbouring tile exists, its own
    # arm (or plate) covers the corner from above, so no two tiles' faces ever lie in one plane (the 2026-10-26 art
    # review: the old L's corner length lay coplanar on the next tile's first length). So a tile needs no turning, an
    # interior side is hidden under its neighbour, and a pad one cell wide shows its curb on all four sides.
    e0, e1 = h + 0.02, h + CURB             # the raised strip, from 2 cm past the tile edge
    ins = 0.003
    for s in (-1, 1):                       # the apron: the sides within the tile's edge length, top z 0.02
        ya = sorted((s * h, s * e1))
        add(K.box("apron_y", (-h, ya[0], -0.18), (h, ya[1], 0.02), "deck", bevel=0.0))
        add(K.box("apron_x", (ya[0], -h, -0.18), (ya[1], h, 0.02), "deck", bevel=0.0))
    for sx in (-1, 1):                      # the apron's corners, 5 mm lower and inset
        for sy in (-1, 1):
            xa = sorted((sx * (h + ins), sx * (e1 - ins)))
            ya = sorted((sy * (h + ins), sy * (e1 - ins)))
            add(K.box("apron_c", (xa[0], ya[0], -0.17), (xa[1], ya[1], 0.015), "deck", bevel=0.0))
    for s in (-1, 1):                       # the four arms
        v = sorted((s * e0, s * e1))
        for a, b in ((-0.99, -0.01), (0.01, 0.99)):
            add(K.box("curb_y", (a, v[0], -0.16), (b, v[1], 0.06), "deck", bevel=0.0))
            P.extend(chevron_strip("chev_y", a + 0.008, b - 0.008, v[0] + 0.008, v[1] - 0.008, 0.064, "x"))
            add(K.box("curb_x", (v[0], a, -0.16), (v[1], b, 0.06), "deck", bevel=0.0))
            P.extend(chevron_strip("chev_x", a + 0.008, b - 0.008, v[0] + 0.008, v[1] - 0.008, 0.064, "y"))
    for sx in (-1, 1):                      # the raised corner blocks: 0.054 top, chevrons at 0.058 (under 0.06)
        for sy in (-1, 1):
            xa = sorted((sx * (e0 + ins), sx * (e1 - ins)))
            ya = sorted((sy * (e0 + ins), sy * (e1 - ins)))
            add(K.box("curb_c", (xa[0], ya[0], -0.15), (xa[1], ya[1], 0.054), "deck", bevel=0.0))
            P.extend(chevron_strip("chev_c", xa[0] + 0.008, xa[1] - 0.008, ya[0] + 0.008, ya[1] - 0.008, 0.058, "x"))
    ob = finish(P, PAD, 520)
    check_bounds(ob, (-e1, -e1, -0.20), (e1, e1, PLATE_Z + 0.012))
    visible_share(ob)
    return ob


# ================================================================ the scaffold bay
def build_scaffold():
    new_kit()
    P = []
    add = P.append
    R = 0.030                                # 60 mm tube
    XS, YO, YI = 0.93, -0.53, 0.53           # standards at x +-XS, the outer face y YO, the inner face y YI
    ZD = 0.15                                # deck ledger / transom axis
    ZP = ZD + R                              # planks rest on the transoms
    for x in (-XS, XS):
        for y in (YO, YI):
            add(K.box("base_plate", (x - 0.07, y - 0.07, 0.0), (x + 0.07, y + 0.07, 0.02), "slate", bevel=0.0))
            add(tube("standard", (x, y, 0.017), (x, y, BAY_H), R, sides=8, caps=True))
            # a slate coupler collar at the deck ledger and (outer face) the guard rail
            for z in ((ZD, 1.10) if y == YO else (ZD,)):
                add(K.box("coupler", (x - 0.045, y - 0.045, z - 0.04), (x + 0.045, y + 0.045, z + 0.04), "slate",
                          bevel=0.0))
    # ledgers along X, the full 2.00 m: neighbouring bays' ledgers meet end to end
    for z in (ZD, 0.62, 1.10):               # outer: the deck ledger, the mid rail, the guard rail
        add(tube("ledger_o", (-1.0, YO, z), (1.0, YO, z), R * 0.9, sides=8, caps=True))
    add(tube("ledger_i", (-1.0, YI, ZD), (1.0, YI, ZD), R * 0.9, sides=8, caps=True))
    # transoms across the bay under the planks (and one mid-span)
    for x in (-XS, 0.0, XS):
        add(tube("transom", (x, YO, ZD), (x, YI, ZD), R * 0.9, sides=6))
    # the facade brace on the outer face, bottom (-X) to top (+X), just outside the standards
    yb = YO - R - 0.018
    add(tube("brace", (-XS, yb, ZD), (XS, yb, BAY_H - 0.06), 0.024, sides=6))
    # the plank deck: four slate planks across, 2 cm gaps, the full 2 m along X less 1 cm each end
    w, gap = 0.21, 0.02
    y = YO + R + 0.02 + 0.025               # past the toe board
    for k in range(4):
        add(K.box("plank", (-0.99, y, ZP - 0.003), (0.99, y + w, ZP + 0.045), "slate", bevel=0.006, seg=1))
        y += w + gap
    assert y - gap <= YI + R + 0.01, "the planks overrun the inner ledger (%.3f)" % (y - gap)
    # the hazard toe board inside the outer standards, standing on the deck
    add(K.box("toe_board", (-0.995, YO + R + 0.002, ZP), (0.995, YO + R + 0.027, ZP + 0.24), "accent",
              bevel=0.006, seg=1))
    # and a low one on the inner deck edge, toward the building
    add(K.box("toe_inner", (-0.995, y - gap + 0.004, ZP), (0.995, y - gap + 0.024, ZP + 0.12), "accent",
              bevel=0.004, seg=1))
    ob = finish(P, BAY, 1500)
    check_bounds(ob, (-1.0, -BAY_D / 2, 0.0), (1.0, BAY_D / 2, BAY_H), tol=0.005)
    visible_share(ob)
    return ob


# ================================================================== the lamp mast
def build_mast():
    new_kit()
    P = []
    add = P.append
    B, BH = 0.45, 0.40                       # ballast half width, height
    add(K.box("ballast", (-B, -B, 0.0), (B, B, BH), "hull", bevel=0.03, seg=2))
    # forklift pockets: two slate channels through the ballast's foot (the read of a block you set down)
    for y in (-0.22, 0.22):
        add(K.box("pocket", (-B - 0.004, y - 0.08, 0.04), (B + 0.004, y + 0.08, 0.13), "slate", bevel=0.0))
    # the hazard band round the ballast's top edge, 4 mm proud
    add(K.box("band", (-B - 0.004, -B - 0.004, BH - 0.17), (B + 0.004, B + 0.004, BH - 0.04), "accent", bevel=0.004,
              seg=1))
    # a slate deck plate on top, the pole's flange and four bolts
    add(K.box("top_plate", (-0.30, -0.30, BH - 0.004), (0.30, 0.30, BH + 0.03), "slate", bevel=0.008, seg=1))
    add(K.cyl("flange", (0.0, 0.0, BH + 0.02), (0.0, 0.0, BH + 0.06), 0.14, "slate", sides=12))
    for a in range(4):
        q = math.radians(45.0 + 90.0 * a)
        add(K.cyl("fbolt", (0.2 * math.cos(q), 0.2 * math.sin(q), BH + 0.025),
                  (0.2 * math.cos(q), 0.2 * math.sin(q), BH + 0.045), 0.022, "metal", sides=6))
    # the two-stage alloy pole and its slate collars
    Z1, Z2 = 2.60, 4.30
    add(K.cyl("pole_lo", (0.0, 0.0, BH + 0.05), (0.0, 0.0, Z1), 0.075, "metal", sides=12, caps=False))
    add(K.cyl("collar", (0.0, 0.0, Z1 - 0.10), (0.0, 0.0, Z1 + 0.10), 0.095, "accent", sides=12))
    add(K.cyl("pole_hi", (0.0, 0.0, Z1 + 0.05), (0.0, 0.0, Z2), 0.055, "metal", sides=12, caps=False))
    add(K.cyl("cap", (0.0, 0.0, Z2 - 0.05), (0.0, 0.0, Z2 + 0.04), 0.075, "slate", sides=12))
    # the cable: from the junction box on the ballast up the pole's -X side to the crossbar
    add(K.box("jbox", (-0.30, -0.12, BH + 0.02), (-0.14, 0.12, BH + 0.30), "slate", bevel=0.012, seg=1))
    add(K.pipe("cable", [(-0.22, 0.0, BH + 0.30), (-0.22, 0.0, BH + 0.50), (-0.095, 0.0, BH + 0.70),
                         (-0.095, 0.0, Z2 - 0.10), (-0.05, 0.0, Z2 + 0.10)], 0.018, "rubber", bend=0.08, sides=6,
                samples=3))
    # the crossbar (Y) on top, and the two lamp heads at its ends, aimed +X and 12 deg down: steeper and the lenses
    # turn away from the strategy camera at every yaw (55-75 deg down needs a tilt under 15 deg; the 2026-10-26
    # review measured the 35 deg lenses at 0 % of the visible surface)
    ZC = Z2 + 0.10
    add(K.box("crossbar", (-0.05, -0.62, ZC - 0.05), (0.05, 0.62, ZC + 0.05), "slate", bevel=0.01, seg=1))
    # an amber beacon on the crossbar's middle: the one lens the top-down camera sees at every yaw
    add(K.cyl("beacon_base", (0.0, 0.0, ZC + 0.045), (0.0, 0.0, ZC + 0.075), 0.075, "slate", sides=12))
    add(K.cyl("beacon", (0.0, 0.0, ZC + 0.07), (0.0, 0.0, ZC + 0.17), 0.055, "glow", sides=12))
    tilt = math.radians(12.0)
    for s in (-1, 1):
        y = s * 0.42
        head = [K.box("lamp_body", (-0.10, -0.17, -0.13), (0.06, 0.17, 0.13), "slate", bevel=0.02, seg=1),
                K.box("lamp_hood", (0.04, -0.18, 0.10), (0.16, 0.18, 0.135), "slate", bevel=0.006, seg=1),
                K.box("lamp_lens", (0.06, -0.145, -0.10), (0.070, 0.145, 0.095), "glow", bevel=0.0),
                K.box("lamp_rim", (0.055, -0.165, -0.12), (0.066, 0.165, 0.115), "metal", bevel=0.004, seg=1)]
        # the yoke: two alloy arms from the crossbar to the head's trunnions
        for e in (-1, 1):
            add(K.box("yoke", (-0.03, y + e * 0.19 - 0.015, ZC - 0.02), (0.03, y + e * 0.19 + 0.015, ZC + 0.16),
                      "metal", bevel=0.004, seg=1))
        for o in head:
            o.data.transform(Matrix.Rotation(tilt, 4, "Y"))       # +X face leans down
            o.data.transform(Matrix.Translation(Vector((0.02, y, ZC + 0.16))))
            add(o)
    ob = finish(P, MAST, 1500)
    lo, hi = bounds(ob)
    shift = Vector((-(lo.x + hi.x) / 2, -(lo.y + hi.y) / 2, -lo.z))
    assert shift.length < 0.003, "%s is off its ground-centred pivot by %s" % (MAST, shift)
    d = hi - lo
    rh_hs.log("%s %.3f x %.3f x %.3f m" % (MAST, d.x, d.y, d.z))
    assert 0.85 <= d.x <= 0.95 and 1.20 <= d.y <= 1.30 and 4.4 <= d.z <= 4.8, "unexpected mast size %s" % (d,)
    visible_share(ob)
    return ob


# ===================================================================== the crate
def build_crate():
    new_kit()
    P = []
    add = P.append
    h, H = 0.5, 1.0
    CC, CZ = 0.09, 0.08                      # corner casting plan size / height
    for ex in (-1, 1):
        for ey in (-1, 1):
            xa = sorted((ex * h, ex * (h - CC)))
            ya = sorted((ey * h, ey * (h - CC)))
            for za, role in (((0.0, CZ), "slate"), ((H - CZ, H), "accent")):   # the lift points are painted
                add(K.box("cast", (xa[0], ya[0], za[0]), (xa[1], ya[1], za[1]), role, bevel=0.0))
            px = sorted((ex * (h - 0.006), ex * (h - 0.075)))
            py = sorted((ey * (h - 0.006), ey * (h - 0.075)))
            add(K.box("post", (px[0], py[0], CZ - 0.005), (px[1], py[1], H - CZ + 0.005), "slate", bevel=0.0))
            # a lifting lug on each top casting
            add(K.box("lug", (xa[0] + 0.02, ya[0] + 0.02, H - 0.004), (xa[1] - 0.02, ya[1] - 0.02, H + 0.012),
                      "metal", bevel=0.003, seg=1))
    # top and bottom rails; the two top END rails (+-X, where the strap comes down) are the hazard lifting bars
    for za, top in (((0.006, 0.075), False), ((H - 0.065, H - 0.006), True)):
        for ey in (-1, 1):
            ya = sorted((ey * (h - 0.006), ey * (h - 0.06)))
            add(K.box("rail", (-h + CC - 0.005, ya[0], za[0]), (h - CC + 0.005, ya[1], za[1]), "slate", bevel=0.0))
        for ex in (-1, 1):
            xa = sorted((ex * (h - 0.006), ex * (h - 0.06)))
            add(K.box("rail", (xa[0], -h + CC - 0.005, za[0]), (xa[1], h - CC + 0.005, za[1]),
                      "accent" if top else "slate", bevel=0.0))
    # the body, recessed 3 cm behind the frame: amber lid and +-Y walls, bone-white +-X end panels. The lid is amber
    # because the strategy camera sees mostly lids (the 2026-10-26 review: bone-white lids read as all-white boxes;
    # the Supply Ship's containers read amber from above).
    BI = 0.03
    add(K.box("body", (-h + BI, -h + BI + 0.004, 0.07), (h - BI, h - BI - 0.004, H - 0.02), "cargo", bevel=0.006,
              seg=1))
    for sx in (-1, 1):
        xa = sorted((sx * (h - BI - 0.006), sx * (h - BI + 0.004)))
        add(K.box("end", (xa[0], -h + 0.075, 0.075), (xa[1], h - 0.075, H - 0.07), "hull", bevel=0.0))
    for sy in (-1, 1):
        ya = sorted((sy * (h - BI - 0.006), sy * (h - BI + 0.004)))
        add(K.box("side", (-h + 0.075, ya[0], 0.075), (h - 0.075, ya[1], H - 0.07), "cargo", bevel=0.0))
        # vertical ribs, and a hazard data plate between the middle ribs
        for dx in (-0.27, -0.09, 0.09, 0.27):
            yr = sorted((sy * (h - BI + 0.002), sy * (h - BI + 0.020)))
            add(K.box("rib", (dx - 0.022, yr[0], 0.09), (dx + 0.022, yr[1], H - 0.085), "cargo", bevel=0.0))
        yp = sorted((sy * (h - BI + 0.002), sy * (h - BI + 0.010)))
        add(K.box("plate", (-0.065, yp[0], 0.52), (0.065, yp[1], 0.82), "accent", bevel=0.0))
    for sx in (-1, 1):                       # +-X ends: two horizontal ribs
        for z in (0.36, 0.64):
            xr = sorted((sx * (h - BI - 0.002), sx * (h - BI + 0.016)))
            add(K.box("erib", (xr[0], -h + 0.09, z - 0.025), (xr[1], h - 0.09, z + 0.025), "hull", bevel=0.0))
    # the lid's roof ribs (Y), broken by the strap lane
    for dy in (-0.27, 0.27):
        for xa, xb in ((-h + 0.08, -0.11), (0.11, h - 0.08)):
            add(K.box("rrib", (xa, dy - 0.03, H - 0.024), (xb, dy + 0.03, H - 0.006), "cargo", bevel=0.0))
    # the dark strap over the lid along X and down both ends, a ratchet on top
    SW = 0.07
    add(K.box("strap", (-h + 0.004, -SW, H - 0.024), (h - 0.004, SW, H - 0.004), "rubber", bevel=0.0))
    for sx in (-1, 1):
        xa = sorted((sx * (h - BI - 0.004), sx * (h - 0.004)))
        add(K.box("strapd", (xa[0], -SW, 0.10), (xa[1], SW, H - 0.010), "rubber", bevel=0.0))
    add(K.box("ratchet", (0.12, -0.05, H - 0.006), (0.24, 0.05, H + 0.016), "metal", bevel=0.004, seg=1))
    ob = finish(P, CRATE, 800)
    check_bounds(ob, (-h, -h, 0.0), (h, h, H + 0.016))
    visible_share(ob)
    return ob


# ===================================================================== main
BUILDERS = {"pad": (PAD, build_pad, 0.10), "scaffold": (BAY, build_scaffold, 0.18),
            "mast": (MAST, build_mast, 0.20), "crate": (CRATE, build_crate, 0.15)}
for part in ("pad", "scaffold", "mast", "crate"):
    if PART not in ("all", part):
        continue
    name, build, dust = BUILDERS[part]
    ob = build()
    out = export_preview(ob, name)
    if not PREVIEW_ONLY:
        files = K.bake_and_export(ob, name, out, size=SIZE, dust=dust, ao_distance=0.25)
        rh_hs.log("files: %s" % files)
