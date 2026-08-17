#!/usr/bin/env python
"""Batch sprite->textured-mesh: load shape + paint pipelines ONCE (40GB A100 holds both),
stream every sprite in io/inbox/batch through shape + paint, emit an OBJ+textures each.
The Blender finalize (obj2glb + cleanup + preview) runs separately per output.
Usage: python rh_batch.py <repo_dir> <inbox_dir> <outbox_dir>"""
import sys, os, glob, time, traceback

repo, inbox, outbox = sys.argv[1], sys.argv[2], sys.argv[3]
os.chdir(repo)
sys.path.insert(0, repo)
sys.path.insert(0, os.path.join(repo, "hy3dshape"))
sys.path.insert(0, os.path.join(repo, "hy3dpaint"))
try:
    from torchvision_fix import apply_fix; apply_fix()
except Exception as e:
    print(f"[warn] torchvision_fix: {e}", flush=True)

import torch
from PIL import Image
from hy3dshape.rembg import BackgroundRemover
from hy3dshape.pipelines import Hunyuan3DDiTFlowMatchingPipeline
from textureGenPipeline import Hunyuan3DPaintPipeline, Hunyuan3DPaintConfig

def vram():
    free, total = torch.cuda.mem_get_info()
    return f"{(total-free)//2**20}/{total//2**20} MiB"

t0 = time.time()
print("[batch] loading shape pipeline...", flush=True)
shape = Hunyuan3DDiTFlowMatchingPipeline.from_pretrained("tencent/Hunyuan3D-2.1")
print(f"[batch] shape loaded ({vram()})", flush=True)

print("[batch] loading paint pipeline...", flush=True)
conf = Hunyuan3DPaintConfig(6, 512)
conf.realesrgan_ckpt_path = "hy3dpaint/ckpt/RealESRGAN_x4plus.pth"
conf.multiview_cfg_path   = "hy3dpaint/cfgs/hunyuan-paint-pbr.yaml"
conf.custom_pipeline      = "hy3dpaint/hunyuanpaintpbr"
paint = Hunyuan3DPaintPipeline(conf)
print(f"[batch] both resident ({vram()}) after {time.time()-t0:.0f}s", flush=True)

rembg = BackgroundRemover()
jobs = sorted(glob.glob(os.path.join(inbox, "*.png")))
print(f"[batch] {len(jobs)} sprites: {[os.path.basename(j) for j in jobs]}", flush=True)

done, failed = [], []
for i, sprite in enumerate(jobs, 1):
    name = os.path.splitext(os.path.basename(sprite))[0]
    try:
        t = time.time()
        src = Image.open(sprite)
        has_alpha = src.mode == "RGBA" and src.getchannel("A").getextrema()[0] < 250
        img = src.convert("RGBA") if has_alpha else rembg(src.convert("RGB"))
        tmp_shape = os.path.join(outbox, f"{name}_shape.glb")
        mesh = shape(image=img)[0]
        mesh.export(tmp_shape)
        out_obj = os.path.join(outbox, f"{name}_textured.obj")
        paint(mesh_path=tmp_shape, image_path=sprite, output_mesh_path=out_obj, save_glb=False)
        print(f"[batch] ({i}/{len(jobs)}) {name} OK in {time.time()-t:.0f}s ({vram()})", flush=True)
        done.append(name)
    except Exception as e:
        print(f"[batch] ({i}/{len(jobs)}) {name} FAILED: {e}", flush=True)
        traceback.print_exc()
        failed.append(name)
    torch.cuda.empty_cache()

print(f"[batch] COMPLETE done={done} failed={failed}", flush=True)
