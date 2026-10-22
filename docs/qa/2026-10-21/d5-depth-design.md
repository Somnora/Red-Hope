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


# DIRECTOR ANSWER (2026-10-21)

- **Q-A (a):** shielding pays as a small capped Hope bonus (up to +3) for residents homed below the cut-off, built like the window bonus.
- **Q-B (a):** a cooling shortfall costs Hope only (warmth up to -5), fully recoverable when power returns. It never touches the livable rating, arrivals or recall.


# PROFESSOR REVIEW (2026-10-22)

**Verdict: BLOCKED.** One structural defect, plus nine required corrections.

The defect: the cooling shortfall, which is the whole of Q-B's cost, is driven by a signal that alternates every step. As specified, Warmth can never build up in a colony that has a Lander. Most of the rest is sound:
- the cut-off holds for floors -1 to -5;
- `MaxDepth` has no array sized by it;
- the dig multiplier sits at the right point;
- the save block is placed correctly.

A REVISION that answers items 1-10 below unblocks the gate. Only items 1 and 2 need a second look.

Anchors are read at HEAD `31fc24c` (D4 committed at `fe232c7`, save v46, plus the buttons fix). The tree is clean.

## What I verified and found sound

- **The cut-off (attack 1).** Every new term is either skipped or `+0.0` when no floor below -5 has cells and no floor has Warmth > 0:
  - the batch hours;
  - the `StepPower` appends;
  - Shelter, appended after `Solidarity`, and the warmth penalty, appended after `CommutePenalty` (`RHSimWorldSubsystem.cpp:6695-6697`);
  - the 4-byte save block.

  For two buildings, the comparator returns what `:8296` returns today. With no cooling entry the array and the sort are unchanged.
- **MaxDepth readers (attack 2).** These are the full list at HEAD:
  - sim: `:130`, `:2808`, `:3662`, `:5487-5489`, `:5620`, `:5632`, `:5650-5652`, `:5913`, `:7801`, `:8083-8085`, `:8160`, `:8436`, `:8924`, `:8946`; `RHSimDistricts.cpp:327`;
  - deck: `RHCommandDeck.cpp:152`, `:289`, `:346`, `:1591`, `:2354`;
  - visualizer: `RHColonyVisualizerSubsystem.cpp:3460`, `:4016-4018`;
  - cheats: `RHCheats.cpp:734`, `:1686`, `:1889`, `:1908`, `:2580`;
  - the commandlet print: `RHSimCommandlet.cpp:16425`.

  Nothing in the sim is sized by `MaxDepth`:
  - `CarveQueue` is a `TMap` (`.h:323`);
  - D4's portals walk `Sh.Depth` (`RHSimTransit.cpp:740`);
  - commute matrices are sized by districts (`RHSimCommute.cpp:288`).

  The light mask `1u << Min(L, 31)` over `L = 0..MaxDepth` uses bits 0..30 at the cap of 30, so the cap is correct. No hard-coded depth of 5 exists in the sim or presentation code (grep).
- **Dig time (attack 3).** The batch start (`:1596-1633`) is the only Borer site. Its tunnel floor needs the `FindTunnel(WorkItem.Y)->Level` lookup that the log already does (`:1622`). Growth sets durations at `RHSimGrowth.cpp:709` and `:1117` (`bCarve` at `:1070`), and both are saved. D4 does not interact:
  - the hand-off hold belongs to haulers, not to the Borer's recipe;
  - spoil drops on the surface;
  - `StepProduction` runs in both bands.
- **The save (attack 7).** `WriteTransitBlock` is the last header block (`:13183`) and is read at `:13634`. A warmth block after it moves nothing:
  - TR18 reads only its three ints (`RHSimTransitSuite.cpp:1595`);
  - no test asserts the file's end or total size.

## REQUIRED changes

1. **[BLOCKER] Warmth never accumulates. Drive it from the shortage itself, not from this step's `bPowered`.**
   - **Why the step's shed flag alternates.** `StepPower` sheds only when the step *starts* with `BatteryWh <= 0` (`:8294`). It then sheds down to `LoadW <= GenW`, so the step ends with a surplus, and `:8312-8313` puts charge into the bank. The next step therefore serves every load, drains the bank and clamps it to 0. Then it sheds again.
   - **Why GenW never reaches 0.** The Lander generates a 50 W base at all hours (`RH_Buildings.csv`, Lander row). So whenever the grid is short, the shed share of steps is at most one half, in both bands. The code already says so (`:8315-8318`), and D5a latched on the night for exactly this reason.
   - **What that does to Warmth.** It rises at `dt/1200` on shed steps and falls at `dt/600` on served steps. Each pair of steps therefore nets `-dt/1200`, so Warmth never exceeds one step's rise: 0.05 in era and about 8e-5 in the agent band.
     - The 0.25 alert never fires.
     - `WarmPenalty` stays at or below 0.25 Hope in era, and at about 0 in the agent band.
     - DP6 can be reached only by a debug force.
     - DP9 passes vacuously, and only at its boundary (0.05 against a tolerance of 0.05).
   - **Specify instead:** a floor is short in a step that is *scraping*. A scraping step either started with an empty bank and a full-load deficit, or saw the clamp at 0 fire (`BatteryWh + Delta_full < 0`). The short floors are the cooling entries in the shed prefix of the priority walk over the *full* load, computed whether or not this step actually sheds. An energy-share rule is acceptable if it is equally band-honest.
   - **Tests:**
     - DP6 reaches Warmth 1.0 through the real grid: a night powered only by the Lander, an empty bank, and cooling over 50 W. The mutation "drive from `bPowered`" must fail it.
     - DP5 reads the shed set on a step that starts with an empty bank.
     - DP9 must actually reach Warmth >= 0.25 in both bands. Its tolerance comes from the rows: one era step of the faster rate, `60 / (HeatFallSols x 1200)` = 0.1, not 0.05. Measure it at a fixed sol fraction, and state the window of up to 60 s (D4 O3).
   - **Correct the ladder in section 3.** Lander, BatteryBank and Pylon are 1 and shed first. Borer is 2. Habitat, HabModule and HabCorridor are 3. HumidityRegulator is 5. Floodmast is 12 and stays on longest, not "air filters and charge pads".
