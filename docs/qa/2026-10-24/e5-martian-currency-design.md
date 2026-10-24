# E5, a Martian currency: design (2026-10-24)

Step 3 (economy), gate E5. **Design only; nothing here is built.** The code was read at HEAD `4edd7fd` (save v53) with `git show HEAD:`. E4 (the Martian market) is designed, re-reviewed (CLEARED WITH CONDITIONS) and answered by the director, but not built. E5 is designed on E4's REVISION as the re-review and the director's answer left it, so every E4 name below (`RollMarket`, `Convoys`, `CreditReservedMcr`, `GetSpendableMcr`, `CreditSpend`, `RivalEconomies`, ...) is E4's design name. The builder rebases onto E4 as built. Line numbers are at HEAD and will move, so every anchor is also named by its function.

Short names used below:
- "W" is `Source/RedHopeSim/Private/RHSimWorldSubsystem.cpp` and "H" is its header.
- "G" is `RHSimGrowth.cpp`, "C" is `RHSimCommandlet.cpp`, "D" is `RHDefinitionsSubsystem.cpp`.
- "K" is `Source/RedHope/Private/RHCommandDeckPanels.cpp`, "P" is `RHPlaytest.cpp`.
- "E4" is `docs/qa/2026-10-24/e4-martian-market-design.md`; "E4 R5" is its REVISION's fifth item.

**Binding rulings:**
- **Q3a (economy plan, 2026-10-20):** the Martian currency appears later, as a **political act**. The colony issues it once the identity axis leans Martian, or as part of a Defy choice. Earth reads it as a step away, and tension rises. Rivals can choose to join it, which makes a trade bloc.
- **Q4a and ruling 11:** there is no money inside the city, and Hope is never spent or valued.
- **Q1a, Q2a:** Earth pays in credits and takes the quota. Earth only ever accepts credits.
- **E4's director answer:** the standing trade is settled at market prices (Q-A a); convoys stay timed trips, one per neighbour, three in all (Q-B a); buying on Mars and selling to Earth is allowed (Q-C a).
- **The plan's E5 risks:** the rate is a pure function of saved scalars, updated at the sol threshold; there is no change until the currency is issued; `-solidarity` and the endings suites stay identical.
- **E4's exactness:** E4's prices are identical in the agent and era bands because the market reads only the rivals' data and orders keyed by their `ExecuteAt`. E5 keeps that exactness, and that decides most of this design (section 3.2).

## What E5 builds, in one paragraph

The colony can **issue a Martian currency** (placeholder name: the **mark**) by an uplink order, once its identity axis is at +20 or more, or as a third answer to Earth's demand: Defy, and issue. Earth reacts at once: tension +10 and its requisition multiplier −0.10 for as long as the mark exists. Two sols later the region's market **counts in marks**: E4's prices keep their integers, now read in marks. **Rivals join the bloc** when the colony's identity reaches their data stance, in name order. Bloc members settle in marks only. Rivals outside the bloc still settle in credits, converted at the rate. Earth takes only credits. The colony holds **two balances**, credits and marks, and can convert between them at a **bloc exchange desk**, at the rate, with a spread and a daily cap. **The rate** is one saved integer, in milli-credits per mark. It starts at parity and steps at most 2% a sol, at the sol threshold. Three things move it: the colony's trade balance with the bloc, the identity axis and Earth tension, with a pull back toward parity. It reads tension and identity from a **sol-close political record** keyed by each act's `ExecuteAt`, never the live doubles, so it is identical in both bands. A Comply withdraws the mark (Q-B). Until the mark is issued nothing reads any of it, so the battery and the playtest do not move.

---

## 0. What HEAD gives E5 (politics, read at `4edd7fd`)

- **The identity axis** (`IdentityAxis`, double, −100 Earth .. +100 Martian). It starts at `IdentityAxisStart` (W:454; 0). **Only `ResolveDilemma` writes it in play:** −20 on Comply (W:10488) and +20 on Defy (W:10502). The harness writes it too (`Debug_ShiftIdentity`, H:1637; `Debug_SetRequisitionInputs`, H:1489).
- **Earth tension** (`EarthTension`, double, 0..100). It starts at `EarthTensionStart` (W:453; 0). It drifts +0.6 a sol in `StepEarth` (W:10080) while `HasActiveRival()` (W:9851: any `SliceActive` row, or any discovered rival). It falls 45 on Comply and 12 on Defy (W:10489, :10503) and 20 on Pacify (W:10276).
- **Earth's pressure.**
  - A demand is pending once tension ≥ 60 (W:10084-10090).
  - At ≥ 75 Earth turns the closest available neighbour (highest relation) to an embargo (`StepPreemptive`, W:10220-10252), which hardens into a defection after 8 sols.
  - **The requisition multiplier** (W:10052-10067) is `1 + 0.4·EarthAlign − 0.6·MartianAlign − 0.3·T/100`, clamped to [0, 1.4]. The quota award (E2) and Earth's sale prices (E3: `V × M − 3 cr/kg`) read it.
- **The Dilemma is an uplink verb:** `Solidarity`, Target `Comply` = comply, anything else = defy (W:9566-9575). The deck sends it (K:2251), and so does `RH.Solidarity` (RHCheats.cpp:925).
- **A Comply closes the route of every `SliceActive` row** (`ForEachRival`, W:10475; D:496-510 visits active rows only). A dormant rival the colony has discovered (Meridian) keeps its route. Defy's +5 relations walks the same active rows (W:10507).
- **Influence** is earned by fair trade (W:9810), laundering and Evolved drift (W:10198), and spent only by Pacify (W:10274).
- **The endings** (W:10430-10444) read identity (±60) and HumanNature (30). `StepEarth` declares the path and the epilogue.
- **Step order in both bands:** `StepUplink` (W:672 / :844) … `StepCityGrowth` (W:683 / :855, where the rolls run, G:486-488) → `StepTrade` (W:685 / :857) → `StepEarth` (W:686 / :858).
- **Why E5 cannot read the live politics.** These live values differ between the bands:
  - **Tension** is a sum of per-step drifts (0.1 s against 60 s steps), so its last bits differ. So does the step that applies an act: an act due 10 s after a sol boundary runs before that boundary's roll in the era band and after it in the agent band (E4 section 5).
  - **Relations** change when a convoy comes home (W:9794-9817), and that step differs between bands (E4 R7).
  - **Discovery** runs only in the agent band.
- **The playtest bot never answers a demand:** `git grep -c "Solidarity\|ResolveDilemma\|Defy" HEAD -- Source/RedHope/Private/RHPlaytest.cpp` finds nothing. In the 110-sol run, tension reaches 60 at sol 100 and the demand stays pending.

---

## 1. Issuing

### 1.1 When it is possible

There are two acts, both uplink orders, so both have an `ExecuteAt`.
- **`Currency` with Target `Issue`:** the stand-alone issue. It needs the live `IdentityAxis` ≥ `CurrencyIssueIdentity` (new config row, 20): "leans Martian". With the data, identity moves only in steps of 20, so this means at least one more Defy than Comply.
- **`Solidarity` with Target `DefyIssue`:** the third answer to a pending demand. It runs `ResolveDilemma(false)` exactly as a Defy does, then issues, at **any** identity: the Defy is the act that permits it. Without a pending demand it is refused with HEAD's text, `There is no demand to answer right now.`

Both also need, in this order (`CanIssueCurrency(FString& OutWhy) const`, one rule for the verb, the deck and the cheat):
1. not already issued: `The mark is already issued`;
2. no withdrawal pending (1.5): `The mark is being withdrawn`;
3. at least one rival with an economy (E4 R1): `No neighbour trades in a market yet`;
4. for `Issue` only, the identity: `The colony does not lean Martian yet (identity %+.0f, needs %+.0f)`.

`DefyIssue` checks 1-3 **before** its Defy runs. If any fails, the whole order is refused and nothing changes; it is never half done.

### 1.2 What Earth does (at the act's execution, live)

- `EarthTension = clamp(EarthTension + CurrencyIssueTension, 0, 100)` (new config row, 10; Q-A).
- `bCurrencyIssued = true`. While it holds, `GetRequisitionMultiplier` subtracts `CurrencyRequisitionPenalty` (new config row, 0.10; Q-A) inside its existing expression, before the clamp, and only inside an `if (bCurrencyIssued)`. So with the mark unissued the function is HEAD's, statement for statement.
- One alert and one log line (Gate-D placeholder copy): `=== CURRENCY ISSUED: the mark (identity %+.0f, tension %.0f, requisition x%.2f) ===`.
- **Not touched:** `IdentityAxis`, `HumanNatureAxis`, Influence, relations, Hope and the endings' read. The Defy half of `DefyIssue` does exactly what a Defy does, including its morale shock and its +5 relations.

**With the data** (modelled, section 3.5): the first demand comes at sol 100.0. A `DefyIssue` at 100.5 leaves tension at 60.3 − 12 + 10 = 58.3, so Earth's next demand comes at sol 103.3, not 120.0. The multiplier falls from 0.735 to 0.605. A Q3 round's on-time award falls from 10,290 to 8,470 cr. Earth's price for Struct falls from 0.675 to 0.025 cr/kg.

### 1.3 What the market does (at the roll that closes the act's sol)

The act appends entries to the **political journal** (3.2) with `ClearSol = floor(ExecuteAtSimSeconds / SolLength)`. At the roll that closes `ClearSol` = s:
- the mark is **issued in the market**: `CurrencyIssueSol = s`, `RateMcr = CurrencyRateStartMcr` (1,000: parity);
- **the currency opens two sols later:** an order (a convoy or a desk order) settles in marks only if its clearing sol is at least `CurrencyOpenSol = s + 2`. Section 6 explains the two sols;
- rivals are admitted to the bloc (section 4);
- the rate takes its first step at the roll closing s + 1.

**The market's unit.** From `CurrencyOpenSol` on, E4's prices are **read in milli-marks per kg**. Their integers do not change at the issue, and E4's update is untouched: its anchor `V` keeps its number, now read in marks. So Martian prices are **sticky in marks**, and the rate passes through to what Martian goods cost in credits. That is the point of a floating rate. (If the anchor followed `V × 1000 / Rate`, Martian prices would re-anchor to Earth's credit value within weeks and the rate would cancel out.)

### 1.4 What the player does

- The Dilemma card (K:2251) gains a third button, **Defy, and issue a Martian currency**, greyed by `CanIssueCurrency` with the reason in its tooltip.
- The Earth & Neighbours panel gains **Issue a Martian currency** (`Currency Issue`), also greyed by the rule.
- Console: `RH.Currency issue | status`, and `RH.Solidarity defyissue`.

### 1.5 Reversible? (Q-B; the recommendation is written in)

