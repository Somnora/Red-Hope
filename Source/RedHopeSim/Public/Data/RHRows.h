#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "RHRows.generated.h"

// Row structs backing docs/data/RH_*.csv. Column headers in the CSVs match
// these property names exactly; the CSVs are the balance source of truth.
// Semicolon-list columns (Inputs, Requirements, ...) stay FString here and are
// parsed by the definition registry, keeping the CSV format designer-editable.

USTRUCT(BlueprintType)
struct REDHOPESIM_API FRHConfigRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FString Value;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FString Unit;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FString Notes;
};

USTRUCT(BlueprintType)
struct REDHOPESIM_API FRHResourceRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FString DisplayName;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FString Category;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FString StorageType;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") bool SliceActive = false;
	// E1 (director answer Q-A (a)): what a kilogram is worth to Earth, in integer milli-credits (one value, used both ways; E3 adds the
	// launch cost on top). A HabSegment's "kg" is one unit. Power, Research and Hope are 0: they have no ledger slot, and Hope is never
	// valued. Placeholders for the director's review.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") int32 EarthValue_mcr = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FString Notes;
};

USTRUCT(BlueprintType)
struct REDHOPESIM_API FRHBuildingRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FString DisplayName;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") int32 FootprintX = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") int32 FootprintY = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") float PowerDraw_W = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") float PowerIdle_W = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") float PowerGenPeak_W = 0.f;
	// Curve-independent generation (the Lander's RTG trickle). Solar scaling
	// applies only to PowerGenPeak_W.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") float PowerGenBase_W = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") float StorageWh = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") float CoverageRadius_m = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") float LinkRange_m = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") float CostStruct_kg = 0.f;
	// Multi-resource construction bill ("Struct:250" or "Struct:600;HabSegment:4").
	// Empty falls back to CostStruct_kg. Read via URHDefinitionsSubsystem::GetBuildCost.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FString CostResources;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") float BuildTime_s = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") int32 LoadPriority = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") bool RequiresDeposit = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") bool ImportOnly = false;
	// Director directive: import-only is a Phase 1 rule, not a world law.
	// A non-empty UnlockTech flips this definition to locally-producible once
	// that tech exists. All checks go through CanFabricateLocally(); never
	// branch on ImportOnly directly.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FName UnlockTech;
	// M1-d Gate A2: this definition works bore/carve designations (the Borer).
	// The designation queue is the gate - its recipes never auto-start.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") bool CanBore = false;
	// M1-d Gate B: a completed, powered instance circulates a floor's air -
	// the "circulate" link of the habitability chain (AirFilter station).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") bool CirculatesAir = false;
	// M1-d Gate B: the surface radiation tax - built at Level >= 0 this def's
	// bill gains Shielding:N kg; underground the overburden shields for free
	// (the M1-c radiation curve made physical). 0 = untaxed (rugged hardware;
	// the bootstrap chain stays orderable at landing).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") float SurfaceShielding_kg = 0.f;
	// M1-d Gate A2: >0 means a batch burns Hydrogen stock (kg per sol-hour,
	// whole batch deducted up-front, committed like extraction) instead of
	// drawing grid power - and runs straight through shedding. 0 = grid only.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") float H2BurnKgPerHour = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") bool SliceActive = false;
	// UX (M2 Gate D+): player-facing copy for the in-world action card. Category
	// groups the build palette Sims-style (Boring/Habitat/Power/Production/
	// LifeSupport...); Blurb is the "what is this / how to use it" line shown
	// when you click the machine. Director-authored - never derived in code.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FName Category;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FString Blurb;
	// Prefab stations (tiered production, 2026-10-15): a building that IS a
	// room station - LabModule provides LabFull, InfirmaryModule an Infirmary.
	// A completed, switched-on, powered prefab on a rated floor counts as one
	// cell of that room (its seats, YieldMul, the infirmary grace). DT_Rooms
	// stays the single source of the station's numbers. Empty elsewhere.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FName ProvidesRoom;
	// Agri Gate C (2026-10-15): the player sizes this structure with a drag
	// (beds for the greenhouse dome; bays for a BM-1 habitat shell).
	// FootprintX/Y is ONE bed (a 10 m greenhouse bed = one underground
	// greenhouse cell's numbers); the instance holds SizeX x SizeY beds, and
	// the bill and build time scale with the bed count. Surface only.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") bool DragSized = false;
	// BM-1 (2026-10-16): a pressurized surface habitat shell (HabModule,
	// HabCorridor). Surface only; bays are 10 m (footprint 5x5 per bay; bays =
	// (FootprintX/5*SizeX) x (FootprintY/5*SizeY)) and centre on the world 10 m
	// grid. Shells sharing a bay edge form one network (derived, never saved).
	// ProvidesRoom on a shell is the FIXED room of every bay (not a station -
	// GetProvidedRoom returns null).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") bool Pressurized = false;
	// BM-1: may stand wholly inside one bay of a pressurized shell (the
	// footprint-overlap rule exempts that pair). Station modules with this flag
	// are allowed in a bay, one station per bay.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") bool Interior = false;
	// BM-1: drag cap per side (beds for a dome, bays for a hab); 0 = config
	// DomeMaxBedsPerSide.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") int32 MaxBedsPerSide = 0;
	// BM-1: > 0 caps the SHORTER drag side (a corridor is 1 bay wide); 0 = none.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") int32 DragShortSideMax = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FString Notes;
};

