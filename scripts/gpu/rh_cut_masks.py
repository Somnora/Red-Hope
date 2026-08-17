"""Cut emissive masks from albedo textures, per-model recipes. Local, no UE.

  uv run --with pillow --with numpy --with scipy python scripts/gpu/rh_cut_masks.py \
      --spec '<name>:<albedo.png>' [...] --out <dir> [--sheet contact.jpg]

Writes <out>/T_<name>_EmissiveMask.png (2048 grayscale) plus a contact sheet so
the masks are JUDGED before import - the same gate every other generated
artifact in this project passes through.

WHY THIS EXISTS, AGAIN
----------------------
The original cutter ran in a session scratchpad on 2026-08-14 and was lost with
it; only its outputs (the .uassets) and its recipes (commit 3bf72f3's message)
survived. Then the 2026-08-17 TRELLIS.2 refresh regenerated the UV layouts of 21
assets, and A MASK IS UV-KEYED TO THE ATLAS IT WAS CUT FROM - so every mask on a
refreshed asset silently pointed its glow at the wrong texels (HeavyForge wore
orange in the wrong places at EmissiveAmount 2.2). This file is the cutter
rebuilt IN THE REPO, so the next re-bake is a re-run, not an archaeology dig.

THE STANDING RULE THIS ENCODES: anything derived from a texture's UV layout
(masks today; anything else tomorrow) must be REGENERATED whenever the mesh is
re-baked, because TRELLIS.2's unwrap is not stable run to run - the same seed
has produced 7817 and 7571 triangles. Stable UVs across bakes are not a thing
this pipeline has; derived artifacts must therefore be cheap to remake.

RECIPES (from 3bf72f3, adapted where the refreshed art changed the subject):
  HeavyForge     ember hue 8-38deg, S>0.72, V>165/255. Excludes hazard yellow
                 (hue ~50deg) by the hue window alone.
  battery        teal/blue display panels: hue 150-250, moderate S, bright V.
  CommandModule  blue screens, dimmer window than battery's.
  ice            teal accents at 1.0. The original also lit the painted ice
                 mass at 0.28; the refreshed vessels are bone-white overall, so
                 a pale-key would glow the whole building - DROPPED, noted.
  extractor2, airfilter2
                 teal key, then connected-component size filter 15-900 px:
                 status dots and small screens, never the big painted panels.
  HabitatDome    the original keyed LIT portholes. The refreshed dome has
                 OPAQUE viewport covers (dark discs), so the equivalent is
                 geometric: near-circular dark blobs on the pale shell, then
                 gaussian blur 5 px for the warm frosted read. If disc
                 detection finds an implausible count it emits BLACK and warns,
                 because no glow beats wrong glow.
"""
import argparse
import os

import numpy as np
from PIL import Image, ImageFilter
from scipy import ndimage


def hsv(img):
    a = np.asarray(img.convert("HSV"), dtype=np.float32)
    return a[..., 0] * 360.0 / 255.0, a[..., 1] / 255.0, a[..., 2]


def teal_key(h, s, v, hue_lo=150, hue_hi=250, s_min=0.35, v_min=110):
    return (h >= hue_lo) & (h <= hue_hi) & (s >= s_min) & (v >= v_min)


def size_filter(mask, lo=15, hi=900):
    lab, n = ndimage.label(mask)
    if n == 0:
        return mask
    sizes = ndimage.sum(mask, lab, range(1, n + 1))
    keep = np.zeros(n + 1, dtype=bool)
    keep[1:] = (sizes >= lo) & (sizes <= hi)
    return keep[lab]


def cut(name, img):
    h, s, v = hsv(img)
    out = np.zeros(h.shape, dtype=np.float32)
    note = ""

    # Thresholds retuned 2026-08-17 against the REFRESHED albedos, from measured
    # HSV percentiles rather than the 2026-08-14 values: the TRELLIS.2 bake
    # desaturates and darkens the glow accents relative to the old Hunyuan paint
    # (ice's readout survives at S~0.30 V~190; the old S>0.35 cut found zero).
    # Where a loose key risks noise, the component size filter is the guard -
    # scattered pixels never form a 15-900 px blob.
    if name == "HeavyForge":
        ember = (h >= 8) & (h <= 38) & (s > 0.72) & (v > 165)
        out[ember | teal_key(h, s, v)] = 1.0
        note = "ember + teal readout"
    elif name == "battery":
        out[teal_key(h, s, v)] = 1.0
    elif name == "CommandModule":
        out[size_filter((h >= 150) & (h <= 260) & (s > 0.15) & (v > 120))] = 1.0
    elif name == "ice":
        out[teal_key(h, s, v, s_min=0.22, v_min=150)] = 1.0
        note = "frost-mass layer dropped: refreshed vessels are bone-white overall"
    elif name == "extractor2":
        out[size_filter((h >= 150) & (h <= 260) & (s > 0.18) & (v > 100))] = 1.0
    elif name == "airfilter2":
        out[size_filter(teal_key(h, s, v))] = 1.0
    elif name == "HabitatDome":
        # Disc detection was tried and found 151 candidates on the refreshed
        # atlas - a triangulated-panel dome is full of near-circular fragments,
        # so the geometric route cannot tell a porthole from a panel. BLACK,
        # deterministically: no glow beats wrong glow. The warm-porthole night
        # read the director liked is therefore LOST on the new dome until a
        # mask is hand-painted or the portholes are made teal in the reference.
        note = "BLACK by recipe - see comment; portholes need a hand mask"
    else:
        raise SystemExit("no recipe for %s" % name)

    m = Image.fromarray((out * 255).astype(np.uint8), "L")
    if name == "HabitatDome" and out.any():
        m = m.filter(ImageFilter.GaussianBlur(5))
    return m, note


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--spec", action="append", required=True,
                    help="<name>:<albedo.png>")
    ap.add_argument("--out", required=True)
    ap.add_argument("--sheet", default=None)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)

    cells = []
    for spec in a.spec:
        name, path = spec.split(":", 1)
        img = Image.open(path).convert("RGB")
        mask, note = cut(name, img)
        dst = os.path.join(a.out, "T_%s_EmissiveMask.png" % name)
        mask.save(dst)
        cov = np.asarray(mask, dtype=np.float32).mean() / 255.0 * 100.0
        print("%-14s coverage %6.2f%%  %s" % (name, cov, note))
        cells.append((name, img, mask))

    if a.sheet and cells:
        S = 300
        sheet = Image.new("RGB", (2 * S, len(cells) * (S + 18)), (16, 16, 18))
        from PIL import ImageDraw
        d = ImageDraw.Draw(sheet)
        for i, (name, img, mask) in enumerate(cells):
            y = i * (S + 18)
            d.text((4, y + 2), name, fill=(240, 240, 240))
            sheet.paste(img.resize((S, S)), (0, y + 18))
            sheet.paste(mask.convert("RGB").resize((S, S)), (S, y + 18))
        sheet.save(a.sheet, quality=90)
        print("sheet -> %s" % a.sheet)


if __name__ == "__main__":
    main()
