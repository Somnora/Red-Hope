#!/usr/bin/env python
"""Style-lock (2D): SDXL + IP-Adapter turns one rough sprite into a clean, consistent
multi-ANGLE reference sheet in the source's own style. IP-Adapter locks palette/identity
from the reference image; per-angle text prompts drive the view. Output feeds gen-3d.

Usage: python rh_stylelock.py <ref.png> <out_dir> <subject> [ip_scale] [seed]
  subject: short noun phrase, e.g. "a white-armored humanoid robot"
Consistency note: front/3-quarter read best; back is partly inferred (inherent to a
single-image condition). For a hero, generate once + rig, don't regenerate."""
import sys, os
import torch
from PIL import Image
from diffusers import StableDiffusionXLPipeline, DDIMScheduler
from transformers import CLIPVisionModelWithProjection

ref_path = sys.argv[1]
out_dir  = sys.argv[2]
subject  = sys.argv[3] if len(sys.argv) > 3 else "the subject"
ip_scale = float(sys.argv[4]) if len(sys.argv) > 4 else 0.75
seed     = int(sys.argv[5]) if len(sys.argv) > 5 else 20260709
os.makedirs(out_dir, exist_ok=True)

ENC = "h94/IP-Adapter"
image_encoder = CLIPVisionModelWithProjection.from_pretrained(
    ENC, subfolder="models/image_encoder", torch_dtype=torch.float16)

pipe = StableDiffusionXLPipeline.from_pretrained(
    "stabilityai/stable-diffusion-xl-base-1.0",
    image_encoder=image_encoder, torch_dtype=torch.float16, variant="fp16")
pipe.scheduler = DDIMScheduler.from_config(pipe.scheduler.config)
# vit-h adapter pairs with the models/image_encoder (ViT-H, 1024-dim) loaded above.
pipe.load_ip_adapter(ENC, subfolder="sdxl_models",
                     weight_name="ip-adapter_sdxl_vit-h.bin")
pipe.set_ip_adapter_scale(ip_scale)
pipe = pipe.to("cuda")
pipe.set_progress_bar_config(disable=True)

ref = Image.open(ref_path).convert("RGB")

# Clean studio reference-sheet framing; IP-Adapter carries the actual design.
BASE = (f"{subject}, full body, centered, clean studio product shot, "
        "neutral light grey background, even soft lighting, sharp focus, "
        "highly detailed, concept art turnaround sheet, orthographic")
NEG  = ("blurry, low quality, extra limbs, duplicated, watermark, text, "
        "busy background, dramatic shadows, cropped, cut off")

ANGLES = [
    ("front",        "front view, facing the camera directly"),
    ("three_quarter","3/4 front view, turned 45 degrees"),
    ("side",         "side profile view, facing left"),
    ("back",         "back view, facing away from the camera"),
]

print(f"[style] ref={os.path.basename(ref_path)} subject='{subject}' ip_scale={ip_scale}", flush=True)
tiles = []
for i, (name, angle) in enumerate(ANGLES):
    g = torch.Generator("cuda").manual_seed(seed + i)
    img = pipe(prompt=f"{BASE}, {angle}", negative_prompt=NEG,
               ip_adapter_image=ref, num_inference_steps=34,
               guidance_scale=6.5, width=832, height=1216, generator=g).images[0]
    p = os.path.join(out_dir, f"{name}.png")
    img.save(p); tiles.append(p)
    print(f"[style] {name} -> {p}", flush=True)

# stitch a contact sheet (PIL is present in this venv)
imgs = [Image.open(t) for t in tiles]
W = sum(i.width for i in imgs); Hh = max(i.height for i in imgs)
sheet = Image.new("RGB", (W, Hh), (30, 30, 34))
x = 0
for im in imgs:
    sheet.paste(im, (x, 0)); x += im.width
sheet_path = os.path.join(out_dir, "sheet.png")
sheet.save(sheet_path)
print(f"[style] contact sheet -> {sheet_path} ({W}x{Hh})", flush=True)
print("[style] DONE", flush=True)
