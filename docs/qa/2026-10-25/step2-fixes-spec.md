# Step-2 fixes gate: build spec (2026-10-25)

The build spec for the seven should-fix findings of `docs/qa/2026-10-24/step2-code-review.md`, with the nits worth folding in and one director question. Written read-only: nothing was built or run, and no existing file was edited.

**Baseline.** Every finding was re-read at HEAD `cd886a3`. Its Source is identical to `da80819`, where the review read it (`git diff --stat da80819 HEAD` touches two docs only), so the review's line numbers still hold. The short names are the review's:
- **W:** `Source/RedHopeSim/Private/RHSimWorldSubsystem.cpp`
- **Cav:** `RHSimCavern.cpp`
- **Dist:** `RHSimDistricts.cpp`
- **Walk:** `RHSimDeepWalk.cpp`
- **Com:** `RHSimCommute.cpp`
- **Tram:** `RHSimTram.cpp`
- **Sec:** `Source/RedHope/Private/RHSectionVisualizerSubsystem.cpp`
- **.h:** `RHSimWorldSubsystem.h`

**When to build.** Build after E4 commits. E4 is open in this tree and edits most of the same files: W, .h, `RHSimCommandlet.cpp`, the cavern, deep-district and tram suites, `RHClickTest.cpp` and `gate-d-wording.md`. Its lines will have moved, so find each site by its function name.

**What this gate does not do.**
- No save format change and no version bump; the version stays wherever E4 leaves it.
- No data row or CSV.
- Every new player-facing string is a Gate-D placeholder row in `gate-d-wording.md`: row J1 for caverns, I1 for tunnels, D144 for district paint, N1 for the tram alert.

## Summary

| # | Fix | Proof | Battery |
|---|-----|-------|---------|
| 1 | Add a two-ride term to the tram walk's lower bound; the one-line case is unchanged | TM2b: a two-line sweep against the oracle | `-trams` gains checks; nothing moves |
| 2 | Make the bed assignment a lazy keyed cache that every reader goes through, and reset it at load | DC10: idempotence after a bunk lands, era against agent at a sol boundary, load after another colony | `-deepcommute` gains checks; nothing moves |
| 3 | Refuse a cavern order at 1,024 records and a tunnel order at 4,096 | CV1, CV8, TN2, TN8 at the cap, with a save round trip | `-caverns` and `-tunnels` gain checks |
| 4 | Drop a district whose Id repeats or goes back, before its shape attaches; guard the empty shape | DD9: a duplicated Id loads with a Warning and no Error | `-deepdistricts` gains checks |
| 5 | A lattice range bound (2^20 cells) on cavern corners, the cavern loader and both paint preflights | CV1, CV8, DD1, `-districts`: out-of-range corners refused, the int32 edge refused at once | `-caverns`, `-deepdistricts` and `-districts` gain checks |
| 6 | Seed the TRAFFIC overlay hash with the view, fold its numbers one at a time, redraw on a structural rebuild | A click step: Q while paused with TRAFFIC on must redraw; a `section.unit` collision check | Sim battery unchanged; the click test gains steps |
| 7 | TM7 rolls under load: real Lab rooms, a nonzero wait, a third leg that never saved | The reworked TM7 | `-trams`: TM7's one line changes; one check added |

---

## Fix 1. The tram lower bound misses routes that ride two lines (D6d)

**Re-verified.** In `Walk:507-522`, `WalkTramCm` returns the walk when `min over (line k, end e) of oct(A, stop_e) + Cm_k + oct(stop_other, B) >= Base`. That bound holds only for a route that rides one line.
- Take the review's floor: digs X, Z and Y, an unlined tunnel T0, and lined tunnels T1 and T2.
- The walk from (0,0) to (30,0) costs 30,000 cm. Riding T1 then T2 costs 22,328 cm.
- The bound is 44,410, so the stop Dijkstra (`Walk:523-623`) never runs.

**Change.** In `URHSimWorldSubsystem::WalkTramCm`, the block at `Walk:510-522` becomes:

