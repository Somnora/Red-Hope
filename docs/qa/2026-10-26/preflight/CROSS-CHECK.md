# CROSS-CHECK: E5, E6, D6e, D7, E7, E8 build briefs against each other and against HEAD d842dd0

This check is read-only. Code was read with `git show d842dd0:` and `git grep d842dd0` only. The baseline logs in `Saved/baselines/` were read for line counts; nothing in the repo was edited, built or run.
Inputs: the six `<gate>-brief.md` files in this folder.
Verified at HEAD: `RHSaveFrame.h` and `RHSaveFrame.cpp` (how frames are found), MX1, MX9 and XE10 (`RHSimMarketSuite.cpp`, `RHSimExportSuite.cpp`), the CV8 and DC9 fixtures, the version literals, the gate-d prefixes, and the version-line counts in the newest baseline, `battery_post_tram`.

**Verdict:** the queue is consistent in its save versions and frame tags. Seven cross-gate issues would turn a gate red or break its "line-identical except named lines" bar if the briefs were built as written (items 1-7 below). None of them needs the director; each is a brief correction.

---

## 0. How frames are found at HEAD (this drives several findings)

From `RHSaveFrame.cpp` at d842dd0:
- `Read` first tries the frame at the outer reader's position (the forward read).
- If that fails, it walks back from the end of the file, footer by footer. The walk **stops at the first footer whose header does not answer it**: a destroyed tag or a wrong length ends the walk, and frames before that point can then be found only by the forward read.
- `Read` moves `Ar` past any frame it finds, even when the CRC then fails. It leaves `Ar` unmoved when no frame is found.
- `git grep -E` on this machine does not honour `\b`: `git grep -c -E 'Ver == 55\b'` finds 0 files, against 10 without `\b` or with `-P`.

---

## 1. The save chain (one table)

