"""Build the tank from code - the room-prop remake (2026-10-14).

  blender -b -P scripts/blender/rh_build_tank.py -- [--out Martians/gen/hs/tank]
      [--size 2048] [--preview-only]

The fluid-works furnishing (RHColonyVisualizerSubsystem::RoomPropPath:
WaterWorks, Septic). Replaces the image-to-3D tank the director marked "just
needs a little polish" on the 2026-10-13 triage: a lumpy shell with a melted,
rust-blotched open end and a valve that had fused into a knot. The DESIGN is
kept: Martians/gen/props-refresh/stripped/tank.png, the approved reference - a
horizontal bone-white pressure tank with domed ends cradled in a tubular steel
frame, raised hoop bands (yellow straps among them), panel seams, and at one
end a valve manifold with two handwheels, a pipe looping in from the dome top
and a gauge box with a small lit status screen. Only the construction changes:
every part is an exact closed primitive, and the weathering is light (wear
0.12): clean and crisp, the brief's "light weathering only".

Layout facts:
  - long axis X (2.37 m; the game fits the larger horizontal extent to 2.5 m,
    so the scale is ~1.05 and the human-scale cues - handwheels at ~0.8-0.9 m,
    a 0.94 m diameter shell on a knee-high cradle - stay true beside crew).
  - shell: a 0.47 m radius cylinder with 2:1 ellipsoidal domes, one surface
    of revolution (no seams to open). Two light-alloy weld rings where the
    domes meet the cylinder, two yellow strap bands (accent) with tensioner
    blocks, two longitudinal seam strips with rivets.
  - the tank RESTS on two slate saddles whose tops follow the shell (sunk
    4 mm into it), each sitting on a cross rail between a leg pair - nothing
    floats. Frame: 4 slate legs with rubber feet, top and bottom side rails.
  - valve end (+X): manifold column, a pipe from the dome-top nozzle, a pipe
    from the dome-bottom outlet, two handwheels (one facing out along +X, one
    to the side), a gauge box with the lit screen and a round dial. Other end
    (-X): a bolted manway hatch with a grab handle and a drain valve, so every
    side is finished (room props are seen from all four yaws; no face_camera).
  - top: a relief valve and two lifting lugs, so the shell does not read as a
    blank capsule from the game's 54 deg camera.
  - lit: the status screen and a status lamp beside it (one glow hue, teal).
  - accent (#E3C548, the family yellow) is TRIM: the two strap bands and the
    manway's hazard ring.
Real-world metres, ground-centred pivot (bounds bottom at z = 0, centred on XY).
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mathutils import Matrix, Vector  # noqa: E402

import rh_agri as A  # noqa: E402
import rh_hs  # noqa: E402

argv, OUT, SIZE, PREVIEW_ONLY = A.cli("Martians/gen/hs/tank")
NAME = "tank"

K = rh_hs.Kit(accent_hex="#E3C548")        # family yellow (props addendum)
A.add_screen_role(K)
parts = []
add = parts.append

R, ZC = 0.47, 0.86                         # shell radius, axis height
XC0, XC1, D = -0.86, 0.66, 0.25            # cylinder ends, dome depth (2:1 ellipsoid ~ R/2)
AX = (1.0, 0.0, 0.0)
O = (0.0, 0.0, ZC)
LXS, LY = (-0.70, 0.52), 0.52              # leg x positions, leg y (= saddle positions)


def on_axis(x, ang, rr):
    """Point on a circle of radius rr round the tank axis at azimuth ang
    (0 = top, +90 deg = toward +Y)."""
    return Vector((x, rr * math.sin(ang), ZC + rr * math.cos(ang)))


# -------------------------------------------------------------------- shell
prof = [(0.0, XC0 - D)]
for k in range(1, 7):
    th = math.radians(15.0 * k)
    prof.append((R * math.sin(th), XC0 - D * math.cos(th)))
for k in range(6, 0, -1):
    th = math.radians(15.0 * k)
    prof.append((R * math.sin(th), XC1 + D * math.cos(th)))
prof.append((0.0, XC1 + D))
add(A.revolve(K, "shell", prof, ["hull"], sides=40, origin=O, axis=AX, closed=False))


def band(name, x, w, proud, role, sides=40):
    h = w / 2
    return A.revolve(K, name, [(R - 0.006, x - h), (R + proud - 0.004, x - h), (R + proud, x - h + 0.005),
                               (R + proud, x + h - 0.005), (R + proud - 0.004, x + h), (R - 0.006, x + h)],
                     [role], sides=sides, origin=O, axis=AX)


# weld rings where the domes meet the cylinder (light alloy), yellow straps
for x in (XC0 + 0.035, XC1 - 0.035):
    add(band("weld_ring", x, 0.055, 0.016, "metal"))
STRAPS = (-0.36, 0.20)
for i, x in enumerate(STRAPS):
    add(band("strap", x, 0.090, 0.014, "accent"))
    # tensioner block on the strap's lower flank (alternate sides)
    ang = math.radians(-118.0 if i == 0 else 118.0)
    c = on_axis(x, ang, R + 0.030)
    blk = K.box("tensioner", (-0.065, -0.024, -0.030), (0.065, 0.024, 0.030), "slate", bevel=0.006, seg=1)
    blk.data.transform(Matrix.Translation(c) @ Matrix.Rotation(-ang, 4, "X"))
    add(blk)
    for dx in (-0.04, 0.04):
        add(K.cyl("tbolt", on_axis(x + dx, ang, R + 0.050), on_axis(x + dx, ang, R + 0.064), 0.011, "metal",
                  sides=6))

# longitudinal seam strips (5 mm proud) with rivets, at +-42 deg from the top
for ang_d in (-42.0, 42.0):
    ang = math.radians(ang_d)
    strip = K.box("seam", (XC0 + 0.07, -0.0125, R - 0.004), (XC1 - 0.07, 0.0125, R + 0.005), "hull",
                  bevel=0.002, seg=1)
    strip.data.transform(Matrix.Translation((0, 0, ZC)) @ Matrix.Rotation(-ang, 4, "X"))
    add(strip)
    for x in (-0.72, -0.56, -0.16, 0.00, 0.38, 0.52):
        add(K.cyl("rivet", on_axis(x, ang, R + 0.002), on_axis(x, ang, R + 0.011), 0.008, "metal", sides=6))

# top fittings for the top-down camera: a relief valve and two lifting lugs
add(K.cyl("relief_base", (-0.08, 0, ZC + R - 0.02), (-0.08, 0, ZC + R + 0.035), 0.050, "slate", sides=10))
add(K.cyl("relief_body", (-0.08, 0, ZC + R + 0.03), (-0.08, 0, ZC + R + 0.11), 0.030, "metal", sides=10))
add(K.cyl("relief_cap", (-0.08, 0, ZC + R + 0.105), (-0.08, 0, ZC + R + 0.135), 0.042, "slate", sides=10))
for x in (-0.62, 0.42):
    add(K.box("lug", (x - 0.05, -0.012, ZC + R - 0.02), (x + 0.05, 0.012, ZC + R + 0.075), "slate", bevel=0.006,
              seg=1, chamfers=[(lambda d, m: abs(d.y) > 0.99 and m.z > ZC + R + 0.07, 0.035)]))
    add(K.cyl("lug_eye", (x, -0.016, ZC + R + 0.040), (x, 0.016, ZC + R + 0.040), 0.018, "metal", sides=8))

# --------------------------------------------------------------- the cradle
for x in LXS:
    # saddle: a curved slate band hugging the shell's underside (inner radius
    # R - 4 mm, sunk in) across +-62 deg, on three posts to the cross rail.
    # Open, like the reference's tubular cradle (round 1's solid plates read
    # as dark blocks under the tank).
    poly = []
    n = 12
    a_lim = math.radians(62.0)
    for k in range(n + 1):                                  # outer arc, -a .. +a
        a = -a_lim + 2 * a_lim * k / n
        poly.append(((R + 0.045) * math.sin(a), ZC - (R + 0.045) * math.cos(a)))
    for k in range(n, -1, -1):                              # inner arc back
        a = -a_lim + 2 * a_lim * k / n
        poly.append(((R - 0.004) * math.sin(a), ZC - (R - 0.004) * math.cos(a)))
    add(A.prism(K, "saddle", poly, x - 0.045, x + 0.045, "slate", axis="X"))
    for y in (-0.30, 0.0, 0.30):
        z_top = ZC - math.sqrt((R + 0.03) ** 2 - y * y)
        add(K.box("saddle_post", (x - 0.03, y - 0.03, 0.235), (x + 0.03, y + 0.03, z_top), "slate", bevel=0.006,
                  seg=1))
    # arms from the band tips out to the top side rails
    for sy in (-1, 1):
        tip = ((R + 0.02) * math.sin(a_lim), ZC - (R + 0.02) * math.cos(a_lim))
        ya, yb = sorted((sy * (tip[0] - 0.02), sy * (LY + 0.005)))
        add(K.box("saddle_arm", (x - 0.03, ya, 0.600), (x + 0.03, yb, 0.640), "slate", bevel=0.006, seg=1))
for x in LXS:
    for sy in (-1, 1):
        y = sy * LY
        add(K.cyl("leg", (x, y, 0.035), (x, y, 0.655), 0.026, "slate", sides=8))
        add(K.cyl("foot", (x, y, 0.0), (x, y, 0.045), 0.034, "rubber", sides=8))
        add(K.cyl("leg_cap", (x, y, 0.645), (x, y, 0.668), 0.031, "slate", sides=8))
    add(K.cyl("cross_rail", (x, -LY, 0.245), (x, LY, 0.245), 0.024, "slate", sides=8))
for sy in (-1, 1):
    for z in (0.62, 0.12):
        add(K.cyl("side_rail", (LXS[0], sy * LY, z), (LXS[1], sy * LY, z), 0.022, "slate", sides=8))

# ------------------------------------------------------------ valve end (+X)
MX = 1.08                                   # manifold column x
XN = 0.80                                   # dome-top nozzle / bottom outlet x
dome_r = lambda x: R * math.sqrt(max(0.0, 1 - ((x - XC1) / D) ** 2))  # noqa: E731
ZT, ZB = ZC + dome_r(XN), ZC - dome_r(XN)
add(K.cyl("nozzle", (XN, 0, ZT - 0.06), (XN, 0, ZT + 0.07), 0.045, "metal", sides=10))
add(K.cyl("nozzle_flange", (XN, 0, ZT + 0.055), (XN, 0, ZT + 0.085), 0.068, "slate", sides=10))
add(K.pipe("top_pipe", [(XN, 0, ZT + 0.08), (XN, 0, ZT + 0.15), (MX, 0, ZT + 0.15), (MX, 0, 1.05)],
           0.035, "hull", bend=0.07, sides=8, samples=4))
add(K.cyl("outlet", (XN, 0, ZB + 0.06), (XN, 0, ZB - 0.05), 0.040, "metal", sides=10))
add(K.pipe("bottom_pipe", [(XN, 0, ZB - 0.04), (XN, 0, 0.34), (MX, 0, 0.34), (MX, 0, 0.72)], 0.035, "hull",
           bend=0.07, sides=8, samples=4))
add(K.cyl("manifold", (MX, 0, 0.70), (MX, 0, 1.06), 0.070, "slate", sides=12))
for z in (0.72, 1.04):
    add(K.cyl("man_flange", (MX, 0, z - 0.018), (MX, 0, z + 0.018), 0.092, "metal", sides=12))
# valve bodies + handwheels: one facing out along +X, one to the side (-Y)
add(K.cyl("valve_x", (MX, 0, 0.82), (MX + 0.12, 0, 0.82), 0.040, "metal", sides=10))
add(K.cyl("stem_x", (MX + 0.10, 0, 0.82), (MX + 0.16, 0, 0.82), 0.014, "metal", sides=6))
parts += A.handwheel(K, "wheel_x", (MX + 0.16, 0, 0.82), (1, 0, 0), 0.095, "slate", "metal")
add(K.cyl("valve_y", (MX, 0, 0.93), (MX, -0.12, 0.93), 0.038, "metal", sides=10))
add(K.cyl("stem_y", (MX, -0.10, 0.93), (MX, -0.16, 0.93), 0.014, "metal", sides=6))
parts += A.handwheel(K, "wheel_y", (MX, -0.16, 0.93), (0, -1, 0), 0.080, "slate", "metal")
# gauge box on the manifold (+Y side), lit status screen facing out (+X), dial facing +Y
GB = (MX - 0.06, MX + 0.07, 0.04, 0.20, 0.86, 1.02)
add(K.box("gauge_box", (GB[0], GB[2], GB[4]), (GB[1], GB[3], GB[5]), "hull", bevel=0.010))
add(K.box("gauge_arm", (MX - 0.03, 0.03, 0.91), (MX + 0.03, GB[2] + 0.01, 0.97), "slate", bevel=0.005, seg=1))
scr = []
bz, wy = A.bezel(K, "scr_bezel", (0.0, 0.003, 0.0), 0.13, 0.10, 0.012, radius=0.018, rim=0.012, recess=0.005,
                 rim_role="slate", inner_role="slate")
scr.append(bz)
scr.append(K.box("scr", (-0.048, wy - 0.004, -0.031), (0.048, wy + 0.002, 0.031), "screen", bevel=0.002,
                 seg=1))
A.place(scr, Matrix.Translation((GB[1], (GB[2] + GB[3]) / 2, 0.955)) @ Matrix.Rotation(math.radians(90), 4, "Z"))
parts += scr
add(K.cyl("lamp", (GB[1] - 0.004, (GB[2] + GB[3]) / 2 + 0.0, 0.885), (GB[1] + 0.008, (GB[2] + GB[3]) / 2, 0.885),
          0.010, "screen", sides=8))
# round dial on the gauge box's +Y face: bezel ring, glass face, needle
dial_c = Vector(((GB[0] + GB[1]) / 2, GB[3], 0.94))
add(A.torus(K, "dial_ring", dial_c + Vector((0, 0.004, 0)), (0, 1, 0), 0.042, 0.009, "metal", major=20, minor=6))
add(K.cyl("dial_face", dial_c - Vector((0, 0.004, 0)), dial_c + Vector((0, 0.006, 0)), 0.040, "glass", sides=20))
nd = K.box("needle", (-0.003, 0.0, -0.002), (0.003, 0.004, 0.030), "accent", bevel=0.0)
nd.data.transform(Matrix.Translation(dial_c + Vector((0, 0.006, 0))) @ Matrix.Rotation(math.radians(-35), 4, "Y"))
add(nd)

# ---------------------------------------------------------- manway end (-X)
XP = XC0 - D                                 # dome pole x
add(K.cyl("manway", (XP + 0.045, 0, ZC), (XP - 0.035, 0, ZC), 0.20, "hull", sides=24))
add(A.revolve(K, "manway_ring", [(0.150, XP - 0.030), (0.205, XP - 0.030), (0.205, XP - 0.044),
                                  (0.150, XP - 0.044)], ["accent"], sides=24, origin=(0, 0, ZC), axis=(1, 0, 0)))
for k in range(8):
    a = 2 * math.pi * k / 8 + math.pi / 8
    p = Vector((XP - 0.035, 0.178 * math.sin(a), ZC + 0.178 * math.cos(a)))
    add(K.cyl("mbolt", p + Vector((0.004, 0, 0)), p - Vector((0.014, 0, 0)), 0.012, "metal", sides=6))
add(K.pipe("manway_handle", [(XP - 0.02, -0.07, ZC + 0.02), (XP - 0.09, -0.07, ZC + 0.02),
                             (XP - 0.09, 0.07, ZC + 0.02), (XP - 0.02, 0.07, ZC + 0.02)], 0.014, "metal",
           bend=0.03, sides=8, samples=3))
# drain valve under the -X dome
XD = XC0 - 0.12
ZD = ZC - R * math.sqrt(max(0.0, 1 - ((XD - XC0) / D) ** 2))
add(K.cyl("drain", (XD, 0, ZD + 0.05), (XD, 0, ZD - 0.10), 0.030, "metal", sides=8))
add(K.cyl("drain_valve", (XD, -0.045, ZD - 0.075), (XD, 0.045, ZD - 0.075), 0.032, "slate", sides=8))
parts += A.handwheel(K, "wheel_d", (XD, -0.06, ZD - 0.075), (0, -1, 0), 0.045, "slate", "metal", rim_r=0.010)

# ------------------------------------------------------------------ finish
ob = K.finish(parts, NAME)
A.recentre(ob)
A.role_shares(ob)
A.export(K, ob, NAME, OUT, PREVIEW_ONLY, SIZE, wear=0.12, dust=0.12)
