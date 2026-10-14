"""Build planter_wet from code - the room-prop remake (2026-10-14).

  blender -b -P scripts/blender/rh_build_planter_wet.py -- [--out Martians/gen/hs/planter_wet]
      [--size 2048] [--preview-only]

The planted Garden / Greenhouse furnishing (RHColonyVisualizerSubsystem::
RoomPropPath: Garden#planted, Greenhouse). Replaces the image-to-3D mesh the
director flagged "just needs a little polish and fix the back wall thats
breaking apart" on the 2026-10-13 triage: its tall back panel had shredded into
triangular holes and was paper-thin, so from behind it read as torn foil. The
DESIGN is kept: Martians/gen/props-refresh/stripped/planter_wet.png, the
approved reference - a raised hydroponic trough on a tubular steel stand,
leafy green plants, a tall panelled back wall with a hood carrying a light bar,
a yellow edge stripe along the trough, a pipe rail along its front lip, and a
control box with a lit screen, keypad and a lit vertical slot at one end.
Only the construction changes: every part is an exact closed primitive.

Layout facts:
  - long axis X (2.5 m: the game fits the larger horizontal extent to 2.5 m,
    so this is real scale - trough rim ~0.95 m, bench height beside 1.6-1.9 m
    crew; hood top 1.63 m). The plants face -Y; the back wall is at +Y.
  - the BACK WALL is one solid slab 10 cm thick (slate core) carrying raised
    bone-white plates on BOTH faces (2 rows x 4 columns), so the wall is
    finished and closed from the front and from behind (the reason for the
    rebuild); bolts on the front plates, a conduit and cable clips on the back.
  - stand: the trough's upper band (full width) rests on the frame's top
    rails; the narrower lower tub hangs between them, as in the reference. Legs
    are slate tubes (r 2.8 cm) with rubber feet, collars at the rail joints.
  - lit elements share ONE glow hue in game (teal): the control-box screen,
    its vertical status slot, and the light bar under the hood front. The bar
    faces forward and down so the 54 deg camera sees its front strip.
  - accent (#E3C548, the family's yellow edge stripe) is TRIM: a stripe ring
    round the trough band, a stripe on the hood fascia and on the control box.
  - plants: 14 leafy rosettes (chard/lettuce-like) in two staggered rows; every
    leaf is a closed cupped solid (rh_agri.broad_leaf) - M_RH_Master is
    one-sided, so a single-plane leaf would vanish from behind.
  - room props are seen from every side (per-cell yaw 0/90/180/270), so there
    is no face_camera and no blank side.
Real-world metres, ground-centred pivot (bounds bottom at z = 0, centred on XY).
"""
import math
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mathutils import Matrix, Vector  # noqa: E402

import rh_agri as A  # noqa: E402
import rh_hs  # noqa: E402

argv, OUT, SIZE, PREVIEW_ONLY = A.cli("Martians/gen/hs/planter_wet")
NAME = "planter_wet"

K = rh_hs.Kit(accent_hex="#E3C548")        # family yellow edge stripe (props addendum)
A.add_screen_role(K)
A.add_role(K, "soil", "#3F3128", rough=0.95, dust=0.0)
PLANT = dict(leaf="#3C7832", leaf_b="#4C8A38", leaf_young="#7DB453", stem="#A6C47C")
for r, h in PLANT.items():
    A.add_role(K, r, h, rough=0.55, dust=0.0)

parts = []
add = parts.append

# ------------------------------------------------------------------ layout
TX, TY = 1.24, 0.38          # trough upper band half extents
TUB_X, TUB_Y = 1.18, 0.30    # lower tub (hangs between the frame's top rails)
Z_TUB0, Z_BAND0, Z_BAND1 = 0.50, 0.78, 0.935
Z_RIM, Z_SOIL = 0.955, 0.918
LX, LY = 1.02, 0.335         # leg centres
LEG_R, RAIL_R = 0.028, 0.022
Z_RAIL_TOP, Z_RAIL_LOW = 0.760, 0.150
CB_X0 = 0.74                 # control box spans x CB_X0..TX
WALL_Y0, WALL_Y1 = 0.255, 0.355
Z_WALL1, Z_HOOD1 = 1.565, 1.630

