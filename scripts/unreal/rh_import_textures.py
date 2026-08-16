"""Import textures OVER existing assets, manifest-driven. Compile-free.

  RH_MANIFEST=<pairs.json> RH_REPORT=<out.txt> \
  UnrealEditor-Cmd <proj> -run=pythonscript \
      -script=$PWD/scripts/unreal/rh_import_textures.py -unattended -nosound -stdout

Manifest is a list of:
  {"png": "/abs/file.png", "dest_path": "/Game/.../Textures",
   "dest_name": "<asset name>", "srgb": true|false}

WHY THIS EXISTS
---------------
`rh_reimport_inplace.py` reimports a STATIC MESH and reports success. It does
NOT bring the source file's textures with it - the texture assets beside the
mesh keep whatever pixels they already had. Measured 2026-08-16 on the crops:
a clean "8/8 ok" mesh reimport left every one of them still wearing an aerial
photograph of a city. New geometry, old paint, and a green log at every step.

So a mesh reimport must ALWAYS be followed by a texture import. This is the
generic form; `rh_import_crop_textures.py` was the Agri-only first cut.

sRGB is ASSERTED per texture rather than inherited, because the importer guesses
from the filename: base colour is COLOUR, and a packed metallic-roughness map is
DATA. Importing an MR map as sRGB silently gamma-shifts roughness and metallic,
and nothing downstream complains - the surface just responds wrongly to light.
"""
import json
import os

import unreal

MANIFEST = os.environ["RH_MANIFEST"]
OUT = os.environ.get("RH_REPORT", "/tmp/rh_import_textures.txt")

pairs = json.load(open(MANIFEST))
tools = unreal.AssetToolsHelpers.get_asset_tools()
log = []
ok_n = 0

for e in pairs:
    png = e["png"]
    dest_path = e["dest_path"]
    dest_name = e["dest_name"]
    srgb = bool(e.get("srgb", True))

    if not os.path.exists(png):
        log.append("MISSING source %s" % png)
        continue

    task = unreal.AssetImportTask()
    task.set_editor_property("filename", png)
    task.set_editor_property("destination_path", dest_path)
    task.set_editor_property("destination_name", dest_name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", True)
    tools.import_asset_tasks([task])

    obj = unreal.load_asset("%s/%s.%s" % (dest_path, dest_name, dest_name))
    if not obj:
        log.append("FAILED  %s/%s" % (dest_path, dest_name))
        continue

    obj.set_editor_property("srgb", srgb)
    if not srgb:
        obj.set_editor_property("compression_settings",
                                unreal.TextureCompressionSettings.TC_MASKS)
    unreal.EditorAssetLibrary.save_loaded_asset(obj)

    ok_n += 1
    log.append("ok    %-58s %dx%d srgb=%s" % (
        dest_name, obj.blueprint_get_size_x(), obj.blueprint_get_size_y(),
        obj.get_editor_property("srgb")))

log.append("")
log.append("imported %d/%d" % (ok_n, len(pairs)))
with open(OUT, "w") as f:
    f.write("\n".join(log) + "\n")
