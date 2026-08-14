# W2 capture findings — 2026-08-14

Four boots of the real game, real Metal renderer, scripted camera, identical
snapshot times. Sheets in this folder.

## 1. The machines are buried under an unmasked emissive (NEW ROOT CAUSE)

`M_RH_Master` adds `EmissiveColor * EmissiveAmount` to the **entire** surface —
there is no mask, though the commandlet documents the parameter as "lit *area*
strength", so one was intended and never built. The glow colours are HDR
(`FurnaceGlow` = 6.0, 1.6, 0.15), so HeavyForge at `EmissiveAmount 0.22` emits
about 1.3 orange over every square centimetre and the albedo disappears.

Same mesh, same texture, same light, same sun angle:

| EmissiveAmount | what renders |
|---|---|
| 0.22 (shipped) | a flat saturated orange silhouette, no surface detail at all |
| 0.00 | rust-red panels, yellow-black hazard chevrons, weathered chimney, bolts |

**Every room prop was authored at 0.00.** That is exactly why the interiors
already read as finished while the machines did not — it was never the meshes.

This is the fourth independent root cause behind "models look weirdly
unfinished", after the skeletal usage flags, the Nanite fallbacks and the
unwelded meshes. It is also the one that most changes what to do next: the
painted models do NOT need redoing.

Fix: mask the emissive to windows, furnace mouths and indicator strips. Interim
knob, compile-free and reversible:

```bash
RH_EMISSIVE=0.0 UnrealEditor-Cmd <proj> -run=pythonscript \
  -script=$PWD/scripts/unreal/rh_tune_emissive.py -unattended -nosound -stdout
git checkout HEAD -- Content/     # restore the authored values
```

Caveat: 0.0 also switches off the powered/pulse read, which rides the same term.

## 2. Crew render correctly — verdict 1 is effectively answered

At magnification a walker is a properly textured EVA suit: white shell, dark
grey joints and boots, red chest marking, gold visor, correct stride. Not a grey
statue, no mitten hands, no lumpy silhouette. The skeletal-usage fix is
confirmed in pixels, not just in warning counts. **P2 does not need its planned
"different generator / retopo" scope.** A low-angle shot is still owed for a
proper silhouette read; this was top-down.

## 3. The regolith tiles, and macro variation does not fix it

Quantified on a 600x400 patch of bare ground by autocorrelation repeat-peak:

| | mean | repeat peak |
|---|---|---|
| shipped, t=35 s | 36.42 | 0.439 |
| shipped, t=70 s | 62.18 | 0.493 |
| macro variation added | 40.09 | 0.484 |

Multiplying the finished colour by low-frequency noise moved nothing; the fix
has to disturb the texture **lookup** (second sample at another scale, or
stochastic/hex tiling). Reverted — see commit 215b564.

Also note row 1 vs row 2: the same untouched material swings mean luminance
36 to 62 between t=35 s and t=70 s as the sun moves. **Any art A/B must pin the
snapshot time**, or it is measuring the time of day.

## 4. Smaller observations

- The floor-1 room floor reads blown out, near white, losing all colour.
- A translucent teal quad (room designation?) clips through props on floor -1.
- The Borer reads as a lumpy yellow blob at strategy distance.
- The default camera opens at 216 m, which is too far to see any of this. It is
  why none of these problems surfaced before.

---

# W3: the emissive mask — root cause 4 FIXED (2026-08-14, same day)

The scope approved after W2 is done, entirely compile-free.

## What changed

- `M_RH_Master` gained `EmissiveMask` (linear grayscale, defaults to the new
  8x8 white `T_RH_MaskWhite`, so unmasked = old behaviour; proven a rendering
  no-op at mean pixel diff 0.586 before any mask was assigned).
- 11 masks were cut from the models' own paint and live in
  `/Game/RedHope/Art/Masks/`. Sources of truth were the IMPORTED texture pixels
  (post AO/wear composite), exported and thresholded per model:
  Forge = ember hue (hazard chevrons excluded by hue window), battery = its
  teal/blue display panels (dozens of them), ComputeModule = dark screens,
  WaterPlant = small accents at full value plus the painted ice mass at 0.28
  (a low inner frost-glow), Borer/AirFilter/Humidity = size-windowed status
  dots and small screens, Habitat = its round portholes, blurred 5 px for the
  director's "warm and frosted" windows. SolarArray, Lander, Stockpile are
  deliberately dark (black mask AND amount 0) — the Lander's orange trim reads
  as paint, not lights, so no invented beacon.
- Post-mask strengths: Forge 2.2, ComputeModule 1.8, battery 1.6,
  Borer/AirFilter 1.5, Humidity 1.3, Habitat 1.0, WaterPlant 0.9.

## Verified with pixels

- Day (sheet 4): the forge keeps ALL its paint — rust panels, chevrons,
  weathered chimneys — with ember glints on top. Day-subtle, as directed.
