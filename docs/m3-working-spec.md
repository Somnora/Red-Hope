# M3 Working Spec — Sovereignty (Phase 3 opens)

Status: **Director opened M3 2026-07-07** ("Open M3 now") after M2's structure completed (crew, rooms, Hope end-to-end, garden, water, growth, discoveries). Anchors — all approved canon:
- Brief §52-66 (Phase 3): rival national colonies, trade routes as physical objects, the Solidarity Dilemma, the Earth-Aligned ↔ Martian-Identity axis, the CEO endgame, terraforming clock.
- Design-decisions 2026-07-07j: **build trade routes as physical dependencies FIRST**, so the Dilemma later cuts something the player actually relies on — systemic, not scripted (brief open question #8).
- The director's own framing (Session 29): appease your nation (cut trade → +pride, +Earth ships, −colonist morale, −rival relations) or defy it (+pan-Martian solidarity, +fair exchange, +morale, −Earth supply priority).

## 1. Gate plan

- **Gate A — "The Neighbors" (this build):** ONE rival colony as data + a physical rover-convoy trade loop. Convoys cost Hydrogen + vehicle wear (SpareParts), take real sols, FREEZE in dust storms, and barter your surplus for their profile goods. Completed runs warm a per-rival relation scalar — the dependency the Dilemma will later cut. No Hope coupling yet; no Earth politics yet.
- **Gate B — "Earth's Shadow":** the Earth-tension meter (rises from off-screen conflict events + your identity-axis position), the requisition system (your Earth supply priority as a function of alignment), and demand generation (tension thresholds crossing → a demand arrives).
- **Gate C — "The Solidarity Dilemma":** the Comply/Defy choice itself, generated from live state (never a scripted tree): Comply severs named trade routes (the material loss is whatever YOU built), Defy costs Earth priority/requisitions. Consequences write back to relations, supply, colonist Hope (community size), and the **identity axis** accumulator that gates endings.
- **Gate D — presentation + review:** diplomacy UI, rival visualization at region edge, director hand-play + wording review (the standing Gate-D mental-health framing review folds in here for all Dilemma text).

## 2. Gate A — the sim

**The rival (DT_Rivals, one active row):** `Zarya Station` — an ice-rich crater settlement (brief: "their ice-rich crater vs. your ore-rich highlands"). Fictional naming PLACEHOLDER pending director; real-nation flavor is a director call at Gate D. Row: distance (km), what they EXPORT per convoy lot, what they IMPORT (your side of the barter), starting relation.

**The convoy (one vehicle, sim state machine):** `Idle → Outbound → Return → Idle`, driven on the sim clock in BOTH bands.
- Dispatch = uplink verb `Convoy` (signal-lagged like every order). Preflight: Hydrogen ≥ `ConvoyH2PerRun`, SpareParts ≥ `ConvoyWearParts`, and your export lot on hand. ALL committed at departure (the Borer's H2-batch pattern: fuel spent when the wheels roll).
- Transit: `DistanceKm / ConvoySpeedKmPerSol` sols each way. **Progress advances only under clear sky** — an active dust storm freezes the convoy where it sits (deterministic from DT_Events; the brief's "routes disrupted by storms" made mechanical).
- Return: their export lot lands — solids drop at the Lander (the trade depot) for normal hauling; fluids join the pool. Relation +`RelationPerRun`.
- Solids leave via `TakeSolid` (drains building stores in ascending building-Id order — deterministic).

**Balance (legible-math defaults, director review queued):** Zarya at 120 km; convoy 60 km/sol → 2 sols out, 2 back. 8 kg H2 + 1 SparePart per run. Lot: your `Struct:100` for their `Ice:150` (imported ice → WaterPlant → FRESH water → potability restores — **trade becomes an alternative to drilling your own ice**, coupling straight into the water loop).

**Save v17:** convoy state + per-rival relations.

**Verification `-trade`:** dispatch refusals (no H2 / no exports); commit-at-departure math exact; mid-transit save/load resumes; storm freeze (dispatch into the canon Storm_1 window → arrival delayed by exactly the storm's length); return credits + relation bump; zero-rival/zero-dispatch = no-op (baseline byte-identical).

## 3. Open design questions (director)

1. Rival naming/flavor: fictional consortiums vs real-nation coding (Gate D wording review).
2. Convoy loss risk (storm catastrophe, breakdown) — deferred; Gate A convoys are slow, never destroyed (prevention framing until the director rules otherwise).
3. Multiple simultaneous routes/vehicles — Gate A ships ONE convoy; fleet expansion is data once proven.
4. Does the player SEE the convoy drive (presentation) — Gate D; Gate A is deck-status only.
