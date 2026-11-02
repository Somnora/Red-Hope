export const meta = {
  name: 'gate-idle-robots',
  description: 'Red Hope gate: idle robots look busy (display only; NO sim change, NO save version): one builder builds and gates it from the brief; read-only reviewers audit; fix loop',
  phases: [
    { title: 'Build', detail: 'one builder: the before binary (timer only), the pad ring, the inspections, RH.IdleRobots, the robots.idle click step, full gate' },
    { title: 'Review', detail: 'read-only lenses: spec, code, tests, gate evidence (sim untouched), presentation' },
    { title: 'Fix', detail: 'builder fixes blocking findings and re-gates' },
  ],
}

// Gate "idle robots" (display only). Built AFTER BM-7 (a free Move order; still save v60) is committed. It changes NO file under
// Source/RedHopeSim, docs/data or Content/RedHope/Data, adds no battery suite and takes no save version. The orchestrator must pass:
//   head           - git -C /Volumes/Unreal/red_hope/red_hope rev-parse --short HEAD, after BM-7's commit (and this script's docs commit) land.
//   base           - the battery snapshot BM-7's gate left: 'Saved/baselines/battery_post_bm7' (it holds BM-7's moves.log).
//   clickSteps     - (integer) the main click test's step count at that HEAD (the "CLICKTEST TOTAL pass=N" of BM-7's counted PASS logs: 114). This gate adds exactly 1.
//   deepbenchSteps - (integer) the deepbench scene's step count at that HEAD (9 after BM-7; recount). Unchanged by this gate.
//   batteryRuns    - (integer) the run count expected: the number of *.log files in base (64 after BM-7; no new suite).
//   saveVersion    - (integer) RHSaveVersion at HEAD (60; BM-7 took none). This gate must NOT change it.
// Patched 2026-11-02 after the orchestrator's audit of 2026-11-01: a run note and a work folder on the SSD; one label per click run
// and ruling 26 for a dropped press; quiet starts; the frame check under rulings 18, 22, 24 and 25; the pad-off path spelled out;
// pre-steps (a0) and (a2); rh_capture in the one-process rule.
//   playtestBase   - optional: the playtest baseline (default Saved/baselines/ind_full_playtest.txt).

const REPO = '/Volumes/Unreal/red_hope/red_hope'
const WORK = '/Volumes/Unreal/red_hope/wip-backups/idle-work'
const PF = 'docs/qa/2026-10-26/preflight'
// Ruling 23: one hash over every change outside docs/ (docs/data included), the untracked files there and both installed binaries.
// A fix round that only edits docs/ must reproduce the last full gate run's hash instead of re-running the gate.
const FINGERPRINT_CMD = `cd ${REPO} && { git diff --binary HEAD -- Source Content Config scripts docs/data; git ls-files -z --others --exclude-standard -- Source Content Config scripts docs/data | xargs -0 shasum -a 256; shasum -a 256 Binaries/Mac/libUnrealEditor-RedHope.dylib Binaries/Mac/libUnrealEditor-RedHopeSim.dylib; } | shasum -a 256`
const DOC_ONLY_FIX = `DOC-ONLY ROUND (ORCHESTRATOR RULING 23): if EVERY blocking finding can be fixed by editing files under docs/ alone (docs/data does not count as docs), fix them there, take only the minors that are docs/-only too (list each other minor in open_issues as a follow-up), and rebuild nothing and re-run nothing: run the fingerprint command (THE GATE, FINGERPRINT) and report its hash in gate.fingerprint beside the one recorded at the last full run, which it must equal; set doc_only_round to true; carry every other gate result over as last recorded (the frame check, the click runs and the captures stand: the fingerprint proves the source and both binaries unchanged). If any fix needs code, data, a test run or a capture, or the fingerprint differs, the round is not doc-only.`
const HEAD = args.head
const BASE = args.base
const CLICK_STEPS = Number(args.clickSteps)
const DEEPBENCH_STEPS = Number(args.deepbenchSteps)
const BATTERY_RUNS = Number(args.batteryRuns)
const SAVE_VERSION = Number(args.saveVersion)
const PLAYTEST_BASE = args.playtestBase || args.playtestBaseline || 'Saved/baselines/ind_full_playtest.txt'
const DESIGN = 'docs/qa/2026-10-28/robot-idle-design.md'
const BRIEF = 'docs/qa/2026-10-28/robot-idle-brief.md'
if (!HEAD || !BASE || args.clickSteps == null || args.deepbenchSteps == null || args.batteryRuns == null || args.saveVersion == null) {
  return { error: 'missing args: head, base, clickSteps, deepbenchSteps, batteryRuns, saveVersion are required (see the comment at the top)' }
}
if (![CLICK_STEPS, DEEPBENCH_STEPS, BATTERY_RUNS, SAVE_VERSION].every(n => Number.isInteger(n) && n > 0)) {
  return { error: 'clickSteps, deepbenchSteps, batteryRuns and saveVersion must be positive integers' }
}
const NEW_CLICK = CLICK_STEPS + 1

