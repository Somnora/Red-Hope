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

---

# REVISION (2026-10-21)

**This is the W1 spec to build from.** It is complete on its own. It replaces sections 1-9 at the top of this file, draft B (`w1-distant-ice-draft-b.md`), and the "MERGED DESIGN AND REVISION" section above, wherever any of them differ.
- **Base:** B, as the first review chose.
- **Carried in:** the first review's changes 1-12 and its grafts from A, tagged [P1-n]. Every NEW item of the second review (3, 4, 5, 7b, 8, 9, 10, 12, 13), tagged [P2-n]. Its optional O1, O2, O5, O6, O7 and O8, adopted.
- **Declined:** P2-O3 (drop RegionId). The brief asked for a step-4 hook. It costs one row column and no saved field.
- **Anchors** are `git show 992383d:<path>`. D1 touches none of this logic; only line numbers drift. [P1-12] Build only after D1 commits, then re-anchor every reference to that commit. The table in the MERGED section above gives today's D1-tree lines.

## 0. For the director (things to tell; one needs a ruling)

**Ruling needed: where is the first new ice?** [P1-10] The ruling says "farther out". Ice_B is 702 m out.
- **Costs.** The bot's Struct cost to open the FIRST outpost, from its standing roads and coverage: pylons at 70 m and 40 kg each, road at 2 kg/m, a drill at 80 kg. A CargoRover is needed in every case.
  - **(a)** 453 m ESE at (390, -230): 6 pylons + 368 m of road + the drill = **1,056 kg**. Nearer than Ice_B.
  - **(b)** about 790 m N, along the bot's Ice_B chain: **about 2.1 t**. A row near Ice_B would forbid Ice_B's own plant at (48, 690). Moving the row past 300 m from that plant lengthens the road.
  - **(c)** 720 m ESE at (624, -360): 10 pylons + 635 m of road + the drill = **1,749 kg** (+693 kg, +66% over (a)).
- **Recommendation: (c).** It keeps the ruling's words, and every outpost is then farther out than Ice_B, so the rule reads "ice beyond Ice_B comes home raw". If the 110-sol bar fails on Struct, the bot report says so. Falling back to (a) is a CSV edit (two numbers per row).
- **Row sets.**
  - (c): Ice_C (624, -360), Ice_D (-20, -760), Ice_E (-700, 400).
  - (a): Ice_C (390, -230), Ice_D (-20, -620), Ice_E (-700, 400).
  - Ice_C 80 t, Ice_D 120 t, Ice_E 200 t in both.

**To tell: Ice_B and the outposts follow different rules.** [P2-13]
- Ice_B (702 m) may be melted on site. The guide says so (D97), and the bot does it at (48, 690) (`RHPlaytest.cpp:1106`).
- An outpost refuses any Water Plant within 300 m. The rule rides on a row flag the player cannot see.
- What the player is shown:
  - the dig site's label reads "Outpost ice deposit";
  - the refusal says why;
  - under (c), every outpost lies beyond Ice_B.
- Under (a), a 453 m deposit is refused where a 702 m one is not. That reads as arbitrary.
- Making Ice_B an outpost too would move -icelow I11, the bot and D97. It is not proposed.

**To tell: what the player now sees when Ice_B runs low.** [P2-7b] This change is real and shipped, even though -icelow's log hides it (section 9).
- **The moment:** Ice_A and Ice_B are both found and low.
- **Before (992383d):**
  - The alert reads `ICE_B RUNNING LOW — about N t of ice left. The last known ice. Ration water: recycling stretches it, and the Water Plant restores it while ice remains.` That is D87; its approved text reads "Stretch the water ...".
  - The guide shows no ice step (stage -1).
- **After W1:**
  - The alert reads `ICE_B RUNNING LOW — about N t of ice left. Survey for more ice farther out: outpost ice comes home by road and cargo rover.` (new row G4).
  - The guide shows "Find more ice" with the outpost hint (stage 0), until all five deposits are found.
  - D87 appears only when every ice row is found and low.
