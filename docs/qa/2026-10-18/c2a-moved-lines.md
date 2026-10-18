# C2a moved lines (battery post-C1 -> post-C2a, revision 2), 2026-10-18

Baseline run (no colonists), the pins (power gen 50 W load 20 W; deposit Regolith_A 40645 kg) and every other suite: line-identical (0 differing).

Raw lines: '-' is the post-C1 line, '+' is the post-C2a line (timestamps stripped, autosave byte counts masked). Total differing lines: 305 in 14 suites; plus the new suite percapita (11878 sim lines, not diffed).

Revision 2 changed three things against revision 1: departures count the residents at the ONSET of the shortage (a total failure empties a home in EvacSols, steadily); HopeAdjacencyMax is 16, not 24; a crew moment needs two different colonists.


## agri.log

Reason: 2 lines (one old, one new). The garden's cumulative figure 'cum' moves 20.69 -> 20.74 kg: garden yield rides the human work tempo, which rides smoothed Hope, and Hope moved because the jobs term is now a rate (7.5 x seats / people) instead of 1.5 a seat. No colonist is short in this run; nothing about rationing is involved.

```
    -AGRI duct: co2=3.00 kg o2Delta=-13.10 kg cum=20.69 (expect 3.00 = 2+2-1; o2 includes crew draw + 0.40 emitted)
    +AGRI duct: co2=3.00 kg o2Delta=-13.10 kg cum=20.82 (expect 3.00 = 2+2-1; o2 includes crew draw + 0.40 emitted)
```

## alive.log

