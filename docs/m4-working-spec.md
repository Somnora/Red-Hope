# M4 Working Spec — Complex Diplomacy, Subterfuge & Dynamic Crises (on-brief adaptation)

Status: **Director opened M4 2026-07-07** from a feature spec, ruled "build the on-brief version" (design-decisions 2026-07-07m). Goal: kill mid/late-game stagnation with two-layer diplomacy, an espionage economy, Earth geopolitical pressure, and alignment-gated dynamic crises — all in the prevention-framed, deterministic register (no on-map combat, no feral robots, no theft-as-bloodshed). Builds on M3 (rivals, trade convoys, EarthTension, IdentityAxis, the Solidarity Dilemma).

## The two late-game axes (the throughline)
- **IdentityAxis** (M3, built): −100 Earth-aligned .. +100 Martian. WHO you belong to.
- **HumanNatureAxis** (M4, new): −100 Destructive/Predatory .. +100 Evolved/Diplomatic. HOW you treat others. Fair trade / pacify / aid → +; steal / sabotage / coerce → −. Gates which crisis spawns and (with IdentityAxis) reads into the endings. This IS the director's "Evolved Diplomacy vs Destructive Human Nature" metric.

## Gate plan
- **Gate A — the covert layer (this build):** per-rival `HiddenTension` (the secret hostility beneath Public_Standing) + the HumanNatureAxis + one covert action: **Covert Requisition** (steal a resource lot from a rival). A DETERMINISTIC detection check — a seeded hash of (sol, rival, attempt#) vs a detection probability modulated by DAY/NIGHT (night = lower detection; folds in Module 2's visibility) and the rival's relation. Clean → you gain the lot, HiddenTension rises, HumanNatureAxis −. Caught → no goods, Public_Standing craters, HiddenTension spikes, an incident alert. Uplink verb `Covert`. Save bump.
- **Gate B — espionage economy + discovery:** laundering (trade stolen goods back to the victim to defuse HiddenTension); covert SABOTAGE (a temporary deterministic production/power downtime on a rival); Discovery_Event when a survey team finds a new settlement (unlock the rival + its diplomacy/covert options). Day/night visibility deepened.
- **Gate C — Earth pre-emptive pressure:** under high EarthTension an allied/neutral colony gets an Earth override → it EMBARGOES you (trade refused) or DEFECTS (relation locks hostile). Diplomatic COUNTERS: spend Influence (a new soft currency earned by fair dealing / high HumanNatureAxis) to pacify — the colony "sees reason." A cold-war severance state when pacification fails.
- **Gate D — dynamic crises + alignment gating + review:** the solar flare deepened into a systemic BLACKOUT + deterministic robot "malfunction" downtime (units inert, need a reset order — never feral). A crisis SELECTOR: the HumanNatureAxis (and IdentityAxis) picks WHICH late-game crisis fires (a Destructive colony draws sabotage-blowback / unrest; an Evolved colony draws environmental/logistics tests). The standing Gate-D mental-health framing review of ALL M3/M4 player-facing wording folds in here. Ending gates read both axes.

## Determinism discipline (hard)
Every "check" (detection, malfunction, crisis selection) uses a SEEDED deterministic hash of sim state (sol, entity id, attempt counter) — never live RNG, never a dynamic AI-controller flip. Runs identically in both time bands; the zero-rival / pre-M4 path is a strict no-op so all prior baselines stay byte-identical.

## Open questions (director)
1. Influence as a new currency vs reusing an existing resource — Gate C call.
2. How visible is HiddenTension to the player (fully shown vs inferred from incidents) — a fog-of-war design choice.
3. Endings framework (which axis-combinations map to which ending) — M4 close / M5.
