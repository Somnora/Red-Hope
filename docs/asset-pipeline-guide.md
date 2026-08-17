# Building Red Hope assets yourself — the Manifold / Lambda GPU guide

_Written 2026-07-15. This is the human-runnable version of the five pipeline
skills (`lambda-bootstrap`, `style-lock`, `gen-3d`, `mesh-cleanup`,
`generation-server`). It turns a sprite into a game-ready textured GLB on your
own A100 box, with no web tools (Meshy/Tripo are retired)._

Everything below assumes you have the runtime config at `~/.config/rh3d/host.env`
(host IP, user, key path, NFS paths) and the SSH key at
`~/.ssh/lambda_burst_ed25519`. The box's **root disk is ephemeral** — it is wiped
when the instance terminates — but every weight, script, repo, and output lives
on the persistent `Somnora-East` NFS under `red_hope/`, so a recycled box is a
few-minutes rebuild, never a from-zero one.

---

## 0. The mental model (read this once)

```
 sprite.png ──▶ [style-lock] ──▶ clean multi-angle sheet ──▶ front frame
                                                                │
                                                                ▼
                                         [gen-3d shape] ──▶ raw dense GLB (geometry)
                                                                │
                                         [gen-3d paint] ──▶ + PBR textures (colour)
                                                                │
                                       [mesh-cleanup] ──▶ 18k game-ready textured GLB
                                                                │
                                                                ▼
                                               import into Unreal (headless commandlet)
```

- **style-lock** is optional polish — it locks the art style/palette before 3D
  so a rough or off-model sprite comes out on-brand. Skip it if your sprite is
  already clean and centered.
- **gen-3d** is the core: shape (geometry) then paint (colour). Two stages.
- **mesh-cleanup** decimates the dense output to a tri budget and grounds it.
- The **generation-server** is the same pipeline behind one HTTP call — use it
  when you want to fire many sprites without typing per-stage commands.

Two Python venvs live on the box on purpose: `~/rh3d-hy3d` (Hunyuan3D, diffusers
0.30) for shape+paint, and `~/rh3d-venv` (SDXL, diffusers 0.39) for style-lock.
They can't share a process; the server bridges them with a subprocess.

---

## 1. Bring the box up

### 1a. Launch the instance
The cheapest path is to ask Claude: **"launch the A100 for the pipeline"** — it
drives Manifold (`launch_gpu` → `wait_for_launch`) and reports the IP when SSH is
up. An SXM4 boot takes **15–40 minutes**; that is normal, not a hang.

If you'd rather do it by hand, launch a `gpu_1x_a100_sxm4` in `us-east-1` with
the `Somnora-East` filesystem attached from the Lambda dashboard, then update
`RH3D_HOST` in `~/.config/rh3d/host.env` to the new IP (it changes every launch).

> A 40GB A100 holds the shape+paint pipeline (~14.5 GB) resident with room for
> SDXL to load transiently. The older A10 (24 GB) also works but can't keep both
> sets resident at once.

### 1b. Bootstrap (rebuilds the ephemeral root)
```bash
source ~/.config/rh3d/host.env
SSH="ssh -i $RH3D_SSH_KEY $RH3D_USER@$RH3D_HOST"

# bootstrap.sh lives in the skill; push the current copy and run it:
scp -i $RH3D_SSH_KEY ~/.claude/skills/lambda-bootstrap/bootstrap.sh \
    $RH3D_USER@$RH3D_HOST:$RH3D_NS/bootstrap.sh
$SSH "bash $RH3D_NS/bootstrap.sh"
```
This is idempotent and safe to re-run. It installs the CUDA/torch venv, the
Blender X11 runtime libs, the pinned portable Blender 4.2.22, and self-checks the
GPU. **You will need this after every recycle** — a fresh root has no venv and
Blender's system libraries (`libSM.so.6` etc.) are gone until apt reinstalls
them. It ends with an `OK torch … | vram …` line when the box is ready.

