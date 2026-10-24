# UI polish pass: design

2026-10-24, against HEAD `b5f5863`. Design only; no code is written. New wording is Gate-D placeholder copy for `docs/qa/2026-10-15/gate-d-wording.md`.

**Sources.** Every "Known gaps" and "Findings" paragraph in `docs/design-decisions.md`, the bodies of `git log -40`, the handplay card's known list and look questions (`docs/qa/2026-10-21/living-city-handplay-card.md:209-214, 300-307`), and the kept captures `Saved/RHCapture/d8/keep` and `d6d/keep`, which were looked at. Items 1-7 are the known list; 8-11 were found in the logs.

## Rules for every fix

- Only presentation files change: `Source/RedHope/**`, except `RHPlaytest.cpp`. No `Source/RedHopeSim` file, config row, save field or asset changes. Art is job Q.
- Battery (`bash scripts/sim/rh_battery.sh 10`): line-identical to the last gate's baseline, version lines included, since nothing is bumped. The RedHopeSim dylib's hash is unchanged.
- The 110-sol playtest is byte-identical (`cmp`) to the last gate's file.
- Click test: `RH_CLICKTEST_RES=1440x860 bash scripts/unreal/rh_clicktest.sh` passes three clean runs and two loaded runs (P1). The REACH sweep stays full.
- Captures: `scripts/unreal/rh_capture.sh` at 1440x860 (also 1600x900 where the right column matters), before and after, kept in `Saved/RHCapture/uipolish/keep` and looked at.

## Build P: one build, in this order

