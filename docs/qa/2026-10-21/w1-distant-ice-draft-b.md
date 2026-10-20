# W1, distant ice: design (draft for adversarial review)

Repo: /Volumes/Unreal/red_hope/red_hope (UE 5.8). Design only. No code is written. Anchors were read on main @ 992383d. D1 is editing `RHSimWorldSubsystem.cpp/.h` in this tree, so those line numbers will drift by D1's insertions.
Ruling (director, 2026-10-21, "water a"): after Ice_A and Ice_B, water comes from more ice farther out, found by surveying, too far for haulers, brought home by road and cargo rover.
Evidence: 58c8cae, `docs/design-decisions.md:619` ("Playtest bot plays the living city"). Water production is 0 by sol 110. Settlers are held on water (C5e). No freight ever moved.

## Facts the design rests on
- **Deposits** load from DT_Deposits in row-map order. `Id = Deposits.Num() + 1`, and `bDiscovered = SurfaceVisible` (`RHSimWorldSubsystem.cpp:392-406`; `ForEachDeposit` filters SliceActive, `RHDefinitionsSubsystem.cpp:338-352`). So appended rows get new Ids and leave Ids 1-6 alone. Every run logs `Deposits %d` at init (`:424`).
- **Survey** is geometric, with no roll: every hidden deposit within the scout's `WorkRate` (RC_S: 60 m) of the point is revealed (`:10672-10710`). The only roll is the rival one: `MaybeDiscoverSettlement(SurveyHistory.Num())`, 50%, seeded by the survey count, picking the first dormant rival by name (`:10712-10746`). No battery suite issues a Survey verb. -espionage calls the roll directly (`RHSimCommandlet.cpp:1455`).
- **Ice state.** `HasUndiscoveredIce` (`:1557`) feeds the alert advice (`:1525-1550`) and `GetIceExpansionStage` (`:1594-1626`, stages 0 find, 1 grid, 2 drill, 3 melt beside it). The guide reads those stages (`RHCommandDeckGuide.cpp:466-515`). **-icelow pins the two-ice world:** I9 asserts `!HasUndiscoveredIce()` after Ice_B is found, and I12 asserts the dead end ("Ration water", stage -1) (`RHSimCommandlet.cpp:3836-3932`).
- **Coverage is placement only.** Power is one pool; there is no grid topology (`StepPower`, `:6379`). A drill must sit inside a coverage disc (`:6746`). A pylon must link to a node within 80 m (`:6714-6729`) and covers 40 m. Every road node must be in coverage (`RHSimRoads.cpp:213-222`), and edges are at most 40 m (`RoadMaxEdgeCm`). **So any road to an outpost needs the pylon chain under it, the same chain the drill needs.**
- **Drills and plants.** A drill attaches to a DISCOVERED deposit within 12 m (`:8279-8292`). Its ice piles up in its OutputKg with no cap (`:1310-1316`). Water is a fluid: a plant's melt joins the pool and the ledger wherever the plant stands (`:1318-1324`). **So a Water Plant beside a far drill teleports the water home, and freight never happens.** That is today's guide stage 3 and the bot's Ice_B play (`RHPlaytest.cpp:1101-1109`).
- **Ice is never freight today.** Drill ice goes to the nearest consumer with `Have < 2 x Need` (`:1017-1070`). The rover amount is capped by the room, `2 x 50 - Have <= 100 kg` (`:922-930`, C6 R5). Freight needs more than 200 kg (`RHSimRovers.cpp:99-117`, O4). With no consumer room, ice waits at the drill and never goes to a store (`:1072-1078`).
- **Haulers.** They take freight after 300 agent-band seconds (`:10333-10334`). At 4 m/s with 200 kg and 20 s of handling, a hauler at 460 m one-way moves 0.8 kg/s, which is one drill (40 kg/h = 960 kg/sol). Two haulers cover 800 m. **Inside the map, distance alone never makes haulers bad.**
- **Era.** `EraLogistics` teleports drill output to the FIRST consumer, or else to a store (`:731-880`). It needs no road and no rover.
- **Map.** The sim plane is z=0. Relief starts at 150 m and opens by 350 m (`RHMarsTerrain.cpp:303-345`). Craters lie 190-420 m out. Mesas sit at 900-1700 m on bearings near 0/60/.../300 degrees (from +X; +Y is north), and the hero massif is NE at 38 degrees, 520 m (`:380-406`). The ground pad is a 5 km plane, and terrain runs to 4.6 km.

## 1. Where the new ice is
- **Three authored rows, hidden, appended:**

