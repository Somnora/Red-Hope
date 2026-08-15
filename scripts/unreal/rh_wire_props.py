"""Wire the Props2 room props into the M_RH_Master family. Compile-free.

  UnrealEditor-Cmd <proj> -run=pythonscript \
      -script=$PWD/scripts/unreal/rh_wire_props.py -unattended -nosound -stdout

WHY THIS EXISTS, AND THE MISTAKE IT CORRECTS: commit 92be72c claimed the room
props had joined the master material family. They had not. rh_assign_masks.py
searched /Game/RedHope/Art for an asset merely NAMED MI_<n> and took the first
hit, which is the FLAT Props/<n>/MI_<n> lineage. But RoomPropPath
(RHColonyVisualizerSubsystem.cpp) loads Props2/<n>/StaticMeshes/<n>, whose slot 0
points at Props2/<n>/Materials/Material - a plain Interchange instance of
M_GLTF, with no MI_ asset anywhere under Props2. So nine of the ten reparents
landed on meshes nothing loads (only Props/locker is referenced, as clutter) and
every prop a player sees in a room was still emissive-dead.

Name-matching across two lineages is the trap; this script addresses assets by
FULL PATH only, and asserts the mesh's slot 0 afterwards.

What it does per prop: create Props2/<n>/Materials/MI_<n> parented to
M_RH_Master, carry over BaseTex and the glTF metallic-roughness map (via the
master's UseMRTex path, so the per-pixel surface survives the move), assign the
already-committed emissive mask plus its colour and strength, and set the mesh's
slot 0 to the new instance.
"""
import os
import unreal

MEL = unreal.MaterialEditingLibrary
OUT = os.environ.get("RH_REPORT", "/tmp/rh_wire_props.txt")
MASTER = "/Game/RedHope/Art/M_RH_Master.M_RH_Master"
MASKS = "/Game/RedHope/Art/Masks"

SCREEN = unreal.LinearColor(0.25, 2.20, 2.80, 1.0)
WARM = unreal.LinearColor(2.60, 1.45, 0.35, 1.0)

# name -> (EmissiveAmount, EmissiveColor)
PROPS = {
    "bunk":        (0.9, SCREEN),
    "conduit":     (0.8, SCREEN),
    "console":     (1.4, SCREEN),
    "diningtable": (0.8, SCREEN),
    "galley":      (1.0, WARM),
    "labbench":    (1.1, SCREEN),
    "locker":      (0.6, SCREEN),
    "planter_dry": (0.8, SCREEN),
    "planter_wet": (0.8, SCREEN),
    "tank":        (0.9, SCREEN),
}

log = []
tools = unreal.AssetToolsHelpers.get_asset_tools()
master = unreal.load_asset(MASTER)
if not master:
    log.append("ABORT: %s missing" % MASTER)

for name, (amount, colour) in sorted(PROPS.items()) if master else []:
    base_dir = "/Game/RedHope/Art/Props2/%s" % name
    mesh_path = "%s/StaticMeshes/%s.%s" % (base_dir, name, name)
    mesh = unreal.load_asset(mesh_path)
    if not mesh:
        log.append("MISSING MESH %s" % mesh_path)
        continue

    mi_pkg = "%s/Materials/MI_%s" % (base_dir, name)
    mi = unreal.load_asset("%s.MI_%s" % (mi_pkg, name))
    if not mi:
        mi = tools.create_asset("MI_%s" % name, "%s/Materials" % base_dir,
                                unreal.MaterialInstanceConstant,
                                unreal.MaterialInstanceConstantFactoryNew())
    if not mi:
        log.append("FAILED to create MI for %s" % name)
        continue
    mi.set_editor_property("parent", master)

    albedo = unreal.load_asset("%s/Textures/prop_%s_textured.prop_%s_textured" % (base_dir, name, name))
    if albedo:
        MEL.set_material_instance_texture_parameter_value(mi, "BaseTex", albedo)
    else:
        log.append("  ! %s has no albedo - it would render as the parent default" % name)

    # glTF packs roughness in G and metallic in B of one texture; the master
    # reads those channels only when UseMRTex is 1. The asset NAME is the glTF
    # image name and is sometimes truncated on import, so scan the folder for
    # anything carrying "metallic" instead of trusting an exact spelling.
    mr = None
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    for a in registry.get_assets_by_path("%s/Textures" % base_dir, recursive=False):
        if "metallic" in str(a.asset_name).lower():
            mr = unreal.load_asset("%s.%s" % (str(a.package_name), str(a.asset_name)))
            break
    if mr:
        MEL.set_material_instance_texture_parameter_value(mi, "MRTex", mr)
        MEL.set_material_instance_scalar_parameter_value(mi, "UseMRTex", 1.0)
    else:
        MEL.set_material_instance_scalar_parameter_value(mi, "UseMRTex", 0.0)
        MEL.set_material_instance_scalar_parameter_value(mi, "Rough", 0.70)

    mask_name = "T_%s_EmissiveMask" % name
    mask = unreal.load_asset("%s/%s.%s" % (MASKS, mask_name, mask_name))
    if mask:
        MEL.set_material_instance_texture_parameter_value(mi, "EmissiveMask", mask)
        MEL.set_material_instance_scalar_parameter_value(mi, "EmissiveAmount", amount)
        MEL.set_material_instance_vector_parameter_value(mi, "EmissiveColor", colour)
    else:
        log.append("  ! %s has no mask" % name)

    unreal.EditorAssetLibrary.save_loaded_asset(mi)

    # Point the mesh at it, then read the slot back: the whole reason this file
    # exists is that an assignment was believed rather than verified. First
    # attempt used get/set of static_materials, and the read-back proved the
    # write never stuck (the getter returns copies). set_material is the real
    # engine-side setter.
    mesh.set_material(0, mi)
    unreal.EditorAssetLibrary.save_loaded_asset(mesh)

    check = mesh.get_editor_property("static_materials")[0].material_interface
    got = check.get_name() if check else "<none>"
    parent = mi.get_editor_property("parent")
    log.append("%-5s %-12s slot0=%-14s parent=%-13s BaseTex=%s  MR=%s  mask=%s" % (
        "ok" if got == ("MI_%s" % name) else "FAIL",
        name, got,
        parent.get_name() if parent else "<none>",
        albedo.get_name() if albedo else "NONE",
        "yes" if mr else "no",
        "yes" if mask else "no"))

with open(OUT, "w") as f:
    f.write("\n".join(log) + "\n")
