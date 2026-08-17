"""Extract a TRELLIS.2 GLB's baked PBR maps to PNGs, and emit a UE import manifest.

  uv run --with pillow python scripts/gpu/rh_glb_textures.py \
      --glb <asset.glb> --name <asset> --out <dir> [--manifest <pairs.json>]

WHY THIS EXISTS
---------------
A mesh reimport NEVER carries the source file's textures - measured 2026-08-16,
when a clean "8/8 ok" reimport left every crop still wearing an aerial photo of
a city. So `rh_reimport_inplace.py` must ALWAYS be followed by
`rh_import_textures.py`, and that one wants PNGs on disk plus a manifest. The
maps are embedded in the GLB's binary chunk, so something has to unpack them.

The manifest asserts sRGB per texture rather than letting the importer guess
from the filename: base colour is COLOUR, the packed metallic-roughness map is
DATA. Import an MR map as sRGB and nothing downstream complains - the surface
just responds wrongly to light.

The output names match the asset names UE already has, so the import lands on
the existing textures instead of creating siblings:
    <name>_textured                                     (albedo, sRGB)
    <name>_textured_metallic-<name>_textured_roughness  (MR, linear, TC_MASKS)
"""
import argparse
import io
import json
import os
import struct

from PIL import Image


def glb_images(path):
    """Yield (index, PIL.Image) for every image in the GLB, plus the parsed json."""
    b = open(path, "rb").read()
    magic, _, total = struct.unpack("<III", b[:12])
    if magic != 0x46546C67:
        raise ValueError("%s is not a GLB (bad magic)" % path)
    off, chunks = 12, []
    while off < total:
        clen, _ = struct.unpack("<II", b[off:off + 8])
        chunks.append(b[off + 8:off + 8 + clen])
        off += 8 + clen
    j = json.loads(chunks[0].decode("utf-8"))
    bin_ = chunks[1] if len(chunks) > 1 else b""
    out = []
    for i, img in enumerate(j.get("images", [])):
        bv = j["bufferViews"][img["bufferView"]]
        s = bv.get("byteOffset", 0)
        out.append((i, Image.open(io.BytesIO(bin_[s:s + bv["byteLength"]]))))
    return j, out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--glb", required=True)
    ap.add_argument("--name", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--dest", default=None, help="/Game path for the manifest")
    ap.add_argument("--manifest", default=None)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)

    j, images = glb_images(a.glb)
    prim = j["meshes"][0]["primitives"][0]
    pbr = j["materials"][prim["material"]]["pbrMetallicRoughness"]

    def src(texinfo):
        return j["textures"][texinfo["index"]]["source"]

    want = {}
    if "baseColorTexture" in pbr:
        want[src(pbr["baseColorTexture"])] = ("%s_textured" % a.name, True)
    if "metallicRoughnessTexture" in pbr:
        want[src(pbr["metallicRoughnessTexture"])] = (
            "%s_textured_metallic-%s_textured_roughness" % (a.name, a.name), False)
    if not want:
        raise SystemExit("no baseColor/metallicRoughness texture in %s" % a.glb)

    dest = a.dest or "/Game/RedHope/Art/Tiers/%s/Textures" % a.name
    entries = []
    for idx, im in images:
        if idx not in want:
            continue
        asset_name, srgb = want[idx]
        png = os.path.join(a.out, asset_name + ".png")
        im.convert("RGB").save(png, "PNG")
        entries.append({"png": png, "dest_path": dest, "dest_name": asset_name,
                        "srgb": srgb})
        print("  %-62s %dx%d srgb=%s %d KB"
              % (asset_name, im.size[0], im.size[1], srgb,
                 os.path.getsize(png) // 1024))

    if len(entries) != len(want):
        raise SystemExit("expected %d maps, wrote %d" % (len(want), len(entries)))
    if a.manifest:
        prev = []
        if os.path.exists(a.manifest):
            prev = json.load(open(a.manifest))
        json.dump(prev + entries, open(a.manifest, "w"), indent=1)
        print("  manifest -> %s (%d entries)" % (a.manifest, len(prev) + len(entries)))


if __name__ == "__main__":
    main()
