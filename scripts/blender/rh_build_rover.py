"""Build the Cargo Rover from code - the hard-surface build (2026-10-20, C6b).

  blender -b -P scripts/blender/rh_build_rover.py -- [--out Martians/gen/hs/Rover]
      [--size 1024] [--preview-only]

The freight vehicle of C6 (design: docs/qa/2026-10-20/c6-traffic-design.md): the
big hauler that only drives roads. A LOW, SIX-WHEELED FLATBED: three axles of fat
lugged tyres, a long open cargo bed with low rails, a tailgate and a tall
headboard, and a squat cab at the front with a raked windscreen, a roof light bar
and a beacon. The bed is EMPTY on purpose: the game shows the load as a separate
lump on it while the sim's CargoKg is above zero (RHAgentVisualizerSubsystem), so
the same mesh reads loaded or not.

Layout facts:
  - TRUE SCALE, real metres, ground-centred pivot (bounds centred on XY, tyre
    soles at z = 0): 5.0 m long, 2.6 m wide, 2.4 m to the beacon. A robot walker
    is 1.8 m tall, so the rover reads as a truck beside it. The game draws it at
    uniform scale 1 and follows the sim's road path with it.
  - built FRONT at +X (the direction of travel). The game turns the actor to the
    heading of the road leg it is driving, so no face_camera() here: a vehicle
    has no "identity face" for the strategy camera.
  - palette (art bible): chassis, rails, bumper, wheel arches slate; cab and
    headboard bone-white (hull); cab glass dark; tyres rubber; hubs, bed ribs
    light alloy; hazard-yellow stripe on the rail tops and tailgate. Accent
    (#D9822B, freight orange) is TRIM only: the headboard band, the cab belt
    stripe, the bumper tow bar, step edges, the beacon base.
  - the glow is ONE hue (amber, as the charge pad): the headlights, the roof
    light bar, the beacon lens and the tail lamps.
  - budget: ~9k tris.
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bpy  # noqa: E402
from mathutils import Matrix, Vector  # noqa: E402

import rh_hs  # noqa: E402

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
OUT = argv[argv.index("--out") + 1] if "--out" in argv else "Martians/gen/hs/Rover"
SIZE = int(argv[argv.index("--size") + 1]) if "--size" in argv else 1024
PREVIEW_ONLY = "--preview-only" in argv
NAME = "Rover"

K = rh_hs.Kit(accent_hex="#D9822B")
K.roles["glow"] = dict(albedo="#FFB040", metal=0.0, rough=0.25, emit=1, dust=0.0)
K.mats["glow"] = K._preview_material("glow", K.roles["glow"])
_gb = K.mats["glow"].node_tree.nodes["Principled BSDF"]
_gb.inputs["Emission Color"].default_value = (1.0, 0.49, 0.045, 1.0)
_gb.inputs["Emission Strength"].default_value = 1.5

parts = []
add = parts.append

# ------------------------------------------------------------------ layout
WR = 0.36                      # tyre radius
WW = 0.40                      # tyre width
WY = 1.00                      # wheel centre line off the axis (tyre spans WY +- WW/2)
AXLES = (-1.50, -0.05, 1.40)   # axle x
ZC = WR                        # axle height
FX0, FX1 = -2.30, 2.30         # chassis length
ZF0, ZF1 = 0.46, 0.70          # chassis frame
BX0, BX1 = -2.30, 0.88         # cargo bed
ZB = 0.86                      # bed floor top (its underside clears the tyres' guards)
CX0, CX1 = 0.98, 2.28          # cab


def rotated(ob, deg, axis="Y", about=(0.0, 0.0, 0.0)):
    ob.data.transform(Matrix.Translation(-Vector(about)))
    ob.data.transform(Matrix.Rotation(math.radians(deg), 4, axis))
    ob.data.transform(Matrix.Translation(Vector(about)))
    return ob


def lug_ring(cx, cy, cz, sy, n=14):
    """Tread lugs round a tyre whose axis is Y, centred (cx, cy, cz)."""
    out = []
    for k in range(n):
        a = 2 * math.pi * k / n
        ob = K.box("lug", (-0.06, -WW / 2 - 0.004, WR - 0.012), (0.06, WW / 2 + 0.004, WR + 0.028),
                   "rubber", bevel=0.006, seg=1)
        ob.data.transform(Matrix.Rotation(a, 4, "Y"))
        ob.data.transform(Matrix.Translation(Vector((cx, cy, cz))))
        out.append(ob)
    return out


# ------------------------------------------------------------------ chassis
add(K.box("frame", (FX0, -0.86, ZF0), (FX1, 0.86, ZF1), "slate", bevel=0.02, seg=1))
for sy in (-1, 1):
    # a deeper side rail either side of the frame, the alloy cross-members between
    add(K.box("rail", (FX0 + 0.05, sy * 0.86 - 0.04, ZF0 - 0.06), (FX1 - 0.05, sy * 0.86 + 0.04, ZF1 + 0.02),
              "pipe", bevel=0.01, seg=1))
for x in (-1.9, -0.8, 0.55, 1.0, 1.85):
    add(K.box("xmember", (x - 0.05, -0.80, ZF0 - 0.08), (x + 0.05, 0.80, ZF0 + 0.02), "metal", bevel=0.008, seg=1))

# battery pack slung under the frame between the middle and rear axles
add(K.box("pack", (-0.95, -0.62, 0.30), (-0.45, 0.62, ZF0 + 0.01), "slate", bevel=0.02, seg=1))
for k in range(5):
    add(K.box("pack_cell", (-0.93 + 0.0, -0.50 + k * 0.205, 0.33), (-0.91, -0.50 + k * 0.205 + 0.14, 0.46), "glow", bevel=0.0))

# ------------------------------------------------------------------ wheels
for ax in AXLES:
    add(K.cyl("axle", (ax, -WY, ZC), (ax, WY, ZC), 0.06, "pipe", sides=10))
    for sy in (-1, 1):
        cy = sy * WY
        add(K.cyl("tyre", (ax, cy - WW / 2, ZC), (ax, cy + WW / 2, ZC), WR, "rubber", sides=24))
        parts.extend(lug_ring(ax, cy, ZC, sy))
        face = cy + sy * (WW / 2)
        add(K.cyl("rim", (ax, face - sy * 0.02, ZC), (ax, face + sy * 0.015, ZC), 0.265, "slate", sides=20))
        add(K.cyl("hub", (ax, face, ZC), (ax, face + sy * 0.05, ZC), 0.11, "metal", sides=14))
        for b in range(6):
            a = 2 * math.pi * b / 6
            add(K.bolt("hbolt", (ax + 0.18 * math.cos(a), face + sy * 0.012, ZC + 0.18 * math.sin(a)),
                       (0, sy, 0), r=0.016, h=0.012))
        # swing arm from the frame out to the hub
        add(K.box("arm", (ax - 0.07, min(sy * 0.84, sy * (WY - WW / 2 + 0.02)), ZC - 0.07),
                  (ax + 0.07, max(sy * 0.84, sy * (WY - WW / 2 + 0.02)), ZC + 0.07), "pipe", bevel=0.01, seg=1))
        # mud-guard over the tyre (slate), a low arch on the bed side
        add(K.box("guard", (ax - 0.52, cy - WW / 2 - 0.05, ZC + WR + 0.02),
                  (ax + 0.52, cy + WW / 2 + 0.05, ZC + WR + 0.05), "slate", bevel=0.012, seg=1))
        add(K.box("guard_lip", (ax - 0.52, cy + sy * (WW / 2 + 0.03), ZC + WR - 0.16),
                  (ax + 0.52, cy + sy * (WW / 2 + 0.065), ZC + WR + 0.05), "slate", bevel=0.01, seg=1))

# ------------------------------------------------------------------ cargo bed
add(K.box("bed_floor", (BX0, -1.10, ZB - 0.07), (BX1, 1.10, ZB), "hull", bevel=0.012, seg=1))
for x in (-2.05, -1.55, -1.05, -0.55, -0.05, 0.45):
    add(K.box("bed_rib", (x - 0.03, -1.04, ZB), (x + 0.03, 1.04, ZB + 0.018), "metal", bevel=0.004, seg=1))
for sy in (-1, 1):
    lo_y, hi_y = sorted((sy * 1.04, sy * 1.12))
    add(K.box("side_rail", (BX0, lo_y, ZB), (BX1, hi_y, ZB + 0.44), "slate", bevel=0.012, seg=1))
    add(K.box("rail_top", (BX0 + 0.02, lo_y - 0.005, ZB + 0.44), (BX1 - 0.02, hi_y + 0.005, ZB + 0.475), "hazard", bevel=0.006, seg=1))
    for x in (-1.95, -1.15, -0.35, 0.45):
        add(K.box("stake", (x - 0.05, min(sy * 1.02, sy * 1.14), ZB + 0.05), (x + 0.05, max(sy * 1.02, sy * 1.14), ZB + 0.62),
                  "pipe", bevel=0.006, seg=1))
        add(K.bolt("tie", (x, sy * 1.14, ZB + 0.28), (0, sy, 0), r=0.025, h=0.02))
# tailgate and headboard
add(K.box("tailgate", (BX0 - 0.06, -1.12, ZB), (BX0, 1.12, ZB + 0.40), "slate", bevel=0.012, seg=1))
add(K.box("tail_band", (BX0 - 0.066, -1.0, ZB + 0.24), (BX0 - 0.054, 1.0, ZB + 0.31), "hazard", bevel=0.004, seg=1))
for sy in (-1, 1):
    add(K.box("tail_lamp", (BX0 - 0.075, sy * 0.92 - 0.10, ZB + 0.06), (BX0 - 0.055, sy * 0.92 + 0.10, ZB + 0.15), "glow", bevel=0.0))
add(K.box("headboard", (BX1 - 0.12, -1.12, ZB), (BX1, 1.12, ZB + 1.10), "hull", bevel=0.015, seg=1))
add(K.box("head_band", (BX1 - 0.135, -1.10, ZB + 0.70), (BX1 - 0.115, 1.10, ZB + 0.84), "accent", bevel=0.004, seg=1))
add(K.box("head_frame", (BX1 - 0.135, -1.12, ZB + 1.06), (BX1 - 0.105, 1.12, ZB + 1.14), "slate", bevel=0.006, seg=1))
for y in (-0.8, -0.27, 0.27, 0.8):
    add(K.box("head_rib", (BX1 - 0.15, y - 0.03, ZB + 0.10), (BX1 - 0.12, y + 0.03, ZB + 0.60), "metal", bevel=0.004, seg=1))

# ------------------------------------------------------------------ cab
ZC0, ZC1 = 0.78, 1.66
cab = K.box("cab", (CX0, -0.98, ZC0), (CX1, 0.98, ZC1), "hull", bevel=0.03, seg=2,
            chamfers=[(lambda d, m: abs(d.y) > 0.99 and m.x > (CX0 + CX1) / 2 and m.z > ZC1 - 0.02, 0.30)])
add(cab)
add(K.box("cab_belt", (CX0 - 0.005, -0.99, 1.00), (CX1 + 0.005, 0.99, 1.07), "accent", bevel=0.005, seg=1))
add(K.box("cab_skirt", (CX0 + 0.02, -1.00, ZC0 - 0.10), (CX1, 1.00, ZC0 + 0.10), "slate", bevel=0.015, seg=1))
# windscreen: a slab lying on the 45 deg chamfer (x from CX1 - 0.30 at the roof down to CX1 at z = ZC1 - 0.30)
ws = K.box("windscreen", (-0.20, -0.84, -0.011), (0.20, 0.84, 0.011), "glass", bevel=0.003, seg=1)
ws.data.transform(Matrix.Rotation(math.radians(45.0), 4, "Y"))
ws.data.transform(Matrix.Translation(Vector((CX1 - 0.15 + 0.012, 0.0, ZC1 - 0.15 + 0.012))))
add(ws)
for sy in (-1, 1):
    add(K.box("side_window", (CX0 + 0.28, sy * 0.978 - 0.008, 1.14), (CX1 - 0.50, sy * 0.978 + 0.008, 1.50), "glass", bevel=0.004, seg=1))
    add(K.box("window_frame", (CX0 + 0.25, sy * 0.974 - 0.004, 1.11), (CX1 - 0.47, sy * 0.974 + 0.004, 1.53), "slate", bevel=0.004, seg=1))
    add(K.box("door_handle", (CX0 + 0.12, sy * 0.99 - 0.015, 1.02), (CX0 + 0.26, sy * 0.99 + 0.015, 1.06), "metal", bevel=0.004, seg=1))
    # step
    add(K.box("step", (CX0 + 0.20, sy * 1.08 - 0.10, 0.44), (CX0 + 0.70, sy * 1.08 + 0.10, 0.485), "slate", bevel=0.006, seg=1))
    add(K.box("step_edge", (CX0 + 0.20, sy * 1.17 - 0.012, 0.44), (CX0 + 0.70, sy * 1.17 + 0.012, 0.50), "accent", bevel=0.003, seg=1))
    # headlights
    add(K.box("headlamp", (CX1 + 0.005, sy * 0.66 - 0.17, 0.90), (CX1 + 0.04, sy * 0.66 + 0.17, 1.00), "glow", bevel=0.0))
    add(K.box("headlamp_bezel", (CX1 - 0.005, sy * 0.66 - 0.20, 0.87), (CX1 + 0.02, sy * 0.66 + 0.20, 1.03), "slate", bevel=0.01, seg=1))
# grille slats
for k in range(4):
    add(K.box("grille", (CX1 + 0.005, -0.28, 0.80 + k * 0.045), (CX1 + 0.03, 0.28, 0.80 + k * 0.045 + 0.022), "pipe", bevel=0.003, seg=1))
# bumper and tow bar
add(K.box("bumper", (CX1 + 0.01, -1.05, 0.46), (CX1 + 0.20, 1.05, 0.74), "slate", bevel=0.025, seg=1))
add(K.box("bumper_bar", (CX1 + 0.195, -0.90, 0.57), (CX1 + 0.215, 0.90, 0.64), "accent", bevel=0.004, seg=1))
add(K.cyl("tow_hook", (CX1 + 0.20, 0.0, 0.52), (CX1 + 0.32, 0.0, 0.52), 0.03, "metal", sides=8))
# roof: light bar, beacon, mast
add(K.box("light_bar_base", (CX1 - 0.62, -0.62, ZC1), (CX1 - 0.42, 0.62, ZC1 + 0.07), "slate", bevel=0.01, seg=1))
add(K.box("light_bar", (CX1 - 0.60, -0.58, ZC1 + 0.07), (CX1 - 0.44, 0.58, ZC1 + 0.14), "glow", bevel=0.0))
bx, bz = CX0 + 0.28, ZC1
add(K.cyl("beacon_base", (bx, 0.55, bz), (bx, 0.55, bz + 0.07), 0.10, "accent", sides=14))
add(K.cyl("beacon_lens", (bx, 0.55, bz + 0.07), (bx, 0.55, bz + 0.20), 0.07, "glow", sides=14))
add(K.cyl("mast", (bx, -0.50, bz), (bx, -0.50, bz + 0.62), 0.016, "pipe", sides=8))
add(K.cyl("mast_tip", (bx, -0.50, bz + 0.62), (bx, -0.50, bz + 0.70), 0.026, "metal", sides=8))

# ------------------------------------------------------------------ finish
ob = K.finish(parts, NAME)
vs = [v.co for v in ob.data.vertices]
lo = Vector((min(v.x for v in vs), min(v.y for v in vs), min(v.z for v in vs)))
hi = Vector((max(v.x for v in vs), max(v.y for v in vs), max(v.z for v in vs)))
ob.data.transform(Matrix.Translation(Vector((-(lo.x + hi.x) / 2, -(lo.y + hi.y) / 2, -lo.z))))
ob.data.update()
dims = hi - lo
rh_hs.log("recentred; dims %s m" % [round(d, 3) for d in dims])
assert 4.4 <= dims.x <= 5.1 and 2.3 <= dims.y <= 2.8 and 1.9 <= dims.z <= 2.7, "unexpected size %s" % (dims,)
os.makedirs(OUT, exist_ok=True)
bpy.ops.object.select_all(action="DESELECT")
ob.select_set(True)
bpy.context.view_layer.objects.active = ob
bpy.ops.export_scene.gltf(filepath=os.path.join(OUT, NAME + "_preview.glb"), export_format="GLB",
                          use_selection=True, export_apply=True)
rh_hs.log("preview glb written")
if not PREVIEW_ONLY:
    files = K.bake_and_export(ob, NAME, OUT, size=SIZE, dust=0.22)
    rh_hs.log("files: %s" % files)
