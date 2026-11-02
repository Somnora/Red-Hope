"""Author M_RH_RouteUnlit: an UNLIT, OPAQUE colour material for the Routes overlay's trade ribbons (E8 fix round 2, 2026-10-31).

    RH_REPORT=<file> UnrealEditor-Cmd <proj> -run=pythonscript \
        -script=$PWD/scripts/unreal/rh_make_route_unlit.py -unattended -nosound -stdout

Why: the review measured the ribbons against the legend. Glazed with M_DomeGlass (lit translucent, opacity 0.7, glow 0.6) the
"open" ribbon read a pale green-white over the orange ground (hue 124-136, lightness 0.84-0.90), not the legend's teal (hue 174).
A probe with M_Graybox (opaque, a black Tint and the legend colour as its Emissive) was still near white: the sun lights it whatever
its Tint, so the emissive could not carry the colour. Unlit and opaque, the ribbon is drawn in exactly the colour the visualizer
gives it, by day and night, over any ground.

The graph: the vector parameter "Tint" (the legend's linear colour, set per status bin by URHTradeVisualizerSubsystem) over the
camera's exposure (EyeAdaptationInverse, as M_RH_TextUnlit does) is the emissive. Two-sided, so a draped quad reads from any side.

Idempotent: rebuilds the material in place.
"""
import os
import unreal

PATH = "/Game/RedHope/Art/M_RH_RouteUnlit"
MEL = unreal.MaterialEditingLibrary
report = []

if unreal.EditorAssetLibrary.does_asset_exist(PATH):
    mat = unreal.EditorAssetLibrary.load_asset(PATH)
    MEL.delete_all_material_expressions(mat)
    report.append("rebuilt existing " + PATH)
else:
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    mat = tools.create_asset("M_RH_RouteUnlit", "/Game/RedHope/Art", unreal.Material, unreal.MaterialFactoryNew())
    report.append("created " + PATH)

mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
mat.set_editor_property("two_sided", True)

tint = MEL.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -500, 0)
tint.set_editor_property("parameter_name", "Tint")
tint.set_editor_property("default_value", unreal.LinearColor(0.30, 0.90, 0.82, 1.0))
inv = MEL.create_material_expression(mat, unreal.MaterialExpressionEyeAdaptationInverse, -250, 0)
try:
    names = list(MEL.get_material_expression_input_names(inv))
except Exception as e:  # older API
    names = ["(unknown: %s)" % e]
report.append("EyeAdaptationInverse inputs: %s" % names)
pin = next((n for n in names if "light" in n.lower()), "LightValueInput")
ok_i = MEL.connect_material_expressions(tint, "", inv, pin)
ok_e = MEL.connect_material_property(inv, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
report.append("Tint -> EyeAdaptationInverse.%s: %s; -> emissive: %s" % (pin, ok_i, ok_e))

MEL.recompile_material(mat)
ok = unreal.EditorAssetLibrary.save_asset(PATH, only_if_is_dirty=False)
report.append("saved=%s" % ok)

out = os.environ.get("RH_REPORT")
if out:
    with open(out, "w") as f:
        f.write("\n".join(report) + "\n")
print("\n".join(report))
