# T1, the colony record: professor review r1 (2026-10-28)

**Reviewed:**
- `t1/t1-colony-design.md` (822 lines) and its evidence: `inventory_state.{md,csv}`, `inventory_access.md`, `access_scripts/`, `design/` and `rewrite/` (including `report.md`, which appeared during this review);
- `docs/qa/2026-10-20/territories-plan.md`, E7's design (REVISION 3), and the preflight briefs and rulings.

**Ground rules:**
- C++ was read from the frozen snapshot `t1/head` (`HEAD_COMMIT` 8da8c2a).
- Docs were read from the live tree, read only. Engine facts come from `/Volumes/Unreal/UE_5.8`, read only.
- Nothing was built or run, and no file was written except this one. The analysis scripts ran from stdin with bytecode writing off.

**Short names** (all in the snapshot):
- S `Source/RedHopeSim/Private/RHSimWorldSubsystem.cpp`; SH its header;
- M `RHSimMarket.cpp`; K `RHSimCommandlet.cpp`; TR `RHSimTransit.cpp`; RD `RHSimRoads.cpp`; RV `RHSimRovers.cpp`; AG `RHAgentSubsystem.cpp`.

## Verdict: CLEARED WITH CONDITIONS

Not buildable until conditions 1 to 6 (the MAJOR findings) are met. None of them needs a redesign.

**What holds.** The core mechanic survives every attack I tried:
- qualify in place, with one hoisted reference per body;
- `[this, &Colony]` captures;
- a nested, non-copyable record behind `TArray<TUniquePtr<>>`, made by `MakeUnique` in the constructor;
- a load that resets in place.

At 8da8c2a this preserves behaviour exactly:
- every reference keeps its type and constness, `mutable` included;
- nothing reads layout, sizes or addresses;
- no moved member is uninitialized, and no moved initializer has a side effect, so construction order cannot matter;
- `-ffp-contract=off` closes the route by which an inlining change could change float results.

Token identity plus a compile plus the battery is a sufficient proof for T1a.

**What fails:**
- the structural evidence for leaving the "?" members on the subsystem;
- the claim that there are no director questions;
- the value of the save frame against its cost;
- the corpus's coverage claim;
- the refresh's silent default;
- the build loop's file-time mechanics on the 8 GB Mac.

---

## Findings

### 1. MAJOR: the spine does not split along the T1 line; section 1 counts direct references only

`bodies.py` does not follow calls (it says so). Following them, with a call graph built over all 1,235 bodies using the inventory's own `members.py` and `methods.py` parsers, both directions are false.

**Player-wide steps write the colony:**
- `StepQuota` (S:2141), counted "0 C/DC", lands the ship into the colony. `ApplyManifestItemEffect` (S:2211, defined at S:2445) calls `SpawnRobotTracked` (S:2470) and `Debug_AddColonists` (S:2500, :2528). `ApplyHomeAgain` (S:7102) calls `AddColonists` (S:7120). In all, it reaches 68 methods and 39 C/DC members.
- `StepTrade` (M:1216) calls `StepConvoyReturn` (M:1276), which writes the Lander's `OutputKg` (M:1323) and `Stocks` (`AddStock`).
- `StepEarth` (S:10010) calls `IsColonyQuiet` (SH:2022), which reads `Colonists`.

**Per-colony steps write polity and world state:**
- `StepCityGrowth` (RHSimGrowth.cpp:476) runs the world market's sol roll: `RollMarket(SolNow)` at RHSimGrowth.cpp:489, beside `RollProductionLedger` and `RollEconomyLedger` (:487-488). `RollMarket` is M:516.
- `StepUplink` (S:1068) calls `ExecuteCommand` (S:9200), which calls `BookConvoy` (M:1140). Through such calls it reaches 54 "?" members.
- `StepHope` (S:6946) calls `GetColonyHope` (S:6452), which reads `ActiveCrisis` and `SolidarityHope`.
- `StepAgriculture` (S:4198) calls `GetHumanWorkTempo` (S:7658), which reads `ActiveCrisis`.

**The spine also interleaves:** the per-colony `StepPower` runs after the player-wide `StepQuota` (S:690-691 in the agent band, S:862-863 in the era band).

**Consequence.** These statements are false:
- "T3 inherits that split for free" (section 1);
- section 4's argument that putting "?" in the record would make T3 "misread" `StepEarth`, `StepQuota` and `StepTrade`;
- ruling 16.4's "along the line section 1 measured".

