"""Import the code-built furniture pieces (scripts/blender/rh_build_furnishings.py)
in ONE commandlet run - rh_import_hs.py once per piece.

  RH_SRC=$PWD/Martians/gen/hs RH_PIECES="chair desk ..." RH_REPORT=<txt> \
    UnrealEditor-Cmd <proj> -run=pythonscript \
    -script=$PWD/scripts/unreal/rh_import_furniture.py -unattended -nosound -stdout

Each piece lands at /Game/RedHope/Art/Furniture/furn_<piece>/StaticMeshes/
furn_<piece> with MI_furn_<piece> on M_RH_Master (AccentAmount 0, Nanite off),
its emissive mask at /Game/RedHope/Art/Masks/T_furn_<piece>_EmissiveMask. The
lamp glows warm; screens glow the colony teal. The report ends with each
mesh's bounds in cm read back from the asset - furniture is placed at TRUE
SCALE, so a chair must come back ~47 cm wide, not fitted to a cell.
"""
import os
import runpy

import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.environ["RH_SRC"]
PIECES = os.environ.get("RH_PIECES", "chair desk bench sofa lamp rug cot bunkbed plant").split()
REPORT = os.environ.get("RH_REPORT", "/tmp/rh_import_furniture.txt")
GLOW = {"lamp": ("2.4,1.45,0.65", "1.8")}   # warm lamp; everything else: teal screens
out = []

for p in PIECES:
    name = "furn_%s" % p
    rep = "%s.%s" % (REPORT, p)
    glow, amt = GLOW.get(p, ("0.05,2.2,1.56", "1.6"))
    os.environ.update({"RH_NAME": name, "RH_DIR": os.path.join(SRC, name),
                       "RH_DEST": "/Game/RedHope/Art/Furniture", "RH_GLOW": glow,
                       "RH_GLOW_AMT": amt, "RH_REPORT": rep})
    os.environ.pop("RH_TEX_TAG", None)
    try:
        runpy.run_path(os.path.join(HERE, "rh_import_hs.py"), run_name="__main__")
    except Exception as e:  # keep going: one bad piece must not hide the rest
        out.append("%s: EXCEPTION %s" % (p, e))
        continue
    lines = open(rep).read().splitlines() if os.path.exists(rep) else ["(no report)"]
    done = [l for l in lines if l.startswith("DONE ")]
    if not done:
        out.append("%s: ** FAILED ** %s" % (p, " | ".join(l for l in lines if "**" in l)[:400]))
        continue
    path = done[0].split(" ", 1)[1]
    mesh = unreal.load_asset(path)
    bb = mesh.get_bounding_box() if mesh else None
    ext = (bb.max - bb.min) if bb else None
    out.append("%s: OK %s  bounds %.0f x %.0f x %.0f cm  tris %d" % (
        p, path, ext.x, ext.y, ext.z, mesh.get_num_triangles(0)) if ext else "%s: OK %s (no bounds)" % (p, path))

with open(REPORT, "w") as f:
    f.write("\n".join(out) + "\n")