- The commit message and the design-decisions entry quote both texts.

**To tell: Gate-D wording.** [P2-13] W1 adds new wording, and makes some approved rows wrong for outposts.
- **New rows, all placeholders for review:**
  - G1, Dig refusal: `Outpost ice is drilled, not dug - place an Ice Drill on it.`
  - G2, placement refusal: `Outpost ice comes home raw: build the Water Plant at least 300 m away.`
  - G3, hint alert, once per row: `ICE SIGNATURES — about %d m %s of the Lander. ORDER > Survey there; the scout reports anything within 60 m.`
  - G4, RUNNING LOW advice: `Survey for more ice farther out: outpost ice comes home by road and cargo rover.`
  - G5, guide "Find more ice" body for the hint (a sibling of D90): `%s has about %.0f t of ice left. Ice signatures lie about %d m %s of the Lander: ORDER > Survey there.`
  - G6, guide stage 2 body for an outpost (replaces D93 there): `BUILD > Production > Ice Drill, placed on %s. Outpost ice comes home by road and cargo rover, never by hauler.`
  - G7, guide stage 4, "Road the outpost home": `%s's ice comes home raw. BUILD > Logistics > Road from the colony's roads to within 30 m of the drill, and to a Water Plant at least 300 m from it.`
  - G8, guide stage 5, "Bring a cargo rover": `The road is ready. Add a Cargo Rover to the next supply ship - haulers do not run outpost ice.`
  - G9, the dig-site label: `Outpost ice deposit`.
- **Approved rows W1 affects:**
  - D93: false for an outpost. G6 takes its place for outposts, and D93 stays for Ice_A and Ice_B.
  - D97: never shown for an outpost.
  - D87: now arrives only when all five deposits are found and low.
  - D91: in the shipped data it is replaced by G5 whenever an outpost hint is open, which is every time D91 used to show.
  - D86: stays while Ice_B is hidden. G4 replaces it once only outposts are hidden.
  - If the Gate-D wording lands before W1, -icelow I12's "Ration water" check moves with D87's approved text. That move belongs to the Gate-D gate, not W1.

## 1. Data
- **`FRHDepositRow`** (`RHRows.h:202-212`) gains two columns:
  - `Outpost` (bool): TRUE on the three new rows, FALSE on the six old ones.
  - `RegionId` (int32): 0 everywhere.
  - This is a struct change, so the director compiles, the CSV is re-imported and the DT live-synced.
- **`FRHDepositState`** (`RHSimTypes.h:108`) gains `bOutpost` and `RegionId`.
  - Both are set at init (`RHSimWorldSubsystem.cpp:392-406`) and re-derived by RowName at load, beside `InitialKg` (`:11534-11549`).
  - Neither is saved.
- **`FRHTask`** gains a runtime `bOutpostSource` [P2-O1]. It is true when `From` is a drill attached to an outpost deposit. It is set at post and re-derived for every task after a load. It is not saved (tasks are written field by field, `:10880-10885`).
- **Three hidden rows** (`SurfaceVisible` FALSE, `SliceActive` TRUE) are appended after Ice_B, using the section 0 set. Ids 1-6 are unchanged; the new rows are 7, 8 and 9. Rows are appended only (`ForEachDeposit` walks row-map order, `RHDefinitionsSubsystem.cpp:338-352`).
- **Authored, not generated.**
  - The sim may not read `RHMarsTerrain` (the module rule), so a generator could not avoid scenery.
  - A seed would be a new saved, determinism-bearing number.
  - Row order is the hint order.

## 2. Finding them [P1-8]
- **`CompleteSurvey` is unchanged** (`:10672-10710`). A scout reveals every hidden deposit within its `WorkRate` (RC_S: 60 m). There is no new reveal path and no new roll.
- **`GetOutpostHint(FVector& OutCentreCm) const`** returns the lowest-Id undiscovered outpost, but only once every non-outpost Ice row is discovered. In play that is when Ice_B is found, about sol 45.
  - Its centre is the row rounded to `OutpostHintGridCm` (50 m), half away from zero, in integer cm.
  - The worst offset is 35.4 m, which is under 60 m. The centres are, set (c): (600, -350), (0, -750), (-700, 400); set (a): (400, -250), (0, -600), (-700, 400).
