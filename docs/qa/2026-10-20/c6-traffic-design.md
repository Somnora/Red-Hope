# C6, traffic: design (draft for adversarial review)

Repo: /Volumes/Unreal/red_hope/red_hope (UE 5.8). Plan: docs/qa/2026-10-18/living-city-plan.md (C6 row, Q4a, appendix). Prior: C4 roads (save v36, REVISION binding), C5a-e districts and growth (save v39).

Rulings: Q3a (heavy cargo rovers need roads), Q4a (commuting is district-to-district flows; long commutes cost Hope and work time; street traffic is drawn from the flows; freight rovers are real), Q1a (named individuals), no combat.

## Facts the design rests on (read in the code, 2026-10-20)
- **Movement.** `RH::MoveAlong` (RHSimRoads.cpp) is `MoveToward` call for call when `!HasBuiltRoads()`. Otherwise `MoveAlongRoads` plans once per exact target via `PlanRobotPath`, stores it in `RobotPaths` (subsystem map, not a fragment) and reads edge speed live (`GetRoadEdgeSpeedMul`: built 1.6, missing 1.0).
- **Routing.** `FRHRoadGraph::PlanRoute` is integer: K=4 nearest built nodes per end, one Dijkstra from a virtual source, cost `M x off-road cm + 1000 x road cm` (M = SpeedMulPermille). It returns `bRoad=false` when the straight line is no dearer. There is no all-pairs table and no connected-component label.
- **Road demolish is still debug-only.** `Debug_RemoveRoadEdge` refunds and removes. No player verb exists (C4 item 12 deferred it).
- **Robots.** One Mass archetype; the class is `FRHRobotFragment.RobotClass` from DT_Robots. Both brains branch on it: `FRHWorkTask::Tick` (RHRobotStateTree.cpp) and RHRobotTaskProcessor.cpp:143-168. `ExtraHauler`/`AdvExcavator` manifest items spawn through `SpawnRobotTracked` (`ApplyManifestItemEffect`). Saves keep DefName, position, charge and wear. In-flight cargo goes back to its source at save (SaveColony).
- **Hauls.** `StepTaskBoard` posts Haul tasks with `AmountKg = min(available, 200)`, one per (From, To) via `HasOpenTask`. `TryClaimHaul` takes the nearest pickup by straight `DistXY`; the first found wins ties. `HaulLoad` loads `min(CargoCapKg, AmountKg)`. Underground drops go to the shaft head (`GetApproachPoint`).
- **Jobs.** `RefreshJobs` re-derives `ColonistStations` (Id -> station key) every step in `StepPopulation`. A key is `(Level, cell, 0)`, `(Level, INDEX_NONE, BuildingId)` or `(0, Bay, HabId)`. Job output reads `LabYieldSeatSum` (discovery), `WorkstationYieldSeatSum` (fabrication) and `GetCrewSkillMul` (garden yield, labs).
- **Homes and districts.** `GetDistrictOfColonist` returns the HomeHab's building district or the HomeLevel floor's district. Membership is cached on (Districts, Zoning, ColonistSet, Furniture, building set). Rehome bumps `ColonistSetVersion`.
- **Hope.** `GetColonyHope().Total` is one clamped sum. `StepHope` smooths it with exp(-dt/Tau), so the bands agree.
- **Era.** `EraLogistics` teleports. `CanEnterEraMode` refuses on unbuilt road edges. Robots park in era.
- **Suites that already hold both a district and a built road:** -citygrowth (case d5, line 9534 of RHSimCommandlet.cpp) and -scale (not in the battery). See Risks 1.

## 1. Cargo rovers (real sim vehicles)
- **Acquisition: a manifest item, `CargoRover`** (3,500 kg, Fleet). This is the `ExtraHauler` pattern: no new building, recipe or verb. Fleet growth stays import-gated, like power. A Fabricator recipe would need robot fabrication, which does not exist. Q1 asks the director.
- **Entity: the robot archetype, unchanged.** A new DT_Robots row `RC_R`, `RobotClass = Rover`, StartingCount 0. There are no new fragments, so the explicit StateTree archetype and external-data linking stay untouched (C4 item 4). A new archetype would duplicate the charge, wear, repair, save and visualizer plumbing for no gain. Both brains gain a `Rover` branch beside `Hauler`.
- **Row (placeholders):** Battery 6000 Wh, DrawMove 200 W, DrawWork 20 W, DrawIdle 8 W, Speed 5.0 m/s, Cargo 1000 kg, WearPerActiveSol 6. On a built edge it runs 5.0 x RoadSpeedMul = 8 m/s.
- **Landing.** The rover spawns at the built node nearest the Lander, ties by node Id. With no built road it goes to `ImportStock["RC_R"]` (an existing saved map). It deploys at the first step that has a built node, in both bands. Alert (Gate-D placeholder): "A cargo rover is waiting aboard the Lander for a road."
- **Road-only movement, with a last-leg rule and no depot.**
  - A rover plans with `PlanRoute` on a copy of `RoadParams` whose `SpeedMulPermille = 1000 / RoverOffRoadSpeedMul` (3333). Off-road cm become so dear that any road route wins.
  - A trip is **road-served** only if three things hold: `OffInCm <= RoverLastLegCm` (30 m), `OffOutCm <= RoverLastLegCm`, and the entry and exit nodes share a connected component.
  - Off-road legs run at `Speed x RoverOffRoadSpeedMul` (0.3). Road legs use the live edge speed. A missing edge counts as off-road (0.3), not 1.0.
  - `MoveAlongRoads` gains a speed-profile argument (Robot or Rover). Paths stay in `RobotPaths`, which is not saved.
  - Components: an integer flood fill in node-Id order, derived when the built version changes, beside `BuiltAdj` in `EnsureAdjacency`.
  - I chose this over a depot building because depots add placement, saves and a hand-off leg. The last leg is one integer test on numbers the planner already returns.
- **Which hauls.** A Haul task is **freight** when three things hold: its pickup (`GetSiteLocation(From)`) and drop (`GetApproachPoint(To, 0)`) are road-served, both are on level 0, and the straight distance is at least `FreightMinCm` (80 m).
  - `bFreight` is a runtime field on FRHTask. It is not saved. It is set at post and re-derived for every open task when the built version changes or after a load.
  - Freight is computed only while `ActiveRovers > 0`. An active rover is wear below halt, charge > 0, fleet not held. With no rover, every task is posted exactly as today.
  - While rovers are active, a freight task's AmountKg cap is `RC_R.Cargo_kg` instead of 200.
- **Claims.**
  - `TryClaimFreight`: the rover takes the unclaimed freight task with the nearest pickup by integer straight cm, ties by task Id.
  - `TryClaimHaul`: haulers skip freight tasks while `ActiveRovers > 0`. The exception is a task left unclaimed for `FreightHaulerFallbackSec` (300 s), measured from a runtime `PostedAt`. A load resets `PostedAt` to the load time, which is deterministic. A big task taken by a hauler moves 200 kg, and the rest is re-posted.
  - Rovers never take Dig, Build, Survey or Repair work.
- **Charge.** Rovers dock only at **road-served pads**. `FindNearestRoadServedPad` uses the same last-leg test from the pad's footprint centre.
  - The reserve is `DriveWh(straight x RoverReserveDetourMul)` (3.0), an O(1) bound. It is not a route plan per sub-step.
  - With no road-served pad, a rover claims nothing below the seek fraction. It parks at its node and the fleet panel says why.
- **Wear.** This is the existing `AccrueWear` and `GetWearWorkMul`. Maintenance units repair rovers by the straight-line Repair rule (C4 item 3).
- **Saves.** The format is unchanged for rovers: an RC_R robot saves like any other, and its cargo returns to the source at save. See section 6 for v40.
- **A road removed under a rover.** Today that can only happen through `Debug_RemoveRoadEdge`. There is no re-plan (C4 item 5): the leg drops to 0.3x and the rover still arrives. If the network splits, the task is completed by crawling. New claims see the new components. If Q3 adds a player demolish, the same rule holds.

## 2. Commuting flows
- **Gate (first line of every entry point):** `Districts.Num() == 0 || RoadGraph.BuiltEdgeCount == 0` returns the zero summary. No cache is touched and nothing is logged.
- **Home district** = `GetDistrictOfColonist(Id)`. **Work district** = the district of `ColonistStations[Id]`: `GetDistrictOfFloor(L)` for L < 0; for L == 0, `GetDistrictOfBuilding(HabId or BuildingId)`.
  - A **commuter** has both districts non-zero and different.
  - The same district means a commute of 0 ("live where you work"). An unzoned home or seat, or no job, means not a commuter.
