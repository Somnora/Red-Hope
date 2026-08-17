"""Diff a permissive re-bake against its nvdiffrast original.

Compares geometry (tri/vert counts) and, more importantly, the baked baseColor
atlas pixel-for-pixel. A vertical flip - the failure mode the orientation
derivation could have got wrong - would show as a large diff that collapses to
near-zero when one side is flipped, so we test that explicitly rather than
inferring it from a single number.
"""
import io, json, struct, sys
import numpy as np
from PIL import Image

def load(p):
    b=open(p,'rb').read(); _,_,ln=struct.unpack('<III',b[:12]); off=12; ch=[]
    while off<ln:
        cl,_=struct.unpack('<II',b[off:off+8]); ch.append(b[off+8:off+8+cl]); off+=8+cl
    j=json.loads(ch[0].decode()); bin_=ch[1]
    acc=j['accessors']
    tris=sum(acc[pr['indices']]['count']//3 for m in j['meshes'] for pr in m['primitives'])
    vts =sum(acc[pr['attributes']['POSITION']]['count'] for m in j['meshes'] for pr in m['primitives'])
    pbr=j['materials'][0]['pbrMetallicRoughness']
    img=j['images'][j['textures'][pbr['baseColorTexture']['index']]['source']]
    bv=j['bufferViews'][img['bufferView']]; s=bv.get('byteOffset',0)
    tex=Image.open(io.BytesIO(bin_[s:s+bv['byteLength']])).convert('RGB')
    return tris, vts, np.asarray(tex).astype(np.int16)

for name, a_path, b_path in [tuple(x.split('=',2)) for x in sys.argv[1:]]:
    ta,va,A = load(a_path)
    tb,vb,B = load(b_path)
    if A.shape != B.shape:
        print("%-14s atlas shape differs %s vs %s" % (name, A.shape, B.shape)); continue
    d      = np.abs(A-B)
    dflip  = np.abs(A-B[::-1])
    print("%-14s tris %5d/%-5d verts %6d/%-6d | mean|d| %6.2f  >8: %5.2f%% | vs V-FLIPPED mean|d| %6.2f" %
          (name, ta, tb, va, vb, d.mean(), 100*(d.max(axis=2)>8).mean(), dflip.mean()))
