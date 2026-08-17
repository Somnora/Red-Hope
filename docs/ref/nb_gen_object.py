#!/usr/bin/env python3
"""Nano Banana Pro object generator (ships / facilities / habitats) for Red Hope.
Multi-angle, single ISOLATED object, NO base/floor/ground so it sits flush
in-engine. Hero 3/4 view defines the design; other angles anchor to it.

Usage:
  uv run --with google-genai --with pillow python nb_gen_object.py --roster object_roster.json --id HeavyForge --out <dir>
"""
import argparse, io, json, os, sys, time
from google import genai
from google.genai import types

PROJECT, LOCATION, MODEL = "somnora-dev-01", "global", "gemini-3-pro-image"
IMAGE_SIZE, ASPECT, REF_MAX = "4K", "1:1", 1280

STYLE = (
    "Grounded near-future Mars-colony industrial design. Physically-based studio "
    "PRODUCT render, soft even studio lighting, gentle painterly finish. Cohesive "
    "muted utilitarian palette: dusty greys, oxide red, off-white, worn metal, "
    "restrained safety-accent color. High material detail: panel lines, bolts, "
    "weld seams, honest wear, dust, believable surfaces. Clean readable silhouette, "
    "sharp focus, crisp edges."
)
NEG = (
    "No thick black outlines, no cel shading, no comic-book look, no cartoon. No "
    "text, no logos, no UI, no callouts, no measurement lines. Absolutely NO "
    "ground, no floor, no base plate, no pedestal, no platform, no terrain, no "
    "rug, no cast shadow, nothing underneath the object. Plain flat uniform "
    "light-grey studio background only. Exactly ONE object, centered, whole "
    "object fully in frame, no duplicates, no parts diagram, no split panels."
)
VIEW_CAM = {
    "three_quarter": "three-quarter front view from a slightly raised angle",
    "front": "straight-on front elevation view at eye level",
    "back":  "straight-on rear elevation view",
    "left":  "straight-on left side elevation view",
    "right": "straight-on right side elevation view",
}
ORDER = ["three_quarter", "front", "right", "back", "left"]

_client = None
def client():
    global _client
    if _client is None:
        _client = genai.Client(vertexai=True, project=PROJECT, location=LOCATION)
    return _client

def _cfg():
    return types.GenerateContentConfig(
        response_modalities=["TEXT", "IMAGE"],
        image_config=types.ImageConfig(aspect_ratio=ASPECT, image_size=IMAGE_SIZE),
        candidate_count=1)

def _extract(resp):
    for c in (resp.candidates or []):
        if not c.content: continue
        for p in (c.content.parts or []):
            if getattr(p, "inline_data", None) and p.inline_data.data:
                return p.inline_data.data
    return None

def _gen(contents, label, retries=4):
    for a in range(retries):
        try:
            r = client().models.generate_content(model=MODEL, contents=contents, config=_cfg())
            d = _extract(r)
            if d: return d
            print(f"    [{label}] no image part (try {a+1})")
        except Exception as e:
            w = 20*(a+1); print(f"    [{label}] {type(e).__name__}: {str(e)[:150]} -> {w}s"); time.sleep(w)
    return None

def _ref_part(b):
    from PIL import Image
    im = Image.open(io.BytesIO(b)).convert("RGB"); im.thumbnail((REF_MAX, REF_MAX), Image.LANCZOS)
    buf = io.BytesIO(); im.save(buf, "PNG")
    return types.Part.from_bytes(data=buf.getvalue(), mime_type="image/png")

def _save(d, path):
    open(path, "wb").write(d)
    try:
        from PIL import Image; im = Image.open(path)
        print(f"    saved {os.path.basename(path)} {im.size[0]}x{im.size[1]} {len(d)//1024}KB")
    except Exception: print(f"    saved {os.path.basename(path)} {len(d)//1024}KB")

def hero_prompt(s):
    return (f"A single full 3D object concept render of {s['desc']}. {VIEW_CAM['three_quarter']}, "
            f"whole object centered with clear margin. {STYLE} {NEG}")

# Hardware lock. Some objects are the SAME physical prop in a different state
# (a planter at three growth stages), and the engine swaps those meshes in
# place on one cell - so if the tray body differs between stages the planter
# visibly morphs as the crop grows, and neighbouring cells at different stages
# disagree. This anchors the hero itself to another object's hero, so only the
# named part is allowed to change.
def chained_hero_prompt(s):
    return ("Use the attached reference image as the definitive design of the HARDWARE. "
            "Keep the planter tray/chassis EXACTLY as shown: identical shape, proportions, "
            "footprint, panel lines, colors, materials, wear and status readout. Do not "
            "redesign the hardware, do not change its size or silhouette. Change ONLY what "
            "is growing in it and its immediate plant-support structure (trellis, support "
            f"mesh). The result must show: {s['desc']}. {VIEW_CAM['three_quarter']}, "
            f"whole object centered with clear margin. {STYLE} {NEG}")

def anchored_prompt(s, view):
    return ("Use the attached reference image as the definitive design of this object. "
            "Render THE EXACT SAME object: identical shape, proportions, parts, colors, "
            f"materials and wear. Do not redesign. Now: {VIEW_CAM[view]}, whole object "
            f"centered with clear margin. {STYLE} {NEG}")

def run(spec, out_dir):
    raw = os.path.join(out_dir, "raw"); os.makedirs(raw, exist_ok=True)
    oid = spec["id"]
    def p(v): return os.path.join(raw, f"{oid}_{v}.png")
    def have(x): return os.path.exists(x) and os.path.getsize(x) > 10000
    hero_path = p("three_quarter")
    if have(hero_path):
        hero = open(hero_path,"rb").read(); print("  [three_quarter] exists")
    else:
        anchor = spec.get("anchor")
        apath = None
        if anchor:
            root = os.path.dirname(os.path.abspath(out_dir))
            apath = os.path.join(root, anchor, "raw", f"{anchor}_three_quarter.png")
        if apath and have(apath):
            print(f"  [three_quarter] generating hero anchored to {anchor}")
            hero = _gen([_ref_part(open(apath,"rb").read()), chained_hero_prompt(spec)], "three_quarter")
        else:
            if anchor:
                print(f"  WARN anchor {anchor} missing at {apath} - falling back to unanchored hero")
            print("  [three_quarter] generating hero")
            hero = _gen(hero_prompt(spec), "three_quarter")
        if not hero: print(f"  FAILED hero {oid}"); return False
        _save(hero, hero_path)
    if spec.get("hero_only"):
        print("  hero_only - skipping the 4 elevations")
        return True
    ref = _ref_part(hero)
    for v in ["front", "right", "back", "left"]:
        path = p(v)
        if have(path): print(f"  [{v}] exists"); continue
        print(f"  [{v}] generating"); d = _gen([ref, anchored_prompt(spec, v)], v)
        if d: _save(d, path)
        else: print(f"  WARN missing {v}")
    return True

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--roster"); ap.add_argument("--id"); ap.add_argument("--spec"); ap.add_argument("--out", required=True)
    a = ap.parse_args()
    spec = json.loads(a.spec) if a.spec else {s["id"]: s for s in json.load(open(a.roster))}[a.id]
    os.makedirs(a.out, exist_ok=True)
    print(f"=== {spec['id']} :: {spec.get('name','')} ===")
    ok = run(spec, a.out); print("DONE" if ok else "FAILED"); sys.exit(0 if ok else 1)

if __name__ == "__main__":
    main()
