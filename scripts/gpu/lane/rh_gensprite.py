#!/usr/bin/env python
"""Generate STANDALONE isometric building sprites in the Red Hope illustrated style.

Fixes the diorama problem: the source art for several buildings baked the ground IN
(excavation pits, cutaway rooms, floating regolith islands), which meshes into a plinth
that never seats on the real terrain. Here IP-Adapter locks the established style from a
clean on-style reference (battery/forge) while the text prompt drives NEW content, and the
negative prompt hard-rejects ground/dirt/base/diorama/cutaway.

Usage: python rh_gensprite.py <ref.png> <out_dir> [n_seeds]
Writes <out_dir>/<key>_s<seed>.png for each subject x seed, for director pick.
"""
import sys, os, torch
from PIL import Image
from diffusers import StableDiffusionXLPipeline, DDIMScheduler
from transformers import CLIPVisionModelWithProjection

ref_path = sys.argv[1]
out_dir  = sys.argv[2]
n_seeds  = int(sys.argv[3]) if len(sys.argv) > 3 else 3
IP_SCALE = 0.55   # low enough that the PROMPT drives content, style still hugs the ref
os.makedirs(out_dir, exist_ok=True)

# The house style, read off the on-style sprites (battery/forge/ice/drone).
STYLE = ("isometric game asset illustration, 3/4 isometric view, cel shaded, "
         "crisp dark ink outlines, flat clean shading, muted grey-white industrial hull, "
         "orange and yellow hazard accents, cyan tech panel accents, highly detailed, "
         "single isolated object, centered, plain flat light grey background")

# Hard-reject everything that caused the plinth/diorama failure.
NEG = ("ground, dirt, soil, terrain, sand, regolith, rock base, plinth, pedestal, "
       "floating island, diorama, cutaway, cross-section, pit, excavation, hole, "
       "room interior, floor plan, walls, hangar bay, landing pad, tan desert soil, "
       "photorealistic, 3d render, raytraced, smooth gradient, no outlines, "
       "blurry, low quality, watermark, text, cropped, cut off, multiple objects")

SUBJECTS = {
  "extractor": "a heavy ore mining excavator machine, tracked chassis, digging arm, conveyor belt",
  "habitat":   "a sealed modular mars habitat module, curved roof, airlock door, small viewports",
  "stockpile": "a neat stack of supply crates and cargo containers on a low steel skid",
  "solar":     "a solar array, four tilted photovoltaic panels on a steel support frame",
  "lander":    "a cargo lander spacecraft standing upright on four landing legs, open cargo hatch",
}

enc = "h94/IP-Adapter"
image_encoder = CLIPVisionModelWithProjection.from_pretrained(
    enc, subfolder="models/image_encoder", torch_dtype=torch.float16)
pipe = StableDiffusionXLPipeline.from_pretrained(
    "stabilityai/stable-diffusion-xl-base-1.0",
    image_encoder=image_encoder, torch_dtype=torch.float16, variant="fp16")
pipe.scheduler = DDIMScheduler.from_config(pipe.scheduler.config)
pipe.load_ip_adapter(enc, subfolder="sdxl_models", weight_name="ip-adapter_sdxl_vit-h.bin")
pipe.set_ip_adapter_scale(IP_SCALE)
pipe = pipe.to("cuda")
pipe.set_progress_bar_config(disable=True)

ref = Image.open(ref_path).convert("RGB")
print(f"[gensprite] ref={os.path.basename(ref_path)} ip_scale={IP_SCALE} seeds={n_seeds}", flush=True)
for key, subj in SUBJECTS.items():
    for s in range(n_seeds):
        seed = 7000 + s * 101
        g = torch.Generator("cuda").manual_seed(seed)
        img = pipe(prompt=f"{subj}, {STYLE}", negative_prompt=NEG,
                   ip_adapter_image=ref, num_inference_steps=36,
                   guidance_scale=7.0, width=1024, height=1024, generator=g).images[0]
        p = os.path.join(out_dir, f"{key}_s{seed}.png")
        img.save(p)
        print(f"[gensprite] {key} seed={seed} -> {os.path.basename(p)}", flush=True)
print("[gensprite] DONE", flush=True)
