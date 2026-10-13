"""Build the HumidityRegulator from code - the hard-surface remake (2026-10-13).

  blender -b -P scripts/blender/rh_build_humidity.py -- [--out Martians/gen/hs/humidity]
      [--size 2048] [--preview-only]

Replaces the image-to-3D "humidity" mesh the director flagged "weird" on the
2026-10-13 triage (a shredded white cabinet with yellow blobs on its corners).
No approved reference exists, so the DESIGN comes from function, in the family
language of the buildings the director liked (Lander, Habitat, Pylon,
SolarArray - clean primitive silhouettes) and the rebuilt BatteryBank (bone-white
body, slate machinery, accent bezels, lit elements that mean something).

What it does (game text + docs/greenhouse-agriculture-spec.md section 4): holds a
greenhouse floor's climate at its setting (Humid / Temperate / Arid) - a small
powered machine standing among crop planters. So it reads as a compact climate
tower that stores water and makes mist:
  - a vertical bone-white WATER RESERVOIR tank (the body), an accent hoop band
    under its shoulder
  - a vertical SIGHT-GLASS level gauge on the tank's front: a slate strip, an
    isolation valve above and below the glass (hazard T-handles), the lit water
    column a continuous teal bar over black glass (continuous = liquid; the
    BatteryBank's segmented bars = charge, so the two do not read alike)
  - a MISTER head on top: a lit vapour hoop (the status ring) under a low
    bone-white DIFFUSER DOME that carries a ring of six chunky mist nozzles
    angled up and out round a bigger centre nozzle, every nozzle tip a lit
    orifice - discrete jets, the "fountain / sprinkler head" read
  - an INTAKE grille skirt round the base (dark slot, alloy bars)
  - a sloped STATUS console at the front: a lit droplet glyph and a humidity
    readout bar (lit fill over a visible dim track - one mesh serves every
    climate setting, so the panel shows a level, not a selected mode)
  - a CONDENSER on the back (it dehumidifies for Arid): fin pack in a slate
    frame with an accent rim, a copper serpentine with a feed and a return leg
    down to floor flanges
  - FEED pipes from the tank down the two back quarters to floor flanges
It must not read as the AirFilter (a box with a teal-ringed fan face) or the
WaterPlant (horizontal tanks on a skid): everything here is vertical and round,
and the top carries no ring-and-spoke motif.

Revision 1 (critics' round, same day): vent head rebuilt round a lit top face;
condenser coil dead end closed with a return leg; frame rails butt-join; gauge
fittings moved off the bezel to valves that frame it; one inspection hatch moved
to the back-left quarter (two front hatches + the gauge read as a face); feed
flanges dropped clear of the accent band.

Revision 2 (critics' round 2): the round-1 crown (louvre rings + radial ribs
over a lit disc inside a teal ring) read as a FAN GUARD - the AirFilter's
identity, same accent and glow - and as an electric hob once dusted; replaced by
the domed diffuser + lit nozzle jets above, and the six grey down-spouts on the
collar (read as pegs) folded into it. Hidden-face culling fixed: display decals
no longer count as solids (they had got the sight-glass window deleted), glass
and cell faces are never culled, and a downward face is culled only at ground
contact or on a box resting within 2 cm of an upward surface (the camera can
pitch near-horizontal: RHStrategyPawn clamps -6..86 deg) - tubes and lathed
rings keep every facet. Contact is now checked on the CULLED mesh by triangle
overlap / 2 mm gap. Accent raised to trim-in-every-view (wider hoop band,
condenser frame rim); coplanar same-facing pairs removed (hatch rims and tap
stub start deeper, intake bars inside the console skipped); bolts sink 1 mm;
gauge T-handles 17 mm; coil tube r 25 mm; condenser frame closes on the tank.
QA numbers are an orthographic raster (first hit per pixel), not area sums.

Layout facts: identity face (gauge + console) built at -Y, then K.face_camera
turns it to -X, the face the default strategy camera meets. Real-world metres,
ground-centred pivot; the visualizer fits the larger horizontal extent (the
plinth depth, console to condenser, ~1.36 m) to the 1-cell (2 m) footprint and
seats the bounds bottom on the ground, so only the proportions matter: about
1.15 : 1 height to depth, 1.4 : 1 height to width - a squat tower, ~2.3 m tall
in game.
"""
import math
import os
import sys
from collections import defaultdict

import bmesh
import bpy
from mathutils import Matrix, Vector
from mathutils.bvhtree import BVHTree

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rh_hs  # noqa: E402

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OUT = argv[argv.index("--out") + 1] if "--out" in argv else "Martians/gen/hs/humidity"
SIZE = int(argv[argv.index("--size") + 1]) if "--size" in argv else 2048
PREVIEW_ONLY = "--preview-only" in argv
NAME = "humidity"

K = rh_hs.Kit(accent_hex="#95EDE7")       # TintFor(HumidityRegulator)
# Runtime roles (this asset only):
#  copper   - condenser coil: RustBeam's hue (the family tie), as a metal
#  cell_dim - the unlit part of a readout: visibly lighter than the black glass
#             it sits on (cell_off on glass vanished), never emissive
for _name, _spec in (("copper", dict(albedo="#B86F4B", metal=0.60, rough=0.38, dust=0.6)),
                     ("cell_dim", dict(albedo="#33504C", metal=0.00, rough=0.30, dust=0.0))):
    K.roles[_name] = _spec
    K.mats[_name] = K._preview_material(_name, _spec)
