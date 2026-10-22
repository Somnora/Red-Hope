# D6, caverns and underground districts: design, draft B (2026-10-22, for adversarial review)

Step 2 (going deep), gate 6. Design only. No code is written.

**Why "draft B".** `d6-caverns-design.md` in this folder was written by another agent while this draft was in progress. This draft does not edit it. The two agree on most decisions. Section 11 lists where they differ. Three of those differences are defects in the other draft that this one fixes (11.1-11.3).

**Anchors.** Code is read at HEAD `4620a94` with `git show HEAD:`. D4 is committed (save v46). D5 (depth, save v47) is being built in this tree now. Its working-tree lines are quoted only where D6 must branch around them, and they are marked "(wt)". Every `RHSimWorldSubsystem.cpp` anchor moves when D5 lands. The builder re-anchors after D5, as D5 did after D4.

**Sources.** `going-deep-plan.md` (the D6 and D7 rows, the D6 risks, Q2a "you paint the shapes the Borer carves"); D1-D5 with every REVISION and DIRECTOR ANSWER; `c5-districts-design.md` (C5b-R project records); `c6-traffic-design.md` (R8 variant (a), the 60-160 ramp).

**What you see.** You drag a cavern box over three floors, and the Borers hollow it out from the top down. You paint a Homes shape and an Industry shape on one floor. Growth fills the Homes shape with bunks. A home in dig A with a job in dig B becomes a real walk, or a ride up and down. A tunnel or a tram shortens it. In a brownout the lifts carry less the next sol.

---

## 0. Facts the design rests on (HEAD `4620a94`)

- **A floor district is a whole floor.** `FRHDistrict::Lattice` is "surface only" (`RHDistricts.h:53`), and `IsFloor()` is `Level < 0` (`:59`). The ground cache maps `FloorOwner` (Level -> Id, `RHSimDistricts.cpp:64-67`). Floor buildings, floor residents and floor seats all read it (`:142-145`, `:172-175`, `:302-305`). Load rejects a floor district that has a lattice (`RHSimWorldSubsystem.cpp:14186-14189`). This is the house Error-and-keep pattern.
- **No caller sets `Value` on a `PaintDistrict` order.** The callers are the playtest (`RHPlaytest.cpp:1970-1975`), the deck (`RHPlayerController.cpp:1448-1453`), the cheats (`RHCheats.cpp:2346`, `:2357-2360`) and the 16 commandlet orders. So a new meaning for `Value` is inert.
- **Two lattice frames.** Surface districts use the world grid (`LatticeOf`, `RHSimDistricts.cpp:46-51`); floors use region 0's frame on shaft 0's off-grid head (`CmToLattice`, `.h:665`). D1 R1: never compare them.
- **A floor resident has no cell** (`HomeLevel`, `HomeHab`, `.h:60-64`), but beds are already assigned, derived: `BuildBedAssignments` (`:11984-12027`) walks bed slots per floor in cell order, then piece Id, colonists in Id order (`ColonistBedPiece`, `.h:2883`). Legacy beds have no piece: every carved cell holds `ColonistsPerCell` beds, except a Living cell with bed pieces (`CountFloorBeds`, `:11618-11660`).
- **Seats already have a cell:** `(L, ordinal, 0)`, or `(L, INDEX_NONE, BuildingId)` for a module, which needs a carved cell (D2 REVISION O4). Both come from `RefreshJobs` (`:5855`).
- **Air, rating and size are per floor** (`StepHabitability` `:2802`, `bBigEnough` `:2846`, `IsFloorCirculated` `:2786-2800`). D2 logged this and named D6.
- **Cells are append-only.** `AppendCarvedCell` (`:5134-5139`) is the only writer. Digs are a flood fill in ordinal order (`EnsureDigInfo`, `:5272-5335`).
- **The Borer loop** runs bore, carve queue, then tunnel fronts (`:1573-1594`); the bore-in-flight scan reads `X == 0` only (`:1569`). Completion sends **every `Work->X >= 2`** to the tunnel (`:1524-1526`), and D5's batch start prices every `WorkItem.X >= 2` at a tunnel's level ((wt) `:1624-1625`).
- **The commute.** A floor home's key is `-floor` (`RHSimCommute.cpp:335`), a floor seat's too (`:340`), both anchored at a head (`:363`, `:369`). Same floor is 0 cm with transit (`:244`) and without (`:268`). With transit a floor-to-floor trip splits over the reaching shafts (D4 item 5). The ramp is open in `-percapita` (5,000) and `-arrivals` (1,500), on floor-only colonies (C6 R8).
- **Transit.** `StepTransit` returns at once without a car (`RHSimTransit.cpp:166-171`); `RollTransit` reads `GetShaftPeopleLoad` and `GetShaftCapacityPlaces` per shaft (`:180-213`, `:95`, `.h:755`). No tram code exists (D4 DIRECTOR ANSWER Q-B).
- **D5 `StepPower`** ((wt) from `:8286`): `FLoadEntry {B, WantW, Priority, CoolLevel}`; the comparator treats **any `B == nullptr` as cooling**; the scraping walk marks short floors in `ShortMask` (bit `-Level`). The warmth block is the last header block ((wt) write `:13308`, read `:13765`).
- **Growth.** A floor district goes to `PlanFloorHomes` (`RHSimGrowth.cpp:616`, `:684-711`), which carves at the floor's end (`CompleteFloorCells`, `:1201-1238`). Growth never zones a tunnel cell (`:387-393`). Homes wait in the Failing band (`:558`).

