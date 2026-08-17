#!/usr/bin/env bash
# Persistent A100 job queue so the box never sits idle.
# Each job = a dir under $NS/io/queue/<job>/in/*.png. Worker drains oldest-first
# through shape+paint+finalize into <job>/out/, marks <job>/done, then keeps
# polling for new job dirs. Waits for any in-flight batch first (avoid VRAM OOM).
# Add work:  put PNGs in $NS/io/queue/<newjob>/in/  ->  auto-picked up in <=30s
# Stop:      touch $NS/io/queue/STOP
set -u
NS=/lambda/nfs/red-hope-east/red_hope
REPO=$NS/repos/Hunyuan3D-2.1
BL=$NS/bin/blender
Q=$NS/io/queue
source $HOME/rh3d-hy3d/bin/activate
export RH3D_NS=$NS HF_HOME=/lambda/nfs/red-hope-east/hf-cache HF_HUB_ENABLE_HF_TRANSFER=0 HF_HUB_DISABLE_XET=1
export HY3DGEN_MODELS=$NS/weights-extra/hy3dgen CUDA_HOME=/usr/local/cuda
mkdir -p "$Q"
log(){ echo "[queue $(date '+%m-%d %H:%M:%S')] $*"; }

# Never co-load models with an in-flight batch (crew_run or another rh_batch).
while pgrep -f 'crew_run.sh' >/dev/null 2>&1; do log "waiting on crew_run.sh..."; sleep 15; done
log "queue worker online; draining $Q"

idle=0
while true; do
  [ -e "$Q/STOP" ] && { log "STOP found; exiting"; rm -f "$Q/STOP"; break; }
  job=""
  for d in "$Q"/*/; do
    [ -d "${d}in" ] || continue
    ls "${d}in"/*.png >/dev/null 2>&1 || continue
    [ -e "${d}done" ] && continue
    job="$d"; break
  done
  if [ -z "$job" ]; then
    idle=$((idle+1)); [ $((idle%20)) -eq 1 ] && log "idle; polling for new jobs (drop a dir in $Q/<job>/in/)"
    sleep 30; continue
  fi
  idle=0
  name=$(basename "$job")
  log "=== job '$name' start ==="
  mkdir -p "${job}out"
  TRIS=18000; [ -f "${job}tris" ] && TRIS=$(tr -dc 0-9 < "${job}tris")
  STRIP=0;    [ -f "${job}stripbase" ] && STRIP=1
  log "budget: ${TRIS} tris, stripbase=${STRIP}"
  python "$NS/scripts/rh_batch.py" "$REPO" "${job}in" "${job}out"
  for obj in "${job}out"/*_textured.obj; do
    [ -e "$obj" ] || continue
    base=$(basename "$obj" _textured.obj)
    log "finalize $base"
    "$BL" --background --python "$NS/scripts/obj2glb.py"        -- "$obj" "${job}out/${base}_textured.glb"                    2>&1 | grep -a "\[obj2glb\]"
    "$BL" --background --python "$NS/scripts/mesh_cleanup.py"   -- "${job}out/${base}_textured.glb" "${job}out/${base}_game.glb" "$TRIS" "$STRIP" 2>&1 | grep -a "\[cleanup\]"
    "$BL" --background --python "$NS/scripts/render_preview.py" -- "${job}out/${base}_game.glb" "${job}out/${base}_preview.png" textured 2>&1 | grep -a "\[preview\]"
  done
  touch "${job}done"
  log "=== job '$name' COMPLETE -> ${job}out ==="
done
