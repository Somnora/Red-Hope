#!/usr/bin/env python
"""Red Hope generation-server: FastAPI over the sprite->3D pipeline.

Runs in the rh3d-hy3d venv and holds the Hunyuan3D SHAPE + PAINT pipelines RESIDENT
(the ~175s-to-load, expensive part) so every request is warm. Style-lock lives in a
different venv (diffusers version conflict), so it is shelled out as a subprocess.
Mesh finalize (obj->glb, decimate, preview) uses the standalone Blender, same pattern.

Endpoints (all but /health require Bearer RH3D_API_TOKEN when that env is set):
  GET  /health              -> VRAM, which models are resident
  POST /reconstruct         -> image  -> textured game-ready GLB  (resident pipelines)
  POST /style-lock          -> image  -> 4-angle concept sheet zip (subprocess venv)
  POST /sprite-to-mesh      -> image  -> style-lock, then reconstruct the front
Env: RH3D_NS, HY3DGEN_MODELS, HF_HOME, CUDA_HOME set by the launcher; RH3D_API_TOKEN optional.
"""
import os, sys, time, uuid, subprocess, glob
from fastapi import FastAPI, UploadFile, File, Header, HTTPException, Form
from fastapi.responses import FileResponse, JSONResponse

NS   = os.environ["RH3D_NS"]
REPO = os.path.join(NS, "repos", "Hunyuan3D-2.1")
BL   = os.path.join(NS, "bin", "blender")
SCR  = os.path.join(NS, "scripts")
WORK = os.path.join(NS, "io", "server")
os.makedirs(WORK, exist_ok=True)
TOKEN = os.environ.get("RH3D_API_TOKEN", "")

sys.path.insert(0, REPO)
sys.path.insert(0, os.path.join(REPO, "hy3dshape"))
sys.path.insert(0, os.path.join(REPO, "hy3dpaint"))
try:
    from torchvision_fix import apply_fix; apply_fix()
except Exception as e:
    print(f"[warn] torchvision_fix: {e}", flush=True)

import torch
from PIL import Image
from hy3dshape.rembg import BackgroundRemover
from hy3dshape.pipelines import Hunyuan3DDiTFlowMatchingPipeline
from textureGenPipeline import Hunyuan3DPaintPipeline, Hunyuan3DPaintConfig

app = FastAPI(title="Red Hope generation-server", version="1.0")
STATE = {"shape": None, "paint": None, "rembg": None, "loaded_at": None}