| Gate | Version | Frame tag | Written after | Read | Default frame bytes (no history) | Status |
|---|---|---|---|---|---|---|
| HEAD | v55 | 'RHE1'..'RHE4' | - | tail: E1, E2, E3, E4 (W:14739-14743) | - | - |
| E5 | **v56** | 'RHE5' `CurrencyTag` | 'RHE4' (W:13933) | after `ReadMarketBlock`, then `ReconcileReservations` | 258 (payload 238; computed from E5 §5's layout with an empty journal: verify at build) | OK |
| E6 | **v57** | 'RHL6' `LeversTag` | 'RHE5' | after E5's reader | 53 (payload 33) | OK |
| D6e | **v58** | 'RHD6' `TransitPowerTag` | 'RHL6' | after E6's reader | 24, plus 24 per fitted shaft or tram line | OK |
| D7 | **v59** | 'RHD7' `AtriumTag` | 'RHD6' | **early, mid-body**, before `RepairLoadedRobots`; non-moving | 24 | see item 4 |
| E7 | none | none | - | - | 0 | OK |
| E8 | **v60** | 'RHE8' `HistoryTag` | 'RHD7' | after the last tail reader | 241 + regions x 5,086 (with E5) = **5,327** for one region | OK |

- The versions are consecutive and unique in queue order. If D6e and D7 swap, only their two numbers swap; E8 stays v60.
- The tags are unique, and none collides with 'RHE1'-'RHE4'. The write order equals the queue order, so every frame is appended at the end.
- The walk-back cap is 64 steps (`FindBack`), and the file holds at most 9 frames.

---

## 2. The whole-file size lines, step by step (unmasked lines only)

`diff_battery.py` masks only `\(\d+ bytes`. So MX1 x2, MX9, CR9, EC6 and XE10 never show in a diff. The unmasked lines are CV8 (two numbers), DP11 (two numbers), EC1 x2 and DC9.

Assumptions behind the numbers:
- No fixture behind these lines has a lift car, a convoy out, a political act or an atrium. I checked the fixtures at HEAD: CV8's file colony fits no car, DP11 comes before DP12's car, and DC9's `Build(true,1,true)` calls no `Fit`.
- So every frame has its default size in each of these files, and each line moves by the same delta at each gate.

| Line (HEAD value) | after E5 (+258) | after E6 (+53) | after D6e (+24) | after D7 (+24) | after E7 (0) | after E8 (+5,327) |
|---|---|---|---|---|---|---|
| CV8 Z 12059 | 12317 | 12370 | 12394 | 12418 | 12418 | 17745 |
| CV8 W2 12139 | 12397 | 12450 | 12474 | 12498 | 12498 | 17825 |
| DP11 12087 | 12345 | 12398 | 12422 | 12446 | 12446 | 17773 |
| DP11 12119 | 12377 | 12430 | 12454 | 12478 | 12478 | 17805 |
| EC1 era 13100 | 13358 | 13411 | 13435 | 13459 | 13459 | 18786 |
| EC1 agent 13100 | 13358 | 13411 | 13435 | 13459 | 13459 | 18786 |
| DC9 13408 | 13666 | 13719 | 13743 | 13767 | 13767 | 19094 |

The E5 and E8 deltas are computed from the brief layouts. Each gate must check its own delta against its predecessor's baseline, never against the HEAD numbers the briefs quote. CV8's own check (`W2.Num() == Z.Num() + 80`) is relative and holds throughout.

**Briefs that state this wrongly:**
- **D7** says MX1 and MX9 move by +24. They are masked.
- **D6e** leaves DC9's delta UNCERTAIN. It is exactly +24.
- **E8** gives +5,057 or +5,327. E5 is in the queue, so it is **+5,327**.

**Version lines, true count.** `battery_post_tram` has 27 version lines in 15 logs. E5's "about 31" adds 4 false positives:
- `luxury.log` x2 and `rooms.log`: Hope `expect 55.00`;
- `dayshift.log`: `expect 5520`.

These four must NOT move (item 5).

**Suites and runs:** HEAD 57/58; E5 58/59; E6 59/60; D6e 60/61; D7 61/62; E7 62/63; E8 62/63 (E8 adds to `-market`).

**New suites add version lines that later gates must move:**
- CU10 (E5), LV9 (E6), TP5 (D6e) and AT9 (D7) each print a "previous version is refused (/NN)" line.
- E6 moves E5's; D6e moves E5's and E6's; D7 moves those plus D6e's; E8 moves all four suites' lines.

---

## 3. Per-gate tables

### E5 (v56, 'RHE5')
| Check | Result | Detail |
|---|---|---|
| Version and tag | OK | v56 after v55; 'RHE5' after 'RHE4'. |
| Size lines | OK, with a fix | The delta is not stated: it is 258 bytes (CV8, DP11, EC1 x2, DC9). The masked list is right. |
| Battery claim | **WRONG** | The version-line list includes dayshift, luxury and rooms, which are Hope and bank values (item 5). The `expect 55` replacement would rewrite C:331/442/457 and move `rooms.log` and `luxury.log`. |
| Last-frame assertion | **BLOCKS E6** | CU10 asserts "'RHE5' is the last frame". E6 appends 'RHL6', so CU10 turns red (item 1). XE10's fix (compare to 'RHE5''s header offset) is robust for every later gate. MX1's bounded compare is robust too. |
| Names later gates need | GAP | E6 needs the command inside `ResolveDilemma` (E5 leaves it optional). E8 needs `GetCurrencyIssueSol`, `GetCurrencyOpenSol`, `IsMarketInMarks`, `GetRateInputsBp`, `GetDeskBooked`, `GetDeskOrders`, `GetMarkOrder`, a market-block-loss **reason**, and a currency-block-lost flag (items 3, 8, 10). |
| Files | sequential | K, `RHClickTest.cpp`, `RHCheats.cpp`, M, W, H: E6, D7, E7 and E8 edit the same files after it. No parallel collision. |

### E6 (v57, 'RHL6')
| Check | Result | Detail |
|---|---|---|
| Version and tag | OK | v57 after 'RHE5'. Tag 'RHL6' is pre-reserved in the comment on `RHSaveFrame.h:11`. |
| Size lines | OK | +53 on all five unmasked lines. The brief correctly says MX1 and MX9 do not move. |
| Battery claim | **WRONG** | Load repair 11 ("an unknown or cleared convoy Id is dropped", with a Warning) fires on MX9 fixtures where convoy #2 is a waiting, priced MxKes order with a terms entry: `mktmx9r2` (lost block), `mktmx9d5` (rival missing), `mktmx9d6` (untraded line) and `mktmx9d7` (zero line). Each adds an unlisted Warning to `market.log`, and E5's CU10 lost-'RHE4' case adds one to `currency.log` (item 2). |
| E5 assumptions (A1-A10) | mostly OK | A3: E5 must make passing the command mandatory (item 8). A4: E6 writes `CanExchange(Dir, Amount, OutWhy)`, but E5's signature is `(Dir, Amount, EffSol, OutWhy)`. A6: E5 adds `EffSol` with the same rule; E6 reuses it. |
| Wording prefix | **COLLISION** | E6 picks `L`, but `gate-d-wording.md:422/439/455` reserve `L` for D7 (item 7). |
| grep command | **BROKEN** | `git grep -n -E 'Ver(sion)? == 56\b|...'` matches no `Ver ==` site on this machine (item 6). |
| Mutation-14 fixture | OK through E8 | It destroys 'RHE5''s tag. 'RHL6' is still found by the walk while every later frame is intact. |

