# The finishing lane (local Blender)

Gate G0.3 of `docs/premium-asset-plan.md`. Everything here runs **locally** on
Blender 5.2 LTS — no GPU box, no cost. Generation (Nano Banana sheets, Hunyuan3D
shape) still needs the A100; finishing does not.

```bash
BL=/opt/homebrew/bin/blender
$BL --background --python scripts/blender/rh_finish.py -- \
    --in  <src.glb> \
    --out <dst.glb> \
    --report <report.json> \
    --tier S            # H | S | B, sets the triangle budget
    [--uv]              # re-unwrap (see the warning below)
    [--no-decimate]
```

Stages run in order: **weld → smooth-by-angle → decimate-to-tier → (UV) →
ground-centre pivot → export**, and every stage narrates itself so the terminal
output *is* the receipt. `--report` additionally writes before/after metrics as
JSON (tris, verts, UV utilisation, texel density, dimensions).

## What this fixed, measurably

The 2026-07-17 painted batch shipped **completely unwelded** — 53,894 vertices
for 18,000 triangles, i.e. 2.99 verts/tri, every triangle carrying its own three
vertices with nothing shared. That forces flat shading across the whole model no
matter what the normals say, which is a large part of why the models read as
faceted and unfinished. Welding merges ~83 % of vertices on every asset in the
batch **without touching a single triangle or UV**:

| | shipped | finished |
|---|---|---|
| verts in Blender | 53,894 | 9,002 |
| verts as exported (glTF re-splits at sharp edges/seams) | 53,894 | 13,760 |
| tris | 18,000 | 18,000 |
| UV utilisation | 0.544 | 0.544 (unchanged) |

Proof render: `Martians/gen/weld_smooth_proof_20260814.png`.

## Two rules this kit encodes

**Never `shade_auto_smooth`.** It adds a modifier whose custom normals the glTF
exporter silently drops, so the mesh arrives in Unreal exactly as faceted as it
started. `smooth_by_angle()` sets `polygon.use_smooth` directly and marks sharp
edges by face angle, which survives export.

**Do not re-unwrap an asset that already has good UVs.** Measured on
HabitatDome: the Hunyuan-authored `UVMap` uses 54.4 % of UV space; a
`smart_project` re-unwrap dropped that to 13.8 % *and* broke correspondence with
the existing baked texture. `--uv` is therefore **opt-in**, and is only correct
when every map is going to be re-baked onto the new layout (the C4 stage). For
everything else, weld+smooth preserves the original UVs byte-for-byte — verified.

## Round-trip safety

Embedded image and material names survive the Blender round-trip unchanged
(`HeavyForge_textured` in → `HeavyForge_textured` out), so re-importing over an
existing asset keeps every texture object path — and therefore every path
hardcoded in `RHArtWireCommandlet.cpp` — valid.

## Re-import

Editor closed, one file per invocation, judged on the completion line rather than
the exit code (a benign port-8000 HttpListener bind makes exit codes always
non-zero in this project):

```bash
UnrealEditor-Cmd <proj> -run=ImportAssets -source=<finished.glb> \
  -dest="/Game/RedHope/Art/Models2/<Name>" -nosourcecontrol -replaceexisting \
  -unattended -nosound -stdout
# success == "Interchange import completed" in the log
```

Interchange produces a **double-folder** layout when `-dest` already ends with
the asset name: `Models2/<N>/<N>/StaticMeshes/<N>`. That is expected, and the
paths wired in the visualizer and in `RHArtWire` match it.

**Order is load-bearing: import first, wire second.** A re-import with
`-replaceexisting` resets the mesh's material slot back to the auto-generated
Interchange material, silently undoing the `MI_<name>` assignment. So any time an
asset is re-cut through this lane, re-run the wiring pass afterwards:

```bash
UnrealEditor-Cmd <proj> -run=RHArtWire -unattended -nosound -stdout   # -dryrun to preview
```

Also keep stray files out of the finished directory before a batch import — the
import loop globs `finished/*.glb`, so a leftover experiment lands in the project
as its own asset (three did, and were removed by hand).

## Not yet built (C4/C5, P1)

`bake_transfer`, `bake_projection` (camera-projection albedo from the 4K Nano
Banana sheets — all 13 objects have 5 views at 4096², all 12 redo characters have
8), `bake_ao`, `bake_curvature`, `compose_textures` (wear + grime + accent mask +
MRA packing). Cycles CPU baking is **proven working headless** on this machine —
an EMIT bake to a 256² target returned a correct non-empty image — so the
remaining work is authoring, not feasibility.
