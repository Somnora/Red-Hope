# The Red Hope — Project Status

**Dated 2026-08-18.** This is the working source-of-truth snapshot: what the
game IS, what is built, what changed, and what is owed. Ordered by the
three-phase arc from the design brief
(`docs/the-red-hope-design-brief.md`).

The SIMULATION is unchanged since the 2026-07-15 consolidation - sections 1 and 2
below still describe it accurately. Everything since has been the ART AND
PRESENTATION arc, summarised in section 3b.

Verification bar for every claim below: the full headless self-test battery
(**25 suites + the 10-sol baseline, all green**) plus the byte-identical
regression pins (`power: gen 50 W load 20 W`, `deposit Regolith_A: 40645 kg`),
which have not moved once across the whole graphics arc.
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

## 3. What the 2026-07-15 consolidation changed (history, kept)

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

## 3b. The art & presentation arc (2026-08-14 → 08-18)

No simulation code changed. The battery stayed 25/25 with identical pins
throughout, which is the evidence that none of this touched the sim.

**One root cause explained years of "splotchy".** The character pipeline
DECIMATED BEFORE RIGGING. Decimation shatters a mesh into disconnected shells
(measured: one crew mesh goes from 1 connected component to 95, largest holding
38%); a shattered mesh unwraps to confetti UV islands, so the baked paint IS
confetti; and Blender's heat weighting fails on it, falling through to a backstop
that hard-pinned **53,885 of 53,885 vertices** to one nearest bone each. Rigid
binding then tears geometry off the body at the joints in motion - the "you can
see through parts of their body" report. Fixed by welding before the bind (0
backstop hits after), by blending the backstop over 3 nearest bones instead of
snapping to 1, and by importing the coherent source paint over the confetti.
Full account: `docs/crew-generator-test.md`.

**Two shading inputs did not exist.** Neither master material had a normal input
(`M_RH_Master`, `M_RH_Character` both dumped `Normal <- <none>`), and the
character master had no metallic/roughness input either - so every walker's
imported roughness map sat inert. Both added, opt-in, with 42 derived normal maps
wired. Honest measurement: at the distances the strategy camera actually reaches
(29 m closest), the whole normal-map change moves 0.08-0.29% of pixels. It is
foundation, not a visible win.

**What DOES read at 29-216 m**, established by measurement rather than taste:
silhouette, large-scale value contrast, emissive points, and lighting. Hence the
per-building night lamps (every powered building, accent-tinted, riding the
existing power pass) rather than more surface detail.

**Asset work:** 12 room props and 10 buildings re-baked through TRELLIS.2; 215
orphan assets deleted (art tree 939 → 736); 13 baked-in floor plates removed;
the crew's subsurface-shader bug fixed; licence notices now ship beside the
executable; the project stopped calling itself "Top Down BP Game Template".

**The 2026-08-16→18 tail of the arc** (commits `6816d4b`→`f12736d`): the
surface catastrophe was root-caused to `M_RH_Master` failing the Metal shader
translator — every master-family building had rendered engine-default gray
in-game since mid-08-17 while editor dumps looked healthy (the fix: a
TC_GRAYSCALE white default for the EmissiveMask sampler; the doctrine: a frame
with material compile failures is NOT JUDGEABLE, now hard-counted by
`rh_capture.sh` and testable via `viewmode unlit`). Five buildings then went
through the new hero lane (Nano-Banana 4K hero reference → TRELLIS.2 with
kept hi-poly → Blender-baked real 2048 normals, ~$0.35–0.45 each):
Electrolyzer, IceDrill, Pylon (solid monopole re-roll), Borer, AirFilter.
The director's six photo notes were each root-caused and fixed (furniture
avoidance on all walks; bone-roll re-rig of all 20 walkers killing the
belly-stretch and wrong-plane elbows; `rh.RobotPlantCm` grounding the robots;
derived-normal strengths halved — the "TV static" amplifier). A complete
beginner paint pipeline now exists for the director (`rh_paint_kit.py` /
`rh_paint_return.py` + the Paint Shop artifact). Still on old paint:
BatteryBank, WaterPlant, Lander, Stockpile — queued for the same lane.

**Infrastructure:** Red Hope moved off the shared `Somnora-East` filesystem onto
its own `red-hope-east` (checksum-verified, acceptance-tested by bootstrapping
the lane from it). The GPU lane's tooling - 71 files including `rig_colonist.py`,
the rigger everything depends on - was rescued off the filesystem into git, where
it had never been.

