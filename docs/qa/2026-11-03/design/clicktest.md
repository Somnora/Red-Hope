# P2 design, key `clicktest`: test-strength fixes to the click step `robots.idle`

Read-only design, written 2026-11-03 at HEAD `6df7fcc` (working tree clean; `Source/RedHope/Private/RHClickTest.cpp` sha256 `34b690e2...`, the same file the idle gate's round 2 froze as its final test source). Every line number below is at `6df7fcc`. Every claim about the code was read from it, and every claim about behaviour was read from round 2's logs in `Saved/RHCapture/logs/idle_r2_*.log`.

Scope: the four follow-ups C1-C4 from the idle gate's review round 2 (`docs/design-decisions.md`, "Round 2's review", and the handoff's Follow-ups) and ruling 28 (`docs/qa/2026-10-26/preflight/ORCHESTRATOR-RULINGS.md`, item 28). The gate is display only. Nothing here touches `Source/RedHopeSim`, `docs/data` or `Content/RedHope/Data`. No player-facing string is added; the new lines are click-test log text, which is developer-facing. No random draw is used, and no route is planned (the click test plans none).

## 0. Summary

| Item | What | Rec. | Files (final build) | Main-scene steps | Mutation (label) |
|---|---|---|---|---|---|
| C1 | A figure first seen on the pad is tracked once it is seen off it (today it is skipped for its whole idle spell). The `FTest` comment is corrected. | GO | `RHClickTest.cpp` | 115 (no new step) | `p2_mut_c1`: robot 5 is staged as first seen on the pad, and its idle legs walk straight. FAIL "robot 5 came ... cm" |
| C2 | Long gaps are counted per leg. A round leg counts as the "floor exercised" proof only if none of its polls is over 200 cm from the last. Such legs are named in the line. | GO | `RHClickTest.cpp` | 115 | `p2_mut_c2`: polls only every 30th frame. FAIL "floor not exercised ... judged in part by end points only" |
| C3 | Just before Switch OFF, the watched robot must be drawn waiting at its slot, its goal that slot, with the pad on. Its `FIdleDebug` is recorded there. | GO | `RHClickTest.cpp` | 115 | `p2_mut_c3`: at 1x, a figure back at its slot after an inspection heads for the Lander rank. FAIL "just before Switch OFF robot 0 was not drawn waiting at its slot ..." |
| C4 | Ruling 28: the floor rises from PadHalf + 90 (290 cm) to KeepOutCm (350 cm for the 2x2 pad). The test works the number out itself and requires the visualizer's `KeepOutCm` to equal it, as it does for R1. | GO, only together with the walks design's W1 (it needs W1's `FIdleDebug::KeepOutCm` and W1's routing; if W1 is deferred, C4 is deferred) | `RHClickTest.cpp` (reads W1's new field) | 115 | `p2_mut_c4`, shared with W1: W1's keep-out routing removed. FAIL "robot 5 came ~315 cm ... floor, 350 cm" |

Can the step observe a job walk that starts from a ring slot (C4's second half)? **No.** See section 5.6. The step holds the fleet from setup to its report, so no robot can claim a job from a ring slot while the step runs. Ruling 28's own text then puts job walks on the captures.

No item adds a `Report` call. The main scene stays at **115** steps and the deepbench scene at 9. No world click is added, so `leak.total` stays 11 reached / 11 on purpose and `ExpectedWorld` is unchanged.

---

## 1. The step as it is at `6df7fcc`

### 1.1 The `FTest` idle fields (`RHClickTest.cpp:909-946`)

```cpp
		// Idle robots: the Charging Pad robots.idle setup places and the distances the test works out for it ON ITS OWN, from the placed
		// pad's footprint and the brief's numbers (R1 = PadHalf + 90 + 250, the floor PadHalf + 90), never from the visualizer; the robot
		// it watches and its trail, to tell when it stands still; every figure at the pad, for the floor (below); and what each part of the
		// step saw (Why: what went wrong).
		int32 IdlePad = 0;
		FVector IdlePadCm = FVector::ZeroVector;
		FVector2D IdlePadBoxCm = FVector2D::ZeroVector; // the pad's footprint half-axes plus 90: its clearance box, as the test works it out
		double IdlePadHalfCm = 0.0, IdleR1Cm = 0.0, IdleFloorCm = 0.0;
		...
		// Round 2: the floor holds for EVERY figure drawn waiting at or inspecting for the harness's pad, polled every frame through the wait and
		// the watch from its first sighting outside the pad's clearance box (one first seen inside it is walking off the pad: not tracked until
		// it has been seen away from the pad). A leg runs from where a figure was when its goal was set to that goal; a leg whose straight line
		// passes inside the floor had to go round the pad, and at least one such leg must be walked to its end while tracked, or the floor
		// was not exercised and the step fails.
		struct FIdleFig
		{
			bool bTracked = false;
			FVector LastCm = FVector::ZeroVector;    // drawn on the last poll: the segment since is measured, not just the point
			FVector GoalCm = FVector::ZeroVector;    // its leg's goal
			FVector LegFromCm = FVector::ZeroVector; // where the leg began (first sight, or where it stood when its goal changed)
			double NearestCm = 1.0e9;                // nearest the pad's centre since its first sighting
			double LegNearestCm = 1.0e9;             // on this leg
			double LegLineCm = 1.0e9;                // the leg's straight line, at its nearest the pad's centre
			bool bLegDone = false;                   // the leg was walked to its goal while tracked
		};
		TMap<int32, FIdleFig> IdleFigs;
		TSet<int32> IdleOnPad;                      // seen first inside the pad's clearance box on this sighting (walking off it)
		double IdleAllNearestCm = 1.0e9;
		int32 IdleAllNearestRobot = INDEX_NONE;
		int32 IdleLegs = 0, IdleRoundLegs = 0, IdleRoundCut = 0, IdleLongGaps = 0;
		FString IdleRoundSaid;                      // the first leg round the pad walked to its end
		FString IdleWhy, IdleSaid, IdleWatchSaid;
```

The comment at :924-925 says a figure first seen inside the box is "not tracked until it has been seen away from the pad". The code does not do this (1.3).

### 1.2 Setup's numbers (`robots.idle setup`, `:3633-3718`; the floor at `:3713-3714`)

```cpp
			T->IdleR1Cm = T->IdlePadHalfCm + 90.0 + 250.0;
			T->IdleFloorCm = T->IdlePadHalfCm + 90.0;
			Sim->SetFleetHold(true);
			PC->SetSimSpeed(3.f);
```

`IdlePadBoxCm = Half + (90, 90)` (`:3705`). `IdlePadHalfCm = max(Half)` (`:3704`). For the 2x2 pad these are 290 x 290 and 200, so R1 = 540 and the floor = 290.

### 1.3 `IdleTrackAll` (`:3719-3824`): the parts C1, C2 and C4 change

The first sighting (`:3743-3775`):

```cpp
				if (!bHere)
				{
					if (F && F->bTracked)
					{
						CloseLeg(*F);
						F->bTracked = false;
					}
					T->IdleOnPad.Remove(i);          // :3750 - the ONLY way out of IdleOnPad
					continue;
				}
				...
				if (!F || !F->bTracked)
				{
					if (T->IdleOnPad.Contains(i))
					{
						continue;                    // :3758-3761 - held for as long as it is drawn idle at the pad
					}
					if (FMath::Abs(P.X - C.X) < T->IdlePadBoxCm.X && FMath::Abs(P.Y - C.Y) < T->IdlePadBoxCm.Y)
					{
						T->IdleOnPad.Add(i); // walking off the pad (it stood on it when it went idle): not a figure coming to the ring
						continue;
					}
```

So C1 is real. Once a figure is in `IdleOnPad`, the `Contains` test at `:3758` skips it on every poll while it is drawn waiting at or inspecting for the pad. It leaves only at `:3750`, when it stops being drawn there. Its whole idle spell is never judged.

The segment measure (`:3776-3789`):

```cpp
					const double Seg = FVector::Dist2D(F->LastCm, P);
					const double Near = Seg <= 200.0 ? LineCm(F->LastCm, P) : Dist;
					T->IdleLongGaps += Seg > 200.0 ? 1 : 0;      // :3780 - one global count; no leg knows it had a long poll
					F->NearestCm = FMath::Min(F->NearestCm, Near);
					F->LegNearestCm = FMath::Min(F->LegNearestCm, Near);
					F->LastCm = P;
```

The leg's end (`:3802-3814`):

```cpp
				if (!F->bLegDone && FVector::Dist2D(P, F->GoalCm) <= 50.0)
				{
					F->bLegDone = true;
					const bool bRound = F->LegLineCm < T->IdleFloorCm;
					if (bRound)
					{
						++T->IdleRoundLegs;                       // :3808 - counted whatever its polls were
```

So C2 is real. A round leg judged partly by end points (a poll over 200 cm from the last) still raises `IdleRoundLegs`, and that count is the "floor exercised" proof.

### 1.4 The watch's verdict (`:3978-4006`)

```cpp
			bool bRoundOpen = false;
			for (const TPair<int32, FTest::FIdleFig>& E : T->IdleFigs)
			{
				bRoundOpen = bRoundOpen || (E.Value.bTracked && !E.Value.bLegDone && E.Value.LegLineCm < T->IdleFloorCm);
			}
			if (!bLast && !(T->IdleWatchSaid.Len() > 0 && T->IdleRoundLegs > 0 && !bRoundOpen))
			{
				return false;
			}
			...
			if (T->IdleAllNearestCm < T->IdleFloorCm)
			{
				T->IdleWhy += FString::Printf(TEXT(" robot %d came %.0f cm from the pad's centre (never under the floor, %.0f cm, for any figure at the pad);"), ...);
			}
			if (T->IdleRoundLegs == 0)
			{
				T->IdleWhy += FString::Printf(TEXT(" floor not exercised: of %d leg(s) tracked, none whose straight line passes inside %.0f cm of the centre was walked to its end (%d such leg(s) cut short, %s);"), ...);
			}
			T->IdleSaid += FString::Printf(TEXT("; %s; the floor over every figure at the pad: %d tracked, %d leg(s), %d round the pad walked to the end (first: %s), nearest %.0f cm (robot %d)%s"), ...,
				T->IdleLongGaps > 0 ? *FString::Printf(TEXT(" (%d poll(s) over 200 cm apart judged by their end point)"), T->IdleLongGaps) : TEXT(""));
			return true;
		}, 0.2f, 45.f);
```

### 1.5 Select, the goal checks and the presses (`:4008-4066`)

```cpp
		Runner.Do(TEXT("robots.idle select"), [T]()
		{
			if (ARHPlayerController* PC = GetPC())
			{
				PC->Debug_SelectBuilding(T->IdlePad);
				FrameCamera(T->IdlePadCm, 0.2f);
			}
		}, 0.8f);
		{
			// The goal check after each press: the pad's switch, then the watched robot's goal (rank slot when off, its ring slot when on).
			const auto GoalCheck = [T](const TSharedRef<FPress>& Press, bool bOff, const TCHAR* Label)
			{
				...
						URHAgentVisualizerSubsystem::FIdleDebug D;
						Viz->GetIdleDebug(T->IdleRobot, D);
						const bool bSwitched = Sim->IsManualOff(T->IdlePad) == bOff;
						const bool bGoal = bOff
							? (D.Show == URHAgentVisualizerSubsystem::EIdleShow::LanderRank && FVector::Dist2D(D.GoalCm, D.RankCm) < 1.0)
							: (D.Show == URHAgentVisualizerSubsystem::EIdleShow::AtPad && D.PadId == T->IdlePad && FVector::Dist2D(D.GoalCm, D.SlotCm) < 1.0);
				...
			};
			const TSharedRef<FPress> Off = PushPress(TEXT("robots.idle off"), LocateButton(TEXT("Switch OFF"), 0, true));
			Runner.Push(TEXT("robots.idle off goal"), GoalCheck(Off, true, TEXT("Switch OFF")), 0.3f, 15.f);
			const TSharedRef<FPress> On = PushPress(TEXT("robots.idle on"), LocateButton(TEXT("Switch ON"), 0, true));
			Runner.Push(TEXT("robots.idle on goal"), GoalCheck(On, false, TEXT("Switch ON")), 0.3f, 15.f);
		}
```

So C3 is real. The OFF check reads only the state after the press. Nothing in the step requires the watched robot to be at its ring slot after the watch. A figure already drawn in the Lander rank passes the OFF check without the press changing anything.

### 1.6 Harness timing facts

- `FRunner::Tick` (`:497-537`) is an `FTSTicker` core ticker. It runs a polling step's `Run` once per rendered frame. A gap ends on the wall clock and after `MinGapFrames` = 2 frames. `bLast` comes once `Elapsed >= Timeout` and at least `MinTimeoutFrames` = 30 frames have passed (`:528`).
- `PushPress` (`:761-785`) is three `Do` steps: `move` (0.15 s gap, records `LeakBase`), `down` (0.12 s) and `up` (0.2 s). In `idle_r2_click1` the pad switched OFF while the `up` was being handled (`07.03.52:025`, frame `[12]`, logged before the `up` line at `[13]`).
- `Report` (`:439-451`) is the only thing that counts toward `CLICKTEST TOTAL`. `Push` and `Do` steps never count.

### 1.7 Visualizer facts the design relies on

- `URHAgentVisualizerSubsystem::FIdleDebug` (`Public/RHAgentVisualizerSubsystem.h:74-93`) has `Show`, `PadId`, `Slot`, `bAtSlot`, `PadCm`, `PadHalfCm`, `ClearCm`, `RingCm`, `SlotCm`, `GoalCm`, `RankCm`, `DrawnCm`, `InspectId`, `NextInspectS`, `InspectLeftS` and `bInspecting`. It has no keep-out field today. `EIdleShow` is `NotWalker = 0, Busy, AtPad, LanderRank, Inspecting` (`:70-73`).
- `GetIdleDebug` (`Private/RHAgentVisualizerSubsystem.cpp:649-688`) fills the pad block (`PadId` to `bInspecting`) only when the figure has a cached pad and a slot (`:670-674`). So any figure the click step counts as "at the pad" (`D.PadId == IdlePad`) has its pad fields filled.
- `AdvanceDrawn`'s idle branch (`:1076-1170`):
  - Phase 0 (walking to its slot or waiting there) and phase 3 (walking back) set `IdleShow = AtPad` and `IdleGoalCm = SlotCm` (`:1137-1153`).
  - Phases 1 and 2 (walking to a building, then Operate) set `Inspecting` and `IdleGoalCm = InspectAtCm` (`:1154-1167`).
  - A return to the slot sets `IdleWaitForS = InspectWaitS` = 20 sim s (`:1112-1118`).
  - `Pace = min(clock speed, 8)` (`:942`).
- `ChooseIdlePad` (`:565-647`): when the choice really changes, it sets `bAtSlot = false` (`:611`).
- `RobotClearanceCm` = 90 (`:116`). `IdleRingOutCm` = 250 (`:122`). `IdleDockClearCm` = 500 (`:128`).

### 1.8 The drawn pad (for C4's own number)

- `URHColonyVisualizerSubsystem::GetBuildingVisual(int32)` (`Public/RHColonyVisualizerSubsystem.h:51-55`) returns the building's main `AStaticMeshActor`. The click test already reads it in `build.site` (`RHClickTest.cpp:1937-1938`) and already includes `RHColonyVisualizerSubsystem.h`, `Engine/StaticMeshActor.h` and `Components/StaticMeshComponent.h` (`:25`, `:44-45`).
- The ChargePad is drawn with its V2 model (`RHColonyVisualizerSubsystem.cpp:2447`). It is fitted to its footprint: `Fit = (FootprintX*2, FootprintY*2)` for ChargePad (`:2513-2520`), `TargetCm = min(Fit) * 100` (`:2522`), and the uniform scale makes the larger horizontal extent `TargetCm` (`:2523`). For the 2x2 row that is 400 cm, so the larger horizontal half-extent is **200 cm**.
- The model is a round deck that nothing leaves: deck radius 1.98 m, as asserted in `scripts/blender/rh_build_chargepad.py` header lines 25-29. `idle_r2_click1` logs "ChargePad renders as imported model 'ChargePad' (uniform scale 1.01 ...)".
- A building with a real model gets no `BuildSilhouette` accents (`:2588-2590`), and building actors are not yawed. So the actor's `UStaticMeshComponent::Bounds.BoxExtent` (public in UE 5.8, `SceneComponent.h:129`) has max(X, Y) = 200 cm.
- The actor exists from the placing frame. In `idle_r2_click1`, "Built ChargePad #8" and "ChargePad renders as imported model" are both in frame `[591]`, before setup's "Fleet HELD".

### 1.9 Sim facts (read only, for C4's job-walk question)

- `SetFleetHold` (`RHSimWorldSubsystem.cpp:2821-2830`). Under `bFleetHold`, `TryClaimDig` (`:13046`), `TryClaimHaul` (`:13073`), `TryClaimBuild` (`:13268`), `TryClaimSurvey` (`:13513`) and `TryClaimRepair` (`:13622`) all return at once.
- A charge is self-issued in `RHRobotTaskProcessor.cpp:74-114` with no hold check. It fires below `ChargeSeekFraction` 0.25 and runs to `ChargeResumeFraction` 0.9 (`RHSimWorldSubsystem.h:3846-3847`).
- `FindNearestChargePad` skips a switched-off pad and falls back to the Lander (`RHSimWorldSubsystem.cpp:13402`, `:13417-13422`).
- A docking robot stops `ArriveDistCm` = 300 cm short of the pad's centre (`RHRobotMovement.h:13`).

### 1.10 What round 2's runs show (`idle_r2_click1..3`, `idle_r2_dev_click1`, `idle_r2_mut_click1`)

The same timeline held in every run. Times are seconds after "Built ChargePad #8":

- +0.55: the first poll.
- +2.8 to +3.6: robots 0-2 reach their slots.
- +5.2 to +5.5: robots 3 and 6 reach theirs.
- +6.0 to +6.3: robots 4 and 5 reach slots 4 and 5 **round the pad**. Their straight lines pass 94-131 cm and 168-271 cm from the centre. The robots came no nearer than 334 cm and 315 cm in the counted runs.
- +9.7: robot 0 leaves for building #1 (the Lander), whose stand point is 0.4 m from slot 0.
- +11.7: robot 0 is back at slot 0, and the watch ends on that poll ("robot 0 watched 22 sim s: its wait, building #1 inspected and back at slot 0").
- +12.7 to +13.8: the OFF and ON presses.
- +14.4: the `robots.idle` line.

Two other facts from the same runs:

- Frames run about 32 fps (frames 609 to 969 between `07.03.39:493` and `07.03.50:652`). At 3x, a walk to a slot moves `FreeWalkCmS x 1.5 x Pace` = 675 cm/s, so the figure moves about 21 cm per poll.
- No poll was over 200 cm from the last in any round 1 or round 2 log: `grep -c "over 200 cm"` is 0 in all 14. The PASS line read "7 tracked" against 7 surface walkers. No figure was first seen on the pad, and no charge or dock was logged during the step.

Buildings within 20 m of the harness pad at (10, -10) m: #1 Lander (14.1 m), #5 SolarArray (exactly 20.0 m) and #7 HabModule (18.0 m). So robot 0's inspection rounds are short walks.

---

## 2. C1: a figure first seen on the pad is tracked once it is seen off it

### 2.1 Change

1. **`IdleOnPad` becomes a map.** It records where the figure was first seen, for the release line. A counter pair is added.
2. **"On the pad" also covers nearer the centre than the floor.** The region becomes "inside the pad's clearance box, **or** nearer its centre than the floor". With today's floor (290 cm) the disc is the box's inscribed circle, so this is exactly today's box. With C4's floor (350 cm) it also covers a robot that charged on the pad's axis and is walking off the mast: it stands 300 cm out (`ArriveDistCm`), outside the 290 cm box but inside the keep-out.

   Without this, C4 + C1 would start judging such a figure at 291-349 cm and fail a correct walk. The ruling exempts "the robot docking at or leaving that pad's mast".
3. **The release.** A held figure that is seen off the pad (outside both) is removed from `IdleOnPad`, logged once, and tracked from that point. A new leg starts there. `:3750` stays: a figure that stops being drawn at the pad is dropped from the map, so its next spell starts fresh.
4. **The comments are corrected.** That covers the `FTest` comment (`:909-912`, `:923-927`, `:940`), the step's header comment (`:3617-3632`), `IdleTrackAll`'s comment (`:3719-3722`) and the wait's pick comment (`:3846-3847`). The full texts are in section 6.
5. **The line.** It gains "(N first seen on the pad, M of them tracked once off it)" when N > 0. The wait's timeout line (`:3863-3864`) says "held as first seen on the pad (inside its clearance box or keep-out)".

The code is in section 6.3, marked `C1`.

### 2.2 Proof that the check can fail (mutation `p2_mut_c1`)

C1 adds no check of its own. It puts more figures under the floor. In the main scene it changes nothing: no figure is first seen on the pad (1.10). So its mutation must stage one. There is one patch with two hunks, and both are reverted after the run.

- **Hunk 1, `RHClickTest.cpp`, in `IdleTrackAll`'s first-sighting branch, just before the on-pad test.** It stages robot 5 as if it had come off the pad:
  ```cpp
  					// MUTATION p2_mut_c1 (staging): robot 5 is taken as first seen on the pad at its first sighting.
  					if (i == 5 && !F && !T->IdleOnPad.Contains(i))
  					{
  						T->IdleOnPad.Add(i, P);
  						++T->IdleOnPadSeen;
  						continue;
  					}
  ```
- **Hunk 2, `RHAgentVisualizerSubsystem.cpp`.** Robot 5's idle legs walk straight to their goal. This is round 2's `r2/dev/mutation.diff` hunk (sets `T.FreePath = { T.DrawCm, T.IdleGoalCm }` with `bFree`, `FreeGoalCm`, `FreeLenCm` and `FreeDoneCm`), gated `if (i == 5 && ...)`. Place it where W1's idle walk plans its path, just before `Desired = FreeWalkTo(T.IdleGoalCm);` (`:1169` at HEAD), so that W1's keep-out does not re-route it.

**Expected:** `CLICKTEST robots.idle FAIL robot 5 came N cm from the pad's centre (never under the floor, 350 cm, ...)`. N is robot 5's straight line, 168-271 cm in the recorded runs.

- The log has "RHClickTest: robots.idle: robot 5, first seen on the pad ... is off it at (...) m ...: tracked from here".
- The line reads "(1 first seen on the pad, 1 of them tracked once off it)".
- Robot 4's round leg is walked correctly under W1's keep-out, so the floor is exercised.
- The ring, band, rank margin, keep-out agreement, C3 and the OFF/ON checks all pass. The total is **114/1**.

**Why it proves C1:** the same two hunks on HEAD's `IdleOnPad` logic would PASS. Robot 5 stays in the set for its whole spell, its straight walk is never judged, and robot 4's leg exercises the floor.

### 2.3 Steps, timing, risk

- **Steps:** none added (115).
- **Timing:** no new wait. A released figure's round leg can keep the watch running until it ends (`bRoundOpen`), inside the watch's 45 s bound.
- **Risk:**
  - In a run where a robot charges at the harness pad (possible: charges are not held, 1.9), that robot is now judged from where it leaves the pad.
  - If W1 walks it off the pad and round correctly, it passes.
  - If W1 sends it back across the keep-out, the step fails. That is the point of C1.
  - Known limit, not changed: while a figure is held (still on the pad), its walk is not judged. One walking across the disc from the mast to the far side is caught only once it is off the disc. See section 9.4.
  - No effect on `leak.total` or `ExpectedWorld`.

---

## 3. C2: a round leg with a long poll is not the proof that the floor was exercised

### 3.1 Change

1. **Per-leg count.** `FIdleFig` gains `int32 LegLongGaps = 0;`. Each poll over 200 cm from the last adds 1 to it as well as to `IdleLongGaps`, before the goal-change test, so the poll counts toward the leg it ended. A new leg resets it to 0.
2. **At the leg's end.**
   - A round leg (`LegLineCm < floor`) walked to within 50 cm of its goal raises `IdleRoundLegs` only if `LegLongGaps == 0`, and only then can it become `IdleRoundSaid`.
   - Otherwise it raises `IdleRoundGapped`. The first three are named in `IdleRoundGappedSaid` ("robot 4 to (13.8, -13.8) m, 2 poll(s) over 200 cm apart").
   - The leg-end log line says "(N poll(s) over 200 cm apart: judged in part by end points, not counted)".
3. **The watch's exit** (`:3983`) is unchanged in form. It now waits for a clean round leg, because `IdleRoundLegs` counts only those.
4. **The verdict** (`:3998-4002`). When `IdleRoundLegs == 0` the step fails "floor not exercised: of N leg(s) tracked, none whose straight line passes inside F cm of the centre was walked to its end with every poll under 200 cm apart (C such leg(s) cut short, G walked to the end but judged in part by end points only: <names>, none under way);". This covers the case where every round leg had a long poll.
5. **The PASS line** gains ", G more judged in part by end points only, not counted (<names>)" when G > 0.

The code is in sections 6.3 and 6.6, marked `C2`.

### 3.2 Proof that the check can fail (mutation `p2_mut_c2`)

There is one hunk, in `RHClickTest.cpp`. Put it at the top of `IdleTrackAll`, after the `if (!Viz || T->IdlePad == 0) return;` block:

```cpp
			// MUTATION p2_mut_c2: the figures are polled every 30th frame only, as on a machine whose frames take about a second.
			if (GFrameCounter % 30 != 0)
			{
				return;
			}
```

At about 32 fps that is one poll every 0.94 s. At 3x a walk moves 675 cm/s, so the walking polls are about 630 cm apart. Every walking leg gets long polls. Robots 4 and 5's round legs still reach their slots, because they stand there for 20 sim s or more, but they no longer count. `IdleRoundLegs` stays 0, so the watch runs to its 45 s bound.

**Expected:** `CLICKTEST robots.idle FAIL floor not exercised: ... (0 such leg(s) cut short, 2 walked to the end but judged in part by end points only: robot 4 to (13.8, -13.8) m, K poll(s) over 200 cm apart, robot 5 to (15.4, -10.0) m, ..., none under way);`.

Everything else passes:

- **The floor.** End points of walks that keep out are at least 349 cm.
- **The wait.** It watches robot 0 through its own trail, polled every frame.
- **The ring, band and rank margin.**
- **The keep-out agreement.**
- **The watch.** Its own per-frame logic sets "its wait, building #1 inspected and back at slot 0".
- **C3.** At the 45 s bound robot 0 may be out on a later round. C3's wait (4.1) brings it back at 3x in about 5 s, then passes.
- **The OFF and ON checks.**

The total is **114/1**. The same hunk on HEAD's counting would PASS, because robots 4 and 5's legs would count.

### 3.3 Steps, timing, risk

- **Steps:** none added (115).
- **Timing:** none in a healthy run (no long poll in any recorded run). On a run with a frame over about 300 ms during both round walks, the watch now waits on, up to its 45 s, for a clean round leg, then fails. That is the strictness the review asked for. The gate starts its click runs quiet (ruling 22, load under 5).
- **Risk:** a hitchy machine can now fail `robots.idle` with "floor not exercised". The line names the legs and their long polls, so the cause is visible, and ruling 26's dropped-press rule is untouched. No effect on `leak.total` or `ExpectedWorld`.

---

## 4. C3: the watched robot is at its ring slot just before Switch OFF

### 4.1 Change

A new polling runner step, `robots.idle before off`, is pushed **after `robots.idle select` and before `PushPress(TEXT("robots.idle off"), ...)`**, inside the block that opens at `:4016`. Its gap is 0.15 s and its bound is 10 s.

1. **Read.** On each poll it reads the watched robot's `FIdleDebug`. It is **home** when the pad is on (`!Sim->IsManualOff(IdlePad)`), `Show == AtPad`, `PadId == IdlePad` and `Dist2D(GoalCm, SlotCm) < 1`. That is walking to its slot, standing at it, or walking back from an inspection (phases 0 and 3, `RHAgentVisualizerSubsystem.cpp:1137-1153`).
2. **Home on the first poll.** In a healthy run it always is: the watch ends on the poll that sees robot 0 back at slot 0, which is 20 sim s before its next inspection (1.10). The step records the snapshot in `T->IdleBeforeOff` and adds "; just before Switch OFF it was drawn waiting at slot K of pad #P, its goal that slot (x, y) m" to `IdleSaid`.
3. **Out on an inspection round for this pad.** If `Show == Inspecting`, `PadId == IdlePad`, the pad is on and no failure has been recorded yet (`IdleWhy` empty), it waits. On the first such poll it sets 3x, so the round ends sooner. On the poll that shows it home it records the snapshot and sets 1x again.
   - Phase 3, the walk back, already counts as home.
   - The longest wait in the main scene is a walk out of under about 13 m at 450 cm/s, plus 2 s of Operate: about 5 s.
4. **Anything else, or the bound.** A task (`Busy`), the Lander rank, another pad, the pad off, an earlier failure, or the 10 s bound: no wait. The step records the snapshot, and if it is not home it adds:
   `" just before Switch OFF robot %d was not drawn waiting at its slot of pad #%d with the pad on: drawn %d at pad #%d, goal (%.1f, %.1f) m, slot (%.1f, %.1f) m, the pad %s;"`.
   It puts the speed back to 1x if it raised it.
5. **Unchanged.** `GoalCheck` and both presses stay as they are.

Together the two checks prove what the review asked. About 0.42 s plus 6 frames before the press, the robot's goal was its ring slot: the gaps are 0.15 + 0.15 + 0.12 s before the `up` that switches the pad off, plus `MinGapFrames`. After the press, its goal is its rank slot. The pad is placed at least 920 cm from every rank slot and ring slots stand at most 760 cm from the centre, so the two goals are at least 160 cm apart. The press moved it.

The code is in section 6.7.

### 4.2 Proof that the check can fail (mutation `p2_mut_c3`)

There is one hunk, in `RHAgentVisualizerSubsystem.cpp`'s idle branch, right after the phase-0 block that sets `AtPad` and `IdleGoalCm = SlotCm` (`:1137-1147` at HEAD):

```cpp
				// MUTATION p2_mut_c3: at 1x, a figure back at its slot after an inspection heads for the Lander rank.
				if (T.IdlePhase == 0 && T.bAtSlot && T.InspectCount > 0 && Pace < 2.f)
				{
					T.IdleShow = (uint8)EIdleShow::LanderRank;
					T.IdleGoalCm = T.IdleRankCm;
					T.bIdleFace = false;
				}
```

1. **Before `robots.idle`.** The scene has no pad, so no figure has inspected (`InspectCount` 0), and nothing changes.
2. **During the wait and the watch.** They run at 3x (`Pace` 3), so the hunk is off.
3. **After the watch sets 1x.** Robot 0 (`InspectCount` 1, at slot 0) is drawn heading for the rank. At `before off` it is `LanderRank`. That is neither home nor out, so the step fails at once:
   `CLICKTEST robots.idle FAIL just before Switch OFF robot 0 was not drawn waiting at its slot of pad #8 with the pad on: drawn 3 at pad #8, goal (-7.5, -5.5) m, slot (6.2, -6.2) m, the pad on;`
4. **The OFF check passes.** No pad means the rank, the normal way.
5. **The ON check passes.** `ChooseIdlePad`'s new choice sets `bAtSlot = false` (`:611`), so robot 0, far from slot 0, is `AtPad` with its goal on slot 0 at the first poll.

Every other part passes. The total is **114/1**. The same hunk on HEAD's step would PASS 115/0: the OFF check is met by a figure already in the rank. That is the hole C3 closes.

### 4.3 Steps, timing, risk

- **Steps:** none added (`before off` is a `Push` without `Report`), so 115.
- **Timing:** about +0.15 s and one frame in a healthy run. At most +10 s, and only when the watched robot is still out on an inspection round 10 s into the step at 3x, which no main-scene inspection takes (about 5 s or less). See section 7.
- **Risk:**
  - The 3x interval, only when waiting, is put back to 1x before the press, so the presses run at 1x as today. The report step sets 1x anyway (`:4081`).
  - A watched robot given a charge during the watch now fails at `before off` as well as at the OFF check, which already fails that case today. It fails at once, with no wait.
  - No world click, no `leak.total` or `ExpectedWorld` change.

---

## 5. C4: ruling 28's floor, and the job walk

### 5.1 The number

Ruling 28: KeepOutCm = the pad's drawn radius + 100, never less than PadHalf + RobotClearanceCm + 60. For the 2x2 pad that is max(200 + 100, 200 + 90 + 60) = **350 cm**. 350 is under R1 = 540, so no slot moves.

**The test works it out itself**, as it works out R1 (the round-1 review's lesson: numbers taken from the visualizer under test cannot fail it):

- `IdleFloorMinCm = PadHalf + 90 + 60` at setup.
- `IdleDrawnHalfCm` = max(`BoxExtent.X`, `BoxExtent.Y`) of the pad actor's `UStaticMeshComponent::Bounds`, via `URHColonyVisualizerSubsystem::GetBuildingVisual(IdlePad)` (1.8).
- `IdleFloorCm = max(IdleDrawnHalfCm + 100, IdleFloorMinCm)`.

**It reads the visualizer's own number and requires it equal within 1 cm.** That is `D.KeepOutCm`, the field W1 adds to `FIdleDebug`; see 5.4 for the contract. The text on a mismatch is `" the visualizer's keep-out for pad #%d is %.0f cm, not %.0f cm (ruling 28: its drawn radius %.0f + 100, never under PadHalf + 90 + 60 = %.0f);"`. The pattern matches the ring's check at `:3909-3912`.

So whenever the step passes, the floor is the visualizer's `KeepOutCm`. A wrong visualizer value fails the step instead of moving the floor.

**Independence:** the cache design (`cache.md`, its W1 note) asks W1 to compute the drawn radius from the asset and the colony visualizer's fit, never from a spawned actor. The test measures the actor. The two derivations are independent and both give 200 cm.

**When the floor is read:** lazily, at the first figure drawn at the pad, inside `IdleTrackAll` and before that figure is judged. That is at least 0.55 s after the pad is placed, so the actor and W1's cache entry both exist. Setup leaves `IdleFloorCm = 0` (unset). No figure is judged before the read, because the read happens at the first figure that could be.

### 5.2 The measure

- **Polled points:** each figure's polled point is judged against the floor.
- **Segments of 200 cm or less** are judged as a chord with the allowance a chord of that length between two points on the floor's circle has:

  `Dip = F - sqrt(F^2 - (c/2)^2)`, and `Near = min(Dist, LineCm(Last, P) + Dip)`.

  The allowance is 0.16 cm at the 21 cm of a frame at 3x, 1.6 cm at 67 cm, and 14.6 cm at 200 cm. If both ends of a segment are at least F from the centre, its nearest point is at least `sqrt(F^2 - c^2/4)`. Proof: the foot of the perpendicular is at most c/2 from one end. A brute-force check of 200k random compliant segments gave a least `h + Dip - F` of +0.03 cm.

  So a walk that keeps exactly to the keep-out, for example W1's arc, never fails between two polls. A walk that cuts inside it does: a straight line at 345 cm gives Near ≈ 345.2. Without the allowance, an arc walked at exactly 350 cm fails on 0.2-1.6 cm chord dips at ordinary frame lengths.
- **Segments over 200 cm** are judged by their end point only and count as long, as today (C2).
- **The verdict:** `IdleAllNearestCm < IdleFloorCm - 1.0` fails the step. 1 cm is allowed for float rounding: W1 may place points at exactly 350.0 cm, and `Dist2D` of float coordinates then reads 349.9997. The round-leg rule (`LegLineCm < floor`) and C1's on-pad region use the floor without that tolerance.

### 5.3 Mutation `p2_mut_c4`, shared with W1's proof

Ruling 28: "a mutation that removes the keep-out must fail it". W1's keep-out routing is skipped: idle legs use `PlanRoute`'s path unchanged, as at HEAD. `KeepOutCm` stays computed, so the agreement check still passes. W1's design names the exact line.

**Expected:** `CLICKTEST robots.idle FAIL robot 5 came ~315 cm from the pad's centre (never under the floor, 350 cm, ruling 28's keep-out, for any figure at the pad; 1 cm allowed for rounding);`. These are HEAD's routes: robots 4 and 5 came 334 and 315 cm in every counted round-2 run, and the line names the nearest.

- Robots 4 and 5's legs (straight lines 94-131 and 168-271 cm) are walked to their ends with every poll about 21 cm apart, so the floor is exercised.
- The ring, band, rank margin, keep-out agreement, C3 and OFF/ON all pass. The total is **114/1**.

The same build with the floor at today's 290 cm would PASS (315 > 290). So this mutation fails only because of C4.

### 5.4 The interface C4 needs from W1 (for the brief)

- In `FIdleDebug` (`RHAgentVisualizerSubsystem.h:74-93`):
  ```cpp
  float KeepOutCm = 0.f; // ruling 28: no idle walk at this pad comes nearer its centre (0 when not at a pad)
  ```
  It is filled in `GetIdleDebug`'s pad block beside `Out.RingCm = Pad->RingCm;` (`.cpp:681`), from the cached pad.
- If W1 exposes it through an accessor instead, for example `float GetIdleKeepOutCm(int32 PadId) const`, the read in 6.3 calls that with the same meaning.
- W1 must define the drawn radius as **the larger horizontal half-extent of the drawn mesh's bounds, after the colony visualizer's fit** (200 cm here). If W1 uses another definition (the half-diagonal of that box, 283 cm, or the 3D bounding sphere), its number and the test's disagree and the step fails until the brief settles one.
- W1's walked path must keep every point at least KeepOutCm from the centre. An arc made of waypoints on a circle of exactly KeepOutCm (an inscribed polygon) dips inside it between waypoints: 1.3 cm at 10-degree steps, 3.0 cm at 15. That fails the 1 cm tolerance, and it breaks the ruling's "at least KeepOutCm". Use a circumscribed polygon (vertices at KeepOutCm / cos(step/2)) or a margin.
- The docked robot's position, which W1 also exposes: **the click step does not use it.** No robot docked at the harness pad in any recorded main-scene run, so a 150 cm check there could not fail. Ruling 28 gives that rule to the presentation review.

### 5.5 Steps, timing, risk

- **Steps:** none added (115).
- **Timing:** no new wait. The read happens once, in an existing poll. Under W1, robots 4 and 5's walks round the pad are a little longer than HEAD's box routes, but they end about 6 s after the pad is placed, well before robot 0's return at about 11.7 s, which ends the watch. So the step's length is unchanged.
- **Risk:**
  - **C4 without W1 fails the step** (315 < 349). It is GO only together with W1. If the brief defers W1, C4 is deferred too, and C1-C3 are built against today's floor, as 9.1 says.
  - The equality check couples the test to W1's definition of the drawn radius (5.4).
  - The reported "nearest" numbers now include the chord allowance: about 0.2 cm more at ordinary frames.
  - No effect on `leak.total` or `ExpectedWorld`.

### 5.6 Can the step observe a job walk that starts from a ring slot? No.

Ruling 28 asks the floor to cover a job walk from a ring slot "where the click step can observe one without changing any other step". This step cannot:

1. **The fleet is held for the whole step.** `robots.idle setup` calls `SetFleetHold(true)` (`:3715`), and the report step releases it (`:4072`). Under the hold the sim refuses every claim a job walk needs: dig, haul, build, survey and repair (1.9). Through the wait, the watch, `before off` and both goal checks, no figure in the ring can leave for a job.

   The only task that can still begin is a self-issued charge, and the robot walking to dock is exempt under ruling 28. A robot that was mid-task when the hold began finishes and walks **to** the ring, not from it.
2. **Releasing the fleet during the step would change its other parts.** Once released, any of the 7 walkers can claim work while the watched robot's band, watch, `before off` and both Switch checks run. Those checks would race the claims.
3. **Adding a job phase after the ON goal check** (release the fleet, place a site by harness call, follow the first figure that turns `Busy`) would not change another step's code. But:
   - which figure claims, and from which slot toward which side of the pad, is decided by the sim's claim order from the robots' **sim** positions (near the Lander), so the test cannot make the walk pass the pad, and the check could pass with no walk near it;
   - it would add 15-30 s to a step whose worst case already nearly fills the slowest recorded run's budget (section 7);
   - it would leave a site and busy robots for `final`.

So C4 covers idle legs only. Ruling 28's own text then makes the captures the proof for job walks ("A job walk from a ring slot is shown by the captures (the robot leaving its slot for a new job)"): those are W1's before and after frames of a robot leaving its slot for a job. The step's header comment says so (6.2).

---

## 6. The code (exact replacements in `RHClickTest.cpp`)

The anchors are at `6df7fcc`. Re-find them by symbol: the `FTest` idle block, `robots.idle setup`, `IdleTrackAll`, `robots.idle wait`, `robots.idle watch`, `robots.idle select`, `GoalCheck`. C1 alone is 6.1, 6.2, 6.3 (its parts) and 6.4. C2 alone is 6.1 (`LegLongGaps`, `IdleRoundGapped*`), 6.3 and 6.6. C3 alone is 6.1 (`IdleBeforeOff`, `bIdleOffWaited`) and 6.7. C4 is the rest. Without C4, see 9.1.

### 6.1 The `FTest` idle block (replace `:909-946`, up to and including `FString IdleWhy, IdleSaid, IdleWatchSaid;`)

```cpp
		// Idle robots: the Charging Pad robots.idle setup places and the distances the test works out for it ON ITS OWN, from the placed
		// pad's footprint, its drawn model and the brief's numbers (R1 = PadHalf + 90 + 250; the floor, ruling 28's keep-out: the pad's drawn
		// radius + 100, never under PadHalf + 90 + 60), never from the visualizer, whose ring and keep-out are checked against them; the robot
		// it watches and its trail, to tell when it stands still; every figure at the pad, for the floor (below); the watched robot as drawn
		// just before Switch OFF; and what each part of the step saw (Why: what went wrong).
		int32 IdlePad = 0;
		FVector IdlePadCm = FVector::ZeroVector;
		FVector2D IdlePadBoxCm = FVector2D::ZeroVector; // the pad's footprint half-axes plus 90: its clearance box, as the test works it out
		double IdlePadHalfCm = 0.0, IdleR1Cm = 0.0;
		double IdleFloorCm = 0.0;                   // the floor (ruling 28's keep-out), the test's own; worked out at the first figure drawn at the pad
		double IdleFloorMinCm = 0.0;                // the least the ruling allows: PadHalf + 90 + 60
		double IdleDrawnHalfCm = -1.0;              // the pad's drawn radius: the larger horizontal half-extent of its actor's mesh bounds (-1: none)
		double IdleKeepOutCm = 0.0;                 // the visualizer's own KeepOutCm for the pad (must equal the floor within 1 cm)
		double IdleSim0 = 0.0;                      // the sim clock when the step began (for a timeout's line)
		int32 IdleRobot = INDEX_NONE;
		double IdleFirstCm = 0.0;                   // where the watched figure was first seen waiting at the pad
		TArray<TPair<double, FVector>> IdleTrail;   // (wall s, drawn) for the last 2 s
		double IdleWatch0 = 0.0;                    // the sim clock when the watched figure stood still
		int32 IdleInspected = 0;                    // the building the watched figure went to inspect during the watch (0: none)
		// The floor holds for EVERY figure drawn waiting at or inspecting for the harness's pad, polled every frame through the wait and the
		// watch, each from its first sighting off the pad. One first seen on the pad (inside its clearance box, or nearer its centre than the
		// floor: walking off it, as a robot that charged there does) is held, not judged, until it is seen off it, and is tracked from there.
		// A leg runs from where a figure was when its goal was set to that goal; a leg whose straight line passes inside the floor had to go
		// round the pad, and at least one such leg must be walked to its end while tracked with every poll under 200 cm from the last (one
		// judged in part by end points does not count), or the floor was not exercised and the step fails.
		struct FIdleFig
		{
			bool bTracked = false;
			FVector LastCm = FVector::ZeroVector;    // drawn on the last poll: the segment since is measured, not just the point
			FVector GoalCm = FVector::ZeroVector;    // its leg's goal
			FVector LegFromCm = FVector::ZeroVector; // where the leg began (first sight, the first sight off the pad, or where it stood when its goal changed)
			double NearestCm = 1.0e9;                // nearest the pad's centre since its first sighting
			double LegNearestCm = 1.0e9;             // on this leg
			double LegLineCm = 1.0e9;                // the leg's straight line, at its nearest the pad's centre
			int32 LegLongGaps = 0;                   // polls on this leg over 200 cm from the last (judged by their end point only)
			bool bLegDone = false;                   // the leg was walked to its goal while tracked
		};
		TMap<int32, FIdleFig> IdleFigs;
		TMap<int32, FVector> IdleOnPad;             // held: first seen on the pad on this sighting (where), until it is seen off it
		int32 IdleOnPadSeen = 0, IdleOffPad = 0;    // figures first seen on the pad, and those of them tracked once seen off it
		double IdleAllNearestCm = 1.0e9;
		int32 IdleAllNearestRobot = INDEX_NONE;
		int32 IdleLegs = 0, IdleRoundLegs = 0, IdleRoundCut = 0, IdleLongGaps = 0;
		int32 IdleRoundGapped = 0;                  // legs round the pad walked to the end but judged in part by end points (not counted)
		FString IdleRoundSaid;                      // the first leg round the pad walked to its end with every poll under 200 cm apart
		FString IdleRoundGappedSaid;                // the first three legs round the pad judged in part by end points
		URHAgentVisualizerSubsystem::FIdleDebug IdleBeforeOff; // the watched robot as drawn just before Switch OFF
		bool bIdleOffWaited = false;                // the step waited (at 3x) for it to come back from an inspection round before the press
		FString IdleWhy, IdleSaid, IdleWatchSaid;
```

### 6.2 The step's header comment (replace `:3617-3632`)

```cpp
		// ---- idle robots look busy (docs/qa/2026-10-28/robot-idle-brief.md, section 3; ruling 28). The scene has no Charging Pad, so
		// robots.idle setup places one by harness call (Debug_PlaceInstant, as move.setup lays out its habitat: no world click) at the first spot
		// outward from the Lander where the sim takes a pad AND whose band (below) is clear of every robot's Lander rank slot by 100 cm, so a
		// figure standing in the rank can never pass for one in the ring. It holds the fleet, so robots finish their tasks and claim nothing new
		// (a self-issued charge still can); so no job walk can start from a ring slot while the step runs, and ruling 28's job walks are shown by
		// the captures, not here. The test works out R1 = PadHalf + RobotClearanceCm (90) + 250, PadHalf being the larger half-axis of the placed
		// pad's footprint (URHSimWorldSubsystem::GetFootprintHalfCm on the ChargePad row at the pad's size), and the floor, ruling 28's keep-out:
		// the pad's drawn radius (the larger horizontal half-extent of its actor's mesh bounds) + 100, never under PadHalf + 90 + 60; it measures
		// every distance from the sim's pad location. Then, at 3x and bounded: the lowest-index surface robot drawn waiting at the new pad is
		// watched: once its figure stands still (every position polled in the last 1.5 s, one of them a second old or more, within 5 cm) it must
		// stand within [R1 - 60, R1 + 280] cm of the centre and over 100 cm from its own Lander rank slot, the visualizer's own ring must be R1
		// and its own keep-out the floor. The watch then follows it through one inspection round where one comes in time. The floor holds for
		// EVERY figure drawn waiting at or inspecting for the pad, polled every frame through the wait and the watch from its first sighting off
		// the pad (one first seen on it is tracked once seen off it), and at least one leg whose straight line passes inside the floor (a walk
		// that must go round the pad) must be walked to its end while tracked with every poll under 200 cm from the last, or the step fails with
		// 'floor not exercised'. The pad is selected with Debug_SelectBuilding (not a world click); just before the deck's Switch OFF is pressed
		// (HandlePowerToggle, no uplink lag) the watched robot must be drawn waiting at its slot, its goal that slot; after the press its goal
		// must be its Lander rank slot (ParkSlotCm); Switch ON must make its ring slot its goal again. Every part reports into the one
		// robots.idle line; the fleet is released, the pad left switched on and the speed put back at the end. Bounds (wall time): wait 50 s,
		// watch 45 s, before the press 10 s, each goal check 15 s.
```

### 6.3 Setup's floor (replace `:3713-3714`) and `IdleTrackAll` (replace `:3719-3824`)

```cpp
			T->IdleR1Cm = T->IdlePadHalfCm + 90.0 + 250.0;
			// Ruling 28: the floor (the keep-out) is worked out at the first figure drawn at the pad (IdleTrackAll), when the pad's drawn model and
			// the visualizer's cache both hold it; the least the ruling allows is the test's own from here.
			T->IdleFloorMinCm = T->IdlePadHalfCm + 90.0 + 60.0;
			T->IdleFloorCm = 0.0;
```

```cpp
		// One poll of EVERY figure drawn waiting at or inspecting for the harness's pad (every frame through the wait and the watch). The floor
		// is ruling 28's keep-out, worked out by the test at the first figure drawn at the pad (the pad's drawn radius, the larger horizontal
		// half-extent of its actor's mesh bounds, + 100, never under PadHalf + 90 + 60) and checked against the visualizer's own KeepOutCm, as
		// its ring is against R1. Each polled point is judged against it, and so is the segment from the figure's last polled point: a segment
		// of c cm (200 or less) may dip what a chord of that length between two points on the floor's circle dips, F - sqrt(F^2 - (c/2)^2)
		// (0.16 cm for the 21 cm of a frame at 3x), so a walk that keeps exactly to the keep-out never fails between two polls while one that
		// cuts inside it does; a segment over 200 cm (a long frame) is judged by its end point only and marks its leg, and such a leg is never
		// the walk round the pad that exercises the floor. Each leg's straight line from where it began to its goal says whether it had to go
		// round the pad, and a leg is walked when the figure comes within 50 cm of its goal while tracked. A figure first seen on the pad (inside
		// its clearance box, or nearer its centre than the floor: one that charged there, walking off the mast) is held, not judged, until it is
		// seen off it, and is tracked from there.
		const auto IdleTrackAll = [T]()
		{
			UWorld* W = FindGameWorld();
			const URHAgentVisualizerSubsystem* Viz = W ? W->GetSubsystem<URHAgentVisualizerSubsystem>() : nullptr;
			if (!Viz || T->IdlePad == 0)
			{
				return;
			}
			const FVector C = T->IdlePadCm;
			const auto LineCm = [&C](const FVector& A, const FVector& B) { return (double)FMath::PointDistToSegment(C, A, B); };
			const auto CloseLeg = [T](FTest::FIdleFig& F)
			{
				T->IdleRoundCut += (F.LegLineCm < T->IdleFloorCm && !F.bLegDone) ? 1 : 0;
			};
			for (int32 i = 0; i < Viz->GetTrackedCount(); ++i)
			{
				URHAgentVisualizerSubsystem::FIdleDebug D;
				const bool bHere = Viz->GetIdleDebug(i, D) && D.PadId == T->IdlePad
					&& (D.Show == URHAgentVisualizerSubsystem::EIdleShow::AtPad || D.Show == URHAgentVisualizerSubsystem::EIdleShow::Inspecting);
				FTest::FIdleFig* F = T->IdleFigs.Find(i);
				if (!bHere)
				{
					if (F && F->bTracked)
					{
						CloseLeg(*F);
						F->bTracked = false;
					}
					T->IdleOnPad.Remove(i);
					continue;
				}
				if (T->IdleFloorCm <= 0.0)
				{
					// C4: the floor, worked out once, at the first figure drawn at the pad (its drawn model exists by then), and the visualizer's own
					// keep-out checked against it.
					const URHColonyVisualizerSubsystem* Colony = W->GetSubsystem<URHColonyVisualizerSubsystem>();
					const AStaticMeshActor* PadActor = Colony ? Colony->GetBuildingVisual(T->IdlePad) : nullptr;
					const UStaticMeshComponent* PadMesh = PadActor ? PadActor->GetStaticMeshComponent() : nullptr;
					T->IdleDrawnHalfCm = PadMesh ? FMath::Max(PadMesh->Bounds.BoxExtent.X, PadMesh->Bounds.BoxExtent.Y) : -1.0;
					T->IdleFloorCm = FMath::Max(T->IdleDrawnHalfCm + 100.0, T->IdleFloorMinCm);
					T->IdleKeepOutCm = D.KeepOutCm; // W1's field (or its accessor for T->IdlePad)
					if (!PadMesh)
					{
						T->IdleWhy += FString::Printf(TEXT(" pad #%d has no drawn model to take its radius from (floor %.0f cm, the ruling's least);"), T->IdlePad, T->IdleFloorCm);
					}
					if (FMath::Abs(T->IdleKeepOutCm - T->IdleFloorCm) > 1.0)
					{
						T->IdleWhy += FString::Printf(TEXT(" the visualizer's keep-out for pad #%d is %.0f cm, not %.0f cm (ruling 28: its drawn radius %.0f + 100, never under PadHalf + 90 + 60 = %.0f);"),
							T->IdlePad, T->IdleKeepOutCm, T->IdleFloorCm, T->IdleDrawnHalfCm, T->IdleFloorMinCm);
					}
				}
				const FVector P(D.DrawnCm.X, D.DrawnCm.Y, 0.0);
				const double Dist = FVector::Dist2D(P, C);
				bool bNewLeg = false;
				if (!F || !F->bTracked)
				{
					// C1: on the pad when first seen (inside its clearance box, or nearer its centre than the floor): held until it is seen off it,
					// then tracked from there.
					if ((FMath::Abs(P.X - C.X) < T->IdlePadBoxCm.X && FMath::Abs(P.Y - C.Y) < T->IdlePadBoxCm.Y) || Dist < T->IdleFloorCm)
					{
						if (!T->IdleOnPad.Contains(i))
						{
							T->IdleOnPad.Add(i, P); // walking off the pad (it stood on it when it went idle, e.g. after charging there)
							++T->IdleOnPadSeen;
						}
						continue;
					}
					if (const FVector* OnPadAt = T->IdleOnPad.Find(i))
					{
						++T->IdleOffPad;
						UE_LOG(LogRedHope, Display, TEXT("RHClickTest: robots.idle: robot %d, first seen on the pad %.0f cm from its centre, is off it at (%.1f, %.1f) m, %.0f cm from the centre: tracked from here"),
							i, FVector::Dist2D(*OnPadAt, C), P.X / 100.0, P.Y / 100.0, Dist);
						T->IdleOnPad.Remove(i);
					}
					if (!F)
					{
						F = &T->IdleFigs.Add(i);
					}
					F->bTracked = true;
					F->LastCm = P;
					F->NearestCm = FMath::Min(F->NearestCm, Dist);
					bNewLeg = true;
				}
				else
				{
					// C4: the chord's own dip is allowed a segment, never the polled point; C2: a long poll marks its leg.
					const double Seg = FVector::Dist2D(F->LastCm, P);
					const bool bLong = Seg > 200.0;
					const double Dip = T->IdleFloorCm - FMath::Sqrt(FMath::Max(T->IdleFloorCm * T->IdleFloorCm - 0.25 * Seg * Seg, 0.0));
					const double Near = bLong ? Dist : FMath::Min(Dist, LineCm(F->LastCm, P) + Dip);
					T->IdleLongGaps += bLong ? 1 : 0;
					F->LegLongGaps += bLong ? 1 : 0;
					F->NearestCm = FMath::Min(F->NearestCm, Near);
					F->LegNearestCm = FMath::Min(F->LegNearestCm, Near);
					F->LastCm = P;
					if (FVector::Dist2D(FVector(D.GoalCm.X, D.GoalCm.Y, 0.0), F->GoalCm) > 1.0)
					{
						CloseLeg(*F);
						bNewLeg = true;
					}
				}
				if (bNewLeg)
				{
					F->GoalCm = FVector(D.GoalCm.X, D.GoalCm.Y, 0.0);
					F->LegFromCm = P;
					F->LegLineCm = LineCm(P, F->GoalCm);
					F->LegNearestCm = Dist;
					F->LegLongGaps = 0;
					F->bLegDone = false;
					++T->IdleLegs;
					UE_LOG(LogRedHope, Display, TEXT("RHClickTest: robots.idle leg %d: robot %d (%s, slot %d) from (%.1f, %.1f) m, %.0f cm from the pad's centre, to (%.1f, %.1f) m; its straight line passes %.0f cm from the centre (%s)"),
						T->IdleLegs, i, D.Show == URHAgentVisualizerSubsystem::EIdleShow::AtPad ? TEXT("waiting") : TEXT("inspecting"), D.Slot, P.X / 100.0, P.Y / 100.0, Dist,
						F->GoalCm.X / 100.0, F->GoalCm.Y / 100.0, F->LegLineCm, F->LegLineCm < T->IdleFloorCm ? TEXT("inside the floor: it must go round the pad") : TEXT("clear of the floor"));
				}
				if (!F->bLegDone && FVector::Dist2D(P, F->GoalCm) <= 50.0)
				{
					F->bLegDone = true;
					const bool bRound = F->LegLineCm < T->IdleFloorCm;
					if (bRound && F->LegLongGaps == 0)
					{
						++T->IdleRoundLegs;
						if (T->IdleRoundSaid.IsEmpty())
						{
							T->IdleRoundSaid = FString::Printf(TEXT("robot %d from (%.1f, %.1f) to (%.1f, %.1f) m, straight line %.0f cm from the centre, walked round it no nearer than %.0f cm"),
								i, F->LegFromCm.X / 100.0, F->LegFromCm.Y / 100.0, F->GoalCm.X / 100.0, F->GoalCm.Y / 100.0, F->LegLineCm, F->LegNearestCm);
						}
					}
					else if (bRound)
					{
						// C2: walked round the pad, but part of it was judged by end points only: not the proof that the floor was exercised.
						++T->IdleRoundGapped;
						if (T->IdleRoundGapped <= 3)
						{
							T->IdleRoundGappedSaid += FString::Printf(TEXT("%srobot %d to (%.1f, %.1f) m, %d poll(s) over 200 cm apart"),
								T->IdleRoundGappedSaid.IsEmpty() ? TEXT("") : TEXT(", "), i, F->GoalCm.X / 100.0, F->GoalCm.Y / 100.0, F->LegLongGaps);
						}
					}
					UE_LOG(LogRedHope, Display, TEXT("RHClickTest: robots.idle leg: robot %d reached (%.1f, %.1f) m%s, nearest the pad's centre on the leg %.0f cm%s"),
						i, F->GoalCm.X / 100.0, F->GoalCm.Y / 100.0, bRound ? TEXT(" round the pad") : TEXT(""), F->LegNearestCm,
						F->LegLongGaps > 0 ? *FString::Printf(TEXT(" (%d poll(s) over 200 cm apart: judged in part by end points, not counted)"), F->LegLongGaps) : TEXT(""));
				}
				if (F->NearestCm < T->IdleAllNearestCm)
				{
					T->IdleAllNearestCm = F->NearestCm;
					T->IdleAllNearestRobot = i;
				}
			}
		};
```

Notes for the builder:

- **Names and shadowing.** The new locals are `Colony`, `PadActor`, `PadMesh`, `OnPadAt`, `bLong` and `Dip`. None clashes with a name in this lambda. The lambda captures only `T`, as today, so `-Wshadow` is not engaged.
- **Types.** `Bounds` is public (`SceneComponent.h:129`). `TMap::Find` returns `FVector*`. The pointer is used before `Remove`.
- **The one name owned by W1** is `D.KeepOutCm`. If W1 ships an accessor instead, replace that line with `T->IdleKeepOutCm = Viz->GetIdleKeepOutCm(T->IdlePad);`, or whatever W1 names it.

### 6.4 The wait's pick comment (`:3846-3847`) and its timeout line (`:3863-3864`)

```cpp
				// The lowest-index robot drawn waiting at the harness's pad (walking to its slot or standing at it) that is tracked for the floor
				// (first seen off the pad, or tracked since it was seen off it).
```

```cpp
							T->IdleWhy += FString::Printf(TEXT(" no surface robot was drawn waiting at pad #%d within the wait (%d held as first seen on the pad, inside its clearance box or keep-out)%s;"),
								T->IdlePad, T->IdleOnPad.Num(), *IdlePaced());
```

### 6.5 The stand-still line (replace `:3906-3908`)

```cpp
			T->IdleSaid = FString::Printf(TEXT("pad #%d at (%.1f, %.1f) m, PadHalf %.0f cm: R1 %.0f, band %.0f-%.0f, floor %.0f cm (ruling 28's keep-out, the test's own: drawn radius %.0f + 100, never under %.0f; the visualizer's %.0f); robot %d first seen waiting there %.0f cm from its centre, stood still at %.0f cm (slot %d; the visualizer's ring %.0f cm), %.0f cm from its Lander rank slot, nearest on the way %.0f cm"),
				T->IdlePad, T->IdlePadCm.X / 100.0, T->IdlePadCm.Y / 100.0, T->IdlePadHalfCm, T->IdleR1Cm, Lo, Hi, T->IdleFloorCm, T->IdleDrawnHalfCm, T->IdleFloorMinCm, T->IdleKeepOutCm,
				T->IdleRobot, T->IdleFirstCm, Dist, D.Slot, D.RingCm, FromRank, F ? F->NearestCm : -1.0);
```

The floor has always been read by this point: the watched robot is tracked, so a figure at the pad was polled first.

### 6.6 The watch's verdict (replace `:3993-4005`)

```cpp
			if (T->IdleAllNearestCm < T->IdleFloorCm - 1.0)
			{
				T->IdleWhy += FString::Printf(TEXT(" robot %d came %.0f cm from the pad's centre (never under the floor, %.0f cm, ruling 28's keep-out, for any figure at the pad; 1 cm allowed for rounding);"),
					T->IdleAllNearestRobot, T->IdleAllNearestCm, T->IdleFloorCm);
			}
			if (T->IdleRoundLegs == 0)
			{
				T->IdleWhy += FString::Printf(TEXT(" floor not exercised: of %d leg(s) tracked, none whose straight line passes inside %.0f cm of the centre was walked to its end with every poll under 200 cm apart (%d such leg(s) cut short, %d walked to the end but judged in part by end points only%s, %s);"),
					T->IdleLegs, T->IdleFloorCm, T->IdleRoundCut, T->IdleRoundGapped,
					T->IdleRoundGappedSaid.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(": %s"), *T->IdleRoundGappedSaid),
					bRoundOpen ? TEXT("one still under way when the watch ended") : TEXT("none under way"));
			}
			T->IdleSaid += FString::Printf(TEXT("; %s; the floor over every figure at the pad: %d tracked%s, %d leg(s), %d round the pad walked to the end (first: %s)%s, nearest %.0f cm (robot %d)%s"),
				*T->IdleWatchSaid, T->IdleFigs.Num(),
				T->IdleOnPadSeen > 0 ? *FString::Printf(TEXT(" (%d first seen on the pad, %d of them tracked once off it)"), T->IdleOnPadSeen, T->IdleOffPad) : TEXT(""),
				T->IdleLegs, T->IdleRoundLegs, T->IdleRoundSaid.IsEmpty() ? TEXT("none") : *T->IdleRoundSaid,
				T->IdleRoundGapped > 0 ? *FString::Printf(TEXT(", %d more judged in part by end points only, not counted (%s)"), T->IdleRoundGapped, *T->IdleRoundGappedSaid) : TEXT(""),
				T->IdleAllNearestCm, T->IdleAllNearestRobot,
				T->IdleLongGaps > 0 ? *FString::Printf(TEXT(" (%d poll(s) over 200 cm apart judged by their end point)"), T->IdleLongGaps) : TEXT(""));
```

The `*FString::Printf(...)` temporaries inside one `Printf` call follow the pattern already at `:4005`. They live to the end of the full expression.

### 6.7 C3's step (insert as the first statement inside the block that opens at `:4016`, before `const auto GoalCheck`; it is pushed before the presses either way)

```cpp
			// C3: Switch OFF's goal check reads the state after the press, which a figure already in the Lander rank meets. So just before the
			// press the watched robot must be drawn waiting at its slot of the harness's pad (walking to it, standing at it or walking back from
			// an inspection), its goal that slot, with the pad switched on; its FIdleDebug is recorded on the first poll that shows it (in a
			// healthy run the first poll: the watch ends with it back at its slot, 20 sim s before its next inspection). One still out on an
			// inspection round for the pad is waited for at 3x, 10 s at most; anything else (a task, the rank, another pad, the pad off, an
			// earlier failure) is decided at once.
			Runner.Push(TEXT("robots.idle before off"), [T](double, bool bLast)
			{
				UWorld* W = FindGameWorld();
				const URHAgentVisualizerSubsystem* Viz = W ? W->GetSubsystem<URHAgentVisualizerSubsystem>() : nullptr;
				const URHSimWorldSubsystem* Sim = GetSim();
				ARHPlayerController* PC = GetPC();
				if (T->IdleRobot == INDEX_NONE || !Viz || !Sim)
				{
					return true; // the wait already said why
				}
				URHAgentVisualizerSubsystem::FIdleDebug D;
				Viz->GetIdleDebug(T->IdleRobot, D);
				const bool bOn = !Sim->IsManualOff(T->IdlePad);
				const bool bHome = bOn && D.Show == URHAgentVisualizerSubsystem::EIdleShow::AtPad && D.PadId == T->IdlePad
					&& FVector::Dist2D(D.GoalCm, D.SlotCm) < 1.0;
				const bool bOut = bOn && D.Show == URHAgentVisualizerSubsystem::EIdleShow::Inspecting && D.PadId == T->IdlePad;
				if (!bHome && bOut && !bLast && T->IdleWhy.IsEmpty())
				{
					if (!T->bIdleOffWaited && PC)
					{
						PC->SetSimSpeed(3.f); // its round, sooner
					}
					T->bIdleOffWaited = true;
					return false;
				}
				if (T->bIdleOffWaited && PC)
				{
					PC->SetSimSpeed(1.f); // the presses run at 1x, as before
				}
				T->IdleBeforeOff = D;
				if (bHome)
				{
					T->IdleSaid += FString::Printf(TEXT("; just before Switch OFF it was drawn waiting at slot %d of pad #%d, its goal that slot (%.1f, %.1f) m%s"),
						D.Slot, D.PadId, D.SlotCm.X / 100.0, D.SlotCm.Y / 100.0, T->bIdleOffWaited ? TEXT(" (back from an inspection round)") : TEXT(""));
				}
				else
				{
					T->IdleWhy += FString::Printf(TEXT(" just before Switch OFF robot %d was not drawn waiting at its slot of pad #%d with the pad on: drawn %d at pad #%d, goal (%.1f, %.1f) m, slot (%.1f, %.1f) m, the pad %s;"),
						T->IdleRobot, T->IdlePad, (int32)D.Show, D.PadId, D.GoalCm.X / 100.0, D.GoalCm.Y / 100.0, D.SlotCm.X / 100.0, D.SlotCm.Y / 100.0,
						bOn ? TEXT("on") : TEXT("off"));
				}
				return true;
			}, 0.15f, 10.f);
```

The queue order is then: `robots.idle select` (0.8 s gap), `robots.idle before off`, `robots.idle off` (move, down, up), `robots.idle off goal`, `robots.idle on` (move, down, up), `robots.idle on goal`, `robots.idle` (report).

### 6.8 The expected main-scene PASS line after P2 (with W1)

```
CLICKTEST robots.idle PASS pad #8 at (10.0, -10.0) m, PadHalf 200 cm: R1 540, band 480-820, floor 350 cm (ruling 28's keep-out, the test's own: drawn radius 200 + 100, never under 350; the visualizer's 350); robot 0 first seen waiting there ~1560 cm from its centre, stood still at 540 cm (slot 0; the visualizer's ring 540 cm), 1370 cm from its Lander rank slot, nearest on the way 540 cm; robot 0 watched 22 sim s: its wait, building #1 inspected and back at slot 0; the floor over every figure at the pad: 7 tracked, 9 leg(s), 2 round the pad walked to the end (first: robot 4 ..., walked round it no nearer than 35x cm), nearest 35x cm (robot 4 or 5); just before Switch OFF it was drawn waiting at slot 0 of pad #8, its goal that slot (6.2, -6.2) m; after Switch OFF its goal is (-7.5, -5.5) m, its Lander rank slot; after Switch ON its goal is (6.2, -6.2) m, slot 0 of pad #8
```

The "35x" figures depend on W1's arc margin; they are at least 349. The rest is round 2's text.

---

## 7. Step count, timing against the 450 s run, risk to other steps

**Step count:**

- No `Report` call is added. The main scene stays at **115** (`CLICKTEST TOTAL pass=115 fail=0`) and the deepbench scene at **9**; `StartDeepBenchTest` does not run `robots.idle`.
- No step is removed or weakened. `robots.idle` still reports one line, and the only checks that became stricter are its own.

**Timing** (from 1.10; the step today takes 14.4 s from "Built ChargePad #8" to its line):

| Part | Today | After C1-C4 (healthy) | Bound |
|---|---|---|---|
| setup | 0.5 s gap | same | none |
| wait | ~3 s | same (the floor read is in the first poll) | 50 s |
| watch | ~8 s, ending with robot 0's return | same: W1's arcs end robots 4 and 5's walks at ~+6 s, before robot 0's return at ~+11.7 s | 45 s |
| select | 0.8 s | same | none |
| **before off** (C3, new) | none | 1 frame + 0.15 s | **10 s** |
| presses + goal checks | ~2.2 s | same | 15 s each |
| **step total** | **~14.4 s** | **~14.6 s** | |

The worst case, every bound reached, goes from about 129 s to about **139 s**. In practice the 10 s bound is not reached: it needs the watched robot to be out on an inspection that takes over 10 s at 3x, and the main scene's are under about 5 s. If the wait or the watch has already failed, `before off` decides at once.

In the counted runs the pad is placed about 180 s into the 450 s run, so a healthy step ends before 200 s.

The slowest run on record had 123 s left at the pad (`idle_r1_dev_click1`, 94/21 with harness cascades). There the theoretical worst case would overrun by about 16 s instead of about 6 s, and `rh_clicktest.sh` would end without a verdict (never a pass). Both need a failing run.

**Risk to other steps:**

- No world click: `before off` is a poll, and the bounds read is a harness read. `leak.total` stays 11 reached and 11 on purpose; `ExpectedWorld` is untouched.
- `robots.idle` stays the last step before `final`. `final` reads only `leak.total` and restores the quicksave (`:4087-4111`).
- At the step's end the state is as today: fleet released, pad on (`:4072-4076`), selection cleared, 1x (`:4078-4082`). C3's 3x interval is undone before the press, and again by the report step.
- No other step reads any `FTest::Idle*` field.

**Frame cost:** none in the game. The click step runs only under `rh_clicktest.sh`. Per poll it adds one `Sqrt` and one `Min` per figure at the pad. It reads the bounds once per run. It allocates no memory per frame: the `FString` formatting happens only on leg events and releases, as today. It plans no route.

---

## 8. Mutation plan for the builder

Freeze the final test source and the visualizer by sha256 before any mutation build, as round 2 did in `r2/dev/frozen/`. Name every label before its run. Restore by copying the frozen file back (fresh mtime) and comparing sha256. Re-confirm the final dylib's hash after the last mutation. Each mutation build compiles only the touched files.

| Label | Hunks | Expected `robots.idle` | Total |
|---|---|---|---|
| `p2_mut_c1` | test: robot 5 staged as first seen on the pad (2.2); visualizer: robot 5's idle legs walk straight | FAIL "robot 5 came N cm ... (never under the floor, 350 cm ...)", N = robot 5's straight line (168-271 cm recorded); the release line in the log; "(1 first seen on the pad, 1 of them tracked once off it)" | 114/1 |
| `p2_mut_c2` | test: `IdleTrackAll` returns unless `GFrameCounter % 30 == 0` (3.2) | FAIL "floor not exercised: ... 2 walked to the end but judged in part by end points only: robot 4 ..., robot 5 ..." (the watch runs its 45 s) | 114/1 |
| `p2_mut_c3` | visualizer: at `Pace < 2` a figure at its slot with `InspectCount > 0` is drawn heading for the rank (4.2) | FAIL "just before Switch OFF robot 0 was not drawn waiting at its slot of pad #8 with the pad on: drawn 3 at pad #8 ..." | 114/1 |
| `p2_mut_c4` (W1's) | visualizer: W1's keep-out routing skipped, `KeepOutCm` kept (5.3) | FAIL "robot 5 came ~315 cm ... (never under the floor, 350 cm, ruling 28's keep-out ...)" | 114/1 |

Each mutation fails only its own clause in the one `robots.idle` line. Every other step passes. Each would pass on the step without its item (shown in 2.2, 3.2, 4.2 and 5.3).

Cost: each run takes about 4 minutes after its build, and `p2_mut_c2` about 40 s longer (the full watch). `p2_mut_c4` doubles as W1's proof that removing the keep-out fails.

---

## 9. Notes for the brief

1. **Dependencies.**
   - C1, C2 and C3 stand alone.
   - C4 needs W1's `FIdleDebug::KeepOutCm` (or an accessor) and W1's routing.
   - **If W1 is deferred, defer C4.** Then:
     - keep `T->IdleFloorCm = T->IdlePadHalfCm + 90.0;` at setup;
     - leave out the lazy floor block, the chord allowance (`Dip`) and the `- 1.0` tolerance;
     - keep the stand-still text's "floor %.0f cm (the test's own)".

     C1's region is then exactly today's box, because the 290 cm disc is inscribed in it.
2. **W1's definition of the drawn radius** must be the larger horizontal half-extent of the drawn mesh's bounds after the colony visualizer's fit (200 cm, so 350 cm for the 2x2 pad). Otherwise the test's equality check fails (5.4).
3. **W1's arcs** must keep every point at least KeepOutCm. Use a circumscribed polygon or a margin: an inscribed one dips 1.3-3 cm (5.4).
4. **Known limit, not built.** While a figure is held as first seen on the pad, its walk is not judged. A figure walking across the disc from the mast is caught only after it leaves the disc. A rule "a held figure may not come more than ~25 cm nearer the centre than where it was first seen" would close this. It could not fail in the main scene (no figure starts on the pad), so it is left as a follow-up.
5. **The docs entry** (`docs/design-decisions.md`) should list these choices:
   - the floor is the test's own number, with the visualizer's required equal;
   - the chord allowance and the 1 cm rounding tolerance;
   - C1's region covers the keep-out disc too;
   - `before off` waits at 3x only for an inspection round, at most 10 s;
   - no job walk is observed (5.6).

   It should also say that `gate-d-wording.md` gains no block: the new lines are click-test log text, developer-facing.
