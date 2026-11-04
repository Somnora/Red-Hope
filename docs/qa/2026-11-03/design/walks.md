# P2 design, key `walks`: idle walks keep off a charging pad (W1, ruling 28), stay within reach of the sim (W2), and never share a spot (W3)

Read-only design at HEAD `6df7fcc` ("Ruling 28: walking figures keep off a charging pad (display only)"). Every line number below is at that HEAD and was read from the file. Numbers come from the code, the idle gate's logs, or arithmetic shown here. This file replaces a `walks.md` left at 12:05 by an earlier attempt at this step: I re-checked that version against the code and kept its approach to W1, its pad bound for W2 and its W3 stand points. I fixed one fault and changed three things (section 12).

**Files changed:** `Source/RedHope/Private/RHAgentVisualizerSubsystem.cpp` and `Source/RedHope/Public/RHAgentVisualizerSubsystem.h`. The proofs add hunks to `Source/RedHope/Private/RHClickTest.cpp`, folded into the `robots.idle` step, so no step is added. Those hunks must merge with the `clicktest` design (section 9).

**What stays the same:**
- Nothing under `Source/RedHopeSim`, `docs/data` or `Content/RedHope/Data` changes.
- No player-facing string is added. The new text is `UE_LOG` lines, the `RH.IdleRobots` cheat and click-test text, all developer-facing.
- No `FMath::Rand*` or `FRandomStream` is used. Every choice is keyed on indices, Ids and sim-paced time.
- No route planning and no allocation happen per frame. Routes are planned only on a goal change, a kept-off flag change, or a keep-out change that blocks the rest of a walk. The caches are built only on a pad-cache rebuild or a change in which robots are docked.

| Item | What | Rec. | Proof: the check that fails without it | Click steps |
|---|---|---|---|---|
| W1 | Ruling 28. Idle legs, and a job walk that starts in a pad's round, keep the figure's centre at least `KeepOutCm` from every finished pad's centre (350 cm for the 2x2 pad) and 150 cm from every robot drawn docked at one. `PlanRoute` is unchanged. | **GO** | The `robots.idle` floor is raised to `KeepOutCm` (clicktest C4). Mutation `p2_mut_w1` (= `p2_mut_c4`) removes the keep-out from the plan; robots 4 and 5 then walk round at 334 / 315 cm and the step FAILs. The docked robot and the job walk are shown by the `cap_day` captures and the new `RH.IdleRobots` keep-out line. | 0 |
| W2 | An idle figure takes a pad only if its centre is within `IdleReachCm` = 5000 cm of its sim position. Otherwise it takes its Lander rank slot if that is within 5000 cm, and otherwise it stays put. A per-frame leash at 7500 cm makes the snap loop impossible. | **GO** | `robots.idle` places a far pad 120 m+ out. After Switch OFF it is every robot's nearest working pad. The existing OFF goal check (the rank) FAILs under `p2_mut_w2` (bound removed). Capture `cap_far`: before, the 80 m snap loop; after, the rank. | 0 |
| W3a | Figures inspecting one building take different stand points: up to 5 per building, 150 cm apart along its clearance box, held the way slots are held. | **GO** | `robots.idle`: two figures inspecting one building at once must head for points at least 100 cm apart. `p2_mut_w3a` (no hold) FAILs it. | 0 |
| W3b | A ring slot within 100 cm of an open slot of a lower-Id cached pad is closed. | **GO** | `robots.idle`: a second far pad 2 x R1 further out on the same ray, so its slot 0 lands on the first's slot 4. That slot must read closed. `p2_mut_w3b` (no closure) FAILs it. | 0 |

---

## 1. What I read

### 1.1 The Charging Pad as drawn, and `KeepOutCm` = 350 cm

- **The mesh.** The colony visualizer draws a finished ChargePad as the kit model `/Game/RedHope/Art/Models2/ChargePad/ChargePad/StaticMeshes/ChargePad.ChargePad`:
  - `ModelPathsV2`, `RHColonyVisualizerSubsystem.cpp:2444-2447`;
  - `rh.ModelSetV2` defaults to 1 (`:58-61`);
  - `RealModelPaths` has no ChargePad entry, so with the V2 asset missing, or under `rh.ModelSetV2 0`, the pad is the cube primitive.
- **The fit**, `HandleBuildingAdded` (`:2499-2526`):
  ```cpp
  2499		if (RealModel)
  2500		{
  2501			Mesh->SetStaticMesh(RealModel);
  2502			const FBoxSphereBounds MB = RealModel->GetBounds();
  ...
  2513			if (Instance.DefName == FName("Electrolyzer") || Instance.DefName == FName("WaterPlant") || Instance.DefName == FName("ChargePad"))
  2514			{
  2515				const URHDefinitionsSubsystem* FitDefs = World ? World->GetSubsystem<URHDefinitionsSubsystem>() : nullptr;
  2516				if (const FRHBuildingRow* FitDef = FitDefs ? FitDefs->GetBuilding(Instance.DefName) : nullptr)
  2517				{
  2518					Fit.X = FMath::Max(1, FitDef->FootprintX) * 2.f;
  2519					Fit.Y = FMath::Max(1, FitDef->FootprintY) * 2.f;
  2520				}
  2521			}
  2522			const float TargetCm = FMath::Min(Fit.X, Fit.Y) * 100.f;
  2523			const float S = TargetCm / FMath::Max(2.f * FMath::Max(MB.BoxExtent.X, MB.BoxExtent.Y), 1.f);
  2524			Actor->SetActorScale3D(FVector(S));
  2525			// Recenter horizontally, sit the bounds bottom on the ground.
  2526			Actor->SetActorLocation(Seated - FVector(MB.Origin.X, MB.Origin.Y, MB.Origin.Z - MB.BoxExtent.Z) * S);
  ```
  The model's larger horizontal half-extent is drawn at exactly `TargetCm / 2`. It is centred on the pad's location (`Seated`, `:2350`) and not turned (`SpawnActor<AStaticMeshActor>(Location, FRotator::ZeroRotator)`, `:2352`).
- **The row.** `docs/data/RH_Buildings.csv:6`: `ChargePad,Charging Pad,2,2,...`. It is 2 x 2 cells, not drag-sized, with `CoverageRadius_m` 0 and `LinkRange_m` 0. So `TargetCm` = min(4, 4) x 100 = 400 cm.
  - `PadHalf` = `GetFootprintHalfCm(row, 1, 1)` = 2 x 100 = 200 cm (`RHSimWorldSubsystem.cpp:8831-8835`).
- **The mesh's own bounds.** The source `Martians/gen/hs/ChargePad/ChargePad.glb` has POSITION min/max `(-1.98, 0, -1.98)` / `(1.98, 3.226, 1.98)` m and no node transforms, read with a python glTF reader, so `MB.BoxExtent` X/Y = 198 cm.
  - The docstring of `scripts/blender/rh_build_chargepad.py` (lines 25-29) says: "the game fits the LARGER horizontal extent to the 4.0 m footprint, so nothing may leave the deck circle: deck radius 1.98 m ... The script asserts the deck radius is the largest radius in the mesh." So the larger half-extent *is* the drawn disc's radius.
  - S = 400 / 396 = 1.0101. The runtime log agrees: `ChargePad renders as imported model 'ChargePad' (uniform scale 1.01, textured, ...)` (`wip-backups/idle-work/r2/cap/logs/idle_r2_after_day.log`, and 10 earlier capture logs in `Saved/RHCapture/logs/`).
- **The drawn radius = 198 x 1.0101 = 200.0 cm.**
  - The cube fallback (`ScaleFor`, `:2259-2284`: `(4, 4, 0.25)` m) also has a larger half-extent of 200 cm.
- **`KeepOutCm` = max(200 + 100, 200 + 90 + 60) = max(300, 350) = 350 cm** for the 2x2 pad. The ruling's floor term decides it. Every ChargePad is 2x2, because the row is fixed and not drag-sized. The keep-out is under ring 1 (540 cm), so it moves no slot.
- **Where W1 takes it from: the asset, not the actor.** The `cache` design (`cache.md` section 9) asks for "the asset ... or a constant ... never from a spawned actor". The `clicktest` design measures the spawned actor itself for its own number (`clicktest.md` 5.1), so the two derivations stay independent.
  - W1 loads the same asset path once, takes its bounds and applies the same fit (3.3).
  - The colony visualizer has the asset loaded by then: `HandleBuildingAdded` loads it synchronously when it draws the first pad, before the agent visualizer's next tick.

### 1.2 The docked robot

- **Target.** The sim's Charge task targets the pad's centre: `Task.TargetCm = PadLoc;` (`RHRobotTaskProcessor.cpp:114`). `PadLoc` comes from `FindNearestChargePad`, which returns `B.LocationCm` (`RHSimWorldSubsystem.cpp:13409`). The robot stops within `RH::ArriveDistCm` = 300 cm of it (`RHRobotMovement.h:13`, `:21`) and sets `Phase = 1`.
- **Where it is drawn.**
  - On the idle gate's day capture: sim `(10.5, 17.3)` m for pad #6 at `(10.0, 20.0)` m, 275 cm out; drawn at `#0 busy, drawn at (10.5, 17.5) m`, 255 cm out (`idle_r2_after_day.idlerobots.txt`).
  - A robot that docks by road is drawn at the centre: `AdvanceRover`, `:876-879`, "the drawn truck drives the rest of the way in". That includes a cargo rover; rovers dock at ChargePads too (`NAME_ChargePadRover(TEXT("ChargePad"))`, `RHSimRovers.cpp:19`).
- **`GatherDocked`** (`:515-544`) collects, every tick, the SIM positions of robots with `TaskType == Charge && Phase != 0` at a cached pad. `NearDock` uses them (500 cm, `:499-513`).
- **What is drawn where.**
  - A walker's drawn position is `Trips[i].DrawCm`. The drive branch keeps it in step: `WT.DrawCm = ...`, `Tick` `:1733-1744`.
  - A rover's drawn position is `RoverVis[i].Drive.Pos`. Its `Trips[i]` is never advanced, because the drive branch keeps `Trips` only `if (!bRoverI ...)`, `:1733`.

### 1.3 Today's walks round a pad (the problem W1 fixes)

`PlanRoute` (`:232-333`) routes round every footprint's clearance box:
- each box is the footprint + 90 cm (`RebuildObstacles`, `:197-218`);
- box corners are pushed 10 cm out (`:262`);
- any box holding either end is dropped (`:247`: `!O.IsInside(A) && !O.IsInside(B)`).

Round a 2x2 pad that box is 290 cm, so a walk along its side passes about 300 cm from the centre. Round 2's click run measured it (`Saved/RHCapture/logs/idle_r2_click1.log:1111-1112`):
- `robots.idle leg: robot 4 reached (13.8, -13.8) m round the pad, nearest the pad's centre on the leg 334 cm`;
- `robot 5 ... 315 cm`.

In `idle_r2_after_day_04.png` a figure on a job walk from its slot stands on the pad's near rim. In `_day_12` an inspection walk passes the near-left rim; I looked at both frames.

The free walk (`AdvanceDrawn`, `:959-971`):
```cpp
959		auto FreeWalkTo = [&](const FVector& Goal)
960		{
961			if (!T.bFree || FVector::Dist2D(Goal, T.FreeGoalCm) > 100.f)
962			{
963				T.bFree = true;
964				T.FreeGoalCm = Goal;
965				PlanRoute(T.DrawCm, Goal, T.FreePath);
966				T.FreeLenCm = PathLength(T.FreePath);
967				T.FreeDoneCm = 0.f;
968			}
969			T.FreeDoneCm = FMath::Min(T.FreeLenCm, T.FreeDoneCm + FreeWalkCmS * Pace * DeltaTime);
970			return PointAlong(T.FreePath, T.FreeDoneCm);
971		};
```
Its callers are `:1072`, the walk over when a task begins where the sim robot stands, and `:1169`, the idle branch. They are the only two (`grep "FreeWalkTo("`).

The off-road trip a job walk uses (`:1037-1053`):
```cpp
1037					if (!T.bTrip || T.bRoadTrip || FVector::Dist2D(Target, T.TripTargetCm) > 150.f)
1038					{
1039						T.bTrip = true;
1040						T.bFree = false;
1041						T.bRoadTrip = false;
1042						T.TripTargetCm = Target;
1043						T.SimStartDistCm = FMath::Max((float)FVector::Dist2D(SimPos, Target), 1.f);
1044						PlanRoute(T.DrawCm, Target, T.Path);
1045						T.PathLenCm = PathLength(T.Path);
1046					}
1047					const float S = FMath::Clamp(1.f - (float)FVector::Dist2D(SimPos, T.TripTargetCm) / T.SimStartDistCm, 0.f, 1.f);
1048					// Ease in and out: slower off the mark and into the stop, quicker in
1049					// the middle - the same arrival time.
1050					const float Eased = S - 0.22f * FMath::Sin(2.f * PI * S) / (2.f * PI);
1051					Desired = PointAlong(T.Path, Eased * T.PathLenCm);
1052					T.LastSyncedCm = Desired;
```
Road trips go to `AdvanceRover` through `bDrive` (`Tick` `:1688-1699`, `:1730-1745`). `AdvanceDrawn`'s own road branch (`:982-1034`) therefore does not run with routing on.

The job's arrival clears the idle state before either branch runs (`:953-958`):
```cpp
953		if (T.IdleS <= ParkAfterIdleS && T.IdleTick != 0)
954		{
955			// A job arrives (or the sim moves it): the slot is freed and the wait and any inspection end; the branches below take over
956			// from wherever the figure stands.
957			ResetIdle(T);
958		}
```

### 1.4 The 8000 cm snap (the problem W2 fixes)

```cpp
931		FRobotTrip& T = Trips[i];
932		if (!T.bInit || FVector::Dist2D(T.DrawCm, SimPos) > 8000.f)
933		{
934			// First sight, or a jump the walk can't explain (load, teleport): snap.
935			T = FRobotTrip();
936			T.bInit = true;
937			T.DrawCm = SimPos;
938			T.LastSyncedCm = SimPos;
939			return SimPos;
940		}
```
`T = FRobotTrip()` zeroes `IdleS` and `IdleTick`. So after `ParkAfterIdleS` (6 sim s) the figure goes idle again. `ChooseIdlePad` then picks the same pad, since `FindNearestChargePad` has no distance bound (`:13393-13432`), and the figure walks out again. That is the loop.

The Lander rank has the same exposure: `ParkSlotCm(i)` is taken with no bound either (`ChooseIdlePad` `:578`, `:614-621`).

### 1.5 Shared stand points and ring slots (W3)

Today's stand point (`RefreshIdlePads` `:466-495`) is one per building: the point of its clearance box nearest the pad's centre.
```cpp
475				const FVector2D At(FMath::Clamp(C.X, Box.Min.X, Box.Max.X), FMath::Clamp(C.Y, Box.Min.Y, Box.Max.Y));
476				bool bClear = true;
477				for (const FBox2D& O : Obstacles)
478				{
479					if (O.IsInside(At))
480					{
481						bClear = false;
482						break;
483					}
484				}
485				if (bClear)
486				{
487					P.Inspect.Add(B.Id);
488					P.InspectAtCm.Add(FVector(At.X, At.Y, 0.0));
489					P.InspectFaceCm.Add(FVector(BC.X, BC.Y, 0.0));
490				}
491				else
492				{
493					P.InspectDropped.Add(B.Id);
494				}
```
The pick (`:1125-1134`):
```cpp
1125					if (T.IdleWaitS >= T.IdleWaitForS && Pad->Inspect.Num() > 0)
1126					{
1127						// The building: keyed on the robot's index and how many it has inspected, never a random draw.
1128						const int32 Pick = (i + T.InspectCount) % Pad->Inspect.Num();
1129						T.IdlePhase = 1;
1130						T.InspectId = Pad->Inspect[Pick];
1131						T.InspectAtCm = Pad->InspectAtCm[Pick];
1132						T.InspectFaceCm = Pad->InspectFaceCm[Pick];
1133						T.bFree = false;
1134					}
```
Two figures that pick one building go to one point.

Ring slots (`:435-452`) are tested only against footprint boxes. So two pads 2 x R1 = 1080 cm apart on one ray from the Lander put the far pad's slot 0 exactly on the near pad's slot 4. Slot 0 faces the Lander (`:433-434`), and slot 4 is 180 degrees round.

