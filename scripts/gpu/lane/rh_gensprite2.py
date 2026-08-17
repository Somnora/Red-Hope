#!/usr/bin/env python
"""v2: InstantStyle. v1 failed - IP-Adapter at scale 0.55 carried the REFERENCE'S CONTENT
(every output was a recolored battery rack), because IP-Adapter injects into every
cross-attention block. InstantStyle fixes this by scoping the adapter to ONLY the
style-bearing block (up.block_0 attn index 1), so the reference supplies style/palette
and the TEXT PROMPT supplies the subject.

Also: v1's negative prompt was 78 tokens and CLIP silently truncated it at 77. Trimmed.
"""
import sys, os, torch
from PIL import Image
from diffusers import StableDiffusionXLPipeline, DDIMScheduler
from transformers import CLIPVisionModelWithProjection

ref_path = sys.argv[1]
out_dir  = sys.argv[2]
n_seeds  = int(sys.argv[3]) if len(sys.argv) > 3 else 2
os.makedirs(out_dir, exist_ok=True)

STYLE = ("isometric game asset, 3/4 isometric view, cel shaded, crisp dark ink outlines, "
         "flat clean shading, grey-white industrial hull, orange hazard accents, "
         "single isolated object, centered, plain flat grey background")
# <77 CLIP tokens: leading terms are the ones that matter most.
NEG = ("ground, dirt, soil, terrain, regolith, rock base, plinth, floating island, "
       "diorama, cutaway, cross-section, pit, room interior, walls, hangar, "
       "photorealistic, 3d render, blurry, text, multiple objects")

SUBJECTS = {
  "extractor": "a heavy tracked ore mining excavator with a digging arm and conveyor belt",
  "habitat":   "a sealed cylindrical mars habitat module with an airlock door and viewports",
  "stockpile": "a stack of cargo supply crates on a steel skid",
  "solar":     "a solar array of four large tilted photovoltaic panels on a steel frame",
  "lander":    "a cargo rocket lander standing on four splayed landing legs",
}

enc = "h94/IP-Adapter"
image_encoder = CLIPVisionModelWithProjection.from_pretrained(
    enc, subfolder="models/image_encoder", torch_dtype=torch.float16)
pipe = StableDiffusionXLPipeline.from_pretrained(
    "stabilityai/stable-diffusion-xl-base-1.0",
    image_encoder=image_encoder, torch_dtype=torch.float16, variant="fp16")
pipe.scheduler = DDIMScheduler.from_config(pipe.scheduler.config)
pipe.load_ip_adapter(enc, subfolder="sdxl_models", weight_name="ip-adapter_sdxl_vit-h.bin")
# InstantStyle: only up.block_0's second attn processor is the "style" layer.
# Everything else -> 0.0, so the reference cannot inject its content/layout.
pipe.set_ip_adapter_scale({"up": {"block_0": [0.0, 1.0, 0.0]}})
pipe = pipe.to("cuda")
pipe.set_progress_bar_config(disable=True)

ref = Image.open(ref_path).convert("RGB")
print("[gen2] InstantStyle style-only (up.block_0=[0,1,0])", flush=True)
for key, subj in SUBJECTS.items():
    for s in range(n_seeds):
        seed = 4200 + s * 137
        g = torch.Generator("cuda").manual_seed(seed)
        img = pipe(prompt=f"{subj}, {STYLE}", negative_prompt=NEG,
                   ip_adapter_image=ref, num_inference_steps=36,
                   guidance_scale=7.5, width=1024, height=1024, generator=g).images[0]
        img.save(os.path.join(out_dir, f"{key}_s{seed}.png"))
        print(f"[gen2] {key} seed={seed}", flush=True)
print("[gen2] DONE", flush=True)
