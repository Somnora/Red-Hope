# Underground Habitation — Design + Architecture Spec

Status: **director-ruled 2026-07-05.** All five open questions answered; new systems folded in below (excavation-as-tool, oxygen-per-volume habitability gate, hydrogen-fuelled machines, the water/waste recycling chain, sliced-rock camera). Implementation deferred: Z-model → **M1-b**, radiation/flares → **M1-c**, the full vault + life-support → **M1-d**. Gate C finishes first.

Directive (director): underground habitation is explicit and core. Surface structures pay a radiation/shielding tax; the colony houses people underground — bored vertical shaft, habitable floors off a central spine, lift as connective element (*Fallout* vault / *Silo*). Mining borers double as habitat excavators. 5 subsurface floors to start, expandable as data. The Phase 1 exit habitat is substantially underground.

Canon anchor: brief §2 Phase 1 ("excavate the land and begin creating a shelter"), §4 (solar flares / radiation), §5 (Shielding, Hydrogen-as-fuel). Makes an implicit line explicit; invents no canon.

---

## 0. The load-bearing insight

**Underground needs no surface terrain.** The shaft goes *down* from a flat pad — no heightmap, no slopes. So **flat-terrain-until-M2 stands unchanged** and is in fact easier here. The 3D this adds is vertical *interior* floors — a coordinate change, not a terrain change. The two stay separate.

