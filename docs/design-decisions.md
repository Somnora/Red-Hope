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

## Accepted behaviors & known issues — 2026-07-05 (M0-c verification, agent-logged)

- **Accepted (reads as intended drama):** at night on an empty bank the whole grid sheds, charge pads included — docked robots wait for sunrise. Whether the Lander should hold a reserve for pads is an M1 balance question, not an M0 bug.
- **Accepted (player's lesson):** with no charge pad built, robots work until they die where they stand. The sim never rescues a colony that skipped power infrastructure.
- **Known issue (M1 fix):** battery packs' arrive-half-charged credit is clamped away while the bank is under construction (capacity counts only completed storage). Fix: credit the charge at construction completion.
- **Known issue (M1 balance pass):** `RH_Buildings` notes describe the Lander as "trickle gen", but generation multiplies by the solar curve → 0 W at night. Reconcile the data note with the sim rule.
- **Known limitation (logged M1 task):** manifest cargo effects are name-keyed in `ApplyManifestItemEffect` at slice scale; the CSV `Effect` column is display text. Data-driven effect verbs are the M1 design.
- **Cleanup queued:** hand-placed `GB_Dep_*` gray-box markers in L_Slice duplicate the sim-spawned deposit visuals.

## Pending — awaiting director review

- 2026-07-05 (agent-proposed): **M1 scope proposal** (`docs/m1-scope-proposal.md`) — "Phase 1 complete" per brief §8: fleet wear/maintenance/scouting, dust storms, full latency arc (ComputeModule), save/load, era-mode 60× integrator + headless ledger, StateTree robot brain, data-driven manifest verbs, Ore→Shielding→HabSegment→Habitat exit arc with Q2, playable-UI strand (M0 is script-only), logged balance fixes. Four stages (M1-a..d), each with scripted/hand-played verification. M0-c verification itself was reviewed and passed ("move onto the next step", 2026-07-05).
