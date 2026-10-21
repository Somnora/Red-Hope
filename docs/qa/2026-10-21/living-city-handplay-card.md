# Hand-play card: step 1, the living city (C1-C6), 2026-10-21

One session to review gates C1-C6 by hand: about 60-75 minutes, then 15 minutes of free play.
Every command below is taken from the code committed at `6ca943c` (`RHCheats.cpp`, `rh_sandbox.sh`, `rh_checkpoints.sh`, `rh_playtest.sh`).
The two long boot lines in the appendix are the C3 and C6b capture boots, with the screenshot steps taken out.

**Play this after D1 is committed and compiled.**
- D1 bumps the save version. The first boot of `midgame` or `late` therefore re-makes its checkpoint (2-5 minutes; the terminal says so).
- The City checkpoint below is not re-made automatically. Make it again after D1.
- Don't boot anything while the editor target is compiling.

## How to start

Run everything from the repo root: `cd /Volumes/Unreal/red_hope/red_hope`

```bash
bash scripts/unreal/rh_sandbox.sh opening   # title menu, NEW COLONY, sol 0
bash scripts/unreal/rh_sandbox.sh midgame   # bot game, sol 28.35, 4 crew, no districts or roads
bash scripts/unreal/rh_sandbox.sh late      # bot game, sol 62.35, 6 people, no districts or roads
bash scripts/unreal/rh_checkpoints.sh all --force   # re-make both by hand
```

**The City checkpoint, made once (about 10 minutes).** It is the bot's living-city game, saved at sol 80.35. In the 10-21 run it had:
- about 110 people (the Big Arrival landed at sol 75);
- Homes on floor -1 and Civic on floor -2;
- 10 road edges (223 m);
- one cargo rover.

D1 may shift these numbers.

```bash
RH_PLAYTEST_DISTRICTS=1 RH_PLAYTEST_SAVE=City bash scripts/sim/rh_playtest.sh 80.35 checkpoint_City guide full
grep "SAVED slot" Saved/RHPlaytest/checkpoint_City.txt      # confirms Saved/SaveGames/RH_City.sav
```

Boot it with this line (the same as the `late` preset, but it loads City):

```bash
UE=/Volumes/Unreal/UE_5.8/Engine/Binaries/Mac/UnrealEditor; P=/Volumes/Unreal/red_hope/red_hope/red_hope.uproject
"$UE" "$P" /Game/RedHope/Maps/L_Slice -game -windowed -resx=1600 -resy=900 -nosound -RHNoTitle "-ExecCmds=r.setres 1600x900w, RH.After 2 RH.Load City, RH.After 2.5 RH.SetSpeed 1, RH.After 3 RH.Floor 0, RH.After 3.5 RH.Cam 35 35 0.5"
```

**In game:**
- The backquote key opens the console. Type one command per line.
- Load a save with `RH.Load Midgame`, `RH.Load Late` or `RH.Load City`. Save your own with `RH.Save Mine`, then `RH.Load Mine`.
- Speed: Space pauses. Keys 1, 2, 3 and 4 give 1x, 3x, 8x and 60x.
- Orders wait for the uplink lag (8-45 s). `RH.Lag 0` skips the lag for the session.
- Log output: open the console, or `grep "RH\.\|FrameStats" ~/Library/Logs/red_hope/red_hope.log`.

---

## C1. Foundations: the sim holds 5,000 people (pure speed work)

**Setup:** the C3 boot (appendix A), with `RH.CityBench 3000` changed to `5000`. Then `RH.Floor 0`, `RH.SetSpeed 8`.

- [ ] The sol clock still moves at 8x with 5,000 people, with no stall at a sol boundary: yes/no
- [ ] `late` plays as it did before C1 (same pace, same events): yes/no

**Known, not bugs:** nothing is meant to be visible.

**Verdict:** OK / change / discuss: ____

## C2. Population at scale: Hope per head, shared rations, arrivals

