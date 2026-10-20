# Step 4, territories: plan and decisions (2026-10-20)

The director's rulings (`docs/design-decisions.md`, "The grand vision: four rulings (2026-10-18)"):
- **several regions:** rival settlements get their own territories on a regional map, not one shared valley;
- **no combat, ever:**
  - defence is shielding, shelters, sealed compartments and protected convoys;
  - offence is leverage: embargoes, covert operations, competing for colonists through Hope;
- **politics stays,** outside and inside;
- a city of **thousands**;
- step 4 is **territories**: the regional map, visible rival cities and convoys.

Steps 2 and 3 are answered (Q1a-Q4a each) and leave hooks this step uses instead of re-inventing:
- RegionIds on shafts, floors, tunnels and caverns (going deep);
- E7's trade depots, routes with two depots, and per-region endowment tables (economy);
- E4's rival stores and production, and E6's treaties and trade blocs.

C6's cargo rovers (`docs/qa/2026-10-20/c6-traffic-design.md`) are the vehicles that will drive the convoys.

This brief turns step 4 into gates and asks four questions. **Answered 2026-10-20: Q1a Q2a Q3a Q4a** (all four recommendations). It is design only. Nothing here is built, and C6 is still in progress in the same tree.

Every number below is a placeholder for the director. The appendix records what the code does today, with file:line.

---

## The headline finding

Red Hope is one colony in one world, and the engine object *is* the colony:
- **The sim is a world subsystem.** `URHSimWorldSubsystem` exists once per loaded level, and it owns every piece of colony state directly: one building list, one deposit list, one stock pool, one power state, one colonist list, one quota, one road graph, one convoy. There is no "colony" record that could be copied. A second settlement has nowhere to live.
- **Everything is measured from the origin.**
  - The Lander is placed at (0, 0). It is the store, the haul sink, the salvage target, the emergency dock and the fallback for lost cargo.
  - The buildable flat is 150 m around the origin. Relief opens out to 4.6 km, and the authored MarsGround pad is a 5 km plane.
  - Deposits are positioned in metres from the origin, and some are duplicated as constants in the terrain code. Robots spawn at a fixed (30 m, -30 m).
- **Rivals have no place.** A rival is a name, a distance in km and a relation score. Its marker is put 200-320 m out on a bearing hashed from its name, inside your own valley. The convoy is drawn as a straight lerp from the Lander to that marker.
- **One clock, one weather.** One sol clock and one event track serve everything. A dust storm is everywhere at once.
- **Fast-forward is all or nothing.** Era mode is refused colony-wide while anything is being built, a ship is inbound, a maintenance call is running or a survey is out.
- **RegionId exists in only two places,** road edges and districts, and it is always 0.

The good news: the era band is already a tested aggregate model of a colony. It moves material without robots or travel, and it holds a 5% ledger-parity bar against the full sim. That is exactly what an off-screen region needs. So, as in steps 1-3, the first gate is invisible: pull the colony out of the subsystem.

---

## The gates (each a separate commit, battery green, director look at the end of each)

