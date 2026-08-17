export RH3D_NS=/lambda/nfs/red-hope-east/red_hope
export HF_HOME=/lambda/nfs/red-hope-east/hf-cache
export HUGGINGFACE_HUB_CACHE=/lambda/nfs/red-hope-east/hf-cache/hub
export HF_HUB_ENABLE_HF_TRANSFER=1
export RH3D_VENV=/home/ubuntu/rh3d-venv
export PATH=/lambda/nfs/red-hope-east/red_hope/bin:$PATH
[ -f $RH3D_VENV/bin/activate ] && source $RH3D_VENV/bin/activate
