"""Import the Electrolyzer bake as a NEW Models2 asset and wire its MI.

    RH_GLB=<Electrolyzer.glb> RH_NRM=<real_normal.png> RH_REPORT=<txt> \
      UnrealEditor-Cmd <proj> -run=pythonscript \
      -script=$PWD/scripts/unreal/rh_import_electrolyzer.py -unattended -nosound -stdout

First asset through the professor's phase-4 lane: hero reference -> TRELLIS.2
with RH_KEEP_HIPOLY -> Blender-baked REAL normal -> this import. The building
drew composed primitives before (never had model art), so this is a NEW
ModelPathsV2 row, not a replacement - the C++ edit happens after this script
reports the ACTUAL imported mesh path. Nothing here constructs paths for the
C++ side; the report is the source of truth.

Encodes the standing rules, each learned the hard way in this repo:
  - list what Interchange ACTUALLY produced; never construct asset paths
    (five naming conventions live in this art tree already)
  - MR map: TC_MASKS + sRGB off (glTF G=rough B=metal is data, not colour)
  - real normal: TC_NORMALMAP + sRGB off, read back after set
  - MI on M_RH_Master with slot rebind via static_materials rebuild
    (per-index set_material silently no-ops on saved assets)
  - AccentColor violet + AccentAmount 0.42 - the surface-identity level the
    director's "no color design" complaint calibrated on 2026-08-18
  - read back EVERYTHING; report FAIL rather than assume a setter worked
"""
import os

import unreal

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()

GLB = os.environ.get("RH_GLB", "")
NRM = os.environ.get("RH_NRM", "")
DEST = "/Game/RedHope/Art/Models2/Electrolyzer"
ACCENT = (0.55, 0.35, 0.90)  # violet - matches the reference's lit readout strip
ACCENT_AMT = 0.42

log = []


def rec(s):
    log.append(s)
    print("RH: %s" % s)


def leaf(a):
    return a.rstrip("/").split("/")[-1].split(".")[0]


def import_glb():
    task = unreal.AssetImportTask()
    task.filename = GLB
    task.destination_path = DEST
    task.automated = True
    task.replace_existing = True
    task.save = True
    AT.import_asset_tasks([task])
    return EAL.list_assets(DEST, recursive=True)


