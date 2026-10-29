# Idle robots look busy: build brief (2026-10-29)

Built after E8, by `docs/qa/2026-10-26/workflows/idle-gate-wf.js`. Written at HEAD `da73d84` (while D7 builds); every anchor below is given with its symbol, so re-anchor at the build's HEAD by symbol, never by line.

**Authority, highest first:** the design's DIRECTOR ANSWERs (`robot-idle-design.md`: 2026-10-28 (a) display only, after E8; 2026-10-29 Q-A (a), Q-B (a)) > this brief > the design's DESIGN section > the rest of the design. Where this brief says UNCERTAIN, take its default and list it as a deviation.

## 1. What is decided
1. **Display only.** No file under `Source/RedHopeSim`, `docs/data` or `Content/RedHope/Data` changes. The display reads `URHSimWorldSubsystem::FindNearestChargePad` (public, const) and `GetBuildings()`; it writes nothing to the sim, the task fragments or any save.
2. **Who:** surface walkers only: in `URHAgentVisualizerSubsystem::Tick`, the robots that reach `AdvanceDrawn` (not `bDeepDraw`, not `bDrive`, not `IsRoverIndex(i)`). With `rh.RobotRouting 0` nothing changes (the figure is drawn at the sim position, as today).
3. **When:** unchanged threshold: `T.IdleS > ParkAfterIdleS` (6 sim seconds) in `AdvanceDrawn`'s last branch, where today it calls `FreeWalkTo(ParkSlotCm(i))`.
4. **Which pad:** `FindNearestChargePad(SimPos, PadId, PadLoc, 0)` from the robot's SIM position. If the building it returns is the Lander (`DefName == "Lander"`), or it returns false, the figure uses today's `ParkSlotCm(i)` unchanged. Cache the choice per robot in `FRobotTrip` (`IdlePadId`, `IdlePadCm`) and refresh it at most once a second of real time, or at once when `GetBuildings().Num()` changes or the cached pad is gone, switched off (`bManualOff`) or under construction.
5. **Where it waits (Q-A (a)):** a ring centred on the pad. Ring 1: 8 slots at radius `R1 = PadHalf + RobotClearanceCm + 250` cm, where `PadHalf` is the larger axis of `GetFootprintHalfCm` for the pad; ring 2: 12 slots at `R1 + 220` cm. Slot 0 of ring 1 sits on the side facing the Lander, the rest go round at equal angles. A slot whose point is inside any `Obstacles` box is skipped (as `ParkSlotCm` does). Slots are held per pad: a robot takes the lowest free slot when it first goes idle at that pad (`IdleSlot` in `FRobotTrip`), keeps it while idle, and frees it when it takes a task, changes pad, or is untracked (`ResetTracking`). With more idle robots than 20 slots, the rest use `ParkSlotCm(i)` (UNCERTAIN default; say how often it happened in the captures, expected never).
6. **Facing:** once at its slot (within 30 cm), the figure turns to face directly away from the pad's centre at the existing turn rate (`FMath::FixedTurn` as in `Tick`'s facing block). Today a stationary figure keeps its last facing; add a target yaw for this case only.
7. **The inspection (Q-B (a)):**
   - `InspectWaitS = 20.f` sim seconds (placeholder; a `constexpr` beside `ParkAfterIdleS`, with the comment "placeholder, director review").
   - The list: surface buildings (`Level == 0`), not under construction, not the pad itself, whose centre is within 2000 cm of the pad's centre, in ascending Id. Rebuilt with the pad cache.
   - The first wait at a slot is `InspectWaitS + (RobotIndex * 7) % InspectWaitS`; later waits are `InspectWaitS`. Waiting time counts sim-paced (`DeltaTime * Pace`, as `IdleS` does).
   - The target: entry `(RobotIndex + InspectCount) % N` of the list, where `InspectCount` counts this figure's inspections since it went idle. With N = 0 the figure never leaves its slot.
   - The walk: `FreeWalkTo` a point on the building's footprint edge nearest the pad, pushed out by `RobotClearanceCm` (so it stands beside the building, not inside it); at 150 cm/s x Pace (`FreeWalkCmS`), not the 1.5x catch-up speed.
   - At the building: face the building's centre and play **Operate** for 6 sim seconds (`bInspecting` on `FRobotTrip` read by `Tick`'s `WantClip` block, which today picks the clip only from the sim's task fragment; the sim's fragment wins whenever its task is non-zero). Then walk back to its slot and start the next wait.
   - Everything is keyed on robot index, inspection count and sim-paced time. No random draw anywhere.
