# Gate-D mechanics brief: F1-F9 (2026-10-21)

The director approved all the proposed Gate-D wording (`gate-d-proposed-wording.md`). This brief covers the nine mechanics that wording flagged, F1-F9, which are still open. Nothing has been changed in code or data; this file is the only edit.

**To answer, copy and edit this line:**

> F1b F2b F3a F4a F5b F6b F7b F8b F9a

(That line is the set of recommendations. Write "discuss" in place of any letter you want to talk through.)

**How to read it.**
- Line numbers are from the working tree on 2026-10-20. Another session is building D1 in the same files, so they may drift by a few lines.
- **SIM** = `Source/RedHopeSim/Private/RHSimWorldSubsystem.cpp`, **SIMG** = `.../RHSimGrowth.cpp`, **CMD** = `.../RHSimCommandlet.cpp` (the test suites), **CREWVIZ** = `Source/RedHope/Private/RHCrewVisualizerSubsystem.cpp`, **PANELS** = `.../RHCommandDeckPanels.cpp`. Config rows are `docs/data/RH_Config.csv:<line>`.
- **Battery** = `bash scripts/sim/rh_battery.sh 10`: the baseline plus 41 suites (42 runs), line-identical by default. D1 adds `-deep` as a 43rd run. Every option below keeps the zero-colonist baseline unchanged, because each one only acts when people are present.
- **Size:** *minutes* means a few lines and a re-run; *hours* means new logic, a new check and re-derived pins; *a gate* means its own design and review.
- Every option keeps the standing rules: no combat, Hope is never spent, consequences are abstract and recoverable, and prevention comes first.

---

## F1. Named people at odds

**What happens today.** About twice a sol (`CrewMomentIntervalSols` 0.5, RH_Config.csv:149) the sim picks two colonists by a seeded draw (SIM:4497-4553). If anyone is short of a full ration, or smoothed Hope is under `DisputeHopeBelow` (45, RH_Config.csv:150), the pair have a dispute (SIM:4539). The screen shows both people in a still face-off under an amber plate (CREWVIZ:426-429). The moment text names them, but it only reaches the log and the event; it is not drawn on screen. A dispute costs no Hope. Anyone can be picked, including someone born on Mars.

**Why it was built that way.** The alive pass made crew life visible: pastimes, helping out, and friction as a sign that needs are unmet. The code comment says the friction is "a visible symptom, not a second punishment". The director ruled on 2026-10-16 that a dispute is a still face-off with no gestures (`design-decisions.md:263-268`). That ruling settled the body language only.

**The tension.** A shortage is the colony's problem, but the moment puts two named people at the centre of it. It can pair a child born on Mars, which reads worst of all.

**Options.**
- **F1a. Keep as is.**
  - Player sees: two people facing each other under `~ at odds ~` (the approved D24 plate).
  - Size: none.
  - Battery: none.
- **F1b (recommended). Keep the ruled face-off, but never pick someone born on Mars.** The pair is drawn from people who came from Earth. If fewer than two such people remain, the beat passes quietly, as it already does for a lone colonist.
  - Player sees: the same face-off, never involving a Mars-born child.
  - Size: minutes (filter the pair pick in SIM:4524-4535).
  - Battery: probably none. Only a suite that has births and a dispute in the same run could move, and that would be `-growth` at most.
- **F1c. Show tension as a colony state with no names.** The face-off and the named pair go away. The Crew panel and the home's label show "tension over the shortages" while the trigger holds.
  - Player sees: no individual singled out; tension is shown on the place, not the people.
  - Size: hours. It needs a new presentation for the home, and it undoes part of the 10-16 ruling.
  - Battery: `-alive` (its dispute lines) plus every suite whose log prints `MOMENT dispute: A vs B` during a shortage (`-crew`, `-water` and others).

## F2. A child born on Mars can be recalled to orbit

**What happens today.** When a home is short, a recall clock fills in proportion to how deep and how long the shortage is (SIM:4270-4318). The clock uses `ColonistEvacSols` (2, RH_Config.csv:50), plus `InfirmaryEvacBonusSols` (1, :155) while an Infirmary is online. When it is due, people are recalled. Within a home, people born on Mars go last, but they do go (SIM:4344-4349; the flag is set at birth, SIM:5963). A recalled person is removed for good. A colony can only refill through new people: ordered Crew Pods and transports, settlers or births.