### 1.6 The click scene (for the proofs)

- `robots.idle` runs on the `clickatrium` colony: sol 0, 5 buildings, 7 robots, 0 tasks, loaded at `idle_r2_click1.log:919-935`.
  - The Lander is at (0, 0). The harness pad lands at (10, -10) m (`:1130`).
  - The robots' figures were first seen at (-7.0..-5.1, -6.4..-1.4) m (`:1099-1105`), beside the starting fleet's deploy side. Every robot's sim position is near the colony, about 15-20 m from the harness pad.
  - Dig sites (`:928-930`): Regolith_A (20, -30) m, Regolith_B (-300, 40) m, Ore_A (-160, -150) m.
- **Coverage.** A ChargePad cannot be placed by `CanPlaceBuildingSized` 120 m out. Its row has no coverage of its own (`CoverageRadius_m` 0, `LinkRange_m` 0, not pressurized), so it takes the `else if (!IsInCoverage(LocationCm, Level))` refusal (`RHSimWorldSubsystem.cpp:9110-9130`). Coverage is the Lander's 30 m and a pylon's 40 m (`RH_Buildings.csv`; `IsInCoverage` `:12854-12872`).
  - `Debug_PlaceInstant` tests nothing: `AddBuilding(DefName, LocationCm, true, Level, SizeX, SizeY)`, `:12799-12807`. So the far pads are placed by `Debug_PlaceInstant` with the test's own footprint-overlap check (section 9).
- **Slot arrivals and first waits.** In `idle_r2_click1` (wall clock, 3x) robots 0-6 reached their slots at 41.89, 41.70, 42.72, 44.09, 45.24, 45.28 and 44.03 s (`:1106-1112`).
  - First waits are `InspectWaitS + (i * 7) % 20` = 20, 27, 34, 21, 28, 35, 22 sim s, so they pick at about 48.55 (logged 48.618, `:1113`), 50.70, 54.06, 51.09, 54.58, 56.94 and 51.36 s.
  - Robot 0's round ends at 50.65 s (`:1118`). Section 5.4 uses this timeline.

### 1.7 The sibling designs this one must agree with

- **`clicktest.md`**
  - C4 reads `FIdleDebug::KeepOutCm` (`clicktest.md` 5.4, 6.3: `T->IdleKeepOutCm = D.KeepOutCm;`) and requires it to equal the test's own 350 cm within 1 cm.
  - It defines the drawn radius as "the larger horizontal half-extent of the drawn mesh's bounds after the colony visualizer's fit". This design uses the same definition.
  - It asks W1's walked path to stay at or beyond `KeepOutCm`, with a margin, never an inscribed polygon. This design's outline stays 373.6 cm or more out (3.2).
  - Its mutation `p2_mut_c4` is W1's mutation.
- **`cache.md`**
  - `RefreshIdlePads` rebuilds only on a key change: pads, their state, the building count and the sites.
  - W1's keep-outs, W3a's stand points and W3b's closures are all functions of that key: a pad's position, its built and switched-on state, every footprint, the building count.
  - Docking changes come from `GatherDocked` every tick, with their own trigger, as `cache.md` section 9 asks.
  - `RH.IdleRobots` must keep its first per-pad line's format: the checker's `PAD` pattern is `pad #(\d+) at `, and buildings stay `#Id`. The new pad line reads `pad #N keep-out ...`, so it never matches.

---

## 2. Shared machinery

### 2.1 Constants: insert after `RHAgentVisualizerSubsystem.cpp:130` (`const FName LanderName(TEXT("Lander"));`), in that anonymous namespace

```cpp
	// P2, ruling 28 (docs/qa/2026-10-26/preflight/ORCHESTRATOR-RULINGS.md): walking figures keep off a charging pad (display only).
	// KeepOutCm = the pad's drawn radius + KeepOutOverDrawnCm, never under PadHalf + RobotClearanceCm + KeepOutOverClearCm (350 cm for the 2x2 pad).
	constexpr float KeepOutOverDrawnCm = 100.f;
	constexpr float KeepOutOverClearCm = 60.f;
	constexpr float DockKeepCm = 150.f;          // and at least this from a robot drawn docked at a pad
	constexpr float KeepRouteMarginCm = 25.f;    // routes are planned this far outside both (a figure lagging its path cuts a corner a few cm)
	constexpr float DockDriftCm = 25.f;          // a docked robot drawn this far from where the walks were planned round it: they are checked again
	// The Charging Pad's model as the colony visualizer draws it (RHColonyVisualizerSubsystem.cpp, ModelPathsV2): its bounds, fitted as the
	// visualizer fits them, give the pad's drawn radius.
	const TCHAR* ChargePadModelPath = TEXT("/Game/RedHope/Art/Models2/ChargePad/ChargePad/StaticMeshes/ChargePad.ChargePad");
	// P2 W2: an idle figure walks to a pad (its centre) or to its Lander rank slot only within IdleReachCm of its sim position, and an idle walk
	// never carries it more than IdleLeashCm from it (AdvanceDrawn snaps a figure drawn more than 8000 cm from its sim position back to it).
	constexpr float IdleReachCm = 5000.f;
	constexpr float IdleLeashCm = 7500.f;
	// P2 W3: a building's stand points stand InspectSpreadCm apart along its clearance box; no two inspecting figures' stand points, and no two
	// pads' open ring slots, stand nearer each other than IdleApartCm.
	constexpr float InspectSpreadCm = 150.f;
	constexpr float IdleApartCm = 100.f;
```
(The `cache` design also inserts after `:130`; the two blocks are independent.)

### 2.2 Geometry helpers: insert at the end of the second anonymous namespace, after `SegmentHitsBox` (before its `}` at `:195`)

```cpp
	// P2 (ruling 28): a keep-out disc of radius R as three axis boxes centred on it, half-extents (R, R/sqrt3), (R sqrt(2/3), R sqrt(2/3)) and
	// (R/sqrt3, R). Their union holds the disc, its outline stays within R sqrt(4/3) = 1.155 R of the centre, and PlanRoute (unchanged) routes
	// round boxes: a path that enters none of the three (as SegmentHitsBox insets them, 1 cm) stays at least R - 1.4 cm from the centre.
	constexpr double DiscSide = 0.57735026918962576; // 1 / sqrt(3)
	constexpr double DiscMid = 0.81649658092772603;  // sqrt(2 / 3)
	void AddDiscBoxes(TArray<FBox2D>& Out, const FVector2D& C, double R)
	{
		Out.Add(FBox2D(C - FVector2D(R, R * DiscSide), C + FVector2D(R, R * DiscSide)));
		Out.Add(FBox2D(C - FVector2D(R * DiscMid, R * DiscMid), C + FVector2D(R * DiscMid, R * DiscMid)));
		Out.Add(FBox2D(C - FVector2D(R * DiscSide, R), C + FVector2D(R * DiscSide, R)));
	}
	// Inside one of the three (strictly, as FBox2D::IsInside).
	bool InDiscBoxes(const FVector2D& P, const FVector2D& C, double R)
	{
		const double Dx = FMath::Abs(P.X - C.X), Dy = FMath::Abs(P.Y - C.Y);
		return (Dx < R && Dy < R * DiscSide) || (Dx < R * DiscMid && Dy < R * DiscMid) || (Dx < R * DiscSide && Dy < R);
	}
	// How far the ray from the disc's centre along the unit U runs before it leaves the last of its three boxes (each holds the centre and is
	// convex, so the ray leaves the union at the largest of the three exits).
	double DiscExitCm(const FVector2D& U, double R)
	{
		const auto Leave = [&U](double Hx, double Hy)
		{
			const double Tx = FMath::Abs(U.X) > UE_KINDA_SMALL_NUMBER ? Hx / FMath::Abs(U.X) : TNumericLimits<double>::Max();
			const double Ty = FMath::Abs(U.Y) > UE_KINDA_SMALL_NUMBER ? Hy / FMath::Abs(U.Y) : TNumericLimits<double>::Max();
			return FMath::Min(Tx, Ty);
		};
		return FMath::Max3(Leave(R, R * DiscSide), Leave(R * DiscMid, R * DiscMid), Leave(R * DiscSide, R));
	}
	// P2 W3: a point on a box's outline as its distance S along it, counter-clockwise from Min (bottom, right, top, left), and back (S wraps).
	double OutlineS(const FBox2D& Box, const FVector2D& P)
	{
		const double W = Box.Max.X - Box.Min.X, H = Box.Max.Y - Box.Min.Y;
		if (P.Y <= Box.Min.Y) { return P.X - Box.Min.X; }
		if (P.X >= Box.Max.X) { return W + (P.Y - Box.Min.Y); }
		if (P.Y >= Box.Max.Y) { return W + H + (Box.Max.X - P.X); }
		return 2.0 * W + H + (Box.Max.Y - P.Y);
	}
	FVector2D OutlineAt(const FBox2D& Box, double S)
	{
		const double W = Box.Max.X - Box.Min.X, H = Box.Max.Y - Box.Min.Y, L = 2.0 * (W + H);
		S = FMath::Fmod(S, L);
		if (S < 0.0) { S += L; }
		if (S < W) { return FVector2D(Box.Min.X + S, Box.Min.Y); }
		if (S < W + H) { return FVector2D(Box.Max.X, Box.Min.Y + (S - W)); }
		if (S < 2.0 * W + H) { return FVector2D(Box.Max.X - (S - W - H), Box.Max.Y); }
		return FVector2D(Box.Min.X, Box.Max.Y - (S - 2.0 * W - H));
	}
```

**Coverage proof.** Take a point of the disc, (x, y), with |x|, |y| >= 0.
- If |y| < R/sqrt3, it is in box 1 (|x| < R).
- Otherwise |y| >= R/sqrt3, so x^2 < R^2 - R^2/3 and |x| < R sqrt(2/3).
  - If |y| < R sqrt(2/3), it is in box 2.
  - Otherwise |x| < R/sqrt3, and it is in box 3.

A walkable point is one outside all three 1 cm inset boxes. The nearest such point to the centre is a concave corner of the union, sqrt((R sqrt(2/3) - 1)^2 + (R/sqrt3 - 1)^2) = R - 1.39 cm. I checked both kinds of concave corner.

`OutlineS` and `OutlineAt` agree at the four corners: S = 0, W, W + H and 2W + H.

### 2.3 Header (`Source/RedHope/Public/RHAgentVisualizerSubsystem.h`)

- **`EIdleShow`** (`:70-73`) becomes `NotWalker = 0, Busy, AtPad, LanderRank, Inspecting, StayPut`. The new value is last, so every existing compare and the click test's `(int32)D.Show` prints keep their meaning.
- **`FIdleDebug`** (`:74-93`): add after `bool bInspecting = false;` (`:92`):
  ```cpp
  		// P2 (the walks design).
  		FVector SimCm = FVector::ZeroVector;  // its sim position, for every tracked robot (its pad, its rank and their reach are chosen from it)
  		float KeepOutCm = 0.f;                // ruling 28: its pad's keep-out radius: no idle walk at that pad comes nearer its centre (0: no pad)
  		float DrawnRadiusCm = 0.f;            // that pad's drawn radius (the larger horizontal half-extent of its model's bounds, as fitted)
  		int32 InspectStand = INDEX_NONE;      // W3: which of its building's stand points it heads for or stands at (0: the one nearest the pad)
  ```
  `KeepOutCm` is the name the `clicktest` design reads.
- **Public accessors**, after `void LogIdleRobots() const;` (`:96`):
  ```cpp
  	// P2 (ruling 28; RH.IdleRobots, the click test): a finished surface Charging Pad's keep-out radius (0: no such pad), and its drawn radius
  	// into *OutDrawnRadiusCm when given.
  	float GetPadKeepOutCm(int32 PadId, float* OutDrawnRadiusCm = nullptr) const;
  	// The drawn positions of the robots docked at a finished surface pad on the last tick (every idle walk keeps DockKeepCm from each); their count.
  	int32 GetPadDockedDrawnCm(int32 PadId, TArray<FVector>& Out) const;
  	// P2 W3 (the click test): a cached pad's ring slot: where it stands, whether a figure may take it, and the lower pad whose open slot stands
  	// within IdleApartCm of it (0: none). False when the pad is not in the cache or has no such slot.
  	bool GetIdlePadSlot(int32 PadId, int32 Slot, FVector& OutCm, bool& bOutOpen, int32& OutNearSlotOfPad) const;
  ```
- **`FRobotTrip`** (`:191-241`): add after `bool bInspecting = false;` (`:240`):
  ```cpp
  		// P2 W2: no working pad, and not its Lander rank slot, within IdleReachCm of its sim position: it stands where it is drawn.
  		bool bStayPut = false;
  		// P2 (ruling 28): the task in hand arrived while the figure was drawn in a pad's round (a slot held): the task's walks (its trip, or the
  		// walk over to where it begins) keep off the pads until the task ends.
  		bool bKeepOff = false;
  		bool bFreeKeptOff = false;       // its free walk (FreePath) was planned round the keep-outs
  		uint32 KeepOffSerial = 0;        // the KeepOutSerial its kept-off walk or trip was last planned or checked against
  		float TripDoneCm = 0.f;          // how far along its off-road trip path it is drawn (what is left of a kept-off trip is checked from here)
  		int32 InspectStand = INDEX_NONE; // P2 W3: which stand point of its building it took (an index into that building's own)
  ```
- **`FIdlePad`** (`:247-262`): add after `TArray<FVector> DockedCm;` (`:260`):
  ```cpp
  		// P2 (ruling 28): its keep-out, as in KeepOuts (GetIdleDebug and RH.IdleRobots read it here).
  		float KeepOutCm = 0.f;
  		float DrawnRadiusCm = 0.f;
  		TArray<int32> SlotKeptOutBy;     // per slot: the pad whose keep-out holds it (0: none), so it is closed
  		TArray<int32> SlotNearSlotOf;    // P2 W3: per slot: the lower pad whose open slot stands within IdleApartCm of it (0: none), so it is closed
  		// P2 W3: per building in Inspect, its stand points StandCm[StandStart[b], StandStart[b + 1]); the first is InspectAtCm[b].
  		TArray<FVector> StandCm;
  		TArray<int32> StandStart;        // Inspect.Num() + 1 entries
  ```
- **Private members**, after `static void ResetIdle(FRobotTrip& T);` (`:270`):
  ```cpp
  	// P2 (ruling 28): the keep-out round every FINISHED surface Charging Pad (switched on or off), and round every robot drawn docked at one.
  	struct FIdleKeepOut
  	{
  		int32 PadId = 0;
  		bool bOff = false;
  		FVector2D CentreCm = FVector2D::ZeroVector;
  		float DrawnRadiusCm = 0.f;
  		float KeepOutCm = 0.f;
  	};
  	struct FDockDisc
  	{
  		int32 PadId = 0;
  		int32 Index = INDEX_NONE;             // the docked robot's tracked index
  		FVector2D DrawnCm = FVector2D::ZeroVector;
  	};
  	TArray<FIdleKeepOut> KeepOuts;   // rebuilt with the pad cache, in the sim's building order
  	TArray<FDockDisc> DockDiscs;     // the docked robots KeepOutBoxes was last built round
  	TArray<FDockDisc> DockNow;       // this tick's (GatherDocked)
  	TArray<FBox2D> KeepOutBoxes;     // three per disc (pads, then docked robots), appended to Obstacles only for one kept-off PlanRoute call
  	uint32 KeepOutSerial = 0;        // moves only when KeepOutBoxes changes; never reset, so a walk planned before a load can never match a later one
  	float PadModelHalfCm = -1.f;     // the Charging Pad model's larger horizontal half-extent, from its own bounds (-1: not read yet; 0: no model)
  	// Developer evidence for RH.IdleRobots since the last load: kept-off plans, start exits, plans boxed in (today's route instead), frames an idle
  	// walk held at the leash (W2), and the nearest a kept-off plan came to a pad's centre and to a docked robot.
  	int32 KeptOffPlans = 0, KeptOffExits = 0, KeptOffFallbacks = 0, IdleLeashFrames = 0;
  	float KeptOffNearestPadCm = TNumericLimits<float>::Max();
  	float KeptOffNearestDockCm = TNumericLimits<float>::Max();
  	void RebuildKeepOutBoxes();
  	void PlanKeptOff(const FVector& From, const FVector& To, TArray<FVector>& OutPath);
  	bool KeptOffClear(const TArray<FVector>& Path, float DoneCm) const;
  	bool AnyBoxBlocks(const FVector& A, const FVector& B) const;
  	int32 InPadKeepOut(const FVector2D& P, int32 ExceptPadId) const;  // the pad whose keep-out holds P (0: none)
  	bool InDockDisc(const FVector2D& P) const;
  	int32 FreeStand(const FIdlePad& Pad, int32 Building, int32 Self) const; // P2 W3
  ```
