# T2, regions and the regional map: design (2026-10-29)

Written at HEAD `de8ab59` while D7 builds, read-only. T2 builds after T1, so the queue is D7, E7, E8, the idle robots, T1, then T2. E7, E8 and T1 are designed but not built, so the names this design takes from them are design names. **Before any build, this design is refreshed at T1's HEAD** (every anchor re-pinned by symbol, every E7/E8/T1 name rebased onto the code as built). The refresh blocks the build, as T1's own refresh at E8's HEAD does.

**Sources, highest first:** the director's answers (the plan's Q1a-Q4a; E7 Q-A (b) and Q-B (b); E8's discovery rule; T1 (a); this design's own answers, once given) > `docs/qa/2026-10-20/territories-plan.md` (the T2 row, :47; T2's risks, :79-83; the general rules, :67-71) > E4's design (`RegionId` is the rival's home region, :36, :75) > E7's REVISION 2 section 6 and REVISION 3 > T1's design (section 4's event latch, section 8's salt rule, section 10's anchors) > this design.

Short names are E7's and T1's: **S** = `Source/RedHopeSim/Private/RHSimWorldSubsystem.cpp`, **SH** = its header, **M** = `RHSimMarket.cpp`, **V** = `Source/RedHope/Private/RHColonyVisualizerSubsystem.cpp`, **TR** = `Source/RedHope/Private/RHMarsTerrain.cpp`, **K** = `RHCommandDeckPanels.cpp`, **Rows** = `Source/RedHopeSim/Public/Data/RHRows.h`.

---

## What T2 builds, in one paragraph (under the recommended answers, Q-A (a) and Q-B (a))

T2 gives Mars a map. A new data table, `RH_Regions.csv`, holds eight placeholder regions. Each has a stable key, a display name, a position on the regional map in whole metres, a size and an owner: the colony's start region, a rival's home region, or nobody. Region 0 is today's map, unchanged. Zarya's home region sits 120 km out and Meridian's 200 km out, on the same bearings their markers already use in the valley, so the map agrees with both the route lengths and the 3D markers. The sim gains read-only accessors and one discovery-gated read model for the map. E7's two region seams become real (`GetColonyRegionId`, `GetBuildingRegionId`, both still 0). Deposits are created only for the colony's region (all of today's rows are region 0). The terrain's hand-copied deposit list becomes a read of the deposit rows. A full-screen regional map opens from a Map button or the M key: named regions, your region marked, known rivals in their regions with a line and the trip time to each, and "No known settlement" everywhere else. **Nothing in play changes.** The rivals still trade in the market they use today, nothing new is saved, the save version stays, and the battery is line-identical apart from a new `regions.log`.

---

## 0. What HEAD gives T2 (and what E7, E8 and T1 add before it)

**Region ids that exist, all 0:**
- **Rival rows:** `FRHRivalRow::RegionId` (Rows:412-413, "the rival's home region (0 for every row today)"), in `RH_Rivals.csv` column 7, 0 for Zarya and Meridian.
- **Deposit rows:** `FRHDepositRow::RegionId` (Rows:231-232, "Step-4 hook: 0 = the home region"), 0 on all nine rows. `FRHDepositState::RegionId` is set from the row at world start (S:545-558) and re-derived by row on load (S:14402-14416), so it is never saved.
- **Saved records:** floors, shafts, tunnels, caverns and tram lines (SH:297, :323, :356, :409, :481), road edges and districts. All are "always 0".
- **Markets:** one per region that has a rival with an economy (M:296-346); one today, region 0.

