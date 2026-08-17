#!/usr/bin/env python
"""Cut the figure off its halo/spotlight background and re-seat it on a truly
flat grey, so Hunyuan's bg-removal strips it cleanly (no orb artifact).
Usage: rh_flatten.py <in_glob_dir> <out_dir> <suffix e.g. _s8100>
"""
import sys, os, glob
from PIL import Image
try:
    from rembg import remove, new_session
except Exception as e:
    print("REMBG_MISSING", e); sys.exit(3)

indir, outdir, suf = sys.argv[1], sys.argv[2], sys.argv[3]
os.makedirs(outdir, exist_ok=True)
from PIL import ImageFilter
session = new_session("isnet-general-use")  # sharper mattes than u2net for figures
BG = (244, 244, 244)
for p in sorted(glob.glob(f"{indir}/*{suf}.png")):
    im = Image.open(p).convert("RGBA")
    cut = remove(im, session=session, alpha_matting=True,
                 alpha_matting_foreground_threshold=250,
                 alpha_matting_background_threshold=15)
    # Hard-threshold the mask so no semi-transparent halo pixels survive, then
    # erode 1px to shed the fringe. Guarantees a truly flat background.
    r, g, b, a = cut.split()
    a = a.point(lambda v: 255 if v > 150 else 0)
    a = a.filter(ImageFilter.MinFilter(3))  # 1px erode
    cut = Image.merge("RGBA", (r, g, b, a))
    flat = Image.new("RGBA", cut.size, BG + (255,))
    flat.alpha_composite(cut)
    name = os.path.basename(p).replace(suf, "")
    flat.convert("RGB").save(os.path.join(outdir, name))
    print("flattened", name, flush=True)
print("done", flush=True)
