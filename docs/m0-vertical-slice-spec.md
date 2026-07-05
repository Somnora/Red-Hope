# M0 Vertical Slice Spec — "One Perfect Hour"

Status: agent-proposed 2026-07-04, awaiting director review. Companion data: `docs/data/RH_*.csv` (DataTable-import-ready).

The slice: **Landing → power grid → first excavation → Forge online → first ice drill → first quota met → first manifest designed → ship arrival.** One biome, 5 robot types (7 units), no humans. Balance values are first-pass; the ratios and their reasoning are the review target, not the absolute numbers.

---

## 0. Units & Clock Doctrine

Everything below hangs on this. One canonical timeline; all rates deterministic in sim-time.

- **Sim-second (s):** the atomic time unit. At 1× speed, 1 sim-second = 1 real second. Fixed sim tick = 0.1 s.
- **Sol = 1,200 sim-seconds** (20 real minutes at 1×). Config: `SolLengthSimSeconds`.
- **Sol-hour (h) = Sol/24 = 50 sim-seconds.** The UI clock shows "Sol 4, 14:00" over this 24-hour diegetic day. Daylight ≈ 12 h (curve-driven).
- **Power in W; energy in Wh, where 1 Wh = 1 W sustained for one sol-hour.** So a 300 W array over a 12 h day at 0.64 mean curve factor yields ≈ 2,300 Wh/sol. Numbers read like real hardware (hard-sci tone) while integrating on the game clock.
- **Distance in meters (world-space); speeds in m per sim-second.** Movement is *visual-scale*, production is *sol-scale* — the standard colony-sim scale compact, adopted deliberately and logged. Consequence: hauling throughput is governed by trip time in sim-seconds; production by sol-hours. All CSV rate columns are suffixed `_W`, `_Wh`, `_kgph` (kg per sol-hour), `_s` (sim-seconds), `_mps` (m per sim-second).
- **Determinism invariant: acceleration never changes outcomes.** Same seed + same orders ⇒ same result at any speed mix.

## 1. The Two-Timescale Answer (director directive #1)

**One timeline, two integrators.**

**Agent band — 1× / 3× / 8× (M0 ships these).** The fixed-timestep agent sim runs at all three; acceleration just executes more sim ticks per real second. A sol is 20 / 6.7 / 2.5 real minutes respectively. Concretely, at 8×:
- **Day/night power cycle:** unchanged in sim terms — night is still 12 sol-hours of drawdown against the same battery Wh. Visually the sky wheels through a sol every 2.5 min; the power strip-chart UI (last-3-sols graph) is the legible read at speed, not the sky.
- **Uplink latency queue:** lag is sim-time denominated (45 s early game), so at 8× it passes in 5.6 real seconds. Acceleration is the impatience valve — mission control fast-forwarding through dead air is thematically exactly right. The queue UI shows each order's transmit timestamp; cancel any time before execution. While paused, orders enter the queue but the countdown doesn't advance.
- **Presentation degrades, sim doesn't:** above 3×, animation fidelity is unguaranteed (LOD'd robot motion, no per-frame interpolation promises). 1×–3× is the "watch a robot work" register; 8× is the planning register.

**Era band — ~60× "era mode" (specced now; built M1+; needed for the Phase 3 terraform arc).** Above the agent band, per-agent simulation is both unwatchable and wasteful. Era mode suspends agent ticking entirely and integrates the **colony ledger** analytically in 1-sim-minute steps: every producer/consumer already publishes steady-state rates (the same numbers the flow-ledger UI shows); stocks advance as `net rate × Δt`, with event checks each step (quota deadlines, storm rolls, threshold crossings, arrivals). Agents are parked at posts. **Any event needing fidelity auto-drops speed to 1×** — storm onset, crisis, arrival, deadline within 1 sol (data-driven list). One sol ≈ 7.5 real seconds; a 100-sol terraform milestone ≈ 12 real minutes.
- **Why this is honest, not a hack:** the aggregate integrator *is* the headless faster-than-real-time sim the standing orders require. Building it forces the sim/presentation decoupling to stay real, and it consumes the *same DataTable rows* as the agent sim — one balance dataset, two integrators, no forked numbers.
- **Latency at 60×:** no distortion in practice — era mode is a Phase 3 register, and by then local compute has collapsed order lag toward zero. The mechanic that hates fast-forward and the era that needs fast-forward don't overlap. (If a player somehow era-modes with high lag, orders still execute at their sim timestamps.)
- **Day/night at 60×:** the sky stops flickering — presentation switches to a smeared mean-light state, and the atmosphere dial (habitability scalar) becomes the visible sky driver. The sky shows the terraform arc precisely when you're playing the terraform arc.

