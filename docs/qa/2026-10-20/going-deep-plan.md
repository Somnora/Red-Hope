# Step 2, going deep: plan and decisions (2026-10-20)

The director's rulings (`docs/design-decisions.md`, "The grand vision: four rulings (2026-10-18)"):
- no combat, ever;
- politics stays, and grows with the city;
- a city of **thousands**;
- **several regions** later (step 4);
- step 2 is **going deep**: tunnels, transit, cavern districts, atriums.

This brief turns step 2 into gates and asks four questions. **Answered 2026-10-20: Q1a Q2a Q3a Q4a** (all four recommendations): robots ride the lifts and work on any floor; you paint the shapes the Borer carves; deeper is slower to dig and warmer but better shielded, with floors -1 to -5 unchanged; lifts and trams have capacity, counted from the flows. It is design only. Nothing here is built, and C6 (traffic) is still in progress in the same tree; step 2 starts after C6b.

Every number below is a placeholder for the director. The appendix records what the code does today, with file:line. Line numbers in `RHSimCommute.cpp` will move: C6a is being written there now.

---

## The headline finding

The underground is one hole with rooms spiralled around it. Almost every underground system assumes that:
- **One shaft.** There is one `ShaftHeadCm` and one `ShaftDepth`. The head is fixed by the first bore and never moves. "Is this floor reachable?" is just "is it above the shaft's depth".
- **A floor is a number, not a place.** A floor is a carved-cell count. Cell *i* sits at `ShaftHead + SpiralCell(i) x 10 m`. So a cell has no position of its own; it is an index into a spiral around the one head. A second shaft has nowhere to put its cells.
- **Five floors, hard-wired as loops.** `MaxDepth` is data (5), but many systems walk `for Level = -1 .. -MaxDepth`: air, beds, seats, districts, the view.
- **Nobody goes down.** Robots always plan as if on the surface (`RobotLevel 0`); the lift at the shaft head carries cargo instantly. Roads and rovers are level-0 only.
- **The lift is free and endless.** A commute between floors is `|floors| x 60 m`, through the one shaft head, with no capacity or wait.
- **An underground district is a whole floor,** because a floor has no shape (C5 said so and deferred shapes to this step).
- **The view is one floor at a time** around one head; the cutaway CVar only opens surface habitats.

None of this was a mistake for a vault of a few dozen. A deep city of thousands needs floors that are shapes, many shafts, and people and cargo that travel underground. So, as in step 1, the first gate is invisible.

---

## The gates (each a separate commit, battery green, director look at the end of each)

| Gate | What it is | What you see |
|---|---|---|
| **D1. Floors become places** | Each floor stores its carved cells as a set of absolute 10 m lattice points (with a RegionId), not a count around the head. `SpiralCell` stays only as the Borer's default carve order, so today's floors carve the same cells in the same order. Every `-1 .. -MaxDepth` loop becomes "the floors that exist, shallowest first". Room, furniture and seat keys move from spiral index to lattice point. | Nothing. Every battery run stays line-identical. Save v41. |
| **D2. More shafts** | A list of shafts, each with its own head, depth, lift and RegionId. You order a new shaft where you like (inside coverage). A floor's cells are reached when a shaft that reaches that floor touches them. Shaft 0 is today's shaft. `GetApproachPoint` picks the cheapest shaft that serves both ends. | You can sink a second shaft across the valley and dig a separate floor under it. |
| **D3. Tunnels** | An underground road graph per floor, built like C4 roads but carved (Borer work, spoil out). Lift shafts are vertical edges that join each floor's graph and the C4 surface graph into one network. Robots ride lifts and work below (Q1). The commute uses the same graph. | Robots and rovers go down; separate digs join up into one city. |
| **D4. Transit: lifts and trams** | Each lift has a capacity a sol. Commute flows through a lift add a wait that grows with its load (Q4). Trams are a vehicle on long tunnel edges: a capacity and a speed, no individual cars simulated. Everything stays aggregate and integer, like C6's flows. | A crowded lift costs commute time; a second shaft or a tram line fixes it. |
| **D5. Depth has a price** | Deeper floors cost more and give more. Digging slows with depth; rock warms with depth, so floors need cooling power; shielding keeps improving. `MaxDepth` rises (placeholder 5 -> 20) (Q3). | A real choice between shallow and cheap, or deep and safe. |
| **D6. Caverns and underground districts** | A cavern is a big carved volume: a lattice shape several floors tall, dug as one project. Underground districts become shapes on a floor (a lattice rectangle, like the surface), so one floor can hold homes and industry. Growth (C5) can build homes and services inside a zoned cavern. | Large underground neighbourhoods that the city fills on its own. |
| **D7. Atriums** | A multi-floor open volume (a cavern with no floors between). Homes and seats facing it get a Hope bonus for light and space; the open air volume costs more to fill and leaks more. Counted once per atrium, not per floor. | The showpieces of the deep city: tall lit halls you can look down into. |
| **D8. The deep view** | A section (cross-cut) view of all floors under any shaft, a depth slider, a cutaway that opens caverns and atriums, and the tunnel and lift traffic overlay. Presentation only. | You can read the whole deep city at once. |

