#!/usr/bin/env python
"""Contact sheets for batch 2: characters (2 sheets of 10), surfaces (tiled),
dressing props."""
import os
from PIL import Image, ImageDraw

NS = "/lambda/nfs/red-hope-east/red_hope"
PAD, LBL = 6, 18

def sheet(rows, cols, path_fn, label_fn, out, cell=300):
    W = len(cols) * (cell + PAD) + PAD
    H = len(rows) * (cell + PAD + LBL) + PAD
    canvas = Image.new("RGB", (W, H), (28, 28, 32))
    d = ImageDraw.Draw(canvas)
    for r, rk in enumerate(rows):
        for c, ck in enumerate(cols):
            p = path_fn(rk, ck)
            x = PAD + c * (cell + PAD)
            y = PAD + r * (cell + PAD + LBL)
            if os.path.exists(p):
                im = Image.open(p).convert("RGB"); im.thumbnail((cell, cell))
                canvas.paste(im, (x + (cell - im.width)//2, y + LBL + (cell - im.height)//2))
            d.text((x + 2, y + 3), label_fn(rk, ck), fill=(230, 230, 120))
    canvas.save(out); print(f"wrote {out} ({W}x{H})", flush=True)

CHARS = ["cmdr_vale","eng_ruiz","geo_okafor","bot_lindqvist","med_haddad","tech_park",
         "pilot_reyes","quart_bello","res_novak","fab_stone","comms_diallo","hydro_mensah",
         "reactor_ito","rookie_shaw","vet_kowalski","safety_abara","driver_costa","survey_khan",
         "cook_moreau","xeno_adeyemi"]
CSEEDS = [8100, 8273]
sheet(CHARS[:10], CSEEDS, lambda k,s: f"{NS}/io/chars/{k}_s{s}.png",
      lambda k,s: f"{k} s{s}", f"{NS}/io/chars_contact_A.png", cell=300)
sheet(CHARS[10:], CSEEDS, lambda k,s: f"{NS}/io/chars/{k}_s{s}.png",
      lambda k,s: f"{k} s{s}", f"{NS}/io/chars_contact_B.png", cell=300)

WK = ["wall_panel","wall_hex","wall_ribbed","floor_deck","floor_sealed","floor_hazard"]
WS = [5300, 5497, 5694]
sheet(WK, WS, lambda k,s: f"{NS}/io/surfaces/{k}_s{s}_tiled.png",
      lambda k,s: f"{k} s{s} (2x2)", f"{NS}/io/surfaces_tiled_contact.png", cell=310)

DK = ["crate","door","ceilinglight","vent","drum"]
DS = [9200, 9331]
sheet(DK, DS, lambda k,s: f"{NS}/io/dress/{k}_s{s}.png",
      lambda k,s: f"{k} s{s}", f"{NS}/io/dress_contact.png", cell=300)
print("all sheets done", flush=True)
