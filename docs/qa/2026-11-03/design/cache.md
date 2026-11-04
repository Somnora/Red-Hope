# P2 design, key `cache`: the idle pad cache is rebuilt only when its key changes

Designer: read-only. HEAD `6df7fcc6a30920e30aca294db3bb5473aa6ee592`, tree clean (`git status --short` empty, `git diff --stat HEAD -- Source` empty).
All line numbers are at HEAD. Every claim below was read from the code or from the idle gate's logs, which are cited.

**Recommendation: GO.** Display only. It changes `Source/RedHope/Private/RHAgentVisualizerSubsystem.cpp` and `Source/RedHope/Public/RHAgentVisualizerSubsystem.h` (new members; the header cannot be avoided). Nothing under `Source/RedHopeSim`, `docs/data` or `Content/RedHope/Data` changes. No player-facing string: only a `UE_LOG` line, the `RH.IdleRobots` output and a cvar help text, all developer-facing. No random draw. No per-frame planning or allocation: the per-tick work is an integer compare, and the rebuild, which allocates, runs only when the key changes. Click steps added: 0. Nothing changes on screen (section 5).

---

## 1. What the code does today

### 1.1 Tick calls the cache refresh on every tick (`RHAgentVisualizerSubsystem.cpp:1638-1646`)

```cpp
1638	RebuildObstacles();
1639	++TickSerial;
1640	// Idle robots: with rh.RobotRouting 0 every figure is drawn at its sim position, so neither the pad cache nor the docked robots are kept.
1641	const bool bIdleRouting = CVarRobotRouting.GetValueOnGameThread() != 0;
1642	if (bIdleRouting)
1643	{
1644		RefreshIdlePads();
1645		GatherDocked(EntityManager);
1646	}
```

### 1.2 `RefreshIdlePads`: the key, the once-a-second timer, the serial (`:380-416`, body to `:497`)

```cpp
380	void URHAgentVisualizerSubsystem::RefreshIdlePads()
381	{
382		UWorld* World = GetWorld();
383		const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
384		const URHDefinitionsSubsystem* Defs = World ? World->GetSubsystem<URHDefinitionsSubsystem>() : nullptr;
385		if (!Sim || !Defs)
386		{
387			return;
388		}
389		const TArray<FRHBuildingInstance>& Buildings = Sim->GetBuildings();
390		// What the cache depends on: the building count, every surface pad's state (built, switched on) and which surface buildings are
391		// still sites. A change rebuilds it at once; otherwise once a second of real time.
392		uint32 Key = GetTypeHash(Buildings.Num());
393		for (const FRHBuildingInstance& B : Buildings)
394		{
395			if (B.Level != 0)
396			{
397				continue;
398			}
399			if (B.DefName == ChargePadName)
400			{
401				Key = HashCombine(Key, HashCombine(GetTypeHash(B.Id), (B.bUnderConstruction ? 1u : 0u) | (B.bManualOff ? 2u : 0u) | 4u));
402			}
403			else if (B.bUnderConstruction)
404			{
405				Key = HashCombine(Key, GetTypeHash(B.Id));
406			}
407		}
408		const double Now = FPlatformTime::Seconds();
409		if (Key == IdlePadsKey && Now < NextIdlePadsS)
410		{
411			return;
412		}
413		IdlePadsKey = Key;
414		NextIdlePadsS = Now + 1.0;
415		++IdlePadsSerial;
416		IdlePads.Reset();
417		const FRHBuildingInstance* Lander = Buildings.FindByPredicate([](const FRHBuildingInstance& B) { return B.DefName == LanderName && B.Level == 0; });
	... (418-496: for each finished, switched-on surface ChargePad: 20 slots tested against every Obstacles box, the near scan over
	     every building, the inspection stand points tested against every Obstacles box; unchanged by this design)
```

The rebuild at `:409-416` runs when the key differs **or** a second of real time has passed. So it also runs once a second when nothing changed, and bumps `IdlePadsSerial` each time (`:415`). Today's key is also inexact. It is a 32-bit hash of the total count, the pads and the sites only. If one finished non-pad building is removed and another finished non-pad building is added on the same tick, the hash does not change, and only the timer covers that case.

### 1.3 `ChooseIdlePad`: every bump sends every idle figure through (`:565-647`)

```cpp
565	void URHAgentVisualizerSubsystem::ChooseIdlePad(int32 i, FRobotTrip& T, const FVector& SimPos)
566	{
567		// A figure walking to or standing at its slot (not one away inspecting: it is checked when it walks back) makes way when a robot docks
568		// within IdleDockClearCm of that slot: it chooses again at once, as if its slot were built over.
569		const auto MakesWay = [&T](const FIdlePad* Pad)
570		{
571			return Pad && T.IdleSlot != INDEX_NONE && (T.IdlePhase == 0 || T.IdlePhase == 3) && Pad->NearDock(T.IdleSlot);
572		};
573		if (T.PadChoiceSerial == IdlePadsSerial && !MakesWay(T.IdlePadId != 0 ? IdlePads.Find(T.IdlePadId) : nullptr))
574		{
575			return; // chosen against this cache already
576		}
577		T.PadChoiceSerial = IdlePadsSerial;
578		T.IdleRankCm = ParkSlotCm(i);
	...
583		if (!Sim || !Sim->FindNearestChargePad(SimPos, PadId, PadCm, 0) || !IdlePads.Contains(PadId))
	...
588		if (Pad && PadId == T.IdlePadId && T.IdleSlot != INDEX_NONE && Pad->SlotOpen[T.IdleSlot] && !MakesWay(Pad))
589		{
590			// The same pad and slot: an inspection whose building is gone from the list ends, and the figure walks back.
	...
596			return;
597		}
598		// A new pad (or none), its slot built over or beside a docked robot, or a full ring last time: the lowest open slot no other waiting
599		// figure holds (one drawn idle on this tick or the last) and no docked robot stands within IdleDockClearCm of, and a first wait staggered
600		// by the robot's index so the waiting robots do not all leave together (InspectWaitS once it has inspected). When the choice really
601		// changes (another pad, or another slot) the walk there is planned afresh (bFree off), so a new slot near the last walk's goal is still
602		// reached. Round 2: the same choice again (the Lander rank again, or the same pad's ring still full: a figure in the rank meets this on
603		// every once-a-second cache rebuild) keeps its walk, so its goal, ParkSlotCm(i), is replanned only by FreeWalkTo's 100 cm rule (design
604		// point 6).
	... (605-647: the new-choice block; it scans Trips for held slots and allocates TArray<bool> Held at 622)
```

