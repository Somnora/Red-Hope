"""rh_thumbs_compose - cut RH.Thumbs captures into transparent card thumbnails.

  python3 scripts/ui/rh_thumbs_compose.py --src <RH.Thumbs out dir> [--out Martians/gen/ui_thumbs]

RH.Thumbs (Source/RedHope/Private/RHThumbCapture.cpp) writes two frames per
building: <Def>_color.png (the final graded colour) and <Def>_mask.png (scene
colour whose alpha is INVERSE opacity: background 1, building 0). This joins
them - colour from the first, alpha from the second - crops to the building
with a little air, squares it and writes T_Thumb_<Def>.png (256 px) plus a
contact sheet. Imported by scripts/unreal/rh_import_ui.py (RH_DEST/RH_PREFIX)
to /Game/RedHope/UI/Thumbs, where the build menu's cards load them.
"""
import argparse
import glob
import os

from PIL import Image

ap = argparse.ArgumentParser()
ap.add_argument("--src", required=True)
ap.add_argument("--out", default="Martians/gen/ui_thumbs")
ap.add_argument("--size", type=int, default=256)
args = ap.parse_args()
OUT = os.path.abspath(args.out)
os.makedirs(OUT, exist_ok=True)

made = []
for color_path in sorted(glob.glob(os.path.join(args.src, "*_color.png"))):
    name = os.path.basename(color_path)[: -len("_color.png")]
    mask_path = os.path.join(args.src, name + "_mask.png")
    if not os.path.exists(mask_path):
        print("skip (no mask):", name)
        continue
    rgb = Image.open(color_path).convert("RGB")
    a = Image.open(mask_path).convert("RGBA").split()[3]
    # Inverse opacity: the empty corner reads 255. If a capture ever comes back
    # the other way round, follow the corner rather than assume.
    if a.getpixel((0, 0)) > 127:
        a = a.point(lambda v: 255 - v)
    a = a.point(lambda v: 0 if v < 10 else v)
    bbox = a.getbbox()
    if not bbox:
        print("skip (empty):", name)
        continue
    im = rgb.copy()
    im.putalpha(a)
    x0, y0, x1, y1 = bbox
    side = int(max(x1 - x0, y1 - y0) * 1.08)
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    box = (int(cx - side / 2), int(cy - side / 2), int(cx - side / 2) + side, int(cy - side / 2) + side)
    sq = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    sq.paste(im.crop(box), (0, 0))
    sq = sq.resize((args.size, args.size), Image.LANCZOS)
    dst = os.path.join(OUT, "T_Thumb_%s.png" % name)
    sq.save(dst)
    made.append((name, sq))
    print("ok", name, "bbox", bbox)

if made:
    cols = 5
    rows = (len(made) + cols - 1) // cols
    sheet = Image.new("RGBA", (cols * args.size, rows * args.size), (14, 20, 26, 255))
    for i, (_, im) in enumerate(made):
        sheet.alpha_composite(im, ((i % cols) * args.size, (i // cols) * args.size))
    sheet.save(os.path.join(OUT, "_contact.png"))
print("thumbs:", len(made), "->", OUT)