### D6e (v58, 'RHD6')
| Check | Result | Detail |
|---|---|---|
| Version and tag | OK | Next free version; 'RHD6' is unique. |
| Size lines | OK, with a fix | +24 on all five lines. DC9 is +24 exactly (no car in `Build`). Correctly notes that MX1 and MX9 are masked. |
| Battery claim | OK | transit, trams and deepcommute stay identical (the bank is pinned). The version-line count is 27 plus E5's and E6's. |
| Files | sequential with D7 | `RHSectionVisualizerSubsystem.cpp` (label :127, bar :1464, legend :1600), `gate-d-wording.md` (M6, M8) and `RHClickTest.cpp`. D7 edits the same file in other functions, so D7 must re-anchor after D6e. |
| Reader position | OK | It reads after E6's reader by the forward read. |

### D7 (v59, 'RHD7')
| Check | Result | Detail |
|---|---|---|
| Version and tag | OK | v59; 'RHD7'. |
| Size lines | **WRONG** | It lists MX1 and MX9 as moving (both are masked) and cites MX1 at :565 (it is :564). The real moves are CV8, DP11, EC1 x2 and DC9, each +24. |
| Early read | **HAZARD** | The early, non-moving peek finds 'RHD7' **only by the walk from the end**. A damaged tag or length on any later frame ends the walk. E8's MX15 "a lost 'RHE8'" fixture (the house method destroys the tag, as MS:1383 does) then makes D7 log a lost-frame Warning in `market.log` that E8 does not list. A real file damaged after 'RHD7' would also silently lose its atriums. And because nothing consumes 'RHD7' at the tail, E8's reader can never use the forward read (item 4). |
| Baseline name | stale | It says the newest baseline is `battery_post_s2fix`; it is `battery_post_tram`, or what the visual pass leaves. |
| UNCERTAIN items | resolved | Neither E5 nor E6 touches `GrowthBillFits` or `TakeGrowthResource`. E5 does not re-cut the playtest baseline. |
| Prefix | OK | `L`, reserved for D7 since D8. E6 must move off it. |

### E7 (no version, no frame)
| Check | Result | Detail |
|---|---|---|
| Version and tag | OK | None. It moves no size or version line. |
| Battery claim | OK | It diffs 0 lines against D7's baseline, plus `regionready.log`. |
| Names E8 needs | **MATCH** | `GetMarketFloorMcr`, `GetMarketCeilMcr`, `GetConvoyLegSols`, `GetMarketRegionIdAt`, `GetRivalRegionId`, `GetRoutes`, `GetRouteFor`, `GetColonyRegionId` and `GetColonyDepotLocationCm` are declared with E8's names. `GetMarketRegionCount` exists at HEAD (H:1581). |
| On E6 | OK | It keeps `Debug_ClearPolitics` (E6 rebuilds it under the same name). The sabotage cut is computed locally (E6 T2), so it composes with the M:214 permille. |
| grep command | BROKEN | `git grep -E '== 55\b|"/55"'` finds only the "/55" sites (item 6). E7 edits no literal, so this is informational only. |
| Orchestrator rulings | open | S1 (the `Debug_SetEndowment` override) and S2 (the case-sensitive check). |

