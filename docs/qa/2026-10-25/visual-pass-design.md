# Visual pass: the tram, construction sites, the deep view's floor names (DESIGN, 2026-10-25)

**Status:** design only. Nothing built, no art made. HEAD `e639181`; no Source or script file has changed since `31ffa6f`, so the anchors below hold.
**Answers:** the director's hand-play notes of 2026-10-25:
1. "The tram looks way unfinished design-wise."
2. "The solar array 'in progress' state looks unfinished."
3. "The deep view's floor names are hard to read and too big."

**Scope:** `Source/RedHope` only, plus new Blender kit scripts and the assets they make. No `RedHopeSim` file, no CSV or DataTable row, no save field. Each item is its own commit.

## How art is made here (the lane every new mesh follows)

- **Built, not generated:** `scripts/blender/rh_build_<name>.py` on `rh_hs.Kit` (`scripts/blender/rh_hs.py`), exact bevelled primitives in the art-bible roles (`hull` bone-white, `slate`, `metal` light alloy at mid metal 0.55, `pipe`, `glass`, `rubber`, `hazard`). **One function accent, as trim only** (5-15 %), painted into BaseColor (`M_RH_Master` has no accent mask, so the import forces AccentAmount 0). **One glow hue an asset**, lit by construction: the lit parts are the emissive mask.
- **True scale in metres**, ground-centred pivot, dimensions asserted by the script (`rh_build_rover.py`: 5.0 x 2.6 x 2.4 m). A vehicle is built front at +X with no `face_camera()`; the game turns it to its heading.
- **Bake and import:** `K.finish` and `K.bake_and_export` give one mesh, one material and `_BaseColor`, `_MetallicRoughness`, `_EmissiveMask` PNGs. `scripts/unreal/rh_import_hs.py` (`RH_NAME`, `RH_DIR`, `RH_GLOW`, `RH_GLOW_AMT`, `RH_TEX_TAG`) chains `rh_import_model2.py` and `rh_import_masks.py`, sets Nanite off and checks textures by name. `rh_mesh_stats.py` in a fresh process confirms it. The visualizer then loads the path with a logged fallback (the `ModelPathsV2` pattern).
- **Traps on record:** ISM materials need `bUsedWithInstancedStaticMeshes` (`dff3b1a`); a `-replaceexisting` re-import resets the slot, so wire after import; asset names are case-insensitive.

## 1. The tram (D6d)

**What is drawn today.** `URHTramVisualizerSubsystem` (`RHTramVisualizerSubsystem.cpp`):
- `BuildCapsule` (`:19`) makes a procedural pill, 15 m long and 3.6 m across (`:12-14`).
- `MakeCar` (`:121`) gives it `M_DomeGlass` at opacity 0.92 and glow 1.6.
- `Tick` (`:158`) slides it along the tunnel chain by `ArcAt`, and tints the whole body teal, amber or red by the line's wait.

Why it reads unfinished: it is a glowing translucent marker, not a vehicle. Its centre is 1.4 m up with a 1.8 m radius, so it sinks 40 cm through the floor. At a stop its centre is on the end cell's centre, so it overhangs the tunnel end by 2.5 m into rock. It snaps 90 degrees at an L corner, and its yaw flips 180 degrees on the way back.

