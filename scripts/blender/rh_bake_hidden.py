"""Bake each building model's HIDDEN MASK: which surface the strategy camera can never see on the finished model.

  blender -b -P scripts/blender/rh_bake_hidden.py -- [--root Martians/gen/hs] [--size 1024] Name [Name ...]

For each <root>/<Name>/<Name>.glb (the hard-surface lane's import source, whose UVs the Unreal mesh carries) it writes
<root>/<Name>/<Name>_HiddenMask.png: white where a face can be seen from outside, black where it is closed inside
the model. Import with scripts/unreal/rh_import_masks.py as /Game/RedHope/Art/Masks/T_<Name>_HiddenMask (the building
lane's per-model naming, as T_<Name>_EmissiveMask and T_<Name>_Normal; RHColonyVisualizerSubsystem loads it by the
static mesh's name).

Why: a construction site's own model rises through M_RH_BuildReveal, a world-Z clip (visual pass item 2, DIRECTOR
ANSWER Q-A (a)). Above the cut the model is open, and the camera looks into it. The material already paints back faces
in InteriorColor, but these meshes also carry FRONT faces closed inside the solid - plinth tops, internal floors,
duplicate shells - which keep their raw bake: the 2026-10-26 art review saw a magenta slab inside the Electrolyzer and
lit beige floors inside the Forge and the AirFilter. The baked AO in the MR map's R cannot tell them apart (it is
traced 0.25 m: a floor in a large closed hall still reads open). This mask can: the reveal material paints the black
texels InteriorColor too.

How a face is judged: ten points on it (its centroid; points 60 % and 95 % of the way to each corner; and 95 % of the
way to each edge's middle - a big face partly in view must not be judged on its hidden middle), and rays from
each toward every direction the camera can look from - 16 yaws at 15, 35, 55 and 75 deg up, and straight up. If any
ray leaves the model's bounds without hitting it, the face can be seen; and, the other way round, every face that a
ray FROM one of those directions hits first is seen (120 x 120 rays a direction over the model's bounding sphere). A
face that is partly seen and partly not - a big internal slab with one corner showing through a door - is cut into
pieces about 1/60 of the model's size, each piece judged the same way (four points, and any forward hit inside it),
and the face is then painted WHOLE by its seen share of the area: seen when at least SEEN_MIN of it is, else hidden.
SEEN_MIN is the strictest of 0.5, 0.35, 0.25 that keeps the check below under FINISHED_MAX (logged per model).
(Round 2 painted each piece by itself; the 2026-10-27 art review saw the result at the cut - a lit strip of a floor's
seen part inside the dark Forge, with a saw-tooth edge where the pieces stepped.) A face mostly in view keeps its
paint; one seen only through an opening goes dark, the shadowed room behind the opening. No reliance on the normals
(several imports carry flipped ones): a ray from an outside face that heads into the solid hits its far wall. Faces are painted into
the mask by their UV triangles, hidden first, then the seen ones grown by 1.5 texels so a seen face never samples black
at a seam; the background stays white.

Read-only on its inputs. It logs its own sha256 and, per model, the hidden share of the area and the check that
matters: of the surface the camera sees on the FINISHED model, how much the mask would darken (asserted under
FINISHED_MAX, 5 %; round 2 painted each piece of a mixed face by itself and held it under 1 %).
"""
import hashlib
import math
import os
import sys

import bmesh
import bpy
import numpy as np
from mathutils import Vector
from mathutils.bvhtree import BVHTree

with open(os.path.abspath(__file__), "rb") as _f:
    print("[rh_bake_hidden] rh_bake_hidden.py sha256 %s" % hashlib.sha256(_f.read()).hexdigest())

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
ROOT = "Martians/gen/hs"
SIZE = 1024
names = []
i = 0
while i < len(argv):
    if argv[i] == "--root":
        ROOT = argv[i + 1]
        i += 2
    elif argv[i] == "--size":
        SIZE = int(argv[i + 1])
        i += 2
    else:
        names.append(argv[i])
        i += 1
assert names, "name at least one model"