- **Portals.**
  - A surface district's portal is its lattice cell nearest the integer mean of its cells. The mean is the int64 sum divided by the count. The nearest cell is by squared lattice distance, ties by (X, Y). Its cm is the lattice centre.
  - A floor district's portal is the shaft head (rounded whole cm) at its Level.
- **Distance (int64 walking-equivalent cm), for a pair (h, w):**
  - `Surface(A,B)`: with `CP = RoadParams` and `CP.SpeedMulPermille = CommuteRoadSpeedMul x 1000` (4000; shuttles beat walking more than robots do), `PlanRoute(A, B, CP)`. If it returns bRoad, the value is `(CostUnits + M/2) / M`. Otherwise it is the straight `RoundSqrt` (exposed from RHRoads.cpp).
  - Both on the surface: `Surface(Ph, Pw)`.
  - One end on floor L: `|L| x CommuteShaftCmPerFloor + Surface(ShaftHead, P_surface)`.
  - Both underground: `|Lh - Lw| x CommuteShaftCmPerFloor`.
  - Long means `Dist > CommuteLongCm` (300 m).
- **Caches** (C1 pattern, `-RHVerifyCaches` re-derives on reuse and logs an error on a difference):
  - **Pair table**, D x D (Districts order), memoized per pair on first need. Keyed on (DistrictsVersion, RoadGraph.BuiltVersion, ShaftHead). It stores Dist, a long flag and the route's EdgeIds.
  - **Flows**, keyed on (DistrictsVersion, ColonistSetVersion, membership key, JobsVersion). One O(N) pass fills `Flow[h][w]` (int32), `Commuters`, `LongCommuters` and a per-colonist long bit (an array by colonist index).
  - **JobsVersion is new.** `RefreshJobs` already builds the assignment in a fixed order. It compares that `(Id, key)` sequence with the last step's and bumps the version on any difference: O(N), no hashing, no collisions.
  - Cost: O(N) flows plus at most D² plans, only on a key change. A warm read is O(1).
- **Effects.**
  - **Hope:** `CommutePenalty = HopeCommuteMax x min(1, HopeCommuteSteepness x Long / Pop)`, stored positive in FRHHopeBreakdown with `Commuters` and `LongCommuters`. It is appended as the LAST term of the existing subtraction in `Total`.
  - **Work:** a long commuter's seat counts `CommuteWorkMul` (0.85). This has two hooks. (a) `GetCrewSkillMul` multiplies that colonist's term, in a branch taken only for a long commuter. (b) At the end of `RefreshJobs`, when `LongCommuters > 0`, a correction pass subtracts `YieldMul x (1 - mul)` from `LabYieldSeatSum` or `WorkstationYieldSeatSum` for each long commuter.
- **Exactly inert.**
  - When gated, no code path differs from v39 except the JobsVersion compare, which writes nothing the sim reads.
  - When open with `Long == 0`: the penalty is +0.0, and `x - 0.0 == x` for every finite double under IEEE 754, so Total is bit-identical. The skill and seat-sum branches are not taken.
  - There are no log lines in either case. The only line is an alert when the long share first crosses `CommuteAlertShare`, which needs Long > 0.
- **Era parity, argued.**
  - Every input (districts, homes, the job assignment, the built graph, the shaft head, config) is sim state. The same functions mutate it at the same step positions in both bands. No agent-only input (robot positions, RobotPaths, rovers) enters.
  - The built graph is frozen in era: `CanEnterEraMode` refuses on unbuilt edges, and road verbs are agent-band.
  - So commute outputs are a pure function of a state both bands share. The Hope term goes through the exp smoother, and the work multiplier is constant between changes, the same parity class as `GetCrewSkillMul`.
  - Commute parity is therefore exactly the parity of the job assignment, which existing suites pin. Test T7 checks it bit for bit at the split.

## 3. Presentation (RedHope; reads const accessors only, never writes the sim)
- **Shuttles and commuter cars**, a new `URHTrafficVisualizerSubsystem`, one HISM per kind.
  - Per pair with Flow f: `ceil(f / ShuttleSeats)` shuttles (20 seats), or one small car when f < 5. The whole screen is capped at 400 vehicles, cut largest flows first.
  - Each vehicle moves along the pair's route polyline (portal, nodes, portal) and loops. Its phase is `frac(t x v / L + Crc32(hId, wId, k) / 2^32)`.
  - t is sim seconds in the agent band and real seconds at era speed, so 60x does not blur.
  - Underground ends fade out at the shaft head. Rebuild on the flows key. Per frame it is one transform per instance.
- **Real rovers** go through `RHAgentVisualizerSubsystem` as robots. They branch on DefName `RC_R` to a cube-built rover (bed, cab, six wheels, the existing cargo lump). They are drawn on the sim's waypoints by arc length (C4 item 9). The art is a placeholder until a rover mesh exists.
- **TRAFFIC overlay** (a toggle beside the district overlay). Per-edge load = commuters (sum of Flow over the pairs whose route uses the edge) + `RoverHeatWeight` (10) x the rovers whose current path includes it.
  - Heat ramp: grey, yellow, orange, red, normalised to the busiest edge. It is vertex-coloured on the C4 road PMC. It rebuilds on the flows key, and freight refreshes at 2 Hz.
  - Hover: "Edge #12: 48 commuters, 2 freight rovers".
- **Gate-D placeholder copy** (all of it): the Hope line "Long trips to work", the City panel line "N people travel over 300 m to work; homes nearer their work would give them time back", the alert, and the rover-waiting alert. No stress, despair or harm wording.

## 4. Era band
- Rovers park like every robot. `EraLogistics` keeps teleporting, and rovers add no era term. The C4 deviation stands.
- **Rover cargo at the era switch.** On the first era step after agent steps, each rover's in-flight cargo returns to its source and its task is abandoned. This uses the SaveColony return logic, factored into one helper. Without it, 1 t per rover would vanish for the whole era run. Haulers are untouched, for battery identity.
- **The 5% bar.** Rovers only speed the agent band up, toward the teleport. The parity test adds a case with roads finished, 2 rovers, freight-distance sites and long commuters. The agent and era 10-sol ledgers must stay within 5%, and commute summaries must be identical at the split.

## 5. Tests
**A new `-traffic` suite (fails loudly), in rh_battery.sh as the 41st suite:**
- T1 inert: roads without districts, districts without roads, and both with only same-district work. Each must give a zero summary, a bit-identical Hope Total, skill mul and seat sums against the gate-forced-closed twin, and no new log lines.
- T2 districts: floor home, hab home, floor-cell seat, module seat, surface-bay seat, unzoned home, unzoned seat, unemployed.
- T3 distance: hand-computed integers for straight, road, shaft-to-surface, floor-to-floor, a disconnected network (straight), and portal ties.
- T4 determinism: the same city built in two orders gives identical Flow, Dist and long bits.
- T5 caches: each key input bumps and recomputes. An unchanged read returns the cache. -RHVerifyCaches is clean.
- T6 effects: share and cap arithmetic; lab and workstation sums and garden skill mul with long commuters; rehoming nearer clears the penalty.
- T7 era: summary identical at the split; ledger within 5%.
- T8 rovers: landing with and without a road (ImportStock); claims freight only; haulers skip freight then fall back after 300 s; 1000 kg loads; road vs crawl speed; the last-leg refusal; charging only at a road-served pad; wear; an edge removed mid-trip still arrives; save mid-trip returns cargo; cargo settles at the era switch.
- T9 save: v40 round trip of a rover colony; a v39 save is refused.

**-scale:** 5,000 colonists, 20 districts (5 floors + 15 surface), 250 edges.
- Budgets (wall clock, about 20x a quiet run): cold pair table plus flows <= 5 ms; flows only <= 1 ms; warm <= 0.01 ms; GetColonyHope stays inside 0.5 ms.
- 100 rover plans <= the C4 robot-plan budget.

**Battery:** line-identical (masked lines as today). Risk 1 names the one suspect case.

**Playtest:** `RH_PLAYTEST_ROADS=1` (off by default). The bot lays a spine from the Lander to its districts and orders a CargoRover on ship 2.
- Expected: at the default layout, vault-floor homes and floor seats, there are 0 long commuters and Hope is unchanged.
- A variant with a far surface Homes district shows the penalty and its fix.
- Report commuters, long share, the Hope term, rover trips, freight kg and hauler trips.

