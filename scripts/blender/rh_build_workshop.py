"""Build the workshop bench from code - the hard-surface remake (2026-10-14).

  blender -b -P scripts/blender/rh_build_workshop.py -- [--out Martians/gen/hs/workshop]
      [--size 2048] [--preview-only]

The workshop room's top-tier furniture. Replaces the image-to-3D "workshop"
the director flagged "the back looks all smudgy": a smeared mesh whose tool
rack melted into dripping slivers, whose drill press was a blob and whose
back - the side the reference never showed - was a smudged, torn panel. The
DESIGN is the approved reference,
Martians/gen/tiers-redo/workshop/raw/workshop_three_quarter.png: the family
bench (scripts/blender/rh_furniture.py) in three sections on six legs, the
middle one an open machine bay with a drill press on a low plinth; an
overhead pipe rack (an elbowed top pipe on four uprights with tee collars and
an end cap) carrying three light bars and a slate tool rail with a row of
hanging tools - graduated spanners, hammers, screwdrivers, two drills; three
small displays on the backsplash and a lit keypad on each section; parts bins
on the two side shelves. Only the construction changes: every part is an
exact primitive and every tool is chunky (spanners 1.8 cm thick, handles
>= 2 cm, shafts 1.6 cm) - nothing that can shred.

THE BACK, explicitly: it is finished like the front, because a room prop is
turned 0/90/180/270 deg per cell and seen from ~54 deg on every side. From
behind you see: the tool rail's back face with a bolted clamp at every upright
and an ID plate per section; the uprights' bolted brackets on the apron; the
backsplash's bolted panel run; a bolted slate back panel closing the machine
bay (the drill press's backdrop from the front); the screens' slate service
plates; the light bars' finished housings; the tools' own solid backs.

Layout facts: long axis X, working front at -Y, cheek end at -X, elbow of the
rack at -X, rack end cap at +X (no face_camera). Bench top 0.90 m, tool rail
~1.58 m, rack top pipe 1.86 m; larger horizontal extent ~2.41 m, so the
game's fit to 2.5 m is ~1.04 and the human-scale cues stay true. Uprights
r 2.6 cm, frame tubes r 2.8 cm.
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bpy  # noqa: E402,F401
from mathutils import Matrix, Vector  # noqa: E402

import rh_furniture as F  # noqa: E402

OUT, SIZE, PREVIEW_ONLY = F.args("Martians/gen/hs/workshop")
NAME = "workshop"

K = F.make_kit()
F.add_role(K, "lens", dict(albedo="#E8E4D6", metal=0.0, rough=0.30, dust=0.1))
parts = []
add = parts.append
SINK = F.SINK

# ------------------------------------------------------------------- bench
L, D, TH = 2.40, 0.80, 0.90
BAY = 0.30                                           # machine bay: legs at +-BAY
x0, x1 = -L / 2, L / 2
CW = 0.07
B = F.Bench(K, parts, L=L, D=D, TH=TH, seams=[-0.40, 0.36],
            legs_x=[-1.03, -BAY, BAY, 1.03],
            shelves=[(-1.03, -BAY, 0.34), (BAY, 1.03, 0.34)],
            apron_gaps=[(-BAY + 0.03, BAY - 0.03)], open_front=[(-BAY, BAY)],
            bs_panels=[(x0 + CW + 0.03, -0.37), (-0.35, 0.35), (0.37, x1 - 0.03)], lite=True,
            upper_side_rails=False)
yf, yb = B.y_front, B.y_back
ly, R = B.ly, B.R
for x in (-0.62, 0.10, 0.72):                       # a lit keypad on each section
    B.keypad(x)
for x in (-0.86, -0.12, 0.42):                       # a display above each section
    B.screen_unit(x, w=0.28, h=0.12, lite=True)

# machine bay back panel: bolted slate, both faces finished
BPY = ly                                            # on the back legs' plane
BPZ0, BPZ1 = 0.16, B.z_top - 0.07
add(K.box("bay_back", (-BAY + R - 0.004, BPY - 0.011, BPZ0), (BAY - R + 0.004, BPY + 0.011, BPZ1),
          "slate", bevel=0.004, seg=1))
for sy in (-1, 1):
    yy = BPY + sy * 0.011
    ya, yb2 = sorted((yy - sy * SINK, yy + sy * 0.010))
    pz1 = B.z_ur - R - 0.03                          # plate stops under the back rail
    add(K.box("bay_plate", (-BAY + 0.09, ya, BPZ0 + 0.05), (BAY - 0.09, yb2, pz1), "apron",
              bevel=0.004, seg=1))
    F.bolts(K, parts, "bay_b", [(x, yy + sy * 0.010, pz1 - 0.02) for x in (-BAY + 0.11, BAY - 0.11)],
            (0, sy, 0), r=0.009)
# the top over the bay: a slate stiffener under the tiles (front), like the apron's line
add(K.box("bay_lintel", (-BAY - 0.01, yf + 0.040, B.z_top - 0.10), (BAY + 0.01, yf + 0.085,
                                                                    B.z_top - 0.055),
          "slate", bevel=0.006, seg=1))

# ------------------------------------------------------------ drill press
# on a low plinth standing on the floor between the bay legs (the reference's
# low platform; a plinth instead of a third rail ring keeps the frame lean),
# pulled to the FRONT of the bay: from the 54 deg camera the bench top hides
# anything deeper than ~0.3 m behind its front edge (round 1 hid the head).
add(K.box("plinth", (-0.25, -0.33, 0.0), (0.25, 0.18, 0.11), "slate", bevel=0.010, seg=1))
add(K.box("plinth_top", (-0.235, -0.315, 0.105), (0.235, 0.165, 0.125), "hull", bevel=0.006, seg=1))
PZ = 0.125                                          # platform top
dp = []
d_ = dp.append
d_(K.box("dp_base", (-0.16, -0.14, PZ - 0.004), (0.16, 0.14, PZ + 0.04), "slate", bevel=0.010, seg=1))
CX, CY = 0.0, 0.10                                  # column (built at y 0, then moved forward)
d_(K.cyl("dp_col", (CX, CY, PZ + 0.03), (CX, CY, 0.72), 0.034, "metal", sides=12))
d_(K.cyl("dp_colfoot", (CX, CY, PZ + 0.035), (CX, CY, PZ + 0.07), 0.050, "slate", sides=14))
# table on a collar
d_(K.cyl("dp_tcollar", (CX, CY, 0.36), (CX, CY, 0.42), 0.048, "slate", sides=14))
d_(K.box("dp_tarm", (-0.035, -0.04, 0.37), (0.035, CY, 0.41), "slate", bevel=0.006, seg=1))
d_(K.cyl("dp_table", (0.0, -0.05, 0.395), (0.0, -0.05, 0.425), 0.11, "pipe", sides=20))
# head, motor, belt cover
HZ0, HZ1 = 0.57, 0.74
d_(K.box("dp_head", (-0.10, -0.11, HZ0), (0.10, CY + 0.06, HZ1), "hull", bevel=0.024, seg=1))
d_(K.cyl("dp_motor", (0.0, CY + 0.04, 0.69), (0.0, 0.27, 0.69), 0.072, "slate", sides=16))
d_(K.cyl("dp_mcap", (0.0, 0.265, 0.69), (0.0, 0.285, 0.69), 0.058, "pipe", sides=16))
d_(K.box("dp_belt", (-0.11, -0.095, HZ1 - 0.006), (0.11, CY + 0.07, HZ1 + 0.05), "hull", bevel=0.018,
         seg=1))
d_(K.box("dp_label", (-0.065, -0.114, 0.63), (0.065, -0.106, 0.69), "metal", bevel=0.002, seg=1))
# quill, chuck, bit
d_(K.cyl("dp_quill", (0.0, -0.05, HZ0 + 0.01), (0.0, -0.05, 0.51), 0.026, "metal", sides=12))
d_(K.cyl("dp_chuck", (0.0, -0.05, 0.515), (0.0, -0.05, 0.475), 0.032, "pipe", sides=12))
d_(K.cyl("dp_bit", (0.0, -0.05, 0.48), (0.0, -0.05, 0.44), 0.008, "metal", sides=6))
# feed handles: hub on the right side, three chunky spokes with ball knobs
hub = Vector((0.10, -0.01, 0.64))
d_(K.cyl("dp_hub", hub - Vector((0.01, 0, 0)), hub + Vector((0.04, 0, 0)), 0.034, "slate", sides=12))
for k in range(3):
    a = math.radians(90 + 120 * k)
    tip = hub + Vector((0.03, 0.15 * math.cos(a), 0.15 * math.sin(a)))
    d_(K.cyl("dp_spoke", hub + Vector((0.03, 0, 0)), tip, 0.009, "metal", sides=8))
    d_(K.cyl("dp_knob", tip - Vector((0.02, 0, 0)), tip + Vector((0.02, 0, 0)), 0.021, "slate", sides=10))
parts.extend(F.place(dp, Matrix.Translation((0.0, -0.17, 0.0))))

# ----------------------------------------------------------- side shelves
for (sx0, sx1, sz) in B.shelves:
    w = (sx1 - sx0 - 0.08) / 3
    for k in range(3):
        bx0 = sx0 + 0.04 + k * w + 0.005
        parts.extend(F.parts_bin(K, "bin", (bx0, -0.22, sz - 0.004), (bx0 + w - 0.01, 0.06, sz + 0.13),
                                 "slate", "metal"))
    # a lidded stock case behind the bins
    add(K.box("case", (sx0 + 0.08, 0.10, sz - 0.004), (sx1 - 0.08, 0.27, sz + 0.12), "hull", bevel=0.012,
              seg=1))
    add(K.box("case_band", (sx0 + 0.076, 0.096, sz + 0.075), (sx1 - 0.076, 0.274, sz + 0.093), "slate",
              bevel=0.004, seg=1))

# ------------------------------------------------------- overhead pipe rack
UR = 0.026                                          # upright radius
YU = yb + UR - 0.006                                # uprights bite 6 mm into the top's back
ZT = 1.86                                           # top pipe axis
UX = [-1.14, -0.36, 0.36, 0.98]
ZU0 = B.z_ab - 0.05                                 # upright foot, under the apron line
CAP_X = 1.15
# the rack is straight runs + one short elbow (left), every joint under a
# collar with a 2 mm gap inside it: straight runs unwrap to compact UV strips
# (round 1's single filleted L-run unwrapped as two long bent islands and left
# a third of the atlas empty)
EL = 0.16                                           # elbow leg length
add(K.pipe("rack_elbow", [(UX[0], YU, ZT - EL), (UX[0], YU, ZT), (UX[0] + EL, YU, ZT)], UR, "slate",
           bend=0.08, sides=10, samples=5))
add(K.cyl("upright0", (UX[0], YU, ZU0), (UX[0], YU, ZT - EL - 0.002), UR, "slate", sides=10))
add(K.cyl("coupler", (UX[0], YU, ZT - EL - 0.035), (UX[0], YU, ZT - EL + 0.035), UR + 0.007, "pipe",
          sides=10))
add(K.cyl("coupler", (UX[0] + EL - 0.035, YU, ZT), (UX[0] + EL + 0.035, YU, ZT), UR + 0.007, "pipe",
          sides=10))
runs = [UX[0] + EL + 0.002] + [v for x in UX[1:] for v in (x - 0.002, x + 0.002)] + [CAP_X]
for a_, b_ in zip(runs[0::2], runs[1::2]):          # top pipe between the tees
    add(K.cyl("rack_top", (a_, YU, ZT), (b_, YU, ZT), UR, "slate", sides=10))
add(K.cyl("rack_cap", (CAP_X - 0.01, YU, ZT), (CAP_X + 0.02, YU, ZT), UR + 0.007, "pipe", sides=10))
for x in UX[1:]:
    add(K.cyl("upright", (x, YU, ZU0), (x, YU, ZT - UR - 0.002), UR, "slate", sides=10))
    add(K.cyl("tee", (x - 0.05, YU, ZT), (x + 0.05, YU, ZT), UR + 0.008, "pipe", sides=10))
for x in (-0.75, 0.0, 0.67):                        # couplers between the tees (reference)
    add(K.cyl("coupler", (x - 0.035, YU, ZT), (x + 0.035, YU, ZT), UR + 0.007, "pipe", sides=10))
# foot of each upright: end cap + a bolted bracket onto the apron's back face
for x in UX:
    add(K.cyl("ufoot", (x, YU, ZU0 - 0.018), (x, YU, ZU0 + 0.02), UR + 0.005, "pipe", sides=10))
    bz0, bz1 = B.z_ab + 0.012, B.z_top - B.R - 0.035
    add(K.box("ubracket", (x - 0.045, yb - 0.045, bz0), (x + 0.045, YU + UR + 0.012, bz1), "slate",
              bevel=0.006, seg=1))
    F.bolts(K, parts, "ubb", [(x, YU + UR + 0.012, (bz0 + bz1) / 2)], (0, 1, 0), r=0.010)

# --------------------------------------------------------------- light bars
for k, cx in enumerate((-0.75, 0.0, 0.67)):
    hy = YU - 0.05
    hz = ZT - 0.13                                  # shade underside
    pts = []
    for (hl, hw, z) in ((0.25, 0.060, hz), (0.23, 0.045, hz + 0.055)):
        for a in (-1, 1):
            for b in (-1, 1):
                pts.append((cx + a * hl, hy + b * hw, z))
    add(F.hull_solid(K, "shade%d" % k, pts, "metal", bevel=0.008, seg=1))
    lens = [(cx + a * 0.23, hy + b * 0.047, z) for a in (-1, 1) for b in (-1, 1)
            for z in (hz - 0.005, hz + 0.01)]
    add(F.hull_solid(K, "lens%d" % k, lens, "lens"))
    for sx in (-1, 1):                              # two stems up into the top pipe
        add(K.cyl("stem", (cx + sx * 0.16, hy + 0.02, hz + 0.05), (cx + sx * 0.16, YU - 0.01, ZT), 0.012,
                  "pipe", sides=8))

# ---------------------------------------------------------------- tool rail
RZ = 1.58                                           # rail centre
RY1 = YU - UR + 0.004                               # rail back face (bites the uprights)
RY0 = RY1 - 0.036                                   # rail front face
RX0, RX1 = UX[0] - 0.03, UX[-1] + 0.05
add(K.box("rail", (RX0, RY0, RZ - 0.028), (RX1, RY1, RZ + 0.028), "slate", bevel=0.006, seg=1))
for x in UX:                                        # clamp blocks round each upright
    add(K.box("rclamp", (x - 0.04, RY0 - 0.006, RZ - 0.042), (x + 0.04, YU + UR + 0.010, RZ + 0.042),
              "pipe", bevel=0.008, seg=1))
    F.bolts(K, parts, "rcb", [(x, YU + UR + 0.010, RZ)], (0, 1, 0), r=0.009)
    F.bolts(K, parts, "rcf", [(x, RY0 - 0.006, RZ)], (0, -1, 0), r=0.009)
# back face of the rail: an ID plate per section between the clamps
for a, b in zip(UX[:-1], UX[1:]):
    mx = (a + b) / 2
    add(K.box("rplate", (mx - 0.10, RY1 - SINK, RZ - 0.018), (mx + 0.10, RY1 + 0.008, RZ + 0.018), "metal",
              bevel=0.003, seg=1))

PEG_Z = RZ - 0.012
TY = RY0 - 0.020                                    # tool mid-plane, on the pegs


def peg(x):
    add(K.cyl("peg", (x, RY0 + 0.004, PEG_Z), (x, RY0 - 0.040, PEG_Z), 0.011, "metal", sides=8))


def hang_wrench(x, length, head):
    peg(x)
    parts.extend(F.wrench(K, "wr", (x, TY, PEG_Z + head * 0.72), length, head=head, bevel=0.0,
                          ring_sides=8))


def hang_hammer(x, length):
    peg(x)
    # hung by its head on the peg: the head straddles the peg, handle down
    parts.extend(F.hammer(K, "ham", (x, TY, PEG_Z + 0.018), length))


def hang_driver(x, length):
    peg(x)
    add(K.box("drv_loop", (x - 0.016, TY - 0.012, PEG_Z - 0.012), (x + 0.016, TY + 0.012, PEG_Z + 0.022),
              "slate", bevel=0.0))
    parts.extend(F.screwdriver(K, "drv", (x, TY, PEG_Z - 0.010), length))


def hang_drill(x):
    """Cordless drill hung by a hook loop: RustBeam body, slate grip and battery."""
    peg(x)
    z = PEG_Z - 0.015
    add(K.box("dr_hook", (x - 0.012, TY - 0.012, z - 0.012), (x + 0.012, TY + 0.012, PEG_Z + 0.02), "slate",
              bevel=0.004, seg=1))
    add(K.box("dr_body", (x - 0.10, TY - 0.032, z - 0.075), (x + 0.07, TY + 0.032, z), "rust",
              bevel=0.014, seg=2))
    add(K.cyl("dr_chuck", (x - 0.098, TY, z - 0.038), (x - 0.15, TY, z - 0.038), 0.022, "metal", sides=10))
    add(F.obox(K, "dr_grip", (x + 0.025, TY, z - 0.07), (x + 0.045, TY, z - 0.19), 0.040, 0.050, "slate",
               side=(0, 1, 0), bevel=0.008))
    add(K.box("dr_batt", (x - 0.025, TY - 0.036, z - 0.235), (x + 0.10, TY + 0.036, z - 0.18), "slate",
              bevel=0.008, seg=1))


# section 1 (left): graduated spanners, a hammer, two screwdrivers
for k, (ln, hd) in enumerate(((0.27, 0.036), (0.25, 0.034), (0.23, 0.032), (0.21, 0.030))):
    hang_wrench(-1.03 + k * 0.085, ln, hd)
hang_hammer(-0.64, 0.28)
hang_driver(-0.53, 0.22)
hang_driver(-0.47, 0.19)
# section 2 (middle): spanners, an adjustable spanner, screwdrivers
for k, (ln, hd) in enumerate(((0.27, 0.036), (0.25, 0.034), (0.23, 0.032), (0.21, 0.030))):
    hang_wrench(-0.26 + k * 0.085, ln, hd)
hang_wrench(0.12, 0.22, 0.040)
hang_driver(0.21, 0.22)
hang_driver(0.27, 0.20)
# section 3 (right): two drills (chucks clear of the upright clamps)
hang_drill(0.56)
hang_drill(0.82)

# ~380 parts, a third of them bolts and collars: the kit's default UV margins
# round that many tiny islands left ~45 % of the atlas empty; tighter margins
# (still >= 3 px between islands at 2048) give the bigger surfaces the texels
F.finalize(K, parts, NAME, OUT, SIZE, PREVIEW_ONLY, island_margin=0.001, pack_margin=0.0015)
