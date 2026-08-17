p='/lambda/nfs/red-hope-east/red_hope/scripts/mesh_cleanup.py'
s=open(p).read()
# Apply the glTF import rotation/scale so local coords == world for the stripper
# (and everything after). Insert right after the join resolves `obj`.
anchor='obj = bpy.context.view_layer.objects.active\n'
inject=anchor+'''
# glTF import leaves a Y-up->Z-up rotation on the object; bake it in so all
# downstream vertex math (plate strip, recenter) is in true world axes.
bpy.ops.object.select_all(action="DESELECT")
obj.select_set(True)
bpy.context.view_layer.objects.active = obj
bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
'''
# only replace the FIRST occurrence (right after join)
i=s.index(anchor)
s=s[:i]+inject+s[i+len(anchor):]
open(p,'w').write(s)
print('applied transform_apply after join')
