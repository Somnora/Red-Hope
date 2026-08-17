"""Set the ambient-motion parameters per building instance. Compile-free.

    UnrealEditor-Cmd <proj> -run=pythonscript \
      -script=$PWD/scripts/unreal/rh_tune_motion.py -unattended -nosound -stdout

Ownership boundary, so nothing fights: RHArtWireCommandlet owns the *static*
instance parameters (BaseTex, AccentColor, AccentAmount, Metallic, Rough,
EmissiveColor, EmissiveAmount). This script owns PulseDepth/PulseSpeed and
nothing else.

Only machines that RUN get a pulse. Structures (habitat, lander, stockpile) stay
still - a breathing warehouse reads as a bug, not as life. The pulse multiplies
through PoweredState in M_RH_Master, so a shed or switched-off machine stops
moving and goes dark on its own, with no tick and no components.
"""
import os

import unreal

MEL = unreal.MaterialEditingLibrary
OUT = os.environ.get("RH_REPORT", "/tmp/rh_motion.txt")

# instance path -> (depth, speed). Depth is the fraction the glow dips at its
# lowest; speed is roughly radians/second, so ~2.0 is a two-second breath.
TUNING = {
    "/Game/RedHope/Art/Models2/HeavyForge/HeavyForge/StaticMeshes/MI_HeavyForge": (0.35, 1.4),
    "/Game/RedHope/Art/Models2/CommandModule/CommandModule/StaticMeshes/MI_CommandModule": (0.30, 4.0),
    "/Game/RedHope/Art/Models/battery/MI_battery": (0.28, 2.6),
    "/Game/RedHope/Art/Models/ice/MI_ice": (0.22, 1.8),
    "/Game/RedHope/Art/Models/extractor2/MI_extractor2": (0.25, 2.2),
    "/Game/RedHope/Art/Machines/RH_AirFilter2/StaticMeshes/MI_RH_AirFilter2": (0.22, 2.0),
    "/Game/RedHope/Art/Agri/humidity/humidity/StaticMeshes/MI_humidity": (0.18, 1.6),
    # deliberately still:
    "/Game/RedHope/Art/Models2/HabitatDome/HabitatDome/StaticMeshes/MI_HabitatDome": (0.0, 2.0),
    "/Game/RedHope/Art/Models2/SolarPanel/SolarPanel/StaticMeshes/MI_SolarPanel": (0.0, 2.0),
    "/Game/RedHope/Art/Models/lander2/MI_lander2": (0.0, 2.0),
    "/Game/RedHope/Art/Models/stockpile/MI_stockpile": (0.0, 2.0),
}

log = []
ok = 0
for path, (depth, speed) in sorted(TUNING.items()):
    mi = unreal.load_asset(path)
    name = path.rsplit("/", 1)[-1]
    if not mi:
        log.append("MISSING %s" % path)
        continue
    try:
        MEL.set_material_instance_scalar_parameter_value(mi, "PulseDepth", depth)
        MEL.set_material_instance_scalar_parameter_value(mi, "PulseSpeed", speed)
        MEL.update_material_instance(mi)
        unreal.EditorAssetLibrary.save_loaded_asset(mi, only_if_is_dirty=False)
        ok += 1
        log.append("%-26s depth=%.2f speed=%.1f%s" % (name, depth, speed, "  (still)" if depth == 0 else ""))
    except Exception as e:
        log.append("FAILED %s : %s" % (name, e))

log.insert(0, "tuned %d/%d instances" % (ok, len(TUNING)))
with open(OUT, "w") as fh:
    fh.write("\n".join(log) + "\n")
