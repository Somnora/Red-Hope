"""Repo-wide audit of material bases and texture settings. Read-only by default.

  RH_REPORT=<out.txt> [RH_FIX=1] UnrealEditor-Cmd <proj> -run=pythonscript \
      -script=$PWD/scripts/unreal/rh_audit_materials.py -unattended -nosound -stdout

WHY THIS EXISTS
---------------
The walker fix on 2026-08-17 was one instance of a CLASS, and the class is what
keeps producing "the models look unfinished":

  * A mesh left on Interchange's auto-generated material. Its base,
    /InterchangeAssets/gltf/Substrate/M_GLTF, is MSM_SUBSURFACE_PROFILE - a skin
    shader that bleeds light through thin geometry and mottles the surface. All
    21 walkers were on it.
  * A packed metallic-roughness map imported as TC_DEFAULT. That is block
    compression tuned for colour, so DXT artifacts land directly in roughness
    and read as mottled specular. All 20 crew MR maps were like this.
  * A data map imported as sRGB. Gamma-shifts roughness/metallic and nothing
    downstream complains; the surface just responds wrongly to light.

Each was invisible in every log and uniform across the set, which is exactly the
shape that survives review. So this sweeps the whole Art tree rather than
waiting for the next complaint.

RH_FIX=1 repairs ONLY the two unambiguous texture-setting faults (MR/mask maps
that should be TC_MASKS and must not be sRGB). It deliberately does NOT reassign
materials: choosing a material is an art decision, and the walkers only had an
obvious answer because a correctly-authored MI already sat unused beside every
mesh. Material findings are reported for a human to route.
"""
import os

import unreal

MEL = unreal.MaterialEditingLibrary
OUT = os.environ.get("RH_REPORT", "/tmp/rh_audit_materials.txt")
FIX = os.environ.get("RH_FIX") == "1"
ROOT = "/Game/RedHope/Art"

# Names that mean "this is DATA, not colour".
DATA_HINTS = ("metallic", "roughness", "_mask", "mask_", "_mr", "occlusion", "_ao")
NORMAL_HINTS = ("_normal", "normalmap")

reg = unreal.AssetRegistryHelpers.get_asset_registry()
assets = reg.get_assets_by_path(ROOT, recursive=True)

gltf_meshes = []
bad_comp = []
bad_srgb = []
bad_normal = []
fixed = []

for a in assets:
    pkg = str(a.package_name)
    leaf = pkg.rsplit("/", 1)[1]
    obj = unreal.load_asset("%s.%s" % (pkg, leaf))
    if obj is None:
        continue

    # --- meshes: what base material is actually on the slot? ---
    slots = None
    if isinstance(obj, unreal.StaticMesh):
        slots = [(i, sm.material_interface)
                 for i, sm in enumerate(obj.get_editor_property("static_materials"))]
    elif isinstance(obj, unreal.SkeletalMesh):
        slots = [(i, sm.material_interface)
                 for i, sm in enumerate(obj.get_editor_property("materials"))]
    if slots:
        for i, mi in slots:
            if not mi:
                gltf_meshes.append("%s slot%d -> <NONE>" % (pkg, i))
                continue
            base = mi.get_base_material()
            bn = base.get_path_name() if base else "-"
            if "M_GLTF" in bn:
                # FULL package path, never the bare leaf: this repo carries
                # duplicate assets across lineages (Garden dupes, Props/Props2),
                # and a leaf name cannot tell the live one from an orphan.
                # Reporting leaves once made the correctly-wired crops look broken.
                gltf_meshes.append("%s slot%d -> %s" % (pkg, i, bn.split(".")[0]))
        continue

    # --- textures: are the data maps set up as data? ---
    if not isinstance(obj, unreal.Texture2D):
        continue
    low = leaf.lower()
    is_data = any(h in low for h in DATA_HINTS)
    is_normal = any(h in low for h in NORMAL_HINTS)
    srgb = bool(obj.get_editor_property("srgb"))
    comp = obj.get_editor_property("compression_settings")
    comp_n = str(comp).split(".")[-1].split(":")[0]

    if is_normal:
        if comp_n != "TC_NORMALMAP":
            bad_normal.append("%s comp=%s" % (leaf, comp_n))
        continue

    if is_data:
        wrong = []
        if srgb:
            wrong.append("sRGB")
            bad_srgb.append(leaf)
        if comp_n != "TC_MASKS":
            wrong.append("comp=%s" % comp_n)
            bad_comp.append(leaf)
        if wrong and FIX:
            obj.set_editor_property("srgb", False)
            obj.set_editor_property("compression_settings",
                                    unreal.TextureCompressionSettings.TC_MASKS)
            unreal.EditorAssetLibrary.save_loaded_asset(obj)
            fixed.append("%s (%s)" % (leaf, ", ".join(wrong)))

log = []
log.append("scanned %d assets under %s   FIX=%s" % (len(assets), ROOT, FIX))
log.append("")


def section(title, items, cap=250):
    log.append("=== %s: %d ===" % (title, len(items)))
    for it in items[:cap]:
        log.append("    " + it)
    if len(items) > cap:
        log.append("    ... and %d more" % (len(items) - cap))
    log.append("")


section("MESHES ON M_GLTF (subsurface skin shader - art decision to route)", gltf_meshes)
section("DATA MAPS NOT TC_MASKS", bad_comp)
section("DATA MAPS WRONGLY sRGB", bad_srgb)
section("NORMAL MAPS NOT TC_NORMALMAP", bad_normal)
if FIX:
    section("REPAIRED", fixed)

with open(OUT, "w") as f:
    f.write("\n".join(log) + "\n")
print("\n".join(log))
