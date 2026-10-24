# Economy gates E1, E2a, E2b, E3, E3b: adversarial code review (2026-10-24)

A read-only review of the economy as committed: E1 `a7d3036`, E2a `d342ca5`, E2b `c5d0fc9`, E3 `ea9a5cd` and E3b `b5f5863`. The code was read at HEAD `7d20e09` with `git show`. Since `b5f5863` only `RHDefinitionsSubsystem.cpp`, `RHPlaytest.cpp` and `rh_playtest.sh` changed (`93646b0`, the Struct study), so the sim lines below are E3b's.

The specs are `e1-ledger-design.md`, `e2-earth-credit-design.md` and `e3-selling-to-earth-design.md`, read in this order of authority: DIRECTOR ANSWER, then REVISION, then review, then draft. The review also used their `design-decisions.md` entries, the commit messages, and the suites `-economy`, `-credit` and `-export`. Nothing was built, run or edited.

**Method.** Three independent passes:
- the E1 ledger and its ~45 `NoteFlow` sites;
- E2a and E2b (verbs, caps, credit, deck, bot);
- E3 and E3b, plus every save block and money path.

Each finding below was then re-checked against the code by the main reviewer, and anything that could not be verified was dropped (see the end). All three passes found finding 2 independently.

Short names: "W" is `Source/RedHopeSim/Private/RHSimWorldSubsystem.cpp`, "Econ" is `RHSimEconomy.cpp`, "Export" is `RHSimExport.cpp` (both in the same folder).

## Verdict: PASS WITH FIXES

There are no blockers. The money core holds:
- the award is floored once and paid once;
- the launch debits once, after a check, so the balance never goes negative;
- sales are paid after the debit;
- `Balance == Awarded - Spent` holds on every write path;
- no overflow is reachable from real play;
- the draw is deterministic;
- the buy-from-Earth-and-sell-back loop is closed.

There are four should-fix defects:
1. The colony can be stuck at launch for good, because a manifest item can never be removed.
2. The E2b fix for the misaligned save block is incomplete.
3. The ledger reader accepts values that break later.
4. The playtest bot reports a refused Forge as ordered.

There are also ten nits.

---

## Findings, most severe first

### 1. should-fix: a manifest can never be emptied, so "Over budget at launch" can stop the colony for good (E2b; E3 keeps the same refusal)

- **Where.** `Export:276-281` (`CanLaunchShip`) and `Export:383-387` (`LaunchShipShort`) refuse when the manifest costs more than the balance. `GetManifestCostMcr` (W:13341) prices the items aboard from the live `Price_mcr` at launch. Nothing in `Source/` removes a manifest item: the only writers are `ManifestItems.Reset()` at W:2301 (the next round), W:13387 (a debug hook) and W:14102 (load).
- **Scenario A (a data change).**
  1. A player saves in `AwaitingManifest` with 10,000 cr of items aboard and a balance of 10,000 cr.
  2. A data update raises any `Price_mcr`. Every price is marked "placeholder, director review", and a data-only change does not bump the save version.
  3. On load, the full launch and the short launch are both refused, with "remove or re-price items". The player can do neither.
  4. The phase stays `AwaitingManifest`. `StepQuota` pays nothing in that phase. Credit enters only at a met step or a launch. So the balance can never catch up, and the quota ladder stops for good.
- **Scenario B (a save repair).** A load that sets the credit to 0 while items are aboard locks the colony the same way. That happens when the ledger block before the credit block is lost (`Econ:322`), when the credit block is cut short (`Econ:331`), or when a negative balance is repaired to 0.
- **Caught by:** nothing. CR7 (`RHSimCreditSuite.cpp:512-532`) re-prices a row, asserts that the phase stays `AwaitingManifest`, then restores the price by hand. That is the one step the player cannot take.
- **Fix.** Add `RemoveManifestItem` to the sim, with a deck control. E2 risk 6 already planned it, but its claim that "credits taken at launch keep it harmless" stopped being true once the REVISION added the at-launch refusal. Also lock each item's price when it is added (store it beside the item, as E3 locks export prices), so a data change cannot re-price a manifest already composed. Add a CR7 case where the colony leaves the over-budget state by removing an item.

### 2. should-fix: a ledger block that is misaligned but has a valid count still shifts the credit block, so the ~6e18 bug can return (E1, E2b; the E3 credit rows have the same gap)

