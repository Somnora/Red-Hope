# Crew-phase decision brief — 2026-10-17

The 10-15 playtest (`docs/qa/2026-10-15/playtest.md`) showed that the game
works up to the first crew landing (~sol 10.5) and then stops being a game.
There are four separate problems. Each has data already; the balance sweeps
were run with the playtest's hooks and the shipped values as the baseline.
This brief asks five questions (D1-D5) and one minor one (D6), with a
recommendation for each. **Reply with the letters**, e.g. "D1a D2a D3a D4a
D5a", or with any change. I then build them as separate, tested commits and
re-run the playtest to show the result.

Every number below is a placeholder for the playtest to confirm. Nothing
here changes the Gate-D wording rule: new alert text ships as placeholder
copy.

---

## D1. The supply line ends at sol ~10

**What happens.** The ladder has two quotas, so the crew's ship is the last
ship. A crew sent without soil and seeds cannot recover. In the lean run, the
food ran out at sol 75 and everyone, including two Mars-born children, was
recalled at sol 77. The game had effectively been unwinnable since sol 7.5,
when the last ship launched.

**Data.** The dormant Q3 (Struct 900, Water 300, O2 200, H2 60) rescued the
lean colony in sweep A: soil and seeds arrived on ship 3 at sol 13.6, there
were 0 recalls, 4 births, and the population was 8 by sol 94. But Q3 was met
*the moment it opened*. By sol ~10 the colony had already banked ~1,150
Struct, ~3,055 water, ~648 O2 and ~97 H2, so Q3 as authored costs nothing.

| Option | What it is | Effort |
|---|---|---|
| **a (recommended)** | **Make Q3 live, sized to take work**: about twice the sol-10 bank, and no water line (water becomes scarce later; see D2). Proposal: Struct 2,500, O2 1,200, H2 250, due 24 sols after it opens, awards 14 t / 9 t. **Then the ladder repeats Q3 with bills +25% each round**, so there is always a next ship. | Q3: data only. The repeat is a small sim change plus a suite. ~3 h with the sweeps. |
| b | Soil from Mars: switch on the dormant agri chain (compost → fertilizer → soil), so a crew can farm without Earth. | Bigger: recipes, a building, a suite. ~1 day. A good later goal on top of (a), but not instead of it. |
| c | A slow, recurring resupply on a timer, with no quota. | Small, but it removes the goal the ladder gives. |

## D2. Water runs out around sol 40-60

**What happens.** Ice_A (30 t, 180 m NE) is spent by sol ~41, drinkable water
hits 0% from sol ~60, and the stock runs dry by sol ~110. Hope barely moves.

**Data.** A second deposit already exists: **Ice_B, 60 t, 700 m north**,
revealed by a survey. Reaching it takes a chain of about 9 pylons (80 m link
range, ≈360 Struct). The playtest bot never built one. So this is mostly a
"the player is never told" problem, not a missing resource.

| Option | What it is | Effort |
|---|---|---|
| **a (recommended)** | **Tell the player in time.** An alert when a worked ice deposit falls below ~25% ("ICE_A RUNNING LOW - survey for more ice"), plus a guide step pointing to a survey and a pylon chain to Ice_B. Expanding to the second deposit becomes the mid-game goal it should be. | The alert is on the sim's alert channel (inert unless it fires) plus a guide step. ~2 h. |
| b | Move Ice_B closer, or add an Ice_C nearby. | Data only; removes a natural expansion goal. |
| c | Raise greywater recycling (0.85 → 0.95). | Data only; it delays the cliff rather than giving the player something to do. |

## D3. Hope has no tension — it pins at 100 from sol 16

**What happens.** With a sensible layout and comforts, smoothed Hope reaches
100 FLOURISHING by sol 16 and stays there. LIFE ON MARS arrives ~25 sols
after landing.

**Data, sweep C.** With the three biggest weights halved (HopeHousingMax
15 → 7.5, HopePerJob 3 → 1.5, HopePerMoralePoint 5 → 2.5), Hope plateaus at
**83-89 THRIVING**. Births still come (they need 75). FLOURISHING (90+)
becomes a stretch goal, and labs need 85 to research, so discoveries slow
down.

