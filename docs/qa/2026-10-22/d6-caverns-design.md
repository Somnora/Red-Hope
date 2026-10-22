# D6, caverns and underground districts: design (2026-10-22, draft for adversarial review)

Step 2 (going deep), gate 6. Design only. No code is written.

**Anchors.** Code is read at HEAD `4620a94` with `git show HEAD:` (D4 committed at `fe232c7`, save v46). D5 (depth, save v47) is being built in this tree now, so every `RHSimWorldSubsystem.cpp` anchor below moves when D5 lands. What D5 adds is taken from `d5-depth-design.md` (REVISION, re-review, DIRECTOR ANSWERs), not from the working tree. The builder re-anchors after D5.

**Sources:** `going-deep-plan.md` (the D6 and D7 rows, the D6 risks, Q2a "you paint the shapes the Borer carves"); D1-D5 designs with every REVISION and DIRECTOR ANSWER; `c5-districts-design.md` (districts, C5b-R project records); `c6-traffic-design.md` (R8 variant (a), the 60-160 ramp).

**Goal.** Big carved volumes, and districts that are shapes on a floor, so one floor holds homes and industry and the city grows into them. People on one floor walk real distances, through tunnels and on trams. Lifts and trams draw power.

**What you see.** You drag a cavern box, three floors tall, and the Borers hollow it out. You paint a Homes district over part of a floor and an Industry district over the rest. Growth fills the Homes shape with bunks. A home in dig A with a job in dig B is now a real walk, or a ride up and down; a tunnel or a tram line shortens it. A brownout slows the lifts.

---

## 0. Facts the design rests on (HEAD `4620a94`)

- **An underground district is a whole floor.** `FRHDistrict::Level < 0` means "all of that floor", and `Lattice` is surface-only (`RHDistricts.h:42-53`). The ground cache has `FloorOwner` (Level -> Id) beside `CellOwner` (`RHSimDistricts.cpp:55-74`, `.h:2503`). Load refuses a floor district with a lattice (`RHSimWorldSubsystem.cpp:14188`).
- **Membership reads the floor.** A floor building's district is `FloorOwner[B.Level]` (`RHSimDistricts.cpp:142-145`); a floor resident's is `FloorOwner[HomeLevel]` (`:172-175`, `:242-254`); a floor seat's is `FloorOwner[Key.X]` (`:302-305`).
- **Surface lattice is the world grid** (`LatticeOf`, `RHSimDistricts.cpp:46-51`). **Underground lattice is region 0's frame**, anchored on shaft 0's off-grid head (`CmToLattice`, `.h:665`). D1 R1: never compare the two.
- **A floor resident has no cell.** `FRHColonist` carries `HomeLevel` and `HomeHab` only (`.h:60-64`). But beds are already assigned to people: `BuildBedAssignments` (`:11984-12025`) gives each floor resident a bed piece (`ColonistBedPiece`, derived, `.h:2883`), sorted by cell ordinal then piece Id. With no furniture, beds are the legacy `Carved x ColonistsPerCell` (`:11621-11624`).
- **Air, rating and size are floor-wide** (`StepHabitability` `:2802-2900`; `IsFloorCirculated` `:2786-2800`; `bBigEnough` `:2846`). D2 logged this as a deviation and named D6 for per-dig reach.
- **Cells are append-only.** `AppendCarvedCell` (`:5134`) is the only writer; ordinals key rooms, furniture, seats and orders (D1 R0, `.h:286-305`). Digs are derived (`EnsureDigInfo` `:5272`).
- **The commute keys a floor end on `-floor`** (`RHSimCommute.cpp:335`, `:341`) and puts it at a shaft head (`:363`, `:369`). Same floor is 0 cm in both branches (`:244-247`). With transit, floor-to-floor trips split over reaching shafts (D4 item 5).
- **Growth on a floor is whole-floor.** `RunGrowthStartCheck` sends a floor district to `PlanFloorHomes` (`RHSimGrowth.cpp:616`, `:684-711`), which carves `GrowthCellsPerProject` cells wherever the floor's carve order goes next (`CompleteFloorCells` `:1201-1238`). Civic picks the first free cell on the floor (`:1027-1072`). `FirstCell` is informational (`RHGrowth.h:34-37`).
- **D4 has one transit switch,** `bHasTransit` (the first car), tested first in `StepTransit` (`RHSimTransit.cpp:166-168`). Trams were deferred here with people (D4 DIRECTOR ANSWER Q-B). Lift power was deferred twice and given to D6 (D4 R9, D5 O2).
- **D5 (in build):** `MaxDepth` 20; cooling for every carved cell below -5; Warmth driven by the "scraping" short set, not the per-step shed flag (D5 REVISION item 1); save v47 with a warmth block after the transit block.

