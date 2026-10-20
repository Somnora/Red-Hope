# Step 5, rising: plan and decisions (2026-10-20)

The director's rulings (`docs/design-decisions.md`, "The grand vision: four rulings (2026-10-18)" and after):
- no combat, ever;
- politics stays, outside and inside;
- a city of **thousands**, in **several regions**;
- Hope is derived and never spent (ruling 11);
- all morale and evacuation wording is Gate-D placeholder under the mental-health directive;
- step 5 is **rising**: surface towers gated by terraforming, a growth time-lapse, landmarks.

Steps 2-4 are answered (Q1a-Q4a each). This step builds on their hooks:
- **going deep:** depth gives shielding, and atriums (D7) already give Hope for light and space;
- **economy:** credits (E2), markets (E4) and treaties and trade blocs (E6) pay for and argue over big projects;
- **territories:** the colony record (T1), regions and settlements (T2, T4), and off-screen regions run in aggregate (T3).

This brief turns step 5 into gates and asks four questions. It is design only. Nothing here is built, and C6 is still in progress in the same tree.

Every number below is a placeholder for the director. The appendix records what the code does today, with file:line.

---

## The headline finding

There is no planet in Red Hope, only a colony on it. Mars is a set of fixed constants:
- **Radiation is a constant plus a display.** `GetRadiationAtLevel` multiplies a fixed surface index by 0.05 per floor down. Only the deck card, the cheats and the tests read it. What actually keeps the city underground is a build-cost tax: every surface building pays `SurfaceShielding_kg` of Shielding.
- **There is no atmosphere state.** The sim has no pressure, temperature or planetary radiation, and nothing that changes them. `FloorClimate` is a greenhouse setting per floor, not weather.
- **The sky has a dial that nobody turns.** The presentation's atmosphere already reads a 0..1 `Habitability` from the game state to tint the sky. The sim never sets it; it sits at 0.15, and only a console variable moves it.
- **Nothing goes up.** A building has a footprint (X, Y) and no height. Levels above 0 are refused ("No such floor"). Buildings do not rotate, though furniture does.
- **Models are one per building type,** hard-coded by name and scaled to the footprint. There is no modular kit for stacking storeys.
- **The colony has no memory.** A save is a snapshot of now. Buildings don't record when they were built. The only histories are a 72-sample power chart, the survey log and C5d's 8-sol production ring. A time-lapse has nothing to replay.
- **Landmarks have a home already:** Hope's Milestones term counts one-off events once (the vault, the first child, the first harvest, discoveries). The Views term counts windows, capped.
- **The camera stops at 3 km and at a pitch of 86 degrees.**

None of this was wrong for a vault colony. Rising needs a planet that changes, buildings with height, and a colony that remembers. So, as in steps 1-4, the first gate is invisible.

---

## The gates (each a separate commit, battery green, director look at the end of each)