- **The rival roll is untouched** (`:10712-10746`). One completed survey makes one roll, seeded by `SurveyHistory.Num()`. Extra surveys only move which survey finds a rival, deterministically.
- **The hint alert (G3)** fires once when a row's hint opens.
  - Its latch is runtime. At load it is rebuilt as "every row whose hint is already open, or that is found", like the ice-low latch (`:12041-12048`). It never re-fires after a load.
  - The survey map draws the hint as a dashed 60 m ring.
- **The RUNNING LOW advice** (`:1540-1547`) reads G4 when every undiscovered Ice row is an outpost. "The last known ice" (D87) appears only when no Ice row is undiscovered.

## 3. Working them: the outpost rules
**R1. No melting near an outpost (placement).**
- `CanPlaceBuildingSized` (`:6545`) refuses, on level 0, any Def whose slice recipe consumes Ice, within `OutpostMeltMinCm` (300 m) of a FOUND outpost, with reason G2. Today only the WaterPlant has such a recipe.
- Undiscovered rows are not checked, so the refusal leaks nothing.
- Routing (R3) is the guarantee. A plant built before the discovery is never a destination.

**R2. No digging, in both bands.** [P1-2]
- The `Dig` verb (`:6963-6984`) refuses an outpost with reason G1. The player issues Dig by clicking any deposit (`RHPlayerController.cpp:1488-1494`), so the refusal is needed.
- As defence in depth, `TryClaimDig` (`:10295-10320`), the board's pile leg (`:933-1015`) and `EraLogistics`' dig loop (`:766-822`) skip `bOutpost`.

**R3. The destination helper: freight first, nearest of the plants that pass.** [P2-3]
- The signature is `FindOutpostDestination(const FRHBuildingInstance& Drill, bool bIgnoreRoom, int32& OutId, double& OutRoomKg) const`.
- **Candidates:** finished level-0 buildings with an Ice-consuming recipe that stand at least `OutpostMeltMinCm` from the drill's deposit. Unless `bIgnoreRoom`, they must also hold `Have + 200 < OutpostHopperKg`.
- **Order:** sorted by integer straight cm from the drill, ties to the lower Id.
- **Walk:** test candidates in that order with `IsFreightRoute(drill, candidate)` (`RHSimRovers.cpp:83-97`). That test covers road-served at both ends, connectivity and `FreightMinCm`. Plan at most `RoverClaimPlans` (4) routes per call. The first candidate that passes wins.
- So an unserved plant that happens to be nearer never blocks a served one.
- **There is never a store.** `OutRoomKg = OutpostHopperKg - Have`.

**R4. The outpost branch of the output leg; never a store.** [P1-6, P1-7, P2-9, P2-O8]
- **Where:** the first test inside `for (const auto& Out : B.OutputKg)` (`:1017`). When `B` is an outpost drill and `Out.Key` is Ice, run this branch and `continue`. The generic branch, including the `!bAnyConsumer` store overflow (`:1072-1082`), never sees outpost ice.
- **Post** when ALL of these hold:
  - `ActiveRovers > 0`;
  - there is no LIVE task from this drill (R7);
  - R3 returns a plant;
  - `Kg = min(Out, RC_R.Cargo_kg, Room) > 200`.
- **The task:** `AmountKg = Kg`, `bFreight = true`, `bOutpostSource = true`. `SetHaulAmount` is not used.
- **Final remnant (O8):** once the source deposit's `RemainingKg == 0`, a last task of `Out >= HaulLoadMinKg` (25 kg) is allowed under the same conditions, so no ice is stranded at an empty drill.
- **Real load size** [P2-9].
  - DrillIce credits 40 kg at the end of each 1 h (50 s) batch (`:1310-1316`). So a task posts on the first step with more than 200 kg at the drill, which is 240 kg.
  - `HaulLoad` takes `min(cap, AmountKg)` (`:10353-10360`).
  - During a round trip the drill adds about 130 kg at set (a), or about 180 kg at set (c), which is still under 240. So **every trip carries about 240 kg, about four trips a sol per drill.**
  - A rover is busy about 640 s (a) to 900 s (c) of each 1,200 s sol per outpost drill. That is about one rover per outpost drill.
  - There is no "1 t a trip". The ruling needs no throughput argument.
