"""rh_crew_build - crew walkers from ONE MPFB base body (crew pilot, 2026-10-13).

  blender -b -P scripts/blender/rh_crew_build.py -- --face cmdr_vale
      [--out Martians/gen/crew_mpfb] [--size 2048] [--no-clips]

WHY. The 20 shipped walkers are image-to-3D meshes from two AI batches (12 at
18k tris, 8 regenerated at ~11k), each on its own 14-bone rig placed at fixed
height fractions (hips 50 %, neck 82 %) whatever the body underneath - so one
clip moves every body differently, proportions come from different worlds,
and the AI-painted textures read blotchy. The director's brief: same animation,
same drawing style, different sizes, proportions from one world.

HOW. "One body, many people":
  - ONE base body: MPFB2 (MakeHuman's Blender add-on, extension v2.0.17; its
    base mesh, targets, proxies, skins, clothes and hair are CC0 - see
    docs/third-party-notices.md). Height, build, age and ancestry are macro
    sliders on that one topology, so proportions stay in one family.
  - ONE skeleton: MPFB's 53-bone game_engine rig (UE mannequin bone names:
    Root, pelvis, spine_01-03, neck_01, head, clavicle/upperarm/lowerarm/hand +
    fingers, thigh/calf/foot/ball).
  - ONE clip set: scripts/blender/rh_crew_clips.py keys every clip on that rig
    with the same code, so every crew member moves the same way.
  - ONE drawing style: CC0 skins as-is; every garment recoloured into the art
    bible palette by region (its own fold/seam detail kept as luminance,
    multiplied by the garment's AO map), hair and brows tinted per person, then
    everything baked into one 2048 atlas with the same AO treatment. Alpha
    (hair, brows) rides in the atlas's A channel for a masked material.
  - Real heights in metres. The game currently rescales every walker to 1.8 m
    (RHCrewVisualizerSubsystem.cpp ~305); true height variety needs that
    one-line change (director compile) - until then the proportions still
    differ, the heights do not.

OUTPUT (git-ignored Martians/gen/crew_mpfb/<face>/): RH_Walker_<face>.glb
(skinned mesh + armature + the ten clips as glTF actions - the same export
settings scripts/gpu/lane/rig_colonist.py used, so Interchange produces the same
RH_Walker_<face> / RH_Walker_<face><Clip> assets), the BaseColor (RGBA) and MR
atlases, and report.json. Faces -Y, left at +X, feet at z=0 - the orientation
the shipped walkers have, which rh.WalkerYawOffsetDeg (-90) already corrects.
"""
import json
import math
import os
import sys

import bpy
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rh_crew_clips  # noqa: E402
from bl_ext.user_default.mpfb.services import HumanService, LocationService, TargetService  # noqa: E402

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
FACE = argv[argv.index("--face") + 1]
OUT_ROOT = argv[argv.index("--out") + 1] if "--out" in argv else "Martians/gen/crew_mpfb"
SIZE = int(argv[argv.index("--size") + 1]) if "--size" in argv else 2048
NO_CLIPS = "--no-clips" in argv
OUT = os.path.abspath(os.path.join(OUT_ROOT, FACE))   # absolute: the .blend must find its images
DATA = LocationService.get_user_data()
NAME = "RH_Walker_" + FACE

# --------------------------------------------------------------- the roster
# Garment recolour rules: each garment's faces are split into regions, each
# region gets a palette tint. split "z": above/below the waist; "hue": by the
# garment texture's own colour at the face (denim blue -> lower, red -> shirt,
# anything else -> upper).
CREW = {
    "cmdr_vale": dict(
        sex=0.0, age=0.62, height_m=1.62, muscle=0.55, weight=0.42, proportions=0.65,
        race=dict(asian=0.92, caucasian=0.08, african=0.0), skin="middleage_asian_female", proxy="female1605",
        outfit=[("male_casualsuit01", "z", {"upper": "#3A3F48", "lower": "#26282D"}),
                ("shoes03", "all", {"all": "#232427"})],
        hair=("short04", "#171514"), brow=("eyebrow006", "#1A1716"), eyes="brown"),
    "vet_kowalski": dict(
        sex=1.0, age=0.86, height_m=1.88, muscle=0.6, weight=0.74, proportions=0.5,
        race=dict(asian=0.0, caucasian=1.0, african=0.0), skin="old_caucasian_male", proxy="male1591",
        outfit=[("male_casualsuit05", "hue", {"upper": "#6C6948", "lower": "#7E7C77", "shirt": "#7C3A30"}),
                ("shoes03", "all", {"all": "#3A2E26"})],
        hair=("short02", "#8F8B85"), brow=("eyebrow002", "#88837C"), eyes="blue"),
}


