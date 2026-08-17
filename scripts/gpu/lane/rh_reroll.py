#!/usr/bin/env python
"""Single-object reroll for the 3 props that meshed as clusters/exploded parts
(bed, repairbench, door). Same InstantStyle recipe as rh_furnish/rh_dress, but
prompts hammer 'ONE single assembled object' and negatives ban the secondary
objects that caused the multi-mesh. 4 seeds each for a good pick.

Usage: rh_reroll.py <ref_sprite.png> <out_dir>
"""
import sys, os, torch
from PIL import Image
from diffusers import StableDiffusionXLPipeline, DDIMScheduler
from transformers import CLIPVisionModelWithProjection

ref_path, out_dir = sys.argv[1], sys.argv[2]
os.makedirs(out_dir, exist_ok=True)

STYLE = ("isometric game asset, 3/4 isometric view, cel shaded, crisp dark ink outlines, "
         "flat clean shading, grey-white industrial hull, orange hazard accents, "
         "ONE single assembled object, one whole piece, centered, "
         "solid uniform flat grey background, empty background")
# ban the specific secondary objects each one spawned, plus the exploded look.
NEG = ("multiple objects, two objects, group, collection, set, pair, separate parts, "
       "exploded view, disassembled, components apart, extra stool, extra chair, "
       "second bed, duplicate, scattered, ground, dirt, terrain, plinth, floating island, "
       "diorama, cutaway, room interior, walls, floor plan, halo, glow, vignette, "
       "photorealistic, blurry, text, people, person")

PROPS = {
  "bed":        "one single sci-fi crew bed, one low platform bed with one thick padded mattress "
                "and a headboard, a single complete bed unit",
  "repairbench":"one single mechanic repair workbench, one sturdy metal bench with a bench vise "
                "and tools on a pegboard, one complete workbench, no stool",
  "door":       "one single closed sliding hab door, one thick rectangular pressure door set in "
                "one bulkhead frame, one complete assembled doorway unit, door shut",
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
print(f"[reroll] {len(PROPS)} props x 4 seeds, single-object emphasis", flush=True)
for key, subj in PROPS.items():
    for s in range(4):
        seed = 3300 + s * 137
        g = torch.Generator("cuda").manual_seed(seed)
        img = pipe(prompt=f"{subj}, {STYLE}", negative_prompt=NEG, ip_adapter_image=ref,
                   num_inference_steps=36, guidance_scale=8.0,  # higher CFG = tighter prompt adherence
                   width=1024, height=1024, generator=g).images[0]
        img.save(os.path.join(out_dir, f"{key}_s{seed}.png"))
        print(f"[reroll] {key} seed {seed}", flush=True)
print("[reroll] done", flush=True)
