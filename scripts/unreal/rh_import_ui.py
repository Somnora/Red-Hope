"""Import the deck's UI images as UI textures: the icon set
(scripts/ui/rh_icons_build.py) and the build menu's thumbnails
(scripts/ui/rh_thumbs_compose.py).

  RH_ICONS=<abs dir of PNGs> [RH_DEST=/Game/RedHope/UI/Thumbs RH_PREFIX=T_Thumb_] \
    RH_REPORT=<txt> UnrealEditor-Cmd <proj> -run=pythonscript \
    -script=$PWD/scripts/unreal/rh_import_ui.py -unattended -nosound -stdout

Each <RH_PREFIX><name>.png lands as <RH_DEST>/<RH_PREFIX><name> (defaults:
/Game/RedHope/UI/Icons, T_Icon_), set up for Slate:
UserInterface2D compression (uncompressed RGBA, crisp edges), the UI texture
group, no mips, sRGB. RHDeckStyle loads them by these paths.
"""
import os

import unreal

EAL = unreal.EditorAssetLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()
SRC = os.environ["RH_ICONS"]
REPORT = os.environ.get("RH_REPORT", "/tmp/rh_import_ui.txt")
DEST = os.environ.get("RH_DEST", "/Game/RedHope/UI/Icons")
PREFIX = os.environ.get("RH_PREFIX", "T_Icon_")
log = []

pngs = sorted(f for f in os.listdir(SRC) if f.startswith(PREFIX) and f.endswith(".png"))
tasks = []
for f in pngs:
    t = unreal.AssetImportTask()
    t.filename = os.path.join(SRC, f)
    t.destination_path = DEST
    t.automated = True
    t.replace_existing = True
    t.save = True
    tasks.append(t)
AT.import_asset_tasks(tasks)
ok = 0
for f in pngs:
    name = f[:-4]
    tex = unreal.load_asset("%s/%s" % (DEST, name))
    if not isinstance(tex, unreal.Texture2D):
        log.append("** %s not imported **" % name)
        continue
    tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
    tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    tex.set_editor_property("srgb", True)
    EAL.save_loaded_asset(tex)
    ok += 1
    log.append("ok %s/%s %dx%d" % (DEST, name, tex.blueprint_get_size_x(), tex.blueprint_get_size_y()))
log.append("DONE %d/%d images" % (ok, len(pngs)))
with open(REPORT, "w") as fh:
    fh.write("\n".join(log) + "\n")
