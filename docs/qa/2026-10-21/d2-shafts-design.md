# D2, more shafts: design (2026-10-21)

Step 2 (going deep), gate 2. Design only. No code is written.
Anchors are read on main @ 41133b2 (D1) with `git show`, not from the working tree, which the Gate-D pass is editing now. Line numbers will move.
Sources: `docs/qa/2026-10-20/going-deep-plan.md` (D2 row, D2 risks; Q1a Q2a Q3a Q4a answered); `docs/qa/2026-10-21/d1-floors-design.md` (REVISION, R0's O1 deviation, O9); `docs/qa/2026-10-20/c6-traffic-design.md` (commute anchors); the C4 REVISION (house rules).

**Goal.** A list of shafts, each with its own head, depth, Borer, carve queue and RegionId. The player sinks a new shaft where they like, inside coverage. Shaft 0 is today's shaft. With one shaft, nothing changes: every battery line is identical except the lines that print the save version.
**What you see.** You build a second Borer across the valley, order "Sink a shaft here", bore it, and dig a separate floor under it.

---

## 1. Data

**FRHShaft** (new, `RHSimWorldSubsystem.h`, next to `FRHFloor` at `.h:264`):
- `int32 Id`: dense, equals its index. Never reused; step 2 has no shaft removal.
- `int32 RegionId`: always 0 in D2.
- `FIntPoint Column`: the lattice point in region 0's frame. Shaft 0 is (0,0) by construction.
- `FVector HeadCm`: shaft 0 keeps today's exact `ShaftHeadCm` (the Borer's location, off-grid). Shaft k>0 is `LatticeToCm(origin, Column)` (`.h:500`), Z 0. Surface Z is always 0 (`.cpp:8492`).
- `int32 Depth`, `int32 BoreTargetDepth`, `TMap<int32,int32> CarveQueue`: today's `ShaftDepth`, `BoreTargetDepth`, `CarveQueue` (`.h:2202`, `:2220-2221`) move into the shaft.
- `int32 BorerId`: the Borer that anchors it. 0 = none (shaft 0 anchored by `ExtendShaft`, the test path).
- No lift field. In D2 the lift is the trunk itself and stays free and endless. D4 adds capacity here (Q4a). No dead saved field now.

**The list.** `TArray<FRHShaft> Shafts`. `Shafts[0]` always exists, with Depth 0 and a zero head until the first bore, exactly like today's members.
- Derived, never saved: `DeepestShaft` (max Depth), `ShaftColumns` (a lookup set), `BorerShaft` (Borer Id -> shaft Id), `ShaftsVersion` (up on sink, on every Depth change, on the first head fix, and on load).

**Floors are shared: one lattice per (region, level).** Decided.
- Two digs on floor -1 are one `FRHFloor` with one `Cells` array. Ordinals interleave when two Borers carve it (section 4).
- Why: D3 tunnels join separate digs on the same level. With per-shaft floors, a join would have to merge two ordinal spaces, and that is a re-key. With one lattice, a join is just two cells becoming neighbours.
- `FloorIndexByLevel` (`.h:2205`) stays keyed by Level. A second region will key it by (region, level).
- Per-floor systems stay per floor: one air pool, one rating, one district (risks R1, R2).

**Region 0's origin stays shaft 0's head, forever.**
- D1 derives the origin from `ShaftHeadCm` (D1 REVISION item 2, O3). D2 keeps that: a new shaft needs `Shafts[0].Depth > 0` (section 2), so the origin is fixed before any other shaft exists. No origin field is saved in D2 either.
- `GetShaftHeadCm()` (`.h:514`) keeps meaning "shaft 0's head = the origin". Every presentation site that does `Head + GetFloorCell(L,i) x 1000` (D1 item 5) is still exact for cells under any shaft. New code reads `GetShaft(Id).HeadCm`.
- Shaft k's column is `CmToLattice(origin, Borer.LocationCm)` (`.h:501`), so it snaps to region 0's grid.

**The Borer per shaft.**
- A Borer anchors a shaft once: shaft 0 at its first bore completion (`.cpp:1346`, where `BorerId = B.Id` is added beside `ShaftHeadCm = B.LocationCm`), shaft k by the SinkShaft order.
- An anchored Borer works only its own shaft. An unanchored Borer is a helper and works shaft 0, which is exactly today's rule for a second Borer.
- The work loop (`.cpp:1376-1423`) reads S = `BorerShaft[B.Id]` or 0, then runs today's logic on S's fields. The "bore in flight" scan (`:1379`) only counts records whose Borer maps to S.
- `PendingBoreWork` (`.h:2225`) keeps its format. The shaft is derived from the Borer, and SinkShaft refuses a Borer with work in flight, so a record's shaft never changes.