**M0 scope:** speed tiers 0/1/3/8 implemented; era mode is a spec commitment with its integration points designed into the data schema now (per-sol-hour rates everywhere) and an M1 architecture line item.

## 2. Expansion Headroom (director directive #2)

**Decision to log: the region grows by authored sector unlocks through v1; procedural terrain is deferred to the post-v1 sandbox question. Architecture guarantees the growth path now:**

1. The gray-box map is a **World Partition level from day one** (UE 5.8 default). The slice's playable pocket (~600×600 m) sits inside a 2×2 km authored heightmap, itself inside far larger world bounds. Adding terrain later = adding landscape streaming proxies + content, not re-architecture.
2. **Sim-side spatial data is unbounded by construction:** grid occupancy, coverage, deposits, and territory live in sparse world-space structures owned by the sim layer (hash-grid, not a fixed 2D array). The sim never knows how big the world is.
3. **Deposits are data rows** (`RH_Deposits.csv`: type, location, mass) — new sectors are rows plus terrain, no code.
4. The slice already *plays* the headroom: the ore and ice you can see but can't reach until the grid extends is the expansion loop in miniature; M2's second authored sector is the same loop at map scale.
5. Known accepted cost: every sector is hand-authored (deposit placement and pacing stay designed). What would reopen this: a sandbox mode wanting unbounded replayable maps — post-v1.
6. Perf note: agent population, not terrain area, is the real scaling axis — that's the Step 3 MassEntity evaluation's problem, and region growth stays content-bounded, not architecture-bounded.

## 3. The Hour, Sol by Sol

Expected play mixes speeds (1× during action, 3×/8× through waits); ~10–12 sols ≈ 45–60 real minutes.

