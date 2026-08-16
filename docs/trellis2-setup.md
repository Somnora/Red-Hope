# TRELLIS.2-4B on Lambda: a working setup, and the four traps (2026-08-15)

The crop assets were regenerated-from-scratch candidates because their textures
turned out to be **aerial satellite views of a city** (verified by exporting
`crop_root_1_textured` from the shipped game: roads, rooftops, parking lots,
treetops - not one leaf). No mesh repair could fix that; the REFERENCE IMAGE
was wrong at generation time. Hence a new image-to-3D lane.

`microsoft/TRELLIS.2-4B` is the right model for this project: single-image
conditioned and it **outputs PBR materials directly**, which removes the
separate paint stage where the old "gray statue" failures lived.

## Provisioning (Lambda Cloud, API-driven)

The Manifold app terminal exports `LAMBDA_API_KEY`, so the cloud is drivable
over plain HTTP with no MCP:

    curl -s -u "$LAMBDA_API_KEY:" https://cloud.lambdalabs.com/api/v1/instances
    curl -s -u "$LAMBDA_API_KEY:" .../instance-operations/launch -d '{...}'
    curl -s -u "$LAMBDA_API_KEY:" .../instance-operations/terminate -d '{...}'

**Region is not free choice.** The 227 GB `Somnora-East` filesystem (weights,
HF cache, pipeline) lives in `us-east-1`, and a filesystem can only attach to
an instance in its own region. `gpu_1x_a100_sxm4` at $1.99/hr is available
there; the cheaper/faster H100s are in other regions and would strand the data.

    region us-east-1, gpu_1x_a100_sxm4, ssh key lambda-burst-ed25519,
    file system Somnora-East

## The four install traps (all cost a cycle each)

1. **The repo is `microsoft/TRELLIS.2`, not `microsoft/TRELLIS`.** The latter
   is v1 and clones silently, shipping a `trellis` package where the model
   expects `trellis2`.
2. **`setup.sh --new-env` does not create the conda env.** Everything lands in
   system python3.10 user-site instead, and `conda activate trellis2` then
   fails with EnvironmentNameNotFound. Just call `/usr/bin/python3`.
3. **apt's scipy/scikit-learn are built against numpy 1.x**; TRELLIS pulls
   numpy 2, so both raise "numpy.dtype size changed". Fix:
   `pip install --user -U scipy scikit-learn pandas`.
4. **nvdiffrast installs as a nameless package.** The build compiles it, pip
   registers it as `UNKNOWN 0.0.0`, and a LATER nameless package uninstalls it -
   so a build reporting `BUILD_EXIT=0` leaves you with no renderer and
   `o_voxel` fails to import. Fix: copy the `nvdiffrast` package dir into
   site-packages and hand-write a `dist-info/METADATA`, or install it before
   any other unnamed extension. **Never trust the build exit code; verify the
   import.** A copy of the working module + a 174-line `pip freeze` are saved
   at `Somnora-East/red_hope/wheels/`.

## Two API-level gotchas in the runner

- `Trellis2ImageTo3DPipeline.from_pretrained()` needs the **local snapshot
  dir**. Given the repo id, it treats each `ckpts/...` entry in `pipeline.json`
  as its own repo_id and 401s. `snapshot_download()` first, pass the path.
- Export is `o_voxel.postprocess.to_glb(vertices, faces, attr_volume, coords,
  attr_layout, voxel_size, aabb, decimation_target, texture_size, remesh...)`,
  **not** `mesh.export()`. Decimate HERE (art bible: ~8k tris for props) so the
  PBR attributes bake correctly rather than being re-baked in Blender.

Runner: `Somnora-East/red_hope/scripts/rh_trellis2.py <ref.png> <out.glb> [tris]`

## The Vertex model rename (2026-08-15, cost a batch)

