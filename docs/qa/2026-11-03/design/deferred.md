# p2 design, key `deferred`: two items this display-only gate does NOT build

Read-only design at HEAD `6df7fcc` (`git rev-parse HEAD` = `6df7fcc6a30920e30aca294db3bb5473aa6ee592`). Nothing in the repo was edited, built or run; every claim below comes from reading the file and line cited. Anchors are `file:line` at `6df7fcc`; the builder re-anchors by symbol.

| Item | What | Recommendation | Why not this gate |
|---|---|---|---|
| X1 | D7's mid-trip deep path (ruling 21's follow-up) | **DEFER**: its own sim gate | It changes `Source/RedHopeSim` (robot trip state at an atrium open), which this gate may not touch. |
| X2 | Commuter shuttles drawn as boxes | **DEFER**: a director look answer (L3), then a presentation gate with new art | It needs new content (a mesh, its material and masks) and an open director look question (L3); this gate adds no art. |

The convoy's engine cubes are the `meshes` key's M2, not this file's.

---

# X1. D7's mid-trip deep path (ruling 21's follow-up)

## X1.1 What happens today

**The ruling** (`docs/qa/2026-10-26/preflight/ORCHESTRATOR-RULINGS.md:31`): "A robot already walking a planned deep path when an atrium opens keeps its waypoints until its target changes, so for that one trip it can cross the open cells; nothing gets stuck, and only the agent band is affected. A follow-up, not built in D7: at the open, drop the deep paths of robots standing on the new atrium's upper floors (the reviewer's option (a))." The D7 entry gives both options (`docs/design-decisions.md:2244`): "(a) at the open, drop the deep paths of the robots standing on the new atrium's upper floors, so they plan again; (b) keep the deep net's atrium key in the path and plan again when it moves. Neither is built." The handoff's Follow-ups repeat it (`docs/qa/2026-10-26/orchestrator-handoff.md`, the D7 bullet).

**A robot below drives the deep network** (`Source/RedHopeSim/Private/RHSimRoads.cpp:40-49`, inside `RH::MoveAlong`, the call both brains make):
```cpp
	// D4: a robot below, or bound below, drives the deep network once a lift car is fitted (rovers never go below). Back on
	// the surface with a surface target, its deep trip is forgotten and the body below runs as it always did.
	if (Sim.HasRobotLifts())
	{
		if (Sim.LevelOfZ(Transform.GetLocation().Z) != 0 || TargetLevel != 0)
		{
			return Sim.MoveAlongDeep(Entity, Transform, TargetCm, TargetLevel, SpeedMps, Dt, CargoKg);
		}
		Sim.DropDeepPath(Entity);
	}
```

**A deep trip is planned only on a new target** (`Source/RedHopeSim/Private/RHSimTransit.cpp:1577-1594`, `URHSimWorldSubsystem::MoveAlongDeep`):
```cpp
	FRHRobotRoadPath* Path = RobotPaths.Find(Entity);
	if (!Path || !Path->bDeep || Path->TargetCm != TargetCm || Path->TargetLevel != TargetLevel)
	{
		// A new target: plan from here. A hold or a ride in progress is discarded; the robot stays on the column at its
		// boarding floor with its Z unchanged (Z changes only when a ride completes), which is a legal start.
		FRHRobotRoadPath& Slot = RobotPaths.FindOrAdd(Entity);
		const FVector Pos = Transform.GetLocation();
		Slot = FRHRobotRoadPath();
		Slot.bPlanFailed = !PlanDeepRoute(LevelOfZ(Pos.Z), Pos, TargetLevel, TargetCm, PeekRobotStamp(), Slot);
		...
		Slot.Serial = NextPathSerial++;
		Path = &Slot;
	}
```
The header says the same (`Source/RedHopeSim/Public/RHSimWorldSubsystem.h:1382-1383`: "planned on first use and again only when the target changes"; `:113`: "O7: a deep trip is re-planned when (TargetCm, TargetLevel) changes").

**The open changes no trip** (`Source/RedHopeSim/Private/RHSimAtrium.cpp:1116-1128`, the tail of `ExecuteOpenAtrium`):
```cpp
	Atriums.Insert(A, At);
	++AtriumsVersion;
	InvalidateAtriumCaches();
	UE_LOG(LogRedHopeSim, Display, TEXT("Atrium opened over cavern %d (floors %d..%d, %d x %d): %d cells open, %.0f kg of oxygen billed for the premium fill"),
		Id, C->Top, C->Bottom, C->GetSizeX(), C->GetSizeY(), BoxCells * (C->GetFloors() - 1), PremiumKg);
```
The deep net is keyed on the atrium version (`RHSimTransit.cpp:1080-1082`, `EnsureDeepNet`: `N.Atriums == AtriumKey`), and every step test goes through `IsWalkOrdinal` (`BuildLatticeNeighbours` `:817-828`, `BuildLatticeField` `:882-888`, `LatticeAStar` `:915-918`). So every NEW plan walks round the void. A stored path's waypoints are never re-derived, though, and `MoveAlongDeep` drives them as stored (`:1638-1680`).

**What a stored trip holds** (`RHSimWorldSubsystem.h:107-122`):
```cpp
	// D4: a deep trip (planned by MoveAlongDeep, bDeep) also carries each waypoint's floor and its rides: LegEdge < 0 is a
	// ride on shaft -LegEdge - 1, from the column at the previous waypoint's floor to the column at Levels[leg]. ...
	TArray<int32> Levels;
	int32 TargetLevel = 0;                  // O7: a deep trip is re-planned when (TargetCm, TargetLevel) changes
	bool bDeep = false;
	bool bPlanFailed = false;               // no route from here: the robot stands until its target changes
	bool bBoarded = false;                  // on the column, committed to the ride leg Next
	bool bHeldThisStep = false;             // the last sub-step was a hold (the brains draw idle power, no wear)
	int32 HoldStepsLeft = 0;
	int32 RideStepsLeft = 0;
	int32 RideSteps = 0;                    // the boarded ride's whole length in sub-steps
	int32 RideFromLevel = 0;                // the boarding floor
```
`Waypoints`, `LegEdge` and `Levels` stay the same length: `PlanDeepRoute`'s `Walk` (`RHSimTransit.cpp:1451-1456`), its ride legs (`:1527-1529`) and `AppendSurfaceLegs` (`:1058-1075`, `Levels.Add(0)`) each append to all three. `RobotPaths` is "lookups only, never iterated" (`RHSimWorldSubsystem.h:3437`).

## X1.2 Why ruling 21's literal rule is not enough, and the amendment I recommend

The literal rule tests where a robot stands. Two cases slip past it.