| row | mass | at (m) | from Lander | bearing gap |
|---|---|---|---|---|
| Ice_C | 80 t | (390, -230) | 453 m | ESE, between mesas at 0 and 300 |
| Ice_D | 120 t | (-20, -620) | 620 m | S, beyond Ore_B (a south road serves both) |
| Ice_E | 200 t | (-700, 400) | 806 m | WNW, between mesas at 120 and 180 |

- **Size and distance.** Bigger rows sit farther out. Each is 5.6-10x FreightMinCm (80 m), and all are inside the terrain and the pad.
- **Cost to open Ice_C:** about 5-6 pylons (240 kg), about 370 m of road from the bot's east leg at (76,-2) (740 kg) and a drill (80 kg). That is about 1.05 t of Struct, plus a CargoRover (3,500 kg of manifest).
- **Rover range holds.** At 806 m the reserve is DriveWh(3 x 806 m) = 2.5 kWh of 6 kWh, so about four round trips a charge. A road-served pad at the outpost is optional.
- **Authored, not generated.** The guide must name places (the Ice_B precedent, I2 pins its position). The sim cannot sample terrain to avoid mountains. A seed would be a new saved field and a new determinism surface. Rows are appended only, so Ids are 7, 8 and 9 in CSV order, and the pure-data check pins this.
- **New columns on RH_Deposits (FRHDepositRow):**
  - `Outpost` (bool): TRUE for the three new rows, FALSE for the six old ones.
  - `RegionId` (int32, all 0).
  - Both are copied into FRHDepositState at init and re-derived by RowName on load, like `InitialKg` (`:11535-11549`). They are never saved. Step 4 assigns rows to regions through RegionId. In region 0 nothing changes.

## 2. How they are found
- **Reveals stay geometric** (a survey within 60 m), through `CompleteSurvey` unchanged. There is no new reveal path. A path that bypassed CompleteSurvey would skip, or double, the rival roll.
- **The order is in the hints, not in the sim.** The guide's "Find more ice" step and the alert name the NEXT hidden outpost in row order, with its approximate place ("about 450 m east-south-east of the Lander"), as the Ice_B copy does today. A player may still find them in any order.
- **The rival roll is untouched.** One completed survey is one roll, whatever it finds. Extra surveys move which survey finds a rival, deterministically. That is ordinary scouting.
- **Advice.** When every undiscovered ice row is an outpost, the RUNNING LOW advice becomes "Survey for more ice farther out: outpost ice comes home by road and cargo rover." (Gate-D-neutral placeholder; it keeps the "Survey for more ice" prefix that -icelow I7 checks.) "The last known ice" now appears only when every row, outposts included, is found and low.

## 3. How they are worked
- **The split: raw ice travels home.** Water is a fluid that pools anywhere, so melting at the outpost would make freight optional. Two rules close that:
  - **No melt near an outpost (placement).** A building with an Ice-consuming slice recipe (today only the Water Plant) is refused within `OutpostMeltMinCm` (300 m) of a DISCOVERED outpost deposit. The reason is "Outpost ice comes home raw: build the Water Plant at least 300 m away". Undiscovered rows are not checked, so the refusal leaks nothing.
  - **The guarantee (routing).** An outpost source never delivers to a consumer within 300 m of its own deposit, so a plant built before the discovery does not reopen the loophole.
  - **Dig is refused on an outpost deposit** ("Outpost ice is drilled, not dug"), at `:6963-6984`. This removes the pile leg from both bands.
- **Power and coverage: the existing pylon chain.** It is required anyway for the road's nodes. A self-powered drill or an outpost-only pylon would add rules and save state and still not let a road be laid.
- **The outpost freight leg.** In the output loop of StepTaskBoard (`:1017`), a source attached to an outpost deposit takes its own branch and `continue`s past the generic one:
  - Destination: the nearest Ice consumer that has `Have < 2 x Need` (the existing predicate), stands 300 m or more from the source deposit, and has a road-served approach point. Ties go to the lower Id. There is never a store.
  - Post only when `ActiveRovers > 0`, the drill holds at least `OutpostLoadKg` (1,000 kg), and `IsFreightRoute` passes (`RHSimRovers.cpp:83-97`). Then post `AmountKg = OutpostLoadKg` with `bFreight = true`.
  - Otherwise post nothing, and the ice waits at the drill.
  - **A deliberate deviation from R5:** the plant can be overfilled by one load (up to 1,100 kg against a 100 kg hopper rule). It is bounded by one load per plant, and a plant takes no new load until it is back under two batches. R5's starving second Forge cannot happen, because the next load goes to the nearest plant that has room.