## 6. Save v40
- **New fields: none.** Commute caches, flows, `JobsVersion`, `bFreight`, `PostedAt`, paths, components and edge loads are all derived. Rovers use the existing robot record, and an undeployed rover uses the existing ImportStock map.
- **Why bump anyway:** a v39 binary must refuse a save that can hold RC_R robots, `ImportStock["RC_R"]` and Haul tasks over 200 kg. The checkpoints re-make themselves.

## 7. Placeholder numbers (docs/data, live-synced to the DTs; pure-data verifiers fail on drift)
- RH_Config:
  - `RoverOffRoadSpeedMul` 0.3: crawl off the road, and on a missing edge.
  - `RoverLastLegCm` 3000: the furthest a rover leaves the road at each end.
  - `FreightMinCm` 8000: shorter hauls stay with haulers.
  - `FreightHaulerFallbackSec` 300: haulers take freight left unclaimed this long.
  - `RoverReserveDetourMul` 3.0: the charge-reserve bound over the straight line.
  - `CommuteRoadSpeedMul` 4.0: commuters' road speed for the distance test.
  - `CommuteShaftCmPerFloor` 6000: the lift and its wait, counted as 60 m a floor.
  - `CommuteLongCm` 30000: a long commute.
  - `HopeCommuteMax` 4.
  - `HopeCommuteSteepness` 2: half the city long costs the whole term.
  - `CommuteWorkMul` 0.85.
  - `CommuteAlertShare` 0.10.
  - Every note ends "Placeholder, director review".
- RH_Robots: `RC_R` (above).
- RH_ManifestItems: `CargoRover`, 3500 kg, Fleet, "+1 cargo rover (needs a road)".
- `ShuttleSeats` and `RoverHeatWeight` are presentation constants in the visualizer, not sim config.

## 8. Questions for the director
- **Q1. How does the colony get cargo rovers?**
  - a (recommended): Earth ships them, as a manifest item, like extra haulers.
  - b: The Fabricator builds them from Struct and parts. Needs a robot-building recipe first.
- **Q2. What counts as a long commute?**
  - a (recommended): over 300 m of travel, roads and lifts counted, between DIFFERENT districts. Living and working in one district never counts.
  - b: the same, but a big district also counts trips inside it (heavier and fussier).
  - Note: commuting only switches on once the city has a district AND a road. Your first road can therefore lower Hope (Risk 3).
- **Q3. Should you be able to tear up a road in C6?**
  - a (recommended): yes, with 50% salvage like buildings. A rover on it slows to a crawl but finishes its trip.
  - b: later.

## Risks I see
1. **Battery identity is not guaranteed by the gate alone.**
   - -citygrowth d5 paints a district and builds a road. The rule "same district = 0" should keep it inert, but only the line-diff proves it. If it moves, the fix is in d5's layout, and the commit must say so.
   - -scale (outside the battery) has 5 floor districts, roads and 5,000 seated colonists. Its Hope and sink digests may move on purpose.
2. **Portal approximation.** A district made of scattered rectangles has one portal, which can sit far from where people live. Same-district commutes count 0, so one giant district hides every commute. Unzoned people are exempt, which rewards not painting.
3. **Cliff at the first road.** The term switches on when a built road first exists, as specified. Building a road can lower Hope that day. The alert must explain it, or the gate needs revisiting.
4. **Hauler starvation.** If every rover is charging, freight waits up to 300 s. The fallback bounds it, but throughput can dip.
5. **Frame batching.** Rover-versus-hauler claims follow entity order inside a frame. That is deterministic, but it is not frame-rate independent, as with every claim today.
6. **The rover reserve is a bound, not a route.** A very winding road can be more than 3x the straight line, and a rover could run flat. T8 asserts the bound only on its own networks.
7. **The era-switch cargo settle** mutates agent-band state at a moment haulers do not. Mass is conserved, but it is a new asymmetry.
8. **D² plans** use per-pair Dijkstra. Above about 50 districts or 2,000 edges, change to one Dijkstra per district portal.
9. **Three DTs gain rows** (Robots, ManifestItems, Config). A missed live-sync fails the pure-data verifiers. Any verifier that counts DT_Robots rows must be updated.

---

# PROFESSOR REVIEW (2026-10-20)

**Verdict: BLOCKED.** The architecture holds up: rovers as a robot-row class, commute as derived state, the C1 cache pattern, and v40 with no new saved fields. The defects are local but real. One is a cache key that goes stale, one a double penalty, one a path that destroys mass, one a premise in section 4 that is false, one a hopper rule that is bypassed, and one a gameplay exploit. Required changes 1-8 below form the REVISION. Once they are written into this doc, C6 is cleared to build without another review round. The exception is item 8, which needs a director answer first.

## What I verified and found sound
- **Movement and routing facts (section "Facts").** All match the code: `RHSimRoads.cpp:27-34`, `:346-410`, and `RHRoads.cpp:423-580`. The same goes for `TryClaimHaul` (`RHSimWorldSubsystem.cpp:10127-10154`, float `DistXY`, first-found ties) and `HaulLoad` (`:10156-10204`).
- **Battery identity (invariant 1).** The gate `Districts.Num()==0 || BuiltEdgeCount==0` keeps `-districts` (it lays no road) and `-roads` (it paints no district, `RHSimCommandlet.cpp:7535-8183`) inert. In `-citygrowth` d5, `Debug_LayRoad` at `:9534` is followed only by `Debug_RunGrowthCheck` and then `LoadBase()`, with no sim step between them. The gate is open, but nothing that prints reads Hope Total or the seat sums. The doc's stated reason ("same district = 0") is the wrong reason. The real reason is that no step runs. Fix the wording.
- **Data rows.** RC_R with StartingCount 0 does not move `SpawnStartingFleet`: `Slot` increments only per spawn (`RHSimWorldSubsystem.cpp:387-397`). The playtest picks manifest items by name (`RHPlaytest.cpp:204-217`), so adding the CargoRover row moves no pick.
- **Save (invariant 4).** Tasks are written field by field (`:10880-10885`), so the runtime `bFreight` and `PostedAt` fields cannot leak into the format. The robot record is DefName/Pos/Level/Charge/Wear (`:10891`). Claims are cleared (`:10773`). `RobotPaths` is reset at load (`:11665`). The counters only go up across a load (`RHRoads.cpp:53-54, 70-71`, `RHSimWorldSubsystem.cpp:11734, 11805`), so version-keyed caches cannot hit stale across a load. "No new fields" holds, subject to optional item O1.
- **Era parity of commute.** Commute is a pure function of state both bands share, and the exp smoother (`:5446-5457`) keeps parity. I agree with the argument.
- **IEEE inertness.** Appending `- CommutePenalty` as the last term inside the clamp (`:5440-5442`) is bit-identical when the penalty is +0.0.
- **Reserve.** Risk 6 is better than the doc claims, once required item 7 is in (see there).

## REQUIRED changes

1. **The flows cache key must include the pair-table key.**
   - Section 2 keys flows on (DistrictsVersion, ColonistSetVersion, membership key, JobsVersion), but stores `LongCommuters` and the per-colonist long bit, and both are functions of `Dist`. `Dist` changes with `RoadGraph.BuiltVersion` and with ShaftHead.
   - Failure: a Homes surface district 400 m from a Research floor district, with a shortcut road under construction. Flows are computed (long, straight distance). The edge finishes, and `MarkBuilt` bumps `BuiltVersion` (`RHRoads.cpp:367`). The pair table re-plans, but the flows key is unchanged. The stale LongCommuters keep the Hope penalty and the 0.85x until some unrelated rehome or job change.
   - Fix: add (BuiltVersion, ShaftHead) to the flows key. Or keep the long flag only in the pair table and derive `LongCommuters = Σ Flow[h][w]·Long[h][w]` (D², cheap) on read.
   - T5 must include "a road finishes and nothing else changes".

2. **No double penalty on labs.**
   - Discovery is `LabYieldSeatSum * GetCrewSkillMul("Lab")` (`RHSimWorldSubsystem.cpp:6067`). Hook (b) cuts the seat sum and hook (a) cuts the skill average, so a long-commuting lab worker counts about 0.85² = 0.72.
   - Fix: apply exactly one hook per function. Hook (b) covers Lab and Workstation; workstations read only `WorkstationYieldSeatSum` via `GetWorkstationFabMul`. Hook (a) covers Garden only (`GardenSkillMul`, `:3746`).
   - T6 asserts each of the three functions at exactly 0.85 for an all-long crew.

