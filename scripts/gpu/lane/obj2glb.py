"""Standalone-Blender OBJ (+MTL+texture) -> GLB, preserving the baked texture.
Run: blender --background --python obj2glb.py -- <in.obj> <out.glb>"""
import bpy, sys, os
argv = sys.argv[sys.argv.index("--") + 1:]
in_obj, out_glb = argv[0], argv[1]

bpy.ops.wm.read_factory_settings(use_empty=True)
# Blender 4.2 uses the new wm.obj_import
if hasattr(bpy.ops.wm, "obj_import"):
    bpy.ops.wm.obj_import(filepath=in_obj)
else:
    bpy.ops.import_scene.obj(filepath=in_obj)

meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
print(f"[obj2glb] imported {len(meshes)} mesh(es) from {os.path.basename(in_obj)}", flush=True)
bpy.ops.object.select_all(action="SELECT")
bpy.ops.export_scene.gltf(filepath=out_glb, export_format="GLB",
    export_yup=True, export_texcoords=True, export_normals=True,
    export_materials="EXPORT", export_image_format="AUTO")
print(f"[obj2glb] wrote {out_glb} ({os.path.getsize(out_glb)//1024} KB)", flush=True)