8. **A job arrives:** unchanged. The first frame the sim's task is non-zero, the existing branches take over (`bTask` resets `IdleS`; a trip plans from `T.DrawCm`). Clear `bInspecting`, free the slot, reset the wait.
9. **No player-facing string.** The cheat and log texts are developer-facing. `gate-d-wording.md` gains no block (say so in the entry).

## 2. Tools for the gate (developer-facing)
- `rh.RobotRouteLog 1`: the existing 2 s log line gains three counts, waiting at a pad, at the Lander rank and inspecting, and the subsystem's own tick time (the median of the last 2 s of `Tick` durations, measured with `FPlatformTime::Seconds()` around `Tick`'s body).
- `RH.IdleRobots` (new, in `RHCheats.cpp` beside `RH.Sites`, READ-ONLY): one line per surface walker: index, state (busy / waiting at pad #Id slot k / Lander rank / inspecting building #Id), seconds to the next inspection.
- A read-only accessor on the visualizer for the click test, e.g. `bool GetIdleDebug(int32 Index, int32& OutPadId, int32& OutSlot, bool& bOutInspecting, FVector& OutDrawnCm) const`.

## 3. The click step
One new step, **`robots.idle`**, in the existing main scene, after the steps that build a working Charging Pad (or, if the scene has none, after placing and completing one through the scene's existing build path; state which). The step:
- waits (sim-paced, bounded) for a surface robot with no task to pass `ParkAfterIdleS`, then asserts its figure is within `[R1 - 60, R1 + 220 + 60]` cm of that pad's centre and never closer than `PadHalf + RobotClearanceCm`;
- switches that pad off through the player's own off toggle, asserts the same robot's goal becomes its Lander rank slot (`ParkSlotCm`), then switches the pad back on.

The main click test's count rises by exactly 1. The deepbench scene is unchanged.

## 4. The gate
- **Pre-steps, round 0 only:** (a) RHSaveVersion at HEAD equals the `saveVersion` arg; (b) the base snapshot's log count equals `batteryRuns` (no new suite). (c) **The before binary:** add ONLY the tick timer and the route-log counts (section 2's first bullet, which change no behaviour), build, copy `libUnrealEditor-RedHope.dylib` with its sha256 to `Saved/orchestrator/idle_bin_before/`, and with it record the before tick-time medians and take the before captures. Never reinstall it later except through the capture script's own copy-and-restore, restoring the final dylib and confirming its hash.
- **Battery:** `rh_battery.sh 10`, `BATTERY COMPLETE: 0 not green`, the same run count as the base; every log newer than both dylibs; `diff_battery.py <base> Saved/RHBattery` shows timing digits only. Pins: "power: gen 50 W load 20 W", "deposit Regolith_A: 40645 kg". `git diff --stat -- Source/RedHopeSim docs/data Content/RedHope/Data` is empty.
- **Playtests:** the full playtest (`RH_PLAYTEST_DISTRICTS=1 ... 110 idle_full guide full`) cmp-identical to the playtest baseline; the RedHopeSim dylib's sha256 unchanged from HEAD's.
- **Click test:** three back-to-back passes at `clickSteps + 1`, and the deepbench scene at `deepbenchSteps`, at 1440x860 with the 450 s timeout; `uptime` load disclosed before each counted run.
- **Frame check:** on one stated save with at least two working surface pads and idle robots (build a second pad with a disclosed cheat if needed), `rh.RobotRouteLog 1` at 1x for 30 s, before binary vs final binary, three alternating pairs. The median tick-time delta must stay within **+0.2 ms** (orchestrator placeholder). Report every pair.
- **Captures, before and after from the same command file** (it logs the binary hash), at 1440x860: the idle ring at a pad (daylight and night), an inspection at a building, the Lander rank with every pad switched off, a robot leaving its slot for a new job, and a wide colony view. LOOK at each with the Read tool and say what you see. The figures in the ring must not overlap one another, the pad, or any building.
- **Docs:** a `docs/design-decisions.md` entry (what it builds, every deviation and UNCERTAIN default, the frame-check pairs, the click counts, the capture list). No wording block.

## 5. Do not
Touch the sim or its data; move the docked robot's drawing on the pad; change rovers, underground robots, riders, road trips or catch-up walks; change `ParkAfterIdleS`, `FreeWalkCmS`, the Lander rank layout or `PlanRoute`; add a random draw; add a player-facing string; add a battery suite.
