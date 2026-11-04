# Balance pass 1 (A, B, D): build brief (2026-11-04)

Written for the one builder of `balance-wf.js` at HEAD `ffbee4c80ec3fb4147ff9cc143f690ca19b27513` (P2 `d1ae860` and its handoff committed; save v60; tree clean). Every anchor below gives its symbol and its line at `ffbee4c`. The edits shift each other's lines, so **re-find every anchor by symbol (`git grep -P`) before each edit; never trust a line number after the first hunk lands.** The one exception is `docs/data/RH_Config.csv`: its existing rows keep their line numbers, because the only new row is appended at the end (3.1).

**Authority, highest first:** `docs/qa/2026-10-26/preflight/ORCHESTRATOR-RULINGS.md` (rulings 7, 22, 23, 26 for how the gate runs) > James's choice of 2026-11-03 (1.1) > this brief > `docs/qa/2026-11-03/balance/balance-scope.md` and `/Volumes/Unreal/red_hope/wip-backups/balance-gate/preview.md` (reasoning and predictions only).

**GO (3 options):** A (transport food 50 -> 25 kg, the manifest text, 27 stale tags), B (`HopeMilestonesMax` 15 and one `min()`), D (`EarthTensionDemandThreshold` 60 -> 45).
**Main scene: 115 steps, unchanged. Deepbench: 9, unchanged. Battery: 64 runs, no new suite; lines move (section 7). Save stays v60. No `Source/RedHope` file changes. Blockers: none.**

