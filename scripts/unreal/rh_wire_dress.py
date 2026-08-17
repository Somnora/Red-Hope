"""Get the elevators and dress clutter off Interchange's skin shader.

  RH_REPORT=<out.txt> UnrealEditor-Cmd <proj> -run=pythonscript \
      -script=$PWD/scripts/unreal/rh_wire_dress.py -unattended -nosound -stdout

WHY
---
The repo-wide audit on 2026-08-17 (rh_audit_materials.py) found 78 meshes still
on `/InterchangeAssets/gltf/Substrate/M_GLTF`, whose shading model is
MSM_SUBSURFACE_PROFILE. That is the same skin shader that made the crew look
splotchy and see-through. Most of the 78 are unreferenced orphans, but these
five are rendered every session - the player RIDES the elevators - and a steel
crate lit through a subsurface-scattering shader is simply wrong.

`forge` is deliberately excluded even though it is also on M_GLTF and also
live. It is the ORIGINAL Forge (rh.ModelSetV2 0), it carries its colour in
VERTEX data, and RHColonyVisualizerSubsystem applies M_VertexColor to it at
runtime - see the comment at ~line 940. Reparenting it would break that.

The texture STEM differs from the mesh name (RH_crate -> crate_textured), which
is why a name-derived lookup misses these. Hence an explicit table: full paths
only, never a name search across lineages - the Props/Props2 false-done came
from exactly that shortcut.
"""
import os

import unreal

MEL = unreal.MaterialEditingLibrary
OUT = os.environ.get("RH_REPORT", "/tmp/rh_wire_dress.txt")
MASTER = "/Game/RedHope/Art/M_RH_Master.M_RH_Master"

# (mesh dir, mesh name, texture stem)
TARGETS = [
    ("/Game/RedHope/Art/Dress/RH_crate",     "RH_crate",     "crate"),
    ("/Game/RedHope/Art/Dress/RH_drum",      "RH_drum",      "drum"),
    ("/Game/RedHope/Art/Dress/RH_vent",      "RH_vent",      "vent"),
    ("/Game/RedHope/Art/Shaft/RH_Elevator",  "RH_Elevator",  "elevator"),
    ("/Game/RedHope/Art/Machines/RH_Elevator2", "RH_Elevator2", "elevator2"),
]

# The 20 RH_Colonist_<face> statics are the FALLBACK figures - a live code path
# (RHCrewVisualizerSubsystem::ColonistMeshPath, ~line 250) used when the
# skeletal walker is unavailable. They carried the same M_GLTF skin shader, so
# they would have shown the same see-through defect the moment the fallback
# fired. They are characters, so they go on M_RH_Character, not M_RH_Master.
CHARACTER = "/Game/RedHope/Art/M_RH_Character.M_RH_Character"
FACES = ["bot_lindqvist", "cmdr_vale", "comms_diallo", "cook_moreau",
         "driver_costa", "eng_ruiz", "fab_stone", "geo_okafor", "hydro_mensah",
         "med_haddad", "pilot_reyes", "quart_bello", "reactor_ito", "res_novak",
         "rookie_shaw", "safety_abara", "survey_khan", "tech_park",
         "vet_kowalski", "xeno_adeyemi"]

TARGETS = [(d, n, s, MASTER) for d, n, s in TARGETS] + [
    ("/Game/RedHope/Art/Crew/RH_Colonist_%s" % f, "RH_Colonist_%s" % f, f, CHARACTER)
    for f in FACES
]

tools = unreal.AssetToolsHelpers.get_asset_tools()
log = []
ok_n = 0

for base_dir, name, stem, master_path in TARGETS:
    master = unreal.load_asset(master_path)
    if not master:
        log.append("MISSING master %s" % master_path)
        continue
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
        log.append("FAILED  MI for %s" % name)
        continue
    mi.set_editor_property("parent", master)

    albedo = unreal.load_asset("%s/Textures/%s_textured.%s_textured"
                               % (base_dir, stem, stem))
    if albedo:
        MEL.set_material_instance_texture_parameter_value(mi, "BaseTex", albedo)

    mr_name = "%s_textured_metallic-%s_textured_roughness" % (stem, stem)
    mr = unreal.load_asset("%s/Textures/%s.%s" % (base_dir, mr_name, mr_name))
    if mr:
        # Same trap as the crew: a packed MR map imported as TC_DEFAULT puts
        # block-compression artifacts straight into roughness.
        if mr.get_editor_property("compression_settings") != \
                unreal.TextureCompressionSettings.TC_MASKS:
            mr.set_editor_property("compression_settings",
                                   unreal.TextureCompressionSettings.TC_MASKS)
            mr.set_editor_property("srgb", False)
            unreal.EditorAssetLibrary.save_loaded_asset(mr)
        MEL.set_material_instance_texture_parameter_value(mi, "MRTex", mr)
    unreal.EditorAssetLibrary.save_loaded_asset(mi)

    # Rebuild the slot array; the getter returns COPIES.
    rebuilt = []
    for sm in mesh.get_editor_property("static_materials"):
        entry = unreal.StaticMaterial()
        entry.set_editor_property("material_interface", mi)
        entry.set_editor_property("material_slot_name", sm.material_slot_name)
        rebuilt.append(entry)
    mesh.set_editor_property("static_materials", rebuilt)
    unreal.EditorAssetLibrary.save_loaded_asset(mesh)

    # Read BACK from the asset. A believed write is not a write.
    fresh = unreal.load_asset("%s/StaticMeshes/%s.%s" % (base_dir, name, name))
    got = fresh.get_editor_property("static_materials")
    slot0 = got[0].material_interface if got else None
    base = slot0.get_base_material() if slot0 else None
    good = bool(base) and base.get_name() == master.get_name()
    if good:
        ok_n += 1
    log.append("%-4s %-14s slot0=%s base=%s albedo=%s MR=%s"
               % ("ok" if good else "FAIL", name,
                  slot0.get_name() if slot0 else "<none>",
                  base.get_name() if base else "-",
                  "yes" if albedo else "NO", "yes" if mr else "NO"))

log.append("")
log.append("wired %d/%d onto their project master" % (ok_n, len(TARGETS)))
with open(OUT, "w") as f:
    f.write("\n".join(log) + "\n")
print("\n".join(log))