`ChooseIdlePad` is called on every tick for every figure drawn idle (`AdvanceDrawn`'s idle branch, `:1081-1086`):

```cpp
1081		if (T.IdleTick == 0 || T.IdleTick + 1 < TickSerial)
1082		{
1083			ResetIdle(T); // a new idle spell: nothing is held from an earlier one
1084		}
1085		ChooseIdlePad(i, T, SimPos);
1086		T.IdleTick = TickSerial;
```

After each bump, line 573's test fails for every idle figure on that same tick. Each one then runs `ParkSlotCm(i)` (`:335-378`: a pass over the buildings for the Lander, then `Obstacles` box tests), `FindNearestChargePad` (`RHSimWorldSubsystem.cpp:13393-13432`: a pass over every building) and `IdlePads.Contains/Find`. A figure that cannot take the keep path at `:588` also runs the new-choice block, with its `Trips` scan and the `Held` allocation. That figure is in the Lander rank, either with no working pad or with a full ring.

**Measured rate on HEAD's binary** (idle gate round 2 logs; that build has the same visualizer as HEAD). `RH.IdleRobots` printed serial 30, 59, 99, 134 and 151 at 30, 60, 100, 135 and 152 s of `cap_day.cmds` (`wip-backups/idle-work/r2/cap/logs/idle_r2_after_day.idlerobots.txt`). All six frame-check windows printed serial 151 at 153 s (`r2/frame2/frame_r2q1_w*_*.log`). So the serial rises about once a second, and with it every idle figure's `ChooseIdlePad`.

### 1.4 What that once-a-second pass changes, when nothing in the key changed

Each case below was checked against `:565-647`.

| Figure | What the once-a-second pass does today | Visible effect |
|---|---|---|
| At or walking to its slot, or out inspecting (same pad) | `ParkSlotCm` and `FindNearestChargePad` run again (same answers), then the keep path at `:588`. The inspection-list check at `:591` finds the same list. | None |
| In the rank, no working pad (`IdlePadId` 0) | The new-choice block resets `IdlePhase`, `bAtSlot`, `IdleWaitS` and `IdleWaitForS` (to the same formula). `bFree` is kept because there was no old slot (`:616`). | None: a rank figure never reads those fields (`:1091-1095`) |
| In the rank because its pad's ring was full (`bRingFull`) | Looks for a free slot again. A slot freed meanwhile is taken. | **Yes: the only real work** |
| Making way (`MakesWay`) | Not tied to the serial: checked every tick at `:573` | None (unchanged) |

So the timer does one useful thing: it gives a figure in a full ring a fresh look once a second. The design keeps that look, for those figures only.

### 1.5 The other code touched

`ResetTracking` (`:1187-1196`):
```cpp
1191		// Idle robots: the pad cache is rebuilt from the reloaded colony on the first tick (NextIdlePadsS 0 is always due), whatever it held.
1192		IdlePads.Reset();
1193		IdlePadsKey = 0;
1194		NextIdlePadsS = 0.0;
1195		RouteLogTickMs.Reset(); // round 2: a reload starts the tick timer's samples afresh
```
`LogIdleRobots` header line (`:700`):
```cpp
700		UE_LOG(LogRedHope, Display, TEXT("[RH.IdleRobots] %d working surface pad(s) in the cache (serial %u)"), PadIds.Num(), IdlePadsSerial);
```
The route-log line and the samples (`:2038-2042`, `:2051-2054`):
```cpp
2038			UE_LOG(LogRedHope, Display, TEXT("[RobotRoute] %d robots: %d drawn inside a footprint not theirs, %d sim inside one; drawn-sim gap mean %.0f cm max %.0f cm; %d parked (routing %s); idle: %d waiting at a pad, %d at the Lander rank, %d inspecting; tick median %.3f ms over %d frame(s)"),
2039				LogN, LogDrawnIn, LogSimIn, LogN ? LogGapSum / LogN : 0.f, LogGapMax, LogParked,
2040				CVarRobotRouting.GetValueOnGameThread() != 0 ? TEXT("on") : TEXT("off"),
2041				LogAtPad, LogAtRank, LogInspecting, MedianMs, Sorted.Num());
2042			RouteLogTickMs.Reset();
	...
2051		if (CVarRobotRouteLog.GetValueOnGameThread() == 0)
2052		{
2053			RouteLogTickMs.Reset(); // round 2: samples from before the log was switched off never join a later line's median
2054		}
```
The cvar help (`:108-112`) ends "... and the median of this subsystem's own Tick time since the last line.".
Header (`RHAgentVisualizerSubsystem.h:244-246`, `:263-266`):
```cpp
244		// Idle robots look busy: the working surface Charging Pads (the ones FindNearestChargePad docks at), each with its ring of 20 waiting
245		// slots and the buildings near it to inspect. Rebuilt once a second of real time, or at once when the building count, a surface pad's
246		// state or the set of surface sites changes; a rebuild bumps IdlePadsSerial and every waiting figure chooses its pad again.
	...
263		TMap<int32, FIdlePad> IdlePads;
264		uint32 IdlePadsKey = 0;
265		uint32 IdlePadsSerial = 0;
266		double NextIdlePadsS = 0.0;
```
`FRobotTrip::PadChoiceSerial` (`.h:223`, "the pad cache (IdlePadsSerial) it last chose against") and `RouteLogTickMs` (`.h:274`) stay as they are.

---

## 2. What the cache and a pad choice read

Traced to the code. This is what the key must cover.

1. **`FindNearestChargePad(SimPos, Id, Loc, 0)`** (`RHSimWorldSubsystem.cpp:13393-13432`) reads, for each building, `bUnderConstruction`, `bManualOff`, `DefName == ChargePad`, `Level == 0`, `LocationCm` (XY) and `Id`, in array order (the first nearest wins a tie). Its Lander fallback reads the same fields. Whether the fallback answers or not, `ChooseIdlePad` maps it to PadId 0, the rank (`:583-586`), so the Lander's on/off state never matters. It does **not** read `bPowered`: a shed pad still docks.
2. **`RebuildObstacles`** (`:197-218`, every tick before the refresh) reads every `Level == 0` building, sites included: `DefName`'s Defs row, `SizeX`, `SizeY` and `LocationCm`. `Obstacles` feeds `SlotOpen` (`:442-449`), the stand points (`:477-484`) and `ParkSlotCm` (`:364-371`).
3. **`RefreshIdlePads`' body** (`:417-496`) reads the Lander (`Level 0`), each working pad's footprint and location, and every surface building's `bUnderConstruction`, `Id` and `LocationCm` (the 20 m list, ascending Id).
4. **`ParkSlotCm(i)`** (`:335-378`) reads the first `Lander` in `Buildings`, its footprint and `Obstacles`.
5. **A building's identity fields never change after placement.** The only writer of `Id`, `DefName`, `LocationCm`, `Level`, `SizeX` and `SizeY` is `AddBuilding` (`RHSimWorldSubsystem.cpp:10726-10788`; `Id = NextBuildingId++` at 10729). The only other source is a load, which replaces the list (`:14598` `Buildings.Reset()`, `:14646` `Buildings.Add`) and then broadcasts `OnColonyReloaded` (`:15416`), which calls `ResetTracking` (`.cpp:1183`). Every other mutation in `Source/` was checked:
   - `bManualOff` is written only by `SetManualPower` (`:2673-2684`).
   - `bUnderConstruction` goes from true to false only at completion (`:13354`; set true only in `AddBuilding`, `:10758`).
   - Removal is `RemoveBuilding`'s `Buildings.RemoveAt(Index)`, "order-preserving: Buildings stays Id-ascending" (`:11205`).
   - `FindBuilding` (mutable) is private (`RHSimWorldSubsystem.h:3425`; `private:` from `:3323`), so the presentation cannot write a building.
   - BM-7's Move moves furnishings and fittings, not buildings (`RHCheats.cpp:2146-2147`).
6. **Defs footprints are never written at runtime.** `GetFootprintHalfCm` (`:8831-8835`) reads `FootprintX` and `FootprintY`. The only in-memory def cheats are `RH.SetCirculates` and `RH.SetShieldTax` (`RHCheats.cpp:2227`, `:2261`), and they write other fields.
7. **A figure's `SimPos` is fixed during an idle spell.** The sim moves no robot with `TaskType` None: `RHRobotTaskProcessor.cpp:164` only tries to claim work. The wander processor moves only `FRHWanderFragment` entities, the stress dummies. Any sim move at more than 15 cm/s, or any task, resets `IdleS`, and `ResetIdle` follows (`.cpp:943-958`). A new spell starts with `PadChoiceSerial` 0 and chooses afresh.
8. **Not inputs of the cache.** These are dynamic and are already handled every tick:
   - `DockedCm` (`GatherDocked`, every tick) and `MakesWay` (every tick, `:573`);
   - held slots (scanned at each choice);
   - `bPowered`;
   - deep (`Level != 0`) buildings: nothing above reads them for a surface walker.

**Conclusion.** A surface building's `Id` stands for its kind, place, size and floor. The cache and every pad choice are then a function of: the ordered list of surface buildings' Ids, each one's site flag, and each Charging Pad's switched-off flag.

---

## 3. The key

One entry per surface building (`Level == 0`), in the sim's array order:

```
entry = (uint64)(uint32)Id << 2  |  (bUnderConstruction ? 1 : 0)  |  (bManualOff && DefName == ChargePad ? 2 : 0)
```

- It is compared **in place, entry by entry**, against the stored `TArray<uint64>`. That is exact: no hash, so no collision argument is needed.
- No allocation per tick: the compare only reads.
- It is stricter than today's key. Any add, remove, finish or pad switch on the surface changes it, including the remove-plus-add on one tick that today's hash misses (1.2).
- Deep buildings and non-pad switches are left out. Nothing reads them (section 2).
- A load always rebuilds through `bIdlePadsBuilt = false` (4.5). A reload of the same colony gives the same key, and the cache emptied by `ResetTracking` must still be refilled.

---

## 4. The exact change

### 4.1 `.cpp`: the key entry helper. Insert after line 130 (`const FName LanderName(TEXT("Lander"));`), inside the same anonymous namespace

```cpp
	// P2: one surface building's entry in the idle pad cache's key: its Id, bit 0 a construction site, bit 1 a Charging Pad switched off.
	// A building's kind, place, size and floor never change once it is placed (URHSimWorldSubsystem::AddBuilding), so its Id stands for all four.
	uint64 IdlePadKeyEntry(const FRHBuildingInstance& B)
	{
		return ((uint64)(uint32)B.Id << 2) | (B.bUnderConstruction ? 1u : 0u) | ((B.bManualOff && B.DefName == ChargePadName) ? 2u : 0u);
	}
```

### 4.2 `.cpp`: `RefreshIdlePads`. Replace lines 380-416 (through `IdlePads.Reset();`) with the block below; lines 417-497 are unchanged

```cpp
void URHAgentVisualizerSubsystem::RefreshIdlePads()
{
	bIdleRetryDue = false;
	UWorld* World = GetWorld();
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	const URHDefinitionsSubsystem* Defs = World ? World->GetSubsystem<URHDefinitionsSubsystem>() : nullptr;
	if (!Sim || !Defs)
	{
		return;
	}
	const TArray<FRHBuildingInstance>& Buildings = Sim->GetBuildings();
	// P2: once a second of real time a figure sent to the Lander rank by a full ring looks again (ChooseIdlePad); nothing is rebuilt then.
	const double Now = FPlatformTime::Seconds();
	if (Now >= NextIdleRetryS)
	{
		NextIdleRetryS = Now + 1.0;
		bIdleRetryDue = true;
	}
	// The key (P2): one IdlePadKeyEntry per surface building, in the sim's order (Buildings is Id-ascending and a removal keeps the order):
	// its Id, whether it is a site, and for a Charging Pad whether it is switched off. That is everything the cache and a figure's pad choice
	// read: the pads FindNearestChargePad docks a surface robot at (finished, switched on, Level 0), every surface footprint (Obstacles: the
	// open slots, the stand points and ParkSlotCm's rank), the Lander, and which surface buildings are finished (the inspection lists).
	// Compared in place on every tick, with no allocation; the cache is rebuilt, and IdlePadsSerial bumped, only when the key differs, and on
	// the first tick after a load (bIdlePadsBuilt).
	bool bSame = bIdlePadsBuilt;
	int32 NumSurface = 0;
	for (int32 b = 0; bSame && b < Buildings.Num(); ++b)
	{
		if (Buildings[b].Level == 0)
		{
			bSame = IdlePadsKey.IsValidIndex(NumSurface) && IdlePadsKey[NumSurface] == IdlePadKeyEntry(Buildings[b]);
			++NumSurface;
		}
	}
	if (bSame && NumSurface == IdlePadsKey.Num())
	{
		return;
	}
	IdlePadsKey.Reset();
	for (const FRHBuildingInstance& B : Buildings)
	{
		if (B.Level == 0)
		{
			IdlePadsKey.Add(IdlePadKeyEntry(B));
		}
	}
	bIdlePadsBuilt = true;
	++IdlePadsSerial;
	IdlePads.Reset();
```

Notes for the builder:
- `-Wshadow`: the loop variable `b` and the range-for `B` both go out of scope before the existing loops at `:418` and `:457` and the lambda at `:417`, so nothing shadows. `Now` is declared once, as before.
- `IdlePadsKey.Add` can grow the array only on a rebuild. `Reset()` keeps the capacity.

### 4.3 `.cpp`: `ChooseIdlePad`'s early-out. Replace lines 573-577 with

```cpp
	// P2: the cache is rebuilt only when its key changes, so a figure sent to the Lander rank by a full ring (bRingFull: no slot free of another
	// waiting figure and clear of a docked robot) looks again on the once-a-second tick instead (bIdleRetryDue), as every figure did before: a
	// slot freed by a figure taking a job, or by a docked robot leaving, is taken within a second.
	const bool bChosen = T.PadChoiceSerial == IdlePadsSerial;
	const bool bMakeWay = bChosen && MakesWay(T.IdlePadId != 0 ? IdlePads.Find(T.IdlePadId) : nullptr);
	const bool bRetry = bChosen && T.bRingFull && bIdleRetryDue;
	if (bChosen && !bMakeWay && !bRetry)
	{
		return; // chosen against this cache already
	}
	// Developer counters (RH.IdleRobots, rh.RobotRouteLog): what sent the figure through.
	if (T.PadChoiceSerial == 0)
	{
		++IdleChoiceRuns.NewSpell;
	}
	else if (!bChosen)
	{
		++IdleChoiceRuns.AfterRebuild;
	}
	else if (bMakeWay)
	{
		++IdleChoiceRuns.MakingWay;
	}
	else
	{
		++IdleChoiceRuns.FullRingRetry;
	}
	T.PadChoiceSerial = IdlePadsSerial;
```

`MakesWay` is still evaluated only when the serial matches, the same short-circuit as `:573` today, so the per-tick cost is unchanged. `bRingFull` is true only after a pad's ring had no slot (`:640`); it is false for a figure with no working pad (`:609`, then the return at `:620`). The rest of the function (`:578-647`) is unchanged. A full-ring figure's look once a second runs exactly today's path: a same full ring keeps its walk (`:643`), and a found slot starts a new walk.

Replace the comment at lines 602-604 with:
```cpp
	// reached. Round 2: the same choice again (the Lander rank again, or the same pad's ring still full: a figure in the rank meets this on
	// every cache rebuild, and one sent there by a full ring on its once-a-second look, P2) keeps its walk, so its goal, ParkSlotCm(i), is
	// replanned only by FreeWalkTo's 100 cm rule (design point 6).
```

### 4.4 `.cpp`: `LogIdleRobots`' header line. Replace line 700 with

```cpp
	UE_LOG(LogRedHope, Display, TEXT("[RH.IdleRobots] %d working surface pad(s) in the cache (serial %u, keyed on %d surface building(s)); pad choices so far: %u (%u new idle spell, %u after a rebuild, %u making way, %u full ring once a second)"),
		PadIds.Num(), IdlePadsSerial, IdlePadsKey.Num(), IdleChoiceRuns.Total(), IdleChoiceRuns.NewSpell, IdleChoiceRuns.AfterRebuild,
		IdleChoiceRuns.MakingWay, IdleChoiceRuns.FullRingRetry);
```
The prefix "`in the cache (serial N`" is kept, so one regex reads HEAD's line and the new one (section 6).

### 4.5 `.cpp`: `ResetTracking`. Replace lines 1191-1194 with

```cpp
	// Idle robots: the pad cache is emptied and rebuilt from the reloaded colony on the first tick (bIdlePadsBuilt false), whatever its key:
	// a reload of the same colony leaves the key as it was, and the emptied cache must still be refilled (P2). IdlePadsKey is left as it is
	// (the rebuild overwrites it). The full ring's once-a-second look restarts; IdlePadsSerial and the choice counters run on.
	IdlePads.Reset();
	bIdlePadsBuilt = false;
	NextIdleRetryS = 0.0;
	bIdleRetryDue = false;
```
`IdlePadsKey` is deliberately not reset here. The flag alone forces the rebuild, which makes the reload path a single mechanism that one mutation can show (M3, section 7).

### 4.6 `.cpp`: the route-log line, its baselines and the cvar help

Replace lines 2038-2042 with:
```cpp
		// P2: the idle pad cache since the last line: its rebuilds, and the pad choices made (and how many a rebuild sent through).
		UE_LOG(LogRedHope, Display, TEXT("[RobotRoute] %d robots: %d drawn inside a footprint not theirs, %d sim inside one; drawn-sim gap mean %.0f cm max %.0f cm; %d parked (routing %s); idle: %d waiting at a pad, %d at the Lander rank, %d inspecting; tick median %.3f ms over %d frame(s); pad cache: %u rebuild(s), %u pad choice(s) (%u after a rebuild)"),
			LogN, LogDrawnIn, LogSimIn, LogN ? LogGapSum / LogN : 0.f, LogGapMax, LogParked,
			CVarRobotRouting.GetValueOnGameThread() != 0 ? TEXT("on") : TEXT("off"),
			LogAtPad, LogAtRank, LogInspecting, MedianMs, Sorted.Num(),
			IdlePadsSerial - RouteLogSerialAt, IdleChoiceRuns.Total() - RouteLogChoicesAt.Total(), IdleChoiceRuns.AfterRebuild - RouteLogChoicesAt.AfterRebuild);
		RouteLogTickMs.Reset();
		RouteLogSerialAt = IdlePadsSerial;
		RouteLogChoicesAt = IdleChoiceRuns;
```
Replace lines 2051-2054 with:
```cpp
	if (CVarRobotRouteLog.GetValueOnGameThread() == 0)
	{
		RouteLogTickMs.Reset(); // round 2: samples from before the log was switched off never join a later line's median
		RouteLogSerialAt = IdlePadsSerial; // P2: nor do the pad cache's rebuilds and pad choices
		RouteLogChoicesAt = IdleChoiceRuns;
	}
```
Line 111 (the cvar help) gets this appended before the closing `")`: ` Each line also gives how many times the idle pad cache was rebuilt, and how many pad choices the idle figures made, since the last line.`

The new text is appended **after** "`frame(s)`". The frame analyzer's regex (`r2/frame2/analyze.py`, `ROUTE = ... tick median ([0-9.]+) ms over (\d+) frame`) is not anchored at the end, so it still matches; this was tested with python on a sample line.

### 4.7 Header

Replace `.h:244-246` with:
```cpp
	// Idle robots look busy: the working surface Charging Pads (the ones FindNearestChargePad docks at), each with its ring of 20 waiting
	// slots and the buildings near it to inspect. Rebuilt only when its key changes (P2; IdlePadsKey: a surface building placed, removed or
	// finished, or a Charging Pad switched off or on) and on the first tick after a load; a rebuild bumps IdlePadsSerial and every waiting
	// figure chooses its pad again. Everything it holds is derived from that key (the surface buildings, their Defs rows, constants): nothing
	// may be added to it that depends on a robot's position, a cvar or another visualizer's actors, or it goes stale.
```
Replace `.h:264-266` with:
```cpp
	TArray<uint64> IdlePadsKey;   // the key the cache was built from: one IdlePadKeyEntry per surface building, in the sim's order
	bool bIdlePadsBuilt = false;  // false before the first build and after a load (ResetTracking): the next tick rebuilds whatever the key
	uint32 IdlePadsSerial = 0;    // bumped by every rebuild (RH.IdleRobots prints it)
	double NextIdleRetryS = 0.0;  // the next once-a-second tick (real time): a figure sent to the rank by a full ring looks again on it
	bool bIdleRetryDue = false;   // this tick is one (RefreshIdlePads sets it, ChooseIdlePad reads it)
	// Developer counters (P2; RH.IdleRobots and rh.RobotRouteLog's line; never reset): ChooseIdlePad's runs past its early-out, by what
	// sent the figure through.
	struct FIdleChoiceRuns
	{
		uint32 NewSpell = 0;      // the first choice of an idle spell
		uint32 AfterRebuild = 0;  // the cache was rebuilt since the figure last chose
		uint32 MakingWay = 0;     // a robot docked within IdleDockClearCm of its slot
		uint32 FullRingRetry = 0; // its pad's ring had no free slot: the once-a-second look
		uint32 Total() const { return NewSpell + AfterRebuild + MakingWay + FullRingRetry; }
	};
	FIdleChoiceRuns IdleChoiceRuns;
```
After `.h:274` (`TArray<double> RouteLogTickMs;`), insert:
```cpp
	FIdleChoiceRuns RouteLogChoicesAt; // P2: IdleChoiceRuns at the last rh.RobotRouteLog line, or on the last tick with the log off
	uint32 RouteLogSerialAt = 0;       // IdlePadsSerial at the same moment
```
These are plain members of the `UCLASS`, with no `UPROPERTY`, like `FRobotTrip` and `FIdlePad`.

### 4.8 Not changed

`AdvanceDrawn`, `FreeWalkTo`, `PlanRoute`, `ParkSlotCm`, `GatherDocked`, `NearDock`, `ResetIdle`, `GetIdleDebug`, `RebuildObstacles`, `RHCheats.cpp` (the `RH.IdleRobots` cheat only calls `LogIdleRobots`), `RHClickTest.cpp`, and the sim.

---

## 5. Behaviour: what changes on screen

Nothing, by the table in 1.4:

| Figure | Today | After |
|---|---|---|
| Slot holder, inspecting, walking back | a no-op pass once a second | no pass |
| In the rank, no working pad | a no-op pass once a second | no pass |
| In the rank, full ring | looks again once a second | looks again once a second (`bIdleRetryDue`), same path |
| Making way | every tick | every tick |
| New idle spell | chooses at once | chooses at once |
| Key change (pad placed, removed, finished or switched; any surface building placed, removed or finished) | rebuild at once and every figure chooses | rebuild at once and every figure chooses |
| Load | first tick rebuilds (timer 0) | first tick rebuilds (`bIdlePadsBuilt`) |

`ParkSlotCm(i)` (`IdleRankCm`) now runs only at a choice. It depends only on the key's inputs and `i` (section 2). `Tracked` indices are stable: `TrackEntities` appends, and only `ResetTracking` clears. So it gives the same value it gave once a second before. `GetIdleDebug` still computes `RankCm` live (`:658`), so the click step's `GoalCm == RankCm` test (`RHClickTest.cpp:4039`) holds as before.

---

## 6. Proof: fewer `ChooseIdlePad` runs, and no stale cache

### 6.1 The counters
- `RH.IdleRobots`' header line gives the serial, the key's size and the cumulative choice counts by cause.
- Every `[RobotRoute]` line gives the rebuilds and choices since the last line.
- Today the serial rises once a second (1.3), and each rise sends every idle figure through. After the change, a line with no building change in it reads `pad cache: 0 rebuild(s), N pad choice(s) (0 after a rebuild)`, where N counts only new idle spells, figures making way and full-ring looks.

### 6.2 A log-only capture session: `cap_cache.cmds` (scene name `cache`)

It runs on the `idle_day` save beside `cap_day.cmds` and `cap_night.cmds`, with the same capture script, at 1440x860. Its one snapshot only ends the run; the frame is not judged. One line:

```
r.setres 1440x860w, RH.ClearTips, RH.After 2 RH.Load idle_day, RH.After 2.5 RH.SetSpeed 1, RH.After 3 RH.Floor 0, RH.After 3.5 rh.RobotRouteLog 1, RH.After 4 RH.Cam 8 14 0.12 0 0, RH.After 4 RH.Lag 1, RH.After 30 RH.IdleRobots, RH.After 60 RH.IdleRobots, RH.After 61 RH.Spawn Stockpile -4 12, RH.After 64 RH.IdleRobots, RH.After 65 RH.Power 6 0, RH.After 65 RH.Power 26 0, RH.After 68 RH.IdleRobots, RH.After 78 RH.IdleRobots, RH.After 79 RH.Power 6 1, RH.After 79 RH.Power 26 1, RH.After 82 RH.IdleRobots, RH.After 83 RH.Demolish 27, RH.After 90 RH.IdleRobots, RH.After 92 RH.Load idle_day, RH.After 96 RH.IdleRobots, RH.After 97 RH.Snapshot 0.5
```

Why each beat, and what was checked:
- **`idle_day`**: 26 buildings, 7 robots, 0 tasks, two working pads #6 at (10, 20) m and #26 at (26, -4) m (`idle_r2_after_day.log`). It has **no building change of its own in its first 155 s**. The only sim building events in round 2's day capture are the commanded ones at 75, 110 and 137 s. The six frame-window logs (load to about 155 s) show no building event after "Colony loaded".
- **30 s to 60 s**: nothing changes, so the serial must hold (cache.quiet).
- **61 s `RH.Spawn Stockpile -4 12`**: `RHCheats.cpp:39-54` places a finished building through `Debug_PlaceInstant`, and the sim logs "Built Stockpile #27 at (-4, 12) m" (`RHSimWorldSubsystem.cpp:10804`). Id 27 is inferred from round 2, where the Stockpile ordered at 137 s on the same save became #27 (`NextBuildingId` is saved). The checker treats any other Id as an incomplete session, not a check result.
  - The spot is free in `idle_day`: round 2's `RH.Build` there passed placement.
  - It is 16.1 m from pad #6's centre, inside `InspectRadiusCm` 2000, and 34 m from #26.
  - The Stockpile is 3x3 (`RH_Buildings.csv`), so its clearance box is x -7.9..-0.1 m, y 8.1..15.9 m. That box closes none of pad #6's ring slots (its nearest point is 10.9 m from the centre) and none of the Lander rank slots (x -7.5 m, y at most 7.7 m).
  - Expected: pad #6's line names `#27`, either inspected or "in range dropped".
- **65 s, both pads off**: every idle figure goes to the Lander rank. Round 2 showed 4 rank figures and 3 busy in that state. **68 s to 78 s is the case no frame window measured: every idle figure in the rank.** The serial must hold across it.
- **79 s, both on**: 2 pads again.
- **83 s `RH.Demolish 27`**: uplink, landing after `RH.Lag 1`. `CanDemolishBuilding` (`:10878-...`) refuses only the Lander, pylons, an anchoring Borer and a habitat with residents or machines. The sim logs "Demolished Stockpile #27 ..." (`:11296`).
- **92 s, `idle_day` loaded again**: the colony is the same as the save. The spawned #27 is gone, the pads are back on, and no site was ever made. So the key is unchanged, and only `bIdlePadsBuilt` refills the emptied cache.

Cost: about 30 s of boot, 97 s of commands and 4 s of grace. Pass `rh_capture.sh` a timeout of 200 s.

### 6.3 The checker: `cache_check.py <log> <before|after>`

The builder puts it in the P2 work folder, not the repo. Run it with `python3 -I`. Its logic was run against synthetic before, after and M1-M3 logs and gave the expected verdicts below. Its parsing was run against round 2's real day log.

```python
#!/usr/bin/env python3
"""P2 cache item: cache.quiet, cache.fresh and cache.reload over one cap_cache.cmds log (the idle_day save).
usage: cache_check.py <log> <before|after>   (before: HEAD's binary, whose lines carry no pad cache counts)"""
import calendar
import re
import sys

TS = re.compile(r'^\[(\d{4})\.(\d\d)\.(\d\d)-(\d\d)\.(\d\d)\.(\d\d):(\d{3})\]')
HEAD = re.compile(r'\[RH\.IdleRobots\] (\d+) working surface pad\(s\) in the cache \(serial (\d+)'
                  r'(?:.*?pad choices so far: (\d+) \((\d+) new idle spell, (\d+) after a rebuild)?')
PAD = re.compile(r'\[RH\.IdleRobots\]\s+pad #(\d+) at ')
RANK = re.compile(r'\[RH\.IdleRobots\] (\d+) surface walker\(s\): (\d+) busy, (\d+) waiting at a pad, (\d+) at the Lander rank')
ROUTE = re.compile(r'\[RobotRoute\].*pad cache: (\d+) rebuild\(s\), (\d+) pad choice\(s\) \((\d+) after a rebuild\)')
EVENT = re.compile(r'LogRedHopeSim: Display: (Construction site: |Built \w+ #\d+|\w+ #\d+ construction complete|'
                   r'\w+ #\d+ switched (?:ON|OFF)|Demolished \w+ #\d+|Colony loaded: )')
# (name, pattern, which match): the session's five building changes, in order.
STEPS = (('the spawn', r'Built Stockpile #27\b', 0), ('the switch off', r'ChargePad #\d+ switched OFF', 0),
         ('the switch on', r'ChargePad #\d+ switched ON', 0), ('the demolition', r'Demolished Stockpile #27\b', 0),
         ('the reload', r"Colony loaded: slot 'idle_day'", -1))
WHEN = ('30 s', '60 s', '64 s', '68 s', '78 s', '82 s', '90 s', '96 s')


def ts(line):
    m = TS.match(line)
    if not m:
        return None
    y, mo, d, h, mi, s, ms = (int(x) for x in m.groups())
    return calendar.timegm((y, mo, d, h, mi, s)) + ms / 1000.0


def main(path, which):
    reports, routes, events = [], [], []
    cur = None
    for k, ln in enumerate(open(path, errors='replace').read().splitlines()):
        m = HEAD.search(ln)
        if m:
            cur = dict(k=k, pads=int(m.group(1)), serial=int(m.group(2)), rank=None,
                       after=None if m.group(5) is None else int(m.group(5)), padlines={})
            reports.append(cur)
            continue
        if cur is not None and '[RH.IdleRobots]' in ln:
            p = PAD.search(ln)
            if p:
                cur['padlines'][int(p.group(1))] = ln
            w = RANK.search(ln)
            if w:
                cur['rank'] = int(w.group(4))
            continue
        cur = None
        r = ROUTE.search(ln)
        if r:
            routes.append(dict(k=k, t=ts(ln), at=ln[1:24], rebuilds=int(r.group(1)), choices=int(r.group(2)), after=int(r.group(3))))
        e = EVENT.search(ln)
        if e:
            events.append(dict(k=k, t=ts(ln), text=ln[ln.index('Display: ') + 9:]))
    step = {}
    for name, pat, nth in STEPS:
        hits = [e for e in events if re.search(pat, e['text'])]
        if hits:
            step[name] = hits[nth]
    missing = [name for name, _, _ in STEPS if name not in step]
    if len(reports) != len(WHEN) or missing or (which == 'after' and not routes):
        print(f'cache: session incomplete ({len(reports)} RH.IdleRobots report(s), want {len(WHEN)}; no log line for '
              f'{", ".join(missing) or "none missing"}; {len(routes)} [RobotRoute] line(s) with the pad cache counts)')
        return 2
    R = reports
    nxt = lambda e: next((r for r in routes if r['k'] > e['k']), None)
    # cache.quiet: the cache is rebuilt only when a building changes.
    why = []
    for a, b, wa, wb in zip(R, R[1:], WHEN, WHEN[1:]):
        n = sum(1 for e in events if a['k'] < e['k'] < b['k'])
        if b['serial'] - a['serial'] > n:
            why.append(f'serial {a["serial"]} at {wa} -> {b["serial"]} at {wb} with {n} building event(s) between')
        if which == 'after' and n == 0 and b['after'] != a['after']:
            why.append(f'choices after a rebuild {a["after"]} at {wa} -> {b["after"]} at {wb} with no building event between')
    for r in routes if which == 'after' else []:
        if r['rebuilds'] > 0 and not any(r['t'] - 4.0 <= e['t'] <= r['t'] for e in events):
            why.append(f'{r["at"]}: {r["rebuilds"]} rebuild(s) with no building event in the 4 s before')
        if r['after'] > 0 and r['rebuilds'] == 0:
            why.append(f'{r["at"]}: {r["after"]} choice(s) after a rebuild in a line with no rebuild')
    print('cache.quiet ' + ('PASS' if not why else f'FAIL ({len(why)}): ' + '; '.join(why[:6])))
    ok = not why
    # Reported, not judged: every idle figure in the Lander rank (both pads off, 68 s to 78 s), the case no frame window measured.
    inside = [r for r in routes if R[3]['k'] < r['k'] < R[4]['k']]
    print(f'cache.rank (report): {R[4]["rank"]} figure(s) in the Lander rank at 78 s; serial {R[3]["serial"]} -> {R[4]["serial"]}'
          + (f'; [RobotRoute] lines between: {sum(r["rebuilds"] for r in inside)} rebuild(s), {sum(r["after"] for r in inside)}'
             f' pad choice(s) after a rebuild, {sum(r["choices"] for r in inside)} in all' if which == 'after' else ''))
    # cache.fresh: a building placed, a pad switched off and on, a building demolished: each seen at once.
    why = []
    if not re.search(r'#27\b', R[2]['padlines'].get(6, '')):
        why.append('64 s: pad #6 does not name Stockpile #27 (spawned 16.1 m from its centre at 61 s)')
    for i in (3, 4):
        if R[i]['pads'] != 0:
            why.append(f'{WHEN[i]}: {R[i]["pads"]} working pad(s) in the cache, both pads switched off at 65 s')
    if R[5]['pads'] != 2:
        why.append(f'82 s: {R[5]["pads"]} working pad(s) in the cache, both switched on at 79 s')
    if re.search(r'#27\b', R[6]['padlines'].get(6, '')):
        why.append('90 s: pad #6 still names Stockpile #27 after its demolition')
    for name in ('the spawn', 'the switch off', 'the switch on', 'the demolition') if which == 'after' else ():
        r = nxt(step[name])
        if r is None or r['rebuilds'] < 1:
            why.append(f'the first [RobotRoute] line after {name} reads no rebuild')
    print('cache.fresh ' + ('PASS' if not why else f'FAIL ({len(why)}): ' + '; '.join(why)))
    ok = ok and not why
    # cache.reload: a reload of the same colony (same key) still refills the emptied cache.
    why = []
    if R[7]['pads'] != 2:
        why.append(f'96 s: {R[7]["pads"]} working pad(s) in the cache after idle_day was loaded again at 92 s (it has 2)')
    if R[7]['serial'] <= R[6]['serial']:
        why.append(f'serial {R[6]["serial"]} at 90 s -> {R[7]["serial"]} at 96 s: no rebuild across the reload')
    if which == 'after':
        r = nxt(step['the reload'])
        if r is None or r['rebuilds'] < 1:
            why.append('the first [RobotRoute] line after the reload reads no rebuild')
    print('cache.reload ' + ('PASS' if not why else f'FAIL ({len(why)}): ' + '; '.join(why)))
    return 0 if ok and not why else 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1], sys.argv[2]))
```

The checker relies on two formats:
- The sim's own building log lines: `AddBuilding` `:10799` and `:10804`, completion `:13382`, `SetManualPower` `:2681`, `RemoveBuilding` `:11296`, load `:15421`. Every key change has one, because every writer in section 2 logs.
- `RH.IdleRobots`' pad lines naming buildings as `#Id`. The `walks` item may extend those lines, but must keep `#Id` naming.

**Expected verdicts:**

| Run (label) | cache.quiet | cache.fresh | cache.reload | Notes |
|---|---|---|---|---|
| before, pre-step (c), HEAD's binary (`p2_before_cache`) | **FAIL** | PASS | PASS | Serial about +30 from 30 s to 60 s and about +10 from 68 s to 78 s, with no building event. This is the cost being fixed. The counter rules do not apply (no counts in HEAD's lines). |
| after, final binary (`p2_after_cache`) | PASS | PASS | PASS | Serial s0 at 30 s and 60 s, +1 at 64 s, +1 at 68 s (both switches land on one frame), the same at 78 s, +1 at 82 s, +1 at 90 s, +1 at 96 s. `cache.rank` shows about 4 figures in the rank with 0 rebuilds and 0 choices after a rebuild from 68 s to 78 s (HEAD: about 40 re-choices). |
| M1 (`p2_mut_cache_quiet`) | **FAIL** | PASS | PASS | |
| M2 (`p2_mut_cache_fresh`) | PASS | **FAIL** | PASS | |
| M3 (`p2_mut_cache_reload`) | PASS | PASS | **FAIL** | |

