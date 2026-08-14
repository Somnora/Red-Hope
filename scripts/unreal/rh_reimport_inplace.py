"""Re-import meshes IN PLACE, preserving their existing /Game package path.

    RH_MANIFEST=<pairs.json> RH_REPORT=<out.txt> \
    UnrealEditor-Cmd <proj> -run=pythonscript \
      -script=$PWD/scripts/unreal/rh_reimport_inplace.py -unattended -nosound -stdout

Manifest is [{"asset": "/Game/...", "source": "/abs/path.glb"}, ...].

Why not the ImportAssets commandlet: its -dest is fed through Interchange's
SubPath machinery (bSceneNameSubFolder + bAssetTypeSubFolders, both true in the
glTF pipeline), which is what produces the <n>/<n>/StaticMeshes/<n> layout. The
older FBX-era assets are FLAT, so a -dest import would create a NEW asset beside
the real one and silently orphan the path the game references.

Setting ImportAssetParameters.reimport_asset bypasses that entirely: Interchange
writes to the existing asset's own package, verbatim, for flat and double-folder
alike. It also works on the legacy FBX assets, which have no Interchange import
data, because a GLB carrying exactly one static-mesh node takes the
single-factory-node short-circuit.

Preconditions on the source GLB: exactly ONE mesh node (rh_finish.py joins), and
stable internal mesh/image names.

Nanite is disabled inline, because the import pipeline re-enables it every time
(InterchangeGenericMeshPipeline: bBuildNanite = true) and the project renders
with Nanite off, which would otherwise leave the mesh drawing its fallback proxy.

AFTER this, re-run `-run=RHArtWire -wire`: an import also resets the mesh's
material slot back to the auto-generated material.
"""
import json
import os

import unreal

MANIFEST = os.environ["RH_MANIFEST"]
OUT = os.environ.get("RH_REPORT", "/tmp/rh_reimport.txt")

pairs = json.load(open(MANIFEST))
mgr = unreal.InterchangeManager.get_interchange_manager_scripted()

log = []
ok_n = 0
for entry in pairs:
    obj_path = entry["asset"]
    glb = entry["source"]
    name = obj_path.rsplit("/", 1)[-1]
    if not os.path.exists(glb):
        log.append("NOSRC  %s (%s)" % (name, glb))
        continue
    mesh = unreal.load_asset(obj_path)
    if not mesh:
        log.append("NOASSET %s" % obj_path)
        continue

    is_mesh = isinstance(mesh, unreal.StaticMesh)
    before_nanite = (mesh.get_editor_property("nanite_settings").get_editor_property("enabled")
                     if is_mesh else False)
    try:
        src = unreal.InterchangeManager.create_source_data(glb)
        p = unreal.ImportAssetParameters()
        p.set_editor_property("reimport_asset", mesh)
        p.set_editor_property("is_automated", True)
        p.set_editor_property("replace_existing", True)
        try:
            p.set_editor_property("follow_redirectors", False)
        except Exception:
            pass
        res = mgr.import_asset(unreal.Paths.get_path(obj_path), src, p)
        ok = bool(res[0]) if isinstance(res, (tuple, list)) else bool(res)
    except Exception as e:
        log.append("THREW  %s : %s" % (name, e))
        continue

    # the mesh pipeline turns Nanite back on every import (textures have no such
    # setting, hence the type check)
    after_nanite = False
    if is_mesh:
        ns = mesh.get_editor_property("nanite_settings")
        after_nanite = ns.get_editor_property("enabled")
        if after_nanite:
            ns.set_editor_property("enabled", False)
            mesh.set_editor_property("nanite_settings", ns)
    unreal.EditorAssetLibrary.save_loaded_asset(mesh, only_if_is_dirty=False)

    ok_n += 1 if ok else 0
    note = ("nanite %s->off" % ("RE-ENABLED" if after_nanite else "off")) if is_mesh else "texture"
    log.append("%s %-22s %-18s %s" % ("OK  " if ok else "FAIL", name, note, obj_path))

log.insert(0, "in-place reimport: %d/%d ok" % (ok_n, len(pairs)))
with open(OUT, "w") as fh:
    fh.write("\n".join(log) + "\n")
