# Model polish pass — results (2026-07-15)

A weld + smooth-shade polish ran over all **39 pipeline game GLBs** on the A100
(headless Blender 4.2.22). The polished meshes live on the persistent NFS at
`$RH3D_NS/io/polish/polished/<name>_polished.glb`; the original `_game.glb`
files are untouched.

## What the pass did
1. **Welded exact-duplicate vertices** — the pipeline exports meshes fully split
   (every triangle carries its own vertex copies). Merging the exact duplicates
   cleaned the topology without changing the silhouette or losing a single
   triangle or UV.
2. **Applied real smooth shading** — set `polygon.use_smooth` directly so the
   normals actually survive the glTF export (Blender 4.2's `shade_auto_smooth`
   operator adds a modifier whose normals the exporter drops — measured and
   confirmed, so the script sets the flag directly instead).

## Verified numbers
- **39 / 39 models polished, zero errors.**
- **Vertices: 2,101,928 → 347,796 (83% welded).** Every triangle preserved
  (~18k each), all UVs/textures/materials intact.
- **Smoothing confirmed by normal-deviation measurement** (not the misleading
  glTF `use_smooth` round-trip flag): representative models read **89–96 % of
  surface loops smoothed**, mean angular deviation 13–26° — i.e. curved hulls
  now catch light smoothly instead of faceting. Before the fix the same measure
  read < 1 %.

| model | verts before → after | smoothed |
|---|---|---|
| humanoid | 53,873 → 9,006 | 89.0 % |
| battery | 53,910 → 8,812 | 95.1 % |
| lander | 53,887 → 8,898 | 94.2 % |
| solar | 53,888 → 8,788 | 89.6 % |
| prop_bunk | 53,905 → 8,856 | 95.9 % |

(Full per-model table is in the run log; every row welded ~83 %.)

## How to pull them
```bash
source ~/.config/rh3d/host.env
# one model:
scp -i $RH3D_SSH_KEY $RH3D_USER@$RH3D_HOST:$RH3D_NS/io/polish/polished/humanoid_polished.glb \
    ~/Desktop/Martians/assets/models/
# or the whole set (271 MB):
scp -i $RH3D_SSH_KEY $RH3D_USER@$RH3D_HOST:'$RH3D_NS/io/polish/polished/*.glb' \
    ~/Desktop/Martians/assets/models/polished/
```

## Caveats / decisions
- **Full smoothing** was the right call for these organic image-to-3D
  reconstructions. A genuinely hard-surface model (a clean box/CAD piece) would
  instead want a sharp-edge pass (mark edges > ~60° sharp, keep the rest
  smooth). The script (`scripts/rh_polish_geo.py`) is where you'd add that.
- **These are not yet re-imported into the game.** Re-importing is the same
  headless `ImportAssets` loop as the originals (see `asset-pipeline-guide.md`
  §7), and the visual "does it read better in-game" verdict is a hand-play call.
  Recommendation: re-import a couple (humanoid, a building) first, eyeball them
  against the originals in a live boot, then batch-swap if they read better.
- glTF re-splits vertices at normal discontinuities on export, so the polished
  files' on-disk vertex count isn't 83 % smaller — the win is clean topology
  (UE re-welds on import) plus the smooth shading. File sizes are comparable.