**Setup:** City checkpoint. Then:
```
RH.Hope
RH.Crew
```
To test a shortage, take the Food figure from the first `RH.Crew` line and type `RH.AddStock Food -<that figure>`.
Watch for 1-2 minutes at 1x, then put the food back with `RH.AddStock Food <that figure>`.

- [ ] `RH.Hope` reads sensibly at about 110 people: jobs and rooms count as shares, and Hope is not pinned at 100: yes/no
- [ ] The shortage raises one alert per home, not one per person, and one alert on recovery: yes/no
- [ ] The roster's Short rations filter lists everyone short; leavers trickle out newest first, Mars-born last: yes/no
- [ ] The City panel's Rations note says why the next ship's settlers are held, or how many will come: yes/no
- [ ] The Big Arrival's ~100 newcomers settled in (floor -1 is full of people, not a pile at one spot): yes/no

**Known, not bugs:**
- All morale and recall wording is placeholder. The approved Gate-D wording lands after D1, as do the F1b-F9a mechanics. So today's choice of who leaves first is the pre-ruling one.
- The CEO's landing belongs to the politics step.
- The numbers are placeholders.

**Verdict:** OK / change / discuss: ____

## C3. Crowds on screen

**Setup:** the C3 boot (appendix A): 3,000 people on floor -1. Then:
```
RH.FrameStats 15 tsr
r.AntiAliasingMethod 2
RH.FrameStats 15 taa
r.AntiAliasingMethod 4
```
`2` is TAA. `4` is TSR, the engine default the game uses today.

- [ ] Near the camera, about 100 people are full animated figures; the rest read as people walking a routine: yes/no
- [ ] Zooming and panning, the full-figure/crowd swap doesn't pop distractingly: yes/no
- [ ] The crew roster scrolls 3,000 names smoothly, and its name/#id search and filters work: yes/no
- [ ] Floor -1 stays playable; the C6b reference median is 42 ms: yes/no
- [ ] **Look call (L1):** TAA's image is acceptable for its ~14 ms saving: yes/no

**Known, not bugs:**
- Crowd figures use the older static colonist meshes, so there is a visible change at the boundary.
- Crowd figures don't emote or use elevator doors.
- Floors you are not viewing are not drawn.
- People without a bed cluster at the first Living cell's seat ring.
- The roster's "First crew" filter means Id <= 20 (a placeholder).
- The 33 ms target is met on the surface only. Floor -1 is GPU-bound by TSR and the per-cell lamps.

**Verdict:** OK / change / discuss: ____

## C4. Roads you draw; robots drive them 1.6x faster

**Setup:** City checkpoint (it already has 10 edges). Then:
```
RH.Road
RH.CamRobot
RH.Fleet
```
`RH.Road` with no arguments lists the network. `RH.Road x1 y1 x2 y2` prints the preflight verdict before it transmits the order.

To lay a road by hand: BUILD > Logistics > **Road**, then drag.

- [ ] The drag ghost snaps to junctions and shows length, bill, stores, lay time and the refusal reason: yes/no
- [ ] The Struct leaves the stores when the order lands, a Fabricator lays the road, and the dashed ghost becomes dark regolith: yes/no
- [ ] A road can't cross a building, and a building can't be placed on a road: yes/no
- [ ] Robots on road trips follow the strip and turn smoothly, with no sideways slide: yes/no
- [ ] Key 4 (60x) is refused while a road is still being laid: yes/no

**Known, not bugs:**
- There are no edge marker posts, and a road under construction has no partial fill (it shows as a ghost).
- Era mode keeps teleport logistics; this is the logged C4 deviation.
- Robots pick work by straight-line distance.
- There is no starter road.
- The 6 m strip must stay clear of footprints, so the Lander's crowded ring leaves few corridors.
- This is the first real mouse test of the drag. Captures only used `RH.RoadGhost`.

**Verdict:** OK / change / discuss: ____

## C5. Districts, growth, the City / Earth share