2. **"Hope only" still reaches arrivals. Say so; do not exempt it.**
   - `HopeSmoothed` feeds:
     - the settler draw (`RHSimDistricts.cpp:613-614`, `RHSimWorldSubsystem.cpp:6893`);
     - births (`:6730`);
     - the Big Arrival (`:6939`);
     - disputes (`:5005`);
     - discoveries (`:7315`, `:7335`);
     - tempo (`:7419`).
   - **What fails.** DP7's "arrivals ... equal the twin's" fails for any fixture whose Hope sits near a threshold.
   - **Fix:**
     - Write in plainly that warmth slows settlers, births and the arrival streak through Hope, as every other Hope term does.
     - DP7 asserts only the direct non-effects: the rating, air, beds, departure accumulators and the recall list.
     - Add one line to the director's answer noting this. Do not build an exemption: it would make warmth the only Hope term that immigration ignores.
3. **Re-anchor to `31fc24c`.** Every sim anchor in the draft is about 50-170 lines stale, for example:

   | Code | Draft | HEAD |
   |---|---|---|
   | `MaxDepth` read / member / `GetMaxDepth` | `:124` / `.h:2422` / `.h:526` | `:130` / `.h:2662` / `.h:604` |
   | bore / carve / tunnel / H2 / hours | `:1526` / `:1538` / `:1543` / `:1556` / `:1570` | `:1573-1574` / `:1585-1586` / `:1591` / `:1604` / `:1618` |
   | `StepPower`, `FLoadEntry`, sum, `Sort`, flicker note | `:8176-8279`, `:8187`, `:8234`, `:8240`, `:8259-8262` | `:8232-8335`, `:8243`, `:8288`, `:8296`, `:8315-8318` |
   | spines | `:585`, `:756` | `:632`, `:804` |
   | Hope total / C6a note | `:6647-6649` / `.h:982-985` | `:6695-6697` / `.h:1204-1205` |
   | radiation / flare / `AccrueWear` | `:2422-2430` / `:2432-2446` / `:12528` | `:2470-2478` / `:2480-2496` / `:12701` |
   | `RequestChargeWh` free-energy rule | `:12505-12508` | `:12678` |
   | Orders: `ExtendShaftOf` / Bore / Excavate / `CanDigTunnel` / building level | `:8104` / `:8868` / `:8890` / `:7745` / `:8380` | `:8160` / `:8924` / `:8946` / `:7801` / `:8436` |
   | save refusals | `:5439`, `:5572`, `:5584`, `:5602`, `:8027` | `:5487`, `:5620`, `:5632`, `:5650`, `:8083` |
   | growth | `:706`, `:1114`, `:1067` | `:709`, `:1117`, `:1070` |
   | deck Bore label / Borer card; view clamp | `:2256` / `:1559`; `:2122-2133` | `:2354` / `:1591`; `RHPlayerController.cpp:2138-2148` |
   | cheats | `:632`, `:1584`, `:1787`, `:1806`, `:2478` | `:734`, `:1686`, `:1889`, `:1908`, `:2580` |
   | commandlet print / -percapita / -scale / K6 / TN8 | `:16254` / `:5808` / `:15055` / `:12539` / `:14435` | `:16425` / `:5812` / `:15065` / `:12543` / `:14439` |
   | commute floor term | `RHSimCommute.cpp:194-210` | `:235-278` (transit branch `:237-262`) |

4. **Recount the diff against `battery_post_btn` (49 logs), not `battery_post_d4`.**
   - **The version lines: 13 out and 13 in.**
     - `traffic.log:938`, `:939`;
     - `deep.log:596`, `:597`, `:602`;
     - `framing.log:576`;
     - `farice.log:7075`, `:7089`;
     - `shafts.log:1418`;
     - `tunnels.log:311`, `:1712`;
     - `transit.log:3789`, `:3943`.
   - **The conditions that move to 47:**
     - `Ver == 46` at `RHSimCommandlet.cpp:10815`, `:12095`, `:12511`, `:14016`, `:14940` and `RHSimTransitSuite.cpp:1595`;
     - `/46` at `:10821`, `:12095`, `:12555`, `:13668`, `:14455` and TransitSuite `:1742`;
     - the `V45` patches at `:10816`, `:12089`, `:12553`, `:13666`, `:14454` and TransitSuite `:1739` become 46;
     - the literals at `:12526` ("hand-made v46") and `:12101` ("the v46 save loads").
   - **The range text: 2 out and 2 in.** `deep.log:600` (K6) and `tunnels.log:1704` (TN8), not `:594` and `:1710`.
   - **The radiation print:** item 5.
   - `depth.log` is a NEW SUITE. The battery is 50 runs.
