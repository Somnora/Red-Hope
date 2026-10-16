"""Re-import DataTables from their CSVs. Compile-free.

  RH_TABLES=Buildings RH_REPORT=/tmp/r.txt UnrealEditor-Cmd <proj> \
      -run=pythonscript -script=$PWD/scripts/unreal/rh_sync_datatables.py \
      -unattended -nosound -stdout

  RH_TABLES  comma-separated bare names (Buildings,Rooms,...); omit for ALL.

The project rule is that docs/data/RH_*.csv and the in-editor DT rows must
match: the headless self-tests are pure-data verifiers and FAIL on drift. So any
CSV edit must be followed by this sync, and then by the battery.

fill_data_table_from_csv_string is used rather than a reimport task because the
DTs carry a row struct the importer cannot infer; filling preserves the struct
and replaces the rows. Row count is reported before and after so a silently
empty parse cannot pass as success.
"""
import os
import unreal

CSV_DIR = "/Volumes/Unreal/red_hope/red_hope/docs/data"
DT_DIR = "/Game/RedHope/Data"
OUT = os.environ.get("RH_REPORT", "/tmp/rh_dtsync.txt")
WANT = [t.strip() for t in os.environ.get("RH_TABLES", "").split(",") if t.strip()]

log = []
registry = unreal.AssetRegistryHelpers.get_asset_registry()

# RH_CREATE="Furniture:RHFurnitureRow,..." creates a brand-new DT_<Name> with
# that (compiled) row struct before the fill below - the step a new table
# needs once, after the editor compile that adds its struct (BM-4, 2026-10-16).
created = []
for spec in [s.strip() for s in os.environ.get("RH_CREATE", "").split(",") if s.strip()]:
    bare, struct_name = spec.split(":")
    asset_path = "%s/DT_%s" % (DT_DIR, bare)
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        log.append("HAVE %-22s already exists" % ("DT_" + bare))
        continue
    st = unreal.load_object(None, "/Script/RedHopeSim.%s" % struct_name)
    if not st:
        log.append("FAIL %-22s row struct %s not compiled" % ("DT_" + bare, struct_name))
        continue
    factory = unreal.DataTableFactory()
    factory.set_editor_property("struct", st)
    dt = unreal.AssetToolsHelpers.get_asset_tools().create_asset("DT_" + bare, DT_DIR, unreal.DataTable, factory)
    if not dt:
        log.append("FAIL %-22s create_asset returned nothing" % ("DT_" + bare))
        continue
    unreal.EditorAssetLibrary.save_loaded_asset(dt)
    created.append("DT_" + bare)
    log.append("NEW  %-22s created with %s" % ("DT_" + bare, struct_name))

names = []
for asset in registry.get_assets_by_path(DT_DIR, recursive=True):
    name = str(asset.asset_name)
    if name.startswith("DT_"):
        names.append(name)
names.extend(c for c in created if c not in names)  # the registry may lag a fresh asset
names.sort()

for dt_name in names:
    bare = dt_name[3:]
    if WANT and bare not in WANT:
        continue
    csv_path = os.path.join(CSV_DIR, "RH_%s.csv" % bare)
    if not os.path.exists(csv_path):
        log.append("SKIP %-22s no CSV at %s" % (dt_name, csv_path))
        continue
    dt = unreal.load_asset("%s/%s.%s" % (DT_DIR, dt_name, dt_name))
    if not dt:
        log.append("FAIL %-22s could not load asset" % dt_name)
        continue
    before = len(unreal.DataTableFunctionLibrary.get_data_table_row_names(dt))
    with open(csv_path, "r") as fh:
        csv_text = fh.read()
    # Returns a bool in 5.8, not a problem list.
    ok = unreal.DataTableFunctionLibrary.fill_data_table_from_csv_string(dt, csv_text)
    after = len(unreal.DataTableFunctionLibrary.get_data_table_row_names(dt))
    if after == 0:
        # Never save an emptied table over a good one.
        log.append("FAIL %-22s parsed to 0 rows, NOT saved (was %d)" % (dt_name, before))
        continue
    unreal.EditorAssetLibrary.save_loaded_asset(dt)
    log.append("%-4s %-22s rows %d -> %d" % ("ok" if ok else "WARN", dt_name, before, after))

with open(OUT, "w") as fh:
    fh.write("\n".join(log) + "\n")