> **If bootstrap's apt step 404s** on a mesa package (Ubuntu moved the version),
> run `sudo apt-get update` on the box first, then re-run bootstrap.

---

## 2. (Optional) style-lock — clean the sprite before 3D

One rough sprite in, four style-locked angle images + a stitched contact sheet
out (~1 min). Uses SDXL + IP-Adapter to hold identity, palette, and rendering.

```bash
source ~/.config/rh3d/host.env
SSH="ssh -i $RH3D_SSH_KEY $RH3D_USER@$RH3D_HOST"

scp -i $RH3D_SSH_KEY my_sprite.png $RH3D_USER@$RH3D_HOST:$RH3D_NS/io/inbox/robot.png
$SSH '
  NS='$RH3D_NS'; source ~/rh3d-venv/bin/activate
  export HF_HOME='$RH3D_NFS'/hf-cache HF_HUB_ENABLE_HF_TRANSFER=0 HF_HUB_DISABLE_XET=1
  python $NS/scripts/rh_stylelock.py $NS/io/inbox/robot.png $NS/io/outbox/robot_sheet \
    "a white-armored utilitarian robot, orange accent, matte, flat background"'
# pull the sheet back to look at it
scp -i $RH3D_SSH_KEY $RH3D_USER@$RH3D_HOST:$RH3D_NS/io/outbox/robot_sheet*.png ~/Desktop/
```

**Prompt discipline that matters (learned the hard way):**
- Keep the negative prompt hard-rejecting `ground, dirt, regolith, plinth,
  diorama, cutaway, pit, halo, vignette, glow, orb, sphere behind, radial
  gradient` and the whole prompt **under 77 CLIP tokens** or it's silently
  truncated.
- Use the `drone` sprite as the style anchor when you want the house palette
  (grey-white hull, orange accents, already ground-free). A battery-rack anchor
  runs teal-heavy.
- The IP-Adapter is scoped to a single style block (InstantStyle) so the anchor
  gives _style_, the prompt gives _subject_. Don't widen it — that carries the
  anchor's content and you get 15 recoloured copies of the reference.
- **Angle control is soft.** "Side/back" reads as 3/4 variants, not a true
  orthographic turnaround. For 3D you only need the front frame anyway.

---

## 3. gen-3d — sprite → textured mesh (the core)

### 3a. Shape (geometry)
```bash
$SSH '
  NS='$RH3D_NS'; source ~/rh3d-hy3d/bin/activate
  export HF_HOME='$RH3D_NFS'/hf-cache HF_HUB_ENABLE_HF_TRANSFER=0 HF_HUB_DISABLE_XET=1
  export HY3DGEN_MODELS=$NS/weights-extra/hy3dgen
  cd $NS/repos/Hunyuan3D-2.1
  nohup python $NS/scripts/rh_shape.py $NS/repos/Hunyuan3D-2.1 \
    $NS/io/inbox/robot.png $NS/io/outbox/robot_shape.glb > $NS/logs/robot.log 2>&1 &'
```
~31 s model load + ~84 s inference. Watch `logs/robot.log` for a `[receipt]`
line: `verts=… faces=… bounds=… watertight=False`. **Check the bounds are
figure-shaped** (tall/narrow), not a cube — a cube means the background wasn't
stripped and the mesh ate the backdrop.

