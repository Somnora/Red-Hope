#!/usr/bin/env python
"""Colony dressing props (the 'other missing assets'): small set of hero objects
to furnish corridors and rooms. Same InstantStyle recipe as rh_furnish/rh_props,
drone-anchored, single isolated object.

Usage: rh_dress.py <ref_sprite.png> <out_dir> [n_seeds=2]
"""
import sys, os, torch
from PIL import Image
from diffusers import StableDiffusionXLPipeline, DDIMScheduler
from transformers import CLIPVisionModelWithProjection

ref_path, out_dir = sys.argv[1], sys.argv[2]
n_seeds = int(sys.argv[3]) if len(sys.argv) > 3 else 2
os.makedirs(out_dir, exist_ok=True)

STYLE = ("isometric game asset, 3/4 isometric view, cel shaded, crisp dark ink outlines, "
         "flat clean shading, grey-white industrial hull, orange hazard accents, "
         "single isolated object, centered, plain flat grey background")
NEG = ("multiple objects, group, collection, scattered, ground, dirt, soil, terrain, "
       "regolith, plinth, floating island, diorama, cutaway, room interior, walls, "
       "floor plan, photorealistic, 3d render, blurry, text, people, person")

PROPS = {
  "crate":       "one single stackable cargo storage crate, a rugged square container with latches",
  "door":        "one single sliding hab doorway module, a thick doorframe with a sliding pressure door",
  "ceilinglight":"one single ceiling light fixture panel, a flat recessed luminaire unit",
  "vent":        "one single wall air vent unit, a louvered ventilation grille housing with a fan",
  "drum":        "one single sealed industrial storage drum barrel with a ribbed body and lid",
}

enc = "h94/IP-Adapter"
image_encoder = CLIPVisionModelWithProjection.from_pretrained(
    enc, subfolder="models/image_encoder", torch_dtype=torch.float16)
pipe = StableDiffusionXLPipeline.from_pretrained(
    "stabilityai/stable-diffusion-xl-base-1.0",
    image_encoder=image_encoder, torch_dtype=torch.float16, variant="fp16")
pipe.scheduler = DDIMScheduler.from_config(pipe.scheduler.config)
pipe.load_ip_adapter(enc, subfolder="sdxl_models", weight_name="ip-adapter_sdxl_vit-h.bin")
pipe.set_ip_adapter_scale({"up": {"block_0": [0.0, 1.0, 0.0]}})
pipe = pipe.to("cuda"); pipe.set_progress_bar_config(disable=True)

ref = Image.open(ref_path).convert("RGB")
print(f"[dress] {len(PROPS)} props x {n_seeds} seeds, InstantStyle", flush=True)
for key, subj in PROPS.items():
    for s in range(n_seeds):
        seed = 9200 + s * 131
        g = torch.Generator("cuda").manual_seed(seed)
        img = pipe(prompt=f"{subj}, {STYLE}", negative_prompt=NEG, ip_adapter_image=ref,
                   num_inference_steps=36, guidance_scale=7.5,
                   width=1024, height=1024, generator=g).images[0]
        p = os.path.join(out_dir, f"{key}_s{seed}.png")
        img.save(p)
        print(f"[dress] {key} seed {seed} -> {p}", flush=True)
print("[dress] done", flush=True)
