# E3, selling to Earth: design (2026-10-23)

Step 3 (economy), gate E3. **Design only; nothing here is built.** The code was read at HEAD `5a7066f` (save v49) with `git show HEAD:`. D6d (trams, v50) is being built in this tree now. D6e, D7, E1 and E2 are designed and answered but not built. Line numbers are at HEAD. They will move, so every anchor is also named by its function.

Short names:
- "W" is `Source/RedHopeSim/Private/RHSimWorldSubsystem.cpp`, "H" its header.
- "G" is `RHSimGrowth.cpp`. "C" is `RHSimCommandlet.cpp`.
- "P" is `Source/RedHope/Private/RHPlaytest.cpp`.
- "E1" is `docs/qa/2026-10-22/e1-ledger-design.md`, "E2" is `docs/qa/2026-10-23/e2-earth-credit-design.md`. Both are read with their REVISION and DIRECTOR ANSWER sections, which win over their bodies.

**Binding rulings:**
- **Q2a:** Earth takes the quota. The cargo leaves on the supply ship and is paid for. Holding a stockpile no longer meets a round, and the City / Earth share becomes a real cost. This moves balance on purpose.
- **Q1a:** Earth pays in credits, and each ship keeps a mass cap (E2: 30,000 kg, stamped when the manifest opens, separate from payment).
- **Q4a and ruling 11:** no money inside the city; Hope is never spent and never valued.
- **E1 Q-A (a):** one Earth value per resource (`EarthValue_mcr`), used both ways; E3 adds the launch cost on top.
- **E2 Q-A (a):** each ship carries up to 30,000 kg. E2 left E3 to decide whether quota cargo shares that cap.

**Built on E1 and E2.** E3 reads E1's `EarthValue_mcr` column and E1's ledger slots, writes E1's **Out** leg, and pays through E2's `CreditPay`. Build order: E1, E2a, E2b, then E3.

## What E3 builds, in one paragraph

The quota test stays what it is: a round is **met** on the first step the colony holds its bill, Earth pays the award at that step (E2), and the manifest opens. What changes is **launch**. `LaunchShip` now loads the round's bill onto the ship and takes it out of the colony, drawn in building-Id order from uncommitted stores and then the pool, never below a life-support floor. If the colony cannot load the whole bill at launch, the launch is refused with a reason and the ship waits. Beside the bill, the player may add **export lines**: surplus of a Mars-made resource, sold at its Earth value times Earth's political multiplier, less a launch cost per kg. Sales are paid in credits at launch. The bill and the sales share the ship's 30,000 kg cap; the ladder stops compounding once a round's bill fills one ship (Q-A). Growth leaves the owed bill alone while the manifest is open. Nothing is taken from a colony that meets a quota and never launches, so the only battery suites that move are the five that launch ships.

---

## 1. Taking the quota

### 1.1 What "met" means now

