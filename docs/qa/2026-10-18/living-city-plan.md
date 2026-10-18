# Step 1, the living city: plan and decisions (2026-10-18)

The director's rulings (`docs/design-decisions.md`, 2026-10-18):
- no combat;
- politics stays;
- a city of **thousands**;
- **several regions**;
- start with **step 1, the living city**: roads, rover traffic, districts and a population model that scales.

This brief turns step 1 into gates and asks four questions. **Reply with the letters**, e.g. "Q1a Q2a Q3a Q4a".

The appendix records what the code does today. Three read-only surveys mapped the colonist model, logistics and movement, and space and placement. Every gate below is built on those facts.

---

## The headline finding

Today's Red Hope is a colony of about ten, and almost every system assumes it:
- **People:** there are about 8 colonists and 7 robots. Several sim paths cost N² and run up to 32 times a frame. Each colonist is a full animated figure that checks every other figure on its floor every frame.
- **Movement:** there is no pathfinding of any kind. Robots drive in straight lines through buildings and each other; the tidy detours on screen are drawn only. Fast-forward (era mode) logistics has no distance or hauler capacity at all.
- **Hope:** two terms do not scale. Each unsupported colonist costs a flat 10 points, and every filled job seat adds 1.5 points with no cap. Either one saturates the 0-100 index long before a city exists.
- **One colony:** the map, the Lander, the shaft and every resource pool are single and centred on the origin. Rivals are a distance number, and their markers sit inside your own valley.

None of this is a mistake for the game that was built. But a city of thousands cannot be added on top; the foundations go first. That is why gate 1 is invisible.

---

## The gates (each a separate commit, battery green, director look at the end of each)

| Gate | What it is | What you see |
|---|---|---|
| **C1. Foundations** | Make the sim O(N): an Id index; bed, job and skill bookkeeping updated only when something changes, not every 0.1 s; Hope adjacency cached on layout change; one alert per event instead of one per colonist; fair, proportional sharing of food, water and air in a shortage. A new `-scale` suite spawns 1,000 / 3,000 / 5,000 colonists and times a sim step against a budget. | Nothing changes: every existing battery run must stay line-identical. This is what makes everything after it possible. |
| **C2. Population at scale** | The population model (Q1). Hope re-weighted per head (employment rate, share of people supported, access to rooms) so it means the same at 10 people and 5,000. Bigger arrivals: a colonist transport, the brief's Big Arrival, and immigration that follows Hope, so a better city attracts people. People can move home. | Hope behaves at any size. Population can grow into the hundreds and thousands. The Hope-gated battery suites are re-derived, as D3a's were. |
| **C3. Crowds on screen** | Full animated people near the camera only (about the nearest 100); everyone else drawn as cheap instanced crowds. Buildings instanced by model. Labels found by a spatial lookup instead of a scan. A crew roster that scrolls and searches thousands. | A colony of thousands that still runs smoothly on this Mac; measured frame times recorded in the commit. |
| **C4. Roads** | Roads you lay down (Q3), built by robots from structural material. The sim gets its first real path network: robots and vehicles route along roads (a deterministic shortest path) and are faster on them. Buildings cannot sit on roads. Era mode learns road distance and hauler throughput, so normal and fast-forward runs still agree. | Robots stop driving through buildings; the colony gets streets. |
| **C5. Districts and growth** | Paint districts (homes, industry, farming, research, civic) on the surface and underground floors (Q2). Inside home and service districts, the colony builds housing and services on its own when there is demand and material. A demand readout shows what the city needs, in Mars terms: housing, jobs, services, air. Districts get names, and are built so they can later have interests and a political voice (the politics ruling). | The city starts building itself where you zoned it. |
| **C6. Traffic** | Cargo rovers: heavy, fast vehicles that run on roads and carry freight between districts. Commuting (Q4): people travel home to work as flows between districts; long commutes cost Hope and work time. The streets show those flows as rovers and shuttles. A traffic overlay. | Streets with rovers constantly moving from one area to another: the pulse of the city you asked for. |

**Size.** Each gate is roughly one to three working sessions. C1 and C4 are the riskiest, because they touch the deterministic core. C2 moves balance baselines on purpose; those moves will be listed in the commit.

**Saves.** The save format is strict and every gate changes it. With no players yet, I will keep refusing old saves and re-making the hand-play checkpoints automatically (they already re-make themselves when code or data change). A migration layer can come before any public build.

**Regions.** Several regions are step 4, but nothing in step 1 should make them harder. New data (roads, districts) carries a region id from the start, and nothing new is pinned to the origin.

---

## Q1. Who are the thousands?

| Option | What it is |
|---|---|
| **a (recommended)** | **Everyone stays a named individual in the sim.** The surveys show the cost today is algorithmic (N² bookkeeping, full figures for everyone), not the people themselves. A colonist record is small, and thousands of them update cheaply once C1 is done. The screen shows the nearest people in full and the rest as crowds. The panels surface the notable people: the first crew, the Mars-born, the unsupported. Every person still matters, which suits Hope and the mental-health framing. |
| b | A named core crew (about the first hundred), with everyone after that as anonymous households counted by district. Cheaper, but the city stops being made of people. |

## Q2. How does the city grow?

