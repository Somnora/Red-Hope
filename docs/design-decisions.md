# Design Decisions — The Red Hope

Format: each entry records the decision, its origin (agent-proposed / director-approved / director-directive), and why.

## Standing directives (director, from kickoff — 2026-07-04)

- Greenfield rebuild. The Three.js prototype is a design-learnings reference only; never port, adapt, or reference its code.
- Everything is real 3D geometry. Gray-box primitives until an art pass is explicitly scheduled. No sprites/billboards of any kind, no camera workarounds.
- Simulation decoupled from presentation from day one; sim must run headless and faster than real time.
- Data-driven everything: balance values in DataTables/curves/config, never hardcoded.
- Mental-health theme: prevention-focused systems, abstracted consequences, never graphic, never trivialized. Anything in this area is flagged for the director's explicit review before implementation.
- If a prototype pattern conflicts with idiomatic UE5 architecture, UE5 idiom wins.
- Small, reviewable increments; never more than one approved step ahead.

## Approved — 2026-07-04 (agent-proposed, director-approved)

Full rationale for each in `docs/step1-digest-and-recommendations.md`.

1. **Time model:** real-time with pause, stepped acceleration, fixed-timestep sim; latency denominated in sim-time; orders queue while paused.
2. **Placement:** square grid (~2 m cells), multi-cell footprints, 90° rotation; buildings snap, agents move freeform on navmesh.
3. **Colonists:** named individuals with cohort-depth simulation; specialists get extra depth.
4. **Embodiment:** disembodied mission control ("the Program"); no avatar; the entity gets re-chartered diegetically in Phase 3.
5. **Failure:** survive-the-consequences resilience; hard fail only at absolute edges; ironman as later difficulty option.
6. **Automation:** goals + priorities + policy toggles, no player programming at v1; task system data-driven so scripting can bolt on post-v1.
7. **Manifest:** mass budget + discrete packages; no volume Tetris; indivisible oversized items later.
8. **Solidarity Dilemma:** driven by a live Earth-relations sim (tension model + trade-ledger verification), not an event tree.
9. **Map model (adopted as the formal ninth open question):** one authored heightmap region, hand-placed data-defined deposits; procedural is a post-v1 sandbox question.
10. **Logistics:** hybrid — power/water/gases flow instantly in connected networks; solids are physical stockpiles moved by haulers.
11. **Hope:** derived, non-spendable index; explicit actions trade other resources for Hope inputs, never spend Hope itself.
12. **Identity axis:** accumulates silently from Phase 1, surfaces as a visible instrument in Phase 3.

## Approved — 2026-07-04 (Step 2: M0 vertical-slice spec, director-approved)

- The full M0 spec (`docs/m0-vertical-slice-spec.md` + `docs/data/*.csv`): units/clock doctrine (20-min sol, sol-hour energy), speed tiers 1×/3×/8× + era-mode integrator plan, expansion headroom (World Partition + authored sectors through v1), robot roster, chains, quota/manifest numbers, slice map. First-pass balance explicitly not judged until play; tuning from data, not rework.
- **Director directive — import-only is a Phase 1 rule, not a law of the world:** approved for the slice, but must be architected as removable. A late-game tech (Phase 3 independence milestone) unlocks local photovoltaic/robotics fabrication. The data/tech schema must flip a resource from import-only to locally-producible without a rewrite; never hardcode import-only as permanent. (Implementation: `ImportOnly` flag + `UnlockTech` FName column + single `CanFabricateLocally()` gate — see architecture proposal §10.)

## Approved — 2026-07-05 (Step 4 go: architecture + scaffold, director-approved with conditions)

- **UE 5.8 architecture proposal adopted** (`docs/ue-architecture-proposal.md`): two-module split (RedHopeSim pure sim / RedHope presentation, dependency-enforced); sim-outcome-in-C++ vs looks-in-BP rule; MassEntity substrate + StateTree decision layer (Behavior Trees rejected); command-queue-in / event-bus-out seam with the latency queue built into the seam; MPC + CurveTable atmosphere dial; versioned binary save owned by sim; curve-driven camera; fixed-timestep clock, no time dilation.
- **Director conditions, both honored:** (1) explicit OK gates for every destructive/compile step (TopDown deletion, plugin enable + restart, each compile), with the git baseline commit landing before any deletion; (2) the hardware benchmark ran empirically before any population design — 200/500 agents at 1×/8×, numbers logged in the build log (500 agents @8× adds +1.6 ms over the 46 ms editor floor).

