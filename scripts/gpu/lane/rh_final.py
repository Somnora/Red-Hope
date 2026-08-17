#!/usr/bin/env python
"""Final art pass: buildings + room props, on a MAGENTA screen with no cast shadow.

Why magenta: the previous grey-background renders baked a soft cast shadow that rembg
kept, and Hunyuan meshed it into a thin ground plate -- a new plinth. Grey shadow and
grey hull share the same luminance, so no threshold separates them. Nothing in the
house palette is magenta, so an exact chroma key is possible instead.
InstantStyle keeps the reference supplying style only.
"""
import sys, os, torch
from PIL import Image
from diffusers import StableDiffusionXLPipeline, DDIMScheduler
from transformers import CLIPVisionModelWithProjection

ref_path, out_dir = sys.argv[1], sys.argv[2]
os.makedirs(out_dir, exist_ok=True)

STYLE = ("isometric game asset, 3/4 isometric view, cel shaded, crisp dark ink outlines, "
         "flat shading, grey-white industrial hull, orange accents, single isolated object, "
         "centered, solid magenta background, no shadow")
NEG = ("shadow, cast shadow, drop shadow, ground, dirt, soil, regolith, plinth, base, "
       "diorama, cutaway, pit, room interior, walls, multiple objects, scattered, "
       "photorealistic, 3d render, blurry, text")

SUBJECTS = {
 # building replacements (plinth-free)
 "extractor2": "one single heavy tracked mining excavator, digging arm",
 "habitat":    "one single sealed cylindrical mars habitat module, airlock door",
 "stockpile":  "one single tall stack of cargo crates in one pile",
 "lander2":    "one single cargo rocket lander on four landing legs",
 "solar2":     "one single solar power station, a central hub dome with four radiating panel wings",
 # room prop kit (one hero prop per active room type, instanced per 10 m cell)
 "prop_bunk":        "one single sci-fi crew bunk bed with a storage locker",
 "prop_labbench":    "one single laboratory workbench with a microscope",
 "prop_console":     "one single control console workstation desk with monitors",
 "prop_diningtable": "one single dining table with four chairs",
 "prop_galley":      "one single galley kitchen counter with a cooktop",
 "prop_planter_dry": "one single empty rectangular hydroponic planter tray of bare soil",
 "prop_planter_wet": "one single hydroponic planter tray full of lush green leafy crops",
 "prop_conduit":     "one single low floor light strip and pipe conduit segment",
 "prop_tank":        "one single industrial fluid storage tank with a pump",
 "prop_locker":      "one single tall storage locker cabinet",
}

enc = "h94/IP-Adapter"
ie = CLIPVisionModelWithProjection.from_pretrained(enc, subfolder="models/image_encoder", torch_dtype=torch.float16)
pipe = StableDiffusionXLPipeline.from_pretrained("stabilityai/stable-diffusion-xl-base-1.0",
        image_encoder=ie, torch_dtype=torch.float16, variant="fp16")
pipe.scheduler = DDIMScheduler.from_config(pipe.scheduler.config)
pipe.load_ip_adapter(enc, subfolder="sdxl_models", weight_name="ip-adapter_sdxl_vit-h.bin")
pipe.set_ip_adapter_scale({"up": {"block_0": [0.0, 1.0, 0.0]}})
pipe = pipe.to("cuda"); pipe.set_progress_bar_config(disable=True)
ref = Image.open(ref_path).convert("RGB")

print(f"[final] {len(SUBJECTS)} subjects, magenta screen, no shadow", flush=True)
for k, subj in SUBJECTS.items():
    for s in range(2):
        seed = 8800 + s * 193
        g = torch.Generator("cuda").manual_seed(seed)
        img = pipe(prompt=f"{subj}, {STYLE}", negative_prompt=NEG, ip_adapter_image=ref,
                   num_inference_steps=36, guidance_scale=7.5, width=1024, height=1024,
                   generator=g).images[0]
        img.save(os.path.join(out_dir, f"{k}_s{seed}.png"))
        print(f"[final] {k} s{seed}", flush=True)
print("[final] DONE", flush=True)