## 1. Decisions on the seven deferred items

1. **Caverns: built (D6b).** A cavern is one order and one record: a lattice rectangle on each of 1..3 consecutive floors. Its cells are ordinary cells in each floor's table (D1). It is carved by Borers like a tunnel (D3). See section 3.
2. **Districts as shapes on a floor: built (D6a).** Growth inside them: D6b.
3. **Per-dig reach: not built. Re-deferred past step 2.** Reason: D6a-D6c make digs into places for people (districts, home cells, walking) without re-keying the nine Level-keyed stores D2 listed. Air, rating, size and reach stay floor-wide. Director question Q-A.
4. **Real commutes between digs: built (D6c),** on floors the player has "placed" (section 5).
5. **Lift power: built (D6e),** with tram power.
6. **Trams with people: built (D6d),** people only. Director question Q-C.
7. **The O1 re-key: not needed.** Section 2.

## 2. Item 7: does anything remove or reorder a cell?

No. Checked per mechanism:
- **A cavern dig** appends through `AppendCarvedCell`. Cells already carved inside the box are skipped, never re-added or moved.
- **A district repaint** edits `FRHDistrict::Lattice` only. It touches no floor table.
- **Home cells (D6a)** are derived from bed pieces, whose keys are ordinals. Nothing new is saved by ordinal.
- **Growth targets inside a shape (D6b)** save lattice points, not ordinals, so they would survive a future re-key.
- **Trams (D6d)** key on the tunnel Id and lattice stops, like D4's draft (O1 there).

So D1 R0's append-only argument holds through D6. **Forward note for D7:** an atrium must flag cells as open, never delete them, or D7 becomes the re-key gate.

## 3. Caverns (D6b)

**The order.** New verb `DigCavern`: `Level` = top floor Lt, `Value` = height H (1..`CavernMaxFloors`, 3), `Location`/`LocationB` = two corners, snapped by `CmToLattice(origin, ·)`. Preflight at queue and again at execute. Refusals, each with a reason:
1. some floor Lt..Lt-H+1 is reached by no shaft (`ShaftReaches`);
2. a side over `CavernMaxSide` (12 cells, 120 m);
3. a box point is a shaft column (O9);
4. a box point is an uncarved point of an open tunnel or another open cavern;
5. on some floor, no box point is 4-adjacent to a carved cell or to a column of a shaft that reaches it ("a cavern opens off a dig or a shaft on every floor");
6. more than `MaxOpenCaverns` (2) open.
- `DigTunnel` refusal 6 and `SinkShaft` refusal 8 gain "an uncarved point of an open cavern", as D3 did for tunnels.
- `CancelCavern` (`Value` = Id): as `CancelTunnel`.

**`FRHCavern`** (saved): `Id`, `RegionId` (0), `TopLevel`, `Height`, `Min`, `Max`, `bCancelled`. Derived, never saved: per floor, the uncarved count, keyed on `FloorCellsVersion` (the `FRHTunnel` pattern, `.h:341-356`).

**Carving.** The Borer loop gains a branch after the tunnel branch (D3 placed tunnels after bore and carve queue). It runs only when the earlier branches find nothing and a cavern is open.
- The Borer of shaft S (helpers count as shaft 0) takes the shallowest floor of the cavern that S reaches with an eligible point. Work item `(4, CavernId)`. `PendingBoreWork` keeps its `FIntPoint` format.
- **At completion** it appends the first eligible point: box points in row order (Y, then X) on that floor that are uncarved, not a column, and 4-adjacent to a carved cell or a reaching column. D3's invariant holds: every appended cell touches older rock that is open.
- Batches in flight per cavern never exceed its uncarved count. An empty completion drops spoil and adds no cell (D3 O5).
- **Recipe:** `CarveCell` (3 h, 1,200 kg spoil), times D5's `DigPermille(L)` below -5. Director question Q-B offers a cheaper bulk row.
- **Era:** Borer work runs in `StepProduction` in both bands. No era refusal (D3 item 5).