const RUN_NOTE = `OPERATING NOTE FROM THE ORCHESTRATOR (every round): (1) Every background job you start is KILLED the moment you return your structured report, and a Bash call times out after 10 minutes. Start each long run (Build.sh, the battery at 40-90 minutes, the playtest, each click run, each frame session, each capture session) in the background and WAIT for it inside your turn (Monitor with an until-loop on its completion line, or repeated checks); never return while one of yours is still running. E8's round 0 battery died at run 23 of 63 exactly that way. This Mac has no timeout command: never prefix a command with it. (2) Before every Unreal step, confirm both installed dylibs exist and are newer than every changed source file, and that the installed RedHope dylib is the final one (outside the measure or capture script's own copy-and-restore). (3) The Mac has 8 GB and the user's own apps are open: if swap is nearly full, say so in the report with the numbers. (4) The Mac has slept and rebooted during gates before: if the clock jumps between two of your steps, check the volume, the tree and both binaries before going on, and re-run any session the jump cut through. (5) A reboot wipes /private/tmp, the session scratchpad included: keep your working files (scripts, command files, session outputs, the before dylib copy, drafts) under ${WORK}/ on the SSD (create it; one subfolder per round, r0, r1, ...) and name that folder in your report. (6) Power and disk: before each Unreal step run pmset -g batt and df -h /. A run started on 'Battery Power' does not count: say so in open_issues and re-run it once the Mac is back on AC. If / shows under 6 GB available, do not start the step: return with stop_reason 'internal disk under 6 GB free' and your work so far. Never delete, move or clear anything to make room.`

const RULES = `
HARD RULES (this tree is shared; breaking these has lost work before):
- NEVER run git stash, checkout, reset, add, commit or push, and never git worktree add. The orchestrator commits. Do not revert other edits.
- ONE Unreal process at a time (8 GB machine, heavily swapping): never run two of UnrealEditor / UnrealEditor-Cmd / Build.sh / rh_battery / rh_clicktest / rh_playtest / rh_capture / an Unreal commandlet concurrently; wait for each to exit before the next. Never kill a process you did not start.
- Build: "/Volumes/Unreal/UE_5.8/Engine/Build/BatchFiles/Mac/Build.sh" red_hopeEditor Mac Development -project="${REPO}/red_hope.uproject" -waitmutex   (-Wshadow is an error). Before the gate, also compile once with -DisableAdaptiveUnity, then rebuild normally for the final binary; every gate log must be newer than the final dylibs.
- DISPLAY ONLY: no file under Source/RedHopeSim, docs/data or Content/RedHope/Data may change (git diff --stat over them must be empty at the end). RedHopeSim never depends on RedHope. No random draw anywhere in the new code (no FMath::Rand*, no FRandomStream): everything is keyed on robot index, inspection count and sim-paced time.
- zsh does not word-split $VARS (list paths explicitly). git grep -E ignores \\b on this machine: use git grep -P.
- Never print the SecurityToken in Config/DefaultEngine.ini. Do not spawn subagents.
- QUIET STARTS (ruling 22's conditions): start every counted click run and every frame session only when uptime's 1-minute load, read just before it, is under 5 and pgrep -x 'UnrealEditor|UnrealEditor-Cmd|CrashReportClient|ShaderCompileWorker' prints nothing, with no Build.sh or rh_*.sh script running (an idle UnrealTraceServer is allowed; say so). If the machine stays busy, wait (up to 45 minutes per run; never kill a process you did not start) and say so; a run that cannot start quietly within that is a busy machine for the orchestrator (stop_reason), never a pass. Record the load and the pgrep output for every run, counted or not.
- CLICK RUNS (ORCHESTRATOR RULING 26): every run gets its OWN label, written in your notes before it starts, so no log overwrites another. The harness's dropped-press signature: an RHClick: line with reached=YES, then a LeftMouseButton down and up both handled=1 at the same point, then that step's first CLICKTEST check FAILs (/Volumes/Unreal/red_hope/wip-backups/bm7-work/r1/diag/scan_untaken2.py finds it). A counted run whose first failing step is a step older than this gate and carries the signature is reported with its log, its start load and its failing steps, stays on the record and is not counted. ONE replacement run under a new label, named before it starts and started quietly, takes its place and counts only if it passes in full. A failure in robots.idle, a failure without the signature, or a second failure in the same slot is never replaced: report it, and fix the cause if it is this gate's.
- Director rules: no combat; Hope is never read or written; no player-facing string is added (the cheat and log texts are developer-facing).
- STOPPING: if a precondition fails (pre-steps (a0), (a), (a2) or (b)), set stop_reason in the report, complete=false, and return; do not work around it with a worktree or a second build. The orchestrator clears it, not you.
`

