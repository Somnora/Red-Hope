"""Import Red Hope's sounds (scripts/audio/rh_sfx_build.py, plus Lyria music)
as SoundWaves.

  RH_AUDIO=<abs dir holding manifest.json> RH_REPORT=<txt> \
    UnrealEditor-Cmd <proj> -run=pythonscript \
    -script=$PWD/scripts/unreal/rh_import_audio.py -unattended -nosound -stdout

Each manifest row {category, name, file, loop} lands as
/Game/RedHope/Audio/<category>/A_<name> (replaced in place on re-import),
with looping set from the manifest. URHAudioSubsystem loads them by exactly
these paths. Reads every asset back after saving and reports any path, loop
flag or duration that does not match.
"""
import json
import os

import unreal

EAL = unreal.EditorAssetLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()
SRC = os.environ["RH_AUDIO"]
REPORT = os.environ.get("RH_REPORT", "/tmp/rh_import_audio.txt")
log = []


def main():
    rows = json.load(open(os.path.join(SRC, "manifest.json")))
    tasks = []
    for r in rows:
        t = unreal.AssetImportTask()
        t.filename = r["file"]
        t.destination_path = "/Game/RedHope/Audio/%s" % r["category"]
        t.destination_name = "A_%s" % r["name"]
        t.automated = True
        t.replace_existing = True
        t.save = True
        tasks.append(t)
    AT.import_asset_tasks(tasks)
    ok = 0
    for r in rows:
        path = "/Game/RedHope/Audio/%s/A_%s" % (r["category"], r["name"])
        w = unreal.load_asset(path)
        if not isinstance(w, unreal.SoundWave):
            log.append("** %s: not imported (%s) **" % (path, type(w).__name__ if w else None))
            continue
        w.set_editor_property("looping", bool(r["loop"]))
        EAL.save_loaded_asset(w)
        dur = w.get_editor_property("duration")
        good = abs(dur - r["seconds"]) < 0.05 and w.get_editor_property("looping") == bool(r["loop"])
        ok += good
        log.append("%s %s  %.2fs  loop=%s%s" % ("ok " if good else "** ", path, dur, w.get_editor_property("looping"),
                                                "" if good else "  (expected %.2fs loop=%s)" % (r["seconds"], r["loop"])))
    log.append("DONE %d/%d sounds" % (ok, len(rows)))


main()
with open(REPORT, "w") as f:
    f.write("\n".join(log) + "\n")