**How caverns relate to the per-floor tables.** A cavern of height 3 is three rectangles of ordinary cells in three `FRHFloor` tables. It adds no level, no table and no new key. Each floor's air, rating, cooling (D5) and size count include its cavern cells. Floors stay between the levels; removing them is D7's atrium.

**How caverns relate to tunnels.** A tunnel may start beside a cavern cell (a T-join, D3 refusal 3). A cavern may enclose already-carved cells and finished tunnel cells. Neither may contain the other's uncarved points.

**Inside a cavern, floors are joined by stairs** for people (D6c): a vertical walk edge between the same lattice point on adjacent floors of one cavern, both carved, at `CavernStairCmPerFloor` (1,500 cm, placeholder). Robots do not use stairs in D6 (they keep D4's lifts).

**The bill a player should expect.** A full 12 x 12 x 3 cavern is 432 cells: 518 t of spoil, 43.2 t of O2 fill to rate (`O2FillKgPerCell` 100, `RH_Config.csv:41`), and under D5 below -5 a cooling load of `cells x depth-steps` W. At 3 h a cell that is 54 Borer-sols. This is the size question in Q-B.

## 4. Underground districts as shapes (D6a) and growth inside them (D6b)

**Data: no new field.** A district with `Level < 0` and a non-empty `Lattice` is a **floor shape**. Its lattice is in region 0's underground frame (`CmToLattice`), never `LatticeOf`. The header comment says so, and every read goes through `UndergroundLattice(Level, P)` helpers so a frame mix-up cannot compile silently. An empty lattice is today's whole floor.

**Painting.** `PaintDistrict` with `Level < 0` and `Value == 1` paints a rectangle on that floor. `Value == 0` (every existing caller, test and saved order) is today's whole-floor order, untouched (`RHSimDistricts.cpp:334-359`).
- The surface rules carry: cap 32 x 32 a side, one district per rectangle, the same Kind joins, another Kind is refused.
- **A floor is either whole or shaped.** A shape on a whole-owned floor is refused ("erase it first"), and so is a whole-floor paint on a floor with shapes.
- Instead of grid coverage: the floor must be reached, and the rectangle must contain or touch (4-adjacent) a carved cell or a reaching shaft column ("paint where you can dig"). Rock may be painted: that is how a player zones a cavern before it is dug.

**Ground cache.** `DeriveDistrictGeo` (`:55-74`) adds `FloorCellOwner: TMap<FIntVector(Level, X, Y), Id>` for shapes. Whole floors keep `FloorOwner`. `-RHVerifyCaches` compares the new map too.

**Membership.**
- A floor building: `FloorOwner[L]` if the floor is whole; else the owner of `CmToLattice(origin, B.LocationCm)` on L.
- A floor seat: `(L, ordinal, 0)` -> the owner of that cell's point; a module key -> its building.
- **A floor resident: the owner of their home cell.** `GetFloorHomeCell(ColonistId)` is derived:
  - with a bed piece (`ColonistBedPiece`), the piece's cell ordinal (its key's Y);
  - otherwise the legacy fill: the floor's residents without a piece, in Id order, take the legacy beds of the cells that are not Living-with-bed-pieces, in ordinal order, `ColonistsPerCell` each (`CountFloorBeds`' rule, `:11618-11660`);
  - none left (an overfull floor): `INDEX_NONE`, counted as the floor's Unzoned.
  - It is computed lazily, only for floors that have a shape, and cached on (`ColonistSetVersion`, `FurnitureVersion`, `ZoningVersion`, `FloorCellsVersion`).