| Gate | What it is | What you see |
|---|---|---|
| **R1. The planet record** | A planet record above the colonies (T1): surface pressure, mean temperature and surface radiation index, in integer fixed-point units. It is shared by every region and changes only at sol thresholds. Its defaults reproduce today exactly (radiation index 1.0, today's flare rule). `GetRadiationAtLevel` reads the planet's index instead of the constant. | Nothing changes. Every battery run stays line-identical. |
| **R2. Terraforming projects** | Long, planet-scale projects: greenhouse-gas factories, orbital mirrors, a magnetic shield, and ice-cap melting (placeholder list). They are paid in credits and material, run as project records like C5's growth (no robots, the same code in both bands), and add a fixed amount to the planet each sol. Rivals add to it too, in aggregate (T5). Politics: a shared project can be a treaty or trade-bloc clause (E6). Earth may read terraforming as investment or as independence, and that moves tension and the identity axis. | A planet panel showing pressure, temperature and radiation creeping along over hundreds of sols. |
| **R3. Mars responds** | The planet feeds back, gently. Surface radiation scales the shielding tax down. Outside pressure scales surface leak rates down. Temperature widens which greenhouse climates work outside. Floors -1 to -5 keep today's numbers. The sky's `Habitability` dial is driven from the planet record (presentation). | Surface building gets cheaper, the sky slowly shifts, and greenhouses can sit on open ground. |
| **R4. Building up** | Surface towers: a building with a height in storeys. Storeys are local to the building (they are not world Levels, so the underground Z-model is untouched). Each storey can hold homes, seats and rooms. Height classes (low, mid, high) are opened by planet thresholds; heavy shielding can build one class early at a price (Q2). A tower's lift joins step 2's lift capacity and C6's commute. Buildings gain 90-degree rotation (M0 ruling 2, never built). C5 growth can build towers in Homes districts once a class is open. | The first towers rise beside the domes. |
| **R5. Landmarks** | Unique civic projects from a short authored list (Q3), built as project records. Each counts once in Hope's Milestones term (per head under C2a's rules, never per floor), and some move the identity axis or rival relations. One of each per settlement, or one per planet, depending on the row. | Named places the colony is proud of, and that rivals notice. |
| **R6. The chronicle** | A compact, append-only history in the save: every building added or removed (Id, type, position, size, storeys, sol); a one-line summary each sol (population, Hope, planet state, per settlement); and landmark and milestone events. Buildings record the sol they were built. Nothing in the sim reads it back. | Nothing yet. The colony starts remembering. |
| **R7. The time-lapse** | A replay built from the chronicle (presentation only): scrub from sol 0 to now, watch the city spread, go down, and rise, the sky change, and population and Hope as a ribbon. It is available from the City panel and plays at the epilogue (Q4). | Your whole game in two minutes. |
| **R8. The skyline** | A modular tower kit (stacked storey modules from the Blender pipeline), instanced storeys with LODs, a taller camera range (zoom and pitch), and skyline lighting at night. Presentation only. | A city that reads from the horizon. |

**Size.** Each gate is one to three working sessions. R4 is the riskiest, because it adds a vertical dimension to placement, homes, seats and the commute. R6 is the most save-sensitive.

**Order.** R1 first. R2 then R3. R4 needs R3's thresholds. R5 can come before R4 if the director wants landmarks earlier. R6 can start any time after R1, and the earlier it lands, the longer the history a playtest has. R7 needs R6. R8 can be split across R4 and R7.

**Saves.** Strict as now. R1 to R6 each bump the version; R7 and R8 do not. Version numbers are assigned at build time, after steps 2-4.

**Regions.** The planet is one record for every region and settlement. Off-screen regions add to it through the era integrator (T3). Rivals add to it in aggregate. Nothing in it refers to the origin.

---

## Risks per gate

**General rules for every gate:**
- The planet changes only at a sol threshold on the monotone clock, so the 0.1 s agent band and the 60 s era band step it on the same step.
- Contributions are summed in a fixed order (settlements by Id, then rivals by name string) as integers.
- The planet never reads stock levels or robot positions.

**R1. The planet record.**
- *Determinism:* a pure re-sourcing of a constant.
- *Era parity:* unchanged.
- *Save:* the planet record (a few integers).
- *Bar:* the battery line-identical. The radiation lines in `-flare` and the deck read the same values.

**R2. Terraforming projects.**
- *Determinism:* a project adds a fixed amount a sol while it is funded and running. There are no curves inside a sol.
- *Era parity:* project records run in both bands at the same place (as `StepCityGrowth` does), so the bands agree exactly, not just within 5%.
- *Save:* the project list and each project's progress.
- *Bar:* no project in any existing suite, so the battery is identical. A new `-planet` suite pins a 200-sol run (placeholder) with two projects and a rival contribution.

**R3. Mars responds.**
- *Determinism:* each effect is a function of the planet record, read once a sol and cached by version.
- *Era parity:* shared code.
- *Save:* none new.
- *Bar:* at default planet values every multiplier is exactly 1.0, so the battery is identical. `-planet` checks the shielding tax and the leak at two planet states.