**Gap A: a robot that walks an upper floor LATER in the trip.** A trip can ride onto a floor and walk it (`ColumnToCell`, `RHSimTransit.cpp:1475-1487`). It can also walk between two shafts on an intermediate floor (`:1542-1552`):
```cpp
		else if (A.Level < 0)
		{
			// Column A, its landing, along B's field to B's landing, then B's column.
			Cost.LatticeCm += 1000 + B.Dist[A.Landing] + 1000;
			Walk(FloorCellXY(A.Level, A.Landing), A.Level);
			for (int32 O = B.Toward[A.Landing]; O != INDEX_NONE; O = B.Toward[O])
			{
				Walk(FloorCellXY(A.Level, O), A.Level);
			}
			Walk(B.XY, B.Level);
		}
```
Example: a robot on -5 rides shaft 0 up to a site on -1 beyond the box. At the open it is not standing on -1, but after it lands it walks the stored waypoints across the box. This is the commonest case, since robots ride to work.

**Gap B: a boarded robot.** Dropping a boarded robot's trip throws away its hold or ride; "the robot stays on the column at its boarding floor" (`:1582-1583`). It then boards again, and `Board` (`:1600-1617`) counts `++RidesBoarded` a second time and charges its cargo places to the shaft's ledger again (`ChargeTransit`, `:1612-1616`). The presentation also draws a riding robot by lerping Z between the two floors (`Source/RedHope/Private/RHAgentVisualizerSubsystem.cpp:1706-1722`). With the ride gone, the robot would snap back to its boarding floor.

