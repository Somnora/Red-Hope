#!/usr/bin/env python
"""Labeled contact sheets for review. Emits three PNGs to the io root:
  furnish_contact.png     - 9 props (rows) x 3 seeds (cols)
  walltex_base_contact.png  - 3 textures x 3 seeds, the raw tile
  walltex_tiled_contact.png - 3 textures x 3 seeds, 2x2 tiled (seam check)
"""
import os
from PIL import Image, ImageDraw

NS = "/lambda/nfs/red-hope-east/red_hope"
CELL, PAD, LBL = 320, 6, 18

def sheet(rows, cols, path_fn, label_fn, out):
    W = len(cols) * (CELL + PAD) + PAD
    H = len(rows) * (CELL + PAD + LBL) + PAD
    canvas = Image.new("RGB", (W, H), (30, 30, 34))
    d = ImageDraw.Draw(canvas)
    for r, rk in enumerate(rows):
        for c, ck in enumerate(cols):
            p = path_fn(rk, ck)
            x = PAD + c * (CELL + PAD)
            y = PAD + r * (CELL + PAD + LBL)
            if os.path.exists(p):
                im = Image.open(p).convert("RGB").resize((CELL, CELL))
                canvas.paste(im, (x, y + LBL))
            d.text((x + 2, y + 3), label_fn(rk, ck), fill=(230, 230, 120))
    canvas.save(out)
    print(f"wrote {out}  ({W}x{H})", flush=True)

# --- furnish: rows = props, cols = seeds ---
FKEYS = ["repairbench", "electronics", "sciencebench", "bed", "diningbooth",
         "reclounge", "exercise", "hygiene", "airlock"]
FSEEDS = [7300, 7449, 7598]
sheet(FKEYS, FSEEDS,
      lambda k, s: f"{NS}/io/furnish/{k}_s{s}.png",
      lambda k, s: f"{k}  s{s}",
      f"{NS}/io/furnish_contact.png")

# --- walltex: rows = textures, cols = seeds ---
WKEYS = ["habwall_quilted", "habwall_padded", "habfloor_soft"]
WSEEDS = [4200, 4411, 4622]
sheet(WKEYS, WSEEDS,
      lambda k, s: f"{NS}/io/walltex/{k}_s{s}.png",
      lambda k, s: f"{k}  s{s}",
      f"{NS}/io/walltex_base_contact.png")
sheet(WKEYS, WSEEDS,
      lambda k, s: f"{NS}/io/walltex/{k}_s{s}_tiled.png",
      lambda k, s: f"{k}  s{s} (2x2)",
      f"{NS}/io/walltex_tiled_contact.png")
print("contact sheets done", flush=True)