### 6.4 Free evidence from runs the gate makes anyway
- **`cap_day.cmds`, after**: both pads are off from 75 s to 110 s, with every idle figure in the rank. Its `[RobotRoute]` lines from 77 s to 109 s should all read `0 rebuild(s) ... (0 after a rebuild)`. HEAD's serial rose 59, 99, 134 over the same beats. Report it in the docs; it is not judged.
- **The frame check's final windows** (120-152 s on `idle_day`, no building change): every line reads `pad cache: 0 rebuild(s)` and `(0 after a rebuild)`. Recommended: the P2 copy of `analyze.py` sums both per final window and prints them beside the median. Expected 0 and 0; this is reported, not a bar. HEAD's lines have no counts.

---

## 7. Mutations: one change each, built and run as one `cap_cache` session each

Each mutation is restored by copying the file back with a fresh mtime, the sha256 is checked, and the final binary is rebuilt, as the gate requires. Each is visualizer-only: one compile and one session of about 2.5 min.

- **M1 (cache.quiet): the once-a-second rebuild back.** In 4.2, change `if (bSame && NumSurface == IdlePadsKey.Num())` to `if (bSame && NumSurface == IdlePadsKey.Num() && !bIdleRetryDue)`. Expected: cache.quiet FAIL, with serial +30 from 30 s to 60 s and about 40 `[RobotRoute]` lines with 2 rebuilds and no building event; cache.fresh and cache.reload PASS. That is HEAD's cadence with the new key: HEAD passed the idle gate this way, so nothing else fails.
- **M2 (cache.fresh): the key forgets the pad switch.** In 4.1, drop ` | ((B.bManualOff && B.DefName == ChargePadName) ? 2u : 0u)`. Expected: cache.fresh FAIL, because at 68 s and 78 s "2 working pad(s) in the cache, both pads switched off at 65 s", and the first lines after the switch off and on read no rebuild; cache.quiet and cache.reload PASS. The same mutation would also fail `robots.idle`'s existing Switch OFF goal check (`RHClickTest.cpp:4038-4040`), because once the timer is gone that check depends on the key. Do not run the click scene for M2: there the fleet hold can let a site finish inside the 15 s check and hide it, while `idle_day` has no such event.
- **M3 (cache.reload): a reload does not force the rebuild.** In 4.5, delete `bIdlePadsBuilt = false;`. Expected: cache.reload FAIL, because at 96 s the cache shows 0 working pads, the serial does not rise, and no rebuild follows the reload; cache.quiet and cache.fresh PASS. The first load at 2 s still rebuilds, because the boot colony's key (Lander #1 only) differs from `idle_day`'s.

