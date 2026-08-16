"""A permissively-licensed stand-in for the only two nvdiffrast calls TRELLIS.2
makes, so the mesh export path carries no non-commercial dependency.

WHY
---
`o_voxel.postprocess` does `import nvdiffrast.torch as dr` at top level, and
nvdiffrast is under the NVIDIA Source Code License (1-Way Commercial): "The Work
and any derivative works thereof only may be used or intended for use
non-commercially." That makes every GLB exported through `to_glb` a commercial
risk - see docs/asset-licence-audit.md.

Read from upstream source, it uses nvdiffrast in exactly one place (the texture
bake) through exactly two calls:

    rast, _ = dr.rasterize(ctx, uvs_rast, faces, resolution=[S, S])
    pos     = dr.interpolate(out_vertices.unsqueeze(0), rast, out_faces)[0][0]

No gradients flow (this is a bake, not training) and `dr.antialias` is never
called, so none of nvdiffrast's differentiable or anti-aliasing machinery is in
use. It is doing the job of a plain UV-space triangle rasterizer, which is what
this file is. TRELLIS.2-4B itself is MIT, so replacing this one utility makes the
whole lane permissive.

THE CONTRACT BEING MATCHED
--------------------------
`rasterize` returns [1, H, W, 4] where, per nvdiffrast's convention:
    [..., 0] = u, the barycentric weight of the triangle's SECOND vertex
    [..., 1] = v, the weight of the THIRD vertex (the first is 1 - u - v)
    [..., 2] = z (always 0 here - the UV pass feeds z=0)
    [..., 3] = triangle index + 1, with 0 meaning "no coverage"

Overlap: the real rasterizer resolves by depth. Every z here is 0, so ties are
arbitrary in both implementations; this one lets the higher triangle index win,
which matches the chunked `torch.where` overwrite order upstream.

ORIENTATION: DERIVED, NOT GUESSED - AND STILL WORTH CONFIRMING
--------------------------------------------------------------
Whether row 0 is NDC y=-1 or y=+1 is a convention, and getting it backwards
flips every baked texture vertically - a failure that looks like a plausible
texture rather than an error, so it must not be assumed.

`rh_validate_bake.py` tries to settle it from the shipped assets and reports
UNDECIDED, correctly: TRELLIS.2's atlases come out ~100% written (dilated), so
there are no empty gutters to compare a coverage mask against, and both
orientations score identically. It says so instead of picking.

The default below (`flip_y=False`, i.e. row 0 = NDC y=-1) is therefore derived
from evidence rather than chosen:

  1. The shipped TRELLIS.2 GLBs render CORRECTLY - Blender puts the white
     worktop, yellow trim and dark legs where the reference has them. So the
     atlas rows, as the real nvdiffrast wrote them, agree with how glTF samples.
  2. glTF samples with v measured from the TOP: v=0 is the first row.
  3. The caller maps UV into clip space as (v*2 - 1), so v=0 is NDC y=-1.
  4. Therefore nvdiffrast's row 0 holds NDC y=-1 - which is what this file does
     with flip_y=False. It also matches nvdiffrast's documented OpenGL
     bottom-origin convention, arriving from the other direction.

That is a derivation from an observation, not a proof. The cheap confirmation is
to re-bake one asset on a box with this module installed and diff the resulting
atlas against the shipped one; a vertical flip would be unmissable. Do that
before trusting the lane with anything that ships.

Self-test (no GPU needed):  python3 rh_uv_rasterizer.py
"""
from typing import Optional, Tuple

import torch

__all__ = ["rasterize", "interpolate", "RasterizeCudaContext", "install"]


class RasterizeCudaContext:  # noqa: N801 - name mirrors the API being replaced
    """Accepted and ignored. The upstream call site constructs one and passes it
    straight to rasterize; there is no state worth carrying."""

    def __init__(self, *args, **kwargs) -> None:
        pass


