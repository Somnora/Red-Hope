# E6, the levers: design (2026-10-24)

Step 3 (economy), gate E6. **Design only; nothing here is built.** The code was read at HEAD `72e338a` (save v53) with `git show HEAD:`.
- **E4** (the Martian market) and **E5** (a Martian currency) are designed, re-reviewed and answered by the director, but not built. E6 is designed on E4's and E5's REVISIONs, as their re-reviews and director answers left them.
- So every E4 and E5 name below (`RollMarket`, `CanPlaceOrder`, `Convoys`, `ClearSol`, `CreditSpend`, `RivalEconomies`, `CanExchange`, the political journal, ...) is a design name. The builder rebases onto E4 and E5 as built.
- The **economy-fixes gate** (the code review's should-fix items 1-3, `docs/qa/2026-10-24/economy-code-review.md`) is also queued ahead of E6.
- Line numbers are at HEAD and will move, so every anchor is also named by its function.

Short names used below:
- "W" is `Source/RedHopeSim/Private/RHSimWorldSubsystem.cpp` and "H" is its header.
- "C" is `RHSimCommandlet.cpp`.
- "K" is `Source/RedHope/Private/RHCommandDeckPanels.cpp`, "V" is `RHColonyVisualizerSubsystem.cpp`, "P" is `RHPlaytest.cpp`.
- "E4" is `docs/qa/2026-10-24/e4-martian-market-design.md`, and "E5" is `docs/qa/2026-10-24/e5-martian-currency-design.md`.
- "E4 R5" is E4's REVISION, fifth item; "E5 R4" is E5's.

**Binding rulings:**
- **No combat, ever** (`docs/design-decisions.md`, "The grand vision", ruling 1). Conflict is trade, espionage, politics and resilience. Defence means shielding and protected convoys; offence means leverage: embargoes, covert operations, competition for colonists through Hope. The M4 ruling (2026-07-07m) re-skinned "Total War" as a cold-war severance and "feral robots" as a sabotage downtime.
- **Politics stays** (ruling 5): Earth, the CEO arc, the Solidarity Dilemma, the identity axis, rivals, treaties, trade blocs, embargoes and covert operations.
- **Q4a and ruling 11:** there is no money inside the city, and Hope is never spent or valued.
- **Q1a, Q2a, Q3a:** Earth pays in credits and takes the quota. The Martian currency is a political act (E5).
- **E4's director answer:** the standing trade is settled at market prices (Q-A a); convoys stay timed trips, one per neighbour and three in all (Q-B a); buying on Mars and selling to Earth is allowed (Q-C a).
- **E5's director answer:** issuing the mark adds tension +10 and multiplier −0.10 (Q-A a). A Comply withdraws the mark (Q-B a). The desk has a 3% spread and a cap of 500 a sol each way (Q-C a). The Comply redemption loophole is accepted.
- **The plan's E6 risks** (`docs/qa/2026-10-20/economy-plan.md`):
  - rules are a list ordered by Id, and a trade is checked against every rule in order;
  - the existing covert roll is content-seeded and stays as it is;
  - the existing embargo and closed-route sets migrate into the list, which a version bump makes safe;
  - `-covert`, the embargo and the Dilemma suites keep their exact outcome sequences (for example `-covert`'s CXCX).
- **E5 risk 2:** every political act that moves a price or the rate must be keyed exactly first, or the bands split.

## What E6 builds, in one paragraph

Every rule about **who may trade with whom, and at what markup** becomes an entry in one list, `TradeRules`, ordered by Id.
- **Today's four refusals become the first four kinds:**
  - a route closed by a Comply;
  - a neighbour's production disrupted by sabotage;
  - a neighbour that has defected to Earth;
  - a neighbour embargoing the colony under Earth's pressure.

  They keep HEAD's timing, arithmetic and texts exactly.
- **Two new kinds** arrive:
  - **a pact**, a bilateral treaty the colony signs with a neighbour, paying Influence and Earth's displeasure;
  - **a tariff**, a markup on the colony's trade with one neighbour, collected by that neighbour.
- **Every trade is checked against the whole list:** a convoy, a market order, the standing trade and a desk exchange. The walk returns the refusal, if any, and the markup on each line.
- **Covert operations now move prices** through E4's own bounded update, and nothing else touches a price:
  - a sabotaged neighbour makes nothing for the six whole sols after the act;
  - a clean theft comes out of the victim's store;
  - a peace offering goes into it.
- **Determinism.** Every market-facing effect is keyed to the act's own scheduled sol (`ClearSol`, as E4 keys its clearing), so prices stay identical in both bands. The live gates (what may be booked right now) keep HEAD's live timing.
- **Inertness.** With nothing beyond today's political state, the battery and the 110-sol playtest do not move.

---

## 0. What HEAD gives E6 (politics, read at `72e338a`)

**The four sets** (H:3741, :3770, :3782-3783):
- `TSet<FName> ClosedRoutes`. **Only a Comply writes it**: `ResolveDilemma` (W:10457-10518) walks `ForEachRival`, which visits `SliceActive` rows only, adds every route not yet closed, and cuts each relation by 40. **Nothing ever removes one**; only a load empties and refills the set (W:14424).
- `TMap<FName, double> SabotageSols`.
  - `ResolveSabotage` (W:10014-10050) sets the rival's timer to `SabotageDurationSols` (6) with `FindOrAdd(...) =`, so a repeat resets the timer rather than adding to it.
  - `StepEarth` counts it down with `It.Value() -= DtSols` and removes the entry at ≤ 0 (W:10099-10112).
- `TMap<FName, double> EmbargoGrace`. `StepPreemptive` (W:10191-10259) has three parts:
  - **Countdown.** It counts the grace down in the same way. At ≤ 0 it removes the entry, adds the rival to `DefectedRivals`, floors its relation at 5, and logs `=== DEFECTION: %s (relation %.0f) ===`.
  - **Trigger.** It fires only when `EarthTension >= 75` and no embargo exists (`EmbargoGrace.Num() == 0`), so there is one embargo at a time.
  - **Target.** It walks `ForEachRivalRow` (dormant rows included). It skips any rival that is unavailable, on a closed route or defected. It picks the highest relation, with ties broken by `FName::LexicalLess`.
- **`ResolvePacify`** (W:10261-10285) removes an embargo for 20 Influence. It also lowers tension by 20 and raises HumanNature by 4.
- `TSet<FName> DefectedRivals`. Nothing ever removes one.

**The refusal chain** (the `Convoy` verb, W:9485-9520), in this order:
1. no contact: `No trade contact '%s'`;
2. already out: `The convoy is already out (to %s)`;
3. **closed:** `The route to %s is closed - you cut ties when you complied with Earth`;
4. **disrupted:** `%s's production is disrupted (sabotage) - no trade until they recover`;
5. **defected:** `%s has sided with Earth - relations are frozen, no trade`;
6. **embargo:** `%s has embargoed you under Earth pressure - reassure them first`.

The precedence shows in the battery. In `-solidarity`, Zarya is both embargoed (sol 13, tension 100) and closed (the Comply) when the convoy is refused, and the line reads "The route to Zarya Station is closed".

**The other readers:**
- The convoy-home goodwill check: `bRelationshipIntact = !Defected && !Embargo && !Closed` (W:9800-9801). Sabotage is not part of it.
- The getters `IsRouteClosed`, `GetClosedRouteCount` (H:1651-1652), `GetSabotageRemaining` (W:9969), `IsEmbargoed`, `HasDefected` (H:1701-1702), `GetEmbargoGrace` (W:10168) and `GetEmbargoingRival` (W:10174-10189; least grace, then `LexicalLess`).
- **RedHope reads them only through these getters:**
  - `RHCheats.cpp:1066-1071`;
  - V:4246-4248;
  - K:2003-2066 and K:2159-2162.

**Covert operations** (`Covert`, `Sabotage`, `Launder`, W:9580-9610):
- **The roll** (`RollCovertDetected`, W:9884-9902) is `HashCombine(FCrc::StrCrc32(*Rival.ToString()), CovertAttempts * 2654435761u)`, with the saved counter incremented on every roll. It is compared with `P = 0.5 × (1 − relation/100 × 0.5)`, × 0.4 at night.
  - **Not band-exact at HEAD.** `IsNight` reads the clock at execution (W:9865-9870), and the relation is the live one, so an op's outcome is not band-exact at HEAD.
  - **The era band** runs an op at its step's end, up to 60 s after `ExecuteAt`. So near dawn or dusk, or within a step of a convoy coming home, the two bands can roll different P.
  - E6 does not change this (section 4.4).
- **A clean theft** takes `RoundToGrams(ExportLot × 0.5)`: 75 kg of Ice from Zarya (W:9927-9955). **It comes from nowhere**: the rival loses nothing.
- **Sabotage.** Its only effect on trade is refusal 4. It touches no production, because at HEAD a rival has none.
- **Launder** spends the rival's `ImportLot` (100 Struct) out of the colony's stores as a peace offering (W:9989-9993). It goes nowhere.
- **There are no cooldowns or costs** beyond HumanNature (−6 per covert op, −8 per sabotage) and, for Launder, the goods.

**No treaty, bloc or tariff exists in code.** A grep of `Source` and `Config` for `treaty|bloc|alliance|tariff|accord|pact|agreement` finds only an unrelated "disagreement" (W:14653).

**Save.** The sets are written mid-file:
- v19: `Closed`, sorted with `LexicalLess`, then `SolidarityHope` (W:13775-13779);
- v21: `SabotageSols` (a raw TMap), then `Discovered`, sorted (W:13783-13787);
- v22: `EmbargoGrace` (a raw TMap), then `Defected`, sorted, then `Influence` (W:13789-13793).

The reads are at W:14420-14444. The file is loaded whole into a byte array (`FFileHelper::LoadFileToArray`, W:14018) and read through a proxy that writes FNames as strings. The version check is strict (`RHSaveVersion = 53`, W:36; W:14031).

**The suites print their outcomes and assert none of them.** `-covert` (C:1322-1401), `-espionage` (C:1408), `-preemptive` (C:1500-1594) and `-solidarity` (C:1223-1317) log lines with "expect" notes. A suite is red only on an Error line. Regressions show only through `scripts/sim/diff_battery.py`. The sequences in `Saved/baselines/battery_post_ind`:
- `-covert`:
  - `COVERT CLEAN ... took 75 Ice`, `COVERT CAUGHT ... (relation 10 ...)`, clean, then caught;
  - `COVERT sequence: CXCX ... axis=-24`;
  - one convoy, then the v20 round trip.
- `-espionage`:
  - clean, caught, launder;
  - `SABOTAGE: Zarya Station offline 6 sols (caught=0, ...)`, then the disrupted refusal, `remaining=5.8`;
  - recovery and a convoy at sol 7;
  - Meridian found at sol 12, then a second sabotage with `caught=1`;
  - the v21 round trip with `sabotage 5.9->5.9`.
- `-preemptive`:
  - a convoy home (Influence 3, HumanNature 2);
  - `EMBARGO: Zarya Station (Earth tension 78, grace 8 sols)`, the embargo refusal, `grace=7.8`;
  - Pacify (Influence 33 → 13, tension 78 → 58);
  - a convoy out, then a re-embargo at tension 100 while it is out;
  - `delivered Ice:150, relation 42 (soured - goods only)`;
  - `DEFECTION: Zarya Station (relation 5)`;
  - the v22 round trip.
- `-solidarity`:
  - no-demand refusal;
  - Defy (axis +20, relation 45);
  - an embargo at tension 100;
  - Comply (`1 route(s) closed`, relation 5, tension 55);
  - the closed-route refusal;
  - the v19 round trip.

**The playtest bot sends no political order.** `git show HEAD:Source/RedHope/Private/RHPlaytest.cpp` contains none of `Covert`, `Sabotage`, `Launder`, `Pacify` or `Solidarity`, and no `Convoy` order.
- In the 110-sol baseline (`Saved/baselines/ind_full_playtest.txt`), tension reaches 66 at sol 110: a demand, no embargo.
- Meridian is found at sol 35.87.

---

## 1. Rule entries

### 1.1 The entry

A new struct in H, `FRHTradeRule`:

| Field | Type | Meaning |
|---|---|---|
| `Id` | int32 | assigned from `NextRuleId` (saved, starts at 1), never reused |
| `Kind` | uint8 enum | `Closed` = 1, `Disrupted` = 2, `Severed` = 3, `Embargo` = 4, `Pact` = 5, `Tariff` = 6. **The values of the four blocking kinds are their precedence**, HEAD's check order. |
| `Origin` | uint8 enum | `Comply`, `Sabotage`, `Defection`, `EarthPressure`, `Signed`, `Betrayal`, `Harness` |
| `Party` | FName | the neighbour (a rivals row). It is saved as its name string and canonicalised to the row's own FName on load. |
| `ResSlot` | int32 | tariffs only: an E1 slot index, or −1 for every resource. It is saved as the resource's name string, `""` for every resource. |
| `Dir` | uint8 | tariffs only: 1 = on what the colony buys, 2 = on what it sells, 3 = both |
| `MarkupBp` | int32 | tariffs only: 0 .. `TariffMaxBp` |
| `FromSol`, `UntilSol` | int32 | **the market window, exact**: the terms apply to a trade whose clearing sol s has `FromSol ≤ s < UntilSol`. −1 means none (`FromSol`) or open-ended (`UntilSol`). |
| `LiveSols` | double | the live timer: an embargo's grace, or a disruption's downtime. **HEAD's arithmetic, unchanged.** |
| `bLive` | bool | the live gate: the rule refuses or protects now |

Each kind uses the fields as follows:

| Kind | Live part (gates booking; HEAD's timing) | Market part (exact window) | Ends |
|---|---|---|---|
| `Closed` | always live: refuses | none | never (HEAD) |
| `Disrupted` | live while `LiveSols > 0`: refuses | the production cut, `[c+1, c+1+D)` (section 4.1) | retired once the timer has run out and the window has passed |
| `Severed` | always live: refuses | none | never (HEAD) |
| `Embargo` | live while it exists: refuses; `LiveSols` = the grace | none | Pacify removes it; at grace ≤ 0 it becomes `Severed` in place, keeping its Id |
| `Pact` | live from signing until it ends: protects (Q-A) | the bigger convoy, `[c+2, e+2)` | retired once ended and the window has passed |
| `Tariff` | no live part | the markup, `[c+2, c+2+N)` | retired once the window has passed |

Here c is the `ClearSol` of the act that created the rule, `floor(ExecuteAtSimSeconds / SolLength)`, and e is that of the act that ended it.

### 1.2 The list

- **`TArray<FRHTradeRule> TradeRules`** is kept in ascending Id order. Appends only add at the end, so it never needs sorting.
- **Ids are assigned** at act executions and at live events (an embargo, a defection). They are never assigned at a retirement, and never reused.
- **Ids are not market inputs** (section 6). Nothing a price, store, fill or charge depends on reads an Id or the list's order. The order fixes only the walk, the log and the saved bytes.
- **Exclusive kinds.**
  - At most one `Closed`, one `Severed` and one `Embargo` per rival, and never an `Embargo` beside a `Severed`.
  - At most one *live* `Pact` per rival.
  - `Disrupted` and `Tariff` rules may repeat (section 4.1 and 3.2).
- **Retirement** runs at the end of each `RollMarket` roll, after everything else. It removes a rule whose market window has passed (`UntilSol` ≤ the open sol) and whose live part is over. Retirement changes only what is saved; no Id moves.

### 1.3 The check: `EvaluateTradeRules(Rival, EffSol, Lines) const`

It walks **every** rule in Id order and returns one verdict:
- **The block.** Among the live blocking rules naming the rival, take the one with the lowest `Kind` (the precedence), then the lowest Id. Its HEAD text, with the row's `DisplayName`, is the refusal.
- **The markup per line.** The sum of `MarkupBp` over every `Tariff` rule where all of these hold:
  - it names the rival;
  - its `ResSlot` is the line's slot or −1;
  - its `Dir` covers the line's direction;
  - its window covers `EffSol`.

  The sum is clamped to `[0, TariffMaxBp]` (section 3).
- **The pact.** Whether a `Pact` rule names the rival with a window covering `EffSol`. This sets the convoy's cargo cap (section 2.1).

**Why the walk is in Id order and the text is by kind.**
- Sums and "is there any" do not depend on order, so the verdict is the same whatever the order. The Id order is what the plan asks for, and it fixes the logs, the save and every future tie-break.
- The **text** must be HEAD's whatever order the rules were made in. `-solidarity`'s Zarya has an `Embargo` (made first, lower Id) and a `Closed` (made second), and HEAD prints the closed text. Precedence by kind keeps that line; first-match by Id would not (mutation 1).

**`EffSol` is the sol whose roll will clear the trade:** `max(ClearSol, O)`, where O is the market's open sol at booking. That is E5's "clearing sol", E4 R3(b)'s late booking. For a trade that does not clear in the market (a barter convoy, a pact, a desk order's partner check), the walk is used for the block only.

**Who calls it**, each in the place of HEAD's checks 3-6, after "no contact" and "already out", with HEAD's texts:
- the `Convoy` verb (the barter and the standing order);
- E4's `CanPlaceOrder` (written orders and the standing order of an economy rival). For the markups and the cargo cap, it also runs at the clearing (section 3.2);
- E5's `CanExchange`, check 3: "a member the colony can trade with" means available, with no live block. This is a deliberate harmonisation: E5 listed route, defection and embargo, and E6 adds the disruption;
- E6's `CanSignPact`, check 4.

**Who does not call it:**
- `Covert`, `Sabotage`, `Launder` and `Pacify`. HEAD lets them through whatever the politics (a Launder can warm a defected rival, W:9996-9997), and E6 keeps that.
- Earth's manifest, launches and sales. Politics reaches Earth's terms only through the requisition multiplier (E2, E3). **No rule names Earth in E6.** `Party` is a name, so a later gate can name Earth or a settlement.

### 1.4 The migration (HEAD's four sets become rules)

The four members are deleted. Each writer and reader moves as follows:

| HEAD | E6 |
|---|---|
| `ResolveDilemma` Comply: `ClosedRoutes.Add` per active row (W:10477-10479) | It collects the names that are not yet closed, **sorts them by name string with a case-sensitive ordinal compare** (never by FName index or row-map order), and appends one `Closed` rule (Origin `Comply`) per name. Relation, count and log line are as HEAD. It also ends any live pact with those rivals (section 2.1). |
| `ResolveSabotage`: `SabotageSols.FindOrAdd(R) = 6` (W:10046) | It appends a **new** `Disrupted` rule (Origin `Sabotage`, `LiveSols = SabotageDurationSols`, window as section 4.1). The roll, axis, relation, hidden tension, alerts and log line are as HEAD. |
| `StepEarth` sabotage countdown (W:10099-10112) | For each live `Disrupted` rule in Id order: `LiveSols -= DtSols`; at ≤ 0, `bLive = false` and `LiveSols = 0`. The `RECOVERED` alert fires when the rival's **last** live disruption ends. |
| `GetSabotageRemaining(R)` | The largest `LiveSols` among the rival's live `Disrupted` rules, or 0. |
| `StepPreemptive` countdown (W:10201-10222) | For each `Embargo` rule in Id order: `LiveSols -= DtSols`. At ≤ 0, `Kind = Severed`, `Origin = Defection`, `LiveSols = 0`, the relation floor, and HEAD's alert and log line. Q-A (b) adds a branch here. |
| `StepPreemptive` trigger (W:10228-10256) | The trigger condition becomes "no `Embargo` rule". The target filter reads `Closed` and `Severed` rules, and Q-A (a) adds "not a live partner". It appends an `Embargo` rule (Origin `EarthPressure`, `LiveSols = EmbargoGraceSols`). Its pick and tie-break are HEAD's. |
| `ResolvePacify` (W:10261-10285) | It refuses unless an `Embargo` rule names the rival, with HEAD's texts, and removes that rule. Everything else is HEAD's. |
| The goodwill check (W:9800-9801) | Intact means no `Severed`, `Embargo` or `Closed` rule names the rival. Disruption is still left out, as HEAD. |
| The refusal chain (W:9502-9520) | `EvaluateTradeRules`'s block, with HEAD's four texts. |
| The getters (H:1651-1652, :1701-1705; W:10168-10189) | Same signatures, read from the list. `GetEmbargoingRival` keeps "least grace, then `LexicalLess`". `GetClosedRouteCount` counts `Closed` rules. |
| Save v19 / v21 / v22 (W:13775-13793) | **Kept as a mirror** (section 7.2), written from the rules. |

**The arithmetic is HEAD's, statement for statement.** Each timer is a double, decremented by the same `DtSols` in the same function at the same point in the step, and tested with the same `<= 0.0`. So every `grace=7.8`, `remaining=5.8` and defection step in the battery lands on the same step.

**Multiple timers.** HEAD's maps iterate in their own order, while the rules iterate in Id order. The two differ only when two rivals' timers expire on the same step. Then only the order of two alerts changes; the commandlet logs no alert, and no suite has two rivals under a timer.

**A repeated sabotage** appends a second `Disrupted` rule rather than resetting the first. The live remaining time is the largest `LiveSols` (always the newest, because each starts at 6). So `GetSabotageRemaining`, the refusal and the v21 mirror equal HEAD's reset semantics. The new rule gets its own exact market window (section 4.1).

**Why no update in place.** If a rule were updated in place until it retired, its Id would depend on when it retired. Retirement reads a live timer, so it is band-dependent. Appending keeps every Id an act assigns identical in both bands.

---

## 2. Treaties and blocs

### 2.1 The pact (new)

A **pact** is a bilateral treaty between the colony and one neighbour. It is the only treaty in E6. Its placeholder name is for the director, and its wording is a Gate-D placeholder.

**Signing: a new uplink verb, `Treaty`.** `Target` is the rival and `Text` is `Sign` (`FRHCommand::Text` is saved with queued orders since v37).

`CanSignPact(Rival, FString& OutWhy) const` is one rule for the verb, the deck and the cheat. In order:
1. available: HEAD's `No trade contact '%s'`;
2. the levers enabled (section 5.1): `No treaties: disabled`;
3. no live pact with it: `You already have a pact with %s`;
4. **no live block** (1.3): the block's HEAD text. A pact needs a road it can use.
5. the relation at least `PactMinRelation` (50): `%s does not trust you enough for a pact yet (relation %.0f, needs %.0f)`;
6. Influence at least `PactInfluenceCost` (15): `Not enough Influence for a pact (%.0f needed, %.0f on hand)`, worded like Pacify's;
7. no `Betrayal` tariff from it whose window is still open: `%s will not sign while it still charges you for the last broken pact (until sol %d)`.

Every check reads colony-side state at execution, as HEAD's verbs do.

**At execution (live):**
- `Influence -= PactInfluenceCost`;
- `EarthTension = clamp(EarthTension + PactTension, 0, 100)` (5);
- `HumanNatureAxis = clamp(+ PactHumanNatureShift)` (3): diplomacy is the Evolved path;
- append a `Pact` rule (Origin `Signed`, `bLive`, window `[c+2, open)`);
- **append a journal entry** to E5's political record: tension +`PactTension` × 1000. This is E5 risk 2: the rate reads the record, never the live tension (E5 3.2);
- one alert and one log line: `=== PACT SIGNED: %s (influence %.0f, tension %.0f, terms from sol %d) ===`.

**What a pact gives:**
1. **A bigger convoy.** For a trade whose `EffSol` is in the window, the cargo cap each way is `PactCargoKg` (2,000) instead of E4's `ConvoyCargoKg` (1,000). This matters for written orders; the standing trade's lots are fixed.
2. **Earth cannot turn the partner against you (Q-A a, recommended).** `StepPreemptive`'s target filter skips a rival with a live pact. Earth then leans on the next eligible neighbour, or on nobody. Q-A's options (b) and (c) are the alternatives.
3. **No markup from the partner.** A pact does not remove a tariff. Under Q-C (a) the only tariffs come from a betrayed partner, and that partner no longer has a pact by definition.

**Breaking (by the colony): `Treaty` with `Text` `Break`.** It is refused without a live pact (`You have no pact with %s`). At execution (live):
- the pact's `bLive = false`, and its `UntilSol = c + 2`;
- relation −`PactBreakRelationHit` (25), HumanNature −`PactBreakHumanNatureShift` (10): betrayal is the Destructive path;
- `EarthTension −= PactBreakTension` (5): Earth approves. This is journaled like the signing;
- **the betrayal tariff** (Q-C a): a `Tariff` rule from the partner on every resource, both ways, `PactBetrayalTariffBp` (2,500, that is 25%), window `[c+2, c+2+PactBetrayalTariffSols)` (30 sols), Origin `Betrayal`;
- one alert and one log line: `=== PACT BROKEN: %s (relation %.0f, humanNature %.0f); they charge %d%% on every trade from sol %d to sol %d ===`.

**Ended by others:**
- **A Comply** that closes the partner's route (HEAD closes every `SliceActive` row's) ends the pact. `bLive = false`, `UntilSol = c + 2`, with no tariff and no extra cost (the Comply's −40 relation applies). The log line is `=== PACT ENDED: %s (the route is closed) ===`.
  - A pact with a discovered dormant neighbour (Meridian) survives a Comply, because its route stays open (HEAD).
- **A covert op or a sabotage against a partner that is *caught*** ends the pact from the partner's side, with the betrayal tariff. HEAD's caught effects apply as they are, with no further relation or HumanNature change. The log line is `=== PACT BROKEN by %s: they caught you ===`.
  - A clean op is not seen, so the pact stands. That is the covert layer's moral trap: betraying a trusting ally is easy.
- **A defection** cannot happen to a partner under Q-A (a) or (b). Under (c) a defection ends the pact: `bLive = false`, and its window ends at the open sol + 2. From then on every booking with that rival is refused (`Severed`), so the window's end is never read.

### 2.2 The bloc (E5) and how E6 relates to it

**E6 adds no second bloc. The "trade bloc" of the plan is E5's bloc:**
- **Made** when the colony issues the mark, by a `Currency Issue` or a `DefyIssue`. A neighbour joins when the identity record reaches its data `BlocStance`, in name order, from two sols after the roll that admits it (E5 section 4, R7 O2).
- **Broken** only when a Comply withdraws the mark (E5 R5). Every member then leaves at once.
- **Its term** is the currency: an order to a member settles in marks (E5 2.3).

**How it meets the rules:**
- **Membership is not a rule entry.** E5's join rule is exact and fully specified, and its member list is saved in E5's block. Moving it into the rule list would rebuild a proven mechanism for no gain. The trade check reads it where E5 does: the order's currency, decided at booking.
- **A pact and membership are independent.** A member may also be a partner. Joining signs nothing, and a pact never makes a rival join.
- **Hostility still refuses a member at booking.** It stays a member and simply cannot trade (E5 section 4), now through the rule walk.
- **Tariffs are not waived inside the bloc** under Q-C (a). A betrayal is a betrayal. Under Q-C (b) they would be (section 10).
- **The bloc's tally** (E5 3.1, the rate's trade term) stays at **the clearing price, before any markup**. So a tariff never moves the rate.

### 2.3 The political costs, together

| Act | Influence | Earth tension | HumanNature | Relation | Other |
|---|---|---|---|---|---|
| Sign a pact | −15 | +5 (journaled) | +3 | — | needs relation 50 |
| Break a pact | — | −5 (journaled) | −10 | −25 | a 25% betrayal tariff for 30 sols |
| Caught spying on or sabotaging a partner | (HEAD) | — | (HEAD: −6 or −8) | (HEAD: −30) | the pact ends, with the betrayal tariff |
| A Comply that closes a partner's route | — | (HEAD: −45) | — | (HEAD: −40) | the pact ends; no tariff |
| Issue the mark (E5, unchanged) | — | +10 | — | — | multiplier −0.10 while issued |

**The modelled timeline.** The standing trade to Zarya runs every round trip from sol 0. Influence and relation come only from it: +3 Influence and +2 relation a trade, plus Influence's drift of 1.0 × HumanNature / 100 a sol. The fifth trade comes home at sol 23.04 with relation 50 and Influence about 16.0, so **the first pact is possible at about sol 23.5**.

**What the pact's +5 tension does to the timeline:**
- Earth's first demand comes at **sol 91.7 instead of 100.0**.
- If ignored, the embargo comes at 116.7 instead of 125.0.
- While the tension stays higher, the requisition multiplier is 0.015 lower (0.3 × 5 / 100).

---

## 3. Tariffs

### 3.1 What a tariff is

A `Tariff` rule is a markup in basis points that **one neighbour** charges on the colony's trade with it. It can cover one resource or all of them, what the colony buys, what it sells, or both, inside an exact window of clearing sols. **Tariffs on the same line add, and the sum is clamped to `[0, TariffMaxBp]` (9,000).**

### 3.2 The arithmetic (integers; every rounding against the colony)

For a filled line of `kg` kilograms at the clearing price `p` (mcr/kg, or mmk/kg for a mark order), with markup `m` from the walk at the order's `EffSol`:

```
value = kg * p                                         // E4's line value, exact
buy:   cost = value + DivCeil(value * m, 10000)        // what the colony pays
sell:  got  = value - DivCeil(value * m, 10000)        // what the colony receives
tariff     += DivCeil(value * m, 10000)                // the markup on that line, per line
Net         = sum of buy costs - sum of sell receipts  // replaces E4's Net
```

- With `m = 0`, every line is E4's `kg × p`, so **with no tariff the money is E4's, to the milli-unit**.
- `value × m` is at most 2,000 kg × 4 V (V ≤ 900,000 per unit, HabSegment) × 9,000, about 6.5 × 10^16, inside int64.
- **The markup is computed twice and is the same both times:** at booking, for the reservation, from `EffSol = max(ClearSol, O)`, and at the clearing, from the sol being closed.
  - For an order cleared on time, the closing sol is its `ClearSol`. For a late booking it is O, which is `EffSol`.
  - **No window covering a booked order's `EffSol` can change after the booking:**
    - every new tariff starts at its act's c + 2, with c ≥ O;
    - a pact's end sets `UntilSol` = e + 2 > O + 1;
    - retirement removes a rule only once `UntilSol` ≤ the open sol, by which time every order in its window has cleared (E4 R4: a clearing catches `ClearSol` ≤ the closing sol).
  - So the clearing never meets a markup its reservation did not cover. The proof is in section 6.
- **The reservation (E4 3.2 step 5, E5 2.4) gains the markup on each line:**

  ```
  HiM(q, m) = Hi(q) + DivCeil(Hi(q) * m, 10000)
  LoM(q, m) = max(0, Lo(q) - DivCeil(Lo(q) * m, 10000))
  R         = max(0, sum(buy kg x HiM) - sum(sell kg x LoM))       // E5's credit orders then apply HiR / LoR as E5 2.4
  ```

  The clearing price lies in `[Lo(q), Hi(q)]` (E4 R3(b)). A buy line's cost rises with `p`, and a sell line's receipt rises with `p`, so the cost is at most `kg × HiM` and the receipt at least `kg × LoM`. So Net ≤ R. This holds per line and so in sum, and E5's rate margin then bounds the credit conversion as before.
- **Desk exchanges carry no tariff.** The desk swaps money, not goods.

### 3.3 Who collects it

- **The neighbour that charges it.** Rivals keep no purse in E4 or E5: every payment to a rival already leaves the colony's books, and a tariff does the same.
- Two new lifetime totals record it, `TariffPaidMcr` and `TariffPaidMmk`: the markup the colony paid or forwent. They are for the panel only, and no invariant reads them.
- **When step 4 gives rivals books (T5),** the tariff lands there.
- **The colony collects no tariff in E6, and no rule ever pays the colony.**
  - A neighbour has no purse to pay a colony tariff from, and it takes every sell in full (E4 3.4). So a colony-imposed markup would mint money from nothing: a pump.
  - So `MarkupBp` is never negative, a sell markup only lowers what the colony receives, and LV11 enumerates this.
- **Earth collects no tariff either.** Its terms are the requisition multiplier (E3).

### 3.4 Where tariffs come from in E6 (Q-C; the recommendation is written in)

- **Betrayal**, and nothing else. A partner the colony betrays charges 25% on every trade with it, both ways, for 30 sols. Betrayal means the colony breaks the pact, or the partner catches it spying or sabotaging (2.1).
- The harness (`Debug_AddTariff`), for tests.
- No neighbour charges a standing tariff, so with no pact ever signed there is no tariff, and E4's and E5's money is untouched (section 5).

### 3.5 What a tariff never touches

- Prices: E4's update reads the tally in grams, never money.
- The tally itself, the bloc tally and the rate (2.2).
- Earth's prices, the quota, Hope, population and growth bills.

**Modelled (Q-C (a)).** The colony breaks its pact with Zarya at sol 60.5 and keeps the standing trade running:
- each run pays **564.7 cr instead of 827.9 cr** while the tariff lasts (sols 62-91);
- **2,106 cr** goes in tariff over the 30 sols;
- every price is unchanged.

---

## 4. Covert operations and sabotage move prices

The principle: **E6 never writes a price.** It changes three of E4's inputs (a rival's production, a rival's store, and the money on a line), and E4's own bounded update does the rest. Every market-facing effect is keyed by the act's `ClearSol`, never by the step that ran it.

### 4.1 Sabotage: a neighbour's production stops

- **The window.** A sabotage with `ClearSol` c gives its new `Disrupted` rule the market window `[c+1, c+1+D)`. `D = SabotageMarketSols` (new config row, 6, clamped to 1..30) is pinned by LV0 to `round(SabotageDurationSols)`.
- **The cut.** At the roll closing a sol s inside any of a rival's `Disrupted` windows, that rival's effective production is `ProduceG × (100 − SabotageCutPct) / 100` for every slot. `SabotageCutPct` is a new config row: 100 under Q-B (a), clamped to 0..100.
  - It is used in E4's step 2 (the rival's store step) and in step 3's `Prod`, the sum the flow term reads.
  - Consumption goes on: people still eat.
