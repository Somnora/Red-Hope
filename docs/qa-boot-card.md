# QA Boot Card — 2026-10-17, updated for the crew phase (`8fa8299` and later)

**New first: §0, the crew phase.** The crew-phase package (D1a-D5a, built
10-17) is a game that only exists from sol ~15 to sol ~110, and no preset
reached it: they all start from `RH.Demo`'s showcase colony. Three presets
now start real games instead. Your open crew-phase questions (the end of
`docs/qa/2026-10-17/crew-phase-brief.md`) are answered by playing §0.

The rest of the card is unchanged. It covers everything built since August: build mode
(BM-1 to BM-6 plus the prefab), alive pass 2, the MPFB crew, EVA suits and
airlocks, the build drawer, and the sound and score. Almost none of it has
been played by hand. The captures prove what renders; they cannot click,
drag or hover. **Hover, click and drag are the point of this session.**

The 08-18 card is archived beside its frames at
`docs/qa/2026-08-18/qa-boot-card.md`. Its leftover verdicts are at the end
of this card.

## Booting

No editor is needed. Each preset is one line:

```bash
bash scripts/unreal/rh_sandbox.sh opening    # §0a a new game from sol 0
bash scripts/unreal/rh_sandbox.sh midgame    # §0b a real game at sol 28.35: the next ship and the ice are yours
bash scripts/unreal/rh_sandbox.sh late       # §0c a real game at sol 62.35: Ice_B worked, round 5 of the ladder
bash scripts/unreal/rh_sandbox.sh build      # §1 build mode: a surface habitat, stocked
bash scripts/unreal/rh_sandbox.sh crew       # §2 crew life: floor -1 furnished
bash scripts/unreal/rh_sandbox.sh surface    # §3 robots: every building, a dig running
bash scripts/unreal/rh_sandbox.sh            # §4-6 the everything demo, riding down
SOUND=1 bash scripts/unreal/rh_sandbox.sh    # §7 the listening test (any preset)
```

`LOW=1` gives 1280x720 at 30 fps on a busy machine. Quit with Cmd+Q. Only
`opening` shows the title menu; the others skip it, because its CONTINUE
button loads the quick-save slot over the preset. Every boot shows the
WELCOME tip: click GOT IT. The
backquote key opens the console for the commands below. The §1-§6 presets
spend their first ~25-30 s at 60x so the world has filled in before you get
it; the §0 presets start at 1x.

Controls:
- Right-drag orbits the camera and the wheel zooms.
- Left-click places, selects and confirms. Esc cancels.
- T rotates the furnishing or fitting you are holding.
- Speed: Space pauses. **1** = 1x, **2** = 3x, **3** = 8x. One sol is about
  20 minutes at 1x and 2.5 minutes at 8x. **4** = 60x skips time, but at that
  speed the robots park and nothing gets built, so play the crew phase at 8x.

## Before you play: the click test (every gate, every UI commit)

Captures and console cheats cannot click, so a deck that ignored the mouse
went unseen for six days (`docs/qa/2026-10-21/buttons-investigation.md`: the
menus overlay took every click above the deck). The click test presses the
buttons with real Slate pointer events, one boot, no hands (about 70 seconds):

```bash
bash scripts/unreal/rh_clicktest.sh          # exit 0 and "CLICKTEST VERDICT: PASS" or it is broken
```

It boots with the title up and presses: title NEW COLONY and CONTINUE, pause
RESUME, a BUILD tab and a card (it arms the tool), a click on the ground that
queues the build, the five speed buttons, a manifest ADD, Traffic, the Tunnel
tool and a drag that queues a tunnel. It also asserts that every visible button
is reachable (`REACH n/n`), that an open menu and a deck panel swallow a click,
and that no button press leaked into the world (`leak.total`). One
`CLICKTEST <name> PASS|FAIL <why>` line per step is in
`Saved/RHCapture/logs/clicktest.log`. A FAIL names the step; an
`UNREACHABLE '<label>' ... leaf=<widget>@<file>(line)` line names what is in
front of a button. Run it before the hand-play so you are not the one who
finds a dead button.