`Lower = min(OneRide, TwoPlus)`
- `OneRide` is today's paired minimum, unchanged.
- `TwoPlus` exists only when `K >= 2`. It is `min over every stop s of oct(A, s)`, plus `Cm(1) + Cm(2)` (the two smallest `Line.Cm` on the floor), plus `min over every stop s' of oct(s', B)`.

Why it is a valid bound:
- An optimal route through the stop Dijkstra visits each stop at most once, so a route with two or more rides uses two or more different lines.
- So such a route costs at least the walk to its first boarding stop, plus the two cheapest rides, plus the walk from its last stop.
- On the review's floor `TwoPlus` = 2,000 + 7,200 + 7,300 + 2,414 = 18,914, which is below 30,000, so the Dijkstra runs.

Update the comment at `Walk:507-509`.

**Why not the review's simpler bound** (`min oct + min Cm + min oct` for every K)? It is also valid, but it weakens the one-line case. `-scale` lays one line per floor, so its "pruned by the lower bound" count would drop and more walks would run the stop Dijkstra. The `K >= 2` term leaves every one-line floor bit-identical, including that counter.

**Check: TM2b in `-trams`.**
- Build the review's three-dig floor:
  - X at x -2..0, y -1..1;
  - Z at x 30..32, y -1..1;
  - Y at x 14..16, y 30..32;
  - T0 (1,0)-(29,0), unlined;
  - T1 (0,2)-(15,29), lined, 7,200 cm;
  - T2 (17,31)-(31,2), lined, 7,300 cm.

  Then lay both lines.

  **Placing it in a real fixture.** These are the coordinates of the review's model, `scratchpad/step2/d6d/prune.py`. In the suites, (0,0) is shaft 0's column, which is never a cell, so translate the whole layout clear of every column. Distances depend only on differences, so the integers do not change.
  - Each tunnel's first point stays in rock beside a dig, which `CanDigTunnel` requires.
  - Y can be a 3x3 cavern seeded by T1's carved far end (`Debug_DigCavern` with `Debug_CarveCavernCells`).
  - If the layout has to change shape to fit, derive the pair's integers again by hand, the same way.
- Assert the review's pair: `Debug_WalkCm` from X's (0,0) to Z's (30,0) is 22,328, against 30,000 on foot and today's bound of 44,410, and its `Debug_WalkTramLines` names both lines.
- Assert a full sweep of every (home, seat) pair against `OracleWalk(Sim, L, {ride T1, ride T2})`, with zero wrong.
- Two non-vacuity checks:
  - at least one pair rides both lines;
  - `GetTramWalkCalls() - GetTramWalkPruned()` rose during the sweep.

**Mutation M1.** Restore today's bound (delete `TwoPlus`). TM2b fails on the 30,000 against 22,328 pair. Today's TM2 passes under M1, because its floor has one line.

**Battery.** `-trams` gains the TM2b checks. No other line moves:
- TM2's sweep has one line;
- TM1 and TM6 compare no route against a figure. TM6 compares its commutes before and after a save, and both sides run the new code.

Re-run `-scale` and confirm the SCALE trams prune count is unchanged.

---

## Fix 2. Home cells read the bed assignment at the wrong moment (D6b, D6c)

**Re-verified.**
- `DeriveDeepHomes` (`Dist:185`) reads the raw `ColonistBedPiece` (`Dist:249`). That map is rebuilt only by the bed block of `RefreshJobs` (`W:6262-6298`).
- **Window (a).** Hook (b) (`W:6239-6255`) runs before that block, through `EnsureCommuteFlows` and `EnsureDeepHomes`. The DeepHomes key moves on furniture and zoning, so it re-derives at once from the old pieces.
- **Window (b).** A `Designate` (`W:9471-9485`) bumps `ZoningVersion` at `W:5897` and calls no `RefreshJobs`. `RollTransit` then re-derives homes from new zoning and old pieces.
- **Window (c).** Load teardown resets `ColonistStations` (`W:14649`) but not `ColonistBedPiece`. The first derivation at `W:14839-14842` reads the previous colony's pieces.

