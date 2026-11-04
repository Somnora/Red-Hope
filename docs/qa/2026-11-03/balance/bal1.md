# Balance analysis: placeholder rows 1 to 52 (bal1)

Read-only analysis for the P2 balance scope, 2026-11-03. Nothing in the repo was changed, built or run. Repo `/Volumes/Unreal/red_hope/red_hope` at HEAD 6df7fcc (clean tree).

## Scope

`docs/data/RH_Config.csv` has 322 rows; 154 say "Placeholder, director review". Counting only those, numbers 1 to 52 are CSV lines 24 to 221, from `DomeMaxBedsPerSide` to `ProductionWindowSols`.

| System | Rows | Count |
|---|---|---|
| Build mode and world (dome size, depth cap, shaft link, salvage) | #1, #2, #5, #6 | 4 |
| Ice (D2a) | #3, #4 | 2 |
| Hope lines (BM-4/6, D4a, C2a) | #7-#11, #21-#26 | 11 |
| Earth: the ladder, credit, launch (D1a, E2b, E3) | #12-#17 | 6 |
| Day shift (D5a) | #18-#20 | 3 |
| Population: transports, births, settlers, the Big Arrival, moves (C2b, C5c) | #27-#41 | 15 |
| Roads (C4) | #42, #43 | 2 |
| City growth and the City / Earth share (C5b, C5d) | #44-#52 | 9 |

## Evidence used

- The 110-sol scripted playtest `Saved/RHPlaytest/idle_r2_full.txt` (1,732 lines; REPORT block lines 1646-1732) and its `.log`. It is byte-identical to the industry baseline `Saved/baselines/ind_full_playtest.txt` (`diff`: 0 lines). Per-sol CREW, GROWTH and LIFESUPPORT lines were parsed with a scratch python script.
- The battery logs in `Saved/baselines/battery_post_idle/` (64 logs): each row's data pin, where one exists.
- `docs/design-decisions.md` (D1a-D5a, D3a, C2, C4, C5b-C5e, E2b, E3, E3b, "Playtest bot builds industry by default"), `docs/qa/2026-10-17/crew-phase-brief.md`, `docs/qa/2026-10-18/c2-population-brief.md` and `c2a-moved-lines.md`, `docs/PROJECT_STATUS.md`.
- Data: RH_Quotas, RH_Resources (EarthValue), RH_ManifestItems, RH_Furniture, RH_Doors, RH_Buildings, RH_Curve_SolarDiurnal.

Readers were found with `git grep -nP '\b<Row>\b' -- Source`. Every row is loaded from DT_Config in `URHSimWorldSubsystem`'s config block (a `-RHConfig=Row=Value` override works for sweeps). Path short forms below, all under `Source/RedHopeSim/Private/`: **WS** = `RHSimWorldSubsystem.cpp`, **Growth** = `RHSimGrowth.cpp`, **Export** = `RHSimExport.cpp`, **Econ** = `RHSimEconomy.cpp`, **Roads** = `RHSimRoads.cpp`, **Districts** = `RHSimDistricts.cpp`, **Depth** = `RHSimDepth.cpp`, **Cavern** = `RHSimCavern.cpp`.

## The playtest at a glance (what these rows face)