**Why it was built that way.** Q6a (`design-decisions.md:431`) ruled that if a shortage lasts, "people leave gradually, in proportion to how deep and how long it is". C2a then added "Mars-born last" as a protection. Recall itself is the 2026-07-07h ruling: the only failure is a recall, abstract and never harm (`design-decisions.md:109`).

**The tension.** For a child whose only home is Mars, leaving is the hardest moment in the game. Today it is also permanent.

**Options.**
- **F2a. Keep as is.** Mars-born go last, and the approved wording says they are safe in orbit.
  - Player sees: no change.
  - Size: none.
  - Battery: none.
- **F2b (recommended). Mars-born who are recalled wait in orbit and come home first.** The sim keeps a short saved list of recalled Mars-born (name and Id). When a later supply ship lands while every home is on full rations and a bed is free, they come back on it, ahead of any settlers and without the Hope gate. A banner announces it: for example "HOME AGAIN: %d born on Mars are back with their colony" (new wording for the director).
  - Player sees: the recall of a child becomes a pause, not a loss. "Come home to Mars" (the approved D13 wording) becomes literally true for them.
  - Size: hours. It needs a save bump, a hook at ship landing, one alert and a new check.
  - Battery: probably none. The new check would go in `-percapita` or `-arrivals`. Existing lines do not move unless a suite recalls a Mars-born and then lands a ship.
- **F2c. Everyone recalled waits in orbit and comes home, oldest recall first.** The same as F2b, but for every resident.
  - Player sees: every shortage becomes fully reversible.
  - Size: a gate. It changes the colony's population over time and how settlers compete for beds.
  - Battery: several suites, including the Hope-pinned `-percapita` and `-arrivals`, and the playtests.

(Never recalling the Mars-born at all was considered and is not offered. It would leave them on short rations with no end, which is the lingering harm that recall exists to avoid. It would also mean the colony could never become quiet while one of them remained.)

## F3. Who is recalled first

**What happens today.** Inside a home that is due a recall, the newest arrival goes first (the highest Id), and the Mars-born go last (SIM:4324-4349). The banner gives only a count (the approved D125 wording). Names appear only in the log, and as a line missing from the roster.

**Why it was built that way.** The decision log gives no reason beyond the code comment. It is a deterministic tie-break inside a batch whose size Q6a already sets fairly. Q6a's "no one is singled out by roster order" (`design-decisions.md:431`) was about who goes *short*. C2a met it with shared rationing: everyone in a home gets the same fraction.

**The tension.** It is still an order of who leaves, and "last in, first out" could read as the newest people mattering less.

**Options.**
- **F3a (recommended). Keep as is.** The player never sees the order, only a count. "Newest first" keeps a colony's founders and families together the longest, and it is easy to explain if anyone asks.
  - Player sees: no change.
  - Size: none.
  - Battery: none.
- **F3b. A seeded order inside each home.** The order comes from a hash of the colonist's Id and the recall count; the Mars-born still go last.
  - Player sees: nothing visible changes. Only the log names differ.
  - Size: minutes.
  - Battery: `-percapita` (check c expects exactly the newest), `-surfacehab` (S12 expects Id 8), and the recall lines in `-crew`, `-water` and `-ending` that name who left.

## F4. Births wait on Hope

**What happens today.** A birth comes after `HopeGrowthIntervalSols` (20, RH_Config.csv:88) of unbroken eligibility. Eligibility means three things (SIM:5902-5914):
- smoothed Hope at or above `HopeGrowthThreshold` (75, the THRIVING edge, :87);
- a free bed;
- `HopeGrowthFoodBufferSols` (6, :89) of food.

If any of the three lapses, the streak resets. A big city gets births faster (`BirthRefPop` 25, :188). Settlers keep one bed back for births (`ImmigrationReserveBeds`, :194). Settlers and the Big Arrival wait on the same Hope (`ImmigrationHopeMin` and `BigArrivalHopeMin`, 75, :189 and :195).

