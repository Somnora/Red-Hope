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


# DIRECTOR ANSWER (2026-10-26)

- **Q-A (b):** the 30-sol price history lives in the SIM, saved, write-only: about 5,068 B per region per save plus about 0.2 KB of slot names, in its own RHSaveFrame frame, checked against E4's state on load (review change 6). One version bump, taken when E8 builds.
- **Q-C (a):** MARKET joins the accordion, as the director's L7 (a) rule says. Acting on a price costs about 2 extra presses.
- Q-B is settled by the corrected numbers: a 30-glyph sparkline sits on its own line under the name and price.

# REVISION 2 (2026-10-26, after the independent review)

This section replaces the design, the first REVISION and the review's own numbers wherever they differ. It applies the director's answers: **Q-A (b)** (the history lives in the sim, saved, write-only, in its own frame, checked against E4's state on load), **Q-C (a)** (MARKET joins the accordion) and Q-B as settled (the 30-glyph sparkline on its own line under the name and price).

Code was read at HEAD `1f52e3d` with `git show HEAD:`. The review read `60e892a`; the step-2 fixes (`1505941`) moved H by about 14 lines, so every anchor below is re-pinned. Short names are the design's (K, D, Dh, V, T, CT, H, M) plus **W** (`Source/RedHopeSim/Private/RHSimWorldSubsystem.cpp`), **Vh** (`Source/RedHope/Public/RHColonyVisualizerSubsystem.h`) and **MX** (`Source/RedHopeSim/Private/RHSimMarketSuite.cpp`). E5, E6 and E7 are still designs: their names are design names and the builder rebases them, as E5's REFRESH did for E4. E7's director answers are in: Q-A (b), the Lander keeps every depot role; Q-B (b), the endowment is a permille on rival production. Neither changes what E8 reads.

## R2-1. Discovery gates every row, sum, name and marker (review change 1, blocking)

**How discovery is tracked at HEAD.**
- `IsRivalAvailable(Rival)` is `Row && (Row->SliceActive || DiscoveredRivals.Contains(Rival))` (W:9768-9772; declared H:1823). `DiscoveredRivals` is a saved `TSet<FName>` (H:3977, the v21 slot). `IsRivalDiscovered` reads the set alone (H:1826).
- A survey's completion calls `MaybeDiscoverSettlement(SurveyHistory.Num())` (W:13030). The worker (W:13034-13072) takes the first dormant row by `LexicalLess`, rolls `Seed = HashCombine(StrCrc32("survey"), SurveyCount x 2654435761)`, `U = (Seed % 100000) / 100000`, and reveals it when `U < DiscoveryChancePerSurvey` (0.5, H:3984). It seeds the relation, broadcasts `OnAlert` and logs `=== SETTLEMENT DISCOVERED ===`. The harness hook is `Debug_MaybeDiscoverSettlement(int32 SurveyCount)` (H:2739).
- Meridian ships `SliceActive` FALSE (`docs/data/RH_Rivals.csv:3`) and trades `SpareParts` and `Food`; Zarya trades `Ice`, `Water`, `Oxygen`, `Struct` and `Hydrogen` (`:2`). An undiscovered rival still produces and reprices (M:565-566, "availability gates booking only"), and a market's `bTraded` counts every economy rival in its region with no availability test (M:316-330). `IsMarketTraded` reads that flag (M:397-402). So on a new colony `IsMarketTraded(Food)` and `IsMarketTraded(SpareParts)` are true while no neighbour the player knows trades them.
- The deck's precedent: the rival card is collapsed unless `IsRivalAvailable` (K:2343); EARTH & NEIGHBORS itself is collapsed unless some row is available (K:2448-2457); the markers skip unavailable rows (V:4223-4225, "undiscovered dormant settlements stay off the map").

**The rule.** A resource is **shown** in region R when at least one rival that is available, has an economy and sits in R lists it. One deck helper, `ShownMarketSlots(int32 RegionId, TArray<FName>& Out)`, walks `GetRivalEconomyCount()` / `GetRivalEconomyName(i)` (name order, H:1580-1581), keeps `IsRivalAvailable(Name) && HasRivalEconomy(Name) && Defs->GetRival(Name)->RegionId == RegionId` (`FRHRivalRow::RegionId`, `RHRows.h:413`), unions `GetRivalTradedResources(Name, ...)` (H:1577) and sorts by name string. It is part of the PRICES list's rebuild key, never a per-paint read. The ring in the sim holds every slot; the gating is in what the deck reads out of it.

**Everything player-facing reads the shown list:**
- **PRICES:** one row per shown slot: **5** on a new colony (Hydrogen, Ice, Oxygen, Struct, Water), **7** once Meridian is discovered. The header's "N prices" is the shown count.
- **DETAIL:** only a shown slot can be selected; the default is Ice when shown, else the first shown by name. "The neighbours hold N kg" sums `GetRivalStoreKg` over the **available** economy rivals of the region, and the tooltip names only those.
- **2.4's bloc line** lists members that are available (E5 REVISION O10: "the deck lists a member only once it is available").
- **The region selector** lists a region only while it has an available economy rival, and is drawn only when more than one such region exists.
- **ROUTES and the overlay:** available rivals only (REVISION defect 4 stands).
- **OPEN ORDERS** needs no gate: a convoy can only be booked to an available rival (`PlanConvoy`'s first refusal, "No trade contact", M:868), so every convoy names a known rival. MX13 (below) still asserts it.
- **The marker's status word** (R2-9) exists only on markers V spawned, which are available rivals.
- The rebuild key for PRICES carries an availability bitmask over the rival rows (two rows today; `ForEachRivalRow` with `IsRivalAvailable`), since no version counter moves on a discovery.
- Console output (`RH.Market history`, below) prints every slot: a log line is not player-facing, as E5's join line is (O10).

**Click test:** `market.panel` expects the count the rule computes from the sim in the step **and** exactly 5 on the click-test colony, so a data change fails loudly; `market.discover` discovers Meridian through the hook and expects 7 (R2-5).

## R2-2. The sim-side history under Q-A (b) (review change 2, blocking)

**Where it lives.** In `FRHMarket` (H:3850-3860), beside E4's `PriceMcr` and the 8-sol tally, so it is per region and travels with the market's own sync:

```
// H, inside FRHMarket. SAVED (v60, the 'RHE8' frame). Written by RollMarket and by the load repairs only; no sim rule reads it.
static constexpr int32 MarketHistSols = 30;
TArray<int64> HistPrice;          // slot x MarketHistSols + entry: the price in force during that sol, in the market's unit then
                                  // (mcr; mmk from E5's open sol on); -1 = unknown (a sol a long jump skipped)
TArray<int64> HistSoldValue;      // per entry: ClearConvoy's SellsValue summed over the clearings of the pass that closed that sol (R1)
TArray<int64> HistBoughtValue;    // per entry: its BuysValue, the same way
TArray<int64> HistRateMcr;        // per entry, once E5 exists: RateMcr in force at the open of that sol; 0 = unissued
TArray<uint8> HistUnit;           // per entry, once E5 exists: 0 credits, 1 marks (the market's unit during that sol)
int32 HistFill = 0, HistHead = 0; // entries held (0..30) and the next entry to write
int32 HistLastSol = -1;           // the sol of the newest entry; -1 = empty
```
The entries are **contiguous sols ending at `HistLastSol`**: the entry for sol t sits at `(HistHead - 1 - (HistLastSol - t) + 2 x 30) % 30` and is valid while `0 <= HistLastSol - t < HistFill`. Integer mcr or mmk throughout; no double enters.

**The newest entry is the open sol.** After any roll `HistLastSol == MarketCheckedSol`, and the open sol's entry holds the price the roll opened it with. The deck's 30-glyph sparkline is therefore 30 ring entries (29 closed sols and the open sol), nothing live. This is why the load check in R2-6 compares the last sol written with `MarketCheckedSol`, not `MarketCheckedSol - 1` as the review wrote.

**Where it is written: three points inside `RollMarket` (M:509-558), all inside the per-sol pass loop (M:526-552).** For pass i closing sol s = First + i:
1. **At the top of the pass, before the clearing:** `HistEnsure(Mk, s)`. If `HistLastSol < s`, append entries for `HistLastSol + 1 .. s`: unknown (-1, 0, 0) for every sol but s, which takes the live `PriceMcr` (the price in force during s), the live unit and rate. This is where an empty ring starts (the first roll ever, or a ring the loader dropped). On every other roll the entry for s already exists and this is a no-op.
2. **In the clearing** (`ClearConvoys` at M:539; `ClearConvoy` M:639-730): where `ClearConvoy` sums `SellsValue` and `BuysValue` (M:646-678) it adds them into `HistSoldValue` / `HistBoughtValue` at the entry for `ClosingSol`, in the market's unit then. That is REVISION R1: the market's own valuation. On the last pass of a jump, convoys due in skipped sols clear with that pass's `ClosingSol` (M:529-530), so their value lands in the last walked sol's entry, as R1 says.
3. **After the reprice** (`MarketProduceAndReprice`, M:545-550) **and, once E5 exists, after E5's step 4** (the political record, the journal's transitions, admissions and the rate step, and a withdrawal's re-expression of prices: E5 3.4): `HistOpen(Mk, Next)` with `Next = (i == Passed - 1) ? SolNow : s + 1`. It appends the entry for `Next` with the post-reprice `PriceMcr`, and the unit and `RateMcr` in force for `Next`; on the last pass of a jump the skipped sols `s + 1 .. SolNow - 1` are appended as unknown first. It runs whether or not `bLive`: an off market's price is unchanged, so the entry repeats it, and the chart stays flat rather than blank.

   The order matters: E5's withdrawal re-expresses prices after the reprice (E5 1.5 step 3), and E8's open-sol entry must hold the unit and number the new sol actually trades at.
- **Long jumps:** `Passed = min(SolNow - First, MarketRing)` (M:521-522) walks at most 8 sols; the ring marks the rest unknown, as section 3 (b) says. A jump of 30 or more leaves a ring of one known entry (the open sol) and 29 unknown.
- **The resync.** `FRHMarket` is `mutable` and rebuilt by `BuildMarketState` on every sync (M:221-335). The `bKeep` branch (M:289-305) copies `PriceMcr` and the tally when the slot counts match; it **must also copy the six `Hist*` fields** and extend the `bKeep` test to `HistPrice.Num() == N x 30` and the three value arrays to 30. Without that copy, `Debug_InjectRival` (a generation bump, M:337-355) empties the ring. The `else` branch starts the ring empty. MX15 pins this.
- **Harness.** `Debug_SetMarketHistoryEnabled(bool)` (runtime only, never saved, like `Debug_SetMarketEnabled` at H:1607) skips all three write points; this is the review's "runtime switch, not compiled out". `Debug_SetMarketPrice` (H:1610) changes the live price mid-sol and does not touch the ring, so after it the open-sol entry and the live price differ until the next roll; no `-market` check reads both across such a call.

**The frame.** `RHSaveFrame::HistoryTag = MakeTag('R','H','E','8')` in `RHSaveFrame.h` beside `MarketTag` (and E5's `'RHE5'`, E6's reserved `'RHL6'`). Written with `RHSaveFrame::Write` after the last frame then written (`'RHE4'` at W:13933 today; after E5's, E6's, D6e's and D7's frames as built), read with `RHSaveFrame::Read` after the last tail reader (`ReadMarketBlock` at W:14743 today, or E5's/E6's). Read after teardown: it repairs with a Warning and never refuses (R2-6). Payload, in order:
- `int32 SlotCount`, then the slot names ascending as `FString` (E4's layout, M:1286-1292), so the ring's slots can be checked against the market's;
- `int32 MarketCount`; per market in RegionId order: `int32 RegionId`, `int32 HistFill, HistHead, HistLastSol`, then `SlotCount x 30` prices (raw entry order 0..29, as E4 writes its ring, M:1304-1305), 30 sold values, 30 bought values, and once E5 exists 30 rates and 30 unit bytes.

It is sized by the tables, never by history, and measured by `GetMarketHistoryBlockBytes()` on the synced state as `GetMarketBlockBytes()` is (M:1354-1360). Size for one region and 18 slots: 4,320 + 480 = 4,800 bytes of ring, 12 of fill/head/last sol, 4 of region id, 4 + 4 of counts, about 200 of slot names (18 `FString`s), 20 of frame: **about 5.05 KB; about 5.3 KB with E5's rate and unit arrays.** The review's "5,068 B plus about 0.2 KB of slot names" holds.

**The complete list of const accessors E8 reads.**

*Existing at HEAD, unchanged* (H:1572-1602 unless noted): `IsMarketLive`, `IsMarketTraded`, `GetMarketPriceMcr`, `GetMarketTally` (grams; Back 1 = the sol just closed, M:424-438), `GetRivalTradedResources`, `GetMarketCheckedSol`, `GetMarketRegionCount`, `GetRivalEconomyCount`, `GetRivalEconomyName`, `HasRivalEconomy`, `GetRivalStoreKg`, `GetCreditReservedMcr`, `GetSpendableMcr`, `GetMarketSalesMcr`, `OrderLinesText`, `GetMarketClearings` (transient, at most 64), `GetMaxConvoysOut`, `GetMarketMaxStepBp`; `GetConvoys`, `GetConvoyProgress(const FRHConvoy&)`, `GetRivalRelation` (H:1754-1756); `GetSalesPaidMcr`, `GetEarthPriceMcrPerKg`, `DoesEarthBuy` (H:1530-1537); `IsRouteClosed` (H:1787), `IsRivalAvailable` (H:1823), `IsRivalDiscovered` (H:1826), `IsEmbargoed`, `HasDefected`, `GetEmbargoGrace` (H:1837-1839), `GetSabotageRemaining` (H:1825); `GetActiveEvent` (H:2567; a dust storm is `Type == "DustStorm"`, M:1114-1115); `GetBuildings` (the Lander, before E7); `FormatCredits` (H:1519); the definitions' `GetRival` and `ForEachRivalRow` (`RHDefinitionsSubsystem.h:105`, `:109`) for `DisplayName`, `DistanceKm` and `RegionId` (`RHRows.h:402`, `:405`, `:413`).

*New in H, all header-only or one-line const getters, beside the E4 reads:*
```
static constexpr int32 MarketHistSols = 30;
int32  GetMarketHistoryFill(int32 RegionId = 0) const;          // entries held, 0..30
int32  GetMarketHistoryLastSol(int32 RegionId = 0) const;       // the newest entry's sol (== the open sol after any roll); -1 empty
bool   GetMarketHistory(FName Res, TArray<int64>& OutPrice, int32 RegionId = 0) const;
       // OutPrice[k] is the price in force during sol LastSol - (Fill - 1) + k; -1 = unknown; false with no such market or slot
bool   GetMarketHistoryValue(int32 Back, int64& OutSold, int64& OutBought, int32 RegionId = 0) const;
       // Back 0 = the open sol (0 so far), Back 1 = the sol just closed, ... Back Fill - 1; false outside
int64  GetMarketHistoryRateMcr(int32 Back, int32 RegionId = 0) const;   // once E5 exists; 0 = unissued
uint8  GetMarketHistoryUnit(int32 Back, int32 RegionId = 0) const;      // once E5 exists; 0 credits, 1 marks
bool   GetMarketBandMcr(FName Res, int64& OutFloor, int64& OutCeil, int32 RegionId = 0) const;  // FloorMcr / CeilMcr, private today (H:3859)
double GetConvoyLegSols(FName Rival) const { return ConvoyLegSolsFor(Rival); }                  // private today (H:4045)
int32  GetMarketHistoryBlockBytes() const;
void   Debug_SetMarketHistoryEnabled(bool bOn);                                                   // runtime only
```
*E5's reads (design names; E5 design 8 declares the first four):* `GetRate`, `GetBlocMembers`, `GetMarkBalance`, `GetCurrencyBlockBytes`; and the four the review found missing, which E8 adds as header-only const reads if E5 as built lacks them: `IsCurrencyIssued()` (Earth's flag, 1.2) with `GetCurrencyIssueSol()` and `GetCurrencyOpenSol()` (the market's, 1.3), `GetRateInputsBp(int32& Trade, int32& Ident, int32& Tension)` (the last step's inputs, 3.3; the panel must never recompute them from the live axes, 3.2), `GetDeskBooked(int32 Sol, int64& ToMarks, int64& ToCredits)` (2.5 step 4's totals), `GetDeskOrders(TArray<FRHDeskOrder>&)` (7's list: Id, direction, amount, `ClearSol`, `bCleared`), and `GetMarkOrder(int32 ConvoyId, int64& ReservedMmk, int64& OwedMmk)` (7's mark orders; false for a credit convoy).
*E6's reads (design names, E6 design 8):* `GetLineMarkupBp(Rival, Res, Dir, Sol)`, `HasLivePact(Rival)`; and `GetPactCargoKg()` for the cap figure (E6 2.1's `PactCargoKg`, 2,000) if E6 exposes it no other way.
*E7's reads (design names, E7 2 and 9, after its review's change 5):* `GetRoutes(TArray<FRHRoute>&)` and `GetRouteFor(Rival, Out)` for `LengthKm` (keyed by the rival's name, Ids display only), `GetColonyDepotLocationCm(RegionId)` (the Lander's location under E7 Q-A (b); the origin with no depot, as V:4338). Before E7, V's own Lander rule (V:4338-4342) and `GetConvoyLegSols`.

Section 6 (a), Q-A (a) and REVISION R4 are withdrawn: under either answer `Source/RedHopeSim` changes and the dylib hash moves. The director chose (b), so the sim change is the ring above plus the getters.

**What the battery moves** (`Saved/baselines/battery_post_s2fix`, the lines as they print today):
- **The version lines** (every check that prints `RHSaveVersion` or the refusal text "expected 52485331/55"), 27 at HEAD: CV8 (x2), CR9 (x2), K6 (x2), DP11 (x2), EC6 (x2), DD9 (x2), XE10 (x2), FRAMING e5, FARICE F16, SH12, TM6 (x2), MX9 (x2: "MX9 v55: the market block is a frame" and "a v54 file is refused"), TR18 (x2), X15 (x2), TN1 and TN8; plus whatever version lines E5, E6, D6e and D7 add (CU10's at least). The literal "LoadColony v55:" and "v54:" texts inside Warnings are frozen at the version that wrote them (E4's code still says v55 after E8) and do not move; E8's own Warnings say "LoadColony v60:".
- **The whole-file size lines:** CV8 "(12059 -> 12139 bytes)", DP11 "(12087 -> 12119 bytes)", EC1 x2 "both saves are 13100 bytes", DC9 "the 13408-byte file is the same bytes", MX1 x2 "the saves (11761 bytes)" and "(11740 bytes)", MX9 "save, load, save is byte-identical (12173 bytes)"; plus any size line E5-E7 and D6e/D7 add. MX1's "up to the market block (8717 bytes)" offset does not move: the new frame comes after.
- **The new `-market` lines** MX12-MX16 (the test list below).
- Nothing else: `diff_battery.py` against the baseline the gate before E8 leaves shows those lines and wall-clock digits only; the pins "power: gen 50 W load 20 W" and "deposit Regolith_A: 40645 kg" hold.
- **MX1's tail compare** (MX:548-555) requires the on/off twins to differ from the market frame to the end of the file; E8's frame differs too (the off twin's prices sit at V), so it passes either way, but E5's REFRESH bounds it to the market frame's own length, and E8 bounds it itself if that has not happened.
- **The 110-sol playtest** (`RH_PLAYTEST_DISTRICTS=1 bash scripts/sim/rh_playtest.sh 110 e8_full guide full`) stays **byte-identical** to `Saved/baselines/ind_full_playtest.txt` (296,489 bytes) by `cmp`: the ring logs nothing in play; its only lines are load Warnings, and the playtest never loads a doctored file.
- **Checkpoints:** the bump refuses the Midgame and Late checkpoints; re-cut them (`rh_checkpoints.sh all --force`) and load both, as E5 7 says.

## R2-3. The unit before and after E5's issue (review change 3, blocking)

**E5's facts, by their design names.** `RateMcr` is milli-credits per mark, 1,000 = parity (3.1). The issue sets `CurrencyIssueSol = s` and `RateMcr = CurrencyRateStartMcr` (1,000); the currency opens at `CurrencyOpenSol = s + 2` (1.3). `bCurrencyIssued` is Earth's live flag from the act's execution (1.2). From `CurrencyOpenSol` on, E4's prices are read in milli-marks per kg and **their integers do not change** (1.3). A withdrawal re-expresses every price as `clamp(Price x RateMcr / 1000, floor, ceil)` and clears the rate and the issue sol (1.5). After the open, `ValueMcr` is in the market's unit for every priced order (REFRESH F1). The purses are Credits and Marks, the rate reads "1 mark = 1.06 cr" (section 9), the units are mcr and mmk, and the mark's name is a placeholder (section 9).

**E8's rule.**
- **The unit of sol t** is marks when the mark was issued in the market and `t >= CurrencyOpenSol`, else credits. Live: `IsMarketInMarks()` (E8 adds, header-only: `CurrencyIssueSol >= 0 && MarketCheckedSol >= CurrencyOpenSol`). Per sol: the ring's `HistUnit`, written at the open-sol write (R2-2 point 3, after E5's step 4, so a withdrawal's re-expression is already in).
- **Symbols.** Credits: `FormatCredits` and "cr/kg" (H:1519). Marks: "mk/kg", through E5's formatter if it builds one, else the same thousandths format. "mk" is Gate-D placeholder copy beside the placeholder name, in the wording block.
- **PRICES and DETAIL label the live unit:** "cr/kg" or "mk/kg", with the sub-heading "Prices in marks since sol N" after the open sol, and "The mark opens at the end of sol N; prices still in credits" between the issue roll and the open sol.
- **One series, in the live unit.** An entry in the other unit is converted at **its own sol's rate**: a mark entry shown in credits is `Price x HistRateMcr(t) / 1000`; a credit entry shown in marks is `Price x 1000 / HistRateMcr(t)`, and a sol with no rate (before any issue) converts at parity, 1,000. This is exact by E5's own construction: the integers do not change at the issue and the rate starts at parity, so the series is **continuous across the open sol** and shows no step; across a withdrawal the conversion is E5's own re-expression (less the band clamp), so no step there either. `MarketDivTrunc` takes every division (M:37). The 5-sol change, the sparkline's scaling and the tooltip's minimum and maximum are computed on the converted series. An unknown sol (-1) stays unknown.
- **The band** is `V x Floor / Ceil` in the market's unit: V keeps its number read in marks (E5 1.3), so the floor and ceiling lines carry the live unit's label and no conversion.
- **Earth's price is always credits.** "Earth pays 2.25 cr/kg", and while the market is in marks "(2.13 mk at today's rate)", `EarthMcr x 1000 / RateMcr`.
- **The trade balance** (2.3) values each sol in the unit it traded in (`HistSoldValue` and `HistBoughtValue` are recorded in the unit then) and shows the 8-sol sums in the live unit, each sol converted at its own rate, with a second figure in credits at today's rate while in marks (E5 9's "both balances"). "Earth has paid ... so far" is credits always.
- **OPEN ORDERS** labels each convoy's reservation and "owes you" in the order's own currency (E5 2.3 decides it at booking): `GetMarkOrder(Id, ...)` true means marks ("set aside 362 mk", "owes you 200 mk"), else credits.
- **The exchange-rate row** (2.4) is hidden until the issue roll. From the issue roll on: "1 mark = 1.058 cr" (`GetRate() / 1000`, three decimals), the last step's sign (the ring's rate for the open sol against the sol before), a 30-glyph sparkline of `HistRateMcr` (a sol with rate 0 draws a space), and "opens at the end of sol N" until the open sol; the inputs line reads `GetRateInputsBp` (the record, never the live axes); the bloc line lists available members; the desk line reads `GetDeskBooked(MarketCheckedSol, ...)` against `DeskCapPerSol`. A withdrawal hides the row again.
- **Before E5 builds,** the unit is credits everywhere, `HistRateMcr` and `HistUnit` are not written or saved, and the row is not built (section 7's rule).

## R2-4. The price row at the corrected widths (review change 4)

**The numbers.** Mono 9 renders at 12 ppem (Slate's 96 dpi); U+2581-2588 come from `DroidSansFallback.ttf` at 154/256 em = 7.22 px, so **30 glyphs are 217 px**. The panel body is **290 px**: 340 (`RightColumnPx`, D:38) less 24 of panel padding (`FMargin(12, 9, 12, 10)`, `RHDeckStyle.cpp:161`), less 18 for the sub-card's 9 a side (the rival card's, K:2345), less the 8 px scroll bar (D:866).

**The row** is one flat `SButton` (tagged `RH.MarketRow.<Res>`) holding two lines:
- **Line 1:** `ResourceIcon` (14 px) + 4 px + `ResourceLabel` in `Body(9)` (FillWidth, the short form) + the price right-aligned in `Value(9)`, "12.345 cr/kg" (about 70 px). About 200 px of 290 at the longest short label.
- **Line 2:** the 30-glyph sparkline in Mono 9 `Sky` (217 px) + 8 px + the 5-sol change in `TextDim` `Body(8)`, "+12.3%" (about 36 px), right-aligned: 261 px of 290.

A row is about 34 px tall; seven rows about 240 px. The DETAIL chart is three rows of 30 glyphs, 217 px, with the band, Earth and holdings lines under it. The whole panel is about 770 layout px open, under the ~1,014 visible at 1440x860 once the accordion scrolls it to the top.

**No clipping.** The 1440x860 capture must show every row on two clean lines. If one wraps, the sparkline drops to 24 glyphs (173 px), never the text; the label gets `ETextOverflowPolicy::Ellipsis` as a last resort, with the full name in the tooltip. Risk 1 is corrected to these numbers; Q-B (a)'s "about 162 px" is withdrawn.

## R2-5. The click-test steps, rewritten so each can fail (review change 5)

**Mechanics.** `LocateButton` matches a visible, on-screen `SButton` by text or **tag** (`HasText` or `HasTag`, CT:570-577; `HasTag` CT:181-186), so every new button carries an `FTagMetaData` and is located by it. `Gather` keeps only widgets whose centre is inside every clip (CT:111-129), so a folded panel's rows are never found: MARKET must be open before its rows are pressed. `PushCheck` polls `Effect()` until it returns empty or the timeout passes, then reports (CT:900-928). The deck gains `Debug_` const getters so each check reads what the panel built, not the sim alone: `Debug_GetMarketRowCount()`, `Debug_GetMarketSparkGlyphs(Res)` (drawn, non-space glyphs), `Debug_GetMarketSelected()`, `Debug_GetMarketDetailText()`, `Debug_GetMarketOrdersText()`, `Debug_IsMarketRateShown()`, `Debug_GetMarketRegionButtons()`, `Debug_GetMarketRouteCount()`, `Debug_GetMarketRouteChip(Rival)`, `Debug_GetMarketChipColours(...)`, `GetOrderDraft(Rival)` (reads `OrderDrafts`, Dh:322); the trade visualizer gains `GetDrawnRouteCount()` and `GetRouteStatus(Rival)`; the controller gains `IsTradeOverlayOn()`.

**The steps, in order.** The E4 block (`market.setup` at CT:1546, which opens Politics and grants fuel, parts and credit) stays where it is; the new steps sit around it, all before `traffic.on` (CT:1575).

1. **`market.empty`** (before `market.setup`; the panel is shown on a new colony: Zarya is active with an economy). `Runner.Do` opens MARKET through `SetFoldOpen(EFold::Market, true)` (the harness path `city.setup` uses, CT:1589). One check, naming the first failure: `Debug_GetMarketOrdersText()` equals "No orders out"; `Debug_IsMarketRateShown()` is false; `Debug_GetMarketRegionButtons()` is 0 (one region); the Ice row's change text is empty (fewer than 6 points: the open sol is 0 or 1).
2. **`market.setup`, `market.BUY`, `market.SEND`** as today, with **`market.BUY`'s check fixed:** `GetOrderDraft(Zarya)` has exactly one non-zero entry, it is +100, and its key is Hydrogen (the card sorts by name, K:2522, so the first "Buy +100" is Hydrogen's). The report names the key and the amount.
3. **`market.panel`.** Press `RH.MarketToggle`. Check (1 s): `IsFoldOpen(Market)` true and Politics, Crew, City, Fleet and Power all false; `Debug_GetMarketRowCount()` equals the count R2-1's rule computes in the check from the sim (available economy rivals' traded slots) **and** equals 5. The report prints both counts.
4. **`sweep.market`.** `PushSweep(TEXT("sweep.market"))`: a second REACH sweep with MARKET open and scrolled to the top, so the toggle and the price rows are on screen; it fails on any unreachable visible button. (`sweep.deck` at CT:1362 runs with MARKET folded and never sees them.) The region selector is not on this colony (one region) and is **not** covered by the click test; `e8_regions_00` covers it by eye.
5. **`market.orders`.** Check: the ORDERS section has exactly `GetConvoys().Num()` rows (1 after `market.SEND`), the row names Zarya, and its state text contains "clears at the end of sol N" with N = the convoy's `ClearSol`.
6. **`market.history`.** `Runner.Do`: record `G0 = Debug_GetMarketSparkGlyphs(Ice)` and `F0 = GetMarketHistoryFill(0)`, then one `Debug_RollMarket(GetMarketCheckedSol() + 3)`, which walks three sols in one call (M:521-522, `Passed = 3`). Check (1 s): `GetMarketHistoryFill(0) == min(30, F0 + 3)`; `GetMarketHistoryLastSol(0) == GetMarketCheckedSol()`; the open-sol entry equals `GetMarketPriceMcr(Ice)`; `Debug_GetMarketSparkGlyphs(Ice) == min(30, G0 + 3)` after the deck's per-sol rebuild. The market then runs ahead of the clock for the rest of the test, harmlessly: the sim's own `RollMarket` returns at M:514-517 until the clock passes it, and no later step reads the open sol.
7. **`market.select`.** Press `RH.MarketRow.Struct`. Check: `Debug_GetMarketSelected() == Struct`; `Debug_GetMarketDetailText()` contains `ResourceLabel(Struct)`'s full form ("Structural Materials", `RH_Resources.csv:5`) and the exact band text built from `GetMarketBandMcr(Struct)` with `FormatCredits`.
8. **`market.discover`.** `Runner.Do`: call `Debug_MaybeDiscoverSettlement(k)` for k = 1, 2, ... until it returns true, at most 64 times. Check: `IsRivalDiscovered(Meridian)`; `Debug_GetMarketRowCount()` equals the rule's count **and** 7; `Debug_GetMarketRouteCount()` is 2; Meridian's card is visible. A loop that never discovers fails with "no survey count up to 64 discovered Meridian".
9. **`trade.overlay`.** Press the overlay button **by tag `RH.TradeOverlay`**, never by text: "Trade" would match each rival card's TRADE verb (K:2394) under `Equals(IgnoreCase)` (CT:174). The button's label is **Routes** / **Routes [on]**, so the player sees no clash either. Check (1 s): `IsTradeOverlayOn()` and `GetDrawnRouteCount()` equals the number of available rivals counted in the check (2 after step 8).
10. **`trade.overlay.status`.** `Runner.Do`: `Debug_SetEmbargo(Zarya, 5.0)` (H:1618). Check (1 s, the visualizer polls at 2 Hz): `GetRouteStatus(Zarya) == Embargo`, `GetRouteStatus(Meridian) == Open`, and `Debug_GetMarketRouteChip(Zarya)` reads "embargo, 5.0 sols to reassure". Then `Runner.Do` `Debug_ClearPolitics()` (H:1621) and a second check: both Open and the chip reads "open".
11. **`trade.overlay.off`.** Press `RH.TradeOverlay` again. Check: `!IsTradeOverlayOn()` and `GetDrawnRouteCount() == 0`.
12. **`market.chips`.** `CheckDeckContrast` compares only `SButton`s with a filled brush (CT:1271-1306); the ROUTES chips are `SBorder`s and the price rows are flat, so it checks none of E8's widgets. This check walks `Debug_GetMarketChipColours()` (fill and text per chip) and asserts `|Luminance(fill) - Luminance(text)| >= 0.4` for each, reusing the test's `Luminance`. Section 5.7's claim is dropped.

**Counting.** Twelve new reports (`trade.overlay.status` is two), `market.BUY` fixed in place. 86 at the design's HEAD becomes **98**; the builder re-counts at its own HEAD (the visual pass's uncommitted `RHClickTest.cpp` and E5-E7 may add steps) and states the total in the commit. `leak.total`'s deliberate count is unchanged. Three clean runs in a row at 1440x860 (UI polish P1).