5. **Accept the 15 radiation lines. Drop the print bound and deviation 2.**
   - Bounding a ledger diagnostic by a balance row ties the two together. The profile would silently shorten or lengthen whenever the director moves the cut-off, and R6 already concedes it reads as hiding lines.
   - The 15 added lines are `baseline.log` after `:213` ("radiation level -6..-20: 0.0000"). They are deterministic and named.
   - The bar becomes 15 out and 30 in.
6. **DP1 proves nothing, and its mutation cannot fail.**
   - Two colonies bored to -5 never enter a new branch, which is D4 O6's TR1 again.
   - `(double)f * 1000 / 1000.0 == f` for every float `f`: the product is exact in a double (24 + 10 bits), and the division is correctly rounded. So "drop the `== 1000` branch" changes no batch hour. With today's rows, `GrowthCellSols` 1.0 and `GrowthRoomSols` 0.5 do not move either.
   - **Fix:** the battery diff and the byte-identical playtests are the inertness proof. Redefine DP1 as a twin that adds one carved -6 floor (no residents, a healthy bank). Every -1..-5 batch hour, the floors' air and ratings, and `LoadW` minus the -6 entry must equal the twin's. Drop the mutation claim, or name one that can fail.
7. **Fix DP2's arithmetic.** -7 is `1000 + 150 x 2` = 1300 per mille, so 15.6 h, not 16.2 h. The start log prints `%.0f` (`:1630`), so 3.45 h prints as "3 h". DP2 and DP3 must read `BatchRemaining_h`, not the log.
8. **Update DP12 and R5 for D4's transit.** With a car, a -20 home with a surface seat is `20 x 2000 + wait` cm = 40,000 + wait (`RHSimCommute.cpp:252-254`, `CommuteLiftCmPerFloor` 2000), not 120,000.
   - Pin both cases.
   - State plainly that every deep home at -16 or below with a surface seat is a long commute even on an empty lift (`16 x 2000 = 32,000 > CommuteLongCm 30,000`).
   - So Shelter (+3, by share) against Commute (-4, saturating at a long share of one half) is a net loss below -15 unless the jobs are on the same floor. That is for the director.
9. **-scale at 20 floors must cover the depth-scaled D4 structures, not just `StepPower`.**
   - At HEAD, -scale has four shafts with four digs a floor. D4's block fits four cars on every shaft before any new block runs (`RHSimCommandlet.cpp:16228-16240`), so transit is on.
   - **Specify:**
     - which shafts go to -20;
     - the cold deep net (`4 + 4 x 20` portals if all four do);
     - `PlanDeepRoute`, the cold and warm reserve reads, the cold commute and the threshold roll, all with the 2,000 deep residents, timed against D4's budgets;
     - one RC-H on -20 that seeks and docks. At 20 floors the ride is 10 + 80 s: 180 Wh, plus the 75 Wh hold.
10. **The shaft strip is not a scroll box at HEAD, so the click test belongs in the bar.**
    - It is an `SVerticalBox` inside an `SBorder` at `VAlign_Center` (`RHCommandDeck.cpp:298`, `:866-873`), so section 9's `SScrollBox` is new.
    - The buttons fix has just landed (`31fc24c`). `scripts/unreal/rh_clicktest.sh` must PASS (REACH n/n) on a colony bored past -11, so rows are scrolled out and the Fit-lift and Tunnel buttons sit below the window.
    - Its `Gather` already clips by scroll box (`RHClickTest.cpp:94-117`), so the sweep is valid there.

## Optional changes

- **O1.** In the shallow branch, keep the H2 bill as written (`float x float`, `:1604`) rather than `float x double`. It is exact for today's rows, but it is bit-identical by construction only if left alone.
- **O2.** Lift power has now been deferred twice (D4 R9, then here). Name the gate that owns it: D8 is presentation-only, so D6 or D7.
- **O3.** Cooling counts D3 tunnel cells too: a 64-cell tunnel on -20 costs 960 W for ever. Put this in R3 for the director.
- **O4.** The name also clashes with the Gate-D wording rows named "D5" (`docs/design-decisions.md:661`). Use "D5 depth" there too.
- **O5.** Keep the deck's no-sim fallback at 5 (or 0 rows), not `RH::MaxDepthCap`.
- **O6.** Clamp `DepthCostFromFloor` to `0..RH::MaxDepthCap` at load, and assert in DP0 that it is below `MaxDepth`.
- **O7.** "Level descending" in section 7: write "-6 before -7".
- **O8.** PageUp and PageDown are new Enhanced Input mappings (`RHPlayerController.cpp:109-151` binds neither). List them in the presentation diff.

VERDICT: BLOCKED
Warmth, the only consequence Q-B approved, is integrated from the per-step shed flag. With the Lander's 50 W always in `GenW`, that flag alternates every step during a shortage (`RHSimWorldSubsystem.cpp:8294-8313`). With a rise of 1 sol and a fall of 0.5 sol, Warmth therefore stays near 0 in both bands. Unblocked when a REVISION:
- drives Warmth from a band-honest shortage regime (item 1), with DP6 reached through the real grid and DP9 non-vacuous at a tolerance derived from the rows;
- states Hope's downstream reach for Q-B (item 2);
- carries items 3-10.


# REVISION (2026-10-22)

This revision answers the PROFESSOR REVIEW above. Where it disagrees with sections 0-10, the revision wins. Anchors are read at HEAD `31fc24c` (D4 at `fe232c7`, save v46, plus the buttons fix) with `git show HEAD:`. Q-A (a) and Q-B (a) are answered and are built as written.

