#!/usr/bin/env python
"""Wall + floor tiling textures, grounded-industrial palette (the in-engine deck
tiles read 'weird' to the director). Seamless via circular Conv2d padding. Saves
each tile plus a 2x2 tiled preview so seams are visible on review.

Usage: rh_surfaces.py <out_dir> [n_seeds=3]
"""
import sys, os, torch
from PIL import Image
from diffusers import StableDiffusionXLPipeline

out_dir = sys.argv[1]
n_seeds = int(sys.argv[2]) if len(sys.argv) > 2 else 3
os.makedirs(out_dir, exist_ok=True)

TEX = {
  # --- walls: clean sci-fi hab interior linings, not puffy ---
  "wall_panel":   "seamless tileable texture of clean brushed metal wall panels with recessed "
                  "seams and small rivets, light grey spacecraft interior wall, evenly lit, "
                  "flat orthographic front view, matte",
  "wall_hex":     "seamless tileable texture of white insulated wall panels with a subtle "
                  "hexagonal padded quilting, clean bright sci-fi habitat lining, evenly lit, "
                  "flat orthographic front view, matte",
  "wall_ribbed":  "seamless tileable texture of vertical ribbed corrugated metal wall paneling, "
                  "industrial off-white, soft even light, flat orthographic front view, matte",
  # --- floors: readable deck surfaces from a top-down slice camera ---
  "floor_deck":   "seamless tileable texture of industrial metal deck plating, subtle diamond "
                  "tread pattern with panel seams and corner bolts, mid grey, evenly lit, "
                  "flat orthographic top-down, matte",
  "floor_sealed": "seamless tileable texture of clean sealed floor panels, large light-grey "
                  "epoxy tiles with thin dark grout seams, evenly lit, flat orthographic "
                  "top-down, matte",
  "floor_hazard": "seamless tileable texture of an industrial floor of grey panels bordered by "
                  "yellow-and-black hazard striping along the seams, evenly lit, flat "
                  "orthographic top-down, matte",
}
NEG = ("perspective, room photo, furniture, object, people, person, text, watermark, logo, "
       "dark shadows, vignette, uneven lighting, outer border frame, 3d render angle")

pipe = StableDiffusionXLPipeline.from_pretrained(
    "stabilityai/stable-diffusion-xl-base-1.0",
    torch_dtype=torch.float16, variant="fp16").to("cuda")
pipe.set_progress_bar_config(disable=True)

patched = 0
for mod in list(pipe.unet.modules()) + list(pipe.vae.modules()):
    if isinstance(mod, torch.nn.Conv2d):
        mod.padding_mode = "circular"
        patched += 1
print(f"[surfaces] circular padding on {patched} conv layers", flush=True)

def tiled(img, n=2):
    w, h = img.size
    c = Image.new("RGB", (w * n, h * n))
    for i in range(n):
        for j in range(n):
            c.paste(img, (i * w, j * h))
    return c

print(f"[surfaces] {len(TEX)} textures x {n_seeds} seeds", flush=True)
for key, prompt in TEX.items():
    for s in range(n_seeds):
        seed = 5300 + s * 197
        g = torch.Generator("cuda").manual_seed(seed)
        img = pipe(prompt=prompt, negative_prompt=NEG,
                   num_inference_steps=40, guidance_scale=6.5,
                   width=1024, height=1024, generator=g).images[0]
        base = os.path.join(out_dir, f"{key}_s{seed}")
        img.save(base + ".png")
        tiled(img).resize((1024, 1024)).save(base + "_tiled.png")
        print(f"[surfaces] {key} seed {seed} -> {base}.png (+_tiled)", flush=True)
print("[surfaces] done", flush=True)