SEEN_MINS = (0.5, 0.35, 0.25)  # a mixed face is painted seen whole at this share of its area seen, or more
# The most of the finished model's visible surface the mask may darken. The mask only acts while the site is built (the
# finished building wears its shipping MI), so this is the look of the last 5 % step: a few % of dark recesses - a floor
# seen through a door, the inside of an open hopper - that light up at completion. Measured per threshold: lower
# thresholds (0.15, 0.08) kept the Forge's interior floors lit at the cut (29 % dark at 50 %, against 51 % at 0.5).
FINISHED_MAX = 0.05

DIRS = [Vector((0.0, 0.0, 1.0))]
for pitch in (15.0, 35.0, 55.0, 75.0):
    for k in range(16):
        yaw = math.radians(22.5 * k)
        p = math.radians(pitch)
        DIRS.append(Vector((math.cos(p) * math.cos(yaw), math.cos(p) * math.sin(yaw), math.sin(p))))


def log(s):
    print("[rh_bake_hidden] " + s)


def load(name):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=os.path.join(ROOT, name, name + ".glb"))
    bm = bmesh.new()
    for o in [o for o in bpy.context.scene.objects if o.type == "MESH"]:
        t = bmesh.new()
        t.from_mesh(o.data)
        t.transform(o.matrix_world)
        me = bpy.data.meshes.new("tmp")
        t.to_mesh(me)
        bm.from_mesh(me)
        t.free()
    bmesh.ops.triangulate(bm, faces=bm.faces[:])
    bm.faces.ensure_lookup_table()
    return bm


def judge(bm):
    """The mask's triangles: [(uv tri in texels, seen share)], one per face: 1 or 0 for a face all seen or all hidden. A
    face partly seen and partly not is cut into sub-triangles to MEASURE its seen share of the area; raster() then
    paints it seen whole when the share is at least the threshold, else hidden whole (no ragged partial paint)."""
    tree = BVHTree.FromBMesh(bm)
    uvl = bm.loops.layers.uv.active
    vs = [v.co for v in bm.verts]
    lo = Vector([min(v[i] for v in vs) for i in range(3)])
    hi = Vector([max(v[i] for v in vs) for i in range(3)])
    diag = (hi - lo).length
    eps = 1e-5 * diag

    def escapes(p):
        for d in DIRS:
            if tree.ray_cast(p + d * eps, d)[0] is None:
                return True
        return False

    # The forward pass: from every camera direction, a 120 x 120 grid of rays over the model's bounding sphere; each
    # first hit is a point the camera sees.
    c, rad = (lo + hi) / 2, diag
    n = 120
    fwd = {}
    for d in DIRS:
        look = -d
        uu = look.cross(Vector((0.0, 0.0, 1.0)))
        uu = uu.normalized() if uu.length > 1e-6 else Vector((1.0, 0.0, 0.0))
        vv = uu.cross(look).normalized()
        for i in range(n):
            for j in range(n):
                o = c - look * rad * 2 + uu * ((i / (n - 1) - 0.5) * rad * 2) + vv * ((j / (n - 1) - 0.5) * rad * 2)
                hit = tree.ray_cast(o, look)
                if hit[0] is not None:
                    fwd.setdefault(hit[2], []).append(hit[0])

    def bary(a, b, cc, p):
        v0, v1, v2 = b - a, cc - a, p - a
        d00, d01, d11, d20, d21 = v0.dot(v0), v0.dot(v1), v1.dot(v1), v2.dot(v0), v2.dot(v1)
        den = d00 * d11 - d01 * d01 or 1e-12
        wb = (d11 * d20 - d01 * d21) / den
        wc = (d00 * d21 - d01 * d20) / den
        return (1.0 - wb - wc, wb, wc)

    out = []
    stats = {"whole_seen": 0, "whole_hidden": 0, "split": 0, "sub": 0}
    L = diag / 60.0
    for f in bm.faces:
        a, b, cc = [l.vert.co.copy() for l in f.loops]
        uv = [(l[uvl].uv.x * SIZE, (1.0 - l[uvl].uv.y) * SIZE) for l in f.loops]
        cen = (a + b + cc) / 3.0
        pts = [cen] + [cen.lerp(v, t) for t in (0.6, 0.95) for v in (a, b, cc)] + \
              [cen.lerp((v + w) / 2.0, 0.95) for v, w in ((a, b), (b, cc), (cc, a))]
        flags = [escapes(p) for p in pts]
        hits = fwd.get(f.index, [])
        if all(flags):
            out.append((uv, 1.0))
            stats["whole_seen"] += 1
            continue
        if not any(flags) and not hits:
            out.append((uv, 0.0))
            stats["whole_hidden"] += 1
            continue
        # Mixed: cut it in barycentric space until each piece's longest edge is under L (at most 4^5 pieces).
        stats["split"] += 1
        hb = [bary(a, b, cc, h) for h in hits]
        stack = [((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0), 0)]
        seen_area = all_area = 0.0

        def at(w):
            return a * w[0] + b * w[1] + cc * w[2]

        while stack:
            w0, w1, w2, depth = stack.pop()
            p0, p1, p2 = at(w0), at(w1), at(w2)
            if depth < 5 and max((p1 - p0).length, (p2 - p1).length, (p0 - p2).length) > L:
                m01 = tuple((x + y) / 2.0 for x, y in zip(w0, w1))
                m12 = tuple((x + y) / 2.0 for x, y in zip(w1, w2))
                m20 = tuple((x + y) / 2.0 for x, y in zip(w2, w0))
                stack += [(w0, m01, m20, depth + 1), (m01, w1, m12, depth + 1), (m20, m12, w2, depth + 1),
                          (m01, m12, m20, depth + 1)]
                continue
            sc = (p0 + p1 + p2) / 3.0
            ok = any(escapes(q) for q in (sc, sc.lerp(p0, 0.8), sc.lerp(p1, 0.8), sc.lerp(p2, 0.8)))
            if not ok and hb:
                # a forward hit inside this piece (its barycentrics, relative to the piece's corners)
                for h in hb:
                    t = bary(Vector(w0), Vector(w1), Vector(w2), Vector(h))
                    if min(t) >= -1e-6:
                        ok = True
                        break
            area = (p1 - p0).cross(p2 - p0).length
            all_area += area
            seen_area += area if ok else 0.0
            stats["sub"] += 1
        out.append((uv, min(0.999, seen_area / max(all_area, 1e-12))))
    shares = [sh for _uv, sh in out if 0.0 < sh < 1.0]
    log("faces whole seen %d, whole hidden %d, mixed %d (measured in %d pieces); mixed seen share: %s" % (
        stats["whole_seen"], stats["whole_hidden"], stats["split"], stats["sub"], ", ".join(
            "%.2f-%.2f %d" % (lo_, hi_, sum(1 for sh in shares if lo_ <= sh < hi_))
            for lo_, hi_ in ((0.0, 0.08), (0.08, 0.15), (0.15, 0.25), (0.25, 0.35), (0.35, 0.5), (0.5, 1.0)))))
    return out, lo, hi, tree


