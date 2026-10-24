# Hand-play session card (2026-10-24)

This is one sitting, about 90 minutes, to look at everything built since your last real play. It covers:
- the living city (C1-C6);
- going deep (D1-D8);
- water (W1);
- the economy (E1-E3b, plus E4 if it has landed);
- the wording review and the UI pass.

Judge **feel**, not correctness: the tests already cover correctness. Write one line per section in the verdict at the bottom.

**Rule:** only play when both Claude windows are idle. The 8 GB of RAM can't take the game plus builders.

---

## 0. Before you play (about 15 min, hands off)

Run these from the repo folder:

```bash
cd /Volumes/Unreal/red_hope/red_hope
```

The click test should finish with `CLICKTEST VERDICT: PASS`:

```bash
RH_CLICKTEST_RES=1440x860 bash scripts/unreal/rh_clicktest.sh
```

Make the living-city save. This takes about 10 minutes and gives the bot's colony at sol 90: about 115-120 people, three Forges, roads, a rover, and the Big Arrival landed.

```bash
RH_PLAYTEST_DISTRICTS=1 RH_PLAYTEST_SAVE=City bash scripts/sim/rh_playtest.sh 90.35 checkpoint_City guide full
```

Boot that save (1440x860 fits your screen):

```bash
UE=/Volumes/Unreal/UE_5.8/Engine/Binaries/Mac/UnrealEditor; P=/Volumes/Unreal/red_hope/red_hope/red_hope.uproject; "$UE" "$P" /Game/RedHope/Maps/L_Slice -game -windowed -resx=1440 -resy=860 -nosound -RHNoTitle "-ExecCmds=r.setres 1440x860w, RH.After 2 RH.Load City, RH.After 2.5 RH.SetSpeed 1, RH.After 3 RH.Floor 0"
```

**Controls:**
- Space pauses. Keys 1-4 set the speed (1x, 3x, 8x, 60x).
- Backquote (`` ` ``) opens the console.
- Esc backs out of tools and menus.
- PageUp and PageDown change floors.
- `RH.Lag 0` removes the order delay, so things you order happen at once.
- `RH.Save Mine` saves your own game, and `RH.Load Mine` loads it back.

---

## 1. The living city (10 min)

1. Look around the surface at 1x, then 8x. Do the roads, rovers and shuttles make it feel like a city at work?
2. Press **Traffic** on the bottom bar. Are the busy roads obvious? Hover a road to see its numbers.
3. Open the **City** panel on the right. Does it tell you what the city needs (homes, jobs, rations)? Other panels should fold away as it opens.
4. Watch growth for a few sols at 8x. Do new homes and civic rooms appear in the painted districts?
5. **Demolish Road:** hover a road edge. Is the salvage readout clear?

## 2. People and Hope (10 min)

1. Open **Crew**. Is the Hope breakdown readable at a glance? Look for the long-commute line and the deep-home Shelter line.
2. Find the wording you approved: "recalled to orbit", "short rations", the LOW and UNSETTLED bands. Does it read calm and humane?
3. Optional stress test, from a fresh save:
   - type `RH.Save Before`;
   - starve the colony of power or food and watch an alert;
   - then `RH.Load Before`.

   Is the alert prevention-framed? Does it tell you what would help?

## 3. Going deep (20 min)

1. Use **PageDown** to walk floors -1 to -5. Do the digs, rooms and crew look right?
2. Press **Section** on the shaft card, or **V**: the deep view. Can you read the whole underground at once? Try Q/E to turn and W/S to move the cut. (The floor labels at 20 floors are a known small-text issue.)
3. On the shaft card, try **Fit a lift car** (400 kg). Is the button readable? Does a robot ride down and work below?
4. **Tunnel** button: drag a tunnel between two digs on one floor. Watch the Borer carve it at 8x.
5. **Cavern** button: drag a cavern box beside a shaft. Watch it carve across floors.
6. **District** tab underground: paint a Homes shape and an Industry shape on one floor. The hint should say this changes that floor's commutes.
7. Deeper floors, -6 and below:
   - the shaft list shows each floor's cooling watts;
   - starve the power and a deep floor turns amber (warm), costing its residents a little Hope.

   Is that clear?

## 4. Water and freight (5 min)

1. Look for the outpost ice deposits (720 m and beyond): the road out, and the rover hauling raw ice home.
2. Water made should be well above water drunk. Check the resource bar.

## 5. Earth and money (15 min)

1. On the Objective panel's cargo list, check:
   - **Credits** and **Ship mass**: is it clear what you can afford and carry?
   - ADD an item, then **REMOVE** it.
   - **Cargo to Earth**: what Earth will take at launch, and what it will buy.
2. Set the City / Earth share with `RH.Growth share 0.5` (try 0.25 and 0.75). Does the trade-off between growing and paying Earth feel like a real choice?
3. Launch a ship. Is it clear what left and what you were paid?
4. If E4 has landed: trade with Zarya. Do the prices and settled credits make sense?

## 6. Free play (15 min)

Play it as a player would, at 8x. Note anything confusing, ugly, slow or pointless.

---

## Verdict (one line each: OK / change: ... / discuss)

```
1 living city:
2 people and Hope:
3 going deep:
4 water and freight:
5 Earth and money:
6 free play / overall feel:
anything else:
```

**Known issues, so you don't report them:**
- The deep-view floor labels are small at 20 floors.
- Shuttles are plain boxes.
- Lift-car doors on later shafts don't slide.
- Crew always board at the first shaft.
- The rover can overlap a building when it turns on the spot.
- Placeholder numbers (prices, bonuses) are for testing, not balance.
