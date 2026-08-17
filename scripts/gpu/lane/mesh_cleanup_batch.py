"""Headless Blender batch mesh cleanup: dense image-to-3D GLBs -> game-ready static GLBs.
Run as the bpy PIP MODULE (not `blender --python`):  python mesh_cleanup_batch.py <in_dir> <out_dir> [target_tris]

Per GLB: import, join, weld/merge-by-distance, collapse-decimate to a tri budget,
recompute normals, recenter (XY to center, base on Z=0), export GLB (keeps COLOR_0
vertex colors, +Y up to match the UE import convention). Per-mesh failures are logged
and skipped; the rest still process. Exits 2 if any mesh failed (good GLBs kept)."""
import bpy, sys, os, glob, mathutils, traceback

in_dir, out_dir = sys.argv[1], sys.argv[2]
target_tris = int(sys.argv[3]) if len(sys.argv) > 3 and sys.argv[3] not in ("", "0") else 15000

def log(m): print(f"[cleanup] {m}", flush=True)

def tri_count(o):
    return sum(len(p.vertices) - 2 for p in o.data.polygons)

def clean_one(in_glb, out_glb):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=in_glb)
    meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    if not meshes:
        raise RuntimeError("no mesh in GLB")
    bpy.ops.object.select_all(action="DESELECT")
    for o in meshes: o.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    if len(meshes) > 1: bpy.ops.object.join()
    obj = bpy.context.view_layer.objects.active
    v0, t0 = len(obj.data.vertices), tri_count(obj)

    # weld duplicate verts (marching-cubes output is unwelded) + consistent normals
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.mesh.remove_doubles(threshold=0.0001)
    bpy.ops.mesh.normals_make_consistent(inside=False)
    bpy.ops.object.mode_set(mode="OBJECT")

    # collapse-decimate to tri budget (collapse keeps vertex colors)
    cur = tri_count(obj)
    ratio = 1.0
    if cur > target_tris:
        m = obj.modifiers.new("dec", "DECIMATE")
        m.decimate_type = "COLLAPSE"
        m.ratio = max(0.005, min(1.0, target_tris / cur))
        m.use_collapse_triangulate = True
        ratio = m.ratio
        bpy.ops.object.modifier_apply(modifier=m.name)

    # recenter: XY to bbox center, Z so base sits on 0 (grounds correctly in UE)
    bpy.context.view_layer.update()
    coords = [obj.matrix_world @ v.co for v in obj.data.vertices]
    xs = [c.x for c in coords]; ys = [c.y for c in coords]; zs = [c.z for c in coords]
    shift = mathutils.Vector((-(min(xs)+max(xs))/2.0, -(min(ys)+max(ys))/2.0, -min(zs)))
    for v in obj.data.vertices: v.co += shift
    bpy.context.view_layer.update()

    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.export_scene.gltf(
        filepath=out_glb, export_format="GLB", use_selection=True,
        export_yup=True, export_normals=True, export_vertex_color="ACTIVE",
        export_texcoords=True, export_materials="EXPORT")
    return v0, t0, tri_count(obj), ratio, max(zs)-min(zs), os.path.getsize(out_glb)//1024

globs = sorted(glob.glob(os.path.join(in_dir, "*.glb")))
if not globs:
    sys.exit(f"[fatal] no *.glb in {in_dir}")
os.makedirs(out_dir, exist_ok=True)
log(f"{len(globs)} mesh(es); target_tris={target_tris}")

ok = 0; failed = []
for in_glb in globs:
    name = os.path.splitext(os.path.basename(in_glb))[0]
    out_glb = os.path.join(out_dir, f"{name}.glb")
    try:
        v0, t0, tf, ratio, zr, kb = clean_one(in_glb, out_glb)
        log(f"[receipt] {name}: {v0} verts / ~{t0} tris -> ~{tf} tris "
            f"(ratio {ratio:.4f}), Z[0,{zr:.3f}], {kb} KB -> {out_glb}")
        ok += 1
    except Exception as e:
        failed.append(name)
        log(f"[error] {name} FAILED: {e}")
        traceback.print_exc()

log(f"[done] {ok}/{len(globs)} cleaned -> {out_dir}" + (f"; FAILED: {failed}" if failed else ""))
# bpy-as-module segfaults during interpreter teardown AFTER work completes; os._exit
# skips that teardown so the real exit code (not SIGSEGV/139) reaches the dispatcher.
sys.stdout.flush(); sys.stderr.flush()
os._exit(2 if failed else 0)
