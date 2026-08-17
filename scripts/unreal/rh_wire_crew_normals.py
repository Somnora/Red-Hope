"""Import derived crew normal maps and wire them into the walker MIs.

Companion to rh_derive_normals.py for the CREW specifically. Separate from
rh_wire_normals.py because the walkers have their own naming (face names, not
model names), their MIs live in SkeletalMeshes/, and they take a gentler normal
strength: a suit is cloth-over-armour, and machinery-strength relief on fabric
reads as crumpled foil.

Until 2026-08-17 M_RH_Character had NO normal or MR input at all, so this is the
first surface detail the crew have ever had beyond flat scalar roughness.
"""
import os

import unreal

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()

NRM_DIR = os.environ.get("RH_NRM_DIR", "")
DEST = "/Game/RedHope/Art/Normals"
STRENGTH = float(os.environ.get("RH_NORM_STRENGTH", "0.75"))
ROOT = "/Game/RedHope/Art/CrewAnim"

log = []


def main():
    wired = skipped = 0
    for fn in sorted(os.listdir(NRM_DIR)):
        if not fn.endswith("_Normal.png"):
            continue
        face = fn[len("T_"):-len("_Normal.png")]
        walker = "RH_Walker_%s" % face
        mi = None
        for a in EAL.list_assets("%s/%s" % (ROOT, walker), recursive=True):
            if a.rstrip("/").split("/")[-1].split(".")[0] == "MI_%s" % walker:
                mi = unreal.load_asset(a)
                break
        if not mi:
            log.append("%-18s NO MI" % face)
            skipped += 1
            continue
        asset_name = "T_%s_Normal" % face
        task = unreal.AssetImportTask()
        task.filename = os.path.join(NRM_DIR, fn)
        task.destination_path = DEST
        task.destination_name = asset_name
        task.automated = True
        task.replace_existing = True
        task.save = True
        AT.import_asset_tasks([task])
        tex = unreal.load_asset("%s/%s.%s" % (DEST, asset_name, asset_name))
        if not tex:
            log.append("%-18s IMPORT FAILED" % face)
            skipped += 1
            continue
        if tex.get_editor_property("compression_settings") != unreal.TextureCompressionSettings.TC_NORMALMAP:
            tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        if tex.get_editor_property("srgb"):
            tex.set_editor_property("srgb", False)
        EAL.save_loaded_asset(tex)
        MEL.set_material_instance_texture_parameter_value(mi, "NormTex", tex)
        MEL.set_material_instance_scalar_parameter_value(mi, "UseNormTex", STRENGTH)
        EAL.save_loaded_asset(mi)
        bt = MEL.get_material_instance_texture_parameter_value(mi, "NormTex")
        bs = MEL.get_material_instance_scalar_parameter_value(mi, "UseNormTex")
        comp_ok = tex.get_editor_property("compression_settings") == unreal.TextureCompressionSettings.TC_NORMALMAP
        ok = bt is not None and abs(bs - STRENGTH) < 1e-3 and comp_ok
        log.append("%-18s %s UseNormTex=%.2f TC_NORMALMAP=%s" % (face, "ok " if ok else "FAIL", bs, comp_ok))
        wired += 1 if ok else 0
    log.append("wired %d, skipped %d" % (wired, skipped))
    open(os.environ.get("RH_REPORT", "/tmp/rh_crew_normals.txt"), "w").write("\n".join(log) + "\n")


main()