const SPEC = `
SPEC AND AUTHORITY (highest first):
1. ${DESIGN} "# DIRECTOR ANSWER (2026-10-28)" ((a) display only, after E8) and "# DIRECTOR ANSWER (2026-10-29)" (Q-A (a): a loose ring well clear of the pad, about 2.5 m out from its edge, facing outward, 8 spots then 12; Q-B (a): a waiting robot inspects after 20 s of waiting, placeholder).
2. ${PF}/ORCHESTRATOR-RULINGS.md (all of it; for this gate especially ruling 7 (git grep -P), rulings 18, 22, 24 and 25 (the frame check: quiet pairs, every counted pair joins, windows clear of a sol boundary), ruling 23 (the fingerprint) and ruling 26 (a click run lost to the harness's dropped press)). Where a ruling and the brief differ on how the gate is run, the ruling wins.
3. ${BRIEF}: read ALL of it. Its anchors are by symbol (URHAgentVisualizerSubsystem::AdvanceDrawn's idle branch, ParkSlotCm, FreeWalkTo, Tick's bDeepDraw / bDrive dispatch, Tick's facing block, the WantClip block, FRobotTrip, ResetTracking; URHSimWorldSubsystem::FindNearestChargePad; RHCheats.cpp's RH.Sites; RHClickTest.cpp's build.site step as the model for a presentation step): re-anchor each at HEAD and list what moved. Its UNCERTAIN defaults are taken unless a higher layer settles them; list every one under deviations.
4. ${DESIGN} "# DESIGN (2026-10-29)": the reasoning behind the brief; the brief wins where they differ.
`

