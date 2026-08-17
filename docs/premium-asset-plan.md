# Premium Asset Plan — the finishing doctrine

**Dated 2026-08-14. Status: PLAN — nothing below executes until the director
OKs it** (decision points in §10). This document is the standing orders for
taking every asset class — characters, places, objects, activities, vehicles —
from "AI-generated and readable" to premium, and for making the world visibly
interactive everywhere the sim already is.

---

## 0. The strategy in one paragraph

The gap between what we have and premium is **not generation quality — it is
finishing**. The proven front end stays (Nano Banana Pro identity-anchored 4K
sheets → Hunyuan3D shape on the A100), but its output gets demoted from "final
asset" to **blockout + texture source**. Between generation and Unreal we
insert a scripted, local Blender 5.2 finishing lane: silhouette surgery, real
UVs, Cycles bakes, **camera-projection texturing from the director's own 4K
sheets** (crisper than Hunyuan's paint, and we already own 8 views per
character and multi-view sheets per object), one master-material family, and
data-driven interaction anchors. The style tension the A/B sheet exposed —
new-batch coherence vs old-set readability — dissolves under one rule:
**coherence lives in the surface, identity lives in the silhouette**. Keep
whichever MESH reads best per building, and re-texture every kept mesh into
the one weathered-industrial family (which is already codified in code as the
`RHCanon` palette). Characters get the structural fix AI cannot provide: one
clean base body and one canonical skeleton, per-role modular gear, per-face
projection textures — rig once, author the 10 clips once, and attach job props
to hand sockets instead of fusing tools into meshes ever again.

---

## 1. Ground truth (verified on disk 2026-08-14)

| Class | Have | Honest quality read |
|---|---|---|
| **Characters** | 20 rigged `RH_Walker_<face>` walkers, 10 clips each (incl. WorkBench/WorkLab), textured (baseColor+MR bound) | Textures fine; MESHES are sprite-driven Hunyuan humanoids — lumpy silhouettes, soft faces, mitten hands. 20 separate skeletons × 10 clips = 200 anim assets. 12 painted redo meshes exist unrigged; 8 roster faces have no redo art yet |
| **Character source art** | 12 chars × 8 views (habitat+EVA × F/R/B/L), 4K, identity-anchored, + cutouts (`Martians/gen/chars-redo/`) | Excellent — the best raw material we own. Unused for texturing so far |
| **Buildings** | ~14 originals wired (`RealModelPaths`); 13 painted batch imported, 8 wired behind `rh.ModelSetV2`; A/B sheet rendered | Mixed: new set coherent but some lose function-readability (BatteryBank, Borer, Lander, WaterPlant); old set readable but stylistically scattered; two QA failures (ModularBlock open face, HeavyFreighter proportions) |
| **Object source art** | Multi-view NB sheets per object (`Martians/gen/objects-redo/`) | Available for projection texturing — unused so far |
| **Props / rooms** | 10 per-cell props with hand-built `MI_<name>` instances of `M_ModelTex` | Good; the material contract to generalize |
| **Agri** | 19 crop stage models + humidity regulator + gh_dome/gh_above staged for Gate C | Adequate for stage swaps |
| **Vehicles** | Lander wired; ScoutSpeeder + HeavyFreighter imported, unwired; convoys/fleet exist only as sim events | Static only; no motion presentation |
| **Places** | Terrain w/ normal maps + triplanar; pit floors; elevator (cage, doors, light); hover labels | Functional, pre-atmosphere; no scatter kit, no weather visuals |
| **Pipeline** | Local Blender 5.2 LTS (EEVEE render proven ~6 s/model; Cycles CPU for bakes); headless GLB/CSV import proven; A100 via Manifold (session-dependent); rig loop script on NFS only | The one hard external dependency is the rig loop + any new Hunyuan generation |

**The three deficits that keep this from premium:**
1. **Surface** — chaotic auto-UVs, no normal/AO bakes, no edge wear, two
   competing material lineages (hand-built `M_ModelTex` vs auto Interchange).