- **Counts** (`GetDistrictCounts`): a shape's Cells are its carved points; Beds are the beds of the home cells inside it on a rated floor; Seats come through the cell owner.
- The commute's district rows (`RHSimCommute.cpp:363-370`) use the same lookups, so the flow matrix and the overlay show shapes. Distances do not change in D6a.

**Growth inside a shape or a cavern (D6b).** `RunGrowthStartCheck` (`RHSimGrowth.cpp:616`) sends a shape to two new planners:
- **Homes, `ShapeHomes` (new kind 3).** Only on a rated floor. Take up to `GrowthCellsPerProject` free carved cells inside the shape (`IsCellFreeForRoom`, ascending ordinal). If fewer are free, add uncarved shape points that touch the dig (row order), to be carved at completion. Zone Living and furnish bunks, as `CompleteFloorCells` does.
- **Civic.** `PlanCivic`'s floor branch (`:1027-1072`) restricted to cells inside the shape. Its `bCarve` case carves a shape point, not the floor's next spiral cell.
- **The record saves lattice points** (`TArray<FIntPoint> Points`) instead of relying on "the end of the floor". At completion each point is re-checked (still free, or still uncarved and still touching the dig) and carved by a new targeted `CarveFloorPoints(L, Points)` that calls `AppendCarvedCell` in order. A point that fails is refunded and skipped, with a Warning, as `CompleteFloorCells` does for zoning (`:1222-1226`).
- Whole-floor districts keep `PlanFloorHomes` and `ExcavateFloor` exactly.
- A shaped Homes district over a cavern is "a zoned cavern": growth fills the cavern's carved cells first, then extends into painted rock.

## 5. Real commutes between digs (D6c)

**The switch: a placed floor.** Floor L is *placed* when it holds a floor shape (D6a) or a tram line (D6d). Both are player acts. On a placed floor every resident and seat is measured at its cell. On any other floor, every line of today's commute runs: the `-floor` key, the head anchor, D2's cheapest shaft, D4's split.

**The people net, `FRHPeopleNet`** (derived, never saved, wait-free like D4 item 2(d)):
- nodes: the carved cells of placed floors, each shaft's surface head, and each shaft's landing on each placed floor (D3: the lowest-ordinal carved orthogonal neighbour of the column);
- floor edges: 4-neighbours 1,000 cm, diagonals 1,414 cm when both orthogonals are carved (D3 section 1);
- cavern stairs (section 3);
- tram edges (D6d);
- ride edges between a shaft's landings and its head, `|dL| x PerFloor(S)` (D4 rows), with `WaitCm(S)` added at query time, never baked in.
- Keyed on (`FloorCellsVersion`, `ShaftsVersion`, `LiftVersion`, `TunnelsVersion`, `CavernsVersion`, `TramVersion`, `DistrictsVersion`). Distance fields per landing and per distinct work cell, memoized like C6's per-anchor Dijkstra.

