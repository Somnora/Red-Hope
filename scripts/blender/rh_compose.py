"""rh_compose — composite a finished BaseColor from albedo + AO + curvature.

Pure numpy/PIL; no Blender, so tuning is instant and re-runnable without baking.

    python3 scripts/blender/rh_compose.py --name HabitatDome --dir <bakedir> \
        [--out <png>] [--ao 0.55] [--wear 0.30] [--grime 0.25]

The three terms, and why each exists:
  AO      multiplies shadow into contact areas and cavities. Generator albedo is
          uniformly lit, so nothing reads as recessed until this lands.
  wear    convex edges lighten and desaturate - the single strongest "this object
          exists in a world" cue, and the reason curvature is baked at all.
  grime   cavities darken slightly. Kept low; it is easy to make a model muddy.

Strengths are deliberately conservative: the aim is depth, not a filter.
"""

import argparse
import os

import numpy as np
from PIL import Image


def load(path, size=None, mode="RGB"):
    im = Image.open(path).convert(mode)
    if size and im.size != size:
        im = im.resize(size, Image.LANCZOS)
    return np.asarray(im, dtype=np.float32) / 255.0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--name", required=True)
    ap.add_argument("--dir", required=True)
    ap.add_argument("--out")
    ap.add_argument("--ao", type=float, default=0.55)
    ap.add_argument("--wear", type=float, default=0.30)
    ap.add_argument("--grime", type=float, default=0.25)
    a = ap.parse_args()

    base = os.path.join(a.dir, a.name)
    alb = load(base + "_albedo.png")
    size = (alb.shape[1], alb.shape[0])
    ao = load(base + "_ao.png", size, "L")
    cv = load(base + "_curv.png", size, "L")

    # AO: lerp toward 1 so strength 0 is a no-op and 1 is the raw bake.
    ao_term = 1.0 - a.ao * (1.0 - ao)
    out = alb * ao_term[..., None]

    # Curvature splits into a convex (edge) and concave (cavity) mask.
    edge = np.clip((cv - 0.5) * 2.0, 0.0, 1.0)
    cavity = np.clip((0.5 - cv) * 2.0, 0.0, 1.0)

    # Worn edges: brighter and desaturated, as if rubbed back to substrate.
    lum = out.mean(axis=2, keepdims=True)
    worn = np.clip(lum * 1.55 + 0.06, 0.0, 1.0)
    worn = np.repeat(worn, 3, axis=2)
    out = out * (1.0 - (edge * a.wear)[..., None]) + worn * (edge * a.wear)[..., None]

    # Cavity grime.
    out = out * (1.0 - (cavity * a.grime)[..., None] * 0.6)

    out = np.clip(out, 0.0, 1.0)
    dst = a.out or (base + "_final.png")
    Image.fromarray((out * 255.0 + 0.5).astype(np.uint8)).save(dst)

    d = float(np.abs(out - alb).mean())
    print(f"[rh] composed {a.name}: ao={a.ao} wear={a.wear} grime={a.grime} "
          f"mean|delta|={d:.4f} -> {os.path.basename(dst)}")


main()
