#!/usr/bin/env python3
"""Hero-front sprites for the regenerated 8 crew. Nano Banana Pro on Vertex.

  uv run --with google-genai --with pillow python scripts/gpu/rh_nb_crew8.py \
      --roster scripts/gpu/crew8_roster.json --out <dir>

Deliberately HERO-ONLY (one front view per character, ~$0.20 each): TRELLIS.2
meshes from a single front image, so the 8-view identity-anchored roster the
July batch used is unnecessary spend here. Style/negative/pose blocks are
copied verbatim from the proven nb_gen_char.py (2026-07-17 batch, QA-passed)
so these 8 land in the same universe as the 12 existing crew - the skill lives
outside the repo, and this script must be runnable from the repo alone.

Resumable: an existing raw/<id>.png over 10KB is skipped.
QA is built in: the corner-flatness check (grayscale stddev < 6 on all four
corner crops) that gates meshing - a halo or gradient background meshes as
GEOMETRY, which is the Session-51 lesson.
"""
import argparse, io, json, os, subprocess, time

from google import genai
from google.genai import types

PROJECT, LOCATION, MODEL = "somnora-dev-01", "global", "gemini-3-pro-image"
STYLE = (
    "Grounded near-future Mars-colony art direction. Semi-realistic STYLIZED "
    "game character (think high-end console game key art), physically-based "
    "studio render, soft neutral three-point lighting, gentle painterly finish. "
    "Realistic human proportions, roughly 7.5 heads tall, natural head size. "
    "Cohesive muted utilitarian palette: dusty greys, oxide red, off-white, worn "
    "metal, with restrained per-role safety-accent color. High material detail: "
    "fabric weave, seams, honest wear, panel lines, believable surfaces. Clean, "
    "readable silhouette, sharp focus, crisp edges."
)
NEG = (
    "Absolutely no thick black outlines, no cel shading, no comic-book or "
    "graphic-novel look, no anime, no chibi, no oversized head, no cute "
    "proportions. No held tools, no props, no weapons, empty relaxed hands. No "
    "text, no logos, no UI. No ground, no floor, no base, no pedestal, no cast "
    "shadow. Plain flat uniform light-grey studio background only. Exactly ONE "
    "figure, centered, no duplicates, no turnaround sheet, no split panels."
)
POSE = (
    "Standing relaxed neutral A-pose, arms slightly away from the torso, hands "
    "open and empty, feet flat and shoulder-width, entire body from head to the "
    "soles of the boots fully inside the frame with clear margin, centered."
)


def flat_corners(png_path, thresh=6.0):
    from PIL import Image
    import statistics
    im = Image.open(png_path).convert("L")
    w, h = im.size
    c = int(min(w, h) * 0.12)
    worst = 0.0
    for box in [(0, 0, c, c), (w - c, 0, w, c), (0, h - c, c, h), (w - c, h - c, w, h)]:
        px = list(im.crop(box).getdata())
        worst = max(worst, statistics.pstdev(px))
    return worst < thresh, worst


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--roster", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--limit", type=int, default=0,
                    help="process at most N MISSING characters then exit 0 - lets the "
                         "batch run as short foreground chunks instead of one long "
                         "background task (which kept being killed externally on "
                         "2026-08-18; resumability makes chunking free)")
    a = ap.parse_args()
    raw = os.path.join(a.out, "raw")
    os.makedirs(raw, exist_ok=True)
    chars = json.load(open(a.roster))["chars"]
    # ADC is what the SDK wants, but `gcloud auth login` and `gcloud auth
    # application-default login` are DIFFERENT credentials, and 2026-08-18 the
    # director refreshed the first while the SDK needs the second - every call
    # failed with RefreshError while the CLI token minted fine. So: prefer ADC,
    # and when it is dead but the CLI credential works, borrow the CLI access
    # token directly (valid ~1 h, run takes ~10 min). No jam on a half-auth.
    creds = None
    adc_ok = subprocess.run(["gcloud", "auth", "application-default", "print-access-token"],
                            capture_output=True).returncode == 0
    if not adc_ok:
        r = subprocess.run(["gcloud", "auth", "print-access-token"], capture_output=True, text=True)
        if r.returncode == 0:
            from google.oauth2.credentials import Credentials
            creds = Credentials(token=r.stdout.strip())
            print("ADC dead, CLI credential fresh: using the CLI access token", flush=True)
        else:
            raise SystemExit("Neither ADC nor CLI gcloud credentials work. "
                             "Run: gcloud auth application-default login")
    cl = genai.Client(vertexai=True, project=PROJECT, location=LOCATION,
                      credentials=creds) if creds else genai.Client(
                      vertexai=True, project=PROJECT, location=LOCATION)
    cfg = types.GenerateContentConfig(
        response_modalities=["TEXT", "IMAGE"],
        image_config=types.ImageConfig(aspect_ratio="3:4", image_size="4K"),
        candidate_count=1,
    )
    ok = 0
    fresh = 0
    for spec in chars:
        path = os.path.join(raw, "%s.png" % spec["id"])
        if os.path.exists(path) and os.path.getsize(path) > 10000:
            print("%-16s exists" % spec["id"], flush=True); ok += 1; continue
        if a.limit and fresh >= a.limit:
            print("%-16s deferred (chunk limit %d)" % (spec["id"], a.limit), flush=True); continue
        fresh += 1
        prompt = ("A single full-body character concept render. %s. Wearing %s. "
                  "Camera directly in front at eye level, subject facing the camera. %s %s %s"
                  % (spec["identity"], spec["habitat"], POSE, STYLE, NEG))
        data = None
        for attempt in range(4):
            try:
                r = cl.models.generate_content(model=MODEL, contents=prompt, config=cfg)
                for c in (r.candidates or []):
                    for p in (c.content.parts if c.content else []) or []:
                        if getattr(p, "inline_data", None) and p.inline_data.data:
                            data = p.inline_data.data
                            break
                    if data:
                        break
                if data:
                    break
                print("  %s: no image part (attempt %d)" % (spec["id"], attempt + 1), flush=True)
            except Exception as e:
                w = 20 * (attempt + 1)
                print("  %s: %s: %s -> wait %ds" % (spec["id"], type(e).__name__, str(e)[:120], w))
                time.sleep(w)
        if not data:
            print("%-16s FAILED" % spec["id"], flush=True); continue
        open(path, "wb").write(data)
        flat, worst = flat_corners(path)
        print("%-16s %d KB  corners %s (worst stddev %.1f)"
              % (spec["id"], len(data) // 1024, "FLAT" if flat else "NOT FLAT - regenerate", worst), flush=True)
        ok += 1 if flat else 0
    print("done: %d/%d usable" % (ok, len(chars)))


if __name__ == "__main__":
    main()
