# D8, the deep view: design (2026-10-23, draft for adversarial review)

Step 2 (going deep), gate 8, the last. Design only. No code is written.

**Anchors.** Code is read at HEAD `b6aaa8b` (D4 fixes on top of D6b, save v49) with `git show HEAD:`. D6c (deep commute), D6d (trams, v50), D6e (transit power, v51) and D7 (atriums, the next free version) are designed but not built; D8 builds after all of them. Line numbers below move; each anchor is also named by its function. Where D8 shows something a later gate adds, it names the accessor that gate's spec gives, and section 6 says what happens if that accessor is missing.

**Sources.** `going-deep-plan.md` (the D8 row: "a section (cross-cut) view of all floors under any shaft, a depth slider, a cutaway that opens caverns and atriums, and the tunnel and lift traffic overlay. Presentation only. The bar is captures plus a frame-time check with 20 floors and three shafts"); D1-D7 designs with their REVISIONs and DIRECTOR ANSWERs; commits `7160b73` (C3 frame table) and `9745c51` (C6b frame table); `orchestrator-handoff.md` (the rules that bite).

**Goal.** One look shows the whole deep city: every floor, stacked at its real depth, cut open along a vertical plane, with the shafts, lift cars, tunnels, caverns and atriums in place, and the traffic that moves through them.

**What you see.** You press **Section** (or `V`). The ground and the rock in front of the chosen shaft lift away. Twenty floors hang one above the other like an ant farm: warm floors amber, districts tinted, caverns framed, an atrium a tall lit void. Lift cars slide up and down their columns. You drag the depth slider (or press PageDown) and the focus floor sharpens while the others stay as simple plates. TRAFFIC turns the lift columns into load bars and draws the commute between floors. Click any floor and you dive into the ordinary floor view there.

---

## 0. Facts the design rests on (HEAD `b6aaa8b`)

- **One floor at a time.** `ApplyViewLevel` (`RHColonyVisualizerSubsystem.cpp:4486-4565`) shows a building iff `B.Level == ViewLevel` and a carve tile iff its Z rounds to `ViewLevel`; the environment rig (terrain, scenery) hides whenever `ViewLevel < 0` (`:3005-3008`). Every carved cell is its own `AStaticMeshActor` (`TileByCell`, `CarveTileVisuals`, `:3460-3532`), and each carries room props, furniture and a lamp (`LightByCell`). Floors sit at their real Z, `Level x FloorHeightCm` (400 cm).
- **The view level** is set only by `ARHPlayerController::SetActiveLevel` (`RHPlayerController.cpp:2380-2402`), clamped to `-GetShaftDepth()..0` (the deepest bored shaft), from PageUp/PageDown (`IA_FloorUp/Down`, `:155-157`) and the deck's shaft strip (`HandleFloor`).
- **Keys in use:** W A S D (pan), mouse wheel (zoom), Q E (orbit), R F (tilt), T (rotate), Space (pause), 1-4 (speed), PageUp PageDown (floor), Escape, mouse buttons. `V`, `Home`, `End` are free (grep of `EKeys::` over `Source/RedHope`).
- **The camera** (`RHStrategyPawn.cpp`): pitch is `clamp(lerp(Min, Max, zoom) + offset, -6, 86)` degrees, offset `-42..30`; `SetCameraPose(FocusXY, ZoomT, PitchOffset, Yaw)` exists (`:87`), and `SetFocusZCm` rides the floor.
- **`rh.Cutaway`** (`:69`) drops near wall faces of the viewed floor's pit (`ApplyCutaway`, `:4359`); it does not open caverns, atriums or other floors.
- **The deck's shaft strip** (`RHCommandDeck.cpp:318-480`): SURF..-MaxDepth rows in a scroll box capped at 11 rows (D5), "Next shaft" (`FocusNextShaft`), the lift card, the Tunnel and Cavern brushes and their lists. TRAFFIC is a bottom-bar button (`:1075`).
- **The TRAFFIC overlay** (`RHTrafficVisualizerSubsystem.cpp`) is surface-only: roads, rovers, shuttles. It reads `GetRoadGraph`, `GetRoverStates`, `GetCommutePairs`, `GetCommuteEdgeLoads`, `GetCommuteSummary`, `GetCommuteRamp`, refreshing freight at 2 Hz.
- **Robots** (`RHAgentVisualizerSubsystem.cpp`): instanced layers plus walkers; D4 draws a rider at its ride fraction (`:1174-1190`) and shows a robot only when `LevelOfZ(Pos.Z) == ViewLevel` (`:1311`).
- **Const accessors at HEAD** (`RHSimWorldSubsystem.h`): `GetFloors`, `GetFloorCell`, `GetFloorRoomCells`, `GetFloorWarmth`, `GetWarmthVersion`, `GetShafts`, `GetShaftsVersion`, `GetLiftCars`, `GetLiftVersion`, `GetTransitWaitSec`, `GetMaxWaitSec`, `GetShaftCapacityPlaces`, `GetShaftPeopleLoad`, `GetShaftCargoLoad`, `GetCommuteShaftPeople`, `GetTunnels`, `GetTunnelsVersion`, `GetCaverns`, `GetCavernsVersion`, `GetDistricts`, `GetCommutePairs` (with `HomeLevel`, `WorkLevel`, `Count`), `GetRideState`, `LevelOfZ`, `GetFloorCellsVersion`.
- **Frame numbers.** C3 (`7160b73`, 1600x900, median ms, 3,000 colonists): surface 30.5, floor -1 52.1. The floor view is GPU-bound by its room grid: TSR 15.1 ms, the batched lamp per carved cell 8.7 ms, base pass 3.4 ms, on an empty city 42 ms. C6b (`9745c51`, 3,000 colonists, 20 districts): surface 28.6, with 135 shuttles 30.9, shuttles + overlay 31.6, floor -1 42.0.
- **`RH.FrameStats`** (`RHCheats.cpp:1629-1690`) logs avg, median, p95 and max frame ms, actor counts, and two game-thread slots (`RHFrameStats::Crew`, `Colony`). **`RH.CityBench N`** (`:1601`) builds `-scale`'s five-floor, one-shaft city.
- **The playtest runs the presentation module.** `rh_playtest.sh` launches `-game -nullrhi` with `RH.Playtest`, so every RedHope subsystem ticks during the byte-identical 110-sol run. The battery is the headless sim suites.
- **The click test** (`RHClickTest.cpp`, `rh_clicktest.sh`): named steps (`traffic.on`, `tunnel.tool`, `cavern.drag`, …) press buttons found by label, and sweeps report `REACH n/m` for every visible button. The official size on this display is 1440x860 (`RH_CLICKTEST_RES`).

---

## 1. The section view

**It sits beside the floor view, as a mode** (Q-A). The floor view stays exactly as it is and remains where you build. The section view is for reading the deep city. Entering it never issues an order, and leaving it puts you back in the floor view.

**Entering and leaving.**
- The deck's shaft strip gains a **Section** button under the SHAFT header (above "Next shaft"), visible once any shaft has `Depth >= 1`. `V` toggles it too. `Escape` leaves it.
- On entry: any armed brush (tunnel, cavern, build) disarms; the selected card closes; the floor-view camera pose and the view level are remembered.
- On exit: the remembered pose comes back, and the view level becomes the slider's floor (section 2), through `SetActiveLevel`, so the usual clamp applies.
- Clicking a floor plate in the section **dives in**: exit, then `SetActiveLevel` to that floor and focus the camera on that cell.
- No world click in the section ever reaches an order path.

**The cut.** The section is a vertical cut plane on a lattice row, in region 0's frame:
- The plane is perpendicular to the camera's view axis, which snaps to one of the four lattice directions (+X, -X, +Y, -Y). `Q`/`E` turn the view by 90 degrees, so the cut axis turns with it.
- By default the plane passes through the column of the **chosen shaft**: the shaft nearest the camera focus (`GetNearestShaftId`), changed with "Next shaft" as now.
- `W`/`S` step the plane one row away from or toward the camera (slicing through the city); `A`/`D` pan along it. "Cut here" on a cavern or atrium card moves the plane through that box's middle row (section 3).
- Everything between the camera and the plane is not drawn: the cells of rows in front, their buildings, and the ground. Cells **on** the plane row are drawn with a cut face (a darker band on the side toward the camera), so the cut reads as a cut.
- Behind the plane, cells are drawn up to `rh.SectionDepth` rows deep (default 16 rows, 160 m), then a single dark rock backdrop closes the view. Rows beyond are not drawn.

**How the floors stack.** At their real depth, as the floor view already places them: floor L's plates at `L x FloorHeightCm`. Twenty floors are 80 m of stack; a dig is 100-200 m across. The camera on entry looks along the cut axis at a low pitch (about 12 degrees; the pawn already allows `-6..86`), zoomed to fit the bored depth and the plane's carved extent, with the chosen shaft's column a little left of centre. The surface is a thin ground strip along the plane with the shaft heads and surface buildings (which keep their floor-view visuals: the colony visualizer runs as if at SURF, section 1.4).

**What is drawn per floor, by level of detail.** The slider's floor is the **focus**. Bands are measured from it.