2. **Character structure** — no clean topology, no shared skeleton, tools
   fused into meshes, faces at the mercy of Hunyuan.
3. **Motion & state** — buildings don't visibly run, vehicles don't move,
   weather doesn't show, powered/unpowered looks identical.

---

## 2. Doctrine — the rules that make it premium

1. **Readability first.** Silhouette answers *what is it*; the accent color
   answers *what function does it serve*; motion/emissive answers *is it
   working*. Every asset must answer all three at gameplay zoom.
2. **One family.** The in-code `RHCanon` palette (BoneWhite, DarkSlate,
   HazYellow, RustBeam, PVBlue + the glow set) IS the art direction. Every
   shipped asset ends up in one master-material family with a per-function
   accent region. Unify by surface, differentiate by silhouette.
3. **AI proposes, Blender disposes.** Generation output never ships raw.
   The tier (§4) decides finishing depth, not enthusiasm.
4. **Structure over pixels for characters.** Shared base mesh, shared
   skeleton, modular gear, projection-textured faces. Never rig a mesh we
   wouldn't want to keep deforming.
5. **Interaction anchors are data.** Work posts, VFX points, label anchors:
   named points in a code map (graduating to a DataTable if they churn), never
   eyeballed offsets scattered through visualizer code.
6. **Names are contracts.** `RH_Walker_<face>`, clip suffixes,
   `RealModelPaths` keys, `MI_<name>` — art swaps ship with zero code diffs.
7. **The sim is untouchable.** All of this lives in the RedHope module and
   Content. The battery stays green at every gate; the pins stay
   byte-identical. Art that changes sim numbers is a bug by definition.
8. **Everything re-runnable.** Every pass is a script with pinned inputs; a
   .blend is a convenience, never the only source of truth. Receipts (renders,
   import logs, QA numbers) ship with every gate.

---

## 3. The pipeline, stage by stage

**Stage A — Concept (Nano Banana Pro, Vertex).** Identity-anchored multi-view
sheets, Session-51 rules (one figure, flat background, negative-prompt the
halo family), sprite-qa numeric gate before any money moves. ~$1.2–1.7 per
asset sheet. Already proven; unchanged.

**Stage B — Shape (A100, batched).** Hunyuan3D shape (+ paint as a fallback
albedo), mesh-cleanup to ~18 k tris. Output is explicitly a **proxy**.
~82 s/asset; batch everything into one session; terminate after. The only
GPU-dependent stage.

**Stage C — Finish (LOCAL Blender 5.2, scripted, the new core).** Per asset,
in order, each substage auto-rendering a before/after preview tile:
- **C1 Silhouette surgery** (tier-dependent): boolean/lattice/patch fixes —
  e.g. close ModularBlock's open face, fix HeavyFreighter's proportions,
  re-attach a digging arm if we keep the new Borer body.
- **C2 Shading**: weld, `polygon.use_smooth` set directly (the Blender 4.2+
  gotcha — never `shade_auto_smooth`, the glTF exporter drops its modifier
  normals), bevel-weighted hard edges.
- **C3 UVs**: Tier H gets authored seams; Tier S gets scripted seam heuristics
  + Blender 5 Pack Islands; both get a texel-density report.
- **C4 Bakes (Cycles CPU, 2K, overnight-batchable)**: transfer-bake the
  existing albedo onto the new UVs; **camera-projection bake from the NB
  sheets where they exist** (all 12 redo chars, all 13 objects) — set up the
  4–8 views as projected images and bake DIFFUSE to the clean UVs; AO;
  curvature.
- **C5 Texture composite (scripted node graph → baked)**: albedo +
  curvature-driven edge wear + AO grime + function accent-region mask +
  emissive mask (windows, panels, displays). Output: BaseColor, Normal,
  **MRA-packed** (Metallic/Roughness/AO in one texture — memory matters on
  the 8 GB target).
- **C6 Export**: GLB per manifest — ground-center pivot, unit scale, single
  mesh object, no strays.

**Stage D — Characters (special lane, §7).**

