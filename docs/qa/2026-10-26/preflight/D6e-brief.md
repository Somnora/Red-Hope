# D6e build brief: lift and tram power (preflight at HEAD d842dd0)

Read-only preflight. Every anchor below was read at HEAD `d842dd0` with `git show d842dd0:<path>` / `git grep d842dd0`. The working tree was not read. Notation: `W:` = `Source/RedHopeSim/Private/RHSimWorldSubsystem.cpp`, `H:` = `Source/RedHopeSim/Public/RHSimWorldSubsystem.h`, `T:` = `Source/RedHopeSim/Private/RHSimTransit.cpp`.

**Queue position.** D6e builds after the visual pass, E5 (v56) and E6 (v57). E5 and E6 each add a frame at the end of the save and move `W:` lines. **Before you edit anything, re-grep every `W:`/`H:` anchor below by its quoted code at your own HEAD.** Anchors in `T:`, `RHSimTram.cpp` and the suites are unlikely to move, but check them too.

**Spec:** `docs/qa/2026-10-22/d6-caverns-design.md`. The authority order is: DIRECTOR ANSWER, then the REFRESH AT 31ffa6f (D6e only), then MERGED DESIGN AND REVISION section 5, then the review, then the drafts. Two things in the spec are superseded and must not be built: the draft's section 7 (40 W, 2 W, priority 8, `-liftpower`) and "Save v51". There is also a director-answered requirement that lives in a different doc: `docs/qa/2026-10-23/d8-deep-view-design.md` R7 and its DIRECTOR ANSWER Q-C (a). See decision 12.

---

## 1. Authoritative decisions, in one list

1. **The switch is `bHasTransit`, the first lift car.** Every new branch tests it first. With no car, no entry is appended and `StepPower` is D5's code exactly. A tram line cannot exist without a car (LayTramLine refusal 1).
2. **The rows** are placeholders, marked "director review". Add them to the CSV and the DataTable, keeping the two in sync:
   - `LiftCarW` = 200 (W per car);
   - `TramWPerCell` = 5 (W per chain point);
   - `TransitLoadPriority` = 6.
   The draft's 40 W / 2 W / priority 8 are superseded.
3. **Load entries.** In `StepPower`, after D5's cooling loop:
   - one entry per fitted shaft (`Shafts[S].LiftCars > 0`), in ascending shaft Id, drawing `LiftCars x LiftCarW`;
   - then one entry per tram line, in ascending line Id, drawing `GetTunnelChainLength(StopA, StopB) x TramWPerCell`.
   `FLoadEntry` gains `uint8 Kind` (0 building, 1 cooling, 2 lift, 3 tram) and `int32 Index` (the shaft Id or the line Id).
4. **Ungate D5's scraping check from cooling [RC 7].** Both `CoolingEntries > 0` tests become `CoolingEntries + TransitEntries > 0`: the `bScraping` line and the walk gate. `StepDepthWarmth`'s third argument **stays** `CoolingEntries > 0`.
5. **The comparator** orders by:
   1. Priority ascending;
   2. then Kind ascending;
   3. then, among cooling entries, the deeper floor first (`CoolLevel` ascending);
   4. then, among lift and tram entries, the lower `Index`;
   5. otherwise false (building ties stay unbroken, as in D5).

   With no transit entry this is D5's order, element for element.
6. **Inside the walk:**
   - the `ShortMask` write tests `E.Kind == Cooling`, never `!E.B`;
   - a lift or tram entry in the walked prefix adds `RoundToInt64(SubDt x 1000)` ms to its own 2-slot ring, tagged with `TransitSolOf(GetStepEndSeconds())`.
7. **The consequence [RC 8].** `Served = 1000 - min(1000, ShortMs(s-1) / 1200)` permille, read at the roll for sol s.
   - The new `GetShaftServedCapacityPlaces(S)`:
     - returns `HoistPlacesPerSol` for a shaft with no car;
     - else returns `Hoist + (Cars x LiftPlacesPerSol - Hoist) x Served / 1000`, in int64.
   - It replaces the capacity argument **only** in `RollTransit`'s shaft wait (`T:375`). `GetShaftCapacityPlaces` (`T:235-243`) is unchanged, so TR4's 1,000 and 4,000 and the robots' reserve reads keep D4's numbers.
   - A tram line's capacity is `TramPlacesPerSol x Served / 1000` (`T:384`), floored at 1 (see spec problem SP1).
