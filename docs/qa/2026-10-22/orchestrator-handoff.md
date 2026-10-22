# Orchestrator handoff (2026-10-22)

The main orchestrating session ran out of usage. This file lets a fresh session take over. Read all of it before you act.

## Your role
- You orchestrate. Spawn ONE builder agent at a time for code; read-only agents (design, review) may run in parallel.
- You verify every result, commit, push, and report to the director (James) in plain words with lettered questions and a recommendation.
- Log every director decision in `docs/design-decisions.md`, and in the design doc's "# DIRECTOR ANSWER" section.

## State right now
- HEAD: `df3e479` (D5). Save format v47. The battery has 50 runs. The latest baseline is `Saved/baselines/battery_post_d5`.
- **IN FLIGHT: D6a (caverns)** was being built by a builder agent of the old session. That agent stops when the old session's usage runs out, leaving its work **uncommitted** in the tree:
  - new `Source/RedHopeSim/Private/RHSimCavern.cpp`, `RHSimCavernSuite.cpp`;
  - new `Source/RedHope/.../RHCavernVisualizerSubsystem.*`;
  - plus edits to existing files.
- The spec: `docs/qa/2026-10-22/d6-caverns-design.md`. Its order of authority is DIRECTOR ANSWER > MERGED DESIGN AND REVISION > review > drafts. Build only D6a.

## FIRST: take over D6a safely
1. Check that nothing is still running:
   - `pgrep -fl "UnrealEditor|rh_battery|Build.sh" | grep -v Services` must be empty;
   - the newest file under `Source/` must be more than 10 minutes old (`ls -lt` on the `git status` files).
   If either shows activity, wait 10 minutes and re-check. Never kill a process you didn't start.
2. Spawn ONE implementer with a prompt like the one below, and verify its result yourself.

> Take over and FINISH sub-gate D6a of Red Hope ("caverns"). Another session's builder wrote part of it and stopped. Do NOT commit.
>
> Repo `/Volumes/Unreal/red_hope/red_hope`, HEAD `df3e479`; D6a's work is uncommitted. NEVER run git stash, checkout or reset.
>
> **Step 1, assess.** Read `git diff` and the new files against `docs/qa/2026-10-22/d6-caverns-design.md`. The authority is DIRECTOR ANSWER (Q-A a: floor-wide reach stays; Q-B a: 8x8x3 with the ordinary recipe, limits as code constants `RH::CavernMaxSide 8`, `CavernMaxFloors 3`, `MaxOpenCaverns 2`; Q-C a: trams carry people only) > MERGED DESIGN AND REVISION (all [RC n]) > review. Re-anchor to HEAD. Make a checklist (done / partial / missing) of every D6a item:
> - DigCavern: order, preflight, the one-seed-per-floor rule;
> - the cavern record;
> - shared `CarveLatticePoint`, which removes points from Plan;
> - `IsTunnelWork` / `IsCavernWork` replacing EVERY `X >= 2` reader, including `GetTunnelBatchesInFlight` and D5's batch-start pricing;
> - the save block (v48);
> - the visualizer;
> - the `-caverns` suite, added to `rh_battery.sh`.
>
> **Step 2,** finish and fix anything incomplete.
>
> **Step 3, the commit gate:**
> - `bash scripts/sim/rh_battery.sh 10` must end "BATTERY COMPLETE: 0 suite(s) not green". The logs must be newer than both dylibs, and no Source or docs/data file may be newer than `baseline.log`.
> - `python3 scripts/sim/diff_battery.py Saved/baselines/battery_post_d5 Saved/RHBattery`: ONLY the save-version lines plus the new `caverns.log`; timing digits are tolerated.
> - The pins must hold: "power: gen 50 W load 20 W" and "deposit Regolith_A: 40645 kg".
> - Run the spec's mutations and confirm each check fails.
> - Run `-scale` separately.
> - `RH_PLAYTEST_DISTRICTS=1 bash scripts/sim/rh_playtest.sh 110 d6a_full guide full` must be byte-identical to `Saved/RHPlaytest/d5_full.txt`.
> - `bash scripts/unreal/rh_clicktest.sh` must PASS.
> - Captures: a cavern ordered, half carved and finished; look at them.
> - `bash scripts/unreal/rh_checkpoints.sh all --force`, and confirm both load.
> - Log D6a in `docs/design-decisions.md`. New wording goes in `docs/qa/2026-10-15/gate-d-wording.md` with a fresh row prefix.
>
> **Hard rules:**
> - RedHopeSim never depends on RedHope.
> - Determinism; no new Mass fragments.
> - Build with `"/Volumes/Unreal/UE_5.8/Engine/Build/BatchFiles/Mac/Build.sh" red_hopeEditor Mac Development -project="/Volumes/Unreal/red_hope/red_hope/red_hope.uproject" -waitmutex` (-Wshadow is an error).
> - One Unreal process at a time.
> - Never echo the SecurityToken in Config/DefaultEngine.ini.
>
> **Report:** the checklist, changes, battery diff, playtest, click test, captures, and deviations.