My recommendation is to run all three. M1 is the item's core claim. M2 and M3 are the two stale-cache paths the orchestrator named ("switched off", "a reload") that only this key now guards.

---

## 8. Frame-check impact

- **Per tick, the same order of work as today.** Today the tick walks every building with a `Level` test, an `FName` compare and a `HashCombine` for each pad and site. After the change it walks the surface buildings with a `Level` test, one `uint64` built and one compare each (the `FName` compare runs only when `bManualOff` is set), stopping at the first difference. Neither allocates. On `idle_day` that is 26 buildings, well under a microsecond. For scale, `RebuildObstacles` already does a Defs `TMap` lookup per surface building every tick (`:207-217`).
- **Removed, except on a key change.** In steady state these are gone:
  - the rebuild: per working pad, 20 slot tests against every `Obstacles` box, a near scan over every building, each near building against every box, and about 7 `TArray` allocations;
  - the per-figure `ChooseIdlePad` work: `ParkSlotCm`, `FindNearestChargePad` (O(buildings) each), map finds, and for rank figures the new-choice block with its `Held` allocation.
- **The median cannot show it.** The removed work falls on 1 tick in about 60. Round 2's windows read a tick median of 0.039-0.048 ms on both binaries (`r2/frame2/frame_analysis.txt`). This item's expected contribution to P2's median delta is 0.000 ms within noise, and it can only lower cost.
- **What the frame check could never show is what the counters show (6.3, 6.4).** In a big colony the once-a-second rebuild costs more than the choices. Estimated from the loop counts, not measured: with 300 surface buildings and 6 pads, about 6 × (20 × 300 + 300 + 30 × 300) ≈ 90,000 box tests, a once-a-second spike of a few tenths of a millisecond. After the change it happens only when a building changes.
- **No change to the frame-check protocol is needed.** Rulings 18, 22, 24, 25 and 27 are unchanged. The new route-line text is appended after "frame(s)", where the analyzer's regex still matches.