3. **A rover must never destroy its cargo.**
   - The StateTree charge path abandons a Phase-2 haul and zeroes the fragment (`RHRobotStateTree.cpp:292-294, 307`; the comment at `:420-422` says "the cargo is lost either way"). For a hauler that is 200 kg. For a rover it is 1 t.
   - A road edge removed mid-trip drops legs to 0.3x and breaks the reserve proof (item 7), which makes this path reachable.
   - Fix: before resetting, a rover with `CargoKg > 0` calls a sim helper, `ReturnHaulCargo(TaskId, Resource, Kg)`, with the same routing as SaveColony: the From deposit pile or building, else the Lander. Do this in both brains. Haulers stay as they are, for battery identity. Log the deviation.
   - T8 adds "a rover forced to charge mid-delivery conserves mass".

4. **Delete the era-switch cargo settle (section 4, Risk 7).**
   - Its premise is false. SaveColony returns cargo on snapshot copies (`SavedBuildings` and `SavedDeposits`, `RHSimWorldSubsystem.cpp:10761-10822`). The live colony is never touched, so there is no live "return logic" to factor out.
   - In the era band, robots do not tick. `GetStepsThisFrame()==0` and the processors return (`RHRobotTaskProcessor.cpp:43-47`). The rover's fragment keeps its cargo, and it delivers when the band drops. Nothing vanishes.
   - The settle would be the first write into Mass fragments from `EraStep`. It would also re-abandon rover tasks on every auto-drop and re-entry cycle (`Tick`, `:461-474`).
   - If T7's ledger misses 5% because of in-flight mass, count in-flight cargo (every carrier, in both bands) in the ledger instead.

5. **Freight AmountKg must respect the consumer's 2-batch rule.**
   - The board sends to a consumer only while `Have < 2 x Need` (`RHSimWorldSubsystem.cpp:882-892, 956-966, 1043-1053`). With the cap at 1000, a single drop blows past it.
   - Failure: BoreFloor and CarveCell output 1,200 kg of Regolith a batch (RH_Recipes). The Forge's need is 100 a batch. The freight task is `min(1200, 1000) = 1000`, so one Forge ends up holding 1,100 kg, 11 batches, while a second Forge starves. That is exactly the hoarding D2a's rule was written to stop.
   - Fix: for a consumer destination, `AmountKg = min(available, cap, 2 x Need - Have)`, where Need is the input of the recipe that qualified. Stores stay uncapped. Sites are already capped by `NeedKg` (`:1128`).
   - T8 adds a Borer→Forge case.

6. **The rover's own leg must be road-served, not only the task.**
   - `TryClaimFreight` checks pickup→drop only. Failure: an edge removed (Debug, or Q3a demolish) splits the network into West, with the rover and the pad, and East, with the freight. The task is road-served inside East, and the rover claims it as the nearest pickup. `PlanRoute` finds no connected exit (`RHRoads.cpp:534-548`, bRoad=false), so the rover crawls cross-country at 0.3x. That breaks Q3a ("rovers need roads").
   - Fix: the claim must also pass the last-leg test on PlanRoute(rover → pickup), with rover params. Evaluate it only for the straight-nearest candidate, then the next, and so on, with a hard cap on plans per claim.
   - The task-side "share a component" test is redundant. PlanRoute returns bRoad only when the exit was settled from an entry. Drop the flood fill, or use it only as a cheap pre-filter for this rover-side test.

7. **The rover speed profile must hold on every movement path, and the planner metric must match it.**
   - Four places leak full speed:
     - `RH::MoveAlong`'s no-roads branch (`RHSimRoads.cpp:29-32`, MoveToward at full speed once every edge is gone);
     - the first leg (`LegEdge==0` gets `Mul=1.0`, `:368`);
     - the last leg (`MoveToward(..., SpeedMps, ...)`, `:397`);
     - `GetRoadEdgeSpeedMul` returning 1.0 for a missing edge (`:315`).
   - `PlanRobotPath` hard-wires `RoadParams` (`:320`). It must take the rover params.
   - Set `M_rover = round(1000 x RoadSpeedMul / RoverOffRoadSpeedMul) = 5333`. The time-true C4 model is `1000 x v_road/v_off`. The doc's 3333 drops RoadSpeedMul. With 3333, straight 100 m, legs 40 m and road 250 m crawls for 66.7 s when the road takes 58 s.
   - Then replace Risk 6 with this proof. Any plan with bRoad is strictly quicker than crawling the straight line, so the worst trip is straight/(0.3 v). The reserve `DriveWh(3.0 x straight)` covers it whenever `RoverReserveDetourMul x 1.25 >= 1 / RoverOffRoadSpeedMul` (3.75 >= 3.33). Assert that at config load.
   - A mid-trip edge removal is the one case outside the proof, and item 3 covers it.

8. **The commute metric needs a director decision on its loopholes before it is built.**
   - Two exploits exist under section 2 as written, both concrete:
     - (a) Paint a Homes district on the surface habitat and leave floor -1, which holds every seat, unzoned. Every worker becomes "not a commuter", so the penalty is 0 at any distance.
     - (b) Paint one Civic district over the whole colony. Same-district counts 0, so the penalty is 0.
   - Risk 3 adds a third: the first road anywhere, even one unrelated to any commute, switches the penalty on.
   - Recommended fix, compatible with Q4a: districts stay the unit for aggregation and display, and distance is measured by location.
     - `Dist = straight(home anchor → P_h) + Pair(P_h, P_w) + straight(P_w → work anchor)`.
     - Within one district, `straight(home anchor → work anchor)`.
     - An unzoned end uses its own anchor as its portal, memoized per distinct anchor on the pair-table key.
     - The cost is O(N) plus plans per distinct anchor (buildings and floors, not colonists).
   - The gate can stay as it is for battery inertness. Q2 must name the exploits and the first-road cliff, and recommend this fix. Do not build section 2 until it is answered.

## Optional changes
- **O1.** The long-share alert latch and the "rover waiting" alert latch are runtime and reset at load. That is the existing precedent (`bWaterQualityAnnounced`, `:11470`). Say so, so that "no new fields" is exactly true.
- **O2.** Compute `ActiveRovers` once per StepSim, in StepTaskBoard, through a new agent-subsystem accessor (charge and wear). Today's `ForEachRobotState` has no charge. Also re-derive `bFreight` when ActiveRovers crosses 0↔>0, not only on BuiltVersion and load.
- **O3.** Make `PostedAt` an "unclaimed agent-band seconds" accumulator advanced in StepTaskBoard, so the 300 s fallback does not fire for every freight task at once when the era band drops, since EraStep never posts or claims.
- **O4.** Only call a task freight when AmountKg is above the hauler's capacity (200). Rovers should not run 25 kg trips (`HaulLoadMinKg`), and haulers would do those in one trip anyway.
- **O5.** Memoize unordered pairs canonically as (min, max). PlanRoute's cost is symmetric, but on ties its EdgeIds are not, and the overlay would draw the same flow on two routes.
- **O6.** A seat module standing in a habitat bay takes its own anchor cell's district (`GetBuildingAnchorLattice`), not the habitat's. Decide which one is the work district.
- **O7.** `ImportStock` is keyed by building DefName and read by placement (`:6706`). An RC_R key there is harmless today. A comment, or a dedicated runtime-derivable count, would stop a future "list flat-packs" UI from showing a rover.
- **O8.** "Compare with the last step's" should read "the last call's". RefreshJobs runs from 9 call sites, including growth and load. Also update `rh_battery.sh`'s "40 suites" header and echo line.
- **O9.** Decide whether the Lander emergency dock counts as a road-served pad. Today it is every robot's fallback (`:10344-10357`).

VERDICT: BLOCKED. Unblock when required changes 1-7 are written into a REVISION section of this doc and the director has answered item 8 (Q2 expanded with the unzoned and giant-district exploits).

---

# REVISION (2026-10-20)

This section answers the PROFESSOR REVIEW above. Where it disagrees with sections 1-8 or with "Risks I see", the revision wins. Code anchors are as read on 2026-10-20 (main @ 891a700).

## Required changes, as they will be built

**R1. The flows cache key includes the road graph.**
- The flows key is a SUPERSET of the pair-table key: (DistrictsVersion, ColonistSetVersion, the district membership key, JobsVersion, `RoadGraph.BuiltVersion`, ShaftHead in whole cm, building count, NextBuildingId).
- `MarkBuilt` bumps BuiltVersion (`RHRoads.cpp:367`). A finished edge therefore re-derives the pair table AND the per-colonist long bits, `LongCommuters`, `CommutePenalty` and the seat-sum correction on the next read.
- I chose the superset over "derive LongCommuters on read" because the work hooks need the per-colonist bit, not only the count.
- T5 adds: "a road edge finishes and nothing else changes". The penalty must drop on the next step, and -RHVerifyCaches must stay clean.