parts = []
TAU = 2.0 * math.pi
DECALS = ("water", "drop", "rfill", "rtrack")   # display cells: 3 mm off their glass
KEEP_ROLES = {"glass", "cell_off", "cell_dim"}   # never culled; decals never are either


def add(o):
    parts.append(o)
    return o


def zv(z):
    return Vector((0.0, 0.0, z))


# ------------------------------------------------------------------ helpers
def box(*a, **k):
    """K.box tagged 'boxy': the only parts whose resting undersides may be culled."""
    o = K.box(*a, **k)
    o["boxy"] = 1
    return o


def sbolt(name, at, normal, r=0.014, h=0.008, role="metal"):
    """Hex bolt sunk 1 mm into its face, so its base cap is buried, not coincident."""
    n = Vector(normal).normalized()
    return K.cyl(name, Vector(at) - n * 0.001, Vector(at) + n * h, r, role, sides=6)


def lathe(name, prof, roles, seg_roles=None, sides=32, cap_bottom=False, cap_top=False,
          closed=False):
    """Revolve an (r, z) profile about Z. Trace it so the solid is on the left
    going bottom -> out -> up -> in (outer wall upward, tops inward, recess walls
    downward) and normals face out. seg_roles[i] = index into roles for segment
    prof[i] -> prof[i+1]. Caps default OFF: end a profile inside a neighbouring
    part instead, so no buried disc takes UV space. closed=True joins the last
    point back to the first (free-standing rings)."""
    bm = bmesh.new()
    rings = []
    for (r, z) in prof:
        rings.append([bm.verts.new((r * math.cos(TAU * k / sides),
                                    r * math.sin(TAU * k / sides), z))
                      for k in range(sides)])
    nseg = len(prof) if closed else len(prof) - 1
    for i in range(nseg):
        a, b = rings[i], rings[(i + 1) % len(prof)]
        for k in range(sides):
            k2 = (k + 1) % sides
            f = bm.faces.new((a[k], a[k2], b[k2], b[k]))
            f.material_index = seg_roles[i] if seg_roles else 0
    if cap_bottom and prof[0][0] > 1e-6:
        f = bm.faces.new(list(reversed(rings[0])))
        f.material_index = seg_roles[0] if seg_roles else 0
    if cap_top and prof[-1][0] > 1e-6:
        f = bm.faces.new(rings[-1])
        f.material_index = seg_roles[-1] if seg_roles else 0
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-6)
    return K._obj(name, bm, roles)


def cone(name, p0, p1, r0, r1, role, sides=12):
    """Truncated cone p0 (radius r0) -> p1 (radius r1)."""
    p0, p1 = Vector(p0), Vector(p1)
    axis = p1 - p0
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, cap_tris=False, segments=sides,
                          radius1=r0, radius2=r1, depth=axis.length)
    rot = Vector((0, 0, 1)).rotation_difference(axis.normalized()).to_matrix().to_4x4()
    bmesh.ops.transform(bm, matrix=Matrix.Translation((p0 + p1) / 2) @ rot, verts=bm.verts)
    return K._obj(name, bm, [role])


def poly(name, pts_xz, y, role, facing=-1):
    """Flat n-gon in an XZ plane at y, normal facing*Y (glyphs on displays)."""
    bm = bmesh.new()
    f = bm.faces.new([bm.verts.new((x, y, z)) for (x, z) in pts_xz])
    f.normal_update()
    if f.normal.y * facing < 0:
        f.normal_flip()
    return K._obj(name, bm, [role])


def droplet(cx, cz, r):
    """Teardrop outline (round bottom, point up) centred on its circle."""
    tip = 1.9 * r
    alpha = math.acos(r / tip)
    pts = [(cx, cz + tip)]
    n = 14
    for k in range(n + 1):
        t = alpha + (TAU - 2 * alpha) * k / n
        pts.append((cx + r * math.sin(t), cz + r * math.cos(t)))
    return pts


def place(objs, M):
    """Bake a transform into the mesh data of objs (keeps finish() simple)."""
    for o in objs:
        o.data.transform(M)
    return objs


def rot_z(deg):
    return Matrix.Rotation(math.radians(deg), 4, "Z")


def polar(deg, r, z):
    a = math.radians(deg)
    return Vector((r * math.cos(a), r * math.sin(a), z))


def tri_bvh(meshes):
    """BVH over the meshes' real loop triangles (what the GLB and UE get), plus
    triangle -> (mesh index, polygon index). BVHTree.FromPolygons fan-splits
    concave n-gons (chamfered box sides) into triangles that poke outside the
    face with flipped normals - false back-face hits in every ray test."""
    verts, tris, owner = [], [], []
    for k, me in enumerate(meshes):
        me.calc_loop_triangles()
        base = len(verts)
        verts += [v.co.copy() for v in me.vertices]
        for lt in me.loop_triangles:
            tris.append([base + i for i in lt.vertices])
            owner.append((k, lt.polygon_index))
    return BVHTree.FromPolygons(verts, tris, all_triangles=True), owner


# ------------------------------------------------------- QA / texel helpers
def _samples(co, vs, c):
    """Centre, corners and edge midpoints of a face, the latter pulled 30 % in."""
    pts = [c] + [co[i].lerp(c, 0.3) for i in vs]
    pts += [((co[vs[k]] + co[vs[(k + 1) % len(vs)]]) / 2).lerp(c, 0.3) for k in range(len(vs))]
    return pts


