# W1, distant ice: design (2026-10-21, draft for adversarial review)

Design only. No code is written. Anchors are file:line on **main @ 992383d** (D1 is being built in this tree, so header lines in the working copy sit about 70 lines lower).
Ruling: the director's "water a" (2026-10-21). Evidence: bot commit 58c8cae and "Playtest bot plays the living city" in `docs/design-decisions.md:619`. House rules: C4 REVISION and C6 REVISION and re-review.

## Facts the design rests on (read in the code)
- **Deposits** are DT rows loaded in row-map order. Ids are `Deposits.Num()+1` (`RHSimWorldSubsystem.cpp:392-406`). A hidden row (`SurfaceVisible=FALSE`) exists in the sim and is inert until discovered.
- **Surveys are geometric.** The `Survey` verb posts a point anywhere, with no range check (`:6985-6994`). The scout drives there and calls `CompleteSurvey(TaskId, WorkRate)` (`RHRobotStateTree.cpp:145-153`; RC_S WorkRate = 60 m radius). Every hidden deposit inside the disc is revealed (`:10672-10700`). Deposits have **no random roll**.
- **The only roll is the rival one.** `MaybeDiscoverSettlement(SurveyHistory.Num())` is seeded by the survey COUNT alone (`:10708`, seed `:10735`). It does not depend on where the survey was or what it found.
- **No battery suite completes a survey.** The commandlet never orders `Survey`. -icelow reveals with `Debug_RevealDeposit` (`RHSimCommandlet.cpp:3797, 3838`). -espionage drives the rival roll directly (`:1449-1457`).
- **Placement.** An Ice Drill must sit inside coverage (`:6746-6767`) and attaches to a DISCOVERED deposit within 12 m (`:8279-8292`). A Pylon needs a coverage node within 80 m (`:6714-6729`, Pylon disc 40 m). Power is one colony pool, so coverage is territory, not wiring. Every road node must be in coverage (`RHSimRoads.cpp:213-221`), and edges are at most 40 m. So **any road out to far ice already needs a continuous pylon chain**.
- **Water is network-instant.** `MeltIce` output joins the pool wherever the plant stands (`:1309-1330`; `RH_Resources` Water = Fluid/Tank). A Water Plant beside a far drill therefore makes freight pointless. That is why the bot roads nothing to Ice_B (design-decisions:623).
- **Ice routing.** Drill ice goes to the NEAREST Water Plant whose hopper is under 2 x Need. When every plant is full it waits at the drill, never in a store (`:1046-1085`).
- **Finding: ice to a Water Plant can NEVER be freight today.** MeltIce Need is 50 kg, so a plant's room is at most 100 kg (`:1064`). C6 R5 caps a consumer leg at that room (`RoverLoadKg`, `:924-932`). O4 calls a leg freight only above 200 kg (`RHSimRovers.cpp:105`). That is why the 170 m Ice_A lane moved 0 t of freight, not only the timing the bot report gives.
- **Haulers' only limit is throughput.** RC_H burns 80 W x 240 s ≈ 5 Wh of its 1,500 Wh on a 480 m run. Range never stops it, so a rule has to.
- **Era.** `EraLogistics` teleports outputs to the FIRST demander with room, else to a store (`:824-877`). It knows no roads or rovers.
- **Map.** The sim plane is z = 0. The presentation relief is flat inside 150 m and opens by 350 m (`RHMarsTerrain.cpp:303-345`). Hazards: craters 190-420 m out (`:81`), the hero massif at bearing 38°, 520 m (`:403-416`, bench at 400 m, `:97`), mesas at 0.9-1.7 km (`:394-401`). The ground pad is a 5 km plane (`RHColonyVisualizerSubsystem.cpp:1764-1767`). There is no camera pan limit.

## 1. Where the new ice is
- **Three authored rows, appended after Ice_B** (so Ids 1-6 do not move; the new rows are 7-9):

| row | at (m) | from the Lander | bearing | mass | SurveyOrder | why there |
|---|---|---|---|---|---|---|
| Ice_C | (478, 40) | 480 m | E | 120 t | 1 | past the crater ring; 288 m clear of the massif heart; its road extends the bot's Ice_A lane (358 m from the Ice_A drill) |
| Ice_D | (-560, 330) | 650 m | WNW | 160 t | 2 | open plain; 390 m from Regolith_B |
| Ice_E | (260, -760) | 803 m | SSE | 220 t | 3 | inside the mesa band's inner edge (0.9 km); 470 m from Ore_B |

- **Distances.** The nearest is 464 m from the bot's Water Plant at (14, 6), which is 5.8x FreightMinCm (80 m). One Ice Drill makes 960 kg a sol (40 kg/h x 24 h). A hauler would manage about 200 kg per 260 s round trip at 480 m and take the colony's only two haulers off everything else. A rover on the road manages 1 t per about 160 s (8 m/s plus 2 x 10 s). That is roughly 8x the hauler.
- **Sizes.** 500 t in all against 90 t today. One far drill covers about 125 sols. Ice_D and Ice_E are later expansion.
- **Authored, not generated.** The sim may not read `RHMarsTerrain` (it is in the RedHope module, the core boundary rule), so a generator could not avoid craters or the massif. Authored rows are integer metres, determinism is trivial, -ice pins them, and the live smoke checks each dig site's ground.
- **New `FRHDepositRow` columns** (a struct change, so the director compiles before the DT re-import):
  - `Far` (bool): rover-only, melted only at home. FALSE on the six existing rows.
  - `SurveyOrder` (int): the guide's reveal order, 0 = none.
  - `RegionId` (int, 0 = home): the step-4 hook, the same convention as `FRHDistrict::RegionId` (`RHDistricts.h:51`) and D1's floors.
- `FRHDepositState` gains `bFar`, `SurveyOrder` and `RegionId`, derived from the row at begin-play and at load, exactly as `InitialKg` is (`:11535-11548`). None of them is saved.

## 2. How they are found
- **Keep the geometry and add a hint.** There is no guaranteed reveal by survey count and no new roll. A new pure read, `GetNextFarIceHint(FVector& CentreCm)`, returns the lowest-`SurveyOrder` undiscovered Far row, but only once every non-Far ice row is discovered. Its centre is the row rounded to a 100 m grid (`FarIceHintGridCm`), so the hint is an area and not the spot.
- `CompleteSurvey` reveals a Far row within `max(RadiusM, FarIceRevealRadiusCm/100)` (100 m). The rounding offset is at most 70.7 m, so a survey on the hint centre always finds it (Ice_C 46 m, Ice_D 50 m, Ice_E 57 m). Non-Far rows keep the scout's 60 m, and that branch is not taken for them.
- **The order is the guide's, not the sim's.** A player who surveys Ice_E's ground first finds Ice_E. The hint then stays on Ice_C.
- **Rivals.** Untouched. Each far survey is one more survey count, so it is one more 50% roll. The roll per count is unchanged. In play, rivals may turn up earlier for a colony that surveys more. The battery runs no surveys, so it cannot move.
- **Presentation.** The survey map draws the hint as a dashed ring. The guide gets a stage-0 line, a Gate-D placeholder: "Ice signatures about 480 m east. Survey there."

## 3. How they are worked
- **Split: raw ice travels home.** A Far drill's output goes only to a Water Plant at least `FarPlantExclusionCm` (300 m) from that drill (the board's consumer loop, `:1046-1071`, gains the test).
  - Placement also refuses a Water Plant within 300 m of a DISCOVERED Far deposit, with the reason "Ice this far out is hauled home raw - build the Water Plant at the colony".
  - Undiscovered rows are not checked, so the preflight leaks nothing.
- **Power: a pylon chain, no new rule.** The road needs coverage at every node anyway (`RHSimRoads.cpp:213-221`), so an outpost pylon or a self-powered drill would save nothing. Ice_C needs about 5 pylons from the Ice_A disc (200 kg Struct) and 716 kg of road. With the drill (80 kg) that is about 1.0 t of Struct.
- **The far hopper.** For a Far leg only, a plant qualifies while `Have + 200 < FarHopperKg` (1,200 kg), and its room is `FarHopperKg - Have`. That is about a sol of melting, enough to span a round trip. Freight then exceeds O4's 200 kg. Every other leg keeps 2 x Need exactly.
- **Rover-only, posted only as freight.**
  - On the output leg (`:1098`), a Far source's task is posted ONLY when `SetHaulAmount` makes it freight: an active rover and `IsFreightRoute` true (`RHSimRovers.cpp:83-97`). Otherwise nothing is posted and the ice waits at the drill (no output cap: mass is kept).
  - `TryClaimHaul` (`:10333-10334`) skips any task whose `From` is a Far drill, **with no 300 s fallback**.
  - `RefreshFreightFlags` (`RHSimRovers.cpp:119-142`) deletes an unclaimed Far task that stops being freight (rovers gone, or a road demolished) instead of handing it to haulers.
  - This also closes a latent C6 trap: a task posted at 200 kg before any rover can never become freight, because the test is `AmountKg > 200` (`:135`).
