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

log = []
registry = unreal.AssetRegistryHelpers.get_asset_registry()
found = {}
for asset in registry.get_assets_by_path("/Game/RedHope/Art", recursive=True):
    name = str(asset.asset_name)
    if name in AMOUNTS and name not in found:
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

with open(OUT, "w") as f:
    f.write("\n".join(log) + "\n")
