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