# ------------------------------------------------- the rest of the roster
# Outfit kit (the shared wardrobe): three cuts, recoloured per person.
def jumpsuit(c, boots="#2A2622"):
    """Long-sleeved shirt + trousers in one colour reads as a coverall."""
    return [("male_casualsuit01", "z", {"upper": c, "lower": c}), ("shoes03", "all", {"all": boots})]


def shirt(top, bottom, boots="#2A2622"):
    return [("male_casualsuit01", "z", {"upper": top, "lower": bottom}), ("shoes03", "all", {"all": boots})]


def jacket(jk, trousers, inner, boots="#2A2622"):
    return [("male_casualsuit05", "hue", {"upper": jk, "lower": trousers, "shirt": inner}),
            ("shoes03", "all", {"all": boots})]


def person(sex, age, h, skin, race, outfit, hair, brow, muscle=0.55, weight=0.5, proportions=0.6):
    """sex 0 = female / 1 = male; age is MPFB's macro (0.5 = 25 y, 1.0 = 90 y);
    h in metres; race = (african, asian, caucasian)."""
    a, s_, c = race
    return dict(sex=sex, age=age, height_m=h, muscle=muscle, weight=weight, proportions=proportions,
                race=dict(african=a, asian=s_, caucasian=c), skin=skin,
                proxy="female1605" if sex < 0.5 else "male1591",
                outfit=outfit, hair=hair, brow=brow, eyes="brown")


