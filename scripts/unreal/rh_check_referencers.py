"""Ask UE's asset registry who references a candidate orphan. Read-only.

  RH_LIST=<paths.txt> RH_REPORT=<out.txt> UnrealEditor-Cmd <proj> \
      -run=pythonscript -script=$PWD/scripts/unreal/rh_check_referencers.py \
      -unattended -nosound -stdout

WHY NOT GREP
------------
grep over Source/ and docs/ finds C++ string literals and DataTable rows, which is
most of how this project references art - but it cannot see a reference held
inside a BINARY .uasset: a level that placed the mesh, a Blueprint that points at
it, a material instance chain, another mesh's LOD. Deleting on grep evidence alone
risks a missing-asset error at boot, and the failure would land on the director's
screen rather than in a log I read.

UE's asset registry knows the real dependency graph. This asks it.

A referencer INSIDE the candidate's own asset folder does not count - a mesh's own
MI_ and textures live beside it and will be deleted with it. Only a referencer
from somewhere else keeps an asset alive.

Deletes nothing. Prints a verdict per path so the caller can act.
"""
import os

import unreal

LIST = os.environ["RH_LIST"]
OUT = os.environ.get("RH_REPORT", "/tmp/rh_referencers.txt")

reg = unreal.AssetRegistryHelpers.get_asset_registry()
paths = [p.strip() for p in open(LIST) if p.strip()]

log = []
free, held, missing = [], [], []

for p in paths:
    pkg = p.rsplit(".", 1)[0]          # accept Package or Package.Object
    leaf = pkg.rsplit("/", 1)[1]
    if not unreal.EditorAssetLibrary.does_asset_exist("%s.%s" % (pkg, leaf)):
        missing.append(pkg)
        log.append("MISSING  %s" % pkg)
        continue

    # The asset's own folder - a sibling MI/texture is not a reason to keep it.
    own_dir = pkg.rsplit("/", 1)[0]
    # Also treat the enclosing asset folder as "own" for the doubled Interchange
    # layout (Agri/<n>/<n>/StaticMeshes/<n>), where siblings sit a level up.
    own_roots = {own_dir, own_dir.rsplit("/", 1)[0]}

    refs = reg.get_referencers(
        unreal.Name(pkg),
        unreal.AssetRegistryDependencyOptions(
            include_soft_package_references=True,
            include_hard_package_references=True,
            include_searchable_names=False,
            include_soft_management_references=False,
            include_hard_management_references=False,
        ),
    ) or []

    outside = []
    for r in refs:
        rs = str(r)
        if not any(rs.startswith(root + "/") or rs == root for root in own_roots):
            outside.append(rs)

    if outside:
        held.append((pkg, outside))
        log.append("HELD     %s   <- %s%s"
                   % (pkg, ", ".join(outside[:3]),
                      " (+%d more)" % (len(outside) - 3) if len(outside) > 3 else ""))
    else:
        free.append(pkg)
        log.append("free     %s   (%d internal ref(s) only)" % (pkg, len(refs)))

log.append("")
log.append("=== %d free to delete, %d HELD by an outside referencer, %d missing ==="
           % (len(free), len(held), len(missing)))
log.append("")
log.append("--- free list (one per line, for a delete step) ---")
log.extend(free)

with open(OUT, "w") as f:
    f.write("\n".join(log) + "\n")
print("\n".join(log[-8:]))
