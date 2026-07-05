# Build Log — The Red Hope

## 2026-07-05 — Session 7 (M0-b: production spine — dig/haul/build/smelt, quota)

- **Systems (compiled in 2 passes; 1 header omission fixed):** sim world is now the single sub-step driver (uplink → task board → production → power). Deposits load from `DT_Deposits`; `RH.Dig` designates through the uplink. Task board posts haul work by recipe demand and build work from timed construction. Robots are task-driven Mass agents (`FRHRobotFragment`/`FRHTaskFragment`, claim→travel→work→deliver in `URHRobotTaskProcessor`); starting fleet spawns from `DT_Robots.StartingCount` on the first sim step. Struct costs deduct colony-wide; import-only hardware consumes starter flat-pack stock. Production runs recipes on the hybrid logistics split (solids in hoppers, fluids to the network pool); batches stall on power deficit; active-vs-idle draw feeds the ledger. Quota progress vs Q1 in `RH.Status`.
- **Smoke test 1 (unpowered, scripted BP_M0bTest):** the full chain ran — fleet ×7, designation, dig at exactly 2×50 kg/h (128 kg per 64 sim-s measured), haulers skimming piles to the Lander, Forge cost 250 kg (400→150), fabricator worked the 400 s site down, hopper fed, batch started — **then the colony hit `gen 47 W load 820 W battery 0 DEFICIT` and the batch froze at 0.82 h remaining.** The test built no solar; the deficit-stalls-production rule fired exactly as specced. "Every watt matters" emerged un-scripted in the first run.
- **Smoke test 2 (powered, BP_M0bTest2):** 2 solar + battery bank + Forge through the uplink; fabricator built all four sites serially; battery cap 2000→6000 Wh; sunrise charging (1879→4788 Wh); Forge fed and **self-sustaining: 4+ batches completed and auto-restarted (Regolith:100 → Struct:25 each), quota Struct 150→225/300 kg and climbing**, with the 825 W load bridged by the bank against the falling afternoon curve — no deficit.
- Capture: `docs/media/m0b-colony-running.png` (soft — TAA never converges at 8× sim speed; flagged for the look pass). BP_M0bTest/BP_M0bTest2 kept as regression harnesses.
- Known M0-c scope: robot charging etiquette (drained robots currently halt for good), material delivery to construction sites, load-shedding by priority, ISRU chain smoke test (drill/plant/electrolyzer), quota completion → manifest → ship arrival.

## 2026-07-05 — Session 6 (Mars sky pass + M0-a: definitions, power, territory)

- **Mars sky (director directive, rover-referenced):** VolumetricCloud + template SkySphere deleted (no clouds on Mars). SkyAtmosphere inverted from Earth: thin warm Rayleigh (butterscotch dust dome), cool high-anisotropy Mie lobe (blue sunset aureole per rover photos), reddish ground albedo, warm dust fog, 0.357° solar disk at 7 lux. Captures: `docs/media/mars-sky-midday.png`, `mars-sky-sunset.png`. Logged as the habitability-0 endpoints for the dial curves.
- **M0-a systems (compiled clean, 22.5 s, first try):** `URHDefinitionsSubsystem` (DT/CT registry; sole owner of `CanFabricateLocally`); sim world rewritten as tickable colony owner — data-driven buildings, power ledger (gen × `CT_SolarDiurnal`, idle loads, battery Wh clamp, deficit flag), coverage-disc territory with Build rejection, uplink queue pumped at sub-step boundaries; clock refactored to published per-frame step counts (multi-reader timeline; single-driver refactor queued for M0-b); colony visualizer (gray boxes from sim events + coverage disc debug draw + on-screen rejection toasts); console commands `RH.Build` / `RH.Status` / `RH.Lag`. Hand-placed gray-box colony + StressDriver removed from L_Slice (sim spawns the colony; BP kept as benchmark harness).
- **M0-a smoke test (SIE, scripted via BP_M0aTest — kept as regression harness):** all six checks passed in one run:
  1. Bootstrap: Lander #1 at (0,0); lag 45 sim-s from DT_Config; sol length 1200.
  2. Night power: gen 0 W at sol 0–17%, battery draining at exactly load × sol-hour rate (23 Wh over 48 sim-s at ~25 W — units doctrine verified).
  3. Uplink: Build Pylon queued visibly ("executes at t=56, now 27"), landed after 45 sim-s.
  4. Territory chaining: SolarArray at (50,0) accepted only because the new pylon's 40 m disc covers it.
  5. Territory rejection: Stockpile at (200,0) traveled the 45 s uplink and was rejected on arrival — "Outside grid coverage - extend pylons first."
  6. Sunrise: at sol 37%, gen 278 W (curve × panel + lander trickle), battery charging 889→1256 Wh.
- Next (M0-b): single sim driver ordering, Struct costs + build time, dig/haul/production loops, robots as task-driven agents, deposit visuals from data, quota tracking.