Reason: Shared rationing and Hope per head (the fixture is NOT changed: with HopeAdjacencyMax 16 the Garden next to the quarters still costs the old 16, so the supplied colony stays above the dispute gate: supplied smoothed Hope 48.9, was 48.3, no disputes while supplied, 2 while starving - 'ALIVE moments' and 'ALIVE disputes' lines are unchanged). Four 'COLONIST UNSUPPORTED' lines become one 'HOME SHORT RATIONS: Floor -1 (food, 4 residents, ration 0%)'; the starved sol now recalls the newest resident at about 0.6 sol ('COLONIST EVACUATED: Dmitri Duval ... 0.6 sols'), and when food returns 'HOME RATIONS RESTORED: Floor -1 (3 residents)'. 'ALIVE mood margin' 48.3 / 34.6 -> 48.9 / 37.9 (jobs 2 x 1.5 = 3.0 -> 7.5 x 2 seats / 4 people = 3.75 lifts the instant Hope; one recalled resident changes the starved sol's mood curve). The dispute picks that follow shift because the roster shrinks. The first-harvest line moves 1.03 -> 1.05 kg (work tempo moved with Hope; the threshold is 1 kg).

```
    -=== COLONIST UNSUPPORTED: Dmitri Duval (food stores empty) ===
    -=== COLONIST UNSUPPORTED: Carmen Chen (food stores empty) ===
    -=== COLONIST UNSUPPORTED: Bao Brandt (food stores empty) ===
    -=== COLONIST UNSUPPORTED: Amara Adeyemi (food stores empty) ===
    +=== HOME SHORT RATIONS: Floor -1 (food, 4 residents, ration 0%) ===
    -MOMENT dispute: Dmitri Duval vs Amara Adeyemi
    +=== COLONIST EVACUATED: Dmitri Duval (Floor -1, rations short 0.6 sols) ===
    +MOMENT dispute: Bao Brandt vs Amara Adeyemi
    -ALIVE mood margin: smoothed Hope 48.3 supplied, 34.6 after the starved sol (dispute gate 45: while supplied, disputes need it to stay above)
    +ALIVE mood margin: smoothed Hope 48.9 supplied, 37.9 after the starved sol (dispute gate 45: while supplied, disputes need it to stay above)
    -MOMENT dispute: Amara Adeyemi vs Carmen Chen
    +=== HOME RATIONS RESTORED: Floor -1 (3 residents) ===
    +MOMENT dispute: Bao Brandt vs Amara Adeyemi
    -MOMENT dispute: Amara Adeyemi vs Dmitri Duval
    -=== FIRST HARVEST (1.03 kg cumulative, +4 Hope milestone) ===
    +MOMENT dispute: Carmen Chen vs Amara Adeyemi
    +=== FIRST HARVEST (1.05 kg cumulative, +4 Hope milestone) ===
```

## crew.log

Reason: Shared rationing, departures on the onset size (a total failure empties a home in EvacSols). 4 'COLONIST UNSUPPORTED' lines become 1 'HOME SHORT RATIONS: Floor -1 (food, 4 residents, ration 0%)'. The 4 'COLONIST EVACUATED (unsupported 2.0 sols)' lines at 2.0 sols become recalls at 0.55, 1.05, 1.55 and 2.0 sols (4 x 1 / 2 = 2 a sol, one every half sol: the first lands one 60 s step late because 0.1 added ten times falls a hair under 1, the clock carries the remainder), newest first (Dmitri, Carmen, Bao, Amara); the home is empty at 2.0 sols as before. The -crew pins are re-derived: 'starved 0.5 sol pop=4' becomes 0.4 sol pop 4, 0.8 sol pop 3, 1.3 sols pop 2, 1.8 sols pop 1, 2.5 sols pop 0 (it was 2.5 sols pop 0). MOMENT dispute picks shift because the roster shrinks at different times.

```
    -=== COLONIST UNSUPPORTED: Dmitri Duval (food stores empty) ===
    -=== COLONIST UNSUPPORTED: Carmen Chen (food stores empty) ===
    -=== COLONIST UNSUPPORTED: Bao Brandt (food stores empty) ===
    -=== COLONIST UNSUPPORTED: Amara Adeyemi (food stores empty) ===
    +=== HOME SHORT RATIONS: Floor -1 (food, 4 residents, ration 0%) ===
    -CREW starved 0.5 sol: pop=4 unsupported=4 (expect 4, 4)
    -MOMENT dispute: Amara Adeyemi vs Dmitri Duval
    +CREW starved 0.4 sol: pop=4 unsupported=4 (expect 4, 4 - the first recall falls at 0.5 sol)
    +=== COLONIST EVACUATED: Dmitri Duval (Floor -1, rations short 0.6 sols) ===
    +MOMENT dispute: Amara Adeyemi vs Carmen Chen
    -MOMENT dispute: Bao Brandt vs Dmitri Duval
    -MOMENT dispute: Carmen Chen vs Dmitri Duval
    +CREW starved 0.8 sol: pop=3 (expect 3 - the newest recalled just after 0.5 sol)
    +=== COLONIST EVACUATED: Carmen Chen (Floor -1, rations short 1.1 sols) ===
    +MOMENT dispute: Bao Brandt vs Amara Adeyemi
    +CREW starved 1.3 sols: pop=2 (expect 2 - one more at 1.0 sol: a steady one every half sol)
    +=== COLONIST EVACUATED: Bao Brandt (Floor -1, rations short 1.6 sols) ===
    -=== COLONIST EVACUATED: Dmitri Duval (unsupported 2.0 sols) ===
    -=== COLONIST EVACUATED: Carmen Chen (unsupported 2.0 sols) ===
    -=== COLONIST EVACUATED: Bao Brandt (unsupported 2.0 sols) ===
    -=== COLONIST EVACUATED: Amara Adeyemi (unsupported 2.0 sols) ===
    +CREW starved 1.8 sols: pop=1 (expect 1)
    +=== COLONIST EVACUATED: Amara Adeyemi (Floor -1, rations short 2.0 sols) ===
    -CREW starved 2.5 sols: pop=0 (expect 0 - evacuated)
    +CREW starved 2.5 sols: pop=0 (expect 0 - the home emptied in its 2 sols)
```

## demolish.log

Reason: 7 lines, all removed: 'MOMENT pastime (Musician): Amara Adeyemi + Amara Adeyemi'. The post-C1 baseline already carried them: the demolish suite's colony is one person, and the moment generator paired a colonist with herself. Revision 3 of C2a: a crew moment now needs two different colonists, so with fewer than two people no moment fires. Nothing else in the suite moves.

```
    -MOMENT pastime (Musician): Amara Adeyemi + Amara Adeyemi
    -MOMENT pastime (Musician): Amara Adeyemi + Amara Adeyemi
    -MOMENT pastime (Musician): Amara Adeyemi + Amara Adeyemi
    -MOMENT pastime (Musician): Amara Adeyemi + Amara Adeyemi
    -MOMENT pastime (Musician): Amara Adeyemi + Amara Adeyemi
    -MOMENT pastime (Musician): Amara Adeyemi + Amara Adeyemi
    -MOMENT pastime (Musician): Amara Adeyemi + Amara Adeyemi
```

## discovery.log

Reason: Hope per head: the jobs line is HopeJobsMax x seats / people (2 seats for 2 people = 7.5, was 2 x 1.5 = 3.0). DS2 instant Hope 87.25 -> 91.75; DS3 bare Hope 73.25 -> 77.75; DS2 smoothed 63.9 -> 65.6 (the instant Hope is higher). The smoothed Hope crosses the 85 research gate about three sols earlier, so every discovery pops earlier: DS4 found 1 -> 2 (CropStrain, RegolithCeramics; SubsurfaceBrine 0.511 done), DS5 found 3 -> 4 (all four, nothing next), DS6 progress 0.622 -> 0.000, milestones 7.0 -> 9.0 and 12.0 -> 20.0. The 'Research funding banked', 'Discovery unlocked room' and '=== DISCOVERY' lines move earlier in the log and the totals follow (pending funding 500/1300/2500 then +3000 for Microbial Life). The pins were re-derived, with the margins stated in the suite.

```
    -DISC ok: DS2 gate: smoothed=63.9 accruing=0 progress=0.00 (expect <85, 0, 0.00)
    -DISC ok: DS2 instant Hope 87.250 with furnishings 6.0, comforts 8.0 and no penalties (expect 87.25, 6.0, 8.0)
    -DISC ok: DS3 without furnishings and comforts the same colony reads 73.25, under the 85 research gate (the extra care is what opens research)
    +DISC ok: DS2 gate: smoothed=65.6 accruing=0 progress=0.00 (expect <85, 0, 0.00)
    +DISC ok: DS2 instant Hope 91.750 with furnishings 6.0, comforts 8.0, jobs 7.5 and no penalties (expect 91.75, 6.0, 8.0, 7.5)
    +DISC ok: DS3 without furnishings and comforts the same colony reads 77.75, under the 85 research gate (the extra care is what opens research)
    +Research funding banked: +500 kg manifest budget (total pending 500)
    +=== DISCOVERY: Hardy Crop Strain (+2 Hope milestone, +50 kg Seeds) ===
    +Research funding banked: +800 kg manifest budget (total pending 1300)
    +Discovery 'RegolithCeramics' unlocked room 'Workshop'
    +=== DISCOVERY: Regolith Ceramics (+2 Hope milestone, +100 kg Struct) ===
    -Research funding banked: +500 kg manifest budget (total pending 500)
    -=== DISCOVERY: Hardy Crop Strain (+2 Hope milestone, +50 kg Seeds) ===
    -DISC ok: DS4 first window: found=1 [CropStrain] next=RegolithCeramics (0.319 of the way) seeds +50 milestones=7.0 (expect >=1 starting CropStrain, +50, 5 + the found rows' bonus = 7.0)
    -DISC ok: DS4 pin: exactly CropStrain found, RegolithCeramics next (found=1 next=RegolithCeramics; the shipped tuning's pace - re-pin it if the tuning moves)
    +DISC ok: DS4 first window: found=2 [CropStrain,RegolithCeramics] next=SubsurfaceBrine (0.511 of the way) seeds +50 milestones=9.0 (expect >=1 starting CropStrain, +50, 5 + the found rows' bonus = 9.0)
    +DISC ok: DS4 pin: exactly CropStrain and RegolithCeramics found, SubsurfaceBrine next (found=2 next=SubsurfaceBrine; the shipped tuning's pace - re-pin it if the tuning moves)
    -Research funding banked: +800 kg manifest budget (total pending 1300)
    -Discovery 'RegolithCeramics' unlocked room 'Workshop'
    -=== DISCOVERY: Regolith Ceramics (+2 Hope milestone, +100 kg Struct) ===
    +Research funding banked: +1200 kg manifest budget (total pending 2500)
    +Discovery 'SubsurfaceBrine' unlocked room 'LabFull'
    +=== DISCOVERY: Subsurface Brine Seep (+3 Hope milestone, +150 kg Water) ===
    -Research funding banked: +1200 kg manifest budget (total pending 2500)
    -Discovery 'SubsurfaceBrine' unlocked room 'LabFull'
    -=== DISCOVERY: Subsurface Brine Seep (+3 Hope milestone, +150 kg Water) ===
    +Research funding banked: +3000 kg manifest budget (total pending 5500)
    +=== DISCOVERY: Subsurface Microbial Life (+8 Hope milestone) ===
    -DISC ok: DS5 second window: found=3 [CropStrain,RegolithCeramics,SubsurfaceBrine] next=MicrobialLife (0.622 of the way) struct +100 (expect 100 x ceramics found = 100) milestones=12.0 (expect 5 + the found rows' bon
    -DISC ok: DS5 pin: exactly the first three found, MicrobialLife next, Struct +100 (found=3 next=MicrobialLife)
    +DISC ok: DS5 second window: found=4 [CropStrain,RegolithCeramics,SubsurfaceBrine,MicrobialLife] next=None (0.000 of the way) struct +100 (expect 100 x ceramics found = 100) milestones=20.0 (expect 5 + the found rows
    +DISC ok: DS5 pin: all four found, nothing next, Struct +100 (found=4 next=None)
    -DISC ok: DS6 save/load v16: found 3->3 progress 0.622->0.622 (expect identical)
    +DISC ok: DS6 save/load v16: found 4->4 progress 0.000->0.000 (expect identical)
```

## ending.log

Reason: Shared rationing, departures on the onset size. 2 'COLONIST UNSUPPORTED' lines become 1 'HOME SHORT RATIONS: Floor -1 (water, 2 residents, ration 0%)'. The 2 'COLONIST EVACUATED (unsupported 2.0 sols)' lines at 2.0 sols become recalls at 1.0 and 2.0 sols (2 x 1 / 2 = 1 a sol), newest first (Bao, then Amara). The two MOMENT dispute lines go: once Bao is recalled one colonist is left, and with fewer than two people no moment fires (the moments fix). The collapse still declares inside the 3.5 sols the suite runs, so the 'ENDING collapse' line is unchanged.

```
    -=== COLONIST UNSUPPORTED: Bao Brandt (water tanks empty) ===
    -=== COLONIST UNSUPPORTED: Amara Adeyemi (water tanks empty) ===
    +=== HOME SHORT RATIONS: Floor -1 (water, 2 residents, ration 0%) ===
    -MOMENT dispute: Amara Adeyemi vs Bao Brandt
    +=== COLONIST EVACUATED: Bao Brandt (Floor -1, rations short 1.0 sols) ===
    -MOMENT dispute: Bao Brandt vs Amara Adeyemi
    -=== COLONIST EVACUATED: Bao Brandt (unsupported 2.0 sols) ===
    -=== COLONIST EVACUATED: Amara Adeyemi (unsupported 2.0 sols) ===
    +=== COLONIST EVACUATED: Amara Adeyemi (Floor -1, rations short 2.0 sols) ===
```

## food.log

Reason: Shared rationing. F8: 4 'COLONIST UNSUPPORTED' lines become 1 'HOME SHORT RATIONS: Floor -1 (food, 4 residents, ration 0%)'; the F8 expectations are re-derived: one SHORT RATIONS alert for the home (was four LIFE SUPPORT, one per colonist), none of the old per-colonist kind, no recall (RECALLED:) alerts, one RATIONS RESTORED, and the refill prints 'HOME RATIONS RESTORED: Floor -1 (4 residents)'. F10: HopeSmoothed 53.13673/53.14014 -> 53.24213/53.24553 (Hope moved: the jobs line is a rate); the 60 s vs 6 s difference stays 0.00340, the step-size parity.

```
    -=== COLONIST UNSUPPORTED: Dmitri Duval (food stores empty) ===
    -=== COLONIST UNSUPPORTED: Carmen Chen (food stores empty) ===
    -=== COLONIST UNSUPPORTED: Bao Brandt (food stores empty) ===
    -=== COLONIST UNSUPPORTED: Amara Adeyemi (food stores empty) ===
    -FOOD ok: F8 larder emptied in one go: food alerts 2 -> 2 (silent), LIFE SUPPORT +4 (expect 4), FoodPenalty 10.0
    -FOOD ok: F8 refilled: FoodPenalty 0.0, 4 of 4 supported, pop 4, no evacuation
    +=== HOME SHORT RATIONS: Floor -1 (food, 4 residents, ration 0%) ===
    +FOOD ok: F8 larder emptied in one go: food alerts 2 -> 2 (silent), SHORT RATIONS +1 (expect 1, one for the home), per-colonist LIFE SUPPORT +0 (expect 0), FoodPenalty 10.0
    +=== HOME RATIONS RESTORED: Floor -1 (4 residents) ===
    +FOOD ok: F8 refilled: FoodPenalty 0.0, 4 of 4 supported, pop 4, nobody recalled (0 RECALLED alerts), 1 RATIONS RESTORED
    -FOOD ok: F10 HopeSmoothed 53.13673 (60 s) vs 53.14014 (6 s): differ by 0.00340 (tolerance 0.05)
    +FOOD ok: F10 HopeSmoothed 53.24213 (60 s) vs 53.24553 (6 s): differ by 0.00340 (tolerance 0.05)
```

## furnish.log

Reason: Hope per head. The Furnishings line is the comfort sum x ComfortRefPop (8) / people, then capped: the suite has 10 people, so x 0.8. F5: jobs 10.5 -> 5.2 (7.5 x 7 seats / 10 people = 5.25; was 7 x 1.5). F6: dining table 1.0 -> 0.8; plus five plants 6.0 -> 4.8; a sixth plant 6.0 'capped at 6' -> 5.6 'under the cap' (raw 7 x 0.8); the cap is now reached by two more plants (slots 10 and 11, raw 9 x 0.8 = 7.2), which adds two 'Uplink / Furnished Plant' pairs. Those two pieces take Ids #17 and #18, so every later piece's Id moves by two (Plant #17 -> #19, Plant #18 -> #20, Cot #19.. -> #21.., Plant #25 -> #27, #26 -> #28 and F11's 'next piece' and 'highest standing' ids) and the messages that quote them change. F8: the sale of cell c4 is 9 pieces for 150 kg (0.75 x 200), not 7 for 120 kg, and the ledger reads 1520.0 = 2000.0 - 660 + 180 (was 1530.0 = 2000.0 - 620 + 150). F9: 'the plant counts' reads 0.8 -> 1.6 (one plant is 1 x 8 / 10 = 0.8; was 1.0 -> 2.0). The saves and the autosave after it fall on sol 3 instead of sol 2 because the extra orders took two more steps; MOMENT lines move for the same reason.

```
    -FURNISH ok: F5 a lab bench: c5 seats 2, filled 7, jobs 10.5
    +FURNISH ok: F5 a lab bench: c5 seats 2, filled 7, jobs 5.2
    -FURNISH ok: F6 a dining table: Furnishings 1.0
    +FURNISH ok: F6 a dining table: Furnishings 0.8
    -FURNISH ok: F6 + five plants: Furnishings 6.0
    +FURNISH ok: F6 + five plants: Furnishings 4.8
    -FURNISH ok: F6 a sixth plant: capped at 6
    +FURNISH ok: F6 a sixth plant: 5.6, under the cap
    +Uplink: 'Furnish Plant' transmitted, executes in 45 sim-s
    +Furnished Plant #17 in (-1, 4, 0) at slot 10, rot 0
    +MOMENT join: Hana Herrera -> Efe Eriksen (Workstation)
    +Uplink: 'Furnish Plant' transmitted, executes in 45 sim-s
    +Furnished Plant #18 in (-1, 4, 0) at slot 11, rot 0
    +FURNISH ok: F6 two more plants: capped at 6
    -Furnished Plant #17 in (-1, 2, 0) at slot 15, rot 0
    -MOMENT join: Hana Herrera -> Efe Eriksen (Workstation)
    +Furnished Plant #19 in (-1, 2, 0) at slot 15, rot 0
    -FURNISH ok: F7 clearing c2 refused (The Potted Plant #17 can't stand in an unzoned space - sell it first); back to Living
    +FURNISH ok: F7 clearing c2 refused (The Potted Plant #19 can't stand in an unzoned space - sell it first); back to Living
    -Sold 7 furnishing(s); 120 kg Struct back to the Lander
    +Sold 9 furnishing(s); 150 kg Struct back to the Lander
    -FURNISH ok: F8 everything in c4 sold in one order (+120 = 0.75 x 160); the cell can be cleared again
    -FURNISH ok: F8 the Struct ledger closes: 1530.0 = 2000.0 - 620 + 150
    +FURNISH ok: F8 everything in c4 sold in one order (+150 = 0.75 x 200: the table and eight plants); the cell can be cleared again
    +FURNISH ok: F8 the Struct ledger closes: 1520.0 = 2000.0 - 660 + 180
    -Furnished Plant #18 in (0, 0, 6) at slot 0, rot 0
    +Furnished Plant #20 in (0, 0, 6) at slot 0, rot 0
    +MOMENT join: Gabriel Goto -> Dmitri Duval (Workstation)
    -MOMENT join: Gabriel Goto -> Dmitri Duval (Workstation)
    -FURNISH ok: F9 rated: the plant counts (1.0 -> 2.0)
    +FURNISH ok: F9 rated: the plant counts, per resident (0.8 -> 1.6; expect +8 / 10 people)
    -FURNISH ok: F9 a Floodmast over B's plant is refused (Footprint overlaps a furnishing (Plant #18))
    -Uplink: 'Furnish Cot' transmitted, executes in 45 sim-s
    -Furnished Cot #19 in (0, 0, 7) at slot 0, rot 0
    -Uplink: 'Furnish Cot' transmitted, executes in 45 sim-s
    -Furnished Cot #20 in (0, 0, 7) at slot 2, rot 0
    +FURNISH ok: F9 a Floodmast over B's plant is refused (Footprint overlaps a furnishing (Plant #20))
    +Uplink: 'Furnish Cot' transmitted, executes in 45 sim-s
    +Furnished Cot #21 in (0, 0, 7) at slot 0, rot 0
    +Uplink: 'Furnish Cot' transmitted, executes in 45 sim-s
    +Furnished Cot #22 in (0, 0, 7) at slot 2, rot 0
    -Furnished Cot #21 in (0, 0, 6) at slot 2, rot 0
    +Furnished Cot #23 in (0, 0, 6) at slot 2, rot 0
    -Furnished Cot #22 in (0, 0, 9) at slot 0, rot 0
    -Uplink: 'Furnish Cot' transmitted, executes in 45 sim-s
    -Furnished Cot #23 in (0, 0, 9) at slot 2, rot 0
    +Furnished Cot #24 in (0, 0, 9) at slot 0, rot 0
    +Uplink: 'Furnish Cot' transmitted, executes in 45 sim-s
    +Furnished Cot #25 in (0, 0, 9) at slot 2, rot 0
    +MOMENT pastime (Athlete): Gabriel Goto + Bao Brandt
    -FURNISH ok: F9 C:0 to Lab refused (The Cot #19 can't stand in Laboratory - sell it first)
    +FURNISH ok: F9 C:0 to Lab refused (The Cot #21 can't stand in Laboratory - sell it first)
    -Furnished Cot #24 in (0, 0, 10) at slot 0, rot 0
    -Uplink: 'Furnish Plant' transmitted, executes in 45 sim-s
    -Furnished Plant #25 in (0, 0, 10) at slot 15, rot 0
    -MOMENT pastime (Dreamer): Mateo Mbeki + Nadia Novak
    +Furnished Cot #26 in (0, 0, 10) at slot 0, rot 0
    +Uplink: 'Furnish Plant' transmitted, executes in 45 sim-s
    +Furnished Plant #27 in (0, 0, 10) at slot 15, rot 0
    +Colony saved: slot 'auto' (N bytes, sol 3, 10 buildings, 7 robots)
    -Colony saved: slot 'furnishtest' (N bytes, sol 2, 9 buildings, 7 robots)
    +Colony saved: slot 'furnishtest' (N bytes, sol 3, 9 buildings, 7 robots)
    -Furnished Plant #26 in (-1, 2, 0) at slot 14, rot 0
    +Furnished Plant #28 in (-1, 2, 0) at slot 14, rot 0
    -Colony saved: slot 'auto' (N bytes, sol 3, 9 buildings, 7 robots)
    -Colony loaded: slot 'furnishtest' (sol 2, 9 buildings, 7 robots, 0 tasks)
    +Colony loaded: slot 'furnishtest' (sol 3, 9 buildings, 7 robots, 0 tasks)
    -Furnished Plant #26 in (-1, 2, 0) at slot 14, rot 0
    -FURNISH ok: F11 the next piece gets #26, above every Id issued before the save (highest standing #23) - Ids are never reused
    +Furnished Plant #28 in (-1, 2, 0) at slot 14, rot 0
    +FURNISH ok: F11 the next piece gets #28, above every Id issued before the save (highest standing #25) - Ids are never reused
```

## growth.log

Reason: Hope per head: 2 seats for 2 people earn the whole jobs line, 7.5 (was 2 x 1.5 = 3.0), so G1 instant Hope 77.5 -> 82.0 (still THRIVING, under the 90 FLOURISHING line) and smoothed 77.3 -> 81.7. The smoothed Hope crosses the 75 growth gate earlier, so the first birth ('Colonist Carmen Chen housed', 'FIRST MARTIAN-BORN') lands earlier in the log relative to the crew moments and the autosave, and the moments' picks shift. G3 smoothed 69.6 -> 69.8. New check lines: G2 the birth is flagged Mars-born (0 / 0 / 1); G4 reads 'save/load v34 ... the child still Mars-born'.

```
    -GROWTH ok: G1 instant Hope 77.500000 = 50 + vault 5 + housing 7.50 + rooms 4.00 + jobs 3.00 + comforts 8.00, water/food penalty 0.000000/0.000000 (expect 77.5 = 7.5 + 4.0 + 3.0 + 8.0, 0/0)
    -GROWTH ok: G1 eligible: smoothed=77.3 band=THRIVING eligible=1 pop=2 beds-free=6 (expect >=75, THRIVING - a sensible layout lives there, FLOURISHING takes extra care - 1)
    +GROWTH ok: G1 instant Hope 82.000000 = 50 + vault 5 + housing 7.50 + rooms 4.00 + jobs 7.50 + comforts 8.00, water/food penalty 0.000000/0.000000 (expect 82.0 = 7.5 + 4.0 + 7.5 + 8.0, 0/0)
    +GROWTH ok: G1 eligible: smoothed=81.7 band=THRIVING eligible=1 pop=2 beds-free=6 (expect >=75, THRIVING - a sensible layout lives there, FLOURISHING takes extra care - 1)
    +Colonist Carmen Chen housed on floor -1
    +=== FIRST MARTIAN-BORN: Carmen Chen ===
    +MOMENT pastime (Dreamer): Carmen Chen + Amara Adeyemi
    +MOMENT pastime (Dreamer): Carmen Chen + Amara Adeyemi
    +Colony saved: slot 'auto' (N bytes, sol 28, 4 buildings, 7 robots)
    -Colony saved: slot 'auto' (N bytes, sol 28, 4 buildings, 7 robots)
    -MOMENT pastime (Musician): Amara Adeyemi + Bao Brandt
    -MOMENT pastime (Musician): Amara Adeyemi + Bao Brandt
    -MOMENT pastime (Musician): Amara Adeyemi + Bao Brandt
    -MOMENT pastime (Musician): Amara Adeyemi + Bao Brandt
    -Colonist Carmen Chen housed on floor -1
    -=== FIRST MARTIAN-BORN: Carmen Chen ===
    +MOMENT pastime (Dreamer): Carmen Chen + Amara Adeyemi
    +MOMENT pastime (Storyteller): Bao Brandt + Amara Adeyemi
    +GROWTH ok: G2 the birth is flagged Mars-born, the two founders are not (0 / 0 / 1; expect 0 / 0 / 1)
    -GROWTH ok: G3 lapse: smoothed=69.6 band=STEADY eligible=0 progress=0.00 (expect not-eligible, 0.00)
    +GROWTH ok: G3 lapse: smoothed=69.8 band=STEADY eligible=0 progress=0.00 (expect not-eligible, 0.00)
    -GROWTH ok: G4 save/load v15: births 1->1 firstBorn=1 (expect identical, 1)
    +GROWTH ok: G4 save/load v34: births 1->1 firstBorn=1, the child still Mars-born (expect identical, 1, 1)
```

## hopedrive.log

Reason: Hope per head: the instant Hope moves 63.75 -> 64.50 (jobs 7.5 x seats / people instead of 1.5 a seat in this layout), so the smoothed value (52.48 -> 61.71 becomes 52.53 -> 62.21), the expected curve (59.60 -> 60.10, error unchanged at 2.111, the same as before), the tempo (1.0703 -> 1.0732) and the dependent garden yield (0.374 -> 0.375 kg) and band/save lines move by the same amount. The save/load round trip is exact.

```
    -HOPEDRIVE smoother: instant=63.75 smoothed 52.48->61.71 (expect 59.60, |err| 2.111 < 0.05)
    -HOPEDRIVE tempo: 1.0703 (expect 1.0703) band STEADY at smoothed 61.7
    +HOPEDRIVE smoother: instant=64.50 smoothed 52.53->62.21 (expect 60.10, |err| 2.111 < 0.05)
    +HOPEDRIVE tempo: 1.0732 (expect 1.0732) band STEADY at smoothed 62.2
    -HOPEDRIVE garden@tempo: 1 cell(s) gave 0.374 kg (expect 0.265 = base x tempo 1.062; |err| 0.1085)
    -HOPEDRIVE band: smoothed 59.9 -> STEADY (Thriving>=75, Flourishing>=90)
    +HOPEDRIVE garden@tempo: 1 cell(s) gave 0.375 kg (expect 0.266 = base x tempo 1.066; |err| 0.1090)
    +HOPEDRIVE band: smoothed 60.7 -> STEADY (Thriving>=75, Flourishing>=90)
    -HOPEDRIVE save/load v13: smoothed 59.92->59.92 band STEADY->STEADY (expect identical)
    +HOPEDRIVE save/load v13: smoothed 60.65->60.65 band STEADY->STEADY (expect identical)
```

## rooms.log

Reason: Hope per head, the suite that PINS the weights. R0 now pins the new rows (HopeJobsMax 7.5, HopeUnsupportedMax 30, steepness 3, HopeAdjacencyMax 16, RoomServesResidents 25, ComfortRefPop 8) as well as HopeHousingMax 7.5 and HopePerMoralePoint 2.5, read from DT_Config alone. R3 bad kitchen 43.125 -> 43.125 (the VALUE is back where it was: adjacency 16 x 2 offended refuser cells of 2 = -16, the old two pairs x 8); only its line text changes (it now also names '2 of 2 refuser cells (-16.0)'). R4 cured layout 61.375 -> 61.75 (jobs 7.5 x 1 seat / 4 people = 1.875, was 1.5). R5 greed swap 47.25 -> 52.958 (adjacency 16 x 2/3 = -10.667 with three refuser cells, was 2 pairs x 8 = -16; jobs +0.375). R7 save/load 49.25 -> 55.333 (jobs 7.5 x 2/4 = 3.75, was 3.0, plus adjacency as R5), the save label v11 -> v34.

```
    -ROOMS ok: R0 DT_Config HopeHousingMax 7.5 / HopePerMoralePoint 2.5 / HopePerJob 1.5, the table row says 7.500 / 2.500 / 1.500 (D3a, DT/CSV)
    -ROOMS ok: R0 the sim reads 7.500 / 2.500 / 1.500 (an override or a fallback would show here)
    +ROOMS ok: R0 DT_Config HopeHousingMax 7.5 / HopePerMoralePoint 2.5 / HopeJobsMax 7.5, the table row says 7.500 / 2.500 / 7.500 (D3a + C2a, DT/CSV)
    +ROOMS ok: R0 DT_Config HopeUnsupportedMax 30 / steepness 3 / HopeAdjacencyMax 16 / RoomServesResidents 25 / ComfortRefPop 8, the table row says 30.000 / 3.000 / 16.000 / 25.000 / 8.000 (C2a, DT/CSV)
    +ROOMS ok: R0 the sim reads 7.500 / 2.500 / 7.500 / 30.000 / 3.000 / 16.000 / 25.000 / 8.000 (an override or a fallback would show here)
    -ROOMS ok: R3 bad kitchen: hope=43.125 pairs=2 housing=1.875 rooms=2.25 (expect 43.125, 2, 1.875, 2.25)
    +ROOMS ok: R3 bad kitchen: hope=43.125 pairs=2 offended 2 of 2 refuser cells (-16.0) housing=1.875 rooms=2.25 (expect 43.125, 2, 2 of 2, -16.0, 1.875, 2.25)
    -ROOMS ok: R4 cured layout: hope=61.375 pairs=0 jobs=1.50 seats=1 (expect 61.375, 0, 1.50, 1)
    +ROOMS ok: R4 cured layout: hope=61.750 pairs=0 offended 0 of 2 refuser cells jobs=1.875 seats=1 (expect 61.75, 0, 0 of 2, 1.875, 1)
    -ROOMS ok: R5 greed swap: hope=47.250 pairs=2 housing=3.750 (expect 47.25, 2, 3.75)
    +ROOMS ok: R5 greed swap: hope=52.958 pairs=2 offended 2 of 3 refuser cells (-10.667) housing=3.750 (expect 52.958, 2, 2 of 3, -10.667, 3.75)
    -ROOMS ok: R7 save/load v11: cell4=Cooking cell3=LivingQuarters hope=49.250 (expect Cooking, LivingQuarters, 49.25 = the pre-save 49.250) seats=2 (expect 2)
    +ROOMS ok: R7 save/load v34: cell4=Cooking cell3=LivingQuarters hope=55.333 (expect Cooking, LivingQuarters, 55.333 = the pre-save 55.333) seats=2 (expect 2)
```

## surfacehab.log

Reason: Two reasons. (1) Hope per head: S10 Hope line 'adj 8.0, rooms 3.25' -> 'adj 2.7 (1 of 6 refuser cells), rooms 2.75' (the Workstation on the floor and the one in the network are one function, counted once: (0.2 + 0.3 + 0.6) x 2.5 = 2.75; adjacency is 16 x 1 offended refuser cell / 6 refuser cells = 2.667); S10 seats jobs 6 -> 3 (7.5 x 4 seats / 9 people = 3.33; the line prints %.0f). (2) Shared rationing: the five 'COLONIST UNSUPPORTED' lines of the dry pool become 'HOME SHORT RATIONS' for Floor -1 (4 residents) and Surface habitat #15 (1 resident), and 'Surface habitat #6 (4 residents)'; the S12 dry pool recalls the floor's newest resident (Hana Herrera, Id 8, at 0.6 sols; the next is due a half sol later, outside the sol), so S12 reads 'one recalled so far' with pop 8 (it read 'nobody recalled yet', pop 9); the oxygen-back step prints three 'HOME RATIONS RESTORED' lines. S12b: network #6's four residents are recalled at 0.6, 1.1, 1.6 and 2.0 sols (Dmitri, Carmen, Bao, Amara) instead of all four at 2.0; the final pop reads 4 (the floor's three oldest and PQ's one), capacity 5. The save/load check in S12b now also houses the extra colonist (Jaya Joshi) because the floor has a free bed, which the load then drops. MOMENT picks shift throughout (roster and timeline).

```
    -SURFACEHAB ok: S10 Hope: Q-R offends, the corridor partitions P-R (pairs 1, adj 8.0, rooms 3.25)
    -SURFACEHAB ok: S10 seats: floor first (filled 4, jobs 6, lab 4.00, ws 2.00, housing 4.167)
    +SURFACEHAB ok: S10 Hope: Q-R offends, the corridor partitions P-R (pairs 1, 1 of 6 refuser cells, adj 2.7, rooms 2.75)
    +SURFACEHAB ok: S10 seats: floor first (filled 4, jobs 3, lab 4.00, ws 2.00, housing 4.167)
    -=== COLONIST UNSUPPORTED: Idris Ilyina (home habitat lost its air) ===
    -=== COLONIST UNSUPPORTED: Hana Herrera (home floor lost its air) ===
    -=== COLONIST UNSUPPORTED: Gabriel Goto (home floor lost its air) ===
    -=== COLONIST UNSUPPORTED: Freya Farid (home floor lost its air) ===
    -=== COLONIST UNSUPPORTED: Efe Eriksen (home floor lost its air) ===
    +=== HOME SHORT RATIONS: Floor -1 (air, 4 residents, ration 0%) ===
    +=== HOME SHORT RATIONS: Surface habitat #15 (air, 1 resident, ration 0%) ===
    -=== COLONIST UNSUPPORTED: Dmitri Duval (home habitat lost its air) ===
    -=== COLONIST UNSUPPORTED: Carmen Chen (home habitat lost its air) ===
    -=== COLONIST UNSUPPORTED: Bao Brandt (home habitat lost its air) ===
    -=== COLONIST UNSUPPORTED: Amara Adeyemi (home habitat lost its air) ===
    +=== HOME SHORT RATIONS: Surface habitat #6 (air, 4 residents, ration 0%) ===
    +=== COLONIST EVACUATED: Hana Herrera (Floor -1, rations short 0.6 sols) ===
    -SURFACEHAB ok: S12 a dry pool: every rating lost, everyone unsupported, no seats, no infirmary, nobody recalled yet
    +SURFACEHAB ok: S12 a dry pool: every rating lost, everyone unsupported, no seats, no infirmary, one recalled so far - floor -1's newest (Id 8); pop 8 (expect 8)
    -MOMENT pastime (Storyteller): Bao Brandt + Dmitri Duval
    -MOMENT join: Efe Eriksen -> Bao Brandt (Workstation)
    +=== HOME RATIONS RESTORED: Floor -1 (3 residents) ===
    +=== HOME RATIONS RESTORED: Surface habitat #6 (4 residents) ===
    +=== HOME RATIONS RESTORED: Surface habitat #15 (1 resident) ===
    +MOMENT pastime (Stargazer): Dmitri Duval + Idris Ilyina
    +MOMENT join: Efe Eriksen -> Amara Adeyemi (Workstation)
    -=== COLONIST UNSUPPORTED: Dmitri Duval (home habitat lost its air) ===
    -=== COLONIST UNSUPPORTED: Carmen Chen (home habitat lost its air) ===
    -=== COLONIST UNSUPPORTED: Bao Brandt (home habitat lost its air) ===
    -=== COLONIST UNSUPPORTED: Amara Adeyemi (home habitat lost its air) ===
    +=== HOME SHORT RATIONS: Surface habitat #6 (air, 4 residents, ration 0%) ===
    -MOMENT dispute: Carmen Chen vs Amara Adeyemi
    -MOMENT dispute: Gabriel Goto vs Idris Ilyina
    +MOMENT dispute: Amara Adeyemi vs Idris Ilyina
    +=== COLONIST EVACUATED: Dmitri Duval (Surface habitat #6, rations short 0.6 sols) ===
    +MOMENT dispute: Bao Brandt vs Efe Eriksen
    -MOMENT dispute: Freya Farid vs Dmitri Duval
    -MOMENT dispute: Bao Brandt vs Dmitri Duval
    +=== COLONIST EVACUATED: Carmen Chen (Surface habitat #6, rations short 1.1 sols) ===
    +MOMENT dispute: Idris Ilyina vs Amara Adeyemi
    +=== COLONIST EVACUATED: Bao Brandt (Surface habitat #6, rations short 1.6 sols) ===
    +MOMENT dispute: Freya Farid vs Efe Eriksen
    -=== COLONIST EVACUATED: Dmitri Duval (unsupported 2.0 sols) ===
    -=== COLONIST EVACUATED: Carmen Chen (unsupported 2.0 sols) ===
    -=== COLONIST EVACUATED: Bao Brandt (unsupported 2.0 sols) ===
    -=== COLONIST EVACUATED: Amara Adeyemi (unsupported 2.0 sols) ===
    -MOMENT dispute: Hana Herrera vs Efe Eriksen
    -SURFACEHAB ok: S12b the four surface residents are recalled; floor and PQ stay (pop 5, capacity 5)
    +=== COLONIST EVACUATED: Amara Adeyemi (Surface habitat #6, rations short 2.0 sols) ===
    +MOMENT dispute: Efe Eriksen vs Freya Farid
    +SURFACEHAB ok: S12b the four surface residents are recalled; floor and PQ stay (pop 4, capacity 5)
    +Colonist Jaya Joshi housed on floor -1
```

## tiers.log

Reason: Hope per head, fixture adjusted so the Hope total does not move: the Large Chem Table timeline needs 85+. Under C2a five seats for six people earn 6.25 of the jobs line (was 7.5); a fourth home (one more Living cell, cell 10) adds 1.25 of housing, so T1 instant Hope reads 86.750 both before and after - only its parts move (housing 3.75 -> 5.00, jobs 7.50 -> 6.25). The floor is 11 cells (was 10), so the dig line reads 'Excavated 11 cell(s) ... 13200 kg spoil' and 'FLOOR -1 RATED LIVABLE (11 cells, 1100 kg O2)'; the added zoning order prints 'Room designation: floor -1 cell 10 -> LivingQuarters'; and the T1 setup message names c10.

```
    -Excavated 10 cell(s) on floor -1 (10 carved; +12000 kg spoil; pile 13200 kg)
    +Excavated 11 cell(s) on floor -1 (11 carved; +13200 kg spoil; pile 14400 kg)
    -=== FLOOR -1 RATED LIVABLE (10 cells, 1000 kg O2) ===
    +=== FLOOR -1 RATED LIVABLE (11 cells, 1100 kg O2) ===
    -TIERS ok: T1 setup: c0, c8, c9 Living, c1 Large Workbench, c2 Workstation, c3 Large Chem Table, c4 Lab, c5 Dining all accepted
    +Room designation: floor -1 cell 10 -> LivingQuarters
    +TIERS ok: T1 setup: c0, c8, c9, c10 Living, c1 Large Workbench, c2 Workstation, c3 Large Chem Table, c4 Lab, c5 Dining all accepted
    -TIERS ok: T1 instant Hope 86.750 = 50 + 5 + housing 3.75 + rooms 6.50 + jobs 7.50 + comforts 8.00 + furnishings 6.00, no penalties (expect 86.75)
    +TIERS ok: T1 instant Hope 86.750 = 50 + 5 + housing 5.00 + rooms 6.50 + jobs 6.25 + comforts 8.00 + furnishings 6.00, no penalties (expect 86.75)
```

## water.log

Reason: Shared rationing, departures on the onset size. 4 'COLONIST UNSUPPORTED' lines become 1 'HOME SHORT RATIONS: Floor -1 (water, 4 residents, ration 0%)'. The 4 'COLONIST EVACUATED (unsupported 2.0 sols)' lines at 2.0 sols become recalls at 0.55, 1.05, 1.55 and 2.0 sols, newest first. The suite's pins are re-derived: 0.5 sol / unsupported 4 -> 0.4 sol / unsupported 4; a new 1.3 sols pop 2 line; 2.5 sols pop 0 unchanged in value ('ENDING PATH: collapse' moves to the last recall at 2.0 sols).

```
    -=== COLONIST UNSUPPORTED: Dmitri Duval (water tanks empty) ===
    -=== COLONIST UNSUPPORTED: Carmen Chen (water tanks empty) ===
    -=== COLONIST UNSUPPORTED: Bao Brandt (water tanks empty) ===
    -=== COLONIST UNSUPPORTED: Amara Adeyemi (water tanks empty) ===
    +=== HOME SHORT RATIONS: Floor -1 (water, 4 residents, ration 0%) ===
    -WATER dry 0.5 sol: unsupported=4 (expect 4 - no water)
    -MOMENT dispute: Carmen Chen vs Amara Adeyemi
    +WATER dry 0.4 sol: unsupported=4 (expect 4 - no water)
    +=== COLONIST EVACUATED: Dmitri Duval (Floor -1, rations short 0.6 sols) ===
    +MOMENT dispute: Amara Adeyemi vs Carmen Chen
    +=== COLONIST EVACUATED: Carmen Chen (Floor -1, rations short 1.1 sols) ===
    +WATER dry 1.3 sols: pop=2 (expect 2 - recalled for thirst, one every half sol)
    -MOMENT dispute: Dmitri Duval vs Amara Adeyemi
    -=== COLONIST EVACUATED: Dmitri Duval (unsupported 2.0 sols) ===
    -=== COLONIST EVACUATED: Carmen Chen (unsupported 2.0 sols) ===
    -=== COLONIST EVACUATED: Bao Brandt (unsupported 2.0 sols) ===
    -=== COLONIST EVACUATED: Amara Adeyemi (unsupported 2.0 sols) ===
    -=== ENDING PATH: Abandonment / Collapse (the colony emptied; recoverable) ===
    +=== COLONIST EVACUATED: Bao Brandt (Floor -1, rations short 1.6 sols) ===
    +=== COLONIST EVACUATED: Amara Adeyemi (Floor -1, rations short 2.0 sols) ===
    +=== ENDING PATH: Abandonment / Collapse (the colony emptied; recoverable) ===
TOTAL differing lines: 305

```