By hand, at any boot (backquote console): `RH.Click <label>`, `RH.Hover <label>`,
`RH.ClickSweep [why]`, `RH.ClickText <label>`, `RH.ClickAt <fx> <fy>`,
`RH.ClickWorld <Xm> <Ym>`, `RH.DragWorld <X0m> <Y0m> <X1m> <Y1m>`.

## 0. The crew phase — `opening`, `midgame`, `late`

`midgame` and `late` are real games, not set-ups. Both are saved just after
sunrise and open at 1x over the colony. The playtest bot played
each one from sol 0, using only player orders, in the guide's order, and
saved it. The layout is the bot's, not a designed base: solar rows by the
Lander, a pylon chain to the ice. The first boot after any code or data
change re-makes the save (`scripts/unreal/rh_checkpoints.sh`, 2-5 minutes;
the terminal says so). Saves from before 10-17 (format v32 and older) no
longer load.

### 0a. `opening` — a new game from sol 0

The title menu, then NEW COLONY: the intro shot, the Lander, the starting
kits and the guide on step 1, exactly as a player starts.

| Try this | Good looks like |
|---|---|
| Follow the guide at 3x-8x through the first night | At sunset the Forge and Electrolyzer go idle: the day shift is on for both by default. If the bank still runs dry, the guide's POWER step asks you to put a machine on the day shift |
| Select the Forge | Its card has a **Day shift** toggle; every heavy machine has one |
| Note the pace | The bot met quota 1 at sol 5.6 and landed the first crew at sol 14.7. Before the day shift it was sol 4.2 and 10.5 |

### 0b. `midgame` — sol 28.35, lean cargo

The crew ship brought the Crew Pod alone. Four crew live in the vault on
about 67 sols of provisions, with no garden. Q3 (2,500 kg structural, 1,200 kg
O2, 250 kg H2) is nearly met, and the next ship's manifest is empty. Ice_A
runs low about two sols in.

| Try this | Good looks like |
|---|---|
| Watch the Objective panel | Q3 is met around sol 29.6. Then the cargo is yours to pick: Earth Soil Pallet + Seed Vault start a garden; a Spare Parts Crate keeps the fleet repaired |
| LAUNCH | A landing pad appears east of the colony. 3 sols later the supply ship comes down onto it, engines glowing; it sits a sol, then lifts off. The CEO renews Q3 with bills x1.25 (round 1). There is always a next ship |
| Around sol 30.6: **ICE_A RUNNING LOW** | The alert, then the Next Step panel switches to the WATER steps: survey about 700 m north, chain Grid Pylons out to Ice_B, drill it, and put a Water Plant beside the drill. **Do it yourself** - that is the mid-game goal. The bot took 6 sols |
| The Crew panel | Hope sits around 71 STEADY and should climb into THRIVING (75-89) as the garden and comforts come in. THRIVING is meant to be the normal good state now |
| Optional: `RH.AddStock Food -150` in the console | Neither checkpoint ever runs short of food, so this provokes it: **FOOD RUNNING LOW**, a food line in the Crew panel, and the guide's FOOD step jumps the queue |

### 0c. `late` — sol 62.35, full cargo