USTRUCT(BlueprintType)
struct REDHOPESIM_API FRHRecipeRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FName Building;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FString Inputs;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FString Outputs;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") float BatchTime_h = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") float PowerDraw_W = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") bool SliceActive = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FString Notes;
};

USTRUCT(BlueprintType)
struct REDHOPESIM_API FRHRobotRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FString DisplayName;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FName RobotClass;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") float Battery_Wh = 1000.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") float DrawMove_W = 50.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") float DrawWork_W = 50.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") float DrawIdle_W = 5.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") float Speed_mps = 3.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") float Cargo_kg = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") float WorkRate = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") float WearPerActiveSol = 5.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") int32 StartingCount = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FName UnlockTech;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") bool SliceActive = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FString Notes;
};

USTRUCT(BlueprintType)
struct REDHOPESIM_API FRHManifestItemRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FString DisplayName;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") float Mass_kg = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FString Category;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FString Effect;   // display text only (the picker's tooltip)
	// E2a: what landing the item does, as data. Verb is one of Stock, Kit, Robot, RoverAboard, FabSpeed, OrderLag, Crew; Target is a
	// resource, building, robot or config row (or Pod / Transport for Crew); Amount is a double so 0.15 is HEAD's literal bit for bit.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FName Verb;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FName Target;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") double Amount = 0.0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FName UnlockTech;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") bool SliceActive = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FString Notes;
};

USTRUCT(BlueprintType)
struct REDHOPESIM_API FRHQuotaRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FString Requirements;
	// Sols allowed FROM THE SOL THIS QUOTA OPENS (the quota ladder, save v25:
	// quotas after the first open when their predecessor's ship lands, so an
	// absolute sol would be instantly late). The first quota opens at sol 0,
	// where relative == absolute - every M0 pin unchanged.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") int32 DeadlineSol = 10;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") float OnTimeAward_kg = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") float LateAward_kg = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") bool SliceActive = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FString Notes;
};

// World-pressure events (M1-c): dust storms + solar flares in one schema.
// Scripted schedule for M1 (StartSol deterministic); probabilistic scheduling
// is a post-M1 knob on this same table.
USTRUCT(BlueprintType)
struct REDHOPESIM_API FRHEventRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FName Type; // DustStorm | SolarFlare
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") int32 StartSol = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") float DurationSols = 1.f;
	// DustStorm: solar output multiplier floor (0.3 = solar at 30%).
	// SolarFlare: exposed-robot wear multiplier (M1-c Gate B).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") float Severity = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") bool SliceActive = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FString Notes;
};

USTRUCT(BlueprintType)
struct REDHOPESIM_API FRHDepositRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FName Type;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") float Mass_kg = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") float LocX_m = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") float LocY_m = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") bool SurfaceVisible = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") bool SliceActive = false;
	// W1 (distant ice): an outpost deposit is drilled, never dug; its raw ice comes home by cargo rover to a
	// Water Plant at least OutpostMeltMinCm away (never melted beside the drill, never carried by hauler).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") bool Outpost = false;
	// Step-4 hook: 0 = the home region. The same convention as FRHDistrict::RegionId.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") int32 RegionId = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FString Notes;
};

// --- M1-d Gate C: the human-layer schemas, born DORMANT (habitat-vision §9,
// approved 2026-07-07b). M2 activates content, not schema - the M0 dormant-row
// discipline. No sim code consumes these until humans arrive.

