# Idle robots look busy (display only): design stub

Status: the director's answer is in. The full design, the brief and the gate script are written after E8, when this work is built.

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

None of these is decided here.
1. **Where a figure waits.** Around the pad, not on it, so it never looks docked while the sim's docked robot is drawn on the pad. The Charge clip stays with the sim's Charge task.
2. **Which pad.** Either the nearest working pad on the robot's own level, as the sim's dock search does, or the pad nearest the robot's last job.
3. **Idle robots underground.** The parking slot comes from the Lander's position, while the figure keeps its sim height (line 380). Check what an idle robot on a deep level is drawn doing today before changing it.
4. **The inspection pattern.** Choose it per robot from a stable key such as the robot's index, never a random draw, so captures and the click test repeat.
5. **Catch-up when a job arrives.** The figure walks from its idle spot to the sim position, using the existing catch-up at 1.5x the free-walk speed. A waiting spot far from the last job lengthens that lag.
6. **Frame cost.** A large fleet means more routed walks (`PlanRoute`) per frame, so the gate needs a frame check.
7. **The gate.** No RedHopeSim change. Battery and playtest byte-identical apart from timing lines. The click test is unchanged unless a step is added. Captures are looked at.
