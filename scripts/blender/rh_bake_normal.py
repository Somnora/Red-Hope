"""Bake a REAL tangent normal map: high-poly shape onto the shipped low-poly UVs.

    blender --background --python scripts/blender/rh_bake_normal.py -- \
        <low.glb> <hi.glb> <out_normal.png> [size=2048] [extrusion_m=0.05]

On a fresh Lambda box the bundled Blender needs X11 shims even headless:
    sudo apt-get install -y libsm6 libxext6 libxrender1 libxi6 libxxf86vm1 libxfixes3 libgl1

Companion to rh_trellis2.py's RH_KEEP_HIPOLY=1, which exports the
pre-decimation surface the pipeline otherwise discards. This bakes that
surface's detail into a tangent-space normal keyed to the LOW mesh's own UV
layout - the layout its albedo/MR were baked to - so it drops into the same MI
via the existing NormTex/UseNormTex path with no re-wiring.

Replaces the albedo-luminance DERIVED normals for any asset baked from here on:
a derived normal can only guess relief from paint (and once turned paint noise
into orange-peel pebbling); this one measures it from geometry.

CONVENTION: Blender bakes OpenGL (+Y green). UE's raw-PNG normal convention is
DirectX (-Y), so the green channel is FLIPPED here, in the file - self-contained,
no import-side flag for someone to forget.

Cycles CPU, selected-to-active with an extrusion cage distance. Default 5 cm:
the first proof bake (cmdr_vale, 2.5M -> 11.4k tris) ran at 2 cm and left dark
patches on the thigh where the decimated surface deviates past the cage - rays
missed the high poly entirely. 5 cm covers the deviation this decimation level
actually produces. The image is also initialised to FLAT NORMAL (0.5, 0.5, 1)
before the bake, so any texel the rays still miss degrades to "no relief"
instead of decoding as a violently wrong black normal.
"""
import sys

import bpy


def _import_one(path):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=path)
    news = [o for o in bpy.data.objects if o not in before and o.type == "MESH"]
    # join multi-part imports so there is exactly one target/source
    bpy.ops.object.select_all(action="DESELECT")
    for o in news:
        o.select_set(True)
    bpy.context.view_layer.objects.active = news[0]
    if len(news) > 1:
        bpy.ops.object.join()
    obj = bpy.context.view_layer.objects.active
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    obj.select_set(False)
    return obj


argv = sys.argv[sys.argv.index("--") + 1:]
low_p, hi_p, out_p = argv[0], argv[1], argv[2]
size = int(argv[3]) if len(argv) > 3 else 2048
extrusion = float(argv[4]) if len(argv) > 4 else 0.05

bpy.ops.wm.read_factory_settings(use_empty=True)
low = _import_one(low_p)
hi = _import_one(hi_p)

if not low.data.uv_layers:
    raise SystemExit("low mesh has no UVs - nothing to bake onto")

# bake target image, wired into (or created on) the low mesh's material
img = bpy.data.images.new("bake_nrm", size, size, alpha=False, float_buffer=False)
img.colorspace_settings.name = "Non-Color"
img.generated_color = (0.5, 0.5, 1.0, 1.0)  # missed texels degrade to flat, not black
if not low.data.materials:
    low.data.materials.append(bpy.data.materials.new("bakemat"))
mat = low.data.materials[0]
mat.use_nodes = True
node = mat.node_tree.nodes.new("ShaderNodeTexImage")
node.image = img
mat.node_tree.nodes.active = node

sc = bpy.context.scene
sc.render.engine = "CYCLES"
sc.cycles.device = "CPU"
sc.cycles.samples = 16          # normals converge fast; this is not lighting
sc.render.bake.use_selected_to_active = True
sc.render.bake.cage_extrusion = extrusion
sc.render.bake.max_ray_distance = extrusion * 4
sc.render.bake.margin = 8

bpy.ops.object.select_all(action="DESELECT")
hi.select_set(True)
low.select_set(True)
bpy.context.view_layer.objects.active = low
bpy.ops.object.bake(type="NORMAL", normal_space="TANGENT")

# OpenGL -> DirectX green flip, in the pixels
px = list(img.pixels)
for i in range(1, len(px), 4):
    px[i] = 1.0 - px[i]
img.pixels = px
img.filepath_raw = out_p
img.file_format = "PNG"
img.save()
print("[bake] %s (%dpx, extrusion %.3f, hi %d tris -> low %d tris)"
      % (out_p, size, extrusion, len(hi.data.polygons), len(low.data.polygons)), flush=True)
