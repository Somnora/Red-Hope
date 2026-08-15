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

## STILL BLOCKED - two director credentials

1. **HF token with DINOv3 access.** TRELLIS.2's image encoder is
   `facebook/dinov3-vitl16-pretrain-lvd1689m`, a GATED repo. The NFS cache has
   dinov2-giant (old Hunyuan lineage) but not dinov3. Needs: accept the licence
   on that model page, create a token, `huggingface-cli login` (or HF_TOKEN).
2. **gcloud ADC for Vertex.** Nano Banana reference generation fails with
   "Reauthentication is needed"; run `gcloud auth application-default login`.

Nine crop references are rostered and ready at `docs/crop_roster.json`
(root/tall/vine x seedling/growing/mature, flat studio background, no ground
plane per the object-generator rules).
