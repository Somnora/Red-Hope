# THE RED HOPE — Game Design Brief & Agent Kickoff

**Working title:** The Red Hope
**Genre:** Colony automation / city-builder / grand strategy hybrid (SimCity × Civilization × Factorio-lite, on Mars)
**Engine:** Unreal Engine 5.8 — greenfield build via native Unreal MCP + AllToolsets. The prior Three.js prototype is retired; it survives only as a design-learnings reference (see §7). No code, assets, or rendering approaches carry over.
**Perspective:** Top-down / orbital strategic camera with zoom to ground-level cinematic view
**Session model:** Long-form single-player campaign; sandbox mode later

---

## 1. High Concept

A fleet of autonomous robots is dropped onto Martian soil by a private corporation. The player guides them from a bare landing site to a self-sustaining human civilization — and ultimately to a political identity of its own. The game moves through three escalating phases: **Automation** (robots bootstrap infrastructure), **Habitation** (humans arrive with needs, skills, and fragility), and **Sovereignty** (rival national colonies, trade, Earth politics, and the question of what Mars owes Earth).

**Thematic spine:** Hope is a resource. Every system — power, water, morale, diplomacy — feeds a single question: does Mars become an extension of Earth's conflicts, or something new?

---

## 2. The Three Phases

### Phase 1 — Automation (Robots Only)

**Fantasy:** You are mission control for a robotic vanguard. Nothing breathes here yet. Every watt matters.

**Core loop:** Land → deploy solar array + battery/transformer network → robots self-charge → excavate → build shelter → smelt → expand.

**Key systems:**
- **Power grid:** Solar arrays, battery banks, transformers, charging pads. Robots have battery ranges; the grid's footprint literally defines your buildable territory. Night cycles and dust degrade output.
- **Robot fleet management:** Robot classes (excavator, hauler, fabricator, scout, maintenance). Robots wear down; maintenance bots repair others; the player balances fleet composition.
- **The Forge:** First major milestone structure. Smelts regolith and raw ore into structural materials, radiation shielding panels, and pressurized habitat segments.
- **Ice drilling & ISRU chain:** Drill subsurface ice → melt to water → electrolyze into O₂ (breathable air reserves) and H₂ (stored for future fuel: return ships and long-range hydrogen rovers that exceed battery range).
- **Earth latency:** Early on, command queues execute with a signal delay (4–24 min abstracted into gameplay as "order lag"). Building local compute infrastructure reduces lag — the colony's growing independence is mechanical from minute one.
- **Supply ships & the CEO:** Meeting quotas set by the corporation's CEO earns supply ships. **The player designs each ship's manifest** — the signature logistics decision. 10–50 ship awards can be partitioned across soil, seeds, specialty tools, spare parts, luxury goods, or advanced robots. A ship of 100% soil is a legitimate (risky) strategy.

**Phase 1 exit condition:** A sealed, powered, oxygenated, water-positive habitat rated for the first human crew.

### Phase 2 — Habitation (First Humans)

**Fantasy:** The stakes change the moment something can die of despair.

**Arrivals:** Engineers, scientists, botanists, medics — specialists with skills robots lack (research, greenhouse cultivation, tool invention, medical care).

**Key systems:**
- **Needs hierarchy:** Oxygen, water, food, warmth, sleep — then safety (radiation exposure, pressure integrity) — then psychological needs: privacy, recreation, meaningful work, social connection, communication with Earth.
- **Morale & mental health:** Colonists track satisfaction, stress, and purpose. Neglect leads to burnout, work refusal, mutiny factions, and in the worst cases self-harm crises that trigger colony-wide morale shocks. (Design note: handle this theme with care in presentation — consequences and prevention systems, not graphic depiction. Prevention is the gameplay: counselors, recreation, workload balancing, meaningful milestones.)
- **Work assignment & specialization:** Humans invent and unlock things robots can't: crop strains, medical tech, advanced fabrication, research trees.
- **Greenhouse & soil economy:** Earth soil (from ship manifests) vs. slow regolith remediation. Fresh food is a massive morale multiplier over nutrient paste.
- **Robot–human coexistence:** Robots become the labor class; humans direct. Heritage robots (first-wave units) can be preserved as monuments/mascots for morale, or scrapped for parts — an early identity decision.

**Phase 2 exit condition:** Sustained habitability score across N sols → the Big Arrival: a large civilian wave, and the CEO lands in person as a morale event (and a new political actor living in *your* colony).

### Phase 3 — Sovereignty (Nations, Trade, and Earth's Shadow)

