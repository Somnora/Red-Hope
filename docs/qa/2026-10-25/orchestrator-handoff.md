# Orchestrator handoff (2026-10-25)

This replaces `docs/qa/2026-10-22/orchestrator-handoff.md`. That file's state section is stale, but its rules and its verify/commit recipe still hold, so read both.

## Your role
- You orchestrate. Run ONE builder at a time for code. Read-only design and review agents may run in parallel.
- Verify every result yourself: battery freshness, the line diff, the pins, and look at the captures. Then commit, push and report to the director (James).
- Director questions go to James in plain words, as lettered options with a recommendation.
- Log every answer in the design doc's "# DIRECTOR ANSWER" section, committed on its own.

## State at handoff
- **HEAD:** `1505941` (the step 2 fixes). Save v55. The battery has 58 runs.
- **Battery baseline:** `Saved/baselines/battery_post_s2fix`.
- **Playtest baseline:** `Saved/baselines/ind_full_playtest.txt`, the industry bot's default run: pop 122, Big Arrival at sol 81, 7 Q3 rounds.

**IN FLIGHT: the visual pass.** The old session's builder was stopped mid-work. Its work is UNCOMMITTED in the tree.
- **Spec:** `docs/qa/2026-10-25/visual-pass-design.md`, including its DIRECTOR ANSWER:
  - Q-A (a): every building's model rises inside the scaffold.
  - Q-B (a): the tram's wait colour shows on the destination boards only.
- **The three items:**
  - a tram kit model (`scripts/blender/rh_build_tram.py`) replacing the capsule;
  - a construction-site kit for every building (`scripts/blender/rh_build_sitekit.py`);
  - the deep-view floor-name ladder (P8: smaller, sharp, high-contrast, with leader lines).
- **Also:** check the RH.DeepBench over-budget finding (rebuild 10.4 / 15.4 ms against 8; TRAFFIC p95 53.6 ms against 48), which may come from ComputeLabelRoomCm.

## FIRST: take over the visual pass safely
1. Wait for quiet:
   - `pgrep -fl "UnrealEditor|rh_battery|Build.sh" | grep -v Services` is empty;
   - no file in `git status` was modified in the last 10 minutes;
   - check `df -h / /Volumes/Unreal`: at least 5 GB on /, 2 GB on the Unreal drive. The system disk has run out repeatedly; ask James to free space if it is low.
2. Spawn ONE implementer to:
   - audit `git diff` and the new files against the spec, making a done/partial/missing checklist;
   - finish the work;
   - run the gate:
     - the RedHopeSim dylib sha256 unchanged (presentation only);
     - `bash scripts/sim/rh_battery.sh 10`, then `python3 scripts/sim/diff_battery.py Saved/baselines/battery_post_s2fix Saved/RHBattery` (timing digits only);
     - the pins: "power: gen 50 W load 20 W" and "deposit Regolith_A: 40645 kg";
     - the 110-sol playtest byte-identical (`RH_PLAYTEST_DISTRICTS=1 bash scripts/sim/rh_playtest.sh 110 vp_full guide full`, then cmp against `Saved/baselines/ind_full_playtest.txt`);
     - the click test `RH_CLICKTEST_RES=1440x860 bash scripts/unreal/rh_clicktest.sh`, passing 3 times;
     - RH.DeepBench before and after;
     - before and after captures of the tram, the sites at 0%, 50% and 90%, and the floor ladder. LOOK at them: James called the old ones "way unfinished".
3. Commit with a detailed message, then push.

## The queue after the visual pass
Each item is designed, reviewed, refreshed at 31ffa6f, and answered unless noted.
1. **E5,** the Martian currency (`docs/qa/2026-10-24/e5-martian-currency-design.md`; v56). Apply its REFRESH section: after the issue, the market unit is marks, so ValueMcr is in mmk; add a `bMarketBlockLost` flag.
2. **E6,** the levers (`docs/qa/2026-10-24/e6-levers-design.md`; v57). Apply its REFRESH: PlanConvoy gains an EffSol parameter; E4's Debug_ hooks migrate into the rule list.
3. **D6e,** lift and tram power (the D6e section of `docs/qa/2026-10-22/d6-caverns-design.md`). New blocks must be RHSaveFrame frames.
4. **D7,** atriums (`docs/qa/2026-10-23/d7-atriums-design.md`). Its REFRESH notes that D8's section view must learn about open cells.
5. **E7 and E8** (`docs/qa/2026-10-25/`): independently reviewed, CLEARED WITH CONDITIONS. James has NOT answered yet. The open questions and recommendations:
   - E7 Q-A: is the depot a building now? Recommend (b), the Lander keeps the role.
   - E7 Q-B: endowment. Recommend (b), on rival production.
   - E8 Q-A: is the history saved? Recommend (b), saved.
   - E8 Q-C: does MARKET join the accordion? Recommend (a), it joins.

   Each also needs a REVISION written from its review before building.
6. **Also open:**
   - the life-support floor counting arriving crew (recommend (a), leave it; James hasn't answered);
   - the follow-up visuals list in visual-pass-design.md (shuttles are boxes; convoys use engine cubes despite the rover kit).

**Every new save block** goes in an RHSaveFrame frame at the end of the file. These size lines move with each new frame: CV8, DP11, EC1 ×2, DC9, MX1, MX9.

## Rules that bite (unchanged; see the 10-22 handoff)
- **ONE live session per conversation.** Never resume a conversation in a second window while it runs.
- **ONE builder or Unreal process at a time.** The machine has 8 GB; James's hand-play needs both windows idle.
- **NEVER run git stash, checkout or reset in this tree.** Commit only your gate's files, by explicit path.
- **Baselines live in `Saved/baselines/`.**
- **Every gate:** the battery stays line-identical except the named lines; the playtest stays byte-identical unless intended; the click test passes for any UI change.
- **Director rules:**
  - no combat ever;
  - Hope is never spent;
  - wellbeing wording is prevention-framed placeholder (Gate-D);
  - one commit per gate, with a detailed message;
  - questions state consequences with modelled numbers.
- **Push as Somnora:**

```bash
git -c credential.helper= -c credential.https://github.com.helper= -c 'credential.https://github.com.helper=!f() { test "$1" = get || exit 0; echo username=Somnora; echo "password=$(gh auth token -u Somnora)"; }; f' push origin main
```

  Check `git diff --cached | grep -ci SecurityToken` prints 0 before every commit. End each message with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
