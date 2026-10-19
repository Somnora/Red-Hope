# C5, districts and growth: design (draft for adversarial review)

Repo: /Volumes/Unreal/red_hope/red_hope (UE 5.8; RedHopeSim is the deterministic sim, RedHope is presentation).

Read:
- the plan, docs/qa/2026-10-18/living-city-plan.md (the C5 row, the appendix and Q2a);
- the C2 brief, docs/qa/2026-10-18/c2-population-brief.md;
- the decisions, docs/design-decisions.md (the 2026-10-18 rulings, politics included);
- the C4 design, docs/qa/2026-10-18/c4-roads-design.md, for the review style that worked.

Prior gates:
- C1: O(N) sim and the -scale suite.
- C2a: per-head Hope, shared rationing.
- C2b: arrivals. Immigration is capped by free beds minus 1 and by the comfortable room `GetHousingComfortCapacity`; the Big Arrival needs 100 free beds.
- C3: crowds on screen.
- C4: roads, save v36, and the battery at 39 runs.

Director rulings that bind this gate:
- **Q2a, hybrid growth:**
  - the PLAYER places production, power, life support and anything special;
  - the player ZONES districts;
  - inside HOME and SERVICE districts the colony builds housing and services itself, from the material pool, when there is demand and material.
- **Districts get names,** and are built so they can later have interests and a political voice (the politics ruling).

## Gate split
- **C5a, districts and demand.** Data, painting, names, membership, the demand readout, the overlay and the City panel. INERT until a district is painted, so the battery stays line-identical. Save v37.
- **C5b, growth.** The colony builds inside home and civic districts. Save v38 if needed. It moves no existing suite, because none paints districts.

## C5a

### Data (RedHopeSim)
```
FRHDistrict {
  int32 Id;         // never reused
  FName Kind;       // Homes | Industry | Farming | Research | Civic
  FString Name;     // generated, renamable
  int32 RegionId=0; // several regions are step 4
  int32 Level;      // 0 = surface; <0 = an underground floor
  TArray<FIntPoint> Lattice; // surface only, sorted
}
```
- **Surface districts** are sets of 10 m LATTICE cells, the same lattice as habitat bays (the world 10 m grid that RebuildHabTopology uses). They are painted as rectangles and unioned.
- **Underground,** a district is a WHOLE FLOOR (Level), because a floor is a carved-cell count, not a shape. Finer underground shapes come in step 2, going deep.
- **Exclusivity:** a lattice cell belongs to at most one district, and a floor to at most one.
- **Membership is derived, never saved:**
  - a building or surface bay belongs to the district containing its lattice cell (a footprint centre, or a bay centre);
  - an underground cell belongs to its floor's district;
  - a colonist belongs to their home's district.
  - The derived maps are rebuilt on a DistrictsVersion bump, and on building, topology, zoning and population changes. Use the C1 version-counter pattern.
- **Names:** a deterministic pick from a new data list, docs/data/RH_DistrictNames.csv (real Martian place names: Gale, Jezero, Holden, Eberswalde, Gusev, Meridiani, Utopia, Elysium, Tharsis, Arcadia, Acidalia, Chryse, Hellas, Argyre, Isidis, ...; about 40 rows), by district Id. Renaming is an order.

### Orders (uplink verbs, the same lag as other orders)
- PaintDistrict(Kind, lattice rect | Level)
- EraseDistrict(Id) or erase-rect
- RenameDistrict(Id, Name). Its storage needs care: FRHCommand has no string field today. Propose a name index into the names list for C5a; free-text names come with the UI later, OR add an FString to FRHCommand saved in the queue. Reviewer: pick.

Preflight (CanPaintDistrict):
- the rect is in coverage (Power-as-Territory);
- no overlap with another district; overpainting the same Kind merges;
- for a floor, the floor exists (shaft-reached).

### Demand (pure functions of state; colony-wide and per district)
- **Housing:** HousingPressure = max(0, DesiredHeadroom - (ComfortCapacity - N)), where DesiredHeadroom is the growth the colony wants room for:
  - if smoothed Hope >= ImmigrationHopeMin: the immigration count the next ship would bring if room allowed (the C2b formula without the comfort cap), plus the reserve bed;
  - if the Big Arrival is pending (streak running, not done): BigArrivalColonists;
  - otherwise a small base (one Crew Pod).