- **Name checks.**
  - No new name collides with an existing member or local in the touched functions: `-Wshadow` is clean.
  - `FDockDisc::Index` is a nested struct's field, not a class member, so `GetIdleDebug(int32 Index, ...)` does not shadow it.
  - No new include is needed. `Engine/StaticMesh.h` (`:20`), `MassEntitySubsystem.h` (`:9`) and `Mass/EntityFragments.h` (`:10`) are already there.

---

## 3. W1: ruling 28, the keep-out round a charging pad (GO)

### 3.1 The ruling mapped to code

- **The legs.** Each is planned with `PlanKeptOff`:
  - every idle free walk in `AdvanceDrawn`'s idle branch: to a slot; between slots after a re-choice or `MakesWay`; out to a stand point and back; to the Lander rank, for example after Switch OFF;
  - a job walk that starts in a pad's round: a task that arrives while the figure is drawn waiting at, or inspecting for, a pad. That covers its off-road trip and the walk over when the task begins where the sim robot stands.

  Other job walks keep `PlanRoute` exactly as today.
- **The keep-outs.**
  - Each finished surface Charging Pad, switched on or off: a disc of `KeepOutCm` (350 cm). A leg from a ring slot to the rank after Switch OFF keeps off that pad too.
  - Each robot drawn docked at one (`Charge`, `Phase != 0`): a disc of `DockKeepCm` (150 cm) round its drawn position.

  Each is routed at its radius + `KeepRouteMarginCm`, and a docked robot's also + `DockDriftCm`: 375 cm and 200 cm.
- **The exemptions.**
  - The robot docking at a pad, and a figure whose job site is the pad: `PlanRoute` already drops every box that holds the trip's end (`:247`). A trip whose target is the pad's centre (the Charge task) or lies in its keep-out (a job at the pad) therefore walks in.
  - The robot leaving the mast it charged at. Its leg starts inside the keep-out, so it first walks straight out along the ray from the pad's centre to 10 cm past the keep-out's boxes, then routes round.
- **`PlanRoute` is unchanged.** `PlanKeptOff` appends the keep-out boxes to `Obstacles` for one `PlanRoute` call and truncates them again. Its visibility graph then routes round the keep-outs and every building together, so keeping off the pad can never cut through a building.

### 3.2 The routed outline: the ruling's "arc"

`PlanRoute` walks the outline of three boxes per disc (2.2) as a twelve-sided polygon round the pad. For the 2x2 pad (R = 350 + 25 = 375 cm):
- **Boxes:** half-extents (375, 216.5), (306.2, 306.2) and (216.5, 375).
- **Graph nodes** (corners pushed 10 cm, `:262`):
  - (385, 226.5) and its mirrors, 446.7 cm from the centre;
  - (316.2, 316.2) and its mirrors, 447.2 cm;
  - twelve in all, none inside another of the three boxes.

  Every edge between neighbours, and every chord that skips a node (for example (385, 226.5) to (226.5, 385), with x + y = 611.5 > 2 x 305.2), stays outside the inset boxes.
- **The walked outline** is 385 cm at the middle of the four axis edges and up to 447 cm at the nodes. No walkable path comes nearer than R - 1.4 = **373.6 cm**, which is 23.6 cm outside `KeepOutCm`.
  - The drawn figure follows the path with its step clamped to `MaxSpeed x DeltaTime` (`:1171`), so at a 30-degree node it cuts by at most a frame's lag (under 2 cm). The 25 cm margin covers that, and the `clicktest` design's 1 cm tolerance is never needed.
- **Against the rings.** Ring 1 stands at 540 cm and ring 2 at 760 cm, so the outline stays at least 92.8 cm inside the first ring's slots and moves no slot. Today's box corners pass 116 cm inside.
- **A docked walker** (R = 150 + 25 + 25 = 200 cm): nodes 244.6-245.1 cm from it. A route is never nearer than 198.6 cm to where it was planned round it, so never nearer than 173.6 cm to where it is drawn, even after a drift of `DockDriftCm` before the re-check.
- **The exit** for a figure leaving the mast (`DiscExitCm` + 10) is 385-443 cm from the centre, beyond `KeepOutCm`.

**Why boxes and not waypoints on a circle.**
- An inscribed polygon on a circle of `KeepOutCm` dips inside it between waypoints: the `clicktest` design measured 1.3 cm at 10 degrees and 3.0 cm at 15.
- A hand-made arc would also have to be tested against every building box and re-routed when one is in the way. That is a second planner.
- The three boxes are `PlanRoute`'s own model, so it composes them with every footprint and finds the shortest way round both, or reports that it is boxed in (3.6).

### 3.3 `RefreshIdlePads` (`:380-497`): the keep-outs, and the slots and stand points they close

**(a) The keep-out list.** Insert after `const FRHBuildingInstance* Lander = ...;` (`:417`), before the pad loop (`:418`):
```cpp
	// P2 (ruling 28): the keep-out round every FINISHED surface Charging Pad, switched on or off (a leg from a ring slot to the Lander rank after
	// Switch OFF keeps off that pad too). KeepOutCm = the pad's drawn radius + KeepOutOverDrawnCm, never under PadHalf + RobotClearanceCm +
	// KeepOutOverClearCm. The drawn radius is the larger horizontal half-extent of the bounds of the model the colony visualizer draws for the pad,
	// fitted as it fits it (URHColonyVisualizerSubsystem::HandleBuildingAdded: the larger horizontal extent scaled to min(FootprintX, FootprintY)
	// x 2 m, centred on the pad, not turned): 198 cm x 400 / 396 = 200 cm for the 2x2 row, so KeepOutCm 350 cm. Read from the asset, once, never
	// from a spawned actor, so it never depends on the actor existing yet; with no model the visualizer draws the footprint's box (ScaleFor):
	// its larger half-axis. KeepOutSerial moves only when a keep-out really changes.
	TArray<FIdleKeepOut> NewKeepOuts;
	for (const FRHBuildingInstance& KB : Buildings)
	{
		if (KB.DefName != ChargePadName || KB.Level != 0 || KB.bUnderConstruction)
		{
			continue;
		}
		if (PadModelHalfCm < 0.f)
		{
			const UStaticMesh* PadModel = LoadObject<UStaticMesh>(nullptr, ChargePadModelPath);
			PadModelHalfCm = PadModel ? (float)FMath::Max(PadModel->GetBounds().BoxExtent.X, PadModel->GetBounds().BoxExtent.Y) : 0.f;
		}
		const FRHBuildingRow* KDef = Defs->GetBuilding(KB.DefName);
		const FVector2D KHalf = URHSimWorldSubsystem::GetFootprintHalfCm(KDef, KB.SizeX, KB.SizeY);
		const float FitCm = (float)FMath::Min(FMath::Max(1, KDef ? KDef->FootprintX : 1), FMath::Max(1, KDef ? KDef->FootprintY : 1)) * 200.f;
		FIdleKeepOut& KO = NewKeepOuts.AddDefaulted_GetRef();
		KO.PadId = KB.Id;
		KO.bOff = KB.bManualOff;
		KO.CentreCm = FVector2D(KB.LocationCm.X, KB.LocationCm.Y);
		KO.DrawnRadiusCm = PadModelHalfCm > 0.f
			? PadModelHalfCm * (FitCm / FMath::Max(2.f * PadModelHalfCm, 1.f))
			: (float)FMath::Max(KHalf.X, KHalf.Y);
		KO.KeepOutCm = FMath::Max(KO.DrawnRadiusCm + KeepOutOverDrawnCm, (float)FMath::Max(KHalf.X, KHalf.Y) + RobotClearanceCm + KeepOutOverClearCm);
	}
	bool bSameKeepOuts = NewKeepOuts.Num() == KeepOuts.Num();
	for (int32 k = 0; bSameKeepOuts && k < NewKeepOuts.Num(); ++k)
	{
		bSameKeepOuts = NewKeepOuts[k].PadId == KeepOuts[k].PadId && NewKeepOuts[k].CentreCm.Equals(KeepOuts[k].CentreCm, 0.5)
			&& FMath::IsNearlyEqual(NewKeepOuts[k].KeepOutCm, KeepOuts[k].KeepOutCm, 0.5f);
	}
	KeepOuts = MoveTemp(NewKeepOuts); // bOff and the drawn radius are refreshed either way (RH.IdleRobots reads them)
	if (!bSameKeepOuts)
	{
		++KeepOutSerial;
		RebuildKeepOutBoxes();
	}
```
Notes:
- **The fit.** `FitCm` is the colony visualizer's `TargetCm` (`:2518-2522`: `Fit = Footprint x 2`, `TargetCm = min(Fit) x 100`), and the scale is its `S` (`:2523`).
- **The load.** `LoadObject` runs at most once a session, and only once a finished pad exists. The colony visualizer has loaded that asset by then, so the call is a find. `PadModelHalfCm` is not reset on a load, because the asset does not change.
- **`rh.ModelSetV2 0`** draws the cube (half-extent 200 cm), and this reads the V2 asset (200 cm): the same 350 cm for the only pad row there is. That is disclosed in section 12.

**(b) In the pad loop**, after `P.RingCm = P.HalfCm + RobotClearanceCm + IdleRingOutCm;` (`:430`):
```cpp
		P.KeepOutCm = GetPadKeepOutCm(Pad.Id, &P.DrawnRadiusCm); // P2 (ruling 28)
		P.SlotKeptOutBy.Init(0, 20);
		P.SlotNearSlotOf.Init(0, 20);
```

**(c) In the slot loop**, between the `Obstacles` test (`:441-449`) and `P.Slots.Add(...)` (`:450`):
```cpp
			// P2 (ruling 28): nor inside another pad's keep-out (a leg to it could not keep off that pad). A pad's own keep-out never holds its own
			// slots: they stand at RingCm (540 cm) and beyond, its routed boxes reach at most 1.155 x 375 = 433 cm.
			P.SlotKeptOutBy[k] = InPadKeepOut(At, Pad.Id);
			bOpen = bOpen && P.SlotKeptOutBy[k] == 0;
```

**(d) In the stand-point loop**, after the `Obstacles` test (`:476-484`), before `if (bClear)` (`:485`):
```cpp
			// P2 (ruling 28): nor inside a pad's keep-out, this pad's own included (the walk there and back could not keep off it): such a building is
			// dropped and listed like one whose point falls in a footprint's box.
			bClear = bClear && InPadKeepOut(At, 0) == 0;
```
W3a's stand points are added in the same block (5.1).

### 3.4 New functions after `FIdlePad::NearDock` (`:499-513`), before `GatherDocked`

```cpp
void URHAgentVisualizerSubsystem::RebuildKeepOutBoxes()
{
	// P2 (ruling 28): three boxes per keep-out disc (AddDiscBoxes), routed KeepRouteMarginCm outside it; a docked robot's also DockDriftCm.
	KeepOutBoxes.Reset();
	for (const FIdleKeepOut& KO : KeepOuts)
	{
		AddDiscBoxes(KeepOutBoxes, KO.CentreCm, KO.KeepOutCm + KeepRouteMarginCm);
	}
	for (const FDockDisc& Disc : DockDiscs)
	{
		AddDiscBoxes(KeepOutBoxes, Disc.DrawnCm, DockKeepCm + KeepRouteMarginCm + DockDriftCm);
	}
}

int32 URHAgentVisualizerSubsystem::InPadKeepOut(const FVector2D& P, int32 ExceptPadId) const
{
	for (const FIdleKeepOut& KO : KeepOuts)
	{
		if (KO.PadId != ExceptPadId && InDiscBoxes(P, KO.CentreCm, KO.KeepOutCm + KeepRouteMarginCm))
		{
			return KO.PadId;
		}
	}
	return 0;
}

bool URHAgentVisualizerSubsystem::InDockDisc(const FVector2D& P) const
{
	for (const FDockDisc& Disc : DockDiscs)
	{
		if (InDiscBoxes(P, Disc.DrawnCm, DockKeepCm + KeepRouteMarginCm + DockDriftCm))
		{
			return true;
		}
	}
	return false;
}

bool URHAgentVisualizerSubsystem::AnyBoxBlocks(const FVector& A, const FVector& B) const
{
	// PlanRoute's own test of a straight leg, over every box now in Obstacles: one holding either end is the leg's own and does not block.
	const FVector2D A2(A.X, A.Y), B2(B.X, B.Y);
	for (const FBox2D& O : Obstacles)
	{
		if (!O.IsInside(A2) && !O.IsInside(B2) && SegmentHitsBox(A2, B2, O))
		{
			return true;
		}
	}
	return false;
}

float URHAgentVisualizerSubsystem::GetPadKeepOutCm(int32 PadId, float* OutDrawnRadiusCm) const
{
	for (const FIdleKeepOut& KO : KeepOuts)
	{
		if (KO.PadId == PadId)
		{
			if (OutDrawnRadiusCm) { *OutDrawnRadiusCm = KO.DrawnRadiusCm; }
			return KO.KeepOutCm;
		}
	}
	if (OutDrawnRadiusCm) { *OutDrawnRadiusCm = 0.f; }
	return 0.f;
}

int32 URHAgentVisualizerSubsystem::GetPadDockedDrawnCm(int32 PadId, TArray<FVector>& Out) const
{
	Out.Reset();
	for (const FDockDisc& Disc : DockNow)
	{
		if (Disc.PadId == PadId)
		{
			Out.Add(FVector(Disc.DrawnCm.X, Disc.DrawnCm.Y, 0.0));
		}
	}
	return Out.Num();
}
```

### 3.5 `GatherDocked` (`:515-544`), new body

Today's body (`:520-543`) resets each cached pad's `DockedCm`, returns with no cached pad, and adds the sim position of every robot docked at a cached pad. The new body keeps that exactly, and adds the drawn positions at every finished pad (`DockNow`) and the change test:
```cpp
void URHAgentVisualizerSubsystem::GatherDocked(const FMassEntityManager& EntityManager)
{
	// The robots docked at a cached pad: the sim's Charge task past its drive (Phase 1, at the pad, in its queue), at the sim position where
	// the sim stopped it (ArriveDistCm short of the pad's centre, on the side it came from). Read only; every tick, so a robot that docks or
	// leaves moves the waiting figures at once.
	for (TPair<int32, FIdlePad>& P : IdlePads)
	{
		P.Value.DockedCm.Reset();
	}
	DockNow.Reset();
	if (IdlePads.Num() > 0 || KeepOuts.Num() > 0)
	{
		for (int32 i = 0; i < Tracked.Num(); ++i)
		{
			if (!EntityManager.IsEntityValid(Tracked[i]))
			{
				continue;
			}
			const FRHTaskFragment* Task = EntityManager.GetFragmentDataPtr<FRHTaskFragment>(Tracked[i]);
			if (!Task || Task->TaskType != (uint8)ERHTaskType::Charge || Task->Phase == 0)
			{
				continue;
			}
			const FVector DockSimCm = EntityManager.GetFragmentDataChecked<FTransformFragment>(Tracked[i]).GetTransform().GetLocation();
			if (FIdlePad* Pad = IdlePads.Find(Task->DigDepositId))
			{
				Pad->DockedCm.Add(DockSimCm);
			}
			// P2 (ruling 28): a robot docked at any finished surface pad (on or off) is a disc every idle walk keeps DockKeepCm from, at its DRAWN
			// position on the last tick: a rover's drive position, a walker's drawn figure (kept in step on a road leg too), else its sim position.
			const int32 DockPadId = Task->DigDepositId;
			if (KeepOuts.ContainsByPredicate([DockPadId](const FIdleKeepOut& KO) { return KO.PadId == DockPadId; }))
			{
				FVector2D DockDrawn(DockSimCm.X, DockSimCm.Y);
				if (IsRoverIndex(i))
				{
					DockDrawn = RoverVis[i].Drive.bInit ? RoverVis[i].Drive.Pos : DockDrawn;
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
		}
	}
	// P2: the walks round them are checked again (KeepOutSerial) only when a robot docks or leaves, or a docked robot's drawn position has moved
	// more than DockDriftCm from where they were planned round it (it settles within about a second of docking).
	bool bSameDocks = DockNow.Num() == DockDiscs.Num();
	for (int32 k = 0; bSameDocks && k < DockNow.Num(); ++k)
	{
		bSameDocks = DockNow[k].PadId == DockDiscs[k].PadId && DockNow[k].Index == DockDiscs[k].Index
			&& FVector2D::Distance(DockNow[k].DrawnCm, DockDiscs[k].DrawnCm) <= DockDriftCm;
	}
	if (!bSameDocks)
	{
		DockDiscs = DockNow;
		++KeepOutSerial;
		RebuildKeepOutBoxes();
	}
}
```
- **Per-tick cost.** The existing loop, plus a scan of `KeepOuts` and one compare for each docked robot.
- **No allocation.** `DockNow` keeps its capacity across `Reset()`, so nothing allocates after the first docking, and `DockDiscs = DockNow` copies only on a change.
- **A docked rover** is drawn at the pad's centre (1.2), inside the pad's keep-out, so its disc never shapes a route. Its 5 m body is the known limit in 3.12.

