# Orchestrator handoff (2026-10-26)

This replaces `docs/qa/2026-10-25/orchestrator-handoff.md`. The rules and the verify/commit recipe in `docs/qa/2026-10-22/orchestrator-handoff.md` still hold, so read that one too. The 10-25 handoff's state and queue sections are stale.

**The project is moving to James's other MacBook.** The whole project lives on the external SSD "Unreal" and moves with it, with every path unchanged:
- the repo at `/Volumes/Unreal/red_hope/red_hope`;
- the Installed-Build engine at `/Volumes/Unreal/UE_5.8`;
- `Martians/` (16 GB of art sources, gitignored);
- `Saved/baselines/`.

## Your role
- You orchestrate. Run ONE builder or Unreal process at a time. Read-only design and review agents may run in parallel.
- Ultracode is on: run each gate as a Workflow. The pattern is one builder, then five read-only lenses, then a fix loop of up to 4 rounds. Each gate's script is ready in `docs/qa/2026-10-26/workflows/`; see "The queue".
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

## State at handoff
- **HEAD:** the commit that added this handoff, on top of:
  - `48c79db`, the director answer that the floating Solar bar is a known issue;
  - `76a9b4e`, the construction sites (visual pass item 2);
  - `52bf622`, the director answers;
  - `81ab133`, the queue pre-flight briefs;
  - `f922b3d`, the tram.

  Save v55. The battery has 58 runs.
- **Battery baseline:** `Saved/baselines/battery_post_sites`.
- **Playtest baseline:** `Saved/baselines/ind_full_playtest.txt`, unchanged.
- **Click test:** 92 steps. The deep-view scene (`RH_CLICKTEST_SCENE=deepbench`) has 9.
- **The visual pass is done:**
  - item 1, the tram: `f922b3d`;
  - P8c, the floor ladder: `870ba2c`;
  - item 2, the sites: `76a9b4e`.
- **No director questions are open.** Every answer is logged, in `52bf622` and `48c79db`:
  - the tram busy colour stays orange;
  - the tram follow-ups 2a and 3a;
  - the life-support launch floor stays as it is;
  - the sites frame budget is accepted on the quiet-pair median;
  - the floating Solar bar is a known issue (`48c79db`).

  The one remaining question is conditional: D6e's ruling 9, if its `lifts=1` playtest fails at the 200 W placeholder draw.
- **One orchestrator choice to confirm with James when convenient:** D6e's developer-facing wording rows use gate-d prefix **A**. No ruling reserved a prefix for D6e, and `CROSS-CHECK.md` lists A as free. The D6e script makes the builder confirm A is still free first.

## FIRST, on the new MacBook: prove the machine before any gate
1. **Tools.** Report anything missing; do not work around it.
   - `xcodebuild -version` must report Xcode 27; the old Mac had 27.0 (27A266a).
   - Blender 5.2 at `/Applications/Blender.app`, with `blender` on PATH.
   - `python3`.
   - `gh auth status` must show Somnora logged in. Check by status only; never print a token.
   - `node`, used only to syntax-check workflow scripts.
2. **The repo context import.** The repo's `CLAUDE.md` imports `/Users/jamesmcshane/Desktop/obsidian-vault/the-eye/Projects/red_hope/claude-context.md`. If the vault isn't at that path on this Mac, say so; the session then runs without the project context note.
3. **Disk and memory.**
   - `df -h / /Volumes/Unreal` must show at least 5 GB on / and 2 GB on the drive.
   - Note the RAM (`sysctl hw.memsize`). The old Mac had 8 GB, and gates there took 3–7 h because of swap.
4. **Git.** `git -C /Volumes/Unreal/red_hope/red_hope status` must be clean apart from untracked scratch. Then `git fetch`; HEAD must equal origin/main.
5. **Build the editor once.** Expect a long first build and shader compile, because the Epic local data cache stayed on the old Mac.
   ```
   "/Volumes/Unreal/UE_5.8/Engine/Build/BatchFiles/Mac/Build.sh" red_hopeEditor Mac Development -project="/Volumes/Unreal/red_hope/red_hope/red_hope.uproject" -waitmutex
   ```
6. **Reproduce the baseline before building anything.** If anything else differs, stop and tell James.
   - `bash scripts/sim/rh_battery.sh 10`, then `python3 scripts/sim/diff_battery.py Saved/baselines/battery_post_sites Saved/RHBattery | grep -v " 0 differing"`. Only timing digits may differ.
   - The pins: "power: gen 50 W load 20 W" and "deposit Regolith_A: 40645 kg".
   - `RH_PLAYTEST_DISTRICTS=1 bash scripts/sim/rh_playtest.sh 110 newmac_full guide full`, then `cmp Saved/RHPlaytest/newmac_full.txt Saved/baselines/ind_full_playtest.txt`.
   - One `RH_CLICKTEST_RES=1440x860 bash scripts/unreal/rh_clicktest.sh` at 92/92, and the deepbench scene at 9/9.