# Identity from the shipped references (Martians/gen/chars-redo/<id>/cutout/
# <id>_habitat_front.png for the July twelve, Martians/gen/crew8/cutout/<id>.png
# for the August eight): build, age, ancestry, hair and outfit colours. Faces
# are MPFB's - identity at gameplay zoom is silhouette, height, skin, hair and
# clothing, which is what carries over.
CREW.update({
    "eng_ruiz": person(1, 0.70, 1.76, "middleage_caucasian_male", (0.1, 0.15, 0.75), jumpsuit("#4B4541"),
                       ("short01", "#7C7873"), ("eyebrow002", "#5A5550"), muscle=0.6, weight=0.6),
    "geo_okafor": person(1, 0.68, 1.86, "middleage_african_male", (0.95, 0.0, 0.05),
                         jacket("#8A4B33", "#5E5A52", "#3A3C40"), ("short01", "#141110"), ("eyebrow001", "#141110"),
                         muscle=0.75, weight=0.68),
    "med_haddad": person(1, 0.57, 1.78, "young_caucasian_male", (0.08, 0.07, 0.85), shirt("#E4DED2", "#7898AE"),
                         ("short03", "#1E1814"), ("eyebrow003", "#1E1814"), muscle=0.5, weight=0.45),
    "tech_park": person(0, 0.55, 1.66, "young_asian_female", (0.0, 0.95, 0.05), jacket("#4C566C", "#6E6C68", "#2E2F33"),
                        ("short04", "#131110"), ("eyebrow006", "#131110"), weight=0.4),
    "pilot_reyes": person(0, 0.58, 1.68, "young_caucasian_female2", (0.15, 0.15, 0.7), jacket("#3E5A7F", "#2B2A29", "#3A3A3C"),
                          ("short04", "#1B1512"), ("eyebrow005", "#1B1512"), weight=0.45),
    "quart_bello": person(1, 0.60, 1.80, "young_african_male", (0.92, 0.0, 0.08), jacket("#68683E", "#8C7F66", "#55603F"),
                          ("afro01", "#15110F"), ("eyebrow001", "#15110F"), muscle=0.6, weight=0.88),
    "res_novak": person(0, 0.78, 1.66, "old_caucasian_female", (0.0, 0.0, 1.0), jacket("#DAD7CF", "#6F6E6B", "#5D6873"),
                        ("bob01", "#CBC8C2"), ("eyebrow004", "#9A968F"), weight=0.5),
    "hydro_mensah": person(0, 0.52, 1.64, "young_african_female", (0.95, 0.0, 0.05), shirt("#5C6B44", "#4E6A86"),
                           ("ponytail01", "#15100E"), ("eyebrow007", "#15100E"), weight=0.45),
    "reactor_ito": person(1, 0.65, 1.72, "middleage_asian_male", (0.0, 0.95, 0.05), jumpsuit("#3A3C3F"),
                          ("short01", "#2A2826"), ("eyebrow002", "#1E1C1A"), muscle=0.6, weight=0.5),
    "survey_khan": person(1, 0.62, 1.80, "middleage_asian_male", (0.1, 0.35, 0.55), jacket("#8A8270", "#5D5A52", "#3D3D3B"),
                          ("short02", "#1A1512"), ("eyebrow002", "#1A1512"), muscle=0.6, weight=0.55),
    "xeno_adeyemi": person(0, 0.60, 1.70, "middleage_african_female", (0.95, 0.0, 0.05), jacket("#6E3044", "#6D8191", "#E0D8C8"),
                           ("afro01", "#1A1310"), ("eyebrow006", "#1A1310"), weight=0.48),
    "bot_lindqvist": person(0, 0.82, 1.64, "old_caucasian_female", (0.0, 0.0, 1.0), jumpsuit("#6F7D61"),
                            ("short04", "#D9D7D1"), ("eyebrow004", "#B0ADA6"), weight=0.5),
    "fab_stone": person(0, 0.60, 1.74, "middleage_african_female", (0.95, 0.0, 0.05), jumpsuit("#4A4A4C"),
                        ("short01", "#110D0B"), ("eyebrow006", "#110D0B"), muscle=0.7, weight=0.55),
    "comms_diallo": person(1, 0.58, 1.79, "young_african_male", (1.0, 0.0, 0.0), jumpsuit("#4F5B6E"),
                           ("short01", "#100C0A"), ("eyebrow001", "#100C0A"), weight=0.5),
    "rookie_shaw": person(1, 0.47, 1.77, "young_caucasian_male", (0.0, 0.0, 1.0), jumpsuit("#A1956F"),
                          ("short03", "#A34F22"), ("eyebrow003", "#8A4A2A"), muscle=0.45, weight=0.38),
    "safety_abara": person(0, 0.62, 1.69, "middleage_african_female", (1.0, 0.0, 0.0), jacket("#E2862C", "#555556", "#444446"),
                           ("short01", "#100C0A"), ("eyebrow006", "#100C0A"), muscle=0.6, weight=0.55),
    "driver_costa": person(1, 0.60, 1.75, "middleage_caucasian_male", (0.2, 0.1, 0.7), jacket("#9A4A2C", "#5F6266", "#6A6E73"),
                           ("short02", "#120E0C"), ("eyebrow002", "#120E0C"), muscle=0.6, weight=0.55),
    "cook_moreau": person(0, 0.75, 1.60, "middleage_caucasian_female", (0.0, 0.2, 0.8), shirt("#D6CCB8", "#6A6863"),
                          ("ponytail01", "#5E5852"), ("eyebrow005", "#4A453F"), weight=0.62),
})
ROUGH = {"skin": 0.55, "cloth": 0.82, "shoe": 0.45, "hair": 0.6, "eye": 0.15}
CLOTH_MAX_TRIS = 8000      # per garment; keeps every crew member inside the 25k budget

# Measured 2026-10-13 (evaluated mesh, proportions 0.6): MPFB height macro -> metres.
H_TABLE = {0.0: [(0.2, 1.382), (0.5, 1.593), (0.8, 2.008), (1.0, 2.294)],
           1.0: [(0.2, 1.519), (0.5, 1.729), (0.8, 2.144), (1.0, 2.431)]}


def log(msg):
    print("[crew] %s" % msg, flush=True)


def hex_lin(h):
    h = h.lstrip("#")
    c = [int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4)]
    return tuple(x / 12.92 if x <= 0.04045 else ((x + 0.055) / 1.055) ** 2.4 for x in c)


def height_macro(target, sex):
    """Invert the measured table (blend female/male rows by sex)."""
    rows = [(x, (1 - sex) * hf + sex * hm) for (x, hf), (_, hm) in zip(H_TABLE[0.0], H_TABLE[1.0])]
    for (x0, h0), (x1, h1) in zip(rows, rows[1:]):
        if target <= h1 or (x1, h1) == rows[-1]:
            return x0 + (target - h0) * (x1 - x0) / (h1 - h0)
    return 0.5


def P(*a):
    return os.path.join(DATA, *a)


