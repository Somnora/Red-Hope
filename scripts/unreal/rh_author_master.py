"""Author /Game/RedHope/Art/M_RH_Master from Python — no compile required.

    UnrealEditor-Cmd <proj> -run=pythonscript \
      -script=$PWD/scripts/unreal/rh_author_master.py -unattended -nosound -stdout

UMaterialEditingLibrary is a UBlueprintFunctionLibrary, so the whole material
graph API is exposed to Python. That makes the master material iterable in
seconds instead of once per director compile, which is why authoring moved here
from RHArtWireCommandlet::AuthorMaster (that C++ path still works and stays as
the bootstrap; this script is the one to edit from now on).

Rebuilding is safe for existing instances: MI_<name> overrides bind by parameter
NAME, so they survive a graph rebuild as long as the names below do not change.

Graph:
  BaseColor = lerp(BaseTex, AccentColor, AccentAmount)
  Metallic  = Metallic
  Roughness = Rough
  Emissive  = EmissiveColor * EmissiveAmount * PoweredState * Pulse * EmissiveMask(uv)
              * GlowScale (MPC_Atmosphere, driven by rh.Glow)
              + BaseColor * EmissiveFloor

EmissiveMask defaults to T_RH_MaskWhite (8x8 white, linear grayscale), which
makes it a no-op until a model's MI assigns a real mask - the 2026-08-14 W2
finding was that the UNMASKED glow term buried every machine's paint under a
flat HDR wash (HeavyForge at 0.22 emitted ~1.3 orange over the whole hull).
The mask confines the glow to windows, furnace mouths and indicator strips;
pulse and PoweredState ride through it, so the throb becomes blinking lights
instead of a breathing hull and power-off still reads.

Pulse = 1 - PulseDepth * (0.5 - 0.5*sin(Time * PulseSpeed))
so PulseDepth 0 (the default) is a perfect no-op, and because the pulse rides
*through* PoweredState an unpowered machine goes dark AND still for free — no
tick, no components, and it works on instanced static meshes.
"""
import os

import unreal

MASTER_PKG = "/Game/RedHope/Art/M_RH_Master"
DEFAULT_BASE_TEX = "/Game/RedHope/Art/Mars_Regolith_Texture.Mars_Regolith_Texture"
DEFAULT_MASK_TEX = "/Game/RedHope/Art/Masks/T_RH_MaskWhite.T_RH_MaskWhite"
GLOW_COLLECTION = "/Game/RedHope/Sky/MPC_Atmosphere.MPC_Atmosphere"
OUT = os.environ.get("RH_REPORT", "/tmp/rh_master.txt")

MEL = unreal.MaterialEditingLibrary
log = []


def rec(s):
    log.append(str(s))


def node(mat, cls, x, y, **props):
    e = MEL.create_material_expression(mat, cls, x, y)
    for k, v in props.items():
        try:
            e.set_editor_property(k, v)
        except Exception as ex:
            rec("  ! set %s on %s failed: %s" % (k, cls.__name__, ex))
    return e


def link(a, out_name, b, in_name):
    ok = MEL.connect_material_expressions(a, out_name, b, in_name)
    if not ok:
        rec("  ! connect %s.%s -> %s.%s FAILED" % (type(a).__name__, out_name, type(b).__name__, in_name))
    return ok