### E8 (v60, 'RHE8')
| Check | Result | Detail |
|---|---|---|
| Version and tag | OK | v60; 'RHE8' last. |
| Size lines | OK, with a fix | Commit to +5,327, because E5 is built. |
| Battery claim | **INCOMPLETE** | (a) E5's CU10 lost-'RHE4' case in `currency.log` adds E8's "dropped: its market block was lost" Warning (x2). (b) CU10's lost-'RHE5' case on an issued colony adds the "unit of 1 reset" Warning(s). (c) The mktmx9d4 text depends on E5 exposing *why* the block was reset: E5 sets one `bMarketBlockLost` for lost, malformed, slot-reset and repeated-region alike, so E8 would print "was lost" where it predicts "slot table was reset" (item 3). (d) D7's walk-only read adds a line at MX15 unless item 4 is fixed. |
| E5 names | GAP | Several are optional or unnamed in E5's brief (item 10). |
| E6 names | MATCH | `GetLineMarkupBp`, `HasLivePact`, `GetPactCargoKg`. The convoy-terms read is unnamed in E6; name it `GetConvoyTerms`. |
| Values in the ring | needs a line | Under E6, `HistSoldValue` and `HistBoughtValue` must be the pre-markup values, like E4's tally, E5's bloc tally and E6 D21. |
| Price write sites | add one | E5's withdrawal re-expression writes every price. E8's `HistOpen` runs after it, so the code is right, but the list in brief §3 omits it. |
| Click-test total | stale | "103 + E5-E7" leaves out D7's 3 steps. The expected total is 91 + E5 (0-2) + E6 (0-1) + D7 (3) + E8 (12) + the visual pass: 106-109 + VP. |
| grep command | BROKEN | `'== 5[5-9]\b'` finds no `Ver ==` site (item 6). |
| Wording prefix | unset | "E4's W-row style" names no free prefix (item 7). |

---

## 4. Accessor and name alignment (later gate → earlier brief)

| Used by | Name as the later brief writes it | Earlier brief has | Action |
|---|---|---|---|
| E6 A3 | the command passed into `ResolveDilemma` / `ResolvePacify` | E5: "either form acceptable" | **E5: pass `const FRHCommand&` (mandatory)** |
| E6 A4 | `CanExchange(Dir, Amount, OutWhy)` | E5: `CanExchange(Dir, Amount, EffSol, OutWhy)` | E6: use E5's signature |
| E6 A6 | `PlanConvoy(..., EffSol)` and the preflight estimate | E5: `EffSol`, the same formula | E5: compute `EffSol` after the M:1051 catch-up (as E6 D12 does) |
| E6 A7 | `BlocStance`, `Debug_SyncPoliticalView`, `GetCurrencyBlockBytes` | E5: all declared | OK |
| E8 | `IsCurrencyIssued`, `GetRate`, `GetBlocMembers` | E5: declared | OK |
| E8 | `GetCurrencyIssueSol`, `GetCurrencyOpenSol`, `IsMarketInMarks` | E5: optional | **E5: make them mandatory with these names** (E8 then drops its own `IsMarketInMarks`) |
| E8 | `GetRateInputsBp`, `GetDeskBooked`, `GetDeskOrders`, `GetMarkOrder` | E5: "last TradeBp and step sign, desk booked totals, desk list" unnamed | **E5: declare them under E8's names** |
| E8 | `bMarketBlockLost` with lost vs slot-reset | E5: one bool, set in 3 branches | **E5: add a reason (`None`, `Lost`, `Malformed`, `SlotsReset`, `RegionsRepeated`)** |
| E8 (f) | "E5's block was lost" | E5: none | **E5: add a transient `bCurrencyBlockLost`** |
| E8 | `GetLineMarkupBp`, `HasLivePact`, `GetPactCargoKg` | E6: declared | OK |
| E8 | a convoy's terms (the "25% tariff" chip) | E6: "a const read", unnamed | E6: name it `GetConvoyTerms(int32, FRHConvoyTerms&)` |
| E8 | the E7 getters (R3-4) | E7: exact names | OK |
| E7 S13 | the shape of E6's hooks | E6 D30 | OK |
| D7 | `GrowthBillFits` / `TakeGrowthResource` unchanged | E5 and E6 edit neither | OK |

---

## 5. File collisions and ordering hazards

The gates are strictly sequential, so there is no merge race. The hazards are re-anchoring and the semantic order within shared functions.

