#!/usr/bin/env bash
# waterplant + battery + lander2 + stockpile through the real-normals lane,
# one bootstrap. REPO COPY of NFS red_hope/scripts/rh_batch4_bake.sh.
# These four retire the last of the oldest-albedo "TV static" family. Their
# references preserve the mixed-set identity verdicts (battery = display
# panels, waterplant = tanks-and-pipes, lander = splayed descent stage) and
# the solid-silhouette rule (stockpile = one strapped block).
set -u
NS=/lambda/nfs/red-hope-east/red_hope
OUT=$NS/io/batch4/out
mkdir -p $OUT/logs
set -a; . /workspace/ephemeral/hf.env; set +a
echo "=== bootstrap ($(date +%H:%M:%S)) ==="
bash $NS/bootstrap_trellis2.sh > $NS/logs/bootstrap_batch4.log 2>&1
grep -q TRELLIS2_ENV_READY $NS/logs/bootstrap_batch4.log || { echo BOOTSTRAP FAILED; exit 1; }
export HF_HOME=/lambda/nfs/red-hope-east/hf-cache
export HUGGINGFACE_HUB_CACHE=$HF_HOME/hub
export RH_PERMISSIVE_RASTER=1
export RH_KEEP_HIPOLY=1
sudo apt-get install -y -q libsm6 libxext6 libxrender1 libxi6 libxxf86vm1 libxfixes3 libgl1 > /dev/null 2>&1 || true
cd "$NS/repos/TRELLIS.2"
for n in waterplant battery lander2 stockpile; do
  echo "=== $n mesh ($(date +%H:%M:%S)) ==="
  /usr/bin/python3 $NS/scripts/rh_trellis2.py "$NS/io/batch4/in/$n.png" "$OUT/$n.glb" 12000 > $OUT/logs/$n.log 2>&1 \
    && echo "  mesh OK" || { echo "  mesh FAILED"; tail -6 $OUT/logs/$n.log; continue; }
  echo "=== $n normal ($(date +%H:%M:%S)) ==="
  $NS/tools/blender-4.2.22-linux-x64/blender -b -P $NS/scripts/rh_bake_normal.py -- \
      "$OUT/$n.glb" "$OUT/${n}_hi.glb" "$OUT/${n}_real_normal.png" 2048 0.05 > $OUT/logs/${n}_nrm.log 2>&1 \
    && echo "  normal OK" || { echo "  normal FAILED"; tail -4 $OUT/logs/${n}_nrm.log; }
done
echo "BATCH4_DONE $(date +%H:%M:%S)"
