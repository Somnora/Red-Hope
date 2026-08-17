# Measures whether a GLB is genuinely smooth-shaded by comparing each loop's
# stored (split) normal to its face's flat normal. On a flat-shaded mesh the
# loop normal == the face normal (deviation ~0). On a smooth mesh, loop normals
# on curved regions deviate from their face normal. Reports the mean/median
# angular deviation in degrees and the fraction of loops that deviate > 5 deg.
import bpy, sys, math, statistics
from mathutils import Vector

glb = sys.argv[sys.argv.index("--") + 1:][0]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=glb)
devs = []
for o in [x for x in bpy.data.objects if x.type == 'MESH']:
    me = o.data
    me.calc_normals_split() if hasattr(me, "calc_normals_split") else None
    for poly in me.polygons:
        fn = poly.normal
        for li in poly.loop_indices:
            ln = me.loops[li].normal
            d = fn.dot(ln)
            d = max(-1.0, min(1.0, d))
            devs.append(math.degrees(math.acos(d)))
if devs:
    over5 = 100.0 * sum(1 for d in devs if d > 5.0) / len(devs)
    print("NORMCHK %s mean=%.2f median=%.2f max=%.2f pct_smoothed(>5deg)=%.1f%%" % (
        glb.split("/")[-1], statistics.mean(devs), statistics.median(devs),
        max(devs), over5))
else:
    print("NORMCHK %s no loops" % glb.split("/")[-1])