| File or function | Gates | Hazard and resolution |
|---|---|---|
| `RollMarket` per-pass order | E5, E6, E8 | Agreed order per pass: E8 `HistEnsure`; zero the tally; `ClearConvoys` (E6 markups; E8 values, pre-markup); E5 `ClearDeskOrders`; ring advance; E6 pending effects; `if (bLive)` reprice (E6 cut, computed locally); E5 record, transitions, admissions and rate (outside `bLive`); E8 `HistOpen`. After the loop: E6 retirement. Consistent across the three briefs. |
| `PlanConvoy` refusals | E5, E6 | E6 replaces M:887-905 with the walk. **E5's `bWithdrawPending` refusal must stay straight after the walk's block.** E6 should say so. |
| Load tail order | E5, E6, D6e, D7, E8 | E4 → E5 + `ReconcileReservations` → E6 → D6e → (D7 at the tail; see item 4) → E8. The forward read works only if each reader consumes its frame. D7 currently does not. |
| `ResolveCovert` | E6, E7 | E6 rewrites it; E7 replaces its Lander loop. E7 already anchors by snippet. |
| `RHSectionVisualizerSubsystem.cpp` | VP, D6e, D7 | VP → D6e (label, bar, legend) → D7 (`EmitCell`, key, `Pick`, `LocatePlate`). Re-anchor at each step. |
| V (`RHColonyVisualizerSubsystem.cpp`) | VP, D7, E7, E8 | E7 replaces the Home loop; E8 assumes E7's form. OK. |
| K, `RHCheats.cpp`, `RHClickTest.cpp` | VP, E5, E6, D6e, D7, E7, E8 | Re-anchor by symbol at each gate. Click-step names do not collide. |
| `RH_Config.csv` / `DT_Config` | E5, E6, D6e, D7 | Each sync must take the whole CSV, one Unreal process at a time. E5 also changes `RH_Rivals.csv` (`BlocStance` after `StoreCap`); E7 adds `DT_Endowments`. |
| `rh_battery.sh` SUITES and counts | E5, E6, D6e, D7, E7 | Use the counts in section 2. Check `-transit` against `-transitpower` in `FParse::Param` (D6e noted it). |
| `gate-d-wording.md` prefixes | E5 Y, E6 **L (conflict)**, D6e M6/M8, D7 L, E8 unset | Free at HEAD: A, B, C, E, O, Y, Z (L is reserved for D7). Proposed: E5 Y, E6 Z, D7 L, E8 C. |
| Baseline names | all | Newest at HEAD is `battery_post_tram` (E5 has it right; D7 and E8 cite s2fix). Each gate diffs against its predecessor's snapshot. |

---

## 6. Required brief corrections (numbered; items 1-7 would break a gate)

1. **E5 (blocks E6).** CU10's "'RHE5' is the last frame" breaks when E6 appends 'RHL6'.
   - Fix: assert that 'RHE5''s header sits straight after 'RHE4''s footer, with payload == `GetCurrencyBlockBytes()`, not that it ends the file. This is the same shape as E5's own XE10 fix.
   - Every later frame check must follow the rule "follows its predecessor", never "ends the file". E8's MX15 "'RHE8' is the last frame" is safe today, but should use the same bounded form.