def _grid_samples(co, tris, c, step=0.008, cap=600):
    """Dense samples over a face's triangles (~8 mm apart, pulled 2 % toward the
    centre so none sits exactly on a shared edge): a face that is MOSTLY buried
    can still show a sliver - e.g. over the bevel groove between two butt-joined
    frame boxes - which a 9-point sample misses."""
    pts = []
    for (a, b, d) in tris:
        A, B, D = co[a], co[b], co[d]
        n = max(1, math.ceil(max((B - A).length, (D - B).length, (A - D).length) / step))
        for i in range(n + 1):
            for j in range(n + 1 - i):
                pts.append((A + (B - A) * (i / n) + (D - A) * (j / n)).lerp(c, 0.02))
    if len(pts) > cap:
        pts = pts[::math.ceil(len(pts) / cap)]
    return pts


def drop_hidden_faces(objs, offset=0.0015, rest_gap=0.02):
    """Delete faces no camera can meet, before the unwrap, so their UV space goes
    to the faces that show. Conservative: a face survives on any doubt.

      - glass and display-cell faces are never culled;
      - the BVH holds SOLIDS only: decals are single-sided quads 3 mm off their
        glass, and counting their backs as "inside" culled the gauge window;
      - ground contact: a flat downward face whose verts all sit at z <= 1 mm;
      - resting underside: a flat downward face of a BOX part where a ray
        straight down from every sample point meets an upward face within 2 cm
        (the strategy camera can pitch near-horizontal, so a free-hanging
        underside stays; tubes and lathed rings keep every facet);
      - buried: from every sample point, lifted 1.5 mm off the face, rays along
        the normal and 42 deg off it all strike the BACK of a surface, i.e. the
        face lies inside a solid. Samples: 9 per face, then confirmed on an
        ~8 mm grid (a mostly-buried face can still show a sliver).
    Returns the surviving objects (fully buried parts are removed)."""
    solids = [o for o in objs if not o.name.startswith(DECALS)]
    bvh, _owner = tri_bvh([o.data for o in solids])
    down = Vector((0, 0, -1))
    n_ground = n_rest = n_buried = 0
    area = 0.0
    keep = []
    for o in objs:
        if o not in solids:
            keep.append(o)
            continue
        me = o.data
        roles = [m["rh_role"] for m in me.materials]
        co = [v.co for v in me.vertices]
        me.calc_loop_triangles()
        tris_of = defaultdict(list)
        for lt in me.loop_triangles:
            tris_of[lt.polygon_index].append(tuple(lt.vertices))
        kill = []
        for p in me.polygons:
            if roles[p.material_index] in KEEP_ROLES:
                continue
            n, c = p.normal.copy(), p.center.copy()
            vs = list(p.vertices)
            pts = _samples(co, vs, c)
            if n.z < -0.995:
                if max(co[i].z for i in vs) <= 0.001:
                    kill.append(p.index)
                    n_ground += 1
                    area += p.area
                    continue
                if o.get("boxy"):
                    rest = True
                    for s in pts:
                        loc, hn, _i, _d = bvh.ray_cast(s + down * offset, down, rest_gap)
                        if loc is None or hn.z <= 0.5:
                            rest = False
                            break
                    if rest:
                        kill.append(p.index)
                        n_rest += 1
                        area += p.area
                        continue
            t1 = n.orthogonal().normalized()
            t2 = n.cross(t1)
            dirs = [n] + [(n + 0.9 * t).normalized() for t in (t1, -t1, t2, -t2)]

            def inside(samples):
                for s in samples:
                    q = s + n * offset
                    for d in dirs:
                        loc, hn, _i, _d = bvh.ray_cast(q, d, 100.0)
                        if loc is None or hn.dot(d) <= 0.0:
                            return False
                return True

            # cheap 9-point test first; confirm on the dense grid before culling
            if inside(pts) and inside(_grid_samples(co, tris_of[p.index], c)):
                kill.append(p.index)
                n_buried += 1
                area += p.area
        if kill:
            bm = bmesh.new()
            bm.from_mesh(me)
            bm.faces.ensure_lookup_table()
            bmesh.ops.delete(bm, geom=[bm.faces[i] for i in kill], context="FACES")
            bm.to_mesh(me)
            bm.free()
        if len(me.polygons):
            keep.append(o)
        else:
            bpy.data.objects.remove(o)
    rh_hs.log("hidden faces dropped: %d ground-contact + %d resting undersides + %d buried "
              "(%.2f m2), %d parts kept" % (n_ground, n_rest, n_buried, area, len(keep)))
    return keep


def check_contact(objs, gap=0.002, decal_gap=0.0035):
    """No floating parts, measured on the mesh that SHIPS (after culling): two
    parts touch when their triangles intersect or a vertex / face centre of one
    lies within 2 mm of the other's surface. Every part must reach a ground part
    through touching parts. Decals must lie within 3.5 mm of a solid."""
    info = []
    for o in objs:
        me = o.data
        vs = [v.co.copy() for v in me.vertices]
        tree, _owner = tri_bvh([me])
        smp = vs + [p.center.copy() for p in me.polygons]
        lo = Vector([min(v[i] for v in vs) for i in range(3)])
        hi = Vector([max(v[i] for v in vs) for i in range(3)])
        info.append((o.name.split(".")[0], tree, smp, lo, hi, o.name.startswith(DECALS)))
    n = len(info)
    adj = [set() for _ in range(n)]

    def bbgap(a, b):
        return max(max(a[3][i] - b[4][i], b[3][i] - a[4][i], 0.0) for i in range(3))

    for i in range(n):
        for j in range(i + 1, n):
            if info[i][5] or info[j][5] or bbgap(info[i], info[j]) > gap:
                continue
            hit = bool(info[i][1].overlap(info[j][1]))
            if not hit:
                hit = any(info[j][1].find_nearest(p, gap)[0] is not None for p in info[i][2]) or \
                    any(info[i][1].find_nearest(p, gap)[0] is not None for p in info[j][2])
            if hit:
                adj[i].add(j)
                adj[j].add(i)
    zmin = min(it[3].z for it in info)
    seen = {i for i in range(n) if info[i][3].z <= zmin + 1e-3 and not info[i][5]}
    todo = list(seen)
    while todo:
        i = todo.pop()
        for j in adj[i]:
            if j not in seen:
                seen.add(j)
                todo.append(j)
    loose = sorted({info[i][0] for i in range(n) if i not in seen and not info[i][5]})
    bad_decals = []
    for i in range(n):
        if not info[i][5]:
            continue
        near = min((info[j][1].find_nearest(info[i][2][-1], 0.05) or (None, None, None, 1.0))[3]
                   or 1.0 for j in range(n) if not info[j][5])
        if near > decal_gap:
            bad_decals.append("%s %.1f mm" % (info[i][0], 1000 * near))
    rh_hs.log("contact (shipped mesh): %d parts, unreachable from ground: %s; decals off their "
              "glass: %s" % (n, loose or "none", bad_decals or "none"))
    return loose, bad_decals


