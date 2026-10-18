# The Red Hope — Project Status

**Snapshot: 2026-10-16 (build mode complete: BM-1..BM-6 and the BM-1b prefab; alive pass 2).** The 10-15 snapshot ran
through `fdffc75`; the one before was 2026-08-18 (`b306100`). This is the working source-of-truth snapshot: what the
game IS, what is built, what changed, and what is owed. Ordered by the
three-phase arc from the design brief (`docs/the-red-hope-design-brief.md`).
Everything is on `main` (local `main` matches the `origin/main` tracking ref,
`https://github.com/Somnora/Red-Hope.git`). Save format **v32**.

## 2026-10-16: build mode, suits and airlocks

- **The director's build-mode ruling** (`130e065`; decisions 1a-6a in
  `docs/design-decisions.md`): BM-1 surface habitats (sim) → BM-2 their
  presentation → BM-3 demolish → BM-4/5 furniture → BM-6 windows and doors.
- **BM-1, surface habitats** (`e6ee887`, save **v29**): pressurized
  HabModule and HabCorridor shells on a 10 m bay grid join into networks by
  shared edges. A network is livable at 4 bays, full of O2 and circulated (an
  Air Filter in a bay, or the optional shaft link to a rated floor -1). Bays
  are zoned like cells; Lab and Infirmary modules stand in bays. **Crew still
  need the vault** (director 10-16): surface beds open only once the
  underground vault is certified. Suite `-surfacehab` (78 checks).
- **BM-2, presentation** (`05fe968`): shells dragged out on the grid, derived
  walls with doors, cutaway, zone and station cards for bays; residents live,
  sleep, eat and work in their habitat.
- **EVA suits** (`3a0cf82`): male and female suited walkers on the crew rig;
  anyone outdoors on the surface wears one.
- **Airlocks and doorways** (`3047a26`): walks route bay to bay through the
  doorways; each habitat network has one decompression chamber where a mist
  hides the suit coming off (going in) or on (going out). The elevator is the
  other chamber: suited at the top, unsuited below.
- **BM-3, demolish** (this snapshot): an uplink verb, `Demolish`, takes a
  structure down or cancels a site; the action card's button takes two clicks
  and names the salvage or the refusal; the structure wears an amber frame
  while the order is in flight. A site returns everything delivered (and an
  imported kit); a standing structure returns its held stock plus
  `SalvageFraction` (0.5) of its bill, all to the Lander. Refused: the Lander,
  standing pylons, the Borer once the shaft exists, a home, a habitat with
  machines inside, a removal that would strand residents (bay minimum or
  air), and the last filter people breathe through. Cargo in flight is
  rerouted, never lost. No save change. Suite `-demolish` (62 checks).
  **Open for the director:** built imports salvage nothing (no kit back),
  pylons can't come down, a habitat must be emptied of machines first.
- **Furniture art** (`47d6667`): nine pieces built in code in the tier-room
  bench family (`scripts/blender/rh_build_furnishings.py`) - chair, desk with
  its chair, bench, sofa, floor lamp, rug, cot, bunk bed, potted plant - at
  TRUE scale (`/Game/RedHope/Art/Furniture`). Sheet: `docs/media/furniture-pieces.jpg`.
- **BM-4, the furniture catalogue (sim)** (`d180f12`, save **v30**): 13 pieces
  in `RH_Furniture.csv` / `DT_Furniture`, Struct-only, placed on a 4x4 grid of
  2.5 m sub-cells in a zoned cell or habitat bay by a `Furnish` order (instant
  when it lands, decision 5a) and sold back at 75% (`Sell`). Effects by kind:
  a Living cell with beds houses exactly its beds; a station with desks or
  benches seats exactly them; comfort pieces feed a capped (6) bonus-only Hope
  line, Furnishings. Rooms without such pieces are exactly as before. Bed
  guard on selling and re-zoning; a demolished habitat sells its furniture
  back. Derived bed/seat assignments per colonist (for alive pass 2). Suite
  `-furnish` (63 checks).
- **BM-5, Furnish mode** (this snapshot): the zone row's FURNISH tab (the
  catalogue with prices, and Sell); a ghost on the sub-grid with a facing
  arrow, T to turn it, and a hint naming the effect ("beds here 1 -> 2") or
  the refusal; in-flight furnish/sell ghosts; placed pieces in the world (a
  furnished cell hides its automatic room prop; seats pull up to what they
  face); crew walk around them. `RH.Furnish`, `RH.Sell`, `RH.DeckFurnish`.
  Sheet: `docs/media/furnish-mode.jpg`. **Not hand-tested** (captures can't
  move the mouse): the hover ghost, T, and click-to-place.
- **BM-6, windows and doors** (`14f4e99` sim, save **v31**; presentation in
  this snapshot): the M2 door table grown into the wall catalogue. A Pressure
  Door goes in a doorway (between rooms or modules); a door in EVERY doorway
  seals the habitat network - it leaks at the Sealed Bay rate (half). A
  Window (Struct 10 + Glass 40) goes in an outside wall: Views Hope +0.5
  (capped at 3) for 0.5 kg/sol of air. An Airlock sets the habitat's
  entrance (cosmetic, ruling 6a; unplaced networks keep the derived one).
  Fit / Sell uplink orders; no module may wall over a window; demolish sells
  fittings back; Glass refunds go to the pool. Suite `-walls` (37 checks,
  leaks measured). In game: the zone row's WALLS tab, Fit mode (the hovered
  bay's nearest wall lights, the hint names the effect), glazed windows,
  door leaves that part for crew, placed airlocks. Sheet:
  `docs/media/windows-doors.jpg`. **Not hand-tested:** Fit hover/click; the
  door-opening animation was not caught in a capture.
