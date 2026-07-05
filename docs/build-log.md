# Build Log — The Red Hope

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
