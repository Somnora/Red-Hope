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


# PROFESSOR REVIEW (2026-10-22)

Both drafts were read against HEAD `4620a94` (`git show`), D5's working tree only where D6 branches around it (marked (wt)), and `d5-depth-design.md` with its REVISION and DIRECTOR ANSWERs.

## The base

**Draft B (`d6-caverns-design-draft-b.md`) is the base.** It has the better data model (a separate `Deep` field, so no existing `Lattice` reader can see an underground point), a better commute algorithm, a correct D6e capacity formula, and a verified argument that `-transit` stays identical. Draft A contributes four grafts, and in two places A is right where B is wrong.

## B's three claims against A, checked

1. **The `(4, Id)` work item: TRUE, and it is worse than B says.** At HEAD three sites read any `X >= 2` as a tunnel: completion `RHSimWorldSubsystem.cpp:1524`, the start log `:1619`, and **`GetTunnelBatchesInFlight` `:7661`**. D5 adds a fourth: (wt) `:1624-1625` prices any `X >= 2` at `FindTunnel(Y)->Level`, or at level 0 (1000 permille) when no tunnel has that Id. A's item also names no floor. **B's fix misses `:7661`**: a cavern record `(100 + d, CavernId)` still counts as a batch in flight on the tunnel whose Id equals `CavernId`. That throttles that tunnel's `FindTunnelWork` (`:7747`), blocks `CancelTunnel` (`:7768`), and keeps a cancelled tunnel "live" in `FindLiveTunnelOnPoint` (`:7692`). The `-tunnels` TN cases call it with Id 1, and the first cavern is also Id 1.
2. **Refusal 5 needs a seed on every floor: TRUE as described, but it is not a defect, and B's fix is.** It is overstated, because one shaft column beside the box satisfies every floor at once (a column runs through all the floors it reaches). B's rule ("P is carved on L+1 in the same cavern") grows lower floors that can touch no dig and no column. B itself models no vertical travel in a cavern (B section 3). So those floors become new digs with no landing: robots cannot reach them (D4's deep net), and their residents fall to B's off-net rule, a head anchor with **0 walk**, which is a free commute. **Keep A's refusal 5.**
3. **One Dijkstra per work cell: TRUE in substance.** A's `FRHPeopleNet` joins every placed floor, the stairs and the rides into one graph, and memoises a field per distinct work cell. At 5,000 residents that is hundreds of sources, each over thousands of nodes. I did not measure it, so this is an estimate. But millions of heap relaxations cannot meet A's own `-scale` budget of 10 ms cold. B's compartment, octile and door scheme costs one Dijkstra per door, over doors and chain points only.

## Defects found in B (it is not clean either)

- **B1. B's home-cell slot walk contradicts `BuildBedAssignments`.** At `:11984-12027`, bed pieces go to the **lowest-Id residents first**, in the order cell ordinal then piece Id, before anyone gets a legacy bed. B interleaves legacy slots and piece slots in carve order. So if cell 0 is an unfurnished legacy cell and cell 3 is furnished, B homes resident 1 in cell 0 while `ColonistBedPiece` gives resident 1 a piece in cell 3. B's own assertion ("a furnished resident's slot cell is their piece's cell") fails. **A's rule is the correct one:** a piece's cell first, then the residents without a piece take legacy cells in ordinal order.
- **B2. The load order breaks B's headline case.** The district loop at `:14177-14197` tests `IsFloor()`, which B redefines as `Level < 0 && Deep.IsEmpty()`, and refuses a second district on a level already held (`FloorsHeld`) with an **Error line**. B attaches `Deep` only after the district list loads (B section 4, Save v49). So a Homes shape and an Industry shape on one floor both look like whole floors at `:14188`. That gives an Error, which fails DD9 and the battery's error gate.
- **B3. A cavern completion leaves its point in `Plan`.** `AppendCarvedCell` (`:5134-5139`) does not touch `Plan`. The tunnel path removes the point by hand (`:7962`, "a carved point in the plan is a refused save"). B removes it in `CarveDeepPoint` but says "`Plan` is untouched" for caverns. A removes it in neither path.

## Defects found in A, beyond B's three

- **A1. A's D6e capacity formula is not inert.** `HoistPlacesPerSol + LiftCars x LiftPlacesPerSol x Served / 1000` gives 1,200 at full power for one car. D4's `GetShaftCapacityPlaces` (`RHSimTransit.cpp:68-76`) gives 1,000, and TR4 pins 1,000 and 4,000. With A's formula `-transit` fails whatever the power scale. B's `Hoist + (Cars x Lift - Hoist) x Served / 1000` is exact at 1000 permille.
- **A2. Reusing `Lattice` for floor shapes exposes about a dozen existing readers.** They are `RHSimDistricts.cpp:64-69` (which would put underground points into the **surface** `CellOwner`), `:271`, `:539-559`, the load at `:14186-14197`, and the presentation module at `RHDistrictVisualizerSubsystem.cpp:223-269`. A's "typed helpers" do not reach existing code. B's separate field makes those readers inert by construction.

## Defects in both

- **C1. D5's scraping rule (ii) only runs when there is cooling.** (wt) `:8385`: `bScraping = bShedStep || (CoolingEntries > 0 && ...)`; `:8387`: the walk runs only `if (bShedStep || (bScraping && CoolingEntries > 0))`. A colony with lift cars and no floor below -5 (every lift colony today) therefore sees only the alternating per-step shed flag. That is exactly D5's blocker: a dark night reads about 50% served, not 0. B's TP2 reuses DP6's fixture, which has a 200 W cooling floor on -10 (RHSimDepthSuite.cpp DP6), so it passes and hides this.
- **C2. The `ShortMask` write and the comparator.** (wt) `:8420` sets `ShortMask |= 1u << -CoolLevel` for **any** `!E.B`, so a lift entry (CoolLevel 0) sets bit 0. The comparator (wt) `:8400-8402` treats any `B == nullptr` as cooling. B names the comparator; neither draft names the mask.
- **C3. Later D6 suites will move when later sub-gates land.** Once D6c lands, `-deepdistricts`' mapped floors start measuring at cells. Ramp 0 zeroes the Hope effect but not the distances, long counts or flows any DD line prints. Once D6e lands, `-trams` (a line needs a car) and any `-deepcommute` case with a car move `LoadW`. Neither draft pins these.
- **C4. The membership cache key** (`MakeDistrictMembershipKey`, `RHSimDistricts.cpp:107-117`) has no `FloorCellsVersion`. Legacy-fill home cells move when a cell is carved, so a shaped floor's resident counts go stale.

## Checks that pass

- **Inert switches.** No `PaintDistrict` caller sets `Value` (a `double`, default 0). The commandlet's `Value` writes at `:8246`/`:8953` are for Erase and Rename. The default and `districts=1` playtests never dig a cavern or set `Value`.
- **`-transit` under D6e.** Every world step pins the bank to 1e9: `RHSimTransitSuite.cpp:76`, `:82`, `:463`, `:529`; `:435` follows a pinned step. `RequestChargeWh` draws from the bank before the `GenW > LoadW` test (`:12673-12679`). So, with B's formula and C1's fix, `transit.log` stays identical. No debug scale is needed. Reject A's `Debug_SetTransitPowerScale`: it would leave no suite running D4's rules with power on.
- **`depth.log` under D6e.** Only DP12 fits a car (wt `RHSimDepthSuite.cpp:870`). It is the last case and no step follows it, so nothing moves today. D5 is still in build, so re-grep at D6e.
- **Cars elsewhere.** `-scale` fits 16 cars (`RHSimCommandlet.cpp:16235`) but is not in the battery. `lifts=1` is outside the byte-identical set.
- **Step order.** `StepTransit` runs before `StepPower` in both spines (wt `:635`/`:650`, `:807`/`:822`). So B's tagged 2-slot ring is read complete and with no lag.
- **The O1 re-key.** I confirmed both drafts' conclusion that nothing removes or reorders a cell. Caverns and growth append. Repaints touch district points. Home cells, walk networks and trams are derived or keyed on Ids and lattice points. `Plan` removal (B3) is not a cell reorder.
- **Era parity.** Every involved step runs at the same index in both spines. Nothing is band-specific.

## The merged design

- **Order (B's):** D6a caverns (v48), D6b deep districts with growth (v49), D6c deep commute, D6d trams, D6e transit power.
- **D6a and D6b bundles.** D6a is self-contained Borer work on D3's pattern. D6b must bundle shapes with the growth dispatch: otherwise `RunGrowthStartCheck` (`RHSimGrowth.cpp:616`) sends a non-`IsFloor()` deep district to `PlanSurfaceHomes`.
- **D6c takes no bump.** A is right: no state is added and nothing derived is saved. Log it as a deviation, and save the hand-play checkpoints one remake.
- **Grafts from A:**
  - (i) refusal 5, one seed per floor, and no vertical eligibility;
  - (ii) the home-cell rule: pieces first, then legacy fill in ordinal order, then `INDEX_NONE`;
  - (iii) no D6c bump;
  - (iv) the forward note to D7 (atriums flag cells open, never delete).
- **Taken from B over A:**
  - the `Deep` field and the `IsFloor()` call-site list;
  - the fixed cavern `ShaftId`;
  - stairs deferred to D7 (people and robots stay on one network);
  - the compartment, octile and door walk;
  - the capacity formula;
  - the tagged ring;
  - the D5 growth multiplier on `DeepCells` (D5 DP3);
  - the bank-pin argument;
  - the `-scale` cavern block.

## Verdict on the merged design

BLOCKED. The structure is right, but as written the merge would fail the battery in four places:
- **D6e under A's formula:** TR4 pins one-car capacity at 1,000 and A's formula gives 1,200, so `-transit` fails (A1).
- **D6b under B's model:** two shapes on one floor raise an Error line at load (B2).
- **D6a under either encoding:** `:7661` counts cavern batches against the tunnel of the same Id (claim 1).
- **D6e on a colony with no cooling floor:** the served share reads from the alternating shed flag (C1).

All four fixes are mechanical. Unblock when required changes 1-12 are written into a REVISION section of this doc. No further full review round is needed; a check that each item is written suffices.

## Required changes

1. **One predicate for tunnel work.** Add `IsTunnelWork(W) := W.X == 2 || W.X == 3` and `IsCavernWork(W) := W.X >= 100`, and use them at every reader: HEAD `:1524`, `:1619`, `:7661`, and D5's (wt) `:1624-1625` and `:1645`. Keep B's `X = 100 + depth` encoding. CV6's mutation must cover `:7661` too: a tunnel 1 and a cavern 1 run together, and `GetTunnelBatchesInFlight(1)` counts only the tunnel's batches.
2. **Keep A's refusal 5.** Every floor of the box needs a box point 4-adjacent to a carved cell on that floor, or to a reaching shaft's column. Drop B's vertical eligibility. Eligibility is horizontal only, shared with D6b's growth.
3. **One carve helper.** `CarveLatticePoint(L, P)` refuses a column or a duplicate, then does `Floor.Plan.Remove(P)` and then `AppendCarvedCell`. Both the cavern completion and `DeepCells`/civic growth use it. CV2 and DD6 assert that no carved point stays in `Plan`, through a save round trip.
4. **Use A's home-cell rule, not B's interleave.** A resident with a `ColonistBedPiece` lives in that piece's cell. The remaining residents, in Id order, take legacy slots of the non-bed-piece cells in ordinal order. Use B's floor-difference count, `floor((k+1)·CPC) - floor(k·CPC)`, so the total equals `CountFloorBeds` at any `ColonistsPerCell`. Put DD3's literal case in the suite: an unfurnished cell 0 before a furnished cell 3.
5. **Attach `Deep` before the district checks at load.** Read the deep block into locals before teardown (as B says). Inside the district loop at `:14177`, give each `D` its `Deep` before the `IsFloor()`/`FloorsHeld` test. Extend the test: a level held whole may not also hold deep points, and no deep point may sit in two districts. Each is a structural `OutError`, never an Error line. DD9 adds a two-shapes-one-floor round trip.
6. **Gate the cache keys on deep data.** `FRHDistrictMembershipKey` gains a field: `FloorCellsVersion` while any deep district exists, else 0. `FRHCommuteKey`'s `DeepWalk` field is a composite of `FloorCellsVersion`, `TunnelsVersion`, `ShaftsVersion`, `DistrictsVersion` and (D6d) `TramVersion` while any floor is mapped, else 0. State in the doc that both are 0 with no deep district, so the C5 counts and the X13/X14 rebuild counts cannot move.
7. **Fix D6e's scraping set and its tests.** Widen D5's two gates, (wt) `:8385` and `:8387`, from `CoolingEntries > 0` to `CoolingEntries + TransitEntries > 0`. With no transit entry this is D5's code exactly. `FLoadEntry` gains `Kind`. The `ShortMask` write (wt `:8420`) and the comparator (wt `:8400-8402`) test `Kind == Cooling`, never `B == nullptr`. TP2 must run on a colony with **no** cooling floor (cars on -1..-5, Lander only, empty bank) and expect `Served` 0, alongside B's DP6-based case.
8. **Use B's capacity formula, applied inside `RollTransit` only.** It is `Hoist + (Cars x LiftPlacesPerSol - Hoist) x Served / 1000`, int64, used only for the wait computation at `RHSimTransit.cpp:201`. `GetShaftCapacityPlaces` is unchanged, so TR4 and the robot reserve reads keep D4's numbers. No `Debug_SetTransitPowerScale`.
9. **Pin the bank in every suite that fits a car.** Every fixture in `-trams`, `-transitpower`, and any `-deepcommute` case with a car, pins the bank to 1e9 before each step, as `-transit` does. Once D6e lands, `trams.log` and `deepcommute.log` must be line-identical, or each moved line is named. At D6e, grep `depth.log` for a car fitted before any step that prints `LoadW`, `ShedCount`, Warmth or Hope.
10. **Protect `-deepdistricts` from D6c.** Its cases print no commute distance, long count or flow. Otherwise D6c's bar must name each such line. B's "ramp 0 keeps it inert" covers Hope, not distance.
11. **D6c:**
    - B's `FRHDeepWalk` replaces A's `FRHPeopleNet`;
    - no stairs (deviation: stairs and open volumes come with D7);
    - no save bump (A's deviation 3);
    - the trip sort (`RHSimCommute.cpp` end of `DeriveCommuteFlows`) breaks ties on (home ordinal, work ordinal), because several off-net trips share one head `Pos`;
    - `-scale` keeps B's budgets with all 20 floors mapped and 5,000 colonists: cold walk network 5 ms or less, cold commute with transit 10 ms or less.
12. **The version-line count.** Recount literally against the post-D5 baseline (50 runs). D6a, D6b, D6d and D6e bump (v48-v51). Each sub-gate's bar is "save-version lines only", plus the lines named under items 9 and 10.

## Director questions

**Q-A. Should each separate dig on a floor get its own air and Livable rating?**
- **a (recommended): not in step 2.** A floor keeps one air supply and one rating. Shapes, home cells and real walks already make digs into places. Nothing is re-keyed. It can be revisited after step 2.
- b: a derived check with no re-key, as its own sub-gate after D6e. A home or seat in a dig needs a powered air circulator in that dig, and the dig needs `MinLivableCells`. The `-shafts` SH11b deviation line moves on purpose.
- c: full per-dig air and rating, merged when a tunnel joins two digs. Nine saved stores re-keyed, two to three more sessions, and the battery at risk.

**Q-B. How big, and how costly, is a cavern?**
- **a (recommended): up to 8 x 8 cells and 3 floors, carved with the ordinary 3 h `CarveCell`.** A full cavern is 192 cells: 24 Borer-sols, 230 t of spoil, 19.2 t of O2 to fill, more below -5 (D5). No new recipe row, so there is no DataTable to sync.
- b: up to 12 x 12 x 3 (432 cells) with a new bulk `CarveCavern` row at 1.5 h a cell: 27 Borer-sols, 518 t of spoil, 43.2 t of O2.
- c: (a) plus a Struct support bill per cell (50 kg, placeholder).

**Q-C. Your D4 answer moved trams to D6 "with people". Do robots ride them too?**
- **a (recommended): people only.** A tram is a fast, capacity-limited link in the commute. Robots keep to tunnels and lifts, and everything stays world-side.
- b: people and robots. D4's draft robot tram (holds, rides, reserve pricing) is built too, in the riskiest code in step 2: one to two more sessions.

The lift and tram draw rows (40 W against 200 W a car, priority 6 against 8) are placeholders, flagged "director review". They need no question, but the `lifts=1` playtest's assertions must still pass at the chosen draw.

VERDICT: BLOCKED. Base: draft B with A's grafts (i)-(iv). Unblock when required changes 1-12 are written into a REVISION section of this doc.


# MERGED DESIGN AND REVISION (2026-10-22)

This section is the D6 spec. It replaces both drafts above, and the implementer follows it alone. The base is draft B (`d6-caverns-design-draft-b.md`), in B's order. Draft A gives four grafts: (i) one seed per floor, (ii) the home-cell rule, (iii) no save bump for D6c, (iv) the D7 atrium note. The PROFESSOR REVIEW's required changes 1-12 are written in and tagged **[RC n]**.

**Anchors.** HEAD `4620a94` (D4 committed, save v46), read with `git show HEAD:`. D5 (depth, save v47) is being built in the working tree. Its lines are marked (wt). They move while D5 is built, so each one is named by its code as well as its line. The builder re-anchors after D5 lands.

---

## 0. What D6 builds

| Sub-gate | Scope | The switch | Save | New suite (battery runs) |
|---|---|---|---|---|
| **D6a Caverns** | `DigCavern`, `CancelCavern`, `FRHCavern`, the Borer branch, `CarveLatticePoint` | a `DigCavern` order | **v48** | `-caverns` (51) |
| **D6b Deep districts** | floor shapes (`Deep`), home and seat cells, membership, growth inside shapes | `PaintDistrict` below ground with `Value >= 1` | **v49** | `-deepdistricts` (52) |
| **D6c Deep commute** | walks through digs, tunnels and shafts on mapped floors | a mapped floor | **none** | `-deepcommute` (53) |
| **D6d Trams** | `LayTramLine`, the tram edge, the tram load and wait (people only) | `LayTramLine` | **v50** | `-trams` (54) |
| **D6e Transit power** | lift and tram draw; a short sol cuts next sol's capacity | a lift car or tram line exists | **v51** | `-transitpower` (55) |

**Order.**
- **D6a** is self-contained Borer work on D3's pattern.
- **D6b** must ship shapes and the growth dispatch together. Otherwise `RunGrowthStartCheck` (`RHSimGrowth.cpp:616`) would send a deep district, which is not `IsFloor()`, to `PlanSurfaceHomes`.
- **D6c** needs D6b's cells.
- **D6d** needs D6c's network.
- **D6e** is last and separable.

**The bar for every sub-gate:**
- it is one commit, and `rh_battery.sh` is green;
- `diff_battery.py` against the previous baseline shows only the save-version lines, plus any line named under RC 9 or RC 10 **[RC 12]**;
- the default and `districts=1` 110-sol playtests are byte-identical under the `(N bytes` mask;
- `-RHCommuteAudit` margins are unchanged.

The first baseline is post-D5, 50 runs. Its version lines are the 13 D5 named plus whatever `depth.log` prints (DP11). Each later gate adds the lines its predecessor's new suite prints. Recount literally at build time; any other moved line fails the gate. D6c bumps nothing, so its bar has no version lines.

**The seven deferred items:**
1. Caverns: D6a.
2. Floor shapes: D6b.
3. Per-dig reach: not in step 2 (Q-A).
4. Real commutes between digs: D6c.
5. Lift power: D6e.
6. Trams with people: D6d.
7. The O1 re-key: not needed (section 6).

---

## 1. D6a: caverns (v48)

**Data.** `FRHCavern`, saved:
- `Id` (dense, never reused);
- `RegionId` (0);
- `ShaftId`;
- `Top` and `Bottom` (levels, `Top >= Bottom`);
- `Min` and `Max` (lattice corners, region-0 frame);
- `bCancelled`.

Derived, never saved: the uncarved count per floor, keyed on `FloorCellsVersion` (the `FRHTunnel` pattern, `RHSimWorldSubsystem.h:341-355`), and `bFinished`.

**Rows** (placeholders, CSV and DT synced): `CavernMaxSide` 8, `CavernMaxFloors` 3, `MaxOpenCaverns` 2. Config clamps and save refusals share the caps `RH::CavernMaxSideCap` 32, `RH::CavernMaxFloorsCap` 8 and `RH::MaxCavernRecords` 1024.

**`ShaftId`** is fixed when the order executes: `NearestReachingShaft(Bottom, (Min + Max) / 2)` (`RHSimWorldSubsystem.cpp:5141`), with ties to the lower Id. Helper Borers count as shaft 0, as for tunnels. A later `SinkShaft` cannot move the work.

**The order `DigCavern`.**
- `Level` = `Top`; `Value` = the number of floors.
- `Location` and `LocationB` are the corners, snapped by `CmToLattice(origin, ·)` (`.h:665`), never `LatticeOf`.
- The preflight runs at queue time and again at execute time.

**Refusals,** each with a reason:
1. `Bottom` is reached by no shaft (`ShaftReaches`). Every shaft starts at the surface, so the floors above `Bottom` are reached too.
2. A side is over `CavernMaxSide`, there are more floors than `CavernMaxFloors`, or `Bottom < -MaxDepth`.
3. A box point is a shaft column on any of its floors (O9).
4. A box point is an uncarved point of an open tunnel or of another open cavern.
5. **(Graft i, [RC 2]) One seed per floor.** On *every* floor of the box, some uncarved box point must be eligible (defined below): "a cavern opens off a dig or a shaft on every floor". A reaching shaft's column beside the box satisfies every floor at once, because the column runs through all of them.
6. More than `MaxOpenCaverns` are open.
7. Every box point is already carved.

`DigTunnel` refusal 6 (`CanDigTunnel`, `:7797`) and `SinkShaft` refusal 8 (`CanSinkShaft`, `:7483`) gain "an uncarved point of an open cavern". `CancelCavern` (`Value` = Id) works like `CancelTunnel`: no new batch starts, carved cells stay, and a batch in flight lands its cell.

**Eligibility: horizontal only [RC 2].** An uncarved point P on floor L is eligible when it is 4-adjacent to a carved cell on L, or to the column of a shaft that reaches L. There is no vertical rule. Every cavern floor touches a dig or a column, so every cavern cell can be reached by robots (D4's net) and by people (D6c). Cells are never removed, so a point that is eligible once stays eligible. D6b's growth uses the same rule.

**Work items: one predicate each [RC 1].**
- Add `IsTunnelWork(W) := W.X == 2 || W.X == 3` and `IsCavernWork(W) := W.X >= 100`.
- A cavern batch is `X = 100 + (-L)`, `Y = CavernId`. It names its floor, and `-L <= RH::MaxDepthCap` (30) keeps `X <= 130`.
- `PendingBoreWork` keeps its `FIntPoint` format. The bore scan's `X == 0` test (`:1569`) is unchanged.
- **Every reader of `X >= 2` switches to the predicates:**
  - completion, HEAD `:1524` (`else if (Work->X >= 2)` becomes a cavern branch first, then `IsTunnelWork`);
  - the batch-start log, HEAD `:1619` (caverns get their own line, as D3 O4 gave tunnels one);
  - **`GetTunnelBatchesInFlight`, HEAD `:7661`** (`W.Value.X >= 2 && W.Value.Y == TunnelId` becomes `IsTunnelWork(W.Value) && …`). Without this, cavern 1's batches throttle tunnel 1 in `FindTunnelWork` (`:7747`), affect `IsTunnelStalled` (`:7768`), and keep a cancelled tunnel live in `FindLiveTunnelOnPoint` (`:7692`);
  - D5's batch-start pricing, (wt) `:1624-1625` (`WorkItem.X >= 2 ? FindTunnel(WorkItem.Y)`). For a cavern, `DigLevel = -(X - 100)`;
  - D5's (wt) `:1645` start-log branch (`if (WorkItem.X >= 2)`).
- Any later `X >= 2` reader that grep finds is converted the same way. The build's self-review greps `\.X >= 2` and `->X >= 2` over `Source/RedHopeSim`.

**Carving.**
- **Which work.** `FindCavernWork(WorkShaft, WorkItem)` runs after `FindTunnelWork` (`:1591`), only when that found nothing and `Caverns.Num() > 0`. It takes open caverns by Id with `ShaftId == WorkShaft`, on the shallowest floor of the cavern that has an eligible point and whose uncarved count exceeds `GetCavernBatchesInFlight(Id, L)` (counted with `IsCavernWork`).
- **Completion.** Append the eligible box point on floor L that is lowest by (X, then Y), through `CarveLatticePoint`. If none is left (another batch took it): drop the spoil, add no cell, and print a Display line (D3 O5).
- **The recipe** is `CarveCell`: 3 h, 1,200 kg spoil, times D5's `DigPermille(L)`. There is no other bill (Q-B).
- **Asserted in `-caverns` after every append:** the point was eligible.

**One carve helper [RC 3].** `CarveLatticePoint(L, P)`:
1. refuses a shaft column or an already-carved point;
2. does `Floor.Plan.Remove(P)`, as the tunnel path does at `:7962` ("a carved point in the plan is a refused save");
3. calls `AppendCarvedCell` (`:5134-5139`).

The cavern completion and D6b's growth both use it. It is the only new path that appends a cell.

**Relation to D1's floor tables.** A cavern adds no level, no table and no key. Its cells are ordinary cells in each floor's `FRHFloor`, with permanent ordinals. Spiral cursors skip carved points (D2). A carve-queue cell may take a cavern point first, and the cavern skips it (D3 TN6). Each cavern cell counts like any cell for its floor's O2 fill and leak, legacy beds, `bBigEnough`, zoning, and D5 cooling. A cavern touching two digs merges them in `EnsureDigInfo` (`:5272`).

**Relation to D3's tunnels.** Neither may hold the other's uncarved points. A tunnel may start beside a cavern cell (a T-join). A cavern may enclose finished tunnel cells. **No vertical travel inside a cavern in D6.** People and robots change floors by shaft only. Stairs and open volumes come with D7.

**Era.** Carving is `StepProduction` work in both spines. No era refusal (D3 item 5).

**Inertness.** The Borer branch, the widened refusals and the load repair all test an open cavern or a work record with `X >= 100`, and none exists. `GetTunnelBatchesInFlight` gives the same answers, because every existing record has `X` in {0, 1, 2, 3}. The default block is 8 bytes (`NextCavernId` 1, count 0), hidden by the mask.

**Save v48.**
- **Where.** A cavern block straight after D5's warmth block ((wt) `WriteWarmthBlock`, `ReadWarmthBlock`), so no earlier offset moves (K6, SH12, TN1, TR18, DP11). It holds `NextCavernId`, the count, then the records. It is read into locals before teardown.
- **Structural refusals** go through `OutError` ("Save cavern list invalid: …"), never an Error line:
  - Ids not ascending, or not below `NextCavernId`;
  - `RegionId != 0`;
  - a level outside `-MaxDepth..-1`, or reached by no loaded shaft;
  - a side or floor count over its cap;
  - a box point on a column;
  - two open caverns, or an open cavern and an open tunnel, sharing an uncarved point.
- **Warning repair.** A work record with `X >= 100` that names no cavern, or a floor outside its box, completes with spoil and no cell.

**Tests: the new `-caverns` suite.**
- CV1: every refusal, refusal 5 included (a box over -3..-4 beside a dig on -3 only, and no column, is refused; the same box beside a reaching shaft's column is accepted), plus the two widened refusals.
- CV2: the literal append order for a 2 x 2 x 2 box beside a reaching column; spoil per cell; the eligibility invariant; no carved point left in `Plan`, also after a save round trip **[RC 3]**.
- CV3: two Borers on two floors; an empty completion.
- CV4: a -6 batch reads `BatchRemaining_h + SubDt / 50 == 3.45` (D5 item 7's method).
- CV5: cancel with a batch in flight.
- CV6: **mutations [RC 1].** Tunnel 1 and cavern 1 run at once. `GetTunnelBatchesInFlight(1)` counts only the tunnel's batches, and each completion goes to its own branch. Each of the two mutations "test `X >= 2` before `X >= 100` at completion" and "keep `X >= 2` at `:7661`" fails CV6.
- CV7: an era/agent twin appends the same cells in the same order.
- CV8: a save mid-batch equals a twin that never saved; the hand-made refusals; the `X >= 100` repair; a v47 file is refused.
- CV9: ordinal-to-point is identical across a save and a load; `FloorCellFallbacks == 0`.

**-scale** (outside the battery): one 8 x 8 x 3 cavern per shaft. The Borer scan and a cold `EnsureDigInfo` stay within the existing budgets.

---

## 2. D6b: floor shapes, home and seat cells, growth inside shapes (v49)

**Data.** `FRHDistrict` (`RHDistricts.h:46-60`) gains `TArray<FIntVector> Deep`: points (X, Y, Level) in the region-0 frame, sorted by Level descending, then X, then Y.
- A district is exactly one of three things: surface (`Level 0`, `Lattice`), whole floor (`Level < 0`, both empty), or deep (`Deep` non-empty, `Level` = its shallowest floor). One deep district may span floors.
- `Lattice` stays surface-only, so no existing `Lattice` reader can see an underground point.
- `IsFloor()` becomes `Level < 0 && Deep.IsEmpty()`. `IsDeep()` is new. Every existing caller keeps its meaning:
  - `RHSimDistricts.cpp:64`, `:271`, `:275`, `:553`;
  - `RHSimGrowth.cpp:616`, `:1027`;
  - the load at `RHSimWorldSubsystem.cpp:14186`;
  - `RHCheats.cpp:2330`, `:2472`;
  - `RHDistrictVisualizerSubsystem.cpp:223`.

**Painting.**
- `PaintDistrict` with `Level < 0` and `Value = n >= 1` paints the rectangle on floors `Level .. Level - n + 1`. `n <= RH::CavernMaxFloorsCap`.
- `Value 0`, which every existing caller sends, is today's whole-floor order, line for line (`RHSimDistricts.cpp:335-359`). `Value` is a `double` that defaults to 0, and no `PaintDistrict` caller sets it (`RHPlaytest.cpp:1970-1975`, `RHPlayerController.cpp:1448-1453`, `RHCheats.cpp:2346`, `:2357-2360`, the commandlet).
- Corners are snapped by `CmToLattice(origin, ·)`.
- The surface rules carry:
  - at most `RHDistricts::MaxPaintSide` (32) a side;
  - a rectangle over two districts is refused;
  - the same Kind joins the district it overlaps;
  - another Kind is refused;
  - an order that adds nothing is refused.
- In place of coverage, every floor in the range must be reached.
- Rock may be painted, so a player can zone a cavern before it is dug. Shaft columns are left out of the stored points.
- **A floor is either whole or shaped.** A shape on a floor held whole is refused, and so is a whole-floor paint on a floor that holds deep points. Each refusal names the district to erase first.

**Ground cache.** `DeriveDistrictGeo` (`RHSimDistricts.cpp:55-74`) adds `DeepOwner: TMap<FIntVector, int32>`, used for lookups only. Deep points never enter `CellOwner` or `FloorOwner`. The `-RHVerifyCaches` compare (`:87`) adds `DeepOwner`.

**Mapped floor.** A floor is *mapped* when it holds a deep point (D6d adds: or a tram line).

**Home cells (graft ii, [RC 4]).** Derived, never saved. `ColonistHomeCell` (Id -> ordinal) is built in `RefreshJobs` (`:5855`) straight after `BuildBedAssignments` (`:11984-12027`), for residents of mapped floors only:
1. **A resident with a `ColonistBedPiece` lives in that piece's cell:** the ordinal is the piece key's Y. `BuildBedAssignments` hands pieces to the lowest-Id residents first, in (cell ordinal, piece Id) order. This rule reads its result and never re-derives it.
2. **The other residents of the floor, in Id order,** take legacy slots. The non-bed-piece cells are walked in ordinal order, and the k-th such cell offers `floor((k+1) x CPC) - floor(k x CPC)` slots (CPC = `ColonistsPerCell`). A "bed-piece cell" is a Living cell with bed pieces, as in `CountFloorBeds` (`:11618-11660`). The total therefore equals `CountFloorBeds` at any CPC.
3. **Past the last slot:** `INDEX_NONE`. The resident belongs to no deep district, and their commute keeps the head anchor.
- Carving appends legacy slots at the end only, so it never moves anyone already homed. A departure moves later residents, as seats move today.

**Seat cells.** A cell station `(L, i, 0)` is cell i. A module station `(L, INDEX_NONE, BuildingId)` is at `FindCarvedCellAt(L, B.LocationCm)` (`:6166`).

**Membership** (`RHSimDistricts.cpp:125-181`, `:256-311`):
- A floor building: `FloorOwner[L]`, else `DeepOwner(CmToLattice(origin, B.LocationCm), L)`.
- A resident: their home cell's point.
- A seat: its cell's point.
- **Counts:** Cells are the shape's carved points; Beds are the slots in its carved cells on rated floors; Seats come through cells.
- The commute's district rows (`RHSimCommute.cpp:363-370`) use the same lookups. Distances do not change in D6b.

**Cache keys gated on deep data [RC 6].** `FRHDistrictMembershipKey` (`.h:2510`, built at `RHSimDistricts.cpp:113-123`) gains `uint32 DeepCells`. It holds `FloorCellsVersion` while any deep district exists, else 0. With no deep district it is 0 for ever, so the C5 counts and rebuilds cannot move. With one, a carve re-derives the legacy fill.

**Growth inside a shape.** `RunGrowthStartCheck` (`RHSimGrowth.cpp:616`) tests `IsDeep()` first and sends a deep district to two new planners. Whole floors keep `PlanFloorHomes` (`:684-711`), `ExcavateFloor` and the civic floor branch exactly.
- **Homes, the new kind `ERHGrowthKind::DeepCells` (3).** Rated floors only.
  - Pick first the shape's free carved cells (`IsCellFreeForRoom`, `:387-393`), by Level descending, then ordinal.
  - Then eligible uncarved shape points (section 1's horizontal rule; a later pick may lean on an earlier pick), ascending (X, Y).
  - Up to `GrowthCellsPerProject` in all.
  - **Bill:** the bunks per point, plus `O2FillKgPerCell` for each point to be carved.
  - **`TotalSols`:** `GrowthCellSols x max DigPermille(L) / 1000` over the points to be carved, or plain `GrowthCellSols` if none (D5 DP3's rule).
- **Civic:** `PlanCivic`'s floor branch (`:1027-1072`), restricted to the shape. Its `bCarve` case carves an eligible shape point, not the floor's next spiral cell.
- **The record.** `FRHGrowthProject` (`RHGrowth.h:22-56`) gains `TArray<FIntPoint> Points`: lattice points, never ordinals.
- **Completion** re-checks reach and rating, then carves each new point through `CarveLatticePoint` **[RC 3]**, then zones Living and furnishes as `CompleteFloorCells` does (`:1201-1238`). A point that fails is refunded and skipped with a Warning (the `:1222-1226` pattern).

**Inertness.** No deep district can exist without the switch. With none:
- `IsFloor()` answers as today;
- `DeepOwner` is empty;
- `DeepCells` in the key is 0;
- home cells are never built;
- the growth dispatch takes today's branches.

**Era.** A paint executes at `StepUplink` in both spines. Growth records run identically in both bands (C5b-R). Home cells are a pure function of saved state.

**Save v49.**
- **The deep block.** Straight after the cavern block. It holds, per deep district, its Id and points, and, per `DeepCells` or deep civic project, its DistrictId and points. The v37 and v38 slots keep their bytes.
- **Read before teardown, into locals.** Structural refusals go through `OutError`: counts over the caps, a level out of range, the same point listed twice.
- **Attach before the checks [RC 5].** In the district loop (`RHSimWorldSubsystem.cpp:14177-14197`), each `D` receives its `Deep` points from the locals *before* the `IsFloor()` / `FloorsHeld` test at `:14186-14189`. That test is then extended:
  - a level held whole may not also hold deep points;
  - no deep point may sit in two districts;
  - a deep-block Id that names no district, or names a surface district, is invalid.
- **Each new check is a structural `OutError`.** To refuse before teardown, the deep block also carries each deep district's Id and Kind-free level list, and the cross-checks against the district list run on locals. Nothing new ever writes an Error line. A Homes shape and an Industry shape on one floor are two deep districts, so they never meet the whole-floor `FloorsHeld` test.
- Growth points attach to their project the same way, by DistrictId.

**Tests: the new `-deepdistricts` suite.**
- **Two fixture rules [RC 10].** Every fixture keeps 60 colonists or fewer (ramp 0). **And no case prints a commute distance, long count or flow,** so D6c cannot move `deepdistricts.log`.
- DD1: paint, join, a multi-floor shape, every refusal.
- DD2: the frame. A shape under an off-grid head maps a building exactly; the same point through `LatticeOf` would not.
- DD3 **[RC 4]**, the literal case: an unfurnished cell 0 before a furnished cell 3 (two bunk slots). Residents 1 and 2 live in cell 3, and resident 3 in cell 0. Also: CPC 1 and 0.5, an overfull floor, and a departure.
- DD4: counts, beds and seats per shape.
- DD5: a whole-floor twin is byte-equal in counts, membership and growth.
- DD6: `DeepCells` zones free cavern cells, then carves eligible rock; the ghost equals the carve; no carved point is left in `Plan` **[RC 3]**.
- DD7: civic stays inside the shape.
- DD8: an era/agent project finishes on the same step with the same cells.
- DD9: a save round trip with Points; **two shapes on one floor (Homes and Industry) round-trip with no Error line [RC 5]**; the deep-block refusals; a v48 file is refused.
- DD10: `-RHVerifyCaches` is clean, and the membership key moves on a carve only when a deep district exists.

---

## 3. D6c: the deep commute (no save bump)

**The switch is per floor.** On a mapped floor, residents and seats are measured at their cells. On any other floor every line of today's commute runs: the `-floor` key (`RHSimCommute.cpp:335`, `:340`), the head anchor (`:363`, `:369`), D2's cheapest shaft (`PickCommuteShaft`, `:172`) and D4's split. `-percapita` and `-arrivals` map no floor.

**The walk network `FRHDeepWalk` [RC 11]** is draft B's, not draft A's `FRHPeopleNet`. One per mapped floor, derived, never saved.
- **Compartments:** the 4-connected components of carved cells that are not tunnel chain points (`IsTunnelChainCell`, `:7666`).
- **Tunnel cells** are walked cell by cell, 1,000 cm a step.
- **Inside a compartment** a person walks the octile line: `1000 x max(|dx|, |dy|) + 414 x min(|dx|, |dy|)` cm, in integers.
- **Doors:**
  - a compartment cell 4-adjacent to a carved chain point (1,000 cm to step through);
  - a *landing*: the lowest-ordinal carved 4-neighbour of a reaching shaft's column (D3 section 2).
- **Door-to-door distances:** one integer Dijkstra per door over doors and chain points, with ties to the lower node index.
- **`W_L(a, b)`** is the octile when a and b share a compartment. Otherwise it is the minimum over doors d1 (a's) and d2 (b's) of `oct(a, d1) + DD(d1, d2) + oct(d2, b)`. Unconnected is infinite.
- **No stairs.** People change floors only by shaft. Stairs and open volumes come with D7.

**A trip.** `Up(S, n)` is `n x CommutePerFloorCm(S)` with transit, and `n x CommuteShaftCmPerFloor` without.
- **Same floor:** `min(W_L(h, w), Via)`. `Via` is the minimum over landed shafts S1 and S2 of `W(h, land1) + Up(S1, |L|) + Surface(head1, head2) + Up(S2, |L|) + W(land2, w)`.
- **Floor to surface:** the minimum over landed S of `W(h, land_S) + Up(S, |L|) + Surface(head_S, P)`.
- **Floor to floor:** the minimum over shafts landed on both floors of `W_Lh(h, land) + Up(S, |dL|) + W_Lw(land, w)`. If there is none, two shafts via the surface.
- **The choice is wait-free.** Ties go to the lower shaft Id. Then `CommuteWaitCm` of each shaft used is added (D4 2.6), so a crowd never flips routes from sol to sol.
- On mapped floors this replaces D4's round-robin split (REVISION item 5). `ShaftPeople` gains 1 per colonist for each shaft ridden.
- **Off-net.** These keep today's head anchor with no walk, and are counted in `CommuteOffNet`:
  - an unmapped end;
  - a resident with no home cell;
  - a cell with no door path to a landing (D3 R6).
  No trip is ever left unmeasured.

**Keys.**
- On a mapped floor the home key is (L, home ordinal) and the work key is (L, station ordinal or module).
- `FRHCommuteTrip` (`.h:2451`) gains `HomeCell`, `WorkCell` (`INDEX_NONE` elsewhere) and a second shaft Id.
- **The trip sort** (`RHSimCommute.cpp:486-492`) breaks remaining ties on (HomeCell, WorkCell). Several off-net trips share one head `Pos` **[RC 11]**.
- **[RC 6]** `FRHCommuteKey` (`.h:2413-2425`) gains `uint32 DeepWalk`. It is a composite of `FloorCellsVersion`, `TunnelsVersion`, `ShaftsVersion`, `DistrictsVersion` and (D6d) `TramVersion` while any floor is mapped, else 0. With no deep district it is 0 for ever, so the X13 and X14 rebuild counts cannot move. Use separate fields or a strict combination, so no two bumps collide (D4's precedent).

**Effects** go only through the existing terms: `bLong`, the ramped penalty and the R2 work hooks. D5's Shelter and Warmth read `HomeLevel` and do not change.

**Inertness.** No mapped floor exists without D6b's switch. `deepdistricts.log` prints no commute value (RC 10).

**Era.** The commute is a pure function of shared state (C6). Flows are equal at the split.

**Save: none (graft iii) [RC 11].** D6c adds no state and saves nothing derived. Logged as a deviation from the plan's "every gate bumps".

**Tests: the new `-deepcommute` suite.**
- DC1: hand integers inside one compartment, across a 30-cell tunnel, and between unjoined digs through two shafts.
- DC2: floor to surface picks walk plus ride.
- DC3: a crowded shaft does not flip the route the next sol.
- DC4: `ShaftPeople` per shaft ridden.
- DC5: an off-net cell; two off-net trips at one head sort the same in two build orders.
- DC6: an unmapped floor in the same colony is byte-equal to a twin with no deep district.
- DC7: ramp 60, 61 and 160.
- DC8: era equality at the threshold.
- Any case that fits a car pins the bank to 1e9 before each step **[RC 9]**.

**-scale [RC 11].** All 20 floors mapped, 5,000 colonists. A cold walk network takes at most 5 ms. A cold commute with transit takes at most 10 ms (D4's budget). Warm reads are unchanged. A missed budget is a finding for the director, not a budget edit.

---

## 4. D6d: trams with people (v50)

**`LayTramLine`** (`Value` = tunnel Id), refused when:
1. there is no lift car (D4's one switch, `RHSimTransit.cpp:168`);
2. the tunnel is unknown, cancelled or unfinished;
3. the chain is under `TramMinCells` (12);
4. the tunnel already has a line;
5. more than `MaxTramLines` (8, cap `RH::MaxTramLinesCap` 32);
6. short of Struct: `TramStructKgPerCell` (20) x the chain, through D4's `TakeStructForBill`.

**`FRHTramLine`** (saved): `Id`, `RegionId`, `Level`, `TunnelId`, `StopA`, `StopB` (lattice points, never ordinals), `WaitSec` and `PrevWaitSec`. Its floor becomes mapped, and `TramVersion` goes up.

**The edge** joins the two stop cells in `FRHDeepWalk`. People board only at stops.
- The uncrowded cost is `(TramHeadwaySec / 2 + ceil(LenCm / TramSpeedCmPerS)) x CommuteWaitCmPerSec`. Rows: `TramHeadwaySec` 60, `TramSpeedMps` 10, placeholders.
- `WaitSec x CommuteWaitCmPerSec` is added after the route is chosen.
- Example: a 300 m tunnel walks at 30,000 cm, and the tram is (30 + 30) s x 100 = 6,000 cm.

**Capacity.** In `RollTransit` (`RHSimTransit.cpp:180-213`), after the shafts, in ascending line Id:
- `Load = 2 x TramPeople[line] x RampPermille / 1000` (D4 item 8);
- `Cap = TramPlacesPerSol` (600);
- `ComputeTransitWaitSec`;
- `TransitWaitVersion` goes up on any change.

People are world-side, so there is no lag. No car is simulated one by one; the view draws one capsule per line by phase. **Robots do not ride trams** (Q-C (a)).

**Era.** The same step at the same index in both spines. Waits are equal at every threshold.

**Inertness.** No suite lays a line.

**Save v50.** A tram block after the deep block: `NextTramId` (1), the count, then the records. The default is 8 bytes. Structural refusals go through `OutError`: Ids not ascending; a line on an unknown, unfinished or cancelled loaded tunnel; two lines on one tunnel; over the cap; waits out of `0..RH::MaxWaitSecCap`.

**Tests: the new `-trams` suite.** Every fixture fits a car, so every step pins the bank to 1e9 **[RC 9]**.
- TM1: the refusals.
- TM2: the edge integer.
- TM3: riders counted by route; ramp 60, 61 and 160.
- TM4: the wait is added after the choice, with no oscillation.
- TM5: a line maps its floor.
- TM6: save; hand-made refusals; a v49 file is refused.
- TM7: era equality.

---

## 5. D6e: lift and tram power (v51)

**Load entries.**
- Rows (placeholders, director review): `LiftCarW` 200 a car, `TramWPerCell` 5, `TransitLoadPriority` 6.
- In `StepPower` ((wt) from `:8286`), after D5's cooling entries: one entry per fitted shaft (`Cars x LiftCarW`, by shaft Id), then one per tram line (`cells x TramWPerCell`, by Id).
- `FLoadEntry` gains `uint8 Kind`: building 0, cooling 1, lift 2, tram 3, plus an `Index`.

**D5's code that must change [RC 7].**
- **Ungate the scraping check.** The two gates `CoolingEntries > 0`, at (wt) `:8385` (`bScraping = bShedStep || (CoolingEntries > 0 && …)`) and (wt) `:8387` (`if (bShedStep || (bScraping && CoolingEntries > 0))`), both become `CoolingEntries + TransitEntries > 0`. With no transit entry this is D5's code exactly.
- **The comparator** (D5's `Loads.Sort`, which now treats any `B == nullptr` as cooling) orders by Priority, then Kind, then the deeper floor first among cooling entries, then the lower Index among lift and tram entries. With no transit entry it is D5's order, element for element.
- **The `ShortMask` write** (D5's `if (!E.B) ShortMask |= …` inside the walk) tests `E.Kind == Cooling`. A lift or tram entry in the shed prefix instead adds `RoundToInt64(SubDt x 1000)` ms to that shaft's or line's 2-slot ring, tagged with the sol of `GetStepEndSeconds()`.

**The consequence [RC 8].**
- `Served = 1000 - min(1000, ShortMs(s - 1) / 1200)`, in permille.
- `StepTransit` runs before `StepPower` in both spines, so the roll at sol s reads a complete slot s - 1.
- A new `GetShaftServedCapacityPlaces(S)` returns `Hoist` for a shaft with no car. For a fitted shaft it returns `Hoist + (Cars x LiftPlacesPerSol - Hoist) x Served / 1000`, in int64.
- It replaces `GetShaftCapacityPlaces(S)` **only** in the wait computation at `RHSimTransit.cpp:201`. `GetShaftCapacityPlaces` (`:68-76`) is unchanged, so TR4's 1,000 and 4,000 and the robot reserve reads keep D4's numbers. At `Served` 1000 the new formula is exactly `Cars x LiftPlacesPerSol`.
- A tram line's wait uses `TramPlacesPerSol x Served / 1000`.
- So a dark sol raises the next sol's waits. Ride speed, ratings and arrivals do not change.
- No `Debug_SetTransitPowerScale` exists.

**Inertness.**
- With no car and no line, no entry is appended, and D5's code runs.
- `-transit` fits cars, but every step pins the bank to 1e9 (`RHSimTransitSuite.cpp:76`, `:82`, `:463`, `:529`; the jump at `:435` follows a pinned step). So no step scrapes, `Served` is 1000, and the waits are D4's exactly.
- `RequestChargeWh` takes from the bank before it tests `GenW > LoadW` (`RHSimWorldSubsystem.cpp:12673-12679`), so pad charging is unchanged. No `transit.log` line prints `LoadW`.
- **[RC 9]** `trams.log` and `deepcommute.log` must stay line-identical, because their car fixtures pin the bank. Otherwise each moved line is named.
- At build time, grep `depth.log` for a car fitted before any step that prints `LoadW`, `ShedCount`, Warmth or Hope. Today only DP12 fits a car, and it is the last case.
- `-scale` (16 cars) is outside the battery. `lifts=1` is outside the byte-identical set, but its assertions must pass at the chosen draw.

**Era.** D5's bank-cycle argument carries over. `|dServed| <= 2 x 60,000 / 1200 = 100` permille, under D5 R2's two fixture conditions: a daily reset, and a dusk onset within 120 s.

**Save v51.** A power block after the tram block: per fitted shaft and per line, two (sol tag, ms) slots. The default is 4 bytes (count 0). Structural refusals go through `OutError`: a tag below -1, a tag whose `tag % 2` is not its slot, ms above one sol.

**Tests: the new `-transitpower` suite.** Every fixture pins the bank except the shortage cases themselves **[RC 9]**.
- TP1: the entries and their order. With no transit entry the sort equals D5's.
- TP2 **[RC 7]**:
  - (a) **no cooling floor:** cars on shaft 0, floors -1..-3 only, the Lander as the only generator, the bank emptied once, one night. `Served` is 0 and the next sol's capacity is hoist capacity;
  - (b) D5 DP6's fixture, with a cooling floor on -10, gives the same.
  - Mutation: keep the `CoolingEntries > 0` gates, and (a) reads about 500.
- TP3: a (ii) scraping step, with nothing shed, counts as short.
- TP4: era tolerance under the two conditions.
- TP5: a save mid-sol.
- TP6: `transit.log` is line-identical.

---

## 6. The O1 re-key: not needed

No D6 operation removes or reorders a cell:
- caverns and growth append through `CarveLatticePoint` -> `AppendCarvedCell`;
- `Plan` removal is not a cell change;
- repaints and erases touch only district points;
- home cells and walk networks are derived;
- trams key on a tunnel Id and lattice stops;
- growth `Points` are lattice points.

D1 R0's append-only argument holds. Each suite asserts ordinal-to-point across a save and a load, and `FloorCellFallbacks == 0`.

**Forward note for D7 (graft iv).** An atrium flags cells as open. It never deletes them. If D7 removed a cell, it would become the re-key gate for rooms, furniture, seats, planted cells, queued orders and growth `Index`.

## 7. Per-dig reach: not in step 2

A floor keeps one air supply and one Livable rating (`StepHabitability`, `:2802`; `bBigEnough`, `:2846`). Shapes, home cells and real walks make digs into places for people without the nine-store re-key D2 costed. Q-A.

## 8. Era parity

Every step involved runs at the same index in both spines: `StepUplink`, `StepTransit`, `StepProduction`, `StepCityGrowth` and `StepPower`. Nothing is band-specific. Era logistics still teleport past lifts and trams (the C4/D2/D4 deviation, extended). No new era refusal.

## 9. Risks

- **R1. Two lattice frames.** Deep reads go through `CmToLattice`, and DD2 pins it.
- **R2. Octile undercounts a concave painted compartment.** Tunnels are walked exactly.
- **R3. Home cells move** for later-Id residents when someone leaves (as seats do). It is deterministic.
- **R4. The first shape on a floor changes that floor's commutes** (from D6c). Its alert says so, as Gate-D placeholder copy.
- **R5. A cavern's O2 bill** (19.2 t for 192 cells) can make a weak filter's floor flap while it is dug (the drop test, `:2872`). The ghost shows the bill.
- **R6. D6e relies on bank pins** in `-transit`, `-trams` and `-deepcommute`. TP6 and the RC 9 bar guard it.
- **R7. People walk `FRHDeepWalk`; robots use D4's `FRHDeepNet`.** The overlays can disagree.
- **R8. Anchors move** with D5 and with each sub-gate. Re-anchor and recount each time.

## 10. Deviations to log in `docs/design-decisions.md`

1. Per-dig air and rating are not in step 2 (Q-A).
2. No vertical travel inside a cavern until D7.
3. On mapped floors the commute takes the cheapest route, not D4's split.
4. D6c takes no save bump.
5. Trams carry people only (Q-C).
6. Lift power cuts capacity, not ride speed.
7. Era teleports past trams too.

## Director questions

**Q-A. Should each separate dig on a floor get its own air and Livable rating?**
- **a (recommended): not in step 2.** A floor keeps one air supply and one rating. Shapes, home cells and real walks already make digs into places. Nothing is re-keyed. It can be revisited after step 2.
- b: a derived check with no re-key, as its own sub-gate after D6e. A home or seat in a dig needs a powered air circulator in that dig, and the dig needs `MinLivableCells`. The `-shafts` SH11b deviation line moves on purpose.
- c: full per-dig air and rating, merged when a tunnel joins two digs. Nine saved stores re-keyed, two to three more sessions, and the battery at risk.

**Q-B. How big, and how costly, is a cavern?**
- **a (recommended): up to 8 x 8 cells and 3 floors, carved with the ordinary 3 h `CarveCell`.** A full cavern is 192 cells: 24 Borer-sols, 230 t of spoil, 19.2 t of O2 to fill, and more below -5 (D5). No new recipe row, so no DataTable to sync.
- b: up to 12 x 12 x 3 (432 cells), with a new bulk `CarveCavern` row at 1.5 h a cell: 27 Borer-sols, 518 t of spoil, 43.2 t of O2.
- c: (a), plus a Struct support bill per cell (50 kg, placeholder).

**Q-C. Your D4 answer moved trams to D6 "with people". Do robots ride them too?**
- **a (recommended): people only.** A tram is a fast, capacity-limited link in the commute. Robots keep to tunnels and lifts, and everything stays world-side.
- b: people and robots. D4's draft robot tram (holds, rides, reserve pricing) is built too, in the riskiest code in step 2: one to two more sessions.

The draw rows (`LiftCarW`, `TramWPerCell`, `TransitLoadPriority`) are placeholders marked "director review". The `lifts=1` playtest must pass at whatever draw is chosen.


# DIRECTOR ANSWER (2026-10-22)

- **Q-A (a): separate air and rating per dig is NOT in step 2.** Reach, air, the Livable rating and the size minimum stay floor-wide through D8. This is **deferred, not ruled out**: a later step may revisit it (options (b) and (c) above are recorded for that, not for D6). Deviation 1 reads "per-dig air and rating deferred past step 2".
- **Q-B (a): caverns are up to 8 x 8 cells and 3 floors, carved with the ordinary `CarveCell` recipe.** There is no new bill and no new data row: no `CarveCavern` recipe row, and no Struct support bill.
  - **This supersedes section 1's "Rows" line.** The cavern limits are code constants in `RHSimWorldSubsystem.h`, not `RH_Config` rows: `RH::CavernMaxSide` = 8, `RH::CavernMaxFloors` = 3, `RH::MaxOpenCaverns` = 2.
  - So D6a adds nothing to `docs/data/RH_*.csv` or the DataTables, and needs no CSV or DT sync.
  - The save's structural refusals test these constants directly. The separate `*Cap` constants and `MaxCavernRecords` (1024) stay.
  - A later change to the limits is a code change, which the director can request.
- **Q-C (a): trams carry PEOPLE ONLY. Robots do not ride trams.** D6d builds no robot tram edge in `FRHDeepNet`, no robot hold or ride on a tram, and no tram term in the robot reserve. Robots keep D4's tunnels and lifts.

The MERGED DESIGN AND REVISION above already follows all three answers: its sections 1, 4, 7 and 10 assume (a). The other branches of Q-A, Q-B and Q-C are dropped from the D6 build.


# REFRESH AT HEAD 31ffa6f (2026-10-25): D6e only

A read-only maintenance pass over **D6e (lift and tram power)**, the one sub-gate of this design not yet built. It covers section 5 of the MERGED DESIGN AND REVISION, the draft's section 7 and the R6 risk. D6a-D6d, D8, the economy fixes (v54) and E4 (v55) have all landed since. Nothing here changes the design. Where the code contradicts the text, this section says what to use instead.

**Which text is authoritative.** The MERGED section 5 supersedes the draft's section 7:
- its rows are `LiftCarW` 200, `TramWPerCell` 5 and `TransitLoadPriority` 6, not the draft's 40 W, 2 W and priority 8;
- its suite is `-transitpower`, not the draft table's `-liftpower`.

None of D6e's rows or names exists at HEAD yet (`git grep` finds no `LiftCarW`, `TramWPerCell`, `TransitLoadPriority` or `transitpower`).

## Re-anchored references

Section 5 cites D5's uncommitted worktree, "(wt)", and older HEADs. Each anchor is found again here by function:

| Section 5 says | At HEAD | Notes |
|---|---|---|
| `StepPower` "(wt) from `:8286`" | `W:8461` `StepPower` | |
| `FLoadEntry` (gains `Kind` and `Index`) | `W:8473`: `{ B, WantW, Priority, CoolLevel }` | As D5 left it. |
| The scraping gate "(wt) `:8385`" | `W:8560` (`const bool bScraping = bShedStep \|\| (CoolingEntries > 0 && ...)`) | Unchanged text. |
| The walk gate "(wt) `:8387`" | `W:8562` (`if (bShedStep \|\| (bScraping && CoolingEntries > 0))`) | Unchanged text. |
| D5's `Loads.Sort` comparator | `W:8566` | |
| The `ShortMask` write (`if (!E.B) ShortMask \|= ...`) | `W:8595` | |
| *(not cited)* `StepDepthWarmth(SubDt, ShortMask, CoolingEntries > 0)` | `W:8606` | Its third argument must **stay** cooling-only when the gates widen; Warmth reads only cooling floors. |
| "`StepTransit` runs before `StepPower` in both spines" | `W:675` before `W:690` (agent); `W:847` before `W:862` (era) | Holds. |
| The wait computation, `RHSimTransit.cpp:201` | **`RHSimTransit.cpp:375`**, in `RollTransit`: `ComputeTransitWaitSec(Loads[S], GetShaftCapacityPlaces(S))` | D6d restructured `RollTransit`: every load is read first, then the waits. `GetShaftServedCapacityPlaces(S)` replaces the capacity argument here only. |
| A tram line's wait | **`RHSimTransit.cpp:384`**: `ComputeTransitWaitSec(TramLoads[i], (int64)TramPlacesPerSol)` | `TramPlacesPerSol x Served / 1000` replaces the second argument. |
| `GetShaftCapacityPlaces`, `RHSimTransit.cpp:68-76` | `RHSimTransit.cpp:235-243` | Unchanged, as section 5 requires: TR4's figures and the robot reserve keep D4's numbers. |
| `RequestChargeWh` takes from the bank before `GenW > LoadW` (`RHSimWorldSubsystem.cpp:12673-12679`) | `W:12821` `RequestChargeWh`; the bank test at `W:12844-12846`, then `GenW > LoadW` at `W:12849` | Holds. |
| `-transit`'s bank pins at `RHSimTransitSuite.cpp:76`, `:82`, `:463`, `:529` (the jump at `:435`) | `:77`, `:83`, `:464`, `:530` (the jump at `:436`) | +1, from E4's version edits. |
| `depth.log`: "only DP12 fits a car (wt `RHSimDepthSuite.cpp:870`)" | DP12 starts at `RHSimDepthSuite.cpp:860` and fits the car at `:870`; it is still the last case, and no step follows the fit | Holds. |

## Who fits a car now (the inertness bar, RC 9)

Section 5's inertness rests on every fixture that fits a lift car also pinning the bank. At HEAD:

| Where | Fits a car | Pins the bank | Under D6e |
|---|---|---|---|
| `-transit` | yes | yes (`RHSimTransitSuite.cpp:77`, `:83`, `:464`, `:530`) | `Served` 1000; TP6 holds it line-identical. |
| `-trams` (D6d) | yes | yes, every fixture (`RHSimTramSuite.cpp:17`, `:152`, `:158`) | RC 9 was built in. Line-identical. |
| `-deepcommute` (D6c) | yes | yes (`RHSimDeepCommuteSuite.cpp:142`, `:148`) | Line-identical. |
| `-depth` DP12 | yes (`:870`) | no step follows | Nothing moves. |
| `-scale`'s lifts case and D6d's SCALE trams case (`RHSimCommandlet.cpp:16324`, `:16860`) | yes | no | Outside the battery; their assertions must pass at the chosen draw. |
| The playtest bot (`RHPlaytest.cpp:932-950`) | only under `lifts=1` | no | Outside the byte-identical runs, as section 5 says. |
| **The click test** (`RHClickTest.cpp:1973`, D8's section setup) | **yes** | **no** | **Not in section 5.** Its steps check layout and labels, not wait values, so it should stay 86 of 86; but its colony can scrape at night, so the lift labels' waits may differ. Re-run it. |

## The save now

- **v51 is taken** (E1's ledger). HEAD is v55. The forecast queue:
  1. the step-2 fixes, no bump;
  2. E5, v56;
  3. E6, v57;
  4. then D6e and D7 in build order (v58 / v59).

  D6e takes the next free version at build time.
- **The block must be a frame.** Section 5's "a power block after the tram block" with "structural refusals through `OutError`" was written before the economy fixes. Every block since is an `RHSaveFrame` frame (`Source/RedHopeSim/Public/RHSaveFrame.h`) at the end of the file:
  - its own tag;
  - written after the last frame then written (`'RHE4'` at `W:13866` today, or E5's or E6's);
  - read after the last tail reader (`W:14660` today).

  A frame is read after teardown, so it **repairs with a Warning instead of refusing**:
  - a slot whose sol tag is below -1, whose `tag % 2` is not its slot, or whose ms is above one sol is emptied;
  - a lost frame empties every slot.

  The cost of a repair is that one sol's `Served` reads 1000, and the waits stay D4's for that sol. Putting the block mid-file, as written, would also work, but it would be the only new block since v53 outside a frame. See finding F1.
- **The size lines** that move with any new frame: CV8, DP11, EC1's two, DC9, MX1's file size (`RHSimMarketSuite.cpp:565`) and MX9's round trip (`:1359`), plus any that E5 or E6 add.

## Battery baseline

- **Battery:** `Saved/baselines/battery_post_e4/` (58 logs), or the newest baseline the gates before D6e leave. `-transitpower` adds a run.
- **The line-identical bar** (RC 9): `transit.log`, `trams.log` and `deepcommute.log`, by name. `depth.log` too (DP12).
- **Playtest:** `Saved/baselines/ind_full_playtest.txt`. The default bot fits no car, so no transit entry is ever appended: byte-identical.

## Stale or contradicted text

1. **The draft's section 7 and its sub-gate table** (rows, `-liftpower`): superseded by the MERGED section 5, as above.
2. **"Save v51"**: see "The save now".
3. **Section 5's `RHSimTransit.cpp:201` and `:68-76`**: now `:375` and `:235-243`. The tram wait it adds goes at `:384`.
4. **R6 / RC 9's suite list** omits the click test: see the table.

## Blockers and findings

**Blockers: none.**

**Findings for the main session:**
- **F1. The block's placement.** Section 5 has D6e's accumulators as a mid-file block that refuses before teardown. The house rule since v54 is a framed tail block that repairs. This refresh writes the framed form, because a lost or doctored accumulator costs at most one sol's served share. If the main session prefers the original refusal semantics, it should say so before the build.
- **F2. The click test fits a car without pinning the bank** (`RHClickTest.cpp:1973`). Under D6e that colony's lift waits can rise after a dark sol. No current step reads a wait, so this is a re-run, not a fix. A future click step that pins a wait value would need a bank pin.
