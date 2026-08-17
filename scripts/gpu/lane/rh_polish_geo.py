# Geometry-only Red Hope mesh polish (no render — the Cycles GPU render hung
# on headless device init, and the polished GLB is the actual deliverable).
# Weld exact-duplicate verts + auto-smooth by 30deg + export <name>_polished.glb.
# UVs/textures/materials preserved. One JSON line per file to stdout (POLISH).
import bpy, sys, os, json, math

argv = sys.argv[sys.argv.index("--") + 1:]
glb_path, out_dir = argv[0], argv[1]
name = os.path.basename(glb_path).replace("_game.glb", "").replace(".glb", "")
os.makedirs(os.path.join(out_dir, "polished"), exist_ok=True)

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=glb_path)
meshes = [o for o in bpy.data.objects if o.type == 'MESH']

def counts():
    v = sum(len(o.data.vertices) for o in meshes)
    t = 0
    for o in meshes:
        o.data.calc_loop_triangles()
        t += len(o.data.loop_triangles)
    return v, t

v0, t0 = counts()

for o in meshes:
    o.select_set(True)
    bpy.context.view_layer.objects.active = o
bpy.ops.object.mode_set(mode='EDIT')
bpy.ops.mesh.select_all(action='SELECT')
bpy.ops.mesh.remove_doubles(threshold=0.000001)   # exact split-face duplicates only
bpy.ops.mesh.normals_make_consistent(inside=False)
bpy.ops.object.mode_set(mode='OBJECT')
# Smooth by setting polygon.use_smooth DIRECTLY. Blender 4.2's shade_auto_smooth
# operator adds a Smooth-by-Angle *modifier* whose normals are NOT baked by the
# glTF exporter (measured: exported normals stayed flat) — the direct flag IS
# exported. These are organic image-to-3D reconstructions, so full smoothing is
# the right look (a hard-surface model would want a sharp-edge pass instead).
smoothed = "use_smooth_all"
for o in meshes:
    o.data.polygons.foreach_set("use_smooth", [True] * len(o.data.polygons))
    o.data.update()

v1, t1 = counts()
out_glb = os.path.join(out_dir, "polished", f"{name}_polished.glb")
bpy.ops.export_scene.gltf(filepath=out_glb, export_format='GLB', export_normals=True)

print("POLISH " + json.dumps({
    "file": os.path.basename(glb_path),
    "verts_before": v0, "verts_after": v1, "weld_pct": round(100.0 * (v0 - v1) / max(v0, 1), 1),
    "tris_before": t0, "tris_after": t1, "smooth": smoothed,
    "out": os.path.basename(out_glb),
}))