const GATE = `
THE GATE (run all of it after the final build from final source; record exact numbers and log paths):
PRE-STEPS, before writing any behaviour (round 0 ONLY; a fix round reuses them as recorded), under pre_steps:
  (a0) git -C ${REPO} cat-file -e HEAD:${DESIGN} and git -C ${REPO} cat-file -e HEAD:${BRIEF} both succeed (both are committed at HEAD); otherwise stop (stop_reason).
  (a) read RHSaveVersion at HEAD by symbol and confirm it is ${SAVE_VERSION}; otherwise stop (stop_reason).
  (a2) confirm ${BASE} is BM-7's snapshot: it holds baseline.log plus one log per suite in scripts/sim/rh_battery.sh's SUITES at HEAD, moves.log (BM-7's suite) among them, and no other log; otherwise stop (stop_reason).
  (b) count the logs in ${BASE} and confirm it is ${BATTERY_RUNS}; otherwise stop (stop_reason).
  (c) THE BEFORE BINARY: add ONLY the brief's section 2 first bullet (the tick timer and the three route-log counts, which change no behaviour), build, copy libUnrealEditor-RedHope.dylib with its sha256 to Saved/orchestrator/idle_bin_before/ (and a second copy with its sha256 to ${WORK}/pre/), record libUnrealEditor-RedHopeSim.dylib's sha256 (it must never change in this gate), and with this binary take the before tick-time measurements and the before captures (brief section 4). Never reinstall the before dylib outside the capture/measure script's own copy-and-restore; after each use, restore the final dylib and confirm its hash.
1. bash scripts/sim/rh_battery.sh 10 -> "BATTERY COMPLETE: 0 suite(s) not green" and ${BATTERY_RUNS} runs; logs newer than both dylibs; nothing in Source/RedHopeSim or docs/data newer than Saved/RHBattery/baseline.log.
2. python3 scripts/sim/diff_battery.py ${BASE} Saved/RHBattery: timing digits only; no new suite; no version or size line moves. Any other differing line means the sim moved: STOP, find the cause, report it.
3. Pins in baseline.log: "power: gen 50 W load 20 W" and "deposit Regolith_A: 40645 kg".
4. git -C ${REPO} diff --stat -- Source/RedHopeSim docs/data Content/RedHope/Data is EMPTY; the RedHopeSim dylib's sha256 equals pre-step (c)'s.
5. RH_PLAYTEST_DISTRICTS=1 bash scripts/sim/rh_playtest.sh 110 idle_full guide full, then cmp against ${PLAYTEST_BASE} -> byte-identical.
6. Click test (rh_clicktest.sh takes <label> <timeout-s>; every run its OWN label; quiet starts and ruling 26 as RULES say): RH_CLICKTEST_RES=1440x860 bash scripts/unreal/rh_clicktest.sh idle_click1 450, then idle_click2 and idle_click3 the same way: THREE counted passes, each "CLICKTEST TOTAL pass=${NEW_CLICK} fail=0" (${CLICK_STEPS} + exactly 1, the new step robots.idle, brief section 3; no existing step removed or weakened; leak.total still passes). RH_CLICKTEST_SCENE=deepbench RH_CLICKTEST_RES=1440x860 bash scripts/unreal/rh_clicktest.sh idle_deepbench 450 -> PASS at ${DEEPBENCH_STEPS} (unchanged). Record the load and pgrep output before each run; disclose every failed or timed-out run with its log path and failing step, and name any replacement and the run it replaces.
7. Frame check (brief section 4, judged under ORCHESTRATOR RULINGS 18, 22, 24 and 25): the stated save with two or more working surface pads and idle robots; rh.RobotRouteLog 1 at 1x, each window 30 s; before/final pairs alternating, each binary installed only through the measure script's own copy-and-restore (the final dylib restored and its sha256 confirmed after each use).
   - A pair counts only when uptime's 1-minute load, read from a separate shell just before its first window, is under 5, with no other Unreal, Build.sh or rh_*.sh process running (ruling 18; if the editor alone holds that reading at 5 or more, ruling 18's fallback applies, and you say so).
   - Fix each session's schedule before it runs (ruling 24 (1)): it runs its pairs to the end and is never stopped early, repeated or dropped because of its results. EVERY counted pair joins the median, never a chosen subset. Run a further session only while fewer than three pairs count; all of its counted pairs join too.
   - Place every window clear of a sol boundary and of any autosave (ruling 25, "Later gates"): check the sol at both ends of each window in the log. A window that crosses one is reported and its pair does not count; the next session's schedule moves, never the bar.
   - The bar: the median over every counted pair of (final - before) tick time is at most +0.2 ms. Report every pair (counted or not, with its load), the median over every counted pair, and beside it, for the record, the first three counted pairs in order. If three pairs cannot count within 45 minutes, that is a busy machine for the orchestrator (stop_reason), never a pass and never a director question.
   - Report the counts the log printed (waiting at a pad, Lander rank, inspecting).
8. Captures before and after from the SAME command file (log the binary hash) at 1440x860 under Saved/RHCapture/idle/: the idle ring at a pad by day and by night, an inspection at a building, the Lander rank with every pad switched off (each pad switched off as the brief's section 3 does it, or through a disclosed cheat, and back on afterwards), a robot leaving its slot for a new job, a wide colony view. LOOK at every one with the Read tool; the ring's figures must not overlap one another, the pad or a building, and must face outward.
9. RH.IdleRobots output on the capture save, quoted in the report.
10. Docs: the docs/design-decisions.md entry (what it builds; every deviation and UNCERTAIN default; every frame-check pair with its load and whether it counted, and the median over every counted pair; the click runs with their labels, loads and counts, and any ruling 26 replacement; the capture list; that gate-d-wording.md gains no block because no player-facing string was added).
FINGERPRINT (ORCHESTRATOR RULING 23), at the end of every FULL gate run: run exactly   ${FINGERPRINT_CMD}   and report the hash as gate.fingerprint. It covers every change outside docs/ (docs/data included), the untracked files there and both installed binaries; a later fix round that only edits docs/ must reproduce it.
`

