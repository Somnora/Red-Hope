# E1, the ledger: design (2026-10-22)

Step 3 (economy), gate E1. Design only: nothing here is built. Read at HEAD `df3e479` (save v47). D5 is built and D6 is in flight in the same tree. Line numbers are at HEAD. "W" is `Source/RedHopeSim/Private/RHSimWorldSubsystem.cpp`, "G" is `RHSimGrowth.cpp`, "T" is `RHSimTransit.cpp`, "R" is `RHSimRoads.cpp`, "V" is `RHSimRovers.cpp`.

Binding rulings: Q1a, Q2a, Q3a and Q4a (`docs/design-decisions.md`, "Step 3, economy"). Ruling 7: the manifest is a mass budget plus packages. Ruling 11: Hope is never spent, and in E1 it is never valued either.

## What E1 builds, in one paragraph

A second ledger sits beside C5d's. It has one slot per mass resource, and four legs per slot: **Made**, **Used**, **In** (from Earth or a rival) and **Out** (to a rival; to Earth from E3). Each leg has an open tally for the current sol, a ring of the last 8 completed sols in integer grams, and a lifetime total in grams. It rolls at the same sol threshold as `RollProductionLedger`. A new `RH_Resources` column gives each row an Earth reference value. Trade and quota amounts pass through a whole-gram rounding at the boundary. Nothing reads any of it except the new `-economy` suite.

**C5d is not touched.** `ProdAccKg`, `ProdRing`, `NoteProduced` and `GetProductionPerSol` keep their code and their saved bytes. Settlers and the Big Arrival keep reading them. If the old ledger were rebuilt on grams, `GetProductionPerSol` would move, and so would every settler line.

## 1. The call sites

**One rule decides every site.** *Holdings* are: the pool (`Stocks`), every building's `InputKg` and `OutputKg` (construction sites included), the deposit piles (`PileKg`), floor and hab air (`FloorO2Kg`, `HabO2Kg`), cargo on a robot or rover, and the bills held by open growth projects (`P.Bill`, saved since v38). **Made** means mass entering holdings from outside them. **Used** means mass leaving holdings into a batch, a structure, a body, a leak or nothing. A move between two holdings is not recorded.

**One hook.** `NoteFlow(Resource, Leg, Kg)` ignores Kg <= 0 and any name that has no slot. It is never called from a `const` function or a preview (`ComputeSalvage` serves `CanDemolishBuilding` too). It records the amount that actually moved: a value already computed, or a before-minus-after read. It never records a nominal amount. A loop over people or floors sums locally and calls once per resource. **No hook goes into `AddStock`.** `AddStock` also carries transfers, and several flows bypass it (W:4686, every shelf write).

| Family | Site (file:line) | Leg | Band |
|---|---|---|---|
| Recipe inputs, at batch start | W:1743 (solid), W:1747 (pool) | Used | both (`StepProduction`) |
| Recipe outputs, at completion | W:1491 (solid shelf), W:1496 (pool; next to `NoteProduced` :1497) | Made | both |
| Ice Drill: deposit draw at start | W:1737: *not recorded* (the ice is Made at :1491) | - | both |
| Borer fuel; bore and carve spoil | W:1633 Hydrogen; Regolith 1,200 via :1491 | Used; Made | both |
| Floor and hab leaks | W:2844-2845, W:2961-2962 | Used O2 | both |
| Breathing | W:4655, :4661 (floors); W:4719-4735 (surface) | Used O2 | both |
| Eating and drinking | W:4686-4688 `DrawPool` (`Drawn`, short rations included) | Used Food / Water | both |
| Greywater return | W:4906 | see Q-B | both |
| Luxury | W:4746 | Used | both |
| Garden | Soil, Seeds, Glass W:4140-4142; crop water :4327; Fertilizer :4376 | Used | both |
| Garden | Compost :4213; O2 :4349; Food :4363 | Made | both |
| Environmental crisis | W:10136 | Used Water | both |
| Construction completion | W:12700-12705: held before minus held after (the clamp makes it min(held, bill)) | Used | agent only (era refuses a site, W:694) |
| Furnish, Fit | W:9204, W:9224 (through `SpendTradeLot`: record at the caller) | Used | both |
| Roads, lifts | T:251-283 from R:282 (`TakeStructForBill`, up front); T:341 | Used Struct | both |
| Road refund | R:457-467 | Made | both |
| Sales, salvage | `CreditLander` callers W:11185, :12070 (75%); W:10833-10834, the salvage share only (see below) | Made | both |
| Growth bill taken | G:644 to G:249-291 (`TakeGrowthResource`): a move into escrow, *not recorded* | - | both |
| Growth completion | G:499: the bill minus what the call puts back (`CreditLander` G:1205, :1244, :1301, :1323) and minus the O2 it puts into air (G:1190, :1231, :1294) | Used | both |
| Growth refused or cancelled | G:505, G:672: escrow comes back, *not recorded* | - | both |
| Growth excavation | W:8279 SpoilChemicals (the `SpoilPileKg` counter is not a holding) | Made | both |
| Agent dig | inside `DigDeposit`, W:12350-12351 (`Dug`); called from `RHRobotTaskProcessor.cpp:244`, `RHRobotStateTree.cpp:235` | Made | agent |
| Era dig | W:897-898 (to a hopper), W:921-922 (to a store), W:930 (`DigDeposit`, covered above) | Made | era |
| Era transfers | W:973-974 (outpost ice), W:1018-1019 (outputs): *not recorded* | - | era |
| Haul loss | W:12582, W:12592, `NoteHaulCargoLost`, V:275 | Used | agent only |
| Robot repair | W:13068 SpareParts | Used | agent only (era refuses a repair, W:719) |
| Convoy departure | W:9363: H2 `ConvoyH2PerRun` and SpareParts `ConvoyWearParts` | Used | both |
| Convoy departure | W:9363: the rival's `ImportLot` | Out | both |
| Convoy home; covert loot | W:9580-9590; W:9745-9755 | In | both |
| Launder | W:9796, the peace offering | Out | both |
| Ship landing | W:2332, W:2359-2362, W:2372, W:2399 (`ApplyManifestItemEffect`) | In | agent in practice (era refuses an inbound ship, W:711) |
| Big Arrival provisions | W:7018 | In | both |
| Discovery reward | W:7418 | Made | both |