---

## 9. Stale-cache risks, case by case

| Case | Key entry | Result |
|---|---|---|
| A pad demolished (`RH.Demolish`, `RemoveBuilding`) | its Id leaves the list | rebuild on the next tick. The figures re-choose; another pad or the rank. Shown by `cap_cache`: the Stockpile demolition goes through the same Id-removal path (cache.fresh). |
| A pad switched off or on | bit 1 | rebuild at once (cache.fresh; M2) |
| A pad finished | bit 0 of its entry | rebuild at once; the pad enters the cache. Same mechanism as bit 1. No in-session cheat finishes a site, so it is argued from the code. |
| A building finished within 20 m of a pad | bit 0 | rebuild at once; the inspection lists gain it |
| A building placed (site or instant: uplink, `RH.Spawn`, growth hab modules `RHSimGrowth.cpp:1416`, `Debug_PlaceInstant`) | a new Id | rebuild at once: slots, stand points and the rank re-tested (cache.fresh) |
| One building removed and another added on one tick | different Ids | caught. Today's hash misses this when neither is a pad or a site. |
| A reload, including of the same colony | `bIdlePadsBuilt = false` | rebuild on the first tick (cache.reload; M3) |
| `rh.RobotRouting` 0 then 1 | the key is compared on the first tick back | correct. The figures start new spells (`IdleTick` stale). |
| A deep building, a non-pad switch, a power shed | not in the key | correct: nothing reads them (section 2). Today a deep build triggered a redundant rebuild. |
| A docked robot arriving or leaving; a slot freed by a holder | not cache state | `MakesWay` every tick; a full ring's once-a-second look. Unchanged. |
| An idle figure's sim position | fixed during an idle spell (2.7) | a new spell chooses afresh |
| A Defs footprint | never written at runtime (2.6) | none |
| Hash collision | none: exact compare | none |
| Array order | Id-ascending, removal order-preserving | a reorder with the same content would rebuild once, which is harmless |

