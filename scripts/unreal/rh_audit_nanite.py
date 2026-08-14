import unreal

import os
OUT = os.environ.get("RH_REPORT", "/tmp/rh_nanite_audit.txt")

reg = unreal.AssetRegistryHelpers.get_asset_registry()
assets = reg.get_assets_by_path("/Game/RedHope/Art", recursive=True)

rows = []
n_total = 0
n_nanite = 0
for a in assets:
    cls = str(a.asset_class_path.asset_name) if hasattr(a, "asset_class_path") else str(a.asset_class)
    if cls != "StaticMesh":
        continue
    n_total += 1
    path = str(a.package_name)
    m = unreal.load_asset(path)
    if not m:
        rows.append("LOADFAIL %s" % path)
        continue
    try:
        nan = m.get_editor_property("nanite_settings").get_editor_property("enabled")
    except Exception as e:
        nan = "err:%s" % e
    try:
        t0 = unreal.EditorStaticMeshLibrary.get_number_triangles(m, 0)
        v0 = unreal.EditorStaticMeshLibrary.get_number_verts(m, 0)
    except Exception:
        t0, v0 = -1, -1
    if nan is True:
        n_nanite += 1
    rows.append("%-6s LOD0=%6dt/%6dv  %s" % ("NANITE" if nan is True else "plain", t0, v0, path))

rows.sort()
header = "static meshes under /Game/RedHope/Art: %d total, %d with Nanite ENABLED" % (n_total, n_nanite)
with open(OUT, "w") as fh:
    fh.write(header + "\n" + "\n".join(rows) + "\n")