## Item 1. [BLOCKER] Warmth is driven by "the bank is running empty"

**The fault, conceded.** Shedding needs `BatteryWh <= 0` at the step's start (`RHSimWorldSubsystem.cpp:8294`). It sheds down to `LoadW <= GenW`, so the step ends in surplus and charges the bank (`:8312-8313`). The next step serves everything and clamps the bank back to 0. The Lander's 50 W base (`RH_Buildings.csv:2`) means `GenW` is never 0, so the shed flag alternates (the code says so at `:8315-8318`). Integrated from that flag, Warmth stays near 0.

**The rule.** In `StepPower`, after the building entries and the deep-floor cooling entries are summed (`:8288-8291`), `FullW` is that sum, before any shed. `B0` is `Power.BatteryWh` at the step's start.
- **The step is scraping** when
  - (i) `B0 <= 0 && FullW > GenW`, which is exactly today's shed test at `:8294`; or
  - (ii) `B0 + (GenW - FullW) x SubDt / 50 < 0`, which means the clamp at `:8313` would fire under the full load.
- **The short set.** This is the priority walk over the full load: start from `Rem = FullW`, walk the sorted entries, stop at `Rem <= GenW`, and subtract each entry walked. The short floors are the cooling entries in that prefix.
  - In an (i) step, this is literally the existing shed loop (`:8296-8306`): the same start, order and stop. So the floors marked short are the ones the grid actually sheds.
  - In an (ii) step, the same walk runs on the sorted array without shedding anything. It answers "who would be cut".
  - The walk and the sort run only when a cooling entry exists. Otherwise `StepPower` is today's code.