## How to verify and commit (the orchestrator does this, every gate)
```bash
cd /Volumes/Unreal/red_hope/red_hope
stat -f "%Sm %N" -t "%m-%d %H:%M" Binaries/Mac/libUnrealEditor-RedHopeSim.dylib Saved/RHBattery/baseline.log
find Source/RedHopeSim docs/data -newer Saved/RHBattery/baseline.log -type f    # must print nothing
python3 scripts/sim/diff_battery.py Saved/baselines/battery_post_d5 Saved/RHBattery | grep -v " 0 differing"
grep -h "power: gen\|deposit Regolith_A" Saved/RHBattery/baseline.log
```

Look at one or two captures yourself (Read the PNG). Then:
- `git add` only the gate's files. Never `git add -A`, and never add `Saved/`.
- `git diff --cached | grep -ci SecurityToken` must print 0.
- Write a detailed message: the changes, every moved line with its reason, the tests, and the follow-ups. End it with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Push as Somnora. Never `gh auth switch`.

```bash
git -c credential.helper= -c credential.https://github.com.helper= -c 'credential.https://github.com.helper=!f() { test "$1" = get || exit 0; echo username=Somnora; echo "password=$(gh auth token -u Somnora)"; }; f' push origin main
```

- Verify HEAD == origin/main.
- Snapshot the baseline: `cp -R Saved/RHBattery Saved/baselines/battery_post_<gate>`.

## The queue after D6a (in order)
1. **D6a caverns:** finish, verify, commit (above).
2. **Perf fix before trams:** `-scale` at 20 floors fails three D4 budgets:
   - cold deep net: 5.6 ms against 3;
   - 2,500 `PlanDeepRoute`: 26.2 ms against 10;
   - flows only: 1.03 ms against 1.
   Five-floor digests must stay unchanged; the battery must be identical.
3. **D6b:** district shapes on a floor plus growth inside shapes (v49).
4. **D6c:** real commutes between digs (no save bump).
5. **D6d:** trams carrying people only (v50).
6. **D6e:** lift and tram power (v51). D5's scraping check is ungated from cooling; the TP2 fixture has NO cooling floor.

   Each D6 sub-gate follows the merged spec in `d6-caverns-design.md`: one builder, the full commit gate above, and a diff against the previous gate's baseline.
7. **Small UI follow-ups:** the "Fit a lift car" field on the shaft card is unreadable (pale on white); one click-test flake was seen on a loaded machine.
8. **Then step 2's last gates:** D7 atriums (they must FLAG cells open, never delete them, or D7 becomes the re-key gate), then D8, the deep view (presentation only). Each needs design, then a professor review, then a REVISION and the director's answers, then the build.
9. **Read-only jobs anytime:**
   - a D4 code review (prompt in the old chat; it reads code at `4620a94`);
   - the E1 ledger design (step 3).

## Rules that bite (learned the hard way)
- **ONE live session per conversation.** Never resume a conversation in a second window while it runs: two copies collided twice.
- **ONE agent builds or runs Unreal at a time.** Concurrent batteries corrupt `Saved/RHBattery` and `Saved/SaveGames`.
- **Never `git stash`, checkout or reset in this tree.** Other sessions' files get swept up.
- **Baselines live in `Saved/baselines/`** (gitignored). Scratchpads get wiped.
- **Every gate:** the battery stays line-identical except the named lines, the 110-sol playtest stays byte-identical, and the click test passes for any UI change.
- **Two-designer collisions:** if two drafts of one design appear, have a professor pick the base and graft (precedent: W1, D6).
- **The director's standing rules:**
  - no combat ever;
  - Hope is never spent;
  - all morale and evacuation wording is prevention-framed placeholder (Gate-D);
  - commit each gate separately with a detailed message;
  - plain-text lettered questions with a recommendation.
- **Secrets:** never echo the AndroidFileServer SecurityToken in `Config/DefaultEngine.ini`. An ElevenLabs key the director once pasted must never be stored or used.
- **Shared Lambda account:** never terminate an instance you didn't launch.