## 2026-07-05 — Session 5 (Step 4 scaffold, part 2: content, benchmark, smoke test)

- **Content built via MCP:** 8 DataTables imported from `docs/data/RH_*.csv` against the compiled `FRH*Row` structs (row values spot-verified; `ImportOnly`/`UnlockTech` live). `CT_SolarDiurnal` (diurnal curve), `CT_AtmosphereDial` (FogDensity, SunIntensityMul), `CT_CameraRig` (Distance/Pitch/FOV) populated. `MPC_Atmosphere` (Habitability/TimeOfSol/DustAmount/SkyTint) + `M_MarsDial` MPC-driven material. Sun tagged `RH.Sun`.
- **Gray-box L_Slice:** Mars ground plane (5×5 km) + lander, 3 solar arrays, battery, pylon, charge pad, Forge, stockpile, 6 deposit markers at `RH_Deposits` coordinates. **Gotcha:** duplicating the engine OpenWorld template does not bring its World Partition landscape proxies (external actors stay under `/Engine`); the orphaned Landscape actor was removed and a plane used instead — real terrain is an M0 map-pass task.
- **Console-exec gap workaround:** MCP has no console-command tool, so `BP_StressDriver` (authored via the Blueprint graph DSL) fires the benchmark sequence from BeginPlay: baseline → 200 agents → 8× → 500 agents → 8×, via `RH.SpawnDummies` / `RH.SetSpeed` / `RH.Benchmark`.
- **Hardware benchmark (director-required) — PIE Simulate, in-editor viewport, Lumen defaults, this Mac (arm64):**

  | Phase | avg ms | fps | p95 ms | worst ms | memory |
  |---|---|---|---|---|---|
  | 0 agents, 1× (baseline) | 46.29 | 22 | 50.73 | 58.08 | 6,214 MB |
  | 200 agents, 1× | 46.73 | 21 | 51.51 | 56.95 | 6,217 MB |
  | 200 agents, 8× | 46.91 | 21 | 51.44 | 58.75 | 6,222 MB |
  | 500 agents, 1× | 47.77 | 21 | 52.47 | 202.82 | 6,233 MB |
  | 500 agents, 8× | 47.91 | 21 | 88.67 | 190.05 | 7,342 MB |

  **Reading:** agent cost is in the noise — +1.6 ms at 500 agents × 8× sub-stepping (≈4,000 agent-updates/frame-equivalent) over an empty world. The 46 ms floor is the in-editor Lumen viewport on this hardware, not the sim. Memory +19 MB for 500 agents+instances (the 1.1 GB jump in the last phase coincides with the HighResShot capture, not agents). The p95/worst spikes at 500 appear at capture/GC moments. **Conclusion: population targets for M2–M4 (hundreds of agents) are not hardware-limited on this machine; the budget pressure is rendering quality settings, which are tunable.** Numbers are in-editor Simulate — packaged builds will run faster; deltas are the architecture signal.
- **Smoke test:** PIE-Simulate ran the full 95 s sequence unattended; sim clock speed tiers exercised (1×/8×); Mass wander processor + battery drain sub-stepping live; ISM visualizer tracked 200→500 entities; atmosphere subsystem wrote MPC + drove the tagged sun. Captures: `docs/media/scaffold-graybox-editor.png` (first layout), `scaffold-graybox-grounded.png` (grounded pass), `scaffold-sie-500-agents.png` (live Simulate, 500 agents).
- Editor sky is still Earth-blue (template sky rig); Mars-ifying the sky/fog via the dial curves is the M0 look pass, deliberately not scaffold scope.

## 2026-07-05 — Session 4 (Step 4 scaffold, part 1: repo, sources, template removal, first compile)

