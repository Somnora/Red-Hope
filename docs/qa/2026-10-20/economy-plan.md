# Step 3, economy: plan and decisions (2026-10-20)

The director's rulings (`docs/design-decisions.md`, "The grand vision: four rulings (2026-10-18)"):
- no combat, ever: conflict is trade, espionage, politics and resilience (embargoes, covert operations, competing for colonists through Hope);
- politics stays: Earth, the CEO arc, the Solidarity Dilemma, rivals, treaties, trade blocs;
- a city of **thousands**;
- **several regions** in step 4, so the economy must be ready for them but must not need them;
- step 3 is **economy**: markets, exchange rates, regional specialization, a Martian currency.

Two older rulings bind this step:
- **Ruling 7:** the manifest is a mass budget plus discrete packages.
- **Ruling 11:** Hope is a derived index and is never spent.

This brief turns step 3 into gates and asks four questions. **Answered 2026-10-20: Q1a Q2a Q3a Q4a** (all four recommendations). It is design only. Nothing here is built. C6 (traffic) is still in progress in the same tree, and step 2 (going deep) is planned before this one.

Every number below is a placeholder for the director. The appendix records what the code does today, with file:line.

---

## The headline finding

Red Hope has trade and politics, but it has no measure of value. Every exchange is in kilograms of one thing for kilograms of another:
- **Earth pays in cargo mass.** Meeting a quota earns a manifest budget in kg (`AwardMassKg`). Unspent budget is lost when the next quota opens. There is no balance, no price and no credit.
- **Earth never takes anything.** A quota is a holding threshold: the colony only has to *hold* the bill, and nothing is deducted. So there is no export to Earth and nothing to sell.
- **Rivals barter fixed lots.** Each rival row trades one fixed lot for another (Zarya: your 100 Struct for their 150 Ice). There is one convoy at a time. A rival is a name, a distance in km and a relation score. It has no stores, no production and no position on any map.
- **The colony doesn't know its own flows.** The production ledger (C5d) measures only oxygen, water and food, and it records nothing about consumption. A market needs supply and demand for every resource.
- **Amounts are floating-point, kept in unsorted maps.** Stocks are `double` kilograms in a `TMap`, and holdings are summed over every building. Prices built on those numbers would drift between runs and between the two bands.
- **Everything lands at the Lander,** and the Lander sits at the origin.

The good news is that the political machinery already exists and is deterministic: relations, Earth tension, the identity axis, Influence, embargoes, covert operations and the Solidarity Dilemma. Trade, Earth and quota code all run in both bands in the same order. So the economy can be laid underneath politics, not built beside it.

---

## The gates (each a separate commit, battery green, director look at the end of each)

| Gate | What it is | What you see |
|---|---|---|
| **E1. The ledger** | The production ledger grows from 3 legs to every resource, and it records consumption as well as production. Each resource row gets an Earth reference value (a new column in `RH_Resources`, integer milli-credits per kg). Trade and quota amounts are rounded to whole grams at the trade boundary. Nothing reads any of it yet. | Nothing changes. Every battery run stays line-identical. |
| **E2. Earth credit** | The quota award is paid in **credits** (Q1). The colony has a saved credit balance, and unspent credits carry over. Manifest items get a credit price. The ship still has a mass cap (ruling 7), now a property of the ship, not the award. Manifest effects become data-driven verbs instead of name checks, and stacking gets a cap. | The CEO pays you. You shop the manifest with a balance instead of a weight budget. |
| **E3. Selling to Earth** | Quota cargo leaves on the supply ship and is paid for (Q2). Above the quota, you can sell surplus to Earth at its reference value, less a launch cost per kg (hydrogen and oxygen burned). Earth tension and the identity axis move Earth's prices, as they already move the award (`GetRequisitionMultiplier`). | Every supply ship is also an export run. "Pay Earth or build Mars" becomes a price, not just a share slider. |
| **E4. The Martian market** | Rivals get simple stores and production (an aggregate model, no buildings). A market holds one integer price per resource. It reprices once a sol from supply and demand in the ledgers, with a bounded step. Convoys carry orders at market prices instead of fixed lots. Several convoys can be out at once, and C6's cargo rovers can drive them. | Ice gets cheap when Zarya's wells run full; Struct gets dear when everyone builds. |
| **E5. A Martian currency** | A second currency, issued by the colony as a political act (Q3). It floats against Earth credit, and the rate moves with the trade balance, Earth tension and the identity axis. Rivals that join can price in it. Influence stays a separate diplomatic score, and Hope is never spent. | A rate on the panel, and a real choice about whose money Mars uses. |
| **E6. The levers** | Treaties, trade blocs, tariffs and embargoes become market rules: who may trade with whom, at what markup. Today's embargo and closed routes become rule entries. Covert operations and sabotage move prices (a sabotaged rival's exports get scarce) instead of only moving relations. There is still no combat anywhere. | Politics with prices: a bloc that undercuts Earth, an embargo you can feel in your stores. |
| **E7. Region-ready** | Every market, price, route and rival carries a RegionId. A route has two depots instead of a scalar distance. A trade depot is a building you place, and it replaces "the Lander at the origin". Each region gets an endowment table (what is cheap where) for step 4 to fill in. With one region, play is unchanged. | Nothing new for the player. Step 4 plugs in. |
| **E8. The market panel** | A Market panel with prices, a 30-sol price history per resource, the trade balance, the exchange rate, open orders and route status. A trade overlay links depots and rival markers. Presentation only. | You can read the economy at a glance. |