| Option | What it is | Effort |
|---|---|---|
| **a (recommended)** | **Halve the three weights, as in sweep C.** THRIVING is the normal good state; FLOURISHING takes extra care (comforts, views, furnishings: the build-mode Hope lines). | Data only. The battery's Hope-sensitive suites need their expected numbers re-derived. ~2 h. |
| b | Keep the weights and raise the band thresholds instead. | Data only; same effect, but it shifts every band, birth and discovery gate. |
| c | Leave it. | — |

## D4. Hope gives no warning

**What happens.** In the lean run Hope sat at FLOURISHING (91) while food ran
down and the water went stale. The first warning of any kind came at sol 75,
when the food hit zero and six "unsupported" alerts fired at once. The
potability penalty caps at 12 points and only starts below 60% potability.
The guide now warns with live numbers (shipped 10-15), but the sim doesn't.

| Option | What it is | Effort |
|---|---|---|
| **a (recommended)** | **A food-runway line on Hope, plus an early alert.** Under 10 sols of food at the current draw, Hope loses up to 10 points (the same shape as the water penalty: prevention-framed pressure, never a punishment). A "FOOD RUNNING LOW - about N sols left" alert fires once when the runway crosses 10 sols, and re-arms only after it recovers. | A sim Hope line, an alert, and a suite (inert while food is plentiful). ~3 h. |
| b | The alert only, with no Hope effect. | ~1 h. |
| c | Leave it to the guide's warnings. | — |

## D5. The colony lives in blackout

**What happens.** From sol 3 the battery bank hits 0% every night. Power is
short 15-54% of the time and machines are shed 20-78% of the time. The
player sees a permanently starved grid.

**Data, sweep B.** More starting kits (9 solar arrays, 3 batteries instead
of 6 and 2) barely help: still 34% short and 26% shed. The cause is the
**night-time load** - Forge 800 W, Electrolyzer 600 W, Borer 500 W - not
too few kits.

| Option | What it is | Effort |
|---|---|---|
| **a (recommended)** | **A "day shift" mode per machine.** It runs only while the sun is up and idles at night. It is on by default for the Forge and Electrolyzer and a toggle on every heavy machine's card. The guide teaches it when the first blackout comes. | A sim flag (saved), a card toggle, a suite. It **moves the battery baselines**, because the shipped colony's night load changes. ~4 h. |
| b | A night-time load priority (shed the Forge first at night). | Smaller, but the grid still bottoms out every night. |
| c | Leave it; teach the existing per-machine breakers in the guide. | ~1 h, presentation only. |

## D6. Minor: should people work near home?

Work seats are handed out colony-wide in colonist-Id order. In the
surface-habitat capture, the habitat's two desks went to underground
colonists, who commute up. The six surface residents had no seat, because
only six seats existed for 14 people. This is not a bug.

Should seat assignment prefer stations in the colonist's own home (floor or
habitat)? It's a small sim change that moves no baseline without furniture.
**Recommendation: yes, later**, after D1-D5, unless it bothers you in the
hand-play.

---

## If you say yes to the recommended set

I build them as five separate commits, each with its own fails-loudly suite
checks and a full battery with a line diff:
- D2a and D4a first: alerts and a Hope line, inert unless they fire.
- Then D1a, the ladder.
- Then D3a and D5a, which knowingly move baselines. The new pins are
  recorded in the commit.

Then the playtest is re-run, `crew2` (full cargo) and `lean1` (pod only), to
sol 110. **The target:**
- the lean colony survives;
- Hope lives in THRIVING with dips you can see;
- at least one warning comes before any crisis;
- the water expansion happens.

Rough total: about a day and a half of work.

Not covered here, and still separate decisions:
- the Gate-D wording review, which includes "EVACUATED: <name> returned to
  orbit" being said of a Mars-born child;
- quota 1's slack: met at sol 4.2 of 10. I'd leave it, since it is the
  tutorial.

