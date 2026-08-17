# QA Boot Card — regenerated 2026-08-16 against `bccf0f0`

The previous card was written for `08080ce` and is **40 commits stale**. Since
it was written the regolith tiling was fixed at the lookup, all 9 crops were
remade, the TRELLIS.2 lane was stood up, 29 models were texture-audited, the
art pipeline was licence-audited, nvdiffrast was removed, and the Tiers
furniture set completed. This card describes what is actually in the build now.

**No compile needed.** `libUnrealEditor-RedHope.dylib` (Aug 14 18:16) is newer
than the last `Source/` commit (Aug 14 18:03); everything since has been
content-side. Battery 25/25 green at this commit, pins identical
(`gen 50 W load 20 W`, `Regolith_A: 40645 kg`).

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

**Still open, and it is a separate cause:** some residual white speckle remains
on a few suits, and that lives in the ALBEDO textures — Hunyuan paint-stage
artifacts, not a shading bug. It would need a re-paint, which is a GPU job.
So the shape question you were being asked to judge is now actually answerable;
the paint has one more pass owed on some crew.

What I see, for you to agree or overrule: suits now read opaque with legible
colour and trim — a white/orange EVA suit and a blue-trimmed lab coat are
clearly distinct. Silhouettes are legible as people.

**This gates P2 Characters (16–25 h, the long pole in the plan).** Pass and it
shrinks or disappears; fail and it is a *mesh* verdict — different generator or
retopo — because paint is no longer the problem.

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