This does not break T1: a pure move is correct wherever the "?" members live. But it would enter `design-decisions.md` as rationale, and it misprices T3 and T4.

**Fix:**
- Show section 1's table with both direct and transitive counts, and delete "for free".
- Rest ruling 4 on "each member moves at most once, at its own gate", which still holds.
- List in the header comment block the splits T3 and T4 inherit:
  - `RollMarket` inside `StepCityGrowth`;
  - `ExecuteCommand`'s polity verbs;
  - `GetColonyHope`'s crisis and solidarity terms;
  - `StepQuota`'s landing into `ImportStock`, `Colonists` and the robots;
  - `StepConvoyReturn`'s drop at the Lander;
  - `StepPower` running after `StepQuota`.

### 2. MAJOR: "Director questions: none" (section 17) is wrong; three choices are the director's

**(a) The T1a/T1b split adds a gate, and with it a director review stop.**
- The plan lists T1 as one gate, with "a director look at the end of each" (territories-plan.md:42, :46).
- The director treats each gate as a review point.

**(b) The save is a reinterpretation of approved plan text.**
- The plan's T1 risk line reads "written as a list of one colony. Every existing field moves inside it" (territories-plan.md:76).
- The design keeps every field where it is and appends an 8-byte count-and-Id frame (sections 5.1-5.2).
- Section 5.2 calls the bump rule one "the director answered". The director answered Q1-Q4 (territories-plan.md:19); "T1 to T7 each bump the version" is plan text (:59).

**(c) Section 4's table states "final homes" that section 17 calls T4 director questions.** Crises, discoveries and Solidarity are given as player-wide.

**Fix:**
- Before any build, ask the director one plain-text lettered question covering (a) and (b), with finding 3 as the recommendation.
- Relabel section 4's column "proposed home (T4 decides)".

### 3. MAJOR: a simpler path: no 'RHT1' frame and no version bump in T1 (the E7 REVISION 3 precedent)

**Why the frame adds nothing in T1:**
- In T1 the frame can only ever hold (1, 0) (section 5.2).
- Robots are stamped from `GetColony().Id` whether or not the frame exists (section 5.3).
- The design itself says T4 "restructures the file anyway, under its own version" (section 5.1).

**This is the case already ruled on for E7.** The orchestrator dropped 'RHE7' because "it held no state the shipped game writes" and "its singular ColonyRegionId is the shape T1's colony record removes" (e7-region-ready-design.md:1000-1008, :1072).

**What keeping 'RHT1' costs:**
- vN, with version lines moving in 16+ logs;
- the +28 mode of `diff_battery_delta.py`;
- corpus shapes A and B, and the shape C that finding 8 adds;
- CO1, CO6 and CO7, and mutations 2 and 3;
- a checkpoint re-cut;
- the director's v60 saves refused.

All of that buys a frame with no content.

**Without the frame, T1b is inert:**
- `FRHColony::Id`;
- `FRHRobotFragment::ColonyId` (not saved);
- `IsDepotBuilding` and the position accessors;
- the per-colony scratch;
- the `-colony` suite.

All of T1 is then line-identical in the battery apart from the new `colony.log` and the three wall-clock lines, and the corpus is byte-identical for both commits. That makes one gate with two commits natural:
- T1a's identity battery and corpus become an internal checkpoint, run before T1b is applied;
- the playtest, the click test (×3), `-scale` (×3) and the director's look run once.

On this machine that saves a full verification round.

**Fix:** ask the director, as part of finding 2's question:
- (a) as designed;
- (b) recommended: no save change in T1, with the colony list arriving in T4's file restructure.

If (a) is chosen, findings 8 and 9 apply.

### 4. MAJOR: the corpus does not cover "every suite's save"; it keeps only the last writer of each slot name

Section 5.7 copies every `RH_*.sav` newer than one marker, once, after the whole battery, and calls this "the plan's 'the round-trip test covers every suite's save', made mechanical". As specified, the claim is false:
- **Autosaves collapse to one file.** Every autosave writes the slot `auto` (S:617), at every sol of every run. Those are the bulk of the 2,000+ `Colony saved` lines (S:13999), and the corpus keeps one of them for the whole battery.
- **Named slots are overwritten too.** Literal slot names repeat within a run (`foodtest` and `deepcommute_a`, twice each), and later runs overwrite earlier ones.

