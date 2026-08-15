"""Finish a UE-exported Tiers FBX: the fragment-cloud repair. Round-trip safe.

  Blender -b -P scripts/blender/rh_finish_tiers.py -- \
      --in tiers/chemtable_lg.fbx --out out/chemtable_lg.glb --report r.txt

The Tiers furniture (workbench_lg, chemtable_*, infirmary, lab_full, workshop)
never went through the finishing lane. Measured state per mesh: 7,000 tris at
3.00 verts/tri (hard split normals) in ~1,300 disconnected components of which
~1,200 are TINY, with ~8,800 open boundary edges. On screen that reads exactly
as the director put it: "patches and holes".

Order is load-bearing:
  1. clear custom split normals FIRST - the 3.00 v/t is split normals, not
     duplicate positions; welding before clearing leaves the seams.
  2. weld (merge by distance)
  3. strip_loose with a LARGE min_faces (12): kills the fragment confetti.
     The default 4 was tuned for one stray triangle, not twelve hundred shards.
  4. fill_holes for small rings only - big openings are design, not damage
  5. smooth by angle (sets polygon.use_smooth directly; never shade_auto_smooth)
  6. export GLB with the mesh named after the UE asset, for the in-place
     reimport lane (which requires exactly one mesh and a stable name).

Source is the UE FBX EXPORT of the shipped asset (the original staging GLBs
are gone; AssetExportTask gets the geometry back out). UVs ride along, so the
existing 1024 textures keep mapping.
"""
import argparse
import os
import sys

import bpy

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rh_lib

argv = rh_lib.argv_after_ddash()
ap = argparse.ArgumentParser()
ap.add_argument("--in", dest="src", required=True)
ap.add_argument("--out", dest="dst", required=True)
ap.add_argument("--report", required=True)
ap.add_argument("--name", default=None, help="mesh name for the round-trip; default = out basename")
args = ap.parse_args(argv)

name = args.name or os.path.splitext(os.path.basename(args.dst))[0]
lines = []

rh_lib.reset_scene()
bpy.ops.import_scene.fbx(filepath=args.src)
meshes = [o for o in bpy.data.objects if o.type == "MESH"]
if len(meshes) != 1:
    ob = rh_lib.join_meshes(meshes)
else:
    ob = meshes[0]
ob.name = name
ob.data.name = name

v0, f0 = len(ob.data.vertices), len(ob.data.polygons)

# 1. the split normals ARE the 3.00 v/t; clear before any weld
ob.data.free_normals_split_custom() if hasattr(ob.data, "free_normals_split_custom") else None
try:
    bpy.context.view_layer.objects.active = ob
    bpy.ops.mesh.customdata_custom_splitnormals_clear()
except Exception as exc:
    lines.append("split-normal clear via op failed (%s) - free_* path used" % exc)

rh_lib.weld(ob)
v1, f1 = len(ob.data.vertices), len(ob.data.polygons)

# 3. the confetti: every fragment under 12 faces goes
rh_lib.strip_loose(ob, min_faces=12)
v2, f2 = len(ob.data.vertices), len(ob.data.polygons)

rh_lib.fill_holes(ob, max_sides=8)
rh_lib.smooth_by_angle(ob, 40.0)
rh_lib.ground_center_pivot(ob)
rh_lib.export_glb(ob, args.dst)

import bmesh
bm = bmesh.new(); bm.from_mesh(ob.data)
boundary = sum(1 for e in bm.edges if len(e.link_faces) == 1)
bm.free()

v3, f3 = len(ob.data.vertices), len(ob.data.polygons)
lines.append("%s: %d/%d -> weld %d/%d -> strip %d/%d -> final %d verts %d tris (v/t %.2f), boundaryE %d" % (
    name, v0, f0, v1, f1, v2, f2, v3, f3, v3 / max(f3, 1), boundary))
with open(args.report, "w") as fh:
    fh.write("\n".join(lines) + "\n")