**A Comply withdraws the mark.** Earth's demand is satisfied only if the colony withdraws its currency.
- **At the Comply's execution (live):** `bCurrencyIssued = false` and `bWithdrawPending = true`, so the requisition penalty ends at once. A Withdraw entry joins the Comply's journal entry. From then on, booking refuses any order that would settle in marks, convoy or desk, with `The mark is being withdrawn`. HEAD's Comply closes only the active rows' routes (section 0), so a discovered member could otherwise still be booked.
- **At the first roll at or after the Comply's `ClearSol` with no uncleared mark order (convoy or desk),** the market withdraws the mark, in this order:
  1. the colony's whole mark balance is redeemed into credits at the rate in force, with no spread: `CreditPay(floor(MarkBalanceMmk × RateMcr / 1000))` and `MarkSpend(MarkBalanceMmk)`;
  2. every mark convoy still on the road (cleared, its marks owed at its return) has its `OwedMmk` converted at the same rate into E4's `OwedMcr`, which it pays in credits when it comes home. The roll is exact, so the conversion is too, whichever step the convoy lands on;
  3. every traded price is re-expressed in credits: `Price = clamp(Price × RateMcr / 1000, V × Floor, V × Ceil)` (E4's band);
  4. the bloc dissolves, the rate is cleared, `CurrencyIssueSol` = none, and `bWithdrawPending = false`.

  One log line: `=== CURRENCY WITHDRAWN: %s marks redeemed for %s cr at %s ===`.
- A colony can issue again later under 1.1's rule. Its old partners' routes stay closed, as HEAD's Comply leaves them.

---

## 2. Two balances

### 2.1 The purses

- **Credits** are E2's balance, unchanged: `CreditBalanceMcr == CreditAwardedMcr − CreditSpentMcr`, never negative. `CreditPay` and `CreditSpend` (E4) are its only writers.
- **Marks** are new, all int64 milli-marks (mmk), all saved:
  - `MarkBalanceMmk == MarkEarnedMmk − MarkSpentMmk`, and it is never negative;
  - `MarkReservedMmk ≤ MarkBalanceMmk`, with `GetSpendableMmk() = Balance − Reserved`;
  - `MarkPay(Mmk)` is the only income writer, and `MarkSpend(Mmk)` the only debit writer. It pays `min(Mmk, Balance)` and returns the unpaid rest, like E4 R3(c).
- **The desk's credit income** is a new lifetime total, `DeskCreditMcr`, paid through `CreditPay`. E4's invariant becomes `0 ≤ SalesPaidMcr + MarketSalesMcr + DeskCreditMcr ≤ CreditAwardedMcr`.

### 2.2 Who accepts which

| Counterparty | Accepts | What it buys or sells |
|---|---|---|
| **Earth** | credits only | E2's manifest, E3's launches and sales, the quota award. Nothing about it changes, except the requisition penalty (1.2). |
| **A rival outside the bloc** | credits | E4's orders. After the issue, the market's mark price is converted at the rate in force during the clearing sol (2.4). |
| **A bloc member** | marks only | E4's orders and its standing trade, at the market price in marks. |
| **The bloc's desk** | both | marks for credits and credits for marks, at the rate, with a spread and a daily cap (2.5). |

Hope, population, growth bills and Influence never read or hold either purse (2.6).

### 2.3 What decides an order's currency

An order settles in marks if **both** of these hold for its **clearing sol** (the sol whose roll clears it: its `ClearSol`, or the open sol for a late booking, E4 R3(b)):
- the currency is open (`CurrencyOpenSol` ≤ the clearing sol);
- its rival is a member from that sol (`MemberFromSol` ≤ the clearing sol; section 4).

Otherwise it settles in credits. Booking decides this and records it on the order (`bMarks`). Booking can always decide, in both bands, because both thresholds are set by rolls at least two sols before the sol they open (section 6).

### 2.4 Settlement

**A mark order (to a bloc member)** is E4's order in marks, with no conversion:
- the reservation is E4's `max(0, Σ buy kg × Hi(q) − Σ sell kg × Lo(q))`, in marks, held in `MarkReservedMmk`;
- the clearing charges `Net` (marks) through `MarkSpend`, or carries `OwedMmk = −Net`, paid through `MarkPay` when the convoy comes home;
- E4's money refusal, in marks: `The order may cost up to %s marks net; %s marks are spendable`.

**A credit order after the issue (a rival outside the bloc)** converts at the clearing:
- `Charge = ceil(Net × r / 1000)` and `Owed = floor(−Net × r / 1000)`, where `Net` is in the market's unit (marks) and r is the rate in force during the clearing sol;
- the reservation covers a one-step move in both the price and the rate:

  ```
  HiR(r) = r * (10000 + CurrencyMaxStepBp) / 10000 + 1
  LoR(r) = max(1, r * (10000 - CurrencyMaxStepBp) / 10000 - 1)
  R_mcr  = max(0, ceil(Σ buy kg × Hi(q) × HiR(r) / 1000) - floor(Σ sell kg × Lo(q) × LoR(r) / 1000))
  ```

  This bounds the charge because the clearing price lies in [Lo(q), Hi(q)] and the clearing rate in [LoR(r), HiR(r)]. Both hold by E4 R3(b)'s argument: every booking sees `ClearSol − O ≤ 1`, and the rate steps once per roll, as the prices do. So ceil(a − b) ≤ ceil(a) − floor(b) bounds the rounded charge.

**Rounding** always goes against the colony: a charge rounds up, a payment rounds down, so no order or desk trip gains a milli-unit.

**Before `CurrencyOpenSol`** every order is E4's, exactly: no conversion, no rate margin.

### 2.5 The bloc's exchange desk (Q-C; the recommendation is written in)

- **The order.** A new uplink verb, `Exchange`. Target `ToMarks` or `ToCredits`; `Value` = whole units of the source currency (credits for `ToMarks`, marks for `ToCredits`), at least 1.
- **Booking** (`CanExchange(Dir, Amount, FString& OutWhy) const`), in order:
  1. the mark is issued in the market, and open for the order's clearing sol: `The desk opens at the end of sol %d`;
  2. `bCurrencyIssued` (not being withdrawn): `The mark is being withdrawn`;
  3. at least one member the colony can trade with (available, route open, not defected, not embargoed): `No bloc member can trade with you`;
  4. the total booked in this direction for this clearing sol, plus this order, is at most `DeskCapPerSol` (new config row, 500; Q-C): `The desk takes at most %d %s a sol (%d booked)`;
  5. the source spendable covers the amount: `Not enough %s spendable (%s)`.

  It reserves exactly the amount, in its source purse. E4 R3(b)'s catch-up applies.
- **The clearing.** In `RollMarket`'s clearing step, after the convoys, by order Id, at the rate r in force during the clearing sol, with `S = DeskSpreadBp` (300):
  - `ToMarks`: `CreditSpend(A)`, then `MarkPay(floor(A × 1000 / r × (10000 − S) / 10000))`;
  - `ToCredits`: `MarkSpend(A)`, then `CreditPay(floor(A × r / 1000 × (10000 − S) / 10000))`, and `DeskCreditMcr` grows by the payout.

  The reservation is released. One log line: `Desk #%d: %s %s -> %s %s at %s`.
- **Desk flows never move the rate.** Only goods traded with the bloc enter its trade term (3.3). Without this, the colony could push the rate up with money it then converts back.
- **A one-sol round trip always loses.** With the step at most 2% and the spread 3% each way, it returns at most 0.960 of the stake. A steady climb at the full 2% a sol breaks even only after 4 sols (modelled).

### 2.6 What the purses never touch

No sim rule reads a balance, the rate or a mark price outside the market, the desk and the requisition penalty: no Hope term, population rule, growth bill or wage. There are no wages, because there is no money in the city (Q4a). Influence is neither earned nor spent by any E5 path; Pacify costs Influence as before. Pacify's tension relief lifts the mark indirectly, through 3.3's tension term: +12% at equilibrium (modelled).

---

## 3. The exchange rate

### 3.1 State

Saved, int64 unless stated:
- `RateMcr`: milli-credits per mark. 1,000 is parity.
- `CurrencyIssueSol` (int32, or none), `CurrencyOpenSol` (int32), and `bCurrencyIssued` (bool: Earth's view, live from the act's execution).
- **The bloc tally:** a ring of 8 completed sols. `BlocSoldMmk[8]` and `BlocBoughtMmk[8]` are the value of the colony's filled sells to and buys from bloc members, in the market's unit, at the clearing price, written at the clearing into the sol it closes. The ring has its own fill and head.
- **The political record:** `IdentityViewMilli`, `TensionViewMilli` and the pending journal (3.2).

### 3.2 The political record: why, and what it is

**The rate cannot read `IdentityAxis` or `EarthTension`.** The live tension drifts in steps of different size in the two bands. And an act due 10 s after a boundary runs before that boundary's roll in the era band and after it in the agent band (section 0). Either way, a rate that read the live values at the roll would differ between bands. So E5 keeps its own record of the politics **as of each sol's close**, keyed by each act's `ExecuteAt`, exactly as E4 keys its clearing.

- **The journal.** Each political act appends one entry when its worker succeeds: `ClearSol`, `ExecuteAt`, `CommandId`, a sub-index, its kind, and its effect:

  | Act | Identity effect | Tension effect |
  |---|---|---|
  | Defy (and the Defy half of `DefyIssue`) | post-act `IdentityAxis` × 1000, rounded | −`DilemmaDefyTensionDrop` × 1000 |
  | Comply | post-act `IdentityAxis` × 1000, rounded | −`DilemmaComplyTensionDrop` × 1000 |
  | Pacify | none | −`PacifyTensionRelief` × 1000 |
  | Issue (and the issue half of `DefyIssue`) | none | +`CurrencyIssueTension` × 1000 |
  | Withdraw (only with a Comply while issued) | none | none |

  **Identity records the live value after the act.** That value is identical in both bands: identity has no drift, and only acts write it (section 0). **Tension records the nominal delta,** because its live value after the act is not identical.
- **At each roll, for the sol being closed:**
  1. `TensionViewMilli = min(100000, TensionViewMilli + Drift × n)`, where n is the number of sols closed and `Drift = llround(EarthTensionDriftPerSol × 1000)` (600). This applies only while any rival row is `SliceActive` (data, `ForEachRival`, D:496-510). On a long jump, adding n drifts at once equals adding them one at a time, because the drift is positive and only the top clamp can bind.
  2. Every pending entry with `ClearSol` ≤ the sol being closed is applied in `(ExecuteAt, CommandId, sub-index)` order, which is the same in both bands. Identity entries set `IdentityViewMilli` to their value. Tension entries add their delta, clamped to [0, 100000] after each. The entries are then removed.
- **How close the view stays to the live values.**
  - Identity equals the live value at every close where no act is pending.
  - Tension differs from the live value only where a clamp at 0 cuts an act short mid-sol, by at most one sol's drift (0.6). The live value also carries floating-point error, and the era band samples it up to 60 s later (0.03).
  - CU7 bounds the difference at 0.61.
  - **The deck shows the live values. The rate reads the record.**
- **The drift gate is data only.** HEAD's live drift also runs when only a *discovered* rival exists (W:9853-9856), and discovery is agent-band only. In that corner the live tension already differs between HEAD's bands, while the record stays exact and does not drift. That colony also cannot get a demand in the era band, so it cannot issue there either. This is a stated deviation (risk 3).
- **The harness.** `Debug_ShiftIdentity`, `Debug_AddTension` and `Debug_SetRequisitionInputs` write the live values and not the record. A new hook, `Debug_SyncPoliticalView()`, copies the live values (×1000, rounded) into the record. Only `-currency` calls it. No suite before E5 reads the record.
- **It runs from sol 0, issued or not.** It must hold the right tension on the day of the issue, and the live value cannot seed it exactly. It is silent: it logs nothing, and nothing reads it until the issue. It runs in `RollMarket`'s per-sol walk, which E4 R4 runs whether or not the market is live.

### 3.3 The update (all int64; once a sol, only after the roll that issued the mark)

```
R0        = CurrencyRateStartMcr                                                  // 1000
Sold, Bought = sum of the bloc tally's last W completed sols (mmk)                // W = CurrencyWindowSols, 5
TradeBp   = DivTrunc((Sold - Bought) * 10000, max(Sold + Bought, CurrencyMinVolumeMmk))   // -10000 .. 10000
IdentBp   = DivTrunc(IdentityViewMilli, 10)                                       // -10000 .. 10000
TensionBp = TensionViewMilli / 10                                                 // 0 .. 10000
AnchorBp  = DivTrunc((RateMcr - R0) * 10000, R0)                                  // -7500 .. +30000 inside the band
Pressure  = DivTrunc(TradeBp * WTrade + IdentBp * WIdentity - TensionBp * WTension - AnchorBp * WAnchor, 100)
StepBp    = clamp(DivTrunc(Pressure * CurrencyGainPct, 100), -CurrencyMaxStepBp, +CurrencyMaxStepBp)
Delta     = StepBp == 0 ? 0 : sign(StepBp) * max(1, |RateMcr * StepBp| / 10000)
RateMcr   = clamp(RateMcr + Delta, R0 * CurrencyFloorBp / 10000, R0 * CurrencyCeilBp / 10000)
```

- **Its direction.** A positive pressure raises the mark.
  - A trade **surplus** raises it: the bloc must find marks to pay for the colony's goods.
  - A Martian identity raises it: confidence in an independent Mars.
  - Earth tension lowers it: Earth's pressure weighs on it.
  - The anchor pulls it back toward parity, so it never sits at a bound without a cause.
- **Integer throughout.** E4's `DivTrunc` takes every signed division. No float or double enters.
- **Bounded.** One sol moves the rate by at most 2%, or 1 mcr. It stays inside [0.25, 4.0] credits per mark.
  - The highest equilibrium any input can reach is 2.4: trade, identity and tension all at their best, `(10000 × 40 + 10000 × 30) / 50 = 14000` bp.
  - The lowest, with identity at +20 and the worst trade and tension: `(−400000 + 60000 − 300000) / 50 = −12800` bp, below the floor. So the floor binds, a mark at 0.25 with a cause.
- **Overflow.** Sold and Bought are at most 8 sols × 3 convoys × 1,000 kg × 4V (V ≤ 200,000 mmk/kg for SpareParts), under 10^13. Times 10^4 is under 10^17.

### 3.4 Where it runs

`RollMarket` (E4 R4, beside `RollEconomyLedger` at G:488). For each sol it closes:
1. the clearing (E4 R4), now also the desk orders; the bloc tally is written into the closing sol;
2. rival production;
3. E4's reprice;
4. **E5, in this order:**
   - the political record (3.2);
   - the journal's transitions: an issue sets the issue and open sols (1.3); a withdrawal waits for its roll (1.5), then runs after the reprice;
   - the bloc's admissions (section 4);
   - the rate step (3.3), if the mark was issued at an earlier roll.

**A long jump.** The rate steps once for each sol E4 reprices, at most 8, like the prices. The record walks every closed sol.

### 3.5 Modelled numbers

A small integer model was run for every number in this design and in the director questions. It does not copy anything from earlier text. **Assumptions:**
- E4 R5's update, with Zarya's placeholder data;
- the standing trade pressed every round trip: 4 sols, plus 3 for Storm_1;
- tension from 0 at 0.6 a sol, so the first demand comes at sol 100.0;
- the player answers each demand half a sol after it arrives;
- Q-A (a) and the defaults of 3.6;
- the record and the demand are tracked at sol granularity. The demand timings in the director questions are computed exactly, in continuous time;
- a convoy's owed marks are counted at its clearing, not at its return, up to 4 sols early.

The model reproduces E4's figures: about 818 cr a run from sol 60, and 17,478 cr by sol 99.

- **No trade, a Defy at every demand.** The rate falls 2% a sol for two sols (0.980 at sol 101, 0.942 by sol 103). After the second Defy (identity +40, at 103.8) it holds near 0.94, sinks to 0.919 by sol 120 as tension climbs back, and reaches 1.004 at sol 140 (identity +60) and 1.267 at sol 199 (identity +100).
- **No trade, later demands ignored.** Tension climbs from 58.3. The rate is 0.849 at sol 110 and 0.774 at sol 120. Earth embargoes the closest neighbour at sol 128.3, and it defects at 136.3. The rate is 0.684 at sol 140 and 0.537 at sol 199 (tension 100).
- **With the standing trade to Zarya, a member** (a Defy at every demand). The surplus (TradeBp 1,636-3,272) offsets the tension. The low is 0.955 at sol 103, then 1.019 at sol 110, 1.055 at 120, 1.175 at 140 and 1.441 at 199.
- **One act's weight at equilibrium.** Each moves the rate's resting point:
  - a Defy (+20 identity, −12 tension): +19%;
  - a Pacify (−20 tension): +12%;
  - the issue's own tension: −6%.

### 3.6 Defaults

New `RH_Config` rows (placeholders; the C++ fallbacks equal the CSV), each clamped to a sane range on read, as E4 R2 does:
- `CurrencyIssueIdentity` 20, `CurrencyIssueTension` 10, `CurrencyRequisitionPenalty` 0.10;
- `CurrencyRateStartMcr` 1000, `CurrencyWindowSols` 5 (1..8), `CurrencyMinVolumeMmk` 5,000,000 (≥ 1);
- `CurrencyTradeWeight` 40, `CurrencyIdentityWeight` 30, `CurrencyTensionWeight` 30, `CurrencyAnchorWeight` 50;
- `CurrencyGainPct` 20, `CurrencyMaxStepBp` 200 (1..10000), `CurrencyFloorBp` 2500, `CurrencyCeilBp` 40000 (floor < 10000 < ceiling);
- `DeskSpreadBp` 300, `DeskCapPerSol` 500.

---

## 4. Rivals joining the bloc

- **Data.** A new `RH_Rivals` column and `FRHRivalRow` field, `BlocStance` (int32: the identity, in whole points, at which the rival joins; −1 means it never joins). Placeholders: Zarya 20, Meridian 40. An injected row defaults to −1, so `-trade`'s Greed and every test rival never join unless a test says so. `RH.InjectRivals` copies the column, as E4 R1 has it copy E4's.
- **The rule.** At step 4 of each roll after the issue, walk E4's `RivalEconomies`, which is sorted by name string with a case-sensitive ordinal compare and **never by FName index**. Each rival with an economy that is not a member and has `0 ≤ BlocStance ≤ IdentityViewMilli / 1000` joins. It gets `MemberFromSol` = the sol being closed + 2, and a log line, `Bloc: %s joins the mark (stance %+d, identity %+d)`.
- **Only exact inputs.** The rule reads data, the record and the rival's economy, never the relation, availability or a defection. Those change at band-dependent steps (section 0). So membership, and with it every order's currency, is identical in both bands.
- **Rivals choose.** A rival's stance is its choice: Zarya joins at the issue (+20), Meridian only after a further Defy (+40), and a rival with −1 never joins.
- **Hostility is still a booking refusal.** A member that is embargoed, defected, sabotaged or on a closed route is refused at booking by E4's refusals, with HEAD's texts. It stays a member and simply cannot trade.
- **Leaving.** A rival leaves only when the bloc dissolves at the withdrawal (1.5).
- **Membership is saved** as a list of name and `MemberFromSol`, sorted by name string.

---

## 5. Inertness

### 5.1 The switch

**"Issued" is the switch.** While `bCurrencyIssued` is false and no issue has rolled:
- `GetRequisitionMultiplier` is HEAD's, statement for statement (1.2);
- there is no rate step, bloc, mark, desk or conversion, and every order is E4's exactly (2.4);
- the political record and its journal run silently, logging nothing and read by nothing;
- the new refusal texts appear only on the new verbs.

A harness flag, `bCurrencyEnabled` (default true, never saved; `Debug_SetCurrencyEnabled`), refuses the two issue acts with `No currency: disabled`. MX-style twins use it.

### 5.2 The bars

- **No suite and no playtest issues the mark.**
  - `-solidarity` answers `Defy` and `Comply` (C:1223-1300): not `DefyIssue`, never `Currency`.
  - `-ending` moves identity by the harness (C:1790-1792) and sends no order.
  - `-earth`, `-preemptive`, `-covert` and `-espionage` send no new verb.
  - The bot never answers a demand (section 0).

  So in every suite `GetRequisitionMultiplier`, tension, identity, relations, Influence, Hope and the endings follow HEAD's code paths exactly.
- **The battery** is line-identical except:
  - the save-version lines;
  - the masked `(N bytes` lines;
  - the lines that print **whole-file sizes**: CV8, DP11 and EC1's two (E4 R10's list). They move for any new block, as they did in E1-E4;
  - the new `currency.log`.

  The tests that locate a block from the end of the file (E1's EC1/EC6, E2's CR9, E3's XE10 and E4's market block checks) subtract `GetCurrencyBlockBytes()` too, in code, with their texts unchanged. **`-solidarity`, `-ending`, `-earth` and `-preemptive` are identical,** line for line. The builder diffs them by name.