## 1. The seven deferred items, decided

1. **Caverns: built (D6a).** See section 3.
2. **District shapes on a floor: built (D6b), with growth inside them (D6b).**
3. **Per-dig reach: re-deferred for good,** unless the director answers Q-A otherwise (section 8).
4. **Real commutes between digs: built (D6c).**
5. **Lift power: built (D6e),** together with tram power, as D5 O2 ruled.
6. **Trams with people: built (D6d).** People only, under Q-C (a).
7. **The O1 re-key: not needed.** Nothing in D6 removes or reorders a cell (section 9).

## 2. The split

Every sub-gate meets the same bar:
- it is one commit, battery green;
- every suite is line-identical to the previous baseline except the lines that print the save version;
- the default and `districts=1` 110-sol playtests are byte-identical under the `(N bytes` mask;
- `-RHCommuteAudit` margins are unchanged.

| Gate | Scope | The switch | Save | New suite (runs) |
|---|---|---|---|---|
| **D6a Caverns** | `DigCavern` / `CancelCavern`, `FRHCavern`, the Borer branch | a `DigCavern` order | **v48** | `-caverns` (51) |
| **D6b Deep districts** | floor shapes, cells for homes and seats, membership, growth inside shapes | `PaintDistrict` with `Level < 0` and `Value >= 1` | **v49** | `-deepdistricts` (52) |
| **D6c Deep commute** | walks through digs, tunnels and shafts on *mapped* floors | a mapped floor | **v50** (fence) | `-deepcommute` (53) |
| **D6d Trams** | `LayTramLine`, the tram edge, the tram load and wait | `LayTramLine` | **v51** | `-trams` (54) |
| **D6e Transit power** | lift and tram draw; a short sol cuts the next sol's capacity | a lift car or tram line exists | **v52** | `-transitpower` (55) |

**Order.**
- **D6a first.** It is Borer work on the pattern D3 proved, it touches no commute code, and D6b's growth carves with D6a's eligibility helper.
- **D6b next.** It is the only gate that changes district membership, so the C5 suites are its risk.
- **D6c third.** It is the riskiest: the first change to commute anchors since C6. It needs D6b's home cells.
- **D6d needs D6c's network.**
- **D6e is last and separable.** It touches D5's freshly built `StepPower`.

**Version lines.** D5's bar names 13 lines plus whatever `depth.log` prints (DP11). Each later gate adds its predecessor's new suite. Recount literally against the newest baseline at build time. Any other moved line fails the gate.

## 3. D6a: caverns

**Data.** `FRHCavern` (saved): `Id`, `RegionId` (0), `ShaftId`, `Top` and `Bottom` (levels), `Min` and `Max` (lattice corners, region-0 frame), `bCancelled`. Derived, never saved: the uncarved count per floor, keyed on `FloorCellsVersion` (the `FRHTunnel` pattern, `.h:341-355`), and `bFinished`.

**Rows** (placeholders): `CavernMaxSide` 8, `CavernMaxFloors` 3, `MaxOpenCaverns` 2. The caps are `RH::CavernMaxSideCap` 32, `CavernMaxFloorsCap` 8 and `MaxCavernRecords` 1024. Config and save share the caps, as D2-D4 do.

**`ShaftId`** is fixed when the order executes: `NearestReachingShaft(Bottom, (Min + Max) / 2)` (`:5141`), with ties to the lower Id. Helpers count as shaft 0, as for tunnels. Fixing it at order time means a later `SinkShaft` cannot move the work (D2 R5).