8. **Nothing else changes.** No rating, arrivals, ride speed or stranding effect. A dark sol raises only the next sol's waits.
9. **No `Debug_SetTransitPowerScale`.** `-transit` stays identical because every one of its world steps pins the bank to 1e9.
10. **The save is a framed tail block that repairs** (the REFRESH, finding F1, and the 10-25 handoff: "new blocks must be RHSaveFrame frames"):
    - its own tag, written after the last frame written;
    - read after the last tail reader, after teardown;
    - a bad slot or entry is emptied or dropped with a Warning, never refused.
11. **The version** is the next free one at build time: **v58** if E5 is v56 and E6 is v57 as queued. Recount it.
12. **The served-capacity bar ships inside D6e.** This is the D8 DIRECTOR ANSWER Q-C (a) and D8 R7: "D6e's build swaps the bar's denominator (one RedHope line) and drops 'nominal'". It is presentation work, so the click test and captures apply.
13. **Deviations to log** (MERGED section 10, plus these):
    - 6: lift power cuts capacity, not ride speed;
    - the D6e block is a framed tail block that repairs instead of refusing;
    - plus any of SP1-SP4 below that you adopt.

---

## 2. Exact changes per file (HEAD d842dd0 anchors; re-grep after E5 and E6)

### 2.1 `docs/data/RH_Config.csv` and `Content/RedHope/Data/DT_Config.uasset`
- Add three rows after the D6d rows (`TramPlacesPerSol` is CSV line 274): `LiftCarW,200,W,...`, `TramWPerCell,5,W,...`, `TransitLoadPriority,6,priority,...`. Give each a "D6e ... Placeholder, director review" note.
  - The note for `TransitLoadPriority` should say lifts and trams shed after every priority-2..5 machine and the IceDrill (6, which comes first among equals), and before the WaterPlant and D5's cooling (7), the AirFilter (8) and the ChargePad (9).
- Sync the table with `RH_TABLES=Config ... -run=pythonscript -script=scripts/unreal/rh_sync_datatables.py`. The pure-data verifiers fail on drift.

### 2.2 `H:` (`RHSimWorldSubsystem.h`)
- **Members**, beside `CoolingLoadPriority` (`H:3345`): `int32 LiftCarW = 200; int32 TramWPerCell = 5; int32 TransitLoadPriority = 6;`. The defaults equal the CSV.
- **The rings.** Either option works:
  - (a) **Recommended:** add runtime-only fields to `FRHTransitLedger` (`H:441-448`) and `FRHTramLine` (`H:462-472`): `int32 ShortSol[2] = {-1,-1}; int32 ShortMs[2] = {0,0};`. `WriteTransitBlock` (`T:1781`) and `WriteTramBlock` (`RHSimTram.cpp:186`) serialize named fields, not the struct, so their bytes do not change. Check this before relying on it.
  - (b) Parallel arrays, grown at every site that grows `TransitLedgers` (`T:535-536`, `W:7794`, `W:7832`, load `T:1939`) and `TramLines` (`RHSimTram.cpp:155`, load `W:14181`).
- **Public const accessors**, next to `GetShaftCapacityPlaces` (`H:1013`). D8 needs the first one public.
  - `int64 GetShaftServedCapacityPlaces(int32 ShaftId) const;`
  - `int32 GetShaftServedPermille(int32 ShaftId) const;`
  - `int32 GetTramServedPermille(int32 LineId) const;`
  - `int64 GetTramServedCapacityPlaces(int32 LineId) const;`
  - getters for the three rows.
- **Harness hooks (allowed):**
  - `Debug_GetTransitShortMs(Kind, Id, Sol)`;
  - optionally `Debug_SetTransitPowerRows(LiftW, TramW, Priority)` for TP1's equal-priority case.

  **Never** add a scale or switch that turns power off for `-transit` (decision 9).
- **Optional** derived field `UPROPERTY() double TransitW = 0.0;` in the power state, beside `CoolingW` (`H:230`). It is never saved, because the save writes `Power.BatteryWh` only (`W:13540`).

### 2.3 `W:` config load (the D6d block ends at `W:245`)
- Add a "D6e" block after it:
  - `LiftCarW = FMath::Max(0, (int32)GetConfigScalar("LiftCarW", LiftCarW))`;
  - `TramWPerCell` the same way;
  - `TransitLoadPriority = (int32)GetConfigScalar(...)`, unclamped, like `CoolingLoadPriority` at `W:140`.