---

## Results — built 2026-10-17 (director answered "D1a D2a D3a D4a D5a")

**Commits.**

| Commit | What it does |
|---|---|
| D2a `ea78003` | Ice running low |
| D4a `7d958c4` | Food runway |
| D1a `defe9ef` | Repeating ladder, save v32 |
| `af1371e` | Robots head home before they run flat (found by the playtests) |
| D3a `7c3b502` | Hope weights halved |
| D5a `3f4441d` | Day shift, save v33 |

Each commit ran the full battery green with the pins unchanged, and every line moved in the diff is explained in its commit message. The battery is now 36 runs. Four new fails-loudly suites were added (`-icelow`, `-food`, `-resupply`, `-dayshift`), and four old print-only suites were converted to fail loudly (`-rooms`, `-growth`, `-discovery`, `-tiers`).

**Two fixes the playtests forced along the way:**
- **Hauling (D2a).** Hauls went to the *first* consumer in building order, and surplus ice was dumped at the Lander. They now go to the nearest consumer, and ice waits at its drill.
- **Robot charging (`af1371e`).** Robots sought a charger at a flat 25% and always finished a delivery. They ran flat 700 m out and never recovered. The reserve is now distance-aware.

**Final playtest** (`rh_playtest.sh 110`, guide path):

| | Before (b61f975) | After (3f4441d) |
|---|---|---|
| Lean run (Crew Pod alone on ship 2) | emptied at sol 77.5 (6 recalled, incl. 2 Mars-born) | **alive at sol 110**: pop 7, 3 born, 0 recalled |
| Full cargo | pop 8 at sol 110 | pop 8 at sol 110, 4 born |
| First warning | sol 75 (food at zero; recall at 77) | sol ~30 ICE_A RUNNING LOW, then ICE_B RUNNING LOW at ~84, WATER QUALITY LOW at ~105 |
| Water | undrinkable from sol ~60; 1.4 t left at sol 110 (full) | expansion to Ice_B by sol 37-38; 66 t in store at sol 110 |
| Smoothed Hope | 100 FLOURISHING from sol 16 | STEADY/THRIVING (68-85) to sol ~60-70, then FLOURISHING (see below) |
| Supply | ships end at sol ~10 | Q3 rounds 0-2 met by sol 45 |
| Night grid | machines shed 12-16% | shed 9-10%; power short 50% (nights run on the bank) |
| First crew | sol 10.5 | sol 14.4 (the day shift slows the opening) |

**Open for the director** (also in `docs/design-decisions.md`):

1. **Hope still reaches 100 late.**
   - D3a works up to sol ~60. After that the permanent Milestones line (vault, harvest, first-born, discoveries) reaches ~30 points and lifts Hope past the clamp by itself.
   - Options: cap it (like Furnishings and Views), let it fade, or accept that a mature colony flourishes.
2. **The day shift slows the opening.**
   - Quota 1 lands at sol 6.4 (was 4.2) and the first crew at sol 14.4 (was 10.5), with about half the Struct at sol 20.
   - Part of the old pace was free night work: a sliver of bank charge ran a whole step's load before clamping.
   - `DayShiftDefaultsOn=0` reverts the defaults.
3. **The repeating ladder stalls by about round 3.**
   - Bills are holdings and are never paid, and they grow 25% a round. Structural plateaus near 6 t, so round 3 (4.9 t structural) and later rounds go unmet, and ships stop after sol ~45.
   - Options: deduct bills on launch, slow the growth, or scale awards.
4. **Cargo effects stack without limit** now that ships repeat. Each Specialty Toolkit adds +15% build speed, uncapped.
5. **Next water horizon.**
   - Ice_B is the last ice in the data. It runs low at sol ~84, and potability starts falling at sol ~105.
   - The stock (66 t) carries the colony well past sol 110, but a third water source, or the dormant agri recycling, is the next step.
6. **Gate-D wording.** Every new player string is a PLACEHOLDER row in `docs/qa/2026-10-15/gate-d-wording.md` (D86-D122).
