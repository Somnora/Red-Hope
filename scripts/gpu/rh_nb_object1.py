#!/usr/bin/env python3
"""Single-object hero reference via Nano Banana Pro. Object variant of
rh_nb_crew8.py (1:1 canvas, three-quarter machine view, no-ground rules).

  uv run --with google-genai --with pillow python scripts/gpu/rh_nb_object1.py \
      --id <name> --desc "<object description>" --out <dir>

Same auth fallback as the crew generator: prefers ADC, borrows the CLI access
token when ADC is dead (the two-credential trap, 2026-08-18).
"""
import argparse, os, subprocess, time

from google import genai
from google.genai import types

PROJECT, LOCATION, MODEL = "somnora-dev-01", "global", "gemini-3-pro-image"
STYLE = ("Grounded near-future Mars-colony industrial design. Semi-realistic stylized "
         "game asset (high-end console key art), physically-based studio render, soft "
         "neutral three-point lighting. Cohesive muted utilitarian palette: dusty grey, "
         "off-white worn metal, oxide accents, with ONE restrained function-accent colour. "
         "High material detail: panel lines, honest wear, believable surfaces. Clean "
         "readable silhouette, sharp focus.")
NEG = ("No ground, no floor, no base, no pedestal, no cast shadow, no regolith, no "
       "terrain. Plain flat uniform light-grey studio background only. No text, no "
       "logos, no UI. No people. No thick black outlines, no cel shading. Exactly ONE "
       "object, centered, fully inside frame with margin, no cropping.")


def flat_corners(path, thresh=6.0):
    from PIL import Image
    import statistics
    im = Image.open(path).convert("L")
    w, h = im.size
    c = int(min(w, h) * 0.12)
    worst = 0.0
    for box in [(0, 0, c, c), (w - c, 0, w, c), (0, h - c, c, h), (w - c, h - c, w, h)]:
        worst = max(worst, statistics.pstdev(list(im.crop(box).getdata())))
    return worst < thresh, worst


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--id", required=True)
    ap.add_argument("--desc", required=True)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    creds = None
    if subprocess.run(["gcloud", "auth", "application-default", "print-access-token"],
                      capture_output=True).returncode != 0:
        r = subprocess.run(["gcloud", "auth", "print-access-token"], capture_output=True, text=True)
        if r.returncode == 0:
            from google.oauth2.credentials import Credentials
            creds = Credentials(token=r.stdout.strip())
            print("ADC dead, using CLI token", flush=True)
    cl = genai.Client(vertexai=True, project=PROJECT, location=LOCATION, credentials=creds) \
        if creds else genai.Client(vertexai=True, project=PROJECT, location=LOCATION)
    cfg = types.GenerateContentConfig(
        response_modalities=["TEXT", "IMAGE"],
        image_config=types.ImageConfig(aspect_ratio="1:1", image_size="4K"),
        candidate_count=1)
    prompt = ("A single industrial machine concept render, three-quarter view from "
              "slightly above, the machine's front and one side visible. %s %s %s"
              % (a.desc, STYLE, NEG))
    path = os.path.join(a.out, "%s.png" % a.id)
    for attempt in range(4):
        try:
            r = cl.models.generate_content(model=MODEL, contents=prompt, config=cfg)
            data = None
            for c in (r.candidates or []):
                for p in (c.content.parts if c.content else []) or []:
                    if getattr(p, "inline_data", None) and p.inline_data.data:
                        data = p.inline_data.data
                        break
                if data:
                    break
            if data:
                open(path, "wb").write(data)
                ok, worst = flat_corners(path)
                print("%s: %d KB, corners %s (stddev %.1f)"
                      % (a.id, len(data) // 1024, "FLAT" if ok else "NOT FLAT", worst), flush=True)
                return
            print("no image part, attempt %d" % (attempt + 1), flush=True)
        except Exception as e:
            w = 20 * (attempt + 1)
            print("%s: %s -> wait %ds" % (type(e).__name__, str(e)[:120], w), flush=True)
            time.sleep(w)
    raise SystemExit("generation failed")


if __name__ == "__main__":
    main()
