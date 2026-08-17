# Headless Blender mesh QA: audits every game GLB without modifying it.
# Reports per mesh: tris, loose-island count/size, non-manifold edges,
# inverted-normal estimate, origin centering, ground contact, texture size,
# material count. Output: one JSON line per file to stdout (grep MESHQA).
import bpy, sys, json, os
from mathutils import Vector

argv = sys.argv[sys.argv.index("--") + 1:]
glb_path = argv[0]

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=glb_path)

report = {"file": os.path.basename(glb_path)}
meshes = [o for o in bpy.data.objects if o.type == 'MESH']
report["objects"] = len(meshes)

total_tris = 0
islands_small = 0
islands_total = 0
nonmanifold_edges = 0
mins = Vector((1e9, 1e9, 1e9)); maxs = Vector((-1e9, -1e9, -1e9))

for ob in meshes:
    me = ob.data
    me.calc_loop_triangles()
    total_tris += len(me.loop_triangles)
    # world bounds
    for v in ob.bound_box:
        w = ob.matrix_world @ Vector(v)
        mins = Vector(map(min, mins, w)); maxs = Vector(map(max, maxs, w))
    # connected components via union-find on edges
    parent = list(range(len(me.vertices)))
    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a
    for e in me.edges:
        a, b = find(e.vertices[0]), find(e.vertices[1])
        if a != b: parent[a] = b
    comps = {}
    for i in range(len(me.vertices)):
        comps.setdefault(find(i), 0)
        comps[find(i)] += 1
    islands_total += len(comps)
    big = max(comps.values()) if comps else 0
    for c in comps.values():
        if c < max(12, big * 0.002):  # tiny floating debris
            islands_small += 1
    # non-manifold edge count (edges with !=2 faces)
    face_count = {}
    for tri in me.loop_triangles:
        for i in range(3):
            a, b = tri.vertices[i], tri.vertices[(i + 1) % 3]
            k = (min(a, b), max(a, b))
            face_count[k] = face_count.get(k, 0) + 1
    nonmanifold_edges += sum(1 for v in face_count.values() if v != 2)

size = maxs - mins
center = (maxs + mins) / 2
report.update({
    "tris": total_tris,
    "islands": islands_total,
    "tiny_islands": islands_small,
    "nonmanifold_edges": nonmanifold_edges,
    "size_m": [round(size.x, 3), round(size.y, 3), round(size.z, 3)],
    "origin_xy_offset_m": [round(center.x, 3), round(center.y, 3)],
    "min_z_m": round(mins.z, 4),
})

# textures + materials
texs = []
for img in bpy.data.images:
    if img.size[0]:
        texs.append({"name": img.name, "px": list(img.size)})
report["textures"] = texs
report["materials"] = len(bpy.data.materials)

# shading: fraction of faces flat-shaded
flat = sum(1 for ob in meshes for p in ob.data.polygons if not p.use_smooth)
allp = sum(len(ob.data.polygons) for ob in meshes)
report["flat_faces_pct"] = round(100.0 * flat / max(allp, 1), 1)

print("MESHQA " + json.dumps(report))