const BUILD_PROMPT = `You are the ONE builder for the Red Hope gate "idle robots look busy" (display only). Repo: ${REPO} (UE 5.8). HEAD ${HEAD} (BM-7 is committed, save v${SAVE_VERSION}; the tree should be clean apart from untracked scratch).
${SPEC}
STEPS: (0) preconditions: no Unreal/battery/Build.sh process running (pgrep), at least 6 GB free on / and 2 GB on /Volumes/Unreal; the Mac on AC power (pmset -g batt); pre-steps (a0), (a), (a2) and (b); on any miss stop with stop_reason and complete=false. (1) Read the design and the brief; re-anchor every symbol at HEAD and list what moved (E5-E8, D7 and BM-7 touched RHCheats.cpp and RHClickTest.cpp; BM-7 also RHPlayerController.cpp and RHCommandDeck.cpp). (2) Pre-step (c): the before binary, the before measurements and the before captures. (3) Build the brief's section 1 in order: the pad cache, the slot ring with per-pad slot holding, the outward facing, the inspection (list, waits, target, walk, Operate clip, return), the job-arrival reset; then section 2's RH.IdleRobots and accessor; then section 3's robots.idle click step (the main scene has no Charging Pad at HEAD; the brief's section 3 says how the pad is made and switched off and on). (4) The full gate. (5) The docs.
DO NOT: change any sim file, data or DataTable; change rovers, underground robots, riders, road trips or the catch-up walk; change ParkAfterIdleS, FreeWalkCmS, PlanRoute or the Lander rank layout; move the docked robot's drawing on the pad; add a random draw; add a player-facing string; add a battery suite.
Match the codebase's style (comment density, naming, idioms; see RHAgentVisualizerSubsystem.cpp itself).
${GATE}
${RULES}
${RUN_NOTE}
Return the structured report; be faithful about anything failed, skipped or uncertain. List every file changed or created.`

const BUILD_SCHEMA = {
  type: 'object',
  properties: {
    complete: { type: 'boolean' },
    stop_reason: { type: 'string', description: 'EMPTY unless the build stopped at a precondition; then the exact reason, for the orchestrator' },
    checklist: { type: 'array', items: { type: 'object', properties: { item: { type: 'string' }, status: { type: 'string', enum: ['done', 'partial', 'missing'] }, note: { type: 'string' } }, required: ['item', 'status'] } },
    files_changed: { type: 'array', items: { type: 'string' } },
    summary_of_changes: { type: 'string' },
    reanchors: { type: 'string', description: 'brief symbols as found at HEAD, with what moved' },
    gate: {
      type: 'object',
      properties: {
        pre_steps: { type: 'string', description: 'RHSaveVersion at HEAD; the base log count; the before dylib path and sha256; the RedHopeSim dylib sha256' },
        battery_result: { type: 'string' }, battery_diff: { type: 'string', description: 'must be timing digits only; otherwise every differing line with its cause' },
        sim_untouched: { type: 'string', description: 'the empty git diff --stat over Source/RedHopeSim, docs/data and Content/RedHope/Data, and the unchanged RedHopeSim dylib sha256' },
        pins: { type: 'string' }, playtest_cmp: { type: 'string' },
        clicktest_runs: { type: 'array', items: { type: 'string' } }, deepbench: { type: 'string' },
        frame_check: { type: 'string', description: 'the save, every before/final pair, the median delta against +0.2 ms, and the state counts' },
        idle_robots_cheat: { type: 'string' },
        log_paths: { type: 'array', items: { type: 'string' } },
        fingerprint: { type: 'string', description: 'ruling 23: the FINGERPRINT hash from the last full gate run; in a doc-only round, the recomputed hash beside it (they must be equal)' },
      },
      required: ['pre_steps', 'battery_result', 'battery_diff', 'sim_untouched', 'pins', 'playtest_cmp', 'clicktest_runs', 'deepbench', 'frame_check', 'idle_robots_cheat', 'fingerprint'],
    },
    captures: { type: 'array', items: { type: 'string' } },
    own_visual_judgement: { type: 'string' },
    deviations: { type: 'array', items: { type: 'string' } },
    open_issues: { type: 'array', items: { type: 'string' } },
    doc_only_round: { type: 'boolean', description: 'ruling 23: true only for a fix round that edited files under docs/ (not docs/data) and rebuilt and re-ran nothing' },
    director_questions: { type: 'array', items: { type: 'string' }, description: 'only if something genuinely needs the director; plain lettered options with a recommendation and modelled numbers' },
  },
  required: ['complete', 'stop_reason', 'checklist', 'files_changed', 'summary_of_changes', 'reanchors', 'gate', 'captures', 'own_visual_judgement', 'deviations', 'open_issues', 'director_questions'],
}

