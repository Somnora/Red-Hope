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

---

# W5: everything verified on the new binary (2026-08-14, evening)

The compile landed (after one -Wshadow error, mine, fixed) and restored
libUnrealEditor-RedHope.dylib - which also solved the missing-dylib mystery:
an earlier 12:59 build attempt had compiled the atmosphere file, hit the same
shadow error on the visualizer, and its half-finished link left no module
binary. Nothing mysterious deleted it.

## The props are ACTUALLY wired now, and the proof is causal

`rh_wire_props.py` created MI_<n> under Props2, carried BaseTex AND the glTF
metallic-roughness map (master gained MRTex/UseMRTex), assigned the masks, and
read the mesh slot back. The read-back caught a second silent failure on the
first run: get/set of static_materials returns COPIES, so the slot write never
stuck; set_material() is the real setter. All ten now read slot0 = MI_<n>.

Proof the glow is real this time, not an eyeball: two identical boots differing
ONLY in `rh.Glow 1` vs `rh.Glow 0`, same camera, same snapshot second. The diff
is 0.480% of the frame in 38 distinct clusters, and the changed pixels sit ON
the furniture (labbench edge strips, galley, bunks, tank, console desk) plus
two walker-position differences. That one test verifies the prop wiring, the
masks, the MPC GlowScale plumbing, and the new rh.Glow CVar end to end.

## Floodmast: verified in the game, at night

RH.Spawn Floodmast landed the DT row through the real sim ("Built Floodmast #5
at (18, 4) m"), the mast silhouette renders with its two lit heads, and the
point light throws a warm ~20 m pool on the regolith with the Lander catching
the edge. sheet: Saved/RHCapture/shots/w5_floodmast_00.png.

## Cutaway: verified at two yaws + floorplan

docs/qa/2026-08-14/6-cutaway-modes.png. Camera south: near walls drop. Camera
north: the OPPOSITE walls drop. Mode 2: no walls. The swap follows the camera
with no visible artefacts at either yaw.

## Still open for the next pass (from the 56-finding audit)

1. Hab floor/wall re-skin + _Normal maps for Surfaces/ - the biggest remaining
   interior lever, compile-free.
2. RoomPropPath entries for WorkbenchLarge / ChemTableLarge / Infirmary (C++,
   small; next compile).
3. The label-Z fix (14 -> 25 cm, C++, one constant; next compile).
4. Split-normals weld pass over AirFilter / Elevators / Dress / Tiers via the
   FBX export lane.
5. The regolith tiling (lookup-level fix, art decision).

---

# W6: the director's four interior complaints, root-caused and fixed (2026-08-14, night)

Complaints from the live review of the cutaway sheets, in his words: materials
look jank; the floor is missing around the elevator; you cannot see the floor
underneath a lot of the desks; the elevator doors open strangely outside of
its body.

## 1. "Materials look jank" -> the surfaces were re-skinned (DONE, verified)

New procedural T_HabFloor_Sealed (2 m panel grid, mean linear albedo 0.294 vs
the old plaster's 0.471), T_HabFloor_Deck (1 m plates + tread studs), and
T_HabWall_Panel (vertical insulated panels + bolts), each with a height-derived
_Normal sibling imported as TC_Normalmap - so ApplySurface's Foo_Normal
convention lights them automatically, and every floor and wall in the colony is
shaded for the first time. Verified in pixels at the same framing he critiqued.

## 2. "Cannot see the floor under the desks" -> baked plates, CUT (DONE, verified)

The grey slabs were IN THE PROP MESHES - the generator bakes a ground slab
under furniture, and the Aug-14 weld pass reimported from the with-plate
sources, resurrecting plates the July "plate-free re-export" had removed.
scripts/blender/rh_cut_plate.py detects the plate STRUCTURALLY (a connected
component that is razor-flat, footprint-wide and bottom-flush - a blind z-cut
would have taken chair legs with it) and deletes it. 7 of 10 props carried
plates and were cut; conduit / planter_wet / tank genuinely had none (largest
bottom-flush slab: 4.8 % of footprint) and were passed through untouched.
Reimported in place, slots re-asserted, verified: furniture stands on the deck.

## 3. "Floor missing around the elevator" -> C++, awaiting compile

The shaft-head cell deliberately stayed bare dirt ("leave it bare rock") while
every neighbour got bright deck - on his screen that read as a hole. It now
wears dark deck plating like the rest of the floor.

## 4. "Doors open outside the body" -> C++, awaiting compile

Door travel was a fixed 26->94 cm throw regardless of cage size, so parted
panels overshot the cage and hung in the air. Travel is now derived from the
cage's own bounds (outer edge stops at the cage side, panels pocket into the
frame), the same way DoorFaceX already was.

Also riding that compile: the tier rooms (WorkbenchLarge / ChemTableLarge /
Infirmary / LabFull / Workshop) get RoomPropPath entries plus a Function-field
fall-through so a future data-added room can never silently strip a cell
again, and the room-tile label lifts from +14 to +25 so it clears the deck.
