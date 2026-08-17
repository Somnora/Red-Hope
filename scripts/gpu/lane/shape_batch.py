#!/usr/bin/env python
"""Hunyuan3D 2.1 SHAPE stage, batched over a directory (per-sprite isolated)."""
import sys, os, glob, time, traceback

in_dir, out_dir = sys.argv[1], sys.argv[2]
steps = int(sys.argv[3]) if len(sys.argv) > 3 and sys.argv[3] not in ("", "0") else 50
resolution = int(sys.argv[4]) if len(sys.argv) > 4 and sys.argv[4] not in ("", "0") else 384
repo = os.environ["HY3D_REPO"]
os.chdir(repo)
sys.path.insert(0, repo)
sys.path.insert(0, os.path.join(repo, "hy3dshape"))
try:
    from torchvision_fix import apply_fix; apply_fix()
except Exception as e:
    print(f"[warn] torchvision_fix: {e}", flush=True)

from PIL import Image
from hy3dshape.rembg import BackgroundRemover
from hy3dshape.pipelines import Hunyuan3DDiTFlowMatchingPipeline

imgs = sorted(glob.glob(os.path.join(in_dir, "*.png")) +
              glob.glob(os.path.join(in_dir, "*.jpg")) +
              glob.glob(os.path.join(in_dir, "*.jpeg")))
if not imgs:
    sys.exit(f"[fatal] no images (*.png/*.jpg) in {in_dir}")
os.makedirs(out_dir, exist_ok=True)

t0 = time.time()
print(f"[shape] loading pipeline (downloads weights on first run)...", flush=True)
pipe = Hunyuan3DDiTFlowMatchingPipeline.from_pretrained("tencent/Hunyuan3D-2.1")
print(f"[shape] pipeline ready in {time.time()-t0:.0f}s; {len(imgs)} image(s); "
      f"steps={steps} octree_resolution={resolution}", flush=True)
bg = BackgroundRemover()

ok = 0; failed = []
for img_path in imgs:
    name = os.path.splitext(os.path.basename(img_path))[0]
    out_glb = os.path.join(out_dir, f"{name}.glb")
    try:
        src = Image.open(img_path)
        has_alpha = src.mode == "RGBA" and src.getchannel("A").getextrema()[0] < 250
        if has_alpha:
            image = src.convert("RGBA"); print(f"[shape] {name}: real alpha, skip rembg", flush=True)
        else:
            print(f"[shape] {name}: opaque, running rembg...", flush=True)
            image = bg(src.convert("RGB"))
        t1 = time.time()
        mesh = pipe(image=image, num_inference_steps=steps, octree_resolution=resolution)[0]
        mesh.export(out_glb)
        dt = time.time() - t1
        try:
            import trimesh
            m = trimesh.load(out_glb, force="mesh")
            ext = (m.bounds[1] - m.bounds[0]).tolist()
            print(f"[receipt] {name} verts={len(m.vertices)} faces={len(m.faces)} "
                  f"extent={[round(x,3) for x in ext]} {dt:.0f}s -> {out_glb}", flush=True)
        except Exception as e:
            print(f"[receipt] {name} exported ({dt:.0f}s) but load failed: {e}", flush=True)
        ok += 1
    except Exception as e:
        failed.append(name)
        print(f"[error] {name} FAILED: {e}", flush=True)
        traceback.print_exc()

print(f"[done] {ok}/{len(imgs)} meshes -> {out_dir}" +
      (f"; FAILED: {failed}" if failed else ""), flush=True)
sys.exit(2 if failed else 0)