### 2.4 `W:` `StepPower` (`W:8523`)
- `FLoadEntry` (`W:8535`): add `uint8 Kind = 0; int32 Index = 0;`.
  - The building loop (`W:8543-8574`) leaves Kind 0.
  - The cooling loop (`W:8577-8599`) sets `Kind = 1`.
- **New loop**, right after the cooling loop (before the "Pass 2" comment at `W:8601`), only `if (bHasTransit)`:
  - for each shaft S in Id order with `Shafts[S].LiftCars > 0`, add an entry with `Kind 2, Index S, WantW LiftCars x LiftCarW, Priority TransitLoadPriority`;
  - for each `TramLines[i]` in order (ascending Id), add an entry with `Kind 3, Index Line.Id, WantW GetTunnelChainLength(StopA, StopB) x TramWPerCell`;
  - count them in `TransitEntries`.
- `W:8623` becomes `const bool bScraping = bShedStep || ((CoolingEntries + TransitEntries) > 0 && B0 + (GenW - FullW) * (SubDt / 50.0) < 0.0);`.
- `W:8625` becomes `if (bShedStep || (bScraping && (CoolingEntries + TransitEntries) > 0))`.
- The comparator (`W:8629-8641`) follows decision 5.
- **The walk** (`W:8642-8665`):
  - In the `bShedStep` branch, `++Power.ShedFloors` only for `Kind == 1`. A lift or tram entry still counts in `ShedCount` (see SP2). Today's `else` branch would count a lift as a floor.
  - Replace `if (!E.B) ShortMask |= ...` (`W:8660-8663`) with `if (E.Kind == 1) ShortMask |= ...; else if (E.Kind >= 2) NoteTransitShortMs(E.Kind, E.Index, SubDt, StepEndS);`.
- `StepDepthWarmth(SubDt, ShortMask, CoolingEntries > 0)` (`W:8673`): **unchanged**.
- `Power.CoolingW` / `CoolingFloors` (`W:8671-8672`): unchanged (cooling only).

### 2.5 `T:` (`RHSimTransit.cpp`)
- **`NoteTransitShortMs(Kind, Index, SubDt, StepEndS)`.** Put it in this file so it reuses the file-local `TransitSolOf` (`T:21`).
  - `Sol = TransitSolOf(StepEndS)`; `Slot = Sol % 2` (Sol >= 0 here).
  - If the slot's tag is below Sol, reset the slot to (Sol, 0).
  - If the tag equals Sol, add `RoundToInt64(SubDt x 1000)`, **clamped at 1,200,000** (one sol; see SP3).
  - A slot holding a later sol: Warning and drop, as `ChargeTransit` does (`T:316-320`).
- **`ShortMsOf(ring, SolPrev)`:**
  - `SolPrev < 0` returns 0. Guard this: C++ `-1 % 2` is -1, an invalid index.
  - Otherwise return `Ms[SolPrev % 2]` when that slot's tag equals `SolPrev`, else 0.
- **Served permille** is `1000 - min(1000, ShortMs / 1200)`, integer, read against `TransitCheckedSol`'s sol at the roll. Inside `RollTransit(SolNow)` use `SolNow - 1`. The public getters use `TransitCheckedSol - 1`, so after a roll they report the served share the frozen wait used.
- **`GetShaftServedCapacityPlaces(S)`** follows decision 7. At Served 1000 it equals `GetShaftCapacityPlaces(S)` exactly.
- **`RollTransit`** (`T:347-395`):
  - `T:375`: `ComputeTransitWaitSec(Loads[S], GetShaftServedCapacityPlaces(S))`, with the served share read for `SolNow - 1`;
  - `T:384`: `ComputeTransitWaitSec(TramLoads[i], FMath::Max<int64>(1, (int64)TramPlacesPerSol * Served / 1000))` (SP1).
  - Nothing else changes: `TransitWaitVersion` and `PrevWaitSec` work as now.
- `ComputeTransitWaitSec` (`T:245-253`) and `GetShaftCapacityPlaces` (`T:235-243`): **unchanged**.

