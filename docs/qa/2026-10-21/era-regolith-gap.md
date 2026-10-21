# The era/agent regolith gap (read-only diagnosis, 2026-10-21)

Line numbers are at HEAD `41133b2`. The working tree has uncommitted Source edits from another session, so these may drift.
Nothing was built, run or edited for this note.

## 1. Direction: the era band is LONG, the agent band is short

The brief's framing is reversed. Era does not make 79% of the agent's regolith. The agent holds 79% LESS than era.

Where it was measured: the C4 roads gate, live run, 2026-10-19. It was never written into `docs/`; it is only in the C4 implementer's report and the memory note.
- Setup: the `Midgame` checkpoint at sol 29 (roads finished = `RoadsDay`, control = `NoRoadsDay`). Then 10 sols in `-game -nullrhi`: agent at 16x for 750 s, era at 60x for 200 s. `RH.Ledger` at sol 39, then diffed.
- Results (agent / era):
  - solid Regolith 4,331 / 20,689 kg (-79.07%).
  - deposit Regolith_A remaining 47,369 / 31,105 kg (+34%).
  - Struct 3,948 / 3,973 (-0.63%). Water, Oxygen and Hydrogen are all within 0.5%. SpareParts 10 / 13 is the known no-repairs-in-era deviation.
  - The control run without roads gives the same regolith numbers, so C4 did not cause it.
- Read together: era dug 16.26 t more from Regolith_A. Era also holds 16.36 t more solid regolith. The two figures match within 100 kg, so ALL the extra era dig is sitting in a store (the Lander).
- Struct agrees to 0.6%. So the Forge got the same regolith in both bands. The gap is only surplus dig. Production is not affected.

