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
| TRELLIS.2-4B | **MIT** | Yes, unrestricted | none | `crop_vine_3`, `workbench_lg`, `workshop`, `infirmary` |
| DINOv3 (`dinov3-vitl16`) | DINOv3 License (Meta) | Yes, worldwide, no MAU threshold | **Must prominently display "Built with DINOv3"** | The same four (it is TRELLIS.2's image encoder) |
| **nvdiffrast** | **NVIDIA Source Code License (1-Way Commercial)** | **No — non-commercial only** | *"The Work and any derivative works thereof only may be used or intended for use non-commercially."* | The same four — see below |
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

Options, cheapest first:
- **Check whether o_voxel can export without the rasterizer** (e.g. `remesh=0`,
  or a texture path that does not call it). If a code path avoids it, re-export.
- **Substitute a permissively-licensed rasterizer** for the bake.
- **Ask NVIDIA for commercial terms.**
- **Treat the four TRELLIS.2 assets as non-shippable** and keep the Hunyuan ones.

Note the asymmetry that makes this worth solving rather than avoiding:
TRELLIS.2-4B itself is **MIT**, the most permissive thing in the pipeline. The
restriction is in a rendering utility, not in the model — so it is plausibly
engineerable around rather than a dead end.

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