**Fix:**
- **Capture per run.** Behind an opt-in environment variable in `rh_battery.sh`: touch a marker before each `run_one`, and after it copy `RH_*.sav` newer than the marker to `<corpus>/<run>/`.
- **State the bar honestly.** The last write of each slot in each run is byte-compared. Every autosave is checked by size only, through `diff_battery_delta.py`.
- **Make Risk 6 executable.** Its remedy, "re-run the pre battery", needs the pre-T1 binary. Either run the pre-T1 battery twice before the first compile, which gives a determinism baseline for logs and corpus, or keep the pre-T1 dylibs (finding 6c).

### 5. MAJOR: the refresh must stop on unexpected members, not default them

**The problem.**
- Rule 11 ("? (stays), T1 default: unreviewed") keeps T1 pure, but lets per-colony state reach T4 on the subsystem.
- Worse, rules 4-7 (config, harness switch, load scratch, test instrument) put members in W heuristically. W rows are not in the header block that T4 audits. For example, a new per-colony derived counter that a load does not reset matches rule 7 and becomes invisible to T4.

**Answer to question 7: yes, block.**
- Any member that is not in the expected-additions table and that reaches rules 4-7 or rule 11 is UNCLASSIFIED.
- The refresh stops there. The tool prints the class it proposes and the rule that fired.
- The orchestrator records the class and a one-line reason in the gate entry. That costs minutes for a handful of members.

**Add a standing check for after T1:** `inv_classify.py --check` in the gate scripts, failing when a new subsystem member has no class. Then T2 and later gates cannot add per-colony state outside `FRHColony` unnoticed.

### 6. MAJOR: the build-and-fix loop on the 8 GB Mac needs four mechanics the design omits

**(a) Install by content.**
- "Fixes go into the tool" means re-running the tool on the snapshot. If its whole output tree is copied in, the header's mtime changes, and every tool fix becomes a full rebuild of both modules: UBT decides by file time (E5's finding, `design-decisions.md:1851`).
- Section 12's "a compile error confined to a .cpp costs one file and a link" then never holds.
- Install only the files whose bytes differ, and touch only those.

**(b) Cap parallelism.**
- UBA budgets 1.5 GB per action (`UnrealBuildAcceleratorConfig.cs:394`), which is about five actions on 8 GB of RAM.
- The adaptive pass compiles S (14,870 lines) outside its unity blob, while the simulator and other apps stay resident. E6 hit a load of 113 and a full swap that way (project memory, 2026-10-28 15:15).
- Pass `-MaxParallelActions=2` (or 3), and record peak swap.

**(c) Keep the pre-T1 binary.**
- Copy both dylibs, with their sha256, to `bin_pre_t1/` before the first compile, as E6 did with `bin_pre_e6/`.
- They are needed for any `-scale` A/B, and for judging a corpus or log difference (finding 4).

**(d) Gate the compile.**
- The refresh's rewriter run must report 0 unhandled sites, and the checker must report PASS, before `Build.sh` runs. `rh_t1_rewrite.py` lists shadows, init-captures, self-typed receivers, static-method uses and class-scope uses.
- Anything P1-P3 cannot express, for example a parameter named like a moved member, goes into a separate pre-T1 hand commit with its own identity battery, never into T1a.

**Budget and verdict on the discipline:**
- Count each "rebuild" as two compiles: `-DisableAdaptiveUnity`, then the normal build, which recompiles the 16 touched files outside their blobs.
- With (a) and (d), "fix in the tool, never by hand" is workable. The prototype is deterministic: "a second run reproduces out_A byte for byte" (`report.md`).

### 7. MINOR: the prototype needs three changes, not two, and the record's construction must be pinned

**The difference.**
- The prototype emits `TArray<FRHColony> Colonies = MakeColonyList()`, which uses `List.AddDefaulted()` (`rh_t1_rewrite.py:491-494`). That default-initializes the record.
- The design's `MakeUnique<FRHColony>()` (`UniquePtr.h:868-870`, which is `new T()`) value-initializes it. Value-initialization reproduces today's UObject zero-fill.

**Why it is moot today but not at E8.**
- At 8da8c2a, all 79 moved declarations that lack an initializer are class types, so the two agree.
- `FVector`'s default constructor is trivial (`TVector() = default`, `Vector.h:150`). A plain-data member added by E6-E8 would read heap garbage under `AddDefaulted` or `new FRHColony`.

**Fix:**
- Count the container and constructor as the tool's third change.
- Pin the constructor text in the checker: `FRHColony() = default;` (never `{}`) and `MakeUnique<FRHColony>()`, with a comment saying why.
- Declare the subsystem's constructor in the public section.