| Gate | What it is | What you see |
|---|---|---|
| **T1. The colony record** | Move every piece of per-colony state out of `URHSimWorldSubsystem` into an `FRHColony` record. The subsystem holds a list with one colony, and every getter forwards to it. Robots record their colony in `FRHRobotFragment` (no new Mass fragment, so the explicit StateTree archetype is untouched). The Lander's roles move to "this colony's depot" (E7). | Nothing changes. Every battery run stays line-identical. |
| **T2. Regions and the regional map** | A region record: Id, name, position on a regional map (km), size, terrain seed, endowment table (E7), and owner (you, a rival, or nobody). Data in a new `RH_Regions.csv`. Each region has its own local frame, so (0, 0) means the region's own anchor, never the world's. Rivals get a home region. Terrain and deposits are generated or authored per region; nothing is hard-coded in the terrain code any more. | A Mars map with named regions, your colony in one of them, and the rivals placed in theirs. |
| **T3. Off-screen regions** | Only the region you are looking at runs the full sim (robots, 0.1 s steps). Every other region runs the era integrator at 60 s steps (Q1). A region hands over between the two bands at a sol boundary. Era mode becomes per region, and the refusals (construction, survey, and so on) learn to finish in aggregate instead of refusing. | Your other settlements keep living while you look elsewhere. |
| **T4. Founding a settlement** | You found a new settlement in a free region (Q2): a Founding Kit on the manifest, or a convoy of settlers and cargo rovers. It brings its own depot. Each settlement has its own stores, power, colonists and Hope. Earth's quota, credits and the identity axis stay colony-wide. Settlements become inside-politics actors, as districts already are. | A second city across the map, with its own needs. |
| **T5. Rival cities** | Rivals become settlements on the map with E4's stores and production, always in aggregate. They grow, and they compete with you for colonists through Hope: a settler goes where Hope is highest, within reach. Their markers move from your valley to their regions. | Rivals are places that grow and change, not just names. |
| **T6. Convoys as journeys** | Routes link depots on the map: a length in km and a terrain class. Several convoys can be out at once, each driven by real cargo rovers. Risk without combat (Q4): weather is now per region, so a storm on the route halts the convoy; breakdowns are a content-seeded wear roll; covert operations can delay or skim a convoy, and a caught operation costs relation. Protection is escorts, shielded cargo and waystation shelters. | Convoys you can watch crossing the map, and a reason to guard them. |
| **T7. Claims and borders** | Territory is held by presence, never force (Q3). A settlement and its power coverage (Power-as-Territory already exists) claim the ground they cover. Treaties (E6) draw borders between claims. A contested site is settled by treaty, arbitration or by where the colonists choose to live. | Borders on the map that move with settlement and diplomacy. |
| **T8. The regional view** | A regional map screen; travel between regions (a camera cut plus a scene load); convoy lines, borders, depots, and region and rival cards. Presentation only. | You can run a territory from one screen and drop into any city. |

**Size.** Each gate is one to three working sessions. T1 is the largest and riskiest refactor in the project so far: it touches every saved field. T3 is the second riskiest, because it makes the era band carry real colonies for long stretches.

**Order.** T1, then T2, then T3. T4 and T5 can swap. T6 needs T2's routes and C6's rovers. T7 needs E6's treaties. T8 can be split across the others.

**Saves.** Strict as now. T1 to T7 each bump the version; T8 does not. Version numbers are assigned at build time, after steps 2 and 3. One save file still holds the whole territory.

**Performance.** Thousands of people per settlement times several regions is too much to run in full. Off-screen regions cost one era step a minute of sim time. The `-scale` suite gains a budget for 8 regions (placeholder): one in full, seven in aggregate.

---

## Risks per gate

**General rules for every gate:**
- Regions step in RegionId order, every step.
- Regions touch each other only at sol thresholds: convoy arrivals, market repricing (E4), settler choice. So a band difference inside one region cannot leak into another within a sol.
- Every roll is content-seeded with the region's name, never an FName index (the covert-seed bug of 2026-07).
- Positions are local to a region. Map distances are integer metres.

**T1. The colony record.**
- *Determinism:* a pure move. Field order and iteration order stay the same.
- *Era parity:* unchanged.
- *Save:* written as a list of one colony. Every existing field moves inside it, and the round-trip test covers every suite's save.
- *Bar:* the whole battery line-identical, masked only for the save byte count.

**T2. Regions and the regional map.**
- *Determinism:* terrain and deposits come from data and a per-region seed.
- *Era parity:* none new.
- *Save:* the region list and ownership.
- *Bar:* region 1 reproduces today's map exactly: same deposits, same flat, same seeds. So the battery stays identical. A new `-regions` suite builds two regions.

**T3. Off-screen regions.**
- *Determinism:* the hand-off at a sol boundary is the risk. Robots in a region going off screen are parked as era mode parks them, and come back where they stood.
- *Era parity:* this is the big one. A colony that spends most of its life in aggregate must match the full sim within the 5% bar over many sols, not just 10. Extend the paired runs to 50 sols (placeholder) and to a colony with construction under way. Today era refuses construction; T3 has to give it an aggregate build rate and prove it.
- *Save:* each region's band and its last hand-off sol.
- *Bar:* with one region, nothing changes. `-regions` pins a two-region run, both ways round.

