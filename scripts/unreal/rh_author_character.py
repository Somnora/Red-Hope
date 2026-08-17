"""Author M_RH_Character and put every crew/robot walker on it. Compile-free.

    RH_REPORT=/tmp/char.txt UnrealEditor-Cmd <proj> -run=pythonscript \
      -script=$PWD/scripts/unreal/rh_author_character.py -unattended -nosound -stdout

WHY THIS EXISTS. Every skeletal mesh in the project was rendering with UE's
DEFAULT material. Their materials descend from /InterchangeAssets/.../M_GLTF,
which does not carry bUsedWithSkeletalMesh - and a material without that flag is
REFUSED at render time and swapped for the default. The meshes had textures, the
materials had those textures bound, and none of it reached the screen: that is
the actual reason the characters read as unfinished grey statues.

Flipping the flag on M_GLTF works but edits an ENGINE PLUGIN asset - outside the
repo, lost on an engine update, invisible to anyone else building this project.
So the project owns its own character material instead, and every walker is put
onto an MI_<name> of it carrying that walker's own texture.

Mirrors M_RH_Master (art bible section 3) minus PoweredState, which is a
building concept.
"""
import os

import unreal

MEL = unreal.MaterialEditingLibrary
OUT = os.environ.get("RH_REPORT", "/tmp/rh_character.txt")
PKG = "/Game/RedHope/Art/M_RH_Character"
DEFAULT_TEX = "/Game/RedHope/Art/Mars_Regolith_Texture.Mars_Regolith_Texture"

log = []


def link(a, out_name, b, in_name):
    ok = MEL.connect_material_expressions(a, out_name, b, in_name)
    if not ok:
        log.append("  ! connect %s.%s -> %s.%s FAILED"
                   % (type(a).__name__, out_name, type(b).__name__, in_name))
    return ok


def node(mat, cls, x, y, **props):
    e = MEL.create_material_expression(mat, cls, x, y)
    for k, v in props.items():
        try:
            e.set_editor_property(k, v)
        except Exception as ex:
            log.append("  ! %s.%s: %s" % (cls.__name__, k, ex))
    return e