def raster(tris, seen_min):
    img = np.ones((SIZE, SIZE), dtype=np.float32)
    ys, xs = np.mgrid[0:SIZE, 0:SIZE]

    def paint(tri, value, grow):
        # tri: 3 x (u, v) in texels, v down; fill texel centres inside the triangle grown by `grow` texels
        (x0, y0), (x1, y1), (x2, y2) = tri
        bx0 = max(0, int(math.floor(min(x0, x1, x2) - grow - 1)))
        bx1 = min(SIZE - 1, int(math.ceil(max(x0, x1, x2) + grow + 1)))
        by0 = max(0, int(math.floor(min(y0, y1, y2) - grow - 1)))
        by1 = min(SIZE - 1, int(math.ceil(max(y0, y1, y2) + grow + 1)))
        if bx1 < bx0 or by1 < by0:
            return
        px = xs[by0:by1 + 1, bx0:bx1 + 1] + 0.5
        py = ys[by0:by1 + 1, bx0:bx1 + 1] + 0.5
        inside = np.ones(px.shape, dtype=bool)
        area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)
        sgn = 1.0 if area >= 0 else -1.0
        for (ax, ay), (bx, by) in (((x0, y0), (x1, y1)), ((x1, y1), (x2, y2)), ((x2, y2), (x0, y0))):
            ex, ey = bx - ax, by - ay
            ln = math.hypot(ex, ey) or 1e-9
            # signed distance of each texel centre from the edge, positive inside
            dist = sgn * (ex * (py - ay) - ey * (px - ax)) / ln
            inside &= dist >= -grow
        img[by0:by1 + 1, bx0:bx1 + 1][inside] = value

    for tri, s in tris:
        if s < seen_min:
            paint(tri, 0.0, 1.0)
    for tri, s in tris:
        if s >= seen_min:
            paint(tri, 1.0, 1.5)
    return img


