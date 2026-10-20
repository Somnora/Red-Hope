# D1, floors become places: design (2026-10-21)

Step 2 (going deep), gate 1. Design only. No code is written. Anchors were read on main @ 9745c51 (save v40).
Sources: `docs/qa/2026-10-20/going-deep-plan.md` (D1 row, D1 risks, appendix); `docs/design-decisions.md` "Step 2, going deep" (Q2a: floors are painted shapes the Borer carves in order); the C4 REVISION (house rules); C6 (the commute's floor anchors).

**Goal.** A floor stores its carved cells as an ordered list of integer lattice points, plus a RegionId. It no longer stores a count around the shaft head. `SpiralCell` survives only as the Borer's default carve order, so today's floors carve the same cells in the same order. Nothing visible changes. Save v41, strict.

---

## 1. Inventory: who reads cells today, and how each changes

"Ordinal" below means a cell's position in its floor's carve order. On every floor that exists today, ordinal i is spiral index i (proved in section 4).

**Sim state and core (RHSimWorldSubsystem.h / .cpp)**
- `FloorCarvedCells` (TMap Level -> count), `.h:2133`. **Removed.** It is replaced by the floor table (section 2). `GetFloorCarvedCells` (`.h:441`) stays as `Floor.Cells.Num()`. It is a count, which is still honest.
- Carve sites: `.cpp:1354` (Borer batch completion) and `.cpp:6365` (`ExcavateFloor`). Both now append `NextCarveCell(Level)` once per cell. Their logs are unchanged.
- Head fixing: `.cpp:1346` and `.cpp:6336`. These also anchor region 0's origin (section 2).
- `SpiralCell` (`.cpp:4590`, `.h:740`). Its only caller is now `NextCarveCell`. Every position reader below moves off it.
- `FindCarvedCellAt` (`.cpp:5093-5110`) keeps its rounding formula verbatim. It then does an O(1) `IndexOf` lookup instead of the O(n) scan, and returns the ordinal. Callers: `FindModuleOnCell` `:5085`, build check `:6659`, `ResolveFurnitureKey` `:9351`.
- `CellPos` (`:2722`) returns `Floor.Cells[ordinal]`.
- `GetCellCentreCm` (`:9385`) returns origin + lattice x 1000 (section 4).
- `IsFurnishKeyValid` (`:9457`) asks "is this lattice point in the floor's `IndexOf`?" instead of "is the index below the count?". The message text keeps the ordinal.
- Fixed-depth loops become "floors that exist, shallowest first": `StepHabitability` `:2367`, `PickFillHome` `:3207`, the seat scan `:4839`.
- `RHSimDistricts.cpp:327` is a bound check, not a loop, so it stays. D5 raises `MaxDepth`.
- Adjacency (`:5295`) currently re-walks the spiral inline. It reads `Floor.Cells[i]` instead (section 4).
- Per-floor counts at `:2369`, `:3086`, `:3164`, `:4616`, `:5976`, `:9664`, and `RHSimDistricts.cpp:271` keep `GetFloorCarvedCells`. No change.
- Key printers. These keep printing the ordinal, through `LegacyTriple` (section 3): `:3613`, `:4654`, `:7083`, `:9461`, `:9969`, and the garden lines `:3794`, `:3880`, `:3889`.
- Sorts that order by the key's cell field. Both must use `LegacyTriple`, never the lattice point:
  - the bed-slot sort `:10047` (`Key.Y * 1000000 + Id`);
  - the save sorts of planted cells and crops `:11178`, `:11244`. These fix the TSet/TMap insertion order after a load.
- Uplink orders that carry a cell ordinal in `Cmd.Value`: Assign `:7067-7068`, Furnish `:7107` / `:9373` / `:9615`, SellCell `:7177`, Zone `:7205`. Each resolves ordinal -> key at execute time. See risk R4.
- The -scale builder (`:3310`, `:3351`) reads `GetFloorCell(L, I)` instead of `SpiralCell(I)`.

**Growth (RHSimGrowth.cpp)**
- `GrowthSpiralPositions` (`:49-70`) is **deleted**. Its callers change as follows:
  - `GatherFloorRoomGrid` `:427-429` reads `Floor.Cells`.
  - `:1051-1053` and `:1275-1277` used `Pos[Carved]` to mean "the cell that would be carved next". They become `PeekNextCarveCell(Level)`.
- Key builders `:410`, `:1247`, `:1306` build `FloorCellKey(Level, ordinal)`.
- Project fields (`RHGrowth.h:37-40`):
  - `FirstCell` is a count snapshot, so it stays an int.
  - A floor CivicRoom's `Index` (an ordinal) becomes a new saved field `FloorCell` (FIntPoint lattice). `Index` stays for bays.

**Commute (RHSimCommute.cpp).** Floors are anchored at the shaft head (`:60`, `:70`, `:172`), not at cells. Station keys are read only for their Level and owner (`:228`). **No change.** The C6 anchors stay identical. The type of `ColonistStations` changes, but its Level and owner fields mean the same.

**Districts (RHSimDistricts.cpp).** A floor district is still a whole floor (`:227-229`). `GetDistrictOfCell(Level, CellIndex)` (`.h:1403`) keeps the ordinal adapter. The loops at `:683` and `:706` read `FloorRoomCells` unchanged.

**Presentation (RedHope).** Every `Head + SpiralCell(i) x 1000` becomes `Sim->GetFloorCellCentreCm(Level, i)`, or origin + `GetFloorCell(Level, i)` with the same expression shape:
- `RHPlayerController.cpp:588-593`, `:821-822`, `:1201-1202`.
- `FindCellAt` `:1972-1976`: keep its inclusive ±500 scan, first hit wins, but iterate the floor's cells.
- `:1064` builds the key through `FloorCellKey`.
- `RHColonyVisualizerSubsystem.cpp`: `:3448` (floors that exist), `:3481`, `:3956`, `:4163-4167` (delete the wrapper), `:4595`, `:2104` (`LegacyTriple` for `TileByCell`). Its `TileByCell` stays ordinal-keyed: it is a presentation map, and floors are append-only.
- `RHCrewVisualizerSubsystem.cpp:807`, `:840`, `:859`, `:1262`: `SeatOnRing(origin, lattice)`; the cache key becomes `FloorCellsVersion`.
- `RHDistrictVisualizerSubsystem.cpp:298-329`, `:412`.
- `RHGrowthVisualizerSubsystem.cpp:253-269` (`PeekNextCarveCell`), `:322`.
- `RHCommandDeck.cpp:1100-1101`, `:1458-1460`, `:1619-1636`.
- `RHCheats.cpp:461-473`, `:1415`, `:1619`.
- `RHPlaytest.cpp` and `RHCommandDeckGuide.cpp` use counts and ordinals only. No change.

**Commandlet tests.**
- `RHSimCommandlet.cpp` has 66 `FIntVector` uses. Key literals such as `FIntVector(-1, 6, 0)` (`:1956`) become `Sim->FloorCellKey(-1, 6)`.
- Key prints such as `:1957-1959` print `LegacyTriple`, so the text is unchanged.
- `:1888` (`CellCentre` lambda) calls `GetFloorCellCentreCm`.
- The furnish snapshot (`:3279`) and the -scale digest (`:10999`) mix `LegacyTriple`, so the digests are unchanged.

## 2. The data

**Lattice frame.** One frame per region:

`FRHUndergroundRegion { int32 RegionId; FVector OriginCm; bool bAnchored; }`

- Region 0 is anchored once, at the first bore (`:1346` and `:6336`, the same lines that fix `ShaftHeadCm`). `OriginCm = ShaftHeadCm` there, and it never moves afterwards.
- A lattice point is `FIntPoint` in 10 m units in that frame. It is absolute within the region: it does not depend on any shaft.
- Shaft 0's column is lattice (0,0) because the frame was anchored on it. D2's shafts snap to the frame and get their own column point.
- **Why not the world 10 m grid (`LatticeOf`, origin 0)?** The head sits wherever the Borer stood, so it is off-grid in live play. Cell centres would move by up to 5 m, which is not invisible. See R1.

**Floor table.**

`FRHFloor { int32 RegionId; int32 Level; TArray<FIntPoint> Cells; TArray<FIntPoint> Plan; TMap<FIntPoint,int32> IndexOf; }`

- `Cells`: carve order, append-only, no duplicates, never contains the shaft column.
- `Plan`: painted points not yet carved, in paint order. D1 sets it through the Debug API only.
- `IndexOf`: derived, lookups only, never iterated. It is rebuilt on append and on load.
- Storage is a `TArray<FRHFloor>` kept sorted by (RegionId, Level descending), plus a derived `FloorIndexByLevel` map.
- "Floors that exist" means that array in order. It holds only floors with at least one cell.

**Carve order.**
- `NextCarveCell(Level)`: if `Plan` is non-empty, pop its first point. Otherwise take the lowest spiral index k whose point is not yet carved.
- A transient cursor makes the second rule O(1) amortised. It is re-derived on load, so nothing extra is saved.
- `PeekNextCarveCell` is the same rule without the pop.
- The cell is still chosen at batch completion, so `PendingBoreWork` keeps its format.

**Cell-i APIs.** These are kept as thin ordinal adapters. Ordinal means position in carve order; it is stable because floors are append-only.
- Adapters: `DesignateRoom`, `GetRoomAt`, `IsGardenPlanted`, `GetCellCrop*`, `GetDistrictOfCell`, `FindCarvedCellAt`, `IsCellFreeForRoom`.
- New, lattice-based: `GetFloorCell(L,i)`, `FindFloorCell(L,P)`, `GetFloorCellCentreCm`, `FloorCellKey(L,i)`, `LegacyTriple(K)`, `GetFloorLevels()`.
- New code in D2 and later uses the lattice forms.
- **Rooms stay a parallel column.** `FloorRoomCells` (`.h:2293`) stays `TMap<int32, TArray<FName>>`, indexed by ordinal, with today's "prefix up to the last designation" length. This is deliberate:
  - `:3571` (`FloorRoomCells.Num() == 0` is the zero-garden early-out), `:3503`, `:3547` (`Contains(Level)`), and the adjacency grid all depend on that exact shape.
  - Rooms therefore sit on lattice points through the floor table, not through a key of their own. This departs from the plan's wording. See R3.

**Version counter.** `FloorCellsVersion` (uint32) is bumped on every append, every plan change, and every load; it only goes up, as in C6.
- No sim cache keys on floor cells today:
  - adjacency is computed per Hope call;
  - `GatherFloorRoomGrid` is computed per call;
  - the commute keys on the shaft head only;
  - district counts are read live.
- So in D1 the counter feeds the presentation caches: the crew room cache (`:807`) and the growth and district visualizer keys. D2 and D3 caches (shaft reach, tunnel graph) add it to their keys.

## 3. The key migration

New `USTRUCT FRHCellKey { uint8 Kind; int32 RegionId; int32 Level; FIntPoint Cell; int32 OwnerId; }`. The kinds mirror today's three meanings exactly:

| Today (`FIntVector`) | Kind | Fields |
|---|---|---|
| `(L<0, cell, 0)` | FloorCell | Region, Level, Cell = lattice point, Owner 0 |
| `(L, INDEX_NONE, BuildingId)` | Module | Level, Owner = BuildingId |
| `(0, bay or bed, HabId or DomeId)` | BuildingSlot | Cell = (index, 0), Owner = building |

- The RegionId field leaves room for regions. A shaft is not part of the key: a cell belongs to a floor, and which shaft reaches it is derived in D2.
- Equality compares all fields. `GetTypeHash` hashes all fields.
- **There is no `operator<`.** Any code that needs an order goes through `LegacyTriple(K)`, which returns today's `(Level, ordinal or index or INDEX_NONE, Owner)`.
- Migrated containers:
  - `FRHFurnitureInstance::Key` `.h:171`
  - `SeatPriority` `:2301`
  - `ColonistStations`, `StationFilled`, `StationCapacity` `:2305-2307`
  - `FurnishedCells` `:2206`
  - `PlantedCells` `:2534`
  - `PlantedCrops` `:2538`
  - `FRHJobAssign::Key` `:1889`
  - the `FRHOnFurnitureChanged` delegate `:210`
- Wall edges (`:187`, `FeatureByEdge`) are a surface bay lattice and stay `FIntVector`.
- **Determinism.** TSet and TMap iterate in sparse-array order, which is set by the order of adds and removes, not by hash value. Every add and remove happens at the same step and in the same order as today. So iteration order is unchanged, even though the key type and its hash change. The one place where order is rebuilt is load, and it is pinned by the `LegacyTriple` save sorts.

## 4. Why every log line stays identical

1. **Same cells, same order.** Claim: on a floor with an empty `Plan`, the n-th carve appends `SpiralCell(n)`.
   - Induction: suppose cells 0..n-1 are exactly `SpiralCell(0..n-1)`. The lowest uncarved spiral index is then n.
   - `SpiralCell` never yields (0,0) and never repeats a point.
   - Every battery floor is carved by `ExcavateFloor` or the Borer, with no plan. So `Cells[i] == SpiralCell(i)` and ordinal == today's index everywhere.
2. **Bit-identical floats.** Today a centre is `Head.X + P.X * 1000.0` (`:9385`, `RHPlayerController.cpp:593`, and the others).
   - New: `Origin.X + L.X * 1000.0`.
   - `Origin` is a bitwise copy of `ShaftHeadCm`, taken on the same line that sets it, and `L == P` by point 1. Same operands and same operations, so the doubles are identical.
   - Rule: each call site keeps its own expression shape. Only the operands are swapped; no refactor into a shared helper may change the evaluation order.
   - The inverse keeps `FindCarvedCellAt`'s exact `RoundToInt((X - Head.X) / 1000.0)`. It must **not** be unified with `LatticeOf` (`RHSimDistricts.cpp:50`): that one uses `FloorToInt32(x + 0.5)`, which differs at -0.5.
3. **Iteration order.**
   - `StepHabitability`, `PickFillHome` and the seat scan walk existing floors from Level -1 down. This is the old -1..-MaxDepth walk minus floors that are absent:
     - `StepHabitability` already skips 0-cell floors;
     - the other two require `RatedFloors`, and rating needs cells (`:3164`).
     - A load refuses `RatedFloors` that are not a subset of the existing floors.
     - The walk keeps an `ensure(Level >= -MaxDepth)`.
   - The seat scan inside a floor walks `FloorRoomCells` by ordinal, then buildings by array order, exactly as before.
   - Adjacency `:5295`: `Pos[i] = Floor.Cells[i]` for `i < Cells.Num()` of the room array. That array is never longer than the cell count: `DesignateRoom` sizes it at `:4641`, and load enforces it. These are the same points, the same grid and the same emitter order `E = 0..N-1`.
   - Growth `:49`: the same points (point 1). `Pos[Carved]` equals `PeekNextCarveCell`.
   - Districts: there is no cell iteration; a floor is still whole.
4. **Printed numbers** are ordinals, through `LegacyTriple`, and equal today's indices (point 1).
5. **The one known exception.** `-traffic` X15 (`RHSimCommandlet.cpp:10794`, `:10800`) hard-codes version 40.
   - The test must become "the file says the current version" and "the previous version is refused".
   - Its two lines in `traffic.log` change text, from "40/39" to "41/40". These are the only expected diff lines.
   - The C6a precedent was the same: X15 was added with that bump.

## 5. Save v41 (strict)

- **Removed:** `FloorCarvedCells`, at `:11107` and `:11586`.
- **Added, in the same slot:**
  - the region list: RegionId, OriginCm, bAnchored;
  - the floor list in table order: RegionId, Level, Cells (count, then X Y pairs), Plan.
- **Changed key fields**, written field by field: `FRHFurnitureInstance::Key` `:11832`, `SeatPriority` `:11270` / `:11784`, `PlantedCells` `:11177`, `PlantedCrops` `:11243`.
  - Planted and crop entries are written sorted by `LegacyTriple`. The load order, and so the post-load set order, is then identical to v40.
- **Growth project:** the new `FloorCell` field, at `:11968`.
- **Unchanged:**
  - `FloorRoomCells` (ordinal into the saved `Cells`);
  - uplink orders (ordinals; see R4);
  - `CarveQueue`;
  - `PendingBoreWork`;
  - `ShaftHeadCm`.
- **Load refuses** (with an error, leaving the live colony untouched):
  - any version other than 41, including v40, through the existing check at `:11455`;
  - a duplicate point, or the shaft column, in any floor;
  - a `Plan` point that is already carved;
  - a room array longer than its floor;
  - a floor-cell key that is not in its floor;
  - `RatedFloors` that are not a subset of the existing floors;
  - `Origin != ShaftHeadCm` for an anchored region 0 while `ShaftDepth > 0`.
- The hand-play checkpoints are refused as v40 and re-make themselves, as at every bump since v36.

## 6. Tests

**New `-deep` suite.** It fails loudly (`Expect` writes an Error) and is added to `rh_battery.sh`.
- **K1.** Borer carves 30 cells and `ExcavateFloor` carves 30 more. Check:
  - `Cells[i] == SpiralCell(i)` for all i;
  - `FindCarvedCellAt(GetFloorCellCentreCm(L,i)) == i`;
  - each centre equals the old formula with `==`, not a tolerance.
- **K2.** Carve 12 with a painted plan pending, save, load, carve 6 more. The result equals a control colony that carved all 18 without saving: same cells, same order, same `Plan` left over.
- **K3.** Round trip at origins (1000,1000), (3000,3000), (123456.7, -98765.4) and (-2e6, 2e6), for lattice points in [-300,300]²: lattice -> centre -> lattice gives the point back.
- **K4.** Furniture on floor cells and in bays; a seat priority holding a floor cell, a module and a bay; planted cells with crop state. Save, then load. Check:
  - keys are equal and `LegacyTriple` is equal;
  - beds, seats and the Hope breakdown are exactly equal.
- **K5. A painted, non-spiral shape** (`Debug_PaintCarve` only): an 8-cell line 60 m from the head, then a 3 x 3 block, in a deliberately non-spiral order.
  - The Borer carves it in the given order.
  - Zone a Cooking emitter beside a Living refuser: the offence count equals a hand count. A Hallway between them cancels it.
  - Lab seats fill in carve order.
  - A furnishing is placed and resolves to its cell.
  - `FindCarvedCellAt` inverts every cell.
- **K6. Refusals.** Painting the shaft column, a duplicate, or a carved point is refused with a reason. A v40 file is refused. A hand-made v41 file with a duplicate point is refused, and the colony is untouched.
- **K7.** Bore to -3 and carve only -1 and -3. `GetFloorLevels()` returns [-1, -3], and `StepHabitability` visits exactly those two floors.
- **K8.** On a sparse painted floor, the adjacency grid path and the hash fallback (R2) give identical offence counts.

**-scale** runs outside the battery. Record before and after numbers; every existing budget must hold (`RHSimCommandlet.cpp:11260-11267`, `:11356-11357`). The digests mix `LegacyTriple`, so they are unchanged.

**Battery.** Run `bash scripts/sim/rh_battery.sh 10`, then `python3 scripts/sim/diff_battery.py Saved/baselines/battery_post_c6b Saved/RHBattery`. Expected result: zero diff lines except the two X15 lines in `traffic.log` (section 4, point 5). `deep.log` is new.

## 7. Era parity

None expected. D1 changes no rate, no step order and no step function. The era step (`EraStepOnce`) calls the same `StepHabitability`, `RefreshJobs` and Hope code over the same data, so the ledger pairs cannot move. The era refusals (C4) need nothing new, because carving already runs through shared code.

## 8. What D1 does not do

- No second shaft; there is still one `ShaftHeadCm` and `ShaftDepth`.
- No tunnels; robots do not go below; `RobotLevel` stays 0.
- No painting UI; `Debug_PaintCarve` exists for tests only.
- No cell removal.
- No underground district shapes; no depth costs; no view changes.

Hooks it leaves:
- the region frame, which D2 shafts snap to;
- `RegionId` in floors and keys;
- `Plan` plus `NextCarveCell`, ready for Q2a brushes (D2 and D6);
- `FloorCellsVersion`, for D2 and D3 caches;
- `GetFloorLevels()`, which D5's deeper floors reuse;
- lattice-keyed persistent state, so D6 can reshape districts without re-keying.

## 9. Risks I see

- **R1. The frame is not the world grid.** Underground lattice points are offset from the surface `LatticeOf` lattice by the head's sub-10 m remainder.
  - D6 (underground district shapes) and D3 (shaft-to-road joins) must convert between the two through the origin, never compare them directly.
  - Snapping the first Borer to the world grid would unify them, but it is a visible change, so it is deferred to D2.
- **R2. Painted shapes break the adjacency grid's size.** The grid is a dense bounding box (`:5320`). A long painted tunnel could make it about 460 x 460.
  - Fallback: when the box area exceeds 8 x N, look cells up through `IndexOf`. The results are identical (K8). Spiral floors never take the fallback.
- **R3. Rooms stay ordinal-indexed** (section 2). This is safe only while floors are append-only.
  - Any gate that adds cell removal must splice `FloorRoomCells` and re-derive ordinals. D1 asserts append-only: there is no API that removes a cell.
- **R4. Queued uplink orders carry ordinals** in `Cmd.Value`. They are resolved at execute time.
  - Safe for the same append-only reason, and an order's lifetime is short.
  - Moving them to lattice points would change the format of `EncodeFurnish` (`.h:329`) and every test that issues them. That would widen the battery risk for no D1 benefit.
- **R5. Blast radius.** About 200 `FIntVector` key sites across both modules, 66 of them in tests.
  - A missed `Key.Y` read compiles wrongly only if someone writes a conversion. To prevent that, `FRHCellKey` has **no** implicit conversion and no `X/Y/Z` members, so every miss is a compile error.
- **R6. Hash cost.** The key grows from 12 to 24 bytes. `RebuildFurnitureCache` (2,500 pieces in -scale) and the station maps hash more bytes.
  - This should be well under the 0.5 ms Hope budget, but it is unmeasured. -scale is the gate.
- **R7. X15's two lines** are an expected diff. The reviewer must see them named, not waved through.

**Director questions:** none. D1 is meant to be invisible, and R1's snap is a D2 decision.

---

# PROFESSOR REVIEW (2026-10-21)

**Verdict: BLOCKED.** The core works. The floor table, carve order, bit-identity argument, iteration-order argument and append-only departures all hold against the code. But the doc says "zero unnamed diffs", and one battery line will diff that it does not name. It promises load refusals that "leave the live colony untouched", which `LoadColony` cannot do as written; and as K6 is written it would fail the battery's error gate. Four smaller spec gaps break things at runtime. Required changes 1-7 form the REVISION. Once they are written into this doc, D1 is cleared to build without another review round. Read optional item O1 first: it removes required items 1 and 7 outright.

## What I verified and found sound
- **Iteration order (attack 3).** In UE 5.8 the default `TSet` is the sparse set (`UE_USE_COMPACT_SET_AS_DEFAULT 0`, `Engine/Source/Runtime/Core/Public/Containers/ContainerAllocationPolicies.h:1912`). Its iteration order comes from the sparse array's add/remove/free-list sequence and never from hash values. The compact set's swap-remove would not use hashes either. None of the migrated containers is ever `KeySort`ed, `Compact`ed or `Sort`ed; the only `KeySort` calls are on FName maps (`RHSimWorldSubsystem.cpp:8764`, `RHCommandDeck.cpp:2182`). The save sorts (`:11178`, `:11244`) are lexicographic on (X,Y,Z) over unique keys, so `LegacyTriple` reproduces them exactly. The claim holds.
- **Bit identity (attack 2).** `ShaftHeadCm` is written only at `:1346` and `:6336`, both guarded by `ShaftDepth == 0`. No path resets it except load. The centre expression is `:9386`. A bitwise copy taken on those same lines gives identical doubles. The inverse at `:5102-5103` is kept verbatim. Carving needs `IsLevelConnected` (`:6360`), so no floor can exist before the frame is anchored.
- **The Borer rule (attack 6).** `SpiralCell` steps before it returns (`:4590-4609`), so index 0 is (1,0). The walk is a bijection onto Z² minus the origin, so it never yields the shaft cell. The induction is correct. The inline copies of the walk (`:5295-5310`, `RHSimGrowth.cpp:50-69`) are the same walk. Choosing the cell at batch completion (`:1354`) keeps `PendingBoreWork` unchanged.
- **The two departures (attack 5).** Q2a ("the Borer carves them in order", `design-decisions.md:573`) and the D2-D7 rows (`going-deep-plan.md:56-95`) contain no cell removal. Under append-only, ordinals are permanent, so rooms and queued orders indexed by ordinal are safe through D7. R3 names the gate that would break them.
- **X15 (point 5).** The only lines in the baseline that print the save version are `traffic.log:944-945`.
- **-scale (attack 8).** The world is 5 floors x 210 cells (`:3256-3257`), so the `IndexOf` maps total 1,050 entries. `FindCarvedCellAt` and `CellPos` go from O(i) spiral walks to O(1), a net gain. A spiral floor's bounding box is always under 8N, so the R2 threshold is never hit by today's floors.

## REQUIRED changes

1. **An uncarved ordinal has no lattice point. One battery line diffs, and the doc does not name it.**
   - `furnish.log:213`: `F2 an uncarved cell (Floor -1 cell 7 is not carved yet)`, from `RHSimCommandlet.cpp:3081` with 7 cells carved.
   - Under the doc, the test literal becomes `FloorCellKey(-1, 7)`, and ordinal 7 has no point. Section 1 says `IsFurnishKeyValid` "keeps the ordinal", but a key that is not in `IndexOf` has no ordinal to print.
   - The live path breaks the same way. Today `ResolveFurnishOrderKey` always succeeds below ground (`:9374-9376`), and the "not carved yet" reason comes later, from `IsFurnishKeyValid` (`:9461`). If the new resolve fails on an out-of-range ordinal, the player sees "No room there to furnish" (`:7110`) instead. The Sell Cell path (`:7177` -> `CanSellCell` `:9942`) and Assign (`:7067-7068`) build keys from raw `Cmd.Value` in the same way.
   - Fix: specify what `FloorCellKey(L, i)` does when i is outside `[0, Num)`. Put an ordinal-range check, with today's exact reason text, in every ordinal entry point before any key is built (Furnish, Sell Cell, Assign, `IsCellFreeForRoom` at `RHSimGrowth.cpp:410`). Route F2 through that path so line 213 stays the same.

2. **"Load refuses, leaving the live colony untouched" cannot be built on today's `LoadColony`.**
   - The live colony is torn down at `:11462-11480`, straight after the header check. Everything after that is read directly into members. The floor block sits at `:11586`, and the furniture, rated-floor and room data the cross-checks need come later still.
   - There is no refuse-mid-body precedent. The house pattern is "log an Error and keep" (`:11833-11837`, `:11924-11943`).
   - `rh_battery.sh:48` fails any run that contains a `LogRedHopeSim: Error` line, so K6's hand-made-file case, if it is logged as an Error, fails `deep.log`.
   - Fix (recommended): move the region and floor block to immediately after the header, before the teardown. Validate it there: duplicates, the shaft column, carved plan points, and (if it is kept) origin vs head. Refuse through `OutError` and `return false`, never with an Error line. Do the cross-checks that need later data (rooms longer than the floor, keys not in their floor, `RatedFloors`) the house way: repair or drop with a Warning. State which checks are which. K6 then asserts `return false` plus the untouched colony for the structural cases only.

3. **The adjacency fallback must use the room array's bound, and the doc's statement of that bound is wrong.**
   - `FloorRoomCells[L]` has the carve count as of the last designation (`SetNum(Carved)`, `:4641-4643`), not the length "up to the last designation". It can be shorter than `Cells`.
   - The grid only indexes `i < Rows.Num()`. `IndexOf` returns ordinals for every carved cell, so the R2 fallback must treat any ordinal `>= Rows.Num()` as `INDEX_NONE`. Without that, `Rows[E]` reads out of bounds, or the two paths disagree.
   - K8 must include a floor carved past its last designation.

4. **The growth ghost needs an N-cell peek, not `PeekNextCarveCell`.**
   - `RHGrowthVisualizerSubsystem.cpp:255-257` draws `SpiralCell(Carved + k)` for every k below `P.Cells`.
   - Fix: add `PeekCarveCells(Level, N, Out)`, which applies the plan-then-spiral rule without popping. `CompleteCivicRoom`'s offence check (`RHSimGrowth.cpp:1281-1283`) then has to equal the cell `ExcavateFloor(…, 1)` actually carves. Assert that in -deep with a plan pending.

5. **Presentation sites keep their own Z, so take the helper option out of section 1.**
   - Each site builds its own Z: `RHPlayerController.cpp:594` (`Z + 40`), `:823` (`Ground.Z`), `RHColonyVisualizerSubsystem.cpp:3483` (`L * FloorH - 15`). `GetFloorCellCentreCm` returns `Level * FloorHeightCm`.
   - Rule: at every presentation site, replace only `SpiralCell(i)` with `GetFloorCell(L, i)`, and keep `Head = GetShaftHeadCm()`. In D1 the head and the origin are bitwise equal, so this is exact.

6. **The growth project's new `FloorCell` field: drop it, or validate it.**
   - A floor CivicRoom's `Index` is an ordinal. The doc's own append-only argument (R3, R4) already makes ordinals stable, and the growth log lines print ordinals (`RHSimGrowth.cpp:1289`).
   - As written, the field is saved (`:11968`) and appears in no load check.
   - Recommended: keep `Index`, and add nothing.

7. **If `FRHCellKey` is kept: construct keys canonically, hash them by field, and spell out the commute translation.**
   - Equality compares every field, so a key built with a non-canonical unused field misses silently, and no compile error catches it. Example: a Module key with Cell = (-1, 0) at one site and (0, 0) at another (the commandlet builds `FIntVector(0, INDEX_NONE, HabA)` at `:10624` and `:10627`).
   - Fix: build keys only through factories (`FloorCellKey`, `ModuleKey`, `SlotKey`) with fixed unused fields (Slot Level = 0, Module Cell = 0). Allow no aggregate or brace construction.
   - `GetTypeHash` must hash field by field, never the struct's bytes: there are 3 padding bytes after `uint8 Kind`. The same goes for any digest.
   - Commute is not "No change". `RHSimCommute.cpp:228` must read `K.Level < 0 ? K.Level : K.OwnerId`. Underground Module keys carry the Level there, and that is why the ternary tests Level, not Kind.

## Optional changes

- **O1. Simpler path (recommended): keep `FIntVector` keys with ordinals, and drop `FRHCellKey` from D1.**
  - The doc already rests D2-D7 on "ordinals are permanent under append-only": rooms (R3), uplink orders (R4) and growth `Index`. Furniture, seats and planted keys keyed by ordinal are exactly as safe. Every position read still goes through `Floor.Cells[ordinal]`, so painted shapes work unchanged.
  - The mixed scheme is the worst of both:
    - Any future removal gate must re-key two schemes, not one.
    - Every furniture-to-room read turns lattice -> ordinal -> room: `GetCellRoom` `:9336`, through `IsLivingKey` and `IsFurnitureCellWorking`, 22 call sites. That is a new map lookup on the Hope path.
  - Dropping it removes required items 1 and 7, R5, R6, `LegacyTriple`, the save-sort reasoning and about 200 edits.
  - It departs from the D1 row's wording (`going-deep-plan.md:35`), so record it as a plan deviation for the director at the gate.
- **O2.** Leave the depth loops at `:2367`, `:3207` and `:4839` alone in D1. They cost O(MaxDepth) and are correct with count lookups. D5 changes them anyway. Dropping the change also drops the `RatedFloors` subset check.
- **O3.** Don't save `OriginCm` in D1. Derive region 0's origin from `ShaftHeadCm`: it is equal by construction, and both are written on the same two lines. Add the field in D2, when it can differ. That removes a load check.
- **O4.** Correct the reason given in section 4.2. `FMath::RoundToInt(double)` *is* `FloorToInt64(F + 0.5)` (`GenericPlatformMath.h:315-325`), so its rounding is the same as `LatticeOf`'s. The real reason not to unify them is the origin (head vs world 0).
- **O5.** Inventory items the doc misses. None of them changes behaviour, but each needs an edit:
  - `RHSimWorldSubsystem.cpp:3348` (the -scale `FurnishOrder` key);
  - `:5414` (`BayKeys`);
  - `:8695` and `:9980` (`F.Key.Z == Id` becomes `OwnerId`);
  - `RHColonyVisualizerSubsystem.cpp:2089` (`GetTypeHash(F.Key)` signature);
  - `:4550`; `RHCommandDeck.cpp:1490`, `:1521`, `:2034`;
  - `RHSimGrowth.cpp:716`, `:1228`;
  - `:5255` and `RHSimDistricts.cpp:690` (these already walk `RatedFloors`).
- **O6.** `diff_battery.py` counts removed and added lines separately, so the expected total is 4 differing lines (2 out, 2 in), not 2. Say so, so the gate check is literal.
- **O7. Tests.**
  - K1 is close to a tautology once `NextCarveCell` calls `SpiralCell`. Also compare the first 12 cells against hard-coded literals, (1,0), (1,1), (0,1), (-1,1), and so on.
  - Add a growth `CompleteFloorCells` and a `bCarve` civic room to K2's plan case.
- **O8.**
  - `IndexOf` should be "added to on append", not "rebuilt": a rebuild is O(n²) over an `ExcavateFloor` batch.
  - Inserting a floor into the sorted table moves the indices behind it, so no code may hold a floor index across a carve.
- **O9. D2 forward note.** "Never contains the shaft column" covers shaft 0 only. D2 must decide what happens when a new shaft snaps onto a cell that is already carved.

VERDICT: BLOCKED. Unblock when required changes 1-7 are written into a REVISION section of this doc. If O1 is taken, items 1 and 7 fall away, and the REVISION records the plan deviation for the director.

---

# REVISION (2026-10-21)

This revision answers the PROFESSOR REVIEW above. Where it disagrees with sections 1-9, the revision wins. Anchors are as read on main @ 9745c51.

## R0. Plan deviation, recorded for the director: O1 is taken

- **D1 keeps the existing `FIntVector` ordinal keys** for furniture, seats, stations and planted cells: `(Level, ordinal, 0)`, `(Level, INDEX_NONE, BuildingId)` and `(0, Bay, HabId)`. **`FRHCellKey` is dropped.**
- `LegacyTriple`, the key factories, the key-sort reasoning, R5 and R6 all go with it. Required items 1 and 7 fall away. Item 1 is re-checked below anyway.
- **What still changes.** Every *position* goes through the floor table (`Floor.Cells[ordinal]`). Painted shapes therefore work for rooms, seats, furniture and adjacency. Only the *keys* stay as ordinals.
- **This departs from `going-deep-plan.md:35`** ("Room, furniture and seat keys move from spiral index to lattice point"). Re-keying to lattice points is deferred to the first gate that can remove or reorder cells.
- **Why that is safe until then.** An ordinal is a position in an append-only list, so it is permanent. Nothing in D1 to D8 removes or reorders a carved cell:
  - Q2a says the Borer carves the painted shape *in order*, adding cells;
  - caverns (D6) and atriums (D7) are carved volumes, also added;
  - no gate back-fills or collapses a cell.
  - The same argument already carries rooms (R3), queued orders (R4) and growth `Index` (item 6). One scheme, not two.
- **The gate that must decide is D2.** The only planned event that could take a cell away is a new shaft sunk onto a lattice point that is already carved (review O9).
  - Recommendation for D2: refuse a shaft column on a carved cell. With that rule, no gate in step 2 removes a cell, and the re-key waits for the first gate that adds cell removal (back-fill or collapse, not scheduled).
  - If D2 chooses otherwise, D2 itself must re-key every ordinal store at once: furniture, seats, planted cells and crops, rooms, queued orders and growth projects.
- **Guard.** D1 adds no API that removes or reorders a cell. `FRHFloor::Cells` is private, written only by `AppendCarvedCell`. A -deep case (K9) asserts that every ordinal maps to the same point before and after save and load.

## Item 1, re-checked under O1: no key path changes

- **`furnish.log:213`**, `F2 an uncarved cell (Floor -1 cell 7 is not carved yet)`.
  - The test literal stays `FIntVector(-1, 7, 0)` (`RHSimCommandlet.cpp:3081`).
  - `CanPlaceFurniture` checks `IsFurnishKeyValid` at `RHSimWorldSubsystem.cpp:9569`, before any position read.
  - That check stays `Key.Y < GetFloorCarvedCells(Key.X)` (`:9457`). The reason text at `:9461` is untouched.
  - The line is identical.
- **Furnish order.** `ResolveFurnishOrderKey` (`:9361-9376`) still builds `(Cmd.Level, Cell, 0)` from `DecodeFurnish`, and still always succeeds below ground. The "not carved yet" reason still comes from `IsFurnishKeyValid`. "No room there to furnish" (`:7110`) stays bay-only. Unchanged.
- **Sell Cell** (`:7177`). It builds the same key. `CanSellCell` (`:9937`) first asks `GetCellFurniture`, a map lookup, so an uncarved ordinal gives "Nothing to sell here", as today. Unchanged.
- **Assign** (`:7067-7068`). `GetRoomAt(Level, ordinal)` reads `FloorRoomCells`, which is untouched. An out-of-range ordinal gives `NAME_None`, then "Not a work station …", then no key is stored. The log at `:7083` prints the same triple. Unchanged.
- **Position reads with an ordinal.** These are `GetCellCentreCm` `:9385`, `CellPos` `:2722`, and through them `BuildingMaskInCell` `:9517` and `GetFurnitureFootprintCm` `:9400`.
  - Every caller validates first: `CanPlaceFurniture` at `:9569`; `IsCellFreeForRoom` short-circuits on `Cell < GetFloorCarvedCells` at `RHSimGrowth.cpp:410`; stored furniture is checked at load (`:11833`); the -scale builder (`:3350`) uses carved cells.
  - Belt and braces: `GetFloorCell(L, i)` with i outside `[0, Num)` returns `SpiralCell(i)`. That is exactly today's value, so even an unforeseen caller reads what it reads now.
  - Each such read bumps a transient counter, `FloorCellFallbacks`. -deep asserts it is still 0 after its cases.
- **Result:** no battery line moves through a key path.

## Item 2. The floor block is read straight after the header

- **Save.** `SaveColony` writes the floor block immediately after `Ar << Magic << Version << SimSeconds << Speed` (`:11034`). It is deleted from `:11107`, which becomes `Ar << ShaftDepth << ShaftHeadCm << SpoilPileKg;`.
- **Layout:** `int32 FloorCount`, then per floor in table order:
  - `int32 RegionId`, `int32 Level`;
  - `int32 NCells`, then NCells x (`int32 X`, `int32 Y`);
  - `int32 NPlan`, then NPlan x (`int32 X`, `int32 Y`).
- **Origin (O3 taken).** No origin is saved. Region 0's origin is derived from `ShaftHeadCm`; they are equal by construction, and both are written on `:1346` / `:6336` only. The origin field and its check arrive in D2, when they can differ.
- **Load** reads the block into a local `TArray<FRHFloor> Loaded` immediately after the version check (`:11455-11460`), *before* the teardown at `:11462`. It validates there.
- **Structural refusals.** Each one sets `OutError` (e.g. `"Save floor table invalid: floor -2 lists cell (3,1) twice"`) and returns false. **No `UE_LOG` Error.** The live colony has not been touched yet. The refusals are:
  - `Ar.IsError()` after the block;
  - `FloorCount < 0` or above 64;
  - `Level >= 0` or `Level < -MaxDepth`;
  - a repeated (RegionId, Level);
  - `NCells < 1`, or a count larger than the bytes left;
  - a duplicate point in `Cells`;
  - (0,0) (the shaft column) in `Cells` or `Plan`;
  - a duplicate in `Plan`;
  - a `Plan` point already in `Cells`;
  - `RegionId != 0`.
- After the teardown, `Floors` is moved in at the old slot (`:11586`, which becomes `Ar << ShaftDepth << ShaftHeadCm << SpoilPileKg;`). `IndexOf` and the spiral cursor are derived, and `FloorCellsVersion` is bumped.
- **Cross-checks that need later data** follow the house "repair and keep" pattern, as **Warnings** (`UE_LOG(LogRedHopeSim, Warning, …)`). They never refuse and never log an Error:
  - **Rooms.** `FloorRoomCells[L]` longer than the floor's count, or present for a Level with no floor: truncate or drop, with a Warning. Checked right after `:11657`.
  - **Rated floors.** A `RatedFloors` Level with no floor: remove it, with a Warning. Checked after the rated set is read (`:11590` onward).
  - **Keys.** Furniture, planted and seat keys are not new under O1. Their existing load handling (`:11833-11837`) is unchanged, and no new check is added.
- **K6** asserts `return false`, an `OutError` containing "floor table", and an untouched colony (population, buildings and floor counts compared before and after). It covers only the structural cases:
  - a hand-made v41 file with the second cell overwritten by the first;
  - (0,0) in a plan;
  - a v40 file.
  - The test patches the bytes at the fixed offset 20: header (4+4+8+4), then `FloorCount`, then floor 0's `RegionId`, `Level` and `NCells`.
- **K6b** builds a valid file and patches one room array longer than its floor. The load succeeds, the array is truncated, and a Warning appears. The battery gate (`rh_battery.sh:48`) counts only `LogRedHopeSim: Error` lines, so `deep.log` stays green.

## Item 3. The adjacency fallback bound

- **Correction to section 2.** `FloorRoomCells[L].Num()` is the carved count *as of the last designation*: `SetNum(Carved)` at `:4641-4643`. It can be shorter than `Floor.Cells`, never longer, and load enforces this (item 2).
- **In the adjacency pass** (`:5258` onward), let `R = Cells.Num()` (the room array). Then:
  - `Pos[i] = Floor.Cells[i]` for `i < R` only; this replaces the inline walk at `:5295-5310`;
  - `Lo` and `Hi` come from those R points, as today.
- **Grid path** when `(Hi.X - Lo.X + 1) * (Hi.Y - Lo.Y + 1) <= 8 * R`. This is today's code exactly.
  - A spiral prefix's bounding box is at most `(k+1)^2` for `R >= k^2`, so it is always under `8R` for `R >= 1`. Today's floors never leave this path.
- **Fallback path** otherwise. `CellAt(P)` is `IndexOf.Find(P)`, mapped to `INDEX_NONE` when absent *or when the ordinal is `>= R`*. Then:
  - `Rows[E]` and `Offended[E]` are only ever indexed below R;
  - both paths see the same cell set: the first R cells.
  - The emitter loop stays `E = 0 .. R-1` in both.
- **K8** builds a sparse painted floor (an 8 x 1 line, a gap of 30 cells, then a 3 x 3 block). It designates, then carves 4 more cells past the last designation, so `R < Cells.Num()`. It also places a Hallway, emitter and refuser triple across the boundary. A Debug switch forces each path in turn. Offence counts and Hope must be exactly equal.

## Item 4. `PeekCarveCells(Level, N, TArray<FIntPoint>& Out)`

- **Rule.** Out gets the N points that N successive `NextCarveCell(Level)` calls would return, without popping:
  - first `Plan[0 .. min(N, Plan.Num()) - 1]` in order;
  - then spiral indices upward from the cursor, skipping any point already in `Cells`, in the Plan prefix just emitted, or earlier in Out.
  - `NextCarveCell` is defined as `PeekCarveCells(L, 1)` followed by the pop or cursor advance, so the two share one rule.
- **Users:**
  - `RHGrowthVisualizerSubsystem.cpp:255-257`: `SpiralCell(Carved + k)` becomes `Peek[k]` from `PeekCarveCells(Level, P.Cells)`.
  - `:268-269`: when `bCarve`, `PeekCarveCells(Level, 1)[0]`; otherwise `GetFloorCell(Level, P.Index)`.
  - `RHSimGrowth.cpp:1051-1053` and `:1275-1283`: `Pos[Carved]` becomes `PeekCarveCells(P.Level, 1)[0]`. `Pos[Cell]` at `:1287` becomes `GetFloorCell(P.Level, Cell)`, which is only reached for `Cell < Carved` (the `IsCellFreeForRoom` short-circuit).
  - `GatherFloorRoomGrid` `:427-429`: `Pos[i] = GetFloorCell(Level, i)`.
- **-deep K10.** Paint a 2-point plan, then:
  - assert `PeekCarveCells(L, 5)` equals the 5 cells that `ExcavateFloor(L, 5)` then appends;
  - run a growth `bCarve` CivicRoom (`CompleteCivicRoom`), and assert the cell its offence check examined equals `Cells.Last()` after its `ExcavateFloor(…, 1)`;
  - run a growth `CompleteFloorCells` on the same floor, and assert the visualizer's peek equals the appended cells.

## Item 5. Presentation: swap only `SpiralCell(i)`

- **The section 1 helper option is withdrawn.** At every presentation site, the edit is one token: `URHSimWorldSubsystem::SpiralCell(i)` (or the colony visualizer's wrapper) becomes `Sim->GetFloorCell(L, i)`.
  - `Head = GetShaftHeadCm()` stays.
  - Each site keeps its own Z and its own expression.
  - In D1 the head and region 0's origin are bitwise equal, so the result is exact.
- **Sites, with the Level variable used:**
  - `RHPlayerController.cpp:593` (`ActiveLevel`, Z+40 kept), `:822` (`ActiveLevel`, `Ground.Z` kept), `:1202` (selected cell's level), `:1976` (`Level`; the inclusive ±500 scan and first-hit order kept).
  - `RHColonyVisualizerSubsystem.cpp:3481` (`L`, `L * FloorH - 15` kept), `:4595` (`ViewLevel`). The wrapper `:4163-4167` is deleted, and its callers use `GetFloorCell`.
  - `RHCrewVisualizerSubsystem.cpp:840`, `:859` (`Level`, passed to `SeatOnRing`), `:1262` (`Level`).
  - `RHDistrictVisualizerSubsystem.cpp:307`, `:311`, `:329`.
  - `RHGrowthVisualizerSubsystem.cpp:257`, `:269` (item 4).
  - `RHCommandDeck.cpp:1101`.
  - `RHCheats.cpp:473`.
- **No other presentation line changes.** `GetFloorCellCentreCm` is used only by the sim and the commandlet:
  - `GetCellCentreCm` `:9386`, `Origin.X + P.X * 1000.0` in the same shape;
  - the test lambda `RHSimCommandlet.cpp:1888`.

## Item 6. The growth `FloorCell` field: dropped

- A floor CivicRoom keeps `Index` (an ordinal), as today. `FRHGrowthProject` and its save line (`:11968`) are unchanged, and the growth log lines (`RHSimGrowth.cpp:1244`, `:1289`) print the same ordinals.
- `FirstCell` stays a count snapshot.
- The same append-only argument as R0 covers it.

## Optional items adopted

- **O4, the LatticeOf reason.** Section 4.2's rounding argument was wrong. `FMath::RoundToInt(double)` is `FloorToInt64(F + 0.5)` (`GenericPlatformMath.h:315-325`), the same rounding as `LatticeOf` (`RHSimDistricts.cpp:50`). The real reason not to unify the two is the **origin**: `FindCarvedCellAt` measures from the shaft head (region 0's origin), while `LatticeOf` measures from world 0. The formula at `:5102-5103` is still kept verbatim.
- **O6, four diff lines.** `diff_battery.py` lists removed and added lines separately. The expected battery delta is exactly **4 lines**, all in `traffic.log`: the old `:944-945` out, and these two in:
  - `X15 the file says version 41 (expect 41)`
  - `X15 a v40 save is refused and the live colony untouched (Save format mismatch (magic 52485331 version 40; expected 52485331/41))`
  - The test edit is at `RHSimCommandlet.cpp:10794-10800`: `Ver == 41`, `V40 = 40`, slot `trafficv40`, `Contains("/41")`.
  - Any other diff line fails the gate.
- **O5, the missing inventory sites, under O1:**
  - `:3348` (-scale `FurnishOrder` key), `:5414` (`BayKeys`), `:8695` and `:9980` (`F.Key.Z == Id`), `RHColonyVisualizerSubsystem.cpp:2089` (`GetTypeHash(F.Key)`): no change, because the keys stay `FIntVector`.
  - `RHColonyVisualizerSubsystem.cpp:4550`, `RHCommandDeck.cpp:1490` / `:1521` / `:2034`, `RHSimGrowth.cpp:716` / `:1228`: counts, so no change.
  - `:5255` and `RHSimDistricts.cpp:690`: these already walk `RatedFloors`, so no change.
- **O7 (K1), hard-coded values.** K1 also asserts the first 12 carved cells on floor -1 against literals, not against `SpiralCell`. The literals are the walk's own sequence, checked against `:4590-4609`: (1,0) (1,1) (0,1) (-1,1) (-1,0) (-1,-1) (0,-1) (1,-1) (2,-1) (2,0) (2,1) (2,2).
- **O8, also taken.** `IndexOf` is *added to* on append, never rebuilt (rebuilt only at load). No code holds a floor-table index across a carve: lookups go through `FloorIndexByLevel` each time.

**Not taken.**
- **O2:** the depth loops still become "floors that exist", per the plan row. The `RatedFloors` check is now a Warning repair (item 2), not a refusal.
- **O9:** recorded in R0 as D2's decision.

## Updated save v41 and bar

- **v41 changes exactly one thing:** the floor table replaces `FloorCarvedCells`, and moves to directly after the header. No key, room, order or growth field changes.
- **Bar:**
  - the battery (now 43 runs with `-deep`) is green and line-identical except the 4 named X15 lines;
  - -scale stays within every existing budget;
  - -deep's K1-K10 and K6b pass.
