# Polishing the models and materials yourself

_Written 2026-07-15. How to take a pipeline GLB (or one already in-game) from
"fine at strategy distance" to "clean," and how to tune its Unreal material.
Split into two halves: **mesh** (Blender, on the GPU box or any machine with
Blender) and **material** (Unreal, `M_ModelTex` instances)._

---

## Part A — what the pipeline meshes actually look like

Before polishing anything, know the raw material. A QA audit of the current
game GLBs (headless Blender, `scripts/mesh_qa.py`) shows the Hunyuan3D →
decimate signature clearly. Representative row — `battery_game.glb`:

| metric | value | what it means |
|---|---|---|
| tris | 17,998 | on the 18k budget — good |
| objects | 1 | single joined mesh — good |
| textures | 2× 2048² | base-colour + packed metallic/roughness — good |
| **islands** | **17,956** | nearly one island **per triangle** |
| **tiny_islands** | **17,956** | every face is a "loose" shell |
| **non-manifold edges** | **53,910** | no shared edges anywhere |
| **flat_faces_pct** | **94.6 %** | almost entirely flat-shaded |
| origin_xy_offset | (0.0, −0.76) | pivot not centered on Y |
| min_z | −0.77 | **not grounded** — sits below Z=0 |

The islands/non-manifold/flat numbers are all the **same underlying fact**: the
mesh is exported **fully split** — every triangle has its own copies of its
three vertices, welded to nothing. That is why it renders faceted (no shared
normals to smooth across) and why the file is heavier than the triangle count
warrants. It is not broken — it renders fine — but it is exactly what a polish
pass fixes.

The two things worth fixing on almost every model:
1. **Weld + smooth** — merge the duplicate verts and shade smooth by angle, so
   curved surfaces catch light smoothly while hard edges stay crisp.
2. **Ground + center** — drop the lowest point to Z=0 and center the pivot on
   XY, so it seats correctly and its label doesn't drift in UE.

---

## Part B — the mesh polish (Blender, headless)

`scripts/rh_polish.py` (in the pipeline scripts dir) does the weld + auto-smooth
and renders a **before/after preview pair** from the same camera so you can judge
each model. On the GPU box:

```bash
source ~/.config/rh3d/host.env
SSH="ssh -i $RH3D_SSH_KEY $RH3D_USER@$RH3D_HOST"
B=$RH3D_NS/tools/blender-4.2.22-linux-x64/blender   # or $RH3D_NS/bin/blender

$SSH "$B -b --python $RH3D_NS/scripts/rh_polish.py -- \
   $RH3D_NS/io/batch_out/battery_game.glb $RH3D_NS/io/polish"
# outputs: io/polish/polished/battery_polished.glb
#          io/polish/previews/battery_before.png + battery_after.png
```

What it does, and why each step is safe:
- **`remove_doubles(threshold=1e-6)`** — welds only *exact* duplicate verts (the
  split-face copies). UVs live on face-corners (loops), so the texture survives
  the weld untouched. It does **not** merge distinct verts that happen to be
  close, so it can't collapse detail.
- **`shade_auto_smooth(angle=30°)`** — marks edges sharper than 30° as hard,
  smooths the rest. Curved hulls (crew, robots, tanks) stop looking faceted;
  hard-surface machines keep their crisp corners. 30° is conservative — raise
  toward 40–60° for very rounded organic shapes, lower toward 20° for boxy
  props.
- Re-export GLB, geometry and textures otherwise identical.

