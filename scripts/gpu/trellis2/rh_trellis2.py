#!/usr/bin/env python3
"""Red Hope: one reference image -> game-ready PBR GLB via TRELLIS.2-4B.

  /usr/bin/python3 rh_trellis2.py <ref.png> <out.glb> [decimation_target]

Learned the hard way, do not "simplify" these away:
- from_pretrained() must be given the LOCAL snapshot dir. Hand it the repo id
  and it treats every entry in pipeline.json ("ckpts/...") as its own repo_id
  and 401s. snapshot_download first, pass the path.
- Export is o_voxel.postprocess.to_glb(...), not mesh.export().
- The repo is microsoft/TRELLIS.2 on GitHub; microsoft/TRELLIS is v1 and
  clones silently with a 'trellis' package instead of 'trellis2'.
- setup.sh --new-env does not reliably create the conda env; deps land in
  system python3.10 user-site, so call /usr/bin/python3 explicitly. apt's
  scipy/scikit-learn are built against numpy 1.x and must be pip-reinstalled
  to match the numpy 2 TRELLIS pulls in.

decimation_target is the game-facing knob: the art bible budgets ~8k tris for
props, and decimating HERE keeps the PBR attributes baked correctly instead of
re-baking them in Blender afterwards.
"""
import os
import sys
import time

os.environ["OPENCV_IO_ENABLE_OPENEXR"] = "1"
os.environ["PYTORCH_CUDA_ALLOC_CONF"] = "expandable_segments:True"
os.environ.setdefault("HF_HOME", "/lambda/nfs/Somnora-East/hf-cache")
sys.path.insert(0, "/lambda/nfs/Somnora-East/red_hope/repos/TRELLIS2")

if os.environ.get("RH_PERMISSIVE_RASTER") == "1":
    sys.path.insert(0, "/lambda/nfs/Somnora-East/red_hope/scripts")
    import rh_uv_rasterizer as _rh_raster
    _rh_raster.install(flip_y=os.environ.get("RH_RASTER_FLIP_Y") == "1")
    print("PERMISSIVE_RASTER installed (flip_y=%s)"
          % (os.environ.get("RH_RASTER_FLIP_Y") == "1"), flush=True)

from PIL import Image
from huggingface_hub import snapshot_download
from trellis2.pipelines import Trellis2ImageTo3DPipeline
import o_voxel

# --- gated-rembg stub (2026-08-16) -------------------------------------------
# from_pretrained() constructs BiRefNet(model_name="briaai/RMBG-2.0")
# UNCONDITIONALLY, and that repo is gated to this account, so the pipeline could
# not even load. But preprocess_image() only calls the background remover when
# the input has NO usable alpha ("if has alpha channel, use it directly"), and
# every reference we feed it is rembg-stripped RGBA. So the model is constructed
# and never used. Stub the constructor, and assert the alpha below rather than
# assume it - if an opaque image ever arrives, this must fail loudly instead of
# silently meshing the background along with the subject.
from trellis2.pipelines import rembg as _rembg


class _RembgStub:
    def __init__(self, *a, **k):
        pass

    def to(self, *a, **k):
        return self

    def __call__(self, *a, **k):
        raise RuntimeError("rembg stub invoked: input image had no alpha channel")


_rembg.BiRefNet = _RembgStub
# -----------------------------------------------------------------------------

src, out = sys.argv[1], sys.argv[2]
target = int(sys.argv[3]) if len(sys.argv) > 3 else 8000

local = snapshot_download("microsoft/TRELLIS.2-4B")
print("SNAPSHOT %s" % local, flush=True)

t0 = time.time()
pipe = Trellis2ImageTo3DPipeline.from_pretrained(local)
pipe.cuda()
print("PIPE_READY %.1fs" % (time.time() - t0), flush=True)

img = Image.open(src).convert("RGBA")
import numpy as _np
_alpha = _np.array(img)[:, :, 3]
if _np.all(_alpha == 255):
    raise SystemExit("ABORT: %s has no alpha; the rembg model is stubbed, so the "
                     "background would be meshed as geometry. Strip it first." % src)
print("ALPHA_OK coverage %.1f%%" % (100.0 * (_alpha > 0).mean()), flush=True)
t1 = time.time()
mesh = pipe.run(img, seed=1)[0]
print("RUN_DONE %.1fs" % (time.time() - t1), flush=True)

mesh.simplify(16777216)  # nvdiffrast limit

glb = o_voxel.postprocess.to_glb(
    vertices=mesh.vertices,
    faces=mesh.faces,
    attr_volume=mesh.attrs,
    coords=mesh.coords,
    attr_layout=mesh.layout,
    voxel_size=mesh.voxel_size,
    aabb=[[-0.5, -0.5, -0.5], [0.5, 0.5, 0.5]],
    decimation_target=target,
    texture_size=2048,
    remesh=True,
    remesh_band=1,
    remesh_project=0,
    verbose=False,
)
glb.export(out)
print("EXPORTED %s" % out, flush=True)
