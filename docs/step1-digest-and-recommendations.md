# Step 1 — Digest, Gaps, and Open-Question Recommendations

Status: agent-proposed, 2026-07-04. Awaiting director review. Nothing here is canon until approved.

---

## (a) The game, restated

The Red Hope is a long-form, single-player colony sim in three escalating acts, set in one honestly-rendered 3D Martian region. Its thematic spine — hope is a resource — is mechanical, not decorative: every system feeds one question, whether Mars becomes an extension of Earth's conflicts or something new.

**Act 1 — Automation.** The player is mission control for a corporate robot vanguard. Nothing breathes yet; electricity is the pulse. The power grid *is* territory — solar arrays, batteries, transformers, and charging pads define where robots can operate and therefore where anything can be built. Night and dust make output rhythmic and fragile. A small fleet of robot classes (excavator, hauler, fabricator, scout, maintenance) wears down and repairs itself; fleet composition is the tactical layer. Two production spines carry the act: regolith and ore into the Forge, out as structural materials, shielding, and habitat segments; and subsurface ice to water to electrolysis, splitting into oxygen reserves and hydrogen fuel. Orders execute on a delay because Earth is far; building local compute shrinks the lag, so independence is felt in the controls long before the story declares it. Meeting the CEO's quotas earns supply ships, and the player authors every manifest — a hand-built cargo gamble where 100% soil is legal and reckless. The act ends when a sealed, powered, oxygenated, water-positive habitat is rated for a human crew.

**Act 2 — Habitation.** Humans land and the game grows a conscience — the stakes change the moment something can die of despair. A needs ladder runs from oxygen up to purpose. Morale and mental health are simulated with prevention as the gameplay — counselors, workload balance, recreation, meaningful milestones — and consequences abstracted, never depicted. Specialists unlock what robots can't: research, crops, medicine, invention. Earth soil from manifests competes with slow regolith remediation; fresh food is a morale engine. Robots become the labor class, and obsolete first-wave units become heritage objects — scrap value versus identity value. Sustained habitability triggers the Big Arrival: a civilian wave, and the CEO landing in person — a morale event and, from then on, a resident political actor.

**Act 3 — Sovereignty.** Rival national colonies with complementary geology make trade natural; routes are physical infrastructure that dust storms cut and politics targets. Earth's conflicts arrive as the Solidarity Dilemma: comply with the sponsor nation (supplies and pride, a shrunken community) or defy it (Martian solidarity, Earth's cold shoulder, sanction risk). Repeated choices integrate into an Earth-Aligned ↔ Martian-Identity axis that gates the endgame: corporate jewel, independent federation, cold-war Mars, or collapse. Terraforming is a generational clock measured in milestones — first unprotected walk, first liquid water, first rainfall, first Martian-born child — and the planet is the progress bar: one habitability scalar continuously re-grades sky, fog, and light so that dozens of hours of play visibly soften Mars from butterscotch murk toward blue.

In one sentence: it is a game where "life support" keeps redefining itself — watts, then breath, then meaning, then sovereignty — and the power grid, the cargo manifests, and the loyalty choices are how the player answers.

---

## (b) Contradictions, risks, and missing systems

### Contradictions / discrepancies needing a ruling

1. **"Nine" open questions vs. eight listed.** The kickoff prompt says nine open design questions; §9 lists eight. Either one was cut in editing or the count is a typo. The two biggest questions the brief *doesn't* ask are the map/terrain model and the logistics model (below) — either would be a worthy ninth. Ruling requested.
2. **Hope: resource or meter?** §5 lists Hope among resources; §3.5 defines it as a composite *derived* index (morale + milestones + momentum) that modulates other systems. It can't be both without double-counting. Recommendation: derived, non-spendable index at v1 — spending morale like currency is punitive to balance and muddies the read. Explicit "spend-shaped" moments (declare a holiday, hold a festival) can *trade other resources for Hope inputs*, not spend Hope itself.
3. **Heritage robots are called an "early identity decision" (Phase 2) but the identity axis is a Phase 3 system.** Recommendation: the axis exists from Phase 1, accumulates silently (compute investment, heritage choices, naming moments), and is surfaced as a visible instrument only in Phase 3. Cheap, and it makes the Phase 3 reveal feel earned rather than switched on.
4. **"Power (Wh)" listed as a resource.** Power is a flow (W) and storage is a stock (Wh); the grid sim must model both distinctly (generation vs. battery state), unlike pool-style resources. Not a contradiction so much as a precision note — the M0 spec will treat it as the special case it is.

