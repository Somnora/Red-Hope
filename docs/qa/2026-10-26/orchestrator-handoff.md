# Orchestrator handoff (2026-10-26, state refreshed 2026-10-29)

This replaces `docs/qa/2026-10-25/orchestrator-handoff.md`. The rules and the verify/commit recipe in `docs/qa/2026-10-22/orchestrator-handoff.md` still hold, so read that one too. The 10-25 handoff's state and queue sections are stale.

**Where it runs.** The whole project lives on the external SSD "Unreal": the repo at `/Volumes/Unreal/red_hope/red_hope`, the Installed-Build engine at `/Volumes/Unreal/UE_5.8`, `Martians/` (16 GB of art sources, gitignored) and `Saved/baselines/`. The planned move to the other MacBook was dropped (2026-10-28): gates run on this Mac (hostname "Mac", 8 GB) whenever the drive is mounted. Check `ls /Volumes/Unreal` first.

## Your role
- You orchestrate. Run ONE builder or Unreal process at a time. Read-only design and review agents may run in parallel.
- Run each gate as a Workflow. **Ultracode is OFF since 2026-10-28:** launch a gate workflow only on James's explicit go for that gate (ask "start <gate> now? (a) yes (recommended) / (b) not yet"). The pattern is one builder, then five read-only lenses, then a fix loop of up to 4 rounds. Each gate's script is ready in `docs/qa/2026-10-26/workflows/`; see "The queue".
- Verify every result yourself:
  - battery freshness;
  - the line diff;
  - the pins;
  - the playtest cmp;
  - the click test;
  - look at the captures.

  Then commit, push, snapshot the battery and report to the director (James).
- Director questions go to James in plain words, as lettered options with a recommendation and modelled numbers.
- Log every answer twice, committed on its own:
  - in the design doc's "# DIRECTOR ANSWER" section;
  - in `docs/design-decisions.md`.
- For meaningful completed work, write a vault capsule:
  ```
  python3 /Users/jamesmcshane/Desktop/obsidian-vault/tools/memory-capsule.py --project-id red_hope --repo /Volumes/Unreal/red_hope/red_hope --summary-file <file>
  ```

## State at handoff (2026-10-29)
- **HEAD:** the commit that refreshed this handoff, on top of:
  - `f039c66`, D6e lift and tram power (save v58, frame 'RHD6');
  - `01f5299`, the T1 design (the colony record) and the director's answer (a): one gate, no save change, after E8 and the idle robots;
  - `7adc7ee`, E6 the levers (save v57, frame 'RHL6');
  - `8da8c2a`, the director's answer on idle robots (display only, after E8);
  - `26a9c8a`, the director's answer on E5's banner re-raise ((a), revisit later);
  - `9fc2c6d`, E5 the Martian currency (save v56).

  Save v58. The battery has 61 runs.