## Directives — 2026-07-05 (director)

- **Mars sky per rover imagery:** Mars has no oxygen-rich atmosphere → no clouds, ever. Sky referencing comes from Mars rover photography: butterscotch dust dome, cool sunset aureole. Implemented session 6 as the habitability-0 endpoints of the atmosphere dial.
- **M0 progression approved stepwise:** M0-a (definitions/power/territory), then M0-b ("move forward with M0-b"), then M0-c ("please continue building this game"). Each stage verified in-engine before the next began.
- **Canon visual language from the director's reference set** (`~/Martians/assets/sprites`): bone-white / dark-slate industrial bodies, hazard-yellow trim, one saturated glowing accent per function (furnace-orange, cell-teal, ice-blue). Reference designs outrank agent taste per source-of-truth ordering. Applied session (canon pass) as the gray-box legibility language; real art pass still deferred.
- **Underground habitation is now explicit and core** (2026-07-05): surface structures pay a radiation/shielding tax; the colony houses people underground — bored vertical shaft, habitable floors off a central spine, lift as connective element; 5 subsurface floors to start (data-expandable); the Phase 1 exit habitat is substantially underground. Full spec + director rulings in `docs/m1-underground-proposal.md`. **Director rulings (2026-07-05), now approved design:**
  1. **Borer = a tool you own after building a Borer** (Sims terrain-tool): free-form player-directed excavation, dig as wide/deep as you like. Core loop = *excavation is cheap; habitability is the constraint*.
  2. **Floor size is player-defined**, gated by power. New mechanic: **hydrogen-fuelled machines** (Borer et al. can burn Electrolyzer H₂ instead of grid power, conserving batteries) — closes the ISRU loop, canon per brief §5.
  3. **Oxygen-per-carved-volume habitability gate:** every 10×10 of excavated space needs an O₂ fill to be livable; over-dig without ISRU and it's spacesuit-only. Makes the "oxygenated habitat" exit a scaling cost.
  4. **Per-floor habitability chain:** bore → shield → oxygenate/circulate before livable. Plus the **water/waste recycling loop** (urine → urea [building/plants] + graywater [potability degrades per pass]; feces → fertilizer) — Phase 2 *active* loop, facilities buildable/dormant by M1-d.
  5. **Sliced-ant-farm camera:** each floor a horizontal slice from above, surrounding un-excavated rock as a cut/sliced slab; elevator-panel floor selector + focus-depth slicing + shaft-section HUD widget; orbital-to-ground zoom pillar preserved.
  - **Milestone:** Z-model coordinate → **M1-b** (front, before fleet realism); radiation/flares → **M1-c**; full vault + borer + spoil loop + life-support → **M1-d** (the Phase 1 exit). Flat-terrain-until-M2 unaffected (shaft needs no heightmap). Revises the logged M1-d habitat chain: Shielding's role flips to the surface tax; Habitat becomes per-floor build-out; borer/H₂/O₂-fill are net-new.

## Directives — 2026-07-06 (director)

