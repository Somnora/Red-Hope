# DataTable Sync Checklist — accumulated debt (Sessions 30–36)

The CSVs in `docs/data/` are the source of truth and are all committed. The live DataTable assets + the two C++ row structs need syncing next editor session (editor open, MCP connected). Until then: code defaults match every scalar, and headless tests inject the new rows in memory, so everything runs — but the live in-editor game reads stale/absent rows for the new content.

## 1. Recompile first (two structs gained columns / two are new)
- `FRHBuildingRow` gained `Category` (FName) + `Blurb` (FString) — **DT_Buildings must be re-imported or the columns won't exist on the asset.**
- `FRHDiscoveryRow` (new struct) — for the new DT_Discoveries.
- `FRHRivalRow` (new struct) — for the new DT_Rivals.
- These require the director's editor compile to have picked up the current source (the base dylibs are current as of the last headless run).

## 2. New DataTable assets to IMPORT (they do not exist yet)
- **DT_Discoveries** ← `docs/data/RH_Discoveries.csv`, schema `FRHDiscoveryRow`, into `/Game/RedHope/Data/`.
- **DT_Rivals** ← `docs/data/RH_Rivals.csv`, schema `FRHRivalRow`, into `/Game/RedHope/Data/`.
  (`import_file` with the compiled struct as schema — the DrHRows structs must be compiled first.)

## 3. Existing DataTables to re-sync (set_rows / re-import)
- **DT_Buildings** — new `Category` + `Blurb` on all 14 rows (player-facing UX copy; re-import cleanest since it's a schema change).
- **DT_Rooms** — `Greenhouse` row is NEW (Gate C garden fork); confirm it and the earlier active flips.
- **DT_Resources** — `Glass` row is NEW (garden fork).
- **DT_ManifestItems** — `GlassCrate` row is NEW (garden fork).
- **DT_Config** — ~25 NEW rows since the last sync (ColonistEvacSols was the last synced):
  - Hope-drives (7): HopeSmoothTauSols, HopeTempoSlope, HopeTempoMin/Max, HopeFailingEnter, HopeStrainedEnter/Exit, HopeSteadyEnter, HopeThrivingEnter/Exit, HopeFlourishingEnter/Exit
  - Garden fork (3): GardenGrowLightWPerCell, GreenhouseGlassKgPerCell, GreenhouseMinLevel
  - Water loop (6): ColonistWaterKgPerSol, GreywaterReturnFraction, WaterPotabilityDecayPerSol, WaterPotabilityRestorePerKg, WaterPotabilityFloor, HopeWaterPenalty
  - Generational carrot (4): HopeGrowthThreshold, HopeGrowthIntervalSols, HopeGrowthFoodBufferSols, HopeFirstBornMilestone
  - Discoveries (1): HopeDiscoveryThreshold
  - Trade (4): ConvoySpeedKmPerSol, ConvoyH2PerRun, ConvoyWearParts, RelationPerRun

## 4. After sync — verify pure-data (the discipline that catches DT/CSV drift)
Run the full headless battery; the room/discovery/rival tests assert `SliceActive` (and now inject) — once the DTs are live, drop the in-memory injects from those tests so they become true pure-data verifiers (as was done for `-rooms`/`-garden`). Suites: `-trade -discovery -growth -hopedrive -greenhouse -water -rooms -garden -luxury -crew -habitat -vault -borer` + `-sols=10` baseline.

## 5. Saves
Save format is **v17**. All prior on-disk saves refuse loudly (expected). Regenerate any dev slots (e.g. `habitattest`) after the sync.