### Top risks

1. **Triple-genre scope.** Automation game × needs sim × geopolitical strategy is three games. The milestone plan already stages it; the discipline is holding Phase 3 to *shallow-but-systemic* and defending the non-goals list. Biggest scope trap: robot programmability (Q6) and logistics depth (below).
2. **Latency vs. game-feel.** Order lag reads as "broken input" if it ever touches camera, UI, or selection. It must apply only to strategic order *execution*, with a visible uplink queue (inspect/cancel in-flight orders). Denominate lag in sim-time so time acceleration compresses the wait.
3. **Mental-health content.** Needs a written abstraction contract (what is named, what is shown, what is implied) before M2 work begins; everything in this area comes to the director first, per standing orders.
4. **Hundreds of agents × faster-than-real-time.** This is the architecture-defining constraint: fixed-timestep sim decoupled from rendering, agent logic that batches (hence the MassEntity evaluation in Step 3), and presentation as a thin skin over sim state.
5. **The atmosphere dial forbids baked lighting.** Everything is dynamic (sun, sky, fog, eventually water/vegetation), so Lumen + dynamic sky on mid-range hardware needs a scalability plan from the first gray-box map, not retrofitted.

### Missing systems (implied but unspecified)

1. **Terrain/map model** — region size, topology, and whether resource deposits (ice table, ore veins) are authored or procedural. Blocks M0. Recommendation: one authored heightmap region (~2×2 km playable at slice scale), hand-placed deposits driven by a data-defined deposit type system, so procedural seeding can arrive post-v1 without rework. Authored guarantees the "one perfect hour" pacing.
2. **Logistics/storage model** — global resource pool vs. physically hauled goods. Blocks robot design and M0. Recommendation: **hybrid** — power, water, and gases flow instantly within connected networks (grid/pipes as graphs); solids (regolith, ore, materials, cargo) are physical: they sit in stockpiles and hauler robots move them. This keeps the hauler fantasy and the ground-zoom payoff, makes Phase 3 convoys a natural extension, and avoids full Factorio-belt scope.
3. **Research acquisition in Phase 1.** "Corporate-fed" how — quota rewards, manifest slots, time? Needs definition by M1 (proposal: tech unlocks ride ship manifests as optional cargo — makes the manifest even more load-bearing).
4. **Robot end-of-life.** Destroyed vs. repairable wrecks, salvage rules — matters for maintenance loops and for heritage-robot stakes.
5. **Population growth model.** The prototype validated housing-gated growth cadence; needs formalization (arrival waves vs. births, caps) by M2.
6. **Difficulty/accessibility framing.** Ties to the failure-philosophy question; fine to defer past M1, but the save-system design should anticipate ironman/permadeath variants.

---

## (c) Recommendations on the eight listed open questions

**Q1 — Real-time vs. tick-based; time acceleration × latency.** Real-time continuous play with pause and stepped acceleration (roughly 1× / 4× / 12×; exact steps tuned in M0), built on a **fixed-timestep simulation** that presentation merely interpolates. Sols are a calendar over continuous time, not turns — the validated crisis/growth cadence and the power day/night rhythm are continuous-time shapes, and turn-based sols would fight the "zoom down and watch one robot work" register. Latency is denominated in sim-time: acceleration compresses the wait, pause-and-plan is always free (orders queue while paused, transmit on unpause), and local compute shrinks lag toward zero so Phase 2+ controls feel immediate — the latency-to-autonomy curve exactly as designed. Fixed timestep is also what makes headless and faster-than-real-time simulation trivially safe.

**Q2 — Grid vs. freeform placement.** Grid-based, square cells (~2 m to be confirmed in M0), multi-cell footprints, 90° rotation — precisely the prototype-validated footprint UX, now anchored to real terrain. A grid keeps power-footprint radii, adjacency rules, and territory legible and cheap to validate; freeform+snapping buys beauty shots at the cost of endless placement-UX polish and mushy adjacency logic. Buildings snap; robots and colonists move freeform on navmesh. The orbital camera reads grids beautifully, and at ground level natural clutter hides the regularity.

