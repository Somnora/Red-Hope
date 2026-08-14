"""Set EmissiveAmount on every MI_* instance. Compile-free, and reversible.

  RH_EMISSIVE=0.0 UnrealEditor-Cmd <proj> -run=pythonscript \
      -script=$PWD/scripts/unreal/rh_tune_emissive.py -unattended -nosound -stdout

  # put the authored per-model values back:
  git checkout HEAD -- Content/

WHY THIS EXISTS: M_RH_Master adds EmissiveColor * EmissiveAmount to the WHOLE
surface. There is no mask. The commandlet documents the parameter as "lit area
strength", so a mask was clearly intended and never built, and the glow colours
are HDR (FurnaceGlow is 6.0, 1.6, 0.15). HeavyForge at EmissiveAmount 0.22
therefore emits ~1.3 orange across every square centimetre of the mesh, which
buries the albedo completely: the forge renders as a flat saturated blob with no
panel lines, no hazard chevrons, no weathering.

Measured on the showcase grid at identical camera and sun: at 0.22 the forge is
an orange silhouette; at 0.0 the same mesh and the same texture show rust-red
panels, yellow-black chevrons, a weathered chimney and bolt detail. Every room
prop was authored at 0.0, which is exactly why the interiors already looked
finished while the machines did not.

The real fix is a mask so only windows, furnace mouths and indicator strips
emit. Until then this script is the blunt knob for judging the meshes without
the glow on top. Note 0.0 also switches off the powered/pulse read, which rides
the same term - that trade is the director's call, not this script's.
"""
import os
import unreal

MEL = unreal.MaterialEditingLibrary
OUT = os.environ.get("RH_REPORT", "/tmp/rh_emissive.txt")
VAL = float(os.environ.get("RH_EMISSIVE", "0.0"))

log = []
registry = unreal.AssetRegistryHelpers.get_asset_registry()
for asset in registry.get_assets_by_path("/Game/RedHope/Art", recursive=True):
    name = str(asset.asset_name)
    if not name.startswith("MI_"):
        continue
    mi = unreal.load_asset("%s.%s" % (str(asset.package_name), name))
    if not isinstance(mi, unreal.MaterialInstanceConstant):
        continue
    before = MEL.get_material_instance_scalar_parameter_value(mi, "EmissiveAmount")
    if abs(before - VAL) < 1e-6:
        continue
    MEL.set_material_instance_scalar_parameter_value(mi, "EmissiveAmount", VAL)
    unreal.EditorAssetLibrary.save_loaded_asset(mi)
    log.append("%-24s EmissiveAmount %.3f -> %.3f" % (name, before, VAL))

log.append("changed %d instance(s) to %.3f" % (len(log), VAL))
with open(OUT, "w") as f:
    f.write("\n".join(log) + "\n")