**The trip.** For a placed end, the anchor is `(L, cell ordinal)`. The trip key becomes the pair of anchors, interned in colonist Id order (C6's pattern).
- **Same placed floor, same dig or joined by tunnel:** the floor walk.
- **Same floor, unjoined digs:** up one shaft, across the surface, down another: `walk + |L| x Per(S1) + Wait(S1) + Surface(head1, head2) + |L| x Per(S2) + Wait(S2) + walk`.
- **Placed floor to surface, or to another floor:** walk to a landing, ride, then the other end's rule.
- **Route choice is wait-free** (D4's no-oscillation rule): pick the cheapest route without waits, ties to the lower shaft Id, then add the waits of the chosen shafts and lines. `ShaftPeople` gains 1 per colonist per shaft used; `TramPeople` likewise.
- **A cell with no landing in its component** (a dig touched only diagonally, D3 R6) is anchored at its `ServingShaft` head with 0 walk, as today, and counted in `CommuteOffNet`. The City panel names it.
- **Effects** go through the existing terms only: `bLong`, the ramped Hope penalty, the R2 work hooks. No new Hope term.

**Why this matters to play.** A 12-cell cavern is under 170 m corner to corner, under the 300 m long line. Long trips come from unjoined digs and deep surface jobs. Tunnels, trams and same-floor districts are the fixes, as the plan wanted.

## 6. Trams with people (D6d)

- **Verb `LayTramLine`, `Value` = tunnel Id.** Refused: no lift car (keeps D4's one switch, `RHSimTransit.cpp:168`); tunnel unknown, cancelled or unfinished; chain under `TramMinCells` (12); a line already on it; over `MaxTramLines` (8, cap 32); short of Struct (`TramStructKgPerCell` 20 x length, through D4's `TakeStructForBill`, which charges the ledger for floor parts).
- **`FRHTramLine`** (saved): `Id`, `RegionId`, `Level`, `TunnelId`, `StopA`, `StopB`, `WaitSec`. Laying it makes its floor placed.
- **The edge.** Between the stop cells: `CommuteWaitCmPerSec x (TramHeadwaySec / 2 + ceil(LenCm / TramSpeedCmPerS))`, wait-free; `WaitSec x CommuteWaitCmPerSec` is added at query time. Rows: `TramHeadwaySec` 60, `TramSpeedMps` 10.
- **Capacity.** At the `StepTransit` roll, after the shafts, in ascending line Id: `Load = 2 x TramPeople[line] x RampPermille / 1000`, `Cap = TramPlacesPerSol` (600), and D4's wait formula. People are world-side, so no lag and no `PrevWaitSec`.
- **No car is simulated.** The view draws one capsule per line by phase (D4 draft section 8).
- **Robots do not ride trams** (Q-C a).

## 7. Lift and tram power (D6e)

- **The draw.** Each fitted shaft appends one load entry: `LiftCars x LiftDrawWPerCar` (40 W). Each tram line: `LenCells x TramDrawWPerCell` (2 W). They go into `StepPower`'s list after D5's cooling entries, priority `TransitLoadPriority` 8, ties: buildings, then cooling, then lifts by shaft Id, then trams by Id.
- **The consequence uses D5's scraping short set,** never the per-step shed flag (D5's blocker: with the Lander's 50 W the flag alternates). Each shaft and line accumulates whole milliseconds short and served over the open sol.
- **At the roll:** `Cap_S = HoistPlacesPerSol + LiftCars x LiftPlacesPerSol x ServedPermille / 1000`, and a line's cap likewise. A fully short sol leaves a shaft at hoist capacity. Nothing else changes: no rating, no arrivals, no stranding.
- **The accumulators** reset at the roll and are saved (the open sol's share must survive a save).
- **R1 of D5 repeats:** more `LoadW` makes `RequestChargeWh`'s free-energy rule fail sooner (`GenW > LoadW`).

## 8. The split into sub-gates

Each sub-gate is one commit with a battery-green bar: every suite line-identical to the previous baseline except the save-version lines (recounted literally at build time against the post-D5 baseline), the default and `districts=1` 110-sol playtests byte-identical under the `(N bytes` mask, and `-RHCommuteAudit` margins unchanged. Every new branch tests the sub-gate's switch first.

| Gate | Scope | The switch | Save | New suite |
|---|---|---|---|---|
| **D6a** | floor shapes, membership, derived home cells, counts, overlay | `PaintDistrict` with `Value 1` on a floor | **v48** (fence) | `-ugdistricts` |
| **D6b** | `DigCavern`, Borer branch, `FRHCavern`; growth inside shapes | a `DigCavern` order; growth only on shapes | **v49** | `-caverns` |
| **D6c** | people net, placed floors, cell anchors, stairs | a placed floor | **none** (no state) | `-digcommute` |
| **D6d** | `LayTramLine`, tram edge, tram load and wait | `LayTramLine` | **v50** | `-trams` |
| **D6e** | lift and tram draw, served share, capacity scaling | the first car | **v51** | `-liftpower` |

**Order.** D6a first: shapes are what D6b grows into and what places a floor in D6c. D6b next: it is Borer work on a pattern D3 proved. D6c is the riskiest and needs both. D6d needs D6c's net. D6e is independent and last, so it can be re-deferred without touching the others.

**D6a inertness.** No suite or bot paints a shape (`-districts`, `-citygrowth` and the `districts=1` playtest paint whole floors, `RHSimDistricts.cpp:334-359`). With no shape: `FloorCellOwner` is empty; every membership branch takes the `FloorOwner` line it takes today; home cells are never computed; the commute's district rows read the same maps. Save: no new bytes. v48 is a fence, as C6a's v40 was: a v47 binary would refuse a shaped district at `:14188` anyway, but the fence makes the refusal a version error. **Era:** membership is derived state; no rate moves. **Tests (`-ugdistricts`):** UD1 paint, join, refusals (whole vs shaped, frame, reach, rock allowed); UD2 the frame: a shape at a known lattice point under an off-grid head maps buildings exactly, and `LatticeOf` would not; UD3 home cells: bed pieces, legacy fill, overfull floor, a departure re-derives; UD4 counts and seats per shape; UD5 a whole-floor district gives byte-equal counts with and without the new code path forced; UD6 save round trip, the v47 refusal; UD7 `-RHVerifyCaches` clean.

**D6b inertness.** The Borer branch runs after bore, carve queue and tunnels, and only with an open cavern. Growth's new planners run only for shapes (none without D6a's switch). `PlanFloorHomes`, `ExcavateFloor` and the civic floor branch are untouched for whole floors, so `-citygrowth` holds. Save: a cavern block after D5's warmth block (`NextCavernId`, count, records; the default writes 8 bytes), and growth projects gain `Points` (an empty array for every old kind). **Era:** carving is `StepProduction` work and growth records run in both bands (C5b-R); a TN7-style case pins equal carve order in both bands. **Tests (`-caverns`):** CV1 refusals 1-6 and the new tunnel and shaft refusals; CV2 row-order carving, the touch invariant after every append, spoil per cell; CV3 two Borers, two floors, an empty completion; CV4 D5's depth multiplier on a -6 cavern; CV5 cancel; CV6 save mid-batch equals a twin; CV7 `ShapeHomes` fills free cells, then carves shape points, and the ghost equals the carve; CV8 civic inside the shape only; CV9 a refunded point; CV10 era parity.

**D6c inertness.** No placed floor means `GetCommuteDistanceCm` and `DeriveCommuteFlows` run today's lines and `FRHCommuteKey` gains a `Placed` field that stays 0. `-percapita` and `-arrivals` (5,000 and 1,500 floor colonists, ramp open) place no floor, so their trips keep the `-floor` key; X13/X14 rebuild counts cannot move. No save change, so no bump: nothing a v49 binary reads differs (the plan's "every gate bumps" is met by D6 as a whole; logged). **Era:** the commute is a pure function of shared state (C6); home cells are derived from saved state. **Tests (`-digcommute`):** DC1 hand-integer walks in one dig, across a tunnel, between unjoined digs via two shafts; DC2 cavern stairs; DC3 wait-free choice with a crowded shaft (no flip next sol); DC4 `ShaftPeople` per shaft used; DC5 an off-net cell; DC6 whole floors unchanged in the same colony; DC7 ramp 0 is zero; DC8 era equality at the threshold; -scale: 5,000 placed residents, cold net at most 10 ms, warm reads at most 1 ms.

**D6d inertness.** No suite lays a line. `StepTransit`'s tram loop is empty; the people net has no tram edge. Save: a tram block after the cavern block (count 0: 8 bytes with `NextTramId`). **Era:** world-side, same in both spines. **Tests (`-trams`):** TM1 refusals; TM2 the edge cost; TM3 riders counted by route, load ramped (60/61/160, as D4 TR6); TM4 wait applied after choice, no oscillation; TM5 laying a line places its floor; TM6 save.

**D6e inertness.** With no car nothing is appended (`bHasTransit` false), so every suite but `-transit` is untouched. **`-transit` fits cars**, and a draw moves `LoadW`, which can move its charge and dock lines (TN14, TR14c). The recommended fix: `-transit` sets `Debug_SetTransitPowerScale(0)` at its start, so it keeps pinning D4's rules, and `-liftpower` pins the draw. The alternative is to accept the moved `transit.log` lines and name each one in the commit; the reviewer picks. The `lifts=1` playtest is outside the byte-identical set. Save: a power block after the tram block (per shaft and line: short and served ms; default 4 bytes). **Era:** `StepPower` runs in both spines; the share is integrated as D5's Warmth is, with D5 DP9's window. **Tests (`-liftpower`):** LP1 entries and order; LP2 a real-grid night short (D5 DP6's fixture) gives `ServedPermille` 0 and hoist capacity; LP3 a scraping (ii) step counts as short; LP4 era parity within one era step; LP5 save mid-sol.