**R4. Building up.**
- *Determinism:* storeys are filled in a fixed order (low to high, then by Id). The lift's cost is an integer per storey.
- *Era parity:* towers are built like other buildings. Era mode already refuses while anything is under construction, and T3's aggregate build rate covers it later.
- *Save:* storeys and rotation on each building, and tower rooms and furniture keyed by (building, storey, cell). This needs D1's place-based keys, not the old spiral index.
- *Bar:* single-storey buildings with rotation 0 behave exactly as today, so the battery is identical. A new `-towers` suite covers placement, the class gate, homes and seats in a tower, the commute through its lift, and a save round trip.

**R5. Landmarks.**
- *Determinism:* counted once, by row, in row order.
- *Era parity:* the Hope term is shared code.
- *Save:* the landmark list (row, settlement, sol).
- *Bar:* the Hope pins hold while no landmark exists. Wording is Gate-D placeholder.

**R6. The chronicle.**
- *Determinism:* entries are written at the same call sites in both bands. The sol summary is written at the sol threshold. The chronicle is never read by the sim, so it cannot change any outcome.
- *Era parity:* the summaries go into the ledger pair as a check (population and Hope within the 5% bar), not as a new constraint.
- *Save size, the main risk:* about 24 bytes per building event and 32 bytes per settlement per sol (placeholders). A city of 20,000 buildings over 1,000 sols in 4 settlements is about 0.6 MB before compression. Cap it with a coarser summary after a horizon (one a sol for the last 100 sols, one every 10 sols before that), and compress the block. `-scale` gains a save-size budget.
- *Bar:* the battery is identical apart from the masked save byte count.

**R7. The time-lapse.** No sim or save change. The replay reads only the chronicle, never the live sim. Captures, and a frame-time check on the biggest playtest chronicle.

**R8. The skyline.** No sim or save change. A frame-time check with 200 towers (placeholder) on this Mac.

---

## Q1. How far does terraforming go in one game?

| Option | What it is |
|---|---|
| **a (recommended)** | **Partway.** Thicker air and less radiation, enough for towers and open-air greenhouses, but never breathable. It is honest about timescales and keeps the underground city meaningful. It gives a long arc the whole territory works on together, or argues over. |
| b | All the way to breathable air, in a long game. A grand finale, but it makes shielding, depth and sealed habitats matter less and less. |
| c | Cosmetic only: the sky changes, and nothing else does. |

## Q2. What do towers need?

| Option | What it is |
|---|---|
| **a (recommended)** | **Terraforming opens each height class, and heavy shielding lets you build one class early, at a price.** The ruling's gate holds, and a rich colony still has a choice. |
| b | A strict gate: no class until the planet reaches its threshold. |
| c | Shielding only: any height, if you can pay its Shielding bill. |

## Q3. What are landmarks?

| Option | What it is |
|---|---|
| **a (recommended)** | **A short authored list of unique civic projects:** for example a founders' atrium, an observatory, a Mars-born school, a garden of Earth plants (names are placeholder). Each counts once in Hope, and some move the identity axis or rival relations. One per settlement or one per planet, set per row. |
| b | Any building the player chooses to name as a landmark. |
| c | Achievements only, with no building. |

## Q4. When can you watch the time-lapse?

| Option | What it is |
|---|---|
| **a (recommended)** | **Any time, from the City panel, and it also plays at the epilogue.** It is how you show someone your Mars. |
| b | Only as an ending, at the epilogue. |
| c | Only as a debug and capture tool. |

---

## Appendix: what the code does today (read-only survey, 2026-10-20)

**Radiation and shielding:**
- `GetRadiationAtLevel` (`Source/RedHopeSim/Private/RHSimWorldSubsystem.cpp:2044-2051`): `RadiationSurface x RadiationPerLevelMul^depth`. "Above-surface stays at the surface index."
- `GetRadiationNow` (`:2054-2068`): a solar flare multiplies surface exposure.
- Config: `RadiationSurface`, `RadiationPerLevelMul` (`RHSimWorldSubsystem.h:2105-2111`; `docs/data/RH_Config.csv:36-37`).
- Readers: the deck card (`Source/RedHope/Private/RHCommandDeck.cpp:1495-1496`), cheats (`RHCheats.cpp:2191-2201`), and tests (`RHSimCommandlet.cpp:5734`, `:11830-11858`). No health, Hope or cost term reads it.
- The real surface cost: `GetBuildCostFor` adds `SurfaceShielding_kg` of Shielding at Level >= 0 (`Public/RHDefinitionsSubsystem.h:157-168`; the field is at `Public/Data/RHRows.h:74`).