### 2.6 The save (`W:` save tail and load tail)
- **`Source/RedHopeSim/Public/RHSaveFrame.h`:** add `constexpr uint32 TransitPowerTag = MakeTag('R','H','D','6');` beside `MarketTag` (`:22`). It is free: E5 is 'RHE5', E6 'RHL6', E8 'RHE8', and D7 has none yet. Update the header comment.
- **Write** with `RHSaveFrame::Write(Ar, RHSaveFrame::TransitPowerTag, ...)` straight after the last frame written at your HEAD. At d842dd0 that is `'RHE4'` at `W:13933`; after E5 and E6 it is E6's `'RHL6'` write.
- **Payload** (the spec: "default 4 bytes, count 0"):
  1. `int32 Count`;
  2. then `Count` entries, lifts by shaft Id first and then trams by line Id, each `int32 Kind` (2 or 3), `int32 Id`, `int32 Tag0, Ms0, Tag1, Ms1` (24 bytes).
  
  Write one entry per fitted shaft and per line. Ms is clamped at 1,200,000 at write time (SP3), so int32 is safe. UNCERTAIN: the exact layout is the builder's choice. The spec fixes only "per fitted shaft and per line, two (sol tag, ms) slots, default 4 bytes".
- **Read** with `RHSaveFrame::Read(Ar, Bytes, TransitPowerTag, Payload, Why)` after the last tail reader. At d842dd0 that is `ReadMarketBlock` at `W:14743`; after E5 and E6 it is theirs. It runs after `ReadTransitBlock`/`ReadTramBlock` (`W:14022`), so the shafts, cars and lines are already loaded. Repairs, each a Warning and never an Error or a refusal:
  - **a lost frame:** every ring starts empty;
  - **a count outside 0..(MaxShafts + RH::MaxTramLinesCap), or past the payload:** every ring is empty;
  - **an entry naming an unknown or unfitted shaft, an unknown line, a repeated Id, or a Kind other than 2/3:** that entry is dropped;
  - **a slot whose tag is below -1, or is >= 0 with `tag % 2 != slot`, or whose ms is outside 0..1,200,000:** that slot is emptied to (-1, 0). Tag -1 is "empty" and is exempt from the slot test.
  
  A repair costs at most one sol's Served (it reads 1000).
- **`RHSaveVersion`** (`W:37`): bump to the next free version (v58 forecast). Prepend a "v58: transit power - ..." clause to the comment that describes the frame.
- Edit every literal-version test to the new number. At d842dd0 these read `Ver == 55`; after E6 they read 57:
  - `RHSimCavernSuite.cpp:738`;
  - `RHSimCommandlet.cpp:10867`, `:12147` (and its `/55`), `:12563`, `:14103`, `:15128`;
  - `RHSimCreditSuite.cpp:609`;
  - `RHSimDeepDistrictSuite.cpp:771`;
  - `RHSimDepthSuite.cpp:786`;
  - `RHSimEconomySuite.cpp:857`;
  - `RHSimExportSuite.cpp:530`;
  - `RHSimMarketSuite.cpp:1316`;
  - `RHSimTramSuite.cpp:683`;
  - `RHSimTransitSuite.cpp:1596`;
  - plus E5's and E6's own.

  Grep for `Ver == 57` and `/57`.
