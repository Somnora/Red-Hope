"""Run the Red Hope crop references through TRELLIS.2-4B -> textured GLB.

Per-asset, resumable, and it writes a receipt line for every stage so a failure
names itself instead of vanishing. GLBs land in io/crops/out/<id>.glb.
"""
import os, sys, time, traceback
os.environ['OPENCV_IO_ENABLE_OPENEXR'] = '1'
os.environ['PYTORCH_CUDA_ALLOC_CONF'] = 'expandable_segments:True'

IN = "/lambda/nfs/red-hope-east/red_hope/io/crops/in"
OUT = "/lambda/nfs/red-hope-east/red_hope/io/crops/out"
os.makedirs(OUT, exist_ok=True)

from PIL import Image
import torch
from trellis2.pipelines import Trellis2ImageTo3DPipeline
import o_voxel

ids = sys.argv[1:] or sorted(x[:-4] for x in os.listdir(IN) if x.endswith(".png"))
print(f"[batch] {len(ids)} assets: {ids}", flush=True)

print("[batch] loading TRELLIS.2-4B ...", flush=True)
t0 = time.time()
pipe = Trellis2ImageTo3DPipeline.from_pretrained("microsoft/TRELLIS.2-4B")
pipe.cuda()
print(f"[batch] pipeline ready in {time.time()-t0:.0f}s", flush=True)

ok = fail = 0
for i, oid in enumerate(ids, 1):
    dst = os.path.join(OUT, f"{oid}.glb")
    if os.path.exists(dst) and os.path.getsize(dst) > 50000:
        print(f"[{i}/{len(ids)}] {oid} EXISTS, skip", flush=True); ok += 1; continue
    try:
        t = time.time()
        img = Image.open(os.path.join(IN, f"{oid}.png"))
        mesh = pipe.run(img)[0]
        mesh.simplify(16777216)
        print(f"[{i}/{len(ids)}] {oid} mesh in {time.time()-t:.0f}s -> glb ...", flush=True)
        glb = o_voxel.postprocess.to_glb(
            vertices=mesh.vertices, faces=mesh.faces, attr_volume=mesh.attrs,
            coords=mesh.coords, attr_layout=mesh.layout, voxel_size=mesh.voxel_size,
            aabb=[[-0.5, -0.5, -0.5], [0.5, 0.5, 0.5]],
            decimation_target=120000,   # game-tier: finishing lane decimates further
            texture_size=2048, remesh=True, remesh_band=1, remesh_project=0,
            verbose=False,
        )
        glb.export(dst, extension_webp=False)
        sz = os.path.getsize(dst) // 1024
        print(f"[{i}/{len(ids)}] {oid} OK {sz}KB total {time.time()-t:.0f}s", flush=True)
        ok += 1
        del mesh, glb; torch.cuda.empty_cache()
    except Exception:
        fail += 1
        print(f"[{i}/{len(ids)}] {oid} FAILED", flush=True)
        traceback.print_exc()
        torch.cuda.empty_cache()
print(f"[batch] done ok={ok} fail={fail}", flush=True)
