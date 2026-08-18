"""rh_gen_rocks — procedural low-poly Mars rock generator. Blender headless, no GPU.

    blender -b -P scripts/blender/rh_gen_rocks.py -- \
        [--out-dir Martians/gen/rocks] [--count 5] \
        [--preview Martians/gen/rocks/rocks_preview.png] [--preview-px 1200x500]

Each variant starts from an icosphere and gets 3 octaves of deterministic
mathutils.noise displacement along its own radius, a flattened underside (rocks sit
IN regolith, not on it), decimation to 250-600 tris, smoothing with sharp facet
edges, a Smart UV Project, and its own GLB export. Archetype cycles
boulder/slab/boulder/slab/shard across --count variants (two flat-ish slabs, two
rounded boulders, one angular shard at the default count of 5); size climbs
30 -> 150 cm tallest bbox dimension linearly across the run.

Determinism: mathutils.noise.seed_set() only seeds random()/random_vector()/
random_unit_vector() — noise()/turbulence() are pure functions of position, with
no seed of their own (verified on this build: same position, different seed_set,
identical output). So each variant reseeds the RNG with its own seed and draws its
displacement sample-domain OFFSETS and its proportion jitter from that seeded
stream; the noise field sampled at those offsets is what actually varies rock to
rock, and it reproduces exactly run to run because seed_set() itself is
deterministic across fresh processes (also verified on this build).

Shading: deliberately NOT `bpy.ops.object.shade_smooth_by_angle` (confirmed present
on this build). That operator adds a "Smooth by Angle" modifier whose custom split
normals the glTF exporter has already been measured on this project to drop on
export, silently re-flattening the shading after an import round-trip (see
scripts/blender/rh_lib.py and its README's "Two rules this kit encodes"). Instead,
smoothing is set directly on polygons with sharp edges marked by face angle via
bmesh, the same technique rh_lib.py's smooth_by_angle() uses, duplicated here
rather than imported so this script stays self-contained.

Blender 5.2 API notes: `bpy.ops.wm.read_factory_settings(use_empty=True)` clears the
default scene first; `BLENDER_EEVEE` is the only engine identifier on this build
(no `_NEXT` variant), so the render path checks the enum before choosing.
"""
import argparse
import math
import os
import sys

import bmesh
import bpy
from mathutils import Vector
from mathutils import noise

ARCHETYPE_CYCLE = ["boulder", "slab", "boulder", "slab", "shard"]
ARCHETYPE_TRIS = {"boulder": 500, "slab": 340, "shard": 420}

DISPLACE_OCTAVES = 3
DISPLACE_BASE_FREQ = 1.6
DISPLACE_LACUNARITY = 2.0
DISPLACE_PERSISTENCE = 0.5
DISPLACE_CLAMP = (0.55, 1.6)  # hard floor/ceiling on the per-vertex radius multiplier

FLATTEN_FRACTION = 1.0 / 3.0   # lower third of the current bbox gets compressed
FLATTEN_AMOUNT = 0.82          # fraction of that third's depth squashed away
TAPER_FRACTION = 1.0 / 3.0     # shard only: upper third pulled inward

ICOSPHERE_SUBDIV = 4
UV_ANGLE_DEG = 66.0
UV_MARGIN = 0.02
SHADE_ANGLE_DEG = 35.0


def log(msg):
    print(f"[rocks] {msg}", flush=True)


def build_specs(count):
    """count deterministic rock specs: archetype cycles, size climbs 30->150 cm."""
    specs = []
    for i in range(count):
        archetype = ARCHETYPE_CYCLE[i % len(ARCHETYPE_CYCLE)]
        t = i / float(max(count - 1, 1))
        dim_cm = 30.0 + t * (150.0 - 30.0)
        seed = 1013 + i * 1014
        specs.append(dict(
            idx=i, name=f"rock_{i:02d}", archetype=archetype, seed=seed,
            target_dim_cm=dim_cm, target_tris=ARCHETYPE_TRIS[archetype],
        ))
    return specs