## 9. Era parity in one place

Nothing in D6 is band-specific. Caverns and growth run in `StepProduction` and `StepCityGrowth`; commute and trams are world-side functions of saved state, read at the threshold both spines run; power runs in `StepPower`. Era logistics still teleport past lifts, stairs and trams (the C4/D2/D4 deviation, extended). No new era refusal.

## 10. Risks I see

- **R1. Two lattice frames.** Floor shapes use the head frame; surface districts use the world grid. A missed conversion compiles. Mitigation: typed helpers and UD2.
- **R2. Home cells reshuffle.** A derived home cell moves when an earlier-Id neighbour leaves. Commutes can change for people who did nothing. It is deterministic; a saved home cell would need rehoming logic at six sites, so it is rejected for D6.
- **R3. Floor-wide air under shapes.** A Homes shape in dig B breathes through dig A's filter (D2's deviation, kept by Q-A). The City panel must not suggest each shape has its own air.
- **R4. Cavern cost scale.** 432 cells is 54 Borer-sols, 518 t of spoil and 43 t of O2. Without Q-B's answer caverns may read as unaffordable.
- **R5. D6c's blast radius.** It is the first change to commute anchors since C6. The `Placed` key field and the "no placed floor" fast path are the only guarantee; -scale and `-RHCommuteAudit` must run.
- **R6. People and robots use different nets.** Robots plan on D4's `FRHDeepNet` (no stairs, no trams). The crew visual and the traffic overlay can disagree about a person's route and a robot's. Logged.
- **R7. D6e and `-transit`.** The debug scale keeps the suite identical but means no suite runs D4's rules with power on. `-liftpower` LP2 and the `lifts=1` playtest cover the combination.
- **R8. Anchors move twice.** D5 lands first and then every sub-gate moves lines in `RHSimWorldSubsystem.cpp`. Each sub-gate re-anchors and recounts its version lines.
- **R9. D7 dependency.** If D7 removes cells for atriums, the O1 re-key lands there, across five D6 stores (shape points are safe; home cells and growth records keyed by ordinal are not).