- **Does road-served work at 480 m?** Yes. `PlanRoute` has no length limit, the pickup is the drill centre, the drop is the plant centre, and the last legs are 30 m (`IsRoadServed`, `:34-38`). One road order is at most 64 pieces (`RHRoads.h:111`, about 2.3 km). The rover's charge reserve is 3 x straight x 200 W, about 7 Wh of 6,000. The R7 proof holds unchanged.
- **Guide.** `GetIceExpansionStage` gains stage 4, "road and rover": a drilled Far deposit with no freight path. Stage 3, "melt where drilled", is never offered for a Far row.
- **Alert advice** (`:1540-1547`) adds "far ice needs a road and a cargo rover" while Far rows remain undiscovered. This is a Gate-D placeholder.

## 4. The playtest bot (RHPlaytest.cpp, RH_PLAYTEST_ROADS on)
- **Survey.** When `GetNextFarIceHint` answers and Struct is above the open quota plus 30 kg, it surveys the hint centre, once per row. This replaces the "no third ice" branch (`:632-639`).
- **Chain.** It lays pylons every 75 m on the straight line from the nearest standing coverage node, one when the last stands (the `GIceBChain` pattern, `:426`). Each is computed and checked with `CanPlaceBuilding`.
- **Road.** One stretch of at most 120 m a sol from the nearest built node to within 25 m of the drill. Each stretch goes through `CanPlaceRoad`, under the same Struct rule as the six legs (`:725-734`). It starts at the Ice_A drill node (leg 5).
- **Drill.** It drills once the deposit is covered.
- **Rover.** It orders one CargoRover with the drill if none is deployed, aboard or on the way. The open-freight rule cannot start this, because Far tasks do not exist without a rover.
- **Home.** The bot's plant (14, 6) is 10 m from spine node (8, -2) and its pad (10, 20) is 22 m away, so both are road-served already.
- **Timing.** The hint opens when Ice_B is found (about sol 45), not when Ice_B runs low (about sol 85). That leaves the program 60 sols before the 110-sol mark.
- **Report.** A `far ice:` line: hint sol, survey sol, chain, road and drill sols, rover sol, far tonnes freighted, water made over drawn at the end, and the settler hold reason.
- **Pass bar (3 runs):** water production > 0 at sol 110; freight > 0 t; `bSettlersShortWater` false at sol 110 (`RHSimDistricts.cpp:841`).
- `roads=0` stays line-identical to C6a (no far program without roads).

## 5. Inertness
- **Battery: 42 runs line-identical, with one named exception.**
  - Hidden rows are read only by discovered-gated loops (alerts `:1530`, attach `:8283`, markers), by designated-gated ones (era dig `:774`, the ledger print `RHSimCommandlet.cpp:11842`) and by `HasUndiscoveredIce`.
  - Every new branch tests `bFar` on a DISCOVERED deposit or a Far drill, and no suite reveals one.
  - The saved deposit list grows by 3 records. The "(N bytes" line is already masked by `diff_battery.py`. -roads' ledger (`:7726`) compares two legs of one run, so its line does not move.
- **-icelow (`RHSimCommandlet.cpp:3712`) is the one suite that pins the survey sequence.** I9 (`:3843`) and I12 (`:3929-3932`) assert "no undiscovered ice" after Ice_B. **Decision: keep its log identical** with `Debug_RetireFarIce()` at the top of the suite. It removes the trailing Far rows (Ids 7-9) and prints nothing. -icelow remains the D2a two-deposit contract, and -ice re-proves both assertions in the five-deposit world (X10). This is logged as a deviation for the director.
- **Save v42** (D1 holds v41). There are no new fields: the Far columns are row data, re-derived at load, and the hint is derived. The bump is a fence. A v41 binary would load Ice_C-E, warn that the rows are gone, and melt or haul them by the old rules.
- **Era parity.**
  - In `EraLogistics`, a Far source's output moves only if (a) `CountActiveRobots(Rover)` > 0 (a read of parked fragments; the agent-band count is not trusted after a load straight into era), (b) the destination plant is at least 300 m away, and (c) it is road-served (`IsFreightRoute`; the built graph is frozen in era by `CanEnterEraMode`).
  - It moves `min(Out, FarHopperKg - Have)`. Without a rover, era holds the ice at the drill, as the agent band does. So the era is optimistic only on latency.
  - Latency is the risk. The agent band holds 200-1,000 kg at the drill or in flight against about 9.6 t in 10 sols. The X9 ledger therefore counts drill output and in-flight cargo (the C6 R4 test-side rule) and compares H2O mass (ice x 0.9 + water) within 5%.

## 6. Tests: a new fails-loudly `-ice` suite (43 runs; traffic.log does not move)
- X1 data: three Far rows (names, masses, positions, `Far`, SurveyOrder 1-3, RegionId 0, hidden); the six old rows unchanged with `Far` FALSE; Ids 1-6 unchanged; config rows read.
- X2 inert: 20 sols with Far rows hidden, against a `Debug_RetireFarIce` twin. Stocks, deposits, tasks and power digests are bit-identical.
- X3 hint: none while Ice_B is hidden; Ice_C after Ice_B; the 100 m-grid centre; a real scout survey on it reveals Ice_C; a survey 150 m off finds nothing; out-of-order Ice_E leaves the hint on Ice_C.
- X4 rivals: the same survey count gives the same `DiscoveredRivals`, with or without the far surveys.
- X5 placement: drill outside coverage refused; a chain at 75 m accepted; a plant 250 m from discovered Ice_C refused with the reason, 350 m accepted.
- X6 legs: no rover, so no task and the ice waits; a rover but no road, so no task; road and rover give a freight task of 200 < kg ≤ 1,000; haulers never claim it after 600 s; a demolished edge deletes the open task; a plant beside the drill is never the destination.
- X7 end-to-end (agent band, Ice_A/B retired): chain, road, rover, drill. Water made > 0 within 3 sols. Mass is conserved to 1e-6 kg (drilled = at drill + in flight + hoppers + melted / 0.9).
- X8 hopper: one plant up to 1,200 kg; a second plant fed when the first is full; non-Far legs still 2 x Need.
- X9 era: no rover means no teleport; paired 10-sol ledger within 5% (section 5).
- X10 dead end: all five found and low gives stage -1 and the last-ice text (the moved -icelow I9 and I12, re-proved); stage 4 for a drilled Far row with no rover.
- X11 save: v42 round trip mid-trip with Far rows found (cargo returns per SaveColony); v41 refused.
- X12 determinism: two runs, identical digest.
- `rh_battery.sh:37-39`: add `ice` to SUITES; header "42 suites, 43 runs". The commit gate is the line diff of every pre-existing log.

## 7. Placeholder numbers (docs/data, live-synced; each note ends "Placeholder, director review")
- `RH_Deposits.csv`: the new columns `Far`, `SurveyOrder`, `RegionId`, and rows Ice_C 120000 (478, 40), Ice_D 160000 (-560, 330), Ice_E 220000 (260, -760), all FALSE / TRUE / Far TRUE.
- `RH_Config.csv`: `FarIceRevealRadiusCm` 10000; `FarIceHintGridCm` 10000; `FarHopperKg` 1200; `FarPlantExclusionCm` 30000.
- The RHMarsTerrain corridor list (`:437-445`) is NOT extended. Rovers drive roads, not straight corridors, and moving the scatter would move every capture.

## 8. Risks I see
1. **No hauler fallback.** If every rover is worn out or flat, far water stops. Stage 4, the alert and repair by RC_M are the answer. It is deliberate: the director made freight a job.
2. **Era latency** may sit near the 5% bar. X9 decides. If it fails, the fix goes in the ledger (count H2O in flight), not in the colony.
3. **The bot's Struct.** About 1.0 t for Ice_C, with a ship cadence that may land the rover late. The 110-sol bar could miss. If it does, the report must say why.
4. **The Ice_A lane stays hauler-only** (the finding above). Applying the far hopper to every ice leg of 80 m or more would move -traffic and -icelow I11. That is left for later and logged.
5. **The exclusion is gameable.** A plant 301 m from a far drill is legal, but it still needs a 300 m freight leg, so the ruling holds.
6. **A 1.2 t hopper per plant.** Plants at home fill one after another, not shared. That is acceptable for a single melt site, but it is not the R5 rule.
7. **DT row order.** If the re-import reorders rows, Ids 1-6 shift and the battery moves. X1 pins it.
8. **Scenery.** Roads to Ice_C cross the 190-420 m crater ring and can run through scatter rocks (presentation only). The live smoke captures each lane and dig site.
9. **`Debug_RetireFarIce` is a test-only mutation.** It is called only by -icelow and the X2 twin, and X10 covers what it hides.

## 9. Director questions
None. The ruling fixes the split (raw ice home) and the vehicle (rovers on roads). Everything else above is a placeholder for the usual director review.

---

# PROFESSOR REVIEW (2026-10-21)

Two designs were reviewed: **A**, this file, and **B**, the competing draft `w1-distant-ice-design.mine.md` in the session scratchpad. The anchors are on 992383d, read with `git show`. D1 does not touch the deposit, task-board, era, survey or rover code (`git diff 992383d` on `RHSimWorldSubsystem.cpp`), so only line numbers drift.