**Setup:** City checkpoint. Then:
```
RH.District city 1
RH.District list
RH.District paint Industry -2 -2 2 2
RH.Growth
RH.Growth now
RH.Floor -1
```
Lattice cells are 10 m, and cell 0,0 is the Lander. Run at 8x to see a project finish: floor homes take 1 sol, a civic room half a sol.

- [ ] The City panel's chips arm the tool. The surface drag ghost names the district it joins, marks out-of-coverage cells red and refuses overlaps: yes/no
- [ ] The district wash and names read clearly. Hover shows the label, and `RH.District labels 1` shows them all: yes/no
- [ ] Housing, Jobs, Services and Rations: the bars and their plain-words prose match what you see: yes/no
- [ ] The growth scaffold on floor -1 shows "district: what - N%". It finishes as Living cells with bunk beds and people move in: yes/no
- [ ] The CITY <-> EARTH chips (0-100%) change what `RH.Growth` reports as free Struct, at once: yes/no
- [ ] Rename (the field, then Enter) and GROWTH ON/OFF work per district: yes/no

**Known, not bugs:**
- Painting Homes on bare ground grows nothing. The city only extends a rated surface habitat you founded, and the bot founds none, so its growth is all underground.
- At most one project starts a sol, colony-wide. Growth pauses on short rations or when Hope is FAILING, and mostly waits between quota rounds.
- Homes growth runs no adjacency check: new cells next to Gardens cost about 4 Hope.
- A surface network's air push holds about 240 bays.
- Erase removes a whole district, by Id. The DISTRICT tab shows only with the zone row; the chips work anywhere.
- Late water production is 0, because only two ice deposits exist (W1 is in design).

**Verdict:** OK / change / discuss: ____

## C6. Traffic: cargo rovers, commuting, the TRAFFIC overlay, Demolish Road

**Setup A, rovers:** the rover yard (appendix B). It has roads, two Forges, a Stockpile, 5 t of Struct and two rovers, running at 8x. Then:
```
rh.RoverFollowCam 0.045
RH.Rover
RH.Rover drain 0.1
rh.RoverFollowCam 0
```

**Setup B, commuting:** the staged city (appendix A2):
- 3,000 people re-homed into 15 surface habitats;
- 20 districts;
- a ring road with 15 spokes.

Then:
```
RH.Commute
RH.Traffic overlay 1
```

- [ ] Rovers' bodies stay along the road through corners, with no drifting: yes/no
- [ ] A rover facing the wrong way pivots on the spot; crates show on the bed while it is loaded: yes/no
- [ ] After `drain`, a loaded rover puts its load back and drives to a pad on the road: yes/no
- [ ] Shuttles ride the spokes in lanes and slow through corners, and the streets feel busy: yes/no
- [ ] The TRAFFIC button's heat wash and legend read at a glance. Hovering an edge gives "Edge #n: N commuters, M freight rovers": yes/no
- [ ] Demolish Road (BUILD > Logistics): hover outlines the edge and states the 50% salvage; the click sends the order: yes/no
- [ ] The City panel's commuting line and the Hope line "Long trips to work" read right: yes/no

**Known, not bugs:**
- Shuttles are plain boxes. The rover's wheels don't spin.
- A pivoting rover can overlap a building (seen at the Forge and the Lander), and can trail the sim by up to ~1.6 s.
- Commuting is inert below 60 people: no shuttles, and the overlay shows freight only. The `midgame` and `late` presets have 4-6 people.
- The bot builds no long commutes and moves no freight, so the City checkpoint's rover mostly idles. Long trips exist only in the staged city (`RH.Traffic home` is a debug re-homing).
- A hauler still loses up to 200 kg on a forced charge (accepted).
- In a tall city the City panel overflows the right column. `RH.DeckScroll 600` scrolls it.

**Verdict:** OK / change / discuss: ____

---

## D2. More shafts (step 2, going deep)