7. **Follow-up owed to the director: re-measure the sites frame budget here.** Run adjacent before/after pairs with `Saved/RHCapture/sites/fs_pairs.sh` (save se_27, `RH.FrameStats 15`, 5 surface sites). The script copies binaries into `Binaries/Mac`. It takes:
   - `RH_BIN_BEFORE=Saved/orchestrator/sites_bin_before`, the pre-sites RedHope dylib (sha256 `6d29620d…`), saved there because it can't be rebuilt without a checkout;
   - `RH_BIN_AFTER`, a folder holding a copy of this Mac's HEAD build of `libUnrealEditor-RedHope.dylib`. Make the copy first.

   When done, restore the HEAD dylib and confirm its hash. Report each pair against the 1.0 ms budget. This is a check only, not a gate. If the old-Mac before dylib won't load against this Mac's build, say so and skip the check.

## The queue
Run each gate one at a time. After each gate, verify it yourself, commit, push, and snapshot `Saved/baselines/battery_post_<gate>`.

**Authority for every build:** the design doc's DIRECTOR ANSWER > `docs/qa/2026-10-26/preflight/ORCHESTRATOR-RULINGS.md` > `CROSS-CHECK.md` section 6 > `<gate>-brief.md` > the design doc's REVISION/REFRESH > the rest of the doc. The briefs' anchors are at `d842dd0`, so the builders re-anchor by symbol.

| Gate | What | Save | Frame | Script | Args to pass |
|---|---|---|---|---|---|
| E5 | the Martian currency | v56 | 'RHE5' | `e5-gate-wf.js` | DONE 2026-10-27: 9fc2c6d, pushed; baseline `Saved/baselines/battery_post_e5` (59 logs); click test now 93 steps; -scale median 0.863 ms |
| E6 | the levers | v57 | 'RHL6' | `e6-gate-wf.js` | `head`, `base: "Saved/baselines/battery_post_e5"`, `clickSteps: 93`, `batteryRuns: 60`, `e5ScaleMs: 0.863` (E6 does not coerce: pass real numbers) |
| D6e | lift and tram power | v58 | 'RHD6' | `d6e-gate-wf.js` | `head`, `base: ".../battery_post_e6"`, `clickSteps`, `batteryRuns`, optional `playtestBase` |
| D7 | atriums | v59 | 'RHD7' | `d7-gate-wf.js` | `head`, `base: ".../battery_post_d6e"`, `clickSteps`, `batteryRuns`, `deepbenchSteps`, optional `playtestBaseline` |
| E7 | region-ready (inert) | none | none | `e7-gate-wf.js` | `head`, `base: ".../battery_post_d7"`, `clickSteps`, `deepbenchSteps`, `batteryRuns` (logs in base + 1 for regionready.log), `saveVersion` (RHSaveVersion at HEAD, 59 in queue order), optional `playtestBase` |
| E8 | the market panel | v60 (= prev + 1) | 'RHE8' | `e8-gate-wf.js` | `head`, `base: ".../battery_post_e7"`, `clickSteps`, `deepbenchSteps`, `batteryRuns` (logs in base; no new suite), `prevVersion` (59 in queue order), `e5Built: true` (a boolean, checked against HEAD), optional `playtestBase` |

Launch each script as `Workflow({ scriptPath: "<repo>/docs/qa/2026-10-26/workflows/<gate>-gate-wf.js", args: {...} })`, passing args as a JSON object, not a string.
- Each script stops with `{ error: 'missing args' }` if a required arg is absent.
- Each stops and returns `needsDirector` if the builder raises a director question.
- A workflow that returns `cleared: true` with an empty or short `lastReviews` did not clear. Since 00c6900 every script returns `reviewersFailed` instead when a lens returns nothing (E5's first run hit a usage limit mid-review). Resume with `resumeFromRunId`; only the missing reviewers re-run.
- E6, D6e, D7, E7 and E8 were each checked once by an independent read-only reviewer against their brief and the cross-check, and fixed. Read a script before you launch it.
- E7 and E8 (written 2026-10-27) each return `needsOrchestrator` with a `stop_reason` when a precondition fails (a wrong save version, a wrong `base`, E7's S12 double run differing, E8's E5 names missing). Clear the cause, then relaunch. Their reviews added rulings 10–12 to `ORCHESTRATOR-RULINGS.md`: E8's lost-block Warning prints only when the ring held something (10); E8's MX15 asserts 'RHE8' follows its predecessor frame, never "last in the file", and a forged old file is refused with "/<new version>" (11); E7's RR0 `ice` case is `{1, 'ice', 500}` so mutation 8 can fail (12). Numbers are coerced, so `"97"` and `97` both work; E8's `e5Built` must be a real boolean.

**Wording prefixes** in `docs/qa/2026-10-15/gate-d-wording.md`: E5 Y, E6 Z, D6e A (orchestrator choice), D7 L, E8 C.

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
- **From the visual-pass design's follow-up list:**
  - shuttles are boxes;
  - convoys use engine cubes despite the rover kit.
- **Tram:**
  - deep-floor pit walls can hide the far leg of an L tunnel;
  - the underground look is accepted as polish.

## Lessons from this session
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
