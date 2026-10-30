# E7, region-ready: design (2026-10-25)

Step 3 (economy), gate E7. **Design only; nothing here is built.** The code was read at HEAD `6119e7d` with `git show HEAD:`. Its source is E4's (`76d9cc1`, save v55): the commits after it change docs only.
- **Built:** E1-E4 and the economy fixes.
- **Designed and answered, not built:** E5 and E6. Their names here are design names.
- **Queued ahead of E7:** the step-2 fixes, E5, E6, D6e and D7. E7 depends on none of them (section 10).

Short names:
- **W** is `Source/RedHopeSim/Private/RHSimWorldSubsystem.cpp`, and **H** is its header.
- **M** is `RHSimMarket.cpp`.
- **V** is `Source/RedHope/Private/RHColonyVisualizerSubsystem.cpp`.

**Sources:**
- `economy-plan.md`: E7's row. "Every market, price, route and rival carries a RegionId. A route has two depots instead of a scalar distance. A trade depot is a building you place, and it replaces 'the Lander at the origin'. Each region gets an endowment table (what is cheap where) for step 4 to fill in. With one region, play is unchanged." Its risk line: "*Save:* RegionIds and depots. The existing Lander becomes depot 1. *Bar:* line-identical with one region."
- `territories-plan.md` (Q1a-Q4a):
  - T1 moves "the Lander's roles to this colony's depot (E7)";
  - T2 gives regions a record with "endowment table (E7)";
  - T5 makes rivals places;
  - T6 makes routes "a length in km and a terrain class" between depots.
- The E4, E5 and E6 designs with their director answers.
- E4's `design-decisions.md` entry, and the refresh sections appended to E5 and E6 today.

**Binding rulings:**
- No combat, ever.
- No money inside the city (Q4a, ruling 11).
- Earth pays in credits (Q1a).
- Every new order in the sim is by name string, never by FName index (the covert-seed bug of 2026-07).
- One region today: everything E7 adds must be line-identical with one region.

## What E7 builds, in one paragraph

E7 gives the economy the four seams step 4 needs, and changes nothing a player can see.
- **Depots.** A depot is a trade end point with a region and an owner. The colony's depot in region 0 is the Lander, which becomes "depot 1"; each rival gets an off-map depot in its row's region.
- **Routes.** A route joins the colony's depot to a rival's depot and carries its own length. Today that length is the rival row's `DistanceKm`, copied exactly, so every convoy leg is the same double it is now.
- **Landing.** Convoy goods and covert loot land at the colony's depot, not at "the first Lander found".
- **The endowment.** A table of per-region, per-resource permilles on E4's anchor value `V`, read through one accessor that answers 1000 when a region has no row. It ships empty.

Everything is **derived** from saved buildings and data rows, so E7 saves nothing new and needs no version bump. The battery is therefore line-identical, version lines included. That is a deliberate deviation from the plan's "E1 to E7 each bump the version" (section 7). Whether the player can already place a trade depot building is director question Q-A.

---

## 0. What HEAD gives E7

**RegionIds that exist** (all 0 today):
- **Rows:** rival rows (`Data/RHRows.h:412-413`, "the rival's home region") and deposits (`RHRows.h:231-232`; `RHSimTypes.h:138`).
- **Records:** districts (`RHDistricts.h:54`); road edges (`RHRoads.h:30`); floors, shafts, tunnels, caverns and tram lines (H:296, :322, :355, :403, :458).
- **E4's markets:** `FRHRivalEconomy::RegionId` and `FRHMarket::RegionId` (H:3827, :3835). One market per region with an economy rival (`FindMarket(RegionId)`, H:3868). The market accessors take a `RegionId` that defaults to 0 (H:1560-1597).
- **E4's save** already writes one market per region by `RegionId` (W:37's v55 note; `WriteMarketPayload`, M:1280).

**What is still region-blind:**
- **The convoy.**
  - `FRHConvoy` (H:543) has no route and no region.
  - Its leg time is `ConvoyLegSolsFor(Rival)` (W:9657-9662): `Row->DistanceKm / max(ConvoySpeedKmPerSol, KINDA_SMALL_NUMBER)`. `DistanceKm` is a **`float`** (`RHRows.h:405`), and the call is re-read on every step: `StepTrade` at M:1136 and `GetConvoyProgress` at M:1269.
  - With the data, Zarya is 120 km and Meridian 200 km at 60 km a sol (`RH_Config.csv` `ConvoySpeedKmPerSol`), so their legs are 2.0 and 3.33 sols.
- **Where trade lands.**
  - Convoy goods: `StepConvoyReturn` drops each solid line into "the trade depot (the Lander)", meaning the first `Lander` building not under construction, else the pool (M:1203-1218).
  - Covert loot: the same loop in `ResolveCovert` (W:9762-9772).
- **The picture.**
  - A convoy's rover goes from "home", the first `Lander` in the building list (V:4338-4341), to `RivalMarkerPos` (V:4193-4204, a name-hashed bearing 200 m + 0.3 m a km out, inside the valley).
  - E8 (the market panel) will draw a line between them.
- **The colony's own books stay colony-wide:** E1's ledger, E2's credit, E3's sales, and E5's purses and rate (designed). They belong to the colony or to Earth, not to a place.

**The Lander's roles at HEAD** (`NAME_Lander` and `FName("Lander")` outside the suites):

| Role | Where | Trade-facing? |
|---|---|---|
| Placed at the origin at world start | W:546 | — |
| A store and haul sink, beside stockpiles | W:959, :983, :1051, :1177, :1244, :1295, :1343 | no |
| Cannot be demolished ("the colony's anchor"); salvage target | W:10504, :10732 | no |
| Refunds (growth bills, furniture and walls) | `CreditLander` W:11373 (callers in `RHSimGrowth.cpp` and W:11216, :12102) | no |
| Road refunds; rover and charge docks | `RHSimRoads.cpp:465`; `RHSimRovers.cpp:68`, `:263`, `:308`; W:12809, :12829 | no |
| The save-time home of cargo whose source is gone | W:13440 | no |
| **Convoy goods land here** | M:1203-1218 | **yes** |
| **Covert loot lands here** | W:9762-9772 | **yes** |
| **A convoy's drawn start** | V:4338-4341 | **yes** (presentation) |

E7 moves the three trade-facing roles to the depot. T1 moves the rest when it builds the colony record. That is the plan's split ("the Lander's roles move to this colony's depot") read narrowly, so E7 stays a trade gate.

---

## 1. Depots

**The record** (H, beside E4's structs, derived, never saved):
```
struct FRHDepot
{
    int32 Id = 0;            // 1 = the colony's depot in region 0; rivals' depots follow (below)
    int32 RegionId = 0;
    FName Owner;             // NAME_None = the colony; else the rival row's name
    int32 BuildingId = 0;    // the colony's: the depot building's Id; a rival's: 0 (off-map until T5)
};
```

**The colony's depot in a region R** is one rule, `FindColonyDepotBuilding(int32 RegionId) const`:
- Q-A (b), recommended: **the first `Lander` in the building list that is not under construction, in region R.** That is exactly HEAD's rule at M:1212 and W:9767.
- Q-A (a): the first completed `TradeDepot` in building order in region R, and otherwise that Lander.
- A building's region is `GetBuildingRegionId(const FRHBuildingInstance&)`. It answers 0 for every building today; T2 changes this one function.
- A colony with no such building has no depot in R, and landing falls back to the pool, as HEAD's loop does.

**Rival depots.**
- One per rival row: `Owner` = the row, `RegionId` = the row's `RegionId`, `BuildingId` 0.
- Rivals are walked in **name-string order** (case-sensitive ordinal), as E4's `RivalEconomies` are, never by FName index.
- Barter rivals (Greed) and dormant ones (Meridian) get depots too: a depot is a place to trade with, whatever the terms.

**Ids.** The colony's depot in region 0 is **1**; the rivals' depots are 2, 3, ... in name order. Ids are a pure function of the rows, so they are stable across saves and both bands, and nothing saves them. T2's regions will append the colony's depots in other regions after the rivals'. The order is fixed here so T2 cannot reshuffle it.

**Accessors** (public, const):
- `GetDepots(TArray<FRHDepot>&)`;
- `FindColonyDepotBuilding(RegionId = 0)`;
- `GetColonyRegionId()`, which answers 0 (T2 changes it).

Derived on demand. The building scan costs what HEAD's landing loop costs today, and the visualizer already makes that scan per convoy per frame. No cache, so no key can go stale.

**What the depot does in E7:**
1. **Convoy goods land in its `OutputKg`** (M:1203-1218: "the first Lander" becomes `FindColonyDepotBuilding(GetColonyRegionId())`). Fluids still join the pool, and the no-building fallback to `AddStock` stays.
2. **Covert loot lands there** (W:9762-9772, the same change).
3. **A convoy's rover starts and ends there** (V:4338-4341 reads the same accessor, through a const getter).

Under Q-A (a), a `TradeDepot` is **not a store**. Goods landing in its `OutputKg` are hauled to stockpiles by the existing rule for output (W:1244), like any producer's. So no store predicate changes (W:959-1343), and the depot reads as an unloading dock.

---

## 2. Routes

**The record** (derived, never saved):
```
struct FRHRoute
{
    int32 Id = 0;            // = the rival depot's Id - 1: route 1 to the first rival by name, ...
    int32 DepotA = 1;        // the colony's depot in the rival's region (region 0 today: depot 1)
    int32 DepotB = 0;        // the rival's depot
    double LengthKm = 0.0;   // E7: (double) the rival row's DistanceKm, verbatim; T6: the map distance between the depots
    uint8 Kind = 0;          // 0 = abstract (a length from data); T6 adds map routes with a terrain class
};
```

**One route per rival depot.** `GetRouteFor(FName Rival, FRHRoute& Out) const` answers false for an unknown row. `GetRoutes(TArray<FRHRoute>&)` lists them in Id order.

**The leg time reads the route:**
```
double ConvoyLegSolsFor(FName Rival) const
{
    FRHRoute R;
    const double Speed = FMath::Max(ConvoySpeedKmPerSol, KINDA_SMALL_NUMBER);
    return GetRouteFor(Rival, R) ? R.LengthKm / Speed : 0.0;
}
```
- **Bit-identical by construction.** `(double)` of a `float` is exact, so `R.LengthKm` is the same double HEAD's `Row->DistanceKm` promotes to, divided by the same `Speed`. The integer metres the plan speaks of are T6's: E7 keeps the row's own value, not `llround(km × 1000)`. That would differ for an injected row such as 33.33333 km, and mutation 2 pins this.
- **Still read on every step,** as HEAD's is, so a data change mid-trip behaves as today.

**A convoy has no saved route.** Its route is the one its rival names, derived at every read. T6 freezes a route on a convoy at dispatch, because a map route can change while a convoy is on it, and that is T6's save change.

---

## 3. RegionIds everywhere the economy keys things

| Record | Its region in E7 | Notes |
|---|---|---|
| Rival row | `Row.RegionId` | E4. Unchanged. |
| Rival economy, market | `RegionId` | E4. One market per region. |
| Depot | colony: `GetBuildingRegionId(depot building)`; rival: the row's | New. |
| Route | its depots' (A is the colony's depot **in B's region**) | New. With one region both are 0. |
| Convoy | its route's | Derived from the rival. Not saved (section 2). |
| A market price or tally read by the deck | **the rival's region, passed explicitly** | The deck's one price read (`RHCommandDeckPanels.cpp:2534`) and `RH.Market` (`RHCheats.cpp:1126-1135`) use the default `RegionId = 0`. E7 passes the rival's `RegionId` (from its economy) at each call, so no reader depends on the default when T2 adds a region. |
| E1 ledger, E2 credit, E3 sales, E5 purses and rate, E6 rules | none: colony-wide | The colony's books and Earth's terms. E5's "the region's market counts in marks" reads `GetColonyRegionId()` (0). E6's `Party` is a name and can name a settlement later. |

**The default arguments stay** (H:1560-1597), so E4's suites and hooks are untouched; only the presentation call sites stop relying on them.

---

## 4. The endowment table

**What it is.** A per-region, per-resource **permille on E4's anchor value V**: "what is cheap where". 1000 is Earth's value. 500 makes a resource half as valuable in that region's market, so its start price, its floor (0.25 V) and its ceiling (4 V) all halve. E4's update is relative to V (its anchor and step are in basis points of the price), so the whole price path scales. (Q-B asks whether the endowment should act here or on rival production.)

**Data.**
- A new `RH_Endowments.csv` and `DT_Endowments`, with columns `RegionId, Resource, AnchorPermille`.
- **Shipped with no data rows.** Step 4 (T2's regions) fills it.
- The row struct, `FRHEndowmentRow` (`Data/RHRows.h`), and one accessor, `URHDefinitionsSubsystem::GetEndowmentPermille(int32 RegionId, FName Resource)`, which answers **1000** when no row exists.
- Rows must be whole numbers in 1..100,000. A row out of range is a Warning at world start and is read as 1000, as E4 treats a malformed rival row.

**Where it applies: one place.**
- `BuildMarketState` (M:297-300) sets each slot's `V = Res->EarthValue_mcr`.
- E7 multiplies it: `V = EarthValue_mcr * Permille / 1000`, in int64. With 1000 this is exact (V is at most 900,000 mcr, so the product never nears overflow), so V, the floor, the ceiling and the start price are HEAD's integers.
- **The kept-price path.** `BuildMarketState` keeps a market's prices across a resync (`bKeep`, M:286). E7 clamps a kept price into the new `[Floor, Ceil]`. With no endowment change the band does not move, and a kept price is always inside it already, so the clamp changes nothing.
- **Nothing else reads it:**
  - E3's Earth prices use `EarthValue_mcr` directly;
  - `ClearConvoy`'s no-market fallback (M:661) has no region;
  - E5 and E6 read V only through the market.

**A harness hook,** `Debug_SetEndowment(RegionId, Resource, Permille)`: runtime only, never saved. It bumps the definitions' rivals generation, so `SyncRivalEconomies` re-derives the markets on the next read, exactly as `Debug_InjectRival` does.

---

## 5. Inertness

With one region, Q-A (b), and no endowment row:
- **The depot** is the first completed Lander, the very building HEAD's loops pick. The landing writes the same `OutputKg`, then E1's `In` leg and the same broadcast.
- **The leg time** is the same double (section 2), so every convoy, standing trade and storm freeze lands on the same step.
- **V** is the same integer, so every price, floor, ceiling and reprice is unchanged.
- **The rover** starts at the same `LocationCm`.
- **Nothing is saved**, so every file is byte-identical, version lines included.

**The bar.**
- The battery (`rh_battery.sh`, judged on `errors=0 completed=1`) is line-identical to the gate before it, **version lines included**, plus the new `regionready.log`.
- The 110-sol playtest is byte-identical to `Saved/baselines/ind_full_playtest.txt` (the `RH_PLAYTEST_DISTRICTS=1` run).
- `-scale`'s digests are unchanged.
- The click test is unchanged.

**Under Q-A (a)** two things move, both named:
- the build menu gains `TradeDepot`, so the click test's REACH sweep and `deck.contrast` count one more button;
- `-dayshift`'s list of eligible buildings is unchanged only if the depot draws no power. The row must have none.

---

## 6. Era parity

- Depots, routes and V are pure functions of buildings and data rows, read at the same points in both bands.
- **Under Q-A (b) no building changes the depot.**
- **Under Q-A (a)**, a `TradeDepot` completing changes the landing building from the next landing on. Construction completes at band-dependent steps (era refuses construction; the paired suites call `EraStep` directly). So in the step a depot completes, a convoy can land at the Lander in one band and at the depot in the other. Only *where* the goods sit differs; E1's `In` totals, the market and money do not. This is the colony-side class E4 already allows.

---

## 7. Save: nothing new

- **The plan expected E7 to save depots and bump the version.** Every E7 record is derivable:
  - the colony's depot from the building list (saved since the beginning);
  - the rivals' depots and the routes from the rival rows;
  - the endowment from data.

  Saving them would add a second truth that a hand-made file could contradict.
- **So E7 takes no version, adds no block** and changes no byte of any file.
- **Where saved state arrives:**
  - T2 saves its region list;
  - T4 saves new settlements, whose depots are buildings and so already saved;
  - T6 saves a convoy's route and journey.
- **Logged as deviation 1.**

---

## 8. Tests: `-regionready`, a new fails-loudly suite

It lives in `Source/RedHopeSim/Private/RHSimRegionReadySuite.cpp` and is added to `rh_battery.sh`. No path logs an Error on purpose. Hooks: `Debug_SetEndowment` (section 4) and `Debug_SetColonyDepot(BuildingId)`. The second is runtime only and never saved: it points the colony's depot at a building, so landing can be tested without a depot row.

| Check | What it pins |
|---|---|
| RR0 | **Data.** `RH_Endowments.csv` equals `DT_Endowments`; there are no rows; `GetEndowmentPermille` answers 1000 for every (region, resource) asked, and a row out of range reads 1000 with one Warning. |
| RR1 | **Inertness, exact.** For Zarya, Meridian, Greed and injected rows at 30 km and **33.33333 km**: `GetRouteFor` exists, `LengthKm == (double)Row.DistanceKm` bit for bit, and `ConvoyLegSolsFor` equals HEAD's expression computed in the suite, bit for bit. The colony's depot is the Lander's building. Every market slot's V equals `EarthValue_mcr`. |
| RR2 | **Landing follows the depot.** With `Debug_SetColonyDepot` on a stockpile, a standing trade's goods, a written order's goods and a clean covert op's loot land in that building's `OutputKg`, and none in the Lander's. E1's `In` legs equal a twin that lands at the Lander. A rover's home (the const getter V reads) is the stockpile. Clearing the hook returns landing to the Lander. Under Q-A (a), the same with a placed `TradeDepot`; two depots pick the lower Id; demolishing it returns the role. |
| RR3 | **Depots and routes.** Ids: the colony's 1, then rivals in name-string order. Two injected rivals created in the reverse of their name order give the same Ids, routes and logged order as the forward order. Each route's A is depot 1 and its B the rival's; an unknown rival has no route, and its leg time is 0 as HEAD's is. |
| RR4 | **The endowment.** With `Debug_SetEndowment(0, Ice, 500)`, Ice's V, floor, ceiling and start price are half of today's exact integers (1,500, 375, 6,000 and 1,500 mcr/kg). One reprice by hand matches E4's update with the new V, using MX2's reference copied into the suite. Struct at 43 permille gives `5000 × 43 / 1000` = 215 exactly. Back at 1000, every slot of every market is bit-identical to a twin that never set a row. A row for region 1 changes nothing in region 0. E3's Earth price for Ice is unchanged throughout. |
| RR5 | **Era parity.** A paired 10-sol run, the depot pointed at a stockpile before the window in both twins: the landing building, the landed kg, every price and every balance identical (int64 and string equality). |
| RR6 | **Save.** A colony mid-trip, saved and loaded: the depot is the Lander again (the hook is not saved); under Q-A (a), a placed depot is the depot again. The routes and V are re-derived identically. Save, load, save is byte-identical, and **the file is byte-identical to the same colony saved by the gate before E7**: a recorded fixture, compared by `cmp`. |

**Mutations** (each built, run and restored by hash; each must fail the check named):
1. Landing still hard-coded to the first Lander: RR2.
2. The route's length kept in whole metres, with the leg time read from `LengthM / 1000.0`: RR1, at 33.33333 km.
3. Rival depots in FName index order: RR3, the reverse injection.
4. The endowment applied to E3's Earth price too: RR4, the Earth price check.
5. V scaled in `double` (`V × (Permille / 1000.0)`): RR4's Struct case. 5000 × 0.043 is 214.99999999999997 in double, which truncates to 214, not 215.
6. The depot rule picking the **highest** Id: RR2 (two depots), under Q-A (a); or the Lander chosen over a completed depot.

**`-scale`** (outside the battery): 1,000 `StepTrade` calls with three convoys out, and 1,000 `SyncRivalEconomies` with forced resyncs at 10 rivals. Within 0.05 ms of the pre-E7 figures. Every digest is identical.

---

## 9. Presentation

- **E7 adds no panel or button under Q-A (b).**
- V reads the convoy's home from `GetColonyDepotLocationCm(RegionId)`, a new const getter, instead of finding the Lander itself (V:4338-4341).
- Under Q-A (a):
  - the build menu lists **Trade depot** with the tooltip "Convoys unload here. Your first depot replaces the Lander as the trade end point." (Gate-D placeholder copy, a new row in `gate-d-wording.md`);
  - the building card says "Trade depot: convoys from your neighbours unload here". It names no neighbour, so an undiscovered one is never revealed (the rival cards and markers show available rivals only);
  - the placeholder mesh is a flat pad with a mast, from the existing basic shapes.
- E8 reads `GetDepots` and `GetRoutes` for its trade overlay.

---

## 10. Risks

1. **The seams are tested only by E7's suite until step 4 uses them.** A shape step 4 does not want is the main risk. The records carry exactly what T2 and T6 name (a region, an owner, a length, a kind), and nothing else.
2. **No saved route means T6 must add one.** That is T6's save change, stated there already ("the convoy list ... replacing today's single convoy fields").
3. **The endowment only scales V.** Rivals' production stays on their rows (T5 makes it grow). Q-B asks whether that is the right lever.
4. **The Lander keeps every non-trade role** (store, anchor, refunds, docks, orphaned cargo) until T1. A player will not notice. A reader of the code might think E7 "moved the Lander", and this design says it did not.
5. **E5 and E6 are not built.** E7 touches neither: E5's region-0 market and E6's name-keyed rules stay valid. If E7 builds before E5 or E6, nothing changes for them. If after, the deck's explicit `RegionId` reads (section 3) cover E5's and E6's new market readers too.
6. **A new DataTable asset.** `DT_Endowments` is imported from its CSV by the builder's usual data tooling. An empty table is a little ceremony for step 4's benefit; Q-B's (b) or (c) would change its columns.

## Deviations to log in `docs/design-decisions.md`

1. **No save change and no version bump.** Depots, routes and the endowment are derived from saved buildings and data rows (section 7). The battery bar tightens from "line-identical except the version lines" to fully line-identical.
2. **Only the Lander's trade-facing roles move to the depot** (landing goods, landing loot, the convoy's drawn start). The rest move with T1's colony record.
3. **A route's length is the row's distance, verbatim,** not integer metres, until T6 draws routes on the map.
4. **The deck's market reads pass the rival's region explicitly,** instead of the default 0.

## Director questions

The numbers come from the shipped data and E4's own model (E4's REVISION R5, reproduced to the cent by the E5 and E6 reviews). In it, Zarya's Ice sits at its floor (0.25 V) from about sol 60 on, and the standing trade earns 827.9 cr a round trip.