**Size.** Each gate is one to three working sessions. The riskiest are E2 (it changes the supply line every suite depends on) and E4 (the first quantity in the sim that feeds back on itself).

**Order.** E1 must come first. E2 and E3 are Earth-side and can ship before rivals have markets. E5 needs E4. E6 and E7 can swap. Panels can be shown with each gate if the director wants to see them as they land.

**Saves.** Strict as now. E1 to E7 each bump the version; E8 does not. Version numbers are assigned when a gate is built, after step 2's, not reserved now. Every new map is written sorted by name string, not in `TMap` order (see the risks).

---

## Risks per gate

**General rules for every gate:**
- Prices are `int64` milli-credits, and traded amounts are whole grams.
- Repricing is a threshold on the monotone clock (once a sol), like `RollProductionLedger`, so the 0.1 s agent band and the 60 s era band reprice on the same step.
- Order is fixed: resources in row order, rivals by name string (never by FName index; the covert seed bug of 2026-07 came from exactly that), and orders by Id.

**E1. The ledger.**
- *Determinism:* the ledger is an accumulator per resource, rolled at the sol threshold, so there is no new risk. Record consumption at the same call sites in both bands.
- *Era parity:* era logistics moves material without travel, so era's consumption is lumpier within a sol. The sums must still agree within the 5% bar over a sol window. Add that check to the ledger parity pair.
- *Save:* the bigger ring (resources x window). Sorted write.
- *Bar:* the battery is line-identical. A new `-economy` suite pins ledger totals on a known run.

**E2. Earth credit.**
- *Determinism:* the balance is an integer. The award formula keeps today's terms (on-time or late, the requisition multiplier, research funding), converted to credits.
- *Era parity:* the award is paid at one step in `StepQuota`, which already runs in both bands.
- *Save:* the balance, plus the ship's mass cap. The manifest's item list is unchanged.
- *Bar:* not line-identical, because carry-over changes what later ships hold. Pick a default exchange of 1 credit per award-kg and 1 credit per item-kg, so a colony that spends everything sees no change. List every suite that moves.

**E3. Selling to Earth.**
- *Determinism:* cargo is drawn in building Id order, as `SpendTradeLot` already does.
- *Era parity:* sales happen at launch, a single event in both bands.
- *Save:* the export list on the ship.
- *Bar:* Q2 changes the quota ladder, so `-resupply`, `-crisis` and the C5 growth playtests move on purpose. Re-run the 110-sol playtests and report population and rounds met against C5e's table.

