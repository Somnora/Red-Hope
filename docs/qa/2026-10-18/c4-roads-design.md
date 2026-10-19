# C4, roads: design (draft for adversarial review)

Repo: /Volumes/Unreal/red_hope/red_hope.
- Plan: docs/qa/2026-10-18/living-city-plan.md (the C4 row; the appendix's "Logistics and movement" section).
- Director rulings: Q3a, the player draws roads; robots and vehicles are faster on them; heavy cargo rovers need them (rovers are C6, not C4).
- Prior gates: C1 (O(N) sim), C2a/C2b (save v35), C3 (presentation crowds).

## Facts the design rests on (from the code survey)
- **Movement:** the only movement is RH::MoveToward (Source/RedHopeSim/Public/RHRobotMovement.h), a straight 2D line that arrives within 3 m. There is no pathfinding anywhere.
- **Targets:** FRHTaskFragment.TargetCm is set at claim, load and approach (RHRobotStateTree.cpp).
- **Routing choices:** StepTaskBoard's Consider closures, the TryClaim* functions, FindNearestChargePad and DriveWh in FRHNeedsChargeCondition all use straight-line DistXY.
- **Underground:** robots never leave level 0. The shaft head (GetApproachPoint) is the cross-level hand-off.
- **Era logistics** (EraLogistics) teleports material: no travel time or distance. The parity bar is the RH.Ledger paired runs within 5% over 10 sols.
- **Stepping:** frame-batched. Each robot runs all its sub-steps for the frame in turn; N sub-steps come from frame time x speed, capped at 32.
- **Saves:** the format is strict (v35). Robots save position, charge and wear; tasks save with claims cleared.
- **Placement:** CanPlaceBuildingSized does a box overlap against all buildings, on a 2 m grid (GridSnapCm). There is no rotation.
- **Presentation:** RHAgentVisualizerSubsystem draws robots on detours around footprints (PlanRoute), at the sim's progress fraction.

## Design

### 1. Data (RedHopeSim)
- **The graph:** an undirected road graph on level 0.
  - `FRHRoadNode { int32 Id; FIntPoint GridPos; }`: positions snapped to a ROAD GRID of RoadGridCm = 200, integer grid coordinates.
  - `FRHRoadEdge { int32 Id; int32 A, B; int32 LengthCm; bool bBuilt; float BuildProgress; int32 RegionId = 0; }`.
- **Edges:** an edge is straight; any angle is allowed. Long drags are split into edges of at most RoadMaxEdgeCm = 4000. Nodes within one grid cell merge.
- **The version counter:** RoadGraphVersion is bumped on any change.
- **Saved (v36):** nodes, edges, progress and next ids.

### 2. Building a road
- **The order:** a new uplink verb, BuildRoad, from point P to point Q, snapped to the grid. It goes through ExecuteCommand like Build orders, with the same order lag.
- **Preflight (CanPlaceRoad):**
  - both ends inside coverage (Power-as-Territory: the same rule as buildings);
  - the strip of width RoadWidthCm = 600 must not overlap any building footprint or construction site;
  - the edge must not exceed the length cap after splitting;
  - the bill must be affordable: RoadStructKgPerMeter = 2 kg/m of Struct.
- **The bill:** it is paid like a building bill. Each new edge becomes a construction site that haulers supply with Struct from stores. This reuses the construction-bill haul. A Fabricator then builds it (an existing Build task targeting the edge's midpoint), at RoadBuildMetersPerHour.
  - Reviewer: is reusing the building construction pipeline with a "site" that is an edge sound, or should roads use a separate, simpler path? Name the risks.
- **Placement, the other way:** buildings cannot be placed over a built or planned road strip. This is an extra check in CanPlaceBuildingSized.
- **Demolish:** a SellRoad / Demolish edge order, 50% salvage, mirroring building demolish.

### 3. Moving on roads (the risky part)
- **Speeds:** RoadSpeedMul = 1.6 on built edges; off road it is unchanged.
- **Route = straight to an entry node, along the graph, straight from an exit node to the target.**
  - Candidate entry and exit nodes: the K = 4 nearest built-graph nodes to the start and to the target, with deterministic ties by node Id.
  - Shortest path: Dijkstra on integer times (cm / speed in integer units). Ties are broken by node Id, then edge Id, and no container iteration order decides anything.
  - The road route is used only if its total time is less than the straight-line time.
- **The route cache:** keyed by (entry, exit) and invalidated by RoadGraphVersion. Paths are only recomputed when a robot gets a new target.
- **Fragment:** `FRHPathFragment { TArray<FVector> Waypoints; int32 Next; int32 GraphVersion; }`, or a compact handle into the cache. RH::MoveToward is applied leg by leg.
  - With NO built roads, the path is a single waypoint (the target) and movement is EXACTLY today's call sequence. That is required: all battery suites must stay line-identical.
- **Graph changes mid-trip:** if a robot's path crosses an edge that is demolished, it re-plans from its current position at the next sub-step. The GraphVersion check does this.
- **Charge reserve:** DriveWh in FRHNeedsChargeCondition uses the PLANNED route's travel time, not the straight-line distance. Otherwise robots on long road detours could run flat.
- **Choosing work (nearest consumer, the TryClaim*):** stays straight-line in C4, for stability and to bound the change. Network distance in claims is a candidate for C6, when traffic exists.
  - Reviewer: is that split (travel follows roads, choice is straight-line) safe, or does it create pathological choices?

### 4. Era band
- EraLogistics stays the teleport model. It is already idealized: no travel time.
- Roads make the agent band faster, so closer to era.
- Add a road case to the ledger parity check: the same colony with a road network, an agent-band versus era 10-sol ledger within the existing 5% bar.

### 5. Presentation (RedHope)
- **The road card:** a "Road" card in BUILD > Logistics. Drag from P to Q with a ghost strip, snapping, and a live length, bill and preflight readout. Click to order.
- **Road meshes:** procedural strips from the sim's edges (PMC), in a compacted-regolith material (darker, with edge marker posts every 10 m). A planned edge is a dashed ghost; one under construction is a partial fill.
- **Robots on roads:** on road legs, robots are drawn exactly on the sim's waypoints; PlanRoute's detours stay for off-road legs only.
- **Era mode:** RH.Ledger / era mode need no visual change.

### 6. Tests
- **A new fails-loudly -roads suite:**
  - graph build (splitting, merging, snapping);
  - placement conflicts both ways;
  - affordability;
  - construction by the pipeline;
  - pathfinding determinism: the same route twice; the same route after save/load; ties;
  - a robot on a road taking the route and arriving sooner than straight-line;
  - the charge reserve using route length;
  - a mid-trip demolish causing a re-plan;
  - a save/load round trip of the graph and in-flight paths;
  - an era/agent ledger within 5% with roads.
- **-scale:** unchanged budgets. Add a 200-edge network with 100 robots planning paths, under a budget.
- **Battery:** line-identical (no suite builds roads).
- **Captures:** a drawn road network with robots driving it.

### 7. Save: v36.

---

# REVISION after the adversarial review (BLOCKED -> these changes are REQUIRED; they override the sections above)

1. **Construction, not the building pipeline.** The road bill (RoadStructKgPerMeter x length, Struct) is taken from stores ATOMICALLY in ExecuteCommand, with affordability re-checked there. There is NO haul or site-hopper leg.
   - "Built by robots" stays as a LABOUR-ONLY Build task with an explicit site kind:
     - add a RoadEdgeId to FRHSiteRef (RHSimTypes.h) and a separate slot or flag in FRHTaskFragment; never reuse DigDepositId;
     - add the road branch to GetSiteLocation, GetSiteLevel, GetApproachPoint, HasOpenTask, TryClaimBuild, ApplyBuildWork (including demolish-while-building and the refund) and task save.
   - Make the zero-vector fall-through in GetSiteLocation an ensure.
2. **Era.** Add "a road edge is under construction" to CanEnterEraMode's refusals. In the parity test, finish building the roads in the agent band before the paired split.
3. **One shared movement helper,** next to RH::MoveToward, used by BOTH brains: every MoveToward call in RHRobotStateTree.cpp and in the legacy RHRobotTaskProcessor.cpp.
   - First line: if there are no built edges, call MoveToward exactly as today. This keeps the battery line-identical.
   - Plan lazily on first use. Store the TargetCm the path was planned for, and re-plan only when it changes exactly.
   - Repair (its target moves every step) stays straight-line.
   - When a waypoint is reached, continue to the next one within the same sub-step.
4. **Path storage** is in the subsystem: a map from entity handle to path, cleared in LoadColony. It is NOT a new Mass fragment, so the explicit StateTree archetype and external-data linking are untouched. Give RedHope a const accessor.
5. **No mid-trip re-planning.** Each leg records its edge id; speed is looked up live (built = 1.6x, demolished or missing = 1.0x). Edge ids are never reused. Test: a demolish slows the trip and the robot still arrives.
6. **Plan in centimetres:** integer cm on the graph, and decide road vs straight with the speed-free integer comparison 16 x off-road cm + 10 x road cm < 16 x straight cm. The cache is speed-independent.
   - Preferred: an all-pairs road-distance table recomputed only when the BUILT graph's version changes. Each plan is then the K-nearest scan plus 16 lookups. Alternatively, run one Dijkstra from a virtual source connected to the K entry nodes.
   - Deterministic ties throughout.
7. **Charge reserve:** CUT the DriveWh change. Add a test asserting that the straight-line reserve is at least the time-based drain along the road route.
8. **Save v36:** the graph, plus a second end point on FRHCommand, written in BOTH the uplink-queue save and load (or packed into Value, as DecodeBuildSize already does).
   - In-flight paths are not saved; remove them from the tests.
   - Test instead: the graph round-trips, and a robot re-plans to the same route after load.
9. **Visualizer:** on road trips, drawn progress is the ARC LENGTH along the sim's path (replacing the straight-distance fraction in RHAgentVisualizerSubsystem.cpp's AdvanceDrawn). Off-road legs keep today's behaviour.
10. **Junctions:**
    - When a new end point lies on an existing edge (within half the road width), split that edge there.
    - Crossings split both edges at the crossing point.
    - Nodes merge only on exactly equal integer grid positions.
    - Every split node must be inside coverage.
11. **Battery and plan:** there is NO starter road in the default colony. The "(N bytes" save line is masked by diff_battery.py. The deviation "era keeps the teleport model; it does not learn road distance" is logged in docs/design-decisions.md for the director.
12. **Deferred to a follow-up, not in C4:** road demolish and salvage, edge marker posts, and partial-fill art for roads under construction. A planned or under-construction edge can be drawn as a simple translucent ghost.