**Q-A. Is the trade depot a building the player places now, or a role the Lander holds until step 4 founds a second settlement?**
- **(a) A `TradeDepot` building now.**
  - One more item in the build menu: a placeholder pad of 400 kg Struct, no power.
  - Convoys and loot unload there, and robots haul the goods on to stockpiles.
  - **Nothing else changes in one region:** Zarya's legs stay 2.0 sols (120 km) and Meridian's 3.33 (200 km), and every price and credit is the same.
  - The only gain is where goods are dropped: a depot beside the stockpiles shortens each haul by the distance from the Lander, at most about 150 m on today's buildable flat.
  - The click test gains a button; the gate takes about one more session for the row, the pad and the card.
- **(b) (recommended) The role only.** The Lander is depot 1, and `FindColonyDepotBuilding` is where step 4 plugs in.
  - Nothing new for the player, as the plan's row says ("Nothing new for the player. Step 4 plugs in.").
  - The building ships with T4, where a second settlement needs a depot of its own, and its placement then also sets the route's length.

**Q-B. What does a region's endowment change?**

The worked case is a region where Ice is plentiful (500 permille), shown at Zarya.
- **(a) (recommended) Its market's anchor value per resource** (a permille on V).
  - Ice's start price falls from 3.000 to 1.500 cr/kg, its floor from 0.750 to 0.375 and its ceiling from 12 to 6.
  - Ice still sits at its floor, because E4's update scales with V. So the standing trade buys its 150 kg of Ice for 56.25 cr instead of 112.50 and earns **884.2 cr a round trip instead of 827.9 (+6.8%)**.
  - It is direct, it is one integer per (region, resource), and it never touches Earth's prices.
- **(b) Its rivals' production per resource** (a permille on each rival's `Produce` there).
  - At 500 permille Zarya makes 200 kg of Ice a sol instead of 400. It still uses 150, and the colony buys 37.5 a sol.
  - So the store keeps filling (+12.5 kg a sol instead of +212.5), Ice stays at its 0.750 floor, and **the price does not move at all** with today's data.
  - It is indirect: it shows only once a store runs short.
- **(c) Both.** It is the most expressive, but two levers per resource for step 4's data to balance.

---

# LEAD REVIEW (2026-10-25, not independent)

**Not an independent review.** The session's rules allow no subagent without an explicit SWARM in the prompt, so no independent reviewer ran. The lead re-read every anchor and claim above against HEAD, trying to break each. A professor pass before the build is still advisable (see the verdict).

**Verdict: CLEARED WITH CONDITIONS** (the lead's own; see the conditions at the end).

## What holds (checked against HEAD)

- **Every anchor resolves to the named code.** Checked: W:546, :9657-9662 and :9762-9772; M:286, :297-300, :661, :1136, :1203-1218 and :1269; V:4193-4204 and :4338-4341; H:543, :1560-1597, :3827, :3835 and :3868; `RHRows.h:405` and `:412-413`.
- **The leg time is bit-identical.** `DistanceKm` is a `float`, and HEAD divides its promotion to double. `(double)float` is exact, so `R.LengthKm / Speed` is the same IEEE operation on the same operands.
- **The landing rule is HEAD's.** Both loops pick the first Lander with `!bUnderConstruction` in building order.
  - The visualizer's loop (V:4338-4341) ignores construction. The Lander is placed instantly (W:546, `bInstant` true) and cannot be demolished (W:10504), so it is never under construction and the two agree. A test-placed second Lander would not be the first (`Debug_Showcase` skips the Lander, W:12207).
- **V is the only anchor input.** `MarketProduceAndReprice` reads `Mk.VMcr` (M:593). The band is set from V at M:299-300 and the start price at M:312. The update's terms are relative, so Q-B (a)'s "the path scales with V" holds, except for the 1 mcr minimum step.
- **Q-B's numbers.**
  - Zarya's data (`RH_Rivals.csv`): Produce Ice 400, Consume Ice 150, standing lots Ice 150 / Struct 100. The round trip is 4 sols (120 km at 60 km a sol, each way).
  - (a): 150 × (0.750 − 0.375) = 56.25 cr more a run: 827.9 → 884.15 (+6.8%).
  - (b): 400 × 0.5 − 150 − 150/4 = +12.5 kg a sol, so the store still grows.

## Defects found and fixed in the REVISION

1. **The 1 mcr minimum step breaks exact scaling.** E4's step is `max(1, |P × StepBp| / 10000)` mcr. At small prices the 1 mcr floor is a larger share of a halved price. The path scales only approximately at prices under about 20 mcr/kg, where 5% < 1 mcr. Ice at 375 is far above that, so Q-B's figure stands. RR4 must still not assert a scaled *path*, only a scaled V and band and one hand reprice. → R1.
2. **RR6's "byte-identical to the gate before E7" needs a fixture made before E7 is built.** If the fixture is recorded from E7's own build, the check proves nothing. → R2.
3. **`Debug_SetEndowment` bumps the rivals' generation, which E4's MX2 "rival order" check also watches.** A suite that sets an endowment and then reads E4's generation-sensitive state could see an extra resync. Only `-regionready` calls the hook, and E4's suites never do, so the risk is in RR4 alone. RR4 must re-read the market after the resync, not before. → R3.
4. **The draft's mutation 5 could not fail.** It used Ice at 333 permille and claimed the `double` product truncates differently. It does not: 3000 × 0.333 rounds to exactly 999.0 in IEEE, the integer answer. An enumeration over the shipped Earth values and permilles 1-2,000 found 333 cases that do differ. The first for Struct is 43 permille: 214.99999999999997 against 215. RR4 and mutation 5 now use it (fixed in place above, and recorded here).
5. **Under Q-A (a), "not a store" means a TradeDepot's `OutputKg` is hauled away like a producer's,** which needs `HaulLoadMinKg` worth of goods (W:1244). A 50 kg loot line could then sit in the depot until more arrives. That is harmless, but the card should not claim "unloads here and moves to the stockpiles" without the minimum. → R4 (wording only).

**Read but not a defect:** at 500 permille, Ice sits on its floor whichever V applies. The anchor term reads (375 − 1,500) / 1,500 = (750 − 3,000) / 3,000 = −7,500 bp, the flow and store terms do not read V (M:610-612), and the clamp at the floor is exact. So Q-B (a)'s +56.25 cr a run is exact, not approximate.

## Conditions

1. An independent professor pass on sections 1, 2 and 7, the derived-only save, before the build. It is the plan's largest deviation.
2. The director's answers to Q-A and Q-B.

---

# REVISION (2026-10-25)

This section replaces the design wherever they differ.

**R1. RR4's endowment checks** assert, at 500 permille (Ice) and 43 permille (Struct):
- V, the floor, the ceiling and the start price exactly;
- **one** reprice by hand, against E4's update with the new V (MX2's reference copied into the suite).

They do **not** assert that a whole price path scales: E4's 1 mcr minimum step makes the scaling inexact below about 20 mcr/kg. Q-B's figure is unaffected: Ice at its 375 mcr floor is far above that.

**R2. RR6's fixture is recorded before E7 is built.** The builder saves `regionready_pre.sav` from the pre-E7 binary (the colony RR6 builds, mid-trip), commits it beside the suite's other fixtures, and RR6 compares E7's save of the same colony to it with `cmp`. If a later gate changes the file format, the fixture is re-recorded by that gate's builder, as E4 re-cut the checkpoints.

**R3. RR4 reads after the resync.** After `Debug_SetEndowment`, RR4 calls a read that syncs (`GetMarketPriceMcr` does, through `FindMarket`) before asserting, and it restores the endowment to none at the end, so the next check starts from HEAD's markets.

**R4. The depot card's wording under Q-A (a)** reads "Convoys unload here. Robots move the goods on to your stockpiles." It promises no timing, because a small load waits for `HaulLoadMinKg` like any output.

