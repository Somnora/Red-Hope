# Playtest: the first hour and the crew phase — 2026-10-15

`RH.Playtest` (Source/RedHope/Private/RHPlaytest.cpp) drives the real game —
live robots in the agent band at 16x, real construction, real surveys — down
the first-hour guide's path using only player orders, and logs a timeline.
Run it with:

```
bash scripts/sim/rh_playtest.sh <sols> <label> rush|guide [full|lean]
```

Past the first crew it keeps playing the guide's last step (the crew-phase
extension, Finding 4): soil, seeds and comforts ride with the Crew Pod
(`full`; `lean` sends the pod alone), the vault is carved to nine cells and
zoned, and a `CREW` line per sol logs Hope and its parts, food runway, the
garden, water and support.

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

**Fixed (director's call: option 1, the Lander emergency dock).** With no
working Charging Pad, robots dock on the Lander's south side and charge at
`LanderDockRate_W` (RH_Config, 100 W) each, all at once; the pad stays the
real charger (500 W, one at a time). A Lander the player has switched off is
no dock. The alternatives weighed were a starting pad and a queue-jumping
pad order.

Same rush order, after the fix (`rush2`):

| sol | milestone |
|---|---|
| 0.63 | Forge built |
| 2.78 | Charging Pad built (the fleet crawled on the Lander dock until then) |
| 4.60 | quota 1 met |
| 8.38 | floor -1 livable |
| 11.38 | first crew landed |

No robot ever ran flat (stranded-max 0 every sol). Rushing now costs about
a sol against the guide order instead of the game. The guide-order run is
milestone-for-milestone identical before and after the fix (a pad always
stands, so the dock never comes into play).

Era mode (> 16x) already integrated charging as if a pad existed; the dock
does not change it, so the battery baselines are untouched.

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

## Finding 4 — the crew phase (sols 10–110)

**With soil, seeds and comforts on the crew ship** (`crew2`, to sol 50): the
four colonists never lack support; the garden is planted by sol 11.1;
Hope (smoothed) climbs from 53 to **100 FLOURISHING by sol 16 and stays
there**; a child is born at sol 32.8; the discovery chain runs all the way to
LIFE ON MARS by sol 35.7. Food never drops below 60 sols of draw.

**With the Crew Pod alone** — what the guide's "Bring the first crew" step
used to say (`lean1`, to sol 110):

| sol | |
|---|---|
| 10.55 | four colonists land with 200 kg of food; no soil, no seeds |
| 20–70 | Hope THRIVING → FLOURISHING the whole time; births at sols 33.9 and 53.9 |
| 45 → 60 | Ice_A runs dry; drinkable water 100% → 0%; Hope barely moves |
| 75.44 | food hits zero — the **first warning of any kind**: six "unsupported" alerts at once |
| 77.44 | all six recalled to orbit, both Mars-born children included; THE COLONY STANDS EMPTY |

The data ladder has two quotas (Q1, Q2), so the crew's ship is the last ship
the program sends. A lean cargo makes the colony unwinnable at sol 7.5 (the
launch), and nothing says so until sol 75.

**Shipped (presentation):** the guide's "Bring the first crew" now names the
Soil Pallet and Seed Vault and says why; "Keep them well" warns with live
numbers while food runs out with too little growing ("Food runs out in about
N sols - the garden grows X kg a sol against Y eaten...") and when the water
goes stale.

**Shipped (sim, alert channel only):** the GROW-LIGHTS DARK alert fired
**12,524 times in 40 sols** — a bank scraping empty at night flips the lights
every step, and each flip re-armed the alert. It now re-arms only after a
whole sol fully lit: 1 alert in the same run, with every crew number (pop,
births, food, power) identical, so garden yield is untouched.

**For the director:**

- **The supply line ends at sol ~10.** With two quotas, a colony that missed
  soil and seeds on its second ship cannot recover. Extend the ladder (Q3+),
  make soil obtainable on Mars (the dormant agri chain: compost, fertilizer),
  or keep a slow recurring resupply?
- **Hope gives no warning.** It sat at FLOURISHING (91) while food ran down
  and the water hit 0% drinkable; the potability penalty caps at 12 points.
  Should a short food runway or stale water weigh on Hope - and should the sim
  raise a "food running low" alert (today the first alert is at zero)?
- **Hope pins at 100 from sol 16** with a sensible layout and comforts - the
  crew phase has no tension, and the discovery chain reaches LIFE ON MARS
  ~25 sols after landing.
- **Gate-D wording:** "EVACUATED: Freya Farid returned to orbit" is said of a
  child born on Mars.

## Balance questions for the director

- Is quota 1 (met at sol 4.2 of 10) meant to have this much slack?
- Should the starting grid carry the Forge + water chain without nightly
  blackouts (more starting kits, a bigger bank, or lower draws)?
- Is a ~3.5-hour (1x) road to the first crew the intended first session?
