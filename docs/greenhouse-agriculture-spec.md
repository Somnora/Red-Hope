# Greenhouses & Agriculture — working spec (2026-07-10)

Director brief (live session): full greenhouse buildout — structures above and
below ground, drag-sized; crop variety with visible growth stages; soil and
fertilizer economy; per-greenhouse climates; drawable air ducts exchanging
CO2/O2 between gardens and habs.

## What already exists (build on, not around)
- Garden + Greenhouse ROOMS: player-designated cell zones, auto-plant at
  soil+seeds cost, yield/sol, planted-state visuals (planter_dry -> _wet),
  grow-light power draw (GardenGrowLightWPerCell), Greenhouse = glass-glazed
  shallow-floor variant riding the solar curve.
- Drag-to-size ALREADY EXISTS as a pattern: EXCAVATE paint-to-size (click-drag
  rect -> cells). Rooms are arbitrary cell collections. "Blend as you grow" is
  the room model working as designed.
- Soil/Seeds/Glass resources + SoilPallet/SeedVault manifest imports (soil
  from Earth: already canon). Water potability loop. O2 pool + circulation +
  floor rating. Dig spoil (SpoilPileKg) = the "chemicals from digs" source.
- Skills/tempo already scale garden yield.

## Design

### 1. Structures (presentation + placement)
- BELOW-GROUND greenhouse: stays the room path (drag = designate cells; the
  new art replaces per-cell visuals). No new placement system needed.
- ABOVE-GROUND greenhouse: new surface BUILDING family with a FOOTPRINT DRAG
  (extends paint-to-size to building placement — new interaction, Gate C).
  Flat-pane roof at small sizes; DOME at larger drags, dome height scaling
  with footprint (procedural: ISM glass panes on a parametric dome shell —
  generated pane/strut art, geometry in-engine).
- Grow-light fixtures: visible ceiling fixtures over below-ground garden
  cells (auto-infrastructure, same pattern as hab lighting; draw already sim'd).

### 2. Crops & growth stages (data + presentation)
- FRHCropRow table (DT_Crops): Potato, Carrot, Beans, Corn, Grain, Strawberry,
  Cotton — per-crop GrowSols, YieldKgPerSol, WaterDraw, ClimateBand, FoodKind
  (staple/protein/fruit/fiber — cotton feeds a later textiles loop).
- Growth STAGES are presentation: per-cell stage = f(planted time / GrowSols).
  Art: 3 silhouette families x 3 stages (sprout / young / mature) — root-row
  crops (potato/carrot), tall stalks (corn/grain), bush/vine (beans/berry) —
  plus blank and cultivated-row soil plots. Mature+harvest swaps to the
  existing yield pulse.
- POTTED LARGE PLANTS (apple/lemon trees): comfort props placeable in any
  rated room (small Hope bonus, slow fruit trickle) — rides the comforts lane.

### 3. Soil & fertilizer economy (sim, Gate B)
- Soil DEPLETES slowly per harvest (SoilDepleteKgPerHarvest). Restock: import
  (existing SoilPallet) or FERTILIZER: new recipe combining ColonistWasteKg
  (new passive accumulator per colonist-sol — abstract, Gate-D safe wording)
  + SpoilChemicalsKg (extracted fraction of dig spoil) -> Fertilizer ->
  restores soil. Closes the loop: more people = more farming capacity.

### 4. Climate (sim, Gate B)
- Per-greenhouse-room ClimateSetting (humid/temperate/arid). Crop yield x1.0
  matched, x0.6 mismatched. HumidityRegulator: small powered building/prop
  required for non-default climates (new art generated). One setting per
  room = per-crop greenhouses emerge naturally, the director's intent.

### 5. Air exchange (sim + interaction, Gate D)
- DUCT designation (drawable, hallway-like): connects a garden/greenhouse
  room to hab floors. Effect: gardens CONSUME colony CO2 (new passive pool fed
  by crew) and EMIT O2 into the floor pool (offsets leakage; a big enough
  garden network partially self-oxygenates a floor). Ducted gardens get a
  small yield bonus (CO2-rich air). Visual: the conduit prop family along the
  duct path + wall vent art at endpoints (already have the vent model).

## Gates
- A: DT_Crops + growth stages + stage art wiring + grow-light fixtures.
- B: soil depletion + fertilizer recipe + climate settings + regulator.
- C: above-ground drag-footprint greenhouse + parametric dome.
- D: duct designation + CO2/O2 exchange + `-agri` self-test battery.

All numbers placeholder for legible tests; balance = director review. Waste
wording stays abstract (standing Gate-D framing rule).

## Art batch (generating now, this session)
greenhouse_above (flat + dome hero refs), glass pane + strut tiles, grow-light
fixture, soil plot blank + cultivated rows, 3 crop families x 3 stages, potted
apple + lemon trees, soil stockpile, fertilizer unit, humidity regulator.
