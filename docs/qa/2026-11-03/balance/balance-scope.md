# Balance scope: the 154 placeholder rows (2026-11-03)

For James. On 2026-11-03 you asked whether Red Hope has enough choice and consequence, and whether it is fun, too complicated or too simple. This note scopes a balance pass. It changes no value.

- **Read-only.** No file in the repo was changed. Nothing was built or run in Unreal. The repo is at `6df7fcc`, clean.
- **Sources.** Three analyst files sit beside this one: `bal1.md` (rows 1-52), `bal2.md` (rows 53-104) and `bal3.md` (rows 105-154). The playtest is `Saved/RHPlaytest/idle_r2_full.txt`: 110 sols, guide order, full cargo, districts, industry=2, the first outpost. I re-checked its figures and wrote a small Hope model for this note (see "How the numbers were made").
- **Row numbers** like #28 count only the 154 rows tagged "Placeholder, director review" in `docs/data/RH_Config.csv`, in file order. They match the analysts' numbers.

---

## Your questions, short answers

- **Consequence: too little on the main path.** In the 110-sol run, nothing the colony did cost it Hope or people.
  - Hope stayed between 89 and 100 from sol 37 to the end.
  - No shortage penalty fired on any sol.
  - Food never fell below 57 sols of stock.
  - The commute cost 0 Hope.
  - A dust storm, a solar flare and four dry spells passed without moving any gate.
  - Earth's only demand came at sol 100 and was never answered.
- **The pressure that exists is slow, not risky.**
  - Struct held the program back 77 sols.
  - Power was short about half the time.
  - Late oxygen forced two short launches to Earth.
  - The Big Arrival waited 37 sols for 100 empty beds.
  - These are waits. Nothing was lost.
- **Choice: it exists, but mostly off the main path.**
  - The bot passed every bar (the Big Arrival at sol 81, seven Earth rounds) without trading, signing a pact, answering Earth, fitting a lift, laying a tram, digging below floor -3 or opening an atrium.
  - 82 of the 154 rows belong to those unused systems.
  - Only 8 of the 154 shape a real decision in the run today.
- **Fun, too complicated or too simple?**
  - The numbers cannot tell you whether it is fun.
  - They do say the game is wide and soft: many systems, few with teeth. That points to "too soft" more than "too simple".
  - The cure is not more systems. It is letting the existing ones bite.
- **One line of the Hope sum causes most of the softness.**
  - The Milestones line adds a permanent 30 points by sol 49: the vault 5, the first harvest 4, the first-born 6, and four discoveries 15.
  - With the base 50 and comforts 8, Hope is 88 before housing, jobs, rooms or any penalty counts.
  - So 11 placeholder Hope rows, the crises and the commute are all hidden under Hope's 100 clamp.
  - You left this open at D3a (cap, fade, or accept). It is not one of the 154 rows, so no analyst was asked to tune it.
- **Recommendation: option B** (section 4). Cap the Milestones line at 15, and add option A's one fix. Then judge it by hand. Try Earth's earlier demand (option D) by hand first, with a command-line switch.

---

## 1. What the 110-sol playtest shows about pressure

All figures were re-checked in `idle_r2_full.txt`. Line numbers are in brackets.

### Hope

- **Range.** Smoothed Hope ran 55..100 [1687].
  - It starts at 55 when the crew lands (sol 19.72).
  - It reaches 75 at sol 24 (THRIVING from sol 25) and 90 (FLOURISHING) at sol 38.
  - It sits at 100 from sol 52 until the Big Arrival at sol 81.
  - After the wave its lowest point is 89 (sol 95, during a dry spell). It ends at 96.
  - Instant Hope never fell below 84 after sol 30.
- **Why so high.** Base 50 + milestones 30 + comforts 8 = 88 before anything else. Housing, rooms, jobs and furnishings only decide between 88 and the 100 clamp.
- **Above about 92, Hope no longer speeds up work.** Work tempo tops out at 1.25 at Hope 91.7. Past that, Hope only adds settlers.
- **The gates sit far below.** Settlers, births and the Big Arrival's streak all open at 75, and research at 85. From sol 24 on, Hope was at 75 or more on every sol.

### Food and water

