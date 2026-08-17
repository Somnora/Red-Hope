"""Wire each walker's ALREADY-IMPORTED metallic-roughness map into its MI.

The maps were imported with the meshes and then bound by nothing: until
2026-08-17 M_RH_Character had no MRTex parameter to bind them to. Names are
READ OFF DISK, never constructed - UE truncates asset names at 63 chars and
some of these pairs are long enough to hit it (the labbench lesson).
"""
import os, unreal
MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
log = []
root = "/Game/RedHope/Art/CrewAnim"
wired = skipped = 0
for d in EAL.list_assets(root, recursive=False, include_folder=True):
    name = d.rstrip("/").split("/")[-1]
    if not name.startswith("RH_Walker_"):
        continue
    face = name[len("RH_Walker_"):]
    # The MI is NOT in Materials/ for these - it sits in SkeletalMeshes/ beside
    # the mesh (same trap as the Models2 buildings). Search the asset's own
    # folder tree instead of assuming a subfolder name.
    mi = None
    for a in EAL.list_assets("%s/%s" % (root, name), recursive=True):
        leaf = a.rstrip("/").split("/")[-1].split(".")[0]
        if leaf == "MI_%s" % name:
            mi = unreal.load_asset(a); break
    if not mi:
        log.append("%-34s MI MISSING" % name); skipped += 1; continue
    # find the MR texture by listing the Textures folder, not by guessing
    mr = None
    for a in EAL.list_assets("%s/%s" % (root, name), recursive=True):
        leaf = a.rstrip("/").split("/")[-1].split(".")[0]
        if "metallic" in leaf and "roughnes" in leaf:
            mr = unreal.load_asset(a); break
    if not mr:
        log.append("%-34s no MR texture on disk" % name); skipped += 1; continue
    # data map, not colour: TC_MASKS + sRGB off, or roughness decodes wrong
    changed = []
    if mr.get_editor_property("compression_settings") != unreal.TextureCompressionSettings.TC_MASKS:
        mr.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
        changed.append("TC_MASKS")
    if mr.get_editor_property("srgb"):
        mr.set_editor_property("srgb", False); changed.append("sRGB off")
    if changed:
        EAL.save_loaded_asset(mr)
    MEL.set_material_instance_texture_parameter_value(mi, "MRTex", mr)
    MEL.set_material_instance_scalar_parameter_value(mi, "UseMRTex", 1.0)
    EAL.save_loaded_asset(mi)
    # READ BACK - a set that silently no-ops is this project's oldest trap
    back = MEL.get_material_instance_scalar_parameter_value(mi, "UseMRTex")
    tex = MEL.get_material_instance_texture_parameter_value(mi, "MRTex")
    ok = abs(back - 1.0) < 1e-3 and tex is not None
    log.append("%-34s %s MRTex=%s UseMRTex=%.1f %s" % (
        name, "ok " if ok else "FAIL", tex.get_name()[:44] if tex else "<none>", back,
        ("[" + ", ".join(changed) + "]") if changed else ""))
    wired += 1 if ok else 0
log.append("wired %d, skipped %d" % (wired, skipped))
open(os.environ["RH_REPORT"], "w").write("\n".join(log) + "\n")