| Band | What is drawn |
|---|---|
| **Focus ±1 (LOD 0)** | cell plates tinted by room (the floor view's room colours), wall strips where a cell meets rock, buildings as footprint-sized blocks by category, furniture as one small block per furnished cell, resident pips (one pip per resident up to 4 a cell, then a number label on hover), the tunnel tint, the district edge tint |
| **±2 to ±5 (LOD 1)** | plates tinted by room, wall strips, the tunnel tint, the district tint |
| **Further (LOD 2)** | plates only, tinted by D5 Warmth (the floor view's amber lerp) and the depth darkening, no walls |

Always drawn, at every band: the shaft columns (a pale box from head to depth per shaft), the lift cars (section 1.3), cavern frames and atrium voids (section 3), floor labels at the left edge ("-7 · 412 cells · warm"), and robots (section 4).

**How it is drawn (instanced).** One new pure-listener subsystem, `URHSectionVisualizerSubsystem` (RedHope; `RedHopeSim` never depends on it), owns one actor, `RH_SectionRig`, holding hierarchical instanced static mesh components on the unit cube:
- `PlateHISM` (one instance per drawn cell; per-instance custom data: RGB tint and a cut flag), `WallHISM`, `BlockHISM` (buildings and furniture), `PipHISM`, `ShaftHISM` (columns and cars), `FrameHISM` (cavern and atrium edges as thin boxes), and one backdrop plane.
- **No light per cell.** The floor view's lamp per carved cell costs 8.7 ms on one floor (C3); twenty floors of lamps are not affordable. The section uses one unlit, emissive-tinted graybox material for plates, walls and blocks, plus one directional key and one fill attached to the rig. The atrium void is the only emissive accent (section 3).
- Cull distance on each HISM; no shadows on pips, frames or plates (walls and blocks keep them).
- **Rebuild, not per frame.** The instance lists rebuild only when a key moves: `FloorCellsVersion`, `ShaftsVersion`, `TunnelsVersion`, `CavernsVersion`, `DistrictsVersion`, the zoning and props versions the colony visualizer already keys on (`GetPropsVersion`), `GetWarmthVersion` (a tint step), `LiftVersion`, D7's `AtriumsVersion`, and the view's own state (shaft, axis, plane row, focus floor, `rh.SectionDepth`). A focus change rebuilds only the bands that changed LOD.
- Per frame: only the lift car transforms (at most `MaxShaftsCap x MaxLiftCarsCap` = 128 instances, `BatchUpdateInstancesTransforms`) and the overlay arrows.

**Instance bounds.** `-scale`'s 20-floor colony has four digs of 210 cells on every floor (840 a floor). A 16-row slab holds at most about half of that: under 8,000 plates for 20 floors, under 2,500 wall strips, and LOD 0 blocks for three floors only. If a slab ever exceeds `rh.SectionMaxPlates` (default 40,000), LOD 2 floors draw one merged block per dig instead of a plate per cell, and the view says "simplified".

**1.3 Lift cars.** Each fitted shaft draws `LiftCars` boxes in its column. A car's height is a presentation phase: it travels the column top to bottom and back in `2 x Depth x GetShaftSecondsPerFloor` sim seconds, offset by car index. No car is simulated (D4 draws one capsule per line by phase the same way, D6d). A robot riding (`GetRideState`) is drawn at its ride fraction, as D4 already does (`RHAgentVisualizerSubsystem.cpp:1188`).

**1.4 How it coexists with the floor view.**
- `URHColonyVisualizerSubsystem` gains `SetSectionMode(bool)`. While on, it behaves exactly as at SURF for every underground actor (all carve tiles, floor buildings, lamps and the slice rig hidden, the same rule `ApplyViewLevel` uses at `ViewLevel 0`). Its environment rig is also hidden (the ground would cover the cut), and surface buildings stay.
- `URHAgentVisualizerSubsystem` gains a section flag: robots on every level are shown at their real Z (the `:1311` test becomes "on the view floor, or the section is on").
- `URHCrewVisualizerSubsystem` is told `ViewLevel 0`: no full figures or crowd records underground. People below are the section's pips. At 3,000 colonists this keeps the crowd cost at the surface figure (C3: crew tick 0.04 ms at the surface).
- The district, growth, tunnel and cavern visualizers hide while the section is on (they draw one floor). The section draws their content itself, from the same const accessors.
- On exit every visualizer receives the restored view level, and `ApplyViewLevel` runs once, as for any floor change.

---

## 2. The depth slider and its keys

- **The slider.** In section mode the shaft strip's rows become the slider: the focus row is lit (the bright teal the strip already uses for the floor you are on), and clicking a row sets the focus. Beside the rows, a thin vertical `SSlider` spans SURF to the deepest bored floor (`GetShaftDepth()`), one step per floor, so a drag crosses twenty floors in one move; the scroll box follows the thumb so the focus row is always visible.
- **What the focus does:** it sets the LOD bands (section 1), moves the camera's focus Z to that floor (the camera keeps its pitch and yaw), and is the floor the view returns to on exit.
- **Keys** (section mode only; the floor view keeps its own meaning for each):
  - `PageUp` / `PageDown`: focus one floor up or down (the floor view's keys, same meaning);
  - `Shift+PageUp` / `Shift+PageDown`: five floors;
  - `Home`: the surface; `End`: the deepest bored floor;
  - `W` / `S`: step the cut plane; `Q` / `E`: turn 90 degrees; `A` / `D`, the wheel, `R` / `F`: pan, zoom and tilt as now (tilt limited to keep the pitch between 5 and 35 degrees);
  - `V` or `Escape`: leave.
- The clamp is the floor view's: SURF to `-GetShaftDepth()`. A floor no shaft reaches is never a focus.

---

## 3. The cutaway: caverns and atriums

- **A cavern** (`GetCaverns()`) is ordinary cells on `Top..Bottom` (D6a). In the section its box gets a thin teal frame (the cavern visualizer's colour) around all of its floors, so a three-floor cavern reads as one volume even though its floor plates remain (a cavern keeps its floors; D7). Its uncarved points draw as dashed ghost plates while it is dug.
- **An atrium** (D7) has no plates on its open cells (`IsCellOpen`), so the section shows one tall hollow from the hall floor up to the top floor's ceiling, with a railing line on each upper floor's ring and a soft emissive light column (one unlit additive box per atrium, the only translucency in the view). Facing cells (D7's bit 1) get a faint warm edge.
- **"Cut here."** A cavern's or atrium's card (the D6a cavern list, D7's card) gains a **Cut here** button: it enters the section (if needed) and moves the plane to the box's middle row along the current axis, so the cut opens the volume through its centre, and sets the focus to the box's bottom floor.
- **The floor view is not changed** by D8. D7 already lets you look down into an atrium from its floors; `rh.Cutaway` keeps its three modes.

---

## 4. The traffic overlay underground

TRAFFIC keeps its button and meaning. While the section is on, it draws the underground layers below in place of the surface heat wash (roads and rovers on the surface strip still show). Every number comes from a const accessor; the layers refresh at 2 Hz (the overlay's `LoadSeconds`, 0.5 s), never per frame.

- **Lift load.** Beside each fitted shaft's column, a bar whose length is the shaft's load against its capacity: `GetShaftPeopleLoad(S) + GetShaftCargoLoad(S)` over `GetShaftCapacityPlaces(S)`, coloured by `GetTransitWaitSec(S)` against `GetMaxWaitSec()` (calm teal under 25%, amber to 75%, red above). A label: "Shaft 1 · 640 / 1,000 places · wait 45 s · 212 riders" (riders: `GetCommuteShaftPeople(S)`). An unfitted shaft shows "hoist".
- **Deep commute flows.** From `GetCommutePairs`: pairs whose `HomeLevel != WorkLevel` are summed per (home floor, work floor), integers in pair order. Each floor gets an out-arrow and an in-arrow at the plane's left edge, sized by those sums ("-7: 140 up, 35 down"). The arrows meet the shafts' bars, so a crowded lift and the floors feeding it read together. Pairs on one floor are summed as "same floor" and shown only on hover.
- **Trams** (D6d). Each tram line (`FRHTramLine`, from D6d's list accessor) draws along its tunnel's cells on its floor as a band coloured by its wait (`WaitSec`) and labelled with riders (D6d's per-line people count). A line not on the plane's slab shows only in the side legend.
- **Tunnels and robots.** Tunnel cells carry an orange tint in every band. Robots below are drawn by the agent visualizer at their real positions (section 1.4), riders in their columns; the overlay adds a per-tunnel count of robots on its chain, sampled from the agent visualizer's tracked positions at 2 Hz (presentation-side; no sim read).
- **The legend.** The C6b TRAFFIC legend (top left) gains a "below ground" block: lift bar colours, the flow arrows, tram bands.
- **Ramp 0.** Below the commute ramp (60 people) the flows and people loads are 0 (C6, D4 item 8); the overlay says "commuting starts at 61 people" rather than drawing empty arrows.

---

## 5. Frame budget

**Measure.** A new `RHFrameStats::Section` slot wraps the section visualizer's tick; `RH.FrameStats` prints it beside Crew and Colony, plus the section's instance counts. A new cheat, **`RH.DeepBench N`** (RedHope only), builds the 20-floor, three-shaft city through public sim calls `-scale`'s D5 block already uses (`Debug_BuildScaleColony`, `Debug_ExtendShaftOf`, `Debug_SinkShaftAt`, `ExcavateFloor`, `Debug_PlaceInstant`, `Debug_ForceRateFloor`, `Debug_FitLift`, `Debug_SetColonistHome`, `Debug_SetColonistStation`, `Debug_DigTunnel`, `Debug_DigCavern`, `Debug_CarveCavernCells`): 20 floors, three shafts with four cars each, two tunnels, one 8 x 8 x 3 cavern (and one atrium once D7 is built), and N colonists spread over all floors. It adds no sim code.

**Bench runs** (1600x900, the C3/C6b size, and 1440x860, the click-test size; TSR as shipped; 3,000 colonists; 15 s windows):

| Run | Budget (median / p95) |
|---|---|
| A. section, focus -10, TRAFFIC off | **33 ms / 45 ms** (C3's target) |
| B. section, focus -10, TRAFFIC on | **35 ms / 48 ms** |
| C. section, focus -1 and focus -20 | 33 ms / 45 ms each |
| D. surface view, TRAFFIC on, after leaving the section | within 1.0 ms of the same bench's pre-D8 median (C6b: 31.6 ms) |
| E. floor -1 view, after leaving the section | within 1.0 ms of the pre-D8 median (C6b: 42.0 ms) |

Game thread: the section slot at most **1.0 ms** a frame on average (rebuilds excluded), and a full rebuild (a focus change at LOD 0) at most **8 ms**, logged as a spike if over. The pre-D8 medians for D and E are measured on the same machine in the same session, with the build before D8, so the comparison is honest. **A missed budget is a finding for the director, not a budget edit** (D6 precedent).

**Why the budget is reachable.** The floor view's 42-52 ms is its lamp per cell and the TSR pass, not geometry (C3). The section has no per-cell lamps, under 8,000 plate instances in a handful of HISM draws, no crowd below ground, and per-frame work of at most 128 car transforms.

---

## 6. Inertness

**No sim change and no save change.**
- No file under `Source/RedHopeSim` changes. No accessor is added; D8 reads only accessors that exist when it builds. The diff touches `Source/RedHope`, `docs`, `scripts/unreal` and the wording table only.
- No save change: the section's state (on, axis, plane, focus) is presentation state, never saved. The checkpoints are not remade.
- **If a later gate's accessor is missing when D8 builds** (D6d's tram list or per-line riders, D7's `IsCellOpen` and `AtriumsVersion`), that layer is left out and named in the commit; D8 never adds one. The D6d and D7 builders are asked to expose them as const reads.

**The battery.** It runs the sim suites headless; nothing it runs is touched. It must be line-identical to the previous baseline with **no** version lines moved (D8 has no bump).

**The playtest.** It runs the presentation module (`-game -nullrhi`), so D8 must be idle there:
- The section subsystem's `Tick` returns before any sim read while the section is off. The bot never presses Section or `V`.
- The new deck widgets (the Section button, the slider, Cut here) have visibility and label getters that read only `GetShaftDepth()`, `GetCaverns()` and the section's own state, all plain getters with no cache behind them.
- The colony, agent and crew visualizers' new section branches test the flag first; with it off they run today's code.
- Both the default and the `districts=1` 110-sol playtests are byte-identical under the `(N bytes` mask.

**Reads while the section is on** are const. Two of them sit on mutable caches: `GetShaftPeopleLoad` (through `EnsureCommuteFlows`) and `GetCommutePairs`. Those caches are pure functions of saved state, so reading them earlier or more often changes no value, only when a rebuild happens. No battery suite or playtest line prints a rebuild counter while a section is open, because neither opens one.

---

## 7. Tests

**Click test** (`rh_clicktest.sh` at 1440x860; each new button is pressed and checked):
- `section.on`: press Section; the mode is on, no order was queued, the colony visualizer reports section mode.
- `sweep.section`: the REACH sweep with the section on; every visible button is reachable.
- `section.row`: press the -3 row of the shaft strip; the focus is -3.
- `section.slider`: drag the slider thumb from SURF to the bottom; the focus is the deepest bored floor.
- `section.key`: send PageDown; the focus moves one floor.
- `section.cut`: after `cavern.setup`, press Cut here on the cavern's card; the plane is the box's middle row.
- `section.traffic`: Traffic on and off while in the section; the underground layers appear and go.
- `section.dive`: click a floor plate; the section is off and the view level is that floor.
- `section.off`: press Section again; the remembered camera pose and view level are back.
- `leak.total` keeps counting deliberate world clicks: `section.dive` is one more.

**Frame check:** runs A-E of section 5 with `RH.DeepBench 3000` and `RH.FrameStats 15 <tag>`, logged in the commit.

**Captures** (`Saved/RHCapture/d8`, from `RH.DeepBench 3000`, real renderer, looked at before commit):
1. the section on entry: 20 floors, three shafts, cars mid-ride, warm floors amber;
2. focus at -10: LOD 0 detail on -9..-11, plates elsewhere;
3. Cut here through the cavern: its frame over three floors;
4. Cut here through an atrium (once D7 is built): the void and its light column;
5. TRAFFIC on: lift bars, floor arrows, a tram band (once D6d is built);
6. the deck: the slider and the lit focus row;
7. after a dive: the ordinary floor view of the clicked floor.

**Unit-level checks** (in `RHClickTest`, where RedHope code is exercised): the cut-plane math (a cell on the plane row is drawn with a cut face; a cell one row in front is not), the LOD band of each floor for a given focus, and the per-floor flow sums equal a hand sum over `GetCommutePairs` on the click-test colony.

**The bar** (one commit): click test PASS; battery green and line-identical with no moved line; both playtests byte-identical; the frame check inside budget or reported; captures looked at; `RedHopeSim` untouched (`git diff --stat` shows no path under it).

---

## 8. Risks

- **R1. The playtest ticks the presentation module.** A section branch that reads the sim while off would not move the playtest (reads are pure) but would cost frames; the guard is that the section's `Tick` returns first. A deck getter that calls a cached accessor would build caches earlier, which is harmless but must be avoided (section 6).
- **R2. Instance counts at city scale.** A 5,000-colonist city on 20 floors can exceed the plate cap; LOD 2 merges per dig. The cap is a CVar so the director can see both.
- **R3. Translucency.** The atrium light column is the only translucent draw; several atriums on screen cost overdraw. Fallback: an emissive outline only.
- **R4. Later gates' accessors.** D6d's tram list, its per-line riders and D7's open flag must be const reads when D8 builds; otherwise their layers drop (section 6). D8 asks those builders to expose them.
- **R5. Two views of one city can disagree.** The section's tints and labels are recomputed from the same accessors as the floor view; the room colours and the Warmth lerp must come from one shared helper in the colony visualizer, not a copy.
- **R6. The camera.** A low pitch is inside the pawn's clamp, but its zoom curve ties pitch to zoom. Entering the section sets a pitch offset to hold about 12 degrees across the zoom range; a zoom that cannot reach it is clamped and logged.
- **R7. Click-test flake.** One loaded-machine flake has been seen (`manifest.ADD`). Section steps add load; three runs, as for D6a.

## Deviations to log in `docs/design-decisions.md`

1. The section draws people as pips, not figures, below ground.
2. Lift cars in the section move by a presentation phase; no car is simulated (as D4 and D6d).
3. Tunnel robot counts are sampled from the agent visualizer's positions, not from the sim.
4. The floor view's cutaway is unchanged; the cavern and atrium cutaway lives in the section.

## Director questions

**Q-A. Does the section view replace the floor view, or sit beside it?**
- **a (recommended): beside it.** The floor view stays where you build. Section (or `V`) opens the cut-away stack to read the whole deep city, and clicking a floor dives back in. Nothing you know changes.
- b: replace it. The section becomes the only underground view, with building done in the section's focus floor. Every brush, card and click path is reworked, the click test is rebuilt, and the floor view's lamps and props are dropped below ground.

**Q-B. How are people shown below ground in the section?**
- **a (recommended): as pips and flows.** A small dot per resident in each cell of the focus floors, and arrows between floors for commuters. It keeps the frame budget at 3,000 people.
- b: as figures on the focus floor only (the floor view's crowd, at the floor's depth), pips elsewhere. Closer to the floor view's look, with the crowd's cost (C3: about 10 ms at 3,000) added to the section.

---

# PROFESSOR REVIEW (2026-10-23)

Code read at HEAD `a3d5b49` (source identical to `b6aaa8b`; `a3d5b49` adds docs only) with `git show`/`git archive`. The D6c work in the tree (`RHSimCommute.cpp`, `RHSimWorldSubsystem.h`, `RHSimDeepWalk.cpp`) was read with `git diff` only, to check the accessors D8 depends on. Nothing was built or run.

**Verdict: BLOCKED.** The parts that matter most hold up. D8 touches no file under `RedHopeSim`, adds no save field, builds one RedHope subsystem that only listens, rebuilds on version changes, and adds no light per cell. Every HEAD accessor listed in section 0 exists. But three things in the draft would cost a build cycle:
- the way the section coexists with the floor view contradicts itself;
- the section itself is mostly hidden at the specified camera;
- runs D and E of the frame bench cannot be measured as written.

The REVISION must write in R1-R9. Then a check that each item is written suffices; no new full review round is needed.

## REQUIRED

**R1. Coexistence: one source of truth for "which level", and a rule for every consumer.** Section 1.4 runs the colony visualizer "as at SURF", but section 1 says the ground, the buildings and everything in front of the plane are not drawn. At about 12 degrees, the camera sits in front of the plane, above ground, between +4 m and +44 m (focus -10 to -1, distance up to about 210 m). So these would stand between the camera and the stack:
- surface buildings;
- deposit markers, the ship and the pad (`RHColonyVisualizerSubsystem.cpp:1754`, `:2779`, `:2848`);
- the MarsGround actor. It hides only on `IsUnderground()` (`:4737`), a separate test from the environment rig (`:3005-3008`), and the draft names only the rig.

There are also two level sources today:
- `Colony->GetViewLevel()`, read by the crew visualizer (`RHCrewVisualizerSubsystem.cpp:1421`, `:1433`, `:2215`), atmosphere (`RHAtmosphereSubsystem.cpp:152`, which drives the grade and the underground look) and audio (`RHAudioSubsystem.cpp:228`);
- `PC->GetActiveLevel()`, read by:
  - traffic (`RHTrafficVisualizerSubsystem.cpp:693`: every surface layer hides when it is not 0);
  - roads (`RHRoadVisualizerSubsystem.cpp:217`);
  - districts, caverns, tunnels and growth (`:430`, `:270`, `:272`, `:333` of their visualizers);
  - the deck: the lit row (`RHCommandDeck.cpp:2689`, `:2713`), `FollowShaftScroll` (`:2638`) and row visibility (`:2620`).

"Crew is told ViewLevel 0" has no mechanism except the colony's `ViewLevel`. The REVISION must state what `ActiveLevel` is during the section. The recommendation: frozen at the entry floor, plus a `PC->IsSectionOn()`. It must also add a table giving the section rule for each consumer above, plus D6d's tram capsules and D7's atrium un-hide and light. The table must state how surface buildings, crew figures, robots and rovers in front of the plane are clipped or hidden.

**R2. Camera and input in section mode.**
- (a) RMB drag-orbit and MMB pan (`RHPlayerController.cpp:258-262`, `:271-276`) bypass the "snapped to four lattice directions" rule. `IA_Orbit` is a held Axis1D on `Triggered` (`:120-122`, `:206-210`), so "Q/E turn 90 degrees" needs an edge-triggered path. W/S "step one row" needs a rate. State the section behaviour of all four.
- (b) Escape today goes: menu back, then active mode, then the pause menu (`:1853-1867`). Say where "leave the section" sits.
- (c) Pitch is `clamp(lerp(30, 84, ZoomT) + offset, -6, 86)` with the offset at least -42 (`RHStrategyPawn.cpp:59-60`, `:80`). So 12 degrees is reachable only at ZoomT <= 0.444. That is about 210 m of distance and about 188 m of frame width at the 48-degree FOV there. The "5-35 degree tilt limit" also needs code.
  - Either add a section pitch override to the pawn (RedHope, listed in the diff), or have `RH.DeepBench` place its three shafts within about 150 m of each other.
  - `-scale`'s heads are 600 m apart (`RHSimCommandlet.cpp:15976`), so three shafts cannot be framed at a low pitch on that layout.

**R3. The occlusion model.** Plates are 4 m apart. A floor is visible behind the cut only to 4 m / tan(pitch), because the plate above it hides the rest:
- about 19 m (two rows) at 12 degrees;
- under one row at 35 degrees;
- rays above the horizontal see plate undersides.

So on every floor below the top one, rows 3-16 of the default 16-row slab are hidden. That hides the LOD 0 blocks and pips, the district tint and the cavern frames behind the cut, and most of the 8,000 plates draw unseen. Choose one and write it in:
- (a) a stepped cut: floors above the focus band are drawn only on the cut row, so the focus floor reads in depth;
- (b) outline or thin plates for floors that are not the focus;
- (c) a thin ant farm with `rh.SectionDepth` at about 3.

Capture 2 must show LOD 0 detail at the stated depth behind the cut.

**R4. Picking.** `CursorToGround` meets the horizontal plane at `ActiveLevel` (`RHPlayerController.cpp:223-245`), which is useless at 12 degrees. "Dive" and "hover" must pick analytically: the ray against the vertical cut plane gives the floor (Z) and the column. All section geometry is `NoCollision` (the repo precedent: `RHAgentVisualizerSubsystem.cpp:882`). In section mode, `OnClick` (`:1522`) must take a first branch that returns before `CursorToGround` and before any order path. `WorldClickCount` must still count it.

**R5. Instancing details that are not yet true in this repo.**
- (a) No component in the repo uses per-instance custom data (no `NumCustomDataFloats` or `SetCustomData` anywhere). The per-instance tint needs a new material asset, made through MCP. Name it, or bucket plates by colour into separate components.
- (b) The repo uses plain `UInstancedStaticMeshComponent` only. Put the per-frame car transforms on a plain ISM, not an HISM. Updating an HISM's instances rebuilds its tree; that is engine behaviour, not verified here.
- (c) Coalesce focus rebuilds to at most one a frame. A slider drag across 20 floors, or a held PageDown, must not fire 20 rebuilds of up to 8 ms each.
- (d) "Unlit emissive" contradicts "a key and a fill light; walls and blocks keep shadows". Pick one.

**R6. Robots in the section.** The `:1311` change makes every robot on every level visible, including those in front of the plane and beyond the slab.
- Clip robots to the slab.
- The skeletal path is one component per robot (`:958`, `:1321+`). `RH.DeepBench` must state its robot count, and runs A-C include them.
- D4 draws a rider at its true ride fraction in its own lane (`:1188-1190`: `Y + CabHalfY + 125`). Keep the phase-driven car boxes off that lane, so a rider never moves against "its" car.

**R7. The accessor contract per gate. State it in section 6, and which features wait.**
- **HEAD:** everything in section 0 exists. Checked: `GetMaxWaitSec` `:937`, `GetCommuteShaftPeople` `:1972`, `GetCommutePairs` (`HomeLevel`, `WorkLevel`, `Count`) `:1946-1963`, `GetRideState` `:970`, the `Get*Version` getters; `GetPropsVersion` is the colony visualizer's, `GetNearestShaftId` the PC's.
- **D6c (in the tree):** the header diff leaves `FRHCommutePair` unchanged, so the per-floor sums keep working. `ShaftId2` is not public, so "the arrows meet the shafts' bars" is placement only, not attribution. Say so.
- **D6d:** the spec names `FRHTramLine` (with `WaitSec`) and `TramVersion`. It names **no public list getter and no per-line people getter**: `TramPeople` is internal (`d6-caverns-design.md:618-642`). D8's section 0 claim that it "names the accessor that gate's spec gives" is false for trams. Either amend D6d's spec now (`GetTramLines()`, `GetTramPeople(LineId)`, `GetTramVersion()`, all const), or remove the tram band from D8's scope, from capture 5 and from the bar.
- **D6e:** once it lands, the lift bar's denominator is `GetShaftServedCapacityPlaces(S)`. D6e computes the wait from served capacity. Against nominal capacity, the bar reads calm while the wait reads red on a dark sol.
- **D7:** `IsCellOpen` is named. No public getter is named for `AtriumsVersion`, for the atrium list, or for the facing bit (bit 1 lives in a private derived cache, D7 section 1). Drop the "faint warm edge", or have D7's REVISION add `IsCellFacing`, `GetAtriums()` and `GetAtriumsVersion()`. D7 is BLOCKED pending the director's Q-A and Q-B.
- **What waits:**
  - for D6d: the tram band and capture 5's tram part;
  - for D6e: the served-capacity bar;
  - for D7: the atrium void and light column, Cut here on the atrium card, capture 4, and the facing edge.
- Everything else can build on HEAD plus D6c.

**R8. The bench.**
- Runs D and E compare against "the same bench's pre-D8 median, measured with the build before D8". But `RH.DeepBench` is new in D8, so that baseline cannot exist. Either:
  - land `RH.DeepBench` and the `Section` slot as a separate first commit and measure the baselines on it, or
  - redefine D and E as the same build in the same session: never entered, then entered and left. That is what proves nothing leaks.
- Do not quote C6b's 42.0 for E. A 20-floor city carries every floor's tile actors and lamps (`TileByCell` spawns every floor, `RHColonyVisualizerSubsystem.cpp:3460-3532`), so its floor view is a different baseline.
- `RHFrameStats::SlotCount` and the slot initializer (`RHCheats.cpp`, `SlotMs[] = {0.0, 0.0}`) both change. List them.

**R9. Tests that prove the claims.**
- (a) The harness sends pointer events only (`RHClickTest.cpp:263-286`), with no key injection. Add a `ProcessKeyDownEvent` helper, then steps for `V`, Escape (and its priority over the pause menu), Home/End, Shift+PageDown, Q/E (exactly one 90-degree turn a press), W/S (one row) and, after D7, Cut here on the atrium card.
- (b) The slider has no label and the REACH sweep covers buttons. State how the slider's reachability is located and proven.
- (c) The flow-sum check compares `GetCommutePairs` with a sum over `GetCommutePairs`, which proves only the adder. The click-test colony is likely at Ramp 0, where the pairs are empty, so the check passes on zero. Run it on `RH.DeepBench` with Ramp > 0 and commuters between floors, against an independent count (each colonist's home and work floor) and a non-zero expected value.
- (d) Section 6's claim that "reading caches earlier changes no value" holds only if the commute key is complete (`MakeCommuteKey`, `RHSimCommute.cpp:50-72`; `EnsureCommuteFlows`, `:593-614`). Prove it with a live `-RHVerifyCaches` smoke: section open and TRAFFIC on for several sols, with zero `RHVERIFY` lines.

## OPTIONAL

- O1. Spawn `RH_SectionRig` lazily on first entry, so the playtest world gains no actor.
- O2. Read `GetCommuteRamp() <= 0` rather than the literal "61 people". "Riders" is commuters a sol through the shaft (`GetCommuteShaftPeople`), not people riding now: word the label so.
- O3. The tunnel robot count reads the agent visualizer's drawn positions, which come from Mass fragments. Call it "drawn positions", not "no sim read".
- O4. New strings ("simplified", "hoist", the floor labels, the legend block) go into the wording table.
- O5. At 60x speed the phase cars strobe. Clamp the phase rate in real time.

## DIRECTOR QUESTIONS (as they should be posed)

- **Drop Q-A.** Option (b) would rework every D1-D7 click path, against the plan row ("presentation only"). That is not a real choice. Log "beside the floor view" as a deviation.
- **Keep Q-B** (pips, or figures on the focus floor). It is well posed: a real trade between look and about 10 ms.
- **Add Q-C. When does D8 build?** D7 is blocked on your answers, and D6d and D6e are not built. `going-deep-plan.md:46` allows splitting D8.
  - (a, recommended) Build D8 now, on HEAD plus D6c: floors, shafts, cars, tunnels, caverns, districts, lift load and floor flows. The tram band ships inside D6d and the atrium void inside D7.
  - (b) Wait until D6d, D6e and D7 are all built, as drafted.
- **Add Q-D (with R3). How should the section read in depth?**
  - (a) A thin ant farm: the cut face and about two rows behind it.
  - (b) A stepped cut: floors above the focus are trimmed to the cut row, so the focus floor reads in full depth.
  - Both cost about the same in frame time.

VERDICT: BLOCKED. The coexistence mechanism contradicts the draft's own cut rule (R1). The section is mostly hidden at the specified pitch (R3). Runs D and E of the bench cannot be measured (R8). To unblock, the REVISION writes in R1-R9; a written-in check then suffices.

---

# REVISION (2026-10-23)

Code read at HEAD `7b16f6c` with `git show HEAD:` / `git archive HEAD`. Its source is identical to `b6aaa8b` (`git diff --stat b6aaa8b HEAD -- Source` is empty), so the review's anchors hold. Line numbers are HEAD's; each anchor also names its function. The D6c work in the tree was read with `git diff HEAD` only. Nothing was built or run. This REVISION overrides the draft above wherever they differ. R1-R9 are the review's required changes; O1-O5 its optional ones.

**What changed since the review.** (1) The director answered D7 on 2026-10-23: Q-A (a), make an atrium by converting a finished cavern; Q-B (a), no stairs. D7 is no longer blocked. (2) The D6c work in the tree now adds four fields to the public `FRHCommutePair`: `HomeCell`, `WorkCell`, `ShaftId` and `ShaftId2` (working-tree `RHSimWorldSubsystem.h`, after `EdgeIds`). Each reads `INDEX_NONE` for any trip that is not walked. `HomeLevel`, `WorkLevel` and `Count` are unchanged. R7 states how D8 uses the new fields.

---

## R1. One source for "which level", and a rule for every consumer

**The source.** `ARHPlayerController` owns all view state. It pushes a mirror into the colony visualizer, exactly as `SetActiveLevel` pushes `ViewLevel` today (`RHPlayerController.cpp:2380-2402` → `Colony->SetViewLevel`). No consumer keeps its own copy.
- `ActiveLevel` is **frozen at the entry floor** while the section is on. Nothing calls `SetActiveLevel` in section mode except the exit path. A call from anywhere else (the deck's `HandleFloor`, `RH.Floor`) first leaves the section, at that floor (`ExitSection(Level)`), then applies the floor.
- New PC state: `bool bSectionOn`, `int32 SectionFocus` (the slider's floor), `int32 SectionAxis` (0..3: the camera looks along +X, +Y, -X or -Y), `int32 SectionPlaneRow`, `int32 SectionShaft`. New getters: `IsSectionOn()`, `GetSectionFocus()`, `GetSectionAxis()`, `GetSectionPlaneRow()`, and `GetShownLevel()` (`bSectionOn ? SectionFocus : ActiveLevel`). The deck uses `GetShownLevel()` only.
- The colony visualizer gains `SetSectionMode(bool bOn, int32 Focus)`, `IsSectionMode()` and `GetSectionFocus()`. These are pushed by the PC on entry, on every focus change and on exit. Crew, atmosphere and audio already read `Colony->GetViewLevel()`, so they read the mirror from the same object.
- **Exit always re-applies.** `SetViewLevel` returns early when the level is unchanged (`RHColonyVisualizerSubsystem.cpp:4476-4484`). So `SetSectionMode(false, …)` calls `ApplyViewLevel()` itself, before the PC calls `SetActiveLevel(Target)`.

**The rule for anything in front of the cut.** Nothing in front of the plane is drawn. Every world layer that is not the section's own is **hidden** while the section is on, with one exception: robots below ground are drawn, clipped to the slab (R6). The section rig draws all of its content from const accessors: plates, walls, blocks, pips, shafts, cars, frames, and a **surface strip**. The strip is a 1 m crust slab at Z = 0 over the drawn rows behind the plane, with each shaft head as a small block and each Level 0 building whose lattice row lies in the drawn rows as a category block. It is refreshed on the overlay's 2 Hz beat, only when a hash of (Id, DefName, LocationCm) over those buildings changes. Above the strip there is sky only: the environment rig and MarsGround are hidden.

**The colony visualizer's visibility sites go through two helpers.** These are `ShowsLevel(int32 L) = !bSectionMode && L == ViewLevel` and `ShowsSurface() = !bSectionMode && !IsUnderground()`, with `ShowsUnderground() = !bSectionMode && IsUnderground()`. With the section off they are today's expressions exactly. `IsUnderground()` keeps its meaning for non-visibility uses (for example `:3174`). The sites are:

| Site (HEAD) | Today | Becomes |
|---|---|---|
| deposit marker spawn `:1754`, refresh `:4252`, `ApplyViewLevel` `:4527-4532` | `IsUnderground()` / `!bSurface` | `!ShowsSurface()` |
| floor building spawn `:2567`, `ApplyViewLevel` `:4515` | `Level != ViewLevel` | `!ShowsLevel(Level)` |
| pad `:2779`, `:4540`; ship `:2848`, `:4536`; convoy `:4262`, `:4338` | surface tests | `!ShowsSurface()` |
| environment rig `:3005-3008` (`bEnvironmentHidden`) | `IsUnderground()` | `!ShowsSurface()` |
| MarsGround `:4737`, and its search guard `:4710` | `IsUnderground()` | hidden iff `!ShowsSurface()`; the search runs when `IsUnderground() \|\| bSectionMode` |
| shaft 0 cage `:3154-3155`, `ShaftVisual` `:3348`, `:4561`; extra shaft anchors `:3647`, `:3691` | underground tests | `ShowsUnderground() && …` |
| carve tiles: head `:3484`, others `:3506`, `:3528`, `ApplyViewLevel` `:4554` (with each tile's lamp, `LightByCell`) | `ViewLevel != L` | `!ShowsLevel(L)` |
| room, prop and furnishing passes (`RefreshRoomVisuals`, `RefreshFurniture`), the slice rig (`RebuildSliceRig`), the floor fill light | view-level filters | the same helpers; the slice rig and the fill are hidden in section |

The colony's Tick and refresh passes keep running while the section is on, so its actors are current on exit. An actor spawned during the section is born hidden, through the helpers.

**The rule for every consumer.**

| Consumer | Reads today | Section rule |
|---|---|---|
| Colony visualizer | `ViewLevel` | All its actors hidden, through the helpers above. Mirror: `IsSectionMode()`, `GetSectionFocus()`. |
| Crew visualizer: cursor plane `RHCrewVisualizerSubsystem.cpp:1421`, view `:1433`, figure visibility `:2215` | `Colony->GetViewLevel()` | Reads `Colony->IsSectionMode()` first. **Q-B (a):** every figure actor is hidden and no crowd record is drawn; people below are the section's pips. **Q-B (b):** it draws the crowd of `Colony->GetSectionFocus()` only, and each figure is shown iff `Section->IsInSlab(Pos)`. Surface figures are hidden under either answer. |
| Atmosphere `RHAtmosphereSubsystem.cpp:152` | `GetViewLevel() < 0` | `bUnderground = GetViewLevel() < 0 \|\| Colony->IsSectionMode()`: the underground grade. |
| Audio `RHAudioSubsystem.cpp:228` | `GetViewLevel()` | `IsSectionMode() ? GetSectionFocus() : GetViewLevel()`: the focus floor's ambience. |
| Agent visualizer: surface slice (rovers, cargo), `SetViewLevel` `RHAgentVisualizerSubsystem.cpp:867-871` | `Level != 0` | Gains `SetSectionMode(bool)`. While on, `SetSliceHidden(true)`, so rovers and surface robots are hidden. Robots below: R6. |
| Traffic `RHTrafficVisualizerSubsystem.cpp:693` | `PC->GetActiveLevel() != 0` | `bUnder = PC && (PC->IsSectionOn() \|\| PC->GetActiveLevel() != 0)`. Every surface layer (roads heat, rovers, shuttles, the wash) hides. The section's own layers draw instead (section 4). |
| Roads `RHRoadVisualizerSubsystem.cpp:217` | same | Same test as traffic: hidden. |
| Districts `:430`, caverns `:270`, tunnels `:272`, growth `:333` (each `…VisualizerSubsystem.cpp`) | `PC->GetActiveLevel()` | `if (PC && PC->IsSectionOn())`: hide all, then return, before reading the level. The section draws their content itself. |
| Deck: row visibility `RHCommandDeck.cpp:2620`, `FollowShaftScroll` `:2638`, lit row `:2689`, `:2713`, lift card cooling `:2206`, tunnel list key `:2348`, cavern list `:2477` | `GetActiveLevel()` | `GetShownLevel()`. The lists follow the focus, so a cavern's **Cut here** is on the focus floor's list. |
| Deck: Zone row `:508`, `:514`; the brush hints `:2313`, `:2442` | `GetActiveLevel()` | Unchanged. Arming any mode in section mode leaves the section (below). |
| PC Tick: ghosts, brushes, drags, in-flight build holograms, survey discs, traffic hover, selection frames (`RHPlayerController.cpp:290-1520`) | `ActiveLevel`, `CursorToGround` | Not reached: the section branch returns first (R2, R4). |
| PC `OnClick` `:1522` | `CursorToGround` | First branch returns (R4). |
| D6d tram capsules (the visualizer D6d adds; one capsule per line by phase, `d6-caverns-design.md:631`) | its floor | Hidden while `PC->IsSectionOn()`. Under Q-C (a), D6d's builder writes this test. Under Q-C (b), D8 adds it. |
| D7 atrium un-hide (D7 R11: the building, tile, room, prop and furnishing passes, robots, crew wander) and the atrium light (D7 section 8) | `ApplyViewLevel`'s level test | D7's un-hide is written as `ShowsLevel(L) \|\| (!bSectionMode && InAtriumBelowView(L, P))`, and the light is hidden while `bSectionMode`. Under Q-C (a), D7's builder writes this; it is listed under "Required by D8" (R7). Under Q-C (b), D8 converts D7's sites. |

**Arming a mode leaves the section.** At the top of the PC's section branch: `if (HasActiveMode()) { ExitSection(SectionFocus); }` (`HasActiveMode` `:1870`). A deck button that arms a brush therefore lands you in the floor view on the focus floor, with the brush armed. This is one choke point, not one per button. "Next shaft" (`FocusNextShaft` `:2440`) in section mode re-cuts through that shaft's column instead of moving the floor-view camera.

## R2. Camera and input in section mode

**(a) The four inputs.**
- **RMB drag-orbit** (`RHPlayerController.cpp:258-269`) and **MMB pan** (`:271-276`): the whole RMB/MMB block is skipped while `bSectionOn`. Orbit and free pan are locked. An RMB click does nothing (no mode can be active).
- **Q/E, edge-triggered.** `IA_Orbit` (Axis1D, `:121-123`) keeps its `Triggered` binding (`:167`), and `OnOrbit` (`:206-212`) returns at once while `bSectionOn`. A second binding of the same action, `Input->BindAction(IA_Orbit, ETriggerEvent::Started, this, &ARHPlayerController::OnSectionTurn)`, fires once per press. It does nothing in the floor view. In the section, it calls `TurnSection(Sign)`: `SectionAxis = (SectionAxis + Sign + 4) % 4`, the pawn's yaw target becomes exactly `SectionAxis x 90`, and the plane re-derives through the same lattice column (the cut keeps its centre).
- **W/S, one row per tap, with a held rate.** `OnPan` (`:186-196`) in section mode splits its axis:
  - X (W/S) steps the plane: one row on the press (W away from the camera, S toward it). A held key repeats every 0.25 s after 0.4 s. The press and the repeat clock reset on a new `ETriggerEvent::Completed` binding of `IA_Pan`.
  - Y (A/D) pans along the plane only. It is today's `AddPan` with X zeroed, which follows the plane because the yaw is snapped.
  - The plane row is clamped to the carved extent along the depth axis, ±2 rows.
- **The rest.** The wheel zooms as now. R/F tilt the section pitch inside 5-35 degrees. T is ignored. PageUp/PageDown move the focus one floor, with Shift five floors (`OnFloorUp`/`OnFloorDown` `:2405-2413` read `IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift)`). New Boolean actions: `IA_Section` (`V`, Started) toggles the section, and `IA_SectionHome` (`Home`) and `IA_SectionEnd` (`End`) do nothing outside the section. `V` is ignored while a menu (`IsMenuOpen()`) or the title is open, and while no shaft has `Depth >= 1`.

**(b) Escape's slot.** `OnCancel` (`:1853-1867`) becomes: menu open → `Menus->HandleBack()`; **else section on → `ExitSection(SectionFocus)`**; else active mode → `CancelModes()`; else the pause menu. A menu over the section closes first. The second Escape leaves the section, and the third opens the pause menu.

**(c) The pitch: a pawn override.** At HEAD the pitch is `clamp(lerp(30, 84, ZoomT) + offset, -6, 86)` with the offset clamped to -42..30 (`RHStrategyPawn.cpp:55-56`; the offset clamp at `:84` and `:92`). This REVISION takes the **override**, so the section's pitch no longer depends on zoom:
- `ARHStrategyPawn` gains `SetSectionView(bool bOn, float PitchDeg, float YawDeg)`. While on, Tick uses `Pitch = SmoothedSectionPitchDeg` in place of the zoom-coupled pitch. The distance and FOV still follow `ZoomT`. `AddTilt` adjusts `SectionPitchDeg` inside [5, 35]. The yaw eases to the exact multiple of 90.
- It also gains `GetCameraPose(FVector2D&, float& ZoomT, float& PitchOffset, float& Yaw)`, the getter `SetCameraPose` (`:87`) lacks, so the PC can remember the floor-view pose.
- Entry pitch: **8 degrees** for the ant farm, **30 degrees** for the stepped cut (R3).
- On entry, ZoomT is solved so the carved extent along the plane, plus 10%, fills the frame width (`2 x Distance x tan(FOV/2)`, iterated, since the FOV depends on ZoomT).
- **The bench lays its shafts out for one cut as well** (R8). They stand on one lattice row, 80 m apart, so a single plane passes through all three columns. The view is framed at about 375 m wide (ZoomT ≈ 0.6, distance ≈ 440 m, FOV ≈ 46 degrees). With the override, that framing holds any pitch in 5-35 degrees.
- `-scale`'s heads (600 m apart, `RHSimCommandlet.cpp:15976`) are not used by the bench.

## R3. Depth readability: both options written; Q-D picks one

Plates are 4 m apart (`GetFloorHeightCm`, `.h:668`). Behind the cut, a floor is visible to about `4 m / tan(pitch)`, because the plate above hides the rest.

**Slab coordinates (both options).** For axis k, a lattice point P = (x, y) in region 0's frame (`CmToLattice(GetShaftHeadCm(), …)`, `.h:776`, `:791`) has:
- a depth `d(P)` = (x, y, -x, -y)[k], and an along coordinate `a(P)` = (y, -x, -y, x)[k];
- `r = d(P) - SectionPlaneRow`. It is in front when r < 0, on the cut when r = 0, and behind otherwise.

Floor L draws the cells with `0 <= r < Rows(L)`. Cells with r = 0 carry the cut face: a darker band on the camera side. Each floor's slab ends in the backdrop, a dark rock card one row behind its last drawn row. `IsInSlab(Pos)` (R6) uses the same test.

**Option (a): the thin ant farm.**
- `Rows(L) = 3` on every floor: the cut row and two behind it (`rh.SectionDepth` default 3).
- Entry pitch 8 degrees: 4 m / tan 8° = 28.5 m, so rows 0-2 (0-30 m) read on every floor, at about 95% of row 2.
- The LOD bands of the draft's section 1 table apply unchanged.
- Bench bound: the three-shaft dig is about 31 cells along the plane, so 3 rows x 31 x 20 floors ≈ 1,900 plates.
- **Capture 2:** focus -10. LOD 0 blocks and pips are visible on rows 0, 1 and 2 of floors -9, -10 and -11. The log line beside it names "pitch 8, rows 3".

**Option (b): the stepped cut.** The floors above the focus are trimmed to the cut face, so the focus floor reads in full depth.
- `Rows(Focus) = rh.SectionDepth` (default 16, 160 m), drawn at LOD 0.
- Each floor above the focus is drawn only as its cut face: the r = 0 cells, as a 1 m-deep strip (plates, walls and room tint; no blocks or pips).
- Each floor below the focus is drawn as its r = 0 row only. The focus plate hides the rest: at 30 degrees, a floor below shows only its front 6.9 m.
- Entry pitch 30 degrees. A strip at height h hides only a 1 m band of the focus floor at depth `h / tan 30°` (6.9 m, 13.9 m, 20.8 m, …), never the full depth.
- Bench bound: 16 x 31 + 19 x 31 x 2 ≈ 1,700 plates.
- **Capture 2:** focus -10. LOD 0 blocks and pips are visible on floor -10 from about 8 m to at least 100 m behind the cut, with the floors above as cut-face strips. The log line names "pitch 30, rows 16".

**Under both options.**
- `rh.SectionMaxPlates` (default 40,000) and its "simplified" fallback stay as a guard; the bench does not reach it.
- Cavern frames and atrium voids are clipped to the drawn rows of each floor, so a frame reads as the part of the box the cut opens.
- The builder builds only the option the director picks. There is no CVar for the other.

## R4. Picking against the vertical cut plane

**Analytic pick.** `SectionPick(int32& OutLevel, int32& OutOrdinal)`:
1. `DeprojectMousePositionToWorld` gives a ray. Intersect it with the cut face, the vertical plane `d = SectionPlaneRow - 0.5` rows (in cm, through region 0's origin).
2. The hit's Z gives the floor: `L = clamp(floor(Z / FloorHeightCm), -GetShaftDepth(), -1)`. Its along coordinate gives the column: `round(a)`.
3. Starting at r = 0 and walking back to `Rows(L) - 1`, take the first carved point on that column (`FindFloorCell(L, P)`, `.h:769`).
4. No hit, or no carved point: nothing is picked.

`CursorToGround` (`:223-245`) is never called in section mode.

**No collision.** Every section component calls `SetCollisionEnabled(ECollisionEnabled::NoCollision)` and `SetGenerateOverlapEvents(false)`, after the repo precedent (`RHAgentVisualizerSubsystem.cpp:882`).

**`OnClick` returns first.** The first lines of `OnClick` (`:1522`) become:
```
++WorldClickCount;                       // still counted: the click test's leak counter
if (bSectionOn) { HandleSectionClick(); return; }
```
These come before the `Sim` lookup, `CursorToGround` and every order path. `HandleSectionClick` picks. On a hit it dives: `ExitSection(L)`, then `Cam->SetFocusXYCm(cell centre)` (`GetFloorCellCentreCm`, `.h:773`). It issues no order.

**Tick and hover.** In `Tick`, right after `HintText.Reset()` (`:287-288`): `if (bSectionOn) { TickSection(); return; }`. `TickSection` handles the "arming leaves" choke point (R1), the W/S repeat clock (R2), and hover: the same pick, which sets the hover label and the hint "Click a floor to go there" (placeholder). The rest of Tick (`:290-1520`: ghosts, drags, holograms, survey discs, traffic hover, selection frames) is not reached.

## R5. Instancing

- **(a) The tint: a named material asset.**
  - `/Game/RedHope/Art/M_SectionTint`, authored through the Unreal MCP server before any C++ uses it.
  - Unlit, opaque. Emissive = `PerInstanceCustomData[0..2]` x a fixed face factor: `0.55 + 0.45 x saturate(dot(VertexNormalWS, normalize(0.3, 0.5, 0.8)))`, so box faces read without lights. Then x `(1 - 0.35 x PerInstanceCustomData[3])`, the cut-face flag.
  - Plate, wall and block components set `NumCustomDataFloats = 4` and write with `SetCustomData` during a rebuild only.
  - A capture of one tinted test instance is taken before the components are built on it.
  - **Fallback**, if MCP cannot author the `PerInstanceCustomData` node: one component per colour. The palette is fixed at 24 entries: the room colours, five Warmth steps and five depth steps. Each component uses an `M_Graybox` MID with `Tint` black and `Emissive` = the palette colour, the pattern of `GetCabBox` (`RHAgentVisualizerSubsystem.cpp:887-890`). The builder states in the commit which path was taken.
- **(b) Plain ISM only.** Every section component is a plain `UInstancedStaticMeshComponent`, as in the rest of the repo. There is no HISM.
  - The moving lift cars live on their own `CarISM`, updated once a frame by `BatchUpdateInstancesTransforms(0, CarTransforms, /*bWorldSpace*/ true, /*bMarkRenderStateDirty*/ true)`.
  - The static components change only in a rebuild.
- **(c) At most one rebuild a frame.**
  - Focus, plane, axis, depth, style, and every version key (draft section 1) only set `PendingRebuild` bits and the latest target.
  - The subsystem's Tick runs at most **one** rebuild a frame, against the latest state, so a slider drag across 20 floors or a burst of PageDown presses is at most one rebuild per frame, never a queue.
  - A focus-only change rebuilds only the floors whose LOD band changed.
- **(d) Unlit.** The section is unlit: no key light, no fill, no shadows (`CastShadow = false` on every section component). This supersedes the draft's "one directional key and one fill" and "walls and blocks keep shadows". The only translucent draw is D7's atrium light column (one additive box per atrium).

## R6. Robots in the section

- **Clipped to the slab.** At `RHAgentVisualizerSubsystem.cpp:1311`, `bOnView` becomes `bSectionMode ? (bLifts && Section->IsInSlab(Pos)) : (today's test)`.
  - `IsInSlab(Pos)` is true iff `LevelOfZ(Pos.Z) < 0` (`.h:947`), the point's r lies in `[0, Rows(L))` for that level (R3), and its along coordinate lies within the carved extent.
  - Robots in front of the plane, beyond the slab, or on the surface are hidden.
  - A rider (`GetRideState`) is drawn in its column when the column is in the slab.
  - The same `bOnView` gates the robot car's floor plate (`GetCabBox`, `:1312-1319`).
- **The count.** `RH.DeepBench` spawns **60 RC_H robots**: three per floor, on carved cells of rows r = 0..2 behind the bench's default plane, one in each third of the dig. They come from `URHAgentSubsystem::SpawnRobot` (`RHAgentSubsystem.h:67`, public) with the definitions' RC_H row, and are handed to `URHAgentVisualizerSubsystem::TrackEntities` (`.h:45`, the precedent at `RHCheats.cpp:296`). The colony's starting fleet stays on the surface, hidden in the section.
  - With the skeletal walker, that is one component per robot (`:958`, `:1321+`).
  - `[DeepBench]` logs both counts. `[FrameStats]` gains "robots drawn N" (R8), and runs A-C are measured with them.
  - These are harness robots: not in the sim's saved fleet. The bench never saves.
- **Car boxes stay out of D4's rider lane.**
  - D4 draws a rider and its car plate at `Y = head.Y + CabHalfY + 125` = head.Y + 270 cm (`:1188-1190`; `RHElevator::CabHalfY` = 145, `RHElevatorLayout.h:23`).
  - The section's phase cars are confined to the crew cab rectangle, `head ± (CabHalfX, CabHalfY)` = ±110 x ±145 cm (`RHElevatorLayout.h:22-23`, `:47-48`).
  - Car k of n takes lane k, splitting that rectangle along the plane's in-screen axis.
  - No car box reaches `Y >= head.Y + 145`, so a rider never shares a lane with a phase-driven car.
- **The phase rate (O5).** Car height follows the draft's phase (`2 x Depth x GetShaftSecondsPerFloor` sim seconds a round trip, `.h:920`), with its rate clamped to at most 2 floors per real second. At 60x the cars glide instead of strobing.

## R7. The accessor contract per gate

| Gate | What D8 reads | Status | If absent when D8 builds | Q-C (a): build now | Q-C (b): build last |
|---|---|---|---|---|---|
| **HEAD** (`7b16f6c` = `b6aaa8b` source) | `GetFloors` `.h:762`, `GetFloorCell` `:767`, `FindFloorCell` `:769`, `GetFloorCellCentreCm` `:773`, `CmToLattice` `:776`, `GetFloorCellsVersion` `:781`, `GetShaftHeadCm` `:791`, `GetFloorHeightCm` `:668`, `GetFloorWarmth` `:693`, `GetWarmthVersion` `:695`, `GetShafts` `:728`, `GetShaftsVersion` `:741`, `GetShaftDepth` `:724`, `GetTunnels` `:801`, `GetTunnelsVersion` `:804`, `GetCaverns` `:848`, `GetCavernsVersion` `:851`, `GetLiftCars` `:897`, `GetLiftVersion` `:899`, `GetTransitWaitSec` `:903`, `GetShaftCapacityPlaces` `:909`, `GetShaftPeopleLoad` `:912`, `GetShaftCargoLoad` `:913`, `GetShaftSecondsPerFloor` `:920`, `GetMaxWaitSec` `:937`, `LevelOfZ` `:947`, `GetRideState` `:970`, `GetColonists` `:1080`, `GetFloorRoomCells` `:1334`, `GetColonistStation` `:1615`, `GetBuildings` `:1798`, `FRHCommutePair` `:1946-1954`, `GetCommuteRamp` `:1955`, `GetCommutePairs` `:1963`, `GetCommuteShaftPeople` `:1972`, `GetDistricts` `:2019`, `GetDistrictsVersion` `:2021`; RedHope: `GetPropsVersion` (colony visualizer `.h:78`), `GetNearestShaftId` (PC `:2415`) | all exist | n/a | build | build |
| **D6c** (in the tree) | `FRHCommutePair::HomeLevel/WorkLevel/Count` (unchanged). New in the tree: `ShaftId`, `ShaftId2` (public, walked trips only) | uncommitted | The sums work at HEAD. | D8 builds after D6c commits. If the fields land public, the arrows of a walked trip (`ShaftId != INDEX_NONE`) attach to that shaft's bar, and to `ShaftId2`'s too when set. Every other trip's arrow is **placed** at the plane's left edge, not attributed. If they do not land public, every arrow is placement only. The legend says "placed beside the lifts, not counted per lift" (placeholder) for unattributed arrows. | same |
| **D6d** (designed) | `GetTramLines()` → `const TArray<FRHTramLine>&`; `GetTramPeople(int32 LineId)` → `int32` (commuters a sol on the line, as `GetCommuteShaftPeople`); `GetTramVersion()` → `uint32`. All const. | **Not in D6d's spec** (`d6-caverns-design.md:618-642` names `FRHTramLine`, `WaitSec` and an internal `TramPeople` only) | The tram band, capture 5's tram part and `section.traffic`'s tram check are left out and named in the commit. | **Required by D8 in D6d's build** (below). D6d also ships the section's tram band, its capture and its check, and hides its capsules in section mode (R1). | D8 draws the band; D6d must have added the three getters. |
| **D6e** (designed) | `GetShaftServedCapacityPlaces(S)` (`d6-caverns-design.md:665`) | not built | The lift bar's denominator is `GetShaftCapacityPlaces(S)`, and the label reads "nominal". | D6e's build swaps the bar's denominator (one RedHope line) and drops "nominal". | D8 uses served capacity from the start. |
| **D7** (designed; **director answered Q-A (a) and Q-B (a) on 2026-10-23**, so not blocked) | `IsCellOpen(L, Ordinal)` (named, D7 section 1); `IsCellFacing(L, Ordinal)`; `GetAtriums()` → `const TArray<FRHAtrium>&`; `GetAtriumsVersion()` → `uint32` | `IsCellOpen` named; the other three **not in D7's spec** | The atrium void, light column and facing edge, Cut here on the atrium card, capture 4 and `section.cut.atrium` are left out and named in the commit. | **Required by D8 in D7's build** (below). D7 also ships those section layers and their capture and step. | D8 draws them; D7 must have added the getters. |

**Required by D8 in D6d's build.** All are const reads in `RHSimWorldSubsystem.h`, beside `GetCommuteShaftPeople`:
1. `const TArray<FRHTramLine>& GetTramLines() const;` (ascending `Id`)
2. `int32 GetTramPeople(int32 LineId) const;` (0 at Ramp 0; reads the same flows as `GetCommuteShaftPeople`, so the commute's `-RHVerifyCaches` compare must include `TramPeople`, as it includes `ShaftPeople`: `RHSimCommute.cpp:605-607`)
3. `uint32 GetTramVersion() const;`

**Required by D8 in D7's build.** All are const reads:
1. `bool IsCellFacing(int32 Level, int32 Ordinal) const;` (bit 1 of `FRHAtriumCells::Flags`, D7 section 1, through the same cache as `IsCellOpen`)
2. `const TArray<FRHAtrium>& GetAtriums() const;` (ascending `CavernId`, D7 section 2)
3. `uint32 GetAtriumsVersion() const;`
4. The atrium-cells cache is re-derived and compared under `-RHVerifyCaches`, with an `RHVERIFY` Error on a stale reuse, because presentation reads it.
5. D7's un-hide and light go through `ShowsLevel` and `bSectionMode` (R1).

The orchestrator copies these two lists into the D6d and D7 build briefs. This doc is where they are written, and the D6 and D7 docs are not edited.

**Section 6 is amended.** Its sentence "D8 reads only accessors that exist when it builds" stands. Section 0's "names the accessor that gate's spec gives" is withdrawn for trams and atriums: the accessors are the ones listed above. **What waits:** for D6d, the tram band and capture 5's tram part; for D6e, the served-capacity bar; for D7, the atrium void and light column, the facing edge, Cut here on the atrium card, and capture 4. Everything else builds on HEAD plus D6c.

## R8. The bench

**Runs D and E are redefined inside one build.** There is no separate DeepBench commit. What they prove is that entering and leaving the section leaks nothing, so the baseline is the same build in the same session, with the section never entered:

| Run | What | Budget |
|---|---|---|
| E0 | `RH.Floor -1`, TRAFFIC off, section never entered | baseline |
| D0 | `RH.Floor 0`, TRAFFIC on, section never entered | baseline |
| A | `RH.Section on`, focus -10, TRAFFIC off | 33 ms median / 45 ms p95 |
| B | as A, TRAFFIC on | 35 / 48 |
| C | focus -1, then focus -20 | 33 / 45 each |
| D1 | `RH.Section focus 0`, `RH.Section off` (the remembered surface pose returns), TRAFFIC on | median within 1.0 ms of D0 |
| E1 | `RH.Floor -1`, TRAFFIC off | median within 1.0 ms of E0 |

- In D0, E0, D1 and E1 the `Section` slot is at most 0.01 ms a frame. Its Tick returns before any read while the section is off.
- In A-C the slot is at most 1.0 ms a frame on average, rebuilds excluded. A full rebuild is at most 8 ms, logged as a spike if over.
- C6b's 42.0 ms is **not** quoted for E. A 20-floor city carries every floor's tile actors and lamps (`TileByCell`, `RHColonyVisualizerSubsystem.cpp:3460-3532`), so E0 is its own baseline.
- Runs are taken at 1600x900 and 1440x860, 15 s windows, with 3,000 colonists and the 60 robots. A missed budget is a finding for the director, not a budget edit.

**`RH.DeepBench N`** (RedHope, `RHCheats.cpp` beside `RH.CityBench` `:1601`; public sim calls only):
1. `Debug_BuildScaleColony(N)`: five floors, shaft 0 at (3000, 3000) cm, column (0, 0) (`RHSimWorldSubsystem.cpp:3750`, `:3790-3791`); then `Crew->Debug_SettleCrew()` as `RH.CityBench` does.
2. `Debug_ExtendShaftOf(0, 20)`. Then `ExcavateFloor(-L, 210, …, 0)` for L = 6..20, an `AirFilter` per new floor (`Debug_PlaceInstant`), and `Debug_ForceRateFloor` on each: the `-scale` D5 recipe (`RHSimCommandlet.cpp:16411-16425`).
3. Two more shafts **on lattice row y = 0**, at columns (+8, 0) and (-8, 0), which shaft 0's 210-cell spiral leaves free. For each: a `Pylon` at the head for coverage, then `Debug_SinkShaftAt(LatticeToCm(origin, column), 20, …)` (`RHSimWorldSubsystem.cpp:7633`; `CheckSinkColumn` `:7524` has no spacing rule), then `ExcavateFloor(-L, 210, …, S)` on all 20 floors. The three digs touch and merge into one dig about 31 cells along row 0. The shafts are 80 m apart and 160 m end to end, so one cut on row 0 passes through all three columns.
4. Four cars on each shaft: `Debug_AddSolid(Lander, Struct, GetLiftFitBillKg())` then `Debug_FitLift(S, …)`, four times each, as `-scale` does (`RHSimCommandlet.cpp:16249-16257`).
5. Two tunnels on row 0 (`Debug_DigTunnel`), on -4 and -14, from the dig's +X edge, 10 cells out.
6. One 8 x 8 x 3 cavern on -8..-10 (`Debug_DigCavern`, then `Debug_CarveCavernCells(Id, 192)`), sited by the `-scale` search (`CanDigCavern` over `Dx 8..24`, `Dy -8..8`, `RHSimCommandlet.cpp:16606-16619`). D7's atrium is opened on it once D7 is built.
7. Commuters between floors: 40% of colonists are re-homed to -6..-20 (`Debug_SetColonistHome`) and given stations on other floors (`Debug_SetColonistStation`), so `GetCommuteRamp() > 0` and many trips cross floors.
8. 60 RC_H robots (R6).
9. One `[DeepBench]` line: floors, the shafts' columns, cars, tunnels, the cavern box, colonists housed, Ramp, cross-floor commuters (the R9(c) independent count), and robots spawned and tracked.

**New cheat, `RH.Section <on [axis]|off|focus N|axis K|plane R|check flows>`** (RedHope). The bench, the captures and the smoke drive the section with it; `check flows` is R9(c).

**The `RHFrameStats` slot changes:**
- `RHFrameStats.h`: `enum ESlot : int32 { Crew = 0, Colony = 1, Section = 2, SlotCount = 3 };`
- `RHFrameStats.cpp:5`: `double GSlotSeconds[SlotCount] = { 0.0, 0.0, 0.0 };`
- `RHCheats.cpp:1640`: `FFrameStatsRun::SlotMs[RHFrameStats::SlotCount] = { 0.0, 0.0, 0.0 };`. The review placed the zero initializer in `RHCheats.cpp` only; it is in both files.
- `RHCheats.cpp:1677-1680`: the `[FrameStats]` format gains "section tick %.3f", the section's instance counts (plates, walls, blocks, pips, cars) and "robots drawn N".
- `RHCheats.cpp:1717-1718`: the spike string gains "section %.1f".
- `URHSectionVisualizerSubsystem::Tick` opens with `RHFrameStats::FScope StatScope(RHFrameStats::Section);`, as the colony's does (`RHColonyVisualizerSubsystem.cpp:2990`).
- The loops at `:1707` and `:1720-1724` already run to `SlotCount` and need no edit.

## R9. Tests that prove the claims

**(a) Key presses in the click harness.** `RHClickTest.cpp` sends pointer events only (`:263-305`). New helpers, beside `SendDown` and `SendUp`:
- `SendKey(FKey Key, bool bShift, float HoldSec)`:
  1. `FSlateApplication::Get().SetAllUserFocusToGameViewport()`.
  2. If `bShift`, `ProcessKeyDownEvent(FKeyEvent(EKeys::LeftShift, …))`.
  3. `ProcessKeyDownEvent(FKeyEvent(Key, FModifierKeysState(bShift, …), /*UserIndex*/ 0, /*bIsRepeat*/ false, 0, 0))`.
  4. Wait `HoldSec`, then `ProcessKeyUpEvent` for Key, then for Shift.
- `PushKey(Name, Key, bShift = false, HoldSec = 0.1f)` mirrors `PushPress` (`:530`). It records `LeakBase`, so `PushCheck` fails if a key reached `OnClick`.
- `SendDown` and `SendUp` gain an `FKey Button` parameter (default `LeftMouseButton`), so RMB and MMB drags can be sent.

New steps, run in this order after the existing script and before `leak.total`. `section.setup` calls `Debug_ExtendShaftOf(0, 6)` if the shaft is shallower, as `cavern.setup` uses harness calls:

| Step | Action | Pass |
|---|---|---|
| `section.on` | press **Section** | `IsSectionOn()`, `Colony->IsSectionMode()`, no order queued |
| `sweep.section` | REACH sweep (with (b)) | every visible button and the slider reachable |
| `section.row` | press the -3 row | focus -3 |
| `section.slider` | drag the slider (b), top end to bottom end, then click its middle | focus `-GetShaftDepth()`, then `round(-Depth / 2)` |
| `section.key` | PageDown | focus moves exactly one floor |
| `section.home` / `section.end` | `Home`, then `End` | focus 0, then `-GetShaftDepth()` |
| `section.shift` | `Home`, then Shift+PageDown, then PageUp | focus -5, then -4 |
| `section.turn` | Q tap; E tap; Q held 1.0 s | yaw target -90 exactly and the axis index -1; then back; then exactly one more turn (edge-triggered) |
| `section.step` | W tap; S tap; W held 1.0 s | plane row +1; back; more than one row (the repeat) |
| `section.locks` | RMB drag 200 px; MMB drag 200 px | yaw unchanged; focus XY unchanged |
| `section.cut` | after `cavern.setup`, Cut here on the cavern's card | plane = the box's middle row, focus = the box's bottom floor |
| `section.traffic` | Traffic on and off | the section's layers appear and go; the surface traffic layers stay hidden |
| `section.esc.menu` | `pause.setup`'s `OpenPauseMenu()`, then Escape | the menu is closed **and** the section is still on |
| `section.esc` | Escape | the section is off **and** `IsMenuOpen()` is false |
| `section.V` | V, then V | on, then off (the remembered pose and level are back) |
| `section.dive` | V, then a click on the -2 plate (`LocateSectionPlate(-2)`) | section off, view level -2; `ExpectedWorld` +1 |
| `section.cut.atrium` | (with or after D7) Cut here on the atrium card | the plane is through the atrium's middle row |
| `section.arm` | V, then press the Tunnel brush | the section is off and the brush is armed (R1 choke point) |

- `LocateSectionPlate(Level)` asks the section subsystem for the cut-face centre of the first carved r = 0 cell on that floor. It projects it with a new `WorldPointToSlate(FVector)`, because `WorldToSlate` (`:307`) pins Z to `ActiveLevel`.
- **Unit checks**, also in `RHClickTest`, against the section's pure static helpers: the slab classification (R3: front, cut and behind for all four axes), `Rows(L)` for the chosen option, and the LOD band of each floor for a focus.

**(b) The slider.**
- The `SSlider` gets `.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("RH.SectionSlider")))`.
- The harness gains `LocateTagged(FName Tag, float Fraction)`: the point at `Fraction` along the tagged widget's long axis (0 = SURF end, 1 = deepest).
- `Sweep` (`:214-258`) also gathers widgets whose `FTagMetaData` tag starts with `RH.`. Each is tested with `ReachesAt` at its centre and at both ends, and counts in `REACH n/m` as `'<RH.SectionSlider>'`.
- `section.slider` drags `LocateTagged(…, 0)` → `LocateTagged(…, 1)` with `PushDrag` (`:554`).

**(c) The flow sums against an independent count, at a non-zero ramp.** This runs live on `RH.DeepBench 3000`, where the ramp is above 0 and many trips cross floors, not on the click-test colony. `RH.Section check flows` computes two things.
- **The section's sums:** what the overlay draws, Σ `Count` over `GetCommutePairs` grouped by (`HomeLevel`, `WorkLevel`) with `HomeLevel != WorkLevel`.
- **The expected sums, from the roster, never from the commute.** For each colonist in `GetColonists()` with a station (`GetColonistStation`) and a home (`HomeHab != 0` or `HomeLevel < 0`), the eligibility rule of `DeriveCommuteFlows`, `RHSimCommute.cpp:357-368`:
  - home floor = `HomeHab != 0 ? that building's Level : HomeLevel`;
  - work floor = the station key's `X` (≤ 0);
  - count where they differ.

It logs `[SectionCheck] flows: N cross-floor commuters in K floor pairs, matches=1|0`, and the first differing pair if any. It passes iff every pair matches and N ≥ 500, a floor the bench guarantees by step 7. It also fails, and says so, if `GetCommuteRamp() <= 0`.

**(d) A live `-RHVerifyCaches` smoke.**
- `scripts/unreal/rh_capture.sh` gains an `RH_CAPTURE_EXTRA` passthrough before `-ExecCmds` (its launch line, `:61-63`), like `RH_PLAYTEST_EXTRA`.
- The run: `RH_CAPTURE_EXTRA=-RHVerifyCaches`, with commands `RH.DeepBench 3000`, `RH.Section on`, `RH.Traffic overlay 1`, `RH.Section check flows`, `RH.SetSpeed 8`, then after three sols (`RH.After 460 …`; a sol is 1,200 sim s, `RHSimClockSubsystem.h:26`) `RH.Section check flows` and an `RH.Snapshot`.
- **Pass:** zero `RHVERIFY` lines in the log; both check lines read `matches=1`; and the overlay's 2 Hz refresh count is above 0 over the window (logged by the check), so the section really read the cached accessors while the sim moved.
- The log and the count go in the commit.

**The bar** (one commit, or the subset Q-C (a) allows):
- the click test PASS, three runs (R7 of the draft's risks);
- the battery green and line-identical, with no moved line;
- both 110-sol playtests byte-identical;
- runs A-E1 inside budget, or reported;
- the `-RHVerifyCaches` smoke clean;
- captures 1-7 looked at (capture 4 and the tram part of 5 per Q-C);
- `RedHopeSim` untouched (`git diff --stat` shows no path under it).

---

## The optional items

- **O1, adopted.** `RH_SectionRig` is spawned lazily on first entry, so the playtest world gains no actor. The subsystem's Tick returns before any read while the section is off and never has been on.
- **O2, adopted.** The ramp test reads `GetCommuteRamp() <= 0` (`.h:1955`), and the overlay then says "No commuting yet" (placeholder) instead of a literal people count. The lift label reads "Shaft 1 · 640 / 1,000 places · wait 45 s · 212 commuters a sol" (placeholder), because `GetCommuteShaftPeople` counts commuters a sol through the shaft, not people riding now.
- **O3, adopted.** The tunnel robot count is "sampled from the agent visualizer's drawn positions (Mass fragments)", not "no sim read". Deviation 3 is reworded the same way.
- **O4, adopted.** New strings go into `docs/qa/2026-10-15/gate-d-wording.md` under a fresh prefix, `M`. It is free at HEAD (`L` is reserved by D7); take the next free letter if another gate claims `M` first. The strings are "simplified", "hoist", "nominal", the floor labels, the legend's "below ground" block, "No commuting yet", the commuter label, the unattributed-arrow note and the hover hint. All are placeholders.
- **O5, adopted.** In R6 (the phase rate clamp).
- **Rejected:** none.

## The diff (for the builder)

- **New:** `Source/RedHope/Public/RHSectionVisualizerSubsystem.h`, `Source/RedHope/Private/RHSectionVisualizerSubsystem.cpp`; `/Game/RedHope/Art/M_SectionTint` (R5a, through MCP).
- **Changed (RedHope):** `RHPlayerController.h/.cpp` (R1, R2, R4); `RHStrategyPawn.h/.cpp` (R2c); `RHColonyVisualizerSubsystem.h/.cpp` (R1 helpers and mirror); `RHAgentVisualizerSubsystem.h/.cpp` (R1, R6); `RHCrewVisualizerSubsystem.cpp`, `RHAtmosphereSubsystem.cpp`, `RHAudioSubsystem.cpp`, `RHTrafficVisualizerSubsystem.cpp`, `RHRoadVisualizerSubsystem.cpp`, `RHDistrictVisualizerSubsystem.cpp`, `RHCavernVisualizerSubsystem.cpp`, `RHTunnelVisualizerSubsystem.cpp`, `RHGrowthVisualizerSubsystem.cpp` (R1 table); `RHCommandDeck.h/.cpp` (the Section button, the slider, Cut here, `GetShownLevel`); `RHFrameStats.h/.cpp`, `RHCheats.cpp` (R8); `RHClickTest.cpp` (R9).
- **Scripts:** `scripts/unreal/rh_capture.sh` (`RH_CAPTURE_EXTRA`).
- **Docs:** `docs/design-decisions.md` and the wording table.
- **Untouched:** every path under `Source/RedHopeSim`.

## Deviations to log in `docs/design-decisions.md` (the list, revised)

1. **The section sits beside the floor view** (former Q-A, dropped: replacing the floor view would rework every D1-D7 click path, against the plan row's "presentation only"). The floor view stays where you build. The section is for reading, and a click dives back in.
2. The section draws people as pips, not figures, below ground (unless Q-B (b)).
3. Lift cars in the section move by a presentation phase, rate-clamped in real time; no car is simulated (as D4 and D6d).
4. Tunnel robot counts are sampled from the agent visualizer's drawn positions (Mass fragments), not from a sim accessor.
5. The floor view's cutaway is unchanged; the cavern and atrium cutaway lives in the section.
6. While the section is on, every surface layer is hidden: terrain, MarsGround, buildings, the ship, the pad, markers, roads, rovers, shuttles and crew. The section draws its own surface strip.
7. The section is unlit (one unlit tint material); depth reads from tint and face shading, not lights or shadows.
8. Floor-flow arrows are attributed to a lift only for walked trips (D6c's `ShaftId`); all others are placed beside the lifts.

## Director questions (revised)

Q-A is dropped and logged as deviation 1. Each question below is settled by one letter. The spec above builds under either answer.

**Q-B. How are people shown below ground in the section?**
- **a (recommended): as pips.** A small dot per resident in each cell of the focus floors, and arrows between floors for commuters. It keeps the frame budget at 3,000 people.
- b: as figures on the focus floor only (the floor view's crowd, at the floor's depth, clipped to the cut), with pips elsewhere. It is closer to the floor view's look, and adds the crowd's cost (C3: about 10 ms at 3,000) to the section.

**Q-C. When does D8 build?**
- **a (recommended): now, on HEAD plus D6c** (after D6c commits). That covers floors, shafts, cars, tunnels, caverns, districts, the lift load and the floor flows. The tram band ships inside D6d, and the atrium void, its light, Cut here on its card and its capture ship inside D7. The served-capacity bar ships inside D6e. D6d and D7 each add the accessors listed under "Required by D8".
- b: wait until D6d, D6e and D7 are all built, and build D8 whole, as drafted.

**Q-D. How should the section read in depth?**
- **a (recommended): a thin ant farm.** Every floor is drawn three rows deep (the cut face and two rows behind it) at a low 8-degree pitch, so all twenty floors read alike in one look.
- b: a stepped cut. The floors above the focus are trimmed to the cut face, so the focus floor reads in full depth (to 160 m) at 30 degrees, and the other floors show as cut-face strips.
- Both cost about the same in frame time.

**Status.** Every required change R1-R9 is written in above, and O1-O5 are adopted. Per the review's verdict, a check that each item is written in suffices; no new full review round is needed.


# DIRECTOR ANSWER (2026-10-23)

- **Q-B (a):** people below ground are shown as pips (a dot per resident on the focus floors), with commuter arrows between floors.
- **Q-C (a):** D8 builds now, on HEAD plus D6c, once D6c commits. The tram band ships inside D6d, the atrium visuals inside D7 and the served-capacity bar inside D6e; D6d and D7 add the "Required by D8" accessors listed in this doc.
- **Q-D (a):** the thin ant farm: every floor drawn 3 rows deep at 8 degrees, so all twenty floors read alike in one look.
- Q-A was dropped; "beside the floor view" is logged as a deviation.