def rasterize(
    ctx,
    clip: torch.Tensor,
    tris: torch.Tensor,
    resolution,
    *,
    flip_y: bool = False,
    chunk: int = 4096,
    eps: float = 0.0,
) -> Tuple[torch.Tensor, Optional[torch.Tensor]]:
    """Rasterise `tris` over a `resolution` grid from clip-space `clip`.

    clip: [1, V, 4]  (x, y, z, w) - the caller builds (u*2-1, v*2-1, 0, 1)
    tris: [F, 3] integer vertex indices
    """
    H, W = int(resolution[0]), int(resolution[1])
    dev = clip.device
    tris = tris.long()

    ndc = clip[0, :, :2] / clip[0, :, 3:4]
    # NDC -> continuous pixel space. Texel centres sit at integer + 0.5.
    px = (ndc[:, 0] * 0.5 + 0.5) * W
    py = (ndc[:, 1] * 0.5 + 0.5) * H
    if flip_y:
        py = H - py

    ax, ay = px[tris[:, 0]], py[tris[:, 0]]
    bx, by = px[tris[:, 1]], py[tris[:, 1]]
    cx, cy = px[tris[:, 2]], py[tris[:, 2]]

    area = (bx - ax) * (cy - ay) - (cx - ax) * (by - ay)
    live = area.abs() > 1e-12  # drop degenerate triangles rather than divide by ~0

    x0 = torch.clamp(torch.floor(torch.minimum(torch.minimum(ax, bx), cx) - 0.5), 0, W - 1).long()
    x1 = torch.clamp(torch.ceil(torch.maximum(torch.maximum(ax, bx), cx) + 0.5), 0, W - 1).long()
    y0 = torch.clamp(torch.floor(torch.minimum(torch.minimum(ay, by), cy) - 0.5), 0, H - 1).long()
    y1 = torch.clamp(torch.ceil(torch.maximum(torch.maximum(ay, by), cy) + 0.5), 0, H - 1).long()

    bw = (x1 - x0 + 1).clamp(min=0)
    bh = (y1 - y0 + 1).clamp(min=0)
    live &= (bw > 0) & (bh > 0)

    out = torch.zeros((1, H, W, 4), dtype=torch.float32, device=dev)
    idx_all = torch.nonzero(live, as_tuple=False).flatten()

    # Work in chunks of triangles: the flattened candidate-texel list is the only
    # thing here that can get large, and this bounds it.
    for s in range(0, idx_all.numel(), chunk):
        t = idx_all[s:s + chunk]
        if t.numel() == 0:
            continue
        w_, h_ = bw[t], bh[t]
        n = w_ * h_                      # candidate texels per triangle
        total = int(n.sum().item())
        if total == 0:
            continue

        # Expand each triangle's bounding box into a flat texel list.
        rep = torch.repeat_interleave(torch.arange(t.numel(), device=dev), n)
        starts = torch.cumsum(n, 0) - n
        off = torch.arange(total, device=dev) - starts[rep]
        gx = x0[t][rep] + (off % w_[rep])
        gy = y0[t][rep] + torch.div(off, w_[rep], rounding_mode="floor")

        # Barycentric weights at texel centres, normalised so they sum to 1.
        # Dividing by the signed area makes the inside test winding-agnostic.
        cxp, cyp = gx.float() + 0.5, gy.float() + 0.5
        tt = t[rep]
        inv = 1.0 / area[tt]
        w0 = ((bx[tt] - cxp) * (cy[tt] - cyp) - (cx[tt] - cxp) * (by[tt] - cyp)) * inv
        w1 = ((cx[tt] - cxp) * (ay[tt] - cyp) - (ax[tt] - cxp) * (cy[tt] - cyp)) * inv
        w2 = 1.0 - w0 - w1

        hit = (w0 >= -eps) & (w1 >= -eps) & (w2 >= -eps)
        if not bool(hit.any()):
            continue

        flat = (gy[hit] * W + gx[hit])
        tri_hit = tt[hit]
        vals = torch.stack([
            w1[hit],                      # u  -> weight of the 2nd vertex
            w2[hit],                      # v  -> weight of the 3rd vertex
            torch.zeros_like(w1[hit]),    # z  -> the UV pass is planar
            tri_hit.float() + 1.0,        # triangle id + 1
        ], dim=-1)

        # Where two triangles cover a texel, pick a winner DETERMINISTICALLY.
        # `view[flat] = vals` looks like it would work and does not: PyTorch
        # leaves duplicate-index assignment unspecified, so the winner varies
        # run to run. Real UV charts are disjoint so this would almost never
        # fire in production - which is exactly why it would have been a
        # miserable bug to find later. Sort by (texel, triangle) and keep the
        # last row per texel, giving highest-index-wins and, more importantly,
        # leaving no duplicate indices in the write itself.
        key = flat * (int(tris.shape[0]) + 1) + tri_hit
        order = torch.argsort(key)
        flat_s, vals_s = flat[order], vals[order]
        last = torch.ones_like(flat_s, dtype=torch.bool)
        last[:-1] = flat_s[1:] != flat_s[:-1]

        out.view(-1, 4)[flat_s[last]] = vals_s[last]

    return out, None


def interpolate(attr: torch.Tensor, rast: torch.Tensor, tris: torch.Tensor):
    """attr: [1, V, C] -> [1, H, W, C], zero where the raster is empty."""
    tris = tris.long()
    tid = rast[..., 3].long() - 1
    covered = tid >= 0
    tid = tid.clamp(min=0)

    u = rast[..., 0]
    v = rast[..., 1]
    w0 = 1.0 - u - v

    idx = tris[tid]                        # [1, H, W, 3]
    a = attr[0][idx[..., 0]]
    b = attr[0][idx[..., 1]]
    c = attr[0][idx[..., 2]]
    out = a * w0[..., None] + b * u[..., None] + c * v[..., None]
    return out * covered[..., None].to(out.dtype), None