- **Last-frame assertions.** `RHSimExportSuite.cpp:531` asserted at d842dd0 that `'RHE4'` ends the file (`MarketFrame + Header + Payload + Footer == W.Num()`). E5 must already have amended it. Re-check it, and any "this frame is last" assertion E5 or E6 added (for example "RHL6 is last"). Each one breaks when D6e appends its frame: amend it and name the edit in the commit.
- **Hand-play checkpoints:** `bash scripts/unreal/rh_checkpoints.sh all --force` after the bump; confirm that both load (the house practice at every bump; E4's record).

### 2.7 Presentation: the served-capacity bar (decision 12)
`Source/RedHope/Private/RHSectionVisualizerSubsystem.cpp`. The visual pass is editing presentation files now, so re-anchor after it commits.
- `:120-131` `ShaftLabelText`:
  - use `GetShaftServedCapacityPlaces(Sh.Id)` instead of `GetShaftCapacityPlaces`;
  - drop " (nominal)" from `Shaft %d · %s / %s places (nominal) · wait %d s · %s commuters a sol`;
  - update the R7 comment (`:120-121`).
- `:1464`: the bar's `Cap`, the same swap. The `Hash` mix at `:1478` already takes `Cap`, so the bar redraws when served capacity moves.
- `:1600`: in the legend `Below ground: a lift's bar is its load against its places (nominal); ...`, replace "(nominal)". UNCERTAIN wording; simplest is to drop it. A Gate-D placeholder such as "against the places its power served last sol" is also acceptable.
- `docs/qa/2026-10-15/gate-d-wording.md`: update rows M6 (`:431`) and M8 (`:433`) to the new strings, status PLACEHOLDER.
- **Do not** change the shaft card (`RHCommandDeck.cpp:2330`) or `RH.Transit` (`RHCheats.cpp:551`): they are not in any spec (SP5). Log them as a follow-up.

### 2.8 The new suite `-transitpower`
- New `Source/RedHopeSim/Private/RHSimTransitPowerSuite.cpp` with `int32 RHRunTransitPowerSuite(UWorld*, URHSimClockSubsystem*, URHSimWorldSubsystem*)`.
- Declare it near `RHSimCommandlet.cpp:26` and dispatch it like `-trams` (`:14808-14810`). Check that `FParse::Param(..., "transit")` (`RHSimCommandlet.cpp:14783`) does not also fire for `-transitpower`, as the existing `deep`/`deepcommute` and `depth` dispatch already must handle. Put the longer name first if in doubt.
- `scripts/sim/rh_battery.sh`: add `transitpower` to `SUITES` (`:37-39`) and fix the "=== 57 suites ===" echo count, including E5's and E6's additions.

### 2.9 Docs
- `docs/design-decisions.md`: a D6e entry with the rows, the frame, the moved lines, and the deviations (decision 13, plus whichever of SP1-SP3 are adopted).

---

## 3. The tests (`-transitpower`)

Every fixture pins the bank to 1e9 before each step **except the shortage steps themselves** [RC 9]. Use the `-transit` pattern (`RHSimTransitSuite.cpp:74-85`): `Debug_SetBatteryWh(1.0e9)` immediately before `EraStep`/`Debug_StepSim`. `Debug_StepPower(SubDt)` (`H:808`) exists for single-step power cases.

- **TP1, entries and order.**
  - (a) A colony with no car: the load list and its sorted order equal D5's (same count, same `LoadW`, the same `ShortMask` on a DP5-style scraping step).
  - (b) One car: `LoadW` rises by exactly 200 W; four cars on one shaft: 800 W. A 14-point tram line: 70 W; 30-point: 150 W.
  - (c) The order on a shed step: priority-2..5 machines, then IceDrill (6, Kind 0), then the lift (6, Kind 2), then the tram (6, Kind 3), then the WaterPlant and cooling (7).
  - (d) With the priority forced equal to `CoolingLoadPriority` through a harness setter: cooling (Kind 1) before lift before tram; two lifts by shaft Id.
  - (e) A shed step that sheds only a lift: `ShedFloors == 0`, `ShedCount >= 1`.
- **TP2 [RC 7], the shortage.**
  - (a) **No cooling floor.** Shaft 0 to -3 (floors -1..-3 only, so `CoolingEntries == 0`), one car fitted (`Debug_AddSolidAt(Lander, Struct, GetLiftFitBillKg())` + `Debug_FitLift(0)`, as DP12 does at `RHSimDepthSuite.cpp:868-870`). The Lander is the only generator: GenBase 50 W, against a load of Lander idle 20 + car 200 = 220 W.
    - Set the clock to the **start** of sol s-1. Empty the bank. Step the whole sol unpinned, in each band.
    - At the roll for sol s: `ShortMs(s-1)` is 1,200,000 (±0, with the clamp), `GetShaftServedPermille(0) == 0`, and `GetShaftServedCapacityPlaces(0) == 200` (`HoistPlacesPerSol`).
    - A crowd fixture (`Debug_ChargeTransit` places or people) shows the wait computed against 200, not 1,000. For example, 400 places: `ceil(200 x 120 / 200)` = 120 s, where nominal capacity gives 0 s.
    - The spec says "one night". With a constant 50 W generator the colony is short all sol once the bank is empty, so run the full sol.
  - (b) D5 DP6's fixture (`RHSimDepthSuite.cpp:458-498`: shaft to -10, 40 cells on -10, a 200 W cooling floor, Lander only, empty bank) plus one car gives the same: Served 0, hoist capacity.
  - **The mutation** (spec): keep both `CoolingEntries > 0` gates. Then (a) reads about 500, because the per-step shed flag alternates, and must fail.
- **TP3.** A rule-(ii) step: the bank holds less than one step's deficit (the DP5 (ii) method, `RHSimDepthSuite.cpp:438-446`). Nothing is shed (`ShedCount == 0`, `LoadW == FullW`), and the car's ring still gains `RoundToInt64(SubDt x 1000)` ms.
- **TP4, era tolerance.** A paired run under D5 R2's two conditions (a daily reset, a dusk onset within 120 s; the DP9 method, `RHSimDepthSuite.cpp:642-728`): `|Served_agent - Served_era| <= 100` permille every sol (`2 x 60,000 / 1200`).
- **TP5, save mid-sol.**
  - A colony scraping halfway through sol s-1 is saved and loaded, then run to the roll. Its Served, wait and ring equal the never-saved twin's.
  - The frame's layout reads back as written; the default colony writes `Count` 0 (4 payload bytes).
  - A file of the previous version is refused ("Save format mismatch").
  - Doctored and resealed copies (`RHSaveFrame::Find`/`Reseal`) each load with one Warning and no Error: a lost frame, a bad count, an unknown shaft, an unfitted shaft, an unknown line, a tag of -2, a tag whose parity is wrong, ms of 1,200,001.
- **TP6, `-transit` holds.**
  - In-suite: with the bank pinned, a TR4-style real roll (6,000 places through four cars) still gives 60 s, and `GetShaftServedCapacityPlaces == GetShaftCapacityPlaces` (1,000 for one car, 4,000 for four).
  - Gate-level: `transit.log` is line-identical except its two version lines (TR18).
- **TP7 (SP1), a dark tram.** On a pinned colony with a line and a forced dark sol, a tram load of 2 or more gives a wait of `MaxWaitSec` (600), not 0. At Served 1000 the line's capacity is 600 exactly.

**Mutations to run** (each must fail the named check; record the first failing line):
1. Keep `CoolingEntries > 0` in both gates: TP2(a) fails (about 500 permille).
2. Draft A's capacity formula `Hoist + Cars x LiftPlacesPerSol x Served / 1000`: TP6 (1,200 against 1,000) fails.
3. Count a lift in `ShedFloors` (test `!E.B` instead of `Kind == 1`): TP1(e) fails.
4. Read slot `s` instead of `s - 1` at the roll: TP2(a) fails (Served 1000).
5. Drop the `Max(1, ...)` on the tram capacity: TP7 fails (wait 0).
6. Write the frame with `Count` 0 always: TP5's twin compare fails.

`-scale` (outside the battery): run it once. The lifts case fits 16 cars (`RHSimCommandlet.cpp:16428`), the trams case fits one (`:16964`), and `Debug_BuildScaleColony` pins the bank every era step (`W:3887-3893`). All of its assertions must pass, including the depth case's "ShedCount > 0" (`:16704`).

---

## 4. Battery: what moves and why

Diff against the baseline the gate before D6e leaves (E6's), with `scripts/sim/diff_battery.py`. That script masks `(N bytes`. Expected moves:

1. **The save-version lines.** Every line that prints the version the file carries, or the "Save format mismatch ... expected .../N" text.
   - At d842dd0 there are **27**, in 15 logs: credit 2, caverns 2, deep 2, depth 2, deepdistricts 2, economy 2, framing 1, export 2, farice 1, traffic 2, market 2, shafts 1, trams 2, tunnels 2, transit 2.
   - Add the lines E5's and E6's suites print. Recount literally against E6's baseline.
   - `market.log`'s `LoadColony v55:` Warnings are string literals (`RHSimMarket.cpp:1385+`) and do **not** move.
2. **The whole-file size lines the mask does not hide.** The default colony's file grows by 24 bytes (12 header + 4 payload + 8 footer):
   - CV8 `(12059 -> 12139 bytes)`;
   - DP11 `(12087 -> 12119 bytes)`;
   - EC1 era and EC1 agent (`both saves are 13100 bytes`);
   - DC9 (`the 13408-byte file`). DC9's colony fits a car, so its growth is 24 plus 24 per fitted shaft and line. UNCERTAIN exact; record it.

   MX1 (two lines, era and agent) and MX9 print `(N bytes)`, which the mask hides, so they do **not** show in the diff. The handoff's list counts them raw.
3. **The new `transitpower.log`.**
4. **Wall-clock digits** (arrivals e, eraparity EP4, traffic X16): tolerated as usual.

**Everything else is line-identical**, by name:
- `transit.log`: bank pinned on every world step, at `RHSimTransitSuite.cpp:77`, `:83`, `:464`, `:530`; the 3-sol jump at `:436` follows a pinned step.
- `trams.log`: pinned at `RHSimTramSuite.cpp:152`, `:158`.
- `deepcommute.log` except DC9: pinned at `RHSimDeepCommuteSuite.cpp:143`, `:149`.
- `depth.log` except DP11 and the two version lines: only DP12 fits a car (`RHSimDepthSuite.cpp:870`), it is the last case, and no step follows.
- `baseline.log` with its pins `power: gen 50 W load 20 W` and `deposit Regolith_A: 40645 kg`: no car, no entry.

Any other moved line fails the gate.

---

## 5. Playtest

**Byte-identical** (`RH_PLAYTEST_DISTRICTS=1 bash scripts/sim/rh_playtest.sh 110 <label> guide full`, then `cmp` against `Saved/baselines/ind_full_playtest.txt`, or the newer baseline E5/E6 leave).
- The bot fits a car only under `lifts=1` (`RHPlaytest.cpp:935`) and never lays a line, so no transit entry is ever appended.
- The output keeps only `[Playtest]` lines, which carry no byte counts.
- Also record and compare the default (districts off) run at your own HEAD before changing anything; no baseline is named for it (the D7 REFRESH notes the same gap).
- `-RHCommuteAudit` margins are unchanged by construction (no car).

**`lifts=1` playtest** (outside the byte-identical set). Its assertions (`RHPlaytest.cpp:1041-1045`, `LiftsReport` after it) must pass at 200 W: no robot below at charge 0, mass conserved, thresholds, at least one delivery and one build below.
- UNCERTAIN, a real risk (D5 R1 repeats): 200 W of car makes `RequestChargeWh`'s free-energy rule (`W:12916`, `GenW > LoadW`) fail more often once the bank (`W:12911`) is empty, and priority-2..5 machines (the Borer and the Forge) shed more.
- If a charge-0 or no-delivery failure appears, **stop and raise it as a director question** on the placeholder draw or priority. Do not tune the rows yourself.

---

## 6. Click test, captures, bench
- **The click test** (`RH_CLICKTEST_RES=1440x860 bash scripts/unreal/rh_clicktest.sh`) must pass 3 times, for two reasons:
  - D6e changes the section's lift label and legend text;
  - `tram.setup` (`RHClickTest.cpp:2153-2186`) fits a car without pinning the bank (the REFRESH's `:1973` anchor is stale), so its colony now draws 200 W and may scrape at night.

  No step reads a wait value. `section.traffic` (`:2539-2625`) checks refreshes, arrows and the legend's length (`:2609`); a shorter legend still satisfies `LegendText.Len() < LegendFullText.Len()`. No new click step is required.
- **Captures:** the TRAFFIC section of a fitted shaft before and after a dark sol (the bar fills against hoist capacity; the label reads `... / 200 places · wait N s`). Look at them.
- **`RH.DeepBench` before and after.** It fits 12 cars (`RHCheats.cpp:1809-1816`) and pins the bank only once at setup (`:1777`), so it now draws 2.4 kW and its lift waits can move. Frame budgets should not move, but record them.
- The RedHopeSim dylib changes (sim work), so the presentation-only "dylib sha unchanged" check does **not** apply.

---

## 7. Deviations the builder must NOT take
- Do not use the draft's rows (40 W, 2 W, priority 8) or name the suite `-liftpower`.
- Do not add `Debug_SetTransitPowerScale` or any switch that keeps `-transit` green by turning power off. The bank pin is the mechanism.
- Do not change `GetShaftCapacityPlaces`, `ComputeTransitWaitSec`, TR4's numbers or the robot reserve reads.
- Do not change the third argument of `StepDepthWarmth`, `CoolingW` or `CoolingFloors`.
- Do not put the block mid-file, refuse before teardown, or reuse `OutError`. It is a framed tail block that repairs.
- Do not take "v51" or a fixed number: take the next free version.
- Do not touch D5's order for a colony with no transit entry (no stable-sort swap, no tie-break added for buildings).
- Do not add a tram robot edge, a ride-speed effect, a rating or arrivals effect, or a Hope term.
- Do not edit the shaft card or other UI beyond the section bar, label and legend (SP5).
- Do not edit the design doc. Log decisions in `docs/design-decisions.md`.

---

## 8. Spec problems found (details in the structured result)
- **SP1 (blocking):** at Served 0 a tram's capacity `TramPlacesPerSol x 0 / 1000` is 0, and `ComputeTransitWaitSec` returns **0** for `Cap <= 0` (`T:247`). A dark sol would clear the tram's wait, the reverse of the design. Fix: floor the tram capacity at 1. A load of 2 or more then reads `MaxWaitSec`; at Served 1000 nothing changes.
- **SP2 (minor):** the spec does not say how a shed lift or tram counts. Today's `else` branch (`W:8654-8657`) would add it to `ShedFloors`. Use `ShedFloors` for Kind 1 only and count transit in `ShedCount`. The POWER tile (`RHCommandDeckPanels.cpp:409`) then calls a shed lift a "machine": acceptable placeholder copy; log it.
- **SP3 (minor):** a slot can exceed one sol of ms (a step that straddles a boundary, and float drift in the agent band), and the load repair "ms above one sol" would then fire on a legitimate file. Clamp at write.
- **SP4 (minor):** stale anchors and claims; see the anchor table below.
- **SP5 (minor):** the shaft card and `RH.Transit` keep showing nominal capacity. This is a follow-up, not D6e.
- **SP6 (blocking for scope):** the served-capacity bar is a D6e deliverable by D8's DIRECTOR ANSWER, but neither the D6 doc nor its REFRESH lists it.

## 9. Anchor table (spec or REFRESH citation, then HEAD d842dd0)
| Cited | At d842dd0 |
|---|---|
| `StepPower` "(wt) :8286" / REFRESH `W:8461` | `W:8523` |
| `FLoadEntry` REFRESH `W:8473` | `W:8535` |
| scraping gate "(wt) :8385" / `W:8560` | `W:8623` |
| walk gate "(wt) :8387" / `W:8562` | `W:8625` |
| `Loads.Sort` `W:8566` | `W:8629-8641` |
| `ShortMask` write `W:8595` | `W:8660-8663` (ShedFloors `W:8656`, ShedCount `W:8658`) |
| `StepDepthWarmth(...)` `W:8606` | `W:8673` (body `RHSimDepth.cpp:85`) |
| cooling loop | `W:8577-8599` |
| `StepTransit` before `StepPower` `W:675/690`, `W:847/862` | unchanged |
| wait computation `RHSimTransit.cpp:201` / REFRESH `:375` | `T:375` |
| tram wait REFRESH `:384` | `T:384` |
| `GetShaftCapacityPlaces` `:68-76` / REFRESH `:235-243` | `T:235-243` |
| `ComputeTransitWaitSec` | `T:245-253` (returns 0 for Cap <= 0 at `T:247`) |
| `RequestChargeWh` `:12673-12679` / REFRESH `W:12821`, bank `:12844-12846`, `GenW > LoadW` `:12849` | `W:12888`; bank `W:12911-12915`; `GenW > LoadW` `W:12916` |
| `-transit` pins `:76/:82/:463/:529`, jump `:435` / REFRESH `:77/:83/:464/:530`, `:436` | unchanged: `:77`, `:83`, `:464`, `:530`; jump `:436` |
| `-trams` pins REFRESH `:17/:152/:158` | unchanged |
| `-deepcommute` pins REFRESH `:142/:148` | `:143/:149` |
| DP12 REFRESH `:860`, fit `:870` | unchanged; still the last case |
| `-scale` lifts / trams REFRESH `RHSimCommandlet.cpp:16324/:16860` | `:16428` / `:16964` |
| playtest lifts REFRESH `RHPlaytest.cpp:932-950` | `:926-950` (the fit at `:935`) |
| click test REFRESH `RHClickTest.cpp:1973` "D8's section setup" | `:2168`, inside D6d's `tram.setup` (`:2153`) |
| `'RHE4'` write REFRESH `W:13866` | `W:13933` |
| last tail reader REFRESH `W:14660` | `W:14743` (`ReadMarketBlock`) |
| version check | `W:13968-13971` |
| `RHSaveVersion` | `W:37` (v55) |
| MX1 / MX9 REFRESH `RHSimMarketSuite.cpp:565/:1359` | `:564` / `:1359` (both masked by `(N bytes`) |
| CV8 / DP11 / EC1 / DC9 size lines | `RHSimCavernSuite.cpp:739`; `RHSimDepthSuite.cpp:787`; `RHSimEconomySuite.cpp:493`; `RHSimDeepCommuteSuite.cpp:713` |
| section bar (D8 R7) | `RHSectionVisualizerSubsystem.cpp:122-131`, `:1464`, legend `:1600` |
