"""rh_finish — drive the C2/C3/C6 finishing stages over one asset.

  blender --background --python scripts/blender/rh_finish.py -- \
      --in <src.glb> --out <dst.glb> --report <report.json> \
      [--tier H|S|B] [--weld 0.0001] [--angle 40] [--uv] [--no-decimate]

Stages, in order: weld -> smooth-by-angle -> (decimate to tier) -> UV -> pivot
-> export. Baking and texture composition are separate stages (rh_bake.py)
because they cost minutes, not seconds, and want batching overnight.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rh_lib as R  # noqa: E402

TIER_TRIS = {"H": 35000, "S": 20000, "B": 8000}


def parse(argv):
    opts = {"tier": "S", "weld": 0.0001, "angle": 40.0, "uv": False,
            "decimate": True, "in": None, "out": None, "report": None}
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "--in":
            opts["in"] = argv[i + 1]; i += 2
        elif a == "--out":
            opts["out"] = argv[i + 1]; i += 2
        elif a == "--report":
            opts["report"] = argv[i + 1]; i += 2
        elif a == "--tier":
            opts["tier"] = argv[i + 1].upper(); i += 2
        elif a == "--weld":
            opts["weld"] = float(argv[i + 1]); i += 2
        elif a == "--angle":
            opts["angle"] = float(argv[i + 1]); i += 2
        elif a == "--uv":
            opts["uv"] = True; i += 1
        elif a == "--no-decimate":
            opts["decimate"] = False; i += 1
        else:
            i += 1
    if not opts["in"] or not opts["out"]:
        raise SystemExit("[rh] usage: --in <glb> --out <glb> [--report r.json] "
                         "[--tier H|S|B] [--uv] [--no-decimate]")
    return opts


def main():
    o = parse(R.argv_after_ddash())
    name = os.path.splitext(os.path.basename(o["in"]))[0]
    R.log(f"=== finishing {name} (tier {o['tier']}) ===")

    ob = R.load_mesh(o["in"])
    before = R.metrics(ob)

    R.weld(ob, o["weld"])
    R.smooth_by_angle(ob, o["angle"])
    if o["decimate"]:
        R.decimate_to(ob, TIER_TRIS.get(o["tier"], 20000))
        # Welding/decimating invalidates the old UVs; re-unwrap if asked.
    if o["uv"]:
        R.smart_uv(ob)
    R.ground_center_pivot(ob)
    R.export_glb(ob, o["out"])

    after = R.metrics(ob, "RHBake" if o["uv"] else None)
    budget = TIER_TRIS.get(o["tier"], 20000)
    verdict = {
        "asset": name,
        "tier": o["tier"],
        "tri_budget": budget,
        "within_tri_budget": after["tris"] <= budget,
        "before": before,
        "after": after,
    }
    if o["report"]:
        R.write_report(o["report"], verdict)
    R.log(f"=== {name}: {after['tris']} tris "
          f"({'WITHIN' if verdict['within_tri_budget'] else 'OVER'} tier {o['tier']} budget {budget}) ===")
    print("RH_FINISH_OK", name, flush=True)


main()
