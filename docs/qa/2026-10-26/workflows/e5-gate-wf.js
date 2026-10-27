export const meta = {
  name: 'gate-e5-currency',
  description: 'Red Hope gate E5 (the Martian currency, save v56): one builder builds and gates it from the pre-flight brief; read-only reviewers audit; fix loop',
  phases: [
    { title: 'Build', detail: 'one builder: E5 sim code, RHE5 frame, -currency suite, mutations, -scale, minimal UI, full gate' },
    { title: 'Review', detail: 'read-only lenses: spec, code, tests, gate evidence, presentation/wording' },
    { title: 'Fix', detail: 'builder fixes blocking findings and re-gates' },
  ],
}

const REPO = '/Volumes/Unreal/red_hope/red_hope'
const HEAD = args.head
const BASE = args.base
const CLICK_STEPS = args.clickSteps
const PF = 'docs/qa/2026-10-26/preflight'

const RULES = `
HARD RULES (this tree is shared; breaking these has lost work before):
- NEVER run git stash, checkout, reset, add, commit or push. The orchestrator commits. Do not revert other edits.
- ONE Unreal process at a time (8 GB machine, heavily swapping): never run two of UnrealEditor / UnrealEditor-Cmd / Build.sh / rh_battery / rh_clicktest / rh_playtest / rh_checkpoints / an Unreal commandlet or DataTable sync concurrently; wait for each to exit before the next. Never kill a process you did not start.
- Build: "/Volumes/Unreal/UE_5.8/Engine/Build/BatchFiles/Mac/Build.sh" red_hopeEditor Mac Development -project="${REPO}/red_hope.uproject" -waitmutex   (-Wshadow is an error). Before the gate, also compile once with -DisableAdaptiveUnity so the unity build of the changed files is proven, then rebuild normally for the final binary.
- RedHopeSim never depends on RedHope. Determinism: integer money math only, no float/double in the rate, conversion or desk. No new Mass fragments.
- Mutations (CU13) are built, run and RESTORED by sha256 of the source file; the final binary is built from the restored source BEFORE the gate, so every gate log is newer than the final dylibs. Keep backups of mutated files and of before/after binaries in your scratchpad.
- zsh does not word-split $VARS (list paths explicitly). git grep -E ignores \\b on this machine: use git grep -P.
- Never print the SecurityToken in Config/DefaultEngine.ini. Do not spawn subagents.
- Director rules: no combat ever; Hope is never spent (no E5 path reads or writes Hope, population, growth or Influence); all new wording is PLACEHOLDER in gate-d-wording.md, prevention-framed where it touches wellbeing.
`

const SPEC = `
SPEC AND AUTHORITY (highest first):
1. docs/qa/2026-10-24/e5-martian-currency-design.md "# DIRECTOR ANSWER (2026-10-24)": Q-A (a), Q-B (a) (the Comply loophole is ACCEPTED: add no rule), Q-C (a).
2. ${PF}/ORCHESTRATOR-RULINGS.md (all of it; for E5: rulings 1, 2, 6 (wording prefix Y), 7, 8 (v56)).
3. ${PF}/CROSS-CHECK.md section 6 "Required brief corrections" items that name E5: 1 (CU10 asserts 'RHE5' FOLLOWS 'RHE4' with payload == GetCurrencyBlockBytes(), never "last frame"), 2 (a mark-order entry whose convoy E4's loader already dropped is dropped SILENTLY; warn only when the convoy loaded but disagrees), 3 (a market-block loss REASON enum None/Lost/Malformed/SlotsReset/RegionsRepeated plus transient bCurrencyBlockLost), 5 (replace only the literal "(expect 55)"; 27 version lines in 15 logs; dayshift/luxury/rooms do NOT move; 14 Ver == 55 sites), 8 (pass const FRHCommand& into ResolveDilemma and ResolvePacify, mandatory; compute EffSol in BookConvoy AFTER the M:1051 catch-up), 10 (declare GetCurrencyIssueSol, GetCurrencyOpenSol, IsMarketInMarks, GetRateInputsBp, GetDeskBooked, GetDeskOrders, GetMarkOrder under these names), 15 (new suite prints whole-file sizes only in the masked "(N bytes" form, or not at all). Also section 5's RollMarket per-pass order.
4. ${PF}/E5-brief.md (the build brief: read ALL of it; anchors are at d842dd0 and the visual pass moved Source/RedHope lines, so re-anchor there by symbol; RedHopeSim anchors should still hold, verify).
5. The design doc's REFRESH, re-review, REVISION and body, as the brief orders them.
Where the brief says UNCERTAIN, take its stated default unless a higher layer settles it; list every such choice under deviations.
`