- **The 110-sol playtest is byte-identical.** The bot never issues, the record logs nothing, and the `.txt` holds only `[Playtest]` lines. The working tree has uncommitted edits to `RHPlaytest.cpp` from another session. The builder re-checks the claim at the build's HEAD.
- **`-scale`:** every digest is identical.

---

## 6. Era parity

**By construction, the rate, the record, the bloc and every settlement are band-exact.** All of them are written in one place, `RollMarket`, which runs on the same threshold step in both bands. Their inputs are band-independent:
- data: config, `BlocStance`, the rivals' economies;
- E4's market state: prices, fills and clearing prices, already band-exact given the same accepted orders;
- the journal, keyed by `ExecuteAt` and ordered by `(ExecuteAt, CommandId, sub-index)`, never by the step that ran the act.

**Why an order's currency is fixed two sols ahead.** On the boundary step, the era band can book an order due just after the boundary *before* that boundary's roll; the agent band books it after (E4 section 5, R7). Suppose a roll admitted a member, or opened the currency, for the very next sol. An order due 10 s into that sol would then be booked in credits by the era band (the roll had not run yet) and in marks by the agent band. Its settlement, and so the bloc tally and the rate, would split.

With the thresholds set two sols ahead, every booking (`ClearSol` ≤ O + 1 after E4's catch-up) is decided by rolls that closed sol `ClearSol − 2` ≤ O − 1. Those rolls have run in both bands. The withdrawal needs no such delay. From the Comply's execution, booking refuses every order that would settle in marks (1.5). Which orders that refusal catches at the margin is an acceptance difference, which is allowed. And the withdrawal roll itself waits for an exact condition: no uncleared mark order.

**What may still differ between the bands** is colony-side, as in E4:
- the live tension, so when Earth's next demand and an embargo fire. HEAD already differs here, and the issue's +10 is applied at the act's execution step;
- the live requisition multiplier read at an award or a launch (E2 and E3 already read it at band-dependent steps);
- when owed marks and credits arrive (the return step);
- acceptance at the margin: a stand-alone issue reads the live identity, `DefyIssue` needs a pending demand, and bookings read the balances;
- the size of a reservation (the quote).

**Given the same accepted acts and orders, the rate, the record, the bloc, every price, every store, every fill, every charge in either currency and every desk payout are identical.**

**The paired run (CU8).**
- **Start.** A named start save at sol 0 with Zarya plus an injected `Kestrel` (economy as E4 R7, `BlocStance` 40). It runs a **20-sol window with no storm**, which the suite asserts (E4's re-review condition 4).
- **Both twins at sol 0:** `Debug_SetRequisitionInputs(0, 59)`, then `Debug_SyncPoliticalView()`.
- **The script, the same sim times in both bands:**
  - `Solidarity DefyIssue` at sol 3.5 (the demand has been pending in both twins for more than a sol). The currency opens for sol 5.
  - Bloc orders to Zarya on sols 5, 9 and 13 at fraction 0.5.
  - Desk orders (`ToMarks 300` on sol 6, `ToCredits 200` on sol 11).
  - A credit order to Kestrel on sol 7.
  - `Debug_AddTension(25)` at sol 14.0 in both twins, so the live tension passes 75 and an embargo fires; `Pacify` the embargoed rival at sol 15.5. The harness write is not in the record, which is the point: the record, and so the rate, sees only the Pacify. No sync is called after sol 0, because the live values differ between the twins in their last bits.
- **The boundary fixture.** A second demand is pending by sol 8. A `Solidarity Defy` has its `ExecuteAt` 10 s after the sol-10 boundary, and the era twin's steps are placed so that the boundary step runs it before the roll. So:
  - the record must change at the roll closing sol **10** in both twins, never at the era twin's roll closing 9;
  - Kestrel joins at that roll (identity +40), with `MemberFromSol` 12;
  - an order to Kestrel with `ExecuteAt` 10 s after the sol-11 boundary (`ClearSol` 11) is booked **in credits** in both twins;
  - one with `ClearSol` 12 is booked **in marks** in both twins.
- **The bar.** At every threshold, the rate, both views, the membership list, every price, store, fill, charge and desk payout, and the bloc tally are identical (int64 equality). The sols in which convoys come home, and the end balances in both currencies, are equal.

---

## 7. Save

**Version: the next free one** (v55 if E4 takes v54 and nothing else lands first). The number is assigned at build time.

**The currency block** is written straight after E4's market block, as the last thing in the file. It is read after teardown like E1-E4's, so it repairs with a Warning and is never refused. In order:
- the flags and scalars: `bCurrencyIssued`, `CurrencyIssueSol` (−1 for none), `CurrencyOpenSol`, `bWithdrawPending` with its `ClearSol`, `RateMcr`;
- the record: `IdentityViewMilli`, `TensionViewMilli`, then the pending journal entries in `(ExecuteAt, CommandId, sub-index)` order;
- the bloc tally: 8 sold, 8 bought, fill, head;
- the purses: `MarkBalanceMmk`, `MarkEarnedMmk`, `MarkSpentMmk`, `MarkReservedMmk`, and `DeskCreditMcr`;
- the members (count, then name and `MemberFromSol`, by name string);
- **the mark orders:** a count, then each uncleared or uncollected mark convoy by convoy Id (Id, `ReservedMmk`, `OwedMmk`). A convoy not listed is a credit convoy;
- **the desk orders:** a count, then each by Id (Id, direction, amount, `ClearSol`, `bCleared`).

The block is **sized by history**, like E4's convoy list: even unissued, it holds the journal's pending entries at the moment of the save. Tests measure it with `GetCurrencyBlockBytes()` from the live state, never as a constant.

**Load repairs** (each with a Warning; no Error is ever logged):
- a rate outside the band is clamped;
- a record value outside its range is clamped;
- an unsorted or repeated journal or member list resets that part. The journal resets to empty, and the record is re-seeded from the live values with a Warning;
- a mark order naming a convoy Id that E4's block does not hold is dropped;
- `MarkReservedMmk` not equal to the sum of the uncleared mark orders' reservations, or above the balance, is set to that sum. If the sum is above the balance, reservations are cut from the highest Id down, as E4 R3 does;
- a negative purse is set to 0, then `MarkSpent = MarkEarned − MarkBalance`.

**A block before it that cannot be measured.** E4's reader gains a `bMarketBlockLost` flag, as E2-E4 added theirs. With it set, `ReadCurrencyBlock` reads nothing and starts **unissued**:
- the record is seeded from the live values;
- every convoy is treated as a credit convoy;
- a Warning says so.

A colony that had issued loses the mark, and Earth's penalty with it. Nothing is paid that was not owed.

**Checkpoints.** The bump refuses the Midgame and Late checkpoints. Re-cut them (`rh_checkpoints.sh all --force`) and load both.

---

## 8. Tests: `-currency`, a new fails-loudly suite

It goes in its own file, `Source/RedHopeSim/Private/RHSimCurrencySuite.cpp`, and is added to `rh_battery.sh`. No path logs an Error on purpose.

**New harness hooks** (runtime only, never saved):
- `Debug_SetCurrencyEnabled(bool)`;
- `Debug_SyncPoliticalView()`;
- `Debug_SetRate(Mcr)`;
- `Debug_SetBlocTally(SoldMmkPerSol, BoughtMmkPerSol)`;
- `Debug_GrantMarks(Mmk)` (through `MarkPay`);
- const reads `GetRate`, `GetIdentityView`, `GetTensionView`, `GetBlocMembers`, `GetMarkBalance`, `GetCurrencyBlockBytes`.

E4's hooks (`Debug_RollMarket`, `Debug_SetMarketPrice`, `Debug_SetRivalStore`) and `Debug_InjectRival` are reused.

| Check | What it pins |
|---|---|
| CU0 | **Data.** Every new config row and its clamp; `BlocStance` for Zarya (20) and Meridian (40); the CSV equals the live DT; an injected row defaults to −1. |
| CU1 | **Inertness.** Twins with `Debug_SetCurrencyEnabled` on and off, 10 sols in each band, including a Defy, a Comply and a Pacify through the uplink. The digest is bit-identical: stocks, both purses, relations, Influence, HumanNature, tension, identity, the multiplier, Hope, the quota phase and the projected ending. The saves are byte-identical up to the currency block. The record moved and logged nothing. |
| CU2 | **One rate step, by hand.** Fixtures with known tallies, views and rates. Each new rate equals a hand-computed integer written in the suite, including a step that rounds to 0 (the 1 mcr minimum), both clamps, `DivTrunc`'s negative cases and an anchor beyond ±10000. |
| CU3 | **Issuing.** Every refusal of 1.1 with its text, and `DefyIssue` with a check failing leaves the Defy undone. Then a `DefyIssue`: the Defy's HEAD effects, tension +10, the multiplier −0.10 (to the bit), the log line, two journal entries, then at the roll the issue sol, the open sol (+2), rate 1,000, and no step until the next roll. Then a stand-alone `Issue` at identity +20. |
| CU4 | **Joining.** Two injected rivals created in the reverse of their name order, so FName index order and name order differ. They join in name order, with the same log lines and the same saved bytes as the forward order. A stance of −1 never joins. A stance of 40 joins at the roll after a Defy brings the record to +40, with `MemberFromSol` = that sol + 2. A member under embargo is refused at booking with HEAD's text. |
| CU5 | **Settlement.** A mark order: its reservation in marks, the clearing's `MarkSpend`, `OwedMmk` paid at the return, the bloc tally. A credit order after the issue: its reservation with the rate margin, and the converted charge when price and rate each move a full step between booking and clearing (`Debug_SetMarketPrice`, `Debug_SetRate`). Every rounding is against the colony, to the milli-unit. Both purse invariants hold after every step. |
| CU6 | **The desk.** Both directions at a fixed rate: the payouts, the spread, the reservation and its release; the daily cap and its refusal; the refusal while withdrawing. Then a pure desk order: the rate's next step is unchanged. Then the one-sol round trip returns 0.960 or less of its stake. |
| CU7 | **The record.** 30 sols with no act: `TensionViewMilli` = start + 600 per sol, and each roll's \|live − view / 1000\| ≤ 0.61. An act due 10 s after a boundary changes the record at the roll that closes its own sol. A Comply from tension 5 clamps the record at 0. A long jump of 12 sols adds 12 drifts. With no `SliceActive` row there is no drift. After `Debug_ShiftIdentity(+10)` and a Defy, the identity record equals the live value, +30. |
| CU8 | **Era parity.** Section 6's paired run, including the boundary fixture, held to identical. |
| CU9 | **Withdrawal.** After a mark order, a Comply: the penalty ends at the execution, and a mark order to a discovered member (route still open) is refused. The withdrawal waits for the order to clear, then redeems the marks at the rate with no spread. A mark convoy still on the road has its owed marks converted to credits at that rate and pays them at its return. Every traded price is re-expressed (clamped to E4's band), and the bloc dissolves. The desk refuses, and a later `Issue` is allowed by 1.1's rule. |
| CU10 | **Save.** An issued colony with two members, a mark convoy out and a desk order pending: save, load, save is byte-identical, and the lists read back ascending. Every repair of section 7 on a doctored file, with the colony loaded. The previous version is refused. A lost market block starts the currency unissued. |
| CU11 | **Ruling 11 and Q4a.** The rate forced to both bounds, a large mark balance, 10 sols: Hope, the population, growth, Influence and every stock are bit-identical to the twin at parity. |
| CU12 | **Politics untouched.** Twins that issue and do not issue, from one save: identity, HumanNature, relations and Influence differ only by the Defy half of `DefyIssue` (the issue moves none of them), and the projected ending is the same. Tension and the multiplier differ by exactly 1.2's amounts. |

**Mutations** (CU13; each is built, run and restored by hash, and must fail the check named):
1. the rate reads the live `IdentityAxis` (CU8's boundary fixture: the era twin's rate steps with +40 one sol early);
2. the rate reads the live `EarthTension` (CU8's boundary fixture: the era twin's roll closing sol 9 sees the Defy's −12, and the agent twin's does not);
3. the journal keyed by the executing step's sol instead of `ExecuteAt` (CU8's boundary fixture);
4. `MemberFromSol` = the closing sol + 1 (CU8: the `ClearSol` 11 order to Kestrel books in credits in the era twin and in marks in the agent twin);
5. `CurrencyOpenSol` = the issue sol + 1 (CU3's open sol, and CU8 with an extra order due 10 s into sol 5);
6. joins walked in FName order (CU4's reverse injection: the saved member bytes and the log order differ);
7. desk flows fed into the trade term (CU6: the rate moves after a pure desk order);
8. the credit reservation without its rate margin (CU5: the charge outruns the reservation);
9. a mark charge rounded down (CU5's milli-unit check);
10. a `double` in the rate update (CU2);
11. the requisition penalty applied unissued (CU1);
12. a Comply that does not withdraw (CU9);
13. the identity view built from nominal deltas while a harness write intervenes (CU7's sync case: the view no longer equals the live value after `Debug_ShiftIdentity` and a Defy).

**`-scale`** (outside the battery): 1,000 `RollMarket` calls with 8 injected members and the currency issued, at N = 5,000. Budget: 0.5 ms more than E4's measured figure. Every digest is identical.

**What the battery moves:** section 5.2's list and nothing else.

---

## 9. Presentation (RedHope; E8 is the full panel)

- **Earth & Neighbours:**
  - the two balances (Credits / Marks, with spendable and reserved in the tooltip);
  - the rate as `1 mark = 1.06 cr` with the last step's sign;
  - the issue button and its reason.
- **Each rival card:** "Bloc member" or "Joins at identity +40", and the currency its next order would settle in.
- **The desk:** `To marks +100` / `To credits +100` and SEND, read by `CanExchange`, with the cap and the spread shown.
- **The Dilemma card:** the third button (1.4).
- **Console:** `RH.Currency issue | status | desk <tomarks|tocredits> <amount>`.

New strings go into the wording table as placeholders. None is morale, sickness or evacuation wording, so Gate D's review is not engaged. The mark's name is a placeholder for the director.

---

## 10. Risks

1. **A second record of politics.**
   - The rate reads tension and identity from the sol-close record, not the live values. They can differ by up to 0.6 tension at a clamp, and by any harness write.
   - The deck shows the live values. It must label the rate's inputs "at the close of the last sol".
   - CU7 bounds the difference, and mutations 1-3 show why the record exists.
2. **Every future political act must be journaled.** E6 will make embargoes, sabotage and covert operations move prices. Those are not uplink orders: the embargo is a threshold on live tension, and defection is a countdown. If any of them moves the rate, it must first be keyed exactly, or the rate splits between bands. E5 deliberately reads none of them.
3. **The drift gate is data only** (3.2). It is exact, but it differs from HEAD's live drift in a colony made live only by a discovered rival. Such a colony cannot issue in the era band.
4. **The issue accelerates the Dilemma chain** (Q-A). With +10, Earth's next demand comes 2.8 sols after the issue instead of 19.5 sols after a plain Defy. A colony that keeps defying reaches the Martian ending path at identity +60 by sol 123.8 instead of 140.5.
5. **A predictable rate can be traded.** The rate follows the colony's own acts and trade, so a player can foresee it. The desk's cap and spread bound the gain (Q-C).
6. **The withdrawal re-expresses every Martian price** at the rate: ×1.058 in Q-B's modelled case, ×0.537 if demands had been ignored. E4's anchor then pulls prices back toward `V` in credits at up to 5% a sol.
7. **Earth trade is not in the trade term.** Launches are deck calls at band-dependent steps, so the trade balance counts only the colony's cleared trade with the bloc. That is a stated reading of "the trade balance".
8. **E4 is not built.** Every E4 name here is a design name. The builder rebases onto E4 as built and re-checks 2.4's bound against E4's final reservation code.
9. **A standing trade to a member that nets a charge needs marks.** At the modelled prices Zarya's standing trade always pays the colony: Struct at 9.3 marks against Ice at its 0.75 floor. A data change could make it charge, and then it is refused when the colony holds too few marks.
10. **A history-sized save block.** Its size depends on the journal, the mark convoys and the desk orders, so offset tests must read `GetCurrencyBlockBytes()` at the moment of the save.

---

## Director questions

Every number below comes from the model in 3.5, under its stated assumptions (the first demand at sol 100.0, each answered half a sol later; the standing trade to Zarya every round trip). A Q3 round's on-time award is 14,000 kg × the multiplier, at 1 cr a kg, leaving out research funding.

**Q-A. How hard does Earth react when the colony issues the mark?**

| Option | Tension after a `DefyIssue` at sol 100.5 | Earth's next demand | If later demands are ignored: embargo, then defection | Multiplier | A Q3 round's award | Earth pays for Struct / Oxygen / Hydrogen (cr/kg) | With a Defy at every demand, the Martian ending path (identity +60) |
|---|---|---|---|---|---|---|---|
| a plain Defy, no mark (for comparison) | 48.3 | sol 120.0 | 145.0, then 153.0 | 0.735 | 10,290 cr | 0.675 / 2.880 / 5.820 | sol 140.5 |
| **(a) tension +10, multiplier −0.10 (recommended)** | 58.3 | sol 103.3 | 128.3, then 136.3 | 0.605 | 8,470 cr | 0.025 / 1.840 / 4.260 | sol 123.8 |
| (b) tension +20, multiplier −0.20 | 68.3 | at once | 111.7, then 119.7 | 0.475 | 6,650 cr | refused / 0.800 / 2.700 | sol 107.2 |
| (c) tension +10 only | 58.3 | sol 103.3 | 128.3, then 136.3 | 0.705 | 9,870 cr | 0.525 / 2.640 / 5.460 | sol 123.8 |

- **(a) and (c)** give the same rate. With no trade and a Defy at every demand, the mark is 0.943 at sol 105, 0.919 at 120, 1.004 at 140 and 1.267 at 199.
- **(b)** gives 0.958, 1.015, 1.124 and 1.275.
- **If demands are ignored,** the mark sinks to 0.537 under (a) and 0.521 under (b) by sol 199.
- **(a)** makes the mark cost about 1,800 cr a Q3 round and close Earth's market for Struct, without forcing the next crisis at once.

**Q-B. Can the mark be undone?**
- **(a) (recommended): a Comply withdraws it.** Earth's demand is met only if the mark goes.
  - In the modelled case the colony issues at sol 100.5, defies at 103.8 and then complies at 123.8. It holds 4,967 marks at a rate of 1.058, about 820 of them still owed by the convoy on the road. They are redeemed for **5,255 cr**: the balance at once, and the convoy's share, converted at the same rate, when it comes home.
  - Martian prices are re-expressed in credits ×1.058.
  - Earth's penalty ends: the multiplier returns to 0.834 (identity +20, tension 15.3), and a Q3 round pays **11,677 cr**.
  - The colony may issue again later, but every route the Comply closed stays closed.
- **(b): it is permanent.** The Comply still closes every route (HEAD), so the 4,967 marks are stranded with no partner and no desk. The penalty stays for the rest of the game: the multiplier is 0.734, and a Q3 round pays **10,277 cr**, 1,400 cr less every round.

**Q-C. Can marks and credits be exchanged?**
- **(a) (recommended): yes, at the bloc's desk, with a 3% spread each way and at most 500 a sol each way.**
  - The standing trade's mark earnings over sols 100-199, converted as they arrive, become **24,272 cr**. The same trade, never issued, earns **20,698 cr** in credits. The gain is the mark's rise from 1.0 to 1.44.
  - A heavy trader selling 1,000 kg of Struct to Zarya every round trip floods Zarya's store, so Struct falls to its 1.25 floor. It converts to **38,619 cr**, against 31,250 cr never issued.
  - Speculation is bounded. A modelled plan turns the 17,478 cr earned by sol 99 into marks at 500 a sol (sols 101-134), then back at 500 a sol from sol 150. It gains **3,483 cr (+19.9%)**.
- **(b): yes, with no cap.** The trades convert to 24,778 cr and 39,527 cr. The same 17,478 cr turned into marks at the low (0.955, sol 103) and back at sol 199 (1.441) gains **7,336 cr (+42%)**.
- **(c): no desk.** Marks buy only from bloc members. The standing trade leaves **20,698 marks**, which Earth will not take. Once Zarya and Meridian have joined, no neighbour trades in credits, so the rate is a display only.

---

# PROFESSOR REVIEW

An independent adversarial review against HEAD `4edd7fd` (save v53), read-only, with `git show HEAD:`. The reviewer built its own integer model (in the session scratchpad, `e5-review/`). It is recorded as returned, lightly formatted.

**Verdict: CLEARED WITH CONDITIONS**

In the reviewer's words: "The core mechanism is exact by construction. Its test fixtures, two money-path specifications and part of the modelled numbers are not."

## What holds (checked)

- **Section 0's anchors match HEAD**, apart from the `-ending` anchor (observation 15).
- **Every writer of `IdentityAxis` and `EarthTension` is accounted for.** In play: the drift (W:10080), Pacify (W:10276) and the Dilemma (W:10488-10489, :10502-10503). Otherwise only world start (W:453-454), the load (W:14417) and the harness (H:1489, :1637, :1638; `RH.Earth`, RHCheats.cpp:903-904). The journal covers every in-play writer.
- **The keying is exact.** An act with `ClearSol` k always executes at or before the step that runs the roll closing k, in both bands:
  - the roll fires on the first step whose end reaches (k+1)·1200 (G:480-482);
  - `StepUplink` runs earlier in the same step (W:672 before :683; W:844 before :855);
  - in `Tick`, `StepUplink` reads the frame end (W:1067), which is at or after every sub-step end (W:634).

  So the journal's application order is identical in both bands.
- **The two-sol lead is sufficient.** With E4 R3(b), every booking's currency is decided by rolls that closed sol O − 1 or earlier. The roll reads no live value.
- **The record and the live values diverge only in harmless ways**: through the 0 and 100 clamps, and through acts that are live but still pending. Nothing reads the difference.
- **Inertness holds.**
  - `-solidarity` sends only `Comply` and `Defy` (C:1237, :1264, :1276, :1292).
  - `-earth`, `-crisis` and `-ending` use the harness only. `-preemptive` adds only Pacify.
  - The worker maps every non-`Comply` target to Defy (W:9570).
  - The bot's only uplink path (P:260) never sends Solidarity or Pacify.
  - XE6 pins the unissued multiplier to 1e-12 (RHSimExportSuite.cpp:319).
- **These numbers reproduce exactly:**
  - 1.2 and the whole Q-A table: tensions, demands, embargo and defection sols, multipliers (with E3's rounding, X:170), awards and Earth prices;
  - the ending path at 140.5 / 123.8 / 107.2;
  - Q-B's 4,967 marks, multiplier 0.834, and 11,677 / 10,277 cr;
  - the act weights (+19% / +12% / −6%), the one-sol round trip at 0.960, and break-even after 4 sols;
  - Q-A (b)'s rate path and the ignored-demands paths.
- **These numbers are close:** speculation +3,400 cr (+19.5%) against the doc's +3,483 (+19.9%); uncapped +7.0k to +7.2k (+40-41%) against +7,336 (+42%). No number is off by anything like E4's ~16x.

## REQUIRED CHANGES

1. **CU8 cannot run as specified.**
   - **The storm.** Storm_1 runs sols 12-15 (RH_Events.csv:2; `WeatherCycleSols` 0, so it fires once), and a dust storm freezes every convoy (W:9744-9748). The suite's "no storm in a 20-sol window from sol 0" assertion fails. Kestrel's `ClearSol` 11 convoy also freezes, so the `ClearSol` 12 order to Kestrel is refused "already out" in both twins. This is E4's re-review condition 4, repeated.
   - **Zarya's schedule.** Orders on sols 5, 9 and 13 match Zarya's 4-sol round trip exactly. Each executes at the moment the last convoy comes home, and float accumulation decides the "already out" refusal: the era dispatch credits a full 60 s of travel (W:9558-9560, :9749-9751), and `StepUplink` (W:672) runs before `StepTrade` (W:685). A `ClearSol` 12 Kestrel order 10 s after its boundary has the same zero margin against Kestrel's 1-sol round trip. This breaks E4 R7's "home at least a sol before the next order".
   - **The Pacify at 15.5 is refused in both twins.** It needs Influence ≥ 20 (W:10268). Influence comes only from honest convoys coming home (+3 each, W:9810) and HumanNature drift (W:10198, under 0.1 a sol here), so it is about 16 at best by then.
   - **No credits.** `ToMarks 300` and the Kestrel credit order need spendable credit, which a sol-0 colony does not have.
   - **Change:** start at sol 16 or later (or inject Storm_1 inactive) and assert no DustStorm in the window; space Zarya's orders at least 5 sols apart; put the `ClearSol` 12 Kestrel order at 12.5 or later; call `Debug_AddInfluence(20+)` and `Debug_GrantCredits` in both twins; restate E4 R7's two schedule rules.
2. **Mutation 3 cannot fail CU8.**
   - The era twin's boundary step ends after 12,000 s, because the clock advances before `EraStep` (C:1161-1162) and `StepUplink` reads it (W:1067). Its sol, floor(end / 1200) = 10, equals floor(`ExecuteAt` / 1200) = 10. Both twins key the act to 10.
   - E4's review item 8 caught this same mistake, and E4 R8's mutation 4 fixed it.
   - **Change:** define the mutation as "keyed by `MarketCheckedSol` at execution", which is 9 in the era twin, or add an act due 10 s before a boundary.
3. **CU1 tests nothing, and mutation 11 cannot fail it.**
   - `bCurrencyEnabled` only refuses the issue acts, which CU1 never sends. The record runs in both twins, so they run identical code.
   - A penalty applied while unissued appears in both twins. Only XE6, outside the suite, would catch it.
   - **Change:** have the "off" twin skip the record walk and the journal appends; add an absolute check that the unissued multiplier equals HEAD's expression, computed in the suite; name that check for mutation 11.
4. **Desk reservations break E4's credit-reservation invariant and its load repair.**
   - E4 R3(d) defines `CreditReservedMcr` as the sum of the uncleared convoys' `ReservedMcr`. R3's repair recomputes it as exactly that sum, inside the market-block reader, which runs before the currency block is read.
   - A `ToMarks` order reserves credits in the same counter. So MX6's invariant fails whenever a `ToMarks` order is pending. CU10's save, load, save then triggers the repair, which drops the desk reservation, logs a Warning and changes the bytes. The desk order then clears `CreditSpend(A)` with nothing reserved.
   - **Change:**
     - restate the invariant as the convoys' sum plus the pending `ToMarks` amounts;
     - run the credit repair after the currency block is read;
     - make the mark repair name `ToCredits` orders explicitly;
     - book the withdrawal's `CreditPay` to a named lifetime total, and extend E4's lifetime-total repair to match.
5. **The 2.4 margin bound fails across the withdrawal roll.**
   - Take a credit order to a non-member, booked on the boundary step after the Comply has executed, with `ClearSol` = the withdrawal sol + 1. It reserves `Hi(q)·HiR(r)`. The withdrawal then re-expresses the price as `clamp(P·r/1000, V/4, 4V)` and clears the rate.
   - With r < 1 and the slot at its floor, the clamp raises the credit price above the reservation. For example, at r = 684 with Ice at its 750 mmk floor: the reservation covers about 550 mcr/kg, but the charge is 750, about 36% over. Sells at the 4V ceiling fail the same way when r > 1.
   - How a converted order clears once the rate is cleared is also unspecified.
   - **Change:** while `bWithdrawPending`, refuse orders that clear after the withdrawal sol, or size a clamp-aware bound; and state how such an order clears.
6. **The modelled numbers break the design's own keying rule, and one Q-C consequence is wrong.**
   - **The keying.** The doc's model applied the Defies at x.83 sols at the next sol's roll, not at floor(`ExecuteAt` / 1200). Shifting those acts one roll late reproduces every one of the doc's figures exactly. Under the design's own rule:

     | Case | Sol | Doc | Design rule |
     |---|---|---|---|
     | No trade, Defy at every demand | 103 | 0.942 | 0.960 |
     | | 105 | 0.943 | 0.958 |
     | | 120 | 0.919 | 0.922 |
     | | 140 | 1.004 | 1.009 |
     | | 199 | 1.267 | 1.269 |
     | With the standing trade: the low | — | 0.955 (sol 103) | 0.961 (sol 102) |
     | With the standing trade | 110 | 1.019 | 1.027 |

     So Q-A's "(a) 0.943 against (b) 0.958 at sol 105" is an artifact: both are 0.958.
   - **The cap's cost (Q-C).** The standing trade earns 828 marks every 4 sols, and the heavy trader 1,250. A cap of 500 a sol only pushes the second tranche a sol later, into a rising rate.
     - Standing trade: 24,078 cr capped against 24,069 uncapped.
     - Heavy trader: 38,509 against 38,510.

     The doc's figures imply the cap costs honest traders 2.1-2.4%. It costs nothing; the cap binds only on speculation.
   - **Smaller fixes.**
     - The desk opens at sol 102, so "sols 101-134" is wrong.
     - Q-B's 5,255 cr uses the post-step rate. The rate in force during sol 123 is 1.050, which gives 5,216 cr. Pick one, and say whether the rate steps at the withdrawal roll.
   - **Change:** rerun the model with floor keying, and restate these numbers.

## OBSERVATIONS (non-blocking)

1. **The issue switches the whole region's unit to marks.** With no members at all (reachable after a Comply, via `DefyIssue` at identity ≤ 0, or with injected stances), trade is repriced by a currency nobody holds, and the rate slides toward its floor on tension alone. Put this to the director, or require a member at the issue roll.
2. **Admission wording.** Section 4 says admissions run "after the issue", but 1.3, CU8 and the model need admission at the issue roll itself. Also skip admissions once a withdrawal has run in the same roll.
3. **CU3 and CU12.** A `DefyIssue` against a plain Defy moves the multiplier by 0.13, not 0.10. "To the bit" needs a reference computed in the same operation order. CU12's window must end before the issuing twin reaches tension 75, about 27.8 sols after the issue.
4. **Mutation 5's CU8 order** must be 10 s into the issue sol + 1, not into the open sol.
5. **Mutation 9.** A mark charge involves no rounding. Reword it around the converted credit charge's ceil.
6. **Mutation 10.** Add an enumeration with an independent integer reference, as E4's MX5 has.
7. **Redemption rounding.** The balance and each on-the-road convoy's owed marks are floored separately. Which convoys are already home depends on the band, so the total can differ by 1 mcr per convoy.
8. **A doctored-load trap.** A journal reset can lose a pending Issue entry while `bCurrencyIssued` stays true, leaving the penalty with no mark and a re-issue refused. Reconcile `bCurrencyIssued` in that repair.
9. **3.2's "only where a clamp at 0".** It misses the clamp at 100 and the window where an act is live but pending (up to 45 points).
10. **Meridian** joins while undiscovered, so its join line names a hidden settlement. Q-B (b)'s "no partner and no desk" fails if Meridian has been discovered, because a Comply closes only active rows (D:505).
11. **The multiplier can reach 0.** With the penalty, its minimum falls from 0.1 to 0.0: at identity +100 and tension 100, the award is 0 and every E3 sale is refused.
12. **Greed** copies Zarya's row (C:1116), so its `BlocStance` is 20, not −1. It is harmless only because Greed has no economy.
13. **Overflow note.** The largest V is HabSegment's 900,000, not SpareParts' 200,000. Still far inside int64.
14. **E4's figures.** E4 R5 gives 827.5-827.9 cr a run and 17,540 cr by sol 99, not 818 / 17,478.
15. **`-ending` anchor.** The identity harness write is at C:1797, not C:1790-1792.
16. **Desk order Ids.** Say the Id is the `CommandId` (saved since v37), or save a counter.
17. **Mark clearing bounds.** A mark clearing needs E4 re-review condition 5(b)'s bound: its own reservation plus spendable marks. `GoodwillMinValueMcr` needs a unit for mark orders.
18. **A late journal entry under the catch-up.** E4 R3(b)'s catch-up, run after a harness clock jump, can close a sol before a later-queued act due in it runs, so that act applies one roll late. It is the same in both twins, but say so.
19. **E4's own suite lines.** Once E4 is built, re-check every `-market` line that calls its block "last" or prints a size or offset.

---

# REVISION

This section replaces the design wherever they differ. The builder's order of authority:
1. the director's answers;
2. this REVISION;
3. the PROFESSOR REVIEW;
4. the design.

Anchors are at HEAD `4edd7fd`. **Every number below was recomputed** with a corrected integer model:
- convoys dispatched in continuous time, each re-sent on its return, with Storm_1 freezing them;
- every act keyed by floor(`ExecuteAt` / sol).

It agrees to the last digit with the reviewer's independent model on every figure quoted here, including E4's own figures: 827.9 cr a run from about sol 60, and 17,540 cr by sol 99. Both models count a convoy's owed marks at its clearing rather than its return (up to 4 sols early).

**R1. CU8, the paired run, restated** (required change 1, observation 4).

- **Start.** The suite builds its fixture colony with no convoy out, then sets the clock to sol 16.0 (`Debug_SetSimSeconds`) and saves it as `cu8_start`. Both twins load that save.
  - With `WeatherCycleSols` 0, the active event is a pure function of the sol (`GetActiveEvent`, W:2595-2614), so Storm_1 (sols 12-15) cannot recur.
  - The suite asserts that no DustStorm is active at any threshold of the window, sol 16 to 36.
- **Rivals.** Zarya, plus an injected `Kestrel`: E4 R7's economy, 30 km (a 1-sol round trip), `BlocStance` 40, and `RelationStart` 10, so Earth's embargo targets Zarya.
- **At load, in both twins:**
  - `Debug_SetRequisitionInputs(0, 59)` and `Debug_SyncPoliticalView()` (the live values are identical in the twins at this moment);
  - `Debug_GrantCredits(20,000 cr)`;
  - `Debug_AddInfluence(40)`.
- **The era twin's steps.** The era twin first advances 30 s, so every era step ends 30 s past a minute. Each boundary step ends at the boundary + 30 s, so an order due 10 s after a boundary runs on that step, before its roll.
- **The schedule** (sim times, the same in both twins):

  | Sol | Order | Why it is there |
  |---|---|---|
  | 19.5 | `Solidarity DefyIssue` | The demand has been pending since about 17.7 in both twins. `ClearSol` 19, so the currency opens for sol 21 and Zarya is a member from sol 21. |
  | 20.0 + 10 s | `MarketOrder` to Zarya | `ClearSol` 20: **credits** in both twins. With mutation 5, the agent twin books it in marks. Zarya is home at about 24.0. |
  | 22.5 | `Exchange ToMarks 300` | |
  | 23.5 | `MarketOrder` to Kestrel | Credits, converted at the rate. Home at about 24.5. |
  | 25.5 | `MarketOrder` to Zarya | Marks. Home at about 29.5. |
  | 26.0 + 10 s | `Solidarity Defy` | **The boundary act.** The second demand has been pending since about 21.0. `ClearSol` 26. |
  | 27.0 + 10 s | `MarketOrder` to Kestrel | `ClearSol` 27: **credits** in both twins. Kestrel joins at the roll closing 26 and is a member from 28. Home at about 28.0. |
  | 28.5 | `Exchange ToCredits 200` | |
  | 29.5 | `MarketOrder` to Kestrel | `ClearSol` 29: **marks** in both twins. |
  | 30.0 | `Debug_AddTension(25)`, in both twins | About 53.4 + 25 = 78.4 live, so an embargo of Zarya fires. The record does not see it. |
  | 30.5 | `Pacify` Zarya | Influence 40 ≥ 20. `ClearSol` 30, so the record takes −20 at the roll closing 30. |
  | 31.5 | `MarketOrder` to Zarya | Marks, after the Pacify has lifted the embargo. |

- **E4 R7's two schedule rules hold, and the suite asserts both:**
  - every convoy is home at least a sol before the next order to the same rival (Zarya: 24.0 → 25.5, 29.5 → 31.5; Kestrel: 24.5 → 27.0, 28.0 → 29.5);
  - every reservation is at most half the spendable credit or marks.
- **The bar** is section 6's: at every threshold from 16 to 36, everything is identical.
- **Mutations it bites:**
  - 1 and 2: the era twin's roll closing 25 sees the boundary Defy's +20 and −12;
  - 3, restated in R2;
  - 4: the `ClearSol` 27 order;
  - 5: the `ClearSol` 20 order.

**R2. Mutation 3, redefined** (required change 2). It becomes: "the journal keyed by `MarketCheckedSol` at the act's execution". In the era twin the boundary Defy runs before the roll closing 25, while `MarketCheckedSol` is 25, so it applies a roll early. In the agent twin that roll has already run, so it is keyed 26. CU8's boundary fixture fails.

**R3. CU1 and mutation 11, restated** (required change 3).

- `Debug_SetCurrencyEnabled(false)` now turns off **all** E5 code: the record walk, the journal appends, the bloc, the rate and the two issue acts. The CU1 twins therefore run different code. Their digest, and their saves up to the currency block, must still be identical.
- **CU1b, a new absolute check.** For identities −100, −20, 0, +20 and +100 and tensions 0, 48.3, 60 and 100, the unissued `GetRequisitionMultiplier` equals HEAD's expression computed in the suite, in the same operation order, bit for bit.
- Mutation 11 is named for CU1b.

**R4. The reservation invariants and their repair** (required change 4, observations 16 and 17).

- **Credits.** `CreditReservedMcr` == Σ of the uncleared credit convoys' `ReservedMcr` + Σ of the pending `ToMarks` desk amounts, and it is at most `CreditBalanceMcr`.
- **Marks.** `MarkReservedMmk` == Σ of the uncleared mark convoys' `ReservedMmk` + Σ of the pending `ToCredits` desk amounts, and it is at most `MarkBalanceMmk`.
- **Where the repair runs.** Both are recomputed in one post-load step, `ReconcileReservations()`, which runs **after** the currency block has been read. E4 R3's recompute moves out of the market-block reader into this step. If a sum exceeds its balance, reservations are cut from the highest Id down: convoys and desk orders together, by `CommandId`.
- **The withdrawal's payout** goes through `CreditPay` into a new lifetime total, `CurrencyRedeemedMcr`.
  - E4's invariant becomes `0 ≤ SalesPaidMcr + MarketSalesMcr + DeskCreditMcr + CurrencyRedeemedMcr ≤ CreditAwardedMcr`.
  - E4's lifetime-total repair clamps the four together.
  - `Debug_ZeroCredits` zeroes all four (E4 condition 5(a)) and refuses while anything is reserved.
- **Desk order Ids** are their `CommandId`, saved since v37. No counter is needed.
- **A mark clearing** pays at most its own reservation plus the spendable marks, as E4 condition 5(b) has credits do. Any rest is written off with E4 R3(c)'s Warning, worded in marks.
- **E4's goodwill floor** for a mark order reads 300 marks.

**R5. No order spans a withdrawal** (required change 5, observation 7). This replaces 1.5's second bullet.

- **From the Comply's execution, while `bWithdrawPending`,** booking refuses **every** order, convoy or desk, in either currency: `The market is closed while the mark is withdrawn`.
- **The withdrawal runs at the first roll at or after the Comply's `ClearSol` with no uncleared order of any kind.** Orders booked before the Comply have `ClearSol` ≤ the Comply's `ClearSol` + 1, so this is that roll or the next. The condition is exact.
- **At that roll** (after the clearing and E4's reprice; **the rate does not step at the withdrawal roll**):
  1. Take the rate in force during the closing sol, r.
  2. The redemption is **one floor over one exact total**: the mark balance plus every outstanding `OwedMmk` on convoys still on the road. The colony receives `CreditPay(floor(total × r / 1000))` at once. The convoys still on the road owe nothing more and bring home only their goods.

     The total is band-exact even though which convoys are home is not, so the bands cannot split by a milli-credit.
  3. Every traded price is re-expressed: `Price = clamp(Price × r / 1000, V × Floor, V × Ceil)`.
  4. The bloc dissolves, the rate is cleared, `CurrencyIssueSol` = none and `bWithdrawPending = false`. Booking reopens for the sol after.
- **No order is ever charged a re-expressed price that its reservation did not cover**, because none is uncleared at the roll that re-expresses.

**R6. The modelled numbers, restated** (required change 6, observations 11 and 14). These replace 3.5's bullets, 1.2's last paragraph where they differ, and the director questions' numbers.

- **E4's figures:** about 828 cr a run from sol 60 (827.5-827.9), and 17,540 cr by sol 99.
- **Q-A (a), no trade, a Defy at every demand.**
  - The rate is 0.980 at sol 101 and 0.961 at sol 102.
  - The second Defy (at 103.8, applied at the roll closing 103) holds it at 0.960 (103), 0.958 (105) and 0.949 (110).
  - It sinks to its low, 0.916 at sol 122, as tension climbs back. Then it reaches 1.009 at sol 140 and 1.269 at sol 199.
- **Q-A (b), no trade, a Defy at every demand:** 0.958 at sol 105, 0.986 at 110, 1.015 at 120, 1.124 at 140, 1.275 at 199. At sol 105, (a) and (b) are equal (0.958); the first draft's "0.943 against 0.958" is withdrawn.
- **Later demands ignored (unchanged):** under (a), 0.849 at sol 110, 0.774 at 120, 0.684 at 140 and 0.537 at 199; under (b), 0.826, 0.733, 0.633 and 0.521.
- **With the standing trade to Zarya, a member** (Q-A (a), a Defy at every demand):
  - TradeBp is 1,655-3,311;
  - the low is 0.961 at sol 102;
  - then 1.027 at sol 110, 1.058 at 120, 1.178 at 140 and 1.442 at 199.
- **The desk opens at sol 102**, the issue sol + 2.
- **The multiplier can now reach 0.** The penalty lowers its minimum from 0.1 to 0.0: at identity +100 and tension 100, Earth's award is 0 and every E3 sale is refused. Risk 11, new.

**R7. The observations, taken.**

- **O1: the issue needs a partner.** Check 3 of 1.1 becomes: at least one rival with an economy and an open route whose stance the identity after the act meets: `No neighbour would join the mark`.
  - It is colony-side, at execution. The record's identity at the issue roll is that same post-act value (3.2), so at least one rival joins at the issue roll.
  - A `DefyIssue` at identity −20 reaches only 0, so it is refused, not half done.
- **O2: admissions** run at every roll from the issue roll on, inclusive, and are skipped at, and after, the roll that runs a withdrawal.
- **O3: CU3 and CU12.**
  - CU3 compares a `DefyIssue` twin with a plain-Defy twin. The multiplier differs by exactly 0.13 (0.10 + 0.3 × 10 / 100), against a reference computed in `GetRequisitionMultiplier`'s operation order.
  - CU12's window ends 25 sols after the issue, before the issuing twin's tension reaches 75 (27.8 sols).
- **O4: mutation 5** is bitten by R1's `ClearSol` 20 order.
- **O5: mutation 9** becomes "the converted credit charge rounded down instead of up", caught by CU5's milli-credit check.
- **O6: CU2b, new.** 10,000 rate steps from a seeded LCG over tallies, views and rates. Every step is within 2% or 1 mcr, every rate is inside the band, and each result equals an independent integer reference written in the suite. Mutation 10 names CU2b.
- **O7:** R5's single floor.
- **O8: a doctored-load trap.** `ReconcileReservations()` also reconciles the flag. If `bCurrencyIssued` is true, the market has no issue, and no Issue entry is pending, the flag is cleared with a Warning. In the converse case (issued in the market, the flag false and no withdrawal pending), it is set.
- **O9: 3.2's text.** The live and recorded tension differ through the clamps at 0 and at 100 (each by at most one sol's drift), and while an act is live but its sol has not closed (up to the act's size, 45 points at most).
- **O10: Meridian** can join while undiscovered. It is harmless, because availability gates booking. The deck lists a member only once it is available, while the log line, which is not player-facing, names it. Q-B (b)'s "no partner" now reads "no partner, unless a discovered dormant neighbour (Meridian) is a member".
- **O12: Greed.** The join rule requires an economy, so Greed (Zarya's row copied, `BlocStance` 20, no economy) never joins. Section 4's "an injected row defaults to −1" applies only to rows built without the column.
- **O13: overflow.** The bound uses HabSegment's 900,000 mmk per unit: 8 × 3 × 1,000 × 4 × 900,000 ≈ 8.6 × 10^13, and × 10^4 ≈ 8.6 × 10^17. That is inside int64 (9.2 × 10^18).
- **O15:** the `-ending` anchor is C:1797.
- **O18: a late journal entry under the catch-up.** After a harness clock jump, E4 R3(b)'s catch-up can close a sol before a later-queued act due in it runs. The act then applies at the next roll (entries with `ClearSol` ≤ the closing sol). This happens only after a harness jump, and identically in both twins.
- **O19:** once E4 is built, the builder re-checks every `-market` line that calls its block "last" or prints a size or offset, and lists any that move.

**The director questions, restated** (they replace the ones above). The model: the first demand at sol 100.0, each answered half a sol later; the standing trade to Zarya every round trip. A Q3 round's on-time award is 14,000 kg × the multiplier, at 1 cr a kg, leaving out research funding.

**Q-A. How hard does Earth react when the colony issues the mark?**

| Option | Tension after a `DefyIssue` at sol 100.5 | Earth's next demand | If later demands are ignored: embargo, then defection | Multiplier | A Q3 round's award | Earth pays for Struct / Oxygen / Hydrogen (cr/kg) | With a Defy at every demand, the Martian ending path (identity +60) |
|---|---|---|---|---|---|---|---|
| a plain Defy, no mark (for comparison) | 48.3 | sol 120.0 | 145.0, then 153.0 | 0.735 | 10,290 cr | 0.675 / 2.880 / 5.820 | sol 140.5 |
| **(a) tension +10, multiplier −0.10 (recommended)** | 58.3 | sol 103.3 | 128.3, then 136.3 | 0.605 | 8,470 cr | 0.025 / 1.840 / 4.260 | sol 123.8 |
| (b) tension +20, multiplier −0.20 | 68.3 | at once | 111.7, then 119.7 | 0.475 | 6,650 cr | refused / 0.800 / 2.700 | sol 107.2 |
| (c) tension +10 only | 58.3 | sol 103.3 | 128.3, then 136.3 | 0.705 | 9,870 cr | 0.525 / 2.640 / 5.460 | sol 123.8 |

- **The rate.** With no trade and a Defy at every demand:
  - (a) and (c) are the same: 0.958 at sol 105, 0.922 at 120, 1.009 at 140 and 1.269 at 199;
  - (b): 0.958 at sol 105, 1.015 at 120, 1.124 at 140 and 1.275 at 199.
- **If demands are ignored,** the mark sinks to 0.537 under (a) and 0.521 under (b) by sol 199.
- **(a)** costs about 1,800 cr a Q3 round, and closes Earth's market for Struct, without forcing the next crisis at once.

**Q-B. Can the mark be undone?**
- **(a) (recommended): a Comply withdraws it.**
  - In the modelled case the colony issues at sol 100.5, defies at 103.8, then complies at the next demand (123.8). It holds 4,967 marks, about 828 of them still owed by the convoy on the road.
  - At the rate in force during sol 123 (1.050), they are redeemed for **5,215 cr**, all at once.
  - Martian prices are re-expressed in credits ×1.050.
  - Earth's penalty ends: the multiplier returns to 0.834, and a Q3 round pays **11,677 cr**.
  - Trade pauses until the withdrawal roll, at most two sols. The colony may issue again later, but every route the Comply closed stays closed.
- **(b): it is permanent.**
  - The Comply still closes every active neighbour's route (HEAD), so the 4,967 marks have no partner and no desk, unless a discovered dormant neighbour (Meridian) is a member.
  - The penalty stays for the rest of the game: the multiplier is 0.734, and a Q3 round pays **10,277 cr**, 1,400 cr less every round.

**Q-C. Can marks and credits be exchanged?**
- **(a) (recommended): yes, at the bloc's desk, with a 3% spread each way and at most 500 a sol each way.**
  - **The cap costs an honest trader nothing.** The standing trade's 20,698 marks (sols 100-199), each run converted as it arrives, become **25,234 cr**, against 25,227 uncapped and 20,698 cr if the mark is never issued (+21.9%). That gain is the mark's rise from 1.0 to 1.44.
  - A heavy trader selling 1,000 kg of Struct to Zarya every round trip floods Zarya's store to its 1.25 floor. It converts 31,250 marks to **40,354 cr**, against 40,367 uncapped and 31,250 never issued (+29.1%).
  - **The cap binds only on speculation.** A modelled plan turns the 17,540 cr earned by sol 99 into marks at 500 a sol from the desk's opening (sols 102-137), then back from sol 150. It gains **3,409 cr (+19.4%)**.
- **(b): yes, with no cap.**
  - The same trades convert as in (a).
  - Speculation is bounded only by the rate: the same 17,540 cr turned into marks at the low (0.961, in force during sol 103) and back at sol 199 (1.430) gains **7,018 cr (+40.0%)**.
- **(c): no desk.** Marks buy only from bloc members. The standing trade leaves **20,698 marks** and the heavy trade **31,250**, which Earth will not take. Once Zarya and Meridian have joined, no neighbour trades in credits, so the rate is a display only.

**The verdict after revision.** The review's verdict was **CLEARED WITH CONDITIONS**. R1-R6 write in all six required changes, and R7 takes observations 1-19 (11 and 14 in R6). **This revision has not been re-reviewed.** R1 (the paired run's schedule) and R5 (the withdrawal) are new mechanisms; a short second pass on them is advisable before the build.

The conditions left:
- the director's answers to Q-A, Q-B and Q-C;
- E4 built (v54). E5 takes the next free version, and the builder rebases every E4 name onto E4 as built (risk 8);
- the playtest claim re-checked at the build's HEAD (5.2).

---

# PROFESSOR RE-REVIEW (2026-10-24)

A short independent re-review of R1 and R5, with spot checks of R6 and risk 11. It is read-only. The code was read with `git show HEAD:` at `e07e384`, which changes no source since `4edd7fd`. Every number below comes from the reviewer's own integer model, written for this pass and not copied from the REVISION. The model implements E4 R5's update with Zarya's placeholder data, and E5's 3.2-3.3.

**Verdict: CLEARED WITH CONDITIONS**

R5's mechanism is sound, and R6's numbers are right. R1 fixes the storm, Influence and credits, and it now bites mutations 1-4. It does not bite mutation 5, though, and one clause of its bar fails on its own boundary order. R5 leaves out several statements a builder needs.

## What holds (checked)

- **R6 reproduces to the digit.**
  - **No trade, a Defy at every demand:** 0.980, 0.961, 0.960, 0.958 and 0.949; the low is 0.916 at sol 122; then 1.009 and 1.269.
  - **Demands ignored:** 0.849, 0.774, 0.684 and 0.537.
  - **With the standing trade:** TradeBp is 1,655-3,311; the low is 0.961 at sol 102; then 1.027, 1.058, 1.178 and 1.442.
  - **Q-B:**
    - six mark runs (`ClearSol` 103-123) × 827.9 = 4,967.4 marks;
    - the rate in force during sol 123 is 1,050 mcr;
    - floor(4,967,400 × 1,050 / 1,000) = 5,215,770 mcr, which is **5,215 cr**.
  - **Q-C:**
    - (a) speculation: 16,009 marks in over sols 102-137 and back over sols 150-182 gains **+3,408.5 cr (+19.43%)**;
    - (b): 0.961 in and 1.430 out gains +7,017.6 cr (+40.0%);
    - the honest trader converts to 25,233.6 cr capped and 25,226.6 cr uncapped.
  - **E4:** 17,540.35 cr by sol 99, and 827.9 cr a run.
- **What is right in R1.**
  - **The window.**
    - With `WeatherCycleSols` 0, `GetActiveEvent` reads the plain sol (W:2595-2614).
    - Sols 16-36 hold no DustStorm.
    - Flare_1 (sol 17) is a SolarFlare. Only a DustStorm freezes a convoy (W:9745).
  - **The live politics.**
    - Tension is 59 at sol 16.0, so the demand comes at 17.67.
    - The `DefyIssue` takes it from 61.1 to 59.1, and the second demand comes at 21.0.
    - The boundary Defy takes it from 63.0 to 51.0.
    - At sol 30.0 it is 53.4 + 25 = 78.4, so Zarya is embargoed in both twins (relation 50 or more, against Kestrel's 26 or less).
    - The Pacify at 30.5 has 40 or more Influence against the 20 it needs.
  - **The schedule.** Every act can run, and each rival's convoy is home at least 1.4 sols before its next order.
  - **The mutations.**
    - Mutations 1, 2 and 4, and R2's mutation 3, split the twins as stated.
    - The era twin's step ending 31,230 s runs the boundary Defy (`ExecuteAt` 31,210) before the roll closing 25, because `StepUplink` (W:844) runs before `StepCityGrowth` (W:855).
  - **The bar** compares the right market quantities, and correctly leaves out reservations.
- **What is right in R5.**
  - **No overdraft.** The withdrawal only pays, through `CreditPay`.
  - **No order clears at a re-expressed price.** Every booking is refused from the Comply on, and the roll waits until no order is uncleared.
  - **The single floor is band-exact.** It is taken over the balance plus every on-road `OwedMmk`. Owed marks move from the road into the balance at the return without changing the sum.
  - **A save mid-withdrawal is safe.**
    - O8's converse case excludes `bWithdrawPending`, so a mid-withdrawal file is not "repaired".
    - `ReconcileReservations` needs nothing new, because R5 adds no reservation.
  - **"At most two sols"** holds.
- **Risk 11 is safe in every path.**
  - **The value at the bound.** It is 2.8e-17 in IEEE (1 − 0.6 − 0.3 − 0.1), and the clamp keeps it at 0 or above (W:10066).
  - **The award.** 14,000 × 2.8e-17 kg floors to 0 mcr, and `CreditPay` ignores 0 or less (W:13356).
  - **E3's sales.** Mpm = llround(M × 1000) = 0 (X:170), so every line is refused at X:201. Nothing divides by M.
  - **The quota ladder cannot stall.**
    - The manifest no longer reads the award (W:13393-13429).
    - The launch checks neither the award nor the items (X:265-312).
  - **The deck** guards `Award > 0` (K:700).
  - **The bot** never answers a demand, so its identity stays 0 and M stays at 0.7 or more.

## REQUIRED CHANGES

1. **R1: mutation 5 is not bitten, and the order meant to bite it fails the home-sol bar.**
   - **Why the order cannot bite.**
     - Zarya's `MemberFromSol` is 21 with or without the mutation (the issue roll, 19, + 2), so the `ClearSol` 20 order settles in credits in both twins.
     - The rate in force during sol 20 is 1,000, so the mutation's conversion is the identity and the charges are equal.
     - R7 O4's claim is therefore false. Only CU3's direct check kills mutation 5.
   - **The same order comes home in different sols.**
     - The era twin runs it on the step ending 24,030 s. `StepTrade` credits that whole 60 s step (W:9560, :9751; the clock advances before `EraStep`, C:1161-1162).
     - So the era convoy is home at the step ending 28,770 s, sol 23.975. The agent convoy is home at about 28,810 s, sol 24.008.
   - **Change.**
     - Replace the order with an `Exchange ToMarks` due 10 s after the sol-20 boundary. It is refused in both twins, and accepted only in the agent twin under mutation 5.
     - State that desk check 3 counts a rival admitted at an earlier roll, whatever its `MemberFromSol`. Otherwise check 3 masks the bite.
     - Exempt orders booked on the era twin's boundary step from the home-sol clause. Kestrel's `ClearSol` 27 order passes it only because the era band's sums of 0.05 sol overshoot by one step: a 1-sol trip takes 21 steps.
2. **R1: the purse sizes break R1's own "at most half the spendable" assertion.**
   - **The desk orders.** `ToMarks 300` clears at 0.961 for about 302.8 marks, so `ToCredits 200` reserves 66% of the spendable marks.
   - **The Kestrel mark order at 29.5** is booked at the very instant Zarya's 25.5 convoy comes home.
     - In the era twin the convoy is home at the step ending 35,370 s, before the booking.
     - In the agent twin it comes home on the booking step, after `StepUplink`, or later.
     - So the spendable marks differ by Zarya's owed marks.
   - **Change.**
     - Use `ToMarks 500`.
     - Size every mark reservation against the marks held without any convoy due home within a sol of the booking, or move the Kestrel order off 29.5.
     - Add this as a third rule beside E4 R7's two.
3. **R1: two fixture conditions that are not stated.**
   - (a) **No credits from Earth in the window.** The end-balance bar holds only if no E2 award, launch or sale lands in sols 16-36. Section 6 lists the multiplier read at an award as band-dependent. Assert that none lands.
   - (b) **The bloc tally counts `bMarks` orders only.** Membership is decided at booking and is band-exact; "orders to a member" is not a safe definition. At the roll closing 20, Zarya is admitted, yet the sol-20 order settles in credits.
4. **R5: the mark purse is never emptied.**
   - R5 replaced 1.5's list, and `MarkSpend(MarkBalanceMmk)` went with it. Nothing now debits the balance, so a later re-issue would hand back marks already redeemed.
   - **Change.**
     - Step 2 does `MarkSpend(MarkBalanceMmk)`.
     - It sets every on-road `OwedMmk` to 0, and drops those convoys from the saved mark-order list.
     - It asserts `MarkReservedMmk == 0`.
     - CU9 checks a zero balance after the withdrawal, and that a re-issue starts from 0.
5. **R5: step 4 leaves state live, and one path has no rate.**
   - **What step 4 must also reset.**
     - It sets `CurrencyOpenSol` to −1, and resets the members and the bloc tally.
     - "Cleared" means `RateMcr` = R0.
     - Every conversion is gated on the market issue, not on `CurrencyOpenSol` alone.
     - Otherwise, if "cleared" is 0, a credit order after the withdrawal passes `CurrencyOpenSol ≤ ClearSol` and is charged `ceil(Net × 0 / 1000)` = 0.
   - **An issue and a withdrawal at the same roll.**
     - This is reachable: a `DefyIssue` followed by a Comply in one sol. `DefyIssue` leaves tension at 60 or more whenever the demand has waited 3.3 sols or more, and always under Q-A (b).
     - Then "the rate in force during the closing sol" does not exist. Define r = R0 for this case. With r = 0, step 3 would re-express every price to V/4.
   - **Two more things to name.**
     - The trigger reads "no uncleared **priced** order". A barter convoy never clears, and would otherwise hold `bWithdrawPending` for ever.
     - O8 also clears a `bWithdrawPending` that has neither a market issue nor a pending Issue entry, as a journal-reset file can have.
6. **R4: the cut order needs a key the convoys do not have.**
   - "Convoys and desk orders together, by `CommandId`" cannot be computed, because E4's convoy record (E4 3.5) holds a convoy `Id`, not a `CommandId`.
   - **Change:** store the `CommandId` on the convoy, or define the order explicitly. For example: desk orders by `CommandId` descending, then convoys by `Id` descending.
7. **R5's parity claim has no paired test.** "The bands cannot split by a milli-credit" is checked only in CU9, which runs one band.
   - **Extend CU8 with a withdrawal:**
     - A third demand is pending in both twins from sol 30.0 (78.4 ≥ 60), and the Pacify does not clear it.
     - Book Zarya's last mark order 10 s after the sol-31 boundary, in place of the one at 31.5. It is home at about sol 34.975 in the era twin and about 35.008 in the agent twin.
     - Comply at 34.5. The withdrawal roll (closing 34) then sees that convoy home in one twin and still on the road in the other.
   - **The bar.** Hold the redeemed credits identical.
   - **A new mutation.** "On-road `OwedMmk` left out of the total" must fail this check.

## OBSERVATIONS (non-blocking)

1. **The 16-sol jump.**
   - If `cu8_start` is saved before the catch-up roll, the first roll after load adds 16 drifts to the synced 59,000.
   - The record then reads 68.6 against a live 59.0 for the whole run.
   - Parity is unaffected. Save after one step, or sync after the first roll.
2. **The withdrawal is an exit with no cap and no spread.**
   - Q-C (a)'s speculator can Comply at a demand instead of using the desk:
     - at the sol-183 demand, 16,009 marks redeem at 1.411 for +5,049 cr;
     - at the sol-163 demand, the gain is +3,128 cr;
     - through the desk, it is +3,409 cr.
   - The cost is a Comply. Put this beside Q-B for the director.
3. **The 17,540 cr stake** assumes the first dispatch at sol 0.5. At sol 0.0 the same model gives 17,477.55, the draft's 17,478. Write the phase into the model's assumptions.
4. **M is never exactly 0.0.** At the bound it is 2.8e-17. A test must assert the award (0 mcr) and Mpm (0), not M == 0.0.
5. **E4's MX8 has the same hazard as change 1.** Its boundary order (16.0 + 10 s, to Kestrel) passes the home-sol bar only by the same one-step overshoot.
6. **Re-express every slot, not only the traded ones.** An untraded slot keeps its mark integer, and would later be read as credits.
7. **Reopening is a second acceptance edge.**
   - An order due 10 s after the withdrawal roll's boundary is refused in the era twin, which runs it before the roll, and accepted in the agent twin.
   - Section 6 allows this. CU9 should name the band it runs in.


# DIRECTOR ANSWER (2026-10-24)

The numbers were verified independently by the re-review (Q-B 5,215 cr; Q-C +19.4% / +40.0%).
- **Q-A (a):** issuing the mark adds tension +10 and multiplier -0.10 while the mark exists.
- **Q-B (a):** a Comply withdraws the mark, redeems the marks at the rate in force, and ends the penalty. The re-review's note that a Comply redeems with no spread and no cap (about +5,049 cr for a speculator, against +3,409 via the desk) was put to the director.
- **Q-C (a):** the bloc's exchange desk has a 3% spread each way and at most 500 a sol each way.

The re-review's conditions 1-7 are applied by the builder.
- **Loophole (a), accepted:** a Comply redeems the marks with no spread and no cap. The political cost (every neighbour route closes and stays closed) is the price. No rule is added.
