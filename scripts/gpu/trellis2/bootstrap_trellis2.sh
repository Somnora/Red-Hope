#!/usr/bin/env bash
# TRELLIS.2-4B lane bootstrap on a FRESH Lambda box. Idempotent-ish.
#   bash bootstrap_trellis2.sh
# Needs HF_TOKEN in env (DINOv3 is gated: manual; approved for this account
# 2026-08-16). The 16 GB TRELLIS.2-4B snapshot already lives on the NFS cache.
#
# The four traps this encodes, each of which cost a cycle in the 2026-08-15
# session and were then lost with the ephemeral root:
#  1. The repo is microsoft/TRELLIS.2, NOT microsoft/TRELLIS (v1, ships a
#     `trellis` package where the model wants `trellis2`).
#  2. setup.sh --new-env does NOT create the conda env; everything lands in
#     system python3.10 user-site. So: no --new-env, and call /usr/bin/python3.
#  3. apt's scipy/scikit-learn are built against numpy 1.x while TRELLIS pulls
#     numpy 2 -> "numpy.dtype size changed". Reinstall them AFTER the basics.
#  4. nvdiffrast installs as a NAMELESS package (pip records UNKNOWN 0.0.0) and
#     a later nameless package silently uninstalls it, so a build that reports
#     exit 0 can leave you with no renderer. We copy the known-good module from
#     the NFS instead of building, and VERIFY THE IMPORT rather than the exit code.
#
# TWO SOURCE PATCHES LIVE ON THE NFS AND SURVIVE THE BOX (do not re-derive):
#  a. scripts/rh_trellis2.py stubs trellis2.pipelines.rembg.BiRefNet, because
#     from_pretrained constructs briaai/RMBG-2.0 UNCONDITIONALLY and that repo is
#     gated to this account. preprocess_image() only calls it when the input has
#     no alpha, and every reference we feed is rembg-stripped RGBA, so it is
#     built and never used. The runner asserts the alpha and aborts loudly if an
#     opaque image ever arrives.
#  b. repos/*/trellis2/modules/image_feature_extractor.py resolves the DINOv3
#     blocks as model.model.layer (transformers 5.x nests DINOv3ViTEncoder) with
#     a fallback to model.layer. Upstream assumes the flat layout and dies with
#     "'DINOv3ViTModel' object has no attribute 'layer'".
set -u
NS=/lambda/nfs/Somnora-East/red_hope
REPO=$NS/repos/TRELLIS.2
SITE=$HOME/.local/lib/python3.10/site-packages
export PIP_USER=1
export HF_HOME=/lambda/nfs/Somnora-East/hf-cache
export HUGGINGFACE_HUB_CACHE=$HF_HOME/hub
export PATH=$HOME/.local/bin:$PATH
mkdir -p "$SITE"

echo "=== [1/6] torch 2.6.0+cu124 (the pin the working freeze recorded) ==="
/usr/bin/python3 -m pip install -q torch==2.6.0 torchvision==0.21.0 \
  --index-url https://download.pytorch.org/whl/cu124 || echo "  torch install returned $?"

echo "=== [2/6] repo basics ==="
cd "$REPO" && bash setup.sh --basic 2>&1 | tail -5

echo "=== [3/6] trap 3: numpy-2-compatible scipy/sklearn/pandas ==="
/usr/bin/python3 -m pip install -q -U scipy scikit-learn pandas || true

echo "=== [4/6] extensions, IN DEPENDENCY ORDER ==="
# Learned 2026-08-16 by running it: o_voxel's imports only reveal themselves one
# layer at a time - plyfile, then utils3d, then flex_gemm, then cumesh - so each
# earlier attempt reported a different single missing module. Install them all up
# front rather than discovering them serially.
/usr/bin/python3 -m pip install -q plyfile "utils3d==0.1.1" || true
cd "$REPO" && bash setup.sh --flash-attn 2>&1 | tail -2   # prebuilt wheel, fast
cd "$REPO" && bash setup.sh --flexgemm  2>&1 | tail -2
cd "$REPO" && bash setup.sh --cumesh    2>&1 | tail -2
cd "$REPO" && bash setup.sh --o-voxel   2>&1 | tail -2

