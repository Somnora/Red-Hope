# QA Boot Card — updated 2026-08-17 against `cb558e0`

Regenerated 2026-08-16 against `bccf0f0` (the card before that was 40 commits
stale). Nine commits have landed since, so the deltas are recorded in §0 rather
than letting the card drift again.

**No compile pending.** `cb558e0`'s one-line locker swap was compiled
2026-08-17 (`Build.sh red_hopeEditor Mac Development` → `Result: Succeeded`,
editor closed so the BASE dylib was linked, not a hot-reload patch) and proven
live: the smoke boot loads `Props2/locker` with its 2048 textures and the old
`Props/locker` appears nowhere in the log. Battery 25/25 green, pins identical
(`gen 50 W load 20 W`, `Regolith_A: 40645 kg`).

## 0. What changed since the card was regenerated

Read this first — several items below were rewritten by it.

- **The crew were on a skin shader.** All 21 walkers rendered through
  `M_GLTF`, whose shading model is `MSM_SUBSURFACE_PROFILE`. Subsurface
  scattering bleeds light *through* thin geometry and mottles the surface: one
  flag, both of the director's symptoms ("splotchy", "see through parts of their
  body"). Now on `M_RH_Character`. §1 rewritten; judge the zoom ladder.
- **That was a class, not an incident.** A sweep of all 939 art assets found 97
  data maps on colour compression and 78 meshes on the skin shader. Every live
  one is fixed; the elevators were the biggest visible win. One deliberate
  exception remains (`forge`, vertex-coloured).
- **The white platforms are real and mostly gone.** `rh_cut_plate.py` had
  diagnosed them in July and been applied to furniture only. Cut and reimported
  on `conduit`, `battery`, `ice`, `crop_vine_3`, `lander2`, `extractor2`,
  `stockpile`, `crop_root_2`, `crop_vine_2`. The most frequently drawn plate was
  a *caller* bug, not an asset bug — see the compile note above.
- **215 orphan assets deleted.** Art tree 939 → 736. Garden, Struct and Furnish
  are gone entirely.
- **12 room props re-baked** through TRELLIS.2 (`conduit` rejected on
  silhouette) → `qa-props-refresh-sheet.jpg`, `qa-props-refresh-ingame.jpg`.
- **The game is no longer called "Top Down BP Game Template"**, and the licence
  notices now ship beside the executable.
- **In flight at the time of writing:** 10 live buildings and the 8 remaining
  Hunyuan crops, both re-baking through TRELLIS.2. Sheets will follow; nothing
  is imported until it passes a low-angle frame.

**Every item below has a frame already shot**, in `docs/qa/2026-08-16/`. Boot
it yourself if you want to poke the knobs, but you can answer all six verdicts
from the sheets without launching anything.

## Boot sequence, if you want it live

```
RH.Demo
RH.ActivateCrop all
RH.Floor 0          # surface (RH.Demo rides you down to -1)
RH.Showcase         # one of every building on a grid, 16 m north of the Lander
```

Cameras that actually frame things (learned the hard way — `RH.CamCrew` fires
before the colonists exist, and every `-ExecCmds` runs at t=0):

```
RH.Cam 0 10 0.03 0 0      the vault interior
RH.Cam 37 0 0.30 0 0      the showcase grid   (needs RH.Floor 0 first)
```

---

## 1. Crew — THE headline check → `qa-crew-subsurface-fix.jpg`

**Re-shot 2026-08-17 after a real defect was found. Judge the AFTER frame; the
earlier `qa-crew.jpg` shows the broken state.**

Director: *"a lot of the character models still look splotchy, sometimes you can
see through parts of their body."* Correct on both counts, and I had wrongly
written off the transparency in the first card as motion blur. It was not.

All 21 walkers (20 crew + the robot) had slot 0 on Interchange's auto-generated
`Materials/Material`, based on `/InterchangeAssets/gltf/Substrate/M_GLTF`, whose
shading model is **MSM_SUBSURFACE_PROFILE** — a skin/SSS shader. Subsurface
scattering bleeds light *through* thin geometry and mottles the surface: one
flag, both symptoms. `M_RH_Character` (opaque, default-lit, skeletal usage on)
and all 21 `MI_RH_Walker_*` instances already existed, correctly authored, and
had simply never been put on a mesh slot. Second contributor: every
metallic-roughness map was `TC_DEFAULT`, putting DXT block artifacts straight
into roughness, which reads as mottled specular. Both fixed.

