"""Import grayscale mask PNGs as linear TC_Grayscale textures. Compile-free.

  RH_MANIFEST=masks.json RH_REPORT=/tmp/r.txt UnrealEditor-Cmd <proj> \
      -run=pythonscript -script=$PWD/scripts/unreal/rh_import_masks.py \
      -unattended -nosound -stdout

  masks.json: [{"png": "/abs/path.png", "dest": "/Game/RedHope/Art/Masks", "name": "T_x_EmissiveMask"}, ...]

Masks are DATA, not colour: srgb off, TC_Grayscale, no mip bias games. The
sampler in M_RH_Master is LinearGrayscale, and a mismatched sampler type is a
material compile error, so the settings here and the sampler there move
together.

Re-runnable: an existing asset at the destination is reimported in place via
the same task (replace_existing), so iterating on a mask is just re-running
with the regenerated PNG.
"""
import json
import os
import unreal

MANIFEST = os.environ.get("RH_MANIFEST", "")
OUT = os.environ.get("RH_REPORT", "/tmp/rh_masks.txt")
log = []

tools = unreal.AssetToolsHelpers.get_asset_tools()
with open(MANIFEST) as f:
    rows = json.load(f)

for row in rows:
    task = unreal.AssetImportTask()
    task.filename = row["png"]
    task.destination_path = row["dest"]
    task.destination_name = row["name"]
    task.automated = True
    task.replace_existing = True
    task.save = False  # saved after the settings below, so one write per asset
    tools.import_asset_tasks([task])

    path = "%s/%s.%s" % (row["dest"], row["name"], row["name"])
    tex = unreal.load_asset(path)
    if not tex:
        # Interchange SubPath machinery may have nested it; find and relocate.
        found = None
        registry = unreal.AssetRegistryHelpers.get_asset_registry()
        for a in registry.get_assets_by_path(row["dest"], recursive=True):
            if str(a.asset_name) == row["name"]:
                found = str(a.package_name)
                break
        if found and found != "%s/%s" % (row["dest"], row["name"]):
            unreal.EditorAssetLibrary.rename_asset(
                "%s.%s" % (found, row["name"]), path)
            tex = unreal.load_asset(path)
            log.append("  relocated from %s" % found)
    if not tex:
        log.append("FAILED %s (no asset landed)" % row["name"])
        continue

    tex.set_editor_property("srgb", False)
    tex.set_editor_property("compression_settings",
                            unreal.TextureCompressionSettings.TC_GRAYSCALE)
    unreal.EditorAssetLibrary.save_loaded_asset(tex)
    log.append("ok %-28s -> %s  (%dx%d, srgb off, grayscale)" % (
        row["name"], path, tex.blueprint_get_size_x(), tex.blueprint_get_size_y()))

with open(OUT, "w") as f:
    f.write("\n".join(log) + "\n")