def sample(img, bm, fi, p):
    uvl = bm.loops.layers.uv.active
    f = bm.faces[fi]
    a, b, c = [l.vert.co for l in f.loops]
    ua, ub, uc = [l[uvl].uv for l in f.loops]
    v0, v1, v2 = b - a, c - a, p - a
    d00, d01, d11, d20, d21 = v0.dot(v0), v0.dot(v1), v1.dot(v1), v2.dot(v0), v2.dot(v1)
    den = d00 * d11 - d01 * d01 or 1e-12
    wv = (d11 * d20 - d01 * d21) / den
    ww = (d00 * d21 - d01 * d20) / den
    u = ua * (1 - wv - ww) + ub * wv + uc * ww
    return img[min(SIZE - 1, max(0, int((1.0 - u.y) * SIZE))), min(SIZE - 1, max(0, int(u.x * SIZE)))]


def check(img, bm, tree, lo, hi, name):
    """What the strategy camera sees (55 / 75 deg, 8 yaws), above a cut: the share the mask darkens."""
    c, rad = (lo + hi) / 2, (hi - lo).length
    out = {}
    for frac in (1.0, 0.5):
        cut = lo.z + (hi.z - lo.z) * frac + (1e-4 if frac == 1.0 else 0.0)
        dark = tot = back = 0
        for pitch in (55.0, 75.0):
            for k in range(8):
                yaw = math.radians(45.0 * k + 22.5)
                d = Vector((-math.cos(yaw) * math.cos(math.radians(pitch)), -math.sin(yaw) * math.cos(math.radians(pitch)),
                            -math.sin(math.radians(pitch))))
                uu = d.cross(Vector((0, 0, 1))).normalized()
                vv = uu.cross(d).normalized()
                n = 40
                for i in range(n):
                    for j in range(n):
                        o = c - d * rad * 2 + uu * ((i / (n - 1) - 0.5) * rad) + vv * ((j / (n - 1) - 0.5) * rad)
                        for _ in range(64):
                            loc, nrm, fi, _dist = tree.ray_cast(o, d)
                            if loc is None:
                                break
                            if loc.z > cut:
                                o = loc + d * 1e-4
                                continue
                            tot += 1
                            if nrm.dot(d) >= 0:
                                back += 1
                            elif sample(img, bm, fi, loc) < 0.5:
                                dark += 1
                            break
        out[frac] = (dark, back, tot)
        log("%s cut %3d%%: of %d visible hits, %.1f %% are front faces the mask darkens, %.1f %% back faces" % (
            name, frac * 100, tot, 100.0 * dark / max(1, tot), 100.0 * back / max(1, tot)))
    return out


for name in names:
    bm = load(name)
    tris, lo, hi, tree = judge(bm)
    for seen_min in SEEN_MINS:
        img = raster(tris, seen_min)
        log("%s at SEEN_MIN %.2f: %d faces, %.1f %% of the mask's texels black" % (
            name, seen_min, len(bm.faces), 100.0 * float((img < 0.5).mean())))
        res = check(img, bm, tree, lo, hi, name)
        dark, _back, tot = res[1.0]
        if dark <= FINISHED_MAX * tot:
            break
    log("%s: SEEN_MIN %.2f" % (name, seen_min))
    assert dark <= FINISHED_MAX * tot, "%s: the mask darkens %.1f %% of the finished model's visible surface" % (name, 100.0 * dark / tot)
    out = os.path.join(ROOT, name, name + "_HiddenMask.png")
    im = bpy.data.images.new(name + "_HiddenMask", SIZE, SIZE, alpha=False, float_buffer=False)
    im.colorspace_settings.name = "Non-Color"
    rgba = np.ones((SIZE, SIZE, 4), dtype=np.float32)
    rgba[..., 0] = rgba[..., 1] = rgba[..., 2] = img
    im.pixels.foreach_set(rgba[::-1].ravel())
    im.filepath_raw = out
    im.file_format = "PNG"
    im.save()
    log("%s: wrote %s" % (name, out))
    bm.free()
