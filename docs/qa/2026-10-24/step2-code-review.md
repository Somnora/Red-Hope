# Step-2 gates D5, D6a, D6b, D6c, D6d and D8: adversarial code review (2026-10-24)

A read-only review of step 2 as committed: D5 depth `df3e479`, D6a caverns `9364a5d`, the D4 20-floor perf fix `41ac355`, D6b deep districts `6b5653c`, D6c deep commutes `46854bf`, D8 the deep view `f461828` and D6d trams `19051fd`. The code was read from a frozen export of `da80819`. HEAD later moved to `75241a8`, which changes one doc only, so every line number below is HEAD's. Nothing was built, run or edited.

The specs are:
- `d5-depth-design.md` (2026-10-21);
- `d6-caverns-design.md` (2026-10-22): the DIRECTOR ANSWER, then MERGED DESIGN AND REVISION, then the reviews, then the draft;
- `d8-deep-view-design.md` (2026-10-23).

The review also used their `design-decisions.md` entries, the commit messages and the suites `-depth`, `-caverns`, `-deepdistricts`, `-deepcommute`, `-trams` and the click test.

**Method.** There were six independent passes, one per gate: D5; D6a; D6b; D6c with `41ac355`; D6d; D8. A separate cross-check pass then did two things:
- tried to refute every should-fix candidate;
- hunted for defects between gates: block order at load, O1 across all carvers, cache keys across gates, and caverns, growth and trams interacting.

The lead reviewer re-read every should-fix in the code. Two passes found finding 2 independently. The nits were verified by the pass that found them and are not re-verified here.

Short names, all under `Source/RedHopeSim/Private/` unless the path says otherwise:
- **W:** `RHSimWorldSubsystem.cpp`
- **Cav:** `RHSimCavern.cpp`
- **Dist:** `RHSimDistricts.cpp`
- **Walk:** `RHSimDeepWalk.cpp`
- **Com:** `RHSimCommute.cpp`
- **Depth:** `RHSimDepth.cpp`
- **Tram:** `RHSimTram.cpp`
- **Sec:** `Source/RedHope/Private/RHSectionVisualizerSubsystem.cpp`

## Verdict: PASS WITH FIXES

There are no blockers. The structure holds:
- **O1 holds across all five carvers.** Cells are only ever appended, through `AppendCarvedCell`. The tunnel, cavern and tram lists only grow.
- **All eight save blocks (v41 to v50) validate against the loaded data, not the live colony.** They read only the earlier loaded blocks as locals, and they refuse before teardown.
- **The cache keys are structs of separate fields,** and every carve, tunnel, shaft, district, tram and load change bumps the key that reads it.
- **RedHopeSim does not depend on RedHope,** and D8 writes no sim state.
- **The perf fix in `41ac355` is bit-identical by construction.**

There are seven should-fix defects:
1. Tram routing throws away cheaper routes that ride two lines.
2. Home cells read a bed assignment from the wrong moment. This reaches a sol's frozen waits, the seat sums, and the first step after a load.
3. Order 1,025 caverns, even ones you cancel at once, and every later save refuses to load.
4. A district list with a duplicated Id crashes the load after teardown.
5. A box near the int32 edge overflows its loops (cheat or hand-made file only).
6. The underground TRAFFIC overlay stays at its old place after the view turns or steps.
7. No test rolls tram waits under load in both bands.

There are also 31 nits.

---

## Findings, most severe first

### 1. should-fix: the tram walk's lower bound is valid only for routes that ride one line, so cheaper two-line routes are thrown away (D6d)

- **Where.** `Walk:507-522`, in `WalkTramCm`. It returns the plain walk when the following holds over every line k and end e:
  - `Lower = min(oct(A, stop_e) + Line.Cm + oct(stop_other, B)) >= Base`.

  The comment says any riding walk "takes some line k from one end e to the other". That is true only for one ride. A ride is priced below the octile distance between its stops (`GetTramEdgeCm`, `Tram:14-19`, `(30 + n - 1) x 100` cm). So when a route takes a second ride, the leg from line k's far stop to B costs less than its octile bound. The stop Dijkstra after the shortcut (`Walk:525-623`) does handle walk, ride, walk, ride, but it never runs.
