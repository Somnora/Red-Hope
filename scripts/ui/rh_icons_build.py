"""rh_icons_build - the deck's icon set, drawn in code (one consistent line style).

  python3 scripts/ui/rh_icons_build.py [--out Martians/gen/ui_icons]

White line icons on transparency, drawn at 256 px and downsampled to 64 px
(clean anti-aliasing); the deck tints them per resource/state. Imported by
scripts/unreal/rh_import_ui.py as /Game/RedHope/UI/Icons/T_Icon_<name>.
The interface pass (2026-10-15) replaced the monospace text-dump HUD with
icon tiles; a found icon pack would mix styles, these stay one family.
"""
import argparse
import math
import os

from PIL import Image, ImageDraw, ImageFont

ap = argparse.ArgumentParser()
ap.add_argument("--out", default="Martians/gen/ui_icons")
OUT = os.path.abspath(ap.parse_args().out)
os.makedirs(OUT, exist_ok=True)
S = 256
W = 18          # stroke
C = (255, 255, 255, 255)


def canvas():
    im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    return im, ImageDraw.Draw(im)


def save(im, name):
    im.resize((64, 64), Image.LANCZOS).save(os.path.join(OUT, "T_Icon_%s.png" % name))


def font(sz):
    for p in ("/System/Library/Fonts/Supplemental/Arial Bold.ttf", "/System/Library/Fonts/Helvetica.ttc"):
        if os.path.exists(p):
            return ImageFont.truetype(p, sz)
    return ImageFont.load_default()


def poly(d, pts, width=W, closed=True):
    pts = [tuple(p) for p in pts]
    if closed:
        pts = pts + [pts[0]]
    d.line(pts, fill=C, width=width, joint="curve")


# sol - a sun
im, d = canvas()
d.ellipse((88, 88, 168, 168), outline=C, width=W)
for k in range(8):
    a = k * math.pi / 4
    d.line((128 + 62 * math.cos(a), 128 + 62 * math.sin(a), 128 + 100 * math.cos(a), 128 + 100 * math.sin(a)), fill=C, width=W)
save(im, "sol")

# power - a bolt
im, d = canvas()
d.polygon([(150, 20), (70, 140), (124, 140), (104, 236), (190, 106), (134, 106), (160, 20)], fill=C)
save(im, "power")

# battery
im, d = canvas()
d.rounded_rectangle((40, 76, 200, 180), radius=16, outline=C, width=W)
d.rectangle((200, 108, 222, 148), fill=C)
d.rectangle((64, 100, 140, 156), fill=C)
save(im, "battery")

# water - a drop
im, d = canvas()
pts = []
for k in range(0, 361, 6):
    t = math.radians(k)
    r = 70
    x = 128 + r * math.cos(t)
    y = 150 + r * math.sin(t)
    if math.sin(t) < -0.2:
        f = (-math.sin(t) - 0.2) / 0.8
        x = 128 + (x - 128) * (1 - f)
        y = 150 + (y - 150) + (-120 * f)
    pts.append((x, y))
poly(d, pts)
save(im, "water")

# oxygen - O2 in a ring
im, d = canvas()
d.ellipse((24, 24, 232, 232), outline=C, width=W - 4)
d.text((70, 66), "O", font=font(120), fill=C)
d.text((156, 132), "2", font=font(70), fill=C)
save(im, "oxygen")

# structure - an I-beam
im, d = canvas()
d.rectangle((48, 52, 208, 82), fill=C)
d.rectangle((48, 174, 208, 204), fill=C)
d.rectangle((112, 82, 144, 174), fill=C)
save(im, "structure")

# food - a wheat ear
im, d = canvas()
d.line((128, 236, 128, 60), fill=C, width=W - 4)
for k in range(4):
    y = 80 + k * 38
    d.ellipse((76, y, 124, y + 34), outline=C, width=W - 6)
    d.ellipse((132, y, 180, y + 34), outline=C, width=W - 6)
d.ellipse((108, 30, 148, 76), outline=C, width=W - 6)
save(im, "food")

# crew - a person
im, d = canvas()
d.ellipse((92, 28, 164, 100), outline=C, width=W)
d.arc((44, 120, 212, 300), 180, 360, fill=C, width=W)
save(im, "crew")

# hope - a four-point star with a ring
im, d = canvas()
pts = []
for k in range(8):
    a = k * math.pi / 4 - math.pi / 2
    r = 104 if k % 2 == 0 else 34
    pts.append((128 + r * math.cos(a), 128 + r * math.sin(a)))
d.polygon(pts, fill=C)
save(im, "hope")

# robot
im, d = canvas()
d.rounded_rectangle((52, 84, 204, 200), radius=22, outline=C, width=W)
d.ellipse((90, 124, 116, 150), fill=C)
d.ellipse((140, 124, 166, 150), fill=C)
d.line((128, 84, 128, 44), fill=C, width=W - 4)
d.ellipse((114, 26, 142, 54), fill=C)
d.line((40, 130, 52, 130), fill=C, width=W)
d.line((204, 130, 216, 130), fill=C, width=W)
save(im, "robot")

# ship - a rocket
im, d = canvas()
poly(d, [(128, 20), (170, 80), (170, 176), (86, 176), (86, 80)])
poly(d, [(86, 140), (50, 196), (86, 186)], closed=False)
poly(d, [(170, 140), (206, 196), (170, 186)], closed=False)
d.ellipse((110, 96, 146, 132), outline=C, width=W - 6)
d.line((110, 196, 110, 232), fill=C, width=W - 6)
d.line((146, 196, 146, 232), fill=C, width=W - 6)
save(im, "ship")

