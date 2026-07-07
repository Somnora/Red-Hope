# M2 Working Spec — "The Crew Arrives" (Phase 2 opens)

Status: **Gate A sequence director-selected 2026-07-07h** ("Colonists as agents" first; morale/sickness mechanics built abstract now, player-facing framing reviewed before ship). Anchors — all approved canon:
- `docs/habitat-vision.md` §9 (APPROVED): M2 activates room functions, morale/sickness adjacency, drinking-water psychology, luxury crops, windows/domes, interior view, suits, surface habs, vehicles.
- `docs/the-red-hope-design-brief.md` §2 Phase 2 (humans arrive, morale), §5 (Hope index, luxuries, water/waste).
- Rulings: RC-M retained — human purpose is "abilities robots don't have," not a repair monopoly (2026-07-07d); warehouse/vehicles shelter (2026-07-07b); habitat minimum 4 cells (2026-07-07f); **mental-health directive** — prevention-focused, abstracted consequences, never graphic; presentation framing gets director review before final.
- Groundwork banked in M1-d: rated-Livable floors (save v9), room/compartment/door schemas dormant (DT_Rooms/DT_Compartments/DT_Doors), Soil/Seeds/Luxury manifest items + resource rows dormant, T-2/T-1 arrival alert seam, ship→manifest→cargo pipeline.

## 0. What M2 turns the game into

Phase 1 built a machine colony that could keep humans alive in principle. M2 makes that promise literal: **colonists ride the supply ship you earn, and the vault you certified is the only reason they can stay.** Every colonist is a standing draw on the life support you built — oxygen from their floor's fill, food from stores — and every system from here on (rooms, morale, crops, luxuries) exists because people are present to need it. The colony's score stops being kilograms and starts being *how well people live*.

## 1. Gate plan

- **Gate A — the crew arrives (sim first, presentation second):**
  - **A1 (sim, headless-verifiable):** colonist population model + arrival + sustain/evacuate loop. Save v10.
  - **A2 (presentation):** colonist agents visible in the world (vault + suited surface walks), crew panel on the deck, arrival moment staged.
- **Gate B — rooms go live:** designate room functions in carved cells (DT_Rooms activates); adjacency calculus (EmitsTags/RefusesTags, hallway partition + filtration cancel) scored as a colony **Hope/morale index** with neutral placeholder presentation; living quarters raise housing quality; labs/workstations give colonists jobs.
- **Gate C — the garden:** greenhouse room + Soil/Seeds activate → Food production on a rated floor; the SoilPallet/SeedVault gamble pays off; drinking-water psychology (recycled-water morale cost) lands here with abstract numbers.
- **Gate D — framing review + luxury loop:** the director reviews ALL player-facing morale/sickness wording/iconography (the standing mental-health gate) before any of it is final; luxury crops (tobacco), smoking area, LuxuryGoods consumption, windows/domes morale.
- Surface habs / lego shells / vehicles / interior cutaways: **scheduled after A–D**, order by director call at that point.

## 2. Gate A1 — the sim (this increment)

**Colonist model (`FRHColonist`, sim-owned, serialized save v10):** Id, Name (from a fixed callsign table, deterministic), HomeLevel (the rated floor housing them), `bSupported` (life support currently met), UnsupportedSimSeconds (accumulator).

**Arrival:** new manifest item **`CrewPod` (2,400 kg, carries 4 colonists)** — the heaviest single item in the catalog; choosing crew over hardware is the Phase-2 manifest decision. On ship landing, each pod's colonists disembark **only into certified housing**: capacity = Σ(rated floors' cells) × `ColonistsPerCell` (config, default 1) − current population. Pods without housing stay aboard and **return with the ship** (loudly: "No certified housing — the crew stays aboard"), refunding nothing — the vault is the hard prerequisite, made literal.

**Sustain loop (`StepPopulation`, both bands, dimensionally honest):**
- **O2:** each colonist draws `ColonistO2KgPerSol` (default 0.75, hard-sci anchor ~0.84 kg/day scaled) from **their home floor's FillKg** — the M1-d atmosphere gets its first consumer; leak + breathing now compete with the trunk's refill rate.
- **Food:** each draws `ColonistFoodKgPerSol` (default 0.62) from pool stock `Food`. New resource row + starter stock arrives with each CrewPod (`CrewPodFoodKg`, default 200 — ~40 sols for 4 colonists; the clock that makes Gate C's garden urgent).
- **Support state:** a colonist is supported iff home floor is Rated AND food was drawn this step. Unsupported → alert once (edge), accumulator runs; sustained `ColonistEvacSols` (default 2.0) → **evacuation**: colonist removed, "evacuated to orbit" (neutral, abstract, prevention-framed — no death/graphic language; the failure is losing the colonist's labor and the Program's trust). Recovery resets the accumulator.
- **Rated-floor loss cascades naturally:** floor loses rating → its residents unsupported → evac timer — the M1-d "LOST HABITABILITY" alert now has stakes.

**What colonists DO in A1:** exist, consume, survive — the life-support contract. Jobs/abilities land with rooms (Gate B) so "what can only a human do" is designed against real spaces, not invented ad hoc.

**Instruments:** `RH.Crew` (roster: name, home floor, supported, evac timer), `RH.AddColonists <N>` (debug spawn into best housing), crew line in `RH.Status`; commandlet `-crew` self-test (arrival gate, O2/food draw math, evac on starved pool, save v10 round-trip).

**Explicitly deferred from A1:** colonist visuals/agents (A2), jobs (B), morale value (B — population ≠ morale yet), suits-as-items, per-colonist sickness.

## 3. Data (CSV → DT, same rows both places)

- `RH_ManifestItems.csv`: **CrewPod** (2400 kg, Category=Crew, SliceActive TRUE).
- `RH_Resources.csv`: **Food** (Solid? No — pool/network like Water; StorageType=Network) ACTIVE.
- `RH_Config.csv`: `ColonistsPerCell` 1, `ColonistO2KgPerSol` 0.75, `ColonistFoodKgPerSol` 0.62, `CrewPodColonists` 4, `CrewPodFoodKg` 200, `ColonistEvacSols` 2.
- Callsign table: code-side fixed array (deterministic, no data dependency).

## 4. Mental-health discipline in A1

The only failure surface is **evacuation** — an abstract, reversible-in-spirit consequence framed as the Program pulling people back to safety, never harm imagery. All wording ships as placeholder pending the Gate-D framing review. Nothing in A1 depicts or names sickness, injury, or death.

## 5. Verification

- Commandlet `-crew`: land a CrewPod with no rated floor → crew stays aboard; certify 4 cells → next pod disembarks 4; O2 fill drain matches `pop × rate × dt` against the refill; drain Food → unsupported alert → evac after 2 sols; save v10 mid-timer round-trips.
- Regression: `-habitat`/`-vault`/`-borer`/10-sol baseline unchanged (no colonists → StepPopulation no-ops).
- Live smoke: full arc — quota → manifest CrewPod → landing gate refusal → certify vault → second ship delivers crew → RH.Crew shows roster drawing O2/Food.