**The market is keyed by the rival's region.**
- A rival trades in its region's market (E4 design :75: "T2 places rivals in regions").
- E5's reservation reads the rival's own market (`RHSimCurrency.cpp:215`).
- On load, a saved market is kept only by its region (M:1784-1820, then the sync's `bKeep` at M:307-308).

Section 1 follows from this.

**Deposits.** At world start every row becomes a deposit, whatever its region (S:545-558), with `LocationCm` = the row's metres × 100 from the world origin.

**Terrain (presentation only; the sim builds and walks on z = 0, TR:325-326).** Every value is a constant in the code:
- the crater stream `FRandomStream(90210)` (TR:77), with 12 craters 190-420 m out;
- the height noise seed `77u` (TR:334) and the dune seed `913u` (TR:340), with a 115° wind (TR:338);
- the scenery stream `FRandomStream(20260708)` (TR:386);
- the flat pad, out to 150 m and open by 350 m (TR:327).

The haul corridors keep rocks off the line from the origin to five deposits. Those five positions are copied into the code as constants (TR:451-474, "update BOTH if the CSV ever moves"). The copy holds Regolith_A, Regolith_B, Ore_A, Ore_B and Ice_A, not Ice_B and not the three outposts. The terrain API (`GroundZCm` ×22 calls, `SceneryGroundZCm` ×3, `SceneryClearanceCm`, `BuildScenery`, `AppendRock`) is called only from `Source/RedHope`.

**Rival markers in the valley** (V:4953-4964, `RivalMarkerPos`):
- Bearing = `FCrc::StrCrc32(name) % 360`. `StrCrc32` is the standard CRC-32 over 4-byte characters, which gives **Zarya 64°** and **Meridian 116°**.
- Radius = 200 m + 0.3 m per km: 236 m and 260 m.
- E8 makes `RivalMarkerPos` public. Its Routes overlay draws ribbons from the colony's depot to these markers.

**Routes (E7, REVISION 2 section 6).**
- `FRHRoute::LengthKm` is the row's `DistanceKm` (Zarya 120, Meridian 200). At `ConvoySpeedKmPerSol` 60, the legs are 2.0 and 3.33 sols.
- T6 will replace the length with the map distance between the depots. T2's data must therefore make the two equal (section 2.3).
- E7 says T2 "makes `GetBuildingRegionId` (0 for every building today) and `GetColonyRegionId()` real".

**Discovery (E8):**
- "Discovery gates EVERYTHING player-facing (no undiscovered rival's slot, holding, name, route or ribbon is ever shown or counted)."
- `IsRivalAvailable` = the row's `SliceActive` or the rival is discovered (E8 R2-1).
- Zarya ships `SliceActive` TRUE; Meridian ships FALSE, so it needs a survey to be discovered.

**T1 (as designed):**
- `FRHColony` holds the colony's state, and there is no save change (T1's answer (a)).
- The salt rule (section 8): a colony's rolls hash `Tag + "@" + Salt`. The salt is the region's name, and the founding colony's salt is empty, so it keeps today's rolls.
- The event latch's final home is "the region record (T2, T6)" (section 4).

**The UI:**
- `SRHSectionLadder` (`RHSectionLadder.h/.cpp`) is the house precedent for a custom-painted Slate widget in the deck: `MakeLines`, `MakeBox` and `MakeText` in the deck's glass and label face.
- The TRAFFIC panel and its button are at K:1926 and K:1969. E8 adds a Routes button beside Traffic, tagged `RH.TradeOverlay`.
- The key **M is not bound** in `RHPlayerController.cpp` at `de8ab59`. The builder also checks `Config/DefaultInput.ini`.

---

## 1. The finding that shapes T2: a rival's region is also its market (director question Q-A)

**The tension.**
- The plan's T2 places each rival in its own region, and says "the battery stays identical" (:47, :83). The plan was written on 2026-10-20, before E4 (2026-10-24).
- E4 then made a rival's `RegionId` both its home region and the market it trades in (E4 :75).
- So moving Zarya and Meridian into their own regions in the economy is not inert. It splits today's one market into two.

