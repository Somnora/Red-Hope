# D7, atriums: design (2026-10-23, draft for adversarial review)

Step 2 (going deep), gate 7. Design only. No code is written.

**Anchors.** Code is read at HEAD `6b5653c` (D6b, save v49) with `git show HEAD:`. The working tree holds uncommitted D4 review fixes (`RHRobotStateTree.cpp`, `RHRobotTaskProcessor.cpp`, `RHSimTransit.cpp`, `RHSimTransitSuite.cpp`, `RHSimWorldSubsystem.h`); none of their lines is cited. D6c (deep commute, no bump), D6d (trams, v50) and D6e (transit power, v51) are designed and queued but not built (`orchestrator-handoff.md`, queue items 4-6). D7 builds after them, so:
- every line number below moves, and each anchor is also named by its function;
- what D7 changes in D6c's people walk (`FRHDeepWalk`) is written against the D6 MERGED DESIGN AND REVISION section 3, not code.

**Sources.** `going-deep-plan.md` (the D7 row, its risks, Q2a "brushes for tunnel, cavern and atrium"); `d6-caverns-design.md` (all of it; the DIRECTOR ANSWER: per-dig air is not in step 2, one air pool per floor through D8; deviation 2 "no vertical travel inside a cavern until D7"; the forward note "an atrium flags cells as open, never deletes them"); `d1-floors-design.md` (R0: O1 taken, ordinal `FIntVector` keys are safe only while cells are never removed or reordered); `d5-depth-design.md` (Shelter, Warmth, cooling); `d4-transit-design.md` (lifts, the deep net); `design-decisions.md` (the D-gate entries; Hope is never spent; Gate-D framing; the death-spiral guardrail).

**Goal.** A cavern of two or three floors can be turned into one tall open hall. The floor slabs inside the box are gone, so the hall's floor is the cavern's deepest floor and the rooms around it look into it. Homes and work seats that face the hall lift Hope a little, for light and space. The open volume costs more air to fill and leaks more.

**What you see.** You dig an 8 x 8 x 3 cavern, then press "Open as atrium" on its card. The two upper floors lose their slabs inside the box: from floor -2 you look down through a railing into the hall on -4. Homes on the ring around the void and on the hall floor glow softly in the overlay; the City panel says how many people face an atrium. The O2 tip shows the extra fill and leak.

---

## 0. Facts the design rests on (HEAD `6b5653c`)

- **Cells are append-only and ordinals are keys.** `FRHFloor::Cells` is private and written only by `AppendCarvedCell` (`RHSimWorldSubsystem.h:286-305`). Ordinals key rooms (`FloorRoomCells`), furniture, seats and stations (`(L, ordinal, 0)`, `(L, INDEX_NONE, BuildingId)`), planted cells, queued orders and growth `Index` (D1 R0, O1).
- **A cavern is a box of ordinary cells** on `Top..Bottom` (`FRHCavern`, `.h:398-415`), carved through `CarveLatticePoint` (`RHSimCavern.cpp:199`). Limits are code constants `RH::CavernMaxSide` 8, `RH::CavernMaxFloors` 3 (`.h:376-384`). `bFinished` is derived: every box point on every floor is carved (`.h:410`).
- **Air is per floor.** `StepHabitability` (`RHSimWorldSubsystem.cpp:2847-2950`): `RequiredKg = Cells x O2FillKgPerCell`; leak `Cells x O2LeakKgPerCellPerSol x dt/sol`; fill `min(O2FillRateKgPerHour x dt/50, Required - Fill)` with a powered circulator. Rating needs full fill, circulation and `Cells >= MinLivableCells`; it drops only when the fill is failing (`< 98%` and net-declining, `:2914`), so a growing requirement with a working circulator is "expansion, not a crisis". The same `Cells x O2FillKgPerCell` sits in `GetFloorO2RequiredKg` (`.h:1038`) and `Debug_ForceRateFloor` (`:3669`); the leak in `GetStandingO2LeakKgPerSol` (`:7150-7165`).
- **Hope** (`GetColonyHope`, `:6284-6776`) is a pure read. Views: `min(Raw x ComfortRefPop/Pop, HopeViewsMax)` where Raw sums `HopeViews` of exterior windows of rated surface habitats (`GetViewsHope`, `:11363-11378`). Shelter: `HopeShelterMax x DeepResidents / Pop`, the last addend (`:6743-6749`). The addition chain ends `... + Out.Views + Out.Solidarity + Out.Shelter` (`:6773`).
- **Beds.** `CountFloorBeds` (`:11785-11834`): `(int32)(Carved x CPC)` with no furniture, else `(int32)((Carved - BedCells) x CPC) + Sum`. `RehomeResidents` (`:7267`) displaces residents of an overfull home.
- **Home cells are derived for mapped floors only.** `DeriveDeepHomes` (`RHSimDistricts.cpp:179-255`) returns early with no deep district; it walks `Geo.DeepFloor` levels, gives bed-piece cells first, then legacy slots `floor((k+1)CPC) - floor(k CPC)` by ordinal. Its key is (`ColonistSetVersion`, `FurnitureVersion`, `ZoningVersion`, `FloorCellsVersion`, `DistrictsVersion`, `BedAssignVersion`) (`:257-275`). Readers: `GetColonistHomeCell` (gated on `IsFloorMapped`, `:303-312`), membership (`:366-380`, gated on `DeepFloor`), and the commute's district rows (`RHSimCommute.cpp:386`, **not** gated: it reads `HomeCell` for any resident not held by a whole-floor owner).
- **Cell consumers.** `IsCellFreeForRoom` (`RHSimGrowth.cpp:389-394`, all growth and civic picks), `DesignateRoom` (`:5730`), `IsFurnishKeyValid` (`:11576`), `CanPlaceBuildingSized`'s underground branch (`:8709-8720`, via `FindCarvedCellAt` `:6214`), the seat scan (rooms then modules), `IsCavernPointEligible` (`RHSimCavern.cpp:96-122`).
- **Robots walk cells.** `EnsureDeepNet` (`RHSimTransit.cpp:907`), keyed on (`FloorCellsVersion`, `ShaftsVersion`, `LiftVersion`, built roads, `TunnelsVersion`): a floor portal's landing is the lowest-ordinal carved orthogonal neighbour of the column; fields run over `BuildLatticeNeighbours` (`:650-690`: 4 orthogonals, a diagonal only when both orthogonals are carved). Dig labels (`EnsureDigInfo`, `:5317-5380`) are 4-connected components of carved cells.
- **People (D6c, to be built)** walk `FRHDeepWalk`: compartments are 4-components of carved non-chain cells, octile inside a compartment, doors and landings between (D6 REVISION section 3).
- **The view** shows one floor: a tile per carved cell, hidden unless its level is the view level (`RHColonyVisualizerSubsystem.cpp:3460-3532`, `ApplyViewLevel` `:4487`). `rh.Cutaway` (`:69`) only opens surface habitats. The cavern visualizer is a pure listener (`RHCavernVisualizerSubsystem.cpp:173`).
- **Verbs are `FName`s** dispatched in the uplink (`DigCavern` at `:9150`). `StepUplink`, `StepHabitability` and `StepHope` run at the same index in both spines (`:634-642`, `:806-814`).

---

## 1. The critical rule: open, never removed

**An atrium never removes, moves or reorders a cell.** Every cell inside the box keeps its place in `FRHFloor::Cells`, its ordinal and its lattice point for ever. "No floor between" is a **derived per-cell flag**, not a change to the floor table.

- **What is open.** For an opened atrium on cavern C, the cells at the box points of floors `C.Top .. C.Bottom + 1` (every floor of the box but the deepest) are **open**. The deepest floor's box cells are the **hall floor**: ordinary cells, under an open volume.
- **Where the flag lives.** Not in `FRHFloor` (its layout and the floor block do not change). A derived cache in the subsystem:
  ```
  struct FRHAtriumCells            // derived, never saved
  {
      bool bValid = false;
      uint32 Atriums = 0, Cells = 0;           // AtriumsVersion, FloorCellsVersion
      TMap<int32, TArray<uint8>> Flags;        // Level -> per ordinal: bit 0 Open, bit 1 Faces (section 4)
      TMap<int32, int32> OpenCount;            // Level -> open cells on it
  };
  ```
  Built from the atrium list and the floor table, levels in floor-table order, box points in (Y, X) row order. Lookups: `IsCellOpen(L, Ordinal)`, `IsPointOpen(L, P)` (through `FindOrdinal`), `GetFloorOpenCells(L)`.
