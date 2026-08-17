"""Flip the derived-normal / per-pixel-MR paths on every wired MI at once.

    RH_DETAIL=0|1 RH_REPORT=<txt> UnrealEditor-Cmd <proj> -run=pythonscript \
      -script=$PWD/scripts/unreal/rh_toggle_surface_detail.py -unattended -nosound -stdout

Exists to make an HONEST A/B possible: same camera, same sun, same frame, one
variable. Comparing today's build against an archived screenshot is how I
previously "proved" a lighting bug that was actually my own render setup - the
only comparison worth trusting changes exactly one thing.

RH_DETAIL=1 restores the authored strengths (models 0.50, crew 0.35 - tuned
2026-08-17 on an isolated render ladder, see the pipeline guide); 0 turns
both paths off, which returns the exact pre-2026-08-17 look because the masters
lerp to a flat normal and the scalar roughness at 0.
"""
import os

import unreal

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary

ON = os.environ.get("RH_DETAIL", "1") == "1"
log = []
touched = 0

for a in EAL.list_assets("/Game/RedHope/Art", recursive=True):
    leaf = a.rstrip("/").split("/")[-1].split(".")[0]
    if not leaf.startswith("MI_"):
        continue
    mi = unreal.load_asset(a)
    if not isinstance(mi, unreal.MaterialInstanceConstant):
        continue
    # Only touch instances that actually carry a derived normal - never invent
    # detail on something that was never wired.
    try:
        tex = MEL.get_material_instance_texture_parameter_value(mi, "NormTex")
    except Exception:
        continue
    if tex is None or "_Normal" not in tex.get_name():
        continue
    is_crew = "RH_Walker_" in leaf
    n_val = (0.35 if is_crew else 0.5) if ON else 0.0
    MEL.set_material_instance_scalar_parameter_value(mi, "UseNormTex", n_val)
    if is_crew:
        MEL.set_material_instance_scalar_parameter_value(mi, "UseMRTex", 1.0 if ON else 0.0)
    EAL.save_loaded_asset(mi)
    back = MEL.get_material_instance_scalar_parameter_value(mi, "UseNormTex")
    if abs(back - n_val) > 1e-3:
        log.append("%-40s FAIL wanted %.2f got %.2f" % (leaf, n_val, back))
    touched += 1

log.append("detail %s on %d instances" % ("ON" if ON else "OFF", touched))
open(os.environ.get("RH_REPORT", "/tmp/rh_detail.txt"), "w").write("\n".join(log) + "\n")