**Setup:** a fresh colony with the debug demo (shaft 0 at -1, eight cells, four crew), then a second shaft 300 m east. Orders wait for the uplink lag, so set `RH.Lag 0` first.
```
RH.Lag 0
RH.Demo
RH.Spawn Pylon 330 12
RH.Spawn Borer 330 12
RH.SinkNow 330 12 1
RH.ExcavateNow -1 12 1
RH.Floor -1
```
Then press **Next shaft** on the SHAFT strip.
`RH.SinkNow` is the debug path. For the real path, build or `RH.Spawn` a second Borer inside coverage, then select it and press **Sink a shaft here** (or `RH.SinkShaft <BorerId>`), then `RH.Bore 1 1` and `RH.Excavate -1 12 1`. `RH.Status` lists every shaft.

- [ ] The Borer card says **ANCHORS shaft 1** (or **HELPS shaft 0** for a Borer you have not sunk), and its SHAFT lines read that shaft: yes/no
- [ ] **Sink a shaft here** shows the reason on the button while it cannot (shaft 0 not bored, outside coverage, a batch in flight, the column already dug): yes/no
- [ ] The **Next shaft** button on the SHAFT strip walks the camera head to head, and the second elevator cab stands at the second head on floor -1: yes/no
- [ ] Floor -1 shows both digs, 300 m apart, each with its own deck plate under the cab, and the pit walls and skirt cover both: yes/no
- [ ] `RH.Bore 3` (shaft 0) and `RH.Bore 2 1` (shaft 1) go to different shafts; the deck's Bore button acts on the selected Borer's shaft: yes/no
- [ ] The excavate drag on floor -1 near the second head sends the order to shaft 1 (the confirm line says so in the log: `Excavation designation ... for shaft 1`): yes/no

**Known, not bugs (accepted 2026-10-21, see `docs/design-decisions.md`, "D2, more shafts"):**
- A floor is still one place. A dig with no air filter is rated through another dig's filter, and a habitat beside a new shaft's head links to floor -1's air even before that shaft has dug anything.
- Crew still board the elevator at shaft 0 whichever dig they live in; the second cab's doors do not slide. D3 and D4 route crew through the head that serves them.
- An unanchored second Borer helps shaft 0. The card says so.
- The commute between two digs on one floor is 0 m.

**Verdict:** OK / change / discuss: ____

---

## D3. Tunnels (step 2, going deep; option c, tunnels only)

**Setup:** the D2 setup (two digs, 300 m apart). Orders wait for the uplink lag, so set `RH.Lag 0` first.
```
RH.Lag 0
RH.Demo
RH.Spawn Pylon 330 12
RH.SinkNow 330 12 1
RH.ExcavateNow -1 12 1
RH.Floor -1
```
Then press **Tunnel** on the SHAFT strip, click on the rock beside the first dig and drag toward the second, and release. (Console: `RH.DigTunnel -1 2 0 30 0`, lattice cells of 10 m with the first head at (0,0); `RH.Status` lists tunnels.) A Borer carves it: `RH.Spawn Borer 20 12` is a helper that works shaft 0 and carves the end beside the first dig. `RH.SinkNow` is the debug path and leaves shaft 1 with no Borer, so the far end stays stalled until you sink shaft 1 with a Borer (the D2 real path above); then both ends are carved. With no Borer at all, `RH.TunnelNow 1` carves the tunnel at once and `RH.TunnelNow 1 14` carves half.

- [ ] While you drag, a box shows on every cell of the line (X first, then Y), green when the order would be accepted and red when not, and the hint says the cell count, Borer-hours, tons of spoil and the reason: yes/no
- [ ] After the order the line stays as a dashed amber ghost, with a label `Tunnel 1: 29 cells left`, or `stalled - no Borer serves either end` while no Borer works either end: yes/no
- [ ] With a Borer working each end the tunnel is carved from both ends, one cell a Borer every 3 sol-hours, and the dashed part shrinks toward the middle: yes/no
- [ ] The finished tunnel is a dark corridor with a pale centre stripe, and the two digs read as one place: yes/no
- [ ] The TUNNELS card under the floor buttons lists the tunnel (`#1 29 left`, `digging` or `stalled`), and clicking it (**cancel**) stops new cells; the cells already dug stay, and the rest of the line stays as a ghost marked `cancelled`: yes/no
- [ ] Sinking a shaft on an unfinished tunnel's line is refused with the tunnel's number: yes/no
- [ ] Civic growth never puts a room in a tunnel cell (you still can, by hand): yes/no