const FINDINGS_SCHEMA = {
  type: 'object',
  properties: {
    verdict: { type: 'string', enum: ['PASS', 'FAIL'] },
    findings: { type: 'array', items: { type: 'object', properties: { severity: { type: 'string', enum: ['blocking', 'minor'] }, title: { type: 'string' }, file: { type: 'string' }, line: { type: 'integer' }, evidence: { type: 'string' }, required_fix: { type: 'string' } }, required: ['severity', 'title', 'evidence', 'required_fix'] } },
    checked_and_sound: { type: 'array', items: { type: 'string' } },
  },
  required: ['verdict', 'findings', 'checked_and_sound'],
}

const READONLY = `You are a READ-ONLY reviewer. Do NOT edit any file in ${REPO}. Do NOT build; do NOT run UnrealEditor, UnrealEditor-Cmd, Build.sh or any rh_*.sh script. NEVER run git stash/checkout/reset/add/commit. You may read files, run git diff / git status / git show / git grep -P, python3 scripts/sim/diff_battery.py, diff, cmp, shasum, stat, grep -P over Saved/ logs, and Read PNG captures. Do not spawn subagents. "blocking" = must be fixed before commit (a spec miss, a bug, a broken or unproven gate, a click check that cannot fail, ANY sim file or battery line that moved). Cite file:line.`