### 3.6 `PlanKeptOff` (new, after `PlanRoute`, `:333`)

```cpp
void URHAgentVisualizerSubsystem::PlanKeptOff(const FVector& From, const FVector& To, TArray<FVector>& OutPath)
{
	// P2 (ruling 28): PlanRoute (unchanged) over the footprints and the keep-outs together: KeepOutBoxes are appended to Obstacles for this one
	// call. A box holding the goal is dropped by PlanRoute, as for any trip's own building: so the robot docking at a pad, and a figure whose job
	// site is the pad (or a docked robot it goes to), walk in.
	++KeptOffPlans;
	// A start inside a keep-out: the robot leaving the mast it charged at (exempt by the ruling), or a figure a robot docked beside. It walks
	// straight out first, along the ray from that disc's centre to 10 cm past its boxes (at most twice, for overlapping discs), unless a
	// footprint's box is in the way; then it is routed from there.
	FVector Start = From;
	TArray<FVector, TInlineAllocator<2>> Exits;
	for (int32 Step = 0; Step < 2; ++Step)
	{
		const FVector2D S2(Start.X, Start.Y);
		FVector2D DiscC = FVector2D::ZeroVector;
		double DiscR = 0.0;
		bool bHeld = false;
		for (const FIdleKeepOut& KO : KeepOuts)
		{
			if (!bHeld && InDiscBoxes(S2, KO.CentreCm, KO.KeepOutCm + KeepRouteMarginCm))
			{
				DiscC = KO.CentreCm;
				DiscR = KO.KeepOutCm + KeepRouteMarginCm;
				bHeld = true;
			}
		}
		for (const FDockDisc& Disc : DockDiscs)
		{
			if (!bHeld && InDiscBoxes(S2, Disc.DrawnCm, DockKeepCm + KeepRouteMarginCm + DockDriftCm))
			{
				DiscC = Disc.DrawnCm;
				DiscR = DockKeepCm + KeepRouteMarginCm + DockDriftCm;
				bHeld = true;
			}
		}
		if (!bHeld)
		{
			break;
		}
		FVector2D U = S2 - DiscC;
		if (U.IsNearlyZero())
		{
			U = FVector2D(To.X, To.Y) - DiscC; // drawn on the disc's centre (a robot that docked by road): out towards its goal
		}
		if (U.IsNearlyZero())
		{
			U = FVector2D(-1.0, 0.0);
		}
		U.Normalize();
		const FVector2D E2 = DiscC + U * (DiscExitCm(U, DiscR) + 10.0);
		const FVector E(E2.X, E2.Y, From.Z);
		// Only footprints are in Obstacles here: the exit point must not stand in one, and the straight walk out must not cross one.
		bool bExitInBox = false;
		for (const FBox2D& O : Obstacles)
		{
			bExitInBox = bExitInBox || O.IsInside(E2);
		}
		if (bExitInBox || AnyBoxBlocks(Start, E))
		{
			break;
		}
		Exits.Add(E);
		Start = E;
		++KeptOffExits;
	}
	const int32 Bare = Obstacles.Num();
	Obstacles.Append(KeepOutBoxes);
	PlanRoute(Start, To, OutPath);
	const bool bBoxedIn = OutPath.Num() == 2 && AnyBoxBlocks(Start, To); // PlanRoute's straight answer when no way round exists
	Obstacles.SetNum(Bare, EAllowShrinking::No);
	if (bBoxedIn)
	{
		// The keep-outs and the footprints close the goal in: today's route (the footprints only), counted for RH.IdleRobots.
		++KeptOffFallbacks;
		PlanRoute(From, To, OutPath);
		return;
	}
	// Developer evidence (RH.IdleRobots): the nearest the planned route comes to a pad's centre and to a docked robot, from its last exit on,
	// leaving out a disc that holds its start or its goal (those legs are exempt or walk in).
	const FVector2D Start2(Start.X, Start.Y), Goal2(To.X, To.Y);
	for (int32 k = 1; k < OutPath.Num(); ++k)
	{
		const FVector SegA(OutPath[k - 1].X, OutPath[k - 1].Y, 0.0), SegB(OutPath[k].X, OutPath[k].Y, 0.0);
		for (const FIdleKeepOut& KO : KeepOuts)
		{
			const double R = KO.KeepOutCm + KeepRouteMarginCm;
			if (!InDiscBoxes(Start2, KO.CentreCm, R) && !InDiscBoxes(Goal2, KO.CentreCm, R))
			{
				KeptOffNearestPadCm = FMath::Min(KeptOffNearestPadCm, (float)FMath::PointDistToSegment(FVector(KO.CentreCm.X, KO.CentreCm.Y, 0.0), SegA, SegB));
			}
		}
		for (const FDockDisc& Disc : DockDiscs)
		{
			const double R = DockKeepCm + KeepRouteMarginCm + DockDriftCm;
			if (!InDiscBoxes(Start2, Disc.DrawnCm, R) && !InDiscBoxes(Goal2, Disc.DrawnCm, R))
			{
				KeptOffNearestDockCm = FMath::Min(KeptOffNearestDockCm, (float)FMath::PointDistToSegment(FVector(Disc.DrawnCm.X, Disc.DrawnCm.Y, 0.0), SegA, SegB));
			}
		}
	}
	if (Exits.Num() > 0)
	{
		OutPath.Insert(From, 0);       // From, then each exit, then the route from the last exit (OutPath[0] was that exit)
		if (Exits.Num() == 2)
		{
			OutPath.Insert(Exits[0], 1);
		}
	}
}
```
Notes for the builder:
- **No allocation per frame.** It runs only when a walk is planned.
  - `Obstacles` keeps its capacity across `RebuildObstacles`' `Reset()` (`:199`), so after the first plan the append does not allocate.
  - `SetNum(..., EAllowShrinking::No)` keeps the capacity (`UE_5.8/Engine/Source/Runtime/Core/Public/Containers/Array.h:2531`; the enum is in `AllowShrinking.h:9`).
  - `Exits` is inline. `PlanRoute` allocates its graph as it does today.
- **Nothing else reads `Obstacles`** between the append and the truncation: there is one game thread, and `PlanRoute` is the only call between them.
- **The boxed-in test is exact.** `PlanRoute` returns two points only when the straight leg is clear of its candidates or no way round exists (`:252-256`, `:318-321`). Its candidates are every box meeting a region that holds the leg (`:240-251`), so a box outside it cannot block the leg either.
- **`OutPath.Insert(From, 0)`** is safe because `From` is never an element of `OutPath`: the callers pass `T.DrawCm` with `T.FreePath` or `T.Path`.
- **`FBox2D::IsInside` is strict** (`UE_5.8/Engine/Source/Runtime/Core/Public/Math/Box2D.h:353-356`), so a point on a box's edge is outside it, as `PlanRoute` assumes.

### 3.7 `KeptOffClear` (new, after `PlanKeptOff`): when a kept-off walk is checked again

```cpp
bool URHAgentVisualizerSubsystem::KeptOffClear(const TArray<FVector>& Path, float DoneCm) const
{
	// P2 (ruling 28): what is left of a kept-off path, from its point at arc length DoneCm: true while none of it passes inside a keep-out box
	// (one holding where the figure is now, or the path's end, excepted, as PlanRoute excepts them). Read once per KeepOutSerial change.
	if (Path.Num() < 2)
	{
		return true;
	}
	const FVector NowCm = PointAlong(Path, DoneCm);
	const FVector2D Now2(NowCm.X, NowCm.Y), End2(Path.Last().X, Path.Last().Y);
	int32 k = 1;
	for (float Acc = 0.f; k < Path.Num(); ++k)
	{
		const float Seg = (float)FVector::Dist2D(Path[k - 1], Path[k]);
		if (Acc + Seg >= DoneCm)
		{
			break;
		}
		Acc += Seg;
	}
	FVector2D From2 = Now2;
	for (; k < Path.Num(); ++k)
	{
		const FVector2D To2(Path[k].X, Path[k].Y);
		for (const FBox2D& Box : KeepOutBoxes)
		{
			if (!Box.IsInside(Now2) && !Box.IsInside(End2) && SegmentHitsBox(From2, To2, Box))
			{
				return false;
			}
		}
		From2 = To2;
	}
	return true;
}
```

### 3.8 `AdvanceDrawn` (`:925-1174`): the exact edits

**(a) The job's arrival** (`:953-958`). Replace with:
```cpp
	if (T.IdleS <= ParkAfterIdleS && T.IdleTick != 0)
	{
		// A job arrives (or the sim moves it): the slot is freed and the wait and any inspection end; the branches below take over
		// from wherever the figure stands. P2 (ruling 28): a task that arrives while the figure is drawn in a pad's round (a slot held, drawn idle
		// on this tick or the last) is "a job walk that starts from a ring slot": the task's walks keep off the pads until it ends.
		T.bKeepOff = bTask && T.IdlePadId != 0 && T.IdleSlot != INDEX_NONE && T.IdleTick + 1 >= TickSerial;
		ResetIdle(T);
	}
	if (!bTask)
	{
		T.bKeepOff = false; // the task is over (or there was none)
	}
```
The `IdleTick + 1 >= TickSerial` test leaves out a stale idle state. A walker that drove a road leg (`bDrive`) was not drawn by `AdvanceDrawn` meanwhile, so its `IdleTick` is old, and the task it holds after the road leg did not start from a ring slot.

**(b) `FreeWalkTo`** (`:959-971`). Replace with:
```cpp
	auto FreeWalkTo = [&](const FVector& Goal, bool bKeptOff)
	{
		// P2 (ruling 28): a kept-off walk under way when the keep-outs changed (a robot docked or left, a docked robot settled, a pad's keep-out
		// changed) is checked once per change, and planned again from where the figure stands only if what is left of it now passes inside one.
		if (bKeptOff && T.bFree && T.bFreeKeptOff && T.KeepOffSerial != KeepOutSerial)
		{
			T.KeepOffSerial = KeepOutSerial;
			if (T.FreeDoneCm < T.FreeLenCm && !KeptOffClear(T.FreePath, T.FreeDoneCm))
			{
				T.bFree = false;
			}
		}
		// Planned when the goal moves more than 100 cm (unchanged), when it was planned the other way (kept off or not), or as above.
		if (!T.bFree || FVector::Dist2D(Goal, T.FreeGoalCm) > 100.f || bKeptOff != T.bFreeKeptOff)
		{
			T.bFree = true;
			T.FreeGoalCm = Goal;
			T.bFreeKeptOff = bKeptOff;
			if (bKeptOff)
			{
				PlanKeptOff(T.DrawCm, Goal, T.FreePath);
				T.KeepOffSerial = KeepOutSerial;
			}
			else
			{
				PlanRoute(T.DrawCm, Goal, T.FreePath);
			}
			T.FreeLenCm = PathLength(T.FreePath);
			T.FreeDoneCm = 0.f;
		}
		T.FreeDoneCm = FMath::Min(T.FreeLenCm, T.FreeDoneCm + FreeWalkCmS * Pace * DeltaTime);
		return PointAlong(T.FreePath, T.FreeDoneCm);
	};
```
- **When it re-plans.** The 100 cm rule is kept as it is. A standing figure (walk done) is never re-planned by a keep-out change.
- **Cost.** The serial compare runs per frame. `KeptOffClear` runs at most once per walking figure per change.
- **The rank.** Round 2's rule holds: a rank figure is re-planned only by the 100 cm rule, because every idle walk is kept off, so its flag never flips.

**(c) The off-road trip** (`:1037-1053`). Replace with:
```cpp
				// P2 (ruling 28): a kept-off trip is checked once per keep-out change, and planned again from where the figure is drawn only if what
				// is left of it passes inside a keep-out (planning again restarts the eased progress from there, as a target change does).
				if (T.bTrip && !T.bRoadTrip && T.bKeepOff && T.KeepOffSerial != KeepOutSerial)
				{
					T.KeepOffSerial = KeepOutSerial;
					if (!KeptOffClear(T.Path, T.TripDoneCm))
					{
						T.bTrip = false;
					}
				}
				if (!T.bTrip || T.bRoadTrip || FVector::Dist2D(Target, T.TripTargetCm) > 150.f)
				{
					T.bTrip = true;
					T.bFree = false;
					T.bRoadTrip = false;
					T.TripTargetCm = Target;
					T.SimStartDistCm = FMath::Max((float)FVector::Dist2D(SimPos, Target), 1.f);
					// Ruling 28: a job walk that starts from a ring slot keeps off the pads; the target's own pad (the Charge task's) or a docked robot
					// it goes to holds the trip's end, so PlanRoute drops that disc and the figure walks in.
					if (T.bKeepOff)
					{
						PlanKeptOff(T.DrawCm, Target, T.Path);
						T.KeepOffSerial = KeepOutSerial;
					}
					else
					{
						PlanRoute(T.DrawCm, Target, T.Path);
					}
					T.PathLenCm = PathLength(T.Path);
				}
				const float S = FMath::Clamp(1.f - (float)FVector::Dist2D(SimPos, T.TripTargetCm) / T.SimStartDistCm, 0.f, 1.f);
				// Ease in and out: slower off the mark and into the stop, quicker in
				// the middle - the same arrival time.
				const float Eased = S - 0.22f * FMath::Sin(2.f * PI * S) / (2.f * PI);
				T.TripDoneCm = Eased * T.PathLenCm;
				Desired = PointAlong(T.Path, T.TripDoneCm);
				T.LastSyncedCm = Desired;
```
`bKeepOff` lasts for the task. A haul's second leg (pickup to drop) is planned kept-off too. That is harmless: it only keeps off pads, and a leg whose target is in a keep-out walks in.

**(d) The walk over when a task begins where the robot stands** (`:1072`): `Desired = FreeWalkTo(SimPos, T.bKeepOff);`

**(e) The idle branch.** Section 4.4 has the exact edits at `:1090-1095` and `:1169`. Every idle leg is planned kept off: `FreeWalkTo(T.IdleGoalCm, true)`.

### 3.9 `ParkSlotCm` (`:335-378`)

After the `Obstacles` loop (`:364-371`), before `if (bFree)` (`:372`), insert:
```cpp
		// P2 (ruling 28): nor inside a pad's keep-out (a leg from a ring slot to the rank could not keep off that pad).
		if (bFree && InPadKeepOut(P, 0) != 0)
		{
			bFree = false;
		}
```
- **One rank.** `ParkSlotCm` stays the one function that `GetIdleDebug` (`:658`) and `ChooseIdlePad` (`:578`) both call, so the rank a figure walks to and the `RankCm` the click test reads stay one.
- **When it moves a slot.** Only when a pad stands within about 4.3 m of a rank slot.
  - Not on `idle_day`: the pads are at (10, 20) and (26, -4) m, and the rank at x = -7.5 m.
  - Not in the click scene: setup places the harness pad at least 920 cm from every rank slot (`RHClickTest.cpp:3663-3684`), and the far pads stand 120 m+ out.

### 3.10 Exposure for the click step