**R2. One commute hook per job function. A long-commuting worker counts exactly `CommuteWorkMul` (0.85), never 0.85².**
- **Lab and Workstation: hook (b) only.** At the end of `RefreshJobs` (`RHSimWorldSubsystem.cpp:4647`), when `LongCommuters > 0`, subtract `YieldMul x (1 - CommuteWorkMul)` from `LabYieldSeatSum` or `WorkstationYieldSeatSum` for each long commuter's seat.
  - Discovery reads `LabYieldSeatSum * GetCrewSkillMul("Lab")` (`:6067`), and the skill average stays uncut.
  - Fabrication reads only `GetWorkstationFabMul` (header `:972`).
- **Garden: hook (a) only.** `GetCrewSkillMul` multiplies a colonist's term by `CommuteWorkMul` only when `Function == Garden` and that colonist's long bit is set. It feeds `GardenSkillMul` (`:3746`).
- Both hooks take no branch when nobody is long, so v39 bits are unchanged.
- T6 adds a case with an all-long crew in each of the three functions. It asserts the lab discovery rate, the fab multiplier's seat term and the garden yield at exactly 0.85x the twin with no roads.

**R3. A rover that breaks off a delivery returns its cargo; it never destroys it.**
- New sim helper: `ReturnHaulCargo(int32 TaskId, FName Resource, double Kg)`.
  - It routes like SaveColony's snapshot code (`:10783-10820`), but on the LIVE colony. The task's From deposit gets `PileKg += Kg`. A From building gets `OutputKg[Resource] += Kg`. If there is no task or no source, the Lander gets `OutputKg[Resource] += Kg`.
  - It then broadcasts OnStockChanged.
- Call sites, for rovers only (`RobotClass == Rover`, `CargoKg > 0`), before the fragment is reset:
  - the StateTree charge entry (`RHRobotStateTree.cpp:292-294` abandon, `:307` reset);
  - the legacy charge seek (`RHRobotTaskProcessor.cpp:80-91`);
  - the Phase-2 arrival when `HaulUnload` finds no task (`RHSimWorldSubsystem.cpp:10206`). The task can be gone because a demolish removed it. Today that cargo vanishes silently. For a rover it returns to the Lander.
- The comment at `RHRobotStateTree.cpp:420-422` ("the cargo is lost either way") becomes "a hauler's cargo is lost; a rover's goes back".
- Haulers are unchanged, for battery identity. The asymmetry is logged in design-decisions as a deviation for the director: haulers still lose up to 200 kg on a forced charge.
- T8 adds two cases:
  - A rover forced to charge mid-delivery: the colony's total mass of that resource is identical before and after, to 1e-6 kg.
  - A rover whose destination is demolished mid-trip: the cargo is on the Lander.

**R4. The era-switch cargo settle is deleted.**
- Section 4's "rover cargo at the era switch" paragraph is withdrawn, and so is Risk 7.
- The professor is right. SaveColony returns cargo only on snapshot copies (`:10761-10822`). Robots do not tick in era (`RHRobotTaskProcessor.cpp:43-47`, `GetStepsThisFrame()==0`). A rover's fragment keeps its cargo and delivers when the band drops. Nothing vanishes, and `EraStep` never writes Mass fragments.
- If T7's ledger misses 5% because of in-flight mass, the fix goes in the ledger, not the colony. The ledger would count in-flight cargo of every carrier, in both bands. That is a test-side change only.

**R5. A freight load never overfills a consumer's hopper.**
- In `StepTaskBoard`, each `Consider` closure (`:866`, `:938`) also records the qualifying recipe's `Need` and the candidate's `Have` for the chosen destination. So does the store→producer leg (`:1043-1053`).
- The three amount lines (`:918`, `:997`, `:1068`) become:
  - not freight (or no active rover): `min(available, 200)`, exactly as today;
  - freight to a CONSUMER: `min(available, RC_R.Cargo_kg, 2 x Need - Have)`;
  - freight to a STORE (Stockpile or Lander): `min(available, RC_R.Cargo_kg)`.
- Construction-site legs (`:1128`) are already capped by `NeedKg`. For freight their cap becomes `min(NeedKg, RC_R.Cargo_kg)`.
- With O4 (below), a leg is freight only if that amount is above 200. A hopper with less than 200 kg of room is never a rover trip.
- T8 adds the professor's case: a Borer leaves 1,200 kg of Regolith, and there are two Forges (Need 100). The first freight task must be ≤ 200 - Have, so it fails the freight test and goes to a hauler. A store-bound pile of 1,200 kg is a 1,000 kg rover trip.

**R6. A rover claims only work it can reach by road.**
- `TryClaimFreight` keeps freight tasks sorted by straight integer cm from the rover to the pickup, ties by task Id. It walks them in that order. For each, it plans `PlanRoute(rover → pickup, RoverParams)` and accepts the first task whose plan has `bRoad` and `OffInCm <= RoverLastLegCm` and `OffOutCm <= RoverLastLegCm`.
- At most `RoverClaimPlans` (4) plans per claim attempt. If none passes, the rover claims nothing this step.
- The claim attempt runs only while the rover is idle. That is the same O(R·T) shape the board has today, plus at most 4 plans per idle rover.
- The flood-fill component label is DROPPED. `PlanRoute` returns `bRoad` only when an exit was settled from an entry (`RHRoads.cpp:534-548`). The pickup→drop plan made when a task is classified freight already proves connectivity. That classification plan runs once per task at post, and again on a BuiltVersion change or when ActiveRovers crosses 0.
- T8 adds the professor's case. The network is split by `Debug_RemoveRoadEdge`, with the rover and pad in the West and freight in the East. The rover must not claim the East task, and a hauler takes it after the fallback.

**R7. The rover speed profile holds on every movement path, and the planner matches it.**
- New `struct FRHMoveProfile { const FRHRoadParams* Params; double OffRoadMul; double MissingEdgeMul; }`.
  - Robots use `{&RoadParams, 1.0, 1.0}`, which is today's behaviour.
  - Rovers use `{&RoverRoadParams, RoverOffRoadSpeedMul, RoverOffRoadSpeedMul}`.
  - `RoverRoadParams` is a copy of `RoadParams` with `SpeedMulPermille = round(1000 x RoadSpeedMul / RoverOffRoadSpeedMul)`, which is 5333 at 1.6 / 0.3.
- `RH::MoveAlong` takes the profile. Both brains pass it by `Robot.RobotClass`, from the class branches at `RHRobotStateTree.cpp:66-118` and `RHRobotTaskProcessor.cpp:143-168`. It applies in all four places:
  1. the no-roads branch (`RHSimRoads.cpp:29-32`): `MoveToward(..., SpeedMps x OffRoadMul, ...)`;
  2. the off-road legs inside the leg loop (`:368`): `Mul = LegEdge == 0 ? OffRoadMul : EdgeMul(edge)`;
  3. the last leg and the straight branch (`:397`, `:404`): `SpeedMps x OffRoadMul`;
  4. `GetRoadEdgeSpeedMul` (`:312`): it takes the profile and returns `MissingEdgeMul` for a missing or unbuilt edge.
- For robots every multiplier is 1.0. `float x 1.0f` is exact, but to be safe the robot path keeps the existing literal calls behind `if (&Profile == &RobotProfile)`. That way the no-roads battery path does not change by one instruction.
- `PlanRobotPath(Path, From, Target, const FRHRoadParams&)` (`:323`) is parameterised and `MoveAlongRoads` (`:353`) passes `*Profile.Params`. `RobotPaths` stays keyed by entity, so one entity always plans with one class.
- **Risk 6 is retired by proof.**
  - With M = 5333 the planner's cost is time-true: cost / (1000 x v_road) = time.
  - Any plan with `bRoad` is strictly cheaper than `M x straight`, so it is strictly quicker than crawling the straight line at 0.3 v. A plan without bRoad IS that crawl.
  - So the worst trip to a pad takes straight / (0.3 v). The reserve `DriveWh(RoverReserveDetourMul x straight)` spends `3.0 x 1.25 = 3.75 >= 1 / 0.3 = 3.33` times the straight-line drain.
  - Assert at config load: `RoverReserveDetourMul x 1.25 >= 1 / RoverOffRoadSpeedMul`, else ensure and clamp the multiplier up.
  - The one case outside the proof is an edge removed mid-trip. R3 covers it: the cargo is safe, and the rover can halt flat where it stands, as any robot can today.