- **Why derived.** The atrium record fixes the open set exactly (a finished box, its floors). One source of truth means the flag cannot disagree with the record after a load, and the save holds only cavern Ids.
- **Permanence.** A finished cavern's box points are all carved, cells are never removed, so the open set of an atrium never changes while it stands. `CloseAtrium` (section 2) clears the flag; the cells are still there, untouched, and become ordinary again.
- **Guard.** No new API removes a cell. `-atriums` AT2 asserts, around an open, a close, a save and a load: every ordinal of every floor maps to the same lattice point, `Cells.Num()` is unchanged, and `FloorCellFallbacks == 0`.

**What "open" means to each system** (each change gated on `Atriums.Num() > 0`, section 7):

| System | An open cell | Where |
|---|---|---|
| Rooms (zone), planted cells | refused: "That cell is open to the atrium below" | `DesignateRoom` `:5730`; planted cells follow rooms |
| Furniture | refused, same reason | `IsFurnishKeyValid` `:11576` |
| Buildings and modules below ground | refused if any footprint cell is open | `CanPlaceBuildingSized` `:8709` |
| Seats and stations | none can exist (no room, no module) | the seat scan needs a room or a module |
| Growth and civic | never picked | `IsCellFreeForRoom` gains `!IsCellOpen` |
| Legacy beds | 0 | `CountFloorBeds`; `DeriveDeepHomes` slots |
| Home cells | never a home | legacy slots skip open cells; no bed piece can stand there |
| Air | billed at the atrium premium, to its own floor's pool | section 5 |
| Size (`bBigEnough`), cooling (D5), Warmth | unchanged: an open cell is still pressurized, cooled volume | — |
| Dig labels (`EnsureDigInfo`) | unchanged: open cells keep their dig | volume, reach and serving shafts do not move |
| Robot walk (`FRHDeepNet`) | not a node; no step into, out of or diagonally past it; never a landing | section 6 |
| People walk (D6c `FRHDeepWalk`) | not in any compartment; never a door or landing | section 6 |
| Footholds for new digging | not a foothold: a tunnel end, a cavern seed and a D6b growth point need a carved **non-open** neighbour | `IsCavernPointEligible`, `CanDigTunnel`'s touch test, D6b's eligibility |
| District paint, counts | a shape may cover open cells; its Cells count them (volume), its Beds do not | D6b counts |

---

## 2. Data and orders

**How an atrium is ordered: convert a finished cavern.** New verb `OpenAtrium`, `Value` = cavern Id. D6a's carving, record and Borer branch are reused untouched, so D7 adds no carving path and no new kind of cell. A new "dig an atrium" order would duplicate D6a's preflight and reserve cells while they are dug; converting needs neither.

