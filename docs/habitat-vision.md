# Habitat Vision — Director Directives (2026-07-07)

Status: **director-directed canon** (verbal direction after the M1-c hand-play). **§9 milestone mapping APPROVED by the director 2026-07-07b**; weather/arrival rulings from the same exchange appended as §10. This document extends — never replaces — the underground rulings in `docs/m1-underground-proposal.md`.

Canon anchors: brief §2 Phase 2 (humans arrive, morale), §5 (Hope index, luxuries, water/waste), the 2026-07-05 underground rulings (per-floor habitability chain, water/waste recycling loop, sliced camera). The director's words are the source of truth here; agent structuring is labeled.

---

## 1. Modular habs — above ground AND below

- **Above-ground habs are buildable**, connected together **"like legos"**: modules join to modules, with player-made **entrances/exits**.
- **Paint-to-size**: the player paints how large a hab or hallway is (the same free-form designation language as the Borer excavation tool — one interaction pattern, surface and subsurface).
- **Hallways are first-class**: they connect modules AND serve as strategic partitions (see §4 adjacency).
- **Above-ground habs connect to underground habs / the underground city** — one continuous pressurized network across the surface boundary (the shaft/trunk from underground spec §5 generalizes to surface connection points).
- Surface habs live under the existing canon **surface radiation/shielding tax**; underground remains the shielded default. The player chooses the tradeoff.

## 2. Room types (functional interiors)

Rooms are designated functions within the hab shell (agent note: rooms-as-data, like buildings):
- **Gardening station** (crops; interacts with light §8 and fertilizer §4)
- **Workstation**, **Labs**
- **Living quarters**
- **Dining rooms** and **cooking stations**
- **Smoking areas** — when humans arrive, they can **grow tobacco once they can cultivate luxuries**: morale for when basic needs are met (extends the brief's Hope/luxury canon; LuxuryGoods manifest item already exists)

## 3. Life support infrastructure (in-hab)

- **Air filtration stations** must be built **throughout** the hab network — not one global machine; local coverage matters.
- **Plumbing** connects rooms to the **water filtering station** and to the **fertilizer/septic creation area**.
- **Pressurized doors** separate each room and the underground areas: if a storm tears down part of the hab, inhabitants **retreat somewhere with oxygen**. Compartmentalization is the survival strategy, and it is the player's job to build it.

## 4. The waste/water loop with MORALE consequences (extends underground ruling #4)

- **Urine → separated into urea + water.**
  - If the colony has to use **filtered urine for drinking water, inhabitants feel negative about it and get sick over time** — potability isn't just chemistry, it's psychology.
  - The **filtered-out urea becomes a binding agent for manufacturing** (also plant nutrient, per the 2026-07-05 ruling).
- **Feces → converted to fertilizer** → feeds the garden.
- **Adjacency matters — placement is strategic:**
  - Garden/fertilizer/septic area adjacent to living quarters → inhabitants **feel discouraged and eventually get sick**.
  - The remedy is spatial design: **a hallway partitioning the living spaces from the garden, with an air filtration system between them** → inhabitants stay happy.
- (Agent note: this is the game's first *architecture-as-gameplay* system — room adjacency + partition + filtration as the morale/health calculus. Flagged per the standing mental-health directive: sickness/morale presentation gets director review before implementation.)

## 5. Interior visibility

- The player must be able to **see inside** habs — and inside the **Forge, power plants, etc.** — to watch **robots and inhabitants working together**.
- An interior view mode (agent note: the sliced-ant-farm camera ruling already covers underground floors; this extends the same "cut-open" read to surface structures — likely cutaway shells on the same slicing system).

## 6. Suits, tools, vehicles

- **Humans wear space suits when outside.** Always.
- **Humans and robots use tools and vehicles** for jobs at dig sites and to **survey far-away areas** (canon anchor: brief's hydrogen rovers exceeding battery range; the scout's thin margins already gesture at this).

## 7. Territory growth (director question, answered)

- "How do you increase the size of the buildable circle over time?" → **Pylon chains** (already in game): each completed Pylon placed within 80 m link range of an existing grid node projects a new coverage circle. Chains walk the territory outward — this is how the demo reel reached the ice field. Post-M1 headroom: larger hub nodes, and the shaft-as-vertical-pylon (underground spec §5).

## 8. Light, domes, windows

- **Glass/plexiglass domes buildable above underground habitats/cities** — natural light makes crops easier to grow.
- **Windows throughout boost crew morale.**
- (Agent note: light becomes a resource-adjacent quantity — natural vs artificial grow-light power cost — and windows/domes trade shielding for morale. Pairs with the radiation tax: a dome is deliberately the opposite of overburden.)

## 9. Milestone mapping (agent-proposed, awaiting director OK)

The director's closing concern — "with how the game is structured now, I don't know how humans could live in this environment without livable spaces" — is exactly what the Phase 1 exit builds. Proposed sequencing:

- **M1-d (Phase 1 exit — the vault, as already ruled):** the underground habitability chain (bore → shield → oxygenate → circulate) is the FIRST livable space, plus the groundwork this vision needs baked early:
  - **Hab-module/room data schema** (rooms as data rows: function, adjacency tags, filtration/plumbing needs) so M2 activates content, not schema — same discipline as the M0 dormant rows.
  - **Pressurized-door/compartment model** (compartments as the pressure/atmosphere unit; doors as edges). The storm-breach retreat mechanic needs the graph even before breaches exist.
  - Buildable-dormant: water filtering station, septic/fertilizer area, air filtration station (the 2026-07-05 ruling already puts treatment facilities in M1-d).
- **M2 (humans arrive):** room functions go live — living quarters, dining/cooking, labs, gardens; the morale/sickness adjacency system (director review on presentation per the mental-health directive); drinking-water psychology; luxury crops incl. tobacco + smoking areas; windows/domes morale; interior view mode; suits; surface hab modules + lego connection + paint-to-size shells; vehicles/tools for humans and robots.
- **Not scheduled yet:** anything Phase 3.

Rationale for the split: M1-d stays the tight "first vault" exit the rulings define, but every schema the hab vision needs (rooms, compartments, doors, filtration coverage) is born there so the M2 human layer lands on prepared ground instead of a rewrite.

---

## 10. Weather & arrival rulings (director, 2026-07-07b — implemented same session unless noted)

- **Onset snaps time to 1×** (any speed): the player gets real time to batten down; re-speeding afterwards is allowed. Era stays refused at onset and during flares; a steady-state storm may be era-skipped.
- **Robots sent out during a dust storm take accelerated wear** (`StormWearMul`, 2× default) — working through the siege is allowed and costly.
- **Warehouse/garage** (M2, schema headroom M1-d): stored vehicles/robots take no storm wear.
- **Flares become electronic** (M2): software/electronic faults repaired by a HUMAN — robots do not repair robots (director). Interim: flare wear ×3 placeholder. Open ruling: does the no-robot-repairs principle retire the RC-M mechanical-repair loop, or apply only to electronic faults?
- **Ship arrival alerts at T-2 and T-1 sols** — supply ship now; crew ships inherit the seam in M2.