**Stage E — Import & wire (headless UE).** Per-asset manifest JSON → the
proven `ImportAssets` loop → a new **`RHArtWire` commandlet** (RedHope
module): re-parents imported materials to master-material instances with the
right parameters, sets collision (simple auto-convex, per-asset overrides),
verifies texture bindings, logs receipts. One director compile when the
commandlet lands; after that the whole lane is headless.

**Stage F — Behavior (presentation-only, event-driven).** The "interactive"
layer, per class — §8.

**Stage G — QA.** `mesh_qa.py` (numbers of §4), screenshot batch at three
zooms via the BP console driver + capture extraction (known gotchas
documented), full battery, director gate.

---

## 4. Tiers and quality bars

| | Tier H (hero) | Tier S (standard) | Tier B (bulk) |
|---|---|---|---|
| **Members** | Crew, Lander, Forge, Habitat, Elevator, Borer | All other buildings, vehicles, big props | Rocks, scatter, far-background |
| **Tris** | ≤ 35 k | ≤ 20 k | ≤ 8 k |
| **Textures** | 2K BaseColor + Normal + MRA (chars: +1K gear set) | 2K set | 1K, or atlas-shared |
| **UVs** | Authored seams, ≥ 85 % space, texel ±20 % | Scripted seams + pack, ≥ 80 % | Auto |
| **Finishing** | C1–C6 full, hand-checked | C2–C6 scripted | C2 + C6 only |
| **Collision** | Convex ≤ 64 verts or authored | Auto-convex | None/box |

Characters additionally: ≤ 25 k body+gear, ≤ 4 bone influences, socket set
(`Prop_R`, `Prop_L`, `Back`, `Helm`), the 10-clip contract
(Walk/Idle/Charge/Dig/Haul/Operate/Plant/Repair/WorkBench/WorkLab), and later
Sit/Lean idle variety.

**No Nanite** (unneeded at these budgets, 8 GB Metal target), **no
MetaHumans** (style and perf mismatch), **no marketplace packs** (identity),
**no freehand sculpt marathons** (scripted passes + director verdicts).

---

## 5. The material system

One master family in `/Game/RedHope/Art/`:
- `M_RH_Master` (statics): BaseTex, NormTex, MRATex, AccentColor +
  AccentMask (function color — feeds from the same values as `TintFor`),
  EmissiveMask + EmissiveColor + **PoweredState** scalar (0/1 drives emissive
  + subtle panel glow), WearAmount, plus the documented 0.08 emissive floor.
- `M_RH_Character`: same, minus PoweredState, plus a subsurface-ish skin tint
  slot kept subtle.
- `M_RH_Motion` variant: adds rotator/panner inputs for fans, radiators,
  holo-panels — ambient machine motion without skeletal cost.
- Existing `M_ModelTex` instances re-parent to the family; auto-Interchange
  materials get replaced by `RHArtWire`. End state: **every mesh in the game
  wears an `MI_<name>` of one master** — one dial for the whole world's look.

---

## 6. The A/B resolution (buildings)

Per the 2026-08-14 comparison sheet: keep the mesh that reads, unify the
surface. Concretely proposed (director confirms per line in Phase 1):
- **New mesh**: Forge, Habitat, ComputeModule (clear wins).
- **Old mesh, re-textured into the family**: BatteryBank (keep the display
  panels — add green emissive displays to its accent mask), Borer (keep the
  digging arm), WaterPlant (keep tanks-and-pipes), Lander (keep splayed
  descent stage) — each gets the C2–C6 pass so it stops looking like a
  different game than the new set.
- **Either**: SolarArray (call it in-boot).
- **Fix then adopt**: ModularBlock (close the face), HeavyFreighter
  (proportion surgery) — both C1 jobs.
- `rh.ModelSetV2` retires once the mixed set is final (the map becomes the
  one true `RealModelPaths` again).

---

## 7. Characters — the structural fix

1. **One base body**: start from the CC0 Blender Studio human base mesh
   (clean quads, deformation loops), re-proportion to the game's slightly
   stylized read (the proportion dial is a director pick on the pilot
   renders). This body is rigged and weighted ONCE.