**Change.** Make the bed assignment its own lazy cache, the same pattern as `EnsureDeepHomes`.
- **.h:**
  - add `const TMap<int32, int32>& EnsureBedAssignments() const;`;
  - make `ColonistBedPiece`, `BedAssignVersion`, `bBedKeyValid` and `BedKeyColonists/Furniture/Zoning` `mutable` (`.h:3067`, `:3506`, `:3518-3519`);
  - `BuildBedAssignments` becomes `const` (it writes only the mutable map);
  - `GetColonistBedPiece` (`.h:654`) reads through `EnsureBedAssignments()`.
- **W, new `EnsureBedAssignments`.** It holds today's bed block, moved whole:
  - with no furniture: clear, then invalidate the key, and bump the version only if the map was non-empty;
  - when the key `(ColonistSetVersion, FurnitureVersion, ZoningVersion)` moved or is invalid: rebuild and bump `BedAssignVersion`;
  - otherwise, under `bVerifyCaches`: today's compare, with the same message.

  It builds `ByIdOrder` from `Colonists` itself (Id-ascending by construction; sort when not, as `W:6072-6083` does).
- **W, `RefreshJobs`.**
  - Replace the bed block (`W:6262-6298`) with `EnsureBedAssignments();`, placed before hook (b) at `W:6239`.
  - The no-colonist branch at `W:6055-6066` stays as it is.
- **W, load teardown.** Beside `ColonistStations.Reset()` at `W:14649`, add `ColonistBedPiece.Reset(); bBedKeyValid = false; ++BedAssignVersion;`.
- **Dist, `DeriveDeepHomes`.**
  - After the `AnyMapped()` early return (`Dist:196-199`), call `EnsureBedAssignments()`.
  - Re-stamp `Out.BedAssign = BedAssignVersion` after that call, because the call may bump it.
  - Read pieces from the returned map at `:249`.
  - An unmapped colony never reaches the call, so it derives exactly when it does today.
- **Key builders that read `BedAssignVersion`** (`Dist:146` and `Com:69`, both inside `AnyMapped()`): call `EnsureBedAssignments()` first, so a key is never built from the old generation and then derived twice.

The cache key is the one today's C1 cache uses; this gate does not widen it.

