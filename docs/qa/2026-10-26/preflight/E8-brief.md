# E8 BUILD BRIEF: the market panel, the 30-sol history (saved), the Routes overlay

Preflight, read-only. Code was read at HEAD `d842dd0` with `git show d842dd0:` only. Nothing was built or run.
Spec: `docs/qa/2026-10-25/e8-market-panel-design.md`. Authority inside it: DIRECTOR ANSWER > REVISION 3 (final) > RE-CHECK > REVISION 2 > INDEPENDENT REVIEW > REVISION > LEAD REVIEW > design. REVISION 3's closing section, "What the builder builds", is the summary to build to. This brief adds what changed since then and settles the contradictions it found (section 11).

**Build position.** E8 is the last item in the queue. It comes after the visual pass (still uncommitted at `d842dd0`), then E5 (v56), E6 (v57), D6e and D7 (v58 and v59, in either order), then E7 (no save version). Every anchor below is at `d842dd0`. Most files E8 touches will have moved again by build time:
- E5, E6, D6e, D7 and E7 each edit the sim files: H, M, W, MX.
- E7 edits V and K:2551.
- The visual pass's uncommitted work touches V, Vh, `RHCheats.cpp` and `RHClickTest.cpp`.

Find every anchor by its symbol (`git grep`); do not trust the line number. The line numbers show where each thing was at `d842dd0`.

