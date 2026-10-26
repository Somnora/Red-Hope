# E5 BUILD BRIEF: the Martian currency (the mark), save v56, frame 'RHE5'

Prepared read-only at HEAD `d842dd0` (save v55) with `git show d842dd0:` only. Nothing was built or run.
Spec: `docs/qa/2026-10-24/e5-martian-currency-design.md` (1,105 lines; read ALL of it if anything below is unclear).
Authority inside the spec: DIRECTOR ANSWER > REFRESH AT HEAD 31ffa6f > PROFESSOR RE-REVIEW conditions 1-7 > REVISION R1-R7 > PROFESSOR REVIEW > design sections 0-10.
This brief resolves every conflict between those layers. Where it says UNCERTAIN, the evidence is given and the builder picks the stated default unless the orchestrator overrides it.

Short names: W = `Source/RedHopeSim/Private/RHSimWorldSubsystem.cpp`, H = `Source/RedHopeSim/Public/RHSimWorldSubsystem.h`, M = `Source/RedHopeSim/Private/RHSimMarket.cpp`, G = `RHSimGrowth.cpp`, C = `RHSimCommandlet.cpp`, D = `RHDefinitionsSubsystem.cpp`, K = `Source/RedHope/Private/RHCommandDeckPanels.cpp`, P = `RHPlaytest.cpp`, X = `RHSimExport.cpp`, MS = `RHSimMarketSuite.cpp`, XS = `RHSimExportSuite.cpp`.

---

## 0. Preconditions

1. The visual pass is committed (its builder is editing presentation files in the working tree now: `RHCommandDeckPanels.cpp`, `RHClickTest.cpp`, the visualizers). Start E5 only after that commit. Re-anchor every K and click-test line at that commit; sim anchors (W, H, M, G, C, suites) are unaffected by the visual pass, which is presentation-only.
2. Battery baseline to diff against: the newest `Saved/baselines/battery_post_*` the visual pass leaves. Today that is `battery_post_tram` (Oct 26 10:47); the handoff names `battery_post_s2fix`, which is older. Both are v55, 58 runs. Use the newest.
3. Machine rules: one Unreal or build process at a time; never git stash/checkout/reset; the builder does not commit (the orchestrator does).

## 1. Authoritative decisions (one list)

Director (2026-10-24):
- **Q-A (a):** issuing adds Earth tension +10 at once and lowers the requisition multiplier by 0.10 while the mark exists (`bCurrencyIssued`).
- **Q-B (a):** a Comply withdraws the mark, redeems all marks at the rate in force with no spread and no cap, and ends the penalty. The loophole (about +5,049 cr via a Comply against +3,409 cr via the desk) is ACCEPTED. Add no rule.
- **Q-C (a):** the bloc exchange desk takes a 3% spread each way (`DeskSpreadBp` 300) and at most 500 a sol each way (`DeskCapPerSol` 500).

Orchestrator (handoff 2026-10-25, applying the REFRESH):
- After the issue, the market's unit is marks. So `FRHConvoy::ValueMcr` holds milli-marks for every priced order that clears in a sol at or after `CurrencyOpenSol`. The goodwill floor (`GoodwillMinValueMcr` 300,000 at M:1237) is compared in the market's unit for **every** priced order, with no conversion. This closes the REFRESH's finding F1, option 1. The boundary is the open sol, not the issue act: before `CurrencyOpenSol` every order is E4's, exactly.
- Add a transient `bMarketBlockLost` flag (section 4.6 below).
- Save v56. A new frame, `'RHE5'`, is written after `'RHE4'` as the last frame.

Binding design (the REVISION and the re-review conditions, applied):
1. **Two issue acts, both uplink orders:**
   - `Currency` with Target `Issue`: needs the live `IdentityAxis` >= `CurrencyIssueIdentity` (20).
   - `Solidarity` with Target `DefyIssue`: runs the whole Defy (`ResolveDilemma(false)`), then issues, at any identity. It needs a pending demand.
2. **`CanIssueCurrency`, in this order:**
   1. `The mark is already issued` (`bCurrencyIssued`);
   2. `The mark is being withdrawn` (`bWithdrawPending`);
   3. (R7 O1) at least one rival with an economy and an open route (not in `ClosedRoutes`) whose `BlocStance` is met by the identity after the act (live identity for Issue; live + `DilemmaDefyAxisShift`, clamped, for DefyIssue): `No neighbour would join the mark`;
   4. Issue only: `The colony does not lean Martian yet (identity %+.0f, needs %+.0f)`.

   `DefyIssue` checks these before its Defy runs; a failure leaves nothing done.
3. **The issue is live at execution:** tension +10 (clamped to 0..100), `bCurrencyIssued = true`, one alert, and the log line `=== CURRENCY ISSUED: the mark (identity %+.0f, tension %.0f, requisition x%.2f) ===`. It does not touch identity, HumanNature, Influence, relations, Hope or the endings.
4. **The market issue happens at the roll closing the act's `ClearSol` s.** It sets `CurrencyIssueSol = s`, `CurrencyOpenSol = s + 2` and `RateMcr = 1000`. The first rate step is at the roll closing s + 1. Admissions run from the issue roll inclusive.
5. **An order's currency is fixed at booking** from its clearing sol c = max(floor(ExecuteAt / 1200), `MarketCheckedSol`).
   - It settles in marks iff `CurrencyIssueSol >= 0`, `CurrencyOpenSol <= c`, and its rival is a member with `MemberFromSol <= c`.
   - Otherwise, if `CurrencyIssueSol >= 0 && CurrencyOpenSol <= c`, it is a **converted credit order**.
   - Otherwise it is E4's, exactly.
   - Every conversion is gated on the market issue, never on `CurrencyOpenSol` alone (re-review 5).
