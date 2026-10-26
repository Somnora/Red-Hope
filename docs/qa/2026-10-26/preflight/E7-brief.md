# E7 BUILD BRIEF: region-ready (depots, routes, endowment)

Pre-flight by a read-only planner, 2026-10-26. Code was read at HEAD `d842dd0` with `git show` / `git grep` only, never from the working tree.
Spec: `docs/qa/2026-10-25/e7-region-ready-design.md`. Authority inside it: DIRECTOR ANSWER > REVISION 3 > REVISION 2 > REVISION (R3 only) > RE-CHECK > INDEPENDENT REVIEW > LEAD REVIEW > design.
Where this brief and the spec disagree, the brief says why (section 9, "Spec problems"). Two of them are **blocking** and need an orchestrator ruling before the build (S1, S2). Neither needs the director.

---

## 0. Where E7 sits

- **Queue:** visual pass (in flight, uncommitted) → E5 (v56) → E6 (v57) → D6e → D7 → **E7** → E8 (its own `'RHE8'` frame, v60 by its REVISION 2/3).
- **E7 takes NO save version and writes NO save block.** `RHSaveVersion` stays whatever D7 leaves (expected v59: UNCERTAIN, the D6e and D7 numbers are not fixed in the handoff; read W:37 at your HEAD).
- **HEAD facts at `d842dd0`:** `RHSaveVersion = 55` (W:37); the last frame in the file is E4's `'RHE4'` `MarketTag` (written W:13933, read W:14743). E5, E6 (`'RHL6'`), D6e and D7 will append frames after it before E7 builds. E7 appends nothing.
- **The RedHopeSim source has not changed since the spec's last read (`798a549`):** `git diff --stat 798a549 d842dd0 -- Source/RedHopeSim scripts/sim docs/data` is empty. Every W, H and M anchor in REVISION 2/3 is still exact at `d842dd0`.
- **RedHope files that did move:** `RHCommandDeckPanels.cpp` (+7 lines at :327) and `RHCheats.cpp` (+1 at :18). See section 2.
- **Still moving:** the visual pass's working tree currently modifies `RHColonyVisualizerSubsystem.cpp` (V) and `RHCheats.cpp`, both E7 files. D7 also edits V (ApplyCutaway). E5 and E6 rewrite parts of M and W (E6 rewrites `ResolveCovert`). **Every line number below is a `d842dd0` number. Re-anchor each one at your own HEAD by the code snippet given, not by the number.**

---

## 1. The authoritative decisions (one list)

1. **Q-A (b), director:** the trade depot is a **role**, not a building. The Lander keeps every depot role. No `TradeDepot` row, button, mesh, card, cost, Gate-D wording row, or version fence.
2. **Q-B (b), director:** the endowment is a **permille on the rivals' production** (`ProducePermille`), not on V. V, the band (floor and ceiling), the start price, the kept prices, E3's Earth price and `ClearConvoy`'s no-market fallback are untouched.
3. **REVISION 3 §1, the orchestrator's decision:** there is no `'RHE7'` block, no `DepotTag`, no frame, no version bump, no `GetDepotBlockBytes`, and no W:37 entry. REVISION 2 §8 is struck in full.
4. **Derived only.** `FRHDepot` and `FRHRoute` are runtime records rebuilt on every call: no cache, no Ids, never saved. `GetColonyRegionId()` is a constant 0 until T2.
5. **One depot rule,** `FindColonyDepotBuildingId(RegionId)`:
   1. the runtime binding (`DepotBindings`, written only by `Debug_SetColonyDepot`), if its building exists and is not under construction; else
   2. the first building in list order with `DefName == NAME_Lander && !bUnderConstruction` and `GetBuildingRegionId(B) == RegionId`; else
   3. 0, which means no depot (landing falls back to `AddStock`).
6. **Only three trade-facing Lander roles move behind the rule:** convoy goods (M), covert loot (W) and the convoy's drawn start (V). Every other Lander role stays where it is until T1.
7. **The endowment applies at exactly one line:** M:214's `* 1000` becomes `* Defs->GetProducePermille(Row.RegionId, P.Key)`. It is int64 with no division. Consume (M:215) is untouched.
8. **A route's length** is `(double)Row.DistanceKm`, verbatim. Do not use integer metres.
9. **Routes are keyed by (colony region, rival name).** `RegionA` is `GetColonyRegionId()`, the same depot that landing uses. `GetRouteFor` answers true whenever the rival row exists.
10. **`DT_Endowments` ships one identity row,** `R0_Ice,0,Ice,1000`. A missing row reads as 1000.
11. **The deck's price read and `RH.Market` pass the region explicitly.** All other readers of the default `RegionId = 0` stay as they are (including `-scale`).
12. **The bar:**
    - the battery is line-identical to the gate before E7, version and size lines included, plus `regionready.log`;
    - the 110-sol playtest is byte-identical;
    - `-scale`'s digests are unchanged;
    - the click test is unchanged;
    - the one-time pre/post `cmp` of `RH_mktmx9.sav` reports identical.
13. **Seven deviations** go in `docs/design-decisions.md`, as REVISION 3 §7 lists them (copy them verbatim).

---

## 2. Anchor table (spec cites → HEAD `d842dd0`)

All W, H and M anchors verified: they hold at `d842dd0`. Shorthand: W = `Source/RedHopeSim/Private/RHSimWorldSubsystem.cpp`, H = `Source/RedHopeSim/Public/RHSimWorldSubsystem.h`, M = `Source/RedHopeSim/Private/RHSimMarket.cpp`, V = `Source/RedHope/Private/RHColonyVisualizerSubsystem.cpp`, MX = `RHSimMarketSuite.cpp`.