`WORK` = `/Volumes/Unreal/red_hope/wip-backups/balance-gate/work` (one subfolder per round: `WORK/r0`, `WORK/r1`, ...; the gate's helper scripts in `WORK/checks/`, Appendix B).

| File | What changes |
|---|---|
| `docs/data/RH_Config.csv` | A: `ColonistTransportFoodKgPerColonist` 50 -> 25 and its Notes; Notes of the 27 stale rows. B: new row `HopeMilestonesMax,15` (appended). D: `EarthTensionDemandThreshold` 60 -> 45 and its Notes; `EarthPreemptiveThreshold`'s Notes (a stale "(60)") |
| `docs/data/RH_ManifestItems.csv` | A: the Colonist Transport's `Effect` (1000 -> 500 kg, 50 -> 25 kg each) |
| `Source/RedHopeSim/Public/RHSimWorldSubsystem.h` | A, D: two header defaults. B: the new member, its getter, one comment line on `FRHHopeBreakdown` |
| `Source/RedHopeSim/Private/RHSimWorldSubsystem.cpp` | B: the loader line and the one `min()`. A: one stale comment |
| `Source/RedHopeSim/Private/RHSimCommandlet.cpp` | B: `-discovery` DS5 (the window sampled, one check updated, the ONE new check). A: `-arrivals` pins and literals, `-framing` a3's literal. D: `-solidarity` two printed expectations, comments in `-earth` and `-solidarity` |
| `Source/RedHopeSim/Private/RHSimCreditSuite.cpp` | A: CR1's transport literals |
| `Source/RedHopeSim/Private/RHSimCurrencySuite.cpp` | D: CU3's fixture tension 61.1 -> 46.1 |
| `Content/RedHope/Data/DT_Config.uasset`, `DT_ManifestItems.uasset` | by the sync only (section 4) |
| `docs/design-decisions.md` | the gate's entry (section 10) |

Nothing else in the repo changes.

---

## 1. Authoritative decisions

### 1.1 James's choice (2026-11-03)
- **"lets start with one after p2 finishes up"**: "one" is "make the game's choices bite", which is options **A, B and D** of `balance-scope.md` section 4, with B's cap at **15**.
- **A.** `ColonistTransportFoodKgPerColonist` 50 -> 25 (#28, bal1's only proposed value change: 25 = `LifeSupportDeficitCoverSols` 40 x 0.62 kg a sol, so the Big Arrival lands exactly its own 40-sol cover). The Colonist Transport's effect text 1,000 -> 500 kg. Clear the stale "Placeholder, director review" tag on the 27 rows the director already ruled (Notes only).
- **B.** A new config row `HopeMilestonesMax` = 15 and ONE `min()` on the Milestones line in `URHSimWorldSubsystem::GetColonyHope`, after the vault, first-born, first-harvest and discovery terms are summed. It answers D3a's open question (`docs/design-decisions.md:348`: "cap the Milestones line (like Furnishings/Views); let milestone Hope fade ...; or accept") with its first option. The seven milestone values (vault 5, first harvest 4, first-born 6, discoveries 2/2/3/8) do not change. Hope stays a derived index: nothing stores, spends or saves it.
- **D.** `EarthTensionDemandThreshold` 60 -> 45, so Earth's first demand lands inside a 110-sol session (about sol 75).

### 1.2 The preview's findings (`preview.md`, two `-RHConfig` playtests at `d1ae860`)
1. **D held.** Earth's transmission moved from sol 100.25 to 75.25 in both runs.
2. **The scope's B-preview command was wrong.** Zeroing the vault, harvest and first-born milestones left the line at 0: no discovery fired, the Big Arrival never landed, the colony ended at 7 people. Keeping only the first harvest (line max 4): the Big Arrival at sol 76.01, population 107, Hope 68-77 after it.
3. **Correction to the preview's mechanism (recorded, the conclusion stands).** A discovery does not need the FLOURISHING band. Research accrues while smoothed Hope is at or over `HopeDiscoveryThreshold` 85 (`StepDiscovery`, `RHSimWorldSubsystem.cpp:7615`; `IsResearchAccruing` `:7595`). With milestones 0, Hope never passed 81, so nothing accrued. The FLOURISHING exit is also 85, which is why the two read alike.
4. **The real B keeps today's line up to 15.** Today's line: 5 (sol 20), 9 (sol 23), 11 (32.17, CropStrain), 13 (36.46, RegolithCeramics), 16 (41.61, SubsurfaceBrine), 22 (44.06, the first-born), 30 (48.49, MicrobialLife). With the cap, Hope is identical to today's until sol 41.61, then lower by 1, 7, then 15.
5. **The steep lever is the settler count:** settlers a ship = floor((Hope - 70) / 30 x max(4, 0.2 N)).

### 1.3 Rules for every change
1. **Exactly these values:** 25, 15, 45. No other row's Value changes; Notes change only on the rows 3.1 lists.
2. **No save change.** `RHSaveVersion` stays 60 (`RHSimWorldSubsystem.cpp:37`). Config is not saved; Hope is derived.
3. **No new battery suite.** The cap's proof is ONE new check in `-discovery` (5.2).
4. **No `Source/RedHope` edit.** No click step reads Hope, the milestones, the transport food or Earth's demand (5.6).
5. **Never tune to pass.** If a check fails for a reason this brief does not name, section 7.4 says what may be done; anything else is a stop.
6. **Banned in new code:** a random draw, a player-facing string (the manifest text is a number change inside reviewed copy, 10), per-frame allocation.

### 1.4 Choices this brief makes, and why
1. **#28's tag is cleared too (28 tags in all, not 27).** James picked #28's value (option A) on 2026-11-03, which is the analysts' own test for "stale" ("each value was in the option the director picked"). Its Notes must be rewritten anyway ("50 kg is about 80 sols" becomes false). Tagged rows go 154 -> **126**. The docs entry lists it so the director can restore the tag with a one-line Notes edit.
2. **`HopeMilestonesMax` carries no "Placeholder, director review" tag**, for the same reason: its value is the director's pick. Its Notes say it is to be judged in hand-play.
3. **The new row is appended at the end of `RH_Config.csv` (line 324),** after `AtriumLeakPermille`, as D6e and D7 appended theirs. Every existing line number (bal1-bal3, the scope) stays valid.
4. **Member, loader and getter sit beside the Milestones line's first term (`HopeVaultMilestone`)**, written exactly as `HopeFurnishingsMax` is: `double ... = 15.0; // config ...`, `FMath::Max(0.0, Defs->GetConfigScalar(FName("..."), ...))`, `double Get...() const { return ...; }`.
5. **The pin is folded into the new check.** `HopeFurnishingsMax` is pinned by `-furnish` F0 (`RHSimCommandlet.cpp:3076-3077`, the sim's value against a literal). The new check pins `HopeMilestonesMax` more strictly: the synced DT row alone (`GetConfigTableScalar`, no override, no fallback) must read 15 and the sim must hold that same value. That keeps the gate at ONE new check line.
6. **The new check sees the whole window, not only its end.** DS5's `RunSols(6.0)` becomes the same 120 steps with the Milestones line and the uncapped sum sampled after each step. `GetColonyHope` is a pure derived read that `StepHope` already calls every step, so sampling moves no sim number.
7. **No in-repo check guards header-default/CSV parity, and none is added in the repo.** Every config read goes through the DT row when it exists (`GetConfigScalar`, `RHDefinitionsSubsystem.cpp:811-821`), so a header default that differs from its CSV row changes no run while the row is synced. `-levers` LV0 compares the CSV with the live DT for its 13 rows, not with the header; ARRV P0 and the new check compare the DT with literals. This gate guards its own three rows with a **gate-time static check** outside the repo (`WORK/checks/parity_check.py`, 5.4), and its mutation fails it (6). An in-repo parity check is a follow-up for the orchestrator.
8. **D forces two test re-anchors; they are not tuning.** Hand-set tensions chosen just over the old line (60) now sit over the new one in a way that changes what the test hears:
   - `-currency` CU3's twins start at 61.1. A Defy leaves 49.16 and a DefyIssue 59.16, both now at or over 45, so the demand re-raises **in the same era step** (`StepUplink` runs before `StepEarth`, `EraStep` `:854`) and each twin hears two alerts: CU3's one-alert check (`RHSimCurrencySuite.cpp:811-813`) would fail. The fixture moves to **46.1** (the line + 1.1, as 61.1 was), which keeps the test's meaning: one answer, one alert.
   - `-solidarity` prints `pending=0` after a Defy from 67 and calls the Comply's 55 "below re-demand". Under 45 both answers leave tension over the line, so the demand re-arms at once. The suite asserts nothing (print-only); its two expectation texts are corrected to the new truth (3.5).
9. **No D pin is added.** `EarthTensionDemandThreshold` has no getter and no pin today. Its value is proven by the DT read-back (4), by the moved demand lines (`-icelow` and `-arrivals` print "tension 45", which only the synced DT row can produce), by `bal.parity`, and by the control playtest (8.3). A pin is a follow-up.
10. **`-scale` is not run** (it is not in the battery and this gate's script does not run it). Its digests will move: it delivers 80 Colonist Transports (A) and finds all four discoveries at smoothed Hope 62-85, under the clamp (B). The docs entry says the next `-scale` run re-baselines them. No hot path changes: one `min()` per `GetColonyHope`.
11. **The Earth panel's tension bar still turns red above 60** (`RHCommandDeckPanels.cpp:2333`, a hard-coded copy of the old line). It is presentation and not required; changing it would touch `Source/RedHope`. Recorded as a follow-up.

---

## 2. Anchors at HEAD `ffbee4c` (by symbol; line at HEAD)

### 2.1 The three rows and the new one

| Row | CSV (`docs/data/RH_Config.csv`) | Header default (`RHSimWorldSubsystem.h`) | Getter (`.h`) | Loader (`RHSimWorldSubsystem.cpp`, in `OnWorldBeginPlay` `:96`) | Readers |
|---|---|---|---|---|---|
| `ColonistTransportFoodKgPerColonist` | `:192` `50,kg` (#28) | `:4434` `double ColonistTransportFoodKgPerColonist = 50.0; // provisions landed with each colonist` | `:1650` `GetColonistTransportFoodKgPerColonist` | `:330` `FMath::Max(0.0, Defs->GetConfigScalar(...))` | `ApplyManifestItemEffect` (`:2460`): `:2540` `FoodKg = ... * ColonistTransportColonists`, alert D129 `:2546-2548` (comment `:2537-2539` "fifty kilos"); `StepBigArrival` (`:7206`): `:7242` `FoodKg = ... * Housed`, alert D132; `GetBigArrivalSupport` (`:7254`): `:7266` the food runway, `:7275` the food leg's extra stock. Through `GetBigArrivalSupport`: `RHSimDistricts.cpp:1308-1313` (the City demand notes), `RHPlaytest.cpp:596` |
| `EarthTensionDemandThreshold` | `:98` `60,points` | `:4733` `double EarthTensionDemandThreshold = 60.0;` | none | `:471` `Defs->GetConfigScalar(...)` (no clamp) | `StepEarth` (`:10229`): `:10243` `if (!bEarthDemandPending && EarthTension >= EarthTensionDemandThreshold)` - the ONLY reader. A pending demand is cleared only by `ResolveDilemma` (`:10640`, `bEarthDemandPending = false` `:10647`); the embargo reads `EarthPreemptiveThreshold` (`:10409`), not this row |
| `HopeMilestonesMax` (new) | append as `:324` | after `:4400` `double HopeVaultMilestone = 5.0;` | after `:888` | after `:377` `HopeVaultMilestone = Defs->GetConfigScalar(...)` | `GetColonyHope` only (the new `min()`) and the new check |
| `HopeFurnishingsMax` (the pattern) | `:169` `6,Hope` | `:4265` `double HopeFurnishingsMax = 6.0;         // config HopeFurnishingsMax` | `:888` `double GetHopeFurnishingsMax() const { return HopeFurnishingsMax; }` | `:276` `HopeFurnishingsMax = FMath::Max(0.0, Defs->GetConfigScalar(FName("HopeFurnishingsMax"), HopeFurnishingsMax));` | `GetColonyHope` `:6877` `Out.Furnishings = FMath::Min(Raw * PerHead, HopeFurnishingsMax); // BM-4: bonus-only, capped`; `GetFurnishingsHope` `:12432`; `PreviewFurnishEffect` `:12464-12465`. Pinned: `-furnish` F0 `RHSimCommandlet.cpp:3076-3077` ("F0 config: refund 0.75 (ruling 5a), Furnishings cap 6"); read by `:6080-6082`, `:6336` |

Config plumbing: `-RHConfig=Name=Value,...` is parsed at `RHDefinitionsSubsystem.cpp:46-62` and wins in `GetConfigScalar` (`:811-821`), for any name, including one with no row. `GetConfigTableScalar` (`:824-831`) reads the DT row alone. The DT asset is loaded headless (`ConfigTablePath` `/Game/RedHope/Data/DT_Config.DT_Config`, `RHDefinitionsSubsystem.h:239`, loaded at `.cpp:15`), so the battery reads the synced DT. DT_Config holds all 322 CSV rows at HEAD (checked); it has no `HopeMilestonesMax`.

### 2.2 `GetColonyHope` (`RHSimWorldSubsystem.cpp:6479`)
- `:6485-6488` vault; `:6489-6492` first-born; `:6493-6496` first harvest; `:6497-6499` comment; `:6500-6506` the discovery loop (`Out.Milestones += Row->HopeBonus;` `:6504`, loop close `:6506`); `:6507` blank; `:6508` `const int32 Pop = Colonists.Num();`.
- **The `min()` goes between `:6506` and `:6508`** (3.4).
- Total `:6974-6976` (`FMath::Clamp(... + Out.Milestones + ..., 0.0, 100.0)`).
- `StepHope` (`:6980`) reads `GetColonyHope().Total` every step.
- `FRHHopeBreakdown` (`RHSimWorldSubsystem.h:2175`; `Milestones` on `:2177`).

### 2.3 Every reader of `FRHHopeBreakdown::Milestones` (all see the capped value)
- `Source/RedHope`: `RHCheats.cpp:1072-1076` (`RH.Hope`, developer), `RHPlaytest.cpp:2001-2007` (the CREW line), `RHCommandDeckPanels.cpp:1071` (the Crew panel reads the breakdown but prints no milestone figure).
- Suites: `RHSimCommandlet.cpp` `-growth` G2 `:784-791` (5 -> 11), `-discovery` DS4 `:1004-1007` (9), DS5 `:1025-1028` (20 today), `-surfacehab` `:2315`, `:2408` (0, 5), `-alive` `:4467-4470` (5 -> 9), `-percapita` `:5964-5965` (0), `-scale` `:15389` (digest), `:15457` (print); `RHSimAtriumSuite.cpp:251-259`, `:949-951` (rebuild Total from the fields); `RHSimLeversSuite.cpp:2052` (print).
- Developer text that still shows a discovery's own bonus after the cap: the sim log `=== DISCOVERY: %s (+%.0f Hope milestone) ===` (`StepDiscovery`, `:7674-7676`) and `RH.Discoveries` (`RHCheats.cpp:1694`). Not changed; docs entry.

### 2.4 Suite lines that print or assert these rows' values

| Suite | Anchor | What it holds today | Option |
|---|---|---|---|
| `-arrivals` P0 | `RHSimCommandlet.cpp:6435-6439` (`Pins[]`; the row at `:6436`), `:6450` | DT pin `ColonistTransportFoodKgPerColonist` 50.0; sim pin 50.0 | A |
| `-arrivals` (a) | `:6668-6669` comment; `:6677-6679`; `:6680-6681` | `1000.0` kg, text "(20 x 50)"; alert `Contains("1000 kg")` | A |
| `-arrivals` d | `:6884-6885` | `100000.0 - FoodDrawn + 5000.0`, text "+ 5000 for the hundred" | A |
| `-arrivals` g0, g2 | `:7097` `WaveFood`, `:7136-7143`, `:7188-7208` | derived from the getter (prints move, checks hold) | A |
| `-arrivals` h0, h1 | `:7235` `WaveFoodH`, `:7286-7287`; comment `:7289-7290` "(the wave brings 5,000 more)" | derived; h1's food leg keeps 2,659 kg against 2,598.4 needed (the tightest A margin in the battery) | A |
| `-credit` CR1 | `RHSimCreditSuite.cpp:219` comment, `:237`, `:239`, `:240` | `== 1000.0`, alert "with 1000 kg of provisions.", text "20 and 1,000 kg" | A |
| `-framing` a3 | `RHSimCommandlet.cpp:14933-14937` (`:14936`) | the DT's Colonist Transport `Effect` equals "+20 ... +1000 kg Food (50 kg each) ..." | A |
| `-discovery` DS5 | `:1020` `RunSols(6.0);`, `:1024-1028`, `:1029-1031` | Milestones = 5 + the found rows' bonus (20) | B |
| `-earth` | `:1200` comment "crosses 60"; `:1201-1204` | jumps 7.2 -> 62; print "(expect >=60, 1)" stays true | D (comment only) |
| `-solidarity` | `:1282-1285`, `:1287-1293` (defy print), `:1300-1302`, `:1306-1309` (comply print) | "pending=%d ... (expect +20, 45, 0, up, <1)"; "(expect 1, 5, 0, 55 = 100 clamped - 45 relief, below re-demand)" | D |
| `-currency` CU3 | `RHSimCurrencySuite.cpp:785`, `:787`, `:790`; alerts `:811-813` | twins from tension 61.1; one alert each | D |
| `-currency` CU8, CU9, CU12 | CU8 `:2210` (start tension 59.0 `:2276`; the demand checks `:2502`, `:2504`), CU9 `:1578-1612` (DefyIssue from 70), CU12 `:2178-2208` (twins from 60.3) | hold under 45 (pending in time for every answer; CU9's "a demand at once" from 68 still true; CU12's checks read no alert) | D (lines only) |
| `-levers` LV2 | `RHSimLeversSuite.cpp:698-720` | `Near(Ten, 55.0, 0.5)`, one COMPLIED line: hold | D (lines only) |

---

## 3. Exact changes, per file

### 3.0 Build order
1. The CSVs (3.1, 3.2), then run `csv_verify.py` (Appendix B) until it prints PASS.
2. The header and the sim (3.3, 3.4).
3. The suites (3.5-3.7).
4. Build once with `-DisableAdaptiveUnity`, fix any error (`-Wshadow` is an error; the new names below are unused anywhere at HEAD), then build normally for the final dylibs. The header changes, so both modules recompile.
5. The DataTable sync (section 4), then the gate.

### 3.1 `docs/data/RH_Config.csv`
Edit by line-anchored substring replacement: each replacement must match exactly once on its named line, and no other byte of the file changes. Keep LF endings and the final newline.

**(a) Values and their Notes.**
- **`:192` `ColonistTransportFoodKgPerColonist` (A).**
  - Value `50` -> `25`.
  - In its Notes, replace `50 kg is about 80 sols of one person's ration. Placeholder, director review` with:
    `25 kg is about 40 sols of one person's ration (0.62 kg a sol). Balance pass 1 (2026-11-04, option A, the director's choice of 2026-11-03): 50 -> 25, so the Big Arrival lands exactly its own 40-sol cover (LifeSupportDeficitCoverSols 40 x 0.62 kg a sol) and a colony with no farm no longer passes its food check`
- **`:98` `EarthTensionDemandThreshold` (D).**
  - Value `60` -> `45`.
  - In its Notes, replace `(the hook Gate C resolves via Comply/Defy)` with:
    `(the hook Gate C resolves via Comply/Defy). Balance pass 1 (2026-11-04, option D, the director's choice of 2026-11-03): 60 -> 45, so Earth's first demand lands inside a 110-sol session (about sol 75 at 0.6 a sol; it was sol 100). Measured from the demand, a Defy (-12) and a DefyIssue (-2) re-raise it at once in the same windows as before (answered 20 and about 3.3 sols late); a Comply (-45) now re-raises it only if answered at tension 90 or more`
- **`:132` `EarthPreemptiveThreshold` (Notes only).** Replace `higher than the Solidarity demand (60)` with `higher than the Solidarity demand (EarthTensionDemandThreshold, 45 since balance pass 1)`.

**(b) The new row (B), appended as line 324** (after `AtriumLeakPermille`):
```
HopeMilestonesMax,15,Hope,"Balance pass 1 (2026-11-04, option B; D3a's first option, the director's choice of 2026-11-03): cap on the summed Milestones line of Hope - the vault, the first-born, the first harvest and every discovery's HopeBonus are added as before, then the line is min(sum, this). Each milestone still lifts Hope when it lands until the line reaches the cap (about sol 42 in the 110-sol playtest: the vault 5, the first harvest 4, then the first discoveries); a later one adds no Hope, and its rewards and unlocks still land. Bonus-only and clamped at 0 on read, like HopeFurnishingsMax and HopeViewsMax. To be judged in hand-play"
```

**(c) The 27 stale tags (A, Notes only).** On each line in Appendix A, replace the substring `Placeholder, director review` with `Director ruling <ruling> (balance pass 1, 2026-11-04, cleared the stale placeholder tag)`, where `<ruling>` is that row's text in Appendix A. Line 174 (#12) is the one exception: there the tag is mid-Notes; Appendix A gives its whole replacement. No new text contains a double quote.

**(d) Verify** (Appendix B, `csv_verify.py`), which must print `bal.csv PASS (323 rows, 126 tagged, 30 Notes edited)`:
- the rows and their order are HEAD's plus `HopeMilestonesMax` at the end;
- only the three Values differ;
- Notes differ on exactly 30 rows: the 27, #28, `EarthTensionDemandThreshold`, `EarthPreemptiveThreshold`;
- no tag remains on the 28;
- 126 rows stay tagged;
- LF endings and a final newline.

Also: `git -C /Volumes/Unreal/red_hope/red_hope diff -U0 -- docs/data/RH_Config.csv | grep -cP '^-(?!--)'` = **30** and `... | grep -cP '^\+(?!\+\+)'` = **31**.

### 3.2 `docs/data/RH_ManifestItems.csv` (A)
Line 13, `ColonistTransport`, the `Effect` field only:
- old: `+20 colonists into certified housing; +1000 kg Food (50 kg each); everyone stays aboard, safe, if the beds are not free`
- new: `+20 colonists into certified housing; +500 kg Food (25 kg each); everyone stays aboard, safe, if the beds are not free`

Nothing else in the file changes (`DisplayName`, `Mass_kg` 8000, `Price_mcr`, `Notes` stay).

### 3.3 `Source/RedHopeSim/Public/RHSimWorldSubsystem.h`
1. **Getter**, a new line straight after `:888` (`GetHopeFurnishingsMax`):
   ```cpp
   	double GetHopeMilestonesMax() const { return HopeMilestonesMax; } // balance pass 1 (B): the Milestones line's cap (the suites read it)
   ```
2. **`FRHHopeBreakdown`**, a new comment line straight before `:2177` (`double Base = 0, Housing = 0, Rooms = 0, Jobs = 0, Milestones = 0, Comforts = 0;`):
   ```cpp
   		// Milestones (balance pass 1, B): the vault, first-born, first-harvest and discovery terms summed, then capped at HopeMilestonesMax.
   ```
3. **Member**, a new line straight after `:4400` (`double HopeVaultMilestone = 5.0;`):
   ```cpp
   	double HopeMilestonesMax = 15.0;      // config HopeMilestonesMax (balance pass 1, B): the summed Milestones line's cap
   ```
4. **`:4434`** becomes:
   ```cpp
   	double ColonistTransportFoodKgPerColonist = 25.0; // provisions landed with each colonist (balance pass 1, A: was 50)
   ```
5. **`:4733`** becomes:
   ```cpp
   	double EarthTensionDemandThreshold = 45.0; // balance pass 1 (D): was 60
   ```

### 3.4 `Source/RedHopeSim/Private/RHSimWorldSubsystem.cpp`
1. **Loader**, a new line straight after `:377` (`HopeVaultMilestone = Defs->GetConfigScalar(FName("HopeVaultMilestone"), HopeVaultMilestone);`):
   ```cpp
   	HopeMilestonesMax = FMath::Max(0.0, Defs->GetConfigScalar(FName("HopeMilestonesMax"), HopeMilestonesMax)); // balance pass 1 (B): clamped at 0, like HopeFurnishingsMax
   ```
2. **The one `min()`**, in `GetColonyHope`, between the discovery loop's closing `}` (`:6506`) and the blank line before `const int32 Pop = Colonists.Num();` (`:6508`):
   ```cpp
   	// Balance pass 1 (B, 2026-11-04; D3a's first option): the summed Milestones line is capped, like Furnishings and Views. Each
   	// milestone still lifts Hope when it lands until the line reaches HopeMilestonesMax; the seven milestone values are unchanged.
   	Out.Milestones = FMath::Min(Out.Milestones, HopeMilestonesMax);
   ```
   - Applied once, to the whole line, after the sum and before `Total`.
   - It cannot make the line negative.
   - With `HopeMilestonesMax` at 1000 it returns the sum bit for bit (the control, 8.3).
3. **Comment `:2539`**: `// not there - with fifty kilos of provisions for every colonist it carries.` becomes `// not there - with ColonistTransportFoodKgPerColonist (25 kg since balance pass 1) of provisions for every colonist it carries.`

No other line in this file changes. `StepEarth` and `ResolveDilemma` are untouched.

### 3.5 `Source/RedHopeSim/Private/RHSimCommandlet.cpp`

**B, `-discovery` (`:836`), the DS5 block (`:1012-1032`):**
1. **`:1020` `RunSols(6.0);`** is replaced by:
   ```cpp
   		// Balance pass 1 (B): the same 120 steps RunSols(6.0) takes, with the Milestones line and the uncapped sum (the vault 5 + the
   		// found rows' bonus) sampled after each one, so the cap check below sees the whole window. GetColonyHope is a pure derived read
   		// (StepHope already reads it every step): sampling moves no sim number.
   		double MsLinePeak = 0.0, MsUncappedPeak = 0.0;
   		const int32 MsSteps = (int32)(6.0 * StepsPerSolH);
   		for (int32 MsStep = 0; MsStep < MsSteps; ++MsStep)
   		{
   			Step();
   			MsLinePeak = FMath::Max(MsLinePeak, Sim->GetColonyHope().Milestones);
   			MsUncappedPeak = FMath::Max(MsUncappedPeak, 5.0 + LogBonusSum());
   		}
   ```
2. **`:1024-1028`, the DS5 second-window check.** Its Milestones clause becomes the capped sum; its text says so. Replace `&& Near(Sim->GetColonyHope().Milestones, 5.0 + LogBonusSum(), 1e-9),` with `&& Near(Sim->GetColonyHope().Milestones, FMath::Min(5.0 + LogBonusSum(), Sim->GetHopeMilestonesMax()), 1e-9),`, and the `Printf` with:
   ```cpp
   				FString::Printf(TEXT("DS5 second window: found=%d [%s] next=%s (%.3f of the way) struct +%.0f (expect 100 x ceramics found = %d) milestones=%.1f (expect min(5 + the found rows' bonus %.1f, the cap %.1f) = %.1f)"),
   					Sim->GetDiscoveryLog().Num(), *LogText(), *Sim->GetNextDiscovery().ToString(), Sim->GetDiscoveryProgress(), StructGained, Ceramics * 100,
   					Sim->GetColonyHope().Milestones, 5.0 + LogBonusSum(), Sim->GetHopeMilestonesMax(), FMath::Min(5.0 + LogBonusSum(), Sim->GetHopeMilestonesMax())));
   ```
3. **THE NEW CHECK**, inside the same `{ }` block, straight after the `DS5 pin` `Expect` (`:1029-1031`), before the block's `}` (`:1032`): 5.2.

**A, `-arrivals` (`:6411`):**
- `:6436`: `{ TEXT("ColonistTransportFoodKgPerColonist"), 50.0 }` -> `{ TEXT("ColonistTransportFoodKgPerColonist"), 25.0 }`.
- `:6450`: `Near(Sim->GetColonistTransportFoodKgPerColonist(), 50.0, 1e-9)` -> `Near(Sim->GetColonistTransportFoodKgPerColonist(), 25.0, 1e-9)`.
- `:6668`: `// (a) The Colonist Transport. Twenty colonists and fifty kilos of food each; it` -> `// (a) The Colonist Transport. Twenty colonists and twenty-five kilos of food each (balance pass 1, A); it`.
- `:6677`: `Near(Sim->GetStock(NFood) - FoodA, 1000.0, 1e-6)` -> `Near(Sim->GetStock(NFood) - FoodA, 500.0, 1e-6)`.
- `:6678`: `+%.0f kg Food (20 x 50)` -> `+%.0f kg Food (20 x 25)`.
- `:6680`: `Alerts[0].Contains(TEXT("1000 kg"))` -> `Alerts[0].Contains(TEXT("500 kg"))`.
- `:6884`: `100000.0 - FoodDrawn + 5000.0` -> `100000.0 - FoodDrawn + 2500.0`.
- `:6885`: the text `+ 5000 for the hundred` -> `+ 2500 for the hundred`, and its last argument `100000.0 - FoodDrawn + 5000.0` -> `100000.0 - FoodDrawn + 2500.0`.
- `:7290`: `(the wave brings 5,000 more)` -> `(the wave brings 2,500 more)`.
- g0, g2, h0 and h1 derive from the getter: no edit.

**A, `-framing` a3 (`:14936`):** the literal becomes `TEXT("+20 colonists into certified housing; +500 kg Food (25 kg each); everyone stays aboard, safe, if the beds are not free")`. a3's own text (`:14937`) is unchanged, so `framing.log` does not move.

**D, `-earth` (`:1167`), comment only:**
- `:1200` `// 3) DEMAND generated once tension crosses 60: jump it up.` -> `// 3) DEMAND generated once tension crosses EarthTensionDemandThreshold (45 since balance pass 1; it was 60): jump it up.`
- The print at `:1203` ("(expect >=60, 1)") stays: 62 is still >= 60, and `earth.log` does not move.

**D, `-solidarity` (`:1237`):**
- `:1291`, the `SOLID defy` text: `(expect +20, 45, 0, up, <1)` -> `(expect +20, 45, 1 - tension 55 is at or over the demand line 45, so a new demand follows at once (balance pass 1), up, <1)`.
- `:1300` comment `// Raise a second demand for the Comply branch.` -> `// Raise tension for the Comply branch (under balance pass 1's 45 the Defy already re-armed the demand; the +65 still drives the embargo).`
- `:1307`, the `SOLID comply` text: `(expect 1, 5, 0, 55 = 100 clamped - 45 relief, below re-demand)` -> `(expect 1, 5, 0, 55 = 100 clamped - 45 relief; at or over the demand line 45, so a new demand follows (balance pass 1))`.
- `:1284` ("(expect 1, >=60)") stays (67 >= 60).

### 3.6 `Source/RedHopeSim/Private/RHSimCreditSuite.cpp` (A, CR1)
- `:219` comment: `+20 with 1,000 kg.` -> `+20 with 500 kg (balance pass 1, A).`
- `:237`: `Sim->GetStock(FName("Food")) - Food1 == 1000.0` -> `== 500.0`.
- `:239`: `TEXT("A COLONIST TRANSPORT HAS LANDED — 20 colonists disembark into the vault, with 1000 kg of provisions.")` -> `... with 500 kg of provisions.")` (keep the em dash byte for byte).
- `:240`: `a Colonist Transport 20 and 1,000 kg` -> `a Colonist Transport 20 and 500 kg`.

### 3.7 `Source/RedHopeSim/Private/RHSimCurrencySuite.cpp` (D, CU3)
- `:785` comment: `(tension 61.1, identity 0, a demand pending)` -> `(tension 46.1 - the demand line 45 + 1.1, as 61.1 was at 60 before balance pass 1 - identity 0, a demand pending)`.
- `:787`: `Sim->Debug_SetRequisitionInputs(0.0, 61.1);` -> `Sim->Debug_SetRequisitionInputs(0.0, 46.1);`.
- `:790`: the text `a demand pending at tension 61.1, identity 0, saved` -> `a demand pending at tension 46.1, identity 0, saved`.

Why this is enough: the Defy twin ends at 34.16 and the DefyIssue twin at 44.16 (under 45 by 0.84), so neither re-raises. The check bodies (`:811-829`) and the issue-roll block after them (`:830-852`) derive their numbers from the state and are unchanged.

---

## 4. The DataTable sync (gate step 1)

`docs/dt-sync-checklist.md` is kept as history: its MCP `set_rows` route was superseded on 2026-07-16 by the headless route. That route is in `docs/PROJECT_STATUS.md:611` and `:685-688`, and `scripts/unreal/rh_sync_datatables.py` (fill from the CSV string; the row struct is kept; the row counts before and after are reported; an empty parse is never saved). `FRHConfigRow` and `FRHManifestItemRow` are unchanged, so no `RH_CREATE` is needed. Run it after the final build (the editor loads the built modules), with no other Unreal process running, and before the battery.

```bash
cd /Volumes/Unreal/red_hope/red_hope
W=/Volumes/Unreal/red_hope/wip-backups/balance-gate/work/r0
RH_TABLES=Config,ManifestItems RH_REPORT=$W/dtsync.txt \
  "/Volumes/Unreal/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd" /Volumes/Unreal/red_hope/red_hope/red_hope.uproject \
  -run=pythonscript -script=/Volumes/Unreal/red_hope/red_hope/scripts/unreal/rh_sync_datatables.py \
  -unattended -nosound -stdout > $W/dtsync.log 2>&1
cat $W/dtsync.txt
```
**Expected `dtsync.txt`, exactly two lines:**
```
ok   DT_Config              rows 322 -> 323
ok   DT_ManifestItems       rows 14 -> 14
```
Any `FAIL`, `WARN` or `SKIP` line, or other counts, is a stop.

**Read-back (the proof).** Run `WORK/checks/dt_readback.py` (Appendix B) the same way, with `-script=$WORK/checks/dt_readback.py` and `RH_REPORT=$W/dtread.txt`. It loads the two assets, never saves, and must print:
```
/Game/RedHope/Data/DT_Config.DT_Config rows 323
DT_Config HopeMilestonesMax = 15
DT_Config ColonistTransportFoodKgPerColonist = 25
DT_Config EarthTensionDemandThreshold = 45
/Game/RedHope/Data/DT_ManifestItems.DT_ManifestItems rows 14
DT_ManifestItems ColonistTransport Effect = +20 colonists into certified housing; +500 kg Food (25 kg each); everyone stays aboard, safe, if the beds are not free
```
If the Python API call fails (the script writes the error), say so and use the battery's own reads as the proof instead:
- the new check reads `HopeMilestonesMax` 15 from the DT;
- ARRV P0 reads `ColonistTransportFoodKgPerColonist` 25 from the DT;
- `-framing` a3 reads the new `Effect` from the DT;
- `icelow.log` and `arrivals.log` print `=== EARTH DEMAND PENDING (tension 45) ===`, which only the synced row can produce.

`git status` then shows both `.uasset` files modified. That is expected.

---

## 5. Tests, pins and the new check

### 5.1 Counts (state these exactly)
- **Main scene `CLICKTEST TOTAL pass=115 fail=0`**, two counted runs (`bal_click1`, `bal_click2`; quiet starts, ruling 26).
- **Deepbench PASS at 9** (`bal_deepbench`).
- **Battery** `bash scripts/sim/rh_battery.sh 10`: `BATTERY COMPLETE: 0 suite(s) not green`, **64** runs, no new suite, every log newer than both final dylibs.
- **Baseline pins unchanged:** "power: gen 50 W load 20 W", "deposit Regolith_A: 40645 kg" (`baseline.log`).

### 5.2 THE NEW CHECK (`-discovery`, DS5; the suite where the line passes 15: vault 5 + the four rows' 15 = 20)
Inserted straight after the `DS5 pin` `Expect`, inside DS5's block:
```cpp
			// Balance pass 1 (B, D3a's first option): the cap over the whole window. The vault 5 and the four rows' 15 sum to 20; the line
			// never reads more than HopeMilestonesMax, and the sim's cap is the synced DT_Config row (no override, no C++ fallback).
			const double MsCapDt = DefsSub->GetConfigTableScalar(FName("HopeMilestonesMax"), -1.0);
			const double MsLineEnd = Sim->GetColonyHope().Milestones, MsUncappedEnd = 5.0 + LogBonusSum();
			Expect(Near(MsCapDt, 15.0, 1e-9) && Near(Sim->GetHopeMilestonesMax(), MsCapDt, 1e-9) && MsUncappedPeak > MsCapDt && MsLinePeak <= MsCapDt
				&& Near(MsLineEnd, FMath::Min(MsUncappedEnd, MsCapDt), 1e-9),
				FString::Printf(TEXT("DS5 cap (balance pass 1): over the window's %d steps the Milestones line peaked at %.1f, never over HopeMilestonesMax %.1f (DT_Config %.1f), while the uncapped sum (the vault 5 + the found rows' bonus) peaked at %.1f; at the end the line is %.1f = min(%.1f, %.1f) (DT/CSV)"),
					MsSteps, MsLinePeak, Sim->GetHopeMilestonesMax(), MsCapDt, MsUncappedPeak, MsLineEnd, MsUncappedEnd, MsCapDt));
```
**Expected line:**
```
DISC ok: DS5 cap (balance pass 1): over the window's 120 steps the Milestones line peaked at 15.0, never over HopeMilestonesMax 15.0 (DT_Config 15.0), while the uncapped sum (the vault 5 + the found rows' bonus) peaked at 20.0; at the end the line is 15.0 = min(20.0, 15.0) (DT/CSV)
```
And the updated second-window line:
```
DISC ok: DS5 second window: found=4 [CropStrain,RegolithCeramics,SubsurfaceBrine,MicrobialLife] next=None (0.000 of the way) struct +100 (expect 100 x ceramics found = 100) milestones=15.0 (expect min(5 + the found rows' bonus 20.0, the cap 15.0) = 15.0)
```
`=== DISCOVERIES TEST END: 0 failure(s) ===`. DS4 (milestones 9) and every other DISC line are unchanged. DS5's instant Hope is at the 100 clamp both ways: 86.75 + 15 = 101.75 against 86.75 + 20 = 106.75.

### 5.3 Pins and literals updated (each now holds the new value)
- ARRV P0: DT pin and sim pin 25.
- ARRV a: +500 kg; the alert carries "500 kg".
- ARRV d: + 2500.
- CR1: 500 and its alert.
- `-framing` a3: the new `Effect` against the synced DT.
- The new DS5 cap: 15 from the DT.
- `-furnish` F0 is untouched (`HopeFurnishingsMax` 6).

### 5.4 Header-default/CSV parity: none exists in the repo; the gate's static check
No battery or repo check guards it (1.4 (7)). Gate step: `python3 -I $WORK/checks/parity_check.py Source/RedHopeSim/Public/RHSimWorldSubsystem.h docs/data/RH_Config.csv` must print
`bal.parity PASS HopeMilestonesMax 15, ColonistTransportFoodKgPerColonist 25, EarthTensionDemandThreshold 45` (Appendix B).

### 5.5 `bal.csv` (3.1 (d)): `python3 -I $WORK/checks/csv_verify.py /Volumes/Unreal/red_hope/red_hope`, PASS as given.

### 5.6 The click test
- No step in `Source/RedHope/Private/RHClickTest.cpp` reads Hope, the Milestones line, the transport food or Earth's demand (`grep -niP 'hope|milestone|tension|demand|food|GetBigArrival' RHClickTest.cpp`: none outside `RedHope.h`).
- The `manifest.ADD`/`REMOVE` steps (`:2012-2052`) press the first row's ADD and count manifest lines. The Colonist Transport row's sub-line is 2 characters shorter; no step reads it.
- A click session never reaches tension 45 (about sol 75).
- **Main 115, deepbench 9; `redhope_touched` false.** The RedHope dylib is still rebuilt (it includes the changed header), which is why both click runs and the deepbench run are part of the gate.

---

## 6. Mutations (freeze the final sources by sha256 first; each its own label; restore by copy, wait 2 s, touch, compare sha256, rebuild, confirm the installed dylib's code matches the final with `/Volumes/Unreal/red_hope/wip-backups/p2/work/checks/dylib_cmp.py` or reinstall the frozen final and confirm its hash)

The suite-alone run (the battery's own invocation):
```bash
cd /Volumes/Unreal/red_hope/red_hope
"/Volumes/Unreal/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd" /Volumes/Unreal/red_hope/red_hope/red_hope.uproject \
  -run=RHSim -sols=10 -discovery <EXTRA> -unattended -nosound -stdout > $WORK/r0/mut/<label>.log 2>&1
grep -c 'LogRedHopeSim: Error' $WORK/r0/mut/<label>.log; grep -P 'DISC (FAIL|ok): DS5|DISCOVERIES TEST END' $WORK/r0/mut/<label>.log
```

| Label | The one change | Build? | Expected (and nothing else differs from the final `discovery.log`'s DISC lines) |
|---|---|---|---|
| `bal_mut_cap` | delete `Out.Milestones = FMath::Min(Out.Milestones, HopeMilestonesMax);` in `GetColonyHope` | yes (one .cpp) | **2 FAILs:** `DISC FAIL: DS5 second window: ... milestones=20.0 (expect min(5 + the found rows' bonus 20.0, the cap 15.0) = 15.0)` and `DISC FAIL: DS5 cap (balance pass 1): over the window's 120 steps the Milestones line peaked at 20.0, never over HopeMilestonesMax 15.0 (DT_Config 15.0), while the uncapped sum ... peaked at 20.0; at the end the line is 20.0 = min(20.0, 15.0) (DT/CSV)`; `=== DISCOVERIES TEST END: 2 failure(s) ===`. Both lines are the cap's: the second-window clause now states the cap. Restore and rebuild as above. |
| `bal_mut_override` | no source change: `<EXTRA>` = `-RHConfig=HopeMilestonesMax=16` (the sim reads anything but the synced row) | no | **1 FAIL:** `DISC FAIL: DS5 cap (balance pass 1): ... peaked at 16.0, never over HopeMilestonesMax 16.0 (DT_Config 15.0) ...`. The second-window line passes (`milestones=16.0 (expect min(... 20.0, the cap 16.0) = 16.0)`); `1 failure(s)`. Proves the check's DT clause. |
| `bal_mut_parity` | a COPY of the final header (`$WORK/r0/mut/parity/RHSimWorldSubsystem.h`) with `double HopeMilestonesMax = 15.0;` -> `double HopeMilestonesMax = 16.0;`; run `parity_check.py <copy> docs/data/RH_Config.csv` | no | `bal.parity FAIL HopeMilestonesMax: header 16.0 != CSV 15`, exit 1. The repo header is never edited: touching it would force a full rebuild of both modules and break "every dylib newer than every changed source". **Say plainly in the report and the docs entry: no in-repo check guards header/CSV parity; while the DT row exists, the battery cannot see a header default that differs (1.4 (7)).** |

Record each label, its log, the line(s) it failed, and the restore hashes in the docs entry.

---

## 7. Battery lines expected to move

`python3 scripts/sim/diff_battery.py Saved/baselines/battery_post_p2 Saved/RHBattery 100000 > $WORK/r0/battery_diff.txt`. The third argument shows every line. The tool counts a changed line as one `-` and one `+`; "changed" below means one of each. Write `$WORK/r0/battery_moves.md`: every differing line, grouped by log, each assigned to A, B, D or the new check, with its reason. Wall-clock digits (EP4, X16, ARRV e) are named as such. No version or size line moves.

### 7.1 A (transport food 25 kg)

| Log | Lines (today -> after) |
|---|---|
| `arrivals.log` (about 16 changed) | `ARRV ok: P0 DT_Config pins: ... ColonistTransportFoodKgPerColonist=50 ...` -> `=25`. |
| | `=== COLONIST TRANSPORT: 20 colonists housed, +1000 kg Food ===` (3 lines, today's `:1171`, `:1204`, `:1242`) -> `+500 kg Food`. |
| | `ARRV ok: a 20 beds free of 20 needed: the transport lands - population 253 -> 273, +1000 kg Food (20 x 50), ...` -> `+500 kg Food (20 x 25)`. |
| | `ARRV ok: f the landing is one alert, ... "A COLONIST TRANSPORT HAS LANDED — 20 colonists disembark into the vault, with 1000 kg of provisions."` -> `500 kg`. |
| | `=== BIG ARRIVAL (Sol N): 100 settlers, +5000 kg Food (...)` (6 lines: Sol 57, 117 twice, 172, 194, 26) -> `+2500 kg Food`. |
| | `ARRV ok: d with their food: 104986.794 kg (expect 100000 - 13.206 eaten by the 426 already here + 5000 for the hundred = 104986.794)` -> `102486.794 ... + 2500 ... = 102486.794`. |
| | `ARRV ok: g0 food: the larder must hold 4920 kg (... less the wave's own 5000 kg); ... (runway 10.001 sols)` -> `7420 kg ... 2500 kg ... (runway 10.001 sols)`. |
| | `ARRV ok: g2 a sol with the larder at 1224 kg of the 5540 it needs: ...` -> about `2224 kg of the 8040` (0.4 x 8040 less a sol's draw). |
| | `ARRV ok: h0 the wave's food leg counts the 5000 kg it lands with beside the 1000 kg stored (6000), ...` -> `2500 kg ... (3500)`. |
| | Any further line that prints a food stock after a transport or wave lands is a knock-on of A: assign it with the stock it printed. |
| `credit.log` (2 changed) | `=== COLONIST TRANSPORT: 20 colonists housed, +1000 kg Food ===` -> `+500 kg Food`; `CREDIT ok: CR1 with 30 beds free a Crew Pod lands 4 colonists and 200 kg of Food, a Colonist Transport 20 and 1,000 kg, with HEAD's alerts` -> `... 20 and 500 kg, ...`. |
| `framing.log` | 0 (a3's text is constant; it passes against the synced DT). |
| All others | 0. `-districts`, `-depth`, `-deepdistricts` and `-economy` switch the Big Arrival off; no other suite lands a transport or a wave. |

### 7.2 B (`HopeMilestonesMax` 15)

| Log | Lines |
|---|---|
| `discovery.log` (1 changed, 1 new) | the DS5 second-window line (5.2) and the new `DS5 cap (balance pass 1)` line, which the new check adds |
| All others | 0. No other log's line passes 15 (max seen: `growth` 11, `arrivals` 11, `alive` 9, `citygrowth` 9 (two discoveries a run), `tiers` 9, `surfacehab` 5, `percapita` 0); vault + first-born + first harvest = 15 exactly, which the cap leaves alone. The scope's "probably -citygrowth" does not hold: its runs find at most two rows. |

### 7.3 D (`EarthTensionDemandThreshold` 45)

| Log | Lines |
|---|---|
| `icelow.log` (1 moved) | `=== EARTH DEMAND PENDING (tension 60) ===` after sol 100 -> `(tension 45)` after sol 75 (between today's sol-75 and sol-80 autosaves). I8's check counts "RUNNING LOW" only and holds. |
| `arrivals.log` (1 moved) | the same move, sol 100 -> 75. The 40-sol window's check counts "THE BIG ARRIVAL" only and holds. |
| `solidarity.log` (about 5) | a new `(tension 55)` straight after `=== SOLIDARITY: DEFIED - axis 20, tension 55 ... ===`; `SOLID defy: ... pending=1 ...` with the new text (3.5); today's `(tension 60)` near sol 12 goes; a new `(tension 55)` after `=== SOLIDARITY: COMPLIED ... tension 55 ===`, before `SOLID comply:`; `SOLID comply:` with the new text. |
| `levers.log` (about 10-12) | LV2's mirror of `-solidarity` (today's `:392-430`): `(tension 60)` -> a `(tension 55)` after DEFIED and another after COMPLIED; the drift crossing (today's `:565`, `(tension 60)`) -> `(tension 45)` earlier; a new `(tension 55)` after each of the six twin runs' `COMPLIED - 1 route(s) closed, axis -20, tension 55` (`:679`, `:759`, `:841`, `:921`, `:998`, `:1073`); any twin digest that prints the `demand %d` flag. |
| `currency.log` (about 20-40) | CU3: `the twins' start: a demand pending at tension 46.1 ...`; `CU3 DefyIssue against a plain Defy: ... tension 44.160 against 34.160 (+10), the multiplier 0.6475 against 0.7775 ...`; the issue roll's `tension 44.730` and its first rate step (self-derived); every `EARTH DEMAND PENDING` line whose tension or count changes (CU12's twins now re-raise at once at about 48 and 58 instead of at 60 later; each drift crossing at 60 becomes 45; answers that leave tension in [45, 60) add a line). The scope's estimate for D was 20-60 lines, most in `-currency` and `-levers`. |
| `earth.log`, `preemptive.log`, `market.log` | 0 expected: the tension is jumped to 62, 78 or 65, the demand stays pending or a Comply drops it to 20. |
| `export.log`, `credit.log`, `regionready.log` | 0: they set tensions without a step that raises a demand. |

**Totals, roughly:** A about 18 changed lines in 2 logs; B 2 in 1 log; D about 40-60 in 5 logs. The exact count is the run's.

### 7.4 A line or a FAIL this table does not name
- **It must be assigned or it is a stop.** Never wave a line through.
- **Allowed fixes:**
  - A failing literal that is today's 50 / 1,000 / 5,000 transport or wave food: update it to 25 / 500 / 2,500. The arithmetic must show it.
  - A failing check whose fixture tension was hand-set relative to the old line 60: move that tension by exactly -15, so it keeps its distance from the line (as CU3). Or correct a printed expectation that the new rule makes false (as `-solidarity`).
  - Each such fix goes in `battery_moves.md` and the docs entry.
- **Never allowed:**
  - changing any config value;
  - weakening or removing a check;
  - moving a tension that is relative to the embargo (75) or the clamp (100).
- **A B-caused FAIL anywhere but DS5 is not expected: stop.**
- **Outside the battery:** `-scale`'s digests and its transports' food lines move (1.4 (10)). Not run here; the docs entry says so.

---

## 8. The playtest bar, the predictions and the control (gate steps 6 and 7)

### 8.1 The run
`RH_PLAYTEST_DISTRICTS=1 bash scripts/sim/rh_playtest.sh 110 bal_full guide full`, then `python3 /Volumes/Unreal/red_hope/wip-backups/balance-gate/compare.py Saved/baselines/ind_full_playtest.txt Saved/RHPlaytest/bal_full.txt`.

**THE BAR (all must hold):**
- the Big Arrival lands by sol 110;
- at least 3 Q3 rounds (`ROUND   Q3` lines);
- nobody recalled (`REPORT crew: ... recalled 0`);
- the outpost bar PASS;
- 0 sim errors.

**If it fails,** stop the gate there: no click runs, no mutations. Return `director_questions` as plain lettered options with modelled numbers and a recommendation, for example:
- (a) cap 20 (the scope: "With a cap of 20: no gate moves");
- (b) B without D;
- (c) A and D only.

Never tune another row.

### 8.2 Predicted against actual (report every row)

| Figure (as `compare.py` prints it) | Today (`ind_full_playtest`) | Predicted |
|---|---|---|
| Milestones line, max / at the end | 30 / 30 | **15 / 15** |
| Discoveries | 32.17, 36.46, 41.61, 48.49 | the first three unchanged (B moves nothing before 41.61); the fourth about 48.5, unless smoothed Hope falls under 85 (`HopeDiscoveryThreshold`) after sol 42 (the static re-score keeps it at 92-95) |
| The first-born | 44.06 | about 44 |
| Hope sol 25..arrival | 78..100 | **min about 78** (today's sol-25 value: nothing changes before 41.61), **max 93-95**; from sol 42 to the arrival **85-95** (the orchestrator's 85-94) |
| Hope after the arrival | 89..98, ends 96 FLOURISHING | **79-88** (the orchestrator: bal_preview2's 68-77 + the 11 points B keeps); a static re-score of today's CREW lines gives 74-89, ending about 81, THRIVING |
| Sols under 75 from sol 25 | 0 | about 3 (the late dry spell, sols 93-95) |
| The Big Arrival | 81.01 | **about 76-81** |
| Earth's demand | 100.25 | **75.25** |
| ROUND lines (Q1, Q2, Q3s) | 9 (7 Q3) | about 9; **at least 5 (3 Q3)** |
| Population at 110 | 122 | about 115-120 |
| Immigrated / born | 9 / 9 | about 6-9 / about 8 |
| Food low point | 57 sols | about 52-57 (A: after the wave the larder starts 2,500 kg lower) |
| Recalled / outpost bar | 0 / PASS | 0 / PASS |

A miss of more than 5 Hope points or 5 sols needs its cause in the docs entry.

### 8.3 The control (gate step 7)
```
RH_PLAYTEST_DISTRICTS=1 RH_PLAYTEST_EXTRA="-RHConfig=HopeMilestonesMax=1000,EarthTensionDemandThreshold=60,ColonistTransportFoodKgPerColonist=50" bash scripts/sim/rh_playtest.sh 110 bal_control guide full
cmp Saved/RHPlaytest/bal_control.txt Saved/baselines/ind_full_playtest.txt
```
**Byte-identical is the pass.** Why it is the right proof:
- `GetConfigScalar` takes an override for any name, so `HopeMilestonesMax` can be overridden (`RHDefinitionsSubsystem.cpp:811-815`; the loader clamps 1000 to 1000).
- `min(sum, 1000)` returns the sum bit for bit.
- The Notes edits are never read (no code reads `FRHConfigRow::Notes`).
- The manifest `Effect` is display text that the playtest never prints.
- The `[RHConfig]` Warning lines are not `[Playtest]` lines.

A difference is a stop: find it.

---

## 9. Deviations the builder must NOT take
- **Values.** No config Value other than the three; no other row's Notes beyond 3.1; no change to the seven milestone values, `RH_Discoveries.csv`, `HopeDiscoveryThreshold`, the Hope bands, `DilemmaComplyTensionDrop`, `PactTension` (D's optional add-on, not chosen), `CurrencyIssueTension` (#124 stays tagged: the director asked to revisit it with the tension numbers), or the manifest's mass, price or name.
- **Save.** No save version change, no new saved field, nothing that stores, spends or saves Hope.
- **Code.** No second `min()`, no cap on any other line, no fade, no change to `StepEarth`, `ResolveDilemma`, `StepDiscovery` or `GetBigArrivalSupport`. No getter for `EarthTensionDemandThreshold`, no new `Debug_` hook.
- **Tests.** No new battery suite; no second new check line; do not weaken or delete any check (DS5's second-window clause is restated with the cap, not dropped); do not move a fixture tension except CU3's 61.1 -> 46.1 and the 7.4 rule; do not touch `-furnish` F0, `-levers` LV0 or the baseline pins.
- **Presentation.** No `Source/RedHope` edit (the tension bar's 60 stays; follow-up). No `Content/` change except the two DT assets by the sync script. No `Config/` change.
- **Tuning.** Never tune any row to make the playtest or a check pass; a failed bar is a director question.
- **Files that are not this gate's.** No edit to `docs/PROJECT_STATUS.md` (its D3a open item and DT-sync note are the orchestrator's), `orchestrator-handoff.md`, `ORCHESTRATOR-RULINGS.md`, `gate-d-wording.md`, the scope or the analysts' files.
- **Git.** No commit, `git stash`, `checkout`, `reset` or `add`.

---

## 10. The docs entry (`docs/design-decisions.md`, appended after the P2 entry)
Title exactly: `### Balance pass 1 (A, B, D): what it settles, and where it chose (2026-11-04, not yet committed)`. It holds everything gate step 11 lists, plus:

**What it settles:**
- The three changes with row and old/new value: `ColonistTransportFoodKgPerColonist` 50 -> 25 kg; `HopeMilestonesMax` new, 15; `EarthTensionDemandThreshold` 60 -> 45.
- D3a's milestone question (`docs/design-decisions.md:348`) is answered with its first option, the cap at 15.
- The `min()`, quoted.
- Tagged rows 154 -> 126: the 27 stale rows by name, and #28 cleared because the director picked its value (1.4 (1)).
- The Notes of `EarthTensionDemandThreshold` and `EarthPreemptiveThreshold`.

**Player-visible text, and why `gate-d-wording.md` gains no block:**
- The manifest picker (`SRHCommandDeck::BuildObjectivePanel`, `RHCommandDeckPanels.cpp:613`) shows the Colonist Transport row's sub-line (`:758`, "8,000 kg - 8,000 cr - +20 colonists into certified housing; +500 kg Food (25 kg each); ...") and its tooltip (`:737`). It is visible while a quota awaits its manifest.
- This is D136's reviewed wording (Final 2026-10-21; `docs/qa/2026-10-21/gate-d-proposed-wording.md:154`) with only its placeholder numbers changed.
- D129's landing banner and D132's Big Arrival banner print their kg through `%.0f` (now 500 and 2,500); their copy is unchanged.
- Config Notes are data notes, not player-facing.
- The Crew panel's existing "Labs research once Hope holds 85." line will show more often after the wave; its text is unchanged.

**Battery, playtest and runs:**
- The battery moves by option (7), with a pointer to `battery_moves.md`.
- The playtest's predicted-against-actual table (8.2), every discovery's sol, and the control's `cmp`.
- The click runs with labels, loads and counts.
- The DT sync report and the read-back.

**Mutations:**
- Each mutation (6) and the line it failed.
- **That no in-repo check guards header/CSV parity**, with `bal.parity` as this gate's static guard.

**Recorded facts and deviations:**
- The preview's mechanism correction (1.2 (3)).
- The two test re-anchors D forced (CU3 61.1 -> 46.1; `-solidarity`'s two texts) and why they are not tuning (1.4 (8)).
- **D's re-raise, in numbers:**
  - Measured from the demand's arrival, a Defy still re-raises the demand at once if answered 20 sols or more late, and a DefyIssue if answered about 3.3 sols or more late (the same windows as at 60).
  - New: a Comply answered at tension 90 or more (only after ignoring the demand about 75 sols, or after event spikes) leaves 45 or more and re-raises at once. At 60, a Comply never re-raised.
  - The embargo at 75 now comes 50 sols after the demand, not 25.
  - These are #124's "E5 banner" revisit numbers for the director.
- **Old saves.** v60 is unchanged and a save loads with its own state:
  - config is not saved;
  - a transport or a Big Arrival that lands after the load (an in-flight ship's included) brings 25 kg a head;
  - a loaded colony at tension 45-60 with no pending demand raises it on its first step;
  - a colony past 15 milestone points reads the capped Hope at once, and its smoothed Hope eases down over about 3 sols.

**Follow-ups (not built):**
- the tension bar's red at 60 (`RHCommandDeckPanels.cpp:2333`);
- a pin and a getter for `EarthTensionDemandThreshold`;
- an in-repo header/CSV parity check;
- `-scale`'s digests to re-baseline on its next run;
- the developer texts that still print a capped discovery's "+N Hope" (`RHSimWorldSubsystem.cpp:7674-7676`, `RHCheats.cpp:1694`);
- `PROJECT_STATUS.md`'s D3a line and DT-sync count (322 -> 323), for the orchestrator.

---

## Appendix A. The 27 stale rows (Notes only; bal1 #3-#51, bal2 #63-#99, bal3 #125-#151; scope section 2)

On each line, replace `Placeholder, director review` (exactly once) with `Director ruling <ruling> (balance pass 1, 2026-11-04, cleared the stale placeholder tag)`.

| # | CSV line | Row | Value (unchanged) | `<ruling>` |
|---|---|---|---|---|
| 3 | 85 | IceLowAlertFraction | 0.25 | `D2a, about 25%` |
| 9 | 171 | HopeFoodPenalty | 10 | `D4a, Hope loses up to 10 points` |
| 10 | 172 | FoodRunwayFloorSols | 10 | `D4a` |
| 12 | 174 | QuotaRepeatBillMul | 1.10 | **whole replacement:** `Placeholder, director review. E3 (2026-10-23, Q-C (a))` -> `Director ruling E3 Q-C (a) (balance pass 1, 2026-11-04, cleared the stale placeholder tag). E3 (2026-10-23, Q-C (a))` |
| 14 | 176 | ShipMassCapKg | 30000 | `E2b Q-A (a)` |
| 17 | 179 | QuotaShortLaunchSols | 3 | `E3 Q-B (a)` |
| 19 | 183 | DayShiftDefault_Forge | 1 | `D5a` |
| 20 | 184 | DayShiftDefault_Electrolyzer | 1 | `D5a` |
| 30 | 194 | ImmigrationHopeMin | 75 | `C2 Q5a, a thriving city` |
| 36 | 200 | BigArrivalHopeMin | 75 | `C2 Q7a, THRIVING` |
| 37 | 201 | BigArrivalStreakSols | 20 | `C2 Q7a, about 20 sols` |
| 38 | 202 | BigArrivalFreeBeds | 100 | `C2 Q7a, 100 free beds` |
| 39 | 203 | BigArrivalColonists | 100 | `C2 Q7a, about 100 colonists` |
| 51 | 220 | GrowthCityShareDefault | 0.5 | `C5d option (a), confirmed by the industry ruling` |
| 63 | 232 | CommuteRampStartPop | 60 | `C6 Q2a, the ramp from 60 to 160 people` |
| 64 | 233 | CommuteRampFullPop | 160 | `C6 Q2a, the ramp from 60 to 160 people` |
| 76 | 245 | LiftFitStructKg | 400 | `D4 Q-A (a), 2026-10-21` |
| 77 | 246 | FirstLiftFitStructKg | 400 | `D4 Q-A (a), 2026-10-21` |
| 92 | 261 | DepthCostFromFloor | 5 | `Step 2 Q3a, floors -1 to -5 keep today's numbers` |
| 98 | 267 | HopeWarmMax | 5.0 | `D5 depth Q-B (a)` |
| 99 | 268 | HopeShelterMax | 3.0 | `D5 depth Q-A (a)` |
| 125 | 294 | CurrencyRequisitionPenalty | 0.10 | `E5 Q-A (a), 2026-10-24` |
| 137 | 306 | DeskSpreadBp | 300 | `E5 Q-C (a), 3% each way` |
| 138 | 307 | DeskCapPerSol | 500 | `E5 Q-C (a)` |
| 147 | 316 | PactBetrayalTariffBp | 2500 | `E6 Q-C (a)` |
| 148 | 317 | PactBetrayalTariffSols | 30 | `E6 Q-C (a)` |
| 151 | 320 | SabotageCutPct | 100 | `E6 Q-B (a), production stops` |

#124 `CurrencyIssueTension` (line 293) keeps its tag. Three of the rows (#147, #148, #151) are among `-levers` LV0's 13; LV0 compares Values only, so it holds.

---

## Appendix B. The gate's helper scripts (create under `WORK/checks/`; run with `python3 -I`)

**`parity_check.py`** (5.4, `bal_mut_parity`):
```python
#!/usr/bin/env python3
"""bal.parity: this gate's config rows have header defaults equal to RH_Config.csv.
usage: python3 -I parity_check.py <RHSimWorldSubsystem.h> <RH_Config.csv>"""
import csv, re, sys
ROWS = ['HopeMilestonesMax', 'ColonistTransportFoodKgPerColonist', 'EarthTensionDemandThreshold']
hdr = open(sys.argv[1], encoding='utf-8').read()
with open(sys.argv[2], encoding='utf-8', newline='') as fh:
    vals = {r[0]: r[1] for r in csv.reader(fh) if r}
bad, seen = [], []
for row in ROWS:
    m = re.findall(r'^\s*double\s+' + row + r'\s*=\s*(-?[0-9.]+)\s*;', hdr, re.M)
    if len(m) != 1:
        bad.append(f'{row}: {len(m)} header default(s)')
    elif row not in vals:
        bad.append(f'{row}: no CSV row')
    elif float(m[0]) != float(vals[row]):
        bad.append(f'{row}: header {m[0]} != CSV {vals[row]}')
    else:
        seen.append(f'{row} {vals[row]}')
print('bal.parity ' + ('PASS ' + ', '.join(seen) if not bad else 'FAIL ' + '; '.join(bad)))
sys.exit(1 if bad else 0)
```

**`csv_verify.py`** (3.1 (d)):
```python
#!/usr/bin/env python3
"""bal.csv: RH_Config.csv after balance pass 1, against HEAD. usage: python3 -I csv_verify.py <repo>"""
import csv, io, subprocess, sys
repo = sys.argv[1]
TAG = 'Placeholder, director review'
STALE = ['IceLowAlertFraction', 'HopeFoodPenalty', 'FoodRunwayFloorSols', 'QuotaRepeatBillMul', 'ShipMassCapKg', 'QuotaShortLaunchSols',
         'DayShiftDefault_Forge', 'DayShiftDefault_Electrolyzer', 'ImmigrationHopeMin', 'BigArrivalHopeMin', 'BigArrivalStreakSols',
         'BigArrivalFreeBeds', 'BigArrivalColonists', 'GrowthCityShareDefault', 'CommuteRampStartPop', 'CommuteRampFullPop', 'LiftFitStructKg',
         'FirstLiftFitStructKg', 'DepthCostFromFloor', 'HopeWarmMax', 'HopeShelterMax', 'CurrencyRequisitionPenalty', 'DeskSpreadBp',
         'DeskCapPerSol', 'PactBetrayalTariffBp', 'PactBetrayalTariffSols', 'SabotageCutPct']
assert len(STALE) == 27
NEWV = {'ColonistTransportFoodKgPerColonist': '25', 'EarthTensionDemandThreshold': '45'}
head_text = subprocess.run(['git', '-C', repo, 'show', 'HEAD:docs/data/RH_Config.csv'], capture_output=True, text=True, check=True).stdout
now_text = open(repo + '/docs/data/RH_Config.csv', encoding='utf-8', newline='').read()
head = list(csv.reader(io.StringIO(head_text)))
now = list(csv.reader(io.StringIO(now_text)))
h = {r[0]: r for r in head[1:]}
n = {r[0]: r for r in now[1:]}
bad = []
if now[0] != head[0] or [r[0] for r in now[1:]] != [r[0] for r in head[1:]] + ['HopeMilestonesMax']:
    bad.append('header or row names/order')
for k, r in h.items():
    if k not in n or n[k][1] != NEWV.get(k, r[1]) or n[k][2] != r[2]:
        bad.append('value/unit ' + k)
nm = n.get('HopeMilestonesMax')
if not nm or nm[1] != '15' or nm[2] != 'Hope' or TAG in nm[3] or 'Balance pass 1' not in nm[3]:
    bad.append('the new row')
changed = sorted(k for k in h if k in n and h[k][3] != n[k][3])
want = sorted(STALE + ['ColonistTransportFoodKgPerColonist', 'EarthTensionDemandThreshold', 'EarthPreemptiveThreshold'])
if changed != want:
    bad.append('Notes changed on ' + ','.join(sorted(set(changed) ^ set(want))))
for k in STALE + ['ColonistTransportFoodKgPerColonist']:
    if TAG in n[k][3]:
        bad.append('tag left on ' + k)
    if k in STALE and '(balance pass 1, 2026-11-04, cleared the stale placeholder tag)' not in n[k][3]:
        bad.append('ruling text missing on ' + k)
tagged = sum(1 for r in now[1:] if TAG in r[3])
if tagged != 126:
    bad.append(f'{tagged} tagged rows, 126 wanted')
if '\r' in now_text or not now_text.endswith('\n') or '"' in ''.join(n[k][3] for k in STALE).replace('""', ''):
    bad.append('line endings, final newline or a stray quote')
print('bal.csv ' + ('PASS' if not bad else 'FAIL ' + '; '.join(bad)) + f' ({len(now) - 1} rows, {tagged} tagged, {len(changed)} Notes edited)')
sys.exit(1 if bad else 0)
```

**`dt_readback.py`** (section 4; run by `UnrealEditor-Cmd -run=pythonscript`; loads, never saves):
```python
import os, unreal
OUT = os.environ.get("RH_REPORT", "/tmp/bal_dtread.txt")
lib = unreal.DataTableFunctionLibrary
out = []
def table(path, cols):
    dt = unreal.load_asset(path)
    if not dt:
        out.append("FAIL could not load " + path)
        return {}
    names = [str(x) for x in lib.get_data_table_row_names(dt)]
    data = {}
    for c in cols:
        try:
            data[c] = [str(v) for v in lib.get_data_table_column_as_string(dt, c)]
        except Exception as e:
            out.append("FAIL column %s of %s: %s" % (c, path, e))
            data[c] = []
    out.append("%s rows %d" % (path, len(names)))
    return {nm: {c: (data[c][i] if i < len(data[c]) else None) for c in cols} for i, nm in enumerate(names)}
cfg = table("/Game/RedHope/Data/DT_Config.DT_Config", ["Value"])
for nm in ("HopeMilestonesMax", "ColonistTransportFoodKgPerColonist", "EarthTensionDemandThreshold"):
    out.append("DT_Config %s = %s" % (nm, cfg.get(nm, {}).get("Value")))
man = table("/Game/RedHope/Data/DT_ManifestItems.DT_ManifestItems", ["Effect"])
out.append("DT_ManifestItems ColonistTransport Effect = %s" % man.get("ColonistTransport", {}).get("Effect"))
with open(OUT, "w") as fh:
    fh.write("\n".join(out) + "\n")
```