def main():
    if not (GLB and os.path.isfile(GLB)):
        rec("RH_GLB missing: %r" % GLB)
        return
    if not (NRM and os.path.isfile(NRM)):
        rec("RH_NRM missing: %r" % NRM)
        return

    assets = import_glb()
    rec("imported %d assets under %s" % (len(assets), DEST))
    for a in assets:
        rec("  %s" % a)

    # Find the pieces by TYPE and NAME PATTERN among what actually landed.
    mesh = mesh_path = None
    base_tex = mr_tex = None
    for a in assets:
        obj = unreal.load_asset(a)
        if isinstance(obj, unreal.StaticMesh) and mesh is None:
            mesh, mesh_path = obj, a
        elif isinstance(obj, unreal.Texture2D):
            l = leaf(a).lower()
            if "metallic" in l or "roughnes" in l:
                mr_tex = obj
            elif base_tex is None:
                base_tex = obj
    if not mesh:
        rec("** NO STATIC MESH imported - abort **")
        return
    rec("mesh: %s (%d slots)" % (mesh_path, mesh.get_num_sections(0)))
    rec("base: %s" % (base_tex.get_name() if base_tex else "** MISSING **"))
    rec("mr:   %s" % (mr_tex.get_name() if mr_tex else "<none - UseMRTex stays 0>"))
    if not base_tex:
        rec("** NO BASECOLOR TEXTURE - abort before shipping a gray building **")
        return

    # MR is data, not colour.
    if mr_tex:
        changed = []
        if mr_tex.get_editor_property("compression_settings") != unreal.TextureCompressionSettings.TC_MASKS:
            mr_tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
            changed.append("TC_MASKS")
        if mr_tex.get_editor_property("srgb"):
            mr_tex.set_editor_property("srgb", False)
            changed.append("sRGB off")
        if changed:
            EAL.save_loaded_asset(mr_tex)
            rec("mr fixed: %s" % ", ".join(changed))

    # Real baked normal -> the shared Normals folder, TC_NORMALMAP + sRGB off.
    ntask = unreal.AssetImportTask()
    ntask.filename = NRM
    ntask.destination_path = "/Game/RedHope/Art/Normals"
    ntask.destination_name = "T_Electrolyzer_Normal"
    ntask.automated = True
    ntask.replace_existing = True
    ntask.save = True
    AT.import_asset_tasks([ntask])
    nrm_tex = unreal.load_asset("/Game/RedHope/Art/Normals/T_Electrolyzer_Normal.T_Electrolyzer_Normal")
    if not nrm_tex:
        rec("** normal import produced nothing - abort **")
        return
    if nrm_tex.get_editor_property("compression_settings") != unreal.TextureCompressionSettings.TC_NORMALMAP:
        nrm_tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
    if nrm_tex.get_editor_property("srgb"):
        nrm_tex.set_editor_property("srgb", False)
    EAL.save_loaded_asset(nrm_tex)
    comp = nrm_tex.get_editor_property("compression_settings")
    srgb = nrm_tex.get_editor_property("srgb")
    rec("normal: T_Electrolyzer_Normal comp=%s srgb=%s %s" % (
        comp, srgb,
        "ok" if comp == unreal.TextureCompressionSettings.TC_NORMALMAP and not srgb else "** BAD **"))

    # MI_Electrolyzer beside the mesh, parented to M_RH_Master.
    master = unreal.load_asset("/Game/RedHope/Art/M_RH_Master.M_RH_Master")
    if not master:
        rec("** M_RH_Master missing - abort **")
        return
    mi_folder = mesh_path.rsplit("/", 1)[0]
    mi_obj_path = "%s/MI_Electrolyzer.MI_Electrolyzer" % mi_folder
    mi = unreal.load_asset(mi_obj_path)
    if not mi:
        mi = AT.create_asset("MI_Electrolyzer", mi_folder,
                             unreal.MaterialInstanceConstant,
                             unreal.MaterialInstanceConstantFactoryNew())
    if not mi:
        rec("** MI create failed **")
        return
    mi.set_editor_property("parent", master)
    MEL.set_material_instance_texture_parameter_value(mi, "BaseTex", base_tex)
    if mr_tex:
        MEL.set_material_instance_texture_parameter_value(mi, "MRTex", mr_tex)
    MEL.set_material_instance_scalar_parameter_value(mi, "UseMRTex", 1.0 if mr_tex else 0.0)
    MEL.set_material_instance_texture_parameter_value(mi, "NormTex", nrm_tex)
    MEL.set_material_instance_scalar_parameter_value(mi, "UseNormTex", 1.0)
    MEL.set_material_instance_scalar_parameter_value(mi, "EmissiveAmount", 0.0)
    MEL.set_material_instance_vector_parameter_value(mi, "AccentColor", unreal.LinearColor(*ACCENT, 1.0))
    MEL.set_material_instance_scalar_parameter_value(mi, "AccentAmount", ACCENT_AMT)
    EAL.save_loaded_asset(mi)

    # Slot rebind via static_materials rebuild - the pattern from
    # rh_wire_originals.py; per-index SetMaterial does not persist on assets.
    rebuilt = []
    for sm in mesh.get_editor_property("static_materials"):
        entry = unreal.StaticMaterial()
        entry.set_editor_property("material_interface", mi)
        entry.set_editor_property("material_slot_name", sm.material_slot_name)
        rebuilt.append(entry)
    if not rebuilt:
        entry = unreal.StaticMaterial()
        entry.set_editor_property("material_interface", mi)
        entry.set_editor_property("material_slot_name", unreal.Name("Slot0"))
        rebuilt.append(entry)
    mesh.set_editor_property("static_materials", rebuilt)
    EAL.save_loaded_asset(mesh)

    # Read back the whole chain from FRESH loads.
    fresh_mi = unreal.load_asset(mi_obj_path)
    fresh_mesh = unreal.load_asset(mesh_path)
    bt = MEL.get_material_instance_texture_parameter_value(fresh_mi, "BaseTex")
    nt = MEL.get_material_instance_texture_parameter_value(fresh_mi, "NormTex")
    un = MEL.get_material_instance_scalar_parameter_value(fresh_mi, "UseNormTex")
    um = MEL.get_material_instance_scalar_parameter_value(fresh_mi, "UseMRTex")
    aa = MEL.get_material_instance_scalar_parameter_value(fresh_mi, "AccentAmount")
    slots = [str(s.get_editor_property("material_interface").get_name())
             if s.get_editor_property("material_interface") else "<none>"
             for s in fresh_mesh.get_editor_property("static_materials")]
    ok = (bt is not None and nt is not None and abs(un - 1.0) < 1e-3
          and abs(aa - ACCENT_AMT) < 1e-3 and all(s == "MI_Electrolyzer" for s in slots))
    rec("readback: BaseTex=%s NormTex=%s UseNormTex=%.2f UseMRTex=%.2f AccentAmount=%.2f slots=%s -> %s" % (
        bt.get_name() if bt else "<none>", nt.get_name() if nt else "<none>",
        un, um, aa, slots, "OK" if ok else "** FAIL **"))
    if ok:
        rec("CPP_PATH %s" % mesh_path)  # paste THIS into ModelPathsV2


main()
with open(os.environ.get("RH_REPORT", "/tmp/rh_electro.txt"), "w") as f:
    f.write("\n".join(log) + "\n")
