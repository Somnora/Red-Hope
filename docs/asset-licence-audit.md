# Asset-pipeline licence audit — 2026-08-16

`premium-asset-plan.md` §11 flagged this and marked it *"Flagged, not assessed."*
This is the assessment. It grew materially on 2026-08-16, when the pipeline
gained two new dependencies (TRELLIS.2-4B and DINOv3).

**This is fact-gathering, not legal advice.** Every row below is what the
licence text says, with its source. Whether any of it restricts shipping *Red
Hope* commercially is a question for a lawyer, and two rows below genuinely need
one. Nothing here should be treated as a clearance.

## What produced the art that ships today

| Component | Licence | Commercial use | Conditions that bite | Assets affected |
|---|---|---|---|---|
| Gemini 3 Pro Image ("Nano Banana Pro") on Vertex | Google Cloud terms | Yes | Google **asserts no ownership** in Generated Output; output is Customer Data. IP indemnity offered, conditioned on following safety practice | **All** reference art |
| Hunyuan3D 2.1 | `tencent-hunyuan-community` | Yes, **inside the Territory** | Territory **excludes the EU, UK and South Korea**; >1 M MAU requires a separate licence; distribution must carry a notice; **Tencent claims no rights in Outputs** | Most shipped meshes: props, building models, crew walkers, the 8 Hunyuan crops |
| TRELLIS.2-4B | **MIT** | Yes, unrestricted | none | `crop_vine_3`, `workbench_lg`, `workshop`, `infirmary`, `chemtable_lg`, `lab_full` |
| DINOv3 (`dinov3-vitl16`) | DINOv3 License (Meta) | Yes, worldwide, no MAU threshold | **Must prominently display "Built with DINOv3"** | The same six (it is TRELLIS.2's image encoder) |
| ~~nvdiffrast~~ **removed** | NVIDIA Source Code License (1-Way Commercial) | No — non-commercial only | **No longer used.** Replaced by `scripts/gpu/rh_uv_rasterizer.py`; the four assets were re-baked with it absent | none |
| SDXL 1.0 + IP-Adapter | CreativeML Open RAIL++-M | Yes, with behavioural use restrictions | model card states "research purposes only" as intended use | style-lock / older sprite lanes |
| Real-ESRGAN | BSD-3-Clause | Yes | attribution | Hunyuan paint stage |
| rembg (code) | MIT | Yes | the underlying u2net / isnet-general-use **model** licences are unconfirmed | background stripping |
| Blender | GPL | Yes | GPL binds Blender itself, not artwork made with it | finishing lane |

## The two that need a decision

### 1. nvdiffrast is non-commercial, and it is not optional in the TRELLIS.2 path

The NVIDIA Source Code License (1-Way Commercial) grants use "only ... non-
commercially", while reserving commercial use to NVIDIA itself.

It is a **hard dependency of the export path**, not a convenience. Proven by
accident on 2026-08-16: when nvdiffrast failed to import on a clean box,
`o_voxel` — the module whose `postprocess.to_glb` writes the GLB and bakes the
2048 PBR maps — failed to import with it. So every TRELLIS.2 asset shipped today
was produced through it.

#### Tested 2026-08-16: where exactly nvdiffrast is used

Read from source rather than guessed (`o-voxel/o_voxel/postprocess.py`, upstream
`main`). The result is narrower than feared:

- The import is **top-level** (`import nvdiffrast.torch as dr`), which is why
  `o_voxel` dies without it. No parameter of `to_glb` disables it - not
  `remesh`, not `texture_size`.
- But it is called in **exactly one place**, the texture bake, via **two** calls:
  `dr.rasterize(ctx, uvs_rast, faces, resolution=[texture_size]*2)` and
  `dr.interpolate(out_vertices, rast, out_faces)`.
- `o_voxel/rasterize.py` does **not** use it - that is a separate custom CUDA
  voxel renderer (`_C.rasterize_voxels_cuda`).
- Everything else in the export - mesh extraction, decimation, remeshing, UV
  unwrap - is **cumesh**, not nvdiffrast.
- **The model itself does not need it.** Proven from our own bootstrap log: on
  the first clean-box run `trellis2` imported OK while `nvdiffrast.torch` and
  `o_voxel` both failed. Only the export/bake path is affected.

What those two calls actually do is UV-space triangle rasterization plus
barycentric interpolation: `rasterize` returns per-texel barycentrics and a
triangle id, and `interpolate` turns those into a 3D position per texel, which is
then re-projected onto the original mesh via BVH and trilinearly sampled from the
attribute volume. **No gradients flow** (this is a bake, not training) and
`dr.antialias` is never called - so none of nvdiffrast's differentiable or
anti-aliasing machinery is being used. It is being used as a plain rasterizer.

Options, cheapest first:
- **Write a permissive UV-space rasterizer** to replace those two calls: for each
  texel centre, the covering triangle and its barycentrics, then a weighted sum
  of vertex positions. Our own code, so no licence issue. Bounded, but it is a
  real piece of work - roughly 100 lines plus validation against a known-good
  bake, call it a couple of hours, not the 20 minutes first guessed.
- **Ask NVIDIA for commercial terms.**
- **Keep TRELLIS.2 for GEOMETRY and bake textures elsewhere** (Blender is already
  in the lane and is a GPL *tool*, which does not encumber its output).
- **Treat the four TRELLIS.2 assets as non-shippable** and keep the Hunyuan ones.

Note the asymmetry that makes this worth solving rather than avoiding:
TRELLIS.2-4B itself is **MIT**, the most permissive thing in the pipeline. The
restriction is in a rendering utility, not in the model — so it is plausibly
engineerable around rather than a dead end.

#### RESOLVED 2026-08-16: nvdiffrast removed, assets re-baked

`scripts/gpu/rh_uv_rasterizer.py` replaces the two calls with ~120 lines of our
own PyTorch. Proven, not asserted:

- nvdiffrast was **deleted from the box** (`site-packages/nvdiffrast`, the
  `_nvdiffrast_c*.so`, and the dist-info). `import nvdiffrast` then raised
  ModuleNotFoundError, and so did `o_voxel` - the baseline.
- With only the stand-in registered, `o_voxel` imported and **all four assets
  exported**: crop_vine_3, workbench_lg, workshop, infirmary. GLB sizes within
  1% of the originals.
- Rendered side by side, the results are indistinguishable from the nvdiffrast
  bakes, with the trim, readouts and panels in the same places -
  `docs/qa/2026-08-16/permissive-rasterizer-ab.jpg`. No vertical flip, which
  confirms `flip_y=False` by the test that actually matters.
- The four shipped assets have been **re-imported from the nvdiffrast-free
  bakes**, so the repo no longer contains art produced through it.

**Strengthened 2026-08-17.** The proof above had to install nvdiffrast and then
delete it. `bootstrap_trellis2.sh` now takes `RH_NO_NVDIFFRAST=1`, which skips
the install entirely and asserts `import nvdiffrast` raises ModuleNotFoundError
before registering the stand-in. `chemtable_lg` and `lab_full` were baked that
way on a box that never had nvdiffrast on it at any point. Every TRELLIS.2 asset
in the repo is now nvdiffrast-free, and the lane's default path is too.

One honest negative: a pixel diff of the atlases could NOT validate this,
because the pipeline is not reproducible run to run - the same seed produced
7817 vs 7571 triangles for crop_vine_3, so the UV layouts differ and a
pixel-wise comparison is meaningless. The render comparison is the evidence;
the atlas diff is not.

### 2. Hunyuan3D's Territory excludes the EU, UK and South Korea

The licence grants rights "for the Territory only", and the Territory excludes
those markets. Separately and importantly: *"Tencent claims no rights in Outputs
You generate. You and Your users are solely responsible for Outputs."*

So the restriction is on the licence to **use the model**, while Tencent
disclaims rights in what it produces. Whether a game containing Hunyuan-produced
meshes may then be sold into the EU/UK/KR is exactly the question that needs a
lawyer — do not read the output disclaimer as settling it. This matters more
than the usual licence footnote because it names three major games markets.

Generation itself happens in `us-east-1`, which is inside the Territory.

## Cheap compliance, owed regardless

- **"Built with DINOv3"** displayed prominently — website, UI, about page or
  product documentation. Required by the DINOv3 licence. Trivial, and trivially
  forgotten.
- The Tencent notice with any distribution: *"Tencent Hunyuan 3D 2.1 is licensed
  under the Tencent Hunyuan 3D 2.1 Community License Agreement, Copyright ©
  2025 Tencent."*
- Real-ESRGAN BSD attribution.
- Confirm the u2net / isnet-general-use **model** licences (rembg's own code is
  MIT; the weights are a separate question).

None of this exists in the project yet — there is no credits or third-party
notices file.

## Sources

- https://huggingface.co/tencent/Hunyuan3D-2.1 and its `LICENSE`
- https://huggingface.co/microsoft/TRELLIS.2-4B
- https://ai.meta.com/resources/models-and-libraries/dinov3-license/
- https://github.com/NVlabs/nvdiffrast `LICENSE.txt`
- https://cloud.google.com/terms/generative-ai-indemnified-services
- https://huggingface.co/stabilityai/stable-diffusion-xl-base-1.0
- https://github.com/xinntao/Real-ESRGAN , https://github.com/danielgatis/rembg