- **Nothing else changes:**
  - The live refusal (HEAD's `LiveSols`) and its text are HEAD's.
  - So is the roll: the cut lands whether or not the colony was seen, as HEAD's blackout does.
- **Why the window starts at c + 1.** An act with `ClearSol` c always runs before the roll closing c, in both bands (E4 section 5: a command due by a boundary runs at or before that boundary's step, and `StepUplink` comes before `StepCityGrowth` in both spines, W:672/683 and :844/855). So the rule exists at every roll its window covers, in both bands. The sol in which the act happened is left out, because part of it ran before the act.
- **A barter rival** (no economy) has no production; the window does nothing.
- **A market switched off** (`IsMarketLive()` false) runs no production (E4 R4), so a cut has nothing to cut.

### 4.2 Theft: the loot comes out of the victim's store

- **At the execution of a clean `Covert`** against a rival with an economy, the colony side is HEAD's: the loot lands, E1 records it as `In`, and the log line is unchanged. E6 also appends a **pending market effect**: `{ ClearSol, CommandId, sub-index, Theft, rival, slot, grams }`, one per looted resource.
- **At the roll closing a sol s,** every pending effect with `ClearSol ≤ s` is applied in `(ClearSol, CommandId, sub-index)` order. This happens **after the convoy clearing** (the colony's paid orders are served first) and before production: `StoreG -= min(grams, StoreG)`.
  - If the store holds less than the loot, the rest was never there. That is HEAD's loot-from-nowhere, now bounded by the store.
  - The theft is not a trade: it enters no tally.
- **A caught op** takes nothing, so it records nothing.
- **This needs the executing command.** `ResolveCovert`, `ResolveSabotage` and `ResolveLaunder` each gain the command (its `CommandId` and `ExecuteAt`), as E5's journal needs for `Solidarity`. Their outcomes and texts do not change.

### 4.3 Launder: the peace offering arrives

At the execution of a `Launder` against a rival with an economy, the colony side is HEAD's. E6 appends a pending `Offering` effect (the goods spent, per slot), applied the same way: `StoreG += grams`. The goods now reach the rival, as a sell does. **The masses balance:** what leaves the colony as E1's `Out` lands in a store.

### 4.4 The covert roll is unchanged

- E6 adds **no** call to `RollCovertDetected`, **no** change to `CovertAttempts`, and no new input to the seed or to P.
- Pacts, tariffs and windows read the roll's *outcome* after it is made. They never cause or skip a roll.
- So `-covert`'s CXCX (C:1351-1369) and `-espionage`'s `caught=0`, then `caught=1`, are untouched. LV2 asserts them (section 8).
- **The outcome is not band-exact at HEAD's margins** (section 0). E6 inherits that as an input. Given the same outcome, the market is identical (section 6).

### 4.5 Modelled numbers

The model is E4 R5's update with Zarya's placeholder data, in integers. Its assumptions:
- the standing trade every round trip (4 sols, Storm_1 freezing sols 12-15);
- a sabotage at sol 60.5;
- HEAD's six-sol trade refusal in every option.

The model reproduces E4's own figures: 17,540.35 cr by sol 99, and 827.9 cr a run from about sol 60.

| Option | Zarya's Ice (cr/kg) | Zarya's Struct (cr/kg) | Back within 1% of the undisturbed path | The standing trade, sols 60-199 |
|---|---|---|---|---|
| no sabotage | 0.750 (its floor) | 9.351 at sol 60 | — | 28,976 cr |
| **(a) production stops for 6 sols** | **1.002 at sol 66 (+34%)** | **10.331 at sol 66 (+10.5%)** | Ice by sol 78, Struct by sol 80 | 28,257 cr (−719) |
| (b) half production for 6 sols | 0.778 (+3.7%) | 9.958 (+6.5%) | sol 68 and 77 | 28,260 cr (−716) |
| (c) no market effect (HEAD) | 0.750 | 9.591 (the colony's missing sale only) | sol 67 and 73 | 28,196 cr (−780) |

- **Ice rises for one reason:** at the floor its flow term turns from +4,545 to −10,000, so the 5% clamp binds for all six sols. Its store barely moves: 18,950 kg against a 20,000 cap.
- **The colony's own run is lost to the refusal in every option.** The higher Struct price on the following runs pays a little of it back: the first run after recovery is 885.5 cr under (a), 849.1 under (c).
- **A buyer pays for it.** 1,000 kg of Ice bought right after the sabotage costs 1,002 cr under (a) against 750.
- **Theft is small.** A clean theft of 75 kg at sol 60 moves no price at all: Zarya's Ice store is full, and its own production refills it before the reprice. 300 kg stolen at sol 30, when the store is not full, lifts Ice by 2-10 mcr/kg (at most about 1%) for 5 sols.

---

## 5. Inertness

### 5.1 The switch

- **`bLeversEnabled`** is a runtime harness flag, default true and never saved (`Debug_SetLeversEnabled`). When it is false:
  - the `Treaty` verb is refused (`No treaties: disabled`);
  - no pending theft or offering is recorded;
  - a `Disrupted` rule gets no market window;
  - tariffs are read as 0.
- **The migration is not switchable:** it *is* today's behaviour.

### 5.2 The bars

**In every suite the only rules ever made are the four migrated kinds.**
- No suite sends `Treaty`, so no `Pact` exists, so no `Tariff` exists.
- The brief's condition, "no rule beyond today's embargo state", therefore holds in the whole battery.

**What does not move:**
- **Every refusal, timer, defection step and log line of the migrated behaviour** (section 1.4). In particular:
  - **`-covert`:** CXCX and axis −24. The roll's inputs are untouched (4.4). The clean thefts record pending effects that silently take 75 kg twice from Zarya's Ice store when E4 is built, and nothing prints a store. The v20 line is unchanged.
  - **`-espionage`:**
    - the sabotage line, the disrupted refusal, `remaining=5.8`, the recovery step and the sol-7 convoy;
    - the second sabotage;
    - `sabotage 5.9->5.9` after the load (the rule block carries `LiveSols`, and so does the mirror).

    With E4 built, Zarya's prices move during the two windows. No line prints a price or a credit. E4's own check found `cr carried` and `Earth credit` 0 times in the convoy suites' logs.
  - **`-preemptive`:**
    - the embargo at tension 78 with grace 8, the refusal and `grace=7.8`;
    - the Pacify;
    - the re-embargo at tension 100 while a convoy is out;
    - `(soured - goods only)`, through the goodwill check on rules;
    - the defection on HEAD's step with relation 5;
    - the v22 round trip.
  - **`-solidarity`:** the Comply's `1 route(s) closed`. The refusal reads "route closed" though an embargo is also live, by precedence. The v19 round trip.
  - **`-trade`, `-earth`, `-ending`, `-crisis`:** they read none of the four sets beyond the getters, which are unchanged.
- **E4's `-market` and E5's `-currency`:** no tariff exists, so every markup is 0 and every Net and R equal E4's and E5's (3.2).
- **The 110-sol playtest is byte-identical** to `Saved/baselines/ind_full_playtest.txt`:
  - the bot sends no political order and no convoy (section 0);
  - tension stays under 75, so no rule is ever made;
  - E6 logs nothing on a roll, and the `.txt` holds only `[Playtest]` lines.

  The playtest bot changes often (two commits today), so the builder re-checks this at the build's HEAD.
- **`-scale`:** every digest is identical.

**What moves, and why the brief's literal bar cannot hold:**
- the save-version lines;
- the masked `(N bytes` lines;
- **the lines that print whole-file sizes:** CV8 (`8951 -> 9031 bytes`), DP11 (`8979 -> 9011 bytes`) and EC1's two `both saves are ... bytes` lines.
  - They move for any new block, as they did in E1-E3, and E4 R10 found the same.
  - The mid-file bytes do not move, because the old slots stay as a mirror (7.2). Only the tail grows.
- **E4's and E5's lines that call their block "last" or print a size or offset** (E5 R7 O19). The builder lists them by name when E4 and E5 exist;
- the new `levers.log`.

---

## 6. Determinism and era parity

**Order:**
- Rules by Id.
- Rivals by name string with a case-sensitive ordinal compare, wherever E6 itself orders them (the Comply's new `Closed` rules, saved names). HEAD's own `LexicalLess` tie-breaks in the embargo pick and `GetEmbargoingRival` are kept as they are, being HEAD's behaviour.
- Pending effects by `(ClearSol, CommandId, sub-index)`.
- **Nothing is ordered by FName index**, which was the 2026-07 covert-seed bug.

**Integers:**
- Every market term is an integer: windows, `MarkupBp`, grams, mcr and mmk.
- The only doubles in the list are the live timers (`LiveSols`), which are HEAD's and gate booking only.
- **The market never reads `LiveSols`, `bLive` or an Id.**

**Why the prices are identical in both bands, given the same accepted acts and orders and the same act outcomes.** Each market input E6 adds is band-exact:
1. **The sabotage cut.** Its window comes from the act's `ClearSol`, and the rule exists before every roll the window covers, in both bands (4.1).
2. **Thefts and offerings.** They are keyed by `ClearSol`, recorded before the roll closing `ClearSol` in both bands, and applied at that roll. In the era band an act due 10 s after a boundary runs before that boundary's roll, but its `ClearSol` is the later sol, so the roll it runs before does not apply it (4.2).
3. **Markups and the pact's cargo cap.** These are read at booking and at clearing from windows that start at `c + 2`.
   - **Why two sols.** On a 60-s era step, `StepUplink` runs every due command in **queue order**, and the queue can run out of `ExecuteAt` order. `OrderLagSeconds` falls with each speed tier (45, 20, 8 s), so a command issued later can come due earlier (E4 R3(b)).
     - A booking B can therefore run before an act A due earlier on the same step in the era band, and after it in the agent band.
     - Both are due within one step of under a sol, so B's `ClearSol` ≤ A's c + 1.
     - A window from c + 2 therefore never covers a booking that might run before its act, and every booking it covers runs after the act in both bands.
   - The pact's end (`UntilSol` = e + 2) is safe by the same bound.
   - **This is E5's two-sol rule** (E5 section 6), applied at the act's execution, where E5 applies it at a roll.
4. **Booking never reads a store** (E4 3.2), and nothing E6 adds to booking limits a fill.

**What may differ between the bands** is colony-side, as in E4 and E5:
- every live gate. When an embargo fires (live tension), when grace and downtime run out, and so whether a booking is refused at the margin. HEAD differs here already;
- the outcome of a covert op or sabotage at HEAD's margins (section 0), and so whether a theft, an ended pact or a betrayal tariff exists at all;
- acceptance at the margin of Influence, relation, holdings or credit.

### LV8, the paired run

**Start.** As E5's CU8: the suite builds its fixture colony with no convoy out and sets the clock to sol 16.0. After one step, so that E5 O1's catch-up has run, it saves `lv8_start`, and both twins load it.
- With `WeatherCycleSols` 0, sols 16-36 hold no dust storm. The suite asserts that no DustStorm is active at any threshold in the window.
- The era twin first advances 30 s, so every era step ends 30 s past a minute.

**Rivals.**
- Zarya.
- An injected **`Kestrel`**:
  - E4 R7's economy, 30 km away (a 1-sol round trip);
  - `RelationStart` 60, so a pact can be signed at once;
  - `BlocStance` −1.
- At load, both twins get `Debug_AddInfluence(40)` and `Debug_GrantCredits(20,000 cr)`.
- **The name `Kestrel` is checked in the suite.** The content-hash roll at the fixture's `CovertAttempts` for its covert op must give U ≥ 0.5. That is attempt 1: Zarya's sabotage takes attempt 0. P is at most 0.5 at any relation and time of day, so the op is clean in both twins.
  - If the hash gives less, the builder picks another fixture name and records it.
  - The sabotage's own outcome does not matter to the market: the cut lands either way. Its time is mid-night (sol fraction 0.99), and no convoy to Zarya is due near it, so its relation effect is the same in both twins.
- **E5's fixture condition holds:** no E2 award, E3 launch or sale lands in sols 16-36, and the suite asserts it (E5 re-review condition 3(a)).
- **E4 R7's schedule rules hold, and the suite asserts them:** every convoy is home at least a sol before the next order to the same rival, and every reservation is at most half the spendable credit.

**The schedule.** Every act and order is placed with a new harness hook, `Debug_EnqueueAt(Cmd, ExecuteAt)`, which appends to the queue with an explicit execute time. That is how the queue-order inversion is built.

| Sim time | Command | Why |
|---|---|---|
| 17.5 | `Treaty Kestrel Sign` | `ClearSol` 17, so terms from sol 19. |
| queued at 18.2, due 19.0 + 10 s | `MarketOrder Kestrel Ice:1500` | **The inversion fixture (pact).** Queued *before* the next act and due after it. |
| queued at 18.3, due 19.0 − 20 s | `Treaty Kestrel Break` | Due 30 s earlier, queued later. The era twin's step ending at 19.0 + 30 s runs both, in queue order: the order first, then the break. The agent twin runs the break first. |
| due 21.0 − 10 s | `Sabotage Zarya` | **The boundary fixture (cut).** `ClearSol` 20 in both twins. The era twin runs it on the step ending 21.0 + 30 s, whose clock is in sol 21. Zarya's production stops at the rolls closing 21-26 in both. |
| 23.2 | `Debug_SetRivalStore(Kestrel, Ice, 150)` in both twins | Sizes the next fixture. |
| 23.5 | `MarketOrder Kestrel Ice:100` | `ClearSol` 23: it fills 100 kg at the roll closing 23 in both twins. |
| due 24.0 + 10 s | `Covert Kestrel` | **The boundary fixture (theft).** `ClearSol` 24. The era twin runs it on the boundary step, before the roll closing 23. The agent twin runs it after. In both, the 75 kg leaves Kestrel's store at the roll closing 24, after the sol-23 order has filled. |
| 27.5 and 32.5 | `MarketOrder Zarya Struct:-200` | Written orders after the live refusal (it ends by 27.0 + 30 s in both twins). The second goes after the first is home, at about 31.5. |
| 27.5 | `MarketOrder Kestrel Ice:100` | Under the betrayal tariff (window 20-49). |

**The order at 19.0 + 10 s.**
- Its `EffSol` is 19, inside the pact's window `[19, open)` as it stands when the order books in the era twin.
- In the agent twin the break has already set `UntilSol` = 18 + 2 = 20, so the window `[19, 20)` still covers it.
- So the 1,500 kg order is **accepted in both twins**, at the pact's 2,000 kg cap.
- The betrayal tariff's window `[20, 50)` does not cover sol 19, so its markup is 0 in both twins.

**The bar.** At every threshold from 16 to 36, the following are identical (int64 equality):
- every traded price and rival store, every fill and charge, every line's markup, every window;
- the pending effects, and `TariffPaidMcr`.

The sols in which convoys come home, and the end balances, are equal too. **Rule Ids are not compared:** live events may number differently, so the comparison uses each rule's kind, rival, window and origin, as a set.

**Mutations it bites** (section 8): 4, 5, 6 and 7.

---

## 7. Save

### 7.1 Version

**The next free version.** It comes after E4, E5 and the economy-fixes gate. If those take v54, v55 and v56 in that order, it is v57. The number is assigned at build time, and the strict check stays: only the current version loads.

### 7.2 The mirror: HEAD's slots stay, written from the rules

The v19, v21 and v22 slots keep their exact layout and are written as **projections of the rule list**:
- **v19 `Closed`:** the names of the `Closed` rules, sorted with `LexicalLess`, as HEAD.
- **v21 `SabotageSols`:** for each rival with a live disruption, its largest `LiveSols`, in the order of each rival's first live `Disrupted` rule by Id.
- **v22 `EmbargoGrace`:** each `Embargo` rule's `LiveSols`, in Id order.
- **v22 `Defected`:** the names of the `Severed` rules, sorted with `LexicalLess`.

They are byte-identical to HEAD's whenever each map holds at most one entry, which is every suite (one embargo at a time by HEAD's rule; one sabotaged rival in `-espionage`). With more entries they are the same size, possibly in another order.

**Why keep them:**
1. **Every byte before the tail is unchanged**, so every offset test and every mid-file reader is untouched.
2. **A lost tail never reopens a closed route, ends a disruption or forgives a defection.** The mirror is the fallback (7.4).

On load the slots are read into temporaries. The names must be rival rows and the timers finite and in `(0, config]`; anything else is dropped with a Warning. The live rules are built only after the E6 block.

### 7.3 The E6 block: framed, last, findable from the end

It is written **straight after E5's currency block**, as the last thing in the file. It is read after teardown like E1-E5's, and it repairs with a Warning and is never refused. **Its frame is the code review's robust fix**: the review found that a misread count in one block shifts every block after it (`economy-code-review.md`, finding 2).

```
Header   uint32 Tag = 'RHL6'      uint32 PayloadBytes      uint32 PayloadCrc32 (FCrc::MemCrc32)
Payload  (its own FMemoryWriter + FNameAsStringProxyArchive, so a parse can never run past it)
  uint8  FormatRev = 1
  int32  NextRuleId
  int32  RuleCount, then each rule by Id ascending:
         int32 Id, uint8 Kind, uint8 Origin, FString Party, FString Res ("" = every), uint8 Dir,
         int32 MarkupBp, int32 FromSol, int32 UntilSol, double LiveSols, uint8 bLive
  int32  EffectCount, then each pending effect in (ClearSol, CommandId, sub-index) order:
         int32 ClearSol, int64 CommandId, int32 Sub, uint8 Kind (1 theft, 2 offering), FString Party, FString Res, int64 Grams
  int64  TariffPaidMcr, int64 TariffPaidMmk
Footer   uint32 PayloadBytes      uint32 Tag
```

**Reading** (`ReadLeversBlock(Ar, const TArray<uint8>& Bytes)`):
1. **Forward.** If no earlier tail block reported itself lost, and the 4 bytes at the reader's position are the tag, the header is read there.
2. **Otherwise, from the end.** The last 8 bytes of the file must be `(PayloadBytes, Tag)`. The header is then at `Size − 8 − PayloadBytes − 12`, and it must repeat the tag and the length.
   - So a misaligned or lost E1-E5 block cannot take the rules with it.
   - A gate appending a framed block after E6 steps back over that block's footer first. Every framed block from E6 on ends with a footer.
3. `PayloadBytes` must be at most what is left, and the CRC must match. The payload is then parsed by its own reader, bounded by `PayloadBytes`.
4. Afterwards the outer reader is positioned after the footer, whichever path was taken.

If the economy-fixes gate lands a shared frame helper first, E6 uses it with tag `'RHL6'`, and adds the CRC and the footer if the helper has none.

**Validation and repairs** (each with a Warning; no Error is ever logged). These are the code review's lessons, taken one by one:
- **Structure → the block is lost (7.4):**
  - Ids not strictly ascending, an Id ≤ 0, or an Id ≥ `NextRuleId`;
  - a count larger than the bytes could hold;
  - a `FormatRev` other than 1.
- **A kind, origin or direction out of range,** a `Party` that is not a rival row, or a `Res` that is neither a slot nor `""`: that rule is dropped.
  - `Party` is canonicalised to the row's own FName, so a case-only variant ("zarya") resolves to the row and is written back in the row's spelling. Duplicates are found by FName equality, which ignores case.
- **`MarkupBp`** below 0 becomes 0; above `TariffMaxBp`, it is clamped. **No loaded rule can pay the colony.**
- **Windows.** `FromSol` and `UntilSol` must each be −1 or at least 0. A window with `UntilSol ≤ FromSol` drops its rule. Neither may be later than the open sol + 2 + the longest window (`max(PactBetrayalTariffSols, SabotageMarketSols + 1)`); later values are clamped. The open sol is E4's `MarketCheckedSol`, read before this block; if E4's block was lost, `ProductionCheckedSol`.
- **`LiveSols`** is checked as follows:
  - **an `Embargo`'s:** if not finite, or not above 0, it is reset to `EmbargoGraceSols`. A doctored file never makes an instant defection;
  - **a `Disrupted` rule's:** if not finite or negative, its live part ends (`bLive = false`);
  - **either kind:** above its config value, it is clamped.
- **Exclusive kinds** (1.2):
  - a second `Closed`, `Severed` or `Embargo` for one rival is dropped, keeping the lowest Id;
  - an `Embargo` beside a `Severed` is dropped;
  - a second *live* `Pact` is ended;
  - an `Embargo` with `bLive` false is dropped.
- **Pending effects.**
  - Grams ≤ 0 or above 10^9, a bad kind, or a rival without an economy: dropped.
  - Unsorted effects are sorted.
- **The totals.** A negative `TariffPaid` total is set to 0.
- **The mirror check.** After a good read, the rules' projection (7.2) is compared with the mirror just read. If they differ, the Warning lists the difference, and the rules win: the block is CRC-checked, and the mid-file slots are not.

### 7.4 A lost block

If the frame cannot be found, the length is wrong, the CRC fails or the structure is bad, the rules are **rebuilt from the mirror** with a Warning that names what was lost:
- one `Closed` rule per v19 name, sorted by name string;
- then `Severed` per v22 `Defected` name;
- then `Embargo` per v22 `EmbargoGrace` entry, with its grace;
- then `Disrupted` per v21 entry, with its downtime and **no market window**;
- `NextRuleId` = count + 1.

**What is lost:** pacts, tariffs and pending effects. The Warning says the colony's pacts and any betrayal tariff are gone. A route the colony closed stays closed, and a defection stays a defection.

### 7.5 Offsets and checkpoints

- **The block is sized by history** (its rules and pending effects). So every test that measures from the end of the file subtracts `GetLeversBlockBytes()` from the live state, never a constant. That covers E1's EC1 and EC6, E2's CR9, E3's XE10, and E4's and E5's block checks, with their printed texts unchanged.
- **The bump refuses the Midgame and Late checkpoints.** Re-cut them (`rh_checkpoints.sh all --force`) and load both.

---

## 8. Tests: `-levers`, a new fails-loudly suite

It goes in its own file, `Source/RedHopeSim/Private/RHSimLeversSuite.cpp`, and is added to `rh_battery.sh`. No path logs an Error on purpose.

**New harness hooks** (runtime only, never saved):
- `Debug_SetLeversEnabled(bool)`;
- `Debug_AddTariff(Rival, Res, Dir, Bp, FromSol, UntilSol)`;
- `Debug_EnqueueAt(Cmd, ExecuteAtSimSeconds)`;
- `Debug_SetRelation(Rival, Value)`;
- const reads `GetTradeRules()`, `GetPendingMarketEffects()`, `GetLineMarkupBp(Rival, Res, Dir, Sol)`, `HasLivePact(Rival)`, `GetRivalCutPct(Rival, Sol)` and `GetLeversBlockBytes()`.

E4's hooks (`Debug_RollMarket`, `Debug_SetRivalStore`, `Debug_SetMarketPrice`), E5's (`Debug_SyncPoliticalView`), `Debug_InjectRival`, `Debug_AddTension` and `Debug_AddInfluence` are reused.

| Check | What it pins |
|---|---|
| LV0 | **Data.** Every new config row and its clamp, with the CSV equal to the live DT. `SabotageMarketSols == round(SabotageDurationSols)`. `TariffMaxBp` < 10,000. |
| LV1 | **Inertness.** Twins with `Debug_SetLeversEnabled` on and off. Each runs, in each band, the scripts of `-covert`, `-espionage`, `-preemptive` and `-solidarity`, and 10 sols of standing trade. The digest is bit-identical between the twins: relations, hidden tensions, Influence, HumanNature, identity, tension, every refusal text, every timer, `CovertAttempts`, the colony's stocks, Hope and the quota phase. Three things are left out, because the "on" twin's thefts and cuts move them on purpose: rival stores, prices and credit balances. The saves are byte-identical up to E1's ledger block, including every mid-file byte and the v19 / v21 / v22 mirror. A third twin with no sabotage and no theft holds credits and prices identical too. |
| LV2 | **The migrated sequences, asserted.** Each existing suite's script is replayed in a fresh colony with hard checks, which the log-only suites never had: `-covert`'s CXCX and axis −24; `-espionage`'s refusal text, `remaining` within 0.05 of 5.8, recovery on HEAD's step, and `caught=0` then `caught=1`; `-preemptive`'s target, grace, Pacify deltas, re-embargo with the convoy out, soured return and defection step; `-solidarity`'s closed count 1, relation 5 and route-closed refusal while embargoed. Each has its save round trip. |
| LV3 | **Precedence and order.** One rival gets all four blocks, made in reverse precedence (embargo first); the refusal is the closed text, then each next text as the rules are removed. Two rivals injected in the reverse of their name order, each made `Closed` by one Comply, give `Closed` rules in name order and the same saved bytes as the forward order. The walk's verdict is unchanged by any permutation of a 12-rule list (seeded LCG). |
| LV4 | **Tariff arithmetic, by hand.** Buy and sell lines at 0, 1, 2,500 and 9,000 bp, including values whose markup is not whole (rounding against the colony). Two tariffs on one line sum, and the sum clamps. Per resource and per direction. Window edges: `FromSol` is in, `UntilSol` is out. The reservation covers the charge when the price moves a full step between booking and clearing, in credits and in marks (E5's rate margin). `TariffPaidMcr` and `TariffPaidMmk`. Prices, the tally, the bloc tally and the rate are bit-identical to an untaxed twin. |
| LV5 | **Pacts.** Every refusal of `CanSignPact` with its text. Signing: Influence, tension, HumanNature, one journal entry of +5,000, and the log line. The cargo cap: a 1,500 kg order with `EffSol` c+1 is refused, and with c+2 it is accepted. Breaking: relation, HumanNature, tension, journal, and the betrayal tariff's window `[c+2, c+32)`. A caught covert op against a partner ends the pact with the tariff, and a clean one does not. A Comply ends it with no tariff. Re-signing is refused while the betrayal tariff runs. **Q-A (a):** with tension forced to 80, Earth embargoes the next neighbour (Meridian, discovered), never the partner; with no other neighbour, nobody. |
| LV6 | **Sabotage moves prices.** On E4's MX2 fixture, a sabotage with `ClearSol` c gives a window `[c+1, c+7)`. At each covered roll, Zarya's stores and its traded prices equal hand-computed integers written in the suite, and the step after the window is back on the undisturbed path. The live refusal and `LiveSols` are HEAD's. A second sabotage at c + 3 makes a second rule, with `GetSabotageRemaining` = 6. A sabotage on a barter rival has no window effect. |
| LV7 | **Thefts and offerings.** A clean theft's pending effect is applied at the roll closing its `ClearSol`, after the convoy clearing, with `min(store)`. A caught op records nothing. A Launder's offering lands in the store. A barter rival records nothing. E1's legs are as HEAD. |
| LV8 | **Era parity.** Section 6's paired run, held to identical. |
| LV9 | **Save.** A colony with every kind, two pending effects and a moved market: save, load, save is byte-identical. The mirror slots equal HEAD's layout. Every repair of 7.3 on a doctored file, with the colony loaded. A flipped payload byte (CRC) and a wrong length rebuild from the mirror, and no closed route reopens. A doctored earlier tail block (E3's export count off by one) still finds the E6 block by its footer. A case-only `Party` loads as the row. The previous version is refused. |
| LV10 | **Ruling 11 and Q4a.** Tariffs at 9,000 bp, a pact and a sabotage, against a twin with none, 10 sols: Hope, the population, growth and every colony stock are bit-identical. |
| LV11 | **No pump.** 10,000 seeded lines over prices, markups and fills: each buy costs at least its value and each sell receives at most its value. A save with a negative `MarkupBp` loads as 0. |

**Mutations** (LV12). Each is built, run and restored by hash, and must fail the check named:
1. the refusal taken as the first block by Id instead of by kind: LV2 (`-solidarity`'s refusal reads "embargoed") and LV3;
2. a timer decremented after its test instead of before: LV2 (`remaining`, the defection step);
3. the Comply's `Closed` rules appended in row-map order: LV3 (the saved bytes differ with reverse-injected rivals);
4. a pact's or a tariff's window from c + 1: LV8. The inversion order then sees the pact in one twin only, under a break at c + 1;
5. a pact's end at e + 1: LV8 (the 19.0 + 10 s order is capped in one twin);
6. the sabotage window keyed by the executing step's sol: LV8 (the era twin's sabotage, run on the step ending 21.0 + 30 s, gets `[22, 28)`, against the agent twin's `[21, 27)`);
7. a theft applied to the store at execution instead of at the roll: LV8 (the era twin runs the 24.0 + 10 s theft before the roll closing 23, so its sol-23 order fills 75 kg, against the agent twin's 100);
8. a markup rounded toward the colony: LV4;
9. the reservation without the markup: LV4 (the charge outruns the reservation);
10. the markup counted into the tally or the bloc tally: LV4 (a price or the rate moves);
11. a sell markup that adds to what the colony receives: LV11;
12. the mirror not written: LV9 (a lost block reopens a closed route);
13. no CRC check: LV9 (a flipped byte loads silently);
14. forward reading only: LV9 (a misaligned earlier block loses the rules);
15. an extra `RollCovertDetected` call (for example, a pact check rolling for detection): LV2 (CXCX changes);
16. a repeated sabotage updating its rule in place: LV9's save-load-save with a retirement between, and LV8's rule set.

**`-scale`** (outside the battery): 1,000 `RollMarket` calls with 8 injected rivals, 60 rules and 20 pending effects. The budget is 0.2 ms more than E5's measured figure. Every digest is identical.

**What the battery moves:** section 5.2's list, and nothing else.

---

## 9. Presentation (RedHope; E8 is the full panel)

- **Each rival card (K:2135-2202):**
  - its pact: "Pact since sol 31" or "Sign a pact (15 Influence)", greyed by `CanSignPact` with the reason, and BREAK with its cost in the tooltip;
  - any tariff it charges: "25% until sol 91";
  - a disruption: "production stopped until sol 66" (the window) beside HEAD's "disrupted" (the live timer).
- **The status line** (K:2159-2162) keeps HEAD's order: sided with Earth, route closed, embargo, disrupted. It reads the getters, which are unchanged.
- **Console:** `RH.Treaty <Rival> sign|break`, and `RH.Levers` (the rule list, the pending effects, the tariff totals).
- **Wording.** New strings go into the wording table as placeholders. None is morale, sickness or evacuation wording, so Gate D's review is not engaged.

---

## 10. Risks

1. **Two clocks.** Every rule has a live part (what may be booked now, on HEAD's timing) and an exact market window (what the market does). The deck must show both, as section 9 does. A disruption's live timer and its market window end up to a sol apart.
2. **Covert outcomes are not band-exact at HEAD's margins** (section 0). E6 makes them matter more, because they decide a theft's store debit, a pact's end and a betrayal tariff. E6 does not change the roll. The outcome would be exact if the roll read the clock at `ExecuteAt` rather than at execution, but that changes `-covert`'s inputs (its ops run at fixed clock settings), so it is left for a gate that may move CXCX.
3. **Q-A (a) removes Earth's embargo stick for a colony with one neighbour.** Earth keeps its demands, the requisition multiplier and the crises. With more neighbours (step 4), it turns the next one.
4. **Rivals have no purse.** Tariffs, like every payment to a rival, leave the economy. The colony cannot levy a tariff until T5 gives rivals books (3.3).
5. **A pact doubles a heavy trader's volume.** A colony that sells 1,000 kg of Struct to Zarya every round trip earns 84,093 cr by sol 200 at the Struct floor (0.25 V = 1.25 cr/kg). With 2,000 kg from sol 25 it earns **141,386 cr**. E4's floor bounds the price, not the volume, so the director should see this beside Q-A. It needs 500 kg of Struct a sol to spare; the playtest colony is Struct-starved.
6. **Sabotage is double-edged.** Under Q-B (a), Zarya's Ice is 34% dearer for about 12 sols, for the colony too. The colony's own trade with the target stops for 6 sols, so in the model a sabotage never pays the colony that trades with the target.
7. **E4 and E5 are not built.** Every name in 3.2, 4.1-4.3 and 6 is a design name. The builder rebases and re-checks the reservation bound (3.2) against E4's and E5's final reservation code. E5's journal gains two entry kinds (pact sign and break).
8. **The mirror is a second truth.** The rules win, and a mismatch logs a Warning. The mirror is written from the rules every save, so the two can differ only in a doctored or damaged file.
9. **A history-sized block.** Its size depends on the rules and the pending effects, so offset tests must read `GetLeversBlockBytes()` at the moment of the save.
10. **The save-version chain.** E4, E5, the economy-fixes gate and E6 each take the next free version, in whatever order they land.

---

## Director questions

Every number below comes from the model in 4.5 and its siblings, which reproduce E4's own figures to the cent.
- **The market:** E4 R5's update, with Zarya's placeholder data, in integers.
- **The trade:** the standing trade to Zarya every round trip.
- **Tension:** from 0 at 0.6 a sol.
- **The pact:** signed at sol 23.5, the first moment it is possible.

**Q-A. What does a pact do for the colony when Earth leans on its partner?**

Every option has the same base: costs 15 Influence and +5 Earth tension; needs relation 50; doubles the partner's convoy, which by sol 200 earns a heavy Struct seller 141,386 cr against 84,093.

In the table the colony never answers Earth's demands, as the playtest bot plays, and Meridian has not been found.

| Option | What happens | Zarya's trade stops | Zarya defects | The standing trade by sol 200 |
|---|---|---|---|---|
| no pact (for comparison) | today's embargo | sol 125.0 | sol 133.0, for good | 22,508 cr |
| **(a) Earth cannot embargo a partner (recommended)** | Earth leans on the next neighbour instead, or on nobody | never | never | **38,238 cr** |
| (b) the partner holds out | the embargo still lands, but the partner never defects; trade resumes once tension falls under 75 (a Pacify, a Defy) | sol 116.7 | never | 20,852 cr |
| (c) no political term | only the bigger convoy | sol 116.7 | sol 124.7 | 20,852 cr |

- (b) and (c) stop trade 8.3 sols *sooner* than no pact, because the pact's own +5 tension brings the embargo forward.
- Under (a), if Meridian has been found, Earth embargoes Meridian at sol 116.7, and it defects at 124.7.
- A colony that Defies every demand never reaches tension 75, so it never meets an embargo. There the options do not differ. The pact's +5 only brings the demands forward: the Martian ending path (identity +60) arrives at sol 132.2 instead of 140.5.

**Q-B. How hard does a sabotage hit the target's market?**

The model sabotages Zarya at sol 60.5. Today's six-sol trade refusal applies in every option.
- **(a) (recommended): its production stops for the six whole sols after the act.**
  - Zarya's Ice rises 34% (0.75 to 1.00 cr/kg) and its Struct 10.5% (9.35 to 10.33), and both are back on their paths by sols 78-80.
  - 1,000 kg of Ice bought just after costs 1,002 cr instead of 750.
  - The colony's own standing trade loses one run: 719 cr over sols 60-199.
- **(b): half its production.** Ice +3.7%, Struct +6.5%; back by sols 68 and 77; −716 cr.
- **(c): no market effect, as today.** Only the refusal: Ice is flat, Struct +2.6% from the colony's own missing sale; −780 cr.

In every option a clean theft now comes out of the victim's store, but it moves almost nothing: no price at all when the store is full, at most about 1% when it is not.

**Q-C. Who may charge the colony a tariff?**
- **(a) (recommended): only a partner the colony betrays,** by breaking the pact or being caught spying on or sabotaging it.
  - The charge is 25% on every trade with it, both ways, for 30 sols.
  - The standing trade then pays 564.7 cr a run instead of 827.9: **2,106 cr** lost over the 30 sols. Prices do not move.
  - With no betrayal there is no tariff, so E4's and E5's money is untouched.
- **(b): every neighbour charges a standing 10%, waived by a pact or by bloc membership.**
  - Pacts and the mark become worth more: the standing trade pays 5,224 cr in tariff by sol 200 with no pact, and 603 cr with a pact from sol 25.
  - The first run *charges* the colony 45 cr instead of paying it 50, and a heavy Struct seller loses 8,409 cr.
  - It is not inert: E4's money checks (`-market` MX7's "+50 cr at the return") and every credit total move.
- **(c): no tariffs in E6.** The rule list keeps the kind for step 4, and nothing makes one. Breaking a pact then costs only relation, HumanNature and the pact.

---

# PROFESSOR REVIEW

An independent adversarial review against HEAD `72e338a` (2026-10-24), read-only. The reviewer read the design, E4 and E5 in full (draft, review, REVISION, re-review and director answer), the economy code review, the plan, the rulings, and the battery and playtest baselines. It wrote its own integer model of E4 R5 and a copy of the covert hash, and did not reuse the design's model. It is recorded as returned, lightly formatted.

Verdict: **CLEARED WITH CONDITIONS**

In the reviewer's words: "The mechanism holds. The four sets move into the rule list statement for statement, the c+1 and c+2 keying is band-exact, the reservation bound holds with E4 and E5, and the framed block is sound. Every figure behind a director question reproduces. The defects are in the paired-run bar (LV8), the mutation map, one load rule and the moving-lines list. Each would cost a build cycle; none needs a redesign."

## What holds (checked)

- **The migration is complete.** Every HEAD reader and writer of the four sets is in section 1.4: W:9502-9517, :9800-9801, :9971, :10046, :10099-10112, :10168-10189, :10201-10256, :10263-10275, :10477-10479, :13777-13793 and :14424-14444, plus H:1651-1652 and :1701-1703. RedHope reads only through the getters, and no `Debug_` hook writes the sets.
- **Precedence by kind keeps `-solidarity`'s line.** The embargo is made first and the Comply second, and the log reads "route closed" (`solidarity.log:251`).
- **The other migrated steps match HEAD:**
  - a repeated sabotage appends a rule, and its largest `LiveSols` follows HEAD's reset double for double;
  - the embargo becomes `Severed` in place, then the trigger runs in the same call, as HEAD's `RemoveCurrent` plus `Num() == 0` does;
  - goodwill, Pacify and the Comply's count are unchanged.
- **The covert roll.** A copy of `StrCrc32` plus `HashCombine` reproduces CXCX, and `-espionage`'s caught=0 then caught=1.
  - The fourth `-covert` op runs on the step ending 1,500 s (sol fraction 0.25), which is day, so P = 0.475.
  - LV8's rolls are clean in both twins: Kestrel at attempt 1 gives U = 0.727, and Zarya at attempt 0 gives U = 0.907.
  - E6 adds no roll.
- **Keying.**
  - `StepUplink` runs before `StepCityGrowth` in both spines (W:672/683, :844/855), so the c+1 cut and the `ClearSol`-keyed effects are band-exact.
  - The inversion bound holds in Tick too. A frame runs at most 4 era steps (240 s) or 32 sub-steps, and `StepUplink` reads the frame end (W:1067).
  - c ≥ O for any act that runs after a booking. Retirement comes only after every order in the window has cleared (E4 R4).
- **Money.**
  - A sell receives floor(value × (10000 − m) / 10000), which rises with p.
  - Since kg × ⌈x⌉ ≥ ⌈kg × x⌉, kg × HiM covers the buy cost and kg × LoM stays under the sell receipt. The bound composes with E5 2.4.
  - m ≤ 9,000, so no sell can turn negative: no pump.
- **Save.**
  - The from-the-end offset is right, and the payload has its own reader.
  - The tail blocks are read before the clock is restored (W:14786-14792), so taking the open sol from `MarketCheckedSol` or `ProductionCheckedSol` is right.
- **Rulings and the playtest.**
  - No combat, no Hope or city money, the sim code stays in RedHopeSim, and nothing is ordered by FName index.
  - The bot sends no political verb and no convoy. Its baseline's only political event is the demand at sol 100.25.
- **The numbers reproduce.** The reviewer's model matches E4 R5 exactly: 17,540.35 cr by sol 99 (17,477.55 with the first dispatch at 0.0), and 826.9 → 827.9 cr a run.
  - **Section 4.5:** 28,975.80 / 28,256.75 (−719.05) / 28,259.75 (−716.05) / 28,195.70 (−780.10) cr. Ice 750 → 1,002 mcr and Struct 9,351 → 10,331. Recovery at sols 78/80, 68/77 and 73. First runs after recovery 885.5 and 849.1 cr.
  - **Q-A:** 22,507.75 / 38,237.85 / 20,851.95 cr.
  - **Q-C (a):** 564.4-564.7 cr a run and 2,105.63 cr of tariff, with every price bit-identical.
  - **Q-C (b):** 5,223.98 and 602.94 cr of tariff, and the first run charges 45 cr.
  - **Risk 5:** 84,093 and 141,386 cr.
  - **Section 2.3:** home at 23.04 with relation 50 and Influence 15.92; the demand at 91.67, the embargo at 116.67, the Martian path at 132.17.
  - The 75 kg theft at sol 60 moves nothing.

## REQUIRED CHANGES

**1. LV8's bar compares state that is not band-exact under the design's own rules.**
- **(a) Pending effects.** The `Covert Kestrel` due 24.0 + 10 s is recorded by the era twin on the step ending 28,830 s, before the roll closing 23. The agent twin records it at 28,810 s, after that roll. So at the threshold closing 23, one twin's pending list holds the theft and the other's does not. Only the *application* of an effect is keyed by `ClearSol`; its recording is live.
- **(b) The rule set.** Zarya's `Disrupted` rule retires on its live timer.
  - **Era twin:** in doubles, 6.0 − 0.05 × n first reaches ≤ 0 at n = 121, the step ending 32,430 s. Its `StepEarth` runs after that step's roll closing 26 (W:855 before :858), so the rule retires at the roll closing 27.
  - **Agent twin:** the timer runs out at about 32,390 s, so the rule retires at the roll closing 26.
  - At threshold 27 the "kind, rival, window, origin" sets differ. Section 1.4 itself says retirement is band-dependent.
- **Fix.** Hold LV8's identity bar to `RollMarket`'s outputs:
  - prices, stores, fills and charges;
  - the markup applied at the clearing, the cut applied per roll, and the effects applied;
  - `TariffPaidMcr`.

  Compare rules only as the windows that cover sols at or after the open sol, and pending effects only as applied (or at a mid-sol sample).

**2. The mutation map names checks that cannot fail.**
- **Mutation 4 is not caught by LV8.**
  - Under 3.2's recompute at the clearing, a tariff window from c+1, `[19, 49)`, gives the 19.0 + 10 s order a markup of 2,500 at the clearing in *both* twins. Fills, charges, markups, windows and `TariffPaidMcr` are all equal.
  - The only difference is the era twin's reservation, booked before the break with a markup of 0. LV8 does not compare reservations, and E4 condition 5(b) pays the excess from the 20,000 cr spendable.
  - The pact-start half is never exercised: no order has `EffSol` 18.
  - **Fix:** LV8 and LV4 assert, for every order, that the booking markup equals the clearing markup and that the charge is at most the reservation. Alternatively, add an inverted `Sign` fixture: an order with `EffSol` c+1, queued before the Sign.
- **Mutation 16.** LV8 has one sabotage, and LV9's save-load-save is byte-identical under either implementation. LV6's "a second sabotage at c + 3 makes a second rule" is the check that fails; name it.
- **Mutation 15.** Its example (a pact check that rolls for detection) never runs in LV2's pact-free scripts. Define it as an unconditional extra roll, or pin LV5's covert outcome sequence.
- **LV3's fixture cannot be built.** "One rival gets all four blocks" is impossible: an `Embargo` and a `Severed` never coexist (1.2), and no listed hook removes a `Closed` or `Severed` rule. Add a test hook or split the fixture.

**3. 7.3's load rule for windows contradicts the −1 sentinel.**
- 1.1 makes −1 mean "no window" for `FromSol` and "open-ended" for `UntilSol`, but 7.3 says "a window with `UntilSol ≤ FromSol` drops its rule".
- Read literally, that drops every live pact, whose window is `[c+2, −1)`. It also drops every `Closed`, `Severed` and `Embargo` rule if "no window" is stored as (−1, −1).
- With a good block, "the rules win" over the mirror. So every load would reopen closed routes and forgive defections, the exact failure 7.2 exists to prevent.
- **Fix:**
  - define how a rule with no window is stored;
  - apply the ordering test only when both ends are 0 or more;
  - define retirement for `UntilSol` = −1. A `Disrupted` rule rebuilt from the mirror, or made with the levers off, will have it.

**4. The moving-lines list misses DC9.**
- `-deepcommute` prints `DC9 nothing derived is saved: the 10300-byte file ...` (RHSimDeepCommuteSuite.cpp:712).
- `diff_battery.py:11` masks only `\(\d+ bytes`, so the line is not masked.
- It moved at E1 (9,715), E2b (9,787) and E3 (10,300), and it moves with E6. Add it to 5.2, and to E4's and E5's lists if their builds have not.

## OBSERVATIONS (non-blocking)

1. **Numbers that do not reproduce.**
   - **(a)** Under the cut, Ice's flow is −9,000 or −7,500, not −10,000, because Demand (900,000 or 750,000 g) is below `MarketMinVolumeG`. Before the cut it is +3,793 or +4,545. The 5% clamp still binds for all six rolls.
   - **(b)** The 300 kg theft at sol 30 lifts Ice by 2-11 mcr/kg for 17 sols (31-47), peaking at 1.3%, not 2-10 mcr for 5 sols.
   - **(c)** "1,000 kg of Ice bought right after the sabotage costs 1,002 cr": no order to Zarya can be booked before the refusal ends at about 66.5. That order (`ClearSol` 66) pays 955 cr; 1,002 cr is `ClearSol` 67's price.
   - **(d)** E6 labels prices a sol earlier than E4 R5 does: "1.002 at sol 66" is the price in force during sol 67.
   - **(e)** Risk 6: Ice is 10% or more dearer for 12 sols (63-74), and 34% only at its peak (sol 67).
   - **(f)** Overflow: 3.2's product is 6.5 × 10^13, not 6.5 × 10^16.
2. **`-icelow` and `-arrivals`** reach tension 75 and log EMBARGO and DEFECTION through the migrated code (`icelow.log:712`, `:722`; `arrivals.log:3888`, `:3914`). Name them in 5.2 and in LV2.
3. **E4's MX11 sabotages Zarya.** Under E6 the cut moves Zarya's prices, stores and any owed credit MX11 pins. 5.2's "`-market` untouched" needs that caveat.
4. **E4 R3(b)'s catch-up can land a cut or theft late.** In a queue-earlier order it can close sols before a later-queued act due earlier has run. This is harness-only and identical in both twins; state it as E5 R7 O18 does.
5. **Store `EffSol` on the order.** "The clearing markup from the sol being closed" equals `EffSol` only for on-time and late bookings. It does not after a jump of more than 8 sols or a reseeded `MarketCheckedSol`.
6. **Units after the issue.** "p in mcr/kg, or mmk/kg for a mark order" is wrong once the mark is issued: every price is then in mmk/kg, and credit orders convert Net (E5 2.4). Say which total a converted order's markup goes to.
7. **The mirror's and the block's repairs disagree.** Mirror timers above config are dropped (7.2), while the block's are clamped (7.3). After a lost block and a lowered `EmbargoGraceSols`, the embargo is forgiven. Clamp both. Also force `bLive` true on `Closed` and `Severed` at load: a doctored file can recompute the CRC.
8. **Desk check 3** now refuses the desk while the only member is sabotaged, which changes E5's directed text. Put it to the director or drop it.
9. **Q-A (b)'s "trade resumes once tension falls under 75"** is an automatic lift that HEAD does not have: only Pacify removes an embargo. Specify it if (b) is chosen.
10. **A barter partner's betrayal tariff never charges**, because no order to it is priced. Only injected rivals are barter; say so.
11. **Overflow bounds with the 2,000 kg cap.** E5 O13's bloc-tally bound becomes 1.73 × 10^18, still inside int64, and E4 R2's per-sol tally 6 × 10^6 g. Restate both.
12. **Section 6, for Tick.** The inversion bound is "one frame" (at most 240 s), not "one step".
13. **Two unspecified details.** Say whether pending effects apply while `IsMarketLive()` is false, and order the sub-index by slot.

The conditions, as the reviewer listed them:
1. restrict LV8's bar to `RollMarket` outputs, and compare rules and pending effects in a band-exact form;
2. make mutations 4, 15 and 16, and LV3, able to fail as named;
3. define the no-window encoding, so a load can never drop a `Closed`, `Severed`, `Embargo` or live `Pact` rule;
4. add DC9 to the moving lines.

---

# REVISION

This section replaces the design wherever they differ. The builder's order of authority:
1. the director's answers;
2. this REVISION;
3. the PROFESSOR REVIEW;
4. the design.

Anchors are at HEAD `72e338a`. Every corrected number below was recomputed with the design's integer model and checked against the reviewer's figures. Prices are now labelled as E4 R5 labels them: **the price in force during a sol**, set by the roll that closed the sol before.

**R1. LV8's bar, restated** (required change 1, and the first half of required change 2).

- **What must be identical in both twins, at every threshold from 16 to 36** (int64 equality). These are `RollMarket`'s outputs:
  - every traded price, every rival store, every fill and every charge;
  - the markup applied to every line at its clearing;
  - the cut applied to each rival at each roll, and each pending effect at the roll that applies it;
  - `TariffPaidMcr`.
- **Per order, in each twin:** the markup at booking equals the markup at the clearing, and the charge is at most the reservation. **This is the check that catches mutation 4.**
- **Rules** are compared as the set of `(kind, rival, origin, FromSol, UntilSol)` over the rules whose market window covers a sol at or after the open sol. Rules with no window are left out, and Ids are never compared.
- **Pending effects** are compared only as applied, never as a list at a threshold.
- **A second inversion fixture, for the pact's start.** This replaces the schedule's first row:

  | Sim time | Command | Why |
  |---|---|---|
  | queued at 16.2, due 17.0 + 10 s | `MarketOrder Kestrel Ice:1500` | `ClearSol` 17, queued *before* the Sign. |
  | queued at 16.3, due 17.0 − 20 s | `Treaty Kestrel Sign` | `ClearSol` 16, so terms from sol 18. The era twin's step ending at 17.0 + 30 s runs the order first; the agent twin runs the Sign first. |

  - **Under the design,** the order's `EffSol` 17 is below the pact's 18, so the 1,500 kg order is over the 1,000 kg cap and is refused in both twins.
  - **Under mutation 4** (terms from c + 1 = 17), the agent twin accepts it and the era twin refuses it.
  - The rest of the schedule is unchanged: the break fixture at 19.0 still finds the pact window `[18, open)` covering sol 19.

**R2. The mutation map and LV3, restated** (required change 2).

- **Mutation 4** (a window from c + 1): R1's per-order check (the era twin's booking markup 0 against a clearing markup of 2,500), R1's pact-start fixture, and LV4's booking-equals-clearing assertion.
- **Mutation 15** becomes **an unconditional extra `RollCovertDetected` call in `ResolveCovert`**. LV2's CXCX changes.
- **Mutation 16** (a repeated sabotage updating its rule in place): **LV6**, "a second sabotage at c + 3 makes a second rule". LV8 and LV9 are struck from its line.
- **LV3 is split** (no rival can carry an `Embargo` and a `Severed` at once, 1.2):
  - **(a)** One rival gets three blocks in reverse precedence: an `Embargo` (tension forced to 80), then a `Disrupted` (a sabotage), then a `Closed` (a Comply).
    - The refusal is the closed text.
    - After `Debug_RemoveRule(Id)` removes the `Closed` rule, it is the disrupted text. When the downtime ends, it is the embargo text.
  - **(b)** A second rival's embargo runs out into `Severed`, and its refusal is the defection text.
  - **(c)** The reverse-name-order Comply check and the seeded permutation check are unchanged.
- **The new hook**, `Debug_RemoveRule(Id)`, is test-only. No player path removes a `Closed` or `Severed` rule.

**R3. The window encoding, and retirement** (required change 3). This replaces 1.1's −1 note and 7.3's window bullet.

- **The encoding:**
  - **no market window:** `FromSol = −1` and `UntilSol = −1`;
  - **a closed window:** `0 ≤ FromSol < UntilSol`;
  - **an open-ended window:** `FromSol ≥ 0` and `UntilSol = −1`.
- **Each kind's legal shapes:**

  | Kind | Window |
  |---|---|
  | `Closed`, `Severed`, `Embargo` | none, always |
  | `Disrupted` | closed, or none (rebuilt from the mirror, or made with the levers off) |
  | `Pact` | open-ended while live; closed once ended |
  | `Tariff` | closed |

- **At load,** a rule whose window is not a legal shape for its kind is **repaired to its kind's shape, never dropped**, with a Warning:
  - `Closed`, `Severed` and `Embargo` get no window;
  - a `Disrupted` rule gets none;
  - a live `Pact` gets an open window from its `FromSol`, or from the open sol + 2 if `FromSol` is −1;
  - an ended `Pact` or a `Tariff` with no legal window is dropped, since a markup or a cap with no sols does nothing.

  The ordering test (`UntilSol` > `FromSol`) applies only when both ends are 0 or more.
- **The live flag at load.** `bLive` is forced true on every `Closed`, `Severed` and `Embargo` rule, whatever the file says (O7). A doctored file can recompute its CRC.
- **Retirement:**
  - a rule with no window retires when its live part is over: a `Disrupted` rule when its timer is out, and an `Embargo` when it is removed or hardened. `Closed` and `Severed` never retire;
  - a rule with an open-ended window never retires while it is live;
  - a rule with a closed window retires once its live part is over and `UntilSol` is at or below the open sol.

**R4. What the battery moves, and what it exercises** (required change 4; observations 2 and 3). This replaces 5.2's two lists where they differ.

- **The lines that print whole-file sizes** are now five: CV8, DP11, EC1's two, and **DC9** (`-deepcommute`: `the 10300-byte file`, RHSimDeepCommuteSuite.cpp:712). `diff_battery.py:11` masks only `\(\d+ bytes`. The builder checks E4's and E5's lists for DC9 too.
- **Two more suites run the migrated embargo** and stay line-identical: `-icelow` and `-arrivals`. Each reaches tension 75 and logs `=== EMBARGO: Zarya Station (Earth tension 75, grace 8 sols) ===` and `=== DEFECTION: Zarya Station (relation 5) ===`. LV2 replays both with hard checks on the embargo's sol and the defection step.
- **E4's `-market` MX11 sabotages Zarya.** Under E6 the cut moves Zarya's prices and stores in that check. Any MX11 line that prints a price, a store or an owed credit after the sabotage moves **on purpose**. The builder lists it by name; it is not a fixture edit.

**R5. The clearing sol is stored on the order** (observations 4, 5, 6 and 13).

- **`EffSol` is saved.**
  - At booking, every priced order records `EffSol = max(ClearSol, O)` on its convoy record (E4 3.5), and E5's desk orders do not need it.
  - The clearing reads the markup at the stored `EffSol`, never at the sol being closed. So a long jump (more than 8 sols) or a reseeded `MarketCheckedSol` cannot change a booked order's markup.
  - It is saved in E6's block as a list: the count, then `(ConvoyId, EffSol)` for every uncleared priced convoy, by Id.
  - **The repairs:**
    - an unknown convoy Id is dropped;
    - an `EffSol` below the convoy's `ClearSol` is set to `ClearSol`;
    - a convoy missing from the list (for example after a lost block) clears at `EffSol = ClearSol`, with markups read there.

    A lost list can only lower a charge below its reservation, which E4's clearing tolerates.
  - The window-stability proof (3.2, section 6) now rests on the stored value.
- **Units after the issue.**
  - Before the mark is issued, prices are mcr/kg. From `CurrencyOpenSol` on, every price is mmk/kg (E5 1.3), and so is every line value and markup.
  - A mark order pays its marked-up Net in marks. A credit order converts its marked-up Net to credits at the rate (E5 2.4).
  - **`TariffPaidMcr`** counts the markup on orders cleared before the market counts in marks. **`TariffPaidMmk`** counts it on every order cleared after, in the market's unit, whichever currency the order settles in. Both are panel-only.
- **Pending effects while the market is off.** They are applied, because they are part of the clearing, which E4 R4 runs whether or not the market is live.
- **The sub-index** of a theft's or an offering's effects is the slot order.
- **A late landing** (E5 R7 O18). After a harness clock jump, E4 R3(b)'s catch-up can close a sol before a later-queued act due in it has run. That act's cut or effect then applies from the next roll, identically in both twins. It happens only after a harness jump.
- **Section 6, for Tick.** The inversion bound is one frame, at most 240 s, so under one sol. The c + 2 argument stands.

**R6. Repairs that agree** (observation 7). The mirror's timers are clamped to their config values, as the block's are (7.3). They are no longer dropped, so a lost block and a lowered `EmbargoGraceSols` cannot forgive an embargo.

**R7. Desk check 3 is E5's** (observation 8). The harmonisation in 1.3 is withdrawn.
- `CanExchange` check 3 keeps E5's directed text and meaning: available, route open, not defected, not embargoed. It reads those three kinds from the rule list.
- A sabotaged member can still serve the desk, as E5 has it.

**R8. Q-A (b), specified** (observation 9). If the director chooses (b), the defection branch in `StepPreemptive`'s countdown reads as follows:
- **When a live partner's grace reaches ≤ 0,** its `Embargo` rule is removed and the partner does not defect. One log line: `=== EMBARGO LIFTED: %s holds to its pact ===`.
- **The trigger then runs in the same call, as HEAD's does.**
  - If tension is still 75 or more, Earth picks again, and the partner, being eligible, is embargoed afresh with a new grace.
  - If tension is under 75, nobody is picked and trade resumes.
- **So under (b)**, trade with a partner resumes at a Pacify, or at the end of a grace that finds tension under 75. The question's text is restated in R10.

**R9. The smaller corrections** (observations 1, 10 and 11).

- **4.5's Ice flow.** Under the cut, Ice's flow is −9,000 (−7,500 when no colony buy is in the window), not −10,000, because Demand (900,000 or 750,000 g) is under `MarketMinVolumeG`. Before the cut it is +3,793 or +4,545. The 5% clamp binds for all six rolls either way.
- **4.5's prices, relabelled as prices in force:**
  - Ice is 0.750 cr/kg in force during sol 61 and **1.002 during sol 67 (+33.6%)**, and it is **10% or more dearer for 12 sols (in force during sols 63-74)**.
  - Struct is 9.351 during sol 61 and 10.331 during sol 67.
  - The "back within 1%" sols each move one later: Ice 79 and Struct 81 under (a); 69 and 78 under (b); 74 under (c).
- **The buyer.** The first order to Zarya that can book after the refusal ends (sol 66.5, `ClearSol` 66) pays **955 cr** for 1,000 kg of Ice. A sol later it pays 1,002 cr, against 750 cr undisturbed.
- **Theft.** 300 kg stolen at sol 30 lifts Ice by **2-11 mcr/kg for 17 sols, at most 1.3%**. The 75 kg theft at sol 60 still moves nothing.
- **Risk 6** reads: Ice 10% or more dearer for 12 sols, 34% at its peak.
- **Overflow.**
  - 3.2's product is at most 2,000 kg × 3.6 × 10^6 × 9,000, about **6.5 × 10^13**.
  - E4 R2's per-sol tally becomes 3 × 2,000 kg = 6 × 10^6 g each way.
  - E5 O13's bloc-tally bound becomes 8 × 3 × 2,000 × 4 × 900,000 × 10^4, about **1.73 × 10^18**, inside int64 (9.2 × 10^18).
- **Barter partners.** A pact with a barter rival gives no bigger convoy (its lots are fixed), and its betrayal tariff never charges, because no order to it is priced. Only injected test rivals are barter; both shipped rows have an economy.

**R10. The director questions, restated** (they replace the ones above). The model is unchanged:
- E4 R5's update with Zarya's placeholder data, in integers;
- the standing trade to Zarya every round trip;
- tension from 0 at 0.6 a sol;
- a pact signed at sol 23.5, the first moment it is possible.

It reproduces E4's 17,540.35 cr by sol 99, and the reviewer's independent model reproduces every figure below.

**Q-A. What does a pact do for the colony when Earth leans on its partner?**

Every option has the same base:
- the pact costs 15 Influence and +5 Earth tension, so Earth's first demand comes at sol 91.7 instead of 100;
- it needs relation 50, first possible at about sol 23.5;
- it doubles the cargo of the partner's convoy, to 2,000 kg each way. A colony that sells Struct at full loads every round trip earns 141,386 cr by sol 200 instead of 84,093.

The table assumes the colony never answers Earth (as the playtest bot plays) and has not found Meridian.

| Option | Zarya's trade stops | Zarya defects | The standing trade by sol 200 |
|---|---|---|---|
| no pact (today) | sol 125.0 | sol 133.0, for good | 22,508 cr |
| **(a) Earth cannot embargo a partner (recommended)** | never | never | **38,238 cr** |
| (b) the partner holds out: the embargo lands, but at the end of each 8-sol grace the partner refuses to defect. Earth re-imposes it at once while tension is 75 or more | sol 116.7 | never | 20,852 cr |
| (c) no political term, only the bigger convoy | sol 116.7 | sol 124.7 | 20,852 cr |

- **(b) and (c) stop trade 8.3 sols *sooner* than no pact**, because the pact's own +5 tension brings the embargo forward.
- **Under (b)** trade resumes at a Pacify (20 Influence), or when a grace ends with tension under 75.
- **Under (a)**, if Meridian has been found, Earth embargoes Meridian at 116.7 instead, and Meridian defects at 124.7.
- **A colony that Defies every demand** never reaches tension 75, so the options do not differ. The pact only brings the demands forward: the Martian ending path (identity +60) comes at sol 132.2 instead of 140.5.

**Q-B. How hard does a sabotage hit the target's market?** The model sabotages Zarya at sol 60.5. Today's six-sol trade refusal applies in every option.
- **(a) (recommended): the target's production stops for the six whole sols after the act.**
  - Zarya's Ice peaks at 1.00 cr/kg, 34% above its 0.75 floor, and stays 10% or more dearer for 12 sols.
  - Its Struct peaks at 10.33 cr/kg, 10.5% above 9.35.
  - Both are back on their paths about 20 sols after the act.
  - The first order the colony can book after the refusal pays 955 cr for 1,000 kg of Ice, against 750.
  - The colony's own standing trade loses one run: 719 cr over sols 60-199.
- **(b): half its production.** Ice +3.7% and Struct +6.5% at their peaks; −716 cr.
- **(c): no market effect, as today.** Only the refusal: Ice is flat, and Struct is 2.6% up from the colony's own missing sale; −780 cr.

In every option a clean theft now comes out of the victim's store, but it moves almost nothing: no price at all when the store is full, and at most 1.3% for 300 kg stolen when it is not.

**Q-C. Who may charge the colony a tariff?**
- **(a) (recommended): only a partner the colony betrays,** by breaking the pact or by being caught spying on or sabotaging it.
  - The charge is 25% on every trade with it, both ways, for 30 sols.
  - The standing trade pays 564.7 cr a run instead of 827.9: **2,106 cr** lost over the 30 sols. No price moves.
  - With no betrayal there is no tariff, so E4's and E5's money is untouched.
- **(b): every neighbour charges a standing 10%,** waived by a pact or by bloc membership.
  - The standing trade pays 5,224 cr in tariff by sol 200 with no pact, and 603 cr with a pact from sol 25.
  - The first run *charges* the colony 45 cr instead of paying it 50, and a heavy Struct seller loses 8,409 cr.
  - It is not inert: E4's money checks (`-market` MX7's "+50 cr at the return") and every credit total move.
- **(c): no tariffs in E6.** Breaking a pact then costs only relation, HumanNature and the pact.

**The verdict after revision.** The review's verdict was **CLEARED WITH CONDITIONS**. R1-R4 write in all four required changes, and R5-R9 take observations 1-13. R1 (the new fixture), R3 (the window encoding and retirement), R5 (`EffSol` stored on the order) and R8 (Q-A (b)'s lift) are new mechanisms, so a short second review of just those follows.

The conditions left:
- the director's answers to Q-A, Q-B and Q-C;
- E4, E5 and the economy-fixes gate built first. E6 takes the next free version, and the builder rebases every E4 and E5 name onto them as built (risk 7);
- the playtest claim re-checked at the build's HEAD (5.2).

---

# PROFESSOR RE-REVIEW (2026-10-24)

A short, independent re-review of the REVISION's new mechanisms only: R1 (LV8's bar and the pact-start fixture), R3 (the window encoding and retirement), R5 (`EffSol` on the order) and R8 (Q-A (b)'s lift), with spot checks of R9 and R10. It is read-only.
- **Read:** the whole document; E4 and E5 (their REVISIONs, re-reviews and director answers); and, at HEAD `72e338a`:
  - the W spines (W:641-686, :844-858);
  - `StepUplink` (W:1065-1080);
  - `StepTrade` (W:9728-9760);
  - `StepEarth`, `StepPreemptive` and `ResolvePacify` (W:10070-10285);
  - `CanEnterEraMode` (W:776-779) and the clock's per-frame caps;
  - `RHSimEconomySuite.cpp:60-64` and `RH_Events.csv`.
- **Two scratch checks** of its own: convoy timing, and where the fixture's commands land.

It is recorded as returned, lightly formatted.

Verdict: **CLEARED WITH CONDITIONS**

In the reviewer's words: "R1, R3, R5 and R8 are sound and band-exact as written, and the pact-start fixture does what it claims in both bands. One claim in R2's mutation map cannot fail as named."

## What holds (checked)

- **The pact-start fixture.**
  - Both commands land in the era step (20,370, 20,430] s, which contains the sol-17 boundary.
  - `StepUplink` runs due commands in queue order (W:1068-1080). So the era twin books the order first (O = 16, `EffSol` 17), before any pact exists, and then signs.
  - The agent twin signs at 20,380 s and books at 20,410 s, after the roll closing 16 (`EffSol` 17).
  - Under the design (`[18, open)`) both twins refuse the order. Under mutation 4 (`[17, open)`) only the agent twin accepts it.
- **The rest of the schedule, with the pact at `[18, open)`.**
  - The 19.0 + 10 s order is accepted in both twins at markup 0. In the agent twin the break has already set the window to `[18, 20)`, which still covers sol 19.
  - Mutation 5 (`[18, 19)`) makes the agent twin refuse that order.
  - Mutation 6 gives `[22, 28)` in the era twin against `[21, 27)` in the agent twin.
  - Mutation 7 fills 75 kg against 100.
  - Each splits something R1 compares.
- **R1's bar is band-exact for this schedule.**
  - Stores and prices change only at rolls.
  - The rule filter (`UntilSol` > the open sol) hides retirement, which is band-dependent.
  - Pending effects are compared only as applied.
- **No collisions with other rows.**
  - No convoy is out at sol 17, because both twins refuse the order.
  - Kestrel's 19.0 + 10 s convoy is home in sol 20 in both twins: the era twin at 24,030 s after 21 steps, the agent twin at about 24,009.9 s.
  - Flare_1 is a SolarFlare, and only a DustStorm freezes convoys (W:9745). The paired suites call `EraStep` directly, so `CanEnterEraMode`'s flare refusal never runs.
  - The 1,500 kg Ice reservation is at most about 4,725 cr at any Ice price up to V (3 cr/kg). That is under half of the 20,000 cr spendable.
- **R3 handles these states correctly:**
  - a pact ended by a Break, a Comply or (c)'s defection gets a closed window;
  - a `Disrupted` rule rebuilt from the mirror, or made with the levers off, has no window and retires on its timer;
  - R8's lift removes the embargo rule.
  - `Closed` and `Severed` are limited to one per rival, so the save cannot grow without bound.
- **R5.**
  - A frame is at most 4 era steps (240 s), and `StepUplink` reads the frame end (W:1067). So the c + 2 argument holds.
  - At E5's withdrawal roll no order is uncleared and booking is closed (E5 R5). So no stored `EffSol` ever meets a re-expressed price.
  - A lost list means a lost block, which loses the tariffs too. So "a lost list can only lower a charge" holds.
- **R8 has HEAD's shape:** the countdown, then the trigger, in one call (W:10201-10256), inside `StepEarth` (W:10115).
  - R10's (b) row ("re-imposes it at once") matches R8.
  - Its 20,852 cr equals (c)'s, because the modelled tension never falls under 75.
- **R9 and R10's spot checks** agree with the reviewer:
  - +33.6% and +10.5%;
  - 6.48 × 10^13, and 1.73 × 10^18 (in E5 O13's gram convention);
  - sols 91.7, 116.7, 124.7, 125.0 and 133.0.

## REQUIRED CHANGES

**1. Mutation 4's line names two checks that cannot fail as specified.**
- **LV4.** It sets its windows with `Debug_AddTariff`, in one band.
  - The c + 1 or c + 2 offset is applied only when an act executes.
  - A mismatch needs a booking with `EffSol` c + 1 to run before its act, and only an era boundary step with a queue inversion produces that.
  - So LV4 stays green under the mutation, and LV12 fails because of its own map.
- **R1's per-order equality check.**
  - The era twin books the 19.0 + 10 s order and runs the Break in the same `StepUplink` call.
  - A suite that reads `GetLineMarkupBp` after that step sees the window `[19, 49)` and gets 2,500, not the 0 the order was booked at.
  - So the check bites only if the code records the markup at booking.
- **The "charge ≤ reservation" clause** bites only if the fill is above about 1,330 kg (1,500 × 1.05 / (1.25 × 0.95), with Ice falling one step). E4 R7 never pins Kestrel's Ice store or cap.
- **Fix.**
  - Store each line's booking markup on the convoy record, beside `EffSol`, and have the check read it.
  - Strike LV4 from mutation 4's line, or give LV4 an era-band `Break` with a queue inversion.
  - Call `Debug_SetRivalStore(Kestrel, Ice, 1500)` or more before the roll closing 19.
  - The pact-start fixture already catches the mutation, so this corrects the map; nothing needs redesigning.

## OBSERVATIONS (non-blocking)

1. **R3 against 7.3.**
   - 7.3 still says "an `Embargo` with `bLive` false is dropped", which contradicts R3's forced `bLive`. The authority order picks R3; strike the bullet so LV9 has one expectation.
   - R3 replaces 7.3's whole window bullet, and that drops its upper clamp (the open sol + 2 + the longest window). Without the clamp, a doctored Betrayal tariff with `UntilSol` near INT32_MAX never retires, and check 7 refuses re-signing for ever. Keep the clamp.
2. **Pact end states that R3 does not cover.**
   - **An inverted end.** A Break or Comply queued after the Sign but due in the previous sol can run on the same step. That is section 6's own inversion. It gives `FromSol` c + 2 > `UntilSol` e + 2 = c + 1, which no R3 shape or retirement clause fits, so a load drops the rule with a Warning. **Fix:** set `UntilSol = max(e + 2, FromSol)`, and retire an empty window at the next roll.
   - **A second live pact.** 7.3's "a second live Pact is ended" does not set `UntilSol`. An ended pact with `UntilSol` −1 never retires, and it keeps the 2,000 kg cap, because the pact check ignores `bLive`. **Fix:** drop the second pact instead.
3. **R5.**
   - **A default for a missing entry.** For every uncleared convoy, `EffSol` is exactly `max(ClearSol, MarketCheckedSol)`: any roll closing a sol at or after `ClearSol` clears the convoy, and a late booking clears at the next roll. Use that value as the default for a missing entry instead of `ClearSol`. The saved list then becomes optional.
   - **If the list stays,** add it to 7.3's payload and to LV9.
   - **`TariffPaidMmk`.** Its "cleared after" should read "while the market counts in marks", because E5 R5's withdrawal returns the market to credits.
   - **The late-landing note** should also cover windows. After a harness jump an act's c can be below O, so its c + 2 tariff can cover an order booked earlier in the same catch-up step.
4. **R8 and R10.**
   - A Pacify resumes trade only if it takes tension under 75. Otherwise the trigger in the same step's `StepEarth` re-embargoes at once, as HEAD does. R10's "trade resumes at a Pacify" needs that qualifier.
   - Under (b), HEAD's alert "Reassure them within 8 sols or they side with Earth" (W:10253) repeats every 8 sols, and is false for a partner.
   - "The partner is embargoed afresh" holds only while the partner is the friendliest eligible rival.
   - If the director chooses (b), LV5 needs checks for it.
5. **LV8's home-sol clause.** If the clause survives R1, carry over E5's re-review condition 1 exemption for boundary-step orders. The 19.0 + 10 s order passes it only because the era band takes one step more than a sol (21 steps).
6. **LV8's rule comparison** is band-exact only while no rule-making act is due in the first 30 s of a sol, because the era twin's threshold step has already run such acts. The current schedule has none (the 24.0 + 10 s covert op is clean). Say so in LV8, or sample the rules at the roll.

**The condition left:** fix mutation 4's line in the mutation map, as required change 1 describes. As with E4's and E5's re-reviews, the condition and the observations are applied by the builder, after the director's answers to Q-A, Q-B and Q-C.


# DIRECTOR ANSWER (2026-10-24)

The numbers are from the R10 model, reproduced by the re-review.
- **Q-A (a):** Earth cannot embargo a pact partner. Modelled: Zarya's trade never stops; the standing trade earns 38,238 cr by sol 200, against 22,508 with no pact.
- **Q-B (a):** a sabotage stops the target's production for the six whole sols after the act. Zarya's Ice peaks about 34% above its floor and recovers in about 20 sols.
- **Q-C (a):** only a betrayed partner may charge a tariff: 25% both ways for 30 sols (about 2,106 cr lost). With no betrayal there is no tariff, so E4 and E5 money is untouched.


# REFRESH AT HEAD 31ffa6f (2026-10-25)

A read-only maintenance pass, so the builder starts on the code as it is. Since this design was written at `72e338a` (v53), two gates have landed:
- **the economy fixes** (`96d2246`, v54), which put the save tail in `RHSaveFrame` frames;
- **E4** (`76d9cc1`, v55), which moved the convoy and its refusal chain out of W.

E5 is still designed only. HEAD `31ffa6f` changes no source after E4. Nothing here changes the design or the director's answers. Where the code now contradicts the text, this section says what to use instead. Short names are the doc's, plus **"M"** for `Source/RedHopeSim/Private/RHSimMarket.cpp`.

## Re-anchored references

Every `file:line` above was mapped from `72e338a` to HEAD through `git diff`, and the line's text compared.
- **Unchanged (1):** `RHSimDeepCommuteSuite.cpp:712` (DC9's line; it now prints 13,408 bytes).
- **Moved, same text:** every other anchor except the next two bullets.
- **The version line, `W:36`,** is now `W:37`, and reads `RHSaveVersion = 55`.
- **Gone from W:** the refusal chain, `StepTrade` and the goodwill check. Their replacements are in the second table.

| Old | New | Line |
|---|---|---|
| `C:1223-1317` | `C:1225-1319` | `if (FParse::Param(*Params, TEXT("solidarity")))` |
| `C:1322-1401` | `C:1324-1403` | `if (FParse::Param(*Params, TEXT("covert")))` |
| `C:1351-1369` | `C:1353-1371` | `// relation}; the intent always drops HumanNature by the shift (-6);` |
| `C:1408` | `C:1410` | `if (FParse::Param(*Params, TEXT("espionage")))` |
| `C:1500-1594` | `C:1502-1596` | `if (FParse::Param(*Params, TEXT("preemptive")))` |
| `H:1651-1652` | `H:1773-1774` | `bool IsRouteClosed(FName Rival) const { return ClosedRoutes.Contains(R` |
| `H:1701-1702` | `H:1823-1824` | `bool IsEmbargoed(FName Rival) const { return EmbargoGrace.Contains(Riv` |
| `H:1701-1705` | `H:1823-1827` | `bool IsEmbargoed(FName Rival) const { return EmbargoGrace.Contains(Riv` |
| `H:1701-1703` | `H:1823-1825` | `bool IsEmbargoed(FName Rival) const { return EmbargoGrace.Contains(Riv` |
| `H:3741` | `H:3930` | `TSet<FName> ClosedRoutes;` |
| `H:3770` | `H:3959` | `TMap<FName, double> SabotageSols; // rival -> disruption sols remainin` |
| `H:3782-3783` | `H:3971-3972` | `TMap<FName, double> EmbargoGrace;` |
| `K:2003-2066` | `K:2180-2243` | `return S && (S->IsEarthDemandPending() // !S->GetEmbargoingRival().IsN` |
| `K:2135-2202` | `K:2338-2405` | `Defs->ForEachRivalRow([&](FName RowName, const FRHRivalRow& Row)` |
| `K:2159-2162` | `K:2362-2365` | `if (S->HasDefected(RowName)) { return Txt(TEXT("sided with Earth")); }` |
| `RHCheats.cpp:1066-1071` | `RHCheats.cpp:1077-1082` | `const double Sab = Sim->GetSabotageRemaining(Name);` |
| `RHSimEconomySuite.cpp:60-64` | `RHSimEconomySuite.cpp:63-67` | `const auto EraOnce = [&]()` |
| `V:4246-4248` | `V:4249-4251` | `if (Sim->HasDefected(Name)) { State = 2; }` |
| `W:641-686` | `W:643-688` | `if (Clock->IsEraMode())` |
| `W:672` | `W:674` | `StepUplink();` |
| `W:776-779` | `W:778-781` | `if (Event->Type == FName("SolarFlare"))` |
| `W:844` | `W:846` | `StepUplink();` |
| `W:844-858` | `W:846-860` | `StepUplink();` |
| `W:855` | `W:857` | `StepCityGrowth(DtSimSeconds); // same order as the agent band (parity)` |
| `W:858` | `W:860` | `StepEarth(DtSimSeconds); // same order as the agent band (parity)` |
| `W:1065-1080` | `W:1067-1082` | `void URHSimWorldSubsystem::StepUplink()` |
| `W:1067` | `W:1069` | `const double Now = Clock->GetSimSecondsTotal();` |
| `W:1068-1080` | `W:1070-1082` | `for (int32 i = 0; i < UplinkQueue.Num();)` |
| `W:9580-9610` | `W:9516-9546` | `// so timing your op for the Martian night is real. Rides the uplink.` |
| `W:9865-9870` | `W:9688-9693` | `bool URHSimWorldSubsystem::IsNight() const` |
| `W:9884-9902` | `W:9707-9725` | `bool URHSimWorldSubsystem::RollCovertDetected(FName Rival)` |
| `W:9927-9955` | `W:9750-9778` | `*Row->DisplayName, Rel, Hidden, HumanNatureAxis);` |
| `W:9969` | `W:9792` | `double URHSimWorldSubsystem::GetSabotageRemaining(FName Rival) const` |
| `W:9971` | `W:9794` | `const double* S = SabotageSols.Find(Rival);` |
| `W:9989-9993` | `W:9812-9816` | `return false;` |
| `W:9996-9997` | `W:9819-9820` | `}` |
| `W:10014-10050` | `W:9837-9873` | `bool URHSimWorldSubsystem::ResolveSabotage(FName Rival, FString& OutRe` |
| `W:10046` | `W:9869` | `SabotageSols.FindOrAdd(Rival) = SabotageDurationSols;` |
| `W:10070-10285` | `W:9893-10108` | `void URHSimWorldSubsystem::StepEarth(float SubDt)` |
| `W:10099-10112` | `W:9922-9935` | `if (SabotageSols.Num() > 0)` |
| `W:10115` | `W:9938` | `StepPreemptive(SubDt);` |
| `W:10168` | `W:9991` | `double URHSimWorldSubsystem::GetEmbargoGrace(FName Rival) const` |
| `W:10168-10189` | `W:9991-10012` | `double URHSimWorldSubsystem::GetEmbargoGrace(FName Rival) const` |
| `W:10174-10189` | `W:9997-10012` | `FName URHSimWorldSubsystem::GetEmbargoingRival() const` |
| `W:10191-10259` | `W:10014-10082` | `void URHSimWorldSubsystem::StepPreemptive(float SubDt)` |
| `W:10201-10222` | `W:10024-10045` | `if (EmbargoGrace.Num() > 0)` |
| `W:10201-10256` | `W:10024-10079` | `if (EmbargoGrace.Num() > 0)` |
| `W:10228-10256` | `W:10051-10079` | `if (EarthTension >= EarthPreemptiveThreshold && EmbargoGrace.Num() == ` |
| `W:10253` | `W:10076` | `TEXT("EARTH LEANS ON %s — under pressure from Earth, they have suspend` |
| `W:10261-10285` | `W:10084-10108` | `bool URHSimWorldSubsystem::ResolvePacify(FName Rival, FString& OutReas` |
| `W:10263-10275` | `W:10086-10098` | `if (!EmbargoGrace.Contains(Rival))` |
| `W:10457-10518` | `W:10280-10341` | `bool URHSimWorldSubsystem::ResolveDilemma(bool bComply, FString& OutRe` |
| `W:10477-10479` | `W:10300-10302` | `if (!ClosedRoutes.Contains(Name))` |
| `W:13775-13779` | `W:13641-13645` | `// The Solidarity Dilemma (save v19).` |
| `W:13775-13793` | `W:13641-13659` | `// The Solidarity Dilemma (save v19).` |
| `W:13777-13793` | `W:13643-13659` | `TArray<FName> Closed = ClosedRoutes.Array();` |
| `W:13783-13787` | `W:13649-13653` | `// The espionage economy (save v21).` |
| `W:13789-13793` | `W:13655-13659` | `// Earth's pre-emptive pressure (save v22).` |
| `W:14018` | `W:13888` | `if (!FFileHelper::LoadFileToArray(Bytes, *Path))` |
| `W:14031` | `W:13901` | `if (Magic != RHSaveMagic // Version != RHSaveVersion)` |
| `W:14420-14444` | `W:14294-14318` | `// The Solidarity Dilemma (save v19).` |
| `W:14424` | `W:14298` | `ClosedRoutes.Empty();` |
| `W:14424-14444` | `W:14298-14318` | `ClosedRoutes.Empty();` |
| `W:14653` | `W:14527` | `// disagreement left here (a hand-made file) drops the shape with a Wa` |
| `W:14786-14792` | `W:14662-14668` | `// Loads land at 1x regardless of the saved speed: give the player a c` |

**The convoy code, now in M:**

| Old (W at `72e338a`) | Now | What |
|---|---|---|
| `W:9485-9520`, `W:9502-9517`, `W:9502-9520` (the `Convoy` verb's refusal chain) | `M:851` `PlanConvoy`; the chain at `M:866-905` | One plan now serves the `Convoy` and `MarketOrder` verbs (through `BookConvoy`, `M:1041`) and the preflights `CanPlaceOrder` (`M:1027`) and `CanPlaceStandingOrder` (`M:1035`). Its order: no contact (`M:868`); already out to this rival (`M:879`); **E4's new "`%d convoys are already out`"** (`M:884`); closed (`M:887-889`); disrupted (`M:892-894`, through `GetSabotageRemaining`); defected (`M:897-899`); embargo (`M:902-904`). Then E4's lines, cargo, the spare and the money. |
| `W:9728-9760`, `W:9745` (`StepTrade`, the storm) | `M:1106` `StepTrade`, the storm test at `M:1114-1115` | Same rule, for every convoy in `Convoys`. |
| `W:9800-9801` (`bRelationshipIntact`) | `M:1236` | Same expression on the three sets. E4 adds the goodwill floor beside it (`M:1237`). |

## E4 and the economy fixes as built, against what E6 assumes

| E6 says | As built | What E6 must use |
|---|---|---|
| "No `Debug_` hook writes the sets" (the review's "What holds", true at `72e338a`) | **E4 added five that do** (`H:1603-1607`): `Debug_CloseRoute`, `Debug_SetEmbargo(Rival, Grace)`, `Debug_SetDefected`, `Debug_SetSabotage(Rival, Sols)` and `Debug_ClearPolitics`. `-market`'s MX11 (`RHSimMarketSuite.cpp:1606-1665`) uses them to pin the four refusal texts and a defection mid-trip. | **use instead:** add them to 1.4's migration table, rebuilt on the rule list with Origin `Harness`: a `Closed`, an `Embargo` (`LiveSols` = the grace), a `Severed`, a `Disrupted` (`LiveSols` = the sols, **no market window**, a legal R3 shape), and "remove every rule of the four blocking kinds". See finding F1. |
| The block is checked "in the place of HEAD's checks 3-6" in the `Convoy` verb and in E4's `CanPlaceOrder` (1.3) | One site: `PlanConvoy` (`M:887-905`), after **three** earlier checks, E4's convoy count being the third | **use instead:** replace `M:887-905` with the walk's block. The precedence of the four texts is unchanged, and E4's count check stays ahead of them. |
| `EffSol = max(ClearSol, O)`, read at booking (1.3, 3.2, R5) | `PlanConvoy(Rival, bStanding, OrderText, OutPlan, OutWhy)` has **no clearing-sol input**. `BookConvoy` computes `ClearSol` (`M:1046`) before calling it; the deck's preflights have no command time at all. | **use instead:** add an `EffSol` parameter to `PlanConvoy`. `BookConvoy` passes `max(ClearSol, MarketCheckedSol)`. The preflights pass the sol an order sent now would clear in. See finding F2. |
| The pact's cargo cap replaces E4's `ConvoyCargoKg` (2.1) | The cap check is `M:971-974` (`ConvoyCargoKg`, config, 1,000; `GetConvoyCargoKg()`) | The pact's `PactCargoKg` goes in that one check, keyed on `EffSol`. |
| E4 3.2 step 5's reservation, `Hi(q)` and `Lo(q)` (3.2) | `GetOrderReservationMcr(Rival, Lines)` (`M:826`) with `MktHiQuote` / `MktLoQuote` (`M:33-34`) | `HiM` / `LoM` need each line's markup, so `GetOrderReservationMcr` gains the markups (or an `EffSol` to read them from). |
| The tariff at the clearing (3.2) | `ClearConvoy(FRHConvoy&, ClosingSol)` (`M:639`) computes `BuysValue`, `SellsValue` and `ValueMcr` (`M:702`) | The markup goes into `ClearConvoy`'s Net. `ValueMcr` (the goodwill floor's input) stays at the clearing price **before** the markup, as 2.2 has the bloc tally. |
| The convoy record (E4 3.5) gains `EffSol` (R5) and, per the re-review's change 1, each line's booking markup | `FRHConvoy` has neither, and its fields are saved in `'RHE4'` | **use instead:** keep both in E6's own payload, by convoy Id (R5's list), and do not touch `'RHE4'`: E4's MX9 pins its layout. The re-review's observation 3 holds as built: `EffSol` is always `max(ClearSol, MarketCheckedSol)` for an uncleared convoy, so a missing entry defaults to that. The booking markups cannot be re-derived, so the list stays. |
| The sabotage cut in E4's step 2 (the rival's store) and step 3's `Prod` (4.1) | Both inside `MarketProduceAndReprice(FRHMarket& Mk)` (`M:560`), which takes **no sol** | It gains the closing sol, to test the `Disrupted` windows. |
| Thefts and offerings "after the convoy clearing ... and before production" (4.2, R5: also while the market is off) | `RollMarket`'s loop: `ClearConvoys` at `M:539`, then the ring advance, then `if (bLive)` production and repricing at `M:545` | Apply the pending effects between `M:544` and `M:545`, outside the `bLive` gate. |
| Retirement "at the end of each `RollMarket` roll" (1.2) | The roll ends with E4's `RivalEconomies.RemoveAll` (`M:554-558`) | Retire after it. |
| A long jump (R5) | `RollMarket` walks only the **earliest** `min(n, 8)` closed sols, and the last pass clears everything due up to `SolNow - 1` (`M:521-530`) | The windows and the cut are tested per pass. The pending effects follow `DueUpTo`, as convoys do. |
| E5's journal, `CanExchange` and the bloc (2.1, R7) | Not built | Unchanged: E5's names are still design names. |
| E4's politics state (0) | Still HEAD's four sets: `ClosedRoutes` (`H:3930`), `SabotageSols` (`H:3959`), `EmbargoGrace` and `DefectedRivals` (`H:3971-3972`); written in the v19/v21/v22 slots (`W:13641-13659`) and read at `W:14294-14318` | As the design says; the anchors are in the table above. |

## The save now

- **HEAD is v55** (`W:37`), strict. The step-2 fixes take no version.
- **E6 takes the next free version at build time:** v57 if E5 builds first as v56. 7.1's chain (fixes v54, E4 v55, E5 v56, E6 v57) happens to match.
- **The frame exists now.** 7.3's last paragraph ("if the economy-fixes gate lands a shared frame helper first...") has come true. `RHSaveFrame` (`Source/RedHopeSim/Public/RHSaveFrame.h`) has 7.3's exact shape:
  - a 12-byte header (tag, payload bytes, CRC32);
  - an 8-byte footer (bytes, tag);
  - `Read` finds a frame at the reader's position, else by walking the footers back from the end.

  **use instead** of the hand-written frame:
  - add `LeversTag = MakeTag('R','H','L','6')` beside `MarketTag` (the header comment already reserves `'RHL6'` for E6);
  - write with `RHSaveFrame::Write` after the last frame then written: `'RHE5'` if E5 has built, else `'RHE4'` at `W:13866`;
  - read with `RHSaveFrame::Read` after the last tail reader (`W:14660`, or E5's).

  7.3's "if no earlier tail block reported itself lost" clause is not needed: every frame is found by its own tag. `FormatRev` and the payload layout stay as 7.3 has them (plus R5's `EffSol` list and the booking markups).
- **The open sol for 7.3's window clamp** is simply `MarketCheckedSol`. E4's reader already seeds it from `ProductionCheckedSol` when its frame is lost (`M:1383-1386`).
- **Offset tests (7.5).** E1-E4's block tests find their frames by tag (`RHSaveFrame::Find`: EC1/EC6, CR9, XE10, MX9), so "subtracts `GetLeversBlockBytes()`" is not needed, and those checks do not move.
  - MX1 (`RHSimMarketSuite.cpp:548-555`) compares from `'RHE4'` to the end of the file. If E5 has not already bounded it, bound it to the market frame's own length.
  - **The whole-file size lines that move:** CV8, DP11, EC1's two, DC9 (R4), MX1's (`RHSimMarketSuite.cpp:565`) and MX9's round-trip line (`:1359`), plus any `-currency` line E5 adds that prints a file size.

## Battery baseline

- **Battery:** `Saved/baselines/battery_post_e4/` (58 logs), or whatever baseline the gates built before E6 leave. `-levers` adds a run.
- **Every sequence 0 and R4 name holds there**, checked in its logs:
  - `-covert`'s `COVERT sequence: CXCX`;
  - `-espionage`'s `caught=0` then `caught=1`;
  - `-preemptive`'s embargoes at tension 78 and 100 with grace 8, and the defection at relation 5;
  - `-icelow`'s and `-arrivals`' embargo at 75 and defection at 5;
  - `-solidarity`'s closed-route line.
- **Section 0's baseline name, `battery_post_ind`,** is two baselines old. E4 recorded the convoy suites as line-identical, so the sequences carry over.
- **Playtest:** `Saved/baselines/ind_full_playtest.txt` is still valid (E4's playtest was byte-identical). 5.2's re-check at the build's HEAD stands.
- **`-scale`:** E4's `RollMarket` figure is 0.746 ms. E6's budget is "0.2 ms more than E5's measured figure", so it waits on E5.

## Stale or contradicted text

1. **The header.** It says E4 and the economy-fixes gate are designed and not built (also risk 7). Both are built. E5 is not.
2. **Section 0's refusal chain and goodwill anchors:** see the second table. E4 inserted a third early refusal, the convoy count.
3. **Section 0's save paragraph** (`RHSaveVersion = 53`, `W:36`; the reads at `W:14420-14444`) and 7.1's version: see "The save now".
4. **The review's "no `Debug_` hook writes the sets":** no longer true (F1).
5. **R4's MX11 caveat** ("under E6 the cut moves Zarya's prices and stores in that check; any MX11 line that prints a price ... moves on purpose"). MX11 as built sabotages through `Debug_SetSabotage`, not the verb, and prints and checks only refusal texts, a fill, a balance and a relation. With F1's hook (a `Disrupted` rule with no market window), **MX11 does not move.** Withdraw the caveat.
6. **7.3's frame paragraph and 7.5's offset sentence:** see "The save now".
7. **1.3's "who calls it"** list collapses to `PlanConvoy` for the first two entries.

## Blockers and findings

**Blockers: none.** E6's own new hooks (`Debug_EnqueueAt`, `Debug_SetRelation`, `Debug_AddTariff`, `Debug_RemoveRule`, `Debug_SetLeversEnabled`) do not exist yet, as designed. Every E4 hook it reuses does.

**Findings for the main session** (not fixed here):
- **F1. E4's five politics hooks must join the migration.** They write the four sets E6 deletes, and MX11 depends on them. This is a gap in 1.4's table, not a redesign: rebuild them on the rules with Origin `Harness`, as the first table says. With that, `-market` stays line-identical, which is better than R4 expected.
- **F2. The deck's preflight needs a clearing sol to read a pact's cap and a tariff's markup.** The design keys both on `EffSol`. Neither `CanPlaceOrder` nor `CanPlaceStandingOrder` has a command time, so the deck must estimate the sol an order sent now would clear in, from the clock and the uplink lag, and say so. A preflight that disagrees with the booking at a window edge is an acceptance difference, which section 6 already allows. It needs one sentence in 1.3 so the builder does not invent a rule.
