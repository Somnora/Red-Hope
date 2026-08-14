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