**Two mistakes worth carrying forward.** (1) A re-rig replaced 12 crew meshes
without their textures, leaving new UVs bound to old paint - the exact trap
`rh_import_textures.py`'s docstring already warned about. (2) The migration
excluded a 14 GB `io/` directory as "rebuildable intermediates" and then deleted
the source; it held the only surviving sources for 8 characters, who are now
stuck on confetti paint until regenerated. The rule earned: **commit the
excluded-file list before deleting a migration source.**

---

## 4. What is owed

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

2. **Director hand-play** - the visuals headless cannot judge. Boot it without
   the editor: `bash scripts/unreal/rh_sandbox.sh` (add `surface` for the
   building set, `LOW=1` for a busy machine). Outstanding verdicts live in
   `docs/qa-boot-card.md`; the one that gates most work is verdict 1 on the
   crew, which must be RE-TAKEN because it was given before the subsurface fix,
   the normal/MR inputs, the rigid-bind tear fix and the paint fix all landed.

2b. **Open decisions that are the director's, not repairs** (updated
   2026-08-18): night-lamp brightness and the 40% accent tint; the five
   hero-lane buildings (esp. Borer/AirFilter paint acceptance, which unlocks
   their emissive re-cut); Pylon monopole vs lattice. Two former entries are
   RESOLVED: crew now really avoid furniture on every walk (`1f68ffe`), and
   the 8 lost-source crew were regenerated and re-rigged (`97101bc`, crew8).

3. **The Gate-D mental-health framing review** — the standing hard stop.
   **Every** player-facing morale/sickness/evacuation/collapse string and icon
   is a PLACEHOLDER pending the director's review under the prevention-focused,
   never-graphic directive. This includes the new v25 EPILOGUE and COLLAPSE
   wording.

4. **Balance.** Every M2–M4 and v25 number is legible-math-first (chosen so the
   tests read cleanly), explicitly awaiting tuning — not tuned play.

5. **The two 2026-07-10 specs beyond Gate A** — updated 2026-07-16:
   **greenhouse agriculture Gates A/B/D are BUILT** (save v26): DT_Crops with
   7 dormant crops + growth stages + stage-art visualizer wiring (19 agri
   models imported under /Game/RedHope/Art/Agri), soil depletion + the
   compost/spoil-chems -> FertilizerMix loop, per-floor climate with the
   HumidityRegulator gate, duct CO2/O2 exchange; `-agri` self-test exact,
   baseline pins untouched (crops dormant = legacy garden bit-for-bit).
   Still owed from the specs: agri **Gate C** (above-ground drag-footprint
   greenhouse + parametric dome - a new placement interaction, deliberately
   deferred to a presentation session) and the rest of tiered production
   (prefab LabFull/Infirmary buildings, click-to-assign, lazy seat-fill).
   Crop activation (`RH.ActivateCrop all` or flipping the CSV) is a
   director gate flip.