def archetype_shape(archetype, jitter):
    """Per-axis scale (applied before uniform resize-to-target), displacement
    amplitude, and (shard only) a taper amount. jitter is a seeded Vector in
    (-1, 1) so two rocks of the same archetype are not identical."""
    if archetype == "boulder":
        scale_xyz = (1.00 + 0.10 * jitter.x, 1.00 + 0.10 * jitter.y, 0.85 + 0.08 * jitter.z)
        return scale_xyz, 0.30, None
    if archetype == "slab":
        scale_xyz = (1.30 + 0.15 * jitter.x, 1.10 + 0.10 * jitter.y, 0.40 + 0.05 * jitter.z)
        return scale_xyz, 0.20, None
    if archetype == "shard":
        scale_xyz = (0.75 + 0.08 * jitter.x, 0.70 + 0.08 * jitter.y, 1.55 + 0.15 * jitter.z)
        return scale_xyz, 0.32, 0.55
    raise ValueError(f"unknown archetype: {archetype}")


def displace(bm, seed, amplitude):
    """2-3 octaves of deterministic noise, pushed out/in along each vertex's own
    radius. noise.noise() has no seed of its own, so the per-variant seed instead
    picks the sample-domain offset each octave is evaluated at."""
    noise.seed_set(seed)
    offsets = [noise.random_vector() * 4.0 for _ in range(DISPLACE_OCTAVES)]
    lo, hi = DISPLACE_CLAMP
    for v in bm.verts:
        base = v.co.copy()
        total = 0.0
        freq = DISPLACE_BASE_FREQ
        amp = amplitude
        for oct_i in range(DISPLACE_OCTAVES):
            total += noise.noise(base * freq + offsets[oct_i]) * amp
            freq *= DISPLACE_LACUNARITY
            amp *= DISPLACE_PERSISTENCE
        mult = max(lo, min(hi, 1.0 + total))  # no spikes, no inverted geometry
        v.co = v.co * mult


def flatten_bottom(bm):
    """Rocks sit IN regolith, not on it: compress the lower third of the current
    bbox toward its own threshold plane so the base reads roughly flat without a
    hard boolean cut."""
    zs = [v.co.z for v in bm.verts]
    mn_z, mx_z = min(zs), max(zs)
    thresh = mn_z + (mx_z - mn_z) * FLATTEN_FRACTION
    for v in bm.verts:
        if v.co.z < thresh:
            d = thresh - v.co.z
            v.co.z = thresh - d * (1.0 - FLATTEN_AMOUNT)


def taper_top(bm, amount):
    """Shard only: pull the upper third's XY inward so it reads as an angular
    pillar. `amount` is a floor on the shrink factor, so the top stays blunt
    rather than closing to a spike."""
    zs = [v.co.z for v in bm.verts]
    mn_z, mx_z = min(zs), max(zs)
    thresh = mx_z - (mx_z - mn_z) * TAPER_FRACTION
    span = max(mx_z - thresh, 1e-6)
    for v in bm.verts:
        if v.co.z > thresh:
            t = (v.co.z - thresh) / span
            k = 1.0 - t * (1.0 - amount)
            v.co.x *= k
            v.co.y *= k


def scale_axes(bm, scale_xyz):
    sx, sy, sz = scale_xyz
    for v in bm.verts:
        v.co.x *= sx
        v.co.y *= sy
        v.co.z *= sz


def bbox_of(bm):
    xs = [v.co.x for v in bm.verts]
    ys = [v.co.y for v in bm.verts]
    zs = [v.co.z for v in bm.verts]
    return Vector((min(xs), min(ys), min(zs))), Vector((max(xs), max(ys), max(zs)))


def resize_to_target(bm, target_dim_m):
    """Uniform scale so the largest bbox axis equals target_dim_m — applied after
    the archetype's per-axis proportions, so it sets overall size, not shape."""
    mn, mx = bbox_of(bm)
    dims = mx - mn
    current_max = max(dims.x, dims.y, dims.z)
    factor = target_dim_m / current_max
    for v in bm.verts:
        v.co = v.co * factor


