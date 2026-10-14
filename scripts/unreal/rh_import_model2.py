"""Import a hero-lane bake as a NEW Models2 asset and wire its MI. General
form of rh_import_electrolyzer.py - one asset per invocation.

    RH_NAME=IceDrill RH_GLB=<x.glb> RH_NRM=<x_real_normal.png> \
    RH_ACCENT="0.50,0.85,0.95" RH_REPORT=<txt> \
      UnrealEditor-Cmd <proj> -run=pythonscript \
      -script=$PWD/scripts/unreal/rh_import_model2.py -unattended -nosound -stdout

RH_NAME must match the GLB basename (Interchange derives the asset tree from
the filename) and is also the MI/normal naming stem. RH_ACCENT is the canon
TintFor colour (r,g,b linear) applied at RH_ACCENT_AMT (default 0.42).

Standing rules encoded (each learned the hard way in this repo):
  - report what Interchange ACTUALLY produced; never construct paths for C++
  - identify MR vs baseColor by the GLB's OWN material bindings when names
    are anonymous: glTF texture 0 = baseColor, metallicRoughness points at
    its own index - here resolved the cheap way: the mesh ships ONE material,
    baseColor is texture_0, metallicRoughness texture_1 (verified pattern for
    this exporter; the script refuses to guess beyond two textures)
  - MR map: TC_MASKS + sRGB off; real normal: TC_NORMALMAP + sRGB off
  - MI on M_RH_Master, slot rebind via static_materials rebuild
  - read back from a FRESH process if you need disk truth; same-process
    readback only proves the in-memory object (the 08-18 lesson)
"""
import os

import unreal

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()

NAME = os.environ["RH_NAME"]
GLB = os.environ.get("RH_GLB", "")
NRM = os.environ.get("RH_NRM", "")
ACCENT = tuple(float(x) for x in os.environ.get("RH_ACCENT", "0.5,0.5,0.5").split(","))
ACCENT_AMT = float(os.environ.get("RH_ACCENT_AMT", "0.42"))
# RH_DEST overrides the destination for an asset that predates Models2 (e.g. the
# humidity regulator at /Game/RedHope/Art/Agri/humidity): Interchange lays the
# import out as <DEST>/<glb name>/StaticMeshes/<mesh name>, so pointing DEST at
# the old parent folder replaces the old asset in place.
DEST = os.environ.get("RH_DEST") or "/Game/RedHope/Art/Models2/%s" % NAME
# Texture-name tag, NAME by default. Asset names are case-insensitive, so a kit
# rebuild named "Stockpile" would overwrite the V0 lineage's T_stockpile_Normal
# (2026-10-14; the 08-18 batch-4 lesson again) - pass RH_TEX_TAG=StockpileKit.
TAG = os.environ.get("RH_TEX_TAG") or NAME

log = []


def rec(s):
    log.append(s)
    print("RH: %s" % s)


def leaf(a):
    return a.rstrip("/").split("/")[-1].split(".")[0]