**RETRACTED 2026-08-17: the "residual albedo splotch" was not demonstrated.**
I claimed the leftover speckle was Hunyuan paint artifacts needing a GPU
re-paint. I could not substantiate it and the metric I built to prove it was
wrong: it scored "desaturated near-white area", which ranked med_haddad worst
at 20.8% — because he is the medic and that is his WHITE COAT. It measured how
much white clothing a character wears, not defects. The atlases are coherent,
and the import settings are correct (1024 native, `max_texture_size=0`,
`lod_bias=0`, TC_DEFAULT, TEXTUREGROUP_WORLD — nothing is being downscaled).

**Judge from `qa-crew-zoom-ladder.jpg`, not from magnified crops.** Every crew
frame before it was a 3–5× blow-up of a figure ~60 px tall, which is the wrong
instrument. The ladder shows the crew at the four distances the strategy camera
actually reaches:

| zoomT | distance | crew read as |
|---|---|---|
| 0.45 | 216 m | the default opening camera — a few pixels |
| 0.16 | 54 m | small figures, silhouette only |
| 0.07 | 35 m | legible people |
| 0.03 | **29 m — the closest the camera goes** | clearly legible, no visible speckle |

There is no zoom in this game at which the speckle I was chasing is visible.

What I see, for you to agree or overrule: suits now read opaque with legible
colour and trim — a white/orange EVA suit and a blue-trimmed lab coat are
clearly distinct. Silhouettes are legible as people.

**This gates P2 Characters (16–25 h, the long pole in the plan).** Pass and it
shrinks or disappears; fail and it is a *mesh* verdict — different generator or
retopo — because paint is no longer the problem.

> **UPDATE 2026-08-17 — the "different generator" branch is now closed by
> evidence.** The three shipped sprites were re-baked through TRELLIS.2 and
> rendered against the Hunyuan meshes in one fixed light rig
> (`docs/crew-generator-test.md`, sheet
> `qa/2026-08-17/qa-crew-generator-test.jpg`). The Hunyuan meshes are NOT lumpy —
> clean silhouettes, correct proportions, separated fingers — and on gear detail
> they are arguably better; TRELLIS.2 smooths away eng_ruiz's harness and
> geo_okafor's vest pockets. TRELLIS.2 wins only on topology: 0.86–0.93 verts per
> triangle against Hunyuan's **2.99, i.e. completely unwelded**.
>
> So a re-bake would spend the plan's biggest budget line, lose detail, and not
> address the complaint. **Re-take this verdict before authorising any rebuild**:
> it was given before the subsurface-shader fix AND before `M_RH_Character` got a
> normal input and its MR input, so it was taken on crew that could not display
> surface detail at all. The live suspect for "see through parts of their body"
> during MOTION is now the 53,860 split vertices meeting automatic weights — a
> weld + re-rig, which is hours rather than 16–25 h.

## 2. Buildings — the mixed set → `qa-modelset-ab.jpg`

`rh.ModelSetV2 1` (default) over `rh.ModelSetV2 0`, same camera, same sun.

Two buildings change materially and the rest are shared between the sets:

- **SolarArray**: V2 is a tilted panel on a tripod; V0 is a squat dome-topped
  box. V2 reads as "solar" from across the map, V0 does not.
- **HeavyForge**: V2 is a rust-red vessel with a hazard-yellow band and legible
  pipework; V0 is a brown blocky mass.

The rest of §6's per-building proposal in `premium-asset-plan.md` still wants
your line-by-line, but these two are where the A/B actually bites.

## 3. Interiors — the Tiers set is now complete → `qa-interior.jpg`, `chemtable-lg-in-game.jpg`

All five Tiers pieces exist and sit in one band: 7,566–7,843 tris, 0.92–1.20
v/t, ~100 × 40–46 cm footprint, all furnishing at scale 2.50.

`chemtable_lg` was the last player-reachable room wearing the wrong mesh — it
was a 17,381-tri near-cube shared under three names, rendering as a 2.5 m
square block 1.9 m tall. Its detail frame shows the replacement: closed opaque
cabinet with a teal readout, capped sample rack, microscope, centrifuge,
hazard-yellow front stripe, and **clear open space under the worktop** — your
"you cannot see the floor underneath a lot of the desks" answered in pixels.