6. **Building models, updated 2026-08-14.** The painted batch generated on the
   A100 on 2026-07-17 (Hunyuan3D shape + PAINT from the Nano Banana Pro 4K
   sheets) is imported — 13 objects under `/Game/RedHope/Art/Models2/<N>/<N>/`,
   13/13 clean Interchange receipts. Eight are wired in `HandleBuildingAdded`:
   painted replacements for Forge, BatteryBank, WaterPlant, Lander, SolarArray,
   Habitat and Borer, plus first-ever model coverage for ComputeModule. The
   swap rides `rh.ModelSetV2` (default 1; set 0 + re-run `RH.Demo` for the
   originals) so the director can A/B in one boot. **Owed: that verdict.**
   Held back: ModularBlock (open face) and HeavyFreighter (skinny) failed batch
   QA; AirlockModule, GreenhouseDome and ScoutSpeeder have no building DefName
   yet (the dome is Gate C's). Note the new models wear the auto-generated
   Interchange material, not the hand-built `MI_<name>`/`M_ModelTex` instances
   the older models use — if they read flat beside their neighbours, the fix is
   a material-instance pass, not regeneration.
   *(Updated 2026-08-18: `ModelPathsV2` now carries 9 rows — the four
   surviving 07-17 meshes plus the five hero-lane assets, all on `M_RH_Master`
   instances with canon accents. The only buildings left on the oldest paint
   are BatteryBank, WaterPlant, Lander and Stockpile, queued for the hero
   lane with their identity verdicts preserved.)*

7. **Character redo — premise corrected 2026-08-14, gate NOT started.** The
   spec's diagnosis ("the paint stage never ran, so the crew are gray statues")
   is **false**: all 20 `RH_Walker_*` assets carry a baseColor +
   metallic/roughness pair with a material binding both, imported in 29bb31b.
   The "weirdly unfinished" read is therefore about mesh/art quality inherent
   to sprite-driven Hunyuan3D humanoids, and re-running the same pipeline would
   return the same class of result. Get a director verdict on the 2026-07-17
   crew contact sheet first; if it still misses, the real options are a
   different generator, a retopo/sculpt pass, or a deliberate stylization —
   not a repeat. Also note: the rig loop lives only on Somnora-East, and there
   is no working local Blender (the Homebrew cask is registered but the app is
   gone), so any rig work needs the GPU box up. *(Update, same day: Blender
   5.2 LTS reinstalled and proven — local renders/bakes work again; only the
   rig-loop script retrieval still needs the box.)*

8. **The premium asset plan — authored 2026-08-14, awaiting director OK.**
   `docs/premium-asset-plan.md`: the full strategy for characters, places,
   objects, activities and vehicles — AI generation demoted to blockout +
   texture source, a scripted local-Blender finishing lane (real UVs, bakes,
   camera-projection texturing from the identity-anchored 4K sheets), one
   master-material family keyed to the in-code RHCanon palette, a shared crew
   skeleton with modular gear and socketed hand props, data-driven interaction
   anchors, and six phased gates (P0–P5) with quality bars, costs, risks and
   the director's four open decision points.

---

## 5. Risks & gaps (honest read)

- **Determinism is the load-bearing invariant** and it is holding: every
  increment this session kept the 10-sol baseline byte-identical and both
  Hope-band suites parity-exact. The risk is always a future feature that
  mutates state per-step differently in the agent vs era band — the discipline
  (threshold-on-monotone-accumulator, linear scalars, derived-never-saved) is
  what protects it. Keep new features to that shape.
- **The DT-sync debt**: headless reads the CSVs, a live editor reads the DTs, so
  run `scripts/unreal/rh_sync_datatables.py` after any CSV edit and before any
  live demo. Parity is green as of 2026-08-18.
- **Generated art has no stable UVs.** TRELLIS.2's unwrap is not deterministic -
  the same seed has produced 7,817 and 7,571 triangles - so ANYTHING derived from
  a UV layout (emissive masks, derived normals) must be regenerated on every
  re-bake, and a re-bake that ships without its textures leaves new UVs on old
  paint. Both failure modes have already bitten once each.
- ~~**Eight crew are wearing ruined paint.**~~ RESOLVED `97101bc`: the eight
  were regenerated from fresh 4K sheets (crew8), rigged and imported; all 20
  walkers were then re-rigged again in `1f68ffe` (bone-roll planes + blended
  backstop).
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
  `docs/data/*.csv` → DataTables (sync with `rh_sync_datatables.py`).
- Build gate: the editor must be CLOSED for `Build.sh red_hopeEditor` - with a
  live editor the linker produces a hot-reload patch dylib that only that editor
  loads, so headless boots keep running the old code and a fix silently does not
  apply. Headless verify needs no editor (`-run=RHSim` / `-run=RHArt`).
- Look at the game without the editor: `bash scripts/unreal/rh_sandbox.sh`.
- GPU lane: `filesystem=red-hope-east` (NOT Somnora-East, which belongs to the
  other projects). Bootstrap to `TRELLIS2_ENV_READY`, then bake. Everything the
  lane runs is in `scripts/gpu/` - `lane/` is the rescued history including
  `rig_colonist.py`, the rigger every character depends on.
- Art rules the hard way, all in `docs/asset-pipeline-guide.md`: decimate NEVER
  before rigging; weld before bind; re-import textures whenever you re-import a
  mesh; regenerate every UV-derived artifact after a re-bake; and judge a
  generated asset on a contact sheet before it enters the project.
- Anything that runs on a rented box and is not reproducible from this repo is
  one `terminate` away from gone.
