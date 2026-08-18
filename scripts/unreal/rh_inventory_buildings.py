"""Factual inventory of the REAL-GAME building meshes and their material slots.

  RH_REPORT=<out.txt> UnrealEditor-Cmd <proj> -run=pythonscript \
      -script=$PWD/scripts/unreal/rh_inventory_buildings.py -unattended -nosound -stdout

WHY
---
Director, 2026-08-18: booted the real game (not the demo showcase) and every
surface building is either a primitive composite or a grey/blotchy mesh.

RHColonyVisualizerSubsystem::BuildBuilding holds the only def -> mesh mapping
the real game uses: `RealModelPaths` (11 rows, ~line 959) and, when
`rh.ModelSetV2` is non-zero - it defaults to 1 - `ModelPathsV2` (4 rows,
~line 1009) takes precedence for the four defs it covers. This script reads
BOTH tables (transcribed below; the C++ is owned by another workstream and is
not touched) and reports what is actually on disk.

Read-only. It answers three questions and nothing else:

  1. does every referenced mesh load;
  2. for every material slot, is the slot material a MaterialInstanceConstant
     parented to M_RH_Master with BaseTex bound - or is it Interchange's
     auto-generated `Material` / a bare `MI_Default`, which is what a grey
     render looks like from the asset side;
  3. what ELSE is sitting under Models/ and Models2/ unwired, with bounds, so
     the defs that still draw composed primitives (Pylon, ChargePad,
     Electrolyzer, IceDrill) have candidates to be pointed at.

A repair plan is emitted per broken mesh: the MI_<meshname> found by NAME
SEARCH (never a constructed path - this tree carries five folder conventions:
Models/<n>/<n> flat, Models/<n>/StaticMeshes/<n>, Models2/<n>/<n>/StaticMeshes,
Machines/<n>/StaticMeshes and Agri/<n>/<n>/StaticMeshes) and the albedo texture
beside it.
"""
import os

import unreal

MEL = unreal.MaterialEditingLibrary
OUT = os.environ.get("RH_REPORT", "/tmp/rh_inventory_buildings.txt")
MASTER_OBJ = "/Game/RedHope/Art/M_RH_Master.M_RH_Master"
MODEL_ROOTS = ["/Game/RedHope/Art/Models", "/Game/RedHope/Art/Models2"]
ART_ROOT = "/Game/RedHope/Art"

# Transcribed verbatim from RHColonyVisualizerSubsystem.cpp. Order preserved.
REAL_MODEL_PATHS = [
    ("Forge",             "/Game/RedHope/Art/Models/forge/StaticMeshes/forge.forge"),
    ("BatteryBank",       "/Game/RedHope/Art/Models/battery/battery.battery"),
    ("WaterPlant",        "/Game/RedHope/Art/Models/ice/ice.ice"),
    ("Lander",            "/Game/RedHope/Art/Models/lander2/lander2.lander2"),
    ("SolarArray",        "/Game/RedHope/Art/Models/solar2/solar2.solar2"),
    ("Habitat",           "/Game/RedHope/Art/Models/habitat/habitat.habitat"),
    ("Stockpile",         "/Game/RedHope/Art/Models/stockpile/stockpile.stockpile"),
    ("Borer",             "/Game/RedHope/Art/Models/extractor2/extractor2.extractor2"),
    ("AirFilter",         "/Game/RedHope/Art/Machines/RH_AirFilter2/StaticMeshes/RH_AirFilter2.RH_AirFilter2"),
    ("HumidityRegulator", "/Game/RedHope/Art/Agri/humidity/humidity/StaticMeshes/humidity.humidity"),
]
MODEL_PATHS_V2 = [
    ("Forge",         "/Game/RedHope/Art/Models2/HeavyForge/HeavyForge/StaticMeshes/HeavyForge.HeavyForge"),
    ("SolarArray",    "/Game/RedHope/Art/Models2/SolarPanel/SolarPanel/StaticMeshes/SolarPanel.SolarPanel"),
    ("Habitat",       "/Game/RedHope/Art/Models2/HabitatDome/HabitatDome/StaticMeshes/HabitatDome.HabitatDome"),
    ("ComputeModule", "/Game/RedHope/Art/Models2/CommandModule/CommandModule/StaticMeshes/CommandModule.CommandModule"),
]
# The ONE def whose colour is vertex data, so a non-master slot is expected
# there and is NOT a defect: the visualizer forces M_VertexColor at runtime.
VERTEX_COLOURED = {"Forge"}

