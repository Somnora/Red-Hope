# Balance pass 1, round 0: the playtest's predicted against actual

`RH_PLAYTEST_DISTRICTS=1 bash scripts/sim/rh_playtest.sh 110 bal_full guide full` (2026-11-04 02:34:31-02:36:55; 1751 lines, 0 sim errors), then
`compare.py Saved/baselines/ind_full_playtest.txt Saved/RHPlaytest/bal_full.txt` (output `work/r0/compare_full.txt`).

THE BAR: the Big Arrival by sol 110 (81.01), at least 3 Q3 rounds (8), nobody recalled (0), the outpost bar PASS (PASS, 6 ships), 0 sim errors (0): **PASS**.

| Figure | Today (`ind_full_playtest`) | Predicted (brief 8.2 / orchestrator) | Actual (`bal_full`) |
|---|---|---|---|
| Milestones line, max / at the end | 30 / 30 | 15 / 15 | **15 / 15** |
| Discoveries | 32.17, 36.46, 41.61, 48.49 | first three unchanged; fourth about 48.5 | **32.17, 36.46, 41.61, 48.49** (all four unchanged) |
| The first harvest / the first-born | 21.38 / 44.06 | about 44 | **21.38 / 44.06** |
| CREW lines identical to today | - | through sol 41 (B moves nothing before 41.61) | **through sol 41**; the first differing CREW line is sol 42 (milestones 15, uncapped 16) |
| Hope sol 25..arrival | 78..100 | min about 78, max 93-95 | **78..94** |
| Hope sol 42..arrival | 93..100 | 85-95 (orchestrator: 85-94) | **88..94** |
| Hope after the arrival | 89..98, ends 96 FLOURISHING | 79-88 (orchestrator); static re-score 74-89, ends about 81 THRIVING | **75..88, ends 80 THRIVING** (FLOURISHING sols 82-83 only) |
| Sols under 75 from sol 25 | 0 | about 3 (sols 93-95) | **0** (the CREW line reads 75 on sols 94-95, its low) |
| The Big Arrival | 81.01 | about 76-81 | **81.01** |
| Earth's demand | 100.25 | 75.25 | **75.25** |
| ROUND lines (Q1, Q2, Q3s) | 9 (7 Q3) | about 9; at least 5 (3 Q3) | **10 (8 Q3)**: Q3 r5 landed 86.58 (was 87.00), r6 97.00 (was 99.00), and a Q3 r7 opened 97, met 103, landed 109.00 |
| Population at 110 | 122 | about 115-120 | **119** |
| Immigrated / born | 9 / 9 | about 6-9 / about 8 | **7 / 8** |
| Food low point | 57 sols | about 52-57 | **52 sols** |
| Recalled / outpost bar | 0 / PASS | 0 / PASS | **0 / PASS** |
| Sim errors | 0 | 0 | **0** |
