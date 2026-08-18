"""Stopgap albedos for crew whose real paint is unrecoverable: flat role-tinted
coveralls with two-scale fabric noise. Local, no UE, no GPU.

  uv run --with pillow --with numpy --with scipy python scripts/gpu/rh_flat_suits.py --out <dir>

WHY (2026-08-17): eight crew (the confetti-8) have shattered-UV confetti paint
and NO surviving source anywhere - not on the filesystem, not in
game_glbs_20260717.tgz, not on the contact sheet (all checked). Until they are
regenerated from new sprites, this makes them read as DELIBERATE background crew
in sealed work suits instead of static noise. A flat colour is UV-independent,
so it survives the shattered layout by construction.

Skin preservation was tried and REJECTED on a contact sheet: hue detection kept
rust-orange gear fragments alongside real skin, which re-scattered blotches over
the body - the thing being purged. Fully flat, consistently, reads better.

Their MIs also go back to scalar Rough/Metallic and flat normals
(UseMRTex=0, UseNormTex=0): the per-pixel maps were confetti-derived noise.
"""
import argparse
import os

import numpy as np
from PIL import Image
from scipy import ndimage

SUITS = {
    "bot_lindqvist": (96, 118, 86),    # botanist - sage
    "comms_diallo":  (86, 100, 122),   # comms - slate blue
    "cook_moreau":   (168, 158, 140),  # cook - warm cream
    "driver_costa":  (140, 94, 58),    # driver - muted rust
    "fab_stone":     (110, 112, 116),  # fabricator - steel
    "rookie_shaw":   (134, 124, 96),   # rookie - khaki
    "safety_abara":  (170, 130, 52),   # safety officer - amber
    "vet_kowalski":  (104, 110, 88),   # veteran - olive
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--size", type=int, default=2048)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    rng = np.random.default_rng(11)
    for n, (r, g, b) in SUITS.items():
        fine = rng.normal(0, 4.5, (a.size, a.size))
        broad = ndimage.gaussian_filter(rng.normal(0, 1, (a.size, a.size)), 48) * 22
        v = (fine + broad)[..., None]
        out = np.clip(np.array([r, g, b], dtype=np.float32)[None, None, :] + v, 0, 255)
        Image.fromarray(out.astype(np.uint8)).save(os.path.join(a.out, "%s.png" % n))
        print(n, "written")


if __name__ == "__main__":
    main()
