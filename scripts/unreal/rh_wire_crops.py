"""Wire the nine crop-stage meshes onto M_RH_Master. Compile-free.

  UnrealEditor-Cmd <proj> -run=pythonscript \
      -script=$PWD/scripts/unreal/rh_wire_crops.py -unattended -nosound -stdout

Same discipline as rh_wire_props.py / rh_wire_tiers.py, for the same reason.
Two traps are specific to the crops and both are load-bearing here:

1. THERE ARE TWO CROP LINEAGES. `Art/Agri/<n>/<n>/StaticMeshes/<n>` is the one
   the game loads (RHColonyVisualizerSubsystem builds that exact path from the
   room key `<Garden|Greenhouse>#<family>#<stage>`), and `Art/Garden/<n>/...` is
   a flat orphan referenced nowhere in source. Name-searching the asset registry
   for `crop_root_1` finds both and takes whichever comes first - which is
   precisely how the Props/Props2 false-done happened. Full paths only.

2. The Agri layout is DOUBLE-FOLDERED (`<n>/<n>/`), an Interchange SubPath
   artefact. Anything that assumes `<n>/StaticMeshes` silently addresses nothing.

RHArtWire's own table covers Agri/humidity but not the crops, so they were never
in the material family; before this they sat on the auto-generated glTF
`Material`, which is why they got none of the master's surface response.

Emissive is left at the parent's default 0 with a white mask: multiplied together
they stay dark. If the garden ever earns grow-light glow, cut masks and set the
three emissive parameters then - do not invent light here.
"""
import os

import unreal

MEL = unreal.MaterialEditingLibrary
OUT = os.environ.get("RH_REPORT", "/tmp/rh_wire_crops.txt")
MASTER = "/Game/RedHope/Art/M_RH_Master.M_RH_Master"

NAMES = [
    "crop_root_1", "crop_root_2", "crop_root_3",
    "crop_tall_1", "crop_tall_2", "crop_tall_3",
    "crop_vine_1", "crop_vine_2", "crop_vine_3",
]
# Skip list: a mesh whose generation failed should keep whatever it had rather
# than be dressed up in a correct material. Set RH_SKIP=crop_vine_3[,...].
SKIP = {s for s in os.environ.get("RH_SKIP", "").split(",") if s}

log = []
tools = unreal.AssetToolsHelpers.get_asset_tools()
master = unreal.load_asset(MASTER)
registry = unreal.AssetRegistryHelpers.get_asset_registry()
if not master:
    log.append("ABORT: %s missing" % MASTER)

ok_n = 0
for name in (NAMES if master else []):
    if name in SKIP:
        log.append("skip  %-14s (RH_SKIP)" % name)
        continue

    base_dir = "/Game/RedHope/Art/Agri/%s/%s" % (name, name)
    mesh = unreal.load_asset("%s/StaticMeshes/%s.%s" % (base_dir, name, name))
    if not mesh:
        log.append("MISSING mesh %s" % name)
        continue

    mi_pkg = "%s/Materials/MI_%s" % (base_dir, name)
    mi = unreal.load_asset("%s.MI_%s" % (mi_pkg, name))
    if not mi:
        mi = tools.create_asset("MI_%s" % name, "%s/Materials" % base_dir,
                                unreal.MaterialInstanceConstant,
                                unreal.MaterialInstanceConstantFactoryNew())
    if not mi:
        log.append("FAILED MI %s" % name)
        continue
    mi.set_editor_property("parent", master)

    albedo = unreal.load_asset("%s/Textures/%s_textured.%s_textured" % (base_dir, name, name))
    if albedo:
        MEL.set_material_instance_texture_parameter_value(mi, "BaseTex", albedo)

    # glTF packs metal/rough into one texture; Interchange names it
    # "<n>_textured_metallic-<n>_textured_roughness". Find it by scanning the
    # asset's OWN Textures folder - never a global name search.
    mr = None
    for a in registry.get_assets_by_path("%s/Textures" % base_dir, recursive=False):
        if "metallic" in str(a.asset_name).lower():
            mr = unreal.load_asset("%s.%s" % (str(a.package_name), str(a.asset_name)))
            break
    if mr:
        MEL.set_material_instance_texture_parameter_value(mi, "MRTex", mr)
        MEL.set_material_instance_scalar_parameter_value(mi, "UseMRTex", 1.0)
    else:
        MEL.set_material_instance_scalar_parameter_value(mi, "Rough", 0.80)

    unreal.EditorAssetLibrary.save_loaded_asset(mi)
    # get/set static_materials hands back COPIES; set_material is the real setter.
    mesh.set_material(0, mi)
    unreal.EditorAssetLibrary.save_loaded_asset(mesh)

    check = mesh.get_editor_property("static_materials")[0].material_interface
    got = check.get_name() if check else "<none>"
    good = got == "MI_%s" % name
    ok_n += 1 if good else 0
    log.append("%-5s %-14s slot0=%-16s albedo=%s MR=%s  tris=%d verts=%d" % (
        "ok" if good else "FAIL", name, got,
        "yes" if albedo else "NO", "yes" if mr else "no",
        mesh.get_num_triangles(0), mesh.get_num_vertices(0)))

log.append("")
log.append("wired %d/%d" % (ok_n, len([n for n in NAMES if n not in SKIP])))
with open(OUT, "w") as f:
    f.write("\n".join(log) + "\n")