**Known, not bugs (option c, 2026-10-21, see `docs/design-decisions.md`, "D3, tunnels"):**
- Robots do not go below. Fitted lifts, riding, cross-level claims and charging, and repair across floors are D4's, with lift capacity. Haulers and Fabricators still serve a floor from the shaft head.
- The commute ignores tunnels until floor homes and seats get a cell (D6): a trip between two digs on one floor is still 0 m.
- A tunnel cell costs what any cell costs: air fill, leak and a legacy bed. A long tunnel on a rated floor needs more oxygen.
- Era mode still carves a tunnel (it is Borer work in both bands); it is not refused while one is open.

**Verdict:** OK / change / discuss: ____

---

## The big picture: 15 minutes in the City checkpoint

Does the city feel alive? Play at 3x-8x and use the mouse wherever you can.

1. **0-2 min.** Open CITY and read the four bars and the district list. `RH.Hope`.
2. **2-5 min.** Paint an Industry district over the Forge cluster and a Research one beside it. Rename one. Paint floor -3 (the garden floor, if the bot has dug it) Farming from the City panel.
3. **5-7 min.** `RH.Growth now`, `RH.Floor -1`, 8x: watch a project rise and people move in.
4. **7-9 min.** Set CITY <-> EARTH to 25%, then 75%. Watch the Objective panel and `RH.Growth`.
5. **9-11 min.** Lay a road from the spine to somewhere new. Demolish one stub.
6. **11-13 min.** Order a rover: add a Cargo Rover to the next ship's cargo, or `RH.Rover spawn` now. `RH.Rover`.
7. **13-15 min.** Turn TRAFFIC on, hover the roads, read the commuting line and the Hope lines.

Expect few shuttles here: everyone lives and works at the shaft head (the longest trip is about 120 m against the 300 m line). Judge the street life in C6 setup B.

- [ ] It feels like a living city, not a colony with overlays: yes/no. What would make it feel alive: ____

## Open look decisions (waiting on your eye)