const LENSES = [
  { key: 'spec', prompt: `Lens: SPEC CONFORMANCE. Compare the uncommitted work (git -C ${REPO} diff and new files in git status) item by item against ${BRIEF} sections 1-5, under ${DESIGN}'s two DIRECTOR ANSWER sections. Each of section 1's nine decisions is present and correct, or a finding: the pad from FindNearestChargePad on the SIM position with the Lander / false case falling back to ParkSlotCm unchanged; the cache and its refresh triggers; ring radii (R1 = PadHalf + RobotClearanceCm + 250, ring 2 at R1 + 220), 8 + 12 slots, slot 0 toward the Lander, obstacle skip, per-pad slot holding and release; outward facing at the slot; InspectWaitS = 20 with the offset (RobotIndex * 7) % InspectWaitS on the first wait; the building list (Level 0, complete, not the pad, within 2000 cm, ascending Id); the target (RobotIndex + InspectCount) % N; the walk at FreeWalkCmS x Pace to the footprint edge; Operate for 6 sim s with the sim's task fragment winning; job arrival clearing everything. Section 5's "do not" list: none taken. Every UNCERTAIN default listed as a deviation.` },
  { key: 'code', prompt: `Lens: CODE CORRECTNESS of the uncommitted C++ (git -C ${REPO} diff -- Source). Confirm: nothing under Source/RedHopeSim changed; the new branch is reached only for surface walkers (not bDeepDraw, not bDrive, not rovers) and only with rh.RobotRouting != 0; slot holding cannot leak (released on task, pad change, untrack and ResetTracking on reload) or double-assign a slot; the cache cannot hold a dangling pad (demolished, switched off, under construction, or a reload) and refreshes as the brief says; FindNearestChargePad is not called every frame for every robot; PlanRoute runs only on a goal change (FreeWalkTo's 100 cm rule) and not per frame; the Operate override never masks a real sim task clip; the facing target applies only at the slot or the inspected building; no FMath::Rand / FRandomStream / time-of-day seed; the tick timer adds no measurable work when rh.RobotRouteLog is 0 beyond two FPlatformTime reads; -Wshadow-safe names; RH.IdleRobots is read-only.` },
  { key: 'tests', prompt: `Lens: TEST STRENGTH. Read the robots.idle step in RHClickTest.cpp and its lines in the builder's click logs. Can it fail? It must assert the figure's distance band [R1 - 60, R1 + 220 + 60] from the pad centre AND the floor PadHalf + RobotClearanceCm, and that switching the pad off moves the robot's goal to its Lander rank slot (ParkSlotCm) and back on restores the pad; a check that would pass with today's Lander-rank behaviour is BLOCKING. Is the wait bounded and sim-paced, so a slow machine fails loudly rather than hangs? Does it leave the scene as it found it (the pad switched back on) so later steps are unaffected? Confirm the step count is exactly ${NEW_CLICK} in three counted PASS logs under their own labels, and the deepbench scene still passes ${DEEPBENCH_STEPS}. The pad-off path must be the player's own: the pad selected with PC->Debug_SelectBuilding (no world click) and the deck's Switch OFF / Switch ON button pressed with PushPress. leak.total must still pass, and any world click added for the step must be counted in its ExpectedWorld.` },
  { key: 'gate', prompt: `Lens: GATE EVIDENCE AUDIT, from files on disk, not the builder's word. Saved/RHBattery logs newer than both dylibs (stat); nothing in Source/RedHopeSim or docs/data newer than baseline.log; BATTERY COMPLETE with 0 not green and ${BATTERY_RUNS} runs; run python3 scripts/sim/diff_battery.py ${BASE} Saved/RHBattery YOURSELF and require timing digits only (any other line is BLOCKING: the sim moved). git -C ${REPO} diff --stat -- Source/RedHopeSim docs/data Content/RedHope/Data must be empty; the installed RedHopeSim dylib's sha256 equals the pre-step's. Pins present. Saved/RHPlaytest/idle_full.txt cmp-identical to ${PLAYTEST_BASE} and newer than the dylibs. Three counted click logs under their own labels with "CLICKTEST TOTAL pass=${NEW_CLICK} fail=0" (check their start and end timestamps inside the logs: sequential, not overlapping), each started quietly with its load and pgrep output recorded, and the deepbench log at ${DEEPBENCH_STEPS}; every failed run disclosed. A replacement counts only under ORCHESTRATOR RULING 26: the run it replaces must have its first failing step in a step older than this gate, carrying the dropped-press signature (read the log yourself); otherwise BLOCKING. The frame check: every before/final pair present in logs with its load; a pair counted only under ruling 18 (1-minute load under 5, read just before its first window); EVERY counted pair in the median, no chosen subset, and no session stopped early, repeated or dropped for its results (ruling 24 (1)); no counted window crossing a sol boundary or an autosave (ruling 25); the before dylib at Saved/orchestrator/idle_bin_before/ with the recorded sha256, the final dylib restored after each use (installed RedHope dylib sha256 differs from the before one), and the median delta within +0.2 ms: recompute it from the logs yourself. FINGERPRINT (ORCHESTRATOR RULING 23): run ${FINGERPRINT_CMD} yourself and compare it with the report's gate.fingerprint; a mismatch means code, data or a binary changed after the gate ran: BLOCKING. If the report sets doc_only_round true, that equality is the proof that nothing outside docs/ changed, and the gate evidence is the last full run's (its logs still newer than the binaries); confirm each blocking finding of the last round was a docs fix: if one needed code, data, a test run or a capture, the round needed a full re-run: BLOCKING.` },
  { key: 'presentation', prompt: `Lens: PRESENTATION. Read every capture under ${REPO}/Saved/RHCapture/idle/ (before and after) with the Read tool. After: idle robots stand in a loose ring about 2.5 m clear of the pad's edge, facing outward, not overlapping each other, the pad or any building, and NOT reading as a queue for the charger; a charging robot is still drawn on the pad; an inspecting robot stands beside (not inside) a building playing a work pose; with every pad off the robots use the Lander rank as before; at night the ring still reads. Before: the Lander rank. Compare before/after wide views for anything else that changed (it must not). Check docs/design-decisions.md has the entry the brief asks for, and that gate-d-wording.md gained no block and the entry says why.` },
]