## Deviations to log in `docs/design-decisions.md`

1. Per-dig reach is re-deferred past step 2 (Q-A); air, rating and reach stay floor-wide.
2. People use cavern stairs and trams; robots do not (Q-C).
3. D6c adds no save state and does not bump the version.
4. Era teleports past stairs and trams too.

## Director questions

**Q-A. Should each separate dig have its own air and rating?**
- **a (recommended): not in step 2.** A floor keeps one air supply and one Livable rating. Districts, home cells and real walks make digs into places for people. It saves two to three sessions and a save re-key of nine stores.
- b: yes, as a sixth sub-gate after D6e. Each dig gets its own air, rating, size minimum and habitat link, and digs merge their air when a tunnel joins them. Every saved floor store is re-keyed, and the battery is at risk.

**Q-B. How big and how costly is a cavern?**
- **a (recommended): up to 12 x 12 cells and 3 floors, with a bulk `CarveCavern` row at 1.5 h a cell** (same spoil). A full cavern is 27 Borer-sols; two Borers halve it. D5's depth multiplier applies.
- b: the same size at the normal 3 h a cell: 54 Borer-sols.
- c: smaller caverns (8 x 8, 2 floors) at 3 h a cell: 16 Borer-sols.

**Q-C. Who rides trams?**
- **a (recommended): people only.** A tram is a fast, capacity-limited link in the commute. Robots keep walking tunnels and riding lifts. It is world-side and deterministic, with no robot risk.
- b: people and robots. D4's draft tram design for robots (holds, rides, reserve pricing at `MaxWaitSec`) is built too. One to two more sessions, in the riskiest code in step 2.