# ------------------------------------------------------------ build person
def build(spec):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    mac = TargetService.get_default_macro_info_dict()
    mac.update(gender=spec["sex"], age=spec["age"], muscle=spec["muscle"], weight=spec["weight"],
               proportions=spec["proportions"], height=height_macro(spec["height_m"], spec["sex"]))
    mac["race"] = dict(spec["race"])
    bm = HumanService.create_human(macro_detail_dict=mac, feet_on_ground=True, scale=0.1)
    rig = HumanService.add_builtin_rig(bm, "game_engine")
    proxy = HumanService.add_mhclo_asset(P("proxymeshes", spec["proxy"], spec["proxy"] + ".proxy"), bm,
                                         asset_type="Proxymeshes", subdiv_levels=0, material_type="NONE")
    HumanService.set_character_skin(P("skins", spec["skin"], spec["skin"] + ".mhmat"), bm, bodyproxy=proxy,
                                    skin_type="GAMEENGINE")
    parts = {"skin": [proxy], "cloth": [], "shoe": [], "hair": [], "eye": []}
    for garment, split, tints in spec["outfit"]:
        o = HumanService.add_mhclo_asset(P("clothes", garment, garment + ".mhclo"), bm, asset_type="Clothes",
                                         subdiv_levels=0, material_type="MAKESKIN")
        recolour_garment(o, garment, split, tints, rig)
        parts["shoe" if garment.startswith("shoes") else "cloth"].append(o)
    eyes = HumanService.add_mhclo_asset(P("eyes", "low-poly", "low-poly.mhclo"), bm, asset_type="Eyes",
                                        subdiv_levels=0, material_type="MAKESKIN")
    parts["eye"].append(eyes)
    for kind, (asset, colour) in (("Eyebrows", spec["brow"]), ("Hair", spec["hair"])):
        sub = "eyebrows" if kind == "Eyebrows" else "hair"
        o = HumanService.add_mhclo_asset(P(sub, asset, asset + ".mhclo"), bm, asset_type=kind,
                                         subdiv_levels=0, material_type="MAKESKIN")
        tint_strands(o, colour)
        parts["hair"].append(o)
    bm.hide_render = True
    bm.hide_set(True)
    return bm, rig, parts


def _tex_nodes(mat):
    return [n for n in mat.node_tree.nodes if n.type == "TEX_IMAGE" and n.image]


def _principled(mat):
    return next((n for n in mat.node_tree.nodes if n.type == "BSDF_PRINCIPLED"), None)