- **People.** 0 until the Crew Pod (sol 19.72, 4 people). 15 by sol 73: 9 settlers (3 a ship at sols 55.36, 62.70, 72.46) and 2 births (sols 44.06 and about 64.5). The ships at 32.55 and 44.55 brought none: "IMMIGRATION held by life support" (food made 0.8 vs 2.5, then 3.0 vs 3.1 kg a sol). The Big Arrival landed at sol 81.00 (15 -> 115). 122 at sol 110 (born 9, immigrated 9, recalled 0).
- **Hope.** Smoothed 55..100. At least 75 from sol 24, never below 89 after sol 37, 100 on sols 52-81. Instant Hope at sol 110: base 50 + housing 5 + rooms 4 + jobs 3 + comforts 8 + furnishings 1 + views 0 + milestones 30 - adjacency 4 = 97.
- **No penalty fired.** Unsupported 0, water 0, food 0 on every CREW line. Four environmental crises (-6 each for 5 sols) changed no gate. Food low point 57 sols of draw (sol 45). Water: 76 t in store at the end, made 1,410 vs 37 drawn a sol.
- **Earth.** Q3 met on sols 29.46, 40.55, 52.36, 59.70, 69.46, 81.63, 93.73 (rounds 0-6); round 7 unmet at 110. Two short launches (sols 84.00 and 96.00), each held by oxygen, leaving 484 and 575 kg of arrears. Sent to Earth: Struct 24,618, O2 11,010, H2 2,413, water 200 kg. Nothing was sold (the "sold" clause reads 0 on all 9 launches).
- **What binds.** Struct in mid-game: the Homes district (#1) was short of Struct on 43 of 99 GROWTH sols, and "Struct held the program back 77 sols". Oxygen late: Homes was short of O2 on 27 sols, 15 of them from sol 88 on. Free beds for the Big Arrival: "housing short (BigArrival)" was 75 at sol 31, 24 at sols 75-79 and 8 at sols 80-81, then met. It was closed by four Homes batches and by the 36 garden cells carved on floor -3 on sols 70-73: every carved cell on a rated floor counts as one legacy bed unless it is a Living cell with bed pieces (`CountFloorBedsImpl`). Power was short 52% of the run; the bank stood empty 37% of sol-hours.
- **What the scripted player never does.** It never toggles a day shift, demolishes, builds a dome, a surface habitat or a window, buys a Colonist Transport, sells to Earth, or spends carried credit (13,841 cr unspent at the end).

## The short answer

1. **Hope is saturated, so none of the 11 Hope rows here changes a gate in the run** (RoomServesResidents still steers growth's civic rooms, #24). Base 50 + milestones 30 + comforts 8 = 88 before any row in this chunk counts. So jobs (7.5), furnishings (6), views (3), rooms and adjacency (16) only decide between 88 and the 100 clamp: THRIVING or FLOURISHING. Every gate in reach (births, settlers and the Big Arrival, all at 75) sits far below. The penalty rows (unsupported 30, food 10) could reach the gates, but they never fire, because the C5c-C5e life-support gates stop shortages before they start. This is D3a's open question ("cap the Milestones line ... fade ... or accept", `docs/design-decisions.md`), still listed as open in PROJECT_STATUS.md.
   - **Model.** With the Milestones line capped at 15, instant Hope just after the wave (sol 82) is 93 - 15 = 78. Capped at 10, it is 73, under the 75 gates. Then the post-wave gaps become the levers that reopen settlers and births: jobs (3.2 of 7.5), housing (4 of 7.5), rooms (2 of about 5) and furnishings (0 of 6).
   - **So tune the Hope rows after the milestone decision, not before.** The small colony stays at 89 or more even with a cap of 10.
2. **The live levers in this chunk are economic.** QuotaRepeatBillMul, QuotaShortLaunchSols, the two day-shift defaults, GrowthReserveStructKg, GrowthReserveO2Sols, GrowthCityShareDefault and BigArrivalFreeBeds each bind in the run. The numbers are in the rows below.
3. **One value disables a gate (#28).** ColonistTransportFoodKgPerColonist 50 means the Big Arrival brings 5,000 kg (80.6 sols of its own ration). Its food leg then passes for any colony of 101 or fewer, even with no garden and an empty larder.
   - **Proposal: 25 kg** (= LifeSupportDeficitCoverSols 40 x 0.62 kg a sol). The wave then carries exactly its own 40-sol cover. This is the only value change proposed in this chunk.
   - bal2 reaches the same finding from the cover's side (#53).
4. **One decision for the director (#15, #16).** At the launch spread L = 3 cr/kg, water sells at a net 1 cr/kg with no price response. The run's water surplus (about 0.96 t a sol at the end, 76 t stored) could fund about 11,500 cr of cargo per 12-sol round. That is close to a whole Q3 award.
   - Keep L = 3 if ice is meant to be a cash crop.
   - Otherwise, LaunchH2GPerKg 67 and LaunchO2GPerKg 400 give L = 4.004 cr/kg with the 6:1 oxygen-to-hydrogen burn kept. Water's price then goes negative, so it is refused.
5. **Fourteen rows are already director rulings, so their "Placeholder" tag is stale.** Each value was in the option the director picked:
   - IceLowAlertFraction (D2a, "~25%");
   - HopeFoodPenalty 10 and FoodRunwayFloorSols 10 (D4a);
   - QuotaRepeatBillMul 1.10 (E3 Q-C (a));
   - ShipMassCapKg 30,000 (E2b Q-A (a));
   - QuotaShortLaunchSols 3 (E3 Q-B (a));
   - DayShiftDefault_Forge and DayShiftDefault_Electrolyzer (D5a);
   - BigArrivalHopeMin, BigArrivalStreakSols, BigArrivalFreeBeds and BigArrivalColonists (Q7a: "about 100 colonists ... about 20 sols at THRIVING with 100 free beds");
   - GrowthCityShareDefault 0.5 (C5d option a, confirmed by the industry ruling);
   - ImmigrationHopeMin 75 (Q5a, "a thriving city"; ruled in spirit).
6. **Proposal: keep 51 values and change 1 (#28).** The director's taste is needed on 9 rows: #6, #7, #8, #15, #16, #21, #23, #25 and #28.
7. **A rule point found on the way (no row in this chunk).** The Big Arrival's 100 free beds are certified beds. Every carved cell on a rated floor counts as one, so the bot's 36 new garden cells on floor -3 closed a third of the gap (#38). At sol 110 the city had 145 beds but comfortable Living room for only about 80 (housing 5 of 7.5 at 122 people; five Homes batches are 80 bunk beds). Housing Hope halved and post-wave settlers were held by comfort (#33). Whether a garden cell should count as a bed is a rule question for the director.

## Pressure, counted

Labels used in every row:
- **REAL**: the row shapes a decision with a cost that a player faces today.
- **ACTIVE**: it binds or moves numbers in the run, but is not itself a player choice (a rate, a gate, a brake). bal2 calls this "structural".
- **MASKED**: a real lever that moves in the run, but the saturated Hope hides its effect.
- **LATENT**: it matters only for actions the scripted player never takes.
- **NONE**: no decision hangs on it (a bound, an alert, a cadence, or a derived value).

| Label | Count | Rows |
|---|---|---|
| REAL | 8 | 12, 13, 17, 19, 20, 38, 42, 51 |
| ACTIVE | 11 | 24, 28, 29, 31, 32, 33, 39, 44, 45, 49, 50 |
| MASKED | 4 | 7, 23, 25, 26 |
| LATENT | 10 | 5, 6, 8, 9, 10, 15, 16, 21, 22, 27 |
| NONE | 19 | 1, 2, 3, 4, 11, 14, 18, 30, 34, 35, 36, 37, 40, 41, 43, 46, 47, 48, 52 |

## One line per row

| # | CSV line | Row | Value | System | Pressure today | Proposal | Director |
|---|---|---|---|---|---|---|---|
| 1 | 24 | DomeMaxBedsPerSide | 3 beds | Agri dome | NONE (size cap) | keep | no |
| 2 | 35 | MaxDepth | 20 floors | Depth bound | NONE (run uses -1..-3) | keep | no |
| 3 | 85 | IceLowAlertFraction | 0.25 | Ice alert | NONE (alert lead time) | keep (ruled D2a) | no |
| 4 | 86 | IceWaterPlantNear_m | 100 m | Ice guide | NONE (guide only) | keep | no |
| 5 | 166 | ShaftLinkRange_m | 10 m | Surface habs | LATENT (saves an Air Filter) | keep | no |
| 6 | 167 | SalvageFraction | 0.5 | Demolish | LATENT (bot never demolishes) | keep | yes |
| 7 | 169 | HopeFurnishingsMax | 6 Hope | Hope bonus | MASKED (6 -> 0 after the wave) | keep, revisit after milestones | yes |
| 8 | 170 | HopeViewsMax | 3 Hope | Hope bonus | LATENT (views 0 all run) | keep, revisit after milestones | yes |
| 9 | 171 | HopeFoodPenalty | 10 points | Hope penalty | LATENT (never fired) | keep (ruled D4a) | no |
| 10 | 172 | FoodRunwayFloorSols | 10 sols | Food alert, arrivals | LATENT (low point 57) | keep (ruled D4a); firm option 15 | no |
| 11 | 173 | FoodRunwayRearmSols | 15 sols | Food alert | NONE (hysteresis) | keep (floor + 5) | no |
| 12 | 174 | QuotaRepeatBillMul | 1.10 x | Earth ladder | REAL (O2 squeeze from round 5) | keep (ruled Q-C (a)) | no |
| 13 | 175 | AwardMcrPerKg | 1000 mcr/kg | Earth credit | REAL via the award (unit row) | keep | no |
| 14 | 176 | ShipMassCapKg | 30000 kg | Earth ship | NONE (max use 16.5 t) | keep (ruled Q-A (a)) | no |
| 15 | 177 | LaunchH2GPerKg | 50 g/kg | Earth sales | LATENT (nothing sold) | keep; or 67 | yes |
| 16 | 178 | LaunchO2GPerKg | 300 g/kg | Earth sales | LATENT (nothing sold) | keep; or 400 | yes |
| 17 | 179 | QuotaShortLaunchSols | 3 sols | Earth launch | REAL (2 short launches) | keep (ruled Q-B (a)) | no |
| 18 | 180 | DayShiftStartFrac | 0.25 | Day shift | NONE (= sunrise) | keep | no |
| 19 | 183 | DayShiftDefault_Forge | 1 | Day shift | REAL (Forge night rest 50%) | keep (ruled D5a) | no |
| 20 | 184 | DayShiftDefault_Electrolyzer | 1 | Day shift | REAL (O2 is the late line) | keep (ruled D5a) | no |
| 21 | 185 | HopeUnsupportedMax | 30 points | Hope penalty | LATENT (never fired) | keep | yes |
| 22 | 186 | HopeUnsupportedSteepness | 3 | Hope penalty | LATENT | keep | no |
| 23 | 187 | HopeJobsMax | 7.5 points | Hope bonus | MASKED (7.5 -> 3.2 after the wave) | keep, revisit after milestones | yes |
| 24 | 188 | RoomServesResidents | 25 | Hope rooms, growth | ACTIVE (drives 9 civic rooms) | keep | no |
| 25 | 189 | HopeAdjacencyMax | 16 points | Hope penalty | MASKED (-2..-5 all run) | keep | yes |
| 26 | 190 | ComfortRefPop | 8 | Hope per head | MASKED | keep | no |
| 27 | 191 | ColonistTransportColonists | 20 | Arrivals | LATENT (never bought) | keep | no |
| 28 | 192 | ColonistTransportFoodKgPerColonist | 50 kg | Arrivals, wave food | ACTIVE; disables the wave's food leg | **25** | yes |
| 29 | 193 | BirthRefPop | 25 | Births | ACTIVE (rate) | keep | no |
| 30 | 194 | ImmigrationHopeMin | 75 Hope | Settlers | NONE (Hope 94-100 at landings) | keep | no |
| 31 | 195 | ImmigrationHopeFloor | 70 Hope | Settlers | ACTIVE (sizes each ship) | keep | no |
| 32 | 196 | ImmigrationBase | 4 | Settlers | ACTIVE (3 a ship in practice) | keep | no |
| 33 | 197 | ImmigrationPerCapita | 0.2 | Settlers | ACTIVE (18 wanted after the wave) | keep | no |
| 34 | 198 | ImmigrationCapPerShip | 50 | Settlers | NONE (binds from 250 people) | keep | no |
| 35 | 199 | ImmigrationReserveBeds | 1 bed | Settlers, births | NONE | keep | no |
| 36 | 200 | BigArrivalHopeMin | 75 Hope | Big Arrival | NONE (never reset) | keep (ruled Q7a) | no |
| 37 | 201 | BigArrivalStreakSols | 20 sols | Big Arrival | NONE (56.9 at landing) | keep (ruled Q7a) | no |
| 38 | 202 | BigArrivalFreeBeds | 100 beds | Big Arrival | REAL (the binding gate) | keep (ruled Q7a) | no |
| 39 | 203 | BigArrivalColonists | 100 | Big Arrival | ACTIVE (82% of the final city) | keep (ruled Q7a) | no |
| 40 | 204 | BigArrivalLifeSupportSols | 10 sols | Big Arrival, settlers | NONE (O2 pool 2x the need) | keep | no |
| 41 | 205 | MoveHomeIntervalSols | 1 sol | Moving home | NONE (nobody moved) | keep | no |
| 42 | 209 | RoadStructKgPerMeter | 2 kg/m | Roads | REAL, light (4.3% of Struct) | keep | no |
| 43 | 210 | RoadBuildMetersPerHour | 30 m/h | Roads | NONE (Fabricator 63% idle) | keep | no |
| 44 | 213 | GrowthReserveStructKg | 1000 kg | Growth brake | ACTIVE (the strongest wave brake) | keep | no |
| 45 | 214 | GrowthReserveO2Sols | 10 sols | Growth, launch floor | ACTIVE (late growth, short launches) | keep | no |
| 46 | 215 | GrowthModuleSols | 1.0 sol | Growth pacing | NONE (no surface modules) | keep | no |
| 47 | 216 | GrowthCellSols | 1.0 sol | Growth pacing | NONE (material-bound) | keep | no |
| 48 | 217 | GrowthRoomSols | 0.5 sol | Growth pacing | NONE | keep | no |
| 49 | 218 | GrowthCellsPerProject | 4 cells | Growth size | ACTIVE (640 kg Struct + 400 kg O2 lumps) | keep | no |
| 50 | 219 | GrowthBunksPerHome | 2 | Growth size | ACTIVE (16 beds a batch) | keep | no |
| 51 | 220 | GrowthCityShareDefault | 0.5 | City / Earth dial | REAL (the player's dial) | keep (ruled C5d) | no |
| 52 | 221 | ProductionWindowSols | 3 sols | Life-support measure | NONE (smoothing) | keep | no |

## Per row

### 1. DomeMaxBedsPerSide = 3 beds (line 24)
- **Readers.** WS:384 (load, at least 1); WS:3227 `GetMaxBedsPerSide`. It is the fallback for a building whose `MaxBedsPerSide` column is 0, which is only the GreenhouseDome; HabModule (4) and HabCorridor (6) carry their own column.
- **Now.** The largest dome a drag can size: 3 x 3 beds (30 m x 30 m, 9 beds). Each bed takes 250 kg soil, 50 kg seeds and 200 kg glass, and grows by daylight with no grow-light power. The battery pins it in dome.log ("refused past 3 beds a side").
- **Evidence.** The bot never builds a dome; its garden is floor -3 (46 grow-lit cells).
- **Pressure.** NONE. It is a size cap: more domes give the same beds.
- **Proposal.** Keep 3. A full dome needs 1,800 kg of glass, which is 4.5 Glass Crates (2,700 kg of cargo). A bigger cap changes how many domes are dragged, not the trade-off (imported glass against 60 W of grow light a cell).

### 2. MaxDepth = 20 floors (line 35)
- **Readers.** WS:133 (load, fallback 5); Depth:18 `ApplyMaxDepthRow` (clamped to 1..`RH::MaxDepthCap` 30, the light key's bit budget); Depth:187, Cavern:326 and Cavern:647 (refusals below the bottom floor); `RHCommandDeck.cpp`:389 and 486 (elevator rows).
- **Now.** A world bound. Lowering it refuses saves with deeper floors (row note).
- **Evidence.** The run bores to -3 only. depth.log DP0 pins 20 ("this fails on drift").
- **Pressure.** NONE. Depth's costs and rewards are other rows (DepthCostFromFloor etc., bal2).
- **Proposal.** Keep 20.

### 3. IceLowAlertFraction = 0.25 (line 85)
- **Readers.** WS:404 (load); WS:1889-1890 `IsIceDepositLow`, which feeds the alert and `GetIceExpansionStage`.
- **Now.** RUNNING LOW fires once when a found ice deposit falls below 25% of its surveyed mass.
- **Evidence.**
  - Ice_A (30 t) alerted at sol 34.91 ("about 7 t of ice left").
  - The bot found Ice_B at 35.03. The grid reached it at 44.02, its drill ran at 44.89, and its Water Plant at 46.98.
  - Measured water production dipped to 375-495 kg a sol on sols 34 and 45-46 and was back to 735 by sol 48.
  - Ice_B alerted at 93.85 ("about 15 t left"). The battery pin is icelow.log I1.
- **Pressure.** NONE. It is a warning; water was never short (76 t stored).
- **Proposal.** Keep (ruled in D2a's "~25%"). The 7.5 t left at about 0.69 t a sol (one drill and plant) is about 11 sols of warning. The bot's response took about 10 sols (survey 0.1, pylons 9, drill 0.9). Bigger deposits get proportionally longer warning.

### 4. IceWaterPlantNear_m = 100 m (line 86)
- **Readers.** WS:405 (load); WS:2101 (`GetIceExpansionStage` stage 3, "melt it where it is drilled"); the guide at `RHCommandDeckGuide.cpp`:526.
- **Now.** Guide advice only. Hauls go to the nearest plant whatever this says (the D2a logistics fix).
- **Pressure.** NONE.
- **Proposal.** Keep.

### 5. ShaftLinkRange_m = 10 m (line 166)
- **Readers.** WS:273 (load, stored in cm); WS:3559-3564 `IsBayNearShaftHead`, used by the network rating at WS:3549 and WS:10996.
- **Now.** A surface habitat network is circulated through the shaft when a bay centre lies within 10 m (each axis) of a shaft head and floor -1 is rated. That saves its own Air Filter (80 kg Struct, 200 W). surfacehab.log S10 and S12b exercise it.
- **Evidence.** No surface habitats in the run.
- **Pressure.** LATENT, and small.
- **Proposal.** Keep. 10 m is one bay of the 10 m lattice, so the network must touch the head.

### 6. SalvageFraction = 0.5 (line 167)
- **Readers.** WS:274 (load, clamped 0..1); WS:10843-10847 (demolish refund); Roads:458 and Roads:498 (a standing road refunds this share; an unbuilt one refunds all).
- **Now.** Demolishing returns half the Level-taxed bill (per bay for shells) to the Lander. Held stock comes back whole. Imported hardware salvages nothing. Furniture is separate at 75% (`FurnitureRefundFraction`, ruled). Pins: demolish.log D1, roads.log R16.
- **Evidence.** The bot never demolishes (0 demolish lines).
- **Pressure.** LATENT. It is the price of re-planning: a Forge returns 125 of 250 kg, a Habitat Bay 100 of 200 kg a bay.
- **Proposal.** Keep 0.5.
- **Director.** Yes: how forgiving Sims-style build mode should be is taste.

### 7. HopeFurnishingsMax = 6 Hope (line 169)
- **Readers.** WS:276 (load); WS:6877 (the Hope line); WS:12432 and WS:12464-12465 (the furnish preview).
- **Now.** Furnishings = min(summed HopeComfort x ComfortRefPop / pop, 6).
- **Evidence.**
  - CREW lines: 6 (capped) at pop 4 from sol 29, after growth's furnished dining room and infirmary.
  - It fell to 5 at pop 5, 3 at 8, 2 at 11-15, 0 at 116, and 1 at 121-122.
- **Pressure.** MASKED. It moved by 6 points with no gate effect, because Hope sat at 89-100.
- **Proposal.** Keep, and revisit with the milestone decision.
- **Arithmetic.** A Plant, Lamp or Rug is 1 comfort for 20 kg Struct; a Sofa is 2 for 60 kg. The cap is reached at 0.75 comfort a resident:
  - 8 people: 6 plants (120 kg).
  - 122 people: 92 plants (1,840 kg, about 5 sols of the three Forges' 361 kg a sol).
- **Director.** Yes.

### 8. HopeViewsMax = 3 Hope (line 170)
- **Readers.** WS:277 (load); WS:6883 and WS:11800.
- **Now.** Windows in rated surface habitats give 0.5 Hope each, scaled by 8 / pop and capped at 3. A window costs 10 kg Struct + 40 kg Glass and leaks 0.5 kg O2 a sol.
- **Evidence.** Views 0 on every CREW line (no surface habitat). Exercised in walls.log and the move suite.
- **Pressure.** LATENT.
- **Proposal.** Keep, and revisit with the milestone decision.
- **Arithmetic.**
  - At 8 people, 6 windows give 3 Hope (240 kg of glass, 3 kg O2 a sol).
  - At 122 people, 3 Hope needs 92 windows: 3,680 kg of glass (about 9 Glass Crates, 5,520 kg of cargo) and 46 kg O2 a sol of leak.
  - So views are a small-colony bonus by construction.
- **Director.** Yes.

### 9. HopeFoodPenalty = 10 points (line 171)
- **Readers.** WS:401 (load); WS:6916.
- **Now.** Under FoodRunwayFloorSols, Hope loses 10 x (floor - runway) / floor.
- **Evidence.** Never fired (food -0 on every CREW line; low point 57 sols). food.log F4 pins the shape: runway 5 gives 5, 2.5 gives 7.5, 0 gives 10.
- **Pressure.** LATENT. An empty larder also puts a home on short rations, which adds the unsupported line (#21, up to 30).
- **Proposal.** Keep (ruled in D4a: "Hope loses up to 10 points").

### 10. FoodRunwayFloorSols = 10 sols (line 172)
- **Readers.** WS:402 (load); WS:5109 (alert); WS:6911-6916 (Hope line); WS:7259-7272 (the Big Arrival's food and water runways at N + wave); WS:7419 and 7424 (the settlers' water and food stock legs); WS:15414 (load latch).
- **Now.** The FOOD RUNNING LOW alert and the start of the food Hope line. It is also the runway every arrival must leave (the settlers' and the wave's stock checks).
- **Evidence.**
  - The floor was never crossed: the low point was 57 sols.
  - No arrival was held by a stock runway. The two held ships were held by food production (the C5e leg).
  - Pins: food.log F0/F3-F7, plus the arrivals and water-loop checks.
- **Pressure.** LATENT.
- **Proposal.** Keep 10 (ruled in D4a). The guide's "Keep them well" branch already warns at 40 sols.
- **Firm option: 15.** One Q3 round (11.5 sols on average between met sols in the run) plus 3 sols of transit is 14.5. At 15, the alert comes early enough for one ship's soil and seeds to land. The penalty at a 10-sol runway would then be 3.3.
- **Predicted effect.** None on the playtest: the arrivals' stock checks at 15 still pass (e.g. 74 kg needed vs 292 held at the sol 55 ship).

### 11. FoodRunwayRearmSols = 15 sols (line 173)
- **Readers.** WS:403 (load, never below the floor); WS:5120.
- **Now.** Alert hysteresis only (food.log F6/F7).
- **Pressure.** NONE.
- **Proposal.** Keep. If #10 moves, keep the 5-sol gap (20).

### 12. QuotaRepeatBillMul = 1.10 (line 174)
- **Readers.** WS:451 (load); WS:2243, 2302, 2342, 2373 and 2394-2398 (`GetQuotaBillScale`). resupply.log R0 pins 1.10. The resupply, citygrowth and export XE8 ladders set 1.25 by hand for their numbers (`RHSimCommandlet.cpp`:4943, 8938; `RHSimExportSuite.cpp`:393), so they would not move. The credit, economy and export suites restore the configured value after their knob-0 checks, so their later lines would.
- **Now.** After Q3 the ladder repeats with every bill x 1.1^round, in whole kg.
  - Struct: 2,500, 2,750, 3,025, 3,328, 3,660, 4,026, 4,429, 4,872 for rounds 0-7.
  - O2: 1,200 rising to 2,338. H2: 250 rising to 487.
- **Evidence.**
  - Struct sent to Earth is 24,618 = 300 (Q1) + 600 (Q2) + the sum of rounds 0-6 (23,718), exactly.
  - Round 5's O2 (1,933) and round 6's (2,126 + 484 arrears = 2,610) went short.
  - Round 7 (2,338 + 575 = 2,913 kg O2) is unmet at sol 110 with 2,622 held.
  - Net O2 at the end is 427 made - 262 drawn = 165 a sol, about 1,980 a 12-sol round. So from round 5 Earth's O2 bill outruns net production: the squeeze starts around sol 82.
- **Pressure.** REAL. It sets when Earth's demands start to bite, through short launches and held growth.
- **Proposal.** Keep 1.10 (ruled in Q-C (a); the run passes the director's bar with 7 rounds).
- **Options** (cumulative bills for rounds 0-6, from a scratch model):
  - **1.05:** Struct 20,355, O2 9,771 (-14%). The O2 bill passes 1,980 only at round 10, so there is no squeeze before sol 110.
  - **1.15:** Struct 27,667, O2 13,281 (+17%). The O2 squeeze starts at round 4 (2,099), around sol 70. Expect about 6 rounds and a later Big Arrival (fewer sols with free Struct).
  - **1.25** (the pre-E3 value): round 4 is O2 2,930 and Struct 6,104. E3's adapted bot met 5 rounds with no Big Arrival at 1.25 (before industry=2).
  - The bill plateau at the 30 t cap comes at round 21.3 at 1.10 (14.5 at 1.15, 9.1 at 1.25).

### 13. AwardMcrPerKg = 1000 mcr/kg (line 175)
- **Readers.** WS:452 (load); WS:2192 `PayQuotaAward`; WS:13813-13817 `CreditPay(floor(kg x 1000))`. credit.log CR0 pins it.
- **Now.** Every manifest row costs 1,000 mcr per kg, so one award-kg buys exactly one kg of cargo.
- **Evidence.**
  - Awards were 9,887-16,887 kg and manifests 3,000-16,500 kg.
  - The wishlist ran over budget at sol 29.46 (AdvExcavator and ExtraHauler refused, carried next ship).
  - The bot budgets by award kg, so it never spends its carried credit (6,887 cr after ship 1, 13,841 at the end).
- **Pressure.** REAL through the award: the manifest budget is the crew-versus-hardware decision. This row is only the unit that converts the award into a budget.
- **Proposal.** Keep. Tune the awards (RH_Quotas) or the prices (RH_ManifestItems), not this conversion.

### 14. ShipMassCapKg = 30000 kg (line 176)
- **Readers.** WS:453 (load); WS:2174 (stamped when the manifest opens); WS:2221 (cleared at landing); WS:13829 and 13839; Econ:334 and 439; Export:227, 291 and 577.
- **Now.** Each ship carries at most 30 t in, and 30 t out (quota plus sales).
- **Evidence.** The largest manifest was 16,500 kg and the largest outbound load 6,907 kg. credit.log CR0: the largest award the data can make is 25,100 kg, so only saved credit reaches the cap.
- **Pressure.** NONE.
- **Proposal.** Keep (ruled in Q-A (a)).

### 15. LaunchH2GPerKg = 50 g/kg (line 177) and 16. LaunchO2GPerKg = 300 g/kg (line 178)
- **Readers.** WS:454 and 455 (load); Export:164 `GetLaunchCostMcrPerKg`, read by the price at Export:172. Exercised in the export suite.
- **Now.** The launch cost L = (50 x 12,000 + 300 x 8,000) / 1000 = 3,000 mcr, i.e. 3 cr per kg sold. The burn ratio is 6:1 oxygen to hydrogen, a hydrolox burn. No tank is drawn.
  - Net prices at multiplier 1.0: Struct 2, water 1, O2 5, H2 9 cr/kg. Cargo costs 1 cr/kg.
  - Earth's price does not respond to volume.
  - Under Earth displeasure the margins vanish: water reaches zero at a multiplier of 0.75 and Struct at 0.6, and a sale with no positive price is refused.
- **Evidence.** Nothing was sold on any of the 9 launches.
- **Pressure.** LATENT.
- **Arithmetic for a player.**
  - The run's water rose about 957 kg a sol at the end (76 t stored).
  - Sold at 1 cr/kg, that is about 11,500 cr a 12-sol round, or 0.9 of a Q3 award.
  - It fits inside the outbound cap (about 23 t free after a 7 t quota).
  - One Ice Drill and Water Plant pair (180 kg Struct, 550 W) makes about 690 kg a sol, which would sell for about 8,300 cr a round. That is more than the cargo cost of the solar and battery kits that power it.
- **Proposal.** Keep L = 3 if ice is meant to be a cash crop. If not, use 67 and 400 g/kg:
  - (67 x 12,000 + 400 x 8,000) / 1000 = 4,004 mcr = 4.004 cr/kg, with the 6:1 ratio kept.
  - Water then sells at -0.004 (refused); Struct at 0.996, O2 at 4.0 and H2 at 8.0.
- **Director.** Yes, for both rows: it is one question.

### 17. QuotaShortLaunchSols = 3 sols (line 179)
- **Readers.** WS:456 (load); Export:149 `GetShortLaunchSol` (the met sol + 3); Export:381. Exercised in export XE13.
- **Now.** A ship that cannot load its whole bill may launch short from the met sol + 3. It carries what can be loaded, and the shortfall becomes the next round's arrears.
- **Evidence.** Two ships, both held on oxygen by the breathing floor (#45), went short:
  - Met at 81.63, short at 84.00 (waited 2.37 sols, 484 kg arrears).
  - Met at 93.73, short at 96.00 (waited 2.27 sols, 575 kg arrears).
  - The earth REPORT line: "6 launch(es) waited (6.9 sols in all) ... 2 short".
- **Pressure.** REAL: wait, or go short and carry arrears.
- **Proposal.** Keep (ruled in Q-B (a); 3 = ShipTransitSols).
- **Arithmetic.** The window counts from the whole met sol (81 and 93). While a ship waited, loadable O2 rose about 130-160 kg a sol (1,074 -> 1,449 over 2.37 sols; 1,740 -> 2,035 over 2.27).
  - A 1-sol window sends both ships 2 sols sooner (82.00 and 94.00). The first carries about 800 kg of arrears instead of 484; the second, which inherits them, carries more.
  - A 5-sol window would not have filled round 5 either (859 kg missing at 160 a sol is 5.4 sols). It would go short at 86.00 with about 170 kg of arrears, 2 sols later.

### 18. DayShiftStartFrac = 0.25 (line 180)
- **Readers.** WS:114 (load); WS:116-119 (an invalid window falls back to 0.25..0.75); WS:2776 and 2788 (the daylight share).
- **Now.** The day shift opens at sunrise: RH_Curve_SolarDiurnal is 0 until 0.25 and returns to 0 at 0.75. dayshift.log DS0 pins it.
- **Pressure.** NONE. It is derived from the curve; moving it alone puts machines to work in the dark on the bank.
- **Proposal.** Keep.

### 19. DayShiftDefault_Forge = 1 (line 183)
- **Readers.** WS:2712-2715 `IsDayShiftDefault` (reads `DayShiftDefault_<Def>`, under the master switch `DayShiftDefaultsOn`, WS:122); WS:10738 (`AddBuilding` applies it to every placement path); `RHCommandDeckPanels.cpp`:3746 (the card). dayshift.log DS0 pins it as an exact set.
- **Now.** A new Forge rests at night at idle draw (50 W), with its batch paused. That caps it at 150 kg Struct a sol (300 all-hours; the industry study's "150 kg/sol ceiling").
- **Evidence.**
  - Forge time over the run: working 46.0%, night rest 50.0%, shed 3.2%.
  - The bot never toggles. The guide teaches the toggle only when the grid sheds at night.
  - D5a measured the default's cost on an older colony: first crew at sol 14.4 vs 10.5, Q1 at 6.4 vs 4.2.
- **Pressure.** REAL: a player can switch a Forge to all hours.
  - Gain: +150 kg Struct a sol.
  - Cost: 800 W x 12 h = 9,600 Wh a night, which is 2.4 Battery Banks plus 3.7 Solar Arrays to recharge them. A Solar Array makes 2,610 Wh a sol (the curve's area is 0.3625 of a sol, 8.7 full-sun hours).
  - That is about 2,900 kg of cargo (2,900 cr) for 150 kg more Struct a sol: kg for kg, it pays back in about 20 sols.
- **Proposal.** Keep (ruled in D5a).

### 20. DayShiftDefault_Electrolyzer = 1 (line 184)
- **Readers.** Same as #19.
- **Now.** An Electrolyzer on the shift makes 106.8 kg O2 a sol (213.6 all-hours).
- **Evidence.** 4 Electrolyzers x 106.8 = 427 kg a sol, matching "oxygen made 427" at sol 110. O2 is the late binding line (#12, #17, #45).
- **Pressure.** REAL.
  - All-hours costs 600 W x 12 h = 7,200 Wh a night: 1.8 Battery Banks plus 2.8 Solar Arrays, about 2,200 kg of cargo.
  - It gains 106.8 kg O2 a sol, about 1,280 kg a 12-sol round, two-thirds of round 5's O2 bill.
- **Proposal.** Keep (ruled in D5a, which named this default as the first knob if the opening is too slow).
- **Predicted effect of 0.** More O2 by day and early night. But the run's bank is empty 37% of sol-hours, so much of the extra night draw would be shed, and the gain falls well short of 2x. D5a's older colony with the defaults off: O2 at sol 20 was 1,424 vs 432; machines shed 19% vs 9%.

### 21. HopeUnsupportedMax = 30 points (line 185)
- **Readers.** WS:372 (load); WS:6897. Pins: percapita.log P0, rooms.log R0.
- **Now.** Penalty = 30 x min(1, 3 x deficit share).
- **Evidence.** It never fired ("worst unsupported at once 0"). percapita.log: a fifth on 0.8 rations costs 18.0 at N = 10, 100, 1,000 and 5,000 alike.
- **Pressure.** LATENT.
- **Arithmetic.** A full shortage takes the run's colony from 93-97 to 63-67. That is under the 75 gates (settlers, births, the wave's streak), and tempo falls from 1.25 to 1.08-1.10 (garden yield -12 to -14%).
- **Proposal.** Keep (the C2 brief's numbers).
- **Director.** Yes: how harsh a shortage is.

### 22. HopeUnsupportedSteepness = 3 (line 186)
- **Readers.** WS:373 (load); WS:6897.
- **Now.** One resident in three on no ration costs the full 30; one in seven costs 12.9.
- **Pressure.** LATENT (with #21).
- **Proposal.** Keep.

### 23. HopeJobsMax = 7.5 points (line 187)
- **Readers.** WS:371 (load); WS:6862.
- **Now.** Jobs = 7.5 x min(1, filled seats / pop).
- **Evidence.**
  - 7.5 at pop 4-15. After the wave it was 3.2 (50 seats for 116), flat to sol 110.
  - Growth builds homes and civic rooms, never job seats.
- **Pressure.** MASKED: a 4.3-point loss with no gate effect.
- **Proposal.** Keep (the C2 brief said up to 10; it shipped at 7.5, matching HopeHousingMax). With a milestone cap, these 4.3 points become one of the biggest post-wave levers. 66 more seats (33 Workbenches, 2,640 kg Struct) restore them.
- **Director.** Yes.

### 24. RoomServesResidents = 25 (line 188)
- **Readers.** WS:375 (load, at least 1); WS:6849 (room coverage in Hope); Growth:1155 (growth's choice of civic room); Districts:1250 (the City panel's Services gap).
- **Now.** One room of a type serves 25 residents. A 122-person city needs 5 of each for full cover.
- **Evidence.** Rooms 5 at pop 4-15, 2 just after the wave, 3-4 by the end. Growth built 9 civic rooms (dining and infirmary, alternating) of its 14 projects.
- **Pressure.** ACTIVE: it decides how much of growth's Struct and O2 goes to civic rooms instead of homes.
- **Proposal.** Keep.

### 25. HopeAdjacencyMax = 16 points (line 189)
- **Readers.** WS:374 (load); WS:6892.
- **Now.** Penalty = 16 x offended refuser cells / refuser cells.
- **Evidence.** -4 (2 of 9 cells) at pop 4, -2 (2 of 13) at sol 70, -5 (6 of 21) after the wave, -4 (8 of 29) at the end.
- **Pressure.** MASKED.
- **Proposal.** Keep (16 = the old two-pair penalty).
- **Director.** Yes.

### 26. ComfortRefPop = 8 (line 190)
- **Readers.** WS:376 (load); WS:6872 and WS:12463.
- **Now.** The Furnishings and Views sums are multiplied by 8 / pop before their caps.
- **Evidence.** It is what took furnishings from 6 to 0 as the population went from 4 to 116 (#7).
- **Pressure.** MASKED.
- **Proposal.** Keep. A crew of 8 scores as before C2a, and the per-head target (0.75 comfort a resident) stays affordable in a city.

### 27. ColonistTransportColonists = 20 (line 191)
- **Readers.** WS:329 (load); WS:2540-2559 (the landing).
- **Now.** An 8,000 kg (8,000 cr) manifest item. It lands 20 people and their provisions only if 20 beds are free. It has no life-support check: only the wave and settlers are gated.
- **Evidence.** The bot never buys one.
- **Pressure.** LATENT. For a player it is the fastest source of people: one or two a ship add 20-40, against settlers' 3. It costs a third to two-thirds of an award, at 400 kg a head (the Crew Pod is 600), and risks a shortage.
- **Proposal.** Keep.

### 28. ColonistTransportFoodKgPerColonist = 50 kg (line 192)
- **Readers.** WS:330 (load); WS:2540 (a transport's 1,000 kg); WS:7242 (the wave's provisions); WS:7266 (the wave's food runway); WS:7275 (the wave's food production leg, `GetSupportLeg` with the provisions as extra stock).
- **Now.** Provisions per head for a transport and for the Big Arrival.
- **Evidence.** Food went from 1,229 to 6,228 kg at the wave: 87 sols of the 116 people's draw.
- **Pressure.** ACTIVE, and it disables a gate.
  - The wave's food leg passes when stock + provisions >= LifeSupportDeficitCoverSols (40) x deficit.
  - With 5,000 kg of provisions and no garden, the deficit is 0.62 x (N + 100). So the leg passes for N <= 101, even with an empty larder.
  - The wave's own runway check (10 sols) passes for N <= 706.
- **Proposal: 25 kg** (= 40 x 0.62 = 24.8). The wave then brings exactly its own 40-sol cover, and the colony must cover its own N.
  - A colony of 15 with no garden then needs a larder of at least 352 kg (24.8 x 15 - 20). The run had 1,229.
  - A transport lands 500 kg, 40 sols for its 20 people instead of 80.
- **Predicted playtest effect.** Food after the wave is 3,729 kg (52 sols instead of 87). The REPORT's food low point goes from 57 to 52 sols. No penalty: the garden makes 114.9 against 75.6 drawn, and the bot plans its garden on production, not stock.
- **Also changes.** The transport's Effect text in RH_ManifestItems.csv ("+1000 kg Food (50 kg each)") would read 500 kg (25 kg each). The -scale suite lands 80 transports, so its digest may move.
- **Director.** Yes: how self-sufficient arriving settlers should be. The coupling with #53 (bal2) means that if the cover changes, this should follow.

### 29. BirthRefPop = 25 (line 193)
- **Readers.** WS:331 (load, at least 1); WS:7042 (the birth streak runs at max(1, N / 25)); WS:7078 (alert style).
- **Evidence.**
  - 2 births in the 61 sols before the wave (sols 44.06 and about 64.5): one per 20 sols at Hope 75+, with a free bed and 6 sols of food.
  - 7 births in the 29 sols after it: x4.6 at 116 people, one per 4.3 sols.
- **Pressure.** ACTIVE (a rate).
- **Proposal.** Keep.

### 30. ImmigrationHopeMin = 75 Hope (line 194)
- **Readers.** WS:332 (load); WS:7107 and WS:7119; Districts:1130.
- **Evidence.** Smoothed Hope was 99.9-100 at the three settler ships and about 94 at the two after the wave.
- **Pressure.** NONE today.
- **Proposal.** Keep (Q5a, "a thriving city": 75 = THRIVING).

### 31. ImmigrationHopeFloor = 70 Hope (line 195)
- **Readers.** WS:333 (load); WS:7123-7125.
- **Now.** Settlers = floor((Hope - 70) / 30 x max(4, 0.2 N)).
- **Evidence.** A small colony at Hope 99.9 gets floor(3.99) = 3. After the wave at about 94: floor(18.6) = 18 wanted, 0 room.
- **Pressure.** ACTIVE: it sizes each ship.
- **Proposal.** Keep. With Min 75 and Floor 70, a small colony's first settler needs Hope 77.5 or more.

### 32. ImmigrationBase = 4 settlers (line 196)
- **Readers.** WS:334 (load); WS:7124.
- **Evidence.** 3 a ship, 9 in all. Smoothed Hope stays a hair under 100, so the floor gives 3, never the 4 the note promises.
- **Pressure.** ACTIVE.
- **Proposal.** Keep. If 4 is meant: fix the formula's rounding (code), or use 4.2, which gives 4 from Hope 98.6.

### 33. ImmigrationPerCapita = 0.2 (line 197)
- **Readers.** WS:335 (load); WS:7124.
- **Evidence.** After the wave, 0.2 x 116 = 23.2 at full draw, so 18 were wanted at Hope 94. None came: the comfortable-room cap (comfort capacity - pop <= 0) held them. GROWTH reads "housing short 58-74 (Comfort)".
- **Pressure.** ACTIVE: it sizes post-wave demand; housing holds it.
- **Proposal.** Keep.

### 34. ImmigrationCapPerShip = 50 (line 198)
- **Readers.** WS:336 (load); WS:7126.
- **Now.** It binds only from 250 people (0.2 x 250).
- **Pressure.** NONE.
- **Proposal.** Keep.

### 35. ImmigrationReserveBeds = 1 bed (line 199)
- **Readers.** WS:337 (load); WS:7133; Districts:1135 and 1138.
- **Evidence.** Births resumed after the wave.
- **Pressure.** NONE.
- **Proposal.** Keep.

### 36. BigArrivalHopeMin = 75 Hope (line 200)
- **Readers.** WS:338 (load); WS:7218.
- **Evidence.** The streak ran from sol 24.1 to the landing (56.9 sols) without a reset. The crises (-6) never took Hope below 89.
- **Pressure.** NONE.
- **Proposal.** Keep (ruled in Q7a, "THRIVING").

### 37. BigArrivalStreakSols = 20 sols (line 201)
- **Readers.** WS:339 (load, as `BigArrivalStreakSolsNeeded`); WS:7224; Districts:1306.
- **Evidence.** Met by about sol 44; the wave then waited 37 more sols on beds.
- **Pressure.** NONE.
- **Proposal.** Keep (ruled in Q7a, "about 20 sols").

### 38. BigArrivalFreeBeds = 100 beds (line 202)
- **Readers.** WS:340 (load); WS:7224; Districts:1140, 1144 and 1306.
- **Evidence.** The binding gate:
  - GROWTH "housing short (BigArrival)": 91 at sols 28-30, 75 at 31, 65-73 on sols 40-69, 57 at 70, 24 at 75-79, 8 at 80-81.
  - Four Homes batches (sols 29, 68, 78 and 80, 16 beds each) closed 64 of it.
  - The garden floor closed most of the rest. The bot carved floor -3 from 10 to 46 cells on sols 70-73, and the shortfall fell 57 -> 24 (36 cells less the 3 settlers of sol 72.46). Every carved cell on a rated floor counts as one legacy bed (ColonistsPerCell 1, `CountFloorBedsImpl`) unless it is a Living cell with bed pieces.
  - The wave landed at sol 81.00.
- **Pressure.** REAL: the player invests in 100 empty beds for a 100-person windfall. At growth's rates that is 6.25 batches: about 4,000 kg Struct and 2,500 kg O2. Any carved cell does the job at one bed a cell, though. That is why the city ended with 145 beds but comfortable room (Living beds) for only about 80, and why housing Hope halved at the wave (8 -> 4) and post-wave settlers were held by comfort (#33). That is a rule point, not a number in this chunk.
- **Proposal.** Keep (ruled in Q7a). It must stay at least BigArrivalColonists, or part of the wave has no bed.

### 39. BigArrivalColonists = 100 (line 203)
- **Readers.** WS:341 (load); WS:7224, 7236, 7257-7258, 7266 and 7275; Districts:1139.
- **Evidence.** Population went from 15 to 115: the wave is 82% of the final 122. The post-wave strain:
  - smoothed Hope 98 -> 89;
  - jobs 7.5 -> 3.2, housing 8 -> 4, rooms 5 -> 2;
  - settlers held by comfort.
- **Pressure.** ACTIVE.
- **Proposal.** Keep (ruled in Q7a, "about 100").

### 40. BigArrivalLifeSupportSols = 10 sols (line 204)
- **Readers.** WS:342 (load); WS:7262 (the wave's O2 stock); WS:7414 (the settlers' O2 stock leg).
- **Evidence.** At N = 15 the wave needs (15 + 100) x 0.75 x 10 = 862 kg in the pool; sols 76-81 held 1,614-2,350.
- **Pressure.** NONE.
- **Proposal.** Keep. Note that GrowthReserveO2Sols (#45, also 10) protects only N x 0.75 x 10 = 112 kg at N = 15, so this is the stricter check before the wave.

### 41. MoveHomeIntervalSols = 1 sol (line 205)
- **Readers.** WS:348 (load); WS:7456-7466.
- **Evidence.** Nobody needed a new home in the run.
- **Pressure.** NONE.
- **Proposal.** Keep.

### 42. RoadStructKgPerMeter = 2 kg/m (line 209)
- **Readers.** WS:287 (load); Roads:162 (the order's bill, taken whole); Roads:457 and 497 (refunds). roads.log R0 pins it.
- **Evidence.**
  - 6 legs, 223 m for 445 kg. The outpost's 635 m of road was 1,270 kg of its 1,749 kg program (73%).
  - 859 m in all is about 1,716 kg: 4.3% of the 39,725 kg made.
  - The spine waited on Struct at sol 2 ("28 kg needed, 0 on hand") and sol 4 ("spare after the leg -93").
  - Struct held the outpost back 7 sols (41-47).
- **Pressure.** REAL, but light.
- **Proposal.** Keep. A 40 m edge costs 80 kg (two Pylons). Doubling it would add about 1,270 kg to the outpost (about 4-5 more sols held) and no new decision.

### 43. RoadBuildMetersPerHour = 30 m per sol-hour (line 210)
- **Readers.** WS:288 (load, at least 0.1); Roads:417 (x Toolkit and workstation multipliers).
- **Evidence.** The Fabricator was idle 62.8% of the run and building 17.5%.
- **Pressure.** NONE.
- **Proposal.** Keep.

### 44. GrowthReserveStructKg = 1000 kg (line 213)
- **Readers.** WS:350 (load); Growth:221. Exercised in the city-growth suite.
- **Now.** Growth's free Struct = held - (1 - share) x the protected bill - owed - 1,000.
- **Evidence.**
  - Homes was short of Struct on 43 sols.
  - A static model from the 99 GROWTH lines counts the sols with at least 640 kg free (one Homes batch):
    - 17 sols at 1,000. The first four are 29, 68, 78 and 80: exactly the sols on which the Homes batches before the wave started.
    - 28 sols at 500 (from sols 27-29, 52, 59, 67).
    - 40 sols at 0.
    - 10 sols at 1,500, none before sol 92, so no beds for the wave by sol 110.
  - E3b: a reserve of 500 or 0 landed the wave at sols 77-80 but cut Q3 rounds to 1 (before industry=2).
- **Pressure.** ACTIVE: the strongest brake on the Big Arrival in this chunk.
- **Proposal.** Keep 1,000, the tested passing point with industry=2. It is on the director's E3b lever list.

### 45. GrowthReserveO2Sols = 10 sols (line 214)
- **Readers.** WS:351 (load); Growth:225 (growth's breathing floor, N x 0.75 x 10); Export:110 `GetLaunchFloorKg` (Earth never takes this much O2, water or food).
- **Evidence.**
  - Homes was short of O2 on 27 sols: sols 21-35 by the quota's O2 bill, and 88-108 by the bill plus an 878-915 kg reserve.
  - Both short launches were this floor: 862 kg kept at sol 81.63, 870 at 84.00 and 885-892 at sols 93-96.
- **Pressure.** ACTIVE.
- **Proposal.** Keep: one safety floor shared by growth and Earth. Options:
  - **5 sols:** arrears 49 and 128 instead of 484 and 575. Seven more late sols with at least 400 kg O2 free (92-94, 105-108), which is about 2 more Homes batches.
  - **15 sols:** arrears about 919 and 1,020, and growth O2-blocked on nearly every late sol.

### 46. GrowthModuleSols = 1.0 sol (line 215)
- **Readers.** WS:352 (load); Growth:1096.
- **Evidence.** No surface modules were built in the run.
- **Pressure.** NONE.
- **Proposal.** Keep.

### 47. GrowthCellSols = 1.0 sol (line 216)
- **Readers.** WS:353 (load); Growth:736 and 853.
- **Evidence.** The five 4-cell batches took 1.00 sol each. At most one project starts per sol boundary, so a duration of 1 sol or less never slows the cadence; material does.
- **Pressure.** NONE.
- **Proposal.** Keep.

### 48. GrowthRoomSols = 0.5 sol (line 217)
- **Readers.** WS:354 (load); Growth:1355.
- **Evidence.** The nine civic rooms took 0.50 sol each.
- **Pressure.** NONE.
- **Proposal.** Keep.

### 49. GrowthCellsPerProject = 4 cells (line 218)
- **Readers.** WS:355 (load); Growth:724 and Growth:819 (clamped to `RH::MaxGrowthPoints`).
- **Now.** A Homes batch is 4 cells x 2 bunks x 2 beds = 16 beds. It costs 640 kg Struct (8 bunks x 80) plus 400 kg O2 (4 x O2FillKgPerCell 100), taken whole at the start.
- **Evidence.** The late O2 block is exactly this 400 kg.
- **Pressure.** ACTIVE: it makes growth lumpy.
- **Proposal.** Keep (= MinLivableCells 4). A 2-cell batch would start on 6 more of the O2-blocked sols (at least 200 kg free), but builds the same beds per kg.

### 50. GrowthBunksPerHome = 2 (line 219)
- **Readers.** WS:356 (load, clamped 0..4); Growth:334.
- **Now.** 4 beds a cell; up to 4 bunks (8 beds) fit.
- **Evidence.** Comfort capacity is bed-weighted (`LivingCellWeight`), so a denser home costs no Hope. 3 bunks would give 24 beds a batch for the same 400 kg O2 (16.7 kg O2 a bed instead of 25).
- **Pressure.** ACTIVE.
- **Proposal.** Keep, for the city's look. Crowding has no sim cost; that is a missing mechanic, not a number.

### 51. GrowthCityShareDefault = 0.5 (line 220)
- **Readers.** WS:346-347 (load, sets `CityShare`); WS:15311 (load repair); Growth:218 (QuotaBill = (1 - share) x FullBill).
- **Evidence** (the documented studies):
  - 25%: meets 6 rounds and does not grow.
  - 50% with industry=2: population 122, wave at sol 81, 7 rounds (this run, "share 50% city").
  - 75%: grows, but meets only 1-2 rounds.
- **Pressure.** REAL: the player's political dial.
- **Proposal.** Keep (ruled in C5d; confirmed as "the balanced share" in the industry ruling).

### 52. ProductionWindowSols = 3 sols (line 221)
- **Readers.** WS:345 (load, clamped 1..ProdMaxWindow); WS:7326 `GetProductionPerSol`; Econ:112.
- **Evidence.** The two held ships read 0.8 and 3.0 kg of food a sol against 2.5 and 3.1. The garden was too small; the window did not decide it.
- **Pressure.** NONE (smoothing).
- **Proposal.** Keep.

## Cost of a change

- **Every change has three parts.** Change the CSV row. Change the C++ fallback default in `RHSimWorldSubsystem.h`, which must equal the CSV (the D3a rule). Re-sync DT_Config headlessly.
- **Then the pin fails, on purpose.** Almost every row has a pin line that fails on drift: the "DT_Config pins" lines in arrivals, percapita and rooms; food F0; resupply R0; credit CR0; demolish D1; roads R0; icelow I1; depth DP0; dayshift DS0 (an exact set). The pin must be updated.
- **Suites that read each row** (test sources; the expected moves):
  - #1: dome, surfacehab.
  - #2: depth, deepdistricts, districts, -scale.
  - #3, #4: icelow, prefab, farice.
  - #6: demolish, roads, traffic.
  - #7, #8, #26: furnish, walls, move, percapita.
  - #9-#11: food, water, arrivals, percapita.
  - #12: the resupply R0 pin, plus the credit, economy and export lines after they restore the configured value. The hand-set 1.25 ladders in resupply, citygrowth and export XE8 do not move.
  - #13, #14: credit.
  - #15-#17: export.
  - #18-#20: dayshift. The prefab log moves too: the Forge on the shift took 26 steps instead of 16 in D5a's diff.
  - #21-#25: crew, percapita, rooms; -scale for #23 and #25; furnish for #23; surfacehab for #23 and #25.
  - #27-#41: arrivals, districts.
  - #42, #43: roads, traffic.
  - #44-#51: citygrowth; #45 also atrium; #47 also depth.
  - #52: arrivals, citygrowth.
- **Hope weights move far more than their own suites.** C2a's per-head change moved 14 logs: agri, alive, crew, demolish, discovery, ending, food, furnish, growth, hopedrive, rooms, surfacehab, tiers and water (`c2a-moved-lines.md`). The fails-loudly fixtures in growth, discovery and tiers had to be re-derived where a 75 or 85 gate was crossed. Expect 10-15 logs for any one of #7, #8, #21-#26.
- **The playtest baseline.**
  - A NONE or LATENT row changes no decision in the run, but some still move its lines when changed (timings, alert sols). Predicted byte-identical for a moderate change: #1, #2, #4, #5, #6, #8, #9, #10 (at 15), #11 (at 20), #14, #15, #16, #21, #22, #27, #34, #41 and #46. These are never exercised, crossed or reached.
  - Not identical even though no decision hangs on them:
    - #3: the bot surveys for Ice_B on the RUNNING LOW cue (`RHPlaytest.cpp`:2094), so a new fraction moves its whole water expansion.
    - #18: the day-shift window.
    - #43, #47 and #48: when roads and growth finish.
    - #52: the measured production in the GROWTH lines.
    - #30, #35-#37 and #40, if moved past the run's margins (Hope 94+ at landings, a streak of 56.9 sols, an O2 pool about twice the need).
  - Rows that are active in the run move it. That covers #12, #13, #17, #19, #20, #23-#26, #28, #29, #31-#33, #38, #39, #42, #44, #45 and #49-#51. Through tempo (garden yield), any Hope weight also moves the sols where Hope is under 100 (20-51 and 82-110).
  - #28 at 25 moves every food figure from sol 81, with no behaviour change predicted.

## What waits for James's own hand-play

- The Hope weights (#7, #8, #21, #23, #25), after the Milestones decision. The run cannot show their feel: it never leaves FLOURISHING after sol 38.
- The forgiving-or-not questions the bot never asks: salvage (#6), and whether water is a cash crop (#15, #16).
- Whether the day-shift defaults (#19, #20) are taught well enough for a player to use the toggle. The bot never touches it, and it is the cheapest O2 and Struct lever in this chunk.
