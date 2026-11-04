# Balance analysis: placeholder rows 53 to 104 (bal2)

Read-only analysis for the P2 balance scope, 2026-11-03. No value was changed and nothing was built or run in Unreal. Repo at HEAD 6df7fcc.

## Scope

`docs/data/RH_Config.csv` has 154 rows whose Notes say "Placeholder, director review". Counting only those rows in file order, numbers 53 to 104 are CSV lines 222 to 273, from `LifeSupportDeficitCoverSols` to `TramSpeedMps`: 52 rows in eight systems.

| System | Rows | Count |
|---|---|---|
| C5d life support (the Big Arrival gate) | #53 | 1 |
| C6a cargo rovers | #54-#59 | 6 |
| C6a commuting | #60-#68 | 9 |
| W1 outposts (distant ice) | #69-#72 | 4 |
| D2 shafts, D3 tunnels | #73-#75 | 3 |
| D4 transit (hoists, lift cars, waits) | #76-#91 | 16 |
| D5 depth (dig time, cooling, warmth, shelter) | #92-#99 | 8 |
| D6d trams | #100-#104 | 5 |

## Evidence used

- The 110-sol scripted playtest `Saved/RHPlaytest/idle_r2_full.txt` (guide order, full cargo, districts, the bot's life support, living city, industry and outpost; ended at the sol limit). Its REPORT block is lines 1646-1732.
- The battery logs in `Saved/baselines/battery_post_idle/` (64 logs).
- `docs/design-decisions.md` (the C5d, C5e, C6, C6a, W1, D2, D3, D4, D5, D6c and D6d entries) and the designs in `docs/qa/2026-10-20/` to `docs/qa/2026-10-23/`.
- For the lift rows only: the older `lifts=1` playtest `Saved/RHPlaytest/d6e_lifts_final.txt` (D6e era, 2026-10-28), because the default bot never fits a lift.
- The arithmetic below comes from the CSV rows (RH_Config, RH_Robots, RH_Buildings, the solar curve) and was checked with a scratch python model. Every number is reproducible from the data.

Code readers were found with `git grep -n -P '\b<Row>\b' -- Source`. Every row is loaded in `URHSimWorldSubsystem`'s config block (`Source/RedHopeSim/Private/RHSimWorldSubsystem.cpp:126-409`) from the DT_Config table, which can be overridden for sweeps with `-RHConfig=Row=Value` (`RHDefinitionsSubsystem.cpp:46-60`; the playtest passes it through `RH_PLAYTEST_EXTRA`).

## The short answer

1. **No row in this chunk gives the scripted player a real trade-off today (0 of 52).** The playtest colony lives at one shaft head on floors -1 to -3. It never fits a lift car, sinks a second shaft, digs a tunnel, lays a tram or bores past -3 (orders: "Bore to -1", "-2", "-3" only). So the 32 rows #73-#104 are never reached in the playtest. The gates that added these rows (D2, D3, D4, D5 and D6d) each proved the 110-sol playtest byte-identical before and after (design-decisions.md).
2. **Commuting is switched on but empty.** The population peaked at 122 (ramp 0.62) after the Big Arrival, but the longest trip was 120 m against the 300 m line. REPORT line 1688: "most long commuters at once 0 ... the commute's Hope cost at most 0.000 ... the longest trip 120 m". The nine commute rows do nothing in play.
3. **The cargo rover only ever carries outpost ice.** A home haul never becomes freight, because a consumer's room is two batches (a Forge 200 kg, a Water Plant 100 kg) and freight needs a load over a hauler's 200 kg. The rover landed at sol 32.55 and moved nothing until the first outpost load at sol 52.37. Then it carried 54.8 t in 160 loads, all of it Ice_C's (REPORT 1709 and 1713). So FreightMinCm, FreightHaulerFallbackSec and RoverClaimPlans decide nothing in the playtest.
4. **The Big Arrival's deficit cover (#53) can only bind on oxygen.** The wave's own 5,000 kg of provisions count as food stock, so the 40-sol food cover passes with no farm at all for any colony under about 101 people. The water cover is always weaker than C5c's 10-sol water runway. In the playtest the wave waited on beds, not supply, and landed at sol 81.01 with every leg making more than it drew.
5. **Seven rows are already director rulings**, so their "Placeholder" tag is stale:
   - CommuteRampStartPop 60 and CommuteRampFullPop 160 (C6, Q2a);
   - LiftFitStructKg and FirstLiftFitStructKg 400 (D4, Q-A (a));
   - DepthCostFromFloor 5 (Step 2, Q3a);
   - HopeWarmMax 5 (D5, Q-B (a)) and HopeShelterMax 3 (D5, Q-A (a)).
6. **Depth has a price and almost no pull.** Past -5, every dig is slower (+15% a floor), every carved cell draws cooling power, and a warm floor costs Hope. The only reward is Shelter, at most +3 Hope. Floors -1 to -5 cost nothing extra. I found no per-floor cell cap in `Source/RedHopeSim/Public`, so their area is limited only by Borer time, Struct and air. A competent player has no reason to go past -5.
7. **Proposal: keep all 52 values.** No row has evidence to move it. Most are internally consistent (checked below), and several are bounds or proofs. Three items need the director:
   - #53: should the Big Arrival need farms? That is a rule question, not a number.
   - #62: should commuting bite at the playtest's size? CommuteLongCm is the one lever in this chunk that can put pressure on the competent path. A modelled option is below.
   - #99: should depth pull? A design question that sits on the shelter bonus.
8. **Any change is expensive for its size.** Every one of the 52 rows has a literal pin in its battery suite. Each also has a header default in `RHSimWorldSubsystem.h` that must equal the CSV, and a DT_Config row to re-sync. See "Cost of a change".

## The playtest at a glance (the numbers this chunk is judged against)

- **Hope.** Smoothed 55-100 over the run (REPORT 1687). It sat at 93-97 (FLOURISHING) from the wave to the end, with milestones worth 30 of it (CREW lines 1346-1643).
- **Population.** 4 until sol 44, 15 by sol 73, 116 at the Big Arrival (sol 81.01), 122 at the end. There were 50 job seats filled (CREW: "jobs 3 (50 seats)").
- **The wave** waited on beds: "housing short 8 (BigArrival)" at GROWTH 80. It landed when four home cells finished at 81.01. At the wave (LIFESUPPORT 81) oxygen made was 421 against 228 drawn, food 94.1 against 71.3, and water 1,740 against 35.
- **Power** was short 52% of the run, and the bank stood empty 37% of sol-hours (REPORT 1685 and 1711). There were 68 Solar Arrays and 25 Battery Banks at the end.
- **Struct** made 361 kg a sol over the run (REPORT 1725). It held the Earth program back 77 sols (REPORT 1686).
- **Settlers per ship at N = 122** were floor((Hope - 70) / 30 x 24.4). Hope 94 gives 19 and Hope 90 gives 16 (a ship at sol 87 "wanted 18"). So each Hope point is worth about 0.8 settlers a ship. This is the main way a 3-5 point Hope term reaches play.

## One line per row

Pressure: **none** means nothing in the playtest depends on the value. **Structural** means a constraint the bot satisfies without making a choice. **Latent** means it would bite on a different player (one who goes deep, sprawls or under-builds), but the scripted player never does.

| # | Row | Value | Unit | System | Pressure today | Proposal | Director |
|---|---|---|---|---|---|---|---|
| 53 | LifeSupportDeficitCoverSols | 40 | sols | C5d | latent, oxygen leg only | keep; rule question on food | yes |
| 54 | RoverOffRoadSpeedMul | 0.3 | factor | C6a rovers | structural (roads to the ends) | keep (floor 0.267) | no |
| 55 | RoverLastLegCm | 3000 | cm | C6a rovers | structural (30 m rule) | keep | no |
| 56 | FreightMinCm | 8000 | cm | C6a rovers | none | keep | no |
| 57 | FreightHaulerFallbackSec | 300 | s | C6a rovers | none | keep | no |
| 58 | RoverReserveDetourMul | 3.0 | factor | C6a rovers | none (proof bound) | keep (floor 2.667) | no |
| 59 | RoverClaimPlans | 4 | plans | C6a rovers | none (CPU bound) | keep | no |
| 60 | CommuteRoadSpeedMul | 4.0 | factor | C6a commute | none | keep | no |
| 61 | CommuteShaftCmPerFloor | 6000 | cm | C6a commute / D4 | none (sets the 120 m max trip) | keep | no |
| 62 | CommuteLongCm | 30000 | cm | C6a commute | none; the one lever here | keep, or 10000 if the director wants commute pressure | yes |
| 63 | CommuteRampStartPop | 60 | people | C6a commute | none | keep (ruled Q2a) | no |
| 64 | CommuteRampFullPop | 160 | people | C6a commute | none | keep (ruled Q2a) | no |
| 65 | HopeCommuteMax | 4 | Hope | C6a commute | none | keep | no |
| 66 | HopeCommuteSteepness | 2 | factor | C6a commute | none | keep | no |
| 67 | CommuteWorkMul | 0.85 | factor | C6a commute | none | keep | no |
| 68 | CommuteAlertShare | 0.10 | fraction | C6a commute | none | keep | no |
| 69 | OutpostMeltMinCm | 30000 | cm | W1 | structural (ice comes home raw) | keep (must stay under ~700 m) | no |
| 70 | OutpostHopperKg | 1200 | kg | W1 | none | keep (= one sol of melting) | no |
| 71 | OutpostHintGridCm | 5000 | cm | W1 | none (first survey always finds it) | keep | no |
| 72 | OutpostDrillBufferKg | 1000 | kg | W1 | none | keep (= one rover load) | no |
| 73 | MaxShafts | 4 | shafts | D2 | none (never reached) | keep | no |
| 74 | TunnelMaxCells | 64 | cells | D3 | none | keep | no |
| 75 | MaxOpenTunnels | 8 | tunnels | D3 | none | keep | no |
| 76 | LiftFitStructKg | 400 | kg | D4 | none by default; weak and early in lifts=1 | keep (ruled Q-A (a)) | no |
| 77 | FirstLiftFitStructKg | 400 | kg | D4 | as #76 | keep (ruled Q-A (a)) | no |
| 78 | MaxLiftCars | 4 | cars | D4 | none | keep | no |
| 79 | HoistPlacesPerSol | 200 | places | D4 | latent (crowds near 100 commuters at full ramp) | keep | no |
| 80 | LiftPlacesPerSol | 1000 | places | D4 | none | keep | no |
| 81 | LiftKgPerPlace | 100 | kg | D4 | none | keep | no |
| 82 | HoistDwellSeconds | 30 | s | D4 | none | keep | no |
| 83 | HoistSecondsPerFloor | 15 | s | D4 | none | keep | no |
| 84 | LiftDwellSeconds | 10 | s | D4 | none | keep | no |
| 85 | LiftSecondsPerFloor | 4 | s | D4 | none | keep | no |
| 86 | LiftPlanSpeedMps | 4.0 | m/s | D4 | none | keep | no |
| 87 | LiftPlanCm | 1500 | cm | D4 | none (a ranking key) | keep (1600 would match its own definition; not worth a battery move) | no |
| 88 | WaitSecAtDouble | 120 | s | D4 | latent | keep | no |
| 89 | MaxWaitSec | 600 | s | D4 | none (75 Wh hauler reserve) | keep | no |
| 90 | CommuteLiftCmPerFloor | 2000 | cm | D4 | none | keep | no |
| 91 | CommuteWaitCmPerSec | 100 | cm/s | D4 / D6d | none | keep | no |
| 92 | DepthCostFromFloor | 5 | floors | D5 | none | keep (ruled Q3a) | no |
| 93 | DigPermillePerDeepFloor | 150 | permille | D5 | latent | keep | no |
| 94 | CoolMilliWPerCellPerDeepFloor | 1000 | mW | D5 | latent (500 W per 100 cells on -10) | keep | no |
| 95 | CoolingLoadPriority | 7 | priority | D5 | none | keep | no |
| 96 | HeatRiseSols | 1.0 | sols | D5 | none | keep | no |
| 97 | HeatFallSols | 0.5 | sols | D5 | none | keep | no |
| 98 | HopeWarmMax | 5.0 | Hope | D5 | none | keep (ruled Q-B (a)) | no |
| 99 | HopeShelterMax | 3.0 | Hope | D5 | none; the only pull toward depth | keep (ruled Q-A (a)); depth-pull question | yes |
| 100 | TramMinCells | 12 | cells | D6d | none | keep | no |
| 101 | MaxTramLines | 8 | lines | D6d | none | keep (perf guard) | no |
| 102 | TramStructKgPerCell | 20 | kg | D6d | none | keep (= 2 kg/m, a road's price) | no |
| 103 | TramHeadwaySec | 60 | s | D6d | none | keep | no |
| 104 | TramSpeedMps | 10 | m/s | D6d | none | keep | no |

Tally: a real trade-off today, 0. Structural, 4 (#54, #55, #69, and #76/#77 in the lifts variant). Latent, about 12. Inert or a guard, the rest. Three need the director: #53, #62 and #99.

---

## C5d life support (#53)

### #53 LifeSupportDeficitCoverSols = 40 sols

- **Readers.**
  - `RHSimWorldSubsystem.cpp:344` loads it (clamped at 0 or more).
  - `:7394` (`GetSupportLeg`) applies it: `bOk = Deficit <= 0 || Stock >= cover x Deficit`.
  - `:7274-7276` (`GetBigArrivalSupport`) is the only gate that reads `bOk`. Its food leg gets the wave's provisions (50 kg x 100 = 5,000 kg) as extra stock.
  - Settler ships call `GetSupportLeg` (`:7408`) but read only its production and stock (C5e), never `bOk`.
  - Read-outs: `RHCheats.cpp:3734`; the bot's planner `RHPlaytest.cpp:626, 658, 828-830`. Header default `RHSimWorldSubsystem.h:4446`.
  - Pins: `RHSimCommandlet.cpp:6456` (-arrivals P0), `:7249-7307` (h0, h1), `:9075` (-citygrowth).
- **In play now.** It holds the one-off Big Arrival until each leg's measured production covers N + 100 people, or its stock covers 40 sols of the deficit. In the playtest the wave waited on beds, not supply: "housing short 8 (BigArrival)" at sol 80, landed at 81.01. LIFESUPPORT 81 showed oxygen 421 made against 228 drawn, food 94.1 against 71.3, and water 1,740 against 35. No leg had a deficit, so the cover was never consulted. The bot ordered Electrolyzers and a 48-cell garden as soon as the wave was in sight (LIFESUPPORT 70-77).
- **Battery evidence.** -arrivals h0 and h1: the old lean state (108 people, oxygen 30 made against 81 drawn, 1,510 kg stored) covers 29.6 sols. It is refused at 40 and passes at 10.
- **Pressure.** None in the playtest. It is latent, and only the oxygen leg can bind:
  - **Food.** The provisions cover 40 sols for up to 5,000 / (40 x 0.62) = 201 people with no farm. So the food cover never binds for a colony under about 101 people, and pre-wave colonies are 10-20 people.
  - **Water.** C5c's runway needs 10 x 2 = 20 kg a head. The cover needs at most 40 x 0.3 (the unrecycled 15%) = 12 kg a head. So C5c is always stricter.
  - **Oxygen.** At the playtest's wave (115 people, a standing leak of about 142 kg a sol), the cover needs 9,130 kg with no production and 3,130 kg with 150 kg made. C5c needs only 862 kg.
- **Proposal.** Keep 40. This is the number that makes production matter (C5d's deviation). A cover under about 16 sols is redundant with C5c on the lean state: the break-even is 7.5 x 108 / 51 = 15.9 sols.
  - The note's premise, "about a supply round", is off. Ships came about every 11 sols in the playtest (9.6, 19.7, 32.6, 44.6, 55.4, 62.7, 72.5, 87.0, 99.0). The row is really an oxygen-production gate.
  - **For the director: should the Big Arrival need farms?** A number cannot do that. To make food bind on a 15-person colony with no farm, the cover would have to exceed 5,000 / 71.3 = 70 sols, which would ask a farmless colony with no Electrolyzer for 16 t of stored oxygen. The fix would be a rule: count none, or only a share, of the provisions toward the food cover.
- **Arithmetic.**
  - Oxygen at the wave: draw = 115 x 0.75 + 142 = 228 kg a sol. C5c pool = 115 x 0.75 x 10 = 862 kg. Cover = 40 x deficit.
  - Break-even cover against C5c = 7.5 (N + 100) / deficit: 3.8 sols with no production, 15.9 sols in the h1 lean state.
  - Food: 40 x 0.62 x heads ≤ 5,000 kg, so heads ≤ 201.6.
  - Water: 40 x 0.3 = 12 kg, against 10 x 2 = 20 kg a head.
- **Moves on change.** The -arrivals P0 and h0-h1 lines and the -citygrowth pin. The 110-sol playtest does not move at any value, because no leg had a deficit at the wave.
- **Director: yes** (the food rule question; keeping 40 needs nothing).

---

## C6a cargo rovers (#54-#59)

The common facts:
- **The rover (RC_R).** 6,000 Wh battery, a 200 W move draw, 5 m/s, 1,000 kg.
- **Speeds.** On a built edge it runs 5 x 1.6 (RoadSpeedMul) = 8 m/s. Everywhere else it crawls at 5 x 0.3 = 1.5 m/s.
- **Energy units.** The sim's watt-hours are per sol-hour of 50 s. For example, X4's 90.3 s drive at 200 W is 361 Wh.
- **The hauler (RC_H).** 4 m/s, 200 kg.
- **Freight** needs four things: both ends road-served, both on the surface, at least 80 m apart, and a load over 200 kg.

The playtest's only freight lane is Ice_C to the first Water Plant at (14, 6), about 711 m straight and about 700 m of road.
- Delivered: 54.8 t in 160 loads over 57.6 sols, that is 951 kg a sol, 2.8 loads a sol, 342 kg a load.
- The drill makes 40 kg/h x 24 = 960 kg a sol. So freight was **drill-bound, not rover-bound**.
- The bot bought the rover at sol 29.46 on its own candidate rule: 80 m apart and both ends within 30 m of a road, with no room test. The rover then idled from sol 32.55 to 52.37 (LIVING 35-52: "0.0 t loaded").

### #54 RoverOffRoadSpeedMul = 0.3 (factor)

- **Readers.** `RHSimWorldSubsystem.cpp:292` loads it (clamped to 0.05-1). `:301-305` holds the reserve proof (it raises #58 with an Error if the proof fails). `:308` sets the planner's road weight, `SpeedMulPermille = round(1000 x 1.6 / 0.3) = 5333`. `:310-311` sets the move profile's off-road and missing-edge multipliers. Header `:3449`. Pin `RHSimCommandlet.cpp:10119` (-traffic X0).
- **In play now.** On the outpost lane, the two end legs (30 m at most each) are the only crawl. They take 13% (10 m legs) to 31% (30 m legs) of a 202-255 s round trip.
- **Evidence.** -traffic X1 (no road: 382 steps against a robot's 116), X4 (903 steps = 545 + 372), and X8 (a demolished edge: 589 steps against 400; every edge gone: 981; worst reserve ratio 0.874). Playtest REPORT 1709 and 1713.
- **Pressure.** Structural. With #55, it is why the road must reach both ends. No decision in the playtest turns on its value.
- **Proposal.** Keep 0.3. A road is worth 5.3x (8 / 1.5), a clear incentive. Its legal floor is 1 / (3.0 x 1.25) = 0.267. Below that, the load clamps #58 up and logs an Error.
- **Arithmetic.** The outpost round trip is 2 x (700 / 8 + 2 x L / 1.5): L = 10 m gives 202 s, L = 30 m gives 255 s. Charging: 6,000 x (0.90 - 0.25) = 3,900 Wh at a 500 W pad takes 390 s. So with worst-case end legs and charging, a rover can carry about 3.4 t a sol in full loads, against the 0.96 t a sol drilled.
- **Moves on change.** -traffic X0, X1, X4 and X8 (step counts and Wh); -farice F9 and F12 timings. In the playtest, the outpost freight lines shift only a little, since the lane is drill-bound.
- **Director: no.**

### #55 RoverLastLegCm = 3000 cm

- **Readers.** `RHSimRovers.cpp:37` (`IsRoadServed`; it also gates the rover's pads through `FindNearestRoverPad`, `:40-80`). `:96` (`IsFreightRoute`: both end legs at most this). `:206` and `:215` (`TryClaimFreight`: a pickup within this is claimed without a plan; otherwise the plan's end legs must be at most this). W1's `FindOutpostDestination` reaches it through `IsFreightRoute`. Load `RHSimWorldSubsystem.cpp:293`. Header `:3450`. Pin X0.
- **In play now.** A point is road-served when a built node lies within 30 m. This decides whether the outpost lane is freight, which pads a rover may charge at, and the longest crawl per end (30 m / 1.5 m/s = 20 s).
- **Evidence.** -traffic X6 (a pickup 80 m off the road is not freight), X8 (3,000 cm in and 3,400 cm out: 763 steps). -farice F8 (a plant 522 m out but unserved is skipped for the served one at 666 m). The playtest's outpost bar is PASS (REPORT 1723).
- **Pressure.** Structural and binary: end the road within 30 m.
- **Proposal.** Keep 3000. The road strip is 6 m wide and building half-extents are 1-5 m, so 30 m is generous. At 1500, the player lays more road (2 kg/m) without making a new decision. Past about 4000, a rover crawls 27 s per end and the "rovers need roads" ruling (C6 Q3a) weakens.
- **Arithmetic.** Crawl per end = 3,000 / 150 cm/s = 20 s.
- **Moves on change.** -traffic X0, X6 and X8; -farice F8 and F9.
- **Director: no.**

### #56 FreightMinCm = 8000 cm

- **Readers.** `RHSimRovers.cpp:91` (`IsFreightRoute`: a straight pickup-to-drop distance under this is never freight). Load `:294`. Header `:3451`. Pin X0.
- **In play now.** It keeps hauls under 80 m with the haulers. In the playtest it decides nothing:
  - Every home leg fails earlier. The R5 room rule gives a Forge 2 x 100 = 200 kg of room (X6) and a Water Plant 2 x 50 = 100 kg (F13), never over 200 kg.
  - The Forges stand 20-40 m from the Lander.
  - Regolith_B's pile, 300 m west, is a consumer leg and is not road-served.
  - The only freight was the 700 m outpost lane.
- **Pressure.** None.
- **Proposal.** Keep 8000. It is a role split, not an efficiency line. Per kg a rover beats a hauler at every distance, even with worst-case 30 m crawls at both ends:

  | Haul | Hauler | Rover |
  |---|---|---|
  | 20 m | 0.15 s/kg | 0.100 s/kg |
  | 80 m | 0.30 s/kg | 0.105 s/kg |
  | 700 m | 1.85 s/kg | 0.26 s/kg |

  So lowering it would only take short hauls from the haulers. Raising it changes nothing in the playtest. A note for the bot, not this row: its rover trigger lacks the 200 kg room test, so it bought a rover 23 sols before there was freight.
- **Arithmetic.** Hauler: (2D / 4 + 20 s) / 200 kg. Rover: (2 x (40 + (D - 60) / 8) + 20) / 1,000 kg.
- **Moves on change.** -traffic X0 and X6. The playtest: nothing.
- **Director: no.**

### #57 FreightHaulerFallbackSec = 300 s

- **Readers.** `RHSimWorldSubsystem.cpp:13087` and `:13117` (`TryClaimHaul`). Haulers skip an unclaimed freight task until it has waited this many agent-band seconds; outpost tasks are excluded for ever. Load `:295`. `RHSimTypes.h:74`. Header `:3452`. Pin X0.
- **In play now.** Nothing in the playtest. The only freight was outpost ice, which haulers never take, and no home freight existed.
- **Evidence.** -traffic X5 (refused at 200 s, allowed at 300 s; the hauler moves 200 kg and the rest is re-posted; era seconds never count) and X7.
- **Pressure.** None.
- **Proposal.** Keep 300. It only bounds starvation when every rover is charging. A rover's recharge from 25% to 90% at a 500 W pad takes 3,900 Wh / 500 W = 7.8 sol-hours = 390 s, so haulers step in before a lone charging rover returns.
- **Moves on change.** -traffic X0 and X5.
- **Director: no.**

### #58 RoverReserveDetourMul = 3.0 (factor)

- **Readers.** `RHSimWorldSubsystem.cpp:296` loads it (at least 1). `:301-305` raises it to 1 / (0.3 x 1.25) with an Error if below. `RHRobotStateTree.cpp:512-518` uses it: a rover's way-home reserve is `DriveWh(this x the straight distance to the nearest road-served pad)`. Header `:3453`. Pin X0.
- **In play now.** It sets when a rover breaks off to charge. The reserve is small against 6,000 Wh: X8 shows 550 Wh for a 150 m case, and the worst real crawl used 0.874 of it.
- **Pressure.** None (a safety bound).
- **Proposal.** Keep 3.0. It is 12.5% above the proof's floor of 2.667. Lowering it to 2.7 would free about 10% of a reserve that is already under a tenth of the battery, which is not worth a battery move.
- **Arithmetic.** 3.0 x 1.25 = 3.75 ≥ 1 / 0.3 = 3.33. The floor is 1 / (0.3 x 1.25) = 2.667.
- **Moves on change.** -traffic X0 and X8.
- **Director: no.**

### #59 RoverClaimPlans = 4 plans

- **Readers.** `RHSimRovers.cpp:209` (`TryClaimFreight`) and `RHSimWorldSubsystem.cpp:2024` (`FindOutpostDestination`). Load `:297`. Header `:3454`. Pin X0.
- **In play now.** A CPU bound. The playtest never had more than one open freight task ("peaked at 1", REPORT 1709), and one plant on the road network.
- **Evidence.** -farice F8: the fourth plan wins, the fifth finds nothing.
- **Pressure.** None.
- **Proposal.** Keep 4. It is a perf guard; raise it only with a -scale run.
- **Moves on change.** -traffic X0 and -farice F8.
- **Director: no.**

---

## C6a commuting (#60-#68)

The common facts:
- **What is measured.** A commute is each employed colonist's own trip, home to seat, in walking-equivalent cm.
- **What is long.** A trip over 30,000 cm.
- **The penalty.** Hope loses Ramp x 4 x min(1, 2 x long / pop). A long commuter works at 1 - Ramp x 0.15.
- **The ramp.** Ramp = clamp((N - 60) / 100, 0, 1).
- **In the playtest.** The ramp opened only with the Big Arrival and peaked at 0.62. The longest trip was 120 m (2 floors by hoist, a -1 home and a -3 garden seat). The lifts=1 run's longest was 40 m. There were 0 long commuters in 110 sols (REPORT 1688). The D6c `-RHCommuteAudit` of the same layout saw 12,000 cm and an 18,000 cm margin.

### #60 CommuteRoadSpeedMul = 4.0 (factor)

- **Readers.** `RHSimCommute.cpp:46` and `:82` (the commuter road params, `SpeedMulPermille` 4000). Load `RHSimWorldSubsystem.cpp:313`. Header `:3470`. Pin X0.
- **In play now.** A road leg counts a quarter of its length. In the playtest every home and seat is underground at the shaft head, so no trip uses a road.
- **Evidence.** -traffic X11 (32,000 cm of road = 8,000 cm) and X13 (a finished shortcut drops a 49,064 cm trip to 21,750 cm and returns the 1.0 Hope it cost).
- **Pressure.** None.
- **Proposal.** Keep 4.0. It matters only when homes or seats sit across the surface. Robots use 1.6.
- **Director: no.**

### #61 CommuteShaftCmPerFloor = 6000 cm

- **Readers.** `RHSimCommute.cpp:211` (`PerFloor` for a shaft with no car) and `:280`. Load `:314`. Header `:3471`. Pins X0 and `RHSimTransitSuite.cpp:310` (TR0).
- **In play now.** Every floor crossed by hoist costs 60 m. It sets the playtest's longest trip: 2 x 60 = 120 m.
- **Evidence.** -traffic X11 (-2 to the surface: 23,663 cm), -transit TR6, -depth DP12 (a -20 home with a surface seat: 120,000 cm).
- **Pressure.** None today. It sets where depth turns long: -5 to the surface at the head is exactly 30,000 (not long; the test is >), and -6 is 36,000 (long). This lines up with DepthCostFromFloor 5.
- **Proposal.** Keep 6000. The C6 REVISION's 2000/40000 retune was declined as not needed. The alignment with the depth line is worth keeping: -6 is the first long floor by hoist, -16 by lift.
- **Moves on change.** X0, X11, TR6-TR8 and DP12. In the playtest, the "longest trip" figure on REPORT 1688; Hope moves only if 2 x the new value exceeds 30,000, that is from 15,001 up.
- **Director: no.**

### #62 CommuteLongCm = 30000 cm (the one lever in this chunk)

- **Readers.**
  - `RHSimCommute.cpp:494, 570, 610, 669` (`bLong = DistCm > this`) and `:946` (the alert copy).
  - `RHSimWorldSubsystem.cpp:91` (the audit margin).
  - Load `:315`, header `:3472`, pin X0.
- **In play now.** Every commute penalty hangs on this line, and in the playtest no trip passed it. The longest was 40% of the line, so in 110 sols there was no long commuter, no Hope cost, no work cut and no alert.
- **Evidence.**
  - Battery: -traffic X13 and X14, -depth DP12, -deepcommute DC1 (a 30-cell tunnel walk is 34,828 cm, which is long; with a tram it is 11,728 cm).
  - Playtest: REPORT 1688.
- **Pressure.** None today, by design: commuting is a deep-or-sprawl mechanic. It is the only row here that can put pressure on the competent path.
- **Proposal.** Keep 30000, unless the director wants commuting to bite at the playtest's size.
- **The option for the director: CommuteLongCm = 10000 (100 m).**
  - **What changes.** The playtest's 2-floor hoist trips (120 m) become long. The player's answers are the designed ones: home people on their work floor, or fit a lift. A -1 to -3 trip by lift is 2 x 2,000 = 4,000 cm, so the lift gains a commute purpose it has nowhere today.
  - **The modelled effect at the end of the playtest** (122 people, ramp 0.62, 50 workers). If L of them are long, Hope falls 0.62 x 4 x min(1, 2L / 122):

    | Long commuters (L) | Hope lost |
    |---|---|
    | 12 | 0.49 |
    | 30 | 1.22 |
    | 50 (all workers) | 2.03 |

    Each long worker works at 0.907. With Hope at 93-97, a 2-point loss keeps FLOURISHING (exit 85) and costs about 1.6 settlers a ship (0.81 a point). The garden would still make more than the 75.6 kg drawn (114.9 today; even a 9% cut leaves about 104).
  - **What I cannot measure from the logs.** The playtest does not log where each worker lives, so L lies between 0 and 50. Measure it first with `RH_PLAYTEST_EXTRA="-RHConfig=CommuteLongCm=10000"` plus `-RHCommuteAudit`, and again with `RH_PLAYTEST_LIFTS=1`.
  - **Battery cost.** The -percapita and -arrivals suites keep Long = 0 only because their floor-only trips (at most 4 floors = 24,000 cm) stay under the line. Any line under 24,000 moves their Hope pins. X0, X13, X14, DP12, TR6-TR8, and the DC and TM hand integers move too.
- **Director: yes** (taste: should commuting bite at 120 people?).

### #63 CommuteRampStartPop = 60 people

- **Readers.** `RHSimCommute.cpp:36` and `:40` (the ramp). The Hope read and `RefreshJobs` return the zero summary below it (`RHSimWorldSubsystem.cpp:6103`). The ramp also scales the transit and tram people loads. Load `:316`. Header `:3473`. Pin X0.
- **In play now.** Below 61 people nothing is measured. The playtest crossed it only when the Big Arrival landed (15 → 116 people).
- **Pressure.** None (there is nothing long to scale).
- **Proposal.** Keep 60. It is the director's C6 Q2a ramp (60 to 160), so the placeholder tag is stale. It is also a battery constraint: most suites peak at 50 colonists or fewer, so a start under about 50 would open the ramp in them (C6 design, R8).
- **Moves on change.** X0, X14 and TR6. In the playtest, the "peak ramp" figure.
- **Director: no.**

### #64 CommuteRampFullPop = 160 people

- **Readers.** `RHSimCommute.cpp:40`. Load `:317` (clamped to at least the start + 1). Header `:3474`. Pin X0.
- **In play now.** Full cost from 160 people. The playtest peaked at 122.
- **Pressure.** None.
- **Proposal.** Keep 160 (the director's Q2a).
- **Director: no.**

### #65 HopeCommuteMax = 4 Hope

- **Readers.** `RHSimCommute.cpp:786` (penalty = Ramp x this x min(1, steepness x long / pop)). Load `:318`. Header `:3475`. Pin X0.
- **In play now.** 0 in the playtest.
- **Evidence.** -traffic X13 (1 of 8 long: 1.0) and X14 (110 people with 30 of 30 workers long: 1.09; at 160 people: 1.50).
- **Pressure.** None today. If commuting did bite, the cap is mid-scale among the Hope terms:

  | Term | Cap |
  |---|---|
  | views | 3 |
  | shelter | 3 |
  | **commute** | **4** |
  | warm | 5 |
  | furnishings | 6 |
  | housing | 7.5 |
  | jobs | 7.5 |
  | luxury | 8 |
  | food | 10 |
  | water | 12 |
  | adjacency | 16 |
  | unsupported | 30 |

  A full 4 points at the playtest's Hope costs about 3 settlers a ship and keeps the band.
- **Proposal.** Keep 4. Revisit only together with #62.
- **Director: no.**

### #66 HopeCommuteSteepness = 2 (factor)

- **Readers.** `RHSimCommute.cpp:786`. Load `:319`. Header `:3476`. Pin X0.
- **In play now.** 0.
- **Pressure.** None.
- **Proposal.** Keep 2: half the colony long costs the whole term. Workers are 41% of the playtest's colony (50 of 122), so even all workers long gives min(1, 0.82). The term saturates only when most people work far away.
- **Director: no.**

### #67 CommuteWorkMul = 0.85 (factor)

- **Readers.** `RHSimCommute.cpp:783` (`WorkMul = 1 - Ramp x (1 - this)`). It reaches play through `RHSimWorldSubsystem.cpp:5148` (the Garden skill hook) and `:6284` (the Lab and Workstation seat sums). Load `:320`. Header `:3477`. Pins X0 and `RHSimDeepCommuteSuite.cpp:729`.
- **In play now.** Not applied (no long commuter).
- **Evidence.** -traffic X14: Lab 8.5 = 0.85 x 10, Workstation the same, Garden skill 0.85, never 0.85².
- **Pressure.** None.
- **Proposal.** Keep 0.85. At ramp 0.62 a long commuter works at 0.907.
- **Director: no.**

### #68 CommuteAlertShare = 0.10 (fraction)

- **Readers.** `RHSimCommute.cpp:941` (one alert when long / pop first reaches this; it needs at least one long commuter) and `RHSimWorldSubsystem.cpp:6991`. Load `:321`. Header `:3478`. Pin X0.
- **In play now.** It never fired in the playtest (no "travel over" line).
- **Evidence.** -traffic X14: it fires once at 10%.
- **Pressure.** None (information only).
- **Proposal.** Keep 0.10. At 122 people it fires at 13 long commuters, when they cost 0.62 x 4 x 0.21 = 0.53 Hope: early enough to warn.
- **Director: no.**

---

## W1 outposts (#69-#72)

In the playtest:
- Ice_A ran low at sol 34.91. The hint opened at 35.03 ("about 690 m east-south-east").
- The survey at the hint centre (600, -350) m found Ice_C (80 t, 720 m out) at 35.21.
- The program was 10 pylons, 635 m of road and a drill, with a plan bill of 1,749 kg Struct. Struct held it back 7 sols (REPORT 1712-1713).
- The drill ran from 51.43, and the first load came at 52.37.
- The water bar PASSes (REPORT 1723). At the end the colony made 1,410 kg of water a sol against 37 drawn, with 76 t in store (REPORT 1710).

### #69 OutpostMeltMinCm = 30000 cm

- **Readers.** `RHSimWorldSubsystem.cpp:2004` (`FindOutpostDestination` skips plants nearer the deposit than this) and `:8881-8898` (a Water Plant cannot be placed within this of a found outpost: "Outpost ice comes home raw..."). Load `:406`. Header `:4928`. Pin `RHSimCommandlet.cpp:11390-11393` (-farice F1).
- **In play now.** Outpost ice must travel at least 300 m by rover to a plant. In the playtest the only road-served plant was the first one at (14, 6), 711 m from Ice_C, so the rule never had to refuse anything. It is the reason the outpost is a road-and-rover program at all.
- **Evidence.** -farice F5 (a plant at 299 m is refused, at 301 m allowed) and F8 (a road-served plant 250 m away is not a destination).
- **Pressure.** Structural: it closes the shortcut of melting beside the drill.
- **Proposal.** Keep 30000.
  - **Upper bound.** It must stay under the outpost-to-base distance: Ice_C is 711 m from the first plant, and the outposts lie at 720, 757 and 806 m. Otherwise no base plant qualifies and the outpost is useless.
  - **The fair shortcut.** A player can still put a plant 300-410 m out along the lane and halve the haul. That is a fair choice.
- **Arithmetic.** A 300 m haul is a round trip of about 2 x (300 / 8 + up to 40) = 75-155 s, against 202-255 s for the base lane.
- **Director: no.**

### #70 OutpostHopperKg = 1200 kg

- **Readers.** `RHSimWorldSubsystem.cpp:2014` (a plant takes outpost loads only while Have + 200 < this) and `:2018` (room = this - Have). Load `:407`. Header `:4929`. Pin F1.
- **In play now.** It sizes the plant's hopper for outpost ice. The first plant melted what came; loads averaged 342 kg.
- **Evidence.** -farice F8 and F13: a plant holding 999 kg qualifies with 201 kg of room; at 1,000 kg it does not.
- **Pressure.** None.
- **Proposal.** Keep 1200. It is a Water Plant's 24-hour melt (50 kg/h x 24), and one 1,000 kg rover load fits an empty hopper.
- **Director: no.**

### #71 OutpostHintGridCm = 5000 cm

- **Readers.** `RHSimWorldSubsystem.cpp:1941` (`GetOutpostHint` rounds the row's position to this grid). Load `:408`. Header `:4930`. Pin F1.
- **In play now.** The hint names a 50 m area. The first survey always finds the outpost: Ice_C was 26 m off the centre, inside the scout's 60 m.
- **Evidence.** -farice F1 (worst offset 26.0 m) and F3 (a survey 61 m off finds nothing).
- **Pressure.** None. There is no search.
- **Proposal.** Keep 5000. Any grid up to about 84 m still guarantees the first survey, since the worst offset is grid x √2 / 2 ≤ 60 m. If the director wanted a real search, 100-150 m would sometimes need a second survey. That is taste, and it moves F1, F3 and the playtest's survey lines.
- **Director: no** (keep).

### #72 OutpostDrillBufferKg = 1000 kg

- **Readers.** `RHSimWorldSubsystem.cpp:1765` (an outpost drill starts no batch while it holds this). Load `:409`. Header `:4931`. Pin F1.
- **In play now.** A guard for an outpost with no rover. In the playtest the rover kept the drill far below it.
- **Evidence.** -farice F11: 30 sols with no rover, the drill holds 1,000 kg and idles at 15 W.
- **Pressure.** None.
- **Proposal.** Keep 1000, one full rover load (RC_R's Cargo_kg).
- **Director: no.**

---

## D2 shafts and D3 tunnels (#73-#75)

### #73 MaxShafts = 4

- **Readers.** `RHSimWorldSubsystem.cpp:160-165` loads it (clamped to 1-16). `:7799` and `:7845` refuse a sink: "No more shafts - the colony may sink 4". Header `:3930`. Pins `RHSimCommandlet.cpp:13052` and `:13131` (-shafts SH0, SH2).
- **In play now.** Never reached; the playtest sinks no second shaft.
- **Pressure.** None.
- **Proposal.** Keep 4. It is a scope and perf cap: D2's -scale block was measured with four shafts. Raise it only with a -scale run.
- **Director: no.**

### #74 TunnelMaxCells = 64

- **Readers.** `:170-175` loads it (clamped to 2-256). `:8109-8111` refuses: "A tunnel runs at most 64 cells". Header `:3942`. Pins `RHSimCommandlet.cpp:14083-14084` and `:14126` (-tunnels TN0, TN2).
- **In play now.** Never reached (no tunnel).
- **Pressure.** None.
- **Proposal.** Keep 64.
  - Length: 640 m.
  - Work: 64 CarveCell batches of 3 h = 192 sol-hours = 8 Borer-sols above the depth line, and 26 on -20 (x 3.25).
  - Power: on -20 it costs 960 W of cooling for ever.
- **Director: no.**

### #75 MaxOpenTunnels = 8

- **Readers.** `:176-181` loads it (clamped to 1-64). `:8174-8176` refuses: "No more open tunnels - 8 at a time". Header `:3943`. Pins TN0, TN2 and `RHSimCommandlet.cpp:16308`.
- **In play now.** Never reached.
- **Pressure.** None.
- **Proposal.** Keep 8. D3's -scale ran at this cap: 8 tunnels, 360 cells, within budget.
- **Director: no.**

---

## D4 transit (#76-#91)

With no lift car fitted, transit is off. The hoist is free and endless, and none of #76-#89 is read except the fit's bill. The default playtest never fits a car.

The lifts=1 run (`d6e_lifts_final.txt`, 110 sols) is the only playtest evidence:
- The fit waited from 11.26 ("400 kg needed, 196 on hand") to 13.04.
- It made 10 rides, 2 hand-off holds, 4 deliveries below and 1 build below.
- The peak population was 118 (ramp 0.58), and the longest trip was 40 m.
- D4's own entry says waits stayed 0 in the bot's colony (115 people, light cargo).

### #76 LiftFitStructKg = 400 kg

- **Readers.** `RHSimWorldSubsystem.cpp:186` loads it. Header `:1347` (`GetLiftFitBillKg`: this once transit is on). Read by `FitLift` in `RHSimTransit.cpp` (around `:625-671`) and `RHCheats.cpp:465`. Header `:4057`. Pin `RHSimTransitSuite.cpp:294` (TR0).
- **In play now.** Not in the default playtest. In the lifts run, the first car cost about 1.8 sols of waiting for Struct.
- **Pressure.** Weak and early only. 400 kg is about 2.7 sols of one Forge's 150 kg a sol at sol 11, and 1.1 sols of the late colony's 361 kg a sol. For comparison, it is the same Struct as 200 m of road or 20 tram cells.
- **Proposal.** Keep 400. This is the director's D4 Q-A (a) of 2026-10-21, so the placeholder tag is stale.
- **Director: no.**

### #77 FirstLiftFitStructKg = 400 kg

- **Readers.** `:187` loads it (it defaults to LiftFitStructKg). Header `:1347`, debug setter `:1455`, default `:4058`. Pins TR0 (`RHSimTransitSuite.cpp:295`) and `RHSimCommandlet.cpp:16465`.
- **In play now.** As #76. It is the bill of the colony's first car, which switches transit on.
- **Pressure.** As #76.
- **Proposal.** Keep 400 (Q-A (a): the first car costs what every car costs). The tag is stale.
- **Director: no.**

### #78 MaxLiftCars = 4

- **Readers.** `RHSimTransit.cpp:625` and `:666` (the `FitLift` refusal and log). Load `:188-193` (clamped to 1-8). Header `:4059`. Pins TR0 and `RHSimTransitSuite.cpp:318`.
- **In play now.** Never reached.
- **Pressure.** None.
- **Proposal.** Keep 4. That is 4,000 places a shaft, or 2,000 commuters at full ramp. -scale measured 4 cars on each of 4 shafts.
- **Director: no.**

### #79 HoistPlacesPerSol = 200 places

- **Readers.** `RHSimTransit.cpp:244` (the capacity of a shaft with no car). `:353` and `:500` (D6e: a lift short of power falls toward this). `:671`. Load `:204` (must be above 0). Header `:4060`. Pin TR0.
- **In play now.** Read only with transit on. Even then, the playtest's people load is 2 x 50 workers x 0.62 = 62 places, far under 200. The lifts runs had waits of 0.
- **Evidence.** -transit TR4 (hoist waits: Cap + 1 → 1 s, 2 x Cap → 120 s, 10 x Cap → 600 s) and TR6 (61 people → 1 place; 160 → 320).
- **Pressure.** Latent. A hoist saturates at commuters x ramp = 100, roughly a colony of 160-250 people with half of them working across floors.
- **Proposal.** Keep 200.
- **Director: no.**

### #80 LiftPlacesPerSol = 1000 places

- **Readers.** `RHSimTransit.cpp:244` (cars x this). Load `:205`. Header `:4061`. Pin TR0.
- **In play now.** Never crowded.
- **Pressure.** None.
- **Proposal.** Keep 1000. That is five hoists' worth, or 500 commuters per car at full ramp.
- **Director: no.**

### #81 LiftKgPerPlace = 100 kg

- **Readers.** `RHSimTransit.cpp:380` (`GetCargoPlaces = ceil(kg / this)`). Load `:206`. Header `:4062`. Pin TR0.
- **In play now.** Only with transit on: a hauler's 200 kg load is 2 places.
- **Evidence.** -transit TR9 and TR17.
- **Pressure.** None.
- **Proposal.** Keep 100, about one person's mass.
- **Director: no.**

### #82 HoistDwellSeconds = 30 s and #83 HoistSecondsPerFloor = 15 s

- **Readers.** `RHSimWorldSubsystem.h:1341-1342` (`GetShaftDwellSeconds` / `GetShaftSecondsPerFloor`) feed `RHSimTransit.cpp:1858`: a hand-off hold of 10 x (wait + dwell + floors x per-floor) sub-steps. Also `RHSimTransit.cpp:671` (the log). #83 is also read by the section view (`RHSectionVisualizerSubsystem.cpp:1479`, `:1486`, presentation). Loads `:208-209`. Headers `:4063-4064`. Pin TR0.
- **In play now.** Only with transit on, for a hauler handing cargo off at a shaft with no car.
- **Evidence.** -transit TR9: 450 sub-steps for one floor (30 + 15), with idle draw 4.5 Wh; through a car, 140.
- **Pressure.** None.
- **Proposal.** Keep both. A 3-floor hoist hand-off is 30 + 45 = 75 s against a car's 10 + 12 = 22 s ride. That gap is a reason to fit a car.
- **Director: no.**

### #84 LiftDwellSeconds = 10 s and #85 LiftSecondsPerFloor = 4 s

- **Readers.** `RHSimTransit.cpp:1309` and `:1314` (ride cost units), `:1530` (route cost), `:1606` (`RideSteps`). Also through `RHSimWorldSubsystem.h:1341-1342` to `:1858`. Loads `:210-211`. Headers `:4065-4066`. Pin TR0.
- **In play now.** Only with a car. The lifts run made 10 rides in 110 sols.
- **Pressure.** None.
- **Proposal.** Keep both: a one-floor ride is 14 s (140 sub-steps), on 4 m floors at 1 m/s.
- **Director: no.**

### #86 LiftPlanSpeedMps = 4.0 m/s

- **Readers.** `RHSimWorldSubsystem.cpp:212` loads it into `LiftPlanSpeedCmPerS`, which prices a ride in the robots' deep planner (`RHSimTransit.cpp:1308`). Header `:4067`. Pin TR0.
- **In play now.** Robot routing below ground, only with a car.
- **Pressure.** None.
- **Proposal.** Keep 4.0. It equals RC_H's speed, so a second in the cage costs what a second of hauler walking costs.
- **Director: no.**

### #87 LiftPlanCm = 1500 cm

- **Readers.** `RHSimWorldSubsystem.cpp:13097` (the haul claim key) and `:13291` (the target claim key), each DistXY + |floors| x this. Load `:213`. Header `:4068`. Pin TR0.
- **In play now.** A ranking key for cross-floor claims, only with robots below.
- **Pressure.** None.
- **Proposal.** Keep 1500. Its own definition ("one floor at the planning speed, without waits") gives 4 s x 4 m/s = 1,600 cm. But a change only reorders near-tied claims, and it would move -transit TR10-TR13 for nothing a player sees.
- **Director: no.**

### #88 WaitSecAtDouble = 120 s

- **Readers.** `RHSimTransit.cpp:253`: wait = ceil((load - cap) x this / cap), for shafts and tram lines. Load `:207` (must be above 0). Header `:4069`. Pin TR0.
- **In play now.** Never, because nothing crowds.
- **Evidence.** -transit TR4: 2 x Cap gives 120 s; 6,000 places through four cars gives 60 s.
- **Pressure.** Latent.
- **Proposal.** Keep 120. At double load, every commute through the shaft gains 120 m (#91 is 100 cm/s), so a crowded hoist takes a -2 trip from 120 m to 240 m. Crowding makes trips long only together with depth or sprawl, which is a sensible slope.
- **Arithmetic.** For a hoist: 300 places → 60 s, 400 → 120 s, 600 → 240 s, 1,200 → 600 s (the cap).
- **Director: no.**

### #89 MaxWaitSec = 600 s

- **Readers.**
  - `RHSimTransit.cpp:254` (the cap).
  - `:1751-1760` and `RHRobotStateTree.cpp:500`: the deep charge reserve prices every ascent hold at this.
  - `RHTramVisualizerSubsystem.cpp:86-88` (the wait tint).
  - Load `:214-219` (clamped to 0-1200), header `:4070`.
  - Pins: TR0, `RHSimTransitSuite.cpp:318` and `:489`, plus the trams and transitpower suites.
- **In play now.** Never reached. Its reserve cost is real but small: 5 W x 600 s / 50 x 1.25 = 75 Wh per ascent for a hauler, 5% of its 1,500 Wh.
- **Pressure.** None.
- **Proposal.** Keep 600. It is reached at 6x capacity, and it adds 600 m to a commute, twice the long line. Raising it toward the 1,200 cap doubles the hauler reserve to 150 Wh.
- **Director: no.**

### #90 CommuteLiftCmPerFloor = 2000 cm

- **Readers.** `RHSimCommute.cpp:211` (the per-floor cost through a fitted shaft). Load `:220`. Header `:4071`. Pin TR0.
- **In play now.** Only with a car. The lifts run's longest trip was 40 m, against 120 m without a car.
- **Evidence.** -transit TR6 (2,000 + 16,017 beats 6,000 + 14,000) and -depth DP12 (-15 is 30,000 and not long; -16 is 32,000 and long, even on an empty lift).
- **Pressure.** None today; no trip is long either way.
- **Proposal.** Keep 2000. A lift triples the depth a surface job can reach before the trip is long: from -5 to -15.
- **Director: no.**

### #91 CommuteWaitCmPerSec = 100 cm/s

- **Readers.** `RHSimCommute.cpp:216` (a shaft's frozen wait in a commute). `RHSimTram.cpp:18` (the tram edge = (headway / 2 + ride) x this) and `:62` (a line's wait). Load `:221`. Header `:4072`. Pin TR0.
- **In play now.** Waits are 0 in every playtest, and there is no tram.
- **Pressure.** None.
- **Proposal.** Keep 100 (walking pace, 1 m/s). Note that it also prices the tram, so a change rescales trams and waits together.
- **Director: no.**

---

## D5 depth (#92-#99)

None of these is reached in the playtest: the deepest floor is -3, so no floor lies past the -5 line. Every D5 branch tests `Level < -DepthCostFromFloor`.

**Depth has a price and almost no pull.** If a player did go deep, these numbers would apply:

| Floor | Dig time | Bore | Cooling for 100 cells | Solar Arrays (109 W mean) | Battery Banks for a 12 sol-hour night |
|---|---|---|---|---|---|
| -6 | x1.15 | 13.8 h | 100 W | 0.9 | 0.3 |
| -10 | x1.75 | 21.0 h | 500 W | 4.6 | 1.5 |
| -15 | x2.50 | 30.0 h | 1,000 W | 9.2 | 3.0 |
| -20 | x3.25 | 39.0 h | 1,500 W | 13.8 | 4.5 |

Notes on the table:
- The Solar Array mean comes from integrating `RH_Curve_SolarDiurnal.csv`: 0.3625 x 300 W = 109 W. The playtest's 68 arrays averaged 7,445 W, about 109 W each.
- The Borer's hydrogen bill (1.5 kg a sol-hour when it runs on fuel) scales with the dig time.
- Warmth only costs Hope, at most 5, and is fully recoverable.
- Shelter is the only reward: +3 at most.
- By hoist, a surface job from -6 or deeper is a long commute.

### #92 DepthCostFromFloor = 5 floors

- **Readers.** `RHSimDepth.cpp:27-30` (clamped to 0-30) and `:41, :67, :80, :99, :220`. Load `RHSimWorldSubsystem.cpp:137`. Cooling entries `:8620`. `GetFloorsBelowLine` in header `:989`. The shaft strip reads it in `RHCommandDeck.cpp:3177`. Header `:3899`. Pins `RHSimDepthSuite.cpp:158` and `:179` (DP0).
- **In play now.** Nothing in the playtest.
- **Pressure.** None.
- **Proposal.** Keep 5. It is the director's Step 2 Q3a ("Floors -1 to -5 keep today's numbers"), so the tag is stale. Its own note warns that any change moves every baseline.
- **Director: no.**

### #93 DigPermillePerDeepFloor = 150 permille

- **Readers.** `RHSimDepth.cpp:36` (1000 + this x floors past the line, fixed when a batch starts). Load `:138`. Header `:3900`. Pin DP0.
- **In play now.** Nothing.
- **Evidence.** -depth DP0 (-6 is 1150, -20 is 3250) and DP2 (bore -6 13.8 h, -7 15.6 h; hydrogen 26.1 kg on -8).
- **Pressure.** Latent.
- **Proposal.** Keep 150 (+15% a floor).
- **Director: no.**

### #94 CoolMilliWPerCellPerDeepFloor = 1000 mW

- **Readers.** `RHSimDepth.cpp:50`. Load `:139`. Header `:3901`. Pin DP0.
- **In play now.** Nothing.
- **Evidence.** -depth DP4: 100 cells on -6 add 100 W, on -10 500 W; tunnel cells count.
- **Pressure.** Latent but real for a deep player: 100 cells on -10 need about 4.6 arrays and 1.5 banks, out of the playtest colony's 68 arrays.
- **Proposal.** Keep 1000.
- **Director: no.**

### #95 CoolingLoadPriority = 7

- **Readers.** `RHSimWorldSubsystem.cpp:8631`. Load `:140`. Header `:3902`. Pins DP0 and `RHSimTransitPowerSuite.cpp:317`.
- **In play now.** Nothing.
- **Pressure.** None.
- **Proposal.** Keep 7. Cooling sheds with the Water Plant, after the Electrolyzers (4) and drills (6); transit (6) sheds before it. In a scraping night, warmth starts only after the lower priorities are already shed.
- **Director: no.**

### #96 HeatRiseSols = 1.0 sols and #97 HeatFallSols = 0.5 sols

- **Readers.** `RHSimDepth.cpp:94` (rise) and `:95` (fall). Load `:141-148`: both must be above 0, or the load logs an Error and uses 1.0 and 0.5. Headers `:3907-3908`. Pins DP0 and `RHSimDepthSuite.cpp:645`, `:703`.
- **In play now.** Nothing.
- **Evidence.** -depth DP6 (Warmth 1.0 after a sol short; exactly 0 half a sol after recovery) and DP9 (at least 0.25 every night, reset by day, agent and era bands within 0.1).
- **Pressure.** None.
- **Proposal.** Keep both. A 12 sol-hour night short of cooling reaches 0.5 Warmth and clears in a quarter sol of daylight. That is fully recoverable, as Q-B (a) asks.
- **Director: no.**

### #98 HopeWarmMax = 5.0 Hope

- **Readers.** `RHSimWorldSubsystem.cpp:6972` (`WarmPenalty = this x min(1, Σ residents x Warmth / pop)`). Load `:149`. Header `:3909`. Pins DP0 and `RHSimDepthSuite.cpp:566-584`.
- **In play now.** Nothing.
- **Evidence.** -depth DP7: 5.0 x share 1 = 5.0, with no harm to air, beds or recalls.
- **Pressure.** None.
- **Proposal.** Keep 5. It is the director's D5 Q-B (a), "up to -5", so the tag is stale.
- **Director: no.**

### #99 HopeShelterMax = 3.0 Hope

- **Readers.** `RHSimWorldSubsystem.cpp:6943` (Shelter = this x residents homed past the line / pop). Load `:150`. Header `:3910`. Pins DP0 and `RHSimDepthSuite.cpp:634`.
- **In play now.** Nothing; nobody lives past -5.
- **Evidence.** -depth DP8: 10 of 40 on -6 give +0.75; all deep give +3.
- **Pressure.** None. It is the only pull toward depth: +3 at most and bonus-only, against slower digs, cooling power, warmth risk and, by hoist, long commutes from -6 down.
- **Proposal.** Keep 3. It is the director's D5 Q-A (a), so the tag is stale.
- **For the director.** Depth has a price and almost no pull. Floors -1 to -5 carry no extra cost and (as far as I found) no area limit, so a competent player never needs to go past -5. If depth should be a real choice, the pull has to come from somewhere: a bigger shelter bonus, or something only deep floors give. That is a design call; no small change to these eight rows creates it.
- **Director: yes.**

---

## D6d trams (#100-#104)

No playtest lays a tram; even `RH.DeepBench`'s lines carry 0 riders. A tram edge costs (headway / 2 + ceil(length / speed)) seconds at the walking pace (#91, 100 cm/s), with length = (chain points - 1) x 10 m.

| Chain points | Walk | Tram | Saved | Bill (20 kg a point) |
|---|---|---|---|---|
| 4 | 3,000 cm | 3,300 cm | -300 | 80 kg |
| 5 | 4,000 | 3,400 | 600 | 100 |
| 12 (the minimum) | 11,000 | 4,100 | 6,900 | 240 |
| 30 | 29,000 | 5,900 | 23,100 | 600 |
| 64 | 63,000 | 9,300 | 53,700 | 1,280 |

Break-even is 3,333 cm of tunnel, that is 5 chain points.

### #100 TramMinCells = 12

- **Readers.** `RHSimTram.cpp:103-105` (the `LayTramLine` refusal). Load `RHSimWorldSubsystem.cpp:226` (at least 2). `RHCheats.cpp:501` and `:2353`. Header `:4100`. Pins `RHSimTramSuite.cpp:294`, `:336`, `:338`.
- **In play now.** Nothing.
- **Evidence.** -trams TM1: 11 points are refused, 12 are allowed, and the bill is 240 kg.
- **Pressure.** None.
- **Proposal.** Keep 12. A minimum line saves 69 m a ride, so every legal line is worth its car.
- **Director: no.**

### #101 MaxTramLines = 8

- **Readers.** `RHSimTram.cpp:115-117`. Load `:227-232` (clamped to 1-32). `RHTramVisualizerSubsystem.cpp:263` (the presentation's component budget). Header `:4101`. Pins TM0, TM1 and `RHSimCommandlet.cpp:17006`.
- **In play now.** Nothing.
- **Pressure.** None.
- **Proposal.** Keep 8. D6d's -scale with twenty lines measured 9.2 ms of the 10 ms cold-commute budget, so the cap is also a perf guard.
- **Director: no.**

### #102 TramStructKgPerCell = 20 kg

- **Readers.** `RHSimTram.cpp:74` (bill = chain x this, through `TakeStructForBill`). Load `:233`. Header `:4102`. Pin TM0.
- **In play now.** Nothing.
- **Pressure.** None.
- **Proposal.** Keep 20. It is 2 kg a metre, the same as a road (RoadStructKgPerMeter 2). A 30-point line is 600 kg, about 1.7 sols of the late colony's Struct.
- **Director: no.**

### #103 TramHeadwaySec = 60 s

- **Readers.** `RHSimTram.cpp:18`. Load `:234`. Header `:4103`. Pin TM0.
- **In play now.** Nothing.
- **Evidence.** -trams TM2: a 30-point line is 5,900 cm against 29,000 on foot; at a 522 s headway the ride equals the walk.
- **Pressure.** None.
- **Proposal.** Keep 60. The 30 s average wait counts as 30 m of walking.
- **Director: no.**

### #104 TramSpeedMps = 10 m/s

- **Readers.** `RHSimWorldSubsystem.cpp:235-243` loads it into `TramSpeedCmPerS` (must be above 0), which `RHSimTram.cpp:16` reads. Header `:4104`. Pin TM0.
- **In play now.** Nothing.
- **Pressure.** None.
- **Proposal.** Keep 10: 10x walking and 2.5x a road shuttle (#60).
- **Director: no.**

---

## Cost of a change (for the synthesis)

A value change in this chunk touches four places:
1. the CSV row;
2. the DT_Config asset, re-imported (the suites read the live table and fail on drift);
3. the header default in `Source/RedHopeSim/Public/RHSimWorldSubsystem.h` (the code's comments say the defaults "equal the CSV");
4. the literal pin in its suite.

The pins:

| Rows | Suite | Pin |
|---|---|---|
| #53 | -arrivals, -citygrowth | `RHSimCommandlet.cpp:6456` (P0), `:9075` (citygrowth) |
| #54-#68 | -traffic | X0, `RHSimCommandlet.cpp:10119-10121` (all 15 C6a rows) |
| #69-#72 | -farice | F1, `RHSimCommandlet.cpp:11390-11393` |
| #73 | -shafts | SH0, `RHSimCommandlet.cpp:13052` |
| #74-#75 | -tunnels | TN0, `RHSimCommandlet.cpp:14083-14084` |
| #76-#91 | -transit | TR0, `RHSimTransitSuite.cpp:294-310` |
| #92-#99 | -depth | DP0, `RHSimDepthSuite.cpp:158-165` |
| #100-#104 | -trams | TM0, `RHSimTramSuite.cpp:294-298` |

Beyond the pin, the hand integers that derive from a row move. This is my estimate from the logs:
- **Rovers.** #54 moves about 8 -traffic lines (X0, X1, X4, X8) and -farice F9/F12. #55 moves X6 and X8, and F8/F9. #56-#59 move 1-3 lines each.
- **Commuting.** #60-#68 move X11-X14 (about 10-20 lines). #61 and #62 also move TR6-TR8, DP12, and the DC/TM integers. #62 under 24,000 also moves the -percapita and -arrivals Hope pins.
- **Outposts.** #69-#72 move 2-6 -farice lines each.
- **Transit.** #76-#91 move the TR hand integers (TR4, TR6, TR9, TR14), about 3-10 lines each.
- **Depth.** #92-#99 move the DP hand integers. #92 moves every depth line and, by its own note, every baseline that prints a floor range.
- **Trams.** #100-#104 move TM1-TM3, about 3-20 lines each.

**The 110-sol playtest baseline** moves for only a few rows:
- #54, #55: the outpost freight lines, slightly, since the lane is drill-bound.
- #61: the longest-trip figure; Hope moves only from 15,001.
- #62: Hope from sol 81 on, at 11,999 or less.
- #63, #64: the peak-ramp figure.
- #69, #71, #72: only if moved far (#69 past about 700 m strands the outpost; #71 changes the hint text and survey order).
- #53 never moves it, because no leg had a deficit at the wave.
- #73-#104 cannot move it, because the bot never reaches that code. The lifts=1 variant moves for #76-#91.

## Sweeps worth running before any change (not run: this analyst may not run Unreal)

1. `RH_PLAYTEST_DISTRICTS=1 RH_PLAYTEST_EXTRA="-RHConfig=CommuteLongCm=10000 -RHCommuteAudit" bash scripts/sim/rh_playtest.sh 110 bal_long10 guide full`, then the same with `RH_PLAYTEST_LIFTS=1`. This measures the long count L and the Hope cost of #62's option; the model says 0-2 Hope.
2. A control at `-RHConfig=LifeSupportDeficitCoverSols=0`. It should be byte-identical to `idle_r2_full.txt`, because no leg had a deficit at the wave. That would prove #53 inert in the playtest.
3. The D2-D6d rows (#73-#104) cannot be balanced from the playtest. They need a deep or sprawling scripted player, which does not exist. The bot's switches in `scripts/sim/rh_playtest.sh` are CARRYOVER, DISTRICTS, INDUSTRY, LIFESUPPORT, LIFTS, NIGHTBANK, NOGROWTH, OUTPOST and ROADS; none of them digs past -3, sinks a second shaft, or lays a tunnel or a tram. Until then, their only evidence is the battery fixtures and -scale.
