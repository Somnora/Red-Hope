# Tiered Workstations, Research & Autonomy — working spec (2026-07-10)

Director brief (live session): tiered production stations, prefab advanced
modules, research as funding + unlocks, click-to-assign work, and a colony
that runs its own routine ("the player's job is to make a fully functional
Martian town").

## What already exists (this spec builds on, not around)
- Rooms are player-designated cell zones (DesignateRoom); Lab/Workstation rooms
  create deterministic JOB SEATS; seats feed Hope (jobs weight) and staffed Lab
  seats accrue seat-hours toward DISCOVERIES (StepDiscovery, Hope-gated).
- Discoveries already model "research unlocks" (permanent Hope milestones +
  stock rewards, DT_Discoveries). Skills already ramp per-colonist (SkillSols).
- Crew autonomy: eat/drink/breathe via the support contract; presentation now
  adds meals at Dining and sleep at LivingQuarters by night (this session).

## Design

### 1. Station tiers (data, not new systems)
New FRHRoomRow columns: `Tier` (1..3), `UpgradesTo` (row name), `SeatCount`,
`YieldMul`, `EfficiencyMul`. Two families:

- Workstation line: `Workstation` (small bench, T1, 1 seat, 1.0x) →
  `WorkbenchLarge` (T2, 2 seats, 1.35x) → `Workshop` (T3, 3 seats, 1.8x,
  UNLOCKED by discovery `RegolithCeramics`).
- Lab line: `Lab` (small chem table, T1) → `ChemTableLarge` (T2, 1.35x
  seat-hours, -15% reagent draw) → `LabFull` (T3 PREFAB, 2.0x seat-hours,
  unlocks the back half of DT_Discoveries; placed as a BUILDING not a room —
  reuses the building placement/cost path, like AirFilter).
- `Infirmary` (prefab building): passive - colonists recover "strained" state
  faster; feeds the existing evac-prevention loop (abstract, Gate-D safe).

Tier YieldMul multiplies garden-style production for Workstation-adjacent
recipes and seat-hour accrual for Lab-line rooms. EfficiencyMul discounts
recipe inputs. Both flow through existing StepProduction/StepDiscovery math —
one multiplier read each, era-parity-safe (pure scalars on linear paths).

### 2. Research = funding + unlocks
Discoveries gain a `FundingKg` column: each completed discovery credits the
quota/manifest ledger (the existing "CEO funding" currency) — research
literally pays. `UnlockRoom` column: a discovery can flip a dormant room row
SliceActive (Workshop, LabFull) — the tier ladder is gated by playing the
research loop, not a tech-tree UI.

### 3. Designate-work-now
Room props become clickable (the action-card raycast already exists for
buildings; extend the hit test to prop components). Card shows the station +
its seats; verb "Assign worker" enqueues uplink `Assign <Level> <Cell>` which
promotes the seat to the FRONT of the deterministic job-assignment order
(a stable priority list in save data). Without it, seats fill "eventually"
(existing behavior) — exactly the director's ask.

### 4. Autonomy guarantee (mostly done)
Eat/sleep/self-maintain runs without player input as long as resources exist —
already true in sim; presentation beats (meals, sleep) landed this session.
Remaining: idle colonists without seats should occasionally SELF-ASSIGN to an
empty seat after N sols (sim: lazy seat fill), so "someone eventually takes
charge" is literal.

## Gates
- A (sim): tier columns + multipliers + lazy seat fill + `-tiers` self-test.
- B (data): CSV rows for the 5 new stations + funding/unlock columns + DT sync.
- C (presentation): tier art (GENERATED this session: workbench_lg, workshop,
  chemtable_sm/lg, lab_full, infirmary), RoomPropPath rows, prefab building
  wiring (LabFull/Infirmary via RealModelPaths).
- D (interaction): clickable props + Assign verb + card.

Balance numbers above are placeholders for legible test arithmetic — director
review before tuning, per house rule. No morale/sickness wording changes
(Gate-D framing review still standing).