## Optional items
- **O1, adopted.** Both alert latches (long-commute share; rover waiting aboard) are runtime and reset at load. The precedent is `bWaterQualityAnnounced` (`:4301-4308`). "No new saved fields" is exact.
- **O2, adopted.** `ActiveRovers` is counted once per `StepSim` at the top of `StepTaskBoard`, through a new `URHAgentSubsystem::CountActiveRobots(FName RobotClass, float WearHalt)` that reads the battery and wear fragments. `bFreight` is re-derived when ActiveRovers crosses 0↔>0, when BuiltVersion changes, and after a load.
- **O3, adopted.** `PostedAt` becomes `UnclaimedAgentSec`, advanced by SubDt in StepTaskBoard for unclaimed freight tasks only. Era steps never advance it, so a band drop does not release every freight task to haulers at once.
- **O4, adopted.** Freight requires the R5 amount to be above 200 (the RC_H cargo).
- **O5, adopted.** Pairs memoize as (min, max) on Id, and the route is planned from the lower-Id end. The overlay therefore draws one route per pair.
- **O6, decided: the module's own cell.** A seat module in a habitat bay takes its own anchor cell's district (`GetBuildingAnchorLattice`). This is the C5a rule ("a station module inside a bay is its own building and takes its own cell's district"), and `ColonistStations` already keys it by its building Id. A bay seat takes the habitat's district.
- **O7, adopted in part.** The undeployed rover stays in `ImportStock` under the key `RC_R`, with a header comment saying it is not a building flat-pack. Placement's lookup (`:6706`) can never match a robot row name. I reject a dedicated counter because it would be a new saved field.
- **O8, adopted.** It is "the last call's" assignment: RefreshJobs runs from 9 call sites. `rh_battery.sh` becomes "the baseline plus all 41 suites", with "traffic" added to SUITES and the echo line.
- **O9, decided: no exception.** The Lander dock counts for rovers only if it passes the road-served test. Q3a says rovers need roads, and a Lander exception would send rovers crawling to it. A rover with no road-served pad follows section 1's rule: it claims nothing below the seek fraction.

## Wording fix (battery identity, -citygrowth d5)
d5 stays inert because no sim step runs between `Debug_LayRoad` (`RHSimCommandlet.cpp:9534`) and `LoadBase()`. Only `Debug_RunGrowthCheck` runs, and nothing it prints reads Hope Total or the seat sums. The "same district = 0" reason given earlier was wrong.

## R8. Commute distance: two variants for the director (Q2 below)

**Variant (a), recommended: distance by location.** Districts are only the rows and columns of the flow matrix and the display.
- **Anchors.**
  - Home anchor: `(HomeLevel, shaft head)` for a floor resident; the HomeHab building's `LocationCm` in whole cm for a habitat resident.
  - Work anchor: `(L, shaft head)` for a floor seat (`ColonistStations` key Level < 0); the seat's building (`HabId` or `BuildingId` in the key) `LocationCm` on the surface.
  - Anchors are interned per distinct value, in the order they are first met while walking colonists in Id order.
- **Distance between anchors:**
  - both underground: `|Lh - Lw| x CommuteShaftCmPerFloor`;
  - one end on floor L: `|L| x CommuteShaftCmPerFloor + Surface(shaft head, surface anchor)`;
  - both on the surface: `Surface(A, B)`;
  - `Surface(A, B) = min(straight, road)`, where the road value is `(CostUnits + M/2) / M` under the commuter params (M = 4000).
- **Cost.** One full Dijkstra per distinct SURFACE anchor: a new `FRHRoadGraph::DistancesFrom(FIntPoint, const FRHRoadParams&, TArray<int64>& Dist, TArray<int32>& Prev)`. It is the PlanRoute loop (`RHRoads.cpp:494-548`) without the early exit, with the same tie rules.
  - A pair then costs K exit lookups, which is the same number PlanRoute computes.
  - Total: O(A x E log V + P x K + N), where A = distinct surface anchors (buildings and the shaft head, not colonists) and P = distinct anchor pairs (≤ N).
- **Unzoned ends are NOT exempt.** Every employed colonist is measured. A home or seat outside any district goes to an "Unzoned" row or column of the flow matrix.
- **Same district is NOT zero.** It is the anchor distance like any other. Both of the professor's exploits close.
- **The inert gate (no first-road cliff).** The gate is a population ramp, not roads or districts:
  - `Ramp = clamp((Pop - CommuteRampStartPop) / (CommuteRampFullPop - CommuteRampStartPop), 0, 1)`, with 60 and 160.
  - `CommutePenalty = Ramp x HopeCommuteMax x min(1, Steepness x Long / Pop)`.
  - A long commuter's work multiplier is `1 - Ramp x (1 - CommuteWorkMul)`.
  - When `Ramp == 0` every entry point returns the zero summary before touching any cache.
  - Roads only ever SHORTEN `Surface` (it is a min with the straight line), so a road can never raise the penalty. Painting a district changes no distance. The ramp is continuous in population.
  - The only step left is a single colonist crossing `CommuteLongCm`, which is inherent to a share.
- **Why the battery stays line-identical under (a).** *(Corrected by re-review C1 and C3, 2026-10-20; the first version cited the wrong suite and carried a fallback that would have gutted the feature.)*
  - Judging by the explicit `Debug_AddColonists` calls (births and ships during a run are not counted here), every battery suite except four peaks at 50 colonists or fewer. Two of those four are themselves capped at 50: -districts (`:8678`, 8 + 42) and -citygrowth (`:9657`, `:9695`, 8 + 42). So the ramp is exactly 0 there, which is why the start is 60, not 50.
  - The other two exceed 60, and **-percapita peaks at 5,000, not 250**: its ladder is `{10, 100, 1000, 5000}` (`:5899`, filled at `:5911`). The N = 250 call (`:6603`), the 1,500 (`:7028`), the Colonist Transports (`:6634-6660`) and the Big Arrival (`:6799-6850`) belong to -arrivals. The ramp is open in both, so identity needs zero long commuters.
  - They hold it for one reason: **floor-only homes and seats**. Both build floor homes with `Debug_SetColonistsPerCell` and floor Lab seats; the only surface buildings are SolarArrays (`:5881-5882`, `:6543-6544`). Every employed colonist's trip is `|Lh - Lw| x CommuteShaftCmPerFloor`, at most 4 floors x 6,000 cm = 240 m, under `CommuteLongCm` 300 m, so Long = 0, the penalty is `Ramp x 4 x 0 = +0.0`, and `x - 0.0 == x`. Neither work hook takes its branch. The percapita pin `Near(H.Total, 50 + Milestones + Housing + Rooms + Jobs, 1e-9)` (`:5922-5923`) is untouched.
  - The retune to 2,000 / 40,000 is NOT needed for identity (even the original 6,000 / 30,000 clears it), so it is **not made**: the shipped rows are `CommuteShaftCmPerFloor` 6000 and `CommuteLongCm` 30000, the re-review's optional revert (logged in design-decisions under C6a).
  - This is argued, not run. The proof is two things, and the order matters: **(1)** the line diff of all 42 runs (the baseline plus 41 suites) against the C5e logs (`battery_post_c5e`), with today's masks, is the commit gate; **(2)** `-RHCommuteAudit` explains a diff and bounds the margin. It records the MAXIMUM over every step of the run (not one snapshot) and prints one line at the end: steps measured, peak population, peak ramp, the longest employed-colonist trip, the most long commuters at once, and the margin `CommuteLongCm - longest trip`. It measures the flows even where the ramp is closed (that is the point: what the distances WOULD be); it feeds nothing back; default off, so it adds no lines. The diff tool is `scripts/sim/diff_battery.py` (tracked since the C4 gate; the re-review's "not tracked in git" was wrong).
  - **There is no fallback.** The earlier "raise `CommuteRampStartPop` above that suite's peak" is deleted (re-review C3): -percapita's peak is 5,000, so raising the start above it would switch commuting off for every realistic city. If the audit finds Long > 0 anywhere, either the layout reason above is wrong (fix this text) or the line move is intended; in the second case the commit lists it for director sign-off, and nothing is tuned away.
