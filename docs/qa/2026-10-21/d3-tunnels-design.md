# D3, tunnels: design (2026-10-21, draft for adversarial review)

Step 2 (going deep), gate 3. Design only. No code is written.
Anchors are read at HEAD `94d0afa` (D2). The tree is clean, so HEAD and the working tree agree.
Sources: `going-deep-plan.md` (D3 row, D3 risks, appendix "Robots never go below"; Q1a Q2a Q4a); the D1 and D2 REVISIONs (O1 ordinals, O9, floor-wide reach, R2); the C4 REVISION (house rules); C6 (rovers, commute); W1 M-sections (`IsRobotLive`, freight); fix A (`-eraparity`).

**Goal.** Painted tunnels join separate digs. Robots ride a shaft's lift, walk the floor, and deliver to the room that needs it (Q1a). One planner joins C4's surface graph, the lifts and every floor.
**What you see.** You fit a robot lift on a shaft. Haulers and Fabricators ride down and work in the rooms. You paint a tunnel from dig A to dig B; the Borer carves it; the two digs become one.

**Decisions at a glance.**
- A tunnel is a painted line of ordinary carved lattice cells (Q2a). The floor's walk graph is its carved cells, derived. `FRHRoadEdge` is not extended.
- A lift is a set of vertical portal edges. Robots ride only shafts the player has fitted with a robot lift (the inertness gate).
- A ride is a fixed number of whole sub-steps. The robot's level changes only at the end of the sub-step that ends the ride.
- No cargo throughput cap in D3. Capacity comes in D4, from flows, for rides and hand-offs alike (Q-B).
- The instant hand-off at the head stays as the fallback for anything no fitted lift reaches.
- Rovers, excavators and scouts stay on the surface. The commute numbers do not change (R2 goes to D6).

---

## 0. Facts the design rests on

- **The robot level is a dead slot.** `FRHRobotSaveState::Level` exists ("dormant until the shaft lets robots descend", `RHAgentSubsystem.h:18-20`). It is serialized (`RHSimWorldSubsystem.cpp:12405`, `:12906`). But `CollectRobotStates` never writes it (`RHAgentSubsystem.cpp:204-215`), and load spawns from `PosCm` only (`:12909`). It is 0 in every save ever written.
- **Z already carries level for buildings.** `Instance.LocationCm.Z = Level * FloorHeightCm; // Z is derived` (`:9528`). Robots spawn at Z 50 cm (`:453`, `RHSimRovers.cpp:322`). `MoveToward` is 2D and never writes Z (`RHRobotMovement.h:20-26`). `MoveAlongRoads` writes X and Y only (`RHSimRoads.cpp:404-413`).
- **Both brains plan to the surface.** Builds target `GetApproachPoint(Board.To, 0)` (`RHRobotStateTree.cpp:115`, `RHRobotTaskProcessor.cpp:199`). Haul drops come from `HaulLoad`, which also asks for level 0 (`:11704-11706`).
- **The instant cargo lift is `HaulUnload`.** It credits the `To` building's hopper wherever it stands (`:11714-11717`). The site hopper takes sources on any linked floor (`:1298-1303`). Pickups are same-level only (`:11640`). Fabricators claim cross-level sites and work them from the head (`:11755-11757`).
- **Charging and repair are surface-shaped.** Pads match `B.Level == RobotLevel` (`:11841`). Repair picks the most worn robot anywhere (`:12076-12083`) and drives straight at it (`RHRobotStateTree.cpp:157-176`).
- **The battery ticks no Mass robot.** Harness robots are played through the brains' own calls (`RHSimCommandlet.cpp:11112-11150` EP4; the rover harness `:10152-10187` drives `RH::MoveAlong` on a harness `FTransform`). The 110-sol playtest is the only full robot run.
- **Floors are shared lattices with derived digs.** `EnsureDigInfo` labels 4-connected digs and which shafts touch them, cached on (`FloorCellsVersion`, `ShaftsVersion`) (`:5187-5251`). `AppendCarvedCell` is the only writer of `Cells` (`:5050`).
- **Underground placement does not need a cell,** except for station modules (`:7898-7905`). An AirFilter can stand in rock.

## 1. Tunnels

**Data: neither a Level on `FRHRoadEdge` nor a second road graph.** A tunnel is cells.
- Why not a Level on the C4 edge:
  - `FRHRoadGraph::NodeAt` is keyed by a 2D grid point (`RHRoads.h:146`). Its junction rules split and merge on equal 2D cells (`:105-112`). A tunnel under a road would join it.
  - `HasBuiltRoads()` (`RHSimWorldSubsystem.h:1370`) gates every surface robot's movement (`RHSimRoads.cpp:40-44`). A built tunnel would switch the whole surface fleet onto `MoveAlongRoads`.
  - The commute memo keys on `RoadGraph.BuiltVersion` (`RHSimCommute.cpp:74`). `CanPlaceRoad` checks surface coverage and surface footprints (`RHSimRoads.cpp:213-265`).
  - Every one of these would need a level filter. A missed filter is a bug that compiles: a surface robot driving "through" a tunnel.
- Why not a separate per-floor `FRHRoadGraph`: underground, there is no "off-road". A straight leg from a node to a site can cross rock. Movement must stay inside carved cells, so the cells must be the graph.
- So: **the underground road graph of a floor is its carved cells** (derived). Nodes are cells. Edges join 4-neighbours (1000 cm) and diagonals (1414 cm) whose two orthogonal cells are both carved. Rooms and tunnels are walked alike.
- **Plan deviation, to log:** the plan's D3 risk line says "the tunnel edges (in the road graph, with a Level)". D2 already argued the shared lattice for this exact case ("a join is just two cells becoming neighbours").

**`FRHTunnel`** (new, saved): `Id`, `RegionId` (0), `Level`, `A`, `B` (lattice points), `bTwoEnded`, `bCancelled`. The chain is derived: an L from A, along X to (B.X, A.Y), then along Y to B. Literal and deterministic.

**The order.** New verb `DigTunnel`: `Level`, `Location` = A, `LocationB` = B (the C4 field, saved since v36). Snapped with `CmToLattice(origin, ·)` (`.h:559`). The preflight runs again at execute time. Refusals, each with a reason:
1. The level is not reached by any shaft.
2. The chain is over `TunnelMaxCells` (64), or under 2.
3. A is carved, or no 4-neighbour of A is carved ("start a tunnel at the edge of a dig").
4. Any chain point is carved ("a tunnel runs through rock").
5. Any chain point is a shaft column.
6. Any chain point is an uncarved point of another open tunnel. Crossings are refused; T-joins work by starting beside a carved tunnel cell.
7. More than `MaxOpenTunnels` (8) open.
- `bTwoEnded` is set when B has a carved 4-neighbour. Then B's end is carved too. Otherwise the tunnel is a dead end, carved from A.
- No bill. The cost is Borer work, like Excavate.
- New verb `CancelTunnel` (`Value` = Id): no new batch starts; carved cells stay; a batch in flight finishes its cell. It bounds the era lock (section 5).

**Carving: Borer work, spoil out.**
- Each cell is one `CarveCell` batch: 3 h, 500 W or H2, 1,200 kg regolith spoil dropped at the Borer (`RH_Recipes.csv:10`). Same cell, same recipe. A separate `CarveTunnel` row is a later tuning choice, not a design change.
- **The work loop** (`:1491-1530`): after bore, after the carve queue, and only when both found nothing, the Borer of shaft S scans open tunnels by Id on levels S reaches.
  - It takes the A front if S serves it: `ServingShaft(L, the lowest-ordinal carved 4-neighbour of the first uncarved chain point from A)`. Helpers count as shaft 0, as in D2.
  - Otherwise the B front, under the same test, if `bTwoEnded`.
  - It starts only while the uncarved points outnumber that tunnel's batches in flight.
  - The work item is `(2, Id)` for the A end or `(3, Id)` for the B end. `PendingBoreWork` keeps its `FIntPoint` format. The bore-in-flight scan reads `X == 0` only, so it is unchanged.
