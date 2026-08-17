"""Put the crew walkers on M_RH_Character instead of Interchange's M_GLTF.

  RH_REPORT=<out.txt> UnrealEditor-Cmd <proj> -run=pythonscript \
      -script=$PWD/scripts/unreal/rh_wire_walkers.py -unattended -nosound -stdout

THE BUG THIS FIXES
------------------
Director, 2026-08-17: "a lot of the character models still look splotchy,
sometimes you can see through parts of their body."

All 20 walkers had slot 0 pointing at their Interchange auto-generated
`Materials/Material`, whose base is `/InterchangeAssets/gltf/Substrate/M_GLTF`.
That material's shading model is **MSM_SUBSURFACE_PROFILE** - a skin/SSS
shader. Subsurface scattering bleeds light THROUGH thin geometry, which is the
see-through, and mottles the surface, which is the splotch. One flag, both
symptoms, on an albedo that was never authored as skin.

`M_RH_Character` already existed and is correct: BLEND_OPAQUE,
MSM_DEFAULT_LIT, used_with_skeletal_mesh=True. So did all 20
`MI_RH_Walker_<n>` instances, already parented to it with BaseTex wired. The
authoring was done; nothing ever put it on the mesh slot. Same false-done shape
as the Props/Props2 episode - which is why this reads the slot BACK rather than
trusting the write.

MRTex is wired here too: the metallic-roughness map exists beside every walker
and was left unset on every instance, so they were all rendering at the
material's default metallic/roughness.

Skeletal meshes hold `FSkeletalMaterial`, not a plain material pointer, and UE
Python hands back COPIES of the array - so the whole array is rebuilt and
assigned, then re-read from the asset to confirm.
"""
import os

import unreal

MEL = unreal.MaterialEditingLibrary
OUT = os.environ.get("RH_REPORT", "/tmp/rh_wire_walkers.txt")
# RobotAnim carries the same defect: RH_Walker_robot was on M_GLTF too, and a
# metal robot rendering through a subsurface-scattering shader is a defect
# rather than the "robots keep their look" the director scoped out.
ROOTS = ["/Game/RedHope/Art/CrewAnim", "/Game/RedHope/Art/RobotAnim"]
TARGET_BASE = "M_RH_Character"

reg = unreal.AssetRegistryHelpers.get_asset_registry()

# Full paths only. Never a name search across lineages.
found = []
for root in ROOTS:
    for a in reg.get_assets_by_path(root, recursive=True):
        p = str(a.package_name)
        parts = p.split("/")
        if len(parts) < 3:
            continue
        leaf = parts[-1]
        if parts[-2] == "SkeletalMeshes" and leaf == parts[-3] and leaf.startswith("RH_Walker_"):
            found.append((root, leaf))
found = sorted(set(found))

log = []
ok_n = 0
for root, n in found:
    base_dir = "%s/%s" % (root, n)
    mesh = unreal.load_asset("%s/SkeletalMeshes/%s.%s" % (base_dir, n, n))
    mi = unreal.load_asset("%s/SkeletalMeshes/MI_%s.MI_%s" % (base_dir, n, n))
    if not mesh or not mi:
        log.append("MISSING  %s (mesh=%s mi=%s)" % (n, bool(mesh), bool(mi)))
        continue

    base = mi.get_base_material()
    if not base or base.get_name() != TARGET_BASE:
        log.append("SKIP     %s - MI base is %s, expected %s"
                   % (n, base.get_name() if base else "<none>", TARGET_BASE))
        continue

    # Wire the metallic-roughness map that was left unset on every instance.
    stem = n.replace("RH_Walker_", "")
    mr = unreal.load_asset("%s/Textures/%s_textured_metallic-%s_textured_roughness"
                           ".%s_textured_metallic-%s_textured_roughness"
                           % (base_dir, stem, stem, stem, stem))
    mr_state = "absent"
    if mr:
        # Compression: every walker MR map imported as TC_DEFAULT, which is
        # block compression tuned for COLOUR. On a packed metallic-roughness
        # map that puts DXT blocking straight into roughness, and blocky
        # roughness reads as mottled specular - part of the director's
        # "splotchy". rh_import_textures.py has asserted TC_MASKS for MR maps
        # since the crops; these predate it. sRGB was already correct (False).
        if mr.get_editor_property("compression_settings") != \
                unreal.TextureCompressionSettings.TC_MASKS:
            mr.set_editor_property("compression_settings",
                                   unreal.TextureCompressionSettings.TC_MASKS)
            mr.set_editor_property("srgb", False)
            unreal.EditorAssetLibrary.save_loaded_asset(mr)
            mr_state = "comp+"
        if not MEL.get_material_instance_texture_parameter_value(mi, "MRTex"):
            MEL.set_material_instance_texture_parameter_value(mi, "MRTex", mr)
            mr_state = ("wired" if mr_state == "absent" else "wired+comp")
        elif mr_state == "absent":
            mr_state = "already"
        unreal.EditorAssetLibrary.save_loaded_asset(mi)

    # Rebuild the whole FSkeletalMaterial array; the getter returns copies.
    mats = mesh.get_editor_property("materials")
    rebuilt = []
    for sm in mats:
        entry = unreal.SkeletalMaterial()
        entry.set_editor_property("material_interface", mi)
        entry.set_editor_property("material_slot_name", sm.material_slot_name)
        rebuilt.append(entry)
    mesh.set_editor_property("materials", rebuilt)
    unreal.EditorAssetLibrary.save_loaded_asset(mesh)

    # Read BACK from the asset. A believed write is not a write.
    fresh = unreal.load_asset("%s/SkeletalMeshes/%s.%s" % (base_dir, n, n))
    got = fresh.get_editor_property("materials")
    slot0 = got[0].material_interface if got else None
    good = bool(slot0) and slot0.get_path_name() == mi.get_path_name()
    if good:
        ok_n += 1
    log.append("%-4s %-30s slot0=%s  base=%s  MR=%s  slots=%d"
               % ("ok" if good else "FAIL", n,
                  slot0.get_name() if slot0 else "<none>",
                  slot0.get_base_material().get_name() if slot0 else "-",
                  mr_state, len(got)))

log.append("")
log.append("wired %d/%d walkers onto %s" % (ok_n, len(found), TARGET_BASE))
with open(OUT, "w") as f:
    f.write("\n".join(log) + "\n")
print("\n".join(log))