**The one real new risk is data added to the cache later that is not a function of the key.** The timer used to hide such bugs within a second; now they last until the next building change. This matters most for the `walks` item:
- **W1's `KeepOutCm`** is "the pad's drawn radius (from the bounds of the mesh the visualizer draws for it)" under ruling 28. If W1 computes it in `RefreshIdlePads`, it must come from the asset (`LoadObject<UStaticMesh>` of `/Game/RedHope/Art/Models2/ChargePad/ChargePad/StaticMeshes/ChargePad.ChargePad`, then `GetBounds()` and the colony visualizer's min-side fit: the path at `RHColonyVisualizerSubsystem.cpp:2447`, the `LoadObject` at `:2473`, the bounds at `:2502` and the fit scale at `:2522-2523`), which is synchronous and deterministic, or be a constant. It must never come from a spawned actor or component, which may not exist yet on the first tick after a load, and never from a cvar such as `rh.ModelSetV2`, unless that cvar's value is added to the key.
- **W1's 150 cm from a docked robot** must read `DockedCm`, which `GatherDocked` refreshes every tick, at planning time. If W1 wants a walk re-planned when a robot docks mid-walk, it needs its own trigger, because the cache no longer sends figures through `ChooseIdlePad` once a second.
- **W2's distance bound** uses the sim position, which is fixed for a spell: fine.
- **W3's stand points and ring-slot skipping** are derived from pads and footprints, which are in the key: fine. Stand-point holding is per figure, not cache state.

