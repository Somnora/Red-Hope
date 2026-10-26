# E8, the market panel: design (2026-10-25)

Step 3 (economy), gate E8, the last economy gate. **Design only; nothing here is built.** The code was read at HEAD `6119e7d` with `git show HEAD:` (its source is E4's, `76d9cc1`, v55).
- **Built:** E1-E4.
- **Designed, not built:** E5 (the mark), E6 (the levers) and E7 (region-ready, `docs/qa/2026-10-25/e7-region-ready-design.md`). E8 builds after them in the plan's order. It reads their accessors by their design names, and every row that depends on an unbuilt gate hides until that gate exists (section 7).

Short names:
- **K** is `Source/RedHope/Private/RHCommandDeckPanels.cpp`, **D** is `RHCommandDeck.cpp`, and **Dh** is `Source/RedHope/Public/RHCommandDeck.h`.
- **V** is `RHColonyVisualizerSubsystem.cpp`, **T** is `RHTrafficVisualizerSubsystem.cpp`, and **CT** is `RHClickTest.cpp`.
- **H** is `Source/RedHopeSim/Public/RHSimWorldSubsystem.h`, and **M** is `RHSimMarket.cpp`.

**Sources:**
- The plan's row: "A Market panel with prices, a 30-sol price history per resource, the trade balance, the exchange rate, open orders and route status, plus a trade overlay linking depots and rival markers. Presentation only." Also its bar: "No sim or save change. Captures plus a frame-time check."
- The UI rulings: the dark deck style; the accordion (UI polish P3, director L7 (a)); the click test at 1440x860.
- E4's deck pieces; and a read-only survey of the deck, the style, the click test, the visualizers and the frame tools at HEAD, whose facts are cited below.

**Binding rulings:** Hope is never spent and there is no money in the city (Q4a, ruling 11). The panel shows the colony's books and the neighbours' markets, nothing about wages or prices inside the city.

## What E8 builds, in one paragraph

A **MARKET** panel in the right column, a sixth fold in the accordion, reading the sim through const getters only. It has five sections:
- **Prices:** every traded resource with its price, a 30-sol sparkline and its change.
- **The trade balance:** what the colony sold and bought in the market, and what it earned from Earth.
- **The exchange rate**, once E5's mark exists.
- **Open orders:** every convoy and desk order.
- **Routes:** every neighbour's route with its length, round trip and status.

Selecting a price opens a **detail** with a larger chart, the price band and Earth's price for comparison.

A **Trade** button beside Traffic turns on a **trade overlay** in the 3D view: a ground ribbon from the colony's depot to each neighbour's marker, coloured by the route's status.

Nothing in the sim changes, **except one thing the plan's two sentences cannot both have**: a 30-sol history that survives a load needs the sim to keep and save it. That is director question Q-A.

---

## 0. What HEAD gives E8

**The deck.**
- **Technology.** Pure C++ Slate, no UMG (Dh:12-13). The root is hit-test invisible, and a click on a panel never reaches the world (Dh:22, :42-44).
- **Panels.** Each is a `Build*Panel()` returning `RHDeck::Panel(Icon, Title, Accent, Body, HeaderRight)`.
- **The right column** is a 340 px `SBox` (D:38) with a scroll box tagged `RH.RightColumn`. Its panels, in order (D:872-915): Guide, Comms, Objective, EARTH & NEIGHBORS (D:885), Crew, City, Fleet, Power, and Known Ground (only while Map is on).
- **The accordion** (P3): `enum class EFold : uint8 { Politics, Crew, City, Fleet, Power, Count }` (Dh:136). `FoldToggle(EFold, FName Tag)` (K:239-255), `FoldAnchor` (K:257-261) and `SetFoldOpen` (K:263-281) fold the others and scroll the opened one into view. The state is unsaved bools (Dh:134-135).
- **Refresh paths:**
  - attribute lambdas re-read the sim every paint;
  - `SRHCommandDeck::Tick` (D:1307-1401) rebuilds dynamic lists only when their change key moves;
  - the City snapshot runs on a 0.4 s timer, and only while open (K:1340-1349).
- **E4's pieces.**
  - The rival card's MARKET block (`BuildMarketCard`, K:2507-2627): price and holding lines, drafts, SEND ORDER and CLEAR.
  - The convoy strip (K:2289-2334): one row per `GetMaxConvoysOut()`, read once.
  - The Credits bar in Objective (K:665-683).
- **The only chart** is the Power panel's text sparkline: `GetPowerChartText` (D:1533-1566), rows of U+2581..U+2588 block glyphs downsampled to 36 columns, in Mono 9 (K:2701-2706).
- **No widget paints:** no `OnPaint`, `FSlateDrawElement` or `MakeLines` exists anywhere in `Source/RedHope`.

**The style** (`RHDeckStyle.h:25-34`): `PanelBg`, `PanelLine`, `Text`, `TextDim`, `Amber`, `Teal`, `Sky`, `Red`, `Gold` and `Green`.
- Type: `Label`, `Body` and `Value`, plus the Mono `DeckFont`.
- Brushes: `GlassBrush`, `TileBrush`, `RoundedWhite`.
- Helpers in K: `BarRow`, `Prose`, `ResourceLabel`, `ResourceIcon`, `Mass`.
- Icons present include `trade`, `earth` and `ship`.

**The sim's market reads** (H:1555-1600): `IsMarketTraded(Res, Region)`, `GetMarketPriceMcr(Res, Region)`, `GetMarketTally(Res, Back, Sold, Bought, Region)` (the 8-sol ring, `MarketRing = 8`), `GetMarketCheckedSol`, `GetMarketRegionCount`, `GetRivalStoreKg`, `GetConvoys`, `GetMarketClearings` (at most 64, newest last, **not saved**), `GetMarketSalesMcr`, `GetCreditReservedMcr`, `GetSpendableMcr`, and E3's `GetEarthPriceMcrPerKg`.
- **The politics getters:** `IsRouteClosed`, `IsEmbargoed`, `GetEmbargoGrace`, `HasDefected` and `GetSabotageRemaining`. E6 keeps their signatures (E6 1.4).
- **There is no price history anywhere.** `FRHMarket::PriceMcr` holds today's price only (H:3836). No sim delegate fires at a sol roll (H:233-263).

**The 3D view.**
- Rival markers sit at `RivalMarkerPos` (V:4193-4204): a name-hashed bearing, 200 m + 0.3 m a km. They are tinted by diplomatic state (V:4247-4263) and hidden off the surface.
- One rover per convoy is drawn from the Lander's spot to the marker (V:4338-4360); under E7, from the depot.
- **No line joins them.** The only line drawing is a debug coverage circle (V:3021-3038).
- **The TRAFFIC overlay** (T) is the precedent:
  - a command-bar button beside Map (D:1177-1182) that calls `PC->ToggleTrafficOverlay`;
  - a `UProceduralMeshComponent` with one section per colour bin, refreshed at 2 Hz (T:538-623);
  - a legend panel top-left (D:777-784).

**The click test** (CT):
- steps are pushed in `StartClickTest` (CT:1313-2320), using `PushPress`, `PushCheck` and the locators;
- `sweep.deck` (REACH) and `deck.contrast` pick up every new button by themselves;
- it is 86 of 86 at 1440x860;
- **`market.BUY`'s check is empty** (CT:1562-1563: `[]() { return FString(); }`). It proves only that the button was found.

**Frame tools.**
- `RHFrameStats` has three slots: Crew, Colony and Section (`RHFrameStats.h:9-23`).
- `RH.FrameStats` logs the median, p95 and spikes.
- D8's precedent bar is a median and p95 within budget on `RH.DeepBench`, and "a missed budget is a finding for the director, not a budget edit".

---

## 1. The panel: place, fold, header

- **Where.** A new panel, **MARKET** (icon `trade`, accent `Gold`), straight **after EARTH & NEIGHBORS** in the right column (D:885), so the neighbours and their market sit together.
- **The fold.** `EFold` gains `Market` before `Count`. Its toggle is tagged `RH.MarketToggle`, and it joins the accordion: opening it folds the others and scrolls it into view (P3). It starts **folded**. Like Politics, it is shown only while at least one neighbour is available and the market is live (`IsMarketLive()`).
- **The header summary** (one line, `Label(10)`), the first of these that applies:
  - "N orders out";
  - "balance +1,234 cr / 8 sols";
  - "N prices".

## 2. The sections (in order; each a sub-card on `TileBrush`, a `Label(8)` Amber sub-heading)

### 2.1 PRICES

- **One row per traded resource** in the shown region (`IsMarketTraded(Res, Region)`), sorted by resource name. Today that is 7 in region 0 (Zarya's five, Meridian's two).
- **Each row is 340 px less the card padding:**
  - `ResourceIcon` (14 px) and `ResourceLabel` (`Body(9)`);
  - the price, as `FormatCredits` "cr/kg", in `Value(9)`;
  - a **30-glyph sparkline** (Mono 9, `Sky`), about 162 px at the deck's Mono 9;
  - the change over 5 sols as a signed percentage in `TextDim`.

  Rising is not coloured "good", because it is good for a seller and bad for a buyer.
- **The sparkline** scales each resource between its own minimum and maximum over the window, as the power chart does. A flat price draws a flat mid-row, and a missing sol (before history began) draws a space. Its tooltip gives the window's minimum and maximum, today's price, the price 30 sols ago (or the oldest held), and the band.
- **The rows are buttons** (flat style). Pressing one selects it for the detail (2.2), and the selected row shows in `Gold`.
- **A region selector** (a row of flat buttons, one per market) appears only while `GetMarketRegionCount() > 1`. With one region it is not drawn.

### 2.2 DETAIL (the selected resource; Ice by default)

- **A 3-row block chart** of the 30 sols (Mono 9, 30 columns, three rows of U+2581..U+2588 for nine levels each), the same construction as `GetPowerChartText`.
- **Below it:**
  - "Floor 0.75 · today 1.00 · ceiling 12.00 cr/kg" (the market's band; `FloorMcr`, `CeilMcr`);
  - "Earth pays 2.25 cr/kg" (E3's `GetEarthPriceMcrPerKg`, or "Earth does not buy it");
  - "The neighbours hold 18,950 kg" (the sum of `GetRivalStoreKg` over economy rivals), with each rival in the tooltip;
  - "You sold 300 kg and bought 150 kg in the last 8 sols" (`GetMarketTally` summed over `Back` 1..8).
- No button. Buying and selling stay on each rival's card (E4).

### 2.3 TRADE BALANCE (the last 8 sols, the window of E4's tally)

- **"Sold to neighbours 1,240 cr · bought 560 cr · balance +680 cr"**: each sol's sold and bought kilograms per slot (`GetMarketTally`), valued at **that sol's** price (2.1's history), summed.
- **"Earth has paid 2,130 cr for exports so far"** (`GetSalesPaidMcr`, H:1530; E3 keeps a lifetime total, not a per-round one).
- **"Market sales so far: 12,400 cr"** (`GetMarketSalesMcr`): the lifetime credit received from neighbours.
- **Under Q-A (a), the history can be shorter than 8 sols after a load.** Sols before the history began are valued at today's price, and the line says "(at today's price before sol N)". Under Q-A (b) every sol in the window has its own price.
- **Once E5's mark is issued,** the balance is shown in the market's unit (marks) and in credits at the rate, as E5's section 9 asks.

### 2.4 EXCHANGE RATE (only while E5's mark is issued; hidden otherwise)

- **"1 mark = 1.058 cr"**, with a 30-sol sparkline of the rate and the last step's sign.
- **One line of inputs, labelled as E5's risk 1 requires:** "At the close of sol 120: trade +1,655 bp, identity +40, Earth tension 52". The rate reads the political record, not the live values the Earth panel shows.
- **"Bloc: Zarya Station, Meridian Base"** (E5's members).
- **The desk's state** ("today 300 of 500 to marks, 0 of 500 to credits"), read-only. The desk's buttons stay where E5 puts them, under EARTH & NEIGHBORS.

### 2.5 OPEN ORDERS

- **One row per convoy** (`GetConvoys()`, by Id):
  - "#3 Zarya · buy Ice 300 · sell Struct 100" (E4's `OrderLinesText`);
  - its state: "on the road, 40%"; "clears at the end of sol 61"; "cleared: owes you 200 cr"; or "coming home";
  - its reservation ("362 cr set aside");
  - a `Sky` progress bar (`GetConvoyProgress`).

  A standing convoy says "standing trade"; a barter convoy says "barter: Ice 150 for Struct 100".
- **E5's desk orders,** once E5 exists: "Desk: 300 cr to marks, clears at the end of sol 102".
- **E6's terms on an order,** once E6 exists: its booking markup ("25% tariff") and the pact's cap.
- **"No orders out"** when there are none.

### 2.6 ROUTES

One row per **available** neighbour (`IsRivalAvailable`, H:1809), in E7's route order (`GetRoutes()`; before E7, rival rows by name string). The rival cards (K:2342) and the markers (V:4225, "undiscovered dormant settlements stay off the map") show only those, so the panel never names a settlement the colony has not found.
- **"Zarya Station · 120 km · round trip 4.0 sols"** (E7's `LengthKm`; the leg is `ConvoyLegSolsFor`).
- **A status chip** (a small tinted `SBorder` with a label), first applicable wins:
  - `Red` "sided with Earth" (`HasDefected`);
  - `Red` "closed" (`IsRouteClosed`);
  - `Amber` "embargo, 6.5 sols to reassure" (`GetEmbargoGrace`);
  - `Amber` "disrupted, 4.2 sols" (`GetSabotageRemaining`);
  - `Sky` "storm: convoys halted" (an active DustStorm);
  - `Teal` "open".

  This is HEAD's order of the refusal chain and the rival card's status line (K:2362-2365), so the panel never disagrees with the card.
- **Once E6 exists:** "pact: 2,000 kg cap" and "25% tariff until sol 91", from E6's const reads.

---

## 3. The 30-sol history (the one sim question)

**What the panel needs, per region:**
- each slot's **price in force during each of the last 30 sols**;
- the **sold and bought value** of each sol (for 2.3);
- once E5 exists, the **rate** in force during each sol.

Every one of these is fixed at `RollMarket`, which is band-exact (E4), so a history written there is identical in both bands.

**Two ways to keep it** (Q-A):

**(a) In presentation only** (the plan's "no sim or save change").
- **How.** `URHMarketHistory`, a small RedHope `UWorldSubsystem`. Each tick it polls `GetMarketCheckedSol()`, and when the sol moves it appends the prices in force (`GetMarketPriceMcr` per traded slot) and the closed sol's tally, valued at those prices, to a 30-entry ring per region.
- **No save.** On every load (`OnColonyReloaded`), and in every new session, the ring starts **empty**, seeded with today's price as its one point.
- **Logs nothing,** so the `-nullrhi` playtest stays byte-identical.
- **Cost to the player.** The chart refills one point a sol: 30 sols take 10 hours at 1x, 3.3 hours at 3x, 75 minutes at 8x and 10 minutes at 60x (a sol is 1,200 sim-seconds).

**(b) In the sim, saved (recommended).**
- **How.** E4's `FRHMarket` gains a 30-sol ring: `HistPriceMcr[slot × 30]`, `HistSoldValue[30]`, `HistBoughtValue[30]`, and a fill and head; plus `HistRateMcr[30]` once E5 exists.
  - `RollMarket` writes it at each closing sol, after the clearing and before the reprice (prices in force during the closing sol).
  - It is written only; **no sim rule reads it**, so no number in the sim moves.
  - A jump of more than 8 sols writes only the sols E4's walk visits (E4 R4); the rest are marked unknown.
- **Saved** in a new `RHSaveFrame` frame (`'RHE8'`), after the last frame then written. It is read after teardown and repairs with a Warning: a lost frame starts the ring empty, as (a) does every time.
- **Cost.**
  - **Size:** for one region with E4's 18 slots, 30 × 18 × 8 = 4,320 bytes of prices, 480 of values and 240 of rate, about 5.1 KB a save with the frame.
  - **A version bump**, and E8 is no longer presentation-only: the battery's version lines and its whole-file size lines move (CV8, DP11, EC1 twice, DC9, MX1's and MX9's), as with every tail block since E1.
- **The deck reads it** through `GetMarketHistory(Region, Resource, TArray<int64>& OutPerSol)` and friends.

**Under either,** the panel code is the same. A const accessor, `GetPriceHistory(Region, Res, Out)`, is served by `URHMarketHistory` under (a) and by the sim under (b).

---

## 4. The trade overlay

- **The toggle.** A command-bar button **Trade** / **Trade [on]**, beside Traffic (D:1177-1182), calls `PC->ToggleTradeOverlay`. Console: `RH.TradeOverlay [0|1]`. It is independent of the MARKET panel.
- **What it draws.** For each route to an **available** neighbour (E7's `GetRoutes`, or one per rival row before E7; never an undiscovered one, as 2.6):
  - a **ground ribbon** 3 m wide from the colony depot's location (E7's `GetColonyDepotLocationCm`, or the Lander before E7) to the rival's marker (`RivalMarkerPos`);
  - sampled every 10 m and draped on `RHMarsTerrain::GroundZCm` + 40 cm;
  - coloured by the route's status chip (2.6): `Teal` open, `Amber` embargo or disruption, `Red` closed or sided with Earth, `Sky` halted by a storm.
- **The marker's label** gains the status word under the overlay ("Zarya Station · embargo").
- **How it is drawn.** A new RedHope subsystem, `URHTradeVisualizerSubsystem` (a pure listener: const reads only), with one `UProceduralMeshComponent` and **one mesh section per colour**, as TRAFFIC does (T:538-623).
  - It is rebuilt **only when its key moves:** each route's status chip, the depot's location and each marker's position.
  - It is never rebuilt per frame. With today's two routes of about 200-260 m, it is about 50 quads.
- **Hidden** in the section view (D8) and while the view is below ground, as the markers are.
- **A legend** line joins TRAFFIC's legend panel when both are on, or appears alone (D:777-784's slot): "Trade routes: teal open · amber embargo or disrupted · red closed".
- **Convoy rovers** are drawn as today (V:4288-4360).

---

## 5. Click-test steps and captures

**New steps** (before `// ---- the verdict`, CT:2294):
1. **`market.panel`.** Press `RH.MarketToggle`. The panel opens, Politics, Crew, City, Fleet and Power fold, and the PRICES list has exactly one row per `IsMarketTraded` slot (7).
2. **`market.history`.** In setup, `Debug_RollMarket` is called three times.
   - Under Q-A (b): the selected row's sparkline (a new const getter on the deck, `GetMarketSparkText(Res)`) has exactly as many drawn glyphs as rolled sols.
   - Under (a): one more than the polls the subsystem saw, since the seed point counts.
3. **`market.select`.** Press the Struct row. The detail's text names Structural Materials and its band.
4. **`trade.overlay`.** Press Trade. The overlay's route count (a const getter on the visualizer) equals the number of available neighbours' routes, counted in the step from `GetRoutes()` and `IsRivalAvailable`. Press again: 0 drawn.
5. **`trade.overlay.status`.** With the overlay on, `Debug_SetEmbargo(Zarya, 5)`. Within 1 s, Zarya's ribbon is in the embargo section (const getter `GetRouteStatus(Rival)`), and its chip in ROUTES reads "embargo". Then `Debug_ClearPolitics`, and it is open again.
6. **`market.BUY`, fixed** (the survey's finding). Its check asserts that the draft for Zarya's first resource rose by exactly 100 kg (`OrderDrafts`, through a const getter).
7. **`sweep.deck` and `deck.contrast`** cover the new buttons (the toggle, the price rows, Trade, the region selector when present) without new code. Each new tinted chip keeps `deck.contrast`'s 0.4 luminance difference.

The total rises from 86 by the new steps (six, counting `market.BUY`'s existing step as fixed, not new).

**Captures** (`scripts/unreal/rh_capture.sh`, 1440x860, and 1600x900 where the column matters), kept in `Saved/RHCapture/e8/` and looked at:
- `e8_panel_00`: the panel open on a colony with about 20 sols of history (E4's MX8 start save, run on, under Q-A (b); under (a), after 20 sols played at 60x);
- `e8_detail_00`: Ice selected;
- `e8_routes_00`: one route embargoed (`Debug_SetEmbargo`);
- `e8_overlay_00`: the overlay on, every available neighbour's ribbon with Zarya's amber, rovers on the road;
- `e8_rate_00`: once E5 exists, the rate section with a mark issued (E5's harness).

---

## 6. No sim change (and the one exception)

- **Under Q-A (a):**
  - no `Source/RedHopeSim` file changes; the RedHopeSim dylib's hash is unchanged;
  - the battery is line-identical;
  - the 110-sol playtest is byte-identical (`cmp`), since the new subsystem logs nothing;
  - the click test passes three clean runs in a row (UI polish P1's rule).
- **Under Q-A (b):**
  - the sim gains the write-only ring and the `'RHE8'` frame (section 3);
  - the battery moves exactly by the version lines and the whole-file size lines;
  - **a new check in E4's `-market` suite** asserts that the ring is band-exact (MX8's paired run, ring equality at every threshold) and write-only: a twin with the ring writes compiled out has every other line identical;
  - the playtest stays byte-identical (the ring logs nothing; the `.txt` holds only `[Playtest]` lines).

---

## 7. When E5, E6 or E7 is not built yet

E8 builds last, but each section degrades:
- **Without E5:** 2.4 is hidden; 2.3 shows credits only; the history has no rate ring.
- **Without E6:** the route chips use HEAD's getters, which E6 keeps; pact and tariff lines are hidden.
- **Without E7:**
  - routes are one per available rival row, with `DistanceKm` and `ConvoyLegSolsFor`;
  - the overlay starts at the first Lander (V:4338-4341's rule);
  - the price reads use region 0 explicitly.

**Each dependency is one `#if`-free branch on whether the accessor exists at build time.** The builder writes only the branch for what exists then; there is no dead code for an absent gate.

---

## 8. The frame budget

**Costs by design:**
- **Strings are cached.** The sparklines, the detail chart and the balance are cached as `FText` keyed on `(GetMarketCheckedSol(), Region, the selected resource, the clearing count)`, rebuilt once a sol, on a load and on a selection. A paint reads the cached text, and nothing is allocated per paint.
- **The order and route rows** are rebuilt only when a change key moves (the convoy Ids and states, each route's status chip), as the deck's other dynamic lists are (D:1335-1344).
- **The overlay** is rebuilt only on its key (section 4), never per frame.

**The bar:** `RH.FrameStats 15`, at 1440x860 and 1600x900, on two scenes.
- **The scenes:**
  - the E4 market scene (`RH.CityBench 2000`, the market live, two convoys out);
  - `RH.DeepBench 3000`.
- **The pass condition:** median and p95 with the MARKET panel open **and** the overlay on are each within **1.0 ms** of the same scene with both off (D8's leave-and-return precedent).
- **A new `RHFrameStats` slot, `Market`,** wraps the deck's market rebuilds and the trade visualizer's rebuild. On average it is at most **0.2 ms a frame**, and a rebuild is at most **2 ms**.
- **A missed budget is a finding for the director, not a budget edit.**

---

## 9. Wording

Every new string is Gate-D placeholder copy, in `docs/qa/2026-10-15/gate-d-wording.md` as a new block, "Rows added by E8", in E4's W-row style.

None of it is morale, sickness or evacuation wording, so Gate D's review is not engaged. The words follow E4's: "set aside", "clears at the end of sol N", "they hold".

---

## 10. Risks

1. **The 340 px column is tight.** A price row is about 14 + 70 + 60 + 162 + 30 px ≈ 336 px. A long resource name ("Structural Materials") is shortened to its `ResourceLabel` short form in the row and given in full in the tooltip and the detail. If a capture shows clipping, the sparkline drops to 24 glyphs (the last 24 sols), never the text.
2. **Under Q-A (a), the chart restarts empty after every load,** and the trade balance is valued at today's price for the sols before it began. This is the price of "no sim or save change".
3. **E5's and E6's names are design names.** The builder rebases each read onto them as built, as E5's and E6's refreshes did for E4.
4. **The overlay's ribbons are short and in the valley,** because the markers are presentation-only points 200-320 m out. When T5 moves rivals to their regions, the routes leave the valley, and the regional view (T8) takes the overlay over. E8 draws only what the valley shows.
5. **Seven price rows, plus a detail and four sections, make a long panel.** It starts folded and the accordion folds the rest. The order puts what is read most (prices, orders) first.
6. **Under Q-A (b), E8 is no longer presentation-only.** It is still a write-only sim change, and the `-market` check in section 6 guards that.

## Deviations to log in `docs/design-decisions.md`

1. Under Q-A (b): a write-only, saved 30-sol market history, so E8 bumps the save version against the plan's "E8 does not".
2. `market.BUY`'s empty check is fixed in this gate.
3. The trade balance is the market's eight sols (E4's tally window), not thirty. The tally holds eight.

## Director questions

**Q-A. Where does the 30-sol price history live?**

The plan asks for both "a 30-sol price history" and "no sim or save change". At HEAD the sim keeps only today's price. A sol is 1,200 sim-seconds: 20 minutes at 1x, 6.7 at 3x, 2.5 at 8x and 20 seconds at 60x.
- **(a) In presentation only, not saved.**
  - No sim or save change: the battery is line-identical and the dylib unchanged.
  - The chart **starts empty after every load and every new session.** Refilling 30 sols takes **10 hours at 1x, 3.3 hours at 3x, 75 minutes at 8x** or 10 minutes at 60x.
  - The trade balance values the sols before history began at today's price.
- **(b) (recommended) In the sim, saved; written only, never read by the sim.**
  - The chart survives every load and is band-exact.
  - It costs **about 5.1 KB per save** for one region: 30 sols × 18 slots × 8 bytes of prices, plus values, the rate and framing.
  - It costs **a version bump**: the battery's version and file-size lines move, as for every tail block since E1.
  - No number in the sim moves.
- **(c) No price history.** Today's price, and E4's 8-sol tally of kilograms traded, drawn as an 8-glyph sparkline of quantities instead of prices. Nothing to keep; the least to read.

**Q-B. A panel in the column, or a Market screen of its own?**
- **(a) (recommended) A sixth fold in the right column**, as specified.
  - 340 px wide: a 30-glyph sparkline per row (about 162 px) and a 3-row block chart in the detail.
  - The colony stays in view, and it follows the accordion the director chose (L7 (a)).
- **(b) A full Market screen** opened from the command bar, about 900 px wide at 1440x860.
  - It has room for one painted line chart per resource with axes, and all five sections side by side.
  - It **covers the colony while open**.
  - It needs the module's first custom-painted widget (`OnPaint` with `MakeLines`), about one more session.

---

# LEAD REVIEW (2026-10-25, not independent)

**Not an independent review.** The session's rules allow no subagent without an explicit SWARM in the prompt. The lead re-read every claim against HEAD and the survey's anchors, trying to break each. A professor pass before the build is still advisable.

**Verdict: CLEARED WITH CONDITIONS** (the lead's own).

## What holds (checked against HEAD)

- **The deck facts.** Checked: `EFold`'s five values (Dh:136); `RightColumnPx = 340` (D:38); Politics at D:885; the traffic button at D:1177-1182; the block-glyph chart at D:1533-1566 (U+2581 + level, D:1557).
- **`market.BUY`'s check is empty** (CT:1562-1563).
- **`RHFrameStats`** has exactly three slots (`RHFrameStats.h:9-23`).
- **The speed tiers** are 1x, 3x, 8x and 60x (D:1190-1197, `SpeedButton`), and a sol is 1,200 sim-seconds (`RHSimClockSubsystem.h:26`). So Q-A's refill times hold: 30 × 1,200 s at each speed.
- **Q-A (b)'s size.** E4's market payload carries 18 slot names (MX9's layout line, `RHSimMarketSuite.cpp:1316-1321`), so 30 × 18 × 8 = 4,320 bytes. The traded slots are 7 today, but the ring is per slot, as E4's tables are.
- **Band-exactness under (b).** Every value written comes from the roll (E4 section 5): the prices in force during the closing sol, and the fills valued at them.

## Defects found and fixed in the REVISION

1. **2.3's "valued at that sol's price" double-counts nothing, but it is not E4's clearing value.** E4's `ClearConvoy` values a fill at the price in force during the **closing** sol (M:639-702). E4's tally ring records grams per slot per closing sol, and a convoy's clearing sol is its `ClearSol`. So valuing each tally entry at that sol's price reproduces E4's clearing value exactly, **except** for clearings a long jump folded into its last pass (E4 R4). There, several clearing sols' fills land in one ring entry, at the last pass's prices. Under (b), the sim ring should therefore record the **value** at the clearing itself (summing `ClearConvoy`'s `BuysValue` and `SellsValue`), not re-derive it from grams. → R1.
2. **The `market.history` step under Q-A (a) depends on tick timing.** `Debug_RollMarket` three times in one frame is one poll in the presentation subsystem, so it would see one sol move, not three. Under (a) the step must roll once per frame, three frames apart. → R2.
3. **"Hidden while the view is below ground" for the overlay must use the same test as the markers** (V:4264 hides them off the surface), or a ribbon could show with its marker hidden. → R3.
4. **The draft would have revealed undiscovered neighbours.** It listed a route and drew a ribbon "per neighbour", with a "no contact" chip, but the rival cards (K:2342) and the markers (V:4225) show only available rivals. ROUTES and the overlay now show available neighbours only, the "no contact" chip and its dashed ribbon are gone, and `trade.overlay` counts available routes. (Fixed in place above.)
5. **The draft read "paid by Earth this round"**, but E3 keeps only a lifetime total (`GetSalesPaidMcr`, H:1530). The line now reads "so far". (Fixed in place above.)
6. **Section 7's "one `#if`-free branch" needs a sentence on the sole real gap.** If E8 builds before E7, `GetColonyDepotLocationCm` does not exist, and the overlay uses V's Lander rule. If E7 has built, E7's getter. That is the only branch. → stated in R4.

## Conditions

1. An independent professor pass on sections 3 and 4 before the build.
2. The director's answers to Q-A and Q-B.

---

# REVISION (2026-10-25)

This section replaces the design wherever they differ.

**R1. Under Q-A (b), the ring records value at the clearing.**
- `HistSoldValue` and `HistBoughtValue` for the closing sol are the sums of `ClearConvoy`'s `SellsValue` and `BuysValue` (M:639-702) over the convoys cleared in that pass. That is the market's own valuation.
- A long jump's last pass records its folded clearings in the last sol it writes. The sols it skipped are marked unknown.
- Under Q-A (a), the presentation subsystem values each tally entry at the price it polled for that sol. The line says "(estimated)" when a jump folded sols, which shows as a poll seeing the open sol move by more than one.

**R2. `market.history` under Q-A (a)** rolls the market once per frame (`Runner.Do` three times, a frame apart), so the subsystem's poll sees each sol. Under (b) the setup is unchanged.

**R3. The overlay's visibility** reads the same predicate V uses to hide markers off the surface (V:4264), via a shared const helper in V, and adds the section view's state. A ribbon is never drawn while its marker is hidden.

**R4. Section 7's one real branch** is the overlay's start point (E7's getter, or V's Lander rule). Every other dependency is an accessor E8 calls only when the section is shown, and a section is built only for a gate that exists at E8's build. The builder lists each branch it writes, with its gate.

# INDEPENDENT REVIEW (2026-10-25)

Reviewer note: independent professor pass. All code was read at HEAD `60e892a` with `git show HEAD:`. Short names are the design's (K, D, Dh, V, T, CT, H, M). **W** is `Source/RedHopeSim/Private/RHSimWorldSubsystem.cpp`. Font facts come from the UE 5.8 install at `/Volumes/Unreal/UE_5.8`.

**Verdict:** CLEARED WITH CONDITIONS

The structure is sound: a deck fold, an optional write-only sim ring, and a listener overlay. Q-A is correctly put to the director. But three defects change what the director would be told, or what the panel would show, so they must be fixed in the design before Q-A goes to the director and before the build. These are marked **blocking**. A **condition** is something the build must meet.

## Required changes

1. **Blocking. PRICES and DETAIL reveal undiscovered neighbours.**
   - **Evidence.**
     - Meridian ships `SliceActive` FALSE (`docs/data/RH_Rivals.csv:3`).
     - `IsRivalAvailable` is "SliceActive or discovered" (W:9701-9705).
     - A market's `bTraded` counts every economy rival in its region, with no availability test (M:316-331). `IsMarketTraded` reads that flag (M:397-402).
     - An undiscovered rival still produces (M:565-566).
     - So on a new colony, `IsMarketTraded` is true for Food and SpareParts, which only the hidden Meridian trades.
   - **Where the design shows them.**
     - 2.1 counts "7 in region 0 (Zarya's five, Meridian's two)" (design:99), and `market.panel` pins that 7 (design:218).
     - 2.2 sums "the neighbours hold" over economy rivals and names each rival in the tooltip (design:117).
     - 2.4 lists "Bloc: Zarya Station, Meridian Base". E5's own revision says the deck lists a member only once it is available (E5 design:754).
     - E4's card shows a price only on an available rival's card (K:2343). The lead review's defect 4 fixed ROUTES and the overlay, but missed these.
   - **Change.**
     - Count only rivals with `IsRivalAvailable` in: a price row, the holdings sum, every tooltip name, the bloc line, the header's "N prices" and the region selector.
     - `market.panel` then expects 5 rows on the click-test colony.
     - Add one step that discovers Meridian (`Debug_MaybeDiscoverSettlement`, H:2725) and then sees 7.

2. **Blocking. "No sim change" under Q-A (a) is false, and the list of accessors is incomplete.**
   - **Evidence.**
     - **The band.** `FloorMcr` and `CeilMcr` sit in `FRHMarket`, in the private section (`private:` at H:2763; the struct at H:3833-3843). No public getter exists: only M reads them.
       - E7 makes V regional through the endowment (E7 design:173-178, RR4), so presentation cannot re-derive the band from `EarthValue_mcr`.
     - **The leg.** `ConvoyLegSolsFor` is private too (H:4028), so 2.6's round trip cannot call it from RedHope.
     - **E5's reads.** E5 declares these const reads: `GetRate`, `GetIdentityView`, `GetTensionView`, `GetBlocMembers`, `GetMarkBalance` and `GetCurrencyBlockBytes` (E5 design:416). E8 also needs four that E5 does not declare:
       - the issued and open state;
       - the bloc trade term (2.4's "trade +1,655 bp");
       - the desk's booked totals for each sol (2.4's "300 of 500");
       - the desk order list (2.5).
   - **Change.**
     - Add a list, "sim reads E8 adds", for example `GetMarketBandMcr(Res, Region, Floor, Ceil)`, a public leg getter, and the E5 reads above. Each is a header-only const getter.
     - Correct section 6 (a) and Q-A (a). The battery stays line-identical and the playtest byte-identical. But `Source/RedHopeSim` does change, and the dylib hash moves.
     - R4's "the one real branch" is wrong for the same reasons. Restate it.

3. **Blocking. E5 changes the unit of every price, but E8 reads them as credits.**
   - **Evidence.**
     - "From `CurrencyOpenSol` on, E4's prices are read in milli-marks per kg. Their integers do not change" (E5 design:78; also :21).
     - A withdrawal re-expresses every price as `Price × RateMcr / 1000` (E5 design:96).
     - After the issue, `ValueMcr` is in milli-marks for every priced order (E5 design refresh, finding F1).
     - E8 formats prices as `FormatCredits` "cr/kg" (design:102). It puts the band in cr/kg beside Earth's credit price (design:115-116), and it keeps one undifferentiated price series (section 3).
   - **Consequences.**
     - After the open sol, the panel labels marks as credits and compares them with Earth's credits.
     - Take a withdrawal at E5's modelled rate of 1.44. Every sparkline and every 5-sol change then shows a +44% step that is only a change of unit.
   - **Change.**
     - PRICES and DETAIL read the market's unit from E5 and label it.
     - The history records the unit for each sol. Under (b), write `HistRateMcr` at the same point as the prices, before E5's rate step (E5 3.4 step 4), and add an issued/open flag per sol.
     - Draw the sparkline and the change in one unit.
     - Convert each sol of the 8-sol balance at that sol's own rate.

4. **Condition. The price row is about 100 px too wide.**
   - **Evidence.**
     - Mono 9 renders at 12 ppem: Slate's RenderDPI is 96, so 9 × 96/72 = 12 (`SlateFontInfo.h:15`, `FontCache.cpp:1342`).
     - U+2581-2588 are not in `DroidSansMono.ttf`; its cmap has no glyph for them. They come from `DroidSansFallback.ttf` (`LegacySlateFontInfoCache.cpp:122-127`) at 154/256 em = 7.22 px.
     - So 30 glyphs are 216.6 px, not "about 162" (design:103).
     - The space available is about 290 px: 340, less 24 for the panel padding (`RHDeckStyle.cpp:161`), less 18 for the sub-card padding (the rival card uses 9 a side, K:2345), less the 8 px scroll bar (D:866).
     - The row is 14 + 70 + 60 + 217 + 30 = 391 px. At 24 glyphs it is still 347 px. The design's own 336 px was already over the 316 px panel body.
     - The check holds against HEAD: the Power chart's "gen  " plus 36 glyphs comes to 296 px, which fits that panel's 316 px.
   - **Change.**
     - Use two lines per resource: name, price and change; then the sparkline. Or keep one line with about 14-16 glyphs.
     - Correct Risk 1 and Q-B (a).
     - A 1440x860 capture must show no clipping.

5. **Condition. Several click-test claims do not hold.**
   - **(a) No sweep reaches the price rows.**
     - `sweep.deck` runs once, at CT:1362, straight after NEW COLONY, with MARKET folded.
     - `Gather` keeps only widgets that are visible and on screen (CT:105-130).
     - So the price rows and the region selector are never swept. Add a sweep with MARKET open and its rows on screen.
   - **(b) `deck.contrast` checks none of E8's new widgets.** It compares only SButtons with a filled brush (CT:1271-1295). The ROUTES chips are `SBorder`s, and flat buttons have no fill. Drop 5.7's claim, or add a chip check.
   - **(c) "Trade" collides with each rival card's "TRADE" verb (K:2384).**
     - `LocateButton` matches with `Equals(..., IgnoreCase)` (CT:174). So "Trade" #0 can be the convoy dispatch.
     - A player sees the same clash. Rename the overlay button, or at least locate it by a tag.
   - **(d) Which resource `market.BUY` checks.**
     - "Zarya's first resource" must be the card's first row. The card sorts by name (K:2512), so that is Hydrogen, not the first slot.
     - Safer: assert that exactly one of Zarya's draft entries moved, and by +100.
   - **(e) No step checks an empty state.** None checks "No orders out", the rate section hidden, the selector absent, a change with fewer than 6 points, or the panel hidden with no neighbour available. Add at least "No orders out" and the hidden rate section.
   - **(f) Counting.**
     - `market.history` should check the change in glyphs across the three rolls, not an absolute count.
     - State the new total. The text says "six" new steps but lists five.

6. **Condition (under Q-A (b)). The ring must be checked against E4's state when it loads.**
   - **Evidence.**
     - A lost `'RHE4'` frame loads with prices back at the Earth value and the open sol re-seeded. See `Saved/baselines/battery_post_e4/market.log`, the MX9 line "a lost market block loads with a Warning...".
     - E4 keeps prices only if the slot count matches (M:286).
     - In either case an intact `'RHE8'` ring would end at prices and sols that no longer match.
   - **Change.**
     - Save the last sol written and the slot names, as E4's payload does (MX9's "18 slot names ascending").
     - On load, drop the ring with a Warning in three cases: E4's block was lost (E5's `bMarketBlockLost`), the last sol written is not `MarketCheckedSol − 1`, or the slot table differs.
     - The write-only twin must be a runtime switch, as `Debug_SetMarketEnabled` is (H:1593), not "compiled out".
     - Section 6 (b)'s expected battery diff must include the new `-market` lines.

7. **Condition. The frame bar must say how each number is measured.**
   - **Evidence.**
     - `RH.FrameStats` prints only per-slot averages (`RHCheats.cpp:2081-2084`). It shows slot costs only inside spike frames over 150 ms (:2119-2122).
     - D8's worst rebuild came from the section's own `WorstRebuildMs` (`RHSectionVisualizerSubsystem.cpp:621`, :839-840).
     - D8 saw the bench drift by +4 ms over minutes, so it measured in adjacent pairs (`design-decisions.md`, the D8 notes, item 6).
     - "Two convoys out" needs two available rivals, since one convoy goes to each rival (H:1734). So Meridian must be discovered first, and the colony needs fuel, parts and credit.
     - The cache key's "clearing count" stops at 64 (M:24, M:716-719).
   - **Change.**
     - Add worst-rebuild tracking to the deck's market rebuild and to the trade visualizer, and print it on the `[FrameStats]` line with the new slot.
     - Run panel-on and panel-off as adjacent pairs.
     - Write the scene recipe.
     - Drop the clearing count from the key: the sol and a reload flag are enough.

8. **Condition. The accordion separates the panel from the trade buttons.**
   - **Evidence.**
     - `SetFoldOpen` folds every other fold (K:263-276).
     - Every Buy +100, Sell +100 and SEND ORDER is on a rival card inside EARTH & NEIGHBORS (K:2410-2413, K:2554-2603).
     - So MARKET and the buttons it informs are never open together. That contradicts section 1's "so the neighbours and their market sit together".
     - `FoldFlag`'s default case returns Power's flag (K:235). Market needs its own case and its own bool.
   - **Change.** State this in section 1 and in Q-B, and put the choice to the director (Q-C below).

9. **Condition. The overlay is not a pure listener.**
   - **Evidence.**
     - `RivalMarkerPos` and `ShowsLevel` are private to the colony visualizer (`RHColonyVisualizerSubsystem.h`: `private:` at :216, then :236 and :259).
     - The marker's label is set once, when the marker spawns (V:4243).
   - **Change.**
     - Name the edits to V: expose `RivalMarkerPos` and the visibility predicate (R3).
     - Either give V the label update ("Zarya Station · embargo") or drop that line.

## Checked and sound

- **The named getters exist, are public, and mean what the design says.** These are `IsMarketTraded`, `GetMarketPriceMcr`, `GetMarketTally` (grams; Back 1 is the sol just closed; M:424-438), `GetMarketCheckedSol`, `GetMarketRegionCount`, `GetRivalStoreKg`, `GetMarketSalesMcr`, `GetCreditReservedMcr`, `GetSpendableMcr`, `GetEarthPriceMcrPerKg` and `GetSalesPaidMcr` (H:1530-1591).
- **Their neighbours check out too.**
  - `GetMarketClearings` is transient and holds at most 64 (H:3852, M:24).
  - The politics getters are at H:1773 and H:1809-1825.
  - `GetConvoyProgress(const FRHConvoy&)` is at H:1740.
  - `GetActiveEvent` is public (H:2553), and a dust storm freezes convoys (M:1115, M:1131).
- **No history exists at HEAD.** E4's tally is the 8-sol `MarketRing` (H:1555, H:3837-3838). `PriceMcr` holds today's price only (H:3836), and no delegate fires at a sol roll (H:233-263). So a 30-sol history that survives a load needs a save change. Q-A is genuinely the director's, and (b) is a reasonable recommendation.
- **Refill times:** 30 × 1,200 s (`RHSimClockSubsystem.h:26`) = 36,000 s. That is 10 h at 1x, 3.3 h at 3x, 75 min at 8x and 10 min at 60x. Correct.
- **Ring size:** 4,320 + 480 + 240 = 5,040 B. Add 8 for the fill and head, and 20 for the frame (header 12 plus footer 8, `RHSaveFrame.h`): 5,068 B. "About 5.1 KB" holds. The 18 slots are confirmed by MX9's line in `market.log`.
- **R1 is right.** `ClearConvoy`'s `BuysValue` and `SellsValue` use the closing sol's price before the reprice (M:639-678). So recording them reproduces E4's own valuation.
- **Long jumps.** A jump makes at most 8 passes (M:522), and the last pass catches every due convoy (M:529-530). So (b)'s "the rest are marked unknown" matches the code.
- **ROUTES and the overlay.**
  - The availability filter matches the cards (K:2343) and the markers (V:4223-4225).
  - The chip order matches the card (K:2362-2365).
  - Round trip: 120 km at 60 km/sol (RH_Config `ConvoySpeedKmPerSol`; W:9657-9662) is 2 sols each way, 4.0 in all. Correct.
  - The ribbons are 236 m (Zarya) and 260 m (Meridian) (V:4201). At 10 m a quad, that is about 50 quads. Correct.
- **The claimed costs are realistic.** Rebuilds happen once a sol or when a key moves, and the TRAFFIC overlay is the precedent (T:538-627). D8's 1.0 ms paired bar was measurable, with noise of +0.11 and −0.19 ms.
- **The accordion mechanics extend cleanly.** `FoldAnchors` is sized by `EFold::Count` (Dh:374).
  - At 1440x860 the column's viewport is about 1,014 layout px. That is (860 − 106 × 0.768) / 0.768, with the scale 0.768 measured on `Saved/RHCapture/e4/e4_card_00.png`.
  - The open panel, at about 770 px by my estimate, fits once it is scrolled to the top.
- **Other checks.**
  - `market.BUY`'s check is empty (CT:1562-1563). The fix through `OrderDrafts` (Dh:322) is the right idea.
  - No `OnPaint`, `FSlateDrawElement` or `MakeLines` exists in `Source/RedHope`.
  - The panel shows no wage or in-city price, so ruling 11 holds.

## Director questions

**Q-A (the design's question, corrected).** Where does the 30-sol price history live?
- **(a) In presentation only, not saved.**
  - After every load, the chart refills one point a sol: 30 × 1,200 s is 10 h at 1x, 3.3 h at 3x, 75 min at 8x and 10 min at 60x.
  - The 5-sol change stays blank for 5 × 1,200 s: 100 min at 1x, 33 min at 3x, 12.5 min at 8x and 100 s at 60x.
  - It is **not** "no sim change". E8 still adds const getters to H (change 2), so the dylib moves. The battery stays line-identical and the playtest byte-identical.
- **(b) In the sim, saved, write-only.**
  - It costs 5,068 B per region per save, plus about 0.2 KB for slot names (change 6). On MX9's 12,173-byte fixture save, that is +42%.
  - It costs one version bump beyond E5-E7's. The version lines, the size lines and the new `-market` lines move.
- **(c) No price history.** As written. The plan's "30-sol price history" is not delivered.

**Q-B (corrected numbers).** The column option holds a 30-glyph sparkline only on its own line: 217 px of about 290. On one line with the name and price, it fits 14-16 glyphs. So 7 resources take 14 lines, or 10 after change 1.

**Q-C (new). Does MARKET join the accordion? The accordion is L7 (a), the director's rule.**
- **(a) It joins (as designed).**
  - To act on a price, one press opens EARTH & NEIGHBORS and folds MARKET. Then come the Buy or Sell presses, then one press back.
  - That is 2 extra presses for every look-and-act.
- **(b) It is exempt, so both can be open.**
  - EARTH & NEIGHBORS with one card is about 554 layout px: 425 screen px on `e4_card_00` divided by 0.768. MARKET is about 770, my estimate.
  - Together that is about 1,320 against about 1,014 visible. So about 300 px of scrolling separates a price from its button.
  - Each discovered neighbour adds about one card.
  - This departs from L7 (a)'s rule that opening one fold closes the others.

## Not verified

- **Widths are computed, not measured.** They come from the font files and the engine source. I found no capture of the Power chart to measure against.
- **Heights are partly estimated.** MARKET's height is my estimate from its section list. EARTH & NEIGHBORS' height is measured from one capture.
- **E5, E6 and E7 are designs, not code.** Their accessor names and semantics may change when they are built.
- **Not checked:**
  - the battery line list in section 3 (CV8, DP11, EC1, DC9, MX1, MX9);
  - the MX8 "start save" for the captures;
  - Q-B (b)'s "one more session".
- **Nothing was built or run,** so no frame costs were measured.
- **Example figures are illustrative, not modelled:** 1.058, 18,950 kg and +1,655 bp.