- **New and changed rows for (a):** `CommuteRampStartPop` 60, `CommuteRampFullPop` 160, `CommuteShaftCmPerFloor` 2000 (was 6000), `CommuteLongCm` 40000 (was 30000).
- **Trade-off.** The doc's original rule ("inert whenever no districts are painted or no road exists") is replaced by the ramp. Small colonies never feel commuting, roads or not. That is a design change the director must accept.

**Variant (b): the original district reference points** (section 2 as written, plus R1).
- Gate: `Districts.Num() == 0 || BuiltEdgeCount == 0`. Portals are as in section 2. Same district = 0. Unzoned = exempt.
- It is cheaper (D² plans) and the battery is inert by the gate, with d5 inert for the reason above.
- It keeps three loopholes, which Q2 names:
  - leave the work floor unzoned;
  - paint one district over everything;
  - the first road switches the penalty on.
- `CommuteShaftCmPerFloor` 6000 and `CommuteLongCm` 30000 stay as in section 7.

**Shared by both variants:** the flow matrix (with an Unzoned row and column in (a)), R1's key (in (a) plus the anchor set, which the same versions cover), R2's hooks, the overlay and the shuttles. In (a), shuttles are drawn per district pair along the route of that pair's most-used anchor pair, ties by the lower anchor index.

## Q2, rewritten for the director
**Q2. How is a long commute measured?**
- **a (recommended): from each person's actual home to their actual workplace,** over roads and lifts. It counts whether or not their home or work is in a district, and inside one district too. Commuting only starts to matter as the colony grows past about 60 people, and fully at 160. A road can only ever help.
- **b: between district reference points,** as first drafted. It is cheaper, but it has three loopholes:
  - a work floor left out of every district makes its workers count as not commuting at all;
  - one district painted over the whole colony makes every commute zero;
  - the first road built anywhere switches the penalty on, so building a road can lower Hope that day.

## Risks, updated
- **Risk 1:** reworded per the wording fix above. Under (a) it becomes "-percapita and -arrivals have the ramp open, and only the audit proves they stay inert".
- **Risk 2:** closed by (a); stands under (b).
- **Risk 3:** closed by (a); stands under (b).
- **Risk 6:** retired by R7's proof.
- **Risk 7:** deleted with R4.
- **New risk A.** In (a), a surface colony of hundreds spread over the 4.6 km relief has many distinct anchors. The cost is A Dijkstras on a cold rebuild. The -scale budget adds a 500-habitat case (≤ 10 ms cold).
- **New risk B.** R3 makes rovers and haulers differ on a forced charge. The deviation is logged. Unifying them would move the battery.
- **New risk C.** R6 caps a claim at 4 plans. A rover surrounded by unreachable nearer freight can idle while reachable work lies 5th or further. T8 pins the cap. The cap is a config row (`RoverClaimPlans`, 4).

---

# PROFESSOR RE-REVIEW (2026-10-20)

**Verdict: CLEARED WITH CONDITIONS.** R1-R7 and the optional items answer the first review correctly and match the code. The population ramp is structurally sound. It is era-safe and, as far as the argument goes, battery-inert. But the argument rests on one wrong fact, the proof plan is incomplete, and its fallback would gut the feature. Conditions C1-C5 below are spec edits. Write them into this doc and build without another review round. This assumes Q1a, Q2a and Q3a. Section "Under the other options" says what changes if the director picks differently.

## The population ramp, attacked

**Battery identity: holds by argument, but the argument cites the wrong suite.**
- **-percapita peaks at 5,000, not 250.**
  - Its ladder is `{10, 100, 1000, 5000}` (`RHSimCommandlet.cpp:5899`), filled at `:5911`.
  - The N=250 call (`:6603`) is in -arrivals, which also reaches 1,500 (`:7028`), takes ColonistTransports (`:6634-6660`) and runs the Big Arrival (`:6799-6850`).
- **The conclusion survives.** Both suites are floor-only:
  - homes are floor cells with `Debug_SetColonistsPerCell`;
  - seats are floor Lab cells (`:5908-5909`, and `:5948` on floor -5);
  - the only surface buildings are SolarArrays (`:5881-5882`, `:6543-6544`).
  - Under (a), every employed colonist's distance is `|Lh - Lw| x CommuteShaftCmPerFloor` ≤ 4 x 20 m = 80 m. So Long = 0, the penalty is `Ramp x 4 x 0 = +0.0`, and `x - 0.0 == x`. Neither hook takes its branch.
  - The percapita pin `Near(H.Total, 50 + Milestones + Housing + Rooms + Jobs, 1e-9)` (`:5922-5923`) is untouched.
- **The retune is unnecessary.** Even the original 6000/30000 gives 240 m < 300 m on floors, so the move to 2000/40000 is not needed for identity. Keep whichever the director prefers, but do not justify it by the battery.
- **Suites capped at 50.** -districts (`:8678`) and -citygrowth (`:9657`, `:9695`) are 8 + 42 = 50, below 60, so the ramp is exactly 0. Births and ships elsewhere are not counted by the explicit-call survey. Only the audit can close that, which is condition C2.
- **-scale (outside the battery)** has the ramp fully open at 5,000. Its Hope and digest lines may move, and the commit must say so.

**Monotone and era-safe: yes, and it does not need to be monotone.**
- The ramp is a stateless, continuous function of `Colonists.Num()`, not a one-shot threshold. That is the same parity class as the Housing and Jobs terms (`RHSimWorldSubsystem.cpp:5363-5373`), which already divide by Pop.
- Pop changes only at events both bands share:
  - births and evacuations, on accumulators;
  - the Big Arrival, through `StepBigArrival` in both spines (`:501`, `:668`);
  - ship landings. These happen in the agent band only, because `ShipInbound` refuses era (`:564-567`).
- Between events the ramp is constant, and the exp smoother (`:5446-5457`) integrates it identically at 0.1 s and 60 s.
- Evacuations make Pop non-monotone. That is harmless here: no latch reads the ramp, and the long-share alert latch is runtime (O1).
- Pop is covered by the flows key through ColonistSetVersion.

**-RHCommuteAudit: necessary, not sufficient.** See C2. On its own it proves only "Long = 0 where it happened to sample". The proof of line-identity is the battery diff against the 891a700 logs. Note that `diff_battery.py`, cited by the C4 REVISION item 11, is not tracked in git, so that proof is not reproducible today.

## Other REVISION points checked
- **R3:** the demolish case is already safe today, by a different path. `Demolish` redirects claimed hauls bound for the building to the Lander (`:8448-8454`), and `HaulUnload` then credits the Lander's `InputKg`. The no-task branch of `HaulUnload` (`:10206-10217`) is near-unreachable. Keep the guard, but T8's "destination demolished" case will pass through the existing redirect, not the new helper. Name it that way, and assert `InputKg` (the existing path), not `OutputKg`.
- **O3:** `StepTaskBoard()` takes no dt (header `:1624`). Add the parameter. It runs only in the agent band (`EraStep`, `:651-675`, never calls it), so "era never advances it" is true by construction.
- **R7:** `GetRoadEdgeSpeedMul` has no RedHope caller, so changing its signature is free. The `&Profile == &RobotProfile` literal-path guard is the right call.
- **R5, O4, R6, O2, O9:** consistent with the code and with each other.

## REQUIRED conditions (spec edits; no further review round)
- **C1. Correct the facts in R8's battery argument.**
  - -percapita peaks at 5,000 (`:5899`, `:5911`); 250, 1,500, the transports and the Big Arrival belong to -arrivals.
  - The reason both stay inert is "floor-only homes and seats: ≤ 4 floors x CommuteShaftCmPerFloor < CommuteLongCm".
- **C2. Make the audit a real proof.**
  - (a) `-RHCommuteAudit` records the MAXIMUM over every step of the run: peak Pop, peak Ramp, max employed-colonist distance, max Long, and the margin `CommuteLongCm - maxDist`. Print it in one line at the end. A single snapshot is not enough.
  - (b) The commit gate is the line diff of all 42 runs (baseline plus 41 suites) against the 891a700 logs, with today's masks. The audit only explains a diff.
  - (c) Commit the diff tool under `scripts/sim/` so the proof is reproducible.
- **C3. Delete the fallback "raise CommuteRampStartPop above that suite's peak".**
  - -percapita's peak is 5,000. Raising the start above it would switch commuting off for every realistic city.
  - If the audit finds Long > 0 anywhere, either the layout reason is wrong (fix the spec), or the line move is intended. In the second case, list it in the commit as C2 did, for director sign-off.