def main():
    mat = unreal.load_asset(MASTER_PKG)
    created = False
    if not mat:
        tools = unreal.AssetToolsHelpers.get_asset_tools()
        mat = tools.create_asset("M_RH_Master", "/Game/RedHope/Art",
                                 unreal.Material, unreal.MaterialFactoryNew())
        created = True
    rec("master: %s" % ("CREATED" if created else "rebuilding existing"))

    MEL.delete_all_material_expressions(mat)

    base = node(mat, unreal.MaterialExpressionTextureSampleParameter2D, -900, -300,
                parameter_name="BaseTex", sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    tex = unreal.load_asset(DEFAULT_BASE_TEX)
    if tex:
        base.set_editor_property("texture", tex)

    accent = node(mat, unreal.MaterialExpressionVectorParameter, -900, 0,
                  parameter_name="AccentColor", default_value=unreal.LinearColor(0.5, 0.5, 0.5, 1.0))
    accent_amt = node(mat, unreal.MaterialExpressionScalarParameter, -900, 130,
                      parameter_name="AccentAmount", default_value=0.0)
    metallic = node(mat, unreal.MaterialExpressionScalarParameter, -900, 230,
                    parameter_name="Metallic", default_value=0.0)
    rough = node(mat, unreal.MaterialExpressionScalarParameter, -900, 330,
                 parameter_name="Rough", default_value=0.75)
    em_col = node(mat, unreal.MaterialExpressionVectorParameter, -900, 430,
                  parameter_name="EmissiveColor", default_value=unreal.LinearColor(0, 0, 0, 1))
    em_amt = node(mat, unreal.MaterialExpressionScalarParameter, -900, 560,
                  parameter_name="EmissiveAmount", default_value=0.0)
    powered = node(mat, unreal.MaterialExpressionScalarParameter, -900, 660,
                   parameter_name="PoweredState", default_value=1.0)
    floor = node(mat, unreal.MaterialExpressionScalarParameter, -900, 760,
                 parameter_name="EmissiveFloor", default_value=0.08)
    depth = node(mat, unreal.MaterialExpressionScalarParameter, -900, 860,
                 parameter_name="PulseDepth", default_value=0.0)
    speed = node(mat, unreal.MaterialExpressionScalarParameter, -900, 960,
                 parameter_name="PulseSpeed", default_value=2.0)
    mask = node(mat, unreal.MaterialExpressionTextureSampleParameter2D, -900, 1160,
                parameter_name="EmissiveMask",
                sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE)
    mask_tex = unreal.load_asset(DEFAULT_MASK_TEX)
    if mask_tex:
        mask.set_editor_property("texture", mask_tex)
    else:
        rec("  ! default mask %s missing - run rh_import_masks.py first" % DEFAULT_MASK_TEX)

    # BaseColor
    albedo = node(mat, unreal.MaterialExpressionLinearInterpolate, -520, -200)
    link(base, "", albedo, "A")
    link(accent, "", albedo, "B")
    link(accent_amt, "", albedo, "Alpha")

    # Pulse = 1 - PulseDepth * (0.5 - 0.5*sin(Time*PulseSpeed))
    time = node(mat, unreal.MaterialExpressionTime, -760, 1060)
    t_mul = node(mat, unreal.MaterialExpressionMultiply, -600, 1000)
    link(time, "", t_mul, "A")
    link(speed, "", t_mul, "B")
    sine = node(mat, unreal.MaterialExpressionSine, -450, 1000)
    # A single-input expression reports its pin as "None"; the connect API wants
    # an empty string for it.
    link(t_mul, "", sine, "")
    half = node(mat, unreal.MaterialExpressionMultiply, -320, 1000)
    link(sine, "", half, "A")
    half.set_editor_property("const_b", 0.5)
    dip = node(mat, unreal.MaterialExpressionSubtract, -190, 960)
    dip.set_editor_property("const_a", 0.5)
    link(half, "", dip, "B")
    dip_amt = node(mat, unreal.MaterialExpressionMultiply, -60, 960)
    link(depth, "", dip_amt, "A")
    link(dip, "", dip_amt, "B")
    pulse = node(mat, unreal.MaterialExpressionSubtract, 70, 960)
    pulse.set_editor_property("const_a", 1.0)
    link(dip_amt, "", pulse, "B")

    # Emissive
    glow_a = node(mat, unreal.MaterialExpressionMultiply, -520, 430)
    link(em_col, "", glow_a, "A")
    link(em_amt, "", glow_a, "B")
    glow_b = node(mat, unreal.MaterialExpressionMultiply, -360, 500)
    link(glow_a, "", glow_b, "A")
    link(powered, "", glow_b, "B")
    glow_c = node(mat, unreal.MaterialExpressionMultiply, -200, 560)
    link(glow_b, "", glow_c, "A")
    link(pulse, "", glow_c, "B")
    # The W2 fix: glow only where the mask says. White default = old behavior.
    glow_d = node(mat, unreal.MaterialExpressionMultiply, -60, 500)
    link(glow_c, "", glow_d, "A")
    link(mask, "", glow_d, "B")
    # Global dial (rh.Glow -> MPC_Atmosphere.GlowScale, default 1). Lets the
    # player switch the machine-glow assist off once they have built their own
    # lighting. Real lights - hab ceiling lamps, Floodmasts - are untouched by
    # this, and so is the EmissiveFloor ambient lift below.
    glow_scale = node(mat, unreal.MaterialExpressionCollectionParameter, -260, 380,
                      parameter_name="GlowScale")
    coll = unreal.load_asset(GLOW_COLLECTION)
    if coll:
        glow_scale.set_editor_property("collection", coll)
    else:
        rec("  ! %s missing - run rh_add_mpc_glow.py first" % GLOW_COLLECTION)
    glow_e = node(mat, unreal.MaterialExpressionMultiply, 60, 440)
    link(glow_d, "", glow_e, "A")
    link(glow_scale, "", glow_e, "B")
    floor_mul = node(mat, unreal.MaterialExpressionMultiply, -360, 720)
    link(albedo, "", floor_mul, "A")
    link(floor, "", floor_mul, "B")
    emissive = node(mat, unreal.MaterialExpressionAdd, 200, 640)
    link(glow_e, "", emissive, "A")
    link(floor_mul, "", emissive, "B")

    MEL.connect_material_property(albedo, "", unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(metallic, "", unreal.MaterialProperty.MP_METALLIC)
    MEL.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.connect_material_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    try:
        MEL.set_base_material_usage(mat, unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
        MEL.set_base_material_usage(mat, unreal.MaterialUsage.MATUSAGE_SKELETAL_MESH)
    except Exception as e:
        rec("usage flags: %s" % e)

    errors = MEL.recompile_material(mat)
    rec("expressions: %d" % MEL.get_num_material_expressions(mat))
    rec("compile errors: %d" % len(errors))
    for e in errors:
        rec("  ERROR %s" % e)
    unreal.EditorAssetLibrary.save_loaded_asset(mat, only_if_is_dirty=False)
    rec("saved %s" % MASTER_PKG)

    with open(OUT, "w") as fh:
        fh.write("\n".join(log) + "\n")


main()
