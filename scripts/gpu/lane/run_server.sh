#!/usr/bin/env bash
set -e
NS=/lambda/nfs/red-hope-east/red_hope
source $HOME/rh3d-hy3d/bin/activate
export RH3D_NS=$NS
export HF_HOME=/lambda/nfs/red-hope-east/hf-cache HF_HUB_ENABLE_HF_TRANSFER=0 HF_HUB_DISABLE_XET=1
export HY3DGEN_MODELS=$NS/weights-extra/hy3dgen CUDA_HOME=/usr/local/cuda
# API token: read existing or mint one (kept off git, on NFS)
if [ ! -f $NS/.api_token ]; then head -c 24 /dev/urandom | base64 | tr -dc A-Za-z0-9 > $NS/.api_token; fi
export RH3D_API_TOKEN=$(cat $NS/.api_token)
cd $NS/repos/Hunyuan3D-2.1
exec uvicorn rh_server:app --host 127.0.0.1 --port 8700 --app-dir $NS/scripts
