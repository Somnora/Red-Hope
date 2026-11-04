# Balance pass 1 (A, B, D): battery moves, round 0

`python3 scripts/sim/diff_battery.py Saved/baselines/battery_post_p2 Saved/RHBattery 100000` (full output: `work/r0/battery_diff.txt`). Every line it printed is below, grouped by log, in its order, each assigned to A (transport food 25 kg), B (HopeMilestonesMax 15), D (EarthTensionDemandThreshold 45) or the new check, with the value or term it prints and why it changed. Written by `work/r0/write_moves.py` from explicit rules; a line no rule assigns would print UNASSIGNED (none does).

**TOTAL differing lines: 142.** No wall-clock line moved (EP4, X16 and ARRV e print the same digits as the baseline in this run). No version or size line moved: `checks/sizes.py` compares every "Colony saved: slot ... (N bytes" count and every version line in all 64 logs, in order: 0 logs differ.


## arrivals.log: 12893 -> 12893 sim lines, 34 differing

1. `-` **A**: `ARRV ok: P0 DT_Config pins: ColonistTransportColonists=20 ColonistTransportFoodKgPerColonist=50 BirthRefPop=25 ImmigrationHopeMin=75 ImmigrationHopeFloor=70 ImmigrationBase=4 ImmigrationPerCapita=0.2 ImmigrationCapP`  
   P0 prints the synced DT row ColonistTransportFoodKgPerColonist (GetConfigTableScalar): 50 -> 25; the pin literal is updated to 25 (brief 3.5)
2. `+` **A**: `ARRV ok: P0 DT_Config pins: ColonistTransportColonists=20 ColonistTransportFoodKgPerColonist=25 BirthRefPop=25 ImmigrationHopeMin=75 ImmigrationHopeFloor=70 ImmigrationBase=4 ImmigrationPerCapita=0.2 ImmigrationCapP`  
   P0 prints the synced DT row ColonistTransportFoodKgPerColonist (GetConfigTableScalar): 50 -> 25; the pin literal is updated to 25 (brief 3.5)
3. `-` **A**: `=== COLONIST TRANSPORT: 20 colonists housed, +1000 kg Food ===`  
   A: D129's log line prints FoodKg = ColonistTransportFoodKgPerColonist x 20 through %.0f: 20 x 25 = 500 (was 20 x 50 = 1000)
4. `+` **A**: `=== COLONIST TRANSPORT: 20 colonists housed, +500 kg Food ===`  
   A: D129's log line prints FoodKg = ColonistTransportFoodKgPerColonist x 20 through %.0f: 20 x 25 = 500 (was 20 x 50 = 1000)
5. `-` **A**: `ARRV ok: a 20 beds free of 20 needed: the transport lands - population 253 -> 273, +1000 kg Food (20 x 50), all housed on rated floors, none Mars-born`  
   (a): +500 kg Food (20 x 25); the check literal 1000 -> 500 and the text "(20 x 50)" -> "(20 x 25)" (brief 3.5)
6. `-` **A**: `ARRV ok: f the landing is one alert, not one per colonist: 1 alert(s), "A COLONIST TRANSPORT HAS LANDED — 20 colonists disembark into the vault, with 1000 kg of provisions."`  
   D129's alert text carries FoodKg through %.0f (500); the check now looks for "500 kg" (brief 3.5)
7. `+` **A**: `ARRV ok: a 20 beds free of 20 needed: the transport lands - population 253 -> 273, +500 kg Food (20 x 25), all housed on rated floors, none Mars-born`  
   (a): +500 kg Food (20 x 25); the check literal 1000 -> 500 and the text "(20 x 50)" -> "(20 x 25)" (brief 3.5)
8. `+` **A**: `ARRV ok: f the landing is one alert, not one per colonist: 1 alert(s), "A COLONIST TRANSPORT HAS LANDED — 20 colonists disembark into the vault, with 500 kg of provisions."`  
   D129's alert text carries FoodKg through %.0f (500); the check now looks for "500 kg" (brief 3.5)
9. `-` **A**: `=== COLONIST TRANSPORT: 20 colonists housed, +1000 kg Food ===`  
   A: D129's log line prints FoodKg = ColonistTransportFoodKgPerColonist x 20 through %.0f: 20 x 25 = 500 (was 20 x 50 = 1000)
10. `+` **A**: `=== COLONIST TRANSPORT: 20 colonists housed, +500 kg Food ===`  
   A: D129's log line prints FoodKg = ColonistTransportFoodKgPerColonist x 20 through %.0f: 20 x 25 = 500 (was 20 x 50 = 1000)
11. `-` **A**: `=== COLONIST TRANSPORT: 20 colonists housed, +1000 kg Food ===`  
   A: D129's log line prints FoodKg = ColonistTransportFoodKgPerColonist x 20 through %.0f: 20 x 25 = 500 (was 20 x 50 = 1000)
12. `+` **A**: `=== COLONIST TRANSPORT: 20 colonists housed, +500 kg Food ===`  
   A: D129's log line prints FoodKg = ColonistTransportFoodKgPerColonist x 20 through %.0f: 20 x 25 = 500 (was 20 x 50 = 1000)
13. `-` **A**: `=== BIG ARRIVAL (Sol 57): 100 settlers, +5000 kg Food (smoothed Hope 100.0, streak 21.1 sols, population 426 -> 526); the CEO will come with the next wave ===`  
   A: StepBigArrival's line prints FoodKg = ColonistTransportFoodKgPerColonist x 100 housed: 100 x 25 = 2500 (was 5000)
14. `+` **A**: `=== BIG ARRIVAL (Sol 57): 100 settlers, +2500 kg Food (smoothed Hope 100.0, streak 21.1 sols, population 426 -> 526); the CEO will come with the next wave ===`  
   A: StepBigArrival's line prints FoodKg = ColonistTransportFoodKgPerColonist x 100 housed: 100 x 25 = 2500 (was 5000)