**And the core loop of the whole feature (director's ruling):** *excavation is cheap and player-directed; habitability is the constraint.* You can dig as wide and as deep as you like — the challenge is whether you've supplied the oxygen, shielding, power, circulation, and (Phase 2) water/waste processing to make what you dug actually livable. Digging a cavern you can't pressurize just gives you a spacesuit-only void.

---

## 1. Z-model — discrete floors in the grid  *(implement in M1-b, ruling #3)*

**`(X, Y, Level)`, signed integer.** 0 = surface; −1…−5 subsurface; `MaxDepth` config caps it (5 to start, data-expandable).

- `FRHBuildingInstance` / `FRHDepositState` gain `int32 Level`; `LocationCm.Z` is *derived* (`Level × FloorHeightCm`, ~400 cm) for presentation only. Sim reasons in floors, never continuous depth.
- Storage unchanged (sparse `TArray`s, already position-keyed, not a fixed 2D array); queries filter by `Level`.
- Coverage / hauling / task-target / placement become **per-level 2D**, with the shaft the only cross-level connector (§5). Duplicating the plane downward, not inventing 3D geometry.
- **Determinism & headless integrator: no threat.** `Level` is an integer coordinate (order-deterministic); vertical motion is discrete shaft segments (no float 3D pathing); era mode integrates aggregate rates regardless of where producers sit. Five floors = more ledger rows.

Lands in **M1-b** (front), before fleet realism bakes in 2D assumptions — Gate C ships clean first.

---

## 2. Excavation — the Borer as a tool, not a job  *(ruling #1 + #2)*

**You build a Borer once (a capability unlock), then boring is a player-directed designation tool — the Sims terrain tool.** Paint the volume you want carved on the active floor; the borer works it down over time. This reconciles "building vs robot": the Borer *building/unlock* grants the capability; excavation is a *designation action* (we already have the click-to-designate pattern from digging).

**Two gates on how big/fast you dig — both about supply, not permission:**

1. **Power to run the borer.** Heavy draw. Which introduces the director's new mechanic —
   **Hydrogen-fuelled machines.** The Borer (and later heavy tools) can run on **H₂ instead of grid power**, conserving batteries for everything else. This closes a loop already in the data: the Electrolyzer's H₂ (byproduct of making breathable O₂) becomes *fuel*. The same ISRU chain that pressurizes your vault also powers the machine that digs it. Sets up the brief's "hydrogen rovers exceed battery range." Data: heavy machines gain a `FuelType` (Grid | Hydrogen) and an H₂ burn rate; H₂ is scarce (~1.1 kg/sol-hr per Electrolyzer), so you ration it — strategic, not free.

2. **Oxygen to fill what you carve (the habitability gate).** Every 10×10 cell of excavated volume requires an O₂ "fill" to be rated livable; bore a giant space without the O₂ extraction to match and it stays spacesuit-only. This is what makes the Phase 1 exit ("*oxygenated* habitat") a real, *scaling* cost — see §3.

**The spoil loop survives intact — "digging your home makes your building material."** Boring emits regolith spoil (extraction recipe, `DrillIce` pattern), hauled to the Forge like any output. Against existing Forge numbers:

| Quantity | Value | Source |
|---|---|---|
| Forge full duty | ~1,200 kg regolith → ~300 kg Struct / sol | existing recipes |
| Borer spoil | ~1,200 kg regolith per 10×10×1-floor carved | proposed, data |
| A 10×10 floor build-out | ~300–400 kg Struct | proposed, data |

The spoil from carving a floor ≈ the Struct to build it out. A near-self-paying loop, every number a CSV row; **scales with player ambition** — carve a 40×40 hall and you've got 16× the spoil *and* 16× the O₂/shielding/Struct demand.

---

## 3. Habitability chain — what "livable" costs  *(ruling #4)*

A carved floor is not a home until it's **bored → shielded → oxygenated → circulated** (and Phase 2: **plumbed**). Each is a data-driven cost that scales with the volume you carved:

- **Shielding** — per-floor, from depth vs. the surface tax. Deep floors get radiation protection *free* (overburden). Surface structures pay a `Shielding` build tax (the dormant Forge product) proportional to exposure. **Radiation model (lightest viable):** a depth-indexed `RadiationLevel` scalar (surface high, each floor ≈ 0). **Solar flares (brief §4)** spike surface exposure — unshielded surface robots take wear, Phase 2 humans must shelter, underground is immune. *A flare is the moment you're glad you dug.* Flares generalize the M1-c storm system (one `RH_Events` table: dust storms + flares).
- **Oxygen fill + circulation** — the §2 gate: O₂ mass per carved volume to pressurize, plus an ongoing top-up rate for leakage. Distributed down the shaft trunk (§5). Sketch: a 10×10 floor ≈ ~100 kg O₂ fill (≈2 sols of one Electrolyzer); a 40×40 hall ≈ ~1,600 kg — now you need serious ISRU or it stays a suit-only void. All data.
- **Water + waste recycling (Phase 2 loop; infrastructure buildable in M1-d)** — the director's closed-loop life support:
  - **Plumbing → treatment area.** Urine → separated into **urea** (building feedstock / plant nutrient) and **water** (plants + cleaning; *potability degrades with each recycle pass* — you can only re-drink filtered urine so many times, so fresh water still matters).
  - **Feces → treated → fertilizer** (feeds the Phase 2 greenhouse chain).
  - This is a Phase 2 *active* loop (needs humans producing waste), but its **facilities are buildable in the Phase 1 exit** (you prep the vault for the crew). Architecture note in §7.

**The strategic spine the director wants:** build your underground city as large as you like, but a big build-all-at-once demands the plan and the stockpile — enough Struct, O₂, shielding, and power/H₂ staged before you carve, or you've dug a tomb. Ambition is ungated; *readiness* is the game.

---

## 4. Camera — the sliced-ant-farm view  *(ruling #5)*

**Each floor renders as a horizontal slice, viewed from above, with the surrounding un-excavated rock reading as a cut/sliced slab** — an ant farm cleaved with a sword and looked down into. The carved pocket is open, walls of solid sectioned rock around it, floor visible below.

Mechanics that produce this inside our orbital-to-ground zoom (camera pillar preserved — no mode-break):
- **Elevator-panel selector** on the command deck (`SURF · −1 … −5`) — the lift *is* the navigation, diegetically exact.
- Selecting a floor makes it the **interaction plane**: full-opacity, and the camera's focus depth drops to it (ground-zoom on −3 puts you inside −3). Floors *above* the active one fade so you look down into it.
- **The un-excavated volume renders as solid rock with a cut top face at the floor's ceiling** — that's the sliced-slab read. Excavated cells are the open pocket. So the boundary between "carved" and "rock" is legible from directly above.
- **Shaft-section HUD widget** — a vertical strip: the 5 floors stacked, lift position, each floor's state (bored / shielded / oxygenated / built / sealed). Constant vertical orientation as a cheap Slate panel — the map beside the sliced territory.

Rejected (stated for the record): full ONI side-elevation (breaks the zoom pillar), X-ray transparency (incoherent past ~3 floors, worse at gray-box fidelity).

---

## 5. Territory & infrastructure — power=territory in 3D

**The shaft is a vertical trunk / pylon.** Surface coverage doesn't teleport through rock; the shaft carries a **power + O₂/air + (Phase 2) plumbing trunk down its spine** as it's bored. A floor joins the grid when the trunk reaches its depth (bored + spine extended). For the 5-floor starter a floor is small enough that the shaft tap covers it whole; large floors can later need a local distribution node (data headroom). Air/O₂ and circulation run the same spine into the sealed floors. "Power grid = territory" generalizes: the shaft is a pylon pointing down.

---

## 6. Milestone placement  *(ruling #3 confirmed)*

- **M1-a (now):** finish Gate C. No underground work.
- **M1-b:** fleet realism (wear/repair/scout) + **the Z-model coordinate** (front of stage) so everything after is 3D-native. Section-widget stub. No gameplay underground yet — surface plays identically, everything just knows `Level`.
- **M1-c:** dust storms + **radiation + solar flares + the surface shielding tax** — the player feels the surface cost and dreads a flare *before* the habitat payoff.
- **M1-d (Phase 1 exit):** the Borer tool, hydrogen fuel, the spoil→Forge loop, per-floor bore/shield/oxygenate/circulate, the sliced-rock camera, buildable (dormant) water/waste facilities, and **the underground habitat as the exit condition** — a bored, shielded, sealed, trunk-powered, O₂-fed vault rated for the first crew. Verified by a hand-played run from fresh landing to that underground exit card.

---

## 7. Conflicts & architecture flags

- **Flat-terrain-until-M2 stands** (§0). No revisit.
- **Logged M1-d habitat chain is revised:** the old "Ore→Shielding→HabSegment→Habitat on the surface" becomes underground floor build-out. **Shielding's primary role flips to the surface radiation tax**; the `Habitat` building becomes a per-floor build-out, not a surface box; Borer + spoil + O₂-fill + H₂-fuel are net-new. Logged.
- **Some Phase-2 life-support architecture is pulled forward as hooks:** oxygen-per-volume demand, plumbing/treatment *facility* placement, and the water/waste resource types want to exist (dormant) by M1-d so Phase 2 activates content, not schema — same discipline as the M0 dormant rows. The *active* recycling loop is M2.
- **New resource/mechanic surface area (all data):** `FuelType` on machines + H₂ burn rates; per-volume O₂ demand + circulation top-up; `Urea`, `Fertilizer`, graywater potability tiers; per-level `RadiationLevel`; `FloorHeightMeters`. None threaten determinism or the era integrator (all are consumption/production rates or integer coordinates).
- **Deposits stay surface-accessed for Phase 1;** floors intersecting deposits is rich post-M1 headroom, flagged not scheduled.

---

## 8. Rulings on record (2026-07-05)

1. **Borer = tool you own after building a Borer** (Sims terrain-tool), free-form excavation; gated by power and by **oxygen-per-carved-volume** for livability. → §2.
2. **Floor size is player-defined**, gated by battery/**hydrogen** to power the borer; hydrogen machines conserve batteries. → §2.
3. **Z-model → M1-b**, so Gate C finishes now. → §6.
4. **Bore + shield + oxygenate/circulate per floor before livable**, plus the **water/waste recycling chain** (urine → urea + graywater with degrading potability; feces → fertilizer); build as large as you want but stage the plan + stockpile. → §3.
5. **Sliced-ant-farm camera:** per-floor slice from above, surrounding rock as a cut slab. → §4.