> **Grounding/centering** is done by `scripts/mesh_cleanup.py` (drops feet to
> Z=0, recenters XY). If a model's QA shows a nonzero `min_z` or `origin_xy_
> offset`, re-run it through cleanup: `blender -b --python mesh_cleanup.py -- in.glb out.glb 18000`.

### Deciding per-model
Look at the before/after tiles. Auto-smooth is a clear win on anything curved
and neutral-to-positive on hard-surface. If a specific model looks *worse* after
(smoothing bled across an intended hard edge), lower the angle for that one:
edit `math.radians(30)` in `rh_polish.py`, or in the Blender GUI use
Object → Shade Auto Smooth and drag the angle live.

### If you want to polish by hand in the Blender GUI
1. `File → Import → glTF 2.0`, pick the `_game.glb`.
2. `Edit Mode` (Tab) → `A` (select all) → `M → By Distance` (weld, default
   0.0001 m is fine).
3. `Object Mode` → right-click → `Shade Auto Smooth`, set the angle.
4. Check normals: overlay → Face Orientation; blue = outward (good), red =
   inverted → select those faces and `Mesh → Normals → Recalculate Outside`.
5. `Object → Set Origin → Origin to Geometry` (or to the base for grounding),
   then move the object so its lowest vert sits at Z=0.
6. `File → Export → glTF 2.0`, format **GLB**, +Y up, "Apply Modifiers" on.

---

## Part C — the Unreal material (`M_ModelTex`)

Every imported model wears an `MI_<name>` instance of the `M_ModelTex` master.
The master has three exposed parameters worth knowing:

| param | default | what it does | when to touch |
|---|---|---|---|
| **BaseTex** | the model's base-colour map | albedo | only if you re-baked a texture |
| **Rough** | the packed roughness map | surface finish | swap/scale for shinier or more matte |
| **EmissiveFloor** | 0.08 × base colour | self-glow in shadow | raise if a model crushes to black |

### Why the emissive floor exists (don't just delete it)
Movable building and robot actors take almost no skylight in this scene — only
direct sun. Their unlit faces crush to pure black in shadow (the same reason the
old primitive robots were given a self-glow). `EmissiveFloor = 0.08 × BaseColor`
keeps imported models legible in shadow, and keeps them visible when the
director's lighting features are toggled off. If a model reads too flat/glowy,
lower the multiplier toward 0.04; if it disappears into shadow, raise toward
0.12. It is a legibility crutch, not art — tune, don't remove.

### Tuning a material instance
In the editor: open `MI_<name>` under `/Game/RedHope/Art/Models/<name>/`, tick
the parameter you want, and drag. No recompile — instances are live.

Headlessly (the code-authored path, no editor): material instances are set up in
`RHArt` / the import wiring; change the param default there and re-run the
commandlet. For a one-off you almost always want the editor slider.

### Roughness / metallic
The paint stage produces a **packed** map (metallic + roughness in separate
channels). If a metal reads too plasticky, the fix is usually roughness, not
base colour — scale `Rough` down for a sharper specular, up for a matte finish.
Genuinely wrong metallic (a cloth robot reading chrome) means the paint stage
mis-classified it; re-paint with a cleaner reference rather than fighting it in
UE.

### The two special-case materials (don't clobber them)
- **Forge** is the old vertex-colour mesh (baked `COLOR_0`, no textures) and is
  forced onto `M_VertexColor` in `HandleBuildingAdded`. Leave that branch alone.
- Terrain/pit surfaces use `M_MarsSurface` (triplanar, UV-less) — a different
  material entirely, tuned by the `RH.Grade.*` CVars and RHArt, not `M_ModelTex`.

### The ISM-usage trap (bites in packaged/`-game` runs)
Any material that will ride instanced static meshes (robots, scatter, pit walls)
**must** have `bUsedWithInstancedStaticMeshes` set and be resaved. The editor
auto-adds the flag (masking the bug), but a `-game` run swaps in the gray
default material on every ISM with only a log warning. `M_Graybox` and
`M_MarsSurface` already have it; set it on any new material you author for ISMs.

---

## Part D — the fast loop

For one model, cold box:
```bash
# 1. box up + Blender libs (see asset-pipeline-guide.md §1)
# 2. polish + preview
$SSH "$B -b --python $RH3D_NS/scripts/rh_polish.py -- <in.glb> $RH3D_NS/io/polish"
scp -i $RH3D_SSH_KEY $RH3D_USER@$RH3D_HOST:$RH3D_NS/io/polish/previews/'*_after.png' ~/Desktop/
# 3. eyeball before/after; if good, pull the polished GLB
scp -i $RH3D_SSH_KEY $RH3D_USER@$RH3D_HOST:$RH3D_NS/io/polish/polished/<name>_polished.glb \
    ~/Desktop/Martians/assets/models/
# 4. re-import into UE (asset-pipeline-guide.md §7), tune EmissiveFloor if needed
```

The re-import and the look verdict are yours to eyeball — headless can render a
preview, but "does it read right in the game" is a hand-play call.