- `git init` (branch main); baseline commit `557d902` = pristine pre-scaffold restore point (director requirement: lands before any deletion).
- Wrote Source tree: `RedHopeSim` (sim clock w/ fixed 0.1 s sub-steps + speed tiers, sim-world subsystem w/ stocks ledger + uplink latency queue, Mass fragments + dummy-agent spawner, wander processor with per-sub-step integration, DataTable row structs incl. `ImportOnly`+`UnlockTech` per directive, commandlet stub) and `RedHope` (GameMode/GameState/PC, strategy pawn w/ exponential zoom mapping, atmosphere subsystem [MPC writer + `RH.Sun`-tagged sun driver + `RH.Habitability` scrub CVar], ISM agent visualizer, `RH.SpawnDummies`/`RH.SetSpeed`/`RH.Benchmark` console commands).
- All Mass API usage verified against the 5.8 engine headers before compiling. 5.8 restructured Mass: **MassCore is engine-core** (`Runtime/Mass/MassCore`, owns `FTransformFragment` + `FMassEntityHandle`); `ConfigureQueries` now takes `TSharedRef<FMassEntityManager>`; `ForEachEntityChunk` dropped its EntityManager arg; the **processing-phase ticker (`MassSimulationSubsystem`) is in the MassGameplay plugin** — hence MassGameplay is required for agents to tick at all.
- Director-approved deletions executed with `L_Slice` open (never the open level): `Content/TopDown`, `Characters`, `LevelPrototyping`, `Cursor`, and their `__ExternalActors__`/`__ExternalObjects__` subfolders. `L_Slice` created by duplicating the engine OpenWorld template (World Partition) — MCP has no new-level tool. Config retargeted (startup/default map → `L_Slice`, GlobalDefaultGameMode → `/Script/RedHope.RHGameMode`).
- `.uproject`: added Modules (RedHope, RedHopeSim), enabled MassGameplay (director-approved, one restart).
- Session moved from the in-editor Terminal plugin to an external terminal for the build (editor quit kills in-editor sessions; `.mcp.json` → `http://127.0.0.1:8000/mcp` reconnects externally).
- **Compile: success** (red_hopeEditor, Development, arm64; 32 s; 2 iterations). Gotcha for the record: `FMassEntityHandle` needed an explicit `#include "Mass/EntityHandle.h"` — not reliably visible via `MassEntityTypes.h` in a fresh TU. `ExecutionFlags` is `uint8` in 5.8.
- Next (pending director reopening the editor): CSV→DataTable imports, MPC_Atmosphere + curves + sky rig, gray-box slice map, 200/500-agent benchmark @1×/8× (frame ms + memory, logged here), PIE-Simulate smoke test + captures.

## 2026-07-04 — Session 3 (Step 3: architecture + toolchain stress tests)

- Logged Step 2 approval + import-only-is-removable directive in design-decisions.md.
- Stress-tested MCP toolchain in-editor (all artifacts under /Game/RedHope_StressTest, deleted after):
  - MPC pipeline verified end-to-end: created MPC_DialTest, wrote scalar+vector params via ObjectTools (auto-GUIDs confirmed via round-trip read), created M_DialTest, added CollectionParameter expression, bound to collection + wired to BaseColor, recompiled clean. **The atmosphere-dial backbone is fully MCP-buildable.**
  - CurveTable created + solar diurnal keys set/verified. CSV import path exists.
  - DataTable create works but requires existing C++ FTableRowBase structs (no struct-creation tool; registry shows engine structs only) → workflow: agent writes C++ structs → director compiles → agent imports CSVs.
  - Blueprint create (arbitrary parent) + compile verified; graph DSL available for BP logic.
  - BlueprintTools describe overflowed context limits; worked from saved-file extraction.
- Engine facts verified: MassEntity is engine-core in 5.8 (Engine/Source/Runtime/MassEntity); MassGameplay + MassAI plugins present but not enabled; GameplayStateTree already enabled in uproject.
- Delivered `docs/ue-architecture-proposal.md`. Stopped for director review. Editor left clean (stress-test folder deleted).

## 2026-07-04 — Session 2 (Step 2: M0 spec)

- Logged director approvals in `docs/design-decisions.md`: all eight §9 recommendations, map model (formally the ninth question), hybrid logistics, Hope as derived index, silent identity-axis accumulation.
- Delivered M0 vertical-slice spec: `docs/m0-vertical-slice-spec.md` — units/clock doctrine, two-timescale model (agent band 1×/3×/8× + specced 60× era-mode aggregate integrator), expansion-headroom decision (World Partition + authored sectors through v1), power/territory model, 5 robot types, Forge and ISRU chains, quota Q1, manifest catalog, slice map, confidence flags.
- Authored 9 DataTable-ready CSVs under `docs/data/` (config, resources, buildings, recipes, robots, manifest items, quotas, deposits, solar diurnal curve). Dormant M1+ rows included so later milestones activate content, not schema.
- Stopped for director review. No editor/project assets touched this session.

## 2026-07-04 — Session 1 (kickoff)

- Verified Unreal MCP toolchain: 20 toolsets enumerated and reachable. Engine confirmed UE 5.8.0 (Release-5.8, CL 55116800). Editor live with `/Game/TopDown/Lvl_TopDown` open.
- Round-trip test: spawned a StaticMeshActor `MCP_Handshake_DeleteMe` into the level via MCP, then deleted it. Level not saved; no persistent change.
- Design brief was **not present in the project tree**; located at `~/Downloads/files/the-red-hope-design-brief.md` and copied to `docs/the-red-hope-design-brief.md`.
- Seeded `docs/design-decisions.md` and this build log.
- Delivered Step 1 (digest, gaps, open-question recommendations) — archived at `docs/step1-digest-and-recommendations.md`. Stopped for director review per working rules.
- Noted: project is not a git repository. Recommended `git init` before any scaffold work (Step 4).
- No project assets or settings were modified this session.