- **Robots are humanoid — canon.** The workforce reads as general-purpose humanoid androids (director's reference: Tesla Optimus), not wheeled rovers; specialization (excavator/hauler/fabricator/scout) reads as carried tools / backpack rigs / trim color on a shared chassis, not different vehicle bodies. Current instanced bone-white blocks are the gray-box stand-in. Implementation options logged for the deferred art milestone: (a) full pass = skeletal humanoid mesh + walk anim; (b) cheap interim = primitive-built humanoid (torso/head/limb instances across ~6 ISMCs, same per-sub-step transform feed) if legibility demands it before the art pass. Gameplay implication noted for Phase 2+: a humanoid workforce shares walkable space/airlocks with colonists — pathing/space assumptions stay compatible.
- **Gate C look accepted; bespoke building-art pass deferred.** The "designed gray-box" state (panel-seam/panel-variation/dirt material, greeble, foundation plinths, function-color bands, composed multi-mass Lander/Electrolyzer/WaterPlant/Forge/ComputeModule — committed `ed1b71e`) ships as the Gate C visual baseline. The director wants a deeper pass on overall building design "later on when it makes more sense to improve our art design" — i.e. a real art milestone (bespoke meshes/textures), not further primitive polish. Until then the standing gray-box order stands; visual work stays maintenance-only.

## Accepted behaviors & known issues — 2026-07-05 (M0-c verification, agent-logged)

- **Accepted (reads as intended drama):** at night on an empty bank the whole grid sheds, charge pads included — docked robots wait for sunrise. Whether the Lander should hold a reserve for pads is an M1 balance question, not an M0 bug.
- **Accepted (player's lesson):** with no charge pad built, robots work until they die where they stand. The sim never rescues a colony that skipped power infrastructure.
- **Known issue (M1 fix):** battery packs' arrive-half-charged credit is clamped away while the bank is under construction (capacity counts only completed storage). Fix: credit the charge at construction completion.
- **Known issue (M1 balance pass):** `RH_Buildings` notes describe the Lander as "trickle gen", but generation multiplies by the solar curve → 0 W at night. Reconcile the data note with the sim rule.
- **Known limitation (logged M1 task):** manifest cargo effects are name-keyed in `ApplyManifestItemEffect` at slice scale; the CSV `Effect` column is display text. Data-driven effect verbs are the M1 design.
- **Cleanup queued:** hand-placed `GB_Dep_*` gray-box markers in L_Slice duplicate the sim-spawned deposit visuals.

## Approved — 2026-07-05 (M1 scope, director-approved)

- **M1 scope proposal adopted as written** (`docs/m1-scope-proposal.md`): fleet wear/maintenance/scouting, dust storms, full latency arc (ComputeModule), save/load, era-mode 60× integrator + headless ledger, StateTree robot brain, data-driven manifest verbs, Ore→Shielding→HabSegment→Habitat exit arc with Q2, logged balance fixes; stages M1-a..d each ending in a verified run.
- **Director calls on the two flagged options:** (1) the **playability strand stays in M1** — hand-playable from M1-a, diegetic panels accreting per stage, manifest composer as the M1-d set-piece; (2) **heightmap terrain stays deferred to M2** — M1 remains systems-focused on the flat gray-box plane.
- M0-c verification reviewed and passed ("move onto the next step", 2026-07-05).

## Approved — 2026-07-06 (Gate C hand-played sign-off — M1-a closed, director verdict)

- The director hand-played the Gate C run (dig → power spine → Forge → 8× → Struct climbing/spending) with zero order rejections and ruled: **"controls are fine — functionally it's working."** Camera and command deck ship as the M1-a baseline. M1-a (Spine + Hands) is closed: Gates A, B, C all verified.
- **Director direction attached to the verdict:** gameplay and game UI should keep improving — the playability strand carries explicit weight going into M1-b+ (inspection card, fleet panel, visible-feedback fixes), while the diegetic skin remains the M1-d set-piece per the approved scope.
- **Findings from the run, queued as M1-b work:** placement/dig/confirmation prompts render invisibly under the help box (same GEngine-debug channel as the rejection toasts fixed in `924e1df`) → all player-facing prompts move to the deck's Slate notice channel; the paused state at session start is too easy to miss → prominent PAUSED treatment.

## Approved — 2026-07-06 (M1-b begin, director: "move onto Gate A")

- **M1-b working spec adopted** (`docs/m1b-working-spec.md`): Gate A (Z-model + footprint validation) begins; Gates B (fleet reality) and C (player's eyes) follow on its verification.

## Approved — 2026-07-07 (M1-b CLOSED, director hand-played verdict)

- Director hand-played the full M1-b build (survey → Ice_A discovery, inspection card, fleet panel, PAUSED banner, feedback channels): **"everything feels pretty good… we are getting somewhere."** M1-b closed; "progress to the next step" = M1-c approved as specced (`docs/m1c-working-spec.md`).
- **Director feature request (scheduled immediately):** survey results need persistent visibility — a "surveyed land" view highlighting already-surveyed ground with a breakdown of materials located there. Implementing as a survey-history overlay (sim keeps survey records; deck gains a Map toggle: surveyed circles in-world + a Known Ground panel listing discovered deposits with type/tonnage/dig status), riding the M1-c Gate A compile.

## Directives — 2026-07-07 (director, M1-c hand-play + the habitat vision)

- **Habitat vision is canon direction** — full capture in `docs/habitat-vision.md`: modular above-ground habs connected "like legos" with player-made entrances/exits and paint-to-size shells/hallways; room types (garden, workstation, labs, living quarters, dining, cooking, smoking areas — tobacco as a cultivable luxury for morale); air filtration stations throughout; plumbing to water-filter and septic/fertilizer areas; urine→urea (manufacturing binding agent) + water with drinking-recycled-water morale/sickness costs; feces→fertilizer; adjacency-as-gameplay (garden/septic beside living quarters sickens — hallway partition + filtration cures); pressurized doors for storm-breach retreat; interior visibility into habs/Forge/plants; surface↔underground connection; suits outside; tools/vehicles for dig sites and far surveys; glass domes + windows (light for crops, morale for crew, traded against shielding). Milestone mapping proposed in that doc §9 (M1-d births the schemas, M2 activates the human layer) — **awaiting director OK**.
- **Hand-play findings (M1-c surfaces):** (1) pressing 60× during the storm/flare read as "nothing happened" — the era refusal fires only into the small notice line; refusal feedback must be unmissable (queued: refusal banner treatment). Also raised as a design question: should era be allowed during steady-state storms (onset always experienced) while flares stay refused? (2) The storm's effect on individual stations wasn't legible — the banner may be missed and the inspection card says nothing about the storm (queued: card + shed-reason notes).
- **Territory question answered:** coverage grows by Pylon chains (80 m link range, each completed node projects new coverage) — already in game.

## Directives — 2026-07-07b (director rulings on storm handling, flares, arrivals; M1-d/M2 mapping approved)

- **Storm time rule (approved + amended):** era time-skip stays refused at onset and during flares, allowed mid-siege — AND storm/flare onset now **snaps any speed to 1× on the spot** so the player can batten down. Player may re-speed afterwards. Implemented this session.
- **Working through a storm costs:** robots sent outside during a dust storm take **accelerated wear** (`StormWearMul` config row, 2× default). Implemented this session.
- **Warehouse/garage building** (scheduled M2 with vehicles; schema headroom M1-d): stored vehicles/robots take no storm wear — shelter as a buildable choice.
- **Solar flares are ELECTRONIC, not mechanical (design fork, scheduled):** flares should cause electronic/software **faults** repaired by a HUMAN — "I don't think robots should be able to repair other robots." Faults land with the M2 human layer (schema headroom in M1-d). **Interim:** the verified flare wear-×3 stays as placeholder. **Flagged tension for director:** the approved-and-verified RC-M loop has a robot repairing robots' mechanical wear — does the no-robot-repairs principle retire RC-M repair (e.g., move wear repair to a dock/bay facility or human crew), or does it apply only to electronic faults? Awaiting ruling before M1-d schema work.
- **Ship arrival countdown alerts:** at T-2 and T-1 sols to touchdown the colony gets a loud alert to prepare. Implemented this session for the supply ship; crew ships inherit the same seam in M2.
- **M1-d/M2 habitat mapping approved** as proposed in `docs/habitat-vision.md` §9 (M1-d births room/compartment/door/filtration schemas dormant; M2 activates the human layer).

## Directives — 2026-07-07h (M1-d CLOSED; M2 opened — Gate A sequencing + mental-health handling)

- **M1-d / Phase 1 closed:** director cleared progression to M2. Close hygiene: MinLivableCells live in DT_Config, BP_VaultDemo removed from L_Slice.
- **M2 Gate A = colonists as agents** (director-selected from structured options): actual crew arrives first — ship-borne, housed only in certified vault floors, drawing O2/Food — before rooms/morale/crops. Rationale: the "people show up and inhabit what you built" payoff makes the vault earned; rooms then get designed against real occupants.
- **Mental-health handling (director-selected): build mechanics abstract now, review the framing before ship.** Neutral placeholders (numbers, neutral labels like "unsupported"/"evacuated to orbit"); the full player-facing wording/iconography/tone review is Gate D — a hard stop before any of it is final. A1's only failure surface is evacuation: abstract, prevention-framed, never harm imagery.
- **CrewPod economics:** 2400 kg (heaviest single manifest item) carrying 4 colonists + 200 kg Food (~40 sols for 4). Crew-vs-hardware becomes THE manifest decision; the provisions clock makes the Gate-C garden urgent by design.

## Directives — 2026-07-07g (director recording verdict: distinct floors, pit depth, ground/night, HUD drift)

Director watched a screen recording of the underground view and reported: (1) "I can see the floors being forged out while looking at the surface, instead of two distinct floors"; (2) the pit "does not feel like a square hole dug in sand — no depth, looks like a shaved layer"; (3) "the dirt ground disappeared overnight and became grey"; (4) "the HUD top-right kept moving left"; (5) buildings/robots are still square boxes.

- **Two DISTINCT floors (v3):** the elevator is now a hard cut — a floor's visuals show iff `BLevel == ViewLevel`. SURF shows only the intact sunlit surface (no underground bleed); −N shows only that floor's pocket. Fixes (1).
- **Real ground stays at SURF, never greys:** the ground plane is hidden ONLY while underground; at SURF it's always the real lit dirt (the old rig hid it whenever a shaft existed and swapped in gray-box skirt cubes that read dark at night). Fixes (3).
- **The pit reads as a hole (kept from v2, now underground-only):** descending renders the sand skirt + rock walls from the surface rim down to the open floor. Fixes (2)'s intent; the *surface* no longer shows the hole (that's the "distinct floors" tradeoff — **open design note: if a surface cue that digging happened is wanted, add a shaft-head collar visible at SURF; flagged, not built**).
- **Elevator motion:** the camera focus plane now rides smoothly between floors (~1.5 s) instead of cutting.
- **HUD pinned:** the top-right readout + fleet + inspect + known-ground panels are all width-pinned (`ReadoutWidthPx=560`), so the growing power sparkline / toggling `SHED:N` / vault line can't reflow the block. Fixes (4).
- **Square-box art (5):** reaffirmed deferred to the post-gameplay art milestone (director's own earlier ruling).
- **Adversarial self-review caught 3 regressions in the first cut, all fixed before commit:** (a) HIGH — riding the elevator to an *un-bored* floor left the surface hidden, the ground showing, and a phantom pit drawn (fixed: descent clamps to the shaft's *reached* depth); (b) MEDIUM — a deposit discovered / ship landed while underground popped surface furniture into the pit view (fixed: born-hidden per current view); (c) LOW — the shaft column spawned visible at SURF on the first bore (fixed: born-hidden). Ground search/hide now keys off the view, not shaft depth.

## Directives — 2026-07-07f (director ruling: habitat minimum size)

- **The Phase-1 exit requires a minimum vault size (ruling: 4 cells).** A single 10×10 sealed room is a pocket, not a habitat — the exit now fires only when a floor reaches `MinLivableCells` (DT_Config, 4) carved cells AND is pressurized + circulated. Implemented + verified headless (`-habitat`): a 2-cell floor fills 200/200 but stays `rated=0`; carving to 4 rates and fires the exit. **The atmosphere chain still runs below the minimum** (the floor pressurizes as you build toward it, draining the O2 pool) — only the *rating* and the exit gate on size. **Legibility (so it never reads as a silent failure):** a one-shot banner "FLOOR N SEALED — X of 4 cells, carve K more to certify it a livable habitat" fires when a floor is atmospherically complete but undersized AND no further carve is queued (quiet mid-dig-out, speaks when you've stopped); the circulator's inspection card and `RH.Habitat` show `cells/4` + status. Config row `MinLivableCells` (CSV; **live DT sync next editor session**, code default matches). This **resolves the open design question** flagged at the Gate C commit.

## Directives — 2026-07-07e (director findings, M1-d vault hand-play in progress)

- **Placement feedback gap (bug):** during the uplink signal-lag window (~20-45 s) a placed order was invisible — nothing on screen, no red footprint, double-placement possible with the collision surfacing only at execution. **Fixed in source (rides next compile):** every queued Build order draws a cyan hologram + Δ countdown at its spot from the click onward, and `CanPlaceBuilding` now also blocks against in-flight orders ("order already in transit for that spot") so the ghost reads red immediately.
- **Storm power discipline (ruling):** *"incorporate the ability to shut off some of the robots, tools, areas so that you're not wasting battery life when you don't know when you'll get valuable sun again — this makes Hydrogen a very valuable resource."* **Built in source (rides next compile):** per-structure manual breaker (inspection-card "Switch OFF/ON" button + `RH.Power`; zero draw/gen, batches frozen — even H2 batches; a switched-off circulator drops its floor's rating loudly, emergent) + colony-wide **Hold Fleet** (deck button + `RH.HoldFleet`; robots finish current tasks then claim nothing new; held robots skip dead charge pads). Battery banks keep their pooled storage when off (a breaker must not vaporize charge). Save v9. Confirms the H2-as-strategic-fuel design line (fuelled batches already run through brownouts).
- **Borer onboarding confusion:** "what key turns it on?" — the Borer works ORDERS, not a switch, and boring had no deck button (console-only until the next compile lands `Bore → -N`). Director asked to *see it simulated* → `BP_VaultDemo` driver authored (narrated PrintString walkthrough: bore → excavate → elevator → AirFilter → pressurize → exit card), to be placed in L_Slice for a watch-it run; removed at M1-d close like all harnesses.

## Directives — 2026-07-07d (director rulings: RC-M retained, robots repair/fabricate robots; shielding-tax deferral confirmed)

- **RC-M stays; robots CAN repair and fabricate robots** (director, reversing the 2026-07-07b "I don't think robots should be able to repair other robots" line). Rationale, verbatim: *"having robots repair each other, fabricate new robots makes a ton of sense. I was just trying to add more purpose to the humans to give them abilities that the robots don't have."* **This supersedes the no-robot-repairs principle** — the verified RC-M mechanical-repair loop is canon and keeps its full scope. The **open RC-M ruling is CLOSED.**
  - **Downstream (M2, reopened positively):** human purpose must come from **abilities robots don't have**, NOT from being the-only-repairer. The 2026-07-07b "flares → electronic faults repaired by a human" mechanic was a crutch for that purpose-goal and is **no longer load-bearing** — flares may still cause electronic faults, but robots (an electronics-specialist, or RC-M extended) can service them; who/what repairs them is an open M2 design question, not a human monopoly. "What can only a human do?" becomes an explicit M2 design theme (morale/luxury/judgment/research per the habitat vision), replacing the repair crutch. The interim flare wear-×3 placeholder is unaffected.
- **Shielding build tax deferral confirmed** (director: *"I trust your judgement on this"*) — the M1-c→M1-d move (below, 2026-07-07c) stands. M1-d is now **unblocked**: the RC-M ruling is resolved and the §9 mapping is approved.

## Decision — 2026-07-07c (radiation plumbing lands in M1-c; shielding build tax moves to M1-d)

- **Radiation is now a plumbed, data-driven quantity** (config: `RadiationSurface` 1.0, `RadiationPerLevelMul` 0.05): surface exposure index attenuated per floor of overburden (≈20×/floor), with a live solar flare multiplying the *surface* index by its severity. Consumed in M1-c only as **per-station flare legibility** on the inspection card (`SOLAR FLARE: radiation ×N`), giving the flare the same station-level "why" the storm already has and previewing the underground payoff. M2's human-health layer and M1-d's vault read the same accessors.
- **Shielding build tax deferred from M1-c to M1-d** (agent design ruling, within approved M1-c scope): with every structure at Level 0 (no subsurface until the M1-d shaft), a surface-shielding cost tax would raise *all* build costs uniformly, break the M0-c scripted regression arc, and create no decision — there is no shielded underground alternative to weigh against. The tax only becomes meaningful in M1-d, paired with the vault, where surface-vs-underground is a real tradeoff. **This supersedes the M1-c spec's "surface radiation/shielding tax" line item** — radiation plumbing ships in M1-c; the *cost tax* ships in M1-d. Flag to director if you'd rather force it earlier.

## Pending — awaiting director review

- (superseded) **M1-b stage close:** Gates A (Z-model + footprint), B (fleet reality), C (player's eyes) all committed + verified headless/smoke; the stage-end hand-played director run is the remaining sign-off (survey by hand → Ice_A appears; inspection card; fleet panel; PAUSED banner).
- **M1-c working spec** (`docs/m1c-working-spec.md`): events table (storms + flares), DustFactor through power, era honesty fixes (overshoot-carry + accumulator dump + paired-run 5% harness), surface radiation/shielding tax, event banner + sky, ComputeModule + uplink queue panel, power strip-chart.

## Directive — 2026-07-07i (director: fast-track M2 gates without per-gate hand-play)

- Director: *"can we move ahead to gate B without having to do the test? I only have 1 day with Fable 5 (you) and want to get as much done as possible today."* — the Gate-A hand-play stops being a blocker; per-gate director sign-off is DEFERRED to a consolidated review session, not skipped. Headless self-tests + live smokes are the verification bar for the fast-tracked gates; every gate remains individually committed and reviewable.
- Built under this directive (all 2026-07-07): Gate B (rooms/adjacency/jobs/Hope, save v11), Gate C (the garden, save v12), Gate D abstract slice (comforts/luxury loop). The consolidated hand-play now covers: pit view v3, the crew arrival, zoning + the hallway cure, the garden arc, the comforts lift.

## Agent-proposed balance awaiting director review (2026-07-07, M2 Gates B–D)

- Hope weights (DT_Config): base 50, housing max 15, 5/morale-point per room type per rated floor, 3/job seat, vault milestone 5, adjacency −8/pair, unsupported −10 each, comforts +8 at full supply. Chosen for legible arithmetic in tests, not tuned play.
- Garden rates: 250 kg soil + 50 kg seeds per cell (one pallet + one vault = 4 cells), 1.0 kg Food/sol/cell yield, 4.0 kg Water/sol/cell draw (3 cells ≈ break-even for a 4-colonist pod, ~40-sol provisions clock beaten by one pallet+vault landed in time).
- Comforts: 0.2 kg/colonist/sol (one 300 kg crate ≈ 94 sols for a pod of 4).
- Open design questions logged in build-log Session 27: grow-light power, recycled-water psychology (needs a recycling loop first), Hope's mechanical effects, smoking/tobacco (waits on the framing review itself).

## Standing gate unchanged — the Gate-D framing review

- Every player-facing string/icon born in Gates A–D (evacuation, unsupported, comforts, garden loss) is a PLACEHOLDER under the mental-health directive. The director reviews all of it before any of it ships. Nothing in the fast-track waived this.

## Design synthesis + roadmap — 2026-07-07j (director's big-picture design turn; 5-lens panel + director decision)

**Context:** director asked "what mechanical effects should we add" and offered a batch of ideas. Key finding: his rival-nation/trade/appease-or-defy idea is **almost verbatim the brief's signature Phase-3 Solidarity Dilemma (§59-62)** — validated canon, not a new invention. A 5-lens design panel ran (Hope-modulator + survival-economy completed; diplomacy/wonder/UX triaged from analysis).

**DIRECTOR DECISION (structured ask):** build **"Hope drives the colony"** first (Hope Band Spine + Work Tempo) — DONE + verified this session (`db940f0`, save v13). See build-log Session 29.

**STANDING DESIGN RULE established this session — the death-spiral guardrail:** any Hope→work→Hope feedback must be *carrot-first and recoverable*. Work-tempo floors at 0.60 (a strained colony is slow, never dead); low-Hope penalties are SOFT CAPS, never accelerating multipliers; the low-Hope *stick* (burnout/work-refusal) ships ONLY after a hand-proven recovery from a Strained state. This is the mental-health directive ("consequences abstracted, recoverable, prevention not punishment") expressed as an engineering constraint. It also protects determinism (a runaway feedback amplifies tiny state diffs across the 60× era band).

**APPROVED-IN-SPIRIT ROADMAP (director-excited, agent-recommended sequencing; each needs its own explicit build OK):**
1. **UX contextual-action overhaul (director's #1 concrete ask, near-term, low arch risk):** click a finished building → popup of its afforded verbs with time/crew/power/wear costs BEFORE commit (the popup issues a normal uplink order, respecting order-lag — it does NOT bypass the sim). Sims-style CATEGORIZED build menu (Boring/Habitat/Power/Production/Life-Support). On-placement "what is this / how to use it" tooltips. All copy DataTable-authored so the director writes it. Data model: building def → afforded verbs + costs.
2. **Grow-lights vs. glass greenhouse (near-term, kills the free-food loophole):** grow-lights draw power per planted cell; OR fabricate Glass (Regolith→Glass recipe) → a greenhouse that's near-free power but rides the real solar curve (dies in dust storms) AND must sit shallow where RADIATION bites (the M1-c per-level model) — power vs. yield vs. shielding, three built systems colliding. Forces "plan power before settlers."
3. **The water loop (director approved "next"):** greywater potability as a SINGLE decaying scalar on the Water pool (NEVER per-batch cycle-counts — that breaks determinism; a mass-weighted blend is associative and era-parity-safe). Recycled water degrades each cycle; only fresh ice-melt makeup restores it → ice-cap drilling becomes a permanent standing demand.
4. **Cryo berths (reframed from director's "find a cryo chamber or perish"):** a PRE-BUILT strategic object, not a panic scramble — "we have N berths, so N survive a famine dormant, the rest evac." Replaces the abstract evac placeholder with dormancy-not-death (perfectly on the MH directive). Provisioning it reads as leadership; scrambling reads as a fail state.
5. **Rare metals → Workbench → local fabrication:** activate the Fe/Al/Si/rare ore split (data already flags it); rare metals gate Labs/Workbenches which enable local SpareParts + robot fabrication, breaking the import-only umbilical — autonomy felt in the supply chain (the latency-to-autonomy theme in production form).
6. **M3 Sovereignty (the big arc — the Solidarity Dilemma made SYSTEMIC, brief open-question #8):** the key insight — build **trade routes as physical dependencies FIRST** (rover convoys costing hydrogen + vehicle wear, disruptable by storms), let the player grow reliant on a neighbor's ice for their water loop, and THEN Earth demands they cut it. Now Comply/Defy isn't a card — it's "my water supply just got severed and I chose that." The dilemma is generated by the player's OWN supply graph, so the same choice hits differently every run. Hope drives "diplomatic weight"; repeated choices accumulate the Earth-Aligned↔Martian-Identity axis that gates endings.

**Hope's remaining mechanical consumers (beyond work-tempo, for later increments):** the Generational Carrot (sustained high Hope + housing headroom + food surplus → slow growth → FIRST MARTIAN-BORN CHILD, a one-time surge + the identity-axis seed); the Flourishing Layer (Hope≥90 + manned Labs → ordered DISCOVERIES from a DT_Discoveries table, incl. the microbial-life find — the "stoked to watch them thrive" payoff, spectacle in the pure-listener presentation layer); "Letters Home" (agent-original: a thriving named colonist generates a tiny personal beat — the wonder delivered through *specific people you kept alive*).

**Agent-proposed balance (all needs director review):** HopeSmoothTau=3 sols, TempoSlope=0.006 (Hope 50=1.00x, 90=1.24x, 20=0.82x), TempoMin/Max 0.60/1.25, band thresholds 20/28/35/45/70/75/85/90. Chosen for legible math, not tuned play.

**Gate-D framing review UNCHANGED as the standing mental-health hard-stop:** every player-facing string across A–D+ is placeholder pending the director's review of tone/iconography before ship.

## Inline review findings — 2026-07-07k (garden fork + water loop, LOW/inert, documented not fixed)

The Session-30 review workflow hit the session limit before any finder completed, so the c994bff..HEAD diff was reviewed INLINE. Hard invariants verified directly (save v14 symmetry, greywater-from-met-draws-only, dark-cell-spends-no-water, greenhouse depth-gate sign). Two findings surfaced; both are LOW severity with zero gameplay effect, and are recorded here as KNOWN behavior so they are not later rediscovered as "bugs":

1. **Near-boundary era-parity softness (potability clamp + grow-light bank gate).** The water-potability decay/restore terms are linear (parity-exact), but the `[0,1]` clamp on the STORED scalar is a nonlinearity: if potability is pinned near a bound while flux continues, the agent band (which clamps at sub-step granularity) and the 60x era band (clamps once) can differ by a bounded epsilon. Same class for the grow-light `Power.BatteryWh >= GrowLightWh` boolean, which samples the instantaneous bank at different dt granularities near the margin. WHY IT IS INERT: (a) the potability divergence is confined to the region near 1.0, where the Hope penalty is exactly 0 (it only bites below WaterPotabilityFloor=0.6); (b) the grow-light gate diverges only at the exact knife-edge of bank-vs-demand; (c) colonist-scale agent-vs-era parity has been a ~5% ledger tolerance since M1-c (paired-run: extraction 0.4%, stores 4.0%), NOT bit-exactness — the bit-exact invariants are the ZERO-POP 10-sol baseline (unaffected; verified byte-identical) and save/load round-trip within a band (verified exact: potability 0.6280->0.6280). DECISION: document, do not fix — adding an unclamped-accumulator or a smoothed gate would add real complexity to remove a harmless epsilon in a zero-effect region (over-engineering).

2. **Re-zone Garden->Greenhouse skips the Glass cost.** A cell planted as a grow-lit Garden (soil+seeds paid) that is then re-zoned Greenhouse keeps growing under the greenhouse (solar) rule without paying GreenhouseGlassKgPerCell, because the planting loop only fires on UNplanted cells. No material is duplicated (Glass is not created, merely not required for the conversion), and it is a lateral trade (steady grow-lit yield for solar-dependent yield), not a win. DECISION: accept as known minor; the Glass requirement is initial-glazing, and forfeit-on-any-garden-rezone would punish a legitimate conversion.

Both are agent-found/agent-adjudicated; flagged for the director only as design notes, not blockers.
