# M1 Scope Proposal — "Phase 1 Complete"

Status: **agent-proposed, awaiting director review.** Nothing here is started.
Canon anchor: brief §8 — *"M1 — Phase 1 complete: Full automation layer, dust storm event, latency system, save/load."* Everything below traces to the brief, the approved M0 spec's explicit M1 commitments, or the logged M0 debt list; agent-proposed additions are marked **[agent]**.

---

## 1. What M0 proved, and the gap M1 closes

M0 proved the loop: dig → smelt → extend the grid → ISRU → quota → manifest → ship, self-sustaining, in-engine. But it is **script-driven, single-quota, weatherless, wearless, and unsaveable** — a vertical slice, not a phase. M1 turns the slice into the complete Phase 1 game: the full robotic-automation layer under environmental pressure, playable by a human, savable, and ending at the brief's Phase 1 exit condition — *"a sealed, powered, oxygenated, water-positive habitat rated for the first human crew."*

## 2. Canon pillars (brief §2 Phase 1 + §8)

**P1 — Fleet reality: wear + maintenance + scouting.** Robots accrue wear per active sol (`WearPerActiveSol`, already in `RH_Robots`); past `WearDegradeThreshold` (50) work rate degrades linearly; at `WearHaltThreshold` (100) the robot halts — repairable, never destroyed (robot destruction stays out per M0 spec §10; nothing in Phase 1 canon needs it). The idle RC-M finally works: seeks degraded/halted robots, spends `SpareParts` (25 wear/part), making the PartsCrate manifest item a real strategic line. The idle RC-S finally works: survey orders through the uplink reveal `SurfaceVisible=FALSE` deposits (`WorkRate` = survey radius, 60 m) — Ice_A stops being free knowledge and becomes the brief's "first go-look moment"; Ore_B and Ice_B become discoveries. Deposit visuals spawn on discovery, not at BeginPlay.

**P2 — Dust storm event (brief §4).** Multi-sol, data-driven events: `DustFactor` collapses solar output (the config knob has waited since Step 2), fog/sky presentation thickens through the atmosphere dial, wear rate rises while exposed **[agent — cheap, thematic, uses existing fields]**. The colony's answer is the bank + shedding stack M0-c verified, now under multi-sol siege: storms convert "every watt matters" from a night rhythm into a crisis. New `RH_Storms` table (schedule/duration/severity; scripted schedule for M1, probabilistic later).