- **L1.** TAA or TSR (C3 commit 7160b73): TAA saves about 14 ms on floor -1.
- **L2.** Crowd figures: accept the older static meshes, or brief new ones (C3)?
- **L3.** Shuttle art: what should a commuter shuttle be? Today it is a box (C6b, 9745c51).
- **L4.** Rover: are unspinning wheels acceptable? Is the pivot overlap OK, or should pivots be routed clear (C6b)?
- **L5.** Road marker posts and partial-fill construction art: wanted, or is the ghost enough (C4's deferred list)?
- **L6.** The district wash (a glass material used as a matte tint) and the DISTRICT tab only showing with the zone row (C5a).
- **L7.** City panel overflow in a tall city: a scroll, a tab, or a separate window (C6b)?

## Verdict template (copy, edit, send)

> C1 OK, C2 OK, C3 change: ..., C4 OK, C5 discuss: ..., C6 change: ..., big picture: ..., L1 TAA|TSR, L2 ..., L3 ..., L4 ..., L5 ..., L6 ..., L7 ...

---

## Appendix: boot lines (fresh colony, no title; set `UE` and `P` as above)

**A. The C3 crowd boot** (for C1 and C3):
```bash
"$UE" "$P" /Game/RedHope/Maps/L_Slice -game -windowed -resx=1600 -resy=900 -nosound -RHNoTitle "-ExecCmds=r.setres 1600x900w, RH.CityBench 3000, RH.After 3 RH.Floor -1, RH.After 4 RH.Cam 30 30 0.45"
```

**A2. The staged commuting city** (C6 setup B; the C6b city capture without its screenshots):
```bash
"$UE" "$P" /Game/RedHope/Maps/L_Slice -game -windowed -resx=1600 -resy=900 -nosound -RHNoTitle "-ExecCmds=r.setres 1600x900w, RH.After 0.3 RH.Lag 0, RH.After 0.5 RH.CityBench 3000, RH.After 0.7 RH.Traffic noon, RH.After 1.0 RH.Spawn Pylon 245 42, RH.After 1.03 RH.Spawn Habitat 245 30, RH.After 1.06 RH.Spawn Pylon 235 152, RH.After 1.09 RH.Spawn Habitat 235 140, RH.After 1.12 RH.Spawn Pylon 175 262, RH.After 1.15 RH.Spawn Habitat 175 250, RH.After 1.18 RH.Spawn Pylon 45 252, RH.After 1.21 RH.Spawn Habitat 45 240, RH.After 1.24 RH.Spawn Pylon -65 272, RH.After 1.27 RH.Spawn Habitat -65 260, RH.After 1.3 RH.Spawn Pylon -175 252, RH.After 1.33 RH.Spawn Habitat -175 240, RH.After 1.36 RH.Spawn Pylon -205 122, RH.After 1.39 RH.Spawn Habitat -205 110, RH.After 1.42 RH.Spawn Pylon -265 32, RH.After 1.45 RH.Spawn Habitat -265 20, RH.After 1.48 RH.Spawn Pylon -275 -88, RH.After 1.51 RH.Spawn Habitat -275 -100, RH.After 1.54 RH.Spawn Pylon -165 -158, RH.After 1.57 RH.Spawn Habitat -165 -170, RH.After 1.6 RH.Spawn Pylon -95 -238, RH.After 1.63 RH.Spawn Habitat -95 -250, RH.After 1.66 RH.Spawn Pylon 15 -288, RH.After 1.69 RH.Spawn Habitat 15 -300, RH.After 1.72 RH.Spawn Pylon 115 -208, RH.After 1.75 RH.Spawn Habitat 115 -220, RH.After 1.78 RH.Spawn Pylon 215 -158, RH.After 1.81 RH.Spawn Habitat 215 -170, RH.After 1.84 RH.Spawn Pylon 295 -68, RH.After 1.87 RH.Spawn Habitat 295 -80, RH.After 1.9 RH.RoadInstant -30 -11 -23 -23, RH.After 1.93 RH.RoadInstant -23 -23 -12 -30, RH.After 1.96 RH.RoadInstant -12 -30 1 -32, RH.After 1.99 RH.RoadInstant 1 -32 14 -29, RH.After 2.02 RH.RoadInstant 14 -29 25 -20, RH.After 2.05 RH.RoadInstant 25 -20 31 -9, RH.After 2.08 RH.RoadInstant 31 -9 32 4, RH.After 2.11 RH.RoadInstant 32 4 27 17, RH.After 2.14 RH.RoadInstant 27 17 18 26, RH.After 2.17 RH.RoadInstant 18 26 5 32, RH.After 2.2 RH.RoadInstant 5 32 -8 31, RH.After 2.23 RH.RoadInstant -8 31 -19 26, RH.After 2.26 RH.RoadInstant -19 26 -28 15, RH.After 2.29 RH.RoadInstant -28 15 -32 2, RH.After 2.32 RH.RoadInstant -32 2 -30 -11, RH.After 2.35 RH.RoadInstant 32 4 229 28, RH.After 2.38 RH.RoadInstant 27 17 221 132, RH.After 2.41 RH.RoadInstant 18 26 166 237, RH.After 2.44 RH.RoadInstant 5 32 42 224, RH.After 2.47 RH.RoadInstant -8 31 -61 245, RH.After 2.5 RH.RoadInstant -19 26 -165 227, RH.After 2.53 RH.RoadInstant -28 15 -191 103, RH.After 2.56 RH.RoadInstant -32 2 -249 19, RH.After 2.59 RH.RoadInstant -30 -11 -260 -95, RH.After 2.62 RH.RoadInstant -23 -23 -154 -159, RH.After 2.65 RH.RoadInstant -12 -30 -89 -235, RH.After 2.68 RH.RoadInstant 1 -32 14 -284, RH.After 2.71 RH.RoadInstant 14 -29 108 -206, RH.After 2.74 RH.RoadInstant 25 -20 203 -160, RH.After 2.77 RH.RoadInstant 31 -9 280 -76, RH.After 3.3 RH.District paint Homes 23 2 26 4, RH.After 3.35 RH.District paint Industry 22 13 25 15, RH.After 3.4 RH.District paint Farming 16 24 19 26, RH.After 3.45 RH.District paint Research 3 23 6 25, RH.After 3.5 RH.District paint Civic -8 25 -5 27, RH.After 3.55 RH.District paint Homes -19 23 -16 25, RH.After 3.6 RH.District paint Industry -22 10 -19 12, RH.After 3.65 RH.District paint Farming -28 1 -25 3, RH.After 3.7 RH.District paint Research -29 -11 -26 -9, RH.After 3.75 RH.District paint Civic -18 -18 -15 -16, RH.After 3.8 RH.District paint Homes -11 -26 -8 -24, RH.After 3.85 RH.District paint Industry 0 -31 3 -29, RH.After 3.9 RH.District paint Farming 10 -23 13 -21, RH.After 3.95 RH.District paint Research 20 -18 23 -16, RH.After 4.0 RH.District paint Civic 28 -9 31 -7, RH.After 4.05 RH.District floor Industry -1, RH.After 4.1 RH.District floor Farming -2, RH.After 4.15 RH.District floor Research -3, RH.After 4.2 RH.District floor Civic -4, RH.After 4.25 RH.District floor Homes -5, RH.After 5.8 RH.Traffic home Habitat, RH.After 6.8 RH.SetSpeed 1, RH.After 7.3 RH.Cam 0 0 0.6 38 0"
```

**B. The rover yard** (C6 setup A; the C6b rover capture without its screenshots):
```bash
"$UE" "$P" /Game/RedHope/Maps/L_Slice -game -windowed -resx=1600 -resy=900 -nosound -RHNoTitle "-ExecCmds=r.setres 1600x900w, RH.After 0.5 RH.Lag 0, RH.After 1.0 RH.RoadInstant -25 0 -25 -35, RH.After 1.05 RH.RoadInstant -25 -35 25 -35, RH.After 1.1 RH.RoadInstant 25 -35 25 0, RH.After 1.15 RH.RoadInstant -25 0 -100 0, RH.After 1.2 RH.RoadInstant -100 0 -100 -60, RH.After 1.25 RH.RoadInstant -100 0 -100 60, RH.After 1.3 RH.RoadInstant 25 0 100 0, RH.After 1.35 RH.RoadInstant 100 0 100 70, RH.After 1.4 RH.RoadInstant 100 70 40 70, RH.After 1.45 RH.RoadInstant 40 70 40 0, RH.After 1.5 RH.RoadInstant 100 0 100 -70, RH.After 1.55 RH.RoadInstant 100 -70 40 -70, RH.After 1.6 RH.RoadInstant -25 -35 -25 -80, RH.After 1.65 RH.RoadInstant -25 -80 -100 -60, RH.After 2.0 RH.Spawn Forge -100 -70, RH.After 2.05 RH.Spawn Forge -100 70, RH.After 2.1 RH.Spawn Stockpile 100 -80, RH.After 2.5 RH.AddSolid Forge Struct 5000 out, RH.After 3.0 RH.Rover spawn, RH.After 3.1 RH.Rover spawn, RH.After 3.5 RH.Cam -45 -20 0.52 22 25, RH.After 4.0 RH.SetSpeed 8, RH.After 4.5 RH.DeckExpand"
```
