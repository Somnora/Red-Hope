# E6 BUILD BRIEF: the levers (trade rules, pacts, tariffs, covert ops that move prices)

Preflight by a read-only planner, 2026-10-26. Code read at **HEAD `d842dd0`** (save v55) with `git show d842dd0:` only; nothing in the working tree was read (the visual pass is still in it).

- **Spec:** `docs/qa/2026-10-24/e6-levers-design.md` (1,366 lines). The authority inside it runs: DIRECTOR ANSWER (2026-10-24) > REFRESH AT HEAD 31ffa6f (2026-10-25) > REVISION (R1-R10) > the re-review's required change and observations (the re-review says "applied by the builder") > PROFESSOR REVIEW > the design (sections 0-10).
- **Rules:** `docs/qa/2026-10-25/orchestrator-handoff.md` and `docs/qa/2026-10-22/orchestrator-handoff.md`.
- **Gate:** E6, save **v57**, frame tag **`'RHL6'`**. It is built after E5 (v56). Every line number below is at `d842dd0`. E5 and the visual-pass commit will move W, M, H and K, so **re-anchor each one by the function name given beside it** at your own HEAD.

Short names:
- **W** = `Source/RedHopeSim/Private/RHSimWorldSubsystem.cpp`; **H** = `Source/RedHopeSim/Public/RHSimWorldSubsystem.h`;
- **M** = `Source/RedHopeSim/Private/RHSimMarket.cpp`; **C** = `Source/RedHopeSim/Private/RHSimCommandlet.cpp`;
- **K** = `Source/RedHope/Private/RHCommandDeckPanels.cpp`; **F** = `Source/RedHopeSim/Public/RHSaveFrame.h` (and `Private/RHSaveFrame.cpp`);
- **MS** = `Source/RedHopeSim/Private/RHSimMarketSuite.cpp`.

---

## 0. Preconditions (check them before writing code)

1. **The visual pass is committed and pushed, and E5 (v56) is committed** on top of it. If E5 has not landed, stop: E6 is designed on E5 and is not buildable without it (see section 2).
2. **The battery baseline is the one E5's gate left** (expected `Saved/baselines/battery_post_e5/`, 59 runs). **UNCERTAIN:** the exact name is whatever E5's orchestrator snapshot used.
3. **The playtest baseline is `Saved/baselines/ind_full_playtest.txt`** (296,489 bytes), unless E5's gate re-cut it. E5 is designed inert, so it should still be this file.
4. **E5's measured `-scale` figure** for 1,000 `RollMarket` calls is known. E6's budget is that figure + 0.2 ms. E4's figure was 0.746 ms, and E5's budget was 1.246 ms.
5. `pgrep -fl "UnrealEditor|rh_battery|Build.sh" | grep -v Services` is empty. `df -h / /Volumes/Unreal` shows at least 5 GB on / and 2 GB on the Unreal drive.

---

## 1. The authoritative decisions, in one list

**Director (2026-10-24):**
- **D1. Q-A (a).** Earth cannot embargo a pact partner. `StepPreemptive`'s target filter skips a rival with a **live** pact. Earth then embargoes the next eligible neighbour, or nobody. **R8 (the Q-A (b) lift) is NOT built.**
- **D2. Q-B (a).** A sabotage stops the target's production for the six whole sols after the act. `SabotageCutPct` = 100.
- **D3. Q-C (a).** Only a betrayed partner charges a tariff: 25% (2,500 bp) on every resource, both ways, for 30 sols. Betrayal means the colony breaks the pact, or the partner catches a covert op or a sabotage against it. Nothing else makes a `Tariff`, except the harness `Debug_AddTariff`.

**The rule list (sections 1.1-1.4, with R3):**
- **D4.** `TArray<FRHTradeRule> TradeRules`, ascending by Id. `NextRuleId` is saved and starts at 1. Ids are never reused and are assigned only at act executions and live events, never at a retirement. A rule is never updated in place to extend it: a repeated sabotage **appends** a new `Disrupted` rule.
- **D5.** The kinds are `Closed`=1, `Disrupted`=2, `Severed`=3, `Embargo`=4, `Pact`=5, `Tariff`=6. The values 1-4 are the refusal precedence.
- **D6.** The origins are `Comply`, `Sabotage`, `Defection`, `EarthPressure`, `Signed`, `Betrayal`, `Harness`. Number them 1-7 in this order (the spec gives no numbers).
- **D7. The fields:** `Id`, `Kind`, `Origin`, `Party` (FName), `ResSlot` (−1 = every resource), `Dir` (1 buy, 2 sell, 3 both), `MarkupBp`, `FromSol`, `UntilSol`, `LiveSols` (double), `bLive`.
- **D8. The window encoding (R3).**
  - No window is `(-1, -1)`. A closed window is `0 ≤ From < Until`. An open-ended window is `From ≥ 0, Until = -1`.
  - The legal shapes per kind: `Closed`, `Severed` and `Embargo` have none. `Disrupted` has a closed window, or none (harness, levers off, or rebuilt from the mirror). `Pact` is open-ended while live and closed once ended. `Tariff` is closed.
- **D9. Exclusive kinds.**
  - At most one `Closed`, one `Severed` and one `Embargo` per rival, and never an `Embargo` beside a `Severed`.
  - At most one live `Pact` per rival.
  - `Disrupted` and `Tariff` rules may repeat.
- **D10. Retirement** runs at the very end of `RollMarket`, after everything else, including E5's post-loop steps.
  - A rule with no window retires when its live part is over: a `Disrupted` rule once its timer is out. An `Embargo` is removed directly by Pacify, or hardened into `Severed`.
  - `Closed` and `Severed` never retire.
  - An open-ended window never retires while it is live.
  - A closed window retires when its live part is over and `UntilSol ≤ MarketCheckedSol` (the open sol after the roll).
  - **Re-review O2:** an ended pact with an empty window retires at the next roll.
- **D11. `EvaluateTradeRules(Rival, EffSol, Lines) const`.** It walks every rule in Id order and returns three things:
  - **the block:** among the live blocking rules naming the rival, the lowest `Kind`, then the lowest Id. The text is HEAD's for that kind;
  - **the per-line markup:** the sum of `MarkupBp` over the `Tariff` rules that name the rival, whose `ResSlot` is the line's slot or −1, whose `Dir` covers the line, and whose window covers `EffSol`. The sum is clamped to `[0, TariffMaxBp]`;
  - **whether a `Pact` window covers `EffSol`.**

  The market never reads `LiveSols`, `bLive` or an Id.
- **D12. `EffSol = max(ClearSol, MarketCheckedSol)`** at booking. It is stored per uncleared priced convoy, in E6's payload and never in `'RHE4'`. The clearing reads markups at the stored `EffSol`, never at the closing sol (R5).
- **D13. Who calls the walk** (REFRESH):
  - `PlanConvoy` replaces its four political checks (M:887-905) with the walk's block. This one change covers the `Convoy` and `MarketOrder` verbs, `CanPlaceOrder` and `CanPlaceStandingOrder`.
  - `CanSignPact`, check 4.
  - E5's `CanExchange` check 3 reads only `Closed`, `Severed` and `Embargo` from the list. **Sabotage does NOT refuse the desk** (R7: the 1.3 harmonisation is withdrawn).
  - The walk is **never** called by `Covert`, `Sabotage`, `Launder`, `Pacify`, or Earth's manifest, launches and sales.
- **D14. The migration is HEAD's arithmetic, statement for statement** (1.4). The same `DtSols`, in the same function, at the same point, tested `<= 0.0`. The precedence keeps `-solidarity`'s "route closed" line (the embargo is made first, the Comply second).

**Pacts (2.1):**
- **D15. A new uplink verb, `Treaty`.** `Target` is the rival and `Text` is `Sign` or `Break`.
- **D16. `CanSignPact(Rival, OutWhy) const`**, in this order, with these texts:
  1. `No trade contact '%s'`;
  2. `No treaties: disabled` (levers off);
  3. `You already have a pact with %s`;
  4. the walk's live-block text;
  5. `%s does not trust you enough for a pact yet (relation %.0f, needs %.0f)` (50);
  6. `Not enough Influence for a pact (%.0f needed, %.0f on hand)` (15);
  7. `%s will not sign while it still charges you for the last broken pact (until sol %d)`.