**P3 — Latency system complete (signature mechanic #4).** The `ComputeModule` Lander upgrade (row exists, never exercised: 100 kg Struct, +150 W always-on — autonomy has a power price) takes lag 45→20 s; the ComputeCore manifest item (verified in M0-c) takes it to 8 s. The uplink queue becomes visible UI: every order with transmit timestamp and diegetic label ("Δ 11 min"), cancellable until execution (M0 spec §8 commitment).

**P4 — The exit arc: Ore → Shielding → HabSegments → Habitat.** Activates the dormant rows, no schema invention: `SmeltOre` (already active) pulls the colony 220 m SW to Ore_A; `MakeShielding` (Struct+Ore) and `MakeHabSegment` (Struct+Shielding) give the Forge its Phase-1-exit recipes; **Q2 activates** (Struct 600 / O₂ 150 / H₂ 40, deadline sol 22 — the H₂ line makes fuel banking pay, brief's ISRU canon). The **Habitat** build (6×8, consumes HabSegments + Shielding) is the finale. Phase 1 exit check = Habitat complete + powered + O₂ and water stocks positive-trending across N consecutive sols → end card. **Requires one honest schema change:** site delivery generalizes from Struct-only to a resource list (`RH_Buildings` gains `CostResources` — e.g. `Struct:600;HabSegment:4;Shielding:8`; `CostStruct_kg` becomes derived/deprecated).

**P5 — Save/load.** Versioned binary save owned by the sim (approved architecture §saves): full colony state (clock, stocks, buildings incl. hoppers/batches/sites, deposits incl. discovery state, robots incl. battery/wear/task, quota phase, manifest, uplink queue, RNG state). Robots' claimed tasks are released on save and re-claimed after load — the task board reposts from state, so saves never serialize in-flight intent **[agent — simplest honest model]**. Load broadcasts a single resync event; visualizers rebuild from state — which is exactly the presentation-decoupling stress test the standing orders want. Autosave each sol + manual slots.

## 3. Engineering spine (M0 spec commitments + approved architecture)

- **Era mode (60×), the M1 architecture line item from M0 spec §1:** agent ticking suspends; the colony ledger integrates analytically in 1-sim-minute steps from the same DataTable rates; agents park; a **data-driven auto-drop list** (storm onset, ship arrival, quota deadline <1 sol, survey complete) snaps back to 1×. The headless commandlet stub finally earns its name: `-run=RHSim -sols=100` prints the ledger — the standing-order headless proof, runnable in CI.
- **StateTree decision layer (approved architecture, deferred from M0-c):** the robot brain moves from the processor's switch into StateTree states (Work / Charge / Repair / Survey / Shelter / Park); the Mass processor keeps movement + integration. Charging *queue* etiquette (the M0-c backlog item) lands here as a Charge-state refinement.
- **Data-driven manifest effect verbs:** the `Effect` column stops being display text; `ApplyManifestItemEffect`'s name-keyed block becomes a small verb interpreter (`AddImportStock:SolarArray:1`, `SpawnRobot:RC_E2`, `SetLagTier:2`, `AddStock:Soil:1000`, `FabSpeedMul:0.15`) — new manifest items become rows, not code.
- **Logged balance fixes:** battery half-charge credited at construction completion (not clamped away at order time); Lander trickle-gen reconciled with its CSV note (proposal: small constant `PowerGenBase_W` column, curve applies only to solar) — both are data/sim honesty fixes from the M0-c known-issues list.
- **Cleanup:** hand-placed `GB_Dep_*` markers out of L_Slice (superseded by discovery-spawned visuals).

## 4. Playability strand **[agent-proposed, runs through every stage]**

M0 is played by scripts; a human cannot issue an order. Proposal: make the game hand-playable from M1-a and let the UI accrete with its systems, diegetic mission-control style (brief §6):

- **M1-a:** camera input wired (orbital↔ground zoom on the existing curve-driven pawn — "the zoom is the emotional register shift"), click-to-place build palette with coverage/link ghost preview, dig/survey designation, speed controls.
- **With each later stage:** uplink queue panel (+cancel) and power strip-chart (last 3 sols — the spec's "legible read at speed"); fleet panel (battery/wear/state) with the wear system; storm alert banner with storms; quota tracker + the **manifest composer screen** (signature mechanic #1 — the set-piece screen) with Q2; save/load slots.
- **Toolchain risk, flagged honestly:** UMG authoring via MCP is unverified. Plan A is C++-built UMG (compile-gated like everything else); first session of M1-a probes the toolchain and reports before UI scope is trusted.

## 5. Explicitly out of M1

Humans/colonists, needs/morale (M2). Radiation events, micrometeorites, pressure breaches (M2 threats — storms are M1's single environmental pressure). Robot destruction. Rivals/trade/Solidarity (M3). Terraforming visuals beyond the storm/dial work (Phase 3 arc). Sound design. Art pass beyond gray-box + sky/storm presentation. Procedural terrain (logged decision stands). Real heightmap terrain **stays deferred** unless the director promotes it — the flat plane still serves the loop **[agent: recommend deferring to the M2 map pass so M1 stays systems-focused]**.

## 6. Stage plan (reviewable increments, M0 cadence: build → scripted SIE verification → director review)

- **M1-a — Spine + hands.** Save/load, balance fixes, `CostResources` generalization, StateTree port (behavior-identical to M0-c), era-mode integrator + headless ledger run, camera/input + build-palette minimum. *Verify:* save→load→identical ledger; 100-sol headless run; hand-played build order.
- **M1-b — Fleet reality.** Wear/degrade/halt, RC-M repair loop, RC-S survey + deposit discovery, charging queue etiquette, fleet panel. *Verify:* scripted wear-crisis run (fleet degrades → parts crate spends → recovery); Ice_A dark until surveyed.
- **M1-c — World pressure.** `RH_Storms` + DustFactor through power/presentation/wear, era auto-drop on storm onset, ComputeModule + uplink queue UI, power strip-chart. *Verify:* scripted 3-sol storm at era speed — auto-drop fires, colony survives on bank + shedding, lag tier shifts mid-run.
- **M1-d — The exit.** Q2 + H₂ banking, Ore/Shielding/HabSegment chains, Habitat multi-resource build, manifest composer screen, Phase-1-exit end card. *Verify:* one hand-played (not scripted) run from a fresh landing to the exit card — the first time a human plays The Red Hope start to finish.

## 7. Confidence flags (honest uncertainty)

1. **UI is the least-proven axis** — MCP/UMG capability unknown; scope may shift toward fewer, plainer panels (flagged in §4; probe first, commit second).
2. **Storm severity numbers** (`DustFactor` floor, duration) are first-guess; the M0 power margins were tuned clear-sky — expect a solar-count rebalance.
3. **Wear pacing** (8 wear/active-sol ⇒ ~6 sols to degradation) may be too fast against Q2's 22-sol arc; single-knob fix in data.
4. **Save format churn**: serializing mid-batch/mid-haul state will find sim-state leaks; the versioned header is there so early saves can be broken without ceremony.
5. **Era-mode fidelity drift**: ledger rates vs agent-sim truth will diverge at the margins (charge pauses, haul latency); acceptance bar needed — proposal: ≤5% stock divergence over 10 sols, logged by a paired-run test.
