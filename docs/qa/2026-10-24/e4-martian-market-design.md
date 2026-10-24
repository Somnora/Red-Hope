# E4, the Martian market: design (2026-10-24)

Step 3 (economy), gate E4. **Design only; nothing here is built.** The code was read at HEAD `ea9a5cd` (E3, save v53) with `git show HEAD:`. E3b (the City / Earth share applied to the open round) is being built in the same tree; it touches growth and adds no saved state, so it does not meet this design. Line numbers are at HEAD. They will move, so every anchor is also named by its function.

Short names used below:
- "W" is `Source/RedHopeSim/Private/RHSimWorldSubsystem.cpp` and "H" is its header.
- "X" is `RHSimExport.cpp` (E3), "E" is `RHSimEconomy.cpp` (E1 and E2b's blocks), "G" is `RHSimGrowth.cpp`, "C" is `RHSimCommandlet.cpp`.
- "K" is `Source/RedHope/Private/RHCommandDeckPanels.cpp`, "V" is `RHColonyVisualizerSubsystem.cpp`, "P" is `RHPlaytest.cpp`.

**Binding rulings:**
- **Q1a:** Earth pays in credits. Until E5 there is one currency, the Earth credit, held as `int64` milli-credits (mcr).
- **Q2a:** Earth takes the quota (E3).
- **Q3a:** a Martian currency comes later, as a political act (E5). E4 prices in Earth credit.
- **Q4a and ruling 11:** there is no money inside the city, and Hope is never spent or valued. Prices exist only between the colony and the rivals.
- **The economy plan's E4 risks** (`docs/qa/2026-10-20/economy-plan.md`): E4 is "the first quantity in the sim that feeds back on itself". The mitigations it names are all taken here: price from the windowed ledger (flows, not stock), a bounded step a sol (5%), integer maths with no float in the update, a paired-run price-parity test, and a fallback that reprices on rival-side numbers only.

**Built on E1, E2 and E3** (each read with its REVISION and DIRECTOR ANSWER):
- E1's slots (the 18 mass resources, sorted by name with a case-sensitive ordinal compare), its `EarthValue_mcr` column and its `NoteFlow` legs;
- E2's `CreditPay` (the only income writer) and its invariant `Balance == Awarded - Spent`;
- E3's uncommitted-holdings draw (`TakeGrowthResource`, building-Id order, never a site or an unlinked floor), its life-support floor (`GetLaunchFloorKg`), and its rule that sales fund the next ship, not this one.

## What E4 builds, in one paragraph

Each rival gets an **aggregate economy** from data: what it makes and uses each sol, and a store per resource with a cap. No buildings. The colony and its rivals share one **market** per region (one today). It holds an integer price per resource in mcr/kg, starting at E1's Earth value. Once a sol, at the sol threshold, the rivals produce and consume, and the market reprices every traded resource. The new price comes from the window of what was offered and taken over the last few sols, the rivals' store levels, and a pull back toward the Earth value. The step is clamped to 5% a sol, and the price is held inside a band around the Earth value. **Convoys carry orders**: lines to buy and sell. An order is booked and its convoy leaves at once, carrying the goods it sells. The **market clears once a sol**: at the threshold that closes the sol the order was booked in, the rival fills what it can, at the price quoted all that sol. The colony pays for what it bought from credits it reserved when it booked, and what it earns comes home with the convoy. Up to three convoys can be out at once, one per rival. Today's fixed lot becomes a rival's **standing order**: the same kilograms, now priced. Nothing outside the market reads a price, so the battery and the playtest do not move.

---

## 1. Rival stores and production (aggregate, region-ready, from data)

### 1.1 Data

New columns on `docs/data/RH_Rivals.csv` and `FRHRivalRow` (`Source/RedHopeSim/Public/Data/RHRows.h:398-413`), after `ImportLot`:

| Column | Type | Meaning |
|---|---|---|
| `RegionId` | int32 | the rival's home region; 0 for every row today (E7 / T2 fill it in) |
| `Produce` | FString, `Res:kg;...` | whole kg made each sol |
| `Consume` | FString, `Res:kg;...` | whole kg used each sol |
| `StoreStart` | FString | whole kg held at the start |
| `StoreCap` | FString | whole kg the stores hold; **the keys of this list are the resources the rival trades** |

They are parsed with `ParseResourceList`, the parser the lots already use. A row whose `StoreCap` is empty has **no economy**: it trades as today (section 3.6). A row whose lists name a resource that has no E1 slot, a value that is not a whole number, or a `Produce` / `Consume` / `StoreStart` key missing from `StoreCap`, is rejected at load with a Warning and treated as having no economy. MX0 is the guard.

**Why columns, not a new table:** a new DataTable means a new asset and sync path. Four list columns ride the existing `DT_Rivals` and its pure-data checks. They also keep a rival's whole profile on one row, which is what T5 grows into a city.

Placeholder values (director review; each Notes cell gains "Economy placeholder, director review."):

| Rival | Produce (kg/sol) | Consume (kg/sol) | StoreStart (kg) | StoreCap (kg) |
|---|---|---|---|---|
| Zarya (ice-rich crater) | Ice 400, Water 120, Oxygen 60, Struct 40, Hydrogen 10 | Ice 150, Water 140, Oxygen 60, Struct 120, Hydrogen 15 | Ice 12,000, Water 2,000, Oxygen 1,500, Struct 2,500, Hydrogen 300 | Ice 20,000, Water 6,000, Oxygen 3,000, Struct 8,000, Hydrogen 1,000 |
| Meridian (dormant; found by surveys) | SpareParts 2, Food 20 | SpareParts 1, Food 60 | SpareParts 40, Food 600 | SpareParts 100, Food 2,000 |

These make the stories the plan promised. Zarya's ice wells run full, so Ice gets cheap. Zarya builds faster than it makes Struct, so Struct gets dear unless someone sells it some.

### 1.2 State

New members in H, private, beside E3's:
- `TArray<FRHRivalEconomy> RivalEconomies`, one per rival with an economy, **sorted by `ToString()` with a case-sensitive ordinal compare**. It is built at world start and at load from the rows; it is never built from FName order. Each entry holds `Rival`, `RegionId`, and per E1 slot (index-aligned with `EconSlots`) `ProduceG`, `ConsumeG`, `CapG` (int64 grams) and `StoreG` (int64 grams, saved).
- A rival is **live** when it has an economy and `IsRivalAvailable` (W:9878) is true: an active row, or one discovered by surveys. A rival that is not live keeps its stores and does not produce, consume or count toward any price.

### 1.3 The sol

At the sol threshold, rival production runs once, inside `RollMarket` (section 2.2). For each live rival in name order, then each slot in slot order:

```
After  = StoreG + ProduceG - ConsumeG
if After > CapG:  After = max(CapG, StoreG - ConsumeG)     // production never fills past the cap ...
StoreG = max(0, After)                                     // ... but goods the colony sold are never destroyed
```

A rival's own production stops at the cap: its wells "run full", and the surplus is wasted. Goods the colony sells it can take a store past its cap (3.4); they are then used up by consumption, never deleted. Integer grams, once a sol, from data. It is the same in both bands, and the agent band cannot touch it. A rival that runs dry simply goes without: there is no rival population, Hope or growth in E4 (T5 brings those). Production is not affected by sabotage, embargo or covert operations in E4. E6 makes it so.

### 1.4 Region-ready

Every economy carries `RegionId`. There is one market per region (section 2), and a rival trades in its region's market. Today every rival is region 0, so there is one market and play is unchanged. E7 adds depots and routes. T2 places rivals in regions, and T3/T5 run them per region at sol thresholds, which is where E4 already touches them.

---

## 2. The market

### 2.1 State

`TArray<FRHMarket> Markets`, one per region with a live rival (one today, region 0). Each holds, per E1 slot (index-aligned with `EconSlots`):
- `PriceMcr` (int64 mcr/kg; saved), starting at the slot's `EarthValue_mcr`;
- the **market tally**: a ring of `MarketRing` (8) completed sols, `RingSoldG[8]` and `RingBoughtG[8]` (int64 grams; saved). "Sold" is what the colony sold to the market, and "bought" is what it bought, as **filled at the clearing** (section 3.4), written into the sol the clearing closes;
- one shared `RingFill`, `RingHead` and `MarketCheckedSol`.

A slot is **traded** in a region while at least one live rival there lists it in `StoreCap`. Only traded slots reprice and take orders. An untraded slot keeps its last price and refuses orders.

### 2.2 The repricing

`RollMarket(SolNow)` is called in `StepCityGrowth` (G:486-487), straight after `RollEconomyLedger(SolNow)`, with the same `SolNow`. That is a threshold on the monotone clock, so both bands reprice on the same step. The order inside is fixed, once for each sol the threshold closes (a long jump closes several, at most 8, the ring's length; as E1's roll does):
1. **the clearing** (3.4): every booked order of the closing sol, by convoy Id, at the prices in force during that sol; the fills go into that sol's ring entry;
2. rival production and consumption (1.3), rivals by name string, slots in slot order;
3. reprice every traded slot, in slot order. The new prices hold for the sol that opens.

The update for one slot r in one market, all `int64`:

```
W        = MarketWindowSols                                        // 5; at most MarketRing
Prod, Cons = sum over live rivals (name order) of ProduceG, ConsumeG    // grams a sol
Sold, Bought = sum of the ring's last W completed sols                  // grams
Supply   = Prod * W + Sold * ColonyPct / 100                       // ColonyPct = MarketColonyFlowPct, 100 (the fallback sets 0)
Demand   = Cons * W + Bought * ColonyPct / 100
FlowBp   = (Supply - Demand) * 10000 / max(Supply + Demand, MarketMinVolumeG)            // -10000 .. 10000
FillBp   = min(sum StoreG, sum CapG) * 10000 / max(sum CapG, 1)                         // 0 .. 10000
StoreBp  = clamp((FillBp - MarketTargetFillBp) * 2, -10000, 10000)
AnchorBp = clamp((Price - V) * 10000 / V, -10000, 10000)                                // V = EarthValue_mcr > 0
Pressure = (FlowBp * WFlow + StoreBp * WStore + AnchorBp * WAnchor) / 100               // + = too much on offer
StepBp   = clamp(-Pressure * MarketGainPct / 100, -MarketMaxStepBp, +MarketMaxStepBp)
Delta    = StepBp == 0 ? 0 : sign(StepBp) * max(1, |Price * StepBp| / 10000)            // floor division of non-negatives
Price    = clamp(Price + Delta, V * MarketFloorBp / 10000, V * MarketCeilBp / 10000)
```

- **Integer throughout.** No float or double enters the update. Every division is of non-negative int64 values except two: the flow quotient and the anchor quotient, whose sign is taken first and whose magnitude is then divided. The rounding is written once, in a helper, and MX2 pins it.
- **Bounded.** One sol moves a price by at most `MarketMaxStepBp` (500, that is 5%), or 1 mcr when 5% rounds to zero. The price never leaves `[0.25 V, 4 V]`.
- **Flows, not stock, lead.** The flow term reads the window: what the rivals make and use, plus what the colony offered and took. Stores enter only as a fill ratio, and the anchor pulls toward the Earth value so a price does not sit at a bound for ever without a cause.
- **Overflow.** With per-row kg capped at 1,000,000 a sol (MX0 checks), `Supply + Demand` stays under 2 x 10^13 g, and `x 10000` under 2 x 10^17. Both fit in int64 with room to spare.

Defaults (new `RH_Config` rows, placeholders; the C++ fallbacks equal the CSV): `MarketWindowSols` 5, `MarketMaxStepBp` 500, `MarketGainPct` 20, `MarketFlowWeight` 50, `MarketStoreWeight` 30, `MarketAnchorWeight` 50, `MarketTargetFillBp` 5000, `MarketFloorBp` 2500, `MarketCeilBp` 40000, `MarketMinVolumeG` 1,000,000, `MarketColonyFlowPct` 100.

**Worked out at those numbers:**
- **Ice at Zarya, no trade.** Flow = (2,000,000 - 750,000) / 2,750,000 = +4545 bp. The store is 12,000 of 20,000, so Store = +2000 bp. Pressure at P = V is (4545 x 50 + 2000 x 30) / 100 = 2872, and the step is -574, clamped to **-5%**. Ice slides 5% a sol, 3.00 to 0.75 cr/kg (the floor) in 27 sols, while Zarya's wells fill.
- **Struct at Zarya, no trade.** Flow = -5000 bp; the store drains 80 kg a sol, so Store falls to -10000. Struct climbs 5% a sol until the anchor balances it: (P - V) / V = 1.1, about **2.1 x V = 10.5 cr/kg**, unless the colony sells Struct to Zarya.

### 2.3 Order and determinism

- Resources: E1's slot order (the resource rows sorted by name string), the order every economy block already uses.
- Rivals: `RivalEconomies` order, sorted by name string with a case-sensitive ordinal compare. **Never by FName index**: the covert seed bug of 2026-07 came from exactly that (`RollCovertDetected`, W:9884-9890, now content-hashes the name). Sums of integers do not depend on order, but the logs, the save and any future tie-break do, so the order is fixed anyway.
- Markets: by `RegionId`. Convoys: by Id.
- Reads never insert (E1 R7): every read of a stock, store or relation uses `Find` / `FindRef`.

### 2.4 What reads a price

**Only orders** (section 3) and the presentation (section 8). No sim rule, growth bill, Hope term, population rule, Earth price or award reads a market price or a rival store. Earth's prices stay E3's: `EarthValue_mcr` x `GetRequisitionMultiplier` (W:10052) less the launch cost. That is what makes section 4's inertness hold.

---

## 3. Convoys carrying orders

### 3.1 The order

A new uplink verb, `MarketOrder`. `Target` is the rival. `Text` carries the lines as `Res:kg;Res:kg`, with a positive kg to buy and a negative kg to sell (for example `Ice:500;Struct:-200`). `FRHCommand::Text` exists and is saved with queued orders since v37 (H:49). It is parsed by a dedicated `ParseOrderLines`, not `ParseResourceList`, so that a sign, a zero, a fraction, a duplicate resource or a resource both bought and sold is refused by name.

### 3.2 Booking (`CanPlaceOrder(Rival, Lines, FString& OutWhy) const`, one rule for the verb, the deck and the cheat)

**Booking reads the colony, the data and today's quote, never a rival's store.** The quote only sizes the colony's reservation, and the reservation never limits a fill (3.4). So nothing booking reads can change what the market does, which is what keeps the market band-exact (section 5). In order:
1. **The rival.** Available, and with an economy. Then today's convoy refusals, with their texts unchanged (W:9491-9517): no contact, the route closed, sabotage, defection, embargo.
2. **Convoys out.** Fewer than `MaxConvoysOut` (new config row, 3; Q-B), and none already out to this rival: `The convoy to %s is already out`.
3. **The lines.** Each resource is traded in the rival's region (2.1). Each way the total is at most `ConvoyCargoKg` (new config row, 1,000).
4. **The colony can load it.** One aggregate lot, as the convoy has always built it (W:9524-9534; the M3 adversarial fix): fuel `ConvoyH2PerRun` + wear `ConvoyWearParts` + every sell line, summed per resource, each against **E3's loadable** (`GetLaunchLoadableKg`: uncommitted holdings less the life-support floor) with HEAD's trade tolerance.
5. **The money.** The order's **reservation**:

   ```
   Hi(q) = q * (10000 + MaxStep) / 10000 + 1          // one full step up, and the 1 mcr minimum step
   Lo(q) = max(0, q * (10000 - MaxStep) / 10000 - 1)  // one full step down
   R     = max(0, sum(buy kg x Hi(quote)) - sum(sell kg x Lo(quote)))
   ```

   Here the quote is today's slot price and `MaxStep` is `MarketMaxStepBp`. The clearing price is the price in force during the order's sol. That is the quote itself, or one step from it when an era step books an order for the next sol before the roll. Sells always fill in full, and buys never fill past what was ordered, so `R` always covers the net. The colony's spendable credit must hold it: `The order may cost up to %s cr net; %s cr is spendable`.

### 3.3 Departure (the uplink verb's worker, any step)

If every check passes:
1. Draw the sell lines, the fuel and the wear through `TakeGrowthResource`, as E3's launch draws. Record E1: fuel and wear **Used**, the sold goods **Out**.
2. Reserve R. This is a new saved counter, `CreditReservedMcr`, with `GetSpendableMcr() = Balance - Reserved`. E2's manifest check (`CanAddManifestItem`) and E3's launch check (`CanLaunchShip`) read spendable instead of the balance. With nothing reserved, that is the balance, so neither moves.
3. Append a convoy record (3.5) with its **clearing sol** = `floor(Cmd.ExecuteAtSimSeconds / SolLength)`. That is the order's own scheduled time (`EnqueueCommand`, W:8636-8637), not the step that runs it.
4. Log `Order #%d to %s: buy %s, sell %s; clears at the end of sol %d; convoy %d of %d out`.

The convoy leaves now, with the sold goods aboard. The market has not been touched.

### 3.4 The clearing (inside `RollMarket`, the market's only write)

For each convoy whose clearing sol is the sol being closed and which has not cleared, by convoy Id:
1. **Sells fill in full.** The rival takes everything it is sold, in resource-name order; its store rises by it, past the cap if it must (1.3). A glut is answered by the price, never by a refusal, so a sell never comes home.
2. **Buys fill up to the rival's store**, in resource-name order, in whole kg. The rival's store falls by the fill. Money never limits a fill: the reservation was sized for the whole order (3.2), so the fills depend only on the rival's state and the orders, which are band-exact.
3. **Price.** The clearing price is the slot price in force during the closing sol (the price before this roll's step 3).
4. **The money.** `Net = buys filled x price - sells filled x price`. If `Net > 0`, it is paid now through **`CreditSpend(Net)`**. This is a new private primitive and, with it, the **only debit writer**: E3's `FinishLaunch` (X:320-323) calls it instead of writing the balance and the spent total itself, so E2's invariant keeps one writer on each side. If `Net < 0`, the convoy carries `OwedMcr = -Net`, paid at its return. Either way, the order's reservation is released (`Reserved -= R`).
5. **The tally:** the fills are added to the closing sol's ring entry.
6. **The record:** the convoy's filled kg per line and `bCleared`. The clearing logs one line per order, `Order #%d to %s clears: bought %s, sold %s at %s cr net`, except a standing order (3.6).

**Politics after booking does not undo a deal.** A rival that embargoes, defects or is sabotaged after the order was booked still clears it, as a convoy out today still lands its goods (W:9788-9794: "paid for at dispatch - conservation"). Only the goodwill at the return is withheld, as today.

### 3.5 Several convoys

The single convoy (`ConvoyRival`, `bConvoyReturning`, `ConvoyLegSols`, H:3722-3724) becomes `TArray<FRHConvoy> Convoys`, ordered by Id, with `NextConvoyId`. Each record holds:
- `Id`, `Rival`, `bReturning`, `LegSols`;
- `ClearSol`, `bCleared`, `ReservedMcr`, `OwedMcr`;
- the lines: resource, signed kg ordered, kg filled; sorted by resource name;
- `bStanding` (3.6).

`StepTrade` (W:9728-9824) walks the list by Id with the same leg logic per convoy: a dust storm freezes every convoy, each leg is `DistanceKm / ConvoySpeedKmPerSol`, and the spill carries into the return. A convoy cannot complete its return before its order has cleared. It holds at the end of its last leg until the clearing. MX0 checks that every shipped rival's round trip is at least a sol, so with the data this never happens.

**At its return:**
- the bought goods, as filled, land at the Lander, or in the pool for fluids, exactly as today (W:9766-9787), recorded **In**;
- `OwedMcr` is paid through `CreditPay`, and a new lifetime `MarketSalesMcr += OwedMcr`. The invariant becomes `0 <= SalesPaidMcr + MarketSalesMcr <= CreditAwardedMcr` and `0 <= CreditReservedMcr <= CreditBalanceMcr`;
- the goodwill rule is today's (W:9794-9817), with one addition: a convoy warms the relation, the HumanNature axis and Influence only when its filled value (buys plus sells at the clearing price) is at least `GoodwillMinValueMcr` (new config row, 300,000 = 300 cr). Without this, ten one-kilogram orders would farm ten runs' goodwill. The standing order at reference prices is worth 950 cr, so it qualifies;
- the record is removed.

**Readers of the single convoy.** `GetConvoyRival` and `IsConvoyReturning` (H:1616-1617), `GetConvoyProgress` (W:9826), the deck's convoy bar (K:2116-2128), the visualizer (V:4266, :4330) and `RH.Trade` read the **lowest-Id** convoy. That is exactly today's single convoy whenever at most one is out. A new `GetConvoys()` serves the list; section 8 moves the deck and the visualizer onto it.

### 3.6 The standing order: today's fixed lot (Q-A)

A bare `Convoy` order (W:9485), or `RH.Convoy`, keeps its verb and becomes the rival's **standing order**: sell its `ImportLot`, buy its `ExportLot`.

- **A rival with an economy.** The order goes through 3.2-3.5 (Q-A (a)). The booking draws the same kilograms from the same stores. The refusal texts, the E1 records and both log lines (`Convoy dispatched to %s (...)` at departure, `=== CONVOY RETURN: ... ===` at the return) are **unchanged**, and the clearing logs nothing for it. The only new effect is the net in credits. At reference prices Zarya's standing order sells 100 Struct (500 cr) and buys 150 Ice (450 cr). So its reservation is 0 (472.5 - 475 < 0), and it pays the colony **50 cr** at the return.
- **A rival with no economy** (an injected test rival such as `-trade`'s Greed, or a data row with an empty `StoreCap`): today's barter, exactly. No price, no credits, no store, no clearing.

Zarya's data fills the standing order with a wide margin: its Ice store of 12,000 kg is 80 runs, and it only grows. So every convoy fixture's Ice still lands in full.

---

## 4. Inertness

### 4.1 The switch

`IsMarketLive()` = `bMarketEnabled` (a runtime harness flag, default true, never saved; `Debug_SetMarketEnabled`) **and** at least one live rival (1.2). When it is false, `RollMarket` returns at once: no production, no repricing, no tally. `MarketOrder` is refused with `No market: no neighbor trades yet`, and the bare `Convoy` is today's barter.

### 4.2 The bars

- **No rival active (the brief's bar).** The market never runs. The battery is line-identical except the save-version lines and the masked `(N bytes` lines, plus the new `market.log`. The E4 block is written with its start values.
- **Rivals active, no market order (the battery's real state).** Zarya ships `SliceActive`, so `HasActiveRival()` (W:9851) is true in every default colony, and the market does run in every suite. It must still move no line, for three reasons:
  1. nothing outside the market reads a price, a store or the tally (2.4);
  2. the market logs nothing on a reprice;
  3. the standing order keeps its kilograms, refusals, E1 records and both log lines, and only adds credits, which no convoy suite prints.

  The suites that dispatch convoys are `-trade` (11 convoy lines in `battery_post_e3/trade.log`), `-preemptive` (4), `-covert` (2), `-espionage` (2) and `-economy` EC3 (2); `-solidarity`'s dispatch is refused. None launches a ship or prints a credit balance. The builder confirms with `grep -c "cr carried\|Earth credit"` over those five logs: it must be 0.
- **The 110-sol playtest is byte-identical.** The bot never orders a convoy (`git grep -n "Convoy" -- Source/RedHope/Private/RHPlaytest.cpp` finds only a rival-name report, P:546-556). Repricing logs nothing, and the `.txt` holds only `[Playtest]` lines.
- **`-scale`:** every digest is identical (it never trades).

One deliberate change in how the standing order draws, so section 4's claim is checked rather than assumed: it moves from `SpendTradeLot` (W:9666, which drains construction-site hoppers and unlinked floors) to E3's uncommitted draw. Every convoy fixture holds its lot in the Lander's finished stores, so the same kilograms leave the same stores. The builder verifies this per fixture (risk 5). A fixture that differs is a finding, not a fixture edit.

---

## 5. Era parity

**By construction, the price path is band-exact.** Every market quantity (prices, rival stores, the tally, fills and charges) is written in one place, `RollMarket`, which runs on the same threshold step in both bands (G:486-487). Its inputs are band-independent:
- **The rival side** (production, consumption and the daily step of the stores) is integer data.
- **The orders** enter only through their clearing sol, `floor(ExecuteAtSimSeconds / SolLength)`. That is a property of the order (its issue time plus the order lag), not of the step that runs it.

**Why the clearing sol, and not the step that runs the order.** Era steps are not aligned to sol boundaries: the clock adds 60 s from wherever the agent band left it (`RHSimClockSubsystem.cpp:19-23`). Take an order whose `ExecuteAt` is 10 s past a boundary.
- **Era band:** it runs in `StepUplink` (W:1065-1081, second in the step) on the very era step that crosses the boundary, before that step's roll.
- **Agent band:** it runs about 100 sub-steps after the roll.

If the market acted when the order ran, the era band would put it in the old sol and the agent band in the new one. Their stores and fill ratios would then differ at the reprice. Bucketing by `ExecuteAt` puts it in the new sol in both. An order whose `ExecuteAt` is at or before the boundary runs at or before the threshold step in both bands (a step runs every order whose `ExecuteAt` is not after its end), so it clears at that roll in both.

**Booking never reads a rival's store, and nothing it reads limits a fill** (3.2, 3.4). If booking read a store, the era band could book against it before a roll and the agent band after it. So stores are met at the clearing, as fills, sells always fill, and buys fill up to the store alone. The quote booking reads may be a step apart between the bands, but it only sizes the colony's reservation, which covers either price.

**What may differ between bands** is colony-side only:
- when a convoy comes home inside a sol (the leg accumulator crosses on a 0.1 s step or a 60 s step), and so when the goods land and `Owed` is paid;
- the size of an order's reservation when an era step books it before the roll (it is released at the clearing either way);
- whether an order is accepted at all, when the colony's holdings or credit sit right at the edge of the order. The bands' holdings differ within E1's 5% bar, so an order at the margin can pass in one and not the other. That is a difference in the player's input, not in the market. The paired run keeps a wide margin.

**The paired run (MX8).**
- One save; a scripted trader sends the same orders, through `EnqueueCommand` at the same sim times, on sols 1, 4, 7, 10 and 13 at sol fraction 0.5, in each band; both run 20 sols.
- **The boundary case:** one more order is sent so that its `ExecuteAt` falls 10 s after a sol boundary, with the era band's steps placed so that the boundary step runs it before the roll.
- Bar: the price of every traded slot at every threshold is **identical** (int64 equality), and so are every rival store, every fill, every clearing sol and every charge.
- The sols in which convoys come home, and the end balances, are equal. The money arrives at different steps but in the same sol.

**The fallback, if the bands diverge.** Setting `MarketColonyFlowPct` to 0 reprices on rival-side numbers only (production, consumption and store fill). The colony still moves prices, but only through the rival stores its orders fill and empty at the clearing, which the agent band cannot touch. MX8 runs the fallback twin as well and holds it to the same identical bar. If the main run ever fails and the fallback passes, the builder reports both to the director before choosing. The bar is not loosened.

---

## 6. Save

**Version: the next free one** (v54 if E3b adds none). The number is assigned at build time.

**The market block** is written straight after E3's export block, as the last thing in the file, and read after teardown like E1's, E2's and E3's blocks. So it repairs with a Warning and is never refused.
- The market part. For each region in Id order: `RegionId`; then for each E1 slot in slot order: the name, `PriceMcr`, the 8 `RingSoldG` and the 8 `RingBoughtG`; then the ring's fill, head and checked sol.
- The rivals. Their count, then each economy rival in name order: the name, then `StoreG` for every slot (0 where it trades nothing).
- The lifetime `MarketSalesMcr` and `CreditReservedMcr`.
- The convoys. `NextConvoyId`, their count, then each convoy by Id: `Id`, rival name, `bReturning`, `LegSols`, `bStanding`, `ClearSol`, `bCleared`, `ReservedMcr`, `OwedMcr`, the line count, then each line (name, signed kg ordered, kg filled).

The market and rival parts are sized by the tables. **The convoy list is sized by history**, the first economy block that is (E2 risk 7 foresaw this). So tests measure the block with `GetMarketBlockBytes()` from the live state, never as a constant.

**The single-convoy slot (v17, W:13768 save, :14411 load)** keeps being written, from the lowest-Id convoy, or `None` / 0 / 0 when none is out. So every byte before the economy blocks is unchanged whenever at most one convoy is out (every suite today). On load, the market block is authoritative. If it cannot be read (below), the slot's convoy is rebuilt as a barter convoy, with a Warning, so a lost block never strands cargo silently.

**Load repairs** (each with a Warning; no Error is ever logged):
- a price outside `[0.25 V, 4 V]` is clamped;
- a store below 0 is set to 0 (a store above its cap is legal: 1.3);
- a ring index out of range resets the tally;
- unsorted or repeated slot or rival names reset that part to its start values;
- a convoy naming a rival that no longer exists, an untraded resource or a malformed line is dropped. Its cargo and owed credit are lost; the Warning says so;
- a lifetime sales total outside `[0, CreditAwardedMcr - SalesPaidMcr]` is clamped;
- `CreditReservedMcr` not equal to the sum of the uncleared convoys' reservations, or above the balance, is set to that sum, capped at the balance.

**A block before it that cannot be measured.** E3's `ReadExportBlock` returns early on a bad count or a short block (X:465, :473, :485). The builder adds a `bExportBlockLost` flag there, as E2b and E3 did for theirs (`bLedgerBlockLost`, `bCreditBlockLost`), and `ReadMarketBlock` then reads nothing and starts the market at its start values.

**The tests that read offsets** subtract the new block too: E1's EC1 and EC6, E2's CR9 and E3's XE10 measure from the end of the file. The builder updates them in the same commit, as E2 and E3 did.

**Checkpoints.** The bump refuses the Midgame and Late checkpoints. Re-cut them (`rh_checkpoints.sh all --force`) and load both.

---

## 7. Tests: `-market`, a new fails-loudly suite

It goes in its own file, `Source/RedHopeSim/Private/RHSimMarketSuite.cpp`, and is added to `rh_battery.sh` (one more suite, one more run). No path logs an Error on purpose.

**New harness hooks** (runtime only, never saved):
- `Debug_SetMarketEnabled(bool)`;
- `Debug_SetRivalStore(Rival, Res, Kg)`;
- `Debug_SetMarketPrice(Res, Mcr)`;
- `Debug_RollMarket(SolNow)` (the whole roll: clearing, production and reprice);
- `Debug_SetMarketColonyFlowPct(int32)`;
- const reads `GetMarketPrice`, `GetRivalStoreKg`, `GetMarketTally`, `GetConvoys` and `GetMarketBlockBytes`.

The existing `URHDefinitionsSubsystem::Debug_InjectRival` adds rivals.

| Check | What it pins |
|---|---|
| MX0 | **Data.** Zarya's and Meridian's economy columns exactly (whole kg, every key in `StoreCap`, at most 1,000,000 a sol); the CSV equals the live DT; every new config row; each traded slot's start price is its `EarthValue_mcr`; a malformed injected row is rejected with a Warning and trades by barter. |
| MX1 | **Inertness.** (a) Twins with `Debug_SetMarketEnabled` on and off, 10 sols in each band, no order: the digest (stocks, shelves, credits, relations, Influence, HumanNature, tension, Hope, the quota phase) is bit-identical, and the saves are byte-identical up to the market block. (b) The same with every rival made inactive (`Debug_InjectRival` with `SliceActive` false): the market block holds its start values. |
| MX2 | **One reprice, by hand.** A fixture with known stores and tallies; `Debug_RollMarket`; every traded slot's new price equals a hand-computed integer written in the suite, including a step that rounds to zero (the 1 mcr minimum) and both clamps. **Rival order:** two injected rivals created in the reverse of their name order, so FName index order and name order differ, give the same prices and the same saved bytes as the forward order. |
| MX3 | **A glut.** Zarya's Ice at its cap, the colony selling Ice every sol: the Ice price falls exactly 5% a sol (the clamp binds), reaches 0.75 cr and stays. A sale past Zarya's room is taken in full; the store stands above its cap, Zarya's own production adds nothing until consumption brings it back under, and not a gram is deleted. |
| MX4 | **A shortage.** The colony buys Zarya's Struct down: the price rises at most 5% a sol, never past 4 V; a buy above the store fills to the store, and only what filled is charged. |
| MX5 | **Bounded and integer, by enumeration.** 10,000 reprices from a seeded LCG over stores, tallies and prices: every step is within 5% or 1 mcr, every price is inside the band, and the result equals a reference written independently in the suite with the same integer rules. |
| MX6 | **Orders.** Every refusal of 3.2 with its text. A buy and sell order: its draws and reservation at booking; nothing in the market until the roll; at the clearing the fills, the rival stores, the tally, the `CreditSpend` and the reservation released; `CreditPay` and the landed goods at the return; E1's legs, all exact. A buy beyond the store fills to the store. The reservation covers the charge when the slot price moves a full step (or the 1 mcr minimum) between booking and clearing (`Debug_SetMarketPrice` before the roll). Three convoys out, a fourth refused, a second to the same rival refused. E2's manifest and E3's launch read spendable credit. The invariants after every step. |
| MX7 | **The standing order.** A bare `Convoy` to Zarya: Struct -100, Ice +150, relation 40 -> 42, the two log lines byte-identical to HEAD's, plus 50 cr at the return at reference prices. A rival with no economy (Greed) trades by barter with no credit. The goodwill floor: an order under 300 cr comes home with no relation change. |
| MX8 | **Era parity.** Section 5's paired run, including the boundary case, the main twin and the fallback twin, all held to identical. |
| MX9 | **Save.** Two convoys out and a moved market: save, load, save is byte-identical; the block's names read back ascending; the v17 slot mirrors the lowest-Id convoy; every repair in section 6 on a doctored file, with the colony loaded; the previous version refused; a lost earlier block starts the market fresh and rebuilds the v17 convoy as a barter. |
| MX10 | **Ruling 11 and Q4a.** Prices forced to both bounds against the start values, 10 sols, no order: Hope, the population, growth and every stock are bit-identical. |
| MX11 | **Politics.** Orders are refused through a closed route, sabotage, defection and embargo with today's texts; a convoy out when its rival defects lands its goods and pays its owed credit, with no goodwill (today's rule). |

**Mutations** (MX12; each is built, run and restored by hash, and must fail at least one check; the builder records which):
- a `double` in the update (MX2, MX5);
- no step clamp (MX3);
- rivals summed in FName index order with a tie-sensitive step (MX2);
- the clearing sol taken from the step that runs the order instead of `ExecuteAt` (MX8's boundary case);
- the rival's store read at booking (MX8's boundary case);
- no reservation at booking (MX6);
- the reservation without its one-step margin (MX6: the charge outruns the reservation);
- a buy capped by the order's money instead of the store alone (MX8's boundary case);
- the colony's own production ledger fed into Supply (MX8, MX1).

**`-scale`** (outside the battery): 1,000 `RollMarket` calls with 8 injected rivals trading every slot, at N = 5,000; budget 2 ms in all. Every digest identical.

**What the battery moves:** the save-version lines (E1 R4's list and those D6 and E1-E3 added); the masked `(N bytes` lines; the offsets in EC1, EC6, CR9 and XE10 (code, no log line); and the new `market.log`. Nothing else, and in particular not one convoy line (4.2).

---

## 8. Presentation (RedHope; rebase onto whatever E3b leaves)

- **The rival card** (K:2135-2202) gains:
  - a price row per traded resource: today's price and an arrow for the last step;
  - a **draft order**: per traded resource, `Buy +100` / `Sell +100` and the draft's kg;
  - a `SEND ORDER` button read by `CanPlaceOrder`, which is greyed with the reason in its tooltip. The draft shows what the order may cost (its reservation) and that it clears at the end of the sol, up to what the rival holds (its stores as of the last roll).

  TRADE stays; it sends the standing order.
- **The convoy strip** (K:2116-2128) lists every convoy, by Id, with its rival, its leg and its progress.
- **The visualizer** (V:4266-4330) draws every convoy out, each along today's lerp. T6 makes them journeys.
- **Console:** `RH.Market` (prices, the tally window, rival stores, convoys) and `RH.Market order <Rival> <Res:kg;...>`. `RH.Trade` gains the convoy list and each rival's stores.
- New strings go into the wording table as placeholders. None is morale, sickness or evacuation wording, so Gate D's review is not engaged. E8 is the full market panel with price history; E4 shows only what an order needs.

---

## 9. Risks

1. **Feedback.** Prices move orders and orders move prices. It is bounded three ways: a 5% step, a band of [0.25 V, 4 V], and an anchor. Nothing in the sim itself responds to a price (rivals produce from data), so the only loop runs through the player. MX5 proves the bound for any input.
2. **Arbitrage with Earth (Q-C).** A Martian price below what Earth pays (E3: V x the requisition multiplier - 3 cr/kg) lets a colony buy on Mars and sell to Earth. It is bounded by the rival's store, the cargo cap, three convoys, the 5% climb that buying causes, and the ship's 30,000 kg. It is real money, though, and the director rules on it.
3. **Era parity at the edges.** Section 5's argument depends on every market effect being in `RollMarket`, and on orders entering it only by their `ExecuteAt`. Two later changes would break it: a gate that clears at a convoy's arrival (T6's journeys), or any order path that books without an uplink command (a direct call with no `ExecuteAt`). Either must re-prove it. MX8's boundary case and its two mutations guard it, and `MarketColonyFlowPct` is the fallback switch. The deck's direct calls (E2's manifest, E3's launch) are not market effects.
4. **A day's wait.** An order is booked at once, but the market fills it only at the end of the sol, up to 20 real minutes at 1x. The convoy leaves at once, so nothing waits on the road, but the deck must say when the order clears and what it may cost (the reservation), not promise a fill.
5. **The standing order's draw changes** from `SpendTradeLot` to E3's uncommitted draw (4.2). If any convoy fixture holds its lot in a site hopper or on an unlinked floor, its line moves. The builder checks the five fixtures before the build.
6. **The single-convoy readers.** Every reader of `ConvoyRival` must move to the lowest-Id convoy or to the list, or a second convoy is invisible. There are seven: the deck, the visualizer, `RH.Trade`, `-trade`'s checks, `GetConvoyProgress`, the preemptive / embargo code, and the save. The builder greps `ConvoyRival` and lists them.
7. **A history-sized save block** (6). Its size depends on the convoys out, so the offset tests must read `GetMarketBlockBytes()` at the moment of the save.
8. **What E4 leaves to later gates.**
   - E5: prices in a Martian currency.
   - E6: politics, which covers relation and identity markups, sabotage cutting a rival's production, and covert loot coming out of rival stores. In E4 covert loot is still `CovertLotFraction x ExportLot` from nowhere (W:9925-9952), so a rival's store does not lose it.
   - E7 / T2: regions and depots.
   - T5: rival growth.
   - T6: rovers driving convoys.

   E4 keeps each seam (RegionId on every economy, a list of convoys, prices on a slot table) without using it.

---

## Director questions

**Q-A. What happens to today's fixed trade with a rival (your 100 Struct for Zarya's 150 Ice)?**
- **(a) (recommended):** It stays, as that rival's standing order. The same goods change hands, but at market prices, settled in credits: when your side is worth more, the difference comes home with the convoy, and when it is worth less, you pay it when the market settles at the end of that sol. At today's reference prices Zarya's trade pays you 50 credits a run. You can also send orders of your own.
- (b) It stays a straight swap, goods for goods with no credits, beside the new priced orders.
- (c) It goes. Every convoy carries an order you write.

**Q-B. Do the cargo rovers drive the convoys now?**
- **(a) (recommended):** Not yet. A convoy stays an abstract trip with a timer, up to 3 out at once and one per neighbor. Real rovers driving real routes come with the regional map (T6).
- (b) Yes. Each convoy needs a cargo rover, which leaves the colony while the convoy is out, so rovers you send trading are not hauling at home.

**Q-C. May the colony buy cheap on the Martian market and sell to Earth?**
- **(a) (recommended):** Yes. A trading colony is a legitimate path. It is limited by what the neighbors hold, the cargo each convoy carries, prices that climb 5% a sol while you buy, and the ship's 30,000 kg.
- (b) No. A Martian price never falls below what Earth pays for the same resource, so a glut on Mars is never cheaper than Earth's offer.

---

# PROFESSOR REVIEW

An independent adversarial review against HEAD `ea9a5cd` (2026-10-24), read-only, with `git show HEAD:`. It is recorded as returned, lightly formatted.

Verdict: **BLOCKED**

In the reviewer's words: "The design is not correct as written. A V = 0 slot divides by zero. Injected rivals never get an economy. Credit can be overdrawn. A missed clearing strands a convoy for ever. The fallback and several mutations cannot do their job. The worked examples are wrong."

## What holds (checked)

- Every line anchor the reviewer checked matches HEAD.
- The inertness argument holds for the five convoy suites and the playtest. Nothing else prints credits, and the standing order's reservation stays 0 throughout.
- The margin is small at reference prices (472,650 against 474,900 mcr). It widens because Ice falls and Struct rises.

## REQUIRED CHANGES

**1. Injected rivals never get an economy, and Greed is not economy-less.**
- Section 1.2 builds `RivalEconomies` only "at world start and at load". `Debug_InjectRival` only adds a table row (`RHDefinitionsSubsystem.cpp:524-532`).
- So the injected rivals in MX2, in MX6 and in `-scale` (8 rivals) have no economy until a load. MX6's "three convoys out" needs three live economy rivals; the data has two rows, and Meridian is dormant.
- Greed is a full copy of Zarya's row (C:1116 `FRHRivalRow Greed = *ZRow;`). It inherits the new columns, so the claim in 3.6 and MX7 that it has no economy is false.
- After `-trade`'s save and load (C:1136-1137), Greed becomes a live economy rival that the saved block never held, so save, load, save differs.
- **Change:**
  - define the rebuild trigger;
  - define the load rule for a data rival missing from the block (it starts at `StoreStart`);
  - build MX7's barter rival with the economy columns emptied.

**2. Division by zero.**
- Compost is an E1 slot with `EarthValue_mcr` 0 (`RH_Resources.csv`; EC0's pin, `RHSimEconomySuite.cpp:460-463`). Section 1.1's guard rejects only names with no E1 slot.
- `AnchorBp` divides by V, so `-scale` ("8 injected rivals trading every slot") divides by zero.
- **Change:**
  - reject any `StoreCap` key whose V ≤ 0, in MX0 and at load;
  - require `MarketMinVolumeG` ≥ 1;
  - bound `StoreCap` and `StoreStart`. Uncapped, `FillBp`'s `x 10000` can overflow; the overflow note covers only the per-sol amounts.

**3. The money path can overdraw.**
- (a) A third balance reader is missed: `LaunchShipShort` (X:383-384, `Cost > CreditBalanceMcr`). A short launch can spend reserved credit.
- (b) "R always covers the net" assumes the price is at most one step from the quote. That fails under the clock jumps that 2.2 itself handles: E:60 closes up to 8 sols in one step, `-trade` jumps the clock (C:1102/1106), and the design has its own `Debug_RollMarket`. An order booked after a jump but before the roll quotes a price several steps from the clearing price.
- (c) The reservation repair caps the counter "at the balance". That leaves the convoys' `ReservedMcr` summing to more than the counter, so `Reserved -= R` can go negative.
- (d) `CreditSpend` has no defined behaviour when Net exceeds the balance. E2's bar (CR7; e2 design:259) is that the balance never goes negative.
- **Change:**
  - add `LaunchShipShort` to the readers of spendable credit;
  - size R by the number of reprices between the open sol and `ClearSol`, or refuse an order that clears later than the open sol + 1;
  - specify that `CreditSpend` never goes below 0;
  - make the repair keep the per-convoy reservations equal to the counter.

**4. Clearing on "== the sol being closed" (3.4) can strand a convoy for ever.**
- A convoy holds at the end of its last leg until it clears, and only one convoy may be out per rival. A missed clearing therefore blocks that rival's trade permanently and never releases the reservation.
- Paths to a missed clearing:
  - a `ClearSol` already past (a doctored or lost block, or a jump of more than 8 sols);
  - `RollMarket` returning at once when `IsMarketLive()` turns false with a priced convoy out;
  - a lost block whose `MarketCheckedSol` start value is not tied to the loaded clock. Compare E's `EconCheckedSol = Checked > 0 ? Checked : ProductionCheckedSol`.
- **Change:**
  - clear every uncleared convoy with `ClearSol` ≤ the sol being closed;
  - add the matching load repair;
  - seed `MarketCheckedSol` from the loaded clock;
  - define what happens to a priced convoy while the market is off.

**5. Both worked examples are wrong** (recomputed with 2.2's own integer rules).
- **Ice.** The 5% clamp binds only on sols 1-3. The step is -492 bp on sol 4 and -381 by sol 10. The floor comes at sol 37, not 27.
- **Struct.**
  - Flow is -4000, not -5000, because Supply + Demand = 800,000 g, below `MarketMinVolumeG`.
  - The store term starts at -3750 and reaches -10000 only when the store empties at sol 32.
  - The equilibrium is 2.0 V (10.0 cr), where `AnchorBp` hits its +10000 clamp, not 2.1 V.
- **The anchor.** With the doc's -5000 flow, the clamped anchor could not balance at all, and Struct would climb to 4 V. So "the anchor stops a price sitting at a bound" is false above 2 V.
- **MX3.** "The colony selling Ice every sol" is impossible with one convoy per rival on Zarya's 4-sol round trip. At 1,000 kg every 4 sols, the clamp stops binding at sol 26.
- **Division.** "Every division ... except two" is wrong: `Pressure / 100` and `StepBp / 100` also have signed numerators.
- **Change:**
  - fix the numbers;
  - state that the anchor saturates at 2 V;
  - build MX3's fixture so the clamp provably binds.

**6. The fallback is a no-op.**
- `MarketColonyFlowPct = 0` removes only the tally. The colony's fills still move the rival stores, and the stores drive the store term.
- The tally and the stores come from the same fills. So every divergence that section 5 admits (an order accepted in one band and not the other) diverges the fallback too.
- That is not the plan's "rival-side numbers only, which the agent band cannot touch" (economy-plan.md:82).
- **Change:**
  - price the fallback from rival-only quantities: a shadow store, or store weight 0;
  - show in MX8 a case where the main twin fails and the fallback passes.

**7. Section 5's parity argument leaves out band-dependent inputs.**
- **Discovery.** Which rivals are live depends on survey discovery, which only the agent band drives (`RHRobotStateTree.cpp:164` → `CompleteSurvey`, W:13104 → `MaybeDiscoverSettlement`, W:13140). C:1468-1470 says era-mode RunSols does not drive surveys.
- **Returns.** "None already out to this rival" depends on the step the convoy returns on, which differs by band.
- **Tick.** "The agent band runs it about 100 sub-steps after the roll" holds only in the commandlet. In `Tick`, `StepUplink` reads the frame end (`Clock->GetSimSecondsTotal()`, W:1067, against W:630-636), so an order can run before the roll in the same frame.
- **MX8.** Today it can only trade with Zarya, and the orders on sols 4 and 10 are refused because the convoy is still out.
- **Change:**
  - add these to "what may differ";
  - MX8 must fix the discovered set and exercise at least two rivals.

**8. Several mutations cannot fail their named check.**
- **"A buy capped by the order's money".** R covers the full buy at any price within one step, so this cap never binds.
- **"The store read at booking".** It is caught only if the boundary order buys more than a store the roll changes. Zarya holds 12,000 kg of Ice against a 1,000 kg cargo cap.
- **"The clearing sol from the step that runs the order".** The era boundary step's end (G:480) is already in the new sol, so it equals `floor(ExecuteAt / Sol)`. The mutation must be defined as the market's open sol at booking.
- **"FName order with a tie-sensitive step".** Integer sums do not depend on order, and there is no tie-sensitive step. Use "`RivalEconomies` built in FName order", which MX2's saved bytes and MX9's ascending-name check would catch.
- **Change:**
  - specify the boundary fixture: `Debug_SetRivalStore` below the order, a store the roll moves, and a balance that binds;
  - redefine the mutations.

**9. The brief asks for repricing "from the ledger windows", but the design never reads E1.**
- E1 was committed as "read by nothing yet" (`a7d3036`). The plan says "supply and demand in the ledgers" (economy-plan.md:41).
- The design reprices from its own fill ring plus the rivals' data, and makes reading E1 a mutation, yet it still claims the "windowed ledger" mitigation.
- **Change:** state it as a deviation, with its reasons and its consequence: the parity bar tightens from "within a tolerance" to identical. The reasons:
  - E1's Out leg includes Earth launches (X:350), and its In leg includes ship landings;
  - convoy In is recorded at band-dependent return steps.

**10. "What the battery moves" is incomplete.** "The offsets in EC1, EC6, CR9 and XE10 (code, no log line)" is false.
- EC1 prints unmasked whole-file sizes ("both saves are 9992 bytes", economy.log).
- XE10 prints "the export block is the last 513 bytes" (export.log), which stops being true.
- EC6 prints "the 585 bytes of the credit and export blocks", which would move if the market block were folded into `CreditBytes` (`RHSimEconomySuite.cpp:824`).
- **Change:** list these lines as moving.

## OBSERVATIONS (non-blocking)

1. (Recorded under "What holds".)
2. **The "already out" refusal.** 3.2 renames HEAD's refusal "The convoy is already out (to %s)" (W:9499) and moves it later than HEAD checks it (second, W:9497). Yet 3.6 promises unchanged refusal texts. `-trade` triggers this refusal (trade.log:189). It is a `LogRedHope` line, so `diff_battery.py` does not see it.
3. **Convoy limits.** With one convoy per rival and the shipped data (only Zarya active), at most one convoy is out until a survey discovery, so `MaxConvoysOut` 3 never binds. Q-B should say so. The one-per-rival rule is also what keeps `-trade`'s double-dispatch check (C:1083-1086) inert, and the doc should say that too.
4. **Meridian's Food.** The new draw applies E3's life-support floor (X:104-113) to Meridian's standing order, which sells `Food:60`. It can now be refused where HEAD accepts it, and the doc does not mention it.
5. **Goodwill.** The 300 cr goodwill floor can stop today's TRADE from warming relations at all: with both slots at the floor, its value is 237.5 cr.
6. **Partial fills.** `=== CONVOY RETURN ... delivered %s` prints `Rival->ExportLot` (W:9820), which is wrong for a partial fill. Specify what it prints when the fill is short.
7. **Lost block.** A lost market block that rebuilds the v17 convoy as a barter can deliver Zarya's `ExportLot` for a convoy that carried a different order.
8. **Citation.** Section 6 cites "E2 risk 7" for the history-sized block, but E2 risk 7 (e2 design:299) is about blocks sitting after teardown.
9. **The budget bar.** The deck's budget bar (K:529-530) still shows the balance, not the spendable amount.
10. **UI scope.** Section 8's UI (draft orders, a multi-convoy strip and visualizer, console commands) is large for a sim gate whose panel belongs to E8. Consider trimming it.

---

# REVISION

This section replaces the design wherever they differ. The builder's order of authority:
1. the director's answers;
2. this REVISION;
3. the PROFESSOR REVIEW;
4. the design.

Anchors are at HEAD `ea9a5cd`. Rebase onto E3b when it lands. Every number below marked "modelled" was recomputed with a small integer model of the update in this revision's own rules, not taken from the first draft.

**R1. Which rivals have an economy, and when it is built** (required change 1, and part of 7). This replaces 1.2's "live" rule.

- **"Has an economy" is a property of the row**, read by one const helper. A row has an economy when all of these hold:
  - its `StoreCap` is non-empty;
  - every list parses;
  - every key has an E1 slot with `EarthValue_mcr > 0` (R2);
  - every `Produce`, `Consume` and `StoreStart` key is in `StoreCap`;
  - **both standing lots name only resources in its `StoreCap`.**
- **A row without an economy trades by barter, as today.** There are two cases:
  - a row that fails only the last test is legal, and silent;
  - a row that fails any other test is malformed. The shipped table logs one Warning per malformed row at world start, never at runtime, so an injected test rival adds no line. MX0 fails on any malformed shipped row.
- **Greed.** `-trade`'s Greed (C:1116) is a copy of Zarya's row with `ImportLot` `SpareParts:4`. Zarya's `StoreCap` has no SpareParts, so Greed fails the lots test and stays a barter. Its refusal is HEAD's, with no fixture edit.
- **Every economy rival runs from sol 0, whether or not it is available.** Its stores produce and consume, and it counts toward its region's prices, whether or not the colony has found it.
  - Availability (`IsRivalAvailable`, W:9878) gates only booking.
  - Discovery is driven only in the agent band (C:1468-1470). A market that read it would differ between the bands; this one does not.
- **`SyncRivalEconomies()`** builds and refreshes `RivalEconomies` from the rows. It runs:
  - at world start;
  - after a load reads the block;
  - at the top of `RollMarket`;
  - before a save writes the block.
- **What a sync does:**
  - A rival with an economy but no entry gets one at its `StoreStart`. That covers an injected rival, and a data rival missing from a loaded block.
  - An existing entry refreshes `ProduceG`, `ConsumeG` and `CapG` from its row, and keeps its store.
  - An entry whose row has lost its economy stops producing and leaves the sums. Only `RH.InjectRivals` can cause this, by re-injecting a row. The entry is dropped once no uncleared priced convoy names it.
  - The list stays sorted by name string.
- **Save, load, save.** Because the sync runs before every save, the sequence is byte-identical with an injected rival (MX9).
- **The cheat.** `RH.InjectRivals` (RHCheats.cpp:968-987) copies the new columns, so it keeps "mirroring RH_Rivals.csv".
- **Wording that follows from this.**
  - 2.1: there is one market per region with an economy rival. A slot is traded in a region while an economy rival there lists it.
  - 3.2 step 3: each line's resource must be one the **target** rival lists in its `StoreCap`.
- **The switch (4.1), restated.** `IsMarketLive()` = `bMarketEnabled` **and** at least one rival with an economy.
  - With no rival active, the market runs silently on the rivals' data, and no order can be booked (`No trade contact`).
  - Nothing outside the market reads it, so the brief's bar holds (MX1 (b), R8).
- **The tests' rivals.**
  - MX6 injects two rivals with economy columns (`SliceActive` TRUE) for "three convoys out".
  - MX7's barter rival is injected with the economy columns empty.
  - `-scale`'s 8 rivals are injected with economy columns and trade every slot whose Earth value is above 0.

**R2. Data guards and bounds** (required change 2).

- **No zero Earth value.** Every `StoreCap` key must have `EarthValue_mcr > 0`; otherwise the row is malformed (R1). Compost has 0 (EC0's pin, `RHSimEconomySuite.cpp:460-463`). So V > 0 wherever the update divides by it.
- **Row bounds.** `Produce` and `Consume` are at most 1,000,000 kg a sol. `StoreStart` ≤ `StoreCap` ≤ 10,000,000 kg. Outside these, the row is malformed.
- **Config.** `MarketMinVolumeG` is read as `max(1, row)`. Each other market row is clamped to its sane range on read: `MarketWindowSols` 1..8, `MarketMaxStepBp` 1..10000, and `MarketFloorBp` < 10000 < `MarketCeilBp`. MX0 pins the CSV values.
- **Overflow, restated** (N is the number of rivals in a region):
  - `Prod` and `Cons` are each at most N x 10^9 g a sol;
  - one sol's tally is at most `MaxConvoysOut x ConvoyCargoKg` = 3 x 10^6 g each way;
  - so `(Supply - Demand) x 10000` stays below N x 6 x 10^13;
  - `min(sum StoreG, sum CapG) x 10000` stays below N x 10^14 (the `min` covers a store above its cap);
  - `(Price - V) x 10000` stays below 3 x 2^31 x 10^4, about 6.4 x 10^13.
  - All of it fits in int64 for any N under 90,000.

**R3. Money** (required change 3).

- **(a) The spendable readers.** Three checks read `GetSpendableMcr()` instead of the balance:
  - `CanAddManifestItem` (W:13423);
  - `CanLaunchShip` (X:277);
  - **`LaunchShipShort` (X:384).**

  The deck's Credits bar shows spendable credit too (R11, O9).
- **(b) The price at clearing is never more than one step from the quote.** Let O be the market's open sol (`MarketCheckedSol`).
  - **Catching up.** At booking, if `ClearSol > O + 1`, the order's worker first calls `RollMarket(ClearSol)`. That closes the skipped sols exactly as `StepCityGrowth` would later in the same step, and logs nothing. Two things can cause this: a clock jump (`Debug_SetSimSeconds`, C:1098, with the 45 s order lag inside one 60 s era step), or a frame longer than a sol.
  - **So every booking sees `ClearSol - O <= 1`.** The clearing price is the quote, or one step from it, and 3.2's one-step `Hi` / `Lo` reservation covers the net.
  - **A late booking** (`ClearSol < O`) clears at the next roll, at O's price, which is the quote.
  - **Why the catch-up does not run at `ClearSol = O + 1`.** On the boundary step, the queue can run out of `ExecuteAt` order, because `OrderLagSeconds` falls with each tier (45, 20, 8 s), so a later command can come due earlier. A catch-up there would close a sol before an order due in it was booked, in one band and not the other. Without it, every order due by the boundary is booked before that step's roll in both bands.
- **(c) `CreditSpend(Mcr)` never takes the balance below 0.**
  - It pays `min(Mcr, Balance)`, adds that to the spent total, and returns the unpaid rest.
  - `FinishLaunch`'s checks guarantee there is no rest.
  - At a clearing, a rest can only follow a load repair. It logs one Warning, `Order #%d to %s: %s cr could not be paid; %s writes it off`, and the rival takes the loss.
  - E2's bars hold: the balance is never negative (CR7), and `Balance == Awarded - Spent`.
- **(d) The reservation invariant.** `CreditReservedMcr` always equals the sum of the uncleared convoys' `ReservedMcr`, and is at most the balance.
  - Booking reserves at most what is spendable.
  - Every debit (manifest, launch, short launch) checks spendable credit.
  - A clearing pays its Net, which is at most its R, and releases exactly its own R.
  - So no clearing has a rest in play (MX6 asserts this).
- **The load repair (replaces section 6's).** The counter is recomputed as that sum. If the sum exceeds the balance, reservations are cut, starting from the highest-Id convoy, until it fits.

**R4. The clearing catches every due order** (required change 4).

- **The rule (replaces step 1 of 2.2 and the opening of 3.4).** Clear every uncleared priced convoy whose `ClearSol` is **at or before** the sol being closed, by Id, at the prices in force during the sol being closed.
- **A long jump.** It reprices at most 8 sols (the ring's length), but the clearing catches every due convoy, whatever its `ClearSol`.
- **The market off.** When `IsMarketLive()` is false, `RollMarket` still advances `MarketCheckedSol` and runs the clearing in full: fills, stores, money, tally and record. It skips only production and repricing.
  - So a priced convoy that is out when the harness turns the market off still clears, at the last prices.
  - With no economy rival at all, no priced convoy can exist.
- **The checked sol.** At a fresh start, or after a lost block, `MarketCheckedSol` = `ProductionCheckedSol`, as E1 seeds `EconCheckedSol` (E:35, :245). It is never 0 against a loaded clock.
- **Loads.** A loaded convoy whose `ClearSol` has passed needs no repair: the next roll clears it. A convoy held at the end of its last leg (3.5) is held at most until the next sol threshold.

**R5. The update, corrected, and the worked examples recomputed** (required change 5). This replaces 2.2's update and its worked examples.

```
W            = MarketWindowSols                                              // 5; 1 .. MarketRing
Prod, Cons   = sum over the region's economy rivals (name order) of ProduceG, ConsumeG
Sold, Bought = sum of the tally's last W completed sols; 0 and 0 when MarketRivalOnly
Supply       = Prod * W + Sold
Demand       = Cons * W + Bought
FlowBp       = DivTrunc((Supply - Demand) * 10000, max(Supply + Demand, MarketMinVolumeG))  // -10000 .. 10000
FillBp       = min(sum StoreG, sum CapG) * 10000 / max(sum CapG, 1)                       // 0 .. 10000
StoreBp      = MarketRivalOnly ? 0 : clamp((FillBp - MarketTargetFillBp) * 2, -10000, 10000)
AnchorBp     = DivTrunc((Price - V) * 10000, V)                                           // -7500 .. +30000 inside the band
Pressure     = DivTrunc(FlowBp * WFlow + StoreBp * WStore + AnchorBp * WAnchor, 100)
StepBp       = clamp(DivTrunc(-Pressure * MarketGainPct, 100), -MarketMaxStepBp, +MarketMaxStepBp)
Delta        = StepBp == 0 ? 0 : sign(StepBp) * max(1, |Price * StepBp| / 10000)
Price        = clamp(Price + Delta, V * MarketFloorBp / 10000, V * MarketCeilBp / 10000)
```

- **Signed division.** Four divisions have signed numerators: flow, anchor, pressure and step. All four go through one helper, `DivTrunc(a, b)` with b > 0, which truncates toward zero. MX2 pins its negative cases.
- **The anchor is no longer clamped to ±10000.** Inside the band it runs from -7500 to +30000.
  - With the old clamp, a full shortage could not be balanced above 2 V. Modelled: a Struct shortage with the colony buying 1,000 kg a sol climbs to the 4 V ceiling with the clamp, and settles at 2.53 V without it.
  - Now the highest equilibrium any shortage can reach is 2.6 V (flow and store both at -10000 need an anchor of +16000). The ceiling is a safety bound.
  - A glut whose flow and store terms together exceed +3750 sits at the floor. That is a price at a bound with a cause.
- **Ice at Zarya, no trade** (modelled).
  - Flow is +4545 (Supply 2,000,000 g, Demand 750,000 g). Production runs before the reprice, so the first store term is +2250 (12,250 of 20,000 kg).
  - The 5% clamp binds on sols 1-3 only. The step is -492 bp on sol 4, -381 by sol 10, and about -335 to -359 through sol 32, while the store fills.
  - Ice reaches the floor, 0.75 cr/kg, at **sol 37**, and stays there: the store is full from sol 32, and the pressure at the floor is +1522.
- **Struct at Zarya, no trade** (modelled).
  - Supply + Demand is 800,000 g, below `MarketMinVolumeG`, so Flow is **-4000**.
  - The store term starts at -3950 (2,420 kg after sol 1) and reaches -10000 when the store empties at sol 32.
  - Struct climbs 5% a sol for 4 sols, then more slowly: 7.54 cr at sol 10, 9.29 cr at sol 25.
  - It settles at **2.0 V (10.0 cr/kg)**, where `-4000 x 50 - 10000 x 30 + A x 50 = 0` gives A = +10000. At 9.996 cr the pressure is -4 and the step truncates to 0.
- **Section 1.1's stories stand:** Ice gets cheap, and Struct gets dear unless someone sells Zarya some.

**R6. The fallback: `MarketRivalOnly`** (required change 6). This replaces section 5's fallback paragraph. `MarketColonyFlowPct` is removed.

- **The switch.** A new `RH_Config` row, `MarketRivalOnly`, 0 (the C++ fallback is also 0), and the harness hook `Debug_SetMarketRivalOnly(bool)`.
- **What it does.** When it is 1, `Sold` and `Bought` leave `Supply` and `Demand`, and `StoreBp` is 0.
  - The price is then a function of the rivals' data, the Earth value and the sol count alone. No order, fill or store can touch it, in either band.
  - That is the plan's "rival-side numbers only, which the agent band cannot touch" (economy-plan.md:82).
- **Its equilibria** at the placeholder data (modelled): Ice 0.546 V (1.64 cr, reached at sol 92), and Struct 1.4 V (7.00 cr, at sol 48).
- **What it leaves alone.** The colony's fills still move the real stores, and buys still fill only up to them.
- **The rule if the bands diverge.** The builder reports both runs to the director before choosing. The bar is not loosened.
- **The test.** MX8's margin case shows the main twin diverging while the fallback does not (R7).

**R7. Era parity, restated** (required change 7). This replaces section 5's "what may differ" and MX8.

- **What may differ between the bands.** All of these are colony-side:
  - the step at which a convoy comes home, its goods land and its owed credit is paid;
  - whether "a convoy to this rival is already out" at a booking, since the return step differs;
  - the step that books an order, and so its quote and its reservation.
    - The era band can book on the threshold step, before the roll.
    - **In `Tick`, so can the agent band:** `StepUplink` reads the frame end (`Clock->GetSimSecondsTotal()`, W:1067), while the roll reads each sub-step's own end (W:630-636). Section 5's "about 100 sub-steps after the roll" holds only in the commandlet.
    - The clearing is keyed by `ExecuteAt`, so the market never sees which step booked.
  - whether an order is accepted at all, at the edge of the colony's holdings or credit;
  - which rivals the colony has found (surveys run only in the agent band). Under R1 this changes what can be ordered, never a price or a store.
- **Given the same accepted orders, the market is identical.**
- **MX8, restated.**
  - **Rivals.** Zarya, plus an injected economy rival, `Kestrel`: `SliceActive`, 30 km away (a 1-sol round trip), producing Ice 100 kg a sol and consuming none. MX8 drives no surveys, and asserts that the discovered set is equal in both twins at the end.
  - **The schedule.** Orders to Kestrel on sols 2, 4, 7, 9, 12 and 14, and to Zarya on sols 1, 6 and 11, all at sol fraction 0.5. In both bands:
    - every convoy is home at least a sol before the next order to the same rival;
    - every order's reservation is at most half the spendable credit.
  - **The boundary fixture.**
    - At sol 15.5, `Debug_SetRivalStore(Kestrel, Ice, 50)`.
    - An order to buy 120 Ice from Kestrel has its `ExecuteAt` 10 s after the sol-16 boundary. The era twin's steps are placed so that the boundary step books it before the roll.
    - The roll at that boundary adds Kestrel's 100 kg. The order clears at the end of sol 16, against 150 kg, and fills 120 in both bands.
  - **The bar, for the main and the fallback twins.** At every threshold, every traded price, every rival store, every fill, every clearing sol and every charge are identical (int64 equality). The sols in which convoys come home, and the end balances, are equal.
  - **The margin case** (a third pair of twins).
    - A boundary order buys a resource whose price rises a full step at that roll.
    - The colony's spendable credit is set (`Debug_ZeroCredits`, then `Debug_GrantCredits`) between the era twin's reservation, quoted before the roll, and the agent twin's, quoted after it. So one twin books the order and the other refuses it.
    - The suite asserts two things: the main twins' prices then differ (the case bites), and the fallback twins' prices stay identical.

**R8. Tests and mutations, restated** (required change 8, and the fixtures of required change 5).

- **Hooks:**
  - `Debug_SetMarketColonyFlowPct` becomes `Debug_SetMarketRivalOnly(bool)`;
  - new: `Debug_SetMarketTally(Res, SoldKgPerSol, BoughtKgPerSol)`, which fills every ring entry in the window.
- **MX0 adds:**
  - every `StoreCap` key has V > 0;
  - R2's bounds hold, and `MarketMinVolumeG` ≥ 1;
  - an injected row naming Compost is malformed, trades by barter, and logs nothing at runtime;
  - a copy of Zarya's row with SpareParts lots (Greed's shape) trades by barter.
- **MX1 (b), restated.** With every rival inactive, the market runs, no order can be booked (`No trade contact`), and the digest equals the market-off twin's.
- **MX2 adds:**
  - `DivTrunc`'s negative cases (for example, -7 / 2 = -3);
  - an anchor beyond ±10000.
- **MX3, the glut.**
  - Zarya's Ice store is at its cap, and `Debug_SetMarketTally(Ice, 1000, 0)` runs before each roll.
  - Modelled: Flow is +8064, the clamp binds every sol, and Ice falls exactly 5% a sol (truncated) to the floor at sol 28.
  - Then one real order sells 1,000 kg past Zarya's room. It is taken in full, and the store stands above its cap. Production adds nothing until consumption brings the store back under, and not a gram is deleted.
- **MX4, the shortage.**
  - Zarya's Struct store is at 0, and `Debug_SetMarketTally(Struct, 0, 1000)` runs before each roll.
  - Modelled: Flow is -9310. Struct rises 5% a sol for 15 sols and settles at 2.53 V (the suite holds the exact integer), never at the ceiling.
  - A real buy above the store fills to the store, and only the fill is charged.
- **MX6 adds:**
  - a short launch is refused when only reserved credit would pay for it;
  - a 5-sol clock jump before a booking triggers the catch-up: the order books at the caught-up quote, and nothing is logged;
  - every clearing's charge is at most its reservation, and `CreditSpend` has no rest;
  - three convoys are out to three economy rivals (R1).
- **MX7 adds:**
  - the barter rival is injected with empty economy columns;
  - the goodwill floor applies to a written order but not to the standing order (R11);
  - a short standing-order fill prints the kg that landed (R11).
- **MX9 adds:**
  - a loaded convoy with a past `ClearSol` clears at the next roll;
  - a lost block seeds `MarketCheckedSol` from `ProductionCheckedSol`;
  - the over-reserved repair cuts reservations from the highest Id down;
  - a forced `CreditSpend` rest logs its Warning, and the balance never goes below 0;
  - a lost block brings the v17 convoy home empty (R11);
  - a data rival missing from the block starts at `StoreStart`;
  - save, load, save is byte-identical with an injected rival.
- **Mutations (MX12), restated.** Each must fail the check named:
  1. a `double` in the update (MX2, MX5);
  2. no step clamp (MX3: the glut falls faster than 5%);
  3. `RivalEconomies` built in table (FName) order (MX2's saved bytes, MX9's ascending names);
  4. the clearing sol taken as the market's open sol at booking (MX8's boundary fixture: the era twin clears at the sol-15 roll, against 50 kg);
  5. the rival's store read at booking, capping the buy (MX8's boundary fixture: 50 kg in the era twin, 120 kg in the agent twin);
  6. no reservation (MX6);
  7. the reservation without its one-step margin (MX6: a full step between booking and clearing makes the charge exceed the reservation);
  8. clearing on `ClearSol ==` instead of `<=` (MX9's past clearing sol, and MX6's jump);
  9. `CreditSpend` without its floor (MX9: the balance goes negative);
  10. `LaunchShipShort` reading the balance (MX6);
  11. the anchor clamped to ±10000 (MX4: the shortage reaches the ceiling);
  12. the colony's E1 production fed into `Supply` (MX8: E1 is band-dependent within 5%);
  13. the fallback leaving the store term on (MX8's margin case: the fallback twins' prices differ).
- **Removed:** "a buy capped by the order's money". The reservation covers the whole buy, so that cap can never bind.

**R9. A stated deviation: the market's own windowed ledger, not E1's** (required change 9).

- **What the brief and plan ask for.** The brief says "repriced ... from the ledger windows", and the plan says "supply and demand in the ledgers" (economy-plan.md:41).
- **What this design does instead.** It does not read E1. It reprices from:
  - the market's own windowed ledger: the tally of the colony's filled trades with the rivals, kept E1's way, as a ring of completed sols in whole grams;
  - plus the rivals' data flows and store fill.
- **The reasons, from HEAD:**
  - E1's Out leg includes the Earth launch (X:350), and its In leg includes ship landings, so E1 cannot tell Martian trade from Earth trade;
  - E1's Made and Used legs, and the In leg of a convoy home, are written at band-dependent steps. E1's own bar between the bands is 5%, not identity.
- **The consequence.** The parity bar tightens from the plan's "within a tolerance" (economy-plan.md:82) to identical. Reading E1 would bring back a tolerance; R8's mutation 12 guards against it.
- **E1 still has no reader** after E4. E8's market panel would be the first.

**R10. What the battery moves** (required change 10). This replaces section 7's last paragraph.

- **What moves:**
  - the save-version lines;
  - the masked `(N bytes` lines;
  - **four lines that print whole-file sizes:** CV8's `(8951 -> 9031 bytes)`, DP11's `(8979 -> 9011 bytes)`, and EC1's two `both saves are 9992 bytes` lines (era and agent). They moved in E1, E2b and E3 for the same reason;
  - **XE10's first line.** The export block is no longer last, so it reads `XE10 v%u: the export block is the %d bytes before the market block - ...`;
  - the new `market.log`.
- **What does not move:**
  - CR9 (`the credit block is the 72 bytes before E3's export block`) and EC6 (`the 5997 bytes before the 585 bytes of the credit and export blocks`) keep their texts. Each subtracts `GetMarketBlockBytes()` separately, and never folds it into the number it prints.
  - EC1's trailing comparison now spans the market block too. Both twins run the same market, so its text stays.
  - Every convoy line stays (4.2).
- **The brief's literal bar cannot hold.** "Except the save-version lines" fails for any new save block, because these five lines print sizes and positions. The builder reports them by name.

**R11. The observations, taken.**

- **O2. The "already out" refusal.** The check stays second, where HEAD has it (W:9497-9500), with HEAD's text, `The convoy is already out (to %s)`, naming the rival whose convoy is out. The new refusal, `%d convoys are already out` (`MaxConvoysOut`), comes straight after it. 3.2's steps 1 and 2 are reordered to match.
- **O3. Q-B.** It now says that the shipped data allows at most two convoys out (Zarya, and Meridian once found), so the limit of 3 binds only with a third rival. One convoy per rival is what keeps `-trade`'s double-dispatch check (C:1083-1086) inert.
- **O4. The life-support floor.** The standing order now honours E3's life-support floor for Food, Water and Oxygen (X:104-113).
  - Zarya's lot is Struct, so no battery line moves.
  - Meridian's `Food:60` can be refused where HEAD sent it. That is deliberate: no convoy takes the colony's last food. HEAD's refusal text names the short resource.
- **O5. Goodwill.** `GoodwillMinValueMcr` applies to written orders only. The standing order keeps today's goodwill rule unconditionally, since its value with both slots at the floor is 237.5 cr.
- **O6. The return line.** It prints the filled lines.
  - For a standing order filled in full, it prints `ExportLot` verbatim (W:9818-9820), so the line is byte-identical.
  - A short fill prints the kg that landed, in the same `Res:kg` form.
- **O7. A lost block.** It rebuilds the v17 convoy as an **empty** convoy, which comes home with nothing. Its Warning says the order was lost. Nothing is delivered that was not bought.
- **O8. The citation.** Section 6's "E2 risk 7" citation is struck.
- **O9. The budget bar.** The deck's Credits bar (K:529-530) shows spendable credit, with the reserved amount in its tooltip.
- **O10. Section 8, trimmed.**
  - The rival card gets price rows (no step arrow), and the draft order with SEND ORDER.
  - The convoy strip lists every convoy, and the visualizer draws each one.
  - Console: `RH.Market` and `RH.Market order`. `RH.Trade` is unchanged.
  - Price history and the full panel are E8's.

**The director questions, restated** (they replace the ones above):

**Q-A. What happens to today's fixed trade with a neighbor (your 100 Struct for Zarya's 150 Ice)?**
- **(a) (recommended):** It stays, as that neighbor's standing trade, with the same goods each way, now settled at market prices in credits.
  - When your side is worth more, the difference comes home with the convoy. When it is worth less, you pay the difference when the market settles at the end of that sol.
  - At today's reference prices, Zarya's trade pays you 50 credits a run.
  - It keeps today's goodwill. You can also send orders of your own.
- (b) It stays a straight swap, goods for goods with no credits, beside the new priced orders.
- (c) It goes. Every convoy carries an order you write.

**Q-B. Do the cargo rovers drive the convoys now?**
- **(a) (recommended):** Not yet. A convoy stays an abstract trip with a timer.
  - At most one convoy is out to each neighbor, and 3 in all.
  - With today's map that means at most two: Zarya, and Meridian once a survey finds it.
  - Real rovers on real routes come with the regional map (T6).
- (b) Yes. Each convoy needs a cargo rover, which leaves the colony while the convoy is out, so a rover you send trading is not hauling at home.

**Q-C. May the colony buy cheap on the Martian market and sell to Earth?**
- **(a) (recommended):** Yes. A trading colony is a legitimate path. It is limited by:
  - what the neighbors hold;
  - the 1,000 kg each convoy carries;
  - prices that climb up to 5% a sol while you buy;
  - the ship's 30,000 kg.
- (b) No. A Martian price never falls below what Earth pays for the same resource, so a glut on Mars is never cheaper than Earth's offer.

**The verdict after revision.** The review's verdict on the design as first written was **BLOCKED**. R1-R10 write in all ten required changes, and R11 takes observations 2-10. **This revision has not been re-reviewed.** R1, R3, R4 and R7 change the economy rule, the money argument and the parity argument, so a second adversarial pass on them is advisable before the build.

The conditions left:
- the director's answers to Q-A, Q-B and Q-C;
- E3b landed, which fixes the save version (v54 if it adds none);
- the builder's per-fixture check that the standing order's new draw moves no convoy line (risk 5).

---

# PROFESSOR RE-REVIEW (2026-10-24)

An independent adversarial re-review of the REVISION, read-only. The code was read with `git show HEAD:`. HEAD moved from `0ac908a` to `b5f5863` (E3b) during the review. E3b changes none of W, X or E, and it adds no save version, so E4 is **v54**. In G, `RollEconomyLedger` is now at G:488 (it was at 486-487). Every number checked below was recomputed with an independent integer model of R5's update; none was copied from the REVISION.

Verdict: **CLEARED WITH CONDITIONS**

The core holds:
- Given the same accepted orders, the market is identical in both bands.
- The reservation covers every clearing in play.
- Nothing is ordered by FName index.
- All ten required changes are in, and they are correct.

What remains:
- two director questions whose consequences are stated wrongly;
- one double commitment with E3;
- four specification defects that a builder would otherwise find one build at a time.

## The ten required changes, checked

1. **Injected rivals and Greed: fixed.**
   - Greed is Zarya's row with `ImportLot` `SpareParts:4` (C:1116-1117). SpareParts is not in Zarya's `StoreCap`, so Greed fails R1's lots test and trades by barter, with HEAD's refusal.
   - Meridian's lots (`SpareParts:4` and `Food:60`, RH_Rivals.csv:3) are inside its `StoreCap`, so Meridian keeps an economy.
   - The sync covers both an injection and a load.
2. **Division by zero: fixed.**
   - Compost has V = 0 (RH_Resources.csv:20) and is now a malformed key.
   - Every divisor is now one of: V > 0, `max(S + D, MinVol >= 1)`, or `max(Cap, 1)`.
   - R2's overflow bounds check out.
3. **Money: fixed.**
   - X:384 is the third balance reader, beside X:277 and W:13423.
   - With `ClearSol - O <= 1`, the clearing price is at most `q + max(1, floor(q*s/10000))`, which is at most `Hi(q)`. The same holds the other way for `Lo(q)`, so R covers the Net.
   - `StepUplink` runs due commands in queue order (W:1065-1080), so R3(b)'s reason for not catching up at O + 1 is correct.
   - The catch-up passes the same `SolNow` as the other band's `StepCityGrowth`, so it cannot split the bands.
4. **Stranded convoys: fixed.**
   - The clearing rule is now `<=`, the clearing runs with the market off, and the checked sol is seeded as E:245 seeds E1's.
   - `StepCityGrowth` runs before `StepTrade` in both spines (W:683/685 and W:855/857), so a due convoy always clears before it can hold.
5. **The worked examples: all reproduce exactly.**
   - Ice: the clamp binds on sols 1-3; the step is -492 at sol 4 and -381 at sol 10; the store is full at sol 32; the floor comes at sol 37, with a pressure of +1522 there.
   - Struct: flow -4000, store -3950, 7.54 cr at sol 10, 9.29 cr at sol 25, settling at 9.996 cr.
   - The fallback: Ice 1.639 cr at sol 92, Struct 6.996 cr at sol 48.
   - MX3: flow +8064, the clamp binds every sol, and Ice reaches the floor at sol 28.
   - MX4: flow -9310, 15 full steps, settling at 2.53 V.
6. **The fallback: fixed.** With `MarketRivalOnly`, the price depends only on the data and the sol count.
7. **Parity: fixed.** No other band-dependent input to `RollMarket` was found.
8. **The mutations: fixed.** Each one now has a fixture that can bite. Mutation 8 is caught by MX9's past `ClearSol`. MX6's 5-sol jump would not catch it, because that roll walks every sol.
9. **The deviation from E1: fixed.**
   - The tally is the market's only colony-side input.
   - E1 is read by nothing, so nothing is counted twice against E1 or against E3's money.
   - (Condition 2 is a double commitment of goods, not of money.)
10. **The battery list: fixed and complete.**
    - The only unmasked size or position lines in `Saved/baselines/battery_post_e3` and `_e3b` are CV8, DP11, EC1 (twice) and XE10.
    - `diff_battery.py` masks only `\(\d+ bytes`.

**Inertness holds:**
- `-trade` was walked step by step, including its storm jump. The catch-up closes sols 4-11 in one call, and the jump to sol 16 clears `ClearSol` 12 before `StepTrade` runs.
- `cr carried` and `Earth credit` appear 0 times in the logs of the six convoy suites.
- The playtest bot never orders a convoy (P:546-556).

## REQUIRED CONDITIONS

1. **Restate the consequences of Q-A and Q-C.**
   - **Q-A (a).** "50 credits a run" is true on sol 0 only. Under R5's own rules, with TRADE pressed every round trip:
     - Ice falls to its floor and Struct climbs toward 2 V, so a run pays about 566 cr by sol 20, 761 cr by sol 40, and 827 cr from sol 60 on;
     - that is about 19,600 cr over 110 sols (about 15,600 cr at one run every 5 sols), with no decision by the player;
     - for scale, a ColonistTransport costs 8,000 cr, and the quota award is 1 cr a kg.

     The director must be told that (a) makes TRADE a main source of income.
   - **Q-C: what Earth pays.** Earth pays V x M - 3 cr/kg (X:165-171; RH_Config:177-178), with M at most 1.4 (W:10052-10067; RH_Config:100). At M = 1.0:
     - Ice nets 0, so `AddExportLine` refuses it (X:201-206);
     - Water, Struct and Oxygen are worth more on Mars than Earth pays.
   - **Q-C: when arbitrage opens.** With the placeholder data, only an Earth-aligned colony can arbitrage:
     - Ice and Hydrogen, when M > 1.25;
     - Oxygen, when M > 1.375.

     The margins are small: Ice at most 0.45 cr/kg; Oxygen at most 0.2 cr/kg; Hydrogen at most 1.8 cr/kg, and only until about sol 7, when Zarya's shortage lifts its price.
   - **Q-C: the gap that is large** runs the other way: Mars pays about 10 cr for Struct (R5), and Earth pays 2 cr. Q-C should say so.
   - **Q-C (b) needs its cost stated.** A floor at Earth's price makes the market read `GetRequisitionMultiplier`, which depends on IdentityAxis and EarthTension, both colony-side political state.
     - That breaks R7's "the market reads only the rivals' data and accepted orders", and section 2.4.
     - (b) would need M fixed once a sol at the threshold, and its own parity proof.
2. **Sells must not take what is promised to Earth.**
   - Booking (3.2 step 4) checks each sell line against `GetLaunchLoadableKg` only (X:115-118).
   - E3 protects the open bill and the listed sales: `Spare = Loadable - BillLine - Listed` (X:217), and a launch needs `Held - Floor >= Quota + Sale` (X:303-304).
   - So a `MarketOrder` sell booked while a manifest is open can sell kilograms already promised to Earth. A launch that was valid then becomes "Earth's cargo is short", which means a short launch, arrears, and dropped export lines.
   - **Change:** while `AwaitingManifest`, check written-order sells against E3's Spare. Keep the standing order on HEAD's rule, or show that no battery line moves.
3. **The sync: its cost and its measurement.**
   - R1 runs `SyncRivalEconomies` at the top of `RollMarket`, which `StepCityGrowth` calls every step (beside G:488).
   - Each sync re-parses four string lists per row through `ParseResourceList` (RHDefinitionsSubsystem.cpp:459-472), with heap splits and FName lookups.
   - `-scale` allows 2 ms for 1,000 rolls with 8 rivals, about 2 µs a roll. Re-parsing on every roll cannot meet that.
   - The const `GetMarketBlockBytes()` cannot sync either. So right after an injection it measures a different state from the one the save writes.
   - **Change:**
     - cache the parsed economies;
     - re-sync only at world start, at load, and when a rivals-table generation counter changes (bumped by `Debug_InjectRival` and `RH.InjectRivals`);
     - make `GetMarketBlockBytes()` measure the synced state.
4. **MX8's schedule.**
   - Storm_1 runs from sol 12.0 to 15.0 (RH_Events.csv:2), and a dust storm freezes every convoy (W:9745).
   - The design does not name MX8's start save. If it starts at sol 0:
     - Zarya's sol-11 convoy and Kestrel's sol-12 convoy are frozen;
     - Kestrel's sol-14 order is refused in both twins;
     - Kestrel's sol-12 convoy comes home at about sol 16.0, on the boundary step itself. The era twin books there (W:844) before `StepTrade` lands that convoy (W:857). The agent twin books 10 s after it lands. So one twin refuses "already out", and MX8 fails because of its fixture.
   - Even with no storm, the boundary order (sol 16.0 + 10 s) is booked about 0.47 sol after Kestrel's sol-14 convoy comes home (about sol 15.54). That breaks the schedule's own rule, "home at least a sol before the next order".
   - **Change:**
     - name the start save;
     - run MX8 in a 20-sol window with no storm, and assert it;
     - drop Kestrel's sol-14 order, or move the boundary to sol 17 or later.
5. **The money invariants under the harness and the repair.**
   - (a) `Debug_ZeroCredits` (H:1458) zeroes the balance, the awarded and spent totals and `SalesPaidMcr`, but not `MarketSalesMcr` or `CreditReservedMcr`.
     - That breaks `SalesPaid + MarketSales <= Awarded` and `Reserved <= Balance`, and R7's margin case calls it.
     - **Change:** it zeroes `MarketSalesMcr` too, and refuses (or asserts) while anything is reserved.
   - (b) After R3's repair cuts a convoy's reservation, its clearing can still charge its full Net.
     - `CreditSpend` then spends credit set aside for other convoys, which leaves `Reserved > Balance`. MX6 asserts that invariant after every step.
     - **Change:** a clearing pays at most its own R plus spendable credit, and writes off the rest with R3(c)'s Warning.
6. **A strict parser for the economy columns.**
   - `ParseResourceList` (RHDefinitionsSubsystem.cpp:459-472) handles bad input silently:
     - it skips an entry with no `:`;
     - when a key repeats, the last value wins;
     - it reads `Ice:x` as 0.
   - So R1's "every list parses" cannot be checked through it.
   - **Change:** a strict parser, as `ParseOrderLines` is for orders, with these three cases added to MX0.

## OBSERVATIONS (non-blocking)

1. **The barter convoy's record.** Say how a barter convoy (Greed, or the market off) is recorded in `Convoys`, so that R4's "uncleared priced" rule and the hold skip it. Also say whether its return still reads `ExportLot` at the return (W:9768) or the lines booked at departure.
2. **The transit charge.** `TakeGrowthResource` charges D4's shaft ledger (`ChargeBillPart`, G:293). So with a lift car the standing order does more than "only add credits". No convoy fixture has a lift car; add this to risk 5.
3. **A backward clock jump.** A backward `Debug_SetSimSeconds` (as at C:1383-1385) holds a priced convoy until the clock passes `MarketCheckedSol` again. R4's "at most until the next sol threshold" assumes the clock only moves forward. No suite sends a convoy after such a jump.
4. **Mutation 12** bites only if the MX8 colony makes a traded resource at different rates in the two bands. The builder must show that it fails.
5. **The REVISION's own conditions.** The director's answers and risk 5's per-fixture check still stand. E3b has landed (`b5f5863`), with no save change.


# DIRECTOR ANSWER (2026-10-24)

Answered after the re-review's corrected consequences were put to the director, and confirmed.
- **Q-A (a):** the standing trade keeps its goods each way, settled at market prices in credits. Modelled: about 830 cr a run from about sol 60, roughly 15,600-19,600 cr over 110 sols.
- **Q-B (a):** convoys stay timed trips: at most one per neighbour and three in all. Cargo rovers drive convoys only with the regional map (step 4).
- **Q-C (a):** buying on Mars and selling to Earth is allowed. It is closed at neutral politics (Earth refuses Ice) and opens with small margins only for an Earth-aligned colony.

The re-review's conditions 2-6 are applied by the builder.
