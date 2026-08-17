#!/usr/bin/env bash
set -u
NS=/lambda/nfs/red-hope-east/red_hope
source $HOME/rh3d-venv/bin/activate
export HF_HOME=/lambda/nfs/red-hope-east/hf-cache HF_HUB_ENABLE_HF_TRANSFER=0 HF_HUB_DISABLE_XET=1
python $NS/scripts/rh_gensprite3.py $NS/io/style_ref2.png $NS/io/gensprite3_out 2