**Fantasy:** You're no longer building a base. You're governing a society other powers want to use.

**Key systems:**
- **Rival national colonies:** Other nations establish territories. Each has personality, resource profiles, and needs — natural trade complementarity (their ice-rich crater vs. your ore-rich highlands).
- **Trade routes:** Rover convoys and eventually hopper flights. Routes require infrastructure (waypoint charging, beacons), can be disrupted by dust storms, and become physical objects on the map worth protecting.
- **The Solidarity Dilemma (signature mechanic):** Earth conflicts generate demands from your sponsor nation:
  - **Comply** (cut trade with rivals): +national pride, +Earth supply ships, −colonist morale (shrunken community), −rival relations, escalation risk.
  - **Defy** (maintain/expand trade): +pan-Martian solidarity, +fair exchange rates, +colonist morale, −Earth supply priority, denied requisitions, possible sanctions or worse.
  - Repeated choices push the colony along an **Earth-Aligned ↔ Martian-Identity axis** that gates late-game content: a loyalist corporate colony, an independent Martian federation, or fractured cold-war Mars.
- **The CEO endgame:** Once resident, the CEO's interests (profit, Earth shareholders) increasingly diverge from the colony's. He can be ally, rival, or figurehead depending on player choices.
- **Terraforming as generational clock:** Atmospheric processors, orbital mirrors, greenhouse gas factories — century-scale progress measured in milestones, not completion: first unprotected surface walk at low altitude, first liquid surface water, first rainfall, **first Martian-born child** (a political event: native-born citizens weight the identity axis toward Mars).

**Endings (examples, not exhaustive):** Corporate Jewel of Earth · Independent Mars Federation · Martian Cold War · Abandonment/Collapse (failure states).

---

## 3. Signature Mechanics (What Makes This Game *This Game*)

1. **Ship Manifest Design** — every supply award is a hand-built cargo puzzle; player-authored risk.
2. **The Solidarity Dilemma** — recurring, escalating Earth-vs-Mars loyalty choices with systemic (not scripted) consequences.
3. **Power-as-Territory** — your grid footprint *is* your border in Phase 1; expansion is electrical before it is architectural.
4. **Latency-to-Autonomy Curve** — command lag shrinks as the colony's local intelligence grows; independence is felt in the controls before it's declared in the story.
5. **Hope as a Meter** — a colony-wide composite of morale, milestones, and momentum that modulates everything (work speed, birth rates, mutiny risk, diplomatic weight).
6. **Heritage Robots** — obsolete first-wave units as cultural objects; scrap value vs. identity value.
7. **The Planet Is the Progress Bar** — a single habitability scalar (0→1) continuously re-grades the entire world: sky color, fog density and distance, sun color/intensity, ambient light, and eventually surface vegetation and standing water. Terraforming progress is never just a HUD number — the player *sees* Mars soften from butterscotch murk to blue-tinged sky over dozens of hours. (Validated in the Three.js prototype; in UE5 this should be implemented as a Material Parameter Collection + curve-driven sky/fog/light rig, where Lumen makes the payoff dramatically stronger.)

---

## 4. Environmental & Systemic Threats