// A room FUNCTION within a hab/floor (rooms-as-data, like buildings): what a
// designated space is for, what it needs, and how it behaves in the adjacency
// calculus (garden beside living quarters sickens; hallway + filtration cures).
USTRUCT(BlueprintType)
struct REDHOPESIM_API FRHRoomRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FString DisplayName;
	// Function class: Living | Garden | Lab | Workstation | Dining | Cooking |
	// Smoking | Hallway | Septic | WaterWorks (semicolon-free single tag).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FName Function;
	// Adjacency tags this room EMITS ("Odor;Contagion") and REFUSES nearby
	// ("Odor"): the §4 architecture-as-gameplay calculus, data-first.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FString EmitsTags;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FString RefusesTags;
	// A Hallway-function room between emitter and refuser cancels the emission;
	// filtration coverage (NeedsFiltration consumers) cancels air tags.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") bool NeedsFiltration = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") bool NeedsPlumbing = false;
	// Morale contribution when staffed/used (Hope-index input, brief §5).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") float MoraleWeight = 0.f;
	// --- Station tiers (tiered-production spec 2026-07-10, Gate A). A tier
	// ladder within a Function family: same job, better furniture. Defaults
	// make every pre-tier row an implicit T1 (all prior pins unchanged).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") int32 Tier = 1;
	// The row this station re-designates into (the upgrade verb is a later
	// interaction gate; the column is the data spine it will read).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FName UpgradesTo;
	// Job seats one designated cell of this room provides (T1 benches seat 1).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") int32 SeatCount = 1;
	// Output multiplier per filled seat: Lab-line seats multiply discovery
	// seat-hour accrual; Workstation-line seats feed the fabrication bonus.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") float YieldMul = 1.f;
	// C5b (2026-10-19): a service the city may zone for itself inside a Civic district when
	// that function's coverage has a gap (Dining, Infirmary). A CivicService row must never
	// emit Odor (it is zoned beside homes): the sim refuses one that does, loudly.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") bool CivicService = false;
	// C5b: the furniture rows the city fits such a room with, "DiningTable;Chair;Chair" - a
	// piece a row, repeats allowed. Their cost is part of the project's bill.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FString CivicFurniture;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") bool SliceActive = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FString Notes;
};

// A pressure compartment class: compartments are the atmosphere unit, doors
// are edges (§3 - the storm-breach retreat graph exists before breaches do).
USTRUCT(BlueprintType)
struct REDHOPESIM_API FRHCompartmentRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FString DisplayName;
	// O2 buffer a sealed compartment of this class holds per cell when cut
	// off - what "retreat somewhere with oxygen" is worth in sols.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") float O2BufferKgPerCell = 0.f;
	// Leak multiplier vs the open-floor rate (sealed compartments leak less).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") float LeakMul = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") bool SliceActive = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FString Notes;
};

// A wall fitting (build mode BM-6, 2026-10-16; the M2 pressurized-door rows
// grown into windows and airlocks): placed by the player on the derived walls
// of a surface habitat. Kind Door goes in a doorway (between rooms or
// modules); Window and Airlock go in an outside wall. Copy and numbers are
// placeholders for the director.
USTRUCT(BlueprintType)
struct REDHOPESIM_API FRHDoorRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FString DisplayName;
	// Door | Window | Airlock.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FName Kind;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") bool AutoSealOnBreach = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") float SealSeconds = 5.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") float CostStruct_kg = 0.f;
	// "Struct:10;Glass:40"; empty falls back to CostStruct_kg.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FString CostResources;
	// Bonus-only Hope while it stands in a rated habitat (the Views line).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") float HopeViews = 0.f;
	// Extra air its habitat leaks per sol.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") float LeakKgPerSol = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FString Blurb;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") bool SliceActive = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FString Notes;
};