# INDEPENDENT REVIEW (2026-10-25)

Reviewer note: an independent professor pass. All code was read at HEAD `60e892a` with `git show HEAD:` only. Source at HEAD is the same as at the design's base, `6119e7d`.

**Verdict: CLEARED WITH CONDITIONS**

**What holds.** Under Q-A (b) the core is sound:
- the leg time is bit-identical;
- landing picks the same building;
- V is the same integer;
- nothing new has to be saved.

**What does not hold:**
- the one market change, the kept-price clamp, turns E4's `-market` suite red;
- RR4 cannot pass as written;
- both director questions misstate their consequences;
- the route seam has a shape step 4 cannot use.

Every item is a fix to the design's text. None needs a different architecture.

## Required changes

All ten are conditions; none is blocking. Conditions 3 and 4 must be fixed before the director is asked. The other eight must be fixed before the build.

1. **The kept-price clamp breaks E4's MX9 check and the battery bar.**
   - **Defect.** Section 4 clamps a kept price inside `BuildMarketState`, and the load path runs through that function:
     - `ReadMarketBlock` builds its markets from the file with full-size arrays (M:1532-1567), so `bKeep` holds (M:286).
     - It then calls `SyncRivalEconomies(false, true)` (M:1668).
     - Only after that does its own clamp loop run (M:1679-1691). That loop is the only place that logs "region %d's price of %s is ... outside [...]; clamped".
   - **Effect.** A doctored price is clamped silently first, and the Warning never fires.
     - MX9's "price band" repair requires a Warning that contains "outside" (`RHSimMarketSuite.cpp:1443`, `:1454`), so MX9 fails.
     - The two Warning lines at `Saved/baselines/battery_post_e4/market.log:715-716`, and their repeat at `:1419-1420`, disappear.
   - The design's "a kept price is always inside it already" is false on the load path.
   - **Change:** either option keeps the load path's own Warning.
     - Drop the clamp. The next reprice clamps anyway (M:623).
     - Or clamp only where the old market has a valid `VMcr` that differs from the new V. Markets read from a file have no `VMcr` (M:1534-1538).

2. **RR4 cannot pass as written.**
   - **Defect 1: the start price.** `Debug_SetEndowment` acts on a live market, and a resync keeps that market's prices (M:302-309).
     - After `Debug_SetEndowment(0, Ice, 500)`, Ice still costs 3,000 mcr (or whatever it cost before). RR4 asserts a start price of 1,500.
     - A start price of V' shows only in a fresh market (M:312).
   - **Defect 2: the restore check.** At 43 permille, Struct's band is [53, 860] mcr, and a kept Struct price near 5,000 sits outside it.
     - If a clamp or a roll moves that price, "back at 1000, every slot ... bit-identical to a twin" fails.
   - **Change:**
     - Assert the start price in a fresh market. For example, inject an economy rival into region 1 after `Debug_SetEndowment(1, Ice, 500)`.
     - Read V, the floor and the ceiling through getters (condition 6).
     - Run the restore check with no roll and no clamp inside the window, or in a world of its own.
     - State what a mid-game endowment change does to a kept price: it is not rescaled.

3. **Q-B (b) is modelled wrongly.**
   - **How I checked.** I wrote an integer model of E4's update (M:560-624). It reproduces E4's own run figures: 50, 566, 761 and 827.9 cr (sols 0, 20, 40 and steady).
   - **What the model shows.** With Zarya's Ice production at 500 permille (200 kg a sol), Ice never reaches its floor.
     - Ice is 2.438 cr/kg at sol 60 and 2.348 at sol 100.
     - The flow term falls from +3,793 bp to +526 / −243 bp, and Zarya's store holds near 13,200 of 20,000 kg instead of filling.
     - A run earns 573.6 cr at sol 60 and 588.2 at sol 100. That is −29%, not 827.9.
   - **Two claims are false:**
     - "the price does not move at all";
     - "it shows only once a store runs short".

     Lever (b) acts at once, through E4's flow and store terms.
   - **The worked case is inverted.** 500 on V means Ice is plentiful. 500 on production means Ice is scarce. The plentiful case under (b) is 2,000 permille (see the director questions).
   - The lead review checked whether the store grows ("+12.5 kg a sol, so the store still grows"). It did not check the price.
   - **Change:** restate (b) with like-for-like cases and modelled numbers, and argue the recommendation again.

4. **Q-A (a) states the wrong consequences, and (a) needs a version fence.**
   - **Where a depot's goods go.** A `TradeDepot` that is not a store sends its output to the nearest consumer with room (W:1262-1286).
     - It sends output to a store only when `MayOverflowToStore` allows. That answers false while any machine on the level takes the resource (W:12391-12409).
     - Water Plants take Ice (`MeltIce`, Ice:50 to Water:45, `RH_Recipes.csv`).
     - Forges take Struct (`MakeShielding`, `MakeHabSegment`).
   - **What HEAD does instead.** Ice that lands in the Lander's `OutputKg` is never hauled:
     - W:1244 skips the Lander;
     - the store-to-hopper leg reads `InputKg` only, and only for a construction shortage (W:1320-1347);
     - W:1288-1290 records that ice sat in the Lander "for good".
   - **So, under (a):**
     - traded Ice becomes drinking water;
     - Struct may fill a Forge hopper first;
     - each landing of at least `HaulLoadMinKg` adds haul work that HEAD does not have. `HaulLoadMinKg` is max(25, 500 / 5) = 100 kg (W:108, `PilesCapacity_kg`).
   - **Three statements are wrong:**
     - "nothing else changes in one region" (wrong in kind);
     - "shortens each haul ... about 150 m" (wrong in kind);
     - R4's card text, "Robots move the goods on to your stockpiles" (wrong for Ice).
   - **The save.** Under (a), an E7 save can hold a building that a v55 binary has no row for. This project bumps the version as a fence in that case, even when no field is added. W:37 records two such bumps:
     - v40: "the bump is a fence";
     - v43: "the fence keeps a v42 save from loading".

     So section 7's "no version bump" holds only under Q-A (b).
   - **Change:** restate Q-A (a) with the numbers below, fix R4, and make deviation 1 conditional on Q-A (b).

5. **The route seam has a shape step 4 cannot use.**
   - **The two depot rules disagree.**
     - A route's `DepotA` is "the colony's depot in the rival's region" (sections 2 and 3).
     - Landing uses `FindColonyDepotBuilding(GetColonyRegionId())`, which is in the colony's own region (section 1, item 1).

     They agree only while every rival sits in region 0. T2 gives rivals home regions of their own (`territories-plan.md:47`), and T6 links depots across the map (`:51`). From then on, a convoy would leave from a depot that does not exist and land at another one.
   - **The Ids are positions, not identities.**
     - Rival depot Ids follow name order, and a route's Id is the rival depot's Id − 1.
     - So a new data row renumbers every depot and route after it. A rival named "Aurora" is one example.
     - T2's colony depots come after the rivals, so they are renumbered too.
     - T4's second settlement, trading with Zarya, needs a second route to one rival depot. The rule "Id = depot − 1" cannot express that.
     - "Stable across saves" holds only while the rival table does not change.
   - **Change:**
     - `DepotA` is the colony's depot in `GetColonyRegionId()`, the same depot landing uses.
     - `GetRouteFor` answers true whenever the rival row exists, whatever buildings stand, as HEAD's leg does (W:9659-9661). A known rival's leg can then never become 0.
     - `GetColonyDepotLocationCm` answers the origin when there is no depot, as V does (V:4338).
     - Ids are display ordinals only: never saved and never used as a key.
     - Drop "route Id = rival depot Id − 1". Key a route by (DepotA, DepotB), or by the rival's name.

6. **Name the accessors and hooks the tests need.**
   - **What the suites cannot reach.**
     - RR1 calls `ConvoyLegSolsFor`, which is private (H:4028; the private section starts at H:2763).
     - RR1 and RR4 read every slot's V, floor and ceiling, which live in the private `FRHMarket` (H:3836-3842).
     - Suites are free functions (`RHRunMarketSuite`, `RHSimMarketSuite.cpp:188`), so they can reach neither.
   - **Other needs.**
     - E8 reads `ConvoyLegSolsFor` too (E8 design, section 2.6).
     - RR0 checks that "a row out of range reads 1000 with one Warning". That needs a hook that injects a row, and a validation pass after world start: section 4 logs this Warning at world start only.
   - **Change:** list the public const getters and the row hook:
     - the leg time in sols, per rival;
     - V, the floor and the ceiling, per slot and region.

7. **Mutation 3 can survive RR3.**
   - **Defect.** An FName's index follows the order in which the name was first created. If the forward injection runs first in the same process, the reverse case reuses those FNames. Index order then equals name order, and the mutation passes.
   - **Change:**
     - The reverse case must be the first to create its names.
     - Add a pair that differs only in case, for example `aRival` against `Zarya`. This pins case-sensitive ordinal order (M:26) against FName's case-insensitive compare.

8. **The endowment table's data shape.**
   - **The key column.** UE's CSV import takes column 0 as the row name (Engine `DataTableCSV.cpp:149-150`), and every repo CSV starts with `Name` (`RH_Rivals.csv:1`). With the design's columns `RegionId, Resource, AnchorPermille`:
     - RegionId becomes the row name;
     - the rows of one region collide;
     - the `RegionId` field is never filled.
   - **An empty table.**
     - The project's tool refuses to save a table that fills to 0 rows: "FAIL ... parsed to 0 rows, NOT saved" (`scripts/unreal/rh_sync_datatables.py:81`).
     - `RH_CREATE` saves the empty asset when it creates it (`:49`), but every full sync after that prints FAIL for it.
     - UE 5.8 itself accepts a CSV that holds only a header (`DataTableCSV.cpp:143-147`).
   - **Change:**
     - Put `Name` first (for example `R0_Ice`).
     - Treat a duplicate (region, resource) row as a Warning, read as 1000.
     - Either create the table with `RH_CREATE` and record the standing FAIL line, or ship one identity row at 1000.
     - Leave the "Definitions loaded:" line unchanged (`RHDefinitionsSubsystem.cpp:30`).

9. **RR6's fixture from the binary before E7.**
   - **No fixture exists.** The repo commits no save fixture: there is no `.sav` in the tree at HEAD. Checkpoints are built into the untracked `Saved/SaveGames` (`scripts/unreal/rh_checkpoints.sh`).
   - **What R2 would need.** A build without E7 that runs RR6's colony using no E7 hook.
   - **A recorded fixture loses its value.** Each later format gate re-records it, and after the first re-record it proves nothing.
   - **Change:** make the `cmp` between the saves from before and after E7 a one-time build step, recorded in `design-decisions.md` as the mutation runs are. RR6 keeps save, load, save.

10. **Text fixes in the coverage.**
    - `ClearConvoy`'s no-market fallback does have a region (`E->RegionId`, M:645-646, `:657-662`). Say whether the endowment applies there.
    - `RH.Market` (`RHCheats.cpp:1120-1136`) has no rival to take a region from. It needs:
      - a loop over the markets;
      - a getter for each market's RegionId by index. Only `GetMarketRegionCount` exists (H:1567).
    - `ClosedRoutes` is keyed by rival (M:887, `:1236`). It is a route structure that section 3 does not name.
    - The Lander table mislabels two lines:
      - `RHSimRovers.cpp:261-268` is a rover's fallback for a lost load;
      - `:305-314` is the rover's deploy node.
    - It also omits three roles based on the origin. All are non-trade and belong to T1:
      - W:1009, the pad search from the origin;
      - W:1866, bearings measured from the origin;
      - W:2469, manifest robots spawning at (30 m, −30 m).
    - The step-2 fixes, E5 and E6 all build before E7, and E6 changes `ResolveCovert` (E6 design, section 4.2). The builder must re-anchor every line number at its own HEAD and repeat the Lander search.

## Checked and sound

- **The leg time is bit-identical.**
  - `DistanceKm` is a `float` (`RHRows.h:405`) and `ConvoySpeedKmPerSol` is a `double` (H:3914).
  - `(double)` of a float is exact, so the route reproduces HEAD's W:9657-9662 exactly.
  - Zarya's leg is 2.0 sols and Meridian's is 3.3333333333333335.
  - Mutation 2 can fail. 33.33333 km as a float is 33.33332824707031. Kept in metres it becomes 33.333 km, so the leg reads 0.55555 instead of 0.5555554707845052.
- **Landing under Q-A (b) picks the same building.**
  - M:1210-1218 and W:9765-9773 both pick the first Lander that is not under construction.
  - The Lander is placed instantly (W:546) and cannot be demolished (W:10504).
  - Buildings stay in ascending Id order (W:10822), so "first in building order" and "lower Id" agree.
- **The endowment arithmetic is safe.**
  - At 1000 permille, V is exact in int64.
  - The largest Earth value is 900,000 mcr (HabSegment). At 100,000 permille with the clamp maximum of 1,000,000 bp, the largest product is 9e13, far inside int64.
- **Mutations 4 and 5 can fail.**
  - Mutation 5: in `double`, 5000 × (43 / 1000.0) is 214.99999999999997, which truncates to 214.
  - Mutation 4: E3's price is not clamped: V × Mpm / 1000 − L (`RHSimExport.cpp:167-174`), where L = 3,000 mcr (`:160-165`). At M = 1.0 Ice's Earth price is 0 mcr, and −1,500 under the mutation.
- **Q-B (a)'s figures reproduce.**
  - The run earns 884.15 cr against 827.9 (+56.25, +6.8%), and the floor is reached on the same sol (48).
  - One correction: section 4 says "the whole price path scales". It scales only approximately in the integers. At sol 20 the price is 773 mcr against half of HEAD's, 769.5. The cause is the floor division at M:620. R1 already avoids asserting the path.
- **Nothing new needs saving under Q-A (b).**
  - `VMcr`, `FloorMcr` and `CeilMcr` are derived and never saved (H:3840-3842).
  - The market block already writes one market per region (M:1294-1309).
  - Gates that derive their state have shipped without a bump before (`design-decisions.md:688`, `:1149`).
- **Era parity holds under Q-A (b).**
  - `StepTrade` sits at the same index in both spines (W:687, W:859).
  - Covert operations resolve through `StepUplink` in both spines.
  - The depot is a pure scan of the buildings.
- **The playtest cannot move.** `RHPlaytest.cpp` uses no Convoy, MarketOrder or Covert verb.
- **All three trade-facing Lander roles are found:**
  - convoy goods (M:1203-1219);
  - covert loot (W:9762-9775);
  - the convoy's drawn start (V:4337-4342).

  Two neighbouring cases are not Lander roles:
  - Earth's cargo goes to the pool (W:2456-2458), not to the Lander;
  - outbound lots drain every building in Id order (W:9602-9634).
- **The sim keys every market lookup by region:**
  - the clearing (M:646);
  - the reservation (M:830-831);
  - the reprice (M:569);
  - the sync (M:274-334).

  Only three readers rely on the default region 0:
  - the command deck (`RHCommandDeckPanels.cpp:2534`);
  - `RH.Market`;
  - `-scale` (`RHSimCommandlet.cpp:17009`).

## Director questions