- **Global dust storms** (multi-sol events): solar collapse, forced shelter, trade route disruption — and diplomatic openings (send aid to a crippled rival, or don't).
- **Radiation events** (solar flares): shelter alarms, exposure damage to unshielded robots/humans.
- **Micrometeorite strikes, pressure breaches, equipment failure cascades.**
- **Water table depletion** per region → forces expansion and trade.
- **Social threats:** faction formation, strikes, mutiny, corporate espionage from rival colonies (late game).

---

## 5. Progression & Economy Sketch

- **Resources:** Power (Wh), Water, Oxygen, Hydrogen, Regolith, Ore (Fe/Al/Si/rare), Structural Materials, Shielding, Food, Soil, Spare Parts, Luxury Goods, Research Points, Hope.
- **Tech tree split:** Robotic (Phase 1, corporate-fed) → Human R&D (Phase 2, colonist-driven) → Civic/Terraforming (Phase 3, politically gated by the identity axis).
- **Currency-free early game** (quota → ship rewards), evolving into **barter trade** (Phase 3) and optionally a Martian scrip/credit system as an independence milestone.

---

## 6. Presentation Direction

- **Tone:** Hard-science optimism. *The Martian*'s competence porn + *For All Mankind*'s political weight. Awe, isolation, then community.
- **Look:** Realistic Martian palette (ochre, rust, butterscotch skies) punctuated by the alien green of the first greenhouse and warm habitat light — color = life earned.
- **Camera:** Orbital planning view ↔ smooth zoom to ground level to watch a single robot work or a colonist's day. The zoom *is* the emotional register shift.
- **Audio:** Thin-atmosphere sound design (muffled exterior, rich interior), radio chatter, Earth transmissions that grow more distant-feeling as the identity axis shifts.
- **UI:** Diegetic where possible — mission control panels early, civic dashboards later.

---

## 7. Prototype Post-Mortem (Three.js v2 — reference only)

A playable Three.js prototype (~950 lines, `main.js` + `index.html`) preceded this project. **It is a cautionary reference, not a starting point.** It proved the core loop was fun and several design decisions correct, while its presentation approach failed. The repo may be shared for inspection; treat it as an archaeological source.

**Validated design decisions — carry these forward as design facts (re-derive, don't port):**
- **The atmosphere dial** (signature mechanic #7): one habitability scalar lerping sky/fog/light across the whole scene. The single best idea in the prototype.
- **Crisis/growth cadence:** life-support reserves that drain and fill; a grace window (~8s at prototype scale) before a shortage costs a colonist; sustained surplus + available housing before growth. The *shape* of this loop felt right; retune all numbers for UE scale.
- **Era structure with cinematic beats:** Era I Terraforming → "First Light" landing event at a habitability threshold → Era III Colony at a population threshold. The descending-lander arrival beat landed emotionally; keep it.
- **Config-first tuning philosophy:** every number lived in one CONFIG block. In UE5 this becomes DataTables/curve assets — designers tune without code changes.
- **Footprint-based placement UX:** multi-tile footprints, ghost preview, valid/invalid highlight, R-to-rotate. The interaction pattern was solid.

**Failed approaches — do not recreate, emulate, or work around:**
- **Sprite/billboard entities.** 4-direction billboarded sprites for buildings, robots, colonists, and ships made the whole project read as a mess and consumed enormous effort (camera-facing logic, frame-picking, blob contact shadows, transparency sorting). **Everything in UE5 is real 3D geometry.** Gray-box primitives first; real assets later.
- **Constrained camera band.** The orbit camera was locked to a narrow azimuth swing purely to hide that sprites were flat cards. With real meshes this problem does not exist — design the strategic-camera-to-ground-zoom freely.
- **Hand-rolled DOM HUD** glued to globals. UE gets a proper UI layer (UMG/CommonUI) bound to simulation state.
- **Scene-graph-as-game-state.** Entity data lived inside render objects. In UE5 the simulation layer must be decoupled from presentation from day one.

**Standing directive:** if any prototype pattern conflicts with idiomatic UE5 architecture, UE5 idiom wins. The prototype earns a look only for *what* it did (design), never *how* (implementation).

---

## 8. Scope & Milestone Plan (for the design agent to refine)

**M0 — Vertical Slice Target (the "one perfect hour"):**
Landing → power grid → first excavation → Forge online → first ice drill → first quota met → first manifest designed → ship arrival. One biome, ~5 robot types, no humans yet. This proves the core loop.

**M1 — Phase 1 complete:** Full automation layer, dust storm event, latency system, save/load.
**M2 — Phase 2 alpha:** First 6–10 colonists, needs/morale sim, greenhouse chain, first crisis event.
**M3 — Phase 3 prototype:** One rival colony, trade routes, first Solidarity Dilemma chain, identity axis.
**M4 — Beta:** Three rivals, endings framework, terraforming milestones, CEO arc.

**Explicit non-goals for v1:** multiplayer, combat/warfare simulation (war is a *threatened outcome*, mostly off-screen pressure — revisit post-v1), full planet map (one region, expandable).

---

## 9. Open Design Questions (agent: propose answers)

1. Real-time with pause, or tick-based sols? How does time acceleration interact with latency mechanics?
2. Grid-based building or freeform placement with snapping?
3. How granular is the colonist sim — named individuals with traits (RimWorld-style) or statistical cohorts with named specialists only?
4. Should the player embody a character (colony director avatar) or remain a disembodied "mission control"?
5. Failure philosophy: hard fail states, or Frostpunk-style "survive the consequences" resilience?
6. How much of Phase 1 automation should be player-programmable (logic/priorities) vs. fully autonomous with player goals?
7. Manifest design: discrete slots per ship, or mass/volume budget optimization?
8. How do we make the Solidarity Dilemma systemic rather than a scripted event tree?

---

*The agent kickoff prompt lives in the companion file `the-red-hope-kickoff-prompt.md`. Paste that file's contents as the first message in a fresh Claude Code session launched from the UE 5.8 project root, with this brief present in the project.*