- **The hopper deviation from C6 R5, logged.** A plant takes outpost loads up to 1,200 kg, and plants fill one after another. Water pools, so this is harmless. Every non-outpost leg keeps `2 x Need` exactly.

**R5. Haulers never take outpost ice.** [P2-O1, P2-O6]
- `TryClaimHaul` (`:10322-10345`) skips `T.bOutpostSource`, whatever `bFreight` says. That covers the moment after a load and before the first board step. There is no 300 s fallback.
- **Corrected energy** [P1-9]: a hauler uses about 384 Wh on a 480 m round trip, or about 576 Wh at 720 m, against 1,125 Wh above its seek fraction. That is 3 trips, or 2, per charge.
- Two haulers would still match a drill (0.8 kg/s). So the rule is by source. A rule by distance would move battery hauls.

**R6. Refresh deletes only outpost tasks.** [P2-12]
- In `RefreshFreightFlags` (`RHSimRovers.cpp:119-142`), an UNCLAIMED task with `bOutpostSource` is kept (`bFreight = true`) only while `ActiveRovers > 0` and `IsFreightRoute` still passes. Otherwise it is DELETED, and R4 re-posts it later.
- Deleting loses no ice: a task holds no mass.
- **Non-outpost tasks keep `:135` exactly,** so -traffic X4-X7 and X15 are unaffected.
- The earlier claim that W1 "closes a latent C6 trap" is withdrawn. Outpost tasks are never at 200 kg or less, and the C6 edge is a liveness item for a later gate.

**R7. A halted rover never holds the lane.** [P2-4]
- `HasOpenTask` counts claimed tasks (`:10941-10953`), and a halted or flat rover keeps its claim without ticking (`RHRobotBrainProcessor.cpp:103-110`).
- **New const accessor:** `URHAgentSubsystem::IsRobotLive(FMassEntityHandle, float WearHalt)`. It returns the brain's own test: the entity is valid, `Wear < WearHalt`, and `ChargeWh > 0` or the robot is docked (Charge task, phase 1).
- **New check for R4:** `HasLiveOutpostTask(DrillId)` is true if any Haul task from that drill is unclaimed, or is claimed by a live robot. It replaces `HasOpenTask` in R4 only.
- **The dead claim stays.** That rover keeps its cargo, counted as in flight. It delivers if an RC-M repair revives it, which works for wear only. A flat rover off a pad is never revived (the existing rule), so its load stays aboard. That is the one way outpost ice can be stranded, and it is logged.
- A second rover gets the new task.

**R8. Cap the far drill's buffer.** [P2-5]
- In `StepProduction`'s extractor check (`InputsOk`, `:1438-1452`), an outpost drill starts no batch while `OutputKg[Ice] >= OutpostDrillBufferKg` (1,000 kg). A batch already running finishes, so the buffer is at most 1,040 kg.
- `StepProduction` runs in both bands (`StepSim :540`, `EraStep :711`).
- **Effects:**
  - with no rover the deposit is not drained into the drill;
  - RUNNING LOW, stage 4 and D87 stay truthful;
  - an idle drill draws 15 W, not 250 W.

**R9. Guide stages** (`GetIceExpansionStage`, `:1594-1626`).
- The existing loop skips outposts, so every value -icelow pins is kept.
- If that loop resolves (-1), or no non-outpost deposit is healthy, the found healthy outposts are read in Id order:
  - stage 1 (carry the grid) if not reached;
  - stage 2 (drill, G6) if not drilled;
  - stage 4 (road, G7) if `FindOutpostDestination(.., bIgnoreRoom = true, ..)` finds nothing;
  - stage 5 (rover, G8) if no live rover exists and none waits in `ImportStock["RC_R"]` (adopting P2-O2);
  - otherwise -1.
