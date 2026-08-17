#!/usr/bin/env python
"""Furnish batch: workstation + living-kit hero props for player-designated rooms.
Same contract as rh_props.py -- InstantStyle scoped to the style block so the prompt
drives content, anchored on the drone sprite (house palette, ground-free). One hero
prop per function; the sim instances it per designated cell.

Usage: rh_furnish.py <ref_sprite.png> <out_dir> [n_seeds=3]
Outputs <key>_s<seed>.png candidates; the operator selects one per key and drops it
into io/queue/furnish/in/<key>.png for meshing.
"""
import sys, os, torch
from PIL import Image
from diffusers import StableDiffusionXLPipeline, DDIMScheduler
from transformers import CLIPVisionModelWithProjection

ref_path, out_dir = sys.argv[1], sys.argv[2]
n_seeds = int(sys.argv[3]) if len(sys.argv) > 3 else 3
os.makedirs(out_dir, exist_ok=True)

STYLE = ("isometric game asset, 3/4 isometric view, cel shaded, crisp dark ink outlines, "
         "flat clean shading, grey-white industrial hull, orange hazard accents, "
         "single isolated object, centered, plain flat grey background")
NEG = ("multiple objects, group, collection, scattered, ground, dirt, soil, terrain, "
       "regolith, plinth, floating island, diorama, cutaway, room interior, walls, "
       "floor plan, photorealistic, 3d render, blurry, text, people, person")

# Clean keys = the eventual GLB/asset names. Workstations first, living kit second.
PROPS = {
  # --- workstations: develop / repair / invent to keep the Hab afloat ---
  "repairbench":  "one single mechanic repair workbench, a sturdy metal bench with a bench vise, "
                  "hand tools hung on a pegboard, and a parts bin",
  "electronics":  "one single electronics engineering workbench, a bench with a soldering station, "
                  "circuit boards, wiring, and small test instruments",
  "sciencebench": "one single science laboratory bench, a clean bench with a microscope, "
                  "a rack of sample vials, and analysis equipment",
  # --- living kit ---
  "bed":          "one single sci-fi crew bed, a low platform bed with a thick padded mattress "
                  "and a headboard shelf",
  "diningbooth":  "one single dining booth, a curved padded bench seat wrapped around a compact table",
  "reclounge":    "one single recreation lounge sofa with soft cushions and a low coffee table",
  "exercise":     "one single compact exercise machine, a resistance training rig with a seat "
                  "and a weight stack",
  "hygiene":      "one single enclosed shower and wash station pod, a compact sealed bathroom "
                  "cubicle unit with a door",
  "airlock":      "one single heavy sealed airlock hatch, a round pressure door set in a thick "
                  "bulkhead frame with locking wheel",
}

enc = "h94/IP-Adapter"
image_encoder = CLIPVisionModelWithProjection.from_pretrained(
    enc, subfolder="models/image_encoder", torch_dtype=torch.float16)
pipe = StableDiffusionXLPipeline.from_pretrained(
    "stabilityai/stable-diffusion-xl-base-1.0",
    image_encoder=image_encoder, torch_dtype=torch.float16, variant="fp16")
pipe.scheduler = DDIMScheduler.from_config(pipe.scheduler.config)
pipe.load_ip_adapter(enc, subfolder="sdxl_models", weight_name="ip-adapter_sdxl_vit-h.bin")
pipe.set_ip_adapter_scale({"up": {"block_0": [0.0, 1.0, 0.0]}})  # InstantStyle: style block only
pipe = pipe.to("cuda"); pipe.set_progress_bar_config(disable=True)

ref = Image.open(ref_path).convert("RGB")
print(f"[furnish] {len(PROPS)} props x {n_seeds} seeds, InstantStyle", flush=True)
for key, subj in PROPS.items():
    for s in range(n_seeds):
        seed = 7300 + s * 149
        g = torch.Generator("cuda").manual_seed(seed)
        img = pipe(prompt=f"{subj}, {STYLE}", negative_prompt=NEG, ip_adapter_image=ref,
                   num_inference_steps=36, guidance_scale=7.5,
                   width=1024, height=1024, generator=g).images[0]
        p = os.path.join(out_dir, f"{key}_s{seed}.png")
        img.save(p)
        print(f"[furnish] {key} seed {seed} -> {p}", flush=True)
print("[furnish] done", flush=True)