### P1. Item 7: the click-test flake (test harness only)
- **Where.**
  - `RHClickTest.cpp:448-494`: `FRunner` waits by wall clock only (`NotBefore = now + GapAfter`; a step's last try is `Elapsed >= Timeout`).
  - `PushPress` (`:680-701`) sends move, down and up 0.15 s and 0.12 s apart.
  - The checks for state changes allow 1 s (`title.NEW COLONY` `:1019-1027`, `manifest.ADD` `:1198-1203`).
  - Failures seen: those two (D5, D6a); `section.on` and a build placement in E1's five runs (`design-decisions.md:1301`). Each was "a click that never arrived", and later steps then cascaded.
- **Fix.**
  - A step also counts rendered frames (`GFrameCounter`). A gap ends no sooner than 2 frames; a timeout no sooner than 30.
  - Checks for state changes get 3 s, not 1. They poll, so a passing run is no slower.
  - A failed check logs how many frames it waited and its longest frame, so a flake can be told apart from a defect.
- **Verify.** Five runs, all green. Two of them run with eight `yes > /dev/null` processes, which the builder starts and stops itself.

### P2. Item 1: the shaft strip's white buttons
- **Where.**
  - These are buttons, not text fields: `Next shaft` (`RHCommandDeck.cpp:381-404`, style at `:393`) and `Fit a lift car (N kg Struct)` (`:458-489`, style at `:477`).
  - Both use `RHDeck::TintButtonStyle()`. Its brush is white until `ButtonColorAndOpacity` tints it (`RHDeckStyle.cpp:124-133`; the contract is at `RHDeckStyle.h:56-58`). Of the 24 `TintButtonStyle` users, these are the only two that set no tint.
  - Their text is `TextDim` / `Text` (pale) on white.
  - The refusal reason (`:2289-2302`) wraps to three lines inside the 118 px button (`d6d_keep_02.png`, `d8_1_entry_three_shafts.png`).
- **Fix.**
  - Both buttons take `RHDeck::ButtonStyle()` (dark, `RHDeckStyle.cpp:96-101`) with `RHDeck::Text`.
  - The fit button shows only the verb. The reason moves to a `TextDim` line under the button and to its tooltip.
  - The real text fields (roster search `RHCommandDeckPanels.cpp:990`, district name `:1302`) use the engine's default box, and no capture shows them. The same capture checks them; they get a dark deck style only if they show the same fault.
- **Verify.**
  - Capture: the D6d bench floor view (two shafts, one shaft full), cropped to the deck.
  - Click test: a new `deck.contrast` step walks the visible deck buttons. It fails any button whose fill (brush × tint) and text differ by less than 0.4 in linear luminance (0.2126 R + 0.7152 G + 0.0722 B). At HEAD it fails on exactly these two (Text 0.09, TextDim 0.38), which serves as the mutation. It logs every pair; any other button under the bar is reported, not exempted.

### P3. Item 5: no room for the City panel in a tall city (look question L7)
- **Where.**
  - The right column is a 340 px scroll box with a 4 px bar (`RHCommandDeck.cpp:840-899`, bar at `:845`).
  - City is its sixth panel (`:870-873`). Above it are Guide, Comms, Objective (16 manifest rows while a ship waits, `d6d_keep_02.png`), Politics, and Crew, which is open by default (`RHCommandDeck.h:330`).
  - Today's workaround is `RH.DeckScroll 600` (`living-city-handplay-card.md:214`).
- **Fix: L7's "scroll" option, the smallest.**
  - The foldable panels (Politics, Crew, City, Fleet, Power) become an accordion: opening one folds the others, the rule `Debug_SetCityExpanded` already applies (`RHCommandDeck.h:135`).
  - The column then scrolls the opened panel's header to its top (`SScrollBox::ScrollDescendantIntoView`).
  - The scroll bar widens to 8 px.
  - The fold state stays in the widget, as today; nothing is saved.
  - If the director picks a tab or a window instead, P3 waits for that and the rest of P ships without it.
- **Verify.**
  - Capture: the right column of a tall city (the D6d bench, about 2,900 people), with City folded and then opened.
  - Click test: the City toggle gets the tag `RH.CityToggle`. A new `city.open` step checks that City is open, Crew is folded, and the City header sits inside the column's visible rectangle.

### P4. Item 10: the below-ground legend runs over the SHAFT strip (D6d)
- **Where.**
  - The TRAFFIC legend is top-left, under the vitals and the uplink queue (`RHCommandDeck.cpp:757-764`, 300 px wide).
  - The SHAFT strip floats at the left edge's vertical centre (`:1008-1016`).
  - In the section, the legend's prose is the section's legend text (`RHCommandDeckPanels.cpp:1786-1793`, built at `RHSectionVisualizerSubsystem.cpp:1321-1323`). It grows by a sentence for each tram line.
  - At 1600x900 the legend already reaches the strip's top (`d8_5`); D6d's tram sentences push it over (`design-decisions.md:1266`).
- **Fix.**
  - The strip's slot gets a padding lambda, following the build drawer's pattern (`:1019-1022`). The strip stays centred unless the top-left stack's desired height would reach it; then it sits 8 px below that stack.
  - The strip's floor list drops from 11 rows to the rows that fit above the command bar. It already scrolls.
  - In the section, the legend's prose shortens to one sentence, with the full text in its tooltip. It names only tram lines not drawn in the slab; drawn lines carry their own label (`RHSectionVisualizerSubsystem.cpp:1312`).
- **Verify.**
  - Captures: the click test's colony with a tram laid, in the section with TRAFFIC on, at 1440x860. Take one with orders in flight, so the uplink queue shows.
  - Click test: `section.traffic` gains a `layout.legend` check: the legend's and the strip's arranged rectangles do not intersect. The REACH sweep still reaches every strip row.

### P5. Item 4 (and a D6b line): no hint that the first shape changes a floor's commutes
- **Where.**
  - The shape drag tip is at `RHPlayerController.cpp:695-703` and the order confirm at `:726`.
  - D6c's switch is the first district shape on a floor (`46854bf`); its finding proposes this hint.
  - D6b's gap: nothing says a shape breathes its floor's air (`design-decisions.md:1133`).
- **Fix.**
  - When the rectangle covers a floor with no shape yet, the tip adds: "First shape on floor %d: its commutes will now walk through the digs and tunnels." That is a const read: no deep point of `GetDistricts()` lies on the floor.
  - The confirm adds "(floor %d's commutes now walk its digs)".
  - Every shape tip adds: "A shape breathes its floor's air."
- **Verify.**
  - Capture: the tip during a first drag and a second drag on the same floor.
  - Click test: the `shape.*` steps (`RHClickTest.cpp:1319-1327`) read `GetHintText()`. The hint holds "First shape" on the first drag and not on `shape.again`.

### P6. Item 3 (and the labels that share its rule): flat labels fixed to the default yaw
- **Where.**
  - `RHCavernVisualizerSubsystem.cpp:86-89` lays the label flat, with the tops of its letters toward world +X whatever the camera's yaw. `:240` puts it 6.5 m past the box's +Y edge, which is screen-right at the default yaw and so toward the right column (`design-decisions.md:1048`).
  - The same rotation is in the tunnel (`RHTunnelVisualizerSubsystem.cpp:77-80, 160`), district (`RHDistrictVisualizerSubsystem.cpp:72-75`) and growth (`RHGrowthVisualizerSubsystem.cpp:88-91`) labels.
- **Fix.**
  - One shared helper turns these labels to the camera's yaw, snapped to a quarter turn.
  - Each layer's existing `Tick` re-rotates its labels only when the quarter changes, with no rebuild and no new component.
  - The cavern label moves to the box edge nearest the camera for that quarter (the screen's bottom edge).
- **Verify.**
  - Captures: a carved cavern with `RH.Cam <x> <y> 0.45 0 <yaw>` at yaw 0, 90, 180 and 270.
  - Click test: after the `cavern.*` steps, a new `labels.yaw` step turns the camera 90 degrees (`RH.Cam`). It checks that each cavern and tunnel label's up is within 1 degree of the camera's snapped forward.

### P7. Item 11: a shape's name can sit on rock (D6b)
- **Where.** `RHDistrictVisualizerSubsystem.cpp:355-362` picks the shape point nearest the middle. Underground, those points are the shape's painted cells, dug or not (`:318-320`; `design-decisions.md:1133`).
- **Fix.** Prefer the nearest point that is dug (`Sim.FindFloorCell(Level, P) != INDEX_NONE`, a const accessor the shape ghost already uses at `RHPlayerController.cpp:687`). If none is dug, use today's point.
- **Verify.** Capture: a half-dug shape, as in the D6b shapes setup (`Saved/RHCapture/d6b/d6b_shapes_*`), before and after.

### P8. Items 2, 8 and 9: the section's labels (D8)
- **Where.**
  - `FrameSection` (`RHPlayerController.cpp:2888-2905`) centres the slab on the screen and leaves a fixed 6 cells for labels. It ignores the deck: the SHAFT strip covers the left edge and the 340 px column the right.
  - `PlaceLabels` (`RHSectionVisualizerSubsystem.cpp:1077-1112`) right-aligns 300 cm text (`:37`) in a 400 cm floor pitch at the slab's left edge. TRAFFIC's "out · in" makes the labels longer still.
  - The rock card (`:994-999`) reaches only 10 m past the slab, so a long label crosses its edge (item 8, `design-decisions.md:1236`).
  - In `d8_1` and `d8_5`, floors -1 to -5 are hidden under the strip, and shaft 2's label runs under the right column (item 9).
- **Fix.**
  - (a) Fit into the free band: the window's width less the strip's right edge and the right column, read through two const getters on the deck. Shift the focus along the plane so the slab is centred in that band.
  - (b) The room on each side is the widest label reaching past that edge, not 6 cells.
  - (c) Label size becomes min(300 cm, 0.55 × floor height).
  - (d) The rock card extends under the whole label column.
  - No text changes.
- **Verify.**
  - Captures: retake D8's kept frames (entry with three shafts, focus -10, TRAFFIC) at 1440x860 and 1600x900.
  - Click test: a new `section.labels` step runs after `section.on` and after `section.traffic`. It projects each floor and shaft label's bounds to the screen through a new const getter on the section's label pools. It fails on any overlap, any label outside the free band, or any glyph under 9 px tall.

## Job Q (optional, needs new art): item 6, and the other placeholder bodies

- **Where.**
  - Shuttles and cars are instances of `/Engine/BasicShapes/Cube` (`RHTrafficVisualizerSubsystem.cpp:179-186`), at most 400 (`design-decisions.md:598`).
  - The same job could also take the lift car's rider and queue plate (D4, `design-decisions.md:937`) and the rover's still wheels (`living-city-handplay-card.md:209`).
- **Fix.**
  - First the director answers L3 ("what should a commuter shuttle be?") and L4 (the wheels).
  - Then the meshes are made and imported, the one `SetStaticMesh` call and the scale are swapped, and the instance transforms stay as they are.
  - New assets mean a separate commit, gated on its own.
- **Verify.** Capture: the staged city with commuting on (`RH.Traffic home`), checking frame time at the 400 cap. The click test is unchanged.

## Found in the logs, not in this pass

- **Later shafts' cab doors don't slide** (D2, `94d0afa`; `RHColonyVisualizerSubsystem.cpp:3143-3207` drives shaft 0's leaves only). Crew still board only at shaft 0, and their approach is what opens the doors, so the fix would show nothing until crew board at other shafts. Fix both together.
- **The warm-floor tint is subtle, and the ❄ and ≈ glyphs are placeholders** (D5, `design-decisions.md:1012`). This is a look decision.
- **No "remove manifest line"** (E2, risk 6). It needs a new sim command, so it is not presentation-only.
- **The cavern ghost's O2 figure is the whole box** (D6a, `:1048`). Fixing it needs a sim readout of what the filter carries.
- **No "Unzoned" figure for an overfull shaped floor** (D6b, `:1133`). It is a new count; first check whether existing accessors give it.
- **Accepted, no fix:** the TUNNELS card lists only the viewed floor (D3, a design choice); the RMB/MMB lock checks cannot prove that a real drag is refused (D8, a limit of synthetic input); the rover can overlap a building when it pivots (L4).

## Risks

1. The accordion (P3) changes a habit: opening City folds Crew. It is the director's call under L7; P3 is the one item that can be dropped.
2. Framing into the free band (P8) zooms out a little. The 9 px glyph floor in `section.labels` guards legibility.
3. The frame-count waits (P1) slow the click test only on a slow machine. Recording the run time keeps that visible.
4. Every change is in the RedHope module, so the battery cannot see it. The playtest bot (`RHPlaytest.cpp`) is not touched. The dylib hash and `cmp` are the proof that no sim behaviour moved.


# DIRECTOR ANSWER (2026-10-24)

- **L7 (a):** the accordion. Opening one foldable deck panel folds the others and scrolls it into view; the scroll bar widens from 4 to 8 px. P3 ships.
- The optional art job Q waits for L3 (what a commuter shuttle should be) and L4 (wheels).