**Size.** Each gate is one to three working sessions. D1 and D3 are the riskiest: D1 re-keys saved state the whole underground depends on; D3 changes where robots are and how they move.

**Order.** D1 must come first; everything else stands on it. D2 before D3 (tunnels join shafts). D4 needs D3's graph and C6's commute. D5, D6 and D7 can swap order. D8 can be split across the others if the director wants to see each gate rather than read the battery.

**Saves.** Strict as now. Every gate except D8 bumps the version (v41 onward). Old saves are refused, and the hand-play checkpoints re-make themselves.

**Regions.** Shafts, floors, tunnels and caverns all carry a RegionId from the start. Cells are absolute lattice points, never offsets from one head, and nothing new refers to the origin or the Lander. A second region will simply be a second set of shafts.

---

## Risks per gate

**D1. Floors become places.**
- *Determinism:* the cell set must be iterated in carve order (an array), never as a hash set. The adjacency walk (`RHSimWorldSubsystem.cpp:5295`) and room tallies must see cells in today's order.
- *Era parity:* none if it is a pure re-keying; the ledger pairs must not move.
- *Save:* the big one. Room cells, furniture keys and station keys change their meaning (spiral index -> lattice point). Write and read both in one change, with a round-trip test of a full vault.
- *Bar:* the whole battery line-identical, plus a new `-deep` suite: a floor carved, saved, loaded and re-carved gives the same cells.

**D2. More shafts.**
- *Determinism:* ties between shafts break by shaft Id. `AreLevelsLinked` becomes "linked through which shaft"; pick the lowest-cost, then the lowest Id.
- *Era parity:* era logistics teleports material, so era ignores which shaft. Log that as a deviation, as C4 did for roads.
- *Save:* the shaft list, plus each Borer's work record naming its shaft.
- *Bar:* battery line-identical (shaft 0 behaves exactly as today); `-deep` adds two shafts, two separate floors, and a hab linked to the nearer shaft.

**D3. Tunnels.**
- *Determinism:* the joined graph uses C4's integer Dijkstra and tie rules unchanged; lift edges have integer costs. Robots changing level do so at a fixed sub-step point.
- *Era parity:* era still teleports; the ledger bar stays 5% over 10 sols. Robots underground must not change the agent band's totals beyond that. Refuse era mode while a tunnel is being carved (like roads).
- *Save:* the tunnel edges (in the road graph, with a Level), and robot Level (already saved).
- *Bar:* battery identical while no tunnels exist and no robot goes below; `-deep` adds a robot hauling to floor -2 through a tunnel, and a commute across two shafts.

**D4. Transit.**
- *Determinism:* lift load is a function of the commute flows only (sim state), never of robot positions. Integer maths throughout.
- *Era parity:* both bands run the same flows, as in C6.
- *Save:* tram lines and lift upgrades. Loads and waits are derived.
- *Bar:* the C6 battery facts hold (crew-sized colonies never feel commuting: the ramp from 60 to 160 people stays).

