"""Build the BatteryBank from code - the hard-surface remake (2026-10-13).

  blender -b -P scripts/blender/rh_build_batterybank.py -- [--out Martians/gen/hs/BatteryBank]
      [--size 2048] [--preview-only]

Replaces the image-to-3D BatteryBank the director flagged "weird" on the
2026-10-13 triage (shredded fins, holes, melted panels, teal head to toe). The
DESIGN is kept: Martians/gen/batch4/cutout/battery.png, the approved hero
reference - stacked bone-white modules, teal-bezelled charge-bar windows, a
solid ribbed heat-sink stack on each side, coolant loops over the top. Only
the construction changes: every part is an exact primitive, so nothing can melt
or shred and the emissive mask is the lit cells themselves.

Layout matches the asset it replaces: charge bars face -Y (front) and +Y (back),
radiators on +-X, cube proportions (the visualizer fits the larger horizontal
extent to the 2x2 footprint and seats the bounds bottom on the ground).
Real-world metres, ground-centred pivot.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rh_hs  # noqa: E402

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OUT = argv[argv.index("--out") + 1] if "--out" in argv else "Martians/gen/hs/BatteryBank"
SIZE = int(argv[argv.index("--size") + 1]) if "--size" in argv else 2048
PREVIEW_ONLY = "--preview-only" in argv
NAME = "BatteryBank"

K = rh_hs.Kit(accent_hex="#59EDD3")       # TintFor(BatteryBank), art bible 2.3
parts = []
add = parts.append

W, D = 1.0, 0.8                            # half extents of the cabinet (x, y)
Z0 = 0.10                                  # top of the base skid
GAP = 0.02                                 # seam between stacked modules
# (height, bars, has_led_strip) bottom to top
MODULES = [(0.46, 2, False), (0.38, 1, True), (0.62, 3, False), (0.48, 2, False)]
CHARGE = {-1: [0.55, 0.72, 0.68, 0.78, 0.66, 0.60, 0.70, 0.74],
          +1: [0.62, 0.58, 0.75, 0.70, 0.64, 0.71, 0.57, 0.69]}

# ---------------------------------------------------------------- base skid
add(K.box("base", (-0.94, -0.74, 0.0), (0.94, 0.74, Z0), "slate", bevel=0.01))
for s in (-1, 1):
    y0 = s * 0.740
    add(K.box("hazard%d" % s, (-0.90, min(y0, y0 + s * 0.007), 0.072),
              (0.90, max(y0, y0 + s * 0.007), 0.092), "hazard", bevel=0.002, seg=1))


def vertical(d, m):
    return abs(d.z) > 0.99


# ------------------------------------------------------- stacked modules
z = Z0
bar_rows = []                              # (z centre) for every bar, bottom to top
led_rows = []
module_spans = []
for i, (h, nbars, led) in enumerate(MODULES):
    zlo, zhi = z, z + h
    module_spans.append((zlo, zhi))
    chamfers = [(vertical, 0.07)]
    if i == len(MODULES) - 1:              # big top chamfers, front and back
        chamfers.append((lambda d, m, zhi=zhi: abs(d.x) > 0.99 and m.z > zhi - 0.01
                         and abs(m.y) > D - 0.01, 0.08))
    add(K.box("module%d" % i, (-W, -D, zlo), (W, D, zhi), "hull", bevel=0.012,
              chamfers=chamfers))
    pitch = 0.19
    if led:
        bar_rows.append(zhi - 0.13)
        led_rows.append(zlo + 0.09)
    else:
        first = zlo + (h - (nbars - 1) * pitch) / 2
        bar_rows += [first + k * pitch for k in range(nbars)]
    if i < len(MODULES) - 1:               # dark seam slab showing in the gap
        add(K.box("seam%d" % i, (-W + 0.025, -D + 0.025, zhi - 0.02),
                  (W - 0.025, D - 0.025, zhi + GAP + 0.02), "slate", bevel=0.0,
                  chamfers=[(vertical, 0.09)]))
    z = zhi + GAP
TOP = z - GAP                              # 2.10

# ------------------------------------------- charge bars, front and back
BW, BH, RIM, RECESS, PROUD, NCELL = 1.44, 0.15, 0.022, 0.012, 0.022, 22
for facing in (-1, 1):
    yface = facing * D
    for k, zc in enumerate(bar_rows):
        bz, wy = K.bezel("bar%d_%d" % (facing, k), (0.0, yface, zc), BW, BH, PROUD,
                         radius=0.05, rim=RIM, recess=RECESS, rim_role="accent",
                         inner_role="glass", facing=facing)
        add(bz)
        inner_w, inner_h = BW - 2 * RIM, BH - 2 * RIM
        span = inner_w - 2 * 0.028
        pitch = span / NCELL
        lit = round(CHARGE[facing][k] * NCELL)
        for c in range(NCELL):
            # fill left-to-right as seen by someone FACING that side: on the back
            # (+Y) face the viewer's left is +X, so the run is mirrored
            cx = -facing * (-span / 2 + pitch * (c + 0.5))
            add(K.quad("cell%d_%d_%d" % (facing, k, c), (cx, wy + facing * 0.003, zc),
                       pitch * 0.76, inner_h - 0.036, "cell_on" if c < lit else "cell_off",
                       facing=facing))
    for zl in led_rows:                    # status LED dashes
        for j, (x0, x1) in enumerate(((-0.62, -0.40), (-0.25, 0.30), (0.45, 0.62))):
            add(K.box("led%d_%d" % (facing, j), (x0, min(yface, yface + facing * 0.006), zl - 0.009),
                      (x1, max(yface, yface + facing * 0.006), zl + 0.009), "cell_on",
                      bevel=0.002, seg=1))
    for (zlo, zhi) in module_spans:        # corner bolts
        for bx in (-0.88, 0.88):
            for bzc in (zlo + 0.05, zhi - 0.05):
                add(K.bolt("bolt", (bx, yface, bzc), (0, facing, 0)))

# ---------------------------------------- heat-sink stacks on +-X (solid)
NFIN, FZ0, FZ1 = 24, 0.36, 1.80
for s in (-1, 1):
    def xr(a, b):
        return (min(s * a, s * b), max(s * a, s * b))
    x0, x1 = xr(0.98, 1.05)
    add(K.box("core%d" % s, (x0, -0.62, 0.30), (x1, 0.62, 1.86), "slate", bevel=0.008))
    for ey in (-1, 1):                     # end plates frame the fins: reads as a block
        px0, px1 = xr(0.98, 1.27)
        add(K.box("plate%d_%d" % (s, ey), (px0, min(ey * 0.60, ey * 0.66), 0.28),
                  (px1, max(ey * 0.60, ey * 0.66), 1.88), "slate", bevel=0.008))
    fx0, fx1 = xr(1.05, 1.25)
    for k in range(NFIN):
        fz = FZ0 + (FZ1 - FZ0) * k / (NFIN - 1)
        add(K.box("fin%d_%d" % (s, k), (fx0, -0.60, fz - 0.008), (fx1, 0.60, fz + 0.008),
                  "metal", bevel=0.003, seg=1))
    add(K.cyl("hdr_top%d" % s, (s * 1.15, -0.56, 1.95), (s * 1.15, 0.56, 1.95), 0.06, "pipe"))
    add(K.cyl("hdr_bot%d" % s, (s * 1.15, -0.56, 0.24), (s * 1.15, 0.56, 0.24), 0.05, "pipe"))

# ------------------------------------------------ coolant loops over the top
PIPE_Y = (-0.45, -0.15, 0.15, 0.45)
for k, py in enumerate(PIPE_Y):
    add(K.pipe("loop%d" % k, [(1.15, py, 1.95), (1.15, py, 2.24), (-1.15, py, 2.24),
                              (-1.15, py, 1.95)], 0.032, "pipe", bend=0.12))
for rx in (-0.55, 0.55):
    add(K.box("rack%d" % (rx > 0), (rx - 0.05, -0.58, TOP - 0.01), (rx + 0.05, 0.58, 2.19),
              "slate", bevel=0.006))
    for py in PIPE_Y:
        add(K.box("clamp", (rx - 0.045, py - 0.05, 2.19), (rx + 0.045, py + 0.05, 2.285),
                  "hull", bevel=0.008))
        for by in (py - 0.03, py + 0.03):
            add(K.bolt("cbolt", (rx, by, 2.285), (0, 0, 1), r=0.011, h=0.007))

ob = K.finish(parts, NAME)
os.makedirs(OUT, exist_ok=True)
import math  # noqa: E402
import bpy  # noqa: E402
# Quarter turn: the strategy camera's default view meets the model's +-X sides
# (2026-10-13 in-game capture showed the radiator to the player and the charge
# bars edge-on - true of the replaced asset too). Bars are the identity, so they
# face the player; both bar faces are identical, so either quarter turn works.
ob.rotation_euler.z = math.radians(90.0)
bpy.ops.object.transform_apply(location=False, rotation=True, scale=False)
bpy.ops.object.select_all(action="DESELECT")
ob.select_set(True)
bpy.ops.export_scene.gltf(filepath=os.path.join(OUT, NAME + "_preview.glb"), export_format="GLB",
                          use_selection=True, export_apply=True)
rh_hs.log("preview glb written")
if not PREVIEW_ONLY:
    files = K.bake_and_export(ob, NAME, OUT, size=SIZE, dust=0.22)
    rh_hs.log("files: %s" % files)
