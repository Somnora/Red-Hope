# Idle robots look busy (display only): design stub

Status: the director's answer is in, and the design below was written on 2026-10-29 (at HEAD `4e9a360`, while D7 builds) so that the queue does not wait on it after E8. Two points need the director (QUESTIONS FOR THE DIRECTOR, at the end). The brief and the gate script follow the answers, and the code anchors are re-checked at E8's HEAD.

## The director's request (2026-10-27)

> "a lot of the robots seem to just be standing around most of the time is there anyway we can put them in a better formation or we could just have them doing busy work when they're at downtime or they could walk to their charging pads when they're not working, I just see them all the time just standing on the surface not doing anything"

## What the code does today (read at HEAD 26a9c8a)

**In the sim:**
- A robot with no job keeps the task type None. It stays where its last job ended and draws idle power: `Source/RedHopeSim/Private/RHRobotStateTree.cpp:75`, the Work task's None case.
- It goes to a pad only when the charge test says its battery is low.
- A Charging Pad (the `ChargePad` row in `docs/data/RH_Buildings.csv`, 2x2) docks one robot at a time, on its own level only. See `FindNearestChargePad` at `Source/RedHopeSim/Private/RHSimWorldSubsystem.cpp:12903`. The Lander is the emergency dock.

**On screen** (`Source/RedHope/Private/RHAgentVisualizerSubsystem.cpp`):
- `rh.RobotRouting` is 1 by default (line 102).
- A robot idle for 6 sim seconds (`ParkAfterIdleS`, line 116) is drawn walking to a parking slot beside the Lander (`ParkSlotCm`, line 322; the branch is at line 668).
- The slots are ranks of 8, 2.2 m apart, along the Lander's south side, and the figure plays the Idle clip (line 1384). Rovers never park.
- Only the drawn figure moves; the sim position stays put. That rank is the "formation" the director sees.

## Options put to the director (2026-10-27)

- **(a) Display only.** Idle robots walk to their nearest charging pad and wait in a ring around it. Now and then one walks to a nearby building and plays its work animation for a few seconds, like an inspection. Robots with no pad nearby keep the Lander rank. The sim is untouched.
- **(b) Real docking in the sim.** Idle robots actually top up at a pad. This moves every robot test, the playtest baseline and the timings.
- **(c) Both.** (a) now, then (b) with the balance pass.

# DIRECTOR ANSWER (2026-10-28)

- **(a), after E8.** Display only, as described above. No RedHopeSim change: saves, the battery and the playtest stay byte-identical. Queued after E8.

## To settle when the design is written (after E8)

Settled in DESIGN below (points 1 and 4 have a director question each).
1. **Where a figure waits.** Around the pad, not on it, so it never looks docked while the sim's docked robot is drawn on the pad. The Charge clip stays with the sim's Charge task.
2. **Which pad.** Either the nearest working pad on the robot's own level, as the sim's dock search does, or the pad nearest the robot's last job.
3. **Idle robots underground.** The parking slot comes from the Lander's position, while the figure keeps its sim height (line 380). Check what an idle robot on a deep level is drawn doing today before changing it.
4. **The inspection pattern.** Choose it per robot from a stable key such as the robot's index, never a random draw, so captures and the click test repeat.
5. **Catch-up when a job arrives.** The figure walks from its idle spot to the sim position, using the existing catch-up at 1.5x the free-walk speed. A waiting spot far from the last job lengthens that lag.
6. **Frame cost.** A large fleet means more routed walks (`PlanRoute`) per frame, so the gate needs a frame check.
7. **The gate.** No RedHopeSim change. Battery and playtest byte-identical apart from timing lines. The click test is unchanged unless a step is added. Captures are looked at.

# DESIGN (2026-10-29)

Read at HEAD `4e9a360`. `Source/RedHope/Private/RHAgentVisualizerSubsystem.cpp` is not touched by D7's build in progress; re-check every anchor by symbol at E8's HEAD.

## What the director is seeing (measured)
In the full playtest (`Saved/RHPlaytest/d6e_full.txt`, byte-identical to `ind_full_playtest.txt`), over 110 sols:
- excavators (3): digging 71.8%, charging 23.7%, **idle 4.5%**;
- haulers (3): hauling 35.3%, charging 27.9%, **idle 36.8%**;
- the fabricator (1): building 17.5%, charging 19.7%, **idle 62.8%**.

So about **1.9 of the 7 robots are idle at any moment** (3 x 0.045 + 3 x 0.368 + 0.628), and today every one of them is drawn standing in the rank beside the Lander. The charging share (20-28%) is drawn on the pad and is not changed here.

## What changes (presentation only)
All of it is in `URHAgentVisualizerSubsystem::AdvanceDrawn`'s idle branch (today: `T.IdleS > ParkAfterIdleS && !IsRoverIndex(i)` -> `FreeWalkTo(ParkSlotCm(i))`) and in `FRobotTrip`. Nothing under `Source/RedHopeSim` changes; the display only READS `URHSimWorldSubsystem::FindNearestChargePad` (public, const) and `GetBuildings()`.