DATA_HINTS = ("metallic", "roughness", "normal", "mask", "_mr", "occlusion", "_ao")

reg = unreal.AssetRegistryHelpers.get_asset_registry()
log = []


def rec(s=""):
    log.append(str(s))


def leaf_of(pkg):
    return pkg.rsplit("/", 1)[1]


def obj_path(pkg):
    return "%s.%s" % (pkg, leaf_of(pkg))


# --- one pass over the whole Art tree; every later lookup reads this ---------
ART_ASSETS = [str(a.package_name) for a in reg.get_assets_by_path(ART_ROOT, recursive=True)]
MASTER = unreal.load_asset(MASTER_OBJ)


def tree_root_of(pkg):
    """The asset's own folder tree, e.g. .../Models2/HeavyForge.

    Anchored on the lineage folder rather than on a fixed depth, because the
    five conventions put the mesh 1, 2 or 3 levels below it.
    """
    parts = pkg.split("/")
    for marker in ("Models2", "Models", "Machines", "Agri", "Dress", "Shaft",
                   "Props2", "Props", "Tiers"):
        if marker in parts:
            i = parts.index(marker)
            return "/".join(parts[:i + 2])
    return pkg.rsplit("/", 1)[0]


def find_by_name(name, within=None):
    """Every asset called `name`, by search. Full package paths, never built."""
    hits = [p for p in ART_ASSETS if leaf_of(p) == name]
    if within:
        hits = [p for p in hits if p.startswith(within + "/")]
    return sorted(hits)


def textures_in(tree):
    albedo, data = [], []
    for p in ART_ASSETS:
        if not p.startswith(tree + "/"):
            continue
        obj = unreal.load_asset(obj_path(p))
        if not isinstance(obj, unreal.Texture2D):
            continue
        low = leaf_of(p).lower()
        (data if any(h in low for h in DATA_HINTS) else albedo).append(p)
    return sorted(albedo), sorted(data)


def tex_param(mi, name):
    try:
        t = MEL.get_material_instance_texture_parameter_value(mi, name)
        return t.get_name() if t else None
    except Exception as ex:
        return "<err %s>" % ex


def scalar_param(mi, name):
    try:
        return MEL.get_material_instance_scalar_parameter_value(mi, name)
    except Exception:
        return None


def describe_slot(mat):
    """(verdict, one-line description) for a single material slot."""
    if not mat:
        return "BROKEN", "<NONE>  ** empty slot **"
    name = mat.get_name()
    cls = type(mat).__name__
    if not isinstance(mat, unreal.MaterialInstanceConstant):
        return "BROKEN", ("%s  ** NOT a MaterialInstanceConstant (%s) - "
                          "this is the grey-render shape **" % (name, cls))
    parent = mat.get_editor_property("parent")
    pname = parent.get_name() if parent else "<none>"
    base = mat.get_base_material()
    bname = base.get_name() if base else "<none>"
    on_master = bool(MASTER) and bname == MASTER.get_name()
    if not on_master:
        return "BROKEN", ("%s  parent=%s base=%s  ** not on M_RH_Master **"
                          % (name, pname, bname))
    basetex = tex_param(mat, "BaseTex")
    if not basetex or basetex == "Mars_Regolith_Texture":
        return "BROKEN", ("%s  parent=%s  BaseTex=%s  ** albedo unbound "
                          "(master default) **" % (name, pname, basetex))
    extra = "MRTex=%s use=%s NormTex=%s use=%s EmMask=%s EmAmt=%s" % (
        tex_param(mat, "MRTex"), scalar_param(mat, "UseMRTex"),
        tex_param(mat, "NormTex"), scalar_param(mat, "UseNormTex"),
        tex_param(mat, "EmissiveMask"), scalar_param(mat, "EmissiveAmount"))
    return "ok", "%s  parent=%s  BaseTex=%s  %s" % (name, pname, basetex, extra)