- **Jobs:** Unemployed = max(0, N - FilledSeats); SeatsShort = max(0, N - TotalSeats).
- **Services:** per room function with MoraleWeight > 0, the C2a coverage gap: 1 - coverage_f. Report the worst functions.
- **Air:** homes on short rations (C2a's ration < 1), and the oxygen margin in sols per home.
- **Exposure:** GetCityDemand() returns a struct. The deck reads it; the sim does not act on it in C5a.

### Presentation (RedHope)
- A DISTRICT paint mode beside ZONE | FURNISH | WALLS (or a City tab): pick a Kind, drag a lattice rectangle on the surface, or click a floor in the shaft/floor bar underground.
- An overlay: a tinted lattice fill per Kind, with a soft border and the district name label (hover-gated, like other labels); underground, the floor tint plus the name.
- A City panel:
  - four demand bars (Housing, Jobs, Services, Air) with plain-words prose (the Gate-D framing rule applies to anything about people);
  - a district list (name, kind, residents, beds, seats);
  - rename.
- RH.District cheats for captures and tests.

### Save v37
Districts (Id, Kind, Name, RegionId, Level, Lattice) and the next id.

### Tests
- **A -districts suite:**
  - paint, merge, erase, overlap refusal and the coverage refusal;
  - floors;
  - names are deterministic; rename works;
  - membership of buildings, bays, cells and colonists;
  - the demand math for constructed cases (each term);
  - a save/load round trip;
  - a queued order survives save/load.
- **-scale:** budgets unchanged. Add 50 districts over the scale colony; membership rebuild and GetCityDemand under budget.
- **Battery:** line-identical apart from the new suite.

## C5b, growth

### When and where
- **The growth check** runs at each sol boundary: a threshold on monotone time, both bands. For each district in Id order:
  - **Homes, surface:**
    - only EXTENDS an existing rated habitat network that already has a bay inside the district;
    - the PLAYER founds habitats; the city grows them;
    - the project is the next HabModule of at most 2x2 bays on free lattice cells inside the district, edge-adjacent to that network, in coverage, not on a road strip;
    - candidate order: lexicographic over lattice coordinates, nearest to the network's root first.
    - When built, its bays are zoned Living and furnished with beds (BunkBed slots).
  - **Homes, underground floor:** only on a RATED floor. The project is +N carved cells (ExcavateFloor, which needs a Borer), zoned Living and furnished with beds.
  - **Civic:**
    - when Services demand has a gap, zone a free cell or bay in the district to the WORST-covered service room function;
    - a "service room" is any room function with MoraleWeight > 0 that is not a seat function, nor Living or Garden;
    - furnish it to its function.
  - **Industry, Farming, Research:** no growth; the player places these (Q2a). They exist now for identity, membership, the overlay and later politics.
- **One open project per district at a time.**
- **Growth pauses** when:
  - any home is on short rations;
  - smoothed Hope is in the FAILING band;
  - the shortfall would break the reserve (below).

### Spending (protects the player's plans; a placeholder default the director can change)
- Growth spends Struct, and the other bill resources, only ABOVE a reserve: GrowthReserveStructKg (1,000) PLUS the open quota's Struct bill (quota bills are HOLDINGS, so growth must never eat quota progress).
- If the project bill does not fit above the reserve, no project starts.
- **Reviewer:** do the open quota's other bills (O2, H2) need protecting too? Growth bills are Struct, Shielding and Glass, so probably not; confirm.

### Building it
Projects are issued as ORDERS through the same uplink and construction pipeline the player uses, so robots build them visibly:
- a Build order for the HabModule;
- an Excavate order for the cells;
- Designate orders for the zoning;
- Furnish orders for the furniture.

They are tagged as colony-issued (the source = colony) for the UI and for one alert per project ("Gale is growing: 4 new homes underway", Gate-D placeholder).

### Era band
- **The problem:**
  - CanEnterEraMode refuses while construction is open;
  - era has no robots;
  - EraLogistics teleports.
- **Proposal:**
  - in the era band, a growth project is applied INSTANTLY, with its bill paid, at the same sol-boundary decision;
  - so era never refuses because of growth, and the ledger spends the same material in both bands;
  - the agent band builds it with robots over time.
- **Reviewer:** check this against the parity bar and CanEnterEraMode. Is "applied instantly in era" acceptable, or should growth defer to the next agent-band window?

### Tests
- **A -citygrowth suite** (the -growth suite already exists for births):
  - projects are chosen deterministically;
  - the reserve and the quota bill are respected;
  - one project per district;
  - the pauses;
  - surface extension adjacency;
  - the underground floor;
  - a civic service room for the worst gap;
  - an era/agent ledger within 5%;
  - save/load mid-project;
  - no growth without districts.
- **Battery:** line-identical apart from the new suite.
- **A playtest variant:** the bot paints a Homes district over its vault floor and a surface habitat. It shows growth, settlers and, if room allows, the Big Arrival over 110 sols. Report pop, settlers, births, Hope and the growth projects.

### Save
- Per district: an open project id, tagged orders, and the last growth sol, if needed.

---

# REVISION after the adversarial review

C5a is CLEARED with items 1-7 below. C5b was BLOCKED and is redesigned (section C5b-R); C5b is a later gate. These revisions override the sections above.

## C5a required changes
1. **One district per building.** Bays inherit their BUILDING's district, set by its bay-0 / GridMin lattice cell. Residents are known per building (HomeHab), so never split a habitat across districts.
2. **Rename: add `FString Text` to FRHCommand,** serialized in both the uplink-queue save and load (v37). The sim trims the name, caps it at 32 characters and refuses an empty one. Never use FName for names: it compares case-insensitively.
3. **Merge and erase rules:**
   - a rect overlapping more than one district, or a district of another Kind, is REFUSED;
   - same Kind joins the one district it overlaps (it keeps its Id);
   - districts need not be connected;
   - a paint rect is capped at 32x32 lattice cells per order (coverage checks scan buildings, and the ghost runs every frame);
   - every new verb rejects explicitly with a reason, never falling through as "executed" (today unknown verbs do: check ExecuteCommand's default).
4. **Names:**
   - a HARD-CODED array of about 40 real Martian place names in the sim (like the colonist names array), not a new DataTable;
   - the pick is by district Id;
   - after the list runs out, suffix "II", "III", ...
5. **Housing pressure shares GetImmigrationCount's terms.** Split that function into a raw part and a room part, and measure headroom with the SAME limits arrivals use:
   - for settlers: min(FreeBeds - ReserveBeds, ComfortCapacity - N);
   - for the Big Arrival: BigArrivalFreeBeds - FreeBeds (its gate is GetFreeHousing);
   - before the vault is certified, surface beds count in comfort capacity but not in free beds, so state which term applies.
6. **Demand terms stay COLONY-WIDE in C5a** (service coverage is colony-wide in C2a). Per-district rows show counts only: residents, beds, seats.
7. **Caches:**
   - membership is rebuilt lazily, only when read, keyed on (DistrictsVersion, ZoningVersion, ColonistSetVersion, FurnitureVersion);
   - hook them into -RHVerifyCaches;
   - no new log lines in runs without districts.
8. **(Also in C5a) Fix the existing CommandId-on-load bug:** CommandId is not serialized, so every loaded order is Id 0 and the deck's cancel removes the first queued order. Serialize it, or re-assign unique ids on load. Add a test.

## C5b-R (the later gate, for the record): growth as a project record, the same in both bands
- **At start,** take the WHOLE bill atomically:
  - Struct, Shielding, Glass and bed furniture costs, plus the O2 fill for the new bays or cells;
  - read availability from uncommitted holdings, following C4's GetRoadStructAvailableKg / TakeRoadStruct;
  - stop at a colony-wide reserve PER RESOURCE: free = uncommitted holdings - the open quota's bill for that resource - its reserve, plus a breathing O2 floor;
  - at most ONE project starts per sol colony-wide, round-robin by district Id.
- **While open:** a linear RemainingSols countdown, identical in both bands. There is no construction site, so era is never refused, and no uplink orders, so no lag, cancels or tagging.
- **At completion, in one step:**
  - re-check the site (placement without the material check); on failure refund and close;
  - add the building instantly or carve directly;
  - zone and furnish directly;
  - pre-fill O2 from the bill.
- **What the player sees:** a scaffold ghost that fills in with progress. Robot-built growth may come with C6.
- **Surface rules:**
  - a new module may touch ONLY the target network;
  - door-sealed networks either get doors on the new doorways in the bill, or are not extended.
- **Civic rooms:** a data column in RH_Rooms (CivicService), a furniture recipe per civic function, and an adjacency check before zoning. Never auto-zone odor emitters near homes.
- **Erasing a district** cancels its open project and refunds it.
- **Determinism:**
  - candidates sort by integer squared lattice distance, then (X, Y);
  - function ties break by lexical FName;
  - projects save sorted by district Id;
  - test: a project saved and loaded halfway finishes on the same step as a twin that never saved.
- **-scale:** add a surface case of about 500 grown bays with civic emitters, within the 0.5 ms Hope budget. Cache the surface adjacency term on ZoningVersion if it goes over.
