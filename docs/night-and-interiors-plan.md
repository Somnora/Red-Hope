# Night, lights, and interiors — scoped plan (2026-08-14)

Director asks from the emissive-mask review, scoped against what the code
actually has. Three features, one compile between them.

## 1. Sims-style interior viewing

**Ground truth first.** The game's inhabited spaces are the UNDERGROUND floors:
carved cells, rock walls lining the pit, props furnishing the rooms. That is
where Sims-style viewing pays off, and half of it already exists — the elevator
slice view IS "no roof": riding down opens the active floor from above, surface
dressing hides, and the pit renders open-topped. The surface buildings
(HabitatDome and friends) are single hull meshes with **no interior geometry at
all**, so "see through the hab's windows" cannot be a camera mode today. The
warm frosted portholes that shipped with the mask pass are exactly the honest
version of that ask: the hab reads inhabited without interior art. Modelled
surface interiors would be a P3-scale art decision, priced separately if wanted.

**What gets built now** — a view-mode dial for the underground, Sims-style:

```
rh.Cutaway 0   exterior: walls + roofline as today
rh.Cutaway 1   no-roof: today's slice view (current behaviour, now nameable)
rh.Cutaway 2   three-walls: the wall strips on the CAMERA side hide; orbiting
               the camera swaps which walls hide, exactly like The Sims
rh.Cutaway 3   floorplan: all wall faces hidden, furniture and crew only
```

Implementation: the pit walls are already state-diffed segments in the colony
visualizer (`UpdateShaftVisuals`); mode 2 adds a camera-yaw test per wall strip
(hide the two of four sides facing the camera, hysteresis so orbiting does not
flicker), mode 3 hides all four. Keyboard cycle on a single key plus the CVar.
C++ in the RedHope module only; the sim never knows. **Effort 3–5 h, one
compile.**

## 2. Player-placed lights (the pride ask)

A new buildable, working name **Floodmast**: a cheap surface/underground light
tower the player places to light their city because they built it, not because
the UI glows. Proposed stats (director may reprice): cost 40 Struct, draw 150 W,
no jobs, buildable on surface and carved floors.

- Sim side is pure data: one row in `RH_Buildings.csv` + headless DT re-import
  (the proven lane; pure-data verifiers keep CSV and DT locked together). A new
  boolean column `EmitsLight` rides the row — data, so the RedHopeSim module
  stays clean of rendering.
- Presentation: the visualizer reads `EmitsLight` and attaches a real
  `UPointLightComponent` (warm, ~2500 lm-equivalent, 12 m radius, shadowless
  for perf) plus a small emissive head on the mast. Light output rides
  `bPowered` like everything else: a blacked-out district goes dark — storms
  and brownouts stay legible for free.
- The existing battery baseline is untouched: no self-test builds one.

**Effort 2–4 h + the same compile as feature 1.**

## 3. The glow dial (night glow you can turn off)

The masked machine glow is the default night readability layer. Once a player
has laid out real Floodmasts, they should be able to dim or kill it and let
their own lighting carry the night — the pride toggle.

One global scalar, `GlowScale`, in the material parameter collection the
atmosphere subsystem already writes every tick (it pushes `TimeOfSol` there
now). The master material multiplies its masked glow by it — a compile-free
graph edit via `rh_author_master.py`. The C++ half is three lines in the
atmosphere tick pushing a `rh.Glow` CVar into the collection, bundled into the
same compile as features 1–2. Modes:

```
rh.Glow 1      always as authored (default)
rh.Glow 0      machine glow off — Floodmasts and windows carry the night
rh.Glow 0..1   anything between
```

A later settings-menu slider and an "auto: glow only at night" curve (the
material already receives TimeOfSol, so day-dimming is free) hang off the same
scalar; UI is deferred with the rest of the UI pass pending Gate-D.

## Order and the one compile

1. Python-side prep (no compile): MPC gains `GlowScale`; master graph gains the
   multiply; both no-ops until the CVar exists.
2. One C++ pass: cutaway modes + `EmitsLight` point light + `rh.Glow` push.
   Director compiles once.
3. Data: Floodmast CSV row + DT sync + battery re-baseline check (must be
   byte-identical — nothing tests the new row).
4. Capture verification: cutaway orbit shots, a night city lit by Floodmasts
   with `rh.Glow 0`, the same with `rh.Glow 1`, before/after sheets.

Open pricing decisions for the director: Floodmast cost/draw, default `rh.Glow`
value, and whether mode 2's hysteresis angle feels right (tuned live via CVar).


---

# Execution log (2026-08-14)

## Done, compile-free

- `MPC_Atmosphere` gained the `GlowScale` scalar (default 1.0) via
  `scripts/unreal/rh_add_mpc_glow.py`. Params are now
  Habitability, TimeOfSol, DustAmount, GlowScale.
- `M_RH_Master` multiplies its MASKED emissive by that collection parameter
  (`rh_author_master.py`, 49 expressions, 0 compile errors). The
  `EmissiveFloor` ambient lift is deliberately NOT scaled - at `rh.Glow 0` the
  hulls still read, they just stop advertising.
- `Floodmast` row added to `docs/data/RH_Buildings.csv` and DT_Buildings synced
  headless (16 rows) with the new `scripts/unreal/rh_sync_datatables.py`.
  Row: 1x1 footprint, 150 W draw, 5 W idle, 40 Struct, 60 s build,
  LoadPriority 12 (**shed first** - when the grid browns out the decorative
  lights go before life support, which is both correct and dramatic).

## Written, awaiting the single director compile

- `rh.Cutaway 0|1|2` - all walls / drop the faces toward the camera (swapping
  as you orbit, with hysteresis so a boundary crossing cannot strobe) /
  floorplan. WallISM became a filtered view of an authored face list; wall
  vents hide with the wall they are mounted on.
- Floodmast carries a REAL `UPointLightComponent` (45000 lm, 26 m radius, warm
  1.0/0.86/0.66, shadowless by choice) plus a mast silhouette whose emissive
  heads match the light, so the source you see is the source that lights the
  ground. Visibility rides `bPowered`.
- `rh.Glow <0..1>` pushes GlowScale every atmosphere tick.
- **Bug found and fixed in passing:** the atmosphere pushed a scalar named
  `Dust`, but the collection declares `DustAmount`. That write had been going
  nowhere. Renamed.

## Deliberately NOT built

- Surface-hab interiors. Those meshes are hulls with no interior geometry, so a
  "look inside" mode has nothing to show. The warm frosted portholes from the
  mask pass are the honest version of that read. Modelled interiors are a
  separate art decision, not a view mode.
- A roof-lift mode. The pit is an open excavation; there is no roof to lift.
  What actually occludes an interior there is the near wall, which is what
  mode 1 drops.