6. **Membership:** at each roll from the issue roll on, walk `RivalEconomies` (name-string order, never FName order). A rival joins if it has `bHasEconomy`, is not yet a member, and has `0 <= BlocStance <= IdentityViewMilli / 1000`. It gets `MemberFromSol` = the sol being closed + 2 and the log line `Bloc: %s joins the mark (stance %+d, identity %+d)`. The rule reads no relation, availability, embargo or defection. A member leaves only when the bloc dissolves. Hostility is still a booking refusal with HEAD's texts.
7. **The rate** is an integer in mcr per mark, stepped once per roll after the issue roll by section 3.3's formula, read from the **political record**. It never reads the live `IdentityAxis` or `EarthTension`. It does NOT step at the withdrawal roll.
8. **The political record** (`IdentityViewMilli`, `TensionViewMilli`, the pending journal) runs from sol 0, issued or not. It is silent: it logs nothing, and nothing reads it before the issue.
   - **Journal entries**, appended when the act's worker succeeds, keyed (`ClearSol` = floor(ExecuteAt / 1200), `ExecuteAt`, `CommandId`, sub-index):

     | Act | Identity effect | Tension effect |
     |---|---|---|
     | Defy (and DefyIssue's Defy half) | set to `llround(IdentityAxis * 1000)` after the act | -12000 |
     | Comply | set to `llround(IdentityAxis * 1000)` after the act | -45000 |
     | Pacify | none | -20000 |
     | Issue (and DefyIssue's issue half) | none | +10000 |
     | Withdraw (only with a Comply while `bCurrencyIssued`) | none | none |

     The tension deltas are -DilemmaDefyTensionDrop×1000, -DilemmaComplyTensionDrop×1000, -PacifyTensionRelief×1000 and +CurrencyIssueTension×1000.
   - **At each roll, for the sol(s) closed:**
     - add the drift, `llround(EarthTensionDriftPerSol * 1000)` = 600 per sol closed, only while any row is `SliceActive` (`Defs->ForEachRival`, D:592-606), capped at 100000;
     - apply every entry with `ClearSol` <= the last sol closed, in `(ExecuteAt, CommandId, sub)` order, clamping tension to [0, 100000] after each entry;
     - remove the applied entries.
9. **The desk** (`Exchange`, Target `ToMarks` or `ToCredits`, Value = whole units of the source currency, at least 1):
   - It reserves exactly the amount in the source purse.
   - It clears in `RollMarket` after the convoys, by order Id, at the rate r in force during the clearing sol, spread S = 300:
     - ToMarks pays `floor(A × 1000 / r × (10000 − S) / 10000)` mmk;
     - ToCredits pays `floor(A × r / 1000 × (10000 − S) / 10000)` mcr, and `DeskCreditMcr` grows by the payout.
   - Desk flows never enter the bloc tally.
   - The desk order's Id is its `CommandId`.
10. **The withdrawal (R5 + re-review 4, 5):**
    - **From the Comply's execution, while `bCurrencyIssued`:**
      - `bCurrencyIssued = false` (the penalty ends);
      - `bWithdrawPending = true`;
      - a Withdraw journal entry joins the Comply's;
      - booking refuses every priced order and every desk order: `The market is closed while the mark is withdrawn`.
    - **The withdrawal runs at the first roll at or after the Comply's `ClearSol` with no uncleared priced convoy and no pending desk order.** It runs after the clearing and the reprice, and the rate does not step at that roll. In this order:
      1. r = the rate in force during the closing sol. If the issue and the withdrawal fall on the same roll, r = R0 (1000).
      2. Redeem `floor((MarkBalanceMmk + Σ OwedMmk of mark convoys still on the road) × r / 1000)` mcr through `CreditPay`, into the new lifetime total `CurrencyRedeemedMcr`. Then `MarkSpend(MarkBalanceMmk)`, set every on-road `OwedMmk` to 0 and drop those convoys from the mark list. Assert `MarkReservedMmk == 0`.
      3. Re-express **every** slot in every region: `Price = clamp(Price × r / 1000, FloorMcr, CeilMcr)`.
      4. Dissolve the bloc (members and the tally reset), set `RateMcr = R0`, `CurrencyIssueSol = CurrencyOpenSol = -1` and `bWithdrawPending = false`. Skip admissions at this roll. Booking reopens.
    - Log `=== CURRENCY WITHDRAWN: %s marks redeemed for %s cr at %s ===`.
11. **Reservation invariants (R4):**
    - `CreditReservedMcr == Σ uncleared credit-convoy ReservedMcr + Σ pending ToMarks amounts`, and it is at most `CreditBalanceMcr`.
    - `MarkReservedMmk == Σ uncleared mark-convoy ReservedMmk + Σ pending ToCredits amounts`, and it is at most `MarkBalanceMmk`.
    - Both are recomputed only in one post-load step, `ReconcileReservations()`, run after the currency frame is read. The cut order is desk orders by `CommandId` descending, then convoys by `Id` descending.
    - Lifetime totals: `0 <= SalesPaidMcr + MarketSalesMcr + DeskCreditMcr + CurrencyRedeemedMcr <= CreditAwardedMcr`.
12. **Rounding is always against the colony:** charges ceil, payments floor. Integer only, with `MarketDivTrunc` for every signed division. No float or double enters the rate, the conversion or the desk.
13. **Inertness:** while unissued, `GetRequisitionMultiplier` gives HEAD's result bit for bit. Every order is E4's, and nothing logs. The harness switch `bCurrencyEnabled` (default true, never saved; `Debug_SetCurrencyEnabled`) turns off ALL E5 code: the record walk, the journal appends, the admissions, the rate and both issue acts (refused with `No currency: disabled`). The frame is still written.

## 2. Code facts at HEAD that the builder needs (verified, not taken from the doc)

- `RHSaveVersion = 55` is at W:37, and its comment heads the version history. The strict check is at W:13968 (`Version != RHSaveVersion`).
- The save tail is W:13926-13933: `RHSaveFrame::Write` for Ledger, Credit, Export, then `SyncRivalEconomies(false)`, then `MarketTag` at W:13933.
- The load tail is W:14739-14743: `ReadLedgerBlock`, `ReadCreditBlock`, `ReadExportBlock`, then `ReadMarketBlock(Ar, Bytes, LoadedConvoyRival, bLoadedConvoyReturning, LoadedConvoyLegSols)` at W:14743.
- `RHSaveFrame.h`:
  - tags at lines 19-22 (`MarketTag = MakeTag('R','H','E','4')` on line 22);
  - `Write`, `Read`, `ReadName`, `Find` and `Reseal`;
  - header 12 bytes and footer 8 bytes.
- `RollMarket` is M:509-558, called from G:489 (`StepCityGrowth`, after `RollEconomyLedger` at G:488).
  - The early return comes when `SolNow <= MarketCheckedSol`, and `First = MarketCheckedSol`.
  - `Passed = min(SolNow - First, 8)` (M:520).
  - The per-pass loop is M:524-552: zero the ring head (M:530-537); `ClearConvoys(ClosingSol, DueUpTo)` (M:539), where `DueUpTo = SolNow - 1` on the last pass; advance the ring (M:540-544); then `MarketProduceAndReprice` **only if `bLive`** (M:545-551).
- `ClearConvoy` is M:638-736.
  - The price is the slot's price in force, and `Net = BuysValue - SellsValue` in the market unit.
  - `Allowed = max(0, Balance - (Reserved - own))` (M:687).
  - `OwedMcr = -Net` (M:693) and `ValueMcr = Buys + Sells` (M:702).
- `GetOrderReservationMcr` (M:826-847) uses `MktHiQuote` and `MktLoQuote` (file-local, M:33-34). `MktLoQuote` floors at 0; E5's `LoR` floors at 1.
- `PlanConvoy` (M:849-1020): HEAD's refusals come first (contact, barter, already out, convoys out, closed route, sabotage, defection, embargo). The money refusal at M:1017-1021 reads `GetSpendableMcr`.
- `BookConvoy` (M:1041-1102):
  - `ClearSol = floor(ExecuteAt / 1200)`;
  - the catch-up is `if (bMayPrice && ClearSol > MarketCheckedSol + 1) RollMarket(ClearSol)` (M:1051-1054);
  - `CreditReservedMcr += Plan.ReserveMcr` (M:1087).
- `StepTrade` (M:1106-1163): when the rival row vanishes, it releases `CreditReservedMcr` (M:1124-1127). A convoy holds at the end of its last leg until it has cleared.
- `StepConvoyReturn` (M:1165-1262): `CreditPay(OwedMcr)` and `MarketSalesMcr += OwedMcr` (M:1227-1232); the goodwill test at M:1237; `Influence += InfluencePerTrade` at M:1245.
- `ReadMarketBlock` (M:1362-1729):
  - It resets state (M:1370-1378).
  - `bFound` (M:1382-1386): a lost frame gives a Warning.
  - A malformed payload sets `!bParsed` (M:1479-1483), then the start values and an **early `return`** (M:1484-1508).
  - A bad slot table sets `bSlotsBad` (M:1524-1529); it drops the markets and stores.
  - A repeated region sets `!bRegionsOk` (M:1568-1572).
  - The lifetime-sales clamp is M:1702-1708, and the reservation recompute, cut and "disagrees" Warning is M:1709-1728.
  - Every Warning text says `LoadColony v55:` (hard-coded). KEEP those texts: 47 of them print in `market.log`.
- `WriteMarketPayload` (M:1280-1342) writes per convoy: `Id`, `Rival`, `Returning`, `Leg`, `Standing`, `ClearSol`, `Cleared`, `ReservedMcr`, `OwedMcr`, `ValueMcr` and the lines. There is no `CommandId` and no currency. Do not change this payload.
- `CreditSpend` (M:45-56) pays `min(Mcr, Balance)` and returns the rest. `Debug_ZeroCredits` (M:58-71) refuses while `CreditReservedMcr != 0`.
- `MarketDivTrunc` is at M:37.
- `InitMarket` (M:76-99) is called at W:511. It shows the config clamp pattern.
- `FRHConvoy` is H:550-564. The public market API is H:1561-1621 (`GetSpendableMcr` is at H:1589, and the `Debug_*` politics hooks at H:1617-1621). The private market state is H:3837-3906.
- Politics:
  - `ExecuteCommand` branches: `Convoy`/`MarketOrder` at W:9555-9566, `Solidarity` at W:9568-9578 (`ResolveDilemma(Cmd.Target == "Comply")`) and `Pacify` at W:9611-9619.
  - `HasActiveRival` is W:9741-9746.
  - `GetRequisitionMultiplier` is W:9942-9957.
  - `StepEarth`'s drift is at W:9970 and the demand at W:9974-9980.
  - `ResolvePacify` is W:10151-10172. `ResolveDilemma` is W:10347-10411: Comply closes the routes at W:10365-10373, identity and tension change at W:10378-10379, and the Defy at W:10392-10393 and W:10397.
  - The saved politics are W:13706 (write) and W:14358 (read).
  - `ClosedRoutes` is saved at W:13710 and loaded at W:14365-14366.
- `ResolveDilemma` and `ResolvePacify` are called only from the uplink worker (W:9573, W:9615). `RH.Solidarity` (RHCheats.cpp:912-930) and `RH.Pacify` (RHCheats.cpp:1020-1030) enqueue uplink orders. So journaling in the worker covers every in-play path.
- `FRHCommand` (H:26-51) holds `CommandId` and `ExecuteAtSimSeconds`. `NextCommandId` is **not saved**: a load only raises it past the queued orders (W:14065). See section 4.7.
- `FRHRivalRow` is `Source/RedHopeSim/Public/Data/RHRows.h:398-422`. `docs/data/RH_Rivals.csv` column order is `Name,DisplayName,Nation,DistanceKm,ExportLot,ImportLot,RegionId,Produce,Consume,StoreStart,StoreCap,RelationStart,SliceActive,Notes`. Notes is quoted and contains commas.
- Data sync: `scripts/unreal/rh_sync_datatables.py` (E4 synced `DT_Config` and `DT_Rivals` with it).
- `RH.InjectRivals` (RHCheats.cpp:969-1000) sets each column of Zarya and Meridian explicitly.
- Greed copies Zarya's row (C:1118).

## 3. Changes per file (HEAD anchors)

### 3.1 `Source/RedHopeSim/Public/RHSaveFrame.h`
- After line 22, add `constexpr uint32 CurrencyTag = MakeTag('R', 'H', 'E', '5'); // E5, the Martian currency (v56)`.
- Update the header comment (line 11 mentions E6's 'RHL6').

### 3.2 `Source/RedHopeSim/Private/RHSimWorldSubsystem.cpp` (W)
- **W:37:** set `RHSaveVersion = 56`. Prepend a v56 sentence describing the `'RHE5'` frame and its layout (section 5).
- **W:511, beside `InitMarket()`:** call `InitCurrency()`. It reads the config rows with their clamps (section 3.7), resets all E5 state and seeds the record from the live values: `IdentityViewMilli = llround(IdentityAxis*1000)` and `TensionViewMilli = llround(EarthTension*1000)`.
- **W:9555-9566 (the Convoy / MarketOrder branch):** add two sibling branches.
  - `Currency` (Target `Issue`): run `CanIssueCurrency`, then the issue. Journal an Issue entry (sub 0).
  - `Exchange`: the desk worker (section 3.3).
- **W:9568-9578 (Solidarity):**
  - `Target == "DefyIssue"`:
    1. If no demand is pending, refuse with HEAD's text `There is no demand to answer right now.` (UNCERTAIN order, see problem M8: check the demand first).
    2. Run `CanIssueCurrency` checks 1-3.
    3. Run `ResolveDilemma(false)`.
    4. Journal the Defy entry (sub 0).
    5. Issue, and journal the Issue entry (sub 1).
  - `Comply`: after success, journal the Comply entry (sub 0). If `bCurrencyIssued` was true, also set `bCurrencyIssued = false` and `bWithdrawPending = true`, record the withdrawal's `ClearSol`, and journal a Withdraw entry (sub 1).
  - Anything else: Defy as at HEAD, then journal the Defy entry.
  - Journaling needs the command's `ExecuteAt` and `CommandId`, so do it here, or pass the command into `ResolveDilemma` and `ResolvePacify`. E6's design (section 4, line 407) expects the latter for `ResolveCovert`/`Sabotage`/`Launder`; either form is acceptable.
- **W:9611-9619 (Pacify):** journal a Pacify entry after `ResolvePacify` succeeds.
- **W:9942-9957 (`GetRequisitionMultiplier`):** keep HEAD's statements. Only inside `if (bCurrencyIssued)` subtract `CurrencyRequisitionPenalty` from `Mult` before the same clamp. With the mark unissued, the function computes HEAD's value in HEAD's operation order (CU1b pins it bit for bit).
- **W:13933 (after the `MarketTag` write):** `RHSaveFrame::Write(Ar, RHSaveFrame::CurrencyTag, [this](FArchive& Block) { WriteCurrencyBlock(Block); });`
- **W:14743 (after `ReadMarketBlock`):** `ReadCurrencyBlock(Ar, Bytes);` then `ReconcileReservations();`. E8 later reads its own frame after these.

### 3.3 `Source/RedHopeSim/Private/RHSimMarket.cpp` (M), or a new `RHSimCurrency.cpp` (recommended)
- **`RollMarket` (M:509-558):**
  - After `ClearConvoys` (M:539), add `ClearDeskOrders(ClosingSol, DueUpTo)` (by Id, at the current `RateMcr`).
  - The bloc tally ring keeps E4's ring discipline: zero its head at the top of the pass (beside M:530-537), let `ClearConvoy` add to it, and advance it beside M:540-544.
  - **After the `if (bLive)` reprice block, and OUTSIDE it,** run E5's per-pass steps, in this order:
    1. the record (drift and journal);
    2. the transitions (the issue; the withdrawal, if due);
    3. the admissions;
    4. the rate step (only if the market was issued at an earlier roll and no withdrawal ran this roll).

    All four are skipped while `!bCurrencyEnabled`.
  - **The skipped-sol walk** (REFRESH table row 2; F2). On the last pass, where `DueUpTo = SolNow - 1` may be above `ClosingSol`, the record adds `(SolNow - 1) - ClosingSol + 1` drifts and applies every entry with `ClearSol <= SolNow - 1`. For that pass, "the sol being closed" for transitions and admissions is `SolNow - 1` (UNCERTAIN, see M6). The rate still steps at most once per pass, so at most 8 times. CU7's 12-sol jump must total 12 drifts.
- **`ClearConvoy` (M:638-736):**
  - **A mark convoy (`bMarks`):**
    - Net > 0: `Allowed = max(0, MarkBalanceMmk - (MarkReservedMmk - ReservedMmk))`, pay `min(Net, Allowed)` through `MarkSpend`, and write off any rest with a Warning worded in marks.
    - Net < 0: `OwedMmk = -Net`.
    - Release `MarkReservedMmk`.
    - Add SellsValue and BuysValue (mmk) to the bloc tally's closing-sol entry. **Only `bMarks` orders enter the tally** (re-review 3b).
  - **A converted credit order:**
    - Net > 0: charge `ceil(Net × r / 1000)` with E4's `Allowed` logic on credits.
    - Net < 0: `OwedMcr = floor(-Net × r / 1000)`.
  - **Otherwise:** HEAD's code, unchanged.
  - `ValueMcr` stays `Buys + Sells` in the market unit (the director/orchestrator decision).
- **`GetOrderReservationMcr` / `PlanConvoy` (M:826-1021):**
  - PlanConvoy gains a clearing-sol input. Recommended name `EffSol` = max(floor(ExecuteAt/1200), `MarketCheckedSol`), to match E6's REFRESH ("PlanConvoy gains an EffSol parameter").
    - `BookConvoy` passes the order's value.
    - `CanPlaceOrder` / `CanPlaceStandingOrder` (deck, cheat) pass the sol an order sent now would execute in: floor((now + order lag) / 1200), at least `MarketCheckedSol`. This is UNCERTAIN; the deck's answer is colony-side, so a preview at the margin may differ.
  - **New refusal, after HEAD's political refusals and only for a priced plan:**
    - if `bWithdrawPending`: `The market is closed while the mark is withdrawn`.
  - **A mark plan:**
    - its reservation is E4's formula, read in mmk;
    - refuse if it exceeds `GetSpendableMmk()`: `The order may cost up to %s marks net; %s marks are spendable`.
  - **A converted credit plan:** `HiR(r) = r*(10000+CurrencyMaxStepBp)/10000 + 1`, `LoR(r) = max(1, r*(10000-CurrencyMaxStepBp)/10000 - 1)`, and `R_mcr = max(0, ceil(Σ buy kg × Hi(q) × HiR(r) / 1000) - floor(Σ sell kg × Lo(q) × LoR(r) / 1000))`, where r is the current `RateMcr`.
  - Carry `bMarks` in `FRHBookPlan`.
- **`BookConvoy` (M:1041-1102):** a mark plan does `MarkReservedMmk += R` and sets `Convoy.bMarks = true` and `Convoy.ReservedMmk = R` (it leaves `ReservedMcr` at 0). A credit plan is unchanged.
- **`StepTrade` (M:1124-1127), the vanished-row path:** also release `MarkReservedMmk` for an uncleared mark convoy. The spec does not mention this; it is needed for the invariant.
- **`StepConvoyReturn` (M:1227-1232):** if `OwedMmk > 0`, call `MarkPay(OwedMmk)` (no `MarketSalesMcr`). `OwedMcr` keeps HEAD's path.
- **`ReadMarketBlock` (M:1362-1729):**
  - Clear `bMarketBlockLost` at the top.
  - Set it in the `!bParsed` branch (M:1484, which covers `!bFound`), at `bSlotsBad` (M:1524) and at `!bRegionsOk` (M:1568).
  - **MOVE** the lifetime-sales clamp (M:1702-1708) and the reservation recompute (M:1709-1728) into `ReconcileReservations()`. Stash `ReadSales` and `ReadReserved`, and whether the block parsed, in transient members, so `ReconcileReservations` logs exactly E4's Warnings, with E4's texts, under E4's conditions:
    - nothing in the `!bParsed` path, as today;
    - the "disagrees" Warning compared against the new combined sum.
  - The order of `market.log` lines must not change.
- **`Debug_ZeroCredits` (M:58-71):** refuse while anything is reserved, in either purse. Zero `DeskCreditMcr` and `CurrencyRedeemedMcr` as well.
- **New functions:**
  - money: `MarkPay`, `MarkSpend` (a mirror of `CreditSpend`), `GetSpendableMmk`;
  - acts: `CanIssueCurrency`, `IssueCurrency`, `AppendJournal`;
  - the desk: `CanExchange(Dir, Amount, EffSol, OutWhy)`, `BookExchange(Cmd)` (with the same catch-up as M:1051-1054), `ClearDeskOrders`;
  - the per-pass steps: `StepPoliticalRecord`, `RunCurrencyTransitions`, `RunWithdrawal`, `AdmitBlocMembers`, `StepRate`;
  - the save: `WriteCurrencyBlock`, `ReadCurrencyBlock`, `GetCurrencyBlockBytes`, `ReconcileReservations`.
- **`CanExchange` order:**
  1. `The desk opens at the end of sol %d` (issued in the market and open for `EffSol`);
  2. `The mark is being withdrawn` (`!bCurrencyIssued`);
  3. `No bloc member can trade with you`. This needs at least one member in the list (admitted at an earlier roll, whatever its `MemberFromSol`: re-review 1) that is available, on an open route, not defected and not embargoed;
  4. `The desk takes at most %d %s a sol (%d booked)` (the booked total for this direction and `EffSol`, plus this order, must be at most 500);
  5. `Not enough %s spendable (%s)`.
- **Rate step (3.3), int64 throughout:**

  ```
  R0 = 1000
  Sold, Bought = sums of the bloc tally's last CurrencyWindowSols (5) completed entries (respect the fill)
  TradeBp   = DivTrunc((Sold-Bought)*10000, max(Sold+Bought, CurrencyMinVolumeMmk))
  IdentBp   = DivTrunc(IdentityViewMilli, 10)
  TensionBp = TensionViewMilli / 10
  AnchorBp  = DivTrunc((RateMcr-R0)*10000, R0)
  Pressure  = DivTrunc(TradeBp*40 + IdentBp*30 - TensionBp*30 - AnchorBp*50, 100)
  StepBp    = clamp(DivTrunc(Pressure*CurrencyGainPct(20), 100), -200, +200)
  Delta     = StepBp==0 ? 0 : sign(StepBp)*max(1, |RateMcr*StepBp|/10000)
  RateMcr   = clamp(RateMcr+Delta, R0*2500/10000, R0*40000/10000)   // [250, 4000]
  ```

  The weights are the config rows.

### 3.4 `Source/RedHopeSim/Public/RHSimWorldSubsystem.h` (H)
- **`FRHConvoy` (H:550-564):** add in-memory fields `bool bMarks = false; int64 ReservedMmk = 0; int64 OwedMmk = 0;`. They are NOT written by `WriteMarketPayload`; the `'RHE5'` frame persists them. A mark convoy keeps `ReservedMcr == OwedMcr == 0`, so E4's payload and repairs are untouched.
- **Public API (beside H:1561-1621):**
  - `bool CanIssueCurrency(bool bDefyIssue, FString& OutWhy) const`, `bool CanExchange(...) const`;
  - `bool IsCurrencyIssued() const`, `int64 GetRate() const`;
  - `int64 GetIdentityView() const`, `int64 GetTensionView() const`;
  - `void GetBlocMembers(TArray<TPair<FName,int32>>&) const`;
  - `int64 GetMarkBalance() const`, `GetMarkReservedMmk()`, `GetSpendableMmk()`;
  - `int32 GetCurrencyBlockBytes() const`.
  - Optional, for E8, which otherwise adds them later as header-only getters (E8 doc lines 413-419, 663, 683): `GetCurrencyIssueSol`, `GetCurrencyOpenSol`, `IsMarketInMarks`, the last `TradeBp` and step sign, the desk's booked totals per sol and the desk order list.
- **Harness hooks (runtime only):** `Debug_SetCurrencyEnabled(bool)`, `Debug_SyncPoliticalView()`, `Debug_SetRate(int64)`, `Debug_SetBlocTally(SoldMmkPerSol, BoughtMmkPerSol)`, `Debug_GrantMarks(int64)` (through `MarkPay`).
- **Private state (beside H:3837-3906):**
  - flags and sols: `bCurrencyIssued`, `CurrencyIssueSol = -1`, `CurrencyOpenSol = -1`, `bWithdrawPending`, `WithdrawClearSol = -1`, `RateMcr = 1000`;
  - the record: `IdentityViewMilli`, `TensionViewMilli`, `TArray<FRHPoliticalEntry> Journal`;
  - the bloc tally: `BlocSoldMmk[8]`, `BlocBoughtMmk[8]`, `BlocFill`, `BlocHead`;
  - the purses: `MarkBalanceMmk`, `MarkEarnedMmk`, `MarkSpentMmk`, `MarkReservedMmk`, `DeskCreditMcr`, `CurrencyRedeemedMcr`;
  - `TArray<FRHBlocMember> BlocMembers` (by name string) and `TArray<FRHDeskOrder> DeskOrders` (by Id);
  - transient: `bCurrencyEnabled = true`, `bMarketBlockLost`, the stashed `ReadSales` / `ReadReserved` / `bMarketParsed`;
  - the 16 config members (section 3.7).

### 3.5 `Source/RedHopeSim/Public/Data/RHRows.h` + D + data
- **`FRHRivalRow`:** add `UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") int32 BlocStance = -1;`. Put it after `StoreCap` (line 417).
- **`docs/data/RH_Rivals.csv`:** add a `BlocStance` column right after `StoreCap` (before `RelationStart`), unquoted: Zarya 20, Meridian 40. Placing it before the quoted Notes keeps MX0's naive comma split (MS:331-345) and CU0's CSV read sound. Append "Bloc stance placeholder, director review." to each Notes cell.
- **`docs/data/RH_Config.csv`:** 16 rows, each a placeholder for director review:

  | Row | Value | Clamp |
  |---|---|---|
  | `CurrencyIssueIdentity` | 20 | |
  | `CurrencyIssueTension` | 10 | |
  | `CurrencyRequisitionPenalty` | 0.10 | |
  | `CurrencyRateStartMcr` | 1000 | |
  | `CurrencyWindowSols` | 5 | 1..8 |
  | `CurrencyMinVolumeMmk` | 5000000 | >= 1 |
  | `CurrencyTradeWeight` | 40 | |
  | `CurrencyIdentityWeight` | 30 | |
  | `CurrencyTensionWeight` | 30 | |
  | `CurrencyAnchorWeight` | 50 | |
  | `CurrencyGainPct` | 20 | |
  | `CurrencyMaxStepBp` | 200 | 1..10000 |
  | `CurrencyFloorBp` | 2500 | < 10000 |
  | `CurrencyCeilBp` | 40000 | > 10000 |
  | `DeskSpreadBp` | 300 | |
  | `DeskCapPerSol` | 500 | |

  The C++ fallbacks equal the CSV, and each value is clamped on read as `InitMarket` does.
- **Sync `DT_Rivals` and `DT_Config`** with `scripts/unreal/rh_sync_datatables.py` (as E4 did). Commit the two `.uasset` files.
- **`RHCheats.cpp:969-1000` (`RH.InjectRivals`):** set `Z.BlocStance = 20` and `M.BlocStance = 40`.
- An injected row built without the column defaults to -1. Greed (C:1118) copies Zarya's 20 but has no economy, so it never joins.

### 3.6 Suites and battery
- **New file `Source/RedHopeSim/Private/RHSimCurrencySuite.cpp`:** `int32 RHRunCurrencySuite(UWorld*, URHSimClockSubsystem*, URHSimWorldSubsystem*)`.
  - Declare it beside C:33-34.
  - Dispatch it beside C:14828-14831: `if (FParse::Param(*Params, TEXT("currency"))) return RHRunCurrencySuite(...)`.
  - Copy MS's log capture, `EraOnce`, `Inject` and `RetireAll` helpers (MS:228, :267-282) and MX8's start-save pattern (MS:1663-1701).
- **`scripts/sim/rh_battery.sh`:** append `currency` after `market` in `SUITES` (line 39). Update the comment on line 2 and the echo on line 58: 58 suites, 59 runs.
- **Version bump in other suites** (mechanical; this is what E4 did for v55):
  - every `Ver == 55` becomes `Ver == 56`: 15 sites, in CavernSuite:738, C:10867/:12147/:12563/:14103/:15128, CreditSuite:609, DeepDistrictSuite:771, DepthSuite:786, EconomySuite:857, XS:530, MS:1316, TramSuite:683, TransitSuite:1596;
  - every `Contains(TEXT("/55"))` becomes `/56`: 14 sites;
  - every `(expect 55)` text becomes `(expect 56)`.

  Find them with `git grep -nE 'Ver == 55|"/55"|expect 55' -- Source/RedHopeSim/Private`. The "a v54 file is refused" fixtures (CreditSuite:654, EconomySuite:878, XS:597, MS:1374) still work: 54 is refused at v56. Leave them.
- **XS:529-531 (XE10). MUST FIX; the REFRESH missed it.** It asserts `MarketFrame + HeaderBytes + MarketPayload + FooterBytes == W.Num()` (the market frame ends the file). With `'RHE5'` appended this fails. Change that clause to equal the `'RHE5'` frame's header offset (`RHSaveFrame::Find(W, CurrencyTag, ...)`), and the comment on line 529. Keep the printed text: only its version moves.
- **MS:548-555 (MX1).** Bound `bFrameDiffers`'s compare to the market frame's own length (`OnAt` .. `OnAt + 12 + payload + 8`) instead of to the end of the file. Keep the printed text byte-identical.
- **Leave E4's MX `Inv()` lambda, the MX9 repair fixtures and the 47 `LoadColony v55:` Warning texts unchanged.**

### 3.7 Presentation (RedHope), minimal (E8 is the full panel)
- **K, the Dilemma card (K:2200-2232; DEFY button K:2227):** add a third button, `DEFY, AND ISSUE A MARTIAN CURRENCY`. Grey it with `CanIssueCurrency(true, Why)` and put the reason in its tooltip. Its handler, beside `HandleSolidarity` (K:2474-2477), sends `Solidarity` / `DefyIssue` through `HandleRivalVerb`.
- **K, the EARTH & NEIGHBORS panel (`BuildPoliticsPanel`, K:2192-2472):**
  - an **Issue a Martian currency** button (greyed by the rule);
  - two balances (Credits / Marks, with spendable and reserved in the tooltip);
  - the rate as `1 mark = %s cr` with the last step's sign, labelled "at the close of the last sol" (risk 1);
  - the desk: `To marks +100`, `To credits +100` and SEND, read by `CanExchange`, with the cap and the spread shown.
- **K, the rival card (`BuildMarketCard`, K:2524-):** add "Bloc member" or "Joins at identity %+d", and the currency the next order would settle in. List a member only once it is available (R7 O10).
- **`Source/RedHope/Public/RHOrderWords.h`:** add `VerbText` rows for `Currency` and `Exchange`.
- **`RHCheats.cpp`:**
  - `RH.Currency issue | status | desk <tomarks|tocredits> <amount>`;
  - `RH.Solidarity` accepts `defyissue` (its usage text is at RHCheats.cpp:914 and :919).
- **`docs/qa/2026-10-15/gate-d-wording.md`:** add a new block, "Rows added by E5", using the next free row prefix. X is the last one at HEAD, so use Y unless the visual pass took it. It lists every new refusal, log line, button and tooltip as PLACEHOLDER. The mark's name is a placeholder for the director. None is morale or evacuation wording.
- **`docs/design-decisions.md`:** add an E5 entry (what it settles, and every deviation).

## 4. Specific traps (read before coding)

1. **The record runs from sol 0 in every colony.** Its steps must sit outside `RollMarket`'s `bLive` gate and log nothing.
2. **The REFRESH's anchors moved.** Every W anchor in the REFRESH table from W:9502 on is +67 at HEAD, except the market read, which is +83 (14660 becomes 14743). H is +14 and K is +17. The full list is in the structured result's `stale_anchors`.
3. **The CU8 start save.** Set the clock to `16.0*SolLen - EraDt`, take one era step (the catch-up roll closes sols 0-15 here), then save `cu8_start`. Otherwise the first roll after load adds 16 drifts to the synced record (re-review observation 1: 68.6 recorded against 59.0 live). Sync at load in both twins.
4. **The per-convoy currency is not in E4's payload.** On load, `ReadCurrencyBlock` restores `bMarks`, `ReservedMmk` and `OwedMmk` onto the convoys E4's block loaded, by Id. A listed Id that E4 does not hold is dropped with a Warning.
5. **Section 7's layout predates R4.** The frame must also carry `CurrencyRedeemedMcr` (R4 creates it; section 7 never lists it).
6. **`bMarketBlockLost`.**
   - **Where it is set:** the REFRESH names only `!bFound` and the slot table. Set it in every branch where the market starts at its start values: `!bParsed` (lost or malformed), `bSlotsBad` and `!bRegionsOk`.
   - **What it does:** with it set, `ReadCurrencyBlock` starts unissued, seeds the record from the live values and treats every convoy as a credit convoy.
   - **The Warning (decision; see problem B2):** log one only if the `'RHE5'` frame held something to lose. That means issued, a market issue, a pending withdrawal, a member, a nonzero purse or total, a mark convoy or a desk order. A literal "always warn" adds unlisted Warning lines to `market.log` (MX9's lost, malformed and slot-table fixtures) and breaks the line-identical bar.
7. **`NextCommandId` is not saved** (W:14065 raises it only past queued orders). In a fresh process, a new desk order can then reuse a pending desk order's Id. `ReadCurrencyBlock` must raise `NextCommandId` past every loaded desk order's Id and every journal entry's `CommandId`. This saves no field and changes no line.
8. **The lifetime clamp's precedence (R4 says only "together").**
   - First clamp each of `DeskCreditMcr` and `CurrencyRedeemedMcr` to [0, `Awarded - SalesPaid`]. If their sum exceeds that, cut `CurrencyRedeemedMcr` first, then `DeskCreditMcr`, with a Warning each.
   - Then E4's clamp: `MarketSalesMcr` in [0, max(0, `Awarded - SalesPaid - Desk - Redeemed`)], with E4's exact text.
   - With E5's totals at 0 this is E4's behaviour byte for byte.
9. **`ReconcileReservations` also reconciles the flags (R7 O8, re-review 5).**
   - If `bCurrencyIssued` is true, there is no market issue and no pending Issue entry: clear the flag, with a Warning.
   - If the market is issued, the flag is false and no withdrawal is pending: set it.
   - If `bWithdrawPending` is set with neither a market issue nor a pending Issue entry: clear it.
   - All of this is silent when the state is consistent.
10. **A barter convoy during a pending withdrawal.** Refuse only priced plans and desk orders (UNCERTAIN; see M9). The withdrawal trigger counts only uncleared **priced** convoys and pending desk orders, because a barter convoy never clears (re-review 5).
11. **Desk payout formula order.** Compute each payout as ONE floor of the exact rational: ToMarks `floor(A_mcr * 1000 * (10000-S) / (r * 10000))`; ToCredits `floor(A_mmk * r * (10000-S) / (1000 * 10000))`. Write the same expression in CU6's reference. The spec's left-to-right form could floor twice; that is UNCERTAIN. Both round against the colony, and a single floor is exact.
12. **Integer ceil for positive charges:** `(a + b - 1) / b`. All magnitudes fit in int64 (R7 O13: at most about 8.6e17).

## 5. Save: v56, frame `'RHE5'`

Write it with `RHSaveFrame::Write(Ar, CurrencyTag, ...)` straight after `'RHE4'` (W:13933); it is the last frame in the file. Read it with `RHSaveFrame::Read(Ar, Bytes, CurrencyTag, Payload, Why)` after `ReadMarketBlock` (W:14743). It never refuses: it repairs with Warnings, and no Error is ever logged. Payload, in order:

1. `uint8 bCurrencyIssued`, `int32 CurrencyIssueSol` (-1 = none), `int32 CurrencyOpenSol` (-1), `uint8 bWithdrawPending`, `int32 WithdrawClearSol`, `int64 RateMcr`.
2. `int64 IdentityViewMilli`, `int64 TensionViewMilli`, `int32 JournalCount`, then each entry in `(ExecuteAt, CommandId, sub)` order: `int32 ClearSol`, `double ExecuteAt`, `int32 CommandId`, `int32 Sub`, `uint8 Kind`, `uint8 bHasIdentity`, `int64 IdentityMilli`, `int64 TensionDeltaMilli`.
3. The bloc tally: 8 × `int64` sold, 8 × `int64` bought, `int32 Fill`, `int32 Head`.
4. `int64 MarkBalanceMmk`, `MarkEarnedMmk`, `MarkSpentMmk`, `MarkReservedMmk`, `DeskCreditMcr`, `CurrencyRedeemedMcr`.
5. `int32 MemberCount`, then per member by name string: `FString Name`, `int32 MemberFromSol`.
6. `int32 MarkOrderCount`, then per mark convoy by convoy Id: `int32 Id`, `int64 ReservedMmk`, `int64 OwedMmk`.
7. `int32 DeskCount`, then per desk order by Id: `int32 Id` (its `CommandId`), `uint8 Dir`, `int64 Amount`, `int32 ClearSol`, `uint8 bCleared`. A desk order is removed when it clears, so `bCleared` is always 0. Keep the field, as section 7 lists it.

The frame is sized by history. Tests measure it with `GetCurrencyBlockBytes()`, never as a constant. In the battery's non-currency suites the journal is empty at every save that prints a size, so the frame has one fixed size there (the delta check in section 7).

**Load repairs** (Warnings only), all of them CU10's:
- a rate outside [250, 4000] is clamped;
- a view outside its range is clamped;
- an unsorted or repeated journal resets to empty and re-seeds the record from the live values, with the flag reconciled (O8);
- an unsorted or repeated member list resets;
- a mark order naming a convoy Id E4 did not load is dropped;
- a negative purse is set to 0, then `MarkSpent = MarkEarned - MarkBalance`;
- the reservations are set to their recomputed sums (in `ReconcileReservations`).

A lost or bad `'RHE5'` frame starts unissued, with the record seeded from the live values and a Warning.

**Checkpoints:** v56 refuses the Midgame and Late checkpoints. Re-cut them with `bash scripts/unreal/rh_checkpoints.sh all --force` and load both.

## 6. The `-currency` suite (fails loudly; no path logs an Error on purpose)

Reuse E4's `Debug_RollMarket`, `Debug_SetMarketPrice`, `Debug_SetRivalStore`, `Debug_InjectRival`, `Debug_GrantCredits`, `Debug_AddInfluence`, `Debug_SetRequisitionInputs` (H:1558), `Debug_ShiftIdentity` (H:1773), `Debug_AddTension` (H:1774), `Debug_SetEmbargo` (H:1618) and the clock's `Debug_SetSimSeconds`. Every one exists at HEAD.

| Check | What it pins (expected values where the spec gives them) |
|---|---|
| CU0 | Data: the 16 config rows read their CSV values from the live DT, and each clamp holds. `BlocStance` is Zarya 20 and Meridian 40; the CSV equals the live DT; an injected row defaults to -1. Greed (Zarya's copy) has stance 20 and never joins. |
| CU1 | The inert twins, `Debug_SetCurrencyEnabled` on and off: 10 sols in each band, including a Defy, a Comply and a Pacify through the uplink. The digest is bit-identical: stocks, both purses, relations, Influence, HumanNature, tension, identity, the multiplier, Hope, the quota phase and the projected ending. The saves are byte-identical up to `'RHE5'`. The "on" record moved and logged nothing. |
| CU1b | For identity in {-100, -20, 0, +20, +100} × tension in {0, 48.3, 60, 100}, the unissued `GetRequisitionMultiplier()` equals HEAD's expression computed in the suite in the same operation order, bit for bit. (Mutation 11.) |
| CU2 | One rate step by hand: fixtures with known tallies, views and rates, each result equal to a hand-written integer. Include a step that rounds to 0 (giving the 1 mcr minimum), both clamps (250 and 4000), `DivTrunc`'s negative cases and an anchor beyond ±10000. |
| CU2b | 10,000 steps from a seeded LCG. Each step moves at most 2% or 1 mcr, each rate stays in [250, 4000], and each equals an independent integer reference written in the suite. (Mutation 10.) |
| CU3 | **Issuing.** Every refusal of the four checks with its text. Text 4 needs an injected rival with stance <= the current identity, or check 3 answers first (problem M4). A `DefyIssue` with a check failing leaves the Defy undone. Then a `DefyIssue` twin against a plain-Defy twin: the Defy's HEAD effects, tension +10, and the multiplier lower by exactly 0.13 (0.10 + 0.3×10/100) against a reference computed in `GetRequisitionMultiplier`'s operation order; the log line; two journal entries. At the roll: issue sol s, open sol s+2, rate 1000, and no step until the roll closing s+1. Then a stand-alone `Issue` at identity +20. (Mutation 5.) |
| CU4 | **Joining.** Two injected rivals created in the reverse of their name order: they join in name order, with the same log lines and saved bytes as the forward order. A stance of -1 never joins. A stance-40 rival joins at the roll after a Defy brings the record to +40, with `MemberFromSol` = that sol + 2. An embargoed member is refused at booking with HEAD's text (PlanConvoy, M:902-906). (Mutation 6.) |
| CU5 | **Settlement.** A mark order: its reservation in marks, the clearing's `MarkSpend`, `OwedMmk` paid at the return, and the bloc tally. A converted credit order: its reservation with the rate margin, and the converted charge when the price and the rate each move a full step between booking and clearing (`Debug_SetMarketPrice`, `Debug_SetRate`). Every rounding goes against the colony, to the milli-unit. Both purse invariants hold after every step. (Mutations 8 and 9.) |
| CU6 | **The desk.** Both directions at a fixed rate: the payouts, the spread, the reservation and its release; the cap and its refusal; the refusal while withdrawing. A pure desk order leaves the next rate step unchanged (mutation 7). A one-sol round trip returns <= 0.960 of its stake. |
| CU7 | **The record.** 30 sols with no act: `TensionViewMilli` = start + 600 per sol, and at each roll \|live - view/1000\| <= 0.61. An act due 10 s after a boundary changes the record at the roll closing its own sol. A Comply from tension 5 clamps the record at 0. A 12-sol jump adds 12 drifts. With no `SliceActive` row there is no drift. After `Debug_ShiftIdentity(+10)`, `Debug_SyncPoliticalView()` and a Defy, the identity record equals the live value, +30. (Mutation 13.) |
| CU8 | **Era parity.** The paired run below. (Mutations 1, 2, 3, 4, 5 and 14.) |
| CU9 | **Withdrawal (CU9 names its band).** After a mark order, a Comply: the penalty ends at execution, and a mark order to a discovered member whose route is still open is refused with `The market is closed while the mark is withdrawn`. The withdrawal waits for every priced order to clear, then redeems with one floor over balance + on-road owed at the rate in force with no spread, into `CurrencyRedeemedMcr`. The on-road convoy then pays nothing more at its return. Every slot is re-expressed and clamped; the bloc dissolves; the mark balance is 0 and `MarkReservedMmk` is 0. The desk refuses. A later `Issue` is allowed, and its purse starts at 0. Also: the same-roll issue and withdrawal uses r = 1000, and the reopen edge (an order 10 s after the withdrawal roll's boundary). (Mutation 12.) |
| CU10 | **Save.** An issued colony with two members, a mark convoy out and a desk order pending: save, load, save is byte-identical, the lists read back ascending, and `'RHE5'` is the last frame with payload == `GetCurrencyBlockBytes()`. Every repair of section 5 on a doctored, resealed file (`RHSaveFrame::Reseal`), with the colony loaded. A v55 file is refused (`/56`). A lost `'RHE4'` starts the currency unissued, with its Warning. A lost `'RHE5'` starts unissued. The flag reconciliation of O8. A fresh-load `NextCommandId` sits past a pending desk Id. |
| CU11 | Ruling 11 / Q4a: the rate forced to 250 and to 4000 and a large mark balance, over 10 sols. Hope, population, growth, Influence and every stock are bit-identical to the twin at parity. |
| CU12 | Politics untouched: twins that issue and do not issue, from one save. Identity, HumanNature, relations and Influence differ only by the Defy half of `DefyIssue`; the projected ending is the same; tension and the multiplier differ by exactly the issue's +10 and -0.10. The window ends 25 sols after the issue (before tension 75, which falls at 27.8). |

### CU8, the paired run (R1 with re-review conditions 1, 2, 3 and 7 applied)
- **Rivals:**
  - Zarya, as shipped (stance 20, relation 40);
  - injected `Kestrel`: MX8's economy (MS:1674: 30 km, a 1-sol round trip), `BlocStance` 40, `RelationStart` 10, `SliceActive` true.
- **Start:** build the fixture colony with no convoy out. Set the clock to sol 16.0 − `EraDt` and take one era step (the catch-up roll). Save `cu8_start`. Both twins load it.
- **At load, in both twins:** `Debug_SetRequisitionInputs(0, 59)`, `Debug_SyncPoliticalView()`, `Debug_GrantCredits(20,000 cr)`, `Debug_AddInfluence(40)`.
- **Era twin:** first advance 30 s, so each boundary step ends at the boundary + 30 s.
- **The schedule** (same sim times in both twins):

| Sol | Act | Expectation |
|---|---|---|
| 19.5 | `Solidarity DefyIssue` | The demand has been pending since about 17.67. `ClearSol` 19: open 21; Zarya is a member from 21. |
| 20.0 + 10 s | `Exchange ToMarks` (any amount) | **Refused in both twins** (the desk is not open for sol 20). Under mutation 5 it is accepted in the agent twin only. This replaces R1's sol-20 MarketOrder (re-review 1). |
| 22.5 | `Exchange ToMarks 500` | (500, not 300: re-review 2.) |
| 23.5 | `MarketOrder` to Kestrel | Converted credits. Home about 24.5. |
| 25.5 | `MarketOrder` to Zarya | Marks. Home about 29.5. |
| 26.0 + 10 s | `Solidarity Defy` | The boundary act (the second demand has been pending since about 21.0). `ClearSol` 26. In the era twin it runs before the roll closing 25, but it is keyed 26 in both. |
| 27.0 + 10 s | `MarketOrder` to Kestrel | `ClearSol` 27: **credits in both twins.** Kestrel joins at the roll closing 26 and is a member from 28. Exempt from the home-sol clause (booked on the era boundary step). |
| 28.5 | `Exchange ToCredits 200` | |
| 29.5 | `MarketOrder` to Kestrel | `ClearSol` 29: **marks in both twins.** Zarya's convoy comes home at this instant (re-review 2). Make this order net-sell, so its mark reservation is 0, or size it to at most half the marks spendable excluding Zarya's owed marks. (UNCERTAIN; default: net-sell.) |
| 30.0 | `Debug_AddTension(25)` in both twins | About 78.4 live: an embargo on Zarya. The third demand is pending from 30.0. The record does not see the write. |
| 30.5 | `Pacify` Zarya | Influence >= 20. The record takes -20 at the roll closing 30. The pending demand is not cleared. |
| 31.0 + 10 s | `MarketOrder` to Zarya | Marks. This replaces R1's 31.5 (re-review 7). Home about 34.975 in the era twin and 35.008 in the agent twin. |
| 34.5 | `Solidarity Comply` | Withdrawal pending. It runs at the roll closing 34, where the 31 convoy is home in the era twin and still on the road in the agent twin. **The redeemed credits must be identical.** |

- **Assert:**
  - no DustStorm at any threshold from 16 to 36;
  - no E2 award, E3 launch or E3 sale lands in sols 16-36 (re-review 3a);
  - E4 R7's rules plus the third rule:
    1. every convoy is home at least a sol before the next order to the same rival (boundary-step bookings exempt);
    2. every reservation is at most half the spendable credits or marks;
    3. a mark reservation is sized against the marks held, excluding any convoy due home within a sol.
- **The bar:** at every threshold 16-36, int64 equality of the rate, both views, the member list, every price, store and fill, every charge in either currency, every desk payout and the bloc tally. The home sols and the end balances in both currencies are equal. The redeemed credits are equal.

### Mutations (CU13): each built, run and restored by sha256, and each must fail the named check

| # | Mutation | Fails |
|---|---|---|
| 1 | The rate reads the live `IdentityAxis` | CU8: the era twin's roll closing 25 sees the boundary Defy's +20 |
| 2 | The rate reads the live `EarthTension` | CU8: the same roll sees its -12 |
| 3 | The journal keyed by `MarketCheckedSol` at the act's execution (R2) | CU8's boundary fixture |
| 4 | `MemberFromSol` = the closing sol + 1 | CU8: the `ClearSol` 27 Kestrel order books in marks in the agent twin |
| 5 | `CurrencyOpenSol` = the issue sol + 1 | CU3's open sol, and CU8's 20.0 + 10 s Exchange |
| 6 | Joins walked in FName order | CU4 |
| 7 | Desk flows fed into the trade term | CU6 |
| 8 | The credit reservation without its rate margin | CU5 |
| 9 | The converted credit charge rounded down instead of up (R7 O5) | CU5's milli-credit check |
| 10 | A `double` in the rate update | CU2b |
| 11 | The requisition penalty applied unissued | CU1b |
| 12 | A Comply that does not withdraw | CU9 |
| 13 | The identity view built from nominal deltas while a harness write intervenes | CU7's sync case |
| 14 | On-road `OwedMmk` left out of the redemption total (re-review 7) | CU8's withdrawal |

**`-scale`** (outside the battery; run it as E4 did): 1,000 `RollMarket` calls with 8 injected members and the currency issued, at N = 5,000. The budget is E4's measured figure + 0.5 ms. The REFRESH says 1.246 ms (E4 at 0.746); E4's commit message says 0.715 ms. Re-measure E4's line at the build's HEAD before the change and use that + 0.5. E4's existing `<= 2 ms` check (C:17119) must still pass. Every digest is identical.

## 7. Gate run and expected battery movement

Commands:
- Build with `Build.sh red_hopeEditor Mac Development ... -waitmutex`; `-Wshadow` is an error.
- `bash scripts/sim/rh_battery.sh 10`. It must end with 0 suites not green and 59 runs. The logs must be newer than the dylibs.
- `python3 scripts/sim/diff_battery.py Saved/baselines/<newest battery_post_*> Saved/RHBattery`.
- **The pins (`baseline.log`):** `power: gen 50 W load 20 W` and `deposit Regolith_A: 40645 kg` stay unchanged.

**Lines that move (everything else stays identical):**
1. **Save-version lines.** About 31 lines carry the version as `v55`, `version 55`, `/55` or `expect 55` and become 56. Per log in `battery_post_tram`:
   - one line each: dayshift, farice, framing, rooms, shafts;
   - two lines each: caverns, credit, deep, deepdistricts, depth, economy, export, luxury, market, traffic, trams, transit, tunnels.

   Recount at build HEAD with `grep -E "v55\b|version 55|/55\b|expect 55" | grep -v "LoadColony v55"`. The 47 `LoadColony v55:` Warnings in `market.log` are E4's fixed text and must NOT move.
2. **Unmasked whole-file size lines.** Each moves by the same delta, which is the `'RHE5'` frame (12 + payload + 8 bytes) of a default colony with an empty journal:
   - CV8 `(12059 -> 12139 b...` (CavernSuite:739; also a version line);
   - DP11 `(12087 -> 12119 bytes)` (DepthSuite:786; also a version line);
   - EC1 ×2 `both saves are 13100 bytes` (EconomySuite:493);
   - DC9 `the 13408-byte file` (deepcommute.log).

   If any of these moves by a different delta, the frame is not constant-size in that suite: investigate.
3. **Masked lines.** The raw bytes of MX1 ×2 (`the saves (11761 bytes)` / `(11740 bytes)`), MX9's round trip `(12173 bytes)`, CR9, EC6 and XE10's round trips change too. `diff_battery.py` masks `(\d+ bytes` to `(N bytes`, so they show no diff. MX1's market-frame offsets (8717 and 8696) do not move.
4. **`currency.log`** is new.
5. **Timing digits** may differ (tolerated).

**`-solidarity`, `-ending`, `-earth`, `-preemptive`, `-covert` and `-espionage` must be line-identical.** Diff them by name. They journal silently and never issue.

**Playtest:** `RH_PLAYTEST_DISTRICTS=1 bash scripts/sim/rh_playtest.sh 110 e5_full guide full`, then `cmp` against `Saved/baselines/ind_full_playtest.txt` (296,489 bytes). It is **expected byte-identical**:
- the bot never sends `Solidarity`, `Pacify`, `Currency` or `Exchange` (P has 0 matches for any of them, and P:382 is its only uplink path), so the journal stays empty and the mark is never issued;
- the record logs nothing;
- the `.txt` holds only `[Playtest]` lines;
- E4's added frame kept the same playtest byte-identical.

**Click test (UI changed):** `RH_CLICKTEST_RES=1440x860 bash scripts/unreal/rh_clicktest.sh` must pass 3 times. The spec adds no click steps. Optional: one step that sees the Dilemma card's third button (greyed, with a reason) and one that sees the desk's SEND. The step count moves only if these are added.

**Captures:** the Dilemma card with three buttons; EARTH & NEIGHBORS with both balances, the rate and the desk; a rival card that says "Bloc member". Look at them.

**Finish:** re-cut the checkpoints (section 5). Write the `design-decisions.md` entry and the Gate-D rows. Snapshot the battery afterwards as `Saved/baselines/battery_post_e5` (the orchestrator).

## 8. Deviations the builder must NOT take

- Do not read the live `IdentityAxis` or `EarthTension` anywhere in the rate, the admissions or a settlement. Do not read relations, availability, embargoes or defection in the join rule.
- Do not add a field to E4's `'RHE4'` payload (`WriteMarketPayload`). Do not change E4's `LoadColony v55:` texts or the order of its Warnings.
- Do not put E5's record, admissions or rate step inside the `bLive` gate, or inside the `Passed` loop without the skipped-sol handling.
- Do not feed desk flows into the bloc tally. Do not count credit orders in the tally (only `bMarks` orders).
- Do not convert the goodwill floor. `ValueMcr` is compared in the market unit for every order.
- Do not let a Comply leave the mark issued (Q-B a). Do not add a spread or a cap to the withdrawal's redemption: the loophole is accepted.
- Do not step the rate at the withdrawal roll, and do not admit members at or after it in that roll.
- Do not floor the redemption per convoy. Use one floor over the exact total.
- Do not apply the requisition penalty unissued, or restructure `GetRequisitionMultiplier`'s unissued path.
- Do not write E5's block anywhere but as the last frame. Do not measure it as a constant.
- Do not add Earth trade to the trade term (risk 7). Do not let any E5 path read or write Hope, population, growth or Influence.
- Do not modify `RHPlaytest.cpp`.