15. `-` **A**: `ARRV ok: d with their food: 104986.794 kg (expect 100000 - 13.206 eaten by the 426 already here + 5000 for the hundred = 104986.794)`  
   (d): 100000 - 13.206 eaten + 2500 for the hundred = 102486.794 (was + 5000 = 104986.794); literal and text updated (brief 3.5)
16. `+` **A**: `ARRV ok: d with their food: 102486.794 kg (expect 100000 - 13.206 eaten by the 426 already here + 2500 for the hundred = 102486.794)`  
   (d): 100000 - 13.206 eaten + 2500 for the hundred = 102486.794 (was + 5000 = 104986.794); literal and text updated (brief 3.5)
17. `+` **D**: `=== EARTH DEMAND PENDING (tension 45) ===`  
   the drift (0.6 a sol from 0) crosses EarthTensionDemandThreshold 45 between the sol-75 and sol-76 autosaves (new line, tension 45); the 40-sol window's check counts "THE BIG ARRIVAL" only and holds
18. `-` **D**: `=== EARTH DEMAND PENDING (tension 60) ===`  
   the old crossing of 60 between the sol-100 and sol-101 autosaves, gone (the demand has been pending since sol 75); the 40-sol window's check counts "THE BIG ARRIVAL" only and holds
19. `-` **A**: `=== BIG ARRIVAL (Sol 117): 100 settlers, +5000 kg Food (smoothed Hope 100.0, streak 20.0 sols, population 526 -> 626); the CEO will come with the next wave ===`  
   A: StepBigArrival's line prints FoodKg = ColonistTransportFoodKgPerColonist x 100 housed: 100 x 25 = 2500 (was 5000)
20. `+` **A**: `=== BIG ARRIVAL (Sol 117): 100 settlers, +2500 kg Food (smoothed Hope 100.0, streak 20.0 sols, population 526 -> 626); the CEO will come with the next wave ===`  
   A: StepBigArrival's line prints FoodKg = ColonistTransportFoodKgPerColonist x 100 housed: 100 x 25 = 2500 (was 5000)
21. `-` **A**: `=== BIG ARRIVAL (Sol 117): 100 settlers, +5000 kg Food (smoothed Hope 100.0, streak 20.0 sols, population 526 -> 626); the CEO will come with the next wave ===`  
   A: StepBigArrival's line prints FoodKg = ColonistTransportFoodKgPerColonist x 100 housed: 100 x 25 = 2500 (was 5000)
22. `+` **A**: `=== BIG ARRIVAL (Sol 117): 100 settlers, +2500 kg Food (smoothed Hope 100.0, streak 20.0 sols, population 526 -> 626); the CEO will come with the next wave ===`  
   A: StepBigArrival's line prints FoodKg = ColonistTransportFoodKgPerColonist x 100 housed: 100 x 25 = 2500 (was 5000)
23. `-` **A**: `ARRV ok: g0 food: the larder must hold 4920 kg (10 sols of 1600 people's draw less the wave's own 5000 kg); 1 kg under is short, 1 kg over is enough (runway 10.001 sols)`  
   g0 derives from the getter: 10 sols x 1600 x 0.62 kg = 9920 less the wave's own 2500 = 7420 (was less 5000 = 4920); runway unchanged
24. `+` **A**: `ARRV ok: g0 food: the larder must hold 7420 kg (10 sols of 1600 people's draw less the wave's own 2500 kg); 1 kg under is short, 1 kg over is enough (runway 10.001 sols)`  
   g0 derives from the getter: 10 sols x 1600 x 0.62 kg = 9920 less the wave's own 2500 = 7420 (was less 5000 = 4920); runway unchanged
25. `-` **A**: `=== BIG ARRIVAL (Sol 172): 100 settlers, +5000 kg Food (smoothed Hope 100.0, streak 24.1 sols, population 1500 -> 1600); the CEO will come with the next wave ===`  
   A: StepBigArrival's line prints FoodKg = ColonistTransportFoodKgPerColonist x 100 housed: 100 x 25 = 2500 (was 5000)
26. `+` **A**: `=== BIG ARRIVAL (Sol 172): 100 settlers, +2500 kg Food (smoothed Hope 100.0, streak 24.1 sols, population 1500 -> 1600); the CEO will come with the next wave ===`  
   A: StepBigArrival's line prints FoodKg = ColonistTransportFoodKgPerColonist x 100 housed: 100 x 25 = 2500 (was 5000)
27. `-` **A**: `ARRV ok: g2 a sol with the larder at 1224 kg of the 5540 it needs: the wave holds (population 1600)`  
   g2 derives from the getter: FoodNeedF = 10 x 1700 x 0.62 - 2500 = 8040 (was 5540); the larder 0.4 x 8040 = 3216 less a sol's draw of 992 = 2224 (was 2216 - 992 = 1224)
28. `+` **A**: `ARRV ok: g2 a sol with the larder at 2224 kg of the 8040 it needs: the wave holds (population 1600)`  
   g2 derives from the getter: FoodNeedF = 10 x 1700 x 0.62 - 2500 = 8040 (was 5540); the larder 0.4 x 8040 = 3216 less a sol's draw of 992 = 2224 (was 2216 - 992 = 1224)
29. `-` **A**: `=== BIG ARRIVAL (Sol 194): 100 settlers, +5000 kg Food (smoothed Hope 98.7, streak 22.1 sols, population 1600 -> 1700); the CEO will come with the next wave ===`  
   A: StepBigArrival's line prints FoodKg = ColonistTransportFoodKgPerColonist x 100 housed: 100 x 25 = 2500 (was 5000); and its smoothed Hope 98.7 -> 99.0, a knock-on of g2 (below): g2 sets the larder to 0.4 x FoodNeedF, and FoodNeedF = 10 x 1700 x 0.62 less the wave's own food grew by 2500 (5540 -> 8040), so the larder held 1000 kg more through g2's held sol (2224 against 1224 kg at 992 kg a sol drawn: food runway 2.24 against 1.23 sols, FoodPenalty = 10 x (10 - runway) / 10 one point smaller), and smoothed Hope rose about 0.3 more before this wave landed