- **Haulers never claim an outpost task.** `TryClaimHaul` (`:10322`) skips tasks whose From is an outpost site, with no 300 s fallback. Fallback haulers would match a drill's rate at 460 m, which would make roads optional. A rule by source is chosen over a rule by distance because a distance rule could move battery hauls and strand ordinary freight when rovers are out.
- **Re-derivation.** `RefreshFreightFlags` (`RHSimRovers.cpp:119-142`) REMOVES an unclaimed outpost task that fails the freight test (no active rover, or a road demolished). The board re-posts it when it passes again. A claimed run finishes as in C6 (R3 returns cargo on a forced charge).
- **Rovers.** The road-served rule is distance-free: PlanRoute is integer, uses int64 costs and K=4 entries (`RHRoads.h:64`, `RHRoads.cpp:428-580`). One order lays up to 64 pieces (2.3 km). At Ice_C a rover does about 1 t per 160 s, against a drill's 0.8 kg/s. One rover can serve three outposts.

## 4. The playtest bot (`RHPlaytest.cpp`, default on, `RH_PLAYTEST_OUTPOST=0` returns the C6c bot line for line)
- When Ice_A is low, survey Ice_B as today. Then survey at the guide's hint for Ice_C. Ice_C's chain runs from the relay pylon at (44,10) in links of about 75 m, ordered one at a time like `GIceBChain` (`:423-426`).
- Drill Ice_C. Lay road legs from (76,-2) to about 20 m from the drill through `CanPlaceRoad` (the C6c rule: spare Struct beyond the quota bill plus the leg plus 30 kg). The home plant at (14,6) is already within 30 m of the spine node at (8,-2).
- Order one CargoRover when the outpost drill stands and its road is ordered. This bypasses the freight-task count (`:442`), because outpost tasks only appear once a rover is active.
- **Report** adds an `outpost:` line: rows found, the sol each opened, pylons, road metres, Struct, outpost tonnes delivered, and the sol of the first load. The `at the end:` water line gains the settler leg that bound each ship.
- **Bar (110 sols, 50% and 75%):** water made > 0 at sol 110; freight loaded by rovers > 0 t; no ship's settlers capped by the Water leg after the first outpost load. If Struct cannot fund the 1.05 t by about sol 70, the report says so (Risk 1). The numbers are not tuned to pass.

## 5. Inertness
- **Every new branch keys on an outpost row,** and no suite ever reveals one. That covers the board branch, the hauler skip, the refresh removal, the placement refusal, the Dig refusal, the era branch and stages 4 and 5. Hidden rows are skipped by every deposit loop (piles 0, not designated, not low).
- **Two lines differ in every log, by data:**
  - `Deposits 6` becomes `Deposits 9` (`:424`). W1 adds the mask `Deposits \d+` to `scripts/sim/diff_battery.py` (beside `(N bytes`, `:10`). The commit says so.
  - The save byte counts, already masked.
  - The -roads R2 ledger includes every deposit (`RHSimCommandlet.cpp:7726`). It has equal length in both legs, and it prints no numbers.
- **-icelow is the suite that pins the ice set. Decision: keep its lines.**
  - After I8, one silent harness call, `Debug_SpendOutpostIce()`, reveals each outpost, sets it to 0 kg and latches it in `IceLowAnnounced` (no alert).
  - Placing it after I8 matters. Before I6 the spent outposts would count as "low" and turn I6's stage -1 into 3.
  - I9 to I12 then run in the true W1 dead end: everything found and spent, so `HasUndiscoveredIce` is false and the advice is "Ration water". The load-time latch rebuild (`:12041-12048`) re-marks the spent outposts.
  - If the diff shows otherwise, the moved lines are listed for the director. Nothing is tuned to hide them.