- Stage 3 (D97) is never offered for an outpost.

## 4. Power and coverage [P1-5]
- **No new rule.** Power is one pool, and coverage is placement only (`:10128-10145`). A drill must be in coverage (`:6746`). A pylon links within 80 m (`:6714-6729`) and covers 40 m.
- Every road node must be covered, and edges are at most 40 m (`RHSimRoads.cpp:213-221`). So the road needs the same chain the drill needs. An outpost pylon or a self-powered drill would save nothing.
- **Geometry:**
  - Pylons stand **at most 70 m apart**, which leaves a ±19.4 m lens at the midpoint.
  - They stand **6-10 m to one side of the road line**. The 3 m strip half-width plus the pylon's 1 m half-extent (`RHSimRoads.cpp:224-244`) is cleared at 4 m or more.
  - Every planned road node is checked against the standing and planned discs before any pylon is ordered.

## 5. Rovers and energy [P1-9]
- **The road-served rule works at any distance.** `PlanRoute` is integer, with int64 costs and K=4 entries (`RHRoads.h:64`, `RHRoads.cpp:428-580`). One order lays at most 64 pieces (about 2.3 km, `RHRoads.h:111`).
- **Rover reserve.** `DriveWh(3 x straight to the pad)` from the pad at (10, 20), against a 6 kWh battery:
  - 1.47 kWh at 453 m;
  - 2.27 kWh at 720 m;
  - 2.52 kWh at 806 m.
- **A 240 kg round trip** uses about 0.54 kWh at (a), or about 0.75 kWh at (c). The home pad gives 500 W.
- The C6 R7 reserve proof holds unchanged.

## 6. Era: as strict as the agent band [P1-4]
- **The outpost branch comes first in `EraLogistics`' output loop** (`:824`). For an outpost drill's Ice it moves `Kg` (the R4 rule, including the remnant) from the drill to R3's plant, and only if ALL of these hold:
  - `!bFleetHold`;
  - `CountActiveRobots(Rover, WearHaltThreshold) > 0` (`RHAgentSubsystem.cpp:307-327`, a read-only fragment read);
  - a road-served rover pad exists (`FindNearestRoverPad` succeeds), since an agent-band rover below its seek fraction with no such pad claims nothing (`RHSimRovers.cpp:160-169`);
  - R3 returns the nearest plant that passes the freight test, ties to the lower Id.
- **Then it `continue`s, and never goes to a store.** The generic era loop (`:824-879`), including its store fallback at `:862-873`, never sees outpost ice.
- The era dig loop skips outposts (R2). The R8 cap applies through `StepProduction`.
- **Known limit:** `EraStep` applies no wear (`:701-725`), so a long era stint is optimistic about rover availability. It falls outside the 10-sol bar.

## 7. The playtest bot (`RHPlaytest.cpp`; default on with roads; `RH_PLAYTEST_OUTPOST=0` returns the C6c bot line for line, proved by diff)
1. **Survey** the hint centre as soon as `GetOutpostHint` answers, once per row. A survey costs nothing, so there is no Struct gate (P2-O7). This replaces the "no third ice" branch (`:632-639`).
2. **Plan once per row.**
   - The road line runs from built node (76, -2) to 20 m short of the drill centre, ending inside the 30 m last leg and clear of the drill's 3 m half-footprint.
   - Pylons stand on the parallel line 8 m to the left, at s = 40, 110, 180, ... m, plus one within 40 m of the drill. For (c) the first is at about (114, -17), 75 m from the relay at (44, 10).
   - Every road node, at most 36 m apart, must lie in a standing or planned disc. If not, the plan is logged and dropped.
   - The bill: (a) **1,056 kg**, (c) **1,749 kg**.