def recolour_garment(o, garment, split, tints, rig):
    """Split the garment's faces into regions and give each region a palette
    tint x the garment's own luminance detail x its AO map."""
    src = o.data.materials[0]
    imgs = {n.image.name: n.image for n in _tex_nodes(src)}
    diff = next((i for n, i in imgs.items() if "diffuse" in n.lower()), None)
    ao = next((i for n, i in imgs.items() if "_ao" in n.lower()), None)
    if diff is None:
        diff = next(iter(imgs.values()), None)
    px = np.empty(diff.size[0] * diff.size[1] * 4, dtype=np.float32)
    diff.pixels.foreach_get(px)
    px = px.reshape(diff.size[1], diff.size[0], 4)
    lum = px[..., :3] @ np.array([0.2126, 0.7152, 0.0722])
    mean = float(lum[px[..., 3] > 0.5].mean()) if (px[..., 3] > 0.5).any() else 0.4
    regions = list(tints.keys())
    mats = {}
    for r in regions:
        m = bpy.data.materials.new("%s.%s" % (garment, r))
        m.use_nodes = True
        nt = m.node_tree
        b = _principled(m)
        tx = nt.nodes.new("ShaderNodeTexImage"); tx.image = diff
        bw = nt.nodes.new("ShaderNodeRGBToBW")
        nt.links.new(bw.inputs["Color"], tx.outputs["Color"])
        div = nt.nodes.new("ShaderNodeMath"); div.operation = "DIVIDE"; div.inputs[1].default_value = mean
        nt.links.new(div.inputs[0], bw.outputs["Val"])
        pw = nt.nodes.new("ShaderNodeMath"); pw.operation = "POWER"; pw.inputs[1].default_value = 0.55
        nt.links.new(pw.inputs[0], div.outputs["Value"])
        cl = nt.nodes.new("ShaderNodeClamp"); cl.inputs["Min"].default_value = 0.65; cl.inputs["Max"].default_value = 1.3
        nt.links.new(cl.inputs["Value"], pw.outputs["Value"])
        mix = nt.nodes.new("ShaderNodeMix"); mix.data_type = "RGBA"; mix.blend_type = "MULTIPLY"
        mix.inputs["Factor"].default_value = 1.0
        mix.inputs["A"].default_value = (*hex_lin(tints[r]), 1.0)
        comb = nt.nodes.new("ShaderNodeCombineColor")
        for ch in ("Red", "Green", "Blue"):
            nt.links.new(comb.inputs[ch], cl.outputs["Result"])
        nt.links.new(mix.inputs["B"], comb.outputs["Color"])
        out = mix.outputs["Result"]
        if ao is not None:
            ta = nt.nodes.new("ShaderNodeTexImage"); ta.image = ao
            ta.image.colorspace_settings.name = "Non-Color"
            # floor the AO at 0.65: the garment maps carry near-black crease
            # specks (casualsuit01: min 0.08, 1st percentile 0.49) that read as
            # blotches once tinted; above the floor the folds keep their shading
            fl = nt.nodes.new("ShaderNodeMath"); fl.operation = "MAXIMUM"; fl.inputs[1].default_value = 0.65
            nt.links.new(fl.inputs[0], ta.outputs["Color"])
            m2 = nt.nodes.new("ShaderNodeMix"); m2.data_type = "RGBA"; m2.blend_type = "MULTIPLY"
            m2.inputs["Factor"].default_value = 0.85
            nt.links.new(m2.inputs["A"], out)
            nt.links.new(m2.inputs["B"], fl.outputs["Value"])
            out = m2.outputs["Result"]
        nt.links.new(b.inputs["Base Color"], out)
        nt.links.new(b.inputs["Alpha"], tx.outputs["Alpha"])
        b.inputs["Roughness"].default_value = ROUGH["shoe" if garment.startswith("shoes") else "cloth"]
        m["rh_role"] = "shoe" if garment.startswith("shoes") else "cloth"
        mats[r] = m
    o.data.materials.clear()
    for r in regions:
        o.data.materials.append(mats[r])
    me = o.data
    uv = me.uv_layers[0].data
    waist = (rig.matrix_world @ rig.data.bones["spine_01"].head_local).z
    # below the upper thigh it is trousers whatever the texture says (denim
    # under a jacket hem sampled too grey to pass the hue test on the pilot)
    legs = (rig.matrix_world @ rig.data.bones["thigh_l"].head_local).z - 0.10
    h, w = px.shape[:2]
    counts = {r: 0 for r in regions}
    for p in me.polygons:
        if split == "all":
            r = "all"
        elif split == "z":
            zc = sum((o.matrix_world @ me.vertices[v].co).z for v in p.vertices) / len(p.vertices)
            r = "upper" if zc > waist else "lower"
        elif "lower" in tints and sum((o.matrix_world @ me.vertices[v].co).z for v in p.vertices) / len(p.vertices) < legs:
            r = "lower"
        else:
            u = sum(uv[li].uv.x for li in p.loop_indices) / p.loop_total
            v = sum(uv[li].uv.y for li in p.loop_indices) / p.loop_total
            c = px[min(h - 1, max(0, int(v * h))), min(w - 1, max(0, int(u * w))), :3]
            mx, mn = c.max(), c.min()
            sat = (mx - mn) / mx if mx > 1e-4 else 0.0
            hue = _hue(c)
            if sat > 0.22 and 185 <= hue <= 255:
                r = "lower"
            elif sat > 0.30 and (hue <= 22 or hue >= 335) and "shirt" in tints:
                r = "shirt"
            else:
                r = "upper"
        p.material_index = regions.index(r)
        counts[r] += 1
    log("recoloured %s (%s split): %s" % (garment, split, counts))


def _hue(c):
    r, g, b = float(c[0]), float(c[1]), float(c[2])
    mx, mn = max(r, g, b), min(r, g, b)
    if mx - mn < 1e-6:
        return 0.0
    if mx == r:
        h = (g - b) / (mx - mn) % 6
    elif mx == g:
        h = (b - r) / (mx - mn) + 2
    else:
        h = (r - g) / (mx - mn) + 4
    return 60.0 * h