Reference generation 404'd on **every** call with `Publisher model ... not
found`. It read like an auth failure and was not: ADC was valid. The preview
alias `gemini-3-pro-image-preview` was RETIRED when the model went GA. The
live id is **`gemini-3-pro-image`**.

Do not guess the replacement - the SDK enumerates it authoritatively, while a
raw REST probe 404s even for valid ids:

    genai.Client(vertexai=True, project=..., location="global").models.list()

Patched in `nb_gen_object.py` / `nb_gen_char.py`, repo + skill copies both.
`nb_batch_obj.py` now honours `RH_ROSTER` / `RH_OUT` so one driver serves any
roster instead of hardcoding the object batch.

## STILL BLOCKED - one director credential

**HF token with DINOv3 access.** TRELLIS.2's image encoder is
`facebook/dinov3-vitl16-pretrain-lvd1689m`. Needs: accept the licence on that
model page, create a token, `huggingface-cli login` (or HF_TOKEN).
`microsoft/TRELLIS.2-4B` itself is ungated.

Re-verified on-box 2026-08-16: an anonymous fetch of the DINOv3 `config.json`
returns **HTTP 401**, and a `find` across the whole 248 GB filesystem turns up
no dinov3 of any kind. The NFS has dinov2-giant (old Hunyuan lineage), which is
NOT a substitute - TRELLIS.2-4B's weights were trained against DINOv3 features,
so swapping the encoder would burn GPU time to produce garbage. Nor should an
unofficial re-upload be used to route around a licence the rights-holder gated.

**Status 2026-08-16, with the director's token (user `CitiznLame`):** the token
is valid (`whoami-v2` resolves), but the same file returns **HTTP 403**. Read the
two codes apart, because they mean different things and only one is actionable:

    401  no credentials          -> supply a token
    403  credentials, no grant   -> the ACCESS REQUEST has not been approved

The repo is `gated: manual`, i.e. a human reviews each request. Earlier notes in
this file called that "a 30-second click"; that was wrong. Submitting the form is
seconds, approval is an unbounded wait on Meta and is not something the project
controls. **Do not plan a session around it landing.** The crop work therefore
runs on Hunyuan3D 2.1 (installed, weights on the NFS, DINOv2-based and ungated),
with TRELLIS.2 held as a later quality pass.

Worth keeping in view: the mesher was never the root cause of the bad crops. The
reference images were - they were generated from art that textured as aerial
satellite views of a city. Good references through the OLD lane is expected to
fix them; TRELLIS.2 is an upgrade (direct PBR, no separate paint stage), not a
prerequisite.

## State on Somnora-East as of 2026-08-16 (verified, not assumed)

- `microsoft/TRELLIS.2-4B`: **fully downloaded, 16 GB, 0 `.incomplete` parts**,
  at `hf-cache/hub/models--microsoft--TRELLIS.2-4B`. This is what grew the
  filesystem to 248 GB. The download is DONE; do not re-do it.
- Runner `red_hope/scripts/rh_trellis2.py` present; both `repos/TRELLIS2` and
  `repos/TRELLIS.2` clones exist, so the runner's `sys.path` entry is valid.
- Working `nvdiffrast` + `pip freeze` at `red_hope/wheels/`.
- **The nine stripped crop references are parked at
  `red_hope/io/crop_refs/`** (1536 px RGBA, background removed) - so the next
  launch starts at meshing, not at uploading. Strip AppleDouble `._*` files
  after any macOS-made tarball; a glob would otherwise feed them to the model.

## Launch through Manifold, not raw curl

The 2026-08-15 note said "no MCP is involved". That is now wrong: the Manifold
MCP server IS connected, and it authenticates to Lambda on its own - the
`LAMBDA_API_KEY` in `Desktop/Manifold/.env` is not needed by an agent driving
through the MCP. Going through Manifold buys budget guards, an audit trail the
director reads, a 30-minute idle timeout, data rescue on terminate, and
`list_persistent_files` over SSH. The raw-curl path has none of that, and the
expensive failure here has always been a box left running, not anything
technical.

    list_launch_options -> gpu_1x_a100_sxm4 / us-east-1 / Somnora-East ($1.99/hr)
    launch_gpu with max_lifetime_seconds -> wait_for_launch -> run_command

Measured 2026-08-16: **the A100 booted in 4 minutes**, not the 15-40 the
playbook warns about for SXM. There is therefore no reason to hold a box open
"just in case" - relaunching is cheap.

(Also note `S3_ACCESS_KEY_ID` / `S3_SECRET_ACCESS_KEY` exist in that .env but are
EMPTY, which is why `list_persistent_files` cannot browse the filesystem with
nothing running. Filling them would make future state checks free.)

(RESOLVED 2026-08-15: gcloud ADC for Vertex.)

Nine crop references are rostered at `docs/crop_roster.json` (root/tall/vine x
seedling/growing/mature). Each desc now carries an explicit no-legs/no-stand/
no-cart clause on top of the generator's generic NEG block, because a grow tray
is a thing that plausibly stands on legs and a baked-in stand is exactly the
defect that put holes under the desks.