**The order `DigCavern`:** `Level` = `Top`, `Value` = the number of floors, `Location` / `LocationB` = the corners, snapped by `CmToLattice(origin, ·)`. The preflight runs at queue and again at execute. Refusals, each with a reason:
1. `Bottom` is reached by no shaft (`ShaftReaches`). Every shaft starts at the surface, so every floor above `Bottom` is reached too.
2. A side is over `CavernMaxSide`, the box has more floors than `CavernMaxFloors`, or `Bottom < -MaxDepth`.
3. A box point is a shaft column on any of its floors (O9).
4. A box point is an uncarved point of an open tunnel or of another open cavern.
5. No `Top` point is *eligible* (below): "start a cavern at the edge of a dig or beside a shaft".
6. More than `MaxOpenCaverns` are open.
7. Every box point is already carved.

`DigTunnel` refusal 6 and `SinkShaft` refusal 8 gain "an uncarved point of an open cavern", as D3 added for tunnels. `CancelCavern` (`Value` = Id) works like `CancelTunnel`.

**Eligibility (one rule, shared with D6b's growth).** An uncarved point P on floor L is eligible when it is 4-adjacent to a carved cell on L, or to the column of a shaft that reaches L, or (caverns only) P is carved on L+1 and L+1 is in the same cavern. So a cavern grows down from its roof, and its lower floors need no dig of their own. Cells are never removed, so a point once eligible stays eligible.

**Carving.**
- `FindCavernWork(WorkShaft, WorkItem)` runs after `FindTunnelWork` (`:1591`), only when that found nothing and `Caverns.Num() > 0`. It takes open caverns by Id with `ShaftId == WorkShaft`, on the shallowest floor whose uncarved count exceeds its batches in flight.
- **The work item is `X = 100 + (-L)`, `Y = CavernId`.** It carries its floor, so D5's `DigPermille(L)` prices the batch it carves. `PendingBoreWork` keeps its `FIntPoint` format; the `X == 0` bore scan (`:1569`) is unchanged.
- **Branch order is load-bearing.** Completion (`:1524`) and D5's batch start ((wt) `:1624-1625`) both read `X >= 2` as "tunnel". `X >= 100` must be tested **first** at both sites, or a cavern batch goes to `CompleteTunnelCell`. Caverns get their own start log line (D3 O4).
- **Completion** appends the eligible box point on that floor that is lowest by (X, then Y). If none is left: spoil, no cell, a Display line (D3 O5). Asserted in `-caverns` after every append: the point was eligible.
- **Recipe:** `CarveCell` (3 h, 1,200 kg spoil) x `DigPermille(L)`. No other bill (Q-B).

**How caverns relate to D1's per-floor tables.** A cavern adds no level, table or key. It appends ordinary cells to each floor's `FRHFloor`, with permanent ordinals. `Plan` is untouched; spiral cursors skip carved points (D2); a carve-queue cell may take a cavern point first and the cavern skips it (D3 TN6). Each cavern cell counts like any cell for its floor's O2 fill and leak, legacy beds, `bBigEnough`, zoning and D5 cooling. A cavern touching two digs merges them in `EnsureDigInfo`, as a tunnel does.

**How caverns relate to D3's tunnels.** Neither may hold the other's uncarved points. A tunnel may start beside a cavern cell (a T-join). A cavern may enclose finished tunnel cells. **Vertical travel inside a cavern is not modelled in D6.** People and robots change floors only by shaft. Stairs and open volumes are D7 (atriums). That is logged as a deviation.

**Era.** Carving is `StepProduction` work in both spines. There is no era refusal (D3 item 5).

**Inertness.** The Borer branch, the widened refusals and the save repair all test an open cavern. The default block is 8 bytes (`NextCavernId` 1, count 0), hidden by the mask.

**Save v48.** The cavern block goes straight after D5's warmth block, so no earlier offset moves (K6, SH12, TN1, TR18, DP11): `NextCavernId`, the count, the records. It is read into locals before teardown.
- **Structural refusals** through `OutError` ("Save cavern list invalid: …"), no Error line: Ids not ascending or not below `NextCavernId`; `RegionId != 0`; a level outside `-MaxDepth..-1` or reached by no loaded shaft; a side or floor count over its cap; a box point on a column; two open caverns, or a cavern and a tunnel, sharing an uncarved point.
- **Warning repair:** a work record `X >= 100` naming no cavern, or a floor outside its box, completes with spoil and no cell.

**Tests (`-caverns`).** CV1 every refusal, plus the two widened ones. CV2 literal append order for a 2 x 2 x 2 cavern beside a spiral dig, spoil per cell, the eligibility invariant. CV3 two Borers on two floors, an empty completion. CV4 a -6 batch has `BatchRemaining_h + SubDt / 50 == 3.45` (D5 item 7's method). CV5 cancel with a batch in flight. CV6 the mutation "test `X >= 100` after `X >= 2`" fails CV2. CV7 an era/agent twin appends the same cells in the same order. CV8 save mid-batch equals a twin; the hand-made refusals; the repair; v47 refused. CV9 ordinal-to-point identical across save and load, `FloorCellFallbacks == 0`.

**-scale.** One 8 x 8 x 3 cavern per shaft (4 x 192 cells). The Borer scan and a cold `EnsureDigInfo` stay inside the existing budgets.

## 4. D6b: district shapes on a floor, cells for homes and seats, growth inside shapes

**Data.** `FRHDistrict` gains `TArray<FIntVector> Deep`: points (X, Y, Level) in the region-0 frame, sorted by Level descending, then X, then Y.
- A district is exactly one of: surface (`Level 0`, `Lattice`), whole floor (`Level < 0`, both arrays empty), or deep (`Deep` non-empty, `Level` = its shallowest floor). One deep district may span floors, so a shape over a three-floor cavern is one named place.
- `IsFloor()` becomes `Level < 0 && Deep.IsEmpty()`, and every existing caller keeps its meaning: `RHSimDistricts.cpp:64`, `:271`, `:275`, `:553`, `RHSimGrowth.cpp:616`, `:1027`, the load `:14186`, `RHCheats.cpp:2330`, `:2472`, `RHDistrictVisualizerSubsystem.cpp:223`. `IsDeep()` is new.

**Painting.** `PaintDistrict` with `Level < 0` and `Value = n >= 1` paints the rectangle on floors `Level .. Level - n + 1`. `Value 0` is today's whole-floor order, line for line (`RHSimDistricts.cpp:335-359`).
- The surface rules carry: at most 32 x 32 a side; over two districts refused; the same Kind joins; another Kind refused; an order that adds nothing refused. In place of coverage, every floor in the range must be reached.
- Rock may be painted: that is how a player zones a cavern before it is dug. Shaft columns are left out of the stored set.
- **A floor is either whole or shaped.** A shape on a whole-floor district's floor is refused, and so is a whole-floor paint on a floor with shapes; each refusal names the district to erase first.

**Ground cache.** `DeriveDistrictGeo` (`:55-74`) adds `DeepOwner: TMap<FIntVector, int32>`, lookups only; `-RHVerifyCaches` compares it (`:87`).

**Cells for homes and seats (derived, never saved).** A floor is *mapped* when it holds a deep district point (D6d adds tram lines).
- **Home cells.** `ColonistHomeCell` (Id -> ordinal), built in `RefreshJobs` beside `BuildBedAssignments`, for mapped floors only. Each floor's bed slots are walked in carve order. A Living cell with bed pieces offers its pieces' slots in `BuildBedAssignments`' own order (piece Id, `Capacity` each). The k-th other cell offers `floor((k+1) x CPC) - floor(k x CPC)` legacy slots (CPC = `ColonistsPerCell`), so the total equals `CountFloorBeds` exactly. Residents take slots in Id order. Past the last slot: `INDEX_NONE`, measured at the head anchor as today.
- Asserted: a furnished resident's slot cell is their `ColonistBedPiece`'s cell. Carving appends slots at the end only, so it never moves anyone; a departure moves later residents, as seats already move (R3).
- **Seat cells:** the cell station's ordinal, or `FindCarvedCellAt(L, module.LocationCm)`.

**Membership.** A floor building: `FloorOwner[L]`, else `DeepOwner(CmToLattice(origin, B.LocationCm), L)`. A resident: their home cell. A seat: its cell. Counts: Cells are the shape's carved points; Beds the slots in them on rated floors. The commute's district rows use the same lookups (`RHSimCommute.cpp:363-370`); distances do not change in D6b.

**Growth inside a shape.** `RunGrowthStartCheck` (`RHSimGrowth.cpp:616`) sends a deep district to two new planners. Whole floors keep `PlanFloorHomes`, `ExcavateFloor` and the civic floor branch exactly.
- **Homes, new kind `DeepCells` (3),** rated floors only: first the shape's free carved cells (`IsCellFreeForRoom`, Level descending, then ordinal), then eligible uncarved shape points (section 3's rule; a later pick may lean on an earlier one), ascending (X, Y), up to `GrowthCellsPerProject`. Bill: bunks per point plus `O2FillKgPerCell` per point to carve. `TotalSols = GrowthCellSols x max DigPermille / 1000` over the points to carve (plain `GrowthCellSols` if none).
- **Civic:** `PlanCivic`'s floor branch (`:1027-1072`) restricted to the shape; its `bCarve` carves a shape point, not the next spiral cell.
- **The record** gains `TArray<FIntPoint> Points`: lattice points, never ordinals, so they survive any future re-key. Completion re-checks reach and rating, carves through `CarveDeepPoint(L, P)` (column and duplicate checks, drop P from `Plan`, then `AppendCarvedCell`), then zones and furnishes as `CompleteFloorCells` does (`:1201-1238`). Instant growth carving has precedent: `CompleteFloorCells` carves through `ExcavateFloor` at completion (C5b-R).

**Inertness.** No caller sets `Value` (section 0), so no deep district exists: `IsFloor()` answers as today, `DeepOwner` is empty, home cells are never built, the growth dispatch is unchanged.

**Era.** A paint executes at `StepUplink` in both spines; growth records run identically in both bands (C5b-R); home cells are a pure function of saved state.

**Save v49.** A *deep block* after the cavern block: per deep district its Id and points; per deep growth project its DistrictId and points. The v37 and v38 slots stay byte-for-byte. Structural refusals before teardown (`OutError`): counts over caps, a level out of range, a point in two districts. After the districts load (`:14171`, post-teardown), an Id naming no deep district is dropped with a Warning, the house repair pattern, never an Error line.

**Tests (`-deepdistricts`).** Every fixture keeps 60 colonists or fewer (ramp 0), which keeps D6c inert against `deepdistricts.log`. DD1 paint, join, a multi-floor shape, every refusal. DD2 the frame: a shape under an off-grid head maps a building exactly, and `LatticeOf` would not. DD3 home cells: literal slots with pieces, legacy slots at CPC 1 and 0.5, an overfull floor, a departure. DD4 counts, beds, seats. DD5 a whole-floor twin is byte-equal in counts, membership and growth. DD6 `DeepCells` zones free cavern cells, then carves eligible rock, and the ghost equals the carve. DD7 civic stays inside. DD8 an era/agent project finishes on the same step with the same cells. DD9 save round trip with Points, the deep-block refusals, v48 refused. DD10 `-RHVerifyCaches` clean.

## 5. D6c: the deep commute (R2)

**The switch is per floor.** On a mapped floor, residents and seats are measured at their cells. Elsewhere every line of today's commute runs: the `-floor` key, the head anchor, D2's cheapest shaft, D4's split. With no deep district there is no mapped floor, so `-percapita` and `-arrivals` stay on today's lines.

**The walk network, `FRHDeepWalk`,** one per mapped floor, derived, keyed on `FloorCellsVersion`, `TunnelsVersion`, `ShaftsVersion`, `DistrictsVersion` and (D6d) `TramVersion`.
- **Compartments:** 4-connected components of carved cells that are not tunnel chain points. **Tunnel cells** are walked cell by cell, 1,000 cm a step. **Inside a compartment** people walk the octile line: `1000 x max(|dx|,|dy|) + 414 x min(|dx|,|dy|)` cm, in integers.
- **Doors:** a compartment cell 4-adjacent to a carved chain point (1,000 cm through), and *landings* (the lowest-ordinal carved 4-neighbour of a reaching shaft's column, D3 section 2). Door-to-door distances: one integer Dijkstra per door over doors and chain points, ties to the lower node index.
- `W_L(a, b)` = the octile if a and b share a compartment, else the minimum over doors d1 (a's) and d2 (b's) of `oct(a,d1) + DD(d1,d2) + oct(d2,b)`; unconnected is infinite.
- **Why octile, not a Dijkstra per home:** thousands of distinct home cells, tens of doors. Compartments are spiral blobs and cavern boxes, near-convex; the long thin parts (tunnels) are walked exactly. The concave-shape error is R2.

**A trip** (`Up(S, n)` = `n x CommutePerFloorCm(S)` with transit, `n x CommuteShaftCmPerFloor` without):
- **Same floor:** `min(W_L(h,w), Via)`, with `Via` = min over landed shafts S1, S2 of `W(h,land1) + Up(S1,|L|) + Surface(head1,head2) + Up(S2,|L|) + W(land2,w)`.
- **Floor to surface:** min over landed S of `W(h,land_S) + Up(S,|L|) + Surface(head_S, P)`.
- **Floor to floor:** min over S landed on both of `W_Lh(h,land) + Up(S,|dL|) + W_Lw(land,w)`; if none, two shafts via the surface.
- **The choice is wait-free** (ties to the lower Id); `WaitCm` of each shaft used is added after, as D4 2.6, so a crowd never flips routes sol to sol. On mapped floors this replaces D4's round-robin split. `ShaftPeople` gains 1 per colonist per shaft ridden.
- **Off-net:** an unmapped end, a resident with no home cell, or a cell with no door to a landing (D3 R6) keeps the head anchor with no walk (counted in `CommuteOffNet`). No trip is ever left unmeasured.

**Keys.** On a mapped floor the home key is (L, home ordinal), the work key (L, station ordinal or module). `FRHCommuteTrip` (`.h:2451`) gains `HomeCell`, `WorkCell` and a second shaft Id. `FRHCommuteKey` (`.h:2413-2425`) gains `uint32 DeepWalk`, 0 while no floor is mapped, so X13 and X14's rebuild counts cannot move.

**Effects** go only through `bLong`, the ramped penalty and the R2 work hooks. D5's Shelter and Warmth read `HomeLevel` and do not change. **Inertness:** nothing maps a floor without D6b's switch. **Era:** a pure function of shared state (C6); flows equal at the split. **Save v50** is a fence with no field, as C6a's v40: a v49 binary would measure a mapped floor differently.

**Tests (`-deepcommute`).** DC1 hand integers inside one compartment, across a 30-cell tunnel, and between unjoined digs through two shafts. DC2 floor to surface picks walk plus ride. DC3 a crowded shaft does not flip the route next sol. DC4 `ShaftPeople` per shaft. DC5 an off-net cell. DC6 an unmapped floor in the same colony is byte-equal to a no-district twin. DC7 ramp 60, 61, 160. DC8 era equality at the threshold. **-scale:** all 20 floors mapped, 5,000 colonists: cold walk network at most 5 ms, cold commute with transit at most 10 ms (D4's budget).

## 6. D6d: trams with people

**The verb `LayTramLine`** (`Value` = tunnel Id) carries D4's draft section 3.1 without the robots. Refused: (1) no lift car, so D4's one switch stays (`RHSimTransit.cpp:168`); (2) the tunnel is unknown, cancelled or unfinished; (3) the chain is under `TramMinCells` (12); (4) it already has a line; (5) over `MaxTramLines` (8, cap 32); (6) short of Struct, `TramStructKgPerCell` (20) x the chain, through D4's `TakeStructForBill`.

**`FRHTramLine`** (saved): `Id`, `RegionId`, `Level`, `TunnelId`, `StopA`, `StopB` (lattice points), `WaitSec`, `PrevWaitSec`. Its floor becomes mapped.

**The edge** joins the two stop cells in `FRHDeepWalk`; people board only at stops. Uncrowded cost: `(TramHeadwaySec / 2 + ceil(LenCm / TramSpeedCmPerS)) x CommuteWaitCmPerSec` (rows `TramHeadwaySec` 60, `TramSpeedMps` 10). `WaitSec x CommuteWaitCmPerSec` is added only after the route is chosen. A 300 m tunnel walks at 30,000 cm; the tram is (30 + 30) s x 100 = 6,000 cm.

**Capacity.** In `RollTransit`, after the shafts, ascending line Id: `Load = 2 x TramPeople[line] x RampPermille / 1000` (D4 item 8), `Cap = TramPlacesPerSol` (600), `ComputeTransitWaitSec` as for a shaft, `TransitWaitVersion` up on a change. People are world-side: no lag. No car is simulated; the view draws one capsule per line by phase.

**Era:** the same step at the same index in both spines; waits equal at every threshold. **Inertness:** no suite lays a line, so the tram loop is empty. **Save v51:** a tram block after the deep block; the default is 8 bytes (`NextTramId` 1, count 0).

**Tests (`-trams`).** TM1 refusals. TM2 the edge integer. TM3 riders counted by route, ramp 60/61/160. TM4 the wait added after the choice, no oscillation. TM5 a line maps its floor. TM6 save and hand-made refusals. TM7 era equality.

## 7. D6e: lift and tram power

**The load entries.** Rows: `LiftCarW` 200 (per car), `TramWPerCell` 5, `TransitLoadPriority` 6 (transit sheds before the WaterPlant and D5's cooling). `StepPower` appends one entry per fitted shaft (`Cars x LiftCarW`, by Id) and one per tram line, after D5's cooling entries. `FLoadEntry` gains `uint8 Kind` (building 0, cooling 1, lift 2, tram 3). **D5's comparator must change:** it treats any `B == nullptr` as cooling. The new order is Priority, then Kind, then deeper first (cooling) or lower Id (lift, tram). With no transit entry it is D5's sort, element for element.

**Shortage** uses D5's scraping short set, never the per-step shed flag (D5's blocker). A lift or tram entry in the shed prefix on a scraping step adds `RoundToInt64(SubDt x 1000)` ms to its 2-slot ring, tagged by the sol of `GetStepEndSeconds()`. Both steps are world-side and `StepTransit` runs first, so the roll at sol s reads a complete slot s - 1 with no lag.

**The consequence.** `Served = 1000 - min(1000, ShortMs(s-1) / 1200)`. A shaft's capacity is `Hoist + (Cars x LiftPlacesPerSol - Hoist) x Served / 1000` (int64); a line's is `TramPlacesPerSol x Served / 1000`. A dark sol raises the next sol's waits. Ride speed, ratings and arrivals do not change.

**Inertness, honestly.** With no car and no line nothing is appended. But cars exist in `-transit`, the `-scale` transit block and the `lifts=1` playtest.
- `-transit` pins the bank to 1e9 before every step: `EraOnce` and `AgentOnce` (`RHSimTransitSuite.cpp:76`, `:82`) and the batch drivers (`:463`, `:529`); the jump step at `:435` follows a pinned step. So no step scrapes, `Served` is 1000, and capacities are D4's exactly. `RequestChargeWh` takes from the bank before it tests `GenW > LoadW` (`:12673-12679`), so pad charging is unchanged. No `transit.log` line prints `LoadW`.
- The builder greps `depth.log` for any case that fits a car and prints `LoadW`, `ShedCount` or Warmth. Each is named in the commit, or its fixture pins the bank. `lifts=1` is outside the byte-identical set; its assertions must still pass.

**Era.** D5's bank-cycle argument carries: `|dServed| <= 2 x 60,000 / 1200 = 100` per mille, under D5 R2's two fixture conditions (a daily reset, a dusk onset within 120 s). **Save v52:** a power block after the tram block, per fitted shaft and line the two (sol, ms) slots; the default is 4 bytes.

**Tests (`-transitpower`).** TP1 the entries, their order, a no-transit sort equal to D5's. TP2 a night on the Lander alone (D5 DP6's fixture) gives `Served` 0 and hoist capacity. TP3 a (ii) scraping step counts as short. TP4 the era tolerance. TP5 save mid-sol. TP6 `transit.log` line-identical.

## 8. Item 3: per-dig reach

**Recommendation: never re-key.** D2's REVISION costs it at nine stores, a merge rule, and moved `-deep` K5 and `-shafts` SH11b lines. After D6b and D6c, digs are already places for people (shapes, home cells, real walks). What is left is one effect: a dig with no filter of its own rates through its floor, which matches the trunk carrying the grid down (`.h:608-609`). Q-A offers a derived middle option and the full re-key.

## 9. Item 7: the O1 re-key question

No D6 operation removes or reorders a cell. Caverns and growth append through `AppendCarvedCell`; repaints and erases touch only district points; home cells and the walk network are derived; trams key on a tunnel Id and lattice stops. So D1 R0's append-only argument holds. Each sub-gate's suite asserts ordinal-to-point across a save and load, and `FloorCellFallbacks == 0`. **Forward note for D7:** an atrium must flag cells open, never delete them, or D7 becomes the re-key gate (rooms, furniture, seats, planted cells, queued orders, growth `Index`).

## 10. Era parity, in one place

Nothing in D6 depends on the band. Every sim step involved runs in both spines (`StepProduction`, `StepCityGrowth`, `StepUplink`, `StepTransit`, `StepPower`); the commute is a pure function of saved state. Era logistics still teleport past lifts and trams (the C4/D2/D4 deviation, extended). No new era refusal.

## 11. Where this draft differs from `d6-caverns-design.md`

1. **That draft's cavern work item `(4, CavernId)` is caught by the tunnel branch.** Completion sends `Work->X >= 2` to `CompleteTunnelCell` (`:1524`), and D5 prices any `X >= 2` at a tunnel's level ((wt) `:1624-1625`). Its item also does not name a floor, so a batch started on -3 can append on -4 at -3's price. This draft encodes the floor (`X = 100 + depth`) and tests it first.
2. **That draft's refusal 5 needs a carved neighbour or a column on *every* floor of the box.** So a three-floor cavern needs three existing digs. This draft lets a cavern grow down from its roof.
3. **That draft's people net runs a Dijkstra per distinct work cell over every mapped cell.** With 5,000 colonists that is hundreds of floor-sized runs. This draft costs one Dijkstra per door (doors number in the tens) and walks compartments by octile.
4. That draft puts shapes before caverns, adds cavern stairs for people, takes no save bump in D6c, and forces transit power to 0 in `-transit` with a debug scale. This draft puts caverns first, defers stairs to D7 (atriums), fences D6c at v50, and argues `-transit` stays identical because its bank is pinned.

## Director questions

**Q-A. Should each separate dig have its own air and rating?**
- **a (recommended): never.** A floor keeps one air supply and one Livable rating. Shapes, home cells and real walks make digs into places for people. No save re-key.
- b: a derived check, with no re-key. A home or seat in a dig needs a powered air circulator in that dig, and the dig needs `MinLivableCells`. It is built into D6b. The pinned `-shafts` SH11b deviation line changes on purpose.
- c: full per-dig air and rating, merged when a tunnel joins two digs. A new gate between D6b and D6c: two to three sessions, nine stores re-keyed, the battery at risk.

**Q-B. How big and how costly is a cavern?**
- **a (recommended): up to 8 x 8 cells and 3 floors (192 cells), carved with the ordinary 3 h `CarveCell`, with no other bill.** A full one is 576 Borer-hours (24 sols for one Borer), 230 t of spoil and 19.2 t of O2 to fill.
- b: up to 12 x 12 x 3 (432 cells) with a bulk row at 1.5 h a cell: 27 Borer-sols.
- c: (a) plus a Struct support bill per cell (50 kg, placeholder).

**Q-C. Who rides trams?**
- **a (recommended): people only.** A tram is a fast, capacity-limited link in the commute. Robots keep to tunnels and lifts. Everything stays world-side.
- b: people and robots. D4's draft robot tram (holds, rides, the reserve priced at `MaxWaitSec`), in the riskiest code in step 2. One to two more sessions.

## Risks I see

- **R1. Two lattice frames.** Deep shapes use the head frame and surface districts the world grid. A mix-up compiles. DD2 pins it, and every deep read goes through `CmToLattice`.
- **R2. Octile inside a compartment** undercounts a concave painted shape. Spirals and caverns are near-convex. Tunnels are exact.
- **R3. Home cells move** when an earlier-Id resident leaves, as seats already do. Commutes can change for people who did nothing. It is deterministic.
- **R4. The cliff at the first deep shape.** Painting one changes that floor's commutes at once. The alert must say so (Gate-D placeholder).
- **R5. Cavern scale.** 192 cells of fill can pull a rated floor's O2 under 98% while it is being dug. The rating holds while the circulator is net-filling (`:2872`), but a weak filter will flap. The ghost shows the O2 bill.
- **R6. D6e depends on `-transit`'s bank pins.** If a future test stops pinning, power enters `transit.log`. TP6 guards it.
- **R7. People and robots use different networks.** People walk `FRHDeepWalk`; robots use D4's `FRHDeepNet`. The traffic overlay and the crew visual can disagree. Logged.
- **R8. The `X >= 100` encoding** uses one integer for two meanings. The cap of 30 floors keeps it under 131. CV6 pins the order of the branches.
- **R9. Anchors move with every sub-gate after D5.** Each sub-gate re-anchors and recounts its version lines.
- **R10. Five save bumps.** The hand-play checkpoints re-make themselves at each one, as at every bump since v36.

## Deviations to log in `docs/design-decisions.md`

1. Per-dig reach is never built, under Q-A (a).
2. Caverns have no vertical link in D6; stairs and open volumes come with D7.
3. On mapped floors the commute takes the cheapest route, not D4's round-robin split.
4. Trams carry people only, under Q-C (a).
5. Lift power cuts capacity, not ride speed.
6. Era teleports past trams too.
