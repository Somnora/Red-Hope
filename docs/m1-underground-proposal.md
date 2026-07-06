# Underground Habitation — Design + Architecture Impact Proposal

Status: **agent-proposed, awaiting director ruling.** No implementation begun.
Directive (director, 2026-07-05): underground habitation is now explicit and core. Surface structures pay a radiation/shielding tax; underground is how the colony houses people. Bored vertical shaft, habitable floors off a central spine, lift as connective element (Fallout vault / *Silo* language). Mining borers double as habitat excavators. Target: 5 subsurface floors, expandable as data. The Phase 1 exit habitat is assumed substantially underground.

Canon anchor: brief §2 Phase 1 ("excavate the land and begin creating a shelter"), §4 (solar flares / radiation as threats), §5 (Shielding as a resource). This makes an implicit line explicit; it does not invent canon.

---

## 0. The load-bearing insight (read this first)

**Underground does not require surface terrain.** The shaft goes *down* from a flat pad; it needs no heightmap, no slopes, no M2 terrain work. So the logged "flat-terrain-until-M2" decision **stands unchanged** — and is in fact easier here (no slope handling at the shaft head). The 3D this feature adds is *vertical interior floors*, which is a grid/coordinate change, not a terrain change. These are separable, and we should keep them separate.

The one thing that genuinely wants to move earlier is the **Z coordinate itself** — see §6.

---

## 1. Z-model — discrete floors in the grid

**Model: `(X, Y, Level)` where `Level` is a signed integer.** 0 = surface; −1…−5 = subsurface floors; the schema imposes no floor limit (a `MaxDepth` config row caps it; 5 to start).