2. **One canonical skeleton** (`RH_Crew`): bone names preserve the existing
   clip contract. The rig loop (retrieve from Somnora-East
   `red_hope/scripts/` next box session; rewrite locally in bpy if lost —
   it's CPU work, no GPU needed) re-emits the 10 clips once on this skeleton.
   Import branch A: all walkers share the one skeleton asset (20× memory win,
   clips authored once forever). Fallback branch B if Interchange fights the
   shared-skeleton import: identical per-walker skeletons from the same
   armature + scripted clip duplication — same authoring win, less memory win.
3. **Per character** (scripted): shrinkwrap/fit the base to that character's
   redo mesh silhouette → **projection-bake the face/suit albedo from their
   8 identity-anchored 4K views** → attach the role's gear kit (helmet, vest,
   pack, tool holsters — small separate meshes, mix-and-match across roles) →
   transfer weights from the base → export.
4. **Job props in hands, never fused**: crate for Haul, wrench for Repair,
   seed tray for Plant, tablet for Operate — tiny static meshes attached to
   `Prop_R`/`Prop_L` per active clip in `RHCrewVisualizerSubsystem` (it
   already tracks `CurrentClip`; attach/detach rides the same switch). This
   kills the fused-wrench problem at the root and is most of "everyone
   visibly working."
