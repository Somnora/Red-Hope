# D5, depth has a price: design (2026-10-21, draft for adversarial review)

Step 2 (going deep), gate 5. Design only. No code is written.

**Anchors.** Code is read at HEAD `218f672` with `git show HEAD:`. D3 is committed (save v45). D4 (lifts, robots below, save v46) is being built in this tree now, so every `RHSimWorldSubsystem.cpp` anchor below moves when D4 lands. The builder re-anchors after D4, as D4 did after D3.

**Sources:** `going-deep-plan.md` (the D5 row `:39`, the D5 risks `:80-84`, the D8 bar `:98`, Q3a); the D1, D2 and D3 REVISIONs; `d4-transit-design.md` (REVISION, DIRECTOR ANSWER, and R9: lift power was deferred "to D5's cooling power"); D3's rejected O6 (shelter from wear below, "best taken with D5's depth costs").

**Goal (Q3a).** Deeper floors are slower to dig and warmer, so they need cooling power. They are better shielded. Floors -1 to -5 keep today's numbers. `MaxDepth` goes from 5 to 20 (placeholder).

**What you see.** You can bore to -20. Each floor past -5 takes longer to bore and carve. Each carved cell past -5 adds a cooling load to the grid. If the grid cannot carry it, that floor slowly runs warm and its residents' Hope dips, and it cools again when power returns. People who live past -5 get a small Hope bonus for shelter.

**Decisions at a glance.**
1. One data row draws the line: `DepthCostFromFloor` = 5. Every new cost and gain applies only to `Level < -DepthCostFromFloor`. Above it, every branch takes today's lines.
2. Dig time is an integer per-mille multiplier from data, applied when a batch starts.
3. Cooling is a grid load per deep floor, in integer milliwatts, shed like any load. A shed floor gains `Warmth` (0..1), linear in dt. Warmth is a Hope penalty only. It never touches the rating (Q-B).
4. Shielding buys a capped, bonus-only Hope term for residents homed past -5 (Q-A).
5. Save v47 adds one small warmth block. Everything else is derived.

---

## 0. Facts the design rests on (HEAD `218f672`)