def author():
    mat = unreal.load_asset(PKG)
    if not mat:
        tools = unreal.AssetToolsHelpers.get_asset_tools()
        mat = tools.create_asset("M_RH_Character", "/Game/RedHope/Art",
                                 unreal.Material, unreal.MaterialFactoryNew())
        log.append("M_RH_Character: CREATED")
    else:
        log.append("M_RH_Character: rebuilding")
    MEL.delete_all_material_expressions(mat)

    base = node(mat, unreal.MaterialExpressionTextureSampleParameter2D, -800, -200,
                parameter_name="BaseTex",
                sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    tex = unreal.load_asset(DEFAULT_TEX)
    if tex:
        base.set_editor_property("texture", tex)
    tint = node(mat, unreal.MaterialExpressionVectorParameter, -800, 120,
                parameter_name="Tint", default_value=unreal.LinearColor(1, 1, 1, 1))
    rough = node(mat, unreal.MaterialExpressionScalarParameter, -800, 250,
                 parameter_name="Rough", default_value=0.68)
    metal = node(mat, unreal.MaterialExpressionScalarParameter, -800, 350,
                 parameter_name="Metallic", default_value=0.0)
    floor = node(mat, unreal.MaterialExpressionScalarParameter, -800, 450,
                 parameter_name="EmissiveFloor", default_value=0.08)

    # Per-pixel metal/roughness and normal. Dumped 2026-08-17: this master had
    # only ONE texture parameter (BaseTex) and "Normal <- <none>", which means
    # every walker's imported *_metallic-*_roughness map was INERT - present on
    # disk, bound by nothing - and the crew shaded off a flat scalar roughness
    # with no surface normal. That is the shading half of the director's
    # "human models seem incomplete or blotchy"; the subsurface-shader fix
    # earlier this session was the other half. glTF packing: G=rough, B=metal.
    # Both paths are opt-in (UseMRTex / UseNormTex default 0) so the 21
    # existing MI_RH_Walker_* instances are bit-identical until wired.
    mr_tex = node(mat, unreal.MaterialExpressionTextureSampleParameter2D, -800, 620,
                  parameter_name="MRTex",
                  sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    mr_default = unreal.load_asset("/Game/RedHope/Art/Masks/T_RH_MaskWhite")
    if mr_default:
        mr_tex.set_editor_property("texture", mr_default)
    use_mr = node(mat, unreal.MaterialExpressionScalarParameter, -800, 530,
                  parameter_name="UseMRTex", default_value=0.0)
    rough_mix = node(mat, unreal.MaterialExpressionLinearInterpolate, -450, 250)
    link(rough, "", rough_mix, "A")
    link(mr_tex, "G", rough_mix, "B")
    link(use_mr, "", rough_mix, "Alpha")
    metal_mix = node(mat, unreal.MaterialExpressionLinearInterpolate, -450, 350)
    link(metal, "", metal_mix, "A")
    link(mr_tex, "B", metal_mix, "B")
    link(use_mr, "", metal_mix, "Alpha")

    norm_tex = node(mat, unreal.MaterialExpressionTextureSampleParameter2D, -800, 800,
                    parameter_name="NormTex",
                    sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    norm_default = unreal.load_asset("/Engine/EngineMaterials/DefaultNormal")
    if norm_default:
        norm_tex.set_editor_property("texture", norm_default)
    use_norm = node(mat, unreal.MaterialExpressionScalarParameter, -800, 710,
                    parameter_name="UseNormTex", default_value=0.0)
    flat_norm = node(mat, unreal.MaterialExpressionConstant3Vector, -800, 900,
                     constant=unreal.LinearColor(0.0, 0.0, 1.0, 1.0))
    norm_mix = node(mat, unreal.MaterialExpressionLinearInterpolate, -450, 800)
    link(flat_norm, "", norm_mix, "A")
    link(norm_tex, "", norm_mix, "B")
    link(use_norm, "", norm_mix, "Alpha")

    tinted = node(mat, unreal.MaterialExpressionMultiply, -450, -150)
    MEL.connect_material_expressions(base, "", tinted, "A")
    MEL.connect_material_expressions(tint, "", tinted, "B")
    em = node(mat, unreal.MaterialExpressionMultiply, -450, 380)
    MEL.connect_material_expressions(tinted, "", em, "A")
    MEL.connect_material_expressions(floor, "", em, "B")

    MEL.connect_material_property(tinted, "", unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(rough_mix, "", unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.connect_material_property(metal_mix, "", unreal.MaterialProperty.MP_METALLIC)
    MEL.connect_material_property(norm_mix, "", unreal.MaterialProperty.MP_NORMAL)
    MEL.connect_material_property(em, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    # the whole point:
    MEL.set_base_material_usage(mat, unreal.MaterialUsage.MATUSAGE_SKELETAL_MESH)
    errs = MEL.recompile_material(mat)
    log.append("  expressions=%d compile_errors=%d skeletal_usage=%s" % (
        MEL.get_num_material_expressions(mat), len(errs),
        MEL.has_material_usage(mat, unreal.MaterialUsage.MATUSAGE_SKELETAL_MESH)))
    for e in errs:
        log.append("  ERROR %s" % e)
    unreal.EditorAssetLibrary.save_loaded_asset(mat, only_if_is_dirty=False)
    return mat


def base_colour_texture_for(mesh_pkg):
    """Textures sit in a sibling Textures/ folder; skip the MR/normal maps."""
    root = mesh_pkg.rsplit("/", 1)[0]           # .../SkeletalMeshes
    root = root.rsplit("/", 1)[0]               # .../RH_Walker_<face>
    folder = root + "/Textures"
    if not unreal.EditorAssetLibrary.does_directory_exist(folder):
        return None
    best = None
    for p in unreal.EditorAssetLibrary.list_assets(folder, recursive=False, include_folder=False):
        n = p.rsplit("/", 1)[-1].split(".")[0].lower()
        if "metallic" in n or "roughness" in n or "normal" in n:
            continue
        a = unreal.load_asset(p)
        if isinstance(a, unreal.Texture2D):
            best = a
            break
    return best


def main():
    master = author()
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    assets = reg.get_assets_by_path("/Game/RedHope/Art", recursive=True)
    tools = unreal.AssetToolsHelpers.get_asset_tools()

    wired = 0
    total = 0
    for a in assets:
        cls = str(a.asset_class_path.asset_name) if hasattr(a, "asset_class_path") else str(a.asset_class)
        if cls != "SkeletalMesh":
            continue
        total += 1
        pkg = str(a.package_name)
        sk = unreal.load_asset(pkg)
        if not sk:
            continue
        name = pkg.rsplit("/", 1)[-1]
        mi_name = "MI_%s" % name
        mi_dir = pkg.rsplit("/", 1)[0]
        mi_path = "%s/%s" % (mi_dir, mi_name)

        mi = unreal.load_asset(mi_path)
        if not mi:
            mi = tools.create_asset(mi_name, mi_dir, unreal.MaterialInstanceConstant,
                                    unreal.MaterialInstanceConstantFactoryNew())
        MEL.set_material_instance_parent(mi, master)
        tex = base_colour_texture_for(pkg)
        if tex:
            MEL.set_material_instance_texture_parameter_value(mi, "BaseTex", tex)
        MEL.update_material_instance(mi)
        unreal.EditorAssetLibrary.save_loaded_asset(mi, only_if_is_dirty=False)

        mats = sk.get_editor_property("materials")
        changed = False
        for m in mats:
            if m.get_editor_property("material_interface") != mi:
                m.set_editor_property("material_interface", mi)
                changed = True
        if changed:
            sk.set_editor_property("materials", mats)
            unreal.EditorAssetLibrary.save_loaded_asset(sk, only_if_is_dirty=False)
        wired += 1
        log.append("  %-34s -> %s  tex=%s" % (name, mi_name, tex.get_name() if tex else "<default>"))

    log.insert(0, "wired %d/%d skeletal meshes onto M_RH_Character" % (wired, total))
    with open(OUT, "w") as fh:
        fh.write("\n".join(log) + "\n")


main()
