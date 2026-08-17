import bpy, sys, os
argv = sys.argv[sys.argv.index("--")+1:]
src, dst = argv[0], argv[1]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=src)
bpy.ops.export_scene.fbx(filepath=dst, path_mode='COPY', embed_textures=True,
                         apply_unit_scale=True, bake_space_transform=False,
                         object_types={'MESH'}, use_mesh_modifiers=True)
print("WROTE", dst)
