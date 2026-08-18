#!/usr/bin/env bash
# icedrill + pylon through the real-normals lane, one bootstrap. REPO COPY of
# NFS red_hope/scripts/rh_batch2_bake.sh - the multi-asset template: extend
# the for-loop, one bootstrap amortized across the batch.
# LESSON (pylon, 2026-08-18): TRELLIS.2 fuses openwork lattice into a thin
# solid trunk. Reference designs for this lane want SOLID silhouettes -
# monopole over lattice, closed shells over trusses.
set -u
NS=/lambda/nfs/red-hope-east/red_hope
OUT=$NS/io/batch2/out
mkdir -p $OUT/logs
set -a; . /workspace/ephemeral/hf.env; set +a
echo "=== bootstrap ($(date +%H:%M:%S)) ==="
bash $NS/bootstrap_trellis2.sh > $NS/logs/bootstrap_batch2.log 2>&1
grep -q TRELLIS2_ENV_READY $NS/logs/bootstrap_batch2.log || { echo BOOTSTRAP FAILED; exit 1; }
export HF_HOME=/lambda/nfs/red-hope-east/hf-cache
export HUGGINGFACE_HUB_CACHE=$HF_HOME/hub
export RH_PERMISSIVE_RASTER=1
export RH_KEEP_HIPOLY=1
sudo apt-get install -y -q libsm6 libxext6 libxrender1 libxi6 libxxf86vm1 libxfixes3 libgl1 > /dev/null 2>&1 || true
cd "$NS/repos/TRELLIS.2"
for n in icedrill pylon; do
  echo "=== $n mesh ($(date +%H:%M:%S)) ==="
  /usr/bin/python3 $NS/scripts/rh_trellis2.py "$NS/io/batch2/in/$n.png" "$OUT/$n.glb" 12000 > $OUT/logs/$n.log 2>&1 \
    && echo "  mesh OK" || { echo "  mesh FAILED"; tail -6 $OUT/logs/$n.log; continue; }
  echo "=== $n normal ($(date +%H:%M:%S)) ==="
  $NS/tools/blender-4.2.22-linux-x64/blender -b -P $NS/scripts/rh_bake_normal.py -- \
      "$OUT/$n.glb" "$OUT/${n}_hi.glb" "$OUT/${n}_real_normal.png" 2048 0.05 > $OUT/logs/${n}_nrm.log 2>&1 \
    && echo "  normal OK" || { echo "  normal FAILED"; tail -4 $OUT/logs/${n}_nrm.log; }
done
echo "BATCH2_DONE $(date +%H:%M:%S)"