- **BM-1b, the prefab Habitat** (this snapshot; ruling 2a): the dormant
  Habitat row, HabSegment resource and MakeHabSegment recipe go live. The
  prefab is a fixed 1x2-bay pressurized shell whose bays are Living Quarters
  (2 beds), billed 2 HabSegment, which the Forge presses from Struct 120 +
  Shielding 30. The construction shortage now looks through a short output's
  recipe to inputs the colony holds none of, so the Forge makes the Shielding
  the segments need. It joins networks and rates like any shell. Suite
  `-prefab` (11 checks: the chain end to end with stand-in haulers - 100 kg
  Shielding made, 2 segments delivered in 0.8 sols). In game it draws as its
  rooms (no dome model over it). Sheet: `docs/media/prefab-habitat.jpg`.
- **Build-menu thumbnails for the Habitat Bay, Corridor and prefab Habitat**
  (after alive pass 2). They are rendered by RH.Thumbs like the rest, and
  pressurized shells are shot from 50 degrees down so the rooms inside read.
  The prefab's card no longer claims a per-bay, drag-sized bill: it reads "2
  habitat segments" (counted, not weighed) and "a ready-made 1 x 2-bay
  habitat, placed whole". Sheet: `docs/media/hab-thumbnails.jpg`. The
  director then renamed the prefab "Habitat" (was "Habitat Module") and
  approved its blurb, dropping the placeholder tag.

## 2026-10-16: alive pass 2 (presentation only; RedHopeSim untouched)

- **2a, people use the furniture** (`d544d31`). Five new crew clips on the
  shared MPFB rig: Sit, SitWork, SitEat, Lie and Talk. All 20 crew and both
  suits were rebuilt and re-imported with 15 clips each. At night colonists lie
  in the bed the sim assigned them (a bunk's second sleeper takes the top
  berth). At work they sit at their desk's chair and type, or stand at one of
  a bench's two places. At meals they take a free dining seat; off duty, a sofa
  cushion or a lounge chair or bench. They settle in over ~0.45 s and stand up
  before walking off. A facing bug was fixed on the way: the code-built
  furniture came in from glTF back to front, so every chair had faced away from
  its table. `rh.CrewForceBeat` forces beats for captures. Sheet:
  `docs/media/alive-furniture.jpg`. Surface-habitat residents were
  captured on 2026-10-16 (RH.HabDemo, furnished bays, 10 colonists added):
  they sleep in the hab's cots and bunk (both berths), eat at its dining
  table and chairs, and sit on the lounge bench and sofa
  (`docs/media/alive-surface-hab.jpg`). **Gap found:** a Living bay furnished
  without beds (a lounge) still counts its 2 legacy beds in the sim, but its
  automatic bed props are hidden once any furniture is placed. A colonist
  housed on such a bed has no bed piece and stands at the old fallback spot at
  night. Ruled the same day (option 2): a surface Living bay with any
  furnishing now houses exactly its bed pieces (a lounge sleeps nobody), and
  furnishing one is refused while it would leave residents without beds.
  Underground cells keep the legacy beds.
- **2b, conversations and personal space** (`9bf87a8`). Crew moments get body
  language: a pastime is a face-to-face Talk; a join-in has the helper work
  alongside. A dispute is a still face-off under the amber plate, and the Talk
  gesture is deliberately not used there (Gate-D: words, never blows). The
  director ruled 2026-10-16 to keep the still face-off. The plate's wording
  stays a Gate-D placeholder string. Figures steer around one another and
  ease apart when standing inside each other. With 20 crew, overlapping pairs
  fell from 7.9 to 0.18 per sample (A/B via `rh.CrewSeparation`). Sheet:
  `docs/media/alive-crowd-talk.jpg`.
- **2c, robots route, ease and park** (this snapshot). Presentation only: the sim
  still walks robots in straight lines and stays the authority on where they
  are and when they arrive. The drawn figure walks each trip along a detour
  around surface building footprints (a visibility graph over the expanded
  footprints), at the sim's progress fraction, so it arrives on time. Starts
  and stops are eased and turns are smoothed. A robot idle for 6 sim-seconds
  walks to a parking rank beside the Lander. **The trade-off:** a parked robot
  is drawn up to ~38 m from its sim position until its next task, when it walks
  back. Robots drawn inside a footprint that isn't their own fell from 54 to 2
  robot-samples over the same deterministic run (A/B via `rh.RobotRouting`).
  Sheet: `docs/media/alive-robots.jpg`. **Director ruling 2026-10-16:** parking
  stays presentation only. The sim keeps idle robots where they stop, and the
  battery baselines are untouched.

## Since August (29 commits, `147de7a` → `fdffc75`; detail in §3c)

- **Hero lane finished the static family** (`147de7a`); **robots play their
  task poses** - the work clips had shipped unwired (`9efba3d`).
- **Model triage → buildings built in code**: the director judged a lineup of
  every shipping mesh (`db6a99a`) on 10-13; everything marked "weird"/"fix" was
  rebuilt as a hard-surface Blender build from its approved reference
  (`221afe7`, `03d86be`, `07841c8`, `33caddf`, `1d2342c`, `951f3f8`): 12
  buildings, the elevator cab + door, 6 props, 2 crop stages.
- **The whole crew on one MPFB body** (`8b52734`, `2c784da`): one mesh, one
  53-bone skeleton, one clip set, 1.60–1.88 m; **face pass** (`2325955`).
- **Elevator doors + boarding live, crew at true height, real models fill
  their footprints** (`9532013`).
- **Sound** (`20807bf`) and a **six-track Lyria score**, two per phase (`bb240a8`).
- **Interface pass**: step 1 panel deck, console-only verbs as buttons, an
  opening shot (`48934b9`); step 2 card build menu with in-game thumbnails
  (`9a9261e`).