def ground_center(bm):
    """Origin at the centre-bottom of the rock: XY centred, base at Z=0, +Z up."""
    mn, mx = bbox_of(bm)
    centre = Vector(((mn.x + mx.x) / 2.0, (mn.y + mx.y) / 2.0, mn.z))
    for v in bm.verts:
        v.co -= centre


def decimate_to(ob, target_tris):
    ob.data.calc_loop_triangles()
    before = len(ob.data.loop_triangles)
    if before > target_tris:
        mod = ob.modifiers.new("dec", "DECIMATE")
        mod.decimate_type = "COLLAPSE"
        mod.ratio = target_tris / float(before)
        bpy.context.view_layer.objects.active = ob
        bpy.ops.object.modifier_apply(modifier=mod.name)
    ob.data.calc_loop_triangles()
    return before, len(ob.data.loop_triangles)


def shade_smooth_sharp(ob, angle_deg=SHADE_ANGLE_DEG):
    """Smoothing set directly on polygons, sharp edges marked by face angle. Never
    `shade_smooth_by_angle` — see the module docstring."""
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
    return sharp


def smart_uv(ob, angle_deg=UV_ANGLE_DEG, margin=UV_MARGIN):
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
        raise SystemExit("[rocks] FATAL: smart_project signature not understood")


def make_rock_material():
    mat = bpy.data.materials.new("rock")
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get("Principled BSDF")
    if bsdf is not None:
        bsdf.inputs["Base Color"].default_value = (0.16, 0.13, 0.12, 1.0)
        bsdf.inputs["Roughness"].default_value = 0.92
    return mat


def export_glb(ob, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    bpy.ops.object.select_all(action="DESELECT")
    ob.select_set(True)
    bpy.context.view_layer.objects.active = ob
    kwargs = dict(filepath=path, export_format="GLB", use_selection=True)
    for extra in ({"export_apply": True}, {}):
        try:
            bpy.ops.export_scene.gltf(**kwargs, **extra)
            return
        except TypeError:
            continue
    raise SystemExit("[rocks] FATAL: glTF export signature not understood")


def make_rock(spec, out_dir, material):
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=ICOSPHERE_SUBDIV, radius=1.0,
                                           calc_uvs=False, location=(0.0, 0.0, 0.0))
    ob = bpy.context.object
    ob.name = spec["name"]
    ob.data.name = spec["name"]

    noise.seed_set(spec["seed"])
    jitter = noise.random_vector()
    scale_xyz, amplitude, taper = archetype_shape(spec["archetype"], jitter)

    bm = bmesh.new()
    bm.from_mesh(ob.data)
    displace(bm, spec["seed"], amplitude)
    scale_axes(bm, scale_xyz)
    # Flatten/taper run AFTER the archetype's proportions are set, not before: doing
    # it before let the bottom-third compression eat into the shard's Z-elongation
    # (the flatten shrank the sphere's Z range, then the 1.55x Z-scale amplified
    # that shrunk range too, damping the intended elongation) instead of just
    # trimming a base/tip off the final silhouette.
    flatten_bottom(bm)
    if taper is not None:
        taper_top(bm, taper)
    resize_to_target(bm, spec["target_dim_cm"] / 100.0)
    bm.to_mesh(ob.data)
    bm.free()
    ob.data.update()

    _, tris = decimate_to(ob, spec["target_tris"])

    # Recentre AFTER decimate, not before: the DECIMATE collapse can nudge the
    # lowest vertex by a fraction of a millimetre, and centring first left the
    # base measurably (up to ~6mm on the 150cm rock) off Z=0. Recentring is the
    # last geometric step so nothing after it can move the base again.
    bm2 = bmesh.new()
    bm2.from_mesh(ob.data)
    ground_center(bm2)
    bm2.to_mesh(ob.data)
    bm2.free()
    ob.data.update()

    shade_smooth_sharp(ob)

    ob.data.materials.append(material)
    smart_uv(ob)

    out_path = os.path.join(out_dir, spec["name"] + ".glb")
    export_glb(ob, out_path)

    xs = [v.co.x for v in ob.data.vertices]
    ys = [v.co.y for v in ob.data.vertices]
    zs = [v.co.z for v in ob.data.vertices]
    dims_m = (max(xs) - min(xs), max(ys) - min(ys), max(zs) - min(zs))
    return ob, dims_m, tris, out_path


