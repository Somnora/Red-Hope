"""Put an edited albedo PNG back onto the texture its asset actually binds.
The return half of the director's paint clinic (rh_paint_kit.py exports).

  RH_NAME=<AssetName> RH_PNG=<edited.png> RH_REPORT=<txt> \
    UnrealEditor-Cmd <proj> -run=pythonscript \
    -script=$PWD/scripts/unreal/rh_paint_return.py -unattended -nosound -stdout

Finds the asset's material instance across the art roots (exact candidates
first, then a case-insensitive sweep of /Game/RedHope/Art), follows its
BaseTex binding, and replace-imports the PNG at that exact package - so every
slot using the texture updates and no binding changes. Keep the PNG at the
resolution rh_paint_kit.py exported.

RECREATED 2026-08-18: the original was written to a session scratchpad and
referenced by commit 1f68ffe's message without ever being added to git.
Round-trip was proven on Electrolyzer (MI_Electrolyzer -> Electrolyzer_texture_0).
"""
import os

import unreal

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()

NAME = os.environ["RH_NAME"]
PNG = os.environ["RH_PNG"]
out = []


def find_mi():
    candidates = [
        "/Game/RedHope/Art/Models2/%s/%s/StaticMeshes/MI_%s.MI_%s" % (NAME, NAME, NAME, NAME),
        "/Game/RedHope/Art/Models/%s/MI_%s.MI_%s" % (NAME, NAME, NAME),
        "/Game/RedHope/Art/Machines/%s/StaticMeshes/MI_%s.MI_%s" % (NAME, NAME, NAME),
        "/Game/RedHope/Art/Props2/%s/Materials/MI_%s.MI_%s" % (NAME, NAME, NAME),
        "/Game/RedHope/Art/Agri/%s/%s/StaticMeshes/MI_%s.MI_%s" % (NAME, NAME, NAME, NAME),
        "/Game/RedHope/Art/CrewAnim/RH_Walker_%s/MI_RH_Walker_%s.MI_RH_Walker_%s" % (NAME, NAME, NAME),
    ]
    for c in candidates:
        mi = unreal.load_asset(c)
        if mi:
            return mi, c
    want = ("mi_%s" % NAME).lower()
    want_walker = ("mi_rh_walker_%s" % NAME).lower()
    for path in EAL.list_assets("/Game/RedHope/Art", recursive=True, include_folder=False):
        base = path.rsplit("/", 1)[-1].split(".")[0].lower()
        if base in (want, want_walker):
            mi = unreal.load_asset(path)
            if mi:
                return mi, path
    return None, None


def main():
    if not os.path.isfile(PNG):
        out.append("** PNG missing: %s **" % PNG)
        return
    mi, mi_path = find_mi()
    if not mi:
        out.append("** no material instance found for %r **" % NAME)
        return
    out.append("mi: %s" % mi_path)
    tex = MEL.get_material_instance_texture_parameter_value(mi, "BaseTex")
    if not tex:
        out.append("** MI has no BaseTex binding - not a master-family instance **")
        return
    tex_path = tex.get_path_name()                      # /pkg/Name.Name
    pkg, obj = tex_path.rsplit(".", 1)
    out.append("BaseTex: %s (%dx%d)" % (tex_path, tex.blueprint_get_size_x(), tex.blueprint_get_size_y()))

    task = unreal.AssetImportTask()
    task.filename = PNG
    task.destination_path = pkg.rsplit("/", 1)[0]
    task.destination_name = obj
    task.automated = True
    task.replace_existing = True
    task.save = True
    AT.import_asset_tasks([task])
    back = unreal.load_asset(tex_path)
    if not back:
        out.append("** replace-import failed **")
        return
    out.append("replaced: %s now %dx%d" % (tex_path, back.blueprint_get_size_x(), back.blueprint_get_size_y()))
    out.append("PAINT_RETURN_OK")


main()
report = os.environ.get("RH_REPORT", "")
text = "\n".join(out) + "\n"
if report:
    open(report, "w").write(text)
print(text)