def tint_strands(o, colour):
    """Hair/brows: the strand texture's luminance x the person's hair colour;
    the strand alpha is kept for the masked material."""
    src = o.data.materials[0]
    diff = _tex_nodes(src)[0].image
    m = bpy.data.materials.new(o.name + ".tint")
    m.use_nodes = True
    nt = m.node_tree
    b = _principled(m)
    tx = nt.nodes.new("ShaderNodeTexImage"); tx.image = diff
    bw = nt.nodes.new("ShaderNodeRGBToBW")
    nt.links.new(bw.inputs["Color"], tx.outputs["Color"])
    mr = nt.nodes.new("ShaderNodeMapRange")
    mr.inputs["To Min"].default_value = 0.55; mr.inputs["To Max"].default_value = 1.35
    nt.links.new(mr.inputs["Value"], bw.outputs["Val"])
    mix = nt.nodes.new("ShaderNodeMix"); mix.data_type = "RGBA"; mix.blend_type = "MULTIPLY"
    mix.inputs["Factor"].default_value = 1.0
    mix.inputs["A"].default_value = (*hex_lin(colour), 1.0)
    comb = nt.nodes.new("ShaderNodeCombineColor")
    for ch in ("Red", "Green", "Blue"):
        nt.links.new(comb.inputs[ch], mr.outputs["Result"])
    nt.links.new(mix.inputs["B"], comb.outputs["Color"])
    nt.links.new(b.inputs["Base Color"], mix.outputs["Result"])
    nt.links.new(b.inputs["Alpha"], tx.outputs["Alpha"])
    b.inputs["Roughness"].default_value = ROUGH["hair"]
    m["rh_role"] = "hair"
    o.data.materials.clear()
    o.data.materials.append(m)


# ------------------------------------------------------- join + atlas bake
def bake_ready_mesh(rig, parts):
    """Freeze every visible part (mask/delete-group modifiers and shape keys
    applied at the rest pose), keep it skinned, join into one mesh."""
    objs = []
    for role, lst in parts.items():
        for o in lst:
            for m in o.data.materials:
                if m is not None and "rh_role" not in m:
                    m["rh_role"] = role
            arm = [m for m in o.modifiers if m.type == "ARMATURE"]
            for m in arm:
                m.show_viewport = False
                m.show_render = False
            tris = sum(len(p.vertices) - 2 for p in o.data.polygons)
            if role in ("cloth", "shoe") and tris > CLOTH_MAX_TRIS:
                # male_casualsuit01 alone is ~16.6k tris, which put everyone
                # wearing it over the art bible's 25k character budget.
                # Collapse keeps weights, UVs and the recolour regions.
                dec = o.modifiers.new("Budget", "DECIMATE")
                dec.ratio = CLOTH_MAX_TRIS / float(tris)
                # the coarser faces cut the corners of the leg's curve and the
                # boot rim poked through the trouser shin as a black blob;
                # 7 mm out along the normals puts the cloth back over it
                puff = o.modifiers.new("Puff", "DISPLACE")
                puff.mid_level = 0.0
                puff.strength = 0.007
                log("decimated %s %d -> ~%d tris (+7 mm puff)" % (o.name, tris, CLOTH_MAX_TRIS))
            bpy.ops.object.select_all(action="DESELECT")
            o.hide_set(False)
            o.select_set(True)
            bpy.context.view_layer.objects.active = o
            bpy.ops.object.convert(target="MESH")
            o = bpy.context.view_layer.objects.active
            for m in list(o.modifiers):
                o.modifiers.remove(m)
            am = o.modifiers.new("Armature", "ARMATURE")
            am.object = rig
            if o.data.uv_layers:
                o.data.uv_layers[0].name = "UVMap"
            objs.append(o)
    bpy.ops.object.select_all(action="DESELECT")
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    bpy.ops.object.join()
    body = bpy.context.view_layer.objects.active
    body.name = NAME
    body.data.name = NAME
    body.parent = rig
    for p in body.data.polygons:
        p.use_smooth = True
    return body


def atlas_uv(body):
    me = body.data
    src = me.uv_layers["UVMap"]
    # A FRESH non-overlapping unwrap, not MakeHuman's per-asset UVs packed
    # together: those can overlap (2026-10-13: jacket colour bled onto the
    # pilot's trousers through shared atlas texels). The source UVs are still
    # what the bake reads the original textures through.
    at = me.uv_layers.new(name="Atlas")
    me.uv_layers.active = at
    src.active_render = True
    bpy.ops.object.select_all(action="DESELECT")
    body.select_set(True)
    bpy.context.view_layer.objects.active = body
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project(angle_limit=math.radians(66.0), island_margin=0.003, area_weight=0.0,
                             correct_aspect=True, scale_to_bounds=False)
    bpy.ops.uv.select_all(action="SELECT")
    bpy.ops.uv.pack_islands(rotate=True, margin=0.004)
    bpy.ops.object.mode_set(mode="OBJECT")


