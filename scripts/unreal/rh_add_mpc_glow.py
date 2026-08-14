"""Add the GlowScale scalar to MPC_Atmosphere. Compile-free, idempotent.

  UnrealEditor-Cmd <proj> -run=pythonscript \
      -script=$PWD/scripts/unreal/rh_add_mpc_glow.py -unattended -nosound -stdout

GlowScale is the global dial behind `rh.Glow`: the master material multiplies
its MASKED emissive by it, so 0 kills the machine-glow assist entirely and the
night is carried only by lights the player actually built (per-cell hab ceiling
lights and Floodmasts, which are real point lights and are NOT affected).

Default 1.0 means adding the parameter changes nothing until the CVar drives it.
The atmosphere subsystem already writes this collection every tick (Habitability,
TimeOfSol, Dust), so pushing one more scalar is free.
"""
import os
import unreal

MPC = "/Game/RedHope/Sky/MPC_Atmosphere.MPC_Atmosphere"
OUT = os.environ.get("RH_REPORT", "/tmp/rh_mpc.txt")
NAME = "GlowScale"
DEFAULT = 1.0

log = []
mpc = unreal.load_asset(MPC)
if not mpc:
    log.append("MISSING %s" % MPC)
else:
    scalars = list(mpc.get_editor_property("scalar_parameters"))
    log.append("before: %s" % [str(p.get_editor_property("parameter_name")) for p in scalars])

    if any(str(p.get_editor_property("parameter_name")) == NAME for p in scalars):
        log.append("%s already present - no change" % NAME)
    else:
        entry = unreal.CollectionScalarParameter()
        entry.set_editor_property("parameter_name", NAME)
        entry.set_editor_property("default_value", DEFAULT)
        scalars.append(entry)
        mpc.set_editor_property("scalar_parameters", scalars)
        unreal.EditorAssetLibrary.save_loaded_asset(mpc)
        log.append("added %s (default %.2f)" % (NAME, DEFAULT))

    after = mpc.get_editor_property("scalar_parameters")
    log.append("after:  %s" % [str(p.get_editor_property("parameter_name")) for p in after])

with open(OUT, "w") as fh:
    fh.write("\n".join(log) + "\n")