**Salvage.** `RemoveBuilding` credits the held stock and the salvage in one pass. Only the salvage share is Made: the credited total minus what the building held. The held stock is a move.

**The convoy.** The cost lot at W:9340-9346 merges fuel, wear and the lot under shared keys. The split is recorded from its three parts after `SpendTradeLot` returns. `HasTradeLot` has passed, so each part was spent in full.

**Not recorded, on purpose:**
- the starter Struct and SpareParts (W:509, W:515);
- every `Debug_*` helper;
- `ImportStock`, which counts units, not kg;
- CO2 (`ColonyCO2Kg` is not a resource row);
- `AwardMassKg` and the manifest's `Mass_kg` (budgets, not mass);
- batches in progress (`PendingOutputs`);
- the save's cargo return (W:13255-13293 writes only the saved copy).

**The quota.** Today it moves nothing (W:2075). E1 records nothing for it. The Out leg is there for E3.

## 2. The ledger data

- **Slots.** Built at world start and at load from `DT_Resources`: every row except `StorageType` `GridBuffer` (Power) and `Ledger` (Research, Hope). That is 18 slots today, dormant rows included. They are sorted by `ToString()` with a case-sensitive ordinal compare, never by FName index (the covert seed bug of 2026-07). A `TMap<FName, int32>` index serves lookup only and is never iterated.
- **Per slot, per leg:** `OpenKg` (double), `RingG[8]` (int64 grams) and `LifetimeG` (int64). One shared `RingFilled`, `RingHead` and `EconCheckedSol`, separate from C5d's.
- **The roll.** `RollEconomyLedger(SolNow)` is called at G:475, straight after `RollProductionLedger`, with the same `SolNow`. It is a threshold on the monotone clock, so both bands roll on the same step. Each leg: `G = llround(OpenKg x 1000)`. Write `G` to the ring, add it to `LifetimeG`, and keep `OpenKg - G / 1000` as the next sol's start. The carried residual is under half a gram, so lifetime totals never drift. A long jump records empty sols, as C5d does, at most 8 of them.
- **Grams are not rounded per call.** A garden cell's oxygen in one 0.1 s step is tens of milligrams. Rounding each call would zero the agent band and not the era band.
- **Reads (tests only):** `GetLedgerWindowG(Resource, Leg)` sums the last `ProductionWindowSols` completed sols. Also `GetLedgerLifetimeG` and `GetLedgerOpenKg`. A runtime-only `Debug_SetLedgerEnabled(bool)` exists for the twin checks; it is never saved.
- **Save (next free version).** One block straight after the C5d block, the last thing in the file. Layout: the slot count; then, per slot in sorted order, the name and, per leg, `OpenKg`, `LifetimeG` and `RingG[8]`; then fill, head and checked sol. Each slot is a fixed size, so the block's size depends only on the table, never on history.
- **Load.** Names must be strictly ascending. A name with no slot is skipped with a Warning (a row was removed). A slot missing from the file starts at zero. A ring index out of range, a duplicate or an unsorted name logs an Error and resets the ledger to empty, as C5d clamps. This block is not refused before teardown, because nothing reads it and it sits at the end.