**No planetary state:**
- No pressure, temperature or atmosphere field in the sim. `FloorClimate` is a per-floor greenhouse band (`RHSimWorldSubsystem.h:2538`).
- Weather is one global event track (`GetActiveEvent`, `RHSimWorldSubsystem.cpp:2013`).
- The sky dial: `ARHGameState::Habitability = 0.15` (`Source/RedHope/Public/RHGameState.h:17`), read by `RHAtmosphereSubsystem.cpp:83-90`, overridable with `RH.Habitability` (`:20-24`). Nothing in the sim sets it.

**Surface habitats:**
- Built surface shells rate like floors: per-hab O2 fill and leak (`O2LeakKgPerCellPerSol` per bay, `RHSimWorldSubsystem.cpp:9235`), and windows on derived walls (`RHSimWorldSubsystem.h:381-398`).
- A hab can link to the shaft (`IsNetShaftLinked`, `:2916`).

**Placement, footprints and height:**
- Footprints are X and Y only (`Public/Data/RHRows.h:40-41`); `RH_Buildings.csv` has no height column.
- Levels above 0 are refused: `CanPlaceBuildingSized` (`RHSimWorldSubsystem.cpp:6545`, the check at `:6557`).
- Furniture rotates (`RHSimWorldSubsystem.h:163-173`), buildings do not. M0 ruling 2 (`docs/design-decisions.md:20`) asked for 90-degree rotation.

**Models and the kit pipeline:**
- One imported model per building type, hard-coded by name, uniform-scaled to the footprint: `RHColonyVisualizerSubsystem.cpp:2320-2351`.
- Pressurized shells are drawn as walls and rooms instead.
- Authoring is one Blender script per building (`scripts/blender/rh_build_*.py`), plus `rh_paint_kit.py` and `rh_compose.py`. There is no stackable storey module.

**Hope's Milestones and Views** (`RHSimWorldSubsystem.cpp`):
- Milestones (`:5177-5198`): the vault, the first child and the first harvest are one-time flags, and each discovery adds its `HopeBonus` permanently.
- Views (`:5565-5570`): windows in rated surface habitats, per head, capped at `HopeViewsMax` 3 (`RH_Config.csv:170`).
- Total clamped to 0-100 (`:5624-5626`).

**Phases and endings:**
- There is no phase state in the sim. The Phase 1 exit is `bVaultRated` (`RHSimWorldSubsystem.h:2148`). The music derives its phase from tension, identity and crew (`Source/RedHope/Private/RHAudioSubsystem.cpp:234-238`).
- Endings: `GetProjectedEnding` (`RHSimWorldSubsystem.cpp:8137-8157`) reads the identity and human-nature axes, plus Collapse. Declared ending and epilogue: `RHSimWorldSubsystem.h:960-970`. Nothing about the planet.

**Era mode:**
- `CanEnterEraMode` refuses on construction, ship, maintenance and survey (`RHSimWorldSubsystem.cpp:593-640`).
- `EraStep` runs growth, trade, Earth and quota at the same places as `StepSim` (`:701-723`, `:548-552`). That is where project records already live.

**History:**
- A save is a snapshot (`RHSimWorldSubsystem.h:1639-1643`). `FRHBuildingInstance` has no built-sol field (`:120-135`).
- The only histories: `PowerHistory`, 72 samples, presentation (`RHSimWorldSubsystem.cpp:6476-6479`); `SurveyHistory`, saved (`RHSimWorldSubsystem.h:2121`); and C5d's production ring, up to 8 sols (`:2371-2376`).

**Camera:**
- Zoom from 25 m to 3 km (`Source/RedHope/Public/RHStrategyPawn.h:64-65`). Pitch clamped to -6..86 degrees (`RHStrategyPawn.cpp:55-56`).
