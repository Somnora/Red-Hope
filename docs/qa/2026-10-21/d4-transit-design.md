# D4, transit: lifts and trams: design (2026-10-21, draft for adversarial review)

Step 2 (going deep), gate 4. Design only. No code is written.

**Anchors.** Code is read at HEAD `b8ab469` with `git show HEAD:`. Every file under `Source/RedHopeSim` at HEAD is byte-identical to D2 (`94d0afa`). D3 (tunnels) is being built in the working tree right now: it adds about 600 lines to `RHSimWorldSubsystem.cpp` and takes save v45. Every `RHSimWorldSubsystem.cpp` anchor below therefore moves at build time. The builder re-anchors after D3 lands, as D2 did after the Gate-D pass.

**Sources:**
- `going-deep-plan.md`: the D4 row (`:38`), the D4 risks (`:74-78`), Q1a and Q4a;
- `d3-tunnels-design.md`, all of it: sections 2-3, the PROFESSOR REVIEW, the REVISION, and the DIRECTOR ANSWER (c), which moved robots-below into D4;
- the D1 and D2 REVISIONs (O1 ordinals, O9, floor-wide reach, R2, R6);
- `c6-traffic-design.md` (R8 variant (a), the 60-160 ramp, C4's position memo);
- `docs/design-decisions.md` from "Step 2, going deep" onward.

**Goal.**
- Robots ride fitted lifts and work on any floor (Q1a).
- Every shaft has a capacity a sol, for people and cargo together.
- A crowded shaft adds a wait to every commute and every cargo move through it, counted from the flows (Q4a).
- A tram is an aggregate vehicle on a long finished tunnel.
- A second shaft, another lift car or a tram line fixes a crowd.

**What you see.**
- You fit a lift car on a shaft.
- Haulers and Fabricators ride down and work in the rooms.
- Commutes through a busy shaft get longer, and the City panel says which shaft is crowded.
- A second car or a second shaft clears it.
- A tram line on a long tunnel carries robots between digs.

**Decisions at a glance.**
1. **The gate is the colony's first fitted lift car** (`FitLift`). With no car, every line of today's code runs: the free, endless hoist and the instant head hand-off. The battery and the playtest never fit a car. Director question Q-A.
2. **Robots never spend a shared budget.** A robot adds whole numbers to a per-sol ledger, which is commutative, and reads waits frozen for the whole sol. The world sets those waits at the sol threshold, in shaft-Id order, in both bands. So no capacity decision depends on which robot the processor reaches first.
3. **The load for sol s is** people, from the commute flows at the threshold, plus cargo, from the ledger of sol s-2. The two-sol lag makes the cargo count complete whatever the frame schedule.
4. **Once transit is on, every cross-level move pays its shaft:**
   - a robot ride pays the wait, the dwell and the ride;
   - the instant head hand-off becomes a hold for the same wait plus the cage's own ride, and charges the same ledger;
   - world-side instant takes from a floor (road and lift bills, growth bills) charge the ledger too.
5. **An unfitted shaft keeps a slow hoist with a small capacity;** a fitted car is faster and carries five times as much. That is why a lift is now worth 400 kg.
6. **Trams carry robots and their cargo in D4.** People ride trams when floor homes and seats get a cell (D6). Director question Q-B.
7. **Rovers never go below.** With transit on, rover freight to a floor site ends at the head as a charged hand-off.

---

## 0. Facts the design rests on (HEAD `b8ab469`)

**The clock.**
- `SubStepSeconds = 0.1f` and `SolLengthSimSeconds = 1200.f` (`RHSimClockSubsystem.h:23`, `:26`). That is 12,000 sub-steps a sol. One sol-hour is 50 s, the `/50` idiom.
- `EraStepSimSeconds = 60.f` (`:33`). `MaxCarrySimSeconds = 120` (`:37`): a frame never spans more than 120 s, which is a tenth of a sol.

**Frame batching.**
- Each robot entity runs all of the frame's `Steps` before the next entity (`RHRobotBrainProcessor.cpp:71-113`; legacy `RHRobotTaskProcessor.cpp:58-67`).
- The world runs its own batch of `StepSim(0.1)` with a per-step `StepEndOverride` (`RHSimWorldSubsystem.cpp:504-519`).
- The two batches never interleave, and the order between them within a frame is not pinned in code.
- **Robots have no Id.** No fragment carries one; the only ordinal is the spawn index in `RobotHandles` (`RHAgentSubsystem.cpp:182`).

**The threshold precedent.** `StepCityGrowth` uses `SolNow = FloorToInt32(GetStepEndSeconds()/1200)` and crosses on `SolNow > GrowthCheckedSol`, with the checked sol saved (`RHSimGrowth.cpp:459-470`). `RollProductionLedger` handles skipped sols the same way (`RHSimWorldSubsystem.cpp:6933-6955`). The clock's `OnSolElapsed` is the autosave hook. It fires before the frame's steps, so it is not used here.

**The commute** (`RHSimCommute.cpp`):
- The ramp is `clamp((Pop-60)/(160-60), 0, 1)`, with Pop = every colonist (`:33-41`). Every public reader returns the zero result before touching a cache when the ramp is 0 (`:413-416`, `:434`, `:445`, `:456`, `:468`, `:530`, `:552`).
- `GetCommuteDistanceCm` (`:194-210`):
  - floor to floor is `|dL| x CommuteShaftCmPerFloor`, with no shaft chosen;
  - floor to surface is `|L| x Per + CommuteSurfaceCm(head of PickCommuteShaft, P)`.
- `PickCommuteShaft` (`:170-192`): one shaft returns 0; otherwise the reaching shaft with the cheapest surface leg, strict `<`; else shaft 0.
- `FRHCommuteTrip` (`RHSimWorldSubsystem.h:2142`) stores no shaft. Floor-to-floor trips keep shaft 0's head at both ends (`:284-292`, `:299-304`).
- The flows key `MakeCommuteKey` (`:50-65`) has no lift input. X13 pins exactly one rebuild per key bump; X14 pins zero rebuilds at 59 and 60 people.
- `CommuteShaftCmPerFloor` 6000 is documented as "the lift **and its wait**" (`RH_Config.csv:225`).

**Where the ramp is open in fixed tests.**
- `-percapita` reaches 5,000 floor colonists through one shaft, and `-arrivals` reaches 1,500. Both are inert only because every trip is at most 4 x 6000 = 24,000 cm, under the 30,000 line.
- The 110-sol districts playtest reaches 115 people (ramp 0.55) and prints the commute (`RHPlaytest.cpp:361`).
- **So no population rule can keep a people-wait inert. Only a player order can.**

**The cross-level moves that cost nothing today:**
- `HaulUnload` credits `To` wherever the hauler stands (`RHSimWorldSubsystem.cpp:11710-11729`); `HaulLoad` aims the drop at `GetApproachPoint(T->To, 0)` (`:11704-11706`);
- Fabricators work cross-level sites from the head (`:11755-11757`; `ApplyBuildWork` has no level check, `:11774-11830`);
- the site hopper sources from the first holder on any linked floor (`:1297-1314`);
- road bills take Struct from linked floors (`RHSimRoads.cpp:54-109`);
- growth holdings and bills read linked floors (`RHSimGrowth.cpp:162-180`, `:266`).

**Era.**
- `EraStep` runs `EraLogistics` instead of the board (`RHSimWorldSubsystem.cpp:718-746`), and every era leg is level-matched (`:797`, `:901`, `:928`).
- In the agent band the only cross-level cargo is construction delivery (`:1297-1330`), and construction refuses era (`:612-619`).
- Robots park in era (`RHRobotBrainProcessor.cpp:46-50`).

**Save.**
- v44 at HEAD (`:36`); D3 takes v45 and writes its tunnel block straight after `WriteShaftBlock` (`:12345`).
- `FRHRobotSaveState::Level` is serialized but never written and never read (`RHAgentSubsystem.cpp:188-215`, `RHSimWorldSubsystem.cpp:12906-12909`).
- Robots respawn (`:12901-12911`) before `FloorTable` (`:12922`) and `Shafts` (`:12925`) are installed.

**The battery** ticks no Mass robot. Harness robots are played through the brains' own calls: EP4 at `RHSimCommandlet.cpp:11112-11160`, the rover harness at `:10152-10190`. The playtest holds 16x and never runs `EraStep` (`RHPlaytest.cpp:1246-1250`).

---

## 1. Robots below (carried from D3, with D4's changes)

The director's (c) moved D3 section 2, section 3, REVISION items 1-4 and 6, and optional items O3, O7, O8 and O9 into D4 "unchanged". They are carried here by reference to `d3-tunnels-design.md`. **Where this section differs, it is because capacity now exists, and this section wins.**

### 1.1 Fitting a lift: the car

- **New verb `FitLift`, `Value` = shaft Id.** It adds one **lift car** to that shaft.
  - A shaft has `LiftCars` in [0, `MaxLiftCars`]. `MaxLiftCars` is 4, a placeholder row, clamped to `RH::MaxLiftCarsCap = 8`.
  - The first car turns the shaft's hoist into a lift. Each further car adds capacity. This is the plan's "lift upgrade" (`going-deep-plan.md:77`).
- **Refusals,** each with a reason, re-run at execute time:
  1. unknown shaft;
  2. `Depth == 0` (nothing to serve);
  3. already at `MaxLiftCars`;
  4. short of Struct.
- **Bill.** `LiftFitStructKg` per car (400 kg Struct, placeholder), taken atomically at the uplink through the C4 R1 pattern (`TakeRoadStruct`, `RHSimRoads.cpp:75-109`, generalised to a `TakeStructForBill(Kg, Why)` helper).
  - The take charges the cargo ledger for any part taken from a floor (section 2.5).
  - It is instant: no site and no labour, so there is no new site kind and no era refusal.
- **What fitting buys now that lifts pay off** (D3's Q-A argument no longer holds):
  - Robots ride only fitted shafts (section 1.3).
  - Capacity: a hoist carries `HoistPlacesPerSol` (200), and a car `LiftPlacesPerSol` (1,000).
  - Speed. A hoist hand-off holds `HoistDwellSeconds` 30 plus `HoistSecondsPerFloor` 15 a floor. A lift costs `LiftDwellSeconds` 10 plus `LiftSecondsPerFloor` 4 a floor. People's per-floor cost falls from `CommuteShaftCmPerFloor` 6000 to `CommuteLiftCmPerFloor` 2000.
  - Every number is a placeholder row, "Placeholder, director review".
- **No power draw in D4.** A lift draw would change `LoadW`, and `RequestChargeWh` grants free energy when `GenW > LoadW` (`:11901-11904`), so it would shift every robot's charging. It is deferred to D5 with the depth costs (risk R9).
- **Derived state:**
  - `HasTransit()` is "any shaft has `LiftCars > 0`";
  - `HasRobotLifts()` (D3's name) is the same test;
  - `LiftVersion` goes up on every fit;
  - `TransitWaitVersion` goes up when any wait changes at a threshold (section 2).

### 1.2 The level, the ride and the fixed sub-step point

These are carried from D3 section 3, "the level" and "the fixed sub-step point", with O3.
- `RH::LevelOfZ(Z) = RoundToInt((Z - 50) / FloorHeightCm)`. The config read asserts `FloorHeightCm > 100`.
- Z is written only when a ride completes: `Z = L x FloorHeightCm + 50`.
- No new fragment: the explicit StateTree archetype stays untouched (C4 item 4). `FRHTaskFragment` gains `int32 TargetLevel`, set beside every `TargetCm` write.

**A ride leg on shaft S**, from portal level L1 to L2, is now three robot-local counters in `FRHRobotRoadPath` (runtime, in `RobotPaths`):
1. **`HoldStepsLeft = 10 x Wait_S`.** `Wait_S` is shaft S's whole-second wait for the current sol (section 2.3). The robot holds on the column at L1, drawing `DrawIdleW` and accruing no wear: a hold is idle, unlike a Haul leg (`RHRobotStateTree.cpp:272-275`).
2. **`RideStepsLeft = 10 x (LiftDwellSeconds + |L1-L2| x LiftSecondsPerFloor)`.** This is exact integer arithmetic, as in D3: 140 sub-steps for one floor.
3. **Z flips at the end of the sub-step that takes `RideStepsLeft` to 0.** The robot moves no further in that sub-step.

- The hold is read from `Wait_S` once, at boarding. A threshold crossed mid-hold does not change it, so the count is robot-local.
- Boarding also adds the ledger entry (section 2.5): 1 place for the robot plus `ceil(CargoKg / LiftKgPerPlace)`.

### 1.3 Movement, portals and the planner

Carried from D3 section 2 ("Portals", "Edges and costs", `PlanDeepRoute`, `FRHDeepNet`) and section 3 ("Both brains route through `RH::MoveAlong`").

**What D4 changes:**
- **Ride edge cost.** It is `SpeedMulPermille x LiftPlanSpeedCmPerS x (Wait_S + LiftDwellSeconds + |dL| x LiftSecondsPerFloor)`. The planner therefore steers around a crowded shaft from the next plan on.
  - `Wait_S` is frozen for the sol, so a plan is a pure function of sim state.
  - Paths in flight are never re-planned (C4 item 5).
- **The cache key.** `FRHDeepNet` is keyed on (`FloorCellsVersion`, `ShaftsVersion`, `LiftVersion`, `TransitWaitVersion`, `TramVersion`, `RoadGraph.BuiltVersion`, `TunnelsVersion`). `TunnelsVersion` is D3's, so a finished tunnel re-labels the floor graph.
- **Tram edges** join floor nodes (section 3).
- **`LegEdge` encoding.** `LegEdge < 0` means a ride (shaft `-LegEdge - 1`), and `LegEdge <= -1000` means a tram (line `-LegEdge - 1000`). Two places must branch on `< 0` first:
  - `MoveAlongRoads`' leg speed (`RHSimRoads.cpp:388`), where `GetRoadEdgeSpeedMul(negative)` would read a missing edge;
  - the traffic heat (`RHTrafficVisualizerSubsystem.cpp:508`), which counts any `LegEdge != 0`.
- **The `RobotPaths` replan key** is (`TargetCm`, `TargetLevel`) (O7). A level change can therefore never abandon a ride through float noise in Z (`RHSimRoads.cpp:368-376`).
- **The `MoveAlong` gate** (D3 section 3) is `!bRover && HasRobotLifts() && (LevelOfZ(Z) != 0 || TargetLevel != 0)`. Otherwise today's body runs verbatim (`RHSimRoads.cpp:27-45`).

**Who goes below.** This is D3's table, unchanged:
- Haulers and Fabricators go below.
- Maintenance units go below only to a halted patient (item 4).
- Excavators, Scouts and Rovers never go below.

### 1.4 Claims and targets (D3 REVISION item 1, carried, with O9)

- `IsRouteReachable(Level, Pos, Site)`: the site is on the surface, or its cell (`FindCarvedCellAt`) is in a dig with a fitted landing, or in the robot's own dig. A fitted portal column is a legal position.
- **`TryClaimHaul`.** A robot below needs both `From` and `To` reachable. A surface robot with lifts needs `From` at level 0 or reachable; its `To` may be anything, because an unreachable `To` takes the surface fallback.
  - Robots below may now collect from sources below. That ends the latent stall in which the site hopper picks a source below and no surface hauler can claim it (`:1301-1314` against `:11640`).
- **`TryClaimBuild`.** Below, the site must be reachable. Surface robots keep `AreLevelsLinked`.
- **One ranking key while `HasRobotLifts()`** (O9): `DistXY(robot, target XY) + |RobotLevel - TargetLevel| x LiftPlanCm`. `LiftPlanCm` is a whole-cm row (1500 cm, placeholder: the cost of one floor at the planning speed, without waits). Ties go to task order.
  - The key is deliberately wait-free, so a claim never depends on the sol's waits.
- **`GetRobotTarget(Site, RobotLevel, RobotPos, OutCm, OutLevel)`** returns the site at its level when it is reachable. Otherwise it returns `GetApproachPoint(Site, 0)` with `OutLevel = 0`, always. A robot is never targeted into rock or onto a bare column.
  - `HaulLoad(…, int32 RobotLevel = 0, int32* OutDropLevel = nullptr)` uses it. Harness calls keep their defaults.
- **The head hand-off is retired** for every site a fitted landing reaches. It is kept for sites in rock, digs with no fitted landing, and colonies with no car. With transit on, the kept hand-off is charged (section 2.4).

### 1.5 Charging (D3 REVISION item 2, carried and extended for holds)

- **`DeepReserveWh`** is computed on the path `PlanDeepRoute` returns to the pad `EnterState` will pick (`FindNearestChargePad(Pos, …, 0)`, `RHRobotStateTree.cpp:327-329`):
  - `DriveWh(lattice cm + surface cm)`;
  - plus `DrawMoveW x RideSeconds / 50 x 1.25`;
  - **plus `DrawIdleW x (HoldSeconds + TramHoldSeconds) / 50 x 1.25`**, where the holds are the frozen waits on that path. This is new in D4.
  - There is no minimum over landings.
  - The memo is per entity on (level, cell or portal, `PadId`, the `FRHDeepNet` key). It is never saved.
- **Descent.** While a ride is next or in progress (`GetRideState(Entity)`), the reserve is evaluated as if the robot stood at the ride's destination landing, plus the hold and ride time left. This runs at boarding and on every hold and ride sub-step.
  - A descent that fails fires the seek from the column at the boarding level.
  - An ascent is never broken off.
- **Both brains.**
  - StateTree: the deep branch is taken when `Level < 0` or a ride is next or in progress. Its mid-delivery exemption (`RHRobotStateTree.cpp:456-467`) is then not taken.
  - Legacy: seek when `Current != Charge && ((!bMidDelivery && ChargeFrac < seek) || bDeepLow)` (`RHRobotTaskProcessor.cpp:74-77`).
  - Both branches are untaken at level 0 with no ride, so today's code runs.
- **The hand-off hold (section 2.4) is mid-delivery.** A hauler holding at the head keeps today's exemption only while `ChargeWh >= DrawIdleW x HoldSecondsLeft / 50 x 1.25 + DriveWh(head to pad)`. Otherwise it seeks, and loses its load by today's hauler rule. A rover returns its load by `ReturnHaulCargo`, as today.

### 1.6 Interrupted rides and the load repair (D3 REVISION item 3, carried)

**Interrupted rides.**
- A charge seek, a vanished task or a reset fragment during a hold, a ride or a tram hold discards the counters.
- A halt gate or an era park freezes them.
- **In every case the robot stays at its boarding XY and level, with Z unchanged.**
- A fitted portal column, or a tram stop cell, is a legal robot position: for the planner, for `IsRouteReachable`, for repair, and it is not counted in `DeepOffLattice`.
- **No capacity is reserved, so nothing is released.** A dead rider pins nothing; this is the point of decision 2.

**Save.**
- `CollectRobotStates` writes `Level = LevelOfZ(Z)`. Paths, holds and rides are not saved (C4 R8).
- In-flight cargo returns to its source at save, as today (`:12294-12334`). A robot saved mid-ride reloads on its column at its boarding level, re-plans, holds again for the current sol's wait, and rides.
- Ledger entries already added are not undone. A ride counted twice across a save is one place of over-count at most, and it is stated.

**The load repair** (replaces D3 section 7's). The saved `Level` is kept in a list beside the `Respawned` handles (`:12901-12911`). The repair runs **after**:
- the floor table, the shafts (`:12922`, `:12925`);
- the D2 warning repairs (`:12926-12953`) and `RebuildShaftDerived` (`:12954`);
- D3's tunnel block install;
- and D4's transit block install.

It never reads the previous colony. In order, each with a Warning:
1. `Level != LevelOfZ(Z)`: set `Z = Level x FloorH + 50`.
2. `Level < 0` on a fitted portal column or a tram stop at that level: kept.
3. `Level < 0` on a carved cell in a dig with a fitted landing: kept.
4. Any other `Level < 0`: moved to the surface deploy ring, through a new sim-only `URHAgentSubsystem::SetRobotPosition`.

### 1.7 Repair across levels (D3 REVISION item 4, carried)

- Halted means `Wear >= WearHaltThreshold`.
- `TryClaimRepair` (`:12059-12094`) allows a cross-level claim, in either direction, only for a halted, route-reachable patient.
- The repair tick:
  - same level: today's `MoveToward` re-track;
  - levels differ and the patient is halted: one `MoveAlong` to the patient's fixed position, with `TargetLevel`;
  - levels differ and the patient is not halted: `ReleaseRepairClaim` and reset.
- `ApplyRepairAt` requires equal levels.
- A robot drained (not worn out) below is never a repair target, as on the surface today. The reserve in 1.5 is the only guard.

### 1.8 The opt-in `lifts=1` playtest (D3 REVISION item 6, carried)

- **Where.** `RHPlaytest.cpp` parses options at `:1924-1936`; `lifts=1` sits beside `nogrowth=1` (`:1927`). It is off by default and outside the byte-identical comparison.
- **What the bot does.** Once -1 is rated it fits one car on shaft 0, once. If nothing below needs a robot by then, it orders one Lab module on a -1 cell. The run lasts at least 20 sols.
- **What it asserts,** each on a `[Playtest] lifts:` line, with a failure as an Error line:
  - `DeepOffLattice == 0`;
  - no robot below sits at charge 0 undocked;
  - `DeepDeliveries >= 1` and `DeepBuildsCompleted >= 1`;
  - mass is conserved: loaded = credited + returned + lost, to 1e-3 kg;
  - a save and load at sol 10 keeps the multiset of (DefName, Level).
- **New in D4,** two more assertions:
  - `TransitThresholds` equals the sols run since the fit, plus or minus one;
  - the ledger's cargo kg for each sol equals the sum of what `HaulUnload` and boarding added, recomputed independently by the bot from its own event log.

---

## 2. Capacity: one rule for people and cargo

### 2.1 Units

- **A place** is one person, one robot, or `LiftKgPerPlace` (100 kg, placeholder) of cargo, rounded up per move.
- **Capacity a sol:**
  - a hoist (an unfitted shaft): `HoistPlacesPerSol` (200);
  - a lift: `LiftCars x LiftPlacesPerSol` (1,000 a car);
  - a tram line: `TramPlacesPerSol` (600).
- All of it is `int64`. No float enters a load, a capacity or a wait.

### 2.2 The ledger (cargo and robots)

- **Per shaft and per tram line,** `FRHTransitLedger { int32 Sol[3]; int64 Kg[3]; int32 Places[3]; int32 WaitSec; }`.
- **A writer** (a robot event or a world-side take):
  - computes `EventSol = FloorToInt32(Clock->GetSimSecondsTotal() / 1200)` and its slot `EventSol % 3`;
  - if that slot's tag is older, resets the slot to (`EventSol`, 0, 0);
  - then adds whole kg (`RoundToInt64(Kg)`) and places.
- Integer addition commutes, so **the totals do not depend on which robot runs first.**
- A robot's event is tagged with the clock total of the frame it runs in. Near a boundary, a frame schedule can move an event into the next sol's bucket. Totals are conserved; only the bucket edge moves. This is the same "deterministic for a fixed frame schedule" class as every claim today (D3 R1), and it never depends on entity order.

### 2.3 The threshold step: `StepTransit`

- **Position.** It is a new step at the same index in `StepSim` and `EraStep`: straight after `StepUplink`, before `StepTaskBoard` or `EraLogistics`. Its first line is `if (!HasTransit()) return;`.
- **Threshold.**
  - `SolNow = FloorToInt32(GetStepEndSeconds() / 1200)`.
  - On `SolNow > TransitCheckedSol` it rolls once to `SolNow` and sets `TransitCheckedSol = SolNow`. That is the `StepCityGrowth` pattern.
  - A jump of several sols rolls once, to the latest sol. The ledger reads only its own tags, so no sol is double-charged.
  - The first `FitLift` sets `TransitCheckedSol = current sol`, with every wait 0.
- **The roll, in ascending shaft Id:**
  - `PeopleLoad_S` from the commute flows (section 2.6). It is 0 when the ramp is 0, and then the flows are never touched.
  - `CargoPlaces_S = Places[slot of SolNow-2]` when that slot's tag is `SolNow-2`, else 0.
  - `Load_S = PeopleLoad_S + CargoPlaces_S`, and `Cap_S` from 2.1.
  - `WaitSec_S = 0` if `Load_S <= Cap_S`. Otherwise `min(MaxWaitSec, ceil((Load_S - Cap_S) x WaitSecAtDouble / Cap_S))`, all in int64.
  - The rows are `WaitSecAtDouble` 120 (the wait at twice capacity) and `MaxWaitSec` 600 (placeholders).
- **Then the tram lines in ascending Id:** the same formula on their own ledger, with `TramPlacesPerSol`.
- If any `WaitSec` changed, `++TransitWaitVersion`.
- **Why a two-sol lag for cargo.**
  - The world's threshold step and the robots' batch are not ordered within a frame. So events tagged `SolNow-1` can still arrive in the frame where the world crosses into `SolNow`.
  - Events tagged `SolNow-2` cannot: a frame spans at most 120 s, a tenth of a sol.
  - This is the D3 review's "sol s would use rides counted in sol s-2" (`d3-tunnels-design.md:423`).
- **The people part needs no lag.** It is world-side, a pure function of state at the threshold step, and identical in both bands.
- **Robots read only the frozen `WaitSec`.** They never decide anything that depends on another robot's progress.

### 2.4 The hand-off pays the same shaft (D3 review item 8)

With `HasTransit()` true, the fallback hand-off at the head is no longer instant.
- **Which shaft.** S is the shaft `GetApproachPoint` already uses: `ServingShaft(min(levels), CmToLattice(origin, site))` (`:2678-2700`).
- **The hold.**
  - When a hauler, or a rover (section 4), reaches a head with a cross-level drop, it holds `10 x (WaitSec_S + Dwell(S) + |L| x SecPerFloor(S))` sub-steps.
  - `Dwell` and `SecPerFloor` are the hoist's (30 s, 15 s) or the lift's (10 s, 4 s).
  - The hauler waits until the cage has taken its load down. Then `HaulUnload` credits exactly as today, and the ledger gains the cargo's places (no robot place).
- **The ride versus the hand-off.**
  - A hand-off is never cheaper in capacity than a ride: the same wait, the same places, and the same cage time.
  - A hand-off on a hoist is slower than a ride in a fitted car: 60 s against 18 s for 2 floors, before walking.
  - So the review's dominance ("the hand-off dominates for ever") ends. The remaining advantage of the hand-off is that the robot does not walk below, and it exists only where no fitted landing reaches.
- **Inert without transit.** The hold branch's first test is `HasTransit()`, so the `HaulUnload` timing is today's.
- **Fabricator head labour is not charged.** Labour moves no mass. A Fabricator works an unreachable site from the head as today, and a reachable one by riding. This is stated, not a gap.
- **World-side instant takes** charge the ledger of the source building's serving shaft, with no hold:
  - road and `FitLift` bills (`TakeStructForBill`, `RHSimRoads.cpp:75-109`);
  - growth bills (`RHSimGrowth.cpp:266`).
  
  They run inside world steps, so they are deterministic in both bands.
- **The site hopper's source scan is unchanged** (`:1297-1314`). The delivery it posts pays when it moves.

### 2.5 What enters the ledger

| Move | Shaft or line | Places | Who writes |
|---|---|---|---|
| A robot boards a lift | S | 1 + ceil(cargo / 100) | the robot, at boarding |
| A robot boards a tram | line | 1 + ceil(cargo / 100) | the robot, at boarding |
| A head hand-off unloads | serving S | ceil(kg / 100) | `HaulUnload` |
| `TakeStructForBill` from a floor | serving S of the source | ceil(kg / 100) | the world, in the order |
| Growth bill from a floor | serving S of the source | ceil(kg / 100) | the world, in `StepCityGrowth` |

Every row is skipped when `!HasTransit()`.

### 2.6 People: the commute wait from the flows (Q4a)

**Inert by two gates:**
- `!HasTransit()`: `GetCommuteDistanceCm` and `DeriveCommuteFlows` take today's code, line for line;
- `Ramp == 0`: every reader returns the zero result before touching a cache (unchanged), and `StepTransit` does not touch the flows.

**With transit on, each underground end pays its shaft.** The rows:
- `PerFloor(S)` is `CommuteShaftCmPerFloor` (6000) for a hoist and `CommuteLiftCmPerFloor` (2000) for a lift.
  - The 6000 row is re-described as "a hoist ride and its usual wait, uncrowded", so the crowding wait is not counted twice (the `RH_Config.csv:225` note changes; the value does not).
- `WaitCm(S) = WaitSec_S x CommuteWaitCmPerSec`. The row is 100 cm/s (walking pace, placeholder).

**Per trip:**
- **Floor to surface.** S = `PickCommuteShaft(L, P)` (wait-free, today's rule). `Dist = |L| x PerFloor(S) + WaitCm(S) + CommuteSurfaceCm(head_S, P)`. Load: `2 x Count` places on S.
- **Floor to floor, different levels.**
  - Floors are one place (D2's floor-wide reach), so residents spread across every shaft that reaches the deeper floor.
  - Split `Count` over those n shafts in ascending Id: `Count / n` each, with the first `Count % n` taking one more.
  - Load: `2 x n_i` places on each.
  - `Dist` is the integer weighted mean `Σ n_i x (|dL| x PerFloor(S_i) + WaitCm(S_i)) / Count`, in int64.
  - If no shaft reaches it, shaft 0, as today.
- **Same floor, or surface to surface:** no shaft, and no change (R2 stays deferred).

**Why the choice is wait-free.**
- If the shaft choice read the waits, a crowd would move every trip to the other shaft next sol, and back the sol after.
- With the choice fixed, the wait only lengthens trips. The player's fixes are real:
  - another car raises `Cap_S`;
  - a second shaft nearer the surface workplaces takes the floor-to-surface trips;
  - any second shaft that reaches the floor halves the floor-to-floor load.

**No fixed point.**
- `DistCm` reads the frozen `WaitSec`.
- The threshold reads the trips' `Count` and the split rule, neither of which reads a wait.
- So flows depend on the last roll, and the next roll depends on the flows. That is a one-sol lag, not an iteration.

**Cache keys.**
- `FRHCommuteKey` gains `uint32 Transit = LiftVersion ^ (TransitWaitVersion << 16)`, or two fields; the builder's choice, as long as each bump moves it.
- With no car, neither version ever moves. So `CommuteFlowRebuilds` and `CommuteDijkstraRuns` (X13, X14) cannot move.
- The position memo stays position-pure: lift and wait terms are added at the `GetCommuteDistanceCm` level, never inside `CommuteSurfaceCm` (`:97-168`).
- `-RHVerifyCaches` gains `Trips[].DistCm` and the per-shaft people load in its compare (`:381-382`), so a stale wait cannot pass silently.

**`FRHCommuteTrip`** gains `int32 ShaftId` (INDEX_NONE for no shaft, and for a split trip; the split's shares are recomputed in the threshold pass from the same rule). With no transit it is always INDEX_NONE and nothing reads it.

**Effects** go through the existing terms only:
- `bLong` (`DistCm > CommuteLongCm`);
- the Hope penalty scaled by the ramp;
- the R2 work hooks.

No new Hope term.

---

## 3. Trams (aggregate)

### 3.1 The line

**New verb `LayTramLine`, `Value` = tunnel Id.** The tunnel's two chain ends become the stops: A, and B or the last chain point for a dead end. A tram line is a vehicle service on that tunnel. It is not a new shape.

**Refusals,** re-run at execute time:
1. no lift car in the colony ("trams carry robots below; fit a lift first");
2. the tunnel is unknown, cancelled, or not finished (`GetTunnelUncarved > 0`, D3's API);
3. the chain is shorter than `TramMinCells` (12, placeholder: 120 m);
4. the tunnel already has a line;
5. more than `MaxTramLines` (8, clamped to `RH::MaxTramLinesCap = 32`);
6. short of Struct: `TramStructKgPerCell` (20 kg) x the chain length, through `TakeStructForBill`.

**`FRHTramLine`** (saved): `Id`, `RegionId` (0), `Level`, `TunnelId`, `StopA`, `StopB` (lattice points), and its `FRHTransitLedger`.
- The name differs from D3's tunnel "line" strings.
- It is keyed on the tunnel Id and lattice points, never on chain ordinals (O1).
- `TramVersion` goes up on a lay.
- No removal in D4.

### 3.2 What a tram does

- **For robots.** `FRHDeepNet` adds one edge between the two stop cells, with cost `SpeedMulPermille x LiftPlanSpeedCmPerS x (TramHeadwaySec / 2 + WaitSec_line + ceil(LenCm / TramSpeedCmPerS))`.
  - `LenCm` is the chain's lattice length.
  - Rows (placeholders): `TramHeadwaySec` 60 and `TramSpeedMps` 10.
- **A robot that plans the edge:**
  - holds at its stop `10 x (TramHeadwaySec / 2 + WaitSec_line)` sub-steps;
  - then rides `10 x ceil(LenCm / TramSpeedCmPerS)` sub-steps;
  - then **jumps to the other stop's cell at the end of the last ride sub-step**, the same fixed-point rule as a lift's Z flip.
  - It draws `DrawIdleW` while holding and `DrawMoveW` while riding, and boarding writes the line's ledger.
  - Aggregate only: no car is simulated, and riders never interact.
- **Where trams pay.** A hauler walks lattice at 4 m/s. A 300 m tunnel walks in 75 s and trams in 60 s (30 + 30) uncrowded, so trams pay on long lines and on cargo-heavy routes between digs.
- **Choosing between a crowded shaft and a tram.** The planner already prices the shaft's `WaitSec`. A robot whose shaft 0 is crowded will ride shaft 1 into dig B and tram back, when that is cheaper. That is "a tram line fixes it", for cargo.
- **For people: not in D4.** A floor home is `HomeLevel` only and a floor seat is its level (`RHSimCommute.cpp:257`, `:262`). No commute can see two digs on one floor (R2), so a tram cannot change a commute number until homes and seats get a cell (D6). Director question Q-B.
- **Era:** no tram term. Robots park (section 5).

---

## 4. Rovers: never below; the head hand-off is their underground leg

- **The decision.** Rovers stay on the surface for good. The plan's "robots and rovers go down" (`going-deep-plan.md:37`) is closed as a deviation.
- **Why.** Every rover rule is a surface rule:
  - road-served pads at `Level == 0` (`RHSimRovers.cpp:50`, `:68`);
  - the XY-only last-leg test (`:34-38`);
  - the off-road crawl profile;
  - the reserve proof, which bounds a surface straight-line crawl (`RHSimWorldSubsystem.cpp:169-179`).

  Below there is no "off-road". A rover below would need every one of them redone for a vehicle that is three times a hauler's draw. The tram is the underground bulk mover, and the head is the interchange.
- **The change, transit on only.** `IsFreightRoute` (`RHSimRovers.cpp:83-97`) drops its `GetSiteLevel(T.To) != 0` refusal (`:85`). The drop is already `GetApproachPoint(T.To, 0)` (`:90`, `RHSimWorldSubsystem.cpp:11706`).
  - The rover holds at the head as in 2.4, with `ceil(kg / 100)` places of up to 1,000 kg (10 places), and `HaulUnload` credits.
  - `RefreshFreightFlags` also re-derives on `LiftVersion` (`:124`), so freight is re-classified at the first fit.
  - With no transit, `:85` keeps refusing, and W1's `FindOutpostDestination` (`:1810`, `C.Level != 0`) is untouched in every case.

---

## 5. Era parity

**People.** `StepTransit` sits at the same index in both spines and reads the same flows. People loads and waits are therefore identical at every threshold in both bands, and the commute stays a pure function of state and the frozen waits (C6's argument, extended). X16-style exact equality holds whenever the cargo part is equal.

**Cargo.**
- Robot ledger events happen only in the agent band. Era teleports level-matched and moves no cross-level cargo (`:797`, `:901`, `:928`).
- But **the only cross-level robot cargo is construction delivery** (`:1297-1330`), and construction refuses era (`:612-619`). No D4 rule posts a new cross-level task: D3 section 4, "No posting rule changes", carries.
- So over any stretch that era can run, the robot part of the ledger is empty in both bands, except the tail of sol s-2's construction entries. Those are at most two sols of waits that era also reads, from the same saved ledger.
- World-side charges (bills, growth) run in both bands.
- **Deviation, logged as C4 and D2 did:** "era teleports past lifts and trams; a robot hold or ride is not paid in era".

**Robots** park in era on their column, stop or cell, with every counter frozen (`RHRobotBrainProcessor.cpp:46-50`). There is no new era refusal: holds and rides are robot-local and resume when the band drops.

**The 5% bar,** with the fix-A method (drive the brains through their own calls on harness transforms):
- **TR17:** a paired 10-sol run with transit on, the ramp open (120 colonists, floor homes on -1 and -2, surface seats), and two harness haulers with sources below. The era and agent legs must agree within 5% on Dug, Struct and Hope.
  - People waits must be **equal** at every threshold when no construction is open.
  - The agent leg's robots must surface and none may halt below (O8).
  - `DeepPlanRuns > 0` and `TransitThresholds == 10` in both legs.
- **EP4's 2-point headroom is untouched,** because EP4 fits no car.

---

## 6. Inertness: what the gate keeps identical

**The gate:** `HasTransit()` (any `LiftCars > 0`). No suite and no playtest bot issues `FitLift`, and growth issues no uplink verbs.

Branch by branch, with no car:

| Site | With no car |
|---|---|
| `StepTransit` | returns at its first line |
| `MoveAlong` | today's body (`RHSimRoads.cpp:27-45`) |
| Claims and targets | the cross-level eligible set is empty, so today's scans and keys run (`:11628-11658`, `:11743-11772`), as does `HaulLoad`'s drop (`OutDropLevel` 0) |
| `HaulUnload` and the hold | no hold, no ledger write |
| `TakeStructForBill` | the same take order as `TakeRoadStruct`, no ledger write; the road bill path is byte-identical |
| Growth bills | no ledger write |
| The commute | `GetCommuteDistanceCm` and `DeriveCommuteFlows` take today's lines; `FRHCommuteKey.Transit` stays 0 |
| `IsFreightRoute` | `:85` still refuses floor drops |
| The charge reserve, repair, the tram verb, the visualizers | each new branch tests `HasTransit()`, a level below 0 or a ride |
| Save | every robot writes `Level = LevelOfZ(50) = 0`, the same bytes; the new block is 20 bytes (section 7), hidden by the `(N bytes` mask (`diff_battery.py:11`) |

**The bar:**
- **Battery:** the baseline plus every suite after D3 (48 runs), plus `-transit`, is 49 runs, 0 not green.
- **`diff_battery.py` against `battery_post_d3`:** only the save-version lines. That is 9 out and 9 in, as D3 counted them:
  - `traffic.log` X15 x2;
  - `deep.log` K6 x3;
  - `framing.log` e5 x1;
  - `farice.log` F16 x2, including the literal "the v45 save loads";
  - `shafts.log` SH12 x1;
  - plus any version line `tunnels.log` prints.
  
  Recount literally at build time; any other line fails the gate. The `Contains("/45")` conditions move to `/46` with them.
- **Playtests:** the default and `districts=1` 110-sol runs are byte-identical under the mask against the post-D3 runs.
- **`-RHCommuteAudit`:** its margin line is unchanged in `-percapita` and `-arrivals`, as a check on the gate.

---

## 7. Save: v46, strict

**Version.** `RHSaveVersion` 45 (D3) becomes 46, with the comment chain extended. If D3 lands later or takes another number, D4 takes the next free one, and every version line here follows it.

**Robots.** `CollectRobotStates` writes `Level = LevelOfZ(Z)`. The record layout is unchanged, since `Level` has been serialized since the robot record began.

**The transit block** goes straight after D3's tunnel block, so K6's offset 20, SH12's `BlockAt` and D3's block do not move. Read it into locals before teardown.

```
int32 LiftShaftCount;   per: int32 ShaftId, int32 Cars       (ShaftId ascending, Cars in 1..MaxLiftCarsCap)
int32 TransitCheckedSol;                                      (INDEX_NONE until the first fit)
int32 LedgerCount;      per: int32 ShaftId, 3 x (int32 Sol, int64 Kg, int32 Places), int32 WaitSec
int32 NextTramId; int32 TramCount;
                        per: Id, RegionId, Level, TunnelId, StopA.X, StopA.Y, StopB.X, StopB.Y,
                             3 x (int32 Sol, int64 Kg, int32 Places), int32 WaitSec
```

- The default colony writes 20 bytes: five zeros, with `TransitCheckedSol` = -1.
- **The waits are saved.** They are the frozen verdict of the last threshold, and the people part was read from the state at that moment. Re-deriving them at load would not reproduce them mid-sol. The plan's "loads and waits are derived" is honoured for loads, not waits (risk R5).

**Structural refusals,** through `OutError` "Save transit block invalid: …", with no Error line:
- a shaft Id not in the loaded shafts, repeated, not ascending, or with Depth 0;
- Cars outside 1..`MaxLiftCarsCap`;
- a ledger for an unknown shaft;
- a ledger slot tag above `TransitCheckedSol`;
- a negative kg or places;
- `WaitSec` outside 0..`MaxWaitSecCap`;
- `TransitCheckedSol >= 0` with no car, or `< 0` with a car;
- tram Ids not ascending or not below `NextTramId`;
- `RegionId != 0`;
- a tram on an unknown, unfinished or cancelled loaded tunnel;
- a level that does not match the tunnel's;
- stops that are not the tunnel's chain ends;
- two trams on one tunnel;
- more than `MaxTramLinesCap`;
- counts against the remaining bytes.

**Warning repairs:** the robot repair of 1.6, after install.

**Load resets** (runtime only): `RobotPaths`, the reserve memo, `FRHDeepNet`, holds and rides. The checkpoints re-make themselves.

---

## 8. Presentation (RedHope; const accessors only)

These are carried from D3 section 8:
- robots shown on their floor (`SetViewLevel`);
- cab riders drawn by `GetRobotRide`;
- the fleet panel's Level column;
- the "N robots below" badge.

D4 adds:
- **The shaft card:**
  - "Fit a lift car (400 kg Struct)", with the reason when refused;
  - cars n/4, the load against capacity for the current sol, and the wait in seconds;
  - "hoist" for an unfitted shaft once transit is on.
- **The City panel:** one line per crowded shaft: "Shaft 0 is crowded: commutes through it take about N s longer. Another car or another shaft would help." It is placeholder copy, prevention-framed.
- **The cab:** riders queue visibly at the head while holding. The queue length is drawn from the riders' hold counters, not simulated.
- **The tunnel card:** "Lay a tram line (N kg Struct)", with the reason.
- **Trams:**
  - one capsule per line, drawn by phase from (`TramHeadwaySec`, `TramSpeedMps`, the line length), with nothing simulated;
  - robots riding a tram are drawn inside the capsule by `GetRobotTramRide(Entity, Line, Frac)`;
  - the traffic overlay tints each head and each line by `Load / Cap`.
- **New strings** are new wording for the director, rows after D3's in `docs/qa/2026-10-15/gate-d-wording.md`. Gate-D's final review does not cover them.
- **Captures** (`Saved/RHCapture/d4`):
  - one shaft with no car, before and after (identical);
  - a hauler in the cab;
  - a crowded head with a queue;
  - a tram line on floor -1.

---

## 9. Tests: new fails-loudly `-transit` suite (49 runs)

Robots are played through the brains' own calls on harness `FTransform`s with real `RH::MoveAlong`, as C6a's rover harness and EP4 do. Holds and rides are counted in sub-steps, with no distance timer.

**Inertness and data**
- **TR0 data.** Every new `RH_Config` row is in the live DT and equals the CSV:
  - `LiftFitStructKg`, `MaxLiftCars`;
  - `HoistPlacesPerSol`, `LiftPlacesPerSol`, `LiftKgPerPlace`;
  - `HoistDwellSeconds`, `HoistSecondsPerFloor`, `LiftDwellSeconds`, `LiftSecondsPerFloor`;
  - `LiftPlanSpeedMps`, `LiftPlanCm`;
  - `WaitSecAtDouble`, `MaxWaitSec`;
  - `CommuteLiftCmPerFloor`, `CommuteWaitCmPerSec`;
  - `TramMinCells`, `MaxTramLines`, `TramStructKgPerCell`, `TramHeadwaySec`, `TramSpeedMps`, `TramPlacesPerSol`.
  
  The clamps hold against the `RH::` caps, and `FloorHeightCm > 100` holds.
- **TR1 inert twin.** Twenty sols of the `-shafts` two-shaft colony, with a road, districts, 160 colonists, a construction site on -2 and harness haulers. It runs against a twin with the D4 branches forced off. Bit-identical:
  - stocks, Hope Total, `CommuteFlowRebuilds`, `CommuteDijkstraRuns`;
  - every `GetApproachPoint`, `HaulLoad` drop, `TryClaim*` result and `MoveAlong` sub-step count;
  - the save bytes, apart from the 20-byte block.

**Orders**
- **TR2 refusals,** one per item, each with its reason text:
  - `FitLift` 1-4;
  - `LayTramLine` 1-6;
  - a FitLift that would take Struct from a floor charges the ledger exactly `ceil(kg / 100)`.

**The capacity rule**
- **TR3 the threshold.**
  - The first fit sets `TransitCheckedSol`.
  - Exactly one roll per sol in the agent band (12,000 sub-steps) and in era (20 steps).
  - A three-sol jump rolls once.
  - The roll reads sol s-2's slot only when the tag matches.
- **TR4 the wait.** Hand integers at loads `Cap`, `Cap + 1`, `2 x Cap`, and `10 x Cap` (clamped at `MaxWaitSec`). One car against four. A hoist against a lift.
- **TR5 order independence (the core D4 check).**
  - Six harness haulers deliver below through two shafts for 5 sols.
  - The run is repeated with the entity order reversed, and with frame batches of 1, 7 and 32 sub-steps.
  - Every ledger slot, every `WaitSec` and the delivered kg must be **equal** across all runs.
  - The bucket edge is pinned too: an event tagged sol s+1 instead of s, in the frame that crosses the boundary, changes no wait for sols s+1 and s+2. That is what the lag is for.
- **TR6 people load.**
  - A floor-to-surface trip loads `PickCommuteShaft`'s shaft.
  - A floor-to-floor trip of 7 over 2 reaching shafts loads 8 places and 6 places (4 and 3 colonists).
  - A same-floor trip loads nothing.
  - Ramp 0 loads nothing, and `CommuteFlowRebuilds` does not move.
- **TR7 the commute wait.**
  - Hand-computed `DistCm` for hoist, lift and crowded lift.
  - The weighted mean for a split trip.
  - Exactly one flows rebuild per `TransitWaitVersion` bump, and none on a roll that changes no wait.
  - `-RHVerifyCaches` stays clean.
  - At 59 and 60 people: zero rebuilds and bit-identical Hope with transit on.
- **TR8 "a second shaft fixes it".** 400 floor commuters through shaft 0's hoist give a wait above 0 and long commuters above 0. Then:
  - fitting a car lowers it;
  - a second car lowers it more;
  - a second shaft reaching both floors halves the floor-to-floor load.
  
  Each step's Hope penalty goes down, and none goes up.

**The hand-off**
- **TR9 the hold.** A hauler at the head of a hoisted shaft holds exactly `10 x (Wait + 30 + 15 x floors)` sub-steps, then credits once. With a car: `10 x (Wait + 10 + 4 x floors)`. With no transit: 0, and the credit happens at the same sub-step as today. A charge seek mid-hold follows 1.5's rule.

**Robots below** (D3's TN8-TN16, carried and renamed)
- **TR10 planner:** D3's TN8 integer costs, plus a crowded shaft priced in and a tie to the lower shaft Id.
- **TR11 ride:** D3's TN9, plus a hold before boarding.
- **TR12 haul and build below:** D3's TN10, TN11, TN12 and TN12b. A pickup below by a robot below closes the source-below stall.
- **TR13 claims:** D3's TN13 and O9's one key.
- **TR14 charge:** D3's TN14 and TN14b with a seeded `FRandomStream` (O10), plus a hold at `MaxWaitSec` on the reserve path.
- **TR15 repair:** D3's TN15 and TN15b.

**Trams and rovers**
- **TR16 trams.**
  - A 20-cell finished tunnel: lay, plan, hold, ride, jump to the far stop at the last sub-step.
  - The ledger charges 1 + cargo places.
  - A crowded shaft 0 makes a robot plan shaft 1 plus the tram.
  - An unfinished or cancelled tunnel is refused.
  - People's commute numbers are unchanged by a tram (R2).
- **TR16b rovers.** With transit on, a 1,000 kg freight task to a -1 site is classified freight, ends at the head, holds, and charges 10 places. With no car it is not freight, as today.

**Era and save**
- **TR17 era parity:** see section 5.
- **TR18 save.**
  - A mid-ride robot reloads on its column at its boarding level and arrives.
  - A tram rider reloads at its boarding stop.
  - The ledger, the waits and `TransitCheckedSol` round-trip, and a load mid-sol gives the same next roll as a run that never saved.
  - One hand-made refusal for each structural rule.
  - A v45 file is refused.
  - The robot repair runs against the loaded floors over a different live colony (D3's TN16 (ii)), and a robot on -2 with no car goes to the ring.

**-scale** (outside the battery)
- 4 shafts x 5 floors x 210 cells, 4 cars on each, 8 tram lines, 100 robots below, 5,000 colonists.
- Budgets (about 20x a quiet run, measured as C6 did):
  - one `StepTransit` roll at most 1 ms (inside the 25 ms sol-boundary step);
  - cold commute flows with transit on at most 10 ms, and a JobsVersion bump at most 1 ms;
  - a cold `FRHDeepNet` with trams at most 3 ms;
  - 2,500 `PlanDeepRoute` calls at most 10 ms;
  - 10,000 warm reserve reads at most 1 ms, and 1,000 cold at most 10 ms.
- The one-shaft, no-car numbers do not move.

**Mutation checks** (stated in the commit):
- reading sol s-1's slot instead of s-2 fails TR5's boundary case;
- a wait-aware `PickCommuteShaft` fails TR8's monotonicity (it oscillates);
- dropping the hold's `HasTransit()` test fails TR1;
- ledger kg as double fails TR5's equality.

---

## 10. Risks I see

- **R1. The first fit is a cliff for a multi-shaft colony.** Every other shaft's hand-off gains a hoist hold (60 s for 2 floors), and its people get a capacity. The alert at the fit must say so. Q-A's (b) removes the cliff and moves every baseline.
- **R2. The cargo bucket edge depends on the frame schedule,** not on entity order. An event a frame before or after a boundary lands in the neighbouring sol. Totals are conserved and the lag absorbs it. TR5 pins both.
- **R3. A one-sol lag on people and two on cargo.** A crowd built this sol is felt next sol. That is honest for a "how busy was it" measure, but a player who fits a car sees the wait fall a sol later. The shaft card shows "next sol: N s".
- **R4. Wait-free shaft choice.** A crowd never shifts floor-to-surface trips to a nearer uncrowded shaft. That is deliberate (no oscillation), but a player may expect it. Q4a's fixes still work.
- **R5. The waits are saved state.** That breaks the plan's "waits are derived" (`going-deep-plan.md:77`) in letter. It is logged as a deviation with its reason: re-deriving mid-sol would not reproduce them.
- **R6. A ride counted twice across a save.** One place of over-count at most, stated.
- **R7. The hand-off hold makes haulers slower than today** once transit is on, even uncrowded: the cage time. That is the point of item 8, but it lowers construction pace below. Placeholder rows are the knob.
- **R8. People get no tram in D4** (R2, Q-B). The plan's "a tram line fixes it" holds for cargo only until D6.
- **R9. Lifts draw no power.** That is unrealistic. A draw would shift every robot's free-charge rule (`:11901-11904`), so it waits for D5's cooling power.
- **R10. Robot Z means level** (D3 R9). Any code that writes a robot's Z from a target silently teleports it. Review greps every `SetLocation` on a robot transform.
- **R11. D3's anchors move.** D3 adds about 600 lines to `RHSimWorldSubsystem.cpp`. Every anchor here is HEAD `b8ab469`'s; the builder re-anchors after D3 lands.
- **R12. The legacy brain** (`RH.BrainMode=0`) has no wear gate, no repair and no reserve today. D4 mirrors holds, rides and `bDeepLow` there and nothing else, as D3 item 2 did.

**Deviations to log in `docs/design-decisions.md`:**
1. Rovers never go below (`going-deep-plan.md:37`).
2. Trams carry no people until D6.
3. Waits are saved.
4. Era teleports past lifts and trams.
5. `CommuteShaftCmPerFloor` is re-described as the uncrowded hoist.
6. The gate is a player order, not the plan's "the ramp from 60 to 160 people stays" alone (`:78`). The ramp still scales every effect, and the gate adds a second condition.

---

## Director questions

**Q-A. When does lift capacity start to count?**
- **a (recommended): from your first lift car.** Until you fit one, every shaft is today's free, endless hoist, and every existing colony, test and playtest stays exactly as it is. The moment you fit one, the colony runs its shafts as a transit system. Every shaft then has a capacity, people and cargo share it, hand-offs at the head take the hoist's time, and a crowded shaft lengthens commutes. The catch: if you already have several shafts, fitting the first car also starts the clock on the others (an alert tells you).
- b: from the first bore. Q4a applies to every colony from day one, with no switch. But a vault's builds and its commutes change pace at once, and the battery's population tests (5,000 people through one shaft) and the 110-sol playtest move and must be re-cut.

**Q-B. Do people ride trams in D4?**
- **a (recommended): not yet; robots and cargo ride trams now, people from D6.** Today a home on a floor is just "floor -1", not a place on it. So a tram between two digs cannot change anyone's commute until homes and seats get a cell, which is D6's job.
- b: yes, by giving floor homes and seats a cell in D4. It moves D6's re-key forward: every level-keyed store, the save, and a battery at risk. That is two to three more sessions.

---

# PROFESSOR REVIEW (2026-10-21)

**Verdict: CLEARED WITH CONDITIONS.** The architecture holds:
- the first fitted car is the gate;
- per-sol waits are frozen at a world-side threshold;
- the ledger is commutative and integer, read with a lag;
- one global switch for every shaft (gating per shaft would bring back the free hand-off that D3 item 8 had to end).

Nothing in it needs re-architecting. But four claims the draft rests on are false against the code:
- how events are time-tagged (item 2);
- what TR5 can prove (item 3);
- era parity with robot bodies in the ledger (item 4);
- the one-pass commute split (item 5).

Two mechanisms have no home or no safe bound: the hand-off hold (item 6) and the reserve when a wait jumps (item 7). Write items 1-10 into a REVISION. D4 is then cleared to build without another round. If the director moves trams to D6 (Q-B), the doc only shrinks.

Anchors are read at HEAD `5b89cc1` (D3 committed, save v45). The tree is clean apart from this doc.

## What I verified and found sound

- **What switches it on.** Only a `FitLift` order. The playtest bot issues no such verb (D3 review: Bore, Excavate, Designate, BuildRoad, Survey, Dig, PaintDistrict, Build), growth issues no uplink verbs, and `LayTramLine` requires a car.
- **The Z invariant.** Every robot spawns at Z 50 (`RHSimWorldSubsystem.cpp:470`, `:2272`; `RHSimRovers.cpp:322`), and nothing writes Z afterwards:
  - `MoveToward` moves along `GetSafeNormal2D` (`RHRobotMovement.h:25`);
  - `MoveAlongRoads` writes X and Y only (`RHSimRoads.cpp:403-413`);
  - no RedHope code writes a robot's `FTransformFragment` (grep).

  `RoundToInt((Z - 50) / FloorH)` gives exactly 0 at Z 50 and exactly -1 at Z -350. It never truncates. Every default save writes `Level` 0, the same bytes as today.
- **The Level slot.** It is dead today, as the draft says. `CollectRobotStates` never sets it (`RHAgentSubsystem.cpp:188-215`). Load reads it at `RHSimWorldSubsystem.cpp:13518` and drops it.
- **The only cross-level cargo is construction delivery.**
  - The deposit, consumer and feed legs are level-matched (`:1066`, `:1155`, `:1247`).
  - The site hopper sources through `AreLevelsLinked` (`:1318`).
  - Construction refuses era (`:629-631`).
- **Load order.** Robots respawn (`:13514-13522`) before the floor table, shafts and tunnels are installed (`:13534-13540`) and before `RebuildShaftDerived` (`:13569`). Section 1.6 orders the repair correctly after all of them.
- **Save layout.** `WriteTunnelBlock` (`:12950`) is the last header block. A transit block after it moves neither K6, SH12 nor TN1 (`RHSimCommandlet.cpp:14012`). D3's block has no lift field (`tunnels.log:317`, "8 bytes, no lift field"), so `LiftShaftCount` is new and correct. Saving the waits is right: the people part cannot be re-derived mid-sol.
- **D3 carry-forward is complete:**
  - REVISION item 1 → 1.4 and TR12 (TN12b);
  - item 2 → 1.5 and TR14 (TN14b), with the legacy `bMidDelivery` override;
  - item 3 → 1.6 and TR18 (TN16 i-iii);
  - item 4 → 1.7 and TR15 (TN15b);
  - item 6 → 1.8;
  - O3 → 1.2; O7 → 1.3; O8 → TR17; O9 → 1.4;
  - the review's item 8 note (charge the hand-off) → 2.4.

  Nothing was dropped.
- **No entity-order dependence by design.** Robots add integers and read waits frozen per sol. Nothing a robot decides reads another robot's progress. The flaws are in how time is tagged, not in the structure (item 2).

## REQUIRED changes

1. **Re-anchor to `5b89cc1`.** D3 has landed. Every `RHSimWorldSubsystem.cpp` anchor in the draft is about 600 lines stale. For example:

   | Code | Draft says | HEAD |
   |---|---|---|
   | `HaulUnload` | `:11710` | `:12314` |
   | `GetApproachPoint` | `:2678` | `:2714` |
   | `TryClaimHaul` / `TryClaimBuild` | — | `:12232` / `:12347` |
   | `FindNearestChargePad` | — | `:12436` |
   | `RequestChargeWh` | `:11901` | `:12477` |
   | `TryClaimRepair` | — | `:12663` |
   | world `Tick` / `StepSim` / `EraStep` | — | `:519-560` / `:563` / `:733` |
   | load respawn | — | `:13514-13522` |

   Drop the "D3 is being built" framing in the anchors note and in R11.

2. **Tag every ledger event with its own step time, not the frame clock.**
   - **(a) World-side writers must use `GetStepEndSeconds()`** (`:2570-2572`), not `Clock->GetSimSecondsTotal()` (2.2). The world runs its whole batch after the clock has advanced the frame (`:525-535`). In era a frame holds up to 4 x 60 s (`RHSimClockSubsystem.h:34`). So a growth bill or road bill in the first era step of a frame would be stamped up to 180 s late. Bills near a boundary would then fall in different sols in the two bands, which breaks section 5's "world-side charges run in both bands".
   - **(b) Robot writers** (boarding, `HaulUnload` hand-offs) use their own sub-step end: `FrameEnd - (Steps - 1 - Step) x SubStepSeconds`, the same formula as `StepEndOverride` (`:532`).
     - Both processors set it on the sim before each tick: `RHRobotBrainProcessor.cpp:89-110` and the legacy loop at `RHRobotTaskProcessor.cpp:66`.
     - Harnesses set it too.
     - The bucket then depends on sim time, not on the frame schedule, and risk R2 goes away.
   - **(c) Fix the lag rationale.**
     - An agent-band frame is at most 32 sub-steps, 3.2 s (`RHSimClockSubsystem.h:29`). An era frame is at most 240 s and runs no robot.
     - `MaxCarrySimSeconds` (`:37`) is the accumulator carry, not a frame span. "A frame never spans more than 120 s" is wrong on both counts.
     - Keep the two-sol lag. It is needed because, under (b), the robot batch can run after the world's roll in the crossing frame (section 0: the batches are unordered).
     - Note that under the draft's frame-end tagging, every crossing-frame robot event is stamped SolNow. The "read s-1 instead of s-2" mutation would then pass, so the mutation list was not testing what it claims.

3. **Rewrite TR5. As written it asserts things the design does not deliver.**
   - **(a) The boundary claim is false.** "An event tagged s+1 instead of s changes no wait for sols s+1 and s+2" cannot hold. Sol s+2's roll reads slot s (2.3), so moving an event out of slot s changes the waits of sols s+2 and s+3. The lag makes a slot complete before it is read; it does not make the bucket edge harmless. Item 2(b) is what removes the edge dependence.
   - **(b) Contention.** With six haulers contending for claims, reversing the entity order changes who claims what: `TryClaimHaul` is first-come, nearest-first (`:12232-12262`). Trajectories, and so the slots, then differ legitimately.
   - **(c) No batching harness exists.** Every harness steps one robot sub-step and then one world step (`AgentOnce`, `RHSimCommandlet.cpp:10959-10964`). Batches of 7 and 32 need a new driver: K robot sub-steps, then K world steps with `StepEndOverride` set as in `Tick`, run in both orders.
   - **The new TR5:**
     - a contention-free fixture (one pre-assigned task per hauler);
     - batches of 1, 7 and 32, times robots-first and world-first, times reversed entity order;
     - every slot, every `WaitSec` and the delivered kg are equal;
     - plus a boundary case: an event at sub-step 11,999 of sol s lands in slot s under every batching.

4. **Charge cargo only; a robot's body is 0 places.** Section 5 is false otherwise.
   - **Why bodies break parity.** Sections 1.2 and 2.5 charge one place per robot boarding. Robots ride only in the agent band (they park in era, `RHRobotBrainProcessor.cpp:46-50`). With transit on, they ride for reasons that move no cross-level cargo:
     - to surface and charge (pads are level 0);
     - for repair rides;
     - for floor-level hauls. Every non-construction haul is level-matched, and `TryClaimHaul`'s same-level rule (`:12244`) means no robot has ever claimed a floor-to-floor haul. At the first fit, robots below start taking them.

     So "the robot part of the ledger is empty in both bands" is false. The agent band's waits exceed era's, and that flows into `DistCm`, then `bLong`, then Hope. TR17's "people waits must be equal" passes only because its loads sit far below capacity.
   - **The fix.** Q1a says "a lift has a **cargo** capacity". Count `ceil(CargoKg / LiftKgPerPlace)` and nothing for the robot. Outside construction (which refuses era), the ledger then holds only world-side bills, which are identical in both bands, and section 5's equality becomes true.
   - **Also state in 1.4 and R1** that the first fit opens floor-level hauls to robots below. That is new behaviour at the fit.

5. **The commute split cannot be computed in one pass.**
   - `DeriveCommuteFlows` fixes `DistCm` and `bLong` when a trip is created, on its first colonist (`RHSimCommute.cpp:281-310`). It pushes `Dists` and `LongFlow` per colonist in the same pass as `++Trip.Count` (`:320-332`).
   - The weighted mean over `Count`, and "the first `Count % n` take one more", both need `Count` before the first colonist is counted.
   - **Fix:** colonist k of a floor-to-floor trip (iterated in Id order) uses shaft `S_(k mod n)`.
     - It is the same split, in one pass.
     - Each colonist's distance is exact for its shaft, with no division, and `bLong` is per colonist.
     - The threshold pass recomputes the shares from `Count` by the same rule.
     - A specified two-pass derive is the alternative.
   - `Dists` is already compared by `-RHVerifyCaches` (`:380-381`), so "gains `Trips[].DistCm`" adds nothing; pin the per-shaft people load instead.

6. **Give the hand-off hold a home.**
   - **The gap.** Section 1.2 keeps hold counters in `RobotPaths`. A surface hauler at the head usually has no entry there:
     - with no built road it moves by `MoveToward` (`RHSimRoads.cpp:42-45`), which never touches `RobotPaths`;
     - with roads, `MoveAlongRoads` removes the entry on arrival.

     `FRHTaskFragment` has no counter (`RHAgentFragments.h:62-79`), a new fragment is barred (C4 item 4), and both brains unload on the arrival sub-step (`RHRobotStateTree.cpp:229-235`, `RHRobotTaskProcessor.cpp:257-261`).
   - **Specify:**
     - **(a) Storage:** a runtime per-entity map in the sim, never saved. At save, cargo returns to its source as today.
     - **(b) Phase:** stay in Phase 2, so both mid-delivery tests (`RHRobotStateTree.cpp:456`, `RHRobotTaskProcessor.cpp:76`) see the hold and 1.5's last bullet applies.
     - **(c) Draw:** the Haul case sets `DrawMoveW` for every phase (`RHRobotStateTree.cpp:205`), so a hold accrues wear unless its branch sets `DrawIdleW`.
     - **(d) Knowing it is a hand-off:** `HaulUnload` (`:12314`) does not know the robot's level. Add `int32 RobotLevel = 0`. It writes the ledger only when transit is on and the site's level differs.
   - **TR9** runs the hold in both a no-road and a road colony.

7. **Price ascent holds at `MaxWaitSec` in the deep reserve.**
   - **The defect.** A robot below can cross a threshold while it walks to its landing. At the column it reads the new wait, up to 600 s.
     - For an RC-H that hold costs 5 W x 12 sol-hours x 1.25 = 75 Wh (`RH_Robots.csv`: idle 5 W, move 80 W).
     - `DriveWh`'s slack is 0.5 x `DrawMoveW` = 40 Wh (`RHRobotStateTree.cpp:420`).
     - So a robot at its reserve under wait 0 can drain in the queue. A drained robot below is never repaired (D3 item 4 note), so D3 item 2's route-true principle fails.
   - **The fix.**
     - Ascent holds are priced at `MaxWaitSec`. That is at most 75 Wh for a hauler, and it takes `TransitWaitVersion` out of the memo key.
     - Descents may keep the frozen wait, because the seek fires on the surface.
     - TR14 adds the case: wait 0, a threshold to `MaxWaitSec` mid-walk, and the robot docks.

8. **The ramp does not scale the robot waits; fix it or say so.**
   - Deviation 6 says "the ramp still scales every effect". It scales the Hope penalty and the work hooks (`c6-traffic-design.md:363-364`).
   - But `PeopleLoad` enters every wait, and with it every robot hold and every hand-off, at full weight from 61 colonists. At 60 → 61 a hoist's load jumps by twice the floor commuters.
   - **Recommended:** `PeopleLoad_S = 2 x Count x RampPermille / 1000` (int64, floored), with `RampPermille = RoundToInt(Ramp x 1000)`. It is continuous, exactly 0 at 60 or fewer, and keeps the plan's D4 bar ("the ramp from 60 to 160 people stays") for robots too.
   - Otherwise, correct deviation 6 and R1.
   - TR6 pins 60, 61 and 160.

9. **Save details.**
   - **(a) Write and keep `Level` correctly.**
     - `URHAgentSubsystem` has no `FloorHeightCm`. Set `R.Level` in `SaveColony` right after the collect (`:12893-12897`), or pass it in.
     - On load, append the kept level only when `SpawnRobotTracked` returns a valid handle (`:13519-13522`, `:497-501`). Otherwise a missing def row shifts every later robot's level.
   - **(b) The "slot tag above `TransitCheckedSol`" refusal can reject a legitimate save.**
     - Under item 2(b), the robot batch can stamp `TransitCheckedSol + 1` before the world rolls in the crossing frame.
     - The autosave fires from `OnSolElapsed` inside the clock tick, before the world's steps.
     - A strict refusal of the player's own current-version autosave is data loss. Allow `+1` and pin it in TR18 with a save in the crossing frame.
   - **(c) The default block** is four zeros and one -1, not "five zeros".

10. **The diff allowance against `battery_post_d3` is 11 out and 11 in, not 9.**
    - The lines: `traffic.log:937`, `:938`; `deep.log:590`, `:591`, `:596`; `framing.log:576` (not 582); `farice.log:7075`, `:7089`; `shafts.log:1412`; `tunnels.log:317`, `:1718`.
    - The conditions that move to 46:
      - `Contains("/45")` at `RHSimCommandlet.cpp:10817`, `:12091`, `:12551`, `:13664`, `:14451`;
      - `Ver == 45` at `:10811`, `:12091`, `:12507`, `:14012`, `:14930`.
    - TN1's "no lift field" stays literally true of the tunnel block. Edit only its version.

## Optional changes

- **O1. Cut section 4's rover change.** Keep `IsFreightRoute`'s refusal (`RHSimRovers.cpp:85`) in every case.
  - The rover's head hold feeds nothing: trams carry robots, not head cargo.
  - It touches C6a and W1 code.
  - A new `LiftVersion` compare in `RefreshFreightFlags` (`:117-129`, "starts invalid") must start at today's value, or it re-derives once at load and moves the playtest.
  - Floor freight stays hauler work.
- **O2. Boarding near a boundary.** A robot whose sub-step is past the boundary but which runs before the world's roll reads the previous sol's wait. Either state it (frame-schedule class, not entity order), or keep `(WaitSec, PrevWaitSec)` and read by the robot's sub-step sol.
- **O3. Era threshold timing.** Era steps end on 60 s marks offset from the agent band's (`RHSimClockSubsystem.cpp:19-23`), so the era roll can see up to 60 s more state. Say "identical when nothing changes in that window".
- **O4. `PickCommuteShaft` ignores lift against hoist.** A car on shaft 1 moves no trip whose cheapest head is shaft 0. Adding the static `|L| x PerFloor(S)` to the pick (transit on only) cannot oscillate, because it changes only on a fit.
- **O5. Bills from several sources.** `TakeRoadStruct` and growth bills take from several buildings in Id order (`RHSimGrowth.cpp:258-270`). Charge `ceil` per source part, to that source's serving shaft.
- **O6. TR1 is vacuous.** Both twins have `HasTransit()` false. The battery diff and the playtest are the inertness proof. Drop TR1, or make it "fit at sol k, then compare against a no-fit twin up to k".
- **O7. The lifts=1 ledger cross-check is not independent.** The bot (RedHope module) can only see boardings through a sim accessor. Use per-sol mass conservation on the counters instead.
- **O8. Define `MaxWaitSecCap`.** Section 7's refusals use it, but no section defines it.

## Director questions

- **Q-A: needed. Keep (a), and say the cost plainly.**
  - Fitting the first car (400 kg) also puts every unfitted shaft on a 200-place hoist with a 30 s + 15 s-per-floor hold. Deliveries through other shafts' heads get slower the moment you fit.
  - In return, robots go below through that shaft, and its commute drops from 60 m to 20 m a floor.
  - Ask whether 400 kg stays.
- **Q-B: re-pose it as "trams now, for robots only, or in D6 with people?"**
  - Under the current recommendation, trams carry only robots. They pay only past about 240 m of tunnel: half the 60 s headway plus the ride, against a 4 m/s walk. `TunnelMaxCells` is 64, so 640 m.
  - They cost a verb, a save record, a planner edge class, a teleport rule, a visualizer and TR16.
  - "A tram line fixes it" for people is D6 either way.
  - **Recommend: trams move to D6.** D4 is then lifts, capacity and robots below, which is already the riskiest gate in step 2.
- **No question for item 4.** "Cargo only" is what Q1a already says.

VERDICT: CLEARED WITH CONDITIONS
1. Re-anchor every `RHSimWorldSubsystem.cpp` line to `5b89cc1`.
2. Tag world-side ledger events with `GetStepEndSeconds()` and robot events with their own sub-step end. Correct the frame-span facts (3.2 s agent, 240 s era) and the mutation list.
3. Rewrite TR5: a contention-free fixture, a batched driver in both orders, a correct boundary case.
4. Charge cargo only (a robot body is 0 places), so section 5's era equality is true. State that the first fit opens floor-level hauls.
5. Make the floor-to-floor split one-pass (colonist ordinal k mod n), or specify a two-pass derive.
6. Specify the hand-off hold: its storage, Phase 2, the idle draw, and a `RobotLevel` on `HaulUnload`.
7. Price ascent holds at `MaxWaitSec` in the deep reserve, and extend TR14.
8. Scale `PeopleLoad` by the ramp (or correct deviation 6), and pin 60, 61 and 160.
9. Save: write and keep `Level` correctly; allow a slot tag of `TransitCheckedSol + 1`; fix the default-block wording.
10. Diff allowance: 11 out and 11 in, with the five `/45` and five `Ver == 45` conditions listed.


# REVISION (2026-10-21)

This revision answers the PROFESSOR REVIEW above. Where it disagrees with sections 0-10, the revision wins.

Anchors are read at HEAD `5b89cc1` (D3 committed, save v45). The tree is clean apart from this doc, so the working tree and `git show 5b89cc1:` agree. D3 has landed, so the draft's "D3 is being built" framing is withdrawn: the **Anchors** note and R11 now read "anchors are at `5b89cc1`; D3 is committed".

**The two director questions are still open.** Every item below is written to build under either answer. Where an answer changes something, it is marked:
- **[Q-A keep]** the first car costs 400 kg, like every other car (the reviewer's recommendation); **[Q-A free]** the colony's first car costs 0 kg.
- **[Q-B now]** trams are built in D4, for robots only; **[Q-B D6]** trams are not built in D4 at all (the reviewer's recommendation). Under [Q-B D6], every tram line, verb, record, edge, row, test and visual in the draft and in this revision is dropped, and nothing replaces it.

## Item 1. Re-anchored to `5b89cc1`

Only `RHSimWorldSubsystem.cpp`, `RHSimWorldSubsystem.h`, `RHSimCommandlet.cpp`, `RH_Config.csv` and two lines of `RHSimGrowth.cpp` changed between `b8ab469` and `5b89cc1` (`git diff --stat`). I mapped every draft anchor through the diff hunks and then read each target line.

**`RHSimWorldSubsystem.cpp`** (the draft's anchors, old → new):

| What | Draft (`b8ab469`) | `5b89cc1` |
|---|---|---|
| `RHSaveVersion` | `:36` (44) | `:36` (45) |
| rover reserve proof (O1: untouched) | `:169-179` | `:184-194` |
| `SpawnRobotTracked`; adds the handle only when valid | — | `:489-503`; `:497-501` |
| world `Tick`; the agent batch with `StepEndOverride`; the era batch | `:504-519` | `:519-561`; `:528-535` (formula `:532`); `:554-560` |
| `StepSim`; `StepUplink` in it | — | `:563-590`; `:568` |
| construction refuses era (`CanEnterEraMode`) | `:612-619` | `:625-634` (the test is `:629-631`) |
| `EraStep`; `StepUplink` in it | `:718-746` | `:733-761`; `:741` |
| era legs, level-matched | `:797`, `:901`, `:928` | `:812`, `:916`, `:943` |
| `StepUplink` (orders run on the frame clock, `:961`) | — | `:959-975` |
| board legs, level-matched (deposit, consumer, feed) | — | `:1042` / `:1069`, `:1156` / `:1187`, `:1247` |
| site hopper source scan (`AreLevelsLinked` at `:1318`) | `:1297-1314` | `:1312-1329` |
| site hopper, whole delivery block | `:1297-1330` | `:1312-1345` |
| W1 `FindOutpostDestination` level test (O1: untouched) | `:1810` | `:1846` |
| `GetStepEndSeconds` | — | `:2570-2572` |
| `GetApproachPoint`; the serving-shaft pick | `:2678-2700`; `:2681`, `:2694` | `:2714-2736`; `:2724-2725` |
| `RebuildShaftDerived`; `ServingShaft`; `WriteShaftBlock` | — | `:5206`; `:5289`; `:5511` |
| `FindCarvedCellAt` | — | `:6118` |
| `RollProductionLedger` (precedent only) | `:6933-6955` | `:6969-6991` |
| `ExecuteSinkShaft`; its two `Shafts.Add` | — | `:7477`; `:7491`, `:7525` |
| `GetTunnelUncarved`; `IsTunnelChainCell` | — | `:7560`; `:7610` |
| `WriteTunnelBlock`; `ReadTunnelBlock` | — | `:7964`; `:7978` |
| `ExecuteCommand` verb chain (`FitLift` goes beside `DigTunnel`) | — | `:8931-8938` |
| `GetSiteLevel` | — | `:12127` |
| `TryClaimHaul`; its same-level test | `:11628-11658`; `:11640` | `:12232-12262`; `:12244` |
| `HaulLoad`; the head drop | `:11704-11706` | `:12264-12312`; `:12308-12310` |
| `HaulUnload`; the credit | `:11710-11729` | `:12314-12333`; `:12318-12322` |
| `TryClaimBuild`; its `AreLevelsLinked` and head approach | `:11743-11772`; `:11755-11757` | `:12347-12376`; `:12359-12361` |
| `ApplyBuildWork` | `:11774-11830` | `:12378-12434` |
| `FindNearestChargePad` | — | `:12436` |
| `RequestChargeWh`; its free-energy branch | `:11901-11904` | `:12477`; `:12505-12508` |
| `AccrueWear` | — | `:12528` |
| `TryClaimRepair`; `ApplyRepairAt`; `ReleaseRepairClaim` | `:12059-12094`; — | `:12663-12698`; `:12706`; `:12736` |
| `SaveColony`; the robot collect; the cargo return | `:12294-12334` | `:12869`; `:12893-12897`; `:12898-12938` |
| `WriteShaftBlock` / `WriteTunnelBlock` calls (the transit block goes after `:12950`) | `:12345` | `:12949` / `:12950` |
| robot record write | — | `:13006-13011` |
| `LoadColony`; `ReadTunnelBlock` call (the transit read goes after it) | — | `:13350`; `:13392-13398` |
| teardown | — | `:13400-13418` |
| robot respawn; the `Level` read | `:12901-12911`; `:12906-12909` | `:13513-13523`; `:13518-13521` |
| floor table; shafts; tunnels installed | `:12922`, `:12925` | `:13534-13536`; `:13537`; `:13538-13540` |
| D2 warning repairs | `:12926-12953` | `:13541-13568` |
| `RebuildShaftDerived`; `ShaftReachVersion` bump | `:12954` | `:13569`; `:13570-13573` |

**`RHSimWorldSubsystem.h`:** `ShaftReaches` `:546`; `FRHShaft` `:299`; `FRHTunnel` `:324`; `Debug_StepSim` `:935`; `TryClaimHaul` `:1763`, `HaulLoad` `:1767`, `HaulUnload` `:1771`; `GetWearHaltThreshold` `:1736` → `:1811`; `RobotPaths` `:2049` → `:2124`; `FRHCommuteKey` `:2183-2194`; `FRHCommuteTrip` `:2142` → `:2217`; `FRHCommuteFlows` `:2218-2233`; `StepEndOverride` / `GetStepEndSeconds` `:2385-2386`; `WearHaltThreshold` `:2318` → `:2393`; `NextTunnelId = 1` `:2439`.

**`RHSimGrowth.cpp`:** `GetGrowthHoldingsKg` `:162-181`; `TakeGrowthResource` `:249-285`, its per-source loop `:260-279` (the draft's `:266` is right); `StepCityGrowth`'s threshold `:459-470` → `:461-472` (D3's O1 added two lines at `:387-390`); the growth bill call `:641`.

**`RHSimCommandlet.cpp`:** the commandlet's earlier suites did not move. EP4 `:11112-11160`, the rover harness `:10152-10190` and `AgentOnce` `:10959-10964` are as the draft says.

**Unchanged files, re-read at HEAD; the draft's anchors hold, with these corrections:**
- `RHRobotBrainProcessor.cpp`: the entity loop is `:71-114`, the per-step loop `:94-113`, the halt gates `:103-111`, the tick `:112`; era/pause park `:46-50`.
- `RHRobotTaskProcessor.cpp`: the entity loop `:58`, the per-step loop `:67`, `bMidDelivery` `:76`, the seek `:77`, the Haul draw `:234`, Phase 2 `:257-264`, the unload `:261`.
- `RHRobotStateTree.cpp`: the Haul draw is `:206` (the review's `:205`); Phase 2 `:229-236`, the unload `:233`; the wear gate `:271-275`; the seek pad pick `:327-329`; `DriveWh` `:417-421`, its slack `:420`; the mid-delivery exemption `:456-467`.
- `RHSimRoads.cpp` (`:27-45`, `:54-109`, `:75-109`, `:297-307`, `:361-376`, `:388`, `:403-413`), `RHSimCommute.cpp` (`:33-41`, `:50-65`, `:170-192`, `:194-210`, `:212-333`, `:360-366`, `:369-389`, `:409-416`, `:530`, `:552`), `RHAgentSubsystem.cpp` (`:182`, `:188-215`), `RHAgentFragments.h:62`, `RHSimClockSubsystem.h` (`:23`, `:26`, `:29`, `:33`, `:34`, `:37`), `RHPlaytest.cpp` (`:361`, `:1246-1250`, `:1902`, `:1922-1936`, `:1927`), `RHTrafficVisualizerSubsystem.cpp:508`, `RH_Config.csv:225`, `diff_battery.py:11`: all as the draft says.
- `RHSimRovers.cpp` is untouched under O1 (adopted), so its anchors are no longer build anchors.

## Item 2. Every ledger event is stamped with its own step time

**(a) World-side writers** (`TakeStructForBill`, growth bills) stamp `GetStepEndSeconds()` (`:2570-2572`), never `Clock->GetSimSecondsTotal()`.
- The world runs its batch after the clock has advanced the whole frame (`:528-535`), and an era frame holds up to 4 x 60 s (`RHSimClockSubsystem.h:34`). Under frame-end stamping, a bill in the first era step of a frame would be stamped up to 180 s late.
- With the step end, both bands stamp a world-side bill by its own step window, the same rule `StepCityGrowth` crosses on (`RHSimGrowth.cpp:466`).

**(b) Robot writers** (boarding a lift; the hand-off credit in `HaulUnload`) stamp their own sub-step end:
- `RobotStepEnd = FrameEnd - (Steps - 1 - Step) x SubStepSeconds`. That is the `StepEndOverride` formula (`:532`), with `FrameEnd = Clock->GetSimSecondsTotal()` read once, before the entity loop.
- **Where it is set.** A new sim-only `double RobotStepEndSeconds = -1.0` and `SetRobotStepEnd(double)` sit beside `StepEndOverride` (`.h:2385`).
  - StateTree brain: set before `StateTreeContext.Tick(SubDt)` (`RHRobotBrainProcessor.cpp:112`), after the halt gates (`:103-111`). The chunk lambda (`:56`) captures `Sim` and `FrameEnd`. It is reset to -1 after `ForEachEntityChunk` (`:115`).
  - Legacy brain: set at the top of the per-step loop (`RHRobotTaskProcessor.cpp:67`). It is reset after the chunk loop.
  - Harnesses set it themselves (item 3's driver).
- **The writer's stamp** is `RobotStepEndSeconds >= 0 ? RobotStepEndSeconds : GetStepEndSeconds()`. The fallback bumps a transient counter, `TransitUnstampedRobotWrites`. The `-transit` suite and the `lifts=1` playtest assert that it stays 0.
- **The stamp is exact and identical under every batching.** `SubStepSeconds` is the float `0.1f`, so every clock total is a whole multiple of 2^-27 s. `(double)k x 0.1f` and the subtraction are exact in double. The stamp of a sub-step is therefore bit-identical whether it ran in a frame of 1, 7 or 32. It is also the reason the sol boundary is found by stamp, not by index: `12000 x 0.1f` = 1200.0000179 s, so sol 0's last sub-step is the 11,999th and the 12,000th is already sol 1.
- **Risk R2 is withdrawn.** The bucket now depends on sim time only. Orders still execute on the frame clock (`StepUplink`, `:961`), as today; that is not a D4 change.

**(c) The frame-span facts and the lag, corrected** (section 0 "The clock" and 2.3 "Why a two-sol lag"):
- An agent-band frame is at most `MaxSubStepsPerFrame` = 32 sub-steps, 3.2 s (`RHSimClockSubsystem.h:29`).
- An era frame is at most `MaxEraStepsPerFrame` x 60 s = 240 s (`:33-34`), and it runs no robot (`RHRobotBrainProcessor.cpp:46-50`).
- `MaxCarrySimSeconds` = 120 (`:37`) is the accumulator's carry cap. It is not a frame span. "A frame never spans more than 120 s" is struck.
- **The two-sol lag stays, for this reason.** The robot batch and the world batch of one frame are unordered (section 0). Under world-first, the crossing frame's robot sub-steps that end before the boundary are written after the world has rolled into `SolNow`. So slot `SolNow-1` can still gain events after the roll that reads it. Slot `SolNow-2` cannot: its last events came from the frame that crossed into `SolNow-1`, a whole sol earlier.

**(d) Robots read waits by their own stamp (O2, adopted in a guarded form).** Without this, TR5's equality across batch orders fails.
- **The problem.** Under robots-first, a robot whose sub-step is past the boundary runs before the world's roll and reads the old wait. Under world-first it reads the new one. Hold lengths, plans and arrival times then differ by batch order.
- **Why the review's plain form is not enough.** Reading "by the robot's sub-step sol" needs the new sol's wait, which under robots-first does not exist yet.
- **The rule.** Each ledger keeps `WaitSec` and `PrevWaitSec`. The roll moves `WaitSec` into `PrevWaitSec` before it writes the new `WaitSec`. A robot reads `GetRobotWaitSec(S, Stamp)`:
  - `RobotSol = FloorToInt32((Stamp - RH::TransitRobotLagSeconds) / 1200)`, with `RH::TransitRobotLagSeconds = (MaxSubStepsPerFrame + 1) x SubStepSeconds` = 3.3 s, a constant derived from the clock and not a row. The extra sub-step is needed because the crossing step itself can end up to one sub-step after the boundary, and its frame can run 31 more;
  - the result is `WaitSec` when `RobotSol == TransitCheckedSol`, else `PrevWaitSec`.
- **Why it is order-free.** A sub-step stamped at boundary + 3.3 s or later belongs to a later frame than the crossing step, so the world has rolled by then in either order. A sub-step in [boundary, boundary + 3.3 s) reads the previous sol's wait in either order: `PrevWaitSec` if the world has rolled, `WaitSec` if it has not, and both hold the previous sol's value.
- **Who reads it.** The hold at boarding (1.2), the hand-off hold (item 6), the planner's ride costs (1.3), and the reserve (1.5, item 7). The commute (people, world side) reads `WaitSec` at once, as the draft says.
- **The deep net.** `FRHDeepNet` becomes wait-free: `TransitWaitVersion` leaves its key (1.3). `PlanDeepRoute` adds `GetRobotWaitSec` to each ride edge at query time, so one cached graph serves both wait sets. The reserve memo's key gains the robot wait sol (`RobotSol`).
- The driver in item 3 asserts `K <= MaxSubStepsPerFrame`, which is the bound the guard relies on.

**(e) The mutation list is replaced** (section 9 "Mutation checks"). Each mutation must fail the named check:
1. Robot events stamped with the frame end (the draft's 2.2) fail TR5's boundary case: under K = 32 the event lands in slot s+1.
2. The roll reads slot `SolNow-1` instead of `SolNow-2`. TR5 fails: under world-first with K = 7 or 32, the crossing frame's pre-boundary hand-off is written after the roll, so that roll's `WaitSec` differs from the robots-first and K = 1 runs. Under frame-end stamping this mutation passed, as the review says.
3. A world-side bill stamped with the clock total fails TR3's new era case (item 3).
4. A robot read of `WaitSec` with no lag guard (the draft's plain read) fails TR5's equality across batch orders.
5. A robot body charged 1 place fails TR17's equality at capacity (item 4).
6. A wait-aware `PickCommuteShaft` fails TR8's monotonicity (kept).
7. A people load not scaled by the ramp fails TR6's 61-person case (item 8).
8. An ascent hold priced at the frozen wait fails TR14c (item 7).
9. A hold with no `HasTransit()` test, or a same-level unload that writes the ledger, fails TR9 (item 6).
10. A kept level appended for a failed respawn fails TR18's missing-row case (item 9).

The draft's "ledger kg as double fails TR5" is dropped. Places, not kg, enter a wait, and harness loads are whole kg, so that mutation can fail nothing.

## Item 3. TR5, rewritten

**(a) The boundary claim is corrected.** Sol s+2's roll reads slot s. Moving an event out of slot s therefore changes the waits of sols s+2 and s+3. The lag makes a slot complete before it is read; it does not make the bucket edge harmless. Item 2(b) is what removes the dependence on the frame schedule.

**(b) The fixture has no contention and no world-timed input.**
- Six harness haulers (fake handles, as EP4 uses, `RHSimCommandlet.cpp:11114`, `:11127`) each own a fixed route: their own surface store, seeded with 1e9 kg Struct, to their own completed store below.
  - Haulers 0-2 ride shaft 0, which has one car, into a dig with a fitted landing.
  - Haulers 3-5 deliver through unfitted shaft 1: a hoist hand-off at its head.
- **Tasks are injected by the harness** at the hauler's own sub-step through a new `Debug_PostHaulTask(From, To, Resource, Kg)`. They are never posted by the board and never claimed through `TryClaimHaul`. With no shared task and no world-timed posting, robots-first and world-first cannot change what a hauler does.
- Each hauler sub-step uses real `RH::MoveAlong` with no roads, the ride counters, `BeginHandoffHold` / `StepHandoffHold` (item 6) and `HaulLoad` / `HaulUnload` with `RobotLevel`.
- Population stays at 60 or fewer, so the people part is 0. Capacity rows are overridden to 20 places, so the waits are non-zero and feed back into the holds.
- **Why people are held at 0.** The people load reads world state at the crossing step. A build that a robot completes in the crossing frame can land on either side of that step, depending on batch order. That is the existing frame-schedule class of `StepCityGrowth` (new risk R13), and it is not what TR5 tests.

**(c) The batched-frame driver.** No harness batches frames today: `AgentOnce` (`:10959-10964`) runs one world sub-step, and EP4 plays its robots before advancing the clock.

```
// -transit: one frame of K sub-steps, as Clock::Tick + the world Tick (:528-535) + the brain processor (:94-113) run it.
DriveFrame(K, bRobotsFirst, bReverseEntities):
  check(K >= 1 && K <= URHSimClockSubsystem::MaxSubStepsPerFrame);
  Clock->Debug_AdvanceSimSeconds(K * (double)SubStepSeconds);     // the clock advances the whole frame first
  const double FrameEnd = Clock->GetSimSecondsTotal();
  RobotBatch: for each hauler h in spawn order (reversed if bReverseEntities):
                for Step in 0..K-1: { Sim->SetRobotStepEnd(FrameEnd - (K-1-Step) * (double)SubStepSeconds); h.SubStep(); }
              Sim->SetRobotStepEnd(-1.0);
  WorldBatch: for i in 0..K-1: Sim->Debug_StepSimAt(SubStepSeconds, FrameEnd - (K-1-i) * (double)SubStepSeconds);
  bRobotsFirst ? (RobotBatch, WorldBatch) : (WorldBatch, RobotBatch);
```

- `Debug_StepSimAt(SubDt, StepEnd)` is a new header one-liner beside `Debug_StepSim` (`.h:935`). It sets `StepEndOverride`, calls `StepSim` and resets the override to -1, exactly the body of `Tick`'s loop.
- Entity-major order (each hauler runs all K sub-steps before the next) is the processor's order (`:71-113`).
- A 5-sol run is 60,000 sub-steps. K = 7 runs 8,571 frames of 7 and one frame of 3; K = 32 runs 1,875 frames.

**(d) The new TR5.**
- **The runs:** K in {1, 7, 32}, times robots-first and world-first, times forward and reversed entity order. That is 12 runs of 5 sols each from one saved start.
- **Equal across all 12:** every ledger slot (`Sol`, `Kg`, `Places`), `WaitSec` and `PrevWaitSec` after every roll, `TransitThresholds`, each hauler's delivered kg and final position, and `TransitUnstampedRobotWrites == 0`.
- **The boundary case** is a micro-fixture: one hand-off hauler on the hoist, 200 kg.
  - It uses the default rows. A new `Debug_ChargeTransit(Shaft, Places, Stamp)` preloads slot s with exactly `Cap` (200, the hoist) places, at a mid-sol stamp.
  - The hauler's hold is timed so that its credit falls on the **last sub-step stamped in sol s**, found by stamp (the sub-step before the one where `FloorToInt32(stamp / 1200)` changes).
  - The frames are aligned so that this sub-step and the crossing sub-step share one frame: the crossing sub-step is the 17th of its 32-frame and the 4th of its 7-frame.
  - In every one of the 12 runs: slot s gains exactly 2 places; slot s+1 gains none from it; and the hauler's next event, at the crossing sub-step, lands in slot s+1.
  - Sol s+2's `WaitSec` = `ceil(2 x 120 / 200)` = 2. Sol s+1 and sol s+3 are unaffected (0 from this event).
  - Mutations 1 and 2 (item 2(e)) fail exactly here.

**TR3 gains two cases:**
- **The era stamp.** A road bill from a -1 source executes in the first era step of a 4-step frame whose first step ends before a boundary and whose last ends after it. It lands in the first step's sol (mutation 3).
- **The robot wait guard.** A boarding stamped 1.0 s after the boundary reads the previous sol's wait under both batch orders. A boarding at 3.4 s after it reads the new wait.

## Item 4. Cargo only: a robot's body is 0 places

- **The rule.** Every robot event charges `ceil(WholeKg / LiftKgPerPlace)`, where `WholeKg = RoundToInt64(CargoKg)` and the ceiling is computed in int64 as `(WholeKg + LiftKgPerPlace - 1) / LiftKgPerPlace`. Nothing is charged for the robot. A 0-place event writes nothing, and does not even reset a slot.
  - Section 1.2's "1 place for the robot plus …" becomes "the cargo's places".
  - The 2.5 table loses the robot place in both boarding rows.
  - 2.1's "a place is one person, one robot, or 100 kg" becomes "one person, or 100 kg of cargo".
  - This follows Q1a's "a lift has a cargo capacity" (`going-deep-plan.md:106`). It needs no director question.
- **Why.** Robots ride only in the agent band. With transit on they ride for reasons that move no cross-level cargo: to surface and charge, for repair, and to work floor-level hauls. If those rides were charged, the agent band's waits would exceed era's, and the difference would flow into `DistCm`, then `bLong`, then Hope.
- **Section 5 is now true as written.** Outside construction (which refuses era, `:629-631`), the ledger holds only world-side bills, which run identically in both bands.
- **TR17 is made sensitive to it.**
  - The people load on shaft 0 is set at exactly `Cap` (the ramp open, `PeopleLoad = Cap`).
  - The agent leg's haulers below ride cargo-free to the surface to charge.
  - People waits must be equal at every threshold in both legs, and 0. A robot body charged 1 place would tip shaft 0 to `ceil(120 / Cap)` s in the agent leg only (mutation 5).
- **New behaviour at the first fit, stated in 1.4 and in R1.** No robot has ever claimed a floor-level haul: `TryClaimHaul` requires `GetSiteLevel(T.From) == RobotLevel` (`:12244`), and every robot stands at level 0. Today the board's level-matched legs on a floor (`:1042` / `:1069`, `:1156` / `:1187`, `:1247`) are moved only by era logistics. From the first fit, robots below take them.
  - That is new agent-band work. It moves no mass through a shaft and writes no ledger entry.
  - It brings the agent band closer to era, which already teleports those legs.
  - The fit alert says so (section 8).

## Item 5. The floor-to-floor split in one pass

`DeriveCommuteFlows` fixes a trip's `DistCm` and `bLong` on its first colonist (`RHSimCommute.cpp:269-314`) and pushes `Dists` and `LongFlow` per colonist in the same pass as `++Trip.Count` (`:315-332`). So `Count` is not known while colonists are counted. The split is redone in one pass, with transit on only:

- **At trip creation** (`:271-313`), for a floor-to-floor trip on different levels, the trip records its reach list `R`: the shafts with `ShaftReaches(S, min(HomeLevel, WorkLevel))` (`.h:546`), in ascending Id. If `R` is empty, it is `{0}`, as today.
  - `R` lives in a local array parallel to the trip slots. It is valid until the trip sort at `:360-366`.
  - `FRHCommuteTrip` (`.h:2217`) gains `int32 ShaftId`. It is `INDEX_NONE` for a split trip and for a trip with no shaft. For a floor-to-surface trip it is the shaft from `PickCommuteShaft`, set where `:302-303` set the anchor. With one shaft, it is 0.
- **Per colonist,** `k = Trip.Count` read before `++Trip.Count` (`:320`). The colonist's shaft is `S = R[k % n]`. Its distance is `D_k = |dL| x PerFloor(S) + WaitCm(S)`, in int64 with no division.
  - `Dists` gets `D_k`; `Commuters`, `MaxDistCm`, `LongCount`, `LongIds` and `LongFlow` use `D_k` and `bLong_k = D_k > CommuteLongCm`. The lines `:323-332` read the colonist's values, not the trip's.
- **`k` is the colonist's ordinal in the `Colonists` array order the loop already walks** (`:250`). That order is Id-ascending today. The derive does not rely on it (`:334-335`), and neither does the split: shares depend only on `Count`, and which colonist gets which shaft is a function of saved state.
- **Shares.** Shaft `R[i]` gets `Count / n + (i < Count % n ? 1 : 0)` colonists. That is the draft's split, now reached without knowing `Count` in advance.
- **The per-shaft count is accumulated in the same pass.** `FRHCommuteFlows` (`.h:2218`) gains `TArray<int32> ShaftPeople`, indexed by shaft Id. Floor-to-surface trips add 1 per colonist to `ShaftId`; split trips add 1 to `R[k % n]`. `StepTransit` reads `ShaftPeople` (item 8) and does not recompute it.
- **For display only,** a split trip's own `DistCm` is `Σ D_k / Count` (int64, floored) and its `bLong` is that mean against `CommuteLongCm`. They are set after the loop. `GetCommutePairs` (`:543`) shows them, and no rule reads them.
- **`-RHVerifyCaches`.** `Dists` is already compared (`:381-382`, `:399-400`), so the draft's "gains `Trips[].DistCm`" is dropped. `ShaftPeople` joins both compares instead.
- With no transit, none of this runs: `ShaftId` stays `INDEX_NONE`, `ShaftPeople` stays empty, and `:250-333` take today's lines.
- **TR6** keeps "7 over 2 shafts gives 4 and 3 colonists, 8 and 6 places at full ramp". It adds that colonist ordinals 0, 2, 4, 6 take the lower Id, and that each one's `Dists` entry equals its own shaft's hand integer.

## Item 6. The hand-off hold has a home

- **(a) Storage.** A runtime map in the sim, `TMap<FMassEntityHandle, FRHHandoffHold> HandoffHolds` with `{ int32 TaskId; int32 StepsLeft; }`, beside `RobotPaths` (`.h:2124`).
  - It is never saved. At save, in-flight cargo returns to its source as today (`:12898-12938`), and no ledger entry has been written yet, because the write happens at the credit.
  - It is reset at load with the other runtime state.
  - An entry is valid only while its `TaskId` matches the fragment's task, the type is Haul and the phase is 2. Any other read erases it and reports no hold.
- **(b) Phase.** The hauler stays in Phase 2 throughout. Both mid-delivery tests (`RHRobotStateTree.cpp:456`, `RHRobotTaskProcessor.cpp:76`) therefore see the hold, and 1.5's last bullet governs it.
- **The flow in Phase 2** (`RHRobotStateTree.cpp:229-236`; legacy `RHRobotTaskProcessor.cpp:257-264`):
  1. If `Sim->InHandoffHold(Entity, TaskId)`: set `DrawW = Robot.DrawIdleW`. Call `Sim->StepHandoffHold(Entity)`. When it returns true (the count reached 0, or the task vanished), call `HaulUnload(TaskId, CargoKg, ReturnResource, LevelOfZ(Z))` and reset the fragment. `MoveAlong` is not called while holding.
  2. Otherwise run `MoveAlong` as today. On arrival, call `H = Sim->BeginHandoffHold(Entity, TaskId, LevelOfZ(Z))`.
     - If `H == 0`, unload on this sub-step, exactly as today.
     - If `H > 0`, the entry starts. The arrival sub-step drew `DrawMoveW` as today. Sub-steps 1..H hold at `DrawIdleW`, and the credit lands at the end of sub-step H.
  - `BeginHandoffHold` returns 0 unless `HasTransit()`, the task's site level differs from `RobotLevel`, and the site's building exists. Otherwise H is `10 x (GetRobotWaitSec(S, Stamp) + Dwell(S) + |SiteLevel| x SecPerFloor(S))`, where S is the shaft `GetApproachPoint` uses (`:2721-2730`).
  - A hand-off is always surface to floor. A surface robot's `From` is at level 0 (`:12244`), and every surface site is reachable for a robot below (1.4).
- **(c) Draw.** The Haul case sets `DrawMoveW` for every phase (`RHRobotStateTree.cpp:206`; legacy `:234`). The hold branch overrides it with `DrawIdleW`, so the wear gate (`:272`, `DrawW > DrawIdleW`) does not accrue. The legacy brain has no wear (R12).
- **(d) `HaulUnload` learns the robot's level.** The signature becomes `HaulUnload(int32 TaskId, float CargoKg, FName ReturnResource = NAME_None, int32 RobotLevel = 0)` (`.h:1771`, `:12314`).
  - It writes the ledger only when `HasTransit()`, the credit happens (the `B` branch, `:12318-12322`), and `GetSiteLevel(T->To) != RobotLevel`.
  - It writes to the serving shaft S above, with the robot stamp (item 2(b)) and the cargo's places (item 4).
  - The existing harness calls (EP4 `:11157`, the rover harness) keep the defaults, and no ledger exists without transit.
- **A vanished task mid-hold** (a demolish) ends the hold at once. `HaulUnload` then handles it exactly as today, with no ledger write.
- **Halt gates and era** freeze the entry, because the brains do not tick. A charge seek resets the fragment, and the stale entry is erased on the next read.
- **Charging during the hold** (1.5's last bullet, made concrete):
  - StateTree: the mid-delivery exemption (`:456-467`) adds `DrawIdleW x HoldSecondsLeft / 50 x 1.25` to `ViaDropWh`. `HoldSecondsLeft` is the entry's `StepsLeft / 10` while holding. Before arrival it is the projected hold at the current robot wait.
  - Legacy: `bMidDelivery` (`:76`) becomes `Haul && Phase 2 && !Sim->HandoffHoldShort(Entity, ChargeWh, Robot, Pos)`, the same inequality. The legacy brain had no hold exemption to keep before; it now seeks instead of draining at a head.
  - The hold is spent on the surface, so a failed test seeks from the surface, and the hauler loses its load by today's rule. While holding, the requirement falls by 1.25 x idle per second and the charge by 1 x idle, so a hauler that passes at arrival keeps passing. MaxWaitSec pricing (item 7) is not needed here.
- **TR9** runs in a no-road colony (`MoveToward`, `RHSimRoads.cpp:40-43`, which never touches `RobotPaths`) and in a road colony (`MoveAlongRoads`, which drops the entry on arrival). In both it checks:
  - exactly `10 x (Wait + 30 + 15 x |L|)` hold sub-steps on a hoist, and `10 x (Wait + 10 + 4 x |L|)` on a lift, then one credit;
  - `DrawIdleW` on every hold sub-step, and wear unchanged across the hold;
  - one ledger write of `ceil(kg / 100)` places at the credit stamp;
  - a same-level unload by a robot below writes nothing;
  - with no transit, H = 0, and the credit lands on the arrival sub-step as today;
  - a seek mid-hold follows the rule above in both brains.

## Item 7. Ascent holds are priced at `MaxWaitSec`

- **The defect.** A robot below can cross a threshold while it walks to its landing, and then read up to 600 s at the column.
  - For an RC-H that is 5 W x 12 sol-hours x 1.25 = 75 Wh (`RH_Robots.csv`: idle 5 W, move 80 W).
  - `DriveWh`'s slack is 0.5 x 80 = 40 Wh (`RHRobotStateTree.cpp:420`).
  - A drained robot below is never repaired (1.7), so it would be lost.
- **The fix, in `DeepReserveWh` (1.5).** The route stays route-true: the path `PlanDeepRoute` returns to the pad `EnterState` will pick, under the robot's current wait set (item 2(d)). Only the price of its holds changes:
  - every **ascent** hold on the route, and every tram hold **[Q-B now]**, is priced at `DrawIdleW x MaxWaitSec / 50 x 1.25`;
  - for a tram hold, the headway's half is added;
  - rides are priced as in 1.5.
- **I reject the review's "this takes `TransitWaitVersion` out of the memo key".** The route itself depends on the waits: the planner prices `Wait_S` to steer around a crowd (1.3). A wait-free route would not be the route driven, and energy is not proportional to plan cost: a long 80 W detour can cost more than a 5 W hold. So the memo keeps the wait-set key (now the robot wait sol, item 2(d)). The price inside it is wait-free.
- **Descents keep the frozen wait** for the hold that is left, because that hold is spent where the robot stands:
  - from the surface, a failed descent check seeks on the surface;
  - from a floor (-1 to -2), the generic deep test runs on every sub-step at the current position, with its ascent priced at `MaxWaitSec`, so a hold that eats into the reserve fires the seek on the floor with enough left to surface.
- **TR14c**, in both brains:
  - (i) For an RC-H on -1, one floor below the pad level, with wait 0: `DeepReserveWh` equals the hand integer with the ascent hold at 600 s (75 Wh) plus the 140-sub-step ride (`80 x 14 / 50 x 1.25` = 28 Wh) plus the drives.
  - (ii) A hauler on -1 at that reserve + 1 Wh seeks. `Debug_ChargeTransit` loads shaft 0's s-2 slot to `6 x Cap`, so the threshold crossed during its walk sets `MaxWaitSec`. The hauler holds 6,000 sub-steps, ascends, docks, and is never at charge 0.
  - Under mutation 8, (i) is 75 Wh short and (ii) halts in the queue.

## Item 8. The ramp scales the people load

- **`PeopleLoad_S = 2 x ShaftPeople[S] x RampPermille / 1000`** (int64, floored), where `RampPermille = RoundToInt(GetCommuteRamp() x 1000)` (`RHSimCommute.cpp:33-41`).
  - The sum is taken per shaft first and scaled once, so the floor is taken once per shaft.
  - At 60 people or fewer `RampPermille` is 0, and `StepTransit` does not touch the flows (unchanged).
  - At 61 it is 10. At 160 or more it is 1000.
- **The effect.** Robot holds and hand-offs feel people in proportion to the ramp. A colony of 61 adds about 2% of its floor commuters to a shaft's load, not all of them. The plan's D4 bar ("the ramp from 60 to 160 people stays", `going-deep-plan.md:78`) now holds for robots too.
- **Deviation 6 stands as written** ("the ramp still scales every effect"), because it is now true.
- **R1 gains one line:** under 61 people a fit adds no people load at all. The cliff is cargo and hoist time only.
- **TR6 pins it.** The fixture puts every colonist in a -1 home with a surface seat, through shaft 0.
  - 60 people: `PeopleLoad` 0, and `CommuteFlowRebuilds` does not move.
  - 61 people: `2 x 61 x 10 / 1000` = 1 place.
  - 160 people: 320 places.

## Item 9. Save details

**(a) `Level` is written and kept correctly.**
- **Write.** `URHAgentSubsystem` has no `FloorHeightCm`. `SaveColony` therefore sets the level itself, in a new loop straight after the collect (`:12893-12897`) and before the cargo-return loop (`:12898`), which `continue`s past robots with no cargo: `for (FRHRobotSaveState& R : Robots) { R.Level = RH::LevelOfZ(R.PosCm.Z); }`. A default colony writes 0, the same bytes as today (`:13010`).
- **Keep.** A local `TArray<int32> RespawnedLevels` sits beside `Respawned` (`:13514`). The loop becomes:
  `const FMassEntityHandle H = SpawnRobotTracked(…); if (H.IsValid()) { RespawnedLevels.Add(R.Level); }` (`:13519-13522`).
  - `SpawnRobotTracked` appends to `Respawned` only for a valid handle (`:497-501`), so the two arrays stay parallel even when a def row is missing.
- **The repair (1.6)** runs after the floors, shafts and tunnels are installed (`:13534-13540`), the transit install (straight after `:13540`), the D2 warning repairs (`:13541-13568`), `RebuildShaftDerived` and the reach bump (`:13569-13573`).
- **TR18 adds the missing-row case:** a hand-made file whose second robot names an unknown row. The third robot keeps its own level (mutation 10).

**(b) A slot tagged `TransitCheckedSol + 1` is legal.**
- The refusal becomes "a slot tag above `TransitCheckedSol + 1`".
- **When it happens.** Under a robots-first schedule, the crossing frame's post-boundary robot events are stamped `SolNow` before the world rolls.
  - The autosave cannot see this: it fires from `OnSolElapsed` (`HandleSolElapsed`, `:505-517`) inside the clock's tick, before either batch of the crossing frame runs.
  - A save made outside the sim's steps can: the quicksave (`RHPlayerController.cpp:2262`) or the console save (`RHCheats.cpp:2544`) can run between the two batches, since their tick order is not pinned.
  - A strict refusal would make the player's own current-version save unloadable.
- **Further slot rules**, added because the tags are now checked:
  - an unused slot's tag is -1 (`Sol[]` starts at -1, so sol 0 never collides);
  - a tag below -1 is refused;
  - a tag >= 0 whose `tag % 3` is not its slot index is refused;
  - an older tag is legal (it is overwritten on the next write and never read by a roll).
- **TR18 adds the case.** The item 3 driver runs robots-first and saves between the robot batch and the world batch of a crossing frame. The file loads, the `+1` slot round-trips byte-equal, and the world batch then rolls with the same `WaitSec` as the twin that never saved.

**(c) The block, exactly.** `NextTramId` starts at 1, following D3's `NextTunnelId = 1` (`.h:2439`, `tunnels.log:317`). So the review's "four zeros and one -1" would hold only if it started at 0.

- **[Q-B now]**:
  ```
  int32 LiftShaftCount;    per: int32 ShaftId, int32 Cars                (ShaftId ascending, Cars in 1..MaxLiftCarsCap)
  int32 TransitCheckedSol;                                               (-1 until the first fit)
  int32 LedgerCount;       per: int32 ShaftId, 3 x (int32 Sol, int64 Kg, int32 Places), int32 WaitSec, int32 PrevWaitSec
  int32 NextTramId; int32 TramCount;
                           per: Id, RegionId, Level, TunnelId, StopA.X, StopA.Y, StopB.X, StopB.Y,
                                3 x (int32 Sol, int64 Kg, int32 Places), int32 WaitSec, int32 PrevWaitSec
  ```
  The default colony writes 20 bytes: **0, -1, 0, 1, 0** (`LiftShaftCount`, `TransitCheckedSol`, `LedgerCount`, `NextTramId`, `TramCount`).
- **[Q-B D6]**: only the first three fields. The default colony writes 12 bytes: **0, -1, 0**.
- **`LedgerCount`** is 0 with no car, and `Shafts.Num()` with one: one ledger per shaft, Ids 0..n-1, because a hoist has capacity too. A shaft sunk after the fit gets a zero ledger in `ExecuteSinkShaft` (`:7491`, `:7525`). Any other count is refused.
- **`PrevWaitSec`** (item 2(d)) is saved, with the same 0..`MaxWaitSecCap` range as `WaitSec`.
- **The version is v46.** `RHSaveVersion` at `:36` goes from 45 to 46, with the comment chain extended.
- `WriteTransitBlock` is called straight after `WriteTunnelBlock` (`:12950`). `ReadTransitBlock` validates into locals straight after `ReadTunnelBlock` (`:13392-13398`), before teardown (`:13400`). The block moves neither K6's offset 20, SH12's `BlockAt`, nor TN1's tunnel-block read (`RHSimCommandlet.cpp:14012`).

## Item 10. The diff allowance: 11 out, 11 in, against `battery_post_d3`

I verified each line in `Saved/baselines/battery_post_d3` (48 logs). A grep for `version 4[45]`, `v4[45]` and `/45)` finds exactly these 11, and no others:

| Log:line | Test (`RHSimCommandlet.cpp`) | Becomes |
|---|---|---|
| `traffic.log:937` | X15 `:10811` (`Ver == 45`, "(expect 45)") | version 46 (expect 46) |
| `traffic.log:938` | X15 `:10812` (patch `V44 = 44`), `:10817` (`/45`), `:10818` ("a v44 save") | a v45 save … version 45; expected …/46 |
| `deep.log:590` | K6 `:12507` (`Ver == 45`) | version 46 |
| `deep.log:591` | K6 `:12522` (the literal "hand-made v45") | hand-made v46 |
| `deep.log:596` | K6 `:12549` (patch), `:12551` (`/45`, "a v44 file") | a v45 file … /46 |
| `framing.log:576` | e5 `:14930` (`Ver == 45`, "(expect 45)") | version 46 (expect 46) |
| `farice.log:7075` | F16 `:12085` (patch), `:12091` (`Ver == 45` and `/45`), `:12092` ("(expect 45)", "a v44 save") | 46; a v45 save; /46 |
| `farice.log:7089` | F16 `:12097` (the literal "the v45 save loads") | the v46 save loads |
| `shafts.log:1412` | SH12 `:13662` (patch), `:13664` (`/45`) | version 45; expected …/46 |
| `tunnels.log:317` | TN1 `:14012` (`Ver == 45`; the version is printed by `%u`) | version 46; …"8 bytes, no lift field" unchanged |
| `tunnels.log:1718` | TN8 `:14450` (patch), `:14451` (`/45`) | version 45; expected …/46 |

- **The conditions that move to 46:**
  - `Contains(TEXT("/45"))` at `:10817`, `:12091`, `:12551`, `:13664`, `:14451`;
  - `Ver == 45` at `:10811`, `:12091`, `:12507`, `:14012`, `:14930`.
- **The previous-version patches** (`V44 = 44`, written into a file so that the previous version is refused) at `:10812`, `:12085`, `:12549`, `:13662` and `:14450` become 45. Without that, those files would read "version 44" and the refusal text would not be the previous version's.
- **TN1's "8 bytes, no lift field"** stays literally true of the tunnel block. Only the version changes. The transit block is a separate block after it.
- Comments that say v45 (`:10799`, `:11205`, `:12072`, `:14635`, `:14917`) are updated for honesty. They print nothing.
- **The bar.** 49 runs (48 plus `-transit`), 0 not green, and `diff_battery.py` against `battery_post_d3` shows exactly 11 lines out and 11 in. `transit.log` is reported as NEW SUITE and is not counted. Recount literally at build time; any other line fails the gate.

## The director's questions, written in

**Q-A (the first car's cost).** One code path serves both answers.
- The bill is `HasTransit() ? LiftFitStructKg : FirstLiftFitStructKg`. The rows are:
  - `LiftFitStructKg` = 400 (every car after the first);
  - `FirstLiftFitStructKg` = 400 **[Q-A keep]** or 0 **[Q-A free]**.
- The take runs before the car is added. `HasTransit()` is therefore false during the first fit's own take, and that bill writes no ledger entry under either answer. (Its Struct moved while there was no transit system.)
- **[Q-A free]:** TR2's refusal 4 ("short of Struct") is tested on a second car, and on a first car with `FirstLiftFitStructKg` forced above 0 by a debug override (the D3 REVISION's (b) precedent). The shaft card reads "Fit a lift car (free)".
- **[Q-A keep]:** as the draft says. The fit alert (R1) states the cliff plainly, in the review's words: other shafts become 200-place hoists with a 30 s + 15 s-per-floor hold, and robots start taking jobs on the floors (item 4).

**Q-B (trams).**
- **[Q-B now]:** section 3, the tram rows in TR0, TR16, `LayTramLine`, `FRHTramLine`, `TramVersion` in the deep-net key, `LegEdge <= -1000` and the tram half of the block (item 9(c)) are built as the draft and this revision say. The tram ledger reads its wait through `GetRobotWaitSec` like a shaft's. Tram holds are priced at `MaxWaitSec` in the reserve (item 7).
- **[Q-B D6]:** none of that is built.
  - The deep net has only ride edges. `LegEdge < 0` means a ride only, and the two `< 0` branch sites (`RHSimRoads.cpp:388`, `RHTrafficVisualizerSubsystem.cpp:508`) still branch on it.
  - The deep-net key loses `TramVersion`.
  - Section 8 loses the tunnel card's tram line and the tram capsules.
  - -scale loses its 8 tram lines and the tram half of the cold-net budget.
  - "What you see" becomes "a second car or a second shaft clears it".
  - Deviation 2 becomes "Trams move to D6 with people (`going-deep-plan.md:38`)".
  - The suite still has its 49th run; TR16 is dropped, and TR16b is as O1 below.

## Optional items

**Adopted:**
- **O1. Section 4's rover change is cut** (the reviewer's top pick).
  - `IsFreightRoute`'s floor-drop refusal (`RHSimRovers.cpp:85`) stays in every case. `RefreshFreightFlags` (`:117-129`) gets no `LiftVersion` compare, so nothing re-derives at load. No C6a or W1 line is touched.
  - Floor freight stays hauler work, and a hauler at the head holds (item 6). The rover mentions in 1.5's last bullet and in 2.4's hold are struck.
  - **TR16b becomes:** with transit on, a 1,000 kg task to a -1 site is not freight. A hauler takes it, holds at the head, and charges 2 places for its 200 kg load.
  - Deviation 1 stands as "rovers stay on the surface", with no head hold.
- **O2.** Adopted in the guarded form of item 2(d). The plain form is rejected, as explained there.
- **O3.** Section 5's people equality reads "identical at every threshold when nothing changes in the window between the agent band's crossing sub-step and the era band's crossing step (at most 60 s, `RHSimClockSubsystem.cpp:19-23`)". TR17 holds that window quiet.
- **O4.** With transit on, `PickCommuteShaft` (`RHSimCommute.cpp:170-192`) compares `|L| x PerFloor(S) + CommuteSurfaceCm(head_S, P)`, still strict `<` with ties to the lower Id.
  - The added term changes only on a fit (`LiftVersion` is in the commute key), so it cannot oscillate.
  - With no transit, or one shaft, today's lines run.
  - TR6 adds a case: a car on shaft 1 takes over a trip whose cheapest bare head was shaft 0, when its lift saving outweighs the longer surface leg.
- **O5.** `TakeStructForBill` and `TakeGrowthResource` (`RHSimGrowth.cpp:260-279`) charge per source part.
  - Each part taken from a building with `Level < 0` charges `ceil(part / 100)` to that building's serving shaft, `ServingShaft(B.Level, CmToLattice(Shafts[0].HeadCm, B.LocationCm))` (`:5289`).
  - Surface parts charge nothing.
  - TR2's ledger check becomes per source.
- **O6. TR1 is redefined.** Two twins run from one start. Twin A fits a car at sol 5; twin B never does. Up to the fit step they must be bit-identical (stocks, Hope, `CommuteFlowRebuilds`, `CommuteDijkstraRuns`, every claim and sub-step count). The battery diff and the byte-identical playtests remain the proof of inertness with no car.
- **O7.** The `lifts=1` assertion "the ledger equals what the bot recomputes" is dropped. The bot (RedHope module) can see boardings only through a sim accessor, so it would not be independent. Mass conservation (1.8) is asserted per sol instead of per run, and `TransitUnstampedRobotWrites == 0` is added.
- **O8. `RH::MaxWaitSecCap = 1200`** (one sol). `MaxWaitSec` is clamped to 0..1200 at config load, and the save's `WaitSec` / `PrevWaitSec` ranges use the same cap. `WaitSecAtDouble` is asserted > 0, and every capacity row is asserted > 0, so the wait's division is safe.

**Also adopted (minor, my own):**
- `FRHCommuteKey` gains two fields, `Lift` and `TransitWait`, not the XOR form, so no pair of bumps can collide. `operator==` (`.h:2189`) compares both. With no car both stay 0.
- `RH::MoveAlong` gains `float CargoKg = 0.f`, because boarding happens inside it and needs the cargo's places. The Haul Phase 2 calls pass `Task.CargoKg`; every other call keeps the default.

**Rejected or corrected:**
- **Part of item 7:** removing `TransitWaitVersion` from the reserve memo key. Rejected, as explained in item 7: the route depends on the waits.
- **Item 9(b)'s reason:** the autosave cannot observe a `+1` slot. The rule is still right, because the quicksave and console saves can.
- **Item 2(b)'s anchors:** the brain's set point is `RHRobotBrainProcessor.cpp:112` inside the `:94-113` step loop (the review's `:89-110`), and the legacy point is `RHRobotTaskProcessor.cpp:67` (the review's `:66`).
- **None of O1-O8 is rejected outright.**

## Risks, updated

- **R2 is withdrawn** (item 2(b)).
- **R1** gains item 4's floor hauls and item 8's "no people load under 61".
- **R11** reads "anchors are at `5b89cc1`".
- **R13 (new): the people part reads world state at the crossing step.** A build a robot completes in the crossing frame can land on either side of that step, depending on the engine's tick order. That is the existing frame-schedule class of `StepCityGrowth` and `RollProductionLedger`: deterministic for a fixed schedule, never dependent on entity order. The cargo part is free of it (items 2-3).
- **R14 (new): robots see a new sol's waits 3.3 s after the commute does** (item 2(d)). That is invisible at play speed.

## Deviations to log in `docs/design-decisions.md` (updated)

1. Rovers never go below, and the rover gets no head hold (O1).
2. **[Q-B now]** Trams carry no people until D6. **[Q-B D6]** Trams move to D6 with people.
3. Waits (and `PrevWaitSec`) are saved.
4. Era teleports past lifts (and trams, under [Q-B now]).
5. `CommuteShaftCmPerFloor` is re-described as the uncrowded hoist.
6. The gate is a player order. The ramp still scales every effect, now including the robot waits (item 8).
7. **New.** The plan says "lift load is a function of the commute flows only, never of robot positions" (`going-deep-plan.md:75`). D4 adds a cargo ledger, written by robot cargo moves and by bills. It stays integer, commutative and stamped by sim time, so it never depends on entity order. Without it, the head hand-off would dominate for ever (D3 review item 8).

## Updated bar

- **Save v46, strict.** The default block is 20 bytes (0, -1, 0, 1, 0) **[Q-B now]** or 12 bytes (0, -1, 0) **[Q-B D6]**, hidden by the `(N bytes` mask.
- **Battery:** 49 runs, 0 not green; exactly 11 out and 11 in against `battery_post_d3` (item 10).
- **`-transit`** passes: TR0-TR18 with TR3's two new cases, the new TR5, TR6 at 60/61/160, TR9 in both colonies, TR14c, TR17 at capacity and TR18's `+1` and missing-row cases. TR16 is included only under [Q-B now], and TR16b is as O1. Every mutation in item 2(e) is run once by hand and fails its check (stated in the commit).
- **Playtests:** the default and `districts=1` 110-sol runs are byte-identical under the mask against the post-D3 runs. `lifts=1` passes 1.8's assertions, with O7's changes.
- **-scale** stays within every budget (without the tram parts under [Q-B D6]), and the one-shaft, no-car numbers do not move.
- **Logged:** the deviations above, and the director's answers to Q-A and Q-B once given.
