"""Disable Nanite on every Red Hope art static mesh.

The project ships r.Nanite.ProjectEnabled=False, but 109 of 132 art meshes were
imported with Nanite ENABLED on the asset. With Nanite off at the project level
those meshes render their reduced *fallback* proxy instead of full geometry -
e.g. HabitatDome drawing 3,962 of its 18,000 triangles. Turning the asset flag
off makes UE build normal full-detail render data again.

Reports triangle counts before and after so the gain is evidence, not belief.
"""
import unreal

import os
OUT = os.environ.get("RH_REPORT", "/tmp/rh_nanite_fix.txt")

try:
    SUB = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
except Exception:
    SUB = None


def tris(m):
    for fn in ("get_number_triangles",):
        if SUB is not None and hasattr(SUB, fn):
            try:
                return getattr(SUB, fn)(m, 0)
            except Exception:
                pass
    try:
        return unreal.EditorStaticMeshLibrary.get_number_triangles(m, 0)
    except Exception:
        return -1


reg = unreal.AssetRegistryHelpers.get_asset_registry()
assets = reg.get_assets_by_path("/Game/RedHope/Art", recursive=True)

lines = []
changed = 0
failed = 0
for a in assets:
    cls = str(a.asset_class_path.asset_name) if hasattr(a, "asset_class_path") else str(a.asset_class)
    if cls != "StaticMesh":
        continue
    path = str(a.package_name)
    m = unreal.load_asset(path)
    if not m:
        continue
    try:
        ns = m.get_editor_property("nanite_settings")
        if not ns.get_editor_property("enabled"):
            continue
        before = tris(m)
        ns.set_editor_property("enabled", False)
        m.set_editor_property("nanite_settings", ns)
        m.modify()
        unreal.EditorAssetLibrary.save_loaded_asset(m, only_if_is_dirty=False)
        after = tris(m)
        changed += 1
        lines.append("FIXED  %7d -> %7d tris  %s" % (before, after, path))
    except Exception as e:
        failed += 1
        lines.append("FAILED %s : %s" % (path, e))

lines.sort()
with open(OUT, "w") as fh:
    fh.write("nanite disabled on %d meshes (%d failures)\n" % (changed, failed))
    fh.write("\n".join(lines) + "\n")