**Base: B.** It has fewer wrong facts and it closes a hole that A leaves open.
- **The Dig path.** A leaves Dig, the pile leg and era dig open, so far ice can reach home with no road and no rover. B refuses Dig on an outpost.
- **-icelow.** A's harness call fails I3. B's call, traced line by line, holds.
- **Rover range.** B's 2.5 kWh reserve figure is right. A's 7 Wh is off by about 200 times.

**Verdict: BLOCKED.** The merged design is sound. Freight does happen, and the rival roll, the saves and determinism all hold. But the gate it states cannot pass as written:
- One battery line moves that neither doc names.
- The era gate is looser than the agent band in two ways.
- The bot's pylon chain and road block each other.

Required changes 1-12 form the REVISION. Once they are written in, W1 is cleared to build after D1 commits, with no further review round.

## The three freight claims (attack 1)
- **"A Water Plant beside a far drill makes freight pointless": TRUE.** Fluid outputs go to `AddStock` and the production ledger wherever the plant stands (`:1315-1324`). Both designs close this with a 300 m exclusion, enforced at placement and in routing.
- **"Ice to a Water Plant can never be freight": TRUE.**
  - `MeltIce` needs Ice:50 (`RH_Recipes.csv`), so a plant has at most 100 kg of room.
  - `RoverLoadKg` caps the load at that room (`:922-930`).
  - `SetHaulAmount` needs more than 200 kg (`RHSimRovers.cpp:104`).
  - So the bot's 170 m Ice_A lane could never carry freight. Both designs found this. A's far-hopper rule fixes it (see change 6).
- **"Distance alone never makes haulers bad": TRUE in substance.** B's arithmetic leaves out charging.
  - Energy is counted in watt sol-hours, at 50 s to the sol-hour (`RHRobotStateTree.cpp:414-420`).
  - A hauler on a 480 m round trip uses about 384 Wh, so it makes about 3 trips per charge (1,125 Wh above the seek fraction).
  - Two haulers still roughly match one drill. A rule is needed, and both designs ban haulers by source.
- **The A-only trap: Dig.**
  - `Dig` designates any discovered deposit, of any type (`:6966-6976`).
  - Excavators claim the nearest designated deposit, with no range limit (`:10303-10313`).
  - The pile leg (`:941`) posts hauler tasks.
  - Era dig moves up to 200 kg a step from any designated deposit into any hopper that wants it, with no distance, road or rover check (`:768-797`).
  - Under A, "Dig Ice_C, then switch to era" fills the home plant with no road at all. That is the exploit the brief asks about (attack 3). B's Dig refusal closes it in both bands.

## Grafts from A
- The far-hopper leg rule (`FarHopperKg`, room = `FarHopperKg - Have`, freight over 200 kg). It replaces B's whole 1,000 kg load with overfill (change 6).
- Early hint timing: the hint opens once every non-outpost ice row is found (about sol 45), not at stage 0 (about sol 85). The grid and the reveal radius change (change 8).
- The era ledger in H2O-equivalent mass (ice x 0.9 + water), counting what sits at the drill and what is in flight.
- The X2 inert twin: 20 sols with the outposts hidden, against a twin with them removed. It is test-only and must not be used inside -icelow.
- A's report fields: the sol of the hint, the survey, the chain, the road, the drill and the rover.
- Keep B's suite name, `-farice`.

## REQUIRED changes

1. **Name every battery line that moves. Commit no mask.**
   - Every log has exactly one `Colony sim online ... Deposits 6.` line. I checked all 42 of 42 in `Saved/baselines/battery_post_c6b`. All 43 post-D1 logs move it to 9.
   - **Both docs miss a second moved line.** `roads.log` R2 prints `(%d lines` from `Plain.Num()`, which is 8 + 2 x deposits (`RHSimCommandlet.cpp:7726`, `:7744`). It goes from `(20 lines` to `(26 lines`.
   - A's "42 runs line-identical" is false, and so is B's "prints no numbers".
   - The expected `diff_battery.py` total is **88 lines** (44 removed, 44 added: 43 init lines plus R2).
   - Today a `Deposits \d+` mask would hide only the init line. Committed, it would also hide any later DT re-import that drops or adds a row, which is A's own risk 7. If a filter is used at the gate, anchor it on `Colony sim online`, keep it out of the repo, and leave R2 unmasked and named.
   - Fix the run count: after D1 there are 42 suites and 43 runs, so W1 makes **43 suites and 44 runs**. Both docs are off by one.