const GATE = `
THE GATE (run all of it after the final build from restored, final source; record exact numbers and log paths):
1. bash scripts/sim/rh_battery.sh 10 -> "BATTERY COMPLETE: 0 suite(s) not green" and 59 runs; logs newer than both dylibs; nothing in Source/RedHopeSim or docs/data newer than Saved/RHBattery/baseline.log.
2. python3 scripts/sim/diff_battery.py ${BASE} Saved/RHBattery: ONLY these may differ: (a) the save-version lines (recount in ${BASE} first with grep -P; expected 27 lines in 15 logs, 55 -> 56; the 47 "LoadColony v55:" Warnings in market.log and the Hope "expect 55.00" / dayshift "expect 5520" lines must NOT move); (b) the unmasked whole-file size lines CV8 x2, DP11 x2, EC1 x2, DC9, each by the SAME delta = the default 'RHE5' frame (pre-flight computed 258 bytes; if any line moves by a different delta, investigate before going on); (c) the new currency.log; (d) timing digits. List every differing line with its reason. -solidarity, -ending, -earth, -preemptive, -covert and -espionage logs must be line-identical: diff them by name.
3. Pins in baseline.log: "power: gen 50 W load 20 W" and "deposit Regolith_A: 40645 kg".
4. The -currency suite green with every CU check printed; CU13: each of the 14 mutations built, run, observed to FAIL its named check (quote the failing line), and restored by sha256 (state the hashes).
5. -scale run separately as E4 did: re-measure E4's RollMarket line on the pre-change binary first, then the E5 line (8 injected members, currency issued, N = 5,000) within that + 0.5 ms; E4's <= 2 ms check still passes; digests identical.
6. RH_PLAYTEST_DISTRICTS=1 bash scripts/sim/rh_playtest.sh 110 e5_full guide full, then cmp against Saved/baselines/ind_full_playtest.txt -> byte-identical.
7. RH_CLICKTEST_RES=1440x860 bash scripts/unreal/rh_clicktest.sh -> PASS three runs back to back, every step (HEAD has ${CLICK_STEPS} steps; say if you add the optional E5 steps). Under swap the default 240 s timeout can be too short; earlier builders raised it to 450 s for counted runs - say how. Also RH_CLICKTEST_SCENE=deepbench at 1440x860 must pass 9/9. Disclose every failed or timed-out run.
8. bash scripts/unreal/rh_checkpoints.sh all --force, and load both re-cut checkpoints (v56 refuses the old ones).
9. Captures at 1440x860 (Saved/RHCapture/e5/): the Dilemma card with its three buttons (the third greyed with its reason when it cannot issue); EARTH & NEIGHBORS with both balances, the rate line and the desk, in an issued colony; a rival card reading "Bloc member". LOOK at every capture with the Read tool: text readable, nothing clipped or overlapping.
10. Docs: the docs/design-decisions.md E5 entry (what it settles, every deviation and UNCERTAIN default taken) and the "Rows added by E5" block in docs/qa/2026-10-15/gate-d-wording.md with prefix Y, every new refusal, log line, button and tooltip marked PLACEHOLDER; the mark's name is a placeholder for the director.
`

const BUILD_PROMPT = `You are the ONE builder for Red Hope gate E5, "the Martian currency" (the mark), save v56, frame 'RHE5'. Repo: ${REPO} (UE 5.8). HEAD ${HEAD} (the visual pass is committed; the tree should be clean apart from untracked scratch).
${SPEC}
STEPS: (1) read the brief, the rulings, the cross-check and the design doc's DIRECTOR ANSWER and REFRESH; re-anchor every reference at HEAD and note any that moved; (2) measure E4's -scale RollMarket line on the current binary and keep a copy of both current dylibs; (3) build E5 in the brief's order: RHSaveFrame tag, state and config (CSV + DT sync with scripts/unreal/rh_sync_datatables.py, whole CSVs), the political record and journal, issuing (both acts), membership, the rate, settlement and the desk, the withdrawal, the 'RHE5' frame and ReconcileReservations (moving E4's clamp and recompute without changing a market.log line), the version bump in the other suites, XE10 and MX1's fixes; (4) the -currency suite CU0-CU12 and the battery entry; (5) the mutations CU13 and -scale; (6) the minimal presentation and cheats; (7) the full gate; (8) the docs.
Match the codebase's style (comment density, naming, idioms; see RHSimMarket.cpp and RHSimMarketSuite.cpp).
${GATE}
${RULES}
Return the structured report; be faithful about anything failed, skipped or uncertain. List every file changed or created (including .uasset DataTables and checkpoints).`