def vram():
    free, total = torch.cuda.mem_get_info()
    return {"used_mib": (total - free) // 2**20, "total_mib": total // 2**20}

def load_models():
    if STATE["shape"] is not None:
        return
    t = time.time()
    print("[server] loading shape...", flush=True)
    STATE["shape"] = Hunyuan3DDiTFlowMatchingPipeline.from_pretrained("tencent/Hunyuan3D-2.1")
    print("[server] loading paint...", flush=True)
    conf = Hunyuan3DPaintConfig(6, 512)
    conf.realesrgan_ckpt_path = os.path.join(REPO, "hy3dpaint/ckpt/RealESRGAN_x4plus.pth")
    conf.multiview_cfg_path   = os.path.join(REPO, "hy3dpaint/cfgs/hunyuan-paint-pbr.yaml")
    conf.custom_pipeline      = os.path.join(REPO, "hy3dpaint/hunyuanpaintpbr")
    STATE["paint"] = Hunyuan3DPaintPipeline(conf)
    STATE["rembg"] = BackgroundRemover()
    STATE["loaded_at"] = time.time()
    print(f"[server] models resident in {time.time()-t:.0f}s ({vram()})", flush=True)

def auth(authorization):
    if TOKEN and authorization != f"Bearer {TOKEN}":
        raise HTTPException(status_code=401, detail="bad or missing bearer token")

def prep_image(path):
    src = Image.open(path)
    has_alpha = src.mode == "RGBA" and src.getchannel("A").getextrema()[0] < 250
    return src.convert("RGBA") if has_alpha else STATE["rembg"](src.convert("RGB"))

def run_blender(script, *args):
    subprocess.run([BL, "--background", "--python", os.path.join(SCR, script), "--", *args],
                   check=True, capture_output=True, text=True)

def reconstruct(sprite_path, job, tris):
    """image -> textured game-ready GLB, using the resident pipelines."""
    load_models()
    img = prep_image(sprite_path)
    shape_glb = os.path.join(WORK, f"{job}_shape.glb")
    STATE["shape"](image=img)[0].export(shape_glb)
    out_obj = os.path.join(WORK, f"{job}_textured.obj")
    STATE["paint"](mesh_path=shape_glb, image_path=sprite_path,
                   output_mesh_path=out_obj, save_glb=False)
    tex_glb  = os.path.join(WORK, f"{job}_textured.glb")
    game_glb = os.path.join(WORK, f"{job}_game.glb")
    run_blender("obj2glb.py", out_obj, tex_glb)
    run_blender("mesh_cleanup.py", tex_glb, game_glb, str(tris))
    torch.cuda.empty_cache()
    return game_glb

@app.get("/health")
def health():
    return {"status": "ok",
            "models_resident": STATE["shape"] is not None,
            "loaded_at": STATE["loaded_at"], "vram": vram()}

@app.post("/reconstruct")
async def ep_reconstruct(file: UploadFile = File(...), tris: int = Form(18000),
                         authorization: str = Header(None)):
    auth(authorization)
    job = uuid.uuid4().hex[:10]
    sprite = os.path.join(WORK, f"{job}_in.png")
    with open(sprite, "wb") as f: f.write(await file.read())
    t = time.time()
    game_glb = reconstruct(sprite, job, tris)
    return FileResponse(game_glb, media_type="model/gltf-binary",
                        filename=f"{job}_game.glb",
                        headers={"X-Gen-Seconds": f"{time.time()-t:.0f}", "X-Job": job})

@app.post("/style-lock")
async def ep_stylelock(file: UploadFile = File(...), subject: str = Form("the subject"),
                       ip_scale: float = Form(0.75), authorization: str = Header(None)):
    auth(authorization)
    job = uuid.uuid4().hex[:10]
    sprite = os.path.join(WORK, f"{job}_in.png")
    with open(sprite, "wb") as f: f.write(await file.read())
    out_dir = os.path.join(WORK, f"{job}_sheet")
    # subprocess into the SDXL venv (different diffusers than this process)
    env = dict(os.environ)
    r = subprocess.run(
        [os.path.expanduser("~/rh3d-venv/bin/python"), os.path.join(SCR, "rh_stylelock.py"),
         sprite, out_dir, subject, str(ip_scale)],
        capture_output=True, text=True, env=env)
    if r.returncode != 0:
        raise HTTPException(status_code=500, detail=f"style-lock failed: {r.stderr[-800:]}")
    sheet = os.path.join(out_dir, "sheet.png")
    return FileResponse(sheet, media_type="image/png", filename=f"{job}_sheet.png",
                        headers={"X-Job": job})

@app.post("/sprite-to-mesh")
async def ep_full(file: UploadFile = File(...), subject: str = Form("the subject"),
                  tris: int = Form(18000), authorization: str = Header(None)):
    auth(authorization)
    job = uuid.uuid4().hex[:10]
    sprite = os.path.join(WORK, f"{job}_in.png")
    with open(sprite, "wb") as f: f.write(await file.read())
    out_dir = os.path.join(WORK, f"{job}_sheet")
    subprocess.run(
        [os.path.expanduser("~/rh3d-venv/bin/python"), os.path.join(SCR, "rh_stylelock.py"),
         sprite, out_dir, subject, "0.85"], capture_output=True, text=True, env=dict(os.environ))
    front = os.path.join(out_dir, "front.png")
    src = front if os.path.exists(front) else sprite   # fall back to raw sprite if style-lock skipped
    t = time.time()
    game_glb = reconstruct(src, job, tris)
    return FileResponse(game_glb, media_type="model/gltf-binary", filename=f"{job}_game.glb",
                        headers={"X-Gen-Seconds": f"{time.time()-t:.0f}", "X-Job": job,
                                 "X-Style-Locked": str(os.path.exists(front))})

@app.on_event("startup")
def _startup():
    if os.environ.get("RH3D_EAGER_LOAD", "1") == "1":
        load_models()