- **D17. Signing.** All of these, at execution:
  - `Influence -= 15`; `EarthTension = clamp(+5)`; `HumanNatureAxis = clamp(+3)`;
  - append a `Pact` rule (Origin `Signed`, live, window `[c+2, -1)`);
  - append **one E5 journal entry, tension +5,000**;
  - one alert and the log line `=== PACT SIGNED: %s (influence %.0f, tension %.0f, terms from sol %d) ===`.
- **D18. What a pact gives.** For a trade whose `EffSol` its window covers, the cargo cap is `PactCargoKg` (2,000) each way instead of `ConvoyCargoKg` (1,000), in PlanConvoy's cap check (M:971-974). A barter rival gets no bigger cap.
- **D19. Break.** Refused without a live pact: `You have no pact with %s`. At execution:
  - `bLive = false`, and `UntilSol = max(c+2, FromSol)` (re-review O2, the inverted end);
  - relation −25; HumanNature −10; `EarthTension −= 5`, journaled −5,000;
  - the betrayal tariff: every resource, both ways, 2,500 bp, window `[c+2, c+32)`, Origin `Betrayal`;
  - the log line `=== PACT BROKEN: %s (relation %.0f, humanNature %.0f); they charge %d%% on every trade from sol %d to sol %d ===`.
- **D20. Ended by others.**
  - **A Comply** that closes the partner's route ends the pact: `UntilSol = max(c+2, FromSol)`, no tariff, `=== PACT ENDED: %s (the route is closed) ===`. A pact with a discovered dormant row (Meridian) survives, because a Comply closes only `SliceActive` rows (`ForEachRival`).
  - **A caught covert op or sabotage against a partner** ends the pact from the partner's side, with the betrayal tariff and no extra relation or HumanNature change: `=== PACT BROKEN by %s: they caught you ===`. A clean op leaves the pact standing.