def _swap_emission(body, value_of):
    """Temporary emission materials per slot: value_of(material) -> socket-or-value."""
    saved = [s.material for s in body.material_slots]
    for i, m in enumerate(saved):
        t = m.copy()
        nt = t.node_tree
        em = nt.nodes.new("ShaderNodeEmission")
        outn = next(n for n in nt.nodes if n.type == "OUTPUT_MATERIAL")
        src = value_of(m, t)
        if isinstance(src, (int, float)):
            em.inputs["Color"].default_value = (src, src, src, 1.0)
        else:
            nt.links.new(em.inputs["Color"], src)
        nt.links.new(outn.inputs["Surface"], em.outputs["Emission"])
        body.material_slots[i].material = t
    return saved


def _bake_into(body, img, bake_type, samples=1, pass_filter=None):
    sc = bpy.context.scene
    sc.cycles.samples = samples
    for s in body.material_slots:
        nt = s.material.node_tree
        tex = nt.nodes.new("ShaderNodeTexImage")
        tex.image = img
        for n in nt.nodes:
            n.select = False
        tex.select = True
        nt.nodes.active = tex
    bpy.ops.object.select_all(action="DESELECT")
    body.select_set(True)
    bpy.context.view_layer.objects.active = body
    if pass_filter:
        bpy.ops.object.bake(type=bake_type, pass_filter=pass_filter)
    else:
        bpy.ops.object.bake(type=bake_type)
    a = np.empty(img.size[0] * img.size[1] * 4, dtype=np.float32)
    img.pixels.foreach_get(a)
    return a.reshape(img.size[1], img.size[0], 4)


def bake_atlas(body, rig):
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"
    sc.cycles.device = "CPU"
    sc.render.bake.margin = 8
    sc.render.bake.use_clear = True
    if not sc.world:
        sc.world = bpy.data.worlds.new("bake")
    sc.world.light_settings.distance = 0.12

    def new(name):
        i = bpy.data.images.new(name, SIZE, SIZE, alpha=True, float_buffer=True)
        i.colorspace_settings.name = "Non-Color"
        return i

    # colour: emit the Base Color input (lighting-free, linear)
    def base_src(m, t):
        b = _principled(t)
        sock = b.inputs["Base Color"]
        return sock.links[0].from_socket if sock.is_linked else sock.default_value[0]
    saved = _swap_emission(body, base_src)
    col = _bake_into(body, new("col"), "EMIT")
    for i, m in enumerate(saved):
        body.material_slots[i].material = m

    def alpha_src(m, t):
        # Only hair and brows cut out. MakeHuman garment textures carry stray
        # zero-alpha patches (2026-10-13: square holes through the pilot's
        # jacket), and nothing else on a crew member should ever be see-through.
        if m.get("rh_role") != "hair":
            return 1.0
        b = _principled(t)
        sock = b.inputs["Alpha"]
        return sock.links[0].from_socket if sock.is_linked else float(sock.default_value)
    saved = _swap_emission(body, alpha_src)
    alp = _bake_into(body, new("alpha"), "EMIT")
    for i, m in enumerate(saved):
        body.material_slots[i].material = m

    saved = _swap_emission(body, lambda m, t: float(ROUGH.get(m.get("rh_role", "cloth"), 0.7)))
    rough = _bake_into(body, new("rough"), "EMIT")
    for i, m in enumerate(saved):
        body.material_slots[i].material = m

    saved = _swap_emission(body, lambda m, t: 1.0 if m.get("rh_role") == "skin" else 0.0)
    skin = _bake_into(body, new("skin"), "EMIT")
    for i, m in enumerate(saved):
        body.material_slots[i].material = m

    ao = _bake_into(body, new("ao"), "AO", samples=48)
    log("baked colour, alpha, roughness, skin mask, AO at %d" % SIZE)

    A = col[..., :3]
    occ = ao[..., 0:1]
    sk = skin[..., 0:1]
    strength = 0.30 * sk + 0.50 * (1 - sk)           # one AO treatment, gentler on skin
    A = A * (1.0 - strength * (1.0 - occ))
    srgb = np.where(A <= 0.0031308, A * 12.92, 1.055 * np.power(np.clip(A, 0, 1), 1 / 2.4) - 0.055)
    rgba = np.concatenate([np.clip(srgb, 0, 1), np.clip(alp[..., 0:1], 0, 1)], axis=2)
    mr = np.concatenate([occ, np.clip(rough[..., 0:1], 0.05, 1.0), np.zeros_like(occ)], axis=2)
    base_p = _save(rgba, os.path.join(OUT, NAME + "_BaseColor.png"))
    mr_p = _save(mr, os.path.join(OUT, NAME + "_MetallicRoughness.png"))
    return base_p, mr_p