3. **Build.**
   - Pylons one at a time, each when the previous one stands, through `CanPlaceBuilding` (the `GIceBChain` pattern, `:423-426`).
   - Road stretches of at most 120 m a sol, only over nodes covered by standing pylons, through `CanPlaceRoad` under the C6c Struct rule.
   - The drill once its pylon stands.
4. **Rover.** Order one CargoRover with the drill if none is deployed, aboard or on the way. The open-freight count (`:442`) cannot start it, because outpost tasks need an active rover. The home plant (14, 6) and pad (10, 20) are already road-served from (8, -2).
5. **Report** (A's fields). An `outpost:` line gives:
   - the sols of the hint, the survey, the first and last pylon, the last road stretch, the drill, the rover's order and its deployment;
   - the number of outpost loads, their mean kg, and total tonnes;
   - water made over drawn at the end.
   - Per ship, it prints `GetSettlerLegRoom(Water).ProductionRoom` against `Housed = min(SettlersWanted, SettlerRoom)`.
   - It also says whether Struct held the program back (sols) and names any rival found by the extra surveys.
6. **Bar** [P1-11]: three runs (50% full, 50% lean, 75% full), 110 sols. All of these must hold:
   - water made > 0 at sol 110;
   - outpost tonnes > 0;
   - for every ship after the first outpost load, `ProductionRoom(Water) >= Housed`.
   - `bSettlersShortWater` is not used, because it is set only while settlers are held (`RHSimDistricts.cpp:836-841`).
   - Nothing is tuned to pass.

## 8. Scenery check [P2-10]
- The sim is blind to the scenery meshes.
  - A hero peak's base radius is 231-660 m, stretched up to 1.44x by ridge noise (`RHMarsTerrain.cpp:198-200`, `:370-377`, `:405-406`).
  - Mesa bases are 216-612 m (`:400-401`).
  - Set (c)'s Ice_C (-30 degrees, 720 m) and Ice_E (806 m) are the likely victims.
- **Implementer task, before F1 pins positions:** capture each dig site, and each chain and road line, in the editor through the Unreal MCP. Move any row whose 60 m disc lies under a mesh or rises more than 3 m. Each move is a CSV edit, re-checked by F1 and the hint-offset rule.
- The second review's Python emulation is a reason to look, not a finding.
- The `DepositCm[]` corridor list (`RHMarsTerrain.cpp:437-445`) is not extended, because rovers drive roads.

## 9. Inertness, the exact battery gate, save [P1-1, P1-3, P1-12]
- **Every acting branch keys on a found outpost or an outpost drill:** R1-R9 and the era branch. The read-only paths are the hint and the advice. No suite reveals an outpost except -icelow's spend call.
- **The gate:** `bash scripts/sim/rh_battery.sh 10` with 0 not green. Then `diff_battery.py` against D1's committed battery logs gives **exactly 88 differing lines**, and these lines only:
  - **86 lines:** in each of the 43 pre-existing logs (the baseline plus 42 suites after D1), the init line `Colony sim online. Lag ... Deposits 6. Starter Struct ...` (`RHSimWorldSubsystem.cpp:424-425`) becomes `Deposits 9.` The reason is three new rows.
  - **2 lines:** in `roads.log`, `R2 the 10-sol era ledger is identical with and without roads (20 lines, ...` becomes `(26 lines, ...`. `Plain.Num()` is 8 + 2 x deposits (`RHSimCommandlet.cpp:7726`, `:7744`).
  - **Nothing else moves.** The save "(N bytes" is already masked. Any other differing line fails the gate.
  - **No mask is committed.** A reading filter, if used, is anchored on `Colony sim online`, stays out of the repo, and never hides R2.
- **Runs.** After D1: 42 suites, 43 runs. W1 adds `farice`: **43 suites, 44 runs**. `rh_battery.sh` adds `farice` to SUITES and its header and echo say "43 suites (44 runs)". `farice.log` is NEW.
- **-icelow keeps every line** [P1-3].
  - Directly after I8, `Debug_SpendOutpostIce()` sets each outpost `bDiscovered = true` and `RemainingKg = 0`, and adds its Id to `IceLowAnnounced` directly. It broadcasts and prints nothing.
  - Traced through the suite:
    - I9 gives stage 1 then 2, with `!HasUndiscoveredIce`.
    - At I10 the rebuild (`:12041-12048`) re-marks the spent rows.
    - I11 gives stage 3 then -1, and 3 alerts.
    - I12 gives -1 and "Ration water".
  - No hint opens, because the outposts are found before Ice_B.
  - Placed before I6, the call would turn I6's -1 into 3. Removing the rows instead fails I3 (`RHSimCommandlet.cpp:3768-3778`).
  - **The behaviour this hides is disclosed in section 0** [P2-7b], and -farice F4 pins it.
- **Save v42, strict.** It is required [P1-12].
  - Deposits load whole from the save (`:11527-11533`), and the version must match exactly (`:11455`). Without the bump, a v41 save would load without rows 7-9, so no hint would ever open.
  - There is no new field: `bOutpost`, `RegionId`, `bOutpostSource`, the latches and the hint are all derived.
  - The version comment reads: "v42: outpost ice rows (no new field; the fence keeps a v41 save from loading without them)".

## 10. Tests: new suite `-farice` (fails loudly)
- **F1. Data:**
  - the three rows (mass, position, hidden, Outpost, RegionId 0);
  - the old six rows with Outpost FALSE;
  - Ids 1-6 by name and 7-9 in order;
  - config rows read with a -1 sentinel;
  - each row within RC_S `WorkRate` of its 50 m hint centre.
- **F2. Inert twin.**
  - 20 sols with the outposts hidden, against a twin built with the test-only `Debug_RemoveOutpostRows()`. That call is used here only, never in -icelow.
  - The digest compares stocks, deposits 1-6 (P2-O5), tasks and power: bit-identical.
- **F3. Hint and survey:**
  - no hint while Ice_B is hidden;
  - the Ice_C hint once Ice_A and Ice_B are found, at the 50 m centre;
  - a scout survey there reveals the row, and one 61 m off does not;
  - surveying Ice_E first leaves the hint on Ice_C;
  - G3 fires once and does not re-fire after a load;
  - the same survey count gives the same `DiscoveredRivals`.
- **F4. Advice and stages (the disclosed change):**
  - near ice found and low with outposts hidden: stage 0 and G4, not D87;
  - outpost stages 1, 2, 4, 5, then -1, and never 3;
  - all found and spent: -1 and D87.
- **F5. Refusals:**
  - Dig on an outpost: refused with G1;
  - a Water Plant at 299 m (refused, G2) and at 301 m (accepted);
  - no refusal before discovery, though R3 skips that plant;
  - era dig and `TryClaimDig` skip an outpost whose `bDesignated` is forced by a harness call.
- **F6. Coverage:** a drill outside coverage is refused; a chain at 70 m links; the drill attaches to the outpost Id.
- **F7. No store, ever:**
  - no rover: 5 t at the drill posts nothing;
  - no Water Plant anywhere, or the only one still a site: nothing goes to the Lander or a Stockpile, in either band.
- **F8. Destination** [P2-3]:
  - a nearer plant at 465 m that is not road-served is skipped, and a served one at 480 m is chosen;
  - a plant inside 300 m is skipped;
  - a plant with `Have + 200 >= 1200` is skipped;
  - the plan cap of 4 holds.
- **F9. Legs:**
  - a rover but no road: no task;
  - road and rover: `AmountKg = 240` at a fresh drill, with `bFreight` and `bOutpostSource` set;
  - a hauler never claims it in 600 agent-seconds, nor right after a load (P2-O6);
  - a demolished edge deletes the unclaimed task;
  - a non-outpost freight task through `RefreshFreightFlags` behaves exactly as in C6.
- **F10. Halted rover** [P2-4]: rover A halts by wear with 240 kg aboard. Rover B gets a new task, and deliveries resume. Mass is conserved, with A's cargo counted in flight. After an RC-M repair, A delivers.
- **F11. Buffer** [P2-5]: 30 sols with no rover. The drill holds at most 1,040 kg, the deposit is not low, there is no RUNNING LOW, stage 5 shows, and the drill draws idle power.
- **F12. End to end, agent band, Ice_A and Ice_B spent:**
  - chain, road, rover, drill;
  - water made > 0 within 3 sols;
  - mass conserved to 1e-6 kg (drilled = at drill + in flight + hoppers + melted / 0.9);
  - the final remnant empties the drill.
- **F13. Hopper:** one plant fills to 1,200 kg, then a second plant is fed. A non-outpost leg to the same plant still uses `2 x Need`.
- **F14. Era gate:** nothing moves with no rover, with `bFleetHold`, with no road-served pad, with no route, or when the only plant is inside 300 m. With everything in place, loads move through R3.
- **F15. Era parity** [P1-4c, P2-8]: 10 sols agent against era from one save (set (c) Ice_C, chain, road, one rover). All three checks must pass:
  - the H2O ledger (0.9 x ice at drills, in hoppers and in flight, plus the Water stock) is within 5%;
  - the water MELTED (production ledger, `NoteProduced`) is within 5%, or within one load plus one hopper (0.9 x 1,440 kg);
  - at the end, the agent band's ice at the drill plus in flight is at most 2 x the largest load posted.
- **F16. Save and determinism:**
  - a v42 round trip mid-trip with outposts found (cargo returns per SaveColony);
  - flags re-derived;
  - v41 refused;
  - two runs give identical task Ids, amounts and delivery steps.

## 11. Placeholder numbers (docs/data, live-synced; every note ends "Placeholder, director review")
- **RH_Deposits:** the `Outpost` and `RegionId` columns; Ice_C 80000, Ice_D 120000 and Ice_E 200000 at the positions of the set the director picks.
- **RH_Config:**
  - `OutpostMeltMinCm` 30000;
  - `OutpostHopperKg` 1200;
  - `OutpostDrillBufferKg` 1000;
  - `OutpostHintGridCm` 5000.
- **Unchanged:** FreightMinCm, RoverLastLegCm, RoverClaimPlans (reused by R3), the hauler fallback, the road cost, and every robot and building row.

## 12. Risks
1. **Struct.** 1.06 t for (a), 1.75 t for (c). The C6c bot funded 445 kg by about sol 38. The bar may fail on Struct, and the report must name it.
2. **Rover priority.** `TryClaimFreight` takes the nearest pickup, so home freight could delay outpost loads. There is none today.
3. **A flat rover strands its load** (R7). This is the existing no-revival rule, logged.
4. **Scenery** (section 8): positions may move after the editor capture.
5. **The hopper deviation** (R4) and **no hauler fallback** (R5) are deliberate and logged.
6. **No wear in era** (section 6).
7. **Row order.** A re-import that reorders rows moves Ids and the battery. F1 pins it.
8. **The extra surveys** may find a rival sooner in play. That is deterministic, and the bot reports it.

## 13. Traceability
| item | where |
|---|---|
| P1-1 battery lines, no mask, 44 runs | 9 |
| P1-2 Dig | R2, F5 |
| P1-3 -icelow call | 9 |
| P1-4 era gate (a-d) | 6, F14, F15 |
| P1-5 chain geometry | 4, 7.2 |
| P1-6 far hopper | R4, F13 |
| P1-7 never a store | R4, 6, F7 |
| P1-8 hint | 2, F3 |
| P1-9 energy | R5, 5 |
| P1-10 distance | 0 |
| P1-11 bot bar | 7.6 |
| P1-12 sequencing, v42 | header, 9 |
| P2-3 destination | R3, F8 |
| P2-4 halted rover | R7, F10 |
| P2-5 buffer cap | R8, F11 |
| P2-7b disclosure | 0, F4 |
| P2-8 water melted | F15 |
| P2-9 load size | R4 |
| P2-10 scenery | 8 |
| P2-12 C6 claim, refresh gate | R6, F9 |
| P2-13 Gate-D, Ice_B vs Ice_C | 0 |
