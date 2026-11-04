# Balance preview (2026-11-04)

Two 110-sol scripted playtests at d1ae860, with config overrides only. Nothing was rebuilt and no code changed.

## Commands

All runs used `RH_PLAYTEST_DISTRICTS=1 ... bash scripts/sim/rh_playtest.sh 110 <label> guide full` in `/Volumes/Unreal/red_hope/red_hope`.

| Run | -RHConfig overrides | Timeline |
|---|---|---|
| bal_preview | HopeVaultMilestone=0, HopeFirstHarvestMilestone=0, HopeFirstBornMilestone=0, EarthTensionDemandThreshold=45 (the scope's section 6 command plus D) | `/Volumes/Unreal/red_hope/red_hope/Saved/RHPlaytest/bal_preview.txt` |
| bal_preview2 | HopeVaultMilestone=0, HopeFirstBornMilestone=0, EarthTensionDemandThreshold=45 (keeps the first harvest's 4) | `/Volumes/Unreal/red_hope/red_hope/Saved/RHPlaytest/bal_preview2.txt` |

- Both runs: rc 0, 0 sim errors, and every override logged by `[RHConfig]`.
- Comparison script: `/Volumes/Unreal/red_hope/wip-backups/balance-gate/compare.py`.

## Results against today's baseline

| Figure | Today (ind_full_playtest) | bal_preview | bal_preview2 | Scope's B prediction |
|---|---|---|---|---|
| Milestones line, max | 30 | 0 | 4 | 15 |
| Discoveries | 4 (sols 32, 36, 41, ...) | none | none | as today |
| Hope smoothed over the run | 56..100 | 51..81 | 51..85 | 55..94 |
| Hope from sol 25 to the Big Arrival | 78..100 | 70..81 | 73..85 | 79..94 |
| Hope after the Big Arrival | 89..98, ends 96 FLOURISHING | (no arrival) | 68..77, ends 73 STEADY | 74..88, ends about 81 |
| Sols under 75 from sol 25 | 0 | 9 | 33 | 3 |
| The Big Arrival | sol 81.01 | never | sol 76.01 | about 81 |
| Earth's demand | sol 100.25 | sol 75.25 | sol 75.25 | sol 75.25 (D) |
| Population at sol 110 | 122 | 7 | 107 | about 118 |
| Immigrated / born | 9 / 9 | 2 / 1 | 1 / 2 | about 6 / 8 |
| ROUND lines (Q1, Q2, then Q3 rounds) | 9 | 10 | 8 | 9 |
| Outpost bar | PASS | PASS | PASS | PASS |

## What it shows

1. **D holds.** In both runs Earth's transmission moved from sol 100.25 to sol 75.25, as the scope predicted.
2. **The scope's command for previewing B was wrong.**
   - Zeroing the three milestones does not leave "15, the discoveries alone".
   - A discovery fires only in the FLOURISHING band. HopeFlourishingEnter is 90; its Notes call it "the discovery/growth band".
   - Without the early milestones, Hope never reaches 90, so no discovery fires and the line stays at 0.
   - The scope's static re-score missed this feedback.
3. **There is a cliff below B.**
   - **Milestones 0 (bal_preview):**
     - smoothed Hope never stays at 75 or more long enough while 100 beds are free;
     - settlers stop: 0 or 1 a ship, against 3 today;
     - the city never grows;
     - the Big Arrival never lands, and the colony ends at 7 people.
   - **Milestones 4 (bal_preview2):** the Big Arrival lands at sol 76.01 and the colony reaches 107. Afterwards Hope sits at 68-77 (STEADY and THRIVING) and is under 75 for 33 sols.
4. **The real B should land near the scope's prediction.**
   - The cap keeps the early milestones: the vault 5 and the first harvest 4, then the first discoveries up to 15.
   - So until the line reaches 15 (about sol 41: the vault, the harvest and three discoveries), B's Hope is the same as today's.
   - From then on it is today's less the 15 B removes, so about 85-94 before the Big Arrival.
   - After the Big Arrival, bal_preview2's 68-77 plus the 11 points B keeps over it gives about 79-88. The scope predicted 74-88.
   - Hope is far above the cliff's 75 streak bar the whole way.
   - A later discovery may slip if Hope falls under 85 (FLOURISHING exit). Its Hope is capped anyway, but its reward and unlock come later.
5. **The settler count is the steep lever.** Settlers a ship read how far Hope is above 70. At Hope 78-79, ships brought 0 or 1 settler where today's run brings 3. That rule is what turns a few Hope points into a city-sized difference.

## Decision for the gate

- **Build A, B (HopeMilestonesMax 15) and D (EarthTensionDemandThreshold 45).** D held. B's real cap was not reproducible with config overrides, but the cliff sits well below it.
- **The gate's own 110-sol playtest is the real test of B.** Its bar:
  - the Big Arrival by sol 110;
  - at least 3 Q3 rounds;
  - nobody recalled;
  - no shortage penalty that stops the colony.
- **Stop rule:** if the bar fails, the builder stops and returns a director question with the numbers. It does not tune any other row.
- **Report predicted against actual:**
  - Hope before the Big Arrival 85-94;
  - Hope after it 79-88;
  - the Big Arrival about sol 76-81;
  - the demand at sol 75.25.