- **The first-hour guide**: a live 19-step NEXT STEP panel (`60c14a2`).
- **`RH.Playtest`**, a scripted player, found a **permanent softlock**: robots
  recharge only at a finished Charging Pad (`0ca24d3`). Fixed by the director's
  call - **the Lander is an emergency charging dock**, `LanderDockRate_W`
  100 W (`6410bf8`).
- **Crew-phase playtest** (`4706e5f`): two quotas make the crew ship the
  program's last; a lean cargo empties the colony at sol 77 with no warning
  before sol 75; with soil + seeds Hope pins at 100 from sol 16. The
  GROW-LIGHTS DARK alert flood (12,524 in 40 sols) is fixed (now 1).
- **Toolchain** (outside the repo): Metal Toolchain reinstalled after the
  Xcode 27 update (`db6a99a`); UE 5.8's SDK gate opened for Xcode 27 (`9532013`).
- **Front end + the director's session pack** (`daaf86b`): title, pause,
  settings and credits menus; balance-sweep hooks (`-RHConfig`, `-RHQuotas`);
  the Gate-D wording inventory (`docs/qa/2026-10-15/gate-d-wording.md`).
- **A packaged, double-clickable Mac build** (`e10fedc`):
  `bash scripts/unreal/rh_package.sh` → `/Volumes/Unreal/red_hope/Packaged/Mac/red_hope.app`.
- **Tiered production, finished**: prefab Lab and Infirmary modules (`f77b962`,
  T1); ASSIGN STAFF + seat priority, lazy seat fill behind a switch (`63f452f`,
  T2+T3, save v27); clickable stations with seat cards (`fdffc75`, T4).
- **Agri Gate C**: the drag-sized surface Greenhouse Dome (`585ee8e` sim, save
  v28; `ea2a791` drag placement + a glass shell built in code).

**Verification bar.** The full headless battery is **the 10-sol baseline + 35
suites (36 runs)**, plus the byte-identical regression pins (`power: gen 50 W
load 20 W`, `deposit Regolith_A: 40645 kg`), which have not moved once across
the art, audio, interface, playtest, tiered-production, dome and build-mode
work. Last reported fully green on the BM-1b commit (32 runs) ("0 suite(s) not green", pins
unchanged); also on `d180f12` (BM-4), `3332e21` (BM-3), `e6ee887`, `585ee8e`,
`f77b962`, `63f452f`, `4706e5f` and earlier. Fifteen suites fail loudly on any
mismatch: `-staff`, `-dome`, `-surfacehab`, `-demolish`, `-furnish`, `-walls`,
`-prefab`, `-icelow`, `-food`, `-resupply`, `-dayshift` (D5a, 2026-10-17),
and, since D3a (2026-10-17), `-rooms`, `-growth`, `-discovery` and `-tiers`,
converted from print-only (the run count is 36 with `-dayshift`). `-rooms`
is the pin for the three halved Hope weights (7.5 / 2.5 / 1.5), read from the
DT_Config row alone. The rest are still print-only.
```bash
bash scripts/sim/rh_battery.sh            # all 36 runs; MUST be bash
UnrealEditor-Cmd red_hope.uproject -run=RHSim -sols=10 -<suite> -unattended -nosound -stdout
# suites: crew habitat vault borer rooms garden luxury hopedrive greenhouse water
#         growth discovery trade earth solidarity covert espionage preemptive
#         crisis alive ladder ending tiers agri staff dome surfacehab demolish furnish walls prefab icelow food resupply dayshift
#         (+ no-switch = the baseline)
```

**The simulation has changed since the 07-15 consolidation, in pin-neutral
ways:** greenhouse agriculture (save v26, 07-16, crops dormant), pure-data
self-tests (`163165a`), a read-only manifest iterator (`48934b9`), the Lander
dock (`6410bf8`), the grow-light alert re-arm (`4706e5f`, runtime only),
prefab station modules (`f77b962`), seat priority and lazy fill (`63f452f`,
lazy fill off by default), the greenhouse dome (`585ee8e`), surface habitats
(`e6ee887`), demolish (BM-3), furniture (BM-4), wall fittings (BM-6). Each is inert for a colony that doesn't use it.

---

## 1. The game in one paragraph