def audit(def_name, path, table):
    mesh = unreal.load_asset(path)
    rec("%-18s %-8s %s" % (def_name, table, path))
    if not isinstance(mesh, unreal.StaticMesh):
        rec("    !! FAILS TO LOAD (or is not a StaticMesh)")
        rec()
        return def_name, table, path, "NOLOAD", []
    b = mesh.get_bounds()
    rec("    loads: %d slot(s), bounds %.0f x %.0f x %.0f cm"
        % (len(mesh.get_editor_property("static_materials")),
           b.box_extent.x * 2, b.box_extent.y * 2, b.box_extent.z * 2))
    verdicts = []
    for i, sm in enumerate(mesh.get_editor_property("static_materials")):
        v, desc = describe_slot(sm.material_interface)
        verdicts.append(v)
        rec("    slot%d [%s] %-6s %s" % (i, sm.material_slot_name, v, desc))
    if def_name in VERTEX_COLOURED and table == "original":
        rec("    (vertex-colour lineage: M_VertexColor is forced at runtime, "
            "so a non-master slot here is BY DESIGN)")
    rec()
    return def_name, table, path, ("BROKEN" if "BROKEN" in verdicts else "ok"), verdicts


rec("=" * 100)
rec("REAL-GAME BUILDING INVENTORY   (RHColonyVisualizerSubsystem::BuildBuilding)")
rec("=" * 100)
rec("M_RH_Master: %s" % ("loaded" if MASTER else "** MISSING **"))
rec("rh.ModelSetV2 defaults to 1 -> the 4 ModelPathsV2 rows WIN over their")
rec("RealModelPaths namesakes (Forge, SolarArray, Habitat). ComputeModule has")
rec("no original. Every other def falls through to RealModelPaths; defs in")
rec("NEITHER table draw composed primitives.")
rec()

rec("-" * 100)
rec("TABLE 1of2  ModelPathsV2  (%d rows - the set the game actually renders "
    "for these defs)" % len(MODEL_PATHS_V2))
rec("-" * 100)
results = [audit(d, p, "V2") for d, p in MODEL_PATHS_V2]

rec("-" * 100)
rec("TABLE 2of2  RealModelPaths (%d rows; the 3 shadowed by V2 are marked)"
    % len(REAL_MODEL_PATHS))
rec("-" * 100)
v2_defs = {d for d, _ in MODEL_PATHS_V2}
for d, p in REAL_MODEL_PATHS:
    if d in v2_defs:
        rec("(shadowed by ModelPathsV2 at rh.ModelSetV2=1)")
    results.append(audit(d, p, "original"))

# --- repair plan for whatever came back BROKEN -------------------------------
rec("=" * 100)
rec("REPAIR CANDIDATES (broken rows only; MI + albedo found by NAME SEARCH)")
rec("=" * 100)
broken = [r for r in results if r[3] != "ok"]
if not broken:
    rec("none - every referenced slot is an MI on M_RH_Master with BaseTex bound.")
for def_name, table, path, _, _ in broken:
    pkg = path.split(".")[0]
    name = leaf_of(pkg)
    tree = tree_root_of(pkg)
    mis = find_by_name("MI_%s" % name)
    albedo, data = textures_in(tree)
    rec("%s (%s) mesh=%s tree=%s" % (def_name, table, name, tree))
    rec("    MI_%s found: %s" % (name, ", ".join(mis) if mis else "** NONE - must be created **"))
    rec("    albedo candidates: %s" % (", ".join(albedo) if albedo else "** NONE **"))
    rec("    data maps:         %s" % (", ".join(data) if data else "-"))
rec()

# --- everything under Models/ and Models2/, wired or not ---------------------
rec("=" * 100)
rec("ALL STATIC MESHES UNDER Models/ AND Models2/  (wiring candidates for the")
rec("defs that still draw primitives: Pylon, ChargePad, Electrolyzer, IceDrill)")
rec("=" * 100)
referenced = {p.split(".")[0] for _, p in REAL_MODEL_PATHS + MODEL_PATHS_V2}
rec("%-58s %-22s %-8s %s" % ("package", "size XxYxZ cm", "used?", "slot0 material"))
rec("-" * 130)
for root in MODEL_ROOTS:
    for pkg in sorted(str(a.package_name) for a in reg.get_assets_by_path(root, recursive=True)):
        obj = unreal.load_asset(obj_path(pkg))
        if not isinstance(obj, unreal.StaticMesh):
            continue
        b = obj.get_bounds()
        slots = obj.get_editor_property("static_materials")
        s0 = slots[0].material_interface if slots else None
        if s0 is None:
            s0d = "<NONE>"
        else:
            par = s0.get_editor_property("parent") if isinstance(s0, unreal.MaterialInstanceConstant) else None
            s0d = "%s (parent %s)" % (s0.get_name(), par.get_name() if par else "-")
        rec("%-58s %-22s %-8s %s"
            % (pkg,
               "%.0f x %.0f x %.0f" % (b.box_extent.x * 2, b.box_extent.y * 2, b.box_extent.z * 2),
               "LIVE" if pkg in referenced else "unused",
               s0d))