Short names (the spec's):
- **H** = `Source/RedHopeSim/Public/RHSimWorldSubsystem.h`
- **M** = `Source/RedHopeSim/Private/RHSimMarket.cpp`
- **W** = `Source/RedHopeSim/Private/RHSimWorldSubsystem.cpp`
- **MX** = `Source/RedHopeSim/Private/RHSimMarketSuite.cpp`
- **K** = `Source/RedHope/Private/RHCommandDeckPanels.cpp`
- **D** = `Source/RedHope/Private/RHCommandDeck.cpp`
- **Dh** = `Source/RedHope/Public/RHCommandDeck.h`
- **V** = `Source/RedHope/Private/RHColonyVisualizerSubsystem.cpp`
- **Vh** = `Source/RedHope/Public/RHColonyVisualizerSubsystem.h`
- **T** = `Source/RedHope/Private/RHTrafficVisualizerSubsystem.cpp`
- **CT** = `Source/RedHope/Private/RHClickTest.cpp`

---

## 1. Authoritative decisions (one list)

1. **Q-A (b), director.** The 30-sol history lives in the SIM: saved, write-only, in its own `RHSaveFrame` frame `'RHE8'`, and checked against E4's state on load. No sim rule reads it.
2. **Q-C (a), director.** MARKET joins the accordion as the sixth `EFold`. Opening it folds Politics, Crew, City, Fleet and Power. A look-and-act costs n + 4 presses (accepted).
3. **Q-B is settled.** The panel is a fold in the right column, not a separate screen. Each price row has two lines: first the icon, the short label and the price; then a 30-glyph sparkline and the 5-sol change.
4. **Save version: the next free one at build time.** That is **v60** if E5 v56, E6 v57 and D6e/D7 v58/v59 have all landed. E7 takes no version: its `'RHE7'` block was dropped (E7 REVISION 3 section 1). The frame `'RHE8'` goes after the last frame the build's HEAD writes. Its reader runs after the last tail reader (E4's, then E5's and E6's).
5. **E4's `bKeep` (M:286) stays byte-for-byte unchanged.** The ring is copied under a separate test, `bKeepHist`. A ring is always sized, never absent (`HistInitEmpty`) (R3-1).
6. **The newest ring entry is the open sol.** After any roll, `HistLastSol == MarketCheckedSol`. An empty ring (fill 0, last sol −1) is valid at any open sol. `HistEnsure` starts an empty ring at s, never at 0..s (R3-2).
7. **Discovery gates everything player-facing.** A slot is shown when an available economy rival in the shown region lists it. That is 5 rows on a new colony and 7 after Meridian is discovered (R2-1). Console output is not gated.
8. **Units (after E5).** Each sol is converted at its own sol's rate, with parity before the issue. The credit view shows a step of up to about 4% at the open sol; label it ("marks from sol N", "(unit changed)") and never smooth it (R3-5).
9. **No band or leg getter of E8's own.** Use E7's: `GetMarketFloorMcr`, `GetMarketCeilMcr`, `GetConvoyLegSols`, `GetMarketRegionIdAt`, `GetRivalRegionId`, `GetRoutes` / `GetRouteFor`, `GetColonyRegionId`, `GetColonyDepotLocationCm` (R3-4).
10. **The overlay button is "Routes" / "Routes [on]"**, tagged `RH.TradeOverlay`, and always located by tag ("Trade" would collide with the card's TRADE verb). The console command is `RH.Trade overlay [0|1]` (R2-9 supersedes section 4's `RH.TradeOverlay`).
11. **The overlay's visibility is V's marker predicate.** Add `ShowsSurfaceLevel() { return ShowsLevel(0); }` and keep `ShowsSurface` private and unused by E8 (R3-6).
12. **`Debug_SetMarketPrice` also writes the open sol's ring entry**, so every harness save keeps the invariant (R3-3).
13. **No E8 log text may contain "the market block is lost".** MX:1391 and MX:1430 count that substring exactly. Every E8 Warning begins "LoadColony v<N>: the market history ..." (R3-3).
14. **The frame bar is relative.** In adjacent P0/P1 pairs, P1's median and p95 are each within 1.0 ms of P0's. The `Market` slot costs at most 0.2 ms a frame and at most 2 ms a rebuild. A missed budget is a finding for the director, not a budget edit (R2-7).
15. **Already-settled deviations to log** (R3's list of 10): v<N> against the plan's "no save change"; the open-sol newest entry; `bKeepHist`; `Debug_SetMarketPrice` writes the ring; E7's getters; `market.BUY` fixed; an 8-sol balance; the Routes name; the region selector not click-tested; the unit step labelled.

---

## 2. Re-anchored citations (HEAD `d842dd0`)

**Unmoved.** No `Source/RedHopeSim` file and no `docs/data` file changed between the spec's last read (`798a549`) and `d842dd0`. Every H, M, W, MX, `RHSaveFrame.h` and `RHSimExportSuite.cpp` anchor in REVISION 2 and 3 still holds at `d842dd0`. Spot-checked:
- H:1519 `FormatCredits`; H:1569 `MarketRing`; H:1571-1612, the market getters and hooks (`Debug_SetMarketPrice` H:1610, `Debug_RollMarket` H:1612, `Debug_SetEmbargo` H:1618, `Debug_ClearPolitics` H:1621).
- H:1823 `IsRivalAvailable`; H:2739 `Debug_MaybeDiscoverSettlement`; H:2777 `private:`; H:3850-3860 `FRHMarket`; H:4045 `ConvoyLegSolsFor`.
- M:23 `MktMaxList`; M:97; M:286 `bKeep`; M:509-558 `RollMarket` (`Passed` at 522, `ClearConvoys` at 539, reprice at 545-550); M:639-702 `ClearConvoy` (values at 668 and 676); M:1280-1346 `WriteMarketPayload`; M:1362 `ReadMarketBlock` (lost Warning at 1385); M:1521; M:1526-1529; M:1668; M:1679-1691; M:1693-1701.
- W:37 `RHSaveVersion`; W:13928-13933 the frame writes; W:13968-13971 the strict check; W:14743 `ReadMarketBlock`.
- `RHSaveFrame.h:19-24`; `RHSimExportSuite.cpp:530-531` (XE10's end-of-file clause).
- MX:267 `Inject(..., Region)`; MX:1316 `Ver == 55`; MX:1391 and :1430 (the exact counts); MX:1663-1925, the MX8 block (the start save `marketmx8` at :1693; `RetireAll()` at about :1923).

**Moved in RedHope** (by the P8c ladder `870ba2c`, the tram `f922b3d` and the CT additions):

| Spec cite | What | HEAD `d842dd0` |
|---|---|---|
| D:38 | `RightColumnPx = 340` | D:39 |
| D:47-59 | `DeckButton` (gains an optional tag) | D:48-60 |
| D:777-784 / :782 | TRAFFIC legend slot (inside `TopLeftStack`, D:768) | D:787-794 / :792 |
| D:866 | right column scroll bar 8 px | D:876 |
| D:885 | `FoldAnchor(EFold::Politics, ...)` | D:895 (Crew 899, City 903, Fleet 907, Power 911) |
| D:1177-1182 | Traffic command-bar button | D:1187-1192 |
| D:1190-1197 | speed buttons | D:1203-1207 (lambda D:575) |
| D:1307 / 1335-1343 | `SRHCommandDeck::Tick` / `RefreshTramList()` call | D:1317 / D:1351 |
| D:1533-1566 | `GetPowerChartText` (U+2581 at 1557) | D:1543-1576 (U+2581 at 1567) |
| Dh:322 | `OrderDrafts` | Dh:327 |
| Dh:374 | `FoldAnchors[(int32)EFold::Count]` | Dh:379 (`FoldScrollTarget` Dh:380) |
| Dh:386-387 | fold bools | Dh:391-392 (`bPoliticsExpanded`, `bPowerExpanded`); Crew/City/Fleet at Dh:364-366 |
| Dh:136 / :138 / :29 | `EFold` / `IsFoldOpen` / `Debug_ExpandAll` | unchanged: Dh:136 / :138 / :29 |
| K:227-237, default K:235 | `FoldFlag` | K:228-238, default K:236 |
| K:239-281 | `FoldToggle` / `FoldAnchor` / `SetFoldOpen` | K:240-256 / 258-262 / 264-282 |
| K:2353 (review K:2343) | card availability lambda | K:2360 |
| K:2362-2365 | card status chain (defected, closed, embargo, disrupted) | K:2378-2383 |
| K:2394 | TRADE verb | K:2401 |
| K:2448-2457 | EARTH & NEIGHBORS visibility | K:2455-2464 |
| K:2507-2627 | `BuildMarketCard` | K:2524-2634 |
| K:2522 | card name sort (`Caps.Sort`) | K:2529 |
| K:2564 / 2571 / 2613 / 2627 | Buy +100 / Sell +100 / SEND ORDER / CLEAR | K:2571 / 2578 / 2620 / 2634 |
| K:2703 | Power's `FoldToggle` | K:2710 |
| K:2544 (E7's region read) | the card's price read | K:2551 |
| CT:1313 | `StartClickTest` | CT:1499 |
| CT:1362 | `sweep.deck` | CT:1548 |
| CT:1546 / 1554 | `market.setup` / `Debug_GrantCredits(10000000)` | CT:1732 / 1740 |
| CT:1562-1563 | `market.BUY` empty check | CT:1748-1749 |
| CT:1564 | `market.SEND` | CT:1750 |
| CT:1574-1575 | `traffic.on` | CT:1760 |
| CT:1584 / 1589 | `city.setup` / its `SetFoldOpen` | CT:1770 / 1775 |
| CT:110-141 | `Gather` | CT:112 |
| CT:174 | `Equals(..., IgnoreCase)` | CT:176 |
| CT:181-186 / 191 | `HasTag` | CT:193 |
| CT:558 | `LocateButton` | CT:560 |
| CT:900-928 | `PushCheck` | CT:902 |
| CT:930 | `PushSweep` | CT:932 |
| CT:1271 | `CheckDeckContrast` (`Luminance` CT:1419, `MinContrast` 0.4) | CT:1457 |
| CT:2294 | `// ---- the verdict` | CT:2702 (`deck.contrast` CT:2700) |
| `RHCheats.cpp:1059` | `RH.Trade` | :1060 |
| `RHCheats.cpp:1090` | `RH.Market` | :1091 |
| `RHCheats.cpp:1719` / `:1747` | `RH.CityBench` / `RH.DeepBench` | :1720 / :1748 |
| `RHCheats.cpp:1937` | `RH.Section` | :1976 |
| `RHCheats.cpp:2081` | `[FrameStats]` main line | :2120 (command at :2128) |
| `RHCheats.cpp:2119` / `:2141` | the over-150 ms spike lines | :2161 / :2180 |
| `RHCheats.cpp:2220` | `RH.Floor` | :2259 |
| `RHCheats.cpp:2749` | `RH.Traffic` | :2788 |
| `RHCheats.cpp:981` / `:991` | `RH.InjectRivals` `RegionId = 0` | :982 / :992 |
| `RHPlayerController.cpp:2188` / `:2196` | `ToggleTrafficOverlay` / `IsTrafficOverlayOn` | :2189 / :2197 |
| `RHSectionVisualizerSubsystem.cpp:844` / `:622` | `WorstRebuildMs` / printed | :881 / :658-659 |

**Unmoved in RedHope:**
- V and Vh: V:4193-4204 `RivalMarkerPos`; V:4206 `RefreshSovereigntyVisuals`; V:4216 `bSurface = ShowsLevel(0)`; V:4223-4225; V:4243 `AddLabel`; V:4247-4263; V:4264; V:4338-4342 (the Lander rule); V:4361; Vh:216 `private:`; Vh:234-238; Vh:259.
- T: T:20 `OverlaySampleCm = 200` (2 m; E8's ribbon uses its own 10 m); T:22 `PollSeconds`; T:538-627 `RebuildOverlay`.
- `RHFrameStats.h:11` `SlotCount = 3`; `RHDeckStyle.cpp:161`; `RHDeckStyle.h:25-34`.
- **Note:** the visual pass's uncommitted edits touch V and Vh, so these will move.

**No longer true:**
- **Section 0's "no `OnPaint`, `FSlateDrawElement` or `MakeLines` anywhere in `Source/RedHope`".** P8c's `SRHSectionLadder` is a custom-painted leaf (`RHSectionLadder.cpp:281-321`, `RHSectionLadder.h:33`). This changes nothing for E8, which stays text glyphs under the settled Q-B.
- **"86 click-test steps".** At HEAD the count is **91** (`f922b3d`: "PASS 91/91" at 1440x860; P8c made it 90).

---

## 3. Assumptions checked against HEAD (the code, not the doc)

**Hold:**
- `FRHMarket` (H:3850) holds `RegionId`, `PriceMcr`, the 8-sol `RingSoldG` / `RingBoughtG` with fill and head (saved), and the derived `bTraded`, `VMcr`, `FloorMcr` and `CeilMcr` (private). The `Hist*` fields go here.
- `RollMarket` (M:509) runs one pass per closing sol: zero the tally entry, `ClearConvoys(ClosingSol, DueUpTo)`, advance the ring, then `MarketProduceAndReprice` only if `bLive`. On a jump of more than 8 sols, the last pass catches every due convoy. R2-2's three write points fit this loop.
- `ClearConvoy` is reached only from `ClearConvoys` (M:634). `RollMarket` is called from `RHSimGrowth.cpp:489`, M:1053 and `Debug_RollMarket`.
- Prices are written only at M:304 and M:312 (the resync), M:487 (`Debug_SetMarketPrice`), M:623 (the reprice), M:1536 (the load build) and M:1688 (the load clamp). So R3-3's hook rule plus R2-6 4(e) cover every way a price can diverge from the ring's open-sol entry.
- `WriteMarketPayload` (M:1280) writes the slot table as `int32` plus `FString` names, then each region's ring in raw entry order. `WriteMarketBlock` syncs first, and `SaveColony` syncs at W:13932. E8's writer does the same.
- `RHSaveFrame::Write(Ar, Tag, TFunctionRef<void(FArchive&)>)`; `Read(Ar, File, Tag, OutPayload, OutWhy)` finds the frame at the reader's position or by the walk back from the end; `HeaderBytes` 12, `FooterBytes` 8.
- Today's last frame is `MarketTag` (W:13933). `ReadMarketBlock` is the last tail reader (W:14743). After it, the clock is set and speed goes to 1x; nothing there resyncs the market.
- `IsRivalAvailable` = `SliceActive` or discovered.
- Meridian ships `SliceActive` FALSE and trades `SpareParts` and `Food`. Zarya trades Ice, Water, Oxygen, Struct and Hydrogen. The card's `Caps.Sort` puts Hydrogen first, so `market.BUY`'s first "Buy +100" is Zarya's Hydrogen.
- `MaybeDiscoverSettlement` is deterministic (a hash of the survey count, U < 0.5). It broadcasts `OnAlert`, which V turns into a banner, not a modal.
- `diff_battery.py` masks only `\(\d+ bytes`. So CV8's "(12059 -> 12139 bytes)", DP11's "(12087 -> ...", EC1's "both saves are 13100 bytes" and DC9's "13408-byte" are not masked. MX1, MX9, CR9, EC6 and XE10's "(N bytes" are masked.
- The baseline `Saved/baselines/battery_post_s2fix` has the **27** version lines R2-2 lists. E4's "LoadColony v55:" Warnings appear **only in `market.log`** (four kinds plus the band clamps), so at HEAD R3-3's MX9 list of new E8 Warnings is complete.
- No script parses the `[FrameStats]` line (`git grep FrameStats scripts` is empty), so adding fields is safe.
- `SetFoldOpen` loops to `EFold::Count`, and `FoldScrollTarget == EFold::Count` means "none". Adding `Market` before `Count` keeps both working. `FoldFlag`'s `default:` returns Power's flag, so Market needs its own case.

**Changed or contradicted:** see section 11.

**UNCERTAIN (cannot be checked until those gates land):**
- E5's names: `IsCurrencyIssued`, `GetCurrencyIssueSol`, `GetCurrencyOpenSol`, `GetRate`, `GetRateInputsBp`, `GetDeskBooked`, `GetDeskOrders`, `GetMarkOrder`, `GetBlocMembers`, `bMarketBlockLost`, `CurrencyTag`.
- E6's names: `GetLineMarkupBp`, `HasLivePact`, `GetPactCargoKg`.
- E7's names: all of R3-4.
- Rebase each one onto the gate as built. List every rebase in the commit.

---

## 4. Exact changes per file (anchors at HEAD; find each by symbol at build time)

### 4.1 Sim: `RHSaveFrame.h`
- Add `constexpr uint32 HistoryTag = MakeTag('R','H','E','8'); // E8, the market history (v<N>)` beside `MarketTag` (`:22`), after E5's `CurrencyTag` and E6's `LeversTag`.
- Extend the header comment's tag list.

### 4.2 Sim: H
**Public section, beside the E4 market getters (H:1571-1612):**
- `static constexpr int32 MarketHistSols = 30;`
- `int32 GetMarketHistoryFill(int32 RegionId = 0) const;`
- `int32 GetMarketHistoryLastSol(int32 RegionId = 0) const;` (−1 when empty)
- `bool GetMarketHistory(FName Res, TArray<int64>& OutPrice, int32 RegionId = 0) const;`
  - `OutPrice[k]` is the price during sol `LastSol - (Fill - 1) + k`; −1 means unknown.
  - Returns false when there is no such market or slot.
- `bool GetMarketHistoryValue(int32 Back, int64& OutSold, int64& OutBought, int32 RegionId = 0) const;`
  - Back 0 is the open sol (always 0, 0); Back 1 is the sol just closed.
  - Returns false outside 0..Fill−1.
- `int64 GetMarketHistoryRateMcr(int32 Back, int32 RegionId = 0) const;` and `uint8 GetMarketHistoryUnit(int32 Back, int32 RegionId = 0) const;` (only if E5 is built)
- `bool IsMarketInMarks() const;` (only if E5 is built): `CurrencyIssueSol >= 0 && MarketCheckedSol >= CurrencyOpenSol`, by E5's names as built.
- `int32 GetMarketHistoryBlockBytes() const;`
- `void Debug_SetMarketHistoryEnabled(bool bOn);`
  - Runtime only, never saved. List it with E4's harness hooks (H:1606).
  - **Switching it off empties every ring (`HistInitEmpty`)**; see section 11 problem B1.

**Inside `FRHMarket` (H:3850, saved part):**
- `TArray<int64> HistPrice;` (slot × 30 + entry; −1 = unknown)
- `TArray<int64> HistSoldValue, HistBoughtValue;` (30 each)
- If E5 is built: `TArray<int64> HistRateMcr; TArray<uint8> HistUnit;` (30 each)
- `int32 HistFill = 0, HistHead = 0, HistLastSol = -1;`
- The comment says "SAVED (v<N>, 'RHE8'); written by RollMarket, Debug_SetMarketPrice and the load repairs only; no sim rule reads it".

**Private members:**
- `bool bMarketHistoryEnabled = true;` (runtime)
- Helper declarations: `HistInitEmpty`, `HistEnsure`, `HistOpen`, `WriteMarketHistoryBlock`, `ReadMarketHistoryBlock`
- If E5 did not add it: the transient `bool bMarketBlockLost` (R3-7).

**Do NOT add** `GetMarketBandMcr` or `GetConvoyLegSols` (E7's).

### 4.3 Sim: M
**`BuildMarketState` (M:274-335).** M:286 stays exactly as it is. Add after it:
```
const bool bKeepHist = bKeep && OldMk->HistPrice.Num() == N * MarketHistSols && OldMk->HistSoldValue.Num() == MarketHistSols && OldMk->HistBoughtValue.Num() == MarketHistSols /* && E5's two arrays == 30 */;
```
- In the `bKeep` branch (M:302-309), after the five copies: if `bKeepHist`, copy every `Hist*` field; else `HistInitEmpty(Mk, N)`.
- The `else` branch (M:310-315) calls `HistInitEmpty(Mk, N)` as well.

**`RollMarket` (M:509-558).** Skip all three write points while `!bMarketHistoryEnabled`. Per market `Mk`, in pass i closing sol s = First + i:
1. **Top of the pass, before M:530's tally zeroing:** `HistEnsure(Mk, s)`.
   - If fill is 0, append the one entry for s: the live `PriceMcr` per slot, plus E5's live unit and rate.
   - Else if `HistLastSol < s`, append `HistLastSol + 1 .. s − 1` as unknown (−1 price, 0 values, 0 rate, 0 unit; append at most 30), then s live.
   - Else do nothing.
2. **In `ClearConvoy` (M:639-702),** where `SellsValue` and `BuysValue` are final (after M:676, before M:680):
   - Add them into the clearing market's `HistSoldValue` / `HistBoughtValue` at the entry for `ClosingSol` (that is the sol `HistEnsure` just made).
   - `ClearConvoy` must find the convoy's market (the rival's region) and its entry by `(HistHead - 1 - (HistLastSol - ClosingSol) + 60) % 30`. Guard: only when `0 <= HistLastSol - ClosingSol < HistFill`.
   - Values are in the market's unit at the time.
3. **After the reprice (M:545-550)** and, if E5 is built, after E5's step 4 (rate step, withdrawal re-expression): `HistOpen(Mk, Next)`, with `Next = (i == Passed - 1) ? SolNow : s + 1`.
   - On the last pass of a jump, first append `s + 1 .. SolNow − 1` as unknown (at most 30).
   - Then append `Next` with the post-reprice `PriceMcr`, plus E5's unit and rate in force for `Next`.
   - Run it whether or not `bLive`.

**`Debug_SetMarketPrice` (M:487).** After clamping, if `HistFill > 0`, set `HistPrice[slot × 30 + (HistHead − 1 + 30) % 30]` to the new price.

**Write the frame.**
- `WriteMarketHistoryBlock(FArchive&)`: `SyncRivalEconomies(false)` first. Then write:
  - `int32 SlotCount`, then the names ascending as `FString`, exactly as M:1287-1292 does;
  - `int32 MarketCount`;
  - per market in `RegionId` order: `int32 RegionId`, `HistFill`, `HistHead`, `HistLastSol`, `SlotCount × 30` prices in raw entry order, 30 sold, 30 bought, and if E5 is built 30 rates and 30 unit bytes.
- `GetMarketHistoryBlockBytes()`: measure the payload like `GetMarketBlockBytes` (M:1353) does.

**`ReadMarketHistoryBlock(FArchive&, const TArray<uint8>& File)`.** Warnings only, never an Error, never a refusal. In order:
1. **Find the frame** with `RHSaveFrame::Read(.., HistoryTag, ..)`. If it is lost: "LoadColony v<N>: the market history is lost (%s); the history starts empty".
2. **Check E4's block.**
   - `bMarketBlockLost` set because the frame was not found: "LoadColony v<N>: the market history is dropped: its market block was lost (region %d)".
   - Slot table reset (M:1526-1529): "LoadColony v<N>: the market history is dropped: its market block's slot table was reset (region %d)".
3. **Parse.**
   - Slot count in 0..`MktMaxList`; names through `RHSaveFrame::ReadName`, ascending and unrepeated.
   - Market count in 0..64.
   - Per market: fill in 0..30, head in 0..29, last sol −1 exactly when fill is 0 and ≥ 0 when fill > 0; arrays sized by the slot count; no bytes left over.
   - Any failure: "the market history is malformed (%s); the history starts empty".
4. **Check against E4's loaded state:**
   - (a) The slot names equal the live `EconSlots` names exactly; otherwise drop all.
   - (b) A region with no live market drops its ring. Log a Warning with your own wording, which must not contain "the market block is lost".
   - (c) **Only when fill > 0:** `HistLastSol == MarketCheckedSol`; otherwise drop with "the history ends at sol %d but the market's open sol is %d".
   - (d) Clamp a known price outside `[FloorMcr, CeilMcr]`, with a Warning.
   - (e) Rewrite the open-sol entry from the loaded `PriceMcr`, one Warning per slot that differed: "region 0's history entry for the open sol %d held %s at %lld mcr but the loaded price is %lld; rewritten".
   - (f) If E5 is built: a unit of 1 before `CurrencyOpenSol`, or any unit of 1 when E5's block was lost, is reset to 0 together with its rate, with a Warning.
5. **Assign** each ring into the synced market by `RegionId`.
6. An empty ring (fill 0, last sol −1) passes silently at any open sol.
7. `Debug_SetMarketHistoryEnabled` never touches the loader.

`bMarketBlockLost` (only if E5 did not add it):
- a transient member;
- cleared at the top of `ReadMarketBlock`;
- set in the `!bFound` branch (M:1383-1386) and in the `bSlotsBad` reset (M:1526-1529).

### 4.4 Sim: W
- **W:37:** `RHSaveVersion = <N>`, with the comment R2's "The save version" gives (wording at spec line ~779 of REVISION 2's section). The strict check (W:13968-13971) is unchanged.
- **Save:** after the last `RHSaveFrame::Write` (E4's at W:13933, then E5/E6/D6e/D7's), add `RHSaveFrame::Write(Ar, RHSaveFrame::HistoryTag, [this](FArchive& B){ WriteMarketHistoryBlock(B); });`.
- **Load:** after the last tail reader (W:14743 `ReadMarketBlock`, then E5's and E6's), call `ReadMarketHistoryBlock(Ar, Bytes);`.

### 4.5 Sim: tests
**MX, the `-market` suite.**
- **MX12 and MX15:** after MX11, before MX8 (the file's last block).
- **MX13 goes INSIDE MX8's block** (MX:1663-1925):
  - add a ring digest (prices, values, fill, head, last sol) to `FRun` and compare it in each pair;
  - do not change any existing MX8 line's text.
- **MX14 goes at the end of MX8's block, before `RetireAll()`** (`marketmx8` and the injected Kestrel exist only there; section 11, B2).
- **MX16** is the mutation record (run by hand).
- **Header comment** (MX:16-20):
  - it calls E4's hand-run mutation list "MX12";
  - reword it to "E4's mutation list (hand-run)" so E8's printed MX12 is unambiguous (section 11, m2);
  - add the E8 block list.
- **`LoadSlot`** (MX:280-292): add `Sim->Debug_SetMarketHistoryEnabled(true);` beside the other runtime resets.
- **Version literals:** update `Ver == <prev>`, the `V54`-style previous-version constant and the "/<prev>" texts (MX:1316, :1374, :1380 at HEAD).

**Every other suite's version literals.** `git grep -nE '== 5[5-9]\b|"/5[5-9]"' Source/RedHopeSim` at build time (16 lines in 10 files at HEAD, including `RHSimCommandlet.cpp:10867, 12147, 12563, 14103, 15128`, Cavern :738, Credit :609, DeepDistrict :771, Depth :786, Economy :857, Export :530, Tram :683, Transit :1596). Bump each to the new version.

**XE10** (`RHSimExportSuite.cpp:530-531`):
- If its "the market frame ends the file" clause is still unbounded at build HEAD (E5 should have bounded it), bound it to the market frame's own length.
- The line's text must not change.

### 4.6 Presentation: `RHFrameStats.h` / the stats cheat
- `ESlot` gains `Market = 3`; `SlotCount = 4`.
- In `RHCheats.cpp` around :2120 / :2161 / :2180:
  - the `[FrameStats]` line gains "market tick %.3f" beside "section tick";
  - it gains "market worst rebuild %.2f ms", the max of the deck's and the trade visualizer's `WorstRebuildMs`, both reset at window start (`:2150`);
  - the spike lines include the market slot.

### 4.7 Presentation: the deck (Dh / D / K)
**Dh:**
- `EFold { Politics, Crew, City, Fleet, Power, Market, Count }` (Dh:136).
- `bool bMarketExpanded = false;` beside Dh:391-392.
- `Debug_ExpandAll` (Dh:29) also sets `bMarketExpanded`.
- Declare `BuildMarketPanel`, `RefreshMarketPanel`, `BuildTradeLegend`, `GetTradeLabel`, `HandleTrade`, and the panel's cached `FText`s and their key.
- Add these `Debug_` const getters (R2-5):
  - `Debug_GetMarketRowCount()`
  - `Debug_GetMarketSparkGlyphs(FName)` (non-space glyphs drawn)
  - `Debug_GetMarketSelected()`
  - `Debug_GetMarketDetailText()`
  - `Debug_GetMarketOrdersText()`
  - `Debug_IsMarketRateShown()`
  - `Debug_GetMarketRegionButtons()`
  - `Debug_GetMarketRouteCount()`
  - `Debug_GetMarketRouteChip(FName)`
  - `Debug_GetMarketChipColours(TArray<TPair<FLinearColor, FLinearColor>>&)` (fill and text per chip)
  - `GetOrderDraft(FName Rival)` (reads `OrderDrafts`, Dh:327)

**K:**
- `FoldFlag` (K:228-238) gains `case EFold::Market: return &bMarketExpanded;` **before `default:`** (K:236).
- `BuildMarketPanel()`:
  - `RHDeck::Panel(FName("trade"), "MARKET", Gold, body, summary)`;
  - the body is visible while `bMarketExpanded`;
  - the panel is visible while `IsMarketLive()` and some rival is available (the K:2455-2464 pattern);
  - the header carries the summary label (`Label(10)`) and `FoldToggle(EFold::Market, FName("RH.MarketToggle"))` (the Power pattern, K:2710).
- Sections, in order. Each is a `TileBrush` sub-card (padding 9) with an Amber `Label(8)` heading.
  - **PRICES** (R2-4 two-line rows):
    - each row is a flat `SButton` tagged `RH.MarketRow.<Res>`, Gold while selected;
    - the 30-glyph sparkline is Mono 9 Sky;
    - the 5-sol change is in `TextDim` `Body(8)`.
  - **DETAIL:** a 3-row × 30 block chart, as `GetPowerChartText` builds one (D:1543-1576); the band from E7's getters; Earth's credit price; the available rivals' holdings; the 8-sol tally.
  - **TRADE BALANCE:** the ring's values, Back 1..8.
  - **EXCHANGE RATE:** if E5 is built, R2-3 plus R3-5.
  - **OPEN ORDERS.**
  - **ROUTES:** the chip order defected > closed > embargo > disrupted > storm > open, matching the card's chain at K:2378-2383.
- The region selector is drawn only when more than one region has an available economy rival (`GetMarketRegionIdAt`).
- `ShownMarketSlots(RegionId, Out)`: walk `GetRivalEconomyCount` / `GetRivalEconomyName` and keep `IsRivalAvailable && HasRivalEconomy && GetRivalRegionId == RegionId`; union `GetRivalTradedResources`; sort by name string.
- **Caching (R2-7.3):**
  - strings are keyed on `(GetMarketCheckedSol(), region, selected resource, a reload counter bumped by OnColonyReloaded)`;
  - the PRICES rebuild key also carries the availability bitmask;
  - order and route rows rebuild on their change keys;
  - no allocation per paint.
- Wrap the rebuilds in `RHFrameStats::FScope(Market)` and track `WorstRebuildMs`.
- `BuildTradeLegend()`: "Trade routes: teal open · amber embargo or disrupted · red closed · blue halted by a storm".

**D:**
- `DeckButton` (D:48-60) gains an optional `FName Tag` that adds `FTagMetaData`.
- Insert `FoldAnchor(EFold::Market, BuildMarketPanel())` in its own slot straight after Politics (D:895), with the same `Padding(0,8,0,0)`.
- Add the Routes `DeckButton` beside Traffic (D:1187-1192): label `GetTradeLabel` ("Routes" / "Routes [on]"), tag `RH.TradeOverlay`, `HandleTrade` calls `PC->ToggleTradeOverlay()`.
- `BuildTradeLegend()` goes under the TRAFFIC legend in `TopLeftStack` (D:787-794).
- `SRHCommandDeck::Tick`: call `RefreshMarketPanel()` beside `RefreshTramList()` (D:1351).

### 4.8 Presentation: the trade visualizer (new)
New `Source/RedHope/Public/RHTradeVisualizerSubsystem.h` and `Private/RHTradeVisualizerSubsystem.cpp`:
- `UTickableWorldSubsystem`, following `URHTrafficVisualizerSubsystem` (T: the class shape at `RHTrafficVisualizerSubsystem.h:32-48`).
- Polls every 0.5 s.
- **Key:** a CRC over each available rival in name order (name, status chip, `RivalMarkerPos`), plus the depot location `GetColonyDepotLocationCm(GetColonyRegionId())`, plus `ShowsSurfaceLevel()`.
- Rebuilds one `UProceduralMeshComponent` only when the key moves:
  - one section per colour (Teal, Amber, Red, Sky), cleared when empty;
  - 3 m ribbons sampled every 10 m on `RHMarsTerrain::GroundZCm + 40 cm`.
- `IsOverlayOn` / `SetOverlayOn`, `GetDrawnRouteCount()` and `GetRouteStatus(FName)`.
- Wrapped in `FScope(Market)`, with `WorstRebuildMs`.
- Writes nothing to the sim.

### 4.9 Presentation: V / Vh (R2-9 + R3-6)
1. Move `RivalMarkerPos(FName, float) const` (Vh:259) above `private:` (Vh:216), unchanged.
2. Add `bool ShowsSurfaceLevel() const { return ShowsLevel(0); }` as public. `ShowsSurface` stays private.
3. `RefreshSovereigntyVisuals` (V:4206):
   - add a per-marker status word, diffed like `RivalMarkerState`, over the chip order;
   - while `URHTradeVisualizerSubsystem::IsOverlayOn()`, rewrite the marker's `UTextRenderComponent` label (spawned by `AddLabel`, V:4243) to "<DisplayName> · <word>";
   - restore the bare name when the overlay goes off.

E7 will already have changed V:4337-4341 to `GetColonyDepotLocationCm`.

### 4.10 Presentation: the controller and cheats
- **`ARHPlayerController`:** `ToggleTradeOverlay()` / `IsTradeOverlayOn()`, mirroring `RHPlayerController.cpp:2189-2200`.
- **`RHCheats.cpp`, cheat code only:**
  - `RH.Trade overlay [0|1]` (the `RH.Trade` command at :1060; follow `RH.Traffic overlay` at :2788);
  - `RH.Market` sub-commands: `discover` (loops `Debug_MaybeDiscoverSettlement` k = 1..64), `grant <credits>` (`Debug_GrantCredits(credits × 1000)`), `embargo <Rival> <sols>` (`Debug_SetEmbargo`), and `inject-region <Name> <RegionId>` (copies Zarya's row under the new name with that `RegionId`, via `Debug_InjectRival`);
  - `RH.Market history` is optional (R2-1 names it but never specifies it). If built, it only logs.

### 4.11 Docs
- `docs/qa/2026-10-15/gate-d-wording.md`: a new block, "Rows added by E8", in E4's W-row style (E4's block is at `:528`).
- `docs/design-decisions.md`: R3's 10 deviations, plus the resolutions in section 11 (B1, B2, m2, m6) as builder deviations.

---

## 5. Save version and frame
- **Version:** `RHSaveVersion` = the next free at build time. That is **v60** if E5 = 56, E6 = 57 and D6e/D7 = 58/59 all landed; otherwise state the number used.
- **Frame:** `'RHE8'` (`RHSaveFrame::HistoryTag`), the **last** frame in the file. It is read after the last tail reader, after teardown, and only repairs with Warnings.
- **The previous version is refused** by the strict check, with the text "/<N>".
- **Payload bytes for one region with 18 slots:** **5,037** without E5's arrays, **5,307** with them. Plus 20 of frame: **+5,057 / +5,327 bytes per file**.
- **General formula:** 241 (= 20 + 221) + regions × (4,816, or 5,086 with E5). See problem m3.

## 6. Tests and checks to add (expected values where the spec gives them)

**`-market`:**
- **MX12, the ring by hand.**
  - (a) Empty: a fresh colony has fill 0, last sol −1 and every price −1; `GetMarketHistoryValue(0, ...)` is false; a save at sol 0 then a load logs no E8 Warning and round-trips byte-identical.
  - (b) MX2's fixture, rolled 1 sol, then 3, then a 12-sol jump:
    - the first roll from empty leaves fill 2;
    - the open-sol entry equals the live price for every slot;
    - walked sols match MX2's reference;
    - the jump's 4 skipped sols read −1 with 0 values;
    - each closing sol's values equal the sum of `GetMarketClearings()` fills × the price in force;
    - `GetMarketHistoryValue(0)` is (0, 0), and `Back = Fill` is false;
    - with the market off (`Debug_SetMarketEnabled(false)`) the open-sol entry is still written, equal to the unchanged price;
    - after `Debug_SetMarketPrice(Ice, floor)`, the open-sol entry reads the floor.
- **MX13, band-exact (inside MX8).** On `marketmx8` (20 sols, nine orders, two rivals traded), the era and agent twins' rings are equal entry for entry at every threshold. The fallback twins' rings are equal too. Every convoy names an available rival.
- **MX14, write-only (end of MX8, before `RetireAll`).** From `marketmx8`, run twins with the history switch on and off, 20 sols in each band:
  - MX1's `Digest()` (MX:493) is identical;
  - the saves are byte-identical except inside `'RHE8'`;
  - the off twin's ring is empty (fill 0, last sol −1) and its save loads with no E8 Warning (this needs B1's rule);
  - after 40 sols, the on twin's fill is 30 and its last sol is the open sol.
- **MX15, the save.**
  - `'RHE8'` is the last frame;
  - payload = `GetMarketHistoryBlockBytes()` (5,037 or 5,307 for one region);
  - the slot names are ascending;
  - save, load, save is byte-identical;
  - the previous version is refused with "/<N>";
  - a lost `'RHE8'` starts empty with a Warning while the market loads intact;
  - a lost `'RHE4'` with an intact `'RHE8'` drops the ring with "its market block was lost";
  - a slot-table reset drops it;
  - a doctored last sol (fill > 0) drops it;
  - fill 0 with last sol 5, and fill 3 with last sol −1, are malformed and dropped;
  - a doctored slot name drops it;
  - a price outside the band is clamped;
  - an open-sol entry that disagrees with the loaded price is rewritten, naming the slot;
  - after `Debug_SetMarketPrice(Ice, floor)`, the round trip is byte-identical with no Warning;
  - `Debug_InjectRival` keeps the ring through `bKeepHist`;
  - `Inject(..., 1)` makes two rings that move independently;
  - no E8 text contains "the market block is lost";
  - no Error line.
- **MX16, the mutations** (each built, run and restored by hash; each must fail the check named):
  1. the values written before the clearing (fails MX12);
  2. the open-sol entry written before the reprice (fails MX12);
  3. the `bKeepHist` copy omitted (fails MX15's resync case);
  4. the last-sol check skipped (fails MX15);
  5. the reprice reading the ring (fails MX14's digest);
  6. a jump's skipped sols filled with a price instead of −1 (fails MX12);
  7. if E5 is built: the open-sol entry written before E5's withdrawal step;
  8. the history terms folded into `bKeep` (fails MX9's byte-identical round trip and MX15's "prices as saved").
- **`-scale`** (outside the battery):
  - the existing `budget: 1,000 market rolls ... (<= 2)` line (`RHSimCommandlet.cpp:17118`) holds;
  - the ring costs at most +0.1 ms against the pre-E8 figure (E4 measured 0.746 ms; E5's bar is 1.246 ms);
  - every digest is identical;
  - see m8 for how to measure.

**Click test** (`RH_CLICKTEST_RES=1440x860 bash scripts/unreal/rh_clicktest.sh`). Twelve new reports, plus `market.BUY` fixed in place. All are placed before `traffic.on` (CT:1760), except `market.empty`, which goes just before `market.setup` (CT:1732).
1. **`market.empty`.**
   - `Runner.Do`: `SetFoldOpen(EFold::Market, true)`.
   - Check: the ORDERS text is "No orders out"; the rate section is hidden; there are 0 region buttons; Ice's change text is empty.
2. **`market.BUY` (fixed).** `GetOrderDraft(Zarya)` has exactly one non-zero entry, it is +100, and its key is Hydrogen. The report names the key and the amount.
3. **`market.panel`.**
   - Press `RH.MarketToggle`.
   - Check: `IsFoldOpen(Market)`, and the other five are false.
   - The row count equals the rule's count computed in the check **and** equals **5**. The report prints both.
4. **`sweep.market`.** `PushSweep` with MARKET open and scrolled to the top. Allow about 1 s for the two-frame fold scroll.
5. **`market.orders`.** The rows equal `GetConvoys().Num()` (**1**), the row names Zarya, and it contains "clears at the end of sol <ClearSol>".
6. **`market.history`.**
   - Record F0 and G0, then `Debug_RollMarket(GetMarketCheckedSol() + 3)`.
   - Check: fill == `F0 == 0 ? 4 : min(30, F0 + 3)`; glyphs == `G0 == 0 ? 4 : min(30, G0 + 3)`; last sol == open sol; the open-sol entry == `GetMarketPriceMcr(Ice)`.
7. **`market.select`.** Press `RH.MarketRow.Struct`. Check: selected == Struct; the detail contains "Structural Materials" and the exact band text from `GetMarketFloorMcr` / `GetMarketCeilMcr` formatted with `FormatCredits`.
8. **`market.discover`.**
   - Loop `Debug_MaybeDiscoverSettlement(k)` for k = 1..64 until it returns true.
   - Check: `IsRivalDiscovered(Meridian)`; the row count equals the rule's **and** equals **7**; the route count is **2**; Meridian's card visibility attribute is Visible (not on-screen: EARTH & NEIGHBORS is folded; m5).
9. **`trade.overlay`.** Press by tag `RH.TradeOverlay`. Check: `IsTradeOverlayOn()` and `GetDrawnRouteCount() == 2`. The report also prints `ShowsSurfaceLevel()` (m6).
10. **`trade.overlay.status` (two reports).**
    - `Debug_SetEmbargo(Zarya, 5.0)`. Check within 1 s: Zarya's status is Embargo, Meridian's is Open, and the chip reads "embargo, 5.0 sols to reassure".
    - `Debug_ClearPolitics()`. Check: both are Open and the chip reads "open".
11. **`trade.overlay.off`.** Press the tag again. Check: off, and the drawn count is 0.
12. **`market.chips`.** Every chip has `|Luminance(fill) − Luminance(text)| >= 0.4`, using CT's `Luminance` (CT:1419).

**Total:** **91 at HEAD + 12 = 103**, plus whatever E5, E6, E7 and the site-kit visual pass add. Re-count at build HEAD and state the total. Three clean runs in a row. `leak.total` is unchanged.

**Frame check (R2-7):**
- **Scene S1:**
  1. NEW COLONY;
  2. `RH.CityBench 2000`;
  3. `RH.Market discover`;
  4. `RH.AddStock Hydrogen 200`;
  5. `RH.AddStock SpareParts 20`;
  6. `RH.Market grant 10000`;
  7. `RH.Market order Zarya Ice:300;Struct:-100`;
  8. `RH.Market order Meridian Food:100`;
  9. `RH.Floor 0`.

  UNCERTAIN: whether that hydrogen and those parts dispatch two convoys. Confirm with `RH.Market` that 2 convoys are out.
- **Scene S2:** `RH.DeepBench 3000`, then `RH.Floor 0`.
- **Pairs:** at each of 1440x860 and 1600x900, adjacent P0 (MARKET folded, overlay off) and P1 (MARKET open, Ice selected, overlay on). 15 s windows, 1x clock, 5 s settle.
- **Bar:** the median and p95 deltas are each ≤ 1.0 ms; the Market slot averages ≤ 0.2 ms a frame; the worst rebuild is ≤ 2 ms.
- **DeepBench note:** the handoff records that DeepBench is over its absolute budgets. E8's bar is relative, so that does not block E8; do not "fix" it here.

**Captures** (`scripts/unreal/rh_capture.sh`, `Saved/RHCapture/e8/`; look at each one):
- `e8_panel_00`;
- `e8_detail_00`;
- `e8_balance_00`;
- `e8_routes_00`;
- `e8_overlay_00`, plus a 1600x900 twin;
- `e8_overlay_section_00` (`RH.Section on`: no ribbon, no marker);
- `e8_regions_00` (`RH.Market inject-region Aurora 1`);
- `e8_rate_00` (if E5 is built).

At 1440x860, every price row must sit on two clean lines. If one wraps, cut the sparkline to 24 glyphs, never the text.

## 7. Mutations to run
MX16's eight (seven without E5). Each is built, run and restored by sha256, and each must fail the check named in section 6. Record each pair (mutation → failing line) in `design-decisions.md`, as E4 did (`design-decisions.md:1659`).

## 8. Battery lines expected to move (every other line must stay identical)
Run `bash scripts/sim/rh_battery.sh 10`, then `python3 scripts/sim/diff_battery.py <the baseline the gate before E8 left> Saved/RHBattery`. Exactly these kinds of line, and wall-clock digits, may differ:
1. **The version lines (27 at `battery_post_s2fix`),** plus the version lines E5, E6, D6e and D7 add (CU10's at least):
   - CV8 ×2, CR9 ×2, K6 ×2, DP11 ×2, EC6 ×2, DD9 ×2, XE10 ×2, TM6 ×2, MX9 ×2, TR18 ×2, X15 ×2;
   - FRAMING e5, FARICE F16, SH12, TN1, TN8.

   Why: `RHSaveVersion` changes. E4's 47 "LoadColony v55:" literals do NOT move.
2. **The whole-file size lines.** Each moves by exactly E8's delta (section 5); today's numbers in brackets identify them:
   - not masked by `diff_battery.py`: CV8 "(12059 -> 12139 bytes)" (both numbers); DP11 "(12087 -> 12119 bytes)" (both); EC1 ×2 "both saves are 13100 bytes"; DC9 "the 13408-byte file";
   - masked: MX1 ×2 "(11761 bytes)" and "(11740 bytes)"; MX9 "(12173 bytes)"; CR9 "(11892 bytes)"; EC6 "(13100 bytes)"; XE10 "(11817 bytes".

   Why: a new frame at the end of every file. Name the four unmasked ones in the gate diff and state that the masked ones moved. MX1's "up to the market block (8717 bytes)" and EC1's "(3410 bytes" do NOT move, because the frame comes after them.
3. **MX9's new E8 Warnings in `market.log`,** each appearing twice (live, then in the `LogInit` echo):
   - `mktmx9r2`: "...the market history is dropped: its market block was lost (region 0)";
   - `mktmx9d1`: two lines, Ice and Struct ("...history entry for the open sol 1 held ... rewritten");
   - `mktmx9d4`: "...dropped: its market block's slot table was reset (region 0)".

   Before predicting, grep the pre-E8 baseline. Any lost-`'RHE4'` or slot-reset load E5, E6, D6e or D7 added in any log will add an E8 line too (m7).
4. **The new MX12-MX16 result lines** in `market.log`.

**Must hold:** `BATTERY COMPLETE: 0 suite(s) not green`; logs newer than both dylibs; the pins "power: gen 50 W load 20 W" and "deposit Regolith_A: 40645 kg".

## 9. Playtest
`RH_PLAYTEST_DISTRICTS=1 bash scripts/sim/rh_playtest.sh 110 e8_full guide full` must be **byte-identical** (`cmp`) to `Saved/baselines/ind_full_playtest.txt` (296,489 bytes). Why:
- the ring is write-only;
- it logs nothing during play (its only lines are load Warnings, and the playtest loads no doctored file);
- the `.txt` holds only `[Playtest]` lines;
- no price or other number in the sim moves (MX14 proves it).

Then re-cut the checkpoints (`scripts/unreal/rh_checkpoints.sh all --force`): the bump refuses Midgame and Late. Load both.

## 10. UI notes: the P8c ladder and the accordion as built
- **Accordion.** As built, `SetFoldOpen` (K:264-282) shuts every other fold and scrolls the opened one into view two frames later (`ApplyFoldScroll`, K:285-302). Market slots in cleanly, with three conditions:
  - its `FoldFlag` case must come before `default:`;
  - its anchor must be registered through `FoldAnchor`;
  - `EFold::Count` stays the "no scroll" sentinel.

  Politics opens on its own when a decision is pending (`bPoliticsExpanded || Pending()`), so Politics and MARKET can both show then. That is acceptable.
- **The P8c ladder** (`SRHSectionLadder`, D:629-636; `GetFreeBand` K:305-330). The ladder frames the deep view between the shaft strip's right edge and the right column's left edge. The column is pinned at 340 px (D:39), so MARKET's height never moves the band.

  The one coupling is `GetStripLayout` (K:338-367). It places the shaft strip below `TopLeftStack`, which also holds the TRAFFIC legend slot where `BuildTradeLegend` goes. A visible trade legend therefore pushes the strip down and can cut its rows.

  **Recommended (no director needed):** show the trade legend only while the overlay is on **and** `ShowsSurfaceLevel()`. In the section view the ribbons are hidden, so the legend would describe nothing and would only shift the strip under the ladder. Log it as a builder deviation, or leave it as specified if the orchestrator prefers. Either way, take `e8_overlay_section_00` with Routes on to show the ladder unchanged.
- **The command bar.** "Routes [on]" adds about 100 px to the bar at 1440x860. `sweep.deck` and `deck.contrast` cover it automatically (a filled `DeckButton`). Check the 1440x860 capture for bar clipping.
- **1600x900.** P8c's commit records that the main click test's TRAFFIC steps fail at 1600x900 on a window clamp. Run the click test at 1440x860 only, as the spec says. 1600x900 is for captures and the frame check.

## 11. Problems found (and the resolutions to build)

**BLOCKING (the gate fails if built literally):**
- **B1. MX14 contradicts the history switch as specified.**
  - **The contradiction:**
    - R2-2 and R3-2 make `Debug_SetMarketHistoryEnabled(false)` "skip all three write points" and keep a `HistEnsure` branch for "a ring the switch let lag".
    - R3's MX14 runs its twins from `marketmx8`, which MX8 saved with history on, so the ring is non-empty. MX14 then requires that "the off twin's ring is empty: fill 0, last sol −1" and that "the off twin's save loads with no E8 Warning".
    - Built literally, the off twin keeps the loaded ring, ending at sol 20, while the open sol reaches 40. Its save then fails R2-6 4(c) on load: a Warning, and MX14 is red. "Empty" is also false.
  - **Resolution (no director):** switching the history off empties every ring (`HistInitEmpty` on each market), and later writes are skipped.
    - MX14's "empty" and "no Warning" then hold by construction.
    - The write-only proof is unchanged: the digests are compared, and mutation 5 still bites.
    - The lag branch of `HistEnsure` becomes unreachable through the hook. Keep it as a defensive gap-fill, capped at 30.
    - Log this as a builder deviation.
- **B2. "MX12-MX16 after MX11" cannot hold for MX13 and MX14.**
  - `marketmx8` is created in MX8 (MX:1693), and MX8 is the suite's last block.
  - The fixture needs the injected Kestrel, which MX8 retires at the block's end.
  - MX13 needs MX8's paired-run harness (`RunWindow`, `FRun`).
  - Placed after MX11, these checks run before `marketmx8` exists in this run (or against a stale file of the previous version, which `LoadSlot` refuses with an Error).
  - **Resolution:** MX13 inside MX8's paired run; MX14 at the end of MX8's block, before `RetireAll()`; MX12 and MX15 after MX11. This is placement only, so the printed lines are unchanged.

**MINOR:**
- **m1.** Section 0's "no `OnPaint` or `MakeLines` in `Source/RedHope`" is stale (P8c's ladder). It changes nothing for E8.
- **m2.** "MX12" is already E4's name for its hand-run mutation list (the MX:19 header and `design-decisions.md:1659`). Keep E8's printed MX12-MX16 as specified and reword MX's header comment, or rename E8's checks to MX13-MX17 if the orchestrator prefers. No battery line collides either way.
- **m3.** "+5,057 / +5,327 bytes for every size line" assumes one market region in every fixture. A file with no market gets +241, and each extra region adds 4,816 (or 5,086). Check each size line's delta with the formula in section 5; UNCERTAIN until the gate before E8 is in.
- **m4.** The click-test total in the spec (86 → 98) is stale. HEAD is 91 (the tram), so expect 103 plus what E5-E7 and the site-kit pass add.
- **m5.** `market.discover`'s "Meridian's card is visible" is ambiguous, because the accordion has folded EARTH & NEIGHBORS. Check the card's visibility attribute and `IsRivalAvailable(Meridian)`, not on-screen presence.
- **m6.** The trade legend lives in `TopLeftStack`, so it shifts the shaft strip (and the strip's place under the P8c ladder), and it would still show in the section view. Recommendation in section 10.
  - `trade.overlay`'s `GetDrawnRouteCount() == 2` assumes the view is at the surface. Expected after NEW COLONY, but UNCERTAIN; print `ShowsSurfaceLevel()` in the report.
- **m7.** R3-3's list of new Warnings is complete only at HEAD (E4's market Warnings appear only in `market.log`). Doctored loads that E5, E6, D6e or D7 add may trigger E8's drop Warnings in other logs. Grep the baseline from the gate before E8.
- **m8.** `-scale`'s "+0.1 ms over 0.746 ms" is about the run-to-run noise of a sub-millisecond timing. Measure the ring's cost as in-process adjacent pairs (`Debug_SetMarketHistoryEnabled` off, then on; at least 5 each; take the median). The hard ≤ 2.0 ms line must hold. A miss goes to the director, not into a budget edit.
- **m9.** R2-1 mentions an `RH.Market history` console sub-command "below", but never specifies it. It is optional, cheat-only and log-only.
- **m10.** The spec's E5, E6 and E7 accessor names are design names (UNCERTAIN). Rebase each onto the gate as built and list the rebases.
- **m11.** The visual pass's uncommitted edits touch V, Vh, `RHCheats.cpp` and `RHClickTest.cpp`, and E7 edits V. Every V and CT anchor will move after `d842dd0`.

## 12. Deviations the builder must NOT take
- Do not touch E4's `bKeep` line (M:286), and do not add history terms to it (that is mutation 8).
- Do not let any sim rule, price, digest or log line in play read the ring. Do not log from `RollMarket`.
- Do not declare `GetMarketBandMcr` or E8's own `GetConvoyLegSols`. Do not derive the band from `EarthValue_mcr`.
- Do not show any undiscovered rival's slots, holdings, name, route or ribbon, or count them in "N prices".
- Do not smooth or hide the credit-view step at the mark's open sol. Label it.
- Do not use the word "the market block is lost" in any E8 text. Never refuse a load, and never log an Error.
- Do not locate the overlay button by text, and do not name it "Trade".
- Do not exempt MARKET from the accordion (the director chose Q-C (a)). Do not move Buy, Sell, SEND ORDER or E5's desk buttons into MARKET.
- Do not edit a frame budget to pass. Do not change TRAFFIC's own overlay or the P8c ladder.
- Do not pick a save version other than the next free one at build time. Do not write `'RHE8'` anywhere but last.
- Do not change the text of any existing battery line beyond the version and size lines listed in section 8.
- Do not add a click-test step for the region selector: it is covered by MX15 and `e8_regions_00`.
- No `git stash`, `checkout` or `reset`. Commit only E8's files, by explicit path, as one gate commit.
