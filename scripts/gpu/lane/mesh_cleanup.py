"""Blender headless mesh cleanup: dense image-to-3D GLB -> game-ready static GLB.
Run:  blender --background --python mesh_cleanup.py -- <in.glb> <out.glb> [target_tris]

Steps: import GLB, join meshes, weld/merge-by-distance, planar+collapse decimate to a
tri budget, recompute normals, recenter to origin (feet on Z=0), export GLB. Vertex
colors from image-to-3D (COLOR_0) are preserved for the M_VertexColor material path.
Rigging is intentionally out of scope (static-first); a rig-transfer hook lives at the end."""
import bpy, sys, os

argv = sys.argv[sys.argv.index("--") + 1:]
in_glb, out_glb = argv[0], argv[1]
target_tris = int(argv[2]) if len(argv) > 2 else 15000
strip_base  = len(argv) > 3 and str(argv[3]) not in ("0", "", "false", "False")

def log(m): print(f"[cleanup] {m}", flush=True)

# --- clean scene ---
bpy.ops.wm.read_factory_settings(use_empty=True)

# --- import ---
bpy.ops.import_scene.gltf(filepath=in_glb)
meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
if not meshes:
    log("ERROR: no mesh in GLB"); sys.exit(1)

# join all mesh objects into one
bpy.ops.object.select_all(action="DESELECT")
for o in meshes: o.select_set(True)
bpy.context.view_layer.objects.active = meshes[0]
if len(meshes) > 1: bpy.ops.object.join()
obj = bpy.context.view_layer.objects.active

# glTF import leaves a Y-up->Z-up rotation on the object; bake it in so all
# downstream vertex math (plate strip, recenter) is in true world axes.
bpy.ops.object.select_all(action="DESELECT")
obj.select_set(True)
bpy.context.view_layer.objects.active = obj
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)

def tri_count(o):
    return sum(len(p.vertices) - 2 for p in o.data.polygons)

log(f"imported: {len(obj.data.vertices)} verts, ~{tri_count(obj)} tris, "
    f"vcols={len(obj.data.color_attributes)}")

# --- weld duplicate verts (marching-cubes output is often unwelded) ---
bpy.ops.object.mode_set(mode="EDIT")
bpy.ops.mesh.select_all(action="SELECT")
bpy.ops.mesh.remove_doubles(threshold=0.0001)
bpy.ops.mesh.normals_make_consistent(inside=False)
bpy.ops.object.mode_set(mode="OBJECT")
log(f"after weld: ~{tri_count(obj)} tris")

def strip_bottom_plate(o):
    """Remove a thin flat shadow DISC baked under the object (a wide bottom band,
    a NECK where the footprint pinches in, then the body widening again). Opt-in:
    only runs with strip_base, and only cuts when all three guards hold, so it can
    never eat splayed legs (discrete narrow feet, no filled wide disc) or a
    legitimately wide base (crate: no neck)."""
    import bmesh
    me = o.data
    zs = [v.co.z for v in me.vertices]
    zmin, zmax = min(zs), max(zs); H = zmax - zmin
    if H <= 1e-6:
        log("strip: flat mesh, skip"); return
    NB = 24
    def band_area(b):
        lo = zmin + H*b/NB; hi = zmin + H*(b+1)/NB
        xs = [v.co.x for v in me.vertices if lo <= v.co.z < hi]
        ys = [v.co.y for v in me.vertices if lo <= v.co.z < hi]
        if len(xs) < 8: return 0.0, 0
        return (max(xs)-min(xs))*(max(ys)-min(ys)), len(xs)
    areas = [band_area(b) for b in range(NB)]
    a0, n0 = areas[0]
    amax = max(a for a,_ in areas) or 1.0
    # neck = first band in the bottom 30% whose area drops below 0.6*a0
    neck = None
    for b in range(1, max(2, int(NB*0.3))):
        if areas[b][0] < 0.6 * a0:
            neck = b; break
    if neck is None:
        log("strip: no neck under a wide bottom -> no plate, skip"); return
    # guards: bottom disc must be WIDE (a real plate, not a foot) and the body
    # ABOVE the neck must RECOVER wider than the disc (plate-neck-body signature).
    recover = max((areas[b][0] for b in range(neck, NB)), default=0.0)
    if a0 < 0.35 * amax:
        log(f"strip: bottom band too small (a0={a0:.3f} < .35*amax) -> not a plate, skip"); return
    if recover < 1.05 * a0:
        log(f"strip: body never widens past the disc -> likely legs, skip"); return
    cut = zmin + H*neck/NB
    bm = bmesh.new(); bm.from_mesh(me)
    geom = [g for g in list(bm.verts)+list(bm.edges)+list(bm.faces)]
    res = bmesh.ops.bisect_plane(bm, geom=geom, dist=1e-5,
            plane_co=(0,0,cut), plane_no=(0,0,1), clear_inner=True)
    # cap the opening so the base reads solid from a low angle
    holes = [e for e in bm.edges if len(e.link_faces) == 1]
    if holes:
        try: bmesh.ops.holes_fill(bm, edges=holes, sides=0)
        except Exception: pass
    bm.to_mesh(me); bm.free()
    me.update()
    log(f"strip: removed plate below z={cut-zmin:.3f} (neck band {neck}, "
        f"a0={a0:.2f} recover={recover:.2f}); now ~{tri_count(o)} tris")

if strip_base:
    strip_bottom_plate(obj)


# --- decimate to tri budget (collapse keeps vertex colors) ---
cur = tri_count(obj)
if cur > target_tris:
    m = obj.modifiers.new("dec", "DECIMATE")
    m.decimate_type = "COLLAPSE"
    m.ratio = max(0.005, min(1.0, target_tris / cur))
    m.use_collapse_triangulate = True
    bpy.ops.object.modifier_apply(modifier=m.name)
    log(f"decimated to ~{tri_count(obj)} tris (ratio {m.ratio:.4f})")
else:
    log("already under budget, no decimation")

# --- recenter: XY to bounding-box center, Z so the base sits on 0 (feet on ground) ---
bpy.context.view_layer.update()
coords = [obj.matrix_world @ v.co for v in obj.data.vertices]
xs = [c.x for c in coords]; ys = [c.y for c in coords]; zs = [c.z for c in coords]
import mathutils
shift = mathutils.Vector((-(min(xs)+max(xs))/2.0, -(min(ys)+max(ys))/2.0, -min(zs)))
for v in obj.data.vertices: v.co += shift
bpy.context.view_layer.update()
log(f"recentered; new Z range [0, {max(zs)-min(zs):.4f}]")

# --- rig-transfer hook (later animation phase; no-op for static-first) ---
# TODO(anim-phase): apply auto-rig / rig-transfer here before export as FBX.

# --- export GLB (keep vertex colors, +Y up to match UE import convention used for forge) ---
bpy.ops.object.select_all(action="DESELECT")
obj.select_set(True)
bpy.context.view_layer.objects.active = obj
bpy.ops.export_scene.gltf(
    filepath=out_glb, export_format="GLB", use_selection=True,
    export_yup=True, export_normals=True, export_vertex_color="ACTIVE",
    export_texcoords=True, export_materials="EXPORT")
log(f"exported -> {out_glb} ({os.path.getsize(out_glb)//1024} KB, "
    f"final ~{tri_count(obj)} tris)")