2. **Refuse Dig on an outpost deposit (B's rule).** Also test that era dig can never touch an outpost.
3. **-icelow: use B's `Debug_SpendOutpostIce()` after I8, not A's retire call.**
   - A's `Debug_RetireFarIce()` "at the top" fails I3. `ForEachDeposit` walks every DT row, and `bAll &= D && ...` needs sim state for each one (`RHSimCommandlet.cpp:3770-3774`).
   - B's call, traced through the suite:
     - I9 gives stage 1, then 2, and `!HasUndiscoveredIce`.
     - At I10 the load-time latch rebuild (`:12043-12048`) re-marks the spent rows.
     - I11 gives stage 3, then -1.
     - I12 gives "Ration water".
   - The call must stay silent: write `IceLowAnnounced` directly, and broadcast no alert.
4. **Make the era gate as strict as the agent band.**
   - (a) Use the shared destination helper: nearest plant, 300 m or more away, road-served approach, ties to the lower Id, never a store. `EraLogistics` today picks the FIRST consumer by building order (`:828-846`).
   - (b) Gate on `!bFleetHold`, and on a reachable rover pad (`FindNearestRoverPad`), as well as `CountActiveRobots`.
     - `CountActiveRobots` counts any rover with charge above 0 (`RHAgentSubsystem.cpp:307-325`).
     - But an agent-band rover below `ChargeSeekFraction` with no road-served pad claims nothing (`RHSimRovers.cpp:160-169`), and a held fleet claims nothing (`:156`, `:906`).
     - Without the pad and hold checks, era ships ice forever where the agent band has stopped.
   - (c) Use A's H2O ledger.
   - (d) Name the known limit: `EraStep` applies no wear (`:701-725`), so a long era stint is optimistic about rover availability. This is outside the 10-sol bar.
5. **Specify the bot's chain and road geometry.**
   - A pylon on the road's centre line blocks the road: the strip is 6 m wide and a pylon's half-extent is 1 m (`RHSimRoads.cpp:224-244`, `GetFootprintHalfCm`).
   - A pylon too far off the line leaves road nodes uncovered. Nodes are at most 40 m apart, and every node must be in coverage (`:212-221`).
   - At 75 m spacing the covered lens at the midpoint is only ±13.9 m wide.
   - B's chain starts at (44,10) and its road at (76,-2). The lines converge, so the last pylons stand inside the strip. A does not say where its chain starts.
   - Required:
     - space pylons 70 m or less, as `GIceBChain` does (`RHPlaytest.cpp:426`; ±19.4 m lens);
     - set them 6-10 m to one side of the planned road line;
     - check every planned road node against the planned discs before any pylon is ordered.
6. **Leg size: use A's far hopper (posted once over 200 kg), not B's whole 1,000 kg load.**
   - B posts only when the drill holds 1,000 kg. That strands up to 999 kg when the deposit runs out, and it delays the first water by about 1,250 s.
   - B also needs an overfill exception to R5.
   - Era moves `min(Out, FarHopperKg - Have)` through the same helper as change 4a.
7. **Outpost ice never goes to a store, even when no ice consumer exists.**
   - When nothing consumes ice, the generic `!bAnyConsumer` overflow (`:1072-1082`) would send it to the Lander.
   - Use B's separate branch with `continue`. Add a test with no Water Plant anywhere.
8. **The hint: A's timing, B's reveal rule.**
   - Round the hint centre to a **50 m** grid. The worst offset is 35.4 m, inside RC_S's 60 m radius.
   - Drop `FarIceRevealRadiusCm`, and leave `CompleteSurvey` unchanged.
   - F1 asserts that each row lies within `WorkRate` of its hint centre. A's own rows sit 45.6, 50 and 56.6 m from their 100 m-grid centres, so the larger radius bought nothing.
9. **Correct the energy figures** (50 s to the sol-hour):
   - a hauler uses about 384 Wh per 480 m round trip, not 5 Wh;
   - the rover's reserve is about 1.54 kWh at 480 m and about 2.5 kWh at 806 m, not 7 Wh.
   - The R7 proof still holds (`RHRobotStateTree.cpp:440-441`), but the doc must not carry wrong numbers.
10. **Ruling fidelity: log the distance as a named deviation. Ask no director question.**
    - The ruling says "farther out", but both first rows (A at 480 m, B at 453 m) are nearer than Ice_B, which is 702 m out at (60, 700) in `RH_Deposits.csv`.
    - Record this at the gate, with the Struct reason. Undoing it costs a CSV edit.
    - B's Q1 is answered by the ruling itself ("roads and cargo rovers bring the ice home"), so drop it.
11. **The bot's bar.** All three must hold:
    - water made > 0 at sol 110;
    - freight > 0 t;
    - for each ship, the water leg is not what caps the settlers after the first outpost load (B's measure, from `GetSettlerLegRoom`).
    - The report also states:
      - whether Struct was the limit;
      - any rival found by the extra surveys (the roll is seeded by survey count, `:10735`).
    - Nothing is tuned to pass. The 1.0-1.05 t estimate is unverified, so the run decides it.
12. **Sequencing.**
    - Build only after D1 commits.
    - Re-anchor every line reference to the post-D1 code.
    - Save v42, strict. Deposits load whole from the save (`:11527-11533`), so a v41 save would silently lose the outpost rows. The fence is required, not cosmetic.

## Optional
- **O1.** Siting Ice_C next to the standard Ice_B chain, about 450 m north, would reuse coverage the guide already sends the player to lay, and save about 200 kg of pylons. It is a data-only change, so try it if the bot fails on Struct.
- **O2.** A's finding on C6 stands outside W1: a 200 kg legacy task posted before any rover can never become freight (`RHSimRovers.cpp:135`, `>`). Log it for a later gate.

---

# PROFESSOR REVIEW (2026-10-21)

*This is a second, independent review of this file (design A), written at the same time as the review above. The review above appeared in the file while I worked, and I have not edited it. Anchors are `git show 992383d:<path>`. Where an item repeats the review above, it says so in brackets. Items marked NEW are not covered there.*

**Verdict: BLOCKED.** The architecture holds up: a row flag, a rover-only far leg, a far hopper, a hint that is a pure read, no new saved field, and a v42 fence. The defects are in the one water path the colony has left once Ice_A and Ice_B are gone:
- the far rule covers only one of the three ways ice leaves a deposit;
- three states strand the ice for good;
- the commit gate fails on its first run.

I agree with the review above's verdict and its items 1-12. Items 3, 4, 5, 8, 9, 10 and 13 below are additional REQUIRED changes. Item 12 below disagrees with that review's O2.

## Verified and sound
- **The freight facts hold.** Ice to a plant can never be freight today: the room is at most 2 x 50 kg, `RoverLoadKg` caps the load at the room (`RHSimWorldSubsystem.cpp:924-932`), and O4 needs more than 200 kg (`RHSimRovers.cpp:104`).
- **No battery suite completes a survey.**
  - The commandlet never issues the `Survey` verb.
  - Deposits are revealed only by `Debug_RevealDeposit` (`RHSimCommandlet.cpp:3797`, `:3838`).
  - -espionage drives the rival roll directly (`:1449-1457`).
  - The roll is seeded by the survey count alone (`:10735`). X4 is true by construction.
- **Re-deriving Far, SurveyOrder and RegionId by RowName at load is safe.** It is the InitialKg pattern (`:11535-11548`).
- **The v42 bump is required, not cosmetic.** At load the deposit list comes only from the save (`:11528-11533`), and load requires an exact version match (`:11455`). Without the bump, a W1 binary would load a D1 save with no Ice_C-E: no hint would ever open, and the colony would reach the dead end. [same as the review above, item 12]
- **Deleting an UNCLAIMED far task loses no ice.** A task holds no mass; the ice stays in the drill's OutputKg. A fleet hold zeroes ActiveRovers (`:906-907`), and the tasks are simply re-posted when the hold ends.
- **There is no quota deadlock in the sim.**
  - Quotas never fail (`RH_Quotas.csv` notes).
  - Water appears only in Q1 (sol 0-10).
  - Competition for Struct is a risk to the bot's timing only (item 11).

## REQUIRED changes

1. **Refuse Dig on a Far deposit, in both bands.** [= review above, item 2] This is a blocker.
   - The `Dig` verb designates any discovered deposit of any type (`:6963-6984`).
   - The player issues it by clicking a deposit, with no type filter (`RHPlayerController.cpp:1488-1494`). `TryClaimDig` takes any designated deposit (`:10295-10320`).
   - Failure: survey Ice_C and Dig it. The RC-E digs a pile 480 m out.
     - The deposit leg (`:941-1016`) posts 200 kg hauler tasks to the home plant. Their source is `From.DepositId`, not a Far drill, so none of the section 3 rules apply.
     - Era dig (`:768-800`) puts the ice straight into the hopper, at any distance.
   - Fix: refuse the Dig order with a reason (Gate-D placeholder text), and skip Far deposits in `TryClaimDig` and the era dig loop. X6 tests both bands.

2. **Far ice never falls to a store, in either band.** [= review above, items 4a and 7] This is a blocker.
   - In the agent band, ice falls to a store when `!bAnyConsumer` (`:1076-1085`). In era it falls to a store whenever no demander has room, with no condition (`:862-873`).
   - Nothing ever moves stored ice back to a plant. The store→producer leg runs only on a construction shortage (`:1105-1110`), and -icelow I11 pins "ice waits at its drill".
   - Failure in the agent band: the only Water Plant is demolished to rebuild it, or is still a site (`:1048`). The far ice then rides as freight to the road-served Lander and stays there for good.
   - Failure in era: with no rover or a full hopper, section 5's rules (a)-(c) never say that the store branch at `:862` is skipped.

3. **Pick the far destination from plants a rover can actually serve.** NEW. This is a blocker.
   - The output leg first picks the nearest plant with room (`:1036-1071`). Only then does it test freight, and only on that one plant (`:1098` → `RHSimRovers.cpp:99-117`).
   - Failure: there are two home plants. The nearer one is 465 m from the drill and not road-served; the other is 480 m away and road-served.
     - The nearer plant is chosen, the task is not freight, and nothing is posted, ever.
     - Water stops even though a road, a rover and a served plant all exist.
     - Stage 4 then tells the player to build things they already have.
   - Fix: for a Far source, a plant qualifies only if it is at least 300 m away, has far-hopper room, and passes `IsFreightRoute`. Walk the candidates nearest first, with a cap on route plans, and plan only while ActiveRovers > 0.
   - Era (c) must use the same helper [the review above, item 4a, has the same helper; add the freight test to it]. X8 adds this case.

4. **A halted rover must not hold the drill's only lane.** NEW. This is a blocker.
   - `HasOpenTask` counts claimed tasks (`:10941-10953`), so a drill and plant pair has one task at a time, and the board will not post a second (`:1090`).
   - `RefreshFreightFlags` touches unclaimed tasks only (`RHSimRovers.cpp:130-132`).
   - A rover that is flat away from a pad, or worn to the halt threshold, stops ticking and keeps its claim (`RHRobotBrainProcessor.cpp:103-110`). A flat rover is never revived. RC-M repair needs Spare Parts and only fixes wear (`:10752-10790`).
   - Failure: an edge is demolished under a loaded rover. This is the case C6 R7 named as outside its reserve proof, and the crawl at 50 s per sol-hour drains the battery fast. The rover goes flat.
     - Its far task stays claimed forever.
     - The player follows stage 4 and lands a second rover, but no task is ever posted for it.
     - Far water stops for good.
   - Fix (your choice of mechanism, but this outcome is required): for a Far source, the open-task test ignores tasks claimed by an inactive rover. That rover keeps its cargo, which counts as in flight, and delivers it if revived.
   - X6 tests it: rover A halted with cargo aboard, rover B restores deliveries, mass conserved.

5. **Cap a far drill's buffer, or the alerts and the guide are wrong.** NEW.
   - Extraction checks only the ground (`:1440-1452`, `:1487-1497`); there is no output cap.
   - The low-ice alert and the stage read use `RemainingKg` (`:1551-1578`). The stage loop skips low deposits (`:1604`), after returning early when no healthy ice is left (`:1597-1600`).
   - Failure: Ice_C is drilled and there is no rover. In about 94 sols, 90 t moves into the drill's OutputKg.
     - "ICE_C RUNNING LOW" fires, and stage 4 disappears.
     - Once all five deposits are found and low, the player gets D87 "The last known ice" and stage -1, while 90 t sits one rover away.
     - The drill also draws 250 W for the whole stretch.
   - Fix: a Far drill starts no DrillIce batch while its Ice output is at least `FarDrillBufferKg` (1,000, a placeholder). Put the check in the shared production step so both bands obey it.
   - X10 adds a case: 30 sols with no rover, the deposit is not low, and stage 4 shows.

6. **Name both moved battery lines.** [= review above, item 1]
   - `Colony sim online ... Deposits 6.` (`:424-425`) appears once in every log. I checked all 42 in `battery_post_c6b`.
   - The `roads.log` R2 line `(%d lines` is 8 + 2 x deposits (`RHSimCommandlet.cpp:7726`, `:7744`), so it moves from 20 to 26.

7. **-icelow: fix I3, and show the director what the harness hides.** [I3 = review above, item 3] The second half is NEW.
   - A's retire call "at the top" fails I3. I3 walks the DT rows and needs a sim deposit for each one (`RHSimCommandlet.cpp:3768-3778`).
   - Whichever harness call is used (A's retire, or B's spend after I8), it hides a real change in the shipped game.
     - After Ice_B is found, `HasUndiscoveredIce` (`:1557-1567`) is now true.
     - So Ice_B's low alert reads "Survey for more ice and carry the grid to it" instead of D87, which the director approved at 992383d.
     - Stage 0 replaces stage -1 until all five deposits are found.
   - The commit and the design-decisions entry must quote the before and after text for that moment.

8. **X9 must test delivery, not only digging.** NEW. This disagrees with the review above, item 4c, where that is the only measure.
   - Counting ice as H2O wherever it sits (at the drill, in flight, in a hopper or melted) makes both bands' ledgers equal 0.9 x drilled, minus the draw.
   - So a band that delivers nothing still passes. The stock is 40-45 t, and nobody goes short in 10 sols.
   - Add two checks:
     - the water MELTED over the 10 sols (from the production ledger) is within 5%, or within one load plus a hopper;
     - at the end, the agent band's ice at the drill plus ice in flight is no more than 2 loads.

9. **State the real far load size.** NEW.
   - DrillIce credits 40 kg at the end of each 1 h batch (`RH_Recipes.csv`, `:1312`).
   - A far task is therefore posted on the first step with more than 200 kg at the drill, which is 240 kg. `SetHaulAmount` fixes the amount at that moment, and `HaulLoad` takes `min(cap, AmountKg)` (`:10360`).
   - So every far trip carries about 240 kg, about four trips a sol, never 1 t.
   - Several numbers in the doc are wrong as a result: "1 t per about 160 s", "roughly 8x the hauler", and "take the colony's only two haulers off everything else". X6's bound of 200 < kg ≤ 1,000 passes trivially.
   - The ruling stands without any throughput argument, so correct the numbers rather than defend them.

10. **Check placement against the scenery that is actually there.** NEW.
    - The map section measures clearance from peak centres, but peaks are meshes.
      - A peak's base radius is `Hcm x FRandRange(1.4, 2.2)` (`RHMarsTerrain.cpp:370-377`), and ridge noise stretches it by up to 1.44x (`:198-200`).
      - The hero heart peak sits exactly at (410, 320) m, since the first peak has no offset (`:405-406`). Its base radius is 231-660 m, so Ice_C, 288 m away, is inside it for most random draws.
      - Mesa base radii are `Hcm x 2.4-3.6`, which is 216-612 m (`:400-401`). So the mesa band's "inner edge" is not 0.9 km, and Ice_E at 803 m lies on it.
    - **Unverified:** I emulated `FRandomStream(20260708)` in Python, assuming clang's argument order. It puts Ice_C under about 10-70 m of mesh and Ice_E under about 25 m. The same emulation buries the bench, which contradicts the code comment, so treat this as a reason to look, not as a finding.
    - Fix: capture each site, and each chain and road line, in the editor before X1 pins the positions. Move any row that is covered.

11. **The bot's chain and road, and its pass bar.** [= review above, items 5 and 11]
    - `bSettlersShortWater` is set only while settlers are being held (`RHSimDistricts.cpp:836-841`). The bar as written passes trivially when no settlers are waiting.

12. **The "latent C6 trap": remove "closes" and gate the deletion on Far.** NEW. This partly disagrees with the review above, O2.
    - While any hauler works, a ≤ 200 kg task posted before a rover arrives is an ordinary hauler task (`:10333-10334`). A hauler takes it, and the next post is sized for the rover; -traffic X5 already shows this re-post.
    - It is stuck only when no hauler can work. That is a liveness edge for a later gate.
    - W1 does not close it, because Far tasks are never posted at 200 kg or less.
    - The deletion in `RefreshFreightFlags` must be gated on a Far source. -traffic X4-X7 and X15 run non-Far freight through that function (`RHSimCommandlet.cpp:10250-10353`, `:10817-10826`). Leave `RHSimRovers.cpp:135` untouched.

13. **Gate-D: list the new text and the approved rows W1 makes untrue.** NEW.
    - New text: the placement refusal (not marked as a placeholder in section 3), the hint line, the alert advice, stage 4, and item 1's Dig refusal.
    - Approved rows that W1 affects:
      - D93 ("Its ice is hauled to the Water Plant like the first drill's", `RHCommandDeckGuide.cpp:498`) is false for a Far deposit at stage 2.
      - D87 and D91 now arrive later than before.
      - D97 never shows for Far rows.
    - The "farther out" deviation [review above, item 10] must state the player-visible inconsistency: 700 m Ice_B is melted on site (`RHPlaytest.cpp:1106`), while 480 m Ice_C is refused because of a flag the player cannot see.

## Optional
- **O1.** Give `FRHTask` a runtime `bFarSource`, set at post and re-derived at load. `TryClaimHaul` then skips Far tasks without looking up a building and a deposit for every task on every claim.
- **O2.** Show stage 4 also when a far freight task exists but no active rover can reach its pickup (the split network from C6 R6).
- **O3.** Drop `RegionId` until territories need it. It is a struct change, and nothing in W1 reads it.
- **O4.** The bot's straight road to Ice_C crosses the rim of a crater at about (208, 118) m, radius 18 m. This is from my emulation of `FRandomStream(90210)` (`RHMarsTerrain.cpp:72-89`). It is presentation only.
- **O5.** X2's deposit digest must compare Ids 1-6 only, because the twin removes rows 7-9.
- **O6.** X6: a hauler must not claim a far task after a load, before the first board step runs (`bFreight` is false at that point).
- **O7.** The bot's survey costs nothing (`:6985-6994`), so gating it on Struct only delays the hint.
- **O8.** The last 200 kg or less of each far deposit can never become freight. Allow one final load once the deposit is empty, or say that it stays behind.

VERDICT: BLOCKED. Unblock when a REVISION section carries the review above's items 1-12 plus items 3, 4, 5, 7 (second half), 8, 9, 10, 12 and 13 here. No further review round is needed.

---

# MERGED DESIGN AND REVISION (2026-10-21)

This section is the W1 spec. It replaces sections 1-9 above and design B (the scratchpad draft) wherever they differ. An implementer needs nothing else. It answers:
- the first PROFESSOR REVIEW's required changes 1-12, with its grafts from A;
- the second review's items 3, 4, 5, 7 (second half), 8, 9, 10, 12 and 13, tagged [R2-n] below;
- the second review's optional O1, O5, O6, O7 and O8, adopted. O2 is adopted as stage 4's wording. O3 is declined: RegionId is the requested step-4 hook.

Anchors are given as `992383d` / `D1 working tree today`. Change 12 says: build after D1 commits, then re-anchor to that commit. D1 touches none of this logic; only line numbers drift.

| what | 992383d | D1 tree |
|---|---|---|
| deposit init | `RHSimWorldSubsystem.cpp:392-406` | same |
| init log `Deposits %d` | `:424` | `:424` |
| `EraLogistics`: dig loop / output loop | `:731`, `:766-822` / `:824-879` | `:731`, `:766` / `:825` |
| `StepTaskBoard`: pile leg / output leg | `:900`, `:933-1015` / `:1017-1102` | `:900`, `:933` / `:1017` |
| `StepDepositAlerts` / `HasUndiscoveredIce` / `GetIceExpansionStage` | `:1525` / `:1557` / `:1594` | `:1524` / `:1556` / `:1585` |
| `CanPlaceBuildingSized` (coverage at `:6746`) | `:6545` | `:6796` |
| `Dig` verb | `:6963-6984` | `:7214` |
| drill attach | `:8279-8292` | `:8530` |
| `TryClaimDig` / `TryClaimHaul` | `:10295` / `:10322-10345` | `:10546` / `:10573` |
| `CompleteSurvey` / `MaybeDiscoverSettlement` | `:10672` / `:10712` | `:10923` / `:10963` |
| deposit save / load / row re-derive | `:11078` / `:11527-11533` / `:11534-11549` | `:11330` / `:11791` / `:11794` |
| ice-low latch rebuild at load | `:12041-12048` | `:12327-12334` |

Other files: `RHSimRovers.cpp` (`IsRoadServed :34`, `FindNearestRoverPad :40`, `IsFreightRoute :83`, `RefreshFreightFlags :119`, `TryClaimFreight :154`); `RHAgentSubsystem.cpp:307` (`CountActiveRobots`); `RHRows.h:202` (`FRHDepositRow`); `RHSimTypes.h:108` (`FRHDepositState`); `RHCommandDeckGuide.cpp:466-515`; `RHPlaytest.cpp`.

## M1. Data
- **`FRHDepositRow` gains two columns** (a struct change; the director compiles, then the CSV is re-imported and the DT live-synced):
  - `Outpost` (bool): TRUE on the three new rows, FALSE on the six existing ones.
  - `RegionId` (int32): 0 everywhere. This is the step-4 hook, the same convention as districts, road edges and D1's floors.
- **`FRHDepositState` gains `bOutpost` and `RegionId`.** They are set from the row at init (`:392-406`) and re-derived by RowName at load, beside `InitialKg` (`:11534-11549`). They are never saved.
- **Three rows, appended after Ice_B.** Ids 1-6 do not move; the new rows are 7, 8 and 9. Every row is hidden (`SurfaceVisible` FALSE, `SliceActive` TRUE). The positions depend on the distance call (section M10):

| row | set (a): nearer | set (c): beyond Ice_B (recommended) | mass |
|---|---|---|---|
| Ice_C | (390, -230): 453 m, ESE | (624, -360): 720 m, ESE (the same bearing, -30 deg) | 80 t |
| Ice_D | (-20, -620): 620 m, S | (-20, -760): 760 m, S (Ore_B lies on the way) | 120 t |
| Ice_E | (-700, 400): 806 m, WNW | (-700, 400): 806 m, WNW | 200 t |

- **Why authored rows.** The sim may not read `RHMarsTerrain` (the module rule), so a generator could not avoid craters, mesas or the massif. A seed would be one more saved and determinism-bearing number. Row order is the hint order.
- **RH_Config rows:**
  - `OutpostMeltMinCm` 30000: no building with an Ice-consuming recipe within 300 m of a FOUND outpost, and outpost ice is never delivered to one.
  - `OutpostHopperKg` 1200: a plant's hopper for outpost legs.
  - `OutpostHintGridCm` 5000: the 50 m hint grid.
  - `OutpostDrillBufferKg` 1000 [R2-5]: an outpost drill starts no batch while it holds this much ice.
  - Every note ends "Placeholder, director review".
- **`FRHTask` gains a runtime `bOutpostSource`** (O1). It is set at post and re-derived from `From` after a load. It is never saved: tasks are written field by field (`:10880-10885`).
- **Unchanged:** FreightMinCm, RoverLastLegCm, the hauler fallback, the road cost and every row of RH_Robots and RH_Buildings.

## M2. How outposts are found (change 8: A's timing, B's reveal)
- **`CompleteSurvey` is unchanged.** A scout reveals every hidden deposit within its `WorkRate` (RC_S: 60 m). There is no new reveal path and no new roll.
- **`GetOutpostHint(FVector& OutCentreCm) const`** returns the lowest-Id undiscovered outpost row, but only once every NON-outpost Ice row is discovered. In play that is about sol 45, when Ice_B is found.
  - Its centre is the row rounded to the 50 m grid, half away from zero, in integer cm.
  - The worst offset is 35.4 m, inside 60 m, so a survey on the centre always reveals the row.
  - The centres: set (a) (400, -250), (0, -600), (-700, 400); set (c) (600, -350), (0, -750), (-700, 400). The largest offset is 28.3 m.
- **The rival roll is untouched.** Each completed survey is one `MaybeDiscoverSettlement(SurveyHistory.Num())` call. Extra surveys change which survey finds a rival, deterministically, and that is ordinary scouting.
- **No battery suite completes a survey,** and -espionage calls the roll directly (`RHSimCommandlet.cpp:1455`).
- **Advice and guide (Gate-D-neutral placeholders):**
  - While the hint answers, the RUNNING LOW advice (`:1540-1547`) reads "Survey for more ice farther out: outpost ice comes home by road and cargo rover." The "Survey for more ice" prefix that -icelow I7 checks is kept. I7's state has Ice_B hidden, so its text does not change.
  - "The last known ice" appears only when no ice row, outposts included, is undiscovered.
  - The guide's "Find more ice" step names the hint ("Ice signatures about 720 m east-south-east of the Lander. ORDER > Survey there.").
  - The survey map draws the hint as a dashed 60 m ring.

## M3. The outpost rules: raw ice comes home, by rover only
1. **No melting near an outpost (placement).** `CanPlaceBuildingSized` refuses, on level 0, any Def whose slice recipe consumes Ice (`FindRunnableRecipe` with the predicate `Inputs.Contains(Ice)`; today only the WaterPlant) within `OutpostMeltMinCm` of a DISCOVERED outpost. The reason is "Outpost ice comes home raw: build the Water Plant at least 300 m away". Undiscovered rows are not checked, so nothing leaks.
2. **No digging (change 2).**
   - The `Dig` verb refuses an outpost: "Outpost ice is drilled, not dug - place an Ice Drill on it".
   - As defence in depth for hand-made states, `TryClaimDig`, the board's pile leg and `EraLogistics`' dig loop also skip `bOutpost`.
3. **The outpost branch of the output leg (changes 6 and 7).** It is the first test inside `for (const auto& Out : B.OutputKg)` (`:1017`). When `B` is a drill attached to an outpost deposit, it runs and then `continue`s, so the generic branch, including the `!bAnyConsumer` store overflow (`:1072-1082`), never runs for outpost ice. Its destination comes from the shared helper `FindOutpostDestination(const FRHBuildingInstance& Drill, int32& OutDestId, double& OutRoomKg) const` [R2-3]:
   - Candidates are finished level-0 buildings with an Ice-consuming recipe that are at least `OutpostMeltMinCm` from the drill's deposit and hold `Have + 200 < OutpostHopperKg`.
   - They are walked nearest first, by integer straight cm, ties to the lower Id. The FIRST one for which `IsFreightRoute(drill, candidate)` passes wins. That test covers both road-served ends and connectivity.
   - At most `RoverClaimPlans` (4) route plans per call. Plans run only while `ActiveRovers > 0` on the board, or while the era gate (M5) holds.
   - So a nearer plant that is not road-served never blocks a farther one that is.
   - The room is `OutpostHopperKg - Have`. There is never a store.
   - **Post rule.** `Kg = min(Out, RC_R.Cargo_kg, Room)`. Post only when the helper returns a plant and `Kg > 200`. Then set `AmountKg = Kg`, `bFreight = true` and `bOutpostSource = true`. Otherwise post nothing, and the ice waits at the drill under the buffer cap (M3.8).
   - **The last of a deposit (O8).** When the deposit's `RemainingKg == 0` and no batch is pending, the remainder posts as one final load if it is at least `HaulLoadMinKg` (25 kg), even at 200 kg or less. Nothing is stranded.
   - **Lane liveness [R2-4].**
     - `HasOpenTask` counts claimed tasks (`:10941-10953`). For an outpost source, the open-task test ignores a task whose claimant is no longer active.
     - "Active" is read through a new const accessor, `URHAgentSubsystem::IsRobotActive(FMassEntityHandle, float WearHalt)`: valid, wear below halt, charge above 0, the same test as `CountActiveRobots` (`RHAgentSubsystem.cpp:307-327`).
     - So a flat or worn rover cannot hold the drill's only lane. A second rover gets a fresh task.
     - The halted rover keeps its cargo, which counts as in flight, and delivers it against its own still-claimed task if it is ever revived. Mass is conserved.
   - **The real load size [R2-9].** DrillIce credits 40 kg at the end of each 1 h batch (50 s). The first step over 200 kg holds 240 kg, `AmountKg` is fixed then, and `HaulLoad` takes `min(cap, AmountKg)` (`:10360`).
     - Every far trip carries about **240 kg, about four trips a sol**, never 1 t.
     - The rover's 1 t capacity is headroom, not the flow. The flow is the drill's 960 kg a sol.
     - No throughput claim is made for rovers over haulers. The ruling stands on its own.
   - **The hopper deviation from C6 R5, logged.** A plant fed from an outpost holds up to 1,200 kg. Plants fill one after another, not shared. That is harmless, because water pools. Non-outpost legs keep `2 x Need` exactly.
4. **Haulers never take outpost ice.** `TryClaimHaul` skips a task with `bOutpostSource` set, with no 300 s fallback.
   - The flag is re-derived at load (O1). So a hauler cannot claim a far task after a load, before the first board step runs (O6).
   - With charging counted (change 9), a hauler uses about 384 Wh on a 480 m round trip, or 576 Wh at 720 m, against 1,125 Wh above its seek fraction. That is about 3 trips, or 2, a charge.
   - Two haulers would still match one drill (0.8 kg/s). So the rule is by source; a rule by distance would move battery hauls.
5. **`RefreshFreightFlags`** (`RHSimRovers.cpp:119-142`) REMOVES an unclaimed task with `bOutpostSource` set that fails the freight test (no active rover, or a road demolished). The board re-posts it when it passes again.
   - A task holds no mass, so deleting it loses no ice.
   - A claimed run finishes, and C6 R3 returns the cargo on a forced charge.
   - **The removal is gated on `bOutpostSource` only [R2-12].** Non-outpost freight, which -traffic X4-X7 and X15 run through this function, is untouched. So is `RHSimRovers.cpp:135`. W1 does not fix the C6 "200 kg task posted before any rover" edge. That edge is a liveness gap only when no hauler can work, and it stays logged for a later gate.
6. **Rovers, unchanged.**
   - `PlanRoute` is integer, with int64 costs and K=4 entries. It has no length limit. One order lays up to 64 pieces (about 2.3 km).
   - **Reserves (change 9):** `DriveWh(3 x straight to the pad)` is 1.47 kWh at 453 m, 2.27 kWh at 720 m and 2.52 kWh at 806 m, from the pad at (10, 20), against 6 kWh. A round trip on the road uses about 0.54 kWh at 453 m and 0.75 kWh at 720 m.
   - The R7 proof holds unchanged.
7. **Guide stages** (`GetIceExpansionStage`):
   - The aggregate over non-outpost deposits is today's, so every -icelow value is kept.
   - When that aggregate resolves (-1), or no non-outpost deposit is healthy, the found healthy outposts are read in Id order: stage 1 (carry the grid), stage 2 (drill), stage 4 (lay a road: a drilled outpost whose `FindOutpostDestination` fails, or whose `IsFreightRoute` fails), stage 5 (bring a cargo rover: the route passes, no rover is active and none is in `ImportStock["RC_R"]`), else -1.
   - Stage 3 (melt where drilled) is never offered for an outpost.
   - **Stage 4 also shows (O2)** when outpost freight is open but no active rover reaches its pickup by road (the split network of C6 R6).
   - The guide gains the steps "Road the outpost home" and "Bring a cargo rover". Their copy is a Gate-D-neutral placeholder.
8. **Drill buffer cap [R2-5].**
   - In the shared production step, the extraction branch of `InputsOk` (`:1440-1452`) gains one test: a building attached to an outpost deposit does not START a batch while `OutputKg[Ice] >= OutpostDrillBufferKg`. A batch already running finishes.
   - The step is shared, so both bands obey it.
   - With no rover, an outpost drill fills to about 1 t and idles. Its deposit stays healthy, so no false RUNNING LOW fires, stage 4 keeps showing, and the 250 W draw stops once the batch ends.
   - Non-outpost extractors take no branch here.

## M4. Power and coverage
- **The existing pylon chain.** Power is one pool, and coverage is placement only. Every road node must be in coverage, and edges are at most 40 m (`RHSimRoads.cpp:213-221`). So the road needs the chain the drill needs. An outpost pylon or a self-powered drill would save nothing.
- **Geometry (change 5):**
  - Pylons stand at most **70 m** apart. Two 40 m discs then leave a lens ±19.4 m wide at the midpoint.
  - Pylons stand **6-10 m to one side** of the road line. The strip's half-width is 3 m plus the pylon's 1 m, so 4 m or more clears it, and 19.4 m or less keeps every node covered.
  - The bot (M6) checks every planned road node against the planned and standing discs before it orders any pylon.

## M5. Era (change 4): as strict as the agent band
- **The outpost branch is the first test in `EraLogistics`' output loop (`:824`).** For an outpost drill it moves `Kg = min(Out, RC_R.Cargo_kg, Room)` per step from the drill to the plant, and only if ALL of these hold:
  - `Kg > 200`;
  - `!bFleetHold`;
  - `CountActiveRobots(Rover, WearHaltThreshold) > 0`, a read-only fragment read with no write;
  - a road-served rover pad exists (`FindNearestRoverPad` from the Lander). An agent-band rover below its seek fraction with no such pad claims nothing (`RHSimRovers.cpp:160-169`);
  - `FindOutpostDestination` returns a plant. It is the same helper the board uses: the nearest plant 300 m or more away whose `IsFreightRoute` passes, ties to the lower Id, at most 4 plans, never a store. The built graph is frozen in era (`CanEnterEraMode`).
- **Then it `continue`s, and never goes to a store.** Era dig skips outposts (M3.2). So "Dig it, then switch to era" and "no road, then switch to era" both move nothing.
- **The ledger is in H2O-equivalent (A's graft):** `0.9 x (ice at drills + in hoppers + in flight) + Water stock`. In-flight cargo is read from the rover fragments (the C6 R4 test-side rule).
- **That alone cannot fail [R2-8].** A band that delivers nothing would still pass it. So F14 adds two delivery checks:
  - (i) the Water MELTED over the 10 sols, read from the production ledger (`NoteProduced`), agrees between the bands within 5%, or within one load plus one hopper (240 + 1,200 kg of ice, x 0.9), whichever is larger;
  - (ii) at the end, the agent band's ice at the drill plus ice in flight is at most 2 loads (480 kg).
- **Known limit, named:** `EraStep` applies no wear (`:701-725`), so a long era stint is optimistic about rover availability. It falls outside the 10-sol bar.

## M6. The playtest bot (`RHPlaytest.cpp`; on with roads; `RH_PLAYTEST_OUTPOST=0` returns the C6c bot line for line)
1. **Survey.** When `GetOutpostHint` answers, survey the hint centre at once, once per row. A survey costs no Struct (`:6985-6994`), so it is not gated on Struct (O7). This replaces the "no third ice" branch (`:632-639`).
2. **Plan once.**
   - The road line runs from the built node (76, -2) toward the drill and ends 20 m short of the drill centre. The strip clears the drill's 3 m half-footprint, and the end is inside the 30 m last leg.
   - Pylons stand on the parallel line 8 m to the left, at s = 40, 110, 180, ... m along it, plus one within 40 m of the drill. The first is 75 m from the relay pylon at (44, 10) (link 80 m).
   - Every road node (at most 36 m apart) must lie in a standing disc or a planned one. A plan that fails is logged and dropped.
   - The bill:
     - set (a): 6 pylons (240 kg) + 368 m of road (736 kg) + a drill (80 kg) = **1,056 kg**;
     - set (c): 10 pylons (400 kg) + 635 m (1,269 kg) + a drill = **1,749 kg**.
3. **Build.**
   - One pylon at a time, each as the last stands (the `GIceBChain` pattern, `:423-426`), through `CanPlaceBuilding`.
   - Road stretches of at most 120 m a sol, only over nodes already covered by standing pylons, through `CanPlaceRoad` under the C6c Struct rule.
   - The drill once its pylon stands.
4. **Rover.** Order one CargoRover with the drill if none is deployed, aboard or on the way. The open-freight rule cannot start it, because outpost tasks do not exist without an active rover. The home plant (14, 6) and pad (10, 20) are already road-served from (8, -2).
5. **Report (A's fields).** An `outpost:` line gives:
   - the sols of the hint, the survey, the first and last pylon, the road and the drill, and the rover's order and deployment;
   - outpost tonnes freighted;
   - water made over drawn at the end;
   - per ship, the leg that capped settlers (`GetSettlerLegRoom`);
   - whether Struct held the program back, and on which sols;
   - any rival found by the extra surveys.
6. **Bar (change 11; three runs: 50% full, 50% lean, 75% full; 110 sols).** All three must hold:
   - water made > 0 at sol 110;
   - freight loaded by rovers > 0 t;
   - no ship after the first outpost load has its settlers capped by the Water leg.
   - The third check is measured per landing ship from `GetSettlerLegRoom`, not from `bSettlersShortWater`. That flag is set only while settlers are being held (`RHSimDistricts.cpp:836-841`), so it would pass trivially when none are waiting.
   - Nothing is tuned to pass. If Struct is the limit, the report says so.

## M7. Inertness and the exact battery gate (changes 1 and 3)
- **The only branches that act key on a FOUND outpost, an outpost drill or `bOutpostSource`:** the board branch and its open-task test, the hauler skip, refresh, placement, Dig, the drill buffer, era, and stages 4 and 5. The read-only paths are the hint and the advice text. No suite reveals an outpost except -icelow's spend call (below).
- **Expected `diff_battery.py` result: exactly 88 differing lines, with no mask committed:**
  - In each of the 43 pre-existing logs (D1's baseline plus 42 suites), the init line `Colony sim online. Lag ... Deposits 6. Starter Struct ...` becomes `Deposits 9.` That is 86 lines.
  - In `roads.log`, R2 prints `Plain.Num()`, which is 8 + 2 x deposits (`RHSimCommandlet.cpp:7726`, `:7744`). `R2 the 10-sol era ledger is identical with and without roads (20 lines, max delta 0.000000000)` becomes `(26 lines, ...`. That is 2 lines.
  - Any other differing line fails the gate. If a filter is used to read the diff, it is anchored on `Colony sim online`, it stays out of the repo, and R2 stays unmasked.
- **Runs.** After D1: 42 suites, 43 runs. W1 adds `farice`: **43 suites, 44 runs**. `rh_battery.sh` gets `farice` in SUITES, and its header and echo line read "43 suites (44 runs)". `farice.log` is NEW.
- **-icelow keeps every line (B's call).**
  - Directly after I8, `Debug_SpendOutpostIce()` sets, for each outpost, `bDiscovered = true` and `RemainingKg = 0`, and adds its Id to `IceLowAnnounced` directly. It broadcasts no alert and prints nothing.
  - Traced through the suite:
    - I9 gives stage 1, then 2, with `!HasUndiscoveredIce`.
    - At I10 the load rebuild (`:12041-12048`) re-marks the spent rows.
    - I11 gives stage 3, then -1, and three RUNNING LOW alerts.
    - I12 gives stage -1 and "Ration water".
  - Placed before I6, it would turn I6's -1 into 3. A's "remove the rows" variant fails I3 (`:3770-3774`) and is not used in -icelow.
- **The harness hides a real change in the shipped game [R2-7]. The commit and the design-decisions entry quote this before-and-after.**
  - The moment: Ice_A is spent, Ice_B is found and crosses its low line, and the outposts are still hidden.
  - At 992383d: the alert reads `ICE_B RUNNING LOW — about N t of ice left. The last known ice. Ration water: recycling stretches it, and the Water Plant restores it while ice remains.` (D87), and `GetIceExpansionStage` is -1 (no interrupt).
  - After W1: the alert reads `ICE_B RUNNING LOW — about N t of ice left. Survey for more ice farther out: outpost ice comes home by road and cargo rover.`, and the stage is 0 ("Find more ice", naming the hint) until every outpost is found.
- **Gate-D [R2-13]. The new player-facing text, all placeholders, all prevention-neutral:**
  - the Water Plant placement refusal;
  - the Dig refusal;
  - the far-ice alert advice;
  - the hint and "Find more ice" line;
  - the stage 4 and 5 guide steps;
  - the survey-map hint ring.
- **Approved rows W1 makes untrue or moves:**
  - D93 ("Its ice is hauled to the Water Plant like the first drill's", `RHCommandDeckGuide.cpp:498`) is false at stage 2 for an outpost. W1 gives outposts their own stage-2 sentence and leaves D93 for near ice.
  - D87 and D91 now arrive later: only once every outpost is found and low.
  - D97 never shows for an outpost.
  - These go to the director with the Gate-D list.
- **The player-visible inconsistency, stated:** Ice_B at 702 m is melted on site (`RHPlaytest.cpp:1106`), while an outpost is refused a plant because of a property the player sees only in the refusal text. Set (c) (M10) at least puts every outpost farther out than Ice_B.
- **Save v42, strict** (D1 holds v41).
  - There is no new field. Deposits are saved whole (`:11078`) and load whole (`:11527-11533`). `bOutpost` and `RegionId` are row data.
  - The bump is required: a v41 save would load without rows 7-9 and silently lose W1.
  - The version comment says "v42: outpost ice rows (no new field; the fence keeps a v41 save from loading without them)".

## M8. Tests: new suite `-farice` (fails loudly)
- F1. Pure data:
  - the three rows: mass, position, hidden, Outpost, RegionId 0;
  - the six old rows: Outpost FALSE;
  - Ids 1-6 by name, and 7-9 in order;
  - the config rows, read with a -1 sentinel;
  - each row lies within RC_S `WorkRate` of its hint centre.
- F2. Inert twin (A's graft):
  - 20 sols with the outposts hidden, against a twin made with the test-only `Debug_RemoveOutpostRows()`. It is used only here, never in -icelow.
  - Stocks, deposits Ids 1-6 only (O5), tasks and the power digest are bit-identical.
- F3. Hint and survey:
  - no hint while Ice_B is hidden; Ice_C once Ice_A and Ice_B are found;
  - the 50 m centre;
  - a scout survey on the centre reveals Ice_C, and one 61 m from the row does not;
  - surveying Ice_E first leaves the hint on Ice_C;
  - the same survey count gives the same `DiscoveredRivals`, with or without the outpost surveys.
- F4. Advice and stages:
  - with outposts hidden and every near deposit found and low: stage 0 and the far advice;
  - stages 1, 2, 4, 5, then -1, and never 3 for an outpost;
  - all found and spent: -1 and "The last known ice".
- F5. Refusals:
  - Dig on an outpost;
  - a Water Plant at 299 m (refused) and 301 m (accepted) from a found outpost;
  - before discovery, no refusal, but routing skips that plant.
- F6. Coverage: a drill outside coverage is refused; a chain at 70 m links; the drill attaches to the outpost Id.
- F7. No rover: a drill holding 5 t posts nothing.
  - No Water Plant anywhere, even with a rover: nothing goes to the Lander or a Stockpile (change 7).
- F8. Rover, no road: nothing. Rover and road:
  - one freight task with 200 < AmountKg ≤ min(1000, room);
  - a nearer plant that is not road-served is skipped, and so is one inside 300 m;
  - a plant at `Have + 200 >= 1200` is skipped;
  - the first task's AmountKg is 240 kg [R2-9];
  - **two home plants [R2-3]:** the nearer at 465 m, not road-served; the farther at 480 m, road-served. The task goes to the farther one.
- F9. Haulers:
  - one beside an unclaimed outpost task for 600 agent-seconds never claims it;
  - after a load, before any board step, a hauler does not claim it (O6);
  - a demolished edge removes the open task.
- F9b. Lane liveness [R2-4]:
  - rover A goes flat with cargo aboard, on its claimed task, away from a pad;
  - rover B gets a new task on the next board step, and deliveries resume;
  - mass is conserved, with A's cargo counted in flight.
- F9c. Buffer [R2-5]: 30 sols with an outpost drilled and no rover.
  - The drill holds 1,000 to 1,040 kg and idles.
  - `IsIceDepositLow` is false, stage 4 shows, and no RUNNING LOW alert fires.
- F9d. Final load (O8): a deposit emptied with 120 kg at the drill posts one final 120 kg freight task.
- F10. End to end, in the agent band, with Ice_A and Ice_B spent:
  - chain, road, rover, drill;
  - water made > 0 within 3 sols;
  - mass is conserved to 1e-6 kg (drilled = at drill + in flight + hoppers + melted / 0.9).
- F11. The rover wears to halt: the unclaimed task is removed and the claimed run finishes. A forced charge mid-trip returns the cargo to the drill.
- F12. Hopper: one plant fills to 1,200 kg, then the second plant is fed. A non-outpost leg to the same plant still uses `2 x Need`.
- F13. Era gate: nothing moves with no rover, `bFleetHold`, no road-served pad, no route, or a plant inside 300 m. Era dig never touches an outpost, even with `bDesignated` forced by a harness call.
- F14. Era parity: 10 sols agent against era from one save (set (c) Ice_C, chain, road, 1 rover). Three checks:
  - the H2O-equivalent ledger is within 5%;
  - the Water melted agrees within 5%, or within one load plus one hopper [R2-8];
  - the agent band's ice at the drill plus in flight is at most 480 kg at the end.
- F15. Save: a v42 round trip mid-trip with outposts found (cargo returns per SaveColony); flags re-derived; v41 refused.
- F16. Determinism: two runs give identical task Ids, amounts and delivery steps.

## M9. Risks
1. **Struct.** 1.06 t, set (a), or 1.75 t, set (c). The C6c bot funded 445 kg by about sol 38. The bar may fail on Struct, and the report must say so.
2. **Rover priority.** `TryClaimFreight` takes the nearest pickup, so home freight could starve the outpost. Today there is none.
3. **Terrain is sim-blind [R2-10].** Peaks and mesas are meshes, not height. The hero peak sits at (410, 320) m with a base radius of 231-660 m, stretched up to 1.44x by ridge noise. Mesa bases are 216-612 m, so the mesa band's inner edge is well inside 0.9 km. **This is a hard step before F1 pins positions.**
   - The implementer captures each dig site, and each planned chain and road line, in the editor (a live capture plus the read-only `RH.TerrainProbe x y`).
   - Any row under a mesh, or whose 60 m disc rises more than 3 m, is moved. That is a CSV edit, re-pinned by F1.
   - Set (c)'s Ice_C sits nearer the 0-degree and 300-degree mesa bands than set (a)'s.
   - The second reviewer's emulation (unverified) puts a crater rim on the bot's straight road near (208, 118) m. That is presentation only, and the capture checks it.
4. **Small loads.** About 240 kg a trip, four trips a sol, drill-bound [R2-9]. That is honest flow, but not "1 t a trip".
5. **The hopper deviation** (M3.3) and the haulers' asymmetry (no fallback) are both deliberate and logged.
6. **No wear in era** (M5).
7. **Row order.** A re-import that reorders rows moves Ids and the battery. F1 pins it.
8. **The extra surveys** may find a rival sooner in play. That is deterministic.

## M10. Distance: the director's call (change 10, with the coordinator's costing)
- **The question.** The ruling says "farther out". Set (a)'s first row (453 m) and A's (480 m) are both nearer than Ice_B (702 m).
- **The bot's Struct cost to open the FIRST outpost** (pylons at 70 m, 40 kg each; road at 2 kg/m; a drill at 80 kg; from the bot's standing coverage and road; a CargoRover in both cases):

| option | first row | pylons | road | total Struct | vs (a) | rover reserve |
|---|---|---|---|---|---|---|
| (a) nearer, ESE | (390, -230), 453 m | 6, 240 kg | 368 m, 736 kg | **1,056 kg** | - | 1.47 kWh |
| (b) beyond Ice_B, N, along the bot's Ice_B chain | (-280, 740), 791 m | 6 incl. a relay, 240 kg | about 910 m, 1,820 kg | **about 2,140 kg** | +1,080 kg | 2.43 kWh |
| (c) beyond Ice_B, ESE, its own chain | (624, -360), 720 m | 10, 400 kg | 635 m, 1,269 kg | **1,749 kg** | +693 kg (+66%) | 2.27 kWh |

- **(b) is worse than it looks.** A row near Ice_B within 300 m of Ice_B's own plant at (48, 690) would make that plant illegal for the bot. Moving the row out past 300 m makes the road longer than (c)'s. Its pylon saving is eaten by the extra road.
- **Recommendation: (c).** It keeps the ruling's words, uses the same bearing, code and bot plan as (a), and avoids any conflict with Ice_B's plant. It costs 0.69 t more Struct. If the 110-sol bar fails on Struct (the report names it), falling back to (a) is a two-number CSV edit, and the deviation is logged at the gate.
- Ice_D moves out to 760 m in set (c). Ice_E (806 m) is the same in both sets.