### 8. MINOR (only if 'RHT1' is kept): the corpus shapes and the truncation list are wrong

**Old-version fixtures match neither shape.**
- They write a literal version, so bytes 4-7 are equal before and after; they do not "differ by N − 60". The sites:
  - suites: `RHSimCreditSuite.cpp:654`, `RHSimCavernSuite.cpp:788`, `RHSimCurrencySuite.cpp:1780`, `RHSimDepthSuite.cpp:831`, `RHSimExportSuite.cpp:599`, `RHSimDeepDistrictSuite.cpp:815`, `RHSimEconomySuite.cpp:878`, `RHSimTramSuite.cpp:762`, `RHSimMarketSuite.cpp:1380`, `RHSimTransitSuite.cpp:1740`;
  - K: :10870, :12143, :12607, :13755, :14560.
- Add shape C: bytes 0-7 equal, `post[8:len(pre)] == pre[8:]`, and the last 28 bytes are the frame.

**The truncation list is incomplete.**
- Sections 5.3, 5.7 and 11.8 name only three truncated fixtures. There are at least three more: `depthr12` (`RHSimDepthSuite.cpp:828`), `transith17` (`RHSimTransitSuite.cpp:1720`) and `tunnelsh10` (K:14556).
- Define shape B mechanically, not by a list: the same length, and only bytes 4-7 differ.

### 9. MINOR (only if 'RHT1' is kept): 'RHT1' becomes the entry point of every walk from the end

**Why.**
- `FindBack` stops at the first footer whose header does not answer it (`RHSaveFrame.cpp:33-55`).
- D7 reads 'RHD7' early, in the middle of the body, with a non-moving read that falls back to that walk (`D7-brief.md`, section 2.10).

**Consequence.**
- A header-only tag edit, or damage to the footer of 'RHT1', also loses 'RHD7', with D7's Warning; so does D6e's block if D6e reads early.
- A bad CRC is harmless, and so is a consistent tag edit in both header and footer.
- So CO6's "a destroyed tag loads with 0 Warnings" holds only for the consistent edit.

**Fix:**
- Specify which edit CO6 makes, and add a header-only case that pins the collateral loss.
- Keep "silent" for T1; it is consistent with rulings 2 and 10. Record that T4 must make a lost frame loud once the frame carries data.
- Give the suite a non-logging found/lost readback, so the silent path stays testable.

### 10. MINOR: the `-scale` relative gates cannot see T1's cost