- `MaxDepth` is the data row `RH_Config.csv:35` (5), read at `RHSimWorldSubsystem.cpp:124` with no clamp, held at `.h:2422`, exposed by `GetMaxDepth()` `.h:526`.
- The Borer starts a batch at `:1526` (bore, item `(0, -(Depth+1))`), `:1538` (carve, `(1, L)`) or `:1543` (a tunnel front; the floor is the tunnel's `Level`). It sets the hours at `:1570` (`B.BatchRemaining_h = Work->BatchTime_h`) and buys H2 at `:1556` (`H2BurnKgPerHour * BatchTime_h`). Recipes: `BoreFloor` 12 h, `CarveCell` 3 h (`RH_Recipes.csv:9-10`).
- Growth sets durations at `RHSimGrowth.cpp:706` (`GrowthCellSols`, floor homes) and `:1114` (`GrowthRoomSols`; a carving civic room sets `bCarve` at `:1067`).
- `StepPower` (`:8176-8279`) builds one `FLoadEntry` per building (`:8187`), sums them (`:8234`), and when the bank is empty sheds lowest `LoadPriority` first (`:8240`, an unstable `Sort`). It runs last in both spines (`:585`, `:756`). Priorities: Forge 2, Electrolyzer 4, Compute/Lab 5, IceDrill 6, WaterPlant 7, AirFilter and Infirmary 8, ChargePad 9.
- The Hope total is one expression (`:6647-6649`). C6a appended `CommutePenalty` as the last subtraction so a `+0.0` keeps Total bit-identical (`.h:982-985`). That is the pattern here.
- `GetRadiationAtLevel` (`:2422-2430`) is `1.0 x 0.05^depth`. The baseline log prints it as `%.4f`: floor -4 already reads `0.0000` (`battery_post_d3/baseline.log:212`). Flares spike only `Level >= 0` (`:2432-2446`). The only modelled flare effect is robot wear (`AccrueWear`, `:12528-12541`), which ignores level today.
- Depth already costs commute: floor-to-surface is `|L| x 6000 cm` (`RHSimCommute.cpp:194-210`). A home on -20 with a surface seat is 120,000 cm, four times the 30,000 long line. D5 adds nothing to that.
- No sickness or dose model exists. The `.h:1843` comment "M2's human layer reads this for health" was never built.
- **Name clash.** Old code already uses "D5a" (day shift, v33, `:1375`, `:1399`, `:8181`). New comments and logs say "D5 depth", never bare "D5".

## 1. Raising MaxDepth to 20

**The row.** `MaxDepth` 5 becomes 20. Load clamps it to `1..RH::MaxDepthCap` (30), because the light key packs one bit per floor into a `uint32` (`RHColonyVisualizerSubsystem.cpp:4016-4019`). The CSV edit is live-synced into DT_Config in the same change.

**Every reader, and what happens:**

| Site | Today | D5 |
|---|---|---|
| Floor walks `StepHabitability` `:2757-2760`, seats `:3614`, `:5865` | floor table, `ensure(Level >= -MaxDepth)` | no change |
| Save refusals: floor table `:5439-5441`, shaft queue `:5572`, `:5584`, shaft depth `:5602-5604`, tunnel `:8027-8029` | range from `MaxDepth` | no change; the printed range reads `-1..-20` (section 5) |
| Orders: `ExtendShaftOf` clamp `:8104`, Bore clamp `:8868`, Excavate `:8890`, `CanDigTunnel` `:7745`, building level `:8380`, district paint `RHSimDistricts.cpp:327` | clamp or refuse at 5 | clamp or refuse at 20 |
| Deck shaft strip `RHCommandDeck.cpp:152` (fallback 5), `:289`, `:346-349` | 6 static rows | rows to `MaxDepth`, windowed (section 9); the fallback becomes `RH::MaxDepthCap` |
| Bore label `RHCommandDeck.cpp:2256` | "Bore (max)" past 5 | past 20 |
| Borer card floor list `RHCommandDeck.cpp:1559` | walks 5, prints non-empty | walks 20, prints non-empty |
| View clamp `RHPlayerController.cpp:2122-2133` | clamps to reached depth | no change (it never used `MaxDepth`) |
| Floor tiles `RHColonyVisualizerSubsystem.cpp:3460`; light mask `:4016` | walk 5 | walk 20; under the cap |
| Cheats `RHCheats.cpp:632`, `:1584`, `:1787`, `:1806`, `:2478` | walk 5 | walk 20 (console only) |
| Baseline radiation print `RHSimCommandlet.cpp:16254` | 6 lines, 0..-5 | bounded to `-Min(MaxDepth, DepthCostFromFloor)`: the same 6 lines (section 5) |
| -percapita `:5808`, -scale `:15055` fixtures | `constexpr Floors = 5` | unchanged |

No sim array is sized by `MaxDepth`. Every per-floor store is a `TMap` or the floor table. No test bores past 5 (checked: every `ExtendShaft`, `BoreOrder` and `ExcOrder` in the commandlet uses 1-5).

## 2. The dig-time curve

- Rows: `DepthCostFromFloor` 5, `DigPermillePerDeepFloor` 150 (placeholders, integers).
- `DigPermille(L) = 1000 + DigPermillePerDeepFloor x max(0, -L - DepthCostFromFloor)`, an `int32`. -6 is 1150, -10 is 1750, -20 is 3250.
- At batch start (`:1556-1570`) the Borer computes `BatchH`:
  - when `DigPermille(L) == 1000`, `BatchH = Work->BatchTime_h`, today's expression, so shallow work is bit-identical;
  - otherwise `BatchH = (double)Work->BatchTime_h * DigPermille(L) / 1000.0`.
  - `L` is `WorkItem.Y` for bore and carve, and the tunnel's `Level` for a tunnel front.
- `BatchH` feeds both `BatchRemaining_h` and the H2 bill, so a deep batch on H2 buys its longer run. The start log prints `BatchH`; for shallow work that is the same number, so the text is unchanged.
- Bore -6 takes 13.8 h; bore -20 takes 39 h. Carve -6 takes 3.45 h; carve -20 takes 9.75 h. Spoil per floor and per cell is unchanged.
- Growth: a floor-homes project (`RHSimGrowth.cpp:706`) and a carving civic room (`:1114` with `bCarve`) on a deep floor scale their `TotalSols` the same way. Others are untouched.
- `ExtendShaft` and `ExcavateFloor` (instant debug and harness paths) take no time and stay as they are.
- No new saved state: `BatchRemaining_h` and project sols are already saved.

## 3. Heat: cooling load, shortfall, Warmth

**The load.**
- Row `CoolMilliWPerCellPerDeepFloor` 1000 (placeholder).
- A floor's cooling is `int64 Cells x max(0, -L - DepthCostFromFloor) x row` milliwatts, as `W = mW / 1000.0`. It counts every carved cell on a connected floor below the line, rated or not: the rock warms the workings, not just the homes.
- 100 cells on -6 need 100 W; 100 cells on -10 need 500 W; 210 cells on -20 need 3,150 W (a solar array peaks at 300 W).

**In `StepPower`.**
- After the building pass, one entry per deep floor is appended, in floor-table order. `FLoadEntry` gains `int32 CoolLevel` (0 for buildings, `B == nullptr` for cooling). Pass 2 sets `bPowered` only for buildings and a `bServed` flag for cooling.
- Priority: row `CoolingLoadPriority` 7. The comparator becomes (Priority ascending; then buildings before cooling; then deeper floors first). For two buildings it returns exactly what `:8240` returns today, so a colony with no deep floor sorts identically.
- So a dry bank sheds the Forge, Electrolyzers, labs, ice drills and water plant first. Then it sheds deep floors' cooling, deepest first. Air filters and charge pads stay on longest.
- A shed cooling entry counts in `ShedCount`, so the deck's brownout readout and the dimmed lights already react.

**Warmth (the shortfall).**
- Per deep floor, `double Warmth` in 0..1, kept in `FloorWarmth` (TMap by Level, region 0).
- In `StepPower`, after shedding: served, `Warmth -= dt / (HeatFallSols x 1200)`; shed, `Warmth += dt / (HeatRiseSols x 1200)`; then clamp. Rows: `HeatRiseSols` 1.0, `HeatFallSols` 0.5. A whole sol unserved makes a floor fully warm; half a sol of power makes it comfortable again.
- **It never touches the rating, the air, beds, arrivals or recall** (Q-B's recommendation). A warm floor is recoverable by power or storage alone.
- **Hope.** `WarmPenalty = HopeWarmMax x min(1, sum_L(Residents_L x Warmth_L) / Pop)`. Row `HopeWarmMax` 5. Floors are summed in floor-table order. It is appended as the last subtraction after `CommutePenalty`. When no floor has `Warmth > 0` (a flag kept by `StepPower`), it stays `+0.0` and nothing is counted.
- **Alerts** are edge-triggered with hysteresis: on at Warmth >= 0.25, off at 0. Placeholder, prevention-framed copy: "Floor -8 is running warm: its cooling needs power. More generation or storage brings it back to comfort." and "Floor -8 is comfortable again." No harm imagery. These go to the Gate-D wording table as new rows.

**Lift power stays deferred** (D4 R9). A lift draw would move D4's -transit numbers and the `lifts=1` playtest on floors -1 to -5, which this gate must not touch. It is logged as a deviation.

## 4. Shielding: what "better shielded" gives

- The radiation index cannot be the payoff. It already prints `0.0000` from -4 down, and flares spike only the surface. Nothing past -5 can improve it.
- **Recommended (Q-A a): a Shelter term in Hope.** `Shelter = HopeShelterMax x DeepResidents / Pop`, bonus-only, capped by construction. Row `HopeShelterMax` 3. `DeepResidents` counts colonists whose `HomeLevel < -DepthCostFromFloor`, in one pass over `Colonists` (integers, order-free).
- It is the Views pattern (`HopeViewsMax`, bonus-only). It is appended last in the addition chain after `Solidarity`. While no floor past the line has cells, it is skipped and stays `+0.0`.
- Card copy (placeholder): "Deep homes: shelter from the sky." Prevention-framed.
- Option (b) is D3's O6: robots below ground ignore the flare and storm multipliers in `AccrueWear` (`:12528-12541`). It is already modelled, but it changes floors -1 to -5 once D4's robots go below.

## 5. Inertness

**Why floors -1 to -5 do not move.** Every new branch tests `Level < -DepthCostFromFloor`, a deep floor with cells, or a non-zero Warmth. With none of these, dig hours take today's expression, `StepPower` appends nothing and sorts the same array with the same answers, Hope adds and subtracts `+0.0`, and the save writes a 4-byte zero block (hidden by the `(N bytes` mask, `diff_battery.py:11`).

**Suites that could notice MaxDepth itself**, checked against `Saved/baselines/battery_post_d3` (48 logs; grep for `0..5`, `-1..-5`, `radiation level`, `max depth`, `deepest`):
- `baseline.log:208-213`, the radiation print. It is kept identical by the print bound in section 1. The lines it would add would all read `0.0000`. `-depth` prints the full profile in `%.3e` instead. If the reviewer prefers, the alternative is 15 named added lines.
- `deep.log:594`, K6's refusal text "outside -1..-5" (`RHSimCommandlet.cpp:12539`). It becomes "-1..-20". The assertion checks only "floor table", so it stays green. 1 out, 1 in.
- `tunnels.log:1710`, TN8's "outside -1..-5 or reached by no shaft" (`:14435`). It becomes "-1..-20". The assertion checks "reached by no shaft". 1 out, 1 in.
- No other log prints `MaxDepth`. `shafts.log:627` prints "the deepest is 2" (shaft depth, not the row). The shaft strip, Bore label and Borer card are deck-only. The playtests bore to 3 at most (`RHPlaytest.cpp:753`, `:1815`) and never read `MaxDepth`.

**The bar.** Against `battery_post_d4`: the D4 version lines (11, plus whatever `transit.log` prints) move from 46 to 47, plus the 2 MaxDepth-text lines above. Recount literally at build time; any other line fails the gate. The default and `districts=1` playtests are byte-identical under the mask. `-RHCommuteAudit` margins are unchanged.

## 6. Era parity

- Both spines call the same `StepProduction`, `StepPower` and `GetColonyHope`. There is no band test anywhere in D5.
- The cooling energy is `(GenW - LoadW) x dt / 50`, which is linear in dt. Warmth moves by a rate times dt, which is linear between clamps.
- The batch hours are fixed at the start, so the dig rate is the same per sim-second in both bands.
- The one honest gap: when the bank scrapes empty, shedding alternates step by step (the comment at `:8259-8262` says so). The agent band alternates every 0.1 s, the era band every 60 s. Warmth integrates the shed share of time, so both converge. DP9 pins it within 0.05.
- No new era refusal.

## 7. Save v47, strict

- `RHSaveVersion` 46 (D4) becomes 47, with the comment chain extended. If D4 takes another number, D5 takes the next free one.
- **The warmth block** goes straight after D4's transit block, so K6's offset 20, SH12's `BlockAt`, TN1's read and D4's block do not move. It is read into locals before teardown.
  ```
  int32 WarmCount;   per: int32 RegionId, int32 Level, double Warmth   (Level descending; only Warmth > 0)
  ```
- The default colony writes 4 bytes: `0`.
- **Structural refusals** ("Save warmth block invalid: …", `OutError`, no Error line): a count below 0 or above `RH::MaxDepthCap`; `RegionId != 0`; a level outside `-MaxDepth..-1`; a level with no loaded floor; repeated or out-of-order levels; Warmth NaN, `<= 0` or `> 1`; a count larger than the bytes left.
- **Warning repair:** a level at or above `-DepthCostFromFloor` (the row changed between save and load) is dropped with a Warning.
- Derived, not saved: cooling W, served flags, `DigPermille`, Shelter, the warm flag and the alert latch (re-armed from Warmth at load).

## 8. Tests

**New fails-loudly `-depth` suite** (battery: 49 + 1 = 50 runs). Each check prints its numbers. Every mutation listed is run once by hand, fails its check, and is named in the commit.
- **DP0. Rows.** `MaxDepth` 20, line 5, `DigPermille(-5)` 1000, `(-6)` 1150, `(-20)` 3250; every new row > 0; a `MaxDepth` override of 40 clamps to 30. Print the full radiation profile 0..-20 in `%.3e`.
- **DP1. Twin inertness.** Two colonies bored to -5, floors carved, ramp open, a scraping bank. One runs with the D5 rows forced to 0 by a Debug override. Over 3 sols they are bit-identical: stocks, `LoadW`, `ShedCount`, Hope, every batch hour. (Mutation: drop the `== 1000` branch.)
- **DP2. Bore.** Orders to -7: `BoreFloor` -6 starts at 13.8 h and -7 at 16.2 h, exactly. On H2 the fuel bill scales too.
- **DP3. Carve.** A carve on -6 takes 3.45 h. A tunnel front on -6 is scaled by the tunnel's level. A growth floor-homes project on -6 takes `1.15 x GrowthCellSols`, and one on -5 is unchanged.
- **DP4. Load.** 100 cells on -6 add exactly 100 W to `LoadW`; on -10, 500 W. Cells on -1..-5 add 0.
- **DP5. Shed order.** Empty bank, short generation: WaterPlant sheds before cooling, cooling before the AirFilter, -10 before -6. (Mutation: comparator without the tie rules.)
- **DP6. Warmth.** One sol unserved: Warmth 1.0 (`HeatRiseSols` 1). Power back: 0 after half a sol. `WarmPenalty` equals `5 x share`, and after recovery Total equals the twin's bit for bit.
- **DP7. No harm.** A fully warm, populated floor stays rated. Its departure accumulators, beds, arrivals and recall timers equal the twin's. The alert strings pass the Gate-D banned-word check.
- **DP8. Shelter.** 10 of 40 residents homed on -6: `Shelter = 3 x 0.25`. Homed on -5: 0 and `+0.0`.
- **DP9. Era parity.** A paired 10-sol run (era and agent, fix-A method) with a deep floor and a scraping bank. Warmth agrees within 0.05; Hope, Dug and Struct within 5%.
- **DP10. Limits.** Bore 25 clamps to 20. Excavate on -21 is refused. Building on -21 reads "No such floor". `ExtendShaft(20)` reaches -20. A floor table with -20 loads; one with -21 is refused ("-1..-20").
- **DP11. Save.** The warmth block round-trips byte-equal. Every refusal in section 7 is hit by a hand-made file. The Warning repair keeps the colony. A v46 file is refused.
- **DP12. Depth is far.** A -20 home with a surface seat is 120,000 cm (the existing commute rule, pinned).

**-scale at 20 floors.** A new block runs after every existing block, so the 5-floor digests and budgets above it do not move. It extends the scale city's shaft to -20, carves and rates 210 cells on each of -6..-20 (3,150 more cells; 25,200 W of cooling), and moves 2,000 colonists to deep homes. The agent step, era step, `GetColonyHope`, save and load must stay within the existing budgets; `StepPower` with 15 cooling entries is timed separately (<= 0.05 ms).

## 9. Presentation (RedHope; const accessors only)

- **Shaft strip at 20 floors.** Rows are built for `-1..-MaxDepth` inside an `SScrollBox` capped at 11 rows. A row is visible when its floor is reached, or is the next one to bore. Solid rock below that collapses. Choosing a floor scrolls its row into view. PageUp and PageDown step one floor through `SetActiveLevel`, which keeps its reached-depth clamp.
- **Floor labels.** Floors past the line show their cooling (`-8 ▦120 ●  ❄240 W`). A warm floor shows an amber `≈` and its share. The rows keep today's text from -1 to -5.
- **Heat readout.** The power panel gains "Cooling: N W for k deep floors". The circulator and floor card gains "DEPTH: floor -8, 3 below the cool line; cooling 360 W; comfortable / running warm (40%)". The Bore button reads "Bore → -7 (16 h)" past the line.
- **Floor tint.** Tiles past the line darken a step per three floors (a depth cue), and lerp toward a soft amber by Warmth. The material parameter is set only when Warmth crosses a 10% step (keyed by a `WarmthVersion`), never per frame.
- **Captures** (`Saved/RHCapture/d5`): the strip at 20 floors, a cool and a warm floor -8, and the Bore label past the line.
- **New strings** go into the Gate-D wording table after D4's rows. All of them are placeholders.

## 10. Risks I see

- **R1.** Cooling raises `LoadW`, so `RequestChargeWh`'s free-energy rule (`:12505-12508`, `GenW > LoadW`) fails sooner in deep colonies, and robots charge from the bank more often. This is intended, but it is a second, quiet cost.
- **R2.** With cooling entries present, the unstable `Sort` may order equal-priority buildings differently than in the same colony without deep floors. It stays deterministic. It is only reachable past -5.
- **R3.** Cooling counts unrated workings too, so a deep mine costs power forever. The card says so. The director may prefer rated floors only (Q-B).
- **R4.** Changing `DepthCostFromFloor` moves every baseline. Lowering `MaxDepth` refuses deep saves. Both rows say so in their notes.
- **R5.** The commute already makes depth expensive (section 0). With Shelter at 3 and the commute cost at 4, the deep city may still read as a bad trade until D4's lifts. This is balance, logged for the director.
- **R6.** The radiation-print bound (section 5) can read as hiding a line. The case for it is that the lines carry no information.
- **R7.** 20 floors of tiles is 4,200 hidden actors in the scale city. The frame-time bar belongs to D8, as the plan says (`going-deep-plan.md:98`).
- **R8.** D4 moves the anchors and may print version lines in `transit.log`. Re-anchor and recount after D4 lands.

## Director questions

**Q-A. What does shelter buy?**
- **a (recommended): a Hope bonus for living deep.** People homed below -5 lift Hope a little (up to 3 points, by their share of the colony). It is bonus-only, like windows. No existing colony changes.
- b: robots below ground no longer take extra wear from flares and dust storms (D3's O6). It is real and already modelled. But it starts at floor -1, so it changes the shallow floors once robots ride lifts.
- c: both.

**Q-B. How hard is a cooling shortfall?**
- **a (recommended): Hope only.** A floor without cooling power warms over a sol. Its residents' Hope dips (up to 5 points), and it cools again in half a sol once power returns. Nothing else changes.
- b: as (a), and a warm floor also takes no new arrivals or growth until it cools. Still nobody leaves.
- c: a fully warm floor loses its livable rating, so the usual support and recall chain runs. This is the harshest option and not recommended under the Gate-D directive.

## Deviations to log in `docs/design-decisions.md`

1. Lift power stays deferred past D5 (D4 R9), to keep floors -1 to -5 unchanged.
2. The baseline radiation print stops at the cool line (section 5).
3. Cooling counts every carved deep cell, not only rated floors (unless Q-B changes it).
4. "Better shielded" is a Hope term, not a radiation number, because the index is already zero past -4.
