# P2 follow-ups (display only): build brief (2026-11-03)

Written for the one builder of `p2-wf.js` at HEAD `6df7fcc` (tree clean; `RHClickTest.cpp` sha256 `34b690e2...`, the idle gate's round-2 final). Every anchor below gives its symbol and its line at `6df7fcc`. The items edit the same files and shift each other's lines, so **re-find every anchor by symbol (`git grep -P`) before each edit; never trust a line number after the first hunk lands.**

The designs are `/Volumes/Unreal/red_hope/wip-backups/p2/design/{clicktest,walks,cache,meshes,sites-market,deferred}.md` ("clicktest.md" etc.). Two read-only verifiers (correctness, regression) checked each one; none rejected an item. This brief applies every amendment they made (Appendix A maps each one to where it lands). "Verbatim" below means the design's code exactly as written; where this brief gives code, this brief's code wins.

**Authority, highest first:** `docs/qa/2026-10-26/preflight/ORCHESTRATOR-RULINGS.md` (ruling 28 above all; rulings 7, 18, 22-27 for how the gate runs) and the director's answers in 1.2 > this brief > the designs' exact code as amended here > the rest of the designs.

**GO (13):** C1, C2, C3, C4 (clicktest); W1, W2, W3a, W3b (walks); K1 (cache); M1, M2 (meshes); S1, S2 (sites-market).
**DEFERRED (4):** M1b, M1c, X1, X2 (section 10).
**Main scene: 115 steps, unchanged. Deepbench: 9, unchanged. No battery line moves. Blockers: none.**

| File | Items |
|---|---|
| `Source/RedHope/Public/RHAgentVisualizerSubsystem.h` | K1, W1, W2, W3a, W3b; M1 (a comment only) |
| `Source/RedHope/Private/RHAgentVisualizerSubsystem.cpp` | K1, W1, W2, W3a, W3b, M1 |
| `Source/RedHope/Private/RHClickTest.cpp` | C1, C2, C3, C4, W2, W3a, W3b |
| `Source/RedHope/Public/RHColonyVisualizerSubsystem.h` | S1 |
| `Source/RedHope/Private/RHColonyVisualizerSubsystem.cpp` | S1, M2 |
| `Source/RedHope/Private/RHCheats.cpp` | S1 (`RH.SitePose`, the `RH.Sites` help), S2 |
| `docs/design-decisions.md` | the gate's entry (3.7) |

Nothing else in the repo changes. The gate's tools live outside the repo, under `WORK` = `/Volumes/Unreal/red_hope/wip-backups/p2/work/`.

---

## 1. Authoritative decisions

### 1.1 Rules for every item
1. **Display only.** No file under `Source/RedHopeSim`, `docs/data`, `Content/` (no new or changed asset of any kind) or `Config/` changes. The new code reads the sim through its existing public, const accessors and writes nothing to the sim, a task fragment or a save.
2. **No player-facing string.** All new text is developer-facing: `UE_LOG` lines, the `RH.IdleRobots`, `RH.Sites`, `RH.SitePose` and `RH.Market` cheats, and click-test lines. `gate-d-wording.md` gains no block; the docs entry says why.
3. **No random draw** (`FMath::Rand*`, `FRandomStream`). Every choice is keyed on indices, Ids and sim-paced time.
4. **No per-frame route planning or allocation.** A route is planned only on a goal change (FreeWalkTo's unchanged 100 cm rule), a kept-off flag change, or a keep-out change that makes the rest of a walk pass inside a keep-out. The idle pad cache is rebuilt only on a key change.
5. **`PlanRoute`, `SegmentBlocked`, `RebuildObstacles`, `AdvanceRover`, `Tick`'s `bDrive` and `bDeepDraw` branches, `ParkAfterIdleS`, `FreeWalkCmS`, `InspectWaitS`, `RobotClearanceCm`, `IdleRingOutCm` and `IdleDockClearCm` are unchanged.** The Lander rank's grid (its origin, spacing and order) is unchanged. Its one change is W1's 3.9: `ParkSlotCm` also skips a rank slot that lies inside a pad's keep-out, by the same skip it already applies to a slot inside a footprint's box. Ruling 28 needs it: a leg from a ring slot to such a rank slot would end inside the keep-out. It moves no slot on `idle_day` or in the click scene.
6. **The docked robot's drawing on the pad is not moved.** M1b and M1c are not built. M1 moves only the cargo lump.

### 1.2 Ruling 28 and the director's answers
- **Ruling 28, walking figures keep off a charging pad (2026-11-03).**
  - *Who:* every drawn surface figure walking a leg that starts or ends at a pad's ring slot or one of its inspection stand points, plus a job walk that starts from a ring slot.
  - *The keep-out:* `KeepOutCm` = the pad's drawn radius + 100, never under `PadHalf + RobotClearanceCm + 60`. That is **max(200 + 100, 200 + 90 + 60) = 350 cm** for the 2x2 pad, the only Charging Pad row (`ChargePad,2,2`, not drag-sized). Figures also keep **150 cm** from a robot drawn docked at the pad.
  - *Exempt:* the robot docking at the pad or leaving its mast, and a figure whose job site is the pad.
  - *`PlanRoute` itself is unchanged.*
  - *Proof:* the click floor rises to `KeepOutCm` for idle legs (C4), and a mutation that removes the keep-out fails it (`p2_mut_w1`). Job walks from a ring slot and the 150 cm rule are shown by the captures and judged by the presentation review (6.4).
  - The ring stays at 540 / 760 cm, so the keep-out moves no slot.
- **The idle robots' DIRECTOR ANSWER (2026-10-28, Q-A (a)): "a loose ring well clear of the pad".** Kept: no slot moves. The routed outline passes 385-447 cm from the pad's centre, at least 92.8 cm inside ring 1.
- **The Solar bar's DIRECTOR ANSWER (a) (2026-10-27, `48c79db`; `docs/qa/2026-10-25/visual-pass-design.md`): "a per-model cut hold in `RefreshSites` that keeps the cut under the panel until the hinge is built".** S1 builds that hold for `SolarPanel` (held at 45 % through 50-65 %, released at 70 %). It adds one more row with the same mechanism, `BatteryBank` (the 10 % step drawn at the 5 % cut). The sweep the P2 task asked for found the same fault there. The row goes beyond the Solar-only wording, so the docs entry lists it as a deviation the director can drop (one table row and frames 09-11).

### 1.3 Names and numbers every item uses (resolved across items)

| Name | Value / meaning | Owner | Read by |
|---|---|---|---|
| `FIdleDebug::KeepOutCm` | 350 cm for the 2x2 pad; filled in `GetIdleDebug`'s pad block from `FIdlePad::KeepOutCm` | W1 | C4 (must equal the test's own floor within 1 cm) |
| `FIdleDebug::DrawnRadiusCm`, `GetPadKeepOutCm`, `GetPadDockedDrawnCm` | 200 cm; developer accessors | W1 | `RH.IdleRobots` |
| the drawn radius | the larger horizontal half-extent of the ChargePad model asset's bounds, fitted as the colony visualizer fits it: 198 x 400/396 = **200.0 cm**. Read by `LoadObject` of `/Game/RedHope/Art/Models2/ChargePad/ChargePad/StaticMeshes/ChargePad.ChargePad`, once per session (`PadModelHalfCm`). With no model: the footprint's larger half-axis (200 cm). **Never from a spawned actor or component, never from a cvar.** | W1 | K1's key contract; C4 measures the **spawned actor** (`GetBuildingVisual`), an independent derivation. On today's data the agreement is also decided by the `PadHalf + 150` term, which the test computes from the sim's footprint. |
| `KeepRouteMarginCm` 25, `DockKeepCm` 150, `DockDriftCm` 25 | routed at 375 cm round a pad and 200 cm round a docked robot; walked outline never under **373.6 cm** | W1 | the C4 floor (349 with tolerance) |
| `IdleReachCm` 5000, `IdleLeashCm` 7500 | a pad's centre, or the rank slot, only within 5000 cm of the sim position; an outward step past 7500 cm is held | W2 | the click OFF goal check, `cap_far` |
| `InspectSpreadCm` 150, `IdleApartCm` 100 | stand points 150 cm apart along the clearance box; no two inspectors' points, and no two pads' open ring slots, under 100 cm apart | W3a, W3b | the W3 pair check (100 cm), the W3b slot check |
| `IdlePadKeyEntry`, `IdlePadsKey` (`TArray<uint64>`), `bIdlePadsBuilt`, `NextIdleRetryS`, `bIdleRetryDue`, `FIdleChoiceRuns` | the cache key: per surface building, its Id, a site bit and a pad-off bit; compared in place every tick | K1 | `cache_check.py`; W1/W3 data are functions of it |
| `KeepOutSerial` | moves only when the keep-out boxes change (a pad keep-out at a cache rebuild, or a docking change in `GatherDocked`); never reset | W1 | `FreeWalkTo`, the off-road trip, W3a's dock abort |
| the click floor | `IdleFloorCm = max(IdleDrawnHalfCm + 100, IdleFloorMinCm)`, with `IdleFloorMinCm = PadHalf + 90 + 60 = 350`. Read lazily at the first figure drawn at the pad. A figure fails at `< floor - 1.0`. | C4 | C1's on-pad region (`Dist < floor`), C2's round-leg test (`LegLineCm < floor`) |
| `EIdleShow` | `NotWalker = 0, Busy, AtPad, LanderRank, Inspecting, StayPut` (`StayPut` = 5, appended) | W2 | the click texts print `(int32)D.Show` |

### 1.4 Conflicts settled (each choice, and why)
1. **The drawn radius's source.** The cache and clicktest vets read an older `walks.md` that took it from `GetBuildingVisual`. The final `walks.md` 3.3 takes it from the asset and the fit, which meets K1's contract (`cache.md` 9) and keeps C4's actor measurement independent. Built as `walks.md` 3.3.
   - The W1 regression vet's optional amendment (c), compute `FitCm / 2` with no `LoadObject`, is **not taken**. Ruling 28 says "from the bounds of the mesh the visualizer draws". The read happens once per session, and its only cost is a one-time synchronous load under `rh.ModelSetV2 0`, which no gate run uses.
   - The docs entry still records the vet's finding: the fit makes the asset's extent cancel, so the drawn radius equals `FitCm / 2` for any model.
2. **Docked-disc churn (W1 regression (a)).** Of the two mitigations offered, take **the pad's centre** for a robot drawn by `AdvanceRover`: a rover, or a walker whose `RoverVis[i].Drive.bTrip` is set.
   - It is exact: `AdvanceRover` drives that robot "the rest of the way in" to the pad's centre (`:876-879`), and a parked rover sits where its path ended.
   - Its disc (routed at 200 cm) then lies wholly inside the pad's routed keep-out (373.6 cm), so it never shapes a route, and the drive-in moves no serial.
   - A walker that walked in keeps `walks.md`'s drawn-figure disc, which settles within about a second.
   - The settle rule (wait for under 5 cm of movement) is not taken: it is more state for the same result. A counter of serial moves on a docking change goes in the `RH.IdleRobots` summary (3.2).
3. **The W2 leash.** The regression vet found a freeze: a figure that starts a spell between 7500 and 8000 cm out would be held forever. The leash therefore holds only a step that ends beyond 7500 cm **and** further out than the figure is drawn. An inspection walk (`IdlePhase` 1) held there ends (phase 3, `bFree = false`) and is counted.
4. **C3's wait must not depend on earlier clauses** (both C3 vets, and the C2 vets' cross-finding). Drop `&& T->IdleWhy.IsEmpty()` from `robots.idle before off`. A robot out on an inspection round is always waited for, at 3x, within the 10 s bound.
5. **C2's harness-made long poll** (C2 regression). The `robots.idle wait` step's `GapAfter` goes from `0.2f` to `0.f`: `}, 0.2f, 50.f);` becomes `}, 0.f, 50.f);` at `:3925`. `FRunner` still waits `MinGapFrames` = 2, so the hand-off into the watch can no longer put one shared long poll on both round legs. The alternative (exempt the boundary segment) is not taken: it is more code, and nothing reads that gap.
6. **`p2_mut_c2` placement.** The design's hunk returned from the top of `IdleTrackAll`. That would also starve W3a's pair loop and could add a second clause ("stand points not exercised"). The hunk goes instead as the **first statement of `IdleTrackAll`'s per-figure loop** (`if (GFrameCounter % 30 != 0) { continue; }`), so the pair loop still polls every frame and the mutation fails only "floor not exercised".
7. **`p2_mut_c1` under W1** (both C1 vets). The staged straight path must also set `bFreeKeptOff = true` and `KeepOffSerial = KeepOutSerial`, or W1's `FreeWalkTo` re-plans it round the pad. This brief also refreshes `KeepOffSerial` on every frame for robot 5 (5.2). Nothing re-checks a staged straight path, even if a keep-out changes mid-step.
8. **`p2_mut_c3` placement** (both C3 vets). The hunk goes inside the second `if (T.IdlePhase == 0) { ... }` block (the one that sets `AtPad`), after its `if (T.bAtSlot) { ... }` and before its closing brace. Placed between `:1147` and `:1148`, it would split the `if / else if` chain and not compile.
9. **C3's `IdleBeforeOff`** is written and never read: drop the field. The PASS and FAIL wording says "drawn at or on its way to (its) slot", because a phase-3 figure walking back counts as home.
10. **The watch's exit and verdict take both C2 and W3a.** The exit test becomes `!(T->IdleWatchSaid.Len() > 0 && T->IdleRoundLegs > 0 && !bRoundOpen && T->IdlePairsSeen > 0)`, where `IdleRoundLegs` counts only clean round legs (C2). The verdict is clicktest 6.6, then W3a's pair clause (`walks.md` 9.3).
11. **The FTest block** is clicktest 6.1 (it replaces `:909-945`, **not** `:909-946`; `:946` is the struct's closing `};`) minus `IdleBeforeOff`, plus `walks.md` 9.1's fields before that `};`.
12. **Setup's hunk order:** clicktest 6.3's floor lines replace `:3713-3714`, then `walks.md` 9.2's far-pad block, then `Sim->SetFleetHold(true);` (`:3715`) as today.
13. **`RHColonyVisualizerSubsystem.cpp`** is edited by S1 (`:2757-3414` plus the new `Debug_PoseSite`) and M2 (`:3463-3473`, `:5060-5153`). The two never overlap, but whichever lands second finds the other's lines shifted: anchor both by symbol. After both land, re-grep that M2's namespace block (`ShipPadCm`/`SupplyShipPath`), the `if (!Rover)` block and the placement are intact. Then build once with both.
14. **Coupled items, built as one unit:** C4 needs W1 (`KeepOutCm` and the routing) **and** C1 (the `Dist < floor` region: a robot leaving the mast on the pad's axis stands 300 cm out, outside the 290 cm box). W3b needs W1's `SlotNearSlotOf.Init(0, 20)`. W3a needs W1's `InPadKeepOut` and `InDockDisc`. If any of these cannot be built as specified, **stop** (`stop_reason`). Do not ship a subset or fall back to the 290 cm floor.
15. **The job walk in the click step** (C4 correctness (4)). The fleet is held from setup to the report, so no job claim can start in the step's held-fleet phases; only a self-issued charge, which is exempt, can begin. An added phase could not steer the sim's claim so that the walk passes the pad. Ruling 28's captures carry job walks. The step's header comment says this. It does not say the step "cannot observe one without changing other parts".

---

## 2. Anchors at HEAD `6df7fcc` (by symbol; line at HEAD)

**`Source/RedHope/Private/RHAgentVisualizerSubsystem.cpp`** (all checked at HEAD)

| Symbol | Line(s) | Items |
|---|---|---|
| anonymous namespace: `RobotClipPath` / close | `:34-38` / `:39` | M1 (constants) |
| `CVarRobotRouteLog` help | `:111` | K1 |
| constants `RobotClearanceCm` `:116`, `IdleRingOutCm` `:122`, `IdleDockClearCm` `:128`, `ChargePadName` `:129`, `LanderName` `:130` | `:116-130` | K1 and W (insert after `:130`; the two blocks are independent) |
| second anonymous namespace, after `SegmentHitsBox`, before its `}` | `:195` | W (geometry helpers) |
| `PlanRoute` (unchanged) | `:232-333` | W1 adds `PlanKeptOff` and `KeptOffClear` after it |
| `ParkSlotCm` (`Obstacles` loop `:364-371`, `if (bFree)` `:372`) | `:335-378` | W1 3.9 |
| `RefreshIdlePads`: key `:390-407`, timer and serial `:408-416`, `Lander` `:417`, pad loop `:418`, `RingCm` `:430`, slot loop `:441-450`, stand points `:466-495` (`if (bClear)` body `:485-490`), pad loop close `:496`, function close `:497` | `:380-497` | K1 (`:380-416`), W1 (after `:417`, `:430`, `:441-450`, `:476-485`), W3a (`:485-490`, `:495`), W3b (after `:496`) |
| `FIdlePad::NearDock` | `:499-513` | W1 adds functions after it |
| `GatherDocked` | `:515-544` | W1 (new body) |
| `ResetIdle` | `:546-563` | W2 / W3a (`bStayPut`, `InspectStand`) |
| `ChooseIdlePad`: early-out `:573-577`, `ParkSlotCm` `:578`, `FindNearestChargePad` block `:580-586`, keep path `:588-597` ("building gone" `:590-595`), comment `:602-604`, `OldSlot` `:606`, `bAtSlot = false` `:611`, no-pad return `:614-621`, end `:640-646` | `:565-647` | K1 (`:573-577`, `:602-604`), W2 (after `:586`, after `:606`, `:614-621`, `:640-646`), W3a (`:590-595`) |
| `GetIdleDebug`: `RankCm` `:658`, `Show` `:663`, pad block `:670-688` (`RingCm` `:681`, `InspectId` `:683`) | `:649-688` | W1, W2, W3a; `GetIdlePadSlot` after `:688` |
| `LogIdleRobots`: header `:700`, pad lines `:715-719`, walker loop `:721`, rank line `:752`, inspecting `:756-757`, `default:` `:759`, summary `:765-766`, close `:767` | `:690-767` | K1 (`:700`), W (pad, summary, figures), M1 (after `:766`) |
| `AdvanceRover` (unchanged; "rest of the way in" `:876-879`) | `:774-923` | read only |
| `AdvanceDrawn`: snap `:932-940`, `Pace` `:942`, job arrival `:953-958`, `FreeWalkTo` `:959-971`, off-road trip `:1037-1053`, walk-over `:1072`, idle branch `:1076-1170` (`Pad`/`!Pad` `:1090-1095`, `SimDt` `:1099`, pick `:1125-1134`, phase-0 `AtPad` block `:1137-1147`, phase 3 `:1148-1153`, `FreeWalkTo` `:1169`) | `:925-1174` | W1, W2, W3a; mutations C1, C3 |
| `ResetTracking` (cache lines `:1191-1194`; walkers then lumps destroyed `:1200-1214`) | `:1187-1234` | K1, W1 |
| `TrackEntities`: `Skel` `:1314`, `FeetLiftCm.Add` `:1325`, lump `:1326-1333` | `:1236-1342` | M1 |
| `Tick`: `RebuildObstacles` then `RefreshIdlePads` `:1638-1646`; drive branch `:1730-1745`; lump `if (bShow)` `:1910-1920`; route line `:2038-2042`; log-off reset `:2051-2054` | `:1616-` | K1, M1 |

**`Source/RedHope/Public/RHAgentVisualizerSubsystem.h`**: `EIdleShow` `:70-73`; `FIdleDebug` `:74-93`; `GetIdleDebug` / `LogIdleRobots` `:94-96`; lump comment `:122-125`; `FRoverDrive::bTrip` `:143`; `RoverVis` `:163`; `IsRoverIndex` `:164`; `FRobotTrip` `:191-241` (`PadChoiceSerial` `:223`, `bInspecting` `:240`); cache comment `:244-246`; `FIdlePad` `:247-262` (`DockedCm` `:260`); `IdlePadsKey` / `IdlePadsSerial` / `NextIdlePadsS` `:264-266`; `ResetIdle` `:270`; `RouteLogTickMs` `:274`.

**`Source/RedHope/Private/RHClickTest.cpp`**: `FRunner` `:471-540` (`Push(Name, Run, GapAfter = 0.2f, Timeout)`; `MinGapFrames` 2, `MinTimeoutFrames` 30); `PushPress` `:761-785`; FTest idle block `:909-945` (comments `:909-912`, `:923-927`; `TSet IdleOnPad` `:940`; `IdleWhy, IdleSaid, IdleWatchSaid` `:945`; struct close `:946`); header comment `:3617-3632`; `robots.idle setup` `:3633-3718` (floor `:3713-3714`, `SetFleetHold(true)` `:3715`); `IdleTrackAll` comment `:3719-3722`, lambda `:3723-3824` (`:3750` Remove, `:3758` Contains, `:3762-3765` Add, `:3776-3789` segment, `:3802-3817` leg end); `IdlePaced` `:3826-3832`; `robots.idle wait` `:3833-3925` (pick comment `:3846-3847`, timeout line `:3863-3864`, stand-still `IdleSaid` `:3906-3908`, ring check `:3909-3912`, `}, 0.2f, 50.f);` `:3925`); `robots.idle watch` `:3926-4007` (`bRoundOpen` `:3978-3982`, exit `:3983`, verdict `:3993-4005`); `robots.idle select` `:4008-4015`; goal-check block `:4016-4067` (`GoalCheck` `:4018-4062`, presses `:4063-4066`); report `robots.idle` `:4068-4084` (`SetFleetHold(false)` `:4072`, `Report` `:4083`); `final` `:4087-4111`.

**`Source/RedHope/Private/RHColonyVisualizerSubsystem.cpp`**: `namespace RHSiteKit` `:2757-2782` (`Steps = 20` `:2776`); `AddSite` `:2843` (`using namespace RHSiteKit` `:2845`, hidden mask by mesh name `:2914`, `Site.RevealMesh = Mesh;` `:2957`); `RefreshSites` `:2983` (`bWaiting` `:3034`, `Fraction` `:3045`, `const int32 Step` `:3046`, `BuiltCm` `:3060`, `CutZ` `:3073`, `BuildCutZ` write `:3078`); `GetSiteRevealFraction` `:3375`; `GetSiteRevealCut` `:3381`; `LogSites` `:3397-3414` (Stage `:3405-3406`, reveal `:3411`); the namespace with `ShipPadCm` / `SupplyShipPath` `:3463-3473`; `RefreshSovereigntyVisuals` `:4984` (`World` `:4986`, `using namespace RHCanon` `:4993`, comment `:5060-5061`, spawn block `if (!Rover)` `:5083-5129`, `FVector Loc = FMath::Lerp(From, To, Alpha);` `:5143`, placement through `:5151`, `SetActorHiddenInGame(!bSurface)` `:5152`); `SpawnBox` `:5156`; ChargePad fit (read only) `:2447`, `:2499-2526`.

**`Source/RedHope/Public/RHColonyVisualizerSubsystem.h`**: `GetSiteRevealFraction` comment `:100-101`; `LogSites` comment and declaration `:103-104`; `bPassesDirty` / `MarkPassesDirty` `:142` / `:146`; `FRHSiteVis` `:318-341` (`RevealHeightCm` `:333`).

**`Source/RedHope/Private/RHCheats.cpp`**: `GRHMarket` `:1481`; `inject-region` `:1535-1536` (unchanged); history bound `:1585`; header `IsMarketLive` `:1624-1626`; E7 comment `:1627-1628`; loop `:1629`; region line `:1632`; `GRHTramPose` `:2548` (precedent); `GRHSites` `:2581-2591` (help `:2583`).

**Read-only sim facts** (no edit): `FindNearestChargePad` `RHSimWorldSubsystem.cpp:13393-13432` (no distance, power or coverage test); `SetFleetHold` `:2821-2830`; claim gates `:13046`, `:13073`, `:13268`, `:13513`, `:13622` and `RHSimRovers.cpp:177`; the charge is not held (`RHRobotTaskProcessor.cpp:74-116`); `ArriveDistCm` 300 (`RHRobotMovement.h:13`); `Debug_PlaceInstant` -> `AddBuilding(bInstant)` `:12799-12807`; `GetFootprintHalfCm` `:8831-8835`; `GetMarketRegionCount` `RHSimWorldSubsystem.h:1907`; `GetMarketRegionIdAt` `RHSimMarket.cpp:534-538` (syncs).

---

## 3. Exact changes, per file and item

### 3.0 Build order
1. K1, verbatim from `cache.md` 4.1-4.7, with one edit to the header comment (3.1).
2. W1, then W2, W3a and W3b, from `walks.md` sections 2-8 as amended in 3.2.
3. M1 (`meshes.md` 2.3 (a)-(e)).
4. S1 and S2 (`sites-market.md` S1.4.1-S1.4.7 and S2.3, as amended), then M2 (`meshes.md` 5.3, as amended), anchored by symbol (1.4 (13)).
5. The click test (3.3).

Build after each group (visualizer-only builds are a minute or two) and fix compile errors before going on. `-Wshadow` must stay clean: the designs list their new names, and every one was checked against HEAD.

### 3.1 `RHAgentVisualizerSubsystem.h`
- **K1** (`cache.md` 4.7), verbatim, with one sentence added to the new `:244-246` comment after "constants": "and the Charging Pad model asset's bounds, read once with LoadObject (P2 W1's drawn radius)".
  - `:264-266` become K1's members; `NextIdlePadsS` is removed.
  - After `:274`: `RouteLogChoicesAt`, `RouteLogSerialAt`.
- **W1-W3** (`walks.md` 2.3), verbatim, with one change to the private counters line:
  ```cpp
  	int32 KeptOffPlans = 0, KeptOffExits = 0, KeptOffFallbacks = 0, IdleLeashFrames = 0, IdleLeashInspectEnds = 0, DockSerialBumps = 0;
  ```
  - `EIdleShow` gains `StayPut` last.
  - `FIdleDebug` gains `SimCm`, `KeepOutCm`, `DrawnRadiusCm` and `InspectStand`.
  - The public accessors `GetPadKeepOutCm`, `GetPadDockedDrawnCm` and `GetIdlePadSlot` go after `LogIdleRobots`.
  - `FRobotTrip` gains `bStayPut`, `bKeepOff`, `bFreeKeptOff`, `KeepOffSerial`, `TripDoneCm` and `InspectStand`.
  - `FIdlePad` gains `KeepOutCm`, `DrawnRadiusCm`, `SlotKeptOutBy`, `SlotNearSlotOf`, `StandCm` and `StandStart`.
  - The private section gains `FIdleKeepOut`, `FDockDisc`, `KeepOuts`, `DockDiscs`, `DockNow`, `KeepOutBoxes`, `KeepOutSerial`, `PadModelHalfCm`, the counters and the private methods (including `FreeStand`).
- **M1** (`meshes.md` 2.3 (d)): the `:122-125` comment.

### 3.2 `RHAgentVisualizerSubsystem.cpp`

**K1, the cache key** (`cache.md` 4.1-4.6, verbatim):
- `IdlePadKeyEntry` goes after `:130`. `RefreshIdlePads` `:380-416` is replaced; the body from `:417` stays.
- `ChooseIdlePad`'s early-out `:573-577` (`bChosen` / `bMakeWay` / `bRetry` and the counters) and its comment `:602-604` are replaced.
- The `RH.IdleRobots` header `:700`.
- `ResetTracking` `:1191-1194`. `IdlePadsKey` is **not** reset there; that would make M3 unprovable.
- The route line `:2038-2042`, the log-off baselines `:2051-2054` and the cvar help `:111`.

**W1, ruling 28** (`walks.md` 2.1, 2.2, 3.3-3.9, verbatim except where marked):
- Constants (2.1) after `:130`; geometry helpers (2.2) before `:195`.
- `RefreshIdlePads` (3.3): (a) the keep-out list after `:417`; (b) `P.KeepOutCm`, `SlotKeptOutBy.Init(0, 20)` and `SlotNearSlotOf.Init(0, 20)` after `:430`; (c) the slot test; (d) the stand-point test.
- New functions (3.4) after `NearDock`: `RebuildKeepOutBoxes`, `InPadKeepOut`, `InDockDisc`, `AnyBoxBlocks`, `GetPadKeepOutCm`, `GetPadDockedDrawnCm`.
- **`GatherDocked` (3.5) with amendment (1.4 (2)).** In the design's new body, replace the block from `const int32 DockPadId = Task->DigDepositId;` through the `DockNow` add with:
  ```cpp
  			// P2 (ruling 28): a robot docked at any finished surface pad (on or off) is a disc every idle walk keeps DockKeepCm from, at its DRAWN
  			// position on the last tick. A rover, and a walker on a road trip (RoverVis[i].Drive.bTrip: AdvanceRover draws it), is driven the rest
  			// of the way in to the pad's centre (AdvanceRover), so its disc stands at that centre from the start: inside the pad's own routed
  			// keep-out it never shapes a route, and the drive-in moves no KeepOutSerial. A walker that walked in is at its drawn figure (it settles
  			// within about a second of docking); else its sim position.
  			const int32 DockPadId = Task->DigDepositId;
  			if (const FIdleKeepOut* DockKO = KeepOuts.FindByPredicate([DockPadId](const FIdleKeepOut& KO) { return KO.PadId == DockPadId; }))
  			{
  				FVector2D DockDrawn(DockSimCm.X, DockSimCm.Y);
  				if (IsRoverIndex(i) || (RoverVis.IsValidIndex(i) && RoverVis[i].Drive.bTrip))
  				{
  					DockDrawn = DockKO->CentreCm;
  				}
  				else if (Trips.IsValidIndex(i) && Trips[i].bInit)
  				{
  					DockDrawn = FVector2D(Trips[i].DrawCm.X, Trips[i].DrawCm.Y);
  				}
  				FDockDisc& Disc = DockNow.AddDefaulted_GetRef();
  				Disc.PadId = DockPadId;
  				Disc.Index = i;
  				Disc.DrawnCm = DockDrawn;
  			}
  ```
  In the design's `if (!bSameDocks)` block, add `++DockSerialBumps;` beside `++KeepOutSerial;`.
- `PlanKeptOff` (3.6) and `KeptOffClear` (3.7) after `PlanRoute`.
- `AdvanceDrawn` (3.8): (a) the job's arrival and `bKeepOff`; (b) `FreeWalkTo(Goal, bKeptOff)`; (c) the off-road trip with `TripDoneCm`; (d) `:1072` becomes `FreeWalkTo(SimPos, T.bKeepOff)`.
- `ParkSlotCm` keep-out skip (3.9).
- `GetIdleDebug` additions (`walks.md` 7): `SimCm` after `:658`; `KeepOutCm` and `DrawnRadiusCm` after `:681`; `InspectStand` after `:683`. `GetIdlePadSlot` after `GetIdleDebug`.

**W2, the reach bound and the leash** (`walks.md` 4.2-4.4, verbatim except the leash):
- `ChooseIdlePad`: the `IdleReachCm` pad check after `:586`; `bWasStayPut` after `:606`; the no-pad return `:614-621`; the end `:640-646`.
- `ResetIdle` gains `T.bStayPut = false;` and `T.InspectStand = INDEX_NONE;`.
- The idle branch `:1090-1095`: `bStand`, `StayPut` and `LanderRank`.
- **`:1169` with amendment (1.4 (3)):**
  ```cpp
  		// P2 (ruling 28): every idle leg (to its slot, between slots, out to a building and back, to the rank) is planned round the keep-outs.
  		if (bStand)
  		{
  			Desired = T.DrawCm;
  		}
  		else
  		{
  			Desired = FreeWalkTo(T.IdleGoalCm, true);
  			// P2 W2: an idle walk never takes the figure further out than IdleLeashCm from its sim position (past 8000 cm AdvanceDrawn would snap
  			// it back and the walk would loop). Only a step that ends beyond the leash AND further out than the figure is drawn is held: this
  			// frame's progress is taken back and the figure stands. A figure already beyond it (a spell that began out there) always walks back
  			// in. An inspection walk held at the leash ends, and the figure walks back to its slot.
  			const double LeashNowCm = FVector::Dist2D(Desired, SimPos);
  			if (LeashNowCm > IdleLeashCm && LeashNowCm > FVector::Dist2D(T.DrawCm, SimPos))
  			{
  				T.FreeDoneCm = FMath::Max(0.f, T.FreeDoneCm - FreeWalkCmS * Pace * DeltaTime);
  				Desired = T.DrawCm;
  				++IdleLeashFrames;
  				if (T.IdlePhase == 1)
  				{
  					T.IdlePhase = 3;
  					T.bFree = false;
  					++IdleLeashInspectEnds;
  				}
  			}
  		}
  ```

**W3a, held stand points** (`walks.md` 5.1-5.3, verbatim):
- The stand points in `RefreshIdlePads`' `if (bClear)` body, and `P.StandStart.Add(...)` after the stand-point loop.
- `FreeStand` after `ChooseIdlePad`.
- The pick `:1125-1134`.
- The dock abort right after `SimDt` (`:1099`).
- `ChooseIdlePad`'s keep path `:590-595` (a stand point that vanished ends the inspection).

**W3b, ring-slot closure** (`walks.md` 6.1, verbatim): the loop after the pad loop's `}` (`:496`).

**`LogIdleRobots`** (`walks.md` 7, verbatim except the summary line):
- The per-pad keep-out line.
- The `StayPut` case and `Stay` count.
- The rank line's "no working pad within 50 m of its sim position".
- ", stand point %d" on the inspecting line.
- ", %d staying put" on the summary.
- **The keep-out summary line, amended:**
  ```cpp
  		UE_LOG(LogRedHope, Display, TEXT("[RH.IdleRobots] keep-outs (ruling 28): %d finished surface pad(s) (%d switched off), %d docked robot(s), serial %u (%d move(s) since the last load on a docking change); since the last load %d kept-off plan(s), %d start exit(s), %d boxed in (today's route), %d frame(s) an idle walk held at the leash (%.0f m; %d inspection(s) ended there); the plans came no nearer than %.0f cm to a pad's centre and %.0f cm to a docked robot"),
  			KeepOuts.Num(), OffPads, DockNow.Num(), KeepOutSerial, DockSerialBumps, KeptOffPlans, KeptOffExits, KeptOffFallbacks, IdleLeashFrames, IdleLeashCm / 100.f,
  			IdleLeashInspectEnds, KeptOffNearestPadCm < TNumericLimits<float>::Max() ? KeptOffNearestPadCm : -1.f, KeptOffNearestDockCm < TNumericLimits<float>::Max() ? KeptOffNearestDockCm : -1.f);
  ```
- The header line (K1, `:700`), the first per-pad line and the summary's prefix keep the formats `cache_check.py` parses.

**`ResetTracking`** (`walks.md` 8, after K1's block): verbatim, with the counters line amended to
`KeptOffPlans = KeptOffExits = KeptOffFallbacks = IdleLeashFrames = IdleLeashInspectEnds = DockSerialBumps = 0;`.

**M1, the cargo lump** (`meshes.md` 2.3 (a)-(c) and (e), verbatim):
- (a) `LumpBone("chest")`, `LumpFrontYCm -14.4`, `LumpDropCm -4.0` after `RobotClipPath`.
- (b) `TrackEntities` `:1326-1333`: `SetAbsolute(false, false, true)`, the warning when there is no chest bone, `AttachToComponent(Skel, KeepRelativeTransform, LumpBone)`.
- (c) `Tick` `:1910-1920`: the scale-gated `SetRelativeLocation` / `SetWorldScale3D`.
- (e) the `[RH.IdleRobots] figures:` read-out after the summary `UE_LOG`, whatever its final text, and before `LogIdleRobots`' closing brace. Anchor it by that statement.
- **Frame cost, restated (M1 regression (1)).** A small rise is possible, about 1-3 µs per moving walker: an attached lump's transform is updated with its walker's moves and bone finalize. M1 is the only P2 item that adds per-frame work. The docs entry attributes any measured tick-median rise to M1 first. No attach/detach-on-show mitigation is built: the frame bar decides.

### 3.3 `RHClickTest.cpp` (C1-C4 with W2, W3a and W3b merged)
1. **FTest block** (1.4 (11)): replace `:909-945` with clicktest 6.1, with three edits:
   - delete the line `URHAgentVisualizerSubsystem::FIdleDebug IdleBeforeOff; ...`, and in the block's opening comment delete "the watched robot as drawn just before Switch OFF;";
   - the `IdleOnPadSeen` comment reads "holds as first seen on the pad, counted per sighting (a figure that leaves the pad's round and comes back onto the pad is counted again), and those of them tracked once seen off it";
   - append `walks.md` 9.1's fields (`IdleFarPad`, `IdleFarPad2`, `IdleFarPadCm`, `IdleFarPad2Cm`, `IdleFarNearestCm`, `IdlePairsSeen`, `IdlePairKeys`, `IdlePairNearestCm`, `IdlePairSaid`) after `FString IdleWhy, IdleSaid, IdleWatchSaid;`. `:946`'s `};` stays.
2. **Header comment** `:3617-3632`: clicktest 6.2, with three edits:
   - "just before the deck's Switch OFF is pressed ... the watched robot must be drawn at or on its way to its slot, its goal that slot";
   - add after "the fleet is released ..." the sentence: "P2: setup also places two far Charging Pads 120 m+ out by harness call (W2's reach bound: after Switch OFF the first is every robot's nearest working pad, and the watched robot must still take its Lander rank; W3's ring rule: the second's slot 0 lands on the first's slot 4 and must be closed), and the watch also waits for two figures inspecting one building at once, whose stand points must be at least 100 cm apart (W3).";
   - the job-walk sentence reads: "It holds the fleet, so no job claim can start while the step runs (a self-issued charge, which ruling 28 exempts, still can); an added phase could not steer the sim's claim past the pad, so ruling 28's job walks from a ring slot are shown by the captures".
3. **Setup** (1.4 (12)): clicktest 6.3's floor lines replace `:3713-3714`, then `walks.md` 9.2's far-pad block verbatim, then `:3715`.
4. **`IdleTrackAll`**: replace `:3719-3824` with clicktest 6.3's lambda, then add `walks.md` 9.3's pair loop after the per-figure loop, before the lambda's `};`. In the comment, the chord numbers read "(about 0.07 cm for the 14 cm a figure walks in a frame at 3x, 450 cm/s)" in place of "(0.16 cm for the 21 cm of a frame at 3x)".
5. **The wait** (`robots.idle wait`): clicktest 6.4's pick comment and timeout line. Its closing `}, 0.2f, 50.f);` (`:3925`) becomes:
   ```cpp
   		}, 0.f, 50.f); // P2 (C2): no wall-clock gap into the watch (FRunner still waits MinGapFrames): robots 4 and 5's round walks are under way across this hand-off, and a gap would give both legs one harness-made long poll
   ```
6. **The stand-still line** `:3906-3908`: clicktest 6.5, verbatim.
7. **The watch's exit** `:3983`: `if (!bLast && !(T->IdleWatchSaid.Len() > 0 && T->IdleRoundLegs > 0 && !bRoundOpen && T->IdlePairsSeen > 0))`.
8. **The watch's verdict** `:3993-4005`: clicktest 6.6, then `walks.md` 9.3's pair verdict (the "not exercised" and "< 100 cm" clauses, and the `IdleSaid` pairs text). In 6.6's `IdleSaid` `Printf`, the C1 text becomes `" (%d held as first seen on the pad, counted per sighting; %d of them tracked once off it)"`.
9. **C3, `robots.idle before off`**: clicktest 6.7 inserted as the first statement of the block at `:4016` (before `const auto GoalCheck`), with these edits:
   - the wait condition is `if (!bHome && bOut && !bLast)`, with no `IdleWhy` test;
   - delete `T->IdleBeforeOff = D;`;
   - the PASS text: `TEXT("; just before Switch OFF it was drawn at or on its way to slot %d of pad #%d, its goal that slot (%.1f, %.1f) m%s")`;
   - the FAIL text: `TEXT(" just before Switch OFF robot %d was not drawn at or on its way to its slot of pad #%d with the pad on: drawn %d at pad #%d, goal (%.1f, %.1f) m, slot (%.1f, %.1f) m, the pad %s;")`;
   - in its comment, "anything else (a task, the rank, another pad, the pad off) is decided at once", with "an earlier failure" removed.

   The queue order becomes: select, before off, off (move/down/up), off goal, on (move/down/up), on goal, report. `GoalCheck` and the presses are unchanged.
10. **The report step** `robots.idle`: `walks.md` 9.4's W3b slot check, before `Report(...)` (`:4083`).

No `Report` call and no world click are added. `robots.idle` stays the last step before `final`.

### 3.4 `RHColonyVisualizerSubsystem.h` (S1)
- `sites-market.md` S1.4.2: `CutHoldStep`, `CutReleaseStep` and `PoseStep` after `RevealHeightCm` (`:333`).
- S1.4.6: `bool Debug_PoseSite(int32 BuildingId, int32 Percent);` after `LogSites` (`:104`).
- S1.4.5 / S1.4.7: the comments at `:100-101` and `:103`.

### 3.5 `RHColonyVisualizerSubsystem.cpp` (S1, then M2)
**S1** (`sites-market.md` S1.4.1-S1.4.7, verbatim except `LogSites`):
- `FCutHold CutHolds[] = { {SolarPanel, 9, 14}, {BatteryBank, 1, 3} }` and the `static_assert` before `RHSiteKit`'s `}`.
- The `AddSite` lookup after `Site.RevealMesh = Mesh;`.
- In `RefreshSites`: the non-const `Step` plus the pose, and `CutStep`.
- `Debug_PoseSite` after `LogSites`, setting `bPassesDirty` only.
- **`LogSites` (S1 amendment, both lenses):** "(held)" is read from the cut as drawn (read back), not from the table. The `if (... GetSiteRevealCut(...))` body becomes:
  ```cpp
  			const int32 CutPct = FMath::RoundToInt32(100.f * (CutZ - BottomZ) / S.RevealHeightCm);
  			Reveal += FString::Printf(TEXT(", cut %d%%%s"), CutPct, CutPct < S.Step * 100 / RHSiteKit::Steps ? TEXT(" (held)") : TEXT(""));
  ```
  The rest of S1.4.5 is unchanged: the `(posed)` stage, the `Reveal` string and the comment ("(held) when the cut as drawn is under the step").
- Frame cost: one int compare per site per 1 s pass poll (the pose test), plus two compares when the step moves. No per-frame work.

**M2** (`meshes.md` 5.3 (a), (b), (d), verbatim; (c) amended), anchored by symbol after S1:
- (a) `ConvoyRoverPath`, `ConvoyKitTag`, `ConvoyFrontAxleCm 128.75`, `ConvoyRearAxleCm 161.25` and `ConvoyMaxPitchDeg 30` before the namespace's `}`.
- (b) the spawn block: the kit, `NoCollision`, a MID with `PoweredState 1`, the tag, the pack and beacon `Part`s, the primitive fallback, the `[Convoy]` log line and the editor label kept.
- (d) the comment.
- **(c) the placement, amended (M2 regression (3)).** A tagged kit with no heading must not take the +60 cm branch. Replace from `FVector Loc = FMath::Lerp(From, To, Alpha);` through the end of today's `if (!Travel.IsNearlyZero())` block with:
  ```cpp
  		FVector Loc = FMath::Lerp(From, To, Alpha);
  		const FVector Travel = (To - From).GetSafeNormal2D();
  		if (Rover->ActorHasTag(ConvoyKitTag))
  		{
  			if (Travel.IsNearlyZero())
  			{
  				// P2 M2: no heading to take: the kit sits level on its soles (its pivot), keeping its last yaw.
  				Loc.Z = RHMarsTerrain::SceneryTopZCm(Loc.X, Loc.Y);
  				Rover->SetActorLocationAndRotation(Loc, FRotator(0.f, Rover->GetActorRotation().Yaw, 0.f));
  			}
  			else
  			{
  				// <meshes.md 5.3 (c)'s tagged branch body, verbatim: Zf, Zr, Zc, PitchDeg, TanPitch, Loc.Z, the rh.RoverYawOffsetDeg read,
  				// SetActorLocationAndRotation>
  			}
  		}
  		else
  		{
  			Loc.Z = RHMarsTerrain::SceneryTopZCm(Loc.X, Loc.Y) + 60.f;
  			Rover->SetActorLocation(Loc);
  			// The cab leads: face the rover down its road (X+ = nose).
  			if (!Travel.IsNearlyZero())
  			{
  				Rover->SetActorRotation(Travel.Rotation());
  			}
  		}
  ```
  `:5152` (`SetActorHiddenInGame(!bSurface)`) stays.
- **The beacon's side.** If frame `_convoy_03` (camera yaw 154, the convoy's left side, 6.2) or `_convoy_02` shows the sphere on the thin mast rather than the short lens, change only the beacon `Part`'s `-55.f` to `55.f`. Re-take both runs' frames from the same command file, and say so in the docs.

### 3.6 `RHCheats.cpp` (S1, S2)
- **S1** (`sites-market.md` S1.4.6): `GRHSitePose` straight after `GRHSites`, verbatim. S1.4.5: the `RH.Sites` help text at `:2583` gains "the model's cut as drawn (held when a per-model cut hold keeps it lower)".
- **S2, amended** (S2 correctness: the design's `GetMarketRegionIdAt(0) >= 0 ? ... : 0` would print nothing for a market whose `RegionId` is -1, which `RH.Market inject-region Foo -1` can make). In both branches:
  ```cpp
  			// E7 review: GetMarketRegionCount() is Markets.Num() as last synced and does not sync; GetMarketRegionIdAt syncs first
  			// (SyncRivalEconomies), so it is called for that before the count bounds the loop. Nothing in the loop bumps the rivals generation.
  			(void)Sim->GetMarketRegionIdAt(0);
  			const int32 Regions = Sim->GetMarketRegionCount();
  			for (int32 m = 0; m < Regions; ++m)
  ```
  - The history branch replaces `:1585`.
  - The default branch replaces `:1629`, and `:1632`'s condition becomes `if (Regions > 1)`.
  - No string changes. `:1536` and the deck's `MarketRegionsOf` (`RHCommandDeckPanels.cpp:4143-4156`) are not touched; the second is a recorded follow-up.

### 3.7 `docs/design-decisions.md`
One entry, titled exactly "P2 follow-ups (display only): what it settles, and where it chose (2026-11-03, not yet committed)". It holds everything gate step 10 lists, plus:
- **Per GO item:** what it builds and the numbers that decide it:
  - `KeepOutCm` 350 (and why the drawn radius is `FitCm / 2` whatever the asset);
  - the outline at 373.6-447 cm;
  - `IdleReachCm` 5000 and the leash 7500;
  - the stand points 150 / 100 cm;
  - the cache key;
  - the lump at 126 cm over the soles;
  - the kit's axles;
  - the Solar hold 9/14 with release at 70 % (why not 65) and the Battery row.
- **The verifier corrections, recorded as facts:**
  - walks advance at `FreeWalkCmS x Pace` = 450 cm/s at 3x, about 14 cm per poll; 675 cm/s is the `MaxSpeed` catch-up cap;
  - a healthy long poll needs a frame over about 444 ms; `p2_mut_c2`'s polls are about 420 cm apart; the chord dip is about 0.07 cm;
  - "no long poll" is round 2's five main-scene logs (round 0/1's test printed none);
  - the frame-window serial evidence: the three final-binary windows (`frame_r2q1_w2_F`, `w3_F`, `w6_F`) printed serial 151 at 153 s, and the B windows ran the pre-idle binary, which has no `RH.IdleRobots`;
  - the `cap_far` before-snap time of about 58-63 s;
  - W1's cost: keep-out boxes near a pad join the graph of any blocked kept-off leg in `PlanRoute`'s 30 m region, and a boxed-in plan costs a second `PlanRoute`; the tick median still should not move.
- **Deviations and disclosed limits:**
  - `walks.md` 12's list;
  - a rank slot moved by a keep-out can coincide with the next index's;
  - a staying-put figure is in none of the route log's three idle counts nor its parked count;
  - a figure held at the leash on an inspection walk walks back (1.4 (3));
  - docked rovers and road-docked walkers are discs at the pad's centre (1.4 (2));
  - the Battery Bank row (1.2);
  - the `cache.rank` report covers only the no-working-pad case: the full ring's once-a-second look is not exercised on `idle_day` (20 slots, at most about 5 idle figures, 4 slots excluded by the docked robot), and a mutation forcing `bRetry` false would pass every gate check, so that path rests on the code;
  - the cube fallback under `rh.ModelSetV2 0` gives the same 200 cm;
  - the far pads stay in the click world after `robots.idle` (only `final` follows; it reads `leak.total`);
  - a robot that self-seeks a charge in the 15 s OFF window drives to the far pad (that run only);
  - `p2_mut_c1` is the one two-hunk mutation (5);
  - S2 has no failing mutation (no reachable state differs; verified by reading).
- **Supersedes:** the idle entry's "Known cost" bullet (K1).
- **Follow-ups:** section 10's list. The entry names them; this gate does not edit `orchestrator-handoff.md` or `ORCHESTRATOR-RULINGS.md`, which are the orchestrator's.
- That `gate-d-wording.md` gains no block, and why.

---

## 4. Tests and checks

### 4.1 Counts (state these exactly)
- **Main scene: `CLICKTEST TOTAL pass=115 fail=0`**, three counted runs (`p2_click1`-`3`, 1440x860, 450 s, quiet starts, ruling 26). No `Report` call is added, so every new check folds into `robots.idle`'s one line. No world click is added: `leak.total` still reads 11 reached / 11 on purpose, and `ExpectedWorld` is unchanged.
- **Deepbench: PASS at 9** (`p2_deepbench`). It does not run `robots.idle`.
- **Battery:** 64 runs, `BATTERY COMPLETE: 0 suite(s) not green`, no new suite (section 8).
- **Playtest:** `p2_full` cmp-identical to `Saved/baselines/ind_full_playtest.txt`.

### 4.2 `robots.idle`, the expected PASS line (main scene; `~` values are expectations, not checks)
```
CLICKTEST robots.idle PASS pad #8 at (10.0, -10.0) m, PadHalf 200 cm: R1 540, band 480-820, floor 350 cm (ruling 28's keep-out, the test's own: drawn radius 200 + 100, never under 350; the visualizer's 350); robot 0 first seen waiting there ~1560 cm from its centre, stood still at ~540 cm (slot 0; the visualizer's ring 540 cm), ~1370 cm from its Lander rank slot, nearest on the way ~540 cm; robot 0 watched ~N sim s: its wait, building #1 inspected and back at slot 0; the floor over every figure at the pad: 7 tracked, N leg(s), >=2 round the pad walked to the end (first: robot 4 ..., walked round it no nearer than ~374-385 cm), nearest ~374-385 cm (robot 4 or 5); >=1 pair(s) of figures inspecting one building at once, nearest ~150 cm apart (robots 3 and 6 at building #1, stand points 0 and 1); just before Switch OFF it was drawn at or on its way to slot 0 of pad #8, its goal that slot (6.2, -6.2) m; after Switch OFF its goal is (-7.5, -5.5) m, its Lander rank slot; after Switch ON its goal is (6.2, -6.2) m, slot 0 of pad #8; far pads #9 at (-120.0, -120.0) m and #10 at (-127.6, -127.6) m, ~164 m from the nearest robot's sim position: #10's slot 0, 0 cm from #9's slot 4, closed (W3)
```
**Every counted log must also show:**
- "ChargePad renders as imported model 'ChargePad' (uniform scale 1.01": a fit change shows up here. The floor stays 350 while the drawn half-extent is 250 cm or less.
- No "first seen on the pad" text (no figure starts on the pad in the main scene).
- No "over 200 cm apart" text in a healthy run.

**Timing (healthy):**
- the watch ends at the first poll that has a clean round leg, no open round leg and a pair: typically about +15 s after the pad is placed, at worst the 45 s bound;
- `before off` normally decides on its first poll; if the watch ends after about +18.4 s, robot 0 is out on its second round and `before off` waits at 3x, up to 10 s (typically under 7 s);
- the step takes about 17-19 s healthy, and the run about 205 s, with a passing worst case near 245 s of the 450 s bound;
- a failing run's worst case is about 139 s for the step (bounds unchanged).

### 4.3 Dev runs (uncounted; labels `p2_dev_click<k>`, each named before it starts) and when to stop
Before freezing the final sources, run the main scene at least once, then report and check:
- **7 tracked** figures.
- **Floor 350**, the visualizer's 350.
- **Nearest 373 cm or more.** Robots 4 and 5 walked round.
- **Far pads** at (-120, -120) m and (-127.6, -127.6) m.
- **#B's slot 0 closed** by #A.
- **The W3 pair:** robots, building, stand points and distance. Expected: robots 3 and 6 at #1, the Lander, at stand points 0 (5.9, -5.9) m and 1 (5.9, -4.4) m, about 150 cm apart. `N` is 3: SolarArray #5 is exactly 2000.0 cm from the pad, inclusive.
- **`IdleLeashFrames` 0.**

**Stop** (`stop_reason`, `complete = false`), and never widen a check, if:
- fewer than 7 figures come to the pad because a robot's `SimCm` is over 50 m from it (report every `SimCm`);
- the watched robot's rank is over 5000 cm from its `SimCm`, so it stays put;
- no pair forms within the watch ("stand points not exercised");
- the visualizer's `KeepOutCm` disagrees with the test's floor (fix W1 to `walks.md` 3.3; never move the test's number);
- any fix would need a sim, data or Content change.

A pair at a building other than #1 is not a failure: report it and keep the check as is.

### 4.4 Named capture-log checks (run on the after logs; each with its mutation in section 5)

| Check | Log | Pass |
|---|---|---|
| `cache.quiet`, `cache.fresh`, `cache.reload` | `p2_after_cache` | `python3 -I WORK/checks/cache_check.py <log> after`: all three PASS, plus `cache.rank` reported (about 4 figures in the rank, 0 rebuilds and 0 choices after a rebuild from 68 s to 78 s). The before run (`p2_before_cache`, `before`): `cache.quiet` FAIL (serial about +30 from 30 to 60 s, about +10 in the rank interval), the other two PASS. That is the cost being fixed. |
| `M1 cargo on the back` | `p2_after_cargo` | On each `[RH.IdleRobots] figures:` line whose `(\d+) cargo lump\(s\) shown` is 1 or more: `the farthest (\d+) cm across from its walker's pelvis` is 45 or less, and both values of `centres (-?\d+) to (-?\d+) cm above their walkers' soles` lie in 110-140. At least one such line (#3 hauls about 138.5-142 s). The line's first figures, `the farthest pelvis N cm from its drawn spot` (about 91) and `reference soles A to B` (about 34), are recorded for M1b/M1c and **not** judged. |
| `M2 convoy kit` | `p2_after_convoy` | `[Convoy] #1 to Zarya drawn as the cargo rover kit (Rover, true scale)` is present, and no `drawn as the primitive rover` line. |
| `S1 cut hold` | `p2_after_sites` | **The run is void** (move the pause later or re-place, then re-run under a new label) unless the `RH.Sites` at 56 s lists exactly the two sites `SolarArray ... at (-10.0, -10.0) m` and `BatteryBank ... at (-10.0, 0.0) m`, and no `[RH.SitePose]` line says `no such site`. **Then every posed line carries `(posed)` and:** Solar frames 00 `481 cm model, cut 40%`, 01 `cut 45%`, 02-05 `cut 45% (held)`, 06 `cut 70%`; Battery frames 09 `361 cm model, cut 5%`, 10 `cut 5% (held)`, 11 `cut 15%`. In the supplementary frames 12-25, a Solar line at fabrication 50-65 % reads `cut 45% (held)`, and every other line's cut equals its step. |
| `S2 market lines` | `p2_before_market`, `p2_after_market` (and `p2_after_sites` at 3/3.5 s) | The blocks extracted by the S2 command below the table are identical before and after; at least one resource line (7 expected); `history, region 0:` present; no `  region` line. This proves non-regression only (3.7). |
| W1 read-out (reported; the click floor and the presentation review are ruling 28's checks) | `p2_after_day`, `p2_after_night` | Every keep-out summary: nearest to a pad's centre 373 cm or more, and to a docked robot 198 cm or more (150 cm is the ruling's line). Each pad line: "0 in range dropped". A value under 350 or under 150 is a ruling-28 failure to report and fix, never to explain away. |
| W2 / W3b read-out (reported; the click checks are the proof) | `p2_after_far` | At 40 s every idle figure reads `Lander rank (no working pad within 50 m of its sim position)`. The `[RobotRoute]` gap max stays near the rank distance (about 2000-3000 cm), where the before run's climbs past 7000 cm and snaps at about 58-63 s. The 8 s dump's far pad #28 keep-out line reads `near a lower pad's slot: 0 (by pad #27)`. |

The S2 extraction (`sites-market.md` S2.4; copy it from here, not from the table):
```
grep -E 'LogRedHope: Display: (\[RH\.Market\]|  )' <log> | sed -E 's/^\[[^]]*\]\[ *[0-9]+\]//'
```

Each checker lives in `WORK/checks/` (`cache_check.py`, see 6.3; the other checks may be small python scripts or exact greps), runs with `python3 -I`, and prints the check's name with PASS or FAIL.

---

## 5. Mutations (each its own label `p2_mut_<id>`; freeze the final sources by sha256 first; restore by copy with a fresh file time, compare sha256, rebuild the final binary and confirm the installed dylib's hash before any later gate step)

### 5.1 Click mutations: `RH_CLICKTEST_RES=1440x860 bash scripts/unreal/rh_clicktest.sh p2_mut_<id> 450`; each expects **114/1**, with `leak.total` passing and only the named clause in `robots.idle`'s FAIL

Pad Ids in the expected texts are as round 2's runs predict: harness pad #8, far pads #9 and #10. Use the Ids the dev run reports.

| Label | The one change | Expected `robots.idle` FAIL | Passes without the item because |
|---|---|---|---|
| `p2_mut_c1` | the two-hunk staging of 5.2 (C1 has no main-scene case without it) | `robot 5 came N cm from the pad's centre (never under the floor, 350 cm, ...)`, with N robot 5's straight line (168-271 cm recorded). The log has the release line ("robot 5, first seen on the pad ~1800 cm from its centre, is off it at ... ~1800 cm from the centre: tracked from here"; both distances are about 1800 cm, because it is staged at its first sighting and released on its next poll). The line reads "(1 held as first seen on the pad, counted per sighting; 1 of them tracked once off it)". | HEAD's `TSet` never releases robot 5, so its straight walk is never judged |
| `p2_mut_c2` | first statement of `IdleTrackAll`'s per-figure loop: `if (GFrameCounter % 30 != 0) { continue; }` (1.4 (6)) | `floor not exercised: ... (C such leg(s) cut short, G walked to the end but judged in part by end points only: <first three>, none under way);` (its closing words may instead read `one still under way when the watch ended`). Require the clause with `IdleRoundLegs` 0, **not** an exact G (likely 4 or more: robot 4's inspection round also crosses the pad). The watch runs its 45 s; C3 waits for robot 0 if it is out. | HEAD counts any round leg walked to its end |
| `p2_mut_c3` | in the phase-0 `AtPad` block, after `if (T.bAtSlot) {...}` and before the block's `}` (1.4 (8)): `if (T.IdlePhase == 0 && T.bAtSlot && T.InspectCount > 0 && Pace < 2.f) { T.IdleShow = (uint8)EIdleShow::LanderRank; T.IdleGoalCm = T.IdleRankCm; T.bIdleFace = false; }` | `just before Switch OFF robot 0 was not drawn at or on its way to its slot of pad #8 with the pad on: drawn 3 at pad #8, goal (-7.5, -5.5) m, slot (6.2, -6.2) m, the pad on;` | HEAD's OFF check is met by a figure already in the rank (115/0) |
| `p2_mut_w1` (= C4's) | delete `Obstacles.Append(KeepOutBoxes);` in `PlanKeptOff` | `robot 5 came ~315 cm from the pad's centre (never under the floor, 350 cm, ruling 28's keep-out, for any figure at the pad; 1 cm allowed for rounding);`. HEAD's routes: robot 4 at 334 cm, robot 5 at 315 cm (W3a's longer watch: robot 3 stays at about 342 cm or more). | the 290 cm floor passes 315 |
| `p2_mut_w2` | delete `if (PadId != 0 && FVector::Dist2D(SimPos, PadCm) > IdleReachCm) { PadId = 0; }` in `ChooseIdlePad` | `after Switch OFF the pad is off and robot 0 is drawn 2 at pad #9 with its goal (...) m (rank slot (...) m, ring slot (...) m);` (no snap inside the 15 s check: at most about 34 m walked at 1x) | without the reach bound the far pad is the nearest working pad |
| `p2_mut_w3a` | in `FreeStand`, `bFree = !InDockDisc(...)` only (the hold dropped) | `robots a and b at building #k, stand points 0 and 0 headed for stand points 0 cm apart (W3: 100 wanted);` (a, b and k as found; expected 3, 6 and #1) | HEAD has no pair check |
| `p2_mut_w3b` | delete W3b's closure loop in `RefreshIdlePads` | `far pad #10's slot 0 at (-123.8, -123.8) m, 0 cm from far pad #9's slot 4 (open), is open, closed by pad #0 (W3: closed by pad #9 wanted);` | HEAD has no slot check |

### 5.2 `p2_mut_c1`, the two hunks (one patch, both files restored by sha256)
(a) `RHClickTest.cpp`, in the new `IdleTrackAll`, inside `if (!F || !F->bTracked)` and before C1's on-pad test:
```cpp
					// MUTATION p2_mut_c1 (staging): robot 5 is taken as first seen on the pad at its first sighting.
					if (i == 5 && !F && !T->IdleOnPad.Contains(i))
					{
						T->IdleOnPad.Add(i, P);
						++T->IdleOnPadSeen;
						continue;
					}
```
(b) `RHAgentVisualizerSubsystem.cpp`, in the idle branch's `else` of `if (bStand)`, immediately before `Desired = FreeWalkTo(T.IdleGoalCm, true);`:
```cpp
			// MUTATION p2_mut_c1 (staging): robot 5's idle legs walk straight to their goal, ignoring the pad, and nothing re-checks them.
			if (i == 5)
			{
				if (!T.bFree || FVector::Dist2D(T.IdleGoalCm, T.FreeGoalCm) > 100.f)
				{
					T.bFree = true;
					T.FreeGoalCm = T.IdleGoalCm;
					T.FreePath = { T.DrawCm, T.IdleGoalCm };
					T.FreeLenCm = PathLength(T.FreePath);
					T.FreeDoneCm = 0.f;
					T.bFreeKeptOff = true;
				}
				T.KeepOffSerial = KeepOutSerial;
			}
```
Before counting the run's verdict, confirm in its log that robot 5's leg-end line reads a nearest under 350 cm, which shows its walk really was straight.

### 5.3 Capture-session mutations: build, then `WORK/cap/p2_capture.sh p2_mut_<id> installed <scene>`, then the scene's checker. Each fails only its named check.

| Label | The one change | Scene | Expected |
|---|---|---|---|
| `p2_mut_cache_quiet` | `RefreshIdlePads`: `if (bSame && NumSurface == IdlePadsKey.Num())` becomes `... && !bIdleRetryDue)` | `cache` | `cache.quiet` FAIL (serial +30 from 30 to 60 s; route lines with rebuilds and no building event since the last line); `fresh` and `reload` PASS |
| `p2_mut_cache_fresh` | `IdlePadKeyEntry`: drop its last term, the pad-off bit `((B.bManualOff && B.DefName == ChargePadName) ? 2u : 0u)`, with the bitwise-or before it (`cache.md` 7, M2) | `cache` | `cache.fresh` FAIL (2 working pads at 68 s and 78 s; no rebuild after the switch off or on); `quiet` and `reload` PASS. **Not run in the click scene.** |
| `p2_mut_cache_reload` | `ResetTracking`: delete `bIdlePadsBuilt = false;` | `cache` | `cache.reload` FAIL (0 working pads at 96 s, no rebuild across the reload); `quiet` and `fresh` PASS |
| `p2_mut_m1` | delete `Lump->AttachToComponent(Skel, FAttachmentTransformRules::KeepRelativeTransform, LumpBone);` | `cargo` | `M1 cargo on the back` FAIL (the lump at the world origin: thousands of cm across, about -38 cm above the soles) |
| `p2_mut_m2` | `ConvoyRoverPath`: `Rover.Rover` becomes `Rover.RoverX` | `convoy` | `M2 convoy kit` FAIL (`drawn as the primitive rover (no Rover kit)`) |
| `p2_mut_s1` | `RefreshSites`: `const int32 CutStep = Step;` | `sites` | `S1 cut hold` FAIL: Solar `cut 50%`/`55%`/`60%`/`65%` and Battery `cut 10%`, with no `(held)`; frames 02-04 show the floating strip growing, and frame 10 the hovering tubes |

**S2: no mutation.** No reachable state differs before and after (begin play, load and `inject-region` all sync; `RH.InjectRivals` keeps the count at 1 or more). The gate report says so and verifies the fix by reading.

13 mutation runs in all. Record each mutation and the line it failed in the docs entry.

---

## 6. Captures (before and after from the same command files; 1440x860; `Saved/RHCapture/p2/`)

### 6.1 The capture script
Copy `/Volumes/Unreal/red_hope/wip-backups/idle-work/r2/cap/idle_capture.sh` to `WORK/cap/p2_capture.sh`. Change only these:
- `W` / `CAP` to `WORK` / `WORK/cap`;
- `BEFORE` to `Saved/orchestrator/p2_bin_before/libUnrealEditor-RedHope.dylib` (and its `.sha256`);
- `FINAL` to `WORK/final/libUnrealEditor-RedHope.dylib` (and its `.sha256`, written when the final binary is built);
- `OUT` to `$P/Saved/RHCapture/p2`;
- `LOGS` to `WORK/cap/logs`;
- the default `SCENES` to `day night far cache cargo convoy sites market`.

Keep:
- its modes (`installed` for pre-step (c) and the mutations; `final` for the after run);
- its calls to `idle-work/r0/wait_ready.sh` and `preflight.sh` (read-only tools);
- its hash records;
- its **420 s** timeout for every scene, `cap_cache` included (K1 regression (3)). `rh_capture.sh` stops once the expected PNGs appear.

A run's PNGs are `<label>_<scene>_NN.png`.

### 6.2 Command files (`WORK/cap/cap_<scene>.cmds`, one line each; record each one's sha256 in the meta)

| Scene | File | Shots | Items |
|---|---|---|---|
| `day` | byte copy of `idle-work/cap/cap_day.cmds` (sha256 `117a82630a5b5ae6c11b0825af04ab6bcbdb9b647fbbea45e06631456d3c35c5`) | 13: `_day_00..12` | W1 (pad #6's docked robot and the ring: 00-06; the fabricator leaving slot 3 for the Stockpile site and inspection walks past the pad: 09-12), W3a (dumps), K1 (77-109 s, both pads off: route lines read 0 rebuilds; reported) |
| `night` | byte copy of `idle-work/cap/cap_night.cmds` (sha256 `46fe19d8f27fb1974e00d76fc4693ad29a6f48b71569999869a09e2c1fc4a11f`) | 5: `_night_00..04` | W1 |
| `far` | new (below) | 3: `_far_00..02` | W2, W3b |
| `cache` | `cache.md` 6.2, verbatim | 1: `_cache_00` (only ends the run; not judged) | K1 |
| `cargo` | `meshes.md` 2.7, verbatim (`cube_day.cmds` with its one stated change, checked: 16 snapshots) | 16: `_cargo_00..15` | M1 (09 close, 10 top-down, 12 close) |
| `convoy` | `meshes.md` 5.6 with frame 03's camera **`RH.After 22 RH.Cam 12.9 26.3 0.02 0 154`** (the convoy's left side, lens nearest; M2 correctness) | 4: `_convoy_00..03` | M2 |
| `sites` | below (`sites-market.md` S1.5, expanded) | 26: `_sites_00..25` | S1 (and S2 at 3/3.5 s) |
| `market` | `sites-market.md` S2.4, verbatim | 1: `_market_00` | S2 |

`cap_far.cmds` (`walks.md` 4.5, amended: the 46 s shot moves onto the figures' path, and dumps are added at 60 and 68 s to catch the before run's snap and walk-out):
```
r.setres 1440x860w, RH.ClearTips, RH.After 2 RH.Load idle_day, RH.After 2.5 RH.SetSpeed 1, RH.After 3 RH.Floor 0, RH.After 3.5 rh.RobotRouteLog 1, RH.After 5 RH.Spawn ChargePad 110 20, RH.After 5.5 RH.Spawn ChargePad 120.626 21.932, RH.After 8 RH.IdleRobots, RH.After 8 RH.Fleet, RH.After 10 RH.Power 6 0, RH.After 10 RH.Power 26 0, RH.After 11 RH.Cam -6 2 0.08 0 0, RH.After 40 RH.IdleRobots, RH.After 41 RH.Snapshot 0.5, RH.After 44 RH.Cam 60 20 0.12 0 0, RH.After 46 RH.Snapshot 0.5, RH.After 49 RH.Cam -6 2 0.08 0 0, RH.After 60 RH.IdleRobots, RH.After 68 RH.IdleRobots, RH.After 75 RH.IdleRobots, RH.After 76 RH.Snapshot 0.5, RH.After 79 RH.Power 6 1, RH.After 79 RH.Power 26 1, RH.After 81 RH.Status
```

`cap_sites.cmds` (one line; the design's `for k` schedule expanded; 26 snapshots):
```
r.setres 1440x860w, RH.ClearTips, RH.After 2 RH.SetSpeed 1, RH.After 3 RH.Market, RH.After 3.5 RH.Market history, RH.After 4 RH.Build SolarArray -10 -10, RH.After 4.5 RH.Build BatteryBank -10 0, RH.After 54 RH.SetSpeed 0, RH.After 55 RH.Floor 0, RH.After 56 RH.Sites, RH.After 57 RH.Cam -10 -10 0.07 10 0, RH.After 58 RH.SitePose all 40, RH.After 60 RH.Sites, RH.After 60 RH.Snapshot 0.5, RH.After 62 RH.SitePose all 45, RH.After 64 RH.Sites, RH.After 64 RH.Snapshot 0.5, RH.After 66 RH.SitePose all 50, RH.After 68 RH.Sites, RH.After 68 RH.Snapshot 0.5, RH.After 70 RH.SitePose all 55, RH.After 72 RH.Sites, RH.After 72 RH.Snapshot 0.5, RH.After 74 RH.SitePose all 60, RH.After 76 RH.Sites, RH.After 76 RH.Snapshot 0.5, RH.After 78 RH.SitePose all 65, RH.After 80 RH.Sites, RH.After 80 RH.Snapshot 0.5, RH.After 82 RH.SitePose all 70, RH.After 84 RH.Sites, RH.After 84 RH.Snapshot 0.5, RH.After 86 RH.Cam -10 -10 0.07 10 90, RH.After 86 RH.SitePose all 50, RH.After 88 RH.Sites, RH.After 88 RH.Snapshot 0.5, RH.After 90 RH.SitePose all 70, RH.After 92 RH.Sites, RH.After 92 RH.Snapshot 0.5, RH.After 94 RH.Cam -10 0 0.07 10 0, RH.After 95 RH.SitePose all 5, RH.After 97 RH.Sites, RH.After 97 RH.Snapshot 0.5, RH.After 99 RH.SitePose all 10, RH.After 101 RH.Sites, RH.After 101 RH.Snapshot 0.5, RH.After 103 RH.SitePose all 15, RH.After 105 RH.Sites, RH.After 105 RH.Snapshot 0.5, RH.After 107 RH.SitePose all off, RH.After 107 RH.Cam -10 -10 0.07 10 0, RH.After 108 RH.SetSpeed 1, RH.After 110.5 RH.SetSpeed 0, RH.After 111 RH.Sites, RH.After 111 RH.Snapshot 0.3, RH.After 112 RH.SetSpeed 1, RH.After 114.5 RH.SetSpeed 0, RH.After 115 RH.Sites, RH.After 115 RH.Snapshot 0.3, RH.After 116 RH.SetSpeed 1, RH.After 118.5 RH.SetSpeed 0, RH.After 119 RH.Sites, RH.After 119 RH.Snapshot 0.3, RH.After 120 RH.SetSpeed 1, RH.After 122.5 RH.SetSpeed 0, RH.After 123 RH.Sites, RH.After 123 RH.Snapshot 0.3, RH.After 124 RH.SetSpeed 1, RH.After 126.5 RH.SetSpeed 0, RH.After 127 RH.Sites, RH.After 127 RH.Snapshot 0.3, RH.After 128 RH.SetSpeed 1, RH.After 130.5 RH.SetSpeed 0, RH.After 131 RH.Sites, RH.After 131 RH.Snapshot 0.3, RH.After 132 RH.SetSpeed 1, RH.After 134.5 RH.SetSpeed 0, RH.After 135 RH.Sites, RH.After 135 RH.Snapshot 0.3, RH.After 136 RH.SetSpeed 1, RH.After 138.5 RH.SetSpeed 0, RH.After 139 RH.Sites, RH.After 139 RH.Snapshot 0.3, RH.After 140 RH.SetSpeed 1, RH.After 142.5 RH.SetSpeed 0, RH.After 143 RH.Sites, RH.After 143 RH.Snapshot 0.3, RH.After 144 RH.SetSpeed 1, RH.After 146.5 RH.SetSpeed 0, RH.After 147 RH.Sites, RH.After 147 RH.Snapshot 0.3, RH.After 148 RH.SetSpeed 1, RH.After 150.5 RH.SetSpeed 0, RH.After 151 RH.Sites, RH.After 151 RH.Snapshot 0.3, RH.After 152 RH.SetSpeed 1, RH.After 154.5 RH.SetSpeed 0, RH.After 155 RH.Sites, RH.After 155 RH.Snapshot 0.3, RH.After 156 RH.SetSpeed 1, RH.After 158.5 RH.SetSpeed 0, RH.After 159 RH.Sites, RH.After 159 RH.Snapshot 0.3, RH.After 160 RH.SetSpeed 1, RH.After 162.5 RH.SetSpeed 0, RH.After 163 RH.Sites, RH.After 163 RH.Snapshot 0.3
```

### 6.3 `cache_check.py` (in `WORK/checks/`): `cache.md` 6.3 verbatim, with two edits (K1 regression (2))
1. **The parse loop.** Replace the block from `if cur is not None and '[RH.IdleRobots]' in ln:` through the `cur = None` line after it with the block below. Every `[RH.IdleRobots]` line then attaches to the latest header, so a line interleaved from another thread cannot drop pad #6's line.
   ```python
           if '[RH.IdleRobots]' in ln:
               if cur is not None:
                   p = PAD.search(ln)
                   if p:
                       cur['padlines'][int(p.group(1))] = ln
                   w = RANK.search(ln)
                   if w:
                       cur['rank'] = int(w.group(4))
               continue
   ```
2. **`cache.quiet`'s route test.** It uses log order, not a 4 s wall-clock window. A route line with rebuilds must have a building-event line between the previous `[RobotRoute]` line and itself:
   ```python
       prev_k = -1
       for r in routes if which == 'after' else []:
           if r['rebuilds'] > 0 and not any(prev_k < e['k'] < r['k'] for e in events):
               why.append(f'{r["at"]}: {r["rebuilds"]} rebuild(s) with no building event since the last [RobotRoute] line')
           if r['after'] > 0 and r['rebuilds'] == 0:
               why.append(f'{r["at"]}: {r["after"]} choice(s) after a rebuild in a line with no rebuild')
           prev_k = r['k']
   ```

Run it with `python3 -I`. A `session incomplete` result (for example, the spawned Stockpile is not #27) is neither a pass nor a fail: re-run under a new label and report it.

### 6.4 Names, runs, and what to look for
- **Before**, pre-step (c), installed HEAD binary: `WORK/cap/p2_capture.sh p2_before installed`. That gives `p2_before_day_00..12`, `p2_before_night_00..04`, `p2_before_far_00..02`, `p2_before_cache_00`, `p2_before_cargo_00..15`, `p2_before_convoy_00..03`, `p2_before_sites_00..25` and `p2_before_market_00`.
  - The before binary has no `RH.SitePose`: its `sites` frames show the unposed site and its log "command not recognized". It is a record, void only by the two-sites rule.
- **After**, gate step 9, final binary: `p2_after` with the same eight scenes and the same names.
- **Mutations** (5.3): `p2_mut_cache_quiet_cache_00`, `p2_mut_cache_fresh_cache_00`, `p2_mut_cache_reload_cache_00`, `p2_mut_m1_cargo_00..15`, `p2_mut_m2_convoy_00..03` and `p2_mut_s1_sites_00..25`.
- **LOOK at every after frame beside its before frame** with the Read tool, and say what changed for each GO item:
  - no walking figure on a pad's disc or within about 150 cm of a docked robot (W1);
  - the ring still a loose ring facing out (director Q-A (a));
  - figures inspecting one building stand apart (W3a);
  - the fabricator leaving its slot for the Stockpile site goes round the pad (W1, `_day_09..12`);
  - the far run's rank, not the far pad (W2);
  - the lump on #3's back with one shadow (`_cargo_09`, `_10`, `_12`);
  - the convoy as the rover kit with its beacon on the lens (`_convoy_00..03`);
  - Solar 50-65 % with no teal bar or panel strip, and the Battery at 10 % with no hovering tubes (`_sites_02..05`, `_10`).

  Confirm nothing else changed.
- **Tell the presentation review:**
  - every robot body is drawn 90.6 cm ahead of its drawn spot and 34 cm up (M1b/M1c, deferred), so judge distances to the pad and to the docked robot by the spots in the `RH.IdleRobots` dumps where pixels are ambiguous;
  - the robot that went idle on pad #6 starts on the docked robot's spot (both near (11, 17) m), and its first seconds walking out are the ruling's "leaving the mast" exemption;
  - any change in the cargo lump's place in other after frames (for example `_day_09..12`, where #3 hauls) is M1's.

---

## 7. The frame check plan (gate step 8; rulings 18, 22, 24, 25, 27)
- **Scripts.** Copy `idle-work/r2/frame2/frame_session.sh`, `analyze.py` and `frame_run.sh` to `WORK/frame/`. Change only:
  - their paths (`S` / `W` to `WORK/frame`; `BEFORE` to `Saved/orchestrator/p2_bin_before/...`; `FINAL` to `WORK/final/...`);
  - their labels (`p2_f1`, `p2_f2`, ..., each written in `WORK/frame/labels.txt` before it runs).

  They keep calling `idle-work/r0/wait_ready.sh` and `preflight.sh`.
- **The scene: the `idle_day` save**, fixed in `frame_session.sh`: load, 1x, `RH.Floor 0`, camera, then `rh.RobotRouteLog 1` from **120 s** after the commands fire (ruling 27; the script refuses any other `WIN0`) for a **32 s** window, with `RH.Status` before and after and `RH.IdleRobots` after.
- **The schedule.** `B F F B B F` per session, fixed before it runs (ruling 24 (1)). Each binary is installed only by the script's own copy-and-restore; after each session, restore the final dylib and confirm its hash.
- **Counting.** A pair counts only when `uptime`'s 1-minute load, read just before its first window, is under 5 with no other Unreal, `Build.sh` or `rh_*.sh` process running (ruling 18; say if its editor-alone fallback applied). No counted window crosses a sol boundary or a save (ruling 25); `analyze.py` reports both. Every counted pair joins the median; a session is never stopped early, repeated or dropped for its results. A further session runs only while fewer than three pairs count.
- **The bar:** the median over every counted pair of (final - before) `[RobotRoute]` tick median is at most **+0.2 ms**. Report every pair with its load and whether it counted, the median, and the first three counted pairs in order. Three counted pairs not reached within 45 minutes of the first quiet session start is a busy machine (`stop_reason`; ruling 27 then re-runs the frame check alone), never a pass.
- **Added report (K1, not a bar).** The P2 `analyze.py` sums each final window's `pad cache: N rebuild(s) ... (M after a rebuild)` and prints them beside its median; expected 0 and 0. Its `ROUTE` regex is unchanged: the new text comes after "frame(s)". Before-binary lines carry no counts.
- **Expected:** a delta within noise of 0. Only M1 adds per-frame work (3.2), and any rise is attributed to it first. W1's plans are events, not per-frame work. K1 removes a once-a-second spike the median never saw.

---

## 8. Battery lines expected to move: none (display only)
- `bash scripts/sim/rh_battery.sh 10` gives `BATTERY COMPLETE: 0 suite(s) not green`, **64** runs, and every log newer than both dylibs.
- `python3 scripts/sim/diff_battery.py Saved/baselines/battery_post_idle Saved/RHBattery` shows **wall-clock digits only** (the EP4 and X16 lines, and ARRV e if present). No version line, size line or suite moves. Any other differing line means the sim moved: stop and find the cause.
- The pins in `baseline.log`: "power: gen 50 W load 20 W", "deposit Regolith_A: 40645 kg".
- `git diff --stat -- Source/RedHopeSim docs/data Content/RedHope/Data` is empty, and the installed RedHopeSim dylib's sha256 is unchanged.
- The playtest is byte-identical: the playtest `.txt` keeps only `[Playtest]` lines, and no sim number moves.

---

## 9. Deviations the builder must NOT take
- **Files.** No edit under `Source/RedHopeSim`, `docs/data`, `Content/` or `Config/`; no new asset; no battery suite. No edit to `orchestrator-handoff.md`, `ORCHESTRATOR-RULINGS.md` or `gate-d-wording.md`. No commit, and no `git stash`, `checkout`, `reset` or `add`.
- **Fixed code and constants.** Do not change `PlanRoute`, `SegmentBlocked`, `RebuildObstacles`, `AdvanceRover`, `Tick`'s `bDrive` or `bDeepDraw` branch, rovers, `ParkAfterIdleS`, `FreeWalkCmS`, `InspectWaitS`, the ring constants, or the rank's grid, origin, spacing or order (only 3.9's keep-out skip).
- **The docked robot.** Do not move its drawing, any walker's body position, its Z or `rh.RobotPlantCm`. Do not build M1b, M1c, X1 or X2.
- **The keep-out.**
  - Do not make it waypoints on a circle (an inscribed polygon dips 1.3-3 cm inside).
  - Do not take any `FIdlePad` / `FIdleKeepOut` value from an actor, a component (`GetBuildingVisual`) or a cvar.
  - Do not drop the exemptions or apply the keep-out to the docking robot's own approach.
  - Do not reset `KeepOutSerial`.
- **The cache.** Do not rebuild on a timer, hash the key, add `bPowered` or a non-pad switch to it, reset `IdlePadsKey` in `ResetTracking`, or move the `MakesWay` check behind the full-ring look.
- **The click test.**
  - Do not weaken any check: the floor under `KeepOutCm`; a tolerance over 1 cm; the 200 cm long-poll threshold; the 100 cm stand-point and slot distances; C3's bound over 10 s; a test number taken from the visualizer; an existing check removed.
  - Do not add a `Report` call or a world click, or change the step order beyond inserting `robots.idle before off`.
  - Do not place the far pads with `CanPlaceBuildingSized`, or anywhere but setup.
  - Do not ship C4 without W1 and C1, or keep the 290 cm floor with W1 built.
- **Log formats.** Do not change the `[RobotRoute]` line before "frame(s)", or the `RH.IdleRobots` header, first per-pad line or summary prefix, which `analyze.py` and `cache_check.py` parse.
- **Sites.** Do not add cut-hold rows beyond `SolarPanel` and `BatteryBank`: none for the fallback set's `extractor2` or `solar2`, which are recorded only. Do not tint the convoy by rival or re-add a world label. Do not touch `RHCommandDeckPanels.cpp`'s `MarketRegionsOf`.
- **Banned in any new code:** a random draw, a player-facing string, per-frame planning or allocation.

---

## 10. Deferred items and why (not built)

| Id | What | Why deferred (verdicts) | Next step |
|---|---|---|---|
| **M1b** | re-centre the walker's body on its drawn spot (the mesh's pivot is 90.6 cm behind the body) | It moves every robot's drawing, the docked robot's on its pad included, which this gate may not move (builder rule), and it changes every robot capture before the director has seen it. Both lenses: sound. | Its own small gate or an orchestrator ruling, built with M1c and M1 in one build. The exact change is in `meshes.md` 3. Re-capture `cap_cargo` and do the ruling-28 pixel review on that build. Record that the lift rider is re-centred too. |
| **M1c** | put the walker's soles on the ground (take off the sim's 50 cm agent offset; `rh.RobotPlantCm` 16 to 2) | It drops every robot 36 cm, the docked one included, and supersedes the director's 2026-08-18 plant calibration: a look decision for the director. Both lenses: sound. | With M1b; `meshes.md` 4 |
| **X1** | D7's mid-trip deep path (ruling 21's follow-up) | A sim change (four files under `Source/RedHopeSim`, a new `-atriums` AT14, `atriums.log` 106 -> 109), outside a display-only gate. Ruling 21's literal rule also has two gaps: a later-floor walk, and a boarded robot boarding twice. So it needs an orchestrator amendment of ruling 21 first (a technical scope ruling, not a director question). | Amend ruling 21 to `deferred.md` X1.2's rule, then its own gate after T1/T2, re-anchored by symbol |
| **X2** | commuter shuttles drawn as boxes: a real shuttle kit | It needs new content (a mesh, an MI and masks), which this gate may not add, and the director's unanswered look question L3 (`living-city-handplay-card.md:303`; art job Q parked on L3/L4). | Put L3 to James as lettered options (recommended (a): a tram-family people-mover; ask whether cars come too), then a presentation gate in the code-built lane (`rh_hs.py` + `rh_import_hs.py`); `deferred.md` X2.5 |

**Follow-ups not built (for the docs entry):**
- **C1's known limit:** a figure still held as first seen on the pad is not judged while it crosses the disc; it is caught once off it.
- **The deck's `MarketRegionsOf` bound** (`RHCommandDeckPanels.cpp:4143-4156`).
- **The fallback model set's reveal faults:** `extractor2` 20-75 %, `solar2` 10-15 %, `RH_AirFilter2`'s finished-model gap; `battery` and `ice` unverifiable.
- **The full ring's once-a-second look** is unexercised by any gate check (3.7).
- **A road-trip job walk from a ring slot** keeps `PlanRoute`'s clearance, because `AdvanceRover` is untouched.
- **A docked cargo rover's nose and tail** are kept 124 cm off, not 150.

---

## Appendix A. Every verifier amendment and where it is applied

| Item | Lens | Amendment | Applied |
|---|---|---|---|
| C1 | both | `p2_mut_c1` hunk (b) sets `bFreeKeptOff` and `KeepOffSerial` | 1.4 (7), 5.2 (serial refreshed every frame for robot 5) |
| C1 | both | FTest replaces `:909-945`, keeps `:946` | 1.4 (11), 3.3 (1) |
| C1 | correctness | `IdleOnPadSeen` counts per sighting | 3.3 (1) comment, (8) text |
| C1 | regression | the release line reads about 1800 cm twice; confirm robot 5's nearest in the log | 5.1, 5.2 |
| C2 | both | C3's wait drops `IdleWhy.IsEmpty()` | 1.4 (4), 3.3 (9) |
| C2 | correctness | expect "floor not exercised" with `IdleRoundLegs` 0, not an exact G; either closing phrase | 5.1 |
| C2 | both | speeds 450 cm/s, 14 cm/poll, 444 ms, 420 cm | 3.3 (4) comment, 3.7 |
| C2 | regression | the wait's `GapAfter` 0.2 -> 0 | 1.4 (5), 3.3 (5) |
| C2 | correctness | "0 in 14 logs" is round 2's five | 3.7 |
| C3 | both | the mutation hunk inside the phase-0 block | 1.4 (8), 5.1 |
| C3 | correctness | drop or use `IdleBeforeOff`; "drawn at or on its way to" wording | 1.4 (9), 3.3 (9) |
| C3 | regression | timing with W3a: a passing run may wait up to about 7 s at 3x | 4.2 |
| C4 | correctness | nearest about 374-385 cm in the PASS line | 4.2, 5.1 |
| C4 | both | independence: the drawn-radius source is settled (asset + fit; the actor measured by the test; the 350 decided by PadHalf + 150) | 1.3, 1.4 (1) |
| C4 | correctness | 14 cm per poll, dip 0.07 cm | 3.3 (4), 3.7 |
| C4 | correctness | the job-walk wording | 1.4 (15), 3.3 (2) |
| C4 | regression | ships only with W1 and C1; merge order; the scale-1.01 line in every log | 1.4 (12), 1.4 (14), 4.2 |
| W1 | regression | (a) docked churn | 1.4 (2), 3.2 `GatherDocked`, summary counter |
| W1 | regression | (b) cost text corrected | 3.7 |
| W1 | regression | (c) `FitCm / 2` without `LoadObject` | not taken; reason in 1.4 (1); the finding is recorded in 3.7 |
| W1 | regression | (d) a moved rank slot can coincide with the next | 3.7 |
| W1 | correctness | robot 6's route note; the mast exemption for the pad-#6 figure in the review | 3.7, 6.4 |
| W2 | both | `cap_far` timing (snap about 58-63 s), the path shot, the dumps at 60/68 s | 6.2, 4.4, 3.7 |
| W2 | regression | (a) the outward-only leash; (b) the inspection ends at the leash | 1.4 (3), 3.2 |
| W2 | both | disclose the leash hold; the staying-put figure missing from the parked count | 3.7 |
| W3a | regression | timing restated; the dev run reports the pair, stops if none forms, never widens | 4.2, 4.3 |
| W3b | correctness | depends on W1's `SlotNearSlotOf.Init` | 1.4 (14) |
| K1 | correctness | fix the frame-window evidence line; disclose the full ring's look as unexercised | 3.7 |
| K1 | regression | (1) W1's key contract | 1.3, 1.4 (1), 3.1 comment |
| K1 | regression | (2) checker: log order; lines attached to the latest header | 6.3 |
| K1 | regression | (3) 420 s for every `cap_cache` run | 6.1 |
| M1 | correctness | the check reads the lump clause, not the pelvis clause | 4.4 |
| M1 | regression | (1) frame cost restated and owned by M1; (2) the review told of lump moves in other captures | 3.2, 7, 6.4 |
| M2 | correctness | frame 03 at yaw 154; judge the beacon on 02 too | 6.2, 3.5 |
| M2 | regression | (1) anchor by symbol, since S1 edits the same file; (2) market lines may differ for non-M2 reasons; (3) a tagged kit with no heading | 1.4 (13); this note; 3.5 |
| S1 | both | "(held)" from the read-back cut | 3.5 |
| S1 | both | frame-cost wording; anchors by content; RH.Sites lines in other after captures gain `, cut N%` (no checker compares them) | 3.5, 1.4 (13), this note |
| S2 | correctness | sync, then read the count unconditionally (a RegionId -1 market) | 3.6 |

Note for the `cap_convoy` and `cap_sites` logs (M2 regression (2)): after S2, `RH.Market` calls `GetMarketRegionIdAt(0)` before the loop, so an after log's market lines can differ from the before log's for reasons that are not M2's. The convoy line ("outbound N%") and the `[Convoy]` line are unaffected, and `S2 market lines` judges only the fresh-world `market` scene.