# --------------------------------------------------------------- the stand
for sx in (-1, 1):
    for sy in (-1, 1):
        x, y = sx * LX, sy * LY
        add(K.cyl("leg", (x, y, 0.035), (x, y, Z_BAND0 + 0.01), LEG_R, "slate", sides=8))
        add(K.cyl("foot", (x, y, 0.0), (x, y, 0.045), LEG_R + 0.008, "rubber", sides=8))
        add(K.cyl("leg_band", (x, y, 0.065), (x, y, 0.125), LEG_R + 0.004, "accent", sides=8))
        for z in (Z_RAIL_TOP, Z_RAIL_LOW):
            add(K.cyl("collar", (x, y, z - 0.035), (x, y, z + 0.035), LEG_R + 0.007, "slate", sides=8))
for sy in (-1, 1):                                   # rails along X
    for z in (Z_RAIL_TOP, Z_RAIL_LOW):
        add(K.cyl("rail_x", (-LX, sy * LY, z), (LX, sy * LY, z), RAIL_R, "slate", sides=8))
for sx in (-1, 1):                                   # rails along Y
    for z in (Z_RAIL_TOP, Z_RAIL_LOW):
        add(K.cyl("rail_y", (sx * LX, -LY, z), (sx * LX, LY, z), RAIL_R, "slate", sides=8))
add(K.cyl("rail_mid", (0.0, -LY, Z_RAIL_LOW), (0.0, LY, Z_RAIL_LOW), RAIL_R, "slate", sides=8))

# -------------------------------------------------------------- the trough
add(A.convex_block(K, "tub", A.oct_planes(-TUB_X, TUB_X, -TUB_Y, TUB_Y, Z_TUB0, Z_BAND0 + 0.03, ch=0.03)
                   + [((0, -A.S2, -A.S2), A.S2 * (TUB_Y + (-Z_TUB0) - 0.11)),
                      ((0, A.S2, -A.S2), A.S2 * (TUB_Y + (-Z_TUB0) - 0.11))], "hull", bevel=0.012))
band_planes = A.oct_planes(-TX, TX, -TY, TY, Z_BAND0, Z_BAND1, ch=0.045)
add(A.convex_block(K, "band", band_planes, "hull", bevel=0.010))
# yellow edge stripe: a thin ring round the band, 4 mm proud
OL = A.oct_outline(TX, TY, 0.045)
add(A.sweep_ring(K, "stripe", OL, [(-0.004, 0.834), (-0.004, 0.884), (0.004, 0.884), (0.004, 0.834)],
                 ["accent"]))
# vertical seam battens on the tub (lower front/back) - panel joints
for x in (-0.60, 0.0, 0.60):
    for sy in (-1, 1):
        ya, yb = sorted((sy * (TUB_Y - 0.004), sy * (TUB_Y + 0.008)))
        add(K.box("batten", (x - 0.02, ya, Z_TUB0 + 0.13), (x + 0.02, yb, Z_BAND0 - 0.004), "hull",
                  bevel=0.004, seg=1))
# rim cap: lip, chamfer up to the flat top, chamfer down to the soil
add(A.sweep_ring(K, "rim", OL, [(-0.008, 0.922), (-0.008, 0.940), (0.012, Z_RIM), (0.050, Z_RIM),
                                 (0.075, 0.905), (0.075, 0.900)], ["hull"]))
add(A.convex_block(K, "soil", A.oct_planes(-TX + 0.06, TX - 0.06, -TY + 0.06, TY - 0.06, 0.87, Z_SOIL,
                                           ch=0.02), "soil", bevel=0.0))
# bolts along the band (front and back), sunk 2 mm
for x in (-1.05, -0.45, 0.15):
    for sy in (-1, 1):
        add(K.bolt("bbolt", (x, sy * (TY - 0.002), 0.908), (0, sy, 0), r=0.010, h=0.008))

# ---------------------------------------- pipe rail along the front lip
PR_Y, PR_Z = -0.325, 1.005
add(K.pipe("pipe_rail", [(-1.15, 0.20, 0.93), (-1.15, 0.20, PR_Z), (-1.15, PR_Y, PR_Z),
                         (CB_X0 - 0.10, PR_Y, PR_Z), (CB_X0 - 0.10, PR_Y, 0.93)],
           0.020, "metal", bend=0.06, sides=8, samples=4))
