#!/usr/bin/env python
"""Hunyuan3D 2.1 PAINT stage: untextured mesh + reference image -> textured GLB.
Usage: python rh_paint.py <repo_dir> <mesh.glb> <ref_image> <out.glb> [num_view] [res]
Needs custom_rasterizer + mesh_inpaint_processor (built) and the paint weights
(auto-download to HY3DGEN_MODELS on first run)."""
import sys, os, time

repo, mesh_in, img_path, out_glb = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
num_view = int(sys.argv[5]) if len(sys.argv) > 5 else 6
res      = int(sys.argv[6]) if len(sys.argv) > 6 else 512

os.chdir(repo)
sys.path.insert(0, repo)
sys.path.insert(0, os.path.join(repo, "hy3dshape"))
sys.path.insert(0, os.path.join(repo, "hy3dpaint"))

try:
    from torchvision_fix import apply_fix; apply_fix()
except Exception as e:
    print(f"[warn] torchvision_fix: {e}", flush=True)

from textureGenPipeline import Hunyuan3DPaintPipeline, Hunyuan3DPaintConfig

t0 = time.time()
print(f"[paint] configuring ({num_view} views @ {res})...", flush=True)
conf = Hunyuan3DPaintConfig(num_view, res)
conf.realesrgan_ckpt_path = "hy3dpaint/ckpt/RealESRGAN_x4plus.pth"
conf.multiview_cfg_path   = "hy3dpaint/cfgs/hunyuan-paint-pbr.yaml"
conf.custom_pipeline      = "hy3dpaint/hunyuanpaintpbr"

print("[paint] loading pipeline (downloads paint weights on first run)...", flush=True)
pipe = Hunyuan3DPaintPipeline(conf)
print(f"[paint] pipeline ready in {time.time()-t0:.0f}s", flush=True)

# Emit OBJ+MTL+texture (save_glb=False skips the bpy-dependent GLB writer); we
# convert to GLB with the standalone Blender afterwards.
out_obj = out_glb[:-4] + ".obj" if out_glb.lower().endswith(".glb") else out_glb + ".obj"
t1 = time.time()
result = pipe(mesh_path=mesh_in, image_path=img_path, output_mesh_path=out_obj, save_glb=False)
print(f"[paint] textured in {time.time()-t1:.0f}s -> {result}", flush=True)

# receipt: OBJ + its texture map on disk
import glob, os as _os
d = _os.path.dirname(out_obj) or "."
pngs = glob.glob(_os.path.join(d, "*.png")) + glob.glob(_os.path.join(d, "*.jpg"))
print(f"[receipt] obj={_os.path.basename(result)} "
      f"exists={_os.path.exists(result)} textures={[_os.path.basename(p) for p in pngs]}", flush=True)