**Target look.** A short, double-ended, two-car articulated people-mover, the Mars-tunnel cousin of the elevator cab (`rh_build_elevator.py`):
- **Body:** bone-white over a dark slate underframe and skirts; a continuous dark glass window band each side, lit warm from inside; paired doors in slate frames; low light-alloy roof pods (air, batteries); dark bogies under each car end; a rubber concertina joint between the cars.
- **Ends:** a rounded cab at each end (double-ended, because it shuttles, so the flip at each stop stops showing), with a raked windscreen, two headlamps and a destination board.
- **Accent:** the shaft and elevator amber (#F3C459), so lifts and trams read as one transit family; trim only (a belt stripe under the windows, door edges, bumpers). **Glow:** one hue, warm white (windows, headlamps, boards).

**Dimensions** come from the tunnel cell: 10 m lattice cells (`CellCm`, `GetTunnelChainPoint` x 1000 cm), floors 4 m apart (`FloorHeightMeters`), crew up to 1.88 m tall.
- Two cars, each 5.6 m over the body, plus a 0.6 m joint and rounded noses: **about 12.0 m overall**. Wide enough to read as a train at the overview zoom; short enough to fit the stop (below).
- **2.6 m wide** (the rover's width). That leaves about 3.7 m of walkway each side of the 10 m cell.
- **3.0 m to the roof, 3.2 m over the pods.** That is under the next floor, with the 60 cm plate (`PlateThickCm`).
- Floor 0.35 m up. Doors 1.22 x 2.24 m clear: the elevator's doorway contract, 1.88 m crew plus a foot all round.
- Script asserts: 11.6-12.4 x 2.5-2.7 x 2.9-3.3 m.

**Build plan.**
- **Script.** `scripts/blender/rh_build_tram.py`, two assets from one script, as the elevator does:
  - `RH_TramCar`: one car, its cab at +X and the joint face at -X. The game turns the second car 180 degrees, so one mesh serves both. About 9k tris (the rover's budget).
  - `RH_TramJoint`: the concertina, about 600 tris.
- **Import.** `rh_import_hs.py` with `RH_GLOW` warm white, into `Models2/TramCar` and `Models2/TramJoint`.
- **LODs.** UE's built-in reduction at import: LOD1 at 35 % and LOD2 at 10 %, at screen sizes 0.25 and 0.08. This is the first kit asset with LODs, so the import step sets them and `rh_mesh_stats.py` reports them.
- **Visualizer.** `MakeCar` spawns two car components and one joint component per line, in place of the procedural capsule. `Tick` changes three ways:
  - **(a) The stop fix.** The train's centre runs `HalfLen + ArcAt(...) x (ArcCm - 2 x HalfLen) / ArcCm`, so the noses stop inside the end cells. `ArcAt` itself is unchanged, so `tram.unit` still pins it. `TramMinCells` 12 gives an arc of at least 110 m, so the remap is always valid.
  - **(b) Articulation.** Each car takes its position and yaw from its two bogie points on the chain polyline (about 4 m apart), so cars bend through an L corner instead of snapping. The joint sits at the shared point with the mean yaw.
  - **(c) Height.** The train stands on the floor plane, with no lift.
- **The wait colour** moves from the whole body to the destination boards, through a small MID on a separate board mesh (see question B).
- **Fallback.** A missing asset logs one warning and falls back to today's capsule.
- **Accessors.** `GetCapsuleCount`, `GetShownCapsules` and `GetCapsuleAt` keep their meaning (one train per line; `At` is the train's centre), so `tram.capsule` and `tram.moves` read it unchanged.
- **Cost.** At most 8 lines (`MaxTramLines`), so at most 24 plain static mesh components (no ISM, so the ISM usage trap cannot bite). Per frame there are 3 transforms a shown line. No lights: emissive only, shadowless.

**Verify.**
- **Captures,** before and after at 1440x860, kept in `Saved/RHCapture/tram/`:
  - the click test's own line (`tram.setup`: a finished 14-point tunnel on -2);
  - `RH.DeepBench 2000 trams` on floor -10, framed on a corner, four frames through the turn and one at each stop.
- **Click test** at `RH_CLICKTEST_RES=1440x860`: `tram.unit`, `tram.capsule`, `tram.moves` and `section.tram` pass unchanged. A new `tram.fit` checks:
  - at both stops, both noses are inside the chain's end cells;
  - on a corner frame, the two cars' yaws differ;
  - the train's lowest point is at or above the floor plane.
- **Frame budget:** `RH.FrameStats 15` on the DeepBench floor view stays within 1.0 ms of the same run before (D8's table, run E).

## 2. Construction sites (all buildings, not only the Solar Array)

**What is drawn today.** One branch draws every site, surface or underground:
- `HandleBuildingAdded` (`RHColonyVisualizerSubsystem.cpp:2294`; the site branch is at `:2547-2566`) draws:
  - an engine `Cube` scaled to the footprint and capped at 0.4 m tall (`ScaleFor`, `:2229`; the Solar Array is 0.25 m), tinted Body x 0.3 on `M_Graybox`;
  - four hazard-yellow cube posts, 1.7 m tall;
  - a slate cube mast with an emissive sphere for a lamp.
- `HandleBuildingCompleted` (`:2659`) swaps in the real model.
- No progress shows in the world. Only the inspector's "UNDER CONSTRUCTION - N s fabrication left" (`RHCommandDeck.cpp:1636`) gives it. Waiting for materials and being fabricated look identical.
- For the Solar Array (a 4 x 6 m flat-pack: `ImportOnly`, 60 s, no bill), the site is a dark slab with sticks.

**Recommendation: fix it generally.**
- One site kit serves every building, because the Solar Array is only the first site the player meets. It is the tutorial's first step, and the click test's `build.place`.
- A Solar-only fix would leave the same slab on every other building.

**Target look.** A site that tells the player what stage it is at:
- a **foundation pad**: slate deck tiles with hazard-chevron edges and anchor bolts;
- a **light-alloy scaffold** around the footprint, one 2 m bay a footprint unit along each side;
- a **site mast** with an amber work lamp, which replaces the cube and sphere and stays lit at night as today;
- **crates** on the pad, in the Supply Ship's container language (bone-white and cargo amber, slate frames).

How the stages show:
- **Waiting for materials:** the pad, one scaffold course, and crates arriving in proportion to what has been delivered against the bill.
- **Fabrication:** the crates go and the **building's own model rises inside the scaffold**, revealed bottom-up to the build fraction with a thin teal line at the cut (question A). The scaffold is as tall as the model; underground it is capped at one bay.

Pressurized shells and greenhouse decks (drawn by `RefreshHabShells` and `BuildGreenhouseDome`) and buildings with no model (the Floodmast) get the pad, scaffold and crates, but no reveal.

**Build plan.**
- **Script.** `scripts/blender/rh_build_sitekit.py`, four small assets: `RH_SitePad` (a 2 x 2 m tile, about 400 tris), `RH_SiteScaffold` (a 2 x 2 x 2 m bay of tubes, braces, a slate plank deck and a hazard toe board, about 1.5k), `RH_SiteMast` (about 1.5k) and `RH_SiteCrate` (1 m, about 800). Accent hazard yellow as trim; glow amber, on the lamp only.
- **Import** through `rh_import_hs.py`. LOD1 at 40 % is enough.
- **Visualizer.** One holder with **one HISM per kit part**, so all sites share 4 draws. Today each site is 7 components. A Solar site is about 6 pads, 10 bays, 1 mast and up to 4 crates.
- **The reveal.** A new material, `M_RH_BuildReveal`:
  - `M_RH_Master`'s texture inputs, plus a world-Z opacity-mask clip (`BuildCutZ`) and an emissive line at the cut;
  - authored in a commandlet as the `URHArtCommandlet` precedent, with ISM and static usage set;
  - put on the site's model through a MID while `bUnderConstruction` is set. Completion already rebuilds the actor with the shipping MI.
- **Progress** is read in presentation from public const accessors only:
  - materials: delivered `InputKg` over `GetSiteBill(B)`;
  - fabrication: `1 - BuildRemaining_s / (BuildTime_s x GetBedCount(B))`, the formula the sim uses to set it.
  - It is updated on the existing one-second pass poll, quantised to 5 % steps, and written only when the step changes.
- **New accessor** for the click test: `GetSiteInstanceCount(Id)`.

**Verify.**
- **Captures** before and after at 1440x860, kept in `Saved/RHCapture/sites/`: the click test's Solar site (`build.place`) at 0 %, about 50 % and done; one underground site; one Habitat site; a night frame.
- **Click test:** a new `build.site` step after `build.place`:
  - the site has pad and scaffold instances;
  - two readings show a non-decreasing fraction;
  - on completion its site instances drop to 0 and the model wears its shipping MI.
- **Frame budget:** `RH.FrameStats 15` on the surface, with the tutorial's 3 Solar sites plus 2 others open, within 1.0 ms of before.

## 3. The deep view's floor names (P8c, replacing the refresh's P8b sizes)

**What is drawn today.** `URHSectionVisualizerSubsystem::PlaceLabels` (`RHSectionVisualizerSubsystem.cpp:1146`) draws:
- world-space `UTextRenderComponent` text, `FloorLabelSizeCm` = min(300 cm, 0.75 x floor height) (`:54`);
- right-aligned at the slab's left edge;
- pale grey (0.80, 0.88, 0.92), SURF dark brown, the focus floor teal, warm floors amber (`:1157-1163`).

Because the text lives in the world, its size follows the zoom:
- in a small colony it is about 30 px (`Saved/RHCapture/uipolish/after/ui_after_S_00.png`: "-1 · 8 cells" towers over the slab), which is the director's "too big";
- on the 20-floor bench it is about 7.5 px (the `86f23a4` note).

It is also soft: filtered world glyphs, smeared by TSR, with no backing, pale on lit rock.

**Target look.** The refresh's ladder (`ui-polish-design.md`, "REFRESH AT HEAD 31ffa6f"), but **smaller**, as the director asks:
- a tidy right-aligned column in the free band's left margin;
- one row per floor, SURF to the deepest, at a fixed 16 px pitch;
- each row a narrow chip of the deck's glass (`RHDeck::PanelBg`), text in **`RHDeck::Label(10)`**: BoldCondensed caps, the deck's most-used label face, cap height about 9 px, not the refresh's 13 px;
- the floor name in `RHDeck::Text`, the count ("· 230 cells", TRAFFIC's "out · in") in `RHDeck::TextDim`;
- focus floor teal and warm floors amber, as today. SURF gets the same chip, so the dark-on-tan special case goes;
- a **1 px leader** from each chip to its floor's mid-plane on the cut face, in `TextDim` at 70 %.

Slate text is pixel-snapped and drawn after TSR, so it is sharp at every zoom and the same size in a 3-floor and a 20-floor colony.

**Build plan.** The refresh's steps 1-2, unchanged, and no art:
- the deck draws the rows (box, text, lines) from a const getter on the section visualizer (text, colour, cut-face anchor), projected with `ProjectWorldLocationToScreen`, recomputed on refit or camera keys, never per still frame;
- `FrameSection` fits the slab into the band minus the ladder, `ComputeLabelRoomCm` drops the floor term, and `PlaceLabels` stops placing floor texts;
- the refresh's step 3 (a screen-size floor for the shaft, lift and tram labels) applies at the same 10-size face.

**Verify.** The refresh's verify step, with two numbers changed:
- the glyph bar **stays at 9 px** (smaller text, by request), not 11;
- the contrast bar is **7:1 for the name** and 4.5:1 for the dim count against the chip (WCAG, computed in the suite as `deck.contrast` does).

Run:
- `section.labels` at `RH_CLICKTEST_RES=1440x860` and 1600x900, in the small colony and in the new `RH_CLICKTEST_SCENE=deepbench`;
- all rows present, no overlaps, each leader within 2 px of its floor, the slab inside the band;
- captures in `Saved/RHCapture/p8c/`, before and after, small colony and bench, TRAFFIC off and on;
- the section slot at most 1.0 ms a frame on average (D8's budget).

## Inertness (all three items)

Presentation only. No `RedHopeSim` file, row, save field or command. Proof per commit:
- the RedHopeSim dylib hash is unchanged;
- the battery is line-identical to the baseline current at build time (the newest battery directory today is `Saved/baselines/battery_post_e4`);
- `cmp` of the 110-sol playtest against `Saved/baselines/ind_full_playtest.txt` is byte-identical.

The playtest bot (`RHPlaytest.cpp`) is not touched.

**Order:** 3 (no art, one session), then 1, then 2 (it needs a new material). Each is its own gated commit; the kit assets go in the same commit as their visualizer change.

## Other placeholder visuals noticed (optional follow-ups, not in scope)

- **Commuter shuttles and cars** are `/Engine/BasicShapes/Cube` instances on `M_Graybox` (`RHTrafficVisualizerSubsystem.cpp:179-186`). They wait on L3.
- **The trade convoys (E4)** are composed engine cubes, cylinders and spheres (`RHColonyVisualizerSubsystem.cpp`, the convoy loop after `:4268`), although the kit `Models2/Rover` exists. A one-path swap.
- **The cargo rover does have wheels** (the C6b kit, six of them). Its known placeholders are that they don't spin (L4) and its load lump is an engine cube (`RHAgentVisualizerSubsystem.cpp:826-829`).
- **The Floodmast** has no model in either path map and draws composed primitives. LabModule and InfirmaryModule have no entry either (check whether their Tiers furniture covers them).
- **A finished tunnel** is only a centre stripe on the floor (`RHTunnelVisualizerSubsystem`). A guideway and wall lining would seat the new tram.
- **Later shafts' cab doors** don't slide; the lift car has no rider or queue plate (D4).

## Director look questions

**A. How should a building under construction show its progress?**
- **(a) Recommended:** pad, scaffold and crates, with the building's own model rising inside the scaffold. Progress reads at a glance, and every building gets it from its own model.
- **(b):** pad, scaffold and crates only; the model appears when it is finished. No new material, but no progress in the world.

**B. Where should a tram show its wait colour (teal, amber, red)?**
- **(a) Recommended:** on the destination boards only, with the body in its livery and the windows warm. Matches the lifts' bars.
- **(b):** nowhere on the floor view. The TRAMS card and the section's band already carry it.