for x in (-0.55, 0.05):                              # stand-off posts with collars
    add(K.cyl("rail_post", (x, PR_Y, 0.94), (x, PR_Y, PR_Z), 0.013, "metal", sides=8))
    add(K.cyl("rail_clip", (x, PR_Y, PR_Z - 0.03), (x, PR_Y, PR_Z + 0.03), 0.026, "slate", sides=8))

# -------------------------------------------------------------- back wall
# solid 10 cm slab, slate core, raised hull plates on BOTH faces (2 x 4)
WX0, WX1 = -1.21, CB_X0 + 0.02
add(K.box("wall_core", (WX0, WALL_Y0, 0.92), (WX1, WALL_Y1, Z_WALL1), "slate", bevel=0.010))
COLS = 4
pw = (WX1 - 0.02 - WX0 - 0.04) / COLS
rows = ((0.985, 1.255), (1.275, 1.535))
for c in range(COLS):
    x0 = WX0 + 0.03 + c * pw + 0.008
    x1 = x0 + pw - 0.016
    for z0, z1 in rows:
        for face_y, sgn in ((WALL_Y0, -1), (WALL_Y1, 1)):
            ya, yb = sorted((face_y - sgn * 0.004, face_y + sgn * 0.012))
            add(K.box("wall_plate", (x0, ya, z0), (x1, yb, z1), "hull", bevel=0.006, seg=1))
        # front plates: one bolt top-centre
        add(K.bolt("pbolt", ((x0 + x1) / 2, WALL_Y0 - 0.010, z1 - 0.03), (0, -1, 0), r=0.010, h=0.008))
# hull end cap over the wall's open (-X) end
add(K.box("wall_cap", (WX0 - 0.012, WALL_Y0 - 0.006, 0.93), (WX0 + 0.004, WALL_Y1 + 0.006, Z_WALL1 - 0.004),
          "hull", bevel=0.005, seg=1))
# back face furniture: a conduit with clips running the wall's length
add(K.cyl("conduit", (WX0 + 0.05, WALL_Y1 + 0.045, 1.265), (WX1 - 0.05, WALL_Y1 + 0.045, 1.265), 0.022,
          "pipe", sides=8))
for x in (-0.95, -0.25, 0.45):
    add(K.box("clip", (x - 0.03, WALL_Y1 + 0.005, 1.230), (x + 0.03, WALL_Y1 + 0.078, 1.300), "slate",
              bevel=0.006, seg=1))

# hood: canopy over the trough, yellow fascia stripe, light bar under its front
HY0 = 0.13                                   # hood front edge (25 cm deep: a shallow bar)
add(A.convex_block(K, "hood", A.oct_planes(WX0 - 0.02, CB_X0, HY0, WALL_Y1 + 0.03, Z_WALL1 - 0.005,
                                           Z_HOOD1, ch=0.0)
                   + [((0, -A.S2, -A.S2), A.S2 * (-HY0 + -(Z_WALL1 - 0.005) - 0.025))], "hull",
                   bevel=0.008))
add(K.box("hood_stripe", (WX0, HY0 - 0.004, Z_HOOD1 - 0.046), (CB_X0 - 0.02, HY0 + 0.006, Z_HOOD1 - 0.012),
          "accent", bevel=0.002, seg=1))
# light-bar housing runs from under the hood front back into the wall (it is
# the hood's support); the lit bar faces forward and down
add(K.box("lightbar_housing", (WX0 + 0.08, HY0 + 0.03, Z_WALL1 - 0.060), (CB_X0 - 0.10, WALL_Y0 + 0.004,
                                                                         Z_WALL1 + 0.004),
          "slate", bevel=0.006, seg=1))
add(K.box("lightbar", (WX0 + 0.11, HY0 + 0.026, Z_WALL1 - 0.066), (CB_X0 - 0.13, HY0 + 0.095,
                                                                  Z_WALL1 - 0.030),
          "screen", bevel=0.003, seg=1))