### 3b. Paint (PBR colour)
```bash
$SSH '
  NS='$RH3D_NS'; source ~/rh3d-hy3d/bin/activate
  export HF_HOME='$RH3D_NFS'/hf-cache HF_HUB_ENABLE_HF_TRANSFER=0 HF_HUB_DISABLE_XET=1
  export HY3DGEN_MODELS=$NS/weights-extra/hy3dgen CUDA_HOME=/usr/local/cuda
  cd $NS/repos/Hunyuan3D-2.1
  python $NS/scripts/rh_paint.py $NS/repos/Hunyuan3D-2.1 \
    $NS/io/outbox/robot_shape.glb $NS/io/inbox/robot.png $NS/io/outbox/robot_tex.glb 6 512'
# paint writes an OBJ + textures; fold them into a GLB with standalone Blender:
$SSH '$RH3D_NS/bin/blender --background --python $RH3D_NS/scripts/obj2glb.py -- \
    $RH3D_NS/io/outbox/robot_tex.obj $RH3D_NS/io/outbox/robot_tex.glb'
```
~167 s first load (downloads paint weights once) + ~66 s texturing. Output is
base-colour + metallic + roughness maps — real PBR, the UE-friendly path (not
vertex colours).

### 3c. Input rules (both stages)
- Front view, subject centered, clean/transparent background.
- **Ground that _wraps_ the subject cannot be cropped** — an OreExtractor in a
  pit, a lander in a hangar bay, a room interior on a floating island all mesh
  into a plinth. Feed standalone objects on a flat background.
- Keep origins centered — an off-center pivot shifts placement in UE.

---

## 4. mesh-cleanup — make it game-ready

```bash
# decimate + weld + ground + re-export, KEEPING UVs/textures:
$SSH '$RH3D_NS/bin/blender --background --python $RH3D_NS/scripts/mesh_cleanup.py -- \
    $RH3D_NS/io/outbox/robot_tex.glb $RH3D_NS/io/outbox/robot_game.glb 18000'

# render a 3-angle quality preview (Cycles+CUDA; EEVEE fails headless):
$SSH '$RH3D_NS/bin/blender --background --python $RH3D_NS/scripts/render_preview.py -- \
    $RH3D_NS/io/outbox/robot_game.glb $RH3D_NS/io/outbox/robot_prev.png'
scp -i $RH3D_SSH_KEY $RH3D_USER@$RH3D_HOST:$RH3D_NS/io/outbox/robot_prev*.png ~/Desktop/
```
Cleanup welds duplicate verts (marching-cubes output is unwelded), collapse-
decimates to the tri budget (18k keeps textures cleanly; use 5–8k for background
props, higher for hero close-ups), recomputes normals, **recenters XY and drops
feet to Z=0** so it grounds in UE, and exports +Y-up. Look at the three preview
tiles before you trust it — a coherent figure from all angles, not a lumpy box.

Then bring the finished GLB home:
```bash
scp -i $RH3D_SSH_KEY $RH3D_USER@$RH3D_HOST:$RH3D_NS/io/outbox/robot_game.glb \
    ~/Desktop/Martians/assets/models/
```

---

## 5. Batch a whole roster (the fast path for many sprites)

Loading shape+paint once and streaming N sprites is far faster than per-model
reloading (paint load alone is ~167 s).
```bash
# stage every sprite into $RH3D_NS/io/batch_in/<name>.png, then:
$SSH '
  NS='$RH3D_NS'; source ~/rh3d-hy3d/bin/activate
  export HF_HOME='$RH3D_NFS'/hf-cache HF_HUB_ENABLE_HF_TRANSFER=0 HF_HUB_DISABLE_XET=1
  export HY3DGEN_MODELS=$NS/weights-extra/hy3dgen CUDA_HOME=/usr/local/cuda
  cd $NS/repos/Hunyuan3D-2.1
  python $NS/scripts/rh_batch.py $NS/repos/Hunyuan3D-2.1 $NS/io/batch_in $NS/io/outbox'
$SSH 'bash $RH3D_NS/scripts/batch_finalize.sh'   # obj2glb + cleanup 18k + preview, per model
```
Outputs land at `$RH3D_NS/io/batch_out/<name>_game.glb` + preview tiles, ~2 min
GPU per model. There is also a **queue worker** (`rh_queue.sh`) that drains
`io/queue/<job>/in/*.png` oldest-first and keeps polling, so the box never idles
between batches — drop sprites into a queue folder and walk away.