- Night (sheet 5, sol fraction ~0.87 via RH.SetSpeed 8): the colony reads as a
  working settlement — battery = wall of teal readouts, forge = one warm lit
  window on a dark hull, Borer = amber cab lights, ice plant = cool blue.
- Pulse rides the masks: lit-pixel delta across a 3-frame night triplet is
  2.5–9.2 vs a ~0.5 TAA noise floor. Powered/dark still works — same term.

## Tuning knobs now live

- `scripts/unreal/rh_assign_masks.py` — per-model mask + strength table;
  edit and re-run.
- `scripts/unreal/gen` recipes in the session scratchpad were used to cut the
  masks; the mask PNGs themselves are re-cuttable from the exported albedos.
- ComputeModule's screens are thin (0.04 % coverage) and read faintly; loosen
  its recipe or raise its amount if it should read stronger.

## Still open (director)

- Wear strength / pulse strength / yaw verdicts from the QA card.
- The regolith tiling (root cause list, item 3) — needs a lookup-level fix.
- New scope requested 2026-08-14: Sims-style interior cutaways, player-placed
  lights, and a glow-toggle setting — see `docs/night-and-interiors-plan.md`.

---

# CORRECTION to commit 92be72c, and a hard blocker (2026-08-14, later)

## The correction: the props did NOT join the family

Commit 92be72c is titled "plus the props join the family" and claims all ten
Props2 instances were reparented onto `M_RH_Master` with masks assigned. **That
claim is false.** A six-agent audit caught it and I verified it independently:

- `MI_bunk` and its nine siblings exist ONLY at `Content/RedHope/Art/Props/<n>/`
  - the flat V1 lineage. `git show --stat 92be72c` lists exactly those paths and
  **zero** Props2 files.
- The game loads `Props2/<n>/StaticMeshes/<n>` (RoomPropPath), whose slot-0
  material is `Props2/<n>/Materials/Material`, still parented to
  `/InterchangeAssets/gltf/Substrate/M_GLTF`. No `MI_*` exists under Props2.
- Only one of the ten reparented instances is on a mesh anything loads
  (`Props/locker`, used as floor clutter).

Root cause: `rh_assign_masks.py` searched the asset registry for anything merely
NAMED `MI_<n>` and took the first hit. Two lineages share those names. The
lesson is in `rh_wire_props.py` now: address assets by FULL PATH, and read the
mesh slot back after assigning rather than trusting the write.

My "verified in a boot: screen strips now read" line was also wrong - I
over-read a blue tint in the capture that was the room-designation quad, not a
prop screen. The albedo half of that A/B was real; the emissive half was not.

## The fix, written and ready, blocked

`scripts/unreal/rh_wire_props.py` creates `MI_<n>` under Props2 parented to the
master, carries BaseTex AND the glTF metallic-roughness map across, assigns the
already-committed masks, and asserts mesh slot 0 afterwards.
`rh_author_master.py` gained the `MRTex`/`UseMRTex` path so joining the family
no longer costs per-pixel surface response (default 0 = every existing instance
is bit-identical).

Neither has been RUN, because:

## BLOCKER: the editor cannot start

`Binaries/Mac/libUnrealEditor-RedHope.dylib` is missing - the module manifest
still lists it, and it is nowhere on the volume. Every editor launch now dies
with "The game module 'RedHope' could not be found", which takes down the entire
headless Python lane and the capture harness with it. I could not determine what
removed it and will not guess.

The already-pending director compile restores it AND builds rh.Cutaway /
Floodmast / rh.Glow. Until then no asset work of any kind can run.

## Verified audit findings worth acting on after the compile

Beyond the prop wiring, 56 findings survived adversarial verification. The two
that change previously-held beliefs:

1. **"Unweldable because the source GLBs are gone" is FALSE.** UE 5.8 exports
   geometry back out headlessly (AssetExportTask + StaticMeshExporterFBX), and
   the FBX route returns already-welded data. The library-wide 3.00 verts/tri is
   **hard split normals, not duplicated positions** - so the repair is a normals
   operation (clear custom split normals, THEN smooth by angle), not a merge.
   This unlocks AirFilter, the Elevators, Dress clutter and the Tiers set.
2. **Three live room types furnish nothing.** WorkbenchLarge, ChemTableLarge and
   Infirmary are SliceActive in DT_Rooms and are designatable from the command
   deck, but have no RoomPropPath entry - so designating one silently STRIPS the
   cell's furniture and leaves bare deck, with no warning logged. Confirmed by
   pixel-diffing before/after captures.

Also verified: the hab floor texture is cracked white plaster at 0.471 linear
albedo (not clipped - an albedo/motif problem, so compile-free fixable), the
wall panel reads as bathroom tile at ~0.9 m quilt, and NO `_Normal` sibling
exists for any Surfaces/ texture, so every floor and wall in the colony is
shaded perfectly flat.