function reviewPrompt(lens, report) {
  return `${READONLY}\n\nReviewing the Red Hope gate "idle robots look busy" (display only), built uncommitted in ${REPO} on HEAD ${HEAD}.\n\n${lens.prompt}\n\nThe builder's report (claims to verify, not trust):\n${JSON.stringify(report, null, 1)}`
}

function stopFor(r) {
  return r && typeof r.stop_reason === 'string' && r.stop_reason.trim().length > 0
}

phase('Build')
let report = await agent(BUILD_PROMPT, { label: 'builder:idle', phase: 'Build', schema: BUILD_SCHEMA })
if (!report) { return { error: 'builder died' } }
log(`builder done: complete=${report.complete}`)
if (stopFor(report)) { return { final: report, cleared: false, needsOrchestrator: report.stop_reason } }
if (report.director_questions && report.director_questions.length) {
  return { final: report, cleared: false, needsDirector: report.director_questions }
}
const history = []
for (let round = 0; round < 4; round++) {
  phase('Review')
  const reviews = (await parallel(LENSES.map(l => () =>
    agent(reviewPrompt(l, report), { label: `review:${l.key}:r${round}`, phase: 'Review', schema: FINDINGS_SCHEMA })
      .then(r => r ? { lens: l.key, ...r } : null)))).filter(Boolean)
  const blocking = reviews.flatMap(r => r.findings.filter(f => f.severity === 'blocking').map(f => ({ lens: r.lens, ...f })))
  history.push({ round, reviews: reviews.map(r => ({ lens: r.lens, verdict: r.verdict, findings: r.findings.map(f => f.severity + ': ' + f.title) })) })
  log(`round ${round}: ${blocking.length} blocking`)
  if (reviews.length < LENSES.length) { const missing = LENSES.map(l => l.key).filter(k => !reviews.some(r => r.lens === k)); log(`round ${round}: reviewer(s) returned nothing: ${missing.join(', ')}; NOT cleared`); return { final: report, history, cleared: false, reviewersFailed: missing, failedRound: round } }
  if (blocking.length === 0) { return { final: report, history, cleared: true, lastReviews: reviews } }
  if (round === 3) { return { final: report, history, cleared: false, unresolved: blocking } }
  phase('Fix')
  report = await agent(`You are the ONE builder for the Red Hope gate "idle robots look busy" (display only) in ${REPO}. It is built uncommitted; read-only reviewers found BLOCKING problems. Fix every one (or prove a finding wrong with evidence). ${DOC_ONLY_FIX} Otherwise, RE-RUN THE FULL GATE from scratch on the final build. Do NOT redo pre-step (c): the before dylib in Saved/orchestrator/idle_bin_before/ and the before captures are round 0's; never overwrite them. Re-check (a0), (a), (a2) and (b) only. Keep this round's working files under ${WORK}/r${round + 1}/ and give every re-run a new label (idle_r${round + 1}_click1, ...).\n\nBlocking:\n${JSON.stringify(blocking, null, 1)}\n\nMinor (fix if cheap and safe):\n${JSON.stringify(reviews.flatMap(r => r.findings.filter(f => f.severity === 'minor')), null, 1)}\n\nPrevious report:\n${JSON.stringify(report, null, 1)}\n${SPEC}\n${GATE}\n${RULES}\n${RUN_NOTE}\nReturn the full structured report again (current state).`, { label: `builder:idle:fix${round + 1}`, phase: 'Fix', schema: BUILD_SCHEMA })
  if (!report) { return { error: 'fix builder died', history } }
  if (stopFor(report)) { return { final: report, history, cleared: false, needsOrchestrator: report.stop_reason } }
  if (report.director_questions && report.director_questions.length) {
    return { final: report, history, cleared: false, needsDirector: report.director_questions }
  }
}