**Why the gates are blind:**
- **The agent step is too short for the margin.** It takes about 0.3 ms at N=5000 (E1's ledger timing on that city, `design-decisions.md:1298`), so "+0.2 ms" allows roughly 60%.
- **E5's line barely touches T1.** It reads market state, which section 2.8 itself concedes.
- **The absolute budgets are flaky already.** Some fail under load today: a cold commute at 10.45 and 10.73 ms against 10, and a cold deep net at 4.00 ms against 3 (`design-decisions.md:1298`, :2050).

**Fix:**
- Record the before and after medians of every `-scale` timing line, and flag any line over +10% for a look.
- If an absolute budget fails only after T1, settle it with an A/B against `bin_pre_t1/` (finding 6c), not with re-runs alone.

### 11. MINOR: section 2.7's checker contract omits the guard that makes P2 safe

**The guard.**
- P2 inserts `Colony.` before any bare identifier on the MOVE list. That is sound only if no parameter, local, lambda parameter or structured binding carries a moved member's name.
- The prototype checks this (`local-named-like-moved`, `rh_t1_check.py:248-251`). The design's contract does not list it.

**Why `-Wshadow` is not enough.**
- Section 2.2's "shadowing would not compile" rests on `-Wshadow` (`CppCompileWarnings.cs:90`).
- That is solid for locals. For member-function parameters, clang probably does not flag a parameter that shadows a field (not verified here).

**Fix:** make the guard a named item of the checker's contract.

### 12. MINOR: T4 couplings to add to the header block

**Founding is mixed into world start.** `OnWorldBeginPlay` both loads config and founds the colony:
- `BuildEconSlots` (S:103);
- `OrderLagSeconds` (S:106);
- `HopeSmoothed` (S:513);
- the Lander (S:547);
- the starting fleet (S:589).

A second founding must replay that part (`report.md`, "What a compile would not catch").

**Ids become colony-scoped.**
- E7's `DepotBindings` (W, a harness hook) maps a region to a colony-scoped building Id.
- Every `Next*Id` counter moves into the record, so Ids become per colony from T4.
- Presentation keys its state by building and colonist Ids.

---

## The nine questions, answered briefly

**1. Completeness.** The sim-side inventory is complete:
- **Statics.** The only mutable statics are `Scratch` (TR:872), `LastMatched` (`RHRobotBrainProcessor.cpp:124`) and the brain-mode CVar (AG:14).
- **Processors** hold only their queries.
- **The agent subsystem** holds `RobotHandles`, the archetype handles and `SpawnedCount` (`RHAgentSubsystem.h:113-123`).
- **The definitions subsystem.** Its only play-time mutation is `ActivateRoomRow` (`RHDefinitionsSubsystem.cpp:167-172`). The crop, furniture and quota row activations are harness or cheat only (callers K:1708, :4237, :12394, `RHCheats.cpp:1328-1354`).
- **Presentation** holds no pointers into sim containers. Its mutable statics are log latches and click-test state (`RHAgentVisualizerSubsystem.cpp:1413`, `RHColonyVisualizerSubsystem.cpp:2888`, `RHClickTest.cpp:2385-2386`).
- **The CDO** is never read (no `GetDefault<URHSimWorldSubsystem>`).
- **Accidental sharing at T4:** the Ids (finding 12), the `DT_Rooms` flips (section 9, correct) and `Scratch` (section 3.5, correct).

**2. The proof.** Sound for T1a; see the next section.
- **Object lifetime.** The record exists from the constructor, before `Initialize` (S:76) and `OnWorldBeginPlay` (S:96). `Initialize` and `Deinitialize` touch only W (S:76-94).
- **The vtable-helper constructor** leaves `Colonies` empty, but that object never runs a method.
- **Initializers.** No moved initializer crosses the boundary. They are literals, `ZeroValue`, `INDEX_NONE`, `MAX_uint32`, `ERHHopeBand::Steady` and `{ FRHShaft() }`.
- **`ensure`.** The one with a moved member (S:10886) changes only its failure text.
- **UHT and GC.** No `UPROPERTY` moves. `TUniquePtr` comes in through `CoreMinimal.h:113`, and no UObject lives in the record.
- **Hot-path cost.** One bounds check per method call, none per access.

**3. The save.**
- **The +28 rule.** If 'RHT1' is kept, +28 holds for every `SaveColony` output and every copy patched in place. It does not hold for truncated fixtures or literal old-version fixtures (finding 8).
- **The walk from the end** is generic (`RHSaveFrame.cpp:33-55`), and no suite asserts that a frame ends the file. EC1's comparison of the file's tail (`RHSimEconomySuite.cpp:491`) still holds, because 'RHT1' is identical in both twins.
- **No trailing-byte check.** No load path checks bytes after the last frame. The `TotalSize() - Tell()` bounds (for example S:5686 and S:8328) only widen by 28 bytes, and every doctored count near them is either huge (`RHSimTramSuite.cpp:747`, `RHSimDeepDistrictSuite.cpp:798`) or a truncation.
- **A silently lost frame** is acceptable for T1, with finding 9's caveat.
- **Load in place.** The teardown resets each field in place (S:14085-14104). Keep it.

**4. The split.**
- **T1a is maskless.** No log line prints an address (no `%p` anywhere), a `sizeof` (every use is a hash or a memset), a member count or an allocation size.
- **T1b's scope.** Its content belongs in T1: the plan's T1 row names the robots and the Lander's roles. Its save half is the doubtful part (finding 3).
- **Section 10 is exact.** `NAME_LanderRoad` and `NAME_LanderRover` are both "Lander" (RD:14, RV:18). Both deploy-ring sites compute in float through `const float Angle` (S:574-575, TR:2001-2002), so one accessor is bit-identical under `-ffp-contract=off` (`MacToolChain.cs:209`).

**5. Robots.**
- **Leaving.** The only `DestroyEntity` is in `DespawnAllRobots` (AG:463), whose one caller is the load (S:14088). No code adds or removes a tag or fragment, and there is no scrap or retire path.
- **Spawning.** Robots come only from the starting fleet (S:576), manifest items (S:2470), rover deploys (RV:326), load respawns (S:14208) and the DeepBench cheat (`RHCheats.cpp:1999`).
- **Chunk capacity.** No suite approaches one chunk. `-scale`'s "1,000 robots below" are synthetic handles with a local fragment (K:16461, :16475), not entities.
- **Who carries the fragment.** Colonists are not Mass entities, and presentation only reads the fragment.
- **Both brain paths are stamped.** They copy the same `RobotFrag` (AG:98-107, :156, :177), so the stamp covers the legacy archetype too.

**6. Structure.**
- **The claim is false transitively** (finding 1).
- **Where the "?" members live paints no gate into a corner.** The real constraint is the call structure, which T3 must split whatever T1 does.
- **T4 must name the capital.** Hoists inside player-wide callees (`StepConvoyReturn`, `ApplyManifestItemEffect`) bind them to colony 0; T4's per-body audit must give them the "capital" intent.

**7. The refresh.** Yes, block (finding 5).

**8. The build loop.** Feasible with finding 6's four additions.

**9. The director.** Yes, the design decided things the director owns (finding 2).

## Checked and found sound

- **Mechanic (i) over (ii).** Outside code reaches only methods and the 18 delegates, so it needs 0 outside edits. The hoist makes T4's audit one decision per body.
- **Names.** `Colony`, `GetColony`, `FRHColony`, `Colonies` and `IsDepotBuilding` collide with nothing in RedHopeSim. RedHope's `Colony` locals and the enumerator `RHFrameStats::Colony` (`RHFrameStats.h:11`: namespaced, never brought in by a using-directive) are not visible in subsystem bodies.
- **Lambdas.** All 11 explicit `[this]` lambdas are locals or comparators consumed within the call (S:530, :4821, :4871, :4994, :6287, :7471, :7489, :10675; `RHSimCommute.cpp:310`, `:353`; `RHSimDeepWalk.cpp:718`). The only stored bind captures `[&Rep]` (S:3887). There is no `[=]`.
- **Types and constness.** A `const FRHColony&` hoist in a const method gives exactly today's types, `mutable` included. RedHopeSim has no `const_cast` of the subsystem, and no `decltype`, `offsetof`, `GET_MEMBER_NAME`, `#if` or macro.
- **Compile settings.**
  - FP contraction is off on Mac (`MacToolChain.cs:209`), so an inlining change cannot change a float result.
  - Unused-variable and unused-capture warnings are off by default in 5.8 (`CppCompileWarnings.cs:569-573`), so a hoist that is unused in some configuration cannot break that build.
- **Construction.** Value-initialization through `MakeUnique` equals the UObject zero-fill. Every moved struct type (`FRHPowerState`, the commute and district caches, `FRHDeepNet`) initializes all of its fields.
- **Addresses never reach behaviour.** No pointer-keyed container holds subsystem state; the two `TMap<const FRHRoomRow*>` maps (S:6489-6490) are function-local caches keyed by DataTable rows. No address, and no hash of one, reaches a log line or a roll.
- **Determinism of saves.** Saves are deterministic for a given binary. Neither sim code nor any battery suite branches on wall-clock time; `FPlatformTime` is used only to measure.
- **The record's storage.** `TUniquePtr` storage, the non-copyable record and the load in place are right for T4's address stability. The self-pointer stays W (S:295; the address test at K:10120).
- **The battery's timing lines.** `-scale` is not in the battery (`rh_battery.sh:37-39`), so the battery has only the three known wall-clock lines.
- **The scratch's behaviour is unchanged.** `SurfaceRoute`'s per-colony scratch keeps today's behaviour: the function resets its target on every use (TR:873-874). Its only callers are TR:887 and TR:908.

## Not verified

- Whether clang's `-Wshadow` diagnoses a member-function parameter that shadows a field (finding 11). The checker guards it either way.
- The 39-minute link stall the design cites from E6's entry: not found in the live docs.
- That UHT accepts the nested struct and a user-declared constructor. Only the compile can show it.
- The chunk-capacity figures (inferred in the design). They do not matter here: no suite approaches one chunk.

## Conditions

1. Correct sections 1, 4 and 16.4 (finding 1).
2. Ask the director the lettered question before any build: the split, the save line, and the frame against no frame (findings 2 and 3).
3. Capture the corpus per run, state its coverage honestly, and make Risk 6 executable (finding 4).
4. Make the refresh stop on unexpected members (finding 5).
5. Install by content, cap parallelism, keep the pre-T1 dylibs, and gate the compile on 0 unhandled sites (finding 6).
6. Pin the record's construction, and bring the prototype into line with the design (finding 7).

If 'RHT1' is kept, findings 8 and 9 also apply.

VERDICT: CLEARED WITH CONDITIONS