**T4. Founding a settlement.**
- *Determinism:* settlements are ordered by Id. Colony-wide terms (quota, credits, the identity axis) are summed in that order.
- *Era parity:* founding is a single event, the same in both bands.
- *Save:* the settlement list. Each settlement is a T1 colony record.
- *Bar:* single-settlement suites stay identical. Hope stays per settlement, so the C2 per-head pins hold.

**T5. Rival cities.**
- *Determinism:* rival growth is integer and per sol. Settler choice compares Hope at the sol threshold, ties by Id.
- *Era parity:* rivals are always aggregate, so the band never matters for them.
- *Save:* rival stores, production and population.
- *Bar:* immigration suites stay identical while no rival is within reach. The covert, embargo and Dilemma suites keep their exact sequences.

**T6. Convoys as journeys.**
- *Determinism:* progress is a monotone accumulator per convoy, as today. Breakdowns are content-seeded per convoy and leg.
- *Era parity:* convoys already run in both bands (`StepTrade`).
- *Save:* the convoy list, replacing today's single convoy fields.
- *Bar:* the Zarya route with one convoy reproduces today's timing exactly.

**T7. Claims and borders.**
- *Determinism:* claims are recomputed on a layout change and cached by version. Overlaps are resolved by treaty order, then Id.
- *Era parity:* shared code.
- *Save:* treaties are already saved (E6). Claims are derived.
- *Bar:* `-regions` covers a contested site resolved by treaty.

**T8. The regional view.** No sim or save change. Captures, and a frame-time check while switching regions.

---

## Q1. What happens to the regions you are not looking at?

| Option | What it is |
|---|---|
| **a (recommended)** | **They run in aggregate.** The region on screen runs in full. Every other region runs the era integrator, which already holds the 5% parity bar. That makes a territory of thousands affordable. |
| b | Everything runs in full, always. Faithful, but the cost grows with every region, and fast-forward gets slower as you expand. |
| c | They freeze while you are away. Cheap, but a territory that stops when you look away is not alive. |

## Q2. How many cities can you have?

| Option | What it is |
|---|---|
| **a (recommended)** | **Several settlements, each a real city,** with one capital that deals with Earth. Each has its own stores, power, people and Hope. They trade by convoy, and they have their own interests in inside politics. |
| b | One city, plus remote outposts (mines, ice wells, waystations) that only feed it. Simpler, but the territory never holds a second community. |
| c | One city only. Regions are for rivals. |

## Q3. How is territory held?

| Option | What it is |
|---|---|
| **a (recommended)** | **By presence and by treaty.** What your settlements cover is yours. Treaties draw the borders between claims, and a contested site goes to treaty or arbitration. Nobody takes land by force. |
| b | First come, first served: a whole region belongs to whoever founds in it first. |
| c | Claims are bought and sold on the market. |

## Q4. What can go wrong on a convoy?

| Option | What it is |
|---|---|
| **a (recommended)** | **Weather, breakdowns and covert interference.** A storm on the route halts the convoy. Wear can strand a rover until a repair arrives. A rival's covert operation can delay or skim the load, and if it is caught, that costs the rival. You protect convoys with escorts, shielded cargo and waystation shelters. No raids, no attacks. |
| b | Weather only. Convoys are safe from people, and covert operations stay at settlements. |

---

## Appendix: what the code does today (read-only survey, 2026-10-20)

**One map, one world:**
- One level: `L_Slice` is both the startup and the default map (`Config/DefaultEngine.ini:5-6`). There is no World Partition or level-streaming setup.
- Terrain is generated around the origin (`Source/RedHope/Private/RHMarsTerrain.cpp`): flat for r < 150 m, opening by 350 m, plains rising toward 1.5-4.3 km, all with fixed noise seeds (`:305-327`). The ground fan runs from 40 m to 4.6 km (`:260`). It dips under the authored pad (`:246-250`).
- Deposit positions are copied as constants into the terrain's haul corridors and called "battery-pinned" (`:436-448`). The source data is `docs/data/RH_Deposits.csv` (`LocX_m`, `LocY_m` from the origin).
- The MarsGround pad is a 5 km plane found by name: `RHColonyVisualizerSubsystem.cpp:1766`, `:4458-4490`, `:4524-4541`.
- The camera zooms from 25 m to 3 km (`RHStrategyPawn.h:64-65`). There is no pan bound, and no map-scale view.