- **Scenario.** One floor with three digs:
  - X at x -2..0, y -1..1;
  - Z at x 30..32, y -1..1;
  - Y at x 14..16, y 30..32.

  There are three legal tunnels (43, 44 and 29 cells, at most 64, from rock beside a dig):
  - T0 runs (1,0)-(29,0) and has no line (the player's choice, or `MaxTramLines` is reached);
  - T1 runs (0,2)-(15,29) and carries a 7,200 cm line;
  - T2 runs (17,31)-(31,2) and carries a 7,300 cm line.

  From cell (0,0) to (30,0), the walk is 30,000 cm and riding both lines is 22,328 cm. The bound is 44,410, which is at least 30,000, so 30,000 is returned with an empty line mask. The model, written to the oracle's rules, is `scratchpad/step2/d6d/prune.py`; the cross-check reproduced it.

  The effects:
  - the trip is overstated by 7,672 cm and can cross `CommuteLongCm`;
  - both lines' `TramPeople` is too low, so their frozen waits are too low;
  - the landing-walk memo (`Walk:705-712`) carries the same error.

  It is deterministic and the same in both bands, and no save is affected.
- **Caught by:** nothing.
  - TM2's sweep (`RHSimTramSuite.cpp:422-444`) gives the oracle one ride.
  - TM1 and TM6 lay two lines but compare no route.
  - `-scale` lays one line per floor, where the bound is exact.
  - `IsDeepWalkFresh` re-derives with the same function.
- **Fix.** Use a bound that is valid for any number of rides: `min over s of oct(A, s)`, plus `min over k of Line.Cm`, plus `min over s' of oct(s', B)`. That gives 11,614 here, so nothing is pruned. Alternatively keep the current bound only when `K == 1`. Add a TM2b sweep with this shape, against `OracleWalk(..., {ride1, ride2})`.

### 2. should-fix: home cells read a bed assignment from the wrong moment; on load the first derivation reads the previous session's bed data (D6b and D6c)

- **Where.** `DeriveDeepHomes` reads live furniture and zoning (`Dist:224`) together with `ColonistBedPiece` (`Dist:249`, `if (const int32* PieceId = ColonistBedPiece.Find(Id))`). That map is rebuilt only inside `RefreshJobs`, at the bed block (`W:6271-6278`). Three windows read it at the wrong moment:
  - **(a) Inside `RefreshJobs`.** C6a hook (b) runs `EnsureCommuteFlows` and cuts `Lab/WorkstationYieldSeatSum` for long commuters (`W:6242-6256`) *before* the bed block. The spec has home cells built "straight after `BuildBedAssignments`" (`d6-caverns-design.md:490`). At D6c the home cell became a walked distance and `bLong` (`Com:440-492`), so the bed pieces now move `LongIds`.
  - **(b) Between `StepUplink` and `StepPopulation`.** A `Designate` order lands at `StepUplink` with no `RefreshJobs` (`W:9471-9485`; `ZoningVersion` rises at `W:5897`). `StepTransit`, then `RollTransit` (`RHSimTransit.cpp:362`, `:369`), then `EnsureDeepHomes` re-derives with the new zoning and the old pieces. The resulting waits are frozen for the sol and saved.
  - **(c) Load.** `W:14841-14842` runs `bBedKeyValid = false; RefreshJobs();`, but `ColonistBedPiece` is not reset at teardown (`W:14649` resets `ColonistStations` only). So hook (b)'s first derivation reads whatever was in memory: empty in a fresh process, or another colony's piece Ids on an in-process load, which can match unrelated pieces in the loaded colony.
- **Scenario.** A mapped floor with two unjoined digs (DC1 geometry, 47,015 cm apart) and a ramp above 0. A Lab seat-holder R gets a bed piece in the near dig, while legacy fill would put R in the far dig.
  - On the step the piece lands, hook (b) still sees R far and long, and cuts R's Lab seat by Ramp x 0.15.
  - `StepDiscovery` (`W:7567`) reads the cut sum in that step, and fabrication reads the Workstation sum (`GetWorkstationFabMul`, `W:12917`) in the next.
  - The cut lasts one step, but a step is one sub-step in the agent band and 60 s in the era band, so the bands drift apart.
  - In window (b), with a re-zone landing on the step that crosses a sol boundary, the two bands freeze different shaft and tram waits for that sol. In the agent band a `RefreshJobs` runs before the boundary sub-step; in the era band none does.
  - After a load, the seat sums and Garden skill used before the first `StepPopulation` depend on what ran before the load.
  - `design-decisions.md:1101` says such a derivation "is never kept". The cut and the frozen wait keep it.
- **Caught by:** nothing.
  - `-deepdistricts` is held at ramp 0 (RC 10).
  - DC9's ramp-1 save/load twin and the `-deepcommute` fixtures have no bed furniture.
  - TM7 rolls with 0 riders.
  - `-RHVerifyCaches` re-derives from the same stale input.
- **Fix.**
  - Make the bed assignment its own keyed cache (roster, furniture and zoning versions), read by both `RefreshJobs` and `DeriveDeepHomes`. Short of that, move the bed block above hook (b): it reads only `ByIdOrder`, furniture, zoning and homes.
  - Reset `ColonistBedPiece` at load teardown.
  - Add a `-deepcommute` case with bunks: a departure, then the long flags and seat sums after one step equal a cold recompute in both bands; and the same file gives identical sums when loaded fresh and after another colony.

### 3. should-fix: order 1,025 caverns, even ones you cancel at once, and every later save refuses to load (D6a)

- **Where.**
  - `CanDigCavern` (`Cav:305-445`) never reads `Caverns.Num()`. `ExecuteDigCavern` always appends (`Cav:447`, `:454`).
  - `MaxOpenCaverns` counts only open caverns (`Cav:63-70`).
  - Ordering costs nothing, and cancelling is free (`ExecuteCancelCavern`, `Cav:474-485`). Every record stays on the list (`.h:376`).
  - The loader refuses `Count > RH::MaxCavernRecords` (1,024): `Cav:531`, giving "Save cavern list invalid: next id 1026, cavern count 1025".
- **Scenario.** A player sculpts with 1x1x1 caverns (the only way to carve one chosen cell), or orders and cancels them. After the 1,025th order, `SaveColony` writes a file that `LoadColony` refuses. An autosave or quicksave over the last good slot loses the game.
- **Caught by:** nothing. CV8's `cavr2` (`RHSimCavernSuite.cpp:630`) tests only that the loader refuses a hand-edited count.
- **Fix.** Refuse in `CanDigCavern` when `Caverns.Num() >= RH::MaxCavernRecords`, with a CV1 case. Tunnels have the same gap (`MaxTunnelRecords` 4,096 against `CanDigTunnel`, loader `W:8268`), but it predates step 2 (D3); fix both together.

### 4. should-fix: a district list with one duplicated Id crashes the load after teardown (D6b)

- **Where.** `W:14698-14714`:
  - the first district with the Id takes the shape's points by `MoveTemp`;
  - the second matches the same, now empty, shape, and evaluates `D.Deep[0]` on an empty array;
  - the same happens after the Warning branch has `Reset()` the points.

  `ReadDeepBlock` (`Dist:1400-1414`) checks only that the skeleton's Ids ascend, and the district list's duplicate-Id test (`W:14715-14716`) runs after the attach.
- **Scenario.** One flipped bit turns district #3's Id into 2 in the v37 list. The deep block passes its checks before teardown, and the load then hits a fatal range check (Development) or undefined behaviour (Shipping). The repo's rule after teardown is repair with a Warning, never a crash.
- **Caught by:** nothing. DD9's 25 hand-made files all change the deep block, never the district list.
- **Fix.** Skip an attach when `Shape.Points.IsEmpty()` (with a Warning), or remove a shape from the locals once it is attached. Run the Id-order check before the attach. Add a DD9 case with a duplicated Id.

### 5. should-fix (low): a box at the int32 edge passes the 64-bit side check, then its loops overflow, so the preflight or the loader can hang (D6a, and D6b's paint)

- **Where.**
  - The sides are checked in int64: `Cav:328-329`, and the loader at `:597-598`.
  - Every box loop is `for (int32 X = Min.X; X <= Max.X; ++X)`: `Cav:33`, `:130`, `:344`, `:380`, `:400`, the loader at `:602` and `:617`. `++X` past `INT32_MAX` is undefined behaviour; with wrap-around the loop never ends.
  - The same pattern is in D6b's deep paint (`Dist:641-643`, reached by `RH.District shape`) and in the older surface paint (`Dist:804`).
- **Scenario.**
  - `RH.DigCavern -1 1 2147483640 0 2147483647 0` (`RHCheats.cpp:676-685`): the side is 8, so the side refusal passes, and the preflight loops on the game thread.
  - A hand-made save with those corners hangs in `ReadCavernBlock` before it can refuse.
  - On arm64 an out-of-range cm value also saturates to `INT32_MAX` through `CmToLattice` (`.h:820`).
  - Normal play cannot reach it.
  - This contradicts `design-decisions.md:1019` ("an absurd point is a refusal, not an overflow").
- **Caught by:** nothing. CV1 (`RHSimCavernSuite.cpp:223`) tests one absurd corner, which the side check catches.
- **Fix.** Refuse a corner with `|X|` or `|Y|` above `1 << 20` (or outside the coverage) in `CanDigCavern`, `ReadCavernBlock` and the paint. Or loop on offsets (`for (int32 dx = 0; dx < SizeX; ++dx)`). Add a CV1 case and a CV8 case.

### 6. should-fix (presentation): the underground TRAFFIC overlay stays at its old place after a turn, a plane step, "Next shaft", Cut here or a re-entry (D8)

- **Where.**
  - The overlay hash (`Sec:1337-1493`) mixes only the numbers: shaft load, cap and wait (`:1368`); flows (`:1415`); tunnel robots (`:1439`); tram terms (`:1481-1484`); the label count; and `bRamp`.
  - `if (Hash == OverlayHash && OverlayRefreshes > 1) return;` (`:1495`) returns before the lists and labels are rebuilt.
  - `OverlayHash` is reset only in `ClearOverlay` (`:1316`), which runs only when TRAFFIC goes off (`:708`).
  - The surface strip's hash does mix `Axis`, `PlaneRow`, `Rows` and `AlongMin/Max` (`:1520`).
  - `AddBox` bakes world transforms with the current axis and origin (`:263-270`), and `Rebuild`'s `ApplyAll` (`:815`) merges the old `OverlayLists` back in.
- **Scenario.** A colony of 60 or fewer people (ramp 0, so every number is 0) with one fitted shaft. Turn the section and TRAFFIC on, then press Q:
  - the slab rebuilds for the new axis, but the lift's load bar stays at its old world position, now in front of the cut;
  - its label keeps the old rotation and is seen edge-on;
  - this lasts until a number changes: for ever while paused or at ramp 0.

  The same happens on S/W, "Next shaft", Cut here, and exit then re-entry with TRAFFIC on. The hash also adds numbers that can collide: `L * 7919 + X * 31 + Y` collides for one more commuter out against 31 fewer in.
- **Caught by:** nothing. The click test turns TRAFFIC on only after the turn and step checks, and off before V and dive.
- **Fix.**
  - Mix `Axis`, `PlaneRow`, `Rows`, `AlongMin/Max`, `DeepestSeen` and the rounded `Origin` into the hash, or zero `OverlayHash` and `NextOverlayS` whenever `Rebuild` runs with `bAll`.
  - Fold the numbers with `MixU32` one at a time.
  - Add a click step: TRAFFIC on, then Q, then check that every overlay label lies in the free band.

### 7. should-fix (test gap): no test rolls tram waits under load in both bands (D6d)

- **Where.** `RHSimTramSuite.cpp:708-736`. In each TM7 leg, `LoadColony`'s `RefreshJobs` wipes the hand-laid seats (the fixture has no rooms), and they are laid again only after the step. So the roll sees 0 riders, and the test asserts exactly that ("wait0 prev600"). TM6 (`:582-649`) rolls by hand before its save, and after the load checks only the commute.
- **Scenario.** A regression that makes the threshold roll count riders differently by band, or that loses something the next roll reads after a load, passes TM7. Finding 2, window (b), is such a difference. The design-decisions claims "waits are equal at every threshold" and "era and agent roll alike at the sol boundary" are exercised only at load 0.
- **Fix.** Give TM7's riders seats that survive the step (real rooms, or re-seat before `StepTransit` through a harness hook). Assert a nonzero wait that is equal in both bands. Add a third leg that never saved.

### 8. nit (spec gap, before D7): refusal 5 turns down a multi-floor box when one of its floors is already fully dug inside the box (D6a)

- **Where.**
  - `Cav:397-411` needs an eligible point on every floor of the box.
  - `IsCavernPointEligible` (`Cav:95-103`) is false for any carved point, so a floor fully dug inside the box can never pass.
  - The code follows MERGED section 1, refusal 5, to the letter (`d6-caverns-design.md:393`; RC 2 at `:285`).
  - Refusal 7 catches only a box dug on every floor.
- **Scenario.** The CV1 setup: (1,0) to (1,1) on floors -2 to -4, where -2 and -3 are already dug inside the box. The order is refused on -2 with "the box touches no dig and no shaft", although -2 is a dig. The carver would finish the box: `FindCavernWork` skips floors with nothing left (`Cav:229`).

  D7 (`d7-atriums-design.md:76`, `:95`) makes atriums from finished caverns of two floors or more, so a hall can never be extended downwards over ground already dug.
- **Fix.** A director call before D7: apply refusal 5 only to floors with rock left in the box. At least, fix the reason text.

### 9. nit (doc): deviation 8 understates the D5 warmth parity gap caused by robot charging (D5)

- **Where.**
  - The scraping test reads the bank at the start of the step (`W:8556-8559`).
  - Pad charging drains the bank in the agent band only (`W:13018-13028`).
  - `EraLogistics` (`W:870-901`) only derates digging by the duty cycle.
- **Scenario.** D5 accepted this: `d5-depth-design.md:636`, and `design-decisions.md:990`, "existing parity class; logged as a risk". DP9 has no robots. But a 500 W pad drains about 10 Wh a sim-second, and with `HeatRiseSols` 1.0 every 120 s of earlier onset adds 0.1 Warmth. One RC_E topping up 1,300 Wh at night moves the agent band about 0.3 Warmth above era, against DP9's 0.1 tolerance. "Slightly earlier" does not describe that.
- **Fix.** Restate the deviation with its size, or drain the fleet's duty-cycle charge energy from the bank in `EraLogistics`, and add a DP9 variant with a docking RC_E.

### 10-38. nits

**D5**
- **10. The warmth tint and "warm" label go stale between 0 and 10%.**
  - `WarmthVersion` moves only on a 10% step (`Depth:111-114`), so 0 to (0, 0.1) and back never bumps it.
  - The tile tint and D8's "· warm" label (`Sec:72`, `:1164`) go stale: a floor back at 0 keeps "warm", and one at 0.09 never shows it.
  - Fix: also bump when `(Old > 0) != (New > 0)`.
- **11. The POWER tile counts floors as machines.** "%d machines shut off" (`RHCommandDeckPanels.cpp:399`) now counts the cooling floors shed (`W:8584-8591`). Count buildings and floors separately.
- **12. Two dig-hour quotes ignore depth.** The tunnel brush tip (`RHPlayerController.cpp:1018-1021`) and the Excavate label (`RHCommandDeck.cpp:2074`) leave out `GetDigPermille`. A 64-cell tunnel on -20 is quoted at 192 Borer-hours and costs 624.
- **13. The warm-alert latch is not saved.** `InstallWarmth` re-arms it only at 0.25 or more (`Depth:227-230`). Alerts differ after a load; no sim state does. The spec accepts this; document it.
- **14. The warmth block depends on floor-table order, which `ReadFloorTable` never checks.** The block is written in floor-table order (`Depth:151-159`) and read requiring descending levels (`:200-203`). A hand-made file with floors out of order loads, then its next autosave is refused. Refuse an unsorted floor table, or write warmth in level order.
- **15. The docs disagree on who owns lift power.** The commit says D6e; `design-decisions.md:983` says D6, with trams. No later gate draws lift or tram power.

**D6a**
- **16. A cancelled flag above 1 logs an engine Error.** `bCancelled` is serialised as a UE bool (`Cav:571`). A hand-made value of 2 logs `LogSerialization` Error and is refused as "cut short". The tunnel flags (`W:8255`, `:8292`) have the same pattern. Use int32 and refuse values outside {0, 1}.
- **17. Deviation 2 misstates what happened before D6a.**
  - Before D6a, X from 4 to 99 went to the tunnel branch (`9364a5d^`, `W:1542`), not the carve branch.
  - X below 0 carved a cell, and now completes with spoil and no cell (`W:1592-1595`).
  - The second change is not logged.
- **18. The wrong reason below the deepest floor.** A box below `-MaxDepth` is refused as "not reached - bore a shaft that deep first" (`Cav:315-318`); no shaft can ever be bored that deep. Give it refusal 2's own reason.
- **19. An orphaned cavern work record never completes.** `PendingBoreWork` is loaded without checking each key is a Borer with a batch running (`W:14332-14333`). `GetCavernBatchesInFlight` (`Cav:76-84`) counts the orphan, which keeps a cancelled cavern protected for ever. Hand-made files only; tunnels had the same gap. Drop such records after the buildings load, with a Warning.
- **20. The completion log prints the box size.** "cavern complete (%d cells)" (`Cav:267-268`) prints the box, not the cells carved.

**D6b**
- **21. Carving through deep growth makes no spoil.** `CompleteDeepCells` and the deep civic branch carve through `CarveLatticePoint` (`RHSimGrowth.cpp:1534`, `:1626`) and drop no spoil or `SpoilChemicals`, unlike whole-floor growth through `ExcavateFloor` (`W:8447-8453`). Add it, or log it as a deviation.
- **22. Pending beds overstate a `DeepCells` project that zones free cells.** `HomeSites() * PerSite` (`RHSimGrowth.cpp:140`) counts the legacy beds a free cell already has. DD6 pins 16 where the real gain is 13, so a growth start can slip by a sol.
- **23. The repairs after teardown are incomplete for hand-made files.**
  - A shape with no matching district is dropped silently (`W:14698-14714`).
  - The whole-floor or shape class is not re-checked after the attach.
  - A `DeepCells` project's `Cells/FirstCell` are not checked against its points. `Cells=1, FirstCell=0` with 4 points puts 400 kg of O2 into the floor on a 100 kg bill (`RHSimGrowth.cpp:1510-1511`).
- **24. The growth point scan is slow on a huge shape.** It rescans the whole shape for each pick and runs the tunnel loop before the cheap test (`RHSimGrowth.cpp:770-795`). A 65,536-point Homes shape costs an estimated tens of ms at a sol boundary. This is an estimate, not a measurement; no `-scale` budget covers growth.

**D6c and `41ac355`**
- **25. Walk-network build cost grows with the cube of dig nodes.** The build is about k³ per floor in a dig's nodes (`Walk:137-149`, `:322-329`), and the key is global (`Walk:353`). Lined tunnels (2 doors a cell) push k to about 241: about 280 M checks over 20 floors on every carve. `-scale`'s tunnels have about one door per floor.
- **26. Road load leaves out the surface leg of two-shaft trips.** The distance includes it (`Walk:779-780`, `:850-851`), but `Com:863` loads no surface edge when both ends are below ground. Presentation only.
- **27. A cancelled tunnel's line splits a cavern hall.** Its chain cells count as tunnel cells whatever its state (`Walk:180-199`), so a hall carved across the line costs +31% to walk across. A director call.
- **28. `-RHVerifyCaches` rebuilds the whole network about twice per new trip** (`Walk:359`, with `:640` and `:683`), which is why no verify run covers 20 floors. Compare at most once per derive.
- **29. Off-net same-floor trips cost 0 cm** (`Com:257-260`, `:279-281`), while bedded neighbours pay real walks. This follows the spec; a director call.

**D6d**
- **30. The first-line alert promises 600 people a sol.** It is filled from `TramPlacesPerSol` (600; `Tram:163`), but each rider takes two places (`Tram:51`, `Com:620-633`), so a queue forms above 300.
- **31. Laying a line maps its floor and lengthens every commute there.** `Dist:87-92`, pinned by TM5. This follows the spec, but the N1 alert says nothing.
- **32. The `TramVersion` comments are wrong.** They say it rises on every load (`.h:1087`, `:3335`); the code raises it only when a line exists before or after (`W:14286-14292`). The code is right.

**D8**
- **33. Loading while the section is open keeps the old cut, shaft id and saved pose.** Nothing subscribes to `OnColonyReloaded`. `GetShaft` falls back to `Shafts[0]` (`.h:774`), and the next Q re-cuts through shaft 0. Exit the section on reload.
- **34. `section.unit`'s classification check cannot fail.** `ClassifyRow(0 / -1 / 1)` (`RHClickTest.cpp:1913-1914`) is the same expression whatever the input, and `ClassifyRow` is never called by the code that draws. Unit-check `InSlabRow`, `IsInSlab` and `Pick` instead.
- **35. `section.locks.rmb` and `.mmb` pass even with no lock.** The synthetic drag sends no button in the pressed set (`RHClickTest.cpp:831-858`). Add a positive control in the floor view.
- **36. Several click steps assert less than their names say.** `section.on`'s queue check can be hidden by a delivery. `section.cut`'s focus half is vacuous (the cavern's bottom is -1, and so is the focus). `section.traffic.off` does not check that the section's layers cleared. `section.V.off` does not check that the pose is restored.
- **37. The frame-budget figures predate HEAD.** UI polish `86f23a4` added `ComputeLabelRoomCm` to every `BuildShared` (`Sec:1062`), including the 1 Hz people-only rebuild. "Worst rebuild 2.58/3.13 ms" is a D8-era number. Re-run `RH.DeepBench` at HEAD, and cache the label room on the structure key.
- **38. Two smaller D8 issues.**
  - A focus change can leave stale pips on floors that stay at full detail (`Sec:671-677`; `PeopleKey` advances without a people redraw).
  - Arming Tunnel from the section shows "pick a subsurface floor" and costs the entry floor (`RHCommandDeck.cpp:2433-2441`), because `ActiveLevel` is frozen. Use `GetShownLevel()`.

---

## What holds (checked, not assumed)

**1. O1.** Every carve in step 2 appends through `AppendCarvedCell` (`W:5314-5319`): a cavern through `CarveLatticePoint` (`Cav:198-209`), growth through `RHSimGrowth.cpp:1534/1626`, and a tunnel's cells through `W:8187-8189`.
- The plan's `Remove` keeps its order.
- No step-2 code holds a position in a floor's `Plan` list across calls.
- The tunnel, cavern and tram lists have no Remove, Insert or Sort anywhere.
- Cancelling only sets a flag.
- CV9 pins ordinal-to-point across a save and a load.

**2. Saves (v47 to v50).**
- Every block reader takes the earlier loaded blocks as locals. This covers `ReadFloorTable` (`W:5642`), `ReadShaftBlock` (`:5773`), `ReadTunnelBlock` (`:8260`), `ReadTransitBlock`, `ReadWarmthBlock`, `ReadCavernBlock`, `ReadDeepBlock` and `ReadTramBlock`. None calls a live accessor.
- Each block refuses through `OutError` before teardown, and each can read its own game-written files (finding 3, the record cap, is the one exception):
  - shafts never retract;
  - finished tunnels cannot be cancelled;
  - shapes never shrink;
  - lift cars are never removed.
- Save mid-carve and mid-growth is pinned (the CV8 mid-batch twin, DD9's twin). Riders are derived, and `WaitSec`, `PrevWaitSec` and `TransitCheckedSol` are saved.

**3. Caches.**
- **Walk network** (`Walk:353`): `FloorCellsVersion`, `TunnelsVersion`, `ShaftsVersion`, `DistrictsVersion` and `TramVersion`.
  - Every carve bumps cells; tunnel order and cancel bump tunnels.
  - Shaft depth goes through `RebuildShaftDerived` (`W:5448`).
  - Laying a line bumps tram and districts (`Tram:156-157`).
- **Trip memo:** lift, built, config and tram timing, and it dies with the network.
- **Flows:** separate `Deep*` fields, which are 0 with no mapped floor.
- **Home cells:** roster, furniture, zoning, cells, districts and bed assignment.
- **Deep net:** cells, shafts, lift, built and tunnels.
- **On load:** cells, tunnels, shafts, districts, lift and wait are bumped, and the flows, memo and `DeepNet` are reset.
- **The kept `SurfaceRoutes`:** `BuiltVersion` only goes up (`RHRoads.cpp:59`, `:76`).
- No key adds two versions into one field.

**4. Determinism and era parity.**
- Every step-2 map is used for lookups only. Order comes from arrays (Id-ascending) or sorted keys (`GetMappedLevels`).
- Walk searches are integer and produce distances only. Shaft picks use strict `<` in Id order, and a tie goes to the walk.
- Caverns pick the lowest (X, Y). Several Borers landing on one cavern in a step land in an order that does not matter.
- D5's scraping rule is one function in both spines, with linear terms. Dig time is applied the same way everywhere:
  - every Borer batch start (`W:1679-1682`);
  - the cavern preview (`Cav:387`);
  - growth (`RHSimGrowth.cpp:733/850/1352`).
- The tram roll runs after the shafts, in Id order, all in integers.

**5. `41ac355` is bit-identical by code.**
- `FTransitSearch` pops (Key, Node) in a strict total order, the same sequence as the old linear scan.
- `Edges` use the old `Adj` predicate and kind priority; the ride price is the same integer, split up.
- `LevelPortals` keeps ascending order.
- `BuildLatticeNeighbours` matches `FindOrdinal`, duplicates included.
- `PickedShaft` makes the same pick.

The nine CRCs, the timings and the 25,551 verified memo hits rest on runs.

**6. Inertness, checked in code.** Claims that rest on runs were not re-run: byte-identical playtests, battery line counts, mutation counts, ms budgets, and the click test's REACH counts.
- **D5:** the `== 1000` permille test, `CoolingEntries > 0`, `StepDepthWarmth`'s early return, and `+0.0` Hope terms. The default block is 4 bytes.
- **D6a:** the Borer branch is gated on `Caverns.Num() > 0`, and only `X >= 100` reaches the cavern completion. The default block is 8 bytes.
- **D6b:** with no shape, `IsFloor()` answers as before, the key fields are 0, and growth and civic take their old branches. The default block is 12 bytes.
- **D6c:** every sim reader is gated below the ramp, and an unshaped floor measures as before.
- **D6d:** with no line, `WalkTramCm` returns `WalkCm` at `K == 0` (`Walk:494-497`), and the keys stay 0. The default block is 8 bytes.
- **D8:** the section-off paths reduce to the pre-D8 expressions at every converted site.

**7. Architecture and D8.**
- `RedHopeSim.Build.cs` depends only on engine modules. No RedHopeSim file includes a RedHope header. `git diff --stat f461828~1 f461828 -- Source/RedHopeSim` is empty.
- The section visualizer reaches the sim only through `const` references. The PC's section functions use the const sim and enqueue nothing.
- Writes from D8 code exist only in `RH.DeepBench` and the click test's setup (`Debug_*`, `ExcavateFloor`).
- Clicks in the section return into `HandleSectionClick` before any order path. No mode survives a frame in the section.
- No D8 code uses RNG, and RedHopeSim uses no global `FMath::Rand*`.

**8. Performance in the gates' own scope.** D5 adds O(floors) work to `StepPower`, with no floors × cells loop. A cavern preflight covers at most 192 points. The D8 per-frame work is a version hash, up to 128 car transforms, and 2 Hz overlay and strip passes. The exceptions are findings 24, 25 and 37.

---

## Dropped (unverifiable, or not a defect)

- **Era and agent bands disagreeing when several Borers land on one cavern.** Disproved: each landing takes the lowest eligible point, so the order of landings does not matter.
- **The walk network going stale when a shaft deepens.** Every depth change bumps `ShaftsVersion` through `RebuildShaftDerived`.
- **`41ac355` changing which edges exist or their order.** The `Adj` and `Edges` predicates are identical.
- **The D5 unstable sort with cooling entries (R2).** Accepted by the spec; no concrete permutation could be shown without running.
- **D5 frame-end solar, and robots charging free after a shed step.** Both predate D5 and do not move Warmth.
- **`RefreshDepthTint` re-tinting every deep tile on each new tile.** The cost was not measured, and whether a new tile is always tinted could not be confirmed.
- **Era orders landing up to a frame early** (`StepUplink` reads the frame total). This applies to every verb and predates step 2.
- **`NextCavernId` / `NextTramId` overflow from a hand-made `INT32_MAX`.** Hand-made only, and shared with older blocks. Finding 3's cap covers caverns.
- **A cancelled tunnel later fully carved never taking a line.** The spec refuses a cancelled tunnel.
- **A deep civic carve, then `DesignateRoom` failing, refunding O2 twice.** Not reachable: a fresh cell has no module, and the row is checked first.
- **A cavern carving a growth point first, so the O2 is paid twice.** The O2 stays in the floor's air.
- **The cavern visualizer's key, `OverlayLabels` growing, a stale `RmbDragPx`, `ApplyViewLevel` running twice, and text-component GC.** Cosmetic only, or not shown to fail.
- **`BuildBedAssignments`' sort key colliding past a million furniture Ids.** It predates step 2.
- **The commit and design-decisions figures that come from runs:** battery diffs, mutation counts, CRCs, timings, captures, and checkpoint remakes. Not re-run, as this review forbids running.