def render_preview(rocks, out_path, px_x, px_y):
    """All rocks laid out in a row, neutral grey background, one sun."""
    gap = 0.30
    cursor = 0.0
    for ob, dims_m, _, _ in rocks:
        half_w = dims_m[0] / 2.0
        cursor += half_w
        ob.location.x = cursor
        cursor += half_w + gap
    total_width = cursor - gap
    shift = total_width / 2.0
    for ob, _, _, _ in rocks:
        ob.location.x -= shift
    max_height = max(d[2] for _, d, _, _ in rocks)

    world = bpy.data.worlds.new("w")
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs[0].default_value = (0.5, 0.5, 0.5, 1.0)
    world.node_tree.nodes["Background"].inputs[1].default_value = 0.7
    bpy.context.scene.world = world

    sun_data = bpy.data.lights.new("sun", type="SUN")
    sun_data.energy = 3.2
    sun = bpy.data.objects.new("sun", sun_data)
    bpy.context.scene.collection.objects.link(sun)
    sun.rotation_euler = (math.radians(55), math.radians(15), math.radians(50))

    focus_h = max_height * 0.5
    cam_data = bpy.data.cameras.new("cam")
    cam_data.type = "ORTHO"
    cam_data.ortho_scale = total_width * 1.12
    cam = bpy.data.objects.new("cam", cam_data)
    bpy.context.scene.collection.objects.link(cam)
    cam.location = Vector((0.0, -8.0, focus_h))
    direction = Vector((0.0, 0.0, focus_h)) - cam.location
    cam.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()
    bpy.context.scene.camera = cam

    scene = bpy.context.scene
    engines = [i.identifier for i in scene.render.bl_rna.properties["engine"].enum_items]
    scene.render.engine = "BLENDER_EEVEE_NEXT" if "BLENDER_EEVEE_NEXT" in engines else "BLENDER_EEVEE"
    scene.render.resolution_x = px_x
    scene.render.resolution_y = px_y
    scene.render.film_transparent = False
    scene.render.filepath = out_path
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    bpy.ops.render.render(write_still=True)
    log(f"preview -> {out_path}")


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", default="Martians/gen/rocks")
    ap.add_argument("--count", type=int, default=5)
    ap.add_argument("--preview", default=None,
                     help="optional path to render a side-by-side PNG of all generated rocks")
    ap.add_argument("--preview-px", default="1200x500")
    a = ap.parse_args(argv)

    repo_root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    out_dir = a.out_dir if os.path.isabs(a.out_dir) else os.path.join(repo_root, a.out_dir)

    bpy.ops.wm.read_factory_settings(use_empty=True)

    # One shared material datablock for every rock: bpy.data.materials.new("rock")
    # called once per rock in the same session would auto-suffix every name after
    # the first ("rock.001", "rock.002", ...) since Blender enforces unique
    # datablock names session-wide, breaking the "material slot named rock"
    # requirement for every rock but the first. Sharing one datablock across
    # objects is also just the correct modelling choice here, since all five want
    # the same untextured basalt look.
    material = make_rock_material()

    rocks = []
    for spec in build_specs(a.count):
        ob, dims_m, tris, out_path = make_rock(spec, out_dir, material)
        rocks.append((ob, dims_m, tris, out_path))
        dims_cm = tuple(d * 100.0 for d in dims_m)
        log(f"{spec['name']}: {tris} tris, {dims_cm[0]:.1f} x {dims_cm[1]:.1f} x {dims_cm[2]:.1f} cm "
            f"({spec['archetype']}, seed {spec['seed']}) -> {out_path}")

    if a.preview:
        preview_path = a.preview if os.path.isabs(a.preview) else os.path.join(repo_root, a.preview)
        px_x, px_y = (int(x) for x in a.preview_px.lower().split("x"))
        render_preview(rocks, preview_path, px_x, px_y)


if __name__ == "__main__":
    main()