**The sim is the colony** (`Source/RedHopeSim/Public/RHSimWorldSubsystem.h`):
- `URHSimWorldSubsystem` is a `UTickableWorldSubsystem` (`:240`): one per world.
- Its single instances: `RoadGraph` `:1830`, `Districts` `:1959`, `Stocks` `:2065`, `Buildings` `:2068`, `Deposits` `:2069`, `Power` `:2075`, `Colonists` `:2270`, `FleetCounts` `:2631`, the quota (`QuotaPhase`, `ManifestItems`, `ActiveQuota` `:2636-2643`), and the convoy `:2398-2409`.
- One clock: `URHSimClockSubsystem` (`RHSimClockSubsystem.h:18`), with a 0.1 s sub-step, 60 s era step and 1,200 s sol (`:23-33`).
- One event track: `GetActiveEvent` (`RHSimWorldSubsystem.cpp:2013`). A dust storm freezes the one convoy in `StepTrade` (`:7443`).

**The Lander at the origin** (`Source/RedHopeSim/Private/RHSimWorldSubsystem.cpp`):
- Placed at `FVector::ZeroVector` (`:410`). It cannot be demolished: "the colony's anchor" (`:8395`).
- It is a store and haul sink, alongside stockpiles: `:813`, `:830`, `:866`, `:994`, `:1027`, `:1080`, `:1128`.
- It receives salvage (`:8620`), Earth cargo (`CreditLander` `:9261`), trade goods (`:7492`) and covert loot (`:7657`). It is the emergency dock (`:10548-10556`) and the save-time home for cargo whose source was demolished (`:11016`).
- Robots from the manifest spawn at a fixed (30 m, -30 m) (`:1894`). Cargo rovers deploy at the node nearest the Lander (`RHSimRovers.cpp:68`).

**Rivals and the convoy:**
- `docs/data/RH_Rivals.csv` holds name, nation, `DistanceKm`, fixed lots and a starting relation. There is no position or region.
- Discovery is a content-seeded roll per survey (`RHSimWorldSubsystem.cpp:10725-10745`). Leg time is `DistanceKm / ConvoySpeedKmPerSol` (`:7436`).
- The marker is placed in presentation only: a name-hashed bearing, 200 m + 0.3 m per km, "all inside the sand skirt" (`RHColonyVisualizerSubsystem.cpp:3985-3995`).
- The convoy is drawn as a straight lerp from the Lander to that marker (`:4118-4135`).

**RegionIds that exist:**
- `FRHRoadEdge::RegionId` (`RHRoads.h:30`) and `FRHDistrict::RegionId` (`RHDistricts.h:51`). They are set to 0 (`RHSimDistricts.cpp:549`) and saved (`RHSimWorldSubsystem.cpp:11896`, `:11921`).
- Nothing else carries a region. The plans add them to shafts, floors, tunnels, caverns (going deep), depots, routes, markets and rivals (economy E7).

**Territory today:**
- Power as Territory: `IsInCoverage` (`RHSimWorldSubsystem.cpp:10128`) scans every building's coverage radius. It is the only notion of "ours", and it is what placement checks (`:6714-6746`).

**Era mode:**
- `CanEnterEraMode` (`:593-640`) refuses colony-wide: any construction, any road edge being built, a ship inbound, a maintenance call, a survey.
- `EraStep` (`:701-723`) runs the same growth, trade, Earth and quota steps as `StepSim` (`:548-552`), plus `EraLogistics` (`:731`), which moves dug material to hoppers with no robots or travel.

**Saves:**
- One file per colony, strict, v40 (`:34`). `SaveColony` (`:10958`), autosave (`:480`), quicksave (`RHPlayerController.cpp:1959`).
- Every field is written at the top level of the subsystem, so there is no per-colony block to repeat.
