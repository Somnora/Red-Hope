"""rh_lib — shared helpers for the Red Hope asset finishing lane.

Runs inside Blender (`blender --background --python <driver> -- ...`).
Target: Blender 5.2 LTS. Written defensively because several relevant operators
were renamed between 3.x and 5.x; every risky call is wrapped and reported so a
stage fails loudly with a named cause instead of silently doing nothing.

Doctrine this file enforces (see docs/art-bible.md):
  - NEVER `shade_auto_smooth` — it adds a modifier whose custom normals the glTF
    exporter drops. Smoothing is set on polygons directly, with sharp edges
    marked by angle.
  - Ground-centred pivot, real-world metres, single mesh object out.
"""

import bpy
import bmesh
import json
import math
import os
import sys
from mathutils import Vector

LOG = []


def log(msg):
    """Every stage narrates itself; the driver prints this as the receipt."""
    LOG.append(msg)
    print(f"[rh] {msg}", flush=True)


def argv_after_ddash():
    return sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []


# --------------------------------------------------------------------------
# scene / io
# --------------------------------------------------------------------------

def reset_scene():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete()
    for block in (bpy.data.meshes, bpy.data.materials, bpy.data.images):
        for item in list(block):
            if item.users == 0:
                block.remove(item)


def import_glb(path):
    if not os.path.exists(path):
        raise SystemExit(f"[rh] FATAL: no such GLB: {path}")
    bpy.ops.import_scene.gltf(filepath=path)
    meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    if not meshes:
        raise SystemExit(f"[rh] FATAL: GLB contained no mesh: {path}")
    log(f"imported {os.path.basename(path)}: {len(meshes)} mesh object(s)")
    return meshes


def join_meshes(meshes):
    """One mesh object out — the export contract."""
    bpy.ops.object.select_all(action="DESELECT")
    for o in meshes:
        o.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    if len(meshes) > 1:
        bpy.ops.object.join()
    ob = bpy.context.view_layer.objects.active
    # Bake transforms in so world == object space for projection math later.
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    return ob


def load_mesh(path):
    reset_scene()
    return join_meshes(import_glb(path))


def world_bbox(ob):
    mn = Vector((1e18, 1e18, 1e18))
    mx = -mn
    for c in ob.bound_box:
        w = ob.matrix_world @ Vector(c)
        mn = Vector((min(mn[i], w[i]) for i in range(3)))
        mx = Vector((max(mx[i], w[i]) for i in range(3)))
    return mn, mx


def ground_center_pivot(ob):
    """Pivot at the footprint centre, resting on Z=0 — how UE expects to seat it."""
    mn, mx = world_bbox(ob)
    centre = Vector(((mn.x + mx.x) / 2, (mn.y + mx.y) / 2, mn.z))
    me = ob.data
    for v in me.vertices:
        v.co -= centre
    ob.location = (0.0, 0.0, 0.0)
    me.update()
    log(f"pivot: ground-centred (shifted {tuple(round(c, 4) for c in centre)})")
    return centre


def export_glb(ob, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    bpy.ops.object.select_all(action="DESELECT")
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    kwargs = dict(filepath=path, export_format="GLB", use_selection=True)
    # export_apply changed name across versions; try the modern spelling first.
    for extra in ({"export_apply": True}, {}):
        try:
            bpy.ops.export_scene.gltf(**kwargs, **extra)
            log(f"exported {path} ({os.path.getsize(path)//1024} KB)")
            return path
        except TypeError:
            continue
    raise SystemExit("[rh] FATAL: glTF export signature not understood")


# --------------------------------------------------------------------------
# C2 — shading
# --------------------------------------------------------------------------

def weld(ob, dist=0.0001):
    me = ob.data
    before = len(me.vertices)
    bm = bmesh.new()
    bm.from_mesh(me)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=dist)
    bm.to_mesh(me)
    bm.free()
    me.update()
    after = len(me.vertices)
    log(f"weld: {before} -> {after} verts ({100*(before-after)/max(before,1):.1f}% merged)")
    return before, after


