"""Bring the two stranded ORIGINAL building meshes onto M_RH_Master.

  RH_REPORT=<out.txt> UnrealEditor-Cmd <proj> -run=pythonscript \
      -script=$PWD/scripts/unreal/rh_wire_originals.py -unattended -nosound -stdout

WHAT THIS FIXES
---------------
rh_inventory_buildings.py, 2026-08-18: of the 14 mesh rows the real game's
`RealModelPaths` / `ModelPathsV2` can reach, exactly three are not an
MI_<name> on M_RH_Master with BaseTex bound:

  forge    slot0 = DefaultMaterial on M_GLTF   -- BY DESIGN, not touched. The
           original Forge carries its colour in VERTEX data and
           RHColonyVisualizerSubsystem forces M_VertexColor on it at runtime
           (see VertexColoredModels, ~line 1017). rh_wire_dress.py excludes it
           for the same reason. Reparenting it would break the vertex path.
  solar2   slot0 = MI_solar2   parented to M_ModelTex
  habitat  slot0 = MI_habitat  parented to M_ModelTex

The last two are the whole job. They are the ONLY live-path meshes
RHArtWireCommandlet::WireAll never covered - it wired the eleven rows of the
mixed set, and these two are shadowed at the default rh.ModelSetV2=1
(SolarArray draws SolarPanel, Habitat draws HabitatDome). But the C++ falls
back to them whenever the V2 asset fails to load, and `rh.ModelSetV2 0` selects
them outright, so "shadowed" is not "unreachable": in either case the building
renders off a material family nothing else in the game uses.

WHAT IT DELIBERATELY DOES NOT DO
--------------------------------
No art decisions. The reparent binds the albedo and leaves every look
parameter at the master's default - no accent hue, no glow, no MR/normal opt-in
- because picking those is the wire table's job (RHArtWireCommandlet) and a
guess here would be indistinguishable from authored intent later.

The mesh slot is only rewritten if it is actually pointing somewhere else.
Both meshes already reference their MI by name, and a blanket
set_editor_property on static_materials re-serialises the mesh - the exact
"pure damage" WireOne calls out. The slot is still re-read from a fresh load
either way, because in this repo an unverified setter is bug source #1.
"""
import os

import unreal

MEL = unreal.MaterialEditingLibrary
OUT = os.environ.get("RH_REPORT", "/tmp/rh_wire_originals.txt")
MASTER_OBJ = "/Game/RedHope/Art/M_RH_Master.M_RH_Master"
ART_ROOT = "/Game/RedHope/Art"

# (mesh object path, expected albedo leaf). Mesh paths transcribed from
# RealModelPaths in RHColonyVisualizerSubsystem.cpp; the MI and the texture are
# resolved by NAME SEARCH below, never by a constructed path.
TARGETS = [
    ("/Game/RedHope/Art/Models/solar2/solar2.solar2", "T_solar2_BC"),
    ("/Game/RedHope/Art/Models/habitat/habitat.habitat", "T_habitat_BC"),
]

reg = unreal.AssetRegistryHelpers.get_asset_registry()
ART_ASSETS = [str(a.package_name) for a in reg.get_assets_by_path(ART_ROOT, recursive=True)]
log = []


def rec(s=""):
    log.append(str(s))


def find_by_name(name):
    return sorted(p for p in ART_ASSETS if p.rsplit("/", 1)[1] == name)


def dump_mi(mi, tag):
    parent = mi.get_editor_property("parent")
    rec("    %-7s MI=%s parent=%s base=%s"
        % (tag, mi.get_name(),
           parent.get_name() if parent else "<none>",
           mi.get_base_material().get_name() if mi.get_base_material() else "<none>"))
    for prop, label in (("texture_parameter_values", "tex"),
                        ("scalar_parameter_values", "scalar"),
                        ("vector_parameter_values", "vector")):
        for pv in mi.get_editor_property(prop):
            val = pv.get_editor_property("parameter_value")
            rec("            override %-6s %-16s = %s"
                % (label, pv.get_editor_property("parameter_info").name,
                   val.get_name() if hasattr(val, "get_name") else val))


master = unreal.load_asset(MASTER_OBJ)
rec("M_RH_Master: %s" % ("loaded" if master else "** MISSING - abort **"))
rec()

