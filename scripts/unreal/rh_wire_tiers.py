"""Wire the six Tiers furniture meshes onto M_RH_Master. Compile-free.

  UnrealEditor-Cmd <proj> -run=pythonscript \
      -script=$PWD/scripts/unreal/rh_wire_tiers.py -unattended -nosound -stdout

Same discipline as rh_wire_props.py, for the same reason: full asset paths
only (never name search - the Props/Props2 false-done came from exactly that),
and the mesh slot is READ BACK after assignment because UE Python's
static_materials getter returns copies and a believed write is not a write.

No emissive here: the parent's EmissiveAmount defaults to 0 and the mask
defaults to white, which multiplied together stay dark. If a Tiers piece ever
earns lit screens, cut a mask and add the three parameters then.
"""
import os
import unreal

MEL = unreal.MaterialEditingLibrary
OUT = os.environ.get("RH_REPORT", "/tmp/rh_wire_tiers.txt")
MASTER = "/Game/RedHope/Art/M_RH_Master.M_RH_Master"
NAMES = ["workbench_lg", "chemtable_lg", "infirmary", "lab_full", "workshop", "chemtable_sm"]

log = []
tools = unreal.AssetToolsHelpers.get_asset_tools()
master = unreal.load_asset(MASTER)
registry = unreal.AssetRegistryHelpers.get_asset_registry()

for name in NAMES if master else []:
    base_dir = "/Game/RedHope/Art/Tiers/%s" % name
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

    mr = None
    for a in registry.get_assets_by_path("%s/Textures" % base_dir, recursive=False):
        if "metallic" in str(a.asset_name).lower():
            mr = unreal.load_asset("%s.%s" % (str(a.package_name), str(a.asset_name)))
            break
    if mr:
        MEL.set_material_instance_texture_parameter_value(mi, "MRTex", mr)
        MEL.set_material_instance_scalar_parameter_value(mi, "UseMRTex", 1.0)
    else:
        MEL.set_material_instance_scalar_parameter_value(mi, "Rough", 0.70)

    unreal.EditorAssetLibrary.save_loaded_asset(mi)
    mesh.set_material(0, mi)
    unreal.EditorAssetLibrary.save_loaded_asset(mesh)

    check = mesh.get_editor_property("static_materials")[0].material_interface
    got = check.get_name() if check else "<none>"
    log.append("%-5s %-14s slot0=%-16s albedo=%s MR=%s  tris=%d verts=%d" % (
        "ok" if got == "MI_%s" % name else "FAIL", name, got,
        "yes" if albedo else "NO", "yes" if mr else "no",
        mesh.get_num_triangles(0), mesh.get_num_vertices(0)))

with open(OUT, "w") as f:
    f.write("\n".join(log) + "\n")