- **Food's low point: 57 sols of stock** [1687], on sols 45-46, after the first birth. The alert line is 10 sols.
- **At the end:** 6,862 kg in store (91 sols). The garden made 114.9 kg a sol against 75.6 eaten [1710].
- **Water:** 76 t stored. 1,410 kg made against 37 drawn a sol.
- **Penalties:** no food, water or shortage penalty fired on any sol.
- **The one food consequence:** the settler ships at sols 32.55 and 44.55 brought nobody. Life support held them: the garden made 0.8 and 3.0 kg a sol against 2.5 and 3.1 needed.

### The commute

- **Hope cost: 0.000**, both at most and on average [1688].
- The longest trip was 120 m against the 300 m "long" line.
- Peak population 122, ramp 0.62, and no long commuter at any time.

### Crises

All were weathered:
- **A dust storm** on sols 12-15 cut solar to 30% [139]. It came before the crew landed.
- **A solar flare** on sols 17-17.5 [195].
- **Four dry spells** began at sols 30.25, 50.25, 70.25 and 90.25 [444, 804, 1169, 1441], five sols each.
  - Each costs 6 Hope and drains 8 kg of water a sol, under 2% of the water made.
  - Two of them hit while Hope sat clamped at 100, so they changed nothing at all.
- **Grow lights went dark** at sols 20.80 and 76.28, when the bank could not feed the garden [250, 1264].
- **Ice ran low twice.** Ice_A at 34.91: the bot found Ice_B 0.1 sol later. Ice_B at 93.85 [500, 1491].

### The dilemma

- **Earth's first demand became pending at sol 100.25** ("A demand is coming") [1558]. Tension climbs 0.6 a sol from sol 0, and the demand triggers at 60.
- **The bot has no program to answer it.** A pending demand costs nothing by itself (the code only waits for an answer).
- **Tension does trim Earth's awards as it rises.** The Q3 award was worth 0.95 of its base at sol 29 and 0.83 at sol 96.
- **If the demand is ignored**, Earth turns a neighbour against you at tension 75. That is about sol 125, after the run ends.

### Where the pressure really is

- **Struct** "held the program back 77 sols" [1686]. City growth was short of Struct on 43 of 99 sols (sols 29-80).
- **Power** was short 52% of the run, and machines were shed 15% [1685]. The bank stood empty 37% of sol-hours, and the grow lights were dark 541 sol-hours [1711].
- **Oxygen, late.** From Earth's round 5 (sol 82), the oxygen bill outran the oxygen made.
  - Two ships waited, then launched short at sols 84.00 and 96.00, leaving 484 and 575 kg owed.
  - Round 7 is unmet at sol 110 (2,622 of 2,913 kg held).
  - Growth was short of oxygen on 34 of 99 sols.
- **Beds.** The Big Arrival's Hope streak was complete by about sol 44. The wave then waited until sol 81 for 100 free beds.

### What the scripted player never does

- Toggle the day shift, demolish, or build a dome, a window or a surface habitat.
- Buy a Colonist Transport, sell to Earth, or spend its saved credit (13,841 cr unspent at the end).
- Trade, sign a pact, or answer Earth.
- Fit a lift, lay a tram, dig a tunnel, sink a second shaft, go below -3, or open an atrium.

---

## 2. The 154 rows by system