When it shows up:
- It shows up when a Forge (any consumer of the deposit's resource) is on the deposit's level and its hopper already holds two batches.
- That is any colony whose fleet digs faster than its Forges smelt. 2 RC-E dig about 1,935 kg a sol in era. One day-shift Forge eats at most 600 kg a sol. So from the first Forge onward, almost always.
- It does not show up with no Forge. Then both bands store everything (the baseline: 1 building, pin `deposit Regolith_A: 40645 kg`).

## 2. Mechanism

**Agent band: the excavators are throttled by the pile, by design.**
- Claim: `TryClaimDig` takes the nearest designated deposit with mass left and fewer than 2 claims. It does not look at the pile (`RHSimWorldSubsystem.cpp:10546-10571`). Both brains call it on an empty task: `RHRobotStateTree.cpp:65-75`, `RHRobotTaskProcessor.cpp:151`.
- Dig: once at the site, a robot digs only while `IsDepositWorkable` holds, which includes `PileKg < PileCapKg` (`:10526-10536`, cap 500 kg, `PilesCapacity_kg`). `DigDeposit` also clamps to the room left in the pile (`:10513-10523`).
- When the pile is full, the robot stands by at idle draw, still holding its claim (`RHRobotStateTree.cpp:194-200`, `RHRobotTaskProcessor.cpp:222-228`).
- The pile drains only through the pile haul leg (`StepTaskBoard`, `:933-1015`). Since commit `8fa8299` (2026-10-17, "the structural stall was a hauling bug"), a store takes the pile only if nothing on the level consumes the resource, or it is a construction material (`:990`). Regolith is neither, so it waits for the Forge.
- Result: the agents dig exactly what the Forge eats, about 0.3 t a sol in this save. The two RC-E idle most of the day. That is "excavators barely dig".

**Era band: the fleet digs at full rate and dumps the surplus into the Lander.**
- `EraLogistics` (`:731`) sums every Excavator in `FleetCounts` x `WorkRate` x a charge duty factor (`:741-753`; RC-E duty 0.806). It turns that into a kg budget per step (`:755`). A night gate stops it only when it is dark AND the bank is empty (`:757-764`).
- It feeds a hopper that wants the resource first, up to 200 kg a step (`:775-803`). Then the "leftover budget digs to a store" branch puts the rest into the first Stockpile or Lander on the level, with no consumer check (`:804-822`).
- That branch dates from M1-c (`a11648a`). It was written to match the agent band of that time, which hauled surplus pile to the Lander.
- `8fa8299` changed the agent's pile leg and left this era branch alone. Its battery showed 0 differing lines because no suite ticks robots (section 3). That commit is where the bands split.

**Ruled out as the cause:**
- Claim starvation and priorities: excavators only ever claim Dig, and 2 RC-E fit under the 2-claim limit at Regolith_A.
- Travel: Regolith_A is 36 m from the landing flat.
- Charge: the era duty factor already models it.
- Wear and the D2a fleet-wear stall: if the excavators had halted, the Forge would starve and Struct would split; it agrees to 0.6%. Also, idle excavators draw idle power, so they barely wear (`RHRobotStateTree.cpp:271-275`). The D2a stall was measured before `8fa8299`, when the excavators dug all day.

**Gameplay consequence (the part that matters beyond the ledger):** era play brings back the pre-`8fa8299` structural stall.
- At the measured rate, era empties Regolith_A around sol 55 (31.1 t left at sol 39, about 1.9 t a sol). The agent band runs past sol 110.
- Era never moves store regolith back into a hopper. Its output leg (`:825-880`) only pushes outputs out. So the era Forge starves with 35 t or more sitting in the Lander.

**Smaller era/agent dig differences (real, but not the 79%):**
- Era ignores the pile cap and the 2-claim limit.
- Era works the first designated deposit in list order. The agent works the nearest.
- `FleetCounts` only grows (`:467`), and era applies no wear derate. A worn-out excavator still digs in era; wear is static there, as M1-c logged.
- The same "store anything" rule sits in the era output leg (`:862-872`). The agent's output leg holds non-construction outputs at the producer (`:1072-1080`, D2a). Mass is the same, but the location differs: era drill ice goes to the Lander and is never melted. See section 4 for W1.

## 3. Do the parity suites hide it? Yes, three ways

1. **No suite has robots.** The headless commandlet does not tick Mass. Every suite's "agent leg" runs `StepSim` with no excavators, so it digs nothing. X16 says so in its own message (`RHSimCommandlet.cpp:10889`).
2. **The paired checks leave regolith out.**
   - citygrowth i2 compares Struct, Shielding, Oxygen and Food (`:9889`).
   - traffic X16 compares Hope, Food, Water and Oxygen (`:10889`).
   - roads R2 compares era against era, with and without roads (`:7715-7744`).
   - None has a Regolith or a deposit line.
3. **The only robot paired run is manual and live** (`RH.Ledger` in `-game`). It is not in `rh_battery.sh`. The last full pass before C4 was M1-c in July, before the rules split.
   - The baseline pin is era-only with no Forge, so it sits on the rule both bands still share.
   - Also, the C4 diff script read only the first number of each deposit line. It never saw `PileKg`.

## 4. Fix options

**A. Era follows the agent's rule (recommended).**
- What changes:
  - In `EraLogistics`, the leftover branch (`:804-822`) sends budget to a store only when nothing on the level consumes `D.Type`, or `D.Type` is a construction material.
  - Otherwise the leftover tops the pile up to `PileCapKg`, then is dropped. The fleet idles, as the agents do. It must NOT spill to the next designated deposit: the agents stay claimed at their pile.
  - Factor the condition into one helper that both the pile leg (`:990`) and era call, so the two rules cannot drift apart again.
- Which band moves: era moves to the agent. The agent band and the playtest bot (which runs at the fastest agent speed, `RHPlaytest.cpp:915-918`) do not change.
- Battery impact:
  - The baseline and its pins do not move (no Forge).
  - Lines may move in suites that put a Forge on level 0 and step era (they all boot with Dig Regolith_A, `RHSimCommandlet.cpp:44-48`): `-demolish`, `-prefab`, `-icelow`, `-dayshift`, `-districts`, `-traffic`. The moving lines would be Lander or total regolith and deposit-remaining prints.
  - The commit gate is that no Struct, Shielding or quota line moves. If one does, the fix is wrong.
  - Add one fails-loudly check (for example `-icelow` I14): an era step with the Forge hopper full leaves the Lander's regolith unchanged, the pile at its cap, and the deposit down by only the pile top-up.
  - Then re-cut `Midgame` (`scripts/unreal/rh_checkpoints.sh midgame`; the old `RoadsDay` and `NoRoadsDay` saves are v36 and will be refused) and repeat the live paired run. Bar: solid Regolith and deposit Regolith_A within 5%.
- Size: about 20 lines of sim code and one suite check. No save bump: the pile is already saved state.
- Risk: low. It is the same rule `8fa8299` proved in the agent band. Residual: in era, with a Forge on another level, regolith now waits where before it was stored. That is the agent's behaviour too.
- Optional A2: apply the same rule to the era output leg (`:862-872`), so era holds non-construction outputs at the producer. This reaches more suites (Borer spoil in `-borer` and `-deep` once a Forge exists, and drill ice). Best done in W1, which edits that leg anyway.

**B. The agent follows era.**
- What changes: bring back surplus-to-store in the agent pile leg, plus a new store-to-Forge feed so the stored regolith is not dead.
- Which band moves: agent moves to era.
- Battery impact: the suites barely move (they have no robots). The playtest moves a lot.
- Size: medium.
- Risk: high. It is a balance change.
  - Excavators dig all day, so Regolith_A runs out around sol 44 in both bands.
  - Haulers get busier.
  - RC-E reach wear 100 in about 12.5 active sols (8 a sol), so the D2a fleet-wear stall comes back.
- Not recommended.

**C. Accept it and exclude the lines.**
- What changes: log solid Regolith and the deposit lines as a known deviation, like SpareParts. No code.
- Battery impact: none.
- Risk: the era stall stays (the era Forge starves around sol 55 with 35 t in the Lander). Step 4 plans to run off-screen regions on the era integrator under this same 5% bar (design-decisions, "Q1a"), so the gap would land in every region.
- Not recommended.

**Does it matter for W1?** Yes, moderately. Land A before W1.
- W1's era parity (F15 in the merged revision) assumes era holds material at its source exactly when the agent does. It writes that rule only for Far ice (hold at the drill without a rover, cap by `FarHopperKg`).
- A writes the same rule for deposits, and its helper is what W1's output-leg change should reuse.
- Without A2, a home Ice Drill whose Water Plant is full sends ice to the Lander in era. In the agent band it waits at the drill and is melted later. That lowers era's "water MELTED", which F15 checks.
- F15's H2O ledger should therefore count ice in stores, not only "at drills, in hoppers and in flight", or use `GetTotalSolid(Ice)`.
- F15 compares H2O only, so the regolith line itself will not fail it. But any W1 paired run that diffs the full `RH.Ledger` will show the 79% until A lands.
- W1 already notes that excavators can be pointed at ice with Dig. That is the same pile path, so A covers it.

## 5. Director question

None is needed for A. It changes no balance in the agent band, which is the game as played. It removes an era-only stall and restores the `8fa8299` ruling in era.

Only B is a balance call: "should excavators dig all day and stockpile?" If the director wants that, it needs its own brief.

Aside, not traced: at sol 39 the era bank read 11,228 Wh against 3,859 Wh for the agent. Part of that is a different time of day (27% against 32% of the sol). It is not one of the bar's ledger lines, but worth a look in the re-run.