def tag_parts(objs):
    """Face attribute rh_part = part index, so QA after the join can name parts."""
    names = []
    for i, o in enumerate(objs):
        at = o.data.attributes.new("rh_part", "INT", "FACE")
        at.data.foreach_set("value", [i] * len(o.data.polygons))
        names.append(o.name.split(".")[0])
    return names


def check_coplanar(ob, names, gap=0.0029):
    """z-fight check (rule 7): a same-facing face of ANOTHER part lying in front
    of (or on) a face, closer than 3 mm, over a sample point of it."""
    me = ob.data
    vco = [v.co.copy() for v in me.vertices]
    bvh, owner = tri_bvh([me])
    part = [0] * len(me.polygons)
    me.attributes["rh_part"].data.foreach_get("value", part)
    hits = {}
    for p in me.polygons:
        n, c = p.normal, p.center
        for s in [c] + [vco[i].lerp(c, 0.3) for i in p.vertices]:
            for (q, hn, ti, dist) in bvh.find_nearest_range(s, gap):
                idx = owner[ti][1]
                if idx == p.index or part[idx] == part[p.index] or hn.dot(n) < 0.995:
                    continue
                dv = q - s
                h = dv.dot(n)
                if (dv - n * h).length < 1e-5 and abs(h) < gap:
                    key = tuple(sorted((names[part[p.index]], names[part[idx]])))
                    hits[key] = min(hits.get(key, 1.0), abs(h))
    rh_hs.log("coplanar same-facing overlaps < 3 mm: %s" % (
        ", ".join("%s/%s %.1f mm" % (a, b, 1000 * h) for (a, b), h in sorted(hits.items()))
        or "none"))
    return hits


def _bvh_roles(ob, names=None):
    """BVH of the joined mesh + a per-face label: role, or role/part when the
    rh_part attribute and the part names are available."""
    me = ob.data
    vco = [v.co.copy() for v in me.vertices]
    bvh, owner = tri_bvh([me])
    proles = [me.materials[p.material_index]["rh_role"] for p in me.polygons]
    roles = [proles[pi] for (_k, pi) in owner]          # indexed by BVH triangle
    labels = roles
    if names and "rh_part" in me.attributes:
        part = [0] * len(me.polygons)
        me.attributes["rh_part"].data.foreach_get("value", part)
        labels = ["%s/%s" % (proles[pi], names[part[pi]]) for (_k, pi) in owner]
    lo = Vector([min(v[i] for v in vco) for i in range(3)])
    hi = Vector([max(v[i] for v in vco) for i in range(3)])
    return bvh, roles, labels, lo, hi


def raster_view(ob, yaw, elev, res=240, names=None):
    """Orthographic raster from the lineup renderer's camera convention (yaw 180 =
    camera at -X): role share of the pixels the model covers (first hit per
    pixel), plus the share of pixels whose first hit is a BACK face (a see-
    through hole with single-sided materials; must be 0)."""
    bvh, roles, labels, lo, hi = _bvh_roles(ob, names)
    ctr, rad = (lo + hi) / 2, (hi - lo).length / 2
    where = defaultdict(int)
    e, y = math.radians(elev), math.radians(yaw)
    v = Vector((math.cos(y) * math.cos(e), -math.sin(y) * math.cos(e), math.sin(e)))
    right = v.cross(Vector((0, 0, 1))).normalized()
    up = right.cross(v).normalized()
    tot = defaultdict(int)
    back = hits = 0
    for i in range(res):
        for j in range(res):
            o = ctr + v * rad * 3 + right * ((i + 0.5) / res * 2 - 1) * rad \
                + up * ((j + 0.5) / res * 2 - 1) * rad
            loc, hn, idx, _d = bvh.ray_cast(o, -v, rad * 6)
            if idx is None:
                continue
            hits += 1
            if hn.dot(v) < 0:
                back += 1
                where["%s z%.2f" % (labels[idx], loc.z)] += 1
                continue
            tot[roles[idx]] += 1
    s = max(sum(tot.values()), 1)
    share = {k: 100.0 * c / s for k, c in tot.items()}
    rh_hs.log("raster yaw %3d elev %2d: back-face first hits %.3f %%; " % (
        yaw, elev, 100.0 * back / max(hits, 1)) + ", ".join(
        "%s %.1f%%" % (k, p) for k, p in sorted(share.items(), key=lambda kv: -kv[1])))
    if where:
        rh_hs.log("  back-face hits at: %s" % dict(sorted(where.items(), key=lambda kv: -kv[1])[:6]))
    return share, back


