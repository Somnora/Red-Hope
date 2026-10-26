# Queue pre-flight: the orchestrator's rulings (2026-10-26)

Read-only pre-flight of E5, E6, D6e, D7, E7 and E8 at HEAD `d842dd0`: one planner per gate re-anchored its spec to HEAD and wrote a build brief (`<gate>-brief.md`); a seventh checked the briefs against each other (`CROSS-CHECK.md`). Every gate came back READY WITH FIXES. These rulings settle what the briefs left to the orchestrator. None needs the director.

**Authority for each build:** the design doc's DIRECTOR ANSWER > these rulings > `CROSS-CHECK.md`'s required corrections > the gate's brief > the design doc's latest REVISION/REFRESH > the rest of the doc. Anchors in the briefs are at `d842dd0`; the construction-sites commit (visual pass item 2) will move some lines in `Source/RedHope` (re-anchor there, not in `Source/RedHopeSim`).

1. **Cross-check items 1-4 are adopted before E5 starts.**
   - (1) No gate asserts that its own frame is the last in the file. E5's CU10 and XE10 assert that 'RHE5' follows 'RHE4' (find by tag).
   - (2) E6's "unknown convoy Id dropped" repair drops silently when E4's loader already dropped the convoy, so `market.log` and `currency.log` gain no Warnings.
   - (3) E5 records why the market block was lost (lost, malformed, slot table reset, repeated region), not a single flag, plus `bCurrencyBlockLost`, so E8 prints the right text.
   - (4) D7 reads its frame at the normal tail position too (as a fallback, and so the frame is consumed), not only by the walk from the end.
2. **E5's lost-block Warning** prints only when the 'RHE5' frame held something to lose; otherwise it starts unissued silently (E5 brief, blocking 2). `market.log` stays line-identical apart from version lines.
3. **D6e and D7 save semantics: option (i)**, a framed tail block that repairs with Warnings (the house rule since v54), never a mid-file block that refuses. D7 reads its frame early, after teardown and before `RepairLoadedRobots`, with a non-moving read (D7 brief 2.10).
4. **E7 S1:** `Debug_SetEndowment` writes an override map consulted before the table index; it is range-checked, exempt from the duplicate rule and cleared by `Debug_ClearEndowments`. `Debug_InjectEndowmentRow` stays a raw row under full validation.
5. **E7 S2:** the resource check compares `Row.Resource.ToString()` with the resource rows' key strings case-sensitively; RR0 gains an `ice` case (one Warning, reads 1000). Mutation 8 is "drop the case-sensitive compare".
6. **Wording prefixes** in `docs/qa/2026-10-15/gate-d-wording.md`: E5 **Y**, E6 **Z**, D7 **L** (already reserved for it), E8 **C**.
7. **Version greps:** use `(expect 55)`-style patterns and `git grep -P` (or no `\b`); `git grep -E` ignores `\b` here.
8. **Save versions in queue order:** E5 v56, E6 v57, D6e v58, D7 v59, E7 none, E8 v60. If the order changes, each takes the next free version at build time, and the size-line values in `CROSS-CHECK.md` are recomputed.
9. **Director, conditional only:** if D6e's `lifts=1` playtest fails at the 200 W placeholder draw, stop and put lettered options to the director.
