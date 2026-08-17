#!/usr/bin/env python
"""Colonist pass 3: identical crew/accents/pose to pass 2, but the background
HALO is negated at the source. Pass-2 sprites had a soft spotlight behind each
figure; Hunyuan's bg-removal read it as foreground and meshed a giant orb.
Anti-halo negatives + a 'solid uniform background' cue kill it. Same seed 8100
so the figure is the one already approved.

Usage: rh_chars3.py <style_anchor.png> <out_dir>
"""
import sys, os, torch
from PIL import Image
from diffusers import StableDiffusionXLPipeline, DDIMScheduler
from transformers import CLIPVisionModelWithProjection

ref_path, out_dir = sys.argv[1], sys.argv[2]
os.makedirs(out_dir, exist_ok=True)

STYLE = ("cel shaded character art, crisp dark ink outlines, clean flat shading, "
         "full body head to feet, standing A-pose facing forward, symmetrical, "
         "white and grey sci-fi suit with bold coloured accent panels, "
         "single isolated character, centered, "
         "solid uniform flat light-grey background, empty background")
# anti-halo: everything that would put a shape/glow behind the figure.
NEG = ("vignette, spotlight, halo, glow, glowing background, backdrop, background gradient, "
       "radial gradient, stage light, rim light behind, circle behind, orb, sphere behind, "
       "shadow disc, multiple people, crowd, two people, turnaround, model sheet, cropped, "
       "close-up, portrait, bust, sitting, back view, side view, ground, dirt, terrain, "
       "plinth, floating, photorealistic, blurry, extra limbs, deformed hands, greyscale, text")

CHARS = {
  "cmdr_vale":     "an older stern mission commander, tall, short grey hair, white command suit with bold gold shoulder and chest panels",
  "eng_ruiz":      "a stocky chief engineer, brown skin, cropped black hair, white work suit with bold safety-orange panels and a tool belt",
  "geo_okafor":    "a lean field geologist, dark skin, short dreadlocks, white field suit with bold bronze-brown panels and a sample satchel",
  "bot_lindqvist": "a woman botanist, pale skin, blonde braid, white suit with bold leaf-green panels and an apron",
  "med_haddad":    "a calm field medic, olive skin, short dark hair, white medical suit with bold red cross panels",
  "tech_park":     "a young systems technician, east asian, black undercut hair, white suit with bold cyan panels",
  "pilot_reyes":   "a confident pilot, brown skin, slick black hair, white flight suit with bold steel-blue panels",
  "quart_bello":   "a practical quartermaster, dark skin, buzz cut, white suit with bold olive-khaki panels and many pockets",
  "res_novak":     "a thoughtful researcher, pale skin, glasses, brown hair, white suit with bold indigo-blue panels under an open lab coat",
  "fab_stone":     "a burly fabricator, tanned skin, bald with a beard, white suit with bold deep-orange panels and a heavy work apron",
  "comms_diallo":  "a slim comms officer, dark skin, a headset, short natural hair, white suit with bold violet panels",
  "hydro_mensah":  "a hydroponics tech, brown skin, rolled sleeves, white suit with bold lime-green panels and gloves",
  "reactor_ito":   "a reactor technician, east asian, safety goggles, white suit with bold hazard-yellow panels",
  "rookie_shaw":   "a youthful rookie recruit, freckled pale skin, ginger hair, plain white trainee suit with thin pale-grey trim",
  "vet_kowalski":  "a grizzled veteran, weathered white skin, grey stubble, a worn white suit with faded maroon panels",
  "safety_abara":  "a safety officer, dark skin, a hard hat, white suit with bold high-visibility yellow-and-orange panels",
  "driver_costa":  "a rover driver, olive skin, goggles on forehead, white suit with bold amber panels and heavy gloves",
  "survey_khan":   "a surveyor, brown skin, a peaked cap, white field suit with bold teal panels",
  "cook_moreau":   "a galley cook, pale skin, rounder build, a white suit with a white apron and red collar accents",
  "xeno_adeyemi":  "a xenology researcher, dark skin, white science suit with bold teal-and-violet panels",
}

enc = "h94/IP-Adapter"
image_encoder = CLIPVisionModelWithProjection.from_pretrained(
    enc, subfolder="models/image_encoder", torch_dtype=torch.float16)
pipe = StableDiffusionXLPipeline.from_pretrained(
    "stabilityai/stable-diffusion-xl-base-1.0",
    image_encoder=image_encoder, torch_dtype=torch.float16, variant="fp16")
pipe.scheduler = DDIMScheduler.from_config(pipe.scheduler.config)
pipe.load_ip_adapter(enc, subfolder="sdxl_models", weight_name="ip-adapter_sdxl_vit-h.bin")
pipe.set_ip_adapter_scale({"up": {"block_0": [0.0, 0.55, 0.0]}})
pipe = pipe.to("cuda"); pipe.set_progress_bar_config(disable=True)

ref = Image.open(ref_path).convert("RGB")
print(f"[chars3] {len(CHARS)} colonists, anti-halo, seed 8100", flush=True)
for key, subj in CHARS.items():
    g = torch.Generator("cuda").manual_seed(8100)
    img = pipe(prompt=f"{subj}, {STYLE}", negative_prompt=NEG, ip_adapter_image=ref,
               num_inference_steps=38, guidance_scale=7.5,
               width=832, height=1216, generator=g).images[0]
    img.save(os.path.join(out_dir, f"{key}_s8100.png"))
    print(f"[chars3] {key}", flush=True)
print("[chars3] done", flush=True)
