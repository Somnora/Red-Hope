#!/usr/bin/env python
"""20 unique Mars-colony crew, full-body standing A-pose front views for meshing.
InstantStyle scoped to the style block: the humanoid sprite anchors ART STYLE
(cel shading, crisp linework, white/grey palette), the prompt drives WHO the
colonist is. Diverse crew: varied role, build, gender, age, skin, suit accent.

Usage: rh_chars.py <style_anchor.png> <out_dir> [n_seeds=2]
Outputs <key>_s<seed>.png; operator selects one per key for the mesh queue.
"""
import sys, os, torch
from PIL import Image
from diffusers import StableDiffusionXLPipeline, DDIMScheduler
from transformers import CLIPVisionModelWithProjection

ref_path, out_dir = sys.argv[1], sys.argv[2]
n_seeds = int(sys.argv[3]) if len(sys.argv) > 3 else 2
os.makedirs(out_dir, exist_ok=True)

# Character framing differs from props: we WANT one whole figure, head-to-feet,
# arms held slightly away (A-pose) so the mesh separates limbs from torso.
STYLE = ("cel shaded character art, crisp dark ink outlines, clean flat shading, "
         "full body head to feet, standing A-pose facing forward, symmetrical, "
         "grey-white sci-fi palette, single isolated character, centered, "
         "plain flat grey background")
NEG = ("multiple people, crowd, group, two people, cropped, close-up, portrait, "
       "bust, headshot, sitting, back view, side view, ground, dirt, terrain, "
       "plinth, floating, photorealistic, blurry, extra limbs, deformed hands, text")

# 20 distinct colonists. Each: build/age/skin + role suit + one accent colour.
CHARS = {
  "cmdr_vale":     "an older stern mission commander, tall, short grey hair, dark navy command jumpsuit with gold shoulder trim",
  "eng_ruiz":      "a stocky chief engineer, brown skin, cropped black hair, orange hi-vis work jumpsuit with a tool belt",
  "geo_okafor":    "a lean field geologist, dark skin, short dreadlocks, tan utility field suit with a sample satchel",
  "bot_lindqvist": "a woman botanist, pale skin, blonde braid, olive-green jumpsuit with a gardening apron and gloves",
  "med_haddad":    "a calm field medic, olive skin, short dark hair, white medical suit with a red cross armband",
  "tech_park":     "a young systems technician, east asian, black undercut hair, grey jumpsuit with teal accents holding a tablet",
  "pilot_reyes":   "a confident pilot, brown skin, slick black hair, grey flight suit with silver piping",
  "quart_bello":   "a practical quartermaster, dark skin, buzz cut, brown utility vest over a jumpsuit with many pockets",
  "res_novak":     "a thoughtful researcher, pale skin, glasses, brown hair, blue-accented jumpsuit under an open lab coat",
  "fab_stone":     "a burly fabricator, tanned skin, bald with a beard, heavy leather work apron and a welding visor pushed up",
  "comms_diallo":  "a slim comms officer, dark skin, a headset, short natural hair, jumpsuit with violet accents",
  "hydro_mensah":  "a hydroponics tech, brown skin, rolled sleeves, green jumpsuit with soil-stained gloves",
  "reactor_ito":   "a reactor technician, east asian, safety goggles, grey suit with yellow radiation-hazard trim",
  "rookie_shaw":   "a youthful rookie recruit, freckled pale skin, ginger hair, plain light-grey trainee jumpsuit",
  "vet_kowalski":  "a grizzled veteran, weathered white skin, grey stubble, a well-worn patched dark jumpsuit",
  "safety_abara":  "a safety officer, dark skin, a hard hat, high-visibility yellow-and-orange vest over a jumpsuit",
  "driver_costa":  "a rover driver, olive skin, goggles on forehead, dusty tan jumpsuit and heavy gloves",
  "survey_khan":   "a surveyor, brown skin, a peaked cap, khaki field suit holding a survey instrument",
  "cook_moreau":   "a galley cook, pale skin, rounder build, a white apron over a light jumpsuit",
  "xeno_adeyemi":  "a xenology researcher, dark skin, curious expression, a distinctive teal-and-violet science suit",
}

enc = "h94/IP-Adapter"
image_encoder = CLIPVisionModelWithProjection.from_pretrained(
    enc, subfolder="models/image_encoder", torch_dtype=torch.float16)
pipe = StableDiffusionXLPipeline.from_pretrained(
    "stabilityai/stable-diffusion-xl-base-1.0",
    image_encoder=image_encoder, torch_dtype=torch.float16, variant="fp16")
pipe.scheduler = DDIMScheduler.from_config(pipe.scheduler.config)
pipe.load_ip_adapter(enc, subfolder="sdxl_models", weight_name="ip-adapter_sdxl_vit-h.bin")
pipe.set_ip_adapter_scale({"up": {"block_0": [0.0, 1.0, 0.0]}})  # InstantStyle: style only
pipe = pipe.to("cuda"); pipe.set_progress_bar_config(disable=True)

ref = Image.open(ref_path).convert("RGB")
print(f"[chars] {len(CHARS)} colonists x {n_seeds} seeds, InstantStyle", flush=True)
for key, subj in CHARS.items():
    for s in range(n_seeds):
        seed = 8100 + s * 173
        g = torch.Generator("cuda").manual_seed(seed)
        img = pipe(prompt=f"{subj}, {STYLE}", negative_prompt=NEG, ip_adapter_image=ref,
                   num_inference_steps=38, guidance_scale=7.0,
                   width=832, height=1216, generator=g).images[0]  # portrait aspect for full body
        p = os.path.join(out_dir, f"{key}_s{seed}.png")
        img.save(p)
        print(f"[chars] {key} seed {seed} -> {p}", flush=True)
print("[chars] done", flush=True)