ok_n = 0
for mesh_obj, albedo_leaf in TARGETS:
    mesh_pkg = mesh_obj.split(".")[0]
    name = mesh_pkg.rsplit("/", 1)[1]
    rec("=" * 92)
    rec("%s   %s" % (name, mesh_obj))
    rec("=" * 92)

    mesh = unreal.load_asset(mesh_obj)
    if not isinstance(mesh, unreal.StaticMesh) or not master:
        rec("    !! mesh failed to load (or no master) - skipped")
        rec()
        continue

    mi_hits = find_by_name("MI_%s" % name)
    tex_hits = find_by_name(albedo_leaf)
    rec("    search  MI_%s -> %s" % (name, mi_hits or "<none>"))
    rec("    search  %s -> %s" % (albedo_leaf, tex_hits or "<none>"))
    if len(mi_hits) != 1 or len(tex_hits) != 1:
        rec("    !! expected exactly one hit for each - ambiguous, skipped")
        rec()
        continue

    mi = unreal.load_asset("%s.MI_%s" % (mi_hits[0], name))
    tex = unreal.load_asset("%s.%s" % (tex_hits[0], albedo_leaf))
    if not isinstance(mi, unreal.MaterialInstanceConstant) or not isinstance(tex, unreal.Texture2D):
        rec("    !! MI or texture failed to load as the expected type - skipped")
        rec()
        continue

    dump_mi(mi, "BEFORE")

    # Reparent + bind the albedo. Every look parameter stays at the master's
    # default on purpose (see module docstring).
    mi.set_editor_property("parent", master)
    MEL.set_material_instance_texture_parameter_value(mi, "BaseTex", tex)
    MEL.set_material_instance_scalar_parameter_value(mi, "UseMRTex", 0.0)
    MEL.set_material_instance_scalar_parameter_value(mi, "UseNormTex", 0.0)
    MEL.set_material_instance_scalar_parameter_value(mi, "EmissiveAmount", 0.0)
    unreal.EditorAssetLibrary.save_loaded_asset(mi)

    # Only touch the mesh if the slot really points elsewhere.
    slots = mesh.get_editor_property("static_materials")
    needs = (len(slots) == 0) or (slots[0].material_interface != mi)
    if needs:
        rebuilt = []
        for sm in slots:
            entry = unreal.StaticMaterial()
            entry.set_editor_property("material_interface", mi)
            entry.set_editor_property("material_slot_name", sm.material_slot_name)
            rebuilt.append(entry)
        if not rebuilt:
            entry = unreal.StaticMaterial()
            entry.set_editor_property("material_interface", mi)
            entry.set_editor_property("material_slot_name", unreal.Name("Slot0"))
            rebuilt.append(entry)
        mesh.set_editor_property("static_materials", rebuilt)
        unreal.EditorAssetLibrary.save_loaded_asset(mesh)
    rec("    slot    %s" % ("REWRITTEN" if needs else "already pointed at the MI - mesh left unsaved"))

    # Read BACK off a fresh load. A believed write is not a write.
    unreal.EditorAssetLibrary.load_asset(mesh_obj)
    fresh_mesh = unreal.load_asset(mesh_obj)
    got = fresh_mesh.get_editor_property("static_materials")
    slot0 = got[0].material_interface if got else None
    fresh_mi = unreal.load_asset("%s.MI_%s" % (mi_hits[0], name))
    dump_mi(fresh_mi, "AFTER")

    base = slot0.get_base_material() if slot0 else None
    read_tex = MEL.get_material_instance_texture_parameter_value(fresh_mi, "BaseTex")
    good = (bool(slot0) and slot0.get_path_name() == fresh_mi.get_path_name()
            and bool(base) and base.get_name() == master.get_name()
            and bool(read_tex) and read_tex.get_name() == albedo_leaf)
    ok_n += 1 if good else 0
    rec("    VERIFY  %s  slot0=%s base=%s BaseTex=%s"
        % ("ok" if good else "FAIL",
           slot0.get_name() if slot0 else "<none>",
           base.get_name() if base else "<none>",
           read_tex.get_name() if read_tex else "<none>"))
    rec()

rec("wired %d/%d stranded originals onto M_RH_Master" % (ok_n, len(TARGETS)))
with open(OUT, "w") as f:
    f.write("\n".join(log) + "\n")
print("\n".join(log))