**D5. Depth has a price.**
- *Determinism:* per-floor rates from data, no random terms.
- *Era parity:* cooling power and dig time run in both bands through the same step code.
- *Save:* none new beyond D1's floors; the costs are derived from depth.
- *Bar:* floors -1 to -5 keep today's numbers exactly (the new costs start below -5, placeholder), so the battery stays identical.

**D6. Caverns and districts.**
- *Determinism:* district ownership by lattice point, ties by district Id, as on the surface.
- *Era parity:* growth projects already run in both bands; caverns use the same project code.
- *Save:* cavern shapes; districts gain an underground lattice (the field exists for the surface already).
- *Bar:* a whole-floor district must still work and give the same result, so the C5 suites stay identical.

**D7. Atriums.**
- *Determinism:* "faces the atrium" is a lattice test, done once on layout change and cached by version.
- *Era parity:* the Hope term is shared code.
- *Save:* the atrium is a cavern flag; no new state.
- *Bar:* Hope moves only in a new `-deep` case; the per-head Hope pins hold.

**D8. The deep view.** No sim change, no save change. The bar is captures plus a frame-time check with 20 floors and three shafts.

---

## Q1. Do robots go underground?

| Option | What it is |
|---|---|
| **a (recommended)** | **Robots ride the lifts and work on any floor.** Haulers deliver to the room that needs it, Fabricators build where the site is. A lift has a cargo capacity. This makes tunnels and lifts matter for freight, not just people. It is the riskiest part of D3. |
| b | Keep today's hand-off: robots stop at a shaft head and the lift moves cargo down instantly. Simpler and safe, but tunnels then only carry people. |

## Q2. How do you dig?

| Option | What it is |
|---|---|
| **a (recommended)** | **You paint the shape** on a floor's lattice (cells, a tunnel line, a cavern box), and the Borer carves it in order. Brushes for "tunnel", "cavern" and "atrium" make the big shapes one drag. |
| b | Keep the spiral: each shaft grows its floors outward on its own; you choose only how many cells. Fast, but every floor looks the same. |

## Q3. What does depth cost?

| Option | What it is |
|---|---|
| **a (recommended)** | **Deeper is slower to dig and warmer,** so it needs cooling power, but it is better shielded and does not touch the surface. Floors -1 to -5 keep today's numbers. |
| b | Depth costs only dig time; no heat. |
| c | No depth cost; just a higher `MaxDepth`. |

## Q4. Lifts and trams