1. **Where a figure waits (stub point 1).** Around the pad, never on it. A ring centred on the pad: the first ring holds 8 slots at radius `PadHalf + RobotClearanceCm + 250 cm` (45 degrees apart, the first slot on the side facing the Lander), the second holds 12 slots 220 cm further out. A slot inside any surface footprint (`Obstacles`) is skipped, as `ParkSlotCm` does today. On arrival the figure turns to face **away from the pad** (a new target yaw for a stationary idle figure; today a stationary figure keeps its last facing). The robot docked at the pad is still drawn on the pad's centre (`Task.TargetCm = PadLoc`) playing Charge; that stays the sim's. **Director question Q-A** settles the spacing, because a ring tight to the pad reads as a charging queue.
2. **Which pad (stub point 2).** The pad the sim would actually dock this robot at: `FindNearestChargePad(SimPos, Id, Loc, 0)` from the robot's sim position (where its last job ended). That is the nearest completed, switched-on pad on the surface. When it returns the Lander (no working pad), the figure keeps today's Lander rank (`ParkSlotCm`), unchanged. The choice is cached per robot and refreshed once a second of real time, or at once when the building count changes (a pad built, switched off or demolished), so the call does not run every frame.
3. **Robots underground (stub point 3).** Answered by the code: with lifts in the colony, a robot below the surface, riding, or on a deep road trip is drawn exactly at its sim position (`bDeepDraw`, `Tick` around line 1190) and never parks. Keep that. Only surface walkers (`!bDeepDraw && !bDrive && !IsRoverIndex(i)`) get the new idle behaviour. Rovers still never park.
4. **The inspection (stub point 4).** A waiting figure now and then walks to a nearby building, plays the **Operate** clip for 6 s, and walks back to its slot. (Not Repair: the sim's Repair task is real, and a fake one would mislead the player about wear.)
   - Nearby: surface buildings, not under construction, not the pad itself, whose centre is within 20 m of the pad, in Id order.
   - Which: the `(RobotIndex + TripCount) % N`-th of that list, where `TripCount` counts this figure's inspections; with N = 0 it never leaves its slot.
   - When: after the figure has waited `InspectWaitS` sim seconds at its slot; the first wait is offset by `(RobotIndex x 7) % InspectWaitS` so the idle robots do not all leave together. **Director question Q-B** sets `InspectWaitS`.
   - Everything is keyed on the robot's index and sim-paced time, never a random draw, so a capture or click run repeats.
   - The clip: a display flag on the trip (`bInspecting`) makes the clip choice (`Tick`, the `WantClip` block) pick Operate while the figure stands at the building; the sim's task fragment is untouched.
5. **When a job arrives (stub point 5).** Unchanged mechanics: the moment the sim gives the robot a task and it starts moving, the trip branch plans the drawn route from where the figure stands (`PlanRoute(T.DrawCm, Target, ...)`), so the figure walks from its slot or the building it was inspecting. Any inspection in progress ends at once. The pad is usually nearer the robot's work than the Lander rank, so the catch-up gap shrinks on average.
6. **Frame cost (stub point 6).** `PlanRoute` runs only when a figure's goal moves by more than 100 cm (unchanged rule in `FreeWalkTo`): once when it heads for its slot, once per inspection leg. Today `ParkSlotCm` runs for every parked robot every frame, scanning all buildings and up to 64 slots against every footprint; the cache in point 2 removes that. The gate measures the subsystem's own tick time, not frame pairs (on this 8 GB machine adjacent frame pairs swing -8 to +10 ms; see the sites gate).
7. **The gate (stub point 7).**
   - No file under `Source/RedHopeSim` or `docs/data` changes (`git diff --stat` empty there). The battery is line-identical to the base apart from timing digits; the full and default playtests are byte-identical.
   - A new click step `robots.idle` (on the existing scene): with a working surface pad, a robot idle past `ParkAfterIdleS` is drawn within the pad's ring band and at least `PadHalf + RobotClearanceCm` from the pad's centre; with the pad switched off, the same robot heads for the Lander rank. Then three back-to-back passes at the new step count, and the deepbench scene unchanged.
   - `rh.RobotRouteLog` gains the counts per state (waiting at a pad, at the Lander rank, inspecting) and the subsystem's tick time; a new cheat `RH.IdleRobots` lists each surface robot's state, pad, slot and next inspection.
   - Captures, before and after, on the same save (one with at least two pads and idle robots; the builder picks it and states it): the idle ring at a pad, an inspection at a building, the Lander rank with no pad, a robot leaving its slot for a new job, and the same frames at night.
   - Frame check: median agent-visualizer tick time over 15 s, before and after, on that save. A budget of +0.2 ms (orchestrator placeholder).

## Not in scope
Real docking in the sim (option (b)); the stacking of several docked robots on a pad's centre; crew figures; rovers; robots underground; the Lander rank's own layout when there is no pad.

# QUESTIONS FOR THE DIRECTOR (2026-10-29)

**Q-A. How close to the pad do idle robots wait?** Robots that are really charging, and those queued for the charger, stand ON the pad. Idle robots that crowd its edge can look like a charging queue, which would tell the player to build more pads when they don't need to.
- **(a) A loose ring well clear of the pad (recommended).** About 2.5 m out from the pad's edge, facing outward, 8 spots then 12 further out. With the playtest's 1.9 idle robots, that is usually two figures standing a few metres off the pad.
- **(b) A tidy row on one side of the pad,** about 3 m off, like today's Lander rank moved to the pad. Neater, but a row next to a charger reads even more like a queue.
- **(c) Right at the pad's edge.** Most clearly "waiting at the charger", with the most risk of reading as a queue.

**Q-B. How often does a waiting robot go off to inspect a building?** One inspection is about 20 sim seconds: about 7 s to walk to a building up to 20 m away, 6 s working, 7 s back.
- **(a) After 20 s of waiting (recommended).** Each idle robot spends about half its idle time inspecting; in the playtest colony, about 1 robot is out inspecting at any moment and about 1 is waiting at the pad. Closest to "busy work in downtime".
- **(b) After 60 s.** About a quarter of idle time inspecting; about 0.5 robots out at once.
- **(c) After 3 min.** About a tenth of idle time; an inspection is a rare sight, and the ring at the pad is the main change.
