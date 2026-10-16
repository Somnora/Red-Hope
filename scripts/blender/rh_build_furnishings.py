"""Build the BM-5 furniture catalogue's new pieces from code (2026-10-16).

  blender -b -P scripts/blender/rh_build_furnishings.py -- --piece <name>
      [--out Martians/gen/hs/furn_<name>] [--size 1024] [--preview-only]

  pieces: chair desk bench sofa lamp rug cot bunkbed plant

The furniture catalogue (build mode BM-4/5, director ruling 2026-10-15) needs
pieces the colony never had: the seats, beds and comfort items the player
places by hand. The room props the game already drops (bunk, lab bench,
console, dining table, locker) stay as they are; these are the new ones.

They join the tier-room bench family (scripts/blender/rh_furniture.py) so a
furnished room reads as one kit: round pipe legs with clamp collars and
rubber feet, BoneWhite (hull) tops and frames whose edges carry the thin
yellow stripe, DarkSlate pads and panels, small cyan screens. Soft goods get
their own matte roles (upholstery, linen, blanket, rug wool).

UNLIKE the room props, furniture is placed at TRUE SCALE: the game does not
fit it to a 2.5 m cell; a chair is a chair. Every piece is built in metres,
long axis X, front at -Y, bounds centred on XY with the bottom at z = 0, and
finished on every side (pieces turn 0/90/180/270 deg and are seen from ~54
deg above). Its footprint must fit its catalogue slots (2.5 m sub-cells).
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bpy  # noqa: E402,F401
from mathutils import Matrix, Vector  # noqa: E402

import rh_furniture as F  # noqa: E402

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
PIECE = argv[argv.index("--piece") + 1] if "--piece" in argv else "chair"
OUT, SIZE, PREVIEW_ONLY = F.args("Martians/gen/hs/furn_%s" % PIECE)
if "--size" not in argv:
    SIZE = 1024                      # small pieces: 1K is the family's texel density
NAME = "furn_%s" % PIECE

K = F.make_kit()
F.add_role(K, "upholstery", dict(albedo="#58737A", metal=0.0, rough=0.90, dust=0.5))
F.add_role(K, "fabric", dict(albedo="#8D939B", metal=0.0, rough=0.90, dust=0.5))   # mattress
F.add_role(K, "linen", dict(albedo="#CDCEC8", metal=0.0, rough=0.85, dust=0.4))
F.add_role(K, "blanket", dict(albedo="#6F7782", metal=0.0, rough=0.92, dust=0.5))
F.add_role(K, "rugwool", dict(albedo="#7C5A46", metal=0.0, rough=0.95, dust=0.6))
F.add_role(K, "rugtrim", dict(albedo="#C8B07C", metal=0.0, rough=0.92, dust=0.5))
F.add_role(K, "soil", dict(albedo="#4A3628", metal=0.0, rough=0.95, dust=0.3))
F.add_role(K, "leaf", dict(albedo="#5D8C47", metal=0.0, rough=0.70, dust=0.2))
SINK = F.SINK
parts = []
add = parts.append


# ------------------------------------------------------------------ helpers
def stripe(out, name, x0, x1, y0, y1, z0, z1, sides=(1, 1, 1, 1)):
    """The family's yellow edge stripe: a thin proud band round a slab's
    sides, in the upper part of its edge (front, back, left, right)."""
    za, zb = z1 - (z1 - z0) * 0.62, z1 - (z1 - z0) * 0.22
    p = 0.0025
    if sides[0]:
        out.append(K.box(name + "_sf", (x0 + 0.008, y0 - p, za), (x1 - 0.008, y0 + SINK, zb), "accent", bevel=0.0, seg=1))
    if sides[1]:
        out.append(K.box(name + "_sb", (x0 + 0.008, y1 - SINK, za), (x1 - 0.008, y1 + p, zb), "accent", bevel=0.0, seg=1))
    if sides[2]:
        out.append(K.box(name + "_sl", (x0 - p, y0 + 0.008, za), (x0 + SINK, y1 - 0.008, zb), "accent", bevel=0.0, seg=1))
    if sides[3]:
        out.append(K.box(name + "_sr", (x1 - SINK, y0 + 0.008, za), (x1 + p, y1 - 0.008, zb), "accent", bevel=0.0, seg=1))


def post(out, name, x, y, z1, r=0.016, collars=()):
    """A pipe leg with a rubber foot and clamp collars at the given heights."""
    out.append(K.cyl(name, (x, y, 0.018), (x, y, z1), r, "pipe", sides=12))
    out.append(K.cyl(name + "_ft", (x, y, 0.0), (x, y, 0.022), r + 0.006, "rubber", sides=12))
    for i, z in enumerate(collars):
        out.append(K.cyl("%s_c%d" % (name, i), (x, y, z - 0.013), (x, y, z + 0.013), r + 0.006, "metal", sides=12))


def rail(out, name, p0, p1, r=0.011):
    out.append(K.cyl(name, p0, p1, r, "pipe", sides=10))


def soft(out, name, lo, hi, role, bevel=0.03):
    out.append(K.box(name, lo, hi, role, bevel=bevel, seg=3))


# ------------------------------------------------------------------- pieces
def chair():
    """Seat 0.47 m square at 0.46 m, back to 0.88 m; front at -Y."""
    P = []
    S = 0.20
    for i, (x, y) in enumerate(((-S, -S), (S, -S), (-S, S), (S, S))):
        post(P, "leg%d" % i, x, y, 0.88 if y > 0 else 0.425, r=0.015, collars=(0.41,))
    for i, x in enumerate((-S, S)):
        rail(P, "rail%d" % i, (x, -S, 0.15), (x, S, 0.15))
    rail(P, "railb", (-S, S, 0.15), (S, S, 0.15))
    P.append(K.box("seat", (-0.235, -0.235, 0.425), (0.235, 0.235, 0.455), "hull", bevel=0.006, seg=1))
    stripe(P, "seat", -0.235, 0.235, -0.235, 0.235, 0.425, 0.455)
    soft(P, "cush", (-0.215, -0.222, 0.455), (0.215, 0.205, 0.497), "slate", bevel=0.017)
    P.append(K.box("back", (-0.228, 0.188, 0.58), (0.228, 0.222, 0.872), "hull", bevel=0.008, seg=1))
    stripe(P, "back", -0.228, 0.228, 0.188, 0.222, 0.58, 0.872, sides=(0, 1, 1, 1))
    soft(P, "backpad", (-0.195, 0.162, 0.61), (0.195, 0.19, 0.845), "slate", bevel=0.012)
    return P


def desk():
    """Work desk 1.40 x 0.70 at 0.75 m with a drawer pedestal, a monitor and
    its chair tucked in (one piece = one work seat). Front at -Y."""
    L, D, TH = 1.40, 0.70, 0.75
    x0, x1, y0, y1 = -L / 2, L / 2, -D / 2, D / 2
    add(K.box("top", (x0, y0, TH - 0.04), (x1, y1, TH), "hull", bevel=0.006, seg=1))
    stripe(parts, "top", x0, x1, y0, y1, TH - 0.04, TH)
    lx = x0 + 0.07
    for i, y in enumerate((y0 + 0.07, y1 - 0.07)):
        post(parts, "leg%d" % i, lx, y, TH - 0.04, r=0.017, collars=(TH - 0.075,))
    rail(parts, "rail", (lx, y0 + 0.07, 0.16), (lx, y1 - 0.07, 0.16), r=0.012)
    add(K.box("modesty", (lx + 0.03, y1 - 0.055, 0.30), (0.27, y1 - 0.03, TH - 0.045), "slate", bevel=0.004, seg=1))
    px0, px1 = 0.28, x1 - 0.03
    add(K.box("ped", (px0, y0 + 0.045, 0.035), (px1, y1 - 0.035, TH - 0.04), "slate", bevel=0.006, seg=1))
    add(K.box("pedfoot", (px0 + 0.02, y0 + 0.06, 0.0), (px1 - 0.02, y1 - 0.05, 0.037), "rubber", bevel=0.004, seg=1))
    zs = (0.06, 0.27, 0.47, 0.68)
    for i in range(3):
        F.drawer_front(K, parts, "drw%d" % i, px0 + 0.03, px1 - 0.03, zs[i] + 0.012, zs[i + 1] - 0.012, y0 + 0.045)
    # monitor on a stem, dark glass with lit data rows
    mx, my = -0.20, y1 - 0.16
    add(K.box("mbase", (mx - 0.12, my - 0.07, TH), (mx + 0.12, my + 0.07, TH + 0.016), "slate", bevel=0.004, seg=1))
    add(K.cyl("mstem", (mx, my + 0.02, TH + 0.016), (mx, my + 0.02, TH + 0.22), 0.018, "metal"))
    add(K.box("mon", (mx - 0.30, my - 0.03, TH + 0.15), (mx + 0.30, my + 0.03, TH + 0.50), "slate", bevel=0.008, seg=1))
    gy = my - 0.03
    add(K.box("glass", (mx - 0.28, gy - 0.004, TH + 0.17), (mx + 0.28, gy + SINK, TH + 0.48), "glass", bevel=0.002, seg=1))
    for i, (w, z) in enumerate(((0.40, 0.44), (0.30, 0.39), (0.44, 0.34), (0.22, 0.29), (0.36, 0.24))):
        add(K.box("row%d" % i, (mx - 0.25, gy - 0.007, TH + z - 0.008), (mx - 0.25 + w, gy - 0.003, TH + z + 0.008), "screen", bevel=0.0, seg=1))
    add(K.box("kbd", (mx - 0.22, y0 + 0.10, TH), (mx + 0.22, y0 + 0.25, TH + 0.016), "slate", bevel=0.004, seg=1))
    add(K.box("keys", (mx - 0.20, y0 + 0.115, TH + 0.012), (mx + 0.20, y0 + 0.235, TH + 0.021), "metal", bevel=0.002, seg=1))
    add(K.box("pad", (mx + 0.28, y0 + 0.12, TH), (mx + 0.36, y0 + 0.24, TH + 0.012), "cell_off", bevel=0.003, seg=1))
    # its chair, tucked in and facing the desk (+Y)
    C = chair()
    F.place(C, Matrix.Translation(Vector((mx, y0 - 0.16, 0.0))) @ Matrix.Rotation(math.pi, 4, "Z"))
    parts.extend(C)


def bench():
    """Two-seat bench 1.50 x 0.40, seat at 0.45 m: slate slats on a hull frame."""
    L, D, SH = 1.50, 0.40, 0.45
    for i, x in enumerate((-0.66, 0.66)):
        for j, y in enumerate((-0.14, 0.14)):
            post(parts, "leg%d%d" % (i, j), x, y, SH - 0.05, r=0.016, collars=(SH - 0.08,))
        rail(parts, "end%d" % i, (x, -0.14, 0.14), (x, 0.14, 0.14))
    rail(parts, "stretch", (-0.66, 0.0, 0.14), (0.66, 0.0, 0.14), r=0.013)
    add(K.cyl("stretch_y", (0.0, -0.14, 0.14), (0.0, 0.14, 0.14), 0.011, "pipe", sides=10))
    add(K.box("frame", (-L / 2, -D / 2, SH - 0.05), (L / 2, D / 2, SH - 0.02), "hull", bevel=0.005, seg=1))
    stripe(parts, "frame", -L / 2, L / 2, -D / 2, D / 2, SH - 0.05, SH - 0.02)
    for i, (ya, yb) in enumerate(((-0.19, -0.07), (-0.06, 0.06), (0.07, 0.19))):
        add(K.box("slat%d" % i, (-L / 2 + 0.02, ya, SH - 0.02), (L / 2 - 0.02, yb, SH + 0.012), "slate", bevel=0.008, seg=2))


def sofa():
    """Three-seat sofa 2.00 x 0.86: upholstered on a hull plinth with the stripe."""
    add(K.box("plinth", (-1.0, -0.43, 0.06), (1.0, 0.43, 0.20), "hull", bevel=0.008, seg=1))
    stripe(parts, "plinth", -1.0, 1.0, -0.43, 0.43, 0.06, 0.20)
    for i, (x, y) in enumerate(((-0.92, -0.36), (0.92, -0.36), (-0.92, 0.36), (0.92, 0.36))):
        add(K.box("foot%d" % i, (x - 0.05, y - 0.05, 0.0), (x + 0.05, y + 0.05, 0.062), "rubber", bevel=0.006, seg=1))
    for s in (-1, 1):
        soft(parts, "arm%d" % (s > 0), (min(s * 1.0, s * 0.82), -0.43, 0.20), (max(s * 1.0, s * 0.82), 0.43, 0.60), "upholstery", bevel=0.045)
    soft(parts, "back", (-0.82, 0.22, 0.20), (0.82, 0.43, 0.80), "upholstery", bevel=0.05)
    for i, (xa, xb) in enumerate(((-0.81, -0.275), (-0.265, 0.265), (0.275, 0.81))):
        soft(parts, "seat%d" % i, (xa, -0.41, 0.20), (xb, 0.23, 0.41), "upholstery", bevel=0.04)
        soft(parts, "bcush%d" % i, (xa + 0.01, 0.10, 0.40), (xb - 0.01, 0.25, 0.75), "upholstery", bevel=0.05)
    soft(parts, "pillow", (-0.78, -0.02, 0.41), (-0.56, 0.14, 0.60), "linen", bevel=0.05)
    soft(parts, "throw", (0.38, -0.40, 0.41), (0.80, 0.21, 0.428), "blanket", bevel=0.008)


def lamp():
    """Floor lamp 1.62 m: weighted base, pipe stem, a hull shade lit inside."""
    add(F.lathe_z(K, "base", [(0.0, 0.0), (0.17, 0.0), (0.17, 0.022), (0.13, 0.045), (0.0, 0.045)], 0, 0, ["slate"], seg=32))
    add(K.cyl("stem", (0, 0, 0.045), (0, 0, 1.42), 0.014, "pipe", sides=12))
    for i, z in enumerate((0.07, 0.80, 1.40)):
        add(K.cyl("col%d" % i, (0, 0, z - 0.014), (0, 0, z + 0.014), 0.021, "metal", sides=12))
    add(F.lathe_z(K, "shade", [(0.205, 1.40), (0.115, 1.62), (0.105, 1.612), (0.193, 1.405)], 0, 0,
                  ["hull", "cell_on"], seg=32, mis=[0, 0, 1, 0]))
    add(K.cyl("rim", (0, 0, 1.392), (0, 0, 1.408), 0.21, "accent", sides=32, caps=False))
    add(K.cyl("glow", (0, 0, 1.596), (0, 0, 1.604), 0.104, "cell_on", sides=32))
    add(K.cyl("bulb", (0, 0, 1.43), (0, 0, 1.50), 0.035, "cell_on", sides=16))


def rug():
    """Rug 2.00 x 1.40, 12 mm: warm wool field, sand border, three diamonds."""
    H = 0.012
    add(K.box("field", (-1.0, -0.7, 0.0), (1.0, 0.7, H), "rugwool", bevel=0.004, seg=1))
    b, w = 0.09, 0.055
    for i, (lo, hi) in enumerate((((-1 + b, -0.7 + b), (1 - b, -0.7 + b + w)), ((-1 + b, 0.7 - b - w), (1 - b, 0.7 - b)),
                                  ((-1 + b, -0.7 + b), (-1 + b + w, 0.7 - b)), ((1 - b - w, -0.7 + b), (1 - b, 0.7 - b)))):
        add(K.box("border%d" % i, (lo[0], lo[1], H - 0.002), (hi[0], hi[1], H + 0.002), "rugtrim", bevel=0.0, seg=1))
    for i, (cx, s) in enumerate(((0.0, 0.26), (-0.55, 0.15), (0.55, 0.15))):
        D = [K.box("dia%d" % i, (-s, -s, H - 0.002), (s, s, H + 0.002), "rugtrim" if i == 0 else "accent", bevel=0.0, seg=1)]
        F.place(D, Matrix.Translation(Vector((cx, 0, 0))) @ Matrix.Rotation(math.pi / 4, 4, "Z"))
        parts.extend(D)
    D = [K.box("dia_in", (-0.13, -0.13, H), (0.13, 0.13, H + 0.004), "rugwool", bevel=0.0, seg=1)]
    F.place(D, Matrix.Rotation(math.pi / 4, 4, "Z"))
    parts.extend(D)


def berth(out, tag, z_frame, L=2.0, W=0.90):
    """One sleeping deck: hull frame with the stripe, mattress, pillow at +X,
    a folded blanket at the foot (-X) and a turned-down sheet."""
    x0, x1, y0, y1 = -L / 2 + 0.03, L / 2 - 0.03, -W / 2 + 0.01, W / 2 - 0.01
    out.append(K.box(tag + "frame", (x0, y0, z_frame - 0.09), (x1, y1, z_frame), "hull", bevel=0.006, seg=1))
    stripe(out, tag + "frame", x0, x1, y0, y1, z_frame - 0.09, z_frame)
    mt = z_frame + 0.13
    soft(out, tag + "mattress", (x0 + 0.04, y0 + 0.03, z_frame - 0.01), (x1 - 0.04, y1 - 0.03, mt), "fabric", bevel=0.03)
    soft(out, tag + "pillow", (x1 - 0.34, y0 + 0.10, mt - 0.015), (x1 - 0.07, y1 - 0.10, mt + 0.07), "linen", bevel=0.035)
    out.append(K.box(tag + "sheet", (x1 - 0.52, y0 + 0.025, z_frame + 0.02), (x1 - 0.38, y1 - 0.025, mt + 0.008), "linen", bevel=0.006, seg=1))
    out.append(K.box(tag + "blanket0", (x0 + 0.06, y0 + 0.025, mt - 0.01), (x0 + 0.66, y1 - 0.025, mt + 0.035), "blanket", bevel=0.012, seg=1))
    out.append(K.box(tag + "blanket1", (x0 + 0.09, y0 + 0.05, mt + 0.03), (x0 + 0.62, y1 - 0.05, mt + 0.068), "blanket", bevel=0.012, seg=1))


def cot():
    """Single bed 2.00 x 0.90: frame top 0.42 m, head panel at +X."""
    X, Y = 0.95, 0.42
    for i, (x, y) in enumerate(((-X, -Y), (X, -Y), (-X, Y), (X, Y))):
        post(parts, "post%d" % i, x, y, 0.86 if x > 0 else 0.60, r=0.019, collars=(0.36,))
    for i, x in enumerate((-X, X)):
        rail(parts, "low%d" % i, (x, -Y, 0.13), (x, Y, 0.13))
    berth(parts, "", 0.42)
    add(K.box("head", (X - 0.025, -Y + 0.02, 0.48), (X + 0.012, Y - 0.02, 0.84), "hull", bevel=0.008, seg=1))
    soft(parts, "headpad", (X - 0.05, -Y + 0.06, 0.56), (X - 0.024, Y - 0.06, 0.80), "slate", bevel=0.01)
    add(K.box("foot", (-X - 0.012, -Y + 0.02, 0.44), (-X + 0.025, Y - 0.02, 0.58), "hull", bevel=0.008, seg=1))
    # under-bed drawer on the front
    add(K.box("drawer_box", (-0.55, -Y + 0.02, 0.08), (0.35, Y - 0.06, 0.30), "slate", bevel=0.005, seg=1))
    F.drawer_front(K, parts, "drawer", -0.53, 0.33, 0.10, 0.29, -Y + 0.02)


def bunkbed():
    """Two berths, 0.40 m and 1.40 m, on four tall posts; ladder and guard
    rail on the front (-Y) side."""
    X, Y = 0.96, 0.43
    for i, (x, y) in enumerate(((-X, -Y), (X, -Y), (-X, Y), (X, Y))):
        post(parts, "post%d" % i, x, y, 1.84, r=0.021, collars=(0.34, 1.34, 1.80))
    berth(parts, "lo_", 0.40)
    berth(parts, "hi_", 1.40)
    for i, x in enumerate((-X, X)):
        add(K.box("end%d" % i, (x - 0.015, -Y + 0.025, 1.46), (x + 0.015, Y - 0.025, 1.76), "hull", bevel=0.006, seg=1))
        rail(parts, "lowrail%d" % i, (x, -Y, 0.12), (x, Y, 0.12))
    rail(parts, "guard", (-X, -Y, 1.62), (0.42, -Y, 1.62), r=0.014)
    rail(parts, "guard_up", (0.42, -Y, 1.40), (0.42, -Y, 1.62), r=0.014)
    for i, x in enumerate((0.56, 0.86)):
        rail(parts, "ladder%d" % i, (x, -Y - 0.06, 0.02), (x, -Y - 0.06, 1.66), r=0.015)
        add(K.cyl("lfoot%d" % i, (x, -Y - 0.06, 0.0), (x, -Y - 0.06, 0.022), 0.021, "rubber", sides=12))
        rail(parts, "lhook%d" % i, (x, -Y - 0.06, 1.66), (x, -Y + 0.01, 1.66), r=0.013)
    for j, z in enumerate((0.30, 0.62, 0.94, 1.26)):
        add(K.cyl("rung%d" % j, (0.56, -Y - 0.06, z), (0.86, -Y - 0.06, z), 0.013, "metal", sides=10))


def plant():
    """Potted plant ~1.0 m: a hull pot with the yellow band, soil, blades."""
    add(F.lathe_z(K, "pot", [(0.0, 0.0), (0.15, 0.0), (0.20, 0.40), (0.225, 0.42), (0.225, 0.45),
                             (0.20, 0.45), (0.18, 0.40), (0.0, 0.40)], 0, 0, ["hull"], seg=28))
    add(K.cyl("band", (0, 0, 0.33), (0, 0, 0.36), 0.196, "accent", sides=28, caps=False))
    add(K.cyl("soil", (0, 0, 0.38), (0, 0, 0.415), 0.18, "soil", sides=28))
    add(K.cyl("foot", (0, 0, 0.0), (0, 0, 0.018), 0.155, "rubber", sides=28))
    blades = []
    n = 14
    for i in range(n):
        a = 2 * math.pi * i / n + (0.21 if i % 2 else 0.0)
        tilt = math.radians(28 + (i * 37) % 30)
        ln = 0.42 + ((i * 53) % 7) * 0.035
        # a tapered blade: narrow at the stalk, widest a third of the way out,
        # pointed at the tip (a convex hull, so it stays one clean solid)
        pts = [(x, sy * w, sz * t) for (x, w, t) in ((0.0, 0.016, 0.005), (ln * 0.35, 0.040, 0.005),
                                                     (ln * 0.75, 0.024, 0.004), (ln, 0.003, 0.002))
               for sy in (-1, 1) for sz in (-1, 1)]
        B = [F.hull_solid(K, "blade%d" % i, pts, "leaf")]
        F.place(B, Matrix.Translation(Vector((0, 0, 0.40))) @ Matrix.Rotation(a, 4, "Z")
                @ Matrix.Rotation(-math.pi / 2 + tilt, 4, "Y"))
        blades.extend(B)
    parts.extend(blades)
    add(K.cyl("stalk", (0, 0, 0.40), (0, 0, 0.62), 0.016, "leaf", sides=8))


BUILD = dict(chair=lambda: parts.extend(chair()), desk=desk, bench=bench, sofa=sofa, lamp=lamp, rug=rug,
             cot=cot, bunkbed=bunkbed, plant=plant)
if PIECE not in BUILD:
    raise SystemExit("unknown piece %r (have %s)" % (PIECE, ", ".join(BUILD)))
F.log("building %s" % NAME)
BUILD[PIECE]()
F.finalize(K, parts, NAME, OUT, SIZE, PREVIEW_ONLY)