def lowcam_holes(ob, res=200, names=None):
    """Perspective see-through test from low cameras (height h above the floor,
    distance D in model metres; the closest game zoom is ~17 m model). Counts
    rays whose first hit is a back face. Must be 0."""
    bvh, roles, labels, lo, hi = _bvh_roles(ob, names)
    ctr, rad = (lo + hi) / 2, (hi - lo).length / 2
    worst = 0.0
    total = 0
    where = defaultdict(int)
    for D in (8.0, 17.0):
        for h in (0.1, 0.3, 1.0):
            nb = nh = 0
            for yaw in range(0, 360, 30):
                a = math.radians(yaw)
                cam = Vector((ctr.x + D * math.cos(a), ctr.y + D * math.sin(a), lo.z + h))
                fwd = (ctr - cam).normalized()
                right = fwd.cross(Vector((0, 0, 1))).normalized()
                up = right.cross(fwd).normalized()
                for i in range(res):
                    for j in range(res):
                        tgt = ctr + right * ((i + 0.5) / res * 2 - 1) * rad \
                            + up * ((j + 0.5) / res * 2 - 1) * rad
                        d = (tgt - cam).normalized()
                        loc, hn, idx, _d = bvh.ray_cast(cam, d, 100.0)
                        if idx is None:
                            continue
                        nh += 1
                        if hn.dot(d) > 0:
                            nb += 1
                            where["%s z%.2f" % (labels[idx], loc.z)] += 1
            pct = 100.0 * nb / max(nh, 1)
            worst = max(worst, pct)
            total += nh
    rh_hs.log("low-camera see-through (back-face first hit) over %d model hits, worst %.4f %% %s"
              % (total, worst, dict(sorted(where.items(), key=lambda kv: -kv[1])[:6]) if where else ""))
    return worst


def report_roles(ob):
    """Share of all outward surface per role, upward/side faces only."""
    me = ob.data
    tot = defaultdict(float)
    for p in me.polygons:
        if p.normal.z < -0.9:
            continue
        tot[me.materials[p.material_index]["rh_role"]] += p.area
    s = sum(tot.values())
    rh_hs.log("outward surface %.2f m2 by role: " % s + ", ".join(
        "%s %.1f%%" % (k, 100 * v / s) for k, v in sorted(tot.items(), key=lambda kv: -kv[1])))


def report_open_edges(ob, names):
    """Open (boundary) edge length by role, display decals excluded. Culled faces
    leave boundaries inside solids; the raster and low-camera tests above are the
    proof that none of them shows."""
    me = ob.data
    bm = bmesh.new()
    bm.from_mesh(me)
    lay = bm.faces.layers.int.get("rh_part")
    L = defaultdict(float)
    for e in bm.edges:
        if e.is_boundary:
            f = e.link_faces[0]
            if names[f[lay]].startswith(DECALS):
                continue
            L[me.materials[f.material_index]["rh_role"]] += e.calc_length()
    bm.free()
    rh_hs.log("open edges (m, decals excluded): %s" % ", ".join(
        "%s %.2f" % (k, v) for k, v in sorted(L.items(), key=lambda kv: -kv[1])))


# ------------------------------------------------------------- dimensions
R_TANK = 0.42                              # reservoir radius
Z_PLINTH = 0.08
Z_SKIRT = 0.34                             # skirt top = tank bottom
Z_TANK_TOP = 1.06                          # top of the straight wall
Z_NECK = 1.18                              # shoulder meets the neck


# ---------------------------------------------------------------- plinth
def vertical(d, m):
    return abs(d.z) > 0.99


PL = 0.68                                  # plinth half depth (y), symmetric: centred pivot
add(box("plinth", (-0.55, -PL, 0.0), (0.55, PL, Z_PLINTH), "slate", bevel=0.01,
        chamfers=[(vertical, 0.16)]))
add(box("hazard", (-0.30, -PL - 0.006, 0.030), (0.30, -PL + 0.01, 0.052), "hazard",
        bevel=0.002, seg=1))

# ---------------------------------------------------- intake skirt (round)
SK_LO, SK_HI = 0.13, 0.29
add(lathe("skirt", [(0.36, Z_PLINTH - 0.01), (0.40, Z_PLINTH - 0.01), (0.40, SK_LO),
                    (0.365, SK_LO), (0.365, SK_HI), (0.40, SK_HI), (0.40, Z_SKIRT + 0.01)],
          ["slate", "rubber"], seg_roles=[0, 0, 0, 1, 0, 0]))
NBAR = 30
for k in range(NBAR):
    ang = 360.0 * (k + 0.5) / NBAR
    mid = polar(ang, 0.376, 0.0)
    if abs(mid.x) < 0.24 and mid.y < -0.33:   # inside the console: no bar, no buried caps
        continue
    b = box("ibar", (0.36, -0.011, SK_LO - 0.005), (0.392, 0.011, SK_HI + 0.005), "metal",
            bevel=0.003, seg=1)
    add(place([b], rot_z(ang))[0])

# ---------------------------------------------------------- reservoir tank
c = 0.02                                   # rim chamfer
add(lathe("tank", [(0.39, Z_SKIRT), (R_TANK - c, Z_SKIRT), (R_TANK, Z_SKIRT + c),
                   (R_TANK, Z_TANK_TOP),
                   # elliptical shoulder up to the neck
                   *[(0.14 + (R_TANK - 0.14) * math.cos(t), Z_TANK_TOP + (Z_NECK - Z_TANK_TOP)
                      * math.sin(t)) for t in [math.radians(a) for a in (15, 30, 45, 60, 75, 90)]]],
          ["hull"], sides=40))
