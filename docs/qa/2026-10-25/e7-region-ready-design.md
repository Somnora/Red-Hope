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
