#!/usr/bin/env bash
# Red Hope sprite->3D pipeline - idempotent box bootstrap (Lambda A10, Ubuntu 22.04).
# Rebuilds the box from a clean instance in minutes. Weights + assets persist on the
# Somnora-East NFS; the venv lives on ephemeral root and is recreated here.
# Reads NO credentials. Gated weight pulls are a separate step (needs HF_TOKEN env).
set -euo pipefail

NFS=/lambda/nfs/red-hope-east
NS=$NFS/red_hope
VENV=$HOME/rh3d-venv
BLENDER_VER=4.2.22

echo "=== [1/6] GPU / CUDA / torch baseline ==="
nvidia-smi --query-gpu=name,memory.total,driver_version --format=csv,noheader
nvcc --version | tail -1

echo "=== [2/6] namespace on persistent NFS ==="
mkdir -p "$NS"/{repos,weights-extra,io/inbox,io/outbox,logs,tools,bin}
mkdir -p "$NFS/hf-cache/hub"

echo "=== [3/6] system tools (git-lfs, ninja, blender runtime libs) ==="
sudo -n apt-get update -qq
sudo -n DEBIAN_FRONTEND=noninteractive apt-get install -y -qq \
  git-lfs ninja-build \
  libsm6 libxext6 libxrender1 libxi6 libxkbcommon0 libxfixes3 libxxf86vm1 \
  libgl1 libglu1-mesa libxrandr2 libxinerama1 libxcursor1 libxml2
git lfs install --skip-repo

echo "=== [4/6] Blender (pinned portable, on NFS) ==="
if [ ! -x "$NS/tools/blender-$BLENDER_VER-linux-x64/blender" ]; then
  curl -sSL -o "$NS/tools/blender.tar.xz" \
    "https://download.blender.org/release/Blender4.2/blender-$BLENDER_VER-linux-x64.tar.xz"
  tar xf "$NS/tools/blender.tar.xz" -C "$NS/tools" && rm "$NS/tools/blender.tar.xz"
fi
ln -sf "$NS/tools/blender-$BLENDER_VER-linux-x64/blender" "$NS/bin/blender"
"$NS/bin/blender" --background --version | grep -i blender | head -1

echo "=== [5/6] isolated Python venv + pipeline libs ==="
# Fully isolated (NO --system-site-packages: Lambda's system dist-packages carry a
# broken flatbuffers that pip 24.1+ refuses to resolve). Reproducible + pinned.
if [ ! -f "$VENV/bin/activate" ]; then
  python3 -m venv "$VENV"
fi
# shellcheck disable=SC1091
source "$VENV/bin/activate"
pip install -q --upgrade pip wheel
pip install -q torch==2.7.0 torchvision==0.22.0 --index-url https://download.pytorch.org/whl/cu128
pip install -q \
  "diffusers>=0.31" transformers accelerate safetensors huggingface_hub hf_transfer sentencepiece \
  trimesh pymeshlab rembg onnxruntime einops omegaconf \
  fastapi "uvicorn[standard]" python-multipart \
  opencv-python-headless pillow numpy scipy

echo "=== [6/6] self-check (GPU compute + imports) ==="
python - <<'PY'
import torch, diffusers, transformers, trimesh, rembg, fastapi, cv2, onnxruntime
x = torch.randn(2048, 2048, device="cuda"); _ = (x @ x).sum().item()
free, total = torch.cuda.mem_get_info()
print("OK torch", torch.__version__, "| cuda", torch.cuda.is_available(),
      "|", torch.cuda.get_device_name(0), f"| vram {free//2**20}/{total//2**20} MiB")
print("OK diffusers", diffusers.__version__, "transformers", transformers.__version__,
      "trimesh", trimesh.__version__)
PY

echo "=== project env.sh (sourced by skills; no creds) ==="
cat > "$NS/env.sh" <<EOF
export RH3D_NS=$NS
export HF_HOME=$NFS/hf-cache
export HUGGINGFACE_HUB_CACHE=$NFS/hf-cache/hub
export HF_HUB_ENABLE_HF_TRANSFER=1
export RH3D_VENV=$VENV
export PATH=$NS/bin:\$PATH
[ -f \$RH3D_VENV/bin/activate ] && source \$RH3D_VENV/bin/activate
EOF