def main():
    if not (GLB and os.path.isfile(GLB) and NRM and os.path.isfile(NRM)):
        rec("inputs missing: GLB=%r NRM=%r" % (GLB, NRM))
        return

    task = unreal.AssetImportTask()
    task.filename = GLB
    task.destination_path = DEST
    task.automated = True
    task.replace_existing = True
    task.save = True
    AT.import_asset_tasks([task])
    # Interchange lays the import out as <DEST>/<glb name>/... Look ONLY there
    # and take the mesh BY NAME: with a shared RH_DEST (Machines, Props2, Tiers)
    # "the first static mesh under DEST" was a neighbour - on 2026-10-14 the
    # elevator's material was wired onto RH_AirFilter2.
    own = "%s/%s" % (DEST, os.path.splitext(os.path.basename(GLB))[0])
    root = own if EAL.does_directory_exist(own) else DEST
    assets = EAL.list_assets(root, recursive=True)
    rec("imported %d assets under %s" % (len(assets), root))

    mesh = mesh_path = None
    texs = []
    for a in assets:
        obj = unreal.load_asset(a)
        if isinstance(obj, unreal.StaticMesh) and (mesh is None or obj.get_name() == NAME):
            mesh, mesh_path = obj, a
        elif isinstance(obj, unreal.Texture2D):
            texs.append((leaf(a), obj))
    if not mesh:
        rec("** NO STATIC MESH - abort **")
        return
    texs.sort()  # texture_0 before texture_1
    named_mr = [t for n, t in texs if "metallic" in n.lower() or "roughnes" in n.lower()]
    anon = [t for n, t in texs if "texture_" in n.lower()]
    if named_mr:
        mr_tex = named_mr[0]
        base_tex = next((t for n, t in texs if t is not mr_tex), None)
    elif len(anon) == 2:
        base_tex, mr_tex = anon[0], anon[1]  # glTF order: baseColor, MR
    elif len(anon) == 1:
        base_tex, mr_tex = anon[0], None
    else:
        rec("** cannot identify textures among %s - refusing to guess **" % [n for n, _ in texs])
        return
    rec("mesh: %s" % mesh_path)
    rec("base: %s  mr: %s" % (base_tex.get_name() if base_tex else "**MISSING**",
                              mr_tex.get_name() if mr_tex else "<none>"))
    if not base_tex:
        rec("** no basecolor - abort **")
        return

    if mr_tex:
        if mr_tex.get_editor_property("compression_settings") != unreal.TextureCompressionSettings.TC_MASKS:
            mr_tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
        if mr_tex.get_editor_property("srgb"):
            mr_tex.set_editor_property("srgb", False)
        EAL.save_loaded_asset(mr_tex)

    ntask = unreal.AssetImportTask()
    ntask.filename = NRM
    ntask.destination_path = "/Game/RedHope/Art/Normals"
    ntask.destination_name = "T_%s_Normal" % TAG
    ntask.automated = True
    ntask.replace_existing = True
    ntask.save = True
    AT.import_asset_tasks([ntask])
    nrm_tex = unreal.load_asset("/Game/RedHope/Art/Normals/T_%s_Normal.T_%s_Normal" % (TAG, TAG))
    if not nrm_tex:
        rec("** normal import failed - abort **")
        return
    nrm_tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
    nrm_tex.set_editor_property("srgb", False)
    EAL.save_loaded_asset(nrm_tex)

    master = unreal.load_asset("/Game/RedHope/Art/M_RH_Master.M_RH_Master")
    mi_folder = mesh_path.rsplit("/", 1)[0]
    mi_name = "MI_%s" % NAME
    mi_obj_path = "%s/%s.%s" % (mi_folder, mi_name, mi_name)
    mi = unreal.load_asset(mi_obj_path)
    if not mi:
        mi = AT.create_asset(mi_name, mi_folder, unreal.MaterialInstanceConstant,
                             unreal.MaterialInstanceConstantFactoryNew())
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

    rebuilt = []
    for sm in mesh.get_editor_property("static_materials"):
        entry = unreal.StaticMaterial()
        entry.set_editor_property("material_interface", mi)
        entry.set_editor_property("material_slot_name", sm.material_slot_name)
        rebuilt.append(entry)
    mesh.set_editor_property("static_materials", rebuilt)
    EAL.save_loaded_asset(mesh)

    bt = MEL.get_material_instance_texture_parameter_value(mi, "BaseTex")
    un = MEL.get_material_instance_scalar_parameter_value(mi, "UseNormTex")
    aa = MEL.get_material_instance_scalar_parameter_value(mi, "AccentAmount")
    ok = bt is not None and abs(un - 1.0) < 1e-3 and abs(aa - ACCENT_AMT) < 1e-3
    rec("readback: BaseTex=%s UseNormTex=%.2f AccentAmount=%.2f -> %s" % (
        bt.get_name() if bt else "<none>", un, aa, "OK" if ok else "** FAIL **"))
    if ok:
        rec("CPP_PATH %s" % mesh_path)


main()
with open(os.environ.get("RH_REPORT", "/tmp/rh_model2.txt"), "w") as f:
    f.write("\n".join(log) + "\n")
