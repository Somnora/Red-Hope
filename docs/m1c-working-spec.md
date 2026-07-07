# M1-c Working Spec — "World Pressure"

Status: **agent-proposed, awaiting director review.** Nothing here is started.
Anchors: approved M1 scope (`docs/m1-scope-proposal.md` §6 M1-c: storms, era auto-drop, ComputeModule, uplink queue UI, strip-chart), the underground rulings (`docs/m1-underground-proposal.md` §3/§6: radiation + solar flares + the surface shielding tax land here, ahead of the M1-d habitat payoff), and the queued fix list (era overshoot-carry, era→agent accumulator dump, harness-beats-in-sim-time). No new canon.

## 0. What M1-c turns the game into

M1-b gave the colony a mortal fleet on a finite map. M1-c gives the *planet* agency: multi-sol dust storms that collapse solar output and grind the fleet, radiation that taxes everything built on the surface, and solar flares that make the sky itself an enemy — the dread that makes M1-d's underground vault the payoff instead of a feature. Alongside it, the latency arc completes (ComputeModule + visible uplink queue), and era mode graduates from "works" to "trustworthy" (divergence bar met, auto-drop on events).

## 1. Gate plan (three compiles, each verified before the next)

- **Gate A — events + era honesty** (`RedHopeSim`): the RH_Events table (storms + flares in one schema), DustFactor through power, era auto-drop on event onset, the overshoot-carry fix + paired-run harness, the era→agent accumulator dump fix.
- **Gate B — the surface tax** (`RedHopeSim` + data): per-level RadiationLevel, storm/flare wear multipliers on exposed robots, the Shielding build tax on surface structures (dormant Forge product finally earns its row).
- **Gate C — pressure legible** (`RedHope`): storm/flare alert banner + sky presentation through the atmosphere dial, ComputeModule + uplink queue panel (cancellable orders), power strip-chart (last 3 sols).

## 2. Gate A — events + era honesty

- **`RH_Events` table** (new CSV → DT): `Type` (DustStorm | SolarFlare), `StartSol`, `DurationSols`, `Severity` (DustFactor floor for storms; surface radiation multiplier for flares), `SliceActive`, `Notes`. Scripted schedule for M1 (deterministic); probabilistic scheduling is a post-M1 knob on the same table.
- **DustFactor finally moves:** `StepPower` multiplies solar gen by the active storm's factor (the config row has waited since Step 2). Night + storm = the bank-and-shedding stack under multi-sol siege — the M0-c systems under real pressure, no new code.
- **Era auto-drop:** `CanEnterEraMode` gains event checks (storm/flare onset within the next era step refuses/drops — the `EraAutoDropEvents` config row named in the M1-a spec grows its list). A storm mid-era-run yanks to the agent band at the sol boundary before onset.
- **Era overshoot-carry fix** (the 8.8%-vs-5% divergence, root-caused in Session 9): era production steps carry batch overshoot into the next quantum instead of discarding it. **Paired-run harness** (scripted, in-repo): same save, 10 sols agent 8× vs era 60×, assert ≤5% divergence on every stock line — the acceptance bar from the M1 scope, finally instrumented.
- **Era→agent accumulator dump fix** (Session 12 note): the sub-second accumulator flushes into the era integrator on the way down instead of being discarded (43 sim-s losses observed).
- **Verify (headless, beats in SIM time via sol-fraction polling — the Session 13 rule):** scripted 3-sol storm at era speed: auto-drop fires at onset, colony survives on bank + shedding, era resumes post-storm; paired-run harness passes the 5% bar.

## 3. Gate B — the surface tax (underground rulings #3/#4, the M1-d setup)

- **RadiationLevel:** depth-indexed scalar — surface high, every subsurface floor ≈ 0 (overburden is free shielding). Data: config rows `RadiationSurface`, `RadiationPerLevelMul`. No gameplay sink yet at Level 0-only — the *sink* is:
- **Flare events:** during a flare, exposed surface robots accrue wear at `Severity ×` rate (the brief's §4 threat, generalized through the same wear system Gate B landed — no new mechanics, just a multiplier). Robots docked at pads still count as exposed (no shelter exists yet — that's M1-d's vault; *the player is supposed to feel this gap*).
- **Shielding build tax:** surface structures' `CostResources` gain a `Shielding:N` line (data edit; the `MakeShielding` recipe row activates: Struct+Ore → Shielding). Ore_A finally matters → the colony's pull 220 m SW begins, per the M1 scope's exit-arc geography. Deliberately mild numbers at M1-c (the tax reads as a line item, not a wall) — the real bite arrives with M1-d's per-floor economics.
- **Verify:** scripted flare over a working fleet → wear spike visible in RH.Fleet deltas; a surface build order shows the Shielding line in its site materials; era paired-run still passes with events active.

## 4. Gate C — pressure legible

- **Event banner:** deck-top center strip — "DUST STORM — sol 12..15, solar at 30%" / "SOLAR FLARE — exposed units taking wear" — driven by a sim `GetActiveEvent()` getter; amber for storms, red for flares.
- **Sky presentation:** storms drive the atmosphere dial's dust endpoints (MPC hookup exists since M0-a); flares get a brief sky-brightness pulse. Presentation-only, dial-gated.
- **ComputeModule + uplink queue panel:** the Lander upgrade row activates (100 kg Struct, +150 W always-on, lag 45→20 s — autonomy has a power price); the deck gains the uplink queue panel: every order in flight with its diegetic countdown ("Δ 4:32"), cancellable until execution (M0 spec §8 commitment). Cancel = remove from queue, confirm line notes it.
- **Power strip-chart:** last 3 sols of gen/load/bank as a compact sparkline strip in the readout column (the spec's "legible read at speed") — pure Slate polyline over a ring buffer the sim already effectively has in Power state sampling.
- **Verify:** hand-played storm — banner up, sky dims, shedding readable on the strip-chart, an order cancelled from the queue panel mid-lag.

## 5. Explicitly out of M1-c

The Borer, H₂ fuel, O₂-fill, per-floor habitability, sliced camera, manifest composer (all M1-d). Pressure breaches/micrometeorites (M2). Probabilistic event scheduling (post-M1 data knob). Colonist anything (M2).

## 6. Confidence flags

1. **Storm severity numbers are first-guess** (scope flag #2 stands): M0 power margins were tuned clear-sky; expect a solar-count rebalance after the first storm run — data-only.
2. **Flare wear multiplier vs repair economy:** a hard flare can outrun 10 parts/crate; watch whether the RC-M loop death-spirals during the verify run — the knob is `Severity`, data-only.
3. **Strip-chart is the only novel Slate drawing** (polyline vs text) — if OnPaint fights back, fallback is a text sparkline (block characters), zero scope change.
4. **Era + events interaction** is the deepest water: the auto-drop boundary condition (drop *before* onset, never mid-event) needs the paired-run harness extended with an event window.