**Q3 — Colonist granularity.** Hybrid: **named individuals, cohort-depth simulation.** Every colonist is a persistent named person with a role, one or two traits, and an individual morale/stress state — enough that a crisis happens to *someone* — but economics run batch-wise (shared need pools per habitat, role-based work assignment, no relationship graphs or per-person inventories at v1). The target population (~50–200 by Phase 3) is too large for RimWorld-depth sim and too small for faceless cohorts to carry the mental-health theme or the first-Martian-born-child beat. Specialists get extra depth: unique unlocks and story flags.

**Q4 — Player embodiment.** Disembodied mission control, with a deliberate diegetic frame: the player *is* "the Program" — the mission-operations entity Earth addresses. No avatar; an on-map character would drag third-person expectations (movement, dialogue, mortality) against the strategic camera and add a fourth game to the scope. The payoff: in Phase 3 the colony can formally re-name and re-charter what the player-entity *is* (corporate department → colonial directorate), which makes the identity axis personal without ever spawning a player pawn. The CEO lands better as a foil when the player is an institution, not a rival face.

**Q5 — Failure philosophy.** Frostpunk-style **survive-the-consequences resilience**, with hard failure only at absolute edges (total population loss; Earth recalling the program as the Abandonment ending). Continuous systems degrade with grace windows and recoverable ruin — the prototype's validated cadence — because a dozens-of-hours campaign must not be one dust storm from a reload, and consequence-survival is what generates this genre's stories and feeds Hope as a meter. Ironman/permadeath become difficulty options later, not the default philosophy.

**Q6 — Programmability of automation.** **Goals and priorities, not programming, at v1.** The player sets what and where (build orders, dig zones, quota targets), rank-orders priorities globally and per robot class, and flips a few policy toggles (recharge threshold, storm behavior); robots self-select tasks within that frame. Visual logic circuits are a Factorio-depth rabbit hole competing for the same years as the human and political layers — and the fantasy here is mission control, not firmware authorship. Latency already punishes micromanagement by design, so goals-over-clicks is thematically coherent. The task/priority system stays data-driven so a scripting module can bolt on post-v1.

**Q7 — Manifest design.** **Mass budget + discrete packages.** Each ship grants a tonnage budget by award tier; cargo items are discrete packages with per-item mass (soil pallet, seed vault, spare-parts crate, advanced robot…), no volume Tetris. One constraint keeps the decision strategic — what does the colony need — rather than geometric; discrete packages keep choices chunky, comparable, and DataTable-tunable; and the extreme plays stay visible and legal (a 40 t ship of 40 soil pallets *looks* like the gamble it is). Later, indivisible oversized items (an 18 t atmospheric-processor core) create ship-defining choices inside the same model.

**Q8 — Systemic Solidarity Dilemma.** Drive it from a **live Earth-relations sim, not an event tree.** A lightweight background model tracks sponsor-vs-bloc tension, moved by semi-random geopolitical events *and by your actual trade flows*. Demands are generated, not scripted: when tension crosses thresholds, the demand targets your real dependency graph, quantified ("suspend ice imports from Huoxing Station for 30 sols"). Consequences post to persistent, inspectable stocks — supply priority, exchange rates, rival trust, morale, the identity axis. Crucially, compliance is verified against the trade ledger, not a dialogue flag — quiet smuggling under a public promise is possible, detectable, and has its own escalation. Scripted content shrinks to flavor text over systemic state; every player's dilemma differs because every player's economy differs; the axis becomes an integral of behavior, not a quest counter.

---

## Rulings requested before Step 2 (M0 spec)

1. The ninth question — was one cut, and if so what was it? Otherwise: adopt map model and logistics model (missing-systems #1 and #2 above) as the de facto ninth and tenth, with the recommendations given.
2. Hope: derived index (recommended) or spendable resource?
3. Identity axis accumulating silently from Phase 1–2, surfacing in Phase 3 (recommended)?
4. The eight recommendations above — approve, amend, or overrule each.