**Why it was built that way.** It is the "Generational Carrot" in the 2026-07-07j roadmap (`design-decisions.md:182`): "sustained high Hope + housing headroom + food surplus". It is a carrot. Low Hope never takes anyone away; it only means no one new arrives. The D3a ruling (`:328-335`) made THRIVING "the normal good state", and it re-derived the suites specifically so that births still come at 75.

**The tension.** Linking a child's arrival to a mood score could read as a verdict on whether the colony's people deserve one.

**Options.**
- **F4a (recommended). Keep as is.** It is a ruled carrot that never punishes, and THRIVING is the normal good state since D3a. The approved wording already keeps the bar neutral (D111).
  - Player sees: no change.
  - Size: none.
  - Battery: none.
- **F4b. Births need only STEADY (45+), plus the bed and the larder.** Settlers stay at 75.
  - Player sees: children arrive in any settled, fed colony. Hope matters for settlers, not for births.
  - Size: a config row (minutes) plus re-derived pins (hours).
  - Battery: `-growth` (its lapse check expects no birth at 69.6), and probably `-percapita`, `-arrivals`, `-citygrowth` and `-districts`, wherever a crew sits between 45 and 75 for 20 sols with a free bed. The playtests move too.
- **F4c. Births ignore Hope entirely. A bed, the larder and full rations decide; THRIVING only speeds the streak up.**
  - Player sees: the same as F4b, plus a reward for doing well.
  - Size: hours.
  - Battery: as F4b, and wider.

## F5. Growth stops when Hope is lowest

**What happens today.** At each sol, the city's own builder (C5b) checks for two pauses before it starts a project (SIMG:540-556). The first is any home on short rations. The second is the lowest Hope band (FAILING, to be shown as LOW; it is entered below `HopeFailingEnter` 20, RH_Config.csv:68). The rations pause is checked first, so the Hope pause only matters when nobody is short, and Hope has fallen under 20 for other reasons: water quality, a thin larder, clashing rooms, a crisis or long commutes. Growth builds two things:
- Homes, which only start when beds are short for incoming people;
- Civic rooms, which fill the worst service gap. Service rooms add to Hope's Rooms line.

**Why it was built that way.** The C5b design lists it next to the short-rations pause (`docs/qa/2026-10-19/c5-districts-design.md:113-116`). It gives no further reason.

**The tension.** Civic rooms are a Hope remedy, so pausing them at the lowest Hope takes a cure away exactly when it is needed. The death-spiral guardrail asks for carrot-first and recoverable (`design-decisions.md:172`).

**Options.**
- **F5a. Keep as is.**
  - Player sees: `Growth paused: the colony's Hope is low.`
  - Size: none.
  - Battery: none.
- **F5b (recommended). At LOW, keep building Civic rooms and pause only Homes.** New homes matter little then: settlers don't come below 75, so new beds would only wait for an ordered pod. The short-rations pause stays as it is.
  - Player sees: in a low patch the city keeps adding the services that lift Hope. The pause note reads "new homes wait for Hope to recover" (new wording for the director).
  - Size: minutes to an hour (move the band check into the Homes branch).
  - Battery: `-citygrowth` only (check c5 expects no start at Hope 8 and gets re-derived). No other suite takes Hope under 20 with districts painted.
- **F5c. Drop the Hope pause; keep only the short-rations pause.**
  - Player sees: growth never stops for mood.
  - Size: minutes.
  - Battery: `-citygrowth` c5.

## F6. Hope sets work pace

**What happens today.** Colonist work pace is `1 + HopeTempoSlope x (Hope - 50)`, clamped between `HopeTempoMin` and `HopeTempoMax` (SIM:6557-6573; rows 65-67: slope 0.006, floor 0.60, ceiling 1.25). Robots are not affected. Because Hope can't go below 0, Hope alone never takes the pace below 0.70, and the 0.60 floor is never reached. During a Malfunction crisis, though, the pace is multiplied by `CrisisWorkPenalty` (0.6, row 142) *after* the clamp (SIM:6568-6571). So the pace can fall to 0.42 at Hope 0, and to anything under 0.60 whenever Hope is under 50.

