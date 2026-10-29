# T1, the colony record: design, r2 (2026-10-28)

Step 4 (territories), gate T1. **Design only; nothing here is built.**

## r2: what changed

This revision answers `t1/t1-review-r1.md` (CLEARED WITH CONDITIONS: 0 blocking, 6 MAJOR, 6 MINOR), under the coordinator's rulings, and reads `t1/rewrite/report.md` (the prototype's final report).

| Finding | Ruling | What changed in r2 |
|---|---|---|
| **F1** the spine does not split along the T1 line | accepted | Section 1 now shows direct and transitive counts, from a new call-following script (`t1/design/callgraph.py`). "T3 inherits that split for free" is deleted. Sections 1 and 4 now say plainly that today's spine mixes colony state and player-wide state in both directions; T3 and T4 must design that split, and T1 does not fix it. Section 4 and ruling 18.4 now rest on "each member moves at most once". |
| **F2 + F3** director-owned choices; the simpler path | accepted, as one director question | A new **DIRECTOR QUESTION** section, near the top. The body is now written for option **(a)**: one gate, no save change, the pure move proven at an internal checkpoint, one commit. Section 15 holds the deltas for (b), the frame as the plan says, and (c), the two gates of r1. Section 4's homes are marked provisional. |
| **F4** corpus coverage | accepted | Section 5.4: per-run save capture after every battery run, from an orchestrator tooling commit made before T1 (`rh_battery.sh` is in E6's uncommitted set). The coverage is stated with numbers from `battery_post_e5`. The pre-T1 dylibs are kept with their sha256, with a protocol for re-running the pre-T1 battery. |
| **F5** the refresh's silent default | accepted | Section 11: the refresh blocks on every UNCLASSIFIED or heuristically classified new member until the orchestrator records a class and a reason. A committed classification CSV and a check script run in every later gate's spec lens. |
| **F6** the build loop | accepted | Section 12: install only changed bytes; `-MaxParallelActions=2`; the pre-T1 dylibs aside with sha256; compile only after the rewriter reports 0 unhandled sites and the checker passes. Each rebuild counts as two compiles. |
| Tool alignment (coordinator) | — | New section 14: the exact rewriter and checker changes from the prototype (`RC`/`Col()`, `Col().M` in lambdas, `TArray<FRHColony>` with `AddDefaulted`) to this design. Section 2.3 adds the stored-lambda rule, and answers creation-time capture against call-time lookup. |
| F7-F12 (MINOR) | — | Fixed or answered in section 19's table. |

---

**Provenance.**
- Drafted at `8da8c2a` (save v56, E5 built) while E6 was building in the live tree; r2 the same day.
- The C++ was read from a frozen snapshot of that commit (`t1/head/`, `git archive` of `Source/` and `scripts/`; `HEAD_COMMIT` says `8da8c2a`). The docs and one battery baseline (`Saved/baselines/battery_post_e5`, for counts and timing) were read from the live tree, read only.
- Nothing in the repo was edited, built or run.
- **This design must be refreshed at E8's HEAD before any build** (section 11). E6, D6e, D7, E7 and E8 all build first and add state, frames and Lander sites. Every `file:line` below is at `8da8c2a`; the builder re-anchors by symbol.

**Short names** (paths relative to the repo root):
- **S** `Source/RedHopeSim/Private/RHSimWorldSubsystem.cpp`; **SH** its header `Source/RedHopeSim/Public/RHSimWorldSubsystem.h`.
- **K** `Source/RedHopeSim/Private/RHSimCommandlet.cpp`; **AG** `Source/RedHopeSim/Private/RHAgentSubsystem.cpp`.
- **M** `RHSimMarket.cpp`, **TR** `RHSimTransit.cpp`, **RV** `RHSimRovers.cpp`, **RD** `RHSimRoads.cpp`, **G** `RHSimGrowth.cpp` (all in `Source/RedHopeSim/Private/`).
- The inventory classes are written **C** (per colony, moves), **DC** (derived cache, moves), **W** (world level, stays) and **?** (judgment call, stays).

---

# DIRECTOR QUESTION (T1, the colony record; to be answered before any build)

T1 is invisible. It moves each colony's state into a record of its own, so that a second settlement can exist later. Nothing changes on screen. One choice is yours: what T1 does to the save file, and whether it is one gate or two.

**(a) Recommended: one gate, with no save change.**
- The file format stays exactly as it is, so your current saves and checkpoints keep loading.
- The "list of one colony" in the save moves to T4, which restructures the file anyway. This is the same reasoning that dropped E7's save block.
- Every existing battery line stays identical, apart from the three wall-clock timing lines that vary on every run. There is one new test log, and every captured save file stays byte-identical.
- The pure move is still proven on its own, at an internal checkpoint before the rest is added.
- Cost: 1 commit, 1 review stop, 2 full rebuilds, 2 battery runs (about half an hour each), and 1 round of playtest, click test and `-scale`.

**(b) As the plan says: one gate that also writes a 28-byte "list of one colony" block and bumps the save version.**
- About 33 version lines in 19 test logs change, and so do 12 file-size lines, each by exactly 28 bytes.
- Your v60 saves and both checkpoints stop loading; the checkpoints are re-cut.
- The block can only ever say "one colony, number 0" until T4.
- Cost: 1 commit, 1 review stop, 2 full rebuilds, 2 battery runs, 1 verification round, plus the re-cut.

**(c) Two gates:** the pure move first, then the rest, with the block as in (b).
- Cost: 2 commits, 2 review stops, 2 full rebuilds, 2 battery runs, and 2 verification rounds.
- The second gate moves the same lines as (b).

**Risk.**
- (a) is the lowest: the format does not change.
- (b) and (c) add a block with no content until T4. A damaged block at the end of a file would also hide the atrium data from D7's early read.
- (c) lets you review the pure move alone, at the cost of an extra stop and an extra verification round.

**The numbers behind the options** (for the record; section 15 has the detail):

| | (a) recommended | (b) frame, one gate | (c) two gates |
|---|---|---|---|
| Battery lines that change, in existing logs | **0**, apart from the 3 wall-clock timing lines; plus 1 new log, `colony.log` | about 33 version lines in 19 logs (29 in 16 at E6's recount, plus the refusal lines of the levers, transit-power and atrium suites); 12 whole-file size lines (5 unmasked), each +28 exactly; the size printed by every one of about 2,300 saves, +28 (masked) | gate 1: 0; gate 2: as (b) |
| Saves | every captured save byte-identical; v60 saves and the checkpoints still load | every save +28 bytes; v60 saves and both checkpoints refused, checkpoints re-cut | as (b), after gate 2 |
| Review stops / commits | 1 / 1 | 1 / 1 | 2 / 2 |
| Full rebuilds (2 compiles each) | 2 | 2 | 2 |
| Battery runs (about 27 minutes each at E5's 59 runs) | 2: the checkpoint after the move, then the final | 2 | 2 |
| Verification rounds (playtest, click test, `-scale` three times) | 1 | 1, plus the checkpoint re-cut | 2 |
| `-colony` mutation builds (one file each) | 4 | 6 | 6 |
| Risk | lowest: no format change | a frame with no content; damage to its header also hides `'RHD7'` from D7's early read (F9) | as (b), plus one stop; the pure move is reviewed alone |

- The battery time is measured from `battery_post_e5`'s file times (22:12 to 22:39, 59 runs).
- Every option also needs the pre-T1 baseline battery (section 5.4), unless the gate just before T1 left one captured on the same binary.

---

**Sources:**
- `docs/qa/2026-10-20/territories-plan.md`: the gates table (:40-55, "a director look at the end of each"); "Risks per gate" (the general rules, T1 at :72-76, T3, T4); "Saves" (:59); Q1a-Q4a; the appendix.
- The groundwork in `t1/`:
  - `inventory_state.md` and `.csv` (606 members classified, today's save, the battery's size and version sites);
  - `inventory_access.md` with `access_scripts/`;
  - the prototype `rewrite/`, with its `report.md`;
  - the review, `t1-review-r1.md`.
- E7's design (`docs/qa/2026-10-25/e7-region-ready-design.md`, authority REVISION 3; the frame's withdrawal at :1000-1008 and :1072) and `docs/qa/2026-10-26/preflight/E7-brief.md`.
- The briefs of the gates queued before T1 (`E6-brief.md`, `D6e-brief.md`, `D7-brief.md`, `E8-brief.md`), `ORCHESTRATOR-RULINGS.md` and `CROSS-CHECK.md`.
- `docs/design-decisions.md`: "The grand vision" (:396); "Step 4, territories: Q1a-Q4a" (:603); E1's `-scale` timing (:1298); E5's entry (:1785, its `-scale` record at :1899); E6's entry as it is being written (:1931ff).
- `docs/qa/2026-10-20/rising-plan.md:42`: R1's planet record "above the colonies (T1)".
- The measurement scripts, all read-only: `t1/design/bodies.py`, `lambdas.py` and `callgraph.py` (r2).

**Binding rulings:**
- No combat, ever.
- Q2a: several settlements, each a real city with its own stores, power, people and Hope; one capital deals with Earth.
- The T4 row: "Earth's quota, credits and the identity axis stay colony-wide." With Q2a, "colony-wide" there means player-wide. **`FRHColony` is one settlement.**
- The general rules: regions step in RegionId order; every roll is content-seeded with the region's name; positions are local to a region.
- The T1 risk line: a pure move, in which field order and iteration order stay the same.
- Saves are strict.

## What T1 builds, under (a), in one paragraph

T1 pulls the colony out of `URHSimWorldSubsystem` without changing a single result, a single byte of any save, or the save version.
- **The record.** The 93 C and 87 DC members (180) move, verbatim and in header order, into a plain struct `FRHColony` nested at the end of the subsystem's private section. The subsystem owns `TArray<TUniquePtr<FRHColony>> Colonies`, holding exactly one value-initialized record, made in a new constructor.
- **What stays.** The 77 "?" members stay on the subsystem with the 349 W members. Their final homes are T4's questions.
- **Step 1, the move.**
  - Every one of the 588 bodies that touches a moved member starts with `FRHColony& Colony = GetColony();` (`const` in a const method), and each of its 2,200 references becomes `Colony.X`.
  - The 11 explicit `[this]` lambdas that reference one get `, &Colony` appended to their capture list.
  - A token-identity checker proves every body is the old body plus those insertions.
  - **The internal checkpoint** follows: an identity battery and a byte-identical corpus.
- **Step 2, the inert additions:**
  - `FRHColony::Id`;
  - `FRHRobotFragment::ColonyId`, stamped on every spawn path;
  - the Lander's non-trade roles behind one predicate and three per-colony position accessors, each returning today's exact value;
  - `SurfaceRoute`'s process-global scratch, made per colony;
  - a committed member-classification CSV and its check;
  - a new `-colony` suite.

  Nothing in step 2 is saved.
- **One commit, one director look.** The battery is identical apart from the new `colony.log`, and every captured save is byte-identical.

---

## 1. What `8da8c2a` gives T1

**The class.**
- `URHSimWorldSubsystem` spans SH:644-4426: `public:` at SH:649, `private:` at SH:2923.
- Only `Defs` and `Clock` are `UPROPERTY` (SH:4424-4425), and no other member holds a UObject.
- **Outside code cannot reach private state.**
  - No `friend` declaration opens the subsystem to outside code. The header's three friends are `operator<<` for `FRHRecalledMarsBorn` (SH:89), `FRHFloor`'s grant *to* the subsystem (SH:304), and `GetTypeHash` for the nested `FRHDeepTripKey` (SH:3228).
  - The 163 direct data accesses from outside are all to the 18 public delegates (`inventory_access.md` section 2).
  - **So qualifying the bodies in place needs 0 edits outside the class.**

**The two member counts, reconciled against the header.** The state inventory's 606 is the 597 non-static data members plus the 9 `static constexpr`. Its 24 public members are the 18 delegates (SH:2618, :2889-2905) plus 6 constexpr (SH:733, :1422, :1423, :1581, :1632, :2074), and its 582 private are 579 plus 3 constexpr (SH:3953, :3954, :4070).
- The access inventory lists the constexpr as its own kind.
- The two lists differ by exactly those 9 names.
- The prototype's parser agrees: 606 names, 597 data members in 563 statements (`rewrite/report.md`).
- **Every refresh works from the 597 non-static data members,** name for name. `static constexpr` is W by rule.

**The classes and their unqualified references inside bodies** (`inventory_state.csv`, `access_members.tsv`):

| Class | Members | References | Notes |
|---|---|---|---|
| C | 93 | 1,482 | — |
| DC | 87 | 718 | 17 of them `mutable` |
| ? | 77 | 933 | 3 `mutable` |
| W | 349 (340 data + 9 constexpr) | 2,520 | — |

- **T1 moves C and DC: 180 members, 2,200 references.**
- No W or DC member is saved. The 156 saved members are 87 C and 69 "?".

**The spine mixes colony state and player-wide state in both directions** (r2, F1).

Counts are distinct members, not references. "Reach" follows every call from the step through all 1,235 subsystem bodies, with overloads unioned and lambdas counted inside their body (`t1/design/callgraph.py`):

| Step | Direct C/DC | Direct "?" | Methods reached | C/DC reached | "?" reached |
|---|---:|---:|---:|---:|---:|
| `StepEventEdges` | 0 | 2 | 3 | 0 | 2 |
| `StepUplink` | 1 | 0 | 292 | 96 | **54** |
| `StepTransit` | 2 | 0 | 70 | 46 | 0 |
| `StepTaskBoard` | 7 | 0 | 24 | 18 | 0 |
| `EraLogistics` | 6 | 0 | 21 | 16 | 0 |
| `StepProduction` | 10 | 0 | 45 | 21 | 0 |
| `StepDepositAlerts` | 3 | 0 | 6 | 3 | 0 |
| `StepHabitability` | 5 | 0 | 29 | 24 | 0 |
| `StepAgriculture` | 19 | 0 | 89 | 72 | **1** |
| `StepPopulation` | 23 | 0 | 89 | 83 | 0 |
| `StepHope` | 1 | 0 | 88 | 64 | **3** |
| `StepGrowth` | 4 | 0 | 21 | 24 | 0 |
| `StepBigArrival` | 4 | 0 | 38 | 32 | 0 |
| `StepCityGrowth` | 6 | 0 | 251 | 105 | **39** |
| `StepDiscovery` | 4 | 2 | 68 | 52 | 2 |
| `StepTrade` | 0 | 3 | 16 | **4** | 14 |
| `StepEarth` | 0 | 10 | 13 | **5** | 20 |
| `StepQuota` | 0 | 11 | 67 | **39** | 20 |
| `StepPower` | 6 | 0 | 14 | 11 | 0 |

**Player-wide steps write the colony:**
- **`StepQuota`** (S:2141) lands the ship into the colony:
  - `ApplyManifestItemEffect` (called at S:2211, defined at S:2445) spawns robots (S:2470), houses colonists (`Debug_AddColonists`, S:2500 and :2528), and deploys rovers into `ImportStock`;
  - `ApplyHomeAgain` (S:7102) calls `AddColonists` (S:7120).
- **`StepTrade`** (M:1216) calls `StepConvoyReturn` (M:1276), which drops goods at the Lander (M:1323) and into `Stocks`.
- **`StepEarth`** (S:10010) reads `Colonists` through `IsColonyQuiet` (SH:2022).

**Colony steps write player-wide and world state:**
- **`StepCityGrowth`** (G:476) runs the world market's sol roll, `RollMarket(SolNow)` (G:489, M:516), beside the colony's ledgers (G:487-488).
- **`StepUplink`** (S:1068) executes every order through `ExecuteCommand` (S:9200), including the polity's verbs: `BookConvoy` (M:1140) and `ResolveDilemma`.
- **`StepHope`** reads `ActiveCrisis`, `SolidarityHope` and `DiscoveryLog` through `GetColonyHope` (S:6452).
- **`StepAgriculture`** reads `ActiveCrisis` through `GetHumanWorkTempo` (S:7658).

**The order interleaves too.** The per-colony `StepPower` runs after the player-wide `StepQuota` (S:690-691 in the agent band, S:862-863 in the era band).

**What this means.**
- Only `StepEventEdges` is player-wide all the way down.
- Ten steps are colony-only all the way down.
- Eight mix the two.
- **T3 and T4 must design the per-colony/player-wide split of the spine. T1 does not fix it, and T1's choice of where the "?" members live neither helps nor hinders that split.** Section 4 lists the crossings in the header comment block T4 audits.

---

## 2. Mechanics: qualify in place (i), with a hoisted local

**Decision: (i).** The methods stay on the subsystem, and each body reaches the colony through one local reference.

| | (i) qualify in place | (ii) move the methods with the state |
|---|---|---|
| Edits inside bodies | 2,200 references gain `Colony.`; 588 hoist lines; 11 capture appends | 723 definition qualifiers change (plus 23 return-type qualifiers if nested types move) |
| World-level references in moved code | none | 2,520 W references through a back-pointer (`Defs` 664, `Clock` 70, delegates 176, config ~1,610) |
| Outside edits | **0** | 829 forwarders, or 252 acquisition sites and 575 declarations retyped |
| Methods pinned to the UObject | n/a | the 6 overrides and `HandleSolElapsed` (bound at S:516), with 848 state references between them |
| Mixed methods | stay mixed, one hoist each | must be split now (`SaveColony` has 90 C/DC and 38 "?" references; `LoadColony` 204 and 54) |
| Proof | token identity after stripping the insertions | none comparable |

The figures come from `inventory_access.md` section 5 and `t1/design/bodies.py`.

**(iii), rejected:**
- **Reference members** on the subsystem bound to colony 0 need no body edits, but they cannot be re-seated, so T4 would do the 2,200-site rewrite after all.
- **A `#define` per member** would rewrite the 17 same-named fields of other structs (for example `FRHCommuteKey`, SH:3096; `FRHDeepWalk`, SH:3239).

### 2.1 How a body reaches the colony: one hoist, one rule

- **The rule.** Every subsystem member-function body that references at least one moved member, anywhere in it (lambdas included), starts with:
  - in a non-const method: `FRHColony& Colony = GetColony();`
  - in a const method: `const FRHColony& Colony = GetColony();`
- Every reference becomes `Colony.X`.
- **Counts at `8da8c2a`:** 588 hoists (423 out of line, 165 inline in the header; 400 in const methods), and 2,200 qualifiers.
  - The prototype counts 587, because its rule skips the one body touched only inside a lambda, `IsTunnelStalled` (S:8019), whose lambda is `[&]`. Under this rule it hoists too.
- **Why a hoist:**
  - `GetColony()` is `*Colonies[0]`: a range check, live in Development builds, and two loads. The hoist pays that once per call.
  - **T4's audit is per body.** When `GetColony()` stops meaning "the one colony", T4 decides what each of the 588 bodies means: the stepped colony, the capital, the viewed colony, or a robot's colony. It can do that by re-pointing the hoist, or by turning it into a parameter, with no other change to the body.
- **Never touched:** static methods; the bodies of nested types' own member functions (SH:3106, :3315); and free functions (`RH::MoveAlong`, the suites, K).

### 2.2 Names, and how collisions are ruled out

- **The local is `Colony`, and the accessor is `GetColony()`** (const and non-const overloads).
- Inside the 1,235 subsystem bodies and their parameter lists, `Colony`, `GetColony`, `FRHColony`, `Colonies`, `IsDepotBuilding` and `GetColonyAnchorCm` occur 0 times, and no class member has any of these names.
  - Elsewhere, `Colony` is only in strings (S:561, :13999, :14866), in a suite lambda in a free function (`RHSimCreditSuite.cpp:706`), and in RedHope's own classes. RedHope's `RHFrameStats::Colony` is a namespaced enumerator, never visible in subsystem bodies (review, "Checked and found sound").
- **The prototype's `Col` is rejected.** It occurs 32 times inside subsystem bodies. The prototype needed a `this->Col()` fallback in 7 of them (`rewrite/report.md`, "Collisions").
- **A missed qualifier cannot rebind silently.** No namespace-scope or base-class name equals a member name (`access_scripts/traps.txt`), so a bare use left behind is a compile error.
- **The parameter guard.** `-Wshadow` is an error here, which covers locals. Whether clang flags a member-function parameter that shadows a field is not verified (review F11). So the checker guards it explicitly (2.7, item 6).

### 2.3 Lambdas, including stored ones

There are 218 lambdas in bodies: 50 `[&]`, 27 explicit lists naming `this`, 87 explicit lists without `this`, 54 with no capture, and no `[=]`.

| Kind | What the tool does | References affected |
|---|---|---|
| `[&]`, `[&, ...]` | nothing; `Colony` is captured implicitly | 15, in 6 lambdas: S:1732, :4229, :4252, :8025, :13154, :13184 |
| explicit list naming `this`, no default capture | appends `, &Colony` before `]` (on every such lambda between the body and the reference) | 20, in 11 lambdas: `RHSimCommute.cpp:310`, `:353`; `RHSimDeepWalk.cpp:718`; S:530, :4821, :4871, :4994, :6287, :7471, :7489, :10675 |
| other explicit lists, no capture | cannot reach members today; unchanged | — |

Three of the 11 are sort comparators over `Colonists` (S:4994, :7471, :7489), so a captured reference costs nothing per comparison, where `GetColony()` would pay its range check each time.

**Stored lambdas: creation-time capture and call-time lookup.** The coordinator asked about the two binding sites at S:516 and S:530, and the answer differs per site.
- **S:516** is `Clock->OnSolElapsed.AddUObject(this, &URHSimWorldSubsystem::HandleSolElapsed)`. It is a stored *method* binding, not a lambda, so nothing is captured. `HandleSolElapsed` references no moved member; it calls `SaveColony`, which hoists `GetColony()` at call time.
- **S:530** is `Defs->ForEachDeposit([this](...) { ... Deposits ... })`. `ForEachDeposit` takes a `TFunctionRef` (`RHDefinitionsSubsystem.h:122`), so the lambda is invoked within the call and never stored. It gets `, &Colony`.
- **The one stored lambda** in a subsystem body, `OnCommandRejected.AddLambda([&Rep] ...)` (S:3887), captures a local only, and is removed in the same call (S:3990).

**Are the two forms equivalent in T1?** Yes, for any lambda that lives no longer than the subsystem. There is exactly one record, allocated once in the constructor and never re-seated (TUniquePtr storage, load in place). So `&Colony` captured at creation and `GetColony()` evaluated at call time always denote the same object.

**What T4 needs:**
- **In-call lambdas** (`TFunctionRef` arguments, comparators, local helpers) act on the colony of the body that created them, so creation-time capture is the right meaning.
- **Stored callbacks** (delegates, timers, any `TFunction` kept past the call) must resolve their colony at call time. That means `GetColony()` under T4's meaning, or a colony Id captured by value and looked up when called. They must never hold a reference captured at bind time: T4's "colony in context" may differ when they fire, and a reference to a removed settlement would dangle.
- **Today no stored lambda touches colony state.** The checker makes that a rule (section 14): a lambda passed to a delegate-bind API (`AddLambda`, `BindLambda`, `CreateLambda` and the `Weak` forms), assigned to a member, or returned must not capture or reference `Colony`.

### 2.4 Const methods and `mutable`

- 400 hoists are `const FRHColony&`, which is exactly today's constness.
- **The 17 `mutable` DC members stay `mutable`,** so the lazy caches that const methods fill still compile and behave the same. Examples are `CommuteMemo` (SH:3159), `DeepWalk` (SH:3246), `DeepNet` (SH:3663) and the bed keys (SH:3796-3797).
- The review confirms every reference keeps its type and constness, `mutable` included, and that RedHopeSim has no `const_cast` of the subsystem.

### 2.5 Inline header bodies

The 165 inline bodies follow the same rule. A member-function body defined inside the class is compiled in complete-class context, so it can name `FRHColony` and `GetColony()` even though both are declared at the end of the class.

### 2.6 Macros and strings

- RedHopeSim defines no macros. No `TRACE_` or `SCOPE_` macro takes an identifier in a subsystem body.
- **One assertion macro contains a moved member:** `ensure(!PendingBoreWork.Contains(Id))` at S:10886. Only its failure text changes, and a green battery contains no fired `ensure`.
- **String literals are never edited.** The trap: S:325 reads the DT_Config key `"BigArrivalStreakSols"`, which equals a moved member's name but sets the W member `BigArrivalStreakSolsNeeded`. S:9505's `"Furniture"` is the other literal equal to a moved name.

### 2.7 The token-identity proof (the checker's contract)

The checker lexes the pre-move tree and the post-move tree. Each literal is one token, and **the sequence of literals and comments is compared too**. Only the comment lines that travel with moved declarations are exempt.

1. **Strip exactly three inserted patterns from the post stream:**
   - **P1:** the hoist, as the first statement of a subsystem member-function body. Its constness must match the method's.
   - **P2:** `Colony .` immediately before an identifier on the frozen MOVE list, when not itself after `.`, `->` or `::`.
   - **P3:** `, & Colony` immediately before the `]` of a lambda capture list that names `this` and has no default capture.
2. **In the header:** remove the generated shell (section 3.2's pinned text), and check that the record's declarations are, verbatim and in order, the declarations removed from the class. In S, the new constructor must match its pinned text exactly.
3. **Every other token stream must be identical.**
4. **Tallies:** P1 equals the number of bodies with a moved reference (588); every P2 lies inside a subsystem member body; every P3 sits on a lambda that encloses a P2.
5. **Lambdas:** inside a lambda body, `Colony.` is allowed only if every enclosing lambda up to the method body captures by default `&` or carries P3. No stored lambda references `Colony` (2.3).
6. **The guard that makes P2 sound** (F11): no parameter, local, lambda parameter, init-capture or structured binding in a subsystem body carries a moved member's name, or the name `Colony`.

**The compiler supplies the other half of the proof:** a missed reference cannot compile (2.2).

**The prototype implements this contract except for the names, P3 and the shell** (section 14 lists the changes).
- It passes on move list A: all 1,235 bodies are token-identical; no literal or comment changed; all 57 RedHopeSim files are byte-identical outside the bodies and the header's member list.
- Its planted-defect run catches each of these: an edited string, a missed reference, the local used in a `[this]` lambda, a comment edit, a wrong hoist constness, a qualifier after `.`, and a dropped record member (`rewrite/report.md`, "Defects").

### 2.8 `-scale` (r2: F10)

- **The stated budget:** E5's line, "SCALE currency ... 1,000 RollMarket calls" (K:17129-17159). Its median over three clean runs on the T1 binary must be at most its pre-T1 median + 0.2 ms. E5's own record is 0.863 ms (`design-decisions.md:1899`).
- **Every `-scale` timing line** has its pre-T1 and post-T1 medians recorded.
  - Any line more than **10%** above its pre-T1 median gets an A/B before the gate closes.
  - So does an absolute budget that fails only after T1. Today's flaky ones are the cold commute at 10.45-10.73 ms against 10, and the cold deep net at 4.00 against 3 (`design-decisions.md:1298`, :2050).
  - The A/B runs `-scale` in ABBA order on `bin_pre_t1/` and the T1 dylibs (5.4's swap protocol), never as re-runs alone.
- **The r1 agent-step "+0.2 ms" gate is dropped.** The agent step at N = 5,000 takes about 0.3 ms (E1's figure, `design-decisions.md:1298`), so +0.2 ms would allow about 60%. The 10% rule replaces it.
- **Every digest must be identical** to the pre-T1 binary's (13 at E6).

---

## 3. The record

### 3.1 Nested inside the subsystem, at the end of the private section

- **Position:** after the last nested type (`FRHBookPlan`, SH:4041) and the last moved declaration (SH:4420), before `Defs` and `Clock` (SH:4424).
- **What nesting buys:**
  - The 18 C/DC members typed by nested types, and the 2 arrays sized by private constants (`ProdAccKg` and `ProdRing`, SH:3955-3956, sized by `ProdLegs` and `ProdMaxWindow` at SH:3953-3954), need nothing.
  - A top-level record would need the transitive closure hoisted out: 28 types and 4 constants for move list A (`rewrite/report.md`, "Record needs").
- **The `FRHFloor` friend (SH:304) is unchanged,** because the methods stay on the subsystem.

### 3.2 A plain struct, value-initialized (r2: F7)

- Nothing in the record needs reflection or GC: `Defs` and `Clock` are the only `UPROPERTY` members, and both are W.
- UHT cannot nest a `USTRUCT` (inferred), and the class already holds 60 nested plain types.
- **The pinned shell.** The checker compares this text exactly:

```cpp
// --- private section, after FRHBookPlan ---
// T1: one settlement's state (the C and DC rows of the T1 classification), moved verbatim, in header order.
// Plain data: nothing here holds a UObject. Never copied or moved: one colony lives in one place, and a load resets it in place.
// "= default" (never "{}") keeps value-initialization: MakeUnique<FRHColony>() is new FRHColony(), which zero-fills every member
// without an initializer before construction, as the UObject allocator zero-fills the subsystem today.
struct FRHColony
{
	FRHColony() = default;
	FRHColony(const FRHColony&) = delete;
	FRHColony& operator=(const FRHColony&) = delete;
	FRHColony(FRHColony&&) = delete;
	FRHColony& operator=(FRHColony&&) = delete;
	/* the 180 moved declarations, verbatim, in header order */
	int32 Id = 0;                 // step 2: the colony's Id; 0 is the founding colony (robots carry it: FRHRobotFragment::ColonyId)
	mutable FRHRoadRoute SurfaceRouteScratch; // step 2: SurfaceRoute's stale-net scratch, per colony (was a process-global static, TR:872)
};
TArray<TUniquePtr<FRHColony>> Colonies; // T1: exactly one, made in the constructor, never re-seated (the load is in place)
FRHColony& GetColony() { return *Colonies[0]; }
const FRHColony& GetColony() const { return *Colonies[0]; }

// --- public section, first line after "public:" (SH:649) ---
URHSimWorldSubsystem(); // T1: makes the one colony record (S, beside Initialize)

// --- S, straight before Initialize (S:76) ---
URHSimWorldSubsystem::URHSimWorldSubsystem()
{
	Colonies.Add(MakeUnique<FRHColony>()); // value-initialized (see FRHColony)
}
```

**Why `= default` and `MakeUnique`** (F7):
- `MakeUnique<T>()` is `new T()` (`UniquePtr.h:868-870`): value-initialization. Because the default constructor is not user-provided, every member without an initializer is zero-filled before construction. That is today's behaviour, because the UObject allocator zero-fills the subsystem.
- The prototype's `AddDefaulted` default-initializes instead. It is moot at `8da8c2a`, where all 79 moved declarations without initializers are class types. But a plain-data member added by E6 to E8, or an `FVector`, whose default constructor is trivial (`Vector.h:150`), would read garbage.

**Why non-copyable and non-movable:**
- A copy would be a second colony that silently diverges, and a swap would free the record that references point into (5.2).
- Seven moved members, and every building's `InputKg` and `OutputKg`, stream maps and sets in insertion order: `Stocks`, `PendingOutputs`, `ImportStock`, `PendingBoreWork`, `FloorO2Kg`, `RatedFloors` and `FloorRoomCells`. They must stay in one place, moved whole, never rebuilt.

### 3.3 The container: `TArray<TUniquePtr<FRHColony>>`

**The requirement is address stability:**
- a hoisted reference is live for a whole body;
- `SurfaceRoute` returns a reference into `DeepNet.SurfaceRoutes` (TR:835-880);
- presentation holds `const TArray<...>&` results of getters for a frame;
- in T4, a founding executed inside a body appends a colony while that body's reference is live.

`TArray<FRHColony>` relocates its elements as it grows; `TUniquePtr` keeps every record where it is.
- **The cost** is one allocation per colony and one indirection per hoist.
- **The record exists** from the constructor on, before `Initialize` (S:76) and `OnWorldBeginPlay` (S:96). `Initialize` and `Deinitialize` reference only W members.
- The vtable-helper construction path leaves `Colonies` empty, but that object never runs a method (review).

### 3.4 The self-pointer: untouched

- `RoverProfile.Params = &RoverRoadParams` (S:295). Both members are W (SH:3055-3056), so the pointer never leaves the subsystem, which is a UObject that never moves.
- K's address test (K:10120) is unchanged.
- The classification pins both members as W.

### 3.5 The static scratch: made per colony (step 2)

- `static FRHRoadRoute Scratch` (TR:872) is process-global. `SurfaceRoute` returns it by reference while the deep net is stale.
- In T4 a second colony's call would overwrite a route another colony's caller still holds. Step 2 makes it the record's `mutable FRHRoadRoute SurfaceRouteScratch`.
- **Behaviour is identical:** the function resets its target on every use (TR:873-874), and its only callers are TR:887 and TR:908.
- The other mutable statics stay W: `LastMatched` (`RHRobotBrainProcessor.cpp:124`, a log latch) and `CVarRHBrainMode` (AG:14, a process switch).

---

## 4. The 77 "?" rows stay on the subsystem; their homes are T4's questions

**Decision: in T1, nothing "?" moves, and there is no polity record.**

**The reason (r2, F1).** Each member moves at most once, at the gate that decides its home.
- The plan points most of these groups at player-wide homes. Putting them in `FRHColony` now risks a second move out.
- A polity record now would be a one-instance namespace, costing 933 qualifiers for no T1 behaviour.
- r1's structural argument is withdrawn: section 1 shows the spine mixes both ways whatever T1 does.

**The homes below are provisional (r2, F2).** Each is a question for T4 (and T2, T5, T6), several of them the director's.

| Group (members) | What the plan says | Proposed home (T4 decides) | Gate |
|---|---|---|---|
| **Earth** (22: credits, the manifest, exports, the quota round, `EarthTension`, `IdentityAxis`, the ship and its alert latch, research funding) | Q2a "one capital that deals with Earth"; T4: "Earth's quota, credits and the identity axis stay colony-wide"; T4's risks: "summed in [Id] order" | player-wide | T4 |
| **The mark, E5** (24) | the polity's political act; reads `IdentityAxis` and `EarthTension` | player-wide | T4 |
| **Rival relations** (`RivalRelations`, `HiddenTensions`, `DiscoveredRivals`, `HumanNatureAxis`, `Influence`) | relations between polities; T5 places rivals, T7 draws borders | player-wide | T5, T7 |
| **Treaties** (E6's `TradeRules` and kin) | E6's rules; T7 borders | player-wide | T7 |
| **Covert operations** (`CovertAttempts`, which seeds S:9828) | a polity's acts | player-wide | T6 |
| **The market by RegionId** (`Markets`, `RivalEconomies`, `SyncedRivalsGeneration`, `MarketCheckedSol`, `MarketClearings`) | E4: one market per region, shared with the rivals | world | T5 |
| **Convoys** (`Convoys`, `NextConvoyId`, `CreditReservedMcr`, `MarketSalesMcr`) | T6: journeys between depots | player-wide, with a colony end | T6 |
| **Crises and endings** (8) | the endings read the polity's axes; a crisis drains a colony | **director, T4:** one settlement or the whole territory? | T4 |
| **The event latch** (2) | T6: weather per region | the region record | T2, T6 |
| **`DiscoveryLog`** | knowledge; room unlocks are already world-wide (section 9) | **director, T4:** shared across settlements? | T4 |
| **`SolidarityHope`** | written by a polity act, read as a Hope term | player-wide, applied per settlement | T4 |

**The crossings T3 and T4 inherit.** These go in the header comment block (from section 1, F1, and the review's F12):
1. `RollMarket` runs inside the per-colony `StepCityGrowth` (G:489).
2. `ExecuteCommand`'s polity verbs (treaties, the currency, covert acts, convoys) run inside `StepUplink` (S:9200).
3. `GetColonyHope` reads the crisis and Solidarity terms (S:6452), and `GetHumanWorkTempo` reads the crisis (S:7658).
4. `StepQuota` lands into `ImportStock`, `Colonists` and the robots (S:2211, :2445, :2470, :2500, :2528, :7102). **T4 must name the capital:** the hoists in `ApplyManifestItemEffect` and `StepConvoyReturn` bind those callees to colony 0, and T4's audit must give them the "capital" intent.
5. `StepConvoyReturn` drops goods at the Lander (M:1276, :1323).
6. `StepEarth` reads `Colonists` through `IsColonyQuiet` (SH:2022).
7. The per-colony `StepPower` runs after the player-wide `StepQuota` (S:690-691, :862-863).
8. **Founding is mixed into world start** (F12):
   - `OnWorldBeginPlay` both loads config and founds the colony: `BuildEconSlots` (S:103), `OrderLagSeconds` (S:106), `HopeSmoothed` (S:513) and the Lander (S:547);
   - the starting fleet deploys on the first step (`DeployFleetOnce`, S:584-592).

   A second founding must replay that part. The prototype counts 4 moved members initialized in place in `OnWorldBeginPlay`.
9. **Ids become per colony** (F12):
   - every `Next*Id` counter moves into the record (only `NextCommandId` stays W), so building, colonist, task and other Ids are per colony from T4 on;
   - E7's `DepotBindings` maps a region to a colony-scoped building Id;
   - presentation keys its state by building and colonist Ids;
   - colonist names are a function of the Id (S:4035-4044).
10. `ImportStock` (C) holds Earth's crates; `CityShare` (C) weighs Earth's bill; `Markets` and `RivalEconomies` are index-aligned with `EconSlots` (C; SH:3983-3986); and `MarketCheckedSol` is seeded from `ProductionCheckedSol` (M:104).

---

## 5. The save under (a): no change

### 5.1 What does not change, and why

- **The rule:** T1 writes and reads the same bytes in the same order. There is no new frame, no version bump, and no new field.
- Every `Ar << X` becomes `Ar << Colony.X` on the same type, so every byte offset and every version pin holds (`rewrite/report.md`).
- `FRHColony::Id` and `FRHRobotFragment::ColonyId` are not saved. In T1 both are always 0, and a load stamps every respawned robot from `GetColony().Id`.
- **The "list of one colony"** that the plan's save line names moves to T4. T4 restructures the file anyway, under its own version, once a second colony has blocks to write.
- **This is the E7 precedent:** the orchestrator dropped `'RHE7'` because "it held no state the shipped game writes" (`e7-region-ready-design.md:1000-1008`, :1072).
- **This is a deviation from plan text** (territories-plan.md:59, :75), recorded in section 17 and put to the director.

### 5.2 Load: in place, strict check unchanged

- **In place.** `LoadColony`'s teardown resets each field where it lives (S:14085-14104), and the body reads into the same record. There is no record swap, for three reasons:
  - **References.** A swap would free the record that every `const TArray<...>&` handed out before the load points into, and presentation and the suites hold such references.
  - **The reset list.** The teardown list is today's authority on what a load resets, and a swap would reset everything else as well.
  - **Luck, not a rule.** `OnWorldBeginPlay` sets about 211 members that the load's closure never touches (`inventory_access.md` section 6). The writer heuristic finds none of them C or DC, but nothing guarantees that stays true.
- **The version check stays strict and unchanged** (S:14026-14031).

### 5.3 The battery expectation, and how it is checked

- **Every line of every existing log is identical,** "Colony saved ... (N bytes" counts included, apart from the timing digits on the three wall-clock lines E6 recorded (`ARRV e`, `EP4`, `X16`).
- **One new log:** `colony.log`.
- **The check** is a new script, `scripts/sim/diff_battery_delta.py`:

```
python3 scripts/sim/diff_battery_delta.py <pre_logdir> <post_logdir> --old-version V --new-version V --delta 0 \
    --wallclock "ARRV e" --wallclock "EP4" --wallclock "X16" --new colony.log
```

- **What it compares:**
  - It strips timestamps as `diff_battery.py` does, and masks nothing.
  - The pre and post logs must have the same number of `LogRedHopeSim:` lines; logs named with `--new` are exempt. Lines are paired by index.
  - In an unequal pair, the non-numeric text must match. Each differing number must be:
    - **VERSION** (old == old version and new == new version);
    - **SIZE** (new − old == delta);
    - or **TIMING** (on a `--wallclock` line, followed by `ms` or ` s`).

    Anything else is **FAIL**, and the script exits nonzero.
- **Under (a)** the versions are equal and the delta is 0, so only TIMING may differ.
- The same script serves (b) (section 15).

### 5.4 The corpus: per-run capture, with honest coverage (r2: F4)

**The runner change** is an orchestrator tooling commit, made before T1. It can land only after E6 commits, because `rh_battery.sh` is in E6's uncommitted set. If it lands early, every later gate's battery captures its saves, and the gate before T1 leaves T1's baseline ready.

```bash
# scripts/sim/rh_battery.sh, opt-in: RH_BATTERY_SAVEDIR=<dir> keeps the saves of every run.
SAVEDIR="${RH_BATTERY_SAVEDIR:-}"
run_one() {
  local name="$1"; shift
  local mark="$LOGDIR/.savemark_$name"
  [ -n "$SAVEDIR" ] && touch "$mark"
  "$UE" "$PROJ" -run=RHSim "$@" $EXTRA -unattended -nosound -stdout > "$LOGDIR/$name.log" 2>&1
  if [ -n "$SAVEDIR" ]; then
    mkdir -p "$SAVEDIR/$name"
    find "$PROJ_DIR/Saved/SaveGames" -maxdepth 1 -name 'RH_*.sav' -newer "$mark" -exec cp -p {} "$SAVEDIR/$name/" \;
  fi
  # ... the existing error and completion counts, unchanged ...
}
```

- The same tooling commit adds `diff_battery_delta.py` (5.3) and `scripts/sim/cmp_save_corpus.py`.
- **The compare:** `cmp_save_corpus.py <pre> <post>` compares the two corpora run by run and slot by slot. Under (a), every file present in both must be **byte-identical**, and no file may be in one only, except the new `colony/` run.
- **Never delete or clean `Saved/SaveGames`:** it holds the director's checkpoints.

**Coverage, stated honestly** (measured on `battery_post_e5`, the 59 runs at the snapshot's code):
- The battery makes **2,266 `SaveColony` writes**; 2,065 of them go to the autosave slot `auto`, and there are 194 distinct slot names.
- Per-run capture **byte-compares the last write of each slot in each run: 251 (run, slot) pairs**, plus every doctored copy that is the last write of its slot in its run. A single capture after the whole battery, as r1 had it, would keep 194.
- **The other ~2,015 writes,** almost all autosaves, are compared **by size only**, through their "Colony saved ... (N bytes" log lines (5.3).
- **The content of an earlier write of a slot within the same run is not compared.** Full coverage would need a save-hash log line in the sim, added with its own identity battery before T1. That is not proposed.

**The pre-T1 binary is kept:**
- Before the first T1 compile, copy `Binaries/Mac/libUnrealEditor-RedHopeSim.dylib` and `libUnrealEditor-RedHope.dylib` with `cp -p` to `Saved/RHCapture/t1/bin_pre_t1/`, and record their `shasum -a 256`. After each T1 build, do the same into `bin_t1/`.
- **Re-running the pre-T1 battery** (Risk 6, determinism) or an A/B (2.8) is an explicit swap:
  1. `cp -p` the pre dylibs in, and check both sha256;
  2. run;
  3. `cp -p` the T1 dylibs back, and check both sha256 before any further run or build;
  4. log it in the gate entry.
- **Tension with E5's guard.** E5's gate scripts carry a guard, "never reinstall the pre-change binary". The orchestrator must agree that this manual, sha-checked swap is the exception, or run the pre-T1 battery twice before the first compile instead.

---

## 6. One gate, two steps, one commit (option a)

| | Step 1: the move | Step 2: the inert additions |
|---|---|---|
| What | the record block, `Colonies`, `GetColony`, the constructor; 588 hoists, 2,200 qualifiers, 11 capture appends in 16 files (S 1,263 references, SH 186, `RHSimDistricts.cpp` 154, TR 130, `RHSimCommute.cpp` 105, G 95, then 10 smaller) | `FRHColony::Id`; `FRHRobotFragment::ColonyId` and the required spawn parameter (section 7); `IsDepotBuilding` and the 3 position accessors at the role sites (section 10); `SurfaceRouteScratch` (3.5); the debug readbacks; the `-colony` suite; the classification CSV and its check (section 11) |
| Made by | the rewriter, from the frozen snapshot of the refreshed HEAD; installed by content (section 12) | hand edits, reviewed as an ordinary diff |
| Proof | the checker (2.7) and the compile | the `-colony` suite and its mutations |
| Bar | **the internal checkpoint:** an identity battery (5.3, delta 0) and a byte-identical corpus (5.4) | the final battery (identical, plus `colony.log`), the corpus byte-identical, the playtest, the click test, `-scale` |

**One commit** (the house rule: one commit per gate). Step 1's tree is not committed, so the gate entry records what is needed to regenerate and re-prove it:
- the rewriter's manifest and the checker's PASS report;
- the sha256 of every file in step 1's tree;
- the checkpoint battery and corpus results;
- the step 1 → step 2 diff, listed site by site.

The rewriter and checker are committed (proposed: `scripts/refactor/`), so anyone can regenerate step 1's tree from the commit's parent and re-run the proof. The rewriter is deterministic: "a second run reproduces out_A byte for byte" (`rewrite/report.md`).

**No batching of the move.** Every batch edits the header, so every batch costs a full rebuild of both modules and its own battery.
- Batching buys bisection, and the token proof makes bisection unnecessary in the normal case: a checkpoint difference can only come from P1-P3, the shell or the constructor.
- **The contingency** is bisecting the MOVE list (the rewriter takes any list): about 8 rebuilds, used only if the checkpoint differs and the checker's report does not explain why.

---

## 7. Robots (step 2)

**The field.** `int32 ColonyId = INDEX_NONE;` in `FRHRobotFragment`, the plan's choice: no new fragment, so the StateTree archetype's composition (AG:127-143) is untouched.
- The StateTree tasks already bind the fragment (`RHRobotStateTree.h:38`, `:69`, `:102`), and the brain query reads it (`RHRobotBrainProcessor.cpp:27`). T4's routing therefore costs no lookup.
- A sim-side `TMap<FMassEntityHandle, int32>` would cost a hash lookup per robot call.

**Size.**
- In the editor build the battery runs, `FName` is 12 bytes (`NameTypes.h:33`: `WITH_CASE_PRESERVING_NAME` is `WITH_EDITORONLY_DATA`). So the fragment goes from 52 to 56 bytes; there is no padding to absorb the field.
- Chunk capacity: 128 KiB per chunk (`MassEntitySettings.h:92`), at about 260 bytes per entity, gives 503 robots per chunk before and 496 after (inferred; `MassArchetypeData.cpp:327-357`).

**The order cannot change.**
- The only `DestroyEntity` is in `DespawnAllRobots` (AG:463), whose one caller is the load's teardown (S:14088).
- No code adds or removes a tag or fragment.
- So the archetype's entities are always in spawn order, at any capacity.
- The battery's fleets are a handful: 7 at start (`RH_Robots.csv`). `-scale`'s "robots" are synthetic handles, not entities (K:16461, :16475).
- **How a change would be caught:** CO4 pins the query order against `RobotHandles`, and every robot-driven suite would move.
- **T4's warning:** the first gate that despawns robots one at a time makes capacity matter above 496 robots in the archetype.

**Stamping.**
- `SpawnRobotWithState` (AG:77) and `SpawnRobot` (AG:60) take a **required** `int32 ColonyId`, so the compiler finds every path:
  - `SpawnRobotTracked` (S:594) passes `GetColony().Id`, which covers the starting fleet (S:576), the manifest robot (S:2470), the rover deploy (RV:326) and the load respawn (S:14208);
  - `RH.DeepBench` (`RHCheats.cpp:1999`) passes 0, commented "the founding colony (T4: choose)". It still skips `FleetCounts`, as today; that is noted for T3, whose era rates read it.
- Both brain paths copy the same fragment (AG:98-107, :156, :177), so the legacy archetype is stamped too.
- **Not saved:** under (a) and (b) alike, every robot is colony 0.

---

## 8. RNG for later colonies: the salt rule (recorded; T1 builds nothing)

**Today.**
- Per-colony rolls hash `"trait"` with the colonist Id (S:3774), `"moment"` with `MomentCounter` (S:5171), and `"survey"` with `SurveyHistory.Num()` (S:13114).
- Player-wide rolls hash the rival's name with `CovertAttempts` (S:9828), and `"crisis"` with `CrisisCount` (S:10304).

**The rule, for T2 and T4:**
- A per-colony roll hashes `Tag` when the colony's salt is empty, and `Tag + "@" + Salt` otherwise. The salt is the region's name, plus `#<Id>` if two settlements could share a region.
- The founding colony's salt is empty, so colony 0 keeps today's rolls by construction. If T2 numbers today's map "region 1", region 1 keeps them as well, because the salt follows the colony.
- Player-wide rolls keep their tags.

---

## 9. Colony progress outside the subsystem

**The `DT_Rooms` flips.**
- `StepDiscovery` (S:7575) unlocks rooms by setting `SliceActive` on in-memory `DT_Rooms` rows (S:7622, `RHDefinitionsSubsystem.cpp:167-172`). The load re-applies them (S:14477) and never reverts them.
- T1 leaves this alone. Whether discoveries are shared across settlements is a T4 question for the director (section 4).
- **Flagged, not fixed:** the flips outlive the world in the same process (inferred). This needs its own small gate.

**The other holders stay W:**
- the clock (`RHSimClockSubsystem.h:81-87`);
- the agent subsystem, apart from the stamp;
- the definitions: tables, overrides, `RivalsGeneration`, and E7's endowment overrides. The crop, furniture and quota row activations are harness- or cheat-only (review, question 1).

---

## 10. The Lander's roles (step 2): one predicate and three positions, each returning today's value

E7 moves the trade-facing roles behind `FindColonyDepotBuildingId(GetColonyRegionId())`: convoy goods (M:1323), covert loot (S:9880) and the drawn start. Step 2 moves the rest.

**The accessors** (private; declared in SH, defined in S):
- `static bool IsDepotBuilding(const FRHBuildingInstance& B)` returns `B.DefName == NAME_Lander` (S:19). **E7's rule 2 uses the same predicate.**
- `FVector GetColonyAnchorCm(const FRHColony&) const` returns `FVector::ZeroVector`.
- `FVector GetColonyRobotDropCm(const FRHColony&) const` returns `FVector(3000.f, -3000.f, 50.f)`, S:2470's expression verbatim.
- `FVector GetColonyDeployRingCm(const FRHColony&, int32 Slot) const` returns `FVector(-800.f + 250.f * Slot, -600.f + 150.f * FMath::Sin(Slot * 0.8f), RH::RobotZOffsetCm)`. That is S:573-575's and TR:2001-2002's float expression; both Z values are exactly 50.0, and FP contraction is off on Mac (`MacToolChain.cs:209`).

**Why exact:**
- Each site keeps its own loop and conditions. Only `DefName == NAME_Lander` is replaced, by a call computing the same comparison.
- The position accessors return today's expressions verbatim.
- The non-trade roles do not go through E7's binding-aware rule.

| Role | Site (function) | After step 2 |
|---|---|---|
| Founding: the anchor placed | S:547 (`OnWorldBeginPlay`) | `AddBuilding(NAME_Lander, GetColonyAnchorCm(Colony), true)` |
| Cannot be demolished | S:10627 (`CanDemolishBuilding`) | `IsDepotBuilding(*B)` |
| Store and haul sink (7 sites) | S:960, :984, :1052 (`EraLogistics`); S:1178, :1245, :1296, :1344 (`StepTaskBoard`) | `... == NAME_Stockpile \|\| IsDepotBuilding(...)` |
| Salvage, and hauls bound for a demolished building | S:10852-10858 (`RemoveBuilding`; used at :10876, :10965) | the first `IsDepotBuilding` |
| Refunds: `CreditLander` | S:11498 | `FindByPredicate(IsDepotBuilding)` |
| Road refunds | RD:465 (`RemoveRoadEdge`) | `IsDepotBuilding(B) && !bUnderConstruction` |
| Earth cargo (kits, a rover aboard) | S:2455-2480, into `ImportStock` | unchanged; a C member (the capital's in T4) |
| Manifest robots' drop point | S:2470 | `GetColonyRobotDropCm(Colony)` |
| Starting fleet's ring; a load's robot that cannot stay below | S:573-575; TR:2001-2002 | `GetColonyDeployRingCm(Colony, Slot)` |
| Rover-freight pad search; the ice-hint bearing | S:1010; S:1867 | `GetColonyAnchorCm(Colony)` |
| Emergency docks and the dock's slow rate | S:12932; S:12952; RV:68 | `IsDepotBuilding(...) && ...`, the rest as written |
| A rover's cargo with no source; the rover deploy node | RV:263; RV:305-310 | `IsDepotBuilding`; the fallback `GetColonyAnchorCm(Colony)` |
| Save-time home of orphaned cargo | S:13563 | `FindByPredicate(IsDepotBuilding)` |
| E7's trade roles | E7's rule 2 | its test becomes `IsDepotBuilding` |

**Not touched:**
- the harness benches (S:3897, :12330);
- every string that says "Lander";
- presentation (`RHAgentVisualizerSubsystem.cpp:322` and E7's getter).

The file-local `NAME_LanderRoad` (RD:14) and `NAME_LanderRover` (RV:18) are deleted. Any new anonymous-namespace name must be unique per file, because the game target's unity build merges them.

---

## 11. The refresh at E8's HEAD: it blocks (r2: F5)

**The recipe:**
1. Freeze a snapshot of the HEAD T1 builds on (`git archive HEAD Source scripts`, never `Config/`).
2. Re-run `inv_classify.py`, which runs `inv_members.py`, and `access_scripts/run_all.sh`.
3. **Reconcile the two member lists name for name** (597 at `8da8c2a`, plus the additions); any difference stops the refresh.
4. **Classify** (rules below).
5. **Freeze the MOVE list** (`t1_move_list.txt`, with the commit hash).
6. **Re-run the measurement scripts.** `bodies.py`, `lambdas.py` and `callgraph.py` must show:
   - 0 collisions for `Colony`, `GetColony`, `FRHColony`, `Colonies`, `IsDepotBuilding` and the accessors;
   - no `[=]` lambda;
   - no stored lambda referencing a moved member;
   - no new mutable static;
   - no new `this` store;
   - no member name equal to a namespace-scope or base-class name.
7. **Re-find by snippet:** every Lander site, every spawn path, and every `RHSaveFrame::Write` and tail reader.
8. **Run the rewriter** on the snapshot. Its unhandled-site report must be empty, and the checker must pass (section 12).

**Classification: the refresh blocks.**
- A member found at the refresh HEAD is classified automatically **only if** it is in the expected-additions table below, or is `static constexpr`/`static const` (W), or is a delegate, a `UPROPERTY` or a UObject holder (W).
- **Anything else is UNCLASSIFIED, and the refresh stops.** That includes a member the heuristic rules would place:
  - config (a DT_Config row, no play-time writer);
  - harness switch;
  - load scratch;
  - test instrument;
  - a frame-based guess;
  - a "default".
- The tool prints the class it proposes and the rule that fired. The orchestrator records the class and a one-line reason in the gate entry, and only then does the refresh continue.
- **There is no silent default:** a W row is not in the header block T4 audits, so a heuristic W would hide per-colony state from T4.

**The standing check, for every gate after T1.** Step 2 commits:
- `scripts/refactor/subsystem_members.csv`: every data member of `URHSimWorldSubsystem` and of `FRHColony`, with its class, its owner (subsystem or colony), the gate that classified it, and a one-line reason;
- `scripts/refactor/check_subsystem_members.py`: the inventory's extraction, vendored, plus the CSV lookup.

The check exits 1 with one line per problem:
- `UNCLASSIFIED name (proposed: class, rule n)` for a member with no row;
- `STALE name` for a row with no member;
- `MISPLACED name` for a C or DC row declared on the subsystem, or a W or "?" row inside `FRHColony`.

**Every later gate's spec lens and gate script run it.** A gate that adds a subsystem member adds its row, with a reason, in the same commit, so T2 and later gates cannot add per-colony state outside `FRHColony` unnoticed.

**Expected additions** (the only members classified without a stop):

| Gate | Member (or change) | Class |
|---|---|---|
| E6 | `TradeRules`, `NextRuleId`, `PendingMarketEffects`, `ConvoyTerms`, `TariffPaidMcr`, `TariffPaidMmk` (in `'RHL6'`) | ? |
| E6 | the trimmed transient records of applied effects and production cuts (names as built) | ? |
| E6 | `bLeversEnabled`; a levers-block-lost flag if it is a member | W |
| E6 | the 13 config rows, `PactMinRelation` ... `SabotageCutPct` | W |
| E6 | **removed:** `ClosedRoutes`, `SabotageSols`, `EmbargoGrace`, `DefectedRivals` | — |
| D6e | `LiftCarW`, `TramWPerCell`, `TransitLoadPriority`; the rings inside `FRHTransitLedger` and `FRHTramLine`, or, under option (b), parallel arrays (C) | W / — / C |
| D7 | `Atriums` | C |
| D7 | `AtriumsVersion`, `AtriumCells`, the facing cache and its key | DC |
| D7 | the AT11 rebuild counter; the three config rows | W |
| E7 | `DepotBindings` (harness, runtime) | W |
| E8 | `MarketHistSols` (constexpr), `bMarketHistoryEnabled` | W |
| E8 | the `Hist*` fields inside `FRHMarket` | inside a "?" member |

---

## 12. The build plan on the 8 GB Mac (r2: F6)

**The machine.**
- 8 GB of RAM, one Unreal process at a time. Swap is on `/`, and at least 5 GB must be free there (E6 brief).
- E6's fix rounds hit a load of 113 with swap at 6.9 of 7.2 GB (project memory, 2026-10-28 15:15).
- r1 cited a 39-minute link stall from a draft of E6's entry; that text has since been edited out of the live docs, so the citation is withdrawn.

**The header edit recompiles both modules:**
- RedHopeSim: 38 `.cpp`, about 62,500 lines, 30 of them including SH directly.
- RedHope: 33 `.cpp`, about 39,200 lines, 22 including SH directly.

**The four mechanics:**
1. **Install by content.**
   - The rewriter writes into an output tree, never into the repo. `scripts/refactor/rh_t1_install.py <out> <repo>` copies only the files whose bytes differ, prints the list, and leaves every other file, and its mtime, alone.
   - UBT decides by file time (E5's finding, `design-decisions.md:1851`). So a tool fix that changes one `.cpp` then costs one file and a link, not a full rebuild.
2. **Cap parallelism.**
   - Run `Build.sh red_hopeEditor Mac Development -project=... -waitmutex -MaxParallelActions=2`. UBA budgets 1.5 GB per action (`UnrealBuildAcceleratorConfig.cs:394`), which is about five actions on 8 GB, with the adaptive pass compiling S's 14,870 lines outside its unity blob.
   - Record `sysctl vm.swapusage` and the load before and after each build.
3. **Keep the pre-T1 binary.**
   - `bin_pre_t1/`, with sha256, before the first compile, and `bin_t1/` after each build (5.4).
4. **Gate the compile.**
   - `Build.sh` runs only after the rewriter reports **0 unhandled sites** (shadows, init-captures, `[=]`, stored lambdas, self-typed receivers, static-method or class-scope uses, pointer-to-member, mixed declarators, cross-boundary initializers, name collisions) and the checker reports **PASS**.
   - Anything P1-P3 cannot express, for example a parameter named like a moved member, goes into a separate pre-T1 hand commit with its own identity battery, never into the move.

**The budget.**
- **Each rebuild is two compiles:** `-DisableAdaptiveUnity`, then the normal build, which recompiles the touched files outside their blobs.
- **Under (a):**
  - 2 full rebuilds: the move (header and 15 `.cpp`), then step 2 (SH and `RHAgentFragments.h`);
  - 4 single-file mutation builds;
  - 2 batteries;
  - 1 verification round.
- Fixes go into the tool, never by hand. Install by content keeps a `.cpp`-only fix cheap.

---

## 13. Verification under (a)

**The internal checkpoint, after step 1 (before step 2 is applied):**
1. The checker reports PASS with 0 differences. Record its tallies: 588 hoists, 2,200 qualifiers, 11 capture appends, 180 record members.
2. Both modules build clean.
3. The battery, with save capture: every run is `errors=0 completed=1`, and `diff_battery_delta.py --delta 0` finds only TIMING.
4. The corpus is byte-identical to the pre-T1 corpus, run by run.

**The final bar, after step 2:**
1. **The battery:**
   - every run green;
   - `diff_battery_delta.py --delta 0 --new colony.log` finds only TIMING;
   - `colony.log` is green;
   - no other log gains or loses a line.
2. **The corpus** is byte-identical, apart from the new `colony/` run.
3. **The playtest** (`RH_PLAYTEST_DISTRICTS=1 bash scripts/sim/rh_playtest.sh 110 t1 guide full`) is byte-identical to the current baseline.
4. **The click test** (`bash scripts/unreal/rh_clicktest.sh`) is unchanged.
5. **`-scale`:** 0 failures; every digest identical; E5's line within its pre-T1 median + 0.2 ms; every timing line recorded, with the 10% rule (2.8).
6. **The checkpoints** still load, because the format is unchanged.
7. **`check_subsystem_members.py`** passes.

**The `-colony` suite** lives in `RHSimColonySuite.cpp`, appended to `SUITES` (`rh_battery.sh:37-39`; update the counts at :2 and :58).
- It fails loudly; no path logs an Error on purpose.
- **No line prints a version or a size.**
- Its runtime-only hooks: `Debug_GetColonyCount()`, `Debug_GetColonyId(int32)`, `Debug_GetColonyAddress()` (a `uint64`, compared and never printed), `URHAgentSubsystem::Debug_GetRobotColonyIds(...)` and `URHAgentSubsystem::Debug_CollectQueryOrder(...)`.

| Check | What it pins |
|---|---|
| **CO0 one record, loaded in place** | 1 record with Id 0; `Debug_GetColonyAddress()` is equal before and after a `LoadColony` |
| **CO2 a busy colony round-trips** | the starting fleet, a landed manifest Robot, a rover deployed on a road, colonists, and a floor, shaft and tunnel (built with the hooks of `-credit`, `-traffic`, `-shafts` and `-tunnels`): save, load, save is byte-identical, and every robot's `ColonyId` is 0 afterwards |
| **CO3 every spawn path stamps** | starting fleet, manifest robot, rover deploy, load respawn, and `SpawnRobot(..., 0)`: every robot reads 0, and the count equals `RobotHandles` |
| **CO4 the order** | `Debug_CollectQueryOrder` equals `RobotHandles` order after the spawns and after a save and load |
| **CO5 the depot and the positions** | `IsDepotBuilding` is true for the Lander (building 1) only; the anchor is `ZeroVector`; the drop point and the ring for slots 0-7 equal today's expressions, computed in the suite, bit for bit; `FindColonyDepotBuildingId(0)` is still 1 |

**Mutations.** Each is built and run, then restored by copy **with a fresh file time** and sha256-checked, as E6 did. A restore that keeps the old file time leaves the mutated object in place, because UBT decides by time (`design-decisions.md:1851`). Each mutation must fail the check named:
1. The rover deploy (RV:326) passes `INDEX_NONE`: fails **CO3**.
2. `IsDepotBuilding` is also true for `Stockpile`: fails **CO5**, and the battery.
3. `GetColonyRobotDropCm` returns (3000, −3000, 51): fails **CO5**.
4. `LoadColony` re-seats the record (`Colonies[0] = MakeUnique<FRHColony>();` as its first statement, before the hoist): fails **CO0**.

**No second-colony test.** T1's step loop does not iterate colonies: which steps run per colony, and in what order, is T3's design (section 1). T4's founding test must pin address stability under list growth, and colony 0 unchanged by a second record.

---

## 14. The tool and checker changes, from the prototype to this design

The prototype (`t1/rewrite/`: `rh_t1_rewrite.py`, `rh_t1_check.py`, `run.sh`) needs these changes before it can make T1's step 1.

**The rewriter (`rh_t1_rewrite.py`):**
1. **Names.** The defaults become `--local Colony --accessor GetColony`. The `this->Col()` fallback is deleted, and the tool asserts 0 collisions instead (2.2).
2. **Lambdas.**
   - References inside lambda bodies become `Colony.X`, never `GetColony().X`.
   - For every explicit capture list that names `this` and has no default capture, on the path from the method body to a reference, append `, &Colony` before its `]`.
   - `[&]` lists are left alone.
   - **Refuse, as unhandled sites:** a `[=]` lambda; an init-capture or explicit capture named `Colony`; a lambda that references a moved member while being stored (an argument to `AddLambda`, `AddWeakLambda`, `BindLambda`, `BindWeakLambda`, `CreateLambda`, `CreateWeakLambda`, assigned to a member, or returned).
3. **The hoist.** Every body with a moved reference anywhere, lambdas included, starts with the hoist, so `IsTunnelStalled` gains one (588, not 587).
4. **Storage and the shell.** Emit section 3.2's pinned shell, which means:
   - `TArray<TUniquePtr<FRHColony>> Colonies;`, with no `MakeColonyList` and no `AddDefaulted`;
   - `FRHColony() = default;` and the four deleted operations;
   - the two `GetColony()` definitions;
   - the public `URHSimWorldSubsystem();` declaration;
   - the constructor definition in S, straight before `Initialize`.

   This is the review's third change (F7).
5. **The output.** Write into an output tree; installing is `rh_t1_install.py`'s job (section 12). The manifest's unhandled list must be empty.

**The checker (`rh_t1_check.py`):**
1. **Strip patterns.** It strips P1 (the hoist, constness matched), P2 (`Colony.` before a MOVE name) and the new **P3** (`, & Colony` before `]` in a capture list naming `this` with no default capture).
2. **It replaces "RC never inside a lambda" with:** "inside a lambda body, `Colony.` is allowed only if every enclosing lambda up to the method body has a default `&` capture or carries P3. P3 appears only on a lambda that encloses a `Colony.` reference. No stored lambda (2.3) references or captures `Colony`."
3. **It replaces "in hoist mode `Col()` only inside lambdas" with:** "`GetColony()` appears only in P1 and in the two accessor definitions."
4. **The hoist rule:** a hoist exists if and only if the body uses `Colony.`.
5. **It deletes** the `this->Col()` rule.
6. **The guard (F11).** It keeps `local-named-like-moved` and widens it: no parameter, local, lambda parameter, init-capture or structured binding carries a moved name or `Colony`; and no class member or method other than the generated ones is named `Colony`, `GetColony` or `Colonies`.
7. **The shell template** becomes section 3.2's pinned text: the header shell, the public constructor declaration, and the S constructor definition. That includes `FRHColony() = default;` (never `{}`) and `MakeUnique<FRHColony>()`.
8. **Everything else stays:**
   - the literal and comment sequence;
   - the header interleave;
   - record content equal to the move list;
   - static methods untouched;
   - no qualifier after `.`, `->` or `::`;
   - counts equal to the rewriter's manifest.
9. **The planted-defect run gains** a `GetColony().M` inside a lambda, a missing P3, a P3 on a lambda with no reference, a stored lambda capturing `Colony`, `AddDefaulted` in the shell, and `FRHColony() {}`. Each must fail.

---

## 15. Deltas for options (b) and (c)

### 15.1 Option (b): the 28-byte list-of-one frame and a version bump (one gate)

Everything above holds, and step 2 also does the following.

**The frame.**
- `RHSaveFrame::ColonyTag = MakeTag('R','H','T','1')`, written with `RHSaveFrame::Write` straight after the last frame at the builder's HEAD (`'RHE8'`).
- The payload is `int32 ColonyCount` (1), then per colony `int32 Id` (0).
- Δ = 12 + 8 + 8 = **28 bytes** (`RHSaveFrame.h:24-25`). `GetColonyListBlockBytes()` measures it.

**The load.**
- `ReadColonyListBlock` runs after the last tail reader and validates only.
- A lost frame or a bad CRC is **silent**: it held nothing to lose, per the spirit of rulings 2 and 10. T4 must make a lost frame loud once the frame carries data.
- Count ≠ 1, a payload of the wrong size, or Id ≠ 0 each give one Warning, and colony 0 is kept.
- A transient, non-logging `Debug_WasColonyListFound()` keeps the silent path testable (F9).

**The version.** `RHSaveVersion` becomes vN, the next free after E8's v60. Every version literal in the suites moves (`git grep -n -P 'Ver == 60\b|"/60"|\(expect 60\)'`, ruling 7), and the checkpoints are re-cut.

**The battery.**
- About 33 version lines in 19 logs change from 60 to N.
- The whole-file sizes grow by exactly +28:
  - unmasked: CV8 (two numbers), DP11 (two numbers), EC1 twice, DC9;
  - masked: MX1 twice, MX9, CR9, EC6, XE10, CU10;
  - every "Colony saved (N bytes" count.
- Offsets do not move.
- **The check:** `diff_battery_delta.py --old-version 60 --new-version N --delta 28`.

**The corpus** (F8, defined mechanically, with no list). Every pair must be one of two shapes, with `dv = u32(post[4:8]) − u32(pre[4:8])`:
- **(A):** `len(post) == len(pre) + 28`; bytes 0-3 equal; `dv ∈ {0, N − 60}`, where 0 covers fixtures that write a literal old version (for example `RHSimCreditSuite.cpp:654` and `RHSimMarketSuite.cpp:1380`; the review lists 15 sites); `post[8:len(pre)] == pre[8:]`; and the last 28 bytes are the frame, identical in every slot.
- **(B):** `len(post) == len(pre)`; bytes 0-3 equal; `dv ∈ {0, N − 60}`; `post[8:] == pre[8:]`. These are fixtures truncated before the tail, such as `cavr5`, `deepr18`, `tramr16`, `depthr12` (`RHSimDepthSuite.cpp:828`), `transith17` (`RHSimTransitSuite.cpp:1720`) and `tunnelsh10` (K:14556).

Any other pair fails, and must be explained by the test that wrote it.

**The walk from the end** (F9). `'RHT1'` becomes the first frame every walk from the end meets. A header-only edit of its tag stops the walk (`RHSaveFrame.cpp:33-55`), so D7's early, walk-based read of `'RHD7'` loses that block too, with D7's Warning. A consistent edit of both header and footer, or a bad CRC, is harmless.

**`-colony` adds three checks:**
- **CO1:** `'RHT1'` sits straight after `'RHE8'`'s footer, found by tag, with payload `01 00 00 00 00 00 00 00`.
- **CO6, doctored frames (each resealed):**
  - a consistent tag edit loads silently;
  - a header-only tag edit gives exactly D7's lost-atrium Warning, pinned as collateral;
  - a bad CRC loads silently, and the readback says "lost";
  - count 0, 2, −1 and 1,000,000, Id 7, and 4 extra bytes each give one Warning.
- **CO7, the version fence:** a file of version N − 1 is refused with "Save format mismatch", and the live colony is untouched.

**Two more mutations:** `ReadColonyListBlock` not called (fails CO6), and `'RHT1'` written before `'RHE8'` (fails CO1 and E8's MX15).

### 15.2 Option (c): two gates

- **T1a** is section 6's step 1, committed on its own with its own bar:
  - the identity battery and a byte-identical corpus;
  - the playtest, the click test, `-scale`;
  - a director look.
- **T1b** is step 2 plus 15.1, with its own full bar and director look.
- **The cost:** 2 commits, 2 review stops, and 2 verification rounds instead of 1. The rebuilds are the same.
- **The benefit:** the pure move is a reviewed commit of its own, the cleanest bisect point in history.

---

## 16. Risks

1. **The build loop.** About 100,000 lines of compile on a swapping 8 GB machine. Mitigated by section 12's four mechanics and by tool-only fixes.
2. **Refresh drift.** Mitigated by the blocking refresh (section 11) and the standing check.
3. **`-scale` noise.** Medians of three, the 10% rule, and an A/B against `bin_pre_t1/` (2.8).
4. **T4's audit.**
   - The 588 hoists bind to colony 0.
   - T4 gives each body an intent: stepped, capital, viewed, or robot's. The player-wide callees named in section 4 get "capital".
   - Stored callbacks must resolve their colony at call time (2.3).
5. **The spine split** (section 1). T3 and T4 must separate per-colony from player-wide work in eight mixed steps; T1 leaves that as it is.
6. **Corpus determinism.** If a corpus or log difference appears that the gate cannot explain, re-run the pre-T1 battery from `bin_pre_t1/` (5.4's swap) to tell a T1 effect from a non-deterministic slot.
7. **The pre-binary swap against E5's guard** (5.4). The orchestrator decides which applies.
8. **Only the first compile can show** that UHT accepts the nested plain struct, the non-`UPROPERTY` `TArray<TUniquePtr<>>` and the user-declared constructor. The review and the prototype both leave this unverified. If UHT refuses, the fix belongs in the shell, which means a tool change and a header rebuild.

## 17. Deviations to log in `docs/design-decisions.md` (under (a))

1. **No save change and no version bump in T1.** The plan's "written as a list of one colony" moves to T4's file restructure (E7 precedent). This was put to the director.
2. **`FRHColony` holds the 180 C and DC members. The 77 "?" members stay on the subsystem;** their homes are T4's questions.
3. **One gate with an internal checkpoint:** the pure move is proven by token identity, an identity battery and a byte-identical corpus before step 2 is applied, with one commit.
4. **The bar:** every existing battery line identical, with no byte mask, apart from the three wall-clock lines. The plan's "masked only for the save byte count" becomes "masked for nothing".
5. **`SurfaceRoute`'s process-global scratch becomes a per-colony field.**
6. **The Lander's non-trade roles go behind `IsDepotBuilding` and three position accessors** with today's exact predicates and expressions.
7. **A committed member-classification CSV and check** run in every later gate.

## 18. The orchestrator rulings this design makes

1. **Mechanic (i).** 0 outside edits and a provable identity. (ii) costs 829 forwarders or 2,520 back-pointer references (section 2).
2. **A uniform hoist, `Colony` and `GetColony()`, plus `, &Colony` on explicit `[this]` captures.** `Col` already occurs 32 times in bodies. Stored lambdas never capture the colony (2.1-2.3).
3. **The record:** nested, plain, non-copyable, value-initialized through `= default` and `MakeUnique`, held as `TArray<TUniquePtr<>>`, and loaded in place (section 3).
4. **The 77 "?" stay on the subsystem,** because each member moves at most once, at its own gate. Their homes are provisional, and the spine's mixing is T3's and T4's to split (sections 1 and 4).
5. **The save** follows the director's answer; (a) is recommended (section 5, DIRECTOR QUESTION).
6. **One gate, two steps, one commit, no batching** under (a) (section 6).
7. **The robot's colony** is in the fragment, stamped through a required parameter, and not saved (section 7).
8. **No colony iteration in T1** (section 13).
9. **The Lander's roles:** one predicate and three exact positions (section 10).
10. **The scratch becomes per colony; the self-pointer is untouched** (3.4, 3.5).
11. **The salt rule** is recorded for T2 and T4 (section 8).
12. **The refresh blocks,** and a standing classification check runs in every later gate (section 11).
13. **The build plan's four mechanics** (section 12).
14. **The checks:** `diff_battery_delta.py`, per-run capture with `cmp_save_corpus.py`, and `-scale`'s budget with the 10% rule and A/B (sections 2.8 and 5).

## 19. Review responses

| Finding | Response |
|---|---|
| F1 MAJOR: the spine | Accepted. Section 1's table now shows direct and transitive counts (`callgraph.py` reproduces the review's figures: `StepQuota` reaches 39 C/DC, `StepUplink` 54 "?"). "For free" is deleted, sections 4 and 18.4 are reworded, and the crossings go in the header block. |
| F2 MAJOR: the director's choices | Accepted. One director question covers the split and the save (top of this document). Section 4's homes are "proposed (T4 decides)", and r1's misattribution ("the director answered" the bump rule, which is plan text at :59) is withdrawn. |
| F3 MAJOR: no frame | Accepted as option (a), recommended. The body is written for (a), and (b) and (c) are deltas (section 15). |
| F4 MAJOR: the corpus | Accepted. Per-run capture is a pre-T1 tooling commit after E6 lands; coverage is 251 byte-compared writes and ~2,015 size-only, of 2,266 at E5; the pre-T1 dylibs are kept with sha256 and a swap protocol (5.4). |
| F5 MAJOR: the refresh | Accepted. It blocks; heuristic rules only propose; a committed CSV and check script run in later gates (section 11). |
| F6 MAJOR: the build loop | Accepted. All four mechanics, with each rebuild counted as two compiles (section 12). |
| F7 MINOR: construction | Fixed. The shell is pinned: `FRHColony() = default;`, `MakeUnique<FRHColony>()`, and a public constructor; the tool's third change (3.2, 14). |
| F8 MINOR: corpus shapes (if 'RHT1' is kept) | Fixed in 15.1. The shapes are defined mechanically (`dv ∈ {0, N − 60}`, truncation by length), with the six known truncations named only as examples. |
| F9 MINOR: walk from the end (if 'RHT1' is kept) | Fixed in 15.1: CO6 names its edits, pins the header-only collateral loss, and adds a non-logging readback; T4 must make the lost frame loud. |
| F10 MINOR: `-scale` | Fixed. The agent-step +0.2 ms gate is dropped; every line is recorded with a 10% look; absolute failures go to an A/B against `bin_pre_t1/` (2.8). |
| F11 MINOR: the checker's guard | Fixed. The guard is item 6 of the contract (2.7) and of the checker (14). |
| F12 MINOR: T4 couplings | Fixed. Founding in world start, per-colony Ids, `DepotBindings` and presentation keys are in section 4's crossings list (items 8-9). |
| "Not verified": the 39-minute link stall | Withdrawn. It was in a draft of E6's entry that has since been edited. Section 12 cites the project memory's load and swap figures instead. |

---

## Appendix A. The MOVE list at `8da8c2a` (header order; "(m)" is `mutable`)

**C (93; SH:3032-4411):** RoadGraph, RobotPaths, NextPathSerial, Districts, NextDistrictId, GrowthProjects, GrowthCursor, GrowthCheckedSol, CityShare, Stocks, PendingOutputs, UplinkQueue, Buildings, Deposits, Tasks, ImportStock, Power, OrderLagSeconds, RepairClaims, PadQueues, SurveyHistory, FabricatorSpeedMul, FloorWarmth, Shafts, Tunnels, NextTunnelId, Caverns, NextCavernId, TransitLedgers, TransitCheckedSol, TramLines, NextTramId, HandoffHolds, FloorTable, SpoilPileKg, PendingBoreWork, FloorO2Kg, RatedFloors, bVaultRated, bFleetHold, HabO2Kg, RatedHabs, Furniture, NextFurnitureId, WallFeatures, NextWallFeatureId, Colonists, RecalledMarsBorn, NextColonistId, HomeClocks, DeficitEquivalent, FloorRoomCells, HabBayRooms, SeatPriority, StaffedSeats, SeatFillSols, HopeSmoothed, HopeBand, HopeGrowthStreakSols, bFirstBornAnnounced, BirthsOnMars, ProdAccKg, ProdRing, ProdRingFilled, ProdRingHead, ProductionCheckedSol, EconSlots, EconRingFilled, EconRingHead, EconCheckedSol, UnslottedLostG, BigArrivalStreakSols, bBigArrivalDone, MoveHomeAccSols, ImmigrantsTotal, DiscoverySeatHours, bEverHadCrew, MomentSols, MomentCounter, bFirstHarvestAnnounced, GardenFoodCumKg, PlantedCells, PlantedCrops, FloorClimate, DuctedFloors, ColonyCO2Kg, PlantRotation, bFirstCropAnnounced, WaterPotability, NextBuildingId, NextTaskId, bFleetDeployed, bQuietAnnounced.

**DC (87; SH:3057-4420):** ActiveRovers, bFreightWasActive, FreightBuiltVersion, bRoverWaitAnnounced, bCommuteAlerted, JobAssignLast, JobAssignNow, JobsVersion, CommuteMemo (m), CommuteFlows (m), DeepWalk (m), DistrictsVersion, DistrictGeo (m), DistrictMembers (m), DeepHomes (m), BedAssignVersion (m), GrowthVersion, GrowthNotes, GrowthPause, GrowthSolStarted, GrowthSolFinished, GrowthSolRefused, LastBlackoutNight, PowerHistory, LastPowerSampleHour, WarmAlerted, WarmthVersion, DeepestShaft, ShaftColumns, BorerShaft, ShaftsVersion, ShaftReachVersion, TunnelsVersion, CavernsVersion, bHasTransit, LiftVersion, TransitWaitVersion, TramVersion, DeepNet (m), ReserveMemos (m), DigCache (m), FloorIndexByLevel, FloorCellsVersion, FloorsNotedSmall, HabNetsNotedSmall, HabBayOwner, HabNets, HabMembers, FurnishedCells, ColonistBedPiece (m), ColonistSeatPiece, FurnitureIndexById, ComfortPieces, FurnitureVersion, ZoningVersion, ColonistSetVersion, bBedKeyValid (m), BedKeyColonists (m), BedKeyFurniture (m), BedKeyZoning (m), ColonistIndexById (m), bColonistIndexDirty (m), FeatureByEdge, HomeRations, ColonistJobs, ColonistStations, StationFilled, StationCapacity, LastIdleColonists, LastOpenSeats, EconIndex, bSoilSpentAnnounced, ProducingCells, bGardenThirstAnnounced, GardenPowerDrawW, bGrowLightsDark, bGardenDarkAnnounced, GardenLastDarkSimSeconds, FreshWaterThisStepKg, bWaterQualityAnnounced, bFoodLowAnnounced, OutpostHintAnnounced, IceLowAnnounced, ComfortsSupplied, FleetCounts, LabYieldSeatSum, WorkstationYieldSeatSum.

No moved declaration shares a line with a member of another class (SH:3092, :3371 and :3797 are wholly DC), and no moved initializer names another member. Step 2 adds `Id` and `SurfaceRouteScratch` to the record (3.2).

## Appendix B. The "?" members that stay, by group

- **Earth (22):** CreditBalanceMcr, CreditAwardedMcr, CreditSpentMcr, ShipMassCapKg, ManifestLanded, ExportLines, SalesPaidMcr, QuotaArrearsKg, ShipAlertStage, EarthTension, IdentityAxis, bEarthDemandPending, QuotaPhase, AwardMassKg, ManifestItems, ManifestPricesMcr, ShipArrivalSimSeconds, QuotaMetSol, ActiveQuota, QuotaOpenedSol, QuotaRepeatRound, PendingResearchFundingKg.
- **Relations (10):** RivalRelations, ClosedRoutes, HiddenTensions, HumanNatureAxis, CovertAttempts, SabotageSols, DiscoveredRivals, EmbargoGrace, DefectedRivals, Influence. E6 replaces four of them with `TradeRules`.
- **Market and convoys (9):** RivalEconomies (m), Markets (m), SyncedRivalsGeneration (m), MarketCheckedSol, CreditReservedMcr, MarketSalesMcr, MarketClearings, Convoys, NextConvoyId.
- **The mark (24):** bCurrencyIssued, CurrencyIssueSol, CurrencyOpenSol, bWithdrawPending, WithdrawClearSol, RateMcr, IdentityViewMilli, TensionViewMilli, Journal, BlocSoldMmk, BlocBoughtMmk, BlocFill, BlocHead, MarkBalanceMmk, MarkEarnedMmk, MarkSpentMmk, MarkReservedMmk, DeskCreditMcr, CurrencyRedeemedMcr, BlocMembers, DeskOrders, DeskClearings, LastRateStepMcr, RateStepNote.
- **Crises and endings (8):** ActiveCrisis, CrisisRemainingSols, CrisisCheckSols, CrisisCount, bEndingDeclared, DeclaredEnding, PathSustainSols, bEpilogueDeclared.
- **The event latch (2):** bEventWasActive, LastEventType.
- **Discoveries (1):** DiscoveryLog.
- **Solidarity (1):** SolidarityHope.

---

# ORCHESTRATOR RULINGS (2026-10-28, after r2)
- **The pre-T1 binary: no swap-back exception.** Section 5.4's manual swap of the pre-T1 dylibs is withdrawn; E5's guard ("never reinstall the pre-change binary") stands unchanged. Instead, before T1's first compile, run the pre-T1 battery twice with per-run save capture on the untouched binary. Both runs must agree byte for byte, which proves the capture repeats. The second run is the baseline. The pre-T1 dylibs are still copied aside with their sha256, as evidence only, never reinstalled.
- **The checkpoints** in the director question are the crew-phase hand-play saves that `scripts/unreal/rh_checkpoints.sh` makes and `rh_sandbox.sh` loads.
- **Status.** The director question has been put to James; this design is final once he answers. It moves into `docs/qa/` only after E6 commits, and it must be refreshed at E8's HEAD before any build (section 11, which blocks on unclassified members).

# DIRECTOR ANSWER (2026-10-28)

- **T1's shape: (a).** One gate with no save change and no version bump. Current saves and the two crew-phase checkpoints keep loading. The plan's "save written as a list of one colony" (`docs/qa/2026-10-20/territories-plan.md`:59, :75) moves to T4, which restructures the file anyway, as E7 dropped its save block. Every battery line stays identical apart from wall-clock timing, one new test log (`colony.log`) is added, and every captured save is byte-identical. Options (b) (a 28-byte list-of-one frame and a version bump) and (c) (two gates) are declined.
- This design is final. It is refreshed at E8's HEAD before any build (section 11, which blocks on unclassified members), and T1 is built after E8 and the idle robots.