rec()

# --- why a correctly-wired mesh can still render flat -----------------------
# Slot wiring is only the first way a building goes grey. These are the other
# three that are visible from the asset side, all read-only:
#   Nanite on with no fallback / a stale build -> the proxy renders (cb6aa52);
#   a mesh with no UV0 samples one texel, so the whole hull takes one colour;
#   an albedo that is genuinely flat, which no wiring fix can rescue.
# RH_EXPORT_TEX=<dir> also dumps each live BaseTex so its content can be
# measured off-line instead of argued about from a screenshot.
EXPORT_DIR = os.environ.get("RH_EXPORT_TEX")
rec("=" * 100)
rec("RENDER-SIDE DIAGNOSTICS for the live meshes (Nanite / UVs / albedo)")
rec("=" * 100)
rec("%-16s %-7s %-5s %-5s %-26s %s" % ("mesh", "nanite", "LODs", "UVs", "BaseTex", "tex size / srgb / comp"))
rec("-" * 130)
live = [(d, p) for d, p in MODEL_PATHS_V2] + \
       [(d, p) for d, p in REAL_MODEL_PATHS if d not in v2_defs]
to_export = []
for def_name, path in live:
    mesh = unreal.load_asset(path)
    if not isinstance(mesh, unreal.StaticMesh):
        continue
    try:
        nanite = "on" if mesh.get_editor_property("nanite_settings").enabled else "off"
    except Exception:
        nanite = "?"
    try:
        lods = mesh.get_num_lods()
    except Exception:
        lods = -1
    # UStaticMesh does not expose the UV count; three accessors have carried it
    # across engine versions, so try each and keep the error if all miss - a
    # silent -1 here is exactly the kind of "we never actually checked" that
    # this inventory exists to stop.
    uvs, uv_err = -1, ""
    for get in (lambda: unreal.get_editor_subsystem(
                    unreal.StaticMeshEditorSubsystem).get_num_uv_channels(mesh, 0),
                lambda: unreal.StaticMeshEditorSubsystem().get_num_uv_channels(mesh, 0),
                lambda: unreal.EditorStaticMeshLibrary.get_num_uv_channels(mesh, 0)):
        try:
            uvs = get()
            uv_err = ""
            break
        except Exception as ex:
            uv_err = str(ex).splitlines()[0]
    slots = mesh.get_editor_property("static_materials")
    s0 = slots[0].material_interface if slots else None
    tex = None
    if isinstance(s0, unreal.MaterialInstanceConstant):
        try:
            tex = MEL.get_material_instance_texture_parameter_value(s0, "BaseTex")
        except Exception:
            tex = None
    if tex:
        # ExportAssets takes object PATHS, not objects.
        to_export.append(tex.get_path_name())
        try:
            tinfo = "%dx%d srgb=%s %s" % (
                tex.blueprint_get_size_x(), tex.blueprint_get_size_y(),
                tex.get_editor_property("srgb"),
                str(tex.get_editor_property("compression_settings")).split(".")[-1].split(":")[0])
        except Exception as ex:
            tinfo = "<err %s>" % ex
    else:
        tinfo = "-"
    try:
        lm = mesh.get_editor_property("light_map_coordinate_index")
    except Exception:
        lm = "?"
    rec("%-16s %-7s %-5s %-5s %-26s %s"
        % (leaf_of(path.split(".")[0]), nanite, lods, uvs,
           tex.get_name() if tex else "<none>", tinfo))
    if uv_err:
        rec("%-16s   uv-count unavailable: %s" % ("", uv_err))
    if uvs == 0:
        rec("%-16s   ** NO UV0 - every vertex samples one texel, so the whole "
            "hull renders as ONE FLAT COLOUR **" % "")
    rec("%-16s   lightmap UV index=%s" % ("", lm))
rec()
if EXPORT_DIR and to_export:
    try:
        unreal.AssetToolsHelpers.get_asset_tools().export_assets(to_export, EXPORT_DIR)
        rec("exported %d BaseTex textures to %s" % (len(to_export), EXPORT_DIR))
    except Exception as ex:
        rec("texture export FAILED: %s" % ex)
    rec()

n_bad = len(broken)
rec("=" * 100)
rec("VERDICT: %d of %d referenced meshes are broken or fail to load."
    % (n_bad, len(results)))
rec("=" * 100)

with open(OUT, "w") as f:
    f.write("\n".join(log) + "\n")
print("\n".join(log))