- **`FIdleDebug::KeepOutCm` and `DrawnRadiusCm`**, filled in `GetIdleDebug`'s pad block beside `Out.RingCm = Pad->RingCm;` (`:681`) (section 7). This is the field the `clicktest` design's C4 reads; it is 350 cm here.
- **`GetPadKeepOutCm(PadId, &Drawn)`:** the pad's keep-out whether or not a figure stands there. A switched-off pad keeps it.
- **`GetPadDockedDrawnCm(PadId, Out)`:** the docked robots' drawn positions. `RH.IdleRobots` prints them. The `clicktest` design does not use them, because no robot docks at the harness pad during the step (`clicktest.md` 5.4).

### 3.11 Proof, mutation, captures

- **Check: the `clicktest` design's C4.** The floor of `robots.idle` becomes `KeepOutCm` = 350 cm.
  - In the main scene robots 4 and 5's legs to slots 4 and 5 have straight lines 131 and 174 cm from the centre (`idle_r2_click1.log:1103-1104`), so they must go round.
  - Under W1 they follow the outline at 385-447 cm, never under 373.6 cm, and the step's line reports "nearest" at about 385 cm.
  - The other legs are unaffected. Robot 6's straight line passes 403 cm from the centre at (24, 401) cm off it, outside all three boxes, and robot 3's 440 cm (`:1102`, `:1105`).
