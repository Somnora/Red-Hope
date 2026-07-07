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

## Pending — awaiting director review

- (superseded) **M1-b stage close:** Gates A (Z-model + footprint), B (fleet reality), C (player's eyes) all committed + verified headless/smoke; the stage-end hand-played director run is the remaining sign-off (survey by hand → Ice_A appears; inspection card; fleet panel; PAUSED banner).
- **M1-c working spec** (`docs/m1c-working-spec.md`): events table (storms + flares), DustFactor through power, era honesty fixes (overshoot-carry + accumulator dump + paired-run 5% harness), surface radiation/shielding tax, event banner + sky, ComputeModule + uplink queue panel, power strip-chart.
