"""Import a code-built (scripts/blender/rh_hs.py kit) asset in ONE commandlet run.

  RH_NAME=BatteryBank RH_DIR=<abs dir holding <Name>.glb + PNGs> \
  RH_ACCENT="0.10,0.85,0.65" RH_GLOW="0.05,2.2,1.56" RH_GLOW_AMT=1.6 RH_REPORT=<txt> \
    UnrealEditor-Cmd <proj> -run=pythonscript \
    -script=$PWD/scripts/unreal/rh_import_hs.py -unattended -nosound -stdout

Chains the proven lane instead of re-implementing it:
  1. rh_import_model2.py  mesh + BaseColor + MR into Models2/<Name> (replace in
                          place, same /Game path, so ModelPathsV2 needs no edit),
                          MI_<Name> on M_RH_Master, flat normal.
                          AccentAmount is forced to 0: the kit paints the accent
                          as TRIM into BaseColor, and M_RH_Master has no accent
                          mask, so any non-zero AccentAmount tints the WHOLE hull
                          (the "toy" look the art bible forbids).
  2. rh_import_masks.py   <Name>_EmissiveMask.png -> Masks/T_<Name>_EmissiveMask
  3. here                 MI EmissiveMask / EmissiveColor / EmissiveAmount, the
                          BaseTex/MRTex pairing checked BY NAME (step 1 picks
                          textures by sort order among everything in the folder,
                          including the replaced asset's leftovers), Nanite off.

Verify from a FRESH process afterwards (rh_mesh_stats.py) - same-process
readback only proves the in-memory object (the 08-18 lesson).
"""
import json
import os
import runpy

import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
NAME = os.environ["RH_NAME"]
SRC = os.environ["RH_DIR"]
REPORT = os.environ.get("RH_REPORT", "/tmp/rh_import_hs.txt")
GLOW = tuple(float(x) for x in os.environ.get("RH_GLOW", "0.05,2.2,1.56").split(","))
GLOW_AMT = float(os.environ.get("RH_GLOW_AMT", "1.6"))
MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
log = []


def step(script, env, report):
    os.environ.update(env)
    os.environ["RH_REPORT"] = report
    runpy.run_path(os.path.join(HERE, script), run_name="__main__")
    lines = open(report).read().splitlines() if os.path.exists(report) else ["(no report)"]
    log.extend("[%s] %s" % (script, l) for l in lines)
    return lines


def main():
    m2 = step("rh_import_model2.py", {
        "RH_GLB": os.path.join(SRC, NAME + ".glb"),
        "RH_NRM": os.path.join(SRC, NAME + "_flat_normal.png"),
        "RH_ACCENT_AMT": "0.0",
    }, REPORT + ".model2")
    cpp = [l.split(" ", 1)[1] for l in m2 if l.startswith("CPP_PATH ")]
    if not cpp:
        log.append("** model2 step did not confirm the mesh - stopping **")
        return
    mesh_path = cpp[0]

    manifest = REPORT + ".masks.json"
    json.dump([{"png": os.path.join(SRC, NAME + "_EmissiveMask.png"),
                "dest": "/Game/RedHope/Art/Masks",
                "name": "T_%s_EmissiveMask" % NAME}], open(manifest, "w"))
    step("rh_import_masks.py", {"RH_MANIFEST": manifest}, REPORT + ".masks")

    mesh = unreal.load_asset(mesh_path)
    folder = mesh_path.rsplit("/", 1)[0]
    mi = unreal.load_asset("%s/MI_%s.MI_%s" % (folder, NAME, NAME))
    if not (mesh and mi):
        log.append("** mesh or MI missing after import: %s %s **" % (mesh, mi))
        return

    # BaseTex / MRTex must be THIS build's textures, chosen by name.
    tex_dir = folder.rsplit("/", 1)[0] + "/Textures"
    want = {"BaseTex": "%s_BaseColor" % NAME, "MRTex": "%s_MetallicRoughness" % NAME}
    for param, leaf in want.items():
        cur = MEL.get_material_instance_texture_parameter_value(mi, param)
        if cur and cur.get_name() == leaf:
            log.append("%s = %s (ok)" % (param, leaf))
            continue
        tex = unreal.load_asset("%s/%s.%s" % (tex_dir, leaf, leaf))
        if not tex:
            log.append("** %s: expected texture %s/%s not found **" % (param, tex_dir, leaf))
            return
        MEL.set_material_instance_texture_parameter_value(mi, param, tex)
        log.append("%s rewired %s -> %s" % (param, cur.get_name() if cur else None, leaf))

    mask = unreal.load_asset("/Game/RedHope/Art/Masks/T_%s_EmissiveMask.T_%s_EmissiveMask" % (NAME, NAME))
    if not mask:
        log.append("** emissive mask missing **")
        return
    MEL.set_material_instance_texture_parameter_value(mi, "EmissiveMask", mask)
    MEL.set_material_instance_vector_parameter_value(mi, "EmissiveColor", unreal.LinearColor(*GLOW, 1.0))
    MEL.set_material_instance_scalar_parameter_value(mi, "EmissiveAmount", GLOW_AMT)
    MEL.set_material_instance_scalar_parameter_value(mi, "AccentAmount", 0.0)
    EAL.save_loaded_asset(mi)
    log.append("MI_%s: EmissiveMask=%s EmissiveColor=%s EmissiveAmount=%.2f AccentAmount=0"
               % (NAME, mask.get_name(), GLOW, GLOW_AMT))

    ns = mesh.get_editor_property("nanite_settings")
    if ns.get_editor_property("enabled"):
        ns.set_editor_property("enabled", False)
        mesh.set_editor_property("nanite_settings", ns)
        mesh.modify()
        EAL.save_loaded_asset(mesh, only_if_is_dirty=False)
        log.append("nanite disabled on %s" % mesh_path)
    else:
        log.append("nanite already off")
    # (EditorStaticMeshLibrary.get_number_triangles is gone in 5.8)
    log.append("tris now %d, verts %d" % (mesh.get_num_triangles(0), mesh.get_num_vertices(0)))
    log.append("DONE %s" % mesh_path)


main()
with open(REPORT, "w") as f:
    f.write("\n".join(log) + "\n")