30. `+` **A**: `=== BIG ARRIVAL (Sol 194): 100 settlers, +2500 kg Food (smoothed Hope 99.0, streak 22.1 sols, population 1600 -> 1700); the CEO will come with the next wave ===`  
   A: StepBigArrival's line prints FoodKg = ColonistTransportFoodKgPerColonist x 100 housed: 100 x 25 = 2500 (was 5000); and its smoothed Hope 98.7 -> 99.0, a knock-on of g2 (below): g2 sets the larder to 0.4 x FoodNeedF, and FoodNeedF = 10 x 1700 x 0.62 less the wave's own food grew by 2500 (5540 -> 8040), so the larder held 1000 kg more through g2's held sol (2224 against 1224 kg at 992 kg a sol drawn: food runway 2.24 against 1.23 sols, FoodPenalty = 10 x (10 - runway) / 10 one point smaller), and smoothed Hope rose about 0.3 more before this wave landed
31. `-` **A**: `ARRV ok: h0 the wave's food leg counts the 5000 kg it lands with beside the 1000 kg stored (6000), against 186.00 kg a sol drawn at 300 people`  
   h0 derives from the getter: the wave lands 100 x 25 = 2500 kg beside the 1000 stored = 3500 (was 5000, 6000)
32. `+` **A**: `ARRV ok: h0 the wave's food leg counts the 2500 kg it lands with beside the 1000 kg stored (3500), against 186.00 kg a sol drawn at 300 people`  
   h0 derives from the getter: the wave lands 100 x 25 = 2500 kg beside the 1000 stored = 3500 (was 5000, 6000)
33. `-` **A**: `=== BIG ARRIVAL (Sol 26): 100 settlers, +5000 kg Food (smoothed Hope 100.0, streak 25.7 sols, population 200 -> 300); the CEO will come with the next wave ===`  
   A: StepBigArrival's line prints FoodKg = ColonistTransportFoodKgPerColonist x 100 housed: 100 x 25 = 2500 (was 5000)
34. `+` **A**: `=== BIG ARRIVAL (Sol 26): 100 settlers, +2500 kg Food (smoothed Hope 100.0, streak 25.7 sols, population 200 -> 300); the CEO will come with the next wave ===`  
   A: StepBigArrival's line prints FoodKg = ColonistTransportFoodKgPerColonist x 100 housed: 100 x 25 = 2500 (was 5000)

## credit.log: 585 -> 585 sim lines, 4 differing

1. `-` **A**: `=== COLONIST TRANSPORT: 20 colonists housed, +1000 kg Food ===`  
   A: D129's log line prints FoodKg = ColonistTransportFoodKgPerColonist x 20 through %.0f: 20 x 25 = 500 (was 20 x 50 = 1000)
2. `+` **A**: `=== COLONIST TRANSPORT: 20 colonists housed, +500 kg Food ===`  
   A: D129's log line prints FoodKg = ColonistTransportFoodKgPerColonist x 20 through %.0f: 20 x 25 = 500 (was 20 x 50 = 1000)
3. `-` **A**: `CREDIT ok: CR1 with 30 beds free a Crew Pod lands 4 colonists and 200 kg of Food, a Colonist Transport 20 and 1,000 kg, with HEAD's alerts`  
   CR1: the transport lands 500 kg (== 500.0, alert "with 500 kg of provisions."); the text "20 and 1,000 kg" -> "20 and 500 kg" (brief 3.6)
4. `+` **A**: `CREDIT ok: CR1 with 30 beds free a Crew Pod lands 4 colonists and 200 kg of Food, a Colonist Transport 20 and 500 kg, with HEAD's alerts`  
   CR1: the transport lands 500 kg (== 500.0, alert "with 500 kg of provisions."); the text "20 and 1,000 kg" -> "20 and 500 kg" (brief 3.6)

## currency.log: 1091 -> 1096 sim lines, 81 differing