def smooth_by_angle(ob, angle_deg=40.0):
    """The documented gotcha, handled correctly.

    Set use_smooth on every polygon, then mark edges sharper than the threshold
    as sharp. This survives glTF export; `shade_auto_smooth` does not.
    """
    me = ob.data
    for p in me.polygons:
        p.use_smooth = True
    thresh = math.radians(angle_deg)
    bm = bmesh.new()
    bm.from_mesh(me)
    bm.edges.ensure_lookup_table()
    sharp = 0
    for e in bm.edges:
        if len(e.link_faces) == 2:
            if e.calc_face_angle() > thresh:
                e.smooth = False
                sharp += 1
        else:
            e.smooth = False
    bm.to_mesh(me)
    bm.free()
    me.update()
    log(f"smooth: all polys smooth, {sharp} edges marked sharp (>{angle_deg} deg)")
    return sharp


def decimate_to(ob, max_tris):
    tris = tri_count(ob)
    if tris <= max_tris:
        log(f"decimate: skipped ({tris} tris already <= {max_tris})")
        return tris
    ratio = max_tris / float(tris)
    mod = ob.modifiers.new("RHDecimate", "DECIMATE")
    mod.ratio = ratio
    bpy.context.view_layer.objects.active = ob
    bpy.ops.object.modifier_apply(modifier=mod.name)
    out = tri_count(ob)
    log(f"decimate: {tris} -> {out} tris (target {max_tris}, ratio {ratio:.3f})")
    return out


# --------------------------------------------------------------------------
# C3 — UVs
# --------------------------------------------------------------------------

def smart_uv(ob, name="RHBake", angle_deg=66.0, margin=0.02):
    me = ob.data
    if name in me.uv_layers:
        me.uv_layers.remove(me.uv_layers[name])
    layer = me.uv_layers.new(name=name)
    me.uv_layers.active = layer
    layer.active_render = True
    bpy.context.view_layer.objects.active = ob
    bpy.ops.object.select_all(action="DESELECT")
    ob.select_set(True)
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    ok = False
    for kwargs in (dict(angle_limit=math.radians(angle_deg), island_margin=margin),
                   dict(angle_limit=angle_deg, island_margin=margin),
                   dict()):
        try:
            bpy.ops.uv.smart_project(**kwargs)
            ok = True
            break
        except TypeError:
            continue
    if ok:
        for kwargs in (dict(margin=margin), dict()):
            try:
                bpy.ops.uv.pack_islands(**kwargs)
                break
            except TypeError:
                continue
    bpy.ops.object.mode_set(mode="OBJECT")
    if not ok:
        raise SystemExit("[rh] FATAL: smart_project signature not understood")
    log(f"uv: '{name}' unwrapped (angle {angle_deg} deg, margin {margin})")
    return layer


def uv_utilisation(ob, uv_name=None):
    """Fraction of UV space covered — the §4 budget number."""
    me = ob.data
    layer = me.uv_layers.get(uv_name) if uv_name else me.uv_layers.active
    if layer is None:
        return 0.0
    total = 0.0
    for poly in me.polygons:
        pts = [layer.data[li].uv for li in poly.loop_indices]
        area = 0.0
        for i in range(1, len(pts) - 1):
            a, b, c = pts[0], pts[i], pts[i + 1]
            area += abs((b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y)) / 2.0
        total += area
    return total


# --------------------------------------------------------------------------
# metrics
# --------------------------------------------------------------------------

def tri_count(ob):
    return sum(len(p.vertices) - 2 for p in ob.data.polygons)


def surface_area(ob):
    return sum(p.area for p in ob.data.polygons)


def metrics(ob, uv_name=None):
    mn, mx = world_bbox(ob)
    dims = tuple(round(mx[i] - mn[i], 4) for i in range(3))
    uvu = uv_utilisation(ob, uv_name)
    area = surface_area(ob)
    m = {
        "tris": tri_count(ob),
        "verts": len(ob.data.vertices),
        "materials": len(ob.data.materials),
        "uv_layers": [l.name for l in ob.data.uv_layers],
        "uv_utilisation": round(uvu, 4),
        "dims_m": dims,
        "surface_area_m2": round(area, 4),
        # texels per metre at 2K, assuming uv space maps to the surface
        "texel_density_2k": round(2048.0 * math.sqrt(max(uvu, 1e-9) / max(area, 1e-9)), 1),
    }
    return m


def write_report(path, payload):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    payload = dict(payload)
    payload["log"] = LOG
    with open(path, "w") as fh:
        json.dump(payload, fh, indent=2)
    log(f"report -> {path}")