## 3. The reference values

New column `EarthValue_mcr` in `docs/data/RH_Resources.csv` and `FRHResourceRow`: int32 milli-credits per kg, read into int64. The director compiles the struct, then the CSV is re-imported and the DT live-synced; the pure-data rule applies. Each Notes cell gains "EarthValue placeholder, director review."

| Row | mcr/kg | Row | mcr/kg | Row | mcr/kg |
|---|---|---|---|---|---|
| Regolith | 50 | Water | 4,000 | Seeds | 400,000 |
| Ore | 500 | Oxygen | 8,000 | Luxury | 150,000 |
| Struct | 5,000 | Hydrogen | 12,000 | Glass | 15,000 |
| Shielding | 8,000 | Food | 20,000 | Compost | 0 |
| HabSegment | 900,000 (per unit) | SpareParts | 200,000 | SpoilChemicals | 2,000 |
| Ice | 3,000 | Soil | 5,000 | Fertilizer | 10,000 |
| Power, Research | 0 (no slot) | **Hope** | **0, never valued (ruling 11)** | | |

Notes:
- Earth-made goods are dear, and Mars bulk is cheap.
- Zarya's barter reads as nearly even: 100 Struct is 500 credits out, and 150 Ice is 450 credits in.
- Q1's bill is worth 2,700 credits at these values, against a 10,000 kg award. Under E2's default of 1 credit per award-kg, Earth would pay about 3.7 times the goods' value. That premium is politics, and it is for the director to tune in E2 or E3.
- A `HabSegment` "kg" is one unit, so its value is per unit.
- The widest product E4 can form, grams times mcr/kg, stays under int64 up to 10^9 kg.

## 4. Inertness

**Why nothing moves:**
1. Nothing reads the ledger or the column: no sim rule, no panel, no console cheat, no log line. The roll logs nothing.
2. Every hook only reads values that already exist and writes ledger members. No branch, no float expression and no evaluation order changes. A before-read is taken before the existing write, and the write itself is untouched.
3. **The gram rounding is an identity on every value the battery can make.** It applies to `ParseResourceList` results for `ImportLot`, `ExportLot` and the convoy cost, to the covert amount (lot x `CovertLotFraction`) and to `ScaleQuotaBill`'s return. Every authored lot and bill is a whole or half kilogram. `ScaleQuotaBill` already rounds renewals to whole kg. For k grams, `k / 1000.0` is the double that `Atod` gives for the same decimal, so the value comes back bit-identical. EC2 proves this for every row.
4. C5d and `NoteProduced` are untouched.
5. **The save block is at the end, and its size is fixed by the table.** No earlier offset moves. The `+32` and `72`-byte deltas (`RHSimDepthSuite.cpp:786`, TN8 at `RHSimCommandlet.cpp:14419`) are unchanged.

