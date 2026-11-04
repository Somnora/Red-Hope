# bal3: placeholder rows 105-154 (2026-11-03)

Read-only analysis. HEAD `6df7fcc`. Nothing in the repo was changed.

**Scope.** `docs/data/RH_Config.csv` has 154 rows whose Notes say "Placeholder, director review". This file covers the 105th to the 154th of them, in file order: CSV lines 274-323, from `TramPlacesPerSol` to `AtriumLeakPermille`. That is 50 rows in six systems:

| System | Rows | Placeholder numbers |
|---|---|---|
| D6d trams with people | `TramPlacesPerSol` | 105 |
| D6e lift and tram power | `LiftCarW`, `TramWPerCell`, `TransitLoadPriority` | 106-108 |
| E4 the Martian market (prices, convoys, goodwill) | 14 rows | 109-122 |
| E5 the Martian currency (the mark, the rate, the desk) | 16 rows | 123-138 |
| E6 the levers (pacts, tariffs, sabotage) | 13 rows | 139-151 |
| D7 atriums | `HopeAtriumMax`, `AtriumFillPermille`, `AtriumLeakPermille` | 152-154 |

**Evidence used.**
- Code: `git grep -P` at HEAD; each reader is given as file:line. "W" below is `Source/RedHopeSim/Private/RHSimWorldSubsystem.cpp`, "M" is `RHSimMarket.cpp`, "C" is `RHSimCurrency.cpp`, "L" is `RHSimLevers.cpp`, "A" is `RHSimAtrium.cpp`, "T" is `RHSimTransit.cpp`, all in `Source/RedHopeSim/Private/`.
- The scripted playtest: `Saved/RHPlaytest/idle_r2_full.txt` (110 sols, guide order, districts, industry=2, the first outpost; **not** `lifts=1`). Cited by line number.
- The battery: `Saved/baselines/battery_post_idle/` (`trams.log`, `transitpower.log`, `market.log`, `currency.log`, `levers.log`, `atriums.log`; every check line reads "ok"). Cited by log:line.
- The designs and their DIRECTOR ANSWERs: `docs/qa/2026-10-22/d6-caverns-design.md` (D6d, D6e), `docs/qa/2026-10-24/e4-martian-market-design.md`, `e5-martian-currency-design.md`, `e6-levers-design.md`, `docs/qa/2026-10-23/d7-atriums-design.md`, and the follow-ups in `docs/qa/2026-10-26/orchestrator-handoff.md`.
- Older, supplementary: the D6e gate's `lifts=1` run at 200 W (`Saved/RHPlaytest/d6e_lifts_final_rep.txt`) and its 0 W control (`d6e_lifts_w0_rep.txt`), 2026-10-29, an older binary.
- Two small integer models I wrote for this analysis (Python, not in the repo): E4's repricing and E5's rate step. Each reproduces the suites' and the designs' own figures; see "The models" below.

---

## The short answer

1. **None of these 50 rows does anything in the scripted 110-sol playtest.** The bot never fits a lift car (`lifts=1` is opt-in and was not used), never lays a tram, never sends a convoy, never answers Earth, never signs a pact, never sabotages and never opens an atrium. `Source/RedHope/Private/RHPlaytest.cpp` has a program for none of these except `lifts=1`. Earth's first demand only arrives at sol 100.25 (`idle_r2_full.txt:1558`) and is left unanswered. So every row here is inert in that run: a change to any of them should leave `idle_r2_full.txt` byte-identical (inferred from the code paths and from the suites' own inertness checks: MX1 and MX10, CU1 and CU11, LV1 and LV10, AT0, TM8, and D6e's "with no car nothing is appended"). It also means the playtest cannot show what any of them does.
2. **For a hand player, the systems differ a lot:**
   - **Trade (E4, 14 rows): a real, mild trade-off.** The standing trade (100 kg Struct out, 150 kg Ice in) pays 50 cr on the first run and about 828 cr a run from sol 60: 19,599 cr over 110 sols in the model, about a sixth of the quota awards. It costs 100 kg of Struct a run, and Struct "held the program back 77 sols" in the playtest (`:1686`). Selling big crashes the price to its floor, so "sell small and often" against "sell big" is a real choice.
   - **The mark (E5, 16 rows): reachable only from about sol 100**, after the first demand. It is a political statement with a price: about 16,000 cr of Earth awards lost over sols 100-199, against +3,574 cr (standing trade) or +7,369 cr (heavy trader) from the mark's rise.
   - **Pacts (E6, 8 rows): close to dominant once possible (about sol 23.5 for a trader).** 15 Influence and +5 tension buy embargo immunity and double cargo. Influence piles up: its only other use is Pacify, and Earth cannot embargo a partner.
   - **Betrayal and sabotage (5 rows):** the penalties are coherent, but sabotage never pays a colony that trades with its target (it loses one run, about 720 cr).
   - **Lift and tram power (3 rows): a real draw with no visible capacity penalty.** Shaft loads never pass the hoist's 200 places, so a shed lift costs nothing you can see.
   - **Trams (1 row): nothing to gain at today's sizes.** No commute in the playtest is anywhere near the 300 m long-commute line (the longest trip is 120 m, the commute's Hope cost 0.000).
   - **Atriums (3 rows): dominated while Hope saturates.** The air cost is real (900 kg of oxygen up front for a 3 x 3 x 3 atrium, while growth was "0 free" for oxygen at sols 100 and 105), and the Hope gain is at most +3 in a colony sitting at 95-96.
3. **Proposals: keep 46 of the 50.** Four need the director's taste: `CurrencyIssueTension` (the E5 banner re-raise he asked to revisit), `PactTension` (option 10: make a pact a visible provocation), `HopeAtriumMax` (decide after the Hope rebalance) and `AtriumLeakPermille` (option 1500: let a big atrium fit a 120-person colony's air).
4. **What a gate on these rows costs.** No playtest line moves, for any of them. Each changed row moves its suite's pin line and the fixtures that print its derived numbers: from about 2 battery lines (`MaxConvoysOut`, `PactMinRelation`) to roughly 40-70 for any E4 repricing row (market, currency, levers and regionready all print prices). Every change also needs the DataTable re-imported and the suite's hard-coded expected value edited (for example `RHSimMarketSuite.cpp:455-457`, `RHSimCurrencySuite.cpp:446-449`, `RHSimLeversSuite.cpp:383-386`, `RHSimAtriumSuite.cpp:273-274`, `RHSimTramSuite.cpp:299`, `RHSimTransitPowerSuite.cpp:226-228`).
5. **The pressure for these systems will not come from values.** It needs a player who uses them: a playtest variant that trades, answers Earth, signs a pact, fits lifts, lays a tram and opens an atrium, run past sol 110, or James's own hand-play.

---

## All 50 rows at a glance

"Pressure" is for a hand player. In the scripted playtest every row has none. "Lines" is a rough count of battery lines a change would move; the playtest moves 0 for every row.