Crops are shown live here (`RH.ActivateCrop all`). **They are `SliceActive=FALSE`
in committed data**, so a default boot shows none of them. Flipping them is
your call, not a chore — see §6.

## 4. Cutaway modes — built, compiled, never judged → `qa-cutaway.jpg`

`rh.Cutaway 0|1|2`, same camera:

- **0** exterior: near wall present
- **1** no-roof slice — the current default, near wall dropped
- **2** floorplan: every wall face down

Mode 1 is what you already had; 0 and 2 are new and have never been reviewed.

## 5b. Night window lights — NEW 2026-08-17 → `qa/2026-08-17/qa-night-window-lights.jpg`

Every powered building now carries a small lamp (21 cd, 12 m, shadowless) tinted
to its own authored accent pulled 60% toward warm white, at doorway height. The
colony reads as inhabited at night instead of as dark shapes.

Why this and not more emissive masks: at the distances this camera occupies,
three controlled A/Bs measured the entire normal-map change at 0.08-0.29% of
pixels, while a bright POINT reads at any distance because it is contrast rather
than detail. Authored light also has no UV dependency, so unlike a cut mask it
survives every re-bake - and three separate attempts to key lit panels out of the
refreshed albedos failed because the paint does not contain them.

Tuned once already: at 150 cm the lamp cleared the roof line of the short
buildings and washed their roofs flat, since an opaque hull does not transmit
light. 55 cm reads as spill at ground level. The white roof patches on IceDrill
and ComputeModule are their OWN authored emissive from the earlier glow pass -
present in both frames, not from this lamp.

Verdict wanted: right / brighter / dimmer, and whether the accent tint should be
stronger than 40%.

## 5. Night, floodmast, glow dial → `qa-glow-ab.jpg`

`rh.Glow 1` over `rh.Glow 0` at night. At 1 the ComputeModule panels, the
forge's hazard band and the stockpile read lit; at 0 they go dark but the hulls
still read, because the `EmissiveFloor` ambient lift is deliberately not scaled
— at `rh.Glow 0` the colony stops advertising without disappearing.

The Floodmast is the pole with the warm pool beneath it: a real
`UPointLightComponent` (45000 lm, 26 m, warm, shadowless by choice) whose
emissive head matches the light, so the source you see is the source lighting
the ground. It sheds first when the grid browns out (LoadPriority 12).

## 6. Known limitations — do not bug-report these

- **All 7 crops are `SliceActive=FALSE`.** Nine finished crop assets are
  invisible in a default boot. Deliberate — a design gate, yours to flip.
- **`lab_full` and `workshop` have never been seen in a frame.** Both are
  Tier 3, `SliceActive=FALSE`, so `RH.Designate` furnishes nothing. Verified
  from their GLBs and UE read-back only.
- **ModularBlock & HeavyFreighter stay unwired** — they block nothing.
  ModularBlock needs a decision, not a repair: it is a *designed* open bay.
- **`chemtable_sm` is dead weight** — still the old 17,381-tri mesh, zero
  references in `Source/`, `docs/` or any CSV.
- **AirFilter, both Elevators, Dress clutter and the 20 RH_Colonist fallback
  statics remain unwelded** — their sources lived in a deleted staging
  directory. They render fine, just without that pass.
- Third-party notices exist in `docs/` but **do not ship**; that needs a
  credits screen or `NOTICES.txt`, which is a compile.

## Verdicts I need back

1. **Crew: pass / mesh-redo** (if redo: realistic vs stylized vs chunky). This
   one unblocks the most work.
2. **Mixed set**: SolarArray and HeavyForge especially — V2 or V0.
3. **Wear strength**: right / stronger / weaker (`RH.Wear` is a fleet knob;
   surface wear is baked, so this is a re-run not a re-bake).
4. **Pulse**: right / calmer / more (`RH.Pulse <scale>`, absolute).
5. **`rh.WalkerYawOffsetDeg`** winning value if facing is off.
6. **Crops**: flip `SliceActive` to TRUE, or keep dormant.
7. **Cutaway**: is mode 1 still the right default, and is 2 worth a UI control?
8. **Glow**: is `rh.Glow 1` the right authored level for night?
9. **ModularBlock**: repurpose as open-bay structure, or shelve.

Four of these (1, 2, and the stylization dial inside 1) are also the open
decision points in `premium-asset-plan.md` §10, so answering this card closes
those too.