Z_BAND = Z_TANK_TOP - 0.055                # accent hoop band, under the shoulder
for zb, hh, brole in ((Z_SKIRT + 0.06, 0.025, "slate"), (Z_BAND, 0.05, "accent")):
    add(lathe("band", [(R_TANK - 0.008, zb - hh), (R_TANK + 0.012, zb - hh),
                       (R_TANK + 0.016, zb - hh + 0.005), (R_TANK + 0.016, zb + hh - 0.005),
                       (R_TANK + 0.012, zb + hh), (R_TANK - 0.008, zb + hh)],
              [brole], sides=40))

# Round inspection hatches: one on the front-right quarter, one on the back-left.
for ang in (-35.0, 170.0):
    a = math.radians(ang)
    d = Vector((math.cos(a), math.sin(a), 0))
    zc = 0.74
    add(K.cyl("hatch", d * (R_TANK - 0.03) + zv(zc), d * (R_TANK + 0.022) + zv(zc), 0.11,
              "hull", sides=24))
    add(K.cyl("hatchr", d * (R_TANK - 0.045) + zv(zc), d * (R_TANK + 0.012) + zv(zc), 0.13,
              "slate", sides=24))
    side = Vector((-d.y, d.x, 0))
    for j in range(6):
        t = TAU * j / 6
        bp = d * (R_TANK + 0.022) + zv(zc) + (side * math.cos(t) + zv(1.0) * math.sin(t)) * 0.08
        add(sbolt("hbolt", bp, d, r=0.012, h=0.008))

# ------------------------------------------------------------ mister head
# Stack: neck -> slate collar -> lit vapour hoop (the status ring, proud of
# everything above it) -> low bone-white diffuser dome -> a ring of six mist
# nozzles angled up and out + one centre nozzle, each tipped with a lit orifice.
# No rings or spokes on the top face: that is the AirFilter's fan guard.
Z_COL = 1.25                               # collar bottom
Z_R0 = Z_COL + 0.075                       # vapour hoop bottom (5 mm into the collar)
Z_R1 = Z_COL + 0.13                        # vapour hoop top
R_RING, R_DOME = 0.298, 0.28
Z_LIP = Z_R1 + 0.022                       # dome lip top = dome spring line
H_DOME = 0.10
R_TOP = 0.075                              # flat crown under the centre nozzle boss
Z_TOP = Z_LIP + H_DOME
add(K.cyl("neck", zv(Z_NECK - 0.03), zv(Z_COL + 0.01), 0.15, "slate", sides=24, caps=False))
add(lathe("collar", [(0.145, Z_COL), (0.25, Z_COL), (0.29, Z_COL + 0.03), (0.29, Z_COL + 0.08),
                     (0.283, Z_COL + 0.08)], ["slate"]))
# The hoop's annuli stop just inside the collar wall (bottom) and the dome lip
# (top): the covered inner rings are not built, so the emissive mask holds only
# lit surface that shows. The collar's top annulus likewise ends inside the hoop.
add(lathe("vapour", [(0.28, Z_R0), (R_RING, Z_R0), (R_RING, Z_R1), (R_DOME - 0.004, Z_R1)],
          ["cell_on"]))
DOME_T = [math.radians(a) for a in (0, 15, 30, 45, 60, 75, 90)]


def dome_rz(t):
    return R_TOP + (R_DOME - R_TOP) * math.cos(t), Z_LIP + H_DOME * math.sin(t)


add(lathe("dome", [(R_DOME, Z_R1 - 0.006), *[dome_rz(t) for t in DOME_T],
                   (R_TOP - 0.02, Z_TOP)], ["hull"], sides=36))
# Centre nozzle: slate boss, alloy cone, lit orifice.
add(K.cyl("cboss", zv(Z_TOP - 0.008), zv(Z_TOP + 0.03), R_TOP + 0.004, "slate", sides=20))
add(cone("cnoz", zv(Z_TOP + 0.026), zv(Z_TOP + 0.075), 0.058, 0.042, "metal", sides=16))
add(K.cyl("cori", zv(Z_TOP + 0.073), zv(Z_TOP + 0.079), 0.028, "cell_on", sides=16))
# Ring nozzles on the dome's shoulder, tilted 38 deg out from vertical.
SPOUT_ANG = [-120.0, -60.0, 0.0, 60.0, 120.0, 180.0]   # front pair straddles the gauge
T_NOZ = math.radians(52.0)                  # dome parameter where they stand
TILT = math.radians(38.0)
for ang in SPOUT_ANG:
    rn, zn = dome_rz(T_NOZ)
    base = polar(ang, rn, zn)
    ax = (polar(ang, math.sin(TILT), 0.0) + zv(math.cos(TILT))).normalized()
    p0 = base - ax * 0.02                   # starts inside the dome
    p1 = base + ax * 0.035
    add(K.cyl("nboss", p0, p1, 0.044, "accent", sides=14))   # teal collars: trim on the top
    tip = p1 + ax * 0.055
    add(cone("noz", p1 - ax * 0.004, tip, 0.036, 0.029, "metal", sides=12))
    add(K.cyl("nori", tip - ax * 0.002, tip + ax * 0.005, 0.02, "cell_on", sides=12))

# --------------------------------------------------- sight-glass level gauge
GZ0, GZ1 = 0.63, 1.00
GY = -(R_TANK + 0.035)                      # gauge strip front face


