#!/usr/bin/env bash
# Electrolyzer: single-asset bake on the real-normals path. REPO COPY of the
# NFS script (NFS: red-hope-east:red_hope/scripts/rh_electrolyzer_bake.sh) -
# anything that runs on a rented box and is not reproducible from this repo is
# one terminate away from gone.
# Chain: bootstrap_trellis2.sh -> rh_trellis2.py 12k tris with RH_KEEP_HIPOLY
# -> Blender rh_bake_normal.py (2048, 5cm cage) real normal from the kept
# hi-poly. Template for every single-asset bake after it: copy, rename, point
# at a new io/<asset>/ pair.
set -u
NS=/lambda/nfs/red-hope-east/red_hope
OUT=$NS/io/electrolyzer/out
mkdir -p $OUT/logs
set -a; . /workspace/ephemeral/hf.env; set +a
echo "=== bootstrap ($(date +%H:%M:%S)) ==="
bash $NS/bootstrap_trellis2.sh > $NS/logs/bootstrap_electro.log 2>&1
if ! grep -q TRELLIS2_ENV_READY $NS/logs/bootstrap_electro.log; then
  echo "BOOTSTRAP FAILED"; tail -8 $NS/logs/bootstrap_electro.log; exit 1
fi
echo "env ready"
export HF_HOME=/lambda/nfs/red-hope-east/hf-cache
export HUGGINGFACE_HUB_CACHE=$HF_HOME/hub
export RH_PERMISSIVE_RASTER=1
export RH_KEEP_HIPOLY=1
sudo apt-get install -y -q libsm6 libxext6 libxrender1 libxi6 libxxf86vm1 libxfixes3 libgl1 > /dev/null 2>&1 || true
cd "$NS/repos/TRELLIS.2"
echo "=== mesh ($(date +%H:%M:%S)) ==="
if /usr/bin/python3 $NS/scripts/rh_trellis2.py "$NS/io/electrolyzer/in/electrolyzer.png" "$OUT/electrolyzer.glb" 12000 > $OUT/logs/electrolyzer.log 2>&1; then
  echo "  mesh OK $(stat -c %s $OUT/electrolyzer.glb) bytes (hi: $(stat -c %s $OUT/electrolyzer_hi.glb 2>/dev/null || echo MISSING))"
else
  echo "  mesh FAILED"; tail -6 $OUT/logs/electrolyzer.log; exit 1
fi
echo "=== normal bake ($(date +%H:%M:%S)) ==="
if $NS/tools/blender-4.2.22-linux-x64/blender -b -P $NS/scripts/rh_bake_normal.py -- \
    "$OUT/electrolyzer.glb" "$OUT/electrolyzer_hi.glb" "$OUT/electrolyzer_real_normal.png" 2048 0.05 > $OUT/logs/electrolyzer_nrm.log 2>&1; then
  echo "  normal OK $(stat -c %s $OUT/electrolyzer_real_normal.png) bytes"
else
  echo "  normal FAILED"; tail -4 $OUT/logs/electrolyzer_nrm.log; exit 1
fi
echo "ELECTROLYZER_DONE $(date +%H:%M:%S)"
