# M1-a Working Spec — "Spine + Hands"

Status: agent-authored under the approved M1 scope (`docs/m1-scope-proposal.md`); implementation begins on delivery of this spec. Compile gates remain director-OK'd per standing orders.

## 0. UI toolchain probe result (ran 2026-07-05, editor live)

`BlueprintTools.create` with parent `UserWidget` produces a **plain Blueprint**, not a `WidgetBlueprint`: the asset exposes UserWidget CDO properties but **no `WidgetTree`**, and no MCP toolset offers widget-designer authoring. **Decision: all M1 UI is C++ Slate** (`SCompoundWidget`s owned by the PlayerController/viewport) — fully compile-gated, no MCP dependency, restylable for the diegetic pass in M1-d. UMG is off the table for this project's toolchain. (Probe asset deleted.)

## 1. Gate plan (three compiles, each followed by an SIE verification run)

- **Gate A — sim spine** (`RedHopeSim` only, no new deps): save/load, `CostResources` generalization, the two balance fixes, era-mode integrator + speed tier 60, headless commandlet.
- **Gate B — StateTree brain** (`RedHope`/`RedHopeSim` + **MassAI plugin enable — needs director OK, batched with this gate's restart**): robot decision logic moves from the processor switch to StateTree states (Work/Charge/Park scaffolding now; Repair/Survey/Shelter added in M1-b/c). Behavior-identical acceptance: the M0-c driver re-run must produce the same colony outcome. Fallback if MassAI's glue disappoints: hand-rolled per-class state evaluation stays in the processor (logged, revisit M1-b).
- **Gate C — hands** (`RedHope` only): camera/input wiring on the existing curve-zoom pawn (EnhancedInput, runtime-constructed actions, no asset dependencies), click placement with ghost + coverage/link ring, dig/survey designation, speed controls, and the Slate command deck v1 (build palette from `DT_Buildings` slice rows, quota/power readout, save/load buttons). Verification: a **hand-played** repeat of the M0-b arc — no scripts.

Gates B and C are order-independent; A lands first (both depend on its schema).

## 2. Save/load design (Gate A)

- **Format:** raw binary via `FMemoryWriter`/`FMemoryReader` + `FArchive` operators, owned entirely by `URHSimWorldSubsystem` (per approved architecture — no `USaveGame`). Header `{Magic 'RHS1', uint32 Version, FDateTime, double SimSeconds}`; unknown versions refuse loudly. Files: `Saved/SaveGames/RH_<slot>.sav`.
- **Payload:** clock (sim seconds, speed), stocks, import stock, order lag, fabricator mul, buildings (id, def, location, construction/batch state, active recipe, hoppers, pending outputs, attached deposit), deposits (remaining, pile, designated; **claims reset to 0**), quota block (phase, met sol, award, manifest, ship ETA), uplink queue, id counters, open tasks (**claims cleared** — robots re-claim after load; in-flight hauler cargo is returned to the source at save time so mass is conserved), robots (row name, position, charge, wear, task cleared).
- **Robot state lives in Mass fragments**, so `FRHRobotFragment` gains its `RowName` (needed for respawn) — the one fragment change.
- **Load path:** despawn all agents → reset colony state → apply payload → respawn robots → broadcast `OnColonyReloaded` → visualizers drop every mirror actor/instance and rebuild from a full state walk. This event is the decoupling stress test the standing orders ask for.
- Console: `RH.Save <slot>` / `RH.Load <slot>`; autosave to slot `auto` at each sol boundary (config row `AutosaveEverySols`, 0 disables).

## 3. `CostResources` generalization (Gate A)

`RH_Buildings` gains `CostResources` (semicolon list, e.g. `Struct:250`); empty falls back to `CostStruct_kg`. Site-delivery task posting, `ExecuteCommand` sufficiency pre-check, `ApplyBuildWork` material gate, and completion consumption all iterate the parsed map instead of assuming Struct. CSVs updated now (all rows `Struct:<n>`; Habitat's multi-resource list activates in M1-d). DataTable re-imports after the compile.

## 4. Balance fixes (Gate A, from the M0-c known-issues list)

1. Battery packs' half-charge credit moves from order time (`AddBuilding`) to construction completion (`ApplyBuildWork`), so it can no longer be clamped away by a not-yet-counted capacity. Instant builds (Lander) keep the immediate credit.
2. `RH_Buildings` gains `PowerGenBase_W` (Lander = 50, curve-independent trickle); `StepPower` adds it flat, solar stays curve-driven via `PowerGenPeak_W`. The CSV note and the sim stop disagreeing.

## 5. Era mode (Gate A)

- **Engage rule (data-driven guard):** tier 60 refuses to engage — and auto-drops to 1× — while any of: construction sites open, ship inbound, storm active (M1-c), quota deadline < 1 sol. Config row `EraAutoDropEvents` names the checks; the list grows in M1-c.
- **Mechanism:** at tier 60 the clock publishes 0 agent sub-steps (processors naturally park) and the sim world integrates the ledger in 1-sim-minute steps (`EraModeStepSimMinutes` row, already in config): solar curve sampled per step; battery/shed math identical to `StepPower` at the coarser dt; production advances by per-hour recipe rates gated by input availability; extraction draws deposits; dig→consumer flow abstracts to the parked excavators' aggregate rate (haulers not modeled at era scale — M1 accepts this; see acceptance bar). Robots park at their positions with idle draw suspended (fiction: powered-down posts).
- **Acceptance bar (from the proposal's confidence flag):** paired 10-sol runs (agent 8× vs era 60×) from the same save diverge ≤5% on every stock line; the paired-run test is a scripted harness kept in-repo.
- **Headless proof:** `RHSimCommandlet` grows up — creates a minimal world, initializes the sim chain, era-runs N sols, prints the ledger table (`-run=RHSim -sols=100`). Standing-order headless requirement, CI-runnable.
- `SpeedTiers` config row becomes `0;1;3;8;60`.

## 5b. Gate B architecture (locked 2026-07-05 after engine-header study; MassAI enable director-approved)

- **Determinism ruling:** MassAI's stock StateTree ticking is signal/frame-driven — it would let acceleration change outcomes. We therefore use MassAI's *data plumbing* but *not* its scheduling: **our own processor constructs `FMassStateTreeExecutionContext(Owner, Tree, InstanceData, MassContext)` per entity and ticks it inside the sim sub-step loop** (the 5.6+ context needs no signal subsystem; the engine's `MassStateTreeProcessors.cpp` per-entity flow is the template). Decisions stay locked to sim time.
- **Storage:** per-entity `FStateTreeInstanceData` lives in `UMassStateTreeSubsystem` (engine storage, `AllocateInstanceData` at spawn, handle in `FMassStateTreeInstanceFragment`); the tree asset rides a `FMassStateTreeSharedFragment` const-shared fragment.
- **Tree shape (scaffolding parity):** two states — **Work** (wraps the claim-by-class + dig/haul/build execution, calling the same sim API as the M0-c switch) and **Charge** (pad seek/dock/resume), with `FRHNeedsChargeCondition` (battery < seek fraction, mid-delivery exempt, pad exists) driving Work→Charge and task-success driving Charge→Work. Movement integration + battery drain stay in the processor. M1-b/c add Repair/Survey/Shelter as new states, which is the point of the port.
- **Tasks get fragments via `TStateTreeExternalDataHandle<>`** (resolved by the Mass context; our query declares every fragment the tree needs). The sim subsystem is fetched via `Context.GetWorld()` inside tasks for now — promoting it to a declared external dependency is an M1-b refinement (needs Mass subsystem traits).
- **Asset authoring:** StateTree assets are editor-GUI things and MCP cannot author them (same class of gap as UMG). The tree is built **programmatically** by a `WITH_EDITOR` dev command (`RH.BuildRobotStateTree`) against `StateTreeEditorModule`, then compiled — the asset is reproducible from code, which suits the toolchain.
- **Fallback preserved for free:** robots spawn with StateTree fragments only when the tree asset exists; the M0-c `RHRobotTaskProcessor` keeps running for entities *without* the fragment (its query excludes it). Legacy brain and StateTree brain are A/B-testable against the same driver — that is the Gate B parity instrument.

## 6. Out of M1-a

Wear/repair/survey behavior (M1-b — StateTree states land as scaffolding only), storms (M1-c), Q2/Habitat/manifest-composer (M1-d), any UI beyond command deck v1, any terrain work (deferred to M2 by director call).

## 7. Verification runs

- **A:** scripted SIE — save mid-batch mid-night, load, ledger and next-sol statuses identical to an uninterrupted control run; 100-sol headless ledger prints; a `CostResources` building builds via multi-line delivery check (data-only test row).
- **B:** re-run `BP_M0cTest` — same quota-met sol ±0, same end card (behavior-identical port bar).
- **C:** hand-played M0-b arc (dig → power → Forge → first batches) with zero console commands; capture of the deck + ghost placement.