**What a split would do, measured at `de8ab59`:**
- **Prices:** probably unchanged. The two rivals trade different goods (Zarya: Ice, Water, Oxygen, Struct, Hydrogen; Meridian: SpareParts, Food). Each slot reprices from its own region's sums (M:640-700), so each resource's price path should stay the same. That is inferred, not run; a paired run would have to prove it.
- **Saves:** every save grows by about **7.5 KB**:
  - a second E4 market block, 2,460 B (RegionId, and per slot the price plus 8 + 8 ring entries at 136 B over the 18 economy slots, plus the fill and head; M:1520-1525, the slots from `RHSimEconomy.cpp`'s filter on `RH_Resources.csv`);
  - a second E8 history ring, about 5,068 B (E8's own figure, its answer Q-A (b)).

  Every size line in the battery moves.
- **Old saves:**
  - The saved region-0 market finds no live region-0 market, so E4's sync starts regions 1 and 2 fresh. Every price returns to its Earth value and the 8-sol tallies empty.
  - This hits every existing save and the two crew-phase checkpoints, unless T2 adds a migration rule that hands each slot to the region that now trades it.
- **The market panel:** E8 draws its region selector once a second market holds an available rival, so it appears when Meridian is discovered.

**Recommendation (a): the map now, the trade at T5.**
- In T2, a rival's home region is a fact of the map: the region table's `Owner` column.
- The rival row's `RegionId` stays 0 and keeps its E4 meaning, "the market it trades in". Its comment is reworded so the two meanings can't be confused.
- T5 (rival cities) moves each rival's trade into its home region. It does that with the split's costs on the table: the migration, the size lines and the selector, each modelled for the director.

The map still shows each rival in its own territory, which is T2's promise. Nothing in play moves.

The options (b) (split now) and (c) (rivals stay in your region on the map too) are in the questions at the end.

---

## 2. The region record and its data

### 2.1 `docs/data/RH_Regions.csv` and `DT_Regions`

Column 0 is the row name, as in every repo CSV. The row name is the region's **stable key**:
- T1's salt rule hashes it, so it must never change once a colony lives in that region (T4 onward);
- the display name can change at the wording review without moving a roll.

```
Name,RegionId,DisplayName,MapX_m,MapY_m,Radius_m,Owner,Notes
Basin,0,Landing Basin,0,0,40000,,"T2: the colony's start region, today's map. Placeholder name and size, director review."
FrostCrater,1,Frost Crater,52605,107855,40000,Zarya,"T2: Zarya's home region, 120 km out on its marker's 64-degree bearing. Placeholder."
AmberShelf,2,Amber Shelf,-87674,179759,40000,Meridian,"T2: Meridian's home region, 200 km out on its marker's 116-degree bearing. Placeholder."
IronFlats,3,Iron Flats,-150000,-55000,40000,,"T2: unclaimed. Placeholder."
ColdRim,4,Cold Rim,125000,-215000,40000,,"T2: unclaimed. Placeholder."
OchreSteps,5,Ochre Steps,295000,50000,40000,,"T2: unclaimed. Placeholder."
GlassHollow,6,Glass Hollow,-355000,130000,40000,,"T2: unclaimed. Placeholder."
LongDunes,7,Long Dunes,-210000,-365000,40000,,"T2: unclaimed. Placeholder."
```

- **Eight regions:** the plan's `-scale` figure for T3 (:61), placeholder.
- **The anchors:** no two closer than 157 km (Frost Crater to Amber Shelf), so no discs overlap at a 40 km radius. The map spans about 730 × 625 km.
- **The names are placeholders** for the Gate-D wording review. They are neutral geography on purpose. "Amber Shelf" does not name Meridian, so an undiscovered rival's region gives nothing away. The wording review may prefer real Martian place names; only `DisplayName` would change.
- **The row struct**, `FRHRegionRow : FTableRowBase` in Rows: `int32 RegionId = 0; FString DisplayName; int32 MapX_m = 0; int32 MapY_m = 0; int32 Radius_m = 40000; FName Owner; FString Notes;`.
- **The asset**, `DT_Regions`, is created once with `RH_CREATE=Regions:FRHRegionRow` and then filled by the usual sync. It is E7's `DT_Endowments` path, step for step (E7 REVISION 2 section 9).
- **The path:** a `UPROPERTY(Config) FSoftObjectPath RegionsTablePath` beside the other table paths, loaded in `Initialize`.
- **The "Definitions loaded:" line is unchanged.** As with E7's endowments, regions are not added to it, so `baseline.log` keeps its line.

### 2.2 Validation (at `Initialize` and on every injection; Warnings only, never an Error, never a refusal)

Each bad row draws one Warning. The rest of the table still loads.
- `RegionId` below 0, or repeated: both rows of a repeat are ignored, and the Warning names the id.
- **No row with `RegionId` 0:** one Warning. `GetRegionCount()` then answers 0, and the map shows "No regional survey" (one wording row). Region 0 is the colony's start region in T2; section 3.1 says why it is fixed.
- **An `Owner` on region 0:** ignored with a Warning. Region 0 belongs to the colony.
- **An `Owner` that names no rival row, or a rival that owns two regions:** the owner is ignored on that row (for two, the higher `RegionId`).
- **`Radius_m` not in 1..1,000,000:** read as 40,000.
- **The distance rule:** for each rival-owned region, `GetMapDistanceM(0, R)` must equal the rival's `DistanceKm × 1000`, else a Warning. The map still draws it. This is what lets T6 swap a route's length for the map distance without moving a convoy's time.

### 2.3 Map distance, in whole metres

`int64 GetMapDistanceM(int32 RegionA, int32 RegionB) const` is the nearest whole metre to `sqrt(dx² + dy²)`. It is computed in int64 with an integer square root and a rounding step, so no float decides it (the plan's rule, "Map distances are integer metres", :71).
- Frost Crater: dx² + dy² = 14,399,987,050, which is 119,999.946 m, giving **120,000 m**.
- Amber Shelf: 40,000,028,357, which is 200,000.07 m, giving **200,000 m**.

Each region's anchor was chosen as the nearest whole-metre point on the rival's marker bearing. On the map, a rival therefore lies in exactly the direction its marker points in the valley, at exactly its route's length.

---

## 3. The sim: what changes

### 3.1 The colony's region and the buildings' region (E7's seams, made real)

- **`FRHColony::RegionId`** is a new member, value-initialized to 0. T1's member-classification CSV lists it as C (per colony).
  - It is **not saved**: T2 writes it nowhere. It is the founding region, and T4 is the first gate that can found elsewhere and save it.
  - Region 0 is fixed as the start region because every saved region id in the file is 0 (section 0). A colony founded in another region would disagree with its own saved records. That is T4's problem, and T4 owns the save change.
- **`int32 GetColonyRegionId() const`** (E7) returns `GetColony().RegionId`, which is 0.
- **`int32 GetBuildingRegionId(const FRHBuildingInstance&) const`** (E7) returns the region of the colony that owns the building. There is one colony, so it is 0.
- Nothing reads these differently from E7's constant 0. The battery proves it (section 6).

### 3.2 Deposits are created for the colony's region only

At S:545-558, `ForEachDeposit` skips a row whose `RegionId` is not `GetColonyRegionId()`:
- **with today's nine rows (all region 0), nothing changes:** the same deposits in the same order, with the same dense Ids;
- **the load path (S:14402-14416) is unchanged:** saved deposits are restored as saved and re-derive their row data by name;
- **RG3 (section 6) proves the skip** with an injected region-1 row.

T2 ships no deposit rows for other regions. Nothing could reach them before T4, and T4 authors them with the founding gate.

### 3.3 The region accessors

**In `URHDefinitionsSubsystem`** (the data, beside E7's `GetEndowmentPermille`):
- `int32 GetRegionCount() const`;
- `const FRHRegionRow* GetRegionAt(int32 Index) const`, in ascending `RegionId`;
- `const FRHRegionRow* FindRegion(int32 RegionId) const`;
- `FName GetRegionKey(int32 RegionId) const`, the row name and future salt;
- `Debug_InjectRegionRow(FName, const FRHRegionRow&)` and `Debug_ClearRegionRows()`. These are runtime only and bump a regions generation, as `Debug_InjectRival` bumps the rivals'.

**In `URHSimWorldSubsystem`:**
- `int32 GetRivalHomeRegionId(FName Rival) const`: the region whose valid `Owner` is the rival, or −1 if none;
- `int64 GetMapDistanceM(int32 A, int32 B) const`: section 2.3; −1 if either region is unknown;
- **`void GetRegionViews(TArray<FRHRegionView>& Out) const`, the map's one read model.** One entry per valid region, in `RegionId` order:
  ```
  struct FRHRegionView
  {
  	int32 RegionId = 0;
  	FName Key;                 // the stable row name
  	FString DisplayName;
  	int64 MapXm = 0, MapYm = 0, RadiusM = 0;
  	uint8 Known = 0;           // 0 = no known settlement, 1 = the colony's region, 2 = a known rival's home region
  	FName Rival;               // set only when Known == 2
  	int64 FromColonyM = 0;     // GetMapDistanceM(GetColonyRegionId(), RegionId)
  	double LegSols = 0.0;      // Known == 2: GetConvoyLegSols(Rival), E7's leg on the route as it stands (not the map distance)
  };
  ```
  - Discovery is applied **here, once**. `Known = 2` only when the owner `IsRivalAvailable`. Otherwise the region reads exactly as an unclaimed one, with no rival name, no line and no leg.
  - The map reads nothing else, so the discovery rule has one place to hold and one test to pin it (RG5). This mirrors E8's `ShownMarketSlots`.

### 3.4 What does not change

- **The rival rows:** `RegionId` stays 0 under Q-A (a). Only the comment at Rows:412 is reworded: "RegionId is the market the rival trades in (0 for every row; T5 moves each rival's trade to its home region, `RH_Regions.csv`'s Owner)."
- **The save and the version:** nothing is saved, and the version is not bumped (as E7 and T1). Current saves and both checkpoints load exactly as now.
- **The clock, the event latch, the weather:** stay as they are. T1 points the event latch at "the region record (T2, T6)". T2 leaves it for T6, which is when weather becomes per region; moving it now would be a move with nothing to read it.
- **Every roll:** the colony's salt stays empty (T1 section 8), so every seed is today's.
- **Era mode, `-scale`, the market, routes, depots and convoys:** untouched.

---

## 4. The regional map (director question Q-B)

Under the recommended (a): **a full map in the free band**, the space between the shaft strip and the right column (`SRHCommandDeck::GetFreeBand`).

### 4.1 Opening and closing

- **Opening:** a **Map** button beside Traffic and Routes (`DeckButton` tagged `RH.RegionMap`), or the **M** key. The button reads "Map [on]" while open, as Traffic does (K:1926).
- **Closing:** the button, M, or Esc. While the map is open, Esc closes it first and does nothing else.
- **The game keeps running.** The 3D view stays live behind the map's glass. The camera keys still move the camera underneath, and mouse clicks on the map never reach the world.

### 4.2 What it draws

**The widget.** `SRHRegionMap` is an `SCompoundWidget`. A painter leaf (the `SRHSectionLadder` pattern) draws the backdrop, the discs, the lines and the scale bar. An `SCanvas` places a chip button on each region so the click test can find them by tag (`RH.RegionChip.<Key>`).

**The fit:**
- The view fits every region's anchor ± radius, with a 24 px margin, scaled the same in x and y.
- At 1440x860 the band is about 920 × 760 px, which gives **about 1.1-1.2 px per km**: a 40 km region is a disc about 90 px across.
- The map uses the world's axes as the strategy camera sees them at its default yaw, so a rival on the map lies in its marker's direction from the Lander. The builder checks this with one capture of each.

**Each region:**
- a disc in the deck's glass with a 1 px rim and the `DisplayName` in the label face;
- **your region:** a Gold rim and the chip "Your colony";
- **a known rival's region:** the rival's tint (the marker's colour), the chip naming the settlement (`DisplayName` of the rival row, e.g. "Zarya Station"), and a 1 px line from your region labelled "120 km · 2.0 sols". The sols come from E7's leg, so the label can't disagree with the convoy's real time.
- **every other region:** a dim rim and "No known settlement". That covers an undiscovered rival's region and an unclaimed one alike.

**The scale bar:** 100 km, bottom left.

**The card.** Clicking a region's chip selects it (Gold, as E8's rows do) and opens a card in the band's lower right. It shows:
- the name;
- what is known of the owner;
- the distance from your region ("200 km");
- for a known rival, its nation and what it trades: the resources its E4 economy lists, so nothing undiscovered is named;
- for your region, "Your colony". For the rest, "No known settlement".

The card has no buttons. Founding (T4) and travel (T8) add them.

### 4.3 Cost and wording

- **`[FrameStats]`** gains a `Map` slot (E8's `ESlot::Market = 3` makes this `Map = 4`, `SlotCount = 5`) around the map's tick and layout. Layout is redone only when the region views' digest, the viewport, the free band or the discovered set changes; a still frame only paints.
- **New player-facing strings** go in `gate-d-wording.md` under "Rows added by T2", as placeholders. The proposed prefix is **B**, free at `de8ab59`; the orchestrator confirms it at build, as with A, C and L. The strings: Map / Map [on], Your colony, No known settlement, No regional survey, "%d km · %.1f sols", "%d km", the eight region names, the card's labels, and the M-key hint in the guide if the guide lists keys.

### 4.4 What it does not do

All of these belong to later gates: travel (T8), borders and claims (T7), convoys drawn on the map (T6, T8), settling a region (T4), rival growth (T5).

---

## 5. Terrain: the deposit copy moves to data; the seeds wait for T8 (a deviation from the plan)

**What T2 does.**
- A `Corridor` column joins `RH_Deposits.csv` (`bool Corridor = false` on `FRHDepositRow`). It is TRUE on exactly the five rows the code copies today.
- `BuildScenery` takes the corridor points from the colony region's rows (`LocX_m × 100`, `LocY_m × 100`; every one of the five is exact in float) instead of TR:457-463's constants, and the constants are deleted.
- The corridor test (TR:464-474) is unchanged and does not depend on order, so the scattered rocks are the same rocks.

**Proof:**
- A developer cheat, `RH.TerrainDigest`, read-only, prints two CRCs:
  - one over `GroundZCm` sampled on a 101 × 101 grid across ±4.6 km;
  - one over the scenery rig's mesh sections (vertex counts, then positions).
- The gate takes it on a before binary that adds ONLY the cheat (the idle gate's before-binary pattern), then on the final binary. **They must match exactly.**

**The deviation.**
- The plan has T2 move the terrain's seeds per region too ("nothing is hard-coded in the terrain code any more", :47).
- T2 moves only the deposit copy. The seeds stay constants until T8, when travel is the first thing that can draw another region's ground.
- The reason: moving the seeds now would build per-region terrain that nothing reads for five gates, and test it only on region 0, where every seed must equal today's.
- T8 adds the seed columns to `RH_Regions.csv` with region 0's row holding 90210, 77, 913, 20260708 and 115°, and proves region 0 with the same digest.
- This is logged as a deviation. If you'd rather have the seeds in T2 as the plan says, it costs about half a session more (with a preview cheat to look at another region's ground).

**Noticed, not changed.** Ice_B (700 m out) has no corridor, so its haul line can run through rocks: the drive-through class the director flagged for crew and furniture. Setting its `Corridor` flag would move the rocks on that line. That is a visual change for its own look, not T2's.

---

## 6. Tests: `-regions`, a new fails-loudly suite (`regions.log`)

- **RG0, the table.**
  - The shipped table loads eight regions, in id order, with no Warning.
  - Each validation case in 2.2, injected one at a time, draws exactly its one Warning and the stated reading: a repeated id, a negative id, no region 0, an owner on region 0, an unknown owner, a rival owning two regions, a radius out of range, and a distance that disagrees with the route.
  - `Debug_ClearRegionRows` restores the table.
- **RG1, the accessors.**
  - `GetRegionAt`, `FindRegion` and `GetRegionKey` agree with the rows.
  - `GetRivalHomeRegionId`: Zarya 1, Meridian 2, an injected rival with no region −1.
- **RG2, distances.**
  - `GetMapDistanceM(0, 1) == 120000` and `(0, 2) == 200000` exactly, and equal to each rival's `DistanceKm × 1000`.
  - It is symmetric, and 0 to itself.
  - The integer square root is pinned on two hand cases (14,399,987,050 → 120,000; 40,000,028,357 → 200,000) and one exact square.
- **RG3, deposits by region.**
  - A new world with today's rows has the nine deposits with Ids 1-9, as at `de8ab59`.
  - With a region-1 row injected before world start, the world still has the same nine, with the same Ids and positions.
  - `GetColonyRegionId() == 0`, and every building's `GetBuildingRegionId` is 0.
- **RG4, inertness.**
  - The same 10-sol colony run twice, with the region table loaded and with it cleared, gives identical sim digests.
  - It also gives an identical market state and identical rival stores, and the rival rows' `RegionId` is 0 throughout.
- **RG5, the discovery gate.**
  - At start, `GetRegionViews` gives region 0 `Known = 1`, region 1 `Known = 2` (Zarya, available because it ships active), and every other region `Known = 0` with no rival named.
  - After `Debug_DiscoverRival(Meridian)`, region 2 is `Known = 2`. Nothing else moves.
  - `LegSols` equals `GetConvoyLegSols` for each known rival (2.0 and 3.3333333333333335).
- **RG6, the save.**
  - A save made with the region table loaded is byte-identical to one made with it cleared.
  - Loading it raises no new Warning.

---

## 7. The gate

**Battery.**
- `rh_battery.sh` gains `-regions`, so `batteryRuns` is the logs in base + 1.
- `BATTERY COMPLETE: 0 not green`, and every log is newer than both dylibs.
- `diff_battery.py <base> Saved/RHBattery` shows wall-clock digits only, plus the new log. That includes the version lines and the size lines.
- The pins hold: "power: gen 50 W load 20 W" and "deposit Regolith_A: 40645 kg".

**Playtest:** the full playtest is cmp-identical to the playtest baseline.

**Saves:**
- Every save captured in the battery is byte-identical (`RH_BATTERY_SAVEDIR`, T1's corpus tool) to the base's capture.
- The two crew-phase checkpoints load with no new Warning.

**Click test:**
- Five new steps in the main scene: `map.open`, `map.card` (Zarya's region: its name, "Zarya Station", "120 km"), `map.unknown` (Meridian's region before discovery: "No known settlement" and no line), `map.close`, and `sweep.map` (a REACH sweep with the map open).
- The count rises by exactly 5. The deepbench scene is unchanged.
- Three back-to-back passes at 1440x860 with the 450 s timeout. `uptime`'s load is disclosed before each counted run.

**Terrain:** `RH.TerrainDigest`, before against final, is identical (section 5).

**Frame check:**
- The method is ruling 18's, the director's answer L (a) for E8: quiet pairs only, three per resolution.
- Scene S1 (`RH.CityBench 2000`), map closed (P0) against map open (P1), at 1440x860 and 1600x900.
- The bar: the median of the three deltas is at most 1.0 ms for the median frame time and for p95, and the `Map` slot is at most 0.2 ms a frame in every counted P1 window.
- A missed bar is a director finding.

**Captures** at 1440x860 (`Saved/RHCapture/t2/`), each looked at:
- `t2_map_00`: a new colony, Zarya known, Meridian not;
- `t2_map_01`: after `RH.Market discover` brings Meridian in;
- `t2_card_00`: Zarya's card;
- `t2_map_1600`: 1600x900;
- `t2_bearing_00`: the valley at the default yaw beside the map, for the bearing check;
- `t2_valley_00`: the valley with the map closed, against the before binary's capture.

**Docs:** a `docs/design-decisions.md` entry, and the wording rows.

**Not touched:** `Source/RedHopeSim`'s market, routes, convoys, era code and save code; the rival rows' data; any battery line.

---

## 8. Risks

- **The refresh:** T2 rests on four gates not built yet (E7, E8, the idle robots, T1). If T1 moves where the colony's state lives, section 3.1's member moves with it. The refresh at T1's HEAD stops on any E7/E8/T1 name it cannot find.
- **Deposit ids:** the region skip must not renumber anything. RG3 pins the Ids, and the battery's deposit lines are the second check.
- **The terrain identity:** a float difference in the corridor points (row metres × 100) would move rocks. Every one of the five is an exact float, and the digest catches anything else.
- **The map's orientation:** a mirrored axis would put Zarya at 296° instead of 64°. The `t2_bearing_00` capture is the check.
- **Discovery leaks:** a region name, a line or a tint that hints at Meridian before it is found. The names are neutral and `GetRegionViews` is the only read; RG5 and `map.unknown` pin it.
- **8 GB machine:** T2 compiles one new widget file, a sim data table and a suite. No adaptive build of S is needed. One builder, as always.

---

## 9. Deviations to log in `docs/design-decisions.md`

1. Rivals get home regions on the map only; their trade stays in today's market until T5 (Q-A (a), if chosen).
2. The terrain's seeds stay constants until T8; T2 moves only the deposit copy (section 5).
3. Region 0, not the plan's "region 1", is today's map: E7 already made 0 the home region, and every saved region id is 0.
4. The plan's "Save: the region list and ownership" (:82) moves to T4. In T2 the regions are data, and ownership is the table's plus the colony's fixed start region, so nothing needs saving.
5. The event latch stays on the subsystem until T6 (T1 section 4 named T2 or T6).

---

## 10. What later gates inherit

- **T3:** regions step in `RegionId` order. `GetRegionViews` and the table already iterate that way.
- **T4:**
  - founds a colony in a region with no owner and no colony;
  - saves `FRHColony::RegionId` and the ownership;
  - gives the new colony its salt from `GetRegionKey`;
  - authors the region's deposit rows (section 3.2's skip already keeps them out of other colonies);
  - adds the card's "Found a settlement here" button.
- **T5:** moves each rival's trade `RegionId` to its home region, with the split's costs modelled (section 1). It also moves the markers from the valley.
- **T6:**
  - replaces a route's `LengthKm` with `GetMapDistanceM` between the depots, equal by RG2 for today's two rivals;
  - moves the event latch into a per-region record.
- **T8:** adds the terrain seeds per region (section 5), travel, borders and convoys to this same map.

---

# QUESTIONS FOR THE DIRECTOR

**Q-A. Where do the rivals live, and where do they trade?** They move to their own regions on the map. The question is whether their trade moves with them now.
- **(a) Recommended: on the map now, their trade at T5.**
  - Zarya appears in Frost Crater, 120 km out, and Meridian in Amber Shelf, 200 km out, once discovered.
  - Both keep trading in the one market they share today until T5 makes them cities.
  - Nothing in play changes. The battery stays identical apart from the new `regions.log`, and saves don't change.
- **(b) Move their trade to their own regions now, as E4 planned:** two markets instead of one.
  - Each resource's price path should stay the same, since the two trade different goods.
  - But every save grows by about 7.5 KB (a second market block of 2,460 B, and E8's second 30-sol history of about 5,068 B), and every saved-size line in the battery moves.
  - The market panel shows a region selector once Meridian is discovered.
  - Old saves and the two crew-phase checkpoints restart every market price at its Earth value on load, unless T2 adds a migration rule.
  - About one more session of work.
- **(c) Keep the rivals in your region on the map too, until T5.** The map shows your region holding all three settlements and seven empty regions. It's the simplest, but it doesn't show the rivals' own territories that the plan promised for T2.

**Q-B. What does the regional map look like in T2?**
- **(a) Recommended: a full map in the middle of the screen.**
  - It opens from a Map button beside Traffic and Routes, or the M key, and Esc closes it.
  - The eight regions are named discs about 90 px across at 1440x860 (about 1.1 px per km over a 730 × 625 km map).
  - Your region is marked. Known rivals are named in their regions, with a line to each: "120 km · 2.0 sols" for Zarya, "200 km · 3.3 sols" for Meridian. Everywhere else reads "No known settlement".
  - Clicking a region opens its card. The colony keeps running behind the map, and T8 later adds travel, borders and convoys to it.
- **(b) A seventh MAP fold in the right column.** It is about 300 px wide, so about 0.4 px per km: discs about 33 px across, with labels that crowd. It follows the accordion, so opening it folds the other panels.
- **(c) No map yet.** T2 adds the regions as data and the tests only, and the map arrives with T8. Nothing new is on screen at T2's director look.

**Recorded, not asked** (say if you want either changed):
- **The terrain's per-region seeds wait for T8** (section 5). The plan put them in T2, but nothing can show another region's ground until T8's travel. Doing them now costs about half a session.
- **The eight region names are neutral placeholders** for the wording review, which may prefer real Martian place names.
