p='/lambda/nfs/red-hope-east/red_hope/scripts/mesh_cleanup.py'
s=open(p).read()

# 1) accept optional 4th arg: strip_base (0/1)
s=s.replace(
'target_tris = int(argv[2]) if len(argv) > 2 else 15000',
'target_tris = int(argv[2]) if len(argv) > 2 else 15000\nstrip_base  = len(argv) > 3 and str(argv[3]) not in ("0", "", "false", "False")')

# 2) insert the stripper function + call right after the weld block's log line
anchor='log(f"after weld: ~{tri_count(obj)} tris")'
stripper='''

def strip_bottom_plate(o):
    """Remove a thin flat shadow DISC baked under the object (a wide bottom band,
    a NECK where the footprint pinches in, then the body widening again). Opt-in:
    only runs with strip_base, and only cuts when all three guards hold, so it can
    never eat splayed legs (discrete narrow feet, no filled wide disc) or a
    legitimately wide base (crate: no neck)."""
    import bmesh
    me = o.data
    zs = [v.co.z for v in me.vertices]
    zmin, zmax = min(zs), max(zs); H = zmax - zmin
    if H <= 1e-6:
        log("strip: flat mesh, skip"); return
    NB = 24
    def band_area(b):
        lo = zmin + H*b/NB; hi = zmin + H*(b+1)/NB
        xs = [v.co.x for v in me.vertices if lo <= v.co.z < hi]
        ys = [v.co.y for v in me.vertices if lo <= v.co.z < hi]
        if len(xs) < 8: return 0.0, 0
        return (max(xs)-min(xs))*(max(ys)-min(ys)), len(xs)
    areas = [band_area(b) for b in range(NB)]
    a0, n0 = areas[0]
    amax = max(a for a,_ in areas) or 1.0
    # neck = first band in the bottom 30% whose area drops below 0.6*a0
    neck = None
    for b in range(1, max(2, int(NB*0.3))):
        if areas[b][0] < 0.6 * a0:
            neck = b; break
    if neck is None:
        log("strip: no neck under a wide bottom -> no plate, skip"); return
    # guards: bottom disc must be WIDE (a real plate, not a foot) and the body
    # ABOVE the neck must RECOVER wider than the disc (plate-neck-body signature).
    recover = max((areas[b][0] for b in range(neck, NB)), default=0.0)
    if a0 < 0.35 * amax:
        log(f"strip: bottom band too small (a0={a0:.3f} < .35*amax) -> not a plate, skip"); return
    if recover < 1.05 * a0:
        log(f"strip: body never widens past the disc -> likely legs, skip"); return
    cut = zmin + H*neck/NB
    bm = bmesh.new(); bm.from_mesh(me)
    geom = [g for g in list(bm.verts)+list(bm.edges)+list(bm.faces)]
    res = bmesh.ops.bisect_plane(bm, geom=geom, dist=1e-5,
            plane_co=(0,0,cut), plane_no=(0,0,1), clear_inner=True)
    # cap the opening so the base reads solid from a low angle
    holes = [e for e in bm.edges if len(e.link_faces) == 1]
    if holes:
        try: bmesh.ops.holes_fill(bm, edges=holes, sides=0)
        except Exception: pass
    bm.to_mesh(me); bm.free()
    me.update()
    log(f"strip: removed plate below z={cut-zmin:.3f} (neck band {neck}, "
        f"a0={a0:.2f} recover={recover:.2f}); now ~{tri_count(o)} tris")

if strip_base:
    strip_bottom_plate(obj)
'''
assert anchor in s, 'weld anchor not found'
s=s.replace(anchor, anchor+stripper, 1)
open(p,'w').write(s)
print('patched mesh_cleanup.py')