- **Completion** (`:1432-1475`): new branches before the carve `else`. It appends the first uncarved chain point from that end, through `AppendCarvedCell`. The end is fixed at batch start, so a shaft sunk mid-batch cannot move it (D2's R5 trap).
- **Invariant:** every cell tunnel work appends is 4-adjacent to a cell carved before it. The front advances from carved rock. Asserted after every append in `-tunnels`.
- A dig's spiral or a painted plan may carve a chain point first. The front then skips it. `CarveNextCell` (`:5080-5132`) is untouched.

**How tunnels relate to carved cells.**
- Tunnel cells are ordinary cells in `FRHFloor::Cells`: appended, ordinal-keyed, never removed. D1's O1 keys and the append-only argument hold unchanged.
- **O9 stays.** SinkShaft refusal 8 gains "an uncarved point of an open tunnel". Otherwise a column could land mid-chain, and the tunnel could never finish.
- When the chain completes, `EnsureDigInfo` merges the two digs. `Touching` then lists both shafts. That is the moment "two digs become one city".
- **Consequence, stated plainly:** a tunnel cell counts like any cell for air fill and leak, legacy beds (`:10948-10953`), `MinLivableCells` (`:2762`) and zoning. It costs exactly what a room cell costs, so this is no cheap-bed loophole. But a long tunnel on a rated floor raises the O2 fill (risk R3).

## 2. Lift edges

**The robot lift.** New verb `FitLift`, `Value` = shaft Id.
- Refused for an unknown shaft, Depth 0, or already fitted.
- Its bill is `LiftFitStructKg` (400 kg Struct, placeholder), taken atomically at the uplink, with the C4 R1 pattern (`TakeRoadStruct`, `RHSimRoads.cpp:75-109`, generalized).
- Instant: no site and no labour, so no new site kind and no era refusal.
- Saved as a list of fitted shaft Ids. No removal in D3. `HasRobotLifts()` is "any shaft fitted". `LiftVersion` goes up on a fit.
- People are not affected. Colonists already use every shaft (`PickCommuteShaft`, `RHSimCommute.cpp:170-191`).

**Portals.**
- Each fitted shaft S has a surface portal at `HeadCm`.
- It has a floor portal on each level L it reaches **where one of the column's four orthogonal neighbours is carved**. The landing is the lowest-ordinal such neighbour.
- A dig touched only diagonally (D2's 8-cell touch) has no landing. Robots cannot step out there. D2's `ServingShaft` still serves it for the fallback.

**Edges and costs.** One integer unit for the whole network: C4's `CostUnits` (`RHRoads.h:64`: `SpeedMulPermille x off-road cm + 1000 x road cm`).
- Surface legs: C4's `PlanRoute` cost, else `SpeedMulPermille x RoundSqrtCm`.
- Floor legs: `SpeedMulPermille x` lattice cm.
- A ride from L1 to L2 on S costs `SpeedMulPermille x LiftPlanSpeedCmPerS x (LiftDwellSeconds + |L1 - L2| x LiftSecondsPerFloor)`.
  - Rows (placeholders): `LiftDwellSeconds` 10, `LiftSecondsPerFloor` 4 (4 m floors at 1 m/s), `LiftPlanSpeedMps` 4.0 (the hauler's speed).
  - A ride edge joins every pair of S's portal levels, so the dwell is paid once per ride, not per floor.
- **Time per floor**, as driven: `RideSubSteps = 10 x (LiftDwellSeconds + floors x LiftSecondsPerFloor)`. Both rows are whole seconds and `SubStepSeconds` is 0.1 (`RHSimClockSubsystem.h:23`), so this is exact integer arithmetic. A one-floor ride is 140 sub-steps.

**`PlanDeepRoute(FromL, FromCm, ToL, ToCm, Out)`.**
- One Dijkstra over portal nodes. Sources: the start floor's Dijkstra from the robot's cell to each landing, or the surface cost to each head. Targets: the same from the target. Plus the direct floor leg when both ends share a dig.
- Ties: lower cost, then lower portal index (shaft Id, then level). Predecessors change only on a strict improvement, as in C4.
- The robot's cell is `FindCarvedCellAt(L, Pos)`. A robot standing on a column (just off a ride) starts at that portal. Anything else is counted in `DeepOffLattice`, and `-tunnels` asserts it stays 0.
- **Derived cache `FRHDeepNet`, never saved.** Keyed on (`FloorCellsVersion`, `ShaftsVersion`, `LiftVersion`, `RoadGraph.BuiltVersion`). It holds one lattice distance field per landing and the portal-to-portal costs (surface pairs through `DistancesFrom`, the C6a pattern). Rebuilt lazily. It is a pure function of sim state, so which robot triggers the rebuild cannot matter.

**Capacity (Q1a): decided, no throughput cap in D3.** Director question Q-B.
- Any cap shared between robots is decided in processor order under frame-batched stepping. A robot processed later, which arrived earlier in sim time, loses the seat. That is frame-schedule dependent.
- A cap on fitted lifts alone makes the uncapped instant hand-off on unfitted shafts strictly better, so no player would fit one.
- D4's rule is "lift load from the flows (sim state), never from robot positions". It can charge cargo (completed cross-level hauls per shaft per sol) and people with one wait, on rides and hand-offs alike.
- So in D3 a ride is a fixed duration, independent of every other robot. No new state is shared between robots: the only one is the claim, as today.

## 3. Robots below

**The level.** A robot's level is `RH::LevelOfZ(Z) = RoundToInt(Z / FloorHeightCm)`. It is written only at a ride's end: `Z = L x FloorHeightCm + 50`, which keeps the spawn offset. It is never truncated. No new fragment, so the explicit StateTree archetype is untouched (C4 item 4). `FRHTaskFragment` resets cannot clobber it.

**The fixed sub-step point.**
- A ride leg starts when the robot reaches the column XY. Boarding ends that sub-step, and the leftover time is dropped.
- Each later sub-step counts one off `RideStepsLeft`. Z flips at the **end** of the sub-step that reaches 0, and the robot moves no further in it. The next sub-step walks to the landing.
- It is robot-local, so it does not depend on frame batching. Claims and pad searches always read the level at the start of a sub-step.
- A ride draws `DrawMoveW` (the brain's movement draw), which is conservative.

**Both brains route through `RH::MoveAlong`.**
- New last parameter `TargetLevel = 0`. New first line: if `!bRover && HasRobotLifts() && (LevelOfZ(Z) != 0 || TargetLevel != 0)`, call `MoveAlongDeep`. Otherwise run today's body verbatim (`RHSimRoads.cpp:29-44`).
- `FRHRobotRoadPath` (`.h:93-105`, runtime only, in `RobotPaths`) gains a level per waypoint and ride legs (`LegEdge < 0` = shaft `-LegEdge - 1`), plus `RideStepsLeft`.
- Surface legs inside a deep trip use C4's waypoints and live edge speed. There is no mid-trip re-plan (C4 item 5): cells and fittings are never removed, so a planned deep path stays valid.
- `FRHTaskFragment` gains `int32 TargetLevel`, set beside every `TargetCm` write. Repair still uses `MoveToward` for same-level patients.

**Who goes below.**

| Class | Below? | Why |
|---|---|---|
| Hauler | yes | pickups and drops on any route-reachable level |
| Fabricator | yes | builds at the site's cell (Q1a) |
| Maintenance | only to a **halted** robot below | a halted patient does not move, so one plan suffices; worn-but-working robots surface to charge anyway |
| Excavator, Scout | no | deposits and surveys are level 0 (`:11611`) |
| Rover | no | freight, pads and the last-leg rule are surface rules (`RHSimRovers.cpp:50`, `:68`, `:83-117`); W1's outpost rules ride on them. **Deviation** from the plan's "rovers go down"; it belongs with trams in D4. |

**Claims.** Pass the real level to `TryClaimHaul`/`TryClaimBuild` (the parameter exists, `.h:1688`, `:1699`).
- When `HasRobotLifts()`, a cross-level candidate is eligible if **route-reachable**: each underground end lies in a dig with a fitted landing, or both ends share a dig. That is O(1) on cached labels.
- Ranking key: `DistXY + |dL| x LiftPlanCm`, ties to task order. With no lift the set is empty, and the scan is today's.
- A robot below never claims what it cannot reach. So the fallback below is only ever taken from the surface, with `GetApproachPoint(…, 0)` exactly as today (SH1 pins it).

**Drops and builds.**
- A new `GetRobotTarget(Site, RobotLevel, OutCm, OutLevel)` returns the site itself, at its level, when it is route-reachable. Otherwise `GetApproachPoint` at the robot's level.
- `HaulLoad` gains `int32* OutDropLevel = nullptr`, so the harness calls are unchanged.
- `HaulUnload` is unchanged. Delivered at the room, it credits the hopper exactly as at the head.
- **The instant hand-off is retired** for every site a fitted lift reaches. It is **kept** for sites in rock, digs with no fitted landing, and colonies with no lift.

**Charging below.**
- Pads are surface only in D3 (`FindNearestChargePad`, level 0). A pad built on a floor does nothing, as today.
- **The reserve.** A robot below needs `DriveWh(lattice cm to the landing + 1000 + straight head-to-pad cm) + DrawMoveW x RideSeconds / 50 x 1.25`. It takes the minimum over the fitted landings in its dig, an O(shafts) lookup in `FRHDeepNet`'s fields.
  - The StateTree condition (`RHRobotStateTree.cpp:443-447`) adds this branch for level < 0.
  - The legacy seek test (`RHRobotTaskProcessor.cpp:77`) adds the same helper.
  - Both branches are untaken at level 0.
- The mid-delivery exception (`:456-467`) applies only to surface-to-surface drops. A hauler below that turns back loses up to 200 kg, today's hauler rule.
- **Invariant:** every robot below has a route up. It reached its dig through a fitted landing, and cells and fittings only grow. Asserted.

## 4. Inertness

**The gate:** robots go below only through a lift the player fits. A bored shaft alone is not enough. A tunnel is not required: one fitted shaft over one dig already sends robots down. With no `FitLift` order, nothing below changes. No suite and no playtest bot issues one.
- `MoveAlong`: the gate's else branch is today's body.
- Claims: the cross-level eligible set is empty, so today's scan runs.
- `HaulLoad`: `OutDropLevel` is 0 and the drop is `GetApproachPoint(…, 0)`.
- The Borer loop: the tunnel branch runs only after bore and carve found nothing, and with a tunnel present.
- `CanEnterEraMode`, SinkShaft refusal 8, the charge reserve, repair, the visualizer: each new branch tests a tunnel, a lift or a level below 0.
- Save: every robot writes `Level = RoundToInt(50 / 400) = 0`, the same bytes as now. The two new counts add 8 bytes, which the "(N bytes" mask hides.
- No posting rule changes. `StepTaskBoard` posts the same tasks in both bands. Only who may claim them, and how they travel, changes.

**Why not "any reached floor is a place, so robots ride"?**
- Every vault build would change: Fabricators would walk to the cell, and haulers would ride, instead of the instant hand-off. The battery barely notices, because it ticks no Mass robot. But `-shafts` SH9 and the 110-sol playtest would move everywhere, and the baselines would need a re-cut.
- That is a director's balance call (Q-A), not a refactor.

**The bar:** 48 runs (`-tunnels` added), 0 not green. Line-identical to `Saved/baselines/battery_post_d2` except the 8 save-version lines (8 out, 8 in): `traffic.log:937-938`, `deep.log:590`, `:591`, `:596`, `framing.log:582`, `farice.log:7075`, `shafts.log:1412`. Recount them at build time. The 110-sol playtest must be byte-identical.

## 5. Era parity

- **Era logistics keep teleporting,** same-level as now (`EraLogistics`, `:748` onward). They ignore lifts, tunnels and robot levels. Log this as a deviation, as C4 and D2 did.
- **Refuse era while a tunnel is open:** "tunnel carving in progress (tunnel #n, k cells left)", after the road loop (`:617-628`).
  - Honest note: carving is `StepProduction` work and runs identically in both bands (`EraStep :718-746`). The refusal is not needed for the ledger. It mirrors roads (C4 R2), as the plan asks.
  - It costs era time: a 30-cell one-Borer tunnel is 90 h, about 3.75 sols. `CancelTunnel` bounds the lock. Dropping the refusal is the professor's call; nothing else depends on it.
- **Robots below park in era where they stand.** The processors return on `Steps == 0` (`RHRobotBrainProcessor.cpp:46-50`).
- **The 5% bar with robots below.** The agent band pays rides and lattice walks; era pays nothing. Construction already refuses era, and no underground solid flow exists in steady state. So the gap is bounded by one ride per robot trip. TN17 measures it with the fix-A method.

## 6. The commute

- **Deferred, and a deviation from the plan's D3 row.** The commute's surface legs already plan on C4's graph, which is the surface layer of the joined network. The underground layer cannot change a number yet:
  - A floor home is only `HomeLevel`, and the trip keys on `-floor` (`RHSimCommute.cpp:257`).
  - Both floor anchors sit at a head (`:285`, `:291`).
  - A floor anchor is "the whole floor", so the cost from a floor is the minimum over reaching heads at zero cost. That is `PickCommuteShaft` exactly. A tunnel can only lengthen a head-to-head path.
- **R2 (a 0 m commute between digs on one floor) is deferred to D6,** where floor homes and seats get a cell. D4's lift waits attach to the lift edges defined here.
- **Inert under the 60-160 ramp.** `MakeCommuteKey` (`:49-63`) and every commute input are untouched. Commute numbers are identical at every population. TN18 pins it.

## 7. Save: v45, strict

- **Version.** `RHSaveVersion` 44 becomes 45 (`:36`), with the comment chain extended.
- **The new block** goes straight after `WriteShaftBlock` (`:12345`), so K6's offset 20 and SH12's `BlockAt` do not move.
- **Layout:** `int32 LiftCount`, then the fitted shaft Ids ascending. Then `int32 NextTunnelId` and `int32 TunnelCount`, and per tunnel: `Id`, `RegionId`, `Level`, `A.X`, `A.Y`, `B.X`, `B.Y`, `bTwoEnded`, `bCancelled`.
- **Load.** It reads into locals right after `ReadShaftBlock` (`:12783`), before teardown. Structural refusals use `OutError` "Save tunnel list invalid: …", with no Error line:
  - a lift naming no shaft, a repeated lift, or a lift on a Depth-0 shaft;
  - tunnel Ids not ascending or not below `NextTunnelId`;
  - `RegionId != 0`, or a level outside [-MaxDepth, -1] or reached by no shaft;
  - a chain too long, or a chain point on a column;
  - an A neither carved nor beside a carved cell;
  - two open tunnels sharing an uncarved point.
- **Warning repairs** (robots and buildings load later):
  - A work record `(2|3, Id)` naming no tunnel completes with spoil and no cell.
  - A robot whose `Level` disagrees with its Z gets `Z = Level x FloorH + 50`.
  - A robot below on no carved cell respawns at the surface deploy ring.
- **Robots.** `CollectRobotStates` writes `Level = LevelOfZ(Z)`. Path state stays unsaved (C4 R8). A robot saved mid-ride re-plans from its column after load and boards again. In-flight cargo still returns to its source at save (`:12294-12320`).

## 8. Presentation (RedHope; const accessors only)

- **Robots on their floor.** `SetSliceHidden(!bSurface)` (`RHColonyVisualizerSubsystem.cpp:4484`, `RHAgentVisualizerSubsystem.cpp:865-903`) becomes `SetViewLevel(L)`: a robot shows when `LevelOfZ(SimPos.Z) == L`. With every robot at level 0 that is today's behaviour exactly. Terrain Z (`:1214-1217`) applies at level 0 only. Deep legs are drawn by arc length on the sim's waypoints (C4 item 9).
- **The cab.** D2's per-shaft cab (`RHColonyVisualizerSubsystem.cpp:3144-3180`) also wakes for robot riders. A rider is drawn inside it at a Z interpolated by `GetRobotRide(Entity, Shaft, Frac, FromL, ToL)`. With several riders on one shaft (no contention in D3), the cab follows the lowest-index rider and hides the others: a placeholder until D4 queues them.
- **Tunnel strips.** On the floor view an open tunnel's uncarved chain is a dashed ghost, and carved chain cells get a centre-stripe decal.
  - The Q2a "Tunnel" brush drags A to B. It shows the L ghost, the cell count, Borer-hours, spoil and the live preflight reason.
  - The tunnel card has Cancel. The shaft card has "Fit robot lift (400 kg Struct)".
- **Cross-floor visibility.** The surface view shows a badge at each fitted head ("2 robots below"). The fleet panel gains a Level column (`FRHRobotPanelRow`, `RHAgentSubsystem.h:32-39`). The section view is D8.
- New strings are Gate-D placeholder rows, as in D2's H1-H7.
- **Captures** (`Saved/RHCapture/d3`): one shaft with no lift, before and after (identical); a hauler in the cab; floor -1 with a tunnel joining two digs.

## 9. Tests: new fails-loudly `-tunnels` suite (48 runs)

Robots are played through the brains' own calls on harness `FTransform`s with real `RH::MoveAlong`, as C6a's rover harness does. There is no distance timer, so rides and lattice walks are what gets measured.

- **TN1 inert.** One shaft, no lift, no tunnel. `GetApproachPoint`, the `HaulLoad` drop, `TryClaim*` results and `MoveAlong` sub-step counts match a twin with the D3 branches forced off, bit for bit.
- **TN2 refusals,** one per item: `DigTunnel` 1-7, `FitLift` (unknown shaft, Depth 0, fitted twice, short of Struct), SinkShaft on an open chain point. Each checks its reason text.
- **TN3 chain.** Literal L cells for (2,0)->(6,3); X first.
- **TN4 carve.** One Borer carves from A in order. The adjacency invariant holds after each append, with 1,200 kg spoil per cell at the Borer and 3 h batches.
- **TN5 two ends.** Borers of shafts 0 and 1 meet. The digs merge (`Touching` = {0, 1}). No cell is appended twice; no batch completes empty.
- **TN6 interleaving.** A spiral and a plan point land on the chain first: the front skips them. A shaft sunk mid-batch does not move the end.
- **TN7 era.** Refused while open; allowed after completion and after a cancel. A batch in flight at cancel still lands its cell.
- **TN8 planner.** Hand-computed integer costs: surface to -1 in the same dig; surface to -2 through shaft 0 and a tunnel into dig B; a tie broken to the lower shaft Id; unreachable (rock, unfitted dig) gives false. The same route twice, and again after a save and load.
- **TN9 the ride.** The sub-step count is exactly walk + 140. Z flips at the end of the last ride sub-step; the robot does not move in that sub-step.
- **TN10 haul below.** A hauler drops at a Lab module in dig B on -2 (not the head), via a ride and the tunnel. The hopper is credited once.
- **TN11 build below.** A Fabricator reaches the site cell, and `ApplyBuildWork` runs there.
- **TN12 fallback.** An AirFilter in rock and a site in an unfitted dig both get today's head hand-off. `FloorCellFallbacks == 0`, `DeepOffLattice == 0`.
- **TN13 claims.** A robot below never claims an unreachable task. Ranking follows the integer key; ties go to task order.
- **TN14 charge.** A hauler sent from the farthest cell of -2 at its reserve reaches the surface pad with charge > 0, in both brains. A robot below can always reach a landing, checked after 50 random carves and tunnels.
- **TN15 repair.** An RC-M rides to a halted robot on -1 and repairs it. A worn but working robot below is not claimed.
- **TN16 save.** A robot below round-trips its level and Z. A robot saved mid-ride arrives after load. Tunnels and lifts round-trip. Hand-made refusals: a chain on a column, a lift on an unbored shaft, the previous version.
- **TN17 era parity** (fix A's EP4 method). The agent leg's hauler and Fabricator start on -1 in dig B, shaft 0 is fitted and a tunnel joins A and B; the excavators run as in EP4. Dug, Struct made and Lander regolith agree within 5% over 10 sols.
- **TN18 commute.** 160 colonists with homes and seats in both digs: the commute digest and Hope Total with tunnels and lifts equal the twin without them.

**-scale** (outside the battery): 4 shafts x 5 floors x 210 cells, all lifts fitted, 8 tunnels, 100 robots below.
- Budgets (about 20x a quiet run, measured as C6 did): a cold `FRHDeepNet` at most 2 ms; 2,500 `PlanDeepRoute` calls at most 10 ms; 10,000 reserve lookups at most 1 ms.
- The one-shaft numbers do not move.

## 10. Risks I see

- **R1. Frame batching.** No new state is shared between robots, which is why capacity waits for D4. But the level read inside a frame lags sim time, exactly like claims today.
- **R2. The gate hides Q1a by default.** A colony that never fits a lift keeps the instant hand-off. That is the point for the battery; for the player it is one more verb. Q-A.
- **R3. Tunnel cells are rooms' cells.** They take air fill and leak and give legacy beds. A 30-cell tunnel on a rated floor needs 3,000 kg more O2 fill. The cost is a placeholder; a `CarveTunnel` row is the tuning knob.
- **R4. The planner uses one reference speed** (`LiftPlanSpeedMps`) for the ride cost. A 2.5 m/s Fabricator may take a slightly worse shaft. The choice stays deterministic, and the reserve uses real seconds.
- **R5. Two delivery regimes on one floor.** A site in a cell is ridden to; a site in rock is teleported. D6 should require cells for every floor building.
- **R6. Diagonal-only digs have no landing.** D2's touch rule is 8-cell; a landing needs one of 4. A painted dig can be served (fallback) yet not ridable. TN12 pins it.
- **R7. The era refusal costs real play time** and protects no ledger line (section 5).
- **R8. Mid-delivery below.** A hauler turning back below loses its load (up to 200 kg): today's rule, more often.
- **R9. Robot Z now means level.** Any future code that writes a robot's Z from a target (whose Z can be terrain or level) silently teleports it. Review must grep every `SetLocation` on a robot transform.
- **R10. Rides stack visually.** With no contention, five robots can "share" one cab; four are hidden.
- **R11. Deep plans per target** cost a floor Dijkstra on 1,000-cell floors. -scale sets the budget.
- **R12. Plan deviations to log** in `docs/design-decisions.md`: tunnels are cells, not road edges; rovers stay up; the commute ignores tunnels until D6; era teleports past lifts; no cargo cap until D4 (if Q-B is a).

## Director questions

**Q-A. When do robots start riding the lifts?**
- **a (recommended):** only on shafts you fit with a robot lift (400 kg Struct, placeholder). Everything else stays as today, and the battery and playtest stay identical.
- b: every bored shaft carries robots from day one. Q1a then shows at once, but every vault build changes pace, and the playtest and baselines must be re-cut.

**Q-B. The lift's cargo capacity (Q1a).**
- **a (recommended):** none in D3. D4 gives each shaft one capacity, worked out from the flows, for people and cargo, for rides and hand-offs alike.
- b: a per-sol kg budget per fitted lift now, taken at boarding, with robots waiting at the head. This caps only fitted lifts, so the uncapped hand-off on an unfitted shaft beats it. It also makes boarding order depend on the frame schedule.

---

# PROFESSOR REVIEW (2026-10-21)

**Verdict: CLEARED WITH CONDITIONS.** The architecture holds:
- tunnels are carved cells, not road edges;
- robots go below only through a lift the player fits;
- the level is read from Z and flips only at the end of a ride;
- the ride is local to one robot;
- there is no new state shared between robots.

The inertness claim also holds against the code: with no `FitLift` order, every new branch is dead.

The defects are in what happens to a robot that is already below, mid-ride, or loading from a save. Four of them strand a robot or put it inside rock:
- items 1-4 below;
- item 5 decides R7;
- item 6 closes a test gap: no automated run would ever execute D3's robot code;
- item 7 completes the diff allowance;
- item 8 corrects how Q-A is put to the director.

Write items 1-8 into a REVISION section. D3 is then cleared to build without another review round.

Anchors are read at HEAD `94d0afa`. The tree is clean apart from this doc.

## What I verified and found sound

- **Tunnels as cells (attack 1).** This is the right call, and the plan's road-edge wording was wrong for this code.
  - `HasBuiltRoads()` is `BuiltEdgeCount > 0` (`RHSimWorldSubsystem.h:1370`). It switches every surface robot onto `MoveAlongRoads` (`RHSimRoads.cpp:40-44`).
  - `MoveAlongRoads` writes X and Y only (`:394-397`, `:404`).
  - So one built tunnel edge in `RoadGraph` would move the whole surface fleet, and the battery with it.
  - Log it as a deviation from `going-deep-plan.md:71` ("in the road graph, with a Level").
- **What a tunnel cell costs.** A tunnel cell is a full cell everywhere:
  - O2 fill and leak (`StepHabitability`, `:2725-2734`) and the standing leak (`:6994`);
  - `bBigEnough` (`:2762`) and legacy beds (`CountFloorBeds`, `:10950-10953`);
  - zoning, and civic growth's free-cell pick (`RHSimGrowth.cpp:387`, `:1032-1040`; see O1).

  It moves Hope only through those terms, and only after a player order. The rating does not flap while a tunnel is carved: the drop needs net decline (`:2783`).

  "No cheap-bed loophole" is true: the recipe is the same as Excavate's, and Excavate is not refused in era either.
- **Inertness (attack 2).** It holds for the battery and for the playtest.
  - **The bot fits no lifts.** The playtest's verbs are Bore, Excavate, Designate, BuildRoad, Survey, Dig, PaintDistrict and Build (`RHPlaytest.cpp:213`, `:723`, `:736`, `:761`, `:930`, `:1027`, `:1328`, `:1504`, `:1719`). `FitLift` is a new verb, and growth issues no uplink verbs.
  - **Every robot's Z is 50.** Robots spawn at Z 50 (`:453-456`, `:2236`, `RHSimRovers.cpp:322`), and nothing writes Z afterwards:
    - `MoveToward` moves along `GetSafeNormal2D` (`RHRobotMovement.h:25`);
    - `MoveAlongRoads` copies X and Y only;
    - load respawns from the saved `PosCm` (`:12909`).

    So `LevelOfZ` is 0 for every Mass robot, `Level` serializes as the same 0 it does today, and the brains' new real-level claim arguments are today's 0.
  - **The save size.** The only playtest-visible change is `Colony saved: ... (%d bytes` (`:12740`), which grows by the new block. "Byte-identical" means "identical under the `(N bytes` mask" (`scripts/sim/diff_battery.py:11`), as at D2.
- **Frame batching (attack 3).** Each entity runs all its `Steps` before the next (`RHRobotBrainProcessor.cpp:89-112`).
  - The ride state (`RideStepsLeft` in `RobotPaths`, `:2049`) and the Z flip are per robot.
  - `FRHDeepNet` is a pure cache. `RebuildShaftDerived` bumps `ShaftsVersion` on every bore (`:5184`), so the key catches new portal levels.
  - The cross-robot reads that depend on entity order already exist: claims (`:11627-11655`), the pad queue, `RepairClaims`, and the patient's live position (`:12097-12100`). They stay deterministic for a fixed entity order. One of them becomes dangerous once robots change level (item 4).
- **Capacity (attack 4).** The analysis is correct. A cap shared between robots is decided by which robot the processor reaches first, not by sim time.
- **Who goes below (attack 6).** Rovers stay up: freight, pads and the last-leg rule are level 0 (`RHSimRovers.cpp:50`, `:68`), so that is a sound deviation for D4. Excavators and scouts stay up because `TryClaimDig` is level-matched (`:11611`).
- **Era teleport for "floors reached only by tunnel" (attack 7c).** No such floor exists. A tunnel never changes level, and D2's reach is floor-wide, so every dig a tunnel touches is already reached. `EraLogistics` is level-matched (`:797`). Nothing new is needed.
- **The commute (attack 8).** Deferring is acceptable. Floor homes are `HomeLevel` only, so the underground layer cannot change a commute number. TN18 pins that.
- **The save block position.** The block sits after `WriteShaftBlock` (`:12345`) and is read after `ReadShaftBlock` (`:12783`), before teardown (`:12789`). K6's offset and SH12's `BlockAt` (`RHSimCommandlet.cpp:13608`) do not move. Every SH12 and K6 hand-made case fails before the new block is reached.

## REQUIRED changes

1. **A robot below must never be sent into rock or onto a bare column.**
   - **The defect.** Section 3 sends an unreachable site to "`GetApproachPoint` at the robot's level". But `GetApproachPoint` returns the site itself whenever `SiteLevel == RobotLevel` (`:2681`, `:2698`).
     - A hauler on -1 working an AirFilter in rock on -1 is therefore targeted into rock.
     - A cross-level unreachable site gives the head at `Z = RobotLevel x FloorH` (`:2694-2695`). On a floor, that is a column point, which is never carved (O9).
   - **The claim filter is ambiguous.** "A cross-level candidate is eligible if route-reachable" leaves today's two rules running unfiltered for a robot below:
     - the same-level pickup rule (`:11640`): a pickup on -1 whose drop is in rock on -2;
     - the `AreLevelsLinked` build rule (`:11755`).
   - **State it as:**
     - a robot below claims a haul only if both From and To are route-reachable, and a build only if its site is;
     - a fallback target is always `GetApproachPoint(Site, 0)` with level 0, so the fallback stays a surface act, as section 3 promises;
     - `HaulLoad`'s drop is computed for the robot's actual level, not a literal 0.
   - **Add TN12b.** A robot on -1 is offered (a) an AirFilter site in rock on -1 and (b) a haul from -1 to a rock site on -2. It claims neither, or it ends at the surface head. `DeepOffLattice == 0`.

2. **The charge reserve must cover the route actually driven, including the descent.**
   - **(a) Use the route that is actually driven.**
     - `EnterState` picks the pad nearest in XY to the robot's position below (`RHRobotStateTree.cpp:327-329`, `FindNearestChargePad` `:11832`).
     - `PlanDeepRoute` then picks the landing that is cheapest by cost units. The ride is costed at the reference speed (R4), and surface legs at road cost.
     - "The minimum over fitted landings" is therefore a lower bound on a route that may not be taken. A 2.5 m/s Fabricator is the case R4 names.
     - Compute the reserve on the path `PlanDeepRoute` returns to that pad: its lattice cm, its ride seconds, and its surface cm. Do not take a minimum.
   - **(b) Cover the descent.**
     - Z flips only at the end of the ride, so throughout a descent the condition reads level 0 and the small surface reserve.
     - `DriveWh`'s slack is `0.5 x DrawMoveW` Wh (`RHRobotStateTree.cpp:420`). A one-floor ride costs `DrawMoveW x 14 / 50 = 0.28 x DrawMoveW` Wh, so a round trip costs 0.56 x, more than the slack.
     - A robot that boards at the surface just above its surface reserve therefore lands below its below-ground reserve. It turns straight back and can drain in the cab on the way up.
     - Rule: at boarding, and on every ride sub-step, the reserve is evaluated as if the robot stood at the ride's destination landing, plus the ride time left.
   - **(c) The legacy brain.** Its mid-delivery exemption is unconditional, and it has no distance reserve at all (`RHRobotTaskProcessor.cpp:76-77`). The below-ground reserve must override `bMidDelivery` there too, not only in the StateTree condition.
   - **Add TN14b.** The ride-down-and-back drain is not tested. In both brains, a hauler boards at the surface with charge = surface reserve + epsilon. It reaches a pad without halting.

3. **Define every mid-ride and off-cell state, and fix the load repair.**
   - **An interrupted ride.**
     - `RobotPaths` replans on any change of `TargetCm` (`RHSimRoads.cpp:368-375`). So a charge seek, a vanished task or a reset fragment mid-ride abandons the ride with the robot on the column at its boarding level.
     - The halt gates (`RHRobotBrainProcessor.cpp:103-110`; legacy `:105-107`) can also stop a robot mid-ride.
     - State that both cases leave the robot on the column at its boarding level, with its Z unchanged.
     - A fitted portal column is a legal robot position for the planner (as start and as target), for repair and for `DeepOffLattice`.
   - **A robot saved mid-ride.**
     - Section 7's repair "a robot below on no carved cell respawns at the deploy ring" contradicts TN16's "a robot saved mid-ride arrives after load". The column is never carved (O9).
     - Exempt any robot that stands on a fitted portal column at a level its shaft reaches.
     - Add a repair: a robot below whose level no fitted lift reaches goes to the deploy ring.
   - **When the repair runs.**
     - Robots respawn at `:12902-12911`. At that point `FloorTable` and `Shafts` still hold the previous colony: teardown (`:12789-12806`) resets neither, and they are installed only at `:12920-12921`.
     - `FindCarvedCellAt` reads both (`:6082-6091`).
     - So the repair must test against `LoadedFloors`/`LoadedShafts` at spawn, or run after `:12921`.
   - **Extend TN16.** Two cases: a robot saved mid-ride on -1 loads on its column, not at the ring; and a load over a different live colony repairs correctly.

4. **Repair across levels.**
   - **The defect.** `TryClaimRepair` has no level filter and no "halted" filter: it takes the most-worn robot with Wear >= 50 anywhere (`:12059-12093`). Repair then re-tracks the patient every sub-step with 2D `MoveToward` (`RHRobotStateTree.cpp:157-176`). With lifts fitted:
     - **(a)** An RC-M claims a worn but working patient on the surface, and the patient then rides down. The RC-M reaches its XY and calls `ApplyRepairAt` through the rock.
     - **(b)** An RC-M below claims a moving surface patient. Its target changes every sub-step, so `RobotPaths` replans every sub-step. The ride never completes (a livelock), and each sub-step costs a deep Dijkstra.
   - **The rule.**
     - "Halted" means `Wear >= WearHaltThreshold`.
     - A cross-level repair claim is allowed in either direction only for a halted patient.
     - A same-level claim is released as soon as the levels differ, unless the patient is halted.
     - `ApplyRepairAt` requires equal levels.
   - **Note.** A drained robot (charge 0, not worn out) is never a repair target. That is today's rule on the surface as well, so a robot drained below is lost for good. Say so in R8's neighbourhood.
   - **Add TN15b.** Cover both (a) and (b).

5. **Decide R7: drop the era refusal.**
   - **Why.**
     - Tunnel carving is `StepProduction` work, which `EraStep` runs (`:727`), exactly like the Excavate carve queue. `CanEnterEraMode` does not refuse that queue today (`:611-640`).
     - The road refusal exists because era parks the Fabricators who build roads (`:620-622`). That reason does not apply to Borer work.
     - The lock can also be unbounded. A front whose dig is served only by an unanchored shaft never advances. Load repairs a dead `BorerId` to 0 (`:12925-12935`).
   - **Log it.** Record it as a deviation from `going-deep-plan.md:70`, with this reason.
   - **TN7 becomes:** era is allowed with an open tunnel, and era carves it, as SH12(c) does for shaft 1's queue.

6. **Exercise D3's robot code on real Mass robots in at least one automated run.**
   - **The gap.**
     - The battery ticks no Mass robot (section 0), and the playtest fits no lift. So the StateTree transitions, the halt gates, frame batching, the reserve and the ride run in no automated run at all.
     - TN harness robots call the brains' functions; they are not the brains.
     - Attacks 2-5 live exactly there, and the live smoke is not a regression bar.
   - **The run.** Add an opt-in playtest variant, `lifts=1`, beside `nogrowth=1` (`RHPlaytest.cpp:1859`). It fits one lift once -1 is rated and runs at least 20 sols. It is outside the byte-identical comparison.
   - **It asserts:**
     - `DeepOffLattice == 0`;
     - no robot halts below from a drained battery;
     - at least one haul is delivered and at least one build completed at a room below;
     - every hopper credit is matched by a load;
     - robot levels survive a mid-run save and load.

7. **Complete the diff allowance.**
   - `farice.log:7089` prints a literal `F16 the v44 save loads ...` (`RHSimCommandlet.cpp:12097`). It is not in section 4's list.
   - Either edit it, which makes **9 lines out and 9 in**, or state that it stays literal. House practice (D2 item 7, `deep.log:591`) edits it.
   - The new block is 12 bytes (three int32 counts), not 8. The mask hides it either way.
   - The `Contains(TEXT("/44"))` checks at `:10817`, `:12091`, `:12551` and `:13664` are conditions, not only text, and must move to `/45` with the lines.

8. **Put Q-A to the director honestly, and drop Q-B.**
   - **Q-A.** Under (a), in D3 a fitted lift is strictly worse for every delivery and build that the hand-off already serves:
     - `HaulUnload` credits the hopper wherever the hauler stands (`:11714-11717`), and Fabricators work from the head (`:11755-11757`);
     - a ride adds 140+ sub-steps and a walk, and the fit costs 400 kg;
     - the only gain is a below-ground pickup. Today's same-level pickup rule (`:11640`) means a surface hauler never collects from a source below, and the site's source scan can pick one (`:1301-1312`).

     So section 2's argument against Q-B (b), "the uncapped hand-off is strictly better, so no player would fit one", already holds for this design. Say plainly that in D3 lifts are a mechanism with no economic payoff. The payoff arrives in D4, when the head hand-off pays the same lift wait.

     Q-A is still needed, because (b) re-cuts the baselines. The recommendation stays (a). Let the director also choose whether the 400 kg bill stays while a lift is a pure cost.
   - **Q-B is not a question.** The approved plan already puts lift capacity in D4 (`going-deep-plan.md:38`), with load "from the flows, never robot positions" (`:76`). Q1a answers for step 2, not for D3. Record it as "per plan", not as a deviation.

     A deterministic per-lift budget in D3 is possible only with a lag: sol s would use rides counted in sol s-2, because a sol's count is complete only after every entity has passed its boundary. That is D4's flows rule, built early.

     Add to D4's design: the lift load must charge the fallback hand-off as well as rides. Otherwise this dominance never ends.
   - **Add to R12's deviation list:**
     - D2 REVISION O5's promise that "D3's joined graph replaces both with one route" (`d2-shafts-design.md:468`) is not kept;
     - the plan's D3 bar "a commute across two shafts" (`going-deep-plan.md:72`) is covered by TN18 plus SH, not by a new commute;
     - item 5's era refusal is dropped.

## Optional changes

- **O1. Civic growth will fill tunnel cells.** `IsCellFreeForRoom` (`RHSimGrowth.cpp:387`) and the civic pick (`:1032-1040`) take the lowest free carved cell, so growth can turn a tunnel into a row of rooms. That is harmless to walking but erases the tunnel. Exclude chain cells, or state it, and pin it either way.
- **O2. Pin the head shift at a merge.** A merge adds both shafts to `Touching`, so `ServingShaft`, and with it the fallback head for sites in the merged dig, can change at the moment the tunnel completes. Pin it in TN5.
- **O3. Make `LevelOfZ` robust.** `RoundToInt(50 / FloorH)` is 1 if `FloorHeightMeters` is ever set under 1 m. Use `RoundToInt((Z - 50) / FloorH)`, or assert `FloorHeightCm > 100` at config read.
- **O4. Fix the batch-start log line.** It prints `WorkItem.Y` as "floor %d" (`:1547-1549`). For `(2|3, Id)` it would print the tunnel Id as a floor. Give tunnel batches their own line.
- **O5. Live empty completion.** A spiral or plan point can land on the chain while two batches are in flight. Then a completion finds no uncarved point. Define "spoil, no cell" for the live case too, not only at load, and assert it in TN6.
- **O6. Shelter below (director note, not D3).** `AccrueWear` says every unit is exposed "until the M1-d vault ... exist[s]" (`:11926-11931`). Robots below are the first that could be sheltered. That is a balance call.
- **O7. Path key.** Key `RobotPaths` replans on (`TargetCm`, `TargetLevel`) explicitly rather than relying on Z inside `TargetCm`.
- **O8. TN17 is nearly vacuous by construction.** Era refuses construction, so the paired save has no demand below. Also assert that the agent leg's robots surface and that none halts below.
- **O9. One build ranking key.** Once a lift exists, define a single key for every Build candidate. Today's `DistXY(GetApproachPoint)` and the new `DistXY + |dL| x LiftPlanCm` must not be compared across candidates.
- **O10. Seed TN14's random cases.** Its "50 random carves and tunnels" must come from a seeded stream (`FRandomStream`, as `RHAgentSubsystem.cpp:40`).
- **O11. Show a stalled tunnel.** A tunnel that no Borer can work should say so on its card ("stalled: no Borer serves either end").

## Director questions

- **Q-A: needed, re-worded (item 8).** Recommend (a). Tell the director that a lift is a pure cost until D4, and ask whether the 400 kg bill stays.
- **Q-B: not needed.** Capacity is D4 by the approved plan.

VERDICT: CLEARED WITH CONDITIONS
1. A robot below claims a task only if every end it must visit is route-reachable (the haul's From and To, the build's site). The fallback target is always `GetApproachPoint(Site, 0)`, so a robot is never targeted into rock or onto a bare column. Add TN12b.
2. The charge reserve is computed on the route `PlanDeepRoute` returns to the pad `EnterState` picks, and is evaluated for the destination at boarding and on every ride sub-step. The legacy brain's mid-delivery exemption is overridden below. Add TN14b.
3. Define abandoned and halted rides: the robot stays on the column at its boarding level, which is a legal position. Exempt fitted columns from the deploy-ring repair. Run that repair against the loaded floors and shafts, never the live ones. Extend TN16.
4. A repair claim across levels is allowed only for a worn-out patient, and a claim is released when the levels part. Add TN15b.
5. Drop the era refusal while a tunnel is open. Log the deviation and rewrite TN7.
6. Add an opt-in `lifts=1` playtest variant that runs D3 on real Mass robots, with the assertions in item 6.
7. Count `farice.log:7089` in the diff allowance (9 out, 9 in, or state that it stays literal). Move the `/44` checks to `/45`. The new block is 12 bytes, not 8.
8. Re-word Q-A to say a lift is a pure cost in D3, and drop Q-B as "per plan". Log the O5 promise, the commute bar and the era refusal as deviations.

---

# REVISION (2026-10-21)

This revision answers the PROFESSOR REVIEW above. Where it disagrees with sections 0-10, the revision wins.
Anchors are at HEAD `94d0afa`. The working tree has an uncommitted bot fix in `RHPlaytest.cpp`, so that file is quoted at HEAD (`git show`), and its anchors move at build time.
Items 1-6 apply to options (a) and (b) of item 8. Under (c) they move to D4 with the robots (item 8 says what is kept and dropped).

## Item 1. A robot below claims only what it can reach; the fallback is a surface act

**One test.** `IsRouteReachable(Level, Pos, Site)` is true when:
- the site is on the surface;
- or the site's cell is in a dig with a fitted landing, or in the robot's own dig. The site's cell is `FindCarvedCellAt(SiteLevel, SiteLocation)` (`:6082-6091`), and a site in rock is never reachable.

It is O(1) on `FRHDeepNet`'s dig labels. It also accepts a fitted portal column as a legal position (item 3).

**Claims.**
- **`TryClaimHaul`** (`:11628-11658`). For `RobotLevel < 0`, a candidate needs `IsRouteReachable` for BOTH `T.From` and `T.To`.
  - This replaces the bare same-level test at `:11640` for robots below. The other filters (`bOutpostSource`, the freight fallback) stay.
  - For `RobotLevel == 0` with lifts: `From` must be level 0, or reachable. `To` may be anything, because an unreachable `To` gets the surface fallback below.
- **`TryClaimBuild`** (`:11743-11770`). For `RobotLevel < 0`, the site must be reachable. This replaces `AreLevelsLinked` at `:11755` for robots below. Surface robots keep `AreLevelsLinked`.
- **One ranking key (O9 adopted).** While `HasRobotLifts()`, every candidate of both functions ranks by `DistXY(robot, target XY) + |RobotLevel - TargetLevel| x LiftPlanCm`, where the target comes from `GetRobotTarget`. Ties go to task order. Today's `DistXY(GetApproachPoint)` is never compared with this key. With no lift, today's key and scan run unchanged.

**Targets.**
- `GetRobotTarget(Site, RobotLevel, RobotPos, OutCm, OutLevel)` returns the site at its level when it is reachable.
- **Otherwise it returns `GetApproachPoint(Site, 0)` with `OutLevel = 0`, always.** A robot is never targeted at `GetApproachPoint(Site, L < 0)`, which is the site itself in rock (`:2681`, `:2698`) or a bare column (`:2694-2695`). The fallback hand-off therefore happens only at a surface head, as today.
- `HaulLoad(TaskId, Cap, OutKg, OutDrop, int32 RobotLevel = 0, int32* OutDropLevel = nullptr)` computes the drop with `GetRobotTarget` at the robot's real level. Both brains pass `LevelOfZ`. Harness calls keep their defaults, so SH9 and EP4 are unchanged.
- The build approaches (`RHRobotStateTree.cpp:115`, `RHRobotTaskProcessor.cpp:199`) call `GetRobotTarget`.

**TN12b.** A hauler on -1 in a fitted dig is offered (a) an AirFilter site in rock on -1 and (b) a haul from a Lab on -1 to a rock site on -2. It claims neither. A surface hauler takes (b) and drops at the surface head, with `OutDropLevel == 0`. `DeepOffLattice == 0` throughout.

## Item 2. The reserve covers the route actually driven, including the descent

**(a) The route actually driven.**
- `DeepReserveWh(Entity, Robot, Level, Pos, PadId, PadLoc)` plans `PlanDeepRoute` from the robot to the pad that `EnterState` will pick: `FindNearestChargePad(Pos, …, 0)`, the same call as `RHRobotStateTree.cpp:327-329`.
- It returns `DriveWh(lattice cm + surface cm on that path) + DrawMoveW x RideSeconds(path) / 50 x 1.25`. There is no minimum over landings.
  - Road legs are costed at base speed, which overestimates, the safe side.
  - It uses the robot's real ride seconds, not the planning speed (R4).
- **Memo.** A runtime map in the subsystem, keyed per entity on (level, cell or portal, `PadId`, `FRHDeepNet` key), like `RobotPaths` (`.h:2049`). It is never saved, and it recomputes only when the robot changes cell. -scale gets a budget: 10,000 cached reads at most 1 ms; 1,000 cold at most 10 ms.

**(b) The descent.**
- While the robot's path has a ride next or in progress (`GetRideState(Entity)`, a const accessor on `RobotPaths`), the reserve is evaluated **as if the robot stood on the ride's destination landing, plus the ride time left**.
- This runs at boarding (the sub-step the robot reaches the column) and on every ride sub-step.
- **A descent that fails the test** fires the seek. The new pad target replans from the column at the boarding level, which is the surface (item 3). The robot never lands below under its below reserve.
- **An ascent is never broken off for a charge seek.** Its destination is level 0, toward the pads. The condition returns false while an ascending ride is in progress. After landing, the surface rule applies.

**(c) Both brains.**
- The StateTree condition (`RHRobotStateTree.cpp:424-469`) takes the deep branch when `Level < 0` or a ride is next or in progress. In that branch the mid-delivery exemption (`:456-467`) is not taken.
- The legacy seek (`RHRobotTaskProcessor.cpp:74-77`) becomes: seek when `Current != Charge && ((!bMidDelivery && ChargeFrac < seek) || bDeepLow)`. `bDeepLow` is the same deep test, so the reserve overrides `bMidDelivery` below.
- Both are untaken at level 0 with no ride.

**TN14b,** in both brains: a hauler boards at the surface head with charge = surface reserve + 1 Wh, for a -1 drop. It does not descend. It reaches a pad and never halts. A second hauler, at the deep reserve + 1 Wh on the farthest cell of -2, ascends and docks. TN14's random cases come from a seeded `FRandomStream` (O10 adopted).

## Item 3. Interrupted rides, off-cell states, and the load repair

**An interrupted ride.**
- `RobotPaths` replans on any target change (`RHSimRoads.cpp:368-375`; the key becomes (`TargetCm`, `TargetLevel`), O7 adopted).
- A charge seek, a vanished task or a fragment reset mid-ride therefore discards `RideStepsLeft`. A halt gate (`RHRobotBrainProcessor.cpp:103-110`; legacy `:105-107`) or an era park freezes it.
- **In every case the robot stays on the column XY at its boarding level, with Z unchanged.** Z changes only when a ride completes (section 3).
- **A fitted portal column is a legal robot position:**
  - as a planner start or target;
  - for `IsRouteReachable`;
  - for repair (item 4);
  - and it does not count in `DeepOffLattice`.
- That holds when the robot's XY is within `ArriveDistCm` of a fitted shaft's column centre, at a level where the shaft is a portal (it has a landing, or it is level 0). A robot boards only between portal levels, so an interrupted one always stands on one.

**The load repair** (replaces section 7's robot repair).
- Robots respawn from `PosCm` as today (`:12902-12911`). The saved `Level` of each is kept in a local list beside the `Respawned` handles.
- **The repair runs after** the floor table, the shafts (`:12920-12921`), the D2 warning repairs (`:12925-12935`), `RebuildShaftDerived` and the new lift and tunnel block are installed. It never runs against the previous colony that `FindCarvedCellAt` would otherwise read.
- In order, with a Warning each:
  1. `Level != LevelOfZ(Z)`: set `Z = Level x FloorH + 50`.
  2. `Level < 0`, on a fitted portal column at that level: kept (a robot saved mid-ride).
  3. `Level < 0`, on a carved cell in a dig with a fitted landing: kept.
  4. Any other `Level < 0` (no fitted lift reaches it, rock, an unfitted dig): moved to the surface deploy ring.
- The move uses a new sim-only `URHAgentSubsystem::SetRobotPosition(Entity, PosCm)`.

**TN16 is extended:**
- (i) a robot saved mid-ascent on -1 loads on its column, not at the ring, replans and arrives;
- (ii) a save is loaded over a different live colony (other floors, other shafts): rules 2-4 judge against the loaded ones;
- (iii) a hand-made file with a robot on -2 and no lift list moves it to the ring.

## Item 4. Repair across levels: halted patients only

- **Halted** means `Wear >= WearHaltThreshold` (`.h:1736`, `:2318`).
- **`TryClaimRepair`** (`:12059-12093`): when the patient's level differs from the RC-M's, the patient must be halted and route-reachable (a cell in a fitted dig, or a portal column). This applies in both directions. Same-level claims are as today.
- **Repair tick** (`RHRobotStateTree.cpp:157-176`):
  - same level: today's `MoveToward` re-track;
  - levels differ and the patient is halted: `MoveAlong` to its stationary position with `TargetLevel`. That is one plan;
  - levels differ and the patient is not halted (it rode away): `ReleaseRepairClaim` and reset.
- `ApplyRepairAt` (`:12102`) requires equal levels; otherwise it is a no-op plus a release.
- The legacy brain has no repair branch, so nothing changes there.
- **Note, beside R8:** a drained robot (charge 0, not worn out) is never a repair target. That is today's rule on the surface too. So a robot that drains below is lost for good, and the item 2 reserve is the only guard.
- **TN15b:**
  - (a) An RC-M claims a worn, working surface patient that then rides to -1. The claim is released, and `ApplyRepairAt` never fires across levels.
  - (b) An RC-M on -1 and a moving worn surface patient: no claim. A cumulative `DeepPlanRuns` counter stays flat over 200 sub-steps.

## Item 5. The era refusal is dropped (R7 decided)

- **No era refusal for tunnels.** Carving is `StepProduction` work, which `EraStep` runs (`:727`), like the Excavate carve queue, which `CanEnterEraMode` does not refuse (`:611-640`).
- The road refusal exists because era parks the Fabricators (`:620-622`). That does not apply to Borer work.
- A front served only by an unanchored shaft would also have made the lock unbounded (`:12925-12935`).
- **Deviation** from `going-deep-plan.md:70`, logged with this reason.
- **TN7 rewritten:**
  - With a two-ended open tunnel, `CanEnterEraMode` is true.
  - Era carves the tunnel to completion, and the chain cells land in the same order as in an agent-band twin. (Batch boundaries are the same through the D2 time-budget integrator; only the step size differs.)
  - A stalled tunnel (no Borer serves either end) does not block era.
  - A cancel during era stops new batches.
- **The stalled state is shown (O11 adopted):** "stalled: no Borer serves either end" on the tunnel card.

## Item 6. An opt-in `lifts=1` playtest variant runs D3 on real Mass robots

- **Where.** `RHPlaytest.cpp` parses `nogrowth=1` at HEAD `:1859` (field `:92`, help `:1834`). `lifts=1` sits beside it.
  - The file is being edited by the bot fix now; it is anchored and written after that lands. Nothing in it is touched by this doc.
  - It is off by default, so the default and `districts=1` playtests stay byte-identical under the `(N bytes` mask.
- **What the bot does.** Once floor -1 is rated (`RatedFloors` contains -1), it issues `FitLift` with `Value` 0 through the uplink, once. The run lasts at least 20 sols (`rh_playtest.sh … lifts=1`, Sols 20 or more). It is outside the byte-identical comparison.
- **What it asserts.** Each assertion is one `[Playtest] lifts:` line; a failure is an Error line, so the script fails:
  - `DeepOffLattice == 0` at the end;
  - a once-per-sol scan finds no robot below with charge 0 that is not docked;
  - at least one haul is unloaded at a site below by a robot standing at that level (`DeepDeliveries >= 1`), and at least one build completes while its Fabricator is below (`DeepBuildsCompleted >= 1`);
  - **mass:** every kg `HaulUnload` credits below is matched by a load. Sum loaded = sum credited + sum returned + sum lost at abandon, each a transient counter, within 1e-3 kg;
  - at sol 10 it saves and loads: the multiset of (DefName, Level) is equal before and after.
- If the default bot never needs anything below by sol 20, the variant also orders one floor Lab module on a -1 cell after the fit, to give the robots demand.

## Item 7. The diff allowance: 9 out, 9 in

| Log:line (`battery_post_d2`) | Test line at HEAD | At v45 |
|---|---|---|
| `traffic.log:937`, `:938` | `RHSimCommandlet.cpp:10811` (`Ver == 44`); `:10817` (`/44`, the v43 patch above it) | version 45; "a v44 save"; `/45` |
| `deep.log:590`, `:591`, `:596` | `:12507` (`Ver == 44`), `:12522` (hand-made), `:12551` (`/44`) | "version 45"; "hand-made v45"; "a v44 file"; `/45` |
| `framing.log:582` | e5, `:14052` (`Ver == 44`) | "version 45 (expect 45)" |
| `farice.log:7075` | F16, `:12091-12092` (`Ver == 44`, `/44`) | 45; "a v44 save"; `/45` |
| **`farice.log:7089`** | **`:12097`, the literal "the v44 save loads"** | **"the v45 save loads"** (edited, as D2 edited `deep.log:591`) |
| `shafts.log:1412` | SH12; condition `:13664` (`/44`) | "version 44; expected …/45" |

- The `Contains(TEXT("/44"))` checks at `:10817`, `:12091`, `:12551` and `:13664` are conditions, not only text. Each moves to `/45`, with its previous-version constant and patch, in the same change.
- **Blocks.** Under (a) and (b) the new block is **12 bytes**: `LiftCount`, `NextTunnelId`, `TunnelCount`. Under (c) it is 8 bytes, with no `LiftCount`, because there is no dead saved field. The `(N bytes` mask hides both.
- **The bar.** 48 runs, 0 not green, and `diff_battery.py` against `battery_post_d2` shows exactly 9 lines out and 9 in. Recount literally at build time; any other line fails the gate.

## Item 8. The director's questions

**Q-B is dropped: capacity is D4, per plan** (`going-deep-plan.md:38`, `:76`). It is not a deviation.
- Note for D4's design: the lift load must charge the fallback head hand-off as well as rides. Otherwise the hand-off dominates for ever.
- A deterministic per-lift budget earlier would need a two-sol lag. That is D4's flows rule anyway.

**Q-A, put honestly.**
- In D3 a fitted lift is a pure cost compared with the instant head hand-off:
  - `HaulUnload` credits the hopper wherever the hauler stands (`:11714-11717`), and Fabricators already work underground sites from the head (`:11755-11757`);
  - a ride adds at least 140 sub-steps plus a walk.
- The only gain is collecting from a source below. Today a surface hauler never does that (`:11640`).
- The lift pays off only in D4, when the hand-off pays the same lift wait.

Three options. Each one builds and passes its own bar.

| | (a) Gate, 400 kg | (b) Gate, 0 kg until D4 | (c) Tunnels only; robots below move to D4 |
|---|---|---|---|
| What the player meets | "Fit robot lift" costs 400 kg Struct, and in D3 makes deliveries slower | "Fit robot lift" is free; robots ride, slower, with no penalty to undo | Paint tunnels; digs join; robots keep the head hand-off |
| Section 1 (tunnels, carving, O9, `DigTunnel`/`CancelTunnel`) | kept | kept | kept |
| Section 2 (`FitLift`, portals, `PlanDeepRoute`, `FRHDeepNet`) | kept | kept; `LiftFitStructKg` row 0; the "short of Struct" refusal is tested with the row forced above 0 | **dropped to D4** |
| Section 3 and items 1-4 (robots below, claims, reserve, rides, repair) | kept | kept | **dropped to D4** |
| Section 4 (inertness) | the `FitLift` gate | the same gate | trivial: no robot code changes; only the Borer loop, SinkShaft refusal 8 and the save change |
| Section 5 / item 5 (era: no refusal) | kept | kept | kept |
| Section 6 (commute untouched) | kept | kept | kept |
| Section 7 (save v45) | 12-byte block, lift list | 12-byte block | 8-byte block, tunnels only; `FRHRobotSaveState::Level` stays the dead 0 until D4 |
| Section 8 (presentation) | all | all | tunnel strips, the brush, the stalled card; no cab riders, no robots on floor views |
| Section 9 (tests) | TN1-TN18, TN12b, TN14b, TN15b; `lifts=1` | the same | TN2-TN7 (tunnel refusals, chain, carve, two ends, interleaving, era), TN18, -scale carving; the `lifts=1` variant moves to D4 |
| Risk | the trap: a paid mechanic with no payoff | rides only cost time; still a mild trap | the plan's D3 "What you see: robots go down" waits a gate |

- **Recommendation: (c).**
  - D3 is the riskiest gate of step 2, and items 1-4 are where the risk lives: stranded robots, rock targets, the descent drain, repair livelock.
  - Under (a) or (b) that code ships with no economic reason to use it. It would then be reopened in D4 to wire it to capacity.
  - Under (c) D3 lands small and inert, tunnels and the dig merge are visible, and robots go below once, in D4, together with the capacity that makes a lift worth fitting. The D3 robot design and this revision carry into D4 unchanged.
- **If the director wants robots below in D3, take (b), not (a).** 400 kg for a pure slowdown is a trap.
- Either way, D4's design must charge the head hand-off.

## Deviations to log in `docs/design-decisions.md`

1. Tunnels are carved lattice cells, not road-graph edges with a Level (`going-deep-plan.md:71`).
2. The era refusal while a tunnel is carved is dropped: carving is Borer work in both bands (`going-deep-plan.md:70`; item 5).
3. Era logistics teleport past lifts and tunnels, as C4 and D2 logged.
4. Rovers stay on the surface in D3. The plan's "robots and rovers go down" waits for D4 trams.
5. The commute ignores tunnels until floor homes and seats get a cell (D6). R2 is deferred.
6. D2 REVISION O5's promise that "D3's joined graph replaces both with one route" (`d2-shafts-design.md:468`) is not kept. Robots and crew may still show different heads.
7. The plan's D3 bar "a commute across two shafts" (`going-deep-plan.md:72`) is covered by TN18 plus `-shafts` SH10, not by a new commute.
8. Under (c) only: robots-below (Q1a) moves from D3 to D4.
- Capacity in D4 is **per plan**, not a deviation.

## Optional items

**Adopted:**
- **O1.** Growth's free-cell pick (`IsCellFreeForRoom`, `RHSimGrowth.cpp:387`; the civic pick `:1032-1040`) skips every tunnel chain cell. The player may still zone one by hand. This is inert with no tunnel. Pinned in TN4.
- **O2.** TN5 pins `ServingShaft` (and so the fallback head) for a site in dig B before and after the merge.
- **O3.** `LevelOfZ(Z) = RoundToInt((Z - 50) / FloorHeightCm)`, and the config read asserts `FloorHeightCm > 100`.
- **O4.** Tunnel batches log their own line: "Borer #n CarveCell started … (tunnel t, A end, floor L)". The bore and carve line (`:1547-1549`) is untouched.
- **O5.** A live completion that finds no uncarved chain point drops its spoil and appends no cell, with a Display line. TN6 forces it with a plan point landing on the last chain cell while two batches are in flight.
- **O7.** See item 3.
- **O8.** TN17 also asserts that the agent leg's robots surface, that none halts below, and that the deep code actually ran (`DeepPlanRuns > 0`).
- **O9.** See item 1.
- **O10.** See item 2.
- **O11.** See item 5.

Under (c), O1, O2, O4, O5, O10 and O11 stay in D3. O3, O7, O8 and O9 move to D4 with the robots.

**Rejected:**
- **O6** (shelter from wear below). It is a balance call for the director, best taken with D5's depth costs. D3 leaves `AccrueWear` (`:11926-11931`) unchanged.

## Updated bar

- **Save v45, strict.** The block is 12 bytes under (a) or (b), 8 under (c).
- **Battery:** 48 runs, 0 not green; exactly 9 lines out and 9 in against `battery_post_d2`.
- **`-tunnels`** passes, with the checks of the chosen option.
- **Playtests:** the default and `districts=1` runs are byte-identical under the mask. Under (a) or (b), `lifts=1` passes its five assertions.
- **-scale** stays within every budget, and the one-shaft numbers are unchanged.
- **Logged:** the deviations above, Q-A's answer, and the D4 note (the lift load charges the hand-off).