---

## 6. The generation-server (pipeline as one HTTP call)

When you want to fire sprites from your laptop or an Unreal HTTP node instead of
typing per-stage commands:
```bash
# start it on the box (holds shape+paint resident; ~175s to load then warm):
$SSH 'nohup bash $RH3D_NS/scripts/run_server.sh > $RH3D_NS/logs/server.log 2>&1 &'
# watch logs/server.log for "Application startup complete"

# from the laptop — rh_client.sh opens the tunnel, reads the token, fires, closes:
~/.claude/skills/generation-server/rh_client.sh health
~/.claude/skills/generation-server/rh_client.sh reconstruct sprite.png out.glb
~/.claude/skills/generation-server/rh_client.sh sprite-to-mesh sprite.png out.glb "a white robot"
```
It binds to **127.0.0.1:8700** (loopback only, never public) behind a bearer
token minted onto NFS. Reach it only through the SSH tunnel. It's a `nohup`
process, not a supervised service — if it dies, relaunch and models reload.

---

## 7. Getting GLBs into Unreal

The editor's live importer is the legacy FBX path; **GLB imports headlessly**
through the Interchange commandlet (project must be unlocked, i.e. editor
closed). The established loop, from the repo root:
```bash
"/Volumes/Unreal/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd" \
  "/Volumes/Unreal/red_hope/red_hope/red_hope.uproject" \
  -run=ImportAssets -source="$HOME/Desktop/Martians/assets/models/robot_game.glb" \
  -dest="/Game/RedHope/Art/Models/robot" -nosourcecontrol -replaceexisting
```
Then wire it: add the path to the `RealModelPaths` map in
`HandleBuildingAdded` (RedHope module) — one line per model — and the building
renders the real mesh, min-side-fit scaled and grounded on its bounds. Robots/
crew are Mass entities and go through the visualizer's skeletal/static-mesh path
instead (bigger than a one-liner — that's the animation phase already in the
tree). With the editor OPEN, GLB won't import directly; the batch converts
GLB→FBX in Blender on the box as a fallback.

Each imported model wears an `MI_<name>` instance of the `M_ModelTex` master
(BaseTex + Rough + a subtle EmissiveFloor = 0.08 × base colour — see the
polishing guide for why that floor exists and how to tune it).

---

## 8. Cost hygiene — shut the box down

The A100 bills at **$1.99/hr** while it runs and auto-idles after 30 min, but
don't rely on that for a long gap. When you're done:
- Ask Claude to **"sync outputs and terminate the box"** (it runs `sync_outputs`
  then `terminate_instance` through Manifold — the sync guards against losing
  anything on the ephemeral root), or
- terminate from the Lambda dashboard.

Everything you produced is already on the `Somnora-East` NFS, so termination
only costs you the ephemeral venv, which bootstrap rebuilds in minutes.

---

## Quick reference — one model, cold box, start to finish

```bash
# 1. ask Claude to launch the A100, or launch from the Lambda dashboard
source ~/.config/rh3d/host.env
SSH="ssh -i $RH3D_SSH_KEY $RH3D_USER@$RH3D_HOST"
# 2. bootstrap
scp -i $RH3D_SSH_KEY ~/.claude/skills/lambda-bootstrap/bootstrap.sh $RH3D_USER@$RH3D_HOST:$RH3D_NS/bootstrap.sh
$SSH "bash $RH3D_NS/bootstrap.sh"
# 3. sprite up
scp -i $RH3D_SSH_KEY thing.png $RH3D_USER@$RH3D_HOST:$RH3D_NS/io/inbox/thing.png
# 4. shape -> paint -> obj2glb -> cleanup  (commands in sections 3–4)
# 5. GLB home + import into UE (section 7)
scp -i $RH3D_SSH_KEY $RH3D_USER@$RH3D_HOST:$RH3D_NS/io/outbox/thing_game.glb ~/Desktop/Martians/assets/models/
# 6. terminate the box (section 8)
```

