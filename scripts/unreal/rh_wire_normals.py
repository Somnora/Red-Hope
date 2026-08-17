"""Import derived normal maps and wire them into their model MIs.

    RH_NRM_DIR=<dir of T_<name>_Normal.png> RH_REPORT=<txt> \
      UnrealEditor-Cmd <proj> -run=pythonscript \
      -script=$PWD/scripts/unreal/rh_wire_normals.py -unattended -nosound -stdout

Companion to scripts/gpu/rh_derive_normals.py. That script produces the maps;
this one lands them in UE with the right compression and binds them.

TWO RULES THIS ENCODES, both learned the hard way in this repo:

1. TC_NORMALMAP + sRGB OFF, verified by READ-BACK. A normal imported as colour
   (TC_DEFAULT, sRGB on) decodes wrong and shades WORSE than no normal at all -
   which is why ApplySurface in RHColonyVisualizerSubsystem refuses to bind a
   normal whose compression is not TC_Normalmap. The same skepticism applies
   here: set it, save it, read it back, and report the mismatch rather than
   assuming the setter worked.

2. FIND the MI, never construct its path. Walker MIs live in SkeletalMeshes/,
   Models2 building MIs live in StaticMeshes/, Props2 prop MIs live in
   Materials/ - three conventions in one tree. Guessing the folder cost two
   silent no-op passes already, so this searches each asset's own folder tree
   for MI_<name> and says so when it finds nothing.

Also, because UseNormTex defaults to 0 in both masters, binding the texture is
not enough - the scalar must be turned on per instance. Both are set, and both
are read back.
"""
import os

import unreal

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()

NRM_DIR = os.environ.get("RH_NRM_DIR", "")
DEST = "/Game/RedHope/Art/Normals"
STRENGTH = float(os.environ.get("RH_NORM_STRENGTH", "1.0"))

# name -> (MI search root, MI leaf name). FIVE folder conventions live in this
# one art tree - Props2/<n>/Materials, Models2/<n>/StaticMeshes,
# Models/<n> (flat), Dress/RH_<n>/Materials, Machines/RH_<n>/StaticMeshes -
# which is exactly why nothing here constructs a path. Order matters only in
# that the first hit wins; the names do not collide across these roots.
ROOTS = [
    ("/Game/RedHope/Art/Props2/%s", "MI_%s"),
    ("/Game/RedHope/Art/Models2/%s", "MI_%s"),
    ("/Game/RedHope/Art/Models/%s", "MI_%s"),
    ("/Game/RedHope/Art/Dress/RH_%s", "MI_RH_%s"),
    ("/Game/RedHope/Art/Machines/RH_%s", "MI_RH_%s"),
]

# ...and when even five conventions miss, fall back to a case-INSENSITIVE sweep
# of the whole art tree. airfilter2's MI is MI_RH_AirFilter2 - the folder and
# leaf are CamelCase while every texture and roster entry spells it lowercase,
# so no amount of path templating finds it. The sweep is the honest answer to
# a tree with five naming conventions and inconsistent case.
ART_ROOT = "/Game/RedHope/Art"

log = []


def find_mi(name):
    for root_fmt, leaf_fmt in ROOTS:
        root = root_fmt % name
        if not EAL.does_directory_exist(root):
            continue
        leaf = leaf_fmt % name
        for a in EAL.list_assets(root, recursive=True):
            if a.rstrip("/").split("/")[-1].split(".")[0] == leaf:
                mi = unreal.load_asset(a)
                if mi:
                    return mi, a
    # case-insensitive whole-tree fallback
    want = {("mi_%s" % name).lower(), ("mi_rh_%s" % name).lower()}
    for a in EAL.list_assets(ART_ROOT, recursive=True):
        if a.rstrip("/").split("/")[-1].split(".")[0].lower() in want:
            mi = unreal.load_asset(a)
            if mi and isinstance(mi, unreal.MaterialInstanceConstant):
                return mi, a
    return None, None


def import_normal(png, asset_name):
    task = unreal.AssetImportTask()
    task.filename = png
    task.destination_path = DEST
    task.destination_name = asset_name
    task.automated = True
    task.replace_existing = True
    task.save = True
    AT.import_asset_tasks([task])
    tex = unreal.load_asset("%s/%s.%s" % (DEST, asset_name, asset_name))
    if not tex:
        return None, "import produced nothing"
    fixed = []
    if tex.get_editor_property("compression_settings") != unreal.TextureCompressionSettings.TC_NORMALMAP:
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        fixed.append("TC_NORMALMAP")
    if tex.get_editor_property("srgb"):
        tex.set_editor_property("srgb", False)
        fixed.append("sRGB off")
    if fixed:
        EAL.save_loaded_asset(tex)
    # read back - the whole point
    comp = tex.get_editor_property("compression_settings")
    srgb = tex.get_editor_property("srgb")
    if comp != unreal.TextureCompressionSettings.TC_NORMALMAP or srgb:
        return tex, "BAD SETTINGS after set (comp=%s srgb=%s)" % (comp, srgb)
    return tex, ("[" + ", ".join(fixed) + "]") if fixed else ""


def main():
    if not NRM_DIR or not os.path.isdir(NRM_DIR):
        log.append("RH_NRM_DIR missing or not a directory: %r" % NRM_DIR)
    else:
        wired = skipped = 0
        for fn in sorted(os.listdir(NRM_DIR)):
            if not fn.endswith("_Normal.png"):
                continue
            name = fn[len("T_"):-len("_Normal.png")] if fn.startswith("T_") else fn[:-len("_Normal.png")]
            asset_name = "T_%s_Normal" % name
            mi, mi_path = find_mi(name)
            if not mi:
                log.append("%-16s NO MI FOUND - normal imported but unbound" % name)
                import_normal(os.path.join(NRM_DIR, fn), asset_name)
                skipped += 1
                continue
            tex, note = import_normal(os.path.join(NRM_DIR, fn), asset_name)
            if not tex:
                log.append("%-16s IMPORT FAILED: %s" % (name, note))
                skipped += 1
                continue
            MEL.set_material_instance_texture_parameter_value(mi, "NormTex", tex)
            MEL.set_material_instance_scalar_parameter_value(mi, "UseNormTex", STRENGTH)
            EAL.save_loaded_asset(mi)
            back_t = MEL.get_material_instance_texture_parameter_value(mi, "NormTex")
            back_s = MEL.get_material_instance_scalar_parameter_value(mi, "UseNormTex")
            ok = back_t is not None and abs(back_s - STRENGTH) < 1e-3
            log.append("%-16s %s NormTex=%s UseNormTex=%.2f %s" % (
                name, "ok " if ok else "FAIL", back_t.get_name() if back_t else "<none>",
                back_s, note))
            wired += 1 if ok else 0
        log.append("wired %d, skipped %d" % (wired, skipped))
    with open(os.environ.get("RH_REPORT", "/tmp/rh_normals.txt"), "w") as f:
        f.write("\n".join(log) + "\n")
    for line in log:
        print(line)


main()