# ------------------------------------------------------------ control box
CBZ0, CBZ1, CBY0, CBY1 = 0.92, 1.36, -TY + 0.02, TY - 0.005
CH_F = 0.17                                          # front-top chamfer (screen face)
add(A.convex_block(K, "cbox", A.oct_planes(CB_X0, TX - 0.005, CBY0, CBY1, CBZ0, CBZ1, ch=0.03)
                   + [((0, -A.S2, A.S2), A.S2 * (-CBY0 + CBZ1 - CH_F))], "hull", bevel=0.012))
# screen on the chamfer: slate bezel + lit cell, built facing -Y then tilted up 45 deg
sc = []
bz, wy = A.bezel(K, "scr_bezel", (0.0, 0.003, 0.0), 0.30, 0.16, 0.018, radius=0.03, rim=0.022,
                 recess=0.008, rim_role="slate", inner_role="slate")
sc.append(bz)
sc.append(K.box("scr", (-0.118, wy - 0.004, -0.050), (0.118, wy + 0.002, 0.050), "screen", bevel=0.003,
                seg=1))
cx = (CB_X0 + TX) / 2
ch_mid = Vector((cx, CBY0 + CH_F / 2, CBZ1 - CH_F / 2))
A.place(sc, Matrix.Translation(ch_mid) @ Matrix.Rotation(math.radians(-45.0), 4, "X"))
parts += sc
# front face: keypad (slate plate, 2 x 4 buttons), lit vertical slot, yellow ID strip
FY = CBY0
add(K.box("keypad", (cx - 0.17, FY - 0.012, 1.10), (cx + 0.02, FY + 0.004, 1.17), "slate", bevel=0.005,
          seg=1))
for i in range(4):
    for j in range(2):
        bx = cx - 0.15 + i * 0.045
        bzz = 1.118 + j * 0.034
        add(K.box("key", (bx, FY - 0.020, bzz), (bx + 0.03, FY - 0.008, bzz + 0.022), "metal", bevel=0.0))
slot, swy = A.bezel(K, "slot_bezel", (cx + 0.15, FY + 0.003, 1.08), 0.07, 0.22, 0.016, radius=0.025,
                    rim=0.016, recess=0.006, rim_role="slate", inner_role="slate")
add(slot)
add(K.box("slot", (cx + 0.15 - 0.016, swy - 0.004, 1.08 - 0.085), (cx + 0.15 + 0.016, swy + 0.002, 1.08 + 0.085),
          "screen", bevel=0.003, seg=1))
x0b, x1b = CB_X0, TX - 0.005
add(A.sweep_ring(K, "cb_stripe", [(x1b, CBY0 + 0.03), (x1b, CBY1 - 0.03), (x1b - 0.03, CBY1),
                                  (x0b + 0.03, CBY1), (x0b, CBY1 - 0.03), (x0b, CBY0 + 0.03),
                                  (x0b + 0.03, CBY0), (x1b - 0.03, CBY0)],
                 [(-0.004, 0.972), (-0.004, 1.012), (0.004, 1.012), (0.004, 0.972)], ["accent"]))
# end face (+X): louvre vent in a slate frame
lv = []
lb, lwy = A.bezel(K, "vent_frame", (0.0, 0.003, 0.0), 0.40, 0.22, 0.020, radius=0.03, rim=0.022,
                  recess=0.012, rim_role="slate", inner_role="rubber")
lv.append(lb)
for k in range(5):
    z = -0.075 + k * 0.0375
    lv.append(K.box("slat", (-0.168, lwy - 0.010, z - 0.009), (0.168, lwy + 0.004, z + 0.009), "hull",
                    bevel=0.003, seg=1))
A.place(lv, Matrix.Translation((TX - 0.005, 0.0, 1.15)) @ Matrix.Rotation(math.radians(90.0), 4, "Z"))
parts += lv
# back face (+Y): service hatch with bolts
hb = []
hz, _ = A.bezel(K, "hatch", (0.0, 0.003, 0.0), 0.36, 0.26, 0.012, radius=0.035, rim=0.024, recess=0.006,
                rim_role="slate", inner_role="hull")
hb.append(hz)
A.place(hb, Matrix.Translation((cx, CBY1, 1.13)) @ Matrix.Rotation(math.radians(180.0), 4, "Z"))
parts += hb
# inner face (-X, toward the plants): two bolts on a slate cable gland
add(K.cyl("gland", (CB_X0 + 0.004, -0.10, 1.05), (CB_X0 - 0.020, -0.10, 1.05), 0.035, "slate", sides=10))
add(K.cyl("gland_nut", (CB_X0 - 0.018, -0.10, 1.05), (CB_X0 - 0.034, -0.10, 1.05), 0.024, "metal", sides=6))