# earth - a globe
im, d = canvas()
d.ellipse((28, 28, 228, 228), outline=C, width=W)
d.ellipse((80, 28, 176, 228), outline=C, width=W - 6)
d.line((28, 128, 228, 128), fill=C, width=W - 6)
d.arc((40, 40, 216, 140), 200, 340, fill=C, width=W - 8)
save(im, "earth")

# trade - two arrows
im, d = canvas()
d.line((40, 92, 200, 92), fill=C, width=W)
d.polygon([(216, 92), (176, 60), (176, 124)], fill=C)
d.line((216, 168, 56, 168), fill=C, width=W)
d.polygon([(40, 168), (80, 136), (80, 200)], fill=C)
save(im, "trade")

# warning - a triangle with !
im, d = canvas()
poly(d, [(128, 28), (232, 216), (24, 216)])
d.line((128, 92, 128, 156), fill=C, width=W)
d.ellipse((116, 176, 140, 200), fill=C)
save(im, "warning")

# help - ?
im, d = canvas()
d.ellipse((24, 24, 232, 232), outline=C, width=W - 4)
d.text((88, 50), "?", font=font(150), fill=C)
save(im, "help")

# expand / collapse - chevrons
for name, pts in (("expand", [(64, 100), (128, 164), (192, 100)]), ("collapse", [(64, 156), (128, 92), (192, 156)])):
    im, d = canvas()
    poly(d, pts, width=W + 6, closed=False)
    save(im, name)

# parts - a gear
im, d = canvas()
pts = []
for k in range(16):
    a = k * math.pi / 8
    r = 104 if (k // 1) % 2 == 0 else 80
    pts.append((128 + r * math.cos(a), 128 + r * math.sin(a)))
poly(d, pts, width=W - 4)
d.ellipse((100, 100, 156, 156), outline=C, width=W - 4)
save(im, "parts")

# garden - a sprout
im, d = canvas()
d.line((128, 228, 128, 110), fill=C, width=W)
d.chord((40, 50, 140, 150), 0, 180, outline=C, width=W - 4)
d.chord((116, 30, 216, 130), 0, 180, outline=C, width=W - 4)
save(im, "garden")

# research - a flask
im, d = canvas()
poly(d, [(104, 30), (104, 104), (40, 220), (216, 220), (152, 104), (152, 30)], closed=False)
d.line((90, 30, 166, 30), fill=C, width=W)
d.line((70, 170, 186, 170), fill=C, width=W - 6)
save(im, "research")

# speed - a play triangle
im, d = canvas()
d.polygon([(76, 44), (212, 128), (76, 212)], fill=C)
save(im, "play")
im, d = canvas()
d.rectangle((64, 48, 108, 208), fill=C)
d.rectangle((148, 48, 192, 208), fill=C)
save(im, "pause")

# --- Build-menu category icons (interface pass step 2) ---

# factory - a sawtooth roof with a stack (Production)
im, d = canvas()
poly(d, [(28, 220), (28, 120), (84, 84), (84, 120), (140, 84), (140, 120), (196, 84), (196, 40), (228, 40), (228, 220)])
d.rectangle((60, 160, 92, 220), fill=C)
d.rectangle((124, 160, 156, 192), fill=C)
save(im, "factory")

# pylon - a lattice tower with two arms (Logistics)
im, d = canvas()
poly(d, [(128, 24), (72, 232)], closed=False)
poly(d, [(128, 24), (184, 232)], closed=False)
d.line((60, 76, 196, 76), fill=C, width=W)
d.line((84, 136, 172, 136), fill=C, width=W - 4)
poly(d, [(100, 136), (156, 196), (88, 196), (168, 136)], width=W - 8, closed=False)
save(im, "pylon")

# drill - a bit pointing down (Boring)
im, d = canvas()
d.rectangle((92, 24, 164, 72), outline=C, width=W - 4)
poly(d, [(100, 72), (156, 72), (156, 160), (128, 232), (100, 160)])
for y in (104, 140):
    d.line((100, y + 16, 156, y - 8), fill=C, width=W - 8)
save(im, "drill")

# fan - three blades in a ring (Life Support)
im, d = canvas()
d.ellipse((24, 24, 232, 232), outline=C, width=W - 4)
for k in range(3):
    a = k * 2 * math.pi / 3
    pts = []
    for t in range(0, 61, 6):
        u = a + math.radians(t)
        r = 20 + 66 * math.sin(math.radians(t * 3))
        pts.append((128 + r * math.cos(u), 128 + r * math.sin(u)))
    d.polygon(pts, fill=C)
d.ellipse((112, 112, 144, 144), fill=C)
save(im, "fan")

# dome - a habitat dome on a deck (Habitat)
im, d = canvas()
d.arc((40, 64, 216, 240), 180, 360, fill=C, width=W)
d.line((24, 156, 232, 156), fill=C, width=W)
d.line((40, 200, 216, 200), fill=C, width=W - 4)
d.rectangle((112, 112, 144, 156), fill=C)
save(im, "dome")

# dish - an antenna dish (Command)
im, d = canvas()
d.chord((40, 40, 200, 200), 135, 315, outline=C, width=W - 2)
d.line((120, 120, 176, 64), fill=C, width=W - 6)
d.ellipse((168, 52, 196, 80), fill=C)
d.line((96, 168, 72, 232), fill=C, width=W - 2)
d.line((40, 232, 120, 232), fill=C, width=W)
save(im, "dish")

# close - an X
im, d = canvas()
d.line((64, 64, 192, 192), fill=C, width=W + 6)
d.line((192, 64, 64, 192), fill=C, width=W + 6)
save(im, "close")

print("icons:", len([f for f in os.listdir(OUT) if f.endswith(".png")]), "->", OUT)
