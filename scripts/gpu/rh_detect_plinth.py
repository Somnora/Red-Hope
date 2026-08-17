"""Detect a baked-in ground plinth at the bottom of a mesh.

  uv run --with trimesh --with numpy python scripts/gpu/rh_detect_plinth.py <glb...>

WHY
---
Director, 2026-08-17: "at the bottom of some of the objects there is like a white
platform below it that should be transparent - a bit of the sprite is leaking
into the 3D object that's not supposed to be there."

That is the reference image's GROUND being reconstructed as geometry. Every
image-to-3D reference is a product shot, and if the shot has a floor, a shadow
or a base plate, the model can build it as a slab under the object. The
nano-banana negative prompt has fought this for a while ("absolutely NO ground,
no floor, no base plate, no pedestal, no platform, no terrain, no rug, no cast
shadow, nothing underneath the object") - which is evidence the failure is real
and recurring, not that it is solved.

HOW IT IS MEASURED
------------------
A plinth is a slab: near the very bottom of the mesh the horizontal cross
section is much WIDER than the object's typical cross section, and it is flat.
So for each of N horizontal slabs up the Z axis, take the area of the convex
hull of the vertices in that slab, and compare the bottom slab against the
median. A pedestal shows as a bottom-heavy area ratio.

Reported alongside two corroborating signals, because one number invites the
same mistake the "white-splotch" metric made on the crew - where the ruler
turned out to measure white clothing:

  * bottom_frac  - fraction of ALL vertices sitting in the bottom 4% of height.
                   A slab is dense in Z.
  * flatness     - how planar the bottom slab is (max |z - mean z| / height).
  * area_ratio   - bottom slab hull area / median slab hull area.

None of these alone is a verdict. A wide flat BASE is legitimate on a tank or a
crate. The signature of a defect is a bottom slab that is wide, very flat, and
DISCONNECTED in silhouette from the object above it - so the score is reported,
never auto-applied, and a render is the confirming instrument.
"""
import sys

import numpy as np
import trimesh


def hull_area(pts_xy):
    if len(pts_xy) < 3:
        return 0.0
    try:
        from scipy.spatial import ConvexHull
        return float(ConvexHull(pts_xy).volume)  # 2-D hull "volume" is area
    except Exception:
        # Fallback: bounding-box area. Coarser but monotone in the same way.
        mn, mx = pts_xy.min(axis=0), pts_xy.max(axis=0)
        return float((mx[0] - mn[0]) * (mx[1] - mn[1]))


def analyse(path, slabs=24):
    m = trimesh.load(path, force="mesh", process=False)
    v = np.asarray(m.vertices, dtype=np.float64)
    zmin, zmax = v[:, 2].min(), v[:, 2].max()
    h = zmax - zmin
    if h <= 0:
        return None

    edges = np.linspace(zmin, zmax, slabs + 1)
    areas = []
    for i in range(slabs):
        sel = v[(v[:, 2] >= edges[i]) & (v[:, 2] <= edges[i + 1])]
        areas.append(hull_area(sel[:, :2]) if len(sel) else 0.0)
    areas = np.array(areas)
    med = np.median(areas[areas > 0]) if (areas > 0).any() else 0.0

    bottom_cut = zmin + 0.04 * h
    bottom = v[v[:, 2] <= bottom_cut]
    bottom_frac = len(bottom) / len(v)
    flat = (np.abs(bottom[:, 2] - bottom[:, 2].mean()).max() / h) if len(bottom) else 1.0
    area_ratio = (areas[0] / med) if med > 0 else 0.0

    # Suspicion, not verdict. Wide + flat + Z-dense all at once.
    score = 0
    if area_ratio > 1.25:
        score += 1
    if bottom_frac > 0.06:
        score += 1
    if flat < 0.012:
        score += 1
    return dict(area_ratio=area_ratio, bottom_frac=bottom_frac, flat=flat,
                score=score, height=h)


def main(paths):
    print("%-26s %10s %11s %9s %6s  %s"
          % ("asset", "area_ratio", "bottom_frac", "flatness", "score", "suspicion"))
    rows = []
    for p in paths:
        try:
            r = analyse(p)
        except Exception as exc:
            print("%-26s  <failed: %s>" % (p.rsplit("/", 1)[-1][:26], str(exc)[:40]))
            continue
        if not r:
            continue
        name = p.rsplit("/", 1)[-1].replace(".glb", "")
        label = {3: "PLINTH LIKELY", 2: "suspect", 1: "-", 0: "-"}[r["score"]]
        rows.append((r["score"], r["area_ratio"], name, r, label))
    for score, _, name, r, label in sorted(rows, key=lambda x: (-x[0], -x[1])):
        print("%-26s %10.2f %11.3f %9.4f %6d  %s"
              % (name[:26], r["area_ratio"], r["bottom_frac"], r["flat"], score, label))
    n3 = sum(1 for r in rows if r[0] == 3)
    n2 = sum(1 for r in rows if r[0] == 2)
    print("\n%d likely, %d suspect, %d clean of %d" % (n3, n2, len(rows) - n3 - n2, len(rows)))


if __name__ == "__main__":
    main(sys.argv[1:])