1. `-` **D**: `Colony saved: slot 'auto' (N bytes, sol 2, 1 buildings, 7 robots)`  
   CU1 era band, first twin: alignment only: this text is unchanged (it is printed once as - and once as + because the moved demand line reorders difflib's default (autojunk) alignment; checks/ctxdiff.py with autojunk off shows the line unchanged in place)
2. `-` **D**: `Colony saved: slot 'auto' (N bytes, sol 3, 1 buildings, 7 robots)`  
   CU1 era band, first twin: alignment only: this text is unchanged (it is printed once as - and once as + because the moved demand line reorders difflib's default (autojunk) alignment; checks/ctxdiff.py with autojunk off shows the line unchanged in place)
3. `-` **D**: `Colony saved: slot 'auto' (N bytes, sol 4, 1 buildings, 7 robots)`  
   CU1 era band, first twin: alignment only: this text is unchanged (it is printed once as - and once as + because the moved demand line reorders difflib's default (autojunk) alignment; checks/ctxdiff.py with autojunk off shows the line unchanged in place)
4. `-` **D**: `=== EARTH DEMAND PENDING (tension 65) ===`  
   CU1 era band, first twin: HEAD's demand at tension 65 when the script raised tension for the Comply (sol 4), gone: the demand is already pending since the Defy
5. `+` **D**: `=== EARTH DEMAND PENDING (tension 53) ===`  
   CU1 era band, first twin: the Defy from 65 leaves 53, at or over 45, so the demand re-raises at once (straight after "SOLIDARITY: DEFIED - axis 20, tension 53")
6. `+` **D**: `Colony saved: slot 'auto' (N bytes, sol 2, 1 buildings, 7 robots)`  
   CU1 era band, first twin: alignment only: this text is unchanged (it is printed once as - and once as + because the moved demand line reorders difflib's default (autojunk) alignment; checks/ctxdiff.py with autojunk off shows the line unchanged in place)
7. `+` **D**: `Colony saved: slot 'auto' (N bytes, sol 3, 1 buildings, 7 robots)`  
   CU1 era band, first twin: alignment only: this text is unchanged (it is printed once as - and once as + because the moved demand line reorders difflib's default (autojunk) alignment; checks/ctxdiff.py with autojunk off shows the line unchanged in place)
8. `+` **D**: `Colony saved: slot 'auto' (N bytes, sol 4, 1 buildings, 7 robots)`  
   CU1 era band, first twin: alignment only: this text is unchanged (it is printed once as - and once as + because the moved demand line reorders difflib's default (autojunk) alignment; checks/ctxdiff.py with autojunk off shows the line unchanged in place)
9. `-` **D**: `Colony saved: slot 'auto' (N bytes, sol 2, 1 buildings, 7 robots)`  
   CU1 era band, second twin: alignment only: this text is unchanged (it is printed once as - and once as + because the moved demand line reorders difflib's default (autojunk) alignment; checks/ctxdiff.py with autojunk off shows the line unchanged in place)
10. `-` **D**: `Colony saved: slot 'auto' (N bytes, sol 3, 1 buildings, 7 robots)`  
   CU1 era band, second twin: alignment only: this text is unchanged (it is printed once as - and once as + because the moved demand line reorders difflib's default (autojunk) alignment; checks/ctxdiff.py with autojunk off shows the line unchanged in place)
11. `-` **D**: `Colony saved: slot 'auto' (N bytes, sol 4, 1 buildings, 7 robots)`  
   CU1 era band, second twin: alignment only: this text is unchanged (it is printed once as - and once as + because the moved demand line reorders difflib's default (autojunk) alignment; checks/ctxdiff.py with autojunk off shows the line unchanged in place)
12. `-` **D**: `=== EARTH DEMAND PENDING (tension 65) ===`  
   CU1 era band, second twin: HEAD's demand at tension 65 when the script raised tension for the Comply (sol 4), gone: the demand is already pending since the Defy
13. `+` **D**: `=== EARTH DEMAND PENDING (tension 53) ===`  
   CU1 era band, second twin: the Defy from 65 leaves 53, at or over 45, so the demand re-raises at once (straight after "SOLIDARITY: DEFIED - axis 20, tension 53")
14. `+` **D**: `Colony saved: slot 'auto' (N bytes, sol 2, 1 buildings, 7 robots)`  
   CU1 era band, second twin: alignment only: this text is unchanged (it is printed once as - and once as + because the moved demand line reorders difflib's default (autojunk) alignment; checks/ctxdiff.py with autojunk off shows the line unchanged in place)
15. `+` **D**: `Colony saved: slot 'auto' (N bytes, sol 3, 1 buildings, 7 robots)`  
   CU1 era band, second twin: alignment only: this text is unchanged (it is printed once as - and once as + because the moved demand line reorders difflib's default (autojunk) alignment; checks/ctxdiff.py with autojunk off shows the line unchanged in place)
16. `+` **D**: `Colony saved: slot 'auto' (N bytes, sol 4, 1 buildings, 7 robots)`  
   CU1 era band, second twin: alignment only: this text is unchanged (it is printed once as - and once as + because the moved demand line reorders difflib's default (autojunk) alignment; checks/ctxdiff.py with autojunk off shows the line unchanged in place)
17. `-` **D**: `Colony saved: slot 'auto' (N bytes, sol 2, 1 buildings, 7 robots)`  
   CU1 agent band, first twin: alignment only: this text is unchanged (it is printed once as - and once as + because the moved demand line reorders difflib's default (autojunk) alignment; checks/ctxdiff.py with autojunk off shows the line unchanged in place)
18. `-` **D**: `Colony saved: slot 'auto' (N bytes, sol 3, 1 buildings, 7 robots)`  
   CU1 agent band, first twin: alignment only: this text is unchanged (it is printed once as - and once as + because the moved demand line reorders difflib's default (autojunk) alignment; checks/ctxdiff.py with autojunk off shows the line unchanged in place)
19. `-` **D**: `Colony saved: slot 'auto' (N bytes, sol 4, 1 buildings, 7 robots)`  
   CU1 agent band, first twin: alignment only: this text is unchanged (it is printed once as - and once as + because the moved demand line reorders difflib's default (autojunk) alignment; checks/ctxdiff.py with autojunk off shows the line unchanged in place)
20. `-` **D**: `=== EARTH DEMAND PENDING (tension 65) ===`  
   CU1 agent band, first twin: HEAD's demand at tension 65 when the script raised tension for the Comply (sol 4), gone: the demand is already pending since the Defy
21. `+` **D**: `=== EARTH DEMAND PENDING (tension 53) ===`  
   CU1 agent band, first twin: the Defy from 65 leaves 53, at or over 45, so the demand re-raises at once (straight after "SOLIDARITY: DEFIED - axis 20, tension 53")
22. `+` **D**: `Colony saved: slot 'auto' (N bytes, sol 2, 1 buildings, 7 robots)`  
   CU1 agent band, first twin: alignment only: this text is unchanged (it is printed once as - and once as + because the moved demand line reorders difflib's default (autojunk) alignment; checks/ctxdiff.py with autojunk off shows the line unchanged in place)
23. `+` **D**: `Colony saved: slot 'auto' (N bytes, sol 3, 1 buildings, 7 robots)`  
   CU1 agent band, first twin: alignment only: this text is unchanged (it is printed once as - and once as + because the moved demand line reorders difflib's default (autojunk) alignment; checks/ctxdiff.py with autojunk off shows the line unchanged in place)
24. `+` **D**: `Colony saved: slot 'auto' (N bytes, sol 4, 1 buildings, 7 robots)`  
   CU1 agent band, first twin: alignment only: this text is unchanged (it is printed once as - and once as + because the moved demand line reorders difflib's default (autojunk) alignment; checks/ctxdiff.py with autojunk off shows the line unchanged in place)
25. `-` **D**: `Colony saved: slot 'auto' (N bytes, sol 2, 1 buildings, 7 robots)`  
   CU1 agent band, second twin: alignment only: this text is unchanged (it is printed once as - and once as + because the moved demand line reorders difflib's default (autojunk) alignment; checks/ctxdiff.py with autojunk off shows the line unchanged in place)
26. `-` **D**: `Colony saved: slot 'auto' (N bytes, sol 3, 1 buildings, 7 robots)`  
   CU1 agent band, second twin: alignment only: this text is unchanged (it is printed once as - and once as + because the moved demand line reorders difflib's default (autojunk) alignment; checks/ctxdiff.py with autojunk off shows the line unchanged in place)
27. `-` **D**: `Colony saved: slot 'auto' (N bytes, sol 4, 1 buildings, 7 robots)`  
   CU1 agent band, second twin: alignment only: this text is unchanged (it is printed once as - and once as + because the moved demand line reorders difflib's default (autojunk) alignment; checks/ctxdiff.py with autojunk off shows the line unchanged in place)
28. `-` **D**: `=== EARTH DEMAND PENDING (tension 65) ===`  
   CU1 agent band, second twin: HEAD's demand at tension 65 when the script raised tension for the Comply (sol 4), gone: the demand is already pending since the Defy
29. `+` **D**: `=== EARTH DEMAND PENDING (tension 53) ===`  
   CU1 agent band, second twin: the Defy from 65 leaves 53, at or over 45, so the demand re-raises at once (straight after "SOLIDARITY: DEFIED - axis 20, tension 53")
30. `+` **D**: `Colony saved: slot 'auto' (N bytes, sol 2, 1 buildings, 7 robots)`  
   CU1 agent band, second twin: alignment only: this text is unchanged (it is printed once as - and once as + because the moved demand line reorders difflib's default (autojunk) alignment; checks/ctxdiff.py with autojunk off shows the line unchanged in place)
31. `+` **D**: `Colony saved: slot 'auto' (N bytes, sol 3, 1 buildings, 7 robots)`  
   CU1 agent band, second twin: alignment only: this text is unchanged (it is printed once as - and once as + because the moved demand line reorders difflib's default (autojunk) alignment; checks/ctxdiff.py with autojunk off shows the line unchanged in place)
32. `+` **D**: `Colony saved: slot 'auto' (N bytes, sol 4, 1 buildings, 7 robots)`  
   CU1 agent band, second twin: alignment only: this text is unchanged (it is printed once as - and once as + because the moved demand line reorders difflib's default (autojunk) alignment; checks/ctxdiff.py with autojunk off shows the line unchanged in place)
33. `-` **D**: `=== EARTH DEMAND PENDING (tension 61) ===`  
   CU3 the twins' start: HEAD's demand at the fixture tension 61 (61.1)
34. `+` **D**: `=== EARTH DEMAND PENDING (tension 46) ===`  
   CU3 the twins' start: the fixture moved 61.1 -> 46.1 (the line 45 + 1.1, as 61.1 was at 60; brief 1.4 (8), 3.7), so the start demand prints tension 46
35. `-` **D**: `CURRENCY ok: CU3 the twins' start: a demand pending at tension 61.1, identity 0, saved`  
   CU3 the twins' start check text: "tension 61.1" (HEAD)
36. `+` **D**: `CURRENCY ok: CU3 the twins' start: a demand pending at tension 46.1, identity 0, saved`  
   CU3 the twins' start check text: "tension 46.1" (the fixture re-anchor)
37. `-` **D**: `=== SOLIDARITY: DEFIED - axis 20, tension 49, requisition x0.73 ===`  
   CU3 plain Defy twin: HEAD 61.1 + drift - 12 = 49, requisition x0.73
38. `+` **D**: `=== SOLIDARITY: DEFIED - axis 20, tension 34, requisition x0.78 ===`  
   CU3 plain Defy twin from 46.1: tension 34 (34.16), requisition x0.78 (0.3 x 15 / 100 = 0.045 higher with tension 15 lower)
39. `-` **D**: `=== SOLIDARITY: DEFIED - axis 20, tension 49, requisition x0.73 ===`  
   CU3 DefyIssue twin: HEAD's Defy half at 49, x0.73
40. `-` **D**: `=== CURRENCY ISSUED: the mark (identity +20, tension 59, requisition x0.60) ===`  
   CU3 DefyIssue twin: HEAD's Issue at 59, x0.60
41. `-` **D**: `CURRENCY ok: CU3 DefyIssue against a plain Defy: the Defy's HEAD effects in both (identity +20, relation 45, the same morale swing), tension 59.160 against 49.160 (+10), the multiplier 0.6025 against 0.7325 (exactly`  
   CU3 DefyIssue against a plain Defy (HEAD): 59.160 against 49.160, 0.6025 against 0.7325
42. `-` **D**: `Currency: the market issues the mark at the end of sol 0; orders clearing at the end of sol 2 or later count in marks (1 cr a mark)`  
   CU3: alignment only: this text is unchanged (it is printed once as - and once as + because the moved demand line reorders difflib's default (autojunk) alignment; checks/ctxdiff.py with autojunk off shows the line unchanged in place)
43. `-` **D**: `Bloc: Zarya joins the mark (stance +20, identity +20)`  
   CU3: alignment only: this text is unchanged (it is printed once as - and once as + because the moved demand line reorders difflib's default (autojunk) alignment; checks/ctxdiff.py with autojunk off shows the line unchanged in place)
44. `-` **D**: `CURRENCY ok: CU3 the issue roll closes the act's sol 0: the market issues the mark there, opens it for sol 2, at 1000 mcr, with no step; Zarya joins from sol 2; the journal is applied and empty (identity +20000, ten`  
   CU3 the issue roll (HEAD): tension 59.730
45. `+` **D**: `=== SOLIDARITY: DEFIED - axis 20, tension 34, requisition x0.78 ===`  
   CU3 DefyIssue twin: the Defy half at 34, x0.78
46. `+` **D**: `=== CURRENCY ISSUED: the mark (identity +20, tension 44, requisition x0.65) ===`  
   CU3 DefyIssue twin: the Issue +10 leaves 44 (44.16, under 45 by 0.84: no re-raise, so each twin still hears one alert), x0.65
47. `+` **D**: `CURRENCY ok: CU3 DefyIssue against a plain Defy: the Defy's HEAD effects in both (identity +20, relation 45, the same morale swing), tension 44.160 against 34.160 (+10), the multiplier 0.6475 against 0.7775 (exactly`  
   CU3 DefyIssue against a plain Defy: 44.160 against 34.160 (+10), 0.6475 against 0.7775 (still exactly -0.13); self-derived from the state
48. `+` **D**: `Currency: the market issues the mark at the end of sol 0; orders clearing at the end of sol 2 or later count in marks (1 cr a mark)`  
   CU3: alignment only: this text is unchanged (it is printed once as - and once as + because the moved demand line reorders difflib's default (autojunk) alignment; checks/ctxdiff.py with autojunk off shows the line unchanged in place)
49. `+` **D**: `Bloc: Zarya joins the mark (stance +20, identity +20)`  
   CU3: alignment only: this text is unchanged (it is printed once as - and once as + because the moved demand line reorders difflib's default (autojunk) alignment; checks/ctxdiff.py with autojunk off shows the line unchanged in place)
50. `+` **D**: `CURRENCY ok: CU3 the issue roll closes the act's sol 0: the market issues the mark there, opens it for sol 2, at 1000 mcr, with no step; Zarya joins from sol 2; the journal is applied and empty (identity +20000, ten`  
   CU3 the issue roll: tension 44.730 (44.160 + 0.57 of drift; was 59.730); self-derived
51. `+` **D**: `=== EARTH DEMAND PENDING (tension 53) ===`  
   CU4: DefyIssue then a Defy from 63 leaves 53, at or over 45: the demand re-raises at once (after "DEFIED - axis 40, tension 53")
52. `+` **D**: `=== EARTH DEMAND PENDING (tension 55) ===`  
   CU7 (a Defy due after the sol-3 boundary): the Defy leaves 55 (DEFIED - axis 20, tension 55), at or over 45: the demand re-raises at once
53. `+` **D**: `=== EARTH DEMAND PENDING (tension 53) ===`  
   CU7 (+10 harness shift, first twin): the Defy from 65 leaves 53: the demand re-raises at once
54. `+` **D**: `=== EARTH DEMAND PENDING (tension 53) ===`  
   CU7 (+10 harness shift, second twin): the same, 53
55. `+` **D**: `=== EARTH DEMAND PENDING (tension 53) ===`  
   CU10 fixture: the Defy from 65 leaves 53: the demand re-raises at once before the Pacify (the save CU10 writes keeps its size, 17922 bytes; checks/sizes.py)
56. `+` **D**: `=== EARTH DEMAND PENDING (tension 48) ===`  
   CU12 plain Defy twin from 60.3: leaves 48 (48.3), at or over 45: the demand re-raises at once
57. `-` **D**: `=== EARTH DEMAND PENDING (tension 60) ===`  
   CU12 plain Defy twin: HEAD's re-raise when the drift brought it back to 60 between the sol-19 and sol-20 autosaves, gone
58. `-` **D**: `Colony saved: slot 'auto' (N bytes, sol 1, 1 buildings, 7 robots)`  
   CU12 DefyIssue twin: alignment only: this text is unchanged (it is printed once as - and once as + because the moved demand line reorders difflib's default (autojunk) alignment; checks/ctxdiff.py with autojunk off shows the line unchanged in place)
59. `-` **D**: `Currency: the market issues the mark at the end of sol 0; orders clearing at the end of sol 2 or later count in marks (1 cr a mark)`  
   CU12 DefyIssue twin: alignment only: this text is unchanged (it is printed once as - and once as + because the moved demand line reorders difflib's default (autojunk) alignment; checks/ctxdiff.py with autojunk off shows the line unchanged in place)
60. `-` **D**: `Bloc: Zarya joins the mark (stance +20, identity +20)`  
   CU12 DefyIssue twin: alignment only: this text is unchanged (it is printed once as - and once as + because the moved demand line reorders difflib's default (autojunk) alignment; checks/ctxdiff.py with autojunk off shows the line unchanged in place)
61. `-` **D**: `Colony saved: slot 'auto' (N bytes, sol 2, 1 buildings, 7 robots)`  
   CU12 DefyIssue twin: alignment only: this text is unchanged (it is printed once as - and once as + because the moved demand line reorders difflib's default (autojunk) alignment; checks/ctxdiff.py with autojunk off shows the line unchanged in place)
62. `-` **D**: `Colony saved: slot 'auto' (N bytes, sol 3, 1 buildings, 7 robots)`  
   CU12 DefyIssue twin: alignment only: this text is unchanged (it is printed once as - and once as + because the moved demand line reorders difflib's default (autojunk) alignment; checks/ctxdiff.py with autojunk off shows the line unchanged in place)
63. `-` **D**: `=== EARTH DEMAND PENDING (tension 60) ===`  
   CU12 DefyIssue twin: HEAD's re-raise when the drift crossed 60 between the sol-3 and sol-4 autosaves, gone
64. `+` **D**: `=== EARTH DEMAND PENDING (tension 58) ===`  
   CU12 DefyIssue twin: 48.3 + 10 = 58 (58.3), at or over 45: the demand re-raises at once (after "CURRENCY ISSUED ... tension 58")
65. `+` **D**: `Colony saved: slot 'auto' (N bytes, sol 1, 1 buildings, 7 robots)`  
   CU12 DefyIssue twin: alignment only: this text is unchanged (it is printed once as - and once as + because the moved demand line reorders difflib's default (autojunk) alignment; checks/ctxdiff.py with autojunk off shows the line unchanged in place)
66. `+` **D**: `Currency: the market issues the mark at the end of sol 0; orders clearing at the end of sol 2 or later count in marks (1 cr a mark)`  
   CU12 DefyIssue twin: alignment only: this text is unchanged (it is printed once as - and once as + because the moved demand line reorders difflib's default (autojunk) alignment; checks/ctxdiff.py with autojunk off shows the line unchanged in place)
67. `+` **D**: `Bloc: Zarya joins the mark (stance +20, identity +20)`  
   CU12 DefyIssue twin: alignment only: this text is unchanged (it is printed once as - and once as + because the moved demand line reorders difflib's default (autojunk) alignment; checks/ctxdiff.py with autojunk off shows the line unchanged in place)
68. `+` **D**: `Colony saved: slot 'auto' (N bytes, sol 2, 1 buildings, 7 robots)`  
   CU12 DefyIssue twin: alignment only: this text is unchanged (it is printed once as - and once as + because the moved demand line reorders difflib's default (autojunk) alignment; checks/ctxdiff.py with autojunk off shows the line unchanged in place)
69. `+` **D**: `Colony saved: slot 'auto' (N bytes, sol 3, 1 buildings, 7 robots)`  
   CU12 DefyIssue twin: alignment only: this text is unchanged (it is printed once as - and once as + because the moved demand line reorders difflib's default (autojunk) alignment; checks/ctxdiff.py with autojunk off shows the line unchanged in place)
70. `+` **D**: `=== EARTH DEMAND PENDING (tension 59) ===`  
   CU8 first run: the start save at tension 59.0 (under HEAD's 60, over 45) raises the demand on the first step after the load
71. `-` **D**: `=== EARTH DEMAND PENDING (tension 60) ===`  
   CU8 first run: HEAD's demand when the drift crossed 60 (after the solar flare, about sol 17.7), gone
72. `+` **D**: `=== EARTH DEMAND PENDING (tension 59) ===`  
   CU8 first run: the DefyIssue leaves 59 (49 + 10), at or over 45: re-raised at once (after "CURRENCY ISSUED ... tension 59")
73. `-` **D**: `=== EARTH DEMAND PENDING (tension 60) ===`  
   CU8 first run: HEAD's re-raise when the drift crossed 60 (between the sol-21 and sol-22 autosaves), gone
74. `+` **D**: `=== EARTH DEMAND PENDING (tension 51) ===`  
   CU8 first run: the Defy leaves 51, at or over 45: re-raised at once (after "DEFIED - axis 40, tension 51")
75. `-` **D**: `=== EARTH DEMAND PENDING (tension 78) ===`  
   CU8 first run: HEAD's re-raise at tension 78, gone (pending since the Defy); the embargo at 78 reads EarthPreemptiveThreshold and its line is unchanged; CU8's demand checks hold
76. `+` **D**: `=== EARTH DEMAND PENDING (tension 59) ===`  
   CU8 paired run: the start save at tension 59.0 (under HEAD's 60, over 45) raises the demand on the first step after the load
77. `-` **D**: `=== EARTH DEMAND PENDING (tension 60) ===`  
   CU8 paired run: HEAD's demand when the drift crossed 60 (after the solar flare, about sol 17.7), gone
78. `+` **D**: `=== EARTH DEMAND PENDING (tension 59) ===`  
   CU8 paired run: the DefyIssue leaves 59 (49 + 10), at or over 45: re-raised at once (after "CURRENCY ISSUED ... tension 59")
79. `-` **D**: `=== EARTH DEMAND PENDING (tension 60) ===`  
   CU8 paired run: HEAD's re-raise when the drift crossed 60 (between the sol-21 and sol-22 autosaves), gone
80. `+` **D**: `=== EARTH DEMAND PENDING (tension 51) ===`  
   CU8 paired run: the Defy leaves 51, at or over 45: re-raised at once (after "DEFIED - axis 40, tension 51")
81. `-` **D**: `=== EARTH DEMAND PENDING (tension 78) ===`  
   CU8 paired run: HEAD's re-raise at tension 78, gone (pending since the Defy); the embargo at 78 reads EarthPreemptiveThreshold and its line is unchanged; CU8's demand checks hold

## discovery.log: 123 -> 124 sim lines, 3 differing

1. `-` **B**: `DISC ok: DS5 second window: found=4 [CropStrain,RegolithCeramics,SubsurfaceBrine,MicrobialLife] next=None (0.000 of the way) struct +100 (expect 100 x ceramics found = 100) milestones=20.0 (expect 5 + the found rows`  
   DS5's second-window line: the Milestones line reads min(5 + 20, 15) = 15.0 (was 20.0, uncapped); its clause and text are restated with the cap (brief 3.5), not dropped
2. `+` **B**: `DISC ok: DS5 second window: found=4 [CropStrain,RegolithCeramics,SubsurfaceBrine,MicrobialLife] next=None (0.000 of the way) struct +100 (expect 100 x ceramics found = 100) milestones=15.0 (expect min(5 + the found `  
   DS5's second-window line: the Milestones line reads min(5 + 20, 15) = 15.0 (was 20.0, uncapped); its clause and text are restated with the cap (brief 3.5), not dropped
3. `+` **new check**: `DISC ok: DS5 cap (balance pass 1): over the window's 120 steps the Milestones line peaked at 15.0, never over HopeMilestonesMax 15.0 (DT_Config 15.0), while the uncapped sum (the vault 5 + the found rows' bonus) pea`  
   THE NEW CHECK (brief 5.2): over the 120 steps the line peaked at 15.0, never over HopeMilestonesMax 15.0 = DT_Config 15.0, while the uncapped sum peaked at 20.0; at the end 15.0 = min(20.0, 15.0)

## icelow.log: 594 -> 594 sim lines, 2 differing

1. `+` **D**: `=== EARTH DEMAND PENDING (tension 45) ===`  
   the drift crosses 45 between the sol-75 and sol-76 autosaves (new line); I8 counts "RUNNING LOW" only and holds
2. `-` **D**: `=== EARTH DEMAND PENDING (tension 60) ===`  
   the old crossing of 60 after the sol-100 autosave, gone (pending since sol 75)

## levers.log: 1572 -> 1579 sim lines, 11 differing

1. `+` **D**: `=== EARTH DEMAND PENDING (tension 55) ===`  
   LV2 (-solidarity replayed): the Defy leaves tension 55, at or over 45, so the demand re-raises at once (after "SOLIDARITY: DEFIED - axis 20, tension 55")
2. `-` **D**: `=== EARTH DEMAND PENDING (tension 60) ===`  
   LV2 (-solidarity replayed): HEAD's re-raise when the drift crossed 60 near sol 12, gone (pending since the Defy)
3. `+` **D**: `=== EARTH DEMAND PENDING (tension 55) ===`  
   LV2 (-solidarity replayed): the Comply leaves 55, at or over 45: the demand re-raises at once (after "COMPLIED - 1 route(s) closed, axis 0, tension 55")
4. `+` **D**: `=== EARTH DEMAND PENDING (tension 45) ===`  
   LV2 (-icelow and -arrivals' drift timeline): the drift crosses 45 between the sol-75 and sol-76 autosaves (tension 45)
5. `-` **D**: `=== EARTH DEMAND PENDING (tension 60) ===`  
   LV2 (drift timeline): HEAD's crossing of 60 after the sol-100 autosave, gone; LV2's embargo check (tension 75, sol 125) reads EarthPreemptiveThreshold and holds
6. `+` **D**: `=== EARTH DEMAND PENDING (tension 55) ===`  
   LV1 twin run 1 of 6 (era band pair, agent band pair, the no-covert-op pair): the Comply leaves 55, at or over 45, so the demand re-raises at once (after "COMPLIED - 1 route(s) closed, axis -20, tension 55"); both twins of each pair move alike, so the digests still match
7. `+` **D**: `=== EARTH DEMAND PENDING (tension 55) ===`  
   LV1 twin run 2 of 6 (era band pair, agent band pair, the no-covert-op pair): the Comply leaves 55, at or over 45, so the demand re-raises at once (after "COMPLIED - 1 route(s) closed, axis -20, tension 55"); both twins of each pair move alike, so the digests still match
8. `+` **D**: `=== EARTH DEMAND PENDING (tension 55) ===`  
   LV1 twin run 3 of 6 (era band pair, agent band pair, the no-covert-op pair): the Comply leaves 55, at or over 45, so the demand re-raises at once (after "COMPLIED - 1 route(s) closed, axis -20, tension 55"); both twins of each pair move alike, so the digests still match
9. `+` **D**: `=== EARTH DEMAND PENDING (tension 55) ===`  
   LV1 twin run 4 of 6 (era band pair, agent band pair, the no-covert-op pair): the Comply leaves 55, at or over 45, so the demand re-raises at once (after "COMPLIED - 1 route(s) closed, axis -20, tension 55"); both twins of each pair move alike, so the digests still match
10. `+` **D**: `=== EARTH DEMAND PENDING (tension 55) ===`  
   LV1 twin run 5 of 6 (era band pair, agent band pair, the no-covert-op pair): the Comply leaves 55, at or over 45, so the demand re-raises at once (after "COMPLIED - 1 route(s) closed, axis -20, tension 55"); both twins of each pair move alike, so the digests still match
11. `+` **D**: `=== EARTH DEMAND PENDING (tension 55) ===`  
   LV1 twin run 6 of 6 (era band pair, agent band pair, the no-covert-op pair): the Comply leaves 55, at or over 45, so the demand re-raises at once (after "COMPLIED - 1 route(s) closed, axis -20, tension 55"); both twins of each pair move alike, so the digests still match

## solidarity.log: 73 -> 74 sim lines, 7 differing

1. `-` **D**: `SOLID defy: axis=+20 relation=45 pending=0 hope 55.0->66.8 mult=0.72 (expect +20, 45, 0, up, <1)`  
   HEAD's print: pending=0 under the old line 60
2. `+` **D**: `=== EARTH DEMAND PENDING (tension 55) ===`  
   the re-raise at once at 55 (after the DEFIED line for the first, after the COMPLIED line for the second): 55 >= 45
3. `+` **D**: `SOLID defy: axis=+20 relation=45 pending=1 hope 55.0->66.8 mult=0.72 (expect +20, 45, 1 - tension 55 is at or over the demand line 45, so a new demand follows at once (balance pass 1), up, <1)`  
   pending=1: the Defy from 67 leaves 55, at or over 45, so StepEarth re-raises the demand within Answer's 0.1 sol; the expectation text is corrected (brief 3.5; print-only)
4. `-` **D**: `=== EARTH DEMAND PENDING (tension 60) ===`  
   HEAD's re-raise when the drift brought tension back over 60 near sol 12, gone: the demand has been pending since the Defy
5. `-` **D**: `SOLID comply: closed=1 relation=5 axis=+0 tension=55 (expect 1, 5, 0, 55 = 100 clamped - 45 relief, below re-demand)`  
   HEAD's print, "below re-demand"
6. `+` **D**: `=== EARTH DEMAND PENDING (tension 55) ===`  
   the re-raise at once at 55 (after the DEFIED line for the first, after the COMPLIED line for the second): 55 >= 45
7. `+` **D**: `SOLID comply: closed=1 relation=5 axis=+0 tension=55 (expect 1, 5, 0, 55 = 100 clamped - 45 relief; at or over the demand line 45, so a new demand follows (balance pass 1))`  
   the Comply leaves 55 (100 clamped - 45), at or over 45, so a new demand follows; the expectation text is corrected (brief 3.5; print-only)

## Counts by log and option

| Log | A | B | D | new check | alignment-only (in D) | total |
|---|---|---|---|---|---|---|
| arrivals.log | 32 | 0 | 2 | 0 | 0 | 34 |
| credit.log | 4 | 0 | 0 | 0 | 0 | 4 |
| currency.log | 0 | 0 | 81 | 0 | 38 | 81 |
| discovery.log | 0 | 2 | 0 | 1 | 0 | 3 |
| icelow.log | 0 | 0 | 2 | 0 | 0 | 2 |
| levers.log | 0 | 0 | 11 | 0 | 0 | 11 |
| solidarity.log | 0 | 0 | 7 | 0 | 0 | 7 |
| all | 36 | 2 | 103 | 1 | 38 | 142 |

Unassigned lines: 0.
