"""Build the large workbench (workbench_lg) from code - the hard-surface remake (2026-10-14).

  blender -b -P scripts/blender/rh_build_workbench_lg.py -- [--out Martians/gen/hs/workbench_lg]
      [--size 2048] [--preview-only]

The workshop room's large tier bench. Replaces the image-to-3D "workbench_lg"
the director marked "just needs a little polish": a smeared mesh whose task
lamps had shredded into floating slivers, whose vise had melted into a blob
and whose back was a smudged full-height panel. The DESIGN is the approved
reference, Martians/gen/tiers-redo/workbench_lg/raw/workbench_lg_three_quarter.png:
the family bench (scripts/blender/rh_furniture.py) dressed as a workbench -
two articulated task lamps clamped to the backsplash (left and right, both
reaching over the work surface), a bench vise at the right end with its jaws
and screw over the end, two small displays and lit indicator switches on the
backsplash, and the lower shelf stocked: a toolbox, spanners, a hammer and a
plane laid flat, a row of parts bins, a C-clamp hooked over the shelf lip.
Only the construction changes: every part is an exact primitive and nothing
is thin - lamp arms are 3.4 x 2.6 cm bars with 3 cm knuckles and 40 cm shades, the vise
handle 2.4 cm, the spanners 1.6 cm thick.

Layout facts: long axis X, working front at -Y, cheek end at -X, vise at +X
(no face_camera: room props are turned 0/90/180/270 deg per cell and every
side is seen from ~54 deg; the back is the family's bolted panel run, the
lamps' clamp blocks and the screen units' service plates). Bench top 0.90 m,
lamp heads ~1.45 m, larger horizontal extent ~2.42 m (vise screw included),
so the game's fit to 2.5 m is ~1.03 and the human-scale cues stay true.
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bpy  # noqa: E402,F401
from mathutils import Matrix, Vector  # noqa: E402

import rh_furniture as F  # noqa: E402

OUT, SIZE, PREVIEW_ONLY = F.args("Martians/gen/hs/workbench_lg")
NAME = "workbench_lg"

K = F.make_kit()
F.add_role(K, "lens", dict(albedo="#E8E4D6", metal=0.0, rough=0.30, dust=0.1))
parts = []
add = parts.append
SINK = F.SINK

# ------------------------------------------------------------------- bench
L, D, TH = 2.30, 0.80, 0.90
B = F.Bench(K, parts, L=L, D=D, TH=TH, seams=[-0.44, 0.36])
x0, x1 = -L / 2, L / 2
yf, yb = B.y_front, B.y_back
for x in (-0.52, 0.06, 0.80):
    B.switch(x)
B.screen_unit(-0.24, w=0.30, h=0.13)
B.screen_unit(0.44, w=0.30, h=0.13)


# ------------------------------------------------------------- task lamps
def lamp(prefix, bx, dir_xy, rise=0.56, reach=0.48):
    """Articulated task lamp clamped to the backsplash top at x = bx: base and
    post, lower arm rising slightly back, elbow knuckle, upper arm reaching
    along dir_xy over the work surface, head knuckle, shade with a lens."""
    by, bz = B.bs_mid_y, B.bs_z
    d = Vector((dir_xy[0], dir_xy[1], 0.0)).normalized()
    hinge = Vector((-d.y, d.x, 0.0))                 # knuckle axis: across the arm plane
    # clamp block over the backsplash top (seen from behind) + base disc + post
    add(K.box(prefix + "_clamp", (bx - 0.045, B.bs_y + 0.05, bz - 0.06), (bx + 0.045, yb + 0.02,
                                                                          bz + 0.02),
              "slate", bevel=0.008, seg=1))
    add(K.cyl(prefix + "_base", (bx, by, bz + 0.015), (bx, by, bz + 0.045), 0.052, "slate", sides=14))
    p0 = Vector((bx, by, bz + 0.085))
    add(K.cyl(prefix + "_post", (bx, by, bz + 0.04), p0 + Vector((0, 0, 0.02)), 0.022, "pipe",
              sides=10))
    add(K.cyl(prefix + "_k0", p0 - hinge * 0.032, p0 + hinge * 0.032, 0.030, "pipe", sides=12))
    elbow = p0 + Vector((0.0, 0.0, rise)) - d * 0.06
    add(F.obox(K, prefix + "_arm0", p0, elbow, 0.034, 0.026, "slate", side=hinge, bevel=0.005))
    add(K.cyl(prefix + "_k1", elbow - hinge * 0.032, elbow + hinge * 0.032, 0.030, "pipe", sides=12))
    head = elbow + d * reach + Vector((0, 0, 0.05))
    add(F.obox(K, prefix + "_arm1", elbow, head, 0.034, 0.026, "slate", side=hinge, bevel=0.005))
    add(K.cyl(prefix + "_k2", head - hinge * 0.032, head + hinge * 0.032, 0.028, "pipe", sides=12))
    # shade: a tapered bar along d, hanging 3 cm under the head knuckle, lens underneath
    hz = head.z - 0.035
    c = head + d * 0.08
    pts = []
    for s_len, s_w, z in ((0.20, 0.062, hz), (0.16, 0.042, hz + 0.055)):
        for a in (-1, 1):
            for b in (-1, 1):
                p = c + d * (a * s_len) + hinge * (b * s_w)
                pts.append((p.x, p.y, z))
    sh = F.hull_solid(K, prefix + "_shade", pts, "hull", bevel=0.008, seg=2)
    add(sh)
    add(K.cyl(prefix + "_neck", head, Vector((head.x, head.y, hz + 0.05)), 0.016, "pipe", sides=8))
    lens = []
    for a in (-1, 1):
        for b in (-1, 1):
            for z in (hz - 0.005, hz + 0.008):
                p = c + d * (a * 0.18) + hinge * (b * 0.048)
                lens.append((p.x, p.y, z))
    add(F.hull_solid(K, prefix + "_lens", lens, "lens", bevel=0.003, seg=1))


lamp("lampA", -0.86, (0.62, -0.78))
lamp("lampB", 0.70, (-0.55, -0.83), rise=0.50, reach=0.44)

# ---------------------------------------------------- bench vise (right end)
VX, VY = x1 - 0.13, yf + 0.20                      # swivel centre on the top
VZ = TH
add(K.cyl("v_swivel", (VX, VY, VZ - 0.004), (VX, VY, VZ + 0.028), 0.080, "slate", sides=16))
for a in range(3):                                  # swivel bolts
    t = 2 * math.pi * a / 3 + 0.5
    F.bolts(K, parts, "v_sb", [(VX + 0.062 * math.cos(t), VY + 0.062 * math.sin(t), VZ + 0.028)],
            (0, 0, 1), r=0.010)
# body: fixed jaw block with an anvil at its back, slide housing to the moving jaw
add(K.box("v_body", (VX - 0.10, VY - 0.055, VZ + 0.024), (VX + 0.02, VY + 0.055, VZ + 0.135), "pipe",
          bevel=0.010, seg=1))
add(K.box("v_anvil", (VX - 0.14, VY - 0.045, VZ + 0.095), (VX - 0.08, VY + 0.045, VZ + 0.125), "pipe",
          bevel=0.008, seg=1))
add(K.box("v_slide", (VX + 0.01, VY - 0.032, VZ + 0.040), (VX + 0.18, VY + 0.032, VZ + 0.090), "slate",
          bevel=0.006, seg=1))
add(K.box("v_jaw", (VX + 0.055, VY - 0.060, VZ + 0.045), (VX + 0.115, VY + 0.060, VZ + 0.135), "pipe",
          bevel=0.010, seg=1))
for x_a, x_b in ((VX + 0.018, VX + 0.026), (VX + 0.050, VX + 0.058)):   # jaw plates (steel)
    add(K.box("v_plate", (x_a, VY - 0.058, VZ + 0.100), (x_b, VY + 0.058, VZ + 0.134), "metal",
              bevel=0.002, seg=1))
# screw out past the end of the bench, T-handle hanging down, ball knobs
SXe = VX + 0.215
add(K.cyl("v_screw", (VX + 0.11, VY, VZ + 0.075), (SXe, VY, VZ + 0.075), 0.013, "metal", sides=10))
add(K.cyl("v_hub", (SXe - 0.02, VY, VZ + 0.075), (SXe + 0.012, VY, VZ + 0.075), 0.026, "pipe", sides=12))
add(K.cyl("v_handle", (SXe, VY, VZ - 0.07), (SXe, VY, VZ + 0.14), 0.012, "metal", sides=8))
for z in (VZ - 0.07, VZ + 0.14):
    add(K.cyl("v_knob", (SXe, VY, z - 0.020), (SXe, VY, z + 0.020), 0.021, "slate", sides=10))

# ------------------------------------------------------- lower shelf stock
SZ = B.shelves[0][2]
# toolbox (left, back half)
TX0, TX1, TY0, TY1 = -0.90, -0.50, -0.02, 0.24
TZ1 = SZ + 0.19
add(K.box("tbox", (TX0, TY0, SZ - 0.004), (TX1, TY1, TZ1), "hull", bevel=0.012, seg=1))
add(K.box("tbox_band", (TX0 - 0.004, TY0 - 0.004, TZ1 - 0.05), (TX1 + 0.004, TY1 + 0.004, TZ1 - 0.032),
          "slate", bevel=0.004, seg=1))
tcx = (TX0 + TX1) / 2
add(K.pipe("tbox_handle", [(tcx - 0.10, (TY0 + TY1) / 2, TZ1 - 0.004), (tcx - 0.10, (TY0 + TY1) / 2,
                                                                        TZ1 + 0.05),
                           (tcx + 0.10, (TY0 + TY1) / 2, TZ1 + 0.05),
                           (tcx + 0.10, (TY0 + TY1) / 2, TZ1 - 0.004)],
           0.013, "slate", bend=0.025, sides=8, samples=3))
for lx in (TX0 + 0.08, TX1 - 0.08):
    for sy, yy in ((-1, TY0), (1, TY1)):
        ya, yb2 = sorted((yy - sy * SINK, yy + sy * 0.014))
        add(K.box("tbox_latch", (lx - 0.022, ya, TZ1 - 0.075), (lx + 0.022, yb2, TZ1 - 0.020), "metal",
                  bevel=0.003, seg=1))


def lay_flat(objs, x, y, yaw_deg, t=0.016):
    """Turn a tool built hanging along -Z (flat in XZ, t thick) to lie on the
    shelf floor, its length along yaw_deg, centred near (x, y)."""
    M = (Matrix.Translation((x, y, SZ + t / 2 - 0.001)) @ Matrix.Rotation(math.radians(yaw_deg), 4, "Z")
         @ Matrix.Rotation(math.radians(90.0), 4, "X"))
    return F.place(objs, M)


parts += lay_flat(F.wrench(K, "wr0", (0, 0, 0.13), 0.26), -0.40, -0.16, 90.0, t=0.018)
parts += lay_flat(F.wrench(K, "wr1", (0, 0, 0.11), 0.22, head=0.030), -0.13, -0.20, 75.0, t=0.018)
parts += lay_flat(F.hammer(K, "ham", (0, 0, 0.16), 0.30), 0.17, -0.14, 100.0, t=0.036)
# block plane (back middle): rust body, steel sole, knob and tote
PX, PY = -0.20, 0.13
add(K.box("plane_sole", (PX - 0.13, PY - 0.035, SZ - 0.002), (PX + 0.13, PY + 0.035, SZ + 0.02), "metal",
          bevel=0.004, seg=1))
add(K.box("plane_body", (PX - 0.12, PY - 0.032, SZ + 0.016), (PX + 0.10, PY + 0.032, SZ + 0.06), "rust",
          bevel=0.010, seg=1))
add(K.cyl("plane_knob", (PX - 0.10, PY, SZ + 0.05), (PX - 0.10, PY, SZ + 0.095), 0.020, "rust", sides=10))
add(F.obox(K, "plane_tote", (PX + 0.06, PY, SZ + 0.05), (PX + 0.10, PY, SZ + 0.12), 0.026, 0.040, "rust",
           side=(0, 1, 0), bevel=0.006))
# parts bins in a row (right, back half)
for k in range(3):
    bx0 = 0.32 + k * 0.215
    parts.extend(F.parts_bin(K, "bin%d" % k, (bx0, 0.01, SZ - 0.004), (bx0 + 0.20, 0.25, SZ + 0.13),
                             "slate", "metal"))
# C-clamp hooked over the front lip of the shelf (left), painted RustBeam
CX = -0.62
ly = B.ly
lip_top, lip_y = SZ + 0.022, -ly - 0.004
add(K.box("cc_top", (CX - 0.016, lip_y - 0.078, lip_top - 0.002), (CX + 0.016, lip_y + 0.04,
                                                                    lip_top + 0.021),
          "rust", bevel=0.005, seg=1))
add(K.box("cc_spine", (CX - 0.020, lip_y - 0.085, SZ - 0.14), (CX + 0.020, lip_y - 0.050,
                                                                lip_top + 0.026),
          "rust", bevel=0.006, seg=1))
add(K.box("cc_bot", (CX - 0.016, lip_y - 0.078, SZ - 0.135), (CX + 0.016, lip_y + 0.03, SZ - 0.11), "rust",
          bevel=0.005, seg=1))
SCY = lip_y + 0.004
zr_bot = B.shelves[0][2] - 0.016 - B.R + 0.004 - B.R      # underside of the lower front rail
add(K.cyl("cc_screw", (CX, SCY, SZ - 0.20), (CX, SCY, zr_bot + 0.002), 0.010, "metal", sides=8))
add(K.cyl("cc_pad", (CX, SCY, zr_bot - 0.010), (CX, SCY, zr_bot + 0.004), 0.020, "metal", sides=10))
add(K.cyl("cc_tbar", (CX - 0.06, SCY, SZ - 0.19), (CX + 0.06, SCY, SZ - 0.19), 0.009, "metal", sides=8))

F.finalize(K, parts, NAME, OUT, SIZE, PREVIEW_ONLY)