def _save(arr, path):
    h, w, c = arr.shape
    if c == 3:
        arr = np.concatenate([arr, np.ones((h, w, 1))], axis=2)
    img = bpy.data.images.new(os.path.basename(path), w, h, alpha=True)
    img.colorspace_settings.name = "Non-Color"
    img.pixels.foreach_set(arr.astype(np.float32).ravel())
    img.filepath_raw = path
    img.file_format = "PNG"
    img.save()
    bpy.data.images.remove(img)
    return path


def final_material(body, base_p, mr_p):
    m = bpy.data.materials.new(NAME)
    m.use_nodes = True
    nt = m.node_tree
    b = _principled(m)
    tb = nt.nodes.new("ShaderNodeTexImage")
    tb.image = bpy.data.images.load(base_p); tb.image.name = NAME + "_BaseColor"
    tb.image.colorspace_settings.name = "sRGB"
    tb.image.alpha_mode = "CHANNEL_PACKED"
    tm = nt.nodes.new("ShaderNodeTexImage")
    tm.image = bpy.data.images.load(mr_p); tm.image.name = NAME + "_MetallicRoughness"
    tm.image.colorspace_settings.name = "Non-Color"
    sep = nt.nodes.new("ShaderNodeSeparateColor")
    nt.links.new(b.inputs["Base Color"], tb.outputs["Color"])
    # alpha CLIP, not blend: the glTF exporter writes alphaMode MASK when the
    # alpha passes through a Greater Than node (the game uses a masked material)
    clip = nt.nodes.new("ShaderNodeMath")
    clip.operation = "GREATER_THAN"
    clip.inputs[1].default_value = 0.5
    nt.links.new(clip.inputs[0], tb.outputs["Alpha"])
    nt.links.new(b.inputs["Alpha"], clip.outputs["Value"])
    nt.links.new(sep.inputs["Color"], tm.outputs["Color"])
    nt.links.new(b.inputs["Roughness"], sep.outputs["Green"])
    nt.links.new(b.inputs["Metallic"], sep.outputs["Blue"])
    try:
        m.surface_render_method = "DITHERED"
    except Exception:
        pass
    me = body.data
    me.materials.clear()
    me.materials.append(m)
    # the atlas becomes the only UV set (TEXCOORD_0 in the GLB)
    me.uv_layers.remove(me.uv_layers["UVMap"])
    me.uv_layers["Atlas"].name = "UVMap"


def fit_height(rig, body, target):
    dg = bpy.context.evaluated_depsgraph_get()
    zs = [(body.matrix_world @ v.co).z for v in body.evaluated_get(dg).to_mesh().vertices]
    h = max(zs) - min(zs)
    s = target / h
    bpy.ops.object.select_all(action="DESELECT")
    rig.select_set(True)
    body.select_set(True)
    bpy.context.view_layer.objects.active = rig
    rig.scale = (s, s, s)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    log("height %.3f m -> %.3f m (scale %.4f)" % (h, target, s))
    return h * s


def main():
    spec = CREW[FACE]
    os.makedirs(OUT, exist_ok=True)
    bm, rig, parts = build(spec)
    rig.name = NAME + "_Rig"
    body = bake_ready_mesh(rig, parts)
    bpy.data.objects.remove(bm)
    height = fit_height(rig, body, spec["height_m"])
    atlas_uv(body)
    base_p, mr_p = bake_atlas(body, rig)
    final_material(body, base_p, mr_p)
    tris = sum(len(p.vertices) - 2 for p in body.data.polygons)
    clips = []
    if not NO_CLIPS:
        clips = [a.name for a in rh_crew_clips.author_all(rig, height, log=log)]
    for o in list(bpy.context.scene.objects):
        if o not in (rig, body):
            bpy.data.objects.remove(o)
    bpy.context.scene.frame_start, bpy.context.scene.frame_end = 1, 49
    glb = os.path.join(OUT, NAME + ".glb")
    bpy.ops.object.select_all(action="DESELECT")
    rig.select_set(True)
    body.select_set(True)
    bpy.ops.export_scene.gltf(filepath=glb, export_format="GLB", use_selection=True, export_yup=True,
                              export_animations=not NO_CLIPS, export_animation_mode="ACTIONS",
                              export_skins=True, export_apply=False)
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT, NAME + ".blend"))
    rep = dict(face=FACE, height_m=round(height, 3), tris=tris, verts=len(body.data.vertices),
               bones=len(rig.data.bones), clips=clips, glb=glb)
    json.dump(rep, open(os.path.join(OUT, "report.json"), "w"), indent=1)
    log("DONE %s" % json.dumps(rep))


main()
