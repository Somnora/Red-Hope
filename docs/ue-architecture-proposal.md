# UE 5.8 Technical Architecture Proposal — The Red Hope

Status: agent-proposed 2026-07-04, awaiting director review. Stress-test results in §9 are verified facts, not proposals.

---

## 1. Modules & Project Structure

**Two C++ modules; the decoupling standing order becomes a compiler rule.**

```
Source/
  RedHopeSim/     Pure simulation runtime. No rendering, UI, or presentation deps.
                  Owns: sim clock, colony ledger, grid graph, agent sim, uplink/latency
                  queue, quota/events, era integrator, save serialization.
  RedHope/        Game module. GameMode/GameState/PlayerController, camera pawn,
                  building/robot visuals, sky rig, UI glue. Depends on RedHopeSim.
                  RedHopeSim can NEVER depend on RedHope — enforced by module rules.
Content/RedHope/
  Core/           GameMode, GameState, PC blueprints (thin BP children of C++)
  Data/           DT_* (from docs/data CSVs), CT_* curve tables
  GrayBox/        Buildings/, Robots/ — primitive-mesh placeholder BPs
  Sky/            MPC_Atmosphere, BP_MarsSkyRig, sky/fog/light curves
  Maps/           L_Slice, L_SimTest
  UI/             Widgets (C++ base classes, BP skins)
  Input/          IA_*/IMC_* Enhanced Input assets
```

Naming: `RH` C++ prefix (`FRHRobotRow`, `URHSimWorldSubsystem`), standard asset prefixes (`DT_ CT_ MPC_ BP_ WBP_ M_ MI_ SM_ IA_ IMC_ L_`). **`git init` before any scaffold work**; TopDown template content deleted at Step 4 with your explicit OK.

## 2. C++ vs Blueprint Split

**One rule: if it affects sim outcome, it's C++ + DataTables. If it's what you see or hear, Blueprint is allowed.**

- **C++:** every sim system, all DataTable row structs, save, time control, camera math, Mass fragments/processors, UI view-model bases.
- **Blueprint:** gray-box visual actors (mesh + material hookup), widget skins bound to C++ view-models, the sky rig actor, cinematic one-offs (ship landing beat), feel-tuning exposed as editable properties/curves.
- **DataTables/CurveTables:** every balance number, per Step 2's CSVs. Blueprints never store balance values.

Why: determinism and headless operation demand compiled sim code; designer iteration lives in data, not graphs; and the MCP toolchain (verified §9) is strong exactly where BPs are allowed and weak where C++ takes over — the split matches the tooling seam.

## 3. Agent Simulation — MassEntity vs StateTree vs Behavior Trees

Criteria: hundreds of agents by M2–M4 · 8× agent-band acceleration · determinism · headless ticking · sim/presentation decoupling · authoring cost · 5.8 maturity.

**Behavior Trees — rejected as backbone.** BTs assume actor-per-agent (Pawn + AIController + Blackboard): agent state lives in presentation-side objects, which is the banned coupling; hundreds of full actors is the classic scaling cliff; and our hard problems (task auction across a fleet, flow balancing) live outside any single agent's tree anyway. Mature debugging doesn't buy back the architecture mismatch.

**StateTree — adopted, but as the decision layer only.** Modern, lightweight, data-driven hierarchical state machines; `GameplayStateTree` is already enabled in the project. But its natural host is still a per-actor component: no batching, and no built-in sim/presentation split. As the *brain grammar* for individual agent behaviors it's excellent; as the fleet substrate it isn't one.

**MassEntity — adopted as the substrate.** Verified: in UE 5.8 MassEntity is **engine-core** (`Engine/Source/Runtime/MassEntity`), no longer a plugin — Epic promoted it; `MassGameplay` and `MassAI` remain plugins (present in this install, need enabling at Step 4). Why it fits like it was designed for us:
- Agent state = **fragments** in chunked SoA archetypes: cache-friendly at hundreds–thousands of agents, trivially serializable (save synergy), and *pure data* — the sim is inspectable and headless by construction.
- Logic = **processors** run in an order we fix on the sim clock: deterministic, batched, and parked wholesale in era mode.
- **MassRepresentation** swaps visuals (full actor near camera ↔ ISM instance far) as a separate, presentation-side concern — the standing-order decoupling is engine-native, and it's also how "watch one robot work" coexists with hundreds of agents.

