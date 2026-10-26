"""Author M_RH_TextUnlit: an UNLIT text material for the deep view's world labels (P8c, 2026-10-26).

    RH_REPORT=<file> UnrealEditor-Cmd <proj> -run=pythonscript \
        -script=$PWD/scripts/unreal/rh_make_text_unlit.py -unattended -nosound -stdout

Why: UTextRenderComponent's default material (/Engine/EngineMaterials/DefaultTextMaterialOpaque) is DefaultLit. The section's
lift, tram and robot labels stand on the cut plane underground, where the scene's light does not reach once they are a long way
from the dig (the 20-floor bench's tram labels, 100-300 m out): a teal "Tram 1 ..." label then rendered near black, invisible on
the dark rock card and showing only where it crossed a bright plate. Unlit, the label is drawn in exactly the colour the
visualizer gives it (SetTextRenderColor, the vertex colour), at any depth and any sun.

The graph: the font page's distance field (the engine's RobotoDistanceField, the TextRender default font; its alpha is the
field) is the opacity mask at the 0.5 contour, and the vertex colour (decoded from sRGB) over the camera's exposure is the emissive. Masked, so it writes depth and sorts with
the section's boxes like the default material did. The font parameter is named "Font", which UTextRenderComponent fills with
the label's own font page per section (GetAllFontParameterInfo).

Idempotent: rebuilds the material in place.
"""
import os
import unreal

PATH = "/Game/RedHope/Art/M_RH_TextUnlit"
MEL = unreal.MaterialEditingLibrary
report = []

if unreal.EditorAssetLibrary.does_asset_exist(PATH):
    mat = unreal.EditorAssetLibrary.load_asset(PATH)
    MEL.delete_all_material_expressions(mat)
    report.append("rebuilt existing " + PATH)
else:
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    mat = tools.create_asset("M_RH_TextUnlit", "/Game/RedHope/Art", unreal.Material, unreal.MaterialFactoryNew())
    report.append("created " + PATH)

mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
mat.set_editor_property("opacity_mask_clip_value", 0.5)
mat.set_editor_property("two_sided", False)

font = unreal.load_asset("/Engine/EngineFonts/RobotoDistanceField")
report.append("font=%s" % (font.get_path_name() if font else None))

sample = MEL.create_material_expression(mat, unreal.MaterialExpressionFontSampleParameter, -500, 0)
sample.set_editor_property("parameter_name", "Font")
if font:
    sample.set_editor_property("font", font)
sample.set_editor_property("font_texture_page", 0)
vcol = MEL.create_material_expression(mat, unreal.MaterialExpressionVertexColor, -500, -200)

# The emissive is divided by the camera's exposure (EyeAdaptationInverse): unlit, the vertex colour would otherwise be scaled by
# the auto exposure like any light (the first capture drew the teal tram labels near white and the dark lift labels pale).
inv = MEL.create_material_expression(mat, unreal.MaterialExpressionEyeAdaptationInverse, -250, -200)
try:
    names = list(MEL.get_material_expression_input_names(inv))
except Exception as e:  # older API
    names = ["(unknown: %s)" % e]
report.append("EyeAdaptationInverse inputs: %s" % names)
pin = next((n for n in names if "light" in n.lower()), "LightValueInput")
# The visualizer sets the colour as an sRGB FColor (ToFColor(true)) and UTextRenderComponent hands those bytes to the shader as
# they are (FDynamicMeshVertex, UNORM): decoded here (x^2.2), or an unlit label draws washed out (the second capture: teal near
# white, the lift labels' dark brown a pale grey on the sky).
decode = MEL.create_material_expression(mat, unreal.MaterialExpressionPower, -380, -200)
decode.set_editor_property("const_exponent", 2.2)
ok_d = MEL.connect_material_expressions(vcol, "", decode, "Base")
ok_i = MEL.connect_material_expressions(decode, "", inv, pin)
report.append("vertex colour -> x^2.2: %s -> EyeAdaptationInverse.%s: %s" % (ok_d, pin, ok_i))
ok_e = MEL.connect_material_property(inv, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
ok_m = MEL.connect_material_property(sample, "A", unreal.MaterialProperty.MP_OPACITY_MASK)
report.append("emissive<-vertex colour: %s, opacity mask<-font alpha: %s" % (ok_e, ok_m))

MEL.recompile_material(mat)
ok = unreal.EditorAssetLibrary.save_asset(PATH, only_if_is_dirty=False)
report.append("saved=%s" % ok)

out = os.environ.get("RH_REPORT")
if out:
    with open(out, "w") as f:
        f.write("\n".join(report) + "\n")
print("\n".join(report))