const BUILD_SCHEMA = {
  type: 'object',
  properties: {
    complete: { type: 'boolean' },
    checklist: { type: 'array', items: { type: 'object', properties: { item: { type: 'string' }, status: { type: 'string', enum: ['done', 'partial', 'missing'] }, note: { type: 'string' } }, required: ['item', 'status'] } },
    files_changed: { type: 'array', items: { type: 'string' } },
    summary_of_changes: { type: 'string' },
    reanchors: { type: 'string', description: 'brief anchors that moved at HEAD, old -> new' },
    gate: {
      type: 'object',
      properties: {
        battery_result: { type: 'string' }, battery_diff: { type: 'string', description: 'every differing line with its reason' },
        version_lines: { type: 'string' }, size_line_delta: { type: 'string' }, identical_logs: { type: 'string' }, pins: { type: 'string' },
        currency_suite: { type: 'string' }, mutations: { type: 'array', items: { type: 'string' } }, scale: { type: 'string' },
        playtest_cmp: { type: 'string' }, clicktest_runs: { type: 'array', items: { type: 'string' } }, checkpoints: { type: 'string' },
        log_paths: { type: 'array', items: { type: 'string' } },
      },
      required: ['battery_result', 'battery_diff', 'version_lines', 'size_line_delta', 'identical_logs', 'pins', 'currency_suite', 'mutations', 'scale', 'playtest_cmp', 'clicktest_runs', 'checkpoints'],
    },
    captures: { type: 'array', items: { type: 'string' } },
    own_visual_judgement: { type: 'string' },
    deviations: { type: 'array', items: { type: 'string' } },
    open_issues: { type: 'array', items: { type: 'string' } },
    director_questions: { type: 'array', items: { type: 'string' }, description: 'only if something genuinely needs the director; plain lettered options with a recommendation and modelled numbers' },
  },
  required: ['complete', 'checklist', 'files_changed', 'summary_of_changes', 'reanchors', 'gate', 'captures', 'own_visual_judgement', 'deviations', 'open_issues', 'director_questions'],
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

const READONLY = `You are a READ-ONLY reviewer. Do NOT edit any file in ${REPO}. Do NOT build; do NOT run UnrealEditor, UnrealEditor-Cmd, Build.sh or any rh_*.sh script. NEVER run git stash/checkout/reset/add/commit. You may read files, run git diff / git status / git show / git grep -P, python3 scripts/sim/diff_battery.py, diff, cmp, shasum, stat, grep over Saved/ logs, and Read PNG captures. Do not spawn subagents. "blocking" = must be fixed before commit (a spec miss, a bug, a broken or unproven gate, a test that cannot fail, a moved line nobody named). Cite file:line.`

const LENSES = [
  { key: 'spec', prompt: `Lens: SPEC CONFORMANCE. Compare the uncommitted work (git -C ${REPO} diff and new files in git status) item by item against ${PF}/E5-brief.md sections 1, 3, 4, 5 and 8, with the authority order: the design doc's DIRECTOR ANSWER > ${PF}/ORCHESTRATOR-RULINGS.md > ${PF}/CROSS-CHECK.md section 6 items for E5 (1, 2, 3, 5, 8, 10, 15) > the brief. Every binding-design point 1-13, every trap 1-12, every save field in order, every "deviation the builder must NOT take": present and correct, or a finding. Check the accessor names later gates need (cross-check section 4) exist with those exact names and signatures.` },
  { key: 'code', prompt: `Lens: CODE CORRECTNESS of the uncommitted C++ (git -C ${REPO} diff -- Source and new files). Integer math: every signed division through MarketDivTrunc, ceil for charges and floor for payments, one floor per desk payout and per redemption, int64 overflow bounds, no float/double on the money path. The RollMarket per-pass order and the skipped-sol walk (the record outside the bLive gate; at most 8 rate steps; the 12-sol jump totals 12 drifts). Reservation invariants in both purses on every path (booking, clearing, vanished rival row in StepTrade, withdrawal, load). ReconcileReservations moving E4's clamp and recompute without changing E4's texts, conditions or line order. ReadCurrencyBlock repairs, the loss reason, NextCommandId raised past loaded desk and journal Ids, the silent drop of mark orders whose convoy E4 dropped. GetRequisitionMultiplier's unissued path unchanged in operation order. Nothing reads live IdentityAxis/EarthTension in the rate, admissions or settlement; nothing touches Hope, population, growth or Influence. -Wshadow. RedHopeSim does not include RedHope.` },
  { key: 'tests', prompt: `Lens: TEST STRENGTH. Read the new RHSimCurrencySuite.cpp and Saved/RHBattery/currency.log. For each of CU0-CU12: does it check what the brief's table says, with exact expected values where the spec gives them, and can it actually fail (no tautologies, no reference computed by the code under test)? CU8: the full paired schedule, the three trade rules, int64 equality at every threshold 16-36, identical redeemed credits. CU10: 'RHE5' asserted to FOLLOW 'RHE4' (not to end the file), every section-5 repair on a resealed file, the lost-'RHE4' and lost-'RHE5' cases, NextCommandId. CU13: find the builder's evidence for all 14 mutations (each failed its named check, each restored by sha256) and check the evidence is real (logs or quoted output), not assertion. Suite prints no unmasked whole-file size line.` },
  { key: 'gate', prompt: `Lens: GATE EVIDENCE AUDIT, from files on disk, not the builder's word: Saved/RHBattery logs newer than both dylibs (stat), nothing in Source/RedHopeSim or docs/data newer than baseline.log, nothing in Source newer than the dylibs; BATTERY COMPLETE with 0 not green and 59 runs; run python3 scripts/sim/diff_battery.py ${BASE} Saved/RHBattery yourself and account for EVERY differing line: version lines 55->56 only (27 in 15 logs; "LoadColony v55:" and the Hope "expect 55.00" / dayshift "expect 5520" lines unmoved), the size lines CV8 x2 / DP11 x2 / EC1 x2 / DC9 all moved by one identical delta, currency.log new, timing digits; diff solidarity/ending/earth/preemptive/covert/espionage logs by name against ${BASE} (must be identical); pins present; Saved/RHPlaytest/e5_full.txt cmp-identical to Saved/baselines/ind_full_playtest.txt and newer than the dylibs; three main click-test logs PASS with consistent step counts (HEAD ${CLICK_STEPS}) newer than the RedHope dylib; deepbench passes; checkpoints re-cut and loaded; -scale numbers within budget.` },
  { key: 'presentation', prompt: `Lens: PRESENTATION AND WORDING. Review the Source/RedHope changes (RHCommandDeckPanels.cpp Dilemma card third button and its greying/tooltip, the EARTH & NEIGHBORS currency controls, the rival card's bloc line, RHOrderWords.h, RHCheats.cpp RH.Currency and RH.Solidarity defyissue) and Read every capture under ${REPO}/Saved/RHCapture/e5/: readable, unclipped, consistent with the existing panels, greyed states carry a reason. Check docs/qa/2026-10-15/gate-d-wording.md gains a "Rows added by E5" block with prefix Y listing every new user-facing string as PLACEHOLDER (compare against the strings actually in the diff: none missing), no combat or morale/evacuation wording, the mark's name flagged as a director placeholder; docs/design-decisions.md has an E5 entry that lists every deviation and UNCERTAIN default taken.` },
]

function reviewPrompt(lens, report) {
  return `${READONLY}\n\nReviewing Red Hope gate E5 (the Martian currency, v56), built uncommitted in ${REPO} on HEAD ${HEAD}.\n\n${lens.prompt}\n\nThe builder's report (claims to verify, not trust):\n${JSON.stringify(report, null, 1)}`
}

phase('Build')
let report = await agent(BUILD_PROMPT, { label: 'builder:e5', phase: 'Build', schema: BUILD_SCHEMA })
if (!report) { return { error: 'builder died' } }
log(`builder done: complete=${report.complete}`)
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
  if (blocking.length === 0) { return { final: report, history, cleared: true, lastReviews: reviews } }
  if (round === 3) { return { final: report, history, cleared: false, unresolved: blocking } }
  phase('Fix')
  report = await agent(`You are the ONE builder for Red Hope gate E5 (the Martian currency, v56) in ${REPO}. It is built uncommitted; read-only reviewers found BLOCKING problems. The pre-change steps cannot be re-run on a binary that already has E5 in it: reuse round 0's pre-E5 -scale figure (E4's RollMarket line on the pre-change binary) exactly as recorded, and NEVER reinstall the saved pre-change dylibs or any old binary. Fix every one (or prove a finding wrong with evidence), then RE-RUN THE FULL GATE from scratch on the final build (including any mutation that covers changed code), retake the captures and look at them.\n\nBlocking:\n${JSON.stringify(blocking, null, 1)}\n\nMinor (fix if cheap and safe):\n${JSON.stringify(reviews.flatMap(r => r.findings.filter(f => f.severity === 'minor')), null, 1)}\n\nPrevious report:\n${JSON.stringify(report, null, 1)}\n${SPEC}\n${GATE}\n${RULES}\nReturn the full structured report again (current state).`, { label: `builder:e5:fix${round + 1}`, phase: 'Fix', schema: BUILD_SCHEMA })
  if (!report) { return { error: 'fix builder died', history } }
  if (report.director_questions && report.director_questions.length) {
    return { final: report, history, cleared: false, needsDirector: report.director_questions }
  }
}