**The proposal:** Mass fragments for robot state (Battery, Cargo, Task, Wear, MoveTarget); C++ processors for fleet logic (TaskAuction, Movement, Charging, WearAccrual) ticked by the sim clock; **MassStateTree** (the Mass↔StateTree integration) for per-agent hierarchical behaviors where they help — charging etiquette, storm response. Movement is our own light steering/navmesh-query processor: open Mars terrain, no crowds; **ZoneGraph/MassAI's navigation stack is explicitly skipped** (its maturity is the shakiest part of the Mass ecosystem, and we don't need it). Colonists at M2 ride the same substrate with different fragments.

**Honest costs + logged fallback:** Mass authoring is programmer-centric and its debugger is rawer than BT tooling. M0's seven robots don't need Mass — M2's hundreds do, and agent-architecture rewrites are the rework we must not schedule. If Mass friction threatens M0 velocity, the fallback is a hand-rolled SoA robot sim inside RedHopeSim **using the identical fragment structs** — the data layout is the contract, making later Mass migration mechanical. Decision checkpoint: end of the Step 4 scaffold.

## 4. Sim ↔ Presentation Seam (and how the era integrator talks to it)

`URHSimWorldSubsystem` (RedHopeSim) is the single owner of truth. The seam is two one-way pipes:

**In: command queue.** UI/presentation push typed `FRHCommand`s (PlaceBuilding, SetPriority, ConfirmManifest, SetSpeed…). Commands are timestamped, flow through the **uplink latency queue** (the latency mechanic is thus a property of the seam itself — presentation literally cannot mutate sim state directly), and apply at sim-tick boundaries. The command log doubles as a debug replay format.

**Out: event bus + snapshot reads.** The sim publishes typed events (`OnStockChanged`, `OnMilestone`, `OnSolElapsed`, `OnAgentStateChanged`…) at end-of-batch sync points on the game thread; presentation subscribes (building visuals, robot representation, sky rig, UI view-models) and interpolates. UI reads current values through const getters. Nothing presentation-side is ever authoritative.

**Era mode / headless:** the aggregate integrator advances the same ledger the agent processors feed, in 1-sim-minute analytic steps, publishing the same events at lower frequency — presentation doesn't know or care which integrator is running. Headless uses: (a) era mode in-session; (b) **`RHSimCommandlet`** — run N sols at maximum rate from a scenario, emit CSV metrics (sols-to-quota, energy margin per sol): our balance CI, runnable without opening the editor; (c) automation specs that drive the command API and assert on events. Determinism: fixed processor order, per-system seeded RNG streams, no wall-clock access inside RedHopeSim.

## 5. Atmosphere Dial (verified buildable end-to-end — §9)

- **`MPC_Atmosphere`** holds the global scalars/vectors: `Habitability`, `DustAmount`, `TimeOfSol`, plus derived tints. Verified today: MCP tooling creates the collection, writes parameters (editor auto-assigns GUIDs), binds `CollectionParameter` nodes in materials, and compiles clean.
- **`CT_AtmosphereDial`** curves map habitability → fog density, fog height falloff, sun intensity/color, sky Rayleigh tint, ambient, (later) water/vegetation params. Designers retune the planet's entire arc without code.
- **`URHAtmosphereSubsystem`** (game module — this is presentation) reads habitability + sol clock from sim events, evaluates the curves, writes the MPC, and drives **`BP_MarsSkyRig`**: one placed actor owning SkyAtmosphere, ExponentialHeightFog, DirectionalLight (sun angle from sol clock), SkyLight (periodic recapture). Terrain/rock/building materials read the MPC — the whole world re-grades from one scalar.
- Era mode switches the rig to the smeared-day mean-light state; the sky then *is* the terraform progress bar.
- **Lighting law: fully dynamic, Lumen on, nothing baked, ever** — the dial forbids static lighting. Scalability floor (mid-range target) ships as a documented device profile from the first gray-box map.

## 6. Save System

RedHopeSim owns serialization end-to-end: a version-headered binary archive (FMemoryWriter into a USaveGame slot wrapper for metadata/thumbnails); every sim system implements `Serialize(FRHSaveArchive&)`; **monotonic SaveVersion + per-system migration hooks** from day one (a dozens-of-hours campaign must survive patches — Step 1 risk log). Mass state serializes as archetype walks over SoA fragments (cheap, exact). Autosave at sol boundaries from a snapshot copy on a background thread. **Presentation saves nothing** — on load, visuals rebuild from sim state. Player camera/UI prefs live in a separate settings save. The command-log replay is a debug tool, not the save format.

## 7. Camera — Orbital ↔ Ground

