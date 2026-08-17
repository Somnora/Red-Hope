"""Give every skeletal-mesh material the SkeletalMesh usage flag.

    RH_REPORT=/tmp/skel.txt UnrealEditor-Cmd <proj> -run=pythonscript \
      -script=$PWD/scripts/unreal/rh_fix_skeletal_usage.py -unattended -nosound -stdout

THE BUG THIS FIXES. A UMaterial carries per-domain usage flags. If a material is
applied to a skeletal mesh without bUsedWithSkeletalMesh, UE refuses it at render
time and substitutes the DEFAULT material - the mesh keeps its textures, the
material keeps its bindings, and none of it reaches the screen. The mesh renders
as a plain grey/white body.

Every crew walker and every robot in this project was in that state: their
materials descend from M_GLTF (the Interchange glTF base) with the flag off, and
the log says so 15 times a boot - "Material with missing usage flag was applied
to skeletal mesh". That is the real reason the characters read as unfinished grey
statues; it was never a missing paint stage.

Set with pass_flags so the material is compiled with the usage rather than just
marked, then saved.
"""
import os

import unreal

MEL = unreal.MaterialEditingLibrary
OUT = os.environ.get("RH_REPORT", "/tmp/rh_skeletal.txt")

reg = unreal.AssetRegistryHelpers.get_asset_registry()
assets = reg.get_assets_by_path("/Game/RedHope/Art", recursive=True)

log = []
bases = {}
skel_count = 0

for a in assets:
    cls = str(a.asset_class_path.asset_name) if hasattr(a, "asset_class_path") else str(a.asset_class)
    if cls != "SkeletalMesh":
        continue
    skel_count += 1
    sk = unreal.load_asset(str(a.package_name))
    if not sk:
        continue
    for m in sk.get_editor_property("materials"):
        mi = m.get_editor_property("material_interface")
        if not mi:
            continue
        base = mi.get_base_material() if hasattr(mi, "get_base_material") else mi
        if base:
            bases.setdefault(base.get_path_name(), base)

log.append("skeletal meshes scanned: %d" % skel_count)
log.append("distinct base materials on them: %d" % len(bases))

fixed = 0
for path, base in sorted(bases.items()):
    try:
        had = MEL.has_material_usage(base, unreal.MaterialUsage.MATUSAGE_SKELETAL_MESH)
    except Exception as e:
        log.append("  ? %s : %s" % (path, e))
        continue
    if had:
        log.append("  ok      %s (already flagged)" % path)
        continue
    try:
        MEL.set_base_material_usage(base, unreal.MaterialUsage.MATUSAGE_SKELETAL_MESH)
        MEL.recompile_material(base)
        unreal.EditorAssetLibrary.save_loaded_asset(base, only_if_is_dirty=False)
        now = MEL.has_material_usage(base, unreal.MaterialUsage.MATUSAGE_SKELETAL_MESH)
        fixed += 1 if now else 0
        log.append("  FIXED   %s -> skeletal_usage=%s" % (path, now))
    except Exception as e:
        log.append("  FAILED  %s : %s" % (path, e))

log.insert(0, "skeletal usage flag: fixed %d base material(s)" % fixed)
with open(OUT, "w") as fh:
    fh.write("\n".join(log) + "\n")