| Option | What it is |
|---|---|
| **a (recommended)** | **Lifts and trams have capacity.** A crowded lift adds a wait to every commute through it, counted from the flows. A second shaft or a tram line fixes it. No car is simulated one by one. |
| b | Lifts stay endless with a fixed cost per floor (today's 60 m a floor). Tunnels are the only underground choice. |

---

## Appendix: what the code does today (read-only survey, 2026-10-20)

**One shaft, one head** (`Source/RedHopeSim`):
- State: `FloorHeightCm` 400, `MaxDepth` 5, `ShaftDepth`, `ShaftHeadCm`, `FloorCarvedCells` (Level -> count) at `Public/RHSimWorldSubsystem.h:2127-2132`. Data rows: `docs/data/RH_Config.csv:34-35` (FloorHeightMeters, MaxDepth), `:39-41` (spoil and O2 per 10 m cell).
- Reachability is depth only: `IsLevelConnected` and `AreLevelsLinked` at `RHSimWorldSubsystem.h:431-434` ("the lift is the only cross-level connector").
- The head is fixed by the first bore: `RHSimWorldSubsystem.cpp:1346` (Borer batch) and `:6336` (`ExtendShaft`, clamped to `MaxDepth` at `:6329`). Orders clamp to `MaxDepth` at `:7015` and `:7029`; `:6557` refuses levels outside it.
- Hab-to-shaft link: `IsNetShaftLinked` (`:2916-2930`) measures every bay to the one head and needs floor -1 rated.

**A floor is a count in a spiral:**
- `SpiralCell(Index)` (`RHSimWorldSubsystem.cpp:4590`) maps an index to an offset from the head, skipping the shaft's own cell. Declared at `RHSimWorldSubsystem.h:740`.
- Its inverse `FindCarvedCellAt` (`:5093-5110`) scans the first N spiral cells around `ShaftHeadCm`.
- Seats and furniture find their position from the spiral: `:9385-9386`; key layout `FIntVector (Level, Cell, HabId)` at `RHSimWorldSubsystem.h:171`, the third field overloaded (no room for a shaft or region).
- Room adjacency re-walks the spiral inline: `:5295`. Growth planning walks it too: `RHSimGrowth.cpp:49`.

**Fixed-depth loops** (`for Level = -1 .. -MaxDepth`): `StepHabitability` `:2367`, `PickFillHome` `:3207`, the seat scan `:4839`, the district paint check `RHSimDistricts.cpp:327`, and the floor tiles in `RHColonyVisualizerSubsystem.cpp:3448`.

**Air, leaks and ratings, per whole floor:**
- `StepHabitability` (`:2364-2420`): fill = cells x `O2FillKgPerCell`; leak = cells x `O2LeakKgPerCellPerSol`; a floor rates at full fill, circulated, and at least `MinLivableCells`. One rating per floor, so a cavern and a pocket on one floor share one air pool.
- The standing leak sums rated floors: `:5976`. Radiation falls by a factor per level: `GetRadiationAtLevel` `:2044-2051`. No heat or depth cost exists.
- Hope's Rooms term is counted once per function colony-wide, shallowest floor first: `:5222`.

**Robots never go below:**
- Both brains plan to `GetApproachPoint(Board.To, /*RobotLevel*/ 0)`: `RHRobotStateTree.cpp:115`, `RHRobotTaskProcessor.cpp:199`.
- `GetApproachPoint` (`RHSimWorldSubsystem.cpp:2336-2345`) sends a robot to the one shaft head when the site is on another floor.
- Roads are level 0 only: building checks at `RHSimRoads.cpp:235` and `:248`. Rover pads and the Lander rover need `Level == 0`: `RHSimRovers.cpp:50`, `:68`. `FRHRoadEdge` already carries `RegionId` (`RHRoads.h:30`).

**The commute's lift term** (`RHSimCommute.cpp`, under construction by C6a):
- `GetCommuteDistanceCm` (`:169-183`): both underground = `|Lh - Lw| x CommuteShaftCmPerFloor`; one end underground = depth x per-floor + surface distance from the one shaft head.
- Every floor end is anchored at the one head (`:28`, `:453-455`); caches key on `ShaftHead` (`:60`, `:70`). No lift capacity or wait.

**Underground districts:**
- A district is a whole floor: `FloorOwner` (Level -> district) at `RHSimDistricts.cpp:66`; `GetDistrictOfFloor` `:227-229`; a cell is its floor's district (`RHSimWorldSubsystem.h:1403`). C5's design deferred finer shapes to this step.

**Presentation:**
- The elevator view clamps to the bored depth and shows one floor: `RHPlayerController.cpp:1856-1867`.
- Cells are drawn at `GetShaftHeadCm() + SpiralCell(i)`: `RHPlayerController.cpp:531-536`, `:1135-1137`, `:1833-1837`; `RHColonyVisualizerSubsystem.cpp:3145-3147`, `:3457`.
- Buildings off the view level are hidden: `RHColonyVisualizerSubsystem.cpp:2563`, `:3018`. `rh.Cutaway` (`:68`) opens surface habitats only. There is no section view and no multi-floor volume.

**Saves:** strict, v40 (`RHSimWorldSubsystem.cpp:34`). Shaft depth, head and per-floor counts have been saved since v6; room cells and furniture are keyed by spiral index (v29-v31).