C++ `ARHStrategyPawn`, no spring arm, direct transform math over a terrain-aware focus point. One zoom parameter t∈[0,1] evaluated through designer curves (`CT_CameraRig`): distance 25 m → 3,000 m, pitch 30° → 84°, FOV 55° → 40°, plus exposure/fog hints at the extremes. Zoom-to-cursor pivoting; full 360° free orbit (the prototype's constrained band is dead by construction); WASD/edge pan; **follow-agent mode** at low zoom — the emotional register shift is a camera feature, per brief §6. Enhanced Input (`IMC_Strategy`). All feel numbers in curves/config.

## 8. Time Acceleration

**No global time dilation** — it warps animation/UI and collapses at era speeds. `URHSimClockSubsystem` accumulates `real dt × speed` and executes fixed 0.1 s sim steps (catch-up cap per frame with a visible "sim struggling" indicator if exceeded); tiers 0/1/3/8 from `RH_Config`; pause = 0 with camera/UI fully live (orders queue, countdowns freeze — the Step 2 rule). Era mode switches integrators (§4). Presentation interpolates between the last two sim states for smooth motion at 1×–3×; above 3×, interpolation fidelity is explicitly unguaranteed.

## 9. MCP Toolchain Stress-Test Results (2026-07-04, all tests in-editor, cleaned up after)

**Verified working via MCP:**
1. **MPC pipeline end-to-end** (the flagged unknown — resolved ✓): `create_parameter_collection` → scalar+vector parameters written via ObjectTools (auto-GUIDs assigned correctly, round-trip verified) → `MaterialExpressionCollectionParameter` added to a material, bound to the collection, wired to BaseColor → shader recompiled with zero errors.
2. **CurveTables**: create, add rows, set/read keys (solar diurnal curve written as a live test); CSV `import_file` also available.
3. **DataTables**: create/import work **but require an existing `FTableRowBase` C++ struct** — no struct-creation tool exists (registry search confirmed only engine structs available). Consequence baked into the plan: row structs are C++ source I generate; you compile; then I import the Step 2 CSVs.
4. **Blueprints**: create with arbitrary parent class + compile verified; full graph authoring exists via a dedicated Blueprint graph DSL (used at Step 4 for thin BP glue).
5. Asset ops (folders, move/delete/duplicate/save), scene ops, primitive gray-box tools, annotated viewport capture, PIE control including **Simulate mode** (ideal for watching autonomous robots without a player pawn).

**Not possible via MCP → agreed fallback paths:**
- **All C++** (modules, structs, subsystems, processors): I write source on disk; you compile (editor "Compile" button or IDE); I verify via class-registry queries afterward. This is the primary path for the entire sim core — as anticipated at kickoff.
- **Plugin enables / project settings** (.uproject and .ini edits): I can write the files, but they take effect on editor restart — each such change gets your explicit go-ahead. Needed at Step 4: enable `MassGameplay` (+`MassAI` only if we later want its stacks).
- **Landscape sculpting**: no tool. M0 gray-box terrain = heightmap import or a large displaced ground mesh — decided at Step 4 map build.
- **Niagara, audio, UMG designer layouts**: no tools; slice UI = C++ widget classes with BP skins via the graph DSL; effects/audio are out of M0 scope anyway.
- Friction notes: one toolset description overflowed context (worked around via saved-file grep); DataTable CSV headers must exactly match C++ property names — I'll align headers when writing the structs.

## 10. Import-Only Rule — Architected as Removable (director directive)

Definition structs gain an `UnlockTech` column (FName) alongside the Phase 1 `ImportOnly` flag at Step 4 struct-writing time: empty + ImportOnly=false → always locally producible; ImportOnly=true + UnlockTech set → import-gated **until** that tech unlocks local fabrication (the Phase 3 independence milestone: local photovoltaics/robotics). Flipping a resource is a data edit, not a rewrite. No hardcoded import checks anywhere — all queries go through one `CanFabricateLocally(def, techState)` gate in RedHopeSim.

## 11. Step 4 Scaffold Plan (preview, on approval of this document)

1. `git init` + UE .gitignore/.gitattributes; commit clean baseline. 2. Delete TopDown template content (your OK). 3. Write Source/ modules, row structs (CSV-header-aligned), subsystem skeletons, commandlet stub → **you compile**. 4. Enable MassGameplay (restart, your OK). 5. Import the 9 CSVs → DT/CT assets. 6. Create MPC_Atmosphere, CT rigs, BP_MarsSkyRig, gray-box L_Slice (World Partition), primitive placeholder BPs, camera pawn, time controls. 7. Smoke test: PIE-Simulate + annotated viewport captures into the build log.