**E4. The Martian market.**
- *Determinism:* this is the riskiest gate. A price that reads stock can amplify small drifts. Mitigations: price from the windowed ledger (flows, not stock); a bounded step per sol (for example, at most 5%); integer maths; no float in the update.
- *Era parity:* both bands must reach the same price path within a tolerance. Add a paired-run test on prices, not just ledgers. If the bands diverge, fall back to repricing on rival-side numbers only, which the agent band cannot touch.
- *Save:* prices, the price ring, rival stores, and open orders.
- *Bar:* with no rival active, the battery is identical. A new `-market` suite covers repricing, a glut, a shortage, a save/load round trip, and parity.

**E5. A Martian currency.**
- *Determinism:* the rate is a pure function of saved scalars, updated at the sol threshold.
- *Era parity:* shared code.
- *Save:* the second balance, the rate and the issue flag.
- *Bar:* no change until the currency is issued. `-solidarity` and the endings suites must stay identical.

**E6. The levers.**
- *Determinism:* rules are a list ordered by Id; a trade is checked against every rule in order.
- *Era parity:* the existing covert roll is content-seeded and stays as it is.
- *Save:* the rule list. The existing embargo and closed-route sets migrate into it, which a version bump makes safe.
- *Bar:* `-covert`, the embargo and Dilemma suites keep their exact outcome sequences (for example `-covert`'s CXCX).

**E7. Region-ready.**
- *Determinism:* none new.
- *Save:* RegionIds and depots. The existing Lander becomes depot 1.
- *Bar:* line-identical with one region.

**E8. The market panel.** No sim or save change. Captures plus a frame-time check.

---

## Q1. What does Earth pay in?

| Option | What it is |
|---|---|
| **a (recommended)** | **Credits.** Meeting a quota earns credits. You spend them on the manifest, and what you don't spend carries over. Each ship still has a mass cap, so ruling 7's feel stays: you still pack a ship. |
| b | Keep the kilogram budget for Earth, and use credits only with rivals. Simpler, but Earth and Mars then have two unrelated economies. |

## Q2. Does Earth take the quota?

| Option | What it is |
|---|---|
| **a (recommended)** | **Yes.** The quota cargo leaves on the supply ship and is paid for. This fixes the open point that a rich colony meets every round just by holding the bill. It also makes the City / Earth share a real cost. It moves balance on purpose. |
| b | Keep the quota as a holding threshold, and add sales to Earth separately. |

## Q3. When does a Martian currency appear?

| Option | What it is |
|---|---|
| **a (recommended)** | **As a political act later in the arc.** You issue it once the identity axis leans Martian, or as part of a Defy choice. Earth reads it as a step away; tension rises. Rivals can choose to join it, which makes a trade bloc. |
| b | From the start, beside Earth credit, with a floating rate. |
| c | Never. One Earth credit, and politics moves prices instead. |

## Q4. Is there money inside the city?

| Option | What it is |
|---|---|
| **a (recommended)** | **No.** The economy is the colony's books: production, stores, trade and currency. People's wellbeing stays Hope, which is never spent. It keeps thousands of colonists cheap, and it keeps the mental-health framing clear of poverty mechanics. Districts can still have trade interests for the politics of step 1. |
| b | Households earn wages and pay prices. It is richer, but much heavier, and it touches Gate-D framing. |

---

## Appendix: what the code does today (read-only survey, 2026-10-20)

**The Earth quota and the award** (`Source/RedHopeSim/Private/RHSimWorldSubsystem.cpp`):
- **Quotas are holdings, never deducted.** `GetQuotaProgress` (`:10178-10196`) compares `GetTotalSolid` (every building's input and output stores, `:10162`) plus the pool against the bill. `StepQuota` (`:1626-1645`) only checks; nothing is removed. This is logged as an open point in `docs/design-decisions.md:325`.
- **The bills.** `ScaleQuotaBill` (`:1798`): the raw bill x `QuotaRepeatBillMul`^round, rounded to whole kg. `GetNextQuotaBills` (`:1810`) walks the same ladder. The data is in `docs/data/RH_Quotas.csv` (Q1-Q3, on-time and late awards in kg).
- **The award is mass.** `AwardMassKg` = on-time or late award x the requisition multiplier, plus research funding in kg (`:1653-1659`). It is reset when the next quota opens (`:1782`), so unspent budget is lost. Config: `RH_Config.csv:16-17`.

**The supply ship and the manifest:**
- Items can be added only while `AwaitingManifest`, and only while within the mass budget (`:10850-10870`). `LaunchShip` (`:10874`) sets a fixed transit (`ShipTransitSols`, `RH_Config.csv:15`). Saved at `:11040`.
- **Effects are keyed by item name** (`ApplyManifestItemEffect`, `:1875`); the CSV's Effect column is only display text. Effects stack without a cap (`docs/design-decisions.md:325`).
- Items have mass and no price (`docs/data/RH_ManifestItems.csv`). Ruling 7 (`docs/design-decisions.md:25`): mass budget plus discrete packages.

**Rivals and the convoy:**
- One convoy at a time: `ConvoyRival`, `ConvoyLegSols` (`Public/RHSimWorldSubsystem.h:2398-2409`). Dispatch commits fuel, wear and your lot at departure (`:7211-7240`).
- Distance is a scalar: `ConvoyLegSolsFor` = `DistanceKm / ConvoySpeedKmPerSol` (`:7436`). The convoy is a sol accumulator that a dust storm freezes (`StepTrade`, `:7443`).
- **Fixed lots:** `ExportLot` / `ImportLot` strings per rival (`docs/data/RH_Rivals.csv`; Meridian is dormant). `HasTradeLot` / `SpendTradeLot` (`:7371`, `:7386`) drain building stores in Id order, then the pool.
- Goods land at the first Lander found (`:7492`; covert loot at `:7657`). The Lander is placed at the origin (`:410`).
- A rival has no stores, production or position. Its marker is a content-hashed bearing in presentation only (`Source/RedHope/Private/RHColonyVisualizerSubsystem.cpp:3985-3995`).

**Politics (already built and deterministic):**
- `RivalRelations`, `HiddenTensions`, `IdentityAxis`, Earth tension, `Influence` ("the diplomatic soft currency", `RHSimWorldSubsystem.h:2414`, `:2437`, `:2463-2468`), `GetRequisitionMultiplier` (`:869`).
- Embargoes and defection: `EmbargoGrace`, `DefectedRivals` (`:2464-2465`). Closed routes from the Dilemma: `IsRouteClosed` (`:885`).
- The covert roll is seeded on a content hash (`docs/design-decisions.md:218`). `HiddenTensions`, `RivalRelations` and `Stocks` are saved unsorted, which was accepted as harmless (`:222`).

**The resource pool and stores:**
- `Stocks`: `TMap<FName, double>` for fluids, gases and abstract resources (`RHSimWorldSubsystem.h:2065`); solids are kept in building `InputKg` / `OutputKg`. `ImportStock` counts undeployed hardware (`:2074`). `AddStock` (`:10154`) is a raw double add.
- `RH_Resources.csv` has no value or price column. Hope is a non-spendable ledger row (ruling 11, `docs/design-decisions.md:29`).

**The production ledger (C5d):**
- Only 3 legs (oxygen, water, food), with a window of up to 8 sols (`RHSimWorldSubsystem.h:2371-2376`). There is no consumption side.
- `RollProductionLedger` (`:5915`) is a threshold on the monotone clock; `GetProductionPerSol` (`:5941`); `ProductionLeg` (`:5900`). It is called from `StepCityGrowth` (`RHSimGrowth.cpp:495`).

**The City / Earth share:**
- `CityShare` is saved, 0..1 (`RHSimWorldSubsystem.h:2023-2024`). `GetGrowthAvailability` (`RHSimGrowth.cpp:231`) holds back (1 - share) x the next round's bill. It is the existing political seam between building Mars and paying Earth.

**The two bands:**
- The agent band (`StepSim`, `:548-552`) and the era band (`EraStep`, `:719-723`) both call `StepCityGrowth`, `StepTrade`, `StepEarth` and `StepQuota`, in the same order.
- `EraLogistics` (`:731`) teleports dug material into hoppers at the fleet rate: no distance, no hauler capacity.

**Presentation** (`Source/RedHope/Private/RHCommandDeckPanels.cpp`):
- The manifest picker shows kg used against the kg budget (`:519-578`). The rival card shows distance and fixed lots (`:1762`). The identity line shows Influence (`:1683`).
- There is no market, price or history view.