---

## 10. Interactions with the other P2 items

- **walks (same two files)**: the constraints above. My names are new or renamed. New: `IdlePadKeyEntry`, `bIdlePadsBuilt`, `NextIdleRetryS`, `bIdleRetryDue`, `FIdleChoiceRuns`, `IdleChoiceRuns`, `RouteLogChoicesAt`, `RouteLogSerialAt`. `IdlePadsKey` changes type, and `NextIdlePadsS` is removed. My edit regions in the `.cpp` are 111, after 130, 380-416, 573-577 and 602-604, 700, 1191-1194, 2038-2042 and 2051-2054. W1-W3 edit 418-496 (stand points, slots), 578-647 (W2's bound) and `AdvanceDrawn`. If W2 adds an outcome to `ChooseIdlePad` such as "stay put", it needs no look once a second: only a key change or a new spell can change it.
- **meshes (same `.cpp`, cargo lump in `Tick` and `ResetTracking`'s lump loop)**: different lines.
- **clicktest**: no change needed. `robots.idle`'s Switch OFF and ON goal checks keep passing, because the switch is in the key and is seen at once. Those checks now also guard the key (M2 note).
- **sites-market**: no overlap.

---

## 11. For the builder's docs entry and the deviations list

- **Entry text (suggested):** "The idle pad cache (P2, the idle gate's known cost): rebuilt only when its key changes, never on a timer. The key is one entry per surface building (its Id, whether it is a site, and for a Charging Pad whether it is switched off), compared in place every tick, and a load forces a rebuild. A figure sent to the Lander rank by a full ring still looks again once a second. `RH.IdleRobots` and `rh.RobotRouteLog` count rebuilds and pad choices by cause. `cap_cache` shows the serial held from 30 s to 60 s and through 10 s with every idle figure in the rank (HEAD: +30 and +10), every building change seen at once, and a reload of the same colony refilling the cache. M1-M3 each fail exactly their check."
- **Supersedes** the idle entry's "Known cost" deviation bullet and the handoff follow-up "the once-a-second cache rebuild re-runs `ChooseIdlePad` for every idle figure".
- **Do not:** rebuild on a timer again, hash the key, add the Lander's or any non-pad building's `bManualOff` or `bPowered` to it, reset `IdlePadsKey` in `ResetTracking` (it makes M3 unprovable), move the `MakesWay` check behind the full-ring look, or change `PlanRoute`, `ParkSlotCm`, `FreeWalkTo` or any sim file.

## 12. Click steps, captures, timing

- Click steps added: **0**. Main scene 115 and deepbench 9 are unchanged.
- Captures: no visual change is expected. The `cap_day` and `cap_night` after-frames should show the same idle behaviour as before, apart from the other P2 items.
- `cap_cache` is log-only. Runs: before (pre-step (c)), after (step 9), and M1-M3, about 2.5 min each.
- `gate-d-wording.md` gains no block: no player-facing string is added.
