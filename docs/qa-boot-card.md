# QA Boot Card — 2026-10-17, against `480bda5`

This is the hand-play card for everything built since August: build mode
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
bash scripts/unreal/rh_sandbox.sh build      # §1 build mode: a surface habitat, stocked
bash scripts/unreal/rh_sandbox.sh crew       # §2 crew life: floor -1 furnished
bash scripts/unreal/rh_sandbox.sh surface    # §3 robots: every building, a dig running
bash scripts/unreal/rh_sandbox.sh            # §4-6 the everything demo, riding down
SOUND=1 bash scripts/unreal/rh_sandbox.sh    # §7 the listening test (any preset)
```

`LOW=1` gives 1280x720 at 30 fps on a busy machine. Quit with Cmd+Q. The
backquote key opens the console for the commands below. Every preset spends
its first ~25-30 s at 60x so the world has filled in before you get it.

Controls:
- Right-drag orbits the camera and the wheel zooms.
- Left-click places, selects and confirms. Esc cancels.
- T rotates the furnishing or fitting you are holding.

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
  there without a bed.
- **Surface jobs:** surface residents get no job seats in a surface
  Workstation bay in RH.HabDemo, so nobody sits at its desks there.
- **Parked robots:** a parked robot is drawn up to ~38 m from its sim
  position until its next task (ruled acceptable).

## Verdicts I need back

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
