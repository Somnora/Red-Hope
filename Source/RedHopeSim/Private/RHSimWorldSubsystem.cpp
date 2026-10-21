#include "RHSimWorldSubsystem.h"
#include "RedHopeSim.h"
#include "RHDefinitionsSubsystem.h"
#include "RHSimClockSubsystem.h"
#include "RHAgentSubsystem.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/NameAsStringProxyArchive.h"
#include "Misc/Crc.h"
#include "Misc/CommandLine.h"
#include "Algo/BinarySearch.h"

namespace
{
	const FName NAME_Struct(TEXT("Struct"));
	const FName NAME_Lander(TEXT("Lander"));
	const FName NAME_Stockpile(TEXT("Stockpile"));
	const FName NAME_FurnBed(TEXT("Bed"));
	const FName NAME_FurnJobSeat(TEXT("JobSeat"));
	const FName NAME_FurnDiningSeat(TEXT("DiningSeat"));
	const FName NAME_WallDoor(TEXT("Door"));
	const FName NAME_WallWindow(TEXT("Window"));
	const FName NAME_WallAirlock(TEXT("Airlock"));
	const FName NAME_Glass(TEXT("Glass"));

	// Save format: versioned binary, sim-owned (approved architecture). Bump
	// the version on any payload change; old versions refuse loudly.
	// v2 (M1-b Gate A): Level on commands, buildings, deposits, robots.
	// v3 (M1-b Gate B): task TargetCm (survey), deposit bDiscovered.
	// v4 (M1-b close): survey history (the player's map survives loads).
	constexpr uint32 RHSaveMagic = 0x52485331;   // 'RHS1'
	constexpr uint32 RHSaveVersion = 42;  // v42: the Mars-born recalled to orbit wait there, a short list of (Id, name) written straight after the colonists and restored on load; they come home on a later supply ship (F2b); v41: floors become places - the floor table (region, level, the carved lattice cells in carve order, the painted plan) replaces the per-level carved count and is written straight after the header, so a bad table is refused before the live colony is touched; no key, room, order or growth field changes (D1); v40: cargo rovers and commuting add NO saved field (a rover saves as any robot, an undeployed one in ImportStock["RC_R"], a freight task as any haul; every commute, flow, freight and path cache is derived) - the bump is a fence: a v39 binary must refuse a save that can hold RC_R robots, ImportStock["RC_R"] and Haul tasks over 200 kg (C6a); v39: the City / Earth share and the production ledger - the open sol's tally, the ring of the last completed sols, the sol it last rolled at (C5d); v38: city growth - the open projects (district, kind, target, bill, total and remaining sols), the round-robin cursor, the last sol checked, and each district's growth switch (C5b); v37: districts - the district list (Id, Kind, Name, RegionId, Level, lattice) and the next district Id; every queued order's CommandId and Text (a RenameDistrict's new name), so a loaded order keeps its own handle instead of Id 0 (C5a); v36: roads - the road graph (nodes, edges, build progress, next ids), the BuildRoad order's second end point on queued commands, a road edge in a task's site (C4); v35: arrivals at scale - the Big Arrival streak and flag, the move-home accumulator, the settler count (C2b); v34: shared rationing - per-home departure accumulators, the deficit, the Mars-born flag (C2a); v33: per-machine day shift (D5a); v32: quota repeat round (D1a); v31: wall fittings (BM-6); v30: furniture (BM-4); v29: surface habitats (HomeHab inline; per-hab O2, rated habs, bay rooms); v28: drag-sized buildings (greenhouse dome beds); v27: staffing - seat priority (Assign), lazy-fill state; v26: crops + climate + ducts (greenhouse agriculture); v25 goal ladder + endings + funding; v24 alive pass

	FString SaveSlotToPath(const FString& Slot)
	{
		return FPaths::ProjectSavedDir() / TEXT("SaveGames") / FString::Printf(TEXT("RH_%s.sav"), *Slot);
	}

	void SerializeSite(FArchive& Ar, FRHSiteRef& Site)
	{
		Ar << Site.BuildingId;
		Ar << Site.DepositId;
		Ar << Site.RoadEdgeId; // v36
	}

	void SerializeResourceMap(FArchive& Ar, TMap<FName, double>& Map)
	{
		int32 Num = Map.Num();
		Ar << Num;
		if (Ar.IsLoading())
		{
			Map.Reset();
			for (int32 i = 0; i < Num; ++i)
			{
				FName Key; double Value = 0.0;
				Ar << Key; Ar << Value;
				Map.Add(Key, Value);
			}
		}
		else
		{
			for (auto& Pair : Map)
			{
				FName Key = Pair.Key;
				Ar << Key; Ar << Pair.Value;
			}
		}
	}
}

void URHSimWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Defs = Collection.InitializeDependency<URHDefinitionsSubsystem>();
	Clock = Collection.InitializeDependency<URHSimClockSubsystem>();
	bVerifyCaches = FParse::Param(FCommandLine::Get(), TEXT("RHVerifyCaches"));
	bCommuteAudit = FParse::Param(FCommandLine::Get(), TEXT("RHCommuteAudit")); // C6a: record the maxima over every step
}

void URHSimWorldSubsystem::Deinitialize()
{
	if (bCommuteAudit)
	{
		// C6a (C2): one line at the end of the run - the MAXIMA over every step, never a snapshot.
		UE_LOG(LogRedHopeSim, Display, TEXT("COMMUTE AUDIT: %d steps measured | peak pop %d | peak ramp %.4f | max employed commute %lld cm | max long commuters %d | margin below the long line (%lld cm) %lld cm"),
			CommuteAudit.Steps, CommuteAudit.PeakPop, CommuteAudit.PeakRamp, CommuteAudit.MaxDistCm, CommuteAudit.MaxLong, CommuteLongCm, CommuteLongCm - CommuteAudit.MaxDistCm);
	}
	Super::Deinitialize();
}

void URHSimWorldSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (!Defs)
	{
		return;
	}

	OrderLagSeconds = Defs->GetConfigScalar(FName("OrderLagTier0_s"), OrderLagSeconds);
	PileCapKg = Defs->GetConfigScalar(FName("PilesCapacity_kg"), PileCapKg);
	HaulLoadMinKg = FMath::Max(25.0, PileCapKg / 5.0);
	ChargeSeekFraction = (float)Defs->GetConfigScalar(FName("ChargeSeekFraction"), ChargeSeekFraction);
	ChargeResumeFraction = (float)Defs->GetConfigScalar(FName("ChargeResumeFraction"), ChargeResumeFraction);
	// D5a: the day shift's working window (sol fractions) and the A/B master
	// switch for the per-definition defaults. A malformed window fails loudly
	// (the battery greps Error) and falls back to sunrise..sunset of the curve.
	DayShiftStartFrac = Defs->GetConfigScalar(FName("DayShiftStartFrac"), DayShiftStartFrac);
	DayShiftEndFrac = Defs->GetConfigScalar(FName("DayShiftEndFrac"), DayShiftEndFrac);
	if (!(DayShiftStartFrac >= 0.0 && DayShiftStartFrac < DayShiftEndFrac && DayShiftEndFrac <= 1.0))
	{
		UE_LOG(LogRedHopeSim, Error, TEXT("DayShift window invalid (start %.3f, end %.3f) - using 0.25..0.75"), DayShiftStartFrac, DayShiftEndFrac);
		DayShiftStartFrac = 0.25;
		DayShiftEndFrac = 0.75;
	}
	bDayShiftDefaultsOn = Defs->GetConfigScalar(FName("DayShiftDefaultsOn"), 1.0) >= 0.5;
	LanderDockRateW = Defs->GetConfigScalar(FName("LanderDockRate_W"), LanderDockRateW);
	LazySeatFillSols = Defs->GetConfigScalar(FName("LazySeatFillSols"), LazySeatFillSols);
	AutosaveEverySols = Defs->GetConfigScalar(FName("AutosaveEverySols"), 0.0);
	FloorHeightCm = Defs->GetConfigScalar(FName("FloorHeightMeters"), 4.0) * 100.0;
	MaxDepth = (int32)Defs->GetConfigScalar(FName("MaxDepth"), 5.0);
	WearDegradeThreshold = (float)Defs->GetConfigScalar(FName("WearDegradeThreshold"), WearDegradeThreshold);
	WearHaltThreshold = (float)Defs->GetConfigScalar(FName("WearHaltThreshold"), WearHaltThreshold);
	RepairWearPerPart = (float)Defs->GetConfigScalar(FName("RepairWearPerPart"), RepairWearPerPart);
	StormWearMul = (float)Defs->GetConfigScalar(FName("StormWearMul"), StormWearMul);
	RadiationSurface = (float)Defs->GetConfigScalar(FName("RadiationSurface"), RadiationSurface);
	RadiationPerLevelMul = (float)Defs->GetConfigScalar(FName("RadiationPerLevelMul"), RadiationPerLevelMul);
	ShaftSpoilKgPerFloor = Defs->GetConfigScalar(FName("ShaftSpoilKgPerFloor"), ShaftSpoilKgPerFloor);
	SpoilKgPerCell = Defs->GetConfigScalar(FName("SpoilKgPerCell"), SpoilKgPerCell);
	O2FillKgPerCell = Defs->GetConfigScalar(FName("O2FillKgPerCell"), O2FillKgPerCell);
	O2LeakKgPerCellPerSol = Defs->GetConfigScalar(FName("O2LeakKgPerCellPerSol"), O2LeakKgPerCellPerSol);
	O2FillRateKgPerHour = Defs->GetConfigScalar(FName("O2FillRateKgPerHour"), O2FillRateKgPerHour);
	MinLivableCells = (int32)Defs->GetConfigScalar(FName("MinLivableCells"), MinLivableCells);
	ShaftLinkRangeCm = Defs->GetConfigScalar(FName("ShaftLinkRange_m"), ShaftLinkRangeCm / 100.0) * 100.0;
	SalvageFraction = FMath::Clamp(Defs->GetConfigScalar(FName("SalvageFraction"), SalvageFraction), 0.0, 1.0);
	FurnitureRefundFraction = FMath::Clamp(Defs->GetConfigScalar(FName("FurnitureRefundFraction"), FurnitureRefundFraction), 0.0, 1.0);
	HopeFurnishingsMax = FMath::Max(0.0, Defs->GetConfigScalar(FName("HopeFurnishingsMax"), HopeFurnishingsMax));
	HopeViewsMax = FMath::Max(0.0, Defs->GetConfigScalar(FName("HopeViewsMax"), HopeViewsMax));
	if (const FRHCompartmentRow* Sealed = Defs->GetCompartment(FName("SealedBay")))
	{
		SealedLeakMul = Sealed->SliceActive ? FMath::Clamp((double)Sealed->LeakMul, 0.0, 1.0) : 1.0;
	}
	// C4 roads (header defaults equal the CSV values).
	RoadParams.GridCm = FMath::Max(1, (int32)Defs->GetConfigScalar(FName("RoadGridCm"), RoadParams.GridCm));
	RoadParams.WidthCm = FMath::Max(1, (int32)Defs->GetConfigScalar(FName("RoadWidthCm"), RoadParams.WidthCm));
	RoadParams.MaxEdgeCm = FMath::Max(3 * RoadParams.GridCm, (int32)Defs->GetConfigScalar(FName("RoadMaxEdgeCm"), RoadParams.MaxEdgeCm));
	RoadParams.EntryCandidates = FMath::Max(1, (int32)Defs->GetConfigScalar(FName("RoadEntryCandidates"), RoadParams.EntryCandidates));
	RoadStructKgPerMeter = FMath::Max(0.0, Defs->GetConfigScalar(FName("RoadStructKgPerMeter"), RoadStructKgPerMeter));
	RoadBuildMetersPerHour = FMath::Max(0.1, Defs->GetConfigScalar(FName("RoadBuildMetersPerHour"), RoadBuildMetersPerHour));
	RoadSpeedMul = FMath::Max(1.0, Defs->GetConfigScalar(FName("RoadSpeedMul"), RoadSpeedMul));
	RoadParams.SpeedMulPermille = FMath::RoundToInt(RoadSpeedMul * 1000.0);
	// C6a rovers (header defaults equal the CSV values).
	RoverOffRoadSpeedMul = FMath::Clamp(Defs->GetConfigScalar(FName("RoverOffRoadSpeedMul"), RoverOffRoadSpeedMul), 0.05, 1.0);
	RoverLastLegCm = FMath::Max(0, (int32)Defs->GetConfigScalar(FName("RoverLastLegCm"), RoverLastLegCm));
	FreightMinCm = FMath::Max(0, (int32)Defs->GetConfigScalar(FName("FreightMinCm"), FreightMinCm));
	FreightHaulerFallbackSec = FMath::Max(0.0, Defs->GetConfigScalar(FName("FreightHaulerFallbackSec"), FreightHaulerFallbackSec));
	RoverReserveDetourMul = FMath::Max(1.0, Defs->GetConfigScalar(FName("RoverReserveDetourMul"), RoverReserveDetourMul));
	RoverClaimPlans = FMath::Max(1, (int32)Defs->GetConfigScalar(FName("RoverClaimPlans"), RoverClaimPlans));
	// The charge-reserve proof (design R7): a rover's worst trip to a pad is the straight line at its crawl,
	// 1 / RoverOffRoadSpeedMul times the straight-line drain, and DriveWh bounds it at RoverReserveDetourMul x 1.25
	// of it. The proof needs RoverReserveDetourMul x 1.25 >= 1 / RoverOffRoadSpeedMul: clamp the multiplier up, loudly.
	if (RoverReserveDetourMul * 1.25 < 1.0 / RoverOffRoadSpeedMul)
	{
		const double Needed = 1.0 / (RoverOffRoadSpeedMul * 1.25);
		UE_LOG(LogRedHopeSim, Error, TEXT("RoverReserveDetourMul %.2f x 1.25 does not cover a crawl of %.2fx (needs %.2f): clamped up"), RoverReserveDetourMul, RoverOffRoadSpeedMul, Needed);
		RoverReserveDetourMul = Needed;
	}
	RoverRoadParams = RoadParams;
	RoverRoadParams.SpeedMulPermille = FMath::RoundToInt(1000.0 * RoadSpeedMul / RoverOffRoadSpeedMul);
	RoverProfile.Params = &RoverRoadParams;
	RoverProfile.OffRoadMul = RoverOffRoadSpeedMul;
	RoverProfile.MissingEdgeMul = RoverOffRoadSpeedMul;
	// C6a commuting.
	CommuteRoadSpeedMul = FMath::Max(1.0, Defs->GetConfigScalar(FName("CommuteRoadSpeedMul"), CommuteRoadSpeedMul));
	CommuteShaftCmPerFloor = FMath::Max(0, (int32)Defs->GetConfigScalar(FName("CommuteShaftCmPerFloor"), CommuteShaftCmPerFloor));
	CommuteLongCm = FMath::Max(0, (int32)Defs->GetConfigScalar(FName("CommuteLongCm"), (double)CommuteLongCm));
	CommuteRampStartPop = FMath::Max(0.0, Defs->GetConfigScalar(FName("CommuteRampStartPop"), CommuteRampStartPop));
	CommuteRampFullPop = FMath::Max(CommuteRampStartPop + 1.0, Defs->GetConfigScalar(FName("CommuteRampFullPop"), CommuteRampFullPop));
	HopeCommuteMax = FMath::Max(0.0, Defs->GetConfigScalar(FName("HopeCommuteMax"), HopeCommuteMax));
	HopeCommuteSteepness = FMath::Max(0.0, Defs->GetConfigScalar(FName("HopeCommuteSteepness"), HopeCommuteSteepness));
	CommuteWorkMul = FMath::Clamp(Defs->GetConfigScalar(FName("CommuteWorkMul"), CommuteWorkMul), 0.0, 1.0);
	CommuteAlertShare = FMath::Clamp(Defs->GetConfigScalar(FName("CommuteAlertShare"), CommuteAlertShare), 0.0, 1.0);
	++CommuteCfgVersion;
	ColonistsPerCell = Defs->GetConfigScalar(FName("ColonistsPerCell"), ColonistsPerCell);
	ColonistO2KgPerSol = Defs->GetConfigScalar(FName("ColonistO2KgPerSol"), ColonistO2KgPerSol);
	ColonistFoodKgPerSol = Defs->GetConfigScalar(FName("ColonistFoodKgPerSol"), ColonistFoodKgPerSol);
	CrewPodColonists = (int32)Defs->GetConfigScalar(FName("CrewPodColonists"), CrewPodColonists);
	CrewPodFoodKg = Defs->GetConfigScalar(FName("CrewPodFoodKg"), CrewPodFoodKg);
	// C2b: arrivals at scale (the same fallbacks as RH_Config.csv).
	ColonistTransportColonists = FMath::Max(0, (int32)Defs->GetConfigScalar(FName("ColonistTransportColonists"), ColonistTransportColonists));
	ColonistTransportFoodKgPerColonist = FMath::Max(0.0, Defs->GetConfigScalar(FName("ColonistTransportFoodKgPerColonist"), ColonistTransportFoodKgPerColonist));
	BirthRefPop = FMath::Max(1.0, Defs->GetConfigScalar(FName("BirthRefPop"), BirthRefPop));
	ImmigrationHopeMin = Defs->GetConfigScalar(FName("ImmigrationHopeMin"), ImmigrationHopeMin);
	ImmigrationHopeFloor = Defs->GetConfigScalar(FName("ImmigrationHopeFloor"), ImmigrationHopeFloor);
	ImmigrationBase = FMath::Max(0.0, Defs->GetConfigScalar(FName("ImmigrationBase"), ImmigrationBase));
	ImmigrationPerCapita = FMath::Max(0.0, Defs->GetConfigScalar(FName("ImmigrationPerCapita"), ImmigrationPerCapita));
	ImmigrationCapPerShip = FMath::Max(0, (int32)Defs->GetConfigScalar(FName("ImmigrationCapPerShip"), ImmigrationCapPerShip));
	ImmigrationReserveBeds = FMath::Max(0, (int32)Defs->GetConfigScalar(FName("ImmigrationReserveBeds"), ImmigrationReserveBeds));
	BigArrivalHopeMin = Defs->GetConfigScalar(FName("BigArrivalHopeMin"), BigArrivalHopeMin);
	BigArrivalStreakSolsNeeded = Defs->GetConfigScalar(FName("BigArrivalStreakSols"), BigArrivalStreakSolsNeeded);
	BigArrivalFreeBeds = FMath::Max(0, (int32)Defs->GetConfigScalar(FName("BigArrivalFreeBeds"), BigArrivalFreeBeds));
	BigArrivalColonists = FMath::Max(0, (int32)Defs->GetConfigScalar(FName("BigArrivalColonists"), BigArrivalColonists));
	BigArrivalLifeSupportSols = FMath::Max(0.0, Defs->GetConfigScalar(FName("BigArrivalLifeSupportSols"), BigArrivalLifeSupportSols));
	// C5d: life-support production gates and the City / Earth share (the same fallbacks as RH_Config.csv).
	LifeSupportDeficitCoverSols = FMath::Max(0.0, Defs->GetConfigScalar(FName("LifeSupportDeficitCoverSols"), LifeSupportDeficitCoverSols));
	ProductionWindowSols = FMath::Clamp((int32)Defs->GetConfigScalar(FName("ProductionWindowSols"), ProductionWindowSols), 1, ProdMaxWindow);
	GrowthCityShareDefault = FMath::Clamp(Defs->GetConfigScalar(FName("GrowthCityShareDefault"), GrowthCityShareDefault), 0.0, 1.0);
	CityShare = GrowthCityShareDefault;
	MoveHomeIntervalSols = FMath::Max(0.0, Defs->GetConfigScalar(FName("MoveHomeIntervalSols"), MoveHomeIntervalSols));
	// C5b: city growth (the same fallbacks as RH_Config.csv).
	GrowthReserveStructKg = FMath::Max(0.0, Defs->GetConfigScalar(FName("GrowthReserveStructKg"), GrowthReserveStructKg));
	GrowthReserveO2Sols = FMath::Max(0.0, Defs->GetConfigScalar(FName("GrowthReserveO2Sols"), GrowthReserveO2Sols));
	GrowthModuleSols = FMath::Max(0.0, Defs->GetConfigScalar(FName("GrowthModuleSols"), GrowthModuleSols));
	GrowthCellSols = FMath::Max(0.0, Defs->GetConfigScalar(FName("GrowthCellSols"), GrowthCellSols));
	GrowthRoomSols = FMath::Max(0.0, Defs->GetConfigScalar(FName("GrowthRoomSols"), GrowthRoomSols));
	GrowthCellsPerProject = FMath::Max(1, (int32)Defs->GetConfigScalar(FName("GrowthCellsPerProject"), GrowthCellsPerProject));
	GrowthBunksPerHome = FMath::Clamp((int32)Defs->GetConfigScalar(FName("GrowthBunksPerHome"), GrowthBunksPerHome), 0, 4);
	Defs->ForEachRoom([](FName RowName, const FRHRoomRow& Row)
	{
		// A civic service is zoned beside homes: it must never emit Odor. A row that does is refused at
		// planning time; saying so here, once, is what makes a data slip loud.
		if (Row.CivicService && URHSimWorldSubsystem::RoomEmitsTag(Row, TEXT("Odor")))
		{
			UE_LOG(LogRedHopeSim, Error, TEXT("Room row '%s' is a CivicService but emits Odor - the city will never zone it"), *RowName.ToString());
		}
	});
	ColonistEvacSols = Defs->GetConfigScalar(FName("ColonistEvacSols"), ColonistEvacSols);
	HopeBase = Defs->GetConfigScalar(FName("HopeBase"), HopeBase);
	HopeHousingMax = Defs->GetConfigScalar(FName("HopeHousingMax"), HopeHousingMax);
	HopePerMoralePoint = Defs->GetConfigScalar(FName("HopePerMoralePoint"), HopePerMoralePoint);
	// C2a (per head). HopePerJob, HopeAdjacencyPenalty and HopeUnsupportedPenalty are retired.
	HopeJobsMax = FMath::Max(0.0, Defs->GetConfigScalar(FName("HopeJobsMax"), HopeJobsMax));
	HopeUnsupportedMax = FMath::Max(0.0, Defs->GetConfigScalar(FName("HopeUnsupportedMax"), HopeUnsupportedMax));
	HopeUnsupportedSteepness = FMath::Max(0.0, Defs->GetConfigScalar(FName("HopeUnsupportedSteepness"), HopeUnsupportedSteepness));
	HopeAdjacencyMax = FMath::Max(0.0, Defs->GetConfigScalar(FName("HopeAdjacencyMax"), HopeAdjacencyMax));
	RoomServesResidents = FMath::Max(1.0, Defs->GetConfigScalar(FName("RoomServesResidents"), RoomServesResidents));
	ComfortRefPop = FMath::Max(0.0, Defs->GetConfigScalar(FName("ComfortRefPop"), ComfortRefPop));
	HopeVaultMilestone = Defs->GetConfigScalar(FName("HopeVaultMilestone"), HopeVaultMilestone);
	GardenSoilKgPerCell = Defs->GetConfigScalar(FName("GardenSoilKgPerCell"), GardenSoilKgPerCell);
	GardenSeedsKgPerCell = Defs->GetConfigScalar(FName("GardenSeedsKgPerCell"), GardenSeedsKgPerCell);
	GardenFoodKgPerSolPerCell = Defs->GetConfigScalar(FName("GardenFoodKgPerSolPerCell"), GardenFoodKgPerSolPerCell);
	GardenWaterKgPerSolPerCell = Defs->GetConfigScalar(FName("GardenWaterKgPerSolPerCell"), GardenWaterKgPerSolPerCell);
	GardenGrowLightWPerCell = Defs->GetConfigScalar(FName("GardenGrowLightWPerCell"), GardenGrowLightWPerCell);
	GreenhouseGlassKgPerCell = Defs->GetConfigScalar(FName("GreenhouseGlassKgPerCell"), GreenhouseGlassKgPerCell);
	DomeMaxBedsPerSide = FMath::Max(1, (int32)Defs->GetConfigScalar(FName("DomeMaxBedsPerSide"), (double)DomeMaxBedsPerSide));
	GreenhouseMinLevel = (int32)Defs->GetConfigScalar(FName("GreenhouseMinLevel"), GreenhouseMinLevel);
	CropSoilDepleteKgPerSol = Defs->GetConfigScalar(FName("CropSoilDepleteKgPerSol"), CropSoilDepleteKgPerSol);
	FertilizerSoilPerKg = Defs->GetConfigScalar(FName("FertilizerSoilPerKg"), FertilizerSoilPerKg);
	CompostKgPerColonistSol = Defs->GetConfigScalar(FName("CompostKgPerColonistSol"), CompostKgPerColonistSol);
	SpoilChemFraction = Defs->GetConfigScalar(FName("SpoilChemFraction"), SpoilChemFraction);
	ClimateMismatchYieldMul = Defs->GetConfigScalar(FName("ClimateMismatchYieldMul"), ClimateMismatchYieldMul);
	DuctYieldBonusMul = Defs->GetConfigScalar(FName("DuctYieldBonusMul"), DuctYieldBonusMul);
	GardenCO2DrawKgPerSolPerCell = Defs->GetConfigScalar(FName("GardenCO2DrawKgPerSolPerCell"), GardenCO2DrawKgPerSolPerCell);
	GardenO2EmitKgPerSolPerCell = Defs->GetConfigScalar(FName("GardenO2EmitKgPerSolPerCell"), GardenO2EmitKgPerSolPerCell);
	CO2KgPerColonistSol = Defs->GetConfigScalar(FName("CO2KgPerColonistSol"), CO2KgPerColonistSol);
	ColonistWaterKgPerSol = Defs->GetConfigScalar(FName("ColonistWaterKgPerSol"), ColonistWaterKgPerSol);
	GreywaterReturnFraction = Defs->GetConfigScalar(FName("GreywaterReturnFraction"), GreywaterReturnFraction);
	WaterPotabilityDecayPerSol = Defs->GetConfigScalar(FName("WaterPotabilityDecayPerSol"), WaterPotabilityDecayPerSol);
	WaterPotabilityRestorePerKg = Defs->GetConfigScalar(FName("WaterPotabilityRestorePerKg"), WaterPotabilityRestorePerKg);
	WaterPotabilityFloor = Defs->GetConfigScalar(FName("WaterPotabilityFloor"), WaterPotabilityFloor);
	HopeWaterPenalty = Defs->GetConfigScalar(FName("HopeWaterPenalty"), HopeWaterPenalty);
	HopeFoodPenalty = FMath::Max(0.0, Defs->GetConfigScalar(FName("HopeFoodPenalty"), HopeFoodPenalty));
	FoodRunwayFloorSols = FMath::Max(0.0, Defs->GetConfigScalar(FName("FoodRunwayFloorSols"), FoodRunwayFloorSols));
	FoodRunwayRearmSols = FMath::Max(FoodRunwayFloorSols, Defs->GetConfigScalar(FName("FoodRunwayRearmSols"), FoodRunwayRearmSols)); // hysteresis is never negative
	IceLowAlertFraction = Defs->GetConfigScalar(FName("IceLowAlertFraction"), IceLowAlertFraction);
	IceWaterPlantNear_m = Defs->GetConfigScalar(FName("IceWaterPlantNear_m"), IceWaterPlantNear_m);
	ConstructionMaterials.Reset();
	ConstructionMaterials.Add(FName("Struct"));
	Defs->ForEachBuilding([this](FName, const FRHBuildingRow& Row)
	{
		for (const auto& Line : URHDefinitionsSubsystem::ParseResourceList(Row.CostResources)) { ConstructionMaterials.Add(Line.Key); }
	});
	Defs->ForEachFurnitureRow([this](FName, const FRHFurnitureRow& Row)
	{
		for (const auto& Line : URHDefinitionsSubsystem::ParseResourceList(Row.CostResources)) { ConstructionMaterials.Add(Line.Key); }
	});
	{
		TArray<TPair<FName, const FRHDoorRow*>> DoorRows;
		Defs->GetActiveDoorsSorted(DoorRows);
		for (const auto& Door : DoorRows)
		{
			for (const auto& Line : URHDefinitionsSubsystem::ParseResourceList(Door.Value->CostResources)) { ConstructionMaterials.Add(Line.Key); }
		}
	}
	LuxuryKgPerColonistPerSol = Defs->GetConfigScalar(FName("LuxuryKgPerColonistPerSol"), LuxuryKgPerColonistPerSol);
	HopeLuxuryBonus = Defs->GetConfigScalar(FName("HopeLuxuryBonus"), HopeLuxuryBonus);
	HopeSmoothTauSols = Defs->GetConfigScalar(FName("HopeSmoothTauSols"), HopeSmoothTauSols);
	HopeTempoSlope = Defs->GetConfigScalar(FName("HopeTempoSlope"), HopeTempoSlope);
	HopeTempoMin = Defs->GetConfigScalar(FName("HopeTempoMin"), HopeTempoMin);
	HopeTempoMax = Defs->GetConfigScalar(FName("HopeTempoMax"), HopeTempoMax);
	HopeBandUp[0]   = Defs->GetConfigScalar(FName("HopeStrainedEnter"),    HopeBandUp[0]);
	HopeBandDown[0] = Defs->GetConfigScalar(FName("HopeFailingEnter"),     HopeBandDown[0]);
	HopeBandUp[1]   = Defs->GetConfigScalar(FName("HopeSteadyEnter"),      HopeBandUp[1]);
	HopeBandDown[1] = Defs->GetConfigScalar(FName("HopeStrainedExit"),     HopeBandDown[1]);
	HopeBandUp[2]   = Defs->GetConfigScalar(FName("HopeThrivingEnter"),    HopeBandUp[2]);
	HopeBandDown[2] = Defs->GetConfigScalar(FName("HopeThrivingExit"),     HopeBandDown[2]);
	HopeBandUp[3]   = Defs->GetConfigScalar(FName("HopeFlourishingEnter"), HopeBandUp[3]);
	HopeBandDown[3] = Defs->GetConfigScalar(FName("HopeFlourishingExit"),  HopeBandDown[3]);
	HopeGrowthThreshold = Defs->GetConfigScalar(FName("HopeGrowthThreshold"), HopeGrowthThreshold);
	HopeGrowthIntervalSols = Defs->GetConfigScalar(FName("HopeGrowthIntervalSols"), HopeGrowthIntervalSols);
	HopeGrowthFoodBufferSols = Defs->GetConfigScalar(FName("HopeGrowthFoodBufferSols"), HopeGrowthFoodBufferSols);
	HopeFirstBornMilestone = Defs->GetConfigScalar(FName("HopeFirstBornMilestone"), HopeFirstBornMilestone);
	HopeDiscoveryThreshold = Defs->GetConfigScalar(FName("HopeDiscoveryThreshold"), HopeDiscoveryThreshold);
	EpilogueSustainSols = Defs->GetConfigScalar(FName("EpilogueSustainSols"), EpilogueSustainSols);
	WeatherCycleSols = Defs->GetConfigScalar(FName("WeatherCycleSols"), WeatherCycleSols);
	InfirmaryEvacBonusSols = Defs->GetConfigScalar(FName("InfirmaryEvacBonusSols"), InfirmaryEvacBonusSols);
	WorkstationFabBonusPerSeat = Defs->GetConfigScalar(FName("WorkstationFabBonusPerSeat"), WorkstationFabBonusPerSeat);
	QuotaRepeatBillMul = Defs->GetConfigScalar(FName("QuotaRepeatBillMul"), QuotaRepeatBillMul);
	// The goal ladder: a fresh colony opens on the first slice-active quota
	// (Q1 today - identical to the old hard-wired behavior).
	if (ActiveQuota.IsNone())
	{
		ActiveQuota = FirstQuotaName();
		QuotaOpenedSol = 0;
	}
	ConvoySpeedKmPerSol = Defs->GetConfigScalar(FName("ConvoySpeedKmPerSol"), ConvoySpeedKmPerSol);
	ConvoyH2PerRun = Defs->GetConfigScalar(FName("ConvoyH2PerRun"), ConvoyH2PerRun);
	ConvoyWearParts = Defs->GetConfigScalar(FName("ConvoyWearParts"), ConvoyWearParts);
	RelationPerRun = Defs->GetConfigScalar(FName("RelationPerRun"), RelationPerRun);
	EarthTension = Defs->GetConfigScalar(FName("EarthTensionStart"), EarthTension);
	IdentityAxis = Defs->GetConfigScalar(FName("IdentityAxisStart"), IdentityAxis);
	EarthTensionDriftPerSol = Defs->GetConfigScalar(FName("EarthTensionDriftPerSol"), EarthTensionDriftPerSol);
	EarthTensionDemandThreshold = Defs->GetConfigScalar(FName("EarthTensionDemandThreshold"), EarthTensionDemandThreshold);
	RequisitionEarthBonus = Defs->GetConfigScalar(FName("RequisitionEarthBonus"), RequisitionEarthBonus);
	RequisitionMartianPenalty = Defs->GetConfigScalar(FName("RequisitionMartianPenalty"), RequisitionMartianPenalty);
	RequisitionTensionPenalty = Defs->GetConfigScalar(FName("RequisitionTensionPenalty"), RequisitionTensionPenalty);
	DilemmaComplyAxisShift = Defs->GetConfigScalar(FName("DilemmaComplyAxisShift"), DilemmaComplyAxisShift);
	DilemmaDefyAxisShift = Defs->GetConfigScalar(FName("DilemmaDefyAxisShift"), DilemmaDefyAxisShift);
	DilemmaComplyTensionDrop = Defs->GetConfigScalar(FName("DilemmaComplyTensionDrop"), DilemmaComplyTensionDrop);
	DilemmaDefyTensionDrop = Defs->GetConfigScalar(FName("DilemmaDefyTensionDrop"), DilemmaDefyTensionDrop);
	DilemmaRelationHit = Defs->GetConfigScalar(FName("DilemmaRelationHit"), DilemmaRelationHit);
	DilemmaDefyRelationBonus = Defs->GetConfigScalar(FName("DilemmaDefyRelationBonus"), DilemmaDefyRelationBonus);
	SolidarityHopeShock = Defs->GetConfigScalar(FName("SolidarityHopeShock"), SolidarityHopeShock);
	SolidarityHopeTauSols = Defs->GetConfigScalar(FName("SolidarityHopeTauSols"), SolidarityHopeTauSols);
	HumanNatureAxis = Defs->GetConfigScalar(FName("HumanNatureStart"), HumanNatureAxis);
	HumanNatureFairTradeShift = Defs->GetConfigScalar(FName("HumanNatureFairTradeShift"), HumanNatureFairTradeShift);
	HumanNatureCovertShift = Defs->GetConfigScalar(FName("HumanNatureCovertShift"), HumanNatureCovertShift);
	CovertLotFraction = Defs->GetConfigScalar(FName("CovertLotFraction"), CovertLotFraction);
	CovertDetectionBase = Defs->GetConfigScalar(FName("CovertDetectionBase"), CovertDetectionBase);
	CovertDetectionTrustMul = Defs->GetConfigScalar(FName("CovertDetectionTrustMul"), CovertDetectionTrustMul);
	CovertDetectionNightMul = Defs->GetConfigScalar(FName("CovertDetectionNightMul"), CovertDetectionNightMul);
	CovertHiddenTensionClean = Defs->GetConfigScalar(FName("CovertHiddenTensionClean"), CovertHiddenTensionClean);
	CovertHiddenTensionCaught = Defs->GetConfigScalar(FName("CovertHiddenTensionCaught"), CovertHiddenTensionCaught);
	CovertRelationHitCaught = Defs->GetConfigScalar(FName("CovertRelationHitCaught"), CovertRelationHitCaught);
	LaunderHiddenDrop = Defs->GetConfigScalar(FName("LaunderHiddenDrop"), LaunderHiddenDrop);
	LaunderRelationBonus = Defs->GetConfigScalar(FName("LaunderRelationBonus"), LaunderRelationBonus);
	HumanNatureLaunderShift = Defs->GetConfigScalar(FName("HumanNatureLaunderShift"), HumanNatureLaunderShift);
	HumanNatureSabotageShift = Defs->GetConfigScalar(FName("HumanNatureSabotageShift"), HumanNatureSabotageShift);
	SabotageDurationSols = Defs->GetConfigScalar(FName("SabotageDurationSols"), SabotageDurationSols);
	SabotageHiddenTension = Defs->GetConfigScalar(FName("SabotageHiddenTension"), SabotageHiddenTension);
	DiscoveryChancePerSurvey = Defs->GetConfigScalar(FName("DiscoveryChancePerSurvey"), DiscoveryChancePerSurvey);
	Influence = Defs->GetConfigScalar(FName("InfluenceStart"), Influence);
	InfluencePerTrade = Defs->GetConfigScalar(FName("InfluencePerTrade"), InfluencePerTrade);
	InfluencePerLaunder = Defs->GetConfigScalar(FName("InfluencePerLaunder"), InfluencePerLaunder);
	InfluenceDriftPerSol = Defs->GetConfigScalar(FName("InfluenceDriftPerSol"), InfluenceDriftPerSol);
	EarthPreemptiveThreshold = Defs->GetConfigScalar(FName("EarthPreemptiveThreshold"), EarthPreemptiveThreshold);
	EmbargoGraceSols = Defs->GetConfigScalar(FName("EmbargoGraceSols"), EmbargoGraceSols);
	PacifyInfluenceCost = Defs->GetConfigScalar(FName("PacifyInfluenceCost"), PacifyInfluenceCost);
	PacifyTensionRelief = Defs->GetConfigScalar(FName("PacifyTensionRelief"), PacifyTensionRelief);
	HumanNaturePacifyShift = Defs->GetConfigScalar(FName("HumanNaturePacifyShift"), HumanNaturePacifyShift);
	DefectRelationFloor = Defs->GetConfigScalar(FName("DefectRelationFloor"), DefectRelationFloor);
	CrisisIntervalSols = Defs->GetConfigScalar(FName("CrisisIntervalSols"), CrisisIntervalSols);
	CrisisChance = Defs->GetConfigScalar(FName("CrisisChance"), CrisisChance);
	CrisisDurationSols = Defs->GetConfigScalar(FName("CrisisDurationSols"), CrisisDurationSols);
	CrisisAlignmentThreshold = Defs->GetConfigScalar(FName("CrisisAlignmentThreshold"), CrisisAlignmentThreshold);
	CrisisWorkPenalty = Defs->GetConfigScalar(FName("CrisisWorkPenalty"), CrisisWorkPenalty);
	CrisisMalfunctionHope = Defs->GetConfigScalar(FName("CrisisMalfunctionHope"), CrisisMalfunctionHope);
	CrisisEnvironmentHope = Defs->GetConfigScalar(FName("CrisisEnvironmentHope"), CrisisEnvironmentHope);
	CrisisWaterDrainPerSol = Defs->GetConfigScalar(FName("CrisisWaterDrainPerSol"), CrisisWaterDrainPerSol);
	EndingIdentityThreshold = Defs->GetConfigScalar(FName("EndingIdentityThreshold"), EndingIdentityThreshold);
	EndingHumanNatureThreshold = Defs->GetConfigScalar(FName("EndingHumanNatureThreshold"), EndingHumanNatureThreshold);
	SkillMasterySols = Defs->GetConfigScalar(FName("SkillMasterySols"), SkillMasterySols);
	CrewMomentIntervalSols = Defs->GetConfigScalar(FName("CrewMomentIntervalSols"), CrewMomentIntervalSols);
	DisputeHopeBelow = Defs->GetConfigScalar(FName("DisputeHopeBelow"), DisputeHopeBelow);
	FirstHarvestKg = Defs->GetConfigScalar(FName("FirstHarvestKg"), FirstHarvestKg);
	HopeFirstHarvestMilestone = Defs->GetConfigScalar(FName("HopeFirstHarvestMilestone"), HopeFirstHarvestMilestone);
	HopeSmoothed = HopeBase; // a fresh colony opens at baseline mood
	if (Clock)
	{
		Clock->OnSolElapsed.AddUObject(this, &URHSimWorldSubsystem::HandleSolElapsed);
		// Mission clock at landing (StartSolHour, sol-hour = 50 sim-s). Default
		// 6 = dawn: the colony wakes with the sun instead of five real minutes
		// of darkness (director's first-play feedback).
		const double StartHour = Defs->GetConfigScalar(FName("StartSolHour"), 0.0);
		if (StartHour > 0.0)
		{
			Clock->Debug_SetSimSeconds(StartHour * 50.0);
		}
	}

	// Deposits from data. SurfaceVisible=FALSE rows exist in the world but
	// stay undiscovered until a scout surveys them (M1-b: knowledge costs a
	// trip; the flag waited in the CSV since M0).
	Defs->ForEachDeposit([this](FName RowName, const FRHDepositRow& Row)
	{
		FRHDepositState D;
		D.Id = Deposits.Num() + 1;
		D.RowName = RowName;
		D.Type = Row.Type;
		D.RemainingKg = Row.Mass_kg;
		D.InitialKg = Row.Mass_kg;
		D.LocationCm = FVector(Row.LocX_m * 100.f, Row.LocY_m * 100.f, 0.f);
		D.bDiscovered = Row.SurfaceVisible;
		Deposits.Add(D);
	});

	// Mission start: the Lander, its starter Struct, and the imported
	// flat-pack stock (import-only hardware arrives, never smelted).
	AddBuilding(NAME_Lander, FVector::ZeroVector, /*bInstant*/ true);
	if (FRHBuildingInstance* Lander = FindBuilding(1))
	{
		Lander->InputKg.Add(NAME_Struct, Defs->GetConfigScalar(FName("StarterStruct_kg"), 400.0));
	}
	ImportStock.Add(FName("SolarArray"), (int32)Defs->GetConfigScalar(FName("StarterSolarPacks"), 6));
	ImportStock.Add(FName("BatteryBank"), (int32)Defs->GetConfigScalar(FName("StarterBatteryPacks"), 2));
	// Starter maintenance stock (M1-b: the row waited unused since M0). Parts
	// live in the network pool - the maintenance unit's van is the fiction.
	AddStock(FName("SpareParts"), Defs->GetConfigScalar(FName("StarterSpareParts"), 6.0));

	// Fleet deploys on the first sim step, after every subsystem's
	// BeginPlay has run - subscribers never miss the spawn broadcast.

	UE_LOG(LogRedHopeSim, Display, TEXT("Colony sim online. Lag %.0f sim-s. Deposits %d. Starter Struct %.0f kg."),
		OrderLagSeconds, Deposits.Num(), GetTotalSolid(NAME_Struct));
}

void URHSimWorldSubsystem::SpawnStartingFleet()
{
	TArray<FMassEntityHandle> All;
	int32 Slot = 0;
	Defs->ForEachRobot([&](FName RowName, const FRHRobotRow& Row)
	{
		for (int32 i = 0; i < Row.StartingCount; ++i)
		{
			// Deploy ring south of the lander.
			const float Angle = Slot * 0.8f;
			const FVector Pos(-800.f + 250.f * Slot, -600.f + 150.f * FMath::Sin(Angle), 50.f);
			SpawnRobotTracked(RowName, Row, Pos, Row.Battery_Wh, 0.f, All);
			++Slot;
		}
	});
	UE_LOG(LogRedHopeSim, Display, TEXT("Starting fleet deployed: %d robots"), All.Num());
	OnRobotsSpawned.Broadcast(All);
}

void URHSimWorldSubsystem::DeployFleetOnce()
{
	if (!bFleetDeployed)
	{
		bFleetDeployed = true;
		SpawnStartingFleet();
	}
	DeployWaitingRovers(); // C6a: a rover aboard the Lander lands the first step a road exists (both bands)
}

FMassEntityHandle URHSimWorldSubsystem::SpawnRobotTracked(FName RowName, const FRHRobotRow& Row, const FVector& PosCm, float ChargeWh, float Wear, TArray<FMassEntityHandle>& OutSpawned)
{
	URHAgentSubsystem* Agents = GetWorld()->GetSubsystem<URHAgentSubsystem>();
	if (!Agents)
	{
		return FMassEntityHandle();
	}
	const FMassEntityHandle H = Agents->SpawnRobotWithState(RowName, Row, PosCm, ChargeWh, Wear);
	if (H.IsValid())
	{
		FleetCounts.FindOrAdd(RowName)++;
		OutSpawned.Add(H);
	}
	return H;
}

void URHSimWorldSubsystem::HandleSolElapsed(int32 NewSol)
{
	if (AutosaveEverySols > 0.0 && NewSol != LastAutosaveSol
		&& (NewSol % FMath::Max(1, (int32)AutosaveEverySols)) == 0)
	{
		LastAutosaveSol = NewSol;
		FString Error;
		if (!SaveColony(TEXT("auto"), Error))
		{
			UE_LOG(LogRedHopeSim, Warning, TEXT("Autosave failed: %s"), *Error);
		}
	}
}

void URHSimWorldSubsystem::Tick(float DeltaTime)
{
	if (!Clock)
	{
		return;
	}
	// The clock has already advanced the whole frame, so every step of a burst
	// would read the same end time. Give each step its own window end (D5a era
	// parity: the day-shift share is computed per step, not per frame).
	const int32 Steps = Clock->GetStepsThisFrame();
	const double FrameEndSeconds = Clock->GetSimSecondsTotal();
	for (int32 i = 0; i < Steps; ++i)
	{
		StepEndOverride = FrameEndSeconds - (double)(Steps - 1 - i) * URHSimClockSubsystem::SubStepSeconds;
		StepSim(URHSimClockSubsystem::SubStepSeconds);
	}
	StepEndOverride = -1.0;

	// Era band: agent sub-steps stop publishing; the ledger integrator runs.
	// Any agent-fidelity event yanks the throttle back to 1x (auto-drop).
	if (Clock->IsEraMode())
	{
		FString Why;
		if (!CanEnterEraMode(Why))
		{
			// Refusing era mode is not a punishment: hold the player's last
			// agent-band speed (not 1x) and say so LOUDLY - the notice-line
			// version read as "nothing happened" in the director's hands.
			const float Restore = FMath::Max(1.f, Clock->GetLastAgentSpeed());
			Clock->SetSpeed(Restore);
			UE_LOG(LogRedHopeSim, Display, TEXT("ERA REFUSED: %s - holding %.0fx"), *Why, Restore);
			OnAlert.Broadcast(FString::Printf(TEXT("60x unavailable: %s — holding %.0fx"), *Why, Restore));
			return;
		}
	}
	const int32 EraSteps = Clock->GetEraStepsThisFrame();
	for (int32 i = 0; i < EraSteps; ++i)
	{
		StepEndOverride = FrameEndSeconds - (double)(EraSteps - 1 - i) * URHSimClockSubsystem::EraStepSimSeconds;
		EraStep(URHSimClockSubsystem::EraStepSimSeconds);
	}
	StepEndOverride = -1.0;
}

void URHSimWorldSubsystem::StepSim(float SubDt)
{
	DeployFleetOnce();

	// Fixed order: orders land, work is posted, factories run, the quota
	// arc advances, power settles.
	StepEventEdges();
	StepUplink();
	StepTaskBoard(SubDt);
	StepProduction(SubDt);
	StepDepositAlerts(); // D2a: same position in both bands (parity)
	StepHabitability(SubDt);
	StepAgriculture(SubDt);
	StepPopulation(SubDt);
	StepHope(SubDt); // after population so the mood reflects this step's support state
	StepGrowth(SubDt); // a flourishing colony grows (reads the mood StepHope just set)
	StepBigArrival(SubDt); // and one that holds its Hope draws the big wave, once (C2b)
	StepCityGrowth(SubDt); // and its districts grow, one project a sol (C5b)
	StepDiscovery(SubDt); // and its labs uncover what Mars is hiding
	StepTrade(SubDt); // the convoy rolls toward the neighbors
	StepEarth(SubDt); // and Earth watches Mars connect to itself
	StepQuota();
	StepPower(SubDt);
	if (bCommuteAudit)
	{
		AuditCommuteStep();
	}
}

void URHSimWorldSubsystem::StepEventEdges()
{
	const FRHEventRow* Event = GetActiveEvent();
	const bool bActive = Event != nullptr;
	if (bActive && !bEventWasActive)
	{
		// Onset (director ruling): snap ANY speed to 1x on the spot - the
		// player gets real time to batten down. They may re-speed at will.
		LastEventType = Event->Type;
		if (Clock && Clock->GetSpeed() > 1.f)
		{
			Clock->SetSpeed(1.f);
		}
		const bool bStorm = Event->Type == FName("DustStorm");
		const FString Alert = bStorm
			? FString::Printf(TEXT("DUST STORM ONSET — solar dropping to %.0f%%. Speed set to 1x: batten down. Robots working outside wear %.1fx faster."),
				Event->Severity * 100.f, StormWearMul)
			: FString::Printf(TEXT("SOLAR FLARE — exposed units wearing %.1fx faster until sol %.1f. Speed set to 1x."),
				Event->Severity, Event->StartSol + Event->DurationSols);
		UE_LOG(LogRedHopeSim, Display, TEXT("=== %s ==="), *Alert);
		OnAlert.Broadcast(Alert);
	}
	else if (!bActive && bEventWasActive)
	{
		const FString Alert = LastEventType == FName("DustStorm")
			? FString(TEXT("The dust storm has passed. Solar output restored."))
			: FString(TEXT("The solar flare has subsided."));
		UE_LOG(LogRedHopeSim, Display, TEXT("=== %s ==="), *Alert);
		OnAlert.Broadcast(Alert);
	}
	bEventWasActive = bActive;
}

bool URHSimWorldSubsystem::CanEnterEraMode(FString& OutReason) const
{
	for (const FRHBuildingInstance& B : Buildings)
	{
		if (B.bUnderConstruction)
		{
			OutReason = FString::Printf(TEXT("construction in progress (%s #%d)"), *B.DefName.ToString(), B.Id);
			return false;
		}
	}
	// C4: a road edge under construction is agent-fidelity work too - the era
	// band parks the fabricators, so it would never finish (and era logistics does
	// not know road distance).
	for (const FRHRoadEdge& E : RoadGraph.Edges)
	{
		if (!E.bBuilt)
		{
			OutReason = FString::Printf(TEXT("road construction in progress (edge #%d)"), E.Id);
			return false;
		}
	}
	if (QuotaPhase == ERHQuotaPhase::ShipInbound)
	{
		OutReason = TEXT("supply ship inbound");
		return false;
	}
	// Field operations are agent-fidelity by definition: a scout mid-drive or
	// a maintenance call cannot integrate analytically. (Repair never posts a
	// board task - the claim set is its ledger.)
	if (RepairClaims.Num() > 0)
	{
		OutReason = TEXT("maintenance call in progress");
		return false;
	}
	for (const FRHTask& T : Tasks)
	{
		if (T.Type == ERHTaskType::Survey)
		{
			OutReason = TEXT("survey in progress");
			return false;
		}
	}
	// World pressure (director ruling 2026-07-07b): "sleep through the siege,
	// never through the onset, never through a flare." A steady-state dust
	// storm may be era-skipped (the onset was experienced at 1x); flares
	// never - their whole life is the emergency.
	if (const FRHEventRow* Event = GetActiveEvent())
	{
		if (Event->Type == FName("SolarFlare"))
		{
			OutReason = TEXT("SolarFlare in progress");
			return false;
		}
	}
	if (Defs && Clock)
	{
		const double SolNow = Clock->GetSimSecondsTotal() / URHSimClockSubsystem::SolLengthSimSeconds;
		bool bImminent = false;
		FName ImminentType;
		// Long-game weather (v25): when the cycle knob is on and we're past the
		// authored schedule, onset distances are measured on the FOLDED sol -
		// the era must still drop for every repeated storm's onset (ruling
		// 2026-07-07b holds forever, not just for the authored fortnight).
		// Wrap-aware: an onset just past the cycle boundary is still ahead.
		const double FoldedNow = FoldEventSol(SolNow);
		const double CycleSpan = EventCycleSpan();
		Defs->ForEachEvent([&](FName, const FRHEventRow& Row)
		{
			// Within one era step's horizon of onset (in sols).
			const double HorizonSols = (double)URHSimClockSubsystem::EraStepSimSeconds / URHSimClockSubsystem::SolLengthSimSeconds;
			double DistSols = (double)Row.StartSol - FoldedNow;
			if (CycleSpan > 0.0 && DistSols < 0.0)
			{
				DistSols += CycleSpan;
			}
			if (!bImminent && DistSols > 0.0 && DistSols <= HorizonSols)
			{
				bImminent = true;
				ImminentType = Row.Type;
			}
		});
		if (bImminent)
		{
			OutReason = FString::Printf(TEXT("%s imminent"), *ImminentType.ToString());
			return false;
		}
	}
	if (QuotaPhase == ERHQuotaPhase::Open && Defs && Clock)
	{
		if (const FRHQuotaRow* Quota = Defs->GetQuota(ActiveQuota))
		{
			// Ladder deadlines are relative to the quota's opening sol; the
			// first quota opens at sol 0, so this reads as it always did.
			const double DeadlineSimSeconds = (double)(QuotaOpenedSol + Quota->DeadlineSol + 1) * URHSimClockSubsystem::SolLengthSimSeconds;
			// Refuse only during the last on-time sol: once the deadline sol has
			// passed, era mode cannot change the on-time result, and a late colony
			// must not be locked out of 60x for as long as the ladder repeats.
			const double Left = DeadlineSimSeconds - Clock->GetSimSecondsTotal();
			if (Left > 0.0 && Left < URHSimClockSubsystem::SolLengthSimSeconds)
			{
				OutReason = TEXT("quota deadline within one sol");
				return false;
			}
		}
	}
	return true;
}

void URHSimWorldSubsystem::EraStep(float DtSimSeconds)
{
	DeployFleetOnce();

	// Same spine as StepSim at a coarser dt: the sub-step functions are
	// already dimensionally honest, so era mode is the same integrator run
	// at 1 sim-minute. Only dig/haul need the abstract stand-in (agents park).
	StepEventEdges(); // storm END mid-era alerts; onsets can't occur here (imminent check drops first)
	StepUplink();
	EraLogistics(DtSimSeconds);
	StepProduction(DtSimSeconds);
	StepDepositAlerts(); // D2a: same position in both bands (parity)
	StepHabitability(DtSimSeconds);
	StepAgriculture(DtSimSeconds);
	StepPopulation(DtSimSeconds);
	StepHope(DtSimSeconds); // same order as the agent band (parity)
	StepGrowth(DtSimSeconds); // same order as the agent band (parity)
	StepBigArrival(DtSimSeconds); // same order as the agent band (parity)
	StepCityGrowth(DtSimSeconds); // same order as the agent band (parity)
	StepDiscovery(DtSimSeconds); // same order as the agent band (parity)
	StepTrade(DtSimSeconds); // same order as the agent band (parity)
	StepEarth(DtSimSeconds); // same order as the agent band (parity)
	StepQuota();
	StepPower(DtSimSeconds);
	if (bCommuteAudit)
	{
		AuditCommuteStep();
	}
}

void URHSimWorldSubsystem::EraLogistics(float DtSimSeconds)
{
	// Aggregate dig rate of the parked excavator fleet (kg per sol-hour),
	// derated by the charge duty cycle the agent band lives under: a robot
	// works Battery x (resume - seek) / DrawWork hours, then spends
	// Battery x (resume - seek) / PadRate hours docked. Every term is a data
	// row - a derived physical constant, not a tuning knob (paired-run
	// finding #3: ungated era dig ran ~8% hot from skipped charge trips).
	const FRHBuildingRow* PadDef = Defs->GetBuilding(FName("ChargePad"));
	const double PadRateW = PadDef ? PadDef->PowerDraw_W : 500.0;
	double DigRateKgPerH = 0.0;
	for (const auto& Fleet : FleetCounts)
	{
		if (const FRHRobotRow* Row = Defs->GetRobot(Fleet.Key))
		{
			if (Row->RobotClass == FName("Excavator"))
			{
				const double WorkH = Row->Battery_Wh * (ChargeResumeFraction - ChargeSeekFraction) / FMath::Max(1.f, Row->DrawWork_W);
				const double ChargeH = Row->Battery_Wh * (ChargeResumeFraction - ChargeSeekFraction) / PadRateW;
				const double Duty = WorkH / FMath::Max(0.1, WorkH + ChargeH);
				DigRateKgPerH += (double)Fleet.Value * Row->WorkRate * Duty;
			}
		}
	}
	double DigBudgetKg = DigRateKgPerH * (DtSimSeconds / 50.0); // sol-hour = 50 sim-s

	// Night gate (paired-run finding #2): the agent band's excavators halt
	// after dark whenever the bank is empty (pads shed, robots dock till
	// dawn) - ungated era digging ran ~70% hot. Same physical constraint,
	// integrated: no sun and no stored energy means no work.
	if (Defs->EvalSolarCurve(Clock->GetSolFraction()) <= 0.f && Power.BatteryWh <= 0.0)
	{
		DigBudgetKg = 0.0;
	}

	// Ground -> demanding hopper at fleet rate (piles are an agent-band
	// fidelity detail; era mode digs straight into the chain).
	for (FRHDepositState& D : Deposits)
	{
		if (DigBudgetKg <= 0.0)
		{
			break;
		}
		if (!D.bDesignated || D.RemainingKg <= 0.0)
		{
			continue;
		}
		for (FRHBuildingInstance& B : Buildings)
		{
			if (B.bUnderConstruction || B.Level != D.Level)
			{
				continue;
			}
			const bool bWants = Defs->FindRunnableRecipe(B.DefName, [&](const TMap<FName, double>& Inputs)
			{
				const double* Need = Inputs.Find(D.Type);
				if (!Need)
				{
					return false;
				}
				const double* Have = B.InputKg.Find(D.Type);
				return (!Have || *Have < *Need * 2.0);
			}) != nullptr;
			if (!bWants)
			{
				continue;
			}
			const double TakeKg = FMath::Min3(DigBudgetKg, D.RemainingKg, 200.0);
			D.RemainingKg -= TakeKg;
			B.InputKg.FindOrAdd(D.Type) += TakeKg;
			DigBudgetKg -= TakeKg;
			break;
		}
		// Leftover budget digs to a store, exactly like the agent band's
		// pile->hauler->Stockpile flow (paired-run finding: era extracted 27%
		// less and held 58% less raw regolith because the surplus dig was
		// simply never modeled - the Session 9 "digs only to feed demanding
		// consumers" limitation, now closed).
		if (DigBudgetKg > 0.0)
		{
			for (FRHBuildingInstance& S : Buildings)
			{
				if (!S.bUnderConstruction && S.Level == D.Level && (S.DefName == NAME_Stockpile || S.DefName == NAME_Lander))
				{
					const double TakeKg = FMath::Min(DigBudgetKg, D.RemainingKg);
					D.RemainingKg -= TakeKg;
					S.InputKg.FindOrAdd(D.Type) += TakeKg;
					DigBudgetKg -= TakeKg;
					break;
				}
			}
		}
	}

	// Finished outputs teleport to demanders, else to a store. Haulers are
	// not modeled above the agent band (M1 acceptance: <=5% divergence over
	// 10 sols vs the agent sim; the paired-run harness owns that bar).
	for (FRHBuildingInstance& B : Buildings)
	{
		if (B.bUnderConstruction || B.DefName == NAME_Stockpile || B.DefName == NAME_Lander)
		{
			continue;
		}
		for (auto& Out : B.OutputKg)
		{
			if (Out.Value <= 0.0)
			{
				continue;
			}
			FRHBuildingInstance* Dest = nullptr;
			for (FRHBuildingInstance& C : Buildings)
			{
				if (C.bUnderConstruction || C.Id == B.Id || C.Level != B.Level)
				{
					continue;
				}
				if (Defs->FindRunnableRecipe(C.DefName, [&](const TMap<FName, double>& Inputs)
					{
						const double* Need = Inputs.Find(Out.Key);
						if (!Need)
						{
							return false;
						}
						const double* Have = C.InputKg.Find(Out.Key);
						return (!Have || *Have < *Need * 2.0);
					}))
				{
					Dest = &C;
					break;
				}
			}
			if (!Dest)
			{
				for (FRHBuildingInstance& S : Buildings)
				{
					if (!S.bUnderConstruction && S.Id != B.Id && S.Level == B.Level && (S.DefName == NAME_Stockpile || S.DefName == NAME_Lander))
					{
						Dest = &S;
						break;
					}
				}
			}
			if (Dest)
			{
				Dest->InputKg.FindOrAdd(Out.Key) += Out.Value;
				Out.Value = 0.0;
			}
		}
	}
}

void URHSimWorldSubsystem::StepUplink()
{
	const double Now = Clock->GetSimSecondsTotal();
	for (int32 i = 0; i < UplinkQueue.Num();)
	{
		if (UplinkQueue[i].ExecuteAtSimSeconds <= Now)
		{
			FRHCommand Cmd = UplinkQueue[i];
			UplinkQueue.RemoveAt(i);
			ExecuteCommand(Cmd);
		}
		else
		{
			++i;
		}
	}
}

void URHSimWorldSubsystem::StepTaskBoard(float SubDt)
{
	// C6a: the active cargo rovers, counted once a step (a rover's wear and charge are fragments). With none,
	// every task is posted exactly as before and nothing below this block differs from v39.
	{
		static const FName NRover(TEXT("Rover"));
		const URHAgentSubsystem* AgentsForRovers = bFleetHold ? nullptr : GetWorld()->GetSubsystem<URHAgentSubsystem>();
		ActiveRovers = AgentsForRovers ? AgentsForRovers->CountActiveRobots(NRover, WearHaltThreshold) : 0;
		RefreshFreightFlags();
		if (ActiveRovers > 0 && SubDt > 0.f)
		{
			for (FRHTask& T : Tasks)
			{
				if (T.Type == ERHTaskType::Haul && T.bFreight && !T.ClaimedBy.IsValid())
				{
					T.UnclaimedAgentSec += SubDt; // agent-band seconds only: EraStep never posts or claims
				}
			}
		}
	}
	static const FName NRoverRow(TEXT("RC_R"));
	const FRHRobotRow* RoverRow = ActiveRovers > 0 ? Defs->GetRobot(NRoverRow) : nullptr;
	const double RoverCapKg = RoverRow ? (double)RoverRow->Cargo_kg : 0.0;
	// A rover trip's load: what is there, the rover's hold, and (for a consumer) what its hopper has room for.
	const auto RoverLoadKg = [RoverCapKg](double AvailableKg, double RoomKg)
	{
		double Kg = FMath::Min(AvailableKg, RoverCapKg);
		if (RoomKg >= 0.0)
		{
			Kg = FMath::Min(Kg, RoomKg);
		}
		return Kg;
	};
	// Haul: deposit piles worth collecting -> the NEAREST production building
	// that wants that resource (a slice recipe consumes it and the hopper holds
	// under two batches), else a store (Stockpile or Lander). A store only takes
	// the pile when nothing on the level consumes it, or it is a construction
	// material (the rule the output leg already follows, D2a): regolith the
	// Forge is not ready for waits in its pile. Before this, the surplus went to
	// the Lander for good - nothing brings store regolith back to the Forge - and
	// 50 t of Regolith_A's 60 t sat there by sol 45 while the Forge starved.
	for (const FRHDepositState& D : Deposits)
	{
		if (D.PileKg < HaulLoadMinKg)
		{
			continue;
		}
		FRHSiteRef From; From.DepositId = D.Id;

		int32 DestId = 0;
		double BestDistCm = 0.0;
		bool bAnyConsumer = false; // some building here takes it, hopper full or not
		double DestRoomKg = -1.0;  // C6a: a consumer's room (2 x Need - Have) at the chosen destination; < 0 = a store, uncapped
		double CandRoomKg = 0.0;
		const auto Consider = [&](const FRHBuildingInstance& Cand, double RoomKg)
		{
			const double DistCm = FVector::DistXY(D.LocationCm, Cand.LocationCm);
			if (DestId == 0 || DistCm < BestDistCm || (DistCm == BestDistCm && Cand.Id < DestId))
			{
				DestId = Cand.Id;
				BestDistCm = DistCm;
				DestRoomKg = RoomKg;
			}
		};
		for (const FRHBuildingInstance& B : Buildings)
		{
			if (B.bUnderConstruction || B.Level != D.Level)
			{
				continue;
			}
			if (Defs->FindRunnableRecipe(B.DefName, [&](const TMap<FName, double>& Inputs)
				{
					const double* Need = Inputs.Find(D.Type);
					if (!Need)
					{
						return false;
					}
					bAnyConsumer = true;
					const double* Have = B.InputKg.Find(D.Type);
					const bool bRoom = (!Have || *Have < *Need * 2.0);
					if (bRoom)
					{
						CandRoomKg = *Need * 2.0 - (Have ? *Have : 0.0);
					}
					return bRoom;
				}))
			{
				Consider(B, CandRoomKg);
			}
		}
		if (DestId == 0 && (!bAnyConsumer || ConstructionMaterials.Contains(D.Type)))
		{
			for (const FRHBuildingInstance& B : Buildings)
			{
				if (!B.bUnderConstruction && B.Level == D.Level && (B.DefName == NAME_Stockpile || B.DefName == NAME_Lander))
				{
					Consider(B, -1.0);
				}
			}
		}
		if (DestId != 0)
		{
			FRHSiteRef To; To.BuildingId = DestId;
			if (!HasOpenTask(ERHTaskType::Haul, From, To))
			{
				FRHTask T;
				T.Id = NextTaskId++;
				T.Type = ERHTaskType::Haul;
				T.From = From;
				T.To = To;
				T.Resource = D.Type;
				SetHaulAmount(T, FMath::Min(D.PileKg, 200.0), RoverLoadKg(D.PileKg, DestRoomKg));
				Tasks.Add(T);
			}
		}
	}

	// Haul: production outputs -> demanding consumer (recipe wants it and the
	// hopper holds under two batches), else -> Stockpile/Lander store. This
	// routes drill ice to the Water Plant and Forge Struct to stores with
	// one rule. D2a: the NEAREST consumer (then the nearest store) wins, ties
	// to the lower building Id - a second Water Plant beside a far drill must
	// beat the first one, or every load crosses the map.
	for (const FRHBuildingInstance& B : Buildings)
	{
		for (const auto& Out : B.OutputKg)
		{
			if (Out.Value < HaulLoadMinKg || B.DefName == NAME_Stockpile || B.DefName == NAME_Lander)
			{
				continue;
			}
			int32 DestId = 0;
			double BestDistCm = 0.0;
			bool bAnyConsumer = false; // some machine here takes it, hopper full or not
			double DestRoomKg = -1.0;  // C6a: see the deposit leg
			double CandRoomKg = 0.0;
			const auto Consider = [&](const FRHBuildingInstance& Cand, double RoomKg)
			{
				const double DistCm = FVector::DistXY(B.LocationCm, Cand.LocationCm);
				if (DestId == 0 || DistCm < BestDistCm || (DistCm == BestDistCm && Cand.Id < DestId))
				{
					DestId = Cand.Id;
					BestDistCm = DistCm;
					DestRoomKg = RoomKg;
				}
			};
			for (const FRHBuildingInstance& C : Buildings)
			{
				if (C.bUnderConstruction || C.Id == B.Id || C.Level != B.Level)
				{
					continue;
				}
				if (Defs->FindRunnableRecipe(C.DefName, [&](const TMap<FName, double>& Inputs)
					{
						const double* Need = Inputs.Find(Out.Key);
						if (!Need)
						{
							return false;
						}
						bAnyConsumer = true;
						const double* Have = C.InputKg.Find(Out.Key);
						const bool bRoom = (!Have || *Have < *Need * 2.0);
						if (bRoom)
						{
							CandRoomKg = *Need * 2.0 - (Have ? *Have : 0.0);
						}
						return bRoom;
					}))
				{
					Consider(C, CandRoomKg);
				}
			}
			// Overflow to a store only for what construction draws from stores,
			// or what nothing here consumes (D2a: drill ice hauled to the Lander
			// sat there for good - 7 t in the playtest - and the long trips
			// starved the Water Plant; it now waits at the drill for the plant).
			if (DestId == 0 && (!bAnyConsumer || ConstructionMaterials.Contains(Out.Key)))
			{
				for (const FRHBuildingInstance& S : Buildings)
				{
					if (!S.bUnderConstruction && (S.DefName == NAME_Stockpile || S.DefName == NAME_Lander) && S.Id != B.Id && S.Level == B.Level)
					{
						Consider(S, -1.0);
					}
				}
			}
			if (DestId != 0)
			{
				FRHSiteRef From; From.BuildingId = B.Id;
				FRHSiteRef To; To.BuildingId = DestId;
				if (!HasOpenTask(ERHTaskType::Haul, From, To))
				{
					FRHTask T;
					T.Id = NextTaskId++;
					T.Type = ERHTaskType::Haul;
					T.From = From;
					T.To = To;
					T.Resource = Out.Key;
					SetHaulAmount(T, FMath::Min(Out.Value, 200.0), RoverLoadKg(Out.Value, DestRoomKg));
					Tasks.Add(T);
				}
			}
		}
	}

	// Haul: store stock -> producer hopper, ONLY for inputs of a recipe whose
	// output construction is short of (M1-d Gate B: the Struct+Ore feed that
	// lets the Forge make the Shielding a taxed site is waiting on). Scoped to
	// construction shortage so steady-state logistics never grow this leg.
	{
		const TMap<FName, double> ShortageKg = ComputeConstructionShortage();
		const auto OutputsShort = [&ShortageKg](const FRHRecipeRow& Row)
		{
			for (const auto& Out : URHDefinitionsSubsystem::ParseResourceList(Row.Outputs))
			{
				if (ShortageKg.Contains(Out.Key))
				{
					return true;
				}
			}
			return false;
		};
		for (const FRHBuildingInstance& Store : Buildings)
		{
			if (ShortageKg.Num() == 0)
			{
				break;
			}
			if (Store.bUnderConstruction || (Store.DefName != NAME_Stockpile && Store.DefName != NAME_Lander))
			{
				continue;
			}
			for (const auto& Held : Store.InputKg)
			{
				if (Held.Value < HaulLoadMinKg)
				{
					continue;
				}
				for (const FRHBuildingInstance& C : Buildings)
				{
					if (C.bUnderConstruction || C.Id == Store.Id || C.Level != Store.Level)
					{
						continue;
					}
					double WantsRoomKg = 0.0; // C6a: the qualifying recipe's room, 2 x Need - Have
					const FRHRecipeRow* Wants = Defs->FindRunnableRecipe(C.DefName,
						[&](const TMap<FName, double>& Inputs)
						{
							const double* Need = Inputs.Find(Held.Key);
							if (!Need)
							{
								return false;
							}
							const double* Have = C.InputKg.Find(Held.Key);
							const bool bRoom = (!Have || *Have < *Need * 2.0);
							if (bRoom)
							{
								WantsRoomKg = *Need * 2.0 - (Have ? *Have : 0.0);
							}
							return bRoom;
						}, OutputsShort);
					if (!Wants)
					{
						continue;
					}
					FRHSiteRef From; From.BuildingId = Store.Id;
					FRHSiteRef To; To.BuildingId = C.Id;
					if (!HasOpenTask(ERHTaskType::Haul, From, To, Held.Key))
					{
						FRHTask T;
						T.Id = NextTaskId++;
						T.Type = ERHTaskType::Haul;
						T.From = From;
						T.To = To;
						T.Resource = Held.Key;
						SetHaulAmount(T, FMath::Min(Held.Value, 200.0), RoverLoadKg(Held.Value, WantsRoomKg));
						Tasks.Add(T);
					}
					break;
				}
			}
		}
	}

	// Haul: construction sites want their bill of materials delivered before
	// the fabricator can work (site-delivery rule; multi-resource since M1-a).
	for (const FRHBuildingInstance& Site : Buildings)
	{
		if (!Site.bUnderConstruction)
		{
			continue;
		}
		const FRHBuildingRow* Def = Defs->GetBuilding(Site.DefName);
		if (!Def)
		{
			continue;
		}
		for (const auto& Cost : GetSiteBill(Site))
		{
			const double* Delivered = Site.InputKg.Find(Cost.Key);
			const double NeedKg = Cost.Value - (Delivered ? *Delivered : 0.0);
			if (NeedKg <= 0.0)
			{
				continue;
			}
			// Source: any completed building holding that resource, on the
			// site's floor or any trunk-linked floor (M1-d: the lift carries
			// materials across levels - how underground sites get built).
			int32 SourceId = 0;
			for (const FRHBuildingInstance& S : Buildings)
			{
				if (S.bUnderConstruction || S.Id == Site.Id || !AreLevelsLinked(S.Level, Site.Level))
				{
					continue;
				}
				const double* InS = S.InputKg.Find(Cost.Key);
				const double* OutS = S.OutputKg.Find(Cost.Key);
				if ((InS && *InS > 0.0) || (OutS && *OutS > 0.0))
				{
					SourceId = S.Id;
					break;
				}
			}
			if (SourceId != 0)
			{
				FRHSiteRef From; From.BuildingId = SourceId;
				FRHSiteRef To; To.BuildingId = Site.Id;
				if (!HasOpenTask(ERHTaskType::Haul, From, To, Cost.Key))
				{
					FRHTask T;
					T.Id = NextTaskId++;
					T.Type = ERHTaskType::Haul;
					T.From = From;
					T.To = To;
					T.Resource = Cost.Key;
					SetHaulAmount(T, FMath::Min(NeedKg, 200.0), RoverLoadKg(NeedKg, -1.0));
					Tasks.Add(T);
				}
			}
		}
	}
}

void URHSimWorldSubsystem::StepProduction(float SubDt)
{
	// Time-budget integrator (M1-c era-honesty fix): each building spends the
	// step's hours across batch progress, completion, AND the next batch's
	// start within one call - overshoot carries instead of rounding every
	// batch up to a whole step. At agent dt the budget is far smaller than
	// any batch, so behavior is unchanged; at era dt (1.2 h) this is what
	// closed the 8.8% divergence (a 2 h batch no longer costs 2 whole steps
	// plus an idle step to restart).
	// Construction shortage (M1-d Gate B): what the open sites need beyond
	// colony holdings. Recipes whose outputs are short get first claim on an
	// idle building - the site's demand is what tells the Forge to make
	// Shielding instead of more Struct. Empty map = pure row-order (M0 rule).
	const TMap<FName, double> ShortageKg = ComputeConstructionShortage();
	const auto OutputsShort = [&ShortageKg](const FRHRecipeRow& Row)
	{
		for (const auto& Out : URHDefinitionsSubsystem::ParseResourceList(Row.Outputs))
		{
			if (ShortageKg.Contains(Out.Key))
			{
				return true;
			}
		}
		return false;
	};
	// D5a: the share of this step that falls in daylight - what a day-shift
	// machine may spend. The clock has already advanced to the step's end in
	// both bands (GetStepEndSeconds adds the in-game burst correction).
	const double StepEndS = GetStepEndSeconds();
	const double DayFrac = GetDaylightFraction(StepEndS - SubDt, StepEndS);

	for (FRHBuildingInstance& B : Buildings)
	{
		if (B.bUnderConstruction || B.bManualOff || (!B.bPowered && !B.bBatchOnH2))
		{
			// Shed/unbuilt: batches stall, nothing starts (M0-c shedding rule).
			// Exception (M1-d): a Hydrogen-fuelled batch bought its whole run
			// up-front - it grinds on through the brownout. That is the fuel's
			// entire strategic point. Manual OFF trumps even that: the player
			// pulled the breaker deliberately (storm discipline ruling).
			continue;
		}

		const FRHBuildingRow* BDef = Defs->GetBuilding(B.DefName);
		const bool bExtractor = BDef && BDef->RequiresDeposit;

		double BudgetH = SubDt / 50.0; // sol-hour = 50 sim-s
		if (B.bDayShift)
		{
			// D5a: works only the daylight share of this step (era parity: linear
			// in dt). At night BudgetH = 0 - the batch pauses, nothing new starts,
			// nothing is lost. An H2-fuelled batch pauses too (its fuel is paid).
			BudgetH *= DayFrac;
		}
		// Bounded loop: at most a handful of batch boundaries fit in one era
		// step; the guard is against a zero-time recipe row, not real data.
		for (int32 Guard = 0; Guard < 8 && BudgetH > 0.0; ++Guard)
		{
			if (B.BatchRemaining_h > 0.0)
			{
				const double SpendH = FMath::Min(BudgetH, B.BatchRemaining_h);
				B.BatchRemaining_h -= SpendH;
				BudgetH -= SpendH;
				if (B.BatchRemaining_h > 0.0)
				{
					break; // budget exhausted mid-batch
				}
				// Batch complete: grant outputs (hybrid rule - solids drop at
				// the building for hauling; fluids/gases join the pool).
				if (const TMap<FName, double>* Outputs = PendingOutputs.Find(B.Id))
				{
					for (const auto& Res : *Outputs)
					{
						if (Defs->IsSolidResource(Res.Key))
						{
							B.OutputKg.FindOrAdd(Res.Key) += Res.Value;
							OnStockChanged.Broadcast(Res.Key, GetTotalSolid(Res.Key));
						}
						else
						{
							AddStock(Res.Key, Res.Value);
							NoteProduced(Res.Key, Res.Value); // C5d: the production ledger (Oxygen, Water; nothing else is a leg)
							// Fresh ice-melt makeup restores water potability (M2
							// Gate D+ water loop). MeltIce is the only Water output;
							// StepPopulation consumes this and resets it each step.
							if (Res.Key == FName("Water"))
							{
								FreshWaterThisStepKg += Res.Value;
							}
						}
					}
					PendingOutputs.Remove(B.Id);
				}
				UE_LOG(LogRedHopeSim, Display, TEXT("%s #%d batch complete (%s)"),
					*B.DefName.ToString(), B.Id, *B.ActiveRecipe.ToString());
				// M1-d: designation work applies its world change at completion -
				// the spoil already dropped with the outputs above.
				if (const FIntPoint* Work = PendingBoreWork.Find(B.Id))
				{
					if (Work->X == 0) // bore: the trunk descends one floor
					{
						if (ShaftDepth == 0)
						{
							ShaftHeadCm = B.LocationCm; // the column is fixed by the Borer
						}
						++ShaftDepth;
						UE_LOG(LogRedHopeSim, Display, TEXT("Shaft reached floor -%d%s"), ShaftDepth,
							ShaftDepth >= BoreTargetDepth ? TEXT(" - bore designation complete") : TEXT(""));
					}
					else // carve: one cell opens on its floor
					{
						CarveNextCell(Work->Y);
						UE_LOG(LogRedHopeSim, Display, TEXT("Carved cell on floor %d (%d carved, %d queued)"),
							Work->Y, GetFloorCarvedCells(Work->Y), GetCarveQueued(Work->Y));
					}
					PendingBoreWork.Remove(B.Id);
				}
				B.ActiveRecipe = NAME_None;
				B.bBatchOnH2 = false;
				continue; // leftover budget flows into the next batch below
			}

			if (!B.bPowered)
			{
				break; // shed: an H2 batch may finish above, but nothing new starts
			}

			// M1-d Gate A2: designation-driven work first (the Borer). The
			// player's queue is the gate - these recipes never pass the
			// runnable search below. Bore before carve; carve shallowest-first
			// over sorted keys (TMap iteration order is not deterministic).
			if (BDef && BDef->CanBore)
			{
				FName WorkRecipeName = NAME_None;
				FIntPoint WorkItem(0, 0);
				bool bFloorWorkInFlight = false;
				for (const auto& W : PendingBoreWork)
				{
					if (W.Value.X == 0) { bFloorWorkInFlight = true; break; }
				}
				if (BoreTargetDepth > ShaftDepth && !bFloorWorkInFlight)
				{
					WorkRecipeName = FName("BoreFloor");
					WorkItem = FIntPoint(0, -(ShaftDepth + 1));
				}
				else
				{
					TArray<int32> Levels;
					CarveQueue.GenerateKeyArray(Levels);
					Levels.Sort([](int32 A, int32 C) { return A > C; }); // -1 before -2
					for (int32 L : Levels)
					{
						if (CarveQueue[L] > 0 && IsLevelConnected(L))
						{
							WorkRecipeName = FName("CarveCell");
							WorkItem = FIntPoint(1, L);
							break;
						}
					}
				}
				if (const FRHRecipeRow* Work = WorkRecipeName.IsNone() ? nullptr : Defs->GetRecipe(WorkRecipeName))
				{
					// Fuel decision at start, committed like extraction: the
					// whole batch's Hydrogen deducts up-front if stocked, else
					// the batch draws grid power.
					B.bBatchOnH2 = false;
					if (BDef->H2BurnKgPerHour > 0.f)
					{
						const double FuelKg = BDef->H2BurnKgPerHour * Work->BatchTime_h;
						if (GetStock(FName("Hydrogen")) >= FuelKg)
						{
							AddStock(FName("Hydrogen"), -FuelKg);
							B.bBatchOnH2 = true;
						}
					}
					if (WorkItem.X == 1)
					{
						--CarveQueue[WorkItem.Y]; // committed at start
					}
					PendingBoreWork.Add(B.Id, WorkItem);
					B.ActiveRecipe = WorkRecipeName;
					PendingOutputs.Add(B.Id, URHDefinitionsSubsystem::ParseResourceList(Work->Outputs));
					B.BatchRemaining_h = Work->BatchTime_h;
					UE_LOG(LogRedHopeSim, Display, TEXT("%s #%d %s started%s (%.0f h, floor %d)"),
						*B.DefName.ToString(), B.Id, *WorkRecipeName.ToString(),
						B.bBatchOnH2 ? TEXT(" on H2") : TEXT(""), Work->BatchTime_h, WorkItem.Y);
					continue; // budget flows into the new batch
				}
			}

			// Idle: try to start a batch whose inputs are covered - solids
			// from the hopper, fluids from the network pool, extraction from
			// the attached deposit. Construction-short outputs outrank row
			// order (Gate B); no shortage = the M0 rule exactly.
			const auto InputsOk = [&](const TMap<FName, double>& Inputs)
			{
				if (Inputs.Num() == 0)
				{
					if (!bExtractor || B.AttachedDepositId == 0)
					{
						return false;
					}
					const FRHDepositState* D = nullptr;
					for (const FRHDepositState& Dep : Deposits)
					{
						if (Dep.Id == B.AttachedDepositId) { D = &Dep; break; }
					}
					return D && D->RemainingKg > 0.0;
				}
				for (const auto& In : Inputs)
				{
					if (Defs->IsSolidResource(In.Key))
					{
						const double* Have = B.InputKg.Find(In.Key);
						if (!Have || *Have < In.Value)
						{
							return false;
						}
					}
					else if (GetStock(In.Key) < In.Value)
					{
						return false;
					}
				}
				return true;
			};
			const FRHRecipeRow* Recipe = ShortageKg.Num() > 0
				? Defs->FindRunnableRecipe(B.DefName, InputsOk, OutputsShort)
				: nullptr;
			if (!Recipe)
			{
				Recipe = Defs->FindRunnableRecipe(B.DefName, InputsOk);
			}
			if (!Recipe)
			{
				break; // nothing startable: the rest of the budget is idle time
			}

			const TMap<FName, double> Inputs = URHDefinitionsSubsystem::ParseResourceList(Recipe->Inputs);
			const TMap<FName, double> Outputs = URHDefinitionsSubsystem::ParseResourceList(Recipe->Outputs);

			if (Inputs.Num() == 0 && bExtractor)
			{
				// Draw the output mass from the ground now (committed batch).
				double OutMassKg = 0.0;
				for (const auto& O : Outputs)
				{
					OutMassKg += O.Value;
				}
				FRHDepositState* D = FindDeposit(B.AttachedDepositId);
				if (!D || D->RemainingKg < OutMassKg)
				{
					break; // not enough left for a full batch
				}
				D->RemainingKg -= OutMassKg;
			}
			for (const auto& In : Inputs)
			{
				if (Defs->IsSolidResource(In.Key))
				{
					B.InputKg.FindOrAdd(In.Key) -= In.Value;
				}
				else
				{
					AddStock(In.Key, -In.Value);
				}
			}
			B.ActiveRecipe = FName(*Recipe->Outputs); // display only; outputs cached:
			PendingOutputs.Add(B.Id, Outputs);
			B.BatchRemaining_h = Recipe->BatchTime_h;
			UE_LOG(LogRedHopeSim, Display, TEXT("%s #%d batch started (%s -> %s)"),
				*B.DefName.ToString(), B.Id, *Recipe->Inputs, *Recipe->Outputs);
		}
	}
}

// D2a. Era-parity-safe: a level-triggered threshold on a MONOTONE quantity
// (RemainingKg only falls), latched once per deposit, so it never re-arms or
// flaps. Both bands detect the crossing on the step it happens. It feeds
// nothing back into sim state, and writes no log line, so silent runs stay
// byte-identical.
void URHSimWorldSubsystem::StepDepositAlerts()
{
	for (const FRHDepositState& D : Deposits)
	{
		if (!IsIceDepositLow(D) || IceLowAnnounced.Contains(D.Id))
		{
			continue;
		}
		IceLowAnnounced.Add(D.Id);
		// The remedy named depends on what can still be done (Gate-D
		// PLACEHOLDER wording): never tell the player to find ice that is not
		// there. Untouched known ice outranks surveying.
		const TCHAR* Advice = TEXT("Survey for more ice and carry the grid to it.");
		if (HasHealthyDiscoveredIce())
		{
			Advice = TEXT("Drill the untouched ice before this runs out.");
		}
		else if (!HasUndiscoveredIce())
		{
			Advice = TEXT("The last known ice. Stretch the water: recycling makes it last, and the Water Plant restores it while ice remains.");
		}
		OnAlert.Broadcast(FString::Printf(TEXT("%s RUNNING LOW — about %.0f t of ice left. %s"),
			*D.RowName.ToString().ToUpper(), D.RemainingKg / 1000.0, Advice));
	}
}

bool URHSimWorldSubsystem::IsIceDepositLow(const FRHDepositState& D) const
{
	return D.Type == FName("Ice") && D.bDiscovered && D.InitialKg > 0.0 && IceLowAlertFraction > 0.0
		&& D.RemainingKg < IceLowAlertFraction * D.InitialKg;
}

bool URHSimWorldSubsystem::HasUndiscoveredIce() const
{
	for (const FRHDepositState& D : Deposits)
	{
		if (D.Type == FName("Ice") && !D.bDiscovered)
		{
			return true;
		}
	}
	return false;
}

bool URHSimWorldSubsystem::HasHealthyDiscoveredIce() const
{
	for (const FRHDepositState& D : Deposits)
	{
		if (D.Type == FName("Ice") && D.bDiscovered && D.RemainingKg > 0.0 && !IsIceDepositLow(D))
		{
			return true;
		}
	}
	return false;
}

// What the player can still do about a low ice deposit, as one pure read the
// guide and the suite share: -1 nothing to resolve (no low deposit, or no way
// out - the last ice), 0 find more, 1 carry the grid to healthy ice, 2 drill it,
// 3 melt it where it is drilled (a drilled healthy deposit with no finished
// Water Plant within IceWaterPlantNear_m: its ice is a long haul from any plant).
int32 URHSimWorldSubsystem::GetIceExpansionStage() const
{
	bool bAnyLow = false;
	for (const FRHDepositState& D : Deposits)
	{
		bAnyLow |= IsIceDepositLow(D);
	}
	if (!bAnyLow)
	{
		return -1;
	}
	if (!HasHealthyDiscoveredIce())
	{
		return HasUndiscoveredIce() ? 0 : -1;
	}
	bool bReached = false, bDrilled = false, bMelted = false;
	for (const FRHDepositState& D : Deposits)
	{
		if (D.Type != FName("Ice") || !D.bDiscovered || D.RemainingKg <= 0.0 || IsIceDepositLow(D))
		{
			continue;
		}
		bool bHasDrill = false, bHasPlant = false;
		for (const FRHBuildingInstance& B : Buildings)
		{
			bHasDrill |= B.DefName == FName("IceDrill") && B.AttachedDepositId == D.Id;
			bHasPlant |= B.DefName == FName("WaterPlant") && !B.bUnderConstruction && B.Level == D.Level
				&& FVector::DistXY(B.LocationCm, D.LocationCm) <= IceWaterPlantNear_m * 100.0;
		}
		bDrilled |= bHasDrill;
		bMelted |= bHasDrill && bHasPlant;
		bReached |= bHasDrill || IsInCoverage(D.LocationCm, 0);
	}
	if (bDrilled)
	{
		return bMelted ? -1 : 3;
	}
	return bReached ? 2 : 1;
}

void URHSimWorldSubsystem::StepQuota()
{
	const FRHQuotaRow* Quota = Defs->GetQuota(ActiveQuota);
	if (!Quota)
	{
		return;
	}

	if (QuotaPhase == ERHQuotaPhase::Open)
	{
		for (const auto& Req : GetQuotaProgress())
		{
			if (Req.Value.Key < Req.Value.Value)
			{
				return;
			}
		}
		QuotaPhase = ERHQuotaPhase::AwaitingManifest;
		QuotaMetSol = Clock->GetSol();
		// Deadlines are relative to the sol the quota OPENED (the ladder:
		// later quotas open when their predecessor's ship lands). The first
		// quota opens at sol 0, so every M0 pin reads identically.
		const bool bOnTime = (QuotaMetSol - QuotaOpenedSol) <= Quota->DeadlineSol;
		// M3 Gate B: the requisition multiplier scales the award by your standing
		// with Earth (identity axis + tension). Exactly 1.0 until you have
		// neighbors, so the M0-M2 award is unchanged.
		const double ReqMult = GetRequisitionMultiplier();
		AwardMassKg = (bOnTime ? Quota->OnTimeAward_kg : Quota->LateAward_kg) * ReqMult;
		// Research pays (v25): banked discovery funding rides the next award.
		const double Funding = PendingResearchFundingKg;
		if (Funding > 0.0)
		{
			AwardMassKg += Funding;
			PendingResearchFundingKg = 0.0;
		}
		UE_LOG(LogRedHopeSim, Display,
			TEXT("=== CEO TRANSMISSION (Sol %d): Quota %s%s met%s. Supply ship authorized: %.0f kg manifest budget%s%s. Compose and launch. ==="),
			QuotaMetSol, *ActiveQuota.ToString(),
			QuotaRepeatRound > 0 ? *FString::Printf(TEXT(" (round %d)"), QuotaRepeatRound) : TEXT(""),
			bOnTime ? TEXT(" ON TIME") : TEXT(" (late)"), AwardMassKg,
			ReqMult < 0.999 ? *FString::Printf(TEXT(" (requisition x%.2f - your Martian ties)"), ReqMult)
				: (ReqMult > 1.001 ? *FString::Printf(TEXT(" (requisition x%.2f - Earth loyalty)"), ReqMult) : TEXT("")),
			Funding > 0.0 ? *FString::Printf(TEXT(" (+%.0f kg research funding)"), Funding) : TEXT(""));
		OnQuotaMet.Broadcast(QuotaMetSol, AwardMassKg);
	}
	else if (QuotaPhase == ERHQuotaPhase::ShipInbound)
	{
		// Arrival countdown (director ruling): loud alerts at T-2 and T-1
		// sols so the colony can prepare. Crew ships inherit this seam in M2.
		const double SolsOut = (ShipArrivalSimSeconds - Clock->GetSimSecondsTotal()) / URHSimClockSubsystem::SolLengthSimSeconds;
		if (ShipAlertStage < 1 && SolsOut <= 2.0 && SolsOut > 1.0)
		{
			ShipAlertStage = 1;
			OnAlert.Broadcast(TEXT("SUPPLY SHIP: touchdown in 2 sols — prepare the colony."));
		}
		else if (ShipAlertStage < 2 && SolsOut <= 1.0 && SolsOut > 0.0)
		{
			ShipAlertStage = 2;
			OnAlert.Broadcast(TEXT("SUPPLY SHIP: touchdown in 1 sol."));
		}
		if (Clock->GetSimSecondsTotal() >= ShipArrivalSimSeconds)
		{
			QuotaPhase = ERHQuotaPhase::Completed;
			UE_LOG(LogRedHopeSim, Display, TEXT("=== SHIP LANDED (Sol %d). Cargo transfer: %d items, %.0f kg. ==="),
				Clock->GetSol(), ManifestItems.Num(), GetManifestMassKg());
			for (const FName& Item : ManifestItems)
			{
				ApplyManifestItemEffect(Item);
			}
			// F2b: the recalled Mars-born come home first - after the cargo, ahead of the settlers.
			ApplyHomeAgain();
			// C2b (Q5a): a thriving colony draws settlers on the supply line - after the
			// cargo, so any pods and transports have taken their beds first.
			ApplyImmigration();
			// Slice end card.
			UE_LOG(LogRedHopeSim, Display,
				TEXT("=== THE PROGRAM CONTINUES: sols elapsed %d | quota met sol %d (deadline %d) | fleet + colony operational ==="),
				Clock->GetSol(), QuotaMetSol, QuotaOpenedSol + Quota->DeadlineSol);
			OnShipArrived.Broadcast(ManifestItems);
			// The goal ladder (v25): the next slice-active quota opens as this
			// one's ship lands - the program's demands keep coming.
			AdvanceQuotaLadder();
		}
	}
	else if (QuotaPhase == ERHQuotaPhase::Completed && QuotaRepeatBillMul > 0.0)
	{
		// A colony resting at Completed (a knob-0 run switched on mid-game)
		// picks the ladder back up on its next step. Otherwise this phase is
		// only ever transient inside the landing call above.
		AdvanceQuotaLadder();
	}
}

FName URHSimWorldSubsystem::FirstQuotaName() const
{
	// Smallest (DeadlineSol, name) among slice-active rows: Q1 on today's data.
	FName Best = NAME_None;
	int32 BestDeadline = MAX_int32;
	if (Defs)
	{
		Defs->ForEachQuota([&](FName Name, const FRHQuotaRow& Row)
		{
			if (Row.DeadlineSol < BestDeadline ||
				(Row.DeadlineSol == BestDeadline && (Best.IsNone() || Name.LexicalLess(Best))))
			{
				BestDeadline = Row.DeadlineSol;
				Best = Name;
			}
		});
	}
	return Best;
}

void URHSimWorldSubsystem::AdvanceQuotaLadder()
{
	// Next slice-active quota strictly after the current one in ascending
	// (DeadlineSol, name) order - a deterministic authored ladder, no RNG.
	const FRHQuotaRow* Cur = Defs ? Defs->GetQuota(ActiveQuota) : nullptr;
	if (!Cur)
	{
		return;
	}
	const int32 CurDeadline = Cur->DeadlineSol;
	const FName CurName = ActiveQuota;
	FName Best = NAME_None;
	int32 BestDeadline = MAX_int32;
	Defs->ForEachQuota([&](FName Name, const FRHQuotaRow& Row)
	{
		const bool bAfterCur = Row.DeadlineSol > CurDeadline ||
			(Row.DeadlineSol == CurDeadline && CurName.LexicalLess(Name));
		const bool bBeatsBest = Row.DeadlineSol < BestDeadline ||
			(Row.DeadlineSol == BestDeadline && (Best.IsNone() || Name.LexicalLess(Best)));
		if (bAfterCur && bBeatsBest)
		{
			BestDeadline = Row.DeadlineSol;
			Best = Name;
		}
	});
	if (Best.IsNone())
	{
		// Ladder exhausted. Knob off: the Completed phase stands as the program's
		// rest state (the pre-D1a behavior). Knob on: the LAST rung reopens with
		// its bills scaled up one more round (deadline and award unscaled).
		if (QuotaRepeatBillMul <= 0.0)
		{
			return;
		}
		Best = CurName;
		++QuotaRepeatRound;
	}
	else
	{
		QuotaRepeatRound = 0; // an authored row was found: a later rung restarts the count
	}
	ActiveQuota = Best;
	QuotaOpenedSol = Clock ? Clock->GetSol() : 0;
	QuotaPhase = ERHQuotaPhase::Open;
	QuotaMetSol = 0;
	AwardMassKg = 0.0;
	ShipAlertStage = 0;
	ManifestItems.Reset();
	const FRHQuotaRow* Next = Defs->GetQuota(Best);
	const int32 DueSol = QuotaOpenedSol + (Next ? Next->DeadlineSol : 0);
	const FString Msg = QuotaRepeatRound > 0
		? FString::Printf(TEXT("CEO TRANSMISSION — quota %s renewed (round %d): deliver %s by sol %d. The program's eyes stay on Mars."),
			*Best.ToString(), QuotaRepeatRound, *DescribeQuotaBills(), DueSol)
		: FString::Printf(TEXT("CEO TRANSMISSION — new quota %s: deliver %s by sol %d. The program's eyes stay on Mars."),
			*Best.ToString(), *DescribeQuotaBills(), DueSol);
	OnAlert.Broadcast(Msg);
	UE_LOG(LogRedHopeSim, Display, TEXT("=== QUOTA LADDER: %s opens (sol %d, deadline sol %d)%s ==="),
		*Best.ToString(), QuotaOpenedSol, DueSol,
		QuotaRepeatRound > 0 ? *FString::Printf(TEXT(" - round %d, bills x%.4g"), QuotaRepeatRound, GetQuotaBillScale()) : TEXT(""));
}

double URHSimWorldSubsystem::ScaleQuotaBill(double RawKg, int32 Round) const
{
	// Round 0 keeps the raw bill (bit-identical to pre-D1a); renewals scale it and round to whole kg
	// so the shown bill is the threshold.
	if (Round <= 0)
	{
		return RawKg;
	}
	const double Scale = QuotaRepeatBillMul <= 0.0 ? 1.0 : FMath::Pow(QuotaRepeatBillMul, (double)Round); // as GetQuotaBillScale
	return FMath::RoundToDouble(RawKg * Scale);
}

void URHSimWorldSubsystem::GetNextQuotaBills(TMap<FName, double>& Out) const
{
	// Which row AdvanceQuotaLadder would open next, by the same (DeadlineSol, name) walk, and at
	// which round; its bills through the same scaling GetQuotaProgress uses.
	Out.Reset();
	const FRHQuotaRow* Cur = Defs ? Defs->GetQuota(ActiveQuota) : nullptr;
	if (!Cur)
	{
		return;
	}
	FName Best = NAME_None;
	int32 BestDeadline = MAX_int32;
	Defs->ForEachQuota([&](FName Name, const FRHQuotaRow& Row)
	{
		const bool bAfterCur = Row.DeadlineSol > Cur->DeadlineSol ||
			(Row.DeadlineSol == Cur->DeadlineSol && ActiveQuota.LexicalLess(Name));
		const bool bBeatsBest = Row.DeadlineSol < BestDeadline ||
			(Row.DeadlineSol == BestDeadline && (Best.IsNone() || Name.LexicalLess(Best)));
		if (bAfterCur && bBeatsBest)
		{
			BestDeadline = Row.DeadlineSol;
			Best = Name;
		}
	});
	int32 Round = 0;
	if (Best.IsNone())
	{
		if (QuotaRepeatBillMul <= 0.0)
		{
			return; // the ladder ends: nothing is coming
		}
		Best = ActiveQuota;
		Round = QuotaRepeatRound + 1;
	}
	if (const FRHQuotaRow* Next = Defs->GetQuota(Best))
	{
		for (const auto& Req : URHDefinitionsSubsystem::ParseResourceList(Next->Requirements))
		{
			Out.Add(Req.Key, ScaleQuotaBill(Req.Value, Round));
		}
	}
}

double URHSimWorldSubsystem::GetQuotaBillScale() const
{
	if (QuotaRepeatRound <= 0 || QuotaRepeatBillMul <= 0.0)
	{
		return 1.0;
	}
	return FMath::Pow(QuotaRepeatBillMul, (double)QuotaRepeatRound);
}

FString URHSimWorldSubsystem::DescribeQuotaBills() const
{
	// Player-facing bills in ParseResourceList order; Struct reads "structural",
	// as the guide already says it.
	TArray<FString> Parts;
	for (const auto& Req : GetQuotaProgress())
	{
		Parts.Add(FString::Printf(TEXT("%s %.0f kg"),
			Req.Key == FName("Struct") ? TEXT("structural") : *Req.Key.ToString(), Req.Value.Value));
	}
	return FString::Join(Parts, TEXT(", "));
}

void URHSimWorldSubsystem::ApplyManifestItemEffect(FName ItemName)
{
	// Mechanical effects keyed by row name at slice scale (the CSV Effect
	// column stays display text; data-driven effect verbs are an M1 task).
	if (ItemName == FName("SolarPack"))       { ImportStock.FindOrAdd(FName("SolarArray"))++; }
	else if (ItemName == FName("BatteryPack")) { ImportStock.FindOrAdd(FName("BatteryBank"))++; }
	else if (ItemName == FName("PartsCrate"))  { AddStock(FName("SpareParts"), 10); }
	else if (ItemName == FName("Toolkit"))     { FabricatorSpeedMul += 0.15; }
	else if (ItemName == FName("ComputeCore"))
	{
		OrderLagSeconds = Defs->GetConfigScalar(FName("OrderLagTier2_s"), 8.0);
		UE_LOG(LogRedHopeSim, Display, TEXT("Compute core online: order lag now %.0f sim-s"), OrderLagSeconds);
	}
	else if (ItemName == FName("AdvExcavator") || ItemName == FName("ExtraHauler"))
	{
		const FName RobotRow = (ItemName == FName("AdvExcavator")) ? FName("RC_E2") : FName("RC_H");
		if (const FRHRobotRow* Row = Defs->GetRobot(RobotRow))
		{
			TArray<FMassEntityHandle> NewUnits;
			SpawnRobotTracked(RobotRow, *Row, FVector(3000.f, -3000.f, 50.f), Row->Battery_Wh, 0.f, NewUnits);
			if (NewUnits.Num() > 0)
			{
				OnRobotsSpawned.Broadcast(NewUnits);
			}
		}
	}
	else if (ItemName == FName("CargoRover"))
	{
		// C6a (Q1a): the ExtraHauler pattern, but a rover needs a road to land on. It waits aboard in ImportStock
		// under its robot row name (NOT a building flat-pack: placement's lookup can never match a robot row).
		++ImportStock.FindOrAdd(FName("RC_R"));
		DeployWaitingRovers();
	}
	else if (ItemName == FName("SoilPallet")) { AddStock(FName("Soil"), 1000); }
	else if (ItemName == FName("SeedVault"))  { AddStock(FName("Seeds"), 200); }
	else if (ItemName == FName("GlassCrate")) { AddStock(FName("Glass"), 400); }
	else if (ItemName == FName("LuxuryGoods")) { AddStock(FName("Luxury"), 300); }
	else if (ItemName == FName("CrewPod"))
	{
		// M2 Gate A1: colonists disembark ONLY into certified housing - the
		// M1-d vault is the hard prerequisite, made literal. A pod that finds
		// no beds stays aboard and returns with the ship (no refund; the
		// Program does not land people into a void).
		if (GetFreeHousing() >= CrewPodColonists)
		{
			const int32 Housed = Debug_AddColonists(CrewPodColonists);
			AddStock(FName("Food"), CrewPodFoodKg);
			OnAlert.Broadcast(FString::Printf(
				TEXT("THE CREW HAS LANDED — %d colonists disembark into the vault, with %.0f kg of provisions."),
				Housed, CrewPodFoodKg));
			UE_LOG(LogRedHopeSim, Display, TEXT("=== CREW POD: %d colonists housed, +%.0f kg Food ==="), Housed, CrewPodFoodKg);
		}
		else
		{
			// BM-1: with a rated surface habitat the fix is beds, not a vault
			// (Gate-D placeholder copy).
			OnAlert.Broadcast((bVaultRated && AnyHabNetRated())
				? FString::Printf(TEXT("CREW POD STAYED ABOARD — %d beds free, %d needed; the crew is safe and flies back with the ship. Zone Living Quarters bays in your rated habitat before the next ship."),
					GetFreeHousing(), CrewPodColonists)
				: FString::Printf(TEXT("CREW POD STAYED ABOARD — %d beds free, %d needed; the crew is safe and flies back with the ship. Certify a vault floor before the next ship."),
					GetFreeHousing(), CrewPodColonists));
			UE_LOG(LogRedHopeSim, Display, TEXT("=== CREW POD: stays aboard (%d beds free, %d needed) ==="), GetFreeHousing(), CrewPodColonists);
		}
	}
	else if (ItemName == FName("ColonistTransport"))
	{
		// C2b: the crew pod at scale. The same rule - colonists disembark only into
		// certified housing, and the WHOLE transport stays aboard if the beds are
		// not there - with fifty kilos of provisions for every colonist it carries.
		const double FoodKg = ColonistTransportFoodKgPerColonist * ColonistTransportColonists;
		if (GetFreeHousing() >= ColonistTransportColonists)
		{
			const int32 Housed = Debug_AddColonists(ColonistTransportColonists);
			AddStock(FName("Food"), FoodKg);
			OnAlert.Broadcast(FString::Printf(
				TEXT("A COLONIST TRANSPORT HAS LANDED — %d colonists disembark into the vault, with %.0f kg of provisions."),
				Housed, FoodKg));
			UE_LOG(LogRedHopeSim, Display, TEXT("=== COLONIST TRANSPORT: %d colonists housed, +%.0f kg Food ==="), Housed, FoodKg);
		}
		else
		{
			// Gate-D placeholder copy, in the pod's style.
			OnAlert.Broadcast((bVaultRated && AnyHabNetRated())
				? FString::Printf(TEXT("COLONIST TRANSPORT STAYED ABOARD — %d beds free, %d needed; everyone is safe and flies back with the ship. Zone Living Quarters bays in your rated habitat before the next ship."),
					GetFreeHousing(), ColonistTransportColonists)
				: FString::Printf(TEXT("COLONIST TRANSPORT STAYED ABOARD — %d beds free, %d needed; everyone is safe and flies back with the ship. Certify a vault floor before the next ship."),
					GetFreeHousing(), ColonistTransportColonists));
			UE_LOG(LogRedHopeSim, Display, TEXT("=== COLONIST TRANSPORT: stays aboard (%d beds free, %d needed) ==="), GetFreeHousing(), ColonistTransportColonists);
		}
	}
	UE_LOG(LogRedHopeSim, Display, TEXT("  cargo unloaded: %s"), *ItemName.ToString());
}

double URHSimWorldSubsystem::EventCycleSpan() const
{
	// The repeat period once the cycle knob is on: never shorter than the
	// authored schedule itself (overlapping replays would double-book the sky).
	if (WeatherCycleSols <= 0.0 || !Defs)
	{
		return 0.0;
	}
	double FirstStart = TNumericLimits<double>::Max(), LastEnd = 0.0;
	Defs->ForEachEvent([&](FName, const FRHEventRow& Row)
	{
		FirstStart = FMath::Min(FirstStart, (double)Row.StartSol);
		LastEnd = FMath::Max(LastEnd, (double)Row.StartSol + Row.DurationSols);
	});
	if (LastEnd <= 0.0)
	{
		return 0.0; // no authored events - nothing to repeat
	}
	return FMath::Max(WeatherCycleSols, LastEnd - FirstStart);
}

double URHSimWorldSubsystem::FoldEventSol(double SolNow) const
{
	// Long-game weather (v25, config WeatherCycleSols, default 0 = off): past
	// the authored schedule the pattern repeats on a fixed period. Purely a
	// lookup transform - deterministic, identical in both bands, and inert
	// while the knob is 0 (every existing pin reads the unfolded sol).
	const double Span = EventCycleSpan();
	if (Span <= 0.0)
	{
		return SolNow;
	}
	double FirstStart = TNumericLimits<double>::Max(), LastEnd = 0.0;
	Defs->ForEachEvent([&](FName, const FRHEventRow& Row)
	{
		FirstStart = FMath::Min(FirstStart, (double)Row.StartSol);
		LastEnd = FMath::Max(LastEnd, (double)Row.StartSol + Row.DurationSols);
	});
	if (SolNow < LastEnd)
	{
		return SolNow; // the authored window plays exactly as authored
	}
	return FirstStart + FMath::Fmod(SolNow - FirstStart, Span);
}

const FRHEventRow* URHSimWorldSubsystem::GetActiveEvent() const
{
	if (!Defs || !Clock)
	{
		return nullptr;
	}
	// Sol-fraction precision: an event starting at sol 12 begins at 12.0 exactly.
	// The folded sol makes the authored pattern repeat when the cycle knob is
	// on (v25); with the knob off it IS the plain sol.
	const double SolNow = FoldEventSol(Clock->GetSimSecondsTotal() / URHSimClockSubsystem::SolLengthSimSeconds);
	const FRHEventRow* Active = nullptr;
	Defs->ForEachEvent([&](FName, const FRHEventRow& Row)
	{
		if (!Active && SolNow >= (double)Row.StartSol && SolNow < (double)Row.StartSol + Row.DurationSols)
		{
			Active = &Row;
		}
	});
	return Active;
}

double URHSimWorldSubsystem::GetDustFactorNow() const
{
	const FRHEventRow* Event = GetActiveEvent();
	if (Event && Event->Type == FName("DustStorm"))
	{
		return Event->Severity;
	}
	return Defs ? Defs->GetConfigScalar(FName("DustFactor"), 1.0) : 1.0;
}

float URHSimWorldSubsystem::GetRadiationAtLevel(int32 Level) const
{
	// Level 0 = surface (index 1.0). Subsurface floors are negative; each floor
	// of overburden multiplies exposure by RadiationPerLevelMul (~0.05 = a
	// couple metres of regolith is excellent shielding). Above-surface stays
	// at the surface index.
	const int32 Depth = FMath::Max(0, -Level);
	return RadiationSurface * FMath::Pow(RadiationPerLevelMul, (float)Depth);
}

float URHSimWorldSubsystem::GetRadiationNow(int32 Level) const
{
	float Rad = GetRadiationAtLevel(Level);
	// A solar flare floods the surface; overburden still shields, so only the
	// sky-exposed floors (Level >= 0) take the multiplier.
	if (Level >= 0)
	{
		if (const FRHEventRow* Event = GetActiveEvent())
		{
			if (Event->Type == FName("SolarFlare"))
			{
				Rad *= Event->Severity;
			}
		}
	}
	return Rad;
}

bool URHSimWorldSubsystem::SetManualPower(int32 BuildingId, bool bOn)
{
	FRHBuildingInstance* B = FindBuilding(BuildingId);
	if (!B || B->bUnderConstruction)
	{
		return false;
	}
	B->bManualOff = !bOn;
	UE_LOG(LogRedHopeSim, Display, TEXT("%s #%d switched %s"),
		*B->DefName.ToString(), B->Id, bOn ? TEXT("ON") : TEXT("OFF (manual - zero draw, batches frozen)"));
	return true;
}

bool URHSimWorldSubsystem::IsManualOff(int32 BuildingId) const
{
	for (const FRHBuildingInstance& B : Buildings)
	{
		if (B.Id == BuildingId)
		{
			return B.bManualOff;
		}
	}
	return false;
}

// --- D5a: the day shift --------------------------------------------------

bool URHSimWorldSubsystem::CanDayShift(FName DefName) const
{
	const FRHBuildingRow* Def = Defs ? Defs->GetBuilding(DefName) : nullptr;
	if (!Def || !(Def->PowerDraw_W > Def->PowerIdle_W))
	{
		return false; // nothing above idle to rest
	}
	// Work to pause: a recipe (Forge, Electrolyzer, Water Plant, Ice Drill) or the
	// Borer's designation work. Life support, rooms, pads and storage never qualify.
	return Def->CanBore || Defs->FindRunnableRecipe(DefName, [](const TMap<FName, double>&) { return true; }) != nullptr;
}

bool URHSimWorldSubsystem::IsDayShiftDefault(FName DefName) const
{
	return bDayShiftDefaultsOn
		&& Defs->GetConfigScalar(FName(*FString::Printf(TEXT("DayShiftDefault_%s"), *DefName.ToString())), 0.0) >= 0.5;
}

bool URHSimWorldSubsystem::SetDayShift(int32 BuildingId, bool bOn)
{
	FRHBuildingInstance* B = FindBuilding(BuildingId);
	if (!B || B->bUnderConstruction || !CanDayShift(B->DefName))
	{
		return false;
	}
	B->bDayShift = bOn;
	UE_LOG(LogRedHopeSim, Display, TEXT("%s #%d day shift %s"),
		*B->DefName.ToString(), B->Id, bOn ? TEXT("ON (rests at night)") : TEXT("OFF (works through the night)"));
	return true;
}

bool URHSimWorldSubsystem::IsDayShift(int32 BuildingId) const
{
	for (const FRHBuildingInstance& B : Buildings)
	{
		if (B.Id == BuildingId)
		{
			return B.bDayShift;
		}
	}
	return false;
}

bool URHSimWorldSubsystem::IsDayShiftResting(int32 BuildingId) const
{
	for (const FRHBuildingInstance& B : Buildings)
	{
		if (B.Id == BuildingId)
		{
			return B.bDayShift && !B.bManualOff && !B.bUnderConstruction && !IsDaylightNow();
		}
	}
	return false;
}

int32 URHSimWorldSubsystem::GetDayShiftRestingCount() const
{
	if (IsDaylightNow())
	{
		return 0;
	}
	int32 N = 0;
	for (const FRHBuildingInstance& B : Buildings)
	{
		N += (B.bDayShift && !B.bManualOff && !B.bUnderConstruction) ? 1 : 0;
	}
	return N;
}

// Sol-periodic daylight seconds from sim time 0 to T. Era parity: a step's
// daylight share is (D(T1) - D(T0)) / (T1 - T0), so work and draw are linear in
// dt and exact at any step size or alignment. Aligned values (300/900/60) are
// exact in double.
double URHSimWorldSubsystem::DaylightSecondsUpTo(double T) const
{
	const double L = URHSimClockSubsystem::SolLengthSimSeconds;
	const double S = DayShiftStartFrac * L;
	const double E = DayShiftEndFrac * L;
	const double Sols = FMath::FloorToDouble(T / L);
	return Sols * (E - S) + FMath::Clamp(T - Sols * L - S, 0.0, E - S);
}

double URHSimWorldSubsystem::GetDaylightFraction(double T0, double T1) const
{
	if (T1 - T0 <= KINDA_SMALL_NUMBER)
	{
		const double L = URHSimClockSubsystem::SolLengthSimSeconds;
		const double F = (T1 - FMath::FloorToDouble(T1 / L) * L) / L;
		return (F >= DayShiftStartFrac && F < DayShiftEndFrac) ? 1.0 : 0.0;
	}
	return FMath::Clamp((DaylightSecondsUpTo(T1) - DaylightSecondsUpTo(T0)) / (T1 - T0), 0.0, 1.0);
}

double URHSimWorldSubsystem::GetStepEndSeconds() const
{
	return StepEndOverride >= 0.0 ? StepEndOverride : (Clock ? Clock->GetSimSecondsTotal() : 0.0);
}

bool URHSimWorldSubsystem::IsDaylightAt(double T) const
{
	return GetDaylightFraction(T, T) > 0.5;
}

// Night k runs from sunset on sol k-1 to sunrise on sol k (the key also holds
// through that morning's daylight, so callers pair it with IsDaylightAt).
int32 URHSimWorldSubsystem::NightIndexAt(double T) const
{
	const double L = URHSimClockSubsystem::SolLengthSimSeconds;
	return FMath::FloorToInt((T + (1.0 - DayShiftEndFrac) * L) / L);
}

bool URHSimWorldSubsystem::IsDaylightNow() const
{
	return IsDaylightAt(Clock ? Clock->GetSimSecondsTotal() : 0.0);
}

int32 URHSimWorldSubsystem::GetNightIndexNow() const
{
	return NightIndexAt(Clock ? Clock->GetSimSecondsTotal() : 0.0);
}

void URHSimWorldSubsystem::SetFleetHold(bool bHold)
{
	if (bFleetHold == bHold)
	{
		return;
	}
	bFleetHold = bHold;
	UE_LOG(LogRedHopeSim, Display, TEXT("Fleet %s"), bHold
		? TEXT("HELD - robots finish current tasks, then claim nothing new")
		: TEXT("released - task claims resume"));
}

TMap<FName, double> URHSimWorldSubsystem::ComputeConstructionShortage() const
{
	TMap<FName, double> NeedKg, DeliveredKg;
	for (const FRHBuildingInstance& Site : Buildings)
	{
		if (!Site.bUnderConstruction)
		{
			continue;
		}
		const FRHBuildingRow* Def = Defs->GetBuilding(Site.DefName);
		if (!Def)
		{
			continue;
		}
		for (const auto& Cost : GetSiteBill(Site))
		{
			const double* D = Site.InputKg.Find(Cost.Key);
			const double Delivered = D ? *D : 0.0;
			if (Cost.Value > Delivered)
			{
				NeedKg.FindOrAdd(Cost.Key) += Cost.Value - Delivered;
			}
			DeliveredKg.FindOrAdd(Cost.Key) += Delivered;
		}
	}
	TMap<FName, double> Short;
	for (const auto& P : NeedKg)
	{
		// On hand everywhere EXCEPT what already sits delivered at the sites
		// (GetTotalSolid counts site hoppers; the need already excludes them).
		const double* Delivered = DeliveredKg.Find(P.Key);
		const double OnHand = GetTotalSolid(P.Key) + GetStock(P.Key) - (Delivered ? *Delivered : 0.0);
		if (P.Value > OnHand)
		{
			Short.Add(P.Key, P.Value - OnHand);
		}
	}
	// BM-1b: a short output's recipe inputs that the colony holds NONE of are
	// short too - a prefab's segments need Shielding before the Forge can
	// press them, and nothing else would ask for Shielding. Zero-held inputs
	// only (two levels deep), so a chain the colony already stocks keeps the
	// Forge's legacy choices.
	TArray<FName> Frontier;
	Short.GetKeys(Frontier);
	for (int32 Depth = 0; Depth < 2 && Frontier.Num() > 0; ++Depth)
	{
		TArray<FName> Next;
		for (const FName Res : Frontier)
		{
			const FRHRecipeRow* Rec = nullptr;
			for (const FRHBuildingInstance& B : Buildings)
			{
				Rec = (!B.bUnderConstruction && Defs) ? Defs->FindRecipeByOutput(B.DefName, Res) : nullptr;
				if (Rec)
				{
					break;
				}
			}
			const double PerBatch = Rec ? URHDefinitionsSubsystem::ParseResourceList(Rec->Outputs).FindRef(Res) : 0.0;
			if (PerBatch <= 0.0)
			{
				continue;
			}
			const double Batches = FMath::CeilToDouble(Short[Res] / PerBatch);
			for (const auto& In : URHDefinitionsSubsystem::ParseResourceList(Rec->Inputs))
			{
				if (!Short.Contains(In.Key) && GetTotalSolid(In.Key) + GetStock(In.Key) <= KINDA_SMALL_NUMBER)
				{
					Short.Add(In.Key, Batches * In.Value);
					Next.Add(In.Key);
				}
			}
		}
		Frontier = MoveTemp(Next);
	}
	return Short;
}

bool URHSimWorldSubsystem::HasProducerFor(FName Resource) const
{
	for (const FRHBuildingInstance& B : Buildings)
	{
		if (!B.bUnderConstruction && Defs && Defs->FindRecipeByOutput(B.DefName, Resource))
		{
			return true;
		}
	}
	return false;
}

void URHSimWorldSubsystem::Debug_AddSolid(FName DefName, FName Resource, double Kg)
{
	for (FRHBuildingInstance& B : Buildings)
	{
		if (B.DefName == DefName && !B.bUnderConstruction)
		{
			B.InputKg.FindOrAdd(Resource) += Kg;
			OnStockChanged.Broadcast(Resource, GetTotalSolid(Resource));
			return;
		}
	}
	UE_LOG(LogRedHopeSim, Warning, TEXT("Debug_AddSolid: no completed '%s'"), *DefName.ToString());
}

FVector URHSimWorldSubsystem::GetApproachPoint(const FRHSiteRef& Site, int32 RobotLevel) const
{
	const int32 SiteLevel = GetSiteLevel(Site);
	if (SiteLevel != RobotLevel && AreLevelsLinked(SiteLevel, RobotLevel) && ShaftDepth > 0)
	{
		FVector Head = ShaftHeadCm;
		Head.Z = RobotLevel * FloorHeightCm;
		return Head;
	}
	return GetSiteLocation(Site);
}

bool URHSimWorldSubsystem::IsFloorCirculated(int32 Level) const
{
	for (const FRHBuildingInstance& B : Buildings)
	{
		if (B.Level == Level && !B.bUnderConstruction && B.bPowered)
		{
			const FRHBuildingRow* Def = Defs ? Defs->GetBuilding(B.DefName) : nullptr;
			if (Def && Def->CirculatesAir)
			{
				return true;
			}
		}
	}
	return false;
}

void URHSimWorldSubsystem::StepHabitability(float SubDt)
{
	static const FName NOxygen(TEXT("Oxygen"));
	for (const FRHFloor& Floor : FloorTable) // D1: the floors that exist, shallowest first (the old -1..-MaxDepth walk minus the empty ones)
	{
		const int32 Level = Floor.Level;
		ensure(Level >= -MaxDepth);
		const int32 Cells = Floor.Cells.Num();
		if (Cells == 0 || !IsLevelConnected(Level))
		{
			continue;
		}
		const double RequiredKg = Cells * O2FillKgPerCell;
		double& FillKg = FloorO2Kg.FindOrAdd(Level);

		// Pressurized volume leaks - always. The standing tax of living in
		// what you dug; abandonment drains loudly instead of holding forever.
		const double LeakedKg = FMath::Min(FillKg, Cells * O2LeakKgPerCellPerSol * (SubDt / (double)URHSimClockSubsystem::SolLengthSimSeconds));
		FillKg -= LeakedKg;

		// The trunk pushes O2 down; a powered circulator on the floor is what
		// actually moves air (no circulator, no fill - the chain's last link).
		double TakenKg = 0.0;
		if (IsFloorCirculated(Level))
		{
			const double WantKg = FMath::Min(O2FillRateKgPerHour * (SubDt / 50.0), RequiredKg - FillKg);
			if (WantKg > 0.0)
			{
				TakenKg = FMath::Min(WantKg, GetStock(NOxygen));
				if (TakenKg > 0.0)
				{
					AddStock(NOxygen, -TakenKg);
					FillKg += TakenKg;
				}
			}
		}

		// Rating edges announce banner-weight: rated at full fill + circulation,
		// lost below 98% (hysteresis keeps a healthy equilibrium from flapping).
		const bool bWasRated = RatedFloors.Contains(Level);
		const bool bCirculated = IsFloorCirculated(Level);
		const bool bFull = RequiredKg > 0.0 && FillKg >= RequiredKg - KINDA_SMALL_NUMBER;
		// Director ruling (2026-07-07f): a single sealed room is not a vault -
		// a habitat must reach the cell minimum before it can certify Livable.
		const bool bBigEnough = Cells >= MinLivableCells;

		if (!bWasRated && bCirculated && bFull && bBigEnough)
		{
			RatedFloors.Add(Level);
			FloorsNotedSmall.Remove(Level);
			OnAlert.Broadcast(FString::Printf(
				TEXT("FLOOR %d RATED LIVABLE — %d cells pressurized and circulated, shielded under %d floor(s) of overburden."),
				Level, Cells, -Level));
			UE_LOG(LogRedHopeSim, Display, TEXT("=== FLOOR %d RATED LIVABLE (%d cells, %.0f kg O2) ==="), Level, Cells, FillKg);
			// The Phase 1 exit (M1-d Gate C): the colony's FIRST livable space.
			// Fires once per colony; the deck readout carries it permanently.
			if (!bVaultRated)
			{
				bVaultRated = true;
				OnAlert.Broadcast(TEXT("PHASE 1 EXIT: THE VAULT — the colony's first livable space is ready for a crew. The Program can send humans."));
				OnMilestone.Broadcast(TEXT("PHASE 1 EXIT: THE VAULT — the colony's first livable space is ready for a crew."));
				UE_LOG(LogRedHopeSim, Display, TEXT("=== PHASE 1 EXIT: THE VAULT — floor %d rated for the first crew ==="), Level);
			}
		}
		// The rating drops only when the atmosphere is genuinely FAILING:
		// circulation down, or under-required while net-declining (leak
		// outpacing intake - a dry pool). Growing the requirement by carving
		// new cells while a healthy circulator is filling is EXPANSION, not a
		// crisis - the demo run flapped RATED/LOST four times during a normal
		// dig-out and spammed banner alerts (director watch-through finding).
		else if (bWasRated && (!bCirculated || (FillKg < RequiredKg * 0.98 && TakenKg < LeakedKg)))
		{
			RatedFloors.Remove(Level);
			OnAlert.Broadcast(FString::Printf(
				TEXT("FLOOR %d LOST ITS HABITABILITY RATING — %s. Restore oxygen supply."),
				Level, bCirculated ? TEXT("oxygen fill below requirement") : TEXT("air circulation down")));
			UE_LOG(LogRedHopeSim, Display, TEXT("=== FLOOR %d LOST HABITABILITY (%.0f / %.0f kg O2) ==="), Level, FillKg, RequiredKg);
		}

		// Sealed but too small: the atmosphere chain is complete for the floor's
		// current size, yet it is under the cell minimum. This is the exact
		// moment the player expects the exit banner - so tell them WHY it is
		// withheld, once per episode (edge-triggered; re-fires if they fill a
		// larger-but-still-small floor). Held quiet while a carve is still
		// queued for the floor: mid-dig-out is not the confusing case, a
		// finished-but-undersized floor is. Prevents "nothing happened".
		const bool bSealedSmall = !bWasRated && bCirculated && bFull && !bBigEnough && GetCarveQueued(Level) == 0;
		if (bSealedSmall && !FloorsNotedSmall.Contains(Level))
		{
			FloorsNotedSmall.Add(Level);
			OnAlert.Broadcast(FString::Printf(
				TEXT("FLOOR %d SEALED — %d of %d cells. Carve %d more to certify it a livable habitat."),
				Level, Cells, MinLivableCells, MinLivableCells - Cells));
			UE_LOG(LogRedHopeSim, Display, TEXT("=== FLOOR %d SEALED but under habitat minimum (%d/%d cells) ==="), Level, Cells, MinLivableCells);
		}
		else if (!bSealedSmall && !bWasRated && FloorsNotedSmall.Contains(Level))
		{
			FloorsNotedSmall.Remove(Level); // fill/size changed - re-arm the note
		}
	}
	StepHabNetworks(SubDt); // BM-1: after every floor, so floors keep first claim on the O2 pool
}

void URHSimWorldSubsystem::StepHabNetworks(float SubDt)
{
	// Surface habitat networks (BM-1): the floor chain's twin, per network of
	// pressurized shells that share a bay edge. Each member holds its own O2;
	// one push budget per network fills members in ascending Id; the rating
	// edges mirror the floor's (full + circulated + big enough; lost when
	// circulation fails or the fill is net-declining under 98%). No hab = no
	// work at all (inert for every colony without one).
	if (HabNets.Num() == 0)
	{
		return;
	}
	static const FName NOxygen(TEXT("Oxygen"));
	const double LeakPerBay = O2LeakKgPerCellPerSol * (SubDt / (double)URHSimClockSubsystem::SolLengthSimSeconds);
	const bool bFitted = WallFeatures.Num() > 0;
	for (const FRHHabNetCache& N : HabNets)
	{
		double SumReq = 0.0, SumLeak = 0.0, Taken = 0.0;
		// BM-6: a door in every doorway seals the network into compartments
		// (SealedBay rate); windows leak on top. No fittings = the legacy rate.
		const double LeakMul = (bFitted && IsNetSealed(N)) ? SealedLeakMul : 1.0;
		for (const int32 Id : N.Members)
		{
			const FRHHabMemberCache& M = HabMembers.FindChecked(Id);
			const int32 Bays = M.NX * M.NY;
			double& Fill = HabO2Kg.FindOrAdd(Id);
			double LeakHere = Bays * LeakPerBay;
			if (bFitted)
			{
				LeakHere = LeakHere * LeakMul + GetMemberWindowLeakPerSol(Id) * (SubDt / (double)URHSimClockSubsystem::SolLengthSimSeconds);
			}
			const double Leaked = FMath::Min(Fill, LeakHere);
			Fill -= Leaked;
			SumLeak += Leaked;
			SumReq += Bays * O2FillKgPerCell;
		}
		const bool bCirc = IsNetCirculated(N);
		if (bCirc)
		{
			double Budget = O2FillRateKgPerHour * (SubDt / 50.0);
			for (const int32 Id : N.Members)
			{
				const FRHHabMemberCache& M = HabMembers.FindChecked(Id);
				double& Fill = HabO2Kg.FindOrAdd(Id);
				const double Want = FMath::Min(Budget, M.NX * M.NY * O2FillKgPerCell - Fill);
				if (Want > 0.0)
				{
					const double Take = FMath::Min(Want, GetStock(NOxygen));
					if (Take > 0.0)
					{
						AddStock(NOxygen, -Take);
						Fill += Take;
						Budget -= Take;
						Taken += Take;
					}
				}
			}
		}
		double SumFill = 0.0;
		bool bFull = SumReq > 0.0;
		for (const int32 Id : N.Members)
		{
			const FRHHabMemberCache& M = HabMembers.FindChecked(Id);
			const double Fill = HabO2Kg.FindRef(Id);
			SumFill += Fill;
			if (Fill < M.NX * M.NY * O2FillKgPerCell - KINDA_SMALL_NUMBER)
			{
				bFull = false;
			}
		}
		const bool bWas = IsNetRated(N);
		const bool bBig = N.Bays >= MinLivableCells;
		if (!bWas && bCirc && bFull && bBig)
		{
			int32 Beds = 0;
			for (const int32 Id : N.Members)
			{
				RatedHabs.Add(Id);
				Beds += GetHabBeds(Id);
			}
			HabNetsNotedSmall.Remove(N.Root);
			// Gate-D placeholder copy. Never the Phase 1 vault milestone: that
			// stays the underground's first livable floor.
			OnAlert.Broadcast(FString::Printf(
				TEXT("SURFACE HABITAT RATED LIVABLE — %d bays pressurized and circulated under a shielded hull; %d beds.%s%s"),
				N.Bays, Beds, Beds == 0 ? TEXT(" Zone a bay Living Quarters to add beds.") : TEXT(""),
				bVaultRated ? TEXT("") : TEXT(" Crew can move in once the underground vault is certified.")));
			UE_LOG(LogRedHopeSim, Display, TEXT("=== HAB NETWORK #%d RATED LIVABLE (%d bays, %d beds, %.0f kg O2) ==="), N.Root, N.Bays, Beds, SumFill);
		}
		else if (bWas && (!bCirc || !bBig || (SumFill < SumReq * 0.98 && Taken < SumLeak)))
		{
			for (const int32 Id : N.Members)
			{
				RatedHabs.Remove(Id);
			}
			// !bBig: a demolish split the network into a piece under the minimum.
			OnAlert.Broadcast(FString::Printf(
				TEXT("SURFACE HABITAT LOST ITS HABITABILITY RATING — %s. %s"),
				!bBig ? TEXT("below the habitat minimum") : bCirc ? TEXT("oxygen fill below requirement") : TEXT("air circulation down"),
				!bBig ? TEXT("Rejoin or extend it to restore the rating.") : TEXT("Restore oxygen supply.")));
			UE_LOG(LogRedHopeSim, Display, TEXT("=== HAB NETWORK #%d LOST HABITABILITY (%.0f / %.0f kg O2) ==="), N.Root, SumFill, SumReq);
		}
		else if (bWas)
		{
			// A merge or an added module is expansion, not a crisis (the floor
			// rule for new carves): every member carries the rating.
			for (const int32 Id : N.Members)
			{
				RatedHabs.Add(Id);
			}
		}
		const bool bSealedSmall = !bWas && bCirc && bFull && !bBig;
		if (bSealedSmall && !HabNetsNotedSmall.Contains(N.Root))
		{
			HabNetsNotedSmall.Add(N.Root);
			OnAlert.Broadcast(FString::Printf(
				TEXT("SURFACE HABITAT SEALED — %d of %d bays. Add %d more to certify it a livable habitat."),
				N.Bays, MinLivableCells, MinLivableCells - N.Bays));
			UE_LOG(LogRedHopeSim, Display, TEXT("=== HAB NETWORK #%d SEALED but under habitat minimum (%d/%d bays) ==="), N.Root, N.Bays, MinLivableCells);
		}
		else if (!bSealedSmall && !bWas)
		{
			HabNetsNotedSmall.Remove(N.Root);
		}
	}
}

const FRHBuildingInstance* URHSimWorldSubsystem::FindBuildingById(int32 Id) const
{
	for (const FRHBuildingInstance& B : Buildings)
	{
		if (B.Id == Id)
		{
			return &B;
		}
	}
	return nullptr;
}

int32 URHSimWorldSubsystem::GetMaxBedsPerSide(const FRHBuildingRow* Def) const
{
	return (Def && Def->MaxBedsPerSide > 0) ? Def->MaxBedsPerSide : DomeMaxBedsPerSide;
}

FIntPoint URHSimWorldSubsystem::GetBayGridFor(const FRHBuildingRow* Def, int32 SizeX, int32 SizeY)
{
	if (!Def || !Def->Pressurized)
	{
		return FIntPoint(0, 0);
	}
	const int32 FX = FMath::Max(1, Def->FootprintX), FY = FMath::Max(1, Def->FootprintY);
	if (FX % 5 != 0 || FY % 5 != 0)
	{
		return FIntPoint(0, 0); // not a whole number of 10 m bays (data error)
	}
	return FIntPoint((FX / 5) * FMath::Max(1, SizeX), (FY / 5) * FMath::Max(1, SizeY));
}

FIntPoint URHSimWorldSubsystem::GetBayGrid(const FRHBuildingInstance& B) const
{
	return Defs ? GetBayGridFor(Defs->GetBuilding(B.DefName), B.SizeX, B.SizeY) : FIntPoint(0, 0);
}

bool URHSimWorldSubsystem::IsOnBayGrid(const FRHBuildingRow* Def, const FVector& LocationCm, int32 SizeX, int32 SizeY)
{
	// Bay centres sit on multiples of 10 m: a shell of N bays along an axis is
	// centred (N-1) x 5 m off that lattice.
	const FIntPoint G = GetBayGridFor(Def, SizeX, SizeY);
	if (G.X <= 0)
	{
		return false;
	}
	const auto On = [](double R) { return FMath::Abs(R - 1000.0 * FMath::RoundToDouble(R / 1000.0)) <= 1.0; };
	return On(LocationCm.X - (G.X - 1) * 500.0) && On(LocationCm.Y - (G.Y - 1) * 500.0);
}

FVector URHSimWorldSubsystem::SnapToBayGrid(const FRHBuildingRow* Def, const FVector& LocationCm, int32 SizeX, int32 SizeY)
{
	const FIntPoint G = GetBayGridFor(Def, SizeX, SizeY);
	if (G.X <= 0)
	{
		return LocationCm;
	}
	const double OX = (G.X - 1) * 500.0, OY = (G.Y - 1) * 500.0;
	return FVector(1000.0 * FMath::RoundToDouble((LocationCm.X - OX) / 1000.0) + OX,
		1000.0 * FMath::RoundToDouble((LocationCm.Y - OY) / 1000.0) + OY, LocationCm.Z);
}

FVector URHSimWorldSubsystem::GetBayCentreCm(const FRHBuildingInstance& B, int32 Bay) const
{
	const FIntPoint G = GetBayGrid(B);
	if (G.X <= 0)
	{
		return B.LocationCm;
	}
	const FVector2D Half = GetFootprintHalfCm(Defs->GetBuilding(B.DefName), B.SizeX, B.SizeY);
	const int32 BX = Bay % G.X, BY = Bay / G.X;
	return FVector(B.LocationCm.X - Half.X + (BX + 0.5) * 1000.0, B.LocationCm.Y - Half.Y + (BY + 0.5) * 1000.0, B.LocationCm.Z);
}

bool URHSimWorldSubsystem::FindBayAt(const FVector& LocationCm, int32& OutHabId, int32& OutBay) const
{
	if (!Defs)
	{
		return false;
	}
	for (const FRHBuildingInstance& B : Buildings)
	{
		if (B.Level != 0)
		{
			continue;
		}
		const FRHBuildingRow* Def = Defs->GetBuilding(B.DefName);
		const FIntPoint G = GetBayGridFor(Def, B.SizeX, B.SizeY);
		if (G.X <= 0)
		{
			continue;
		}
		const FVector2D Half = GetFootprintHalfCm(Def, B.SizeX, B.SizeY);
		const int32 BX = FMath::FloorToInt32((LocationCm.X - (B.LocationCm.X - Half.X)) / 1000.0);
		const int32 BY = FMath::FloorToInt32((LocationCm.Y - (B.LocationCm.Y - Half.Y)) / 1000.0);
		if (BX >= 0 && BX < G.X && BY >= 0 && BY < G.Y)
		{
			OutHabId = B.Id;
			OutBay = BY * G.X + BX;
			return true;
		}
	}
	return false;
}

bool URHSimWorldSubsystem::IsWhollyInBay(const FRHBuildingInstance& Hab, const FVector& LocationCm, const FVector2D& HalfCm, int32& OutBay) const
{
	const FRHBuildingRow* Def = Defs ? Defs->GetBuilding(Hab.DefName) : nullptr;
	const FIntPoint G = GetBayGridFor(Def, Hab.SizeX, Hab.SizeY);
	if (G.X <= 0)
	{
		return false;
	}
	const FVector2D Half = GetFootprintHalfCm(Def, Hab.SizeX, Hab.SizeY);
	const int32 BX = FMath::FloorToInt32((LocationCm.X - (Hab.LocationCm.X - Half.X)) / 1000.0);
	const int32 BY = FMath::FloorToInt32((LocationCm.Y - (Hab.LocationCm.Y - Half.Y)) / 1000.0);
	if (BX < 0 || BX >= G.X || BY < 0 || BY >= G.Y)
	{
		return false;
	}
	OutBay = BY * G.X + BX;
	const FVector C = GetBayCentreCm(Hab, OutBay);
	return FMath::Abs(LocationCm.X - C.X) + HalfCm.X <= 500.5 && FMath::Abs(LocationCm.Y - C.Y) + HalfCm.Y <= 500.5;
}

FIntPoint URHSimWorldSubsystem::CellPos(const FIntVector& Key) const
{
	// Comparable only within ONE floor or ONE network: underground cells use
	// the floor's lattice around the shaft head; bays use the world bay lattice.
	if (Key.X < 0)
	{
		return GetFloorCell(Key.X, Key.Y);
	}
	const FRHHabMemberCache* M = HabMembers.Find(Key.Z);
	return (M && M->NX > 0) ? M->GridMin + FIntPoint(Key.Y % M->NX, Key.Y / M->NX) : FIntPoint(MIN_int32, MIN_int32);
}

void URHSimWorldSubsystem::RebuildHabTopology()
{
	// Pure function of Buildings, rebuilt at every mutation site (AddBuilding,
	// construction completion, LoadColony). Networks in ascending root (min
	// member Id), members ascending; the lookup maps are never iterated.
	++ZoningVersion; // C1: bay ownership moved
	HabNets.Reset();
	HabMembers.Reset();
	HabBayOwner.Reset();
	if (!Defs)
	{
		return;
	}
	TArray<int32> Ids;
	TMap<FIntPoint, int32>& BayOwner = HabBayOwner;
	for (const FRHBuildingInstance& B : Buildings)
	{
		if (B.Level != 0 || B.bUnderConstruction)
		{
			continue;
		}
		const FRHBuildingRow* Def = Defs->GetBuilding(B.DefName);
		const FIntPoint G = GetBayGridFor(Def, B.SizeX, B.SizeY);
		if (G.X <= 0)
		{
			continue;
		}
		const FVector2D Half = GetFootprintHalfCm(Def, B.SizeX, B.SizeY);
		FRHHabMemberCache M;
		M.Id = B.Id;
		M.NX = G.X;
		M.NY = G.Y;
		M.GridMin = FIntPoint(FMath::RoundToInt32((B.LocationCm.X - Half.X + 500.0) / 1000.0),
			FMath::RoundToInt32((B.LocationCm.Y - Half.Y + 500.0) / 1000.0));
		M.FixedRoom = Def->ProvidesRoom;
		for (int32 BY = 0; BY < G.Y; ++BY)
		{
			for (int32 BX = 0; BX < G.X; ++BX)
			{
				const FIntPoint P = M.GridMin + FIntPoint(BX, BY);
				if (BayOwner.Contains(P))
				{
					UE_LOG(LogRedHopeSim, Warning, TEXT("Habitat #%d overlaps another shell's bay (%d, %d) - only cheat placement can do this"), B.Id, P.X, P.Y);
				}
				else
				{
					BayOwner.Add(P, B.Id);
				}
			}
		}
		HabMembers.Add(B.Id, M);
		Ids.Add(B.Id);
	}
	if (Ids.Num() == 0)
	{
		return;
	}
	Ids.Sort();
	TMap<int32, int32> Parent;
	for (const int32 Id : Ids)
	{
		Parent.Add(Id, Id);
	}
	const auto FindRoot = [&Parent](int32 X)
	{
		while (Parent[X] != X)
		{
			Parent[X] = Parent[Parent[X]];
			X = Parent[X];
		}
		return X;
	};
	for (const int32 Id : Ids)
	{
		const FRHHabMemberCache& M = HabMembers.FindChecked(Id);
		for (int32 BY = 0; BY < M.NY; ++BY)
		{
			for (int32 BX = 0; BX < M.NX; ++BX)
			{
				const FIntPoint P = M.GridMin + FIntPoint(BX, BY);
				for (const FIntPoint D : { FIntPoint(1, 0), FIntPoint(0, 1) })
				{
					if (const int32* Other = BayOwner.Find(P + D))
					{
						const int32 A = FindRoot(Id), B = FindRoot(*Other);
						if (A != B)
						{
							Parent[FMath::Max(A, B)] = FMath::Min(A, B); // root = min Id
						}
					}
				}
			}
		}
	}
	TMap<int32, int32> NetOfRoot;
	for (const int32 Id : Ids) // ascending: a set's root (its min Id) is met first
	{
		const int32 Root = FindRoot(Id);
		int32 Index = INDEX_NONE;
		if (const int32* Found = NetOfRoot.Find(Root))
		{
			Index = *Found;
		}
		else
		{
			FRHHabNetCache Net;
			Net.Root = Root;
			Index = HabNets.Add(Net);
			NetOfRoot.Add(Root, Index);
		}
		FRHHabMemberCache& M = HabMembers.FindChecked(Id);
		M.NetIndex = Index;
		HabNets[Index].Members.Add(Id);
		HabNets[Index].Bays += M.NX * M.NY;
	}
	// Bucket the Interior machines by the network whose bay they stand in
	// (Buildings is Id-ascending, so each bucket is too).
	for (int32 i = 0; i < Buildings.Num(); ++i)
	{
		const FRHBuildingInstance& B = Buildings[i];
		const FRHBuildingRow* Def = B.Level == 0 ? Defs->GetBuilding(B.DefName) : nullptr;
		int32 Hab = 0, Bay = INDEX_NONE;
		if (!Def || !Def->Interior || Def->Pressurized || !FindMemberBayAt(B.LocationCm, Hab, Bay))
		{
			continue;
		}
		FRHHabNetCache& Net = HabNets[HabMembers.FindChecked(Hab).NetIndex];
		if (Def->CirculatesAir && !B.bUnderConstruction)
		{
			Net.Circulators.Add(i);
		}
		if (!Def->ProvidesRoom.IsNone())
		{
			Net.Modules.Add(i);
		}
	}
}

bool URHSimWorldSubsystem::FindMemberBayAt(const FVector& LocationCm, int32& OutHabId, int32& OutBay) const
{
	// Bay centres sit on multiples of 10 m; a bay spans [10k - 5, 10k + 5) m on
	// each axis - the same half-open floor FindBayAt applies.
	const FIntPoint P(FMath::FloorToInt32(LocationCm.X / 1000.0 + 0.5), FMath::FloorToInt32(LocationCm.Y / 1000.0 + 0.5));
	const int32* Hab = HabBayOwner.Find(P);
	const FRHHabMemberCache* M = Hab ? HabMembers.Find(*Hab) : nullptr;
	if (!M)
	{
		return false;
	}
	const FIntPoint L = P - M->GridMin;
	OutHabId = *Hab;
	OutBay = L.Y * M->NX + L.X;
	return true;
}

const URHSimWorldSubsystem::FRHHabNetCache* URHSimWorldSubsystem::FindHabNet(int32 HabId) const
{
	const FRHHabMemberCache* M = HabMembers.Find(HabId);
	return (M && HabNets.IsValidIndex(M->NetIndex)) ? &HabNets[M->NetIndex] : nullptr;
}

bool URHSimWorldSubsystem::IsNetRated(const FRHHabNetCache& N) const
{
	for (const int32 Id : N.Members)
	{
		if (RatedHabs.Contains(Id))
		{
			return true;
		}
	}
	return false;
}

bool URHSimWorldSubsystem::IsNetCirculated(const FRHHabNetCache& N) const
{
	// A powered air circulator standing in any member bay (cached at topology
	// time; power is read live)...
	for (const int32 Index : N.Circulators)
	{
		if (Buildings.IsValidIndex(Index) && Buildings[Index].bPowered)
		{
			return true;
		}
	}
	// ...or a shaft link.
	return IsNetShaftLinked(N);
}

bool URHSimWorldSubsystem::IsNetShaftLinked(const FRHHabNetCache& N) const
{
	// A member bay beside the shaft head while floor -1 is rated (optional;
	// ruling 6a - a hab needs no link to be lived in).
	if (RatedFloors.Contains(-1))
	{
		for (const int32 Id : N.Members)
		{
			const FRHBuildingInstance* B = FindBuildingById(Id);
			const FRHHabMemberCache& M = HabMembers.FindChecked(Id);
			for (int32 Bay = 0; B && Bay < M.NX * M.NY; ++Bay)
			{
				const FVector C = GetBayCentreCm(*B, Bay);
				if (FMath::Max(FMath::Abs(C.X - ShaftHeadCm.X), FMath::Abs(C.Y - ShaftHeadCm.Y)) <= ShaftLinkRangeCm + 0.5)
				{
					return true;
				}
			}
		}
	}
	return false;
}

bool URHSimWorldSubsystem::AnyHabNetRated() const
{
	for (const FRHHabNetCache& N : HabNets)
	{
		if (IsNetRated(N))
		{
			return true;
		}
	}
	return false;
}

bool URHSimWorldSubsystem::IsHabRated(int32 HabId) const
{
	const FRHHabNetCache* N = FindHabNet(HabId);
	return N && IsNetRated(*N);
}

int32 URHSimWorldSubsystem::GetHabNetworkRoot(int32 HabId) const
{
	const FRHHabNetCache* N = FindHabNet(HabId);
	return N ? N->Root : 0;
}

int32 URHSimWorldSubsystem::GetHabNetworkBays(int32 HabId) const
{
	const FRHHabNetCache* N = FindHabNet(HabId);
	return N ? N->Bays : 0;
}

double URHSimWorldSubsystem::GetHabO2Kg(int32 HabId) const
{
	const double* V = HabO2Kg.Find(HabId);
	return V ? *V : 0.0;
}

double URHSimWorldSubsystem::GetHabO2RequiredKg(int32 HabId) const
{
	const FRHHabMemberCache* M = HabMembers.Find(HabId);
	return M ? M->NX * M->NY * O2FillKgPerCell : 0.0;
}

double URHSimWorldSubsystem::GetHabNetworkO2Kg(int32 HabId) const
{
	double Sum = 0.0;
	if (const FRHHabNetCache* N = FindHabNet(HabId))
	{
		for (const int32 Id : N->Members)
		{
			Sum += GetHabO2Kg(Id);
		}
	}
	return Sum;
}

double URHSimWorldSubsystem::GetHabNetworkO2RequiredKg(int32 HabId) const
{
	const FRHHabNetCache* N = FindHabNet(HabId);
	return N ? N->Bays * O2FillKgPerCell : 0.0;
}

bool URHSimWorldSubsystem::IsHabNetworkCirculated(int32 HabId) const
{
	const FRHHabNetCache* N = FindHabNet(HabId);
	return N && IsNetCirculated(*N);
}

bool URHSimWorldSubsystem::IsHabNetworkSealedButSmall(int32 HabId) const
{
	const FRHHabNetCache* N = FindHabNet(HabId);
	if (!N || IsNetRated(*N) || N->Bays >= MinLivableCells || !IsNetCirculated(*N))
	{
		return false;
	}
	for (const int32 Id : N->Members)
	{
		if (GetHabO2Kg(Id) < GetHabO2RequiredKg(Id) - KINDA_SMALL_NUMBER)
		{
			return false;
		}
	}
	return true;
}

bool URHSimWorldSubsystem::IsLocationInRatedHab(const FVector& LocationCm) const
{
	int32 Hab = 0, Bay = INDEX_NONE;
	return FindMemberBayAt(LocationCm, Hab, Bay) && IsHabRated(Hab);
}

int32 URHSimWorldSubsystem::GetHabLivingBays(int32 HabId) const
{
	const FRHHabMemberCache* M = HabMembers.Find(HabId);
	const FRHBuildingInstance* B = M ? nullptr : FindBuildingById(HabId);
	const FIntPoint G = M ? FIntPoint(M->NX, M->NY) : (B ? GetBayGrid(*B) : FIntPoint(0, 0));
	int32 Living = 0;
	for (int32 Bay = 0; Bay < G.X * G.Y; ++Bay)
	{
		const FRHRoomRow* Row = Defs ? Defs->GetRoom(GetBayRoom(HabId, Bay)) : nullptr;
		if (Row && Row->SliceActive && Row->Function == FName("Living"))
		{
			++Living;
		}
	}
	return Living;
}

int32 URHSimWorldSubsystem::GetHabBeds(int32 HabId) const
{
	return CountHabBeds(HabId); // BM-4: furnished Living bays count their beds
}

void URHSimWorldSubsystem::RebuildColonistIndex() const
{
	ColonistIndexById.Reset();
	ColonistIndexById.Reserve(Colonists.Num());
	for (int32 i = 0; i < Colonists.Num(); ++i)
	{
		ColonistIndexById.Add(Colonists[i].Id, i);
	}
	bColonistIndexDirty = false;
}

const FRHColonist* URHSimWorldSubsystem::FindColonist(int32 Id) const
{
	// The Num check is a belt: an evacuation or a load marks the map dirty, but
	// a roster edit that forgot to would still trip it on a size change.
	if (bColonistIndexDirty || ColonistIndexById.Num() != Colonists.Num())
	{
		RebuildColonistIndex();
	}
	const int32* Index = ColonistIndexById.Find(Id);
	return Index ? &Colonists[*Index] : nullptr;
}

int32 URHSimWorldSubsystem::GetHabResidents(int32 HabId) const
{
	int32 N = 0;
	for (const FRHColonist& C : Colonists)
	{
		N += (C.HomeHab == HabId) ? 1 : 0;
	}
	return N;
}

bool URHSimWorldSubsystem::IsFloorSealedButSmall(int32 Level) const
{
	const int32 Cells = GetFloorCarvedCells(Level);
	return Cells > 0 && Cells < MinLivableCells && !IsFloorRated(Level)
		&& IsFloorCirculated(Level)
		&& GetFloorO2Kg(Level) >= GetFloorO2RequiredKg(Level) - KINDA_SMALL_NUMBER;
}

int32 URHSimWorldSubsystem::GetHousingCapacity() const
{
	int32 Beds = 0;
	for (const int32 Level : RatedFloors)
	{
		Beds += CountFloorBeds(Level, nullptr); // BM-4: the legacy rule unless beds are placed
	}
	// BM-1: Living bays of rated surface habitat networks - open to crew only
	// once the Phase-1 vault is certified (director, 2026-10-16: crew still
	// need the vault; the surface habitat is expansion, not a shortcut).
	for (const FRHHabNetCache& N : HabNets)
	{
		if (bVaultRated && IsNetRated(N))
		{
			for (const int32 Id : N.Members)
			{
				Beds += GetHabBeds(Id);
			}
		}
	}
	return Beds;
}

namespace
{
	// Deterministic name pools: colonist N is always the same person (fixed-
	// timestep discipline extends to the crew manifest). Alive pass: 40 first
	// names x 40 surnames from many cultures, walked with coprime strides so
	// the first 1600 colonists all get distinct full names - a crew that looks
	// like Earth sent it.
	const TCHAR* GRHFirstNames[] = {
		TEXT("Amara"), TEXT("Bao"), TEXT("Carmen"), TEXT("Dmitri"),
		TEXT("Efe"), TEXT("Freya"), TEXT("Gabriel"), TEXT("Hana"),
		TEXT("Idris"), TEXT("Jaya"), TEXT("Kenji"), TEXT("Leila"),
		TEXT("Mateo"), TEXT("Nadia"), TEXT("Omar"), TEXT("Priya"),
		TEXT("Quentin"), TEXT("Rosa"), TEXT("Santiago"), TEXT("Tomoko"),
		TEXT("Umut"), TEXT("Valentina"), TEXT("Wei"), TEXT("Ximena"),
		TEXT("Yusuf"), TEXT("Zola"), TEXT("Anouk"), TEXT("Bjorn"),
		TEXT("Chidi"), TEXT("Daria"), TEXT("Esteban"), TEXT("Fatima"),
		TEXT("Gunnar"), TEXT("Halima"), TEXT("Ines"), TEXT("Jonas"),
		TEXT("Kirra"), TEXT("Lars"), TEXT("Mei"), TEXT("Noor"),
	};
	const TCHAR* GRHCallsigns[] = {
		TEXT("Adeyemi"), TEXT("Brandt"), TEXT("Chen"), TEXT("Duval"),
		TEXT("Eriksen"), TEXT("Farid"), TEXT("Goto"), TEXT("Herrera"),
		TEXT("Ilyina"), TEXT("Joshi"), TEXT("Kowalski"), TEXT("Laurent"),
		TEXT("Mbeki"), TEXT("Novak"), TEXT("Okafor"), TEXT("Petrov"),
		TEXT("Quispe"), TEXT("Rahman"), TEXT("Silva"), TEXT("Tanaka"),
		TEXT("Umarov"), TEXT("Vasquez"), TEXT("Whitehorse"), TEXT("Xu"),
		TEXT("Yilmaz"), TEXT("Zhang"), TEXT("Andersson"), TEXT("Bakker"),
		TEXT("Castellanos"), TEXT("Dlamini"), TEXT("Eze"), TEXT("Fontaine"),
		TEXT("Gustafsson"), TEXT("Haddad"), TEXT("Ivanova"), TEXT("Jansen"),
		TEXT("Kimura"), TEXT("Lindqvist"), TEXT("Morales"), TEXT("Nakamura"),
	};
	// Dispositions (alive pass): derived from the id, never stored - they bias
	// which pastime a colonist reaches for and give the crew visible variety.
	const TCHAR* GRHTraits[] = {
		TEXT("Stargazer"), TEXT("Musician"), TEXT("Cardsharp"), TEXT("Tinkerer"),
		TEXT("Chef"), TEXT("Athlete"), TEXT("Bookworm"), TEXT("Sketcher"),
		TEXT("Gardener"), TEXT("Storyteller"), TEXT("Chessmaster"), TEXT("Dreamer"),
	};
}

const TCHAR* URHSimWorldSubsystem::TraitFor(int32 ColonistId)
{
	// Content-hash pick (stable across runs, like every seeded roll here).
	const uint32 H = HashCombine(FCrc::StrCrc32(TEXT("trait")), (uint32)ColonistId * 2654435761u);
	return GRHTraits[H % UE_ARRAY_COUNT(GRHTraits)];
}

void URHSimWorldSubsystem::Debug_ForceRateFloor(int32 Level)
{
	const int32 Cells = GetFloorCarvedCells(Level);
	if (Cells < MinLivableCells || !IsFloorCirculated(Level))
	{
		UE_LOG(LogRedHopeSim, Warning,
			TEXT("Debug_ForceRateFloor: floor %d not eligible (cells %d/%d, circulated %d)"),
			Level, Cells, MinLivableCells, (int32)IsFloorCirculated(Level));
		return;
	}
	FloorO2Kg.FindOrAdd(Level) = Cells * O2FillKgPerCell; // fill to required
	RatedFloors.Add(Level);
	FloorsNotedSmall.Remove(Level);
	UE_LOG(LogRedHopeSim, Display, TEXT("Debug_ForceRateFloor: floor %d certified (%d cells)"), Level, Cells);
}

void URHSimWorldSubsystem::InitFillState(FRHFillState& State, const TSet<int32>* SkipIds) const
{
	// C1: occupancy and capacity are counted ONCE per batch, then kept current as
	// each colonist moves in (the roster, the beds, the rated floors and the
	// networks cannot change inside the loop). Counting them afresh for every
	// arrival made a crew of K cost K x N. SkipIds leaves some residents out of
	// the count (C2b: the ones about to move home).
	for (const FRHColonist& C : Colonists)
	{
		if (SkipIds && SkipIds->Contains(C.Id))
		{
			continue;
		}
		++State.FloorResidents.FindOrAdd(C.HomeLevel);
		if (C.HomeHab != 0)
		{
			++State.HabResidents.FindOrAdd(C.HomeHab);
		}
	}
	State.NetRatedNow.Init(-1, HabNets.Num());
}

bool URHSimWorldSubsystem::PickFillHome(FRHFillState& State, int32& OutLevel, int32& OutHab) const
{
	// Shallowest certified floor with a free bed (deterministic fill
	// order); then (BM-1) the rated surface networks in root order, their
	// members by Id.
	OutLevel = 0;
	OutHab = 0;
	for (const FRHFloor& Floor : FloorTable) // D1: the floors that exist, shallowest first
	{
		const int32 Level = Floor.Level;
		ensure(Level >= -MaxDepth);
		if (!RatedFloors.Contains(Level))
		{
			continue;
		}
		int32* Beds = State.FloorBeds.Find(Level);
		if (!Beds)
		{
			Beds = &State.FloorBeds.Add(Level, CountFloorBeds(Level, nullptr));
		}
		if (State.FloorResidents.FindRef(Level) < *Beds)
		{
			OutLevel = Level;
			return true;
		}
	}
	for (int32 n = 0; bVaultRated && n < HabNets.Num(); ++n)
	{
		if (State.NetRatedNow[n] < 0)
		{
			State.NetRatedNow[n] = IsNetRated(HabNets[n]) ? 1 : 0;
		}
		if (State.NetRatedNow[n] == 0)
		{
			continue;
		}
		for (const int32 Id : HabNets[n].Members)
		{
			int32* Beds = State.HabBeds.Find(Id);
			if (!Beds)
			{
				Beds = &State.HabBeds.Add(Id, GetHabBeds(Id));
			}
			if (State.HabResidents.FindRef(Id) < *Beds)
			{
				OutHab = Id;
				return true;
			}
		}
	}
	return false;
}

URHSimWorldSubsystem::FRHScaleColonyReport URHSimWorldSubsystem::Debug_BuildScaleColony(int32 ColonistTarget)
{
	// The body of the -scale suite's setup (RHSimCommandlet.cpp), moved here so a
	// running game can build the same colony. Every order and number is unchanged.
	FRHScaleColonyReport Rep;
	constexpr int32 Floors = 5;
	constexpr int32 CellsPerFloor = 210;
	constexpr int32 GardenCells = 10;      // the tail of each floor's spiral
	Rep.Floors = Floors;
	Rep.CellsPerFloor = CellsPerFloor;
	if (!Clock || GetShaftDepth() > 0)
	{
		Rep.bFresh = false;
		Rep.Errors.Add(TEXT("Debug_BuildScaleColony needs a fresh colony (no shaft yet) and a sim clock"));
		return Rep;
	}
	constexpr double EraDt = (double)URHSimClockSubsystem::EraStepSimSeconds;
	constexpr double SolLen = (double)URHSimClockSubsystem::SolLengthSimSeconds;
	const FDelegateHandle RejectH = OnCommandRejected.AddLambda([&Rep](const FRHCommand&, const FString&) { ++Rep.Rejected; });
	const auto EraStepOnce = [&]()
	{
		Debug_AddFreshWater(500.0 * EraDt / SolLen);
		Debug_SetBatteryWh(1.0e9);
		Clock->Debug_AdvanceSimSeconds(EraDt);
		EraStep((float)EraDt);
	};
	FString R;
	int32 LanderId = 0;
	for (const FRHBuildingInstance& B : GetBuildings()) { if (B.DefName == FName("Lander")) { LanderId = B.Id; } }

	// Setup: power, a deep shaft, big floors, an air filter on each.
	Debug_PlaceInstant(FName("SolarArray"), FVector(3500.0, -1000.0, 0.0));
	Debug_PlaceInstant(FName("SolarArray"), FVector(3500.0, -2000.0, 0.0));
	const FName NFood(TEXT("Food")), NWater(TEXT("Water")), NOxygen(TEXT("Oxygen")), NSoil(TEXT("Soil")), NSeeds(TEXT("Seeds")), NStruct(TEXT("Struct"));
	AddStock(NOxygen, 1.0e8);
	AddStock(NWater, 1.0e8);
	AddStock(NFood, 1.0e8);
	AddStock(NSoil, 250.0 * 400.0);
	AddStock(NSeeds, 50.0 * 400.0);
	Debug_AddSolidAt(LanderId, NStruct, 2.0e7, false);
	const FVector Head(3000.0, 3000.0, 0.0);
	ExtendShaft(Floors, Head);
	bool bCarved = true;
	for (int32 L = 1; L <= Floors; ++L) { bCarved &= ExcavateFloor(-L, CellsPerFloor, R); }
	for (int32 L = 1; L <= Floors; ++L) { Debug_PlaceInstant(FName("AirFilter"), Head, -L); }
	for (int32 L = 1; L <= Floors; ++L) { Debug_ForceRateFloor(-L); }
	EraStepOnce();
	Rep.bSetupOk = bCarved && GetShaftDepth() == Floors && IsFloorRated(-1) && IsFloorRated(-Floors);

	// Zoning. Gardens (odor emitters) sit at the tail of each floor's spiral;
	// on floors -1..-4 every cell within reach of one is a non-refuser, so the
	// colony's Hope is not buried under offended pairs. Floor -5 is left to
	// offend: no buffer, two Cooking emitters among the Living cells, and a
	// Hallway every thirty cells - the adjacency code runs both ways, with
	// some pairs partitioned and some not.
	TArray<FIntPoint> LabCells, WorkCells, DiningCells, LivingCells; // (level, cell)
	for (int32 L = 1; L <= Floors; ++L)
	{
		TArray<FIntPoint> Pos;
		for (int32 I = 0; I < CellsPerFloor; ++I) { Pos.Add(GetFloorCell(-L, I)); }
		for (int32 I = 0; I < CellsPerFloor; ++I)
		{
			bool bNearGarden = false;
			for (int32 G = CellsPerFloor - GardenCells; G < CellsPerFloor && L < Floors; ++G)
			{
				bNearGarden |= (FMath::Abs(Pos[I].X - Pos[G].X) + FMath::Abs(Pos[I].Y - Pos[G].Y)) <= 2;
			}
			FName Room;
			if (I >= CellsPerFloor - GardenCells) { Room = FName("Garden"); }
			else if (bNearGarden) { Room = I % 3 == 0 ? FName("Lab") : (I % 3 == 1 ? FName("Workstation") : FName("Hallway")); }
			else if (L == Floors && I % 97 == 5) { Room = FName("Cooking"); }
			else
			{
				switch (I % 10)
				{
				case 7: Room = FName("Workstation"); break;
				case 8: Room = FName("Dining"); break;
				case 9: Room = (I / 10) % 3 == 2 ? FName("Hallway") : ((I / 10) % 3 == 0 ? FName("Lab") : FName("Infirmary")); break;
				default: Room = FName("LivingQuarters"); break;
				}
			}
			if (!DesignateRoom(-L, I, Room, R)) { Rep.Errors.Add(FString::Printf(TEXT("zoning floor %d cell %d failed: %s"), -L, I, *R)); }
			if (Room == FName("LivingQuarters")) { ++Rep.ZonedLiving; LivingCells.Add(FIntPoint(-L, I)); }
			else if (Room == FName("Garden")) { ++Rep.ZonedGarden; ++Rep.ZonedSeat; }
			else if (Room == FName("Workstation")) { ++Rep.ZonedSeat; WorkCells.Add(FIntPoint(-L, I)); }
			else if (Room == FName("Lab")) { ++Rep.ZonedSeat; LabCells.Add(FIntPoint(-L, I)); }
			else { ++Rep.ZonedOther; if (Room == FName("Dining")) { DiningCells.Add(FIntPoint(-L, I)); } }
		}
	}

	// Furnishing, through the same Furnish orders the -furnish suite sends:
	// four bunk beds in every Living cell, four benches in every work and lab
	// cell (each replaces the legacy single seat), a table in each Dining.
	// Batched so one era step lands a few hundred orders.
	int32 Queued = 0;
	const auto FurnishOrder = [&](const TCHAR* Piece, int32 Level, int32 Cell, int32 Slot)
	{
		const FIntVector Key(Level, Cell, 0);
		FRHCommand Order; Order.Verb = FName("Furnish"); Order.Target = FName(Piece); Order.Level = Level;
		Order.Location = GetCellCentreCm(Key);
		Order.Value = EncodeFurnish(Cell, Slot, 0);
		EnqueueCommand(Order);
		if (++Queued >= 400) { EraStepOnce(); Queued = 0; }
	};
	for (const FIntPoint& Cell : LivingCells) { for (const int32 Slot : { 0, 2, 8, 10 }) { FurnishOrder(TEXT("BunkBed"), Cell.X, Cell.Y, Slot); } }
	for (const FIntPoint& Cell : WorkCells) { for (const int32 Slot : { 0, 2, 8, 10 }) { FurnishOrder(TEXT("Workbench"), Cell.X, Cell.Y, Slot); } }
	for (const FIntPoint& Cell : LabCells) { for (const int32 Slot : { 0, 2, 8, 10 }) { FurnishOrder(TEXT("LabBench"), Cell.X, Cell.Y, Slot); } }
	for (const FIntPoint& Cell : DiningCells) { FurnishOrder(TEXT("DiningTable"), Cell.X, Cell.Y, 0); }
	EraStepOnce();
	if (LabCells.Num() > 1 && WorkCells.Num() > 1)
	{
		for (const FIntPoint* Station : { &LabCells[1], &WorkCells[1], &LabCells[LabCells.Num() / 2], &WorkCells[WorkCells.Num() / 2], &LabCells.Last(), &WorkCells.Last() })
		{
			FRHCommand Assign; Assign.Verb = FName("Assign"); Assign.Level = Station->X; Assign.Value = (double)Station->Y;
			EnqueueCommand(Assign);
			++Rep.StationOrders;
		}
	}
	EraStepOnce();
	Rep.Capacity = GetHousingCapacity();
	for (int32 L = 1; L <= Floors; ++L) { for (int32 I = 0; I < CellsPerFloor; ++I) { Rep.SeatsTotal += GetCellSeats(FIntVector(-L, I, 0)); } }
	OnCommandRejected.Remove(RejectH);
	Rep.WorkCells = WorkCells;
	Debug_SetHopeGates(50.0, 50.0); // research and births run in every step
	Debug_SetHopeGrowthIntervalSols(1.0e9); // births scale per head: hold the interval out of reach so the headcount stays exact
	if (ColonistTarget > 0)
	{
		Rep.Housed = Debug_AddColonists(FMath::Max(0, ColonistTarget - GetPopulation()));
	}
	return Rep;
}

int32 URHSimWorldSubsystem::Debug_AddColonists(int32 Count)
{
	return AddColonists(Count, nullptr);
}

int32 URHSimWorldSubsystem::AddColonists(int32 Count, const TArray<FRHRecalledMarsBorn>* Returning)
{
	int32 Housed = 0;
	FRHFillState Fill;
	if (Returning)
	{
		Count = FMath::Min(Count, Returning->Num());
	}
	if (Count > 0)
	{
		InitFillState(Fill);
	}
	for (int32 i = 0; i < Count; ++i)
	{
		int32 Home = 0, HomeHab = 0;
		if (!PickFillHome(Fill, Home, HomeHab))
		{
			break; // no certified bed left
		}
		FRHColonist C;
		if (Returning)
		{
			// F2b: a recalled Mars-born child coming home keeps their own Id and name.
			C.Id = (*Returning)[i].Id;
			C.Name = (*Returning)[i].Name;
			C.bMarsBorn = true;
		}
		else
		{
			C.Id = NextColonistId++;
			// Full name via the Latin-square diagonal walk over the two 40-pools:
			// first = K mod 40, surname = (K + K/40) mod 40 - the first 1600 ids all
			// land on DISTINCT first+surname pairs, with varied surnames from id 1.
			const int32 FirstCount = UE_ARRAY_COUNT(GRHFirstNames);
			const int32 LastCount = UE_ARRAY_COUNT(GRHCallsigns);
			const int32 K = C.Id - 1;
			C.Name = FString::Printf(TEXT("%s %s"), GRHFirstNames[K % FirstCount], GRHCallsigns[(K + K / FirstCount) % LastCount]);
			if (K >= FirstCount * LastCount)
			{
				C.Name += FString::Printf(TEXT(" %d"), K / (FirstCount * LastCount) + 1);
			}
		}
		C.HomeLevel = HomeHab ? 0 : Home;
		C.HomeHab = HomeHab;
		Colonists.Add(C);
		++ColonistSetVersion;
		if (!bColonistIndexDirty)
		{
			ColonistIndexById.Add(C.Id, Colonists.Num() - 1);
		}
		if (HomeHab)
		{
			++Fill.HabResidents.FindOrAdd(HomeHab);
		}
		else
		{
			++Fill.FloorResidents.FindOrAdd(Home);
		}
		bEverHadCrew = true; // M4 Gate D: the collapse ending checks "had a crew, lost it"
		++Housed;
		if (HomeHab)
		{
			UE_LOG(LogRedHopeSim, Display, TEXT("Colonist %s housed in surface habitat #%d"), *C.Name, HomeHab);
		}
		else
		{
			UE_LOG(LogRedHopeSim, Display, TEXT("Colonist %s housed on floor %d"), *C.Name, Home);
		}
	}
	return Housed;
}

bool URHSimWorldSubsystem::AreCropsLive() const
{
	bool bLive = false;
	if (Defs)
	{
		Defs->ForEachCropRow([&bLive](FName, const FRHCropRow& Row) { bLive |= Row.SliceActive; });
	}
	return bLive;
}

FName URHSimWorldSubsystem::GetCellCrop(int32 Level, int32 CellIndex) const
{
	const FRHPlantedCropState* S = PlantedCrops.Find(FIntVector(Level, CellIndex, 0));
	return S ? S->Crop : NAME_None;
}

int32 URHSimWorldSubsystem::GetCellCropStage(int32 Level, int32 CellIndex) const
{
	return GetCropStageForKey(FIntVector(Level, CellIndex, 0));
}

FName URHSimWorldSubsystem::GetDomeBedCrop(int32 DomeId, int32 Bed) const
{
	const FRHPlantedCropState* S = PlantedCrops.Find(FIntVector(0, Bed, DomeId));
	return S ? S->Crop : NAME_None;
}

int32 URHSimWorldSubsystem::GetDomeBedCropStage(int32 DomeId, int32 Bed) const
{
	return GetCropStageForKey(FIntVector(0, Bed, DomeId));
}

int32 URHSimWorldSubsystem::GetCropStageForKey(const FIntVector& Key) const
{
	const FRHPlantedCropState* S = PlantedCrops.Find(Key);
	const FRHCropRow* Row = (S && Defs) ? Defs->GetCrop(S->Crop) : nullptr;
	if (!Row || !Clock || Row->GrowSols <= 0.f)
	{
		return -1;
	}
	// Stage is DERIVED from planting sol (never stored): sprout below half
	// grown, young to maturity, mature after. Presentation reads this; the
	// sim itself only cares about the mature threshold.
	const double Age = Clock->GetSimSecondsTotal() / (double)URHSimClockSubsystem::SolLengthSimSeconds - S->PlantedSol;
	if (Age >= (double)Row->GrowSols) { return 2; }
	return (Age >= 0.5 * (double)Row->GrowSols) ? 1 : 0;
}

double URHSimWorldSubsystem::GetCellSoilKg(int32 Level, int32 CellIndex) const
{
	const FRHPlantedCropState* S = PlantedCrops.Find(FIntVector(Level, CellIndex, 0));
	return S ? S->SoilKg : 0.0;
}

bool URHSimWorldSubsystem::SetFloorClimate(int32 Level, FName Band, FString& OutError)
{
	static const FName NTemperate(TEXT("Temperate")), NHumid(TEXT("Humid")), NArid(TEXT("Arid"));
	if (Band != NTemperate && Band != NHumid && Band != NArid)
	{
		OutError = TEXT("Climate must be Temperate, Humid, or Arid");
		return false;
	}
	if (!FloorRoomCells.Contains(Level))
	{
		OutError = FString::Printf(TEXT("Floor %d has no designated rooms"), Level);
		return false;
	}
	if (Band == NTemperate) { FloorClimate.Remove(Level); }
	else { FloorClimate.Add(Level, Band); }
	UE_LOG(LogRedHopeSim, Display, TEXT("Climate: floor %d set to %s%s"), Level, *Band.ToString(),
		(Band != NTemperate && !HasRegulatorOnline(Level)) ? TEXT(" (INERT until a HumidityRegulator is online on the floor)") : TEXT(""));
	return true;
}

FName URHSimWorldSubsystem::GetFloorClimateSetting(int32 Level) const
{
	static const FName NTemperate(TEXT("Temperate"));
	const FName* Found = FloorClimate.Find(Level);
	return Found ? *Found : NTemperate;
}

FName URHSimWorldSubsystem::GetFloorClimateEffective(int32 Level) const
{
	static const FName NTemperate(TEXT("Temperate"));
	const FName Setting = GetFloorClimateSetting(Level);
	// A non-default climate needs machinery to hold it: no powered regulator
	// on the floor means the room drifts back to Temperate (never a penalty,
	// the setting just waits for its hardware).
	return (Setting != NTemperate && !HasRegulatorOnline(Level)) ? NTemperate : Setting;
}

bool URHSimWorldSubsystem::HasRegulatorOnline(int32 Level) const
{
	static const FName NRegulator(TEXT("HumidityRegulator"));
	for (const FRHBuildingInstance& B : Buildings)
	{
		if (B.DefName == NRegulator && B.Level == Level && !B.bUnderConstruction && B.bPowered)
		{
			return true;
		}
	}
	return false;
}

bool URHSimWorldSubsystem::DesignateDuct(int32 Level, FString& OutError)
{
	if (!FloorRoomCells.Contains(Level))
	{
		OutError = FString::Printf(TEXT("Floor %d has no designated rooms to duct"), Level);
		return false;
	}
	bool bAlready = false;
	DuctedFloors.Add(Level, &bAlready);
	if (!bAlready)
	{
		UE_LOG(LogRedHopeSim, Display, TEXT("Duct: floor %d now breathes with the colony (CO2 in, O2 out)"), Level);
	}
	return true;
}

void URHSimWorldSubsystem::StepAgriculture(float SubDt)
{
	// Zero-garden colonies: FloorRoomCells is empty pre-M2 (and PlantedCells
	// with it), so this is a cheap no-op - every existing baseline unchanged.
	// Agri Gate C: a built greenhouse dome gardens on a colony with no rooms.
	bool bAnyDome = false;
	for (const FRHBuildingInstance& B : Buildings)
	{
		if (GetDomeRoom(B)) { bAnyDome = true; break; }
	}
	if (FloorRoomCells.Num() == 0 && PlantedCells.Num() == 0 && !bAnyDome)
	{
		return;
	}
	static const FName NSoil(TEXT("Soil")), NSeeds(TEXT("Seeds")), NWater(TEXT("Water")), NFood(TEXT("Food")), NGlass(TEXT("Glass"));
	static const FName NGarden(TEXT("Garden")), NGreenhouse(TEXT("Greenhouse"));
	static const FName NCompost(TEXT("Compost")), NFertilizer(TEXT("Fertilizer")), NOxygen(TEXT("Oxygen"));

	// Crop context (greenhouse-agriculture Gates A/B/D), resolved once per
	// step: zero active crop rows = the legacy flat-yield garden, bit-for-bit.
	TArray<TPair<FName, const FRHCropRow*>> ActiveCrops;
	if (Defs) { Defs->GetActiveCropsSorted(ActiveCrops); }
	const bool bCropsLive = ActiveCrops.Num() > 0;
	const double NowSols = Clock ? Clock->GetSimSecondsTotal() / (double)URHSimClockSubsystem::SolLengthSimSeconds : 0.0;

	// Plant: any Garden- OR Greenhouse-zoned cell on a rated floor - and every
	// bed of a built greenhouse dome (Agri Gate C) - when the colony holds the
	// materials. A Greenhouse (cell or dome bed) also needs fabricated/imported
	// Glass. Auto - the gamble pays off the moment the ground is ready. Key is
	// the planted-map key: (Level, Cell, 0) for a cell, (0, Bed, DomeId) for
	// a dome bed; ClimateLevel is the floor whose climate the bed breathes.
	const auto PickCrop = [&](int32 ClimateLevel) -> FName
	{
		// Deterministic crop pick (Gate A): cycle the name-sorted active
		// list, preferring crops matched to the floor's effective climate -
		// per-crop greenhouses emerge naturally.
		TArray<int32> Matching;
		const FName Climate = GetFloorClimateEffective(ClimateLevel);
		for (int32 c = 0; c < ActiveCrops.Num(); ++c)
		{
			if (ActiveCrops[c].Value->ClimateBand == Climate) { Matching.Add(c); }
		}
		const int32 Pick = Matching.Num() > 0
			? Matching[PlantRotation % Matching.Num()]
			: (PlantRotation % ActiveCrops.Num());
		++PlantRotation;
		return ActiveCrops[Pick].Key;
	};
	// The log's name for a bed - built only when a line is actually written.
	const auto BedName = [](const FIntVector& Key)
	{
		return Key.Z != 0 ? FString::Printf(TEXT("dome #%d bed %d"), Key.Z, Key.Y)
			: FString::Printf(TEXT("floor %d cell %d"), Key.X, Key.Y);
	};
	const auto TryPlant = [&](const FIntVector& Key, const FRHRoomRow* Row, int32 ClimateLevel)
	{
		if (PlantedCells.Contains(Key))
		{
			// ADOPTION (director hand-play 2026-07-17): a bed planted before
			// the crop layer went live carries no crop - when the catalogue
			// activates mid-game it adopts one in place (soil already
			// emplaced, no extra cost) and starts growing. With crops dormant
			// this branch never fires - legacy exact.
			if (bCropsLive && !PlantedCrops.Contains(Key))
			{
				FRHPlantedCropState S;
				S.Crop = PickCrop(ClimateLevel);
				S.PlantedSol = NowSols;
				S.SoilKg = GardenSoilKgPerCell;
				PlantedCrops.Add(Key, S);
				UE_LOG(LogRedHopeSim, Display, TEXT("Crop adopted: %s takes %s (bed planted pre-activation)"),
					*BedName(Key), *S.Crop.ToString());
			}
			return;
		}
		const bool bGreenhouse = (Row->Function == NGreenhouse);
		if (GetStock(NSoil) < GardenSoilKgPerCell || GetStock(NSeeds) < GardenSeedsKgPerCell
			|| (bGreenhouse && GetStock(NGlass) < GreenhouseGlassKgPerCell))
		{
			return; // waits for the next pallet/crate, silently - the deck shows the shortfall
		}
		AddStock(NSoil, -GardenSoilKgPerCell);
		AddStock(NSeeds, -GardenSeedsKgPerCell);
		if (bGreenhouse) { AddStock(NGlass, -GreenhouseGlassKgPerCell); }
		PlantedCells.Add(Key);
		bSoilSpentAnnounced = false; // a fresh bed clears the spent-soil edge
		FName PlantedCrop = NAME_None;
		if (bCropsLive)
		{
			FRHPlantedCropState S;
			S.Crop = PickCrop(ClimateLevel);
			S.PlantedSol = NowSols;
			S.SoilKg = GardenSoilKgPerCell;
			PlantedCrops.Add(Key, S);
			PlantedCrop = S.Crop;
		}
		UE_LOG(LogRedHopeSim, Display, TEXT("%s planted: %s (%.0f kg soil, %.0f kg seeds%s)%s"),
			bGreenhouse ? TEXT("Greenhouse") : TEXT("Garden"), *BedName(Key),
			GardenSoilKgPerCell, GardenSeedsKgPerCell,
			bGreenhouse ? *FString::Printf(TEXT(", %.0f kg glass"), GreenhouseGlassKgPerCell) : TEXT(""),
			PlantedCrop.IsNone() ? TEXT("") : *FString::Printf(TEXT(" crop=%s"), *PlantedCrop.ToString()));
		if (!bFirstCropAnnounced)
		{
			bFirstCropAnnounced = true;
			OnAlert.Broadcast(TEXT("THE FIRST CROP IS PLANTED — Earth soil in Martian ground. The colony starts feeding itself."));
			OnMilestone.Broadcast(TEXT("THE FIRST CROP IS PLANTED — Earth soil in Martian ground."));
		}
	};
	for (const auto& Pair : FloorRoomCells)
	{
		const int32 Level = Pair.Key;
		if (!RatedFloors.Contains(Level))
		{
			continue;
		}
		for (int32 i = 0; i < Pair.Value.Num(); ++i)
		{
			const FRHRoomRow* Row = Defs ? Defs->GetRoom(Pair.Value[i]) : nullptr;
			if (!Row || !IsGardenFunction(Row->Function))
			{
				continue;
			}
			TryPlant(FIntVector(Level, i, 0), Row, Level);
		}
	}
	// Dome beds after every zoned cell, domes in Id order, beds in order.
	if (bAnyDome)
	{
		for (int32 b = 0; b < Buildings.Num(); ++b)
		{
			const FRHRoomRow* Row = GetDomeRoom(Buildings[b]);
			if (!Row)
			{
				continue;
			}
			const int32 DomeId = Buildings[b].Id;
			const int32 Beds = GetBedCount(Buildings[b]);
			for (int32 Bed = 0; Bed < Beds; ++Bed)
			{
				TryPlant(FIntVector(0, Bed, DomeId), Row, 0);
			}
		}
	}

	const double DtSols = SubDt / (double)URHSimClockSubsystem::SolLengthSimSeconds;
	const double DtHours = SubDt / 50.0; // sol-hours (matches StepPower's Wh accounting)

	// The crew feeds the farm loop (Gates B/D): compost accrues per colonist-
	// sol (abstract, prevention-framed - the recycling the colony runs anyway)
	// and exhaled CO2 pools for ducted gardens to breathe. Linear per-sol
	// scalars, band-identical; gated on the crop layer so every pre-crop
	// baseline is untouched.
	if (bCropsLive && GetPopulation() > 0)
	{
		AddStock(NCompost, CompostKgPerColonistSol * (double)GetPopulation() * DtSols);
		ColonyCO2Kg += CO2KgPerColonistSol * (double)GetPopulation() * DtSols;
	}

	// Grow-light power (M2 Gate D+): grow-lit GARDEN cells run on the battery,
	// all-or-nothing at colony scale (order-independent, legible - "the gardens
	// went dark"). Debited BEFORE StepPower recomputes the bank, so the drain is
	// consistent in both time bands. Greenhouses draw ~nothing (they glaze).
	int32 GrowLitCount = 0;
	for (const FIntVector& PC : PlantedCells)
	{
		if (PC.Z != 0)
		{
			continue; // a dome bed glazes - no grow lights
		}
		const FRHRoomRow* Row = Defs ? Defs->GetRoom(GetRoomAt(PC.X, PC.Y)) : nullptr;
		if (Row && Row->Function == NGarden && RatedFloors.Contains(PC.X))
		{
			++GrowLitCount;
		}
	}
	GardenPowerDrawW = GardenGrowLightWPerCell * GrowLitCount;
	const double GrowLightWh = GardenPowerDrawW * DtHours;
	bool bGrowLightsPowered = true;
	if (GrowLitCount > 0)
	{
		if (Power.BatteryWh >= GrowLightWh)
		{
			Power.BatteryWh -= GrowLightWh;
		}
		else
		{
			bGrowLightsPowered = false; // bank can't pay - lights off this step
		}
	}
	bGrowLightsDark = (GrowLitCount > 0 && !bGrowLightsPowered);

	// Greenhouse light: the same solar factor the arrays generate on (curve x
	// dust) - dark at night, throttled in a storm. Deterministic, band-shared.
	const double SolarFactor = Defs ? (double)Defs->EvalSolarCurve(Clock->GetSolFraction()) * GetDustFactorNow() : 0.0;

	// Grow: each planted cell on a rated floor turns Water into Food when it has
	// LIGHT (grow-lit = powered; greenhouse = shallow enough + daylight). A cell
	// re-zoned away from any garden function forfeits its soil (loudly, once).
	int32 Producing = 0;
	bool bThirsty = false;
	// Hope drives the harvest: a thriving crew coaxes more food from the same
	// plot (tempo on the YIELD, not the fixed physical Water draw). Exactly 1.0
	// at zero population, so the pure-mechanics -garden regression is untouched.
	const double HumanTempo = GetHumanWorkTempo();
	// C1: the gardeners' mastery is the same for every bed in a step (skills only
	// accrue in StepPopulation), so it is read once, on first use - it was one
	// walk of the whole roster per planted bed.
	bool bGardenSkillKnown = false;
	double GardenSkillMul = 1.0;
	for (auto It = PlantedCells.CreateIterator(); It; ++It)
	{
		const int32 Level = It->X, Cell = It->Y;
		const bool bDomeBed = It->Z != 0;
		const FRHRoomRow* Row = nullptr;
		if (bDomeBed)
		{
			// A dome bed (Agri Gate C): its dome's room, while the dome stands
			// and the bed is inside its size. A demolished dome's beds are
			// cleared by RemoveBuilding, so a missing row is a dormant room -
			// the bed waits, nothing is lost.
			const FRHBuildingInstance* Dome = FindBuilding(It->Z);
			Row = (Dome && Cell < GetBedCount(*Dome)) ? GetDomeRoom(*Dome) : nullptr;
			if (!Row)
			{
				continue;
			}
		}
		else
		{
			Row = Defs ? Defs->GetRoom(GetRoomAt(Level, Cell)) : nullptr;
			if (!Row || !IsGardenFunction(Row->Function))
			{
				OnAlert.Broadcast(FString::Printf(
					TEXT("GARDEN CLEARED: floor %d cell %d was re-zoned — its soil can't be recovered."), Level, Cell));
				PlantedCrops.Remove(*It); // the crop goes with the bed
				It.RemoveCurrent();
				continue;
			}
			if (!RatedFloors.Contains(Level))
			{
				continue; // dormant, not dead: rating loss pauses the crop
			}
		}
		// Light gate: no light => dormant this step (no water spent, crop lives).
		double LightFactor = 1.0;
		if (Row->Function == NGreenhouse)
		{
			if (Level < GreenhouseMinLevel || SolarFactor <= KINDA_SMALL_NUMBER)
			{
				continue; // buried too deep for light, or Martian night
			}
			LightFactor = SolarFactor;
		}
		else if (!bGrowLightsPowered)
		{
			continue; // grow-lit but the bank went dark
		}
		// Crop fork (Gate A): a cell with a crop entry uses that crop's real
		// water draw, grow time, and yield; a legacy cell (crops dormant at
		// plant time, or a pre-v26 save) keeps the flat math exactly.
		FRHPlantedCropState* CropState = PlantedCrops.Find(*It);
		const FRHCropRow* Crop = (CropState && Defs) ? Defs->GetCrop(CropState->Crop) : nullptr;
		const double NeedWater = (Crop ? (double)Crop->WaterKgPerSol : GardenWaterKgPerSolPerCell) * DtSols;
		if (GetStock(NWater) < NeedWater)
		{
			bThirsty = true;
			continue;
		}
		AddStock(NWater, -NeedWater);
		if (Crop && (NowSols - CropState->PlantedSol) < (double)Crop->GrowSols)
		{
			// Immature: the crop drinks but yields nothing yet. Maturity is a
			// threshold on the planting sol - monotone, era-parity-safe.
			continue;
		}
		double CellMul = 1.0;
		if (Crop)
		{
			// Climate preference (Gate B): matched band = full yield.
			CellMul *= (Crop->ClimateBand == GetFloorClimateEffective(Level)) ? 1.0 : ClimateMismatchYieldMul;
			// Duct exchange (Gate D): a ducted garden floor photosynthesizes
			// the crew's CO2 - yield bonus and O2 back out, but only while
			// the pool actually holds the draw (no CO2, no boost, no O2).
			if (DuctedFloors.Contains(Level))
			{
				const double WantCO2 = GardenCO2DrawKgPerSolPerCell * DtSols;
				if (ColonyCO2Kg >= WantCO2)
				{
					ColonyCO2Kg -= WantCO2;
					CellMul *= DuctYieldBonusMul;
					AddStock(NOxygen, GardenO2EmitKgPerSolPerCell * DtSols);
					NoteProduced(NOxygen, GardenO2EmitKgPerSolPerCell * DtSols);
				}
			}
		}
		// Yield rides work tempo, light, and now the gardeners' MASTERY (alive
		// pass): a seasoned garden crew doubles the take.
		if (!bGardenSkillKnown)
		{
			GardenSkillMul = GetCrewSkillMul(NGarden);
			bGardenSkillKnown = true;
		}
		const double YieldKg = (Crop ? (double)Crop->YieldKgPerSol : GardenFoodKgPerSolPerCell)
			* CellMul * HumanTempo * LightFactor * GardenSkillMul * DtSols;
		AddStock(NFood, YieldKg);
		NoteProduced(NFood, YieldKg);
		// Soil depletion (Gate B): mature production spends the bed. Fertilizer
		// refills it in place; an empty colony larder lets the cell revert to
		// unplanted (the plant loop re-emplaces it when stocks allow).
		if (Crop)
		{
			CropState->SoilKg -= CropSoilDepleteKgPerSol * DtSols;
			if (CropState->SoilKg <= 0.0)
			{
				const double FertNeed = GardenSoilKgPerCell / FMath::Max(FertilizerSoilPerKg, (double)KINDA_SMALL_NUMBER);
				if (GetStock(NFertilizer) >= FertNeed)
				{
					AddStock(NFertilizer, -FertNeed);
					CropState->SoilKg = GardenSoilKgPerCell;
					UE_LOG(LogRedHopeSim, Display, TEXT("Fertilizer worked into floor %d cell %d (%.0f kg -> bed restored)"), Level, Cell, FertNeed);
				}
				else
				{
					if (!bSoilSpentAnnounced)
					{
						bSoilSpentAnnounced = true;
						OnAlert.Broadcast(TEXT("A GARDEN BED IS SPENT — the soil gave all it had. Fertilizer or a fresh soil pallet brings it back."));
					}
					UE_LOG(LogRedHopeSim, Display, TEXT("Soil spent: floor %d cell %d reverts to unplanted (crop %s)"), Level, Cell, *CropState->Crop.ToString());
					PlantedCrops.Remove(*It);
					It.RemoveCurrent();
					continue;
				}
			}
		}
		GardenFoodCumKg += YieldKg;
		if (!bFirstHarvestAnnounced && GardenFoodCumKg >= FirstHarvestKg)
		{
			// THE FIRST MARTIAN HARVEST: a once-only milestone. Permanent Hope
			// (folded in GetColonyHope), a whole-crew celebration moment, and
			// the golden-banner channel. Wording = Gate-D review placeholder.
			bFirstHarvestAnnounced = true;
			const FString Line = TEXT("THE FIRST MARTIAN HARVEST — food grown in OUR ground is on the table. The crew is celebrating in the mess.");
			OnMilestone.Broadcast(Line);
			OnAlert.Broadcast(Line);
			OnCrewMoment.Broadcast((uint8)ERHCrewMoment::Celebrate, 0, 0, Line);
			UE_LOG(LogRedHopeSim, Display, TEXT("=== FIRST HARVEST (%.2f kg cumulative, +%.0f Hope milestone) ==="),
				GardenFoodCumKg, HopeFirstHarvestMilestone);
		}
		++Producing;
	}
	ProducingCells = Producing;

	// Grow-light blackout edge: one alert per episode. An episode ends only
	// after the lights have stayed on for a whole sol - a bank scraping empty
	// at night flips them every step, and re-arming on each flip flooded the
	// alert channel (12,524 alerts in 40 sols, RH.Playtest 2026-10-15).
	const double NowSimSeconds = Clock ? Clock->GetSimSecondsTotal() : 0.0;
	if (bGrowLightsDark)
	{
		GardenLastDarkSimSeconds = NowSimSeconds;
		if (!bGardenDarkAnnounced)
		{
			bGardenDarkAnnounced = true;
			OnAlert.Broadcast(TEXT("GROW-LIGHTS DARK — the bank can't power the garden. Crops hold dormant until the grid recovers."));
		}
	}
	else if (NowSimSeconds - GardenLastDarkSimSeconds >= URHSimClockSubsystem::SolLengthSimSeconds)
	{
		bGardenDarkAnnounced = false;
	}

	// Water-starve edge: one alert per episode, cleared when the taps run again.
	if (bThirsty && !bGardenThirstAnnounced)
	{
		bGardenThirstAnnounced = true;
		OnAlert.Broadcast(TEXT("THE GARDEN IS DRY — no Water for the crops. Yield paused until the tanks refill."));
	}
	else if (!bThirsty)
	{
		bGardenThirstAnnounced = false;
	}
}

FString URHSimWorldSubsystem::GetHomeLabel(int32 HomeLevel, int32 HomeHab) const
{
	// Placeholder copy (Gate-D review): the name a rationing alert gives a home. A
	// surface network is named by its root, so two networks never share a name.
	if (HomeHab == 0)
	{
		return FString::Printf(TEXT("Floor %d"), HomeLevel);
	}
	const FRHHabNetCache* Net = FindHabNet(HomeHab);
	return FString::Printf(TEXT("Surface habitat #%d"), Net ? Net->Root : HomeHab);
}

double URHSimWorldSubsystem::GetColonistRation(const FRHColonist& C) const
{
	int32 Key = C.HomeLevel;
	if (C.HomeHab != 0)
	{
		const FRHHabNetCache* Net = FindHabNet(C.HomeHab);
		Key = Net ? Net->Root : C.HomeHab;
	}
	for (const FRHHomeRation& Home : HomeRations)
	{
		if (Home.Key == Key)
		{
			return Home.Fraction;
		}
	}
	return 1.0;
}

void URHSimWorldSubsystem::StepPopulation(float SubDt)
{
	// Jobs re-derive every step (rooms/floors/roster all change them); the
	// reset keeps the map honest at zero pop too.
	RefreshJobs();
	// Lazy seat fill (T3): while someone is idle and an unassigned seat is
	// open, the accumulator runs; each LazySeatFillSols one more seat is
	// staffed (threshold-on-accumulator with reset - the StepGrowth shape, so
	// both time bands cross on the same step window).
	if (LazySeatFillSols > 0.0 && LastIdleColonists > 0 && LastOpenSeats > 0)
	{
		SeatFillSols += SubDt / (double)URHSimClockSubsystem::SolLengthSimSeconds;
		if (SeatFillSols >= LazySeatFillSols)
		{
			SeatFillSols -= LazySeatFillSols;
			++StaffedSeats;
			const int32 Before = ColonistStations.Num();
			RefreshJobs();
			if (ColonistStations.Num() > Before)
			{
				// Placeholder copy (Gate-D review). The newest seat-holder is
				// the highest-Id colonist with a station.
				int32 NewId = 0;
				for (const TPair<int32, FIntVector>& CS : ColonistStations) { NewId = FMath::Max(NewId, CS.Key); }
				FString Name;
				if (const FRHColonist* NewHand = FindColonist(NewId)) { Name = NewHand->Name; }
				const FString Text = FString::Printf(TEXT("%s took charge of an empty %s seat."), *Name, *GetColonistJob(NewId).ToString().ToLower());
				OnCrewMoment.Broadcast((uint8)ERHCrewMoment::JoinWork, NewId, 0, Text);
				UE_LOG(LogRedHopeSim, Display, TEXT("STAFF lazy fill: %s (staffed %d)"), *Text, StaffedSeats);
			}
		}
	}
	else
	{
		SeatFillSols = 0.0;
	}
	if (Colonists.Num() == 0)
	{
		ComfortsSupplied = 0;
		DeficitEquivalent = 0.0; // C2a: no homes, no rations, no one on their way out
		HomeClocks.Reset();
		HomeRations.Reset();
		bFoodLowAnnounced = false; // no crew, no larder episode (D4a; runtime only)
		FreshWaterThisStepKg = 0.0; // no consumer to fold it into potability
		// The quiet colony (F7b, 2026-10-21): a colony that HAD people and lost them all is in a
		// state, not an ending. It says so once per quiet spell, with the recovery path named
		// (prevention framing), and the latch re-arms when people return (see below). The
		// projected ending ignores it, and the epilogue clock waits while it lasts.
		if (bEverHadCrew && !bQuietAnnounced)
		{
			bQuietAnnounced = true;
			OnAlert.Broadcast(TEXT(
				"THE COLONY IS QUIET — everyone is safe in orbit. The machines keep the lights on. "
				"Restore housing and life support, and people can come home to Mars."));
			UE_LOG(LogRedHopeSim, Display, TEXT("=== THE COLONY IS QUIET (everyone recalled; recoverable) ==="));
		}
		return; // pre-crew colonies: zero cost, zero divergence
	}
	bQuietAnnounced = false; // F7b: people are here, so the next quiet spell announces itself again
	StepMoveHome(SubDt); // C2b: once a sol, anyone whose home has lost their bed moves to a free one
	static const FName NFood(TEXT("Food")), NWater(TEXT("Water")), NOxygen(TEXT("Oxygen"));
	const double DtSols = SubDt / (double)URHSimClockSubsystem::SolLengthSimSeconds;

	int32 SuppliedThisStep = 0;
	double WaterDrawnThisStep = 0.0; // for greywater reclaim + potability
	bool bAnyHungry = false; // D4a: the food pool fell short this step
	// C1: the evacuation countdown is the same for everyone this step (nothing in
	// the loop moves a floor's rating, a room or a building), so the infirmary
	// scan runs once, on first need - not once per short home.
	bool bEvacSolsKnown = false;
	double EvacSolsNow = 0.0;
	const auto EvacSolsThisStep = [&]()
	{
		if (!bEvacSolsKnown)
		{
			EvacSolsNow = GetEffectiveEvacSols();
			bEvacSolsKnown = true;
		}
		return EvacSolsNow;
	};

	// --- C2a: shared rationing ---
	// A HOME is a rated floor, or a surface-habitat network. Food and water are
	// colony-wide pools and air is per home. Each pool is drawn once for the
	// people it serves: if it covers them all, everyone is fed; if not, every
	// resident gets the SAME fraction of the ration and the whole of what there
	// was is taken - never "the first in the roster eats, the last goes without".
	// A home's ration is the smallest fraction across food, water and air.
	const int32 Pop0 = Colonists.Num();
	struct FHome
	{
		int32 Key = 0, HomeLevel = 0, HomeHab = 0, Residents = 0;
		bool bWasShort = false;   // someone here was already short at the start of the step
		double RAir = 1.0, R = 1.0;
		int32 Limit = 0;          // 0 none, 1 air, 2 water, 3 food (ties go in that order)
		int32 Departing = 0;
	};
	// Floors are keyed by level (<= 0), a surface network by its root hab Id (> 0).
	const auto HomeKeyOf = [this](const FRHColonist& C) -> int32
	{
		if (C.HomeHab == 0)
		{
			return C.HomeLevel;
		}
		const FRHHabNetCache* Net = FindHabNet(C.HomeHab);
		return Net ? Net->Root : C.HomeHab;
	};
	TArray<int32> KeyOf;
	KeyOf.SetNumUninitialized(Pop0);
	TArray<FHome> Homes;
	TMap<int32, int32> HomeSlot; // key -> index into Homes (lookup only, never iterated)
	for (int32 i = 0; i < Pop0; ++i)
	{
		const FRHColonist& C = Colonists[i];
		const int32 Key = HomeKeyOf(C);
		KeyOf[i] = Key;
		int32* Found = HomeSlot.Find(Key);
		if (!Found)
		{
			FHome Fresh;
			Fresh.Key = Key;
			Fresh.HomeLevel = C.HomeLevel;
			Fresh.HomeHab = C.HomeHab;
			Found = &HomeSlot.Add(Key, Homes.Add(Fresh));
		}
		FHome& Home = Homes[*Found];
		++Home.Residents;
		Home.bWasShort |= !C.bSupported;
	}
	Homes.Sort([](const FHome& A, const FHome& B) { return A.Key < B.Key; });
	HomeSlot.Reset();
	for (int32 h = 0; h < Homes.Num(); ++h)
	{
		HomeSlot.Add(Homes[h].Key, h);
	}

	// The draws repeat the per-colonist arithmetic exactly (one subtraction per
	// person, in the same order), so a colony that is fully supplied sees the
	// numbers it always did. Cover returns how many people the pool feeds in full.
	const auto Cover = [](double Pool, double Need, int32 Count, double& OutLeft, double* OutDrawn) -> int32
	{
		int32 Fed = 0;
		OutLeft = Pool;
		while (Fed < Count && OutLeft >= Need)
		{
			OutLeft -= Need;
			if (OutDrawn)
			{
				*OutDrawn += Need;
			}
			++Fed;
		}
		return Fed;
	};
	const double NeedO2 = ColonistO2KgPerSol * DtSols;
	for (FHome& Home : Homes)
	{
		if (Home.HomeHab != 0)
		{
			// BM-1: a surface resident breathes from the whole network (drawn per
			// resident below). A network that is not rated has no air to share.
			const FRHHabNetCache* Net = FindHabNet(Home.HomeHab);
			if (Net && IsNetRated(*Net))
			{
				const double Supply = GetHabNetworkO2Kg(Home.HomeHab);
				double Left = 0.0;
				Home.RAir = Cover(Supply, NeedO2, Home.Residents, Left, nullptr) == Home.Residents ? 1.0
					: FMath::Clamp(Supply / (Home.Residents * NeedO2), 0.0, 1.0);
			}
			else
			{
				Home.RAir = 0.0;
			}
		}
		else if (RatedFloors.Contains(Home.HomeLevel))
		{
			// Breathe: draw from the home floor's fill. The trunk refills it (the
			// M1-d loop); a crowd can out-breathe a starved circulator.
			double& FillKg = FloorO2Kg.FindOrAdd(Home.HomeLevel);
			double Left = 0.0;
			if (Cover(FillKg, NeedO2, Home.Residents, Left, nullptr) == Home.Residents)
			{
				FillKg = Left;
				Home.RAir = 1.0;
			}
			else
			{
				Home.RAir = FMath::Clamp(FillKg / (Home.Residents * NeedO2), 0.0, 1.0);
				FillKg = FMath::Min(FillKg, 0.0); // all there was is taken (never below zero, never a refund)
			}
		}
		else
		{
			Home.RAir = 0.0;
		}
	}
	// Eat and drink: the pooled Food and Water (the ice-cap imperative: no water
	// is as fatal as no air or food).
	const auto DrawPool = [this, Pop0, &Cover](FName Resource, double Need, double* OutDrawn) -> double
	{
		const double Have = GetStock(Resource);
		double Left = 0.0;
		double Drawn = 0.0;
		const int32 Fed = Cover(Have, Need, Pop0, Left, &Drawn);
		double Ration = 1.0;
		if (Fed < Pop0)
		{
			Ration = Have > 0.0 ? FMath::Clamp(Have / (Pop0 * Need), 0.0, 1.0) : 0.0;
			Left = FMath::Min(Have, 0.0); // all there was is taken (never below zero, never a refund)
			Drawn = FMath::Max(Have, 0.0);
		}
		if (Drawn > 0.0)
		{
			double& Amount = Stocks.FindOrAdd(Resource);
			Amount = Left;
			OnStockChanged.Broadcast(Resource, Amount);
		}
		if (OutDrawn)
		{
			*OutDrawn += Drawn;
		}
		return Ration;
	};
	const double RFood = DrawPool(NFood, ColonistFoodKgPerSol * DtSols, nullptr);
	const double RWater = DrawPool(NWater, ColonistWaterKgPerSol * DtSols, &WaterDrawnThisStep);
	bAnyHungry = RFood < 1.0;
	for (FHome& Home : Homes)
	{
		Home.R = 1.0;
		if (Home.RAir < Home.R) { Home.R = Home.RAir; Home.Limit = 1; }
		if (RWater < Home.R)    { Home.R = RWater;    Home.Limit = 2; }
		if (RFood < Home.R)     { Home.R = RFood;     Home.Limit = 3; }
	}

	for (int32 i = Pop0 - 1; i >= 0; --i)
	{
		FRHColonist& C = Colonists[i];
		const FHome& Home = Homes[HomeSlot.FindChecked(KeyOf[i])];

		// BM-1: a surface resident breathes from the whole network - their own hab
		// first, then the other members in ascending Id.
		if (C.HomeHab != 0 && Home.RAir > 0.0)
		{
			if (const FRHHabNetCache* Net = FindHabNet(C.HomeHab))
			{
				double Left = NeedO2 * Home.RAir;
				const auto Take = [this, &Left](int32 Id)
				{
					if (double* V = HabO2Kg.Find(Id))
					{
						const double T = FMath::Min(*V, Left);
						*V -= T;
						Left -= T;
					}
				};
				Take(C.HomeHab);
				for (const int32 Id : Net->Members)
				{
					if (Id != C.HomeHab && Left > 0.0)
					{
						Take(Id);
					}
				}
			}
		}

		// Comforts (M2 Gate D, abstract): a luxury ration when stocked. Never
		// part of the support contract - going without lifts nothing, costs
		// nothing (prevention framing; Gate-D review owns all wording).
		static const FName NLuxury(TEXT("Luxury"));
		const double NeedLux = LuxuryKgPerColonistPerSol * DtSols;
		if (GetStock(NLuxury) >= NeedLux)
		{
			AddStock(NLuxury, -NeedLux);
			++SuppliedThisStep;
		}

		// Practice makes mastery (alive pass): sols at a post accrue that job's
		// skill. Linear in dt = identical across both time bands.
		const FName Job = GetColonistJob(C.Id);
		if (!Job.IsNone())
		{
			C.SkillSols.FindOrAdd(Job) += DtSols;
		}

		// bSupported reads "on a full ration" (the panels and the births keep
		// reading it that way); UnsupportedSimSeconds is the time spent short.
		C.bSupported = Home.R >= 1.0;
		C.UnsupportedSimSeconds = C.bSupported ? 0.0 : C.UnsupportedSimSeconds + SubDt;
	}
	ComfortsSupplied = SuppliedThisStep;

	// One alert per EVENT, not per person: a home goes on short rations, a home
	// recovers, a batch of residents is recalled. Short homes accumulate toward a
	// departure in proportion to how deep and how long the shortage is: each
	// recall costs one whole unit of OnsetResidents x (1 - ration) x sols /
	// EffectiveEvacSols, where OnsetResidents is the home's size when it went
	// short (raised if it grew). So a total failure empties a home in
	// EffectiveEvacSols at a steady rate (a home of four loses one every half sol),
	// and half a ration takes twice as long - the clock does not slow as the home
	// shrinks. All wording is a Gate-D framing-review placeholder
	// (docs/qa/2026-10-15/gate-d-wording.md).
	const auto Plural = [](int32 Count) { return Count == 1 ? TEXT("") : TEXT("s"); };
	const auto LegName = [](int32 Limit) { return Limit == 1 ? TEXT("air") : (Limit == 2 ? TEXT("water") : TEXT("food")); };
	bool bAnyDeparting = false;
	for (FHome& Home : Homes)
	{
		const bool bShort = Home.R < 1.0;
		if (bShort && !Home.bWasShort)
		{
			const FString Label = GetHomeLabel(Home.HomeLevel, Home.HomeHab);
			OnAlert.Broadcast(FString::Printf(
				TEXT("SHORT RATIONS: %s — %s is running short for %d resident%s. Restore it to bring rations back to full; if it lasts, the Program recalls residents to safety in orbit, a few at a time."),
				*Label, LegName(Home.Limit), Home.Residents, Plural(Home.Residents)));
			UE_LOG(LogRedHopeSim, Display, TEXT("=== HOME SHORT RATIONS: %s (%s, %d resident%s, ration %.0f%%) ==="),
				*Label, LegName(Home.Limit), Home.Residents, Plural(Home.Residents), Home.R * 100.0);
		}
		else if (!bShort && Home.bWasShort)
		{
			const FString Label = GetHomeLabel(Home.HomeLevel, Home.HomeHab);
			OnAlert.Broadcast(FString::Printf(TEXT("RATIONS RESTORED: %s is back on full rations (%d resident%s)."),
				*Label, Home.Residents, Plural(Home.Residents)));
			UE_LOG(LogRedHopeSim, Display, TEXT("=== HOME RATIONS RESTORED: %s (%d resident%s) ==="), *Label, Home.Residents, Plural(Home.Residents));
		}
		if (!bShort)
		{
			HomeClocks.Remove(Home.Key); // a full ration resets the clock and the onset size, as the countdown always did
			continue;
		}
		// Infirmary (station tiers, Gate A): an online infirmary stretches the
		// countdown - more time to fix the missing leg (prevention framing).
		const double EvacSeconds = FMath::Max(EvacSolsThisStep() * (double)URHSimClockSubsystem::SolLengthSimSeconds, (double)KINDA_SMALL_NUMBER);
		FRHHomeClock& HomeClock = HomeClocks.FindOrAdd(Home.Key);
		HomeClock.OnsetResidents = FMath::Max(HomeClock.OnsetResidents, Home.Residents); // the size at onset (a new clock reads 0), raised if the home grew
		HomeClock.Acc += HomeClock.OnsetResidents * (1.0 - Home.R) * (double)SubDt / EvacSeconds;
		while (HomeClock.Acc >= 1.0 && Home.Departing < Home.Residents)
		{
			++Home.Departing;
			HomeClock.Acc -= 1.0;
		}
		bAnyDeparting |= Home.Departing > 0;
	}

	TSet<int32> Leaving; // colonist Ids recalled this step
	if (bAnyDeparting)
	{
		// One pass gathers each leaving home's residents; the newest arrival goes
		// first (highest Id), and the Mars-born go last of all.
		TArray<TArray<int32>> Candidates;
		Candidates.SetNum(Homes.Num());
		for (int32 i = 0; i < Pop0; ++i)
		{
			const int32 h = HomeSlot.FindChecked(KeyOf[i]);
			if (Homes[h].Departing > 0)
			{
				Candidates[h].Add(i);
			}
		}
		for (int32 h = 0; h < Homes.Num(); ++h)
		{
			const FHome& Home = Homes[h];
			if (Home.Departing == 0)
			{
				continue;
			}
			TArray<int32>& Pool = Candidates[h];
			Pool.Sort([this](int32 A, int32 B)
			{
				const FRHColonist& CA = Colonists[A];
				const FRHColonist& CB = Colonists[B];
				return CA.bMarsBorn != CB.bMarsBorn ? !CA.bMarsBorn : CA.Id > CB.Id;
			});
			const FString Label = GetHomeLabel(Home.HomeLevel, Home.HomeHab);
			for (int32 k = 0; k < Home.Departing; ++k)
			{
				const FRHColonist& Gone = Colonists[Pool[k]];
				Leaving.Add(Gone.Id);
				if (Gone.bMarsBorn)
				{
					// F2b: a recalled Mars-born waits in orbit and comes home on a later ship.
					RecalledMarsBorn.Add({ Gone.Id, Gone.Name });
				}
				UE_LOG(LogRedHopeSim, Display, TEXT("=== COLONIST EVACUATED: %s (%s, rations short %.1f sols) ==="),
					*Gone.Name, *Label, Gone.UnsupportedSimSeconds / (double)URHSimClockSubsystem::SolLengthSimSeconds);
			}
			// Abstract, prevention-framed consequence (mental-health directive):
			// the Program recalls people to orbit.
			OnAlert.Broadcast(FString::Printf(TEXT("RECALLED: %d resident%s of %s, now safe in orbit — rations were short too long. Restoring the supply keeps the rest at home."),
				Home.Departing, Plural(Home.Departing), *Label));
		}
		Colonists.RemoveAll([&Leaving](const FRHColonist& C) { return Leaving.Contains(C.Id); });
		bColonistIndexDirty = true; // everyone after a recalled colonist moved down
		++ColonistSetVersion;
	}

	// What Hope reads, and the readout for the panels: who is short, and by how much.
	DeficitEquivalent = 0.0;
	HomeRations.Reset();
	for (const FHome& Home : Homes)
	{
		const int32 Remaining = Home.Residents - Home.Departing;
		if (Remaining <= 0)
		{
			HomeClocks.Remove(Home.Key); // nobody left to leave
			continue;
		}
		DeficitEquivalent += Remaining * (1.0 - Home.R);
		FRHHomeRation Ration;
		Ration.Key = Home.Key;
		Ration.HomeLevel = Home.HomeLevel;
		Ration.HomeHab = Home.HomeHab;
		Ration.Residents = Remaining;
		Ration.Fraction = Home.R;
		Ration.Limiting = Home.Limit == 0 ? NAME_None : (Home.Limit == 1 ? NOxygen : (Home.Limit == 2 ? NWater : NFood));
		HomeRations.Add(Ration);
	}
	// A home that vanished (a floor lost its rating with nobody on it, a network
	// re-rooted) takes its accumulator with it.
	for (auto It = HomeClocks.CreateIterator(); It; ++It)
	{
		if (!HomeSlot.Contains(It.Key()))
		{
			It.RemoveCurrent();
		}
	}

	// The water loop (M2 Gate D+). Recycling reclaims most of what the crew
	// drank - stretching the supply - but every cycle degrades POTABILITY. Only
	// fresh ice-melt makeup (tracked at the MeltIce seam) restores it. Both the
	// decay and the restore are LINEAR (per-sol / per-kg), so the running value
	// integrates identically in the agent band and the 60x era band.
	if (WaterDrawnThisStep > 0.0)
	{
		AddStock(NWater, WaterDrawnThisStep * GreywaterReturnFraction);
	}
	if (Colonists.Num() > 0)
	{
		WaterPotability -= WaterPotabilityDecayPerSol * DtSols;
	}
	WaterPotability += FreshWaterThisStepKg * WaterPotabilityRestorePerKg;
	WaterPotability = FMath::Clamp(WaterPotability, 0.0, 1.0);
	FreshWaterThisStepKg = 0.0; // consumed; reset before the next StepProduction

	// Grim-water edge: one alert per episode when potability drops through the
	// floor, cleared when fresh water lifts it back (prevention framing).
	if (WaterPotability < WaterPotabilityFloor && !bWaterQualityAnnounced)
	{
		bWaterQualityAnnounced = true;
		OnAlert.Broadcast(TEXT("WATER QUALITY LOW — most of the water is recycled now. Fresh ice-melt restores it: drill more ice."));
	}
	else if (WaterPotability >= WaterPotabilityFloor)
	{
		bWaterQualityAnnounced = false;
	}

	// Food runway edge (D4a): one alert per episode as the larder drops under the
	// floor at today's draw; it re-arms only past FoodRunwayRearmSols (hysteresis,
	// so a break-even garden never repeats it). Runtime-only and changes no sim
	// state, so it is band-safe by construction. If someone already went hungry
	// this step, LIFE SUPPORT ('food stores empty') owns the banner, so the
	// episode latches silently. Gate-D framing: placeholder wording.
	const double FoodRunway = GetFoodRunwaySols();
	if (FoodRunway < FoodRunwayFloorSols && !bFoodLowAnnounced)
	{
		bFoodLowAnnounced = true;
		if (!bAnyHungry)
		{
			const int32 N = FMath::Max(1, FMath::FloorToInt(FoodRunway)); // never overstate
			OnAlert.Broadcast(FString::Printf(
				TEXT("FOOD RUNNING LOW — about %d sol%s left at today's draw. Grow more: zone Gardening Station cells and keep their lights powered (Earth Soil and Seeds come by supply ship)."),
				N, N == 1 ? TEXT("") : TEXT("s")));
		}
	}
	else if (FoodRunway >= FoodRunwayRearmSols)
	{
		bFoodLowAnnounced = false;
	}

	// Crew life (alive pass): the moments generator rides the population step -
	// both bands, and the zero-pop early-out above already protects baselines.
	StepMoments(SubDt);
}

double URHSimWorldSubsystem::GetCrewSkillMul(FName Function) const
{
	// The average mastery of everyone currently holding that job: a fresh hand
	// works at 1x, a master at 2x. Nobody employed -> exactly 1.0 (baselines).
	// C6a hook (a), Garden only (Lab and Workstation are cut once, in their seat sums, by RefreshJobs): a long
	// commuter's term counts 1 - Ramp x (1 - CommuteWorkMul). The branch is taken only while someone is long.
	static const FName NGardenFn(TEXT("Garden"));
	const TArray<int32>* LongIds = nullptr;
	double LongMul = 1.0;
	if (Function == NGardenFn)
	{
		const double Ramp = GetCommuteRamp();
		if (Ramp > 0.0)
		{
			const FRHCommuteFlows& CF = EnsureCommuteFlows();
			if (CF.LongCount > 0)
			{
				LongIds = &CF.LongIds;
				LongMul = 1.0 - Ramp * (1.0 - CommuteWorkMul);
			}
		}
	}
	double Sum = 0.0;
	int32 Count = 0;
	for (const FRHColonist& C : Colonists)
	{
		if (GetColonistJob(C.Id) == Function)
		{
			const double* Sols = C.SkillSols.Find(Function);
			double Term = 1.0 + FMath::Min(1.0, (Sols ? *Sols : 0.0) / FMath::Max(SkillMasterySols, KINDA_SMALL_NUMBER));
			if (LongIds && Algo::BinarySearch(*LongIds, C.Id) != INDEX_NONE)
			{
				Term *= LongMul;
			}
			Sum += Term;
			++Count;
		}
	}
	return Count > 0 ? Sum / Count : 1.0;
}

void URHSimWorldSubsystem::StepMoments(float SubDt)
{
	// A deterministic heartbeat of visible crew life. Every interval, a seeded
	// pick (content hash + the saved counter) chooses a moment:
	//  - needs unmet or Hope low -> a DISPUTE (abstracted: two people at odds
	//    over the shortages - never violence, and NO Hope penalty; the
	//    friction is a visible symptom, not a second punishment; never between
	//    people born on Mars, F1b)
	//  - otherwise a JOIN-IN (a free colonist drifts over to help a worker)
	//    or a PASTIME (two crew unwind together, flavored by disposition)
	// All copy is Gate-D framing-review placeholder.
	MomentSols += SubDt / (double)URHSimClockSubsystem::SolLengthSimSeconds;
	if (MomentSols < CrewMomentIntervalSols)
	{
		return;
	}
	MomentSols -= CrewMomentIntervalSols;
	const uint32 Seed = HashCombine(FCrc::StrCrc32(TEXT("moment")), (uint32)(MomentCounter * 2654435761u));
	++MomentCounter;
	const int32 Pop = Colonists.Num();
	if (Pop < 2)
	{
		// C2a: every moment is between two DIFFERENT people. A lone survivor (a
		// shortage can now leave one for sols) has nobody to argue with or unwind
		// beside, so the beat passes silently - and a colony emptied this very step
		// has nobody to index (the pick below divides by the headcount).
		return;
	}
	bool bAnyUnsupported = false;
	for (const FRHColonist& C : Colonists)
	{
		bAnyUnsupported |= !C.bSupported;
	}
	const bool bDispute = bAnyUnsupported || HopeSmoothed < DisputeHopeBelow;

	// F1b (Gate-D review, 2026-10-21): a dispute pair is drawn from the people who came from
	// Earth, never someone born on Mars. With fewer than two of them the beat passes quietly, as
	// it does for a lone colonist. The same seeded draw runs over the Earth-born list, so a colony
	// with nobody born here draws exactly the pair it always did. Pastimes and join-ins still use
	// everyone.
	TArray<int32, TInlineAllocator<64>> EarthBorn;
	if (bDispute)
	{
		for (int32 i = 0; i < Pop; ++i)
		{
			if (!Colonists[i].bMarsBorn)
			{
				EarthBorn.Add(i);
			}
		}
		if (EarthBorn.Num() < 2)
		{
			return;
		}
	}
	const int32 Pool = bDispute ? EarthBorn.Num() : Pop;
	const int32 IdxA = (int32)(Seed % (uint32)Pool);
	int32 IdxB = (int32)((Seed >> 8) % (uint32)Pool);
	if (IdxB == IdxA)
	{
		IdxB = (IdxA + 1) % Pool; // never pair a colonist with themselves
	}
	const FRHColonist& A = Colonists[bDispute ? EarthBorn[IdxA] : IdxA];
	const FRHColonist& B = Colonists[bDispute ? EarthBorn[IdxB] : IdxB];

	if (bDispute && A.Id != B.Id)
	{
		// Placeholder copy (Gate-D review).
		const FString Text = A.HomeHab != 0
			? FString::Printf(TEXT("Tension in the surface habitat — %s and %s are at odds over the shortages. Restocking will ease it."),
				*A.Name, *B.Name)
			: FString::Printf(TEXT("Tension on floor %d — %s and %s are at odds over the shortages. Restocking will ease it."),
				A.HomeLevel, *A.Name, *B.Name);
		OnCrewMoment.Broadcast((uint8)ERHCrewMoment::Dispute, A.Id, B.Id, Text);
		UE_LOG(LogRedHopeSim, Display, TEXT("MOMENT dispute: %s vs %s"), *A.Name, *B.Name);
		return;
	}

	// A join-in wants one FREE colonist and one WORKER; scan deterministically
	// from the seeded start for the first of each.
	int32 WorkerId = 0, FreeId = 0;
	FName WorkerJob;
	FString WorkerName, FreeName;
	for (int32 i = 0; i < Pop; ++i)
	{
		const FRHColonist& C = Colonists[(int32)((Seed % (uint32)Pop) + i) % Pop];
		const FName Job = GetColonistJob(C.Id);
		if (!Job.IsNone() && WorkerId == 0) { WorkerId = C.Id; WorkerJob = Job; WorkerName = C.Name; }
		if (Job.IsNone() && FreeId == 0) { FreeId = C.Id; FreeName = C.Name; }
	}
	if (WorkerId != 0 && FreeId != 0 && (Seed & 4u))
	{
		const FString Text = FString::Printf(
			TEXT("%s wandered over to the %s post and rolled up their sleeves next to %s. Many hands."),
			*FreeName, *WorkerJob.ToString(), *WorkerName);
		OnCrewMoment.Broadcast((uint8)ERHCrewMoment::JoinWork, FreeId, WorkerId, Text);
		UE_LOG(LogRedHopeSim, Display, TEXT("MOMENT join: %s -> %s (%s)"), *FreeName, *WorkerName, *WorkerJob.ToString());
		return;
	}

	// A pastime, flavored by the first colonist's disposition.
	const FString Trait = TraitFor(A.Id);
	FString Text;
	if (Trait == TEXT("Stargazer"))       { Text = FString::Printf(TEXT("%s took %s up top to watch Phobos rise. The dust makes the sunsets blue."), *A.Name, *B.Name); }
	else if (Trait == TEXT("Musician"))   { Text = FString::Printf(TEXT("%s is playing something soft in the mess; %s is humming along."), *A.Name, *B.Name); }
	else if (Trait == TEXT("Cardsharp"))  { Text = FString::Printf(TEXT("%s dealt %s in for cards. The stakes: tomorrow's dish duty."), *A.Name, *B.Name); }
	else if (Trait == TEXT("Tinkerer"))   { Text = FString::Printf(TEXT("%s is rebuilding a spare servo for fun while %s hands over tools."), *A.Name, *B.Name); }
	else if (Trait == TEXT("Chef"))       { Text = FString::Printf(TEXT("%s is improvising with the rations again. %s volunteered to taste."), *A.Name, *B.Name); }
	else if (Trait == TEXT("Athlete"))    { Text = FString::Printf(TEXT("%s challenged %s to low-gravity handball down the corridor."), *A.Name, *B.Name); }
	else if (Trait == TEXT("Bookworm"))   { Text = FString::Printf(TEXT("%s is reading aloud from the archive; %s stayed to listen."), *A.Name, *B.Name); }
	else if (Trait == TEXT("Sketcher"))   { Text = FString::Printf(TEXT("%s is sketching %s against the pit lights. Hold still."), *A.Name, *B.Name); }
	else if (Trait == TEXT("Gardener"))   { Text = FString::Printf(TEXT("%s is showing %s the seedlings. Green, on Mars."), *A.Name, *B.Name); }
	else if (Trait == TEXT("Storyteller")){ Text = FString::Printf(TEXT("%s has the mess laughing at the landing story again. %s swears it grows each telling."), *A.Name, *B.Name); }
	else if (Trait == TEXT("Chessmaster")){ Text = FString::Printf(TEXT("%s and %s are three sols into one chess game. Neither will resign."), *A.Name, *B.Name); }
	else                                  { Text = FString::Printf(TEXT("%s and %s are up top, quietly watching Earth rise. It's small from here."), *A.Name, *B.Name); }
	OnCrewMoment.Broadcast((uint8)ERHCrewMoment::Pastime, A.Id, B.Id, Text);
	UE_LOG(LogRedHopeSim, Display, TEXT("MOMENT pastime (%s): %s + %s"), *Trait, *A.Name, *B.Name);
}

FIntPoint URHSimWorldSubsystem::SpiralCell(int32 Index)
{
	// Deterministic square spiral over the 10 m cell grid, skipping (0,0)
	// (the shaft column's own cell): (1,0), (1,1), (0,1), (-1,1), ...
	// Moved sim-side at Gate B: room adjacency made cell GEOMETRY gameplay,
	// so the sim owns the layout; presentation reads this same function.
	FRHSpiralWalk Walk;
	FIntPoint P(0, 0);
	for (int32 i = 0; i <= Index; ++i)
	{
		P = Walk.Next();
	}
	return P;
}

FRHFloor& URHSimWorldSubsystem::FindOrAddFloor(int32 Level)
{
	if (const int32* I = FloorIndexByLevel.Find(Level))
	{
		return FloorTable[*I];
	}
	// Sorted by (region, Level descending). Region 0 is the only region in D1.
	int32 At = 0;
	while (At < FloorTable.Num() && FloorTable[At].Level > Level)
	{
		++At;
	}
	FRHFloor Fresh;
	Fresh.Level = Level;
	FloorTable.Insert(MoveTemp(Fresh), At);
	RebuildFloorIndex();
	return FloorTable[At];
}

void URHSimWorldSubsystem::RebuildFloorIndex()
{
	FloorIndexByLevel.Reset();
	for (int32 i = 0; i < FloorTable.Num(); ++i)
	{
		FloorIndexByLevel.Add(FloorTable[i].Level, i);
	}
}

void URHSimWorldSubsystem::AppendCarvedCell(FRHFloor& Floor, const FIntPoint& P)
{
	Floor.IndexOf.Add(P, Floor.Cells.Num());
	Floor.Cells.Add(P);
	++FloorCellsVersion;
}

FIntPoint URHSimWorldSubsystem::CarveNextCell(int32 Level)
{
	FRHFloor& Floor = FindOrAddFloor(Level);
	FIntPoint P(0, 0);
	if (Floor.Plan.Num() > 0)
	{
		P = Floor.Plan[0];
		Floor.Plan.RemoveAt(0);
	}
	else
	{
		for (;;) // the cursor skips spiral points that are already carved; it never moves past an uncarved one
		{
			FRHSpiralWalk Probe = Floor.Walk;
			const FIntPoint Q = Probe.Next();
			if (Floor.IndexOf.Contains(Q))
			{
				Floor.Walk = Probe;
				continue;
			}
			P = Q;
			break;
		}
	}
	AppendCarvedCell(Floor, P);
	return P;
}

void URHSimWorldSubsystem::PeekCarveCells(int32 Level, int32 N, TArray<FIntPoint>& Out) const
{
	Out.Reset();
	const FRHFloor* Floor = FindFloor(Level);
	FRHFloor Empty;
	if (!Floor)
	{
		Floor = &Empty;
	}
	const int32 FromPlan = FMath::Min(N, Floor->Plan.Num());
	for (int32 i = 0; i < FromPlan; ++i)
	{
		Out.Add(Floor->Plan[i]);
	}
	FRHSpiralWalk Probe = Floor->Walk;
	while (Out.Num() < N)
	{
		const FIntPoint Q = Probe.Next();
		if (!Floor->IndexOf.Contains(Q) && !Out.Contains(Q))
		{
			Out.Add(Q);
		}
	}
}

FIntPoint URHSimWorldSubsystem::GetFloorCell(int32 Level, int32 Index) const
{
	if (const FRHFloor* Floor = FindFloor(Level))
	{
		if (Floor->Cells.IsValidIndex(Index))
		{
			return Floor->Cells[Index];
		}
	}
	++FloorCellFallbacks;
	return SpiralCell(Index);
}

FVector URHSimWorldSubsystem::GetFloorCellCentreCm(int32 Level, int32 Index) const
{
	const FIntPoint P = GetFloorCell(Level, Index);
	return FVector(ShaftHeadCm.X + P.X * 1000.0, ShaftHeadCm.Y + P.Y * 1000.0, Level * FloorHeightCm);
}

bool URHSimWorldSubsystem::Debug_PaintCarve(int32 Level, const FIntPoint& P, FString& OutReason)
{
	if (Level >= 0 || !IsLevelConnected(Level))
	{
		OutReason = FString::Printf(TEXT("Floor %d not reached - bore the shaft deeper first"), Level);
		return false;
	}
	if (P == FIntPoint(0, 0))
	{
		OutReason = TEXT("That is the shaft column");
		return false;
	}
	const int32* Slot = FloorIndexByLevel.Find(Level);
	if (!Slot)
	{
		OutReason = FString::Printf(TEXT("Floor %d has no carved cell yet - the first cell comes from the Borer or ExcavateFloor"), Level);
		return false; // a floor in the table always holds at least one cell (the save refuses an empty one)
	}
	FRHFloor& Floor = FloorTable[*Slot];
	if (Floor.IndexOf.Contains(P))
	{
		OutReason = TEXT("Already carved");
		return false;
	}
	if (Floor.Plan.Contains(P))
	{
		OutReason = TEXT("Already painted");
		return false;
	}
	Floor.Plan.Add(P);
	++FloorCellsVersion;
	return true;
}

void URHSimWorldSubsystem::WriteFloorTable(FArchive& Ar) const
{
	// Save v41, straight after the header: FloorCount, then per floor in table order RegionId, Level,
	// NCells x (X, Y), NPlan x (X, Y). No origin: region 0's origin is the shaft head, saved further on.
	int32 FloorCount = FloorTable.Num();
	Ar << FloorCount;
	for (const FRHFloor& F : FloorTable)
	{
		int32 Region = F.RegionId, Level = F.Level, NCells = F.Cells.Num(), NPlan = F.Plan.Num();
		Ar << Region << Level << NCells;
		for (const FIntPoint& P : F.Cells) { int32 X = P.X, Y = P.Y; Ar << X << Y; }
		Ar << NPlan;
		for (const FIntPoint& P : F.Plan) { int32 X = P.X, Y = P.Y; Ar << X << Y; }
	}
}

bool URHSimWorldSubsystem::ReadFloorTable(FArchive& Ar, TArray<FRHFloor>& OutFloors, FString& OutError) const
{
	// Every refusal here is a structural one, taken before the live colony is touched: it sets OutError and
	// returns false, and logs nothing at Error level.
	const auto Refuse = [&OutError](const FString& Why) { OutError = FString::Printf(TEXT("Save floor table invalid: %s"), *Why); return false; };
	OutFloors.Reset();
	int32 FloorCount = 0;
	Ar << FloorCount;
	if (Ar.IsError() || FloorCount < 0 || FloorCount > 64)
	{
		return Refuse(FString::Printf(TEXT("floor count %d"), FloorCount));
	}
	for (int32 n = 0; n < FloorCount; ++n)
	{
		FRHFloor F;
		int32 NCells = 0;
		Ar << F.RegionId << F.Level << NCells;
		if (Ar.IsError())
		{
			return Refuse(TEXT("the table is cut short"));
		}
		if (F.RegionId != 0)
		{
			return Refuse(FString::Printf(TEXT("floor %d names region %d (only region 0 exists)"), F.Level, F.RegionId));
		}
		if (F.Level >= 0 || F.Level < -MaxDepth)
		{
			return Refuse(FString::Printf(TEXT("floor level %d is outside -1..-%d"), F.Level, MaxDepth));
		}
		for (const FRHFloor& Prev : OutFloors)
		{
			if (Prev.RegionId == F.RegionId && Prev.Level == F.Level)
			{
				return Refuse(FString::Printf(TEXT("floor %d is listed twice"), F.Level));
			}
		}
		if (NCells < 1 || (int64)NCells * 8 > (int64)Ar.TotalSize() - Ar.Tell())
		{
			return Refuse(FString::Printf(TEXT("floor %d lists %d cells"), F.Level, NCells));
		}
		F.Cells.Reserve(NCells);
		for (int32 i = 0; i < NCells; ++i)
		{
			int32 X = 0, Y = 0;
			Ar << X << Y;
			const FIntPoint P(X, Y);
			if (Ar.IsError())
			{
				return Refuse(TEXT("the table is cut short"));
			}
			if (P == FIntPoint(0, 0))
			{
				return Refuse(FString::Printf(TEXT("floor %d lists the shaft column (0,0) as a cell"), F.Level));
			}
			if (F.IndexOf.Contains(P))
			{
				return Refuse(FString::Printf(TEXT("floor %d lists cell (%d,%d) twice"), F.Level, X, Y));
			}
			F.IndexOf.Add(P, F.Cells.Num());
			F.Cells.Add(P);
		}
		int32 NPlan = 0;
		Ar << NPlan;
		if (Ar.IsError() || NPlan < 0 || (int64)NPlan * 8 > (int64)Ar.TotalSize() - Ar.Tell())
		{
			return Refuse(FString::Printf(TEXT("floor %d lists %d planned cells"), F.Level, NPlan));
		}
		TSet<FIntPoint> Seen;
		for (int32 i = 0; i < NPlan; ++i)
		{
			int32 X = 0, Y = 0;
			Ar << X << Y;
			const FIntPoint P(X, Y);
			if (Ar.IsError())
			{
				return Refuse(TEXT("the table is cut short"));
			}
			if (P == FIntPoint(0, 0))
			{
				return Refuse(FString::Printf(TEXT("floor %d plans the shaft column (0,0)"), F.Level));
			}
			if (F.IndexOf.Contains(P))
			{
				return Refuse(FString::Printf(TEXT("floor %d plans cell (%d,%d), which is already carved"), F.Level, X, Y));
			}
			if (Seen.Contains(P))
			{
				return Refuse(FString::Printf(TEXT("floor %d plans cell (%d,%d) twice"), F.Level, X, Y));
			}
			Seen.Add(P);
			F.Plan.Add(P);
		}
		OutFloors.Add(MoveTemp(F));
	}
	return true;
}

bool URHSimWorldSubsystem::DesignateRoom(int32 Level, int32 CellIndex, FName RoomName, FString& OutReason)
{
	const int32 Carved = GetFloorCarvedCells(Level);
	if (Level >= 0 || CellIndex < 0 || CellIndex >= Carved)
	{
		OutReason = FString::Printf(TEXT("Cell %d on floor %d is not carved yet"), CellIndex, Level);
		return false;
	}
	if (!RoomName.IsNone())
	{
		const FRHRoomRow* Row = Defs ? Defs->GetRoom(RoomName) : nullptr;
		if (!Row || !Row->SliceActive)
		{
			OutReason = FString::Printf(TEXT("'%s' is not a designatable room function"), *RoomName.ToString());
			return false;
		}
	}
	if (!RoomName.IsNone() && FindModuleOnCell(Level, CellIndex))
	{
		OutReason = TEXT("A station module sits on this cell - remove it to zone the cell");
		return false;
	}
	if (!FurnitureAllowsRezone(FIntVector(Level, CellIndex, 0), RoomName, OutReason))
	{
		return false; // BM-4: furniture that can't stand in the new room
	}
	TArray<FName>& Cells = FloorRoomCells.FindOrAdd(Level);
	if (Cells.Num() < Carved)
	{
		Cells.SetNum(Carved); // new carves default undesignated
	}
	Cells[CellIndex] = RoomName;
	++ZoningVersion; // C1: a re-zoned cell can change who has a bed
	{
		const FRHRoomRow* NewRow = (Defs && !RoomName.IsNone()) ? Defs->GetRoom(RoomName) : nullptr;
		if (!NewRow || !(NewRow->Function == FName("Lab") || NewRow->Function == FName("Workstation") || NewRow->Function == FName("Garden")))
		{
			SeatPriority.Remove(FIntVector(Level, CellIndex, 0));
		}
	}
	UE_LOG(LogRedHopeSim, Display, TEXT("Room designation: floor %d cell %d -> %s"),
		Level, CellIndex, RoomName.IsNone() ? TEXT("(cleared)") : *RoomName.ToString());
	return true;
}

FName URHSimWorldSubsystem::GetRoomAt(int32 Level, int32 CellIndex) const
{
	const TArray<FName>* Cells = FloorRoomCells.Find(Level);
	return (Cells && Cells->IsValidIndex(CellIndex)) ? (*Cells)[CellIndex] : NAME_None;
}

FName URHSimWorldSubsystem::GetBayRoom(int32 HabId, int32 Bay) const
{
	// Built members read the topology cache (O(1)); a construction site falls
	// back to the scan.
	if (const FRHHabMemberCache* M = HabMembers.Find(HabId))
	{
		if (Bay < 0 || Bay >= M->NX * M->NY)
		{
			return NAME_None;
		}
		if (const TArray<FName>* Rooms = HabBayRooms.Find(HabId))
		{
			if (Rooms->IsValidIndex(Bay) && !(*Rooms)[Bay].IsNone())
			{
				return (*Rooms)[Bay];
			}
		}
		return M->FixedRoom;
	}
	const FRHBuildingInstance* B = FindBuildingById(HabId);
	const FRHBuildingRow* Def = (B && Defs) ? Defs->GetBuilding(B->DefName) : nullptr;
	const FIntPoint G = B ? GetBayGrid(*B) : FIntPoint(0, 0);
	if (!Def || G.X <= 0 || Bay < 0 || Bay >= G.X * G.Y)
	{
		return NAME_None;
	}
	if (const TArray<FName>* Rooms = HabBayRooms.Find(HabId))
	{
		if (Rooms->IsValidIndex(Bay) && !(*Rooms)[Bay].IsNone())
		{
			return (*Rooms)[Bay];
		}
	}
	return Def->ProvidesRoom; // a corridor's fixed Hallway; None for a free shell
}

const FRHBuildingInstance* URHSimWorldSubsystem::FindModuleInBay(int32 HabId, int32 Bay) const
{
	for (const FRHBuildingInstance& B : Buildings)
	{
		if (B.Level != 0)
		{
			continue;
		}
		const FRHBuildingRow* Def = Defs ? Defs->GetBuilding(B.DefName) : nullptr;
		int32 H = 0, Slot = INDEX_NONE;
		if (Def && Def->Interior && !Def->Pressurized && !Def->ProvidesRoom.IsNone()
			&& FindBayAt(B.LocationCm, H, Slot) && H == HabId && Slot == Bay)
		{
			return &B;
		}
	}
	return nullptr;
}

bool URHSimWorldSubsystem::DesignateBay(int32 HabId, int32 Bay, FName RoomName, FString& OutReason)
{
	// BM-1: zone a surface habitat bay. DesignateRoom stays the underground's.
	// Copy is a Gate-D-adjacent placeholder.
	const FRHBuildingInstance* B = FindBuildingById(HabId);
	const FRHBuildingRow* Def = (B && Defs) ? Defs->GetBuilding(B->DefName) : nullptr;
	const FIntPoint G = B ? GetBayGrid(*B) : FIntPoint(0, 0);
	if (!B || B->Level != 0 || !Def || G.X <= 0 || Bay < 0 || Bay >= G.X * G.Y)
	{
		OutReason = TEXT("No habitat bay there");
		return false;
	}
	if (!Def->ProvidesRoom.IsNone())
	{
		OutReason = FString::Printf(TEXT("This bay is part of a %s - its room is fixed"), *Def->DisplayName);
		return false;
	}
	const FRHRoomRow* Row = RoomName.IsNone() ? nullptr : Defs->GetRoom(RoomName);
	if (!RoomName.IsNone() && (!Row || !Row->SliceActive))
	{
		OutReason = FString::Printf(TEXT("'%s' is not a designatable room function"), *RoomName.ToString());
		return false;
	}
	if (Row && IsGardenFunction(Row->Function))
	{
		// Load-bearing: agriculture treats Z != 0 planted keys as dome beds.
		OutReason = TEXT("Gardens grow underground or under a greenhouse dome, not in a habitat bay");
		return false;
	}
	if (!RoomName.IsNone() && FindModuleInBay(HabId, Bay))
	{
		OutReason = TEXT("A station module sits in this bay - remove it to zone the bay");
		return false;
	}
	if (!FurnitureAllowsRezone(FIntVector(0, Bay, HabId), RoomName, OutReason))
	{
		return false; // BM-4: furniture that can't stand in the new room
	}
	// Bed guard: never take the last bed from someone sleeping here (BM-4:
	// the habitat's beds without this bay, furnished bays counting theirs).
	const FRHRoomRow* OldRow = Defs->GetRoom(GetBayRoom(HabId, Bay));
	const bool bWasLiving = OldRow && OldRow->SliceActive && OldRow->Function == FName("Living");
	const bool bIsLiving = Row && Row->Function == FName("Living");
	if (bWasLiving && !bIsLiving && GetHabResidents(HabId) > CountHabBeds(HabId, Bay))
	{
		OutReason = FString::Printf(TEXT("%d residents sleep in this habitat - rezone after they move"), GetHabResidents(HabId));
		return false;
	}
	TArray<FName>& Rooms = HabBayRooms.FindOrAdd(HabId);
	if (Rooms.Num() < G.X * G.Y)
	{
		Rooms.SetNum(G.X * G.Y);
	}
	Rooms[Bay] = RoomName;
	++ZoningVersion; // C1: a re-zoned bay can change who has a bed
	if (!Row || !IsSeatFunction(Row->Function))
	{
		SeatPriority.Remove(FIntVector(0, Bay, HabId));
	}
	UE_LOG(LogRedHopeSim, Display, TEXT("Bay designation: habitat #%d bay %d -> %s"),
		HabId, Bay, RoomName.IsNone() ? TEXT("(cleared)") : *RoomName.ToString());
	return true;
}

void URHSimWorldSubsystem::RefreshJobs()
{
	// Deterministic seat assignment. Every open station in the default order -
	// floors shallow->deep, each floor's cells in carve order, then its prefab
	// modules in Id order - with the player's ASSIGNED stations moved to the
	// front in priority order (T2). Colonists fill seats by Id. SeatCount seats
	// per station; LabYieldSeatSum / WorkstationYieldSeatSum accumulate each
	// filled seat's YieldMul (station tiers). With no assignments, no modules
	// and instant fill, this is the pre-T1 walk exactly.
	static const FName NLab(TEXT("Lab")), NWorkstation(TEXT("Workstation")), NGarden(TEXT("Garden"));
	ColonistJobs.Reset();
	ColonistStations.Reset();
	StationFilled.Reset();
	StationCapacity.Reset();
	ColonistSeatPiece.Reset();
	LabYieldSeatSum = 0.0;
	WorkstationYieldSeatSum = 0.0;
	LastIdleColonists = 0;
	LastOpenSeats = 0;
	JobAssignNow.Reset();
	if (Colonists.Num() == 0 || !Defs)
	{
		ColonistBedPiece.Reset();
		bBedKeyValid = false;
		if (JobAssignLast.Num() > 0)
		{
			JobAssignLast.Reset();
			++JobsVersion; // C6a: the assignment changed (it is empty now)
		}
		return;
	}
	// C6a: the commute's work-time hook (b) needs each filled Lab / Workstation seat's yield per colonist. Recorded
	// only while the population ramp is open (it is 0 below CommuteRampStartPop, which is every small colony).
	const double CommuteRampNow = GetCommuteRamp();
	struct FCommuteSeat { int32 Id; bool bLab; double Yield; };
	TArray<FCommuteSeat> CommuteSeats;
	TArray<int32> ByIdOrder;
	ByIdOrder.Reserve(Colonists.Num());
	bool bByIdSorted = true;
	for (const FRHColonist& C : Colonists)
	{
		bByIdSorted &= ByIdOrder.Num() == 0 || ByIdOrder.Last() <= C.Id;
		ByIdOrder.Add(C.Id);
	}
	if (!bByIdSorted)
	{
		ByIdOrder.Sort(); // the roster is Id-ascending by construction; sorting a sorted list is a no-op
	}

	struct FStation { FIntVector Key; const FRHRoomRow* Row; };
	TArray<FStation> Stations;
	const auto IsSeatRow = [](const FRHRoomRow* Row)
	{
		return Row && Row->SliceActive && (Row->Function == NLab || Row->Function == NWorkstation || Row->Function == NGarden);
	};
	for (const FRHFloor& Floor : FloorTable) // D1: the floors that exist, shallowest first
	{
		const int32 Level = Floor.Level;
		ensure(Level >= -MaxDepth);
		if (!RatedFloors.Contains(Level))
		{
			continue;
		}
		if (const TArray<FName>* Cells = FloorRoomCells.Find(Level))
		{
			for (int32 i = 0; i < Cells->Num(); ++i)
			{
				const FRHRoomRow* Row = Defs->GetRoom((*Cells)[i]);
				if (IsSeatRow(Row))
				{
					Stations.Add({ FIntVector(Level, i, 0), Row });
				}
			}
		}
		for (const FRHBuildingInstance& B : Buildings)
		{
			if (B.Level == Level)
			{
				const FRHRoomRow* Row = GetProvidedRoom(B, /*bRequireOnline*/ true);
				if (IsSeatRow(Row))
				{
					Stations.Add({ FIntVector(Level, INDEX_NONE, B.Id), Row });
				}
			}
		}
	}
	// BM-1: rated surface networks after every floor - networks by root,
	// members by Id, bays in order, then the network's in-bay modules by Id.
	// No habs = no stations here (the pre-BM-1 walk exactly).
	for (const FRHHabNetCache& Net : HabNets)
	{
		if (!IsNetRated(Net))
		{
			continue;
		}
		for (const int32 HabId : Net.Members)
		{
			const FRHHabMemberCache& M = HabMembers.FindChecked(HabId);
			for (int32 Bay = 0; Bay < M.NX * M.NY; ++Bay)
			{
				const FRHRoomRow* Row = Defs->GetRoom(GetBayRoom(HabId, Bay));
				if (IsSeatRow(Row))
				{
					Stations.Add({ FIntVector(0, Bay, HabId), Row });
				}
			}
		}
		for (const int32 Index : Net.Modules)
		{
			const FRHBuildingInstance& B = Buildings[Index];
			const FRHRoomRow* Row = GetProvidedRoom(B, /*bRequireOnline*/ true);
			if (IsSeatRow(Row))
			{
				Stations.Add({ FIntVector(0, INDEX_NONE, B.Id), Row });
			}
		}
	}

	// Assigned stations first (newest assignment first), then the rest.
	TArray<int32> Order;
	for (const FIntVector& K : SeatPriority)
	{
		for (int32 i = 0; i < Stations.Num(); ++i)
		{
			if (Stations[i].Key == K && !Order.Contains(i))
			{
				Order.Add(i);
			}
		}
	}
	for (int32 i = 0; i < Stations.Num(); ++i)
	{
		if (!SeatPriority.Contains(Stations[i].Key))
		{
			Order.Add(i);
		}
	}

	// Lazy fill (T3): unassigned seats fill only up to StaffedSeats.
	const bool bLazy = LazySeatFillSols > 0.0;
	int32 Next = 0, NonPriorityFilled = 0, NonPrioritySeats = 0;
	for (const int32 Idx : Order)
	{
		const FStation& S = Stations[Idx];
		const bool bAssigned = SeatPriority.Contains(S.Key);
		// BM-4: job-seat furniture replaces the room's SeatCount (yield still
		// comes from the room row); each seat is a piece, in Id order.
		const FRHCellFurnish* CF = FurnishedCells.Find(S.Key);
		const bool bFurnishedSeats = CF && CF->SeatPieces > 0;
		const int32 Seats = bFurnishedSeats ? CF->JobSeats : FMath::Max(1, S.Row->SeatCount);
		TArray<int32> SeatIds;
		if (bFurnishedSeats)
		{
			for (const int32 PieceId : CF->Ids)
			{
				const FRHFurnitureInstance* FP = FindFurniture(PieceId);
				const FRHFurnitureRow* FR = FP ? Defs->GetFurniture(FP->Row) : nullptr;
				for (int32 k = 0; FR && FR->Provides == NAME_FurnJobSeat && k < FR->Capacity; ++k)
				{
					SeatIds.Add(PieceId);
				}
			}
		}
		StationCapacity.Add(S.Key, Seats);
		if (!bAssigned)
		{
			NonPrioritySeats += Seats;
		}
		for (int32 Seat = 0; Seat < Seats && Next < ByIdOrder.Num(); ++Seat)
		{
			if (!bAssigned && bLazy && NonPriorityFilled >= StaffedSeats)
			{
				break;
			}
			const int32 Id = ByIdOrder[Next++];
			ColonistJobs.Add(Id, S.Row->Function);
			ColonistStations.Add(Id, S.Key);
			JobAssignNow.Add({ Id, S.Key });
			if (SeatIds.IsValidIndex(Seat))
			{
				ColonistSeatPiece.Add(Id, SeatIds[Seat]);
			}
			++StationFilled.FindOrAdd(S.Key);
			NonPriorityFilled += bAssigned ? 0 : 1;
			if (S.Row->Function == NLab)
			{
				LabYieldSeatSum += FMath::Max(0.f, S.Row->YieldMul);
				if (CommuteRampNow > 0.0) { CommuteSeats.Add({ Id, true, (double)FMath::Max(0.f, S.Row->YieldMul) }); }
			}
			else if (S.Row->Function == NWorkstation)
			{
				WorkstationYieldSeatSum += FMath::Max(0.f, S.Row->YieldMul);
				if (CommuteRampNow > 0.0) { CommuteSeats.Add({ Id, false, (double)FMath::Max(0.f, S.Row->YieldMul) }); }
			}
		}
	}
	// C6a: the assignment as this call made it, against the last call's (RefreshJobs runs from nine call sites).
	// Any difference bumps JobsVersion, which the commute flows are keyed on.
	if (!(JobAssignNow == JobAssignLast))
	{
		JobAssignLast = JobAssignNow;
		++JobsVersion;
	}
	// C6a hook (b), Lab and Workstation: a long commuter's seat counts 1 - Ramp x (1 - CommuteWorkMul). Discovery reads
	// LabYieldSeatSum x the skill average and fabrication reads WorkstationYieldSeatSum alone, so cutting the SEAT SUM here
	// (and leaving the skill average uncut) cuts each exactly once. No branch is taken below the ramp or with nobody long.
	if (CommuteRampNow > 0.0 && CommuteSeats.Num() > 0)
	{
		const FRHCommuteFlows& CF = EnsureCommuteFlows();
		if (CF.LongCount > 0)
		{
			const double Cut = CommuteRampNow * (1.0 - CommuteWorkMul);
			for (const FCommuteSeat& Seat : CommuteSeats)
			{
				if (Algo::BinarySearch(CF.LongIds, Seat.Id) != INDEX_NONE)
				{
					(Seat.bLab ? LabYieldSeatSum : WorkstationYieldSeatSum) -= Seat.Yield * Cut;
				}
			}
		}
	}
	// Instant mode keeps StaffedSeats at the filled count, so arming lazy
	// mode mid-game evicts nobody; lazy mode cannot bank unused staffing.
	StaffedSeats = bLazy ? FMath::Min(StaffedSeats, NonPriorityFilled) : NonPriorityFilled;
	LastIdleColonists = ByIdOrder.Num() - Next;
	LastOpenSeats = NonPrioritySeats - NonPriorityFilled;
	// C1: who sleeps in which bed is a pure function of the roster, the furniture
	// and the zoning, so it is re-derived only when one of the three has moved
	// (it was an every-step O(N^2) before). No furniture = no pieces, as before.
	if (Furniture.Num() == 0)
	{
		ColonistBedPiece.Reset();
		bBedKeyValid = false;
	}
	else if (!bBedKeyValid || BedKeyColonists != ColonistSetVersion || BedKeyFurniture != FurnitureVersion || BedKeyZoning != ZoningVersion)
	{
		ColonistBedPiece.Reset();
		BuildBedAssignments(ByIdOrder);
		bBedKeyValid = true;
		BedKeyColonists = ColonistSetVersion;
		BedKeyFurniture = FurnitureVersion;
		BedKeyZoning = ZoningVersion;
	}
	else if (bVerifyCaches)
	{
		// The cache is about to be reused: derive the assignment again and compare.
		const TMap<int32, int32> Reused = ColonistBedPiece;
		ColonistBedPiece.Reset();
		BuildBedAssignments(ByIdOrder);
		bool bSame = Reused.Num() == ColonistBedPiece.Num();
		for (const TPair<int32, int32>& Piece : Reused)
		{
			const int32* Fresh = ColonistBedPiece.Find(Piece.Key);
			bSame &= Fresh && *Fresh == Piece.Value;
		}
		if (!bSame)
		{
			UE_LOG(LogRedHopeSim, Error, TEXT("RHVERIFY: the bed assignment cache is stale (%d reused, %d re-derived; colonists v%u, furniture v%u, zoning v%u)"),
				Reused.Num(), ColonistBedPiece.Num(), ColonistSetVersion, FurnitureVersion, ZoningVersion);
		}
	}
}

void URHSimWorldSubsystem::GetStationSeats(const FIntVector& Key, int32& OutFilled, int32& OutCapacity) const
{
	const int32* F = StationFilled.Find(Key);
	const int32* C = StationCapacity.Find(Key);
	OutFilled = F ? *F : 0;
	OutCapacity = C ? *C : 0;
}

const FRHRoomRow* URHSimWorldSubsystem::GetProvidedRoom(const FRHBuildingInstance& B, bool bRequireOnline) const
{
	const FRHBuildingRow* Def = Defs ? Defs->GetBuilding(B.DefName) : nullptr;
	if (!Def || Def->Pressurized || Def->ProvidesRoom.IsNone())
	{
		return nullptr; // BM-1: a shell's ProvidesRoom is its bays' fixed room, not a station
	}
	const FRHRoomRow* Row = Defs->GetRoom(Def->ProvidesRoom);
	if (!Row || !Row->SliceActive)
	{
		return nullptr;
	}
	if (bRequireOnline)
	{
		if (B.bUnderConstruction || B.bManualOff || !B.bPowered)
		{
			return nullptr;
		}
		// Rated here: its floor underground, or (BM-1) the habitat bay it stands in.
		const bool bRatedHere = B.Level < 0 ? RatedFloors.Contains(B.Level) : IsLocationInRatedHab(B.LocationCm);
		if (!bRatedHere)
		{
			return nullptr;
		}
	}
	return Row;
}

const FRHBuildingInstance* URHSimWorldSubsystem::FindModuleOnCell(int32 Level, int32 Cell) const
{
	for (const FRHBuildingInstance& B : Buildings)
	{
		if (B.Level == Level && Level < 0 && GetProvidedRoom(B, /*bRequireOnline*/ false) && FindCarvedCellAt(Level, B.LocationCm) == Cell)
		{
			return &B;
		}
	}
	return nullptr;
}

int32 URHSimWorldSubsystem::FindCarvedCellAt(int32 Level, const FVector& LocationCm) const
{
	const int32 Carved = GetFloorCarvedCells(Level);
	if (Level >= 0 || Carved <= 0)
	{
		return INDEX_NONE;
	}
	// Cell centres sit at the shaft head (region 0's origin) + the floor's lattice point x 10 m (the
	// visualizer's carve tiles use the same mapping). Measured from the head, never from world 0.
	return FindFloorCell(Level, CmToLattice(ShaftHeadCm, LocationCm));
}

double URHSimWorldSubsystem::GetFoodRunwaySols() const
{
	const double Draw = Colonists.Num() * ColonistFoodKgPerSol;
	return Draw > 0.0 ? GetStock(FName("Food")) / Draw : 1.0e9;
}

int32 URHSimWorldSubsystem::LivingCellWeight(const FIntVector& Key) const
{
	// BM-4: a Living cell with beds counts its beds, else 1 (the legacy rule).
	const FRHCellFurnish* CF = FurnishedCells.Find(Key);
	return (CF && CF->BedPieces > 0) ? CF->Beds : 1;
}

int32 URHSimWorldSubsystem::GetHousingComfortCapacity() const
{
	// What the Housing term divides the population by: the weight of every live
	// Living cell on a rated floor, and of every live Living bay in a rated surface
	// network. An integer sum, so no visiting order can matter. GetColonyHope reads
	// this same function.
	static const FName NLiving(TEXT("Living"));
	int32 Total = 0;
	for (const TPair<int32, TArray<FName>>& Pair : FloorRoomCells)
	{
		if (!RatedFloors.Contains(Pair.Key)) // rooms function only on certified floors
		{
			continue;
		}
		for (int32 i = 0; i < Pair.Value.Num(); ++i)
		{
			const FRHRoomRow* Row = Defs ? Defs->GetRoom(Pair.Value[i]) : nullptr;
			if (Row && Row->SliceActive && Row->Function == NLiving)
			{
				Total += LivingCellWeight(FIntVector(Pair.Key, i, 0));
			}
		}
	}
	for (const FRHHabNetCache& Net : HabNets)
	{
		if (!IsNetRated(Net))
		{
			continue;
		}
		for (const int32 HabId : Net.Members)
		{
			const FRHHabMemberCache& M = HabMembers.FindChecked(HabId);
			for (int32 Bay = 0; Bay < M.NX * M.NY; ++Bay)
			{
				const FRHRoomRow* Row = Defs ? Defs->GetRoom(GetBayRoom(HabId, Bay)) : nullptr;
				if (Row && Row->SliceActive && Row->Function == NLiving)
				{
					Total += LivingCellWeight(FIntVector(0, Bay, HabId));
				}
			}
		}
	}
	return Total;
}

URHSimWorldSubsystem::FRHHopeBreakdown URHSimWorldSubsystem::GetColonyHope() const
{
	// Pure derived read (approved M0 decision: Hope is an index, never a
	// stockpile). Components per the Gate-B slice; all weights DT_Config.
	FRHHopeBreakdown Out;
	Out.Base = HopeBase;
	if (bVaultRated)
	{
		Out.Milestones = HopeVaultMilestone;
	}
	if (bFirstBornAnnounced)
	{
		Out.Milestones += HopeFirstBornMilestone; // the first Martian, forever a source of hope
	}
	if (bFirstHarvestAnnounced)
	{
		Out.Milestones += HopeFirstHarvestMilestone; // food from OUR soil, forever
	}
	// Discoveries are permanent momentum (brief §5) - each uncovered row's
	// HopeBonus stays in the index forever. The log is tiny (an authored
	// handful), so the per-call lookups are cheap.
	for (const FName& Found : DiscoveryLog)
	{
		if (const FRHDiscoveryRow* Row = Defs ? Defs->GetDiscovery(Found) : nullptr)
		{
			Out.Milestones += Row->HopeBonus;
		}
	}

	const int32 Pop = Colonists.Num();
	// C2b: the Housing term's denominator is GetHousingComfortCapacity, the one helper
	// the settlers' cap shares (BM-4: a Living cell with beds counts its beds, else 1).
	const int32 LQCells = GetHousingComfortCapacity();
	static const FName NLiving(TEXT("Living")), NHallway(TEXT("Hallway"));
	// C1: a room row's refused tags, split once per call. They were re-split for
	// every candidate (emitter, refuser) pair - a string parse in a C^2 loop.
	// The reference RefusesOf returns is valid until the next call.
	TMap<const FRHRoomRow*, TArray<FString>> RefuseCache;
	TMap<const FRHRoomRow*, TArray<FString>> EmitCache; // C5b: the surface loop splits each emitting row once
	const auto RefusesOf = [&RefuseCache](const FRHRoomRow* Row) -> const TArray<FString>&
	{
		TArray<FString>* Found = RefuseCache.Find(Row);
		if (!Found)
		{
			TArray<FString> Tags;
			Row->RefusesTags.ParseIntoArray(Tags, TEXT(";"));
			Found = &RefuseCache.Add(Row, MoveTemp(Tags));
		}
		return *Found;
	};
	// C2a: the Rooms bonus is counted ONCE per room function colony-wide (it was
	// once per rated floor and surface network), scaled below by how well that
	// function's rooms cover the residents. Tallied in first-occurrence order -
	// shallowest rated floor first, then the networks by root - so the sum order
	// never depends on a map's visiting order. Adjacency is a share of refuser
	// cells over floors and networks together.
	struct FRoomTally { FName Function; double Weight = 0.0; int32 Count = 0; };
	TArray<FRoomTally> RoomTallies;
	const auto TallyRoom = [&RoomTallies](const FRHRoomRow* Row)
	{
		for (FRoomTally& T : RoomTallies)
		{
			if (T.Function == Row->Function)
			{
				++T.Count;
				return;
			}
		}
		FRoomTally Fresh;
		Fresh.Function = Row->Function;
		Fresh.Weight = Row->MoraleWeight; // the first row seen sets the weight, as it always did
		Fresh.Count = 1;
		RoomTallies.Add(Fresh);
	};
	int32 RefuserCells = 0, OffendedRefusers = 0;
	TArray<int32> RatedLevels;
	for (const auto& Pair : FloorRoomCells)
	{
		if (RatedFloors.Contains(Pair.Key)) // rooms function only on certified floors
		{
			RatedLevels.Add(Pair.Key);
		}
	}
	RatedLevels.Sort([](int32 A, int32 B) { return A > B; });
	for (const int32 Level : RatedLevels)
	{
		const TArray<FName>& Cells = FloorRoomCells.FindChecked(Level);
		// Each cell's live room row (null = none, or dormant), looked up once.
		TArray<const FRHRoomRow*> Rows;
		Rows.SetNumZeroed(Cells.Num());
		bool bAnyEmitter = false;
		for (int32 i = 0; i < Cells.Num(); ++i)
		{
			const FRHRoomRow* Row = Defs ? Defs->GetRoom(Cells[i]) : nullptr;
			if (!Row || !Row->SliceActive)
			{
				continue;
			}
			Rows[i] = Row;
			bAnyEmitter |= !Row->EmitsTags.IsEmpty();
			RefuserCells += Row->RefusesTags.IsEmpty() ? 0 : 1;
			if (Row->Function != NLiving && Row->MoraleWeight > 0.f) // Living is housing, counted by GetHousingComfortCapacity - never double-dipped
			{
				TallyRoom(Row);
			}
		}

		// Adjacency offenses (habitat vision §4): an emitter reaches refusers
		// within Manhattan distance 2. At distance 1 nothing fits between -
		// the penalty is architectural and permanent. At distance 2 a Hallway
		// cell directly between them, with the floor's circulation running,
		// cancels it (partition + filtration - the canonical cure).
		// C1: found by looking at each emitter's twelve neighbours on a dense
		// grid of the floor's spiral (it was every emitter x every cell, then a
		// third scan for the hallway - O(C^2) to O(C^3) with string splits). Each
		// (emitter, refuser) pair is still counted once; only the search changed.
		if (!bAnyEmitter)
		{
			continue;
		}
		// D1: the floor table owns where each cell is. NRooms is the room array's length (the carve count as of
		// the last designation: never longer than the floor, possibly shorter), so only the first NRooms cells exist
		// for this pass and both searches below see exactly that set.
		const int32 NRooms = Cells.Num();
		const FRHFloor* FloorHere = FindFloor(Level);
		TArray<FIntPoint> Pos;
		Pos.SetNumUninitialized(NRooms);
		for (int32 i = 0; i < NRooms; ++i)
		{
			Pos[i] = GetFloorCell(Level, i);
		}
		FIntPoint Lo = Pos[0], Hi = Pos[0];
		for (const FIntPoint& P : Pos)
		{
			Lo = FIntPoint(FMath::Min(Lo.X, P.X), FMath::Min(Lo.Y, P.Y));
			Hi = FIntPoint(FMath::Max(Hi.X, P.X), FMath::Max(Hi.Y, P.Y));
		}
		const int32 GridW = Hi.X - Lo.X + 1;
		const int64 BoxArea = (int64)GridW * (Hi.Y - Lo.Y + 1);
		// The dense grid when the bounding box is at most 8 x NRooms (every spiral floor: a spiral prefix's box is
		// under that); otherwise the table's own hash lookup, so a long painted tunnel never allocates a huge box.
		const bool bUseGrid = AdjacencyMode == 1 || (AdjacencyMode == 0 && BoxArea <= 8 * (int64)NRooms);
		if (!bUseGrid)
		{
			++AdjacencyFallbackRuns;
		}
		TArray<int32> Grid;
		if (bUseGrid)
		{
			Grid.Init(INDEX_NONE, (int32)BoxArea);
			for (int32 i = 0; i < NRooms; ++i)
			{
				Grid[(Pos[i].Y - Lo.Y) * GridW + (Pos[i].X - Lo.X)] = i;
			}
		}
		const auto CellAt = [&](const FIntPoint& P) -> int32
		{
			if (bUseGrid)
			{
				return (P.X < Lo.X || P.X > Hi.X || P.Y < Lo.Y || P.Y > Hi.Y) ? (int32)INDEX_NONE : Grid[(P.Y - Lo.Y) * GridW + (P.X - Lo.X)];
			}
			const int32 Ord = FloorHere ? FloorHere->FindOrdinal(P) : (int32)INDEX_NONE;
			return Ord >= NRooms ? (int32)INDEX_NONE : Ord; // a cell carved past the last designation is no room yet
		};
		static const FIntPoint Reach[12] = {
			FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1),
			FIntPoint(2, 0), FIntPoint(-2, 0), FIntPoint(0, 2), FIntPoint(0, -2),
			FIntPoint(1, 1), FIntPoint(1, -1), FIntPoint(-1, 1), FIntPoint(-1, -1) };
		bool bCircKnown = false, bCirculated = false;
		TArray<uint8> Offended; // per cell: some emitter offends this refuser (the share counts cells, once each)
		Offended.Init(0, Cells.Num());
		for (int32 E = 0; E < Cells.Num(); ++E)
		{
			const FRHRoomRow* ERow = Rows[E];
			if (!ERow || ERow->EmitsTags.IsEmpty())
			{
				continue;
			}
			TArray<FString> Emits;
			ERow->EmitsTags.ParseIntoArray(Emits, TEXT(";"));
			for (const FIntPoint& Off : Reach)
			{
				const int32 R = CellAt(Pos[E] + Off);
				if (R == INDEX_NONE || !Rows[R] || Rows[R]->RefusesTags.IsEmpty())
				{
					continue;
				}
				const TArray<FString>& Refuses = RefusesOf(Rows[R]);
				bool bOffends = false;
				for (const FString& Tag : Emits)
				{
					if (Refuses.Contains(Tag))
					{
						bOffends = true;
						break;
					}
				}
				if (!bOffends)
				{
					continue;
				}
				if (FMath::Abs(Off.X) + FMath::Abs(Off.Y) == 2)
				{
					if (!bCircKnown)
					{
						bCirculated = IsFloorCirculated(Level);
						bCircKnown = true;
					}
					if (bCirculated)
					{
						// A Hallway on any cell 4-adjacent to BOTH ends partitions
						// the pair (straight runs have one such cell, diagonals two).
						bool bPartitioned = false;
						for (int32 U = 0; U < 4 && !bPartitioned; ++U)
						{
							const int32 H = CellAt(Pos[E] + Reach[U]);
							if (H != INDEX_NONE && Rows[H] && Rows[H]->Function == NHallway)
							{
								const FIntPoint Gap = Pos[H] - Pos[R];
								bPartitioned = FMath::Abs(Gap.X) + FMath::Abs(Gap.Y) == 1;
							}
						}
						if (bPartitioned)
						{
							continue;
						}
					}
				}
				++Out.OffendedPairs;
				if (!Offended[R])
				{
					Offended[R] = 1;
					++OffendedRefusers;
				}
			}
		}
	}

	// BM-1: rated surface networks - the floor rules, per network. Bay
	// positions come from the world bay lattice; pairs never cross networks.
	// Station modules do not count toward Rooms (director Q9 open).
	for (const FRHHabNetCache& Net : HabNets)
	{
		if (!IsNetRated(Net))
		{
			continue;
		}
		TArray<const FRHRoomRow*> Rows;
		TArray<FIntPoint> Pos;
		TArray<FIntVector> BayKeys;
		for (const int32 HabId : Net.Members)
		{
			const FRHHabMemberCache& M = HabMembers.FindChecked(HabId);
			for (int32 Bay = 0; Bay < M.NX * M.NY; ++Bay)
			{
				const FRHRoomRow* Row = Defs ? Defs->GetRoom(GetBayRoom(HabId, Bay)) : nullptr;
				Rows.Add((Row && Row->SliceActive) ? Row : nullptr);
				Pos.Add(CellPos(FIntVector(0, Bay, HabId)));
				BayKeys.Add(FIntVector(0, Bay, HabId));
			}
		}
		for (int32 RI = 0; RI < Rows.Num(); ++RI)
		{
			const FRHRoomRow* Row = Rows[RI];
			if (!Row)
			{
				continue;
			}
			RefuserCells += Row->RefusesTags.IsEmpty() ? 0 : 1;
			if (Row->Function != NLiving && Row->MoraleWeight > 0.f)
			{
				TallyRoom(Row);
			}
		}
		TArray<uint8> Offended; // per bay, as on a floor
		Offended.Init(0, Rows.Num());
		const bool bCirc = IsNetCirculated(Net);
		// An emitter reaches the twelve cells within Manhattan distance two, as on a floor, so each looks them up in a
		// position index instead of comparing itself with every bay of its network (emitters x bays: a 500-bay network
		// with a hundred emitters cost 0.4 ms a read, C5b -scale). The pairs it finds are the same ones; only the search
		// changed, and a network with no emitter builds no index at all.
		bool bAnyEmitter = false;
		for (const FRHRoomRow* Row : Rows)
		{
			bAnyEmitter |= Row && !Row->EmitsTags.IsEmpty();
		}
		if (!bAnyEmitter)
		{
			continue;
		}
		TMap<FIntPoint, int32> IndexAt; // lookups only, never iterated
		IndexAt.Reserve(Rows.Num());
		for (int32 I = 0; I < Rows.Num(); ++I)
		{
			IndexAt.Add(Pos[I], I);
		}
		static const FIntPoint Reach[12] = {
			FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1),
			FIntPoint(2, 0), FIntPoint(-2, 0), FIntPoint(0, 2), FIntPoint(0, -2),
			FIntPoint(1, 1), FIntPoint(1, -1), FIntPoint(-1, 1), FIntPoint(-1, -1) };
		for (int32 E = 0; E < Rows.Num(); ++E)
		{
			if (!Rows[E] || Rows[E]->EmitsTags.IsEmpty())
			{
				continue;
			}
			TArray<FString>* Cached = EmitCache.Find(Rows[E]);
			if (!Cached)
			{
				TArray<FString> Tags;
				Rows[E]->EmitsTags.ParseIntoArray(Tags, TEXT(";"));
				Cached = &EmitCache.Add(Rows[E], MoveTemp(Tags));
			}
			const TArray<FString>& Emits = *Cached; // EmitCache is not added to inside this emitter's loop
			for (const FIntPoint& Off : Reach)
			{
				const int32* Found = IndexAt.Find(Pos[E] + Off);
				if (!Found)
				{
					continue;
				}
				const int32 R = *Found;
				if (!Rows[R] || Rows[R]->RefusesTags.IsEmpty())
				{
					continue;
				}
				const TArray<FString>& Refuses = RefusesOf(Rows[R]);
				bool bOffends = false;
				for (const FString& Tag : Emits)
				{
					bOffends |= Refuses.Contains(Tag);
				}
				if (!bOffends)
				{
					continue;
				}
				if (FMath::Abs(Off.X) + FMath::Abs(Off.Y) == 2 && bCirc)
				{
					// A Hallway on any bay 4-adjacent to BOTH ends partitions the pair (straight runs have one such bay,
					// diagonals two): the emitter's four neighbours that are Hallways and one step from the refuser.
					bool bPartitioned = false;
					for (int32 U = 0; U < 4 && !bPartitioned; ++U)
					{
						const int32* H = IndexAt.Find(Pos[E] + Reach[U]);
						if (H && Rows[*H] && Rows[*H]->Function == NHallway)
						{
							const FIntPoint Gap = Pos[*H] - Pos[R];
							bPartitioned = FMath::Abs(Gap.X) + FMath::Abs(Gap.Y) == 1;
						}
					}
					if (bPartitioned)
					{
						continue;
					}
				}
				++Out.OffendedPairs;
				if (!Offended[R])
				{
					Offended[R] = 1;
					++OffendedRefusers;
				}
			}
		}
	}

	// Rooms: each function's weight, once, times its coverage - one room of a type
	// serves RoomServesResidents people. A colony no bigger than one room serves
	// (and an empty one, which nobody needs a room for) takes the whole bonus.
	for (const FRoomTally& T : RoomTallies)
	{
		const double Coverage = Pop > 0 ? FMath::Min(1.0, (double)T.Count * RoomServesResidents / (double)Pop) : 1.0;
		Out.Rooms += T.Weight * HopePerMoralePoint * Coverage;
	}

	if (Pop > 0)
	{
		Out.Housing = HopeHousingMax * FMath::Min(1.0, (double)LQCells / (double)Pop);
	}
	Out.FilledSeats = ColonistJobs.Num();
	Out.Population = Pop;
	if (Pop > 0)
	{
		// C2a: the employment rate, not a head count of seats.
		Out.Jobs = HopeJobsMax * FMath::Min(1.0, (double)Out.FilledSeats / (double)Pop);
	}
	if (Pop > 0)
	{
		// Comforts lift scales with the supplied fraction; never negative.
		Out.Comforts = HopeLuxuryBonus * FMath::Clamp((double)ComfortsSupplied / (double)Pop, 0.0, 1.0);
	}
	// C2a: the comfort sums are per resident - the same sum x ComfortRefPop / pop,
	// then the same caps - so a crew of ComfortRefPop scores as before and a city
	// is not lifted by furnishing one room.
	const double PerHead = ComfortRefPop / (double)FMath::Max(Pop, 1);
	if (Pop > 0 && Furniture.Num() > 0)
	{
		double Raw = 0.0;
		GetFurnishingsHope(&Raw);
		Out.Furnishings = FMath::Min(Raw * PerHead, HopeFurnishingsMax); // BM-4: bonus-only, capped
	}
	if (Pop > 0 && WallFeatures.Num() > 0)
	{
		double Raw = 0.0;
		GetViewsHope(&Raw);
		Out.Views = FMath::Min(Raw * PerHead, HopeViewsMax); // BM-6: windows, bonus-only, capped
	}
	// C2a: adjacency is the share of refuser cells an emitter offends (pairs stay
	// counted for the layout hint); unsupported is the share of the colony short of
	// a full ration, steep so one in three on nothing costs the whole line.
	Out.OffendedRefusers = OffendedRefusers;
	Out.RefuserCells = RefuserCells;
	if (RefuserCells > 0)
	{
		Out.AdjacencyPenalty = HopeAdjacencyMax * (double)OffendedRefusers / (double)RefuserCells;
	}
	Out.DeficitEquivalent = DeficitEquivalent;
	if (Pop > 0 && DeficitEquivalent > 0.0)
	{
		Out.UnsupportedPenalty = HopeUnsupportedMax * FMath::Min(1.0, HopeUnsupportedSteepness * DeficitEquivalent / (double)Pop);
	}
	// Water quality (M2 Gate D+): grim recycled water saps morale below the
	// potability floor - the first scarcity-driven Hope input. Scales with how
	// far below the floor; zero at/above it (fresh ice keeps it painless).
	if (Pop > 0 && WaterPotability < WaterPotabilityFloor && WaterPotabilityFloor > 0.0)
	{
		Out.WaterPenalty = HopeWaterPenalty * (WaterPotabilityFloor - WaterPotability) / WaterPotabilityFloor;
	}
	// Food runway (D4a): a larder under FoodRunwayFloorSols at today's draw weighs
	// on the crew - linear from 0 at the floor to HopeFoodPenalty at an empty store
	// (the water line's shape; a soft cap, never accelerating). Gross stock / draw,
	// so the value is a pure function of a linearly integrated stock: same parity
	// class as WaterPenalty.
	if (Pop > 0 && FoodRunwayFloorSols > 0.0)
	{
		const double Runway = GetFoodRunwaySols();
		if (Runway < FoodRunwayFloorSols)
		{
			Out.FoodPenalty = HopeFoodPenalty * (FoodRunwayFloorSols - FMath::Max(0.0, Runway)) / FoodRunwayFloorSols;
		}
	}
	// Solidarity (M3 Gate C): the fading morale shock from the last Dilemma
	// choice (signed - lifts after Defy, weighs after Comply). Only meaningful
	// once there's a crew to feel it.
	if (Pop > 0)
	{
		Out.Solidarity = SolidarityHope;
		// M4 Gate D: an active crisis weighs on morale while it runs (Malfunction
		// harder than Environmental). Cleared when the crisis ends.
		if (ActiveCrisis == ERHCrisis::Malfunction)      { Out.CrisisPenalty = CrisisMalfunctionHope; }
		else if (ActiveCrisis == ERHCrisis::Environmental) { Out.CrisisPenalty = CrisisEnvironmentHope; }
	}
	// C6a: commuting. Zero (and +0.0) below the population ramp or with nobody on a long trip, so Total is
	// bit-identical to v39 there: x - 0.0 == x for every finite double.
	{
		const FRHCommuteSummary Commute = GetCommuteSummary();
		Out.CommutePenalty = Commute.Penalty;
		Out.Commuters = Commute.Commuters;
		Out.LongCommuters = Commute.LongCommuters;
	}
	Out.Total = FMath::Clamp(
		Out.Base + Out.Housing + Out.Rooms + Out.Jobs + Out.Milestones + Out.Comforts + Out.Furnishings + Out.Views + Out.Solidarity
		- Out.AdjacencyPenalty - Out.UnsupportedPenalty - Out.WaterPenalty - Out.FoodPenalty - Out.CrisisPenalty - Out.CommutePenalty, 0.0, 100.0);
	return Out;
}

void URHSimWorldSubsystem::StepHope(float SubDt)
{
	// Low-pass the instantaneous index into the colony's MOOD. The exp form
	// integrates identically for ANY step size, so an agent sub-step and a 60x
	// era step converge to the same HopeSmoothed - the parity property a naive
	// linear lerp would break. dt is in sols to match Tau's units.
	const double DtSols = SubDt / (double)URHSimClockSubsystem::SolLengthSimSeconds;
	const double Instant = GetColonyHope().Total;
	const double Alpha = 1.0 - FMath::Exp(-DtSols / FMath::Max(HopeSmoothTauSols, KINDA_SMALL_NUMBER));
	HopeSmoothed += (Instant - HopeSmoothed) * Alpha;
	UpdateHopeBand();
	NoteCommuteAlert(); // C6a: one alert when the long share first crosses CommuteAlertShare (needs Long > 0)
}

void URHSimWorldSubsystem::UpdateHopeBand()
{
	// Rise past the up-thresholds, fall past the (lower) down-thresholds; the
	// gap between is the hysteresis that stops the band name from twitching.
	int32 B = (int32)HopeBand;
	while (B < 4 && HopeSmoothed >= HopeBandUp[B]) { ++B; }
	while (B > 0 && HopeSmoothed <  HopeBandDown[B - 1]) { --B; }
	HopeBand = (ERHHopeBand)B;
}

bool URHSimWorldSubsystem::IsGrowthEligible() const
{
	// A thriving colony grows only when it can actually WELCOME a new person:
	// smoothed Hope over the threshold, a free certified bed, and a genuine food
	// buffer (you don't birth into a famine). All three, or the streak stalls.
	if (Colonists.Num() == 0 || HopeSmoothed < HopeGrowthThreshold || GetFreeHousing() < 1)
	{
		return false;
	}
	const double FoodBuffer = GetStock(FName("Food"));
	const double DailyDraw = Colonists.Num() * ColonistFoodKgPerSol;
	return DailyDraw <= 0.0 || FoodBuffer >= DailyDraw * HopeGrowthFoodBufferSols;
}

double URHSimWorldSubsystem::GetGrowthProgress() const
{
	if (HopeGrowthIntervalSols <= 0.0 || !IsGrowthEligible())
	{
		return 0.0;
	}
	return FMath::Clamp(HopeGrowthStreakSols / HopeGrowthIntervalSols, 0.0, 1.0);
}

void URHSimWorldSubsystem::StepGrowth(float SubDt)
{
	// Zero-pop colonies never grow: the streak stays 0 and this is a no-op, so
	// the pre-crew baseline is untouched. Threshold on a MONOTONE accumulator =
	// era-parity-safe (the crossing sol is identical in both bands).
	const double DtSols = SubDt / (double)URHSimClockSubsystem::SolLengthSimSeconds;
	if (!IsGrowthEligible())
	{
		HopeGrowthStreakSols = 0.0; // conditions lapsed: momentum resets (earned, not banked)
		return;
	}
	// C2b: births per head. The streak runs at max(1, N / BirthRefPop) x real time, so
	// a colony of BirthRefPop or fewer grows exactly as it always did (the factor is
	// exactly 1.0) and a city of ten times that gets ten times the births. The gates
	// above are untouched; only the rate scales. Linear in dt, so both bands agree.
	const double PerHead = FMath::Max(1.0, (double)Colonists.Num() / BirthRefPop);
	HopeGrowthStreakSols += DtSols * PerHead;
	if (HopeGrowthStreakSols < HopeGrowthIntervalSols)
	{
		return;
	}
	// A big step in a big city can owe more than one birth (an era step of a city of
	// thousands); a normal step owes exactly one, as before.
	const int32 Due = HopeGrowthIntervalSols > 0.0 ? FMath::Max(1, FMath::FloorToInt(HopeGrowthStreakSols / HopeGrowthIntervalSols)) : 1;
	for (int32 Birth = 0; Birth < Due; ++Birth)
	{
		HopeGrowthStreakSols -= HopeGrowthIntervalSols;

		// A birth: one colonist via the existing certified-housing path (respects
		// the bed gate; if the last bed vanished this step, Debug_AddColonists no-
		// ops and the streak simply re-accrues next eligible window).
		const int32 Before = Colonists.Num();
		Debug_AddColonists(1);
		if (Colonists.Num() <= Before)
		{
			return;
		}
		++BirthsOnMars;
		Colonists.Last().bMarsBorn = true; // C2a: the Mars-born are the last to be recalled in a shortage
		const FString Name = Colonists.Last().Name;
		if (!bFirstBornAnnounced)
		{
			// The brief's landmark beat: the first human born on Mars. A one-time
			// Hope surge (folded into GetColonyHope) and the seed of the M3 identity
			// axis. Wording is a Gate-D framing-review placeholder (celebration, not
			// clinical). Never retracts.
			bFirstBornAnnounced = true;
			OnAlert.Broadcast(FString::Printf(
				TEXT("THE FIRST MARTIAN IS BORN — %s, the first human native to Mars. The colony is no longer a mission. It is a home."), *Name));
			UE_LOG(LogRedHopeSim, Display, TEXT("=== FIRST MARTIAN-BORN: %s ==="), *Name);
		}
		else if ((double)Before <= BirthRefPop)
		{
			OnAlert.Broadcast(FString::Printf(TEXT("A CHILD IS BORN — %s joins the colony (%d born on Mars)."), *Name, BirthsOnMars));
			UE_LOG(LogRedHopeSim, Display, TEXT("=== MARTIAN-BORN: %s (%d total) ==="), *Name, BirthsOnMars);
		}
		else
		{
			// A city's births are a rate, not a news item (C2b, one alert per event,
			// never one per person): a banner on every tenth, the log on all. Gate-D
			// placeholder copy.
			if (BirthsOnMars % 10 == 0)
			{
				OnAlert.Broadcast(FString::Printf(TEXT("THE COLONY GROWS — %d children born on Mars so far."), BirthsOnMars));
			}
			UE_LOG(LogRedHopeSim, Display, TEXT("=== MARTIAN-BORN: %s (%d total) ==="), *Name, BirthsOnMars);
		}
	}
}

int32 URHSimWorldSubsystem::GetImmigrationCount(double Hope, int32 Pop, int32 FreeBeds, int32 ComfortCapacity) const
{
	// floor((Hope - Floor) / (100 - Floor) x max(Base, PerCapita x N)), capped at the
	// per-ship cap, at the free beds less ImmigrationReserveBeds (settlers never take
	// the last bed(s), so a Mars-born child always has one: births come first) and at
	// the comfortable room left, ComfortCapacity - N: settlers come only while the
	// Housing term can still serve everyone at full value, and never push it down.
	// A colony with nobody in it has no
	// reputation to draw on (and the pre-crew baselines stay untouched).
	// C5a: the same arithmetic in two halves (the city's Housing demand reads both).
	if (Pop <= 0 || FreeBeds <= 0 || Hope < ImmigrationHopeMin)
	{
		return 0;
	}
	return FMath::Min(GetImmigrationRaw(Hope, Pop), GetImmigrationRoom(Pop, FreeBeds, ComfortCapacity));
}

int32 URHSimWorldSubsystem::GetImmigrationRaw(double Hope, int32 Pop) const
{
	// What a ship would bring if room allowed: floor((Hope - Floor) / (100 - Floor) x
	// max(Base, PerCapita x N)), capped at the per-ship cap; 0 below the Hope minimum or
	// with nobody here.
	if (Pop <= 0 || Hope < ImmigrationHopeMin)
	{
		return 0;
	}
	const double Span = FMath::Max(100.0 - ImmigrationHopeFloor, (double)KINDA_SMALL_NUMBER);
	const double Draw = FMath::Max(ImmigrationBase, ImmigrationPerCapita * (double)Pop);
	const double Raw = FMath::Clamp(Hope - ImmigrationHopeFloor, 0.0, Span) * Draw / Span;
	return FMath::Clamp(FMath::FloorToInt(Raw), 0, FMath::Max(0, ImmigrationCapPerShip));
}

int32 URHSimWorldSubsystem::GetImmigrationRoom(int32 Pop, int32 FreeBeds, int32 ComfortCapacity) const
{
	// Settlers never take the last bed(s) (births come first) and never push the Housing
	// term below its maximum: min(FreeBeds - ReserveBeds, ComfortCapacity - Pop), floored at 0.
	return FMath::Min(FMath::Max(0, FreeBeds - ImmigrationReserveBeds), FMath::Max(0, ComfortCapacity - Pop));
}

int32 URHSimWorldSubsystem::ApplyHomeAgain()
{
	// F2b (Gate-D review, 2026-10-21): the Mars-born recalled to orbit come home first. On a landing
	// ship, while every home is on a full ration and a bed is free, they return in the order they
	// were recalled, with their own names, ahead of any settler and with no Hope gate. One alert for
	// the whole group. Gate-D placeholder copy.
	if (RecalledMarsBorn.Num() == 0 || GetFreeHousing() < 1)
	{
		return 0;
	}
	for (const FRHHomeRation& Home : HomeRations)
	{
		if (Home.Fraction < 1.0)
		{
			return 0;
		}
	}
	const int32 Before = Colonists.Num();
	const int32 Housed = AddColonists(RecalledMarsBorn.Num(), &RecalledMarsBorn);
	if (Housed <= 0)
	{
		return 0;
	}
	RecalledMarsBorn.RemoveAt(0, Housed);
	OnAlert.Broadcast(FString::Printf(TEXT("HOME AGAIN: %d born on Mars %s back with their colony."),
		Housed, Housed == 1 ? TEXT("is") : TEXT("are")));
	UE_LOG(LogRedHopeSim, Display, TEXT("=== HOME AGAIN: %d Mars-born back from orbit with the supply ship (population %d -> %d, %d still waiting) ==="),
		Housed, Before, Colonists.Num(), RecalledMarsBorn.Num());
	return Housed;
}

int32 URHSimWorldSubsystem::ApplyImmigration()
{
	// Q5a: a thriving city draws settlers on every supply ship. They are not Mars-
	// born, and they are housed the way pods are (Debug_AddColonists's fill order).
	// One alert for the whole group, never one per person. Gate-D placeholder copy.
	const int32 Wanted = GetImmigrationCount(HopeSmoothed, Colonists.Num(), GetFreeHousing(), GetHousingComfortCapacity());
	if (Wanted <= 0)
	{
		return 0;
	}
	// C5d: a ship brings no more settlers than the colony's air, water and food can carry (the old lean
	// colony took 100 people into an oxygen supply of 30 kg a sol against a draw of 81).
	const int32 Want = FMath::Min(Wanted, GetSettlerSupportRoom());
	if (Want < Wanted)
	{
		UE_LOG(LogRedHopeSim, Display, TEXT("=== IMMIGRATION held by life support: %d settler%s wanted, %d carried (oxygen %.1f kg/sol made vs %.1f drawn at the present population; water %.1f vs %.1f; food %.1f vs %.1f) ==="),
			Wanted, Wanted == 1 ? TEXT("") : TEXT("s"), Want,
			GetProductionPerSol(FName("Oxygen")), GetSupportLeg(FName("Oxygen"), Colonists.Num()).DrawPerSol,
			GetProductionPerSol(FName("Water")), GetSupportLeg(FName("Water"), Colonists.Num()).DrawPerSol,
			GetProductionPerSol(FName("Food")), GetSupportLeg(FName("Food"), Colonists.Num()).DrawPerSol);
	}
	if (Want <= 0)
	{
		return 0;
	}
	const int32 Before = Colonists.Num();
	const int32 Housed = Debug_AddColonists(Want);
	if (Housed <= 0)
	{
		return 0;
	}
	ImmigrantsTotal += Housed;
	OnAlert.Broadcast(FString::Printf(TEXT("%d settler%s arrived with the supply ship, drawn by the colony's reputation."),
		Housed, Housed == 1 ? TEXT("") : TEXT("s")));
	UE_LOG(LogRedHopeSim, Display, TEXT("=== IMMIGRATION: %d settler%s with the supply ship (smoothed Hope %.1f, population %d -> %d, %d settlers in all) ==="),
		Housed, Housed == 1 ? TEXT("") : TEXT("s"), HopeSmoothed, Before, Colonists.Num(), ImmigrantsTotal);
	return Housed;
}

void URHSimWorldSubsystem::StepBigArrival(float SubDt)
{
	// Q7a, once: sols with smoothed Hope at BigArrivalHopeMin or better are streaked
	// (the streak resets the moment Hope drops below); at BigArrivalStreakSols of
	// them, with BigArrivalFreeBeds free, a wave of BigArrivalColonists lands at once
	// with provisions, as a transport would carry. A threshold on a monotone
	// accumulator, linear in dt: both bands cross on the same step window. The
	// CEO's own landing belongs to the politics step - this only announces it.
	if (bBigArrivalDone)
	{
		return;
	}
	if (Colonists.Num() == 0 || HopeSmoothed < BigArrivalHopeMin)
	{
		BigArrivalStreakSols = 0.0;
		return;
	}
	BigArrivalStreakSols += SubDt / (double)URHSimClockSubsystem::SolLengthSimSeconds;
	if (BigArrivalStreakSols < BigArrivalStreakSolsNeeded || BigArrivalColonists <= 0 || GetFreeHousing() < BigArrivalFreeBeds)
	{
		return;
	}
	// C5c: the colony must be able to carry N + the wave. The streak keeps running; the wave waits
	// for air, food and water (the first lean playtest landed 100 into an air that could not carry
	// them, and 44 were recalled).
	if (!GetBigArrivalSupport().AllOk())
	{
		return;
	}
	const int32 Before = Colonists.Num();
	const int32 Housed = Debug_AddColonists(BigArrivalColonists);
	if (Housed <= 0)
	{
		return;
	}
	bBigArrivalDone = true;
	const double FoodKg = ColonistTransportFoodKgPerColonist * Housed;
	AddStock(FName("Food"), FoodKg);
	// Gate-D placeholder copy.
	OnAlert.Broadcast(FString::Printf(
		TEXT("THE BIG ARRIVAL — %d settlers land at once: the colony held its Hope long enough to be trusted with a city. They bring %.0f kg of provisions."),
		Housed, FoodKg));
	OnAlert.Broadcast(TEXT("THE CEO HAS ANNOUNCED HE WILL COME TO MARS WITH THE NEXT WAVE."));
	UE_LOG(LogRedHopeSim, Display, TEXT("=== BIG ARRIVAL (Sol %d): %d settlers, +%.0f kg Food (smoothed Hope %.1f, streak %.1f sols, population %d -> %d); the CEO will come with the next wave ==="),
		Clock ? Clock->GetSol() : 0, Housed, FoodKg, HopeSmoothed, BigArrivalStreakSols, Before, Colonists.Num());
}

URHSimWorldSubsystem::FRHBigArrivalSupport URHSimWorldSubsystem::GetBigArrivalSupport() const
{
	FRHBigArrivalSupport Out;
	Out.Wave = BigArrivalColonists;
	Out.People = Colonists.Num() + BigArrivalColonists;
	Out.FloorSols = FoodRunwayFloorSols;
	// Air: the pool the growth reserve reads, against everyone's breathing for BigArrivalLifeSupportSols.
	Out.O2HeldKg = GetGrowthHoldingsKg(FName("Oxygen"));
	Out.O2NeedKg = (double)Out.People * ColonistO2KgPerSol * BigArrivalLifeSupportSols;
	Out.bO2Ok = Out.O2HeldKg >= Out.O2NeedKg;
	// Food: the larder plus what the wave lands with, over N + wave's draw, as GetFoodRunwaySols reads it.
	const double FoodDraw = (double)Out.People * ColonistFoodKgPerSol;
	const double FoodKg = GetStock(FName("Food")) + ColonistTransportFoodKgPerColonist * (double)BigArrivalColonists;
	Out.FoodRunwaySols = FoodDraw > 0.0 ? FoodKg / FoodDraw : 1.0e9;
	Out.bFoodOk = Out.FoodRunwaySols >= FoodRunwayFloorSols;
	// Water: the wave brings none, so it is the pool against N + wave's draw.
	const double WaterDraw = (double)Out.People * ColonistWaterKgPerSol;
	Out.WaterRunwaySols = WaterDraw > 0.0 ? GetStock(FName("Water")) / WaterDraw : 1.0e9;
	Out.bWaterOk = Out.WaterRunwaySols >= FoodRunwayFloorSols;
	// C5d: and what the colony MAKES against what N + wave would draw (the wave's provisions count as food stock).
	Out.O2Leg = GetSupportLeg(FName("Oxygen"), Out.People);
	Out.FoodLeg = GetSupportLeg(FName("Food"), Out.People, ColonistTransportFoodKgPerColonist * (double)BigArrivalColonists);
	Out.WaterLeg = GetSupportLeg(FName("Water"), Out.People);
	return Out;
}

// --- C5d: production and the life-support books ---------------------------------------------

int32 URHSimWorldSubsystem::ProductionLeg(FName Resource)
{
	static const FName NOxygen(TEXT("Oxygen")), NWater(TEXT("Water")), NFood(TEXT("Food"));
	return Resource == NOxygen ? 0 : (Resource == NWater ? 1 : (Resource == NFood ? 2 : INDEX_NONE));
}

void URHSimWorldSubsystem::NoteProduced(FName Resource, double Kg)
{
	const int32 Leg = ProductionLeg(Resource);
	if (Leg != INDEX_NONE && Kg > 0.0)
	{
		ProdAccKg[Leg] += Kg;
	}
}

void URHSimWorldSubsystem::RollProductionLedger(int32 SolNow)
{
	// A threshold on the monotone clock, so the agent band and the era band roll on the same step window. The
	// open sol's tally goes into the ring; any further sols skipped in one step (a long jump) are recorded as
	// empty - at most a full ring's worth of them.
	if (SolNow <= ProductionCheckedSol)
	{
		return;
	}
	const int32 Passed = FMath::Min(SolNow - ProductionCheckedSol, ProdMaxWindow);
	ProductionCheckedSol = SolNow;
	for (int32 i = 0; i < Passed; ++i)
	{
		for (int32 Leg = 0; Leg < ProdLegs; ++Leg)
		{
			ProdRing[Leg][ProdRingHead] = i == 0 ? ProdAccKg[Leg] : 0.0;
		}
		ProdRingHead = (ProdRingHead + 1) % ProdMaxWindow;
		ProdRingFilled = FMath::Min(ProdRingFilled + 1, ProdMaxWindow);
	}
	for (int32 Leg = 0; Leg < ProdLegs; ++Leg)
	{
		ProdAccKg[Leg] = 0.0;
	}
}

double URHSimWorldSubsystem::GetProductionPerSol(FName Resource) const
{
	const int32 Leg = ProductionLeg(Resource);
	const int32 N = FMath::Min(ProdRingFilled, ProductionWindowSols);
	if (Leg == INDEX_NONE || N <= 0)
	{
		return 0.0;
	}
	double Sum = 0.0;
	for (int32 i = 1; i <= N; ++i)
	{
		Sum += ProdRing[Leg][(ProdRingHead - i + 2 * ProdMaxWindow) % ProdMaxWindow];
	}
	return Sum / (double)N;
}

void URHSimWorldSubsystem::Debug_SetProductionPerSol(FName Resource, double KgPerSol)
{
	const int32 Leg = ProductionLeg(Resource);
	if (Leg == INDEX_NONE)
	{
		return;
	}
	for (int32 i = 0; i < ProdMaxWindow; ++i)
	{
		ProdRing[Leg][i] = FMath::Max(0.0, KgPerSol);
	}
	ProdRingFilled = ProdMaxWindow; // the whole window is "known"; the other legs keep what they had
}

double URHSimWorldSubsystem::GetStandingO2LeakKgPerSol() const
{
	double Leak = 0.0;
	for (const int32 Level : RatedFloors)
	{
		Leak += (double)GetFloorCarvedCells(Level) * O2LeakKgPerCellPerSol;
	}
	for (const FRHHabNetCache& Net : HabNets)
	{
		if (IsNetRated(Net))
		{
			Leak += GetHabNetworkLeakKgPerSol(Net.Root);
		}
	}
	return Leak;
}

URHSimWorldSubsystem::FRHSupportLeg URHSimWorldSubsystem::GetSupportLeg(FName Resource, int32 People, double ExtraStockKg) const
{
	FRHSupportLeg Leg;
	Leg.Resource = Resource;
	Leg.ProductionPerSol = GetProductionPerSol(Resource);
	const double Heads = (double)FMath::Max(0, People);
	if (Resource == FName("Oxygen"))
	{
		Leg.DrawPerSol = Heads * ColonistO2KgPerSol + GetStandingO2LeakKgPerSol();
		Leg.StockKg = GetGrowthHoldingsKg(Resource);
	}
	else if (Resource == FName("Water"))
	{
		Leg.DrawPerSol = Heads * ColonistWaterKgPerSol * (1.0 - FMath::Clamp(GreywaterReturnFraction, 0.0, 1.0)); // the recycling returns most of it
		Leg.StockKg = GetStock(Resource);
	}
	else
	{
		Leg.DrawPerSol = Heads * ColonistFoodKgPerSol;
		Leg.StockKg = GetStock(Resource);
	}
	Leg.StockKg += ExtraStockKg;
	const double Deficit = -Leg.NetPerSol();
	Leg.CoverSols = Deficit > 0.0 ? Leg.StockKg / Deficit : 1.0e9;
	Leg.bOk = Deficit <= 0.0 || Leg.StockKg >= LifeSupportDeficitCoverSols * Deficit;
	return Leg;
}

URHSimWorldSubsystem::FRHSettlerLegRoom URHSimWorldSubsystem::GetSettlerLegRoom(FName Resource) const
{
	// C5e: settlers come only on NET PRODUCTION, and the stock must back them too. Per leg, the most k with
	//   production >= (N + k) x PerHead + Fixed          (net >= 0 after they land: no stock-cover allowance)
	//   stock check at N + k                             (C5c: the O2 pool >= (N + k) x O2 x BigArrivalLifeSupportSols;
	//                                                     the Food / Water runway at N + k >= FoodRunwayFloorSols)
	// Solved in closed form for N + k as a head count, then floored and taken off N. The 1e-9 head keeps a
	// production that equals the draw exactly from losing a person to rounding.
	FRHSettlerLegRoom Out;
	const int32 Pop = Colonists.Num();
	const FRHSupportLeg Leg = GetSupportLeg(Resource, Pop);
	double PerHead = 0.0, Fixed = 0.0, StockPerHead = 0.0;
	if (Resource == FName("Oxygen"))
	{
		PerHead = ColonistO2KgPerSol;
		Fixed = GetStandingO2LeakKgPerSol();
		StockPerHead = ColonistO2KgPerSol * BigArrivalLifeSupportSols;
	}
	else if (Resource == FName("Water"))
	{
		PerHead = ColonistWaterKgPerSol * (1.0 - FMath::Clamp(GreywaterReturnFraction, 0.0, 1.0)); // the recycling returns most of it
		StockPerHead = ColonistWaterKgPerSol * FoodRunwayFloorSols; // the runway counts the whole draw, as GetBigArrivalSupport reads it
	}
	else if (Resource == FName("Food"))
	{
		PerHead = ColonistFoodKgPerSol;
		StockPerHead = ColonistFoodKgPerSol * FoodRunwayFloorSols;
	}
	const auto RoomFor = [Pop](double Heads) -> int32
	{
		const double K = FMath::FloorToDouble(Heads + 1.0e-9) - (double)Pop;
		return K <= 0.0 ? 0 : (int32)FMath::Min(K, 1.0e6);
	};
	if (PerHead > 0.0)
	{
		Out.ProductionRoom = RoomFor((Leg.ProductionPerSol - Fixed) / PerHead);
	}
	if (StockPerHead > 0.0)
	{
		Out.StockRoom = RoomFor(Leg.StockKg / StockPerHead);
	}
	return Out;
}

int32 URHSimWorldSubsystem::GetSettlerSupportRoom() const
{
	int32 Room = MAX_int32;
	for (const FName Resource : { FName("Oxygen"), FName("Water"), FName("Food") })
	{
		Room = FMath::Min(Room, GetSettlerLegRoom(Resource).Room());
	}
	return Room;
}

void URHSimWorldSubsystem::StepMoveHome(float SubDt)
{
	// One pass per MoveHomeIntervalSols (a sol): an accumulator with a threshold, as
	// the other slow clocks are. 0 turns it off.
	if (MoveHomeIntervalSols <= 0.0)
	{
		MoveHomeAccSols = 0.0;
		return;
	}
	MoveHomeAccSols += SubDt / (double)URHSimClockSubsystem::SolLengthSimSeconds;
	if (MoveHomeAccSols < MoveHomeIntervalSols)
	{
		return;
	}
	MoveHomeAccSols -= MoveHomeIntervalSols;
	RehomeResidents();
}

int32 URHSimWorldSubsystem::RehomeResidents()
{
	// The minimal correctness rule: a colonist whose home no longer has a bed for
	// them moves to a free bed elsewhere. A home is out of beds for its residents
	// when its floor lost its rating, its beds were sold or demolished, or it
	// holds more residents than beds. In a home with fewer beds than residents the
	// oldest hands keep theirs and the newest (highest Id) are the ones displaced.
	// The displaced are placed highest Id first by the arrival fill order. One
	// pass over the roster, one over the homes, one over the displaced: O(N). No
	// free bed anywhere: they stay, and the shortage and recall rules take over.
	const int32 Pop = Colonists.Num();
	if (Pop == 0)
	{
		return 0;
	}
	TMap<int32, TArray<int32>> FloorResidents, HabResidents; // home -> roster indices (lookup only, never iterated for an outcome)
	for (int32 i = 0; i < Pop; ++i)
	{
		const FRHColonist& C = Colonists[i];
		if (C.HomeHab != 0)
		{
			HabResidents.FindOrAdd(C.HomeHab).Add(i);
		}
		else
		{
			FloorResidents.FindOrAdd(C.HomeLevel).Add(i);
		}
	}
	TArray<int32> Displaced; // roster indices
	const auto Displace = [&](TArray<int32>& Residents, int32 Beds)
	{
		if (Residents.Num() <= Beds)
		{
			return;
		}
		Residents.Sort([this](int32 A, int32 B) { return Colonists[A].Id < Colonists[B].Id; });
		for (int32 k = FMath::Max(Beds, 0); k < Residents.Num(); ++k)
		{
			Displaced.Add(Residents[k]);
		}
	};
	for (TPair<int32, TArray<int32>>& Home : FloorResidents)
	{
		Displace(Home.Value, RatedFloors.Contains(Home.Key) ? CountFloorBeds(Home.Key, nullptr) : 0);
	}
	for (TPair<int32, TArray<int32>>& Home : HabResidents)
	{
		Displace(Home.Value, (bVaultRated && IsHabRated(Home.Key)) ? GetHabBeds(Home.Key) : 0);
	}
	if (Displaced.Num() == 0)
	{
		return 0;
	}
	Displaced.Sort([this](int32 A, int32 B) { return Colonists[A].Id > Colonists[B].Id; });
	TSet<int32> DisplacedIds;
	DisplacedIds.Reserve(Displaced.Num());
	for (const int32 Index : Displaced)
	{
		DisplacedIds.Add(Colonists[Index].Id);
	}
	FRHFillState Fill;
	InitFillState(Fill, &DisplacedIds);
	int32 Moved = 0;
	for (const int32 Index : Displaced)
	{
		int32 Level = 0, Hab = 0;
		if (!PickFillHome(Fill, Level, Hab))
		{
			break; // the fill order is the same for everyone: when it is full, it is full for the rest
		}
		FRHColonist& C = Colonists[Index];
		C.HomeLevel = Hab ? 0 : Level;
		C.HomeHab = Hab;
		if (Hab)
		{
			++Fill.HabResidents.FindOrAdd(Hab);
		}
		else
		{
			++Fill.FloorResidents.FindOrAdd(Level);
		}
		++Moved;
	}
	if (Moved > 0)
	{
		++ColonistSetVersion; // beds are assigned per home: re-derive them
		// One alert per batch, never one per person. Gate-D placeholder copy.
		OnAlert.Broadcast(FString::Printf(TEXT("MOVED HOME: %d resident%s settled into free beds elsewhere — their old home's beds were removed or lost their rating."),
			Moved, Moved == 1 ? TEXT("") : TEXT("s")));
		UE_LOG(LogRedHopeSim, Display, TEXT("=== MOVED HOME: %d of %d displaced resident%s found free beds (%d stay) ==="),
			Moved, Displaced.Num(), Displaced.Num() == 1 ? TEXT("") : TEXT("s"), Displaced.Num() - Moved);
	}
	return Moved;
}

FName URHSimWorldSubsystem::GetNextDiscovery() const
{
	TArray<TPair<FName, const FRHDiscoveryRow*>> Rows;
	if (Defs)
	{
		Defs->GetDiscoveriesSorted(Rows);
	}
	for (const auto& Pair : Rows)
	{
		if (!DiscoveryLog.Contains(Pair.Key))
		{
			return Pair.Key;
		}
	}
	return NAME_None;
}

double URHSimWorldSubsystem::GetDiscoveryProgress() const
{
	const FName Next = GetNextDiscovery();
	const FRHDiscoveryRow* Row = (Defs && !Next.IsNone()) ? Defs->GetDiscovery(Next) : nullptr;
	if (!Row || Row->LabSeatHours <= 0.f)
	{
		return 0.0;
	}
	return FMath::Clamp(DiscoverySeatHours / (double)Row->LabSeatHours, 0.0, 1.0);
}

bool URHSimWorldSubsystem::IsResearchAccruing() const
{
	if (HopeSmoothed < HopeDiscoveryThreshold || GetNextDiscovery().IsNone())
	{
		return false;
	}
	for (const auto& Job : ColonistJobs)
	{
		if (Job.Value == FName("Lab"))
		{
			return true;
		}
	}
	return false;
}

void URHSimWorldSubsystem::StepDiscovery(float SubDt)
{
	// Zero-pop / no-Lab / no-table colonies: no staffed seats or no next row,
	// so this is a no-op - every pre-crew baseline is untouched. Accrual is
	// LINEAR in dt (seat-count x sol-hours), and the pop is a threshold on a
	// monotone accumulator - era-parity-safe like growth.
	if (Colonists.Num() == 0 || HopeSmoothed < HopeDiscoveryThreshold)
	{
		return; // progress KEEPS (knowledge doesn't evaporate; accrual just pauses)
	}
	// Station tiers (Gate A): each filled Lab-line seat contributes its room's
	// YieldMul (derived alongside ColonistJobs in RefreshJobs). Plain T1 labs
	// make this the old integer seat count exactly - every pin unchanged.
	if (LabYieldSeatSum <= 0.0)
	{
		return;
	}
	const FName Next = GetNextDiscovery();
	const FRHDiscoveryRow* Row = (Defs && !Next.IsNone()) ? Defs->GetDiscovery(Next) : nullptr;
	if (!Row)
	{
		return; // sequence exhausted (or table absent) - the colony knows all it can
	}
	// Research rides the scientists' MASTERY (alive pass): a seasoned lab crew
	// doubles the pace. Linear scaling = both-band parity preserved.
	DiscoverySeatHours += LabYieldSeatSum * GetCrewSkillMul(FName("Lab")) * (SubDt / 50.0); // sol-hours per seat
	if (DiscoverySeatHours < Row->LabSeatHours)
	{
		return;
	}
	DiscoverySeatHours -= Row->LabSeatHours; // spillover carries to the next row

	DiscoveryLog.Add(Next);
	if (!Row->RewardResource.IsNone() && Row->RewardKg > 0.f)
	{
		AddStock(Row->RewardResource, Row->RewardKg);
	}
	// Research pays (v25): funding banks toward the NEXT quota award.
	if (Row->FundingKg > 0.f)
	{
		PendingResearchFundingKg += Row->FundingKg;
		UE_LOG(LogRedHopeSim, Display, TEXT("Research funding banked: +%.0f kg manifest budget (total pending %.0f)"),
			Row->FundingKg, PendingResearchFundingKg);
	}
	// A discovery can unlock a dormant station (the tier ladder's gate). The
	// flip is in-memory; loads re-apply it from the discovery log.
	if (!Row->UnlockRoom.IsNone() && Defs->ActivateRoomRow(Row->UnlockRoom))
	{
		const FRHRoomRow* Unlocked = Defs->GetRoom(Row->UnlockRoom);
		OnAlert.Broadcast(FString::Printf(
			TEXT("NEW STATION UNLOCKED — %s can now be designated. Knowledge becomes furniture."),
			Unlocked ? *Unlocked->DisplayName : *Row->UnlockRoom.ToString()));
		UE_LOG(LogRedHopeSim, Display, TEXT("Discovery '%s' unlocked room '%s'"),
			*Next.ToString(), *Row->UnlockRoom.ToString());
	}
	if (!Row->Alert.IsEmpty())
	{
		OnAlert.Broadcast(Row->Alert);
		// A BREAKTHROUGH is a celebration: golden banner + the lab crew cheers
		// (alive pass - the presentation gathers the crew to celebrate).
		OnMilestone.Broadcast(Row->Alert);
		OnCrewMoment.Broadcast((uint8)ERHCrewMoment::Celebrate, 0, 0,
			FString::Printf(TEXT("BREAKTHROUGH — %s! The lab is cheering."), *Row->DisplayName));
	}
	UE_LOG(LogRedHopeSim, Display, TEXT("=== DISCOVERY: %s (+%.0f Hope milestone%s) ==="),
		*Row->DisplayName, Row->HopeBonus,
		Row->RewardKg > 0.f ? *FString::Printf(TEXT(", +%.0f kg %s"), Row->RewardKg, *Row->RewardResource.ToString()) : TEXT(""));
}

const TCHAR* URHSimWorldSubsystem::GetHopeBandName() const
{
	switch (HopeBand)
	{
	case ERHHopeBand::Failing:     return TEXT("LOW");
	case ERHHopeBand::Strained:    return TEXT("UNSETTLED");
	case ERHHopeBand::Steady:      return TEXT("STEADY");
	case ERHHopeBand::Thriving:    return TEXT("THRIVING");
	case ERHHopeBand::Flourishing: return TEXT("FLOURISHING");
	}
	return TEXT("STEADY");
}

double URHSimWorldSubsystem::GetHumanWorkTempo() const
{
	// No people, no tempo: exactly 1.0 keeps every pre-crew regression (the
	// garden/vault/baseline suites run at zero pop) bit-identical.
	if (Colonists.Num() == 0)
	{
		return 1.0;
	}
	double Tempo = FMath::Clamp(1.0 + HopeTempoSlope * (HopeSmoothed - HopeBase), HopeTempoMin, HopeTempoMax);
	// M4 Gate D: a Malfunction crisis slows human labor while it runs (automation
	// stutters, units go inert) - recoverable, clears when the crisis ends. F6b (Gate-D
	// review, 2026-10-21): the HopeTempoMin floor holds after the multiplier too, so the
	// pace is never under it ("a strained colony is slow, never dead").
	if (ActiveCrisis == ERHCrisis::Malfunction)
	{
		Tempo = FMath::Max(Tempo * CrisisWorkPenalty, HopeTempoMin);
	}
	return Tempo;
}

void URHSimWorldSubsystem::ExtendShaft(int32 ToDepth, const FVector& HeadCm)
{
	ToDepth = FMath::Clamp(ToDepth, 0, MaxDepth);
	if (ToDepth <= ShaftDepth)
	{
		return; // the trunk never retracts
	}
	if (ShaftDepth == 0)
	{
		ShaftHeadCm = HeadCm; // the column is fixed on the first bore
	}
	const int32 NewFloors = ToDepth - ShaftDepth;
	const double Spoil = NewFloors * ShaftSpoilKgPerFloor;
	SpoilPileKg += Spoil;
	// Gate B: a chemical fraction of fresh dig spoil feeds the fertilizer
	// chain (the "chemicals from digs" source, agri spec §3). Crop-layer
	// gated so pre-crop stock ledgers stay byte-identical.
	if (Spoil > 0.0 && AreCropsLive())
	{
		AddStock(FName("SpoilChemicals"), Spoil * SpoilChemFraction);
	}
	ShaftDepth = ToDepth;
	UE_LOG(LogRedHopeSim, Display, TEXT("Shaft bored to floor -%d (%d new floor(s), +%.0f kg spoil; pile %.0f kg)"),
		ShaftDepth, NewFloors, Spoil, SpoilPileKg);
}

bool URHSimWorldSubsystem::ExcavateFloor(int32 Level, int32 Cells, FString& OutReason)
{
	if (Cells <= 0)
	{
		OutReason = TEXT("Nothing to excavate");
		return false;
	}
	if (Level >= 0 || !IsLevelConnected(Level))
	{
		OutReason = FString::Printf(TEXT("Floor %d not reached - bore the shaft deeper first"), Level);
		return false;
	}
	for (int32 i = 0; i < Cells; ++i)
	{
		CarveNextCell(Level);
	}
	const int32 Carved = GetFloorCarvedCells(Level);
	const double Spoil = Cells * SpoilKgPerCell;
	SpoilPileKg += Spoil;
	// Gate B: dig-spoil chemicals for the fertilizer chain (crop-layer gated).
	if (Spoil > 0.0 && AreCropsLive())
	{
		AddStock(FName("SpoilChemicals"), Spoil * SpoilChemFraction);
	}
	UE_LOG(LogRedHopeSim, Display, TEXT("Excavated %d cell(s) on floor %d (%d carved; +%.0f kg spoil; pile %.0f kg)"),
		Cells, Level, Carved, Spoil, SpoilPileKg);
	return true;
}

void URHSimWorldSubsystem::StepPower(float SubDt)
{
	// The storm's whole mechanical grip is one multiplier on solar generation
	// (M1-c): the bank-and-shedding stack from M0-c does the rest.
	const float Solar = Defs->EvalSolarCurve(Clock->GetSolFraction()) * (float)GetDustFactorNow();
	// D5a: the same daylight share StepProduction used, so a day-shift machine's
	// above-idle draw and its work scale together.
	const double StepEndS = GetStepEndSeconds();
	const double DayFrac = GetDaylightFraction(StepEndS - SubDt, StepEndS);

	// Pass 1: generation, storage, and each building's wanted draw.
	struct FLoadEntry { FRHBuildingInstance* B = nullptr; double WantW = 0.0; int32 Priority = 0; };
	TArray<FLoadEntry> Loads;
	double GenW = 0.0, CapWh = 0.0;
	for (FRHBuildingInstance& B : Buildings)
	{
		if (B.bUnderConstruction)
		{
			B.bPowered = false;
			continue;
		}
		// Manually switched off (storm discipline ruling): no gen, no draw,
		// reads dark. Storage still counts - the pooled bank must not clamp
		// away stored energy because a breaker flipped.
		if (B.bManualOff)
		{
			if (const FRHBuildingRow* OffDef = Defs->GetBuilding(B.DefName))
			{
				CapWh += OffDef->StorageWh;
			}
			B.bPowered = false;
			continue;
		}
		const FRHBuildingRow* Def = Defs->GetBuilding(B.DefName);
		if (!Def)
		{
			continue;
		}
		GenW += Def->PowerGenBase_W + Def->PowerGenPeak_W * Solar;
		CapWh += Def->StorageWh;
		FLoadEntry E;
		E.B = &B;
		// M1-d: a Hydrogen-fuelled batch bought its energy up-front - the grid
		// sees only idle draw while it runs.
		// D5a: a day-shift machine draws above idle only for its daylight share.
		const bool bGridBatch = B.BatchRemaining_h > 0.0 && !B.bBatchOnH2;
		E.WantW = !bGridBatch ? Def->PowerIdle_W
			: (B.bDayShift ? Def->PowerIdle_W + (Def->PowerDraw_W - Def->PowerIdle_W) * DayFrac : Def->PowerDraw_W);
		E.Priority = Def->LoadPriority;
		Loads.Add(E);
	}

	// Pass 2: shedding. While the bank holds charge every load is served;
	// once it empties, loads shed lowest LoadPriority first until demand
	// fits generation (spec: Forge sheds first, charge pads last).
	double LoadW = 0.0;
	for (const FLoadEntry& E : Loads)
	{
		E.B->bPowered = true;
		LoadW += E.WantW;
	}
	Power.ShedCount = 0;
	if (Power.BatteryWh <= 0.0 && LoadW > GenW)
	{
		Loads.Sort([](const FLoadEntry& A, const FLoadEntry& B) { return A.Priority < B.Priority; });
		for (FLoadEntry& E : Loads)
		{
			if (LoadW <= GenW)
			{
				break;
			}
			E.B->bPowered = false;
			LoadW -= E.WantW;
			++Power.ShedCount;
		}
	}

	Power.GenW = GenW;
	Power.LoadW = LoadW;
	Power.BatteryCapWh = CapWh;
	const double DeltaWh = (GenW - LoadW) * (SubDt / 50.0);
	Power.BatteryWh = FMath::Clamp(Power.BatteryWh + DeltaWh, 0.0, CapWh);
	Power.bDeficit = Power.ShedCount > 0;
	// D5a: remember the night the grid had to shed (runtime only; nothing in the
	// sim reads it). Shedding needs an empty bank and a deficit, so a shed at night
	// IS the bank running dry - and the shed flag itself alternates every step
	// while the bank scrapes empty, so the guide latches on this instead.
	if (Power.ShedCount > 0 && !IsDaylightAt(StepEndS))
	{
		LastBlackoutNight = NightIndexAt(StepEndS);
	}

	// Strip-chart ring: one sample per sol-hour, last 3 sols (72 entries).
	const int64 Hour = (int64)(Clock->GetSimSecondsTotal() / 50.0);
	if (Hour != LastPowerSampleHour)
	{
		LastPowerSampleHour = Hour;
		PowerHistory.Add(FVector3f((float)GenW, (float)LoadW, (float)Power.BatteryWh));
		if (PowerHistory.Num() > 72)
		{
			PowerHistory.RemoveAt(0, PowerHistory.Num() - 72);
		}
	}
}

void URHSimWorldSubsystem::EnqueueCommand(FRHCommand Command)
{
	Command.CommandId = NextCommandId++;
	Command.IssuedAtSimSeconds = Clock ? Clock->GetSimSecondsTotal() : 0.0;
	Command.ExecuteAtSimSeconds = Command.IssuedAtSimSeconds + OrderLagSeconds;
	UE_LOG(LogRedHopeSim, Display, TEXT("Uplink: '%s %s' transmitted, executes in %.0f sim-s"),
		*Command.Verb.ToString(), *Command.Target.ToString(), OrderLagSeconds);
	UplinkQueue.Add(MoveTemp(Command));
}

bool URHSimWorldSubsystem::CancelUplinkCommand(int32 CommandId)
{
	for (int32 i = 0; i < UplinkQueue.Num(); ++i)
	{
		if (UplinkQueue[i].CommandId == CommandId)
		{
			UE_LOG(LogRedHopeSim, Display, TEXT("Uplink: '%s %s' CANCELLED before execution"),
				*UplinkQueue[i].Verb.ToString(), *UplinkQueue[i].Target.ToString());
			UplinkQueue.RemoveAt(i);
			return true;
		}
	}
	return false; // already executed - the signal beat the cancel (that is the game)
}

bool URHSimWorldSubsystem::CanPlaceBuilding(FName DefName, const FVector& LocationCm, FString& OutReason, int32 Level) const
{
	return CanPlaceBuildingSized(DefName, LocationCm, OutReason, Level, 1, 1);
}

FVector2D URHSimWorldSubsystem::GetFootprintHalfCm(const FRHBuildingRow* Def, int32 SizeX, int32 SizeY)
{
	return FVector2D((Def ? FMath::Max(1, Def->FootprintX) : 1) * FMath::Max(1, SizeX) * 100.0,
		(Def ? FMath::Max(1, Def->FootprintY) : 1) * FMath::Max(1, SizeY) * 100.0);
}

TMap<FName, double> URHSimWorldSubsystem::GetSiteBill(const FRHBuildingInstance& B) const
{
	const FRHBuildingRow* Def = Defs ? Defs->GetBuilding(B.DefName) : nullptr;
	TMap<FName, double> Bill = Def ? URHDefinitionsSubsystem::GetBuildCostFor(*Def, B.Level) : TMap<FName, double>();
	const int32 Beds = GetBedCount(B);
	if (Beds > 1)
	{
		for (TPair<FName, double>& Line : Bill)
		{
			Line.Value *= Beds;
		}
	}
	return Bill;
}

const FRHRoomRow* URHSimWorldSubsystem::GetDomeRoom(const FRHBuildingInstance& B) const
{
	if (B.Level != 0 || B.bUnderConstruction)
	{
		return nullptr;
	}
	const FRHRoomRow* Row = GetProvidedRoom(B, /*bRequireOnline*/ false);
	return (Row && IsGardenFunction(Row->Function)) ? Row : nullptr;
}

bool URHSimWorldSubsystem::CanPlaceBuildingSized(FName DefName, const FVector& LocationCm, FString& OutReason, int32 Level, int32 SizeX, int32 SizeY, bool bCheckMaterials) const
{
	const FRHBuildingRow* Def = Defs ? Defs->GetBuilding(DefName) : nullptr;
	if (!Def)
	{
		OutReason = FString::Printf(TEXT("Unknown building '%s'"), *DefName.ToString());
		return false;
	}
	// Z-model (M1-d Gate A): the surface is always buildable; a subsurface floor
	// opens once the shaft trunk reaches it. Out-of-range or above-surface
	// refuses; a real floor the bore hasn't reached refuses with the fix ("bore
	// deeper"), not a dead end.
	if (Level > 0 || Level < -MaxDepth)
	{
		OutReason = FString::Printf(TEXT("No such floor (%d)"), Level);
		return false;
	}
	if (Level < 0 && !IsLevelConnected(Level))
	{
		OutReason = FString::Printf(TEXT("Floor %d not reached - bore the shaft deeper"), Level);
		return false;
	}
	// BM-1: pressurized habitat shells stand on the surface, in whole bays.
	if (Def->Pressurized)
	{
		if (Level != 0)
		{
			OutReason = TEXT("Habitats stand on the surface - underground, carve and zone cells instead");
			return false;
		}
		if (GetBayGridFor(Def, 1, 1).X == 0)
		{
			OutReason = TEXT("Habitat footprint is not a whole number of 10 m bays (data)");
			return false;
		}
	}
	// Drag-sized structures (Agri Gate C; BM-1 habitats): surface only, inside
	// the per-row size cap; a corridor's short side is capped.
	if (Def->DragSized)
	{
		const int32 Cap = GetMaxBedsPerSide(Def);
		if (Level != 0)
		{
			OutReason = TEXT("Greenhouse domes stand on the surface - underground, zone Greenhouse cells instead");
			return false;
		}
		if (SizeX < 1 || SizeY < 1 || SizeX > Cap || SizeY > Cap)
		{
			OutReason = Def->Pressurized ? FString::Printf(TEXT("A habitat is 1 to %d bays a side"), Cap)
				: FString::Printf(TEXT("A dome is 1 to %d beds a side"), Cap);
			return false;
		}
		if (Def->DragShortSideMax > 0 && FMath::Min(SizeX, SizeY) > Def->DragShortSideMax)
		{
			OutReason = FString::Printf(TEXT("A %s is %d bay(s) wide"), *Def->DisplayName, Def->DragShortSideMax);
			return false;
		}
	}
	else
	{
		SizeX = SizeY = 1;
	}
	if (Def->Pressurized && !IsOnBayGrid(Def, LocationCm, SizeX, SizeY))
	{
		OutReason = TEXT("Habitats sit on the 10 m bay grid");
		return false;
	}
	// Prefab stations (tiered production T1): on a carved cell or (BM-1) in a
	// habitat bay, and only once research has opened their room.
	if (!Def->ProvidesRoom.IsNone() && !Def->DragSized && !Def->Pressurized)
	{
		int32 BayHab = 0, Bay = INDEX_NONE;
		if (Level >= 0)
		{
			bool bInBay = false;
			if (Def->Interior && Level == 0)
			{
				for (const FRHBuildingInstance& H : Buildings)
				{
					const FRHBuildingRow* HDef = H.Level == 0 ? Defs->GetBuilding(H.DefName) : nullptr;
					if (HDef && HDef->Pressurized && IsWhollyInBay(H, LocationCm, GetFootprintHalfCm(Def, 1, 1), Bay))
					{
						BayHab = H.Id;
						bInBay = true;
						break;
					}
				}
			}
			if (!bInBay)
			{
				OutReason = TEXT("Station modules go on a dug floor or inside a habitat bay - ride down with the SHAFT strip first");
				return false;
			}
		}
		const FRHRoomRow* Room = Defs->GetRoom(Def->ProvidesRoom);
		if (!Room || !Room->SliceActive)
		{
			FString By;
			TArray<TPair<FName, const FRHDiscoveryRow*>> Discoveries;
			Defs->GetDiscoveriesSorted(Discoveries);
			for (const TPair<FName, const FRHDiscoveryRow*>& D : Discoveries)
			{
				if (D.Value && D.Value->UnlockRoom == Def->ProvidesRoom)
				{
					By = D.Value->DisplayName;
					break;
				}
			}
			OutReason = By.IsEmpty() ? FString(TEXT("Locked - research has not opened this station yet"))
				: FString::Printf(TEXT("Locked - research %s first"), *By);
			return false;
		}
		if (Level < 0)
		{
			const int32 Cell = FindCarvedCellAt(Level, LocationCm);
			if (Cell == INDEX_NONE)
			{
				OutReason = TEXT("Place it on a carved cell of this floor");
				return false;
			}
			// One station per cell: a module never stacks on a zoned room or on
			// another module (DesignateRoom refuses the converse).
			const FName Zoned = GetRoomAt(Level, Cell);
			if (!Zoned.IsNone())
			{
				const FRHRoomRow* ZRow = Defs->GetRoom(Zoned);
				OutReason = FString::Printf(TEXT("This cell is zoned as %s - clear the zone first"), ZRow ? *ZRow->DisplayName : *Zoned.ToString());
				return false;
			}
			if (FindModuleOnCell(Level, Cell))
			{
				OutReason = TEXT("A station module already sits on this cell");
				return false;
			}
		}
		else
		{
			// One station per bay, never in a fixed-room shell (a corridor).
			const FRHBuildingInstance* HabB = FindBuildingById(BayHab);
			const FRHBuildingRow* HDef = HabB ? Defs->GetBuilding(HabB->DefName) : nullptr;
			if (HDef && !HDef->ProvidesRoom.IsNone())
			{
				OutReason = FString::Printf(TEXT("This bay is part of a %s - its room is fixed"), *HDef->DisplayName);
				return false;
			}
			const FName Zoned = GetBayRoom(BayHab, Bay);
			if (!Zoned.IsNone())
			{
				const FRHRoomRow* ZRow = Defs->GetRoom(Zoned);
				OutReason = FString::Printf(TEXT("This bay is zoned as %s - clear the zone first"), ZRow ? *ZRow->DisplayName : *Zoned.ToString());
				return false;
			}
			if (FindModuleInBay(BayHab, Bay))
			{
				OutReason = TEXT("A station module already sits in this bay");
				return false;
			}
		}
	}
	// Pylon-like defs (they project coverage and link by range) are the
	// territory extenders: their rule is link range to the nearest node,
	// not the coverage union. Everything else must sit inside coverage.
	if (Level < 0 && IsLevelConnected(Level))
	{
		// Subsurface: the shaft trunk carries the grid down and taps the whole
		// (small, starter) floor - coverage and the grid node both come from the
		// shaft, so neither the link-range nor the coverage-union check applies
		// (underground proposal §5; local distribution nodes are data headroom).
	}
	else if (Def->CoverageRadius_m > 0.f && Def->LinkRange_m > 0.f)
	{
		double NearestNodeCm = TNumericLimits<double>::Max();
		for (const FRHBuildingInstance& B : Buildings)
		{
			const FRHBuildingRow* NodeDef = (B.bUnderConstruction || B.Level != Level) ? nullptr : Defs->GetBuilding(B.DefName);
			if (NodeDef && NodeDef->CoverageRadius_m > 0.f)
			{
				NearestNodeCm = FMath::Min(NearestNodeCm, (double)FVector::DistXY(B.LocationCm, LocationCm));
			}
		}
		if (NearestNodeCm > Def->LinkRange_m * 100.0)
		{
			OutReason = FString::Printf(TEXT("No grid node within %.0f m link range"), Def->LinkRange_m);
			return false;
		}
	}
	else if (Def->Pressurized)
	{
		// BM-1: every bay centre must be inside coverage, not just the middle.
		const FIntPoint G = GetBayGridFor(Def, SizeX, SizeY);
		const FVector2D Half = GetFootprintHalfCm(Def, SizeX, SizeY);
		for (int32 Bay = 0; Bay < G.X * G.Y; ++Bay)
		{
			const FVector BayC(LocationCm.X - Half.X + (Bay % G.X + 0.5) * 1000.0, LocationCm.Y - Half.Y + (Bay / G.X + 0.5) * 1000.0, LocationCm.Z);
			if (!IsInCoverage(BayC, Level))
			{
				OutReason = TEXT("Outside grid coverage - extend pylons first");
				return false;
			}
		}
	}
	else if (!IsInCoverage(LocationCm, Level))
	{
		// BM-1: an Interior def wholly inside a habitat bay is fed by the hab.
		bool bInBay = false;
		if (Def->Interior && Level == 0)
		{
			const FVector2D Half = GetFootprintHalfCm(Def, 1, 1);
			for (const FRHBuildingInstance& H : Buildings)
			{
				const FRHBuildingRow* HDef = H.Level == 0 ? Defs->GetBuilding(H.DefName) : nullptr;
				int32 Bay = INDEX_NONE;
				if (HDef && HDef->Pressurized && IsWhollyInBay(H, LocationCm, Half, Bay))
				{
					bInBay = true;
					break;
				}
			}
		}
		if (!bInBay)
		{
			OutReason = TEXT("Outside grid coverage - extend pylons first");
			return false;
		}
	}
	// Footprint overlap: one cell, one structure (the demo-era gap - two
	// buildings could interpenetrate). Half-extents in cm match the ghost box
	// (FootprintX cells x 2 m); strict < keeps edge-adjacent placement legal.
	// BM-1: a shell is compared at its snapped location (the grid check
	// tolerates 1 cm; AddBuilding snaps), so a hair off-grid never overlaps.
	{
		const FVector OverlapLoc = Def->Pressurized ? SnapToBayGrid(Def, LocationCm, SizeX, SizeY) : LocationCm;
		const FVector2D Half = GetFootprintHalfCm(Def, SizeX, SizeY);
		const double HalfX = Half.X;
		const double HalfY = Half.Y;
		for (const FRHBuildingInstance& B : Buildings)
		{
			if (B.Level != Level)
			{
				continue;
			}
			const FRHBuildingRow* BDef = Defs->GetBuilding(B.DefName);
			if (!BDef)
			{
				continue;
			}
			const FVector2D BHalf = GetFootprintHalfCm(BDef, B.SizeX, B.SizeY);
			if (FMath::Abs(B.LocationCm.X - OverlapLoc.X) < HalfX + BHalf.X
				&& FMath::Abs(B.LocationCm.Y - OverlapLoc.Y) < HalfY + BHalf.Y)
			{
				// BM-1: furniture-scale Interior machines stand INSIDE a shell's
				// bay (one-way: a shell dropped over machines is still refused).
				int32 Bay = INDEX_NONE;
				if (Def->Interior && Level == 0 && BDef->Pressurized && IsWhollyInBay(B, LocationCm, FVector2D(HalfX, HalfY), Bay))
				{
					continue;
				}
				OutReason = FString::Printf(TEXT("Footprint overlaps %s #%d"), *B.DefName.ToString(), B.Id);
				return false;
			}
		}
		// C4: roads and buildings keep clear of each other from both sides - a
		// built road, a road being built, and a BuildRoad order in transit all
		// claim their strip.
		if (Level == 0)
		{
			FString RoadWhat;
			if (FindRoadOverlap(FVector(OverlapLoc.X, OverlapLoc.Y, 0.0), FVector2D(HalfX, HalfY), RoadWhat))
			{
				OutReason = FString::Printf(TEXT("Footprint overlaps %s"), *RoadWhat);
				return false;
			}
		}
		// In-flight orders block too (director finding, M1-d hand-play): during
		// the signal-lag window the same spot happily accepted a second order,
		// and the collision only surfaced ~45 s later as a confusing rejection.
		// The ghost now goes red over a spot that is already spoken for.
		for (const FRHCommand& C : UplinkQueue)
		{
			if (C.Verb != FName("Build") || C.Level != Level)
			{
				continue;
			}
			const FRHBuildingRow* QDef = Defs->GetBuilding(C.Target);
			if (!QDef)
			{
				continue;
			}
			const FIntPoint QSize = QDef->DragSized ? DecodeBuildSize(C.Value) : FIntPoint(1, 1);
			const FVector2D QHalf = GetFootprintHalfCm(QDef, QSize.X, QSize.Y);
			const FVector QLoc = QDef->Pressurized ? SnapToBayGrid(QDef, C.Location, QSize.X, QSize.Y) : C.Location;
			if (FMath::Abs(QLoc.X - OverlapLoc.X) < HalfX + QHalf.X
				&& FMath::Abs(QLoc.Y - OverlapLoc.Y) < HalfY + QHalf.Y)
			{
				OutReason = FString::Printf(TEXT("%s order already in transit for that spot"), *C.Target.ToString());
				return false;
			}
		}
		// BM-4: furnishings stand where a machine would go.
		for (const FRHFurnitureInstance& F : Furniture)
		{
			FVector FC;
			FVector2D FH;
			if ((F.Key.Z == 0 ? F.Key.X : 0) != Level || !GetFurnitureFootprintCm(F.Row, F.Key, F.Slot, F.Rot, FC, FH))
			{
				continue;
			}
			if (FMath::Abs(FC.X - OverlapLoc.X) < HalfX + FH.X && FMath::Abs(FC.Y - OverlapLoc.Y) < HalfY + FH.Y)
			{
				OutReason = FString::Printf(TEXT("Footprint overlaps a furnishing (%s #%d)"), *F.Row.ToString(), F.Id);
				return false;
			}
		}
		// BM-6: a new shell may not wall over a window or an airlock, and
		// nothing may stand in an airlock's chamber.
		for (const FRHWallFeature& W : WallFeatures)
		{
			const FRHDoorRow* WRow = Level == 0 ? Defs->GetDoor(W.Row) : nullptr;
			FIntPoint InsidePt, OutStep;
			if (!WRow || ClassifyWallEdge(W.Edge, nullptr, &InsidePt, &OutStep) != ERHWallEdge::Exterior)
			{
				continue;
			}
			const FVector OutsideBay((InsidePt.X + OutStep.X) * 1000.0, (InsidePt.Y + OutStep.Y) * 1000.0, 0.0);
			if (Def->Pressurized && FMath::Abs(OutsideBay.X - OverlapLoc.X) < HalfX && FMath::Abs(OutsideBay.Y - OverlapLoc.Y) < HalfY)
			{
				OutReason = FString::Printf(TEXT("The %s #%d faces that way - sell it first"), *WRow->DisplayName, W.Id);
				return false;
			}
			FVector ChC;
			FVector2D ChH;
			if (WRow->Kind == NAME_WallAirlock && GetAirlockChamberRect(W.Edge, ChC, ChH)
				&& FMath::Abs(ChC.X - OverlapLoc.X) < HalfX + ChH.X && FMath::Abs(ChC.Y - OverlapLoc.Y) < HalfY + ChH.Y)
			{
				OutReason = FString::Printf(TEXT("That is the chamber outside the Airlock #%d"), W.Id);
				return false;
			}
		}
	}
	if (!bCheckMaterials)
	{
		// C5b: the bill is already paid (growth takes it whole at the start); the site rules above are all that is left.
	}
	else if (Def->ImportOnly)
	{
		// Imported hardware: needs flat-pack stock, never Struct.
		const int32* Stock = ImportStock.Find(DefName);
		if (!Stock || *Stock <= 0)
		{
			OutReason = TEXT("No imported units in stock (manifest required)");
			return false;
		}
	}
	else
	{
		// Materials are delivered to the site by haulers (task board);
		// this only rejects orders the colony cannot POSSIBLY fill - short
		// stock with a completed producer online is an order that waits for
		// production (M1-d Gate B: how the first Shielding gets made - the
		// site's demand is what tells the Forge to make it).
		for (const auto& Cost : URHDefinitionsSubsystem::GetBuildCostFor(*Def, Level))
		{
			const double Need = Cost.Value * SizeX * SizeY;
			const double Available = GetTotalSolid(Cost.Key) + GetStock(Cost.Key);
			if (Available < Need && !HasProducerFor(Cost.Key))
			{
				OutReason = FString::Printf(TEXT("Insufficient %s (%.0f needed, %.0f on hand, no producer online)"),
					*Cost.Key.ToString(), Need, Available);
				return false;
			}
		}
	}
	return true;
}

const FRHDepositState* URHSimWorldSubsystem::FindDepositNear(const FVector& LocationCm, double MaxDistCm, int32 Level) const
{
	const FRHDepositState* Best = nullptr;
	double BestDist = MaxDistCm;
	for (const FRHDepositState& D : Deposits)
	{
		if (D.Level != Level || !D.bDiscovered)
		{
			continue;
		}
		const double Dist = FVector::DistXY(D.LocationCm, LocationCm);
		if (Dist <= BestDist)
		{
			BestDist = Dist;
			Best = &D;
		}
	}
	return Best;
}

void URHSimWorldSubsystem::ExecuteCommand(const FRHCommand& Cmd)
{
	if (Cmd.Verb == FName("Build"))
	{
		FString Reason;
		const FRHBuildingRow* SizedDef = Defs ? Defs->GetBuilding(Cmd.Target) : nullptr;
		const FIntPoint Size = (SizedDef && SizedDef->DragSized) ? DecodeBuildSize(Cmd.Value) : FIntPoint(1, 1);
		if (!CanPlaceBuildingSized(Cmd.Target, Cmd.Location, Reason, Cmd.Level, Size.X, Size.Y))
		{
			OnCommandRejected.Broadcast(Cmd, Reason);
			return;
		}
		const FRHBuildingRow* Def = Defs->GetBuilding(Cmd.Target);
		if (Def->ImportOnly)
		{
			--ImportStock.FindOrAdd(Cmd.Target); // consumption happens at execution, not preview
		}
		AddBuilding(Cmd.Target, Cmd.Location, /*bInstant*/ Def->BuildTime_s <= 0.0, Cmd.Level, Size.X, Size.Y);
	}
	else if (Cmd.Verb == FName("BuildRoad"))
	{
		ExecuteBuildRoad(Cmd); // C4: rejects through OnCommandRejected itself
	}
	else if (Cmd.Verb == FName("Dig"))
	{
		bool bFound = false;
		for (FRHDepositState& D : Deposits)
		{
			// Undiscovered deposits reject as unknown - mission control cannot
			// designate ground truth the colony has not surveyed (and the
			// message must not leak that something is there).
			if (D.RowName == Cmd.Target && D.bDiscovered)
			{
				D.bDesignated = true;
				bFound = true;
				UE_LOG(LogRedHopeSim, Display, TEXT("Dig designation: %s (%s, %.0f t remaining)"),
					*D.RowName.ToString(), *D.Type.ToString(), D.RemainingKg / 1000.0);
			}
		}
		if (!bFound)
		{
			OnCommandRejected.Broadcast(Cmd, FString::Printf(TEXT("Unknown deposit '%s'"), *Cmd.Target.ToString()));
			return;
		}
	}
	else if (Cmd.Verb == FName("Survey"))
	{
		FRHTask T;
		T.Id = NextTaskId++;
		T.Type = ERHTaskType::Survey;
		T.TargetCm = FVector(Cmd.Location.X, Cmd.Location.Y, 0.0);
		Tasks.Add(T);
		UE_LOG(LogRedHopeSim, Display, TEXT("Survey posted: (%.0f, %.0f) m - awaiting a scout"),
			Cmd.Location.X / 100.0, Cmd.Location.Y / 100.0);
	}
	else if (Cmd.Verb == FName("Bore"))
	{
		// M1-d Gate A2: order the shaft trunk down to Value floors. The Borer
		// works it one BoreFloor batch per floor; designations are standing
		// orders, so deeper re-orders just raise the target.
		bool bHaveBorer = false;
		for (const FRHBuildingInstance& B : Buildings)
		{
			const FRHBuildingRow* D = Defs->GetBuilding(B.DefName);
			if (D && D->CanBore && !B.bUnderConstruction)
			{
				bHaveBorer = true;
				break;
			}
		}
		if (!bHaveBorer)
		{
			OnCommandRejected.Broadcast(Cmd, TEXT("No Borer online - build one first"));
			return;
		}
		const int32 Target = FMath::Clamp((int32)Cmd.Value, 0, MaxDepth);
		if (Target <= ShaftDepth)
		{
			OnCommandRejected.Broadcast(Cmd, FString::Printf(TEXT("Shaft already at floor -%d"), ShaftDepth));
			return;
		}
		BoreTargetDepth = FMath::Max(BoreTargetDepth, Target);
		UE_LOG(LogRedHopeSim, Display, TEXT("Bore designation: shaft to floor -%d (now -%d)"), BoreTargetDepth, ShaftDepth);
	}
	else if (Cmd.Verb == FName("Excavate"))
	{
		// M1-d Gate A2: carve Value cells on a reached floor. Queue decrements
		// at batch START (committed work); the carve applies at completion.
		const int32 Cells = (int32)Cmd.Value;
		if (Cells <= 0 || Cmd.Level >= 0 || Cmd.Level < -MaxDepth)
		{
			OnCommandRejected.Broadcast(Cmd, TEXT("Excavation needs a subsurface floor and a cell count"));
			return;
		}
		if (!IsLevelConnected(Cmd.Level))
		{
			OnCommandRejected.Broadcast(Cmd, FString::Printf(TEXT("Floor %d not reached - bore the shaft deeper first"), Cmd.Level));
			return;
		}
		CarveQueue.FindOrAdd(Cmd.Level) += Cells;
		UE_LOG(LogRedHopeSim, Display, TEXT("Excavation designation: %d cell(s) on floor %d (%d queued there)"),
			Cells, Cmd.Level, CarveQueue[Cmd.Level]);
	}
	else if (Cmd.Verb == FName("Assign"))
	{
		// Tiered production T2: put a station at the FRONT of the seat order.
		// Target None = a room cell (Level, Value = cell index); Target
		// "Building" = a prefab module (Value = building id).
		FIntVector Key;
		const FRHRoomRow* Row = nullptr;
		if (Cmd.Target == FName("Building"))
		{
			const FRHBuildingInstance* B = FindBuilding((int32)Cmd.Value);
			Row = B ? GetProvidedRoom(*B, /*bRequireOnline*/ false) : nullptr;
			Key = FIntVector(B ? B->Level : 0, INDEX_NONE, B ? B->Id : 0);
		}
		else
		{
			int32 H = 0, Bay = INDEX_NONE;
			if (Cmd.Level == 0 && FindBayAt(Cmd.Location, H, Bay))
			{
				// BM-1: a habitat bay, found by the order's location.
				Row = Defs ? Defs->GetRoom(GetBayRoom(H, Bay)) : nullptr;
				Key = FIntVector(0, Bay, H);
			}
			else
			{
				Row = Defs ? Defs->GetRoom(GetRoomAt(Cmd.Level, (int32)Cmd.Value)) : nullptr;
				Key = FIntVector(Cmd.Level, (int32)Cmd.Value, 0);
			}
		}
		if (!Row || !Row->SliceActive || !IsSeatFunction(Row->Function))
		{
			OnCommandRejected.Broadcast(Cmd, TEXT("Not a work station - assign staff to a Lab, Workstation or Gardening Station"));
			return;
		}
		SeatPriority.Remove(Key);
		SeatPriority.Insert(Key, 0);
		if (SeatPriority.Num() > MaxSeatPriority)
		{
			SeatPriority.SetNum(MaxSeatPriority);
		}
		RefreshJobs();
		UE_LOG(LogRedHopeSim, Display, TEXT("Assign: station (%d, %d, %d) now first of %d"), Key.X, Key.Y, Key.Z, SeatPriority.Num());
	}
	else if (Cmd.Verb == FName("Demolish"))
	{
		// BM-3: take a structure down or cancel a site (Value = building id).
		FString Reason;
		if (Cmd.Target != FName("Building"))
		{
			OnCommandRejected.Broadcast(Cmd, TEXT("Demolish needs a structure"));
			return;
		}
		if (!CanDemolishBuilding((int32)Cmd.Value, Reason))
		{
			OnCommandRejected.Broadcast(Cmd, Reason);
			return;
		}
		RemoveBuilding((int32)Cmd.Value);
	}
	else if (Cmd.Verb == FName("Furnish"))
	{
		// BM-4: install a furnishing from stock - instant once the order lands.
		FString Reason;
		FIntVector Key;
		int32 Cell = 0, Slot = 0, Rot = 0;
		DecodeFurnish(Cmd.Value, Cell, Slot, Rot);
		if (!ResolveFurnishOrderKey(Cmd, Key))
		{
			OnCommandRejected.Broadcast(Cmd, TEXT("No room there to furnish"));
			return;
		}
		if (!CanPlaceFurniture(Cmd.Target, Key, Slot, Rot, Reason, /*bCheckQueued*/ false))
		{
			OnCommandRejected.Broadcast(Cmd, Reason);
			return;
		}
		SpendTradeLot(URHDefinitionsSubsystem::GetFurnitureCost(*Defs->GetFurniture(Cmd.Target)));
		AddFurniture(Cmd.Target, Key, Slot, Rot);
		RefreshJobs();
	}
	else if (Cmd.Verb == FName("Fit"))
	{
		// BM-6: a window, door or airlock into a habitat wall (instant once
		// the order lands). Location = a point in the bay, Value = the side.
		FString Reason;
		FIntVector Edge;
		if (!ResolveWallEdge(Cmd.Location, (int32)Cmd.Value, Edge))
		{
			OnCommandRejected.Broadcast(Cmd, TEXT("No habitat wall there"));
			return;
		}
		if (!CanPlaceWallFeature(Cmd.Target, Edge, Reason, /*bCheckQueued*/ false))
		{
			OnCommandRejected.Broadcast(Cmd, Reason);
			return;
		}
		SpendTradeLot(URHDefinitionsSubsystem::GetDoorCost(*Defs->GetDoor(Cmd.Target)));
		AddWallFeature(Cmd.Target, Edge);
	}
	else if (Cmd.Verb == FName("Sell") && Cmd.Target == FName("WallFeature"))
	{
		// BM-6: sell a wall fitting back.
		FString Reason;
		if (!CanSellWallFeature((int32)Cmd.Value, Reason))
		{
			OnCommandRejected.Broadcast(Cmd, Reason);
			return;
		}
		TSet<int32> Ids;
		Ids.Add((int32)Cmd.Value);
		RemoveWallFeatures(Ids, nullptr);
	}
	else if (Cmd.Verb == FName("Sell"))
	{
		// BM-4: sell one furnishing (Target "Furniture", Value = Id) or a whole
		// cell's (Target "Cell": Level + Value = cell, or Location = a bay).
		FString Reason;
		TSet<int32> Ids;
		if (Cmd.Target == FName("Furniture"))
		{
			if (!CanSellFurniture((int32)Cmd.Value, Reason))
			{
				OnCommandRejected.Broadcast(Cmd, Reason);
				return;
			}
			Ids.Add((int32)Cmd.Value);
		}
		else if (Cmd.Target == FName("Cell"))
		{
			int32 H = 0, Bay = INDEX_NONE;
			if (Cmd.Level == 0 && !FindBayAt(Cmd.Location, H, Bay))
			{
				OnCommandRejected.Broadcast(Cmd, TEXT("No habitat bay there"));
				return;
			}
			const FIntVector Key = Cmd.Level == 0 ? FIntVector(0, Bay, H) : FIntVector(Cmd.Level, (int32)Cmd.Value, 0);
			if (!CanSellCell(Key, Reason))
			{
				OnCommandRejected.Broadcast(Cmd, Reason);
				return;
			}
			TArray<int32> CellIds;
			GetCellFurniture(Key, &CellIds);
			Ids.Append(CellIds);
		}
		else
		{
			OnCommandRejected.Broadcast(Cmd, TEXT("Sell needs a furnishing"));
			return;
		}
		RemoveFurniture(Ids, nullptr);
		RefreshJobs();
	}
	else if (Cmd.Verb == FName("Designate"))
	{
		// M2 Gate B: zone a carved cell with a room function (Target = room row,
		// None = clear; Value = spiral cell index). Zoning is free - it rides
		// the uplink like every other order because it IS an order.
		// BM-1: at the surface the order's location picks a habitat bay;
		// anything else (no bay hit) is the legacy cell path verbatim.
		FString Reason;
		int32 H = 0, Bay = INDEX_NONE;
		const bool bBay = Cmd.Level == 0 && FindBayAt(Cmd.Location, H, Bay);
		if (bBay ? !DesignateBay(H, Bay, Cmd.Target, Reason) : !DesignateRoom(Cmd.Level, (int32)Cmd.Value, Cmd.Target, Reason))
		{
			OnCommandRejected.Broadcast(Cmd, Reason);
			return;
		}
	}
	else if (Cmd.Verb == FName("Convoy"))
	{
		// M3 Gate A: dispatch the rover convoy to a rival (Target = rival row).
		// Everything is committed AT DEPARTURE - fuel, wear, and your export lot
		// (the Borer's H2-batch discipline). The return leg delivers their goods.
		static const FName NHydrogen(TEXT("Hydrogen")), NSpareParts(TEXT("SpareParts"));
		const FRHRivalRow* Rival = Defs ? Defs->GetRival(Cmd.Target) : nullptr;
		if (!Rival || !IsRivalAvailable(Cmd.Target))
		{
			OnCommandRejected.Broadcast(Cmd, FString::Printf(TEXT("No trade contact '%s'"), *Cmd.Target.ToString()));
			return;
		}
		if (!ConvoyRival.IsNone())
		{
			OnCommandRejected.Broadcast(Cmd, FString::Printf(TEXT("The convoy is already out (to %s)"), *ConvoyRival.ToString()));
			return;
		}
		if (ClosedRoutes.Contains(Cmd.Target))
		{
			OnCommandRejected.Broadcast(Cmd, FString::Printf(TEXT("The route to %s is closed - you cut ties when you complied with Earth"), *Rival->DisplayName));
			return;
		}
		if (GetSabotageRemaining(Cmd.Target) > 0.0)
		{
			OnCommandRejected.Broadcast(Cmd, FString::Printf(TEXT("%s's production is disrupted (sabotage) - no trade until they recover"), *Rival->DisplayName));
			return;
		}
		if (DefectedRivals.Contains(Cmd.Target))
		{
			OnCommandRejected.Broadcast(Cmd, FString::Printf(TEXT("%s has sided with Earth - relations are frozen, no trade"), *Rival->DisplayName));
			return;
		}
		if (EmbargoGrace.Contains(Cmd.Target))
		{
			OnCommandRejected.Broadcast(Cmd, FString::Printf(TEXT("%s has embargoed you under Earth pressure - reassure them first"), *Rival->DisplayName));
			return;
		}
		// Build ONE aggregate cost lot - fuel + wear + your export goods, ADDING
		// overlapping resources - so preflight and commit validate/spend the
		// identical sum. (Adversarial review, M3 Gate A: three independent checks
		// against the same stock could each pass while the SUM overdraws, driving
		// a resource negative if an ImportLot ever names Hydrogen or SpareParts.
		// Current data doesn't, but the aggregate closes the hole permanently.)
		TMap<FName, double> Cost;
		Cost.FindOrAdd(NHydrogen) += ConvoyH2PerRun;
		Cost.FindOrAdd(NSpareParts) += ConvoyWearParts;
		for (const auto& Item : URHDefinitionsSubsystem::ParseResourceList(Rival->ImportLot))
		{
			Cost.FindOrAdd(Item.Key) += Item.Value;
		}
		if (!HasTradeLot(Cost))
		{
			// Name the first shortfall for a legible refusal.
			FName Short = NAME_None;
			for (const auto& Item : Cost)
			{
				const double Have = (Defs && Defs->IsSolidResource(Item.Key))
					? GetTotalSolid(Item.Key) + GetStock(Item.Key) : GetStock(Item.Key);
				if (Have + KINDA_SMALL_NUMBER < Item.Value) { Short = Item.Key; break; }
			}
			OnCommandRejected.Broadcast(Cmd, FString::Printf(
				TEXT("Convoy short of %s (needs fuel %.0f H2 + %.0f parts + %s)"),
				*Short.ToString(), ConvoyH2PerRun, ConvoyWearParts, *Rival->ImportLot));
			return;
		}
		// Commit at departure - one spend of the validated aggregate.
		SpendTradeLot(Cost);
		ConvoyRival = Cmd.Target;
		bConvoyReturning = false;
		ConvoyLegSols = 0.0;
		RelationRef(Cmd.Target); // ensure the relation entry exists
		UE_LOG(LogRedHopeSim, Display, TEXT("Convoy dispatched to %s (%.0f km; %s -> %s)"),
			*Rival->DisplayName, Rival->DistanceKm, *Rival->ImportLot, *Rival->ExportLot);
	}
	else if (Cmd.Verb == FName("Solidarity"))
	{
		// M3 Gate C: answer Earth's demand. Target Comply = cut trade; anything
		// else (Defy) = keep faith. Rides the uplink like every decision.
		FString Reason;
		if (!ResolveDilemma(Cmd.Target == FName("Comply"), Reason))
		{
			OnCommandRejected.Broadcast(Cmd, Reason);
			return;
		}
	}
	else if (Cmd.Verb == FName("Covert"))
	{
		// M4 Gate A: a covert requisition against a rival (Target = rival row).
		// Resolves at execution - the day/night of THIS moment shapes detection,
		// so timing your op for the Martian night is real. Rides the uplink.
		FString Reason;
		if (!ResolveCovert(Cmd.Target, Reason))
		{
			OnCommandRejected.Broadcast(Cmd, Reason);
			return;
		}
	}
	else if (Cmd.Verb == FName("Launder"))
	{
		// M4 Gate B: make amends to a rival, defusing HiddenTension.
		FString Reason;
		if (!ResolveLaunder(Cmd.Target, Reason))
		{
			OnCommandRejected.Broadcast(Cmd, Reason);
			return;
		}
	}
	else if (Cmd.Verb == FName("Sabotage"))
	{
		// M4 Gate B: a covert blackout that disrupts a rival's trade.
		FString Reason;
		if (!ResolveSabotage(Cmd.Target, Reason))
		{
			OnCommandRejected.Broadcast(Cmd, Reason);
			return;
		}
	}
	else if (Cmd.Verb == FName("Pacify"))
	{
		// M4 Gate C: spend Influence to lift an Earth-ordered embargo.
		FString Reason;
		if (!ResolvePacify(Cmd.Target, Reason))
		{
			OnCommandRejected.Broadcast(Cmd, Reason);
			return;
		}
	}
	else if (Cmd.Verb == FName("DemolishRoad"))
	{
		// C6a (Q3a): a real order, with an explicit reject reason.
		FString Reason;
		if (!ExecuteDemolishRoad(Cmd, Reason))
		{
			OnCommandRejected.Broadcast(Cmd, Reason);
			return;
		}
	}
	else if (Cmd.Verb == FName("PaintDistrict") || Cmd.Verb == FName("EraseDistrict") || Cmd.Verb == FName("RenameDistrict"))
	{
		// C5a: districts. Each worker re-runs its own preflight and says why it refuses.
		FString Reason;
		const bool bOk = Cmd.Verb == FName("PaintDistrict") ? ExecutePaintDistrict(Cmd, Reason)
			: (Cmd.Verb == FName("EraseDistrict") ? ExecuteEraseDistrict(Cmd, Reason) : ExecuteRenameDistrict(Cmd, Reason));
		if (!bOk)
		{
			OnCommandRejected.Broadcast(Cmd, Reason);
			return;
		}
	}
	else
	{
		// An order no worker knows is refused, never "executed" (C5a: a new verb that was
		// never wired in, or a misspelt one, used to fall through to the broadcast below).
		OnCommandRejected.Broadcast(Cmd, FString::Printf(TEXT("Unknown order '%s'"), *Cmd.Verb.ToString()));
		return;
	}

	OnCommandExecuted.Broadcast(Cmd);
}

bool URHSimWorldSubsystem::HasTradeLot(const TMap<FName, double>& Lot) const
{
	for (const auto& Item : Lot)
	{
		const double Have = Defs && Defs->IsSolidResource(Item.Key)
			? GetTotalSolid(Item.Key) + GetStock(Item.Key)
			: GetStock(Item.Key);
		if (Have + KINDA_SMALL_NUMBER < Item.Value)
		{
			return false;
		}
	}
	return true;
}

void URHSimWorldSubsystem::SpendTradeLot(const TMap<FName, double>& Lot)
{
	for (const auto& Item : Lot)
	{
		double Remaining = Item.Value;
		if (Defs && Defs->IsSolidResource(Item.Key))
		{
			// Drain building stores in ascending Id order (deterministic),
			// OutputKg then InputKg; pooled remainder (import-only parts) last.
			TArray<FRHBuildingInstance*> Sorted;
			for (FRHBuildingInstance& B : Buildings) { Sorted.Add(&B); }
			Sorted.Sort([](const FRHBuildingInstance& A, const FRHBuildingInstance& B){ return A.Id < B.Id; });
			for (FRHBuildingInstance* B : Sorted)
			{
				if (Remaining <= KINDA_SMALL_NUMBER) { break; }
				for (TMap<FName, double>* Store : { &B->OutputKg, &B->InputKg })
				{
					if (double* Have = Store->Find(Item.Key))
					{
						const double Take = FMath::Min(*Have, Remaining);
						*Have -= Take;
						Remaining -= Take;
						if (Remaining <= KINDA_SMALL_NUMBER) { break; }
					}
				}
			}
			if (Remaining > KINDA_SMALL_NUMBER)
			{
				AddStock(Item.Key, -Remaining); // pooled fallback (e.g. SpareParts)
				Remaining = 0.0;
			}
			OnStockChanged.Broadcast(Item.Key, GetTotalSolid(Item.Key));
		}
		else
		{
			AddStock(Item.Key, -Remaining); // fluids/gases from the pool
		}
	}
}

double& URHSimWorldSubsystem::RelationRef(FName Rival)
{
	if (double* R = RivalRelations.Find(Rival))
	{
		return *R;
	}
	const FRHRivalRow* Row = Defs ? Defs->GetRival(Rival) : nullptr;
	return RivalRelations.Add(Rival, Row ? Row->RelationStart : 30.0);
}

double URHSimWorldSubsystem::ConvoyLegSolsFor(FName Rival) const
{
	const FRHRivalRow* Row = Defs ? Defs->GetRival(Rival) : nullptr;
	const double Speed = FMath::Max(ConvoySpeedKmPerSol, KINDA_SMALL_NUMBER);
	return Row ? Row->DistanceKm / Speed : 0.0;
}

void URHSimWorldSubsystem::StepTrade(float SubDt)
{
	// Idle colonies (and every pre-M3 baseline) have no convoy out - a true
	// no-op, so the 10-sol baseline is byte-identical.
	if (ConvoyRival.IsNone())
	{
		return;
	}
	const FRHRivalRow* Rival = Defs ? Defs->GetRival(ConvoyRival) : nullptr;
	if (!Rival)
	{
		ConvoyRival = NAME_None; // the row vanished (data edit) - abort cleanly
		return;
	}
	// A dust storm freezes the convoy where it sits (brief: routes disrupted by
	// storms). The rover waits out the weather - progress simply doesn't advance.
	const FRHEventRow* Event = GetActiveEvent();
	if (Event && Event->Type == FName("DustStorm"))
	{
		return;
	}
	const double DtSols = SubDt / (double)URHSimClockSubsystem::SolLengthSimSeconds;
	const double LegTime = ConvoyLegSolsFor(ConvoyRival);
	ConvoyLegSols += DtSols;
	if (ConvoyLegSols < LegTime)
	{
		return; // still en route on this leg
	}
	if (!bConvoyReturning)
	{
		// Reached the rival; turn around (carry the spillover into the return).
		bConvoyReturning = true;
		ConvoyLegSols -= LegTime;
		return;
	}
	// Home again: deliver their export lot, warm the relation, go idle.
	ConvoyLegSols = 0.0;
	bConvoyReturning = false;
	const FName Done = ConvoyRival;
	ConvoyRival = NAME_None;
	const TMap<FName, double> TheirLot = URHDefinitionsSubsystem::ParseResourceList(Rival->ExportLot);
	for (const auto& Item : TheirLot)
	{
		if (Defs && Defs->IsSolidResource(Item.Key))
		{
			// Drop at the trade depot (the Lander) for normal hauling.
			bool bDropped = false;
			for (FRHBuildingInstance& B : Buildings)
			{
				if (B.DefName == FName("Lander") && !B.bUnderConstruction)
				{
					B.OutputKg.FindOrAdd(Item.Key) += Item.Value;
					bDropped = true;
					break;
				}
			}
			if (!bDropped) { AddStock(Item.Key, Item.Value); }
			OnStockChanged.Broadcast(Item.Key, GetTotalSolid(Item.Key));
		}
		else
		{
			AddStock(Item.Key, Item.Value); // fluids join the pool
		}
	}
	// The goods always land (paid for at dispatch - conservation), but the
	// DIPLOMATIC upside accrues only if the relationship survived the trip. A
	// rival that defected, embargoed you, or whose route you closed mid-route
	// gets no relation warming (defection's lock holds), no HumanNature nudge,
	// and no Influence - trade with a hostile party is cargo, not goodwill.
	// (Adversarial review, M4 C+D: an in-flight convoy could otherwise warm a
	// defected rival above its locked floor and mint Influence from hostility.)
	const bool bRelationshipIntact = !DefectedRivals.Contains(Done)
		&& !EmbargoGrace.Contains(Done) && !ClosedRoutes.Contains(Done);
	if (bRelationshipIntact)
	{
		double& Rel = RelationRef(Done);
		Rel = FMath::Clamp(Rel + RelationPerRun, 0.0, 100.0);
		// Fair dealing is who you are: an honest completed trade nudges the
		// HumanNature axis toward Evolved/Diplomatic (M4 Gate A) and earns
		// influence (M4 Gate C - the peaceful path earns the tools of peace).
		HumanNatureAxis = FMath::Clamp(HumanNatureAxis + HumanNatureFairTradeShift, -100.0, 100.0);
		Influence += InfluencePerTrade;
		OnAlert.Broadcast(FString::Printf(
			TEXT("CONVOY HOME — %s delivered (%s). Relations with %s now %.0f."),
			*Rival->DisplayName, *Rival->ExportLot, *Rival->Nation, Rel));
	}
	else
	{
		OnAlert.Broadcast(FString::Printf(
			TEXT("CONVOY HOME — cargo from %s arrived, but the relationship soured while the rover was out. Goods only; no goodwill."),
			*Rival->DisplayName));
	}
	UE_LOG(LogRedHopeSim, Display, TEXT("=== CONVOY RETURN: %s delivered %s, relation %.0f%s ==="),
		*Rival->DisplayName, *Rival->ExportLot, GetRivalRelation(Done),
		bRelationshipIntact ? TEXT("") : TEXT(" (soured - goods only)"));
}

double URHSimWorldSubsystem::GetConvoyProgress() const
{
	if (ConvoyRival.IsNone())
	{
		return 0.0;
	}
	const double LegTime = ConvoyLegSolsFor(ConvoyRival);
	if (LegTime <= 0.0)
	{
		return 0.0;
	}
	const double Legs = (bConvoyReturning ? 1.0 : 0.0) + FMath::Clamp(ConvoyLegSols / LegTime, 0.0, 1.0);
	return FMath::Clamp(Legs / 2.0, 0.0, 1.0);
}

double URHSimWorldSubsystem::GetRivalRelation(FName Rival) const
{
	if (const double* R = RivalRelations.Find(Rival))
	{
		return *R;
	}
	const FRHRivalRow* Row = Defs ? Defs->GetRival(Rival) : nullptr;
	return Row ? Row->RelationStart : 0.0;
}

bool URHSimWorldSubsystem::HasActiveRival() const
{
	if (DiscoveredRivals.Num() > 0)
	{
		return true; // a scouted settlement counts, even if its row ships dormant
	}
	bool bAny = false;
	if (Defs)
	{
		Defs->ForEachRival([&bAny](FName, const FRHRivalRow&){ bAny = true; });
	}
	return bAny;
}

bool URHSimWorldSubsystem::IsNight() const
{
	// Sunrise at sol-fraction 0.25, sunset at 0.75 (the sun rig's convention).
	const float Frac = Clock ? Clock->GetSolFraction() : 0.5f;
	return Frac < 0.25f || Frac > 0.75f;
}

double URHSimWorldSubsystem::GetHiddenTension(FName Rival) const
{
	const double* T = HiddenTensions.Find(Rival);
	return T ? *T : 0.0;
}

bool URHSimWorldSubsystem::IsRivalAvailable(FName Rival) const
{
	const FRHRivalRow* Row = Defs ? Defs->GetRival(Rival) : nullptr;
	return Row && (Row->SliceActive || DiscoveredRivals.Contains(Rival));
}

bool URHSimWorldSubsystem::RollCovertDetected(FName Rival)
{
	// The shared covert detection roll (requisition + sabotage). Deterministic:
	// a CONTENT hash of the rival name (FCrc::StrCrc32 - stable across runs,
	// unlike GetTypeHash(FName) which uses the process-unstable comparison
	// index; adversarial-review 2026-07-07) combined with the SAVED attempt
	// counter. A trusting ally watches you less (betrayal is easy, the moral
	// trap); night halves the eyes on you (Module 2 visibility).
	const uint32 Seed = HashCombine(FCrc::StrCrc32(*Rival.ToString()), (uint32)(CovertAttempts * 2654435761u));
	++CovertAttempts;
	const double U = (Seed % 100000u) / 100000.0; // [0,1)
	const double Relation = GetRivalRelation(Rival);
	double P = CovertDetectionBase * (1.0 - (Relation / 100.0) * CovertDetectionTrustMul);
	if (IsNight())
	{
		P *= CovertDetectionNightMul;
	}
	P = FMath::Clamp(P, 0.0, 1.0);
	return U < P;
}

bool URHSimWorldSubsystem::ResolveCovert(FName Rival, FString& OutReason)
{
	if (!IsRivalAvailable(Rival))
	{
		OutReason = FString::Printf(TEXT("No contact with '%s'"), *Rival.ToString());
		return false;
	}
	const FRHRivalRow* Row = Defs->GetRival(Rival);
	// Intent marks you regardless of outcome (the axis is about HOW you act).
	HumanNatureAxis = FMath::Clamp(HumanNatureAxis - HumanNatureCovertShift, -100.0, 100.0);

	const bool bCaught = RollCovertDetected(Rival);
	double& Hidden = HiddenTensionRef(Rival);
	if (bCaught)
	{
		Hidden = FMath::Clamp(Hidden + CovertHiddenTensionCaught, 0.0, 100.0);
		double& Rel = RelationRef(Rival);
		Rel = FMath::Clamp(Rel - CovertRelationHitCaught, 0.0, 100.0);
		OnAlert.Broadcast(FString::Printf(
			TEXT("COVERT OP EXPOSED — %s caught your team in the act. Relations with %s drop sharply."),
			*Row->DisplayName, *Row->Nation));
		UE_LOG(LogRedHopeSim, Display, TEXT("=== COVERT CAUGHT: %s (relation %.0f, hidden %.0f, humanNature %.0f) ==="),
			*Row->DisplayName, Rel, Hidden, HumanNatureAxis);
	}
	else
	{
		// Clean: take a fraction of their export goods (solids drop at the
		// Lander for hauling, fluids join the pool - like a delivery, unpaid).
		Hidden = FMath::Clamp(Hidden + CovertHiddenTensionClean, 0.0, 100.0);
		FString Stolen;
		for (const auto& Item : URHDefinitionsSubsystem::ParseResourceList(Row->ExportLot))
		{
			const double Amount = Item.Value * CovertLotFraction;
			if (Defs && Defs->IsSolidResource(Item.Key))
			{
				bool bDropped = false;
				for (FRHBuildingInstance& B : Buildings)
				{
					if (B.DefName == FName("Lander") && !B.bUnderConstruction)
					{
						B.OutputKg.FindOrAdd(Item.Key) += Amount;
						bDropped = true;
						break;
					}
				}
				if (!bDropped) { AddStock(Item.Key, Amount); }
				OnStockChanged.Broadcast(Item.Key, GetTotalSolid(Item.Key));
			}
			else
			{
				AddStock(Item.Key, Amount);
			}
			Stolen += FString::Printf(TEXT("%s%.0f %s"), Stolen.IsEmpty() ? TEXT("") : TEXT(", "), Amount, *Item.Key.ToString());
		}
		OnAlert.Broadcast(FString::Printf(
			TEXT("COVERT OP CLEAN — your people slipped away from %s with %s. No one saw a thing (tonight)."),
			*Row->DisplayName, *Stolen));
		UE_LOG(LogRedHopeSim, Display, TEXT("=== COVERT CLEAN: %s took %s (hidden %.0f, humanNature %.0f) ==="),
			*Row->DisplayName, *Stolen, Hidden, HumanNatureAxis);
	}
	return true;
}

double URHSimWorldSubsystem::GetSabotageRemaining(FName Rival) const
{
	const double* S = SabotageSols.Find(Rival);
	return S ? *S : 0.0;
}

bool URHSimWorldSubsystem::ResolveLaunder(FName Rival, FString& OutReason)
{
	// Launder: return goods to a rival to DEFUSE the HiddenTension you built - a
	// peace offering that costs their ImportLot (what they want from you), warms
	// public standing, and nudges HumanNature toward Evolved (making amends).
	if (!IsRivalAvailable(Rival))
	{
		OutReason = FString::Printf(TEXT("No contact with '%s' to make amends to"), *Rival.ToString());
		return false;
	}
	const FRHRivalRow* Row = Defs->GetRival(Rival);
	if (GetHiddenTension(Rival) <= 0.0)
	{
		OutReason = FString::Printf(TEXT("No hidden grievance with %s to defuse"), *Row->DisplayName);
		return false;
	}
	const TMap<FName, double> Offering = URHDefinitionsSubsystem::ParseResourceList(Row->ImportLot);
	if (!HasTradeLot(Offering))
	{
		OutReason = FString::Printf(TEXT("Short of the peace offering (%s)"), *Row->ImportLot);
		return false;
	}
	SpendTradeLot(Offering);
	double& Hidden = HiddenTensionRef(Rival);
	Hidden = FMath::Clamp(Hidden - LaunderHiddenDrop, 0.0, 100.0);
	double& Rel = RelationRef(Rival);
	Rel = FMath::Clamp(Rel + LaunderRelationBonus, 0.0, 100.0);
	HumanNatureAxis = FMath::Clamp(HumanNatureAxis + HumanNatureLaunderShift, -100.0, 100.0);
	Influence += InfluencePerLaunder; // visible goodwill builds diplomatic standing (M4 Gate C)
	OnAlert.Broadcast(FString::Printf(
		TEXT("AMENDS MADE — a gift returned to %s (%s). Hidden tension eases; relations warm."),
		*Row->DisplayName, *Row->ImportLot));
	UE_LOG(LogRedHopeSim, Display, TEXT("=== LAUNDER: %s (hidden %.0f, relation %.0f, humanNature %.0f) ==="),
		*Row->DisplayName, Hidden, Rel, HumanNatureAxis);
	return true;
}

bool URHSimWorldSubsystem::ResolveSabotage(FName Rival, FString& OutReason)
{
	// Sabotage: a covert blackout that disrupts a rival's trade/production for a
	// period (SabotageDurationSols). Detection-gated like a requisition; a
	// hostile act, so HumanNature drops harder. NO combat - just a deterministic
	// downtime the rival recovers from.
	if (!IsRivalAvailable(Rival))
	{
		OutReason = FString::Printf(TEXT("No such settlement '%s' to sabotage"), *Rival.ToString());
		return false;
	}
	const FRHRivalRow* Row = Defs->GetRival(Rival);
	HumanNatureAxis = FMath::Clamp(HumanNatureAxis - HumanNatureSabotageShift, -100.0, 100.0);

	const bool bCaught = RollCovertDetected(Rival);
	double& Hidden = HiddenTensionRef(Rival);
	Hidden = FMath::Clamp(Hidden + SabotageHiddenTension, 0.0, 100.0);
	if (bCaught)
	{
		double& Rel = RelationRef(Rival);
		Rel = FMath::Clamp(Rel - CovertRelationHitCaught, 0.0, 100.0);
		OnAlert.Broadcast(FString::Printf(
			TEXT("SABOTAGE EXPOSED — %s traced the blackout to you. Relations drop sharply; their systems are still down."),
			*Row->DisplayName));
	}
	else
	{
		OnAlert.Broadcast(FString::Printf(
			TEXT("SABOTAGE LANDS — %s's production goes dark. They will be offline for a time, and no one saw your hand."),
			*Row->DisplayName));
	}
	// The blackout lands whether or not you were seen (you still hit them).
	SabotageSols.FindOrAdd(Rival) = SabotageDurationSols;
	UE_LOG(LogRedHopeSim, Display, TEXT("=== SABOTAGE: %s offline %.0f sols (caught=%d, hidden %.0f, humanNature %.0f) ==="),
		*Row->DisplayName, SabotageDurationSols, (int32)bCaught, Hidden, HumanNatureAxis);
	return true;
}

double URHSimWorldSubsystem::GetRequisitionMultiplier() const
{
	// Inert (exactly 1.0) until you have neighbors: a pre-M3 colony's awards -
	// and every baseline - are untouched.
	if (!HasActiveRival())
	{
		return 1.0;
	}
	const double AxisFrac = IdentityAxis / 100.0;             // + = Martian
	const double EarthAlign = FMath::Max(0.0, -AxisFrac);     // loyalty
	const double MartianAlign = FMath::Max(0.0, AxisFrac);    // defiance
	const double Mult = 1.0
		+ RequisitionEarthBonus * EarthAlign
		- RequisitionMartianPenalty * MartianAlign
		- RequisitionTensionPenalty * (EarthTension / 100.0);
	return FMath::Clamp(Mult, 0.0, 1.0 + RequisitionEarthBonus);
}

void URHSimWorldSubsystem::StepEarth(float SubDt)
{
	// The Earth-politics layer switches on only once Mars has neighbors - a
	// lonely colony draws no scrutiny, so pre-M3 baselines stay byte-identical.
	if (!HasActiveRival())
	{
		return;
	}
	const double DtSols = SubDt / (double)URHSimClockSubsystem::SolLengthSimSeconds;
	// Background tension: Earth grows uneasier as Mars connects to itself.
	EarthTension = FMath::Clamp(EarthTension + EarthTensionDriftPerSol * DtSols, 0.0, 100.0);
	// A demand is generated once tension crosses the threshold (the hook the
	// Solidarity Dilemma resolves in Gate C; here it only telegraphs). Wording
	// is a Gate-D framing-review placeholder.
	if (!bEarthDemandPending && EarthTension >= EarthTensionDemandThreshold)
	{
		bEarthDemandPending = true;
		OnAlert.Broadcast(TEXT("EARTH TRANSMISSION — your sponsor nation is uneasy about your ties to the other settlements. A demand is coming."));
		UE_LOG(LogRedHopeSim, Display, TEXT("=== EARTH DEMAND PENDING (tension %.0f) ==="), EarthTension);
	}
	// The morale shock from the last Dilemma choice fades toward zero (exp form
	// = parity-exact across step sizes, like HopeSmoothed).
	if (SolidarityHope != 0.0)
	{
		SolidarityHope *= FMath::Exp(-DtSols / FMath::Max(SolidarityHopeTauSols, KINDA_SMALL_NUMBER));
		if (FMath::Abs(SolidarityHope) < 0.01) { SolidarityHope = 0.0; }
	}
	// Sabotage blackouts count down (linear, parity-safe); a rival recovers when
	// the timer hits zero. Iterate a snapshot so we can drop expired entries.
	if (SabotageSols.Num() > 0)
	{
		for (auto It = SabotageSols.CreateIterator(); It; ++It)
		{
			It.Value() -= DtSols;
			if (It.Value() <= 0.0)
			{
				const FRHRivalRow* Row = Defs ? Defs->GetRival(It.Key()) : nullptr;
				OnAlert.Broadcast(FString::Printf(TEXT("RECOVERED — %s's systems are back online."),
					Row ? *Row->DisplayName : *It.Key().ToString()));
				It.RemoveCurrent();
			}
		}
	}
	// Earth's pre-emptive pressure (M4 Gate C) shares the same live-once-you-have-
	// neighbors gate.
	StepPreemptive(SubDt);
	// Dynamic crises (M4 Gate D) - same gate; the finale's escalating pressure.
	StepCrisis(SubDt);

	// The ending milestone (M4 Gate D): fire once when the colony first arrives
	// at a definite political outcome (a strong, settled allegiance). Never
	// retracts - it's the story's declared direction, celebrated on arrival.
	if (!bEndingDeclared && !IsColonyQuiet()) // F7b: no path is declared over an empty colony
	{
		const ERHEnding E = GetProjectedEnding();
		if (E != ERHEnding::Undetermined)
		{
			bEndingDeclared = true;
			DeclaredEnding = (uint8)E; // the epilogue clock (v25) measures against this
			PathSustainSols = 0.0;
			OnAlert.Broadcast(FString::Printf(
				TEXT("A PATH IS SET — the colony's course reads clearly now: %s. History will remember which Mars you chose to build."),
				GetEndingName(E)));
			UE_LOG(LogRedHopeSim, Display, TEXT("=== ENDING PATH: %s (identity %.0f, humanNature %.0f) ==="),
				GetEndingName(E), IdentityAxis, HumanNatureAxis);
		}
	}
	// Ending resolution (v25): hold the declared path for EpilogueSustainSols
	// and the story is WRITTEN - a one-time epilogue card; the sandbox keeps
	// running. Wavering resets the clock (the path must be re-earned), the
	// declaration itself never retracts. The clock waits, neither running nor
	// resetting, while the colony is quiet (F7b): an epilogue is never written over an
	// empty colony. Threshold-on-monotone-accumulator = era-parity-safe, like growth.
	// Wording is a Gate-D review placeholder.
	if (bEndingDeclared && !bEpilogueDeclared && !IsColonyQuiet())
	{
		if ((uint8)GetProjectedEnding() == DeclaredEnding)
		{
			PathSustainSols += DtSols;
			if (PathSustainSols >= EpilogueSustainSols)
			{
				bEpilogueDeclared = true;
				const FString Card = FString::Printf(
					TEXT("EPILOGUE — the story of this Mars is written: %s. The colony carries it forward."),
					GetEndingName((ERHEnding)DeclaredEnding));
				OnAlert.Broadcast(Card);
				OnMilestone.Broadcast(Card);
				UE_LOG(LogRedHopeSim, Display, TEXT("=== EPILOGUE: %s (held %.1f sols) ==="),
					GetEndingName((ERHEnding)DeclaredEnding), PathSustainSols);
			}
		}
		else
		{
			PathSustainSols = 0.0;
		}
	}
}

double URHSimWorldSubsystem::GetEmbargoGrace(FName Rival) const
{
	const double* G = EmbargoGrace.Find(Rival);
	return G ? *G : 0.0;
}

FName URHSimWorldSubsystem::GetEmbargoingRival() const
{
	// The soonest-to-defect embargoing rival (deterministic: least grace, then
	// name), so the deck names one clear target to pacify.
	FName Best = NAME_None;
	double BestGrace = TNumericLimits<double>::Max();
	for (const auto& Pair : EmbargoGrace)
	{
		if (Pair.Value < BestGrace || (Pair.Value == BestGrace && (Best.IsNone() || Pair.Key.LexicalLess(Best))))
		{
			BestGrace = Pair.Value;
			Best = Pair.Key;
		}
	}
	return Best;
}

void URHSimWorldSubsystem::StepPreemptive(float SubDt)
{
	const double DtSols = SubDt / (double)URHSimClockSubsystem::SolLengthSimSeconds;

	// Influence drifts up scaled by POSITIVE HumanNature - the Evolved path earns
	// the diplomatic capital that buys pacification. Linear = parity-safe.
	const double Evolved = FMath::Max(0.0, HumanNatureAxis / 100.0);
	Influence += InfluenceDriftPerSol * Evolved * DtSols;

	// Embargo grace timers count down; expiry hardens into a DEFECTION.
	if (EmbargoGrace.Num() > 0)
	{
		for (auto It = EmbargoGrace.CreateIterator(); It; ++It)
		{
			It.Value() -= DtSols;
			if (It.Value() <= 0.0)
			{
				const FName Name = It.Key();
				It.RemoveCurrent();
				DefectedRivals.Add(Name);
				double& Rel = RelationRef(Name);
				Rel = FMath::Min(Rel, DefectRelationFloor);
				const FRHRivalRow* Row = Defs ? Defs->GetRival(Name) : nullptr;
				OnAlert.Broadcast(FString::Printf(
					TEXT("DEFECTION — %s has sided with Earth. The route is closed and relations are frozen."),
					Row ? *Row->DisplayName : *Name.ToString()));
				UE_LOG(LogRedHopeSim, Display, TEXT("=== DEFECTION: %s (relation %.0f) ==="),
					Row ? *Row->DisplayName : *Name.ToString(), Rel);
			}
		}
	}

	// Trigger a new embargo when Earth tension is high, nobody is currently
	// embargoed (one incoming crisis at a time), and there is an eligible target
	// (available, not closed, not already defected). Deterministic pick: highest
	// public relation - Earth leans hardest on your closest friend (the betrayal
	// hurts most, and matches the "trusting ally" moral trap of the covert layer).
	if (EarthTension >= EarthPreemptiveThreshold && EmbargoGrace.Num() == 0)
	{
		FName Target = NAME_None;
		double BestRel = -1.0;
		if (Defs)
		{
			Defs->ForEachRivalRow([&](FName Name, const FRHRivalRow&)
			{
				if (!IsRivalAvailable(Name) || ClosedRoutes.Contains(Name) || DefectedRivals.Contains(Name))
				{
					return;
				}
				const double Rel = GetRivalRelation(Name);
				if (Rel > BestRel || (Rel == BestRel && (Target.IsNone() || Name.LexicalLess(Target))))
				{
					BestRel = Rel;
					Target = Name;
				}
			});
		}
		if (!Target.IsNone())
		{
			EmbargoGrace.Add(Target, EmbargoGraceSols);
			const FRHRivalRow* Row = Defs->GetRival(Target);
			OnAlert.Broadcast(FString::Printf(
				TEXT("EARTH LEANS ON %s — under pressure from Earth, they have suspended trade with you. Reassure them within %.0f sols or they side with Earth."),
				Row ? *Row->DisplayName : *Target.ToString(), EmbargoGraceSols));
			UE_LOG(LogRedHopeSim, Display, TEXT("=== EMBARGO: %s (Earth tension %.0f, grace %.0f sols) ==="),
				Row ? *Row->DisplayName : *Target.ToString(), EarthTension, EmbargoGraceSols);
		}
	}
}

bool URHSimWorldSubsystem::ResolvePacify(FName Rival, FString& OutReason)
{
	if (!EmbargoGrace.Contains(Rival))
	{
		OutReason = FString::Printf(TEXT("%s is not under an Earth embargo to lift"), *Rival.ToString());
		return false;
	}
	if (Influence < PacifyInfluenceCost)
	{
		OutReason = FString::Printf(TEXT("Not enough Influence to reassure them (%.0f needed, %.0f on hand)"),
			PacifyInfluenceCost, Influence);
		return false;
	}
	Influence -= PacifyInfluenceCost;
	EmbargoGrace.Remove(Rival);
	EarthTension = FMath::Clamp(EarthTension - PacifyTensionRelief, 0.0, 100.0);
	HumanNatureAxis = FMath::Clamp(HumanNatureAxis + HumanNaturePacifyShift, -100.0, 100.0);
	const FRHRivalRow* Row = Defs ? Defs->GetRival(Rival) : nullptr;
	OnAlert.Broadcast(FString::Printf(
		TEXT("REASSURED — %s sets Earth's order aside. Trade resumes; tension eases."),
		Row ? *Row->DisplayName : *Rival.ToString()));
	UE_LOG(LogRedHopeSim, Display, TEXT("=== PACIFIED: %s (influence %.0f, tension %.0f) ==="),
		Row ? *Row->DisplayName : *Rival.ToString(), Influence, EarthTension);
	return true;
}

const TCHAR* URHSimWorldSubsystem::GetActiveCrisisName() const
{
	switch (ActiveCrisis)
	{
	case ERHCrisis::Malfunction:  return TEXT("SYSTEMS MALFUNCTION");
	case ERHCrisis::Environmental: return TEXT("DRY SPELL");
	default: return TEXT("none");
	}
}

void URHSimWorldSubsystem::TriggerCrisis()
{
	// The alignment-gated selector (the spec's thematic payoff): a DESTRUCTIVE
	// colony (HumanNature below the split) draws a self-inflicted Malfunction -
	// its own predatory conduct comes home as sabotage-blowback / a systems
	// blackout; an EVOLVED colony draws an external Environmental test it can
	// weather with reserves + cooperation. Deterministic - the axis decides.
	ActiveCrisis = (HumanNatureAxis < CrisisAlignmentThreshold)
		? ERHCrisis::Malfunction : ERHCrisis::Environmental;
	CrisisRemainingSols = CrisisDurationSols;
	++CrisisCount;
	if (ActiveCrisis == ERHCrisis::Malfunction)
	{
		OnAlert.Broadcast(TEXT("SYSTEMS MALFUNCTION — blowback from covert work: automation stalls, units go idle, work slows. It passes in a few sols."));
		UE_LOG(LogRedHopeSim, Display, TEXT("=== CRISIS: MALFUNCTION (%.0f sols, humanNature %.0f) ==="), CrisisDurationSols, HumanNatureAxis);
	}
	else
	{
		OnAlert.Broadcast(TEXT("DRY SPELL — the colony's water draw spikes for a few sols. Keep the tanks full and it passes."));
		UE_LOG(LogRedHopeSim, Display, TEXT("=== CRISIS: ENVIRONMENTAL (%.0f sols, humanNature %.0f) ==="), CrisisDurationSols, HumanNatureAxis);
	}
}

void URHSimWorldSubsystem::StepCrisis(float SubDt)
{
	// Live only once the late game is on (has neighbors) - pre-M3 baselines have
	// no rival, so ActiveCrisis stays None and nothing here runs: byte-identical.
	if (!HasActiveRival())
	{
		return;
	}
	const double DtSols = SubDt / (double)URHSimClockSubsystem::SolLengthSimSeconds;

	if (ActiveCrisis != ERHCrisis::None)
	{
		// Apply the ongoing effect, then count down. Environmental drains Water
		// (a strain on reserves); Malfunction's work/Hope hit is read live in
		// GetHumanWorkTempo / GetColonyHope while active (no per-step mutation).
		if (ActiveCrisis == ERHCrisis::Environmental)
		{
			static const FName NWater(TEXT("Water"));
			const double Draw = CrisisWaterDrainPerSol * DtSols;
			AddStock(NWater, -FMath::Min(Draw, GetStock(NWater)));
		}
		CrisisRemainingSols -= DtSols;
		if (CrisisRemainingSols <= 0.0)
		{
			const ERHCrisis Was = ActiveCrisis;
			ActiveCrisis = ERHCrisis::None;
			CrisisRemainingSols = 0.0;
			OnAlert.Broadcast(Was == ERHCrisis::Malfunction
				? TEXT("RECOVERED — the malfunction is contained; automation is coming back online.")
				: TEXT("WEATHERED — the dry spell has passed; the colony held."));
		}
		return; // one crisis at a time; the interval clock waits
	}

	// No crisis active: accumulate toward the next check. Every interval, a
	// deterministic seeded roll (crisis count) decides whether one fires.
	CrisisCheckSols += DtSols;
	if (CrisisCheckSols >= CrisisIntervalSols)
	{
		CrisisCheckSols -= CrisisIntervalSols;
		const uint32 Seed = HashCombine(FCrc::StrCrc32(TEXT("crisis")), (uint32)(CrisisCount * 2654435761u));
		const double U = (Seed % 100000u) / 100000.0;
		if (U < CrisisChance)
		{
			TriggerCrisis();
		}
		else
		{
			++CrisisCount; // a quiet interval still advances the seed sequence
		}
	}
}

bool URHSimWorldSubsystem::HasInfirmaryOnline() const
{
	// Passive presence check: any slice-active Infirmary-function cell on a
	// RATED floor. Cheap at colony scale (a handful of floors x cells).
	static const FName NInfirmary(TEXT("Infirmary"));
	if (!Defs)
	{
		return false;
	}
	for (const int32 Level : RatedFloors)
	{
		if (const TArray<FName>* Cells = FloorRoomCells.Find(Level))
		{
			for (const FName& CellRoom : *Cells)
			{
				const FRHRoomRow* Row = Defs->GetRoom(CellRoom);
				if (Row && Row->SliceActive && Row->Function == NInfirmary)
				{
					return true;
				}
			}
		}
	}
	// BM-1: an Infirmary bay in a rated surface network.
	for (const FRHHabNetCache& Net : HabNets)
	{
		if (!IsNetRated(Net))
		{
			continue;
		}
		for (const int32 HabId : Net.Members)
		{
			const FRHHabMemberCache& M = HabMembers.FindChecked(HabId);
			for (int32 Bay = 0; Bay < M.NX * M.NY; ++Bay)
			{
				const FRHRoomRow* Row = Defs->GetRoom(GetBayRoom(HabId, Bay));
				if (Row && Row->SliceActive && Row->Function == NInfirmary)
				{
					return true;
				}
			}
		}
	}
	// A prefab Infirmary module counts too (tiered production T1).
	for (const FRHBuildingInstance& B : Buildings)
	{
		const FRHRoomRow* Row = GetProvidedRoom(B, /*bRequireOnline*/ true);
		if (Row && Row->Function == NInfirmary)
		{
			return true;
		}
	}
	return false;
}

URHSimWorldSubsystem::ERHEnding URHSimWorldSubsystem::GetProjectedEnding() const
{
	// The two axes decide the political outcome once an allegiance is definite.
	if (IdentityAxis <= -EndingIdentityThreshold)
	{
		return ERHEnding::CorporateJewel;         // loyal to Earth
	}
	if (IdentityAxis >= EndingIdentityThreshold)
	{
		return (HumanNatureAxis >= EndingHumanNatureThreshold)
			? ERHEnding::IndependentFederation    // Martian + Evolved: a free, cooperative Mars
			: ERHEnding::MartianColdWar;          // Martian + Destructive/contested: a fractured, hostile Mars
	}
	return ERHEnding::Undetermined;               // still contested
}

const TCHAR* URHSimWorldSubsystem::GetEndingName(ERHEnding Ending) const
{
	switch (Ending)
	{
	case ERHEnding::CorporateJewel:        return TEXT("Corporate Jewel of Earth");
	case ERHEnding::IndependentFederation: return TEXT("Independent Mars Federation");
	case ERHEnding::MartianColdWar:        return TEXT("A Divided Mars");
	default: return TEXT("Undetermined");
	}
}

bool URHSimWorldSubsystem::ResolveDilemma(bool bComply, FString& OutReason)
{
	if (!bEarthDemandPending)
	{
		OutReason = TEXT("There is no demand to answer right now.");
		return false;
	}
	bEarthDemandPending = false;

	if (bComply)
	{
		// COMPLY: cut trade with the settlements. Every open route CLOSES for
		// good (you lose whatever that trade fed - the ice that kept your water
		// loop, say); relations crater; identity swings Earth-ward; the demand
		// is satisfied so tension falls hard; the colony grieves a shrunken world.
		int32 Closed = 0;
		if (Defs)
		{
			Defs->ForEachRival([&](FName Name, const FRHRivalRow&)
			{
				if (!ClosedRoutes.Contains(Name))
				{
					ClosedRoutes.Add(Name);
					double& Rel = RelationRef(Name);
					Rel = FMath::Clamp(Rel - DilemmaRelationHit, 0.0, 100.0);
					++Closed;
				}
			});
		}
		// A convoy already committed to a now-closed rival still comes home
		// (the goods were paid for); only NEW dispatches are refused.
		IdentityAxis = FMath::Clamp(IdentityAxis - DilemmaComplyAxisShift, -100.0, 100.0);
		EarthTension = FMath::Clamp(EarthTension - DilemmaComplyTensionDrop, 0.0, 100.0);
		SolidarityHope -= SolidarityHopeShock; // morale grief (fades)
		OnAlert.Broadcast(FString::Printf(
			TEXT("YOU COMPLIED — %d trade route(s) severed to satisfy Earth. Requisitions restored; the community feels smaller tonight."), Closed));
		UE_LOG(LogRedHopeSim, Display, TEXT("=== SOLIDARITY: COMPLIED - %d route(s) closed, axis %.0f, tension %.0f ==="),
			Closed, IdentityAxis, EarthTension);
	}
	else
	{
		// DEFY: keep faith with the settlements. Identity swings Martian, every
		// relation warms, morale lifts - but Earth's requisitions stay slashed by
		// your Martian axis (Gate B), and tension barely eases, so the next
		// demand comes sooner. Solidarity has a price you pay in Earth's supply.
		IdentityAxis = FMath::Clamp(IdentityAxis + DilemmaDefyAxisShift, -100.0, 100.0);
		EarthTension = FMath::Clamp(EarthTension - DilemmaDefyTensionDrop, 0.0, 100.0);
		SolidarityHope += SolidarityHopeShock; // pride (fades)
		if (Defs)
		{
			Defs->ForEachRival([&](FName Name, const FRHRivalRow&)
			{
				double& Rel = RelationRef(Name);
				Rel = FMath::Clamp(Rel + DilemmaDefyRelationBonus, 0.0, 100.0);
			});
		}
		OnAlert.Broadcast(TEXT("YOU DEFIED EARTH — the settlements stand together. Hope rises, but your sponsor's supply priority is cut. Mars will have to provide."));
		UE_LOG(LogRedHopeSim, Display, TEXT("=== SOLIDARITY: DEFIED - axis %.0f, tension %.0f, requisition x%.2f ==="),
			IdentityAxis, EarthTension, GetRequisitionMultiplier());
	}
	return true;
}

void URHSimWorldSubsystem::AddBuilding(FName DefName, const FVector& LocationCm, bool bInstant, int32 Level, int32 SizeX, int32 SizeY)
{
	FRHBuildingInstance Instance;
	Instance.Id = NextBuildingId++;
	Instance.DefName = DefName;
	Instance.LocationCm = LocationCm;
	Instance.Level = Level;
	Instance.LocationCm.Z = Level * FloorHeightCm; // Z is derived, never authored

	const FRHBuildingRow* Def = Defs->GetBuilding(DefName);
	// D5a: a new heavy machine starts on the day shift where the config says so.
	// Every placement path (uplink Build, debug, showcase) routes through here.
	Instance.bDayShift = Def && CanDayShift(DefName) && IsDayShiftDefault(DefName);
	if (Def && Def->DragSized)
	{
		const int32 Cap = GetMaxBedsPerSide(Def);
		Instance.SizeX = FMath::Clamp(SizeX, 1, Cap);
		Instance.SizeY = FMath::Clamp(SizeY, 1, Cap);
		if (Def->DragShortSideMax > 0)
		{
			int32& Short = Instance.SizeX <= Instance.SizeY ? Instance.SizeX : Instance.SizeY;
			Short = FMath::Min(Short, Def->DragShortSideMax);
		}
	}
	if (Def && Def->Pressurized)
	{
		// BM-1: every shell sits on the bay grid, even from cheat/debug paths
		// that skip CanPlace (identity for a CanPlace-checked order).
		Instance.LocationCm = SnapToBayGrid(Def, Instance.LocationCm, Instance.SizeX, Instance.SizeY);
	}
	if (!bInstant && Def && Def->BuildTime_s > 0.0)
	{
		Instance.bUnderConstruction = true;
		Instance.BuildRemaining_s = Def->BuildTime_s * GetBedCount(Instance); // a bigger dome takes longer
	}
	if (Def && !Instance.bUnderConstruction)
	{
		// Packs arrive half-charged. Constructed storage gets its credit at
		// completion instead (M0-c bug: crediting at order time clamped the
		// energy away because capacity only counts completed banks).
		Power.BatteryWh += Def->StorageWh * 0.5;
	}
	// Extraction buildings bind to the deposit under them (within 12 m).
	// Undiscovered ground never attaches: you cannot drill what the colony
	// has not surveyed, even by console coordinates.
	if (Def && Def->RequiresDeposit)
	{
		for (const FRHDepositState& D : Deposits)
		{
			if (D.Level == Level && D.bDiscovered && FVector::DistXY(D.LocationCm, LocationCm) <= 1200.0)
			{
				Instance.AttachedDepositId = D.Id;
				break;
			}
		}
		if (Instance.AttachedDepositId == 0)
		{
			UE_LOG(LogRedHopeSim, Warning, TEXT("%s placed with no deposit within 12 m - it will never produce"), *DefName.ToString());
		}
	}

	const int32 NewId = Instance.Id;
	Buildings.Add(MoveTemp(Instance));
	RebuildHabTopology(); // BM-1: before any listener reads the new building
	const FRHBuildingInstance& Added = Buildings.Last();

	if (Added.bUnderConstruction)
	{
		FRHTask T;
		T.Id = NextTaskId++;
		T.Type = ERHTaskType::Build;
		T.To.BuildingId = NewId;
		Tasks.Add(T);
		UE_LOG(LogRedHopeSim, Display, TEXT("Construction site: %s #%d at (%.0f, %.0f) m (%.0f s of fabrication)"),
			*DefName.ToString(), NewId, Added.LocationCm.X / 100.0, Added.LocationCm.Y / 100.0, Added.BuildRemaining_s);
	}
	else
	{
		UE_LOG(LogRedHopeSim, Display, TEXT("Built %s #%d at (%.0f, %.0f) m"),
			*DefName.ToString(), NewId, Added.LocationCm.X / 100.0, Added.LocationCm.Y / 100.0);
	}
	OnBuildingAdded.Broadcast(Added);
}

// --- Demolish (BM-3, 2026-10-16) ---

void URHSimWorldSubsystem::ComputeSalvage(const FRHBuildingInstance& B, TMap<FName, double>& OutIn, TMap<FName, double>& OutOut, int32& OutPacks) const
{
	// Every kg keeps its shelf: held inputs stay inputs (the Forge can still
	// draw them from the Lander), outputs and the reclaimed hull are outputs.
	OutIn.Reset();
	OutOut.Reset();
	OutPacks = 0;
	for (const auto& L : B.InputKg)
	{
		if (L.Value > KINDA_SMALL_NUMBER)
		{
			OutIn.FindOrAdd(L.Key) += L.Value;
		}
	}
	for (const auto& L : B.OutputKg)
	{
		if (L.Value > KINDA_SMALL_NUMBER)
		{
			OutOut.FindOrAdd(L.Key) += L.Value;
		}
	}
	const FRHBuildingRow* Def = Defs ? Defs->GetBuilding(B.DefName) : nullptr;
	if (B.bUnderConstruction)
	{
		// A cancelled site: what was delivered is already in InputKg; an
		// imported kit goes back in its crate.
		OutPacks = (Def && Def->ImportOnly) ? 1 : 0;
		return;
	}
	// A standing structure: part of its hull comes back. Imported hardware
	// has no bill, so it salvages nothing (no free half-charged bank).
	if (SalvageFraction > 0.0)
	{
		for (const auto& L : GetSiteBill(B))
		{
			OutOut.FindOrAdd(L.Key) += L.Value * SalvageFraction;
		}
	}
	// BM-6: and the fittings on its walls.
	if (Def && Def->Pressurized && WallFeatures.Num() > 0)
	{
		TSet<int32> OnWalls;
		GetFeaturesTouchingHab(B.Id, OnWalls);
		for (const auto& L : WallFeatureRefundFor(OnWalls))
		{
			OutOut.FindOrAdd(L.Key) += L.Value;
		}
	}
	// BM-4: a habitat's furnishings are sold back with it.
	if (Def && Def->Pressurized && Furniture.Num() > 0)
	{
		TSet<int32> Inside;
		for (const FRHFurnitureInstance& F : Furniture)
		{
			if (F.Key.Z == B.Id)
			{
				Inside.Add(F.Id);
			}
		}
		for (const auto& L : FurnitureRefundFor(Inside))
		{
			OutOut.FindOrAdd(L.Key) += L.Value;
		}
	}
}

bool URHSimWorldSubsystem::CanDemolishBuilding(int32 Id, FString& OutReason, TMap<FName, double>* OutSalvage) const
{
	const FRHBuildingInstance* B = Defs ? FindBuildingById(Id) : nullptr;
	if (!B)
	{
		OutReason = FString::Printf(TEXT("No structure #%d - it is already gone"), Id);
		return false;
	}
	const FRHBuildingRow* Def = Defs->GetBuilding(B->DefName);
	if (B->DefName == NAME_Lander)
	{
		OutReason = TEXT("The Lander is the colony's anchor - it cannot be demolished");
		return false;
	}
	if (Def && !B->bUnderConstruction && Def->CoverageRadius_m > 0.f)
	{
		OutReason = TEXT("Grid pylons hold your territory - a standing pylon cannot be demolished yet");
		return false;
	}
	if (Def && Def->CanBore && ((!B->bUnderConstruction && ShaftDepth > 0) || PendingBoreWork.Contains(Id)))
	{
		OutReason = TEXT("The Borer anchors the shaft - it stays once boring has begun");
		return false;
	}
	int32 Residents = 0;
	for (const FRHColonist& C : Colonists)
	{
		Residents += C.HomeHab == Id ? 1 : 0;
	}
	if (Residents > 0)
	{
		OutReason = FString::Printf(TEXT("%d colonist(s) live in this habitat - it cannot be demolished while it is home"), Residents);
		return false;
	}
	const bool bShell = Def && Def->Pressurized && B->Level == 0;
	if (bShell)
	{
		// Machines standing in its bays (any build state) - clear it first.
		int32 Inside = 0;
		const FRHBuildingInstance* First = nullptr;
		for (const FRHBuildingInstance& X : Buildings)
		{
			const FRHBuildingRow* XDef = X.Level == 0 && X.Id != Id ? Defs->GetBuilding(X.DefName) : nullptr;
			int32 H = 0, Bay = INDEX_NONE;
			if (XDef && !XDef->Pressurized && FindBayAt(X.LocationCm, H, Bay) && H == Id)
			{
				First = First ? First : &X;
				++Inside;
			}
		}
		if (Inside > 0)
		{
			OutReason = FString::Printf(TEXT("Clear the habitat first - %d machine(s) stand inside (%s #%d)"), Inside, *First->DefName.ToString(), First->Id);
			return false;
		}
	}
	// The colonists each network piece would still house.
	const auto NetResidents = [this](const FRHHabNetCache& N)
	{
		int32 Count = 0;
		for (const FRHColonist& C : Colonists)
		{
			Count += (C.HomeHab != 0 && N.Members.Contains(C.HomeHab)) ? 1 : 0;
		}
		return Count;
	};
	const FRHHabNetCache* Net = (bShell && !B->bUnderConstruction) ? FindHabNet(Id) : nullptr;
	if (Net)
	{
		// A split: every piece that still houses someone must keep the bay
		// minimum and its own air (a circulator in its bays, or the shaft link).
		TArray<int32> Rest = Net->Members;
		Rest.Remove(Id);
		TMap<int32, int32> PieceOf;
		int32 Pieces = 0;
		for (const int32 Seed : Rest)
		{
			if (PieceOf.Contains(Seed))
			{
				continue;
			}
			TArray<int32> Stack = { Seed };
			PieceOf.Add(Seed, Pieces);
			while (Stack.Num())
			{
				const int32 Cur = Stack.Pop();
				const FRHHabMemberCache& M = HabMembers.FindChecked(Cur);
				for (int32 BY = 0; BY < M.NY; ++BY)
				{
					for (int32 BX = 0; BX < M.NX; ++BX)
					{
						for (const FIntPoint D : { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) })
						{
							const int32* Other = HabBayOwner.Find(M.GridMin + FIntPoint(BX, BY) + D);
							if (Other && *Other != Id && *Other != Cur && !PieceOf.Contains(*Other))
							{
								PieceOf.Add(*Other, Pieces);
								Stack.Add(*Other);
							}
						}
					}
				}
			}
			++Pieces;
		}
		TArray<int32> Bays, Homes;
		TArray<bool> Air;
		Bays.SetNumZeroed(Pieces);
		Homes.SetNumZeroed(Pieces);
		Air.Init(false, Pieces);
		for (const int32 M : Rest)
		{
			const FRHHabMemberCache& MC = HabMembers.FindChecked(M);
			const int32 P = PieceOf.FindChecked(M);
			Bays[P] += MC.NX * MC.NY;
			const FRHBuildingInstance* MB = FindBuildingById(M);
			for (int32 Bay = 0; MB && RatedFloors.Contains(-1) && Bay < MC.NX * MC.NY; ++Bay)
			{
				const FVector C = GetBayCentreCm(*MB, Bay);
				if (FMath::Max(FMath::Abs(C.X - ShaftHeadCm.X), FMath::Abs(C.Y - ShaftHeadCm.Y)) <= ShaftLinkRangeCm + 0.5)
				{
					Air[P] = true;
				}
			}
		}
		for (const int32 Index : Net->Circulators)
		{
			int32 H = 0, Bay = INDEX_NONE;
			if (Buildings.IsValidIndex(Index) && FindMemberBayAt(Buildings[Index].LocationCm, H, Bay) && PieceOf.Contains(H))
			{
				Air[PieceOf[H]] = true;
			}
		}
		for (const FRHColonist& C : Colonists)
		{
			if (const int32* P = C.HomeHab != 0 ? PieceOf.Find(C.HomeHab) : nullptr)
			{
				++Homes[*P];
			}
		}
		for (int32 P = 0; P < Pieces; ++P)
		{
			if (Homes[P] > 0 && Bays[P] < MinLivableCells)
			{
				OutReason = FString::Printf(TEXT("Demolishing this would leave %d resident(s) in a habitat under the %d-bay minimum"), Homes[P], MinLivableCells);
				return false;
			}
			if (Homes[P] > 0 && !Air[P])
			{
				OutReason = FString::Printf(TEXT("Demolishing this would leave %d resident(s) in a habitat with no air circulator"), Homes[P]);
				return false;
			}
		}
	}
	if (Def && Def->CirculatesAir && !B->bUnderConstruction)
	{
		// The last air for the people who breathe it.
		int32 Breathing = 0;
		if (B->Level < 0)
		{
			bool bOther = false;
			for (const FRHBuildingInstance& X : Buildings)
			{
				const FRHBuildingRow* XDef = (X.Id != Id && X.Level == B->Level && !X.bUnderConstruction) ? Defs->GetBuilding(X.DefName) : nullptr;
				if (XDef && XDef->CirculatesAir)
				{
					bOther = true;
				}
			}
			if (!bOther)
			{
				for (const FRHColonist& C : Colonists)
				{
					Breathing += (C.HomeHab == 0 && C.HomeLevel == B->Level) ? 1 : 0;
				}
				if (B->Level == -1)
				{
					// ...and the habitats whose only air comes down the shaft link.
					for (const FRHHabNetCache& N : HabNets)
					{
						Breathing += (N.Circulators.Num() == 0 && IsNetShaftLinked(N)) ? NetResidents(N) : 0;
					}
				}
			}
		}
		else
		{
			int32 H = 0, Bay = INDEX_NONE;
			const FRHHabNetCache* In = FindMemberBayAt(B->LocationCm, H, Bay) ? FindHabNet(H) : nullptr;
			if (In && In->Circulators.Num() <= 1 && !IsNetShaftLinked(*In))
			{
				Breathing = NetResidents(*In);
			}
		}
		if (Breathing > 0)
		{
			OutReason = FString::Printf(TEXT("This is the only air circulator for %d resident(s) - build another first"), Breathing);
			return false;
		}
	}
	if (OutSalvage)
	{
		TMap<FName, double> In, Out;
		int32 Packs = 0;
		ComputeSalvage(*B, In, Out, Packs);
		OutSalvage->Reset();
		for (const auto& L : In)
		{
			OutSalvage->FindOrAdd(L.Key) += L.Value;
		}
		for (const auto& L : Out)
		{
			OutSalvage->FindOrAdd(L.Key) += L.Value;
		}
		if (Packs > 0)
		{
			OutSalvage->FindOrAdd(B->DefName) += Packs; // the kit itself, back in stock
		}
	}
	return true;
}

void URHSimWorldSubsystem::RemoveBuilding(int32 Id)
{
	const int32 Index = Buildings.IndexOfByPredicate([Id](const FRHBuildingInstance& X) { return X.Id == Id; });
	if (Index == INDEX_NONE || !Defs)
	{
		return;
	}
	// A copy: nothing may hold a pointer into Buildings across the RemoveAt.
	const FRHBuildingInstance Gone = Buildings[Index];
	const FRHBuildingRow* Def = Defs->GetBuilding(Gone.DefName);
	TMap<FName, double> SalvIn, SalvOut;
	int32 Packs = 0;
	ComputeSalvage(Gone, SalvIn, SalvOut, Packs);
	int32 LanderId = 0;
	for (const FRHBuildingInstance& B : Buildings)
	{
		if (B.DefName == NAME_Lander)
		{
			LanderId = B.Id;
			break;
		}
	}

	// The task board. Unclaimed work for it is void; a fabricator on the site
	// finds it gone (ApplyBuildWork reports done) and moves on; cargo already
	// bound for it comes home to the Lander instead. A haul FROM it finds an
	// empty source at load time and clears itself.
	Tasks.RemoveAll([Id](const FRHTask& T)
	{
		if (T.From.BuildingId != Id && T.To.BuildingId != Id)
		{
			return false;
		}
		return !T.ClaimedBy.IsValid() || T.Type == ERHTaskType::Build;
	});
	for (FRHTask& T : Tasks)
	{
		if (T.Type == ERHTaskType::Haul && T.To.BuildingId == Id && LanderId != 0)
		{
			T.To.BuildingId = LanderId;
		}
	}

	// Everything keyed by its Id.
	const bool bBatch = PendingOutputs.Contains(Id) || Gone.BatchRemaining_h > 0.0;
	PendingOutputs.Remove(Id);
	PadQueues.Remove(Id);
	ensure(!PendingBoreWork.Contains(Id));
	SeatPriority.RemoveAll([Id](const FIntVector& K) { return K.Z == Id; });
	int32 BedsLost = 0;
	for (auto It = PlantedCells.CreateIterator(); It; ++It)
	{
		if (It->X == 0 && It->Z == Id)
		{
			PlantedCrops.Remove(*It);
			It.RemoveCurrent();
			++BedsLost;
		}
	}
	static const FName NOxygen(TEXT("Oxygen"));
	const double AirBack = HabO2Kg.FindRef(Id);
	HabO2Kg.Remove(Id);
	if (AirBack > 0.0)
	{
		AddStock(NOxygen, AirBack); // the hab's air is pumped back to the tanks
	}
	RatedHabs.Remove(Id);
	HabBayRooms.Remove(Id);
	++ZoningVersion; // C1: its bays are gone
	HabNetsNotedSmall.Remove(Id);
	// BM-6: the fittings on its walls (their refund is in the salvage too).
	TSet<int32> WallsGone;
	GetFeaturesTouchingHab(Id, WallsGone);
	TArray<FIntVector> WallEdgesGone;
	for (const FRHWallFeature& W : WallFeatures)
	{
		if (WallsGone.Contains(W.Id))
		{
			WallEdgesGone.Add(W.Edge);
		}
	}
	const int32 WallsSold = WallFeatures.RemoveAll([&WallsGone](const FRHWallFeature& W) { return WallsGone.Contains(W.Id); });
	if (WallsSold > 0)
	{
		RebuildWallFeatureIndex();
	}
	// BM-4: its furnishings (their refund is already in the salvage).
	TArray<FIntVector> FurnKeys;
	for (const FRHFurnitureInstance& F : Furniture)
	{
		if (F.Key.Z == Id)
		{
			FurnKeys.AddUnique(F.Key);
		}
	}
	const int32 FurnSold = Furniture.RemoveAll([Id](const FRHFurnitureInstance& F) { return F.Key.Z == Id; });
	if (FurnSold > 0)
	{
		RebuildFurnitureCache();
	}
	if (Def && !Gone.bUnderConstruction && Def->StorageWh > 0.f)
	{
		Power.BatteryCapWh = FMath::Max(0.0, Power.BatteryCapWh - Def->StorageWh);
		Power.BatteryWh = FMath::Min(Power.BatteryWh, Power.BatteryCapWh);
	}

	Buildings.RemoveAt(Index); // order-preserving: Buildings stays Id-ascending
	RebuildHabTopology();      // the network caches hold Buildings INDICES

	// The last Compute Module gone: the uplink falls back to tier 0 (never
	// past a manifest ComputeCore's tier 2).
	bool bLag = false;
	if (!Gone.bUnderConstruction && Gone.DefName == FName("ComputeModule")
		&& !Buildings.ContainsByPredicate([](const FRHBuildingInstance& B) { return B.DefName == FName("ComputeModule") && !B.bUnderConstruction; }))
	{
		const double Tier0 = Defs->GetConfigScalar(FName("OrderLagTier0_s"), 45.0);
		const double Tier1 = Defs->GetConfigScalar(FName("OrderLagTier1_s"), 20.0);
		if (FMath::IsNearlyEqual(OrderLagSeconds, Tier1) && Tier0 > OrderLagSeconds)
		{
			OrderLagSeconds = Tier0;
			bLag = true;
		}
	}

	// Salvage to the Lander (looked up after the array changed).
	FRHBuildingInstance* Lander = FindBuilding(LanderId);
	TArray<FName> Touched;
	FString Line;
	const auto Credit = [&](const TMap<FName, double>& Lines, bool bOutput)
	{
		for (const auto& L : Lines)
		{
			// Glass is a solid every consumer takes from the pool (greenhouse
			// glazing): it goes back there, never onto the Lander's shelf.
			if (Lander && Defs->IsSolidResource(L.Key) && L.Key != NAME_Glass)
			{
				(bOutput ? Lander->OutputKg : Lander->InputKg).FindOrAdd(L.Key) += L.Value;
			}
			else
			{
				AddStock(L.Key, L.Value);
			}
			Touched.AddUnique(L.Key);
		}
	};
	Credit(SalvIn, false);
	Credit(SalvOut, true);
	if (Packs > 0)
	{
		ImportStock.FindOrAdd(Gone.DefName) += Packs;
	}
	TMap<FName, double> Total;
	for (const FName& R : Touched)
	{
		Total.Add(R, SalvIn.FindRef(R) + SalvOut.FindRef(R));
	}
	Total.KeySort(FNameLexicalLess());
	for (const auto& L : Total)
	{
		Line += FString::Printf(TEXT("%s%.0f kg %s"), Line.IsEmpty() ? TEXT("") : TEXT(", "), L.Value, *L.Key.ToString());
		OnStockChanged.Broadcast(L.Key, Defs->IsSolidResource(L.Key) ? GetTotalSolid(L.Key) : GetStock(L.Key));
	}
	if (Packs > 0)
	{
		Line += FString::Printf(TEXT("%sthe %s kit"), Line.IsEmpty() ? TEXT("") : TEXT(", "), *Gone.DefName.ToString());
	}
	RefreshJobs();

	FString Notes;
	if (bBatch)
	{
		Notes += TEXT(" The batch in progress was scrapped.");
	}
	if (BedsLost > 0)
	{
		Notes += FString::Printf(TEXT(" %d planted bed(s) - the soil is forfeit."), BedsLost);
	}
	if (AirBack > 0.0)
	{
		Notes += FString::Printf(TEXT(" %.0f kg of air pumped back to the tanks."), AirBack);
	}
	if (bLag)
	{
		Notes += FString::Printf(TEXT(" Order lag back to %.0f s."), OrderLagSeconds);
	}
	if (FurnSold > 0)
	{
		Notes += FString::Printf(TEXT(" %d furnishing(s) sold back with it."), FurnSold);
	}
	if (WallsSold > 0)
	{
		Notes += FString::Printf(TEXT(" %d window(s) / door(s) sold back with it."), WallsSold);
	}
	const FString Name = Def && !Def->DisplayName.IsEmpty() ? Def->DisplayName : Gone.DefName.ToString();
	UE_LOG(LogRedHopeSim, Display, TEXT("Demolished %s #%d (%s); salvage: %s"), *Gone.DefName.ToString(), Gone.Id,
		Gone.bUnderConstruction ? TEXT("site cancelled") : TEXT("standing"), Line.IsEmpty() ? TEXT("nothing") : *Line);
	if (Gone.bUnderConstruction)
	{
		OnAlert.Broadcast(FString::Printf(TEXT("SITE CANCELLED — %s #%d. Returned to the Lander: %s.%s"), *Name, Gone.Id,
			Line.IsEmpty() ? TEXT("nothing") : *Line, *Notes));
	}
	else
	{
		OnAlert.Broadcast(FString::Printf(TEXT("DEMOLISHED — %s #%d. Salvage at the Lander: %s.%s"), *Name, Gone.Id,
			Line.IsEmpty() ? TEXT("nothing") : *Line, *Notes));
	}
	for (const FIntVector& K : FurnKeys)
	{
		OnFurnitureChanged.Broadcast(K);
	}
	for (const FIntVector& E : WallEdgesGone)
	{
		OnWallFeatureChanged.Broadcast(E);
	}
	OnBuildingRemoved.Broadcast(Gone);
}

bool URHSimWorldSubsystem::Debug_GetTask(int32 TaskId, FRHTask& Out) const
{
	for (const FRHTask& T : Tasks)
	{
		if (T.Id == TaskId)
		{
			Out = T;
			return true;
		}
	}
	return false;
}

void URHSimWorldSubsystem::Debug_AddSolidAt(int32 BuildingId, FName Resource, double Kg, bool bOutput)
{
	if (FRHBuildingInstance* B = FindBuilding(BuildingId))
	{
		(bOutput ? B->OutputKg : B->InputKg).FindOrAdd(Resource) += Kg;
		OnStockChanged.Broadcast(Resource, GetTotalSolid(Resource));
	}
}

// --- Wall fittings (BM-6, 2026-10-16) ---

FIntVector URHSimWorldSubsystem::WallEdgeKey(const FIntPoint& P, int32 Dir)
{
	switch (Dir & 3)
	{
	case 0: return FIntVector(P.X, P.Y, 0);
	case 1: return FIntVector(P.X - 1, P.Y, 0);
	case 2: return FIntVector(P.X, P.Y, 1);
	default: return FIntVector(P.X, P.Y - 1, 1);
	}
}

bool URHSimWorldSubsystem::ResolveWallEdge(const FVector& LocationCm, int32 Dir, FIntVector& OutEdge) const
{
	const FIntPoint P(FMath::FloorToInt32(LocationCm.X / 1000.0 + 0.5), FMath::FloorToInt32(LocationCm.Y / 1000.0 + 0.5));
	if (!HabBayOwner.Contains(P) || Dir < 0 || Dir > 3)
	{
		return false;
	}
	OutEdge = WallEdgeKey(P, Dir);
	return true;
}

int32 URHSimWorldSubsystem::WallBayIndex(int32 HabId, const FIntPoint& Point) const
{
	const FRHHabMemberCache* M = HabMembers.Find(HabId);
	if (!M)
	{
		return INDEX_NONE;
	}
	const FIntPoint L = Point - M->GridMin;
	return L.Y * M->NX + L.X;
}

URHSimWorldSubsystem::ERHWallEdge URHSimWorldSubsystem::ClassifyWallEdge(const FIntVector& Edge, int32* OutInsideHab, FIntPoint* OutInsidePoint, FIntPoint* OutOutward) const
{
	const FIntPoint A(Edge.X, Edge.Y);
	const FIntPoint B = A + (Edge.Z == 0 ? FIntPoint(1, 0) : FIntPoint(0, 1));
	const int32* HA = HabBayOwner.Find(A);
	const int32* HB = HabBayOwner.Find(B);
	if (!HA && !HB)
	{
		return ERHWallEdge::None;
	}
	const bool bAInside = HA != nullptr;
	if (OutInsideHab)
	{
		*OutInsideHab = bAInside ? *HA : *HB;
	}
	if (OutInsidePoint)
	{
		*OutInsidePoint = bAInside ? A : B;
	}
	if (OutOutward)
	{
		*OutOutward = bAInside ? (B - A) : (A - B);
	}
	if (!HA || !HB)
	{
		return ERHWallEdge::Exterior;
	}
	if (*HA != *HB)
	{
		return ERHWallEdge::Doorway; // a joint between two modules
	}
	return GetBayRoom(*HA, WallBayIndex(*HA, A)) == GetBayRoom(*HB, WallBayIndex(*HB, B)) ? ERHWallEdge::Open : ERHWallEdge::Doorway;
}

FVector URHSimWorldSubsystem::GetWallEdgeMidCm(const FIntVector& Edge) const
{
	return Edge.Z == 0 ? FVector(Edge.X * 1000.0 + 500.0, Edge.Y * 1000.0, 0.0) : FVector(Edge.X * 1000.0, Edge.Y * 1000.0 + 500.0, 0.0);
}

bool URHSimWorldSubsystem::GetAirlockChamberRect(const FIntVector& Edge, FVector& OutCentre, FVector2D& OutHalf) const
{
	FIntPoint Out;
	if (ClassifyWallEdge(Edge, nullptr, nullptr, &Out) != ERHWallEdge::Exterior)
	{
		return false;
	}
	// The presentation's chamber is 3 x 3 m outside the doorway (RHHabLayout).
	OutCentre = GetWallEdgeMidCm(Edge) + FVector(Out.X * 160.0, Out.Y * 160.0, 0.0);
	OutHalf = Out.X != 0 ? FVector2D(160.0, 150.0) : FVector2D(150.0, 160.0);
	return true;
}

const FRHWallFeature* URHSimWorldSubsystem::FindWallFeature(int32 Id) const
{
	return WallFeatures.FindByPredicate([Id](const FRHWallFeature& W) { return W.Id == Id; });
}

const FRHWallFeature* URHSimWorldSubsystem::FindWallFeatureOnEdge(const FIntVector& Edge) const
{
	const int32* Id = FeatureByEdge.Find(Edge);
	return Id ? FindWallFeature(*Id) : nullptr;
}

void URHSimWorldSubsystem::RebuildWallFeatureIndex()
{
	FeatureByEdge.Reset();
	for (const FRHWallFeature& W : WallFeatures)
	{
		FeatureByEdge.Add(W.Edge, W.Id);
	}
}

bool URHSimWorldSubsystem::CanPlaceWallFeature(FName RowName, const FIntVector& Edge, FString& OutReason, bool bCheckQueued) const
{
	if (!Defs || !Defs->HasWallCatalogue())
	{
		OutReason = TEXT("No windows or doors catalogue");
		return false;
	}
	const FRHDoorRow* Row = Defs->GetDoor(RowName);
	if (!Row)
	{
		OutReason = FString::Printf(TEXT("'%s' is not a window, door or airlock"), *RowName.ToString());
		return false;
	}
	if (!Row->SliceActive)
	{
		OutReason = FString::Printf(TEXT("%s is not available yet"), *Row->DisplayName);
		return false;
	}
	const ERHWallEdge Kind = ClassifyWallEdge(Edge);
	if (Kind == ERHWallEdge::None)
	{
		OutReason = TEXT("No habitat wall there");
		return false;
	}
	if (Row->Kind == NAME_WallDoor)
	{
		if (Kind == ERHWallEdge::Exterior)
		{
			OutReason = TEXT("Doors go in doorways between rooms - an outside wall takes a Window or an Airlock");
			return false;
		}
		if (Kind == ERHWallEdge::Open)
		{
			OutReason = TEXT("That opening joins one room - doors go in doorways between rooms or modules");
			return false;
		}
	}
	else if (Kind != ERHWallEdge::Exterior)
	{
		OutReason = FString::Printf(TEXT("A %s goes in an outside wall"), *Row->DisplayName);
		return false;
	}
	if (const FRHWallFeature* Have = FindWallFeatureOnEdge(Edge))
	{
		const FRHDoorRow* HaveRow = Defs->GetDoor(Have->Row);
		OutReason = FString::Printf(TEXT("The %s #%d is already there"), HaveRow ? *HaveRow->DisplayName : *Have->Row.ToString(), Have->Id);
		return false;
	}
	if (bCheckQueued)
	{
		for (const FRHCommand& Q : UplinkQueue)
		{
			FIntVector QEdge;
			if (Q.Verb == FName("Fit") && ResolveWallEdge(Q.Location, (int32)Q.Value, QEdge) && QEdge == Edge)
			{
				OutReason = TEXT("A fitting is already in transit for that wall");
				return false;
			}
		}
	}
	if (Row->Kind == NAME_WallAirlock)
	{
		FVector ChC;
		FVector2D ChH;
		GetAirlockChamberRect(Edge, ChC, ChH);
		for (const FRHBuildingInstance& B : Buildings)
		{
			const FRHBuildingRow* BDef = B.Level == 0 ? Defs->GetBuilding(B.DefName) : nullptr;
			const FVector2D BH = BDef ? GetFootprintHalfCm(BDef, B.SizeX, B.SizeY) : FVector2D::ZeroVector;
			if (BDef && FMath::Abs(B.LocationCm.X - ChC.X) < BH.X + ChH.X && FMath::Abs(B.LocationCm.Y - ChC.Y) < BH.Y + ChH.Y)
			{
				OutReason = FString::Printf(TEXT("The %s #%d stands where the airlock's chamber goes"), *B.DefName.ToString(), B.Id);
				return false;
			}
		}
	}
	for (const auto& Line : URHDefinitionsSubsystem::GetDoorCost(*Row))
	{
		const double Have = Defs->IsSolidResource(Line.Key) ? GetTotalSolid(Line.Key) + GetStock(Line.Key) : GetStock(Line.Key);
		if (Have + KINDA_SMALL_NUMBER < Line.Value)
		{
			OutReason = FString::Printf(TEXT("Insufficient %s (%.0f needed, %.0f on hand)"), *Line.Key.ToString(), Line.Value, Have);
			return false;
		}
	}
	return true;
}

TMap<FName, double> URHSimWorldSubsystem::WallFeatureRefundFor(const TSet<int32>& Ids) const
{
	TMap<FName, double> Refund;
	for (const FRHWallFeature& W : WallFeatures)
	{
		const FRHDoorRow* Row = (Ids.Contains(W.Id) && Defs) ? Defs->GetDoor(W.Row) : nullptr;
		if (!Row)
		{
			continue;
		}
		for (const auto& L : URHDefinitionsSubsystem::GetDoorCost(*Row))
		{
			Refund.FindOrAdd(L.Key) += L.Value * FurnitureRefundFraction;
		}
	}
	return Refund;
}

bool URHSimWorldSubsystem::CanSellWallFeature(int32 Id, FString& OutReason, TMap<FName, double>* OutRefund) const
{
	if (!FindWallFeature(Id))
	{
		OutReason = FString::Printf(TEXT("No fitting #%d - it is already gone"), Id);
		return false;
	}
	if (OutRefund)
	{
		TSet<int32> Ids;
		Ids.Add(Id);
		*OutRefund = WallFeatureRefundFor(Ids);
	}
	return true;
}

void URHSimWorldSubsystem::AddWallFeature(FName RowName, const FIntVector& Edge)
{
	FRHWallFeature W;
	W.Id = NextWallFeatureId++;
	W.Row = RowName;
	W.Edge = Edge;
	WallFeatures.Add(W);
	RebuildWallFeatureIndex();
	UE_LOG(LogRedHopeSim, Display, TEXT("Fitted %s #%d in the wall (%d, %d, %d)"), *RowName.ToString(), W.Id, Edge.X, Edge.Y, Edge.Z);
	OnWallFeatureChanged.Broadcast(Edge);
}

void URHSimWorldSubsystem::RemoveWallFeatures(const TSet<int32>& Ids, TMap<FName, double>* OutRefund)
{
	if (Ids.Num() == 0)
	{
		return;
	}
	const TMap<FName, double> Refund = WallFeatureRefundFor(Ids);
	TArray<FIntVector> Edges;
	for (const FRHWallFeature& W : WallFeatures)
	{
		if (Ids.Contains(W.Id))
		{
			Edges.Add(W.Edge);
		}
	}
	const int32 Sold = WallFeatures.RemoveAll([&Ids](const FRHWallFeature& W) { return Ids.Contains(W.Id); });
	RebuildWallFeatureIndex();
	CreditLander(Refund, /*bOutputShelf*/ true);
	if (OutRefund)
	{
		*OutRefund = Refund;
	}
	UE_LOG(LogRedHopeSim, Display, TEXT("Sold %d wall fitting(s)"), Sold);
	for (const FIntVector& E : Edges)
	{
		OnWallFeatureChanged.Broadcast(E);
	}
}

void URHSimWorldSubsystem::GetFeaturesTouchingHab(int32 HabId, TSet<int32>& OutIds) const
{
	OutIds.Reset();
	for (const FRHWallFeature& W : WallFeatures)
	{
		const FIntPoint A(W.Edge.X, W.Edge.Y);
		const FIntPoint B = A + (W.Edge.Z == 0 ? FIntPoint(1, 0) : FIntPoint(0, 1));
		const int32* HA = HabBayOwner.Find(A);
		const int32* HB = HabBayOwner.Find(B);
		if ((HA && *HA == HabId) || (HB && *HB == HabId))
		{
			OutIds.Add(W.Id);
		}
	}
}

bool URHSimWorldSubsystem::IsNetSealed(const FRHHabNetCache& N) const
{
	if (FeatureByEdge.Num() == 0)
	{
		return false;
	}
	int32 Doorways = 0;
	for (const int32 Id : N.Members)
	{
		const FRHHabMemberCache& M = HabMembers.FindChecked(Id);
		for (int32 BY = 0; BY < M.NY; ++BY)
		{
			for (int32 BX = 0; BX < M.NX; ++BX)
			{
				const FIntPoint P = M.GridMin + FIntPoint(BX, BY);
				for (int32 Axis = 0; Axis < 2; ++Axis)
				{
					const FIntVector Edge(P.X, P.Y, Axis);
					if (ClassifyWallEdge(Edge) != ERHWallEdge::Doorway)
					{
						continue;
					}
					++Doorways;
					const FRHWallFeature* W = FindWallFeatureOnEdge(Edge);
					const FRHDoorRow* Row = (W && Defs) ? Defs->GetDoor(W->Row) : nullptr;
					if (!Row || Row->Kind != NAME_WallDoor)
					{
						return false;
					}
				}
			}
		}
	}
	return Doorways > 0;
}

void URHSimWorldSubsystem::GetHabNetworkDoorways(int32 HabId, int32& OutDoorways, int32& OutDoored) const
{
	OutDoorways = 0;
	OutDoored = 0;
	const FRHHabNetCache* N = FindHabNet(HabId);
	if (!N)
	{
		return;
	}
	for (const int32 Id : N->Members)
	{
		const FRHHabMemberCache& M = HabMembers.FindChecked(Id);
		for (int32 BY = 0; BY < M.NY; ++BY)
		{
			for (int32 BX = 0; BX < M.NX; ++BX)
			{
				const FIntPoint P = M.GridMin + FIntPoint(BX, BY);
				for (int32 Axis = 0; Axis < 2; ++Axis)
				{
					const FIntVector Edge(P.X, P.Y, Axis);
					if (ClassifyWallEdge(Edge) == ERHWallEdge::Doorway)
					{
						++OutDoorways;
						const FRHWallFeature* W = FindWallFeatureOnEdge(Edge);
						const FRHDoorRow* Row = (W && Defs) ? Defs->GetDoor(W->Row) : nullptr;
						OutDoored += (Row && Row->Kind == NAME_WallDoor) ? 1 : 0;
					}
				}
			}
		}
	}
}

bool URHSimWorldSubsystem::IsHabNetworkSealed(int32 HabId) const
{
	const FRHHabNetCache* N = FindHabNet(HabId);
	return N && IsNetSealed(*N);
}

double URHSimWorldSubsystem::GetMemberWindowLeakPerSol(int32 HabId) const
{
	double Sum = 0.0;
	for (const FRHWallFeature& W : WallFeatures)
	{
		const FRHDoorRow* Row = Defs ? Defs->GetDoor(W.Row) : nullptr;
		int32 Inside = 0;
		if (Row && Row->LeakKgPerSol > 0.f && ClassifyWallEdge(W.Edge, &Inside) == ERHWallEdge::Exterior && Inside == HabId)
		{
			Sum += Row->LeakKgPerSol;
		}
	}
	return Sum;
}

double URHSimWorldSubsystem::GetHabNetworkLeakKgPerSol(int32 HabId) const
{
	const FRHHabNetCache* N = FindHabNet(HabId);
	if (!N)
	{
		return 0.0;
	}
	const double Mul = IsNetSealed(*N) ? SealedLeakMul : 1.0;
	double Sum = 0.0;
	for (const int32 Id : N->Members)
	{
		const FRHHabMemberCache& M = HabMembers.FindChecked(Id);
		Sum += M.NX * M.NY * O2LeakKgPerCellPerSol * Mul + GetMemberWindowLeakPerSol(Id);
	}
	return Sum;
}

double URHSimWorldSubsystem::GetViewsHope(double* OutRaw) const
{
	double Sum = 0.0;
	for (const FRHWallFeature& W : WallFeatures)
	{
		const FRHDoorRow* Row = Defs ? Defs->GetDoor(W.Row) : nullptr;
		int32 Inside = 0;
		if (Row && Row->HopeViews > 0.f && ClassifyWallEdge(W.Edge, &Inside) == ERHWallEdge::Exterior && IsHabRated(Inside))
		{
			Sum += Row->HopeViews;
		}
	}
	if (OutRaw)
	{
		*OutRaw = Sum;
	}
	return FMath::Min(Sum, HopeViewsMax);
}

// --- Furniture (BM-4, 2026-10-16) ---

void URHSimWorldSubsystem::CreditLander(const TMap<FName, double>& Lines, bool bOutputShelf)
{
	FRHBuildingInstance* Lander = Buildings.FindByPredicate([](const FRHBuildingInstance& B) { return B.DefName == NAME_Lander; });
	for (const auto& L : Lines)
	{
		if (L.Value <= 0.0)
		{
			continue;
		}
		if (Lander && Defs && Defs->IsSolidResource(L.Key) && L.Key != NAME_Glass)
		{
			(bOutputShelf ? Lander->OutputKg : Lander->InputKg).FindOrAdd(L.Key) += L.Value;
			OnStockChanged.Broadcast(L.Key, GetTotalSolid(L.Key));
		}
		else
		{
			AddStock(L.Key, L.Value);
		}
	}
}

FIntPoint URHSimWorldSubsystem::GetFurnitureSize(const FRHFurnitureRow& Row, int32 Rot)
{
	const int32 X = FMath::Clamp(Row.SlotsX, 1, FurnishGrid);
	const int32 Y = FMath::Clamp(Row.SlotsY, 1, FurnishGrid);
	return (Rot & 1) ? FIntPoint(Y, X) : FIntPoint(X, Y);
}

uint32 URHSimWorldSubsystem::FurnitureMask(int32 Slot, const FIntPoint& Size)
{
	const int32 SX = Slot % FurnishGrid;
	const int32 SY = Slot / FurnishGrid;
	if (Slot < 0 || Slot >= FurnishGrid * FurnishGrid || SX + Size.X > FurnishGrid || SY + Size.Y > FurnishGrid)
	{
		return 0u; // doesn't fit
	}
	uint32 M = 0u;
	for (int32 y = SY; y < SY + Size.Y; ++y)
	{
		for (int32 x = SX; x < SX + Size.X; ++x)
		{
			M |= 1u << (y * FurnishGrid + x);
		}
	}
	return M;
}

const FRHFurnitureInstance* URHSimWorldSubsystem::FindFurniture(int32 Id) const
{
	// C1: O(1) through the index RebuildFurnitureCache keeps. The size check and
	// the Id check make a stale index fall back to the scan, never a wrong piece.
	if (FurnitureIndexById.Num() == Furniture.Num())
	{
		const int32* Index = FurnitureIndexById.Find(Id);
		return (Index && Furniture.IsValidIndex(*Index) && Furniture[*Index].Id == Id) ? &Furniture[*Index] : nullptr;
	}
	return Furniture.FindByPredicate([Id](const FRHFurnitureInstance& F) { return F.Id == Id; });
}

int32 URHSimWorldSubsystem::GetCellFurniture(const FIntVector& Key, TArray<int32>* OutIds) const
{
	const FRHCellFurnish* CF = FurnishedCells.Find(Key);
	if (OutIds)
	{
		OutIds->Reset();
		if (CF)
		{
			*OutIds = CF->Ids;
		}
	}
	return CF ? CF->Ids.Num() : 0;
}

FName URHSimWorldSubsystem::GetCellRoom(const FIntVector& Key) const
{
	return Key.Z == 0 ? GetRoomAt(Key.X, Key.Y) : GetBayRoom(Key.Z, Key.Y);
}

bool URHSimWorldSubsystem::ResolveFurnitureKey(int32 Level, const FVector& LocationCm, FIntVector& OutKey) const
{
	if (Level == 0)
	{
		int32 H = 0, Bay = INDEX_NONE;
		if (!FindBayAt(LocationCm, H, Bay))
		{
			return false;
		}
		OutKey = FIntVector(0, Bay, H);
		return true;
	}
	const int32 Cell = FindCarvedCellAt(Level, LocationCm);
	if (Cell == INDEX_NONE)
	{
		return false;
	}
	OutKey = FIntVector(Level, Cell, 0);
	return true;
}

bool URHSimWorldSubsystem::ResolveFurnishOrderKey(const FRHCommand& Cmd, FIntVector& OutKey) const
{
	if (Cmd.Level == 0)
	{
		int32 H = 0, Bay = INDEX_NONE;
		if (!FindBayAt(Cmd.Location, H, Bay))
		{
			return false;
		}
		OutKey = FIntVector(0, Bay, H);
		return true;
	}
	int32 Cell = 0, Slot = 0, Rot = 0;
	DecodeFurnish(Cmd.Value, Cell, Slot, Rot);
	OutKey = FIntVector(Cmd.Level, Cell, 0);
	return true;
}

FVector URHSimWorldSubsystem::GetCellCentreCm(const FIntVector& Key) const
{
	if (Key.Z != 0)
	{
		const FRHBuildingInstance* B = FindBuildingById(Key.Z);
		return B ? GetBayCentreCm(*B, Key.Y) : FVector::ZeroVector;
	}
	const FIntPoint P = GetFloorCell(Key.X, Key.Y);
	return FVector(ShaftHeadCm.X + P.X * 1000.0, ShaftHeadCm.Y + P.Y * 1000.0, Key.X * FloorHeightCm);
}

bool URHSimWorldSubsystem::GetFurnitureFootprintCm(FName RowName, const FIntVector& Key, int32 Slot, int32 Rot, FVector& OutCentre, FVector2D& OutHalf) const
{
	const FRHFurnitureRow* Row = Defs ? Defs->GetFurniture(RowName) : nullptr;
	if (!Row)
	{
		return false;
	}
	const FIntPoint S = GetFurnitureSize(*Row, Rot);
	const int32 SX = Slot % FurnishGrid;
	const int32 SY = Slot / FurnishGrid;
	OutHalf = FVector2D(125.0 * S.X, 125.0 * S.Y);
	OutCentre = GetCellCentreCm(Key) + FVector(-500.0 + 250.0 * SX + OutHalf.X, -500.0 + 250.0 * SY + OutHalf.Y, 0.0);
	return true;
}

int32 URHSimWorldSubsystem::AnchorSlotFor(const FVector2D& LocalFromCellMinCm, int32 W, int32 H)
{
	const int32 CW = FMath::Clamp(W, 1, FurnishGrid);
	const int32 CH = FMath::Clamp(H, 1, FurnishGrid);
	const int32 SX = FMath::Clamp(FMath::RoundToInt32(LocalFromCellMinCm.X / 250.0 - CW * 0.5), 0, FurnishGrid - CW);
	const int32 SY = FMath::Clamp(FMath::RoundToInt32(LocalFromCellMinCm.Y / 250.0 - CH * 0.5), 0, FurnishGrid - CH);
	return SY * FurnishGrid + SX;
}

void URHSimWorldSubsystem::RebuildFurnitureCache()
{
	++FurnitureVersion; // C1: the keyed caches (bed assignment) re-derive on their next read
	FurnishedCells.Reset();
	FurnitureIndexById.Reset();
	FurnitureIndexById.Reserve(Furniture.Num());
	ComfortPieces.Reset();
	for (int32 FurnIndex = 0; FurnIndex < Furniture.Num(); ++FurnIndex)
	{
		const FRHFurnitureInstance& F = Furniture[FurnIndex];
		FurnitureIndexById.Add(F.Id, FurnIndex);
		FRHCellFurnish& CF = FurnishedCells.FindOrAdd(F.Key);
		CF.Ids.Add(F.Id);
		const FRHFurnitureRow* Row = Defs ? Defs->GetFurniture(F.Row) : nullptr;
		if (!Row)
		{
			continue;
		}
		CF.Occupied |= FurnitureMask(F.Slot, GetFurnitureSize(*Row, F.Rot));
		if (Row->HopeComfort > 0.f)
		{
			ComfortPieces.Add(FurnIndex);
		}
		if (Row->Provides == NAME_FurnBed)
		{
			CF.Beds += Row->Capacity;
			++CF.BedPieces;
		}
		else if (Row->Provides == NAME_FurnJobSeat)
		{
			CF.JobSeats += Row->Capacity;
			++CF.SeatPieces;
		}
		else if (Row->Provides == NAME_FurnDiningSeat)
		{
			CF.DiningSeats += Row->Capacity;
		}
	}
}

bool URHSimWorldSubsystem::IsFurnishKeyValid(const FIntVector& Key, FString* OutReason) const
{
	if (Key.Z == 0)
	{
		if (Key.X >= 0 || Key.Y < 0 || Key.Y >= GetFloorCarvedCells(Key.X))
		{
			if (OutReason)
			{
				*OutReason = FString::Printf(TEXT("Floor %d cell %d is not carved yet"), Key.X, Key.Y);
			}
			return false;
		}
		return true;
	}
	const FRHBuildingInstance* B = FindBuildingById(Key.Z);
	const FRHBuildingRow* Def = (B && Defs) ? Defs->GetBuilding(B->DefName) : nullptr;
	const FIntPoint G = B ? GetBayGrid(*B) : FIntPoint(0, 0);
	if (Key.X != 0 || !B || B->Level != 0 || !Def || !Def->Pressurized || Key.Y < 0 || Key.Y >= G.X * G.Y)
	{
		if (OutReason)
		{
			*OutReason = TEXT("No habitat bay there");
		}
		return false;
	}
	if (B->bUnderConstruction)
	{
		if (OutReason)
		{
			*OutReason = TEXT("The habitat is still being built");
		}
		return false;
	}
	return true;
}

bool URHSimWorldSubsystem::IsLivingKey(const FIntVector& Key) const
{
	static const FName NLiving(TEXT("Living"));
	const FRHRoomRow* Room = Defs ? Defs->GetRoom(GetCellRoom(Key)) : nullptr;
	return Room && Room->SliceActive && Room->Function == NLiving;
}

bool URHSimWorldSubsystem::IsFurnitureCellWorking(const FIntVector& Key) const
{
	const FRHRoomRow* Room = Defs ? Defs->GetRoom(GetCellRoom(Key)) : nullptr;
	if (!Room || !Room->SliceActive)
	{
		return false;
	}
	return Key.Z == 0 ? RatedFloors.Contains(Key.X) : IsHabRated(Key.Z);
}

uint32 URHSimWorldSubsystem::BuildingMaskInCell(const FIntVector& Key, uint32 Probe, const FRHBuildingInstance** OutFirst) const
{
	if (OutFirst)
	{
		*OutFirst = nullptr;
	}
	if (!Defs)
	{
		return 0u;
	}
	const int32 Level = Key.Z == 0 ? Key.X : 0;
	const FVector C = GetCellCentreCm(Key);
	uint32 M = 0u;
	for (const FRHBuildingInstance& B : Buildings)
	{
		const FRHBuildingRow* BDef = B.Level == Level ? Defs->GetBuilding(B.DefName) : nullptr;
		if (!BDef || BDef->Pressurized)
		{
			continue; // shells are the rooms themselves, not obstacles in them
		}
		const FVector2D Half = GetFootprintHalfCm(BDef, B.SizeX, B.SizeY);
		if (Half.X <= 0.0 || Half.Y <= 0.0)
		{
			continue;
		}
		uint32 BM = 0u;
		for (int32 i = 0; i < FurnishGrid * FurnishGrid; ++i)
		{
			const double X0 = C.X - 500.0 + 250.0 * (i % FurnishGrid);
			const double Y0 = C.Y - 500.0 + 250.0 * (i / FurnishGrid);
			if (B.LocationCm.X - Half.X < X0 + 249.0 && B.LocationCm.X + Half.X > X0 + 1.0
				&& B.LocationCm.Y - Half.Y < Y0 + 249.0 && B.LocationCm.Y + Half.Y > Y0 + 1.0)
			{
				BM |= 1u << i;
			}
		}
		if (OutFirst && !*OutFirst && (BM & Probe))
		{
			*OutFirst = &B;
		}
		M |= BM;
	}
	return M;
}

bool URHSimWorldSubsystem::CanPlaceFurniture(FName RowName, const FIntVector& Key, int32 Slot, int32 Rot, FString& OutReason, bool bCheckQueued, bool bCheckCost) const
{
	if (!Defs || !Defs->HasFurnitureCatalogue())
	{
		OutReason = TEXT("No furniture catalogue");
		return false;
	}
	const FRHFurnitureRow* Row = Defs->GetFurniture(RowName);
	if (!Row)
	{
		OutReason = FString::Printf(TEXT("'%s' is not in the furniture catalogue"), *RowName.ToString());
		return false;
	}
	if (!Row->SliceActive)
	{
		OutReason = FString::Printf(TEXT("%s is not available yet"), *Row->DisplayName);
		return false;
	}
	if (!IsFurnishKeyValid(Key, &OutReason))
	{
		return false;
	}
	const FRHRoomRow* Room = Defs->GetRoom(GetCellRoom(Key));
	if (!Room || !Room->SliceActive)
	{
		OutReason = TEXT("Zone this space first - furniture goes in rooms");
		return false;
	}
	if (!URHDefinitionsSubsystem::FurnitureAllowsFunction(*Row, Room->Function))
	{
		OutReason = FString::Printf(TEXT("A %s goes in a %s room, not %s"), *Row->DisplayName,
			*Row->RoomFunctions.Replace(TEXT(";"), TEXT(" or ")), *Room->DisplayName);
		return false;
	}
	const uint32 Mask = (Rot >= 0 && Rot <= 3) ? FurnitureMask(Slot, GetFurnitureSize(*Row, Rot)) : 0u;
	if (Mask == 0u)
	{
		OutReason = TEXT("It doesn't fit there - keep it inside the room");
		return false;
	}
	if (const FRHCellFurnish* CF = FurnishedCells.Find(Key))
	{
		for (const int32 Id : CF->Ids)
		{
			const FRHFurnitureInstance* F = FindFurniture(Id);
			const FRHFurnitureRow* FR = F ? Defs->GetFurniture(F->Row) : nullptr;
			if (FR && (FurnitureMask(F->Slot, GetFurnitureSize(*FR, F->Rot)) & Mask))
			{
				OutReason = FString::Printf(TEXT("Overlaps the %s #%d"), *FR->DisplayName, F->Id);
				return false;
			}
		}
	}
	if (bCheckQueued)
	{
		for (const FRHCommand& Q : UplinkQueue)
		{
			const FRHFurnitureRow* QRow = Q.Verb == FName("Furnish") ? Defs->GetFurniture(Q.Target) : nullptr;
			FIntVector QKey;
			if (!QRow || !ResolveFurnishOrderKey(Q, QKey) || QKey != Key)
			{
				continue;
			}
			int32 QCell = 0, QSlot = 0, QRot = 0;
			DecodeFurnish(Q.Value, QCell, QSlot, QRot);
			if (FurnitureMask(QSlot, GetFurnitureSize(*QRow, QRot)) & Mask)
			{
				OutReason = FString::Printf(TEXT("A %s is already in transit for that spot"), *QRow->DisplayName);
				return false;
			}
		}
	}
	const FRHBuildingInstance* Blocker = nullptr;
	if (BuildingMaskInCell(Key, Mask, &Blocker) & Mask)
	{
		OutReason = Blocker ? FString::Printf(TEXT("The %s #%d stands there"), *Blocker->DefName.ToString(), Blocker->Id)
			: FString(TEXT("A machine stands there"));
		return false;
	}
	// Bed guard (ruling 2026-10-16): furnishing a surface Living bay may not
	// take beds from the habitat's sleepers - its first non-bed piece ends the
	// legacy beds, and a smaller bed replaces them.
	if (Key.Z != 0 && IsLivingKey(Key))
	{
		FRHFurnishPreview P;
		if (PreviewFurnishEffect(RowName, Key, P) && P.BedsAfter < P.BedsBefore)
		{
			const int32 Residents = GetHabResidents(Key.Z);
			if (CountHabBeds(Key.Z) - P.BedsBefore + P.BedsAfter < Residents)
			{
				OutReason = FString::Printf(TEXT("%d resident(s) sleep in this habitat - put a bed in this bay first"), Residents);
				return false;
			}
		}
	}
	if (!bCheckCost)
	{
		return true; // C5b: the city's growth has paid its bill whole at the start
	}
	for (const auto& Line : URHDefinitionsSubsystem::GetFurnitureCost(*Row))
	{
		const double Have = Defs->IsSolidResource(Line.Key) ? GetTotalSolid(Line.Key) + GetStock(Line.Key) : GetStock(Line.Key);
		if (Have + KINDA_SMALL_NUMBER < Line.Value)
		{
			OutReason = FString::Printf(TEXT("Insufficient %s (%.0f needed, %.0f on hand)"), *Line.Key.ToString(), Line.Value, Have);
			return false;
		}
	}
	return true;
}

int32 URHSimWorldSubsystem::CountFloorBeds(int32 Level, const TSet<int32>* Exclude) const
{
	const int32 Carved = GetFloorCarvedCells(Level);
	if (Furniture.Num() == 0)
	{
		return (int32)(Carved * ColonistsPerCell); // the legacy rule, exactly
	}
	// Every carved cell holds the legacy beds, except a Living cell with bed
	// pieces, which holds exactly its beds.
	int32 BedCells = 0, Sum = 0;
	if (!Exclude)
	{
		// C1: read off the per-cell tallies RebuildFurnitureCache keeps (a cell's
		// Beds is the capacity of its bed pieces) - one entry per furnished cell,
		// not one pass over every piece with a row lookup each. Integer sums:
		// the map's visiting order cannot matter.
		for (const TPair<FIntVector, FRHCellFurnish>& Cell : FurnishedCells)
		{
			if (Cell.Key.Z != 0 || Cell.Key.X != Level || Cell.Value.BedPieces == 0 || !IsLivingKey(Cell.Key))
			{
				continue;
			}
			++BedCells;
			Sum += Cell.Value.Beds;
		}
	}
	else
	{
		// What-if (a sale preview): the same count with some pieces taken away.
		TMap<int32, int32> CellBeds;
		for (const FRHFurnitureInstance& F : Furniture)
		{
			if (F.Key.Z != 0 || F.Key.X != Level || Exclude->Contains(F.Id))
			{
				continue;
			}
			const FRHFurnitureRow* Row = Defs ? Defs->GetFurniture(F.Row) : nullptr;
			if (Row && Row->Provides == NAME_FurnBed && IsLivingKey(F.Key))
			{
				CellBeds.FindOrAdd(F.Key.Y) += Row->Capacity;
			}
		}
		BedCells = CellBeds.Num();
		for (const auto& P : CellBeds)
		{
			Sum += P.Value; // an integer sum: order-free
		}
	}
	const int32 Beds = BedCells == 0 ? (int32)(Carved * ColonistsPerCell) : (int32)((Carved - BedCells) * ColonistsPerCell) + Sum;
	if (bVerifyCaches && !Exclude)
	{
		const TSet<int32> Nothing;
		const int32 PerPiece = CountFloorBeds(Level, &Nothing); // the original per-piece count
		if (PerPiece != Beds)
		{
			UE_LOG(LogRedHopeSim, Error, TEXT("RHVERIFY: floor %d bed count %d from the cell tallies, %d counting piece by piece"), Level, Beds, PerPiece);
		}
	}
	return Beds;
}

int32 URHSimWorldSubsystem::CountHabBeds(int32 HabId, int32 ExcludeBay, const TSet<int32>* Exclude) const
{
	const FRHHabMemberCache* M = HabMembers.Find(HabId);
	const FRHBuildingInstance* B = M ? nullptr : FindBuildingById(HabId);
	const FIntPoint G = M ? FIntPoint(M->NX, M->NY) : (B ? GetBayGrid(*B) : FIntPoint(0, 0));
	int32 Plain = 0, Sum = 0;
	for (int32 Bay = 0; Bay < G.X * G.Y; ++Bay)
	{
		const FIntVector Key(0, Bay, HabId);
		if (Bay == ExcludeBay || !IsLivingKey(Key))
		{
			continue;
		}
		// Director ruling 2026-10-16 (option 2): a Living bay with ANY
		// furnishing houses exactly its bed pieces - a lounge furnished
		// without beds sleeps nobody. Only a bare bay keeps the legacy beds.
		int32 Beds = 0;
		bool bAny = false;
		if (const FRHCellFurnish* CF = FurnishedCells.Find(Key))
		{
			for (const int32 Id : CF->Ids)
			{
				const FRHFurnitureInstance* F = (Exclude && Exclude->Contains(Id)) ? nullptr : FindFurniture(Id);
				const FRHFurnitureRow* Row = (F && Defs) ? Defs->GetFurniture(F->Row) : nullptr;
				bAny |= F != nullptr;
				if (Row && Row->Provides == NAME_FurnBed)
				{
					Beds += Row->Capacity;
				}
			}
		}
		if (bAny)
		{
			Sum += Beds;
		}
		else
		{
			++Plain;
		}
	}
	return (int32)(Plain * ColonistsPerCell) + Sum;
}

int32 URHSimWorldSubsystem::GetCellBeds(const FIntVector& Key) const
{
	if (Key.Z != 0 && !IsLivingKey(Key))
	{
		return 0; // a bay houses only as Living Quarters
	}
	const FRHCellFurnish* CF = FurnishedCells.Find(Key);
	if (CF && CF->BedPieces > 0 && IsLivingKey(Key))
	{
		return CF->Beds;
	}
	if (Key.Z != 0 && CF && CF->Ids.Num() > 0)
	{
		return 0; // a surface Living bay furnished without beds (ruling 2026-10-16)
	}
	return (int32)ColonistsPerCell; // every carved cell holds the legacy beds
}

int32 URHSimWorldSubsystem::GetCellSeats(const FIntVector& Key) const
{
	const FRHRoomRow* Room = Defs ? Defs->GetRoom(GetCellRoom(Key)) : nullptr;
	if (!Room || !Room->SliceActive || !IsSeatFunction(Room->Function))
	{
		return 0;
	}
	const FRHCellFurnish* CF = FurnishedCells.Find(Key);
	return (CF && CF->SeatPieces > 0) ? CF->JobSeats : FMath::Max(1, Room->SeatCount);
}

double URHSimWorldSubsystem::GetFurnishingsHope(double* OutRaw) const
{
	double Sum = 0.0;
	// C1: only the pieces that lift Hope, in the piece order they were always
	// summed in (a Hope-less bunk bed added nothing but a row lookup per call).
	// Whether a row may stand in a room is a string parse, so each (row, room
	// function) answer is kept for the rest of the call.
	struct FStandsIn { const FRHFurnitureRow* Row; FName Function; bool bAllowed; };
	TArray<FStandsIn, TInlineAllocator<16>> StandsIn;
	const auto Allowed = [&StandsIn](const FRHFurnitureRow& Piece, FName Function)
	{
		for (const FStandsIn& Known : StandsIn)
		{
			if (Known.Row == &Piece && Known.Function == Function)
			{
				return Known.bAllowed;
			}
		}
		const bool bAllowed = URHDefinitionsSubsystem::FurnitureAllowsFunction(Piece, Function);
		StandsIn.Add({ &Piece, Function, bAllowed });
		return bAllowed;
	};
	for (const int32 PieceIndex : ComfortPieces)
	{
		const FRHFurnitureInstance& F = Furniture[PieceIndex];
		const FRHFurnitureRow* Row = Defs ? Defs->GetFurniture(F.Row) : nullptr;
		if (!Row || Row->HopeComfort <= 0.f || !IsFurnitureCellWorking(F.Key))
		{
			continue;
		}
		const FRHRoomRow* Room = Defs->GetRoom(GetCellRoom(F.Key));
		if (Room && Allowed(*Row, Room->Function))
		{
			Sum += Row->HopeComfort;
		}
	}
	if (OutRaw)
	{
		*OutRaw = Sum;
	}
	return FMath::Min(Sum, HopeFurnishingsMax);
}

bool URHSimWorldSubsystem::PreviewFurnishEffect(FName RowName, const FIntVector& Key, FRHFurnishPreview& Out) const
{
	const FRHFurnitureRow* Row = Defs ? Defs->GetFurniture(RowName) : nullptr;
	if (!Row)
	{
		return false;
	}
	const FRHCellFurnish* CF = FurnishedCells.Find(Key);
	Out.BedsBefore = GetCellBeds(Key);
	Out.SeatsBefore = GetCellSeats(Key);
	Out.BedsAfter = Out.BedsBefore;
	Out.SeatsAfter = Out.SeatsBefore;
	if (Row->Provides == NAME_FurnBed && IsLivingKey(Key))
	{
		Out.BedsAfter = ((CF && CF->BedPieces > 0) ? CF->Beds : 0) + Row->Capacity;
	}
	else if (Key.Z != 0 && IsLivingKey(Key) && (!CF || CF->Ids.Num() == 0))
	{
		Out.BedsAfter = 0; // the first furnishing of a bare Living bay, not a bed: it stops sleeping anyone
	}
	else if (Row->Provides == NAME_FurnJobSeat && Out.SeatsBefore > 0)
	{
		Out.SeatsAfter = ((CF && CF->SeatPieces > 0) ? CF->JobSeats : 0) + Row->Capacity;
	}
	// C2a: the Furnishings line is per resident now (see GetColonyHope), so the hint
	// shows the same scaled figure the Hope breakdown will.
	double Raw = 0.0;
	GetFurnishingsHope(&Raw);
	const double PerHead = ComfortRefPop / (double)FMath::Max(Colonists.Num(), 1);
	Out.ComfortBefore = FMath::Min(HopeFurnishingsMax, Raw * PerHead);
	Out.ComfortAfter = FMath::Min(HopeFurnishingsMax, (Raw + (IsFurnitureCellWorking(Key) ? (double)Row->HopeComfort : 0.0)) * PerHead);
	return true;
}

TMap<FName, double> URHSimWorldSubsystem::FurnitureRefundFor(const TSet<int32>& Ids) const
{
	TMap<FName, double> Refund;
	for (const FRHFurnitureInstance& F : Furniture)
	{
		const FRHFurnitureRow* Row = Ids.Contains(F.Id) && Defs ? Defs->GetFurniture(F.Row) : nullptr;
		if (!Row)
		{
			continue;
		}
		for (const auto& L : URHDefinitionsSubsystem::GetFurnitureCost(*Row))
		{
			Refund.FindOrAdd(L.Key) += L.Value * FurnitureRefundFraction;
		}
	}
	return Refund;
}

bool URHSimWorldSubsystem::BedGuardAllowsRemoval(const FIntVector& Key, const TSet<int32>& Ids, FString& OutReason) const
{
	int32 Residents = 0, Before = 0, After = 0;
	if (Key.Z == 0)
	{
		for (const FRHColonist& Col : Colonists)
		{
			Residents += (Col.HomeHab == 0 && Col.HomeLevel == Key.X) ? 1 : 0;
		}
		Before = CountFloorBeds(Key.X, nullptr);
		After = CountFloorBeds(Key.X, &Ids);
	}
	else
	{
		Residents = GetHabResidents(Key.Z);
		Before = CountHabBeds(Key.Z);
		After = CountHabBeds(Key.Z, INDEX_NONE, &Ids);
	}
	if (After < Before && After < Residents)
	{
		OutReason = FString::Printf(TEXT("%d resident(s) sleep here - selling this would leave someone without a bed"), Residents);
		return false;
	}
	return true;
}

bool URHSimWorldSubsystem::CanSellFurniture(int32 Id, FString& OutReason, TMap<FName, double>* OutRefund) const
{
	const FRHFurnitureInstance* F = FindFurniture(Id);
	if (!F)
	{
		OutReason = FString::Printf(TEXT("No furnishing #%d - it is already gone"), Id);
		return false;
	}
	TSet<int32> Ids;
	Ids.Add(Id);
	if (!BedGuardAllowsRemoval(F->Key, Ids, OutReason))
	{
		return false;
	}
	if (OutRefund)
	{
		*OutRefund = FurnitureRefundFor(Ids);
	}
	return true;
}

bool URHSimWorldSubsystem::CanSellCell(const FIntVector& Key, FString& OutReason, TMap<FName, double>* OutRefund) const
{
	TArray<int32> CellIds;
	if (GetCellFurniture(Key, &CellIds) == 0)
	{
		OutReason = TEXT("Nothing to sell here");
		return false;
	}
	TSet<int32> Ids;
	Ids.Append(CellIds);
	if (!BedGuardAllowsRemoval(Key, Ids, OutReason))
	{
		return false;
	}
	if (OutRefund)
	{
		*OutRefund = FurnitureRefundFor(Ids);
	}
	return true;
}

void URHSimWorldSubsystem::AddFurniture(FName RowName, const FIntVector& Key, int32 Slot, int32 Rot)
{
	FRHFurnitureInstance F;
	F.Id = NextFurnitureId++;
	F.Row = RowName;
	F.Key = Key;
	F.Slot = Slot;
	F.Rot = Rot & 3;
	Furniture.Add(F);
	RebuildFurnitureCache();
	UE_LOG(LogRedHopeSim, Display, TEXT("Furnished %s #%d in (%d, %d, %d) at slot %d, rot %d"),
		*RowName.ToString(), F.Id, Key.X, Key.Y, Key.Z, Slot, F.Rot);
	OnFurnitureChanged.Broadcast(Key);
}

void URHSimWorldSubsystem::RemoveFurniture(const TSet<int32>& Ids, TMap<FName, double>* OutRefund)
{
	if (Ids.Num() == 0)
	{
		return;
	}
	const TMap<FName, double> Refund = FurnitureRefundFor(Ids);
	TArray<FIntVector> Keys;
	for (const FRHFurnitureInstance& F : Furniture)
	{
		if (Ids.Contains(F.Id))
		{
			Keys.AddUnique(F.Key);
		}
	}
	const int32 Sold = Furniture.RemoveAll([&Ids](const FRHFurnitureInstance& F) { return Ids.Contains(F.Id); });
	RebuildFurnitureCache();
	CreditLander(Refund, /*bOutputShelf*/ true);
	if (OutRefund)
	{
		*OutRefund = Refund;
	}
	UE_LOG(LogRedHopeSim, Display, TEXT("Sold %d furnishing(s); %.0f kg Struct back to the Lander"), Sold, Refund.FindRef(NAME_Struct));
	for (const FIntVector& K : Keys)
	{
		OnFurnitureChanged.Broadcast(K);
	}
}

bool URHSimWorldSubsystem::FurnitureAllowsRezone(const FIntVector& Key, FName NewRoom, FString& OutReason) const
{
	const FRHCellFurnish* CF = FurnishedCells.Find(Key);
	if (!CF || CF->Ids.Num() == 0)
	{
		return true;
	}
	const FRHRoomRow* Room = (Defs && !NewRoom.IsNone()) ? Defs->GetRoom(NewRoom) : nullptr;
	for (const int32 Id : CF->Ids)
	{
		const FRHFurnitureInstance* F = FindFurniture(Id);
		if (!F)
		{
			continue;
		}
		const FRHFurnitureRow* FR = Defs ? Defs->GetFurniture(F->Row) : nullptr;
		if (!Room || !FR || !URHDefinitionsSubsystem::FurnitureAllowsFunction(*FR, Room->Function))
		{
			OutReason = FString::Printf(TEXT("The %s #%d can't stand in %s - sell it first"),
				FR ? *FR->DisplayName : *F->Row.ToString(), Id, Room ? *Room->DisplayName : TEXT("an unzoned space"));
			return false;
		}
	}
	return true;
}

void URHSimWorldSubsystem::BuildBedAssignments(const TArray<int32>& ByIdOrder)
{
	if (Furniture.Num() == 0)
	{
		return;
	}
	// Bed slots per home - a floor (Level, 0) or a habitat (0, HabId) - in
	// cell / bay order, then piece Id; each piece offers Capacity slots.
	TMap<FIntPoint, TArray<TPair<int64, int32>>> Slots;
	for (const FRHFurnitureInstance& F : Furniture)
	{
		const FRHFurnitureRow* Row = Defs ? Defs->GetFurniture(F.Row) : nullptr;
		if (!Row || Row->Provides != NAME_FurnBed || !IsLivingKey(F.Key))
		{
			continue;
		}
		const FIntPoint Home = F.Key.Z == 0 ? FIntPoint(F.Key.X, 0) : FIntPoint(0, F.Key.Z);
		for (int32 k = 0; k < Row->Capacity; ++k)
		{
			Slots.FindOrAdd(Home).Emplace((int64)F.Key.Y * 1000000 + F.Id, F.Id);
		}
	}
	for (auto& P : Slots)
	{
		P.Value.StableSort([](const TPair<int64, int32>& A, const TPair<int64, int32>& B) { return A.Key < B.Key; });
	}
	TMap<FIntPoint, int32> NextSlot;
	for (const int32 Id : ByIdOrder)
	{
		const FRHColonist* Col = FindColonist(Id);
		if (!Col)
		{
			continue;
		}
		const FIntPoint Home = Col->HomeHab != 0 ? FIntPoint(0, Col->HomeHab) : FIntPoint(Col->HomeLevel, 0);
		const TArray<TPair<int64, int32>>* S = Slots.Find(Home);
		int32& N = NextSlot.FindOrAdd(Home);
		if (S && S->IsValidIndex(N))
		{
			ColonistBedPiece.Add(Id, (*S)[N].Value);
			++N;
		}
	}
}

void URHSimWorldSubsystem::Debug_PlaceInstant(FName DefName, const FVector& LocationCm, int32 Level, int32 SizeX, int32 SizeY)
{
	if (!Defs || !Defs->GetBuilding(DefName))
	{
		UE_LOG(LogRedHopeSim, Error, TEXT("Debug_PlaceInstant: unknown building '%s'"), *DefName.ToString());
		return;
	}
	AddBuilding(DefName, LocationCm, /*bInstant*/ true, Level, SizeX, SizeY);
}

void URHSimWorldSubsystem::Debug_Showcase()
{
	if (!Defs)
	{
		return;
	}
	// Lay the whole canon set out on a grid north of the Lander so every
	// silhouette + glow is visible in one frame. Lander already sits at origin.
	TArray<FName> Names;
	Defs->ForEachBuilding([&Names](FName Name, const FRHBuildingRow&)
	{
		if (Name != NAME_Lander)
		{
			Names.Add(Name);
		}
	});
	Names.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });

	const int32 Cols = 4;
	const float SpacingCm = 1400.f;    // 14 m centres: big silhouettes clear each other
	const float OriginXCm = 1600.f;    // start north of the Lander
	// Drag-sized structures (the dome) show at 2 x 2 beds on their own row
	// past the grid, so the bigger shell overlaps nothing.
	TArray<FName> Sized;
	Names.RemoveAll([&](const FName& N) { const FRHBuildingRow* D = Defs->GetBuilding(N); if (D && D->DragSized) { Sized.Add(N); return true; } return false; });
	for (int32 i = 0; i < Names.Num(); ++i)
	{
		const int32 Row = i / Cols;
		const int32 Col = i % Cols;
		const FVector Loc(
			OriginXCm + Row * SpacingCm,
			(Col - (Cols - 1) * 0.5f) * SpacingCm,
			0.f);
		AddBuilding(Names[i], Loc, /*bInstant*/ true);
	}
	const int32 Rows = (Names.Num() + Cols - 1) / Cols;
	for (int32 i = 0; i < Sized.Num(); ++i)
	{
		AddBuilding(Sized[i], FVector(OriginXCm + (Rows + 0.5f) * SpacingCm + 600.f, (i - (Sized.Num() - 1) * 0.5f) * 2600.f, 0.f),
			/*bInstant*/ true, 0, 2, 2);
	}
	Names.Append(Sized);
	UE_LOG(LogRedHopeSim, Display, TEXT("[Showcase] placed %d building types"), Names.Num());
}

bool URHSimWorldSubsystem::IsInCoverage(const FVector& LocationCm, int32 Level) const
{
	for (const FRHBuildingInstance& B : Buildings)
	{
		if (B.bUnderConstruction || B.Level != Level)
		{
			continue;
		}
		if (const FRHBuildingRow* Def = Defs->GetBuilding(B.DefName))
		{
			const double RadiusCm = Def->CoverageRadius_m * 100.0;
			if (RadiusCm > 0.0 && FVector::DistXY(B.LocationCm, LocationCm) <= RadiusCm)
			{
				return true;
			}
		}
	}
	return false;
}

double URHSimWorldSubsystem::GetStock(FName Resource) const
{
	const double* Found = Stocks.Find(Resource);
	return Found ? *Found : 0.0;
}

void URHSimWorldSubsystem::AddStock(FName Resource, double Delta)
{
	double& Amount = Stocks.FindOrAdd(Resource);
	Amount += Delta;
	OnStockChanged.Broadcast(Resource, Amount);
}

double URHSimWorldSubsystem::GetTotalSolid(FName Resource) const
{
	double Total = 0.0;
	for (const FRHBuildingInstance& B : Buildings)
	{
		if (const double* In = B.InputKg.Find(Resource))
		{
			Total += *In;
		}
		if (const double* Out = B.OutputKg.Find(Resource))
		{
			Total += *Out;
		}
	}
	return Total;
}

TMap<FName, TPair<double, double>> URHSimWorldSubsystem::GetQuotaProgress() const
{
	TMap<FName, TPair<double, double>> Progress;
	if (const FRHQuotaRow* Quota = Defs ? Defs->GetQuota(ActiveQuota) : nullptr)
	{
		for (const auto& Req : URHDefinitionsSubsystem::ParseResourceList(Quota->Requirements))
		{
			double Have = GetTotalSolid(Req.Key);
			if (const double* Fluid = Stocks.Find(Req.Key))
			{
				Have += *Fluid;
			}
			// Round 0 keeps the raw bill (bit-identical to pre-D1a); renewals scale
			// it and round to whole kg so the shown bill is the threshold.
			const double Need = ScaleQuotaBill(Req.Value, QuotaRepeatRound);
			Progress.Add(Req.Key, TPair<double, double>(Have, Need));
		}
	}
	return Progress;
}

// --- Robot work API ---

FVector URHSimWorldSubsystem::GetSiteLocation(const FRHSiteRef& Site) const
{
	if (Site.RoadEdgeId > 0)
	{
		// C4: a road edge is worked from its midpoint.
		if (const FRHRoadEdge* Edge = RoadGraph.FindEdge(Site.RoadEdgeId))
		{
			return RoadGraph.EdgeMidpointCm(*Edge, RoadParams);
		}
	}
	if (Site.BuildingId > 0)
	{
		for (const FRHBuildingInstance& B : Buildings)
		{
			if (B.Id == Site.BuildingId)
			{
				return B.LocationCm;
			}
		}
	}
	if (Site.DepositId > 0)
	{
		for (const FRHDepositState& D : Deposits)
		{
			if (D.Id == Site.DepositId)
			{
				return D.LocationCm;
			}
		}
	}
	// Reaching here means the site is gone (or was never set). A valid reference
	// that resolves to nothing is a bug in whoever kept it: say so, once.
	ensureMsgf(!Site.IsValid(), TEXT("GetSiteLocation: site (building %d, deposit %d, road edge %d) does not exist"), Site.BuildingId, Site.DepositId, Site.RoadEdgeId);
	return FVector::ZeroVector;
}

int32 URHSimWorldSubsystem::GetSiteLevel(const FRHSiteRef& Site) const
{
	if (Site.BuildingId > 0)
	{
		for (const FRHBuildingInstance& B : Buildings)
		{
			if (B.Id == Site.BuildingId)
			{
				return B.Level;
			}
		}
	}
	if (Site.DepositId > 0)
	{
		for (const FRHDepositState& D : Deposits)
		{
			if (D.Id == Site.DepositId)
			{
				return D.Level;
			}
		}
	}
	return 0;
}

double URHSimWorldSubsystem::DigDeposit(int32 DepositId, double Kg)
{
	FRHDepositState* D = FindDeposit(DepositId);
	if (!D)
	{
		return 0.0;
	}
	const double Dug = FMath::Min3(Kg, D->RemainingKg, FMath::Max(0.0, PileCapKg - D->PileKg));
	D->RemainingKg -= Dug;
	D->PileKg += Dug;
	return Dug;
}

bool URHSimWorldSubsystem::IsDepositWorkable(int32 DepositId) const
{
	for (const FRHDepositState& D : Deposits)
	{
		if (D.Id == DepositId)
		{
			return D.bDesignated && D.RemainingKg > 0.0 && D.PileKg < PileCapKg;
		}
	}
	return false;
}

void URHSimWorldSubsystem::ReleaseDigClaim(int32 DepositId)
{
	if (FRHDepositState* D = FindDeposit(DepositId))
	{
		D->DigClaims = FMath::Max(0, D->DigClaims - 1);
	}
}

int32 URHSimWorldSubsystem::TryClaimDig(const FVector& RobotPosCm, int32 RobotLevel)
{
	if (bFleetHold)
	{
		return 0; // storm discipline: held fleets claim nothing new
	}
	int32 BestId = 0;
	double BestDist = TNumericLimits<double>::Max();
	for (FRHDepositState& D : Deposits)
	{
		if (D.Level == RobotLevel && D.bDesignated && D.RemainingKg > 0.0 && D.DigClaims < 2)
		{
			const double Dist = FVector::DistXY(D.LocationCm, RobotPosCm);
			if (Dist < BestDist)
			{
				BestDist = Dist;
				BestId = D.Id;
			}
		}
	}
	if (BestId != 0)
	{
		FindDeposit(BestId)->DigClaims++;
	}
	return BestId;
}

bool URHSimWorldSubsystem::TryClaimHaul(FMassEntityHandle Robot, const FVector& RobotPosCm, FRHTask& OutTask, int32 RobotLevel)
{
	if (bFleetHold)
	{
		return false;
	}
	FRHTask* Best = nullptr;
	double BestDist = TNumericLimits<double>::Max();
	for (FRHTask& T : Tasks)
	{
		// C6a: a freight task is the rovers' while one is active, until it has waited out the fallback.
		if (T.Type == ERHTaskType::Haul && !T.ClaimedBy.IsValid() && GetSiteLevel(T.From) == RobotLevel
			&& !(T.bFreight && T.UnclaimedAgentSec < FreightHaulerFallbackSec))
		{
			const double Dist = FVector::DistXY(GetSiteLocation(T.From), RobotPosCm);
			if (Dist < BestDist)
			{
				BestDist = Dist;
				Best = &T;
			}
		}
	}
	if (Best)
	{
		Best->ClaimedBy = Robot;
		OutTask = *Best;
		return true;
	}
	return false;
}

bool URHSimWorldSubsystem::HaulLoad(int32 TaskId, float CargoCapKg, float& OutLoadedKg, FVector& OutDropoffCm)
{
	FRHTask* T = FindTask(TaskId);
	if (!T)
	{
		return false;
	}
	const double WantKg = FMath::Min((double)CargoCapKg, T->AmountKg);
	double Taken = 0.0;

	if (T->From.DepositId > 0)
	{
		if (FRHDepositState* D = FindDeposit(T->From.DepositId))
		{
			Taken = FMath::Min(WantKg, D->PileKg);
			D->PileKg -= Taken;
		}
	}
	else if (FRHBuildingInstance* B = FindBuilding(T->From.BuildingId))
	{
		// Outputs first, then held inputs (stores like the Lander keep their
		// starter Struct in InputKg).
		if (double* Out = B->OutputKg.Find(T->Resource))
		{
			Taken = FMath::Min(WantKg, *Out);
			*Out -= Taken;
		}
		if (Taken < WantKg)
		{
			if (double* In = B->InputKg.Find(T->Resource))
			{
				const double More = FMath::Min(WantKg - Taken, *In);
				*In -= More;
				Taken += More;
			}
		}
	}

	if (Taken <= 0.0)
	{
		CompleteTask(TaskId); // stale task: source emptied since posting
		return false;
	}
	OutLoadedKg = (float)Taken;
	// Cross-level delivery drives to the shaft head; the lift takes the cargo
	// the last leg (robots are surface-bound until a later gate).
	OutDropoffCm = GetApproachPoint(T->To, /*RobotLevel*/ 0);
	return true;
}

void URHSimWorldSubsystem::HaulUnload(int32 TaskId, float CargoKg, FName ReturnResource)
{
	if (FRHTask* T = FindTask(TaskId))
	{
		if (FRHBuildingInstance* B = FindBuilding(T->To.BuildingId))
		{
			B->InputKg.FindOrAdd(T->Resource) += CargoKg;
			OnStockChanged.Broadcast(T->Resource, GetTotalSolid(T->Resource));
		}
		else if (!ReturnResource.IsNone())
		{
			ReturnHaulCargo(TaskId, ReturnResource, CargoKg); // a rover's load is never dropped on the ground
		}
		CompleteTask(TaskId);
	}
	else if (!ReturnResource.IsNone())
	{
		ReturnHaulCargo(TaskId, ReturnResource, CargoKg); // R3: the task is gone (a demolish removed it): the load goes to the Lander
	}
}

bool URHSimWorldSubsystem::IsDepositSpent(int32 DepositId) const
{
	for (const FRHDepositState& D : Deposits)
	{
		if (D.Id == DepositId)
		{
			return !D.bDesignated || D.RemainingKg <= 0.0;
		}
	}
	return true;
}

bool URHSimWorldSubsystem::TryClaimBuild(FMassEntityHandle Robot, const FVector& RobotPosCm, FRHTask& OutTask, int32 RobotLevel)
{
	if (bFleetHold)
	{
		return false;
	}
	FRHTask* Best = nullptr;
	double BestDist = TNumericLimits<double>::Max();
	for (FRHTask& T : Tasks)
	{
		// Cross-level claims allowed when the trunk links the floors: the
		// fabricator works the shaft head, the lift carries the work (M1-d).
		if (T.Type == ERHTaskType::Build && !T.ClaimedBy.IsValid() && AreLevelsLinked(GetSiteLevel(T.To), RobotLevel))
		{
			const double Dist = FVector::DistXY(GetApproachPoint(T.To, RobotLevel), RobotPosCm);
			if (Dist < BestDist)
			{
				BestDist = Dist;
				Best = &T;
			}
		}
	}
	if (Best)
	{
		Best->ClaimedBy = Robot;
		OutTask = *Best;
		return true;
	}
	return false;
}

bool URHSimWorldSubsystem::ApplyBuildWork(int32 BuildingId, double Seconds)
{
	FRHBuildingInstance* B = FindBuilding(BuildingId);
	if (!B || !B->bUnderConstruction)
	{
		return true;
	}
	// Fabrication waits for the full bill of materials on site (haulers are
	// bringing it; multi-resource since M1-a; Level-taxed since M1-d Gate B).
	const FRHBuildingRow* Def = Defs->GetBuilding(B->DefName);
	const TMap<FName, double> Cost = Def ? GetSiteBill(*B) : TMap<FName, double>();
	for (const auto& Line : Cost)
	{
		const double* Delivered = B->InputKg.Find(Line.Key);
		if (!Delivered || *Delivered + 0.5 < Line.Value)
		{
			return false; // standing by for materials; no work progress
		}
	}
	// Station tiers (Gate A): staffed workstation-line seats speed construction
	// alongside the Toolkit multiplier. Zero pop or an unarmed bonus knob
	// makes the factor exactly 1.0 - every pre-tier pin unchanged.
	B->BuildRemaining_s -= Seconds * FabricatorSpeedMul * GetWorkstationFabMul();
	if (B->BuildRemaining_s <= 0.0)
	{
		B->bUnderConstruction = false;
		B->BuildRemaining_s = 0.0;
		RebuildHabTopology(); // BM-1: a finished shell joins its network now
		// The delivered materials become the structure.
		for (const auto& Line : Cost)
		{
			double& Held = B->InputKg.FindOrAdd(Line.Key);
			Held = FMath::Max(0.0, Held - Line.Value);
			OnStockChanged.Broadcast(Line.Key, GetTotalSolid(Line.Key));
		}
		if (Def)
		{
			Power.BatteryWh += Def->StorageWh * 0.5; // constructed storage arrives half-charged
		}
		// ComputeModule (M1-c latency arc): local autonomy takes the uplink
		// from tier 0 to tier 1. min() so a manifest ComputeCore's tier 2 is
		// never regressed. OrderLagSeconds serializes - survives loads.
		if (B->DefName == FName("ComputeModule"))
		{
			const double Tier1 = Defs->GetConfigScalar(FName("OrderLagTier1_s"), 20.0);
			if (Tier1 < OrderLagSeconds)
			{
				OrderLagSeconds = Tier1;
				UE_LOG(LogRedHopeSim, Display, TEXT("Compute module online: order lag now %.0f sim-s"), OrderLagSeconds);
			}
		}
		UE_LOG(LogRedHopeSim, Display, TEXT("%s #%d construction complete"), *B->DefName.ToString(), B->Id);
		OnBuildingCompleted.Broadcast(*B);
		return true;
	}
	return false;
}

bool URHSimWorldSubsystem::FindNearestChargePad(const FVector& RobotPosCm, int32& OutBuildingId, FVector& OutLocationCm, int32 RobotLevel) const
{
	const FName NAME_ChargePad(TEXT("ChargePad"));
	double BestDist = TNumericLimits<double>::Max();
	OutBuildingId = 0;
	for (const FRHBuildingInstance& B : Buildings)
	{
		// A manually-off pad delivers nothing - robots must not dock at a dead
		// plug and wait forever (storm discipline ruling).
		if (!B.bUnderConstruction && !B.bManualOff && B.DefName == NAME_ChargePad && B.Level == RobotLevel)
		{
			const double Dist = FVector::DistXY(B.LocationCm, RobotPosCm);
			if (Dist < BestDist)
			{
				BestDist = Dist;
				OutBuildingId = B.Id;
				OutLocationCm = B.LocationCm;
			}
		}
	}
	if (OutBuildingId != 0)
	{
		return true;
	}
	// No working pad: the Lander is the emergency dock (see the header). A
	// Lander the player has switched off is no dock - the storm-discipline
	// rule that a dead plug must never draw robots still holds.
	for (const FRHBuildingInstance& B : Buildings)
	{
		if (B.DefName == NAME_Lander && !B.bUnderConstruction && !B.bManualOff && B.Level == RobotLevel)
		{
			OutBuildingId = B.Id;
			// Dock on the south side, clear of the hull (the 5x5-cell footprint
			// is 10 m across; the starting fleet deploys on this side too).
			OutLocationCm = B.LocationCm + FVector(-750.f, 0.f, 0.f);
			return true;
		}
	}
	return false;
}

double URHSimWorldSubsystem::RequestChargeWh(int32 PadBuildingId, double SubDt, FMassEntityHandle Robot)
{
	FRHBuildingInstance* Pad = FindBuilding(PadBuildingId);
	if (!Pad || Pad->bUnderConstruction || !Pad->bPowered)
	{
		return 0.0;
	}
	// The Lander emergency dock: slow, and every docked robot at once.
	const bool bLanderDock = Pad->DefName == NAME_Lander;
	// Queue etiquette (M1-b): one umbilical - only the head draws. A robot
	// that never joined (legacy brain) passes an invalid handle and bypasses.
	if (Robot.IsValid() && !bLanderDock)
	{
		const TArray<FMassEntityHandle>* Queue = PadQueues.Find(PadBuildingId);
		if (Queue && Queue->Num() > 0 && (*Queue)[0] != Robot)
		{
			return 0.0; // docked, waiting for the umbilical
		}
	}
	const FRHBuildingRow* Def = Defs->GetBuilding(Pad->DefName);
	const double RateW = bLanderDock ? LanderDockRateW : (Def ? Def->PowerDraw_W : 500.0);
	const double WantWh = RateW * (SubDt / 50.0);
	// Grid energy: from the bank if it holds charge, else from live surplus.
	if (Power.BatteryWh >= WantWh)
	{
		Power.BatteryWh -= WantWh;
		return WantWh;
	}
	if (Power.GenW > Power.LoadW)
	{
		return WantWh; // absorbed by generation surplus (slice approximation)
	}
	return 0.0; // night + empty bank: robot waits docked
}

void URHSimWorldSubsystem::JoinPadQueue(int32 PadBuildingId, FMassEntityHandle Robot)
{
	TArray<FMassEntityHandle>& Queue = PadQueues.FindOrAdd(PadBuildingId);
	Queue.AddUnique(Robot);
}

void URHSimWorldSubsystem::LeavePadQueue(int32 PadBuildingId, FMassEntityHandle Robot)
{
	if (TArray<FMassEntityHandle>* Queue = PadQueues.Find(PadBuildingId))
	{
		Queue->Remove(Robot);
	}
}

// --- Fleet reality (M1-b Gate B) ---

void URHSimWorldSubsystem::AccrueWear(float& Wear, float WearPerSol, float Dt) const
{
	// Weather tax (M1-c + director ruling 2026-07-07b): an active flare
	// multiplies exertion wear on exposed units (placeholder until M2's
	// electronic-fault system - flares become software faults repaired by
	// humans); a dust storm grinds working machinery at StormWearMul. Until
	// the M1-d vault / M2 warehouse exist, EVERY working unit is exposed;
	// docked at a pad is not shelter. The player is supposed to feel this gap.
	float Mul = 1.f;
	if (const FRHEventRow* Event = GetActiveEvent())
	{
		Mul = (Event->Type == FName("SolarFlare")) ? Event->Severity : StormWearMul;
	}
	Wear = FMath::Min(WearHaltThreshold, Wear + WearPerSol * Mul * (Dt / (float)URHSimClockSubsystem::SolLengthSimSeconds));
}

float URHSimWorldSubsystem::GetWearWorkMul(float Wear) const
{
	if (Wear <= WearDegradeThreshold)
	{
		return 1.f;
	}
	const float Span = FMath::Max(1.f, WearHaltThreshold - WearDegradeThreshold);
	return FMath::Max(0.f, 1.f - (Wear - WearDegradeThreshold) / Span);
}

bool URHSimWorldSubsystem::TryClaimSurvey(FMassEntityHandle Robot, const FVector& RobotPosCm, FRHTask& OutTask)
{
	if (bFleetHold)
	{
		return false;
	}
	FRHTask* Best = nullptr;
	double BestDist = TNumericLimits<double>::Max();
	for (FRHTask& T : Tasks)
	{
		if (T.Type == ERHTaskType::Survey && !T.ClaimedBy.IsValid())
		{
			const double Dist = FVector::DistXY(T.TargetCm, RobotPosCm);
			if (Dist < BestDist)
			{
				BestDist = Dist;
				Best = &T;
			}
		}
	}
	if (Best)
	{
		Best->ClaimedBy = Robot;
		OutTask = *Best;
		return true;
	}
	return false;
}

void URHSimWorldSubsystem::CompleteSurvey(int32 TaskId, double RadiusM)
{
	const FRHTask* T = FindTask(TaskId);
	if (!T)
	{
		return;
	}
	const FVector Point = T->TargetCm;
	int32 Found = 0;
	for (FRHDepositState& D : Deposits)
	{
		if (!D.bDiscovered && FVector::DistXY(D.LocationCm, Point) <= RadiusM * 100.0)
		{
			D.bDiscovered = true;
			++Found;
			UE_LOG(LogRedHopeSim, Display, TEXT("=== SURVEY: %s discovered - %.0f t %s at (%.0f, %.0f) m ==="),
				*D.RowName.ToString(), D.RemainingKg / 1000.0, *D.Type.ToString(),
				D.LocationCm.X / 100.0, D.LocationCm.Y / 100.0);
			OnDepositDiscovered.Broadcast(D);
		}
	}
	if (Found == 0)
	{
		UE_LOG(LogRedHopeSim, Display, TEXT("Survey complete at (%.0f, %.0f) m: nothing within %.0f m"),
			Point.X / 100.0, Point.Y / 100.0, RadiusM);
	}
	// The record is the player's map (director request): covered ground stays
	// known, including the empty circles - "nothing there" was paid for too.
	FRHSurveyRecord Record;
	Record.PointCm = Point;
	Record.RadiusM = (float)RadiusM;
	Record.Sol = Clock ? Clock->GetSol() : 0;
	Record.FoundCount = Found;
	SurveyHistory.Add(Record);
	OnSurveyCompleted.Broadcast(SurveyHistory.Last());
	// M4 Gate B: a survey may uncover a dormant settlement (seeded by survey #).
	MaybeDiscoverSettlement(SurveyHistory.Num());
	CompleteTask(TaskId);
}

bool URHSimWorldSubsystem::MaybeDiscoverSettlement(int32 SurveyCount)
{
	// A survey may uncover a DORMANT settlement (a rival row that ships
	// SliceActive=false and isn't yet discovered), unlocking trade + diplomacy.
	// Deterministic: a seeded roll (survey count) vs the discovery chance; the
	// FIRST dormant rival by sorted name is revealed.
	if (!Defs || DiscoveryChancePerSurvey <= 0.0)
	{
		return false;
	}
	TArray<TPair<FName, const FRHRivalRow*>> Dormant;
	Defs->ForEachRivalRow([&](FName Name, const FRHRivalRow& R)
	{
		if (!R.SliceActive && !DiscoveredRivals.Contains(Name))
		{
			Dormant.Emplace(Name, &R);
		}
	});
	if (Dormant.Num() == 0)
	{
		return false;
	}
	Dormant.Sort([](const TPair<FName, const FRHRivalRow*>& A, const TPair<FName, const FRHRivalRow*>& B)
	{ return A.Key.LexicalLess(B.Key); });
	const uint32 Seed = HashCombine(FCrc::StrCrc32(TEXT("survey")), (uint32)(SurveyCount * 2654435761u));
	const double U = (Seed % 100000u) / 100000.0;
	if (U >= DiscoveryChancePerSurvey)
	{
		return false;
	}
	const FName Name = Dormant[0].Key;
	DiscoveredRivals.Add(Name);
	RelationRef(Name); // seed its relation from RelationStart
	OnAlert.Broadcast(FString::Printf(
		TEXT("SETTLEMENT FOUND — your scouts have made contact with %s (%s). Trade and diplomacy are open."),
		*Dormant[0].Value->DisplayName, *Dormant[0].Value->Nation));
	UE_LOG(LogRedHopeSim, Display, TEXT("=== SETTLEMENT DISCOVERED: %s ==="), *Dormant[0].Value->DisplayName);
	return true;
}

bool URHSimWorldSubsystem::TryClaimRepair(FMassEntityHandle Self, FMassEntityHandle& OutTarget, FVector& OutTargetCm)
{
	if (bFleetHold)
	{
		return false;
	}
	if (GetStock(FName("SpareParts")) < 1.0)
	{
		return false; // no parts: nothing to offer the patient
	}
	URHAgentSubsystem* Agents = GetWorld()->GetSubsystem<URHAgentSubsystem>();
	if (!Agents)
	{
		return false;
	}
	FMassEntityHandle Best;
	FVector BestPos = FVector::ZeroVector;
	float BestWear = WearDegradeThreshold; // strictly-worse-than-threshold claims only
	Agents->ForEachRobotState([&](FMassEntityHandle Entity, const FVector& PosCm, float Wear)
	{
		if (Entity != Self && Wear >= BestWear && !RepairClaims.Contains(Entity))
		{
			Best = Entity;
			BestPos = PosCm;
			BestWear = Wear;
		}
	});
	if (!Best.IsValid())
	{
		return false;
	}
	RepairClaims.Add(Best);
	OutTarget = Best;
	OutTargetCm = BestPos;
	return true;
}

bool URHSimWorldSubsystem::GetRepairTargetPos(FMassEntityHandle Target, FVector& OutPosCm) const
{
	const URHAgentSubsystem* Agents = GetWorld()->GetSubsystem<URHAgentSubsystem>();
	return Agents && Agents->GetRobotPosition(Target, OutPosCm);
}

void URHSimWorldSubsystem::ApplyRepairAt(FMassEntityHandle Target)
{
	URHAgentSubsystem* Agents = GetWorld()->GetSubsystem<URHAgentSubsystem>();
	if (!Agents)
	{
		return;
	}
	// Spend parts against the wear, one part = RepairWearPerPart, bounded by
	// stock. Instant at slice scale - the visit is the cost, like site work.
	const FName NAME_SpareParts(TEXT("SpareParts"));
	int32 PartsSpent = 0;
	float WearRemoved = 0.f;
	while (GetStock(NAME_SpareParts) >= 1.0)
	{
		const float Removed = Agents->RemoveRobotWear(Target, RepairWearPerPart);
		if (Removed <= 0.f)
		{
			break; // clean (or target gone)
		}
		AddStock(NAME_SpareParts, -1.0);
		++PartsSpent;
		WearRemoved += Removed;
	}
	if (PartsSpent > 0)
	{
		UE_LOG(LogRedHopeSim, Display, TEXT("REPAIR: %.0f wear removed (%d parts, %.0f left in stock)"),
			WearRemoved, PartsSpent, GetStock(NAME_SpareParts));
	}
}

void URHSimWorldSubsystem::ReleaseRepairClaim(FMassEntityHandle Target)
{
	RepairClaims.Remove(Target);
}

double URHSimWorldSubsystem::GetManifestMassKg() const
{
	double Total = 0.0;
	for (const FName& Item : ManifestItems)
	{
		if (const FRHManifestItemRow* Row = Defs->GetManifestItem(Item))
		{
			Total += Row->Mass_kg;
		}
	}
	return Total;
}

int32 URHSimWorldSubsystem::GetImportStock(FName DefName) const
{
	const int32* Found = ImportStock.Find(DefName);
	return Found ? *Found : 0;
}

bool URHSimWorldSubsystem::AddManifestItem(FName ItemName, FString& OutError)
{
	if (QuotaPhase != ERHQuotaPhase::AwaitingManifest)
	{
		OutError = TEXT("No manifest is open (quota not met, or ship already launched)");
		return false;
	}
	const FRHManifestItemRow* Row = Defs->GetManifestItem(ItemName);
	if (!Row || !Row->SliceActive)
	{
		OutError = FString::Printf(TEXT("Unknown manifest item '%s'"), *ItemName.ToString());
		return false;
	}
	if (GetManifestMassKg() + Row->Mass_kg > AwardMassKg)
	{
		OutError = FString::Printf(TEXT("Over budget: %.0f + %.0f > %.0f kg"),
			GetManifestMassKg(), Row->Mass_kg, AwardMassKg);
		return false;
	}
	ManifestItems.Add(ItemName);
	UE_LOG(LogRedHopeSim, Display, TEXT("Manifest: +%s (%.0f kg) -> %.0f / %.0f kg"),
		*ItemName.ToString(), Row->Mass_kg, GetManifestMassKg(), AwardMassKg);
	return true;
}

bool URHSimWorldSubsystem::LaunchShip(FString& OutError)
{
	if (QuotaPhase != ERHQuotaPhase::AwaitingManifest)
	{
		OutError = TEXT("No manifest is open");
		return false;
	}
	const double TransitSols = Defs->GetConfigScalar(FName("ShipTransitSols"), 3.0);
	ShipArrivalSimSeconds = Clock->GetSimSecondsTotal() + TransitSols * URHSimClockSubsystem::SolLengthSimSeconds;
	ShipAlertStage = 0; // fresh countdown per launch
	QuotaPhase = ERHQuotaPhase::ShipInbound;
	UE_LOG(LogRedHopeSim, Display, TEXT("Ship launched: %d items, %.0f/%.0f kg, arrival in %.0f sols"),
		ManifestItems.Num(), GetManifestMassKg(), AwardMassKg, TransitSols);
	return true;
}

void URHSimWorldSubsystem::CompleteTask(int32 TaskId)
{
	Tasks.RemoveAll([TaskId](const FRHTask& T) { return T.Id == TaskId; });
}

void URHSimWorldSubsystem::AbandonTask(int32 TaskId)
{
	if (FRHTask* T = FindTask(TaskId))
	{
		T->ClaimedBy = FMassEntityHandle();
	}
}

// --- helpers ---

FRHBuildingInstance* URHSimWorldSubsystem::FindBuilding(int32 Id)
{
	for (FRHBuildingInstance& B : Buildings)
	{
		if (B.Id == Id)
		{
			return &B;
		}
	}
	return nullptr;
}

FRHDepositState* URHSimWorldSubsystem::FindDeposit(int32 Id)
{
	for (FRHDepositState& D : Deposits)
	{
		if (D.Id == Id)
		{
			return &D;
		}
	}
	return nullptr;
}

FRHTask* URHSimWorldSubsystem::FindTask(int32 Id)
{
	for (FRHTask& T : Tasks)
	{
		if (T.Id == Id)
		{
			return &T;
		}
	}
	return nullptr;
}

bool URHSimWorldSubsystem::HasOpenTask(ERHTaskType Type, const FRHSiteRef& From, const FRHSiteRef& To, FName Resource) const
{
	for (const FRHTask& T : Tasks)
	{
		if (T.Type == Type && T.From.BuildingId == From.BuildingId && T.From.DepositId == From.DepositId
			&& T.To.BuildingId == To.BuildingId && T.To.DepositId == To.DepositId
			&& T.From.RoadEdgeId == From.RoadEdgeId && T.To.RoadEdgeId == To.RoadEdgeId
			&& (Resource.IsNone() || T.Resource == Resource))
		{
			return true;
		}
	}
	return false;
}

// --- Save / load ---

bool URHSimWorldSubsystem::SaveColony(const FString& Slot, FString& OutError)
{
	if (!Clock || !Defs)
	{
		OutError = TEXT("Sim not initialized");
		return false;
	}

	// Snapshot copies: claims cleared, in-flight hauler cargo returned to its
	// source so mass is conserved without serializing robot intent.
	TArray<FRHBuildingInstance> SavedBuildings = Buildings;
	TArray<FRHDepositState> SavedDeposits = Deposits;
	for (FRHDepositState& D : SavedDeposits)
	{
		D.DigClaims = 0;
	}
	TArray<FRHTask> SavedTasks;
	for (const FRHTask& T : Tasks)
	{
		FRHTask Copy = T;
		Copy.ClaimedBy = FMassEntityHandle();
		SavedTasks.Add(Copy);
	}

	TArray<FRHRobotSaveState> Robots;
	if (URHAgentSubsystem* Agents = GetWorld()->GetSubsystem<URHAgentSubsystem>())
	{
		Agents->CollectRobotStates(Robots);
	}
	for (FRHRobotSaveState& R : Robots)
	{
		if (R.CargoKg <= 0.f || R.TaskId == 0)
		{
			continue;
		}
		for (const FRHTask& T : SavedTasks)
		{
			if (T.Id != R.TaskId)
			{
				continue;
			}
			if (T.From.DepositId > 0)
			{
				for (FRHDepositState& D : SavedDeposits)
				{
					if (D.Id == T.From.DepositId)
					{
						D.PileKg += R.CargoKg;
						break;
					}
				}
			}
			else if (T.From.BuildingId > 0)
			{
				FRHBuildingInstance* Back = SavedBuildings.FindByPredicate([&T](const FRHBuildingInstance& B) { return B.Id == T.From.BuildingId; });
				if (!Back)
				{
					// BM-3: the source was demolished - the cargo goes to the Lander.
					Back = SavedBuildings.FindByPredicate([](const FRHBuildingInstance& B) { return B.DefName == NAME_Lander; });
				}
				if (Back)
				{
					Back->OutputKg.FindOrAdd(R.CargoResource) += R.CargoKg;
				}
			}
			break;
		}
		R.CargoKg = 0.f;
		R.TaskId = 0;
	}

	TArray<uint8> Bytes;
	FMemoryWriter Raw(Bytes);
	FNameAsStringProxyArchive Ar(Raw);

	uint32 Magic = RHSaveMagic, Version = RHSaveVersion;
	double SimSeconds = Clock->GetSimSecondsTotal();
	float Speed = Clock->GetSpeed();
	Ar << Magic << Version << SimSeconds << Speed;
	WriteFloorTable(Ar); // v41: the floor table sits straight after the header (a load validates it before any teardown)

	Ar << OrderLagSeconds << FabricatorSpeedMul << NextBuildingId << NextTaskId;
	uint8 Phase = (uint8)QuotaPhase;
	Ar << Phase << AwardMassKg << QuotaMetSol << ShipArrivalSimSeconds << ManifestItems;
	Ar << Power.BatteryWh;
	SerializeResourceMap(Ar, Stocks);
	Ar << ImportStock;

	int32 Num = UplinkQueue.Num();
	Ar << Num;
	for (FRHCommand& C : UplinkQueue)
	{
		Ar << C.Verb << C.Target << C.Location << C.LocationB << C.Level << C.Value << C.IssuedAtSimSeconds << C.ExecuteAtSimSeconds // LocationB: v36
		   << C.CommandId << C.Text; // CommandId and Text: v37
	}

	Num = SavedBuildings.Num();
	Ar << Num;
	for (FRHBuildingInstance& B : SavedBuildings)
	{
		Ar << B.Id << B.DefName << B.LocationCm << B.Level << B.bUnderConstruction << B.bPowered
		   << B.BuildRemaining_s << B.BatchRemaining_h << B.ActiveRecipe << B.AttachedDepositId
		   << B.bBatchOnH2 << B.bManualOff << B.SizeX << B.SizeY // size: v28
		   << B.bDayShift; // day shift: v33
		SerializeResourceMap(Ar, B.InputKg);
		SerializeResourceMap(Ar, B.OutputKg);
	}

	Num = PendingOutputs.Num();
	Ar << Num;
	for (auto& P : PendingOutputs)
	{
		int32 BuildingId = P.Key;
		Ar << BuildingId;
		SerializeResourceMap(Ar, P.Value);
	}

	Num = SavedDeposits.Num();
	Ar << Num;
	for (FRHDepositState& D : SavedDeposits)
	{
		Ar << D.Id << D.RowName << D.Type << D.RemainingKg << D.PileKg << D.LocationCm << D.Level << D.bDiscovered << D.bDesignated;
	}

	Num = SavedTasks.Num();
	Ar << Num;
	for (FRHTask& T : SavedTasks)
	{
		uint8 Type = (uint8)T.Type;
		Ar << T.Id << Type;
		SerializeSite(Ar, T.From);
		SerializeSite(Ar, T.To);
		Ar << T.Resource << T.AmountKg << T.TargetCm;
	}

	Num = Robots.Num();
	Ar << Num;
	for (FRHRobotSaveState& R : Robots)
	{
		Ar << R.DefName << R.PosCm << R.Level << R.ChargeWh << R.Wear;
	}

	Num = SurveyHistory.Num();
	Ar << Num;
	for (FRHSurveyRecord& S : SurveyHistory)
	{
		Ar << S.PointCm << S.RadiusM << S.Sol << S.FoundCount;
	}

	// Shaft & excavation + designations (save v5/v6).
	Ar << ShaftDepth << ShaftHeadCm << SpoilPileKg;
	Ar << BoreTargetDepth << CarveQueue << PendingBoreWork;
	// Habitability chain (save v7).
	Ar << FloorO2Kg << RatedFloors << bVaultRated << bFleetHold;
	// Population (save v10).
	{
		int32 PopNum = Colonists.Num();
		Ar << PopNum << NextColonistId;
		if (Ar.IsLoading())
		{
			Colonists.SetNum(PopNum);
		}
		for (FRHColonist& C : Colonists)
		{
			Ar << C.Id << C.Name << C.HomeLevel << C.HomeHab << C.bSupported << C.UnsupportedSimSeconds << C.bMarsBorn; // HomeHab: v29, bMarsBorn: v34
		// Skills (save v24): sorted for deterministic bytes; load repopulates.
		if (Ar.IsLoading())
		{
			int32 SkillCount = 0;
			Ar << SkillCount;
			C.SkillSols.Empty();
			for (int32 Sk = 0; Sk < SkillCount; ++Sk)
			{
				FName Fn; double Sols = 0.0;
				Ar << Fn << Sols;
				C.SkillSols.Add(Fn, Sols);
			}
		}
		else
		{
			TArray<FName> Fns;
			C.SkillSols.GetKeys(Fns);
			Fns.Sort([](const FName& X, const FName& Y){ return X.LexicalLess(Y); });
			int32 SkillCount = Fns.Num();
			Ar << SkillCount;
			for (const FName& Fn : Fns)
			{
				FName FnCopy = Fn; double Sols = C.SkillSols[Fn];
				Ar << FnCopy << Sols;
			}
		}
		}
	}
	// The Mars-born recalled to orbit, oldest first (save v42, F2b).
	Ar << RecalledMarsBorn;
	// Shared rationing (save v34, C2a): the deficit Hope reads, and each short
	// home's departure accumulator and onset size, sorted by home key for
	// deterministic bytes.
	{
		Ar << DeficitEquivalent;
		TArray<int32> AccKeys;
		HomeClocks.GetKeys(AccKeys);
		AccKeys.Sort();
		int32 AccCount = AccKeys.Num();
		Ar << AccCount;
		for (const int32 AccKey : AccKeys)
		{
			int32 KeyCopy = AccKey;
			FRHHomeClock Saved = HomeClocks[AccKey];
			Ar << KeyCopy << Saved.Acc << Saved.OnsetResidents;
		}
	}
	// Arrivals at scale (save v35, C2b): the Big Arrival's streak and once-only flag,
	// the move-home accumulator, and the settlers who came with ships.
	{
		uint8 BigDone = bBigArrivalDone ? 1 : 0;
		Ar << BigArrivalStreakSols << BigDone << MoveHomeAccSols << ImmigrantsTotal;
	}
	// Room designations (save v11).
	Ar << FloorRoomCells;
	// The garden (save v12).
	{
		TArray<FIntVector> Planted = PlantedCells.Array();
		Planted.Sort([](const FIntVector& A, const FIntVector& B){ return A.X != B.X ? A.X < B.X : (A.Y != B.Y ? A.Y < B.Y : A.Z < B.Z); });
		Ar << Planted << bFirstCropAnnounced;
	}
	// Hope-drives (save v13). Smoothed mood + band, so a mid-swing save/load is
	// exact-equivalent (re-deriving the band would differ inside a hysteresis gap).
	{
		uint8 BandByte = (uint8)HopeBand;
		Ar << HopeSmoothed << BandByte;
	}
	// Water potability (save v14).
	Ar << WaterPotability;
	// Generational growth (save v15).
	Ar << HopeGrowthStreakSols << bFirstBornAnnounced << BirthsOnMars;
	// Discoveries (save v16).
	Ar << DiscoveryLog << DiscoverySeatHours;
	// Rivals & trade (save v17).
	{
		uint8 Returning = bConvoyReturning ? 1 : 0;
		Ar << ConvoyRival << Returning << ConvoyLegSols << RivalRelations;
	}
	// Earth's Shadow (save v18).
	{
		uint8 Demand = bEarthDemandPending ? 1 : 0;
		Ar << EarthTension << IdentityAxis << Demand;
	}
	// The Solidarity Dilemma (save v19).
	{
		TArray<FName> Closed = ClosedRoutes.Array();
		Closed.Sort([](const FName& A, const FName& B){ return A.LexicalLess(B); });
		Ar << Closed << SolidarityHope;
	}
	// The covert layer (save v20).
	Ar << HiddenTensions << HumanNatureAxis << CovertAttempts;
	// The espionage economy (save v21).
	{
		TArray<FName> Discovered = DiscoveredRivals.Array();
		Discovered.Sort([](const FName& A, const FName& B){ return A.LexicalLess(B); });
		Ar << SabotageSols << Discovered;
	}
	// Earth's pre-emptive pressure (save v22).
	{
		TArray<FName> Defected = DefectedRivals.Array();
		Defected.Sort([](const FName& A, const FName& B){ return A.LexicalLess(B); });
		Ar << EmbargoGrace << Defected << Influence;
	}
	// Dynamic crises + endings (save v23).
	{
		uint8 Crisis = (uint8)ActiveCrisis, Ever = bEverHadCrew ? 1 : 0, Declared = bEndingDeclared ? 1 : 0;
		Ar << Crisis << CrisisRemainingSols << CrisisCheckSols << CrisisCount << Ever << Declared;
	}
	// The alive pass (save v24).
	{
		uint8 Harvest = bFirstHarvestAnnounced ? 1 : 0;
		Ar << MomentSols << MomentCounter << Harvest << GardenFoodCumKg;
	}
	// The goal ladder + ending resolution + research funding (save v25).
	{
		uint8 Epilogue = bEpilogueDeclared ? 1 : 0, Quiet = bQuietAnnounced ? 1 : 0;
		Ar << ActiveQuota << QuotaOpenedSol << PendingResearchFundingKg
		   << DeclaredEnding << PathSustainSols << Epilogue << Quiet;
	}
	// Crops + climate + ducts (save v26). Maps/sets serialize SORTED so the
	// bytes are deterministic regardless of insertion history.
	{
		TArray<FIntVector> CropKeys;
		PlantedCrops.GetKeys(CropKeys);
		CropKeys.Sort([](const FIntVector& A, const FIntVector& B){ return A.X != B.X ? A.X < B.X : (A.Y != B.Y ? A.Y < B.Y : A.Z < B.Z); });
		int32 CropCount = CropKeys.Num();
		Ar << CropCount;
		for (const FIntVector& K : CropKeys)
		{
			FRHPlantedCropState& S = PlantedCrops[K];
			FIntVector Key = K;
			Ar << Key << S.Crop << S.PlantedSol << S.SoilKg;
		}
		TArray<int32> ClimateKeys;
		FloorClimate.GetKeys(ClimateKeys);
		ClimateKeys.Sort();
		int32 ClimateCount = ClimateKeys.Num();
		Ar << ClimateCount;
		for (const int32 K : ClimateKeys)
		{
			int32 Key = K; FName Band = FloorClimate[K];
			Ar << Key << Band;
		}
		TArray<int32> Ducts = DuctedFloors.Array();
		Ducts.Sort();
		Ar << Ducts << ColonyCO2Kg << PlantRotation;
	}
	// Staffing (save v27): the player's seat order (order-preserving) and the
	// lazy-fill state.
	{
		Ar << SeatPriority << StaffedSeats << SeatFillSols;
	}
	// Surface habitats (save v29). Sorted for deterministic bytes; topology,
	// networks, circulation, beds, stations and Hope terms are DERIVED.
	{
		TArray<int32> O2Keys;
		HabO2Kg.GetKeys(O2Keys);
		O2Keys.Sort();
		int32 O2Count = O2Keys.Num();
		Ar << O2Count;
		for (const int32 K : O2Keys)
		{
			int32 Key = K;
			double V = HabO2Kg[K];
			Ar << Key << V;
		}
		TArray<int32> Rated = RatedHabs.Array();
		Rated.Sort();
		Ar << Rated;
		TArray<int32> RoomKeys;
		HabBayRooms.GetKeys(RoomKeys);
		RoomKeys.Sort();
		int32 RoomCount = RoomKeys.Num();
		Ar << RoomCount;
		for (const int32 K : RoomKeys)
		{
			int32 Key = K;
			TArray<FName> Rooms = HabBayRooms[K];
			Ar << Key << Rooms;
		}
	}
	// Furniture (save v30): pieces in Id order. The cell cache and the bed /
	// seat assignments are DERIVED.
	{
		Ar << NextFurnitureId;
		int32 FurnCount = Furniture.Num();
		Ar << FurnCount;
		for (const FRHFurnitureInstance& F : Furniture)
		{
			int32 FId = F.Id;
			FName FRow = F.Row;
			FIntVector FKey = F.Key;
			int32 FSlot = F.Slot;
			int32 FRot = F.Rot;
			Ar << FId << FRow << FKey << FSlot << FRot;
		}
	}
	// Wall fittings (save v31): Id order. The edge index is DERIVED.
	{
		Ar << NextWallFeatureId;
		int32 WallCount = WallFeatures.Num();
		Ar << WallCount;
		for (const FRHWallFeature& W : WallFeatures)
		{
			int32 WId = W.Id;
			FName WRow = W.Row;
			FIntVector WEdge = W.Edge;
			Ar << WId << WRow << WEdge;
		}
	}
	// Quota repeat round (save v32, D1a).
	Ar << QuotaRepeatRound;
	// The road graph (save v36, C4): nodes, edges and their build progress, in Id
	// order, then the next Ids. The lookups, the route adjacency and every robot's
	// trip are DERIVED: a robot re-plans from the loaded graph.
	{
		int32 NodeCount = RoadGraph.Nodes.Num();
		Ar << NodeCount;
		for (const FRHRoadNode& N : RoadGraph.Nodes)
		{
			int32 NId = N.Id;
			FIntPoint NGrid = N.GridPos;
			Ar << NId << NGrid;
		}
		int32 EdgeCount = RoadGraph.Edges.Num();
		Ar << EdgeCount;
		for (const FRHRoadEdge& E : RoadGraph.Edges)
		{
			int32 EId = E.Id, EA = E.A, EB = E.B, ELen = E.LengthCm, ERegion = E.RegionId;
			uint8 EBuilt = E.bBuilt ? 1 : 0;
			float EProgress = E.BuildProgress;
			Ar << EId << EA << EB << ELen << EBuilt << EProgress << ERegion;
		}
		int32 NextNode = RoadGraph.NextNodeId, NextEdge = RoadGraph.NextEdgeId;
		Ar << NextNode << NextEdge;
	}
	// Districts (save v37, C5a): in Id order, each with its sorted lattice, then the next Id.
	// Membership, the ground maps and the counts are DERIVED and rebuilt on first read.
	{
		int32 DistrictCount = Districts.Num();
		Ar << DistrictCount;
		for (const FRHDistrict& D : Districts)
		{
			int32 DId = D.Id, DRegion = D.RegionId, DLevel = D.Level;
			FName DKind = D.Kind;
			FString DName = D.Name;
			TArray<FIntPoint> DLattice = D.Lattice;
			uint8 DGrowth = D.bGrowth ? 1 : 0; // v38
			Ar << DId << DKind << DName << DRegion << DLevel << DLattice << DGrowth;
		}
		int32 NextDistrict = NextDistrictId;
		Ar << NextDistrict;
	}
	// City growth (save v38, C5b): the round-robin cursor and the sol whose boundary has been checked, then
	// the open projects in ascending DistrictId, each with its bill in name order. The notes, the pause and
	// this sol's alert tallies are runtime readouts and are never saved.
	{
		Ar << GrowthCursor << GrowthCheckedSol;
		int32 ProjectCount = GrowthProjects.Num();
		Ar << ProjectCount;
		for (const FRHGrowthProject& P : GrowthProjects)
		{
			int32 PDistrict = P.DistrictId, PLevel = P.Level, PHab = P.HabId, PCells = P.Cells, PFirst = P.FirstCell, PIndex = P.Index;
			uint8 PKind = (uint8)P.Kind, PCarve = P.bCarve ? 1 : 0;
			FIntPoint PCell = P.Cell, PSize = P.Size;
			FName PRoom = P.Room;
			double PTotal = P.TotalSols, PRemaining = P.RemainingSols;
			Ar << PDistrict << PKind << PLevel << PCell << PSize << PHab << PCells << PFirst << PIndex << PCarve << PRoom << PTotal << PRemaining;
			TArray<FName> BillKeys;
			P.Bill.GetKeys(BillKeys);
			BillKeys.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
			int32 BillCount = BillKeys.Num();
			Ar << BillCount;
			for (const FName& K : BillKeys)
			{
				FName Key = K;
				double Kg = P.Bill.FindChecked(K);
				Ar << Key << Kg;
			}
		}
	}

	// The City / Earth share and the production ledger (save v39, C5d): the player's standing share, then the
	// open sol's tally per leg, the ring of completed sols (its fill, its head and every slot) and the sol the
	// ledger last rolled at. The windowed means are derived.
	{
		double Share = CityShare;
		Ar << Share;
		int32 RingFilled = ProdRingFilled, RingHead = ProdRingHead, CheckedSol = ProductionCheckedSol;
		Ar << RingFilled << RingHead << CheckedSol;
		for (int32 Leg = 0; Leg < ProdLegs; ++Leg)
		{
			double Acc = ProdAccKg[Leg];
			Ar << Acc;
			for (int32 i = 0; i < ProdMaxWindow; ++i)
			{
				double Kg = ProdRing[Leg][i];
				Ar << Kg;
			}
		}
	}

	const FString Path = SaveSlotToPath(Slot);
	if (!FFileHelper::SaveArrayToFile(Bytes, *Path))
	{
		OutError = FString::Printf(TEXT("Could not write %s"), *Path);
		return false;
	}
	UE_LOG(LogRedHopeSim, Display, TEXT("Colony saved: slot '%s' (%d bytes, sol %d, %d buildings, %d robots)"),
		*Slot, Bytes.Num(), Clock->GetSol(), SavedBuildings.Num(), Robots.Num());
	return true;
}

bool URHSimWorldSubsystem::LoadColony(const FString& Slot, FString& OutError)
{
	if (!Clock || !Defs)
	{
		OutError = TEXT("Sim not initialized");
		return false;
	}
	const FString Path = SaveSlotToPath(Slot);
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *Path))
	{
		OutError = FString::Printf(TEXT("No save at %s"), *Path);
		return false;
	}

	FMemoryReader Raw(Bytes);
	FNameAsStringProxyArchive Ar(Raw);

	uint32 Magic = 0, Version = 0;
	double SimSeconds = 0.0;
	float Speed = 1.f;
	Ar << Magic << Version << SimSeconds << Speed;
	if (Magic != RHSaveMagic || Version != RHSaveVersion)
	{
		OutError = FString::Printf(TEXT("Save format mismatch (magic %08x version %u; expected %08x/%u)"),
			Magic, Version, RHSaveMagic, RHSaveVersion);
		return false;
	}

	// v41: the floor table, validated before anything live is torn down. A bad table is a refusal through
	// OutError, never an Error line, and leaves the running colony exactly as it was.
	TArray<FRHFloor> LoadedFloors;
	if (!ReadFloorTable(Ar, LoadedFloors, OutError))
	{
		return false;
	}

	// Tear down the live colony before applying the snapshot.
	if (URHAgentSubsystem* Agents = GetWorld()->GetSubsystem<URHAgentSubsystem>())
	{
		Agents->DespawnAllRobots();
	}
	Buildings.Reset();
	Deposits.Reset();
	Tasks.Reset();
	UplinkQueue.Reset();
	Stocks.Reset();
	ImportStock.Reset();
	PendingOutputs.Reset();
	ManifestItems.Reset();
	FleetCounts.Reset();
	RepairClaims.Reset();  // claim state is runtime-only; robots re-claim
	LastBlackoutNight = -1; // D5a runtime latch: re-seen within a step if the blackout continues
	PadQueues.Reset();
	SurveyHistory.Reset();
	bFleetDeployed = true; // robots respawn explicitly below

	Ar << OrderLagSeconds << FabricatorSpeedMul << NextBuildingId << NextTaskId;
	uint8 Phase = 0;
	Ar << Phase << AwardMassKg << QuotaMetSol << ShipArrivalSimSeconds << ManifestItems;
	QuotaPhase = (ERHQuotaPhase)Phase;
	Ar << Power.BatteryWh;
	SerializeResourceMap(Ar, Stocks);
	Ar << ImportStock;

	int32 Num = 0;
	Ar << Num;
	for (int32 i = 0; i < Num; ++i)
	{
		FRHCommand C;
		Ar << C.Verb << C.Target << C.Location << C.LocationB << C.Level << C.Value << C.IssuedAtSimSeconds << C.ExecuteAtSimSeconds // LocationB: v36
		   << C.CommandId << C.Text; // CommandId and Text: v37
		// A loaded order keeps its own handle; later orders count on from past the highest
		// (never into a loaded order's Id, never below the live counter).
		NextCommandId = FMath::Max(NextCommandId, C.CommandId + 1);
		UplinkQueue.Add(MoveTemp(C));
	}

	Ar << Num;
	for (int32 i = 0; i < Num; ++i)
	{
		FRHBuildingInstance B;
		Ar << B.Id << B.DefName << B.LocationCm << B.Level << B.bUnderConstruction << B.bPowered
		   << B.BuildRemaining_s << B.BatchRemaining_h << B.ActiveRecipe << B.AttachedDepositId
		   << B.bBatchOnH2 << B.bManualOff << B.SizeX << B.SizeY // size: v28
		   << B.bDayShift; // day shift: v33
		SerializeResourceMap(Ar, B.InputKg);
		SerializeResourceMap(Ar, B.OutputKg);
		Buildings.Add(MoveTemp(B));
	}
	RebuildHabTopology(); // BM-1: derived, never saved

	Ar << Num;
	for (int32 i = 0; i < Num; ++i)
	{
		int32 BuildingId = 0;
		TMap<FName, double> Outputs;
		Ar << BuildingId;
		SerializeResourceMap(Ar, Outputs);
		PendingOutputs.Add(BuildingId, MoveTemp(Outputs));
	}

	Ar << Num;
	for (int32 i = 0; i < Num; ++i)
	{
		FRHDepositState D;
		Ar << D.Id << D.RowName << D.Type << D.RemainingKg << D.PileKg << D.LocationCm << D.Level << D.bDiscovered << D.bDesignated;
		Deposits.Add(MoveTemp(D));
	}
	// D2a: the surveyed mass is data, not save state - re-derive it by row.
	{
		TMap<FName, double> RowMass;
		Defs->ForEachDeposit([&RowMass](FName RowName, const FRHDepositRow& Row) { RowMass.Add(RowName, Row.Mass_kg); });
		for (FRHDepositState& D : Deposits)
		{
			if (const double* Mass = RowMass.Find(D.RowName))
			{
				D.InitialKg = *Mass;
			}
			else
			{
				UE_LOG(LogRedHopeSim, Warning, TEXT("Load: deposit row '%s' is gone from data - its low-ice alert is off"), *D.RowName.ToString());
			}
		}
	}

	Ar << Num;
	for (int32 i = 0; i < Num; ++i)
	{
		FRHTask T;
		uint8 Type = 0;
		Ar << T.Id << Type;
		SerializeSite(Ar, T.From);
		SerializeSite(Ar, T.To);
		Ar << T.Resource << T.AmountKg << T.TargetCm;
		T.Type = (ERHTaskType)Type;
		Tasks.Add(MoveTemp(T));
	}

	Ar << Num;
	TArray<FMassEntityHandle> Respawned;
	for (int32 i = 0; i < Num; ++i)
	{
		FRHRobotSaveState R;
		Ar << R.DefName << R.PosCm << R.Level << R.ChargeWh << R.Wear;
		if (const FRHRobotRow* Row = Defs->GetRobot(R.DefName))
		{
			SpawnRobotTracked(R.DefName, *Row, R.PosCm, R.ChargeWh, R.Wear, Respawned);
		}
	}

	Ar << Num;
	for (int32 i = 0; i < Num; ++i)
	{
		FRHSurveyRecord S;
		Ar << S.PointCm << S.RadiusM << S.Sol << S.FoundCount;
		SurveyHistory.Add(S);
	}

	// Shaft & excavation + designations (save v5/v6). Maps repopulate wholesale.
	FloorTable = MoveTemp(LoadedFloors);
	RebuildFloorIndex();
	++FloorCellsVersion;
	Ar << ShaftDepth << ShaftHeadCm << SpoilPileKg;
	CarveQueue.Empty();
	PendingBoreWork.Empty();
	Ar << BoreTargetDepth << CarveQueue << PendingBoreWork;
	// Habitability chain (save v7).
	FloorO2Kg.Empty();
	RatedFloors.Empty();
	Ar << FloorO2Kg << RatedFloors << bVaultRated << bFleetHold;
	// A rated floor with no floor in the table cannot have been saved by this build: repair and keep (D1).
	for (TSet<int32>::TIterator It(RatedFloors); It; ++It)
	{
		if (!FloorIndexByLevel.Contains(*It))
		{
			UE_LOG(LogRedHopeSim, Warning, TEXT("Load: floor %d was rated but has no cells in the floor table - the rating is dropped"), *It);
			It.RemoveCurrent();
		}
	}
	// Population (save v10).
	{
		int32 PopNum = Colonists.Num();
		Ar << PopNum << NextColonistId;
		if (Ar.IsLoading())
		{
			Colonists.SetNum(PopNum);
		}
		for (FRHColonist& C : Colonists)
		{
			Ar << C.Id << C.Name << C.HomeLevel << C.HomeHab << C.bSupported << C.UnsupportedSimSeconds << C.bMarsBorn; // HomeHab: v29, bMarsBorn: v34
		// Skills (save v24): sorted for deterministic bytes; load repopulates.
		if (Ar.IsLoading())
		{
			int32 SkillCount = 0;
			Ar << SkillCount;
			C.SkillSols.Empty();
			for (int32 Sk = 0; Sk < SkillCount; ++Sk)
			{
				FName Fn; double Sols = 0.0;
				Ar << Fn << Sols;
				C.SkillSols.Add(Fn, Sols);
			}
		}
		else
		{
			TArray<FName> Fns;
			C.SkillSols.GetKeys(Fns);
			Fns.Sort([](const FName& X, const FName& Y){ return X.LexicalLess(Y); });
			int32 SkillCount = Fns.Num();
			Ar << SkillCount;
			for (const FName& Fn : Fns)
			{
				FName FnCopy = Fn; double Sols = C.SkillSols[Fn];
				Ar << FnCopy << Sols;
			}
		}
		}
	}
	// The Mars-born recalled to orbit, oldest first (save v42, F2b).
	Ar << RecalledMarsBorn;
	// Shared rationing (save v34, C2a). The per-home readout is derived: it is
	// rebuilt by the next population step.
	{
		Ar << DeficitEquivalent;
		HomeClocks.Reset();
		HomeRations.Reset();
		int32 AccCount = 0;
		Ar << AccCount;
		for (int32 i = 0; i < AccCount; ++i)
		{
			int32 AccKey = 0;
			FRHHomeClock Loaded;
			Ar << AccKey << Loaded.Acc << Loaded.OnsetResidents;
			HomeClocks.Add(AccKey, Loaded);
		}
	}
	// Arrivals at scale (save v35, C2b).
	{
		uint8 BigDone = 0;
		Ar << BigArrivalStreakSols << BigDone << MoveHomeAccSols << ImmigrantsTotal;
		bBigArrivalDone = BigDone != 0;
	}
	// Room designations (save v11). Map repopulates wholesale.
	FloorRoomCells.Empty();
	Ar << FloorRoomCells;
	// A room array longer than its floor, or for a Level with no floor, is repaired (D1): the house way, a warning.
	for (TMap<int32, TArray<FName>>::TIterator It(FloorRoomCells); It; ++It)
	{
		const int32 Have = GetFloorCarvedCells(It.Key());
		if (Have == 0)
		{
			UE_LOG(LogRedHopeSim, Warning, TEXT("Load: room designations for floor %d, which has no cells - dropped"), It.Key());
			It.RemoveCurrent();
		}
		else if (It.Value().Num() > Have)
		{
			UE_LOG(LogRedHopeSim, Warning, TEXT("Load: floor %d lists %d room cells but carved %d - truncated"), It.Key(), It.Value().Num(), Have);
			It.Value().SetNum(Have);
		}
	}
	// The garden (save v12).
	{
		TArray<FIntVector> Planted;
		Ar << Planted << bFirstCropAnnounced;
		PlantedCells.Empty();
		PlantedCells.Append(Planted);
		bGardenThirstAnnounced = false; // runtime edge re-derives
	}
	// Hope-drives (save v13).
	{
		uint8 BandByte = 0;
		Ar << HopeSmoothed << BandByte;
		HopeBand = (ERHHopeBand)BandByte;
	}
	// Water potability (save v14).
	Ar << WaterPotability;
	FreshWaterThisStepKg = 0.0;      // per-step derived, re-accrues from production
	bWaterQualityAnnounced = false;  // runtime edge re-derives from the loaded value
	// Generational growth (save v15).
	Ar << HopeGrowthStreakSols << bFirstBornAnnounced << BirthsOnMars;
	// Discoveries (save v16). Array repopulates wholesale.
	DiscoveryLog.Reset();
	Ar << DiscoveryLog << DiscoverySeatHours;
	// Rivals & trade (save v17).
	{
		uint8 Returning = 0;
		RivalRelations.Empty();
		Ar << ConvoyRival << Returning << ConvoyLegSols << RivalRelations;
		bConvoyReturning = (Returning != 0);
	}
	// Earth's Shadow (save v18).
	{
		uint8 Demand = 0;
		Ar << EarthTension << IdentityAxis << Demand;
		bEarthDemandPending = (Demand != 0);
	}
	// The Solidarity Dilemma (save v19).
	{
		TArray<FName> Closed;
		Ar << Closed << SolidarityHope;
		ClosedRoutes.Empty();
		ClosedRoutes.Append(Closed);
	}
	// The covert layer (save v20). Map repopulates wholesale.
	HiddenTensions.Empty();
	Ar << HiddenTensions << HumanNatureAxis << CovertAttempts;
	// The espionage economy (save v21).
	{
		TArray<FName> Discovered;
		SabotageSols.Empty();
		Ar << SabotageSols << Discovered;
		DiscoveredRivals.Empty();
		DiscoveredRivals.Append(Discovered);
	}
	// Earth's pre-emptive pressure (save v22).
	{
		TArray<FName> Defected;
		EmbargoGrace.Empty();
		Ar << EmbargoGrace << Defected << Influence;
		DefectedRivals.Empty();
		DefectedRivals.Append(Defected);
	}
	// Dynamic crises + endings (save v23).
	{
		uint8 Crisis = 0, Ever = 0, Declared = 0;
		Ar << Crisis << CrisisRemainingSols << CrisisCheckSols << CrisisCount << Ever << Declared;
		ActiveCrisis = (ERHCrisis)Crisis;
		bEverHadCrew = (Ever != 0);
		bEndingDeclared = (Declared != 0);
	}
	// The alive pass (save v24).
	{
		uint8 Harvest = 0;
		Ar << MomentSols << MomentCounter << Harvest << GardenFoodCumKg;
		bFirstHarvestAnnounced = (Harvest != 0);
	}
	// The goal ladder + ending resolution + research funding (save v25).
	{
		uint8 Epilogue = 0, Quiet = 0;
		Ar << ActiveQuota << QuotaOpenedSol << PendingResearchFundingKg
		   << DeclaredEnding << PathSustainSols << Epilogue << Quiet;
		bEpilogueDeclared = (Epilogue != 0);
		bQuietAnnounced = (Quiet != 0);
		if (ActiveQuota.IsNone())
		{
			ActiveQuota = FirstQuotaName(); // legacy safety: never load ladderless
		}
		// Room unlocks are DT-row flips (in-memory), so they re-apply from the
		// loaded discovery log - deterministic, no extra save state.
		for (const FName& Found : DiscoveryLog)
		{
			const FRHDiscoveryRow* Row = Defs->GetDiscovery(Found);
			if (Row && !Row->UnlockRoom.IsNone())
			{
				Defs->ActivateRoomRow(Row->UnlockRoom);
			}
		}
	}
	// Crops + climate + ducts (save v26).
	{
		PlantedCrops.Empty();
		int32 CropCount = 0;
		Ar << CropCount;
		for (int32 i = 0; i < CropCount; ++i)
		{
			FIntVector Key; FRHPlantedCropState S;
			Ar << Key << S.Crop << S.PlantedSol << S.SoilKg;
			PlantedCrops.Add(Key, S);
		}
		FloorClimate.Empty();
		int32 ClimateCount = 0;
		Ar << ClimateCount;
		for (int32 i = 0; i < ClimateCount; ++i)
		{
			int32 Key = 0; FName Band;
			Ar << Key << Band;
			FloorClimate.Add(Key, Band);
		}
		TArray<int32> Ducts;
		Ar << Ducts << ColonyCO2Kg << PlantRotation;
		DuctedFloors.Empty();
		DuctedFloors.Append(Ducts);
		bSoilSpentAnnounced = false; // runtime edge re-derives
	}
	// Staffing (save v27).
	{
		Ar << SeatPriority << StaffedSeats << SeatFillSols;
	}
	// Surface habitats (save v29). Topology was rebuilt after the building loop.
	{
		HabO2Kg.Empty();
		RatedHabs.Empty();
		HabBayRooms.Empty();
		HabNetsNotedSmall.Reset();
		int32 O2Count = 0;
		Ar << O2Count;
		for (int32 i = 0; i < O2Count; ++i)
		{
			int32 Key = 0;
			double V = 0.0;
			Ar << Key << V;
			HabO2Kg.Add(Key, V);
		}
		TArray<int32> Rated;
		Ar << Rated;
		RatedHabs.Append(Rated);
		int32 RoomCount = 0;
		Ar << RoomCount;
		for (int32 i = 0; i < RoomCount; ++i)
		{
			int32 Key = 0;
			TArray<FName> Rooms;
			Ar << Key << Rooms;
			HabBayRooms.Add(Key, MoveTemp(Rooms));
		}
		for (const FRHColonist& Col : Colonists)
		{
			const FRHBuildingInstance* HB = Col.HomeHab ? FindBuildingById(Col.HomeHab) : nullptr;
			const FRHBuildingRow* HD = (HB && Defs) ? Defs->GetBuilding(HB->DefName) : nullptr;
			if (Col.HomeHab != 0 && !(HB && HB->Level == 0 && HD && HD->Pressurized))
			{
				UE_LOG(LogRedHopeSim, Error, TEXT("LoadColony v29: colonist %d HomeHab #%d is not a surface habitat"), Col.Id, Col.HomeHab);
			}
		}
	}
	// Furniture (save v30).
	{
		Furniture.Reset();
		Ar << NextFurnitureId;
		int32 FurnCount = 0;
		Ar << FurnCount;
		for (int32 i = 0; i < FurnCount; ++i)
		{
			FRHFurnitureInstance F;
			Ar << F.Id << F.Row << F.Key << F.Slot << F.Rot;
			if (!(Defs && Defs->GetFurniture(F.Row)) || !IsFurnishKeyValid(F.Key))
			{
				// Kept (it may come back with its row) but it furnishes nothing.
				UE_LOG(LogRedHopeSim, Error, TEXT("LoadColony v30: furnishing #%d (%s) has no row or no cell"), F.Id, *F.Row.ToString());
			}
			Furniture.Add(F);
		}
		RebuildFurnitureCache();
	}
	// Wall fittings (save v31).
	{
		WallFeatures.Reset();
		Ar << NextWallFeatureId;
		int32 WallCount = 0;
		Ar << WallCount;
		for (int32 i = 0; i < WallCount; ++i)
		{
			FRHWallFeature W;
			Ar << W.Id << W.Row << W.Edge;
			if (!(Defs && Defs->GetDoor(W.Row)) || ClassifyWallEdge(W.Edge) == ERHWallEdge::None)
			{
				UE_LOG(LogRedHopeSim, Error, TEXT("LoadColony v31: wall fitting #%d (%s) has no row or no habitat wall"), W.Id, *W.Row.ToString());
			}
			WallFeatures.Add(W);
		}
		RebuildWallFeatureIndex();
	}
	// Quota repeat round (save v32, D1a).
	Ar << QuotaRepeatRound;
	if (QuotaRepeatRound < 0)
	{
		UE_LOG(LogRedHopeSim, Error, TEXT("LoadColony v32: quota repeat round %d is negative; clamped to 0"), QuotaRepeatRound);
		QuotaRepeatRound = 0;
	}
	// The road graph (save v36, C4). Every robot's trip was cleared with the robots.
	{
		RoadGraph.Reset();
		RobotPaths.Reset();
		// C6a: everything the rovers and the commute derive is rebuilt, never loaded. The version counters only go
		// up across a load (JobsVersion included), so a key can never hit a stale entry from before it.
		ActiveRovers = 0;
		bFreightWasActive = false;
		FreightBuiltVersion = MAX_uint32;
		bRoverWaitAnnounced = false;
		bCommuteAlerted = false;
		JobAssignLast.Reset();
		ColonistStations.Reset(); // derived by RefreshJobs: nothing from before the load may answer a read until it re-runs
		++JobsVersion;
		CommuteFlows = FRHCommuteFlows();
		CommuteMemo = FRHCommuteMemo();
		int32 NodeCount = 0;
		Ar << NodeCount;
		for (int32 i = 0; i < NodeCount; ++i)
		{
			FRHRoadNode N;
			Ar << N.Id << N.GridPos;
			RoadGraph.Nodes.Add(N);
		}
		int32 EdgeCount = 0;
		Ar << EdgeCount;
		for (int32 i = 0; i < EdgeCount; ++i)
		{
			FRHRoadEdge E;
			uint8 EBuilt = 0;
			Ar << E.Id << E.A << E.B << E.LengthCm << EBuilt << E.BuildProgress << E.RegionId;
			E.bBuilt = EBuilt != 0;
			RoadGraph.Edges.Add(E);
		}
		Ar << RoadGraph.NextNodeId << RoadGraph.NextEdgeId;
		RoadGraph.RebuildDerived();
		for (const FRHRoadEdge& E : RoadGraph.Edges)
		{
			if (!RoadGraph.FindNode(E.A) || !RoadGraph.FindNode(E.B))
			{
				UE_LOG(LogRedHopeSim, Error, TEXT("LoadColony v36: road edge #%d joins a node that does not exist"), E.Id);
			}
		}
	}
	// Districts (save v37, C5a). The ground maps and the membership caches re-derive on first read.
	{
		Districts.Reset();
		int32 DistrictCount = 0;
		Ar << DistrictCount;
		TSet<FIntPoint> Held; // a lattice cell belongs to one district at most
		TSet<int32> FloorsHeld;
		for (int32 i = 0; i < DistrictCount; ++i)
		{
			FRHDistrict D;
			uint8 DGrowth = 1;
			Ar << D.Id << D.Kind << D.Name << D.RegionId << D.Level << D.Lattice << DGrowth;
			D.bGrowth = DGrowth != 0; // v38
			D.Lattice.Sort([](const FIntPoint& A, const FIntPoint& B) { return A.X != B.X ? A.X < B.X : A.Y < B.Y; });
			bool bBad = RHDistricts::ResolveKind(D.Kind).IsNone() || D.Id <= 0 || (Districts.Num() > 0 && D.Id <= Districts.Last().Id) || D.Level > 0;
			if (D.IsFloor())
			{
				bBad |= !D.Lattice.IsEmpty() || FloorsHeld.Contains(D.Level);
				FloorsHeld.Add(D.Level);
			}
			else
			{
				for (const FIntPoint& Cell : D.Lattice)
				{
					bBad |= Held.Contains(Cell);
					Held.Add(Cell);
				}
			}
			if (bBad)
			{
				UE_LOG(LogRedHopeSim, Error, TEXT("LoadColony v37: district #%d '%s' (%s, level %d) is malformed or overlaps another"), D.Id, *D.Name, *D.Kind.ToString(), D.Level);
			}
			Districts.Add(MoveTemp(D));
		}
		Ar << NextDistrictId;
		if (Districts.Num() > 0 && NextDistrictId <= Districts.Last().Id)
		{
			UE_LOG(LogRedHopeSim, Error, TEXT("LoadColony v37: next district Id %d is not past the highest (#%d); raised"), NextDistrictId, Districts.Last().Id);
			NextDistrictId = Districts.Last().Id + 1;
		}
		++DistrictsVersion; // the ground maps and the membership caches re-derive on first read
	}
	// City growth (save v38, C5b).
	{
		GrowthProjects.Reset();
		GrowthNotes.Reset();
		GrowthPause.Reset();
		GrowthSolStarted.Reset();
		GrowthSolFinished.Reset();
		GrowthSolRefused.Reset();
		Ar << GrowthCursor << GrowthCheckedSol;
		int32 ProjectCount = 0;
		Ar << ProjectCount;
		for (int32 i = 0; i < ProjectCount; ++i)
		{
			FRHGrowthProject P;
			uint8 PKind = 0, PCarve = 0;
			Ar << P.DistrictId << PKind << P.Level << P.Cell << P.Size << P.HabId << P.Cells << P.FirstCell << P.Index << PCarve << P.Room << P.TotalSols << P.RemainingSols;
			P.Kind = (ERHGrowthKind)PKind;
			P.bCarve = PCarve != 0;
			int32 BillCount = 0;
			Ar << BillCount;
			for (int32 b = 0; b < BillCount; ++b)
			{
				FName Key;
				double Kg = 0.0;
				Ar << Key << Kg;
				P.Bill.Add(Key, Kg);
			}
			const FRHDistrict* Owner = FindDistrict(P.DistrictId);
			const bool bKindOk = Owner && PKind <= (uint8)ERHGrowthKind::CivicRoom && ((P.Kind == ERHGrowthKind::CivicRoom) == (Owner->Kind == FName(TEXT("Civic"))));
			const bool bSorted = GrowthProjects.Num() == 0 || GrowthProjects.Last().DistrictId < P.DistrictId;
			if (!bKindOk || !bSorted)
			{
				UE_LOG(LogRedHopeSim, Error, TEXT("LoadColony v38: the growth project of district #%d is malformed (no such district, the wrong kind, or out of order); dropped"), P.DistrictId);
				continue;
			}
			GrowthProjects.Add(MoveTemp(P));
		}
		++GrowthVersion;
	}
	// The City / Earth share and the production ledger (save v39, C5d).
	{
		Ar << CityShare;
		Ar << ProdRingFilled << ProdRingHead << ProductionCheckedSol;
		for (int32 Leg = 0; Leg < ProdLegs; ++Leg)
		{
			Ar << ProdAccKg[Leg];
			for (int32 i = 0; i < ProdMaxWindow; ++i)
			{
				Ar << ProdRing[Leg][i];
			}
		}
		if (!(CityShare >= 0.0 && CityShare <= 1.0) || ProdRingFilled < 0 || ProdRingFilled > ProdMaxWindow || ProdRingHead < 0 || ProdRingHead >= ProdMaxWindow)
		{
			UE_LOG(LogRedHopeSim, Error, TEXT("LoadColony v39: the City / Earth share (%.3f) or the production ledger (filled %d, head %d) is out of range; clamped"), CityShare, ProdRingFilled, ProdRingHead);
			CityShare = FMath::Clamp(FMath::IsNaN(CityShare) ? GrowthCityShareDefault : CityShare, 0.0, 1.0);
			ProdRingFilled = FMath::Clamp(ProdRingFilled, 0, ProdMaxWindow);
			ProdRingHead = FMath::Clamp(ProdRingHead, 0, ProdMaxWindow - 1);
		}
		++GrowthVersion;
	}

	// Loads land at 1x regardless of the saved speed: give the player a calm
	// frame before they choose a tier again.
	Clock->Debug_SetSimSeconds(SimSeconds);
	Clock->SetSpeed(1.f);
	LastAutosaveSol = Clock->GetSol();
	// C1: the roster, the zoning and the furniture all arrived wholesale, so every
	// derived lookup and keyed cache is dropped and rebuilt from what loaded.
	bColonistIndexDirty = true;
	++ColonistSetVersion;
	++ZoningVersion;
	bBedKeyValid = false;
	RefreshJobs(); // derived, never saved - rebuilt from the loaded roster + rooms

	// Transient edge state re-derives from the loaded clock: a mid-storm load
	// must not re-announce "onset", and the ship countdown picks up mid-run.
	const FRHEventRow* ActiveNow = GetActiveEvent();
	bEventWasActive = ActiveNow != nullptr;
	LastEventType = ActiveNow ? ActiveNow->Type : NAME_None;
	if (QuotaPhase == ERHQuotaPhase::ShipInbound)
	{
		const double SolsOut = (ShipArrivalSimSeconds - SimSeconds) / URHSimClockSubsystem::SolLengthSimSeconds;
		ShipAlertStage = SolsOut <= 1.0 ? 2 : (SolsOut <= 2.0 ? 1 : 0);
	}
	else
	{
		ShipAlertStage = 0;
	}
	// D2a: already low at load = already announced.
	IceLowAnnounced.Reset();
	for (const FRHDepositState& D : Deposits)
	{
		if (IsIceDepositLow(D))
		{
			IceLowAnnounced.Add(D.Id);
		}
	}
	// D4a: a save loaded inside a food episode must not re-announce it (already below = latched).
	bFoodLowAnnounced = Colonists.Num() > 0 && GetFoodRunwaySols() < FoodRunwayFloorSols;

	OnColonyReloaded.Broadcast();
	if (Respawned.Num() > 0)
	{
		OnRobotsSpawned.Broadcast(Respawned);
	}
	UE_LOG(LogRedHopeSim, Display, TEXT("Colony loaded: slot '%s' (sol %d, %d buildings, %d robots, %d tasks)"),
		*Slot, Clock->GetSol(), Buildings.Num(), Respawned.Num(), Tasks.Num());
	return true;
}

