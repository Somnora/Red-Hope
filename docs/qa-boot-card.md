# QA Boot Card — 2026-08-14 art overhaul

One live boot, judged in this order. Everything below shipped since your last
boot (`08080ce`): P0 + P1 of `docs/premium-asset-plan.md`, all pushed through
this card's commit. **No compile needed** — the binaries you built this morning
are current; every change since has been content-side.

## Boot sequence

```
RH.Demo
RH.ActivateCrop all
```

Then judge each item. Where a knob exists, it's listed.

## 1. Characters — THE headline check

Every walker should now be **textured** — colored suits, faces, per-crew looks —
instead of uniform grey. All 21 were rendering UE's *default material* until
today: their materials descended from Interchange's `M_GLTF`, which lacks the
skeletal-mesh usage flag, so the renderer refused the material outright. The
textures were always there and never reached the screen.

**Judge the crew AFTER this fix, and only then decide P2**: if they now read as
acceptable, the expensive character redo shrinks or disappears. If the shapes
still miss (lumpy silhouettes, mitten hands), that's a *mesh* verdict —
different generator or retopo — because paint is no longer the problem.

- Facing: walkers should face travel (`rh.WalkerYawOffsetDeg` is live if not;
  report the value that looks right).

## 2. Buildings — the mixed set

- `rh.ModelSetV2 1` (default): painted Forge / Habitat / ComputeModule /
  SolarArray; original BatteryBank / Borer / WaterPlant / Lander / Stockpile.
  `rh.ModelSetV2 0` + `RH.Demo` again = all-originals for comparison.
- **Full geometry**: 109 meshes were drawing their Nanite fallback proxy
  (HabitatDome at 22 % of its triangles). Everything now draws real geometry.
- **Welded shading**: curved hulls should read smooth, not faceted.
- **Surface depth**: AO in recesses, lightened worn edges, cavity grime — most
  visible on Borer treads, IceProcessor pipework, HeavyForge. It's deliberately
  subtle; say if you want it stronger or weaker (it's a re-run, not a re-bake).
- **One material family**: the 11 wired models share `M_RH_Master` — the colony
  should read as one kit. `MI_<name>` instances carry each machine's function
  accent (art-bible colors).

## 3. "Is it working" — power + pulse

- Machines carry a subtle emissive pulse (forge: slow throb; ComputeModule:
  fast blink; battery/ice/borer/filters: gentle breath). Habitat, Lander,
  Stockpile, Solar are deliberately still.
- **Kill power to something** (let the battery drain, or switch a building off):
  it should go **dark and still** — the pulse rides through `PoweredState`.
  Power restored → glow returns. This is the sim's own `bPowered`, visualized.

## 4. Rooms, gardens, crops

- Room props (bunk, console, galley, lab bench, planters, tank…) are welded +
  AO/wear textured. Interiors should feel less flat.
- Crops and trees got *gentle* treatment (age, not damage). Stage swaps as
  before; `RH.Climate` / `RH.Duct` unchanged.

## 5. Known limitations — do not bug-report these

- **AirFilter, both Elevator meshes, Dress clutter (crate/drum/vent), and the
  20 RH_Colonist fallback statics remain unwelded** — their source files lived
  in a deleted staging directory and cannot be re-cut. They render fine, just
  without this pass's improvements.
- **ModularBlock & HeavyFreighter stay unwired** (they block nothing).
  ModularBlock needs a decision, not a repair: it's a *designed* open bay —
  usable as a vehicle-bay/workshop-type structure if you want it.
- Orphan sets exist under `Art/` (Struct ×10, Tiers ×6, Garden ×19 dupes,
  Furnish, ceilinglight, 9 flat Props) — imported but referenced by nothing.
  Cleanup is a separate decision; nothing renders from them.
- Robots kept their look (scoped out by you); the robot walker did gain its
  texture back via the same skeletal-flag fix.

## Verdicts I need back

1. Crew: pass / mesh-redo (and if redo: realistic vs stylized vs chunky).
2. Mixed set: per-building thumbs, esp. SolarArray old-vs-new.
3. Wear strength: right / stronger / weaker.
4. Pulse: right / calmer / more.
5. `rh.WalkerYawOffsetDeg` winning value if facing is off.
6. ModularBlock: repurpose as open-bay structure, or shelve.
