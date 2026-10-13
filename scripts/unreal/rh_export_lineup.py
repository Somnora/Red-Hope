"""Export every art mesh the game can reach to GLB, materials baked, for a lineup.

  RH_OUT=<dir> RH_REPORT=<out.txt> [RH_ROOTS=/Game/a,/Game/b] [RH_BAKE=1024] \
  UnrealEditor-Cmd <proj> -run=pythonscript \
      -script=$PWD/scripts/unreal/rh_export_lineup.py -unattended -nosound -stdout

Why export from Unreal instead of rendering the GLBs under Martians/gen: the
source folder holds several generations of most assets (raw, welded, plate-cut,
re-baked, hero-lane), and the imported asset has had MI wiring and real normal
maps applied after import. The only version worth judging is the one that ships,
so this pulls it out of the project itself via the built-in GLTFExporter, with
material inputs baked against the mesh so M_RH_Master / M_RH_Character's look
comes along instead of a white mesh.

Output: <RH_OUT>/<Folder>__<Asset>.glb, one per StaticMesh/SkeletalMesh, plus a
report line per asset (tris/verts for static meshes, OK/FAIL). Render the GLBs
with scripts/blender/rh_render_sheet.py.
"""
import os

import unreal

OUT = os.environ.get("RH_OUT", "/tmp/rh_lineup")
REPORT = os.environ.get("RH_REPORT", os.path.join(OUT, "export_report.txt"))
ROOTS = [r for r in os.environ.get("RH_ROOTS", ",".join([
    "/Game/RedHope/Art/Models2",
    "/Game/RedHope/Art/Models",
    "/Game/RedHope/Art/Machines",
    "/Game/RedHope/Art/Shaft",
    "/Game/RedHope/Art/Props2",
    "/Game/RedHope/Art/Tiers",
    "/Game/RedHope/Art/Dress",
    "/Game/RedHope/Art/Agri",
    "/Game/RedHope/Art/CrewAnim",
    "/Game/RedHope/Art/RobotAnim",
])).split(",") if r.strip()]
BAKE = int(os.environ.get("RH_BAKE", "1024"))
KINDS = ("StaticMesh", "SkeletalMesh")

os.makedirs(OUT, exist_ok=True)
log = []


def class_name(ad):
    try:
        return str(ad.asset_class_path.asset_name)
    except AttributeError:  # pre-5.1 AssetData
        return str(ad.asset_class)


def options():
    o = unreal.GLTFExportOptions()
    # 5.8: the bake size is an FGLTFMaterialBakeSize {X, Y, bAutoDetect} struct,
    # not the POT enum older docs show. Auto-detect = the largest texture in the
    # input's graph, so a 2048 hero albedo is not halved; BAKE is the fallback.
    settings = (
        ("bake_material_inputs", lambda: unreal.GLTFMaterialBakeMode.USE_MESH_DATA),
        ("default_material_bake_size",
         lambda: unreal.GLTFMaterialBakeSize(x=BAKE, y=BAKE, auto_detect=True)),
        ("export_vertex_colors", lambda: True),
        ("export_unlit_materials", lambda: True),
        ("export_preview_mesh", lambda: False),
    )
    for prop, val in settings:
        try:
            o.set_editor_property(prop, val())
        except Exception as e:  # property and type names drift between engine versions
            log.append("OPTION   %s not set: %s" % (prop, e))
    return o


reg = unreal.AssetRegistryHelpers.get_asset_registry()
opts = options()
seen = 0
for root in ROOTS:
    for ad in reg.get_assets_by_path(root, recursive=True):
        kind = class_name(ad)
        if kind not in KINDS:
            continue
        seen += 1
        pkg = str(ad.package_name)
        name = str(ad.asset_name)
        folder = pkg[len("/Game/RedHope/Art/"):].split("/")[0]
        dst = os.path.join(OUT, "%s__%s.glb" % (folder, name))
        asset = ad.get_asset()
        stats = ""
        if kind == "StaticMesh":
            stats = "tris=%d verts=%d" % (asset.get_num_triangles(0), asset.get_num_vertices(0))
        try:
            ok = unreal.GLTFExporter.export_to_gltf(asset, dst, opts, set())
        except TypeError:
            ok = unreal.GLTFExporter.export_to_gltf(asset, dst, opts)
        except Exception as e:
            ok = False
            stats += " err=%s" % e
        log.append("%s %-12s %s.%s %s -> %s" % ("OK  " if ok and os.path.exists(dst) else "FAIL",
                                               kind, pkg, name, stats, os.path.basename(dst)))

log.append("DONE %d meshes under %s" % (seen, ", ".join(ROOTS)))
with open(REPORT, "w") as f:
    f.write("\n".join(log) + "\n")

# Material baking needs a live RHI, which the pythonscript commandlet does not
# have (it exports geometry with EMPTY materials, and -AllowCommandletRendering
# exits silently on Metal). So the real run is the full editor:
#   UnrealEditor <proj> -ExecutePythonScript=<this> -unattended -nosound
# and RH_QUIT=1 closes it once the report is written. The quit is deferred to a
# post-tick callback: -ExecutePythonScript runs during startup, and a quit
# requested then is dropped - the 2026-10-13 runs wrote their reports and then
# sat idle for 90 minutes.
if os.environ.get("RH_QUIT") == "1":
    _ticks = [0]

    def _quit_when_ticking(_dt):
        _ticks[0] += 1
        if _ticks[0] == 30:
            unreal.unregister_slate_post_tick_callback(_quit_handle)
            unreal.SystemLibrary.quit_editor()

    _quit_handle = unreal.register_slate_post_tick_callback(_quit_when_ticking)