**Carve order per shaft.** `CarveNextCell(Level, S)` and `PeekCarveCells(Level, N, Out, S)` (`.cpp:4643`, `:4671`):
1. The first `Plan` point that S owns, in paint order. A plan point's owner is `ServingShaft(Level, P)` (section 7): for an uncarved point, the nearest shaft that reaches Level, ties by the lower Id.
2. Otherwise S's own spiral: `Column_S + FRHSpiralWalk.Next()` (`.h:237`), skipping any point that is carved on Level, any shaft column, or any plan point.
- Each floor keeps one transient cursor per shaft (`Walks[S]`; D1's `Walk` is `Walks[0]`). A cursor only advances over permanent blockers (carved points and columns). It is re-derived at load.
- With one shaft: every plan point is shaft 0's, the spiral runs only when the plan is empty, and the only column is (0,0), which the walk never yields. So the rule reduces to D1's, cell for cell.
- `ExcavateFloor(Level, N, Why, S = INDEX_NONE)` (`.cpp:6601`): INDEX_NONE means the lowest-Id shaft that reaches Level. Growth and the -scale builder use that default.

## 2. The new-shaft order

**Verb `SinkShaft`**, `Cmd.Value` = the Borer's building Id. It executes at the uplink step like every order. The preflight runs at execute time, because the column could be carved while the order is in flight. Refusals, each with a reason:
1. Not a completed `CanBore` building.
2. It already anchors a shaft. This includes shaft 0's Borer.
3. It has a batch in flight (`PendingBoreWork.Contains(Id)`): "wait for its batch".
4. Shaft 0 is not bored yet (`Shafts[0].Depth == 0`): "bore the first shaft first". This keeps the origin fixed.
5. `Shafts.Num() >= MaxShafts`. New `RH_Config` row `MaxShafts` 4, placeholder for the director; CSV and DT synced.
6. The head is outside coverage: `IsInCoverage(HeadCm, 0)` (`.cpp:10379`). The Borer was placed inside coverage, and pylons cannot be demolished (`:8651`), but the check is cheap.
7. The snapped column equals another shaft's column.
8. The column is carved on any floor of the region (O9, section 4), or sits in any floor's `Plan`.
- **On success:** a new `FRHShaft` gets Id = `Num`, the column, the head, Depth 0, `BorerId`. `ShaftsVersion` goes up. Log: `Shaft 1 sunk at (x, y) by Borer #n`.
- **Footprint.** On the surface, the Borer stands on the column's cell. Underground, the column has to be free on every level (item 8), and from now on every cell writer skips it (section 4).
- **Cost.** Nothing beyond the Borer, which was already built and paid for. Each floor is then a normal `BoreFloor` batch (H2 or power, spoil), as for shaft 0. Depth pricing is D5 (Q3a).
- **Bore and Excavate pick their shaft through `Cmd.Target`.** NAME_None means shaft 0, which covers every existing order, test and cheat. `Shaft<k>` means shaft k. Anything else is refused as "Unknown shaft". The queued-order save format is unchanged (`Target` is already saved).
  - Bore (`.cpp:7246-7273`). For shaft 0, "No Borer online" now means "no complete Borer that is shaft 0's or a helper". With one shaft that is the same set. The clamp and texts stay the same for shaft 0. Shaft k's texts name it.
  - Excavate (`:7275-7292`). The check becomes "shaft S reaches Level". Shaft 0 keeps today's text (`"Floor %d not reached - bore the shaft deeper first"`).
- **Test path.** `Debug_SinkShaftAt(HeadCm, Depth, OutId, Why)` runs refusals 4-8, then extends the new shaft. It is the analogue of `ExtendShaft` (`.cpp:6575`), which stays the shaft-0 path.

## 3. Every single-shaft consumer

Sim (`RHSimWorldSubsystem`):
- `ShaftHeadCm` / `ShaftDepth` members (`.h:2202-2203`) become `Shafts[0]`. `GetShaftHeadCm()` returns `Shafts[0].HeadCm`, bitwise the same value. `GetShaftDepth()` (`.h:475`) returns `DeepestShaft`. New: `GetShafts()`, `GetShaft(Id)`, `GetShaftDepth(Id)`, `GetBoreTargetDepth(Id)`.
- `IsLevelConnected` (`.h:476`): `-Level <= DeepestShaft`. Every shaft starts at the surface, so a level is reached exactly when the deepest shaft reaches it.
- `AreLevelsLinked` (`.h:479`): the same formula. A shaft that reaches the deeper level also reaches the shallower one, and every shaft opens to the surface, so any two reached levels are linked. Callers are unchanged: the site hopper `:1211`, `TryClaimBuild` `:10699`, roads `RHSimRoads.cpp:59`/`:93`, growth `RHSimGrowth.cpp:172`/`:266`.
- `GetApproachPoint` (`:2335-2345`): uses the head of `ServingShaft(SiteLevel, CmToLattice(origin, SiteLocation))`, restricted to shafts that reach both levels, with `Z = RobotLevel x FloorH`. See section 7.
- `IsNetShaftLinked` (`:2917-2935`) and its inline copy in the demolish check (`:8756`): "a bay within `ShaftLinkRangeCm` of **any** head whose shaft has Depth >= 1, while -1 is rated". It is a boolean over Id order, so the answer is order-free. Callers `:8817`, `:8826` are unchanged.
- Demolish refusal (`:8656`): refuse if the Borer anchors any shaft, or is a helper while `Shafts[0].Depth > 0` (today's rule), or has work in flight.
- Bore completion (`:1340-1351`): S's Depth goes up by one. The shaft-0 log text is kept. Shaft k logs "Shaft k reached floor -n".
- Carve completion (`:1354`): `CarveNextCell(Work->Y, S)`. The log (`:1355`) keeps `GetCarveQueued(L)`, which becomes the sum over shafts; with one shaft it is shaft 0's queue.
- `Debug_PaintCarve` (`:4715-4740`): a level reached by any shaft. It refuses every shaft column with today's text, "That is the shaft column".
- `FindCarvedCellAt` (`:5343-5352`), `GetFloorCellCentreCm` (`:4709-4712`), `GetCellCentreCm` (`:9637`): all measure from the origin. Unchanged, and correct for every dig.
- Placement on a floor (`:6813`, `:6958`): `IsLevelConnected` as above. The trunk taps the whole floor, as the comment says.
- `StepHabitability` (`:2371`), `IsFloorCirculated` (`:2347`): per floor, unchanged (R1).
- `CanEnterEraMode` (`:593`): unchanged (section 6).

Commute (`RHSimCommute.cpp`), C6's single-shaft anchor:
- The key (`:60`) and the memo (`:70-78`) replace `Shaft` (one head in whole cm) with `ShaftsVersion`. Both are internal and never printed.
- `GetCommuteDistanceCm` (`:169-183`): one end on floor L and the other at surface point P costs the minimum, over shafts that reach L, of `|L| x Per + CommuteSurfaceCm(Head_S, P)`, ties by the lower Id. Two floors: `|Lh - Lw| x Per`, unchanged.
- `DeriveCommuteFlows` (`:198`, `:261`, `:267`): a floor anchor's `Pos` is the head of that trip's chosen shaft, stored as a derived `PortalShaft` on `FRHCommuteTrip`.
- `CommuteFillEdgeLoad` (`:470-478`) reads `PortalShaft`, so road loads use the same head as the distance.
- With one shaft, the calls, their arguments and their interning order are identical.
- Cost: at most `MaxShafts` extra Dijkstra sources, one per head. They are memoized by position, as now.

Growth, districts:
- Growth (`RHSimGrowth.cpp:676`, `:691`, `:1019-1037`, `:1193-1205`, `:1242-1269`): unchanged calls. `ExcavateFloor` and `PeekCarveCells` both default to the lowest-Id shaft that reaches the level, so the ghost still equals the carve (D1 K10). Growth never sinks a shaft.
- Districts: a floor district is still the whole floor (`RHSimDistricts.cpp:66`, `:227-229`). The refusal at `:341` uses `IsLevelConnected` as above. Shapes come in D6.

Presentation (section 9): `RHPlayerController.cpp:733`, `:763`, `:1995`; `RHColonyVisualizerSubsystem.cpp:3313-3345`, `:4326`, `:4512`, `:4585-4600`; `RHCrewVisualizerSubsystem.cpp:1248`, `:1357`, `:1375`, `:1388`, `:1534`, `:1565`, `:1929-1960`; `RHAudioSubsystem.cpp:231`; `RHCommandDeck.cpp:1486-1489`, `:1998-1999`, `:2009`, `:2035`, `:2051`, `:2075`; `RHCheats.cpp:348`, `:367`, `:1412-1416`, `:1613-1627`, `:2031`.
- These need no change: `RHHabLayout.h:75` (shaft 0 Depth > 0 holds exactly when `DeepestShaft` > 0), `RHCommandDeckGuide.cpp:169`, `:184`, `:317`, `RHPlaytest.cpp` (one shaft), and the robot brains `RHRobotStateTree.cpp:115` and `RHRobotTaskProcessor.cpp:199` (they call `GetApproachPoint`).

## 4. O9: refuse shaft columns on carved cells; keep the ordinal keys

**Decision: refuse, and do not re-key.**
- **Refuse.** SinkShaft refusal 8: the column must not be carved, or planned, on any floor.
- **Every writer keeps every column free afterwards.** These are the only ways a cell gets into `Cells` or `Plan` (`AppendCarvedCell` is the only writer of `Cells`):
  - `CarveNextCell` / `PeekCarveCells` skip all columns;
  - `Debug_PaintCarve` refuses them;
  - load refuses a column in any `Cells` or `Plan` (section 5).
- D1's invariant "never the shaft column (0,0)" becomes "never any shaft column".
- **Are ordinals per floor?** Yes. An ordinal is a position in one floor's `Cells`, and D2 keeps one `Cells` per (region, level).
- **Do two shafts interleave on one floor?** Yes. Borer batches complete in building order, so shaft 0's and shaft 1's cells alternate in `Cells`. That is still append-only: each ordinal names one point forever. Nothing reads an ordinal as "near shaft 0". I checked every place that assumes a contiguous ordinal range:
  - Growth `CompleteFloorCells` takes `First = count` and carves N in one `ExcavateFloor` call (`RHSimGrowth.cpp:1204-1217`). The call is atomic within the step, and no Borer batch completes inside it, so First..First+N-1 are its own cells.
  - `FloorRoomCells` is an ordinal prefix (`SetNum(Carved)`, `.cpp:4891`). Interleaving does not change what a prefix means.
  - `PlanFloorHomes.FirstCell` (`:691`) is a snapshot that completion re-reads (`:1204`).
  - Queued orders resolve their ordinal at execute time (D1 R4). `TileByCell` in the presentation is keyed by ordinal and stays valid.
  - The -scale builder uses `ExcavateFloor` with one shaft.
- **The adjacency box.** Two digs 300 m apart on one floor make a bounding box far larger than 8R. The pass then takes D1's hash path (D1 REVISION item 3). That path gives the same results and was built for this; the cost is measured in -scale.
- **Re-keying waits**, as D1 R0 says, for the first gate that removes or reorders a cell. Nothing in D2 does.

## 5. Inertness, and the save

**With one shaft, nothing moves:**
- `Shafts == [shaft 0]`, `DeepestShaft == Shafts[0].Depth`, and `ShaftColumns == {(0,0)}`;
- `ServingShaft` always answers 0 (a fast path for `Shafts.Num() == 1` returns before any component work);
- the carve rule reduces to D1's (section 1);
- every shaft-0 log string is kept;
- the commute calls are identical.
So every battery suite (44 runs after the Gate-D pass, 45 with `-shafts`) must be line-identical against the latest baseline in `Saved/baselines` (`battery_post_d1` today; the Gate-D pass will re-cut it).

**The only allowed diff lines** are lines whose only change is the save version number:
- `traffic.log` X15: two lines out, two in;
- `deep.log` K6: the "file says version", "hand-made vN file" and "a v(N-1) file is refused" lines (working tree `RHSimCommandlet.cpp:11308`, `:11323`, `:11352`).
- Recount them literally at build time; they depend on what the Gate-D pass lands. The "(N bytes" line is already masked by `diff_battery.py`. Any other diff fails the gate.
- The 110-sol playtest must stay byte-identical, as in D1.

**Save: the next free version.** That is v43 if the Gate-D pass keeps v42, or the next one after it if W1 lands first. Strict, as always.
- **New block, straight after the floor table** (`WriteFloorTable`, `.cpp:11288`). It goes after it, not before, so the K6 byte offsets (header + floor block at offset 20) do not move. Layout:
  - `int32 ShaftCount`;
  - then, per shaft in Id order: `Id`, `RegionId`, `Depth`, `BoreTargetDepth`, `BorerId`; then `HeadCm` for shaft 0, or `Column.X`, `Column.Y` for k>0 (the head is derived); then `CarveQueue`.
- **The old slot** (`:11359-11360`, load `:11848-11851`) becomes `Ar << SpoilPileKg; Ar << PendingBoreWork;`.
- **Load** reads the block into a local, right after `ReadFloorTable` (`:11717`) and before the teardown. **Structural refusals** go through `OutError` ("Save shaft list invalid: ..."), with no Error line:
  - count < 1 or > 16;
  - Ids not 0..n-1;
  - `RegionId != 0`;
  - Depth or target outside [0, MaxDepth];
  - a shaft k>0 while shaft 0's Depth is 0;
  - a repeated column, or (0,0) for k>0;
  - a column in any loaded floor's `Cells` or `Plan`;
  - a `CarveQueue` key outside [-MaxDepth, -1], or a negative count.
- (0,0) in a floor is still refused by the floor block, with its current text, so K6's strings hold.
- **Warning repairs** (they need the buildings, which load later): a `BorerId` that names no Borer is set to 0; a Borer anchoring two shafts keeps the lower Id.
- Derived state is rebuilt after the load: the cursors, `ShaftColumns`, `BorerShaft`, `DeepestShaft`. `ShaftsVersion` goes up.
- The hand-play checkpoints are refused as the old version and re-make themselves, as at every bump.

## 6. Era parity

- Borer batches (bore and carve) run in `StepProduction`, which `EraStep` calls (`.cpp:711`). So era bores and carves every shaft through the same code. **Yes: era can carve a second shaft's floors**, and in the same order.
- **I do not refuse era while a shaft is being sunk.** The order itself is instant bookkeeping. The Borer is already built (an unbuilt Borer already blocks era: `:595-601`). Bore and carve batches do not depend on the band. The same is true of shaft 0 today, and it is not refused either.
- **Deviation, to log in `docs/design-decisions.md`, as C4 did for roads.** Era logistics teleports, so era ignores which shaft serves a site. The agent band drives to the serving head. The bar is the usual 5% ledger over 10 sols, tested with two shafts (SH12).

## 7. Robots still stop at a shaft head: which one

`ServingShaft(Level, P)`, one deterministic rule, used by `GetApproachPoint`, by plan ownership and by the presentation:
1. Candidates are the shafts with Depth >= -Level. If there are none, the answer is INDEX_NONE.
2. If P is carved: keep only the candidates that **touch P's dig**, when any do. A dig is a 4-connected component of carved cells. A shaft touches a dig when one of the dig's cells lies in the 8 cells around its column. This is the plan's "a floor's cells are reached when a shaft that reaches that floor touches them".
3. Pick the smallest squared lattice distance `|P - Column|^2` (int64). Ties go to the lower Id.
- **Digs are derived per floor**: `DigOf[ordinal]`, labelled by a flood fill in ordinal order, plus the sorted list of shafts touching each dig. Cached on (`FloorCellsVersion`, `ShaftsVersion`). Lookups only.
- **"Cheapest" equals "nearest" in D2.** Every shaft that reaches the level gives the same lift leg (|L| floors), so the only difference is the walk from the column to the site.
- The rule does not use the robot's position. That keeps `GetApproachPoint`'s signature, and it makes the choice a pure function of sim state. D3 replaces it with the joined graph (R7).
- **Uses:**
  - the haul drop-off (`:10650`) and the build approach (`:10699-10701`);
  - pickups stay same-level only (`TryClaimHaul` `:10584`), so nothing changes there.
- An island that no shaft touches (a painted shape, which D1's K5 makes) falls back to the nearest reaching shaft. Nothing is ever refused here: in D2, reach decides *which* head, never *whether*. That keeps -deep identical.

## 8. Tests

**New fails-loudly `-shafts` suite** (`Expect` writes an Error), added to `rh_battery.sh`. -deep is left alone, apart from its version lines.
- **SH1. One shaft is D1.** `GetShaftHeadCm() ==` (bitwise) `GetShaft(0).HeadCm`. `ServingShaft` is 0 for every carved cell and for 20 uncarved ones. `GetApproachPoint` equals the old formula with `==`.
- **SH2. Refusals,** one per preflight item: not a Borer; under construction; shaft 0's own Borer; a batch in flight; shaft 0 unbored; over `MaxShafts`; out of coverage (debug path); the same column as another shaft; a column carved on -1; a column carved only on -3; a column in a plan. Each checks the reason text.
- **SH3. Sink.** A second Borer 300 m away. Check `Column == CmToLattice(origin, Borer)`, `HeadCm == LatticeToCm(origin, Column)` exactly, Depth 0, `BorerId`.
- **SH4. Bore.** Bore shaft 1 to -2 (Target `Shaft1`) while shaft 0 is at -1. Then `IsLevelConnected(-2)` holds, `GetShaftDepth() == 2`, and `GetShaftDepth(0) == 1`. Excavate -2 with NAME_None is refused with today's text; with `Shaft1` it is accepted. `Shaft9` is refused as unknown.
- **SH5. Carve.** Both Borers carve 12 cells each on -1. Shaft 1's cells are `Column1` + the literal walk (1,0) (1,1) (0,1) (-1,1) ... The ordinals interleave. No column appears in `Cells`. Each ordinal maps to the same point after a save and a load.
- **SH6. Collision.** Shaft 1 sinks at (3,0) while shaft 0 has carved 4 cells. Shaft 0 then carves 60 cells: (3,0) is skipped, with no duplicates. Shaft 1's spiral skips shaft 0's cells.
- **SH7. Plan ownership.** Paint points near each head. Each Borer takes only its own, in paint order. `PeekCarveCells(L, 5, Out, S)` equals what S then appends. A third shaft sunk later takes over the plan points nearest to it.
- **SH8. Serving.** Two digs: a site in dig B is served by 1, in dig A by 0. An island is served by the nearest shaft. An equidistant point goes to the lower Id. After a painted line joins the digs, the nearest touching shaft serves.
- **SH9. Haulers.** A haul to a Lab module in dig B: the drop-off is head 1 with Z 0, and the cargo lands. A Build in dig A goes to head 0. `FloorCellFallbacks == 0`.
- **SH10. Commute,** with hand-computed integers. A floor-(-1) home with a surface job beside head 1 gives `1 x Per + Surface(head1, P)`. The job beside head 0 uses head 0. A tie goes to the lower Id. The edge load follows `PortalShaft`.
- **SH11. Air link and demolish.** A habitat beside head 1, with -1 rated, is circulated. Shaft 1's Borer cannot be demolished.
- **SH12. Save, load and era.**
  - Two shafts, both queues, a plan, and a batch in flight on each Borer. Save, load and run on: the result equals a control colony that never saved.
  - Hand-made files are refused with "shaft list" and leave the colony untouched: a column carved on -1, a duplicate column, shaft 1 with shaft 0 unbored, the previous version.
  - An era/agent pair with two shafts stays within 5% over 10 sols, and era carves shaft 1's queued cells.
- **SH13. Growth.** `CompleteFloorCells` on -1 with two shafts carves through shaft 0, and the ghost equals the appended cells.

**-scale** (outside the battery): add 4 shafts x 5 floors x 210 cells.
- Cold dig labelling must stay under 1 ms.
- 2,500 `GetApproachPoint` calls and a cold commute memo with 4 heads must fit the existing budgets (`RHSimCommandlet.cpp:12074`, `:12357-12457`).
- The one-shaft numbers must not move.

## 9. Presentation (minimal)

- **The head.** The second Borer is the visible surface head. Each shaft gets its own invisible anchor, cage, doors and fill light (`RHColonyVisualizerSubsystem.cpp:3313-3345`, `:4326`). They are kept in an array by Id, and each is shown only on floors its shaft reaches.
- **The elevator view.** It clamps to `DeepestShaft` (`RHPlayerController.cpp:1995`). A new "next shaft" key or deck button moves the camera to the next head in Id order. The deck's Bore and Excavate actions act on the selected Borer's shaft, else shaft 0 (`RHCommandDeck.cpp:1998-1999`, `:2009`, `:2035`, `:2051`, `:2075`). The Borer card gains "Sink a shaft here", which pre-checks the same preflight.
- **The excavate drag** (`:733`, `:763`) sets `Target` to the shaft that `ServingShaft` picks at the drag centre.
- **The floor view shows both digs** with no change: tiles are drawn at origin + lattice (`:3457`, `:3482`).
- **The pit.** The open set at `:4585-4600` adds every column that reaches the level. The ±30-cell sand skirt is widened to cover every head.
- **Crew.** Wander, boarding and stroll (`RHCrewVisualizerSubsystem.cpp:1248`, `:1357-1388`, `:1534`, `:1565`, `:1929-1960`) use the head that serves the colonist's cell.
- **Audio.** The elevator sound (`RHAudioSubsystem.cpp:231`) uses the head nearest the camera focus.
- **Captures.** One shaft: before and after match (`Saved/RHCapture/d2`). Two shafts: the second head, and floor -1 showing both digs.

## 10. Risks I see

- **R1. One air pool per floor.** Two digs on -1 share one O2 pool, one rating and `MinLivableCells` (`:2371`, `:2347`). A tiny dig B rides on dig A's rating. That fits "the trunk carries the grid down" (`.h:471-474`), but it is a simplification: log it as a deviation. Air per volume belongs to D6/D7.
- **R2. A floor is still one commute anchor and one district.** A home in dig B and a job in dig A on the same floor commute 0 m. Floor residents have no cell (`HomeLevel` only), so D2 cannot do better. This is named for D4/D6.
- **R3. The Borer stands off the column.** Shaft k's head snaps to the lattice, so it can sit up to about 7 m from the Borer's own location. Snapping Borer placement would move shaft 0's head and break inertness, so it is rejected. The head model goes on the column.
- **R4. Wide digs cost adjacency time.** The hash fallback runs every Hope call on any floor with two far digs. -scale measures it.
- **R5. Plan points change owner.** A new shaft takes over plan points that are nearer to it. This is deterministic and tested (SH7), but surprising. Painting is still Debug-only, so no player meets it before Q2a's brushes.
- **R6. `GetShaftHeadCm()` means the origin.** Any new code that reads it as "the shaft" is a bug that compiles. Add a header comment, and in review grep every new use.
- **R7. Approach ignores the robot.** A robot may drive past a nearer serving head. D3 routes on the joined graph.
- **R8. Helper Borers.** A second Borer that is never ordered to sink silently helps shaft 0, as today. The Borer card must say which shaft it works.
- **R9. The version lines** (section 5) are an expected diff. Name them in the gate check. Do not wave them through.

**Director questions:** none. `MaxShafts` 4 is a placeholder row, like every number here.

---

# PROFESSOR REVIEW (2026-10-21)

**Verdict: CLEARED WITH CONDITIONS.** The architecture holds up against the code. It uses one shared lattice per (region, level), keeps the ordinals append-only, refuses a shaft column on a carved or planned cell, puts the shaft block after the floor table, and leaves all per-shaft lookups derived. With one shaft every reachability formula reduces to today's, and nothing found here forces a redesign. The defects are spec gaps. One sim rule is left unstated (item 2), and one piece of new work-loop logic has no test anywhere, because the battery has only one shaft (item 3). Three edge cases are mis-specified (items 4-6). The diff allowance is incomplete (item 7), and a plan deviation has not been recorded for the director (item 1). Write items 1-7 into a REVISION section of this doc. D2 is then cleared to build without another review round.

Anchors are read at 41133b2. `git diff 41133b2 HEAD -- Source` is empty, so HEAD gives the same lines. Lines marked "working tree" come from the uncommitted Gate-D pass.

## What I verified and found sound

- **Inventory (attack 1).** I grepped every caller of `GetShaftHeadCm`, `GetShaftDepth`, `GetBoreTargetDepth`, `GetCarveQueued`, `IsLevelConnected`, `AreLevelsLinked`, `GetApproachPoint`, `ExtendShaft`, `ExcavateFloor`, `PeekCarveCells` and `Debug_PaintCarve`, and every raw read of `ShaftHeadCm` and `ShaftDepth`.
  - The doc does not list some sites, but each one is either an "origin" read, which section 1's blanket rule covers, or needs no change:
    - `RHSimRovers.cpp:90` is surface-to-surface only (`:86`).
    - `RHSimWorldSubsystem.cpp:2449` reads `GetCarveQueued(L)` (the sum is right there).
    - `:3264` and `:3295-3301` are the -scale build.
    - `RHCheats.cpp:1491` and `:1539` call `ExtendShaft`, which is the shaft-0 path.
    - These presentation reads all mean the origin: `RHCommandDeck.cpp:1100`, `RHColonyVisualizerSubsystem.cpp:3145`, `RHPlayerController.cpp:588`/`:821`/`:1125`/`:1201`/`:1972`, `RHDistrictVisualizerSubsystem.cpp:301`, `RHGrowthVisualizerSubsystem.cpp:189`/`:254`/`:270`, and `RHCrewVisualizerSubsystem.cpp:840`/`:859`.
  - The one real gap is semantic, not a missing site: the no-argument `GetBoreTargetDepth()` (item 5).
- **Every existing Bore and Excavate order leaves `Target` as NAME_None.** The issuers are `RHCommandDeck.cpp:1998`, `RHCheats.cpp:348`/`:367`, `RHPlayerController.cpp:763`, `RHPlaytest.cpp:669`/`:682`/`:1182`/`:1188`/`:1328`/`:1424`/`:1429` and `RHSimCommandlet.cpp:90`/`:98`/`:10993`/`:10998`/`:11008`. So "NAME_None = shaft 0, anything else unknown" cannot refuse a battery order.
- **Shared floors (attack 2).** Shared floors are the right call. Keeping digs separate until D3 would re-key every Level-keyed store now, then merge them again at D3's join. The stores are:
  - `FloorO2Kg`, `RatedFloors`, `FloorsNotedSmall`, `FloorRoomCells` and `FloorClimate` (`.h:2230-2231`, `:2244`, `:2376`, `:2622`);
  - the districts' `FloorOwner` (`.h:2041`);
  - the saved colonist `HomeLevel` (`.h:59`);
  - the furniture, seat and planted keys `(Level, ordinal, 0)`;
  - `CarveQueue`, growth `(Level, Index)`, and queued orders.

  R2 is not new behaviour. A same-floor commute is already 0 at any distance (`RHSimCommute.cpp:173-175`). The cross-floor `|Lh - Lw| x Per` also stays honest under the floor-as-one-place fiction: every shaft opens at the surface, so the deeper floor's shaft also reaches the shallower one.
- **O9 (attack 3).** No site assumes a consecutive run of ordinals per dig:
  - `CompleteFloorCells` snapshots `First` and carves inside one synchronous `ExcavateFloor` (`RHSimGrowth.cpp:1203-1217`).
  - `FloorRoomCells` is a prefix (`.cpp:4891`).
  - `FirstCell` is written (`RHSimGrowth.cpp:691`) and saved, and is never read for a position.
  - `CompleteCivicRoom` peeks and carves at the same instant (`:1252`, `:1269`).
  - The presentation's "last cell" reads are random draws (`RHColonyVisualizerSubsystem.cpp:3956`, `RHCrewVisualizerSubsystem.cpp:1262`).
  - Adjacency iterates `i < R` over positions.
  - The work record picks its cell at completion (`.cpp:1354`). A SinkShaft that lands while a carve batch is in flight therefore cannot collide: the completion skips columns.
  - The rule "a cursor advances only over permanent blockers" is sound. Re-deriving a cursor at load gives the same next point as a lagging live cursor, because a column can never sit on a carved point.
- **Inertness (attack 4).** With `Shafts == [shaft 0]`:
  - `DeepestShaft == Shafts[0].Depth == ShaftDepth`, so `IsLevelConnected` and `AreLevelsLinked` (`.h:476`, `:479`) are the same expressions.
  - `IsNetShaftLinked`'s new "Depth >= 1" filter is vacuous. `RatedFloors.Contains(-1)` (`:2921`) needs carved cells (`Debug_ForceRateFloor` `:3165-3166`), and carving needs reach.
  - `RHHabLayout.h:75` holds, because a shaft k needs `Shafts[0].Depth > 0` and depths never fall.
  - The demolish rule is the same set as `:8656`.
  - The one-shaft carve rule reduces to D1's (`:4643-4669`, `:4671-4693`): the spiral runs only on an empty plan, and the walk never yields (0,0).
- **Era (attack 7).** This is safe. SinkShaft is bookkeeping applied at `StepUplink`, which `EraStep` runs (`:709`). Bore and carve batches run in `StepProduction` (`:711`). An unbuilt Borer already refuses era (`:595-601`). Shaft 0 is not refused today either.
- **Save (attack 8).**
  - The block after `WriteFloorTable` (`:11288`) leaves K6's fixed offsets alone (`RHSimCommandlet.cpp:11295`: 20/24/28/32).
  - K6b's splice copies everything after the old floor block (`:11367-11371`), so it carries the shaft block through intact.
  - Every K6 hand-made case fails inside `ReadFloorTable` before the shaft block is reached.
  - Building Ids start at 1 (`.h:2710`), so `BorerId 0 = none` is safe.
  - Load already resets the commute memo (`:12167-12168`).
- **Hauler head choice (attack 6).** Deterministic: an integer squared distance, the Id as tie-break, a flood fill in ordinal order, and lookup-only maps. Every caller runs on the game thread (`RHRobotBrainProcessor.cpp:18`, `RHRobotTaskProcessor.cpp:16`), so the lazy mutable cache is not a race.
- **Battery diff tool.** It compares only `LogRedHopeSim:` lines (`diff_battery.py:8`). The config row count is not printed in any baseline log, so the new `MaxShafts` row cannot diff.

## REQUIRED changes

1. **Record the reach model as a plan deviation for the director, as D1's R0 did, and fix "What you see".**
   - The plan's D2 row says: "A floor's cells are reached when a shaft that reaches that floor touches them" (`going-deep-plan.md:36`). This doc keeps reach floor-wide: placement `:6813`/`:6958`, rating `:2371`, districts `RHSimDistricts.cpp:341` and growth `RHSimGrowth.cpp:676`. "Touch" is used only to pick a head (section 7: "reach decides *which* head, never *whether*"). That is a decision for the director, not a risk note.
   - R1 must state the player-visible consequence. A 2-cell dig B with no circulator is rated through dig A: `IsFloorCirculated` scans the whole floor (`:2347-2360`), and `bBigEnough` counts the whole floor (`:2407`).
   - Extend R1 to the surface as well. With "any head with Depth >= 1", a shaft k bored one floor links the surface habitats around its head through floor -1's rating (`:2917-2935`, `:8756`), even when shaft k has carved nothing on -1.
   - Pin both in -shafts: SH11 already pins the habitat case. Add the dig-B rating case.
   - "dig a separate floor under it" (line 8) is wrong. It is the same floor -1 with a second dig.

2. **The Borer's carve scan must test "S reaches L", not `IsLevelConnected(L)`.**
   - `:1395` gates the carve on `IsLevelConnected`. Once that means "the deepest shaft", a shaft-0 Borer with a queue entry for a level only shaft 1 reaches would carve shaft 0's spiral there. That is a floating dig no shaft touches.
   - The Excavate preflight keeps that from happening live, but nothing at load does.
   - State the test explicitly in section 1 for the work loop and for `ExcavateFloor(L, N, Why, S)` with an explicit S. Add the load refusal "a `CarveQueue` key deeper than its shaft's Depth". No save this build writes can hit it.

3. **Test the new work-loop logic. It is the one sim change the one-shaft battery cannot see.**
   - The in-flight scan becomes per shaft (`:1379-1382` today: any bore anywhere blocks all bores). Helpers stay on shaft 0, and anchors work only their own shaft. SH4 bores shaft 1 *while shaft 0 sits still*, which covers none of this.
   - Add SH4b. Both anchors have a bore in flight at the same time. A helper never starts a shaft-0 bore while shaft 0's anchor has one in flight. A helper never takes shaft 1's carve queue. After SinkShaft, the former helper works only its new shaft.

4. **Edge cases of `ServingShaft` and the commute.**
   - The fast path "always answers 0" (section 5) breaks rule 1. It must answer 0 only when shaft 0 reaches Level, and INDEX_NONE otherwise. The excavate drag (section 9) calls it on unreached levels.
   - `GetCommuteDistanceCm` with no shaft reaching L must fall back to shaft 0's head. Today's formula never checks reach (`RHSimCommute.cpp:169-183`), and the min over an empty set is otherwise undefined.
   - In `GetApproachPoint`, state that INDEX_NONE cannot occur behind the `AreLevelsLinked` guard (`:2338`).

5. **Define every no-argument getter, and classify every caller of each.**
   - The doc defines `GetShaftDepth()` as the deepest, `GetShaftHeadCm()` as shaft 0 and `GetCarveQueued(L)` as the sum. It never defines `GetBoreTargetDepth()`, which is read at `RHCommandDeck.cpp:1486`/`:1999`/`:2009`, `RHCheats.cpp:1613`/`:1627` and the commandlet.
   - Make it shaft 0's, which keeps every one-shaft number.
   - `max(GetShaftDepth(), GetBoreTargetDepth()) + 1` at `RHCommandDeck.cpp:1999`/`:2009` would then mix the deepest shaft with shaft 0. With shaft 1 at -3, one click would order shaft 0 to -4. Those lines must move to `GetShaftDepth(S)` / `GetBoreTargetDepth(S)` in the same change, not later.
   - This is R6's trap in a second getter. Name it there.

6. **`MaxShafts` and the load cap must agree.**
   - Load refuses more than 16 shafts, and `MaxShafts` is a director-editable row. A row set above 16 would make every such save unloadable.
   - Clamp the row to [1, 16] at config read, with a Warning, or derive both from one constant.

7. **Complete the diff allowance.**
   - Once the Gate-D pass lands, the baseline holds a sixth line that prints the save version: `e5 the file says version 42 (expect 42)` (working tree `RHSimCommandlet.cpp:11917`). Section 5 does not list it.
   - The five that section 5 does list are confirmed in `battery_post_d1` (`traffic.log:937-938`, `deep.log:590-591`, `:596`).
   - With D1's O6 counting, the expected delta is 6 lines out and 6 in, recounted against the re-cut baseline.
   - Also record that W1's design still claims "Save v42" (`w1-distant-ice-design.md:562`), which Gate-D has taken. Whichever of W1 and D2 lands first takes v43, and the other re-edits these same lines.

## Optional changes

- **O1. Commute memo key.** The memo is keyed by position (`RHSimCommute.cpp:66-80`, `:100-150`). A head is just one more interned position, so the memo needs no shaft term; only the flows key needs `ShaftsVersion`. Keyed as the doc says, the memo also flushes on every one-shaft bore, where today it flushes only on the first head fix. That raises the cumulative `CommuteDijkstraRuns` printed in X13 (`RHSimCommandlet.cpp:10664-10665`) whenever a bore lands between two commute reads. Today's -traffic suite never does that: its one bore, at `:10089`, also moves the head. So nothing diffs now, but "the commute calls are identical" is not literally true.
- **O2. `CommuteFillEdgeLoad` can read `T.HomePos` / `T.WorkPos` directly.** Those already hold the chosen head (`:261`, `:267`), so no `PortalShaft` field is needed.
- **O3. Refusal 6 (coverage).** It tests the snapped head, which can sit up to 7 m outside coverage while the Borer is inside it. That gives a refusal the player cannot explain. Test the Borer's location, or test both and word the reason for the head.
- **O4. The column under a floor building.** Only station modules need a carved cell (`:6910-6913`). An AirFilter on -1 can therefore stand on a future column, and the trunk would run through it. This is presentation only; refuse it or ignore it, but say which.
- **O5. Two head rules.** The commute takes the cheapest shaft, while robots and crew take `ServingShaft`. The traffic overlay and the crew visual can disagree about which head a colonist uses. Note it for D3.
- **O6. Hand-play.** Give `RH.Bore` and `RH.Excavate` an optional `[shaft]` argument, and add `RH.SinkShaft <BorerId>`, for the consolidated director session.
- **O7. Add SH2b, the execute-time race.** A SinkShaft is in flight while the Borer carves its column, so the order is refused at the uplink step with refusal 8's text.

VERDICT: CLEARED WITH CONDITIONS
1. Record the floor-wide reach model, R1 (including the habitat link through shaft k's head) and R2 as plan deviations for the director. Pin the dig-B rating in -shafts, and correct line 8.
2. The work loop (`:1395`) and `ExcavateFloor` with an explicit S test "S reaches L". Add the load refusal for a queue key deeper than its shaft.
3. Add SH4b: concurrent bores on two shafts, helpers confined to shaft 0, and the former helper confined to its new shaft.
4. The `ServingShaft` fast path answers INDEX_NONE on unreached levels. The commute falls back to shaft 0's head when no shaft reaches L.
5. Define `GetBoreTargetDepth()` as shaft 0's, and move `RHCommandDeck.cpp:1999`/`:2009` to per-shaft getters in this gate.
6. Clamp `MaxShafts` to the load cap of 16.
7. Name the e5 version line, give the delta as 6 lines out and 6 in, and record the W1/D2 v43 ordering.

---

# REVISION (2026-10-21)

This revision answers the PROFESSOR REVIEW above. Where it disagrees with sections 1-10, the revision wins.
**Re-anchored on HEAD 541f650** (the Gate-D pass, committed). The Gate-D pass moved lines in `RHSimWorldSubsystem.cpp/.h`, `RHSimCommandlet.cpp` and `RHCommandDeck.cpp`; the other files did not move. Old anchor -> HEAD:

| Site | 41133b2 | HEAD 541f650 |
|---|---|---|
| Borer bore/carve completion | `.cpp:1340-1358` | same |
| Borer work loop, in-flight scan, carve gate | `:1376-1423`, `:1379`, `:1395` | same |
| `GetApproachPoint` (guard on `:2340`) | `:2335-2345` | `:2337-2347` |
| `IsFloorCirculated`, `StepHabitability`, `bBigEnough` | `:2347`, `:2368`, `:2407` | `:2349`, `:2368`, `:2409` |
| `IsNetShaftLinked`, its inline copy, callers | `:2917-2935`, `:8756`, `:8817` | `:2919-2937`, `:8833`, `:8894` |
| `CarveNextCell`, `PeekCarveCells`, `Debug_PaintCarve` | `:4643`, `:4671`, `:4715` | `:4692`, `:4720`, `:4764` |
| `FloorRoomCells` prefix | `:4891` | `:4940` |
| `ExtendShaft`, `ExcavateFloor` | `:6575`, `:6601` | `:6657`, `:6683` |
| Placement: reach, carved-cell rule, coverage | `:6813`, `:6910`, `:6997` | `:6895`, `:6992`, `:7079` |
| Bore verb, Excavate verb | `:7246`, `:7275` | `:7328`, `:7357` |
| Borer demolish refusal | `:8656` | `:8733` |
| `IsInCoverage` | `:10379` | `:10456` |
| Haul pickup, drop-off, build claim | `:10584`, `:10650`, `:10699` | `:10661`, `:10727`, `:10776` |
| Save: floor table, old shaft slot | `:11288`, `:11359-11360` | `:11365`, `:11436-11437` |
| Load: floor table, old shaft slot, memo reset | `:11717`, `:11848-11851`, — | `:11796`, `:11927-11930`, `:12249` |
| Header: shaft getters, reach, members | `.h:475-489`, `:514`, `:2202-2225` | `.h:485-489`, `:524`, `:527`, `:2227-2250` |
| Deck Bore / Excavate | `RHCommandDeck.cpp:1486-1489`, `:1999`, `:2009`, `:2035`, `:2051`, `:2075` | `:1489-1492`, `:2002`, `:2012`, `:2038`, `:2054`, `:2078` |
| Colony visualizer shaft and pit | `:3313-3345`, `:4326`, `:4585-4600` | `:3314-3346`, `:4327`, `:4586-4601` |

`RHSimCommute.cpp`, `RHSimDistricts.cpp`, `RHPlayerController.cpp`, `RHCrewVisualizerSubsystem.cpp` (the shaft reads) and `RHCheats.cpp` keep the section 3 anchors. In `RHSimGrowth.cpp`, `PlanFloorHomes`'s reach check is now `:682`.

## Item 1. DEVIATION for the director: reach is floor-wide (plain words)

> **What the plan said.** "A floor's cells are reached when a shaft that reaches that floor touches them" (`going-deep-plan.md:36`). That would make each separate dig its own place: its own air, its own rating, its own reach.
>
> **What D2 builds instead.** A floor is still one place, however many digs it has. Once any shaft reaches floor -1, the whole of floor -1 counts as reached: you can build on any carved cell, it has one air supply, one Livable rating, one district and one commute anchor. Which shaft "touches" a dig only decides which shaft head a robot or a colonist uses. It never decides whether the dig can be used.
>
> **What you will notice:**
> - **A dig with no air filter is rated through another dig's filter.** Air filters count anywhere on the floor (`IsFloorCirculated`, `:2349-2362`), and so does the size minimum (`bBigEnough`, `:2409`). So a two-cell dig under the new shaft is Livable the moment the main dig is.
> - **A new shaft links nearby surface habitats to floor -1's air**, even before it has dug anything on -1. The habitat link is "a bay near any shaft head that has reached floor -1, while floor -1 is rated" (`:2919-2937`, `:8833`).
> - **(R2) Commutes between two digs on one floor cost 0 m.** A colonist who lives in one dig and works in the other is "on the same floor", and a same-floor commute is already 0 today (`RHSimCommute.cpp:173-175`). Floor residents have no cell of their own (only `HomeLevel`), so D2 cannot measure that walk.
>
> **The alternative: per-dig reach.** Each 4-connected dig that a shaft touches becomes its own place.
> - **What it fixes.** Air, rating, size minimum, the habitat link and districts would all follow the dig.
> - **What it costs.** Every Level-keyed store moves to (Level, dig): `FloorO2Kg`, `RatedFloors`, `FloorsNotedSmall`, `FloorRoomCells`, `FloorClimate` (`.h:2255-2256`, `:2269`, `:2648`), the districts' `FloorOwner`, the colonist `HomeLevel`, `CarveQueue`, growth targets and queued orders. That means a save re-key, and a merge rule for when D3's tunnels join two digs into one. D1's painted island (-deep K5) would become unreached, so `deep.log` would change. Air per dig needs a new rule for the O2 pool when digs merge. I estimate two to three sessions, with the battery at risk.
>
> **Recommendation.** Keep reach floor-wide in D2. Move per-volume air and reach to D6 (caverns), which reshapes underground districts anyway, and give floor homes a cell there so the commute can see digs (D4/D6). The two effects above are pinned by tests (SH11 and the new SH11b), so changing the rule later is a visible, tested change.

- **"What you see" (line 8) is corrected:** "...bore it, and dig a second, separate dig on floor -1 (or deeper) under it." It is the same floor -1, not a separate floor.
- **SH11b (new).** Dig A on -1 has an AirFilter and 8 cells. Shaft 1 digs a 2-cell dig B on -1 with no filter. Floor -1 is rated, so B is Livable, and a resident can be housed in B. The suite prints this as the deviation it is.
- SH11 also asserts the surface case: shaft 1 bored to -1, with **zero** cells of its own on -1, links a habitat beside head 1.

## Item 2. The work loop and `ExcavateFloor` test "S reaches L"

- Define `ShaftReaches(S, L)` as `L < 0 && Shafts[S].Depth >= -L`.
- **The work loop's carve gate** (`:1395`) becomes `CarveQueue_S[L] > 0 && ShaftReaches(S, L)`, never `IsLevelConnected(L)`. So a shaft-0 Borer never carves on a level that only shaft 1 reaches. With one shaft, `ShaftReaches(0, L) == IsLevelConnected(L)` for every L < 0.
- **`ExcavateFloor(L, N, Why, S)`** (`:6683`):
  - with an explicit S, it refuses unless `ShaftReaches(S, L)`;
  - with `S = INDEX_NONE`, it picks the lowest-Id shaft with `ShaftReaches`, and keeps today's text (`:6690-6692`) when there is none.
- **The Excavate verb** (`:7357-7374`) uses the same test for the shaft named by `Target`.
- **New load refusal**, structural, in the shaft block: "shaft k queues cells on floor L, below its depth". It applies to any `CarveQueue` key with `-L > Depth`. No save written by this build can reach it, because the Excavate preflight refuses such a queue entry and depths never fall.

## Item 3. SH4b: concurrent work on two shafts

The new loop logic (`:1376-1423`) is the one sim change the one-shaft battery cannot see. SH4b sets up two anchored Borers (shaft 0, shaft 1) and one helper Borer, all powered. Then it asserts:
- **Concurrent bores.** Both shafts have a bore target and both anchors start `BoreFloor` in the same step. The in-flight scan (`:1379-1382`) is per shaft, so neither blocks the other. Both depths go up by one at completion.
- **The helper does not double-bore shaft 0.** It never starts a shaft-0 bore while shaft 0's anchor has one in flight. It does take shaft 0's carve queue (today's helper behaviour).
- **The helper stays off shaft 1.** It never takes shaft 1's carve queue, even when shaft 0's queue is empty. In that case it idles.
- **The former helper moves over.** After a SinkShaft on it (once its batch is done), it works only its new shaft 2. Shaft 0's queue then waits for shaft 0's anchor.
- **Work records stay with their shaft.** `PendingBoreWork` always maps to the right shaft, checked across a save and load taken mid-batch.

## Item 4. Edge cases of `ServingShaft` and the commute

- **The fast path follows rule 1.** When `Shafts.Num() == 1`: Level 0 gives 0; L < 0 gives 0 if `ShaftReaches(0, L)`, else INDEX_NONE. The excavate drag (`RHPlayerController.cpp:733`, `:763`) treats INDEX_NONE as "not reached" and does not issue the order.
- **The commute with no reaching shaft.** If no shaft reaches L, `GetCommuteDistanceCm` (`RHSimCommute.cpp:169-183`) uses shaft 0's head. That is today's formula, which never checks reach, so the minimum is never taken over an empty set.
- **`GetApproachPoint` can never get INDEX_NONE.** Its branch is guarded by `AreLevelsLinked(SiteLevel, RobotLevel) && ShaftDepth > 0` (`:2340`). Both levels are therefore reached, so the deepest shaft reaches the deeper one, and the candidate set is never empty. The code still adds `ensure` and falls back to shaft 0.

## Item 5. Every no-argument getter, and every caller

| Getter | Means | Callers, and their verdict |
|---|---|---|
| `GetShaftHeadCm()` `.h:524` | shaft 0's head = the origin | All presentation reads are origin reads (review, "Inventory"). `RHAudioSubsystem.cpp:231`: moves to the head nearest the camera (section 9). `RHHabLayout.h:75`: fine. |
| `GetShaftDepth()` `.h:485` | the deepest shaft | Elevator clamp `RHPlayerController.cpp:1995`, guide `RHCommandDeckGuide.cpp:169`/`:184`/`:317`, playtest, `RHCheats.cpp:1412`, -scale `:3264`: all mean "how deep the colony goes". Fine. Deck `:1489`, `:2002`, `:2012` and cheats `:1613`/`:1627`: **moved to per-shaft** (below). |
| `GetBoreTargetDepth()` `.h:527` | **shaft 0's** target | Fine where it sits alone. Never mixed with `GetShaftDepth()`; see below. |
| `GetCarveQueued(L)` | the sum over shafts | Status lines and `:2451` (`bSealedSmall`: a floor with any queued cell is not "sealed small"). Correct as a sum. |

- **Moved to per-shaft getters in this gate**, not later:
  - `RHCommandDeck.cpp:2002` and `:2012`: `Max(GetShaftDepth(S), GetBoreTargetDepth(S)) + 1`, where S is the selected Borer's shaft, else 0.
  - `:1489`: the status line, for the same S.
  - `RHCheats.cpp:1613-1627`: one status line per shaft. Shaft 0's line keeps today's text.
  - Without these moves, with shaft 1 at -3, one click on the deck would order shaft 0 to -4.
- **R6 is extended.** `GetShaftDepth()` (deepest) and `GetBoreTargetDepth()` (shaft 0) answer about different shafts. Any new code that combines them is a bug that compiles. The header comment for each getter states its shaft, and the build's self-review greps every new use of both.

## Item 6. `MaxShafts` and the load cap agree

- There is one constant, `RH::MaxShaftsCap = 16`, in `RHSimWorldSubsystem.h`.
- The config read, next to `MaxDepth` (`:122`), clamps the `MaxShafts` row to [1, MaxShaftsCap], with a Warning if it had to clamp.
- The load refuses `ShaftCount > MaxShaftsCap`, not the row. A save made under any row value therefore always loads.
- The shipped row is 4. No battery run logs that Warning, so no Error line or baseline line changes.

## Item 7. The save version and the diff allowance

- **Version.** The Gate-D pass took v42 (`.cpp:34` at HEAD). The queued order is: the regolith fix (no bump), then W1 (v43), then **D2 at v44**. If the order changes, D2 takes the next free version, and these lines follow it.
- **W1 note.** `w1-distant-ice-design.md:562` still says "Save v42". W1's builder must make it v43. Whichever of W1 and D2 lands second re-edits the six test lines below.
- **The six version lines**, recounted in `Saved/baselines/battery_post_gd` (the Gate-D baseline, 44 runs). These are the only lines in the whole baseline that print a save version:

| Log:line (post_gd) | Text today (v42) | Test line at HEAD | Text at v44 |
|---|---|---|---|
| `traffic.log:938` | `X15 the file says version 42 (expect 42)` | `RHSimCommandlet.cpp:10806` | `... version 44 (expect 44)` |
| `traffic.log:939` | `X15 a v41 save is refused ... (... version 41; expected 52485331/42))` | `:10813` (and the previous-version patch above it) | `X15 a v43 save ... version 43; expected 52485331/44` |
| `deep.log:590` | `K6 the file says version 42, and straight after the header: ...` | `:11308` | `... version 44, ...` |
| `deep.log:591` | `K6 a hand-made v42 file with a duplicate cell ...` | `:11323` | `K6 a hand-made v44 file ...` |
| `deep.log:596` | `K6 a v41 file is refused ... (... version 41; expected 52485331/42))` | `:11352` | `K6 a v43 file ... version 43; expected 52485331/44` |
| `framing.log:577` | **`e5 the file says version 42 (expect 42)`** | `:11917` | `e5 ... version 44 (expect 44)` |

- **The delta.** With D1's O6 counting, `diff_battery.py` reports **6 lines out and 6 in**, and nothing else.
  - Against a post-W1 baseline, the "out" lines read v43 / v42; against `battery_post_gd` they read v42 / v41. It is 6 / 6 either way.
  - Recount literally against whichever baseline is current when D2 is built.
  - `shafts.log` is new and is not part of the diff.
  - Any other differing line fails the gate.
- **The battery** becomes 45 runs: the baseline plus 44 suites, with `-shafts` added to `rh_battery.sh`'s list and its header count.

## Optional items

- **O1, adopted in a stricter form: the commute keys stay literally as they are for one shaft.**
  - **The memo key.** It keeps `M.Shaft` = shaft 0's head in whole cm (`RHSimCommute.cpp:70-78`), exactly as today, and gains no shaft term. Other heads are only extra interned positions, and the memo is a pure function of position. So it never flushes on a bore, and `CommuteDijkstraRuns` (X13, `RHSimCommandlet.cpp:10590-10592`, `:10675`) cannot move.
  - **The flows key.** `FRHCommuteKey` (`.h:1990-2000`) keeps `Shaft` and gains `uint32 ShaftReach`. It goes up on every SinkShaft, and on a bore completion only while `Shafts.Num() > 1`. With one shaft it never changes, so `CommuteFlowRebuilds` (X13, `:10672-10683`) cannot move either.
  - So "the commute calls are identical" is now literally true. Section 1's `ShaftsVersion` stays for the dig cache only.
- **O2, adopted.** There is no `PortalShaft` field. `DeriveCommuteFlows` writes the chosen head into `Home.Pos` / `Work.Pos` (`:261`, `:267`). `CommuteFillEdgeLoad` reads `T.HomePos` / `T.WorkPos` instead of `Shaft` (`:477-478`). With one shaft those hold exactly `Shaft`.
- **O3, adopted.** Refusal 6 tests the **Borer's** location (`IsInCoverage(Borer.LocationCm, 0)`, `:10456`), with the reason "the Borer is outside coverage". The snapped head is at most 707 cm away and is never tested on its own.
- **O4, adopted as two refusals, both inert for shaft 0.**
  - **SinkShaft refusal 9.** "A building stands on that column on floor L": any underground building, built or under construction, whose `CmToLattice(origin, LocationCm)` is the column. Only station modules need a carved cell (`:6992`), so an AirFilter could otherwise stand on a future column.
  - **A placement refusal.** "That is a shaft column", for a floor building on the column of any shaft **k > 0**.
  - Shaft 0's column keeps today's behaviour. Tests do place floor buildings at the head, so refusing (0,0) would move battery lines. Logged as a known gap.
- **O5, noted for D3.** The commute takes the cheapest shaft per trip, while robots and crew take `ServingShaft`. The traffic overlay and the crew visuals can therefore show different heads for the same person. D3's joined graph replaces both with one route.
- **O6, adopted** (presentation and cheats only; not in the battery). `RH.Bore <ToDepth> [shaft]`, `RH.Excavate <Level> <Cells> [shaft]`, `RH.SinkShaft <BorerId>`, and the per-shaft `RH.Status` lines. They go on the consolidated hand-play card.
- **O7, adopted: SH2b.** A SinkShaft is queued on Borer B at column C. Before the order executes, shaft 0's helper carves C (the order is placed so that C is the next spiral cell). At the uplink step the order is refused with refusal 8's text, and no shaft is added.

**Not taken:** none.

## Updated bar

- The save is **v44**.
- The new shaft block keeps the section 5 layout and gains: the `MaxShaftsCap` count check (item 6), the queue-depth refusal (item 2), and the Warning repairs as written.
- **The battery:** 45 runs, 0 not green. It is line-identical to the current baseline except the 6 out / 6 in version lines named in item 7.
- **`-shafts`:** SH1-SH13 plus SH2b, SH4b and SH11b pass, with `FloorCellFallbacks == 0`.
- **-scale:** the one-shaft numbers are unchanged, and the 4-shaft case fits its budgets.
- **The 110-sol playtest** is byte-identical.
- **Logged in `docs/design-decisions.md`:** the item 1 deviation, the era teleport deviation (section 6), and O4's shaft-0 column gap.