- **Where.**
  - `Econ:173-175`: `bLedgerBlockLost` is set only for a count outside [0, 4096] or an archive error.
  - `Econ:226-240`: a trailer that fails its range check sets `bBad` but not `bLedgerBlockLost`.
  - `Econ:381-400`: the credit block's rows rely on their count the same way.
- **False premise.** `design-decisions.md:1342` and the comment at `Econ:168-169` say "each slot is a fixed size". It is not: every slot starts with a variable-length `FString` name.
- **Scenario.** One corrupted byte sets the ledger's slot count to 17 instead of the 18 written. This is the field EC6's `econr6` sets to -1.
  1. The 17 slots read cleanly. The reader then takes the trailer from slot 18, `Water`: Filled = 6 (the name's length, which passes the check), Head = the bytes "Wate" (0x65746157, which fails).
  2. The ledger resets with a Warning, but `ReadCreditBlock` then reads Balance, Awarded, Spent and the cap from inside Water's Made leg.
  3. The high bytes of Balance come from Water's first ring entry. Depending on that one value, the balance is either about 1e18 mcr (for example 0x7530... when the entry is 30,000 g), which the R5 repair keeps, or negative, so all three totals are repaired to 0 and the player's credit is lost.
  4. The export block is then read from a shifted offset too.
- **Same failure, other causes.** A name-length prefix that is off by a few bytes shifts the block the same way. So does a wrong credit-row count, which shifts the export block.
- **Knock-on effects:**
  - the trailer's `Checked` is also misread (see finding 3);
  - a dropped credit block resets the landing counts, so four more Toolkits can be bought while `FabricatorSpeedMul`, saved elsewhere, is kept (up to 1 + 8 x 0.15 = 2.2x);
  - the export lines' locked price has no upper bound on load (`Export:520-537`), so a shifted read can turn into a huge sale value at launch.
- **Caught by:** nothing. EC6 tests a count of -1 and edits that keep every length the same. CR9 and XE10 never edit the ledger.
- **Fix.**
  - The robust fix, at the next save bump: write an int32 byte length before each economy block (measured as `GetLedgerBlockBytes` already measures it), and have each reader seek to its start plus that length whatever it found inside.
  - Until then: treat a failed trailer check as lost; bound the repaired balance and totals by a sane maximum; bound a loaded export price by `V x 1400 / 1000 - L`.
  - Add EC6 cases for a count of 17, a count of 19 and a name length off by one, each asserting the credit is 0 and never larger.

### 3. should-fix: the ledger reader accepts values that break later (E1)

All three need a doctored or corrupted file. Each slips past the R1 rule "a malformed block resets with a Warning" and fails later instead.
- **Non-finite or negative values (`Econ:188`, applied at `Econ:253-254`).** The open kg and lifetime grams are not checked.
  - An open kg of -5.0 loads. The next roll (`Econ:73-75`) writes -5,000 g into the ring and lowers the lifetime. The next save writes that, and the load after it resets the whole ledger (`Econ:192`).
  - A NaN open kg never clears. `RoundToInt64(NaN)` is undefined: INT64_MIN on x86, 0 on arm64, so the same file gives different ledgers on each platform.
  - `NoteFlow` (`Econ:43`) also lets +Inf through.
- **`EconCheckedSol` is never checked (`Econ:263`, and `Econ:245` on the reset path).**
  - With a checked sol of 1,000,000, `SolNow <= EconCheckedSol` holds forever, so the ring never fills again.
  - On the reset path, `Checked > 0 ? Checked : ProductionCheckedSol` takes the bad block's value. In finding 2's shifted read that value is 0x????0072, built from Water's name and open-kg bytes.
- **Duplicate names differing only in case (`Econ:206`, `Econ:216`).** The order check is a case-sensitive ordinal, but the slot lookup by `FName` ignores case. So "Ice" followed by "ice" passes, and the second overwrites the first. The spec says a repeated name resets the block.
- **Caught by:** nothing. EC6's `AllZero` does not look at the checked sol, and no case edits a double.
- **Fix.**
  - Treat the block as bad unless every open kg is finite and above -0.001 kg (a real residual can sit just below 0) and every lifetime is at least 0.
  - Require `Checked == ProductionCheckedSol`; both roll in lockstep from the same `SolNow` (`RHSimGrowth.cpp:487-488`). Always use `ProductionCheckedSol` on a reset.
  - Reject a name whose `FName` is already in `Read`.
  - Make `NoteFlow` reject non-finite amounts.

### 4. should-fix: the playtest bot reports a refused second Forge as "ordered", so E3's Q-C (a) adaptation never ran (E3, RedHope)

- **Where.** `Source/RedHope/Private/RHPlaytest.cpp:2107-2113`: `SecondForgeSol` is set and the order is marked "ordered" before `OrderBuild(..., Forge, 24, -14)` is queued. `Order` (`:281-291`) only enqueues the command. A rejection arrives later, by `OnCommandRejected`, and nothing listens for it. The REPORT line (`:476-480`) prints "second Forge ordered at sol X".
- **Scenario.** In the default 50% run the order at sol 12 is rejected with "Footprint overlaps a road" (the spine road went down at sol 6). The colony keeps one Forge. Commit `93646b0` documented this, but the default path at HEAD still reports it as ordered.
- **Consequence.** The E3 and E3b playtest verdicts that went to the director were measured with the "scripted player adapts" step of Q-C (a) only partly applied: there was no second Forge, the one lever the Struct study found binding.
- **Caught by:** nothing. The playtest is compared byte for byte, not checked.
- **Fix.**
  - Record the second Forge only when a Forge site with a new Id appears, or on the absence of a rejection.
  - Place it with the clear-spot finder that `industry=1` already uses.
  - Re-run Q-C (a)'s bar with the bot doing what it claims.

### 5. nit: a half-gram open tally rolls into a -1 g ring entry, and the next load rejects it (E1)

- **Where.** `Econ:73-77` against the load check at `Econ:192`.
- **Scenario.** `NoteFlow(Water, Made, 0.0045)` rolls to 5 g, leaving -0.0005000000000000004 kg. With no flow the next sol, that rolls to -1 g. A save then a load resets the whole ledger.
- **How often.** Numerically, 966 of the 2,000 tallies k.5 g (k < 2,000) do this. No authored flow lands exactly on a half gram, so reachability is unproven.
- **Caught by:** nothing. EC5's amounts avoid exact halves.
- **Fix.** Clamp the rolled grams at 0 and keep the residual in the open kg.

### 6. nit: a jump of more than 8 sols keeps the old open tally as the oldest ring entry (E1)

- **Where.** `Econ:62-82`. After a 12-sol jump, Back 8 holds a sol from 12 back.
- **Against the spec.** E1's test table (EC5 row) says "a long jump of 12 sols gives 8 empty entries". EC5 (`RHSimEconomySuite.cpp:784-791`) asserts the code's behaviour instead: the open sol, then 7 empty sols.
- **Reach.** It mirrors C5d's ledger, and only debug clock jumps reach it.
- **Fix:** none needed. State it in the spec, or zero the first entry when Passed is capped.

### 7. nit: manifest effects are resolved once, at world start, for active rows only (E2a)

- **Where.** W:2410 (`ForEachManifestItem` skips rows that are not slice-active) and W:13393-13428.
- **Scenario.** `CanAddManifestItem` reads the live table, never the resolved effects. So a row whose verb failed to resolve, or one activated after world start, can be bought and charged, and then lands as nothing, with no Warning at landing.
- **The cap uses two values.** The landing count uses the `MaxStack` captured at world start (W:2447); the purchase check uses the live one (W:13408).
- **Reach.** Inert with today's data: all 14 rows are active, and CR0 checks every row resolves.
- **Fix.** Have `CanAddManifestItem` refuse a row with no resolved effect.

### 8. nit: data and config are pinned by tests, not validated in code (E2a, E2b)

- **Rows.** `BuildManifestEffects` (W:2395) accepts:
  - an Amount of 0 or less;
  - a fractional Kit count (0.4 rounds to 0 and does nothing);
  - a negative `Price_mcr` (the launch then raises the balance and drives Spent negative);
  - `MaxStack` on a verb that does not multiply or replace;
  - a `RoverAboard` target other than `RC_R`. `DeployWaitingRovers` (`RHSimRovers.cpp:281-288`) deploys only `RC_R`, so any other target waits aboard forever.
- **Config.** `AwardMcrPerKg` and `ShipMassCapKg` are not checked. A cap of 0 or less refuses every manifest add and every export.
- **The tests.** CR0 (`RHSimCreditSuite.cpp:290-301`) pins today's values, so an edit fails the suite. But its price check rounds `Mass_kg`, a float, so a fractional mass passes and breaks Lemma 1's whole-kg assumption. Its CSV-to-DT comparison also skips `Price_mcr` and `MaxStack`.
- **Fix.** Validate these at world start, with a Warning; compare the CSV and DT on every column.

### 9. nit: the award's float-to-int64 conversion is unguarded (E2b)

- **Where.** W:13368, `FloorToInt64(Kg * AwardMcrPerKg)`.
- **Scenario.** `PendingResearchFundingKg` is loaded with no check (W:14463). A non-finite value converts to INT64_MIN on x86, which `CreditPay` ignores, and saturates to INT64_MAX on arm64, which is paid; the next payment then overflows. Doctored saves only.
- **Fix.** Require a finite award between 0 and a sane maximum.

### 10. nit: closing the money pump is a pinned value, not a rule (E3)

- **Where.** XE0 (`RHSimExportSuite.cpp:100-146`) pins the nine `EarthBuys` rows exactly.
- **Gap.** No check ties `EarthBuys` to what the manifest lands. E3 section 2.1 itself expects Food to open later. Doing that, and updating XE0's pin, would quietly reopen the Colonist Transport pump, 9,000 cr a ship by the spec's own table.
- **Fix.** Add a check that no manifest row's landed resource (a Stock target, or Crew food) is `EarthBuys` unless its `Price_mcr` covers the sale value at x1.4.

### 11. nit: smaller items

- **`Debug_OpenManifestWithAward`, test only (W:13372-13383).** Called from the knob-0 rest state, it keeps the landed ship's `ManifestItems` (AdvanceQuotaLadder returned early). They are charged and landed a second time, and counted twice against the stack cap. Reset `ManifestItems` in the hook.
- **The deck (`RHCommandDeckPanels.cpp:700-702`).** It shows "this round's award X cr" for ever in the knob-0 rest state, because `AwardMassKg` stays stamped there by design (E2 R2).
- **A negative landing count** is dropped without a Warning (`Econ:416`).
- **`design-decisions.md:1400`.** The E3b heading still says "not yet committed".

---

## What holds (checked, not assumed)

**1. Money.**
- **Overflow.** The largest real products are 30,000 kg x 16,800 mcr for a sale and an award near 3.5e7 mcr. No int64 overflow is reachable from play; finding 2 and finding 9 need doctored bytes.
- **Rounding.** Every rounding goes the way the specs say:
  - the award is floored once on the whole sum (W:13368);
  - Mpm is one `llround` (`Export:170`), then the price is a floor division (`Export:171`);
  - the launch cost is a floor division (`Export:162`);
  - plateau lines are floored from the raw row (`Export:42`);
  - a short launch loads `floor(min(loadable, line))` and owes the whole-kg remainder (`Export:400-406`).
- **No double payment.** One award per met round, in `StepQuota`'s met branch only (W:2174). No payment on load.
- **No double charge.** `FinishLaunch` is the one debit (`Export:322-323`). A full launch called from `LaunchShipShort` returns at once (`Export:379-381`). Both launches leave `AwaitingManifest` in the same call.
- **No overdraft.** Cost is checked against the balance (`Export:276-277`, `:384`), then debited unchanged. `CreditPay` ignores non-positive amounts.
- **Save repairs.** The credit and export repairs (`Econ:335-371`, `Export:493-498`) restore `Balance == Awarded - Spent` and `0 <= SalesPaid <= Awarded`.
- **The E2b fix.** It holds for a count outside the valid range, for a cut-short block, and for every aligned malformed case (finding 2 is the one gap).

**2. Mass.**
- **The launch takes exactly what it checks.** It draws each launch line once, quota plus sale merged (`Export:120-143`), through `TakeGrowthResource` (`RHSimGrowth.cpp:261-307`), the same uncommitted holdings that `CanLaunchShip` checks. It records the drawn kg on E1's Out leg (`Export:349-350`).
- **The pool.** It can end at most 1e-4 kg below 0, as specified.
- **The life-support floor** (`Export:104-113`) covers Oxygen, Water and Food at 10 sols of the current head count.
- **Arrears.** They join the round's bill in the Open and AwaitingManifest phases and the next round's otherwise (`RHSimWorldSubsystem.h:2721`), so they are never counted twice. A full launch clears them, and a short launch replaces them.
- **E3b.** `RHSimGrowth.cpp:215-236` is exactly `Free = Held - (1 - share) x FullBill - Owed - Reserve` in every phase. `FullBill` is the open round's bill while it is Open, otherwise the next round's. `Owed` is the launch bill with its arrears plus the export lines, and only in `AwaitingManifest`, so nothing is held twice. The growth reserve and the launch floor agree for Oxygen.
- **E1's ledger sites.** About 45 `NoteFlow` sites record what actually moved, with no double recording (`TakeStructForBill`, `DigDeposit`, `NoteHaulCargoLost` and `ReturnHaulCargo` each record once). R8's formula matches the change in holdings.

**3. Determinism.**
- **The draw is in Id order.** `Buildings` is only appended to (W:10582), removed from with the order-preserving `RemoveAt` (W:10999), and loaded in saved order (W:14141), so it stays in ascending Id.
- **Every E-gate order uses the name string, never the FName index:** the slots (`Econ:25`), the capped rows (`Econ:279`), and the export lines (`Export:236`).
- **Floats** appear in money only in the award and in Mpm's one rounding, both as specified.
- **One event in both bands.** `LaunchShip` and `LaunchShipShort` are called only from the deck, the cheats and the bot, all between steps.
- **The ledger rolls** on C5d's threshold, with the same `SolNow` (`RHSimGrowth.cpp:487-488`).

**4. E2a.**
- **Same behaviour as before.** Each verb branch is pre-E2a's code: the same calls, order and keys, `+= 0.15` as a double, OrderLag's fallback, ImportStock's order, and the alert text.
- **No leftover name checks.** No literal row name is left in non-suite sim code; in RedHope, only the bot's lists and the debug cheats use them.
- **The stack cap** counts landings plus items aboard. Only `Debug_DeliverCargo` bypasses `CanAddManifestItem`, which the spec allows. It applies only to Toolkit (4) and Compute Core (1), per Q-B (a).
- **The 30,000 kg cap** is stamped at W:2156 and in the debug hook, cleared at W:2203, untouched by `AdvanceQuotaLadder`, and repaired on load.

**5. EarthBuys: the pump is closed.**
- **Nothing the manifest lands can be sold.** Soil, Seeds, SpareParts, Luxury, Glass and Food (crew provisions) all have `EarthBuys` FALSE.
- **No conversion path.** No recipe turns an import into an `EarthBuys` row (`RH_Recipes.csv`).
- **No salvage path.** Kit hardware has `CostStruct_kg` 0, so it salvages nothing (`ComputeSalvage`, W:10606-10670). Demolition and furniture refunds return less Mars-made material than was spent.
- **The weak point** is finding 10.

**6. Inertness, checked in code.** Claims that rest on runs (byte-identical playtests, battery line counts, mutation results) were not re-run, as this review forbids running.
- **E1.** Nothing outside the tests and the playtest log reads ledger values. E3 iterates the slot table, not the values.
- **E2a.** The branches are unchanged.
- **E2b.** The bot pre-checks against the award as HEAD did, so nothing it adds can be refused (Lemma 1, a 30,000 kg cap above the 25,100 kg maximum award).
- **E3.**
  - The take happens only at launch, so a suite that never launches is unchanged.
  - The met test equals HEAD's for rounds 0-9; the plateau and arrears change it by design.
- **E3b.** It changes no saved field.

**7. Architecture.** `RedHopeSim.Build.cs` depends only on engine modules. No RedHopeSim file includes a RedHope header or uses its symbols.

---

## Dropped (unverifiable, or not a defect)

**Not a defect (by design):**
- **The quota passing the cap with arrears.** Designed (E3 R1: "it still flies whole").
- **Prices and the award differing between the bands.** Designed (E2 section 5, E3 section 6).
- **Adding to a line re-quotes the whole line.** Designed (E3 section 2.2).
- **The short window counting from the integer met sol.** Designed (E3 R1).

**Unreachable in play:**
- **`LaunchShipShort` accepting the export-cap refusal that the deck's button hides.** The bill cannot grow during `AwaitingManifest` except through a config change.

**Could not be verified:**
- **Shipped-build sort order.** In non-editor builds an `FName`'s display case comes from its first registration, so `ToString()`-based sorts could differ from editor builds. The capitalised spellings are registered first by RedHopeSim file statics, and the lowercase ones (`RHCommandDeckPanels.cpp:62-65`, `:269-304`) only at runtime. Not verifiable without a cooked build.
- **UE's `FString` reader on a huge corrupted length.** It does not matter for finding 2, which needs only a small shift.
- **Thread safety** of the deck's per-frame reads.
- **Deep-cell oxygen with more points than cells.** The planner keeps the two equal.
- **Agent-band robots acting outside the step at a sol boundary.** The spec accepts it within its 5% bar.

**A design observation, not a code defect:**
- **The launch floor ignores the crew aboard.** It uses the present head count. A Colonist Transport aboard adds 20 people at landing, so a floor of 10 sols for 8 people is 2.9 sols for 28 if nothing were produced during the 3-sol transit. The director may want the floor to count the crew aboard.