**Preflight** (`CanOpenAtrium(CavernId, OutReason, OutPreview)`), run when queued (the card's ghost uses it) and again at execute. Refusals, each with a reason:
1. no such cavern, or it is cancelled;
2. it is already an atrium;
3. it is not finished ("finish digging it first: N cells left");
4. it spans one floor ("an atrium needs a cavern at least two floors tall");
5. a box cell on an upper floor (`Top .. Bottom + 1`) is in use: a room, a furniture piece, a building footprint or module (`BuildingMaskInCell`, `FindModuleOnCell`), a station key, a tunnel chain cell (`IsTunnelChainCell`, so no tunnel or tram line is cut), or an open growth project's target (its `Index` cell or any of its `Points` on that floor). The reason names the floor and the first such cell in ordinal order;
6. beds: on some upper floor, `CountFloorBeds` with the box cells open would be below the floor's residents ("floor -2 would have 40 beds for 46 residents: add beds or move people first"). **An atrium never rehouses anyone;**
7. walking: on some upper floor, a dig that holds box points would be split (section 6, the no-split rule);
8. air: the Oxygen stock is below the premium fill, `PremiumKg` (section 5) ("store 4.8 t of oxygen first").

**`CloseAtrium`** (`Value` = cavern Id; refused if it is not an atrium) puts the slabs back: the flag clears, the premium falls away, nothing is refunded and no cell changes. It is the cancel of every D-verb, and it keeps a misplaced atrium from being a dead end. Close is always allowed: removing an open cell only adds beds, walk nodes and footholds.

**The record.** `FRHAtrium` (saved): `CavernId` only. The box, floors and region are the cavern's. `TArray<FRHAtrium> Atriums`, ascending by `CavernId`, at most one per cavern. `AtriumsVersion` (derived, not saved) goes up on every open, close and load.

**Save: the next free version** (v52 if D6d and D6e take v50 and v51 as designed).
- **Where.** A new atrium block straight after the last block written when D7 builds (D6e's power block, as designed), so no earlier offset moves. It holds the count, then each `CavernId`. The default colony writes 4 bytes (count 0), hidden by the `(N bytes` mask.
- **Why not a flag inside the cavern block.** D6a's cavern record is 40 bytes and `caverns.log` saves colonies with caverns. A field there would move every cavern record and risk a `caverns.log` line. A separate block leaves the cavern block byte-for-byte as D6a wrote it.
- **Read before teardown, into locals.** Structural refusals go through `OutError` ("Save atrium list invalid: …"), never an Error line: a count below 0 or over `RH::MaxCavernRecords`; Ids not strictly ascending; an Id naming no loaded cavern; a cancelled or unfinished cavern (checked against the loaded floor table, already in locals); a one-floor cavern.
- **Occupancy repair.** A hand-made file can put a room, furniture, a module or a station on an open cell. Those stores load after teardown, so this is not a structural refusal. After the full load, an atrium with an occupied open cell is **dropped** with a Warning ("atrium over cavern 3 closed at load: floor -2 cell 17 is zoned"). Dropping an atrium loses nothing: its cells become ordinary cells. This is D6a deviation 3's "repair, do not tear down" precedent.

---

## 3. Era parity (in one place)

- `OpenAtrium` and `CloseAtrium` execute at `StepUplink`, the same index in both spines.
- The air premium is a rate times dt (leak) and a capped rate times dt (fill), the same class as carving a cell (section 5). Nothing new integrates a per-step flag.
- Hope is a pure read of saved and derived state, shared code.
- The flag and facing caches are pure functions of saved state.
- No band-specific branch; no new era refusal. Era logistics already teleport past lifts (the C4/D2/D4 deviation); walking around a void changes nothing in era.

---

## 4. Hope: facing the atrium

**The facing test (lattice adjacency, cached).** A carved, **non-open** cell `(L, P)` faces atrium A when `L` is one of A's floors (`Bottom..Top`) and P is inside A's box or 4-adjacent to a box point. On an upper floor that is the ring of cells around the void (balconies); on the hall floor it is the hall itself and the ring around it. No diagonal, no reach across floors, no line of sight. Bit 1 of `FRHAtriumCells::Flags` holds it; it is rebuilt only when `AtriumsVersion` or `FloorCellsVersion` moves (a cell carved beside the ring starts to face at once).

**A head faces an atrium** when either:
- **home:** they live in a floor cell (`HomeHab == 0`, `HomeLevel < 0`) whose home cell faces; or
- **seat:** their station (`ColonistStations`) is a floor cell `(L, i, 0)` whose cell faces, or a floor module `(L, INDEX_NONE, BuildingId)` whose cell (`FindCarvedCellAt(L, B.LocationCm)`, D6b's seat-cell rule) faces.

Surface homes and surface seats never face.

**Home cells on atrium floors.** D6b derives home cells only for mapped floors. D7 extends `DeriveDeepHomes` to the floors of every opened atrium:
- the legacy-slot walk skips open cells (`Beds[i] = 0`, no slot, `k` does not advance), so the slots total exactly the new `CountFloorBeds`;
- **the home cells of a floor that is an atrium floor but not mapped go into a new map, `AtriumHomeCell`, never into `HomeCell`.** `HomeCell` keeps exactly D6b's contents. The commute's ungated reader (`RHSimCommute.cpp:386`) therefore sees nothing new, and a floor does not become "mapped" (D6c's switch) because it holds an atrium;
- `FRHDeepHomes`'s key gains `uint32 Atriums`: `AtriumsVersion` while any atrium exists, else 0 (D6 RC 6's pattern). With no atrium, the key, the early return and every byte of the derivation are D6b's.

**The count, cached by version.** `FacingHeads` (int32) is one pass over `Colonists` in array order, integers only. It is cached on (`AtriumsVersion`, `FloorCellsVersion`, `ColonistSetVersion`, `FurnitureVersion`, `ZoningVersion`, `DistrictsVersion`, `BedAssignVersion`, `JobsVersion`): every input of the home cells plus the stations. A move home bumps `ColonistSetVersion` (`:7352`), a new station bumps `JobsVersion`. `-RHVerifyCaches` re-derives it and logs an Error on a stale reuse, as `IsDeepHomesFresh` does.

**The term.** `FRHHopeBreakdown` gains `double Atrium` and `int32 FacingHeads`.
```
if (Pop > 0 && Atriums.Num() > 0)
    Out.FacingHeads = GetAtriumFacingHeads();
    Out.Atrium = HopeAtriumMax x FacingHeads / Pop;
Total = clamp( ... + Out.Views + Out.Solidarity + Out.Shelter + Out.Atrium - ..., 0, 100 )
```
Row `HopeAtriumMax` 3 (placeholder; Q-A). Appended as the **last addend**, after Shelter.

**Per head, counted once.** Each colonist counts at most once, however many atriums, floors or roles (home and seat) they face. An atrium three floors tall is one atrium: nothing is summed per floor. This is C2a's per-head rule (the Jobs and Shelter shape, a share of the colony), so one showpiece cannot lift a city of thousands, and the term is capped by construction (the share is at most 1). It is bonus-only: no head is ever penalised for not facing one (Hope is never spent; prevention framing).

**No double count with Views.** Views counts windows in exterior walls of rated **surface** habitats; the atrium counts heads by **underground** cells. No cell or window can feed both. They measure different things (a window to the sky, a hall underground) and each has its own cap. A surface resident with a window whose seat faces an atrium adds to both terms, once each; that is not a double count of one feature.

**With D5's Shelter.** They stack: a head homed below -5 beside an atrium earns Shelter (shielding) and Atrium (light and space), both bonus-only, each capped at its own row. Together they can add at most `HopeShelterMax + HopeAtriumMax` (3 + 3 = 6 at the placeholders). Q-A offers the alternative. D5's Warmth and cooling read floors and residents, not cells, and do not change.

**Rooms and the adjacency penalty** do not change: an open cell holds no room.

---

## 5. Air: who pays for the void

**Each floor's pool pays for the slice of the void at its own height.** The open cells are in their own floor's table, so they stay in their own floor's air, at a premium. No air is moved between floors, no new pool exists, and the director's "one pool per floor through D8" holds. Summed over the floors, the void is billed once.
- The hall floor's box cells are ordinary cells and pay the ordinary rate.
- A hall floor that is "the bottom of a tall room" with its own pool is a model simplification. It is logged with D6's floor-wide-air deviation (deviation 3 below).

**The rows** (placeholders, director review; CSV and DT synced, as every Hope and air row is):
- `AtriumFillPermille` 1500: an open cell needs 1.5 cells of air (the slab's volume and the light well);
- `AtriumLeakPermille` 2000: an open cell leaks twice as fast (a tall volume's stack effect).

**The formulas,** for floor L with `O_L` open cells (0 on every floor while no atrium exists):
- `RequiredKg = Cells x O2FillKgPerCell`, then **only if `O_L > 0`**: `+= O_L x O2FillKgPerCell x (AtriumFillPermille - 1000) / 1000`.
- `LeakedKg = min(FillKg, (Cells x O2LeakKgPerCellPerSol [+ O_L x O2LeakKgPerCellPerSol x (AtriumLeakPermille - 1000) / 1000 only if O_L > 0]) x dt / SolLength)`.
- One helper per quantity, `GetFloorO2RequiredKg(L)` (`.h:1038`) and `GetFloorO2LeakKgPerSol(L)` (new), used by `StepHabitability`, `Debug_ForceRateFloor`, `IsFloorSealedButSmall` and `GetStandingO2LeakKgPerSol`. When `O_L == 0` each helper evaluates **the exact expression it replaces** (the premium branch is skipped, not added as 0), so a floor with no open cell is bit-identical even in a colony with an atrium.

**Linear in dt.** The leak is a rate times dt. The fill is `min(rate x dt/50, Required - Fill)`, exactly the term that fills a newly carved cell. Opening an atrium raises `RequiredKg`, as carving does, and the existing rating hysteresis treats a working circulator's fill as expansion, not failure (`:2914`). So no rating flaps in either band while the premium fills, and the premium itself needs no up-front bill: the circulator draws it from the Oxygen stock, which refusal 8 guarantees is there.

**`PremiumKg`** (the preflight figure and the ghost's tip) is `sum over upper floors of O_L x O2FillKgPerCell x (AtriumFillPermille - 1000) / 1000`. For an 8 x 8 x 3 atrium: 128 open cells x 100 kg x 0.5 = **6.4 t**. The extra leak is `128 x O2LeakKgPerCellPerSol` a sol.

**`CloseAtrium`** lowers `RequiredKg`. The fill above the new requirement stays in the pool and leaks down at the normal rate, as on any floor whose requirement fell.

---

## 6. Walking, robots and the commute

**The no-split rule (preflight 7).** On each upper floor L of the box, consider every dig (an `EnsureDigInfo` label) that holds box points. After opening:
- its non-open cells must form **at most one** 4-connected component (none is allowed: a dig that is entirely void, such as a box beside a column on a floor with no other cells);
- if it keeps any non-open cell and had a landing (a carved orthogonal neighbour of a reaching column), it must keep a **non-open** landing.

So opening never makes a cell that was reachable unreachable, and dig labels stay a true statement about walking. Later carving only adds cells, and open cells never change while the atrium stands, so the rule keeps holding.

**Robots (`FRHDeepNet`).**
- `BuildLatticeNeighbours` treats an open cell as absent: no step into it or out of it, and it does not count as a carved orthogonal for a diagonal.
- A floor portal's landing is the lowest-ordinal **non-open** carved orthogonal neighbour of the column. A floor whose only neighbours are open has no portal on that shaft.
- `EnsureDeepNet`'s key gains `Atriums`: `AtriumsVersion` while any atrium exists, else 0. No rebuild is added in a colony without one, so `DeepNetBuilds` and the D4 perf digests cannot move.
- No building, furniture or station can stand on an open cell, so no robot site is ever there.
- Robots still change floors only by lift (D4). An atrium has no robot route down.

**People (D6c `FRHDeepWalk`, built before D7).**
- Compartments are the 4-components of carved cells that are neither chain points nor open. Doors and landings are never open cells.
- `FRHCommuteKey`'s `DeepWalk` composite gains `AtriumsVersion` while any floor is mapped and any atrium exists, else 0 (D6 RC 6).
- Homes and seats are never on open cells, so no trip is anchored in the void.
- **Inside a compartment the walk is still the octile line.** A ring around a void is a concave compartment, so a trip across the hall is measured straight through the air. This is D6's R2 (octile undercounts a concave compartment), extended; across an 8-cell void the undercount is at most about 80 m, under the 300 m long-trip line. Logged as a risk, not fixed in D7.
- **No stairs.** People change floors by shaft only, as in D6 (Q-B).

**The commute on unmapped floors** is unchanged: an atrium does not map a floor, and atrium home cells live in `AtriumHomeCell`, which no commute line reads.

---

## 7. Inertness

**The switch: an atrium exists** (`Atriums.Num() > 0`). Only an executed `OpenAtrium`, or a load of a file that holds one, can make it true. No suite, bot or playtest sends `OpenAtrium`. Every new branch tests the switch (or `O_L > 0`, which implies it) first:
- Hope: `Out.Atrium` stays `+0.0` and is the last addend, so `Total` is bit-identical (`x + 0.0 == x`).
- Air: the premium branches are skipped; the helpers evaluate today's expressions.
- Beds: `CountFloorBeds` subtracts open cells only when the switch is on.
- Home cells: `DeriveDeepHomes` adds no level, and the key's `Atriums` field is 0.
- The deep net, the people walk and the commute key: the new key fields are 0; the neighbour and landing rules test the switch first.
- Cell consumers (`DesignateRoom`, `IsFurnishKeyValid`, `CanPlaceBuildingSized`, `IsCellFreeForRoom`, foothold tests): `Atriums.Num() == 0 || !IsCellOpen(…)`, the D3 tunnel-guard pattern (`RHSimGrowth.cpp:394`).
- The flag and facing caches are never built.
- Save: a 4-byte block, hidden by the mask.

**The bar** (one commit):
- `rh_battery.sh` green;
- `diff_battery.py` against the previous gate's baseline (post-D6e) shows **only the save-version lines** (recounted literally at build time) plus the new `atriums.log`;
- the default and `districts=1` 110-sol playtests are byte-identical under the `(N bytes` mask;
- `-RHCommuteAudit` margins unchanged; the two pins hold (`power: gen 50 W load 20 W`, `deposit Regolith_A: 40645 kg`);
- `-scale`'s five-floor and twenty-floor digests unchanged before the new atrium block, which runs last.

---

## 8. Presentation (RedHope; const accessors only; a pure listener)

- **The void.** On the view of an upper atrium floor, the carve tiles of open cells are hidden: there is no slab. Through the box you see the floors below as they are: the tiles, buildings and furniture of every floor of the box below the view, down to the hall floor, at their real depth. (They are real actors at their real Z; `ApplyViewLevel` un-hides them when they lie inside the box of an atrium whose floors include the view level.) A darkening step per floor down keeps the depth readable.
- **The railing.** A thin outline around the void on each upper floor (the cavern visualizer's box outline, `RHCavernVisualizerSubsystem.cpp:173`, in a warm off-white).
- **Light.** One tall light per atrium at the top floor's ceiling, centred on the box, shining down to the hall floor. It is visible from every floor of the atrium (the per-floor light key takes the atrium's floors). Placeholder intensity, flagged for the art pass.
- **The cutaway.** With `rh.Cutaway` on and the view on an atrium floor, the rock above the box is cut away, so the full height reads from the default camera. D8's section view supersedes this; D7 does only this case.
- **Looking down.** From the hall floor the void is simply open above (top-down camera, nothing to draw). From the top floor you look down through the railing into the hall.
- **The card and brush.** A finished cavern of two or more floors shows "Open as atrium". The ghost tints the cells that would open, and the facing ring in soft gold. The tip quotes `PremiumKg`, the extra leak a sol, and "N homes and seats would face it". A refusal shows its reason. An atrium's card shows "Close the atrium".
- **The overlay and the City panel.** Facing homes and seats glow softly. The City panel gains "Atriums: N, facing M of P people". The crew visual never places a person on an open cell.
- **Copy.** All new strings are Gate-D placeholders, prevention-framed, in `docs/qa/2026-10-15/gate-d-wording.md` with a fresh row prefix (`L` is free at HEAD).
- **Redraw keys.** The visualizer rebuilds on `AtriumsVersion`, `FloorCellsVersion` and the view level only, never per frame.
- **Checks:** `rh_clicktest.sh` gains `atrium.open` and `atrium.close`; captures: the ghost, an atrium from its top floor looking down, the hall floor, and the City panel line.

---

## 9. Tests: the new fails-loudly `-atriums` suite

Added to `rh_battery.sh` as the next run. Every fixture keeps 60 colonists or fewer (ramp 0) unless a case says otherwise, and every fixture that fits a lift car pins the bank to 1e9 before each step (D6 RC 9).

- **AT0** The rows and the switch: with no atrium every new read returns its inert value (`Atrium == +0.0`, `O_L == 0`, the caches unbuilt, the key fields 0).
- **AT1** Every refusal by its reason: unknown, cancelled, already open, unfinished, one floor, each kind of occupancy in refusal 5 (room, furniture, module, station, tunnel chain cell, growth target), beds (refusal 6), a split (7, the ring cut in two by the box), a lost landing (7), oxygen (8). Then a box beside a column on a floor with no other cell is accepted (an all-void dig).
- **AT2** **The critical rule.** Open a 3 x 3 x 3 atrium: every ordinal on every floor maps to the same point, `Cells.Num()` per floor is unchanged, `FloorCellFallbacks == 0`; the same after close, after a save and a load, and after carving more cells.
- **AT3** The open set is literal: 18 open cells (two upper floors), the hall floor's 9 box cells not open; `IsCellOpen` and `IsPointOpen` agree.
- **AT4** Consumers skip open cells: zone, furnish, place a module, growth (`ShapeHomes` and civic inside a shape painted over the box), `CountFloorBeds` (hand integer at CPC 1 and 0.5, with and without furniture), `DeriveDeepHomes`' slot total equals `CountFloorBeds`; a queued Zone order on an open cell is refused at execute.
- **AT5** Facing, literally: on a 4 x 4 x 2 box beside a dig, the ring cells on the upper floor and the hall and its ring on the lower floor face; a cell two steps out does not; a cell carved beside the ring after the open faces at once (the cache key moves on `FloorCellsVersion`).
- **AT6** **The Hope term.** 40 residents, 10 facing by home, 4 by seat only, 3 by both: `FacingHeads == 17`, `Atrium == 3 x 17 / 40` exactly. A head facing two atriums counts once. Total equals a twin with no atrium plus `Atrium`, to the bit. Shelter on a -6 atrium stacks (DP8's numbers plus this). Views with a window in a surface hab is unchanged by the atrium.
- **AT7** **Air.** Hand numbers for `RequiredKg` and the leak on each atrium floor and the hall floor; a floor with `O_L == 0` in the same colony is bit-identical to a twin with no atrium. A working circulator fills the premium with no rating loss; an empty Oxygen stock refuses the order.
- **AT8** Walking. A robot ordered across an upper floor goes round the void (path never visits an open ordinal); a landing in the box is replaced by the lowest non-open neighbour; with D6c built, a commute across the ring uses the rim compartment and never anchors in the void.
- **AT9** Save: a round trip equals a twin that never saved (Hope, Required, FacingHeads, the flags); each structural refusal; the occupancy repair drops the atrium with a Warning and no Error line; the previous version's file is refused.
- **AT10** Era parity: an era and an agent twin open the same atrium at the same uplink; over 10 sols the floors' O2 fill agrees within one era step's fill (`O2FillRateKgPerHour x 60/50`), and Hope within 5%.
- **AT11** `-RHVerifyCaches` is clean; the facing count is rebuilt only when a key field moves (a counter, asserted over 1,000 steps with no change: 0 rebuilds).
- **AT12** Close: the cells become ordinary (zonable, beds back, walkable), Hope drops to the twin's, the premium falls away.

**Mutations,** each run by hand, each must fail the named case:
- M1 the bed count ignores open cells: AT4.
- M2 `IsCellFreeForRoom` ignores open cells: AT4 (growth zones a void).
- M3 a head counted once per role or per atrium: AT6.
- M4 `BuildLatticeNeighbours` ignores open cells: AT8.
- M5 the premium applied to every cell of an atrium floor, or to the hall floor: AT7.
- M6 the facing cache drops `FloorCellsVersion` from its key: AT5 and AT11.
- M7 atrium home cells written into `HomeCell` instead of `AtriumHomeCell`: a new AT4 assertion that `HomeCell` equals the no-atrium twin's.
- M8 the occupancy repair as a structural refusal: AT9 (the file must load).

**-scale** (outside the battery; the block runs last): the 20-floor colony, four 8 x 8 x 3 atriums (one per shaft), 5,000 colonists.
- cold open-and-facing flags at most 1 ms; cold `FacingHeads` at most 2 ms; a warm Hope read unchanged from the D6 budget;
- a cold deep net with atriums inside D4's 3 ms; with D6c, a cold people walk inside D6's 5 ms;
- 2,000 accepted preflights at most 100 ms.

---

## 10. Risks

- **R1. The octile walks through the void** (D6 R2 extended): up to about 80 m undercounted across an 8-cell atrium.
- **R2. Refusal 6 (beds) will often fire.** An 8 x 8 x 3 atrium removes 128 cells of legacy beds. The player must furnish or move people first; the reason says how many beds are short.
- **R3. Per-floor slices of one volume.** An upper floor can lose its rating while the hall keeps its own. Physically one air mass, modelled as slices (deviation 3).
- **R4. `AtriumHomeCell` beside `HomeCell`.** Two maps from one derivation. M7 and AT4 guard that `HomeCell` never changes.
- **R5. D7 depends on D6c, D6d and D6e being built first.** Section 6's people-walk changes, the save position and the version number assume them. If any is re-deferred, its items here fall away and the version is recounted.
- **R6. Draw cost of looking down.** Un-hiding up to two floors of the box adds actors to the view. Bounded by 8 x 8 x 2 tiles and their furniture per atrium; checked in the captures' frame time.
- **R7. Foothold change.** New tunnels and caverns cannot start from a void cell. A player who wants a tunnel off the ring must start it from a ring cell.

## Deviations to log in `docs/design-decisions.md`

1. An atrium is a separate saved list keyed by cavern Id, not a field in the cavern block (the plan said "a cavern flag; no new state").
2. "Facing" is a 4-adjacency test on the atrium's own floors, with no line of sight.
3. Each floor's pool pays for its slice of the void (one pool per floor holds); the atrium is not one air volume.
4. People and robots still change floors only by shaft (D6 deviation 2 continues), unless Q-B (b) is chosen.
5. A hand-made file with an occupied open cell loads with that atrium closed (a Warning repair), not a refusal.

## Director questions

**Q-A. How big is the atrium bonus, and does it stack with the deep-home bonus?**
- **a (recommended): up to +3 Hope, by the share of people whose home or seat faces an atrium, and it stacks with Shelter** (+3 for living below -5). Each person counts once. It is bonus-only, like windows. A deep atrium district can add up to +6 in all.
- b: up to +5, stacking with Shelter (up to +8).
- c: up to +3, but a deep resident gets the larger of Shelter and Atrium, not both.

**Q-B. D6 promised vertical travel "with D7". Do atriums carry stairs?**
- **a (recommended): no stairs in step 2.** People and robots change floors by shaft, as now. An atrium is a place to live beside and look into. D6c's walk stays one floor at a time and no commute moves.
- b: stairs for people inside every atrium: a walk edge from each upper floor's ring down to the hall floor, so the people walk spans floors. One to two more sessions, and every commute through an atrium's floors changes.

The air rows (`AtriumFillPermille` 1500, `AtriumLeakPermille` 2000) are placeholders marked "director review". An atrium has no other bill: no Struct, no power draw.


# PROFESSOR REVIEW (2026-10-23)

Read against HEAD 6b5653c (save v49) with `git show HEAD:`. The working tree was ignored. Nothing was built or run. The D6c, D6d and D6e items are judged against the D6 MERGED DESIGN AND REVISION, not code.

## What I verified and found sound

1. **No path removes or reorders a cell.**
   - `AppendCarvedCell` (RHSimWorldSubsystem.cpp:5179-5184) is the only writer. Its callers only append: `CarveLatticePoint` (RHSimCavern.cpp:199-210), `CarveNextCell` (:5259) and the tunnel completion (:8052).
   - A finished cavern cannot be cancelled (RHSimCavern.cpp:478-482).
   - A shaft column cannot land on a carved point (`CheckSinkColumn`, :7535-7541).
   - Tunnels run through rock only (`CanDigTunnel`, :7934-7938). So no later column or chain can enter an atrium box.
   - Deriving the open flag from the atrium list is the right structure.
2. **Hope inertness.** The chain at :6772-6774 is `... + Shelter - Adj ... - Warm`. Adding `+ Out.Atrium` (+0.0) after Shelter is exact. The partial sum can never be -0.0, because `HopeBase` is 50 (RH_Config.csv:51). D5's Shelter (:6743-6749) is the precedent.
3. **The air helpers can be bit-identical.**
   - The O2 rows are doubles (RHSimWorldSubsystem.h:3099-3101).
   - :2859 `Cells * O2FillKgPerCell` is the same expression as `GetFloorO2RequiredKg` (.h:1038).
   - :2864 evaluates `(Cells * Leak) * (SubDt / Sol)`. A helper that returns `Cells * Leak` and is then multiplied by `(SubDt / Sol)` gives the same result.
   - :7155 `(double)Cells * Leak` is identical too.
   - This holds only if the premium branch is skipped when O_L is 0, never added as 0.
4. **The AtriumHomeCell split is needed and sound.**
   - The commute's district rows read `ShapeHomes->HomeCell` (RHSimCommute.cpp:386) only when `bShapeTrip` (:371). A resident of an unmapped atrium floor can still reach :386 through a mapped work floor.
   - Keeping atrium-only floors out of `HomeCell` holds D6b's numbers, provided the early return (RHSimDistricts.cpp:190-193) and the resident filter (:200) are widened only while an atrium exists.
5. **The slot-skip matches the bed count.** The telescoping slot formula (RHSimDistricts.cpp:224) still sums to `(int32)(m x CPC)` when open cells are skipped and k does not advance. So it equals the new `CountFloorBeds`.
6. **The facing cache key covers home levels and stations.**
   - Every write to `HomeLevel` bumps `ColonistSetVersion`: :3929/:3932, :7338/:7352, RHSimCommute.cpp:824-825.
   - A station change bumps `JobsVersion` (:6099-6103).
7. **Planted cells follow rooms.** A planted cell without a Garden room is cleared at :4308-4315.
8. **Save.** Blocks are read into locals before teardown (:13784-13826; teardown starts at :13828). The new block follows that pattern.
9. **Era parity.** `StepUplink`, `StepHabitability` and `StepHope` run at the same index in both spines (:634/:639/:642 and :806/:811/:814).
10. **Q-B is a required question.** D6 deviation 2 and RC 11 promised vertical travel and open volumes "with D7".

## Defects

**D1. "Dig labels stay a true statement about walking" is false.**
- `CarveNextCell` (:5209-5261) carves the next plan point or spiral point with no touch test. So after an atrium opens, it can append a cell whose only carved neighbours are open.
- `EnsureDigInfo` (:5335-5362) puts that cell in the same dig as the atrium, but no walk reaches it.
- `IsRouteReachable` (RHSimTransit.cpp:584-588, :597-598) and the load repair (:1976-1977) then report it reachable.
- `Touching` (:5363-5378) also counts open cells, so `ServingShaft` can name a shaft that has no landing on that floor.
- The design's foothold list covers tunnels, caverns and D6b growth only. The spiral and plan carves have no foothold at all.

**D2. The design names one of three neighbour generators.**
- The others are `BuildLatticeField`'s inline path (RHSimTransit.cpp:718-724) and `LatticeAStar` (:785-795).
- `PlanDeepRoute` takes the direct same-dig leg through `LatticeAStar` (:1145-1151). Two ring cells on opposite sides share a dig, so a robot walks straight across the void.
- M4 mutates `BuildLatticeNeighbours` only, so it cannot catch this.

**D3. Robots already inside the box when the atrium opens are unhandled.**
- `Resolve` (RHSimTransit.cpp:1103-1112) accepts an open ordinal as a cell end. Its field distance is then infinite, so the robot has no route and is not counted as off-lattice (:1117).
- The load repair (:1973-1981) keeps a robot that stands on an open cell.
- A preflight refusal based on robot positions is impossible, because only the agent band moves robots (era has no `StepTaskBoard`, :806-822). The refusal would break era parity.

**D4. The air claim holds only under a per-floor ceiling, and the atrium lowers that ceiling.**
- The drop test (:2917) fires when `Fill < 0.98 x Required` and `TakenKg < LeakedKg`.
- A floor's fill is capped at `O2FillRateKgPerHour x dt/50` (:2872). That is 480 kg a sol (sol = 1200 s, RHSimClockSubsystem.h:26; rows at RH_Config.csv:41-43), whatever the number of circulators. The leak is 2 kg per cell per sol.
- So any floor whose leak-equivalent exceeds 240 cells loses its rating for good. At 2000 permille, an 8 x 8 slice adds 64 cell-equivalents, so an upper floor of 177 cells or more fails permanently. At ColonistsPerCell 1, a city of thousands has floors that size.
- An unrated floor gets 0 beds in `RehomeResidents` (:7310), and everyone on it is displaced. This breaks the death-spiral guardrail (design-decisions.md:172) and the Gate-D prevention framing.
- Separately, the multi-sol fill window can drop a rating whenever the Oxygen stock dips. Refusal 8 checks the stock once, at execute.
- Growth already avoids this window by billing O2 up front (RHSimGrowth.cpp:711, :1442).

**D5. The district membership cache goes stale on a mapped atrium floor.**
- `MakeDistrictMembershipKey` (RHSimDistricts.cpp:136-141) keys on `FloorCellsVersion` and `BedAssignVersion` only.
- Opening an atrium moves `HomeCell` (slots skip open cells), which `DeriveDistrictMembership` reads (:371-378). Neither key field moves.
- Result: stale counts, and the `-RHVerifyCaches` Error at :401.
- The commute key (RHSimCommute.cpp:66-70) has the same gap until D6c's `DeepWalk` field exists. Under R5, that may never happen.

**D6. The building refusal is in the wrong branch.**
- `CanPlaceBuildingSized:8709` is inside the station-module branch (comment at :8664).
- The general underground path (:8759-8765) never maps a building to a cell. So a circulator or any other non-module floor building can still be placed over the void after the open, including by a queued Build order.

**D7. Overlapping atriums are unspecified.**
- `CanDigCavern` skips already-carved points (RHSimCavern.cpp:356-359) and refuses only *uncarved* points of a live cavern (:367).
- So a new cavern can enclose a finished atrium's cells, on the same or adjacent floors, and then be opened.
- Unspecified for that case: union of open sets, one atrium's hall floor becoming another's void, PremiumKg double counting, and what Close does.

**D8. The M7 and AT4 assertion contradicts section 4.**
On a floor that is both mapped and an atrium floor, `HomeCell` must change, because the slots skip open cells. "HomeCell equals the no-atrium twin's" only holds on floors with no open cell.

**D9. Table gaps.**
- `GetCellBeds` (:11889-11904) returns CPC for any carved cell, open ones included.
- `CountFloorBeds` has two exits: the early return when there is no furniture (:11790) and the shared formula (:11833). The per-piece verify (:11834-11841) must agree with both.
- `CanDigTunnel`'s two-ended test `BesideCarved(B)` (:7972) is a foothold too.
- `IsDeepHomesFresh` (RHSimDistricts.cpp:277-301) does not compare the new map.

**D10. The Q2a deviation is not logged.** The director's Q2a answer promised atrium brushes that "make the big shapes one drag" (going-deep-plan.md:113). Converting a finished cavern is two steps.

**D11. Naming.**
- AT4 says `ShapeHomes`; at HEAD the kind is `ERHGrowthKind::DeepCells` (RHGrowth.h:20).
- AT6's "10 by home" must read "10 by home only", or 17 does not add up.

## Required changes

1. **Walk labels.**
   - While atriums exist, `EnsureDigInfo` labels 4-components of non-open cells only. Open cells get `DigOf = INDEX_NONE`.
   - Guard every `DigOf` reader for INDEX_NONE: `Touching` (:5374), :5407, :13043, RHSimTransit.cpp:584, :598, :978, :1111, :1977.
   - The dig cache key gains `Atriums` (AtriumsVersion, else 0), so `DigLabelRuns` cannot move in a colony without one.
   - Keep refusal 7 as player protection. Delete the "later carving keeps it" sentence.
   - New AT8 case: a plan point carved beside open cells only becomes its own dig with no landing, and `IsRouteReachable` is false.
2. **One open-aware step predicate** used by `BuildLatticeNeighbours` (650-684), the inline field (718-724), `LatticeAStar` (785-795) and the landing loop (959-966).
   - Split M4 into M4a (neighbours) and M4b (`LatticeAStar`).
   - AT8 must include a same-dig direct leg across the ring.
3. **Robots on open cells.**
   - `Resolve` maps a position on an open cell to the lowest-ordinal non-open 4-neighbour in the same walk component. If there is none, it is off-lattice and takes the existing surface-ring fallback.
   - The load repair (:1973-1987) treats an open ordinal as "not a cell".
   - No refusal is based on robot positions.
   - AT8: a robot parked on a box cell at the open reaches its next site.
4. **Air.**
   - (a) New refusal 9, per upper floor: `(Cells + O_L x (AtriumLeakPermille - 1000) / 1000) x O2LeakKgPerCellPerSol <= O2FillRateKgPerHour x SolLengthSimSeconds / 50`. The reason names the floor and the excess cells.
   - (b) Bill the premium up front at execute: take `Premium_L` from the Oxygen stock and add it to `FloorO2Kg[L]`, as growth does. Refusal 8 then tests that bill.
   - Rewrite section 5's "Linear in dt" claim to state the condition: Taken >= Leaked every step.
   - AT7: a 200-cell upper floor with an 8 x 8 slice is refused; a 170-cell floor is accepted and stays rated for 20 sols in both bands.
   - AT10's fill check becomes exact at the open.
5. **Cache keys.**
   - `FRHDistrictMembershipKey` gains `Atriums` (AtriumsVersion while a deep district *and* an atrium exist, else 0).
   - So does the D6b block of `MakeCommuteKey` if D6c is not built.
   - New AT case: a shape and an atrium on one floor; `-RHVerifyCaches` is clean after the open and after the close.
6. **Placement.** The open-cell refusal goes in the general `Level < 0` path of `CanPlaceBuildingSized`, tested on every footprint cell (`GetFootprintHalfCm` through `CmToLattice`), not only in the module branch.
   - AT4 adds: a circulator over the void is refused, and a queued Build order is refused at execute.
7. **No overlap.** New preflight refusal: "the box shares a point with the atrium over cavern N" (any floor, 3D). Add the same as a structural refusal at load.
8. **Section 4 and the tests.**
   - State that `HomeCell` moves on mapped atrium floors.
   - Restrict M7 and AT4's twin-equality to floors with no open cell.
   - Rename `ShapeHomes` to `DeepCells`.
   - Fix AT6's wording.
9. **Table additions.**
   - `GetCellBeds` returns 0 for an open cell.
   - `CountFloorBeds` subtracts open cells at both exits (:11790, :11833).
   - The `bTwoEnded` test (:7972) uses the non-open foothold.
   - `IsDeepHomesFresh` compares `AtriumHomeCell`.
   - The early return becomes "no deep floor and no atrium".
10. **Deviation 6.** "Q2a's atrium brush is delivered as convert-a-finished-cavern (two steps)."

## Optional changes

- **Cutaway.** The pit has no rock to cut. `ApplyCutaway` (RHColonyVisualizerSubsystem.cpp:4359-4400) drops `WallISM` pit faces ("The pit has no roof to lift", :67-68). Re-specify it as "drop the pit walls of the void on upper floors", or drop the item.
- **Presentation scope.** "Un-hide below" touches the building pass (4500-4516), the tile pass (4549-4556), the room, prop and furnishing passes, the robot layer (4542-4545) and crew wander (RHCrewVisualizerSubsystem.cpp:1262 picks any carved cell). List them all.
- **-scale.** `CountFloorBeds` walks every `FurnishedCells` entry per call (:11801-11809). I did not measure whether 2,000 preflights fit 100 ms at 5,000 colonists. Measure it before trusting the budget.
- **Director questions.** Q-A is mostly a row value. The Q2a brush is the decision the director actually ruled on, so consider swapping Q-A for it. Keep Q-B.

VERDICT: BLOCKED. The core design is sound (cells are flagged, never removed, and the flag is derived), but walk reachability (D1-D3), the air guardrail (D4) and a stale cache (D5) are wrong as written. Every fix is mechanical: it unblocks when required changes 1-10 are written into a REVISION section, and a check that each one is written is enough, with no new full review round.


# REVISION (2026-10-23)

This section is the D7 spec where it differs from the draft above; the implementer follows the draft as amended here. The required changes are tagged **[RC n]**; the optional changes taken are tagged **[O]**. The two spot-checked claims were re-read at HEAD before this was written: the per-floor fill ceiling (`O2FillRateKgPerHour` 20 x 1200 / 50 = 480 kg a sol against `O2LeakKgPerCellPerSol` 2, `RH_Config.csv:42-43`, so 240 cell-equivalents a floor) and the general underground branch of `CanPlaceBuildingSized` (`RHSimWorldSubsystem.cpp:8759-8765`), which maps no building to a cell.

## R1. Walk labels follow the open flag [RC 1]

- **The dig labels.** While `Atriums.Num() > 0`, `EnsureDigInfo` floods 4-neighbours over **non-open** cells only, seeded in ordinal order as today. An open cell gets `DigOf = INDEX_NONE`. With no atrium the labelling is today's, line for line.
- **The key.** `FRHDigInfo` gains `uint32 Atriums`, holding `AtriumsVersion` while an atrium exists, else 0. So `DigLabelRuns` cannot move in a colony without one.
- **`Touching`** (the 8 cells around a column) skips open cells, so `ServingShaft` never names a shaft that only touches the void on that floor.
- **Every `DigOf` reader treats `INDEX_NONE` as "not in any dig"** (unreachable, no landing, no shaft): `Touching` (`:5374`), `:5407`, `:13043`, `RHSimTransit.cpp:584`, `:598`, `:978`, `:1111`, `:1977`. The build's self-review greps `DigOf[` over `Source/RedHopeSim` and lists each site with its guard.
- **What stays true, and what does not.** Refusal 7 (no split) stays, as player protection at the moment of opening. **The draft's sentence "Later carving only adds cells … so the rule keeps holding" is deleted: it is false.** `CarveNextCell` (`:5209-5261`) carves plan and spiral points with no touch test, so a later cell can touch only open cells. Under R1 such a cell is its own dig with no landing. Robots cannot reach it (`IsRouteReachable` false), and people anchored there take D6c's off-net rule. Nothing is left mislabelled.
- **AT8 adds:** a plan point carved after the open, beside open cells only, becomes its own dig; it has no landing; `IsRouteReachable` to it is false.

## R2. One open-aware step predicate [RC 2]

- One helper, `IsWalkOrdinal(const FRHFloor&, int32 Ordinal)`: true when the ordinal is carved and, while an atrium exists, not open. It is the only test for "a robot or a person may stand here".
- It is used by **every** neighbour generator: `BuildLatticeNeighbours` (`RHSimTransit.cpp:650-684`, orthogonals and the "both orthogonals" diagonal rule), `BuildLatticeField`'s inline path (`:718-724`), `LatticeAStar` (`:785-795`, which `PlanDeepRoute` uses for the direct same-dig leg, `:1145-1151`), and the landing loop in `EnsureDeepNet` (`:959-966`).
- D6c's `FRHDeepWalk` compartments, doors and landings use the same helper.
- **Mutations:** M4a (`BuildLatticeNeighbours` ignores open cells) and M4b (`LatticeAStar` ignores open cells) each fail AT8.
- **AT8 adds:** a same-dig direct leg between two ring cells on opposite sides of the void; the path never visits an open ordinal and its cost equals the hand-counted walk around.

## R3. Robots standing on a cell that opens [RC 3]

- **`Resolve`** (`RHSimTransit.cpp:1103-1112`): a position whose cell is open maps to the lowest-ordinal non-open 4-neighbour in the same walk component (R1's label). If there is none, the position is off-lattice and takes the existing surface-ring fallback, and it is counted as off-lattice (`:1117`).
- **The load repair** (`:1973-1987`) treats an open ordinal as "not a cell", so a loaded robot on one is repaired like any robot off the lattice.
- **No refusal is based on robot positions.** Only the agent band moves robots (era has no `StepTaskBoard`, `:806-822`), so such a refusal would break era parity.
- **AT8 adds:** a robot parked on an upper-floor box cell when the atrium opens reaches its next site, from the neighbour `Resolve` picks.

## R4. Air: a ceiling check and an up-front bill [RC 4]

**The ceiling, refusal 9.** For each upper floor L of the box:
```
(Cells_L + O_L x (AtriumLeakPermille - 1000) / 1000) x O2LeakKgPerCellPerSol  <=  O2FillRateKgPerHour x SolLengthSimSeconds / 50
```
- The right side is the most one floor's circulation can push in a sol (480 kg at the rows); the left side is the floor's leak a sol with the premium.
- The reason names the floor and the excess, in cells: "floor -2 would leak more air than its circulation can replace: open 24 fewer cells or dig the atrium on a smaller floor".
- With the rows: 64 open cells at 2000 permille add 64 cell-equivalents. A 200-cell upper floor (264 > 240) is refused; a 170-cell one (234) is accepted.
- The test uses the floor's own `Cells` (open ones included), and the hall floor is not checked: it gets no premium.

**The bill, up front.** At execute, after every refusal passes:
- `PremiumKg` is billed through growth's path: `GrowthBillFits` (`RHSimGrowth.cpp:230`) with `{Oxygen: PremiumKg}`. So the bill never takes the breathing reserve (`GrowthReserveO2Sols` of everyone's draw) or the open quota's Oxygen (C5d). If it does not fit, that is **refusal 8**, with growth's reason text.
- It is taken with `TakeGrowthResource(Oxygen, PremiumKg)` (`:251`).
- Each upper floor gets its share at once: `FloorO2Kg[L] += O_L x O2FillKgPerCell x (AtriumFillPermille - 1000) / 1000`, as growth fills a new floor's volume at completion (`:1442`).
- So every atrium floor is exactly as full after the open as before it, against its new requirement. There is no fill window, and no rating can drop while a premium fills.

**Section 5's "Linear in dt" is replaced by this:**
- The leak is a rate times dt, and the fill is `min(rate x dt/50, Required - Fill)`. Both are linear in dt, the same class as carving a cell.
- A floor stays rated in steady state **only while `TakenKg >= LeakedKg` every step**. Refusal 9 guarantees that at the open, for a powered circulator and a stocked Oxygen pool.
- **Later carving on an atrium floor** moves the floor toward the ceiling 64 cells sooner than on an ordinary floor (a carve can push any floor past 240 cells at HEAD). The atrium card shows "air headroom: N cells" for each of its floors. The premium itself is never re-billed.
- `CloseAtrium` lowers the requirement; the extra fill stays in the pool and leaks down at the normal rate. Nothing is refunded.

**Tests.**
- **AT7 adds:** a 200-cell upper floor of an 8 x 8 x 2 atrium (64 open cells, 264 cell-equivalents against 240) is refused by refusal 9, and the reason says "24 fewer"; a 170-cell floor is accepted and stays rated for 20 sols in both bands; an Oxygen stock under the bill plus the breathing reserve is refused by refusal 8.
- **AT10 becomes exact at the open:** right after the open, each floor's `FloorO2Kg` equals its pre-open value plus its premium share, in both bands, to the bit. The 10-sol tolerance (one era step's fill) applies after that.

## R5. Cache keys that read home cells [RC 5]

- `FRHDistrictMembershipKey` (`RHSimDistricts.cpp:136-141`) gains `uint32 Atriums`: `AtriumsVersion` while any deep district **and** any atrium exist, else 0. Opening or closing an atrium on a mapped floor moves `HomeCell` (its slots skip open cells), so membership re-derives.
- If D6c is not built when D7 builds, the D6b block of `MakeCommuteKey` (`RHSimCommute.cpp:66-70`) gains the same field. If D6c is built, its `DeepWalk` composite carries `AtriumsVersion` (draft section 6).
- **New case AT13:** a Homes shape and an atrium on one floor. `-RHVerifyCaches` is clean after the open and after the close; the shape's resident count matches a fresh derivation each time.

## R6. Placement over the void: every building, every footprint cell [RC 6]

- The open-cell refusal moves out of the station-module branch into the **general `Level < 0` path** of `CanPlaceBuildingSized` (beside `:8759-8765`). It tests **every footprint cell**: the footprint's corners from `GetFootprintHalfCm`, mapped through `CmToLattice(Shafts[0].HeadCm, ·)`, with every lattice point in between. Reason: "That spot is open to the atrium below".
- **Refusal 5** tests the same footprint mapping for every existing floor building on the box's upper floors, not only modules and `BuildingMaskInCell`. A circulator standing on a box cell blocks the open, and the reason names it.
- **AT4 adds:** a circulator placed over the void is refused; a Build order queued before the open, for a spot over the void, is refused at execute with the same reason.

## R7. No overlapping atriums [RC 7]

- **New refusal 10:** "the box shares a point with the atrium over cavern N". It compares the two boxes in 3D (any shared floor and any shared lattice point). Different caverns can share carved cells (`CanDigCavern` skips carved points, `RHSimCavern.cpp:356-359`), so this is needed.
- The same test is a **structural refusal at load** ("Save atrium list invalid: atriums over caverns 2 and 5 overlap").
- So no cell is ever open under two atriums, no hall floor is another atrium's void, `PremiumKg` is never counted twice, and `CloseAtrium` affects exactly one box.
- **AT1 adds** refusal 10, and AT9 adds its load refusal.

## R8. Section 4 and the tests, corrected [RC 8]

- **Section 4 is amended:** on a floor that is both mapped (D6b) and an atrium floor, `HomeCell` **does** change when the atrium opens, because the legacy slots skip open cells. Only a floor that is an atrium floor and not mapped keeps its home cells out of `HomeCell` (in `AtriumHomeCell`).
- **M7 and AT4's twin check** ("`HomeCell` equals the no-atrium twin's") apply to floors with no open cell only. On a mapped atrium floor, AT4 instead checks `HomeCell` against a hand derivation that skips the open ordinals.
- **AT4** says `DeepCells` (`ERHGrowthKind::DeepCells`, `RHGrowth.h:20`), not `ShapeHomes`.
- **AT6 reads:** 40 residents; 10 face by home only, 4 by seat only, 3 by both: `FacingHeads == 17`, `Atrium == 3 x 17 / 40`.

## R9. The table, completed [RC 9]

Added to section 1's table, each behind the switch:
- **`GetCellBeds`** (`:11889-11904`) returns 0 for an open cell.
- **`CountFloorBeds`** subtracts the floor's open cells at **both** exits: the no-furniture early return (`:11790`, `(int32)((Carved - Open) x CPC)`) and the shared formula (`:11833`, `(int32)((Carved - Open - BedCells) x CPC) + Sum`). The per-piece verify (`:11834-11841`) calls the same function, so it agrees by construction; AT4 checks both exits at CPC 1 and 0.5.
- **`CanDigTunnel`'s two-ended test** `BesideCarved(B)` (`:7972`) uses the non-open foothold, like the A end.
- **`IsDeepHomesFresh`** (`RHSimDistricts.cpp:277-301`) compares `AtriumHomeCell` (and its bed list) as well as `HomeCell`.
- **`DeriveDeepHomes`' early return** becomes "no deep floor **and** no atrium"; its resident filter (`:200`) admits atrium floors only while an atrium exists.

## R10. Q2a, logged and asked [RC 10, O]

- **Deviation 6** is added to the list: "Q2a's atrium brush is delivered as convert-a-finished-cavern: dig the cavern with the Cavern brush (one drag), then Open as atrium on its card (a second step)."
- The director questions are revised (end of this section): the Q2a choice replaces the bonus-size question, which becomes a placeholder row marked "director review".

## R11. Presentation, re-specified [O]

- **No rock cutaway in D7.** The pit has no roof (`RHColonyVisualizerSubsystem.cpp:67-68`), so D7's "cutaway" is: on an upper atrium floor, the slice rig's pit wall faces (`WallISM`, `ApplyCutaway` `:4359`) around the void are dropped, so the railing is the edge. Cutting rock above the box is D8's section view.
- **"Look down" touches these passes, and each is listed for the builder:** the building pass (`ApplyViewLevel`, `:4500-4516`), the tile pass (`:4549-4556`), the room, prop and furnishing passes (`RefreshRoomVisuals`, `RefreshFurniture`), the robot layer (`Agents->SetViewLevel`, `:4542-4545`), and crew wander (`RHCrewVisualizerSubsystem.cpp:1262` picks any carved cell; it must skip open cells, so nobody is drawn standing in the air). The lamps under open cells (`LightByCell`) are hidden with their tiles.
- The click test still gains `atrium.open` and `atrium.close`.

## R12. -scale: measure the preflight [O]

`CountFloorBeds` walks every `FurnishedCells` entry per call (`:11801-11809`). The preflight calls it once per upper floor and caches nothing. The `-scale` block measures 2,000 accepted preflights at 5,000 colonists before the 100 ms budget is trusted. If it is over, the preflight computes each floor's bed count once per (`FurnitureVersion`, `ZoningVersion`, `AtriumsVersion`) and reuses it. A missed budget is a finding for the director, not a budget edit.

## The refusals, in full (for the builder)

1. no such cavern, or cancelled;
2. already an atrium;
3. not finished;
4. one floor tall;
5. an upper-floor box cell is in use: room, furniture, module, **any floor building's footprint [RC 6]**, station, tunnel chain cell, growth target;
6. beds: an upper floor would have fewer beds than residents;
7. walking: an upper floor's dig would split, or lose its last non-open landing;
8. air bill: `PremiumKg` does not fit growth's availability rule for Oxygen **[RC 4]**;
9. air ceiling: an upper floor would leak more than one floor's circulation can replace **[RC 4]**;
10. overlap with another atrium's box **[RC 7]**.

## Deviations (the list, revised)

1. An atrium is a separate saved list keyed by cavern Id, not a field in the cavern block.
2. "Facing" is a 4-adjacency test on the atrium's own floors, with no line of sight.
3. Each floor's pool pays for its slice of the void (one pool per floor holds); the slice's premium is billed up front.
4. People and robots still change floors only by shaft (unless Q-B (b)).
5. A hand-made file with an occupied open cell loads with that atrium closed (a Warning repair).
6. Q2a's atrium brush is convert-a-finished-cavern, two steps (unless Q-A (b)).
7. While atriums exist, dig labels exclude open cells, so a dig is a walkable component.

## Director questions (revised)

**Q-A. How does the player make an atrium?**
- **a (recommended): convert a finished cavern.** Dig the cavern with the Cavern brush (one drag), then press "Open as atrium" on its card. Two steps. D6a's carving is reused untouched, and nothing new is reserved while a cavern is dug.
- b: an Atrium brush. One drag orders a cavern marked as an atrium. Its upper-floor cells are held free while they are dug, and it opens by itself when finished. One step, as Q2a's answer promised, but it adds a "reserved" state to D6a's carving and to the zoning, furnishing and growth checks: about one more session.

**Q-B. D6 promised vertical travel "with D7". Do atriums carry stairs?**
- **a (recommended): no stairs in step 2.** People and robots change floors by shaft, as now. An atrium is a place to live beside and look into. D6c's walk stays one floor at a time and no commute moves.
- b: stairs for people inside every atrium: a walk edge from each upper floor's ring down to the hall floor, so the people walk spans floors. One to two more sessions, and every commute through an atrium's floors changes.

**Placeholders marked "director review"** (no question needed): `HopeAtriumMax` 3, stacking with Shelter (so a deep atrium resident can gain up to +6 in all); `AtriumFillPermille` 1500; `AtriumLeakPermille` 2000. An atrium has no Struct bill and no power draw.

**Status.** Every required change 1-10 is written in above (R1-R10), with optional items R11-R12. Per the review's verdict, a check that each item is written suffices; no new full review round is needed.
