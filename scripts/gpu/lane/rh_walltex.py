#!/usr/bin/env python
"""Seamless tiling material textures for the above-ground habs (director ask:
insulated, 'pillowy' padded walls like sci-fi movie interiors, plus a matching
soft deck floor). NOT the object pipeline -- no IP-Adapter framing. Instead every
Conv2d in the UNet + VAE is switched to circular padding so the generation tiles
seamlessly on all edges. Base-color tiles; a normal map is a later add.

Usage: rh_walltex.py <out_dir> [n_seeds=3]
Saves <key>_s<seed>.png (the tile) and <key>_s<seed>_tiled.png (a 2x2 layout so
seams are visible on review).
"""
import sys, os, torch
from PIL import Image
from diffusers import StableDiffusionXLPipeline

out_dir = sys.argv[1]
n_seeds = int(sys.argv[2]) if len(sys.argv) > 2 else 3
os.makedirs(out_dir, exist_ok=True)

TEX = {
  "habwall_quilted": "seamless tileable texture of white quilted padded wall insulation, "
                     "soft puffy diamond-stitched fabric panels, spacecraft interior padding, "
                     "clean off-white, evenly softly lit, flat orthographic top-down, matte",
  "habwall_padded":  "seamless tileable texture of a padded upholstered wall of large soft "
                     "rounded cushions in rows, bone-white insulated habitat lining, "
                     "evenly softly lit, flat orthographic top-down, matte",
  "habfloor_soft":   "seamless tileable texture of a soft rubberized deck floor, light grey "
                     "padded panel flooring with subtle rectangular seams, evenly lit, "
                     "flat orthographic top-down, matte",
}
NEG = ("perspective, room photo, furniture, object, people, person, text, watermark, logo, "
       "dark shadows, vignette, uneven lighting, border, frame, 3d render angle")

pipe = StableDiffusionXLPipeline.from_pretrained(
    "stabilityai/stable-diffusion-xl-base-1.0",
    torch_dtype=torch.float16, variant="fp16").to("cuda")
pipe.set_progress_bar_config(disable=True)

# Seamless tiling: circular padding on every conv in the denoiser and the decoder.
patched = 0
for mod in list(pipe.unet.modules()) + list(pipe.vae.modules()):
    if isinstance(mod, torch.nn.Conv2d):
        mod.padding_mode = "circular"
        patched += 1
print(f"[walltex] circular padding on {patched} conv layers", flush=True)

def tiled(img, n=2):
    w, h = img.size
    canvas = Image.new("RGB", (w * n, h * n))
    for i in range(n):
        for j in range(n):
            canvas.paste(img, (i * w, j * h))
    return canvas

print(f"[walltex] {len(TEX)} textures x {n_seeds} seeds", flush=True)
for key, prompt in TEX.items():
    for s in range(n_seeds):
        seed = 4200 + s * 211
        g = torch.Generator("cuda").manual_seed(seed)
        img = pipe(prompt=prompt, negative_prompt=NEG,
                   num_inference_steps=40, guidance_scale=6.5,
                   width=1024, height=1024, generator=g).images[0]
        base = os.path.join(out_dir, f"{key}_s{seed}")
        img.save(base + ".png")
        tiled(img).resize((1024, 1024)).save(base + "_tiled.png")
        print(f"[walltex] {key} seed {seed} -> {base}.png (+_tiled)", flush=True)
print("[walltex] done", flush=True)