## R2-6. The ring checked against E4's state on load (review change 6)

`ReadMarketHistoryBlock(FArchive&, const TArray<uint8>& File)` runs straight after `ReadMarketBlock` (W:14743) and after E5's and E6's readers once they exist, so E4's **repaired** state is final when it runs. It never refuses and logs Warnings only (no Error), as E1-E4 (M:1362-1370). In order:
1. **Find the frame.** `RHSaveFrame::Read(Ar, File, HistoryTag, ...)`. Lost: "LoadColony v60: the market history is lost (%s); the history starts empty", every ring empty.
2. **E4's block was lost** (E5's `bMarketBlockLost`, set in the `!bFound` branch at M:1383-1386 and where the slot table drops the market, M:1519-1524): the ring is dropped even when its own frame is intact, with "the market history is dropped: the market block it describes was lost". The prices it would describe were reset to the Earth value and the open sol re-seeded (MX9's lost-block line), so an intact ring would end at sols and prices that no longer exist.
3. **Parse.** Slot count in 0..`MktMaxList`; names through `RHSaveFrame::ReadName`, ascending and unrepeated; market count in 0..64; per market fill in 0..30, head in 0..29, last sol >= -1 and -1 exactly when fill is 0; the arrays sized by the slot count; nothing left over. Any failure: "the market history is malformed (%s); the history starts empty".
4. **Against E4's loaded state:** (a) the slot table equals the live `EconSlots` names exactly: E4 skips a stale slot with a Warning (M:1509-1517), but the history drops the whole ring on any difference, since a shifted slot would mislabel every price; (b) a region with no live market drops that region's ring; (c) `HistLastSol == MarketCheckedSol` (the open sol after E4's own repair, M:1695-1704), else that ring is dropped with "the history ends at sol %d but the market's open sol is %d"; (d) a known price outside its slot's band `[FloorMcr, CeilMcr]` is clamped with a Warning; (e) the open sol's entry is overwritten with the loaded `PriceMcr` of every slot, with a Warning naming each slot that differed (E4 may have clamped a price at M:1678); (f) once E5 exists: a unit of 1 on a sol before `CurrencyOpenSol`, or any unit of 1 when E5's block was lost (the currency starts unissued, E5 7), is reset to 0 with its rate, with a Warning.
5. The ring is then assigned into the synced markets; a later resync keeps it through `bKeep` (R2-2).

`Debug_SetMarketHistoryEnabled` never touches the loader.

## R2-7. The frame bar, with how each number is measured (review change 7)

**Instruments.**
- `RHFrameStats` gains a fourth slot, `Market` (`RHFrameStats.h:11`, `SlotCount = 4`), wrapping two ticks in `FScope`: the deck's market rebuilds (`SRHCommandDeck::Tick` calls `RefreshMarketPanel()` beside `RefreshTramList`, D:1335-1343) and `URHTradeVisualizerSubsystem::Tick`.
- `RH.FrameStats <Seconds> [Tag]` (`RHCheats.cpp:2086-2135`) prints per-slot averages in ms a frame (`:2081-2084`) and the slot costs of frames over 150 ms (`:2119-2122`). The `[FrameStats]` line gains "market tick %.3f" beside the section's, and **"market worst rebuild %.2f ms"**, read from a `WorstRebuildMs` on the deck's market rebuild and on the visualizer, the way the section keeps its own (`RHSectionVisualizerSubsystem.cpp:843-844`, printed at `:622`); both are reset when a window starts, and the spike lines include the market slot.
- Each window is **15 s**, the final build, TSR as shipped, at **1440x860 and 1600x900**, the clock at 1x after a 5 s settle.

**Scenes.**
- **S1, the market scene:** NEW COLONY; `RH.CityBench 2000`; `RH.Market discover` (a new sub-command: `Debug_MaybeDiscoverSettlement` looped as R2-5 step 8); `RH.AddStock Hydrogen 200`; `RH.AddStock SpareParts 20`; `RH.Market grant 10000` (new: `Debug_GrantCredits`; no console cheat grants credit at HEAD, the click test calls the hook from C++ at CT:1554); `RH.Market order Zarya Ice:300;Struct:-100`; `RH.Market order Meridian Food:100` (one convoy to each rival, H:1748); `RH.Floor 0`. Two convoys are then out and the market is live. The sub-commands are cheat code in `RHCheats.cpp`, not sim rules.
- **S2:** `RH.DeepBench 3000` (`RHCheats.cpp:1746`), then `RH.Floor 0`, so the overlay is drawn (it hides below ground).

**Runs, in adjacent pairs** (D8's rule, `design-decisions.md:1206`: the bench drifts over minutes, so off-then-on seconds apart): for each scene and resolution, **P0** MARKET folded and the overlay off, then **P1** MARKET open (scrolled to the top, Ice selected) and the overlay on.

**The bar.**
1. P1's median and p95 are each within **1.0 ms** of P0's, in every pair (D8's leave-and-return precedent, `design-decisions.md:1215-1222`, where the noise was +0.11 and -0.19 ms).
2. The `Market` slot averages at most **0.2 ms a frame** in P1, and the worst rebuild in any window is at most **2 ms**.
3. The cache key for the panel's strings is `(GetMarketCheckedSol(), the region, the selected resource, a reload counter from OnColonyReloaded)`: the clearing count is dropped (it stops at 64, M:24 and M:716-719). Order and route rows rebuild on their change keys (convoy Ids and states; each route's status chip; the availability mask).
4. A missed budget is a finding for the director, not a budget edit.

## R2-8. The accordion: MARKET joins it (Q-C (a), review change 8)

- `EFold` becomes `{ Politics, Crew, City, Fleet, Power, Market, Count }` (Dh:136). `FoldFlag` gains `case EFold::Market: return &bMarketExpanded;` **before** the `default:` (K:227-237): the default returns Power's flag, so a missing case would silently fold Power instead. `bMarketExpanded = false` joins the fold bools (Dh:386-387); `Debug_ExpandAll` (Dh:29) sets it too. `FoldAnchors[(int32)EFold::Count]` (Dh:374) grows by itself. The panel is wrapped in `FoldAnchor(EFold::Market, BuildMarketPanel())` straight after Politics's slot (D:885), and its header carries `FoldToggle(EFold::Market, FName("RH.MarketToggle"))` as Power's does (K:2703). `SetFoldOpen` (K:263-281) then folds the others and scrolls it into view unchanged.
- **The trade buttons stay on EARTH & NEIGHBORS:** TRADE (K:2394), Buy +100 and Sell +100 (K:2564, K:2571), SEND ORDER (K:2613) and CLEAR (K:2627) are on each rival card. E5's desk buttons stay there too (E5 9).
- **The press count for a look-and-act:** open MARKET (1), open EARTH & NEIGHBORS, which folds MARKET (1), the nudges (n), SEND ORDER (1), back to MARKET (1): **n + 4**, two more than if both could be open, which the director accepted ("about 2 extra presses").
- The header summary stays readable while folded, so the price count or balance needs no press. The selected resource and the region survive a fold (deck state, unsaved). Section 1's "so the neighbours and their market sit together" now means adjacent in the column, not open together.

## R2-9. The overlay as a pure listener (review change 9)

`URHTradeVisualizerSubsystem` (RedHope, a `UWorldSubsystem`) reads const getters only and writes nothing to the sim. Its state is its mesh, its key and its on flag.
- **The named edits to V** (the colony visualizer; none to the sim):
  1. `RivalMarkerPos(FName, float)` moves above `private:` (Vh:216; it is at Vh:259) as a public const read, unchanged (V:4193-4204).
  2. `ShowsSurface()` (Vh:238, `!bSectionMode && !IsUnderground()`) becomes a public const read. The markers hide with `!bSurface` where `bSurface = ShowsLevel(0)` (V:4216, V:4264); on the surface the two are the same test, so a ribbon is never drawn while its marker is hidden, and the section view hides both (REVISION R3).
  3. **The marker's status word is V's.** `AddLabel` sets the text once at spawn (V:4243; a `UTextRenderComponent`, V:1334-1340). `RefreshSovereigntyVisuals` gains a per-marker word, diffed like `RivalMarkerState` (V:4247-4263) over the chip order of 2.6 (defected, closed, embargo, disrupted, storm, open), and rewrites the label to "Zarya Station · embargo" when the word changes while the trade overlay is on (V reads `URHTradeVisualizerSubsystem::IsOverlayOn()`), and back to the name alone when it turns off.
- **The key** is a CRC over, per available rival in name order: the name, the status chip, `RivalMarkerPos`, and the depot location (`GetColonyDepotLocationCm(0)` after E7; the Lander's `LocationCm` from `GetBuildings()` before it, V:4338-4342's rule), plus `ShowsSurface()`. It is polled at **2 Hz** (TRAFFIC's `PollSeconds = 0.5`, T:22) and the mesh is rebuilt only when the key moves, never per frame.
- **The mesh:** one `UProceduralMeshComponent`, one section per colour (`Teal` open, `Amber` embargo or disrupted, `Red` closed or sided with Earth, `Sky` storm), built as TRAFFIC's `RebuildOverlay` is (T:537-627: a strip of quads at `OverlaySampleCm`, `CreateMeshSection_LinearColor` per bin, `ClearMeshSection` when empty), 3 m wide, draped on `RHMarsTerrain::GroundZCm + 40 cm` every 10 m. About 50 quads for today's two routes (236 m and 260 m, V:4201).
- **The switch:** `ARHPlayerController::ToggleTradeOverlay()` / `IsTradeOverlayOn()` mirror `ToggleTrafficOverlay` (`RHPlayerController.cpp:2188-2194`); the command-bar button beside Traffic (D:1177-1182) is a `DeckButton` with the tag `RH.TradeOverlay` (the helper at D:47-59 gains an optional tag), labelled **Routes** / **Routes [on]**; console `RH.Trade overlay [0|1]` beside the existing `RH.Trade` log (`RHCheats.cpp:1059`), as `RH.Traffic overlay` is (`:2749`).
- **The legend:** `BuildTradeLegend()` in the TRAFFIC legend's slot (D:777-784), under the traffic legend when both are on: "Trade routes: teal open · amber embargo or disrupted · red closed · blue halted by a storm".
- **Convoy rovers** stay V's (V:4288-4360).

## The save version

HEAD is **v55** (W:37). The queue after the visual pass: E5 **v56**, E6 **v57**, D6e and D7 **v58 and v59** in whichever order they build; E7 takes **no** version under its Q-A (b) (no building, no fence). So E8 takes **v60** if the queue builds in that order, and in every case the next free version at build time. The `RHSaveVersion` comment (W:37) gains: "v60: the market history - per region a 30-sol ring of the price in force during each sol (in the market's unit then), the value sold and bought at that sol's clearings, and (E5) the rate and unit, with its fill, head and last sol, in its own frame ('RHE8') after the last frame then written; sized by the tables (E8)". The strict check stays (W:13968-13971).

## Tests (the complete list)

**`-market` checks** (new, in MX after MX11; the battery's run count does not change):
- **MX12, the ring by hand.** On MX2's fixture (`Debug_SetMarketTally`, `Debug_SetRivalStore`), after `Debug_RollMarket` of 1 sol, of 3 sols, and of a 12-sol jump: fill, head and last sol; the open sol's entry equals the live price for every slot; the walked sols' entries equal the prices MX2's reference computed; the four sols the jump skipped read -1 with 0 values; the values at each closing sol equal the sum of `GetMarketClearings()`'s fills times the price in force (the `SellsValue` / `BuysValue` split); `GetMarketHistoryValue(0)` is 0, 0 and `Back = Fill` is false; a market turned off (`Debug_SetMarketEnabled(false)`) still writes the open sol's entry, equal to the unchanged price.
- **MX13, band-exact.** MX8's paired run (the start save `marketmx8`, 20 sols, nine orders to two rivals, MX:1690-1850): the era and agent twins' rings are equal entry for entry at every threshold (prices, values, fill, head, last sol), and the fallback twins too. Every convoy in the run names an available rival.
- **MX14, write-only.** From `marketmx8`, twins with `Debug_SetMarketHistoryEnabled` on and off, 20 sols in each band: MX1's digest is identical; the saves are byte-identical except inside the `'RHE8'` frame (the off twin's ring is empty); after 40 sols the on twin's fill is 30 and its last sol is the open sol.
- **MX15, the save.** The frame is `'RHE8'`, after the last frame, its payload bytes equal `GetMarketHistoryBlockBytes()`, the slot names ascending, per region fill, head and last sol then the arrays; save, load, save is byte-identical; the previous version is refused; a lost `'RHE8'` frame starts empty with a Warning while the market loads intact; a lost `'RHE4'` with an intact `'RHE8'` drops the ring with the E4-lost Warning; a doctored last sol loads with the ring dropped; a doctored slot name drops the ring; a doctored price outside the band is clamped; an open-sol entry that disagrees with the loaded prices is rewritten with a Warning naming the slot; a resync (`Debug_InjectRival`) keeps the ring; an injected region-1 rival gives two rings that move independently; no Error line is logged by any repair.
- **MX16, mutations** (each built, run and restored by hash; each must fail the check named): (1) the values written before the clearing (MX12: a sol late); (2) the open-sol entry written before the reprice (MX12: the open sol's entry equals the closed sol's price); (3) the `bKeep` copy omitted (MX15's resync case: the ring empties); (4) the last-sol check skipped on load (MX15's doctored last sol loads); (5) the reprice reading the ring (MX14: the digests differ); (6) a jump's skipped sols filled with the last pass's price instead of -1 (MX12's jump case); (7) once E5 exists, the open-sol entry written before E5's withdrawal step (a unit and price that disagree with the re-expressed price at the withdrawal roll).
- **`-scale`** (outside the battery): 1,000 `RollMarket` calls with the ring written: at most **0.1 ms** more than the figure measured just before E8 builds (E4 measured 0.746 ms; E5's bar is 1.246 ms). Every digest identical.

**The battery.** `bash scripts/sim/rh_battery.sh 10`: `BATTERY COMPLETE: 0 suite(s) not green`, logs newer than both dylibs; `diff_battery.py` against the baseline the previous gate left shows only R2-2's version lines, size lines, the MX12-MX16 lines and wall-clock digits; both pins hold.

**The playtest.** The 110-sol run byte-identical to `Saved/baselines/ind_full_playtest.txt` by `cmp`.

**The click test** (`RH_CLICKTEST_RES=1440x860 bash scripts/unreal/rh_clicktest.sh`): R2-5's steps, three clean runs in a row, the total stated.

**The frame check:** R2-7's pairs, both scenes, both resolutions, the `[FrameStats]` lines kept in the gate's log.

**Captures** (`scripts/unreal/rh_capture.sh`, 1440x860, plus 1600x900 where marked; `Saved/RHCapture/e8/`; looked at, as `e4_card_00.png` was):
- `e8_panel_00`: S1 run 20 sols at 60x (about 7 minutes), MARKET open: 7 rows on two clean lines each, the sparklines filled;
- `e8_detail_00`: Ice selected, the 3-row chart, the band, Earth's price, the holdings;
- `e8_balance_00`: the 8-sol balance after both convoys clear;
- `e8_routes_00`: one route embargoed (`Debug_SetEmbargo` through `RH.Market embargo Zarya 5`, a cheat sub-command);
- `e8_overlay_00` (and 1600x900): Routes on, both ribbons with Zarya's amber, rovers on the road, the legend, Zarya's label reading "· embargo";
- `e8_overlay_section_00`: the section view (`RH.Section on`): no ribbon, no marker;
- `e8_regions_00`: a second region through `Debug_InjectRival` with `RegionId = 1` (via `RH.InjectRivals` if it can set the field, else a cheat edit logged in the gate): the selector drawn, the rings independent;
- `e8_rate_00`: once E5 exists, the rate row after an issue and its open sol, the series continuous across the open (E5's harness).

**Wording.** Every new string goes into `docs/qa/2026-10-15/gate-d-wording.md` as "Rows added by E8", W-rows in E4's style (`:528`): the panel's headings and lines, "Routes" / "Routes [on]", "mk/kg", the chip words, the legend, the marker's status word. None is morale, sickness or evacuation wording.

**Deviations to log in `docs/design-decisions.md`.**
1. A write-only, saved 30-sol market history (v60), against the plan's "no sim or save change" (the director's Q-A (b)).
2. The ring's newest entry is the open sol, written at the roll that opens it, so the load check is `HistLastSol == MarketCheckedSol`, not `- 1`.
3. `market.BUY`'s empty check is fixed in this gate.
4. The trade balance covers the market's eight sols, not thirty.
5. The overlay button is **Routes**, not Trade, to avoid the TRADE verb.
6. The region selector is covered by MX15 and a capture, not by the click test, until a two-region fixture exists in it.

**Not resolved here.**
- E5's, E6's and E7's accessors are design names; the builder rebases each.
- The click-test total is 86 at the design's read; the working tree's `RHClickTest.cpp` is modified by the visual pass and uncommitted, so the builder re-counts.
- The widths are computed from the font files and the engine, not measured; the 1440x860 capture is the measurement.
- No console cheat grants credit at HEAD; the frame scene needs `RH.Market grant`, cheat code only.
- Whether `RH.InjectRivals` can set `RegionId` for `e8_regions_00` is unchecked.

# RE-CHECK (2026-10-26)

Reviewer note: a short, independent re-check of REVISION 2, done together with E7's REVISION 2. Code was read at HEAD `3909395` with `git show HEAD:` only. Battery facts come from `Saved/baselines/battery_post_s2fix`. I built nothing and ran nothing in the repo.

**Verdict: CLEARED WITH CONDITIONS**

## What holds

- **All nine review changes are written in.**
- **The discovery gate is complete.** One rule, `ShownMarketSlots`, gates every part the review named:
  - the price rows;
  - the "N prices" header count;
  - the holdings sum and its tooltip;
  - the bloc line;
  - the region selector;
  - ROUTES and the overlay;
  - the marker's status word.

  ORDERS needs no gate, because `PlanConvoy` refuses an unknown rival (M:868). `IsRivalAvailable` short-circuits before `Defs->GetRival(Name)->RegionId` is read, so no null row is dereferenced.
- **The ring's write points match `RollMarket`.** They are inside the pass loop (M:526-552). The clearing is the only place `ClearConvoy` runs (M:634). The value sums sit at M:668 and M:676, and the reprice at M:545-550.
- **The accordion's fold case goes before `default:`.** That is required, because the default returns Power's flag (K:235).
- **The overlay is a sim-side pure listener.** The edits to V are named.
- **The version-line count is right.** There are 27 version lines in the baseline (excluding E4's 47 frozen Warnings), and the "LoadColony v55:" texts are literals (M:1385 and others).

## Remaining required changes

1. **Do not extend E4's `bKeep` test** (R2-2, line 634).
   - `ReadMarketBlock` builds its markets with `PriceMcr` and the tally only (M:1534-1538, `Markets.Add` at M:1566). It then calls `SyncRivalEconomies(false, true)` at M:1668, before the history reader runs.
   - With `HistPrice.Num() == N x 30` added to `bKeep` (M:286), every load fails `bKeep` and resets every price to V and every tally to 0. MX9's round trip would go red.
   - **Change:** keep M:286 as it is. Copy the `Hist*` fields under a separate test (`bKeep && OldMk->HistPrice.Num() == N x 30 && ...`), else start the ring empty and sized.
2. **An empty ring must pass the load check, and `market.history` must allow for it.**
   - **Why the ring is empty.** `MarketCheckedSol` is seeded from `ProductionCheckedSol` (M:97), and the first roll is at sol 1 (M:516). So every save made before then has fill 0 and `HistLastSol` −1.
   - **The load check would fire.** R2-6 4(c) (line 733) compares `HistLastSol` with `MarketCheckedSol`, so it Warns on such a load. **531 of the battery's 765 loads are at sol 0**, in 12+ suites (caverns, credit, deep, economy, export, shafts, transit, market, ...). Those Warning lines are not in R2-2's list, and any suite that asserts zero Warnings fails.
   - **The click step would fail.** In `market.history` (line 717), if the ring is still empty (F0 = 0), `HistEnsure` writes the open sol and then three more, which is +4, not +3.
   - **Change:** state that a ring with fill 0 and last sol −1 is valid at any open sol (4(c) applies only when fill > 0). Have `market.history` expect `F0 == 0 ? 4 : min(30, F0 + 3)` and the matching glyph count. Or seed the open sol's entry when a market is created, so the invariant always holds.
   - Also, `HistEnsure` from −1 (line 628) appends sols 0..s. For a ring started or dropped mid-game, that is up to s writes and 29 invented "unknown" entries. Start an empty ring at s.
3. **The "nothing else" battery claim (line 673) is false.**
   - MX9's doctored loads trigger E8's repairs, which add Warning lines to market.log:
     - the lost market block (MX:1385) drops the ring under R2-6 step 2;
     - the price-band file (`mktmx9d1`, MX:1453-1454) rewrites the open-sol entry for Ice and Struct under 4(e);
     - the slot-order file (`mktmx9d4`, MX:1465-1466) drops the ring.
   - MX9 still passes, because its checks are `CountWarnings(x) >= 1` (MX:1443). But the expected diff must list these lines.
4. **The version is stale.** Line 779 says "E7 takes **no** version". E7's REVISION 2, section 8, now writes `'RHE7'` and takes v60.
   - If E7 keeps that block, E8 is **v61**. Then lines 612, 670, 730, 779 and 812 change, `'RHE8'` is written and read after `'RHE7'`, and E8 must edit E7's RR6 if it asserts `'RHE7'` is last.
   - If E7 drops the block (E7's RE-CHECK recommends this), v60 stands as written.
5. **The accessors must not duplicate E7's.**
   - `GetConvoyLegSols` (line 659) is E7's (E7 REVISION 2, line 783; there `ConvoyLegSolsFor` delegates to it, the reverse of line 659). Mark it "E7 adds".
   - Drop `GetMarketBandMcr` (line 658) and read E7's `GetMarketFloorMcr` and `GetMarketCeilMcr` (E7 line 786). Two APIs for one private field is a needless second path.
   - The region selector (line 598) needs E7's `GetMarketRegionIdAt` (E7 line 787). Name it.
   - Use `GetRivalRegionId` in `ShownMarketSlots`, as E7's deck read does (E7 section 11).
   - Re-cite E7 as "REVISION 2 sections 6, 7 and 13", not "E7 2 and 9" (line 665). The "Before E7 ... `GetConvoyLegSols`" clause is moot.
6. **Size lines.**
   - **State the exact delta** relative to the gate before E8, not today's absolute numbers. The frame is +**5,327** bytes for a one-region file with E5's rate and unit arrays, or 5,057 without them:
     - 4 (slot count) + 213 (18 names as `FString`, 4 + len + 1 each, M:1288-1292) + 4 (market count);
     - then, per region: 4 + 12 + 4,320 + 240 + 240, plus E5's 240 + 30;
     - plus 20 for the frame.
   - **The list is incomplete.** Add CR9 (11892), EC6 (13100) and XE10 (11817). They are masked by `diff_battery.py`'s `\(\d+ bytes`, but they move.
   - **E7's lines.** If E7 keeps its block, also move `-regionready`'s version lines.
7. **The unit claim is overstated** (line 686).
   - E5 steps the rate at every roll after the issue roll (E5 design:253). So at `CurrencyOpenSol` the rate is no longer parity.
   - Converting pre-open credit entries "at their own sol's rate" then puts a step, up to two rate steps (about 4%), at the open sol. Meanwhile the price integers do not change (E5 1.3).
   - **Change:** convert pre-open entries at parity, which is E5's own reading, or withdraw "continuous ... shows no step".
8. **Use V's own predicate for visibility.** `ShowsSurface()` is at Vh:237, not :238, and it is not the predicate the markers use: they use `ShowsLevel(0)` (V:4216, V:4264). Expose and use the same one, so "a ribbon is never drawn while its marker is hidden" holds by construction.

## Cross-consistency with E7 (the main point of this re-check)

- **Regional V.** No line of E8's REVISION 2 relies on E7 making V regional.
  - The phrase exists only in the INDEPENDENT REVIEW (line 411), which REVISION 2 supersedes.
  - R2-3 says V keeps its number (line 687). R2-6 4(d) clamps to `[FloorMcr, CeilMcr]` as built.
  - The band is read through a getter, not derived from `EarthValue_mcr`, which is right under Q-B (b).
  - The one fix is change 5 above: read E7's band getters instead of declaring E8's own.
- **The version, in build order.** E5 v56, E6 v57, D6e v58, D7 v59 (their refreshes say so: `d6-caverns-design.md:820-824`), then E7, then E8.
  - **E7 keeps `'RHE7'`:** E7 says v60 and E8 must say **v61** (change 4).
  - **E7 drops it (recommended):** E7 says "no version" and E8 keeps v60.
  - Today the two documents contradict each other: E7 line 802 says v60, and E8 line 779 says v60 because "E7 takes no version".
- **Size deltas.** E7's are relative, +28 against the gate before E7, with today's figures in brackets for identification only. E8 states none (change 6).
  - Nothing is double-counted, but neither list is complete: both lack CR9, EC6 and XE10.
  - Both frames come after `'RHE4'`, so MX1's "up to the market block (8717 bytes)" offset moves in neither.
- **Accessors.** Each E7 name E8 reads has the same name and signature in E7 REVISION 2 section 7:
  - `GetRoutes(TArray<FRHRoute>&)`;
  - `GetRouteFor(FName, FRHRoute&)` (keyed by `FRHRoute::Rival`, with `LengthKm` a double);
  - `GetColonyDepotLocationCm(int32 RegionId = 0)`;
  - `GetConvoyLegSols(FName)`.

  E8 redeclares `GetConvoyLegSols` and invents `GetMarketBandMcr`; both must defer to E7 (change 5). E8 does not use `GetRivalProduceG` or `GetDepots`, which is harmless. E8 should call `GetMarketRegionIdAt` and `GetRivalRegionId` by name.
- **Frame order.** If E7 keeps its block: `'RHE8'` comes after `'RHE7'`, E8's reader runs after E7's, and E7's RR6 must not pin `'RHE7'` as last. XE10's "market frame ends the file" clause (`RHSimExportSuite.cpp:531`) was missed by E5's REFRESH. It breaks at the first frame appended after `'RHE4'`, so it will already have been fixed before E7 or E8 builds. Each builder confirms that at its own HEAD.