def install(flip_y: bool = False) -> None:
    """Make `import nvdiffrast.torch as dr` resolve to this module.

    Call BEFORE importing o_voxel, since its import is top-level. Registering a
    stub package is deliberate: patching after the fact would leave the real
    module already bound inside postprocess's namespace.
    """
    import sys
    import types

    pkg = types.ModuleType("nvdiffrast")
    sub = types.ModuleType("nvdiffrast.torch")
    sub.RasterizeCudaContext = RasterizeCudaContext
    sub.RasterizeGLContext = RasterizeCudaContext
    sub.rasterize = lambda ctx, clip, tris, resolution, **kw: rasterize(
        ctx, clip, tris, resolution, flip_y=flip_y)
    sub.interpolate = interpolate
    pkg.torch = sub
    sys.modules["nvdiffrast"] = pkg
    sys.modules["nvdiffrast.torch"] = sub


# --------------------------------------------------------------------------
# Self-test: brute force over every texel is the reference. Slow by design and
# obviously correct, which is the point of a reference.
# --------------------------------------------------------------------------
def _reference(clip, tris, S, flip_y=False):
    ndc = clip[0, :, :2] / clip[0, :, 3:4]
    px = (ndc[:, 0] * 0.5 + 0.5) * S
    py = (ndc[:, 1] * 0.5 + 0.5) * S
    if flip_y:
        py = S - py
    out = torch.zeros((1, S, S, 4))
    for ti in range(tris.shape[0]):
        i, j, k = tris[ti].tolist()
        axx, ayy = px[i].item(), py[i].item()
        bxx, byy = px[j].item(), py[j].item()
        cxx, cyy = px[k].item(), py[k].item()
        ar = (bxx - axx) * (cyy - ayy) - (cxx - axx) * (byy - ayy)
        if abs(ar) < 1e-12:
            continue
        for yy in range(S):
            for xx in range(S):
                Px, Py = xx + 0.5, yy + 0.5
                a0 = ((bxx - Px) * (cyy - Py) - (cxx - Px) * (byy - Py)) / ar
                a1 = ((cxx - Px) * (ayy - Py) - (axx - Px) * (cyy - Py)) / ar
                a2 = 1.0 - a0 - a1
                if a0 >= 0 and a1 >= 0 and a2 >= 0:
                    out[0, yy, xx] = torch.tensor([a1, a2, 0.0, ti + 1.0])
    return out


def _selftest():
    torch.manual_seed(7)
    S = 64
    ok = True

    for trial in range(4):
        V = 9
        uv = torch.rand(V, 2)
        clip = torch.cat([uv * 2 - 1, torch.zeros(V, 1), torch.ones(V, 1)], dim=-1).unsqueeze(0)
        tris = torch.tensor([[0, 1, 2], [3, 4, 5], [6, 7, 8]], dtype=torch.int32)

        mine, _ = rasterize(None, clip, tris, [S, S])
        ref = _reference(clip, tris, S)

        same_id = (mine[..., 3] == ref[..., 3])
        cov_m = int((mine[..., 3] > 0).sum())
        cov_r = int((ref[..., 3] > 0).sum())
        agree = float(same_id.float().mean())

        both = (mine[..., 3] > 0) & (ref[..., 3] > 0) & same_id
        bary_err = float((mine[..., :2][both] - ref[..., :2][both]).abs().max()) if bool(both.any()) else 0.0

        good = agree > 0.999 and bary_err < 1e-4
        ok &= good
        print("  trial %d: coverage mine=%5d ref=%5d | id agreement %.4f | max bary err %.2e  %s"
              % (trial, cov_m, cov_r, agree, bary_err, "OK" if good else "FAIL"))

    # interpolate must reproduce the positions a barycentric blend implies
    V = 3
    uv = torch.tensor([[0.1, 0.1], [0.9, 0.15], [0.5, 0.9]])
    clip = torch.cat([uv * 2 - 1, torch.zeros(V, 1), torch.ones(V, 1)], dim=-1).unsqueeze(0)
    tris = torch.tensor([[0, 1, 2]], dtype=torch.int32)
    rast, _ = rasterize(None, clip, tris, [S, S])
    attr = torch.tensor([[[1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]]])
    interp, _ = interpolate(attr, rast, tris)
    cov = rast[..., 3] > 0
    sums = interp[cov].sum(-1)
    unit = bool(torch.allclose(sums, torch.ones_like(sums), atol=1e-5))
    print("  interpolate: %d covered texels, barycentric weights sum to 1: %s" % (int(cov.sum()), unit))
    ok &= unit

    print("SELFTEST", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(_selftest())