- **-traffic** has no outpost, so it is line-identical.
- **Save v42** (strict, the version after D1's v41). No new field: deposits are already saved whole (`:11078`), and `bOutpost` and `RegionId` are derived. The bump is a fence. A v41 save would load without the outpost rows, a silent loss of W1, so it is refused.
- **Era parity.**
  - Without a rule, era would be optimistic: it would teleport outpost ice with no road or rover, an exploit through the time controls.
  - The era branch in `EraLogistics` therefore uses the same destination helper. It moves a whole `OutpostLoadKg` only when the drill holds one, `CountActiveRobots(Rover)` > 0 (a read-only accessor; rovers are parked), and `IsFreightRoute` passes. It never moves ice to a store, and the generic era loop skips outpost sources.
  - The agent band lags by one trip (about 160 s of a 1,250 s load cycle). Over 10 sols that is about 140 kg of water out of about 8.6 t, roughly 2%, inside the 5% bar. The ledger counts in-flight rover cargo (C6 R4).

## 6. Tests: new suite `-farice` (fails loudly; named so it is never confused with -icelow; the 42nd suite)
- F1. Pure data: the three rows (mass, position, hidden, Outpost, RegionId 0); the old six Outpost FALSE; config rows read with a -1 sentinel; `OutpostLoadKg` <= RC_R `Cargo_kg`.
- F2. Ids 1-6 by row name unchanged; outposts 7, 8, 9.
- F3. Survey: 60 m reveals, 61 m does not; one roll per survey (the rival found at survey n matches a pure predictor of n).
- F4. Advice and stages: outposts hidden with near ice low gives stage 0 and the far-survey advice. The hint names the next hidden row in order. A stage moves 1 to 2 to 4 (road) to 5 (rover) to -1, and never to 3 for an outpost.
- F5. Refusals: Dig on an outpost; a Water Plant at 299 m (accepted at 301 m); no refusal before discovery, though routing still skips that plant.
- F6. Coverage: a drill outside the chain is refused; the chain links at 75 m; the drill attaches to the outpost Id.
- F7. No rover: a drill with 5 t of ice posts nothing, with no store fallback.
- F8. Rover, no road: nothing. Road at both ends: one freight task of 1,000 kg to the nearest valid plant. A nearer plant that is not road-served, or one inside 300 m, is skipped.
- F9. 999 kg, or a plant at `Have >= 2 x Need`: nothing.
- F10. A hauler next to an unclaimed outpost task for 600 agent-seconds never claims it.
- F11. Delivery in the agent band: the plant gains 1,000 kg; mass is conserved to 1e-6 (deposit + drill + cargo + plant + water / 0.9); the Water ledger is > 0.
- F12. The rover wears to halt: the unclaimed task is removed and the claimed run finishes. A forced charge mid-trip returns the cargo to the drill.
- F13. Era: no rover, nothing moves; rover and road, whole loads only; never a store.
- F14. Parity: 10 sols agent against era from one save (Ice_C drilled and roaded, 1 rover). Ice and water ledgers within 5%.
- F15. Save: v42 round trip, flags re-derived, v41 refused. Determinism: the same scenario twice gives the same task Ids, amounts and delivery steps.
- **Gate:** `bash scripts/sim/rh_battery.sh 10`, 0 not green. Then `diff_battery.py` against the 992383d logs (with D1's own accepted moves): 0 differing lines in every existing suite after the new mask.

## 7. Placeholder numbers (docs/data, live-synced; every note ends "Placeholder, director review")
- RH_Deposits: Ice_C, Ice_D, Ice_E as in section 1; the `Outpost` and `RegionId` columns.
- RH_Config: `OutpostMeltMinCm` 30000 (no ice-melting building within 300 m of a found outpost; outpost ice is never delivered there). `OutpostLoadKg` 1000 (an outpost leg moves only whole loads, in both bands).
- No change to FreightMinCm, RoverLastLegCm, the hauler fallback or the road cost.

## 8. Risks I see
1. **Struct.** Ice_C costs about 1.05 t. The C6c bot struggled to fund 445 kg by sol 38. If the 110-sol bar fails on Struct, the report shows it. The fix is a director call on road cost or deposit distance, not a quiet retune.
2. **Rover priority.** `TryClaimFreight` takes the nearest pickup, so home freight can starve an outpost. The bot shows none today. If it appears, the bot orders a second rover.
3. **Terrain is sim-blind.** The coordinates avoid the known bands, but a mesa or crater rim may still cover a dig site. Before the rows are committed, the implementer checks each one with a read-only `RH.TerrainProbe x y` (RedHope) and moves any row whose 60 m disc rises more than 3 m. The `DepositCm[]` corridor list (`RHMarsTerrain.cpp:437-445`) gains Ice_C. That changes captures only.
4. **The mask.** It hides one data-driven line per log. A reviewer may prefer a moved line per suite. The professor can rule.
5. **-icelow's harness call** is a setup change inside a pinned suite. It is honest only because the end state it builds (all ice spent) is reachable in play.
6. **The R5 deviation:** one load can overfill a plant (section 3).
7. **A dead or broken rover stops outpost water** until a Maintenance unit repairs it. This is intended pressure, and stage 5 says so.
8. **The bot's extra surveys** may find a rival earlier than in C6c runs. That is deterministic, and the report logs it.

## 9. Director question
- **Q1. How is outpost ice kept from being melted on site?**
  - **a (recommended):** no Water Plant within 300 m of found outpost ice, and haulers never run outpost ice. Only a road and a cargo rover bring it home. Ice_A and Ice_B keep today's rules.
  - **b:** outpost ice is a new raw resource that only a plant near home can clean. It is heavier (a new resource row, recipes, alerts and the ledger) and changes nothing for the player that (a) does not.
