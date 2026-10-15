"""Author M_DomeGlass: the translucent pane material the greenhouse dome's
parametric shell is glazed with (Agri Gate C, 2026-10-15).

    RH_REPORT=<file> UnrealEditor-Cmd <proj> -run=pythonscript \
        -script=$PWD/scripts/unreal/rh_make_glass.py -unattended -nosound -stdout

Idempotent: rebuilds the material in place. Parameters the visualizer drives
through a MID: Tint (vector), Opacity (scalar), Glow (scalar, emissive gain on
Tint - 0 by day).
"""
import os
import unreal

PATH = "/Game/RedHope/Art/M_DomeGlass"
MEL = unreal.MaterialEditingLibrary
report = []

if unreal.EditorAssetLibrary.does_asset_exist(PATH):
    mat = unreal.EditorAssetLibrary.load_asset(PATH)
    MEL.delete_all_material_expressions(mat)
    report.append("rebuilt existing " + PATH)
else:
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    mat = tools.create_asset("M_DomeGlass", "/Game/RedHope/Art", unreal.Material, unreal.MaterialFactoryNew())
    report.append("created " + PATH)

mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
mat.set_editor_property("two_sided", True)
# The dome glazes with instanced panes (one ISM per dome): without this flag
# the renderer swaps in the default material (first capture, 2026-10-15).
mat.set_editor_property("used_with_instanced_static_meshes", True)
# Per-pixel surface lighting so the sun glints off the panes; the volumetric
# default would ignore Specular and read as tinted fog.
try:
    mat.set_editor_property("translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
except Exception as e:  # older enum spelling
    report.append("lighting mode left default: %s" % e)


def vec(name, x, y, default):
    e = MEL.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, x, y)
    e.set_editor_property("parameter_name", name)
    e.set_editor_property("default_value", default)
    return e


def scal(name, x, y, default):
    e = MEL.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, x, y)
    e.set_editor_property("parameter_name", name)
    e.set_editor_property("default_value", default)
    return e


tint = vec("Tint", -600, -100, unreal.LinearColor(0.58, 0.74, 0.80, 1.0))
opacity = scal("Opacity", -600, 100, 0.2)
rough = scal("Roughness", -600, 200, 0.06)
spec = scal("Specular", -600, 300, 0.9)
glow = scal("Glow", -600, 0, 0.0)
emis = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -300, 0)
MEL.connect_material_expressions(tint, "", emis, "A")
MEL.connect_material_expressions(glow, "", emis, "B")

MEL.connect_material_property(tint, "", unreal.MaterialProperty.MP_BASE_COLOR)
MEL.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)
MEL.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
MEL.connect_material_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)
MEL.connect_material_property(emis, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

MEL.recompile_material(mat)
ok = unreal.EditorAssetLibrary.save_asset(PATH, only_if_is_dirty=False)
report.append("saved=%s" % ok)

out = os.environ.get("RH_REPORT")
if out:
    with open(out, "w") as f:
        f.write("\n".join(report) + "\n")
print("\n".join(report))