5. **Roster**: 12 redo characters cover 12 of 20 faces. The remaining 8
   (cook_moreau, comms_diallo, driver_costa, fab_stone, rookie_shaw,
   safety_abara, vet_kowalski, bot_lindqvist) need NB sheets (~$13) — or
   keep their current meshes on the unified material until a later batch
   (they'll read as background crew). Director call (§10).
6. **Robots unchanged** until the crew pass proves out (standing scope).

---

## 8. What "interactive" means, per class (Stage F)

All presentation-layer, all driven by **existing** sim events — no sim edits:
- **Buildings**: construction ghost → built swap (exists) + PoweredState
  emissive (off when unpowered/brownout — the sim already knows), ambient
  motion (AirFilter fan spin, Forge glow pulse + Niagara sparks, radiator
  shimmer), function accent = `TintFor` fed into the material.
- **Crew**: job clips at posts (exists) + hand props (§7.4) + idle variety
  later. Morale/status surfaces REMAIN abstract per the Gate-D standing rule.
- **Crops**: stage swaps (exists) + planter soil-wetness material state +
  greenhouse interior fill light by climate setting.
- **Vehicles**: Lander descent/launch flight arc + engine-dust Niagara on the
  existing Deliver/Launch events; ScoutSpeeder as the convoy/trade courier on
  a spline (Fleet/Convoy events); HeavyFreighter flyover on ship arrivals.
- **Places**: exterior atmosphere pass (height fog, sky gradient + sun disc,
  wind-gust dust Niagara keyed to the weather schedule when active),
  slope/curvature terrain layers + deterministic rock scatter (ISM per the
  documented usage guide); pit interior lighting per room function; elevator
  hero pass (cables, interior light, door ease curves).
- **Interaction anchors**: `RHAssetPoints` code map (asset → named points:
  WorkPost, VFX_Engine, VFX_Stack, Label) consumed by visualizers; graduates
  to a DataTable + drift verifier only if it churns.

---

## 9. Phases & gates (each = commit + push + receipts + director stop)

**P0 — Foundations** — *EXECUTED 2026-08-14, awaiting one director compile.*
- G0.1 **DONE** — `docs/art-bible.md`: `RHCanon` converted to authoritative sRGB
  hex, the 14 function accents, wear discipline, tier budgets, naming contracts,
  an 8-point per-asset acceptance checklist, standing prohibitions.
- G0.2 **WRITTEN** (needs the compile) — `M_RH_Master` + the wiring pass, as a
  new `RHArtWireCommandlet` (`-run=RHArtWire [-master] [-wire] [-dryrun]`).
  Recon changed the design twice, both for the better: (a) an editor-only,
  asset-authoring, package-saving commandlet **already exists** in this repo
  (`URHArtCommandlet`, `-run=RHArt`) so the new one copies a proven pattern
  rather than inventing one; (b) it needs **zero Build.cs changes** —
  `UMaterialInstanceConstant`, `SavePackage` and `FAssetRegistryModule` are all
  already reachable from `RedHope`, and pulling in `UnrealEd`/`MaterialEditor`
  would have dragged Slate into the closure for no gain. The master is
  deliberately texture-light (BaseTex + scalars) because a texture parameter
  whose default is the wrong sampler type fails to compile, and today's assets
  have no normal/MRA maps to point at — those arrive with the C4 bakes in P1.
  Wired set = **the 11 meshes the game actually renders**, spanning both
  lineages, so the kept originals are re-parented onto the same master.
- G0.3 **DONE** — `scripts/blender/` (`rh_lib.py`, `rh_finish.py`, `README.md`),
  proven on **13 assets**, not the 2 planned. Two findings changed the doctrine:
  **(1)** the whole painted batch shipped **unwelded** — 2.99 verts/tri, every
  triangle carrying its own vertices, which forces faceted shading regardless of
  normals. Welding merges ~83 % of vertices on every asset while preserving
  triangles and UVs exactly (0.544 → 0.544 utilisation, measured), and the
  visual difference is obvious: `Martians/gen/weld_smooth_proof_20260814.png`.
  **(2)** re-unwrapping is a REGRESSION on assets that already have good UVs
  (smart_project dropped HabitatDome from 54.4 % to 13.8 % utilisation and broke
  texture correspondence), so `--uv` is opt-in and only correct when every map is
  being re-baked. Cycles headless baking was proven working before anything was
  built on it.
- G0.4 **DONE** — the mixed set is wired (§6), and `rh.ModelSetV2` now means
  "mixed set" vs "all originals".

**P1 — Buildings to bar** — *EXECUTED 2026-08-14, no compile required.*
- G1.1 **DONE** — the whole remaining library welded (36 meshes: 7 building
  models, 19 agri, 10 props) and re-imported **in place**. Receipts: battery
  53,920 → 24,853 verts, lander2 → 17,231, stockpile → 13,771, all with triangle
  counts preserved exactly. 10 of the 11 rendered models are now welded;
  `RH_AirFilter2` is the one hold-out because no local source file exists for it.
  Honest note: the FBX-era originals already carried smooth normals despite being
  unwelded, so for those the win is ~55 % fewer vertices rather than a visible
  change — unlike the Models2 batch, where welding was dramatic.
- G1.2 **DONE, and cheaper than planned.** `UMaterialEditingLibrary` turns out to
  be fully exposed to Python, so ambient motion shipped with **no compile**: the
  master gained `PulseDepth`/`PulseSpeed` and an emissive pulse that multiplies
  *through* `PoweredState`, which means an unpowered machine goes dark **and
  still** for free — no tick, no components, works on instanced meshes. Tuned per
  machine (forge a slow furnace throb, ComputeModule a fast data blink,
  structures deliberately still). Niagara is available but was **not** needed for
  this beat; deferred rather than spent.
- G1.3 texture pass **DONE**: AO + curvature baked per asset (Cycles CPU, ~5 s
  each) and composited into a new BaseColor — contact shadowing, curvature-driven
  edge wear, cavity grime. Reported honestly: this is a *modest* gain, clearly
  visible on complex assets (OreExtractor treads, IceProcessor pipework) and
  marginal on convex ones. **Normal-map baking was cut from the plan**: with no
  high-poly source anywhere in the pipeline there is nothing for it to capture.
- Still owed from P1: the two C1 silhouette repairs (ModularBlock's open face,
  HeavyFreighter's proportions — both unwired, so they block nothing) and the
  three-zoom screenshot batch, which wants a live boot.

**P2 — Characters** (~16–25 h; the long pole; prep starts during P1)
- G2.1 Base body + `RH_Crew` skeleton + rig-loop retrieval/rewrite + clips
  re-emitted once. *6–10 h (the range is the rewrite risk).*
- G2.2 Three pilots (suggest cmdr_vale, eng_ruiz, med_haddad) through the
  full lane → in-game contact sheet → **director pick gate**. *4–6 h.*
- G2.3 Roster rollout ×12; missing-8 decision executes. *4–6 h scripted.*
- G2.4 Job hand-props + attach logic. *2–3 h + compile.*

**P3 — Places** (~15–24 h)
- G3.1 Exterior: terrain v2, rock kit, atmosphere, look-grade vs the Mars
  reference. — G3.2 Pit/interiors + elevator hero pass. — G3.3 **Agri Gate C
  lands here** (above-ground drag-footprint greenhouse + parametric dome, per
  its 2026-07-10 spec; the gh_dome art is already imported).

**P4 — Vehicles & activity** (~6–10 h)
- G4.1 Lander flight + dust. G4.2 Convoy courier + freighter flyover.
  G4.3 Ambient density polish (post occupancy, idle variety).

**P5 — Hardening** (~4–6 h)
- G5.1 8 GB perf pass (texture budget, instancing audit, draw calls).
- G5.2 QA sweep vs §4 numbers; guides updated (asset-pipeline-guide v2);
  PROJECT_STATUS art section rewritten. G5.3 Full battery + live-boot
  acceptance.

Total ≈ **55–85 focused hours ≈ 12–16 working sessions**, directed pace.
GPU money: near-zero until new generation is needed (shape batches exist);
NB missing-8 ~$13 if approved; occasional $2–4 A100 session.

---

## 10. Decision points (the director's, before P0 starts)

1. **A/B mixed set** — accept §6's per-building proposal (or amend lines).
2. **Character stylization dial** — realistic / lightly stylized (~15 %) /
   chunky: shown as three pilot variants at G2.2; pick then.
3. **Missing-8 faces** — generate NB sheets now (~$13) or keep old meshes as
   background crew until a later batch.
4. **Phase order** — P1→P2→P3 as written, or pull Places forward if a vista
   demo matters sooner.

---

## 11. Risks & diligence

- **Shared-skeleton headless import** may fight Interchange → fallback branch
  B defined (§7.2), same authoring win.
- **8 GB bakes are slow** → 2K cap, MRA packing, overnight batches.
- **Projection seams** (multi-view blending) → per-view incidence-weighted
  masks; fallback = Hunyuan paint transfer (what we ship today, so the floor
  is never lower than now).
- **License diligence before any commercial ship**: ~~verify Hunyuan3D 2.1
  weight license terms for commercial use, SDXL/IP-Adapter terms, and
  Vertex/NB output terms.~~ **ASSESSED 2026-08-16 -> `docs/asset-licence-audit.md`.**
  Two items need a decision, not just a note: nvdiffrast is NON-COMMERCIAL and is
  a hard dependency of the TRELLIS.2 export path, and Hunyuan3D's Territory
  excludes the EU, UK and South Korea. Attribution owed for DINOv3, Tencent and
  Real-ESRGAN -> `docs/third-party-notices.md`, which does not yet ship.
  CC0 base meshes are safe.
- **Scope creep** → the tier table is the budget; anything wanting Tier H
  treatment outside the Tier H list is a director decision, not a drift.
- **Manifold availability** is session-dependent → only Stages B and the
  one-time rig-loop retrieval depend on it; everything else runs local.

---

## 12. Appendix — to build

**Scripts** (all local bpy/py, each with auto-preview): `finish_shading.py`,
`finish_uv.py`, `bake_transfer.py`, `bake_projection.py`, `compose_textures.py`,
`mesh_qa.py`, `export_manifest.py`, `char_fit.py`, `char_gear.py`,
`clip_emit.py` (port), plus `RHArtWire` (C++ commandlet, RedHope module).

**Naming contracts** (unchanged + new): `RH_Walker_<face>`, clip suffixes,
`MI_<name>`, `RealModelPaths` keys, `T_<name>_BC/_N/_MRA`, points map keys
`WorkPost/VFX_*/Label`.

**Per-gate receipts**: before/after render pairs, mesh-QA table, import logs,
battery summary (0 sim errors + END markers + pins), in-game screenshots.