A Mars colony sim across three phases — **Automation → Habitation →
Sovereignty**. Robots bootstrap an empty region (dig, refine, build, meet the
CEO's supply quotas); humans arrive with needs and morale; the colony grows into
a settlement that must choose what kind of Mars it becomes — Earth's corporate
jewel, an independent federation, or something colder. **Hope** is the central
derived index that couples it all together: rooms, comforts, and water feed
Hope; Hope drives work tempo, population growth, and research; research and
milestones feed Hope back. Two political axes with teeth — **Identity**
(Earth ↔ Mars) and **Human Nature** (Evolved ↔ Destructive) — read toward four
endings.

---

## 2. The mechanics, phase by phase (all built + headless-verified)

### Phase 1 — Automation (M0 + M1)
- **Power & territory**, deterministic Mass-entity robot fleet on a fixed sim
  sub-step (StateTree brains, `ai.mass.DynamicSTProcessorsEnabled=0`).
- **Production spine**: dig → haul → refine (Forge/Electrolyzer/WaterPlant/ISRU)
  → build. Hybrid logistics (solids hauled, fluids pooled).
- **The CEO loop**: meet a quota → earn a supply-ship award → **hand-design the
  manifest** (the brief's signature logistics puzzle) → ship transits → cargo
  lands with mechanical effects. Save/load exact-equivalence; 60× era-mode
  aggregate integrator with proven agent-vs-era parity.
- **Fleet reality**: wear/degrade/halt + repair, survey (hidden deposits),
  one-umbilical pad queue, storms & solar flares (dust derates solar, flares tax
  exposed robots), radiation curve (shielded underground). **Charging**: the
  Charging Pad is the real charger (500 W, one robot at a time); with no working
  pad on the robot's floor, robots dock on the Lander's south side at
  `LanderDockRate_W` (100 W each, all at once). A Lander switched off is no
  dock (`6410bf8`).
- **The vault**: bore a shaft, carve floors, shield → oxygenate → circulate →
  **rate a floor LIVABLE**. Underground construction, the trunk hauls across
  levels, sliced-camera pit view.

### Phase 2 — Habitation (M2) — "The Crew Arrives"
- **Colonists as agents**: crew pods disembark only into certified housing;
  breathe the floor's O2, eat pooled Food, drink Water; unmet needs → a
  prevention-framed evacuation countdown (abstract, never graphic).
- **Rooms & adjacency**: designate carved cells (Living/Lab/Workstation/Dining/
  Cooking/Garden/Greenhouse/Hallway…); an Odor emitter beside a refuser offends;
  a Hallway partition + floor filtration cures it. Deterministic **job seats**.
- **Hope index** (pure derived read): base + housing + rooms + jobs + vault +
  comforts − adjacency − unsupported, low-passed into **HopeSmoothed** (τ=3
  sols, the agent/era parity property), five hysteresis bands.
- **Hope drives the colony**: **work tempo** 0.60–1.25 (floored at 0.60 —
  slow, never dead: the death-spiral guardrail), **garden yield**, and
  **population growth** (Hope≥75 + free bed + food buffer → the first
  Martian-born child).
- **The garden & water loop**: soil+seeds auto-plant; grow-lit Garden (battery
  cost) vs solar-riding Greenhouse (glass); greywater recycling degrades
  **potability** (a scalar) → drilling/importing ice is what keeps a colony
  flourishing enough to grow. **Discoveries**: staffed Labs uncover an authored
  sequence ending in **LIFE ON MARS** (permanent Hope milestones).
- **Greenhouse agriculture** (save v26, spec Gates A/B/D): `DT_Crops` with 7
  crops and growth stages, soil depletion + the compost/spoil-chems →
  FertilizerMix loop, per-floor climate with the HumidityRegulator gate, duct
  CO2/O2 exchange. **All crops ship dormant** (dormant = the legacy garden
  bit-for-bit); activation is a director gate flip.
- **The alive pass**: per-colonist **skills** (mastery ramps output 1×→2×),
  crew-life **moments** (abstracted, never violent), the **first Martian
  harvest** celebration, a 1600-name diverse roster. **Onboarding** is now the
  first-hour guide (`60c14a2`; presentation, capture-verified) plus tip cards
  and milestone/first-stock toasts.
- **Tiered production** (`f77b962`, `63f452f`, `fdffc75`): prefab **Lab Module**
  (counts as a Full Laboratory cell: 2 seats at x2.0) and **Infirmary Module**,
  placed on a dug cell once research opens their room; one station per cell.
  **ASSIGN STAFF** (uplink verb `Assign`) puts a station first in the staff
  order; click a station to see its seats. **Lazy seat fill**
  (`LazySeatFillSols`, shipped 0 = instant) makes unassigned seats wait.
- **The Greenhouse Dome** (agri Gate C, `585ee8e`, `ea2a791`): drag to size on
  the surface in 10 m beds (up to 3 x 3); each bed = one underground greenhouse
  cell (planted from Glass + Soil + Seeds, grows by daylight, no grow-light
  power); a glass dome from four beds up, a flat glass house below.

### Phase 3 — Sovereignty (M3 + M4)
- **Neighbors & trade** (M3-A): rover convoys to rival colonies, all costs
  committed at departure, transit freezes in dust storms; Zarya's ice restores
  your water potability — **trade is an alternative to drilling your own ice**.
- **Earth's Shadow** (M3-B): tension drifts → demands; a **requisition
  multiplier** scales your CEO award by standing (Identity axis + tension).
- **The Solidarity Dilemma** (M3-C): Comply (sever routes, national pride,
  morale grief) vs Defy (Martian pride, warm rivals, requisitions stay slashed,
  escalation) — the brief's signature choice.
- **Espionage economy** (M4-A/B): covert requisition (seeded-hash detection
  check), launder, sabotage-blackout — on-brief embargo/blackout, **no combat**.
- **Earth's pre-emptive pressure** (M4-C): Influence currency, embargo, Pacify,
  permanent defection.
- **The finale** (M4-D): alignment-gated **dynamic crises** (units inert, never
  feral) and the **endings framework** → Corporate Jewel / Independent Mars
  Federation / Martian Cold War / Abandonment-Collapse.
- Since `48934b9` every M3/M4 verb has a deck button that enqueues the same
  uplink command the `RH.*` cheats send.

---

## 3. History: the 2026-07-15 consolidation (kept, compressed)

All of it is on `main`.
1. Committed the uncommitted animation-phase work (`29bb31b`, 646 files) and
   the two 2026-07-10 specs + project CLAUDE.md (`4df818d`).
2. **v25** (`6fd3faa`): the goal ladder (meeting a quota opens the next), ending
   resolution (EPILOGUE / COLLAPSE), research funding + UnlockRoom, station
   tiers (tiered-production Gate A, incl. the Infirmary), repeatable weather.
   Suites `-ladder -ending -tiers`.
3. Guides: `docs/asset-pipeline-guide.md`, `docs/model-material-polishing-guide.md`.
4. A headless-Blender polish pass on ~36 GLBs (QA found them fully split/
   faceted). Mostly moot now: see §3c.

Follow-ups that landed 07-16: the full DataTable sync, headless (`e556267`);
the self-tests became pure-data verifiers (`163165a`: CSV-mirroring injects
replaced by drift assertions; two synthetic fixtures, Greed and TestCeramics,
stay injected by design); greenhouse agriculture (save v26).

---

## 3b. History: the art & presentation arc (2026-08-14 → 08-18, compressed)

No simulation code changed; the battery stayed green with identical pins.
- **"Splotchy / see-through crew"**: the pipeline decimated BEFORE rigging
  (shattered shells, confetti UVs, every vertex hard-pinned to one bone) and the
  crew sat on a subsurface skin shader. Fixed then (`docs/crew-generator-test.md`);
  superseded now by the MPFB crew (§3c).
- **Master materials** gained normal and MR inputs - <0.3% of pixels at the
  29–216 m camera range. What reads there is silhouette, value contrast,
  emissive and lighting, hence per-building night lamps.
- **`M_RH_Master` failed the Metal shader translator** until `6816d4b`:
  buildings rendered gray in-game while editor dumps looked healthy. Doctrine: a
  frame with material compile failures is NOT JUDGEABLE (`rh_capture.sh` counts
  them).
- **The hero lane** (Nano-Banana 4K → TRELLIS.2 → baked normals) re-did nine
  buildings through `147de7a`; only the Pylon is still a hero-lane mesh in game.
  GPU work moved to its own filesystem `red-hope-east`; the lane's tooling was
  rescued into git (`scripts/gpu/lane/`).
- **Lessons kept:** commit the excluded-file list before deleting a migration
  source (the 14 GB `io/` loss); scripts the docs point at belong in git
  (`147de7a` recreated `rh_paint_kit.py`/`rh_paint_return.py` from a lost
  scratchpad).

---

## 3c. The October arc (2026-10-13 → 10-15)

**Buildings and props, built in code.** The 10-13 triage split the models
cleanly: the ones the director liked (Lander, Habitat, Pylon, SolarArray) were
clean silhouettes; the "weird" ones were image-to-3D output - melted detail,
shredded fins, holes, a function tint over the whole hull. The hard-surface kit
`scripts/blender/rh_hs.py` (Blender 5.2, headless, local) builds bevelled
primitives in art-bible role materials and bakes BaseColor (edge chipping,
cavity rust, AO grime, Mars dust), an MR map and an emissive mask: one mesh, one
material, mostly 8–17k tris. Each `rh_build_<name>.py` keeps its approved
reference, was self-checked (no floating islands, z-fights or degenerate faces)
and reviewed, then `scripts/unreal/rh_import_hs.py` replaced it IN PLACE (same
`/Game` path, no C++ change). Accent is trim only; glow sits behind a real mask.
- Rebuilt: BatteryBank, ComputeModule (`CommandModule`), AirFilter,
  HumidityRegulator, Electrolyzer, Forge (`HeavyForge`), IceDrill, Stockpile,
  WaterPlant, Borer, Lander, Habitat; elevator cab + door; props infirmary (no
  IV pole, Gate-D), lab_full, workbench_lg, workshop, planter_wet, tank;
  crop_tall stages 2–3. Each verified in a fresh `-game` boot (renders as the
  imported model on `MI_<name>`, 0 material failures, 0 sim errors).
- Of the 13 `ModelPathsV2` buildings, 11 are now code-built; **SolarArray**
  (07-17 Hunyuan paint) and **Pylon** (08-18 hero lane) are not - both liked.

**The crew on one body.** All 20 walkers are MPFB2 (MakeHuman, CC0) builds on
one topology, one 53-bone `game_engine` skeleton (UE mannequin names), one clip
set (Walk, Idle, WorkBench, WorkLab, Dig, Haul, Charge, Plant, Repair, Operate)
with planted-feet crouches, recoloured into the art-bible palette, one 2048
atlas, 16.4–22.7k tris (budget 25k). Scripts: `rh_crew_build.py`,
`rh_crew_clips.py`, `rh_crew_roster.py`; import `scripts/unreal/rh_import_crew.py`
onto `M_RH_CharacterMasked`. The face pass (`2325955`) fixed red eyes and dotted
brows, added a face normal map and lashes, retired hair over the eyes. Roster
and face sheets: `docs/media/crew-roster-20.png`, `crew-faces-20.png`. A boot
reaches population 20 with 0 material/sim/usage errors.

**Elevator, height, footprint (`9532013`).** `RHElevatorLayout.h` is the
geometry contract shared with `rh_build_elevator.py`: a 1.22 × 2.22 m clear
doorway on the camera side (tallest crew 1.88 m + a foot each way), pocket
doors that slide on ElevatorWakeAlpha, Boarding/Alighting phases, walks routed
around the cab. Doors part for riders only, with a 2 s dwell (`20807bf`).
Walkers measuring 140–220 cm render at true scale (`rh.CrewTrueHeight`,
default 1); the walk rate scales with height. ScaleFor's gray-box squeezes no
longer shrink the code-built Electrolyzer (2.07, was ~1.52) and WaterPlant.

**Audio (`20807bf`, `bb240a8`).** 26 WAVs (seeded synthesised beds and
machine loops from `scripts/audio/rh_sfx_build.py`; Kenney CC0 UI/door/thruster
sounds) and six Lyria tracks (`lyria-3-pro-preview` on Vertex, two per phase,
levelled under the beds). `URHAudioSubsystem` is presentation only (reads the
sim, never writes): wind vs habitat bed, storm layer, machine loops full within
15 m and gone by ~75 m (the 215 m overview stays quiet), elevator doors, chime,
event and UI cues, phase music with 25–70 s silences. Dials
`rh.AudioMaster/Music/Ambience/SFX/UI`. Verified with the speakers muted only.

**Interface (`48934b9`, `9a9261e`).** `RHDeckStyle` (palette, type ramp, glass
panels, 29 code-drawn icons). A vitals strip top-left; a right column of panels
(Objective with the manifest picker + LAUNCH, Earth & Neighbors with every
M3/M4 verb, Crew, Fleet with friendly names instead of IDs, Power history,
Known Ground, Comms); a drawer of build cards (thumbnail from `RH.Thumbs`, bill
at the viewed floor, power line) with a detail panel; an opening shot that eases
from ~40 m off the Lander to the 215 m register.

**The first-hour guide (`60c14a2`).** `RHCommandDeckGuide.cpp`: 19 steps in
three stages, each a predicate on sim state with a high-water mark (a loaded
save or `RH.Demo` lands on the right step); it lights the button the step names
and diagnoses the livable-floor blocker. Hidden with SKIP ALL TIPS.

**Playtesting (`0ca24d3`, `6410bf8`, `4706e5f`; `docs/qa/2026-10-15/playtest.md`).**
`RH.Playtest` plays the real game in the agent band at 16× with player orders
only (era mode parks the robots), windowless on a fixed 0.1 s step: ~2 min per
30 sols, deterministic run to run.
- **Rush order:** before the dock, structural froze at sol 3 and all 7 robots
  were dead by sol 13. After `6410bf8`: quota 1 at 4.60, first crew at 11.38,
  no robot ever flat.
- **Guide order:** quota 1 at sol 4.2 (deadline 10), floor -1 livable 7.18,
  first crew 10.55 (~3.5 h at 1×); unchanged by the dock. Browned out
  throughout (machines shed 20–78% of each sol); ~4 of 7 robots idle.
- **Crew phase:** full cargo → Hope 100 from sol 16, a birth at 32.8, LIFE ON
  MARS by 35.7. Lean cargo → Hope FLOURISHING while food and water run out,
  first warning at 75.44, all six (two Mars-born) recalled at 77.44.
- Fixed in `4706e5f`: the grow-light alert re-arms only after a whole sol fully
  lit (`GardenLastDarkSimSeconds`, runtime only; every crew number identical);
  the guide names the Soil Pallet and Seed Vault and warns with live numbers.

**Late 10-15: the P1/P2 round** (`daaf86b` → `fdffc75`).
- **Menus and the Mac app.** Title (New Colony / Continue / Settings /
  Credits / Quit), Esc pause, settings (five audio buses, window, resolution,
  quality, guide on/off). The first package failed on two unity-build name
  collisions the editor build never sees (`Txt`, `Expr`); then the archived
  app died at launch on a missing `libtbb.12.dylib` because UE 5.8's archive
  step copies the bare executable bundle. `rh_package.sh` now copies the
  complete staged app (1.5 GB) and the app boots to the title and runs a
  colony. Ad-hoc signed, this Mac only.
- **Balance sweeps** for the director's questions: Q3 + a lean cargo rescues
  the colony (4 births, pop 8); 9 solar + 3 battery barely changes the
  brownouts; halved Hope weights plateau at 83-89 THRIVING.
- **Tiered production T1-T4** and **agri Gate C**: see §2 Phase 2.
  Verification: `-staff` (25 checks) and `-dome` (15 checks) suites, the
  full battery green on each sim gate, live captures of the dome
  (`docs/media/dome-*.jpg`) and the station cards (`docs/media/station-card-*.jpg`,
  `build-drawer-habitat.jpg`).
- **Not yet hand-tested** (captures cannot drive the mouse): the dome's
  click-and-drag sizing, and a real click on a station cell.

---

## 4. What is owed

**Closed since the last snapshot** (so nobody re-does them): dropping the
`Debug_Inject*` rows from the self-tests - already landed in `163165a`
(2026-07-16), before the 08-18 doc listed it; the four buildings on the oldest
paint (`147de7a`, then code-built) and the Borer/AirFilter emissive masks
(code-built with real masks); the character redo (answered with a different
generator: the MPFB crew, `8b52734`/`2c784da`/`2325955`); the 1.8 m walker
normalisation and ScaleFor shrinking real models (`9532013`); elevator doors
(`33caddf` + `9532013`); the uncompiled `RHArtWireCommandlet` rows (`9532013`);
the first-hour softlock (`6410bf8`); the alert flood (`4706e5f`). **No compile
is pending**: the tree was clean at `4706e5f`, and every C++ commit from
`9532013` on reports a clean build.

**Still owed:**

1. **Director hand-play - the consolidated in-game verdicts.** Boot without the
   editor: `bash scripts/unreal/rh_sandbox.sh`. The 10-13 triage judged
   Blender renders of the exported shipping meshes (`db6a99a`); no commit
   records a director verdict from the running game since. Owed: the
   code-built buildings and props in context; the MPFB crew at true height (no
   in-game crew close-up has been captured - `RH.CamCrew` runs before figures
   spawn, `8b52734`); elevator boarding; the interface panels, build cards and opening shot; the guide's
   wording; and the **listening test** for the sound and score (verified only
   with the speakers muted; a 40 s preview mix was sent separately).
   `docs/qa-boot-card.md` was rewritten for this on 2026-10-17 (against
   `480bda5`): one sandbox preset per area (`rh_sandbox.sh build | crew |
   surface`, `SOUND=1` for the listening test), what to try by hand, what
   good looks like, the frames already shot, and the verdicts wanted.

2. **Director decisions from the playtest** (`docs/qa/2026-10-15/playtest.md`;
   **one-reply brief with recommendations: `docs/qa/2026-10-17/crew-phase-brief.md`**,
   D1-D6):
   - **The supply line ends at sol ~10.** Two quotas make the crew ship the
     last; a colony that missed soil and seeds cannot recover. Extend the ladder
     (Q3+), make soil obtainable on Mars (the dormant agri chain), or a slow
     recurring resupply?
   - **Hope gives no warning.** It sat at FLOURISHING (91) while food ran out
     and water went to 0% drinkable (the potability penalty caps at 12). Should
     a short food runway or stale water weigh on Hope, and should the sim raise
     a "food running low" alert (today the first alert is at zero)?
   - **Hope pins at 100 from sol 16** with a sensible layout and comforts: the
     crew phase has no tension, and LIFE ON MARS arrives ~25 sols after landing.
   - **Balance:** is quota 1 (met at sol 4.2 of 10) meant to have that slack?
     Should the starting grid carry the Forge + water chain without nightly
     blackouts? Is ~3.5 h at 1× to the first crew the intended first session?

3. **The Gate-D mental-health framing review** - the standing hard stop.
   **Every** player-facing morale/sickness/evacuation/collapse string and icon
   is a PLACEHOLDER pending the director's review under the prevention-focused,
   never-graphic directive. It now also covers the v25 EPILOGUE/COLLAPSE
   wording, the Earth/politics/colonist-support panel text (`48934b9`), the
   guide's crew-phase warnings, and one concrete case the playtest surfaced:
   "EVACUATED: <name> returned to orbit" is said of a child born on Mars.
   The full inventory is `docs/qa/2026-10-15/gate-d-wording.md` (85 strings,
   32 decisions, `daaf86b`); this round adds the lazy-fill crew moment and the
   Infirmary card line.

4. **Balance.** Every M2–M4 and v25 number is legible-math-first, awaiting
   tuning; new since August: `LanderDockRate_W` = 100 W; the Lab/Infirmary
   module and dome bills; `LazySeatFillSols` (0); `DomeMaxBedsPerSide` (3).
   Director decisions open in the session pack: Q8 lazy fill on/off, Q9 do
   modules count toward Hope's Rooms term (they don't today), Q10 the new
   placeholder numbers.

5. **Build mode: built (BM-1 to BM-6 and the BM-1b prefab, 10-16), owed a
   hand-play** (§4.1, the boot card's §1). Both 07-10 specs are also built
   (agri Gate C, tiered production). Crop activation (`RH.ActivateCrop all` or
   the CSV flag) is still a director gate flip - and one of the playtest's
   candidate answers to the supply-line problem.

6. **The game around the game.** Menus and a packaged Mac build exist
   (`daaf86b`, `e10fedc`). Still owed: distribution signing/notarization (a
   director call), and the packaged app has not been double-clicked in
   Finder by a human yet.

7. **Open art decisions carried from August** (status not settled by any later
   commit - unverified): night-lamp brightness and the 40% accent tint; Pylon
   monopole vs lattice (the 10-13 triage listed Pylon as liked, and the lineup
   exported the shipping mesh, the monopole - probably settled, confirm).

8. **The premium asset plan** (`docs/premium-asset-plan.md`, 08-14): no commit
   records the director's OK. The October kit lane and MPFB crew overlap much
   of it (scripted local-Blender finishing, one shared crew skeleton); the plan
   should be reconciled with what shipped rather than started from P0.

---

## 5. Risks & gaps (honest read)

- **Determinism is the load-bearing invariant** and it is holding: the pins
  have not moved through `4706e5f`, and both October sim changes were shaped to
  keep them (the dock lives in the agent band only; the alert re-arm is
  runtime state on the alert channel, not saved). The discipline
  (threshold-on-monotone-accumulator, linear scalars, derived-never-saved) is
  what protects it. One asymmetry to know: era mode (> 16×) integrates charging
  as if a pad existed, while the agent band now charges a pad-less fleet at the
  Lander's 100 W - the two bands already differed for a pad-less colony; the
  dock narrowed the gap rather than created it.
- **The crew phase is the design hole now.** The structure is complete, but
  the playtest shows the second half of a normal game either flatlines at Hope
  100 or fails silently 70 sols after the decision that doomed it. The guide's
  text is a mitigation, not a fix; the fixes are the director's (§4.2).
- **The opening grid is starved**: a guide-order colony sheds machines most of
  every sol and empties its bank nightly from sol 3 (§4.2, balance).
- **DT sync debt**: headless reads the CSVs, a live editor reads the DTs, and
  the self-tests fail on drift. Run `rh_sync_datatables.py` after any CSV edit.
  Last sync: `585ee8e` (DT_Config 162 rows, DT_Buildings 19).
- **Toolchain edits live outside the repo.** The UE 5.8 install's
  `Engine/Config/Apple/Apple_SDK.json` has MaxVersion 27.9.0 for Xcode 27 (the
  original is `Apple_SDK.json.bak-20261014-before-xcode27`), and the Metal
  Toolchain was installed with `xcodebuild -downloadComponent MetalToolchain`.
  An engine reinstall or Xcode change undoes either (the Metal error shows only
  in `~/Library/Logs/Unreal Engine/red_hopeEditor/`).
- **Import traps that have each bitten**: case-insensitive asset names (a
  re-bake's `T_Stockpile_*` overwrote `T_stockpile_*` twice, `147de7a`/`07841c8`;
  use `RH_TEX_TAG`); shared-folder discovery (`33caddf`, now scoped per asset);
  a crew reimport keeps the old skeleton unless its folder is deleted first.
- **Generated art has no stable UVs** - still true for what remains generated
  (SolarArray, Pylon, props and crops not rebuilt); code-built assets are
  reproducible from their scripts.
- **Visuals and sound are verified by construction, not by eye or ear**:
  captures prove 0 material failures and what is on screen; "does it read" and
  "does it sound right" are the director's (§4.1).
- **The written history has drifted.** `docs/build-log.md` and
  `docs/design-decisions.md` lagged from 07-09 to October (the decision log is
  current again from the 10-15 build-mode ruling on, and the boot card was
  rewritten 10-17). Between those dates the commit messages are the record, and the 10-13
  triage verdicts exist only in commit messages (no triage note in `docs/`).
- **No hard fail-state decision** (brief open question #5): Collapse is a
  recoverable, prevention-framed empty-colony state, not a "you lost" screen.
  The lean playtest shows it does fire in live play.
- **Demolish is instant once the order lands** (BM-3): no robot works it
  and nothing animates the teardown; the structure simply goes. Making it
  take work would need new saved state.
- **People and robots move like board pieces up close.** Robots travel in
  straight lines through buildings and each other, turn instantly and stand
  idle with no task; crew walk well (speed-matched, steer around furniture,
  ride the elevator) but never sit or lie down, socialize only through
  floating text plates, and walk through each other. This is the
  "alive pass 2" track, after build mode.
- **Scope**: a complete structure with a playable first hour, a new interface,
  sound, menus and a Mac build - but one region, legible-math balance,
  placeholder wording. Not a finished, tuned, shipped game.

---

## 6. Fast orientation for the next session

- Source of truth order: (1) the director, (2) the design brief, (3) nothing.
  History since July: `git log`. Playtest findings and open decisions:
  `docs/qa/2026-10-15/playtest.md`. Balance: `docs/data/*.csv` → DataTables.
- **Build gate:** the editor must be CLOSED for `Build.sh red_hopeEditor` - with
  a live editor the linker produces a hot-reload patch dylib only that editor
  loads, so headless boots keep running the old code. Xcode 27 builds need the
  `Apple_SDK.json` edit in §5.
- **Battery:** `bash scripts/sim/rh_battery.sh [sols]` (bash only). Judge on
  `BATTERY COMPLETE: N suite(s) not green`, never on exit codes (a benign
  port-8000 bind makes every run exit nonzero). Logs: `Saved/RHBattery/`.
- **Playtest:** `bash scripts/sim/rh_playtest.sh <sols> <label> rush|guide [full|lean]`
  → `Saved/RHPlaytest/<label>.txt` (timeline, per-sol vitals, CREW lines,
  REPORT). In a running game: `RH.Playtest [Sols] [rush|guide] [lean]`.
- **Captures:** `bash scripts/unreal/rh_capture.sh <label> "<execcmds>" [expect_n] [timeout_s] [resx] [resy]`
  - the commands must include `RH.Snapshot`; `-ExecCmds` must stay last and
  unquoted (see the script). Put `r.setres <W>x<H>w` first for frame size.
  `RH.Snapshot <delay> ui` keeps the deck in frame; `RH.DeckExpand [roster]`
  unfolds every panel; `RH.DeckBuild <Category>` opens the build drawer;
  `RH.After <seconds> <command...>` stages a later beat (all `-ExecCmds` fire
  at boot). Audio: `RH_SOUND=1` + `rh.AudioMaster 0` + `rh.AudioLog 1`.
  Judge on harvested PNGs and the material-failure count.
- **Thumbnails:** `RH.Showcase`, `RH.Thumbs <AbsOutDir>` →
  `scripts/ui/rh_thumbs_compose.py` → `scripts/unreal/rh_import_ui.py`
  (copy only the new `<Def>_color/_mask.png` pairs to a fresh folder to avoid
  reimporting every card).
- **Packaging:** `bash scripts/unreal/rh_package.sh` (editor closed, nothing
  else heavy; ~6 min). Judge on `BUILD SUCCESSFUL` and the 1.5 GB app with a
  `Contents/UE` folder. Unity collisions only show up here: name
  anonymous-namespace helpers uniquely per file.
- **Dome glass:** `scripts/unreal/rh_make_glass.py` (rerunnable) authors
  `M_DomeGlass`; it must stay used-with-instanced-static-meshes.
- **DataTables:** `RH_TABLES=Config,Buildings UnrealEditor-Cmd red_hope.uproject
  -run=pythonscript -script=$PWD/scripts/unreal/rh_sync_datatables.py
  -unattended -nosound -stdout` (omit `RH_TABLES` for all), then the battery.
  A brand-new table: add `RH_CREATE=<Name>:<RowStruct>` once its struct is
  compiled (how `DT_Furniture` was made).
- Look at the game without the editor: `bash scripts/unreal/rh_sandbox.sh`.
  `RH.Demo` builds a colony; its fleet idles until `RH.Dig Regolith_A`.
- **Art lanes.** Local Blender works (5.2.2 at `/Applications/Blender.app`):
  the hard-surface kit (`scripts/blender/rh_hs.py` + `rh_build_<name>.py` →
  `scripts/unreal/rh_import_hs.py`) and the crew (`rh_crew_build.py`,
  `rh_crew_clips.py` → `rh_import_crew.py`) need no GPU. The hero lane
  (Nano-Banana → TRELLIS.2) still runs on the A100: `filesystem=red-hope-east`
  (NOT Somnora-East); its tooling is in `scripts/gpu/`.
- **Art rules the hard way:** decimate NEVER before rigging; weld before bind;
  re-import textures whenever you re-import a mesh; regenerate every UV-derived
  artifact after a re-bake; kit assets take `AccentAmount 0`; build the front at
  -Y and call `face_camera()` (the default camera meets a model's Blender -X
  face); check for case-insensitive name collisions before importing; judge a
  generated asset on a contact sheet before it enters the project.
- **Audio:** `scripts/audio/rh_sfx_build.py` (seeded), `rh_music_gen.py` +
  `rh_music_prompts.json` (Vertex, gcloud ADC) → `rh_import_audio.py`.
- Anything on a rented box and not reproducible from this repo is one
  `terminate` away from gone; anything in a session scratchpad, one reboot.
