import bpy, sys
argv = sys.argv[sys.argv.index("--")+1:]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=argv[0])
ms=[o for o in bpy.context.scene.objects if o.type=="MESH"]
bpy.ops.object.select_all(action="DESELECT")
for o in ms: o.select_set(True)
bpy.context.view_layer.objects.active=ms[0]
if len(ms)>1: bpy.ops.object.join()
o=bpy.context.view_layer.objects.active
me=o.data
zs=[ (o.matrix_world @ v.co).z for v in me.vertices ]
zmin,zmax=min(zs),max(zs); H=zmax-zmin
print(f"[an] verts={len(me.vertices)} Zrange={H:.4f}")
# histogram of verts by Z band (20 bands), plus XY footprint per band
import mathutils
co=[o.matrix_world @ v.co for v in me.vertices]
NB=20
for b in range(NB):
    lo=zmin+H*b/NB; hi=zmin+H*(b+1)/NB
    band=[c for c in co if lo<=c.z<hi or (b==NB-1 and c.z==hi)]
    if not band:
        print(f"[an] band{b:2d} z[{b/NB:.2f}-{(b+1)/NB:.2f}] verts=0"); continue
    xs=[c.x for c in band]; ys=[c.y for c in band]
    fw=max(xs)-min(xs); fd=max(ys)-min(ys)
    print(f"[an] band{b:2d} z[{b/NB:.2f}-{(b+1)/NB:.2f}] verts={len(band):5d} footprint={fw:.3f}x{fd:.3f}")
# loose parts count
bpy.ops.object.mode_set(mode="EDIT")
bpy.ops.mesh.select_all(action="SELECT")
bpy.ops.mesh.separate(type="LOOSE")
bpy.ops.object.mode_set(mode="OBJECT")
parts=[x for x in bpy.context.scene.objects if x.type=="MESH"]
print(f"[an] loose_parts={len(parts)}")
for p in sorted(parts,key=lambda x:-len(x.data.vertices))[:6]:
    pz=[(p.matrix_world@v.co).z for v in p.data.vertices]
    print(f"[an]   part verts={len(p.data.vertices):5d} zc={ (min(pz)+max(pz))/2-zmin:.3f} zext={max(pz)-min(pz):.3f}")