echo "=== [7/8] 3D stage: Hunyuan3D 2.1 dedicated venv + custom ops ==="
# Separate venv: Hunyuan pins diffusers 0.30 / transformers 4.46 (conflict with newer stages).
# torch stays 2.7.0+cu128 (matches nvcc 12.8 = clean CUDA-extension builds); pulled from pip cache.
HY3D=$HOME/rh3d-hy3d
if [ ! -d "$NS/repos/Hunyuan3D-2.1" ]; then
  GIT_LFS_SKIP_SMUDGE=1 git clone --depth 1 \
    https://github.com/Tencent-Hunyuan/Hunyuan3D-2.1.git "$NS/repos/Hunyuan3D-2.1"
fi
if [ ! -f "$HY3D/bin/activate" ]; then python3 -m venv "$HY3D"; fi
# shellcheck disable=SC1091
source "$HY3D/bin/activate"
pip install -q --upgrade pip wheel
pip install -q torch==2.7.0 torchvision==0.22.0 --index-url https://download.pytorch.org/whl/cu128
pip install -q diffusers==0.30.0 transformers==4.46.0 accelerate==1.1.1 huggingface-hub==0.30.2 \
  hf_transfer safetensors trimesh==4.4.7 pymeshlab omegaconf einops pyyaml tqdm timm \
  opencv-python-headless rembg onnxruntime scikit-image imageio numpy scipy \
  realesrgan basicsr pytorch-lightning==1.9.5 xatlas open3d cupy-cuda12x pygltflib
# custom_rasterizer: NON-editable + no build isolation (setup.py imports torch; -e lacks PEP660)
( cd "$NS/repos/Hunyuan3D-2.1/hy3dpaint/custom_rasterizer" \
  && CUDA_HOME=/usr/local/cuda MAX_JOBS=16 pip install . --no-build-isolation )
# DifferentiableRenderer: c++ pybind
pip install -q pybind11
( cd "$NS/repos/Hunyuan3D-2.1/hy3dpaint/DifferentiableRenderer" && bash compile_mesh_painter.sh )
# make bpy import optional (no pip bpy wheel here; obj2glb.py handles GLB via standalone Blender)
MESHUTILS="$NS/repos/Hunyuan3D-2.1/hy3dpaint/DifferentiableRenderer/mesh_utils.py"
python3 -c "import pathlib; p=pathlib.Path('$MESHUTILS'); s=p.read_text(); \
p.write_text(s.replace('import bpy\n','try:\n    import bpy\nexcept Exception:\n    bpy = None\n',1)) if 'except Exception:\n    bpy = None' not in s else None"
# RealESRGAN ckpt
mkdir -p "$NS/repos/Hunyuan3D-2.1/hy3dpaint/ckpt"
[ -f "$NS/repos/Hunyuan3D-2.1/hy3dpaint/ckpt/RealESRGAN_x4plus.pth" ] || \
  curl -sSL -o "$NS/repos/Hunyuan3D-2.1/hy3dpaint/ckpt/RealESRGAN_x4plus.pth" \
  https://github.com/xinntao/Real-ESRGAN/releases/download/v0.1.0/RealESRGAN_x4plus.pth
python -c "import torch, custom_rasterizer, diffusers; print('OK hy3d venv: torch', torch.__version__, 'custom_rasterizer built')"

echo "=== [8/8] shape+paint weights (auto-download on first gen; not gated, no HF token) ==="
# Weights land in HY3DGEN_MODELS=$NS/weights-extra/hy3dgen on first rh_shape.py / rh_paint.py run.

echo "BOOTSTRAP COMPLETE. Env vars for the 3D stage:"
echo "  source $HY3D/bin/activate"
echo "  export HF_HOME=$NFS/hf-cache HF_HUB_ENABLE_HF_TRANSFER=0 HF_HUB_DISABLE_XET=1"
echo "  export HY3DGEN_MODELS=$NS/weights-extra/hy3dgen CUDA_HOME=/usr/local/cuda"