# ------------------------------------------------------------------ plants
rng = random.Random(20261014)
soup = A.Soup(list(PLANT))
X_MIN, X_MAX = -TX + 0.03, CB_X0 - 0.02
Y_MIN, Y_MAX = -TY + 0.01, WALL_Y0 - 0.015


def dirv(az, el):
    return Vector((math.cos(az) * math.cos(el), math.sin(az) * math.cos(el), math.sin(el)))


def fits(pts):
    return all(X_MIN <= p.x <= X_MAX and Y_MIN <= p.y <= Y_MAX for p in pts)


n_leaves = 0
ROWS = ((-0.165, [-1.05 + 0.26 * k for k in range(7)]), (0.070, [-0.92 + 0.26 * k for k in range(7)]))
for ry, xs in ROWS:
    for x in xs:
        c = Vector((x + rng.uniform(-0.03, 0.03), ry + rng.uniform(-0.02, 0.02), Z_SOIL))
        size = rng.uniform(0.95, 1.18)
        # crown: pale stem stub the leaves grow from, sunk into the soil
        soup.tube([[c + Vector((0.032 * math.cos(a), 0.032 * math.sin(a), -0.02))
                    for a in (2 * math.pi * k / 6 for k in range(6))],
                   [c + Vector((0.026 * math.cos(a), 0.026 * math.sin(a), 0.045))
                    for a in (2 * math.pi * k / 6 for k in range(6))]], ["stem"])
        nl = rng.choice((9, 10, 10, 11))
        az0 = rng.uniform(0, 2 * math.pi)
        placed = []                                   # (az, el) of this rosette's leaves
        for i in range(nl):
            inner = i < 3
            az = az0 + (i * 2.39996) + rng.uniform(-0.2, 0.2)      # golden-angle spiral
            el = math.radians(rng.uniform(68, 80) if inner else rng.uniform(32, 55))
            L = size * (rng.uniform(0.18, 0.22) if inner else rng.uniform(0.23, 0.30))
            W = size * (rng.uniform(0.036, 0.044) if inner else rng.uniform(0.045, 0.058))
            droop = math.radians(rng.uniform(35, 55))
            ok = False
            for attempt in range(10):
                # never two leaves of one rosette on (nearly) the same heading:
                # identical shapes there overlap coplanar (z-fight)
                while any(abs(((az - a2 + math.pi) % (2 * math.pi)) - math.pi) < math.radians(18)
                          for a2, e2 in placed):
                    az += math.radians(19)
                # each leaf roots 1 cm out along its heading at its own height, so
                # no two petiole bases share a point (their faces met coplanar)
                p0 = c + Vector((0.010 * math.cos(az), 0.010 * math.sin(az), 0.022 + 0.0025 * i))
                P1 = p0 + dirv(az, el) * L * 0.35
                P2 = P1 + dirv(az, el * 0.6) * L * 0.35
                P3 = P2 + dirv(az, el * 0.6 - droop) * L * 0.32
                pts = A.bezier(p0, P1, P2, P3, 4)
                if fits(pts):
                    ok = True
                    break
                # too long for the trough here: shorten first, then turn in 25 deg steps
                if attempt % 2 == 0:
                    L *= 0.88
                else:
                    az += math.radians(25)
            if not ok:
                continue
            placed.append((az, el))
            hw = [0.010, W * 0.62, W, W * 0.66, 0.0]
            role = "leaf_young" if inner else ("leaf" if rng.random() < 0.55 else "leaf_b")
            A.broad_leaf(soup, pts, hw, cup=0.22, thick=0.005, roles=["stem"] + [role] * 3)
            n_leaves += 1
rh_hs.log("planted 14 rosettes, %d leaves" % n_leaves)
parts.append(soup.obj(K, "plants"))

# ------------------------------------------------------------------ finish
ob = K.finish(parts, NAME)
A.recentre(ob)
A.role_shares(ob)
A.export(K, ob, NAME, OUT, PREVIEW_ONLY, SIZE, wear=0.18, dust=0.12)