**The lines that do move** (the builder re-counts at build time with `grep -n "Ver == 47\|/47\|v47"` over the suites, D6's included):
- **Every save-version line.** At HEAD: `-traffic` X15 (:10817), `-deep` K6 (:12513, :12528), `-framing` e5 (:14947), `-farice` F16 (:12097, :12103), `-shafts` (:14018), `-tunnels` TN1, `-transit` (`RHSimTransitSuite.cpp:1595`) and `-depth` DP11 (`RHSimDepthSuite.cpp:786-787`). DP11 also prints the two whole-file sizes, which grow by the block on the same line.
- **Every `(N bytes` line** grows by the block. `diff_battery.py`'s mask hides these. The builder states the byte delta, as W1 did.
- **A new `economy.log`.**

Nothing else may move. Playtests must be byte-identical to the run before E1, and the `-scale` digests must be identical.

## 5. Era parity

Shared code runs at the same index in both bands, so most flows are recorded at the same site in both. These differ:
- **Digging.** In the agent band it goes deposit to pile, through `DigDeposit`. In era it goes deposit to hopper or store (W:897, W:921). Both record Made at the moment mass leaves the deposit, so the dig leg compares like with like.
- **Agent only:** haul losses, repairs, construction completion and ship landing. Era refuses the last three while they are pending. A paired run starts from a save with none pending.
- **Lumpiness.** An era step is 60 s, a twentieth of a sol, and a batch or a ration lands in whole steps. At the sol threshold, the step that crosses it is credited to the old sol in both bands. Era's ring entry is therefore shifted by up to one 60 s step. For a constant flow this cancels. For a diurnal flow it moves no more than the first minute after midnight.
- **The bar:** over the window, sums agree within 5% per slot and leg.

**The paired check (EC7, in `-economy`; `-eraparity` keeps every line).**
- **Setup.** It uses EP4's harness pattern: the agent leg's excavators and haulers are played through the same sim calls (`TryClaimDig`, `DigDeposit`, `TryClaimHaul`, `HaulLoad`, `HaulUnload`). The colony is EP4's (a Forge beside Regolith_A, the Lander), plus an Ice Drill, a Water Plant, an Electrolyzer, a two-cell garden and 6 colonists. Both legs load one save and run 10 sols.
- **The bar.** For every slot and leg where either band is over 1 kg, two things must agree within 5% of the larger: the 10-sol `LifetimeG` delta, and the final window. Smaller legs must agree within 1 kg. Per-sol entries are printed as information lines and have no bar.

## 6. Tests

**`-economy`**, fails loudly. It goes in its own file, `RHSimEconomySuite.cpp`, and is added to `rh_battery.sh` (one more suite, one more run).

| Check | What it pins |
|---|---|
| EC0 | Data. Every row has `EarthValue_mcr` >= 0. The CSV equals the live DT. Hope, Research and Power are 0 and have no slot. 18 slots, ascending. |
| EC1 | The inert twin. 10 sols with the ledger on, against `Debug_SetLedgerEnabled(false)`, in both bands. The digest of stocks, shelves, deposits, piles, air, buildings, power, Hope and the quota phase is bit-identical. |
| EC2 | Rounding. Every authored lot, the convoy cost, the covert lot and the bills of rounds 0-6 at 1.25 and 1.2 round-trip bit-exact. 0.0004 kg is 0 g and 0.0005 kg is 1 g. |
| EC3 | One fixture per family in section 1, asserting the leg and the exact amount. Examples: Forge Used 100 Regolith at start and Made 25 Struct at completion; convoy Used 8 H2 and 1 SpareParts, Out 100 Struct, then In 150 Ice; a growth project records nothing at start and its bill, less the air, at completion. |
| EC4 | Conservation over 10 sols of the busy colony, each band. For every slot, the change in holdings equals Made + In - Used - Out within 1 g plus 0.05 g per haul (the float cast at W:12530). This is the net that catches a missed site, D6's included. |
| EC5 | The roll. A step across the boundary; an era step across it; a long jump of 12 sols gives 8 empty entries; 1,000 rolls keep the residual under 0.5 g. |
| EC6 | The save. The block's names are read back ascending. Save, load, save is byte-identical. The previous version is refused. An unknown name is skipped; an unsorted file resets with an Error. |
| EC7 | The paired run (section 5). |
| EC8 | Pins. Lifetime grams per slot after the 10-sol baseline colony, taken from the first green build and written into the suite. |

**`-scale`** (outside the battery). At N = 5,000, time the agent sub-step and the era step with the ledger on and off. Budget: within 5% or 0.1 ms. Time 1,000 rolls; budget 2 ms. Every existing digest line must be identical.

## 7. Risks

1. **A missed site.** About 45 sites in six files. EC4 is the guard. D6 will add sites, so EC4 must run after D6 lands.
2. **Merge with D6.** D6 edits growth and carving (G, W:5189-8279). The builder rebases the hooks on D6's code, not on these line numbers.
3. **Escrow on growth.** If a future change spends a project's bill early, EC4 shows it.
4. **Known leaks that become visible:** furniture lost when `Designate` fails (G:1200, :1239), and the save dropping cargo whose source is gone. They are recorded as found and not fixed in E1.
5. **The hauls' float cast** creates or destroys under 0.02 g per load. EC4's tolerance covers it, and the ledger does not hide it.
6. **Era refusals** mean agent-only flows never pair. EC7 starts with none pending.

## Director questions

**Q-A. What does an Earth reference value mean?**
- **(a) (recommended):** one price per resource, what a kilogram is worth to Earth, used both ways. E3 adds the launch cost per kg on top, in whichever direction the goods move. One column, and E3 owns the spread.
- (b) Separate buy and sell columns from the start.

**Q-B. Does recycled water count as production?**
- **(a) (recommended):** no. Greywater is netted, so Water Used at W:4906 is drawn minus returned, and `DrawPool` records nothing for water. A later market sees the water really lost, with no phantom supply.
- (b) Yes, gross. Drinking is Used, and the return is Made.

---

# PROFESSOR REVIEW

Independent adversarial review against HEAD `df3e479` (2026-10-22). Recorded as returned.

Verdict: **CLEARED WITH CONDITIONS**

Line numbers are at HEAD df3e479. Files are under /Volumes/Unreal/red_hope/red_hope/Source/RedHopeSim/Private/ unless a path is given. W is RHSimWorldSubsystem.cpp, G is RHSimGrowth.cpp, C is RHSimCommandlet.cpp.

I checked the call-site table line by line against the code, and it holds apart from the gaps below. These also hold:
- The C5d block really is the last thing in the save (W:13679-13695).
- The rounding is an identity on every lot and bill the battery makes, including Greed's `SpareParts:4` / `Ice:10` at C:1103.
- There are 18 slots.
- G:475 is the right place to roll.
- Nothing reads save bytes counting back from the end of the file.

## REQUIRED CHANGES

**1. EC6 can never pass the battery.**
- Defect: the design resets a bad ledger block "with an Error", and EC6 triggers that on purpose ("an unsorted file resets with an Error").
- Evidence: `rh_battery.sh` `run_one` marks a suite red if `grep -c "LogRedHopeSim: Error"` is not 0. No suite today logs an Error on purpose. W:13737-13738 sets the convention: "a refusal through OutError, never an Error line". D5's warmth repair uses a Warning.
- Fix: reset the ledger with a Warning. EC6 asserts the reset, not an Error line.

**2. Two haul-loss sites cannot name the resource.**
- Defect: `NoteHaulCargoLost(double Kg)` has no resource parameter (RHSimWorldSubsystem.h:897). Its callers are RHRobotTaskProcessor.cpp:107 and RHRobotStateTree.cpp:370. In `HaulUnload`'s task-gone branch (W:12590-12593) there is no task left to read `T->Resource` from.
- `ReturnResource` cannot carry it. It is the rover switch: callers pass `bRover ? Task.CargoResource : NAME_None` (RHRobotStateTree.cpp:275, :283; RHRobotTaskProcessor.cpp:283, :291). A non-None value turns a hauler's loss into a return, which is a behaviour change.
- Fix:
  - Add an `FName` resource parameter to `NoteHaulCargoLost`.
  - Add a separate cargo-resource parameter to `HaulUnload`.
  - Update the four robot callers. The fragment already holds `CargoResource` for haulers (RHRobotStateTree.cpp:96, :112; RHRobotTaskProcessor.cpp:186, :201).
  - Say what the harness callers that pass `(Id, Kg)` record.

**3. One Made site is missing.**
- Defect: `ExtendShaftOf` adds SpoilChemicals at W:8227. That is the same flow as W:8279, which the table does hook.
- Callers: `ExtendShaft` (W:8208), which the RH.BoreNow cheat (Source/RedHope/Private/RHCheats.cpp:1764) and test/scale setups use, and `Debug_SinkShaftAt` (W:7640).
- An EC4 fixture that bores a shaft with crops live would break conservation for no real reason.
- Fix: record W:8227 as Made, the same as W:8279.

**4. The list of version lines is wrong and incomplete.**
- W:14018 is TN1 in `-tunnels` (that suite starts at C:13760), not `-shafts`. The `-shafts` line is SH12 at C:13670.
- Missing lines:
  - X15 C:10824
  - K6 C:12557
  - TN8 C:14457
  - TR18 RHSimTransitSuite.cpp:1742
  - DP11 RHSimDepthSuite.cpp:831
  - F16 C:12103 (the literal "v47")
- K6 C:12528 also has a literal "v47" in its text.
- D5 counted 13 version lines (traffic 2, deep 3, framing 1, farice 2, shafts 1, tunnels 2, transit 2). DP11 adds 2, so E1 moves 15.
- Fix: list all 15 by suite and file:line, and keep the grep as a cross-check. "Nothing else may move" can only be checked against a correct list.

**5. EC7's window bar has no room for one batch.**
- Defect: the window is 3 sols (`ProductionWindowSols`, docs/data/RH_Config.csv:216). EP4 makes 1,500 kg of Struct against 1,475 kg in era: 6 Forge batches a sol, and the two bands are one 25 kg batch apart (C:11191).
- One batch is 25/450 = 5.6% of a 3-sol window. Regolith Used is the same: 100/1,800 = 5.6%.
- A one-batch phase offset can fail EC7 by construction.
- Fix: before the build, pick one:
  - a per-leg window tolerance of max(5%, that leg's largest batch);
  - an 8-sol window;
  - the 5% bar on the 10-sol lifetime only, with the window printed as information.

**6. EC7's colony is underspecified.**
- Defect: EP4 is a Forge, the Lander and Regolith_A, with only excavators and haulers played (C:11097-11180). Six colonists need a rated floor or hab (cells, a circulator, air). The garden needs zoned cells, soil and seeds. Ice needs a designated deposit and a Water Plant on the same level.
- Era moves ice instantly (W:1018). The agent band waits on two harness haulers that are also carrying regolith.
- The design has no measured number for any leg except the two EP4 already proves.
- Fix:
  - Specify positions, levels, housing and robot counts.
  - Use grow-lit beds, not greenhouses. A greenhouse samples sunlight once a step, so 0.1 s and 60 s steps integrate dawn and dusk differently.
  - Prototype EC7 and put the numbers in the design.
  - State that a missed leg is reported to the director, not fixed by re-tuning the fixture.

**7. EC1 cannot see a read that changes state.**
- Defect: resource maps are saved in TMap order (`SerializeResourceMap`, W:50-72), and `HasTradeLot` / `SpendTradeLot` walk TMap order (W:9457-9508). A before-read written with `FindOrAdd` inserts a key, which changes both.
- EC1's twins run the reads in both runs and only switch off the ledger writes, so its digest misses this.
- Fix: new reads must use `Find`, `FindRef`, `GetStock` or an existing reference, never `FindOrAdd` or `[]`. EC1 also compares the twins' save bytes up to the start of the E1 block.

**8. The growth-completion hook cannot be computed at G:499.**
- Defect: "the bill minus what the call puts back" needs values G:499 cannot see. `Unplaced` is local to `CompleteSurfaceModule` (G:1193-1205) and `CompleteFloorCells` (G:1234-1244). `CompleteCivicRoom` passes `FurnishGrowthRoom(...)` straight into `CreditLander` (G:1301, :1323).
- Fix:
  - Give `CompleteGrowthProject` an out-parameter holding the credited map and the O2 put into air.
  - Split the two inline calls into a local followed by `CreditLander`.
  - On success only, record Used = bill - credited - air.

**9. Q-B blocks the build.** EC3, EC4 and EC8 all depend on the answer (W:4906 and the water draw at W:4697). The director must answer before building starts.

## OBSERVATIONS (non-blocking)

1. "Each part was spent in full" is not exact. `HasTradeLot` passes at Have + 1e-4 >= Need (W:9464), and `SpendTradeLot` stops at Remaining <= 1e-4 (W:9486, :9494). Recording the three nominal parts goes against the design's own "never nominal" rule. The gap is at most 0.1 g a line, well inside EC4's 1 g.
2. The crossing step is credited to the old sol only for systems that run before `StepCityGrowth` (W:645, :817). Trade, Earth, the quota and power run after the roll, so they go to the new sol. Both bands do the same, so parity holds; fix the wording.
3. The goal says the production ledger "grows". The design builds a second ledger beside C5d instead. That is the right call for inertness, but say it to the director in plain words. Under Q-B (b) the two ledgers would disagree on Water Made.
4. The design should mention re-cutting the Midgame and Late checkpoints. The strict version bump refuses them.
5. "Every `Debug_*` helper is not recorded" is too broad. `Debug_StepSim` and `Debug_PlaceInstant` run hooked code. The true rule: helpers that write holdings directly record nothing (W:2779, W:3649, W:3764-3768).
6. These edge cases would show up in EC4. Leave them unfixed in E1:
   - `CompleteCivicRoom` fills the floor's air (G:1294), and if `DesignateRoom` then fails, G:505 refunds the whole bill, O2 included. The code comments call this unreachable.
   - The crisis drain adds water if the stock is ever negative (W:10136).
   - `ComputeSalvage` drops held amounts of 0.1 g or less (W:10410, :10417).

VERDICT: CLEARED WITH CONDITIONS (required changes 1-9 above)

---

# REVISION

This section replaces the design wherever they differ. The builder's order of authority: the director's answers, then this REVISION, then the PROFESSOR REVIEW, then the design. Every line number is at HEAD `df3e479`. Rebase onto D6's code when it lands (risk 2).

**R1. A bad block resets with a Warning, never an Error** (required change 1). In section 2, "Load", these cases log a **Warning** and reset the ledger to empty: an unsorted name, a duplicate name, a ring index out of range, or a negative ring entry. A name with no slot also logs a Warning and is skipped. The load still succeeds. No path in E1 logs an Error. EC6 asserts the reset itself (every leg is 0 and the fill is 0) and that the colony loaded. It does not assert a log line. This follows W:13737-13738 and D5's warmth repair.

**R2. Every haul loss names its resource** (required change 2).
- `NoteHaulCargoLost(double Kg)` (RHSimWorldSubsystem.h:897) becomes `NoteHaulCargoLost(FName Resource, double Kg)`. Its body still adds to `HaulLostKg` exactly as now, then calls `NoteFlow(Resource, Used, Kg)`. The two callers pass `Task.CargoResource`: RHRobotTaskProcessor.cpp:107 and RHRobotStateTree.cpp:370.
- `HaulUnload` gains a last parameter, `FName CargoResource = NAME_None`. It does not take part in `ReturnResource`'s rover test. That test and its four callers' `bRover ? Task.CargoResource : NAME_None` stay exactly as they are (RHRobotStateTree.cpp:275, :283; RHRobotTaskProcessor.cpp:283, :291). The same four callers also pass `Task.CargoResource` as the new argument.
- Recording:
  - In W:12582, where the task exists, the resource is `T->Resource`.
  - In W:12592, where the task is gone, it is `CargoResource`.
  - V:275, a rover load lost with no Lander, uses the resource `ReturnHaulCargo` already holds.
- **Harness callers** call `HaulUnload(Id, Kg)` with no resource (EP4 at C:11150-11160, and the `-farice` and `-transit` harnesses). They lose nothing in the task-exists branch. If one ever reaches W:12592 with `NAME_None`, the kilograms go to `UnslottedLostG`: a single int64, saved as the last field of the E1 block, and read only by tests. EC3 pins that path. EC4 asserts `UnslottedLostG == 0` in its fixtures.

**R3. Add the missing Made site** (required change 3). Add a row to the call-site table: `ExtendShaftOf`, W:8227, SpoilChemicals, Made, both bands. It is the same flow as W:8279. EC3 gains a fixture: an `ExtendShaft` with crops live records the SpoilChemicals it adds.

**R4. The exact list of version lines** (required change 4). This replaces section 4's list. There are 15 check lines; each prints one log line that moves, which is 30 lines in `diff_battery.py`'s output.

| Suite | Check lines |
|---|---|
| `-traffic` | X15, C:10817 and C:10824 |
| `-deep` | K6, C:12513, C:12528 (literal "v47") and C:12557 |
| `-framing` | e5, C:14947 |
| `-farice` | F16, C:12097 and C:12103 (literal "v47") |
| `-shafts` | SH12, C:13670 |
| `-tunnels` | TN1, C:14018; TN8, C:14457 |
| `-transit` | `RHSimTransitSuite.cpp:1595`; TR18, `:1742` |
| `-depth` | DP11, `RHSimDepthSuite.cpp:786-787` and `:831` |

- DP11 at :787 also prints both whole-file sizes, which grow by the E1 block on the same line.
- Add whatever version lines D6 introduces.
- The builder cross-checks with `grep -n "Ver == 47\|/47\|v47\|V46\|v46"` over the three suite files at HEAD. Any line found that is not in this table is a finding to report.
- Beyond these lines: every `(N bytes` line (masked), and the new `economy.log`. Nothing else.

**R5. EC7's bar** (required change 5). Two numbers are barred at 5% of the larger band's value, per slot and leg: (i) the 10-sol `LifetimeG` delta, and (ii) the sum of the whole 8-sol ring at the end of the run. The 3-sol `ProductionWindowSols` window is printed as an information line and is not barred. One Forge batch is 25 kg against about 12,000 kg of Struct in 8 sols, so 0.2%: a one-batch phase offset cannot fail the bar. Any leg under 10 kg in both bands must agree within 1 kg.

**R6. EC7's two colonies, fully specified** (required change 6). Each pair loads one save and runs 10 sols in each band.

- **EC7a, dig and Forge.** EP4's colony and harness exactly (C:11097-11180): the Forge beside Regolith_A, the Lander, `RH_Robots`' starting excavators and haulers, played through `TryClaimDig`, `DigDeposit`, `TryClaimHaul`, `HaulLoad` and `HaulUnload`. Barred legs: Regolith Made and Used, Struct Made. Holdings include the harness haulers' `CargoKg`.
- **EC7b, life support. No robots and no hauling, so no harness enters.**
  - **Housing.** Floor -1: 12 cells via `Debug_PaintCarve`, rated with `Debug_ForceRateFloor(-1)`, and 6 colonists via `Debug_AddColonists`, homed on -1.
  - **Garden.** Two grow-lit garden cells on -1 (not greenhouse beds), zoned through the same `Designate` command the `-garden` suite uses. Soil and Seeds are stocked in the pool for both cells.
  - **Surface machines.** An Electrolyzer and a Water Plant, placed with `Debug_PlaceInstant`. The Water Plant's ice hopper is pre-filled with `Debug_AddSolidAt` for 10 sols of batches, so neither band moves ice.
  - **Power.** Held by `Debug_SetExtraGenW` (D5's runtime-only hook) so the bank never empties and nothing sheds.
  - **Barred legs:** Oxygen Made and Used, Water Made and Used, Food Made and Used, Hydrogen Made, and Ice Used.
- **Prototype first.** The builder builds EC7 before any other E1 check. The measured numbers go into the design-decisions entry: both bands, every barred leg, and the percentage. This session could not run anything.
- **A miss is a finding.** If a leg misses 5%, the builder reports it to the director with the numbers. It is not fixed by re-tuning the fixture, the colony or the bar.

**R7. Reads never insert** (required change 7).
- Every before-read a hook adds uses `Find`, `FindRef`, `GetStock` or a reference the code already holds. It never uses `FindOrAdd` or `operator[]`. A new key would change TMap order, and with it `SerializeResourceMap` (W:50-72) and the `HasTradeLot` / `SpendTradeLot` walk (W:9457-9508).
- EC1 also saves both twins after 10 sols and compares their bytes from 0 up to the start of the E1 block. The block's size is fixed by the slot count, so the start is the file size minus that size.

**R8. Growth completion gets the numbers it needs** (required change 8).
- `CompleteGrowthProject(const FRHGrowthProject& P, FString& OutWhy)` (G:1132) gains a third parameter, `FRHGrowthApplied* OutApplied = nullptr`. `FRHGrowthApplied` holds `TMap<FName, double> Credited` and `double AirO2Kg`.
- Inside, the `CreditLander` calls at G:1205 and G:1244 add their `Unplaced` maps to `Credited`.
- G:1301 and G:1323 become a local (`const TMap<FName, double> Unplaced = FurnishGrowthRoom(...)`), then `CreditLander(Unplaced, ...)`, then the add to `Credited`. Same call, same arguments, same order.
- The O2 writes into air at G:1190, G:1231 and G:1294 add the amount they write to `AirO2Kg`.
- At G:499, on success only, for each line of `Done.Bill`: Used = bill - Credited - (AirO2Kg if the line is Oxygen), floored at 0. The refused branch at G:505 records nothing.

**R9. Q-B must be answered before the build** (required change 9). Both branches are written out, so only the answer is needed:
- **(a), recommended.** `DrawPool`'s water draw at W:4697 records nothing for Water. At W:4906 record Used Water = `WaterDrawnThisStep x (1 - GreywaterReturnFraction)`. Crop water at W:4327 stays Used. Food at W:4696 stays Used.
- **(b).** W:4697 records Used = drawn. W:4906 records Made = the return.
- EC3, EC4 and EC8 follow the answer.

**Observations taken in.**
1. **The convoy split is nominal, by exception.** `HasTradeLot` admits a lot 1e-4 kg short (W:9464), and `SpendTradeLot` stops at 1e-4 (W:9486, :9494). So the three parts are recorded as their nominal, gram-rounded amounts. That error is at most 0.1 g a line, well inside EC4's 1 g. It is the only nominal record in E1, and the rule in section 1 now says so.
2. **Section 5's wording is corrected.** At the threshold step, what runs before `StepCityGrowth` (W:645, :817) is credited to the old sol. That covers production, habitability, agriculture, population and growth. What runs after it (trade, Earth, the quota, power) goes to the new sol. Both bands run the same order, so parity holds.
3. **For the director in plain words.** The old three-leg ledger (C5d) stays as it is, because settlers and the Big Arrival read it. E1 adds a second ledger beside it, which no rule reads. If Q-B is (b), the two ledgers will disagree on how much water is "made".
4. **Checkpoints.** The version bump refuses the Midgame and Late checkpoints, so the builder re-cuts them (`rh_checkpoints.sh all --force`) and loads both, as D5 did.
5. **The Debug rule, restated.** A helper that writes holdings directly records nothing (`Debug_AddSolid` W:2779, `Debug_ForceRateFloor` W:3649, `Debug_BuildScaleColony` W:3764-3768). A helper that runs hooked sim code records what that code records (`Debug_StepSim`, `Debug_PlaceInstant`). Fixtures take their "before" snapshot after setup.
6. **Edge cases, added to risk 4.** None is fixed in E1, and EC4's fixtures avoid them:
   - a civic room whose `DesignateRoom` fails after the air fill (G:1294, then G:505);
   - the crisis drain on a negative stock (W:10136);
   - `ComputeSalvage` dropping 0.1 g or less (W:10410, :10417).

**The verdict after revision:** CLEARED WITH CONDITIONS. R1-R8 are written in. R9 waits on the director's answer to Q-B. R6 waits on the builder's prototype numbers. Q-A also shapes E1's data. Under (a) there is one column, `EarthValue_mcr`. Under (b) the builder adds a second, `EarthSellValue_mcr`, with the same placeholder numbers. So Q-A also needs an answer before the CSV is edited.