**Recommended amendment** (an orchestrator ruling, not a director question; the ruling's intent, "for that one trip it can cross the open cells", is unchanged):
> At the open, every robot whose deep trip still walks on one of the new atrium's upper floors plans again. A robot that is walking plans again at once. A boarded robot (in a hold or a ride) keeps its ride and plans again on the sub-step its ride ends.

"Walks on floor L" means a remaining leg `k` in `[Next, Waypoints.Num())` with `LegEdge[k] >= 0` and `Levels[k] == L`. The upper floors are `C.Bottom < L <= C.Top`, the same range as the premium loop (`RHSimAtrium.cpp:1107`, `for (int32 L = C->Top; L > C->Bottom; --L)`) and `DeriveAtriumCells`' hall skip (`:102-105`). A robot standing on an upper floor and walking is always caught, because its current leg `Next` is a walk on that floor. A robot walking to the column to ride away is caught too, because its walk to the column is on that floor.

**Options considered and not recommended:**
- **Option (b)** (the path keeps the atrium key and plans again when it moves). It needs no enumeration, but `AtriumsVersion` moves on every open, every close (`RHSimAtrium.cpp:1143`) and at load (`:1287`). So every deep robot in the colony would plan again after any open or close, including trips that never touch the atrium. That is wider behaviour change for no gain, and the ruling chose (a).
- **A cell-level test** (drop only trips with a waypoint on a newly open cell). It is narrower, but to be exact it would also need the robot's own cell, and the diagonal steps whose orthogonal cell just opened: `BuildLatticeNeighbours` (`:834-844`) forbids such corner-cutting in new plans, while a stored diagonal step would still graze the void. The floor test is a superset of every crossing. Its only extra cost is one re-plan per robot walking on two floors, at a rare player action.
- **The literal rule.** It misses gaps A and B above.

## X1.3 The exact change (four files under `Source/RedHopeSim`, plus docs)

**(1) `Source/RedHopeSim/Public/RHSimWorldSubsystem.h`**
- In `FRHRobotRoadPath`, after `bool bHeldThisStep` (`:117`), add:
  ```cpp
  	bool bReplanAtLanding = false;          // ruling 21: an atrium opened during this ride on a floor the trip walks after it; dropped as the ride ends
  ```
  Runtime only. It is never saved (no robot trip is) and is reset by `Slot = FRHRobotRoadPath()` (`RHSimTransit.cpp:1586`) on every new plan.
- `:113`'s comment becomes: `// O7: a deep trip is re-planned when (TargetCm, TargetLevel) changes, or when an atrium opens on a floor it still walks (ruling 21)`.
- `:1382-1383` (the `MoveAlongDeep` comment) becomes: "planned on first use, again when the target changes, and again after an atrium opens on a floor the trip still walks (`DropDeepPathsOverAtrium`; a boarded robot plans again as its ride ends)".
- In the private atrium block, after `RemapRobotsOffOpenCells` (`:4015`), declare:
  ```cpp
  	void DropDeepPathsOverAtrium(const FRHCavern& C); // ruling 21: trips that still walk the new atrium's upper floors plan again (spawn order)
  ```
- `:3437`'s comment stays true: the pass looks `RobotPaths` up per robot and never iterates it.
- No data member is added to `URHSimWorldSubsystem`, so T1's member inventory, which classifies the subsystem's data members (`wip-backups/t1-prep/PREP-STATUS.md:63`, "606 members (597 data + 9 constexpr)"), is unaffected.

**(2) `Source/RedHopeSim/Private/RHSimAtrium.cpp`**
- In `ExecuteOpenAtrium`, between `InvalidateAtriumCaches();` (`:1125`) and the `UE_LOG` (`:1126`), add `DropDeepPathsOverAtrium(*C);`. `C` still points into `Caverns`, which nothing here changes. The uplink (`RHSimWorldSubsystem.cpp:9470-9472`) and `Debug_OpenAtrium` (`RHSimAtrium.cpp:1148-1159`, used by the suites, `-scale`, `RH.OpenAtrium now`, `RH.DeepBench` and the click test) both reach it. `ExecuteCloseAtrium` is not changed: a close only adds walkable cells, so no stored trip crosses a void.
- The new function, placed after `RemapRobotsOffOpenCells` (`:1292-1334`), whose way of reaching the agent subsystem it copies:
  ```cpp
  void URHSimWorldSubsystem::DropDeepPathsOverAtrium(const FRHCavern& C)
  {
  	// Ruling 21's follow-up (as amended): a deep trip planned before this open keeps its waypoints until its target changes, and the new
  	// atrium's upper floors (C.Bottom < L <= C.Top) may hold open cells on them. Every robot whose trip still walks one of those floors plans
  	// again: a walker at once (its next MoveAlongDeep plans from where it stands, through Resolve), a boarded robot as its ride ends (its hold
  	// or ride is kept: dropping it would board it again, counting the ride and charging its cargo places twice, and draw its car back to
  	// the boarding floor). Robot-local runtime state only: no stock, ledger, save or era-band read moves, and no cache is built here. Real
  	// robots in spawn order (the agent subsystem's list), each decided by its own trip alone, so no container order decides anything.
  	URHAgentSubsystem* Agents = GetWorld() ? GetWorld()->GetSubsystem<URHAgentSubsystem>() : nullptr;
  	if (!Agents || RobotPaths.Num() == 0)
  	{
  		return;
  	}
  	Agents->ForEachRobotState([this, &C](FMassEntityHandle Entity, const FVector&, float)
  	{
  		FRHRobotRoadPath* Path = RobotPaths.Find(Entity);
  		if (!Path || !Path->bDeep)
  		{
  			return; // no trip, or a surface road trip (a rover's, or a robot's on the surface)
  		}
  		bool bWalksThere = false;
  		for (int32 k = Path->Next; k < Path->Waypoints.Num() && !bWalksThere; ++k)
  		{
  			bWalksThere = Path->LegEdge[k] >= 0 && Path->Levels[k] > C.Bottom && Path->Levels[k] <= C.Top;
  		}
  		if (!bWalksThere)
  		{
  			return;
  		}
  		if (Path->bBoarded)
  		{
  			Path->bReplanAtLanding = true;
  		}
  		else
  		{
  			RobotPaths.Remove(Entity);
  		}
  	});
  }
  ```
  `ForEachRobotState` (`Source/RedHopeSim/Private/RHAgentSubsystem.cpp:217-232`) walks `RobotHandles` in spawn order and hands each live entity to the callback. Removing from `RobotPaths` inside it is safe, because the loop iterates `RobotHandles`, not `RobotPaths`. A failed plan (`bPlanFailed`, no waypoints) is never dropped, and that is right: an open only removes walkable cells, and refusal 7 keeps every dig whole and a landing, so it cannot create a route.

**(3) `Source/RedHopeSim/Private/RHSimTransit.cpp`**: in `MoveAlongDeep`'s ride-end branch (`:1626-1634`), after `++Path->Next;` (`:1633`), add:
  ```cpp
  			if (Path->bReplanAtLanding)
  			{
  				RobotPaths.Remove(Entity); // ruling 21: an atrium opened on a floor this trip walks next; plan again from the column
  			}
  ```
  `Path` dangles after the removal, so this must stay the last statement in the block; `return false;` (`:1635`) follows. On the next sub-step `Path` is null, so the robot plans from where it stands, and `Resolve` (`:1268-1275`) puts it on the arrival column (`EEnd::Column`). The ride was charged once at boarding and is not charged again.

**(4) `Source/RedHopeSim/Private/RHSimAtriumSuite.cpp`**: AT14 (section X1.5), plus two edits to the header comment:
- `:19-20` ("...(it does not tick them); AT9 moves two of them by hand...") gains "AT14 drives three of them through RH::MoveAlong."
- The case list ending at `:34-35` gains "AT14 a trip planned before an open (ruling 21's follow-up): a walker plans again at once, a rider as its ride ends, a trip on the hall floor keeps its waypoints."

**Docs (same commit):**
- `docs/design-decisions.md` gets a new entry that cites the amended ruling.
- `ORCHESTRATOR-RULINGS.md` ruling 21 is amended by the orchestrator before the brief.
- The handoff's D7 follow-up clause is struck.
- No `docs/data` change, no `Content` change, no save version, no frame, no config row, and no player-facing string. The pass logs nothing; AT14 reads the trips directly, as AT9 reads positions.

## X1.4 Battery and other runs: what moves and why

Only two battery logs open an atrium at all (`grep -c "Atrium opened over cavern"` over `Saved/baselines/battery_post_idle/*.log`): `atriums.log` (36 opens) and `moves.log` (1). At every one of those opens `RobotPaths` is empty, so the pass returns at its first test:
- **`-atriums`**: "The headless world holds the base colony's Mass robots (it does not tick them)" (`RHSimAtriumSuite.cpp:19-20`). There is no `MoveAlong` call in the file (`grep -c MoveAlong` = 0). Every case starts with `Reset()` (`:186-197`), which runs `LoadColony` (whose load clears trips, `RHSimWorldSubsystem.cpp:15116`, `RobotPaths.Reset()`) and then `Debug_ForgetRobotTransit()` (`RHSimWorldSubsystem.h:1457`, `RobotPaths.Reset()`).
- **`-moves`**: MV4 opens the box colony's atrium with no lift fitted and no robot driven (`RHSimMoveSuite.cpp:507-537`; `grep -c MoveAlong` = 0).
- **`-transit`**: it drives made-up handles (`RHSimTransitSuite.cpp:173`, `FMassEntityHandle(NextHandle++, 1)`) but opens no atrium.

**Predicted diff against `Saved/baselines/battery_post_idle`:**
- `atriums.log` gains AT14's lines between AT13's last check and the END line. AT8's identical setup (BoxColony, the corridor, `Fit(0)`, the open) prints 46 sim lines (baseline lines 3188-3243), so expect about 46 setup lines plus the 3 new check lines. The END line moves from `=== ATRIUMS TEST END: 106 check(s), 0 failure(s) ===` to `109 check(s)`. Every other `atriums.log` line is unchanged, including Finish's ordinal-fallback check.
- Every other log is line-identical, apart from the known wall-clock digits (`eraparity` EP4, `traffic` X16), which differ run to run (handoff: "against `battery_post_bm7` TOTAL 4").
- The battery stays at 64 runs: no new suite.

**Outside the battery:**
- **`-scale`**: its atrium block loads `scalecaverns` and era-steps only (`RHSimCommandlet.cpp:17277-17335`), so `RobotPaths` is empty at its four opens. The pass calls no `Ensure*`, so its check "the deep net and the walk rebuild once" (`:17373-17376`) holds. The opens are outside its timed windows (the 2,000 preflights are timed before the fourth open, `:17325-17332`). Run it once to show its result lines unchanged; judge any timing under ruling 22.
- **Playtest**: `Saved/baselines/ind_full_playtest.txt` mentions no atrium (`grep -ci atrium` = 0). It stays byte-identical.
- **Click test**: no step is added (115 steps, deep-view scene 9). Its atrium steps open a harness-dug atrium on -4..-5 (`RHClickTest.cpp:960-997`), and no check reads a robot's trip.
- **`RH.DeepBench`**: it opens its atrium while building the bench (`RHCheats.cpp:2449-2460`), before any robot has a trip.

## X1.5 The `-atriums` check to add: AT14 (3 checks; 106 -> 109)

Insert this block after AT13 (`RHSimAtriumSuite.cpp:1747`) and before `return Finish();` (`:1749`). It uses only helpers the suite already has:
- `BoxColony` (`:224-240`), `Fit` (`:215-220`), `Open` (`:241`), `RobotCm` (`:181`), `Ord` (`:148`), `BoxOrds` (`:244-250`);
- `Agents` (`:117`), `AgentDt` (`:113`);
- `CollectRobotsOfClass`, as AT9 uses it (`:1421-1422`).

The base colony has two Haulers and two Excavators (`docs/data/RH_Robots.csv`: RC_H `StartingCount` 2, `Speed_mps` 4.0; RC_E `RobotClass` Excavator, `StartingCount` 2).

The geometry is AT8's colony, whose same-dig leg from (1,0) to (5,0) is 4,000 cm straight through the box before the open and 8,000 cm round the corridor after it (`:1128-1136`). At the rows, a ride from -5 to -1 lasts `10 x (LiftDwellSeconds 10 + 4 floors x LiftSecondsPerFloor 4)` = 260 sub-steps (`Board`, `RHSimTransit.cpp:1606`). The loops wait on states, not counts, so a balance-pass change to those placeholder rows cannot break the check.

```cpp
	// --- AT14. A trip planned before the open (ruling 21's follow-up, as amended): a robot whose deep trip still walks an upper floor of the new
	// atrium plans again - a walker at once, a boarded robot as its ride ends - and a trip on the hall floor keeps its waypoints. AT8's colony: the
	// box (2,-1)..(4,1) on -1..-3, the corridor round its north and east sides on -1, a lift car on shaft 0. Three of the base colony's robots are
	// driven by hand through RH::MoveAlong, the call both brains make (the suite does not tick Mass): Hauler A from (1,0) of -1 to (5,0), across
	// the box; Hauler B from (1,0) of -5 to the same goal, riding shaft 0 up; Excavator C from (1,0) to (4,0) of the hall -3, across its box cells.
	// 4 m/s (RC_H's Speed_mps), no cargo (no place is charged, no stamp is taken), one AgentDt a sub-step.
	{
		int32 C1 = INDEX_NONE;
		bool bOk = BoxColony(C1);
		for (const FIntPoint& P : { FIntPoint(1, 2), FIntPoint(2, 2), FIntPoint(3, 2), FIntPoint(4, 2), FIntPoint(5, 2), FIntPoint(5, 1), FIntPoint(5, 0) }) { bOk &= Sim->CarveLatticePoint(-1, P); }
		bOk &= Fit(0);
		TArray<FRHRoverState> Hs, Es;
		if (Agents) { Agents->CollectRobotsOfClass(FName("Hauler"), Hs); Agents->CollectRobotsOfClass(FName("Excavator"), Es); }
		bOk &= Agents && Hs.Num() > 1 && Es.Num() > 0 && Ord(-5, 1, 0) != INDEX_NONE && Ord(-3, 4, 0) != INDEX_NONE;
		const TArray<int32> Box1 = BoxOrds(-1, FIntPoint(2, -1), FIntPoint(4, 1));
		const auto OnOpenCell = [&](const FVector& P) { const int32 L = Sim->LevelOfZ(P.Z); const int32 O = L < 0 ? Sim->FindCarvedCellAt(L, P) : (int32)INDEX_NONE; return O != INDEX_NONE && Sim->IsCellOpen(L, O); };
		// The rest of a trip (legs Next on): a walk leg ending on a box cell of -1; every waypoint off the open cells.
		const auto RestOnBox = [&](const FRHRobotRoadPath& P)
		{
			bool bOn = false;
			for (int32 k = P.Next; k < P.Waypoints.Num(); ++k) { bOn |= P.LegEdge[k] >= 0 && P.Levels[k] == -1 && Box1.Contains(Sim->FindCarvedCellAt(-1, P.Waypoints[k])); }
			return bOn;
		};
		const auto RestNeverOpen = [&](const FRHRobotRoadPath& P)
		{
			bool bNever = true;
			for (int32 k = P.Next; k < P.Waypoints.Num(); ++k)
			{
				const int32 L = P.Levels[k];
				const int32 O = L < 0 ? Sim->FindCarvedCellAt(L, P.Waypoints[k]) : (int32)INDEX_NONE;
				bNever &= O == INDEX_NONE || !Sim->IsCellOpen(L, O);
			}
			return bNever;
		};
		const auto Drive = [&](FMassEntityHandle H, FTransform& X, const FVector& To, int32 ToLevel)
		{
			const bool bArrived = RH::MoveAlong(*Sim, H, X, To, 4.0f, AgentDt, false, 0.f, ToLevel);
			Agents->SetRobotPosition(H, X.GetLocation()); // the entity stands where it is driven (MX3 reads it)
			return bArrived;
		};
		const FVector Goal = RobotCm(-1, 5, 0), HallGoal = RobotCm(-3, 4, 0);
		bool bCrossA = false, bDroppedA = false, bNeverOpenA = false, bOffA = true, bArrA = false;
		bool bCrossB = false, bRideKept = false, bFlagB = false, bDroppedAtLanding = false, bNeverOpenB = false, bOffB = true, bArrB = false;
		bool bKeptC = false, bArrC = false;
		int32 SerialA = -1, SerialA2 = -1, SerialB = -1, SerialB2 = -1, SerialC = -1, StepsA = 0, StepsB = 0, StepsC = 0, Rides = -1, RideLeft = -1, RideAll = -1;
		if (bOk)
		{
			const FMassEntityHandle HA = Hs[0].Entity, HB = Hs[1].Entity, HC = Es[0].Entity;
			FTransform XA(RobotCm(-1, 1, 0)), XB(RobotCm(-5, 1, 0)), XC(RobotCm(-3, 1, 0));
			Agents->SetRobotPosition(HA, XA.GetLocation());
			Agents->SetRobotPosition(HB, XB.GetLocation());
			Agents->SetRobotPosition(HC, XC.GetLocation());
			const int32 Rides0 = Sim->GetRidesBoarded();
			// B first: to the column of -5, aboard, its hold over and at most half its ride left.
			FRHRideState R0;
			for (int32 K = 0; K < 5000 && !(Sim->GetRideState(HB, R0) && R0.HoldStepsLeft == 0 && 2 * R0.RideStepsLeft <= R0.RideSteps); ++K) { Drive(HB, XB, Goal, -1); }
			const FRHRobotRoadPath* PB = Sim->GetRobotRoadPath(HB);
			SerialB = PB ? PB->Serial : -1;
			RideLeft = R0.RideStepsLeft;
			RideAll = R0.RideSteps;
			bCrossB = PB && PB->bBoarded && RestOnBox(*PB);
			// A and C plan with one sub-step each (A moves 40 cm toward (2,0) and still stands on (1,0)).
			Drive(HA, XA, Goal, -1);
			Drive(HC, XC, HallGoal, -3);
			const FRHRobotRoadPath* PA = Sim->GetRobotRoadPath(HA);
			const FRHRobotRoadPath* PC = Sim->GetRobotRoadPath(HC);
			SerialA = PA ? PA->Serial : -1;
			SerialC = PC ? PC->Serial : -1;
			const int32 NextC = PC ? PC->Next : -1, WaysC = PC ? PC->Waypoints.Num() : -1;
			bCrossA = PA && RestOnBox(*PA) && Sim->FindCarvedCellAt(-1, XA.GetLocation()) == Ord(-1, 1, 0);
			bOk &= Open(C1);
			// At the open: A's trip is gone; B's ride and trip are untouched but flagged; C's trip is untouched.
			FRHRideState R1;
			PA = Sim->GetRobotRoadPath(HA);
			PB = Sim->GetRobotRoadPath(HB);
			PC = Sim->GetRobotRoadPath(HC);
			bDroppedA = PA == nullptr;
			bRideKept = Sim->GetRideState(HB, R1) && R1.Shaft == R0.Shaft && R1.FromLevel == R0.FromLevel && R1.ToLevel == R0.ToLevel
				&& R1.HoldStepsLeft == R0.HoldStepsLeft && R1.RideStepsLeft == R0.RideStepsLeft && R1.RideSteps == R0.RideSteps;
			bFlagB = PB && PB->Serial == SerialB && PB->bBoarded && PB->bReplanAtLanding;
			bKeptC = PC && PC->Serial == SerialC && PC->Next == NextC && PC->Waypoints.Num() == WaysC;
			// A plans again on its next sub-step, round the corridor, and arrives without standing on an open cell.
			bArrA = Drive(HA, XA, Goal, -1);
			bOffA &= !OnOpenCell(XA.GetLocation());
			PA = Sim->GetRobotRoadPath(HA);
			SerialA2 = PA ? PA->Serial : -1;
			bNeverOpenA = PA && RestNeverOpen(*PA);
			for (StepsA = 1; StepsA < 3000 && !bArrA; ++StepsA) { bArrA = Drive(HA, XA, Goal, -1); bOffA &= !OnOpenCell(XA.GetLocation()); }
			// B rides on; the sub-step that lands it on -1 drops its trip, and the next plans again from the column.
			for (int32 K = 0; K < 5000 && Sim->LevelOfZ(XB.GetLocation().Z) != -1; ++K) { Drive(HB, XB, Goal, -1); }
			bDroppedAtLanding = Sim->LevelOfZ(XB.GetLocation().Z) == -1 && Sim->GetRobotRoadPath(HB) == nullptr;
			bArrB = Drive(HB, XB, Goal, -1);
			bOffB &= !OnOpenCell(XB.GetLocation());
			PB = Sim->GetRobotRoadPath(HB);
			SerialB2 = PB ? PB->Serial : -1;
			bNeverOpenB = PB && !PB->bBoarded && RestNeverOpen(*PB);
			for (StepsB = 1; StepsB < 3000 && !bArrB; ++StepsB) { bArrB = Drive(HB, XB, Goal, -1); bOffB &= !OnOpenCell(XB.GetLocation()); }
			Rides = Sim->GetRidesBoarded() - Rides0;
			// C keeps its trip over the hall's box cells and arrives.
			for (StepsC = 0; StepsC < 3000 && !bArrC; ++StepsC) { bArrC = Drive(HC, XC, HallGoal, -3); }
		}
		Expect(bOk && bCrossA && bDroppedA && SerialA2 > 0 && SerialA2 != SerialA && bNeverOpenA && bOffA && bArrA,
			FString::Printf(TEXT("AT14 a walker mid-trip on an upper floor: the Hauler on (1,0) of -1 bound for (5,0) had planned across the box; the open drops its trip, its next sub-step plans again (serial %d -> %d) round the corridor, never through an open cell, and it arrives in %d sub-steps without standing on one"),
				SerialA, SerialA2, StepsA));
		Expect(bOk && bCrossB && bRideKept && bFlagB && bDroppedAtLanding && SerialB2 > 0 && SerialB2 != SerialB && bNeverOpenB && bOffB && bArrB && Rides == 1,
			FString::Printf(TEXT("AT14 a rider bound for an upper floor: the Hauler riding shaft 0 from -5 to -1 (%d of %d ride sub-steps left; its trip then crossed the box) keeps its ride through the open, flagged; the sub-step that lands it drops its trip and the next plans again from the column (serial %d -> %d), never through an open cell; it arrives in %d sub-steps, boarding %d time(s) in all"),
				RideLeft, RideAll, SerialB, SerialB2, StepsB, Rides));
		Expect(bOk && bKeptC && bArrC,
			FString::Printf(TEXT("AT14 the hall floor never opens: the Excavator crossing -3's box cells from (1,0) to (4,0) keeps its trip through the open (serial %d, the same waypoints) and arrives in %d sub-steps"),
				SerialC, StepsC));
	}
```

Why each premise holds, from the code:
- **A's first plan is the direct same-dig leg** (`PlanDeepRoute` `:1324-1330`; AT8 measures 4,000 cm). Its waypoints are (2,0), (3,0), (4,0), then the goal, so `RestOnBox` is true. One sub-step moves it 40 cm (4 m/s x 0.1 s), so it still stands on (1,0) at the open. Its re-plan therefore starts off the void, which makes "never stands on an open cell" exact for it.
- **B's trip** walks to the -5 column, rides (`LegEdge` < 0, `Levels` -1), then `ColumnToCell` walks the -1 landing's field across the box (`:1475-1487`). Ride edges link every pair of portals on a shaft (`EnsureDeepNet` `:1184`), so it is one ride.
- **C's legs are all on -3**, which is `C.Bottom`, the hall. The hall is never open (`DeriveAtriumCells` `:102-105`).
- **`Rides == 1`**: A and C never board.

## X1.6 Mutations

Each mutation is built, run under `-atriums` and restored by hash, as TR19 did (`docs/design-decisions.md:1140`). **MX1 and MX2 are required**: one for each new code path. MX3-MX5 pin the scope decisions and are recommended.

| Id | Mutation | Fails |
|---|---|---|
| **MX1** | Delete the `DropDeepPathsOverAtrium(*C);` call in `ExecuteOpenAtrium` | AT14 (a): A keeps serial `SerialA` and walks (2,0), an open cell. Also (b): B is never flagged. |
| **MX2** | Delete the `if (Path->bReplanAtLanding) { RobotPaths.Remove(Entity); }` in `MoveAlongDeep` | AT14 (b): B lands with its stale trip (`bDroppedAtLanding` false) and walks onto (2,0). |
| MX3 | The literal rule: test the robot's own floor, `LevelOfZ(PosCm.Z)` from `ForEachRobotState`'s position, instead of its remaining legs | AT14 (b): B stands on -5 at the open, is not flagged, and walks the box after landing. |
| MX4 | Drop a boarded robot at once (`RobotPaths.Remove` without the `bBoarded` branch) | AT14 (b): `bRideKept` is false at the open, and B boards twice (`Rides` 2). |
| MX5 | Count the hall: `Path->Levels[k] >= C.Bottom` | AT14 (c): C's trip is dropped and its serial changes. |

## X1.7 Risk

- **Over-reach (accepted).** A robot walking an upper floor far from the box also plans again. It takes the same route (opening cells only lengthens other routes) or an equal-cost tie, at one `PlanDeepRoute` each, at a rare player action.
- **A robot standing ON an open cell at the open** still leaves the void by D7's `Resolve` rule (P2, `RHSimTransit.cpp:1281-1286`). Its first leg crosses open cells to the nearest floor cell, as AT8's parked robots do. This follow-up does not change that, and AT14 keeps A off the box so its check is exact.
- **Pointer lifetime.** In the ride-end branch, `RobotPaths.Remove` must be the last use of `Path`.
- **Real robots only.** The pass sees `RobotHandles`, so the `-transit` suite's made-up handles are never seen. No caller opens an atrium with them; a future suite that does must use real robots, as AT14 does.
- **Determinism, era parity, save.** Spawn order, and each robot is decided by its own trip. `RobotPaths` is never read by the era band and never saved. No stock, ledger, air or Hope value moves, so AT10 and the `eraparity` suite are unchanged.
- **Caches.** The pass calls no `Ensure*`, so AT0's and AT11's build counts and `-scale`'s "rebuild once" hold.
- **Presentation (unchanged code).** A dropped walker's figure re-syncs on the new `Serial` (`RHAgentVisualizerSubsystem.cpp:804-806`), and a deep robot is drawn at its sim position (`:1703-1726`), so nothing pops. A flagged rider is drawn riding until it lands.
- **Scheduling.** X1 adds no `URHSimWorldSubsystem` data member, but its method is one more body for T1's rewriter. Build it after T1 (and T2), re-anchored by symbol (`FRHRobotRoadPath`, `MoveAlongDeep`'s ride-end branch, `ExecuteOpenAtrium`, AT13's block).

## X1.8 Gate plan and effort (one paragraph)

DEFER X1 to its own sim gate, after the orchestrator amends ruling 21 as in X1.2 and after T1 (whose prep is ready and waits for James's go).
- **Build:** the three source edits (about 45 lines in `RHSimWorldSubsystem.h`, `RHSimAtrium.cpp` and `RHSimTransit.cpp`), AT14 (about 110 lines), and the docs. No save version, frame, row or string.
- **Gate:**
  - the battery (64 runs), line-identical to `battery_post_idle` apart from `atriums.log`'s AT14 lines and END count (106 -> 109) and the known wall-clock digits;
  - `-atriums` under MX1-MX5, each failing only its row and passing again when restored by hash;
  - the 110-sol playtest byte-identical;
  - one click run (115) and the deep-view scene (9), for regression only;
  - `-scale` once, with its result lines unchanged;
  - five read-only lenses.
- **Effort:** about half a day of builder time plus about 3-4 hours of gate time (the battery about 20 minutes; each mutation is a sim rebuild plus a one-minute suite).

---

# X2. Commuter shuttles are drawn as boxes

## X2.1 How shuttles are drawn now

A pure listener draws commuting in `URHTrafficVisualizerSubsystem` (`Source/RedHope/Public/RHTrafficVisualizerSubsystem.h:14-28`): "COMMUTER SHUTTLES AND CARS. One instanced layer per part, a transform per vehicle a frame. Per district pair (home, work) with Flow f: ceil(f / 20) shuttles, or one small car when f < 5; the whole screen is capped at 400 vehicles". The constants are `ShuttleSeats` 20, `SmallFlow` 5 and `MaxVehicles` 400 (`.h:79-81`); the four layers are declared at `.h:111-114`.

**The parts are engine cubes** (`Source/RedHope/Private/RHTrafficVisualizerSubsystem.cpp:25-29`):
```cpp
	// The shuttle and the car, in metres (the cube is 1 m a side).
	const FVector ShuttleBodyM(7.0, 2.3, 2.2);
	const FVector ShuttleBandM(5.9, 2.36, 0.62);
	const FVector CarBodyM(3.8, 1.7, 0.85);
	const FVector CarCabinM(1.9, 1.55, 0.62);
```
`EnsureLayers` (`:172-202`) builds every layer from `/Engine/BasicShapes/Cube.Cube` on a dynamic instance of `M_Graybox`:
```cpp
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RedHope/Art/M_Graybox.M_Graybox"));
	...
		Layer->SetCastShadow(false); // small, fast and many: the shadow is not worth 400 casters
	...
	ShuttleBody = MakeLayer(TEXT("ShuttleBody"), FLinearColor(0.70f, 0.80f, 0.90f), FLinearColor(0.05f, 0.09f, 0.14f));
	ShuttleBand = MakeLayer(TEXT("ShuttleBand"), FLinearColor(0.02f, 0.04f, 0.06f), FLinearColor(0.10f, 0.45f, 0.55f));
```
In short, a pale blue-grey box with a dark, teal-glowing slab for its window band.

**Each frame** (`UpdateVehicles`, `:426-481`) every vehicle is placed along its route by time, with a fade that shrinks it over the last 12 m of an underground end. A shuttle takes two transforms (`:462-467`):
```cpp
		if (V.bShuttle)
		{
			const FVector Base(Pos.X, Pos.Y, Ground + RoadLiftCm + 20.f * S + (float)ShuttleBodyM.Z * 50.f * S);
			SBody.Add(FTransform(Rot, Base, ShuttleBodyM * S));
			SBand.Add(FTransform(Rot, Base + FVector(0.f, 0.f, 52.f * S), ShuttleBandM * S));
		}
```
These go to the GPU through `BatchUpdateInstancesTransforms` (`:475-479`). Instances are created parked in `RebuildVehicles` (`:409-421`), and phases are hashes of (home, work, k): no random draw. The layers hide underground and in the section (`:690-700`, the list at `:697`).

**Measured** (C6b, `9745c51`'s message): in a 3,000-colonist, 20-district city at 1600x900, 135 shuttles took the median frame from 28.6 ms (no traffic) to 30.9 ms.

**The record:**
- C6b's own follow-up: "Shuttles are plain instanced boxes, with no art yet."
- The visual-pass design's follow-up list (`docs/qa/2026-10-25/visual-pass-design.md:164`): "Commuter shuttles and cars are `/Engine/BasicShapes/Cube` instances on `M_Graybox` ... They wait on L3."
- L3 is an open director look question (`docs/qa/2026-10-21/living-city-handplay-card.md:303`: "**L3.** Shuttle art: what should a commuter shuttle be? Today it is a box (C6b, 9745c51)."). The UI-polish DIRECTOR ANSWER of 2026-10-24 parked it: "The optional art job Q waits for L3 (what a commuter shuttle should be) and L4 (wheels)" (`docs/qa/2026-10-24/ui-polish-design.md:149`). No later answer exists (`git grep -P "\bL3\b" -- docs` finds only these and an unrelated build-log L3).

## X2.2 What art exists (listed only; no large file opened)

**`Content/RedHope/Art/Models2/` vehicle kits.** Each kit is `<Kit>/<Name>/{StaticMeshes/<Name>, StaticMeshes/MI_<Name>, Materials, Textures/<Name>_BaseColor, _MetallicRoughness}`, with an emissive mask in `Art/Masks/` and a normal in `Art/Normals/`:
- `Rover/` (2.6 MB): the C6b cargo rover, a 6-wheel flatbed, 5.0 x 2.6 x 2.4 m, accent `#D9822B`. Built by `scripts/blender/rh_build_rover.py` (207 lines) and drawn by `RHAgentVisualizerSubsystem.cpp:28`.
- `TramCar/` (6.1 MB) and `TramJoint/` (460 KB): the tram people-mover. Built by `scripts/blender/rh_build_tram.py` (685 lines), a 5.71 x 2.668 x 3.20 m car, 7,748 tris with LODs at 35 % and 10 % (commit `f922b3d`), loaded by `RHTramVisualizerSubsystem.cpp:23`.
- `SupplyShip/` (8.3 MB) and `Lander/` (19 MB): spacecraft (`rh_build_supplyship.py`, `rh_build_lander.py`).
- **No shuttle, bus or car asset exists anywhere in `Content`** (`find Content -iname "*shuttle*" -o -iname "*bus*" -o -iname "*vehicle*"` finds none).

**`Martians/`** (art sources, gitignored, `.gitignore:24`):
- `gen/hs/`: the outputs of the code-built hard-surface lane, one folder per asset (GLB, BaseColor, MetallicRoughness, EmissiveMask, flat normal, preview GLB). Vehicles: `Rover/` (6.3 MB), `RH_TramCar/` (14 MB), `RH_TramJoint/` (520 KB), `SupplyShip/` (39 MB), `Lander/` (32 MB). No shuttle.
- `gen/objects-redo/`: Nano Banana object sheets of 2026-07-17 (`raw/`, `cutout/`, a contact PNG of about 1 MB each). Per `gen/object_roster.json`, the vehicle-like ones are:
  - `ScoutSpeeder`: "a sleek low single-seat rover-craft with a bubble canopy";
  - `HeavyFreighter`: "a heavy interplanetary freighter spaceship";
  - `CargoLander`.

  None is a commuter shuttle.
- `glb_out/`: July Hunyuan outputs, `ScoutSpeeder.glb` (6.7 MB), `HeavyFreighter.glb` (7.6 MB), `CargoLander.glb` (37 MB). These are the image-to-3D generation the 10-13 triage rejected for hard-surface machinery.
- **No shuttle source exists.**

## X2.3 What a real shuttle needs

**1. The director's answer to L3** (a look question; lettered, with a recommendation):
- **(a) Recommended:** a low, rounded people-mover in the tram's family. Bone-white body, slate underframe, a warm-lit window band, the transit amber `#F3C459` as trim only, four fat tyres and a destination board. The tram's script already chose that amber "so lifts and trams read as one transit family" (`rh_build_tram.py`); shuttles would join it.
- **(b)** A rugged six-wheeled crew bus in the cargo rover's language (`#D9822B` trim, hazard stripe, light bar): it reads as a colony work vehicle.
- **(c)** Keep the boxes and close the follow-up.
- **Sub-question:** the commuter cars are cubes too (`CarBodyM`, `CarCabinM`). Recommend that the same job builds a small two-seat runabout in the same family.
- **Optional, on James's go only:** concept sheets so he judges pictures, not words. With `nano-banana` (Vertex `gemini-3-pro-image-preview`, about $0.15-0.25 a 4K image per the skill), two looks x 4 views comes to about $2 and about 30 minutes with strip and contact sheet.

**2. An asset to a geometry contract** that the visualizer can trust without rescaling, as the tram's is (`rh_build_tram.py`, "GEOMETRY CONTRACT"):
- `RH_Shuttle` at true scale in metres, with the pivot on the ground (bounds centred on XY, tyre soles at z = 0) and the **front at +X**, the travel direction. `EvalRoute` yaws the vehicle to its direction of travel (`:219-222`); today's cube is symmetric, a kit is not.
- Footprint 7.0 x 2.3 m, as today's body. With `LaneCm` 150 (`:18`) the two directions then still clear each other on the 6 m road (edges 0.35-2.65 m from the centre line). Height about 2.6-2.8 m.
- Tier S (`docs/art-bible.md` section 4, "vehicles", ≤ 20 k tris). Because up to 400 draw at once, LOD0 ≤ about 8 k (the tram car's 7,748), LOD1 35 % at screen size 0.25 and LOD2 10 % at 0.08 (the tram's `RH_LODS="0.35@0.25,0.10@0.08"`).
- One material on `M_RH_Master` (`MI_RH_Shuttle`), with BaseColor + MR + an emissive mask: the window band glows warm white, the tram's single glow hue. Accent 5-15 % of the visible surface (art bible 2.3).

**3. A lane to make it.** The task names the sprite-to-mesh lane; the repo's own record favours the code-built lane for this kind of object. Both are given so the orchestrator can choose.

- **Lane A, recommended: code-built hard-surface (local Blender, no GPU).**
  - A new `scripts/blender/rh_build_shuttle.py` on `rh_hs.Kit` (`scripts/blender/rh_hs.py`: bevelled primitives in art-bible role materials; baked BaseColor with chipping, AO grime and dust; an MR map; an emissive mask "by construction"). The tram's single loft of rounded-rect sections gives a smooth people-mover shell, and it ray-casts the accent share on every build.
  - Output goes to `Martians/gen/hs/RH_Shuttle/`. Import with `scripts/unreal/rh_import_hs.py` (`RH_LODS`; replace-in-place; masks; Nanite off), then verify from a fresh process with `scripts/unreal/rh_mesh_stats.py`.
  - Why this lane:
    - The 10-13 triage rebuilt every "weird" model in code, because image-to-3D hard-surface output showed "melted detail, shredded fins, holes, a function tint over the whole hull" (`docs/PROJECT_STATUS.md:399-404`).
    - Every vehicle in the game is a code build (Rover, TramCar, SupplyShip, Lander).
    - The script rebuilds its outputs byte-identically from git (the tram's receipt in `f922b3d`).
  - A Nano Banana sheet, if James wants one, becomes the approved reference, as every 10-13 rebuild had one.
- **Lane B: the sprite-to-mesh pipeline on the Lambda GPU box** (per the skills' own files):
  1. **`nano-banana`:** a hero view, then views anchored to it, on a flat grey background with no ground or shadow, 4K 4:3. Strip (`nb_strip.py`), make a contact sheet (`nb_contact.py`), then run `sprite-qa`.
  2. **The box:** launched through Manifold (`launch_gpu`, then `wait_for_launch`, 15-40 minutes to boot), then `lambda-bootstrap` if recycled (weights persist on NFS) and `~/.config/rh3d/host.sh`.
  3. **`gen-3d`:** Hunyuan3D 2.1 shape (`rh_shape.py`), about 31 s to load and 84 s to infer on the A10. The raw mesh is 300 k-700 k tris.
  4. **`paint-3d`:** Hunyuan3D 2.1 paint (`rh_paint.py`). It needs an **A100 40 GB** (paint peaks past the A10's 23 GB), takes about 167 s on first load and about 66 s per texture, and pre-remeshes to about 40 k tris. Then `obj2glb.py`.
  5. **Cleanup:** `mesh-cleanup` to about 18 k tris keeping UVs, then down to about 8 k for 400 instances (`scripts/blender/rh_finish.py`: weld, smooth by angle, decimate to tier).
  6. **Masks:** cut a window-glow emissive mask by hand (`scripts/gpu/rh_cut_masks.py`), because the paint stage makes none.
  7. **Import and wire:** `scripts/unreal/rh_import_model2.py`, then the `RHArtWire` pass, with import before wiring (`scripts/blender/README.md`).
  8. **Terminate** after `sync_outputs`.

  Risks:
  - the triage's melt on exactly this kind of object, made worse by the extra decimation;
  - the outputs cannot be reproduced from the repo (`scripts/gpu/lane/README.md`: "Anything that runs on a rented box and is not reproducible from this repo is one `terminate` away from gone");
  - the outputs live only in `Martians/`, whose `gen/` has no second copy until James's backup drive arrives (handoff Follow-ups).

**4. The visualizer change** (presentation only; `RHTrafficVisualizerSubsystem.cpp/.h`; about 50 lines):
- **Load the kit with fallback**, as the tram does (`RHTramVisualizerSubsystem.cpp:235-249`: `LoadObject<UStaticMesh>(..., LOAD_NoWarn | LOAD_Quiet)`, then one Warning and the old look if missing):
  - Add `const TCHAR* ShuttleKitPath = TEXT("/Game/RedHope/Art/Models2/Shuttle/RH_Shuttle/StaticMeshes/RH_Shuttle.RH_Shuttle");`, Interchange's double-folder layout (`scripts/blender/README.md`).
  - In `EnsureLayers`, when the kit loads, `ShuttleBody` takes the kit mesh with the mesh's own material (no `M_Graybox` MID) and `ShuttleBand` is not created.
  - Otherwise, today's two cube layers plus one Warning: "Shuttle kit missing (...) - shuttles draw as boxes (scripts/blender/rh_build_shuttle.py, then scripts/unreal/rh_import_hs.py)". This is a log line, not a player-facing string.
  - A member `bool bShuttleKit` records which.
- **Per frame:** with the kit, one transform per shuttle, `FTransform(Rot, FVector(Pos.X, Pos.Y, Ground + RoadLiftCm), FVector(S))`. The ground pivot means no half-height lift, and the fade's uniform `S` shrinks the vehicle onto the road. There is no band transform. The parked instances in `RebuildVehicles` skip `SBand` likewise.
- **Unchanged:**
  - the section-hide list (`:697`), which already holds `ShuttleBody` and null-checks `ShuttleBand`;
  - counts, lanes, hash phases, fades, the 400 cap, no shadow, no collision;
  - the deck's "N shuttles and M cars" line (`RHCommandDeckPanels.cpp:2025`).
- **Cars:** the same pattern if (a)'s sub-question says yes (`CarBody` takes the kit, `CarCabin` is dropped).

**5. Proof (its own presentation gate, like the tram's `f922b3d`):**
- **Inertness:** the `RedHopeSim` dylib sha256 is unchanged. The battery is line-identical apart from the wall-clock digits. The 110-sol playtest is byte-identical.
- **Click test:** no step is added. One run for regression, or three if the orchestrator counts it as a UI change.
- **Captures before and after, from one command file:**
  - the staged commuting city, `docs/qa/2026-10-21/living-city-handplay-card.md:322-325` (appendix A2: `RH.CityBench 3000`, 15 habitats, the ring road and 15 spokes, 20 districts, `RH.Traffic home Habitat`, `RH.Traffic noon`, `RH.Cam 0 0 0.6 38 0`), with `RH.Snapshot` at the street framing;
  - a close shuttle;
  - the same frame with `RH.Traffic overlay 1`, since the heat wash must still read under the new vehicles.
- **The kit-missing fallback**, exercised once.
- **A frame check** at the 400 cap in that city under rulings 18, 22, 24 and 27: the median-frame delta ≤ 1.0 ms on quiet pairs, with one fixed window offset.

## X2.4 Effort estimate

| Step | Lane A (code-built, recommended) | Lane B (sprite-to-mesh) |
|---|---|---|
| L3 to the director | one lettered question; optional sheets about $2 and 30 min (James's go) | the sheet is lane B's input, so it is required |
| The mesh | `rh_build_shuttle.py`, about 350-500 lines (the rover's 207, the tram's 685); about 0.5-1 day; local Blender, no GPU spend | an A100 session: boot 15-40 min, about 2 min of GPU a variant for shape plus 1-4 min for paint, 2-4 variants; cleanup and decimation to about 8 k; about 1 day, plus a likely redo if the triage's melt shows |
| Import | `rh_import_hs.py` with `RH_LODS`; fresh-process stats; about 0.5 h | `rh_import_model2.py`, `RHArtWire`, a hand-cut emissive mask; about 1-2 h |
| Visualizer | about 50 lines; 1-2 h | the same |
| Cars (if wanted) | +0.5 day | +0.5 day plus GPU |
| Gate | presentation gate: captures, the frame check (up to 45 min a scene under ruling 18), five lenses, 1-3 rounds; about 0.5-1 day | the same |
| **Total** | **about 2-3 days elapsed, no GPU** | **about 3-4 days elapsed, plus an A100 session; higher redo risk** |

## X2.5 Plan (one paragraph)

DEFER X2. It cannot start before the director answers L3, and it needs new content, which this display-only gate may not add.
1. Put L3 to James as above: (a) a tram-family people-mover (recommended), (b) a rover-family crew bus, or (c) keep the boxes. Ask whether cars come too, and offer the optional ~$2 Nano Banana sheets so he judges pictures.
2. On (a) or (b), one presentation-only gate:
   - `rh_build_shuttle.py` (and the car, if wanted) on the `rh_hs` kit, to the geometry contract in X2.3;
   - import through `rh_import_hs.py` with the tram's LODs;
   - swap the shuttle's two cube layers for one kit layer with a cube fallback, the kit assets in the same commit as the visualizer change (the visual pass's rule, `visual-pass-design.md:160`);
   - prove it with before/after captures of the staged city, a frame check at the 400 cap, and the inertness proofs.
3. Use the GPU lane only if the director picks a look the kit cannot model, and keep its outputs backed up.

---

## Notes for the orchestrator

- **X1 needs a ruling first.** Amend ruling 21 as in X1.2: remaining walk legs on the new atrium's upper floors, with a boarded robot planning again at its landing. The literal "standing on" rule misses riders and transfer walks and would double-charge boarded robots. The amendment is a technical scope ruling, not a director question.
- **X2 needs a director answer:** L3, lettered options in X2.3.
- The convoy follow-up (engine cubes) is the `meshes` key's M2. It is not covered here.
- **Uncertain, to check in X2's gate:** whether a plain `UInstancedStaticMeshComponent` picks LODs per instance in UE 5.8. The handoff records that `r.ForceLOD` does not reach instanced meshes and `foliage.ForceLOD` does, so `foliage.ForceLOD 2` must visibly change the shuttles. Do not switch to HISM for 400 moving instances without measuring, because per-frame transform updates rebuild its tree. The frame check decides.
