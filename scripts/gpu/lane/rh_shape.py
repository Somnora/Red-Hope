#!/usr/bin/env python
"""Hunyuan3D 2.1 SHAPE stage only: single image -> untextured GLB mesh.
Usage: python rh_shape.py <repo_dir> <input_image> <output_glb>
Weights auto-download to HF_HOME (NFS cache) on first run. No compiled ops needed."""
import sys, os, time

repo, img_path, out_glb = sys.argv[1], sys.argv[2], sys.argv[3]
os.chdir(repo)
sys.path.insert(0, repo)  # repo root: torchvision_fix.py, textureGenPipeline
sys.path.insert(0, os.path.join(repo, "hy3dshape"))
sys.path.insert(0, os.path.join(repo, "hy3dpaint"))

try:
    from torchvision_fix import apply_fix; apply_fix()
except Exception as e:
    print(f"[warn] torchvision_fix: {e}")

from PIL import Image
from hy3dshape.rembg import BackgroundRemover
from hy3dshape.pipelines import Hunyuan3DDiTFlowMatchingPipeline

t0 = time.time()
print("[shape] loading pipeline (downloads weights on first run)...", flush=True)
pipe = Hunyuan3DDiTFlowMatchingPipeline.from_pretrained("tencent/Hunyuan3D-2.1")
print(f"[shape] pipeline ready in {time.time()-t0:.0f}s", flush=True)

src = Image.open(img_path)
# Strip background unless the image already carries real transparency.
has_alpha = src.mode == "RGBA" and src.getchannel("A").getextrema()[0] < 250
if has_alpha:
    image = src.convert("RGBA")
    print("[shape] input already has alpha, skipping rembg", flush=True)
else:
    print("[shape] opaque input, running background removal...", flush=True)
    image = BackgroundRemover()(src.convert("RGB"))

t1 = time.time()
mesh = pipe(image=image)[0]
mesh.export(out_glb)
print(f"[shape] mesh generated in {time.time()-t1:.0f}s -> {out_glb}", flush=True)

# receipt
try:
    import trimesh
    m = trimesh.load(out_glb, force="mesh")
    print(f"[receipt] verts={len(m.vertices)} faces={len(m.faces)} "
          f"bounds={m.bounds.tolist()} watertight={m.is_watertight}", flush=True)
except Exception as e:
    print(f"[receipt] load failed: {e}", flush=True)