**Why it was built that way.** It is the approved death-spiral guardrail (`design-decisions.md:172`): "Work-tempo floors at 0.60 (a strained colony is slow, never dead)", with low-Hope penalties as soft caps that never accelerate.

**The tension.** The Hope curve itself fits the guardrail. The Malfunction stacking does not: it goes under the floor the guardrail promises. (Separately, the kept D76 tip still says Hope "drives how hard the colony works"; the glossary word is "work pace". That is a wording matter, noted only so it isn't lost.)

**Options.**
- **F6a. Keep as is.**
  - Player sees: no change.
  - Size: none.
  - Battery: none.
- **F6b (recommended). Keep the ruled Hope curve, and apply the 0.60 floor after the Malfunction multiplier too.** The pace is then never under 0.60, as the guardrail says.
  - Player sees: a Malfunction during a low patch slows work to 60%, not 42%.
  - Size: minutes (one clamp).
  - Battery: probably none. `-crisis` prints the multiplied pace and moves only if its Malfunction case falls under 0.60. Going by the D3a notes (its Hope falls from 62.5 to 50.5), it should sit just above; not run.

## F7. The quiet colony is an ending

**What happens today.** If a colony that once had people loses them all, the projected ending becomes Collapse (SIM:8388-8395). The check comes first, ahead of the political read. The Earth panel then shows `On the path to: <name>` (PANELS:1874-1878; approved D14 renames it "The Quiet Colony"). A one-time banner fires (SIM:4022-4035, approved D13). Play continues, and ordering a Crew Pod or transport brings people back. Settlers do not come to an empty colony (SIM:6017). Three details:
- The banner's latch is saved and never reset, so a colony that empties a second time gets no banner.
- The Collapse read is never "declared" (SIM:8087) and never gets an epilogue.
- An empty colony resets the epilogue clock of any declared political path (SIM:8106-8121), because the projection no longer matches it.

**Why it was built that way.** M4 Gate D gave the brief's failure state (§66) a place among the endings. It was later made recoverable, with the recovery named, under the 2026-07-07h ruling.

**The tension.** Listing an empty colony beside the political endings calls a recoverable state an ending, which is the opposite of the framing the directive asks for.

**Options.**
- **F7a. Keep as is, with the approved name.**
  - Player sees: `On the path to: The Quiet Colony`.
  - Size: none.
  - Battery: none.
- **F7b (recommended). Make it a state, not an ending.**
  - The projected ending ignores an empty colony.
  - The Earth panel and the Crew panel show one line instead: "The colony is quiet: everyone is safe in orbit" (new wording for the director).
  - The banner re-arms when people return.
  - The epilogue clock pauses while the colony is quiet, so a political epilogue can't be written over an empty colony. Today the Collapse read does this job by resetting the clock.
  - Player sees: the ending list holds only the political futures; an empty colony reads as a pause.
  - Size: about an hour, with no save change (the latch byte keeps its slot).
  - Battery: `-ending` (its collapse line expects `projected=Collapse`). The approved wording commit already moves the lines that print the old name.
- **F7c. Keep it as an ending, but re-arm the banner when people return.**
  - Player sees: as F7a, but a second quieting gets its banner.
  - Size: minutes.
  - Battery: none, unless a suite empties a colony twice.

## F8. Tobacco as a comfort

**What happens today.** The Smoking Area room is dormant (`docs/data/RH_Rooms.csv:8`, SliceActive FALSE). It is described as usable "once tobacco is cultivated", with a high morale weight (0.8) and an Odor emission. Nothing in the game can activate it. Its row key, "Smoking", is used in two tests as the example of a dormant room that gets refused (CMD:311-315 in `-rooms` R2; CMD:2296 in `-surfacehab`).

**Why it was built that way.** It comes from the director's own habitat vision (`docs/habitat-vision.md:24`; `design-decisions.md:92`): "tobacco as a cultivable luxury for morale". It was deliberately held back for this review (`design-decisions.md:160`, build-log Session 27).

**The tension.** A game about mental health that offers smoking as a way to feel better could read as endorsing it as a coping habit, which conflicts with "never trivialized".

**Options.**
- **F8a. Keep it dormant and decide later.**
  - Player sees: nothing.
  - Size: none.
  - Battery: none.
- **F8b (recommended). Re-theme it now as a Lounge, still dormant.** Change only the DisplayName and the Notes. The internal row key and function stay, so the tests are untouched. Strike the tobacco line from the habitat vision with a dated note.
  - Player sees: nothing yet. If it is ever activated, a lounge.
  - Size: minutes, plus a DT live-sync of one row.
  - Battery: none.
- **F8c. Retire the row.**
  - Player sees: nothing.
  - Size: minutes plus a DT sync. Both tests need a different dormant example.
  - Battery: `-rooms`, `-surfacehab`.

## F9. The cost of recycled water

**What happens today.** Recycling returns `GreywaterReturnFraction` (0.85, RH_Config.csv:80) of what people drink. Each sol of recycling lowers potability by `WaterPotabilityDecayPerSol` (0.08, :81). Fresh ice-melt restores it (`WaterPotabilityRestorePerKg`, :82) (SIM:4400-4426). Below `WaterPotabilityFloor` (0.6, :83), Hope loses up to `HopeWaterPenalty` (12, :84), in proportion to how far below the floor it is (SIM:5833-5839). It is zero at or above the floor, and it disappears as soon as fresh water lifts potability again. A WATER QUALITY LOW banner names the fix (approved D8).

**Why it was built that way.** The director approved the water loop as "next" on 2026-07-07j (`design-decisions.md:177`): recycled water degrades, and only fresh ice-melt restores it, so drilling ice becomes a standing need. The Hope cost is how that need is felt. The build log notes it is what makes drilling keep a colony thriving enough to grow (build-log:183). The 07-07 note at `design-decisions.md:160` logged "recycled-water psychology" as an open question *before* the loop existed. The penalty's size has stayed agent-proposed balance since then.

**The tension.** A drop in Hope over drinking recycled water is a psychological cost, and the directive wants such costs abstract, small and fixable.

**Options.**
- **F9a (recommended). Keep as is, and rule it now.** It is soft (a capped, linear line that never accelerates), the fix is named, and the approved wording puts the state on the water rather than on what people drink.
  - Player sees: no change.
  - Size: none.
  - Battery: none.
- **F9b. Halve `HopeWaterPenalty` to 6.**
  - Player sees: the same signal, felt less.
  - Size: one config row and a DT sync (minutes), plus re-derived pins.
  - Battery: `-water` (the penalty lines) and any suite that sits below the floor (`-growth`, `-hopedrive`, `-tiers` and `-agri` read potability).
- **F9c. Move the cost off Hope.** Low potability means recycling returns less water, so the tanks drain faster: a supply cost, not a mood.
  - Player sees: no Hope line for water; the water simply runs out sooner if no ice is drilled.
  - Size: hours.
  - Battery: `-water`, `-growth`, `-hopedrive`, `-tiers`, `-agri`, `-discovery` and `-alive`, plus the playtests.

---

## If you take all the recommendations

**For the player:**
- Disputes never involve a child born on Mars.
- A recalled Mars-born child comes home on the next ship once the colony is fed and has a bed.
- In a low patch, the city keeps building the rooms that lift Hope.
- A Malfunction can't slow work below 60%.
- An empty colony reads as a quiet pause instead of an ending.
- The dormant smoking room becomes a dormant lounge.

Births, recall order, the Hope pace curve and the water cost stay as ruled.

**Engineering:** about half a day in total. F2b takes most of it: a save bump, a hook when a ship lands, and one new check. The other five changes are minutes to an hour each. Four new strings need your wording approval: the F2b banner, the F5b pause note, the F7b quiet line, and the Lounge name.

**Battery:** expected moves are `-citygrowth` (c5) and `-ending` (the collapse line), each explained line by line. `-crisis` and `-growth` should be checked but are expected to hold. The Hope-pinned `-percapita`, `-arrivals` and `-crew` should not move.

**Which gate:** F1b, F5b, F6b, F7b and F8b are small enough to ride in the approved Gate-D wording commit after D1. That commit already moves the alert-text and ending-name lines, and it already needs a DT live-sync. F2b is better as its own commit straight after, because of the save bump. Both belong to Gate D.