2. **E6 (blocks E6's battery bar).** Load repair 11 must drop a terms entry **silently** when its convoy is absent because E4's loader dropped it (lost or malformed block, or a convoy repair that already warned). Keep the Warning only for a terms entry whose convoy loaded but disagrees (`LineCount`, `EffSol`).
   - Without this, `market.log` gains unlisted Warnings at `mktmx9r2`, `d5`, `d6` and `d7`, and `currency.log` gains one at CU10's lost-'RHE4' case.
   - Apply the same rule to E5's "mark order naming a convoy E4 did not load".
3. **E5 and E8 (blocks E8's predicted lines).**
   - E5 exposes a market-block loss reason (`None`, `Lost`, `Malformed`, `SlotsReset`, `RegionsRepeated`) alongside, or instead of, the bool, plus a transient `bCurrencyBlockLost`.
   - E8 maps `Lost`/`Malformed` to "its market block was lost" and `SlotsReset`/`RegionsRepeated` to "its market block's slot table was reset". It uses `bCurrencyBlockLost` for rule (f).
   - E8's section 4.3 fallback ("if E5 did not add it") is moot: delete it.
4. **D7 (blocks E8's MX15 line set; also a robustness gap).**
   - Keep the early non-moving peek for `RepairLoadedRobots`. **Also read 'RHD7' at the tail position**, after D6e's reader, with the normal forward read: this consumes the frame and is the fallback.
   - If the peek failed and the tail read succeeds, install the atriums there and run the open-cell robot remap (Resolve's BFS) then. Warn "lost" only if both reads fail.
   - E8 then lists no D7 line. Alternatively E8's MX15 could break 'RHE8' by CRC rather than by tag, but the D7 fix is the right one.
5. **E5 (would move rooms.log and luxury.log).**
   - Replace `expect 55` with the literal `(expect 55)`. Plain `expect 55` also matches C:331, :442 and :457, which are Hope texts `expect 55.00`.
   - Remove dayshift, luxury and rooms from the moved-lines list: the true count is 27 lines in 15 logs.
   - E5 says "15 `Ver == 55` sites": there are 14.
6. **E6, E7 and E8 (grep commands).** `git grep -E` does not honour `\b` here. Use `git grep -n -P 'Ver == 56\b|"/56"|\(expect 56\)'`, or drop the `\b`. As written, E6's and E8's commands miss every `Ver ==` site.
7. **E6 (wording prefix).** `L` is reserved for D7 (`gate-d-wording.md:422`). Use `Z`, or the next free prefix after E5's `Y`. **E8** must name a free prefix (proposed: `C`).
8. **E5.** Make passing `const FRHCommand&` into `ResolveDilemma` and `ResolvePacify` mandatory (E6 A3 needs `c` inside `ResolveDilemma`). Compute `EffSol` in `BookConvoy` after the M:1051-1054 catch-up.
9. **E6.** Change the A4 signature to E5's `CanExchange(Dir, Amount, EffSol, OutWhy)`. State that E5's withdrawal refusal stays directly after the walk's block in `PlanConvoy`. Name the terms read `GetConvoyTerms`. Optional: write the effect `CommandId` as int32 to match E5's journal, or note that the widening is intentional.
10. **E5.** Declare as mandatory, under E8's names: `GetCurrencyIssueSol`, `GetCurrencyOpenSol`, `IsMarketInMarks`, `GetRateInputsBp`, `GetDeskBooked`, `GetDeskOrders` and `GetMarkOrder`. E8 then rebases nothing.
11. **E8.** Add to the expected moves:
    - `currency.log` CU10's lost-'RHE4' E8 Warning (x2);
    - CU10's lost-'RHE5' unit-reset Warning(s) (x2 each);
    - the version lines of CU10, LV9, TP5 and AT9.

    Grep the pre-E8 baseline for every lost-'RHE4' and slot-reset load in `currency.log`, `levers.log`, `transitpower.log` and `atriums.log`.
12. **E8.** State that history values are pre-markup (E6). Add E5's withdrawal re-expression to the list of price write sites. Commit the frame delta to +5,327. Recount the click-test total including D7's 3 steps.
13. **D7.**
    - Size-line list: drop MX1 and MX9 (masked); MX1 is at :564.
    - Baseline: the newest is `battery_post_tram`, or the visual pass's snapshot.
    - The two UNCERTAIN items (growth bill, playtest baseline) are resolved: no change from E5 or E6.
14. **D6e.** DC9's delta is exactly +24: DC9's `Build` fits no car. Remove the UNCERTAIN.
15. **All new suites (E5 CU, E6 LV, D6e TP, D7 AT).**
    - Print whole-file sizes only in the masked `(N bytes` form, or not at all. Any unmasked size line in a new suite moves at every later gate.
    - Each gate's version-line recount must include the earlier new suites' "/NN" lines (section 2).
16. **E6.** Its battery section should name `currency.log` among the logs that must stay line-identical apart from version lines and `levers.log`. That holds only once item 2 is fixed.
17. **Informational.** E8's route-chip order (defected > closed > embargo > disrupted) follows the card. E6's refusal precedence is Closed < Disrupted < Severed < Embargo, so a rival that is both closed and defected shows "defected" on the chip and refuses with "closed". This is a UI consistency note only, not a gate issue.

---

## 7. Rulings needed

**Orchestrator, not the director:**
- **D7/D6e F1.** Confirm the framed tail block that repairs (option (i)) for both.
- **E7 S1 and S2.** The `Debug_SetEndowment` override layer, and the case-sensitive resource check (or strike mutation 8).
- The gate-d prefix assignments (item 7).
- Items 1-4 change E5, E6, D7 and E8 tests and loaders. Adopt them in the briefs before E5 starts, because E5's CU10 and the loss reason are cheapest to get right at E5.

**Director:** none new. D6e's conditional question stands: only if the `lifts=1` playtest fails at the 200 W placeholder draw, put lettered options to the director.
