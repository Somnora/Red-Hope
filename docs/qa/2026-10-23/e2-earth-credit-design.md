# E2, Earth credit: design (2026-10-23)

Step 3 (economy), gate E2. **Design only; nothing here is built.** The code was read at HEAD `46854bf` (D6c, save v49) with `git show HEAD:`. D8 is being built in the same tree. D6d, D6e, D7 and E1 are designed but not built. Line numbers are at HEAD. They will move, so every anchor is also named by its function.

Short names used below:
- "W" is `Source/RedHopeSim/Private/RHSimWorldSubsystem.cpp` and "H" is its header.
- "C" is `RHSimCommandlet.cpp`.
- "P" is `Source/RedHope/Private/RHPlaytest.cpp`.
- "K" is `Source/RedHope/Private/RHCommandDeckPanels.cpp`.

**Binding rulings:**
- **Q1a:** Earth pays in credits. Unspent credits carry over. Each ship keeps a mass cap.
- **Ruling 7:** the manifest is a mass budget plus packages.
- **Q2a:** Earth takes the quota. That is E3, but E2 must not block it.
- **Q3a:** a Martian currency comes later.
- **Q4a and ruling 11:** there is no money inside the city, and Hope is never spent.

**Built on E1** (`docs/qa/2026-10-22/e1-ledger-design.md`, REVISION included). E2 follows E1's general rule that money is held as `int64` milli-credits (mcr). It does **not** read E1's `EarthValue_mcr` column; E3 does. E1 must be built first (the plan's order), because E2 re-homes E1's ship-landing hooks (section 3).

## What E2 builds, in one paragraph

The colony gets a saved, integer **credit balance**, held in milli-credits, that never resets. When a quota is met, `StepQuota` works out today's award exactly as now: on time or late, times the requisition multiplier, plus research funding, all in kg. It then pays `floor(award_kg x AwardMcrPerKg)` milli-credits into the balance (1,000 mcr = 1 credit per award-kg by default). Each manifest row gets a credit price (by default 1 credit per kg of the item). Composing a ship is limited by two things:
- the balance, net of what this manifest already costs;
- the ship's own **mass cap**, stamped on the ship when its manifest opens (`ShipMassCapKg`, 30,000 kg, placeholder).

Credits are taken at launch, and what is left carries over. Manifest effects become **verbs** read from the data. Effects that multiply or replace a value get a **stack cap**, refused at the manifest. The playtest bot keeps spending exactly what it spends today, so the 110-sol runs stay byte-identical. A switch lets it spend the carry-over as well.

## 1. The credit balance

### 1.1 State (new members, `H`, beside `AwardMassKg` at H:3678)

| Member | Type | Meaning | Saved |
|---|---|---|---|
| `CreditBalanceMcr` | int64 | the colony's Earth credit; never negative; never reset by the ladder | yes |
| `CreditAwardedMcr` | int64 | lifetime total paid in | yes |
| `CreditSpentMcr` | int64 | lifetime total spent | yes |
| `ShipMassCapKg` | double | the cap of the ship now being composed or in flight; 0 when there is none | yes |
| `ManifestLanded` | `TMap<FName,int32>` | landings of each **stack-capped** row (section 3.3); lookup only | yes, sorted |

`AwardMassKg` (double, H:3678) **stays exactly as it is**: computed at W:2122-2128, reset at W:2253, saved at W:13357. Its meaning is now "this round's award, in kg terms". So:
- the CEO line (W:2131-2137) keeps its text;
- `OnQuotaMet.Broadcast(QuotaMetSol, AwardMassKg)` (W:2138) is unchanged;
- `GetAwardMassKg()` (H:2280) is unchanged;
- every suite and playtest line that prints them is unchanged.

**Invariant (asserted by tests):** `CreditBalanceMcr == CreditAwardedMcr - CreditSpentMcr`, and `CreditBalanceMcr >= 0`.

### 1.2 The award (one step, in `StepQuota`)

Today's met branch (W:2103-2138) is unchanged up to the funding add (W:2124-2129). Straight after it, before the CEO log line, comes one call:

```
PayQuotaAward(AwardMassKg);   // new
// PayQuotaAward(Kg):
//   const int64 Mcr = FMath::FloorToInt64(Kg * AwardMcrPerKg);   // AwardMcrPerKg: new RH_Config row, 1000
//   CreditBalanceMcr += Mcr; CreditAwardedMcr += Mcr;
//   ShipMassCapKg = ShipMassCapCfgKg;                             // RH_Config ShipMassCapKg, 30000: the ship's cap is stamped now
```

- **The floor is taken once, on the sum** (base x multiplier + funding), never per term. This is what makes the proof in 1.4 exact.
- `PendingResearchFundingKg` keeps its name, its kg unit and its save slot (W:13556). It is converted at the award like any other award-kg. The `-tiers` check at C:4144 and the ladder's `pending=0` line do not move.
- `PayQuotaAward` is the **only** way credit enters in E2. E3 adds its sales through the same function under a different reason. If Q2a needs the award moved to launch, E3 moves that one call; nothing else in E2 assumes when the award is paid.

### 1.3 Spending

- `GetManifestCostMcr()`: the sum over `ManifestItems` of `Row->Price_mcr`, in int64, read from the live DT the same way `GetManifestMassKg` (W:13134) is. It is derived and never saved.
- `LaunchShip` (W:13178) keeps its phase refusal. On success, `CreditBalanceMcr -= GetManifestCostMcr()` and `CreditSpentMcr += GetManifestCostMcr()`. These run after the phase change and before the log.
- The launch log line gains the credits: `Ship launched: %d items, %.0f kg of a %.0f kg cap, %s cr spent, %s cr carried, arrival in %.0f sols`. This is a deliberate move (section 7).
- `AdvanceQuotaLadder` (W:2253-2255) still zeroes `AwardMassKg` and empties `ManifestItems`. It does **not** touch the balance: that is the carry-over. It sets `ShipMassCapKg = 0` (no ship is being composed).
- **No refund path:** there is still no "remove manifest line" call. Credits are taken only at launch, so a line added in error costs nothing until launch. A remove call is a presentation follow-up (risk 6).

### 1.4 The claim: "a colony that spends everything sees no change", proved

**Lemma 1 (fresh ship).** Take a ship whose balance before the award is 0, with `ShipMassCapKg >= AwardMassKg`, at the default exchange (`AwardMcrPerKg` = 1000, `Price_mcr` = 1000 x `Mass_kg`). For any sequence of `AddManifestItem` calls, E2 accepts exactly the items HEAD accepts.

*Proof:*
- Every active row's `Mass_kg` is a whole number (`RH_ManifestItems.csv`: 200 to 8,000), so the manifest mass M before an add, and M + m after it, are whole numbers. The `float` to `double` sum at W:13134 is exact for whole numbers below 2^24.
- HEAD accepts an item when M + m <= A, with A = `AwardMassKg` (W:13166).
- E2 accepts it when 1000(M + m) <= B, with B = floor(fl(1000A)).
- 1000 x floor(A) is a whole number that is exactly representable and no larger than 1000A. Rounding is monotone, so 1000 x floor(A) <= fl(1000A), and therefore 1000 x floor(A) <= B.
- If A is a whole number, fl(1000A) = 1000A exactly, so B = 1000A.
- In every case floor(B / 1000) = floor(A). For a whole number N: N <= A iff N <= floor(A) iff 1000N <= B.
- The mass-cap check (cap >= A >= M + m) never fires first. ∎

This matters because **the awards are fractional in almost every run.** Rivals are active in the default colony, so the requisition multiplier is not exactly 1. Examples: `9999 kg` (x0.9999...), `9887 (x0.99)` and `13207` in the playtest. Comparing credits to rounded kilograms would have moved refusals at the margin; the floor-on-mcr rule cannot.

**Lemma 2 (later ships).** On any later ship the balance is B_n + carry, with carry >= 0. Every item HEAD accepts, E2 also accepts, but E2 may accept more. The difference is exactly the carry, and nothing else.

So "spends everything" holds **exactly when the carry is 0**. That case never occurs on its own: items are lumpy (the cheapest is 200 kg), and the floor leaves up to 999 mcr. **Every run that launches more than one ship with an unspent remainder can move** unless its spender caps itself at the round's award.

E2 therefore gives the playtest bot that cap (section 4). Lemma 2 also tells us which suites can move: those that add items to a second or later ship. **There are none.** The only suite that adds an item is `-arrivals`: one Colonist Transport of 8,000 kg against an award of about 9,999 kg (C:6697). The suites that launch ships launch them empty: `-ladder` C:1714, `-resupply` C:4874, `-citygrowth` C:9245 and :9279, `-framing` C:15000. **No suite outcome moves.** Only text lines move; section 7 lists them.

### 1.5 Ruling 11 and Q4a

- No credit value is read by `StepHope`, `StepPopulation`, `StepCityGrowth` or any colonist rule. Credits exist only at the manifest.
- There is no wage, price, debt or poverty state anywhere.
- CR10 pins this (section 7).

## 2. Manifest prices, and the ship's mass cap

### 2.1 Data

New columns in `docs/data/RH_ManifestItems.csv` and in `FRHManifestItemRow` (`Source/RedHopeSim/Public/Data/RHRows.h:153-164`), added after `Effect`:

| Column | Type | Default for every row today |
|---|---|---|
| `Price_mcr` | int32 | `Mass_kg x 1000` (SoilPallet 1,000,000 ... ColonistTransport 8,000,000) |
| `Verb` | FName | section 3 |
| `Target` | FName | section 3 |
| `Amount` | **double** | section 3 |
| `MaxStack` | int32 | 0 (none), except Toolkit 4 and ComputeCore 1 |

The `Effect` column stays as display text (the picker's tooltip, K:539). Each Notes cell gains "Price placeholder, director review." The usual workflow applies: the director compiles, the CSV is re-imported, and the DT is live-synced. The pure-data rule applies.

New `RH_Config` rows, each read at config load next to `QuotaRepeatBillMul`:
- `AwardMcrPerKg` = 1000 (mcr per award-kg; placeholder; E3 is where the director tunes Earth's premium, see E1 section 3's note);
- `ShipMassCapKg` = 30000 (kg; placeholder).

The C++ fallbacks equal the CSV.

### 2.2 The cap is a property of the ship

- It is stamped into `ShipMassCapKg` at the award (1.2), saved, and cleared when the next quota opens.
- A config change while a manifest is open does not move that ship's cap. CR5 checks this with `Debug_SetShipMassCapCfg`.
- **Why 30,000 kg:** the largest award the data can produce is Q3 on time, 14,000 x (1 + `RequisitionEarthBonus` 0.4) = 19,600 kg, plus all of the research funding, 500 + 800 + 1,200 + 3,000 = 5,500 kg. That is **25,100 kg**, under the cap. So under Lemma 1 the cap never binds on a fresh ship, and in every run today it binds only on saved credits. The director may want it lower (Q-A).

### 2.3 `AddManifestItem` (W:13153-13176), new order of checks

1. The phase check, unchanged.
2. The row check, unchanged.
3. **The stack cap** (section 3.3).
4. **The mass cap:** `GetManifestMassKg() + Row->Mass_kg > ShipMassCapKg` refuses with `Over the ship's mass cap: %.0f + %.0f > %.0f kg`.
5. **Credits:** `GetManifestCostMcr() + Row->Price_mcr > CreditBalanceMcr` refuses with `Over budget: %s + %s > %s cr` (whole credits, with the fraction shown when it is not zero).

The success line becomes `Manifest: +%s (%.0f kg, %s cr) -> %.0f / %.0f kg, %s / %s cr`.

A new const query, `CanAddManifestItem(FName, FString& OutWhy) const`, runs checks 1-5 without adding. `AddManifestItem` calls it, and the deck's ADD button reads it (2.4). There is one rule and no copy.

### 2.4 Presentation (RedHope; D8 is building in this tree, so rebase there)

- The picker's "Cargo budget" bar (K:526-530) becomes two bars:
  - **Credits**: this manifest's cost against the balance;
  - **Ship mass**: kg against the ship's cap.
- Each row shows its price beside its mass (K:559).
- ADD (K:565-570) reads `CanAddManifestItem`, and when it is greyed the reason is the button's tooltip.
- A one-line credit readout sits in the Earth / objective panel: balance, and last award.
- `RH.Status` and the manifest cheat line (`RHCheats.cpp:2233`) print the balance and the cap.
- **No new morale, sickness or evacuation wording**, so the Gate-D review is not engaged. The new strings ("Credits", "Ship mass", the refusal reasons) are new rows in the wording table as placeholders.

## 3. Manifest effects become data-driven verbs

### 3.1 The verbs

`ApplyManifestItemEffect(FName)` (W:2346-2441) keeps its signature. `Debug_DeliverCargo` (H:1320) calls it from 57 suite sites and two cheats, so it must stay. It now looks up the row, resolves `Verb` to an enum (a lookup built once at DT load, never a string compare per landing), and dispatches.

**Every branch reproduces HEAD's statement exactly:** the same calls, in the same order, on the same keys, with the same log and alert text.

| Verb | Target | Amount | HEAD statement it replaces | Rows |
|---|---|---|---|---|
| `Stock` | resource row | kg | `AddStock(Target, Amount)` | PartsCrate (SpareParts, 10), SoilPallet (Soil, 1000), SeedVault (Seeds, 200), GlassCrate (Glass, 400), LuxuryGoods (Luxury, 300) |
| `Kit` | building row | count | `ImportStock.FindOrAdd(Target)++` repeated Amount times | SolarPack (SolarArray, 1), BatteryPack (BatteryBank, 1) |
| `Robot` | robot row | count | the `SpawnRobotTracked` block at the fixed `(3000, -3000, 50)` cm, with the broadcast | AdvExcavator (RC_E2, 1), ExtraHauler (RC_H, 1) |
| `RoverAboard` | robot row | count | `++ImportStock.FindOrAdd(Target)` then `DeployWaitingRovers()` | CargoRover (RC_R, 1) |
| `FabSpeed` | - | add | `FabricatorSpeedMul += Amount` | Toolkit (0.15) |
| `OrderLag` | config row | fallback | `OrderLagSeconds = GetConfigScalar(Target, Amount)` plus the "Compute core online" line | ComputeCore (OrderLagTier2_s, 8) |
| `Crew` | `Pod` or `Transport` | - | the CrewPod or ColonistTransport block, with its config members and its Gate-D-approved texts verbatim | CrewPod (Pod), ColonistTransport (Transport) |

- **`Crew` is the one verb whose target is a style key.** The two blocks differ in their config members (`CrewPodColonists` / `CrewPodFoodKg` against `ColonistTransportColonists` / `...FoodKgPerColonist`, H:3308-3309) and in approved wording. Moving that wording into a CSV would re-open Gate D, so the data picks which of the two blocks runs.
- **An unknown verb, or a target that does not resolve,** logs a Warning at DT load and applies nothing at landing. `cargo unloaded: %s` (W:2440) is still printed for every item, as HEAD does for an unknown name.
- **Bit-exactness of `Amount`.** It is a `double` column. The Toolkit's 0.15 must be the same double as HEAD's literal `0.15`; a `float` column would make it 0.15000000596, and with it every later fabrication time (the playtest buys a Toolkit on ship 1). CR0 asserts `Amount == 0.15` bit-exact, and that every stock amount is exactly HEAD's integer.
- **`ImportStock` order.** `ImportStock` is a TMap saved in insertion order (W:13360). `Kit` and `RoverAboard` must call `FindOrAdd` on the same key at the same moment as HEAD. CR1 asserts that the key order after a fixed delivery sequence matches.

### 3.2 E1's landing hooks

E1 records the In leg at the ship-landing sites inside `ApplyManifestItemEffect` (E1's table: W:2332, :2359-2362, :2372, :2399 at `df3e479`). With verbs they collapse to two sites: the `Stock` verb, and the `Crew` verb's food. E1's EC3 landing fixture must still pass unchanged. That is part of E2's bar.

### 3.3 The stack cap

- **What it caps:** only rows with `MaxStack > 0`. Today those are **Toolkit 4** (fabricator speed at most 1.6) and **ComputeCore 1** (a second one does nothing: W:2356 sets the same tier).
- **The count:** `ManifestLanded[Row]` plus the copies already on this manifest. If that plus one is over `MaxStack`, `AddManifestItem` refuses: `%s: the colony already has the most of this it can use (%d)`.
- **Refused at the purchase, never wasted at landing.** `ApplyManifestItemEffect` increments `ManifestLanded` only for capped rows. That includes `Debug_DeliverCargo`, which bypasses the purchase check by design, so suites can still deliver what they like.
- **No safety clamp at landing.** Only one manifest is open at a time and only landings raise the count, so the purchase check is enough. CR6 proves it.
- **What is not capped:** stock, kits, robots, rovers and crew. They are bounded by credits and the ship's mass. The open point in design-decisions ("a Toolkit is +15% each; solar and robot packs are unlimited") is closed for the multiplying effects only. Q-B asks whether the director wants more.
- **Inert today:** the bot buys one Toolkit (P:1655-1656) and no ComputeCore, and no suite buys either through the manifest.

## 4. The playtest bot

### 4.1 The change (P only, in the RedHope module)

The bot's per-ship spending is pinned to **this round's award in kg terms** (`GetAwardMassKg()`), exactly HEAD's budget. It leaves the carry-over unspent:
- `LoadCargo` (P:292-330) checks each item **before** calling the sim: if `GetManifestMassKg() + Row.Mass_kg > GetAwardMassKg()`, it logs `CARGO %s refused: Over budget: %.0f + %.0f > %.0f kg`. That is **HEAD's own message, byte for byte**, so the 66 `refused: Over budget` lines of the full run (for example the ones at sol 31.71) read the same. It does not call `AddManifestItem` for that item.
- The fill loop (P:310) stops on the same check.
- `ManifestListMassKg` (P:276) and the rover room test (P:1825) already read `GetAwardMassKg()`. They are unchanged.

**Why this is exact.** In every item the bot lets through, the mass is at most the award. By Lemma 1 the credits cover it, because the balance is at least floor(1000 x the award) mcr, and the cap is at least the award. And only the Toolkit is stack-capped, bought once. So the sim never refuses anything the bot sends, and the sequence of `AddManifestItem` calls equals HEAD's.

Nothing in E2 reads the balance except the manifest checks. So the colony, the quota ladder and every `[Playtest]` line are unchanged.

### 4.2 Expected moves

- **Default runs** (`rh_playtest.sh 110 <label> guide full|lean`, with `RH_PLAYTEST_DISTRICTS=1`): the `.txt` is **byte-identical** to the last baseline before E2 (`cmp` clean, the same sha256).
- The full `.log` moves only in sim lines: each `Manifest: +` line and each `Ship launched` line carries credits.
- The balance grows by the unspent remainder each ship. In the perf baseline: ship 1 leaves 6,904 kg (3,000 of 9,904), ship 2 leaves 7,078 (4,700 of 11,778), and every later ship less than 500, because it fills with Parts Crates.

### 4.3 The opt-in run that shows what credits change

- `RH_PLAYTEST_CARRYOVER=1` (or the arg `carryover=1`) turns off the bot's pre-check, so the sim's own rule (credits and cap) is the budget.
- It is **not** a baseline. It is reported once, as a table beside C5e's: population at 110, rounds met, credits carried in at each ship, Parts Crates and Solar kits a ship, and any cap refusals.
- **Expected direction:** ship 3 arrives with about 14,000 credits carried. The fill loop then adds Parts Crates and the kits that were refused at sol 31.71 (`13100 + 400 > 13207`), up to `GMaxArrays` and the 30,000 kg cap. After that the ships return to about one award each.
- The bot spends no more than the award plus the carry-over. A rule that saves credits for a big item is not in E2.

## 5. Determinism and era parity

- **One payment, one step.** `PayQuotaAward` runs in `StepQuota`'s met branch. `StepQuota` sits at the same index in both bands: after `StepEarth`, before `StepPower` (agent W:649, era W:821).
  - The award is a pure function of saved state at that step: the quota row, on-time or late, the requisition multiplier read from the saved `IdentityAxis` and `EarthTension`, and the saved pending funding.
  - The floor is integer, and nothing else is accumulated.
- **What is not new.** The two bands can meet a quota on different steps, and `EarthTension` drifts per step (`StepEarth`), so the agent band and the era band may compute slightly different `AwardMassKg`. That is true at HEAD and not compared by `-eraparity`. E2 adds no tolerance and no new drift. Each band's credit is exactly the floor of its own award. CR8 asserts this per band and prints the cross-band difference as information.
- **Landing is agent-only in practice:** `CanEnterEraMode` refuses while a ship is inbound (W:711-715). The verbs are the same code in both bands, as HEAD's name checks were.
- **Composing and launching** are direct calls between steps (K:627, :641; `RHCheats.cpp:3011`, :3027), as at HEAD. `AwaitingManifest` is allowed in era. CR8 composes and launches in the era band too.
- **Order:** `ManifestLanded` is iterated only at save, sorted by name string with a case-sensitive ordinal compare, never by FName index (the covert seed bug of 2026-07). `GetManifestCostMcr` iterates the `ManifestItems` array in order.
- **Reads never insert.** Every new read of `ImportStock`, `Stocks` or `ManifestLanded` uses `Find` or `FindRef`. Only the verbs that HEAD already wrote with `FindOrAdd` keep it (E1 R7's rule).

## 6. Save

**Version: the next free one.** The builder assigns it. The queue before E2 is D6d (v50), D6e (v51), D7 (the next free version, v52 as designed), D8 (no bump) and E1 (the next free version). If all land in that order, E2 is **v54**. The number here is a forecast, not a reservation.

**The credit block** is written straight after E1's ledger block, the last thing in the file:
- `CreditBalanceMcr`, `CreditAwardedMcr`, `CreditSpentMcr` (3 x int64);
- `ShipMassCapKg` (double);
- the number of rows with `MaxStack > 0`, then per row, sorted by `ToString()` with a case-sensitive ordinal compare: the name (FString) and `ManifestLanded` (int32).

**Its size is fixed by the table** (today 2 capped rows), never by history.

**Load.**
- The block sits at the end, so it is read after teardown, as C5d's and E1's blocks are. It therefore repairs with a **Warning**, never an Error (E1 R1; W:14499-14501 is the one block that still logs an Error).
- A negative balance or lifetime total, `Awarded - Spent != Balance`, or a negative or non-finite cap: the field is repaired (balance = max(0, Awarded - Spent); cap = 0 when no manifest is open, else the config cap), with a Warning.
- Names must be strictly ascending. An unknown name is skipped with a Warning; a missing one starts at 0. An unsorted file resets every count to 0 with a Warning.
- The previous version is refused by the strict check (W:13775).

**What E1's EC1 must learn.** E1 R7 finds the start of its block as "the file size minus the block's size". Once E2 appends after it, that is the file size minus E1's block minus E2's block. Both sizes are fixed by tables, so E2's builder updates EC1's offset in the same commit.

**Checkpoints.** The bump refuses the Midgame and Late checkpoints. Re-cut them (`rh_checkpoints.sh all --force`) and load both in a real-renderer window, as D5 and D6a did.

## 7. Tests

### 7.1 `-credit`, a new fails-loudly suite

It goes in its own file, `Source/RedHopeSim/Private/RHSimCreditSuite.cpp`, and is added to `rh_battery.sh` (one more suite, one more run). No path logs an Error on purpose: the battery's `run_one` turns red on any `LogRedHopeSim: Error`.

| Check | What it pins |
|---|---|
| CR0 | **Data.** For every active manifest row: `Price_mcr == 1000 x Mass_kg`, the verb is known, the target resolves (a resource, building, robot or config row, or `Pod` / `Transport`), `Amount` is bit-exact to HEAD's literal (0.15; 10, 1000, 200, 400, 300; 1; 8), and `MaxStack` is 4 for Toolkit, 1 for ComputeCore and 0 for the rest. The CSV equals the live DT. `AwardMcrPerKg` is 1000 and `ShipMassCapKg` is 30000. The cap is at least 14,000 x 1.4 + the sum of `FundingKg`. |
| CR1 | **The verbs, one fixture per active row**, each on a fresh colony through `Debug_DeliverCargo`, against expected deltas written in the suite: stock kg; the `ImportStock` delta and the **key order** after SolarPack, BatteryPack, CargoRover; the robot count and row; `FabricatorSpeedMul == 1.0 + 0.15` bit-exact; `OrderLagSeconds == 8`; the crew blocks both ways (beds free gives +4 with 200 kg of food, and +20 with 1,000 kg; no beds means it stays aboard and its alert is HEAD's text); the rover waits aboard with no road. |
| CR2 | **The award.** Q1 met on time, with the requisition forced fractional (rival on, tension set) so that `AwardMassKg` has a fraction both above and below 0.5. The balance delta is `floor(AwardMassKg x 1000)` mcr. A late award follows the late tier. Funding of 500 kg rides the same floor, taken on the sum. `AwardMassKg` and the `OnQuotaMet` argument are bit-identical to a twin with the credit code switched off (`Debug_SetCreditEnabled(false)`, runtime only). |
| CR3 | **Lemma 1, by enumeration.** For awards {9999.3, 9998.7, 10000.0, 13207.4, 11778.0, 8380.6} and 2,000 fixed manifest sequences per award (a seeded LCG inside the suite; items drawn from the active rows), on a fresh ship: HEAD's rule (`M + m > A`, computed in the suite) and `CanAddManifestItem` accept and refuse the same items at the same positions. |
| CR4 | **Carry-over.** Q1 launched empty: the balance keeps floor(1000 x A1). The ladder advances: `AwardMassKg` is 0 and the balance is unchanged. Q2 met: the balance is the sum. An item costing more than Q2's award alone is accepted. Launch: the balance is the old balance minus the cost. The invariant `Balance == Awarded - Spent` holds after every step. |
| CR5 | **The cap.** With a balance of 40,000 credits (debug grant) the manifest stops at the cap with the cap message. The cap is stamped at the award: changing the config cap mid-manifest does not move it. The next ship gets the new config value. The cap is 0 between rounds. |
| CR6 | **The stack cap.** Toolkits 1-4 are accepted across ships and the 5th is refused at the manifest (landed plus aboard); the 4th and 5th on one manifest are counted together. A second ComputeCore is refused. After four Toolkits, `FabricatorSpeedMul` is bit-identical to HEAD's sequence of four `+= 0.15`. `Debug_DeliverCargo` bypasses the check and still counts. |
| CR7 | **Launch.** An empty launch spends 0. A launch outside `AwaitingManifest` is refused as at HEAD. The balance is never negative after any sequence in CR3. |
| CR8 | **Era parity.** One save, the quota met in the agent band and in the era band: each band's balance delta equals `floor(1000 x` its own `AwardMassKg)`, and both pay on the met step. The met sols and the awards are printed as information. Composing and launching in the era band give the same balance as in the agent band from the same state. |
| CR9 | **Save.** Mid-manifest (items aboard, a carry-over, a Toolkit landed): save, load, save is byte-identical, and the block's names read back ascending. A doctored negative balance repairs with a Warning and the colony loads. An unsorted block resets the counts. The previous version is refused. |
| CR10 | **Ruling 11 and Q4a.** A twin of 10 sols with a debug grant of 10^6 credits against none: Hope, the population, growth starts and every stock are bit-identical. Nothing outside the manifest moves with money. |

### 7.2 What the battery moves, suite by suite

The builder re-counts at build time with `grep -c "Ship launched\|Manifest: +"` over the battery logs and the version grep from E1 R4.

| Suite | Lines that move | Why |
|---|---|---|
| `-arrivals` | 2: the `Manifest: +ColonistTransport` line and its `Ship launched` line | credit text |
| `-citygrowth` | 5 `Ship launched` lines | credit text |
| `-resupply` | 5 `Ship launched` lines | credit text |
| `-ladder` | 1 `Ship launched` line | credit text |
| `-framing` | 1 `Ship launched` line | credit text |
| Every suite with a save-version line | E1 R4's 15 lines, plus those D6d, D6e, D7 and E1 add | the bump |
| `-depth` DP11 | the whole-file sizes | grow by the credit block |
| every `(N bytes` line | masked by `diff_battery.py` | grow by the block; the builder states the byte delta |
| `-economy` (E1) | EC1's offset (code), no log line | the block after E1's |
| new | `credit.log` | the new suite |

**Nothing else may move.** In particular:
- every `CEO TRANSMISSION` line (64 lines, in 34 suites) is unchanged;
- every `cargo unloaded` line (61, in 14 suites) is unchanged;
- every `R2` to `R11` line of `-resupply` and every `LADDER` line is unchanged;
- the pins `power: gen 50 W load 20 W` and `deposit Regolith_A: 40645 kg` hold.

**Playtests:** the default `.txt` files are byte-identical (4.2). The click test passes, with `manifest.ADD` unchanged. Capture the picker's two bars.

`-scale` (outside the battery): its 80 Colonist Transports go through the `Crew` verb. Every digest must be identical, and the time inside the noise.

## 8. Risks

1. **The verb refactor is the real risk, not the money.** 57 suite deliveries and every playtest landing go through it. One reordered `FindOrAdd`, one `float` amount or one changed alert breaks bytes far from the cause. CR1 and the line-identical battery are the guard. Build the verbs first, prove the battery unchanged, then add credits.
2. **The carry-over is invisible to the default bot.** The baseline never spends it, so a defect in carry-over spending appears only in CR4 and in the opt-in run. Run the opt-in once per E-gate.
3. **The CEO line stays "kg manifest budget".** This is kept verbatim so that 64 lines in 34 suites do not move. The player sees credits on the deck. Change the wording in E3, when the award itself changes.
4. **Two money scales.** Award credits are 1 per award-kg, while E1's Earth values make Q1's bill worth 2,700 credits. E3 must reconcile them (`AwardMcrPerKg` against `EarthValue_mcr`). E2 does not.
5. **Merge with D8 and the step-2 gates.** E2 touches K, P and `RHCheats.cpp` in RedHope, where D8 is being built. It also appends after E1's block. Rebase on the code at build time, not on these line numbers.
6. **No way to remove a manifest line.** It existed before E2, and credits taken at launch keep it harmless. Adding "remove" to the picker is a small follow-up.
7. **Two blocks at the end of the save.** E1's and E2's blocks both sit after teardown and both repair rather than refuse. A third economy block (E3's exports) should follow the same rule or move all three before teardown in one gate.

## Director questions

**Q-A. How big is a ship?**
- **(a) (recommended):** 30,000 kg per ship, a config row stamped on each ship. That is more than any award the data can make today (25,100 kg at most), so the cap only matters once you have saved credits. Every run today is unchanged.
- (b) 15,000 kg. A ship carries about one award's worth, so saved credits must be spread over several ships. It also caps a very loyal colony's best award (up to 25,100 kg). No battery run reaches 15,000 kg today.

**Q-B. What does the stacking cap cover?**
- **(a) (recommended):** only effects that multiply or replace a value: at most 4 Toolkits (fabrication up to 1.6x) and 1 Compute Core over the whole game, refused when you try to add one too many. Supplies, kits, robots, rovers and crew stay limited only by credits and the ship's mass. Nothing in any run changes.
- (b) As (a), plus a per-ship limit on fleet items, for example 2 robots or rovers a ship. Nothing today buys robots through the manifest, so no run moves, but a player who saves up cannot buy a fleet in one ship.

---

# PROFESSOR REVIEW

An independent adversarial review against HEAD (2026-10-23), recorded as returned. HEAD moved to `f461828` (D8) during the review. Between the two commits nothing changed in `RedHopeSim`, `RHPlaytest.cpp`, `docs/data` or `rh_battery.sh`. `RHCommandDeckPanels.cpp` gained 7 lines, so the K anchors move by about one line. Every W, H, C and P anchor still holds.

Verdict: **CLEARED WITH CONDITIONS**

## What holds (checked, not assumed)

- **AwardMassKg.** `StepQuota` is the only place that writes it (W:2122, :2127), apart from the ladder reset (W:2253) and load (W:13850). Outside the sim it is read only by:
  - the deck (K:528-529, :569);
  - the manifest cheat (`RHCheats.cpp:2233`);
  - the bot (P:316, :1825-1830);
  - the `-ladder`, `-resupply` and `-arrivals` suites.

  None of those reads changes under E2.
- **Callers of `AddManifestItem` and `LaunchShip`.** The only suite that adds an item is `-arrivals` (C:6697), on a fresh ship. The launch sites are C:1714, :4874, :9245, :9279 and :15000. The battery logs agree with the design's move table exactly: arrivals 2, citygrowth 5, resupply 5, ladder 1, framing 1.
- **`Debug_DeliverCargo`.** No suite delivers a Toolkit or a ComputeCore through it, so the stack cap is inert in every suite.
- **Playtest refusals.** In `d8_full_playtest.txt`, all 66 refusals are `refused: Over budget`. The `.txt` holds only `[Playtest]` lines. The `d6c` and `d8` transcripts are identical.
- **The largest award.** 14,000 x the requisition clamp of 1.4 (W:9906), plus 5,500 of research funding, is 25,100 kg. That is under the 30,000 kg cap.
- **ComputeCore.** "A second one does nothing" is true: the module only lowers the lag (W:12757, a `min`), and demolishing it raises the lag only from exactly tier 1 (W:10848).
- **The verb table.** It reproduces every HEAD branch (W:2346-2441). `FabricatorSpeedMul` is a `double` (H:2951), so a double `Amount` column is right. `ImportStock` is written in insertion order (W:13360), so key order does matter.
- **Bands and battery.** `StepQuota` sits at the same index in both bands (W:649, W:821). The battery turns red only on `LogRedHopeSim: Error` (`rh_battery.sh:48`), so repairing with a Warning is right.

## REQUIRED CHANGES

**1. Lemma 1's proof is missing its upper bound.**
- Section 1.4 also needs B < 1000(floor(A) + 1) when A is fractional. Otherwise an A just below a whole number N could round to fl(1000A) = 1000N, and E2 would accept a load of exactly N kg that HEAD refuses (W:13166).
- The bound does hold: N - A >= ulp(A) > A x 2^-53, and the rounding error of fl(1000A) is at most 1000A x 2^-53 < 1000(N - A).
- **Fix.** State this and its hypotheses (the factor is an exactly representable positive integer; CR0 pins it at 1000). Add `nextafter(10000, 0)`, `nextafter(9999, 0)` and `nextafter(13207, +inf)` to CR3.

**2. Stamping the cap inside `PayQuotaAward` couples the cap to payment, and that blocks E3.**
- E3 pays its sales at launch. If sales went through `PayQuotaAward`, each sale would re-stamp the cap, which contradicts section 2.2 and CR5.
- If E3 moves the award to launch, the cap would be 0 for the whole time the manifest is open, so every add would be refused.
- `PayQuotaAward` takes kg, but E3's sales are priced in `EarthValue_mcr`.
- **Fix.**
  - A `CreditPay(int64 Mcr)` primitive is the only writer of the balance and of the awarded total. `PayQuotaAward` becomes a wrapper around it.
  - Stamp the cap at the transition to `AwaitingManifest` (W:2112).
  - Clear the cap at landing (W:2153), not in `AdvanceQuotaLadder`. That function returns before its resets when the ladder is exhausted and the repeat knob is 0 (W:2238-2241).

**3. The bot's pre-check must sit behind HEAD's earlier refusals.**
- HEAD refuses in this order: the phase (W:13155), then an unknown or inactive row (W:13161), then the budget (W:13166).
- A pre-check that tests mass first would replace the first two reasons with a budget message. It also needs a row before it can read the mass.
- **Fix.** Pre-check only when the phase is `AwaitingManifest` and the row resolves with `SliceActive`. Otherwise call the sim and log its reason. The same applies to the fill loop (P:310).

**4. Every credit debug hook must keep the invariant and use the real path.**
- CR3's fractional awards, CR5's and CR10's grants and CR2's switch all need hooks that the design never names.
- A grant that writes only the balance breaks `Balance == Awarded - Spent`.
- **Fix.** Name each hook. Each one is runtime-only, and each goes through `PayQuotaAward` or `CreditPay`.

**5. The save repair must restore the invariant, and must keep the cap through `ShipInbound`.**
- When Awarded < Spent, `balance = max(0, Awarded - Spent)` leaves the invariant false.
- "Cap = 0 when no manifest is open" drops the cap of a ship in flight, which section 1.1 says it should hold.
- **Fix.** After the clamp, set `Spent = Awarded - Balance`. Keep the cap in `AwaitingManifest` and in `ShipInbound`.

## OBSERVATIONS (non-blocking)

1. **A ship can land in era.** `CanEnterEraMode` refuses only to *enter* era while a ship is inbound (W:711). A ship launched while already in era lands in the era band (W:821). This is HEAD behaviour and the verbs are shared code, but section 5's wording should say so.
2. **The Crew style key is acceptable.** CR0 must reject any other Target for `Crew`.
3. **A Warning for an unknown verb is acceptable.** CR0 is the primary guard.
4. **Prices can change between add and launch** (a live DT sync), which could drive the balance negative. `LaunchShip` should refuse, or clamp with a Warning.
5. **In the opt-in run, the rover room test (P:1825) still measures against the award.** It will defer rovers that credits would admit. Note this, or measure against min(cap, balance) in that mode only.
6. **Under Q2a, the Q3 bill passes 30,000 kg at round 10.** E3 must decide whether exports share the cap. Q-A should mention it.
7. **HEAD has 58 `Debug_DeliverCargo` sites**, one of them a cheat that takes any name. Re-count at build time.
8. **The version is a forecast, which is correct.** The coupling with E1's EC1 offset is real.
9. **The scope is complete.** Pinning the bot to the award is right. Build the verbs first, in their own commit.

VERDICT: CLEARED WITH CONDITIONS (required changes 1-5)

---

# REVISION

This section replaces the design wherever the two differ. The builder's order of authority is:
1. the director's answers;
2. this REVISION;
3. the PROFESSOR REVIEW;
4. the design.

Anchors are at HEAD `f461828`, which is the same code as `46854bf` for W, H, C and P; K moves by about one line. Rebase onto D6d, D6e, D7 and E1 when they land.

**R1. Lemma 1, completed** (required change 1). Section 1.4's proof now reads as follows.

*Hypotheses:*
- `AwardMcrPerKg` = k is a positive whole number, exactly representable (CR0 pins 1000).
- Every `Price_mcr` = k x `Mass_kg`.
- Every active `Mass_kg` is a whole number below 2^24.
- `ShipMassCapKg` >= A.

*Proof:* let A = `AwardMassKg` > 0, B = floor(fl(kA)), and M + m the whole-number manifest mass after the add.
- **(i) The lower bound.** k x floor(A) is exact and at most kA. Rounding is monotone, so k x floor(A) <= fl(kA), and therefore k x floor(A) <= B.
- **(ii) The upper bound.** If A is a whole number, fl(kA) = kA (exact below 2^53). Otherwise let N = floor(A) + 1. Then N - A >= ulp(A) > A x 2^-53. The rounding error of fl(kA) is at most kA x 2^-53, which is less than k(N - A). So fl(kA) < kN, and B <= kN - 1.
- **(iii) Conclusion.** From (i) and (ii), floor(B / k) = floor(A). For a whole number W: W <= A iff W <= floor(A) iff kW <= B.
- **(iv) The cap.** It is at least A, so check 4 never fires before check 5. ∎

CR3 adds the margin cases `nextafter(10000.0, 0)`, `nextafter(9999.0, 0)`, `nextafter(13207.0, +inf)` and `nextafter(8380.0, +inf)`, each with a manifest whose mass is exactly that whole number and one whose mass is that whole number minus 200.

**R2. Payment and the cap are separate** (required change 2). This replaces section 1.2's sketch and the cap lines of sections 1.3 and 2.2.

- **`CreditPay(int64 Mcr)`** (private) is the **only** writer of `CreditBalanceMcr` and `CreditAwardedMcr`. It ignores Mcr <= 0. E3 pays its sales through it with its own Earth-value arithmetic.
- **`PayQuotaAward(double Kg)`** = `CreditPay(FMath::FloorToInt64(Kg * AwardMcrPerKg))`. It is called once, in `StepQuota`'s met branch, after the funding add (W:2124-2129) and before the CEO line.
- **`LaunchShip`** is the only writer of `CreditSpentMcr` and the only place that subtracts from the balance.
- **The cap is stamped** at the transition to `AwaitingManifest` (W:2112, `QuotaPhase = ERHQuotaPhase::AwaitingManifest`): `ShipMassCapKg = ShipMassCapCfgKg`.
  - It is stamped before `PayQuotaAward` in the same branch. If E3 later moves the payment to launch, the cap is still there while the manifest is open.
  - Payment never touches the cap.
- **The cap is cleared** at landing, where `QuotaPhase = ERHQuotaPhase::Completed` (W:2153), before the cargo is applied. That is the end of that ship's life. `AdvanceQuotaLadder` does not touch the cap. Its early return in the knob-0 rest state (W:2238-2241) therefore leaves the cap at 0.
- **`AwardMassKg` itself** keeps HEAD's behaviour exactly, including staying stamped in the knob-0 rest state. Changing that would move `-resupply` R4's knob-0 line.
- **CR5's last clause becomes:** "the cap is 0 in `Open` and in `Completed`, and holds its stamped value in `AwaitingManifest` and in `ShipInbound`". It is tested both with the repeat knob on and with it at 0.

**R3. The bot defers to HEAD's earlier refusals** (required change 3). This replaces the first bullet of section 4.1.

`LoadCargo` (P:292-330) runs its pre-check for an item **only** when all of these hold:
- `Sim->GetQuotaPhase() == ERHQuotaPhase::AwaitingManifest`;
- the row resolves through `GetManifestItem`;
- `Row->SliceActive`.

In every other case it calls `AddManifestItem` and logs the sim's own reason, as HEAD does. When the pre-check runs and `GetManifestMassKg() + Row->Mass_kg > GetAwardMassKg()`, it logs HEAD's message byte for byte, `Over budget: %.0f + %.0f > %.0f kg` (the same `%.0f` arguments as W:13168-13169), and skips the call. The fill loop (P:310) applies the same gate before each add.

The default run cannot reach the deferral branch: every bot item is an active row, and every call is in `AwaitingManifest` (P:1650, :1740, :1768). So the `.txt` stays byte-identical.

**R4. The debug hooks** (required change 4). These are new in `H`. Each is runtime-only, never saved, and named in section 7.

| Hook | What it does | Used by |
|---|---|---|
| `Debug_OpenManifestWithAward(double Kg)` | Needs `Open` or `Completed`. Sets `AwardMassKg = Kg`, sets `QuotaPhase = AwaitingManifest`, stamps the cap, then calls the real `PayQuotaAward(Kg)`. | CR3, CR5, CR6, CR7 |
| `Debug_ResetManifest()` | Empties `ManifestItems`, sets `AwardMassKg = 0` and the cap to 0, and sets `QuotaPhase = Open`. The balance and the lifetime totals are untouched. | CR3 (2,000 fresh ships each) |
| `Debug_ZeroCredits()` | Sets the balance and both lifetime totals to 0 together, so each CR3 case starts on a truly fresh ship (Lemma 1's "balance before the award is 0"). | CR3 |
| `Debug_GrantCredits(int64 Mcr)` | Goes through `CreditPay`, so the invariant holds. | CR5, CR10 |
| `Debug_SetShipMassCapCfg(double Kg)` | Changes the config value only, never a stamped cap. | CR5 |
| `Debug_SetCreditEnabled(bool)` | When false, `PayQuotaAward` and the launch debit do nothing, and `CanAddManifestItem` skips check 5. Only CR2's twin uses it. | CR2 |

**After every CR step**, the suite asserts `Balance == Awarded - Spent` and `Balance >= 0`.

**R5. Save repair restores the invariant and keeps the cap in flight** (required change 5). This replaces section 6's repair bullet.

- **Balance and totals**, in this order:
  1. If any of the three is negative, set it to 0, with a Warning.
  2. If `Balance != Awarded - Spent`, set `Spent = Awarded - Balance`.
  3. If that leaves `Spent` negative, set `Awarded = Balance` and `Spent = 0`. Each step logs a Warning.

  The invariant holds after the load in every case. CR9 asserts it after each doctored file:
  - a negative balance;
  - Awarded < Spent;
  - a balance that disagrees with the two totals.
- **The cap:**
  - Non-finite or negative is invalid.
  - In `Open` or `Completed`, the cap is set to 0. In `AwaitingManifest` or `ShipInbound`, an invalid or 0 cap is set to the config value. Each repair logs a Warning.
  - A valid cap saved while the ship is in flight is kept.
  - CR9 adds a round trip while `ShipInbound` and asserts the cap survives.

**Observations taken in.**
1. **Section 5, corrected.** A ship launched while already in era lands in the era band (W:821). Era refuses only to *enter* while a ship is inbound (W:711). The verbs are shared code, so landing is the same in both bands. CR8 launches in the era band and lets the ship land there. Its cargo deltas must equal those from the agent band's landing from the same state.
2. **The Crew target.** CR0 rejects any `Crew` target other than `Pod` or `Transport`.
3. **Unknown verbs** stay a Warning at DT load. CR0 is the guard. A Warning in every suite log would also show in `diff_battery.py`.
4. **A launch never drives the balance negative.**
   - `LaunchShip` re-checks `GetManifestCostMcr() <= CreditBalanceMcr` before it changes the phase.
   - If prices were live-synced up between the add and the launch, it refuses: `Over budget at launch: the manifest now costs %s cr against %s cr; remove or re-price items`.
   - The phase does not change, and no Error is logged.
   - CR7 adds this case through a debug re-price of one row.
5. **The opt-in run.** With `RH_PLAYTEST_CARRYOVER=1`, the rover room test (P:1825) measures against `min(ShipMassCapKg, balance in kg) - list mass` instead of `GetAwardMassKg() - list mass`. Otherwise the opt-in would defer rovers that credits admit. The default mode keeps P:1825 byte-identical. The opt-in report counts deferrals.
6. **Q-A, with E3 in view.** The 30,000 kg cap is justified against inbound awards only. Under Q2a the Q3 bill (3,950 kg in all) grows 1.25x a round and passes 30,000 kg at round 10. E3 decides whether exports share the ship's cap, and E2 does not block either choice. Q-A now says so.
7. **The site count.** "57 suite sites and two cheats" becomes "58 call sites at HEAD, one of them the cheat that takes any name". The builder re-counts. Nothing depends on the number.
8. **The version and E1.** The version is a forecast (v54 if D6d v50, D6e v51, D7 v52 and E1 v53 land in that order). E1 R2's `UnslottedLostG` stays the last field of E1's block, but it is no longer the last field in the file. EC1's offset subtracts both blocks.
9. **The build order is two commits.**
   - **E2a, the verbs only.** It has the `Verb`, `Target` and `Amount` columns, the dispatch, and CR0 and CR1. There is no save bump, no stack cap and no money. The battery must be line-identical apart from the new suite's log, and the playtest must be byte-identical.
   - **E2b, credits.** It brings the balance, prices, the cap, `MaxStack` with `ManifestLanded`, the bot pre-check, the save block and its version bump, CR2-CR10, and the deck. The moves are those in section 7.2.

**Q-A, restated** (it replaces the text above):
- **(a) (recommended):** 30,000 kg per ship, a config row stamped on each ship when its manifest opens. That is more than any award the data can make today (25,100 kg at most), so the cap only matters once you have saved credits. Every run today is unchanged. When Earth starts taking the quota cargo (E3), its bill passes 30,000 kg around round 10, and E3 decides whether that cargo shares the cap.
- (b) 15,000 kg. A ship carries about one award's worth, so saved credits must be spread over several ships. It also caps a very loyal colony's best award (up to 25,100 kg). No battery run reaches 15,000 kg today.

**Q-B stands as written.**

**The verdict after revision:** CLEARED WITH CONDITIONS. R1-R5 are written in. The conditions left are:
- the director's answers to Q-A and Q-B;
- E1 built first, and its own Q-A and Q-B answered, since E2 re-homes E1's landing hooks and appends after its block.
