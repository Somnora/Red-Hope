"""Cut the baked-in floor plate off a prop GLB. Round-trip safe.

  Blender -b -P scripts/blender/rh_cut_plate.py -- --in a.glb --out b.glb --report r.txt

The generated props carry a thin ground slab under their feet (the generator
meshes the ground in; the buildings had the same disease and were regenerated
plinth-free, the props never were - and the 2026-08-14 weld pass reimported
from the WITH-plate sources, which is why plates the July pass had hidden came
back). On the director's screen the slab reads as "you cannot see the floor
underneath the desks".

Detection is structural, not a blind plane cut: the plate is the connected
component whose bounding box is very flat (height <= 12% of the mesh) AND spans
most of the footprint (>= 55% of XY area) AND sits at the very bottom. A blind
z-cut would take chair legs and planter bases with it; a component that fails
the tests is left alone, and the report says NO_PLATE so the caller knows this
prop still needs eyes.

Mesh/image names are left untouched: the reimport lane requires them stable.
"""
import argparse
import sys

import bpy
import bmesh

argv = sys.argv[sys.argv.index("--") + 1:]
ap = argparse.ArgumentParser()
ap.add_argument("--in", dest="src", required=True)
ap.add_argument("--out", dest="dst", required=True)
ap.add_argument("--report", required=True)
args = ap.parse_args(argv)

log = []

bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=args.src)
meshes = [o for o in bpy.data.objects if o.type == "MESH"]
if len(meshes) != 1:
    log.append("ERROR expected 1 mesh, found %d" % len(meshes))
    with open(args.report, "w") as f:
        f.write("\n".join(log) + "\n")
    sys.exit(0)
ob = meshes[0]

bm = bmesh.new()
bm.from_mesh(ob.data)
bm.verts.ensure_lookup_table()
bm.faces.ensure_lookup_table()

zs = [v.co.z for v in bm.verts]
zmin, zmax = min(zs), max(zs)
H = max(zmax - zmin, 1e-6)
xs = [v.co.x for v in bm.verts]
ys = [v.co.y for v in bm.verts]
foot_area = max((max(xs) - min(xs)) * (max(ys) - min(ys)), 1e-9)

# Connected components by face adjacency.
unvisited = set(bm.faces)
components = []
while unvisited:
    seed = unvisited.pop()
    comp = {seed}
    frontier = [seed]
    while frontier:
        face = frontier.pop()
        for edge in face.edges:
            for nb in edge.link_faces:
                if nb in unvisited:
                    unvisited.discard(nb)
                    comp.add(nb)
                    frontier.append(nb)
    components.append(comp)

log.append("components: %d  H=%.4f" % (len(components), H))
cut = 0
for comp in components:
    vs = {v for f in comp for v in f.verts}
    czmin = min(v.co.z for v in vs); czmax = max(v.co.z for v in vs)
    cxmin = min(v.co.x for v in vs); cxmax = max(v.co.x for v in vs)
    cymin = min(v.co.y for v in vs); cymax = max(v.co.y for v in vs)
    ch = czmax - czmin
    carea = (cxmax - cxmin) * (cymax - cymin)
    # Two profiles: the standard plate (flat + spans most of the footprint),
    # and the strict-flat partial plate (a slab so thin it cannot be furniture,
    # even if it only underlies part of the prop - the bunk's bed-frame plate).
    flat = ch <= H * 0.12
    wide = carea >= foot_area * 0.55
    razor = ch <= H * 0.04 and carea >= foot_area * 0.20
    low = czmin <= zmin + H * 0.02
    log.append("  comp %4d faces  h %5.1f%%  area %5.1f%%  low=%s -> %s" % (
        len(comp), 100 * ch / H, 100 * carea / foot_area, low,
        "CUT" if ((flat and wide) or razor) and low else "keep"))
    if ((flat and wide) or razor) and low:
        bmesh.ops.delete(bm, geom=list(comp), context="FACES")
        cut += 1

if cut == 0:
    log.append("NO_PLATE cut nothing - this prop needs eyes")
else:
    # Drop verts orphaned by the deletion.
    orphans = [v for v in bm.verts if not v.link_faces]
    if orphans:
        bmesh.ops.delete(bm, geom=orphans, context="VERTS")
    log.append("CUT %d component(s), %d verts %d faces remain" % (cut, len(bm.verts), len(bm.faces)))

bm.to_mesh(ob.data)
bm.free()

bpy.ops.export_scene.gltf(filepath=args.dst, export_format="GLB", use_selection=False)
with open(args.report, "w") as f:
    f.write("\n".join(log) + "\n")