All numbers come from my integer model of M:560-624, which reproduces E4's run figures. The model assumes:
- the standing trade runs every 4 sols, clearing on sols 0, 4, ... 96 (25 runs);
- Struct's price path is unchanged (steady at 9.404 cr/kg).

A run's net is 100 kg of Struct sold, less 150 kg of Ice bought.

**Q-A. Is the trade depot a building the player places now (a), or a role the Lander keeps (b)?**

- **(a) A `TradeDepot` building.**
  - Traded Ice is hauled to a Water Plant and melted, instead of sitting in the Lander.
    - At HEAD's standing trade that is 150 / 4 = 37.5 kg of Ice a sol.
    - It becomes 37.5 × 0.9 = 33.75 kg of fresh water a sol, enough for about 16.9 colonists at 2.0 kg each.
    - Potability gains 33.75 × 0.0008 = 0.027 a sol, against a decay of 0.08 a sol.
  - A clean covert run's loot is 150 × 0.5 = 75 kg of Ice. That is below the 100 kg haul minimum, so it waits at the depot.
  - Struct may fill a Forge hopper first.
  - Prices, credits and leg times are unchanged.
  - The build adds:
    - one menu button;
    - a version bump as a fence (condition 4);
    - the depot's own cost (400 kg of Struct, the design's figure).
- **(b) The Lander keeps the role.** Nothing changes. Traded Ice stays in the Lander as holdings only. It counts toward the quota, sales and convoy lots, but it is never melted.

**Q-B. What does a region's endowment change?**

HEAD's baseline:
- Ice reaches its 0.750 cr/kg floor from sol 48;
- a run earns 827.9 cr;
- the first 25 runs spend 4,355.40 cr on Ice.

- **(a) A permille on V. At 500 (Ice plentiful):**
  - Ice starts at 1.500 cr/kg, with a floor of 0.375 and a ceiling of 6.000;
  - it still reaches the floor from sol 48;
  - a run earns 884.15 cr (+56.25, +6.8%);
  - the first 25 runs spend 2,181.45 cr on Ice (−2,173.95).
- **(b) A permille on rivals' production.**
  - **At 2,000 (plentiful: Zarya makes 800 kg a sol):**
    - Ice reaches the floor from sol 28;
    - from sol 60 a run earns 827.9 cr, as at HEAD;
    - the first 25 runs spend 3,877.95 cr on Ice (−477.45).
  - **At 500 (scarce: 200 kg a sol):**
    - Ice never reaches the floor. It is 2.438 cr/kg at sol 60 and 2.348 at sol 100;
    - a run earns 573.6 to 588.2 cr (−29%);
    - the first 25 runs spend 9,429.45 cr on Ice (+5,074.05).
- **(c) Both levers.** Two levers per resource for step 4's data to balance.

## Not verified

- The claim that the Lander is "at most about 150 m" from today's buildable flat.
- Section 6's "era refuses construction".
- Whether a v55 binary misbehaves when it loads a `TradeDepot` building. The fence argument rests on the project's precedent, not on a failure I traced.
- Whether a missing DT asset makes `TryLoad` log a line in every run.
- The `-scale` timings and the click test.
- FName index allocation order. It is UE engine behaviour, which I did not trace in the engine source.
- Cumulative totals from E5 and E6. I checked per-run figures only: E5's 17,540 cr by sol 99 uses a different clearing schedule.

I ran nothing in the repo and built nothing.


# DIRECTOR ANSWER (2026-10-26)

- **Q-A (b):** the trade depot is NOT a building in E7. The Lander keeps every depot role until step 4's regions arrive. Traded ice stays as holdings in the Lander and is never melted. No new menu button, no depot cost, no version fence for it.
- **Q-B (b):** a region's endowment is a permille on the rivals' PRODUCTION, not on V. Modelled at Zarya: 2,000 (plentiful, 800 kg a sol) brings Ice to its floor from sol 28; 500 (scarce, 200 kg a sol) never reaches the floor, Ice sits at about 2.35-2.44 cr/kg and a run earns 574-588 cr (-29%).


# REVISION 2 (2026-10-26, after the independent review)

This section replaces the design, the LEAD REVIEW's fixes and the first REVISION wherever they differ. The authority order is: the DIRECTOR ANSWER, then this section, then REVISION (R1-R3 only; R4 is struck), then the INDEPENDENT REVIEW, then the design.

**Code read at HEAD `1f52e3d`** with `git show HEAD:` only. The step-2 fixes (`1505941`) landed in code after the review read `60e892a`, so **W's and H's line numbers in the sections above are stale**; M's and V's are not. Every anchor below is at `1f52e3d`. The builder re-anchors again at its own HEAD, because E5, E6, D6e and D7 build first (the handoff's queue), and E6 rewrites `ResolveCovert` (its 4.2).

Re-anchors used here: `ConvoyLegSolsFor` W:9724-9729 (was :9657); the covert loot loop W:9831-9841, its Lander test W:9834 (was :9762-9772); `NAME_Lander` W:19; the Lander placed W:546; `HaulLoadMinKg` W:108; the manifest spawn at (30 m, −30 m) W:2469; the spines' `StepTrade` W:687 and W:859, `StepUplink` W:674 and W:846; the save header W:13524-13527; the tail frames W:13928-13933; the strict version check W:13968; the tail reads W:14740-14743. H: `FRHConvoy` H:550-565; the public market section H:1560-1616; the convoy getters H:1748-1755; `private:` H:2777; `FRHRivalEconomy` H:3841-3849; `FRHMarket` H:3850-3860; `ConvoySpeedKmPerSol` H:3931; `ConvoyLegSolsFor` H:4045.

## 1. The two answers, applied

**Q-A (b): the depot is a role, not a building.** Struck from the design: the `TradeDepot` row, button, tooltip, card, mesh and fence; section 1's "(a)" rule; section 5's two "Under Q-A (a)" bullets; section 6's depot-completion paragraph; the "(a)" clauses of RR2 and mutation 6; section 9's three "(a)" bullets; risk 6's "(b) or (c) would change its columns"; **R4** (there is no card). Nothing player-facing is added, so no Gate-D wording row either.

What stays: **one rule in one function** gives the colony's depot building in a region, and the Lander is that building in region 0. The Lander keeps every other role (store, anchor, refunds, docks, orphaned cargo; the origin-based ones at W:1009's pad search, the bearings from the origin and W:2469's spawn) until T1. Traded Ice stays in the Lander's `OutputKg` as holdings, as at HEAD (W:1244 skips the Lander), and is never melted.

**Q-B (b): the endowment is a permille on the rivals' production.** Section 4 is replaced by section 4 below. V, the floor, the ceiling, the start price and E3's Earth price are not touched by E7 at all. The kept-price clamp is struck (section 2).

**The route seam keeps its two endpoints**, with the Lander as the colony-side end for both colony-side events, departure (drawn) and landing, until T4 gives other regions depots of their own and T6 draws routes on the map (section 5).

**The save.** The design's section 7 ("nothing new") is replaced by section 8: E7 takes the next free version and writes one framed block at the end of the file, as every gate since v54 has. With one region the block is 28 bytes of ceremony; what it saves is the seam T2 and T4 fill in.

## 2. Condition 1: the kept-price clamp is dropped

Under Q-B (b) E7 never changes V, so the band never moves and there is nothing to clamp. `BuildMarketState`'s market part (M:274-333) is **untouched**: the V line M:297-300, the kept prices M:302-309 and the start price M:312 are HEAD's bytes. The load path's own clamp loop (M:1678-1690) is still the only clamp, so its Warning (M:1686) still fires for MX9's doctored price band (`RHSimMarketSuite.cpp:1454`), and the four Warning lines at `market.log:715-716` and `:1419-1420` stay.

E7's only change on the sync path is one line in `EvaluateRivalRow`, the Produce grams at M:217 (section 4). At 1000 permille it computes the same integer, so every market, store, tally, reservation, clearing and price in `-market` is HEAD's.

**`-market` stays line-identical except these lines**, all of which every version bump moves:
- `RHSimMarketSuite.cpp:1316-1321`, MX9's shape line: `Ver == 55` and the text "MX9 v55:" become E7's version;
- `:1374-1381`, "MX9 a v54 file is refused": `V54 = 54` and `"/55"` become the version before E7 and E7's;
- `:1359`, "save, load, save is byte-identical (12173 bytes)": +28 bytes (section 8; `diff_battery.py:10` masks this one on its own);
- `:564`, MX1's two band lines: "the saves (11761 bytes)" and "(11740 bytes)" are +28 each (masked by the script too); "up to the market block (8717 bytes)" and "(8696 bytes)" do **not** move, because E7's frame sits after E4's.

E4's 47 "LoadColony v55:" Warnings in `market.log` are E4's text about E4's block; E7 does not edit them.

## 3. Condition 2: RR4 rewritten so it can pass

Under (b) the review's two defects do not arise, and RR4 is rebuilt on MX2's recipe (one roll by hand from a set state) instead of on start prices:
- **No start-price check.** The endowment does not touch V, so a fresh market starts at V with or without a row. RR4 asserts instead that `GetMarketAnchorMcr`, `GetMarketFloorMcr` and `GetMarketCeilMcr` are HEAD's integers for every slot with every endowment set (Ice 3,000 / 750 / 12,000; Struct 5,000 / 1,250 / 20,000 mcr).
- **The restore check has no roll inside it.** `BuildMarketState` keeps `PriceMcr`, both rings, `RingFill`, `RingHead` (M:302-309) and every rival's `StoreG` (M:250-253); it recomputes only `ProduceG`, `ProdSumG` and their kin. So `Debug_SetEndowment(0, Ice, 500)` followed by `Debug_ClearEndowments()` with no roll between leaves `GetMarketBlockBytes()`'s bytes identical to before, and every `GetRivalProduceG` back at kg × 1000. That is the restore check.
- **Reads come after the resync** (R3 stands): every read in RR4 goes through a getter that calls `FindMarket` or `FindRivalEconomy`, which sync lazily (M:357-361).
- **A mid-game endowment change rescales nothing.** From the next roll the rivals make the new grams; prices, stores and the tally keep their values. RR4 states this and pins it (the restore check above is the proof).

The numbers RR4 asserts are in section 4 and in the test table.

## 4. Condition 3: Q-B (b), modelled and written as the column's semantics

### 4.1 The column

**`ProducePermille`**, one integer per (RegionId, Resource): *every rival whose home region is RegionId makes `Produce_kg × ProducePermille` grams of that resource a sol, instead of `Produce_kg × 1000`.*
- 1000 is the row as written. 2,000 doubles a region's output of that resource; 500 halves it.
- **Exact by construction.** HEAD's M:217 is `ProduceG = P.Value * 1000` (grams from whole kg). E7's is `ProduceG = P.Value * Permille`, int64, **no division anywhere**, so every permille is exact and 1000 is bit-identical.
- **Production only.** `Consume` (M:218), `StoreStart`, `StoreCap`, both standing lots, `Trades`, `CapG`, V, the band, E3's price (`RHSimExport.cpp:167-174`) and `ClearConvoy`'s no-market fallback (M:661, the Earth value) are untouched.
- **Where it applies: one line.** `EvaluateRivalRow` (M:106), at M:217, reads `Defs->GetProducePermille(Row.RegionId, P.Key)`. `ProdSumG` (M:327) follows automatically, so the flow term (M:607-612) sees the new grams on the next roll, as the store (M:575-581) does. No other reader of `ProduceG` exists outside M (checked with `git grep` at HEAD).
- **Range.** A row's `ProducePermille` must be a whole number in **1..100,000**. The arithmetic is safe at the top of the range: `Produce` is at most `MktMaxRateKg` = 1,000,000 kg (M:20, M:163), so `ProduceG` is at most 10^11 g; `ProdSumG × W` with 64 rows and W ≤ 8 is under 10^14; times 10^4 in the flow term is under 10^18, inside int64. `StoreG + ProduceG` is under 10^12.
- **Which region.** The rival row's `RegionId` (`RHRows.h:413`). Both shipped rows are region 0, so `(0, Ice, p)` scales Zarya's Ice and `(0, Food, p)` Meridian's Food; a row for region 1 touches nothing today.

### 4.2 The model, reproduced

The review's integer model of M:560-624 with the standing trade clearing every 4 sols was rebuilt for this revision and reproduces its figures to the cent (and MX2's Set A Ice reprice, 3,000 → 2,850):

| | HEAD (1000) | 2,000 (Ice plentiful: Zarya makes 800 kg a sol) | 500 (Ice scarce: 200 kg a sol) |
|---|---|---|---|
| Ice at its 0.750 floor from | sol 48 | **sol 28** | never |
| Ice at sol 60 / sol 100 | 0.750 / 0.750 | 0.750 / 0.750 | **2.438 / 2.348** cr/kg |
| A run's net at sol 0 / 20 / 40 / 60 / 100 | 50.0 / 565.5 / 761.0 / 826.8 / 827.9 | 50.0 / 634.1 / 783.0 / 826.8 / 827.9 | 50.0 / 408.8 / 521.9 / **573.6 / 588.2** cr |
| The first 25 runs spend on Ice | 4,355.40 | 3,877.95 (−477.45) | 9,429.45 (+5,074.05) cr |
| Zarya's Ice store at sol 100 | 20,000 (full) | 20,000 | 13,150 kg |

Struct's path is the same in all three (9,404 mcr/kg at sol 100): the endowment row names Ice only. The lever acts **at once**, through both the flow term (`ProdSumG`) and the store term (the store's growth), which is why the design's "(b) ... the price does not move at all" was wrong and is withdrawn.

### 4.3 One roll by hand (RR4's integers)

From MX2's Set A fixture for Ice (`RHSimMarketSuite.cpp:612`: Zarya's store 12,000 kg, every tally entry 0 with the ring full, price 3,000 mcr), one `Debug_RollMarket`:

| ProducePermille | Zarya's Ice `ProduceG` | store after | FlowBp | FillBp / StoreBp | Pressure | StepBp | price after |
|---|---|---|---|---|---|---|---|
| 1000 | 400,000 g | 12,250 kg | 4,545 | 6,125 / 2,250 | 2,947 | −500 (clamped) | **2,850** (MX2's own figure) |
| 500 | 200,000 g | 12,050 kg | 1,428 | 6,025 / 2,050 | 1,329 | −265 | **2,921** |
| 2,000 | 800,000 g | 12,650 kg | 6,842 | 6,325 / 2,650 | 4,216 | −500 (clamped) | **2,850** (the store tells it from HEAD) |
| 43 | 17,200 g | 11,867 kg | −6,640 | 5,933 / 1,866 | −2,760 | +500 (clamped) | **3,150** |

(The 2,000 case saturates the 5% step, so its price equals HEAD's; its store, 12,650 against 12,250 kg, is the pin. The 43 case is a shortage: the price rises.)

### 4.4 The director's question, restated for the record

Q-B is answered: (b). The design's Q-B text is superseded by 4.2. Its (a) figures (884.15 cr, −2,173.95 cr) remain correct for a lever E7 does not build.

## 5. Condition 4: Q-A (b) applied; what still needs a fence

- **Removed:** everything listed in section 1. No build-menu item, no cost, no mesh, no card, no `-dayshift` or click-test change.
- **No fence is needed for a building.** A save written by E7 holds no building a pre-E7 binary lacks a row for.
- **The version bump E7 takes is for its own block** (section 8), not a fence. The project's strict check (W:13968, `Version != RHSaveVersion`) then refuses pre-E7 files, as every bump does; MX9's "previous version is refused" line moves with it.
- The design's Q-A text, R4 and the lead review's defect 5 are superseded. The review's Q-A (a) numbers (33.75 kg of water a sol, the 100 kg haul minimum) describe a lever E7 does not build.

## 6. Condition 5: the route seam, reshaped for T2, T4 and T6

**The records** (public, beside `FRHConvoy` at H:550-580 so the suites can hold them; derived, never saved):
```
struct FRHDepot
{
	int32 RegionId = 0;      // the region the depot stands in
	FName Owner;             // NAME_None = the colony; else the rival row's name
	int32 BuildingId = 0;    // the colony's: the depot building's Id (the Lander's, 1, today); a rival's: 0 (off-map until T5)
};
struct FRHRoute
{
	FName Rival;             // the key, with RegionA: one route per (colony region, rival) - T4 adds routes from a second colony region
	int32 RegionA = 0;       // the colony's end: GetColonyRegionId() - the SAME depot landing uses
	int32 BuildingA = 0;     // that depot's building (the Lander's Id today; 0 = no depot standing: the origin for the picture)
	int32 RegionB = 0;       // the rival's end: its row's RegionId
	double LengthKm = 0.0;   // E7: (double) the row's DistanceKm, verbatim; T6: the map distance between the two depots
	uint8 Kind = 0;          // 0 = abstract (a length from data); T6 adds map routes with a terrain class
};
```
- **No Ids on the records.** `GetDepots` and `GetRoutes` fill arrays; their indices are display ordinals (E8 may print "route 2"), never saved and never used as a key. "Route Id = depot Id − 1" is dropped, as is "T2 appends colony depots after the rivals'". A new row such as "Aurora" moves ordinals, nothing else.
- **Both colony-side events use one depot.** Landing (M:1212, W:9834) and the drawn start (V:4337-4341) both read `FindColonyDepotBuildingId(GetColonyRegionId())`; `RegionA` is that same region. The disagreement the review found (A "in the rival's region", landing "in the colony's") is gone.
- **`GetRouteFor(Rival, Out)` answers true whenever the rival row exists**, whatever buildings stand (HEAD's W:9726-9728 shape). A known rival's leg can never become 0; `BuildingA` is 0 only in the no-depot case the picture already handles (V:4338's default `Home`).
- **Order.** `GetDepots`: the colony's depots first in RegionId order (one today), then one per rival row in name-string order (M:26's `MktNameLess`, case-sensitive ordinal), barter and dormant rivals included. `GetRoutes`: one per rival row in the same order, from each colony region (one today). Both walk `Defs->ForEachRivalRow` and sort, exactly as `BuildMarketState` does (M:232-233); nothing is cached, so no key can go stale.
- **The leg time reads the route**, as the design wrote it (section 2), with `R.LengthKm / Speed`; `(double)float` is exact, so Zarya's leg is 2.0 and Meridian's 3.3333333333333335, bit for bit, read on every step as HEAD's is (M:1136, M:1269).
- **What T2, T4 and T6 change, and nothing else:** T2 makes `GetBuildingRegionId(const FRHBuildingInstance&)` (0 for every building today) and `GetColonyRegionId()` real; T4 writes a binding for a new region's depot (section 8's block) and `GetRoutes` lists routes from that region too; T6 fills `LengthKm` and `Kind` from the map and freezes a copy on the convoy (its save change). `ClosedRoutes` (M:887, M:1236) is keyed by rival, which is this seam's key; it is named in section 3's table now and stays as it is.

**The depot rule**, `int32 FindColonyDepotBuildingId(int32 RegionId) const`:
1. the saved binding for `RegionId` (section 8), if it names a building that exists and is not under construction; else
2. the first building in list order with `DefName == NAME_Lander && !bUnderConstruction` whose `GetBuildingRegionId` is `RegionId` (HEAD's rule at M:1212 and W:9834; buildings stay in ascending Id order, so "first" and "lowest Id" agree); else
3. 0: no depot, and landing falls back to `AddStock` as HEAD's loops do.

Nothing in the shipped game writes a binding before T4, so rule 2 is the only live rule: **the Lander, always.**

## 7. Condition 6: the accessors and hooks the tests need

All public, const unless a hook; declared in H's public section. The suites are free functions (`RHRunMarketSuite`, `RHSimMarketSuite.cpp:188`), so every read below must be public; `ConvoyLegSolsFor` (H:4045, private) stays private and becomes a one-line call to the public getter.

**The sim (H, beside the convoy getters H:1748-1755 and the market getters H:1560-1616):**
- `double GetConvoyLegSols(FName Rival) const` — the leg in sols (0.0 for an unknown row). E8's 2.6 reads it too.
- `bool GetRouteFor(FName Rival, FRHRoute& Out) const`; `void GetRoutes(TArray<FRHRoute>& Out) const`; `void GetDepots(TArray<FRHDepot>& Out) const`.
- `int32 GetColonyRegionId() const` (the saved `ColonyRegionId`, 0); `int32 FindColonyDepotBuildingId(int32 RegionId = 0) const`; `int32 GetColonyDepotBindingId(int32 RegionId = 0) const` (0 = none); `FVector GetColonyDepotLocationCm(int32 RegionId = 0) const` (the depot building's `LocationCm`, else `FVector::ZeroVector`).
- `int64 GetMarketAnchorMcr(FName Resource, int32 RegionId = 0) const`, `GetMarketFloorMcr`, `GetMarketCeilMcr` — V and the band per slot (0 with no market or slot). E8 reads these for its band; it must not derive the band from `EarthValue_mcr` (its note at E8 design:411 assumed E7 scales V; under (b) it does not, but the getters are the right source either way).
- `int32 GetMarketRegionIdAt(int32 Index) const` (−1 out of range) — for `RH.Market`'s loop (section 11) and E8.
- `int32 GetRivalRegionId(FName Rival) const` (the row's `RegionId`; 0 with no row) — for the deck's explicit read.
- `int64 GetRivalProduceG(FName Rival, FName Resource) const` (−1 with no economy entry) and `int64 GetMarketProdSumG(FName Resource, int32 RegionId = 0) const` — RR4's pins.
- `int32 GetDepotBlockBytes() const` — the E7 block's payload bytes, measured as `GetMarketBlockBytes` (M:1353) is.
- Hook: `bool Debug_SetColonyDepot(int32 RegionId, int32 BuildingId)` — writes the binding (0 clears it); false unless the building exists and is complete. **Saved** (section 8).

**The definitions subsystem (`RHDefinitionsSubsystem.h`):**
- `int32 GetProducePermille(int32 RegionId, FName Resource) const` — 1000 when no valid row.
- `void ForEachEndowmentRow(TFunctionRef<void(FName, const FRHEndowmentRow&)>) const` — RR0's CSV-against-DT check.
- Hooks: `void Debug_SetEndowment(int32 RegionId, FName Resource, int32 Permille)` (an injected row named `Dbg_<Region>_<Resource>`), `void Debug_InjectEndowmentRow(FName Name, const FRHEndowmentRow& Row)` (a raw row, for RR0's malformed cases) and `void Debug_ClearEndowments()` (drops every injected row). Each rebuilds the index **with warnings on** (section 9) and bumps `RivalsGeneration` (`:250`), so the sim re-derives `ProduceG` on its next read, as `Debug_InjectRival` (`:620-629`) makes it re-parse. R3's note stands: only `-regionready` calls these; E4's suites never do.

E6's REFRESH moves E4's `Debug_` hooks into its rule list; E7's hooks take whatever shape E6 leaves for hooks.

## 8. The save: version "next free", one framed block at the end

**Version.** The next free number at build time: **v60** if E5 (v56), E6 (v57), D6e (v58) and D7 (v59) build first, as the queue says. The strict check stays (W:13968).

**The block: `'RHE7'`** (`RHSaveFrame::DepotTag = MakeTag('R','H','E','7')`, beside `MarketTag` in `RHSaveFrame.h`), written with `RHSaveFrame::Write` after the last frame the builder's HEAD writes (E4's `MarketTag` at W:13933 today; E5's, E6's `'RHL6'`, D6e's and D7's when they have built), read with `RHSaveFrame::Read` after the last tail read (W:14743 today), after the buildings are loaded. Payload, little-endian as every block:
```
int32 ColonyRegionId              // 0
int32 BindingCount                // the colony's depot bindings, in RegionId order
per binding: int32 RegionId, int32 BuildingId
```
The default colony writes **8 payload bytes, 28 with the frame** (12-byte header, 8-byte footer). One binding writes 16 and 36.

**What is saved and why.** The two things step 4 adds that are state, not derivation: which region is the colony's home (T2 reads it instead of a constant) and which building is a region's depot once a region can hold more than one candidate (T4 writes it when a settlement founds, bringing its own depot). Routes, rival depots and the endowment stay derived from the rows: saving them would be the second truth the design warned about. The shipped game writes no binding before T4; the harness hook is the only writer, so every battery file carries `0, 0`.

**Load repairs** (each one Warning, never a refusal, in the pattern of M:1362-1710):
- the frame lost or its CRC wrong: "LoadColony vN: the depot block is lost (%s); the Lander keeps every depot role" — `ColonyRegionId` 0, no bindings;
- `ColonyRegionId` other than 0 (until T2 defines regions): set to 0;
- `BindingCount` below 0, above 64, or past the payload: every binding dropped;
- a binding whose `RegionId` is not 0 (until T2), repeats an earlier one, or names a building that does not exist or is under construction: that binding dropped.

**The size lines that move**, each by **+28 bytes** against the gate before E7's figures (today's `battery_post_s2fix` numbers in brackets; E5-D7 will have moved them before E7 does):
- **CV8** `RHSimCavernSuite.cpp:739`: "(12059 -> 12139 bytes)", both numbers;
- **DP11** `RHSimDepthSuite.cpp:787`: "(12087 -> 12119 bytes)", both;
- **EC1 ×2** `RHSimEconomySuite.cpp:493`: "both saves are 13100 bytes" in the era and the agent band; "the E1 block's frame (3410 bytes)" does not move;
- **DC9** `RHSimDeepCommuteSuite.cpp:713`: "the 13408-byte file";
- **MX1** `RHSimMarketSuite.cpp:564`: "(11761 bytes)" and "(11740 bytes)"; the market-block offsets do not move;
- **MX9** `:1359`: "(12173 bytes)"; the "3343 payload bytes" does not move.

`diff_battery.py:10` masks "(N bytes" on its own, which covers MX1 and MX9 but not CV8, DP11, EC1 or DC9; the builder names those four in the gate's diff.

**The version lines that move**, every suite line that prints the file's version from its bytes or expects the refusal text "/N" (at HEAD: `RHSimCavernSuite.cpp:738-739`, `:788`; `RHSimDepthSuite.cpp:786`, `:831`; `RHSimCommandlet.cpp:6171`, `:10867-10873`, `:12141-12148`, `:12563`, `:12605-12607`, `:13753-13755`, `:14103`, `:14558-14559`, `:15128`; `RHSimCreditSuite.cpp:609`, `:654-655`; `RHSimDeepDistrictSuite.cpp:771`, `:815`; `RHSimEconomySuite.cpp:857`, `:878-881`; `RHSimExportSuite.cpp:530`, `:597-600`; `RHSimMarketSuite.cpp:1316`, `:1374-1381`; `RHSimTramSuite.cpp:683`, `:762`; `RHSimTransitSuite.cpp:1596`, `:1740-1743`): each literal moves from the version before E7 to E7's, as every bump since v41 has moved them. `git grep -n "== <N-1>\b\|\"/<N-1>\"" Source` at the builder's HEAD lists them. E4's "LoadColony v55:" texts and the W:37 note's older entries do not move; W:37 gains E7's entry.

## 9. Condition 8: the endowment table's data shape

**`docs/data/RH_Endowments.csv`**, column 0 the row name as every repo CSV (`RH_Rivals.csv:1`; the engine takes column 0 as the name):
```
Name,RegionId,Resource,ProducePermille,Notes
R0_Ice,0,Ice,1000,"E7 (region-ready): the identity row - region 0 makes Ice at the rate its rival rows say. 1000 = the row as written; 2000 doubles a region's output of a resource, 500 halves it. Step 4's regions add rows. Placeholder, director review."
```
- **One shipped row, at 1000.** `rh_sync_datatables.py:78-81` refuses to save a table that fills to 0 rows ("FAIL ... parsed to 0 rows, NOT saved"), so an empty table would print a standing FAIL on every sync. The identity row is read as 1000, which is also the no-row default, so it changes nothing and RR0 pins that both paths agree.
- **The row struct**, `FRHEndowmentRow : FTableRowBase` in `Data/RHRows.h`: `int32 RegionId = 0; FName Resource; int32 ProducePermille = 1000; FString Notes;`.
- **The asset**, `DT_Endowments`, created once after the compile with `RH_CREATE=Endowments:FRHEndowmentRow` (`rh_sync_datatables.py:33-52`), then filled by the usual sync; the sync log reads "NEW DT_Endowments created with FRHEndowmentRow" then "ok DT_Endowments rows 0 -> 1". The path is a new `UPROPERTY(Config) FSoftObjectPath EndowmentsTablePath` (`/Game/RedHope/Data/DT_Endowments.DT_Endowments`, beside `:231`), loaded in `Initialize` like the others (`:12-28`; "absent = every permille 1000"). The asset exists, so `TryLoad` logs nothing new.
- **The "Definitions loaded:" line (`:30`) is unchanged.** Endowments are not added to it.
- **Validation** builds an index `TMap<TPair<int32, FName>, int32>` at `Initialize` and on every injection, with one Warning per bad row, each read as 1000: `ProducePermille` outside 1..100,000; `RegionId` below 0; `Resource` that is no resource row; and a (RegionId, Resource) pair that repeats, in which case **both** rows are ignored and one Warning names the pair. Warnings are on at `Initialize` and on injection, so RR0 sees them without a world restart.

## 10. Condition 9: RR6's fixture from the binary before E7

The review is right that a committed fixture proves nothing after its first re-record, and the repo commits no `.sav`. So the pre/post comparison is a **one-time build step**, recorded in E7's `design-decisions.md` entry as the mutation runs are (`design-decisions.md:1659-1661`), and RR6 inside the suite keeps save, load, save plus the block's own checks.

**The recipe** (the builder runs it; nothing here is run by this revision):
1. `git worktree add /Volumes/Unreal/red_hope/wt_pre_e7 <the commit of the gate before E7>`; build the editor target there (the worktree has its own `Binaries/` and `Saved/`).
2. In the worktree, run `-run=RHSim -market -sols=10`. MX9 saves its fixture, two convoys out with an injected rival and a moved market, with no E7 hook (`RHSimMarketSuite.cpp:1296-1302`, slot `mktmx9`, `SaveSlotToPath` W:39), and then only loads doctored copies into other slots, so `Saved/SaveGames/mktmx9.sav` is that colony. Copy it to `Saved/baselines/regionready_pre/mktmx9.sav` (untracked, beside the battery baselines).
3. After E7 builds, its own `-market` run leaves the same colony's save at `Saved/SaveGames/mktmx9.sav`.
4. The check, with `cmp -l` or a few lines of Python: the post file is exactly **28 bytes longer**; bytes 0-3 (`RHSaveMagic`) equal; bytes 4-7 read the pre-E7 version in one and E7's in the other; bytes 8 to the pre file's end are **identical**; the last 28 bytes of the post file are one `'RHE7'` frame whose payload is `00 00 00 00 00 00 00 00`. Record the byte counts and the two versions in the entry.
5. Remove the worktree (`git worktree remove`), never with a `checkout` or `reset` in the main tree.

If a later gate changes the format, that gate repeats the step against its own predecessor; nothing is re-recorded.

## 11. Condition 10: the text fixes

- **`ClearConvoy`'s fallback has a region** (`E->RegionId`, M:645-646; the Earth-value price M:657-662). Under (b) the endowment touches no price, so it applies nowhere in `ClearConvoy`; the fallback stays as it is. Section 4's old "has no region" is withdrawn.
- **`RH.Market`** (`RHCheats.cpp:1090-1135`) has no rival to take a region from. E7 loops `for i < GetMarketRegionCount()` with `GetMarketRegionIdAt(i)` and passes that region to `IsMarketTraded`, `GetMarketTally` and `GetMarketPriceMcr` (`:1126`, `:1132`, `:1135`). With one region the lines are byte-identical to today's; with more, each region's lines follow a "region N" line. The cheat is in no battery log.
- **The deck's one price read** (`RHCommandDeckPanels.cpp:2544`) passes `GetRivalRegionId(RowName)`.
- **`-scale`** (`RHSimCommandlet.cpp:17113`, `:17118`) reads Ice's price with the default region; its injected rivals are all region 0, so it is left as it is and its line does not move.
- **`ClosedRoutes`** (M:887, M:1236) is a route structure keyed by rival: section 3's table gains the row "Closed routes | the rival's name, the route key | E7 leaves it".
- **The Lander table** (section 0) is corrected: `RHSimRovers.cpp:260-273` is the rover's fallback for a load whose task or source is gone; `:294-305` is the deploy node (a rover waits aboard the Lander for a road); `:65-68` the emergency dock; `RHSimRoads.cpp:456-465` road refunds. Three origin-based roles join it, all T1's: the pad search from the origin, the bearings measured from the origin, and the manifest robots spawning at (30 m, −30 m) (W:2469). The trade-facing roles stay three: convoy goods (M:1203-1219), covert loot (W:9831-9841), the drawn start (V:4337-4341).
- **E6 rewrites `ResolveCovert`** (E6 design 4.2: the loot comes out of the victim's store, with the executing command passed in). The builder re-anchors W:9834's loop at its own HEAD and repeats the `NAME_Lander` / `FName("Lander")` search (W:19, :546, :959, :983, :1051, :1177, :1244, :1295, :1343, :3896, :9834, :10571, :10799, :11440-11442, :12274, :12876, :12896, :13507 today).
- **The 110-sol playtest cannot move.** `RHPlaytest.cpp` (the bot, in the RedHope module) uses no convoy, order, market or covert verb, and `Saved/baselines/ind_full_playtest.txt` prints no save size or version. The bar is byte-identical.
- **Units.** Prices are mcr at HEAD; after E5's issue they are read in milli-marks. E7 adds no money and changes no unit.

## 12. Inertness and the bars

With one region, no endowment row but the identity row, and no binding:
- **Landing** picks the same building: rule 2 of section 6 is HEAD's loop at M:1212 and W:9834. The same `OutputKg` is written, then E1's `In` leg and the same broadcast.
- **The leg** is the same double, read on the same steps.
- **Every rival's `ProduceG`** is `kg × 1000` either way (the identity row and the no-row default both give 1000), so every `ProdSumG`, store, flow, price, reservation and clearing is HEAD's.
- **V, the band and the Earth price** are not read by anything E7 adds.
- **The rover's home** is the same `LocationCm`.
- **The file** differs only in bytes 4-7 (the version) and the 28-byte frame at its end.

**The bars.**
- The battery (`rh_battery.sh`, judged on `errors=0 completed=1`) is line-identical to the gate before it except the size lines and the version lines of section 8, plus the new `regionready.log`.
- The 110-sol playtest (`RH_PLAYTEST_DISTRICTS=1`) is byte-identical to `Saved/baselines/ind_full_playtest.txt`.
- `-scale`'s digests are unchanged and its market budget line holds; E7 adds no `-scale` line.
- The click test is unchanged: no UI change.
- The RedHopeSim dylib changes (it must); the one presentation change is V:4337-4341.

**Era parity.** Depots, routes and the endowment are pure functions of the buildings, the binding and the rows, read at the same points in both spines (`StepTrade` W:687 and W:859; covert operations through `StepUplink`, W:674 and W:846). No building changes the depot; only the hook writes the binding, and RR5 writes it before the window in both twins.

## 13. Presentation

- No panel, no button, no copy.
- V:4337-4341 reads `Sim->GetColonyDepotLocationCm(Sim->GetColonyRegionId())` instead of finding the Lander itself; the origin fallback is V's own.
- E8 reads `GetDepots`, `GetRoutes`, `GetConvoyLegSols`, `GetColonyDepotLocationCm` and the band getters. Its revision should drop "E7 makes V regional through the endowment" (E8 design:411): V stays Earth's value; the endowment is on production.

## 14. Deviations to log in `docs/design-decisions.md` (replacing the design's list)

1. **E7 takes a version and a framed tail block** (`'RHE7'`: the colony's region and its depot bindings), per the house rule since v54, not the design's "nothing new". Routes, rival depots and the endowment stay derived.
2. **The depot is a role, not a building** (Q-A (b)); only the three trade-facing Lander roles move behind the depot rule, and the rest move with T1.
3. **The endowment is a permille on production** (Q-B (b)), named `ProducePermille`, exact in integers with no division; the review's model, reproduced here, gives the numbers.
4. **A route's length is the row's distance, verbatim,** not integer metres, until T6.
5. **The deck and `RH.Market` read the rival's region explicitly** instead of the default 0.
6. **No Ids on depots or routes**: display ordinals only, keyed by (colony region, rival).
7. **The pre/post save comparison is a one-time build step** (section 10), not a committed fixture.

## 15. Tests: `-regionready`, complete

`Source/RedHopeSim/Private/RHSimRegionReadySuite.cpp`, dispatched from `RHSimCommandlet.cpp` beside `-market` (`:14829-14831`), appended to `SUITES` in `rh_battery.sh:37-39`. Fails loudly; no path logs an Error on purpose. **RR3 runs first** (section 7's FName reason). Rivals are injected with the market suite's `Inject` shape (`Debug_InjectRival`); covert operations use the `-covert` suite's recipe.

| Check | What it pins |
|---|---|
| **RR0 data** | `RH_Endowments.csv` equals `DT_Endowments` row for row (one row, `R0_Ice`, 0, Ice, 1000). `GetProducePermille` answers 1000 for every (region 0..3, every E1 slot). `Debug_InjectEndowmentRow` with 0, 100,001, a negative region, an unknown resource and a repeat of (0, Ice) each log exactly one Warning and read 1000; a repeat ignores both rows. `Debug_ClearEndowments` restores 1000 everywhere. The "Definitions loaded:" line is unchanged. |
| **RR1 inertness, exact** | For Zarya, Meridian, a barter rival (Greed's shape) and injected rows at 30 km and **33.33333 km**: `GetRouteFor` true, `RegionA` 0, `BuildingA` the Lander's Id (1), `RegionB` the row's, `Kind` 0, `LengthKm == (double)Row.DistanceKm` bit for bit, and `GetConvoyLegSols` equals `Row.DistanceKm / max(ConvoySpeedKmPerSol, KINDA_SMALL_NUMBER)` computed in the suite, bit for bit (2.0; 3.3333333333333335; 0.5555554707845052 at 33.33333 km). An unknown rival: no route, leg 0.0. `FindColonyDepotBuildingId(0)` is 1 and `GetColonyDepotLocationCm(0)` is the Lander's `LocationCm`. Every rival's `GetRivalProduceG` equals its row's kg × 1000; every slot's anchor, floor and ceiling equal `EarthValue_mcr` × {1, 0.25, 4}. |
| **RR2 landing follows the depot** | A stockpile placed with `Debug_PlaceInstant`, `Debug_SetColonyDepot(0, Id)` true: a standing trade's goods, a written order's filled goods and a clean covert run's loot land in that stockpile's `OutputKg`, none in the Lander's; fluids join the pool; E1's `In` legs equal a twin's that lands at the Lander; `GetColonyDepotLocationCm(0)` is the stockpile's spot. `Debug_SetColonyDepot(0, 0)` returns landing to the Lander; a binding to a missing or under-construction building is refused (false) and the rule stands. |
| **RR3 depots and routes** | Two rivals injected in the reverse of their name order, **the first creation of those names in the process** (`RrZeta` then `RrAlpha`): `GetDepots` reads colony first, then `Meridian, RrAlpha, RrZeta, Zarya`; `GetRoutes` the same order; `GetRivalEconomyName` agrees (the injected rows carry economies). A third, `aRival`, sorts **last** (ordinal: `Z` 0x5A before `a` 0x61), where a case-insensitive compare would put it first. A rival injected in region 1: its route's `RegionA` is 0 and `BuildingA` the Lander's (the colony's own depot), `RegionB` 1, its leg its row's. No record carries an Id; indices move when `aRival` is added and nothing else does. |
| **RR4 the endowment** | From MX2's Set A Ice state (store 12,000 kg, tally 0, price 3,000): `Debug_SetEndowment(0, Ice, 500)` then one `Debug_RollMarket`: Zarya's `GetRivalProduceG` 200,000, `GetMarketProdSumG` 200,000, the store 12,050 kg, the price **2,921**; the same at 2,000: 800,000 / 800,000 / 12,650 kg / 2,850; at 43: 17,200 / 11,867 kg / 3,150 (section 4.3). `Consume` (via the twin's store arithmetic), anchor, floor, ceiling and E3's Earth price for Ice unchanged throughout. `(0, Ice, 9)`: `ProduceG` **3,600**; `(0, Struct, 43)`: 1,720. A row for region 1 changes no region-0 figure. Set then clear with no roll between: `GetMarketBlockBytes` bytes identical to before, every `ProduceG` back at kg × 1000. A mid-game change rescales no price or store (the kept values after the resync equal the twin's). |
| **RR5 era parity** | A paired 10-sol run, `Debug_SetColonyDepot` on a stockpile and `(0, Ice, 500)` set before the window in both twins: the landing building, the landed kg, every price, store and balance identical (int64 and string equality), and the digest of everything outside the market (MX1's `Digest`) identical. |
| **RR6 the save** | A colony mid-trip with a binding: save, load, save byte-identical; the file says E7's version (bytes 4-7); `RHSaveFrame::Find` puts `'RHE7'` last, its payload bytes `== GetDepotBlockBytes()` (16 with one binding; 8 with none); after the load the binding, `GetColonyRegionId()`, the routes and every `ProduceG` are as before. A file of the version before E7 is refused with "Save format mismatch" and "/N". Doctored and resealed copies each load with one Warning and no Error: a lost frame (the Lander keeps every role), `ColonyRegionId` 1 (set to 0), a count of 65 or past the payload (bindings dropped), a binding to building 999 or to one under construction or a repeated region (that binding dropped). The one-time pre/post comparison of section 10 is recorded in the gate's entry, not run here. |

**Mutations** (each built, run and restored by sha256; each must fail the check named):
1. Landing hard-coded to the first Lander (the binding ignored): **RR2**, and RR5's landing building.
2. The route's length kept in whole metres, the leg read from `LengthM / 1000.0`: **RR1** at 33.33333 km (0.55555 against 0.5555554707845052).
3. Rival depots and routes in FName index order: **RR3**, the reverse-first injection (`RrZeta` before `RrAlpha`).
4. The endowment applied to `Consume` too (M:218): **RR4**, the 500 case (the store reads 12,125 kg and the price moves differently) and the Consume pin.
5. Production scaled in `double`, `(int64)(kg × 1000 × (Permille / 1000.0))`: **RR4** at `(0, Ice, 9)` (3,599 against 3,600 g) and `(0, Struct, 43)` (1,719 against 1,720). The design's 43-permille-on-Struct-at-5000 case is withdrawn: at 400 kg the double product is exact at 43.
6. The depot block written but not read back (the binding never restored): **RR6**, the binding after the load; and the block's `BindingCount` not validated: RR6's doctored count.
7. `RegionA` taken as the colony's depot in the rival's region (the review's defect): **RR3**, the region-1 rival (no route, or `BuildingA` 0 and a leg of 0.0).
8. The no-row default answering 0 instead of 1000: **RR0** and **RR1** (every `ProduceG` of a resource with no row reads 0).
9. The endowment index keyed case-insensitively or by FName index (a row for `ice`): **RR0**'s unknown-resource Warning and RR4's region-0 figures.

**`-scale`** (outside the battery): its existing market budget line (`RHSimCommandlet.cpp:17118`, 1,000 `RollMarket` calls at 10 rivals, ≤ 2.0 ms) holds; `GetRoutes` and `GetDepots` called 1,000 times at 10 rivals cost under 0.05 ms a call; every digest is identical. No new `-scale` line.

**The one-time build step** (section 10): the pre-E7 worktree's `mktmx9.sav` against E7's, recorded with byte counts and both versions in the `design-decisions.md` entry.

# RE-CHECK (2026-10-26)

Reviewer note: a short, independent re-check of REVISION 2, done together with E8's REVISION 2. Code was read at HEAD `3909395` with `git show HEAD:` only. Source at HEAD is the same as at `1f52e3d`. I built nothing and ran nothing in the repo. The arithmetic was recomputed in a scratch Python session.

**Verdict: CLEARED WITH CONDITIONS**

## What holds

- **All ten review changes are written in.** The director's Q-A (b) and Q-B (b) are applied throughout REVISION 2. `TradeDepot`, the fence, the card, `AnchorPermille` and "permille on V" appear only in section 1's list of struck items.
- **The kept-price clamp is gone, and `-market` moves only where REVISION 2 says.**
  - `BuildMarketState`'s market part (M:274-334) is untouched. The load clamp loop and its Warning (M:1678-1690, M:1686) remain the only clamp.
  - In `-market`, only these lines read the version or the file size: MX9's `Ver == 55` (MX:1316) and its "v%u" text (:1321); `V54` and `"/55"` (:1374-1381); "(%d bytes)" (:1359); and MX1's "(%d bytes)" (:564). The battery count agrees: market.log has 49 version matches, 2 from MX9 and E4's 47 "LoadColony v55:" Warnings.
  - Those Warnings are hard-coded literals (M:1385, :1521, :1686 and others), so they do not move.
  - MX1's tail compare (MX:555) only needs the tail to differ somewhere, so a frame appended after `'RHE4'` that is the same in both twins does not break it.
- **The `ProducePermille` arithmetic is exact.**
  - `P.Value` is `int64` (M:121, M:214), so `P.Value * Permille` cannot overflow.
  - I recomputed section 4.3 from M:579-623 and MX2's Set A Ice fixture (MX:608: store 12,000 kg, ring full of 0, price 3,000; Zarya Ice 400 / 150 / cap 20,000):
    - **500:** ProduceG 200,000 g; store 12,050,000 g; Flow (1,000,000 − 750,000) × 10⁴ / 1,750,000 = 1,428; Fill 6,025, so Store 2,050; Pressure (71,400 + 61,500) / 100 = 1,329; Step −265; Mag 3,000 × 265 / 10⁴ = 79, so the price is **2,921**.
    - **2,000:** 800,000 g; 12,650,000 g; Flow 6,842; Store 2,650; Pressure 4,216; Step −843, clamped to −500; price **2,850**.
    - **1000 and 43:** 2,850 and 3,150, as written.
  - Mutation 5's cases fail as claimed: in double, 400 × 1000 × 0.009 = 3599.9999999999995 and 40 × 1000 × 0.043 = 1719.9999999999998. Zarya's Struct Produce is 40 kg (`RH_Rivals.csv:2`).
  - Mutation 4's 12,125 kg is right.
- **The route seam fits T2 and T6.**
  - T6 asks for "a length in km and a terrain class" between depots (`territories-plan.md:51`). `LengthKm` and `Kind` match that.
  - T2's "endowment table (E7)" on the region record (`:47`) is a lookup keyed by RegionId, which `GetProducePermille` is.
  - Keying by (colony region, rival), with no Ids, survives a new data row and gives T4 a second route to the same rival.
  - `GetRouteFor` answers true whenever the row exists, as `ConvoyLegSolsFor` does at W:9724-9729.
- **The save frame's numbers are right.** `HeaderBytes` is 12 and `FooterBytes` is 8 (`RHSaveFrame.h:23-24`), so 8 + 20 = 28. Read order does not matter: `Read` finds a frame by tag, first at the reader's position and then by walking back from the end (`RHSaveFrame.h:28-31`).

## Remaining required changes

1. **The `'RHE7'` block and its version bump are new, and no one asked for them.**
   - **The record.** The independent review found "nothing new needs saving under Q-A (b)" (its "Checked and sound"). The director's answer says "no version fence for it".
   - **What the block holds.** It saves `ColonyRegionId`, which is always 0, and bindings that no shipped code writes before T4. Its only writer is a `Debug_` hook, which REVISION 2 makes **saved** (line 791). That breaks the house pattern that harness hooks are runtime-only.
   - **Why it is likely throwaway.**
     - T1 moves every per-colony field into `FRHColony`.
     - T2 saves the region list, and its bar speaks of "region 1" for today's map (`territories-plan.md:83`). So E7's repair "a ColonyRegionId other than 0 is set to 0" may become wrong.
     - T4 saves settlements whose depots are buildings.
     - A singular `ColonyRegionId` per file is the shape T1 removes.
   - **What it costs.** A version bump that refuses every v59 save and the checkpoints, the size and version lines, the pre/post worktree step, and RR6's block checks.
   - **Recommended: drop it.** Return to derived-only:
     - no version;
     - `Debug_SetColonyDepot` runtime-only;
     - RR6 as originally written: the hook is not saved, and the file is byte-identical to the gate before E7, so section 10's step becomes a plain `cmp`;
     - remove `GetDepotBlockBytes`, mutation 6 and the size and version lists;
     - restore the fully line-identical bar.
   - **If the lead keeps it,** that is a decision to record. RR6 must then not assert that `'RHE7'` is "last" (line 917), because E8 appends `'RHE8'` after it.
   - Either way, E8's version must follow (see E8's RE-CHECK).
2. **Anchors.** The Produce grams line is M:214, not M:217 (lines 674 and 702). Consume is M:215, not M:218 (lines 701 and 923).
3. **RR4's restore check cannot read "`GetMarketBlockBytes()`'s bytes".**
   - That getter returns a count (M:1354-1360), and `WriteMarketBlock` is private (H:3891).
   - Say how the bytes are compared. Either save before and after and `memcmp` the `'RHE4'` payloads located with `RHSaveFrame::Find`, or add a public payload getter.
4. **The size-line list is incomplete (only if any frame is kept).**
   - Three more lines print the whole-file size: CR9 "(11892 bytes)" (`credit.log:843`), EC6 "(13100 bytes)" (`economy.log:5939`) and XE10 "(11817 bytes)" (`export.log:708`).
   - All three are masked by `diff_battery.py`'s `\(\d+ bytes` rule, but they move.
   - Also, XE10 asserts that the market frame ends the file (`RHSimExportSuite.cpp:530-531`, `MarketFrame + Header + MarketPayload + Footer == W.Num()`). E5's REFRESH missed this (it says "XE10's first line does not move", E5 design:1079). The builder confirms that clause has been bounded at its own HEAD before appending.
5. **The authority line** (line 652) keeps "REVISION (R1-R3)". But R1's V, band and start-price asserts are replaced by section 3, and R2's committed fixture by section 10. It should read "R3 only".

E7 and E8 must agree on the version and frame order. Those findings are in E8's RE-CHECK.

# REVISION 3 (2026-10-26, final, after the re-check)

This section applies the RE-CHECK's five remaining changes, with the orchestrator's decision on its first, and replaces REVISION 2 wherever they differ. The authority order is now: the DIRECTOR ANSWER, then this section, then REVISION 2, then REVISION (**R3 only**: R1 is replaced by REVISION 2's section 3, R2 by section 5 below, and R4 was struck by REVISION 2), then the RE-CHECK, then the INDEPENDENT REVIEW, then the LEAD REVIEW, then the design.

**Code read at HEAD `798a549`** with `git show HEAD:` only. `git diff --stat 1f52e3d 798a549 -- Source scripts docs/data` is empty: the three commits since REVISION 2's read are docs, so REVISION 2's anchors are as valid at this HEAD as at `1f52e3d`; the two the RE-CHECK found wrong are corrected in section 3. Line numbers re-cited here are at `798a549`; the builder re-anchors at its own HEAD, as REVISION 2 says, because E5, E6, D6e and D7 build first. Nothing was built or run.

## 1. The orchestrator's decision: the `'RHE7'` block is dropped; E7 takes no save version

REVISION 2's section 8 is **struck in full**: no `'RHE7'` tag, no `DepotTag`, no frame, no payload, no load repairs, no version bump, no `GetDepotBlockBytes`, no W:37 entry. `RHSaveVersion` (W:37) and the strict check (W:13968) are not touched by E7, so a file of the gate before E7 loads under E7 exactly as it did. The design's section 7 ("Save: nothing new") is restored as written, under the Q-A (b) rule REVISION 2's section 5 already applied.

**Every route and depot value is derived at runtime:**
- the colony's depot: the building scan (REVISION 2 section 6, rule 2: HEAD's loop at M:1212 and W:9834);
- the rivals' depots and the routes: the rival rows, walked and sorted as `BuildMarketState` walks them (M:232-233);
- the endowment: `DT_Endowments` (REVISION 2 section 9, unchanged; data, not save);
- `GetColonyRegionId()`: **a constant 0**, not a saved field. T2 saves its region list and makes it real; that is T2's save change, not E7's. REVISION 2 section 7's "(the saved `ColonyRegionId`, 0)" reads "(0 until T2)".

**The depot binding hook is runtime-only, never saved.** `Debug_SetColonyDepot(int32 RegionId, int32 BuildingId)` (true only when the building exists and is complete; 0 clears) writes a transient member, `TMap<int32, int32> DepotBindings` (region to building Id), declared in H's private state beside `RepairClaims` (H:3330) and listed under the heading E4's hooks already sit under, "Harness hooks (runtime only, never saved; each keeps the invariants)" (H:1606). Nothing but the hook writes it, and it is cleared in two places:
- `LoadColony`'s teardown (W:14026-14046), beside `RepairClaims.Reset(); // claim state is runtime-only` (W:14041) and the D5a latch (W:14042), so every load returns the depot to rule 2, the Lander. The suites' `Reset` helpers load a slot (`LoadSlot`, `RHSimMarketSuite.cpp:284-287`, which calls `LoadColony`), so every suite reset clears it too;
- world start (`OnWorldBeginPlay`, W:96, where the Lander is placed at W:546), where the member is simply empty.

`FindColonyDepotBuildingId(RegionId)` keeps REVISION 2 section 6's three rules; rule 1 reads `DepotBindings` instead of a saved binding and still demands, at every read, that the bound building exists and is not under construction (a demolished stockpile hands the role back to rule 2 at once). `GetColonyDepotBindingId(RegionId)` stays, as the read of that map (0 = none). `SaveColony` (W:13443) and `LoadColony` (W:13946) are not edited by E7.

Since nothing in the shipped game writes a binding, rule 2 is the only live rule, as before: **the Lander, always.** RR2 and RR5 exercise rule 1 through the hook; RR6 proves the hook leaves the save untouched and does not survive a load (section 8).

## 2. The battery: nothing moves

With no frame and no version, **E7 changes no byte of any save file**, so:

- **The size-line list does not move for E7.** None of the nine lines that print a whole file's size moves: CV8 (`RHSimCavernSuite.cpp:739`), DP11 (`RHSimDepthSuite.cpp:787`), EC1 ×2 (`RHSimEconomySuite.cpp:493`, the era and the agent band), DC9 (`RHSimDeepCommuteSuite.cpp:713`), MX1 (`RHSimMarketSuite.cpp:564`, the file and the "up to the market block" offset both), MX9 (`:1359`), and the RE-CHECK's three additions CR9 (`RHSimCreditSuite.cpp:619`; `credit.log:843`), EC6 (`RHSimEconomySuite.cpp:866`; `economy.log:5939`) and XE10 (`RHSimExportSuite.cpp:538`; `export.log:708`). Each prints `W.Num()` of a file E7 writes identically. REVISION 2's "+28 bytes" list and its `diff_battery.py` note are withdrawn; the mask (`scripts/sim/diff_battery.py:11`, `\(\d+ bytes`) is not relied on, because the expected diff of every existing log is empty.
- **The version-line list does not move for E7.** REVISION 2 section 8's list of version literals (16 lines in 10 files at this HEAD, found by `git grep -n -E '== 55\b|"/55"' Source`; among them `RHSimMarketSuite.cpp:1316`, `:1374` and `:1380`, `RHSimCommandlet.cpp:12147`, `RHSimCreditSuite.cpp:654-655`, `RHSimEconomySuite.cpp:878-881`, `RHSimExportSuite.cpp:597-599`) moves only when `RHSaveVersion` moves. E5, E6, D6e and D7 will have moved every one of them to their own numbers before E7 builds; **E7 moves none**, and E7's own suite prints no version and no size (section 8), so no E7 line moves with any later bump either.
- **`-market` is line-identical except nothing.** The RE-CHECK asked whether MX9's version lines still move with no version bump: **they do not.** MX9's `Ver == 55` (MX:1316) and its "MX9 v%u" text (MX:1321) read the file's bytes 4-7, which E7 leaves at `RHSaveVersion`; `V54 = 54` and "/55" (MX:1374, :1380) are the refusal of the version before, which E7 also leaves. Whatever numbers E7's predecessor left in those four lines, E7's `market.log` prints the same. MX9's `B.PayloadBytes == Sim->GetMarketBlockBytes()` (MX:1316) holds because the market payload is unchanged: `BuildMarketState`'s market part (M:274-333) is untouched, E7's one M line (the Produce grams, section 3) computes the same `int64` at 1000 permille, and `ProduceG` is not in the payload anyway (W:37's v55 note lists what is). MX1's "(N bytes)" and "up to the market block (N bytes)" (MX:564) print the same numbers, and its tail compare (MX:555) sees the same `'RHE4'` difference between its twins. E4's 47 "LoadColony v55:" Warnings are literals (M:1385, :1521, :1686 and others), and the load path's "outside" Warnings (`market.log:715-716`, `:1419-1420`) still fire, because the kept-price clamp stays struck (REVISION 2 section 2). So `market.log` is expected line-identical to the gate before E7, with no exception.
- **XE10's end-of-file clause** (`RHSimExportSuite.cpp:530-531`: `MarketFrame + HeaderBytes + MarketPayload + FooterBytes == W.Num()`): E7 appends no frame, so E7 cannot break it and requires nothing of it. E8's RE-CHECK notes that it breaks at the first frame appended after `'RHE4'` and is that gate's fix; the builder confirms at its own HEAD, and E7 adds nothing to the question.
- **E8.** With no `'RHE7'`, E8's REVISION 2 stands as written: E8 takes **v60** (its line 779, "E7 takes no version"), `'RHE8'` is the first frame after the last one the gate before E7 writes, and no E7 check pins any frame's position, so E8 edits nothing in E7's suite. The contradiction E8's RE-CHECK named (E7 "v60" against E8 "v60 because E7 takes no version") is resolved on E8's side of it.

**The bar** returns to the design's original (section 5), tightened by REVISION 2's other changes:
- the battery (`scripts/sim/rh_battery.sh`, `SUITES` at `:37-39`, judged on `errors=0 completed=1`) is **line-identical to the gate before E7, version lines and size lines included**, plus the new `regionready.log`;
- the 110-sol playtest (`RH_PLAYTEST_DISTRICTS=1`) is byte-identical to `Saved/baselines/ind_full_playtest.txt`;
- `-scale`'s digests are unchanged and its market budget line holds; no new `-scale` line;
- the click test is unchanged;
- the RedHopeSim dylib changes (it must); the one presentation change is V:4337-4341.

REVISION 2's section 12 line "the file differs only in bytes 4-7 and the 28-byte frame" becomes: **the file is byte-identical.**

## 3. The line-number fixes (RE-CHECK 2)

- The Produce grams line is **M:214**, not M:217: `for (const TPair<FName, int64>& P : Produce) { OutEcon->ProduceG[EconIndex.FindChecked(P.Key)] = P.Value * 1000; }`. E7's one sim-side arithmetic change is this line's `* 1000` becoming `* Defs->GetProducePermille(Row.RegionId, P.Key)`, `int64` throughout (`P.Value` is `int64`), no division. REVISION 2's "at M:217" (its section 1 and section 4.1) reads M:214.
- Consume is **M:215**, not M:218 (REVISION 2's section 4.1 "Consume (M:218)" and its mutation 4). It is untouched.
- Nothing else in REVISION 2's section 4 changes: the model (4.2), the hand roll (4.3) and the range argument stand, and the RE-CHECK recomputed them.

## 4. RR4's restore check: how the bytes are compared (RE-CHECK 3)

`GetMarketBlockBytes()` (M:1354-1360, public at H:1584) returns a **count**, and `WriteMarketBlock` (M:1348, H:3891) is private, so a free-function suite cannot ask for the payload's bytes. **No new accessor is added.** RR4 compares saves instead, with public calls only, the recipe MX9 already uses (MX:1302-1304):

1. `Sim->SaveColony(TEXT("rrmx4a"), Err)` (H:2627) from MX2's Set A state, **before** `Debug_SetEndowment(0, Ice, 500)`; then `Debug_ClearEndowments()`; then `Sim->SaveColony(TEXT("rrmx4b"), Err)`. No roll and no step between. `WriteMarketBlock` syncs first (M:1350), so each save is the **resynced** state, which is what R3 requires.
2. `FFileHelper::LoadFileToArray` on both (`Saved/SaveGames/RH_rrmx4a.sav` and `RH_rrmx4b.sav`, through the suite's own `SaveFilePath` lambda as every suite declares it, for example `RHSimCreditSuite.cpp:262`).
3. **The assertion:** the two files are byte-identical (`A.Num() == B.Num()` and `FMemory::Memcmp == 0`). Nothing between the saves touched saved state: the resync keeps `PriceMcr`, both rings, `RingFill` and `RingHead` (M:302-309) and every `StoreG` (M:250-253), and `ProduceG`, the one thing it recomputes, is not in the payload.
4. **The diagnostic,** so a failure says where: `RHSaveFrame::Find(File, RHSaveFrame::MarketTag, HeaderAt, PayloadBytes)` (`RHSaveFrame.h:36`, the harness API) on each; both found, `PayloadBytes` equal in both and `== Sim->GetMarketBlockBytes()`, and `Memcmp` of the `HeaderBytes + PayloadBytes + FooterBytes` span (`RHSaveFrame.h:23-24`; the header's CRC included) is 0. If 3 fails and 4 passes, something outside the market moved; if 4 fails, the endowment hooks changed the market payload.
5. Then, as before: every `GetRivalProduceG` back at kg × 1000, and the twin's kept values after the resync equal (the "a mid-game change rescales nothing" pin).

This replaces REVISION 2 section 3's "leaves `GetMarketBlockBytes()`'s bytes identical" and RR4's "`GetMarketBlockBytes` bytes identical to before". The two slots are E7's and no other suite uses them.

## 5. The one-time pre/post comparison becomes a plain `cmp` (RE-CHECK 1, with no frame)

REVISION 2 section 10's step keeps its shape and loses its arithmetic: with no frame and no version, **the pre-E7 and post-E7 saves of the same colony must be identical**, and the step proves it.

1. **Before the first E7 compile,** with the binary of the gate before E7 as it stands, run `-run=RHSim -market -sols=10`. MX9 saves its fixture with no E7 hook (MX:1296-1302, slot `mktmx9`), so `Saved/SaveGames/RH_mktmx9.sav` is that colony. Copy it to `Saved/baselines/regionready_pre/RH_mktmx9.sav` (untracked, beside the battery baselines). If the pre-E7 binary is already gone, fall back to REVISION 2's worktree (its steps 1, 2 and 5; never a `checkout` or `reset` in the main tree).
2. After E7 builds, its own `-market` run leaves the same colony's save at `Saved/SaveGames/RH_mktmx9.sav`.
3. `cmp Saved/baselines/regionready_pre/RH_mktmx9.sav Saved/SaveGames/RH_mktmx9.sav` **exits 0**: the same length, every byte equal, bytes 4-7 included. Record "identical, N bytes, version V" in E7's `design-decisions.md` entry, as the mutation runs are recorded (`design-decisions.md:1659-1661`).
4. If `cmp` reports a difference, E7 is not inert, and the gate stops there.

No fixture is committed, and a later gate that changes the format repeats nothing for E7.

## 6. The authority line (RE-CHECK 5)

REVISION 2's "then REVISION (R1-R3 only; R4 is struck)" is corrected to **"then REVISION (R3 only)"**, as the first paragraph of this section states. R1's asserts on V, the band and the start price are replaced by REVISION 2 section 3 (under Q-B (b) the endowment never touches V); R2's committed fixture by section 5 above; R4 by REVISION 2 section 1. R3 (every RR4 read comes after the resync; the endowment is restored at the end) stands, and section 4 above satisfies it through `WriteMarketBlock`'s own sync.

## 7. Deviations to log in `docs/design-decisions.md` (replacing REVISION 2's section 14)

1. **No save change and no version bump** (the design's section 7, restored). Depots, routes, the endowment and the colony region are derived from the buildings and the rows; the harness binding is runtime-only. REVISION 2's `'RHE7'` block was proposed and withdrawn at the RE-CHECK: it held no state the shipped game writes before T2 and T4, its only writer was a hook, and its singular `ColonyRegionId` is the shape T1's colony record removes. The battery bar is fully line-identical.
2. **The depot is a role, not a building** (Q-A (b)); only the three trade-facing Lander roles move behind the depot rule, and the rest move with T1.
3. **The endowment is a permille on production** (Q-B (b)), `ProducePermille`, applied at M:214, exact in integers with no division; the review's model, reproduced in REVISION 2 section 4, gives the numbers.
4. **A route's length is the row's distance, verbatim,** not integer metres, until T6.
5. **The deck and `RH.Market` read the rival's region explicitly** instead of the default 0.
6. **No Ids on depots or routes**: display ordinals only, keyed by (colony region, rival).
7. **The pre/post save comparison is a one-time build step**, a plain `cmp` that must report identical (section 5), not a committed fixture.

## 8. Tests: `-regionready`, corrected and complete

`Source/RedHopeSim/Private/RHSimRegionReadySuite.cpp`, dispatched from `RHSimCommandlet.cpp` beside `-market` (`:14829-14831`), appended to `SUITES` in `scripts/sim/rh_battery.sh:37-39`. Fails loudly; no path logs an Error on purpose. **RR3 runs first** (the FName reason, REVISION 2 section 7). Rivals are injected with the market suite's `Inject` shape (`Debug_InjectRival`); covert operations use the `-covert` suite's recipe. **No check prints a file size or a version**, so no E7 line moves with a later bump.

| Check | What it pins |
|---|---|
| **RR0 data** | `RH_Endowments.csv` equals `DT_Endowments` row for row (one row, `R0_Ice`, 0, Ice, 1000). `GetProducePermille` answers 1000 for every (region 0..3, every E1 slot). `Debug_InjectEndowmentRow` with 0, 100,001, a negative region, an unknown resource and a repeat of (0, Ice) each log exactly one Warning and read 1000; a repeat ignores both rows. `Debug_ClearEndowments` restores 1000 everywhere. The "Definitions loaded:" line is unchanged. |
| **RR1 inertness, exact** | For Zarya, Meridian, a barter rival (Greed's shape) and injected rows at 30 km and **33.33333 km**: `GetRouteFor` true, `RegionA` 0, `BuildingA` the Lander's Id (1), `RegionB` the row's, `Kind` 0, `LengthKm == (double)Row.DistanceKm` bit for bit, and `GetConvoyLegSols` equals `Row.DistanceKm / max(ConvoySpeedKmPerSol, KINDA_SMALL_NUMBER)` computed in the suite, bit for bit (2.0; 3.3333333333333335; 0.5555554707845052 at 33.33333 km). An unknown rival: no route, leg 0.0. `FindColonyDepotBuildingId(0)` is 1, `GetColonyDepotBindingId(0)` is 0 and `GetColonyDepotLocationCm(0)` is the Lander's `LocationCm`. Every rival's `GetRivalProduceG` equals its row's kg × 1000; every slot's anchor, floor and ceiling equal `EarthValue_mcr` × {1, 0.25, 4}. |
| **RR2 landing follows the depot** | A stockpile placed with `Debug_PlaceInstant`, `Debug_SetColonyDepot(0, Id)` true: a standing trade's goods, a written order's filled goods and a clean covert run's loot land in that stockpile's `OutputKg`, none in the Lander's; fluids join the pool; E1's `In` legs equal a twin's that lands at the Lander; `GetColonyDepotLocationCm(0)` is the stockpile's spot. `Debug_SetColonyDepot(0, 0)` returns landing to the Lander; a binding to a missing or under-construction building is refused (false) and the rule stands; demolishing the bound stockpile returns the role to the Lander at the next read. |
| **RR3 depots and routes** | Two rivals injected in the reverse of their name order, **the first creation of those names in the process** (`RrZeta` then `RrAlpha`): `GetDepots` reads colony first, then `Meridian, RrAlpha, RrZeta, Zarya`; `GetRoutes` the same order; `GetRivalEconomyName` (H:1583) agrees (the injected rows carry economies). A third, `aRival`, sorts **last** (ordinal: `Z` 0x5A before `a` 0x61), where a case-insensitive compare would put it first. A rival injected in region 1: its route's `RegionA` is 0 and `BuildingA` the Lander's (the colony's own depot), `RegionB` 1, its leg its row's. No record carries an Id; indices move when `aRival` is added and nothing else does. |
| **RR4 the endowment** | From MX2's Set A Ice state (store 12,000 kg, tally 0, price 3,000): `Debug_SetEndowment(0, Ice, 500)` then one `Debug_RollMarket`: Zarya's `GetRivalProduceG` 200,000, `GetMarketProdSumG` 200,000, the store 12,050 kg, the price **2,921**; the same at 2,000: 800,000 / 800,000 / 12,650 kg / 2,850; at 43: 17,200 / 11,867 kg / 3,150 (REVISION 2 section 4.3). `Consume` (M:215, via the twin's store arithmetic), anchor, floor, ceiling and E3's Earth price for Ice unchanged throughout. `(0, Ice, 9)`: `ProduceG` **3,600**; `(0, Struct, 43)`: 1,720. A row for region 1 changes no region-0 figure. **The restore check (section 4):** save `rrmx4a`, set (0, Ice, 500), clear, save `rrmx4b`, no roll between: the two files byte-identical, and their `'RHE4'` frames (located with `RHSaveFrame::Find`) equal in `PayloadBytes` (`== GetMarketBlockBytes()`) and in every byte; every `ProduceG` back at kg × 1000. A mid-game change rescales no price or store (the kept values after the resync equal the twin's). Every read comes after the resync (R3). |
| **RR5 era parity** | A paired 10-sol run, `Debug_SetColonyDepot` on a stockpile and `(0, Ice, 500)` set before the window in both twins: the landing building, the landed kg, every price, store and balance identical (int64 and string equality), and the digest of everything outside the market (MX1's `Digest`) identical. |
| **RR6 the save: nothing saved, nothing kept** | A colony mid-trip (MX9's shape: two convoys out, an injected rival, a moved market) with the hook's binding on a stockpile: (i) a save with the binding set and a save with it cleared, the same colony with no step between, are byte-identical (the binding is not in the file); (ii) with the binding set again, save, load, save is byte-identical; (iii) after the load, `GetColonyDepotBindingId(0)` is 0 and `FindColonyDepotBuildingId(0)` is the Lander's Id again (the teardown clears the hook), `GetColonyRegionId()` is 0, and the routes, every rival's `ProduceG` and every slot's anchor, floor and ceiling equal their pre-save values. RR6 asserts nothing about the file's version, its size, or any frame or frame order, and prints neither. The one-time pre/post `cmp` of section 5 is recorded in the gate's entry, not run here. |

**Mutations** (each built, run and restored by sha256; each must fail the check named):
1. Landing hard-coded to the first Lander (the binding ignored): **RR2**, and RR5's landing building.
2. The route's length kept in whole metres, the leg read from `LengthM / 1000.0`: **RR1** at 33.33333 km (0.55555 against 0.5555554707845052).
3. Rival depots and routes in FName index order: **RR3**, the reverse-first injection (`RrZeta` before `RrAlpha`).
4. The endowment applied to `Consume` too (M:215): **RR4**, the 500 case (the store reads 12,125 kg and the price moves differently) and the Consume pin.
5. Production scaled in `double`, `(int64)(kg × 1000 × (Permille / 1000.0))`: **RR4** at `(0, Ice, 9)` (3,599 against 3,600 g) and `(0, Struct, 43)` (1,719 against 1,720).
6. `RegionA` taken as the colony's depot in the rival's region (the review's defect): **RR3**, the region-1 rival (no route, or `BuildingA` 0 and a leg of 0.0).
7. The no-row default answering 0 instead of 1000: **RR0** and **RR1** (every `ProduceG` of a resource with no row reads 0).
8. The endowment index keyed case-insensitively or by FName index (a row for `ice`): **RR0**'s unknown-resource Warning and RR4's region-0 figures.
9. The runtime binding not cleared in `LoadColony`'s teardown (the `DepotBindings.Reset()` line removed): **RR6** (iii), the depot after the load is the stockpile, not the Lander.

REVISION 2's mutation 6 (the block written but not read back) is dropped with the block; its 7-9 are 6-8 here, and 9 is new.

**`-scale`** (outside the battery): its existing market budget line (`RHSimCommandlet.cpp:17118`, 1,000 `RollMarket` calls at 10 rivals, ≤ 2.0 ms) holds; `GetRoutes` and `GetDepots` called 1,000 times at 10 rivals cost under 0.05 ms a call; every digest is identical. No new `-scale` line.

**The one-time build step** (section 5): `cmp` of the pre-E7 and E7 `RH_mktmx9.sav`, identical, recorded with the byte count and the version in the `design-decisions.md` entry.

## 9. What the builder builds

E7 is a runtime-only seam gate with no save change. In `RedHopeSim`: two public derived records (`FRHDepot`, `FRHRoute`, beside `FRHConvoy`) and the getters of REVISION 2 section 7 less `GetDepotBlockBytes` (`GetDepots`, `GetRoutes`, `GetRouteFor`, `GetConvoyLegSols` with the private `ConvoyLegSolsFor` delegating to it, `GetColonyRegionId` returning 0, `FindColonyDepotBuildingId`, `GetColonyDepotBindingId`, `GetColonyDepotLocationCm`, the anchor, floor and ceiling getters, `GetMarketRegionIdAt`, `GetRivalRegionId`, `GetRivalProduceG`, `GetMarketProdSumG`); one depot rule (the runtime binding, else the first completed Lander in the region, else none) behind which the three trade-facing Lander roles move (convoy goods at M:1203-1219, covert loot at W:9831-9841 as E6 leaves it, the drawn start at V:4337-4341); one arithmetic change, the Produce grams at M:214 multiplied by `ProducePermille` instead of 1000; a new `FRHEndowmentRow`, `RH_Endowments.csv` with its one identity row, `DT_Endowments`, `GetProducePermille`, `ForEachEndowmentRow` and the three endowment hooks in the definitions subsystem (REVISION 2 section 9); the transient `DepotBindings` map with its `Debug_SetColonyDepot` hook, cleared in `LoadColony`'s teardown; the explicit-region reads in the deck (`RHCommandDeckPanels.cpp:2544`) and `RH.Market` (REVISION 2 section 11); and the `-regionready` suite with its nine mutations. It does **not** touch `RHSaveVersion`, `SaveColony`, `LoadColony`, `BuildMarketState`'s market part, V, the band, E3's price, any UI, or any existing suite. The bar is the battery line-identical to the gate before E7 with every version and size line in place plus `regionready.log`, the playtest byte-identical, `-scale` unchanged, the click test unchanged, and the one-time `cmp` identical; the seven deviations of section 7 go in `design-decisions.md`.


# DIRECTOR ANSWER (2026-10-29): -scale runs under load

Asked as the orchestrator's question R, after D7's loaded `-scale` misses (`docs/qa/2026-10-23/d7-atriums-design.md`, DIRECTOR ANSWER (2026-10-29), question S), so that a run slowed by load does not stop the overnight E7 and E8 gates:

- **(a) The quiet-rerun rule covers E7's and E8's `-scale` runs.** A `-scale` budget counts only on a run started when `uptime`'s 1-minute load is under 5 and no other Unreal process (UnrealEditor, UnrealEditor-Cmd, CrashReportClient, ShaderCompileWorker) and no build or test script is running. A miss on a run that did not meet that is reported with its log, its load and its cause, and the run is repeated: it is not a director question, and no budget changes. A miss on a run that did meet it is still a director finding.
- Orchestrator ruling 22 carries it into the gate scripts. E8's frame bar keeps ruling 18.