- **Mutation `p2_mut_w1` (= the clicktest design's `p2_mut_c4`).** Delete the one line `Obstacles.Append(KeepOutBoxes);` in `PlanKeptOff`.
  - Every kept-off walk is then planned by `PlanRoute` over the footprints alone, as at HEAD. The exits still run, but no figure starts inside a keep-out in the main scene (`clicktest.md` 1.10), and `KeepOutCm` is still computed, so C4's agreement check passes.
  - Expected: `CLICKTEST robots.idle FAIL robot 5 came 315 cm from the pad's centre (never under the floor, 350 cm, ...)` (round 2 measured 315 and 334 cm on exactly these routes), with every other check passing: 114/1.
- **The 150 cm from a docked robot, and the job walk from a ring slot: captures.** The step holds the fleet (`RHClickTest.cpp:3715`), so no job starts from a ring slot there (`clicktest.md` 5.6). Ruling 28 puts these on the captures. Re-run the idle gate's own `cap_day.cmds` and `cap_night.cmds` (`wip-backups/idle-work/cap/`) before and after:
  - `_day_00` to `_day_06`: pad #6 with its docked robot at (10.5, 17.3) m (sim) and the ring;
  - `_day_09` to `_day_12`: the fabricator leaving its slot for the Stockpile site at (-4, 12) m, and inspection walks past the pad.

  After: no walking figure on the disc or within 150 cm of the docked robot.
- **The keep-out line.** `RH.IdleRobots`' dumps at 30, 60, 100, 135 and 152 s print it (section 7): "the plans came no nearer than N cm to a pad's centre and M cm to a docked robot".
  - After, expect N >= 373 and M >= 198.
  - Under `p2_mut_w1`, N reads about 300 cm (the plain route along the pad's box). A `cap_day` run on that build is optional, since the click mutation already shows the removal fails.
- **The robot leaving the mast.** On `idle_day`, robots 4 and 7 (sim Ids) stand at (11, 17) m (`idle_r2_after_day.log:365`, `:368`), 316 cm from pad #6's centre. One is the docked robot; the other went idle on the pad. Its walk to a slot starts with the exit, and the line counts it: "N start exit(s)".

### 3.12 Frame cost, interactions, risk

**Frame cost:**
- Per tick: `GatherDocked`'s existing loop, plus, per docked robot, a scan of `KeepOuts` and one compare. `RefreshIdlePads`' per-tick part is unchanged (and unchanged by the `cache` design's key).
- Per idle figure per frame: two compares in `FreeWalkTo`. Per kept-off trip: one compare and one float store.
- Per plan (a goal change, a kept-off flag change, or a keep-out change that blocks the rest of a walk): `PlanKeptOff` adds 3 boxes per pad within about 30 m of the leg (`PlanRoute`'s region, `:240-251`) and 3 per docked robot.
  - A leg that passes a pad gains up to 12 graph nodes, or 24 with a docked robot, roughly 1.3-2x a plain plan's edge tests.
  - Legs that pass no pad cost what they cost today: the boxes miss the region or the straight test.
  - Plans happen a few times a minute per figure (the inspection round), as today.
- Per keep-out change: `RebuildKeepOutBoxes` (no allocation after the first), and one `KeptOffClear` per walking kept-off figure.
- Per cache rebuild: a `KeepOuts` pass over the buildings, plus the slot and stand-point keep-out tests (a few pads each).
- The frame check's tick median should not move. The idle gate's windows read 0.04-0.05 ms, and plans are rare events, not per-frame work.

**Interactions:**
- **Slot holding:** unchanged. Keep-outs route walks, close a slot inside another pad's keep-out at rebuild, and (W3a) skip stand points. `ChooseIdlePad`'s keep path (`:588`) re-chooses a figure whose slot closed.
- **`ResetTracking`** (load): section 8 empties every new list. The first tick rebuilds them: the keep-outs with the pad cache, and the docked discs in `GatherDocked`. Both bump `KeepOutSerial` when anything is there.
- **The docked robot:**
  - Its own approach is a Charge trip. If the charge arrived while it waited in the ring, that is a kept-off trip whose target, the pad's centre, holds its pad's three boxes, so it walks in, as today.
  - While docked it is a disc for everyone else.
  - `NearDock` (500 cm from its sim position) still keeps waiting figures' slots away from it, unchanged.
  - When it leaves after charging, its first idle leg starts with the exit.
- **The Lander rank:** a rank leg is planned kept off. After Switch OFF the switched-off pad is still a keep-out. A rank slot inside a pad's keep-out moves to the next free rank position (3.9).
- **Deep, drive and rover figures: untouched.**
  - `PlanKeptOff` is called only from `AdvanceDrawn`.
  - `AdvanceRover` (`:774-923`), the `bDrive` branch of `Tick` (`:1730-1745`) and the `bDeepDraw` branch (`:1724-1729`) are not edited.
  - `GatherDocked` reads a docked robot's `Trips[i].DrawCm` or `RoverVis[i].Drive.Pos`, and never writes them.
  - A deep figure is reset on return by the snap (`Trips[i].bInit = false`, `:1727`), which clears every new field.

**Known limits (disclose):**
- A job walk from a ring slot that the sim drives by road is drawn by `AdvanceRover` (the drive branch), which this design keeps untouched as instructed. Its first off-road leg keeps `PlanRoute`'s box clearance (about 300 cm), not the keep-out. A road trip needs a built road, and `idle_day` has none: no road line in its capture log.
- A docked cargo rover is drawn at the pad's centre (`AdvanceRover` `:876-879`) with a 5 m body. The keep-out keeps walkers at least 373.6 - 250 = 123.6 cm from its nose and tail, under 150 along its axis. A rover docks at a ChargePad only when the pad is road-served (`RHSimRovers.cpp:50`).
- A job site inside a keep-out that is not the pad (a small building touching the pad) holds the trip's end, so that trip walks in through the keep-out. The ruling exempts only the pad.

**Risk: medium.**
- The routing is a new input to an unchanged planner, so a wrong box set shows as a route, not a crash.
- Every degenerate case has a defined, counted fallback:
  - boxed in: today's route;
  - an exit that would end in or cross a building's box: planned from inside, with `PlanRoute` dropping the boxes that hold the start, so the leaving robot may cross that disc as it does today.
- **No trap and no oscillation.**
  - A walk is planned only on a goal change (100 cm), a kept-off flag change, or a keep-out change that makes the rest of it pass inside a keep-out.
  - Each keep-out change is looked at once per figure (`KeepOffSerial`).
  - A docked robot settles within about a second (a few `DockDriftCm` steps at most).
  - A figure inside a keep-out either walks straight out (and is then outside, so it never exits again) or is planned from where it stands.
- **The outline against the ring.** Walkers now pass between the pad and the ring at 385-447 cm, at least 93 cm inside ring 1's slots. Today's box corners pass 116 cm inside.
- **Dropped stand points.** A stand point inside a keep-out drops that building from the pad's inspection list. On `idle_day` both pads list "0 in range dropped" today, and their inspected stand points are 10 m or more out (#15's at (10.1, 9.9) m, `idle_r2_after_day.log:428`). The builder confirms "0 in range dropped" in the after capture's `RH.IdleRobots`. In the click scene the Lander's stand point is 580 cm from the harness pad's centre, outside.

---

## 4. W2: an idle goal within reach of the sim position (GO)

### 4.1 The bound, and why 5000 cm

- **The snap.** It fires when the drawn figure is more than 8000 cm from its sim position (`:932`).
- **What a 5000 cm bound on the pad's centre allows** (`IdleReachCm` = 5000):
  - ring slots within 5000 + 760 = 5760 cm;
  - inspection stand points within 5000 + 2000 = 7000 cm (a building is inspected when its centre is within `InspectRadiusCm` = 2000 cm of the pad's, `:460`, and the nearest point of its box is no farther than its centre; W3a keeps its extra points within 2000 cm too, 5.1);
  - the keep-out exits and the outline round a pad stay inside its ring 2.

  That leaves at least 1000 cm under the snap for a walk's detours round buildings near a stand point.
- **The alternatives.** 6000 cm would put stand points at up to 8000 cm, with no margin. 4500 cm would turn more far robots away for no gain: a 45-50 m walk to the ring takes about 22 s at 1x (`FreeWalkCmS x 1.5`), which is fine.
- **The guard behind it: `IdleLeashCm` = 7500 cm, checked per frame** (one `Dist2D`, no planning).
  - An idle walk never steps the figure to a point more than 7500 cm from its sim position.
  - At the leash the walk holds: this frame's progress is taken back and the figure stands until its goal changes.
  - Only a detour bulging 5 m or more past a stand point already 70 m out can reach it.
  - So no idle walk can reach the snap, whatever the layout. The loop is gone by construction, not only by arithmetic. `RH.IdleRobots` counts held frames.
- **The Lander rank** is bounded the same way: its slot `ParkSlotCm(i)` must be within 5000 cm. Otherwise the figure stays put, standing where it is drawn.
- **What changes on screen.** A robot idle more than 50 m from its nearest working pad no longer walks to it; today it walks there and, beyond about 80 m, loops.
  - On `idle_day` every idle robot's sim position is within 25 m of a pad (`[RH.Fleet]`, `idle_r2_after_day.log:365-371`), so nothing changes there.
  - In the click scene every robot is about 15-20 m from the harness pad (1.6).

### 4.2 `ChooseIdlePad` (`:565-647`): the exact edits

After the `FindNearestChargePad` block (`:580-586`), insert:
```cpp
	// P2 W2: only a pad whose centre is within IdleReachCm of the sim position (its ring within IdleReachCm + 760 cm, its stand points within
	// IdleReachCm + InspectRadiusCm). AdvanceDrawn snaps a figure drawn more than 8000 cm from its sim position back to it, so a walk to a pad
	// about 80 m away looped (walk, snap back, wait ParkAfterIdleS, walk again). FindNearestChargePad returns the nearest pad: if it is out of
	// reach, every pad is. (The Lander rank is bounded below.)
	if (PadId != 0 && FVector::Dist2D(SimPos, PadCm) > IdleReachCm)
	{
		PadId = 0;
	}
```
`PadCm` is the pad's `LocationCm` (`RHSimWorldSubsystem.cpp:13409`). When `FindNearestChargePad` returned the Lander (no working pad), `PadId` is already 0 from `:583-586`.

After `const int32 OldSlot = T.IdleSlot;` (`:606`), add:
```cpp
	const bool bWasStayPut = T.bStayPut;
```
Replace the no-pad return (`:614-621`):
```cpp
	if (!Pad)
	{
		// P2 W2: the Lander rank only within IdleReachCm of the sim position too; else the figure stays where it stands (no walk).
		T.bStayPut = FVector::Dist2D(SimPos, T.IdleRankCm) > IdleReachCm;
		if ((OldPadId != 0 && OldSlot != INDEX_NONE) || T.bStayPut != bWasStayPut)
		{
			T.bFree = false; // from a ring slot (or an inspection) to the rank, or between the rank and staying put: a new walk (or none)
		}
		return;
	}
```
Replace the end (`:640-646`):
```cpp
	T.bRingFull = T.IdleSlot == INDEX_NONE;
	// P2 W2: a full ring sends the figure to the rank only within reach too.
	T.bStayPut = T.bRingFull && FVector::Dist2D(SimPos, T.IdleRankCm) > IdleReachCm;
	// A slot (new, or this pad again after the rank) is a new walk. A full ring again sends the figure to the rank it was already walking
	// to, and a full ring after a slot elsewhere is a walk from that slot to the rank: new only in the second case (or between the rank and
	// staying put).
	if (T.IdleSlot != INDEX_NONE ? (PadId != OldPadId || T.IdleSlot != OldSlot) : ((OldPadId != 0 && OldSlot != INDEX_NONE) || T.bStayPut != bWasStayPut))
	{
		T.bFree = false;
	}
```
- **Stable between choices.** The keep path (same pad and slot, `:588-597`) returns before any of this, so a figure holding a slot keeps `bStayPut` false. A choice that comes out the same keeps its walk, which is round 2's rule. The sim position does not change during an idle spell, so the result is stable between choices.
- **Under the `cache` item** a choice is made again only on a key change, a new spell or a `MakesWay`. Stay-put needs no look once a second (`cache.md` 10).

`ResetIdle` (`:546-563`): add `T.bStayPut = false;` and `T.InspectStand = INDEX_NONE;` before its closing brace. It does **not** touch `bKeepOff`: 3.8 (a) sets it just before `ResetIdle`.

### 4.3 What the figure shows

- **`EIdleShow::StayPut` (2.3).**
- **`GetIdleDebug`** needs no special case: on an idle tick `Out.Show` is `(EIdleShow)T.IdleShow` (`:663`), `GoalCm` is where it stands, and the pad block is skipped (no slot).
- **The route log line** (`:2038-2041`) is **not** changed. The frame analyzer parses it with a fixed pattern (`wip-backups/idle-work/r2/frame2/analyze.py:15`: `idle: (\d+) waiting at a pad, (\d+) at the Lander rank, (\d+) inspecting; tick median ...`). So a figure staying put is counted in none of the three (disclose). `RH.IdleRobots` lists it.

### 4.4 The idle branch (`:1090-1095` and `:1169`)

Replace `:1090-1095`:
```cpp
		const FIdlePad* Pad = (T.IdlePadId != 0 && T.IdleSlot != INDEX_NONE) ? IdlePads.Find(T.IdlePadId) : nullptr;
		bool bStand = false;
		if (!Pad && T.bStayPut)
		{
			// P2 W2: no working pad, and not its Lander rank slot, within IdleReachCm of its sim position: it stands where it is drawn.
			T.IdleShow = (uint8)EIdleShow::StayPut;
			T.IdleGoalCm = T.DrawCm;
			bStand = true;
		}
		else if (!Pad)
		{
			T.IdleShow = (uint8)EIdleShow::LanderRank;
			T.IdleGoalCm = T.IdleRankCm;
		}
```
The `else { ... }` pad block follows, unchanged apart from 5.3. Then replace `Desired = FreeWalkTo(T.IdleGoalCm);` (`:1169`) with:
```cpp
		// P2 (ruling 28): every idle leg (to its slot, between slots, out to a building and back, to the rank) is planned round the keep-outs.
		if (bStand)
		{
			Desired = T.DrawCm;
		}
		else
		{
			Desired = FreeWalkTo(T.IdleGoalCm, true);
			// P2 W2: an idle walk never takes the figure more than IdleLeashCm from its sim position (past 8000 cm AdvanceDrawn would snap it back
			// and the walk would loop). At the leash the walk holds: this frame's progress is taken back and the figure stands until its goal
			// changes (only a detour of 5 m or more past a stand point already 70 m out can reach it).
			if (FVector::Dist2D(Desired, SimPos) > IdleLeashCm)
			{
				T.FreeDoneCm = FMath::Max(0.f, T.FreeDoneCm - FreeWalkCmS * Pace * DeltaTime);
				Desired = T.DrawCm;
				++IdleLeashFrames;
			}
		}
```
At the leash the figure stays at `DrawCm`, which never stepped past the leash, and it never cuts a chord back to its path later.

### 4.5 Proof, mutation, captures

- **Check, in `robots.idle`.** It merges with the `clicktest` design, adds no step, and its full hunk is in section 9.2.
  - Setup places two far Charging Pads by harness call (`Debug_PlaceInstant`), on a 10 m grid 120-250 m out from the Lander.
  - The first is more than 12000 cm from the harness pad and more than 7000 cm from every tracked robot's sim position (`FIdleDebug::SimCm`). The watched robot is drawn waiting at the harness pad, so under W2 its sim position is within 5000 cm of it, and therefore more than 7000 cm from the far pad whatever it did after setup.
  - While the harness pad is on, every robot has it nearer. Switch OFF (`:4063-4064`) turns the harness pad off, so the far pad becomes every robot's nearest working pad.
  - The existing goal check (`:4038-4040`: `Show == LanderRank && GoalCm == RankCm`) then holds only if the reach bound turned the far pad away.
  - In the main scene the first spot that passes is (-120, -120) m: 170 m from the harness pad and about 164 m from the robots; Ore_A at (-160, -150) m is a dig site, not a building.
- **Mutation `p2_mut_w2`.** Delete the `if (PadId != 0 && FVector::Dist2D(SimPos, PadCm) > IdleReachCm) { PadId = 0; }` block.
  - Expected: `CLICKTEST robots.idle FAIL after Switch OFF the pad is off and robot 0 is drawn 2 at pad #<far> with its goal (...) m (rank slot (...) m, ring slot (...) m);`.
  - Every other check passes: 114/1.
  - Switch ON brings the figure back to the harness pad, which is nearer again.
  - At 1x for the 15 s check the robots walk at most about 34 m toward the far pad, so no snap happens inside the window.
- **Capture `cap_far.cmds`** (new; before and after, the same file; also W3b's evidence):
  ```
  r.setres 1440x860w, RH.ClearTips, RH.After 2 RH.Load idle_day, RH.After 2.5 RH.SetSpeed 1, RH.After 3 RH.Floor 0, RH.After 3.5 rh.RobotRouteLog 1, RH.After 5 RH.Spawn ChargePad 110 20, RH.After 5.5 RH.Spawn ChargePad 120.626 21.932, RH.After 8 RH.IdleRobots, RH.After 8 RH.Fleet, RH.After 10 RH.Power 6 0, RH.After 10 RH.Power 26 0, RH.After 11 RH.Cam -6 2 0.08 0 0, RH.After 40 RH.IdleRobots, RH.After 41 RH.Snapshot 0.5, RH.After 44 RH.Cam 112 21 0.12 0 0, RH.After 46 RH.Snapshot 0.5, RH.After 49 RH.Cam -6 2 0.08 0 0, RH.After 75 RH.IdleRobots, RH.After 76 RH.Snapshot 0.5, RH.After 79 RH.Power 6 1, RH.After 79 RH.Power 26 1, RH.After 81 RH.Status
  ```
  - **The cheats.**
    - `RH.Spawn <DefName> [Xm] [Ym]` places a finished building with `Debug_PlaceInstant` and no placement test (`RHCheats.cpp:39-54`).
    - `RH.Power <BuildingId> <0|1>` (`RHCheats.cpp:1838`) is the cheat `cap_day.cmds` already uses.
  - **The scene.**
    - With #6 and #26 off, the idle robots' nearest working pad is the one at (110, 20) m, 94-99 m from their sim positions (11-16, 8-22) m.
    - The second pad is 2 x 5.4 m further out along the ray from the Lander through it. The Lander is at (0, 0): the rank's logged positions (-7.5, -1.1) .. (-7.5, 5.5) put it there (`ParkSlotCm`, `:361-362`).
  - **Before (HEAD).** `RH.IdleRobots` at 40 s shows them "waiting at pad #<far> slot k (walking to it, ...)". The route log's "drawn-sim gap max" climbs past 7000 cm, and at about 46 s they snap back. At 75 s they are walking out again: snap at 80 m, then 6 s, then again.
  - **After.** "#k Lander rank (no working pad within 50 m of its sim position)". The gap max stays near the rank distance (about 2000-3000 cm). `_far_00` and `_far_02` show them in the rank and `_far_01` shows the far pads empty.
  - **Files:** `Saved/RHCapture/p2/p2_before_far_00..02.png`, `p2_after_far_00..02.png`, and the `RH.IdleRobots` dumps from the log.
  - **Stay put.** A robot idle out at a far dig site (robot #2 was "busy, drawn at (124.3, 123.3) m" on `idle_day`) shows the stay-put line there, if it goes idle in the window.

### 4.6 Frame cost

Per choice: two `Dist2D`. Per idle figure per frame: one `Dist2D` (the leash). Nothing allocates.

### 4.7 Interactions

- **Slot holding:** a figure turned away holds no slot (`IdleSlot` stays `INDEX_NONE`), so it frees nothing and blocks nothing.
- **`ResetTracking`:** `Trips.Reset()` clears `bStayPut` with everything else.
- **The docked robot and `MakesWay`:** unchanged; they act only on figures with a slot.
- **The Lander rank:** bounded. `ParkSlotCm` is unchanged apart from 3.9.
- **The snap itself:** unchanged; it still catches loads and teleports.
- **Deep, drive and rover figures:** untouched. The idle branch already excludes rovers (`:1076`).

### 4.8 Risk

Low. The visible change is limited to robots idle 50-80 m from their nearest pad, which now take the rank or stand. Those beyond 80 m stop looping. A figure staying put stands wherever it was drawn when the choice was made: where its last job left it, or where a pad switched off left it.

**One check to confirm in the dev run.** `robots.idle`'s Switch OFF goal check wants the watched robot at its Lander rank slot. Under W2, a robot whose sim position is more than 50 m from its rank slot stays put instead (`Show` 5), and the check would fail. The click scene's robots stand about 7.5 m from the rank, so this is not expected. If the dev run shows it, report the watched robot's `SimCm` and stop: do not widen the check.

---

## 5. W3a: stand points held the way slots are held (GO)

### 5.1 The stand points, built with the cache

In `RefreshIdlePads`' stand-point block, replace the `if (bClear) { ... }` body (`:485-490`) with:
```cpp
			if (bClear)
			{
				P.Inspect.Add(B.Id);
				P.InspectAtCm.Add(FVector(At.X, At.Y, 0.0));
				P.InspectFaceCm.Add(FVector(BC.X, BC.Y, 0.0));
				// P2 W3: the building's stand points: the nearest the pad first (InspectAtCm), then InspectSpreadCm and twice that either way along
				// its clearance box (round a corner where an edge ends), each kept only where the first may stand (outside every footprint's box and
				// every pad's keep-out) and within InspectRadiusCm of this pad's centre (so W2's reach holds).
				P.StandStart.Add(P.StandCm.Num());
				P.StandCm.Add(FVector(At.X, At.Y, 0.0));
				const double S0 = OutlineS(Box, At);
				for (const double Steps : { 1.0, -1.0, 2.0, -2.0 })
				{
					const FVector2D Q = OutlineAt(Box, S0 + Steps * InspectSpreadCm);
					bool bQ = FVector2D::Distance(Q, C) <= InspectRadiusCm && InPadKeepOut(Q, 0) == 0;
					for (int32 o = 0; bQ && o < Obstacles.Num(); ++o)
					{
						bQ = !Obstacles[o].IsInside(Q);
					}
					if (bQ)
					{
						P.StandCm.Add(FVector(Q.X, Q.Y, 0.0));
					}
				}
			}
```
After the stand-point loop ends (`:495`), before the pad loop's closing brace (`:496`), add:
```cpp
		P.StandStart.Add(P.StandCm.Num()); // P2 W3: the end of the last building's stand points
```
- **Which buildings are inspected** is unchanged apart from W1's keep-out test: the first point decides, as today (`InspectDropped`).
- **`OutlineS` is exact.** `At` lies on the box's outline: the pad's centre is never inside another building's clearance box, as the comment at `:473-474` says.
- **A point on the building's own box is not "inside" it**, as today, because `FBox2D::IsInside` is strict.
- **The points never wrap onto each other.** The smallest clearance box (a 1x1 building: 380 cm square) has a 1520 cm outline.

### 5.2 `FreeStand` (new, after `ChooseIdlePad`)

```cpp
int32 URHAgentVisualizerSubsystem::FreeStand(const FIdlePad& Pad, int32 Building, int32 Self) const
{
	// P2 W3: the first of a building's stand points that no other figure drawn idle on this tick or the last heads for or stands at (IdlePhase
	// 1 or 2, its InspectAtCm within IdleApartCm: a figure of any pad, so two pads' figures never share one either) and, ruling 28, none inside a
	// docked robot's disc. INDEX_NONE: none free. Held the way ChooseIdlePad holds slots (IdleTick).
	for (int32 s = Pad.StandStart[Building]; s < Pad.StandStart[Building + 1]; ++s)
	{
		const FVector& Cand = Pad.StandCm[s];
		bool bFree = !InDockDisc(FVector2D(Cand.X, Cand.Y));
		for (int32 j = 0; bFree && j < Trips.Num(); ++j)
		{
			const FRobotTrip& O = Trips[j];
			bFree = j == Self || O.IdleTick == 0 || O.IdleTick + 1 < TickSerial || (O.IdlePhase != 1 && O.IdlePhase != 2)
				|| FVector::Dist2D(O.InspectAtCm, Cand) >= IdleApartCm;
		}
		if (bFree)
		{
			return s;
		}
	}
	return INDEX_NONE;
}
```
**Two figures picking on one tick.** A lower index is drawn first, so a higher index sees its `IdlePhase` 1 and its `InspectAtCm` the same tick. A higher index drawn last tick is still held (`IdleTick + 1 >= TickSerial`).

### 5.3 The pick, and a stand point that is gone

Replace the pick (`:1125-1134`) with:
```cpp
					if (T.IdleWaitS >= T.IdleWaitForS && Pad->Inspect.Num() > 0)
					{
						// The building: keyed on the robot's index and how many it has inspected, never a random draw. P2 W3: at that building's first free
						// stand point (FreeStand); a building with none free gives way to the next in the list, and with none free anywhere the wait starts
						// again. With nobody else inspecting this is today's pick: building (i + InspectCount) % N, its stand point 0 (InspectAtCm).
						const int32 N = Pad->Inspect.Num();
						for (int32 Try = 0; Try < N && T.IdlePhase == 0; ++Try)
						{
							const int32 Pick = (i + T.InspectCount + Try) % N;
							const int32 Stand = FreeStand(*Pad, Pick, i);
							if (Stand != INDEX_NONE)
							{
								T.IdlePhase = 1;
								T.InspectId = Pad->Inspect[Pick];
								T.InspectStand = Stand - Pad->StandStart[Pick];
								T.InspectAtCm = Pad->StandCm[Stand];
								T.InspectFaceCm = Pad->InspectFaceCm[Pick];
								T.bFree = false;
							}
						}
						if (T.IdlePhase == 0)
						{
							T.IdleWaitS = 0.0;
							T.IdleWaitForS = InspectWaitS;
						}
					}
```
At the top of the pad block of the idle branch, right after `const double SimDt = ...` (`:1099`), insert the stand-point side of W1:
```cpp
				// P2 (ruling 28): a robot drawn docked beside its stand point since it set out (the keep-outs changed): that inspection ends and it walks
				// back, so the walk there never ends beside the docked robot.
				if (T.IdlePhase == 1 && T.KeepOffSerial != KeepOutSerial && InDockDisc(FVector2D(T.InspectAtCm.X, T.InspectAtCm.Y)))
				{
					T.IdlePhase = 3;
					T.bFree = false;
				}
```
It runs only on a keep-out change, before `FreeWalkTo` updates `KeepOffSerial` at the end of the branch.

In `ChooseIdlePad`'s keep path, replace `:590-595`, the "building gone" test, with:
```cpp
		// The same pad and slot: an inspection whose building is gone from the list (or, P2 W3, whose stand point is no longer one of that
		// building's after the rebuild) ends, and the figure walks back.
		if (T.IdlePhase == 1 || T.IdlePhase == 2)
		{
			const int32 b = Pad->Inspect.IndexOfByKey(T.InspectId);
			bool bStillThere = false;
			for (int32 s = b != INDEX_NONE ? Pad->StandStart[b] : 0; b != INDEX_NONE && s < Pad->StandStart[b + 1]; ++s)
			{
				bStillThere = bStillThere || FVector::Dist2D(Pad->StandCm[s], T.InspectAtCm) < 1.0;
			}
			if (!bStillThere)
			{
				T.IdlePhase = 3;
				T.bFree = false;
			}
		}
```
The `return;` at `:596` stays. Stand point 0 depends only on the pad and the building, so the old rule is a subset. Extra points can vanish when a new footprint or keep-out covers them.

`GetIdleDebug`'s pad block gets `Out.InspectStand = (T.IdlePhase == 1 || T.IdlePhase == 2) ? T.InspectStand : INDEX_NONE;` beside `Out.InspectId` (`:683`).

### 5.4 Proof, mutation

- **Check, in `robots.idle`.** It merges with the `clicktest` design, adds no step, and its hunk is in 9.3.
  - On each `IdleTrackAll` poll, every two figures drawn inspecting (`Show == Inspecting`) for the harness pad with the same `InspectId` must head for goals (`GoalCm` = their stand points) at least `IdleApartCm` (100 cm) apart.
  - The watch's end condition gains "and such a pair has been seen". If none is seen within the watch's 45 s, the step fails with "stand points not exercised", so the check cannot pass empty.
- **Will a pair come? Yes, whatever N is.** From 1.6, robots 1, 3 and 6 pick at about 50.70, 51.09 and 51.36 s, about 12 s after the pad is placed, and inspect for about 3 s each (a walk of 4-10 m at 450 cm/s, then 6 sim s of Operate = 2 s).
  - N = 1: robots 1 and 3 both at building 0.
  - N = 2: robots 1 and 3 at building 1.
  - N = 3: robots 3 and 6 at building 0.
  - N = 4: robots 6 (51.36 s) and 2 (54.06 s) at building 2.
  - N = 5: robots 1 and 6 at building 1.

  Robot 0 inspected #1 in every run, so N >= 1. The watch ends today with robot 0's return at about +11.7 s; with this condition it ends at the first pair, about +12.2 s (N <= 3) to +15 s (N = 4). That is within the 45 s bound.
  - Under W1 the walks of robots 0-3 and 6 are unchanged: their legs do not cross the keep-out (3.11). So these times hold.
- **Mutation `p2_mut_w3a`.** In `FreeStand`, drop the hold, keeping only the `InDockDisc` test.
  - Expected: `CLICKTEST robots.idle FAIL robots 1 and 3 at building #k, stand points 0 and 0 headed for stand points 0 cm apart (W3: 100 wanted);`. The robot pair depends on N, as above.
  - Every other check passes: 114/1.
- **With W3a, at the Lander** (the harness pad's building #1): the first figure takes stand point 0, the box corner (5.9, -5.9) m. The second takes stand point 1, 150 cm along the edge at (5.9, -4.4) m. The Lander's clearance box is 590 cm each side of (0, 0), and that point is 694 cm from the harness pad's centre, outside its keep-out.
- **Capture evidence:** `RH.IdleRobots`' inspecting lines print the stand point (section 7), so `cap_day`'s dumps show which point each inspecting figure took.

### 5.5 Frame cost

- At the pick (once per inspection per figure): a scan of the tracked figures for each of up to 5 stand points.
- At a cache rebuild: up to 4 more points per inspected building, each tested against the footprint boxes and the keep-outs. On `idle_day` that is about 2 pads x 2 buildings x 4 x 30 boxes.
- Nothing per frame.

### 5.6 Interactions and risk

- **Slot holding:** stand points are held exactly like slots (drawn idle on this tick or the last). A figure taking a job, changing pad, riding or being untracked frees its point at once.
- **`ResetTracking`:** the cache is emptied (stand points with it) and `Trips` is reset.
- **The docked robot:** points inside a docked robot's disc are skipped at the pick, and a walk there ends if one docks (5.3).
- **Risk: low.** With nobody else inspecting, nothing changes. Under contention the second figure stands 150-300 cm along the edge (round a corner near one), or goes to the next building in the list. A building with only point 0 valid behaves as today, except that a second figure waits for it or takes the next building.

---

## 6. W3b: no two pads' ring slots at one point (GO)

### 6.1 The change

In `RefreshIdlePads`, after the pad loop's closing brace (`:496`), before the function's (`:497`):
```cpp
	// P2 W3: two pads' rings: a slot within IdleApartCm of an open slot of a pad with a lower Id is closed (the lower pad keeps it), so no two
	// waiting figures of two pads stand at one point. Lower pads first, so each one's open set is final before a higher one is tested against it.
	TArray<int32> PadOrder;
	IdlePads.GetKeys(PadOrder);
	PadOrder.Sort();
	for (int32 a = 1; a < PadOrder.Num(); ++a)
	{
		FIdlePad& Upper = IdlePads.FindChecked(PadOrder[a]);
		for (int32 k = 0; k < Upper.Slots.Num(); ++k)
		{
			for (int32 b = 0; b < a && Upper.SlotOpen[k]; ++b)
			{
				const FIdlePad& Lower = IdlePads.FindChecked(PadOrder[b]);
				for (int32 m = 0; m < Lower.Slots.Num(); ++m)
				{
					if (Lower.SlotOpen[m] && FVector::Dist2D(Lower.Slots[m], Upper.Slots[k]) < IdleApartCm)
					{
						Upper.SlotOpen[k] = false;
						Upper.SlotNearSlotOf[k] = PadOrder[b];
						break;
					}
				}
			}
		}
	}
```
- **What it relies on.** `ChooseIdlePad` already skips closed slots (`:634`), and its keep path re-chooses a figure whose slot closed (`:588`, `SlotOpen`).
- **The references stay valid.** Nothing is added to `IdlePads` in the loop, so the two `FindChecked` references do not move.

### 6.2 Proof, mutation

- **Check, in `robots.idle`.** It merges with the `clicktest` design, adds no step, and its hunks are in 9.2 and 9.4.
  - The second far pad stands on the ray from the Lander through the first, 2 x R1 (1080 cm) beyond it.
  - Both rings' slot 0 faces the Lander (`:433-434`). So the second pad's slot 0 is at B - R1 x u = A + R1 x u, which is the first pad's slot 4 (A0 + 180 degrees).
  - In the main scene that is (-123.82, -123.82) m for both, to well under 1 cm. Positions are doubles, and both rings use `FMath::Cos` and `FMath::Sin` on doubles.
  - Nothing else closes that slot:
    - it is 540 cm from each pad's centre, outside both keep-outs (433 cm at most);
    - no footprint is out there;
    - the first pad's slot 4 is open.
  - At the report step, `GetIdlePadSlot(second, 0)` must read closed with `OutNearSlotOfPad` = the first pad, and `GetIdlePadSlot(first, 4)` open.
- **Mutation `p2_mut_w3b`.** Delete the loop.
  - Expected: `CLICKTEST robots.idle FAIL far pad #B's slot 0 at (-123.8, -123.8) m, 0 cm from far pad #A's slot 4 (open), is open, closed by pad #0 (W3: closed by pad #A wanted);`.
  - Every other check passes: 114/1.
- **Capture evidence: `cap_far.cmds`'s first `RH.IdleRobots` (4.5).** The far pad's second line reads "near a lower pad's slot: 0 (by pad #<near>)". A few ring-2 slots of each pad also fall inside the other's keep-out and are listed as "inside a keep-out".

### 6.3 Frame cost, interactions, risk

- **Frame cost:** per cache rebuild only, pads^2 x 20 x 20 distance tests.
- **Where it acts:** nowhere with one pad, or with pads more than 15.2 m apart (two ring-2 radii). That is none on `idle_day` (#6 and #26 are 28.8 m apart) and none near the click scene's colony.
- **Interactions:** slot holding and `ResetTracking` work as for any slot.
- **Risk: low.** A higher pad near a lower one has fewer open slots, so its overflow reaches the rank (or stays put) sooner.

---

## 7. `GetIdleDebug`, `GetIdlePadSlot` and `RH.IdleRobots` (developer-facing)

**`GetIdleDebug`** (`:649-688`). After `Out.RankCm = ParkSlotCm(Index);` (`:658`), before the `WalkTick` early return:
```cpp
	// P2: its sim position, for every tracked robot (the click test places its far pads from these; RH.IdleRobots prints a figure's reach).
	if (const UMassEntitySubsystem* Mass = GetWorld() ? GetWorld()->GetSubsystem<UMassEntitySubsystem>() : nullptr)
	{
		const FMassEntityManager& EM = Mass->GetEntityManager();
		if (Tracked.IsValidIndex(Index) && EM.IsEntityValid(Tracked[Index]))
		{
			Out.SimCm = EM.GetFragmentDataChecked<FTransformFragment>(Tracked[Index]).GetTransform().GetLocation();
		}
	}
```
`UMassEntitySubsystem::GetEntityManager() const` returns `const FMassEntityManager&` (`UE_5.8/Engine/Source/Runtime/MassEntity/Public/MassEntitySubsystem.h:35`). `Tick` already reads `FTransformFragment` through the const manager (`:1673`).

In the pad block, after `Out.RingCm = Pad->RingCm;` (`:681`):
```cpp
	Out.KeepOutCm = Pad->KeepOutCm;         // P2 (ruling 28)
	Out.DrawnRadiusCm = Pad->DrawnRadiusCm;
```
and after `Out.InspectId = ...` (`:683`): `Out.InspectStand = (T.IdlePhase == 1 || T.IdlePhase == 2) ? T.InspectStand : INDEX_NONE;`

**`GetIdlePadSlot`** (new, after `GetIdleDebug`):
```cpp
bool URHAgentVisualizerSubsystem::GetIdlePadSlot(int32 PadId, int32 Slot, FVector& OutCm, bool& bOutOpen, int32& OutNearSlotOfPad) const
{
	OutCm = FVector::ZeroVector;
	bOutOpen = false;
	OutNearSlotOfPad = 0;
	const FIdlePad* Pad = IdlePads.Find(PadId);
	if (!Pad || !Pad->Slots.IsValidIndex(Slot))
	{
		return false;
	}
	OutCm = Pad->Slots[Slot];
	bOutOpen = Pad->SlotOpen[Slot];
	OutNearSlotOfPad = Pad->SlotNearSlotOf.IsValidIndex(Slot) ? Pad->SlotNearSlotOf[Slot] : 0;
	return true;
}
```

**`LogIdleRobots`** (`:690-767`). The first per-pad line and the summary prefix keep their formats, as the `cache` design's checker needs (`cache.md` 6.3: `HEAD`, `PAD`, `RANK`).

- After each pad's line (`:715-719`), add a second line:
  ```cpp
  			// P2: the pad's keep-out (ruling 28), the slots closed by another pad (its keep-out, or W3's ring rule), its stand points (W3).
  			TArray<FString> Discs, KeptOut, NearSlot;
  			for (const FDockDisc& Disc : DockNow) { if (Disc.PadId == Id) { Discs.Add(FString::Printf(TEXT("(%.1f, %.1f)"), Disc.DrawnCm.X / 100.0, Disc.DrawnCm.Y / 100.0)); } }
  			for (int32 k = 0; k < P.SlotKeptOutBy.Num(); ++k) { if (P.SlotKeptOutBy[k] != 0) { KeptOut.Add(FString::Printf(TEXT("%d (pad #%d)"), k, P.SlotKeptOutBy[k])); } }
  			for (int32 k = 0; k < P.SlotNearSlotOf.Num(); ++k) { if (P.SlotNearSlotOf[k] != 0) { NearSlot.Add(FString::Printf(TEXT("%d (by pad #%d)"), k, P.SlotNearSlotOf[k])); } }
  			UE_LOG(LogRedHope, Display, TEXT("[RH.IdleRobots]   pad #%d keep-out %.0f cm (drawn radius %.0f cm from %s, + %.0f; never under %.0f), routed at %.0f cm; %d robot(s) drawn docked%s, kept %.0f cm off (routed at %.0f); slot(s) closed inside a keep-out: %s; near a lower pad's slot: %s; %d stand point(s) for its %d building(s)"),
  				Id, P.KeepOutCm, P.DrawnRadiusCm, PadModelHalfCm > 0.f ? TEXT("its model's bounds, fitted") : TEXT("its footprint's box (no model)"), KeepOutOverDrawnCm,
  				P.HalfCm + RobotClearanceCm + KeepOutOverClearCm, P.KeepOutCm + KeepRouteMarginCm, Discs.Num(),
  				Discs.Num() ? *(TEXT(" at ") + FString::Join(Discs, TEXT(" ")) + TEXT(" m")) : TEXT(""), DockKeepCm, DockKeepCm + KeepRouteMarginCm + DockDriftCm,
  				KeptOut.Num() ? *FString::Join(KeptOut, TEXT(", ")) : TEXT("none"), NearSlot.Num() ? *FString::Join(NearSlot, TEXT(", ")) : TEXT("none"),
  				P.StandCm.Num(), P.Inspect.Num());
  ```
  This line reads "pad #N keep-out", so the checker's `pad #(\d+) at ` never matches it.
- After the pad loop (`:720`), add the keep-out summary line:
  ```cpp
  		int32 OffPads = 0;
  		for (const FIdleKeepOut& KO : KeepOuts) { OffPads += KO.bOff ? 1 : 0; }
  		UE_LOG(LogRedHope, Display, TEXT("[RH.IdleRobots] keep-outs (ruling 28): %d finished surface pad(s) (%d switched off), %d docked robot(s), serial %u; since the last load %d kept-off plan(s), %d start exit(s), %d boxed in (today's route), %d frame(s) an idle walk held at the leash (%.0f m); the plans came no nearer than %.0f cm to a pad's centre and %.0f cm to a docked robot"),
  			KeepOuts.Num(), OffPads, DockNow.Num(), KeepOutSerial, KeptOffPlans, KeptOffExits, KeptOffFallbacks, IdleLeashFrames, IdleLeashCm / 100.f,
  			KeptOffNearestPadCm < TNumericLimits<float>::Max() ? KeptOffNearestPadCm : -1.f, KeptOffNearestDockCm < TNumericLimits<float>::Max() ? KeptOffNearestDockCm : -1.f);
  ```
  It reads "finished surface pad(s)", so the `HEAD` pattern ("working surface pad(s) in the cache") never matches it.
- **Figures.**
  - Declare `int32 Walkers = 0, ..., Inspecting = 0, Stay = 0;` (`:721`).
  - Add a `case EIdleShow::StayPut:` before `default:` (`:759`):
    ```cpp
    		case EIdleShow::StayPut:
    			++Stay;
    			UE_LOG(LogRedHope, Display, TEXT("[RH.IdleRobots] #%d staying put: no working pad and not its Lander rank slot within %.0f m of its sim position (%.1f, %.1f) m (the rank slot %.0f m away), %s"),
    				i, IdleReachCm / 100.f, D.SimCm.X / 100.0, D.SimCm.Y / 100.0, FVector::Dist2D(D.SimCm, D.RankCm) / 100.0, *At);
    			break;
    ```
  - The Lander-rank line's "no working pad" (`:752`) becomes `*FString::Printf(TEXT("no working pad within %.0f m of its sim position"), IdleReachCm / 100.f)`. Both branches of that `?:` must then be `FString`.
  - The inspecting line (`:756-757`) gains `", stand point %d"` with `D.InspectStand`.
  - The summary (`:765-766`) gains `", %d staying put"` with `Stay`, appended after "%d inspecting". The `RANK` pattern reads only up to "at the Lander rank".
- **Nothing else parses these lines.** I searched `scripts`, `Source`, `docs/qa/2026-10-26/workflows` and the idle gate's analyzers. The route-log line that `analyze.py` parses is untouched.

---

## 8. `ResetTracking` (`:1187-1234`)

After `NextIdlePadsS = 0.0;` (`:1194`; under the `cache` item, after its replacement of `:1191-1194`):
```cpp
	// P2 (ruling 28): the keep-outs and the docked robots are rebuilt from the reloaded colony on the first tick too (both then differ from these
	// empty lists whenever a pad or a docked robot exists, so KeepOutSerial moves and the boxes are rebuilt). KeepOutSerial itself only moves
	// forward; PadModelHalfCm is the asset's and stays.
	KeepOuts.Reset();
	DockDiscs.Reset();
	DockNow.Reset();
	KeepOutBoxes.Reset();
	KeptOffPlans = KeptOffExits = KeptOffFallbacks = IdleLeashFrames = 0;
	KeptOffNearestPadCm = KeptOffNearestDockCm = TNumericLimits<float>::Max();
```
`Trips.Reset()` (`:1190`) clears every new `FRobotTrip` field.

---

## 9. The click step's hunks (`RHClickTest.cpp`) and the merge with the other P2 designs

### 9.1 `FTest`: after `FString IdleWhy, IdleSaid, IdleWatchSaid;` (`:945`)

```cpp
		// P2 (the walks design): W2's and W3's two far pads (placed in setup, 120 m+ out; the second 2 x R1 beyond the first on the ray from the
		// Lander, so its slot 0 lands on the first's slot 4), and W3's pairs of figures inspecting one building at once.
		int32 IdleFarPad = 0, IdleFarPad2 = 0;
		FVector IdleFarPadCm = FVector::ZeroVector, IdleFarPad2Cm = FVector::ZeroVector;
		double IdleFarNearestCm = 0.0;
		int32 IdlePairsSeen = 0;
		TSet<int32> IdlePairKeys;
		double IdlePairNearestCm = 1.0e9;
		FString IdlePairSaid;
```

### 9.2 W2 and W3b: the far pads in `robots.idle setup`

Place this after `T->IdleFloorCm = ...` (`:3714`; under the `clicktest` design, after its replacement of `:3713-3714`), before `Sim->SetFleetHold(true);` (`:3715`). Setup's own `Viz`, `Sim`, `Defs`, `PadRow` and `Lander` are in scope (`:3636-3655`).
```cpp
			// P2 W2 and W3 (the walks design): two far Charging Pads by harness call (Debug_PlaceInstant, as above: no world click, so leak.total's
			// ExpectedWorld is unchanged; no placement test, since a Charging Pad needs grid coverage, which reaches 30 m from the Lander and 40 m from
			// a pylon). The first stands on a 10 m grid 120-250 m out from the Lander, its footprint 100 cm clear of every building's, more than
			// 12000 cm from the harness pad's centre (a robot drawn waiting at the harness pad has its sim position within the visualizer's 5000 cm
			// reach of it, so more than 7000 cm from this one) and more than 7000 cm from every tracked robot's sim position: while the harness pad is
			// on, every robot has it nearer. After Switch OFF the first far pad is every robot's nearest working pad, and W2's reach bound must still
			// send the watched robot to its Lander rank (the OFF goal check fails without it). The second stands on the ray from the Lander through
			// the first, 2 x R1 beyond it, so its slot 0 (facing the Lander) lands on the first's slot 4: W3 must close it (checked at the report).
			{
				TArray<FVector> SimAt;
				for (int32 i = 0; i < Viz->GetTrackedCount(); ++i)
				{
					URHAgentVisualizerSubsystem::FIdleDebug D;
					if (Viz->GetIdleDebug(i, D)) { SimAt.Add(D.SimCm); }
				}
				const FVector2D FarHalf = URHSimWorldSubsystem::GetFootprintHalfCm(PadRow, 1, 1);
				const auto FarClear = [Sim, Defs, &FarHalf](const FVector& At)
				{
					for (const FRHBuildingInstance& B : Sim->GetBuildings())
					{
						const FVector2D BHalf = URHSimWorldSubsystem::GetFootprintHalfCm(Defs->GetBuilding(B.DefName), B.SizeX, B.SizeY);
						if (B.Level == 0 && FMath::Abs(B.LocationCm.X - At.X) < FarHalf.X + BHalf.X + 100.0 && FMath::Abs(B.LocationCm.Y - At.Y) < FarHalf.Y + BHalf.Y + 100.0)
						{
							return false;
						}
					}
					return true;
				};
				bool bFar = false;
				for (int32 Ring = 12; Ring <= 25 && !bFar; ++Ring)
				{
					for (int32 Dx = -Ring; Dx <= Ring && !bFar; ++Dx)
					{
						for (int32 Dy = -Ring; Dy <= Ring && !bFar; ++Dy)
						{
							if (FMath::Max(FMath::Abs(Dx), FMath::Abs(Dy)) != Ring)
							{
								continue;
							}
							const FVector At = Lander + FVector(Dx * 1000.0, Dy * 1000.0, 0.0);
							const FVector At2 = At + (At - Lander).GetSafeNormal2D() * (2.0 * T->IdleR1Cm);
							double Nearest = 1.0e12;
							for (const FVector& S : SimAt) { Nearest = FMath::Min(Nearest, FVector::Dist2D(At, S)); }
							if (Nearest > 7000.0 && FVector::Dist2D(At, T->IdlePadCm) > 12000.0 && FarClear(At) && FarClear(At2))
							{
								bFar = true;
								Sim->Debug_PlaceInstant(FName("ChargePad"), At, 0);
								Sim->Debug_PlaceInstant(FName("ChargePad"), At2, 0);
								for (const FRHBuildingInstance& B : Sim->GetBuildings())
								{
									if (B.DefName == FName("ChargePad") && FVector::DistXY(B.LocationCm, At) < 1.0) { T->IdleFarPad = B.Id; }
									if (B.DefName == FName("ChargePad") && FVector::DistXY(B.LocationCm, At2) < 1.0) { T->IdleFarPad2 = B.Id; }
								}
								T->IdleFarPadCm = At;
								T->IdleFarPad2Cm = At2;
								T->IdleFarNearestCm = Nearest;
							}
						}
					}
				}
				if (T->IdleFarPad == 0 || T->IdleFarPad2 == 0)
				{
					T->IdleWhy += TEXT(" the far Charging Pads (P2 W2 and W3) were not placed;");
				}
			}
```
- **Why setup and not later.** Placing them at setup leaves any transient notice of the new buildings well clear of the Switch OFF press, about 12 s later.
- **The robots ignore them meanwhile.** Every robot has the harness pad (placed the same frame) as its nearest working pad, so the far pads change no choice before Switch OFF.
- **What W2's hunk does not do.** It does not use `CanPlaceBuildingSized`. That function refuses anything outside grid coverage (1.6), so the earlier draft's call there could never place a pad.
- **Names.** `D`, `i`, `At`, `B` and `S` are loop-local, and setup's earlier `At` and `D` live in closed loop scopes (`:3660`, `:3677`), so `-Wshadow` is clean.

### 9.3 W3a: the pair check in `IdleTrackAll` and the watch

After the per-figure loop in `IdleTrackAll` (`:3737-3823`), before the lambda's `};` (`:3824`):
```cpp
			// P2 W3 (the walks design): two figures inspecting one building at once for the harness pad head for stand points at least 100 cm apart.
			TArray<TPair<int32, URHAgentVisualizerSubsystem::FIdleDebug>, TInlineAllocator<8>> Inspectors;
			for (int32 i = 0; i < Viz->GetTrackedCount(); ++i)
			{
				URHAgentVisualizerSubsystem::FIdleDebug D;
				if (Viz->GetIdleDebug(i, D) && D.PadId == T->IdlePad && D.Show == URHAgentVisualizerSubsystem::EIdleShow::Inspecting)
				{
					Inspectors.Emplace(i, D);
				}
			}
			for (int32 a = 0; a < Inspectors.Num(); ++a)
			{
				for (int32 b = a + 1; b < Inspectors.Num(); ++b)
				{
					const URHAgentVisualizerSubsystem::FIdleDebug& Da = Inspectors[a].Value;
					const URHAgentVisualizerSubsystem::FIdleDebug& Db = Inspectors[b].Value;
					if (Da.InspectId != Db.InspectId)
					{
						continue;
					}
					bool bSeen = false;
					T->IdlePairKeys.Add(Inspectors[a].Key * 1024 + Inspectors[b].Key, &bSeen);
					T->IdlePairsSeen += bSeen ? 0 : 1;
					const double Apart = FVector::Dist2D(Da.GoalCm, Db.GoalCm);
					if (Apart < T->IdlePairNearestCm)
					{
						T->IdlePairNearestCm = Apart;
						T->IdlePairSaid = FString::Printf(TEXT("robots %d and %d at building #%d, stand points %d and %d"), Inspectors[a].Key, Inspectors[b].Key, Da.InspectId, Da.InspectStand, Db.InspectStand);
					}
				}
			}
```
- **The watch's exit test** (`:3983`) gains `&& T->IdlePairsSeen > 0` inside its parenthesis: `!(T->IdleWatchSaid.Len() > 0 && T->IdleRoundLegs > 0 && !bRoundOpen && T->IdlePairsSeen > 0)`. Merge it with the `clicktest` design's C2 edit of the same test.
- **The verdict.** After the floor checks (`:3993-4002`; the `clicktest` design replaces `:3993-4005`):
  ```cpp
  			if (T->IdlePairsSeen == 0)
  			{
  				T->IdleWhy += TEXT(" stand points not exercised: no two figures inspected one building at once within the watch (W3);");
  			}
  			else if (T->IdlePairNearestCm < 100.0)
  			{
  				T->IdleWhy += FString::Printf(TEXT(" %s headed for stand points %.0f cm apart (W3: 100 wanted);"), *T->IdlePairSaid, T->IdlePairNearestCm);
  			}
  			T->IdleSaid += FString::Printf(TEXT("; %d pair(s) of figures inspecting one building at once, nearest %.0f cm apart (%s)"),
  				T->IdlePairsSeen, T->IdlePairsSeen > 0 ? T->IdlePairNearestCm : 0.0, T->IdlePairsSeen > 0 ? *T->IdlePairSaid : TEXT("none"));
  ```
- **Timing.** The click test is not game code, and it allocates nothing on the heap per poll (an inline array of 8). Each poll adds 7 `GetIdleDebug` calls.

### 9.4 W3b: the check in the report step `robots.idle` (`:4068-4084`), before `Report(...)` (`:4083`)

```cpp
			// P2 W3 (the walks design): the second far pad's slot 0 stands on the first's slot 4, so the visualizer must close it (the lower pad keeps it).
			if (UWorld* W = FindGameWorld())
			{
				const URHAgentVisualizerSubsystem* Viz = W->GetSubsystem<URHAgentVisualizerSubsystem>();
				if (Viz && T->IdleFarPad != 0 && T->IdleFarPad2 != 0)
				{
					FVector Slot0 = FVector::ZeroVector, Slot4 = FVector::ZeroVector;
					bool bOpen0 = true, bOpen4 = false;
					int32 Near0 = 0, Near4 = 0;
					const bool bGot = Viz->GetIdlePadSlot(T->IdleFarPad2, 0, Slot0, bOpen0, Near0) && Viz->GetIdlePadSlot(T->IdleFarPad, 4, Slot4, bOpen4, Near4);
					const double Apart = FVector::Dist2D(Slot0, Slot4);
					if (!bGot || !bOpen4 || bOpen0 || Near0 != T->IdleFarPad)
					{
						T->IdleWhy += FString::Printf(TEXT(" far pad #%d's slot 0 at (%.1f, %.1f) m, %.0f cm from far pad #%d's slot 4 (%s), is %s, closed by pad #%d (W3: closed by pad #%d wanted);"),
							T->IdleFarPad2, Slot0.X / 100.0, Slot0.Y / 100.0, Apart, T->IdleFarPad, bOpen4 ? TEXT("open") : TEXT("closed"),
							bGot ? (bOpen0 ? TEXT("open") : TEXT("closed")) : TEXT("not in the visualizer's cache"), Near0, T->IdleFarPad);
					}
					else
					{
						T->IdleSaid += FString::Printf(TEXT("; far pads #%d at (%.1f, %.1f) m and #%d at (%.1f, %.1f) m, %.0f m from the nearest robot's sim position: #%d's slot 0, %.0f cm from #%d's slot 4, closed (W3)"),
							T->IdleFarPad, T->IdleFarPadCm.X / 100.0, T->IdleFarPadCm.Y / 100.0, T->IdleFarPad2, T->IdleFarPad2Cm.X / 100.0, T->IdleFarPad2Cm.Y / 100.0,
							T->IdleFarNearestCm / 100.0, T->IdleFarPad2, Apart, T->IdleFarPad);
					}
				}
			}
```
**After the step.**
- The far pads stay in the world. `robots.idle` is the last step before `final`, which reads only `leak.total` and restores the quicksave file (`:4087-4111`).
- The report releases the fleet and switches the harness pad on (`:4070-4077`), as today.

**Charging.** During the up-to-15 s OFF window, a robot that needs charge now drives to the far pad instead of the Lander's emergency dock (sim behaviour, this run only).
- If that robot is the watched one, the OFF goal check fails exactly as it would at HEAD, since it is busy either way, so this adds no failure mode.
- No charge or dock was logged during the step in any recorded run (`clicktest.md` 1.10).

### 9.5 Merging with `clicktest.md`

- **C4 reads `D.KeepOutCm`.** It has the same name and meaning here (3.10). Its mutation `p2_mut_c4` is this design's `p2_mut_w1`: delete `Obstacles.Append(KeepOutBoxes);` in `PlanKeptOff`.
- **C1's on-pad region must include "nearer the centre than `KeepOutCm`".** The `clicktest` design already does (`clicktest.md` 2.1 item 2). W1's exit takes a figure that charged on the pad radially out to 385-443 cm. Tracked from the 290 cm box, it would read 291-349 cm and fail a correct walk.
- **C1's mutation hunk 2.** It is a straight idle path hand-set for robot 5 just before the idle walk is planned, round 2's `r2/dev/mutation.diff` pattern. Under W1 it must also set `T.bFreeKeptOff = true;` and `T.KeepOffSerial = KeepOutSerial;` beside `bFree`, `FreeGoalCm`, `FreePath`, `FreeLenCm` and `FreeDoneCm`. Otherwise the new `FreeWalkTo(T.IdleGoalCm, true)` re-plans it kept off, through its `bKeptOff != T.bFreeKeptOff` and serial tests.
  - The hunk sits in the `else` of 4.4's `if (bStand)`, before `Desired = FreeWalkTo(T.IdleGoalCm, true);`.
- **C3's mutation** (a figure back at its slot heading for the rank at 1x) is unaffected. Its rank is 7.5 m from its sim position, within reach.
- **Timing.** The `clicktest` design's table changes in two places:
  - **setup:** two `Debug_PlaceInstant` calls and a grid search, a few ms;
  - **watch:** ends at the first pair, about +12.2 s to +15 s after the pad is placed, instead of +11.7 s.

  The step's worst case is unchanged: no bound changed.
- **The watch's exit and verdict** take both designs' clauses (C2's round-leg rule and W3a's pair).

### 9.6 Merging with `cache.md`

- **Edit regions.**
  - My edits sit in `RefreshIdlePads` after its key block. The `cache` design replaces `:380-416` and keeps `:417-497`.
  - In `ChooseIdlePad` my edits come after its early-out. It replaces `:573-577` and `:602-604`. My `bWasStayPut` line follows `:606`, its own.
  - My `LogIdleRobots` edits come after its header line (`:700`), and my `ResetTracking` edit comes after its replacement of `:1191-1194`.
  - The names do not collide.
- **Its stale-cache rule for `KeepOutCm` is met.** The number comes from the asset and the row, never an actor. The keep-outs, stand points and closures are functions of its key, and docking changes come from `GatherDocked` every tick, with their own trigger (`KeepOutSerial`).
- **Its "do not change `ParkSlotCm` or `FreeWalkTo`"** (`cache.md` 11) is scoped to the cache item. W1 changes both on purpose (3.8 (b), 3.9).

### 9.7 `meshes` and `sites-market`

Different lines: the cargo lump in `Tick` `:1898-1921` and `TrackEntities`, and `RHColonyVisualizerSubsystem.cpp`. No overlap. This design does not edit `RHColonyVisualizerSubsystem.cpp`.

---

## 10. Captures (into `Saved/RHCapture/p2/`, 1440x860, the idle gate's capture script)

- **`cap_day.cmds` and `cap_night.cmds`**, the idle gate's own (`wip-backups/idle-work/cap/`, sha256 recorded there), before and after. They show W1: the docked robot, the job walk from slot 3, inspection walks past the pad, and the robot leaving the mast. They also show W3a's stand points in the dumps.
- **`cap_far.cmds`** (new, 4.5), before and after. It shows W2 (the loop before, the rank after) and W3b's closed slot.
- **What the presentation lens checks:** no walking figure on a pad's disc or within about 150 cm of a docked robot; the ring still reads as a loose ring facing outward; figures inspecting one building stand apart.

## 11. Mutations (each under its own label; freeze the final sources by sha256 first; restore by copying the frozen file back, fresh mtime, sha256 compared)

| Label | Change (visualizer unless noted) | Expected `robots.idle` | Total |
|---|---|---|---|
| `p2_mut_w1` (= `p2_mut_c4`) | delete `Obstacles.Append(KeepOutBoxes);` in `PlanKeptOff` | FAIL "robot 5 came ~315 cm from the pad's centre (never under the floor, 350 cm ...)" | 114/1 |
| `p2_mut_w2` | delete the `IdleReachCm` pad check in `ChooseIdlePad` | FAIL "after Switch OFF the pad is off and robot 0 is drawn 2 at pad #<far> with its goal ..." | 114/1 |
| `p2_mut_w3a` | in `FreeStand`, `bFree = !InDockDisc(...)` only (no hold) | FAIL "robots a and b at building #k, stand points 0 and 0 headed for stand points 0 cm apart (W3: 100 wanted)" | 114/1 |
| `p2_mut_w3b` | delete W3b's closure loop in `RefreshIdlePads` | FAIL "far pad #B's slot 0 ..., 0 cm from far pad #A's slot 4 (open), is open, closed by pad #0 (W3: closed by pad #A wanted)" | 114/1 |

**Each fails only its own clause.**
- `p2_mut_w1` leaves `KeepOutCm`, the slot closures and the stand points as built.
- `p2_mut_w2` leaves the far pads' slots as built.
- `p2_mut_w3a` and `p2_mut_w3b` touch no route.

**Each passes on the step without its check:**
- HEAD's floor of 290 cm passes `p2_mut_w1`;
- HEAD's OFF check alone passes `p2_mut_w2` only with no far pad;
- HEAD's step has no pair check and no slot check.

## 12. For the builder

- **What is not changed:**
  - `PlanRoute` (`:232-333`), `SegmentBlocked`, `RebuildObstacles`, `AdvanceRover`, the `bDrive` and `bDeepDraw` branches of `Tick`, and rovers;
  - the route-log line's format;
  - `RHColonyVisualizerSubsystem.cpp`;
  - any file under `Source/RedHopeSim`, `docs/data` or `Content/RedHope/Data`.
- **Dev-run expectations (main scene), all to be reported:**
  - `robots.idle` PASSes with "7 tracked";
  - the floor reads 350 cm, and "nearest" about 385 cm;
  - the far pads stand at (-120, -120) and (-127.6, -127.6) m, the first 164 m from the nearest robot;
  - #B's slot 0 is closed;
  - a pair of figures at one building heads for stand points about 150 cm apart.

  If fewer than 7 figures come to the pad, a robot's sim position is more than 50 m from it: report the robots' `SimCm` (a W2 behaviour change, by design).
- **Deviations to disclose in the docs entry:**
  - a figure staying put is counted in none of the route log's three idle counts;
  - a road-trip job walk from a ring slot keeps `PlanRoute`'s clearance (the drive branch is untouched);
  - a docked rover's nose and tail are kept 124 cm off, not 150;
  - a job site inside a keep-out that is not the pad is walked into;
  - a rank slot inside a pad's keep-out moves along the rank;
  - every idle leg is kept off, including legs that neither start nor end at a ring slot or stand point (rank legs), which is more than the ruling asks;
  - the job walk's keep-off lasts for its whole task;
  - under `rh.ModelSetV2 0` the drawn cube has the same 200 cm half-extent the model gives, so the number holds;
  - the far pads stay in the click world after the step.
- **What changed from the earlier draft of this file (12:05).**
  - **The far pads.** That draft placed W2's far pad with `CanPlaceBuildingSized`, which refuses anything outside grid coverage (1.6), so no spot would ever pass. Here the pad is placed by `Debug_PlaceInstant` with the test's own footprint check.
  - **W3b's proof** moves from a capture log line to a click check (9.4), using a second far pad.
  - **The drawn radius** comes from the asset and the fit, not the spawned actor, as the `cache` and `clicktest` designs ask.
  - **The leash** takes back the frame's progress instead of letting the path run on (no chord cut later).
  - **`bKeepOff` ignores a stale idle state** (`IdleTick + 1 >= TickSerial`).
  - **A docked rover's drawn position** comes from its drive (`RoverVis`).
  - **The W3a check** fails when no pair was seen ("not exercised"), so it cannot pass empty.
  - **`ChooseIdlePad`'s keep path** also ends an inspection whose extra stand point vanished.