| Spec cite | At `d842dd0` | Status |
|---|---|---|
| W:19 `NAME_Lander`; W:546 Lander placed; W:108 `HaulLoadMinKg` | same | OK |
| W:9724-9729 `ConvoyLegSolsFor` | same | OK |
| W:9831-9841 covert loot loop; W:9834 Lander test (`FName("Lander")`) | same (in `ResolveCovert` W:9795) | OK now; **E6 rewrites ResolveCovert**, so re-anchor |
| W:13443 `SaveColony`, W:13946 `LoadColony`, W:13968 strict check | same | OK (E7 does not change the format) |
| W:14026-14046 load teardown; W:14041 `RepairClaims.Reset()`; W:14042 D5a latch | same (the comment "Tear down the live colony" is at 14027) | OK |
| W:96 `OnWorldBeginPlay` | same | OK |
| Lander search list (W:959 … :13507) | identical set: 19, 546, 959, 983, 1051, 1177, 1244, 1295, 1343, 3896 (a bench, non-trade), 9834, 10571, 10799, 11442, 12274, 12876, 12896, 13507 | OK |
| H:550-565 `FRHConvoy` (struct at H:549) | same; `FRHMarketClearing` follows, `UCLASS` at ~H:582 | OK |
| H:1560-1616 the market public section; H:1583 `GetRivalEconomyName`; H:1584 `GetMarketBlockBytes`; H:1606 "Harness hooks (runtime only, never saved…)" | same | OK |
| H:1748-1755 the convoy getters | same | OK |
| H:2627 `SaveColony`; H:2651 `Debug_PlaceInstant` (**returns void**) | same | OK; see note in §5 RR2 |
| H:2777 `private:`; H:3330 `RepairClaims`; H:3841 `FRHRivalEconomy`; H:3850 `FRHMarket`; H:3891 `WriteMarketBlock` (private); H:3931 `ConvoySpeedKmPerSol`; H:4045 `ConvoyLegSolsFor` (private) | same | OK |
| M:26 `MktNameLess` | same | OK |
| M:106 `EvaluateRivalRow`; **M:214 Produce grams**; M:215 Consume | same | OK (REVISION 3 corrected the 217/218 cites) |
| M:232-233 rows sorted by name string; M:274-333 the market part of `BuildMarketState`; M:286 `bKeep`; M:297-300 V and band; M:302-309 kept prices; M:312 start price | same | OK; **do not touch** |
| M:357-361 lazy `FindMarket` sync | same | OK |
| M:579 store step; M:608 the flow term's Supply | same | OK |
| M:646 clearing, M:661 no-market fallback, M:831 reservation | same | OK |
| M:887, M:1236 `ClosedRoutes` | same | OK |
| M:1136, M:1269 `ConvoyLegSolsFor` callers | same | OK |
| M:1203-1219 convoy landing (Lander test at M:1212) | same | OK; E5 and E6 edit around `StepConvoyReturn`, so re-anchor |
| M:1348-1352 `WriteMarketBlock` (syncs first); M:1354-1360 `GetMarketBlockBytes` | same | OK |
| M:1678-1690 the load clamp, its Warning at M:1686 | same | OK |
| V:4337-4342 the convoy's "Home = the Lander pad" loop | same at `d842dd0` | **WILL MOVE:** the uncommitted visual pass modifies V, and D7 edits V. Re-anchor by the snippet `// Home = the Lander pad; destination = the rival's marker.` |
| V:4193 `RivalMarkerPos` | same | OK |
| `RHCommandDeckPanels.cpp:2534` / `:2544` (the deck's price read) | **:2551** | **MOVED** (+7, P8c's `GetLadderEmPx` at :330-334). Snippet: `S->GetMarketPriceMcr(Res)` inside `"%s  %s cr/kg  (they hold %d kg)"` |
| `RHCheats.cpp:1090-1135` `RH.Market`; reads at :1126, :1132, :1135 | command at **:1090-1136**; `IsMarketTraded` **:1127**, `GetMarketTally` **:1133**, `GetMarketPriceMcr` **:1136** | **MOVED** (+1, an include at :18). The working tree changes it again (visual pass) |
| `RHSimCommandlet.cpp:14829-14831` `-market` dispatch; suite declarations at :32-34 | same | OK |
| `RHSimCommandlet.cpp:17113`, `:17118` `-scale` market lines | same | OK |
| `RHSimCommandlet.cpp:1324` `-covert` suite | same | OK |
| `scripts/sim/rh_battery.sh:37-39` SUITES | same; **also** line 2 (comment "57 suites (58 runs)") and line 58 (`echo "=== 57 suites ==="`) | spec omits lines 2 and 58 (see S9) |
| `scripts/sim/diff_battery.py:11` the `\(\d+ bytes` mask | same | OK (not relied on) |
| `scripts/unreal/rh_sync_datatables.py:29-52` `RH_CREATE`; `:78-81` the 0-row FAIL | same (FAIL text at :81) | OK; see S4 on the struct name |
| `RHDefinitionsSubsystem.cpp:12-28` Initialize loads; `:30` "Definitions loaded:"; `:620-629` `Debug_InjectRival` (generation bump at :628) | same | OK |
| `RHDefinitionsSubsystem.h:231` `RivalsTablePath`; `:250` `RivalsGeneration` | same | OK |
| `Data/RHRows.h:405` `DistanceKm` (float); `:412-413` `RegionId` | same; the file ends at :454 | OK |
| `RHSaveFrame.h:23-24` Header/Footer bytes; `:36` `Find` | same | OK |
| MX:188 `RHRunMarketSuite`; MX:230 `SaveFilePath`; MX:267 `Inject`; MX:272 `RetireAll`; MX:284-296 `LoadSlot`; MX:297 `Reset`; MX:331 the CSV-vs-DT precedent; MX:493 `Digest`; MX:611-612 Set A; MX:1294-1302 the MX9 fixture (slot `mktmx9`) | same | OK |
| Size lines (CV8 Cavern:739, DP11 Depth:787, EC1 Economy:493, DC9 DeepCommute:713, MX1 MX:564, MX9 MX:1359, CR9 Credit:619, EC6 Economy:866, XE10 Export:538) | same | OK; **E7 moves none** |
| `git grep -E '== 55\b\|"/55"' Source` | 16 hits in 10 files | OK; **E7 moves none** |
| `docs/design-decisions.md:1659-1661` (the mutation-table precedent) | the E4 MX12 mutation table, ~:1659 | OK |

---

## 3. Exact changes, file by file

### 3.1 `Source/RedHopeSim/Public/RHSimWorldSubsystem.h` (H)

- **Two plain structs** (not USTRUCT), public, placed after `FRHMarketClearing` and before `UCLASS()` (around H:580). Use REVISION 2 §6's shape exactly, with no `Id` member:
  ```cpp
  struct FRHDepot { int32 RegionId = 0; FName Owner; /* NAME_None = the colony */ int32 BuildingId = 0; };
  struct FRHRoute { FName Rival; int32 RegionA = 0; int32 BuildingA = 0; int32 RegionB = 0; double LengthKm = 0.0; uint8 Kind = 0; };
  ```
- **Public const getters** beside the convoy getters (H:1748-1755) and the market getters (H:1560-1616). Keep these exact names and signatures, because E8 reads them by name (E8 REVISION 3, lines 998-1003):
  - `double GetConvoyLegSols(FName Rival) const;` (0.0 for an unknown row)
  - `bool GetRouteFor(FName Rival, FRHRoute& Out) const;`
  - `void GetRoutes(TArray<FRHRoute>& Out) const;`
  - `void GetDepots(TArray<FRHDepot>& Out) const;`
  - `int32 GetColonyRegionId() const { return 0; }` (until T2)
  - `int32 FindColonyDepotBuildingId(int32 RegionId = 0) const;`
  - `int32 GetColonyDepotBindingId(int32 RegionId = 0) const;` (0 = none)
  - `FVector GetColonyDepotLocationCm(int32 RegionId = 0) const;` (`FVector::ZeroVector` with no depot)
  - `int32 GetBuildingRegionId(const FRHBuildingInstance& B) const { return 0; }` (T2 changes it; public or private, your call)
  - `int64 GetMarketAnchorMcr(FName Resource, int32 RegionId = 0) const;` plus `GetMarketFloorMcr` and `GetMarketCeilMcr` with the same signature (0 with no market or slot; they read `FindMarket`, so they sync)
  - `int32 GetMarketRegionIdAt(int32 Index) const;` (−1 out of range)
  - `int32 GetRivalRegionId(FName Rival) const;` (the row's `RegionId`; 0 with no row)
  - `int64 GetRivalProduceG(FName Rival, FName Resource) const;` (−1 with no economy entry; reads `FindRivalEconomy`)
  - `int64 GetMarketProdSumG(FName Resource, int32 RegionId = 0) const;`
- **The hook,** under the "Harness hooks (runtime only, never saved…)" heading (H:1606): `bool Debug_SetColonyDepot(int32 RegionId, int32 BuildingId);`. It returns true only when the building exists and is complete, or when `BuildingId` is 0 (which clears the binding). It refuses any `RegionId` other than 0 until T2 (UNCERTAIN: the spec does not say, but it is consistent with rule 2).
- **Private state** beside `RepairClaims` (H:3330): `TMap<int32, int32> DepotBindings; // runtime only, never saved (E7)`.
- **`ConvoyLegSolsFor`** (H:4045) stays private and becomes a one-line call to `GetConvoyLegSols`.
- **Do not add** `GetDepotBlockBytes`, a `DepotTag`, or anything in `RHSaveFrame.h`.

### 3.2 `Source/RedHopeSim/Private/RHSimWorldSubsystem.cpp` (W), or a new `RHSimRegion.cpp` (your call; M is where E4 put its code)

- **`ConvoyLegSolsFor` (W:9724-9729), and the length.** Write one length source, `RouteLengthKm(Row) = (double)Row->DistanceKm`, used by both `GetRouteFor` and `GetConvoyLegSols`:
  ```cpp
  double GetConvoyLegSols(FName Rival) const { FRHRoute R; const double Speed = FMath::Max(ConvoySpeedKmPerSol, KINDA_SMALL_NUMBER); return GetRouteFor(Rival, R) ? R.LengthKm / Speed : 0.0; }
  ```
  This is bit-identical to HEAD's `Row->DistanceKm / Speed`.
  - Performance note: `GetRouteFor` fills `BuildingA` by a building scan. The leg is read on every `StepTrade` per convoy (M:1136) and per frame by V and the deck (M:1269 through `GetConvoyProgress`).
  - If that is measurable, have the leg read the shared length helper directly; it is the same double. RR1 still pins both values, so mutation 2 still bites.
- **Covert loot (W:9831-9841).** Replace the `for (FRHBuildingInstance& B : Buildings) { if (B.DefName == FName("Lander") && !B.bUnderConstruction) ...` loop with a lookup of `FindColonyDepotBuildingId(GetColonyRegionId())`:
  - when it is non-zero, add to that building's `OutputKg` and set `bDropped`;
  - otherwise `AddStock`, as now.

  Keep `NoteFlow`, the broadcast and the fluid branch byte-for-byte. **E6 rewrites `ResolveCovert`** (it passes the command, and theft also draws the victim's store). Its colony side stays HEAD's, so apply this change to whatever loop E6 leaves.
- **Load teardown (W:14041-14042):** add `DepotBindings.Reset(); // E7: the harness depot binding is runtime-only` beside `RepairClaims.Reset()`. This is the ONLY edit inside `LoadColony`. No `SaveColony` edit, no read or write change.
- **World start (W:96):** nothing needed (a fresh subsystem's member is empty). An explicit `DepotBindings.Reset()` there is harmless.
- **Do not touch** W:37 (`RHSaveVersion`), W:13524, W:13968, or the frame writes and reads at W:13928-13933 and W:14740-14743.

### 3.3 `Source/RedHopeSim/Private/RHSimMarket.cpp` (M)

- **M:214, the one arithmetic change:**
  ```cpp
  for (const TPair<FName, int64>& P : Produce) { OutEcon->ProduceG[EconIndex.FindChecked(P.Key)] = P.Value * (int64)Defs->GetProducePermille(Row.RegionId, P.Key); }
  ```
  `P.Value` is int64. There is no division and no double. M:215 (Consume) and M:216 (Start) are untouched.
  - `Defs` is non-null here: `BuildMarketState` returns early without it. `GetRivalTradeKind` also calls `EvaluateRivalRow` with `OutEcon == nullptr`, so guard with the same `if (OutEcon)` block the line already sits in.
- **Convoy landing (M:1203-1219):** the same replacement as the covert loot. The first `Lander` loop becomes `FindColonyDepotBuildingId(GetColonyRegionId())`. Fluids still go to `AddStock`, and the no-building fallback still goes to `AddStock`.
- **Comment text at M:1203 and M:1208** ("at the Lander", "the trade depot (the Lander)"): rewording is optional. These are comments, not logged text. **Do not change any logged text.**
- **Do not touch** `BuildMarketState`'s market part (M:274-333), the load clamp (M:1678-1690), `ClearConvoy`'s fallback (M:657-662), or `WriteMarketPayload`.
- New getters that read private market structs (`GetMarketAnchorMcr`, floor, ceiling, `GetMarketRegionIdAt`, `GetRivalProduceG`, `GetMarketProdSumG`) sit naturally here beside `GetMarketPriceMcr` (M:404).

### 3.4 Definitions: `RHDefinitionsSubsystem.h/.cpp`, `Data/RHRows.h`

- **`RHRows.h`, appended:**
  ```cpp
  USTRUCT(BlueprintType) struct REDHOPESIM_API FRHEndowmentRow : public FTableRowBase
  { GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="RH") int32 RegionId = 0;
    UPROPERTY(...) FName Resource;
    UPROPERTY(...) int32 ProducePermille = 1000;
    UPROPERTY(...) FString Notes; };
  ```
- **Header:**
  - `UPROPERTY(Config) FSoftObjectPath EndowmentsTablePath = FSoftObjectPath(TEXT("/Game/RedHope/Data/DT_Endowments.DT_Endowments"));` beside :231.
  - `UPROPERTY() TObjectPtr<UDataTable> EndowmentsTable;`
  - the index `TMap<TPair<int32, FName>, int32>`.
  - Public:
    - `int32 GetProducePermille(int32 RegionId, FName Resource) const;` (1000 when there is no valid row);
    - `void ForEachEndowmentRow(TFunctionRef<void(FName, const FRHEndowmentRow&)>) const;` (iterate the **DT asset's rows only**, so RR0's CSV-vs-DT check is not polluted by injections);
    - hooks: `Debug_SetEndowment(int32 RegionId, FName Resource, int32 Permille)`, `Debug_InjectEndowmentRow(FName Name, const FRHEndowmentRow& Row)`, `Debug_ClearEndowments()`.
- **`.cpp` Initialize:**
  - add `EndowmentsTable = Cast<UDataTable>(EndowmentsTablePath.TryLoad()); // absent = every permille 1000` after :27;
  - **do not** add it to the "Definitions loaded:" line (:30);
  - build the index with warnings on.
- **Validation:** one Warning per bad row, and each bad row reads as 1000:
  - `ProducePermille` outside 1..100,000;
  - `RegionId < 0`;
  - `Resource` not a resource row (**case-sensitive**, see S2);
  - a (RegionId, Resource) pair that repeats: both rows are ignored, with one Warning naming the pair.
- **The hooks:**
  - Keep injected rows in the subsystem's own containers. **Do not `AddRow` into the loaded `DT_Endowments` asset**, unlike `Debug_InjectRival`. Then `Debug_ClearEndowments` can drop them exactly, and RR0 can compare the asset to the CSV.
  - Each hook rebuilds the index and does `++RivalsGeneration`, so the sim re-derives `ProduceG` on its next read.
  - **How `Debug_SetEndowment` combines with the shipped `R0_Ice` row is blocking issue S1.** The recommended resolution: `Debug_SetEndowment` is an override layer consulted before the table index, not a duplicate row.

### 3.5 Data

- **`docs/data/RH_Endowments.csv`,** exactly REVISION 2 §9 (column 0 `Name`):
  ```
  Name,RegionId,Resource,ProducePermille,Notes
  R0_Ice,0,Ice,1000,"E7 (region-ready): the identity row - region 0 makes Ice at the rate its rival rows say. 1000 = the row as written; 2000 doubles a region's output of a resource, 500 halves it. Step 4's regions add rows. Placeholder, director review."
  ```
- **`Content/RedHope/Data/DT_Endowments.uasset`** (tracked in git: `.uasset` is binary in `.gitattributes`, and `DT_Rivals.uasset` is committed).
  - Create it after the compile with `RH_CREATE=Endowments:RHEndowmentRow`. Use **no `F` prefix**: the script loads `/Script/RedHopeSim.<name>`, and its own example is `Furniture:RHFurnitureRow` (S4).
  - Use `RH_TABLES=Endowments` and `rh_sync_datatables.py`.
  - The expected report lines are `NEW  DT_Endowments … created with RHEndowmentRow`, then `ok   DT_Endowments … rows 0 -> 1`.

### 3.6 `Source/RedHope/Private/RHColonyVisualizerSubsystem.cpp` (V), presentation

- Replace the `Home` loop (V:4337-4342 at `d842dd0`, after the comment `// Home = the Lander pad; destination = the rival's marker.`) with:
  ```cpp
  const FVector Home = Sim->GetColonyDepotLocationCm(Sim->GetColonyRegionId());
  ```
- This is identical in effect: the Lander is placed instant (W:546), never under construction, and never demolishable (W:10571). V's own loop ignored `bUnderConstruction`; the getter applies it. That makes no difference while the Lander stands.
- **Re-anchor after the visual pass and D7 commit.**

### 3.7 `Source/RedHope/Private/RHCommandDeckPanels.cpp`

- At **:2551** (was :2534/:2544): `S->GetMarketPriceMcr(Res)` becomes `S->GetMarketPriceMcr(Res, S->GetRivalRegionId(RowName))`. `RowName` is already captured by the lambda. This adds no visible text change.

### 3.8 `Source/RedHope/Private/RHCheats.cpp`, `RH.Market`

- At **:1090-1136** (re-anchor after the visual pass): wrap the per-resource loop (:1123-1137) in `for (int32 m = 0; m < Sim->GetMarketRegionCount(); ++m) { const int32 Region = Sim->GetMarketRegionIdAt(m); ... }`.
- Pass `Region` to `IsMarketTraded`, `GetMarketTally` and `GetMarketPriceMcr`.
- Log a `"  region %d"` line **only when `GetMarketRegionCount() > 1`**, so one region prints byte-identical lines.
- With 0 markets, HEAD prints no resource lines (`IsMarketTraded` is false), and the loop prints none either.
- The cheat appears in no battery log.

### 3.9 Not touched by E7

- `-scale` (`RHSimCommandlet.cpp:17113/:17118`): leave the default-region reads; its rivals are all region 0.
- Every existing suite's checks.
- `RHSaveVersion`, `SaveColony`, the save format, `BuildMarketState`'s market part, V and the band, E3's price, any UI layout or text.

---

## 4. Save version and frame tag

- **None.** E7 writes no frame and takes no version. A file of the gate before E7 loads under E7 unchanged, and E7's saves are byte-identical to that gate's saves of the same colony.
- E8 follows with v60 and `'RHE8'`, appended after whatever frame the gate before E7 left last. E7 pins no frame position, so E8 edits nothing of E7's.

---

## 5. Tests: the new suite `-regionready`

- **Files:** `Source/RedHopeSim/Private/RHSimRegionReadySuite.cpp`, with `int32 RHRunRegionReadySuite(UWorld*, URHSimClockSubsystem*, URHSimWorldSubsystem*)`.
- **Dispatch:** declare it beside :32-34 and dispatch with `if (FParse::Param(*Params, TEXT("regionready")))` beside `-market` (:14829-14831).
- **Battery script:** append `regionready` to `SUITES` (`rh_battery.sh:37-39`), and bump the count in line 2's comment and line 58's echo by one from whatever your HEAD says.
- **Suite rules:**
  - It fails loudly and logs no Error on purpose.
  - **No check prints a file size or a save version.**
  - Copy MX's helper lambdas (`SaveFilePath`, `MakeRival`/`Inject`/`RetireAll`, `LoadSlot`/`Reset` with `Debug_ClearPolitics` or whatever E6 leaves, `Digest`).
  - Save your own base slot, for example `rrbase`.
  - Use slot names starting `rr` only.
- **Order matters:**
  1. **RR3 first.** It must be the first code in the process to construct `FName("RrZeta")` and then `FName("RrAlpha")`. Construct them at runtime, never as `static const FName`.
  2. RR0.
  3. RR1.
  4. RR2.
  5. **Before RR4: retire every rival injected so far** (MX's `RetireAll`: re-inject with an empty `StoreCap` and `SliceActive=false`, then `Debug_SyncRivalEconomies`). Otherwise their region-0 Ice economy changes `ProdSumG`, `CapSumG` and the store fill, and every RR4 integer below is wrong (S3). Retired rows still appear in `GetDepots`/`GetRoutes` (`ForEachRivalRow` walks every row), which is fine after RR3.
  6. RR4.
  7. RR5.
  8. RR6.

| Check | What it asserts (expected values) |
|---|---|
| **RR3, depots and routes** | Inject `RrZeta`, then `RrAlpha` (economy rows, region 0, trading a resource of your choice; see S3).<br>• `GetDepots`: the colony first (`Owner` None, `RegionId` 0, `BuildingId` = the Lander's Id, 1), then `Meridian, RrAlpha, RrZeta, Zarya`.<br>• `GetRoutes`: the same rival order.<br>• `GetRivalEconomyName(i)` agrees.<br>• Inject `aRival`: it sorts **last** (`Zarya` < `aRival` ordinally).<br>• A rival injected with `RegionId` 1: its route has `RegionA` 0, `BuildingA` the Lander's Id, `RegionB` 1, and the leg is its row's distance / 60.<br>• No record carries an Id; indices move when `aRival` is added, and nothing else does. |
| **RR0, data** | `RH_Endowments.csv` equals `DT_Endowments` row for row (one row: `R0_Ice`, 0, Ice, 1000), in MX:331's way.<br>• `GetProducePermille` answers 1000 for regions 0..3 × every E1 slot.<br>• `Debug_InjectEndowmentRow` with permille 0, permille 100,001, a negative region, an unknown resource, a wrong-case resource (`ice`, see S2) and a repeat of (0, Ice): each logs **exactly one** Warning and reads 1000; a repeat ignores both rows. **Run `Debug_ClearEndowments()` between cases**, because each injection re-validates every injected row (S6).<br>• `Debug_ClearEndowments` restores 1000 everywhere.<br>• The "Definitions loaded:" line is unchanged. |
| **RR1, inertness, exact** | For Zarya, Meridian, a barter rival in Greed's shape (lots outside `StoreCap`, as MX:461's `MxGreed`; Greed is **not** a shipped row) and injected rows at 30 km and **33.33333 km**:<br>• `GetRouteFor` is true; `RegionA` 0; `BuildingA` 1; `RegionB` = the row's; `Kind` 0; `LengthKm == (double)Row.DistanceKm` bit for bit.<br>• `GetConvoyLegSols == Row.DistanceKm / FMath::Max(60.0 (ConvoySpeedKmPerSol), KINDA_SMALL_NUMBER)` bit for bit: **2.0**; **3.3333333333333335**; **0.5555554707845052** at 33.33333 km (the float is 33.33332824707031).<br>• An unknown rival: `GetRouteFor` false and leg 0.0.<br>• `FindColonyDepotBuildingId(0)` == 1, `GetColonyDepotBindingId(0)` == 0, and `GetColonyDepotLocationCm(0)` == the Lander's `LocationCm`.<br>• Every economy rival's `GetRivalProduceG` == row kg × 1000.<br>• Every slot's anchor, floor and ceiling == `EarthValue_mcr` × {1, 0.25, 4} (`MarketFloorBp` 2500, `MarketCeilBp` 40000). |
| **RR2, landing follows the depot** | Place a stockpile with `Debug_PlaceInstant` (it returns **void**: take the Id from `GetBuildings().Last().Id`, since buildings are Id-ascending), then `Debug_SetColonyDepot(0, Id)` returns true. Then:<br>• a standing trade's goods (Zarya's `Ice:150`), a written order's filled solids and a clean covert run's loot land in that stockpile's `OutputKg`, and none in the Lander's;<br>• fluids (for example Water bought from Zarya) join the pool;<br>• E1's `In` legs equal a twin that lands at the Lander;<br>• `GetColonyDepotLocationCm(0)` is the stockpile's spot.<br>`Debug_SetColonyDepot(0, 0)` returns landing to the Lander. A binding to building 999, or to one under construction, returns false and the rule stands. **Demolishing** the bound stockpile (a `Demolish`/`Building` command, as in `RHSimEconomySuite.cpp:685`) returns the role to the Lander at the next read.<br>Covert: there is no hook to force a clean run (the roll is deterministic from `CovertAttempts` and the rival's name, W:9774-9793), so repeat `Covert` until the outcome is clean, or take the `-covert` suite's recipe (`RHSimCommandlet.cpp:1324`). UNCERTAIN how many attempts that takes. |
| **RR4, the endowment** | After the retire, from MX2's Set A Ice state: Zarya's Ice store 12,000 kg (`Debug_SetRivalStore`), the tally 0/0 (`Debug_SetMarketTally`), the price 3,000 (`Debug_SetMarketPrice`), no politics. Then `Debug_SetEndowment(0, Ice, p)` and one `Debug_RollMarket(GetMarketCheckedSol()+1)`, each p from a fresh `Reset()`:<br>• **p = 500:** `GetRivalProduceG(Zarya, Ice)` **200,000**; `GetMarketProdSumG(Ice)` **200,000**; the store **12,050 kg**; the price **2,921**.<br>• **p = 2,000:** **800,000 / 800,000 / 12,650 kg / 2,850**.<br>• **p = 43:** ProduceG **17,200**; the store **11,867 kg**; the price **3,150**.<br>• **p = 1000 (control):** 400,000 / 12,250 kg / 2,850 (MX2's own figure).<br>• `(0, Ice, 9)` gives ProduceG **3,600**; `(0, Struct, 43)` gives Zarya's Struct ProduceG **1,720** (Struct Produce is 40 kg).<br>• `ConsumeG` is unchanged: the store arithmetic is `12,000,000 + ProduceG − 150,000`.<br>• Anchor, floor and ceiling for Ice stay **3,000 / 750 / 12,000**, and for Struct **5,000 / 1,250 / 20,000**. `GetEarthPriceMcrPerKg(Ice)` is unchanged throughout.<br>• A row for region 1 changes no region-0 figure.<br>**The restore check (REVISION 3 §4):** `SaveColony("rrmx4a")`, then `Debug_SetEndowment(0, Ice, 500)`, then `Debug_ClearEndowments()`, then `SaveColony("rrmx4b")`, with no roll or step between. The two files must be byte-identical (`Num` equal and `FMemory::Memcmp == 0`). Diagnostic: `RHSaveFrame::Find(File, MarketTag, …)` on each; `PayloadBytes` equal and `== GetMarketBlockBytes()`; the framed span equal. Every `ProduceG` is back at kg × 1000. A mid-game change rescales no price or store. Every read comes after the resync (R3: getters go through `FindMarket`/`FindRivalEconomy`). |
| **RR5, era parity** | A paired 10-sol run (era band and agent band, as MX1/MX8 pair them). In both twins, `Debug_SetColonyDepot` points at a stockpile and `(0, Ice, 500)` is set before the window. Assert identical (int64 and string equality): the landing building, the landed kg, every price, store and balance, and MX1's `Digest`. |
| **RR6, the save: nothing saved, nothing kept** | MX9's colony shape (two convoys out, an injected rival, a moved market) plus a binding on a stockpile:<br>(i) a save with the binding and a save with it cleared, no step between, are byte-identical;<br>(ii) with the binding set again, save, load, save is byte-identical;<br>(iii) after the load, `GetColonyDepotBindingId(0)` is 0, `FindColonyDepotBuildingId(0)` is the Lander's Id, `GetColonyRegionId()` is 0, and the routes, every `ProduceG`, and every anchor, floor and ceiling equal their pre-save values.<br>Assert and print **no** version, size, frame or frame order. |

### Mutations (REVISION 3 §8: build, run and restore each by sha256; each must fail the named check)

1. Landing hard-coded to the first Lander (the binding ignored): fails **RR2**, and RR5's landing building.
2. Route length kept in whole metres, with the leg read as `LengthM / 1000.0`: fails **RR1** at 33.33333 km (0.55555 against 0.5555554707845052).
3. Rival depots and routes in FName index order: fails **RR3**'s reverse-first injection.
4. The endowment applied to Consume (M:215) too: fails **RR4** at 500 (the store reads 12,125 kg) and the Consume pin.
5. Production scaled in double, `(int64)(kg*1000*(Permille/1000.0))`: fails **RR4** at `(0, Ice, 9)` (3,599 against 3,600) and `(0, Struct, 43)` (1,719 against 1,720).
6. `RegionA` taken as the colony's depot in the rival's region: fails **RR3**'s region-1 rival.
7. The no-row default answering 0: fails **RR0** and **RR1**.
8. The endowment index keyed case-insensitively or by FName index (a row for `ice`): fails **RR0** and RR4. **As specified this mutation cannot fail. See S2.**
9. `DepotBindings.Reset()` removed from the load teardown: fails **RR6 (iii)**.

Record the hashes and the failing checks in a table in `design-decisions.md`, in the E4 MX12 format.

---

## 6. Battery: which lines move

- **Expected: no existing line moves.** Every existing `.log` diffs at **0 differing lines** against the battery baseline of the gate before E7 (`Saved/baselines/battery_post_<D7>`), version lines and size lines included. The only addition is `regionready.log: NEW SUITE`.
- **Why nothing moves:**
  - E7 writes no byte of any save;
  - `ProduceG` is kg × 1000 at the identity row and at the no-row default;
  - landing picks the same building (the Lander);
  - the leg is the same double;
  - the "Definitions loaded:" line is unchanged;
  - no existing suite calls an E7 hook.
- **`market.log` has no exception.** MX9's `Ver ==`, its "v%u" text, the refusal of the previous version, and "(N bytes)" all read files E7 leaves alone. The four load-clamp "outside" Warnings still fire, because the kept-price clamp stays struck.
- **Judge** on `errors=0 completed=1` for every run.
- **The pins** must hold in `baseline.log`: `power: gen 50 W load 20 W` and `deposit Regolith_A: 40645 kg`.
- Any other moved line means E7 is not inert. Stop and report; do not re-baseline.

---

## 7. Other bars

- **Playtest:** **byte-identical.** Run `RH_PLAYTEST_DISTRICTS=1 bash scripts/sim/rh_playtest.sh 110 e7_full guide full`, then `cmp` it with `Saved/baselines/ind_full_playtest.txt` (or the baseline the gate before E7 left).
  - Why: `RHPlaytest.cpp` uses no convoy, order, market or covert verb, and the file prints no save size or version.
  - Even if it traded, landing, leg and production are unchanged.
- **The one-time pre/post `cmp` (REVISION 3 §5):**
  1. **Before the first E7 compile,** with the gate-before-E7 binary, run `-run=RHSim -market -sols=10`, then copy `Saved/SaveGames/RH_mktmx9.sav` to `Saved/baselines/regionready_pre/RH_mktmx9.sav`.
  2. **UNCERTAIN:** whether that file is byte-stable across processes was not verified (no timestamp is written in `SaveColony`, but this was not run). So run the pre binary's `-market` **twice** and `cmp` the two copies first. If they differ, the cross-binary check proves nothing: stop and ask the orchestrator.
  3. After E7, run `cmp` on the pre copy against E7's `RH_mktmx9.sav`. It must exit 0.
  4. Record "identical, N bytes, version V" in the entry.
  5. If the pre binary is gone, use REVISION 2 §10's worktree recipe (`git worktree add`, never a checkout or reset in the main tree).
  6. Confirm the slot is still `mktmx9` after E5 and E6.
- **`-scale`** (outside the battery):
  - digests identical;
  - the market line (`RHSimCommandlet.cpp:17118`, ≤ 2.0 ms) holds;
  - `GetRoutes` and `GetDepots` at 10 rivals under 0.05 ms a call.

  Measure the last with a local, uncommitted timing (or record it in the entry). **Add no `-scale` line** (S7).
- **Click test:** no UI change is intended. The deck and `RH.Market` edits change no visible text. The RedHope module does change (V, the deck, the cheat), so run `RH_CLICKTEST_RES=1440x860 bash scripts/unreal/rh_clicktest.sh` once as insurance, and expect PASS.
- **Captures:** optional. A convoy rover leaving the Lander, before and after, should look identical.
- **dylib:** the RedHopeSim dylib changes, as it must. RedHope changes too (V, the deck, the cheat).

---

## 8. Deviations the builder must NOT take

- **Saving:** no version bump, no `'RHE7'`/`DepotTag`, no `GetDepotBlockBytes`, no saved binding, no saved `ColonyRegionId`. `Debug_SetColonyDepot` stays runtime-only.
- **No `TradeDepot` building.** No menu item, mesh, card, Gate-D row, or `-dayshift` or click-test change.
- **Do not scale V.** Leave alone the band, the start price, kept prices, E3's Earth price and `ClearConvoy`'s fallback, and add **no** kept-price clamp in `BuildMarketState`. If you clamp, MX9's "outside" Warnings vanish and `market.log` moves.
- **No Ids** on `FRHDepot`/`FRHRoute`. No "route Id = depot Id − 1". No cache of depots or routes.
- **No integer metres** and no `llround(km*1000)` for `LengthKm`. No `double` in the permille arithmetic.
- **Do not move the non-trade Lander roles:** store, haul sink, anchor and salvage, refunds (`CreditLander`), road refunds, rover docks, orphaned cargo, origin pad search, bearings, manifest spawn.
- **Do not change the default `RegionId = 0` arguments** (H:1560-1597) or `-scale`'s reads. Do not edit any existing suite.
- **Do not add** endowments to the "Definitions loaded:" line, and do not `AddRow` injected endowment rows into the loaded asset.
- **Do not rename E8's getters.** E8 reads `GetRoutes`, `GetRouteFor`, `GetColonyRegionId`, `GetColonyDepotLocationCm`, `GetConvoyLegSols`, `GetMarketFloorMcr`/`GetMarketCeilMcr`, `GetMarketRegionIdAt` and `GetRivalRegionId` by these exact names.
- **Repo safety:** never stash, checkout or reset in the tree. Commit only E7's files, by explicit path: the suite, H, W (or the new .cpp), M, the definitions .h/.cpp, `RHRows.h`, the CSV, `DT_Endowments.uasset`, V, the deck, the cheat, `RHSimCommandlet.cpp`, `rh_battery.sh`, `design-decisions.md`.

---

## 9. Spec problems found in pre-flight

- **S1 (BLOCKING): `Debug_SetEndowment(0, Ice, p)` collides with the shipped identity row `R0_Ice`.**
  - **The conflict:**
    - REVISION 2 §9 ships `R0_Ice,0,Ice,1000` and rules that a repeated (RegionId, Resource) pair ignores **both** rows with a Warning.
    - REVISION 2 §7 makes `Debug_SetEndowment` "an injected row named `Dbg_<Region>_<Resource>`".
    - RR0 even uses "a repeat of (0, Ice)" as its duplicate case.
  - **The effect:** built as written, `Debug_SetEndowment(0, Ice, 500)` makes (0, Ice) a duplicate, so it reads 1000 with a Warning. RR4's 500 / 2,000 / 43 / 9 Ice cases and RR5's (0, Ice, 500) then all fail.
  - **Fix (recommended):** `Debug_SetEndowment` writes an override map consulted **before** the table index. It is range-checked, exempt from the duplicate rule, and cleared by `Debug_ClearEndowments`. `Debug_InjectEndowmentRow` stays a raw row subject to full validation, so RR0's (0, Ice) repeat still tests the duplicate rule.
  - Alternative: ship the identity row for a pair no test sets.
  - No director needed.
- **S2 (BLOCKING, for the mutation gate): mutation 8 cannot fail as specified.**
  - The spec's own index is `TMap<TPair<int32, FName>, int32>`. FName equality and hashing are case-insensitive, and `GetResource(FName("ice"))` (`FindRow`) finds `Ice`.
  - So a row for `ice` is accepted and applies to Ice: the prescribed build *is* "keyed case-insensitively", and the mutation is a no-op.
  - RR0 lists only "an unknown resource", so a correct-looking RR0 cannot catch it.
  - **Fix:** validate `Row.Resource.ToString()` against the resource rows' key strings with `ESearchCase::CaseSensitive`. That works in editor and commandlet builds, which are case-preserving (UNCERTAIN in a packaged build, which is irrelevant here). Then add an explicit `ice` case to RR0 (one Warning, reads 1000). Mutation 8 becomes "drop the case-sensitive compare". Alternatively, strike mutation 8.
  - No director needed.
- **S3 (minor): RR4's integers assume Zarya is region 0's only Ice economy.** RR3 runs first and RR1 injects rows, and injected rows persist for the process: `Debug_InjectRival` `AddRow`s into the live `DT_Rivals`, and `LoadColony` does not remove them.
  - If any injected region-0 economy lists Ice, `ProdSumG`, `CapSumG` and the fill change, and 2,921 / 2,850 / 3,150 / 12,050 are wrong.
  - **Fix:** RR3/RR1 economies avoid Ice and Struct, or retire every injected rival before RR4 (MX `RetireAll`).
- **S4 (minor): the wrong `RH_CREATE` struct name.** REVISION 2 §9 writes `RH_CREATE=Endowments:FRHEndowmentRow`. The script loads `/Script/RedHopeSim.<name>`, and reflection drops the `F`, so this prints "FAIL … row struct FRHEndowmentRow not compiled". Use `Endowments:RHEndowmentRow`; the expected log says "created with RHEndowmentRow".
- **S5 (minor): a text contradiction.** REVISION 3 §1 and §9 say "`LoadColony` (W:13946) is not edited by E7" and "does not touch … `LoadColony`". The same §1 and mutation 9 require `DepotBindings.Reset()` in `LoadColony`'s teardown. Read the "not edited" lines as "no save-format change": the one-line Reset is required.
- **S6 (minor): RR0's "exactly one Warning".** Each hook "rebuilds the index with warnings on", so every later injection re-warns earlier bad rows. Clear between cases, or count only new Warnings.
- **S7 (minor): the `-scale` timing has no home.** "`GetRoutes`/`GetDepots` under 0.05 ms a call" sits beside "No new `-scale` line". Measure locally, record the figure in the entry, and commit no timing line.
- **S8 (minor): moved anchors.**
  - The deck read is at `RHCommandDeckPanels.cpp:2551`, not :2544.
  - The `RH.Market` reads are at `RHCheats.cpp:1127/:1133/:1136`, not :1126/:1132/:1135.
  - V and `RHCheats.cpp` are being edited right now by the uncommitted visual pass, and D7 edits V. E5 and E6 move M and W lines (E6 rewrites `ResolveCovert`).
- **S9 (minor): two battery-script lines the spec omits.** `rh_battery.sh` line 2 (the comment's suite/run count) and line 58 (`echo "=== N suites ==="`) need the +1 that E4 also made. E5 to D7 may each add a suite first.
- **S10 (minor): no shipped "Greed" row.** `RH_Rivals.csv` has only Zarya and Meridian. RR1's "a barter rival (Greed's shape)" must be injected (MX:461's `MxGreed` recipe). Section 1's "Barter rivals (Greed)" describes a shape, not a row.
- **S11 (minor): `Debug_PlaceInstant` returns void** (H:2651). RR2 needs the new building's Id from `GetBuildings().Last()`.
- **S12 (UNCERTAIN): the pre/post `cmp` assumes `RH_mktmx9.sav` is byte-stable across processes.** This was not verified. Run the pre binary twice first.
- **S13 (note): E6 composes with the endowment.** E6's sabotage cut multiplies the E7-scaled `ProduceG` in `MarketProduceAndReprice`, which is consistent. RR4 must run with politics and sabotage cleared, using whatever E6 leaves in place of `Debug_ClearPolitics`. E6 also moves E4's `Debug_` hooks into its rule list; E7's hooks take E6's hook shape.