- **Sim change:** `FRHBuildingInstance` and `FRHDepositState` gain `int32 Level`. `LocationCm.Z` is *derived* for presentation as `Level × FloorHeightCm` (~400 cm); the sim reasons in `Level`, never in continuous depth. This keeps every vertical quantity discrete.
- **Storage:** no change to how state is stored — buildings/deposits are already sparse `TArray`s keyed by world position, not a fixed 2D array. Queries simply filter by `Level`. (The architecture proposal's "sparse world-space structures, sim never knows how big the world is" claim already covers a third axis.)
- **Coverage / hauling / task-target / placement** all become *per-level 2D* operations, with the shaft as the only cross-level connector (§5). Horizontal reasoning on a floor is identical to today's surface logic — we are duplicating the plane downward, not inventing 3D geometry.

**Determinism & the headless integrator — no threat, and here's why precisely:**
- The fixed timestep is untouched; `Level` is just another integer coordinate, and integer-keyed iteration stays order-deterministic.
- Vertical motion is **discrete** (shaft segments / lift stops), never floating-point 3D pathing — so no new nondeterminism enters movement.
- Era mode already integrates an *aggregate ledger* of producer/consumer rates and does not care where a producer physically sits. Five floors of producers are just more rows in the same ledger. The paired-run acceptance bar is unaffected.

**Net:** the Z-model is a coordinate addition and a set of "same-level" filters, not a re-architecture. Cheap now, expensive if retrofitted later (§6).

---

## 2. Excavation as a production chain — the spoil loop

**Borer = placed building, not a robot** (recommended). A shaft is a fixed-location, persistent, floor-unlocking operation; a roving robot task models it badly. Proposal: a **Tunnel Borer** building placed at the shaft head. It shares the "excavation" tech/visual family with the surface excavator robots (satisfying "one tool, two jobs" thematically — same machine lineage, two form factors). *Alternative if you prefer the fleet-unit read: a heavy Borer robot permanently assigned to the shaft. I recommend the building; flagging the fork.*

**The spoil loop — "digging your home produces your building material."** Boring a floor is an extraction recipe (like the IceDrill pattern we already have): it consumes power + time and *emits spoil* — regolith — into a hopper, hauled to the Forge/Stockpile like any output. This is the loop you named, and it closes cleanly against the *existing* Forge numbers:

| Quantity | Value | Source |
|---|---|---|
| Forge, full duty | 100 kg regolith → 25 kg Struct / 2 sol-hr | existing `RH_Recipes` |
| → per sol (24 sol-hr) | eats ~1,200 kg regolith → makes ~300 kg Struct | derived |
| Tunnel Borer bore rate | ~1 floor / sol @ full power | **proposed, data** |
| Borer power draw | ~600 W (heavy; sheds early) | **proposed, data** |
| Spoil yield | ~1,200 kg regolith / floor | **proposed, data** |
| A habitat floor's build-out | ~300–400 kg Struct | **proposed, data** |

**Reading:** the spoil from boring floor N (~1,200 kg regolith → ~300 kg Struct) ≈ the Struct needed to build out floor N. The home you dig very nearly pays for its own construction — not *exactly* (that would remove the challenge), but a major offset, and every number above is a `RH_Buildings`/`RH_Recipes` row you can bend. New recipe `BoreFloor` (empty inputs, `Regolith:1200` output, `RequiresShaft`), mirroring `DrillIce`.

---

## 3. Radiation — the lightest model that makes the tradeoff real

Underground only matters if the surface *costs* something. Minimum viable model:

- **A per-level `RadiationLevel` scalar.** Surface (Level 0) = high; each level down ≈ 0 (regolith overburden shields GCR/UV — real physics, a few meters suffices). This is a config curve by depth, not a field simulation.
- **Surface human-structures carry a Shielding tax:** their build cost includes `Shielding` (the dormant Forge product) proportional to surface exposure. **Underground floors need zero shielding — depth is the shield.** That asymmetry *is* the mechanic: the same habitat volume is dramatically cheaper below ground.
- **Phase 1 (robots) barely feels it** in calm conditions — robots tolerate ambient radiation (at most a slow surface wear premium). The teeth are the events:
- **Solar flares (brief §4)** are spikes: during a flare, surface `RadiationLevel` multiplies; unshielded surface robots take wear damage; humans (Phase 2) must shelter. **Underground is immune.** A flare is the moment you're glad you dug — the visceral payoff that makes the whole feature legible. Flares generalize the storm-event system already proposed for M1-c: one `RH_Events` table carries dust storms *and* flares.

This is the whole model: one depth-indexed scalar, one derived surface build tax, one event type. It answers "why dig down" without a physics sim.

---

## 4. Camera & readability — the hard problem

We can't adopt ONI's flat side-view or Dwarf Fortress's pure 2D z-paging: they fight our orbital-to-ground zoom, and "the zoom is the emotional register" is a logged pillar. But we can borrow their *idea* — one legible slice at a time — inside our camera.

**Recommendation: active-level slicing, with an elevator-panel selector and a shaft-section HUD widget.**

1. **Level selector on the command deck** — literally an elevator panel: `SURF · −1 · −2 · −3 · −4 · −5`. This is thematically exact: the lift is the connective element you named, so the *UI to navigate floors is the lift itself*.
2. **The active level is the interaction plane.** Selecting a floor: (a) renders it at full opacity, (b) fades levels *above* it to low opacity / hides them, so from the orbital camera you look *down into* the active floor, and (c) drops the camera's focus Z to that floor's depth — so ground-level zoom on −3 puts you *inside* −3. **The camera model itself does not change** — same zoom curve, same orbit; only its focus depth and the opacity mask move. No jarring mode-switch.
3. **A shaft-section HUD widget** — a vertical strip showing the 5 floors as stacked cells, the lift's current position, and each floor's state (bored / building / sealed / powered). This gives the constant "where am I vertically, what's the whole column doing" read that ONI/DF get natively from 2D — but as a cheap Slate panel (we already build these), not a world-render mode. It's the map; the world is the territory.

Why this fits us and the alternatives don't:
- *Full cutaway/section camera (ONI-style):* clean vertical read but a hard camera break that violates the zoom pillar. Rejected as primary; the section *widget* captures its value without the break.
- *Transparent overburden / X-ray:* reads all floors at once but goes visually incoherent past 2–3 floors, and worse at gray-box fidelity. Rejected.

Net: elevator-panel navigation + focus-depth slicing + a section widget. Preserves the signature camera, makes the lift diegetic, solves vertical orientation with UI rather than a mode.

---

## 5. Territory & infrastructure — "power = territory" in 3D

Keep the pillar coherent, don't contradict it: **the shaft is a vertical trunk / pylon.**

- Surface grid coverage does **not** teleport through rock. Instead the shaft carries a **power + air trunk down its spine** as it's bored — the lift/spine is infrastructure, not just transport.
- A floor becomes powered/covered when the trunk reaches its depth (floor bored + spine extended). For the 5-floor starter, a floor is small enough that the shaft tap covers it whole — *bore + extend spine = floor is on the grid, drawing from the surface network.* (Large floors later can require a local distribution node; keep that as data headroom, not M1 scope.)
- **Oxygen** runs down the same trunk: surface-produced O₂ flows into the sealed floors via the spine. (Underground floors are sealed pressure volumes — the point of going under.) Air-as-distributed-network is mostly a Phase 2 concern; in Phase 1 the trunk simply carries it down.

So the rule generalizes rather than breaking: **power grid = territory, and the shaft is how territory goes vertical.** The shaft is a pylon that points down.

---

## 6. Milestone placement

Your instinct is right, with one carve-out — **split the Z-model from the feature**:

- **Z-model coordinate lands EARLY — before M1-b fleet realism.** Adding `Level` and making coverage/hauling/task-target/placement level-aware is small and contained. Doing it *first* means fleet realism, storms, and everything after are built 3D-native instead of baking in a 2D assumption we'd pay to unwind. This is exactly the risk you flagged. Proposal: a thin **M1-a addendum gate (or the front of M1-b)** — the coordinate + level-filtered rules + the section-widget stub, with zero gameplay yet (surface still plays identically, everything just knows about `Level`).
- **Radiation + solar flares land in M1-c** (environmental pressure), beside dust storms. The player must *feel* the surface tax and dread a flare *before* the habitat payoff asks them to dig — so the motivation exists before the answer.
- **The full underground feature — shaft, Tunnel Borer, spoil→Forge loop, floor construction, camera slicing, the subterranean habitat — is the Phase 1 exit stage (M1-d).** This feature *is* habitat construction. The exit condition ("sealed, powered, oxygenated habitat rated for the first human crew") becomes: a bored, sealed, trunk-powered, O₂-fed multi-floor vault. Verified, per the plan, by a hand-played run from fresh landing to that underground exit card.

Revised M1 shape: **M1-a** (Gate C finishing) + Z-model addendum → **M1-b** fleet realism (Z-aware) → **M1-c** storms + radiation + flares + surface shielding tax → **M1-d** the shaft, the borer, the spoil loop, the underground habitat, the exit.

---

## 7. Conflicts with logged decisions (flagged plainly)

- **Flat-terrain-until-M2: NO revisit needed.** Underground is orthogonal to surface terrain; the shaft needs no heightmap. Stated up front (§0) because you asked directly. The decision stands.
- **M1-d habitat chain (logged in the M1 scope proposal) CHANGES.** The logged exit was "Ore→Shielding→HabSegment→Habitat assembled on the surface." Under this directive: HabSegments go *underground*, **Shielding's primary role flips to the surface radiation tax** rather than the habitat's own hull, and the Borer + spoil loop is net-new. The `Habitat` building becomes a *floor build-out*, not a surface box. This is a real revision to a logged plan — calling it out explicitly for your ruling.
- **"Square grid, 2 m cells" — extended, not broken:** adds a Z cell (floor height ~4 m) as a data value. `CellSizeMeters` gains a `FloorHeightMeters` sibling.
- **"Power = territory" — extended, not broken** (§5, shaft as vertical trunk).
- **Deposits modeled at surface (Z=0):** Ice is "subsurface" in fiction but drilled from the surface today. Keep that for Phase 1. Floors eventually intersecting deposits is a rich future interaction — flagged as post-M1, not now.
- **Determinism / era integrator / headless proof:** no conflict (§1). Explicitly reassured because it's a standing order.

---

## 8. Open questions for your ruling

1. **Borer as building (recommended) vs. heavy robot** — §2 fork.
2. **Floor size** — I sketched ~10×10 cells (20×20 m) per floor; is a vault floor bigger/smaller in your mind? (drives spoil and build-cost numbers).
3. **Does the Z-model addendum go into M1-a (now) or the front of M1-b?** I lean M1-b-front so Gate C ships clean first — your call.
4. **Shielding's dual role** (surface hull tax *and* the old habitat-chain input) — confirm the flip in §7 is the intent.
5. Anything in §4 (camera) you want steered before it's the thing I build — you flagged it as the hardest, and it's the least reversible once players form habits.