- **"Real in the run"** means the row shapes a decision with a cost in the playtest today (the analysts' reading).
- **"For a hand player"** means what it would do for a player who uses that system.

| System | Rows | Count | Real in the run | For a hand player |
|---|---|---|---|---|
| Build mode and world (dome size, depth cap, shaft link, salvage) | #1, #2, #5, #6 | 4 | 0 | salvage (#6), if you re-plan |
| Ice alerts | #3, #4 | 2 | 0 | 0 |
| Hope lines | #7-#11, #21-#26 | 11 | 0 (4 masked: Hope sits at the 100 clamp) | masked until Hope comes down from the clamp |
| Earth: the quota ladder, credit, launches | #12-#17 | 6 | 3 (#12, #13, #17) | plus water sales (#15, #16) |
| Day shift | #18-#20 | 3 | 2 (#19, #20) | the toggle |
| Population: transports, births, settlers, the Big Arrival | #27-#41 | 15 | 1 (#38) | plus the Colonist Transport (#27) |
| Roads | #42, #43 | 2 | 1 (#42, light) | the same |
| City growth and the City/Earth dial | #44-#52 | 9 | 1 (#51) | the same |
| Life-support cover for the Big Arrival | #53 | 1 | 0 | oxygen only |
| Cargo rovers | #54-#59 | 6 | 0 | 0 (it only forces roads to both ends) |
| Commuting | #60-#68 | 9 | 0 | only for a deep or sprawling city |
| Outposts (distant ice) | #69-#72 | 4 | 0 | 0 |
| Shafts and tunnels | #73-#75 | 3 | 0 | 0 (caps) |
| Transit: hoists and lift cars | #76-#91 | 16 | 0 | weak |
| Depth past floor -5 | #92-#99 | 8 | 0 | a price with almost no pull |
| Trams (including riders, #105) | #100-#105 | 6 | 0 | nothing to cure at today's sizes |
| Lift and tram power | #106-#108 | 3 | 0 | weak |
| Martian market and convoys | #109-#122 | 14 | 0 | mild; real for a bulk seller (#116, #121) |
| Martian currency and the desk | #123-#138 | 16 | 0 | real but late (#124, #125) |
| Pacts, tariffs, sabotage | #139-#151 | 13 | 0 | real if you break or sabotage (#144, #145, #147, #148, #151) |
| Atriums | #152-#154 | 3 | 0 | two air costs with no gain yet (#153, #154) |
| **All** | | **154** | **8** | **about 11 more** |

- **The run, counted.**
  - 8 rows are real (5%).
  - About 15 bind without being a choice: rates, brakes and structure.
  - 4 are masked, because Hope sits at the 100 clamp.
  - About 127 are latent or inert.
- **A stricter reading of the 8.** #13 is a unit conversion; the real choice sits in the quota and manifest tables. Roads (#42) cost only 4% of the Struct made. The bot itself never uses the day-shift toggle (#19, #20) or moves the City/Earth dial (#51).
- **82 rows sit in systems the bot never uses:** shafts and tunnels, transit, depth, trams, lift power, the market, the currency, pacts and atriums. The playtest cannot judge any of them.
- **27 rows are already your rulings**, so their "Placeholder, director review" tag is stale:
  - bal1: #3, #9, #10, #12, #14, #17, #19, #20, #30, #36, #37, #38, #39, #51;
  - bal2: #63, #64, #76, #77, #92, #98, #99;
  - bal3: #125, #137, #138, #147, #148, #151.
  - #124 is also ruled, but you asked to revisit it with the tension numbers.
- **The analysts propose changing only one value:** #28, from 50 to 25 kg. They want your taste on 16 rows (section 6). For most of those, keeping today's value is one of the options.

---

## 3. The rows that would add the most pressure per change

Ranked by the new decision or consequence a 110-sol session would meet, per row changed. Three of the top five are not among the 154.

| Rank | Row | Today, then try | Modelled effect | One of the 154? | Who can judge it |
|---|---|---|---|---|---|
| 1 | The Milestones line: HopeVaultMilestone 5, HopeFirstHarvestMilestone 4, HopeFirstBornMilestone 6, and the four discoveries' HopeBonus 2, 2, 3, 8 (`RH_Discoveries.csv`) | 30, uncapped; then a cap of 15 | After the wave, smoothed Hope 74-88 (was 89-98), ending about 81 (was 96). Under 75 on sols 93-95. Settlers 2 a ship instead of 3. Tempo 1.18 instead of 1.25. | No (D3a, still open) | The playtest, then you |
| 2 | EarthTensionDemandThreshold | 60; then 45 | Earth's first demand at about sol 75 instead of 100.25. Nothing else in the run moves. | No | You, by hand |
| 3 | GrowthReserveO2Sols (#45) | 10 sols; then 15 | The short launches leave about 919 and 1,020 kg owed (was 484 and 575). Growth is blocked on oxygen on nearly every late sol. | Yes | The playtest |
| 4 | QuotaRepeatBillMul (#12) | 1.10; then 1.15 | Earth's bills for rounds 0-6 rise 17% (Struct 27,667 against 23,718 kg; oxygen 13,281 against 11,385 kg). About 6 rounds by sol 110, and a later wave (estimate). | Yes, but your ruling (E3 Q-C (a)) | The playtest |
| 5 | CrisisEnvironmentHope | 6; then 10 | Alone: nothing (even at 12, Hope stays above 83 after the wave). With rank 1 at 15: under 75 on 6 sols instead of 3. | No | The playtest |
| 6 | CommuteLongCm (#62) | 30,000 cm; then 10,000 | The 120 m hoist trips become long. Hope 0.5 to 2.0 lower at sol 110; long workers work at 0.907. The lift gains a purpose. How many workers go long is not logged. | Yes | A sweep first |
| 7 | PactTension (#141) | 5; then 10 | A trader who signs at sol 23.5 meets Earth's demand at 83.3 instead of 91.7, and loses 420 cr a Q3 round (210 today). No effect in the run. | Yes | You, by hand |
| 8 | ColonistTransportFoodKgPerColonist (#28) | 50 kg; then 25 | After the wave, 52 sols of food instead of 87; the low point drops from 57 to 52. A colony with no farm no longer passes the wave's food check. | Yes | The playtest |

**Why each is ranked where it is:**
1. **The Milestones line** is the one change that makes a dozen other rows count without retuning them: the 11 Hope rows, the crises' Hope, the commute (#62-#68), the atrium bonus (#152), and the dilemma's ±12 Hope shock.
   - With a cap of 10: after the wave, Hope is 66-83 with crises at 10, under 75 for 21 sols (87-107). Births stop from about sol 87, and settlers fall to 1-2 a ship.
   - With a cap of 20: no gate moves.
   - Even with no milestones at all, the small crew sits at 72-80. The base terms alone (50 + housing 7.5 + rooms 5 + jobs 7.5 + comforts 8) already reach the 75 gates.
   - Before the wave, both caps leave Hope at 79 or more, so the wave still lands at about sol 81.
2. **EarthTensionDemandThreshold** is the cheapest way to put the game's main designed choice (Comply or Defy) inside a 110-sol session.
   - It has one reader, the demand trigger.
   - Earth's awards read tension, not this row, so the run changes only by its alert line.
   - The escalation stays where it is: Earth turns a neighbour at tension 75, about sol 125.
   - Raising the drift instead (0.6 to 0.8) gives the same demand date but shrinks every Q3 award by 2-7%, about 3,600 kg of cargo over the run.
3. **GrowthReserveO2Sols** sets how hard the late oxygen squeeze is: Earth's bill against the colony's breathing reserve.
   - It makes the day-shift toggle on Electrolyzers matter. Each one switched to all hours gives +107 kg of oxygen a sol, and the bot never uses it.
   - The gentle mirror is 5 sols: arrears of 49 and 128 kg, and about two more Homes batches.
4. **QuotaRepeatBillMul** is the strongest economic lever, but you ruled it. I do not propose it for the first pass.
5. **CrisisEnvironmentHope** is cheap. It only matters after rank 1.
6. **CommuteLongCm** needs a measuring sweep first, and it only matters after rank 1.
7. **PactTension** turns the pact from a near-free win into a provocation. Only a trader sees it.
8. **#28** is a fix more than pressure. The garden covers the run, but the number currently disables a gate.

---

## 4. A first balance pass: options

### A. Gentle: fix what is broken, add no pressure

- **Values.**
  - ColonistTransportFoodKgPerColonist (#28): 50 to 25 kg. Also change the Colonist Transport's effect text in `RH_ManifestItems.csv` from 1,000 to 500 kg.
  - Clear the stale tag on the 27 ruled rows. This edits notes only; no value changes.
- **Predicted effect.**
  - Identical to the run up to sol 81.
  - After the wave, food is 3,729 kg (52 sols, was 87). The report's food low point drops from 57 to 52 sols. No penalty fires.
  - Hope 55..100, commute 0, the wave at 81.00, 7 rounds, arrears 484 and 575 kg, population 122: all unchanged.
  - This is bal1's prediction: no change in behaviour.
- **Recommendation.** Do it with whichever option you pick. On its own it is not a balance pass: Hope stays at the clamp and no new consequence appears.

### B. Firm: make Hope a resource after the wave (recommended)

- **Values.**
  - Everything in A.
  - Cap the Milestones line at 15: one new config row (suggested name `HopeMilestonesMax`, 15) and one `min()` in `GetColonyHope`. This is the same pattern as the Furnishings cap (6) and the Views cap (3), and D3a's first option.
  - The seven milestone values stay. Each still lifts Hope when it happens, until the line reaches 15 (about sol 42).
- **Predicted effect.** This is a static re-score of the run's Hope lines, not a re-run, so the real run will differ in detail.
  - Smoothed Hope runs 55..94 (was 55..100).
  - Before the wave, Hope is 79-94. The wave's 20-sol streak holds, and it lands at about sol 81.
  - After the wave, Hope is 74-88 (was 89-98) and ends at about 81 (was 96). The city is THRIVING, not FLOURISHING.
  - The late dry spell (sols 93-95) takes Hope under 75. That pauses births once.
  - Settlers drop to 2 a ship instead of 3 (about 6 before the wave, was 9), because the settler count reads how far Hope is above 70.
  - Births drop to about 8 (was 9). Population at sol 110 is about 118 (was 122).
  - Tempo at the end is 1.18 (was 1.25). The garden makes about 109 kg a sol (was 115) against about 74 eaten.
  - Food's low point is about 54 sols.
  - Unchanged: commute 0, 7 Earth rounds, arrears 484 and 575 kg (oxygen does not read Hope), and the playtest bar (the wave by sol 110, at least 3 rounds, nobody recalled).
- **What the player gets.** After the wave, the Hope gaps become the road from about 80 back to FLOURISHING:
  - jobs 3.2 of 7.5;
  - housing 4 of 7.5;
  - rooms 2-4 of 5;
  - furnishings 0-1 of 6.

  They cost the same Struct Earth wants, so they become real choices:
  - 33 Workbenches (2,640 kg) restore the jobs line (+4.3);
  - about three Homes batches (about 1,900 kg of Struct and 1,200 kg of oxygen) restore housing (+3.5);
  - about 1,840 kg of plants fills furnishings at 122 people (+6).

  Each Q3 round asks 4,429-4,872 kg of Struct. That is the crew-versus-quota trade the run lacks today. Before the wave, furnishing the rooms (about 60-120 kg of plants) can win back the third settler on some ships. The dry spells and the commute line start to show.
- **Recommendation: yes.**
  - It is D3a's first option and adds one row.
  - The playtest can measure it.
  - It needs no retune of the 11 Hope rows first. Tune those against B's new baseline.
  - A fade (milestone Hope that decays over some sols) is the D3a alternative. It needs more code and was not modelled.

### C. Harsh: the colony fights for Hope and air after the wave

- **Values.**
  - Cap the Milestones line at 10.
  - CrisisEnvironmentHope: 6 to 10.
  - GrowthReserveO2Sols (#45): 10 to 15.
- **Predicted effect.**
  - Smoothed Hope runs 55..90.
  - Before the wave, Hope stays at 79 or more, and the wave still lands at about sol 81. Settlers drop to 1-2 a ship (about 4).
  - After the wave, Hope is 66-83. It is under 75 for 21 sols (87-107) and under 70 in the late dry spell, so it drops to STEADY for about 3 sols.
  - Births stop from about sol 87: about 4 in all. Population at sol 110 is about 112.
  - Tempo is about 1.15, and the garden makes about 106 kg a sol.
  - Oxygen: the short launches leave about 919 and 1,020 kg owed, and growth is blocked on oxygen on nearly every late sol (bal1's static estimate).
  - That means fewer homes and rooms after the wave, so housing and rooms Hope stay low. This is a loop: low Hope, slower work, fewer homes.
  - The first dry spell (sols 32-36) takes Hope under research's 85 gate, so the discoveries due from sol 32 on come about 5 sols later. Their Hope is capped anyway, but their rewards and unlocks (the Workshop, the Full Laboratory) slip.
- **Recommendation: not as a first pass.**
  - Three changes at once cannot be told apart.
  - The post-wave loop may not recover.
  - Run it as a stress sweep to find the floor.

### D. Add-on, try by hand first: Earth's dilemma inside the session

- **Values.**
  - EarthTensionDemandThreshold: 60 to 45 (not one of the 154).
  - Optionally, PactTension (#141): 5 to 10.
- **Predicted effect in the run.** The Earth transmission moves from sol 100.25 to about 75.25, and nothing else moves (the bot never answers).
- **What a hand player gets.**
  - Comply or Defy, with 35 sols left in the session, about 6 sols before the Big Arrival.
  - Its ±12 Hope shock only registers if B is in. Today the clamp would absorb it.
  - With a pact signed at sol 23.5, the demand comes at about sol 67, or about 59 with PactTension 10.
  - Ignoring the demand still brings Earth's embargo at tension 75 (about sol 125).
- **Recommendation.**
  - Launch with `-RHConfig=EarthTensionDemandThreshold=45` and play a session. This needs no rebuild and no gate.
  - If it plays well, fold it into B's gate.

### The options side by side (the 110-sol run's figures)

| Figure | Today | A gentle | B firm | C harsh |
|---|---|---|---|---|
| Hope, smoothed, over the run | 55..100 | 55..100 | 55..94 | 55..90 |
| Hope after the wave | 89-98, ends 96 | the same | 74-88, ends about 81 | 66-83, ends about 76 |
| Sols under 75 once settled (sol 25 on) | 0 | 0 | 3 (93-95) | 21 (87-107) |
| Band after the wave | FLOURISHING | the same | THRIVING | THRIVING; STEADY for about 3 sols |
| Work tempo at the end | 1.25 | 1.25 | 1.18 | 1.15 |
| Garden at the end (kg a sol; about 75 eaten) | 114.9 | 114.9 | about 109 | about 106 |
| Settlers before the wave | 9 | 9 | about 6 | about 4 |
| Births | 9 | 9 | about 8 | about 4 |
| Population at sol 110 | 122 | 122 | about 118 | about 112 |
| Food's low point (sols of stock) | 57 | 52 | about 54 | about 54 |
| The commute's Hope cost | 0 | 0 | 0 | 0 |
| The Big Arrival | sol 81.0 | 81.0 | about 81 | about 81 |
| Earth rounds met | 7 | 7 | 7 | 7, with larger arrears |
| Oxygen owed after the two short launches | 484, 575 kg | the same | the same | about 919, 1,020 kg |
| Crises | no gate moved | the same | the late dry spell pauses births | the late dry spell takes Hope under 70 |
| Earth's demand | sol 100.25, unanswered | the same | the same | the same (D moves it to about 75) |
| The playtest bar | passes | passes | passes (predicted) | likely passes |

---

## 5. What a balance gate costs

### The fixed work for any value change

All three analysts agree on these steps:
1. Edit the CSV row.
2. Edit the matching C++ default in `Source/RedHopeSim/Public/RHSimWorldSubsystem.h`. It must equal the CSV, so even a "data" change is a build.
3. Re-sync DT_Config headlessly.
4. Update the suite's pin line and any hard-coded expected values. The pins fail on drift on purpose.
5. Re-run the 64-log battery and diff it line by line.
6. Re-run the 110-sol playtest and make it the new baseline.

One gate for A + B together is cheaper than two: one battery re-baseline and one playtest re-baseline.

### Battery lines per changed row (the analysts' estimates)

| Rows | Lines that move |
|---|---|
| Pin-only rows: bounds, caps and guards (most of #53-#154) | 1-7, in one suite |
| Hope weights (#7, #8, #21-#26) | 10-15 logs each (C2a's per-head change moved 14) |
| Earth (#12-#17), day shift (#18-#20), population (#27-#41), growth (#44-#52) | Their pin, plus the resupply, credit, export, dayshift, prefab, arrivals, districts or citygrowth lines that print them |
| Rovers (#54-#59) | 1-8 each (traffic, farice) |
| Commuting (#60-#68) | 10-20 each. #62 under 24,000 also moves the -percapita and -arrivals Hope pins. |
| Outposts (#69-#72) | 2-6 each |
| Transit (#76-#91) | 3-10 each |
| Depth (#92-#99) | #92 moves every depth line and every baseline that prints a floor range. The rest move the depth suite's hand numbers. |
| Trams (#100-#105) | 3-20 each |
| Lift power (#106) | About 15 of transitpower's 39 lines |
| Market repricing (#109-#115, #118, #119) | 40-70 each, because market, currency, levers and regionready all print prices |
| Currency (#123-#138) | 2-20 each (about 10 for a rate row) |
| Pacts, tariffs, sabotage (#139-#151) | 2-7 each |
| Atriums (#152-#154) | 8-10 each |

### The playtest baseline

- **35 rows move it when changed:**
  - #3, #7, #12, #13, #17-#20, #23-#26, #28, #29, #31-#33, #38, #39, #42-#45 and #47-#52;
  - #54, #55, #61, #62 (at 11,999 or less), #63 and #64.
- **9 more move it only if pushed far past the run's margins:** #30, #35, #36, #37, #40 and #69-#72.
- **110 cannot move it.** The bot never reaches their code, or never crosses them. This covers every row from #73 to #154 (the `lifts=1` variant does move for #76-#91), plus 28 lower rows that bal1 and bal2 list.
- **Any Hope weight also shifts the sols where Hope is under 100** (sols 20-51 and 82-110), through tempo.

### Per option

| Option | Work | Battery (estimate) | Playtest |
|---|---|---|---|
| A | 1 value row, the manifest text, and 27 note edits | About 5-15 lines: arrivals' pin and the wave and transport food lines, districts, and -scale's transports | Food figures from sol 81 (about 60 lines) and the report's low point |
| B | A, plus 1 new row and 1 code line. No save change is expected: it is a derived read, like D3a's weights. | A's lines, plus the suites whose milestones pass 15: -discovery (it reaches 20) and probably -citygrowth. The others stay at 5-11 and do not move. A few to a few dozen lines; the exact count needs the run. | A new baseline. Every Hope figure moves from sol 42; settlers move from sol 55, and everything downstream follows. |
| C | B, plus two value rows | B's lines, plus -growth (its milestones reach 11), -crisis and -ending prints, and citygrowth, atriums and export for #45. Perhaps 30-80 lines over about 8 logs. | Moves wholesale |
| D | 1 row; try it with `-RHConfig` first | 8 logs trigger the demand on their own: arrivals, currency (34 times), earth, icelow, market, levers (16), preemptive and solidarity. Roughly 20-60 lines, most in -currency and -levers. | One alert line |

---

## 6. What must wait for your own hand-play

- **Whether it is fun, too complicated or too simple.** Only play can say. The run can only show where the game is soft.
- **How option B feels.**
  - Does a city at about 80 after the wave read as a fair challenge or as nagging?
  - Is the crew-versus-quota choice legible on the Crew panel?
  - Is the dry spell's dip a good beat?
  - Then retune the Hope weights (#7, #8, #21, #23, #25) against that feel.
- **Every system the bot never touches**, because the playtest cannot judge it:
  - the market (14 rows), the mark (16), and pacts, betrayal and sabotage (13);
  - transit, trams and their power (25), depth (8), and shafts and tunnels (3);
  - atriums (3);
  - salvage (#6), water sales (#15, #16), the day-shift toggle (#19, #20) and the Colonist Transport (#27).
- **The 16 taste calls the analysts flagged:**
  - SalvageFraction (#6): how forgiving is re-planning?
  - HopeFurnishingsMax, HopeViewsMax, HopeUnsupportedMax, HopeJobsMax, HopeAdjacencyMax (#7, #8, #21, #23, #25): decide after the milestones.
  - LaunchH2GPerKg and LaunchO2GPerKg (#15, #16): should water be a cash crop? Today it sells for a net 1 cr/kg, about 11,500 cr a round from the run's surplus. 67 and 400 g/kg would refuse water sales.
  - ColonistTransportFoodKgPerColonist (#28): option A.
  - LifeSupportDeficitCoverSols (#53): should the Big Arrival need farms? That needs a rule, not a number.
  - CommuteLongCm (#62): should commuting bite at 120 people?
  - HopeShelterMax (#99): should depth pull? Today it has a price and only +3 Hope of pull.
  - CurrencyIssueTension (#124): keep 10 and fix the banner re-raise in code, or 6.
  - PactTension (#141): should a pact be a decision?
  - HopeAtriumMax (#152): 5 if Hope ends up in 70-85.
  - AtriumLeakPermille (#154): 1,500 to make a big atrium affordable.
- **One more rule question** from bal1: should a carved garden cell count as a certified bed?
  - The bot's 36 garden cells closed a third of the Big Arrival's bed gap.
  - The city ended with 145 beds but comfortable room for only about 80.
- **What a script could do first,** cheaper than hand-play for numbers but not for feel:
  - **B, without a rebuild.** `RH_PLAYTEST_DISTRICTS=1 RH_PLAYTEST_EXTRA="-RHConfig=HopeVaultMilestone=0,HopeFirstHarvestMilestone=0,HopeFirstBornMilestone=0" bash scripts/sim/rh_playtest.sh 110 bal_ms15 guide full`.
    - The line then ends at 15 (the discoveries alone).
    - It matches B after sol 49. Before that it is lower: THRIVING comes at sol 28 instead of 24.
    - My model gives the same post-wave range as B (74-88).
  - **bal2's sweeps.** `-RHConfig=CommuteLongCm=10000 -RHCommuteAudit` (and again with `RH_PLAYTEST_LIFTS=1`) measures how many workers go long. `-RHConfig=LifeSupportDeficitCoverSols=0` should come out byte-identical.
  - **bal3's "trader" variant.** It trades every round trip, signs a pact at the first chance and answers Earth, and runs to about 200 sols. It needs new bot code.

---

## Rows an analyst missed

- **None of the 154.**
  - A script matched the analysts' 154 names against every row tagged "Placeholder, director review".
  - Every one is covered, once, in CSV order.
  - The values match the CSV.
- **EpilogueSustainSols** (CSV line 153, 15 sols). Its note says "Balance placeholder", but it lacks the "director review" tag, so it fell outside the 154.
  - It sets how long a declared path must hold before the one-time epilogue card.
  - It is not reached in the playtest, which declares no ending. The suites set it by hand (2.0 and 30.0), so a change probably moves no battery line (not checked).
  - It is a taste call. Nothing presses on it.
- **DisputeHopeBelow** (line 150, 45). Its note calls it a wording placeholder, not a balance one.
  - Disputes need smoothed Hope under 45.
  - The run never went below 55, and its log has no dispute line. Even option C stays above 65 from sol 25 on.
- **The rows that actually set the pressure are not placeholders, so nobody was asked to cover them:**
  - The milestone rows: HopeVaultMilestone (line 57), HopeFirstBornMilestone (line 90), HopeFirstHarvestMilestone (line 152), and the HopeBonus column of `RH_Discoveries.csv`. These are the 30 points.
  - HopeBase 50 and HopeLuxuryBonus 8. Comforts read 8 on every sol from the crew's landing.
  - The crisis rows: CrisisEnvironmentHope 6; CrisisWaterDrainPerSol 8 kg, which is a non-event for a colony storing 76 t of water; CrisisIntervalSols 15; CrisisChance 0.6; CrisisDurationSols 5.
  - Earth's clock: EarthTensionDriftPerSol 0.6, EarthTensionDemandThreshold 60, EarthPreemptiveThreshold 75.
  - bal1 and bal3 named the Milestones line as the cause. No analyst modelled a cap; section 3 does.
- **A tag slip, nothing lost.** DayShiftEndFrac (line 181) is untagged, while its twin DayShiftStartFrac (#18) is tagged. bal1's #18 covers the window.

---

## How the numbers were made

- **Playtest figures.** Every playtest figure above was re-read from `idle_r2_full.txt`.
  - Most agree with the analysts.
  - One differs: growth was short of oxygen on 34 of 99 GROWTH sols by my parse (sols 21-35 and 85-108). bal1 counted 27.
- **The Hope model.** It is a short Python script, not in the repo.
  - It rebuilds instant Hope for each of the run's 91 CREW lines (sols 20-110). It uses the printed parts (base 50, housing, rooms, jobs, comforts, furnishings, views, milestones, adjacency) plus the dry spells' -6.
  - On the 54 sols where Hope was under the 100 clamp, it matches the printed instant within 1.2 points.
  - It then applies a cap and re-runs the 3-sol smoother at sol steps. That smoother tracks the printed smoothed Hope to 0.5 points on average.
  - Settlers use floor((Hope - 70) / 30 x 4) at each ship's sol. Births need smoothed Hope of 75 or more. Tempo is 1 + 0.006 x (Hope - 50), capped at 1.25.
  - **Limits.** It holds the build order, the people and the discovery dates fixed. Lower Hope also slows the garden and changes settlers and births, and those feed back. Treat its settler, birth and population counts as first-order estimates. Only a re-run can confirm them.
- **Other options' figures.** The economic figures for options C and #12 are bal1's static estimates from the playtest lines, not re-runs. The trade, currency and pact figures are bal3's integer models, which reproduce the suites' own numbers.
- **Code reads.** The claim about the demand threshold comes from `git grep`: the only reader is `RHSimWorldSubsystem.cpp:10243`. A pending demand is cleared only when it is answered (`ResolveDilemma`, `:10640`). The awards read tension in `GetRequisitionMultiplier` (`:10207`).
- **Coverage check.** A script matched all 154 tagged rows against the three analyst files: every name, in CSV order, with the CSV's value.
