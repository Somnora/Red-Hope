#!/usr/bin/env python
"""Prop kit for player-designated room cells (10 m deck panels, viewed from the
strategic slice camera). Rooms are DesignateRoom'd at runtime in arbitrary shapes,
so a room interior can never be prefabbed -- but one hero prop per room type can be
instanced per cell. Style anchored on the drone sprite (house palette, ground-free);
the ModularBlock interior is the furniture reference for what these objects ARE.

InstantStyle: adapter scoped to the style block only, so the prompt drives content.
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
       "floor plan, photorealistic, 3d render, blurry, text")

# One hero prop per active room type (RH_Rooms.csv). Garden ships two states to
# match the sim's existing Garden / Garden#planted visual distinction.
PROPS = {
  "prop_bunk":        "one single sci-fi crew bunk bed, one metal bed frame with a mattress and a storage locker",
  "prop_labbench":    "one single laboratory workbench with microscope and equipment on it",
  "prop_console":     "one single industrial control console workstation desk with monitors",
  "prop_diningtable": "one single dining table with four chairs around it",
  "prop_galley":      "one single galley kitchen counter unit with a cooktop and cabinets",
  "prop_planter_dry": "one single empty rectangular hydroponic planter tray of bare tilled soil",
  "prop_planter_wet": "one single hydroponic planter tray full of lush green leafy crops",
  "prop_conduit":     "one single low floor light strip and pipe conduit segment, a hallway fixture",
  "prop_tank":        "one single industrial fluid storage tank with a pump and valves",
  "prop_locker":      "one single tall storage locker cabinet with equipment racks",
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
print(f"[props] {len(PROPS)} props x {n_seeds} seeds, InstantStyle", flush=True)
for key, subj in PROPS.items():
    for s in range(n_seeds):
        seed = 6100 + s * 167
        g = torch.Generator("cuda").manual_seed(seed)
        img = pipe(prompt=f"{subj}, {STYLE}", negative_prompt=NEG, ip_adapter_image=ref,
                   num_inference_steps=36, guidance_scale=7.5,
                   width=1024, height=1024, generator=g).images[0]
        img.save(os.path.join(out_dir, f"{key}_s{seed}.png"))
        print(f"[props] {key} seed={seed}", flush=True)
print("[props] DONE", flush=True)
