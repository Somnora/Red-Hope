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

names = []
for asset in registry.get_assets_by_path(DT_DIR, recursive=True):
    name = str(asset.asset_name)
    if name.startswith("DT_"):
        names.append(name)
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