**Check: DC10 in `-deepcommute`.**
- **Setup.** Use DC1 layout A: two unjoined digs on -1 (the review's 47,015 cm geometry), with the floor mapped and `FullRamp()`.
  - Copy DD3's `ZoneLiving` and `FurnishBunk` helpers.
  - Seat-holder R holds a Lab seat in dig A.
  - Before the bunk, legacy fill homes R in dig B, so R is long.
- **(a)** Zone a Living cell in dig A and give it a bunk. R, the lowest Id, takes it, which makes R near. Step once in the agent band. Assert that `GetLabYieldSeatSum()` equals its value after one more `Debug_RefreshJobs()` with nothing changed, the idempotence test. Run the same from a save twin in the era band and assert the two bands agree.
- **(b)** From a save made 30 s before a sol boundary (TM7's pattern), send a Designate that re-zones the bunk's cell. Step once in the era band and once in agent sub-steps. Assert that the frozen shaft waits and `TransitCheckedSol` are equal in both bands. Use a Living re-zone so that only the bed side moves; see the note below.
- **(c)** Save file F. Load F after resetting to the base colony, then load colony G (bunks whose piece Ids collide with F's), then load F again. Assert that the seat sums right after each load of F are equal, and that a `Debug_RefreshJobs()` straight after a load changes nothing.

**Mutations.**
- **M2a:** `DeriveDeepHomes` reads the raw `ColonistBedPiece` (skip the ensure). (a) and (c) fail.
- **M2b:** drop `BedKeyZoning` from `EnsureBedAssignments`' key. (b) fails.

**Battery.** `-deepcommute` gains the DC10 checks.
- **Suites with no mapped floor:** bit-identical, because beds are still derived only at the `RefreshJobs` point.
- **`-deepdistricts`, `-trams` and `-caverns`:** no line is expected to move. DD3 calls `Debug_RefreshJobs()` before it reads pieces. If a line does move, it was reading an old bed map: investigate it, do not re-pin it.
- **`-RHVerifyCaches`:** the bed message keeps its text.

**Note, outside this gate.** Window (b) has a seat side too. A Designate that zones a Lab changes `ColonistStations` only at the next `RefreshJobs`. In the era band none runs before the boundary roll; in the agent band one does. This is C6a and D4 behaviour that predates step 2, and the fix (a `RefreshJobs` after a Designate, as Sell does at `W:9469`) would move numbers in many suites. Log it as a known era-parity risk. DC10 avoids it on purpose.

---

## Fix 3. Order 1,025 caverns and every later save refuses to load; tunnels at 4,096 (D6a, D3)

**Re-verified.**
- `CanDigCavern` (`Cav:305`) never reads `Caverns.Num()`. `ExecuteDigCavern` always appends (`Cav:454`).
- Cancelling is free (`ExecuteCancelCavern`, `Cav:460-485`), and the loader refuses `Count > RH::MaxCavernRecords` (1,024, `.h:376`) at `Cav:531`.
- Tunnels have the same gap: `CanDigTunnel` (`W:8017`) against the loader at `W:8268`, `MaxTunnelRecords` 4,096 (`.h:343`).

**Choice: cap during play with a refusal (recommended), rather than retiring cancelled records.**
- **Retiring cancelled records does not close the gap.** Finished caverns stay on the list too. Sculpting with 1x1x1 caverns that all finish still reaches 1,025, and so do tunnels.
- **D7 needs every finished record kept.** An atrium is a finished cavern's record (`d7-atriums-design.md`, `OpenAtrium` by cavern Id).
- **Removing records breaks a stated invariant.** The review lists "the tunnel, cavern and tram lists only grow".
- **It would reach other state.** A cancelled cavern's batches still in flight are keyed by its Id (`GetCavernBatchesInFlight`), and the save layout would need record deletion.

A refusal at the loader's own limit is one comparison and leaves every invariant intact.

**Change.**
- `CanDigCavern`: refuse first, before refusal 1, when `Caverns.Num() >= RH::MaxCavernRecords`, with `"The colony has ordered %d caverns, the most one save can hold"`. Taking it first means the reason is the same whatever else is wrong with the box.
- `CanDigTunnel`: the same against `Tunnels.Num() >= RH::MaxTunnelRecords` (`"... %d tunnels ..."`).

The preflight runs at queue time and again at execute (`Cav:441`), so the order path and the brush both refuse. Add both strings to rows J1 and I1.

**Check.**
- **CV1 in `-caverns`.** Reach 1,023 records, then send two real orders: the 1,024th is accepted and the 1,025th is refused with the cap reason by the preflight and by the order, and nothing is appended.
  - Reaching 1,023: either order and cancel a 1x1x1 box beside the dig in a loop, or add a `Debug_PadCavernRecords(N)` that appends cancelled 1x1x1 records with fresh Ids, which the loader accepts.
- **CV8.** At 1,024 records, `SaveColony` then `LoadColony` round-trips with an empty error.
- **TN2 and TN8 in `-tunnels`.** The same at 4,096.

**Mutation M3.** Delete the cavern refusal. CV1 fails, because the 1,025th order is appended. CV8 fails too, with "Save cavern list invalid: next id 1026, cavern count 1025". Do the same for tunnels.

**Battery.** `-caverns` and `-tunnels` gain checks. No suite comes near either cap, so nothing else moves.

---

## Fix 4. A duplicated district Id crashes the load after teardown (D6b)

**Re-verified.**
- In the v37 district loop (`W:14681-14746`), the first district with an Id takes the shape's points by `MoveTemp` (`W:14706`).
- A second district with the same Id matches the same shape, now empty, and reads `D.Deep[0]` at `W:14707`.
- The same happens after the Warning branch has reset the points (`W:14712`).
- The Id-order test is part of `bBad` at `W:14716`, after the attach. `ReadDeepBlock` (`Dist:1400-1414`) checks only the skeleton's own order, so a district list and a skeleton that disagree pass it.

**Change.** In `LoadColony`'s v37 loop, right after the record is read and sorted (`W:14694`):
- When `Districts.Num() > 0 && D.Id <= Districts.Last().Id`, log a Warning (`"LoadColony v37: district #%d repeats or goes back on the Id order; it is dropped"`) and `continue`. Its shape is not attached and it is not added.
- In the attach loop, before `D.Deep = MoveTemp(Shape.Points)`: when `Shape.Points.IsEmpty()`, log a Warning and `break` with no attach. This is a guard, so that `D.Deep[0]` can never be read on an empty array, whatever the order.

**Check: DD9 in `-deepdistricts`.**
- Take DD9's saved file. In the v37 list, write Id 2 over district #3's Id.
  - `FMemoryArchive` writes an `FName` as an `FString`, so the record starts `int32 Id, FString Kind, FString Name`.
  - Find the byte run that is #3's Kind and Name as written (each an int32 length, the chars and a NUL), then `PutI(at - 4, 2)`.
  - If the builder finds `FName` is not written as a string, a one-shot debug hook on the writer is the fallback.
- Load through `TryFile`. Assert:
  - the load succeeds;
  - one Warning names the duplicate;
  - the counted Error lines stay at 0 (DD9's own counted load, `RHSimDeepDistrictSuite.cpp:84`);
  - there are four districts, and #2 holds its own shape.

**Mutations.**
- **M4a:** remove both new lines. The load crashes (a fatal range check), so the suite never completes and the battery's `completed=0` fails it.
- **M4b:** remove only the dropping line and keep the guard. The duplicate reaches `bBad` and the old `LoadColony v37 ... malformed` Error line at `W:14736`, so the counted-Error check fails.

**Battery.** `-deepdistricts` gains the DD9 checks. Nothing moves.

---

## Fix 5. Boxes at the int32 edge overflow their loops (D6a, D6b paint, the older surface paint)

**Re-verified.**
- These loops all run `for (int32 X = Min.X; X <= Max.X; ++X)`:
  - `Cav:33` (`GetCavernUncarved`), `:130` (`FindCavernPoint`), `:344`, `:380` and `:400` (`CanDigCavern`), and `:602` and `:617` (`ReadCavernBlock`);
  - `Dist:641` (`CanPaintDeepDistrict`), `:804` and `:867` (`CanPaintDistrict`) and `:951` (`ExecutePaintDistrict`).
- The sides are checked in int64, but nothing bounds the corners. `RH.DigCavern -1 1 2147483640 0 2147483647 0` passes the side check and then loops for ever.
- **New in this re-check:** `ExecuteDigCavern`'s midpoint `(C.Min.X + C.Max.X) / 2` (`Cav:453`) also overflows. So looping on offsets alone (the review's second option) is not enough, and the corners need a bound.

**Change.**
- In the .h `RH` namespace beside `CavernMaxSide` (`.h:371`), add `constexpr int32 MaxLatticeCoord = 1 << 20;` (about 10,000 km of lattice) and `inline bool IsLatticeInRange(const FIntPoint& P)`.
  - The helper compares `FMath::Abs((int64)P.X)` and the same for Y, so `INT32_MIN`, a saturated `CmToLattice` and NaN are all out.
- Refuse a corner out of range:
  - in `CanDigCavern`, straight after `CmToLattice`, before the side check: `"That box is out of range"`;
  - in `ReadCavernBlock`, before its side check at `Cav:597`: `"cavern %d has a corner out of range"`;
  - in `CanPaintDeepDistrict` and `CanPaintDistrict`, straight after their `CmToLattice`.
- `ExecutePaintDistrict` and the cavern work functions then only see bounded boxes.
- No legitimate box is anywhere near 2^20, so no game-written file changes.

Add the strings to rows J1 and D144.

**Check.**
- **CV1.** A corner at (1,048,577, 0) is refused with the range reason. Then, only if that check passed, try the `2147483640 .. 2147483647` box and assert it is refused at once. The guard keeps a mutation run from hanging.
- **CV8.** A hand-made file with a corner X of 1,048,577 is refused before teardown, and its error contains "out of range".
- **DD1.** A deep paint corner past the bound is refused.
- **`-districts`.** A surface paint corner past the bound is refused.

**Mutation M5.** Drop the `CanDigCavern` bound. CV1 fails, because the box gets a different refusal, and the guarded edge case is skipped. Drop the loader's bound and CV8 fails. Do the same per paint site.

**Battery.** `-caverns`, `-deepdistricts` and `-districts` gain checks. Nothing moves.

---

## Fix 6. The underground TRAFFIC overlay stays put after a turn or step (D8)

**Re-verified.**
- `RefreshOverlay`'s hash starts at 0 (`Sec:1337`) and mixes only numbers:
  - shaft load, cap and wait (`:1368`);
  - flows, with an arithmetic fold, `L * 7919 + X * 31 + Y` (`:1415`);
  - robots (`:1439`);
  - trams (`:1481-1484`);
  - the label count and `bRamp`.
- It returns early at `:1495` before the lists and labels are rebuilt.
- `OverlayHash` is reset only by `ClearOverlay` (`:1316`).
- `Rebuild` (`:721`) re-bakes the slab for the new `Axis`, `PlaneRow` and `Origin`, but never touches the overlay.

**Change, in `URHSectionVisualizerSubsystem`.**
- Add `uint32 ViewHash() const`, which mixes with `MixU32`, one value at a time:
  - `Axis`, `PlaneRow`, `Rows`, `AlongMin`, `AlongMax` and `DeepestSeen`;
  - `FMath::RoundToInt(Origin.X)` and `FMath::RoundToInt(Origin.Y)`.
- At `Sec:1337`: `uint32 Hash = ViewHash();`.
- Replace each arithmetic fold with one `MixU32` per value: flows at `:1415`, robots at `:1439` and trams at `:1481`. Put the flow fold in a static `MixFlow(uint32 H, int32 Level, int32 Out, int32 In)` so it can be unit-checked.
- In `Rebuild`, when `!bPeopleOnly`, set `NextOverlayS = 0.0`, so the overlay redraws in the same tick as the slab, not up to 0.5 s later.
- `RefreshSurface` may reuse `ViewHash()` in place of its `AlongMin * 131 + AlongMax` fold. This is optional, not required.

**Check, in `RHClickTest.cpp`.**
- Add a new step `section.traffic.turn`, after `section.labels traffic`, while TRAFFIC is on:
  - pause the sim, so no number can move;
  - record a new `GetOverlayRedraws()` (counted past the early return);
  - press Q;
  - within 1 s, the redraw count must have risen, and `CheckSectionLabels("section.labels.traffic.turned")` must pass;
  - press E, then unpause.
- In `section.unit`, assert `MixFlow(0, -1, 2, 0) != MixFlow(0, -1, 1, 31)`. That pair is one more commuter out against 31 fewer in, which collides under the old fold.

**Mutations.**
- **M6a:** seed with 0 again. `section.traffic.turn` fails: no redraw while paused.
- **M6b:** restore the arithmetic flow fold. `section.unit` fails.

**Battery.** The sim battery does not move, because RedHope is not in it. The click test's REACH count rises by the steps added. Re-run `RH.DeepBench` at the gate's HEAD; this also settles nit 37's stale figures.

---

## Fix 7. TM7 rolls tram waits only with 0 riders (D6d, test gap)

**Re-verified.** In each TM7 leg (`RHSimTramSuite.cpp:693-737`), `LoadColony`'s `RefreshJobs` wipes the hand-laid seats; the fixture has no rooms. They are laid again only after the step (`:726-729`), so the boundary roll sees 0 riders. The check pins exactly that, `wait0 prev600` (`:734`).

**Change, in TM7.**
- Seat the residents through real rooms, which survive every `RefreshJobs`.
  - In the setup, zone Lab cells in the dig across the line from the residents' home cells (rated floor, `SliceActive` Lab row).
  - Use enough seats that `GetTramPeople(1) >= 3` at the roll. At capacity 5 places and 2 places a rider, that queues.
  - Drop the hand re-seat at `:726-729`.
- Assert in each leg that the post-roll wait is above 0, and that it equals the wait formula TM4 pins for the logged load and capacity 5.
- Add a third leg that never saved: rebuild the fixture, advance to the same clock and step the era band once. Assert that its leg string equals the two loaded legs.

**Mutations.**
- **M7a:** `TramLoads[i] = 0` in `RollTransit` (`RHSimTransit.cpp:369`). The new TM7 fails on the nonzero wait. Today's TM7 passes under M7a, which is the gap.
- **M7b:** scale the tram load by 2 when the step is shorter than `EraDt`. The new TM7 fails on equal legs. Today's passes, because 0 x 2 = 0.

**Battery.** `-trams`: TM7's expectation line changes, the one existing line that legitimately moves (from `wait0 prev600` to the loaded figures), and one check is added for the third leg.

---

## Nits

### Fold in: trivial, and inert for the sim battery

The numbers are the review's. Each was spot-checked at HEAD unless marked as a doc fix.

- **9 (doc).** Restate D5 deviation 8 with its size: one charging RC_E at night can put the agent band about 0.3 Warmth above era, against DP9's 0.1 tolerance. Leave the code (see below).
- **10.** At `Depth:111`, also bump `WarmthVersion` when `(Old > 0) != (New > 0)`. Only presentation reads it (`RHColonyVisualizerSubsystem.cpp:3589`, `Sec:656`); no suite does.
- **11.** The POWER tile says machines when it means machines and floors.
  - Add a derived, unsaved `ShedFloors` beside `ShedCount` in the power state, and print "N machines and M floors shut off" (`RHCommandDeckPanels.cpp:399`). `ShedCount` keeps its meaning, so no power line moves.
  - New wording row.
  - If the power state turns out to be saved, leave this nit.
- **12.** Multiply both dig-hour quotes by `GetDigPermille(level) / 1000`: the tunnel tip (`RHPlayerController.cpp:1018-1021`, at the shown level) and the Excavate label (`RHCommandDeck.cpp:2074`). No test reads either string.
- **13, 15, 17 (doc).** Document that the warm-alert latch is not saved. Settle who owns lift power (the commit says D6e, `design-decisions.md:983` says D6). Correct deviation 2's account of X from 4 to 99 and of X below 0.
- **14.** `ReadFloorTable` (`W:5642`) refuses a floor table whose levels do not strictly descend, before teardown. Game-written tables always descend. Add a hand-made case in the suite that holds D1's floor-table refusals.
- **16.** Read `bCancelled` in the cavern loader (`Cav:571`), and both tunnel flags (`W:8291`), as `uint32`, refusing values outside {0, 1}.
  - UE writes a `bool` as 4 bytes, so the format is byte-identical (40-byte cavern records, 36-byte tunnel records).
  - A value of 2 then refuses through `OutError`, not a `LogSerialization` Error.
  - Add a CV8 and a TN8 case.
- **18.** Give a box below `-MaxDepth` refusal 2's own reason (`Cav:315-318`), not "bore a shaft that deep". No CV1 line tests below -20 through the order today, so add one.
- **20.** The completion log prints the cells this cavern carved, not the box (`Cav:267-268`). No test reads it.
- **30 and 31.** The N1 alert (`Tram:163`):
  - print `TramPlacesPerSol / 2` people a sol (each rider takes two places);
  - add that laying a line maps its floor, so commutes there are measured by walk.

  No test reads the alert. Update wording row N1.
- **32.** Fix the two `TramVersion` comments (`.h:1087`, `:3335`). The code is right.
- **34 (test).** Rewrite `section.unit` to check `InSlabRow`, `IsInSlab` and `Pick`, not `ClassifyRow`'s constant expression.
- **36, part (test).** `section.traffic.off` also asserts that the layers cleared: arrow count 0, tram band 0, legend empty. Fix 6 already edits these steps.
- **37, part.** Re-run `RH.DeepBench` (fix 6 does).
- **38b.** Arming Tunnel from the section uses `PC->GetShownLevel()` (`RHPlayerController.h:157`), not `GetActiveLevel()` (`RHCommandDeck.cpp:2438`). The Cavern button has the same test at `:2567` (seen in this re-check); fix both.

### Leave, with the reason

- **8:** the director question below. It rides here as fix 8 only if the answer is (a) or (b).
- **9, code:** draining the fleet's charge energy in `EraLogistics` moves era-band numbers.
- **19:** dropping orphaned work records needs load ordering, after the buildings load; pair it with the tunnels' identical gap later.
- **21 and 22:** spoil from deep growth, and the pending-bed count. Both move growth and mass numbers and need a design call.
- **23 (the rest):** the load repairs for hand-made deep blocks, a separate hardening pass.
- **24, 25 and 28:** performance. Measure first.
- **26:** moves the traffic figures.
- **27 and 29:** director calls.
- **33:** a behaviour change with its own click step (D8 polish).
- **35, and the rest of 36:** test rework.
- **37, the label-room cache:** performance work.
- **38a:** the stale pips.

**Seen while re-verifying, also left.** The v37 district loader logs `LogRedHopeSim` Error for a malformed district (`W:14736`), against the rule that no load path logs an Error. It predates step 2 (C5a). Fix 4 routes the duplicate-Id case around it.

---

## Order and verification

1. **3, 4, 5:** the refusals and the loader. They are independent.
2. **1, then 7:** both touch the trams suite.
3. **2:** the largest change.
4. **6:** presentation.
5. **The nits.**

Run each mutation once and revert it, and record the outcome per mutation in the commit message, as the economy-fixes gate did. Run the overflow mutation (M5) under a timeout as well, even with the guard.

**Done means:**
- the full battery (`bash scripts/sim/rh_battery.sh`, judged on `errors=0 completed=1` for all 57 runs);
- `-RHVerifyCaches` on `-deepdistricts`, `-deepcommute` and `-trams`;
- `-scale` (the tram prune count unchanged);
- the click test;
- `RH.DeepBench`.

---

## Director question: may a cavern reach down through floors already dug inside its box?

**What happens today.** A cavern order needs a starting point on every floor of the box: fresh rock beside a dig or a shaft. A floor that is already completely dug inside the box has no fresh rock, so the order is refused, and with a wrong reason: "On floor -2 the box touches no dig and no shaft", although the box sits inside a dig.

The digging would cope. The Borers already skip a floor with nothing left (`Cav:229`). A box dug on every floor is refused separately, and correctly ("Every point of that box is already dug").

**Why it matters for D7.** An atrium is made by converting a finished cavern of two floors or more. So this rule decides whether ground the player has already dug can become part of an atrium.

**(a) Exempt any floor with no rock left in the box. (Recommended.)** The rule still applies to every floor that has rock to dig. A box dug through on every floor is still refused.
- **For D7:** a player can open an atrium over rooms they already dug. They order a cavern from the dug floors down into new rock, let it finish, and convert it. D7's own refusals still protect anything in use on the upper floors: rooms, furniture, beds, tunnels and growth targets.
- **Cost:** a few lines in `CanDigCavern` and new CV1 cases. No save change, and no existing test line moves (no CV1 line pins a fully dug floor).

**(b) Keep the rule; fix only the wording.** For example: "On floor -2 there is no rock left in the box to start from - widen the box to take in rock beside the dig".
- **For D7:** a dug floor can be part of an atrium only if the player widens the box to take in fresh rock on that floor. A stack of floors dug right through can never become an atrium by itself. The workaround is not obvious.

**(c) Keep the rule, and give D7 its own way in:** an atrium made from any box of dug cells, not only from a finished cavern.
- **For D7:** atriums over dug ground work. But D7 then needs a second preflight and its own reservation of cells, which is exactly what its design avoided by converting caverns. This is the most work, and it reopens D7.

**Recommendation: (a).** It matches what the carver already does, keeps D7 as designed, and removes the misleading reason without extra work. If (a) or (b) is chosen, it rides in this gate as fix 8. If (c) is chosen, it goes to D7.
