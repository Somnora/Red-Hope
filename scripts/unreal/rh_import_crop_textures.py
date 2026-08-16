"""Import crop albedo + metallic-roughness textures OVER the existing assets.

  RH_TEXDIR=<dir of PNGs> RH_REPORT=<out.txt> \
  UnrealEditor-Cmd <proj> -run=pythonscript \
      -script=$PWD/scripts/unreal/rh_import_crop_textures.py -unattended -nosound -stdout

WHY THIS EXISTS - the trap it closes
------------------------------------
`rh_reimport_inplace.py` reimports a STATIC MESH in place, and reports 8/8 ok.
It does NOT bring the GLB's textures with it. The texture assets beside the mesh
keep whatever pixels they already had.

Measured on 2026-08-16, after a clean 8/8 mesh reimport: the UE texture was
1024x1024 with mean RGB 88.9/87.4/83.7 while the source GLB carried 2048x2048 at
138.1/126.7/112.3. Exported and looked at, the UE one was still the original
AERIAL VIEW OF A CITY - roads, rooftops, parking lots - i.e. the very defect the
regeneration existed to remove. New geometry, old wrong paint, and every log line
said OK.

The lesson is the project's own: a stage that reports success for the thing it
actually did will happily let you believe it did the thing you meant.

sRGB is the one setting that must be asserted rather than inherited: albedo is
colour, the packed metallic-roughness map is DATA, and importing an MR map as
sRGB silently gamma-shifts roughness and metallic. The importer guesses from the
filename and gets it wrong here, because the name says "metallic" and "roughness"
rather than anything it recognises.
"""
import os

import unreal

TEXDIR = os.environ["RH_TEXDIR"]
OUT = os.environ.get("RH_REPORT", "/tmp/rh_crop_tex.txt")

NAMES = [
    "crop_root_1", "crop_root_2", "crop_root_3",
    "crop_tall_1", "crop_tall_2", "crop_tall_3",
    "crop_vine_1", "crop_vine_2", "crop_vine_3",
]
SKIP = {s for s in os.environ.get("RH_SKIP", "").split(",") if s}

tools = unreal.AssetToolsHelpers.get_asset_tools()
log = []
ok_n = 0
want_n = 0


def do_import(png, dest_path, dest_name, srgb):
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
        return None
    # assert, never inherit
    obj.set_editor_property("srgb", srgb)
    if not srgb:
        obj.set_editor_property("compression_settings",
                                unreal.TextureCompressionSettings.TC_MASKS)
    unreal.EditorAssetLibrary.save_loaded_asset(obj)
    return obj


for name in NAMES:
    if name in SKIP:
        log.append("skip  %s (RH_SKIP)" % name)
        continue
    base = "/Game/RedHope/Art/Agri/%s/%s/Textures" % (name, name)
    alb_name = "%s_textured" % name
    mr_name = "%s_textured_metallic-%s_textured_roughness" % (name, name)
    alb_png = os.path.join(TEXDIR, alb_name + ".png")
    mr_png = os.path.join(TEXDIR, mr_name + ".png")

    for png, dest_name, srgb in ((alb_png, alb_name, True), (mr_png, mr_name, False)):
        want_n += 1
        if not os.path.exists(png):
            log.append("MISSING source %s" % png)
            continue
        obj = do_import(png, base, dest_name, srgb)
        if not obj:
            log.append("FAILED  %s" % dest_name)
            continue
        ok_n += 1
        log.append("ok    %-58s %dx%d srgb=%s" % (
            dest_name, obj.blueprint_get_size_x(), obj.blueprint_get_size_y(),
            obj.get_editor_property("srgb")))

log.append("")
log.append("imported %d/%d" % (ok_n, want_n))
with open(OUT, "w") as f:
    f.write("\n".join(log) + "\n")
