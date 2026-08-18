"""Import rock_*.glb meshes and wire them onto one shared M_RH_Master instance.

  RH_ROCKS_DIR=<dir of rock_*.glb> RH_REPORT=<txt> UnrealEditor-Cmd <proj> \
      -run=pythonscript -script=$PWD/scripts/unreal/rh_import_rocks.py \
      -unattended -nosound -stdout

Rocks are clutter, not hero props: one MI (MI_Rock, a dark basalt pull off the
Mars regolith texture) covers every mesh in the batch, instead of growing a
per-rock Materials/ folder the way Props2/Tiers do for assets that carry their
own textures.

TWO TRAPS THIS ENCODES, both already paid for elsewhere in this tree:

1. Interchange's glTF pipeline nests fresh imports under a SceneName folder
   (bSceneNameSubFolder) plus a StaticMeshes/Textures/Materials split
   (bAssetTypeSubFolders), so rock_boulder01.glb can land at
   Rocks/rock_boulder01/StaticMeshes/rock_boulder01 rather than the flat
   Rocks/rock_boulder01 a constructed path would expect. This finds each mesh
   AFTER importing by listing /Game/RedHope/Art/Rocks recursively and matching
   leaf names, never by building the destination path.

2. mesh.get_editor_property("static_materials") hands back copies -
   rh_wire_dress.py and rh_wire_walkers.py already paid for this one:
   mutating an element of the returned list and walking away writes nothing to
   the asset. The fix proven there is to rebuild the whole array with fresh
   unreal.StaticMaterial() entries, set_editor_property the new list back,
   save, then reload the mesh FRESH off disk and read the slot from that copy.
   A believed write is not a write, which is this project's rule after
   unverified setters turned out to be its #1 bug source.
"""
import os

import unreal

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()

ROCKS_DIR = os.environ.get("RH_ROCKS_DIR", "")
OUT = os.environ.get("RH_REPORT", "/tmp/rh_rocks.txt")
DEST = "/Game/RedHope/Art/Rocks"
MASTER = "/Game/RedHope/Art/M_RH_Master.M_RH_Master"
REGOLITH_TEX = "/Game/RedHope/Art/Mars_Regolith_Texture.Mars_Regolith_Texture"
ACCENT_COLOR = unreal.LinearColor(0.10, 0.075, 0.06, 1.0)  # dark basalt pull

log = []


def import_rocks(glb_dir):
    """Import every rock_*.glb, then find the resulting StaticMesh assets by
    listing DEST recursively and matching leaf names (trap #1) - never by
    constructing the path Interchange might have nested them under."""
    names = []
    for fn in sorted(os.listdir(glb_dir)):
        if not (fn.startswith("rock_") and fn.lower().endswith(".glb")):
            continue
        name = fn[:-len(".glb")]
        task = unreal.AssetImportTask()
        task.filename = os.path.join(glb_dir, fn)
        task.destination_path = DEST
        task.automated = True
        task.replace_existing = True
        task.save = True
        AT.import_asset_tasks([task])
        names.append(name)

    found = {}
    if names:
        want = {n.lower(): n for n in names}
        for a in EAL.list_assets(DEST, recursive=True):
            leaf = a.rstrip("/").split("/")[-1].split(".")[0]
            canon = want.get(leaf.lower())
            if not canon or canon in found:
                continue
            obj = unreal.load_asset(a)
            if isinstance(obj, unreal.StaticMesh):
                found[canon] = (obj, a)

    for name in names:
        if name not in found:
            log.append("FAIL  %-24s not found under %s after import" % (name, DEST))
    return found


def create_mi_rock():
    """Create (or reuse) MI_Rock and set its surface parameters. Not read back
    parameter-by-parameter - Props2/Tiers set MI params the same way and only
    verify the mesh-side slot assignment, which is the read that actually
    proves a rock renders right."""
    master = unreal.load_asset(MASTER)
    if not master:
        log.append("ABORT: master material missing: %s" % MASTER)
        return None

    mi = unreal.load_asset("%s/MI_Rock.MI_Rock" % DEST)
    if not mi:
        mi = AT.create_asset("MI_Rock", DEST, unreal.MaterialInstanceConstant,
                             unreal.MaterialInstanceConstantFactoryNew())
    if not mi:
        log.append("ABORT: failed to create MI_Rock")
        return None
    mi.set_editor_property("parent", master)

    base_tex = unreal.load_asset(REGOLITH_TEX)
    if base_tex:
        MEL.set_material_instance_texture_parameter_value(mi, "BaseTex", base_tex)
    else:
        log.append("  ! %s missing - MI_Rock has no BaseTex" % REGOLITH_TEX)

    MEL.set_material_instance_scalar_parameter_value(mi, "Rough", 0.92)
    MEL.set_material_instance_scalar_parameter_value(mi, "Metallic", 0.0)
    MEL.set_material_instance_scalar_parameter_value(mi, "UseMRTex", 0.0)
    MEL.set_material_instance_scalar_parameter_value(mi, "UseNormTex", 0.0)
    MEL.set_material_instance_scalar_parameter_value(mi, "EmissiveAmount", 0.0)
    MEL.set_material_instance_scalar_parameter_value(mi, "AccentAmount", 0.45)
    MEL.set_material_instance_vector_parameter_value(mi, "AccentColor", ACCENT_COLOR)

    EAL.save_loaded_asset(mi)
    return mi


def wire_mesh(name, mesh, mesh_path, mi):
    """Point slot 0 at MI_Rock by rebuilding static_materials (trap #2), save,
    then reload the mesh fresh off disk and read the slot back from that."""
    materials = mesh.get_editor_property("static_materials")
    if not materials:
        log.append("FAIL  %-24s %-55s no material slots" % (name, mesh_path))
        return False

    rebuilt = []
    for i, sm in enumerate(materials):
        entry = unreal.StaticMaterial()
        entry.set_editor_property("material_interface", mi if i == 0 else sm.material_interface)
        entry.set_editor_property("material_slot_name", sm.material_slot_name)
        rebuilt.append(entry)
    mesh.set_editor_property("static_materials", rebuilt)
    EAL.save_loaded_asset(mesh)

    # Read BACK from a fresh load. A believed write is not a write.
    fresh = unreal.load_asset(mesh_path)
    got = fresh.get_editor_property("static_materials") if fresh else []
    slot0 = got[0].material_interface if got else None
    ok = bool(slot0) and slot0.get_path_name() == mi.get_path_name()
    log.append("%-5s %-24s %-55s slot0=%s" % (
        "ok" if ok else "FAIL", name, mesh_path, slot0.get_name() if slot0 else "<none>"))
    return ok


def main():
    if not ROCKS_DIR or not os.path.isdir(ROCKS_DIR):
        log.append("RH_ROCKS_DIR missing or not a directory: %r" % ROCKS_DIR)
    else:
        found = import_rocks(ROCKS_DIR)
        mi = create_mi_rock()

        wired = 0
        for name in sorted(found):
            mesh, mesh_path = found[name]
            if not mi:
                log.append("FAIL  %-24s %-55s MI_Rock unavailable" % (name, mesh_path))
                continue
            if wire_mesh(name, mesh, mesh_path, mi):
                wired += 1

        log.append("imported %d, wired %d" % (len(found), wired))

    with open(OUT, "w") as f:
        f.write("\n".join(log) + "\n")
    for line in log:
        print(line)


main()