def gtop_front(d, m):
    return abs(d.x) > 0.99 and m.z > GZ1 + 0.09 and m.y < GY + 0.01


add(box("ghouse", (-0.095, GY, 0.44), (0.095, -R_TANK + 0.05, GZ1 + 0.10), "slate",
        bevel=0.012, chamfers=[(gtop_front, 0.03)]))
GW, GH = 0.15, GZ1 - GZ0
gz = (GZ0 + GZ1) / 2
bz, wy = K.bezel("gbezel", (0.0, GY + 0.002, gz), GW, GH, 0.02, radius=0.04, rim=0.022,
                 recess=0.012, rim_role="accent", inner_role="glass", facing=-1)
add(bz)
inner_h = GH - 2 * 0.022 - 0.024
level = 0.64
zlo = gz - inner_h / 2
add(K.quad("water", (0, wy - 0.003, zlo + inner_h * level / 2), GW - 0.072, inner_h * level,
           "cell_on"))
# Isolation valves above and below the glass: hex body, stem, hazard T-handle.
for zf in (GZ1 + 0.05, GZ0 - 0.05):
    add(K.cyl("gvalve", (0, GY + 0.01, zf), (0, GY - 0.032, zf), 0.036, "metal", sides=6))
    add(K.cyl("gstem", (0, GY - 0.026, zf), (0, GY - 0.05, zf), 0.015, "pipe", sides=10))
    add(box("ghandle", (-0.034, GY - 0.056, zf - 0.009), (0.034, GY - 0.039, zf + 0.009),
            "hazard", bevel=0.004, seg=1))

# ---------------------------------------------------- status console (front)
CY0, CY1, CZ1, CH = -0.62, -0.36, 0.52, 0.19


def front_top(d, m):
    return abs(d.x) > 0.99 and m.z > CZ1 - 0.01 and m.y < CY0 + 0.01


add(box("console", (-0.21, CY0, Z_PLINTH - 0.01), (0.21, CY1, CZ1), "slate", bevel=0.012,
        chamfers=[(front_top, CH), (vertical, 0.03)]))
slope_c = Vector((0.0, CY0 + CH / 2, CZ1 - CH / 2))
tilt = Matrix.Translation(slope_c) @ Matrix.Rotation(math.radians(-45.0), 4, "X")
DW, DH, DRIM = 0.36, 0.18, 0.024
dz, dwy = K.bezel("dbezel", (0.0, 0.002, 0.0), DW, DH, 0.02, radius=0.045, rim=DRIM,
                  recess=0.012, rim_role="accent", inner_role="glass", facing=-1)
disp = [dz]
gy = dwy - 0.003
RD = 0.034                                  # droplet glyph, vertically centred in the window
disp.append(poly("drop", droplet(-0.098, -0.45 * RD, RD), gy, "cell_on"))
# Humidity readout: a lit fill over a dim track (adjacent quads, no overlap).
RX0, RX1, RH_, FILL = -0.042, 0.128, 0.05, 0.6
xf = RX0 + FILL * (RX1 - RX0)
disp.append(K.quad("rfill", ((RX0 + xf) / 2, gy, 0.0), xf - RX0, RH_, "cell_on"))
disp.append(K.quad("rtrack", ((xf + RX1) / 2, gy, 0.0), RX1 - xf, RH_, "cell_dim"))
place(disp, tilt)
parts += disp
top_back = tilt @ Vector((0.0, 0.002, DH / 2))   # bezel top edge on the slope
rh_hs.log("display top edge y %.3f z %.3f; gauge strip face y %.3f -> clearance %.1f mm"
          % (top_back.y, top_back.z, GY, 1000 * (GY - top_back.y)))
for bxp in (-0.15, 0.15):                   # console face bolts (vertical lower face)
    add(sbolt("cbolt", (bxp, CY0, 0.15), (0, -1, 0)))
TZ = 0.20                                   # drain tap: stub, down-spout, T-handle
add(K.cyl("tapstub", (0, CY0 + 0.02, TZ), (0, CY0 - 0.055, TZ), 0.024, "metal", sides=12))
add(K.cyl("tapflange", (0, CY0 + 0.01, TZ), (0, CY0 - 0.012, TZ), 0.04, "slate", sides=12))
add(K.cyl("tapspout", (0, CY0 - 0.045, TZ + 0.005), (0, CY0 - 0.045, TZ - 0.06), 0.02, "metal",
          sides=12))
add(K.cyl("tapstem", (0, CY0 - 0.035, TZ), (0, CY0 - 0.035, TZ + 0.05), 0.011, "metal", sides=8))
add(box("taphandle", (-0.045, CY0 - 0.047, TZ + 0.045), (0.045, CY0 - 0.023, TZ + 0.065),
        "hazard", bevel=0.005))

# ------------------------------------------------- condenser coil (back)
CX = 0.23
CYA, CYB = 0.32, 0.60                       # CYA closes the frame onto the tank (r 0.42)
CZA, CZB = 0.44, 0.98
add(box("cframe_t", (-CX - 0.03, CYA, CZB), (CX + 0.03, CYB, CZB + 0.04), "slate", bevel=0.008))
add(box("cframe_b", (-CX - 0.03, CYA, CZA - 0.04), (CX + 0.03, CYB, CZA), "slate", bevel=0.008))
for s in (-1, 1):                           # side rails butt-join between the top and bottom
    add(box("cframe_s", (min(s * CX, s * (CX + 0.03)), CYA, CZA),
            (max(s * CX, s * (CX + 0.03)), CYB, CZB), "slate", bevel=0.008))