echo "=== [5/6] nvdiffrast: BUILD, and build it LAST ==="
# Trap 4, corrected 2026-08-16. The old note said "copy the saved module from
# wheels/". That could never have worked: the compiled extension is
# _nvdiffrast_c, a SIBLING top-level module in site-packages, not a child of the
# nvdiffrast/ package directory - so a copy of the package dir carried only .py
# files and ops.py fails at `import _nvdiffrast_c` on line 12. It must be built.
# Building it LAST also removes the original trap entirely: the danger was a
# LATER nameless package uninstalling it (pip records it as UNKNOWN 0.0.0), and
# nothing installs after it now.
# BOTH HALVES ARE REQUIRED, and they come from DIFFERENT PLACES. Measured on a
# clean box 2026-08-16: `setup.sh --nvdiffrast` compiles and installs
# _nvdiffrast_c*.so, but because pip registers the project as the nameless
# UNKNOWN 0.0.0 it installs ONLY a dist-info stub - the pure-Python nvdiffrast/
# package directory never lands, and the import fails with
# "No module named 'nvdiffrast'" even though the extension is right there.
# So: build for the .so, then copy the saved package dir for the .py. Either
# step alone leaves a half-installed renderer that reports success.
# RH_NO_NVDIFFRAST=1 is the SHIPPING path and should be the default choice.
# nvdiffrast is NVIDIA Source Code License (1-Way Commercial) - non-commercial
# only - and it was only ever serving two calls in the texture bake: a UV-space
# triangle rasterize and a barycentric interpolate. scripts/rh_uv_rasterizer.py
# is our own permissive replacement for exactly those. Not installing it at all
# is a cleaner claim than installing and deleting it, which is what the
# 2026-08-16 proof run had to do.
if [ "${RH_NO_NVDIFFRAST:-0}" = "1" ]; then
  echo "  SKIPPED (RH_NO_NVDIFFRAST=1) - the permissive rasterizer covers it."
else
cd "$REPO" && bash setup.sh --nvdiffrast 2>&1 | tail -2
cp -r "$NS/wheels/nvdiffrast" "$SITE/" 2>/dev/null || true
cp -r "$NS/wheels/nvdiffrast-0.3.3.dist-info" "$SITE/" 2>/dev/null || true
[ -f "$SITE"/_nvdiffrast_c*.so ] || cp "$NS/wheels"/_nvdiffrast_c*.so "$SITE/" 2>/dev/null || true
fi

echo "=== [6/6] VERIFY IMPORTS (never trust a build exit code) ==="
cd "$REPO" && PYTHONPATH="$REPO" RH_NS="$NS" RH_NO_NVDIFFRAST="${RH_NO_NVDIFFRAST:-0}" /usr/bin/python3 - <<'PY'
import os, sys, importlib
ok = True
MODS = ["torch", "nvdiffrast.torch", "o_voxel", "trellis2", "flash_attn",
        "utils3d", "scipy", "sklearn", "plyfile", "cumesh", "flex_gemm"]
if os.environ.get("RH_NO_NVDIFFRAST") == "1":
    # o_voxel does `import nvdiffrast.torch as dr` at module level, so the
    # stand-in has to be registered BEFORE o_voxel is imported, not after.
    MODS.remove("nvdiffrast.torch")
    try:
        importlib.import_module("nvdiffrast")
        print("  FAIL nvdiffrast is INSTALLED - the licence-clean run wanted it absent")
        ok = False
    except ModuleNotFoundError:
        print("  OK   nvdiffrast absent (ModuleNotFoundError), as required")
    sys.path.insert(0, os.path.join(os.environ["RH_NS"], "scripts"))
    import rh_uv_rasterizer
    rh_uv_rasterizer.install(flip_y=False)
    print("  OK   rh_uv_rasterizer registered as nvdiffrast.torch")
for m in MODS:
    try:
        importlib.import_module(m)
        print("  OK   %s" % m)
    except Exception as e:
        ok = False
        print("  FAIL %s -> %s" % (m, str(e)[:120]))
import torch
print("  torch %s cuda=%s %s" % (torch.__version__, torch.cuda.is_available(),
      torch.cuda.get_device_name(0) if torch.cuda.is_available() else ""))
print("TRELLIS2_ENV_READY" if ok else "TRELLIS2_ENV_INCOMPLETE")
PY
