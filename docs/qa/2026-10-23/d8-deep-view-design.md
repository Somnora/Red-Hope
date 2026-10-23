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
