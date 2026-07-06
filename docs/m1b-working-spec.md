# M1-b Working Spec — "Fleet Reality + the Third Dimension"

Status: **agent-proposed, awaiting director review.** Nothing here is started.
Anchors: approved M1 scope (`docs/m1-scope-proposal.md` §6 M1-b), the underground rulings (`docs/m1-underground-proposal.md`, ruling #3: Z-model fronts this stage), the inspection-card commitment (playability strand), and the Gate C / demo-era findings log. No new canon.

## 0. What M1-b turns the game into

M1-a ended with a hand-playable colony on an infinite flat plane where every deposit is free knowledge, robots never tire, and a building tells you nothing when you click it. M1-b makes the world **finite in knowledge** (survey before you see), the fleet **mortal in capability** (wear, repair, spare parts as a strategic line), and the colony **legible to the player's cursor** (inspection card, fleet panel) — while quietly re-founding the coordinate system so everything after this stage is 3D-native for the underground milestone.

## 1. Gate plan (three compiles, each verified before the next)

- **Gate A — Z-model + placement honesty** (`RedHopeSim` schema + save version bump):
  the third dimension and the placement fixes land together because both touch `CanPlaceBuilding`.
- **Gate B — fleet reality** (`RedHopeSim` + StateTree states): wear/degrade/halt, RC-M repair loop, RC-S survey + deposit discovery, charging queue etiquette.
- **Gate C — the player's eyes** (`RedHope` only): inspection card, fleet panel, notice-channel unification, PAUSED treatment.

A before B (survey/repair reason about `Level`); C last so its panels read the finished state. Each gate ends in a scripted SIE verification; the stage ends in a short hand-played director run (the M1-a cadence).

## 2. Gate A — Z-model coordinate (underground ruling #3)

Per the approved underground spec §1 — this stage lays the coordinate, not the gameplay:

- `(X, Y, Level)` signed-integer grid. `FRHBuildingInstance`, `FRHDepositState`, task targets, and robot positions gain `int32 Level` (0 = surface; −1…−`MaxDepth` reserved). `LocationCm.Z` becomes *derived* presentation (`Level × FloorHeightCm` config row, ~400).
- Coverage, hauling, task claiming, placement, and deposit queries become **per-level 2D** (filter by `Level`); no cross-level connector exists yet — that's the M1-d shaft. Surface play is bit-identical: everything sits at Level 0.
- Save format version bumps; loads of v1 saves refuse loudly per the header contract.
- **Shaft-section HUD stub** (underground spec §4): the deck gains the vertical floor-strip widget showing `SURF` only — the diegetic promise of what's coming, and the panel the M1-d selector will inhabit.
- **Verify:** `BP_M0cTest` parity re-run (quota sol, ship sol, end card word-identical to the Gate B baseline) + save→load→identical-ledger with the new field. The whole point is that nothing observable changes.

## 3. Gate A — placement honesty (the demo-era gap + Gate C findings)

- **Footprint/overlap validation:** `CanPlaceBuilding` and the uplink-arrival re-check both reject a footprint that intersects any existing instance or open site on the same `Level` (the reel + showcase Electrolyzers interpenetrating is the logged exhibit). Data already carries `FootprintX/Y`; this is a rectangle test against position-keyed state, deterministic, no physics.
- Rejection reason strings stay sim-authored (they now surface via the deck notice line, proven in `924e1df`).

## 4. Gate B — fleet reality (M1 scope P1, verbatim where possible)

- **Wear:** robots accrue `WearPerActiveSol` (already in `RH_Robots`) while working; past `WearDegradeThreshold` (50) work rate degrades linearly; at `WearHaltThreshold` (100) the robot halts in place — repairable, never destroyed. Wear serializes (save v-bump already paid in Gate A).
- **Repair:** the idle RC-M gains its StateTree **Repair** state — seeks the most-worn degraded/halted robot, travels, spends `SpareParts` at 25 wear/part from colony stock. PartsCrate (verified arriving in the reel's manifest) becomes a real strategic line.
- **Survey:** the idle RC-S gains its **Survey** state — survey designations cross the uplink like any order; execution reveals `SurfaceVisible=FALSE` deposits within `WorkRate` (60 m) of the surveyed point. **Ice_A flips to hidden** in `RH_Deposits`: the brief's "first go-look moment" — the ISRU chain now *costs a decision*. Deposit visuals spawn on discovery, not BeginPlay (and the queued `GB_Dep_*` cleanup lands here).
- **Charging queue etiquette** (M0-c backlog): pads gain a claim queue so two hungry robots stop thrashing one pad; Charge-state refinement, no new schema.
- **Verify:** the scripted wear-crisis run from the scope — fleet works to degradation, parts spend, recovery — plus Ice_A provably dark (no visual, drill order rejected) until surveyed.

## 5. Gate C — the player's eyes (playability strand + director's UI directive)

- **Inspection card (the promised panel):** click any placed building → deck card: name + state (site / building / complete / SHED), construction or batch progress with recipe line, hopper contents, power draw/gen, attached deposit (name + remaining), for sites the outstanding material list. Pure Slate poll of existing sim getters; clicking ground dismisses. This is the first "point at the world and ask" surface — the pattern every later panel (robot, deposit, floor) reuses.
- **Fleet panel:** one row per robot — class, state (Work/Charge/Repair/Survey/Halted), battery %, wear % with the degrade/halt thresholds tinted. The wear system's legibility surface, per scope.
- **Notice-channel unification (Gate C findings):** `ShowToast`'s player-facing uses (placement ghost prompt, dig prompt, order-transmitted confirm, save/load result) move into the deck — ghost/dig prompts render beside the cursor-adjacent readout or the notice line; the GEngine debug path retires for gameplay feedback (stays for genuine debug).
- **PAUSED treatment:** when speed is 0, a dim full-width `— PAUSED — press Space or a speed key —` banner on the deck bar. The Gate C stall never happens to another player.
- **Verify:** hand-played — director clicks buildings mid-run, reads the card against `RH.Status` truth, watches a robot wear out on the panel.

## 6. Explicitly out of M1-b

Underground gameplay (bore/O₂/shaft — M1-d). Storms, radiation, flares, ComputeModule, uplink-queue panel, strip-chart (M1-c). Manifest composer, diegetic skin (M1-d). Era-divergence overshoot-carry fix + era→agent accumulator dump (M1-c, logged). Heightmap terrain (M2). Art beyond gray-box maintenance.

## 7. Confidence flags

1. **Z-model save/query churn** is wide but shallow — the risk is a missed `Level` filter in one query (symptom: cross-level hauling). The M0-c parity re-run is the net.
2. **Wear pacing** (8/active-sol ⇒ ~6 sols to degradation) may need the single-knob retune the scope predicted once repair exists.
3. **Ice_A hidden** rebalances the early game (ISRU starts later unless the player surveys early); quota Q1's sol-10 deadline has ~4 sols of slack on the verified runs — watch it in the wear-crisis run.
4. **Inspection card data needs** may expose one or two missing const getters on the sim subsystem — additive, no schema risk.