**Tariffs (3.2):**
- **D21. Integer arithmetic,** with every rounding against the colony:

  ```
  value = kg*p
  buy cost      = value + DivCeil(value*m, 10000)
  sell received = value - DivCeil(value*m, 10000)
  tariff        = DivCeil(value*m, 10000)
  Net = Σ buy costs − Σ sell receipts
  ```

  - With `m = 0` everything is E4's, to the milli-unit.
  - **The reservation:** `HiM(q,m) = Hi(q) + DivCeil(Hi(q)*m, 10000)`, `LoM(q,m) = max(0, Lo(q) - DivCeil(Lo(q)*m, 10000))`, then E5's `HiR`/`LoR` for credit orders after the issue.
  - **Desk exchanges carry no tariff.**
  - **`ValueMcr`** (the goodwill floor's input, M:702), E4's tally and E5's bloc tally stay at the clearing price **before** the markup.
- **D22. `TariffPaidMcr` and `TariffPaidMmk`** are lifetime totals, for the panel only.
  - `TariffPaidMcr`: orders cleared before the market counts in marks.
  - `TariffPaidMmk`: orders cleared **while** the market counts in marks, in the market's unit (re-review O3).
- **D23. `MarkupBp` is never negative.** The colony collects no tariff, and no rule ever pays the colony.

**Covert and sabotage (4.1-4.4):**
- **D24. Sabotage.** The new `Disrupted` rule gets the market window `[c+1, c+1+D)`, with `D = SabotageMarketSols` (6, clamped 1..30, and LV0 pins it to `round(SabotageDurationSols)`).
  - At a roll closing sol s inside any of a rival's windows, that rival's effective production is `ProduceG*(100-SabotageCutPct)/100` for every slot. This applies both in E4's step 2 (the store) and in step 3's Prod term (`ProdSumG`). See trap T2.
  - Consumption goes on.
  - The live refusal (6 sols on `LiveSols`) is HEAD's.
  - The cut lands whether or not the act was caught.
- **D25. A clean `Covert`** against a rival with an economy appends a pending effect `{ClearSol, CommandId, Sub (slot order), Theft, rival, slot, grams}` for each looted resource. A caught op records nothing. A barter rival records nothing.
- **D26. `Launder`** against a rival with an economy appends an `Offering` effect for each slot it spent.
- **D27. Pending effects are applied in `RollMarket`'s per-sol pass.** The order is: after `ClearConvoys` and after E5's desk clearing; after the ring advance; **before** the `if (bLive)` production block and **outside** it (they apply even with the market off).
  - Every effect with `ClearSol ≤ DueUpTo` is applied, in `(ClearSol, CommandId, Sub)` order.
  - A theft does `StoreG -= min(grams, StoreG)`. An offering does `StoreG += grams`.
  - Neither enters any tally.
- **D28. The covert roll is untouched.** E6 adds no `RollCovertDetected` call, no change to `CovertAttempts`, and no new seed or P input.

**The switch (5.1):**
- **D29. `bLeversEnabled`** is runtime only: default true, never saved, set by `Debug_SetLeversEnabled`. When it is false:
  - `Treaty` is refused;
  - no theft or offering is recorded;
  - a `Disrupted` rule gets no window;
  - every markup reads 0.

  The migration itself is not switchable.

**The harness (REFRESH F1).** E4's five politics hooks (H:1617-1621) are rebuilt on the list with Origin `Harness`:
- **D30.** `Debug_CloseRoute` appends a `Closed` rule (if none). `Debug_SetEmbargo(R, Grace)` adds or updates the one `Embargo` (`LiveSols` = Grace). `Debug_SetDefected` adds a `Severed` rule. `Debug_SetSabotage(R, Sols)` appends a `Disrupted` rule with **no window**. `Debug_ClearPolitics` removes every rule of the four blocking kinds.
  - Then MX11 does not move (MS:1607-1661).

**Save (7, R3, R5, R6, re-review O1/O3, REFRESH):**
- **D31. v57, strict.** The v19, v21 and v22 slots stay as **mirrors**, written from the rules:
  - v19 `Closed`: the names of the `Closed` rules, sorted with `LexicalLess`;
  - v21 `SabotageSols`: a `TMap<FName,double>` of each rival's largest live `LiveSols`, in the order of each rival's first live `Disrupted` rule by Id;
  - v22 `EmbargoGrace`: each `Embargo` rule's `LiveSols`, in Id order;
  - v22 `Defected`: the `Severed` names, sorted with `LexicalLess`.

  At load, mirror timers are **clamped** to their config values, not dropped (R6).
- **D32. The E6 payload sits in an `RHSaveFrame` frame tagged `'RHL6'`,** written after E5's `'RHE5'` and read after E5's reader. The layout is in section 4.
- **D33. A lost block rebuilds the rules from the mirror** (7.4):
  - `Closed` for each v19 name, sorted by name string;
  - then `Severed` for each v22 Defected name;
  - then `Embargo` for each EmbargoGrace entry;
  - then `Disrupted` for each v21 entry, with no window;
  - `NextRuleId` = count + 1.

  Pacts, tariffs, pending effects and the terms list are lost, with a Warning.
- **D34. The load repairs** (7.3 as amended). Each logs a Warning and never an Error. They are listed in section 4.

**Tests:**
- **D35. A new fail-loudly suite, `-levers`,** with checks LV0-LV12, in a new file `RHSimLeversSuite.cpp`. It is added to `rh_battery.sh`. The details are in section 5.

---

## 2. What E6 assumes E5 will have added (re-check every item at your HEAD)

E6 was designed on E5's REVISION and REFRESH. E5's names are **design names**. If E5 built any of these differently, rebase onto E5 as built. Do not re-create a second copy.

| # | E5 provides | E6 uses it for |
|---|---|---|
| A1 | The **`'RHE5'` frame** (`CurrencyTag` in F), written straight after `'RHE4'` (W:13933 today) and read straight after `ReadMarketBlock` (W:14743 today). A post-load `ReconcileReservations()` runs after it. | E6's `'RHL6'` frame goes after `'RHE5'`, both in the write and in the read. |
| A2 | **The political journal:** entries with `ClearSol`, `ExecuteAt`, `CommandId`, a sub-index, a kind and a tension delta ×1000, applied at the roll in `(ExecuteAt, CommandId, sub)` order. | Two new entry kinds: **pact sign** (+`PactTension`×1000 = +5,000) and **pact break** (−`PactBreakTension`×1000 = −5,000). |
| A3 | **The political resolvers receive the executing command.** At least `ResolveDilemma` and `ResolvePacify`, for the journal. | E6 needs `c = floor(ExecuteAt/SolLen)` and `CommandId` in `ResolveDilemma` (a Comply ends pacts at `c+2`), `ResolveCovert`, `ResolveSabotage` and `ResolveLaunder`. **E6 adds the command parameter to the three covert resolvers itself** (only `ExecuteCommand` calls them: W:9585, :9595, :9605). |
| A4 | **`CanExchange(Dir, Amount, OutWhy) const` check 3:** "a member the colony can trade with". It is implemented against `ClosedRoutes`, `DefectedRivals` and `EmbargoGrace` (or through their getters). | Migrated onto the list: `Closed`, `Severed` and `Embargo` only (D13). |
| A5 | **The mark:** `CurrencyOpenSol`, the market unit becoming mmk/kg from that sol, `bMarks` decided at booking from the clearing sol, mark reservations in `MarkReservedMmk`, `HiR`/`LoR` for credit orders after the issue, the desk clearing after `ClearConvoys`. | The markups thread into both the mark and the credit reservations and clearings (D21). The desk carries no tariff. `TariffPaidMmk` starts counting when the market counts in marks (D22). |
| A6 | **A clearing-sol input to `PlanConvoy` (UNCERTAIN).** E5 also needs the clearing sol at booking to decide `bMarks` (E5 2.3), so its builder may already have added one, and a preflight estimate for the deck. | **Reuse E5's parameter and E5's preflight estimate if they exist.** Add the parameter only if E5 did not (section 3.2). |
| A7 | **The `BlocStance` rival column, `Debug_SyncPoliticalView()`, the CU8 paired-run harness and `GetCurrencyBlockBytes()`.** | LV8 (Kestrel has `BlocStance` −1; its start is copied from CU8). |
| A8 | **`bMarketBlockLost`** (transient). | Not needed by E6: the open sol is `MarketCheckedSol`, which E4's reader seeds from `ProductionCheckedSol` on a lost frame (M:1383-1386). |
| A9 | **E5's battery baseline** (59 runs), its `-currency` suite, and its `-scale` figure. | The diff base, and the `-scale` budget. |
| A10 | **MX1's tail compare bounded to the market frame (UNCERTAIN whether E5 did it).** | Harmless either way: E6's frame is identical in MX1's on/off twins. |

---

## 3. The exact changes, per file (anchors at `d842dd0`)

### 3.1 `RHSaveFrame.h`

- Add `constexpr uint32 LeversTag = MakeTag('R','H','L','6'); // E6, the levers (v57)` beside `MarketTag` (F:22) and E5's `CurrencyTag`.
- The header comment at F:11-12 already reserves `'RHL6'`.

### 3.2 H: `Source/RedHopeSim/Public/RHSimWorldSubsystem.h`

**New types:**
- the enum `ERHTradeRuleKind : uint8` and the enum `ERHTradeRuleOrigin : uint8`;
- the struct `FRHTradeRule` (D7);
- `FRHMarketEffect { int32 ClearSol; int32 CommandId; int32 Sub; uint8 Kind /*1 theft, 2 offering*/; FName Party; int32 Slot; int64 Grams; }`;
- `FRHConvoyTerms { int32 ConvoyId; int32 EffSol; TArray<int32> LineMarkupBp; }`, the terms list, indexed like the convoy's `Lines`.

**Delete** the four members, `ClosedRoutes` (H:3947), `SabotageSols` (H:3976), `EmbargoGrace` (H:3988) and `DefectedRivals` (H:3989), and add:
- `TArray<FRHTradeRule> TradeRules; int32 NextRuleId = 1;`
- `TArray<FRHMarketEffect> PendingMarketEffects;`
- `TArray<FRHConvoyTerms> ConvoyTerms;` (by convoy Id)
- `int64 TariffPaidMcr = 0, TariffPaidMmk = 0;`
- `bool bLeversEnabled = true;` (never saved)
- the config members: `PactMinRelation` 50, `PactInfluenceCost` 15, `PactTension` 5, `PactHumanNatureShift` 3, `PactCargoKg` 2000, `PactBreakRelationHit` 25, `PactBreakHumanNatureShift` 10, `PactBreakTension` 5, `PactBetrayalTariffBp` 2500, `PactBetrayalTariffSols` 30, `TariffMaxBp` 9000, `SabotageMarketSols` 6, `SabotageCutPct` 100.

**The getters keep their signatures** and are re-implemented on the list:
- `IsRouteClosed` and `GetClosedRouteCount` (H:1787-1788; the latter counts `Closed` rules);
- `GetSabotageRemaining` (H:1825): the largest `LiveSols` among the rival's live `Disrupted` rules, or 0;
- `IsEmbargoed` and `HasDefected` (H:1837-1838);
- `GetEmbargoGrace` (H:1839);
- `GetEmbargoingRival` (H:1841): least grace, then `LexicalLess`, as HEAD.

The two inline ones (H:1787-1788, :1837-1838) must move out of line or read the list.

**The E4 hooks (H:1617-1621)** are rewritten per D30.

**The signatures that change:**
- `PlanConvoy(..., int32 EffSol, ...)` (H:3904), unless E5 already added the input (A6);
- `MarketProduceAndReprice(FRHMarket&, int32 ClosingSol)` (H:3887);
- `GetOrderReservationMcr` (H:1601) gains the line markups. **Keep a 2-argument overload, markup 0**: MS:1876 and :1878 call it with 2 arguments;
- `ResolveCovert`, `ResolveSabotage` and `ResolveLaunder` (H:1807, :1820-1821) gain `const FRHCommand&`, or the CommandId and ExecuteAt.

**The new public API** (the spec's section 8; E8 reads several of these):
- `CanSignPact`, `HasLivePact(Rival)`, `GetPactCargoKg()`;
- `GetTradeRules()`, `GetPendingMarketEffects()`, `GetLineMarkupBp(Rival, Res, Dir, Sol)`, `GetRivalCutPct(Rival, Sol)`, `GetLeversBlockBytes()`, `GetTariffPaidMcr()` and `GetTariffPaidMmk()`;
- a const read of a convoy's terms (EffSol and the booking markups), for LV8 and for E8's "25% tariff" chip.

**The new hooks** (runtime only, never saved):
- `Debug_SetLeversEnabled(bool)`;
- `Debug_AddTariff(Rival, Res, Dir, Bp, FromSol, UntilSol)`;
- `Debug_EnqueueAt(FRHCommand, double ExecuteAt)`. It assigns `CommandId = NextCommandId++` exactly as `EnqueueCommand` does (W:8703-8711), sets `IssuedAt` to now, then **appends** to `UplinkQueue` without sorting. That append is what builds the queue-order inversion;
- `Debug_SetRelation(Rival, Value)`;
- `Debug_RemoveRule(Id)` (R2; test only).

`FRHMarketClearing` (H:567-579) is transient and never saved. Add the per-line applied markup and the tariff total to it, so LV8 and LV4 can compare "booking markup == clearing markup" and "charge ≤ reservation".

### 3.3 M: `Source/RedHopeSim/Private/RHSimMarket.cpp`

**`RollMarket` (M:509-558):**
- Inside the per-sol loop, after the ring advance (M:540-544) and before `if (bLive)` (M:545), apply the pending effects with `ClearSol ≤ DueUpTo` (D27). Do this after E5's desk clearing, wherever E5 put it.
- Pass `ClosingSol` into `MarketProduceAndReprice` (call at M:549).
- After `RivalEconomies.RemoveAll` (M:554-558), and after any E5 post-loop step, run **retirement** (D10).
- The windows and the cut are tested **per pass**. After a long jump only the earliest 8 sols are walked (M:521-530). The effects follow `DueUpTo`, as the convoys do.

**`MarketProduceAndReprice` (M:560-626):**
- Take the closing sol. For each rival with a `Disrupted` window covering it (and the levers on), use `ProduceG*(100-Cut)/100` in step 2 (M:573-581).
- In step 3, compute a **local** effective `ProdSum` per slot: `Mk.ProdSumG[s]` less the cut part of each cut rival in that region. Use it at M:608 (trap T2).

**`ClearConvoy` (M:639-723):**
- Read the convoy's terms (its stored EffSol). For each filled line, recompute the markup at that EffSol with `EvaluateTradeRules` and apply D21 to the line's cost or receipt.
- `Net` = Σ buy costs − Σ sell receipts.
- `ValueMcr` (M:702) stays `BuysValue + SellsValue` before the markup.
- Add the per-line tariff to `TariffPaidMcr` or `TariffPaidMmk` (D22).
- Record the applied markups on the `FRHMarketClearing` record.
- Under E5, the conversion of a credit order (`ceil(Net*r/1000)`) applies to the marked-up Net.
- The log line "at %s cr net" (M:722) prints the marked-up Net. With no tariff it is unchanged.

**`GetOrderReservationMcr` (M:826-849):** `HiM`/`LoM` per line. The markups come from the plan's walk at EffSol.

**`PlanConvoy` (M:851-1025):**
- **Replace M:887-905** (closed, disrupted, defected, embargo) with one call to the walk's block. The texts stay exactly HEAD's, with `Rival->DisplayName`:
  - `The route to %s is closed - you cut ties when you complied with Earth`
  - `%s's production is disrupted (sabotage) - no trade until they recover`
  - `%s has sided with Earth - relations are frozen, no trade`
  - `%s has embargoed you under Earth pressure - reassure them first`

  The three earlier refusals (no contact M:866-870, already out M:877-881, the convoy count M:882-886) stay ahead of it, as do the "No market" and barter refusals.
- **The cargo cap (M:971-974):** use `PactCargoKg` when the walk's pact covers EffSol (a priced rival only), and print the effective cap in the same text.
- Store `EffSol` and the per-line markups on `OutPlan`, for `BookConvoy` to record.

**`CanPlaceOrder` and `CanPlaceStandingOrder` (M:1027-1039):** pass the preflight EffSol (see S6 in section 7):

```
max(floor((Now + OrderLagSeconds) / SolLen), MarketCheckedSol)
```

Use E5's own estimate instead if E5 already wrote one (A6).

**`BookConvoy` (M:1041-1104):**
- After the R3(b) catch-up (M:1051-1054), compute `EffSol = max(ClearSol, MarketCheckedSol)` and pass it to `PlanConvoy`.
- When the convoy is created (M:1083-1093), record `FRHConvoyTerms{Id, EffSol, booking markups}` for a priced convoy.
- Drop a convoy's terms entry once its convoy is cleared, or keep it until the convoy is removed. Either way, save only the uncleared ones (R5).

**The goodwill check (M:1236):** `bRelationshipIntact` = no `Severed`, `Embargo` or `Closed` rule names the rival. Disruption is still left out.

**Unity build:** M's helpers (`MktNameLess` and the rest, M:26-34) are file-local. If you put E6's code in a new `.cpp`, give its helpers **different names**. E4 hit a unity-build clash.

### 3.4 W: `Source/RedHopeSim/Private/RHSimWorldSubsystem.cpp`

**Version (W:37):** `RHSaveVersion = 57`. Prepend a `v57:` sentence to the comment describing the `'RHL6'` frame and the mirror.

**Config (around W:472-492):** read the 13 new rows with `Defs->GetConfigScalar`, then clamp:
- `SabotageMarketSols` to 1..30, `SabotageCutPct` to 0..100, `TariffMaxBp` to 0..9999;
- the rest to ≥ 0 (**UNCERTAIN**: the spec gives no clamps for the others; pick sane ones and pin them in LV0).

**`ExecuteCommand` dispatch (W:9555-9619):**
- Add `else if (Cmd.Verb == FName("Treaty"))`. It calls `ResolveTreaty(Cmd, Reason)` (Sign or Break, case-insensitive) and refuses any other `Text`.
- Pass `Cmd` to `ResolveCovert` (W:9585), `ResolveLaunder` (W:9595) and `ResolveSabotage` (W:9605).

**`ResolveCovert` (W:9795-9857):**
- In the clean branch (W:9821-9849), after HEAD's code and only with the levers on and an economy rival, append a Theft effect per loot item: grams = `llround(Amount*1000)`, Sub = slot order.
- In the caught branch (W:9808-9818): if the rival is a live partner, break the pact from the partner's side (D20).
- **No other change. Never add a roll.**

**`ResolveLaunder` (W:9865-9902):** after `SpendTradeLot(Offering, &Spent)` (W:9888), append an Offering effect per slot of `Spent` (economy rival, levers on).

**`ResolveSabotage` (W:9904-9940):**
- Replace `SabotageSols.FindOrAdd(Rival) = SabotageDurationSols` (W:9936) with: append a `Disrupted` rule (Origin `Sabotage`, `LiveSols = SabotageDurationSols`, window `[c+1, c+1+D)`, or none with the levers off).
- If caught and the rival is a live partner, break the pact (D20).
- The log line (W:9937-9938) is unchanged.

**`StepEarth` sabotage countdown (W:9989-10002):** for each live `Disrupted` rule in Id order, `LiveSols -= DtSols`; at `<= 0.0`, set `bLive = false` and `LiveSols = 0`. Fire the `RECOVERED` alert when the rival's **last** live disruption ends.

**`GetSabotageRemaining` (W:9859-9863), `GetEmbargoGrace` (W:10058-10062) and `GetEmbargoingRival` (W:10064-10079):** read the list.

**`StepPreemptive` (W:10081-10149):**
- **The countdown (W:10091-10112):** for each `Embargo` rule in Id order, decrement; at `<= 0`, change it **in place** to `Kind = Severed`, `Origin = Defection`, `LiveSols = 0`, keeping its Id. Then the relation floor, HEAD's alert and HEAD's log line.
- **The trigger (W:10118):** `EmbargoGrace.Num() == 0` becomes "no `Embargo` rule".
- **The filter (W:10126):** add `|| HasLivePact(Name)` (D1). Closed and defected are read from the rules.
- **The append (W:10140):** an `Embargo` rule, Origin `EarthPressure`, `LiveSols = EmbargoGraceSols`. Pick and tie-break are HEAD's.

**`ResolvePacify` (W:10151-10175):** refuse unless an `Embargo` rule names the rival (HEAD's text, W:10153-10156), then remove that rule (W:10165). Everything else is HEAD's.

**`ResolveDilemma` Comply (W:10347-10408; the close at W:10363-10373):**
- Collect the not-yet-closed `SliceActive` names.
- Cut each relation by 40, as HEAD does; the order does not matter for relations.
- **Sort the names by name string, case-sensitive ordinal** (never by FName index or row-map order), and append one `Closed` rule per name, Origin `Comply`.
- End any live pact with those rivals (D20).
- The count and the log line are HEAD's.
- This needs the command for `c` (A3).

**Save write (W:13708-13726):** keep the slots byte for byte, written from temporaries projected from the rules (D31):
- v19: `TArray<FName>` sorted `LexicalLess`, then `SolidarityHope`;
- v21: `TMap<FName,double>` then `Discovered`;
- v22: `TMap<FName,double>` then `Defected` sorted, then `Influence`.

**Save tail (W:13928-13933):** after E5's `'RHE5'` write, add:

```
RHSaveFrame::Write(Ar, RHSaveFrame::LeversTag, [this](FArchive& B){ WriteLeversBlock(B); });
```

**Load (W:14361-14385):**
- Read v19, v21 and v22 into **function-scope temporaries**. The HEAD blocks are `{}`-scoped, so hoist them, as `LoadedConvoyRival` is hoisted.
- Validate them: rival rows only; finite timers in `(0, config]`, **clamped** above config (R6). Do **not** fill live sets.
- After E5's reader (after W:14743 and E5's call), call `ReadLeversBlock(Ar, Bytes, <the mirror temporaries>)`. It builds the live rules: from the block, or from the mirror when the block is lost.
- Nothing between the v19 read and the tail reads the rules. **Check that this still holds at your HEAD.**

### 3.5 New `Source/RedHopeSim/Private/RHSimLevers.cpp` (recommended)

This file holds the rule list helpers, `EvaluateTradeRules`, `CanSignPact`, `ResolveTreaty`, `HasLivePact`, retirement, the pending-effect application, `WriteLeversBlock`/`ReadLeversBlock` and the hooks. The spec does not name a file. E4 set the precedent with `RHSimMarket.cpp`.

### 3.6 C and the other suites

- **C:** declare `int32 RHRunLeversSuite(UWorld*, URHSimClockSubsystem*, URHSimWorldSubsystem*);` beside `RHRunMarketSuite` (C:34). Register `if (FParse::Param(*Params, TEXT("levers"))) return RHRunLeversSuite(...)` beside `-market` (C:14829-14831).
- **The version literals.** Every hard-coded expected version moves from E5's 56 to 57. At HEAD these are the `Ver == 55`, `"/55"` and `(expect 55)` literals:
  - C:10867, :10873, :12147-12148, :12563, :12607, :13755, :14103, :14559, :15128;
  - `RHSimCavernSuite.cpp:738`;
  - `RHSimCreditSuite.cpp:609`, `:655`;
  - `RHSimDeepDistrictSuite.cpp:771`;
  - `RHSimDepthSuite.cpp:786`;
  - `RHSimEconomySuite.cpp:857`, `:881`;
  - `RHSimExportSuite.cpp:530`, `:599`;
  - MS:1316, :1380;
  - `RHSimTramSuite.cpp:683`;
  - `RHSimTransitSuite.cpp:1596`, `:1743`;
  - plus any E5 added.

  At your HEAD run `git grep -n -E 'Ver(sion)? == 56\b|"/56"|expect 56' Source`.
- **Leave the literal `"LoadColony v55: ..."` warnings in M (M:1385 and the rest) alone.** They name the block's own version.

### 3.7 Data

- **`docs/data/RH_Config.csv`:** add the 13 rows (D-list in 3.2), with units and a one-line note each.
- **Sync `Content/RedHope/Data/DT_Config.uasset`** with `scripts/unreal/rh_sync_datatables.py`. It runs in the editor, and only one Unreal process may run at a time. E4 did the same ("14 RH_Config rows, synced").

### 3.8 `scripts/sim/rh_battery.sh`

Append `levers` to `SUITES` (after E5's `currency`). Update the header comment and the echo count: E5 made 58 suites in 59 runs, so E6 makes **59 suites in 60 runs**.

### 3.9 Presentation (RedHope; section 9)

**K, the rival card** (`ForEachRivalRow` lambda, K:2355-2434):
- **Under the description line (K:2388-2393):**
  - the pact: "Pact since sol N", or "Sign a pact (15 Influence)". The sign button is greyed by `CanSignPact`, with the reason in its tooltip. A BREAK button carries its cost in the tooltip;
  - the tariff: "25% until sol U−1" (trap T6);
  - the disruption window: "production stopped until sol U−1", beside HEAD's "disrupted".
- **The status line (K:2379-2382)** keeps HEAD's order and reads the unchanged getters.
- **The verb buttons (K:2397-2425):** `HandleRivalVerb` (K:2479-2497) sets no `Text`, so the Treaty buttons need a handler that sets `Text` to `Sign` or `Break`.

**K, the order draft (`HandleOrderNudge`, K:2646-2651):** it clamps each line to ±1000, so one resource can never reach a pact's 2,000 kg cap. Clamp to the sim's effective cap for that rival (`GetPactCargoKg()` when `HasLivePact`). This is a spec gap; see problem P7.

**`Source/RedHope/Public/RHOrderWords.h` (`VerbText`, lines 13-24):** add `Treaty` → "Pact" (placeholder wording).

**`Source/RedHope/Private/RHCheats.cpp`:**
- `RH.Treaty <Rival> sign|break` (model it on `RH.Pacify`, :1021-1030);
- `RH.Levers` (the rule list, the pending effects, the tariff totals);
- the status flags at :1078-1083 read the getters and stay as they are.

**V** (`RHColonyVisualizerSubsystem.cpp:4249-4251`) reads the getters and is unchanged.

**Wording:** new strings go in `docs/qa/2026-10-15/gate-d-wording.md` as placeholders with a fresh prefix. **L** is unused at HEAD; check that E5 did not take it. None of them is morale or evacuation wording.

---

## 4. Save: version, frame and payload

- **Version:** v57 (W:37), strict. The previous version is refused.
- **Frame:** `'RHL6'` (`RHSaveFrame::LeversTag`), written after `'RHE5'`, the last frame in the file. It is read with `RHSaveFrame::Read`, and its payload is parsed by its own `FMemoryReader`. That reader is bounded, so a parse can never run past the payload. **Never use `Error`; repairs are Warnings.**
- **The payload** (`FMemoryWriter`; names as `FString`, never FName):

```
uint8  FormatRev = 1
int32  NextRuleId
int32  RuleCount; then each rule by Id ascending:
       int32 Id, uint8 Kind, uint8 Origin, FString Party, FString Res, uint8 Dir,
       int32 MarkupBp, int32 FromSol, int32 UntilSol, double LiveSols, uint8 bLive
int32  EffectCount; then each in (ClearSol, CommandId, Sub) order:
       int32 ClearSol, int64 CommandId, int32 Sub, uint8 Kind, FString Party, FString Res, int64 Grams
int64  TariffPaidMcr, int64 TariffPaidMmk
int32  TermsCount; then each uncleared priced convoy by ConvoyId ascending:
       int32 ConvoyId, int32 EffSol, int32 LineCount, then LineCount x int32 MarkupBp   (R5 + REFRESH: the booking markups)
```

**`Res` for "every resource" must NOT be the empty string** (trap T1). `RHSaveFrame::ReadName` refuses a length below 1 (`RHSaveFrame.cpp:103`), and every betrayal tariff is an every-resource rule. Write the sentinel `"*"`, or a `uint8 bAllRes` before the name. Pick one and pin it in LV9.

`CommandId` is `int32` on `FRHCommand` (H:36). Widen it to int64 in the payload, as 7.3 lays it out.

**The load repairs** (each a Warning):
1. **Structure → lost** (rebuild from the mirror, D33). Any of: Ids not strictly ascending, an Id ≤ 0, an Id ≥ `NextRuleId`, a count larger than the bytes, `FormatRev` ≠ 1, a CRC failure, or no frame.
2. **A rule is dropped** for any of: a kind, origin or `Dir` out of range; a `Party` that is not a rival row; a `Res` that is neither a slot nor the every-resource sentinel.
3. **`Party` is canonicalised** to the row's own FName (a case-only variant loads as the row). Duplicates are found by FName equality.
4. **`MarkupBp`:** below 0 becomes 0; above `TariffMaxBp` it is clamped.
5. **Windows (R3):**
   - a shape that is not legal for its kind is **repaired, never dropped**: `Closed`, `Severed`, `Embargo` and `Disrupted` get no window. A live `Pact` gets `[From, -1)`, or `[open+2, -1)` if `From` is −1;
   - an ended `Pact` or a `Tariff` with no legal window is dropped;
   - the ordering test applies only when both ends are ≥ 0;
   - **keep the upper clamp** (re-review O1): neither end may exceed `open + 2 + max(PactBetrayalTariffSols, SabotageMarketSols + 1)`. Clamp it.
6. **`bLive` is forced true** on every `Closed`, `Severed` and `Embargo` (R3). **Strike 7.3's "an Embargo with bLive false is dropped"** (re-review O1).
7. **`LiveSols`:**
   - an `Embargo`'s that is not finite, or ≤ 0, is reset to `EmbargoGraceSols`;
   - a `Disrupted` rule's that is not finite, or < 0, sets `bLive = false`;
   - either kind's above its config value is clamped.
8. **Exclusive kinds:** a second `Closed`, `Severed` or `Embargo` for one rival is dropped (the lowest Id is kept). An `Embargo` beside a `Severed` is dropped. **A second live `Pact` is DROPPED** (re-review O2, replacing 7.3's "ended").
9. **Effects:** a gram value ≤ 0 or above 10^9, a bad kind, or a rival without an economy is dropped. Unsorted effects are sorted.
10. **Totals:** a negative total is set to 0.
11. **Terms:**
    - an unknown or cleared convoy Id is dropped;
    - an `EffSol` below the convoy's `ClearSol` is raised to `max(ClearSol, MarketCheckedSol)`;
    - a `LineCount` that differs from the convoy's lines drops the entry;
    - markups are clamped to `[0, TariffMaxBp]`;
    - **a convoy with no entry uses `EffSol = max(ClearSol, MarketCheckedSol)`** (re-review O3), with its booking markups read at that sol.
12. **The open sol** for the clamps is `MarketCheckedSol`, read by E4's reader before this one.
13. **The mirror check:** after a good read, compare the projection (D31) with the mirror just read. If they differ, a Warning names the difference, and **the rules win**.

**Checkpoints:** the bump refuses Midgame and Late. Re-cut them with `bash scripts/unreal/rh_checkpoints.sh all --force` and load both.

---

## 5. The `-levers` suite (`Source/RedHopeSim/Private/RHSimLeversSuite.cpp`)

It is fail-loudly: a failure logs an Error and makes the suite red. No path logs an Error on purpose.

It reuses these hooks:
- E4's `Debug_RollMarket`, `Debug_SetRivalStore` and `Debug_SetMarketPrice` (H:1609-1612);
- `Debug_InjectRival` (`RHDefinitionsSubsystem.h:197`);
- `Debug_AddTension` (H:1774), `Debug_AddInfluence` (H:2741) and `Debug_GrantCredits` (H:1527);
- E5's `Debug_SyncPoliticalView`.

| Check | What to assert (expected values where the spec gives them) |
|---|---|
| **LV0** | **Data.** The 13 config rows with their defaults (50, 15, 5, 3, 2000, 25, 10, 5, 2500, 30, 9000, 6, 100) and their clamps. The CSV equals the live DT. `SabotageMarketSols == round(SabotageDurationSols)` (6). `TariffMaxBp < 10000`. |
| **LV1** | **Inertness.** Twins with the levers on and off. Each runs, in each band, the scripts of `-covert`, `-espionage`, `-preemptive` and `-solidarity`, plus 10 sols of standing trade. The digest is bit-identical, except rival stores, prices and credit balances. The digest covers relations, hidden tensions, Influence, HumanNature, identity, tension, every refusal text, every timer, `CovertAttempts`, stocks, Hope and the quota phase. The saves are byte-identical up to E1's frame, mirrors included. A third twin with no sabotage and no theft is identical in credits and prices too. Under E5 (R2 of E5), the "off" twin must still journal; the levers switch does not touch E5. |
| **LV2** | **The migrated sequences, hard-checked** in fresh colonies, each with a save round trip: `-covert` **CXCX** and axis **−24**; `-espionage`'s disrupted text, `remaining` within 0.05 of **5.8**, recovery on HEAD's step, `caught=0` then `caught=1`, and the v21 round trip **5.9→5.9**; `-preemptive`'s embargo at tension **78**, grace **8**, `grace=7.8`, Pacify (Influence **33→13**, tension **78→58**), the re-embargo at **100** with a convoy out, `(soured - goods only)`, and the defection at relation **5**; `-solidarity`'s **1 route(s) closed**, relation **5**, tension **55**, and the route-closed refusal while embargoed. Also `-icelow` and `-arrivals`: the embargo at tension **75**, grace 8, then the defection at relation **5**, with hard checks on the embargo's sol and the defection step (R4). |
| **LV3** | **Precedence (R2 split).** (a) One rival gets an `Embargo` (tension forced to 80), then a sabotage, then a Comply. The refusal is the closed text. After `Debug_RemoveRule(closed Id)` it is the disrupted text, and once the downtime ends, the embargo text. (b) A second rival's embargo runs out into `Severed`, and its text is the defection text. (c) Two rivals injected in reverse name order and closed by one Comply give `Closed` rules in name order, and saved bytes equal to the forward order. A 12-rule list under a seeded LCG permutation gives an unchanged verdict. |
| **LV4** | **Tariff arithmetic by hand.** Buy and sell lines at 0, 1, 2,500 and 9,000 bp, including markups that are not whole (rounding against the colony). Two tariffs on one line sum and clamp. Per resource and per direction. Window edges: `FromSol` is in, `UntilSol` is out. The reservation covers the charge when the price moves a full step, in credits and in marks. `TariffPaidMcr` and `TariffPaidMmk`. Prices, the tally, the bloc tally and the rate are bit-identical to an untaxed twin. **Do not name LV4 for mutation 4** (re-review). |
| **LV5** | **Pacts.** Every `CanSignPact` refusal with its text. Signing: Influence −15, tension +5, HumanNature +3, **one journal entry of +5,000**, and the log line. The cargo cap: a 1,500 kg order with `EffSol` c+1 is refused, and with c+2 accepted. Breaking: relation −25, HumanNature −10, tension −5 (journaled −5,000), and the betrayal tariff `[c+2, c+32)`. A caught covert op against a partner ends the pact with the tariff; a clean one does not. A Comply ends the pact with no tariff. Re-signing is refused while the tariff runs. **Q-A (a):** with tension at 80, Earth embargoes Meridian (discovered), never the partner. With no other neighbour, nobody. |
| **LV6** | **Sabotage moves prices.** On MX2's fixture (MS:590ff), a sabotage with `ClearSol` c gives the window `[c+1, c+7)`. At each covered roll, Zarya's stores and traded prices equal **hand-computed integers written in the suite**, and the roll after the window is back on the undisturbed path. The live refusal and `LiveSols` are HEAD's. **A second sabotage at c+3 makes a second rule, and `GetSabotageRemaining` = 6** (mutation 16). A barter rival shows no window effect. |
| **LV7** | **Thefts and offerings.** A clean theft is applied at the roll closing its `ClearSol`, after the convoy clearing, with `min(store)`. A caught op records nothing. A Launder's offering lands in the store. A barter rival records nothing. E1's legs are HEAD's. |
| **LV8** | **Era parity.** The section 6 run as amended by R1 and the re-review; see the details below this table. |
| **LV9** | **Save.** A colony with every kind, two pending effects, a terms entry and a moved market: save, load, save is byte-identical. The mirror slots have HEAD's layout. Every repair in section 4 is exercised on a doctored file (use `RHSaveFrame::Reseal` so a doctored payload reaches the checks), with the colony loaded. A flipped payload byte (CRC) and a wrong length rebuild from the mirror, and no closed route reopens. **The "forward reading only" fixture (mutation 14):** destroy the tag of the frame **immediately before** `'RHL6'` (E5's `'RHE5'`), as MS:1383 does for `'RHE4'`. E6's block must still be found by the walk from the end (trap T5; the spec's "E3's export count off by one" no longer misaligns anything since v54). A case-only `Party` loads as the row. The previous version is refused. |
| **LV10** | **Ruling 11 and Q4a.** Tariffs at 9,000 bp, a pact and a sabotage, against a twin with none, over 10 sols: Hope, the population, growth and every colony stock are bit-identical. |
| **LV11** | **No pump.** 10,000 seeded lines: each buy costs at least its value, and each sell receives at most its value. A save with a negative `MarkupBp` loads as 0. |
| **LV12** | **The mutations** (section 6). |

**LV8 in detail:**
- **Start:** copy E5's CU8 start (no convoy out, sol 16.0, one step, save `lv8_start`, both twins load it). The era twin first advances 30 s.
- **Rivals:** Zarya, plus an injected **Kestrel** (30 km, E4 R7's economy as in MS:1674, `RelationStart` 60, `BlocStance` −1). Kestrel's covert roll at attempt 1 must give U ≥ 0.5: the review computed **U = 0.727**, and Zarya's at attempt 0 **U = 0.907**. Assert both in the suite.
- **At load:** `Debug_AddInfluence(40)` and `Debug_GrantCredits(20,000 cr)`.
- **Asserts:** no DustStorm in sols 16-36; no E2 award or E3 launch or sale in the window; E4 R7's schedule rules.
- **The schedule** (`Debug_EnqueueAt`). R1 replaces the first row:
  - queued at 16.2, due 17.0 + 10 s: `MarketOrder Kestrel Ice:1500`;
  - queued at 16.3, due 17.0 − 20 s: `Treaty Kestrel Sign`. The pact window is `[18, -1)`, so the order is **refused in both twins**;
  - queued at 18.2, due 19.0 + 10 s: `MarketOrder Kestrel Ice:1500`, **accepted in both** at markup 0;
  - queued at 18.3, due 19.0 − 20 s: `Treaty Kestrel Break`. This gives the pact `[18, 20)` and the tariff `[20, 50)`;
  - **re-review fix:** `Debug_SetRivalStore(Kestrel, Ice, ≥1500)` before the roll closing 19, so "charge ≤ reservation" bites;
  - due 21.0 − 10 s: `Sabotage Zarya` (window `[21, 27)`);
  - 23.2: `Debug_SetRivalStore(Kestrel, Ice, 150)`;
  - 23.5: `MarketOrder Kestrel Ice:100` (fills 100);
  - due 24.0 + 10 s: `Covert Kestrel` (the 75 kg leaves at the roll closing 24);
  - 27.5 and 32.5: `MarketOrder Zarya Struct:-200`;
  - 27.5: `MarketOrder Kestrel Ice:100` (under the tariff).
- **The bar (R1).** At every threshold from 16 to 36, int64-equal across the twins: prices, rival stores, fills, charges, the applied markup per line, the cut per rival per roll, the effects as applied, and `TariffPaidMcr`.
- **Per order, in each twin:** the stored booking markup equals the clearing markup, and the charge is at most the reservation.
- **Rules** are compared as the set of `(kind, rival, origin, From, Until)` over the rules whose window covers a sol ≥ the open sol. Rules with no window are left out, and Ids are never compared.
- **Pending effects** are compared only as applied.
- The convoys' home sols are compared, with E5's boundary-step exemption (re-review O5). State in a comment that no rule-making act is due in the first 30 s of a sol (re-review O6).

---

## 6. Mutations to run (LV12): build, run `-levers`, restore by hash

Each must turn the named check red.

| # | Mutation | Must fail |
|---|---|---|
| 1 | The refusal is the first block by Id, not by kind. | LV2 (`-solidarity` reads "embargoed"), LV3 |
| 2 | A timer is decremented after its test instead of before. | LV2 (`remaining`, the defection step) |
| 3 | The Comply's `Closed` rules are appended in row-map order. | LV3(c) (bytes differ with reverse-injected rivals) |
| 4 | A pact's or a tariff's window starts at c+1. | **LV8** only: the pact-start fixture (the agent twin accepts the 17.0 + 10 s order, the era twin refuses it) and the per-order booking == clearing markup check. Not LV4. |
| 5 | A pact's end is at e+1. | LV8 (the agent twin refuses the 19.0 + 10 s order) |
| 6 | The sabotage window is keyed by the executing step's sol. | LV8 (`[22, 28)` against `[21, 27)`) |
| 7 | A theft is applied to the store at execution. | LV8 (the era twin fills 75 kg against 100) |
| 8 | A markup is rounded toward the colony. | LV4 |
| 9 | The reservation is computed without the markup. | LV4 (the charge outruns the reservation) |
| 10 | The markup is counted into the tally or the bloc tally. | LV4 (a price or the rate moves) |
| 11 | A sell markup adds to what the colony receives. | LV11 |
| 12 | The mirror is not written. | LV9 (a lost block reopens a closed route) |
| 13 | No CRC check. | LV9 (a flipped byte loads silently). The CRC test lives in shared `RHSaveFrame::Read` (`RHSaveFrame.cpp:85`), so mutate it there and restore it by hash. |
| 14 | Forward reading only. | LV9, with the trap T5 fixture: the frame before `'RHL6'` has its tag destroyed. |
| 15 | An **unconditional** extra `RollCovertDetected` in `ResolveCovert`. | LV2 (CXCX changes) |
| 16 | A repeated sabotage updates its rule in place. | **LV6** (the second rule at c+3). Not LV8 or LV9. |

---

## 7. The battery and the playtest

**Commands:**
- `bash scripts/sim/rh_battery.sh 10`: expect `BATTERY COMPLETE: 0 suite(s) not green`, with 60 runs.
- `python3 scripts/sim/diff_battery.py <E5 baseline> Saved/RHBattery`.
- Freshness: the logs are newer than both dylibs, and `find Source/RedHopeSim docs/data -newer Saved/RHBattery/baseline.log -type f` prints nothing.
- Both pins hold in `baseline.log`: `power: gen 50 W load 20 W` and `deposit Regolith_A: 40645 kg`.

**Lines expected to move, relative to E5's baseline, and why. Every other line must be identical, timing digits aside:**
1. **The save-version lines** (56 → 57). Every line that prints the read version or the expected-version text:
   - "the file says version", in CV8, K6, DP11, DD9, TM6, TR18, TN1/TN8, SH12, X15, FRAMING e5 and FARICE F16;
   - "CR9 v", "EC6 the file says", "XE10 v" and "MX9 v";
   - the "Save format mismatch (... /57)" texts;
   - any `-currency` line E5 added that prints the version.

   E8's count at HEAD was 27 such lines, plus E5's.
2. **The whole-file size lines not masked** by `diff_battery.py`. The script masks only `\(\d+ bytes`. They move because the file gains the `'RHL6'` frame, at least 20 + 33 bytes:
   - **CV8** (`caverns.log`, "(12059 -> 12139 bytes)" at HEAD);
   - **DP11** (`depth.log`, "(12087 -> 12119 bytes)");
   - **EC1** ×2 (`economy.log`, "both saves are 13100 bytes");
   - **DC9** (`deepcommute.log`, "the 13408-byte file", `RHSimDeepCommuteSuite.cpp:713`);
   - **UNCERTAIN:** any `-currency` line that prints an unmasked whole-file size.
3. **`levers.log`, a NEW SUITE.**

**What NOT to expect:**
- **MX1 and MX9 do not move under `diff_battery.py`**, even though the handoff lists them.
  - MX1 (MS:564) prints its sizes as "(11761 bytes)" and "(8717 bytes)". MX9's round-trip line (MS:1359) prints "(12173 bytes)". All of these are masked to "(N bytes".
  - MX1's market-frame offset does not change, and MX9's payload line ("3343 payload bytes") does not change: E6 never touches `'RHE4'`.
  - Only MX9's "v%u" version line moves (item 1).
- **The literal "LoadColony v55: ..." warnings in `market.log` stay as they are.**
- **MX11** (`market.log`) **must not move.** The rebuilt hooks keep its four texts, and its `Debug_SetSabotage` makes a `Disrupted` rule with no window (REFRESH, stale item 5).
- **The migrated sequences must stay line-identical:** `-covert`, `-espionage`, `-preemptive`, `-solidarity`, `-icelow` and `-arrivals`. In `-covert` and `-espionage` the thefts and the sabotage cut now silently move Zarya's stores and prices, but no line prints a store, a price or a standing-order credit:
  - `ClearConvoy` logs only non-standing orders (M:720);
  - the standing order always passes the goodwill check (M:1237).

  **If any line in those suites moves, stop and investigate. It is not an expected move.**
- **`-trade`, `-earth`, `-ending`, `-crisis` and `-market`** read only the getters, which are unchanged.

**`-scale`** (run it separately, outside the battery): 1,000 `RollMarket` calls with 8 injected rivals, 60 rules and 20 pending effects. Every digest is identical, within E5's measured figure + 0.2 ms.

**Playtest: byte-identical expected.** Run `RH_PLAYTEST_DISTRICTS=1 bash scripts/sim/rh_playtest.sh 110 e6_full guide full`, then `cmp` it against `Saved/baselines/ind_full_playtest.txt` (or E5's re-cut, if E5 made one). The reasons:
- `RHPlaytest.cpp` at HEAD sends no `Covert`, `Sabotage`, `Launder`, `Pacify`, `Solidarity`, `Convoy`, `MarketOrder` or `Treaty`;
- tension reaches only about 66 by sol 110 (a demand at sol 100.25, no embargo), so no rule is ever made;
- E6 logs nothing on a roll;
- the `.txt` holds only `[Playtest]` lines and prints no save size.

Re-check the bot at your HEAD, because it changes often.

---

## 8. UI and the click test

- **The UI work** is section 3.9. It needs `RH_CLICKTEST_RES=1440x860 bash scripts/unreal/rh_clicktest.sh` to pass **3 times**.
- **UNCERTAIN:** whether the click test must gain a pact step. Recommended: add one, modelled on the E4 market step (`RHClickTest.cpp:1754`, `QueueHas(... "MarketOrder" ...)`). It clicks SIGN on a card whose `CanSignPact` passes and asserts that a `Treaty` with Text `Sign` was transmitted.
- **Captures:** a rival card with no pact; with a live pact; after a break (the tariff line); and a sabotaged rival (the "production stopped until sol N" line beside "disrupted"). **Look at them.**
- **Log the gate** in `docs/design-decisions.md`.

---

## 9. Traps found in this preflight (follow these)

- **T1. Empty `Res`.** `RHSaveFrame::ReadName` refuses an empty string (`RHSaveFrame.cpp:103`, `Chars < 1`). 7.3's `""` = every resource would make every betrayal tariff fail to parse. The block would be lost, the rules rebuilt from the mirror, every pact and tariff dropped, and LV9 red. Use a non-empty sentinel or a flag (section 4).
- **T2. `ProdSumG` is a cached, derived sum.** It is mutable and filled at sync (M:291-329), not per roll. Never write the cut into `Mk.ProdSumG` or `E.ProduceG`. Compute the effective values locally in `MarketProduceAndReprice` for the closing sol.
- **T3. Keep the clearing markup's source single.** Charge the markup recomputed at the **stored EffSol** (R5). Store the booking markups (the REFRESH / re-review fix) only so LV8 can assert that the two are equal. Do not charge the stored value, or mutation 4's per-order check has nothing to compare.
- **T4. Mid-file temporaries.** The v19, v21 and v22 reads are in `{}` scopes (W:14361-14385). Hoist the temporaries to `LoadColony` scope, so the tail reader can rebuild from them.
- **T5. Mutation 14's fixture.** Since v54, every tail block is framed and parsed by its own reader, so a miscounted E3 payload never misaligns the outer reader. Only a **lost** frame immediately before `'RHL6'` leaves the reader standing in the wrong place: `RHSaveFrame::Read` does not move `Ar` when a frame is not found.
- **T6. Displayed end sols.** Windows are half-open `[From, Until)`. The spec's "25% until sol 91" for `[62, 92)` means print `UntilSol − 1` as the last sol charged. Apply the same rule to the `PACT BROKEN` log line's "from sol %d to sol %d" and to CanSignPact check 7's "(until sol %d)". **UNCERTAIN:** the spec never says. The modelled text ("sols 62-91", "until sol 91") supports `Until − 1`.
- **T7. The harness hooks' old overwrite semantics.** `TMap::Add` overwrote. Rebuilt:
  - `Debug_SetEmbargo` updates the existing `Embargo` in place;
  - `Debug_SetSabotage` appends a rule (the remaining time is then the max);
  - `Debug_SetEmbargo` on a `Severed` rival should be refused or do nothing, to keep D9.

  No suite at HEAD calls either hook twice on one rival (MS:1622-1660).
- **T8. Readers that E5 adds** to the four sets (at least `CanExchange` check 3) are not in the spec's migration table. At your HEAD run `git grep -n "ClosedRoutes\|SabotageSols\|EmbargoGrace\|DefectedRivals" Source` and migrate every hit. After E6 the grep must be empty, except in comments.
- **T9. -Wshadow is an error.** RedHopeSim never depends on RedHope. No new Mass fragments. Nothing is ordered by FName index.

---

## 10. Deviations the builder must NOT take

- Do **not** build R8 (Q-A (b)'s lift) or any Q-A (b)/(c) or Q-C (b)/(c) path.
- Do **not** add a field to `FRHConvoy`'s `'RHE4'` payload. MX9 pins its layout; EffSol and the markups live in `'RHL6'`.
- Do **not** call `RollCovertDetected` anywhere new, or change `CovertAttempts`, `IsNight` or the roll's inputs (CXCX).
- Do **not** let the market read `LiveSols`, `bLive` or a rule Id, or let a tariff enter a tally, the bloc tally or the rate.
- Do **not** make sabotage refuse the desk (R7), and do **not** make bloc membership a rule (2.2).
- Do **not** drop the v19, v21 and v22 slots, change their bytes, or move any mid-file byte.
- Do **not** order Comply's `Closed` rules by FName index or row-map order.
- Do **not** hand-roll a frame; use `RHSaveFrame`.
- Do **not** log `Error` on any load path.
- Do **not** charge a tariff on desk exchanges or let any rule pay the colony (`MarkupBp` ≥ 0).
- Do **not** touch the playtest bot, the click-test baseline expectations, or `Saved/baselines/`.
- **Never** run `git stash`, `checkout`, `reset` or `git add -A`. Commit only E6's files, by path, after the orchestrator verifies.

---

## 11. Re-anchored references (spec → HEAD `d842dd0`)

The REFRESH re-anchored everything to `31ffa6f`. Since then, the step-2 fixes and the visual pass moved H, W (from the political code down), K and RHCheats. M, V, C's suites and the spines did not move.

**H:**

| Spec / REFRESH (31ffa6f) | HEAD d842dd0 | Text |
|---|---|---|
| H:1603-1607 (E4's politics hooks) | **H:1617-1621** | `void Debug_CloseRoute(FName Rival) { ClosedRoutes.Add(Rival); }` |
| H:1773-1774 | **H:1787-1788** | `bool IsRouteClosed(...)`, `GetClosedRouteCount` |
| H:1823-1827 (also 1823-1825 / -1824) | **H:1837-1841** | `bool IsEmbargoed(...)` .. `GetEmbargoingRival()` |
| H:3930 | **H:3947** | `TSet<FName> ClosedRoutes;` |
| H:3959 | **H:3976** | `TMap<FName, double> SabotageSols;` |
| H:3971-3972 | **H:3988-3989** | `EmbargoGrace`, `DefectedRivals` |

**K and RedHope:**

| Spec / REFRESH (31ffa6f) | HEAD d842dd0 | Text |
|---|---|---|
| K:2180-2243 | **K:2197-2260** | the Earth / embargo card (`GetEmbargoingRival`) |
| K:2338-2405 (section 9, rival card) | **K:2355-2434** | `Defs->ForEachRivalRow([&](FName RowName, const FRHRivalRow& Row)` |
| K:2362-2365 (status line) | **K:2379-2382** | `if (S->HasDefected(RowName)) ...` |
| RHCheats.cpp:1077-1082 | **RHCheats.cpp:1078-1083** | `const double Sab = Sim->GetSabotageRemaining(Name);` |
| `RHSimDeepCommuteSuite.cpp:712` (DC9) | **:713** | `DC9 nothing derived is saved` |

**W, the uplink dispatch and the covert resolvers:**

| Spec / REFRESH (31ffa6f) | HEAD d842dd0 | Text |
|---|---|---|
| W:9516-9546 (covert verb dispatch) | **W:9579-9619** | `else if (Cmd.Verb == FName("Covert"))` .. Pacify |
| W:9688-9693 | **W:9755-9760** | `IsNight()` |
| W:9707-9725 | **W:9774-9792** | `RollCovertDetected` |
| (ResolveCovert) | **W:9795-9857** (clean branch W:9821-9849) | `ResolveCovert` |
| W:9750-9778 | **W:9817-9845** | the caught log line and the clean loot loop |
| W:9792 / 9794 | **W:9859 / 9861** | `GetSabotageRemaining` / `SabotageSols.Find` |
| W:9812-9816 / 9819-9820 | **W:9879-9888 / 9886-9887** | the Launder offering |
| W:9837-9873 / 9869 | **W:9904-9940 / 9936** | `ResolveSabotage` / `SabotageSols.FindOrAdd(...) = ...` |

**W, Earth's politics:**

| Spec / REFRESH (31ffa6f) | HEAD d842dd0 | Text |
|---|---|---|
| W:9893-10108 | **W:9960-10175** | `StepEarth` .. `ResolvePacify` |
| W:9922-9935 | **W:9989-10002** | the sabotage countdown |
| W:9938 | **W:10005** | `StepPreemptive(SubDt);` |
| W:9991 / 9997-10012 | **W:10058 / 10064-10079** | `GetEmbargoGrace` / `GetEmbargoingRival` |
| W:10014-10082 | **W:10081-10149** | `StepPreemptive` |
| W:10024-10045 | **W:10091-10112** | the embargo countdown |
| W:10051-10079 | **W:10118-10146** | the trigger |
| W:10076 | **W:10143** | `EARTH LEANS ON %s` |
| W:10084-10108 / 10086-10098 | **W:10151-10175 / 10153-10165** | `ResolvePacify` |
| W:10280-10341 / 10300-10302 | **W:10347-10408 / 10367-10369** | `ResolveDilemma` / the close |

**W, the save:**

| Spec / REFRESH (31ffa6f) | HEAD d842dd0 | Text |
|---|---|---|
| W:13641-13659 (13643, 13649, 13655) | **W:13708-13726 (13710, 13716, 13722)** | the v19 / v21 / v22 writes |
| W:13866 | **W:13933** | the `'RHE4'` write (the last frame) |
| W:13888 / 13901 | **W:13955 / 13968** | `LoadFileToArray` / the strict version check |
| W:14294-14318 / 14298 | **W:14361-14385 / 14365** | the v19 / v21 / v22 reads |
| W:14527 | **W:14604** | `disagreement left here` |
| W:14660 | **W:14743** | `ReadMarketBlock(...)` (the last tail reader) |
| W:14662-14668 | **W:14745-14751** | `// Loads land at 1x ...` |

**Unchanged since the REFRESH:**
- **W:** W:37, the spines (W:643-688, 674/685/688, 846/857/859/860), W:778-781, W:1067-1082 / 1069 / 1070.
- **C:** C:1225, 1324, 1353, 1410, 1502.
- **V:** V:4249-4251.
- **The economy suite:** `RHSimEconomySuite.cpp:63-67`.
- **Every M anchor:** M:33-34, 509, 521-530, 539, 545/549, 554-558, 560, 639, 702, 826, 851, 866-905, 971-974, 1027, 1035, 1041, 1046, 1106, 1114-1115, 1236-1237, 1383-1386.
- **MS:** MX11 at MS:1607-1661 (the REFRESH said 1606-1665).

**The section 0 anchors (at `72e338a`)** are stale twice over. Use this table, not the design's section 0.