- **Warmth.** A short floor gets `+SubDt / (HeatRiseSols x 1200)`. Every other deep floor, and every floor in a step that is not scraping, gets `-SubDt / (HeatFallSols x 1200)`. Then clamp to 0..1.
- **The bank-cycle argument.**
  - A shortage cycles: one shed step charges `X = (GenW - Lshed) x dt / 50`. Then served steps each drain `D = (FullW - GenW) x dt / 50`, until the step whose clamp fires.
  - Both X and D scale with dt. So the cycle has the same number of steps in both bands, `floor(r) + 2` with `r = X / D`, and exactly two of them are scraping (the shed step and the clamp step).
  - When `r < 1` (cooling's own cut is what balances the grid), every step scrapes, and Warmth rises at the full rate in both bands.
  - When `r >= 1`, the over-shed carries the floor's cooling for most of the cycle, and Warmth falls. That matches the energy actually delivered.
  - So the scraping share of time is the same in both bands, up to generation changing inside one era step.
- **The ladder, corrected** (`RH_Buildings.csv`, `LoadPriority`):
  - 1: Lander, BatteryBank and Pylon, shed first;
  - 2: Forge and Borer;
  - 3: Habitat, HabModule and HabCorridor (0 W);
  - 4: Electrolyzer;
  - 5: ComputeModule, LabModule and HumidityRegulator;
  - 6: IceDrill;
  - 7: WaterPlant, then deep cooling (deepest first);
  - 8: AirFilter and InfirmaryModule;
  - 9: ChargePad;
  - 12: Floodmast, which stays on longest.
- **Tests:**
  - **DP6, through the real grid with no debug force.** A fresh colony: the Lander only, no SolarArray, no other consumer. A 40-cell floor on -10 (200 W of cooling), the bank set to 0 once (`Debug_SetBatteryWh`, `.h:2155`).
    - Full load 220 W against 50 W, so `r = 50/170 < 1`, and every step scrapes in both bands.
    - After 1 sol (12,000 agent steps or 20 era steps), Warmth is exactly 1.0. The 0.25 alert fires once.
    - Then the bank is filled. After 0.5 sol Warmth is 0, the off alert fires, and `WarmPenalty` returns to `+0.0`.
    - **Mutation:** driving Warmth from the entry's served flag leaves Warmth <= 8e-5 and fails DP6.
  - **DP5.** On a step that starts with an empty bank: WaterPlant is in the short prefix before any cooling, -10 before -6, and the AirFilter is left out. A second case checks the (ii) step: the bank holds less than one step's deficit, nothing is shed, and the walk still marks -10 short.
  - **DP9.** A paired 10-sol run (era and agent, fix-A method): the Lander, one SolarArray, the 200 W floor on -10, and the Lander's 2,000 Wh bank.
    - Nights scrape, so Warmth reaches >= 0.25 every night in both bands (asserted, so the check is not vacuous).
    - The tolerance comes from the rows: one era step at the faster rate, `60 / (HeatFallSols x 1200)` = 0.1. Warmth is read each sol at sol fraction 0.95, at the first step end at or after it (the up-to-60 s window, D4 O3).
    - Hope agrees within 5%.

## Item 2. Warmth reaches arrivals through Hope (plain words for the director)

Warmth costs Hope, and nothing else directly. But Hope is not just a number on the panel. The colony's smoothed Hope already decides:
- whether settlers come and how many (`RHSimDistricts.cpp:613-614`, `RHSimWorldSubsystem.cpp:6893`);
- whether babies are born (`:6730`);
- whether the Big Arrival comes (`:6939`);
- whether disputes start (`:5005`);
- whether labs make discoveries (`:7315`, `:7335`);
- how fast people work (`:7419`).

So a long cooling shortfall can slow settlers, births and the arrival streak, as every other Hope term does (food, water, commute). Nobody is sent away or harmed by it, and the floor keeps its rating. No exemption is built: it would make warmth the only Hope term that immigration ignores.

**Proposed one-line note for the director's answer** (I have not edited that section): "Warmth costs Hope only, and like every Hope term it can slow settlers, births and the Big Arrival while it lasts."

**DP7, rewritten.** It runs as twins A and B on one start, with a real-grid shortage. A new runtime-only `Debug_SetExtraGenW(250)` adds a constant source. This sets generation, never Warmth. Full load 420 W against 300 W makes cooling short while the floor's AirFilter stays served (`r` = 0.83).
- B runs with `HopeWarmMax` forced to 0 (the D3 debug-override precedent).
- The fixture asserts its own conditions:
  - no free bed (so no births, settlers or Big Arrival);
  - food and water stocks deep;
  - no lab;
  - `HopeSmoothed` above `DisputeHopeBelow` in both twins.
- After 2 sols at Warmth 1.0, A and B are equal in:
  - the floor's rating and `FloorO2Kg`;
  - beds per floor;
  - every departure accumulator;
  - the recall list.
- A's Hope is lower than B's (the penalty bites).

## Item 3. Re-anchored to `31fc24c`

I adopt the review's table as written, and I spot-checked every row against HEAD. Also, for this design:
- **`StepPower`:** the Pass 2 sum is `:8288-8291`; the shed test `:8294`; the shed loop `:8296-8306`; the delta and clamp `:8312-8313`.
- **Hope readers:** see item 2. `HopeSmoothed` is saved at `:13335`.
- **The commute:** the transit branch is `RHSimCommute.cpp:237-262`. The long test is `> CommuteLongCm` (`:389`, `:424`, `:456`). The penalty is `:555`.
- **Rows:** `CommuteShaftCmPerFloor` 6000 and `CommuteLongCm` 30000 (`RH_Config.csv:225-226`); `LiftDwellSeconds` 10 and `LiftSecondsPerFloor` 4 (`:248-249`); `CommuteLiftCmPerFloor` 2000 (`:254`).
- **-scale:** D4's transit block is `RHSimCommandlet.cpp:16221-16380`, its budgets `:16374-16380`, and `SCALE TEST END` is `:16383`.
- **Deck:** the fallback `RHCommandDeck.cpp:152`; the read `:289`; the rows `:345-348`; Fit lift `:371`; Tunnel `:402`; the strip's `SBorder` `:866-873`.
- **Visualizer:** the tile loop `RHColonyVisualizerSubsystem.cpp:3460`; the light mask `:4016-4018`.
- **Save:** the warmth block goes after `WriteTransitBlock` (`:13183`) and is read after `ReadTransitBlock` (`:13634`).

## Item 4. The diff, against `battery_post_btn` (49 logs)

**The version lines: 13 out and 13 in.** Each moves from 46 to 47, and each "v45 / 45" previous-version mention moves to 46. I read each line in the baseline:
- `traffic.log:938`, `:939`;
- `deep.log:596`, `:597`, `:602`;
- `framing.log:576`;
- `farice.log:7075`, `:7089`;
- `shafts.log:1418`;
- `tunnels.log:311`, `:1712`;
- `transit.log:3789`, `:3943`.

**The code that moves to 47:**
- `Ver == 46` at `RHSimCommandlet.cpp:10815`, `:12095`, `:12511`, `:14016`, `:14940` and `RHSimTransitSuite.cpp:1595`;
- `/46` at `:10821`, `:12095`, `:12555`, `:13668`, `:14455` and TransitSuite `:1742`;
- the `V45` patches at `:10816`, `:12089`, `:12553`, `:13666`, `:14454` and TransitSuite `:1739` become 46;
- the literals at `:12526` and `:12101`.

**The range text: 2 out and 2 in.** `deep.log:600` (K6) and `tunnels.log:1704` (TN8): "-1..-5" becomes "-1..-20".

**The radiation print: 0 out and 15 in** (item 5). `depth.log` is a NEW SUITE. The battery is 50 runs. **The bar: 15 out and 30 in.** Recount literally at build time; any other line fails the gate.

## Item 5. The 15 radiation lines are accepted

The bound in section 1 is withdrawn. `RHSimCommandlet.cpp:16425` is unchanged. `baseline.log` gains "radiation level -6: 0.0000" through "radiation level -20: 0.0000" after `:213`. They are deterministic and named in item 4. Deviation 2 is dropped.

## Item 6. DP1, rewritten

- **DP1 (twin with a deep floor).**
  - Twins A and B both bore to -6 (`ExtendShaft`, instant) and run the same Borer orders on -1..-5 with a healthy bank.
  - A also gets one carved -6 floor by `ExcavateFloor` (instant, no Borer time, no residents).
  - Over 3 sols, these must be equal between A and B:
    - every -1..-5 batch's hours (read as in item 7);
    - `FloorO2Kg` and the rating of each of -1..-5;
    - `A.LoadW - CoolingW(-6)` and `B.LoadW`.
  - A's `LoadW` exceeds B's by exactly `CoolingW(-6)` (`Cells x 1 W`).
- **The mutation that can fail:** an off-by-one in the cut-off (`-L - DepthCostFromFloor + 1`, which counts -5 as deep). It scales -5's batch hours by 1.15 and adds a -5 cooling entry, so DP1 fails.
- The "drop the `== 1000` branch" claim is withdrawn. The review's proof that it is exact holds.
- The inertness proof for -1..-5 is the battery diff (item 4) and the byte-identical playtests.

## Item 7. DP2 and DP3, corrected

- -6 is 1150 per mille: `BoreFloor` 13.8 h. -7 is `1000 + 150 x 2` = 1300 per mille: **15.6 h**. `CarveCell` on -6 takes 3.45 h.
- **The tests read `BatchRemaining_h`, not the start log** (it prints `%.0f`, `:1630`). An idle Borer starts the batch in the step that executes the order, then spends that step's budget at once (`continue`, `:1618` onward). So DP2 and DP3 assert `BatchRemaining_h + SubDt / 50 == BatchH` within 1e-9, read after that step (no day shift on the Borer).

## Item 8. Commute against Shelter under D4, and a director question

**The numbers (pinned in DP12):**
- **No lift car anywhere:** every floor costs 6,000 cm (`RHSimCommute.cpp:266-277`). A -5 home with a surface seat is 30,000 cm plus the surface leg. Any leg over 0 makes it long (`> 30,000`). A -20 home is 120,000 cm plus the leg.
- **With a lift car:** each floor costs 2,000 cm, plus the frozen wait, plus the surface leg (`:253-257`). -15 is 30,000 + wait + leg, which is long as soon as either is over 0. **From -16 down a surface seat is always long, even on an empty lift** (`16 x 2000 = 32,000`). -20 is 40,000 + wait + leg.
- **A deep home with a seat on its own floor** is 0 cm (`:244-246`).
- **The trade, once the ramp is full (160 people or more):**
  - Shelter is `+3 x deep share`.
  - The commute costs `-4 x min(1, 2 x long share)`.
  - So if a share s of the colony lives at -16 or below and works on the surface, the net is `3s - 8s = -5s` up to s = 0.5. It is a net loss at every share.
  - Under 60 people the commute costs nothing and Shelter is pure gain.

**R5 is replaced by Q-C.** DP12 pins: a -20 home and surface seat with no car (120,000 + leg); the same with a car (40,000 + wait + leg); -15 against -16 with a car and a zero leg (30,000 is not long, 32,000 is); and a -20 home with a -20 seat (0).

## Item 9. -scale at 20 floors re-times D4's deep structures

A new block runs after D4's transit block and before `SCALE TEST END` (`:16383`). The 5-floor numbers and digests above it do not move.
- **Setup:**
  - All four shafts go to -20 (`ExtendShaftOf`, instant). The four-dig layout the transit block uses on -1..-5 is repeated on -6..-20 by the same helper, and each new floor is rated.
  - 2,000 colonists are rehomed to -6..-20.
  - The 100 harness RC_H robots are re-spread over all 20 floors.
- **Timed against D4's budgets (`:16374-16380`), unchanged:**
  - the cold deep net, asserting `4 + 4 x 20` = 84 portals (`<= 3 ms`);
  - 2,500 `PlanDeepRoute` calls (`<= 10 ms`);
  - 1,000 cold and 10,000 warm reserve reads (`<= 10` / `<= 1 ms`);
  - a cold commute with transit on (`<= 10 ms`) and flows only (`<= 1 ms`);
  - one `StepTransit` roll (`<= 1 ms`).
  - Plus `StepPower` with 15 cooling entries and a scraping bank, so the walk runs (`<= 0.05 ms`), and `GetColonyHope` with Shelter and Warm live, within the existing Hope budget.
- A budget that fails is a finding for the director, not a budget edit.
- **One RC_H on -20** seeks the surface pad and docks. Its ride is `10 + 4 x 20` = 90 s. The reserve must cover the drive, the 180 Wh ride and the 75 Wh hold (the review's figures, re-derived by the build from `DriveWh` and the hold rows). Asserted: it docks, never halts below, and arrives with charge > 0.
- -scale also gets the save and load timing at 20 floors (the existing ms budgets).

## Item 10. The shaft strip becomes a scroll box, and the click test is in the bar

At HEAD the strip is a plain `SVerticalBox` (`RHCommandDeck.cpp:298`) in an `SBorder` at `VAlign_Center` (`:866-873`).
- **The layout.** The "SHAFT" header and "Next shaft" stay at the top, outside the scroll. The rows, SURF..-MaxDepth (`:345-348`), move into an `SScrollBox` inside an `SBox` with `MaxDesiredHeight` = 11 rows (11 x the row's desired height, measured once).
  - Fit lift (`:371`), Tunnel (`:402`) and the tunnel list stay below the scroll box, outside it. So they never scroll away.
  - A row is visible when its floor is reached by any shaft, or is the next to bore, or is the active level.
  - When `GetActiveLevel()` changes, the deck calls `ScrollDescendantIntoView` on that row.
- **Keys (O8).** New Enhanced Input actions `IA_FloorUp` and `IA_FloorDown` on PageUp and PageDown, beside the existing mappings (`RHPlayerController.cpp:109-151`). Each calls `SetActiveLevel(ActiveLevel ± 1)`, which keeps its reached-depth clamp (`:2138-2148`).
- **The click test.** `scripts/unreal/rh_clicktest.sh` gains an optional `RH_CLICKTEST_PRE`, a list of console commands inserted before `RH.After 3 RH.ClickTest`. The D5 bar is two passing runs (`CLICKTEST VERDICT: PASS`, REACH n/n):
  - the default run;
  - `RH_CLICKTEST_PRE="RH.BoreNow 14"`, a colony bored past -11. Rows are then scrolled out, and the clipping `Gather` (`RHClickTest.cpp:94-117`) leaves them out of the count. Fit lift and Tunnel must be reached.

## Optional items

**All are adopted. None is rejected.**
- **O1.** The shallow branch keeps `BDef->H2BurnKgPerHour * Work->BatchTime_h` (`:1604`) exactly as written. Only the deep branch uses `BatchH`.
- **O2. D6 owns lift power.** D6 brings trams and people riding them, so lift and tram power get priced together there, against D5's cooling. Deviation 1 names D6.
- **O3. Tunnel cells cost cooling.** A 64-cell tunnel on -20 costs `64 x 15 x 1 W` = 960 W for ever. R3 now says so, and the shaft card shows tunnels in the floor's cooling.
- **O4.** "D5 depth" is used in the Gate-D wording rows too (`docs/design-decisions.md:661` uses bare "D5").
- **O5.** The deck's no-sim fallback stays 5 (section 1's `RH::MaxDepthCap` fallback is withdrawn).
- **O6.** `DepthCostFromFloor` is clamped to `0..RH::MaxDepthCap` at load. DP0 asserts it is below `MaxDepth`.
- **O7.** Section 7 reads "-6 before -7" (shallowest first).
- **O8.** See item 10.

## Director question

**Q-C. Living deep and working on the surface is a net Hope loss.**

With a lift car, every floor adds 2,000 cm to a commute, so anyone living at -16 or below with a surface job has a long commute even when the lift is empty. In a colony of 160 or more, the deep-home bonus (up to +3) is smaller than the commute cost (up to -4). Below -15, a deep home with a surface job loses Hope overall.
- **a (recommended): accept it. Deep floors are for deep jobs.** A deep home with a seat on its own floor commutes 0 cm and keeps the full bonus. It points players at D6's underground districts, where homes and work share a floor. No rule changes.
- b: the shelter bonus grows with depth (for example `+0.2` a floor past -5, capped at +5), so it can outweigh the commute. It is a new curve, and it moves DP8.
- c: lift cars get cheaper per floor (for example 1,000 cm), so -16 down is no longer automatically long. It changes D4's commute numbers and the -transit suite on floors -1 to -5.

## Risks, updated

- **R3** now includes tunnel cells (O3).
- **R5** is replaced by Q-C.
- **R6** is withdrawn (item 5).
- **R9 (new).** `Debug_SetExtraGenW` is a new test-only hook. It is runtime-only, 0 by default, never saved, and asserted 0 after `-depth`.
- **R10 (new).** The scraping walk sorts the load array on (ii) steps too, but only when a cooling entry exists. Inert otherwise.

## Deviations to log in `docs/design-decisions.md` (updated)

1. Lift power is deferred to D6, with trams (O2).
2. *Dropped* (the radiation print is not bounded).
3. Cooling counts every carved deep cell, tunnels included (O3).
4. "Better shielded" is a Hope term, not a radiation number.
5. **New.** Warmth reaches settlers, births and the Big Arrival through Hope; no exemption is built (item 2).

## Updated bar

- **Save v47, strict.** The default warmth block is 4 bytes.
- **Battery:** 50 runs, 0 not green. Against `battery_post_btn`, exactly 15 out and 30 in (item 4).
- **`-depth`** passes DP0-DP12 as revised. DP1's and DP6's mutations are run by hand and fail (named in the commit).
- **Playtests:** the default and `districts=1` runs are byte-identical under the mask.
- **-scale** passes the 20-floor block within D4's budgets (item 9).
- **`rh_clicktest.sh`** passes twice, default and bored past -11 (item 10).
- **Captures** as in section 9.
- **Logged:** the deviations above, and the director's answer to Q-C.


# PROFESSOR RE-REVIEW (2026-10-22)

**Verdict: CLEARED WITH CONDITIONS.** The blocker is fixed in design. The scraping rule is the right signal, and item 2's paragraph is honest. Five test specifications are still wrong against the code, and one of them (DP6's recovery half) would fail at build time. Fix items R1-R5 in the build; no further review round is needed. Anchors are at `31fc24c`.

## Item 1, checked

- **The rule fires on a real shortage.**
  - Rule (i) is today's test at `:8294`, with cooling inside `LoadW`.
  - Rule (ii) is exactly the clamp condition at `:8313` under the full load.
  - In a cycle, the shed step leaves `X = overshed x dt/50` in the bank. The next served step fires (ii) whenever `X < D`.
  - When cooling is in the cut, the over-shed is `sum(walked) - deficit`, and the last walked entry is a cooling entry. So `r < 1` unless that one cooling entry exceeds the whole deficit.
- **When the shortage is real, every step scrapes in both bands and Warmth rises monotonically at the full rate.** For example, at night on the Lander's 50 W, against 220 W, `r = 0.29`.
- **For `r >= 1` Warmth does not rise.** For `1 <= r < 2` the cycle is 3 steps with 2 scraping, a net rate of exactly 0, so Warmth holds. For `r >= 2` it falls. That follows the energy actually delivered, and the step pattern is the same in both bands while generation holds. I accept it. Note the plateau in the risks.
- **The ladder is correct** against `RH_Buildings.csv`.

## REQUIRED (build-time corrections, no new design)

**R1. DP6's recovery half fails as written.**
- "Then the bank is filled" gives the Lander's 2,000 Wh.
- At 220 W against 50 W, that drains at 3.4 Wh per sim-second, so it lasts 588 s, under half a sol (600 s).
- At 588 s the (ii) test fires, and Warmth bottoms at about 0.02 and rises again. The assertion "0 after 0.5 sol" fails.
- **Fix:** recover by generation, which is also what Q-B's "when power returns" means. Use `Debug_SetExtraGenW(250)`, so `GenW` of 300 W is above `FullW` of 220 W and no step scrapes. Alternatively, top up the bank every step.
- **"Exactly 1.0" after one sol.**
  - The agent band is safe: `0.1f` is above 0.1, so 12,000 increments overshoot and the clamp gives 1.0.
  - The era sum of 20 x 0.05 can land one ulp under 1.0.
  - Assert 1.0 one step after the sol, so the clamp has engaged in both bands, or compare within 1e-12 there.

**R2. DP9's 0.1 holds only under two conditions the fixture must assert.**
- The read is aligned: 0.95 x 1200 = 1140 s = 19 x 60, so the harness's era step ends on it. The divergence at the read is then 0.05 times the dusk-onset offset, counted in era steps, **provided Warmth started that night at the 0 clamp**.
- Off the clamps, a one-step offset at a rise-to-fall or fall-to-rise switch diverges by `0.05 + 0.1 = 0.15`, and offsets accumulate across sols.
- **Add both assertions:**
  - (a) in both bands, Warmth reaches 0 at some step of each sol before the read (the daily reset);
  - (b) in both bands, each night's first scraping step falls within 120 s (two era steps). Print both times.
- With (a) and (b), 0.1 is derived. If the fixture cannot reset daily, change the fixture, not the tolerance.

**R3. DP1's twin equality cannot catch the off-by-one mutation.**
- Both twins run the same mutated code. Their -5 hours and their -5 cooling entries are therefore equal, and the -1..-5 equalities pass.
- Only the literal "exactly `Cells x 1 W`" check fails under the mutation.
- **Fix:**
  - name that literal as the check that catches it;
  - add absolute pins: a -5 `CarveCell` reads `BatchRemaining_h + SubDt/50 == 3.0` in both twins, and B's `LoadW` equals the sum of its building entries (no cooling entry exists).

**R4. Item 2 and DP7 are missing one Hope consumer and one stock.**
- **City growth.** Its home projects wait in the Failing band (`RHSimGrowth.cpp:558`). Add it to item 2's list and to the director's note. DP7 needs either no zoned district or an assertion that neither twin is in the Failing band.
- **Oxygen.** `GetHumanWorkTempo` (`:7411-7427`) differs between the twins by design. DP7 should keep Oxygen deep as well as food and water, so the tempo gap cannot reach `FloorO2Kg`, support or the departure accumulators.
- With these, DP7's scope is correct: only the direct non-effects, with A's Hope lower than B's.

**R5. Q-C.** Build under Q-C (a), which changes no rule. If the director picks (b), DP8 is re-pinned before commit. If (c), D4's -transit numbers move and the gate needs a new diff count. State this in the bar.

## Spot-check of items 3-10: sound

- **Item 4's count** matches my recount: 15 out and 30 in.
- **Item 5's lines** are as stated.
- **Item 7's expectation** `BatchRemaining_h + SubDt/50` matches the start-then-spend loop (`:1596-1634`, then `:1456-1463`) for an idle, powered Borer.
- **Item 8's arithmetic** checks out against `RHSimCommute.cpp:237-277`.
- **Item 9** reuses D4's budgets unchanged.
- **Item 10** checks out:
  - `RH.BoreNow` exists (`RHCheats.cpp:1750`);
  - `rh_clicktest.sh:29` is the right insertion point for `RH_CLICKTEST_PRE`;
  - the Fit lift and Tunnel buttons stay outside the scroll.
- **The optional items** are acceptable as adopted.

## Optional

- **Live-game agent band.** Robot charging takes from the bank outside the load list (`RequestChargeWh`, `:12673-12676`), and robots park in era. A live agent band therefore starts scraping slightly earlier than era does. This is the existing parity class, and the headless DP9 has no robots. Log it as a risk.
- **Live era frames** read solar at the frame end (`:8236` uses `Clock->GetSolFraction()`, not `StepEndS`). The harness steps one era step at a time, so DP9 is unaffected. This existing skew is out of D5's scope.

VERDICT: CLEARED WITH CONDITIONS
1. DP6 recovers by generation (`Debug_SetExtraGenW(250)`) or a bank topped every step, not one fill. The full-warmth assertion is made one step after the sol, so era's sum cannot land one ulp under 1.0.
2. DP9 asserts a daily reset of Warmth to 0 and a dusk onset within 120 s in both bands, which is what makes the 0.1 tolerance hold.
3. DP1 names the literal `Cells x 1 W` check as the one that catches the mutation, and adds the absolute -5 hours and B's no-cooling `LoadW` pins.
4. Item 2 and the director's note add city growth (`RHSimGrowth.cpp:558`). DP7 rules out the Failing band (or has no zoned district) and keeps Oxygen deep.
5. The build proceeds under Q-C (a). Under (b), DP8 is re-pinned before commit; under (c), the diff is recounted.


# DIRECTOR ANSWER (2026-10-22)

- **Q-C (a):** accept it. Deep floors are for deep jobs: a deep home with a seat on its own floor commutes 0 cm and keeps the full bonus, while a deep home with a surface job may lose Hope overall below -15. No rule change; build under (a).
