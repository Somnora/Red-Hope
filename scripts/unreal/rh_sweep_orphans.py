"""Delete orphan asset FOLDERS, but only after proving the whole folder is free.

  RH_LIST=<free-mesh-paths.txt> RH_REPORT=<out.txt> [RH_APPLY=1] \
  UnrealEditor-Cmd <proj> -run=pythonscript \
      -script=$PWD/scripts/unreal/rh_sweep_orphans.py -unattended -nosound -stdout

Dry-run unless RH_APPLY=1.

WHY FOLDER-LEVEL RATHER THAN ASSET-LEVEL
----------------------------------------
Deleting only the mesh leaves its MI_ and its textures behind - dangling material
instances that still show up in every audit and confuse the next reader, which is
the whole reason this sweep exists. So the unit is the asset FOLDER.

But a folder is only safe to remove if NOTHING outside it references ANYTHING
inside it. A mesh being free does not make its siblings free: a texture in one
prop's folder can be wired into a different prop's material instance, and that
sharing is invisible from the mesh alone. So this re-derives the check per folder
over every asset in it, using UE's dependency graph rather than grep - a
reference held inside a binary .uasset (a level placement, a Blueprint, a
material chain) is exactly what a text search cannot see.

Any folder with a single outside referencer is skipped whole and reported, not
partially emptied.
"""
import os

import unreal

LIST = os.environ["RH_LIST"]
OUT = os.environ.get("RH_REPORT", "/tmp/rh_sweep.txt")
APPLY = os.environ.get("RH_APPLY") == "1"

reg = unreal.AssetRegistryHelpers.get_asset_registry()
EAL = unreal.EditorAssetLibrary

OPTS = unreal.AssetRegistryDependencyOptions(
    include_soft_package_references=True,
    include_hard_package_references=True,
    include_searchable_names=False,
    include_soft_management_references=False,
    include_hard_management_references=False,
)

# Map each free mesh to its top-level asset folder:
#   /Game/RedHope/Art/<lineage>/<name>/...   ->  /Game/RedHope/Art/<lineage>/<name>
folders = {}
for line in open(LIST):
    p = line.strip()
    if not p.startswith("/Game/RedHope/Art/"):
        continue
    parts = p.split("/")
    if len(parts) < 6:
        continue
    folder = "/".join(parts[:6])
    folders.setdefault(folder, []).append(p)

log = []
safe, blocked = [], []

for folder in sorted(folders):
    assets = [str(a.package_name) for a in reg.get_assets_by_path(unreal.Name(folder), recursive=True)]
    if not assets:
        log.append("EMPTY    %s" % folder)
        continue
    inside = set(assets)
    outside_refs = []
    for pkg in assets:
        for r in (reg.get_referencers(unreal.Name(pkg), OPTS) or []):
            rs = str(r)
            if rs not in inside and not rs.startswith(folder + "/") and rs != folder:
                outside_refs.append("%s <- %s" % (pkg.rsplit("/", 1)[1], rs))
    if outside_refs:
        blocked.append((folder, outside_refs))
        log.append("BLOCKED  %s  (%d asset(s))  %s"
                   % (folder, len(assets), outside_refs[0]))
    else:
        safe.append((folder, len(assets)))
        log.append("safe     %-58s %d asset(s)" % (folder, len(assets)))

log.append("")
log.append("=== %d folders safe (%d assets), %d blocked ==="
           % (len(safe), sum(n for _, n in safe), len(blocked)))

deleted = 0
if APPLY:
    log.append("")
    for folder, n in safe:
        ok = EAL.delete_directory(folder)
        log.append("%-8s %s (%d assets)" % ("deleted" if ok else "FAILED", folder, n))
        if ok:
            deleted += n
    log.append("")
    log.append("=== deleted %d assets across %d folders ===" % (deleted, len(safe)))
else:
    log.append("")
    log.append("DRY RUN - set RH_APPLY=1 to delete")

with open(OUT, "w") as f:
    f.write("\n".join(log) + "\n")
print("\n".join(log[-6:]))
