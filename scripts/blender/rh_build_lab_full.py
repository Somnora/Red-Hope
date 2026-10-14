"""Build the full lab bench (lab_full) from code - the hard-surface remake (2026-10-14).

  blender -b -P scripts/blender/rh_build_lab_full.py -- [--out Martians/gen/hs/lab_full]
      [--size 2048] [--preview-only]

The lab room's top-tier furniture. Replaces the image-to-3D "lab_full" the
director marked "just needs a little polish": a smeared mesh whose tall
cabinet had a torn, half-open back, whose sink and instruments had melted into
the top and whose shelf boxes had fused into the frame. The DESIGN is the
approved reference, Martians/gen/tiers-redo/lab_full/raw/lab_full_three_quarter.png:
the family bench (scripts/blender/rh_furniture.py) dressed as a lab bench - a
tall two-door wall cabinet / fume-hood unit standing on the backsplash right of
centre (lit screen on the left door, louvre vents low on both doors, a small
lit readout in a slate bezel, an exhaust collar on the roof), a round sink
basin with a chunky gooseneck tap at the left, a small display on the
backsplash, a four-knob instrument block at the front right, lit indicator
switches, a rack of chunky reagent bottles in front of the cabinet, and
storage on the lower shelf (a lidded drum, a cabinet with a
door, a latched case). Only the construction changes: every part is an exact
primitive, and the emissive mask is the displays and switches.

Layout facts: long axis X, working front at -Y, cheek end at -X (no
face_camera: room props are turned 0/90/180/270 deg per cell and every side is
seen from ~54 deg). The back is finished: the cabinet's back carries raised
panels, bolts and two conduits that drop into the apron; its sides carry
bolted panels; the backsplash back is the family's bolted panel run. Bench
top 0.90 m, cabinet roof 2.00 m, exhaust collar ~2.10 m; larger horizontal
extent ~2.41 m, so the game's fit to 2.5 m is ~1.04 and the human-scale cues
stay true. Tap r 1.8 cm, knobs r 3 cm, louvre slats 1.6 cm.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bpy  # noqa: E402,F401

import rh_furniture as F  # noqa: E402

OUT, SIZE, PREVIEW_ONLY = F.args("Martians/gen/hs/lab_full")
NAME = "lab_full"

K = F.make_kit()
parts = []
add = parts.append
SINK = F.SINK

# ------------------------------------------------------------------- bench
L, D, TH = 2.40, 0.80, 0.90
B = F.Bench(K, parts, L=L, D=D, TH=TH, seams=[-0.46, 0.38])
x0, x1 = -L / 2, L / 2
yf, yb = B.y_front, B.y_back
for x in (-0.98, -0.12):
    B.switch(x)


def bezel(name, center, w, h, proud, radius, rim, recess, rim_role, inner_role, facing=-1):
    assert radius - rim >= 0.005, "%s: rim must be < radius - 5 mm" % name
    return K.bezel(name, center, w, h, proud, radius=radius, rim=rim, recess=recess,
                   rim_role=rim_role, inner_role=inner_role, facing=facing)


def louvre(prefix, cx, cz, y, w, h, n):
    """Slate frame on a -Y face (plane y) with n hull slats standing 6 mm proud."""
    add(K.box(prefix + "_fr", (cx - w / 2, y - 0.010, cz - h / 2), (cx + w / 2, y + SINK, cz + h / 2),
              "slate", bevel=0.005, seg=1))
    pitch = (h - 0.03) / n
    for k in range(n):
        z = cz - h / 2 + 0.015 + pitch * (k + 0.5)
        add(K.box(prefix + "_sl", (cx - w / 2 + 0.014, y - 0.016, z - 0.008),
                  (cx + w / 2 - 0.014, y - 0.008, z + 0.008), "hull", bevel=0.003, seg=1))


# --------------------------------------------- the tall cabinet / fume-hood unit
CX0, CX1 = 0.02, 0.97
CY0, CY1 = B.bs_y - 0.10, yb - 0.004               # front face, back face
CZ0, CZ1 = B.bs_z - 0.006, B.bs_z + 0.95           # sits on the backsplash top
add(K.box("cab", (CX0, CY0, CZ0), (CX1, CY1, CZ1), "hull", bevel=0.016, seg=2))
# two doors, raised, a 1 cm seam between
DF = CY0 - 0.014                                   # door front plane
DZ0, DZ1 = CZ0 + 0.035, CZ1 - 0.035
CM = (CX0 + CX1) / 2
doors = [(CX0 + 0.025, CM - 0.005), (CM + 0.005, CX1 - 0.025)]
for k, (a, b) in enumerate(doors):
    add(K.box("door%d" % k, (a, DF, DZ0), (b, CY0 + SINK, DZ1), "hull", bevel=0.007, seg=1))
    F.bolts(K, parts, "dbolt", [(x, DF, z) for x, z in F.grid((a + 0.025, b - 0.025),
                                                               (DZ0 + 0.025, DZ1 - 0.025))],
            (0, -1, 0), r=0.009)
    # pull beside the seam
    px = b - 0.045 if k == 0 else a + 0.045
    add(K.box("pull%d" % k, (px - 0.014, DF - 0.022, CZ0 + 0.40), (px + 0.014, DF + SINK, CZ0 + 0.56),
              "slate", bevel=0.006, seg=1))
    # hinges on the outer edges
    hx = a + 0.004 if k == 0 else b - 0.004
    for hz in (DZ0 + 0.12, DZ1 - 0.12):
        add(K.cyl("hinge", (hx, DF - 0.006, hz - 0.045), (hx, DF - 0.006, hz + 0.045), 0.014,
                  "pipe", sides=8))
# left door: the main lit screen in a slate bezel, a lit bar and a knob below it
sx_, sz_ = (doors[0][0] + doors[0][1]) / 2 - 0.02, CZ0 + 0.56
bz, wy = bezel("scr_bezel", (sx_, DF + SINK, sz_), 0.27, 0.21, 0.022 + SINK, radius=0.03,
               rim=0.020, recess=0.010, rim_role="slate", inner_role="glass")
add(bz)
add(K.box("scr_lit", (sx_ - 0.105, wy - 0.005, sz_ - 0.075), (sx_ + 0.105, wy + 0.002, sz_ + 0.075),
          "screen", bevel=0.003, seg=1))
add(K.box("scr_bar_plate", (sx_ - 0.12, DF - 0.010, sz_ - 0.175), (sx_ + 0.03, DF + SINK, sz_ - 0.135),
          "slate", bevel=0.004, seg=1))
add(K.box("scr_bar", (sx_ - 0.11, DF - 0.014, sz_ - 0.166), (sx_ - 0.04, DF - 0.008, sz_ - 0.144),
          "screen", bevel=0.002, seg=1))
add(K.cyl("scr_knob", (sx_ + 0.09, DF + 0.002, sz_ - 0.155), (sx_ + 0.09, DF - 0.026, sz_ - 0.155),
          0.024, "slate", sides=12))
# louvres low on both doors; a small readout in a bezel on the right door
louvre("lv0", sx_ + 0.02, CZ0 + 0.16, DF, 0.32, 0.15, 6)
louvre("lv1", (doors[1][0] + doors[1][1]) / 2, CZ0 + 0.16, DF, 0.32, 0.15, 6)
rx, rz = (doors[1][0] + doors[1][1]) / 2 + 0.02, CZ0 + 0.34
bz, wy = bezel("rd_bezel", (rx, DF + SINK, rz), 0.17, 0.10, 0.026 + SINK, radius=0.025, rim=0.016,
               recess=0.012, rim_role="slate", inner_role="glass")
add(bz)
add(K.box("rd_lit", (rx - 0.060, wy - 0.005, rz - 0.028), (rx + 0.060, wy + 0.002, rz + 0.028),
          "screen", bevel=0.002, seg=1))
# roof: exhaust collar (the fume-hood read from the 54 deg camera) on a slate plinth
add(K.box("roof_plinth", (0.45, CY0 + 0.05, CZ1 - 0.006), (0.85, CY1 - 0.04, CZ1 + 0.03), "slate",
          bevel=0.008, seg=1))
EX, EY = 0.65, (CY0 + CY1) / 2
add(K.cyl("duct", (EX, EY, CZ1 + 0.02), (EX, EY, CZ1 + 0.12), 0.085, "slate", sides=16))
add(K.cyl("duct_ring", (EX, EY, CZ1 + 0.09), (EX, EY, CZ1 + 0.13), 0.098, "pipe", sides=16))
add(K.cyl("duct_cap", (EX, EY, CZ1 + 0.125), (EX, EY, CZ1 + 0.142), 0.07, "rubber", sides=16))
F.bolts(K, parts, "rbolt", [(x, y, CZ1 + 0.03) for x in (0.475, 0.825)
                            for y in (CY0 + 0.075, CY1 - 0.065)], (0, 0, 1), r=0.009)
# sides: bolted raised panels
for sx, xx in ((-1, CX0), (1, CX1)):
    xa, xb = sorted((xx - sx * SINK, xx + sx * 0.010))
    add(K.box("cab_side%d" % sx, (xa, CY0 + 0.04, CZ0 + 0.06), (xb, CY1 - 0.04, CZ1 - 0.06), "hull",
              bevel=0.005, seg=1))
    F.bolts(K, parts, "sbolt", [(xx + sx * 0.010, y, z) for y, z in
                                F.grid((CY0 + 0.06, CY1 - 0.06), (CZ0 + 0.08, CZ1 - 0.08))],
            (sx, 0, 0), r=0.009)
# back: two bolted panels, a slate vent and two conduits dropping into the apron
for k, (a, b) in enumerate(((CX0 + 0.04, CM - 0.006), (CM + 0.006, CX1 - 0.04))):
    add(K.box("cab_back%d" % k, (a, CY1 - SINK, CZ0 + 0.05), (b, CY1 + 0.012, CZ1 - 0.05), "hull",
              bevel=0.005, seg=1))
    F.bolts(K, parts, "bbolt", [(x, CY1 + 0.012, z) for x, z in
                                F.grid((a + 0.025, b - 0.025), (CZ0 + 0.075, CZ1 - 0.075))],
            (0, 1, 0), r=0.009)
add(K.box("back_vent", (CM - 0.16, CY1 + 0.008, CZ1 - 0.30), (CM + 0.16, CY1 + 0.024, CZ1 - 0.14),
          "slate", bevel=0.005, seg=1))
for k in range(5):
    z = CZ1 - 0.285 + k * 0.032
    add(K.box("bv_slat", (CM - 0.145, CY1 + 0.020, z - 0.008), (CM + 0.145, CY1 + 0.030, z + 0.008),
              "pipe", bevel=0.002, seg=1))
for cx in (CX0 + 0.16, CX1 - 0.16):
    cy = yb + 0.034
    add(K.pipe("conduit", [(cx, CY1 + 0.004, CZ0 + 0.30), (cx, cy, CZ0 + 0.30), (cx, cy, TH - 0.10),
                           (cx, yb - 0.06, TH - 0.10)], 0.022, "pipe", bend=0.05, sides=10,
               samples=3))
    add(K.box("cclamp", (cx - 0.035, yb - 0.016, TH + 0.06), (cx + 0.035, cy + 0.030, TH + 0.10),
              "slate", bevel=0.005, seg=1))

# ------------------------------------------------------------- sink (left)
SXc, SYc = -0.70, -0.06
SR = 0.20
add(F.lathe_z(K, "sink", [(0.0, TH + 0.010), (SR - 0.05, TH + 0.010), (SR - 0.03, TH + 0.042),
                          (SR - 0.018, TH + 0.052), (SR + 0.004, TH + 0.052), (SR + 0.014, TH + 0.040),
                          (SR + 0.014, TH - 0.006), (0.0, TH - 0.006)],
              SXc, SYc, ["hull", "metal"], seg=24, mis=[1, 1, 1, 0, 0, 0, 0, 0]))
add(K.cyl("drain", (SXc, SYc, TH + 0.008), (SXc, SYc, TH + 0.016), 0.035, "slate", sides=12))
TX, TY = SXc, B.bs_y - 0.05                        # tap riser, in front of the backsplash
add(K.cyl("tap_base", (TX, TY, TH - 0.004), (TX, TY, TH + 0.030), 0.036, "slate", sides=12))
add(K.pipe("tap", [(TX, TY, TH + 0.02), (TX, TY, TH + 0.30), (TX, SYc + 0.02, TH + 0.30),
                   (TX, SYc + 0.02, TH + 0.215)], 0.018, "metal", bend=0.07, sides=10, samples=4))
add(F.obox(K, "tap_lever", (TX, TY, TH + 0.10), (TX + 0.11, TY, TH + 0.14), 0.030, 0.026, "slate",
           side=(0, 1, 0), bevel=0.005))

# ----------------------------------------------- display on the backsplash
B.screen_unit(-0.36, w=0.30, h=0.13)

# ----------------------------- reagent rack in front of the cabinet (lab read)
F.add_role(K, "amber", dict(albedo="#8C5530", metal=0.0, rough=0.30, dust=0.3))   # amber glass
RX0, RX1, RY0, RY1 = 0.10, 0.44, 0.02, 0.15
add(F.tub(K, "rack", (RX0, RY0, TH - 0.004), (RX1, RY1, TH + 0.035), 0.016, 0.022, "slate",
          inner_role="rubber", bevel=0.004))
for k, (role, r, h) in enumerate((("hull", 0.034, 0.15), ("amber", 0.030, 0.12),
                                  ("hull", 0.034, 0.15), ("amber", 0.030, 0.12))):
    bx = RX0 + 0.05 + k * (RX1 - RX0 - 0.10) / 3
    by = (RY0 + RY1) / 2
    add(K.cyl("bottle", (bx, by, TH + 0.012), (bx, by, TH + h), r, role, sides=10))
    add(K.cyl("bneck", (bx, by, TH + h - 0.004), (bx, by, TH + h + 0.022), r * 0.62, role, sides=8))
    add(K.cyl("bcap", (bx, by, TH + h + 0.018), (bx, by, TH + h + 0.044), r * 0.70, "slate", sides=8))

# ------------------------------------------ instrument block (front right)
IX0, IX1, IY0, IY1 = 0.60, 0.98, -0.32, -0.08
IZ1 = TH + 0.11
add(K.box("instr", (IX0, IY0, TH - 0.006), (IX1, IY1, IZ1), "hull", bevel=0.014, seg=2))
add(K.box("instr_band", (IX0 - 0.004, IY0 - 0.004, TH + 0.012), (IX1 + 0.004, IY1 + 0.004, TH + 0.034),
          "slate", bevel=0.004, seg=1))
for kx in (IX0 + 0.10, IX1 - 0.10):
    for ky in (IY0 + 0.065, IY1 - 0.065):
        add(K.cyl("knob_ring", (kx, ky, IZ1 - 0.004), (kx, ky, IZ1 + 0.010), 0.045, "pipe", sides=12))
        add(K.cyl("knob", (kx, ky, IZ1 + 0.008), (kx, ky, IZ1 + 0.032), 0.030, "slate", sides=12))
add(K.box("instr_lit", ((IX0 + IX1) / 2 - 0.05, IY0 - 0.008, TH + 0.055),
          ((IX0 + IX1) / 2 + 0.05, IY0 + 0.004, TH + 0.085), "screen", bevel=0.002, seg=1))

# ----------------------------------------------------- lower shelf storage
SZ = B.shelves[0][2]
# lidded drum (left)
DX, DY, DR = -0.66, 0.0, 0.17
add(K.cyl("drum", (DX, DY, SZ - 0.004), (DX, DY, SZ + 0.27), DR, "hull", sides=18))
for z in (SZ + 0.06, SZ + 0.20):
    add(K.cyl("drum_hoop", (DX, DY, z - 0.012), (DX, DY, z + 0.012), DR + 0.008, "slate", sides=18))
add(K.cyl("drum_lid", (DX, DY, SZ + 0.265), (DX, DY, SZ + 0.29), DR - 0.012, "slate", sides=18))
add(K.cyl("drum_cap", (DX + 0.06, DY - 0.04, SZ + 0.285), (DX + 0.06, DY - 0.04, SZ + 0.305), 0.03,
          "pipe", sides=10))
# cabinet with a door on each face (centre)
BX0, BX1, BY = -0.36, 0.30, 0.24
BZ1 = SZ + 0.29
add(K.box("scab", (BX0, -BY, SZ - 0.004), (BX1, BY, BZ1), "hull", bevel=0.012, seg=1))
for sy in (-1, 1):
    yy = sy * BY
    ya, yb2 = sorted((yy - sy * SINK, yy + sy * 0.014))
    add(K.box("sdoor", (BX0 + 0.03, ya, SZ + 0.03), (BX1 - 0.03, yb2, BZ1 - 0.03), "hull", bevel=0.006,
              seg=1))
    yo = yy + sy * 0.014
    ya, yb2 = sorted((yo - sy * SINK, yo + sy * 0.020))
    add(K.box("shandle", (BX1 - 0.085, ya, SZ + 0.09), (BX1 - 0.060, yb2, BZ1 - 0.09), "slate",
              bevel=0.006, seg=1))
# latched case (right)
CX_0, CX_1, CY_ = 0.44, 0.92, 0.22
add(K.box("case", (CX_0, -CY_, SZ - 0.004), (CX_1, CY_, SZ + 0.22), "hull", bevel=0.014, seg=2))
add(K.box("case_lid_band", (CX_0 - 0.004, -CY_ - 0.004, SZ + 0.145), (CX_1 + 0.004, CY_ + 0.004,
                                                                       SZ + 0.165),
          "slate", bevel=0.004, seg=1))
for sy in (-1, 1):
    for lx in (CX_0 + 0.10, CX_1 - 0.10):
        ya, yb2 = sorted((sy * CY_ - sy * SINK, sy * (CY_ + 0.016)))
        add(K.box("latch", (lx - 0.025, ya, SZ + 0.12), (lx + 0.025, yb2, SZ + 0.19), "metal",
                  bevel=0.004, seg=1))

F.finalize(K, parts, NAME, OUT, SIZE, PREVIEW_ONLY)