| | HEAD | E3 |
|---|---|---|
| **Met** (the `StepQuota` test, W:2103-2112) | the colony holds every line of the bill (`GetQuotaProgress`, W:12301: every building's stores, construction sites included, plus the pool) | **unchanged**, line for line |
| **What met earns** | the award in kg (E2: in credits, paid at that step); the manifest opens | unchanged |
| **Launch** (`LaunchShip`, W:13178) | sets the transit; nothing leaves | **loads the bill**: it leaves the colony; refused if the colony cannot load it |
| **The next round** | opens when the ship lands; the same stockpile that met this round counts again | opens when the ship lands; the stockpile that met this round is **gone** |

So "met" now means **Earth authorises the ship**, and "launched" means **Earth has collected**. A stockpile can meet a round once. It cannot meet the next one, because it left with the ship. That is Q2a.

**Why the take is at launch, not at the met step:**
1. **Blast radius.** 34 battery suites meet a quota (64 `CEO TRANSMISSION` lines). Most meet Q1 by accident, while stocking a fixture, and never launch. A take at the met step would move Struct, Water and Oxygen in all 34, and with them every later line that reads those stocks. A take at launch moves only the five suites that launch (section 4).
2. **The plan's own word.** The economy plan and E2 R2 both put the money at launch; E2 R2 kept the cap separate from payment "so E3 can pay at launch".
3. **One event in both bands.** A launch is a direct call between steps (the deck, the cheat, the bot, a suite), so it is the same single event in the agent and era bands (section 6).
4. **The award stays at the met step.** The manifest is composed after the met step and before launch, so it must be paid for by then. The award therefore stays where E2 put it, and only the cargo and the sales move to launch.

**What the player sees.** The CEO line at the met step is unchanged (E2 kept it verbatim so that 64 lines do not move; section 9, risk 5). The deck's Earth panel says what is owed at launch, and the launch button is greyed with the reason when the colony cannot load it.

### 1.2 What can be loaded: uncommitted holdings above a floor

For each resource, at launch:

```
Loadable(R) = GetGrowthHoldingsKg(R) - LaunchFloorKg(R)
```

- **`GetGrowthHoldingsKg`** (G:163-182) is C4's "uncommitted holdings": the pool, plus the stores of **completed** buildings on floors linked to the surface. A construction site's hopper never counts, and neither does a floor no lift links. That is right for cargo: material in a site is spoken for, and material on an unlinked floor cannot reach the pad.
- **`LaunchFloorKg(R)`**: Earth never takes what the colony breathes, drinks or eats. With N = `Colonists.Num()` and S = `GrowthReserveO2Sols` (10, the C5b row, reused so that growth and Earth respect the same breathing floor):
  - Oxygen: S x N x `ColonistO2KgPerSol` (0.75). **The same number as growth's oxygen reserve** (G:225).
  - Water: S x N x `ColonistWaterKgPerSol` (2.0).
  - Food: S x N x `ColonistFoodKgPerSol` (0.62). No bill asks for Food today; the rule covers sales.
  - Every other resource: 0. **Earth does not honour `GrowthReserveStructKg`.** That reserve is the city's working float, a growth rule. A launch can take Struct below it, and then growth waits for production above it, as it does after any big build.

**The met test does not use the floor or the uncommitted rule** (it stays HEAD's, 1.1). So a colony can be met and still not loadable: its Struct may sit in a site's hopper, or its oxygen may be only just above the bill with people breathing. The launch then refuses, with the reason, and the player (or the bot, section 5.2) launches once production catches up. The deadline is not affected: on time or late was decided at the met step.

### 1.3 The draw, in a deterministic order

The take reuses **`TakeGrowthResource(R, Kg)`** (G:251-292), the growth bill's draw, unchanged:
- Solids: `Buildings` in array order, which **is ascending Id** (the invariant at W:10837, `RemoveAt` "order-preserving: Buildings stays Id-ascending"; appends at W:10420 and the load at W:13879 keep it). Construction sites and unlinked floors are skipped. In each building, `OutputKg` first, then `InputKg`. Then the pool.
- A part taken from a floor building charges its shaft's cargo ledger (`ChargeBillPart`, D4 O5), as a growth bill does. The cargo rides a lift to the surface.
- Fluids and gases come from the pool.

**Why not `SpendTradeLot`** (W:9515-9555), which the plan named:
- it draws from construction-site hoppers, which would stall a build and break C4's rule;
- it draws from floors with no lift;
- it walks the lot's `TMap`, while E3 walks the bill in a fixed order.

Its Id order (an explicit sort) and E3's (the array's invariant) are the same order.

**The order across resources.** First the bill's lines in `ParseResourceList` order of the row's `Requirements` string (Struct, Oxygen, Hydrogen for Q3), which is the order `GetQuotaProgress` already uses. Then the export lines in slot order (E1's slots, sorted by `ToString()` with a case-sensitive ordinal compare). A resource that is both billed and sold is drawn **once**, as one amount, at its bill line's place. Draws of different resources touch different keys, so the order only matters for the broadcast and log order. It is still fixed.

**Amounts.** Bills are whole kg (Q rows are whole; `ScaleQuotaBill` rounds renewals to whole kg; the plateau in 2.4 floors to whole kg). Export lines are whole kg (2.2). So every draw is a whole number of kg, and E1's gram rounding is an identity on it (E1 section 4, point 3).

**All or nothing.** `LaunchShip` first checks every line, with HEAD's trade tolerance (`Loadable + KINDA_SMALL_NUMBER >= Need`, as `HasTradeLot` at W:9506). Only if every line passes does it draw anything. A refused launch changes no state at all.

**The pool's last 0.1 g.** `TakeGrowthResource` books a solid's shortfall on the pool (G:287-290). Because the check passes within 1e-4 kg, the pool can end at most 1e-4 kg below 0. `SpendTradeLot` already has the same edge (W:9549), and E1's EC4 tolerance covers it.

### 1.4 `LaunchShip`, the new order

HEAD's `LaunchShip` (W:13178-13192) has one check. After E2 and E3 it runs:
1. **Phase:** `AwaitingManifest`, else HEAD's "No manifest is open".
2. **Credits (E2 R-obs 4):** `GetManifestCostMcr() <= CreditBalanceMcr`, else E2's "Over budget at launch".
3. **Cargo (new):** for each line (1.3 order), `Need = bill line + export line`. If any line is short: refuse, with
   `Earth's cargo is short: %s needs %.0f kg (quota %.0f + sale %.0f); %.0f kg can be loaded (%.0f held outside sites and cut-off floors, %.0f kept for life support)`.
   The first short line is named. If the met test saw more than the launch can load, because the difference sits in sites or on unlinked floors, the message adds `; %.0f kg more is in construction sites or on floors with no lift`.
4. **On success, in this order:**
   1. draw each line (1.3);
   2. record each drawn amount on E1's **Out** leg for that resource (`NoteFlow(R, Out, Kg)`); the export is a flow out of holdings, as a convoy's lot is;
   3. E2's debit of the manifest's cost;
   4. **then** pay the sales (2.3) through `CreditPay`. Sales fund the **next** ship, never this one (the credit check in step 2 ran before them);
   5. clear the export lines;
   6. HEAD's transit, alert stage and phase change;
   7. the launch log line.
5. **The launch log line** (already changed by E2) gains Earth's cargo:
   `Ship launched: %d items, %.0f kg of a %.0f kg cap, %s cr spent, %s cr carried; to Earth %.0f kg (quota %.0f, sold %.0f) for %s cr; arrival in %.0f sols`.
   This is the only log line E3 changes. It moves exactly where E2's change moves it (section 4).

A new const query, **`CanLaunchShip(FString& OutWhy) const`**, runs checks 1-3 without acting. `LaunchShip` calls it, and the deck's LAUNCH button and the bot read it. There is one rule.

A refusal logs nothing in the sim: the reason goes back through `OutError`, as every refusal does. No Error line is ever logged on purpose (`rh_battery.sh` turns a suite red on `LogRedHopeSim: Error`).

### 1.5 Does the quota share the 30,000 kg cap? Recommendation: **yes** (Q-A)

**The rule:** outbound mass = the bill's total + the export lines' total, and it must fit the ship's cap (E2's `ShipMassCapKg`, stamped when the manifest opens).
- The quota is loaded first, and **always fits**, by 2.4.
- Export lines take only what the bill leaves: `AddExportLine` refuses past `cap - bill - lines already listed`.
- So `LaunchShip` never refuses on the cap. Step 3 above has no cap clause.

**The ladder plateaus at one ship.** Today the bills grow 1.25x a round without end. Q3's total is 3,950 kg at round 0 and **29,430 kg at round 9; round 10 would be 36,787 kg**. Under Q-A (a), from the first round whose scaled total passes the cap, every line becomes:

```
plateau line = floor(raw line x ShipMassCapCfgKg / raw total)    // int64; raw lines are whole kg
```

Q3's plateau is **Struct 18,987, Oxygen 9,113, Hydrogen 1,898: 29,998 kg**, the same every round after that. It is computed from the raw row, not from the scaled bills, so it does not wobble by a kilogram from round to round. Only `ScaleQuotaBill`'s two callers change (`GetQuotaProgress` W:12315 and `GetNextQuotaBills` W:2319). They call one new helper, `GetRoundBills(Row, Round, Out)`. Rounds 0 to 9 are bit-identical to HEAD.

**What this does to the game:** Earth's demand stops compounding at one full hull. Without the plateau, the 1.25x compounding eventually outruns any colony, because the bill is now taken every round instead of held. From round 10 a Q3 ship carries nothing to sell, so a colony that wants an export run must keep its rounds below round 10's size, or argue its way to a smaller bill (a lever for E6).

**The config, not the stamped cap.** The plateau reads the config value (`ShipMassCapCfgKg`), because the bill must be known while the round is Open, before any cap is stamped. The two can differ only if the config changes mid-round. Then the quota still flies whole (rule above), and the export room is `max(0, cap - bill)`.

**Display.** `GetQuotaBillScale` (W:2324) keeps its meaning (the ladder's multiplier). The objective header (`Q3 - round 11, bills up ...`) reads `bills capped at one ship` once the plateau applies, through a new `IsQuotaBillCapped()`. The renewal alert's bills (`DescribeQuotaBills`, W:2333) already read `GetQuotaProgress`, so they show the plateau.

### 1.6 Era refusal and the deadline

`CanEnterEraMode` (W:690-735) refuses only while the ship is **inbound**. A colony waiting in `AwaitingManifest` because its launch was refused may run in era, and production fills the shortfall there. No new refusal. The deadline was settled at the met step, so a launch delayed by the floor never turns an on-time award late. It delays only the next round (which opens at landing), and that delay is its own cost.

---

## 2. Selling surplus to Earth

### 2.1 What Earth buys (Q-B)

Earth buys only **Mars-made** resources. A new bool column, **`EarthBuys`**, in `RH_Resources.csv` and `FRHResourceRow`. Placeholder, director review:

| TRUE | FALSE, and why |
|---|---|
| Regolith, Ore, Struct, Shielding, Ice, Water, Oxygen, Hydrogen, SpoilChemicals | SpareParts, Seeds, Luxury, Soil, Glass: **Earth-made**; Earth sells them in packages and will not buy them back |
| | Food: Mars-made but also landed by every Crew Pod and Transport (see below) |
| | HabSegment: its "kg" is one unit (E1 section 3), so a per-kg launch cost is meaningless |
| | Compost, Fertilizer: worth 0, or dormant |
| | Power, Research, Hope: no ledger slot. **Hope is never valued (ruling 11).** |

**Why the list is needed: the manifest prices would otherwise be a money pump.** E2 prices every manifest item at 1 credit per kg of the package, while E1 values the contents. Selling the contents back at E1's values:

| Buy (E2) | Contents (E1) | Sell back at value - launch | Profit a crate |
|---|---|---|---|
| Seed Vault, 200 cr | 200 kg Seeds x 400 cr | 79,400 cr | **79,200 cr** |
| Luxury Goods, 300 cr | 300 kg Luxury x 150 cr | 44,100 cr | 43,800 cr |
| Glass Crate, 600 cr | 400 kg Glass x 15 cr | 4,800 cr | 4,200 cr |
| Parts Crate, 500 cr | 10 kg SpareParts x 200 cr | 1,970 cr | 1,470 cr |
| Soil Pallet, 1,000 cr | 1,000 kg Soil x 5 cr | 2,000 cr | 1,000 cr |
| Colonist Transport, 8,000 cr | 1,000 kg Food x 20 cr | 17,000 cr | 9,000 cr |

Every ship could buy credits from Earth. The `EarthBuys` list closes all six. Food is FALSE for the last row. A later gate can open Food once crew ships price their provisions (E2's `Price_mcr` column is where that would go).

### 2.2 Export lines

**New state** (H, beside E2's credit members):
- `TArray<FRHExportLine> ExportLines`, at most one per resource, kept sorted by resource name (E1's ordinal order).
- `FRHExportLine`: `FName Resource; int32 Kg; int64 PriceMcrPerKg`.
- `int64 SalesPaidMcr`: the lifetime total of sales paid in. It is a part of E2's `CreditAwardedMcr`, which counts every credit paid in. **Invariant:** `0 <= SalesPaidMcr <= CreditAwardedMcr`.

**Calls** (all direct calls between steps, like `AddManifestItem`):
- **`AddExportLine(FName Resource, int32 Kg, FString& OutError)`.** Its checks, in order:
  1. phase is `AwaitingManifest` (HEAD's message);
  2. the resource has an E1 slot and `EarthBuys` (`Earth does not buy %s`);
  3. Kg >= 1;
  4. the price is positive (2.3), else `Earth pays %s cr/kg for %s, less than the %s cr/kg the launch burns`, or `Earth will not buy from the colony now` when the multiplier is 0;
  5. surplus: `line kg + Kg + bill line <= Loadable(R)` (1.2), else `Only %.0f kg of %s is spare above the quota and the life-support floor`;
  6. the cap: `bill total + all lines + Kg <= ShipMassCapKg`, else `Over the ship's mass cap for cargo to Earth: ...`.

  On success it adds to the resource's line (or makes one) and **re-quotes the whole line** at the current price. The success log: `Export: +%d kg %s at %s cr/kg -> %d kg, %s cr`.
- **`RemoveExportLine(FName Resource, FString& OutError)`.** Phase check, then the line is dropped. Nothing was drawn yet, so nothing is returned.
- **`GetExportLines()`, `GetExportValueMcr()`** (the sum of kg x price): const reads for the deck.
- **`AdvanceQuotaLadder`** and the landing clear nothing: the lines are emptied at launch (1.4 step 4.5). A load in another phase repairs (7).

The surplus check at add time is advice: the colony can spend the material afterwards. **Launch re-checks everything** (1.4 step 3).

### 2.3 The price

All in integer milli-credits (mcr), with no float after one rounding:

```
Mpm   = llround(GetRequisitionMultiplier() x 1000)         // 0 .. 1400; exactly 1000 with no rival
L     = (LaunchH2GPerKg x V(Hydrogen) + LaunchO2GPerKg x V(Oxygen)) / 1000    // int64, mcr per kg shipped
Price = (V(R) x Mpm) / 1000 - L                              // int64, floor division of non-negatives
Pay   = sum over lines of Kg x Price                          // int64
```

- `V` is E1's `EarthValue_mcr`.
- **Two new `RH_Config` rows,** whole numbers so that L is exact:
  - `LaunchH2GPerKg` = 50 (grams of hydrogen burned per kg lifted);
  - `LaunchO2GPerKg` = 300 (grams of oxygen).

  So **L = (50 x 12,000 + 300 x 8,000) / 1,000 = 3,000 mcr = 3 credits a kg.** Placeholders, director review; the C++ fallbacks equal the CSV.
- **The launch cost is a price spread in credits, charged on sales only.** No hydrogen or oxygen leaves the colony's tanks for it. The quota's own lift is Earth's cost, part of the contract. If the director wants the colony to fuel the ascent from its own tanks, it is a later lever (risk 6).
- **The price is locked when the line is added or changed**, from state saved at that moment. Earth tension drifts every step (`StepEarth`, W:9916), so a launch-time price would differ from the quote the player saw.

**Prices at the reference values (credits a kg; "-" means refused):**

| Resource | V | neutral (x1.0) | Earth-loyal (axis -100, x1.4) | Martian (axis +100, x0.4) | tension 100, neutral axis (x0.7) |
|---|---|---|---|---|---|
| Regolith | 0.05 | - | - | - | - |
| Ore | 0.5 | - | - | - | - |
| SpoilChemicals | 2 | - | - | - | - |
| Ice | 3 | - (0) | 1.2 | - | - |
| Water | 4 | 1 | 2.6 | - | - |
| Struct | 5 | 2 | 4 | - | 0.5 |
| Oxygen | 8 | 5 | 8.2 | 0.2 | 2.6 |
| Shielding | 8 | 5 | 8.2 | 0.2 | 2.6 |
| Hydrogen | 12 | 9 | 13.8 | 1.8 | 5.4 |

**How tension and the identity axis move prices:** through the **same multiplier that already moves the award**, `GetRequisitionMultiplier` (W:9892-9907). It is 1.0 exactly while the colony has no active rival, so every pre-M3 colony and every suite without a rival sees neutral prices.
- Earth loyalty lifts prices by up to 40% (`RequisitionEarthBonus` 0.4).
- A Martian lean cuts them by up to 60% (`RequisitionMartianPenalty`).
- Earth tension cuts them by up to 30% (`RequisitionTensionPenalty`).
- The clamp is 0..1.4.

One political dial moves both what Earth pays for its quota and what it pays for your surplus. A Martian colony finds bulk unsaleable: only refined gases clear the launch cost. A loyal one can even sell ice. **E3 adds no new effect on tension or identity.** Selling is not a political act until E6 makes it one.

### 2.4 The quota as a price: reconciling E1 and E2 (E2 risk 4)

The award stays E2's: `floor(award_kg x 1000)` mcr, flat across rounds, while the bill compounds. At E1's values, net of the launch cost:

| Round | Bill (kg) | Goods worth, at value - launch | On-time award | Earth pays, of the goods' worth |
|---|---|---|---|---|
| Q1 | 550 | 1,050 cr | 10,000 cr | 952% |
| Q2 | 790 | 2,310 cr | 12,000 cr | 519% |
| Q3 r0 | 3,950 | 13,250 cr | 14,000 cr | 106% |
| Q3 r1 | 4,938 | 16,567 cr | 14,000 cr | 85% |
| Q3 r2 | 6,172 | 20,706 cr | 14,000 cr | 68% |
| Q3 r3 | 7,715 | 25,878 cr | 14,000 cr | 54% |
| Q3 r5 | 12,054 | 40,435 cr | 14,000 cr | 35% |

This is the intended reading of Q2a: **the quota starts as seed money and becomes a levy from Q3's second round.** From there a colony would earn more by selling the same goods than by delivering them, and Earth takes them anyway. "Pay Earth or build Mars" becomes a number the deck can show: the levy, the award minus the goods' worth. **E3 does not retune the award** (`AwardMcrPerKg` stays 1,000): changing it would move every manifest E2 proved unchanged.

---

## 3. The City / Earth share and growth protection under real removal

### 3.1 The rules at HEAD (C5b, C5c, C5d)

`GetGrowthAvailability` (G:210-228):

```
Free = Held - QuotaBill - Reserve
```

- **Held** is uncommitted holdings.
- **QuotaBill:**
  - while `Open`, the open round's whole bill;
  - otherwise (`AwaitingManifest`, `ShipInbound`, `Completed`), `(1 - CityShare)` x the **next** round's bill (`GetProtectedQuotaBillKg`, G:185-207).
- **Reserve:** 1,000 kg of Struct; 10 sols of breathing for Oxygen.

### 3.2 What breaks under real removal

During `AwaitingManifest`, HEAD protects only the next round's share, because the current bill was already "delivered" by being held. Under E3 the current bill is still **owed**, and leaves at launch. A growth project that starts during that window (a sol boundary while the player composes the manifest; one sol is 20 real minutes at 1x) would take it, and the launch would refuse.

### 3.3 The fix: a new `Owed` term, beside the share's term, not inside it

```
FRHGrowthAvailability gains: double Owed = 0.0;
Owed = (QuotaPhase == AwaitingManifest) ? the current round's bill line + this resource's export line : 0
Free = Held - QuotaBill - Owed - Reserve
```

- `QuotaBill`, `FullBill` and `bNextRound` keep their HEAD values in every phase. So every C5d share check, and every bot line that prints them, reads as before.
- Only `Free` falls, by `Owed`, and only while the manifest is open.
- `GrowthBillFits`'s refusal (G:230-247) adds `- %.0f owed to Earth at launch` to its text **only when Owed > 0**. Every refusal string HEAD prints in other phases is unchanged.

### 3.4 Do the quota-reserve rules still make sense? Yes. All three stay, and each now means more.

- **The open round's whole bill (C5b).** Unchanged. While the round is Open the colony is collecting it, so growth must leave it.
- **The next round's Earth share (C5c and C5d).** At HEAD the protected share was a parked stockpile: it stayed in the colony and met the next round by being held. Under E3 the colony starts each round from what is left after the take. So `(1 - share)` x the next bill is now **new production set aside for Earth**. The share's two ends:
  - share 0 still means "Earth's next round first";
  - share 1 still means "the city first; Earth waits".

  The share becomes the real cost Q2a asked for: whatever growth spends is what the next ship cannot take.
- **`GrowthReserveStructKg` (1,000 kg).** The city's working float. Earth does not honour it (1.2). After a launch, Struct can be below it, and growth waits. That is the right signal: Earth has just emptied the yard.
- **`GrowthReserveO2Sols` (10 sols of breathing).** It is now also Earth's floor (1.2). One row, one breathing floor for growth and for Earth.

**Nothing in C5e changes.** The settlers' net-production rule reads the C5d ledger, which the take does not touch (the take is not production). The Big Arrival's stock legs read the pool after the take, so a launch can hold a wave back. That is a real cost, and the floor bounds it to the 10-sol breathing reserve.

---

## 4. The battery: what moves, suite by suite

The take happens only inside `LaunchShip`, and the battery calls `LaunchShip` at **six sites in five suites** (C:1714, C:4874, C:6698, C:9245, C:9279, C:15000). Every other suite that meets a quota never launches, so **nothing is taken and nothing moves** in it.

The builder re-counts at build time:
- `git grep -n "LaunchShip" -- Source/RedHopeSim` for the launch sites;
- `grep -c "Ship launched"` over the battery logs for the launch lines;
- E1 R4's version grep for the version lines.

| Suite | What moves | Why |
|---|---|---|
| **`-ladder`** | 1 `Ship launched` line (the Earth clause). Every `LADDER` line holds. | Q1's ship takes Struct 300, Water 200, Oxygen 50: Struct from the Lander's starter 400 kg in `InputKg` (W:509), then the pool. Q2 is met from the 600 / 150 / 40 the suite adds afterwards. The fresh colony has no colonists, so the floor is 0. **The one exact edge:** Oxygen, 150 held against 150. If anything in a fresh colony draws oxygen in the 0.2-sol run, `LADDER Q2 met` turns false. The builder reports that, and the fixture gains a margin; the margin is not hidden. |
| **`-resupply`** | 5 `Ship launched` lines. Every R line holds (R0-R12). | `TopUp` (C:4859-4866) adds bill + 1 kg before each met, and `SetHold` sets absolute holdings, so the take empties what the suite then refills. R6 is the one line whose meaning shifts. The comment at C:4984 ("holdings stay at the round-1 bills, so round 2 cannot be met") is now "holdings are about 1 kg after the take". The check (Open, still unmet) holds, and the builder corrects the comment. No colonists, so the floor is 0. |
| **`-citygrowth`** | 5 `Ship launched` lines, plus the **`Free` checks taken while awaiting the manifest** (section 3.3), on purpose. | (i) b3 C:9225-9227 asserts `Free == Held - 600 - 1000` after Q1 is met: it becomes `Held - 600 - (300 or what Q1 bills) - 1000`. (ii) b4's `Boundary` calls in "awaiting the manifest" (C:9269, C:9273 and the round-2 call after C:9305) put held at reserve + bill + project +/- 1 kg: the "one kilo over starts it" half fails until `Owed` is added to the set-up. The fixture adds `Owed` and the check text names it. (iii) C:9323's knob-0 check (`Free == Held - 1000` in `AwaitingManifest`) gains `- Owed`. `QuotaBill` and `FullBill` checks (C:9174, :9279, :9296-9306, :9316, :9343-9344, :9402, :9422) are **unchanged**, because `Owed` sits outside them. (iv) **The floor:** `MeetQuota` tops up bill + 50 kg; with 8 colonists the oxygen floor is 60 kg. If the base save's pool has less than 10 kg of extra oxygen, the b4 launches are refused. The builder checks that first. If they are, `MeetQuota` adds `GetLaunchFloorKg` and the check says so. |
| **`-arrivals`** | 2 lines (E2's `Manifest: +` line is unchanged by E3; its `Ship launched` line gains the Earth clause). | The (a) ship tops up bill + 1 kg (C:6688-6692) with a crewed colony, so the Water floor (10 x N x 2 kg) and the Oxygen floor may refuse the launch. The builder checks the pool at C:6693. If it refuses, the top-up adds the floor (a fixture change, named in the check text). The 8,000 kg Colonist Transport is inbound cargo; outbound is 550 kg, far inside the cap. |
| **`-framing`** | 1 `Ship launched` line (e6). | One colonist and `Stock(4000)` (C:14993): the floor is 7.5 kg of oxygen, and every line loads. |
| **`-credit` (E2)** | none | E2's hooks open a manifest with no cargo held (`Debug_OpenManifestWithAward`, E2 R4). Under E3 those launches would refuse. E3 adds `Debug_SetEarthTakesQuota(bool)`, runtime-only, default true. When false, the bill is not loaded (export lines still are). E2's CR3-CR8 fixtures set it false. That is code in the suite; their log lines are unchanged. |
| **`-economy` (E1)** | none | EC1's offset subtracts E3's block too (the same note as E2 section 6). No E1 fixture launches. |
| **Every save-version line** | E1 R4's 15 lines, plus those D6d, D6e, D7, E1 and E2 add | the bump |
| **`-depth` DP11** | the two whole-file sizes | grow by E3's block |
| **every `(N bytes` line** | masked by `diff_battery.py` | the builder states the byte delta |
| **new** | `export.log` | the new suite |

**`-crisis` does not move.** The economy plan predicted it would, assuming the take happened at the met step. `-crisis` meets Q1 while it stocks its colony (one `CEO TRANSMISSION` line) and never launches, so under a take at launch nothing leaves. The same holds for the **29 other suites that meet a quota and never launch:** `alive`, `agri`, `deep`, `depth`, `districts`, `hopedrive`, `crew`, `growth`, `deepcommute`, `prefab`, `discovery`, `ending`, `food`, `tiers`, `greenhouse`, `rooms`, `surfacehab`, `transit`, `water`, `dayshift`, `demolish`, `furnish`, `luxury`, `solidarity`, `staff`, `traffic`, `percapita`, `walls`, and `crisis` itself. Their met lines and every line after them are unchanged.

**Nothing else may move:**
- every `CEO TRANSMISSION` line (64) and every `cargo unloaded` line;
- the pins `power: gen 50 W load 20 W` and `deposit Regolith_A: 40645 kg`;
- `-scale`'s digests (`-scale` delivers its transports through `Debug_DeliverCargo` and never launches).

**The plateau moves nothing:** no suite reaches round 10. `-resupply` reaches round 3 and `-citygrowth` round 3.

**The `Owed` term moves no suite but `-citygrowth`.** Growth runs during `AwaitingManifest` only where a suite meets a quota with growth on. The builder confirms this with `grep -n "AwaitingManifest" -A3` over the growth suites: `-citygrowth` b3/b4/b5 and `-districts` (which meets Q1 once and does not test `Free` after it).

---

## 5. Playtests

### 5.1 The bar and the comparison

Runs: `RH_PLAYTEST_DISTRICTS=1 bash scripts/sim/rh_playtest.sh 110 e3_<full|lean> guide <full|lean>`, plus the 75% full run (`share=0.75`). These are the C5 growth playtests.

**Compared against:**
- the director's current figures: **pop 117 / 115, 0 recalled**;
- the latest baseline transcript, `Saved/baselines/d8_full_playtest.txt` (HEAD, full, 50%): pop 115, recalled 0, the Big Arrival at sol 98.01, **8 ships**:

| | Q1 | Q2 | Q3 r0 | r1 | r2 | r3 | r4 | r5 |
|---|---|---|---|---|---|---|---|---|
| met (sol) | 5.61 | 10.53 | 31.71 | 41.59 | 49.68 | 62.68 | 77.51 | 96.34 |

The re-run reports one table, per run: pop at 110 / recalled / Big Arrival sol / each round's met and launched sol / on time or late / launch refusals (count, sols waited, the binding line) / Struct, Oxygen and Hydrogen sent to Earth / growth projects / beds / excavator halts / Hope range. Every difference is explained in the design-decisions entry.

### 5.2 What the bot must change to play at all (P only)

1. **Retry a refused launch.** `LoadCargo` (P:292-330) adds the cargo and launches in one call, and a refused launch is never retried: the cargo token is spent (P:1651, :1740, :1766). Under E3 a refusal is normal (1.2), so this would stall the bot forever. The fix:
   - `LoadCargo` sets `P.PendingLaunch` when `LaunchShip` refuses;
   - each tick, while `PendingLaunch` and the phase is `AwaitingManifest`, the bot calls `CanLaunchShip` and launches when it passes;
   - the refusal reason is logged once, and again only when it changes: `[Playtest] sol %.2f LAUNCH  waits: %s`;
   - `RoundLaunchSol` and `RoundCargo` are set at the real launch.
2. **Roads respect `Owed`.** The two road checks that read `Av.Held - Av.QuotaBill` (P:1108 and P:1136 for roads, P:1171 for the outpost program) subtract `Av.Owed`. Otherwise a road ordered while the manifest is open takes the cargo.
3. **The bot does not sell.** The playtest measures Q2a's cost, not a selling strategy. A seller is a follow-up (risk 7).

### 5.3 The forecast: the colony will probably stall, and why

This design cannot run anything, so the forecast is read from the HEAD transcript's `SOL` lines (Struct held, every 3 sols). **Struct is the binding resource** (the REPORT: "Struct held the program back on 24 sols"). The colony nets about 115-150 kg of Struct a sol from one Forge (1,025 kg at sol 16 to 2,741 at sol 31).

Under E3, Struct holdings are HEAD's minus everything taken so far:
- **Q2** needs 600 more after Q1 took 300. It is met around **sol 14-15** (HEAD 10.53), on time (deadline sol 30).
- **Q3 r0** needs 2,500 after 900 has gone. HEAD's holdings reach 3,400 near **sol 46-47** (HEAD met at 31.71). Q3's deadline is 24 sols from opening (about sol 18), so it is **late**: the award is 9,000, not 14,000.
- **But the fleet is short of parts.** HEAD's sol-34.7 ship brought 6 Parts Crates. Under E3 the first crates after ship 1's single crate arrive with r0's ship, about **sol 50**. D2a found the excavators halt at wear 100 by about sol 44 with no crates. **If the dig halts before r0 is met, the Forge starves and r0 is never met: a permanent stall**, at about 4-5 people, with no Big Arrival.
- Without the halt, r1 is met near sol 85-90 and r2 not by sol 110: **4 ships against 8, and 2 Q3 rounds against 6.** Growth is starved by the same Struct, so the beds the Big Arrival needs (40% of its free beds) are probably not built by sol 110. **Expected pop at 110: about 10-20, not 115.**

That is a stall, and it needs a ruling (Q-C). The forecast is written so the builder can confirm or refute it with the first run, before any tuning.

### 5.4 The tuning menu (the director rules: Q-C)

- **T1, the bot plays the new economy (bot only; no sim or data change).**
  - a Parts Crate on every ship from Q2 on (the Q2 cargo at P:1740 has none today);
  - a second Forge when Struct has held the program back for 3 sols, with its own regolith dig (player orders only).

  Expected: Struct about doubles; r0 near sol 35, r1 near 55, r2 near 75, r3 near 100.
- **T2, flatten the repeat (data).** `QuotaRepeatBillMul` 1.25 -> 1.10, so r1-r5 ask 2,750 / 3,025 / 3,328 / 3,660 / 4,026 kg of Struct. Under holding, 1.25 asked only the 25% increment each round. Under taking it asks the whole bill, so the old number is about five times harsher than it was tuned to be.
- **T3, a smaller Q3 (data).** Q3's base `Struct:2500` -> `Struct:1500`.

**The bar** (the colony neither starves nor stalls): in the full, lean and 75% runs at sol 110:
- 0 recalled;
- no excavator stands at wear 100 for more than 5 sols;
- at least 3 Q3 rounds met;
- the Big Arrival has landed.

---

## 6. Determinism, era parity and the two bands

- **The met step is HEAD's.** `StepQuota` runs at the same index in both bands, after `StepEarth` and before `StepPower` (agent W:649, era W:821). E3 changes nothing in it.
- **A launch is one event in both bands.**
  - `LaunchShip` is a direct call between steps: the deck (K:641), the cheat (`RHCheats.cpp:3027`), the bot (P:314) and the suites.
  - It reads only saved state: holdings, `Colonists.Num()`, the bill (a pure function of the saved row, round and config), the export lines (saved) and E2's balance.
  - It writes only that state.
  - Its order is fixed (1.3). It contains no float accumulation: every draw is a whole kg taken whole from whole stores.

  The same state launches identically in either band. Era allows `AwaitingManifest` (W:711 refuses only `ShipInbound`), so a refused launch can wait in era and launch there. A ship launched in era lands in era (E2 R-obs 1).
- **Prices** are integers after one `llround` of a pure function of saved doubles (`IdentityAxis`, `EarthTension`, the rival set), locked at add time. `EarthTension` drifts per step, so two bands that add a line at different moments quote different prices. That is the same per-band rule as E2's award (E2 CR8): each band is exact for its own state, and the cross-band difference is information, not a bar.
- **The `Owed` term** is a pure function of the phase, the bill and the export lines. It is linear in nothing and read only at growth's sol threshold, which both bands hit on the same step.
- **Order.** `ExportLines` is an array, kept sorted by name string with a case-sensitive ordinal compare, never by FName index (the covert seed bug of 2026-07). The draw walks the bill in `ParseResourceList` order, then the lines.
- **Reads never insert** (E1 R7). `Loadable`, the floor and `CanLaunchShip` use `GetStock`, `Find` and `FindRef`. `TakeGrowthResource` writes only keys that exist (`Find`), apart from the pool's `AddStock`, which HEAD's growth already does.

## 7. Save

**Version: the next free one.** The queue before E3 is D6d (v50), D6e (v51), D7 (v52), E1 (v53) and E2 (v54), so **E3 is v55 if all land in that order.** The number is a forecast, not a reservation.

**The export block** is written straight after E2's credit block, as the last thing in the file. Its **size is fixed by the table**, never by history (E2 risk 7's rule):
- `SalesPaidMcr` (int64);
- the slot count (int32);
- then, for **every** E1 slot in E1's sorted order: the name (FString), `Kg` (int32) and `PriceMcrPerKg` (int64). A resource with no line writes 0 and 0.

**Load** (after teardown, as E1's and E2's blocks are: it repairs with a **Warning**, never an Error):
- Names must be strictly ascending. An unknown name is skipped with a Warning; a missing one has no line. An unsorted block drops every line, with a Warning.
- A negative `Kg`, or `Kg > 0` with `PriceMcrPerKg <= 0`, drops that line, with a Warning.
- Lines saved in any phase but `AwaitingManifest` are dropped, with a Warning: only an open manifest holds lines.
- A line for a resource that is no longer `EarthBuys` is dropped, with a Warning.
- If `SalesPaidMcr < 0` or `SalesPaidMcr > CreditAwardedMcr`, it is clamped into `[0, CreditAwardedMcr]`, with a Warning. This runs after E2's own repair, so `CreditAwardedMcr` is already sound.
- The previous version is refused by the strict check.

**What is not saved:**
- The bill: a pure function of the row, the round and the config.
- `Owed`, `Loadable`, the floor, prices not on a line: all derived.
- `Debug_SetEarthTakesQuota`: runtime-only.
- Nothing about the cargo after launch: it is gone. E1's Out leg holds the lifetime amounts.

**E1's EC1 offset** subtracts E3's block as well. All three sizes are fixed by tables. The builder updates EC1 in the same commit.

**Checkpoints.** The bump refuses the Midgame and Late checkpoints. Re-cut them (`rh_checkpoints.sh all --force`) and load both.

## 8. Tests: `-export`, a new fails-loudly suite

It goes in its own file, `Source/RedHopeSim/Private/RHSimExportSuite.cpp`, and is added to `rh_battery.sh` (one more suite, one more run). No path logs an Error on purpose.

**New debug hooks** (runtime-only, never saved):
- `Debug_SetEarthTakesQuota(bool)`;
- `GetLaunchFloorKg(FName)`, a const read;
- `Debug_SetRequisitionInputs(double Axis, double Tension)`, which sets the two saved doubles through the existing `Debug_ShiftIdentity` path plus a tension setter.

| Check | What it pins |
|---|---|
| XE0 | **Data.** `EarthBuys` is TRUE for exactly the nine rows in 2.1 and FALSE for the rest; the CSV equals the live DT. `LaunchH2GPerKg` 50 and `LaunchO2GPerKg` 300 give L = 3,000 mcr. `ShipMassCapKg` 30,000. Q3's raw total 3,950. The plateau lines are 18,987 / 9,113 / 1,898. |
| XE1 | **Met is unchanged.** Twins with `Debug_SetEarthTakesQuota` on and off, 5 sols to Q1's met step and 0.5 sol after it, with no launch: holdings, the phase, the met sol, the award, the balance and the growth availability (except `Owed`, which is identical because nothing is billed differently) are bit-identical. |
| XE2 | **The draw order.** The Lander's starter Struct (400 kg in its `InputKg`, W:509) is moved to a debug store first, so the fixture controls every store. Q1 met with Struct in building #3's output (100), #3's input (50), #7's output (200), a construction site (#5, 400) and an unlinked floor store (300), plus the pool (100). Launch takes 100 + 50 + 150 from #3 and #7 in that order, nothing from #5 or the floor, and nothing from the pool. Each store's kg is exact. E1's Struct Out leg += 300 000 g. Holdings fall by exactly the bill. |
| XE3 | **The floor.** With 6 colonists: oxygen held = bill + floor - 1 kg is refused, with the reason naming Oxygen and `45 kg kept for life support`; the phase is still `AwaitingManifest` and every holding is bit-identical. +2 kg launches. The same for Water. With 0 colonists the floor is 0. |
| XE4 | **Met but not loadable.** Struct held only in a site: met, launch refused with the "construction sites" clause, nothing drawn. The site completes; Struct reaches the Forge's output; the launch goes. **On time stays on time** across a 10-sol wait. |
| XE5 | **`Owed`.** In `AwaitingManifest`, `Owed` = the bill line (+ the export line). A growth start 1 kg under `Reserve + QuotaBill + Owed + project` is refused with "owed to Earth at launch", 1 kg over starts. `Owed` is 0 in `Open`, `ShipInbound` and `Completed`. `QuotaBill` and `FullBill` equal HEAD's in all four phases, at shares 0, 0.5 and 1. |
| XE6 | **Prices.** Every slot's price at multipliers 1.0 (no rival), 1.4, 0.4 and 0.7 (through the real `GetRequisitionMultiplier`), against the table in 2.3 as exact integers. Ice at 1.0 is refused (0); Regolith, Ore and SpoilChemicals are always refused; Seeds and Food are refused as not `EarthBuys`. A multiplier of 0 refuses everything with "will not buy". |
| XE7 | **A sale.** Struct 1,000 kg added, then +500 (re-quoted at a new tension), then removed and re-added. Launch: draws bill + 1,000, pays 1,000 x 2,000 mcr, `SalesPaidMcr` += 2,000,000. `Balance == Awarded - Spent` holds. The manifest's credit check used the balance **before** the sale: a manifest costing more than the balance but less than the balance plus the sale is refused. |
| XE8 | **The cap and the plateau.** Export lines are refused past `cap - bill`. Rounds 0-9 bills are bit-identical to `ScaleQuotaBill`'s. Round 10 and 11 are the plateau. `GetNextQuotaBills` before the landing equals `GetQuotaProgress` after it at 9->10 and 10->11 (C5c's identity). `IsQuotaBillCapped` is false through round 9 and true from round 10. |
| XE9 | **Era parity.** One save with a met Q3 and 2 export lines: launch it after one agent step and, from the same save, after one era step at the same clock. Draws, Out legs, payment and every holding are identical. Then a refused launch waits in era for 2 sols and launches there; the ship lands in era. |
| XE10 | **Save.** Mid-manifest with 2 lines: save, load, save is byte-identical, and the names read back ascending. Doctored files, each repaired with a Warning and the colony loaded: a negative kg; a priced-0 line; lines saved while `ShipInbound`; an unsorted block; `SalesPaidMcr` > Awarded. The previous version is refused. |
| XE11 | **Ruling 11 and Q4a.** Hope, Research and Power can never be listed. A 10-sol twin with `SalesPaidMcr` raised by a debug grant through `CreditPay`, against none: Hope, the population and every stock are bit-identical. |
| XE12 | **Mutations, each must fail at least one check** (the builder runs them once and records which): draw from a site (XE2); pool before buildings (XE2); no floor (XE3); price in float (XE6's integer equality at 0.7); sales paid before the credit check (XE7); the plateau from the scaled bills instead of the raw row (XE8's round-11 equality); `Owed` inside `QuotaBill` (XE5's HEAD equality). |

**`-scale`** (outside the battery): 1,000 `CanLaunchShip` calls at N = 5,000 with 200 buildings, budget 5 ms. Every digest identical.

**Playtests:** section 5. **The click test:** the deck's Earth cargo panel and the LAUNCH tooltip get one capture each.

## 9. Presentation (RedHope; rebase onto whatever D6d to E2 leave there)

- The manifest picker gains **"Cargo to Earth"**: the bill lines (owed at launch), each export line with its kg, its locked price and a remove button, a resource chooser limited to `EarthBuys` rows with the live quote, and the outbound mass against the cap.
- LAUNCH reads `CanLaunchShip`; when it is greyed, the reason is its tooltip.
- The objective panel's bars stay the met test. Under them, once met: "Earth collects at launch".
- `RH.Export add <res> <kg> | remove <res> | list`. `RH.Status` prints owed cargo, lines and `SalesPaidMcr`.
- New strings go into the wording table as placeholders. None is morale, sickness or evacuation wording, so the Gate-D review is not engaged.

## 10. Risks

1. **The playtest colony probably stalls (5.3).** The forecast says a Struct-starved fleet can halt before Q3 r0 and never recover. Q-C decides the response. Without a ruling the builder reports the numbers and stops.
2. **Met and loadable can disagree** (site hoppers, unlinked floors, the floor). This is deliberate: met keeps its 34 suites still. The launch reason names the gap, and the bot waits for it. A later gate could align met with the uncommitted rule, at the cost of moving those suites.
3. **The levy (2.4).** From Q3 r1 Earth pays less than the goods' worth, and by r5 a third of it. That is Q2a's intent, but it is a feel change the director should see in hand-play, not only in tables.
4. **E1 and E2 are not built.** E3 writes E1's Out leg, reads its slots and values, pays through E2's `CreditPay` and appends after E2's block. Build after E2b, rebased on their code, not these anchors.
5. **The CEO line still says "kg manifest budget"** (E2 risk 3). Changing it moves 64 lines in 34 suites. E3 keeps it, and the deck carries the meaning. A wording pass can change it once, with the line count stated.
6. **The launch cost burns nothing real.** It is a credit spread. If the director later wants the ascent fuelled from the colony's own tanks, that moves the quota's oxygen and hydrogen balance a second time.
7. **No selling bot.** The default playtest never sells, so sales are proven by `-export` only. An opt-in `RH_PLAYTEST_SELL=1` is a follow-up.
8. **`ChargeBillPart` on cargo.** Struct taken from a floor store charges the shaft's cargo ledger, as a growth bill does. No launching suite has a lift car, so no line moves. The `-transit` suite's ledger checks never launch.

---

## Director questions

**Q-A. Does Earth's quota cargo share the ship's 30,000 kg?**
- **(a) (recommended):** Yes. The quota is loaded first, and your sales fill what is left. Once a round's bill would fill a whole ship (Q3's tenth round), Earth's demand stops growing: every later round asks for one full ship, about 19 t of structural, 9 t of oxygen and 2 t of hydrogen. From then on a Q3 ship has no room to sell.
- (b) No. The quota rides outside the cap, Earth's demand keeps growing 25% a round forever, and every ship always has 30 t for sales.

**Q-B. What will Earth buy?**
- **(a) (recommended):** Only what Mars makes: regolith, ore, structural, shielding, ice, water, oxygen, hydrogen and spoil chemicals. Not spare parts, seeds, luxury goods, soil, glass or food. Without this, buying a Seed Vault for 200 credits and selling the seeds back would earn 79,000.
- (b) Everything, at its value less the launch cost. Earth's manifest prices must then be raised to at least the value of their contents, which changes what every ship can carry.

**Q-C. If the 110-sol colony stalls once Earth takes the quota, what do we change?**
- **(a) (recommended):** First the scripted player adapts: it orders spare parts on every ship and builds a second Forge when structural holds it back. The game's numbers do not change. If it still fails the bar (nobody recalled, the fleet running, at least three Q3 rounds, the Big Arrival landed by sol 110), Earth's repeat multiplier drops from 1.25 to 1.10 a round, and both runs are reported.
- (b) Change the game's numbers straight away: the repeat multiplier to 1.10, and Q3's structural to 1,500 kg.
- (c) Change nothing. A smaller colony is the price of paying Earth.

---

# PROFESSOR REVIEW

An independent adversarial review against HEAD `5a7066f` (2026-10-23), read-only, with `git show HEAD:`. It is recorded as returned, lightly formatted.

Verdict: **CLEARED WITH CONDITIONS**

The economic core holds up. The take at launch, the integer price maths and the plateau are sound. Three things must change before the build:
- the design makes an unrecoverable state reachable and treats it as bot tuning;
- its `-citygrowth` move table misses checks that are certain to turn the battery red;
- XE9 cannot pass as written.

## What holds (checked)

- **Six launch sites, five suites.** `git grep` at HEAD confirms C:1714, :4874, :6698, :9245, :9279 and :15000, plus P:314, `RHCheats.cpp:3027` and the deck. No suite hook skips a phase; the only one is `Debug_SetQuotaRepeatBillMul` (H:2350). The post-D8 logs agree: 34 files carry `CEO TRANSMISSION`, and exactly 5 carry `Ship launched` (framing 1, citygrowth 5, ladder 1, resupply 5, arrivals 1).
- **The cargo is taken exactly once.**
  - `LaunchShip` (W:13178) runs only in `AwaitingManifest` and leaves it in the same call.
  - `Debug_DeliverCargo` (H:1320) only applies effects.
  - The knob-0 rest state (W:2238-2241, :2179) never launches.
  - Research funding (W:2124-2129) only adds to the award.
  - A load restores holdings and the phase together.
- **The draw order is deterministic.**
  - `TakeGrowthResource` (G:251-292) walks `Buildings`, which stays Id-ascending: appends at W:10420, the order-preserving `RemoveAt` at W:10837, the load in saved order at W:13879.
  - It skips sites and floors no lift links, exactly as `GetGrowthHoldingsKg` (G:163-182) counts them.
  - `ChargeBillPart` stamps `GetStepEndSeconds()` (W:2663). Between steps that is the clock time, so it is deterministic in both bands.
  - Growth's own bill already takes this path (G:647).
- **The price maths.** Every cell of the 2.3 table recomputes exactly, with L = 3,000 mcr/kg. So do the 2.1 pump table and the 2.4 levy table.
- **The plateau.** UE's `RoundToDouble` rounds halves up (Hydrogen 313 at round 1). Round 9 is 29,430 kg, round 10 would be 36,787 kg, and the plateau is 18,987 / 9,113 / 1,898 = 29,998 kg, each line at or above round 9's. Routing both callers (W:2315 and W:2319) through one `GetRoundBills` keeps C5c's identity.
- **The suites that launch.**
  - `-ladder`: Q1 takes 300 of the Lander's 400 kg starter Struct (W:509). Q2's Oxygen is exactly 150 against 150 with no colonists, which the design names.
  - `-resupply`: the five launches are R2, R3, R4, R6 and R9. `SetHold` and `TopUp` read `GetQuotaProgress`, so the R lines hold.
  - `-framing` e6: `Stock` (C:14704) sets Food, Water and Oxygen to 4,000 kg with one colonist.
  - `-crisis`: it meets Q1 and never launches.
- **The `Owed` term reaches only the suites the design says.** A scan of every battery log for city-growth lines after a `CEO TRANSMISSION` finds only `deep` K10, which is a fresh `deepbase` load with no met line, so its quota is still Open. "next quota round" appears only in `citygrowth.log`.
- **Payment order.** E2's credit check comes before the draw, and sales go through `CreditPay` after the debit. `Balance == Awarded - Spent` holds, and sales fund only the next ship. The largest product is 30,000 kg x 13,800 mcr: no overflow.

## REQUIRED CHANGES

**1. A permanent stall is reachable for any player, not just the bot.**
- **Defect.** At HEAD a met quota can always launch (W:13180-13190). Under E3 a met colony can be unable to load, with no way out in the game:
  - Spare Parts are import-only, no recipe makes them (`RH_Recipes.csv`), and robots halt at wear 100 (`RH_Config.csv:26`);
  - Struct needs hauled regolith;
  - met counts Struct in site hoppers (W:12284-12298), but the launch cannot load it;
  - so: met but not loadable, no ship, no Parts Crates, the haulers and excavators halt, the Forge starves, never loadable.
- **Evidence.** Section 5.3 forecasts exactly this ("a permanent stall"). T1-T3 tune the bot and the data; none is a way out inside the game.
- **Fix.** Add a sim way out and ask the director about it. The simplest that keeps the met test is **launch short**: after N sols waiting, the player may launch with what can be loaded, and the shortfall becomes arrears on the next round or a credit charge at Earth value. `CanLaunchShip` reports it, the bot falls back to it after a fixed wait, and a new XE test pins it. This question matters more than Q-B, whose option (b) is a strawman: make Q-B a stated rule and give its slot to this.

**2. The `-citygrowth` move table is wrong in ways that turn the battery red.**
- **(a) Water certainly refuses the b4 launches.** The base holds Water below 200 (C:9174's text: "its Water bill of 200 is not met") with 8 colonists, so the Water floor is 10 x 8 x 2 = 160 kg. `MeetQuota` tops Water up to the bill + 50 = 250, so at most 90 kg is loadable against a 200 kg bill. C:9245 (`LaunchAndLand` false, failing all four ladder rungs) and C:9279 both log Errors. Oxygen also refuses on any rung where the +50 top-up fires (floor 60). **Fix:** `MeetQuota` adds `GetLaunchFloorKg` unconditionally, and the check text says so.
- **(b) Three more checks become Errors:**
  - C:9317, the b5 round-2 `Free` in `AwaitingManifest` (Owed = Struct 3,906);
  - C:9345, the b5 dial's `Free` for Struct and Oxygen;
  - C:9371-9381, the instant dial: at share 0.5, Free = 1,941 - 300 - 300 (owed) - 1,000 = 341, less than the 640 kg project, so `bStarts` fails.

  The six `Pair` calls (C:9357-9362) are covered only if the fix goes inside the `Boundary` lambda (C:9254-9265). Say so.

**3. XE9 cannot pass as written.**
- **Defect.** It launches after one agent step and, from the same save, after one era step "at the same clock", then asserts every holding is identical. The bands integrate production differently in that step; that is why `-eraparity` and E1's EC7 use tolerances.
- **Fix.** Assert the launch's deltas (per-store draws, Out legs, payment) from each band's own pre-launch state, or launch both from the same loaded state with no step between.

## OBSERVATIONS (non-blocking)

1. **The bot's leaks are broader than roads.** HEAD's bot launches on the met step, so no waiting window exists today. Under E3 the 31 `OrderBuild` sites (P:680-2010) feed Struct into site hoppers while the ship waits. `Owed` does not cover them, and the sim's road and lift bills ignore the quota (`GetRoadStructAvailableKg`, `RHSimRoads.cpp:54`). Hold Struct-eating builds while `PendingLaunch`, or count them in the forecast.
2. **The forecast reads the wrong series.** The `SOL` lines' "struct" is `GetQuotaProgress`, hoppers included. The loadable figure is the `OUTPOST` line's "Struct held". At Q1's met step (sol 5.61) that was 235 against a 300 bill (`d8_full_playtest.txt:57-74`), so Q1's own launch would wait until about sol 8 and delay ship 1's kits and crate. The stall starts earlier than 5.3 says.
3. **Section 1.6 overstates era access.** `CanEnterEraMode` refuses during any construction (W:692-697): exactly the "Struct in a site" wait.
4. **No cap check at launch.** The bill depends on live config (`QuotaRepeatBillMul` and the cap). A change mid-manifest can push bill + export lines past the stamped cap. Check at launch, or repair at load.
5. **No loading deadline.** On time is fixed at the met step, so a player can meet on time with hopper Struct and launch whenever. State it for the director.
6. **XE6.** A multiplier of 0 is unreachable through the real function: its minimum is 1 - 0.6 - 0.3 = 0.1. And `Debug_SetRequisitionInputs` must activate a rival, or the multiplier stays 1.0 (W:9896). That affects XE7's re-quote too.
7. **XE1 contradicts itself** on whether `Debug_SetEarthTakesQuota(false)` zeroes `Owed`, and twins that differ only in a flag read at launch are weak without figures pinned from HEAD.
8. **E2's `Debug_ZeroCredits` must zero `SalesPaidMcr` too**, or `SalesPaid <= Awarded` breaks in `-credit`.
9. **References and counts.** "always fits, by 2.4" and "the plateau in 2.4" mean 1.5. The `-arrivals` row says 2 lines; `arrivals.log` has one `Ship launched` line. The ladder log (W:2266) would print "bills x9.313" at round 10 while the bills are on the plateau.
10. **A better blast-radius check for `Owed`.** The log scan above (city-growth lines after a met line), not `grep AwaitingManifest`, which cannot see suites that meet Q1 by accident.

VERDICT: CLEARED WITH CONDITIONS (required changes 1-3)

---

# REVISION

This section replaces the design wherever they differ. The builder's order of authority:
1. the director's answers;
2. this REVISION;
3. the PROFESSOR REVIEW;
4. the design.

Anchors are at HEAD `5a7066f`. Rebase onto D6d, D6e, D7, E1 and E2 when they land.

**R1. A way out: the short launch** (required change 1). The met test, the award and the normal launch are unchanged. New:
- **`QuotaShortLaunchSols`**, a new `RH_Config` row, 3 sols (placeholder, director review). The C++ fallback equals the CSV.
- **When it is allowed:** in `AwaitingManifest`, when `Clock->GetSol() >= QuotaMetSol + QuotaShortLaunchSols` and the normal launch is refused for cargo only. `QuotaMetSol` is the saved int32 (H:3681), so the threshold is the same integer test in both bands.
- **`LaunchShipShort(FString& OutError)`.** It runs checks 1 and 2 of section 1.4 (phase, credits). Then, per bill line in the 1.3 order, it loads `floor(min(Loadable, line))` whole kg, and the shortfall is `line - loaded`. **Export lines are dropped, not sold:** a ship that cannot carry Earth's quota carries no sales. The rest is section 1.4 step 4.
  - The launch line's Earth clause reads `to Earth %.0f kg (quota %.0f of %.0f, short %.0f owed next round)`.
- **The shortfall becomes arrears (Q-B (a)).**
  - `QuotaArrearsKg`: one int32 per E1 slot, saved (R8).
  - `GetRoundBills` adds the arrears to the bills of the **round that opens next**, after the plateau. A resource the row does not bill gets its own line: Q1's Water arrears become a Water line on Q2.
  - `GetNextQuotaBills` adds them too, so C5c's identity holds.
  - The arrears clear when a launch loads them. A short launch's arrears replace the old ones: they are the new total shortfall, never added twice.
  - With arrears the quota may exceed the cap. It still flies whole, and the export room is `max(0, cap - bill)` (R6).
- **`CanLaunchShip(FString& OutWhy, bool& bOutShortAllowed)`.** It also reports whether a short launch is open, and from which sol. The deck shows **LAUNCH SHORT** beside LAUNCH, with the arrears it would leave.
- **Why arrears and not a credit charge:** arrears keep the ships coming and keep the debt in the goods Earth asked for. A credit charge would need a debt below zero (E2 forbids a negative balance), or would quietly let a rich colony buy its way out of Q2a.
- **What it does not cover:** a quota never met at all. That is HEAD's existing stall (D2a: no ship comes until the bill is held). E3 makes it likelier, because each ship empties the stores. Q-C's bar still measures it.
- **The bot.** Its retry (section 5.2) calls `LaunchShipShort` as soon as a short launch is allowed, and logs `[Playtest] sol %.2f LAUNCH  short: %s`.
- **XE13 (new).** The window opens exactly at `QuotaMetSol + 3` (refused at +2.99, allowed at +3.00, in both bands). The loaded and owed kg are exact. Export lines are dropped and unpaid. The arrears reach the next round's bill, with a Water line on Q2 after a Q1 short. Arrears clear on a full launch. The C5c identity holds with arrears. Save, load, save is byte-identical with arrears held.

**R2. `-citygrowth`, corrected** (required change 2). This replaces that row of section 4.
- **The floor.** `MeetQuota` (C:9232-9241) tops each line up to `bill + GetLaunchFloorKg(R) + 50`, **unconditionally**, and its first `Expect` text says "bill + the launch floor + 50". Without it the Water floor (10 x 8 x 2 = 160 kg) refuses every b4 launch, and C:9245 and C:9279 log Errors.
- **The `Owed` fix goes inside the `Boundary` lambda** (C:9254-9265). It puts holdings at `Reserve + Bill + Owed + Project +/- 1`, with `Owed = GetGrowthAvailability(Res).Owed`, read in the lambda. That covers its calls at C:9269, :9273, :9281 and after :9305, and the six `Pair` calls (C:9357-9362).
- **The checks that move on purpose,** each rewritten with `- Owed` in its expectation and its text:
  - b3 C:9225-9227;
  - knob-0 C:9323-9324;
  - b5 C:9317;
  - b5 dial C:9345;
  - the instant dial C:9371-9381 (at share 0.5: 1,941 - 300 - 300 owed - 1,000 = 341 < 640, so its setup holdings gain `Owed`).
- **What does not move:** every `QuotaBill`, `FullBill` and `bNextRound` check (C:9174, :9279's bills, :9296-9306, :9316, :9343-9344, :9402, :9422).
- **The builder re-counts** with the log scan in R10, not by grepping the source.

**R3. XE9, rewritten** (required change 3).
- **(i) One state, two bands.** Load one save with Q3 met and two export lines. Launch at once, with no step: once in a world driven by `StepSim`, once in one driven by `EraStep`. Every per-store draw, Out leg, payment and the balance are identical. The launch has no band-dependent input.
- **(ii) From each band's own state.** Run 1 sol in each band, then launch. Assert in each band that its deltas equal its own bill and lines, drawn in the 1.3 order from its own pre-launch stores. The cross-band difference in holdings is printed as information, not barred.
- **(iii) Waiting in era.** A floor-short launch (no construction site) waits 2 sols in era, then launches there, and the ship lands in era.

**R4. The forecast, corrected** (observation 2). This replaces section 5.3's numbers.
- **The right series** is the `OUTPOST` lines' "Struct held" (uncommitted), not the `SOL` lines' "struct" (hoppers included).
- **Q1:** met at sol 5.60 with 235 kg loadable against 300 (`d8_full_playtest.txt:59`). Loadable reaches 300 between sol 8 (275) and sol 9 (350), so ship 1 launches about **sol 8.7**, not 5.61. That is three sols later than HEAD for its four Solar kits, its Battery, its Parts Crate and the Toolkit, and that is before any bot deferral (R5).
- **Q2:** opens at that landing (about sol 11.7). Q3 r0 opens about sol 18-20.
- **Every later forecast shifts about 3 sols earlier into the stall.** The parts gap after ship 1 now runs from about sol 12 to Q3 r0's ship.
- **With the short launch (R1)** the permanent stall becomes a slow one. A Q3 ship leaves at most 3 sols after its met step, carrying its crates, and the shortfall is owed. So the forecast's pop of about 10-20 and 2 Q3 rounds still stands, but "never recovers" is replaced by "recovers one ship at a time, carrying arrears".

**R5. The bot's waiting window** (observation 1). While `PendingLaunch` is set, the bot places **no order whose bill includes Struct** (roads, outpost pylons, buildings), and logs `[Playtest] sol %.2f HOLD    %s waits for the launch` once per order. A life-support order is the one exception: an Electrolyzer, Water Plant or Air Filter that `LsBuild` asks for is still placed. The window is at most `QuotaShortLaunchSols`, because the bot then launches short (R1). This replaces section 5.2 item 2, which is kept as one case of it.

**R6. A cap check at launch, for exports only** (observation 4). `CanLaunchShip` refuses when `bill + export lines > ShipMassCapKg` **and** the export lines are not empty, with `Over the ship's mass cap for cargo to Earth: remove %.0f kg of sales`. The quota alone never refuses: it flies whole (section 1.5, and R1's arrears). At load, lines that no longer fit are dropped from the end of the sorted list, with a Warning.

**R7. Smaller corrections.**
- **Era (observation 3).** Section 1.6 now reads: a wait caused by the **floor** or by an unlinked floor can run in era; a wait caused by Struct in a **construction site** cannot, because `CanEnterEraMode` refuses during any construction (W:692-697).
- **The loading deadline (observation 5).** On time is decided at the met step. A player who meets on time may launch whenever they like, and the short launch is optional for a player. This is stated in risk 2. The deadline does not move to launch, because that would move every met suite's award line.
- **XE6 (observation 6).**
  - The "will not buy" message and the 0 case are removed: the real minimum is x0.1, where every price is under the launch cost anyway.
  - `Debug_SetRequisitionInputs(Axis, Tension)` first activates Zarya through the existing rival activation, so `HasActiveRival()` (W:9896) is true.
  - XE6 adds the x0.1 column, where every slot is refused.
  - XE7's re-quote uses the same hook.
- **XE1 (observation 7).** `Debug_SetEarthTakesQuota(false)` means: the bill is not loaded at launch, **and `Owed` counts only the export lines**. XE1 runs with growth off, so `Owed` cannot act. It pins the met sol, the award and every holding at the met step against figures the builder records from the pre-E3 binary on the same fixture, written into the suite.
- **E2's hooks (observation 8).** `Debug_ZeroCredits` also zeroes `SalesPaidMcr`, and the invariant check after every CR and XE step includes `0 <= SalesPaidMcr <= CreditAwardedMcr`.
- **References and counts (observation 9).**
  - In 1.3 and 1.5, "by 2.4" and "in 2.4" mean section 1.5.
  - The `-arrivals` row moves **1** line, its `Ship launched` line.
  - The ladder log at W:2266 prints `bills capped at one ship` instead of `bills x%.4g` when `IsQuotaBillCapped()`. No suite reaches it.

**R8. Save, extended** (for R1). The export block gains, per E1 slot and after `PriceMcrPerKg`, `QuotaArrearsKg` (int32). The block's size is still fixed by the slot table. Load repairs a negative arrears value to 0, with a Warning. Arrears are kept in every phase: they are a debt, not a manifest line.

**R9. `EarthBuys` is a stated rule, no longer a question.** Section 2.1's list and its money-pump table stand as the design. The column is placeholder data under the usual "director review" note, and the director can strike or add a row at review. Q-B's slot goes to the short launch.

**R10. The `Owed` blast-radius check** (observation 10). The builder scans every battery log for city-growth lines (`growth:` starts, refusals naming "next quota round" or "owed to Earth") after a `CEO TRANSMISSION` line. At HEAD only `citygrowth.log` qualifies, plus `deep` K10, whose quota is still Open. Any other log that qualifies after the build is a finding, not a fixture edit.

**R11. Risk 1, restated.** With R1 the playtest colony cannot lock itself in at a met quota. It can still starve before one: HEAD's own stall, made likelier. Q-C's bar and tuning menu stand. The forecast (R4) still expects the bar to fail without a ruling.

**The director questions, restated** (they replace the ones above):

**Q-A. Does Earth's quota cargo share the ship's 30,000 kg?**
- **(a) (recommended):** Yes. The quota is loaded first, and your sales fill what is left. Once a round's bill would fill a whole ship (Q3's tenth round), Earth's demand stops growing: every later round asks for one full ship, about 19 t of structural, 9 t of oxygen and 2 t of hydrogen. From then on a Q3 ship has no room to sell.
- (b) No. The quota rides outside the cap. Earth's demand keeps growing 25% a round forever, and every ship always has 30 t for sales.

**Q-B. A ship cannot load the whole quota (it is in construction, or taking it would leave the colony short of air or water). What happens?**
- **(a) (recommended):** After 3 sols you may launch it short. It carries what can be loaded, no sales, and the rest is added to the next round's bill. The supply line never stops.
- (b) As (a), but the shortfall is charged in credits at Earth's value instead of added to the next bill. A colony with savings pays its way out.
- (c) No short launch. The ship waits until the whole bill is loaded, and a colony that cannot load it gets no more ships.

**Q-C. If the 110-sol colony still stalls once Earth takes the quota, what do we change?**
- **(a) (recommended):** First the scripted player adapts: it orders spare parts on every ship, builds a second Forge when structural holds it back, and pauses building while a ship waits to load. The game's numbers do not change. If it still fails the bar (nobody recalled, the fleet running, at least three Q3 rounds, the Big Arrival landed by sol 110), Earth's repeat multiplier drops from 1.25 to 1.10 a round, and both runs are reported.
- (b) Change the game's numbers straight away: the repeat multiplier to 1.10, and Q3's structural from 2,500 to 1,500 kg.
- (c) Change nothing. A smaller colony is the price of paying Earth.

**The verdict after revision:** CLEARED WITH CONDITIONS. R1-R11 are written in. The conditions left are:
- the director's answers to Q-A, Q-B and Q-C;
- E1 and E2 built first;
- the builder's first playtest run, which confirms or refutes the forecast (R4) before any tuning.
