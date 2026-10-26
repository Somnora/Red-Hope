"""Report triangle/vertex counts and material wiring for a set of static meshes.

  RH_ASSETS=/Game/a.a,/Game/b.b RH_REPORT=<out.txt> \
  UnrealEditor-Cmd <proj> -run=pythonscript \
      -script=$PWD/scripts/unreal/rh_mesh_stats.py -unattended -nosound -stdout

Run it BEFORE a reimport and again AFTER, so an asset swap is evidenced by
numbers from the thing that actually ships rather than from the source GLB.

The vertex-to-triangle ratio is the tell worth reading. A clean closed surface
sits near 0.5-1.2 v/t. A mesh reconstructed as hundreds of disconnected shells
cannot share vertices between them, so the ratio climbs toward 3.0 - which is
exactly what the pre-2026-08-16 Tiers furniture measured, and why it read as
shattered no matter what texture was on it.

Material slots are printed too, because an import RESETS the mesh's slot back to
the auto-generated material - so a wire pass has to follow, and this is how you
see whether it did.
"""
import os

import unreal

ASSETS = [a for a in os.environ["RH_ASSETS"].split(",") if a.strip()]
OUT = os.environ.get("RH_REPORT", "/tmp/rh_mesh_stats.txt")

log = []
for path in ASSETS:
    path = path.strip()
    mesh = unreal.load_asset(path)
    if not mesh:
        log.append("MISSING  %s" % path)
        continue

    tris = mesh.get_num_triangles(0)
    verts = mesh.get_num_vertices(0)
    lods = mesh.get_num_lods()
    ratio = (float(verts) / tris) if tris else 0.0
    bounds = mesh.get_bounds()
    ext = bounds.box_extent

    log.append("%-58s tris %6d  verts %6d  %.2f v/t  lods %d  extent %.0fx%.0fx%.0f cm"
               % (path.split(".")[-1], tris, verts, ratio, lods, ext.x * 2, ext.y * 2, ext.z * 2))

    if lods > 1:
        # (the tram, 2026-10-26: the first kit asset with LODs) each LOD's triangles and screen size
        sub = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem) or unreal.StaticMeshEditorSubsystem()
        sizes = sub.get_lod_screen_sizes(mesh)
        log.append("      " + ", ".join("LOD%d %d tris @ %.2f" % (i, mesh.get_num_triangles(i), sizes[i] if i < len(sizes) else -1)
                                        for i in range(lods)))
        # Each LOD's sections: the slot each one draws and its triangles, so a reduction that drops a small slot (the tram's
        # destination boards, which carry the line's wait colour) shows here and not only at distance in a capture.
        for i in range(lods):
            parts = []
            for s in range(mesh.get_num_sections(i)):
                slot = sub.get_lod_material_slot(mesh, i, s)
                try:
                    tri_idx = unreal.ProceduralMeshLibrary.get_section_from_static_mesh(mesh, i, s)[1]
                    parts.append("s%d slot %d %d tris" % (s, slot, len(tri_idx) // 3))
                except Exception as exc:  # the plugin's library missing: the slot alone still says whether the section survived
                    parts.append("s%d slot %d (tris n/a: %s)" % (s, slot, exc))
            log.append("      LOD%d sections: %s" % (i, "; ".join(parts) if parts else "none"))

    for i, sm in enumerate(mesh.get_editor_property("static_materials")):
        mi = sm.material_interface
        log.append("      slot %d: %s" % (i, mi.get_path_name() if mi else "<none>"))

log.append("")
log.append("read %d/%d" % (len([l for l in log if " v/t " in l]), len(ASSETS)))
with open(OUT, "w") as f:
    f.write("\n".join(log) + "\n")
print("\n".join(log))