| Real time | Sols | Beat |
|---|---|---|
| 0–5 min | 1 | Lander touches down (validated descending-lander beat, now as the *player's* arrival). Starter kit deploys: 7 robots, 6 solar flat-packs, 2 battery packs, 400 kg Struct, 6 spare parts. First placements: solar row, battery, charge pad. First order feels the 45 s uplink lag. |
| 5–15 min | 1–3 | Grid bootstrap: pylon chain, dig zone on the regolith flat, excavators produce, haulers feed the stockpile. Scout dispatched — finds Ice A behind the NE ridge (survey ping), Ore A to the SW. |
| 15–25 min | 3–5 | **The Forge** (250 kg Struct, biggest build) goes up — milestone sting, first smelt batch. Struct now locally produced; lander stock was the bridge. |
| 25–35 min | 5–7 | Grid extends toward Ice A (pylon + forward charge pad — the leash in action). Ice drill, water plant, electrolyzer online. Power gets tight: 6 arrays can't run Forge + full ISRU + night recharge; player schedules loads (Forge by day) or waits on the manifest. First wear event: an excavator crosses 50 wear, maintenance bot teaches the repair loop. |
| 35–50 min | 7–9 | Quota panel front and center: **300 kg Struct · 200 kg Water · 50 kg O₂, soft deadline Sol 10** (on-time award 10 t ship, late 6 t — resilience, not failure). Quota met → CEO transmission → **manifest screen** (pause encouraged; this is the set-piece decision). |
| 50–60 min | 9–12 | Ship inbound 3 sols (8× montage: sols wheeling, colony humming). Landing beat. Cargo transfers. End card: sols elapsed, quota margin, kWh generated, fleet status — "The Program continues." Play may continue free-form. |

## 4. Power Grid Model (Power-as-Territory)

**Topology:** implicit cabling, explicit nodes. The Lander is the root node (30 m coverage). **Pylons** auto-link to the nearest node within 80 m (visualized as real cable meshes auto-laid on terrain — no manual routing) and project a **40 m coverage disc**. Buildings must sit fully inside coverage; robots recharge *only* at charging pads, which also need coverage. **Buildable territory = the union of coverage discs**, drawn as the literal border in placement mode. Expansion is electrical before it is architectural.

**Generation:** Solar Array, 300 W peak, output = peak × diurnal curve (`RH_Curve_SolarDiurnal.csv`, sunrise 06:00, sunset 18:00, cosine-shaped, mean daylight factor ≈ 0.64) × DustFactor (1.0 in M0; storms are M1). **Yield ≈ 2,300 Wh/sol/array.**

**Storage:** Battery Bank, 4,000 Wh, 1,000 W max charge/discharge.

**Brownout rule:** when demand > generation + battery discharge cap, loads shed by `LoadPriority` (data column): charge pads last to shed; Forge first. Shed buildings halt (no damage in M0). Advisor toast reports stalls ("Forge unpowered 2 sols").

**The intended pressure (the ratio reasoning):** mid-slice demand ≈ Forge 4.8 kWh/sol (6 h/day duty) + ISRU chain ≈ 6.6 + fleet recharge ≈ 4.6 + compute upgrade 3.6 + idles ≈ 1 → **~20.6 kWh/sol wanted vs. 18.4 kWh from 8 arrays** (6 starter + realistic manifest additions). Night baseline (fleet charging + compute + idles) ≈ 6.8 kWh vs. 8 kWh in two banks. Deliberate slight deficit: the player must schedule loads or spend manifest tonnage on power. "Every watt matters" is a budget fact, not flavor. Running the Forge overnight (+9.6 kWh) is a late-slice luxury requiring ~4 banks — a visible aspiration.

**Robot leash math:** an excavator (2,000 Wh, ~100 W moving) has huge *theoretical* range but burns ~7%/100 m one-way; a remote work stint costs 30–50% battery, so the practical operating radius is ~200–300 m from a pad. Ice A at 180 m is reachable; Ore A at 220 m is uncomfortable; both *want* the forward pad. The leash teaches grid extension without a tutorial.

## 5. Robot Fleet (5 types, 7 starting units)

Full stats in `RH_Robots.csv`. Reasoning: one excavator (50 kg/h) exactly feeds one Forge (50 kg/h appetite); the second excavator builds storm stockpile. One hauler (200 kg, 4 m/s: ~2.5 kg/s sustained over a 100 m loop) over-feeds the Forge alone, so two cover Forge + ISRU + construction. Endurance ≈ 17 h on ~115 W duty → one ~4 h charge per sol at a 500 W pad: a legible workday rhythm.

| Unit | Battery | Move/Work draw | Speed | Role numbers |
|---|---|---|---|---|
| RC-E Excavator ×2 | 2,000 Wh | 100/120 W | 2.5 m/s | Digs 50 kg/h at designated deposits |
| RC-H Hauler ×2 | 1,500 Wh | 80/30 W | 4.0 m/s | 200 kg cargo; 10 s load/unload |
| RC-F Fabricator ×1 | 2,500 Wh | 90/150 W | 2.5 m/s | Builds at 1× base rate; consumes Struct delivered to site |
| RC-S Scout ×1 | 800 Wh | 60/40 W | 7.0 m/s | 60 m survey radius reveals deposits |
| RC-M Maintenance ×1 | 1,200 Wh | 70/80 W | 3.5 m/s | −25 wear per spare part; carries 2 |

**Wear:** 0–100 per unit, accrues per active sol (excavators fastest at 8/sol); work rate degrades linearly past 50; at 100 the robot halts (destruction/salvage is M1+). First teaching repair lands ~Sol 5–6 by arithmetic, not script.

## 6. Production Chains

**Regolith → Forge:** dig zones on deposit patches → piles (500 kg cap) → haulers → Forge hopper (200 kg). Forge batch: **100 kg regolith → 25 kg Struct, 2 h, 800 W** (ore variant: 100 kg → 60 kg Struct). Struct builds everything structural. Dormant-but-defined recipes: Shielding (40 Struct + 60 Ore → 50), HabSegment (120 Struct + 30 Shielding) — M1's habitat consumes them; the slice schema already knows them.

**Ice ISRU:** Ice Drill on deposit (**40 kg ice/h, 250 W**) → Water Plant (**50 ice → 45 water per h, 300 W**) → Electrolyzer (**10 water → 8.9 O₂ + 1.1 H₂ per h, 600 W** — true 8:1 mass stoichiometry). Quota needs ≈ 285 kg ice, ~7 h drilling spread over 2–3 sols of daylight ops. H₂ has no slice consumer — banked fuel, labeled as such (future hoppers/return ships), and the quota's O₂ line gives the chain its point *now*.

**Import-only rule (proposed):** photovoltaics, battery cells, and robots cannot be smelted from regolith — **solar arrays, battery banks, and robots arrive only via lander stock or ship manifests**; everything structural is local. Hard-sci honest, and it interlocks the two signature mechanics: Power-as-Territory growth is gated by Ship-Manifest choices.

## 7. Quota & Manifest (the set-piece)

**Quota Q1** (arrives Sol 1 as CEO transmission): 300 kg Struct + 200 kg Water + 50 kg O₂ by Sol 10 (soft). On-time: 10,000 kg ship. Late: 6,000 kg. Never a fail state.

**Manifest catalog** (mass budget; `RH_ManifestItems.csv`): Soil pallet 1,000 · Seed vault 200 (dormant until Phase 2 — the "invest in a future that isn't here yet" temptation) · Spare parts crate 500 · Specialty toolkit 300 (+15% build speed) · **RC-E2 advanced excavator 2,500 (100 kg/h)** · RC-H hauler 1,800 · Solar flat-pack 400 · Battery pack 600 · **Compute core 800 (lag 20→8 s)** · Luxury goods 300 (dormant). A greedy build (2 robots + 4 solar + 2 battery + compute + parts + toolkit) hits 9,200 of 10,000 kg with soil still whispering — the tension is the design. 100% soil remains legal and reckless. Ship transit: 3 sols, then the landing beat.

## 8. Latency (slice tuning)

Order lag 45 s at landing (UI labels the diegetic fiction: "Δ 11 min signal delay"). **Lander Compute Module** upgrade (100 kg Struct, +150 W continuous — 3.6 kWh/sol, a real power price for autonomy) → 20 s. Manifest **Compute Core** → 8 s. Lag applies to strategic order execution only — never camera, selection, or UI. Every order visible in the uplink queue with its transmit time; cancellable until execution.

## 9. Slice Map

2×2 km authored heightmap (World Partition), playable pocket ~600×600 m defined by power reach, not walls. Landing flat: Regolith A (60 t) adjacent; Regolith B (100 t) 300 m W. Ore A (8 t) 220 m SW surface-visible; Ore B (15 t) 450 m S, scout-only. **Ice A (30 t) 180 m NE behind a ridge — scout-only, the first "go look" moment.** Ice B (60 t) 700 m N, visible on survey, out of practical reach: the standing invitation to expand (and M1 bait). Grid cell 2 m; footprints from Solar 2×3 to Forge 4×5 to Lander 5×5 (`RH_Buildings.csv`).

## 10. Win Condition & Out-of-Scope

**Slice win:** ship touchdown + cargo transfer → end card (sols, quota margin, kWh, fleet wear) → free play continues.
**Explicitly out of M0:** humans, habitat construction, dust storms, radiation, robot destruction, save/load (dev autosave only), era mode (specced §1, built M1), sound design, any art pass. Dormant data rows ship in the tables so M1 activates content, not schema.

## 11. Confidence Flags (honest uncertainty)

1. **Hauling saturation** depends on final layout distances — trip math is the first thing playtests will bend. (Knobs: cargo, load time, speed.)
2. **Real-minutes-per-sol pacing mix** (how much of the hour players spend at 8×) — the sol budget per beat may compress/stretch ±30%.
3. **Solar mean factor and robot draws** — the ~10% deliberate power deficit could land punishing instead of tense; `SolarPeak_W` is the single-knob fix.
4. **45 s starting lag** — untested feel; the floor of fun might be 30 s.
5. Batch time 2 h (100 s at 1×) per Forge cycle — cadence chosen so 8× shows a batch every ~12 s; may feel slow at 1×.

---

*All numbers live in `docs/data/RH_*.csv`, DataTable-ready (Name-keyed, typed columns, semicolon lists). Balance is tuning, not rework.*
