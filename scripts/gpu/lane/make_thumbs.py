#!/usr/bin/env python
"""Downscaled thumbnails (for artifact data-URI embedding) of the graphics-pass
deliverables. Writes to io/thumbs/."""
import os, glob
from PIL import Image
NS = "/lambda/nfs/red-hope-east/red_hope"
OUT = f"{NS}/io/thumbs"; os.makedirs(OUT, exist_ok=True)

def thumb(src, dst, w):
    if not os.path.exists(src):
        print("MISS", src); return
    im = Image.open(src).convert("RGB")
    im.thumbnail((w, w * 3))  # cap width; keep aspect
    im.save(dst, "JPEG", quality=82)
    print("ok", os.path.basename(dst), im.size)

# 20 colonists (accent pass, s8100)
CH = ["cmdr_vale","eng_ruiz","geo_okafor","bot_lindqvist","med_haddad","tech_park",
      "pilot_reyes","quart_bello","res_novak","fab_stone","comms_diallo","hydro_mensah",
      "reactor_ito","rookie_shaw","vet_kowalski","safety_abara","driver_costa","survey_khan",
      "cook_moreau","xeno_adeyemi"]
for k in CH:
    thumb(f"{NS}/io/chars2/{k}_s8100.png", f"{OUT}/char_{k}.jpg", 300)

# 5 surface picks (base tile)
SURF = {"wall_panel":5497,"wall_hex":5300,"floor_deck":5497,"floor_sealed":5497,"floor_hazard":5497}
for k,s in SURF.items():
    thumb(f"{NS}/io/surfaces/{k}_s{s}.png", f"{OUT}/surf_{k}.jpg", 240)

# furnish + dress mesh previews (three_quarter)
for k in ["airlock","hygiene","sciencebench","diningbooth","reclounge","exercise","electronics","bed","repairbench"]:
    thumb(f"{NS}/io/queue/furnish/out/{k}_preview_three_quarter.png", f"{OUT}/prop_{k}.jpg", 240)
for k in ["crate","ceilinglight","vent","drum"]:
    thumb(f"{NS}/io/queue/dress/out/{k}_preview_three_quarter.png", f"{OUT}/prop_{k}.jpg", 240)
print("thumbs done ->", OUT)