// A crop variety (greenhouse-agriculture spec 2026-07-10, Gate A). Crops ride
// the existing garden cells: when any crop row is SliceActive the garden's
// flat legacy yield hands over to per-cell crops with real grow time, water
// draw, climate preference, and soil depletion. All rows authored DORMANT;
// activation is a per-gate director flip (the M2 dormant-row convention).
USTRUCT(BlueprintType)
struct REDHOPESIM_API FRHCropRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FString DisplayName;
	// Sols from planting to maturity; growth stage (sprout/young/mature) is
	// presentation-derived from age/GrowSols - never stored, era-parity-safe.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") float GrowSols = 3.f;
	// Food per sol per MATURE cell (immature cells yield nothing).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") float YieldKgPerSol = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") float WaterKgPerSol = 2.f;
	// Preferred climate band (Temperate/Humid/Arid). Yield x1.0 matched,
	// x ClimateMismatchYieldMul otherwise - per-crop greenhouses emerge.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FName ClimateBand;
	// staple/protein/fruit/fiber (fiber feeds a later textiles loop).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FName FoodKind;
	// Stage-art silhouette family the visualizer draws: root/tall/vine.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FName VisualFamily;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") bool SliceActive = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FString Notes;
};

// A research discovery (M2 Gate D+, the Flourishing layer): what a thriving
// colony's staffed Labs UNCOVER. Discoveries pop in authored Order (a
// deterministic sequence, never a random roll - era-parity by construction),
// each granting a permanent Hope milestone and an optional stock reward. The
// sequence ends in the landmark the director asked for: microbial life.
USTRUCT(BlueprintType)
struct REDHOPESIM_API FRHDiscoveryRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FString DisplayName;
	// Authored sequence position - discoveries fire strictly in this order.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") int32 Order = 0;
	// Research cost: staffed-Lab seat-hours accrued while the colony's smoothed
	// Hope holds above HopeDiscoveryThreshold (breakthroughs are earned by
	// sustained flourishing).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") float LabSeatHours = 100.f;
	// Permanent Hope milestone once discovered (momentum, brief §5).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") float HopeBonus = 0.f;
	// Optional one-time stock grant (None/0 = knowledge only).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FName RewardResource;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") float RewardKg = 0.f;
	// The banner the colony sees (player-facing - Gate-D framing review).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FString Alert;
	// --- Research pays (tiered-production spec 2026-07-10): completing this
	// discovery banks manifest budget toward the NEXT CEO quota award (the
	// existing funding currency - research literally funds the program).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") float FundingKg = 0.f;
	// A discovery can unlock a dormant room row (flips SliceActive live;
	// re-applied from the discovery log on load). The tier ladder is gated by
	// playing the research loop, not a tech-tree UI.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FName UnlockRoom;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") bool SliceActive = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FString Notes;
};

// A rival national colony (M3 Gate A): the neighbor the player trades with -
// and whom the Solidarity Dilemma will later demand they cut off. One row per
// settlement; ExportLot is what THEY send per convoy run, ImportLot is your
// side of the barter. Naming/flavor is Gate-D wording-review placeholder.
USTRUCT(BlueprintType)
struct REDHOPESIM_API FRHRivalRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FString DisplayName;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FString Nation;
	// Overland distance; transit = Distance / ConvoySpeedKmPerSol each way.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") float DistanceKm = 100.f;
	// "Res:Kg;Res:Kg" - their goods per completed run / your goods per dispatch.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FString ExportLot;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FString ImportLot;
	// Opening relation 0..100; completed runs warm it (RelationPerRun).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") float RelationStart = 30.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") bool SliceActive = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FString Notes;
};

// A furnishing (build mode BM-4, 2026-10-16): placed by hand on a 4x4 grid of
// 2.5 m sub-cells inside a zoned room cell or habitat bay, paid in stock when
// the order lands, sold back at FurnitureRefundFraction. Functional pieces
// REPLACE their room's legacy number, by kind: a Living cell with beds houses
// exactly its beds; a work station with job seats seats exactly them. Rooms
// without such pieces keep working exactly as before. Absent table = no
// furnishing layer at all. Copy is placeholder pending director review.
USTRUCT(BlueprintType)
struct REDHOPESIM_API FRHFurnitureRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FString DisplayName;
	// Catalogue folder: Sleep | Work | Table | Seating | Comfort | Plant | Storage.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FName Category;
	// What it means to the sim: Bed | JobSeat | DiningSeat | Comfort | None.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FName Provides;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") int32 Capacity = 0;
	// Room FUNCTIONS it may stand in ("Living;Dining"), so higher-tier rows
	// of a function family inherit.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FString RoomFunctions;
	// Footprint in 2.5 m sub-cells at rotation 0 (odd rotations swap them).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") int32 SlotsX = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") int32 SlotsY = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FString CostResources;
	// Bonus-only Hope while it stands in a working room (capped colony-wide).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") float HopeComfort = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FString Blurb;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") bool SliceActive = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RH") FString Notes;
};
