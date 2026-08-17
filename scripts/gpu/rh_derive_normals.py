"""Derive tangent-space normal maps from albedo luminance. Local, no UE, no GPU.

  uv run --with pillow --with numpy --with scipy python \
      scripts/gpu/rh_derive_normals.py --spec '<name>:<albedo.png>' [...] \
      --out <dir> [--sheet contact.jpg] [--strength 1.0]

Writes <out>/T_<name>_Normal.png (RGB, tangent space, +Y up) plus a contact
sheet, because a derived normal is a guess and a guess gets judged before it
ships.

WHY THIS EXISTS
---------------
Dumped 2026-08-17: NEITHER master material had a normal input at all
(M_RH_Master and M_RH_Character both reported "Normal <- <none>"), and only 6
normal maps existed in the whole art tree - all hand-authored surfaces. Of the
~700 GENERATED models: zero. TRELLIS.2 and Hunyuan emit baseColor + metallic +
roughness and no normal, so every prop, building and crew member shaded off
nothing but its decimated vertex normals. On a large flat panel that reads as
soft irregular light/dark patching - the director's "splotchy", reported three
times across three sessions and never fixed by re-baking the ALBEDO, because
the albedo was never the defect.

WHAT THIS IS AND IS NOT
-----------------------
This is the CHEAP half of the fix and it is honest about being an approximation.
A real normal map is baked from high-poly geometry; TRELLIS.2's high-poly is
discarded at decimation time, so for the 700 assets already baked there is no
high-poly left to bake from. What survives is the PAINT, and generated paint
puts real surface information in its luminance: panel gaps, rivets, weld beads
and grille slots are all painted darker than the plate around them. Treating
luminance as a height field recovers those as relief. It cannot invent detail
the paint does not describe, and it will read a dark PAINTED marking (a hazard
stripe, a stencil) as a dent - which is why strength stays conservative and the
sheet gets looked at.

The RIGHT fix for future batches is to keep TRELLIS.2's pre-decimation mesh and
bake a real normal in Blender; that is a pipeline change, noted in the asset
pipeline guide. This script closes the gap for everything already on disk.

METHOD
------
1. luminance -> lightly blurred height (blur kills per-texel paint noise that
   would otherwise become a field of pimples)
2. high-pass: subtract a WIDE blur, so broad albedo shading (one panel darker
   than its neighbour) does not tilt the whole surface. Only local detail
   survives, which is what relief actually is.
3. Sobel gradients -> tangent normal, normalized per texel
4. +Y up (OpenGL/glTF convention, which is what UE's TC_Normalmap expects for
   these imports; the existing 6 hand normals follow it too)
"""
import argparse
import os

import numpy as np
from PIL import Image
from scipy import ndimage


def derive(img, strength=1.0, detail_blur=2.4):
    a = np.asarray(img.convert("RGB"), dtype=np.float32) / 255.0
    lum = 0.299 * a[..., 0] + 0.587 * a[..., 1] + 0.114 * a[..., 2]

    # detail_blur is the load-bearing knob, tuned 2026-08-17 against an
    # isolated render: at 1.1 the map was correct but OVER-COOKED - real panel
    # gaps and louvre slots came through beautifully AND every flat plate picked
    # up a hammered orange-peel pebbling, because per-texel paint noise became
    # relief. 2.4 keeps the features the eye reads as construction and drops the
    # noise floor that reads as damage.
    detail = ndimage.gaussian_filter(lum, detail_blur)
    broad = ndimage.gaussian_filter(lum, 24.0)
    height = np.clip(0.5 + (detail - broad) * 2.2, 0.0, 1.0)

    # Sobel in texels; the scale folds in the user strength.
    gx = ndimage.sobel(height, axis=1) * (12.0 * strength)
    gy = ndimage.sobel(height, axis=0) * (12.0 * strength)

    nx, ny, nz = -gx, gy, np.ones_like(gx)
    ln = np.sqrt(nx * nx + ny * ny + nz * nz)
    nx, ny, nz = nx / ln, ny / ln, nz / ln

    out = np.stack([nx * 0.5 + 0.5, ny * 0.5 + 0.5, nz * 0.5 + 0.5], axis=-1)
    return Image.fromarray((np.clip(out, 0, 1) * 255).astype(np.uint8), "RGB")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--spec", action="append", required=True, help="<name>:<albedo.png>")
    ap.add_argument("--out", required=True)
    ap.add_argument("--sheet", default=None)
    ap.add_argument("--strength", type=float, default=1.0)
    ap.add_argument("--detail-blur", type=float, default=2.4,
                    help="small-scale blur before the high-pass; higher = less "
                         "paint-noise pebbling, fewer fine features")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)

    cells = []
    for spec in a.spec:
        name, path = spec.split(":", 1)
        img = Image.open(path)
        nrm = derive(img, a.strength, a.detail_blur)
        dst = os.path.join(a.out, "T_%s_Normal.png" % name)
        nrm.save(dst)
        # Deviation from flat: how much relief was actually found. Near zero
        # means the paint had no detail to recover and the map is a no-op -
        # worth SAYING rather than shipping a flat map as if it did something.
        arr = np.asarray(nrm, dtype=np.float32)
        dev = float(np.abs(arr[..., :2] - 127.5).mean() / 127.5 * 100.0)
        print("%-16s relief %5.1f%%  -> %s" % (name, dev, os.path.basename(dst)))
        cells.append((name, img.convert("RGB"), nrm))

    if a.sheet and cells:
        S = 300
        sheet = Image.new("RGB", (2 * S, len(cells) * (S + 18)), (16, 16, 18))
        from PIL import ImageDraw
        d = ImageDraw.Draw(sheet)
        for i, (name, src, nrm) in enumerate(cells):
            y = i * (S + 18)
            d.text((4, y + 2), name, fill=(240, 240, 240))
            sheet.paste(src.resize((S, S)), (0, y + 18))
            sheet.paste(nrm.resize((S, S)), (S, y + 18))
        sheet.save(a.sheet, quality=90)
        print("sheet -> %s" % a.sheet)


if __name__ == "__main__":
    main()
