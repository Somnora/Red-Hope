# The Red Hope — Project Status

**Dated 2026-07-15.** Consolidation pass. This is the working source-of-truth
snapshot: what the game IS, what is built, what this session changed, and what is
owed. Ordered by the three-phase arc from the design brief
(`docs/the-red-hope-design-brief.md`).

Verification bar for every claim below: the full headless self-test battery
(**23 suites + the 10-sol baseline, all green today**) plus the byte-identical
regression pins (`power: gen 50 W load 20 W`, `deposit Regolith_A: 40645 kg`).
Reproduce:
```bash
UnrealEditor-Cmd red_hope.uproject -run=RHSim -sols=10 -<suite> -unattended -nosound -stdout
# suites: crew habitat vault borer rooms garden luxury hopedrive greenhouse water
#         growth discovery trade earth solidarity covert espionage preemptive
#         crisis alive ladder ending tiers   (+ no-switch = the baseline)
```

---

## 1. The game in one paragraph

A Mars colony sim across three phases — **Automation → Habitation →
Sovereignty**. Robots bootstrap an empty region (dig, refine, build, meet the
CEO's supply quotas); humans arrive with needs and morale; the colony grows into
a settlement that must choose what kind of Mars it becomes — Earth's corporate
jewel, an independent federation, or something colder. **Hope** is the central
derived index that couples it all together: rooms, comforts, and water feed
Hope; Hope drives work tempo, population growth, and research; research and
milestones feed Hope back. Two political axes with teeth — **Identity**
(Earth ↔ Mars) and **Human Nature** (Evolved ↔ Destructive) — read toward four
endings.

---

## 2. The mechanics, phase by phase (all built + headless-verified)

### Phase 1 — Automation (M0 + M1)
- **Power & territory**, deterministic Mass-entity robot fleet on a fixed sim
  sub-step (StateTree brains, `ai.mass.DynamicSTProcessorsEnabled=0`).
- **Production spine**: dig → haul → refine (Forge/Electrolyzer/WaterPlant/ISRU)
  → build. Hybrid logistics (solids hauled, fluids pooled).
- **The CEO loop**: meet a quota → earn a supply-ship award → **hand-design the
  manifest** (the brief's signature logistics puzzle) → ship transits → cargo
  lands with mechanical effects. Save/load exact-equivalence; 60× era-mode
  aggregate integrator with proven agent-vs-era parity.
- **Fleet reality**: wear/degrade/halt + repair, survey (hidden deposits),
  one-umbilical pad queue, storms & solar flares (dust derates solar, flares tax
  exposed robots), radiation curve (shielded underground).
- **The vault**: bore a shaft, carve floors, shield → oxygenate → circulate →
  **rate a floor LIVABLE**. Underground construction, the trunk hauls across
  levels, sliced-camera pit view.

### Phase 2 — Habitation (M2) — "The Crew Arrives"
- **Colonists as agents**: crew pods disembark only into certified housing;
  breathe the floor's O2, eat pooled Food, drink Water; unmet needs → a
  prevention-framed evacuation countdown (abstract, never graphic).
- **Rooms & adjacency**: designate carved cells (Living/Lab/Workstation/Dining/
  Cooking/Garden/Greenhouse/Hallway…); an Odor emitter beside a refuser offends;
  a Hallway partition + floor filtration cures it. Deterministic **job seats**.
- **Hope index** (pure derived read): base + housing + rooms + jobs + vault +
  comforts − adjacency − unsupported, low-passed into **HopeSmoothed** (τ=3
  sols, the agent/era parity property), five hysteresis bands.
- **Hope drives the colony**: **work tempo** 0.60–1.25 (floored at 0.60 —
  slow, never dead: the death-spiral guardrail), **garden yield**, and
  **population growth** (Hope≥75 + free bed + food buffer → the first
  Martian-born child).
- **The garden & water loop**: soil+seeds auto-plant; grow-lit Garden (battery
  cost) vs solar-riding Greenhouse (glass); greywater recycling degrades
  **potability** (a scalar) → drilling/importing ice is what keeps a colony
  flourishing enough to grow. **Discoveries**: staffed Labs uncover an authored
  sequence ending in **LIFE ON MARS** (permanent Hope milestones).
- **The alive pass**: per-colonist **skills** (mastery ramps output 1×→2×),
  crew-life **moments** (abstracted, never violent), the **first Martian
  harvest** celebration, a 1600-name diverse roster, and pickup-and-play
  onboarding (tutorial cards, milestone/first-stock toasts).

### Phase 3 — Sovereignty (M3 + M4)
- **Neighbors & trade** (M3-A): rover convoys to rival colonies, all costs
  committed at departure, transit freezes in dust storms; Zarya's ice restores
  your water potability — **trade is an alternative to drilling your own ice**
  (wired into the M2 water loop, not a bolt-on).
- **Earth's Shadow** (M3-B): tension drifts → demands; a **requisition
  multiplier** scales your CEO award by standing (Identity axis + tension).
- **The Solidarity Dilemma** (M3-C): Comply (sever routes, national pride,
  morale grief) vs Defy (Martian pride, warm rivals, requisitions stay slashed,
  escalation) — the brief's signature choice, cutting something real because the
  trade dependency was built first.
- **Espionage economy** (M4-A/B): covert requisition (seeded-hash detection
  check, content-stable), launder, sabotage-blackout — re-skinned from the
  director's combat spec to on-brief embargo/blackout, **no combat**.
- **Earth's pre-emptive pressure** (M4-C): Influence currency, embargo (turns
  your closest friend against you), Pacify, permanent defection.
- **The finale** (M4-D): alignment-gated **dynamic crises** (Destructive→
  Malfunction work-slow, Evolved→Environmental water-drain; units inert, never
  feral), and the **endings framework** reading both axes → Corporate Jewel /
  Independent Mars Federation / Martian Cold War / Abandonment-Collapse.

---

## 3. What this session (2026-07-15) changed

**Everything below is committed on branch `graphics-pass-6-textured-models` and
verified against the 24-green battery.**

1. **Committed the uncommitted animation-phase work** (`29bb31b`, 646 files):
   rigged skeletal walkers for crew + robots (Walk/Idle + per-job clips), the
   20-face roster, the elevator-as-a-place (cage + sliding doors + fill light),
   hover-gated labels, `RH.Demo` everything-button. Was ~935 lines + 12 art dirs
   sitting uncommitted (asset-loss risk); compiled clean, battery green, then
   committed. Plus the two 2026-07-10 specs + project CLAUDE.md (`4df818d`).

2. **Built the v25 increment** (`6fd3faa`) — closing the game-structure loop
   that was the one real gap. Sim-only, save v25, baseline byte-identical:
   - **The goal ladder**: quota deadlines relative to opening; meeting one opens
     the next (Q2 now active). The program's demands keep coming.
   - **Ending resolution**: hold a declared path → one-time EPILOGUE; a colony
     that had crew and emptied → one-time COLLAPSE (prevention-framed).
   - **Research pays + unlocks**: discovery FundingKg banks toward the next
     award; discovery UnlockRoom flips a dormant station live (the tier ladder
     gated by playing research, not a tech tree).
   - **Station tiers** (tiered-production spec Gate A): Tier/SeatCount/YieldMul
     on rooms; T2/T3 Workstation & Lab lines; workstation seats speed
     construction; an **Infirmary** passively stretches the evac countdown.
   - **Long-game weather**: the authored storm/flare schedule can repeat on a
     cycle (off by default), era-drop rules fold with it.
   - Three new suites (`-ladder -ending -tiers`), all exact.

3. **Wrote three human-runnable guides** (this commit):
   - `docs/asset-pipeline-guide.md` — build assets yourself on the Lambda/
     Manifold A100 (launch → bootstrap → style-lock → gen-3d → mesh-cleanup →
     server → UE import → shut down).
   - `docs/model-material-polishing-guide.md` — mesh polish (weld/smooth/ground)
     + Unreal `M_ModelTex` material tuning (incl. why the emissive floor exists,
     the ISM-usage trap).
   - This status doc.

4. **Ran a model polish pass** on the ~36 game GLBs (headless Blender on the
   A100): QA audit + weld + auto-smooth + before/after preview renders. The QA
   confirmed the meshes are fully-split/faceted (≈18k islands, 94.6% flat) —
   `_polished.glb` variants + preview pairs are on NFS for a re-import decision.

---

## 4. What is owed (unchanged by this session unless noted)

These are **finishing, not structure** — the whole arc is built.

1. ~~**The large DataTable sync**~~ **DONE 2026-07-16, headless.** All 14
   DataTables re-imported from the `docs/data/` CSVs via the `ImportAssets`
   commandlet with `CSVImportFactory` + `ImportRowStruct` JSON settings — no
   editor session needed (the Session-48 editor-free discovery extends to
   DataTables). This landed the new struct columns (`FRHRoomRow`
   Tier/UpgradesTo/SeatCount/YieldMul; `FRHDiscoveryRow` FundingKg/UnlockRoom),
   the new `DT_Rivals` + `DT_Discoveries` assets, `DT_Buildings`
   Category/Blurb, Q2 active, and all accumulated `DT_Config` rows (150 live).
   Verified: full 24-run battery green against the live tables, baseline pins
   byte-identical. The `RH.ActivateRoom`/`RH.ActivateQuota` bridge is no
   longer needed for live runs. Remaining follow-up (needs a director
   compile): drop the in-memory `Debug_Inject*` rows from the self-tests so
   they become true pure-data verifiers that fail on DT/CSV drift.

2. **Director hand-play**, especially the visuals headless can't judge: the
   action card, categorized build menu, Mars look-pass grade, the rigged
   walkers + elevator, pit view, the crew-arrival/zoning/garden/Hope loop, and
   now the v25 ladder/ending/tiers surfaces.

3. **The Gate-D mental-health framing review** — the standing hard stop.
   **Every** player-facing morale/sickness/evacuation/collapse string and icon
   is a PLACEHOLDER pending the director's review under the prevention-focused,
   never-graphic directive. This includes the new v25 EPILOGUE and COLLAPSE
   wording.

4. **Balance.** Every M2–M4 and v25 number is legible-math-first (chosen so the
   tests read cleanly), explicitly awaiting tuning — not tuned play.

5. **The two 2026-07-10 specs beyond Gate A**: greenhouse agriculture (crops/
   growth-stages/fertilizer/climate/ducts) and the rest of tiered production
   (prefab LabFull/Infirmary buildings, click-to-assign, lazy seat-fill) are
   authored and partly art-batched; only tiered-production **Gate A** is built
   (this session). The rest are the obvious next increments.

---

## 5. Risks & gaps (honest read)

- **Determinism is the load-bearing invariant** and it is holding: every
  increment this session kept the 10-sol baseline byte-identical and both
  Hope-band suites parity-exact. The risk is always a future feature that
  mutates state per-step differently in the agent vs era band — the discipline
  (threshold-on-monotone-accumulator, linear scalars, derived-never-saved) is
  what protects it. Keep new features to that shape.
- **The DT-sync debt is the biggest latent trap**: headless is green because it
  reads the CSVs, but a live editor session reads the stale DTs until synced. Do
  the sync before any live demo of M2+ content. The pure-data verifiers will
  fail loudly on drift once the injects are dropped.
- **Visuals are unverified by construction** — headless renders previews but
  "does it read in-game" is a hand-play verdict the director owns. The models
  are a decisive upgrade over primitives, but the polish pass's re-import + look
  check is still owed.
- **No hard fail-state decision** (brief open question #5): the game currently
  has Collapse as a recoverable, prevention-framed empty-colony state, not a
  hard "you lost" screen. That is a deliberate placeholder consistent with the
  mental-health directive; the director owns the final call.
- **Scope**: the full arc is built but shallow in places by design (one region,
  legible-math balance, placeholder wording). It is a complete *structure* ready
  for depth passes, not a finished, tuned, shipped game.

---

## 6. Fast orientation for the next session

- Source of truth order: (1) the director, (2) the design brief, (3) nothing.
- Approvals: `docs/design-decisions.md`. History: `docs/build-log.md`. Balance:
  `docs/data/*.csv` → DataTables.
- Build gate: director quits UE, terminal `Build.sh red_hopeEditor`, reopen,
  `/mcp`. Headless verify loop needs no editor (`-run=RHSim` / `-run=RHArt` /
  `-run=ImportAssets`).
- Pipeline: five skills (`lambda-bootstrap`, `style-lock`, `gen-3d`,
  `mesh-cleanup`, `generation-server`) + the two new guides drive the A100.