| Option | What it is |
|---|---|
| **a (recommended)** | **A hybrid.** You still place production, power, life support and anything special, which is the Factorio-like half of the game. You zone districts, and inside home and service districts the colony builds housing and services itself, from the material pool, when there is demand. This is SimCity's key ingredient without losing the hands-on building that already works. |
| b | Full SimCity: zone everything, and the colony builds it all. |
| c | Keep placing every building by hand (impractical at thousands). |

## Q3. Roads

| Option | What it is |
|---|---|
| **a (recommended)** | **You draw roads.** Robots and vehicles are faster on them, and heavy cargo rovers need them. A short starter road from the Lander shows the idea. |
| b | Roads form on their own along busy routes, and you upgrade or remove them. |

## Q4. Commuting and traffic

| Option | What it is |
|---|---|
| **a (recommended)** | **Commuting is simulated as flows between districts.** The sim knows how many people travel from each home district to each work district, and how far along roads and tunnels. A long commute costs Hope and work time, so where you put homes and jobs matters. The rovers and shuttles on the streets are drawn from those flows. Freight rovers are real vehicles. |
| b | Every person's trip is simulated as a vehicle: real congestion, but far heavier, and harder to keep deterministic. |
| c | Traffic is decoration only. |

---

## Appendix: what the code does today (surveys of 2026-10-18)

**Colonists** (`Source/RedHopeSim`):
- **The record.** `FRHColonist` is small: Id, Name, HomeLevel, HomeHab, bSupported, UnsupportedSimSeconds, and skill sols. It is stored in a flat array with no Id index.
  - Jobs, beds and seats are re-derived every step.
  - Homes are set once at arrival and never change.
  - There is no death or sickness; the only exit is evacuation.
- **The N² hot spots**, all run every 0.1 s step and up to 32 times a frame:
  - `BuildBedAssignments` does a find-by-Id inside the loop.
  - `GetCrewSkillMul` runs inside the planted-cell loop.
  - Furnished seats are looked up linearly.
  - Arrivals cost O(K·N): `Debug_AddColonists` recounts residents per floor and per habitat.
- **Shortages** are taken all-or-nothing per colonist, in array order, so whoever is last in the array goes without.
- **Alerts:** one alert and one log line per colonist when support is lost or someone is evacuated.
- **Hope** (`GetColonyHope`):
  - Housing and Comforts are fractions and scale fine.
  - Unsupported (-10 each) and Jobs (+1.5 per seat, uncapped) do not.
  - The Rooms bonus repeats per floor or habitat network.
  - Room adjacency costs O(C²-C³) per floor, with string splitting. It runs every step and every frame from the deck.
- **Growth:**
  - births need smoothed Hope ≥ 75 and come one per 20 sols;
  - pods carry 4 colonists;
  - the Big Arrival does not exist in code.
- **Era mode** runs the same colonist code as the agent band (linear draws, threshold accumulators, the exp smoother). No parity test covers more than about 10 colonists.

**Presentation of people** (`RHCrewVisualizerSubsystem`):
- One actor per colonist: a skeletal walker plus name and emote text.
- Every colonist runs full logic every frame, even when hidden.
- Crowd avoidance is O(N²) per floor per frame.
- The crew roster is an unvirtualized list.
- Robots already use Mass plus instanced meshes; colonists use neither.

**Logistics and movement:**
- **Movement.** The only movement is `RH::MoveToward`: a straight 2D line that arrives within 3 m. There is no pathfinding, navmesh, graph or avoidance in `Source`.
  - The decision log's "navmesh" was never built.
  - MassAI is enabled but unused.
- **The task board.** `StepTaskBoard` runs every step and costs O(B²·recipes + (B+D)·T). Idle robots retry their claims every step, at O(R·T·B).
- **Robots underground.** Robots never go below the surface; the shaft head is the single hand-off point.
- **Presentation detours.** `RHAgentVisualizerSubsystem` plans detours around building footprints for drawing only. The drawn figure follows the sim's progress fraction.
- **Era logistics** teleports material: no distance, travel time or hauler capacity. The 5% ledger parity bar between normal and fast-forward runs is the constraint roads must respect.
- **The convoy** is a scalar timer with one rover at a time. Rivals have no position.
- **Determinism risks for roads and traffic:**
  - frame-batched stepping: each robot runs all its sub-steps before the next;
  - float distance ties fall to array order;
  - claims and reservations are not saved;
  - a halted robot would block a lane.

**Space and placement:**
- **The map.** One map is centred on the origin: a 150 m flat at z=0, relief out to 4.6 km, and the map's MarsGround pad, a 5 km plane at z=0.
  - Nothing bounds the playable area.
  - The sim never reads the terrain.
- **Placement.**
  - The grid is 2 m, hard-coded (`GridSnapCm`). Buildings do not rotate.
  - Overlap is tested against every building.
  - Coverage (Power as Territory) scans every building. Power is one colony-wide pool with no connectivity.
- **Underground.** There is one shaft (one head, one depth, at most 5 floors of 4 m). Cells are 10 m, laid out in a spiral around the head. A floor is a carved-cell count, not a shape.
- **Presentation of buildings.**
  - One actor per building, with many components; labels, lights and audio are per building.
  - The per-frame passes build text keys.
  - The hover-label scan touches every actor.
  - The territory disc is debug-drawn, so it would vanish in a Shipping build.
- **Single-colony anchors:**
  - the Lander at the origin, which is also the store, emergency dock and fallback;
  - one resource pool, one power state, one quota, one colonist list;
  - furniture and station keys overload their third field for "underground or habitat id", with no room for a region or district.