# Accent rim on the frame's back face: full-height side strips; the top and
# bottom strips run 6 mm INTO them and stand 4 mm less proud (a butt joint of
# two bevelled boxes left a notch a low camera could see into once the end
# faces were culled; equal fronts would z-fight where they overlap).
RIM_Y0, RIM_Y1 = CYB - 0.006, CYB + 0.012
for s in (-1, 1):
    add(box("crim_s", (min(s * (CX + 0.004), s * (CX + 0.026)), RIM_Y0, CZA - 0.034),
            (max(s * (CX + 0.004), s * (CX + 0.026)), RIM_Y1, CZB + 0.034), "accent",
            bevel=0.003, seg=1))
for z0, z1 in ((CZB + 0.006, CZB + 0.034), (CZA - 0.034, CZA - 0.006)):
    add(box("crim_h", (-CX - 0.010, RIM_Y0, z0), (CX + 0.010, RIM_Y1 - 0.004, z1), "accent",
            bevel=0.003, seg=1))
NF = 12
for k in range(NF):
    fx = -CX + 0.02 + (2 * CX - 0.04) * k / (NF - 1)
    add(box("fin", (fx - 0.008, CYA + 0.03, CZA), (fx + 0.008, CYB - 0.03, CZB), "metal",
            bevel=0.003, seg=1))
NROW, CYC = 7, CYB - 0.016                  # coil runs across the fin face
XE = CX + 0.07                              # row ends (hairpins outside the frame)
zrows = [CZA + 0.05 + (CZB - CZA - 0.10) * k / (NROW - 1) for k in range(NROW)]
pts = []
for k, zr in enumerate(zrows):
    xs = (-XE, XE) if k % 2 == 0 else (XE, -XE)
    pts += [(xs[0], CYC, zr), (xs[1], CYC, zr)]
add(K.pipe("coil", pts, 0.025, "copper", bend=0.04, sides=10, samples=5))
# Feed (bottom row, left) and return (top row, right) legs to floor flanges,
# stepped outboard so they clear the hairpins. Each starts inside the coil's
# end, swallowing its cap.
XD = CX + 0.145
for s, zr in ((-1, zrows[0]), (1, zrows[-1])):
    add(K.pipe("cleg", [(s * (XE - 0.01), CYC, zr), (s * XD, CYC, zr),
                        (s * XD, CYC, Z_PLINTH - 0.01)], 0.030, "copper", bend=0.045, sides=10))
    add(K.cyl("cflange", (s * XD, CYC, Z_PLINTH - 0.01), (s * XD, CYC, Z_PLINTH + 0.035),
              0.048, "slate", sides=12))

# ----------------------------------------- feed pipes (the two back quarters)
PZ = Z_BAND - 0.11                          # tank flange (r 5 cm) clears the accent band
for ang in (40.0, 140.0):
    d = polar(ang, 1.0, 0.0)
    add(K.pipe("feed", [d * (R_TANK - 0.05) + zv(PZ), d * 0.50 + zv(PZ),
                        d * 0.50 + zv(Z_PLINTH - 0.01)], 0.032, "pipe", bend=0.08))
    add(K.cyl("tflange", d * (R_TANK - 0.03) + zv(PZ), d * (R_TANK + 0.03) + zv(PZ), 0.05,
              "slate", sides=12))
    add(K.cyl("fflange", d * 0.50 + zv(Z_PLINTH - 0.01), d * 0.50 + zv(Z_PLINTH + 0.04), 0.055,
              "slate", sides=12))
    clamp = box("clamp", (0.37, -0.025, 0.53), (0.54, 0.025, 0.59), "slate", bevel=0.008)
    add(place([clamp], rot_z(ang))[0])

# ------------------------------------------------------------------ finish
parts = drop_hidden_faces(parts)
check_contact(parts)
names = tag_parts(parts)
ob = K.finish(parts, NAME)
K.face_camera(ob)
vs = [v.co for v in ob.data.vertices]
ctr = Vector(((min(v.x for v in vs) + max(v.x for v in vs)) / 2,
              (min(v.y for v in vs) + max(v.y for v in vs)) / 2, min(v.z for v in vs)))
ob.data.transform(Matrix.Translation(-ctr))   # bounds centred on XY, bottom at z=0
report_roles(ob)
for yaw, elev in ((180, 54), (180, 28), (145, 28), (215, 28), (35, 28), (180, 80)):
    raster_view(ob, yaw, elev, names=names)
lowcam_holes(ob, names=names)
check_coplanar(ob, names)
report_open_edges(ob, names)
ob.data.attributes.remove(ob.data.attributes["rh_part"])
os.makedirs(OUT, exist_ok=True)
bpy.ops.object.select_all(action="DESELECT")
ob.select_set(True)
bpy.ops.export_scene.gltf(filepath=os.path.join(OUT, NAME + "_preview.glb"), export_format="GLB",
                          use_selection=True, export_apply=True)
rh_hs.log("preview glb written; bounds x %.3f..%.3f y %.3f..%.3f z %.3f..%.3f" % (
    min(v.co.x for v in ob.data.vertices), max(v.co.x for v in ob.data.vertices),
    min(v.co.y for v in ob.data.vertices), max(v.co.y for v in ob.data.vertices),
    min(v.co.z for v in ob.data.vertices), max(v.co.z for v in ob.data.vertices)))
if not PREVIEW_ONLY:
    files = K.bake_and_export(ob, NAME, OUT, size=SIZE, dust=0.22)
    rh_hs.log("files: %s" % files)