| # | Row | Value | System | Pressure for a hand player | Proposal | Director | Lines |
|---|---|---|---|---|---|---|---|
| 105 | TramPlacesPerSol | 600 places | D6d | none: no queue below 300 riders a line | keep | no | ~3 |
| 106 | LiftCarW | 200 W | D6e | weak: a real draw, no visible cost | keep | no | ~15 |
| 107 | TramWPerCell | 5 W | D6e | none (no trams) | keep | no | ~4 |
| 108 | TransitLoadPriority | 6 | D6e | none visible | keep | no | ~5 |
| 109 | MarketWindowSols | 5 sols | E4 | mild, via the trade's price | keep | no | 40-70 |
| 110 | MarketMaxStepBp | 500 bp | E4 | mild (speed only) | keep | no | 40-70 |
| 111 | MarketGainPct | 20 pct | E4 | mild (speed only) | keep | no | 40-70 |
| 112 | MarketFlowWeight | 50 | E4 | mild | keep | no | 40-70 |
| 113 | MarketStoreWeight | 30 | E4 | mild (a strong lever) | keep | no | 40-70 |
| 114 | MarketAnchorWeight | 50 | E4 | mild (the strongest lever) | keep | no | 40-70 |
| 115 | MarketTargetFillBp | 5000 bp | E4 | mild | keep | no | 40-70 |
| 116 | MarketFloorBp | 2500 bp | E4 | real for a bulk seller | keep | no | 10-20 |
| 117 | MarketCeilBp | 40000 bp | E4 | none: never reached | keep | no | 5-10 |
| 118 | MarketMinVolumeG | 1,000,000 g | E4 | mild (binds for Struct) | keep | no | 40-70 |
| 119 | MarketRivalOnly | 0 | E4 | none: a fallback switch | keep 0 | no | 40-70 |
| 120 | MaxConvoysOut | 3 | E4 | none: never binds | keep | no | ~2 |
| 121 | ConvoyCargoKg | 1000 kg | E4 | real for a bulk seller | keep | no | ~3 |
| 122 | GoodwillMinValueMcr | 300,000 mcr | E4 | none (anti-farming) | keep | no | ~3 |
| 123 | CurrencyIssueIdentity | 20 | E5 | none before sol ~100 | keep | no | ~2 |
| 124 | CurrencyIssueTension | 10 | E5 | real, late | keep 10 + code fix, or 6 | **yes** | ~6 |
| 125 | CurrencyRequisitionPenalty | 0.10 | E5 | real, late (the mark's price) | keep (ruled) | no | ~5 |
| 126 | CurrencyRateStartMcr | 1000 mcr | E5 | none: a unit | keep | no | ~20 |
| 127 | CurrencyWindowSols | 5 sols | E5 | slight, late | keep | no | ~10 |
| 128 | CurrencyMinVolumeMmk | 5,000,000 mmk | E5 | slight, late | keep | no | ~10 |
| 129 | CurrencyTradeWeight | 40 | E5 | slight, late | keep | no | ~10 |
| 130 | CurrencyIdentityWeight | 30 | E5 | slight, late | keep | no | ~10 |
| 131 | CurrencyTensionWeight | 30 | E5 | slight, late | keep | no | ~10 |
| 132 | CurrencyAnchorWeight | 50 | E5 | slight, late | keep | no | ~10 |
| 133 | CurrencyGainPct | 20 | E5 | none (speed only) | keep | no | ~10 |
| 134 | CurrencyMaxStepBp | 200 bp | E5 | none (binds only at the issue) | keep | no | ~10 |
| 135 | CurrencyFloorBp | 2500 bp | E5 | none: not reached in modelled play | keep | no | ~3 |
| 136 | CurrencyCeilBp | 40000 bp | E5 | none: unreachable | keep | no | ~3 |
| 137 | DeskSpreadBp | 300 bp | E5 | slight (bounds speculation) | keep (ruled) | no | ~6 |
| 138 | DeskCapPerSol | 500 | E5 | slight (bounds speculation) | keep (ruled) | no | ~3 |
| 139 | PactMinRelation | 50 | E6 | a timing gate (5th trade) | keep | no | ~2 |
| 140 | PactInfluenceCost | 15 | E6 | weak: Influence piles up | keep | no | ~3 |
| 141 | PactTension | 5 | E6 | weak: the pact is near-dominant | keep 5 or **10** | **yes** | ~3 |
| 142 | PactHumanNatureShift | 3 | E6 | slight | keep | no | ~2 |
| 143 | PactCargoKg | 2000 kg | E6 | none for a Struct-starved colony | keep | no | ~4 |
| 144 | PactBreakRelationHit | 25 | E6 | real if you break | keep | no | ~2 |
| 145 | PactBreakHumanNatureShift | 10 | E6 | real if you break | keep | no | ~2 |
| 146 | PactBreakTension | 5 | E6 | slight | keep | no | ~2 |
| 147 | PactBetrayalTariffBp | 2500 bp | E6 | real if you break (ruled) | keep | no | ~5 |
| 148 | PactBetrayalTariffSols | 30 sols | E6 | real if you break (ruled) | keep | no | ~4 |
| 149 | TariffMaxBp | 9000 bp | E6 | none: a safety cap | keep | no | ~7 |
| 150 | SabotageMarketSols | 6 sols | E6 | slaved to SabotageDurationSols | keep | no | ~4 |
| 151 | SabotageCutPct | 100 pct | E6 | real, but sabotage never pays a trader (ruled) | keep | no | ~5 |
| 152 | HopeAtriumMax | 3.0 Hope | D7 | none: Hope saturates at 95-100 | keep for now | **yes** | ~8 |
| 153 | AtriumFillPermille | 1500 | D7 | a real cost with no matching gain | keep | no | ~10 |
| 154 | AtriumLeakPermille | 2000 | D7 | a real cost; a big atrium outruns the colony's air | keep, or **1500** | **yes** | ~8 |

---

## D6d trams with people (1 row)

### 105. TramPlacesPerSol = 600 places (CSV line 274)
- **Read by:** W:244-251 (load; a value of 0 or less logs an Error and keeps 600); T:510 (`RollTransit`: each line's wait against `max(1, 600 x Served / 1000)`); T:364 (`GetTramServedCapacityPlaces`); `RHSimTram.cpp:166` (the first line's alert: "each line carries 300 people a sol before a queue forms"); `Source/RedHope/Private/RHCheats.cpp:3108` (`RH.Transit`). Default `RHSimWorldSubsystem.h:4105`. The wait formula is T:247-255.
- **In play:** a line's load is 2 x its riders x the commute ramp. Up to 600 the line adds no wait. Past it the wait is `ceil((load - 600) x 120 / 600)` s (`WaitSecAtDouble` 120), at most 600 s. It is frozen at the sol threshold and added to each rider's commute at 100 cm a second.
- **Evidence:** the playtest lays no line. Its peak population is 122, the peak ramp 0.620, the longest trip 120 m, and the commute's Hope cost 0.000 (`idle_r2_full.txt:1688`). `trams.log:203` (TM0) pins 600. At 160 residents, 116 ride and the load is 232, 39% of 600 (`trams.log:793`, TM3). A queue appears only when the suite forces the capacity to 5 (`trams.log:871`, TM4). `transitpower.log:980` (TP7): 600 at full power; after a dark sol the capacity floors at 1 place and a load of 24 waits the full 600 s.
- **Pressure:** none. A tram cures long commutes, and the playtest has none to cure.
- **Arithmetic:** a queue starts at 300 riders on one line at full ramp (484 at the playtest's ramp of 0.62). TM2's 11,728 cm ride turns "long" (30,000 cm or more) only with a wait of at least 183 s. That needs a load of 600 x (1 + 183/120) = 1,515, which is 758 riders on one line. At 300 places it would be 150 riders to queue and 379 to turn long.
- **Proposal:** keep 600. Revisit only with a playtest that lays trams in a colony of 300 or more. Wording note for the Gate-D review: the alert's "300 people" is exact only at full ramp.
- **Director:** no.

---

## D6e lift and tram power (3 rows)

Context from the playtest: at the end it generates 7,445 W on average against a 3,804 W load (`:1641`). The bank stood empty 983 of 2,640 sol-hours, 37% (`:1711`). A night is 12 dark sol-hours (`:357`). A Solar Array yields about 2,300 Wh a sol (`RH_Buildings.csv`), and a Battery Bank stores 4,000 Wh.

### 106. LiftCarW = 200 W (CSV line 275)
- **Read by:** W:257 (load, clamped at 0 or more); W:8651 (`StepPower`: one load entry per fitted shaft, cars x `LiftCarW`, at `TransitLoadPriority`). If the entry falls in the shed prefix, its short ms go on the shaft's ring (`NoteTransitShortMs`). The next sol's capacity is then `Hoist + (Cars x 1000 - Hoist) x Served / 1000` (T:500). Default `.h:3904`.
- **In play:** each fitted car draws 200 W all sol, as much as an Air Filtration Station. After a night short of power the next sol's capacity falls toward the hoist's 200 places.
- **Evidence:** the playtest fits no car, so nothing is appended. `transitpower.log:183` (TP0) pins 200. `:286` (TP1 b): +200 W for one car, +800 W for four. `:404` (TP2 a): a dark sol gives Served 0, a capacity of 200, and 400 places wait 120 s. The older D6e `lifts=1` run: one car from sol 13, and "waits 0 s" on all 98 lifts lines. The shaft was never crowded, so a shed lift cost nothing visible. Against its 0 W control, the REPORT outcomes moved both ways (the Big Arrival at sol 95.00 with the draw against 101.36 without; machines shed 15% against 29%). That is path noise, not a cost signal.
- **Pressure:** weak. The draw is real, but the capacity penalty never shows.
- **Arithmetic:** 200 W x 24 sol-hours = 4,800 Wh a sol, 2.1 Solar Arrays. 200 W x 12 dark hours = 2,400 Wh, 0.6 of a Battery Bank. A full 4-car shaft draws 800 W (a Forge), 21% of the playtest's end load. At 400 W per car it would be 4.2 arrays a car and 42% of the end load for a full shaft, and still nothing visible until a shaft's load passes 200 places.
- **Proposal:** keep 200.
- **Director:** no.

### 107. TramWPerCell = 5 W (CSV line 276)
- **Read by:** W:258 (load, clamped at 0 or more); W:8661 (one entry per line: chain points x 5 W). Default `.h:3905`.
- **In play:** the shortest legal line (12 points) draws 60 W, and a 30-point line 150 W: 3,600 Wh a sol, 1.6 Solar Arrays.
- **Evidence:** the playtest lays no line. `transitpower.log:184` (TP0) pins it. `:286` (TP1 b): a 14-point line adds 70 W and a 30-point line 150 W.
- **Pressure:** none, since no trams are used.
- **Note:** a dark sol is harsher on a tram than on a lift. After a fully dark sol a line's capacity floors at 1 place (SP1), so any load of 6 places or more waits the full 600 s the next sol: 60,000 cm, twice the long-commute line. A lift falls back to the hoist's 200 places. That is the design (TP7), not this row's value.
- **Proposal:** keep 5.
- **Director:** no.

### 108. TransitLoadPriority = 6 (CSV line 277)
- **Read by:** W:259 (unclamped, like `CoolingLoadPriority`); W:8652 and W:8662 (each entry's priority); W:8730 (where the lift and tram entries merge: after every entry of priority 6 or less). Default `.h:3906`.
- **In play:** the shed order in a shortage is: Forge and Borer (2), Habitat (3), Electrolyzer (4), Lab, Compute and Humidity (5), Ice Drill (6), then lifts by shaft Id, then trams by line Id, then the Water Plant and cooling (7), the Air Filter and Infirmary (8), the Charging Pad (9), the Floodmast (12).
- **Evidence:** `transitpower.log:329-333` (TP1 c and d) prove the order. The playtest has no car.
- **Pressure:** none visible. A shed lift shows only once a shaft's load passes the hoist's 200 places.
- **Proposal:** keep 6. Life support (7-9) stays above transit, and production below.
- **Director:** no.

---

## E4 the Martian market (14 rows)

**What the market does today.** Zarya (active from sol 0) and Meridian (found by a survey: `idle_r2_full.txt:527`, sol 35.87) have economies in region 0. Prices reprice once a sol, whether or not anyone trades. With no trade, Ice slides from 3.00 to 1.896 cr/kg in 10 sols (`market.log:236`, MX1) and reaches its floor (0.75) at about sol 37 in the model, while Struct climbs to about 2 x V (10.0 cr/kg). But with no order, prices change nothing. MX10 (`market.log:1011`) forces every price to its floor or ceiling for 10 sols, and Hope, the population, credit, the ledger and every stock stay bit-identical. The scripted bot sends no convoy, so the whole market is invisible in the playtest.

**What it is worth to a hand player.** The model below runs the standing trade (100 kg Struct out, 150 kg Ice in, every 4 sols from sol 0) through E4's own update with the shipped data.
- The first run pays 50 cr. From sol 60 a run pays about 828 cr, and the 110-sol total is 19,599 cr. This matches E4's DIRECTOR ANSWER: "about 830 cr a run from about sol 60, roughly 15,600-19,600 cr over 110 sols" (the model has no storm, hence the upper figure).
- That is about a sixth to a fifth of the playtest colony's quota awards (Q1 10,000, Q2 12,000 and seven Q3 rounds of 14,000 x a multiplier of about 0.84-0.95, Earth tension's drag at identity 0).
- The cost is 100 kg of Struct, 8 kg of hydrogen and 1 spare part a run. Struct is the playtest's bottleneck: "Struct held the program back 77 sols" (`:1686`); it makes 361 kg a sol (`:1725`).
- A heavy seller (1,000 kg of Struct a run) crashes Struct to its floor at once. The first run pays 5,000 cr, then 1,250 cr a run, 54,858 cr over 110 sols. It needs 250 kg of Struct a sol, 69% of the playtest's output. **Selling small keeps the price; selling big pays the floor.** That is a real choice, set by the floor, the cargo cap and the weights.

**The model's variants** (one row changed at a time; standing trade):

| Variant | Mean run from sol 60 | 110-sol total | Struct at sol 60 (cr/kg) | Ice at sol 30 / 60 |
|---|---|---|---|---|
| **defaults** | **827.8** | **19,599** | **9.317** | **1.155 / 0.750** |
| MarketWindowSols 1 / 3 / 8 | 713.3 / 773.5 / 835.4 | 16,121 / 18,163 / 19,821 | 8.217 / 8.819 / 9.468 | 1.451 / 0.879 ; 1.162 / 0.750 ; 1.155 / 0.750 |
| MarketMaxStepBp 200 / 300 / 1000 | 824.5 / 827.8 / 827.8 | 18,263 / 19,305 / 19,602 | 9.317 (all) | 1.614 / 0.892 ; 1.207 / 0.750 ; 1.155 / 0.750 |
| MarketGainPct 10 / 40 | 816.6 / 831.7 | 17,916 / 20,346 | 9.216 / 9.271 | 1.567 / 0.929 ; 0.865 / 0.750 |
| MarketFlowWeight 25 / 100 | 746.1 / 970.1 | 16,939 / 23,664 | 8.648 / 10.640 | 1.519 / 0.942 ; 0.750 / 0.750 |
| MarketStoreWeight 0 / 60 | 351.9 / 1,128.8 | 8,950 / 26,838 | 6.345 / 12.302 | 2.008 / 1.934 ; 0.750 / 0.750 |
| MarketAnchorWeight 25 / 100 | 1,265.2 / 487.5 | 29,257 / 11,853 | 13.606 / 7.149 | 0.750 / 0.750 ; 1.821 / 1.572 |
| MarketTargetFillBp 3000 / 7000 | 707.1 / 796.5 | 16,840 / 19,108 | 8.116 / 9.330 | 0.854 / 0.750 ; 1.647 / 1.074 |
| MarketFloorBp 5000 | 715.3 | 17,420 | 9.317 | 1.500 / 1.500 |
| MarketCeilBp 20000 | 827.8 | 19,599 | 9.317 | unchanged: the ceiling never binds |
| MarketMinVolumeG 10,000,000 | 662.7 | 14,802 | 8.109 | 1.684 / 1.109 |
| MarketRivalOnly 1 | 452.1 | 11,429 | 6.996 | 1.781 / 1.670 |
| heavy seller, 1,000 kg Struct a run | 1,250 (the floor) | 54,858 | 1.250 | 0.916 / 0.750 |
| heavy seller at a pact's 2,000 kg | 2,500 | 93,798 | 1.250 | 0.916 / 0.750 |

So the step and gain rows set only speed. The weights set the size of the trade's payoff, by up to about 2.5 times either way. The anchor and store weights are the strongest levers. The director's E4 answer was given on the default outcome, so every weight is a "keep" unless he wants trade to pay more or less.

### 109. MarketWindowSols = 5 sols (CSV line 278)
- **Read by:** M:105 (clamped to 1..8, the ring); M:766-767 (W, and how far back the tally is read); M:785-786 (Supply = rival production x W + the colony's sales; Demand = rival use x W + the colony's buys).
- **In play:** how many completed sols of the colony's own sales and buys count against W sols of rival production in the flow term. A sale depresses its price for 5 sols.
- **Evidence:** pinned by MX0 (`market.log:190`); the band-exact paired runs MX8 and MX13 (`:1513`). No trade in the playtest.
- **Pressure:** none in the playtest; mild for a trader.
- **Arithmetic:** the table: 1 sol gives 713 cr a run, 3 gives 774, 5 gives 828, 8 gives 835.
- **Proposal:** keep 5. **Director:** no.

### 110. MarketMaxStepBp = 500 bp (CSV line 279)
- **Read by:** M:106 (1..10000); M:794 (the step clamp); M:1128 and M:1133 (an order's reservation is quoted a full step against the colony); C:233 and C:238 (the same for mark orders).
- **In play:** a price moves at most 5% a sol, or 1 mcr. An order reserves its buys at 5% above the quote and its sells at 5% below.
- **Evidence:** MX3 (`market.log:405`): in a glut the Ice price falls exactly 5% a sol, the clamp binding all 28 sols, to the 0.75 floor. MX4 (`:420`): in a shortage Struct rises at most 5% a sol, 15 full steps, and settles at 12.579 cr/kg. MX5 (`:435`): 10,010 seeded reprices never step more than 5%. MX6 (`:445`): a reservation of 300 x Hi(3) - 100 x Lo(5) = 470.400 cr.
- **Pressure:** none in the playtest. It sets only how fast prices move.
- **Arithmetic:** 200 bp gives 18,263 cr over 110 sols and leaves Ice above its floor at sol 60 (0.892). 1,000 bp gives 19,602, the same as 500: the clamp binds only in the first sols.
- **Proposal:** keep 500. **Director:** no.

### 111. MarketGainPct = 20 pct (CSV line 280)
- **Read by:** M:107 (0 or more); M:794 (`step = -pressure x 20 / 100`, before the clamp).
- **In play:** how much of a price's pressure becomes its step: the speed of convergence.
- **Evidence:** as for 110.
- **Arithmetic:** 10 gives 17,916 cr and 40 gives 20,346 cr over 110 sols. The resting price is the same (Struct about 9.36 cr/kg at sol 109 in all three).
- **Proposal:** keep 20. **Director:** no.

### 112. MarketFlowWeight = 50 (CSV line 281)
- **Read by:** M:108; M:793 (`Pressure = (Flow x 50 + Store x 30 + Anchor x 50) / 100`).
- **In play:** the weight of the flow term: what the rivals make and use over W sols, plus what the colony sold and bought.
- **Arithmetic:** 25 gives 746 cr a run (16,939 in all); 100 gives 970 cr a run (23,664), with Ice on its floor by sol 30.
- **Proposal:** keep 50. **Director:** no.

### 113. MarketStoreWeight = 30 (CSV line 282)
- **Read by:** M:109; M:790 (`StoreBp = clamp((fill - target) x 2, -10000, 10000)`); M:793.
- **In play:** the weight of the rivals' store fill. Zarya's Struct store drains 80 kg a sol, so its store term saturates at -10000 and pushes Struct up.
- **Arithmetic:** 0 gives only 352 cr a run (8,950 in all); 60 gives 1,129 cr a run (26,838), with Struct at 12.3 cr/kg.
- **Proposal:** keep 30. **Director:** no.

### 114. MarketAnchorWeight = 50 (CSV line 283)
- **Read by:** M:110; M:793 (the pull back toward the Earth value V).
- **In play:** the strongest single lever on the trade's payoff. At rest the pressure is zero, so `AnchorBp = -(FlowBp x 50 + StoreBp x 30) / 50`. For Struct with no trade, a flow of -4,000 and a store term of -10,000 give +10,000 bp: P = 2 V = 10 cr/kg (the model: 9.996). The larger this weight, the closer every price rests to V.
- **Arithmetic:** 25 gives 1,265 cr a run (29,257 in all; Struct 13.6 cr/kg, 2.7 V); 100 gives 488 cr a run (11,853; Struct 7.1).
- **Proposal:** keep 50. This is the knob to use if the director wants trade to pay more or less than his modelled 830 cr a run. **Director:** no.

### 115. MarketTargetFillBp = 5000 bp (CSV line 284)
- **Read by:** M:111 (0..10000); M:790.
- **In play:** the rivals' store fill at which the store term is neutral.
- **Arithmetic:** 3000 gives 707 cr a run (16,840); 7000 gives 797 cr a run (19,108) and keeps Ice off its floor longer (1.074 at sol 60).
- **Proposal:** keep 5000. **Director:** no.

### 116. MarketFloorBp = 2500 bp (CSV line 285)
- **Read by:** M:112 (1..9999); M:326 (`FloorMcr = V x 2500 / 10000`); M:801 (the clamp); the load repairs at M:2032 and M:2409.
- **In play:** Ice sits on its 0.75 cr floor from about sol 37 with no trade (sol 47 with the standing trade buying it). A heavy seller drives Struct to its 1.25 cr floor at once.
- **Evidence:** MX3 (`market.log:405`): the floor is reached at roll 28 and held. MX2 set B (`:364`): the floor holds Ice at 750. MX7 (`:659`): the standing order at both floor prices is worth 237.5 cr.
- **Pressure:** real for a bulk seller. The floor is the price of flooding: at 0.25 V a glut pays 1.25 cr/kg of Struct.
- **Arithmetic:** 5000 (0.5 V) props Ice at 1.50 cr, so the standing trade's late run falls to 715 cr (the colony pays more for Ice), while a heavy seller would earn 2,500 cr a run instead of 1,250.
- **Proposal:** keep 2500. **Director:** no.

### 117. MarketCeilBp = 40000 bp (CSV line 286)
- **Read by:** M:113 (10001..1000000); M:327 (`CeilMcr = V x 4`).
- **In play:** never reached. Struct's shortage price rests at 2.1-2.5 V: MX4 (`market.log:420`) says "the 20 cr ceiling is never reached". In the model, 20000 changes nothing.
- **Proposal:** keep 40000. It is a safety bound. **Director:** no.

### 118. MarketMinVolumeG = 1,000,000 g (CSV line 287)
- **Read by:** M:114 (read as at least 1); M:788 (`FlowBp = (Supply - Demand) x 10000 / max(Supply + Demand, 1 t)`).
- **In play:** it binds for Struct at Zarya. Rival Struct over 5 sols is 200 kg made plus 600 kg used, 800 kg, under 1 t. So Struct's flow is -4,000 bp rather than -5,000 (E4 review), and a 100 kg sale moves it by 1,000 bp.
- **Arithmetic:** 10,000,000 g damps every flow: 663 cr a run (14,802 in all).
- **Proposal:** keep. **Director:** no.

### 119. MarketRivalOnly = 0 (CSV line 288)
- **Read by:** M:115 (0.5 or more is on); M:776 (skips the colony's tally); M:790 (the store term is 0).
- **In play:** off. It is the fallback switch E4's review asked for: on, a price follows only the rivals' data and the sol count (MX8's twin, `market.log:1513`).
- **Arithmetic:** on, the standing trade would pay 452 cr a run (11,429 in all).
- **Proposal:** keep 0. A safety switch, not a balance knob. **Director:** no.

### 120. MaxConvoysOut = 3 (CSV line 289)
- **Read by:** M:116 (1 or more); M:1171-1173 (the refusal "%d convoys are already out"); M:1426 (the log line); `RHCommandDeckPanels.cpp:2375` (the deck's convoy rows).
- **In play:** it never binds. There is one convoy per rival and two rivals in the data, so at most 2 are ever out (E4 review, observation 3). MX6 (`market.log:515`) tests it with injected rivals.
- **Proposal:** keep 3. It matters only when step 4 adds neighbours. **Director:** no.

### 121. ConvoyCargoKg = 1000 kg (CSV line 290)
- **Read by:** M:117 (1 or more, **no upper clamp**); M:1262 (the cap each way unless a pact window covers the order); L:448 (`GetOrderCargoCapKg`); `RHCommandDeckPanels.cpp:3189` and `:3198` (the draft clamp).
- **In play:** the standing order uses 100-150 kg of it. Only written orders approach the cap.
- **Pressure:** real for a bulk seller: a full 1,000 kg of Struct at the floor earns 1,250 cr and takes 2.8 sols of the playtest's Struct.
- **Note:** the int64 bounds in M:44-46 and E5's section 3.3 assume a line of at most 10,000 kg (`PactCargoKg` is clamped there; this row is not). Keep any future value at or under 10,000.
- **Proposal:** keep 1000. **Director:** no.

### 122. GoodwillMinValueMcr = 300,000 mcr (300 cr) (CSV line 291)
- **Read by:** M:118 (0 or more); M:1569 (a written order warms the relation, HumanNature and Influence only if what filled is worth 300 cr or more; the standing order always does).
- **Evidence:** MX7 (`market.log:659`): an order worth 3 cr leaves the relation at 40; one worth 1,100 cr warms it to 42; the standing order at floor prices (237.5 cr) still warms. MX6 (`:467`): a 1,400 cr order warms 40 to 42.
- **Arithmetic:** the smallest Ice buy that counts at the floor is 400 kg (300 cr); 33 kg of Struct at about 9.36 cr counts.
- **Proposal:** keep. It stops ten 1 kg orders from farming goodwill, and a convoy's 4-sol round trip bounds it anyway. **Director:** no.

---

## E5 the Martian currency (16 rows)

**When it can happen.** Only Comply and Defy move the identity axis, by 20 each (`DilemmaDefyAxisShift`). Earth's first demand comes when tension reaches 60. Tension drifts 0.6 a sol from sol 0, so that is sol 100: the playtest's EARTH TRANSMISSION is at sol 100.25 (`:1558`). So the mark is possible at the earliest at about sol 100.5, by a DefyIssue or a Defy then an Issue. In a 110-sol session it is barely reachable, and the scripted bot never gets there.

**What it does once issued.** The rate changes nothing but money. CU11 (`currency.log:1502`) forces the rate to 250, 4,000 or parity, with a billion marks held, for 10 sols, and Hope, the population, Influence, HumanNature, credit and every stock stay bit-identical. Earth's penalty is the real cost (rows 124 and 125).

**The rate model** (E5 section 3.5's assumptions: a DefyIssue at sol 100.5, a Defy at each later demand). It reproduces E5's figures: no trade gives 0.922 at sol 120, 1.009 at 140, 1.269 at 199 (E5: 0.919, 1.004, 1.267). Demands ignored give 0.849, 0.774, 0.684, 0.537 at 110, 120, 140, 199 (E5: the same). With the standing trade to Zarya it gives 1.056, 1.174, 1.436 at 120, 140, 199 (E5: 1.055, 1.175, 1.441).

| Variant (standing trade) | Rate at 110 | at 120 | at 140 | at 199 | Low / high |
|---|---|---|---|---|---|
| **defaults** | **1.045** | **1.056** | **1.174** | **1.436** | **0.972 / 1.454** |
| CurrencyWindowSols 8 | 1.078 | 1.135 | 1.274 | 1.533 | 0.972 / 1.545 |
| CurrencyMinVolumeMmk 1,000,000 | 1.166 | 1.399 | 1.639 | 1.895 | 0.980 / 1.913 |
| CurrencyTradeWeight 80 | 1.119 | 1.178 | 1.325 | 1.600 | 0.980 / 1.624 |
| CurrencyTensionWeight 15 | 1.141 | 1.202 | 1.342 | 1.601 | 0.995 / 1.615 |
| CurrencyAnchorWeight 25 | 1.045 | 1.077 | 1.280 | 1.865 | 0.971 / 1.879 |
| CurrencyGainPct 10 | 1.021 | 1.036 | 1.134 | 1.424 | 0.985 / 1.426 |
| CurrencyMaxStepBp 500 | 1.044 | 1.056 | 1.175 | 1.436 | 0.969 / 1.454 |

In every modelled case the rate stays between 0.54 and 1.91, so the floor (0.25) and the ceiling (4.0) never bind. The rate's whole effect on a standing trader over sols 100-199 is the difference between 24,272 cr (issued) and 20,698 cr (never issued): +3,574 cr (E5 Q-C a). That is small next to Earth's penalty, about 1,820 cr every Q3 round. So the rate rows are second-order and the two Earth rows are first-order.

### 123. CurrencyIssueIdentity = 20 (CSV line 292)
- **Read by:** C:59 (clamp -100..100); C:82 (load); C:105; C:409-411 (the refusal "The colony does not lean Martian yet (identity %+.0f, needs %+.0f)"); `RHCommandDeckPanels.cpp:2781` (the tooltip).
- **In play:** the stand-alone Issue needs the identity at +20 or more. A DefyIssue issues at any identity.
- **Evidence:** CU3 (`currency.log:416`): an Issue at identity +20 is accepted; the refusal text at +10 needs a harness shift, since play moves identity only in steps of 20.
- **Arithmetic:** identity takes the values 0, 20, 40... So any value from 1 to 20 means "after one Defy", and 21-40 means two. A value under 20 changes nothing anyway, because Zarya's `BlocStance` of 20 decides "No neighbour would join the mark" first.
- **Proposal:** keep 20. **Director:** no.

### 124. CurrencyIssueTension = 10 (CSV line 293)
- **Read by:** C:60 (0..100); C:83; C:106; C:476 (tension rises at the act); C:484 (the journal entry); `RHCommandDeckPanels.cpp:2278` and `:2781`.
- **In play:** a DefyIssue nets -2 tension (the Defy's -12, the issue's +10). Earth raises its next demand at 60. So a DefyIssue answered at tension T brings the next demand (62 - T) / 0.6 sols later: 2.8 sols if answered at 60.3, against 19.5 after a plain Defy. **A DefyIssue answered at 62 or more re-raises the demand in the same tick.** That is the "E5 banner" follow-up the director chose to revisit (option a, 2026-10-27): the new alert replaces D44 and Y10 on the one-slot banner, and Y10 says "so tension rises" while the act nets -2.
- **Evidence:** CU3 (`currency.log:400`): DefyIssue against a plain Defy, tension 59.160 against 49.160, the multiplier 0.6025 against 0.7325 (exactly -0.13). CU12 (`:1600`): 25 sols later, 73.36 against 63.36. CU3 (`:416`): a stand-alone Issue takes tension from 10.00 to 20.03.
- **Pressure:** real for a hand player who issues (the next crisis is close), none in the playtest.
- **Arithmetic** (X = this row; answered at 60.3; a Q3 round is 14,000 kg on time):

| X | Re-raise in the same tick if answered at | Next demand after | Multiplier while issued | Awards lost per Q3 round |
|---|---|---|---|---|
| 0 (a plain Defy) | 72 | 19.5 sols | -0.100 | 1,400 cr |
| 5 | 67 | 11.2 sols | -0.115 | 1,610 cr |
| 6 | 66 | 9.5 sols | -0.118 | 1,652 cr |
| 8 | 64 | 6.2 sols | -0.124 | 1,736 cr |
| **10** | **62** | **2.8 sols** | **-0.130** | **1,820 cr** |

- **Proposal:** two options for the director. (a, recommended) **Keep 10**, his Q-A (a), and fix the banner in code (queue the alerts, or delay the re-raise a sol) at E8 or the Gate-D review. (b) **6**: a DefyIssue then re-raises only if answered 10 sols late (at 66), and the next demand comes 9.5 sols later. That softens the mark's political cost by about 170 cr a round.
- **Director:** yes (he asked for this to be revisited "with the tension numbers").

### 125. CurrencyRequisitionPenalty = 0.10 (CSV line 294)
- **Read by:** C:61 (0..1); C:84; C:107; W:10224 (`Mult -= 0.10` while `bCurrencyIssued`, before the clamp).
- **In play:** while the mark exists, Earth's requisition multiplier is 0.10 lower: 1,400 cr less per Q3 round, 1,820 cr with the issue's +10 tension.
- **Evidence:** CU3 (`currency.log:416`): 0.7199, HEAD's expression less 0.10. CU9 (`:780`): a Comply withdraws the mark and the multiplier returns to 0.8197. CU12 (`:1600`).
- **Pressure:** real, late. It is the mark's price.
- **Arithmetic:** sols 100-199 hold about 8.8 Q3 rounds at the playtest's cadence (about 11.3 sols each). At 0.10 the mark costs about 16,100 cr of awards there. The mark's rise gives back +3,574 cr to a standing trader or +7,369 cr to a heavy trader (E5 Q-C a: 24,272 against 20,698, and 38,619 against 31,250). Net: about -12,500 or -8,700 cr. At 0.05 the net is -6,300 / -2,500; at 0.03, -3,900 / about 0; at 0, about 0 / +3,650. So **at any penalty above about 0.03 the mark never pays**. It is an identity act with a price, which is what the director chose.
- **Proposal:** keep 0.10 (the director's Q-A (a), 2026-10-24). If he ever wants the mark to break even for a heavy trader, 0.03. **Director:** no (ruled).

### 126. CurrencyRateStartMcr = 1000 mcr (CSV line 295)
- **Read by:** C:62 (1..1,000,000); C:85; C:108; C:132 (the issue rate); C:183 and C:188 (the floor and ceiling are start x bp); C:686; C:774 (the rate while unissued); C:817 (the reset at withdrawal); C:865 (R0, the anchor).
- **In play:** parity: one mark is one credit at the issue, and the anchor pulls back to it.
- **Proposal:** keep 1000. It is a unit; another value rescales the band and the anchor and buys nothing. **Director:** no.

### 127. CurrencyWindowSols = 5 sols (CSV line 296)
- **Read by:** C:63 (1..8); C:86; C:109; C:310; C:867.
- **In play:** the trade term reads the last 5 sols of the colony's trade with the bloc.
- **Arithmetic:** 8 gives 1.533 at sol 199 instead of 1.436.
- **Proposal:** keep 5. **Director:** no.

### 128. CurrencyMinVolumeMmk = 5,000,000 mmk (5,000 marks) (CSV line 297)
- **Read by:** C:64 (1 or more); C:87; C:110; C:317; C:874.
- **In play:** it binds for a standing trader. One run sells about 936 marks of Struct and buys about 113 of Ice, so a window holds 1,048-2,096 marks, well under 5,000. TradeBp is then 1,645 (one run in the window) to 3,290 (two), a sixth to a third of a full surplus (E5: 1,636-3,272).
- **Arithmetic:** 1,000,000 lets the trade count in full: 1.166 at sol 110 and 1.895 at 199.
- **Proposal:** keep. It is the most consequential rate row, but the director's Q-C answer was given at these numbers. **Director:** no.

### 129. CurrencyTradeWeight = 40 (CSV line 298)
- **Read by:** C:65 (clamped 0 or more); C:88; C:111; C:878 (`Pressure = (Trade x 40 + Identity x 30 - Tension x 30 - Anchor x 50) / 100`).
- **Arithmetic:** 80 gives 1.600 at sol 199.
- **Proposal:** keep 40. **Director:** no.

### 130. CurrencyIdentityWeight = 30 (CSV line 299)
- **Read by:** C:65; C:89; C:112; C:878.
- **Arithmetic:** each Defy (+20 identity, -12 tension) moves the rate's resting point by (2,000 x 30 + 1,200 x 30) / 50 = +1,920 bp, +19% (E5: "+19%").
- **Proposal:** keep 30. **Director:** no.

### 131. CurrencyTensionWeight = 30 (CSV line 300)
- **Read by:** C:65; C:90; C:113; C:878.
- **Arithmetic:** 15 halves Earth's drag: 1.141 at sol 110 and 1.601 at 199, low 0.995.
- **Proposal:** keep 30. **Director:** no.

### 132. CurrencyAnchorWeight = 50 (CSV line 301)
- **Read by:** C:65; C:91; C:114; C:878.
- **Arithmetic:** 25 lets the rate run further: 1.865 at sol 199.
- **Proposal:** keep 50. **Director:** no.

### 133. CurrencyGainPct = 20 (CSV line 302)
- **Read by:** C:65; C:92; C:115; C:879.
- **Arithmetic:** speed only: 10 gives 1.424 at sol 199 against 1.436.
- **Proposal:** keep 20. **Director:** no.

### 134. CurrencyMaxStepBp = 200 bp (CSV line 303)
- **Read by:** C:69 (1..10000); C:93; C:116; C:242-243 (the rate a full step against the colony, for reservations); C:879 (the clamp).
- **Evidence:** CU2b (`currency.log:332`): 10,000 seeded steps never move more than 2% or 1 mcr. CU6 (`:587`): a one-sol round trip at the steepest climb returns at most 0.97 x 0.97 x 1.02 = 0.9597 of the stake.
- **Arithmetic:** 500 gives the same path within 0.003: the clamp binds only in the first sols after the issue.
- **Proposal:** keep 200. It also keeps the desk's one-sol round trip a loss. **Director:** no.

### 135. CurrencyFloorBp = 2500 bp (CSV line 304)
- **Read by:** C:70 (1..9999); C:94; C:117; C:183 (floor = 0.25 cr a mark).
- **In play:** reached only in E5's worst case (identity +20 with the worst trade and tension, a resting point of -12,800 bp). Modelled play never goes below 0.537 (every later demand ignored, sol 199).
- **Proposal:** keep. A safety bound. **Director:** no.

### 136. CurrencyCeilBp = 40000 bp (CSV line 305)
- **Read by:** C:71 (10001..1000000); C:95; C:118; C:188.
- **In play:** unreachable. The highest resting point any input gives is 2.4 (E5 3.3: (10000 x 40 + 10000 x 30) / 50 = 14,000 bp).
- **Proposal:** keep. **Director:** no.

### 137. DeskSpreadBp = 300 bp (CSV line 306)
- **Read by:** C:72 (0..9999); C:96; C:119; C:617 (`Keep = 10000 - 300`); `RHCommandDeckPanels.cpp:2890` and `:2947`.
- **In play:** the bloc's desk keeps 3% of every exchange each way.
- **Evidence:** CU6 (`currency.log:538`, `:587`): 300 cr clear for 280.888 marks at 1.036 cr a mark; the steepest one-sol round trip returns 0.9597 of the stake.
- **Arithmetic:** a speculator needs the rate to climb 1 / 0.9409 = 6.3% before a round trip breaks even: at least 4 sols of maximum climb. E5's modelled speculation over about 100 sols gains +19.9% with the cap (+42% with no cap).
- **Proposal:** keep (the director's Q-C (a): "a 3% spread each way"). **Director:** no.

### 138. DeskCapPerSol = 500 (CSV line 307)
- **Read by:** C:73 (0..1,000,000); C:97; C:120; C:535-537 (the refusal "The desk takes at most %lld %s a sol"); C:1234 (the load check); `RHCommandDeckPanels.cpp:2692`, `:2890`, `:4905`.
- **In play:** at most 500 credits or marks through the desk a sol, each way.
- **Evidence:** CU6 (`currency.log:538`): a second exchange of 201 is refused after 300.
- **Arithmetic:** the standing trade earns about 207 marks a sol (828 every 4 sols), so the cap never binds for it. It binds for speculation and for a heavy trader (1,250 a run).
- **Proposal:** keep (Q-C a). **Director:** no.

---

## E6 the levers (13 rows)

**When a pact is possible.** Zarya starts at relation 40 (`RH_Rivals.csv`), and each completed run adds +2 (`RelationPerRun`), +3 Influence (`InfluencePerTrade`) and +2 HumanNature. Influence also drifts at 1.0 x HumanNature / 100 a sol (W:10372). So the fifth trade, at about sol 23, brings relation 50 and Influence about 16 together, and the first pact is possible at about sol 23.5 (E6 2.3). Influence has no cap. A steady trader holds about 100 by sol 100 (75 from trades and about 25 from drift). Its only uses are a pact (15) and Pacify (20). The scripted bot never trades, so its Influence stays 0 and its relation 40.

**What a pact is worth.** E6's director-answered table: by sol 200 a non-answering trader's standing trade earns 38,238 cr with a pact against 22,508 without (Earth can never embargo the partner). A heavy Struct seller earns 141,386 against 84,093 (double cargo). The costs are 15 Influence, +5 tension and nothing else. So **once possible, the pact is close to a free win.** Breaking it is costly and coherent (rows 144-148). Sabotage never pays a colony that trades with its target.

### 139. PactMinRelation = 50 (CSV line 308)
- **Read by:** L:81 (0..100); L:119; L:148; L:504-506 (the refusal "does not trust you enough for a pact yet").
- **Evidence:** LV5 (`levers.log:1512`): "Zarya Station does not trust you enough for a pact yet (relation 40, needs 50)".
- **Arithmetic:** Zarya needs 5 runs (about sol 23); Meridian, starting at 30, needs 10. A Defy adds +5 to every rival.
- **Proposal:** keep 50. The relation gate and the Influence gate open at the same trade. **Director:** no.

### 140. PactInfluenceCost = 15 (CSV line 309)
- **Read by:** L:82 (0..1000); L:120; L:149; L:509-511 (the refusal); L:560 (the spend); `RHCommandDeckPanels.cpp:2479` and `:2498`.
- **Evidence:** LV5 (`levers.log:1516`): a Sign takes Influence from 40 to 25.
- **Arithmetic:** 15 is reached at the fifth trade. 30 would move the first pact to about sol 37 (Influence about 0.75 t + t² / 400 for a trader). Either way a trader holds about 100 by sol 100, so the cost never binds after the first pact.
- **Pressure:** weak. Influence piles up with no other use until an embargo needs a Pacify, and a pact removes the embargo threat from its partner.
- **Proposal:** keep 15. A higher cost only delays the pact; it creates no lasting choice. **Director:** no.

### 141. PactTension = 5 (CSV line 310)
- **Read by:** L:83 (0..100); L:121; L:150; L:561 (tension rises at the Sign); L:569 (the journal entry, +5,000 for E5's rate); the deck's pact tooltip.
- **In play:** the pact's one real cost. With a pact at sol 23.5, Earth's first demand comes at sol 91.7 instead of 100.0, and the requisition multiplier is 0.015 lower while that tension stands (about 210 cr a Q3 round).
- **Evidence:** LV5 (`levers.log:1516`): tension +5 and one journal entry of +5,000.
- **Arithmetic** (pact at sol 23.5, drift 0.6, demand at 60):

| PactTension | Earth's first demand | Awards lost per Q3 round |
|---|---|---|
| 0 | sol 100.0 | 0 |
| 3 | sol 95.0 | 126 cr |
| **5** | **sol 91.7** | **210 cr** |
| 8 | sol 86.7 | 336 cr |
| 10 | sol 83.3 | 420 cr |
| 15 | sol 75.0 | 630 cr |

- **Proposal:** two options for the director. **Keep 5** (gentle: the pact stays a near-free win). Or **10** (firm): signing brings the Solidarity Dilemma about 17 sols early, inside a 110-sol session, and costs 420 cr a round. Then a pact is a visible provocation that Earth answers, which is the only way in this range to turn the pact into a decision. Keep `PactBreakTension` at 5 in either case (row 146).
- **Director:** yes.

### 142. PactHumanNatureShift = 3 (CSV line 311)
- **Read by:** L:84 (0..100); L:122; L:151; L:562.
- **In play:** +3 toward Evolved, as much as 1.5 fair trades (`HumanNatureFairTradeShift` 2). The Evolved ending needs 30 (`EndingHumanNatureThreshold`).
- **Proposal:** keep 3. **Director:** no.

### 143. PactCargoKg = 2000 kg (CSV line 312)
- **Read by:** L:88 (1..10000, which keeps E5's rate arithmetic inside int64); L:123; L:152; L:448; L:574 (the alert); M:1262.
- **Evidence:** LV5 (`levers.log:1523`): a 1,500 kg order is refused in sol 1 (before the window) and booked in sol 2.
- **Arithmetic:** a heavy seller at 2,000 kg earns 93,798 cr over 110 sols in the model against 54,858 at 1,000 kg. But it needs 500 kg of Struct a sol; the playtest colony makes 361 in all and is Struct-starved. Its water is plentiful (76 t in store at the end), so bigger Ice buys are worth nothing to it either.
- **Pressure:** none for the colony the playtest builds.
- **Proposal:** keep 2000. **Director:** no.

### 144. PactBreakRelationHit = 25 (CSV line 313)
- **Read by:** L:85 (0..100); L:124; L:153; L:586.
- **Evidence:** LV5 (`levers.log:1526`): a Break takes the relation from 60 to 35.
- **Arithmetic:** 35 is under the 50 a pact needs, so re-signing takes 8 more runs (about 32 sols), longer than the 30-sol tariff block.
- **Proposal:** keep 25. **Director:** no.

### 145. PactBreakHumanNatureShift = 10 (CSV line 314)
- **Read by:** L:84; L:125; L:154; L:587.
- **In play:** -10 toward Destructive. Betrayal ranks worst: a sabotage is -8 (`HumanNatureSabotageShift`) and a covert op -6.
- **Proposal:** keep 10. **Director:** no.

### 146. PactBreakTension = 5 (CSV line 315)
- **Read by:** L:83; L:126; L:155; L:588-589 (tension falls; the journal entry -5,000).
- **In play:** Earth approves a break: -5 tension, 8.3 sols of drift, a quarter of a Pacify (-20 for 20 Influence).
- **Arithmetic:** with `PactTension` 5, a sign-and-break cycle nets 0 tension, so it is no tension pump. It costs 15 Influence, -25 relation, -10 HumanNature and about 2,106 cr of tariff.
- **Proposal:** keep 5, even if `PactTension` goes to 10 (a cycle then nets +5, still no pump). **Director:** no.

### 147. PactBetrayalTariffBp = 2500 bp (CSV line 316)
- **Read by:** L:89 (0..9999); L:127; L:156; L:167 (never above `TariffMaxBp`); L:174; L:233 (the rule's markup).
- **Evidence:** LV5 (`levers.log:1526`): 2,500 bp on every resource both ways for sols [4, 34). LV0 (`:210`): the cross-row rule (a cap of 2,000 holds the betrayal tariff at 2,000).
- **Arithmetic:** the standing trade pays 564.7 cr a run instead of 827.9 for 30 sols: 2,106 cr in all (E6 Q-C a).
- **Proposal:** keep (the director's Q-C (a)). **Director:** no.

### 148. PactBetrayalTariffSols = 30 sols (CSV line 317)
- **Read by:** L:90 (1..1000); L:128; L:157; L:235; L:591; L:935.
- **In play:** the tariff runs 30 sols from two sols after the betrayal, and the partner refuses a new pact until it ends (LV5: "until sol 33").
- **Proposal:** keep (Q-C a). **Director:** no.

### 149. TariffMaxBp = 9000 bp (CSV line 318)
- **Read by:** L:91 (0..9999); L:129; L:158; L:167 and L:173 (the cross-row rule); L:233; L:305 (each line's markups sum and clamp here); L:975 and L:1174 (the load's repairs).
- **In play:** only one tariff exists in play, the betrayal's 2,500 bp. So the cap binds only when tariffs stack, which needs the harness: LV4 (`levers.log:1436`) sums two 5,000 bp tariffs to 9,000. Under 10,000, a sale can never become a charge (LV11, `:2422`).
- **Proposal:** keep. A safety cap. **Director:** no.

### 150. SabotageMarketSols = 6 sols (CSV line 319)
- **Read by:** L:92 (1..30); L:130; L:159; L:935; W:10196 (the market window [act + 1, act + 7)).
- **In play:** a sabotage stops the target's production for 6 whole sols. The row must equal `SabotageDurationSols` (6, not a placeholder), which runs the live trade refusal; LV0 checks that they are equal.
- **Proposal:** keep 6, and change it only with `SabotageDurationSols`. **Director:** no.

### 151. SabotageCutPct = 100 pct (CSV line 320)
- **Read by:** L:93 (0..100); L:131; L:160; L:345 (`GetRivalCutPct`); M:736 and M:751 (the cut at the roll); `RHCommandDeckPanels.cpp:2553`.
- **Evidence:** LV6 (`levers.log:1609`): Zarya's Ice reaches 3,801 mcr at the roll closing sol 6, against 2,137 undisturbed. LV10 (`:2421`): Hope, the population, every stock and the relation stay bit-identical; only money and prices move.
- **Arithmetic** (the market model, sabotage at sol 60.5, the six-sol refusal costing one run):

| Cut | Zarya's Ice at its peak | Struct at its peak | The standing trade, sols 60-199 |
|---|---|---|---|
| none | 0.750 | 9.69 | -768 cr |
| 50 | 0.777 | 9.98 | -716 cr |
| **100** | **1.002 (+34%)** | **10.36 (+10.5%)** | **-721 cr** |

(E6's own figures: +34%, +10.5%, -719 cr.) In every option the colony that trades with its target loses money: the refusal costs a run, and the dearer Struct pays back only about 58 cr on the next one.
- **Pressure:** real as a Destructive act, but never profitable for a trader.
- **Proposal:** keep 100 (the director's Q-B (a), "production stops"). **Director:** no.

---

## D7 atriums (3 rows)

**Context from the playtest.** Hope ends at 95-96, FLOURISHING (`:1632`: housing 4, rooms 4, jobs 3, comforts 8, furnishings 1, views 0, **milestones 30**, adjacency -4). Its smoothed range over the run is 55-100 (`:1687`). Oxygen at the end: 427 kg made against 262 drawn and 22 for the quota (`:1640`), a surplus of about 140-165 kg a sol from 4 Electrolyzers (about 107 kg a sol each). Growth itself was short of oxygen late: "needs 400 kg, 0 free" at sols 100 and 105 (`:1556`, `:1593`).

### 152. HopeAtriumMax = 3.0 Hope (CSV line 321)
- **Read by:** W:155 (clamped at 0 or more); W:6950 (`Atrium = 3 x heads facing / population`, the last addend, after Shelter). Default `.h:3966`.
- **In play:** bonus-only. Each head counts once, by home or seat.
- **Evidence:** AT6 (`atriums.log:2108`): 17 of 40 facing gives +1.275. AT12 (`:4397`): Hope 13 with the atrium against 10 without (all facing).
- **Pressure:** none. At Hope 96, the most an atrium can add is +3 (every head facing), to 99. That crosses no band: FLOURISHING enters at 90 and exits at 85. A third of the colony facing gives about +1.
- **Proposal:** keep 3 for now. The atrium's balance depends on the Hope total, which saturates mostly through milestones (30 of 96 points). That belongs to the rows that set the other Hope terms. If the rebalance brings Hope down into the 70-85 band, raise this to **5** (a third of the colony facing then gives +1.7 instead of +1.0, near a band edge) so the atrium's oxygen buys something.
- **Director:** yes.

### 153. AtriumFillPermille = 1500 (CSV line 322)
- **Read by:** W:156 (1000 or more); A:251 (a floor's required oxygen); A:828 (the preview's premium); A:919 (the card); A:1109 and A:1114 (the open's bill and each floor's share).
- **In play:** each open cell needs 1.5 cells of air. The premium, 50 kg of oxygen per open cell, is billed up front through growth's path (refusal 8 if it does not fit).
- **Evidence:** AT7 (`atriums.log:2272`): a 3 x 3 x 3 atrium's premium is 900 kg (18 x 100 x 0.5); AT7 (`:2480`): an 8 x 8 x 3 one's is 6,400 kg. AT1 (`:1221`): refusal 8 names growth's reason.
- **Arithmetic:** a 3 x 3 x 3 atrium's 900 kg is 2.25 of growth's 400 kg home-cell batches, or 5.5-6.5 sols of the end surplus. At sols 100-105 the playtest colony could not have paid it at all ("0 free").
- **Pressure:** a real cost with no matching gain today (row 152).
- **Proposal:** keep 1500. A cheaper atrium is still a dominated choice while its Hope buys nothing. **Director:** no (decide it with row 152 if ever).

### 154. AtriumLeakPermille = 2000 (CSV line 323)
- **Read by:** W:157 (1000 or more); A:262 (a floor's leak); A:829 (the preview); A:848-853 (refusal 9, the air ceiling of 480 kg a sol a floor); A:920 (the card); A:1374 (the headroom).
- **In play:** each open cell leaks twice as fast: +2 kg of oxygen a sol per open cell, for as long as the atrium stands.
- **Evidence:** AT7 (`atriums.log:2272`, `:2276`): the 3 x 3 x 3 atrium adds 36 kg a sol (the colony's standing leak 222 becomes 258). AT7 (`:2480`): an 8 x 8 x 3 atrium adds 256 kg a sol. AT7 (`:2690`): refusal 9 stops a 200-cell upper floor with 64 open cells.

| Fill / leak | Atrium | Premium | Extra leak | Share of the end surplus (165 kg a sol) | Electrolyzers at 107 kg a sol |
|---|---|---|---|---|---|
| **1500 / 2000** | 3 x 3 x 3 | 900 kg | 36 kg a sol | 22% | 0.34 |
| **1500 / 2000** | 8 x 8 x 3 | 6,400 kg | 256 kg a sol | **155%** | **2.4** |
| 1500 / 1500 | 3 x 3 x 3 | 900 kg | 18 kg a sol | 11% | 0.17 |
| 1500 / 1500 | 8 x 8 x 3 | 6,400 kg | 128 kg a sol | 78% | 1.2 |

- **Pressure:** a real, lasting cost. A full-size atrium leaks more than the whole surplus of the colony the playtest builds, so it needs about 2.4 more Electrolyzers (about 1.4 kW more draw while they run) for at most +3 Hope.
- **Proposal:** keep 2000 while atriums buy no Hope. If the director wants a full-size atrium to be a showpiece a 120-person colony can afford, **1500**: it halves the lasting leak, and refusal 9 then counts each open cell as 1.5 cells instead of 2.
- **Director:** yes.

---

## The models

Both are small Python re-implementations, in integers with truncating signed division, written for this analysis. They are not in the repo.

**E4's repricing** (M:709-805). It uses Zarya's and Meridian's shipped columns (`RH_Rivals.csv`) and the Earth values in `RH_Resources.csv`. Each sol it clears the orders at the price in force, runs the rivals' production (with E6's cut when asked), then reprices every slot by M:776-801's formula. It reproduces:
- MX1's Ice 1.896 cr/kg after 10 sols with no order (`market.log:236`);
- MX2 set A's first roll: Ice 3,000 to 2,850 and Water 4,000 to 4,112 (`:354`);
- E4's director-answer figures: 50 cr on the first run, about 830 cr a run from sol 60, and 19,600 cr over 110 sols;
- E6's sabotage figures: Ice +34% at its peak, Struct +10.5%, the standing trade -719 cr (the model: -721).

Limits: no storm (E4's model freezes convoys in Storm_1), a convoy every 4 sols exactly, and the standing order clears at the roll of its departure sol.

**E5's rate step** (C:864-885), at sol granularity. Tension drifts 0.6 a sol from 0; the first demand comes at 60; a DefyIssue answers it half a sol later and a Defy answers each later one; the record is read at each sol's close. With the standing trade, each run sells 935.7 marks of Struct and buys 112.5 of Ice. It reproduces E5 section 3.5's paths to within 0.005, and exactly where demands are ignored (see the E5 section).

---

## What a balance gate on these rows would cost

- **The playtest:** 0 lines for every row here, because none of these systems runs in `idle_r2_full.txt`. By the same token, the playtest cannot judge any change. The `lifts=1` variant (outside the byte-identical set) moves wholesale with `LiftCarW`, as D6e's 0 W control showed, and may move with `TransitLoadPriority`.
- **The battery,** per changed row (rough counts; the "Lines" column above):
  - about 2-7 lines for a row printed only in its suite's pin and one or two fixtures: `MaxConvoysOut`, `ConvoyCargoKg`, `GoodwillMinValueMcr`, `CurrencyIssueIdentity`, the pact and break rows, `TariffMaxBp`, `SabotageMarketSols`, `SabotageCutPct`, `DeskCapPerSol`;
  - about 3-15 lines for the transit rows (`transitpower.log` has 39 check lines; `LiftCarW` touches about 15 of them, TP0-TP5) and the atrium rows (AT0, AT1, AT6, AT7, AT10, AT12: about 8-10 each);
  - about 5-10 lines for the two Earth rows of E5 (CU3, CU7, CU8, CU9, CU12) and each rate row (CU2, CU3, CU5, CU6, CU8, CU9, MX12);
  - **roughly 40-70 lines for any E4 repricing row** (window, step, gain, the three weights, the target, the minimum volume, the fallback switch): 42 of `market.log`'s 82 check lines print a price, a rate or a credit sum, and so do 18 of `currency.log`'s 63, 13 of `levers.log`'s 46 and 9 of `regionready.log`'s 49 (RR4 prints Zarya's one-roll prices and the band).
- **The work beyond the logs:** each change edits the CSV, re-imports `DT_Config` (each suite's x0 check compares the live DataTable with the CSV), and edits the suite's hard-coded expected value. So it is a sim gate with a new battery baseline, not a data-only change.

---

## What must wait for play

- **Every row here needs a player who uses its system.** A "trader" playtest variant would exercise rows 109-151: the standing trade every round trip from sol 0, a pact at the first chance, DefyIssue at the first demand, Defy after it, and a run of about 200 sols so the mark's 100 sols are seen. Rows 105-108 need `lifts=1` plus a tram program (a line on the first finished 12-point tunnel). Rows 152-154 need an atrium program (a 3 x 3 x 3 cavern opened as soon as its oxygen fits).
- **Whether the pact should be a decision** (row 141), **whether the mark's price feels right** (rows 124-125) and **whether an atrium is worth its air** (rows 152-154) are taste calls that hand-play should inform.
- **The atrium rows wait on the Hope rebalance.** While milestones carry 30 points and Hope sits at 95-100, no bonus-only Hope row in any range can create a choice.