**Never** put the SSH key or the server API token into a committed file — they
live in `~/.ssh/` and on NFS respectively, and the skills read them at runtime.

## Normal maps: the gap, the stopgap, and the real fix (2026-08-17)

**The gap.** Neither master material had a normal input until 2026-08-17. Both
`M_RH_Master` and `M_RH_Character` dumped as `Normal <- <none>`, and only 6
normal maps existed in the whole art tree - all hand-authored surfaces. Of the
~700 GENERATED models: zero. TRELLIS.2 and Hunyuan both emit baseColor +
metallic + roughness and no normal, so every prop, building and crew member
shaded off nothing but its decimated vertex normals. On a large flat panel that
reads as soft irregular light/dark patching, which is the "splotchy" the
director reported three times across three sessions. Re-baking the ALBEDO could
never fix it, because the albedo was never the defect. `M_RH_Character` was
worse still: it had only ONE texture parameter, so every walker's imported
`*_metallic-*_roughness` map sat inert on disk, bound by nothing.

**The stopgap, now shipped.** `scripts/gpu/rh_derive_normals.py` derives a
tangent normal from the albedo's luminance (local blur - wide blur high-pass ->
Sobel), and `rh_wire_normals.py` / `rh_wire_crew_normals.py` import it as
TC_NORMALMAP + sRGB off and bind it. Generated paint puts real surface
information in its luminance - panel gaps, rivets, weld beads, grille slots are
all painted darker than the plate around them - so this recovers genuine relief
(22 models at 24-41%, 20 crew at 25-30%). It is an approximation and it says so:
it cannot invent detail the paint does not describe, and a dark PAINTED marking
reads as a dent, which is why crew strength is 0.75 rather than 1.0 (a suit is
cloth, and machinery-strength relief on fabric reads as crumpled foil).

**The real fix, for future batches.** Keep TRELLIS.2's PRE-DECIMATION mesh and
bake a real normal from it in Blender before the high-poly is discarded. Today
the bake decimates and throws the high-poly away, which is why the 700 assets
already on disk have nothing to bake from. Any new batch should retain it.

**Strength was tuned on an isolated render, not guessed.** The first pass shipped
at detail-blur 1.1 / strength 1.0 and was wrong in a way no in-game frame could
show: a controlled Blender A/B of one locker (same mesh, same light rig, one
variable) differed on 29% of pixels and revealed the map was OVER-COOKED - real
louvre slots and door seams came through beautifully, and every flat plate also
picked up a hammered orange-peel pebbling because per-texel paint noise became
relief, with painted markings embossing as dents. Blur 2.4 removes the pebbling
while keeping the construction detail; material strength 0.5 (models) / 0.35
(crew) keeps the remaining marking-emboss subtle. `rh_toggle_surface_detail.py`
flips the whole set for that A/B and carries the tuned values as its ON state.

**Two things the in-game A/B established, both worth knowing before spending
more on surface fidelity.** First, floor-label rendering and day/night state
dominate a naive frame diff - the pixel-diff hot spots in three separate
comparisons were LABELS, not shading, which is why the honest instrument is a
single mesh under a fixed light rig. Second, at the distances the strategy camera
actually reaches, the entire normal-map change moves 0.08-0.29% of pixels: it is
essentially invisible in gameplay and only matters at the close range the
director inspects at by WASD-ing in. That is a reason to stop investing in
close-range surface fidelity, not a reason to invest more.

**Both paths are opt-in.** `UseNormTex` and `UseMRTex` default to 0 in both
masters, lerping to a flat (0,0,1) normal and the scalar roughness, so any
instance authored before this is bit-identical until its MI opts in. Verified:
the 13 prop EmissiveAmount/Mask overrides were byte-identical before and after
the master graph rebuild.
