"""Assign each MI its emissive mask and the post-mask glow strength.

  UnrealEditor-Cmd <proj> -run=pythonscript \
      -script=$PWD/scripts/unreal/rh_assign_masks.py -unattended -nosound -stdout

Why the amounts change here: pre-mask, the glow covered the whole hull, so the
wire pass kept EmissiveAmount tiny (0.08-0.22) and it STILL washed out the
albedo (the W2 finding). Masked, the lit area is 0.04-5% of the surface, so the
same numbers would be invisible. These values relight the small regions to read
as working lights: night-readable, day-subtle - the director's stated
philosophy. Dark models get amount 0 on top of their black mask, belt and
braces.

Re-runnable; edit AMOUNTS and re-run to retune.
"""
import os
import unreal

MEL = unreal.MaterialEditingLibrary
OUT = os.environ.get("RH_REPORT", "/tmp/rh_assign.txt")
MASKS = "/Game/RedHope/Art/Masks"

# MI name -> (mask texture name, post-mask EmissiveAmount)
AMOUNTS = {
    "MI_HeavyForge":    ("T_HeavyForge_EmissiveMask",    2.2),
    "MI_battery":       ("T_battery_EmissiveMask",       1.6),
    "MI_CommandModule": ("T_CommandModule_EmissiveMask", 1.8),
    "MI_ice":           ("T_ice_EmissiveMask",           0.9),
    "MI_extractor2":    ("T_extractor2_EmissiveMask",    1.5),
    "MI_RH_AirFilter2": ("T_airfilter2_EmissiveMask",    1.5),
    "MI_humidity":      ("T_humidity_EmissiveMask",      1.3),
    "MI_HabitatDome":   ("T_HabitatDome_EmissiveMask",   1.0),
    "MI_SolarPanel":    ("T_SolarPanel_EmissiveMask",    0.0),
    "MI_lander2":       ("T_lander2_EmissiveMask",       0.0),
    "MI_stockpile":     ("T_stockpile_EmissiveMask",     0.0),
}

# Room props (Props2). These were authored with EmissiveAmount 0 AND a black
# EmissiveColor, so a mask alone would still render nothing - the colour has to
# be set too. Screens read cool; the galley reads warm because it is a kitchen.
# Amounts stay low: these are seen from a metre away under the hab ceiling
# lights, not across a night colony, so they only need to look powered.
SCREEN = unreal.LinearColor(0.25, 2.20, 2.80, 1.0)
WARM = unreal.LinearColor(2.60, 1.45, 0.35, 1.0)
PROPS = {
    "MI_bunk":        ("T_bunk_EmissiveMask",        0.9, SCREEN),
    "MI_conduit":     ("T_conduit_EmissiveMask",     0.8, SCREEN),
    "MI_console":     ("T_console_EmissiveMask",     1.4, SCREEN),
    "MI_diningtable": ("T_diningtable_EmissiveMask", 0.8, SCREEN),
    "MI_galley":      ("T_galley_EmissiveMask",      1.0, WARM),
    "MI_labbench":    ("T_labbench_EmissiveMask",    1.1, SCREEN),
    "MI_locker":      ("T_locker_EmissiveMask",      0.6, SCREEN),
    "MI_planter_dry": ("T_planter_dry_EmissiveMask", 0.8, SCREEN),
    "MI_planter_wet": ("T_planter_wet_EmissiveMask", 0.8, SCREEN),
    "MI_tank":        ("T_tank_EmissiveMask",        0.9, SCREEN),
}

log = []
registry = unreal.AssetRegistryHelpers.get_asset_registry()
found = {}
for asset in registry.get_assets_by_path("/Game/RedHope/Art", recursive=True):
    name = str(asset.asset_name)
    if (name in AMOUNTS or name in PROPS) and name not in found:
        found[name] = "%s.%s" % (str(asset.package_name), name)

for name, (mask_name, amount) in AMOUNTS.items():
    path = found.get(name)
    mi = unreal.load_asset(path) if path else None
    if not isinstance(mi, unreal.MaterialInstanceConstant):
        log.append("MISSING %s" % name)
        continue
    mask = unreal.load_asset("%s/%s.%s" % (MASKS, mask_name, mask_name))
    if not mask:
        log.append("MISSING MASK %s" % mask_name)
        continue
    MEL.set_material_instance_texture_parameter_value(mi, "EmissiveMask", mask)
    MEL.set_material_instance_scalar_parameter_value(mi, "EmissiveAmount", amount)
    unreal.EditorAssetLibrary.save_loaded_asset(mi)
    log.append("ok %-18s mask=%-30s amount=%.2f" % (name, mask_name, amount))

for name, (mask_name, amount, colour) in PROPS.items():
    path = found.get(name)
    mi = unreal.load_asset(path) if path else None
    if not isinstance(mi, unreal.MaterialInstanceConstant):
        log.append("MISSING %s" % name)
        continue
    # The props shipped parented to M_ModelTex, which has no EmissiveMask - so a
    # mask assigned there would silently do nothing. M_RH_Master is a strict
    # SUPERSET of M_ModelTex (BaseTex, Rough, EmissiveFloor, plus Metallic,
    # Accent* and the emissive chain; neither material has a normal map), and
    # instance overrides bind by NAME, so BaseTex and Rough survive the
    # reparent untouched and AccentAmount defaults to 0 = no tint change.
    # This also finally puts the room props in the same material family as the
    # machines, which the art bible always claimed they were in.
    parent = mi.get_editor_property("parent")
    pname = parent.get_name() if parent else "<none>"
    if pname == "M_ModelTex":
        master = unreal.load_asset("/Game/RedHope/Art/M_RH_Master.M_RH_Master")
        if not master:
            log.append("SKIP %-16s M_RH_Master missing" % name)
            continue
        mi.set_editor_property("parent", master)
        pname = "M_RH_Master (reparented)"
    elif pname != "M_RH_Master":
        log.append("SKIP %-16s parent is %s, not M_RH_Master" % (name, pname))
        continue
    mask = unreal.load_asset("%s/%s.%s" % (MASKS, mask_name, mask_name))
    if not mask:
        log.append("MISSING MASK %s" % mask_name)
        continue
    MEL.set_material_instance_texture_parameter_value(mi, "EmissiveMask", mask)
    MEL.set_material_instance_scalar_parameter_value(mi, "EmissiveAmount", amount)
    MEL.set_material_instance_vector_parameter_value(mi, "EmissiveColor", colour)
    unreal.EditorAssetLibrary.save_loaded_asset(mi)
    # Verify the albedo survived: if BaseTex came back empty the prop would
    # render as the parent's default (the regolith), which is a loud regression.
    base = MEL.get_material_instance_texture_parameter_value(mi, "BaseTex")
    log.append("ok %-16s mask=%-28s amount=%.2f  parent=%-24s BaseTex=%s" % (
        name, mask_name, amount, pname, base.get_name() if base else "!! NONE !!"))

with open(OUT, "w") as f:
    f.write("\n".join(log) + "\n")
