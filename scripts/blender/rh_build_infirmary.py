"""Build the infirmary bed from code - the hard-surface remake (2026-10-14).

  blender -b -P scripts/blender/rh_build_infirmary.py -- [--out Martians/gen/hs/infirmary]
      [--size 2048] [--preview-only]

The infirmary room's tier furniture. Replaces the image-to-3D "infirmary" the
director marked "just needs a little polish": a smeared mesh whose curtain
had shredded into a fringe, whose drawers had melted into the frame and whose
back was a smudged slab. The DESIGN is the approved reference,
Martians/gen/tiers-redo/infirmary/raw/infirmary_three_quarter.png: the family
bench (scripts/blender/rh_furniture.py) dressed as a medical bed - a grey
mattress pad with a pillow at the right end and a folded blanket at the left,
a vitals monitor on an arm at the back-left with a lit heartbeat trace, a bank
of drawers on the lower shelf (two big, six small) with supply bins beside it,
and a curtain rail stand on casters at the right end. Only the construction
changes: every part is an exact primitive, the curtain is one solid pleated
panel, and the emissive mask is the trace, the indicator switches and the
monitor's status lamp.

Gate-D framing (CLAUDE.md, art bible 7): a clean, ready bed - no patient, no
fluids, nothing graphic; the monitor shows a steady, abstract trace.

Layout facts: long axis X, working front at -Y, cheek end at -X, curtain
stand at +X (no face_camera: room props are turned 0/90/180/270 deg per cell
and every side is seen from ~54 deg, so the back carries drawers and bolted
panels too). Bed surface: frame top 0.66 m, mattress top ~0.76 m (a bed, not
a bench; the addendum's ~0.65 m cue), backsplash 0.86 m, monitor top ~1.30 m,
curtain stand 1.30 m. Larger horizontal extent ~2.45 m, so the game's fit to
2.5 m is ~1.0 and the human-scale cues stay true. Tube radius 2.8 cm (frame),
2.5 cm (stand); the trace is a 1.6 cm strip standing 4 mm off dark glass.
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bpy  # noqa: E402,F401
from mathutils import Matrix, Vector  # noqa: E402

import rh_furniture as F  # noqa: E402

OUT, SIZE, PREVIEW_ONLY = F.args("Martians/gen/hs/infirmary")
NAME = "infirmary"

K = F.make_kit()
F.add_role(K, "fabric", dict(albedo="#8D939B", metal=0.0, rough=0.90, dust=0.5))   # mattress
F.add_role(K, "linen", dict(albedo="#CDCEC8", metal=0.0, rough=0.85, dust=0.4))    # pillow
F.add_role(K, "blanket", dict(albedo="#6F7782", metal=0.0, rough=0.92, dust=0.5))
F.add_role(K, "curtain", dict(albedo="#AEB2B4", metal=0.0, rough=0.90, dust=0.4))
parts = []
add = parts.append
SINK = F.SINK

# ------------------------------------------------------------------- bench
L, D, TH = 2.12, 0.82, 0.66
B = F.Bench(K, parts, L=L, D=D, TH=TH, top_t=0.06, apron_h=0.10, BH=0.20, BT=0.11,
            shelf_z=0.20, seams=[-0.36, 0.36])
x0, x1 = -L / 2, L / 2
yf = B.y_front
for x in (-0.20, 0.36):                            # high on the panels: clear of the mattress
    B.switch(x, z=B.bs_z - 0.085, h=0.06)

# -------------------------------------------------------------- the bed
# mattress pad: from the cheek to the right end, in front of the backsplash
mx0, mx1 = x0 + B.cheek_w + 0.012, x1 - 0.025
my0, my1 = yf + 0.035, B.bs_panel_y - 0.012
MT = TH + 0.095                                    # mattress top
add(K.box("mattress", (mx0, my0, TH - 0.006), (mx1, my1, MT), "fabric", bevel=0.028, seg=3))
# a seam welt round the mattress (a slightly proud band) reads "upholstered"
add(K.box("welt", (mx0 - 0.004, my0 - 0.004, TH + 0.030), (mx1 + 0.004, my1 + 0.004, TH + 0.046),
          "blanket", bevel=0.006, seg=1))
# pillow at the right end (+X), the head of the bed
add(K.box("pillow", (0.60, my0 + 0.05, MT - 0.012), (mx1 - 0.04, my1 - 0.04, MT + 0.075), "linen",
          bevel=0.034, seg=3))
# turned-down top sheet below the pillow: a pale band wrapping the mattress
add(K.box("sheet", (0.44, my0 - 0.006, MT - 0.045), (0.56, my1 + 0.006, MT + 0.014), "linen",
          bevel=0.010, seg=2))
# folded blanket at the left (foot) end: two folds, the top one smaller
add(K.box("blanket0", (-0.90, my0 + 0.03, MT - 0.008), (-0.36, my0 + 0.46, MT + 0.040), "blanket",
          bevel=0.016, seg=2))
add(K.box("blanket1", (-0.87, my0 + 0.05, MT + 0.034), (-0.40, my0 + 0.43, MT + 0.078), "blanket",
          bevel=0.016, seg=2))

# -------------------------------------------------- vitals monitor on an arm
PX, PY = -0.66, B.bs_mid_y + 0.005                 # post foot, on the backsplash top
add(K.cyl("mon_base", (PX, PY, B.bs_z - 0.01), (PX, PY, B.bs_z + 0.025), 0.05, "slate", sides=12))
add(K.cyl("mon_post", (PX, PY, B.bs_z), (PX, PY, B.bs_z + 0.24), 0.022, "pipe", sides=12))
add(K.cyl("mon_knuckle", (PX, PY, B.bs_z + 0.215), (PX, PY, B.bs_z + 0.265), 0.032, "slate",
          sides=12))
ARM_Y = PY - 0.13
add(F.obox(K, "mon_arm", (PX, PY, B.bs_z + 0.24), (PX, ARM_Y, B.bs_z + 0.24), 0.040, 0.034, "slate",
           side=(0, 0, 1), bevel=0.006))
# monitor: built at the origin facing -Y, tilted back 10 deg, hung on the arm end
MW, MH, MD = 0.40, 0.27, 0.075
mon = []
m = mon.append
HZ = -MH / 2 + 0.08                                # hinge height (clear of the back plate edge)
m(K.box("mon_hinge", (-0.04, 0.02, HZ - 0.03), (0.04, MD + 0.03, HZ + 0.03), "slate", bevel=0.008))
m(K.box("mon_house", (-MW / 2, 0.0, -MH / 2), (MW / 2, MD, MH / 2), "hull", bevel=0.018, seg=2))
GW, GH = MW - 0.05, MH - 0.06
m(K.box("mon_glass", (-GW / 2, -0.005, -GH / 2 + 0.01), (GW / 2, 0.004, GH / 2 + 0.01), "glass",
        bevel=0.004, seg=1))
# heartbeat trace: a 1.6 cm lit strip, 4 mm off the glass (a steady, abstract rhythm)
tr = [(-0.15, 0.0), (-0.085, 0.0), (-0.065, 0.025), (-0.045, 0.0), (-0.020, 0.0), (0.0, 0.072),
      (0.024, -0.058), (0.044, 0.0), (0.085, 0.0), (0.103, 0.022), (0.121, 0.0), (0.150, 0.0)]
m(F.ribbon(K, "mon_trace", [(u, v + 0.022) for u, v in tr], 0.016, 0.006, "screen",
           M=Matrix.Translation((0, -0.009, 0)) @ F.UV_ON_FRONT))
# baseline grid ticks under the trace and a status lamp (lit), a label bar (slate)
m(K.box("mon_lamp", (MW / 2 - 0.058, -0.008, -MH / 2 + 0.018), (MW / 2 - 0.030, 0.003,
                                                                 -MH / 2 + 0.034),
        "screen", bevel=0.002, seg=1))
m(K.box("mon_bar", (-GW / 2 + 0.02, -0.009, -GH / 2 + 0.028), (-GW / 2 + 0.11, -0.003,
                                                                 -GH / 2 + 0.044),
        "screen", bevel=0.002, seg=1))
m(K.box("mon_btns", (-MW / 2 + 0.03, -0.008, -MH / 2 + 0.010), (-MW / 2 + 0.16, 0.003,
                                                                 -MH / 2 + 0.022),
        "slate", bevel=0.002, seg=1))
# back: slate service plate with vent slats and bolts
m(K.box("mon_back", (-MW / 2 + 0.04, MD - SINK, -MH / 2 + 0.04), (MW / 2 - 0.04, MD + 0.012,
                                                                    MH / 2 - 0.04),
        "slate", bevel=0.004, seg=1))
for k in range(4):
    zz = -0.05 + k * 0.032
    m(K.box("mon_slat", (-0.12, MD + 0.010, zz - 0.008), (0.12, MD + 0.020, zz + 0.008), "pipe",
            bevel=0.002, seg=1))
F.bolts(K, mon, "mon_bolt", [(x, MD + 0.012, z) for x, z in F.grid((-0.14, 0.14), (-0.09, 0.09))],
        (0, 1, 0), r=0.008)
MZ = B.bs_z + 0.24 - HZ                            # hinge on the arm
F.place(mon, Matrix.Translation((PX, ARM_Y - MD - 0.02, MZ)) @ Matrix.Rotation(math.radians(-10.0),
                                                                                4, "X"))
parts.extend(mon)

# ------------------------------------------------ drawers on the lower shelf
SZ = B.shelves[0][2]
CZ0, CZ1 = SZ - 0.004, B.z_ur - B.R - 0.010        # cabinet: shelf floor to under the rail
CX0, CX1 = -0.80, 0.30
CY = 0.27
add(K.box("cabinet", (CX0, -CY, CZ0), (CX1, CY, CZ1), "hull", bevel=0.010, seg=1))
add(K.box("cab_plinth", (CX0 + 0.02, -CY + 0.02, CZ0 - 0.002), (CX1 - 0.02, CY - 0.02, CZ0 + 0.022),
          "slate", bevel=0.004, seg=1))
for sy in (-1, 1):                                 # drawers on both faces (a bed is walked round)
    yy = sy * CY
    dz0, dz1 = CZ0 + 0.028, CZ1 - 0.016
    F.drawer_front(K, parts, "drw_big0", CX0 + 0.02, CX0 + 0.37, dz0, dz1, yy, facing=sy)
    F.drawer_front(K, parts, "drw_big1", CX0 + 0.385, CX0 + 0.735, dz0, dz1, yy, facing=sy)
    zm = (dz0 + dz1) / 2
    for c in range(3):
        xa = CX0 + 0.755 + c * 0.112
        for (za, zb) in ((dz0, zm - 0.006), (zm + 0.006, dz1)):
            F.drawer_front(K, parts, "drw_s", xa, xa + 0.102, za, zb, yy, label=False, facing=sy)
# supplies beside it: a lidded canister and two bins
add(K.cyl("canister", (0.47, -0.10, SZ - 0.004), (0.47, -0.10, SZ + 0.15), 0.075, "linen", sides=16))
add(K.cyl("can_lid", (0.47, -0.10, SZ + 0.145), (0.47, -0.10, SZ + 0.175), 0.080, "slate", sides=16))
add(K.cyl("can_knob", (0.47, -0.10, SZ + 0.170), (0.47, -0.10, SZ + 0.195), 0.025, "slate", sides=8))
parts.extend(F.parts_bin(K, "bin0", (0.38, 0.03, SZ - 0.004), (0.66, 0.25, SZ + 0.10), "slate",
                         "linen"))
parts.extend(F.parts_bin(K, "bin1", (0.58, -0.26, SZ - 0.004), (0.78, -0.04, SZ + 0.09), "slate",
                         "linen"))

# ------------------------------------------- curtain rail stand on casters (+X)
SX = x1 + 0.135                                    # stand plane (x)
SY0, SY1 = -0.24, 0.36                             # post positions (y)
SR = 0.025
ZT = 1.28                                          # rail top axis
for y in (SY0, SY1):                               # caster feet: a short bar along X per post
    add(K.cyl("st_foot", (SX - 0.12, y, 0.075), (SX + 0.12, y, 0.075), SR, "slate", sides=10))
    for xx in (SX - 0.10, SX + 0.10):
        add(K.cyl("st_fork", (xx, y, 0.042), (xx, y, 0.094), 0.020, "pipe", sides=8))
        add(K.cyl("st_wheel", (xx, y - 0.016, 0.032), (xx, y + 0.016, 0.032), 0.032, "rubber",
                  sides=12))
add(K.pipe("st_frame", [(SX, SY0, 0.068), (SX, SY0, ZT), (SX, SY1, ZT), (SX, SY1, 0.068)], SR, "slate",
           bend=0.08, sides=12, samples=4))
add(K.cyl("st_stretch", (SX, SY0, 0.34), (SX, SY1, 0.34), SR * 0.9, "slate", sides=10))
for y in (SY0, SY1):                               # fitting collars where the stretcher joins
    add(K.cyl("st_collar", (SX, y, 0.30), (SX, y, 0.38), SR + 0.008, "pipe", sides=10))
    add(K.cyl("st_collar", (SX, y, 0.92), (SX, y, 0.99), SR + 0.008, "pipe", sides=10))
# curtain: one solid panel with soft folds (7 broad folds, 2 cm cloth, 5 cm deep), hung on rings
CZ_TOP, CZ_BOT = ZT - 0.04, 0.42
pleats = [(SY0 + 0.05 + (SY1 - SY0 - 0.10) * i / 48.0,
           0.015 * math.sin(2 * math.pi * 7 * i / 48.0)) for i in range(49)]
# ribbon in local (u=y, v=x) plane, extruded along local w (= z)
Mc = Matrix(((0, 1, 0, SX), (1, 0, 0, 0), (0, 0, 1, CZ_BOT), (0, 0, 0, 1)))
add(F.ribbon(K, "curtain", pleats, 0.020, CZ_TOP - CZ_BOT, "curtain", M=Mc))
for i in range(6):                                 # rings tying the curtain to the rail
    y = SY0 + 0.07 + (SY1 - SY0 - 0.14) * i / 5.0
    add(K.cyl("st_ring", (SX - 0.034, y, ZT - 0.055), (SX + 0.034, y, ZT - 0.055), 0.018, "pipe",
              sides=8))
    add(K.cyl("st_hook", (SX, y, ZT - 0.07), (SX, y, ZT), 0.010, "pipe", sides=6))

F.finalize(K, parts, NAME, OUT, SIZE, PREVIEW_ONLY)