- **Battery baseline:** `Saved/baselines/battery_post_d6e` (61 logs; identical to `Saved/RHBattery` at f039c66).
- **Playtest baseline:** `Saved/baselines/ind_full_playtest.txt`, unchanged by E5, E6 and D6e.
- **Click test:** 94 steps (92 + E5's + E6's `pact.SIGN`). The deep-view scene (`RH_CLICKTEST_SCENE=deepbench`) has 9.
- **D7 is running** (launched 2026-10-29 10:30 as workflow run `wf_046785e5-f77`, args `head f039c66`, `base Saved/baselines/battery_post_d6e`, `clickSteps 94`, `batteryRuns 62`, `deepbenchSteps 9`). If this session died mid-run: do NOT start a second builder; check `/workflows`, or the run's journal under the session's `subagents/workflows/wf_046785e5-f77/`, and resume with `resumeFromRunId` from the session that launched it. Its builder may have uncommitted D7 changes in the tree: never discard them.
- **No director questions are open.** Answers logged since the last refresh: E5 banner (a) (`26a9c8a`), idle robots (a) (`8da8c2a`), T1 (a) (`01f5299`). D6e's conditional ruling 9 did not trigger: the `lifts=1` playtest passed at 200 W (the 0 W control is byte-identical to the pre-change run; the 200 W run meets quotas sooner).
- **Orchestrator rulings 13-18** (2026-10-28/29, `ORCHESTRATOR-RULINGS.md`): E6's EffSol repair is silent (13); E6's LV1 composite script (14); D6e's StepPower merges lift and tram entries into D5's sorted list instead of one unstable sort (15; later gates that add power loads merge the same way); D6e's count bound `MaxShaftsCap + MaxTramLinesCap` (16); D6e prefix A confirmed (17); E8's frame bar judged on quiet pairs, three per scene and resolution, by the median delta (18, the director's answer L (a), 2026-10-29).

## If the machine was rebooted or the drive remounted
The full machine proof (tools, build, baseline reproduction) was done when this Mac took the project back. After a reboot, check only: the drive is mounted; `git status` is clean apart from untracked scratch (or holds a running gate's builder changes); HEAD equals origin/main; `uptime` load has settled below about 5 before any click test (Spotlight and `update_dyld_sim_shared_cache` push it past 40 for the first half hour).

## The queue
Run each gate one at a time. After each gate, verify it yourself, commit, push, and snapshot `Saved/baselines/battery_post_<gate>`.

**Authority for every build:** the design doc's DIRECTOR ANSWER > `docs/qa/2026-10-26/preflight/ORCHESTRATOR-RULINGS.md` > `CROSS-CHECK.md` section 6 > `<gate>-brief.md` > the design doc's REVISION/REFRESH > the rest of the doc. The briefs' anchors are at `d842dd0`, so the builders re-anchor by symbol.

| Gate | What | Save | Frame | Script | Args to pass |
|---|---|---|---|---|---|
| E5 | the Martian currency | v56 | 'RHE5' | `e5-gate-wf.js` | DONE 2026-10-27: 9fc2c6d, pushed; baseline `Saved/baselines/battery_post_e5` (59 logs); click test now 93 steps; -scale median 0.863 ms |
| E6 | | | | | DONE 2026-10-28: 7adc7ee, pushed; baseline `battery_post_e6` (60 logs); click test 94 steps |
| D6e | lift and tram power | v58 | 'RHD6' | `d6e-gate-wf.js` | DONE 2026-10-29: f039c66, pushed; baseline `battery_post_d6e` (61 logs). (This script's `batteryRuns` was the base's own count, 60.) |
| D7 | atriums | v59 | 'RHD7' | `d7-gate-wf.js` | RUNNING (see State). `batteryRuns` here is logs in base + 1 = 62 |
| E7 | region-ready (inert) | none | none | `e7-gate-wf.js` | `head`, `base: ".../battery_post_d7"`, `clickSteps`, `deepbenchSteps`, `batteryRuns` (logs in base + 1 for regionready.log), `saveVersion` (RHSaveVersion at HEAD, 59 in queue order), optional `playtestBase` |
| E8 | the market panel | v60 (= prev + 1) | 'RHE8' | `e8-gate-wf.js` | `head`, `base: ".../battery_post_e7"`, `clickSteps`, `deepbenchSteps`, `batteryRuns` (logs in base; no new suite), `prevVersion` (59 in queue order), `e5Built: true` (a boolean, checked against HEAD), optional `playtestBase`. Frame bar: ruling 18 (quiet pairs; director answer L (a), 2026-10-29) |
| Idle robots | idle robots look busy (display only) | none | none | `idle-gate-wf.js` | after E8: `head`, `base: ".../battery_post_e8"`, `clickSteps` (E8's; the gate adds 1), `deepbenchSteps`, `batteryRuns` (logs in base; no new suite), `saveVersion` (60), optional `playtestBase`. Design `docs/qa/2026-10-28/robot-idle-design.md` (answers (a); Q-A (a), Q-B (a)), brief `robot-idle-brief.md` |
| T1 | the colony record | none | none | not written yet | after the idle robots. `docs/qa/2026-10-28/t1-colony-design.md` (director answer (a)): refresh it at E8's HEAD first (it stops on any member nobody assigned), then a brief and a gate script |

Launch each script as `Workflow({ scriptPath: "<repo>/docs/qa/2026-10-26/workflows/<gate>-gate-wf.js", args: {...} })`, passing args as a JSON object, not a string.
- Each script stops with `{ error: 'missing args' }` if a required arg is absent.
- Each stops and returns `needsDirector` if the builder raises a director question.
- A workflow that returns `cleared: true` with an empty or short `lastReviews` did not clear. Since 00c6900 every script returns `reviewersFailed` instead when a lens returns nothing (E5's first run hit a usage limit mid-review). Resume with `resumeFromRunId`; only the missing reviewers re-run.
- E6, D6e, D7, E7 and E8 were each checked once by an independent read-only reviewer against their brief and the cross-check, and fixed. Read a script before you launch it.
- E7 and E8 (written 2026-10-27) each return `needsOrchestrator` with a `stop_reason` when a precondition fails (a wrong save version, a wrong `base`, E7's S12 double run differing, E8's E5 names missing). Clear the cause, then relaunch. Their reviews added rulings 10–12 to `ORCHESTRATOR-RULINGS.md`: E8's lost-block Warning prints only when the ring held something (10); E8's MX15 asserts 'RHE8' follows its predecessor frame, never "last in the file", and a forged old file is refused with "/<new version>" (11); E7's RR0 `ice` case is `{1, 'ice', 500}` so mutation 8 can fail (12). Numbers are coerced, so `"97"` and `97` both work; E8's `e5Built` must be a real boolean.

**Wording prefixes** in `docs/qa/2026-10-15/gate-d-wording.md`: E5 Y, E6 Z, D6e A (ruling 17), D7 L, E8 C.

**Read each script's `batteryRuns` comment:** E6 and D7 want the expected count (logs in base + 1), D6e wanted the base's own count, E8 wants logs in base (no new suite).

## Follow-ups (not gates; do them when a quiet slot appears or James asks)
- **Sites:** see the sites commit message for the full list. The main items:
  - the Solar Array at about 45–60 % shows a floating teal bar. The panel's bottom rail sits at the cut while the mast-top hinge above it is not built yet. Fix: a per-model cut hold in `RefreshSites`, keeping the cut under the panel until the hinge is built;
  - the Forge interior floor strip;
  - the mast sits below the top deck on sites with 3+ courses;
  - room clutter stands on underground pads;
  - the crate lids still read close to HazYellow;
  - the pad is paler than the regolith;
  - `build.site` checks bays summed over all courses;
  - the HISM bookkeeping check only covers the empty case.
- **E5 banner (director: (a), revisit later, 2026-10-27):** a DefyIssue at tension 62 or more re-raises Earth's demand in the same tick, and that alert replaces D44 + Y10 on the one-slot banner. Revisit it with the tension numbers or in E8; options (b) queue alerts, (c) delay the re-raise a sol. Same place: Y10 says "so tension rises" but the act nets -2 (fix at the Gate-D review).
- **E6 (wording, for the Gate-D wording review; it does not block E8, which does not touch the rival card):** the rival card says "standing" where the refusal says "relation" (Z1/Z5); a partner that catches a covert op ends the pact with only a log line, and the COVERT/SABOTAGE tooltips don't warn of it (Z6); one tariff window is phrased three ways. Code minors are in the E6 commit message.
- **D6e:** SP5, the shaft card and `RH.Transit` still show nominal capacity (card "load 374 / 4000" while the label reads 200); the click test's measured label band moved 225 px in that session (351..1401 -> 126..1176) with no RedHope change behind it, so watch `section.labels` at D7; test minors in the D6e commit message.
- **Placeholders:** `docs/data/RH_Config.csv` has 154 rows marked "Placeholder, director review". After E8, scope a balance pass with modelled numbers for the director.
- **From the visual-pass design's follow-up list:**
  - shuttles are boxes;
  - convoys use engine cubes despite the rover kit.
- **Tram:**
  - deep-floor pit walls can hide the far leg of an L tunnel;
  - the underground look is accepted as polish.

## Lessons from this session
- **An account usage limit kills every agent at once.** On 2026-10-28 the other window's limit hit mid-review and all five reviewers of E6's round 2 returned nothing. Since 00c6900 that returns `reviewersFailed`; re-run just the review (a review-only script from the gate script's preamble, the builder's report passed as a file path) rather than the whole gate.
- **A control run settles cause.** D6e's 0 W control (`-RHConfig=LiftCarW=0,TramWPerCell=0`) proved the lifts playtest shift was the draw, and the same control on -scale exposed the sort defect. Check that the control ran on the final binary (`d6e_lifts_w0_rep`, not the intermediate `d6e_lifts_w0`).
- **Never SendMessage to an agent running inside a Workflow.** The message resumes the agent outside the workflow, so its result comes back to you, and the workflow's `agent()` call waits forever. To change a running builder's instructions, let it finish. Or stop the workflow, then run the remaining phases as a new workflow, passing the builder's report in.
- **Machine load breaks the click test.** With load above about 10 (Codex, an Xcode build, ffmpeg), click runs time out at 450 s and menu steps fail. On a quiet machine the same build passes 92/92 in about 150 s. Check `uptime` before counting click runs, and ask James to pause heavy jobs.
- **`r.ForceLOD` does not reach HISM/instanced meshes; `foliage.ForceLOD` does.** Use the latter to compare LODs on kit parts.
- **Frame timing on a swapping 8 GB machine is noise.** Adjacent pairs swing from -8 to +10 ms. Only quiet pairs mean anything, and a director waiver must quote them honestly.

## Rules that bite (unchanged; see the 10-22 handoff)
- **ONE live session per conversation.** Never resume a conversation in a second window while it runs.
- **ONE builder or Unreal process at a time.** Never kill a process you did not start.
- **NEVER run git stash, checkout or reset in this tree.** Never `git add -A` or add `Saved/`; add by explicit path. Back up before any destructive git operation.
- **Every gate:**
  - the battery stays line-identical except the named lines;
  - the playtest stays byte-identical unless the gate intends otherwise;
  - the click test passes three times back to back for any UI change.
- **Director rules:**
  - no combat ever;
  - Hope is never spent;
  - wellbeing wording is prevention-framed placeholder (Gate-D);
  - one commit per gate, with a detailed message;
  - questions state consequences with modelled numbers.
- **Never print the SecurityToken** in `Config/DefaultEngine.ini`. Never store or use the ElevenLabs key.
- **Before every commit,** `git diff --cached | grep -ci SecurityToken` must print 0. Commit with `-F <msgfile>`, and end each message with the `Co-Authored-By` trailer the session's attribution reminder gives (`Claude Fable 5.1 <noreply@anthropic.com>` as of 2026-10-27; earlier commits carry Opus 5.5).
- **Push as Somnora.** Never run `gh auth switch`. Then check `git rev-parse HEAD origin/main` (no `--short`).

```bash
git -c credential.helper= -c credential.https://github.com.helper= -c 'credential.https://github.com.helper=!f() { test "$1" = get || exit 0; echo username=Somnora; echo "password=$(gh auth token -u Somnora)"; }; f' push origin main
```
