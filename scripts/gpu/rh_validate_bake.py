"""Decide the raster orientation EMPIRICALLY, against a shipped asset.

  uv run --with torch --with numpy --with pillow \
      python scripts/gpu/rh_validate_bake.py <asset.glb> [...]

Whether raster row 0 is NDC y=-1 or y=+1 is a convention. Guess it backwards and
every baked texture comes out vertically flipped - which does not look like an
error, it looks like a texture. So this does not guess.

The test needs no model and no GPU. A shipped TRELLIS.2 GLB already contains both
halves of the answer: the UV layout, and the atlas that was baked through the
real nvdiffrast. Rasterising those UVs with rh_uv_rasterizer must cover the same
texels the real bake wrote to. Score each orientation by how well our coverage
mask agrees with the atlas's own written-vs-empty mask, and take the winner.

A clear winner means the orientation is settled. If both score alike the atlas is
too full to discriminate, and the honest answer is that this test cannot decide -
in which case say so rather than pick one.
"""
import io
import json
import os
import struct
import sys

import numpy as np
import torch
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rh_uv_rasterizer as R


def load_glb(path):
    b = open(path, "rb").read()
    _, _, ln = struct.unpack("<III", b[:12])
    off, chunks = 12, []
    while off < ln:
        cl, _ = struct.unpack("<II", b[off:off + 8])
        chunks.append(b[off + 8:off + 8 + cl])
        off += 8 + cl
    j = json.loads(chunks[0].decode("utf-8"))
    bin_ = chunks[1]

    def acc(i):
        a = j["accessors"][i]
        bv = j["bufferViews"][a["bufferView"]]
        start = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
        ctype = {5121: np.uint8, 5123: np.uint16, 5125: np.uint32, 5126: np.float32}[a["componentType"]]
        ncomp = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[a["type"]]
        arr = np.frombuffer(bin_, dtype=ctype, count=a["count"] * ncomp, offset=start)
        return arr.reshape(a["count"], ncomp) if ncomp > 1 else arr

    prim = j["meshes"][0]["primitives"][0]
    uv = acc(prim["attributes"]["TEXCOORD_0"]).astype(np.float32)
    faces = acc(prim["indices"]).astype(np.int64).reshape(-1, 3)

    pbr = j["materials"][prim["material"]]["pbrMetallicRoughness"]
    img = j["images"][j["textures"][pbr["baseColorTexture"]["index"]]["source"]]
    bv = j["bufferViews"][img["bufferView"]]
    s = bv.get("byteOffset", 0)
    tex = Image.open(io.BytesIO(bin_[s:s + bv["byteLength"]])).convert("RGB")
    return uv, faces, tex


def score(uv, faces, S, flip_y, written):
    clip = torch.cat([
        torch.from_numpy(uv) * 2 - 1,
        torch.zeros(len(uv), 1),
        torch.ones(len(uv), 1),
    ], dim=-1).unsqueeze(0)
    rast, _ = R.rasterize(None, clip, torch.from_numpy(faces), [S, S], flip_y=flip_y)
    cov = (rast[0, ..., 3] > 0).numpy()
    inter = np.logical_and(cov, written).sum()
    union = np.logical_or(cov, written).sum()
    return (inter / union if union else 0.0), cov.mean()


def main(paths):
    verdicts = []
    for p in paths:
        uv, faces, tex = load_glb(p)
        S = tex.size[0]
        a = np.asarray(tex).astype(np.int16)
        # "written" = anything the bake actually put down. Empty atlas gutters
        # come out near-black; a dilated atlas will not discriminate and we say so.
        written = a.max(axis=2) > 8

        iou_f, cov = score(uv, faces, S, False, written)
        iou_t, _ = score(uv, faces, S, True, written)
        gap = abs(iou_f - iou_t)
        pick = "flip_y=False" if iou_f >= iou_t else "flip_y=True"
        decisive = gap > 0.05 and max(iou_f, iou_t) > 0.5

        print("%-18s S=%d  atlas written %.1f%%  our coverage %.1f%%  IoU  noflip %.4f  flip %.4f  -> %s%s"
              % (os.path.basename(p), S, 100 * written.mean(), 100 * cov,
                 iou_f, iou_t, pick, "" if decisive else "   (NOT DECISIVE)"))
        if decisive:
            verdicts.append(pick)

    print()
    if not verdicts:
        print("VERDICT: undecided - the atlases do not discriminate. Do not guess;")
        print("         settle it by re-baking one asset on a box and diffing the texture.")
        return 2
    if len(set(verdicts)) > 1:
        print("VERDICT: CONFLICTING across assets (%s) - investigate before trusting either."
              % ", ".join(sorted(set(verdicts))))
        return 3
    print("VERDICT: %s, agreed by %d asset(s)." % (verdicts[0], len(verdicts)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