The bot has played the whole mid-game. Six people live in the colony, two of
them born on Mars. The garden is producing, Ice_B is drilled, and round 5 of
the ladder is open (7.6 t structural; the Objective panel reads "bills up
205%"). Hope is 98 FLOURISHING. Eight Solar Array kits from later ships sit
unplaced: the bot only places the first ship's. BUILD > Power places them.

| Try this | Good looks like |
|---|---|
| `RH.Hope` in the console | The breakdown: Milestones is at 30 points, and holds Hope at 100 by itself. This is open question 1 |
| Fill and launch round 5 | Each Specialty Toolkit adds +15% build speed, with no cap. This is open question 3 |
| Play at 8x to about sol 87 (an hour) | **ICE_B RUNNING LOW**, "the last known ice". The stock carries the colony well past sol 110. This is open question 4 |

## 1. Build mode — `build` preset

You start over a surface habitat (RH.HabDemo): a 2x2 module, a corridor, a
1x2 module and an air filter, with the bays zoned. There are 3000 kg Struct,
1000 Glass and 500 Shielding on hand. The ZONE | FURNISH | WALLS row sits
above the command bar whenever a habitat stands.

| Try this | Good looks like | Frame already shot |
|---|---|---|
| BUILD > Habitat > **Habitat Bay**, then drag on the ground | The ghost snaps to the 10 m bay grid and sizes as you drag; the bill updates per bay. Release to order it | `build-drawer-habitat.jpg`, `hab-thumbnails.jpg` |
| Place a bay edge to edge with the existing habitat | It joins the network: shared walls become doorways | `prefab-habitat.jpg` |
| **Habitat** (the prefab) | A fixed 1x2 shell; the card reads "2 habitat segments" (the Forge makes them) | `prefab-habitat.jpg` |
| ZONE: click a room, then a bay | The bay takes the room; its props appear | — |
| FURNISH: pick a Cot, hover a Living bay, press T, click | The ghost sits on the 2.5 m sub-grid and turns with T; the hint shows beds before → after; it installs after the uplink delay | `furnish-mode.jpg` |
| FURNISH a **Sofa** into a bare Living bay where someone sleeps | Refused: "put a bed in this bay first" (today's ruling: a Living bay with furniture but no beds sleeps nobody) | — |
| FURNISH > Sell, click a piece | 75% back; refused if it would leave a sleeper without a bed | — |
| WALLS: Window or Door, hover a wall, click | The nearest wall lights; windows glaze outside walls; doors only go in doorways | `windows-doors.jpg` |
| Watch a doored doorway as crew pass | **The door leaves part.** Never caught in a capture yet | — |
| WALLS: Airlock on an outside wall | The crew's entrance moves there; suits come off in its mist | `airlock-cycle.jpg` |
| Select a building, then Demolish twice | The first click arms it (amber), the second confirms; salvage lands on the Lander | `demolish.jpg` |

**Not tested by hand at all yet:** drag-sizing, the furnish and fit ghosts
under a real mouse, T, Sell mode, Demolish's two clicks, and the door
animation.

## 2. Crew life — `crew` preset

Floor -1 is furnished: a bedroom (two cots, a bunk), a lounge, desks, a lab
bench and a dining room. The crew live in it on their own. Day and night
follow the sol clock (night is the first and last quarter of the sol).

- **Sleep:** each colonist lies on the bed the sim gave them; a bunk's
  second sleeper takes the top berth.
- **Work:** at their own desk they sit and type; at a bench they stand.
- **Meals:** a free dining seat - the table's benches or a chair - using
  the eating clip.
- **Off duty:** the sofa, or a lounge bench.
- **Settling:** they sit or lie down in about half a second, and stand up
  before walking off.
- **Crew moments:**
  - A pastime is two people face to face, talking with gestures.
  - A dispute is a still face-off under the amber plate (your ruling).
  - A join-in is the helper working beside the worker.
- **Personal space:** nobody walks through or stands inside anyone.

Don't wait for the clock - force a beat in the console:
`rh.CrewForceBeat 1` sleep / `2` work / `3` eat / `4` relax / `0` natural.
For an A/B on personal space, `rh.CrewSeparation 0` brings the old
pass-through walking back.

Frames already shot: `alive-furniture.jpg`, `alive-crowd-talk.jpg`, and
`alive-surface-hab.jpg` (the same behaviour in a surface habitat).

## 3. Robots — `surface` preset

- Robots walk around buildings, not through them. They are drawn on a
  detour but still arrive when the sim says.
- Starts, stops and turns are eased.
- A robot idle for ~6 s walks to a rank beside the Lander. Your ruling:
  this is presentation only - the sim leaves it where it stopped.
- `rh.RobotRouting 0` draws them at the raw sim position, for an A/B.

Frame: `alive-robots.jpg`.

## 4. The crew close up — default preset

- **The MPFB crew at true height (1.60-1.88 m):** this has never been
  judged in game. Zoom right in, or in the console `RH.CamCrew 0 0.02`
  (then 1, 2, 3...) to frame each one. It works once they are on screen;
  it only fails when run at boot.
- **EVA suits:** anyone on the surface outside a habitat wears one, and it
  comes off in the airlock mist. Frames: `eva-suits-ingame.jpg`,
  `airlock-cycle.jpg`.
- **Elevator boarding:** the crew walk to the cab, the doors part, they
  ride down and step out.

## 5. The interface — default preset

- **Opening shot and the WELCOME tip:** does the guide's wording read
  right?
- **The build drawer:**
  - Every card has a thumbnail.
  - Hovering a card fills the detail panel (power, time, cost) - a hover
    that no capture can test.
- **The right-hand panels:** Next Step, Objective, Earth & Neighbours,
  Crew, Fleet, Power.

## 6. Sound and score — `SOUND=1`

Until now they have been verified with the speakers off only. Listen for:
- the machines' ambience
- interface clicks
- the elevator chime
- two score tracks per phase

The mix is set from the console with the `rh.AudioMaster`, `rh.AudioMusic`,
`rh.AudioSFX`, `rh.AudioAmbience` and `rh.AudioUI` cvars.

## Known limits — please don't bug-report these

- **Morale, sickness, evacuation and collapse wording** - including the
  dispute plate "! heated words !" - is a Gate-D placeholder pending your
  framing review (`docs/qa/2026-10-15/gate-d-wording.md`).
- **Balance numbers** - Hope weights, bills, build times, furniture costs -
  are legible placeholders, not tuning.
- **Unfurnished rooms:** where there is no furniture, crew use the old ring
  of spots around the room's centre.
- **Old saves:** a colonist already housed in a bed-less lounge stays
  there without a bed. Saves from before 10-17 (format v32 and older) don't
  load at all.
- **The checkpoints' layout** is the playtest bot's, not a designed base.
- **New alert and guide wording** (ICE / FOOD / POWER / the CEO's renewals)
  is Gate-D placeholder text, rows D86-D122.
- **Surface jobs:** surface residents get no job seats in a surface
  Workstation bay in RH.HabDemo, so nobody sits at its desks there.
- **Parked robots:** a parked robot is drawn up to ~38 m from its sim
  position until its next task (ruled acceptable).

## Verdicts I need back

**From §0, the crew phase** (the open questions in the brief):
- **A. The opening's pace (`opening`):** the day shift moves the first crew
  from sol 10.5 to 14.7. Keep it, or switch the default off
  (`DayShiftDefaultsOn=0`) and keep the toggle?
- **B. The mid-game (`midgame`):** does the ice warning come in time? Does
  the guide get you to Ice_B without help? Is picking each ship's cargo a
  good decision or a chore?
- **C. Hope:** does THRIVING feel like the normal good state? Late on, should
  the Milestones line be capped, fade, or stay (a mature colony flourishes)?
- **D. Cargo stacking (`late`):** cap the Toolkit bonus, or leave it?
- **E. After Ice_B:** a third ice deposit, the dormant agri water recycling,
  or nothing yet?

- **F. The new surface art:** the Charging Pad (rings brighten while a robot charges), the dig sites (a mound until worked, then flags, terraces and a spoil heap; regolith, ice and ore each look different), and the supply ship and its landing pad. Keep, or what is wrong?

**From §1-§7:**

1. **Build mode by hand:** does placing, zoning, furnishing, fitting and
   demolishing feel right? Note anything fiddly - snapping, hover, T, the
   hint.
2. **Door leaves:** do they part convincingly for passing crew?
3. **Crew life:** do sleeping, working, eating and lounging read at
   play zoom? Is the eating pose right?
4. **The MPFB crew in game, at true height:** pass, or what is wrong?
5. **Robots:** do routing and parking read right?
6. **The interface:** opening shot, guide wording, build drawer.
7. **The listening test:** levels, and which sounds are wrong or missing.

**Carried from the 08-18 card.** Whether these were answered is not
recorded in any commit since, so they may be stale - confirm or close:
- Crop activation: flip `SliceActive`, or keep it dormant. Still open per
  `PROJECT_STATUS.md` §4.
- Night glow (`rh.Glow`), night-lamp brightness and the 40% accent tint.
- The cutaway default (`rh.Cutaway` 0-2).
- Wear strength (`RH.Wear`) and pulse (`RH.Pulse`).
- ModularBlock: repurpose or shelve.
