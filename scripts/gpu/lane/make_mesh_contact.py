#!/usr/bin/env python
"""Contact sheet of meshed-prop previews (three_quarter view) for a queue job.
Usage: make_mesh_contact.py <out_dir_with_previews> <sheet_path> [cols=3]
"""
import sys, os, glob
from PIL import Image, ImageDraw

src, out = sys.argv[1], sys.argv[2]
cols = int(sys.argv[3]) if len(sys.argv) > 3 else 3
CELL, PAD, LBL = 340, 6, 18

tiles = sorted(glob.glob(os.path.join(src, "*_preview_three_quarter.png")))
if not tiles:  # fall back to any preview
    tiles = sorted(glob.glob(os.path.join(src, "*_preview*.png")))
rows = (len(tiles) + cols - 1) // cols
W = cols * (CELL + PAD) + PAD
H = rows * (CELL + PAD + LBL) + PAD
canvas = Image.new("RGB", (W, H), (28, 28, 32))
d = ImageDraw.Draw(canvas)
for i, t in enumerate(tiles):
    name = os.path.basename(t).split("_preview")[0]
    r, c = divmod(i, cols)
    x = PAD + c * (CELL + PAD)
    y = PAD + r * (CELL + PAD + LBL)
    im = Image.open(t).convert("RGB")
    im.thumbnail((CELL, CELL))
    canvas.paste(im, (x + (CELL - im.width) // 2, y + LBL + (CELL - im.height) // 2))
    d.text((x + 2, y + 3), name, fill=(230, 230, 120))
canvas.save(out)
print(f"wrote {out} ({W}x{H}) from {len(tiles)} previews", flush=True)
