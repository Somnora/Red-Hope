# First-hour playtest — 2026-10-15

`RH.Playtest` (Source/RedHope/Private/RHPlaytest.cpp) drives the real game —
live robots in the agent band at 16x, real construction, real surveys — down
the first-hour guide's path using only player orders, and logs a timeline.
Run it with:

```
bash scripts/sim/rh_playtest.sh 30 <label> rush|guide
```

About two minutes per 30 sols (windowless, fixed 0.1 s step). Output:
`Saved/RHPlaytest/<label>.txt`. Both runs below are deterministic (the 2-sol
shakedown and the 30-sol rush run printed identical sol-1/2 lines).

## Finding 1 — a permanent softlock (rush order)

Every opening order at once — the six solar kits, both battery kits, the
Charging Pad, the dig, the Forge, the survey — a plausible first move:

| sol | state |
|---|---|
| 0.63 | Forge built (the fabricator takes the **nearest** site, not the oldest order) |
| 0.69–0.95 | six solar arrays built |
| 1 | 3 robots out of charge; power short 9%, machines shed 28% |
| 3 | structural stuck at 275 kg; nothing more ever builds |
| 13 | **all 7 robots out of charge** |
| 30 | unchanged: Charging Pad, both Battery Banks, the first Pylon never built |

Robots recharge **only** at a finished Charging Pad (`FindNearestChargePad`
looks for nothing else). The fabricator spent its battery on the Forge (400 s
of work) and the arrays, ran flat before the pad, and with no pad nothing can
ever build one. The colony cannot recover — there is no player action that
fixes it.

**Guide mitigation (shipped, presentation only):** step 3 "Give the robots a
charger" now completes only when the pad is *finished*, and tells the player
to wait for it before ordering anything big.

**The rule itself is the director's call** (it changes the sim and its
baselines). Options:

1. **The Lander doubles as a slow emergency dock** — robots that find no pad
   limp home and trickle-charge there. Keeps the pad as the real charger,
   always leaves a way back. *Recommended.*
2. The colony starts with a Charging Pad already standing.
3. A queued Charging Pad jumps the build queue.

## Finding 2 — the guide's path works, and the first quota is generous

Guide order (three arrays, a bank and the pad first; the dig, the Forge and
the rest only once the pad stands):

| sol | milestone |
|---|---|
| 0.62 | Charging Pad running |
| 0.68 | ice found (survey) |
| 1.37 | Forge running |
| 1.82 | the grid reaches the ice (two pylons) |
| 2.05 / 2.69 / 2.90 | Ice Drill / Water Plant / Electrolyzer running |
| 3.02 | structural runs out (the expected squeeze; recovered by sol 4) |
| **4.20** | **quota 1 met** (deadline sol 10) — ship 1 launched |
| 5.06 | Borer running |
| 5.60 / 6.14 | shaft reaches -1 / floor -1 carved (4 cells) |
| 6.34 | Air Filter running on -1 |
| **7.18** | **floor -1 livable** (Phase 1 exit) |
| 7.55 | quota 2 met; Crew Pod launched |
| **10.55** | **first crew landed** |

No robot ever ran flat; no order was rejected.

For scale: a sol is 20 minutes at 1x. Quota 1 at sol 4.2 is ~84 minutes at
1x, ~28 at 3x, ~10 at 8x. The crew lands ~3.5 hours into a 1x game.

## Finding 3 — the colony lives browned out

Guide run, per sol: power short of demand 15–54% of the time, machines shed
20–78% of the time, and the battery bank hits 0% every night from sol 3 on —
even after the first ship's four extra arrays. The Borer (500 W) on top of
the Forge (800 W) and Electrolyzer (600 W) is the worst stretch (sols 6–7:
78% shed). Progress still comes, because shedding drops the lowest-priority
loads first, but the player sees a permanently starved grid.

## Smaller observations

- Robots idle: ~4 of 7 on average through the whole guide run. The fleet is
  under-used once the opening builds are done.
- Water piles up: 3,055 kg by sol 10 against a 200 kg quota line.
- Food is 0 until the Crew Pod's 200 kg lands; the garden is not on the
  guide's path yet, and four colonists draw ~2.5 kg a sol.

## Balance questions for the director

- Is quota 1 (met at sol 4.2 of 10) meant to have this much slack?
- Should the starting grid carry the Forge + water chain without nightly
  blackouts (more starting kits, a bigger bank, or lower draws)?
- Is a ~3.5-hour (1x) road to the first crew the intended first session?