- **C4. Keep the anchor-distance memo independent of the job assignment.**
  - R1's flows key contains JobsVersion, and (a) bundles the per-anchor Dijkstras into that cache. So every reassignment re-runs A Dijkstras. Reassignment happens when a station goes on or off line (`GetProvidedRoom` requires `bPowered`, `:4891`, so night shedding toggles it), when a cell is carved ahead of others, or when anyone arrives.
  - Key a separate memo on (BuiltVersion, ShaftHead, CommuteRoadSpeedMul), with entries keyed by anchor POSITION in whole cm (not by first-met index, which renumbers on any roster change).
  - The flows pass is then O(N) lookups on a JobsVersion bump. A cold memo costs A Dijkstras only when roads change or a new anchor position appears.
  - Pin it in -scale: a JobsVersion-only bump at 5,000 colonists is ≤ 1 ms.
- **C5. If the director answers Q3a, add a short spec for the player verb before building it.**
  - The verb is `DemolishRoad(EdgeId)`, through the uplink with an explicit reject reason.
  - Behaviour is `Debug_RemoveRoadEdge`'s (`RHSimRoads.cpp:453-479`): Build tasks removed; refund to the Lander at `SalvageFraction` for a built edge, or the whole bill for an unbuilt one.
  - `RemoveEdge` bumps BuiltVersion (`RHRoads.cpp:380-384`), which already invalidates R1's key and re-derives `bFreight` (O2).
  - Add the -roads and -traffic cases: demolish a built edge, demolish an unbuilt edge, and demolish under a moving rover.

## Optional
- Revert `CommuteShaftCmPerFloor`/`CommuteLongCm` to 6000/30000 unless the director wants the lift cheaper. The battery does not need the change (see above).
- In T7, add a case that crosses the ramp mid-run with a Big Arrival, so the agent and era bands are compared while Ramp moves from 0 into (0, 1).

## Under the other options
- **Q1b (the Fabricator builds rovers):** adds a robot-fabrication recipe and a spawn path from a building. A batch in progress uses the existing `PendingOutputs` save, so check that before claiming no new fields. It needs its own short review. Commute is unaffected.
- **Q2b (district reference points):** variant (b). The battery is inert by the districts-and-roads gate, so the ramp, the audit, C1-C4 and the 2000/40000 retune all fall away. R1's key still applies. The three loopholes stand, documented in Q2.
- **Q3b (no demolish):** C5 falls away. R3, R6 and R7 are still required, because `Debug_RemoveRoadEdge` exists and T8 uses it.

VERDICT: CLEARED WITH CONDITIONS. Build after C1-C5 are written into the doc (C5 only if Q3a), with the 42-run line diff against 891a700 as the commit gate.

---

# C6a, AS BUILT (2026-10-20): the sim half

Build notes for the re-review's conditions. Variant (a), Q1a, Q2a and Q3a throughout. C6b (shuttles, the TRAFFIC overlay, the demolish UI) comes later; its const accessors are in place.

## C1. The suite facts
Corrected in place in R8 above: -percapita peaks at 5,000; 250, 1,500, the transports and the Big Arrival are -arrivals'; both stay inert because homes and seats are floor-only (at most 4 floors x 6,000 cm = 240 m < 300 m).

## C2. The audit and the gate
`-RHCommuteAudit` (a command-line switch read in `Initialize`, like `-RHVerifyCaches`): every `StepSim` and `EraStep` ends with `AuditCommuteStep`, which keeps the peak population, the peak ramp, the longest employed-colonist trip, the most long commuters and the step count; `Deinitialize` prints them as one `COMMUTE AUDIT:` line with the margin below the long line. The commit gate is `bash scripts/sim/rh_battery.sh 10` ending `BATTERY COMPLETE: 0 suite(s) not green`, then `diff_battery.py` of its logs against the C5e logs with 0 differing lines in every pre-existing suite; the new `traffic.log` is the only new one. `rh_battery.sh` now lists `traffic` (41 suites, 42 runs).

## C3. No fallback
Deleted, see R8.

## C4. The position memo
`FRHCommuteMemo` is keyed on (BuiltVersion, ShaftHead, the commuters' road speed) and NEVER on the job assignment. Entries are keyed by anchor POSITION in whole cm: one Dijkstra per source position (`FRHRoadGraph::DistancesFrom`, the `PlanRoute` loop without the early exit and with the same tie rules), the K nearest built nodes per position, and the finished surface distance per pair of positions (canonical on (low, high) by (X, Y), planned from the low end: O5). The flows (`FRHCommuteFlows`) are keyed on a superset of every input (R1): districts, colonist set, zoning, furniture, JobsVersion, BuiltVersion, shaft head, building count, next building Id, and the commute config. So a reshuffle of seats re-runs the O(N) flows pass and no Dijkstra, and a road that finishes or goes re-derives both. `-traffic` pins that; `-scale` adds the 5,000-colonist budget check (20 districts, 250 edges, a JobsVersion-only bump <= 1 ms, a cold memo + flows <= 5 ms; 500 distinct anchors <= 10 ms; 100 rover plans <= 10 ms).

## C5. Road demolish: the player's verb (Q3a)
The verb is `DemolishRoad`, through the uplink, `Value` = the edge Id. It is a real player order: the same lag as every order, a preflight that says why, and a worker re-run when the order lands.

- **Accepted** when the edge exists (a standing edge or one still being built). **Refused** with `That road is already gone` when it does not (another demolish order or the debug hook took it while this one was in transit; an Id that never existed).
- **Effect** is exactly `Debug_RemoveRoadEdge`'s (`RemoveRoadEdgeWithRefund`, shared): the edge's Build tasks are removed; `RoadGraph.RemoveEdge` runs; the refund goes to the Lander's `OutputKg`: `SalvageFraction` (50%) of `length m x RoadStructKgPerMeter` for a standing edge, the whole bill for an unbuilt one (the order paid for it).
- **State and keys.** Removing a standing edge bumps `BuiltVersion` (`RHRoads.cpp`), which already invalidates the commute flows, the position memo, the adjacency, and re-derives every `bFreight` (O2). An unbuilt edge changes none of them. No new saved field: the graph is already saved (v36), so demolish state round-trips with it.
- **Under a moving robot or rover.** No re-plan (C4 item 5): the leg runs at the edge's live speed, which for a missing edge is 1.0x for a robot and 0.3x for a rover; both still arrive. An in-flight rover whose destination building is demolished goes through the existing Lander redirect (below), not through this verb.
- **Tests.** `-traffic` X10: a standing edge (the 50% refund, BuiltVersion bumped), an unbuilt edge (the whole bill, the Build task gone, BuiltVersion untouched), a gone edge refused with a reason; X8: demolish under a moving rover; X13: the keys. `-roads` is unchanged: its log is under the commit gate's line diff, so a new case there would be a moved line. Its `Debug_RemoveRoadEdge` cases (R16) now run the shared worker and pass line for line; the order's own cases live in `-traffic` (logged as a deviation).
- **Not here (C6b):** the demolish UI and the confirmation, the overlay's hover on an edge. `RH.RoadDemolish <edgeId>` transmits the order.

## The "destination demolished mid-trip" test (re-review correction)
It passes through the EXISTING Lander redirect in `RemoveBuilding` (`RHSimWorldSubsystem.cpp`, "cargo already bound for it comes home to the Lander instead": a claimed haul's `To` becomes the Lander), and `HaulUnload` then credits the Lander's `InputKg`. `-traffic` X9 is named and asserted that way: the task's To is the Lander, the Lander's `InputKg` rose by the load, its `OutputKg` did not. The new `ReturnHaulCargo` is covered by the other two X9 cases: a forced charge mid-delivery (mass conserved to 1e-6, the load back in the source, the task open) and the near-unreachable no-task guard (the load goes to the Lander's `OutputKg`).

## The numbers as built
`RoverOffRoadSpeedMul` 0.3, `RoverLastLegCm` 3000, `FreightMinCm` 8000, `FreightHaulerFallbackSec` 300, `RoverReserveDetourMul` 3.0, `RoverClaimPlans` 4, `CommuteRoadSpeedMul` 4.0, `CommuteShaftCmPerFloor` 6000, `CommuteLongCm` 30000, `CommuteRampStartPop` 60, `CommuteRampFullPop` 160, `HopeCommuteMax` 4, `HopeCommuteSteepness` 2, `CommuteWorkMul` 0.85, `CommuteAlertShare` 0.10 (RH_Config); `RC_R` (RH_Robots); `CargoRover` (RH_ManifestItems). Save v40, strict, no new field.
