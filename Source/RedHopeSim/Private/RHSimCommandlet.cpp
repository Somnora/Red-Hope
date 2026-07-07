#include "RHSimCommandlet.h"
#include "RedHopeSim.h"
#include "RHSimClockSubsystem.h"
#include "RHSimWorldSubsystem.h"
#include "RHDefinitionsSubsystem.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

// Headless balance runner: the standing-order proof that the sim needs no
// presentation. Boots a bare Game world (subsystems and all), era-integrates
// N sols of colony ledger at 1 sim-minute steps, prints the ledger, exits.
//   UnrealEditor-Cmd <proj> -run=RHSim -sols=20 [-dig=Regolith_A]
int32 URHSimCommandlet::Main(const FString& Params)
{
	int32 Sols = 10;
	FParse::Value(*Params, TEXT("sols="), Sols);
	FString DigTarget = TEXT("Regolith_A");
	FParse::Value(*Params, TEXT("dig="), DigTarget);

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("RHSimHeadless"));
	if (!World || !GEngine)
	{
		UE_LOG(LogRedHopeSim, Error, TEXT("RHSim: could not create headless world"));
		return 1;
	}
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();

	URHSimClockSubsystem* Clock = World->GetSubsystem<URHSimClockSubsystem>();
	URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>();
	if (!Clock || !Sim)
	{
		UE_LOG(LogRedHopeSim, Error, TEXT("RHSim: sim subsystems unavailable in headless world"));
		return 1;
	}

	Sim->Debug_DeployFleet();
	FRHCommand Dig;
	Dig.Verb = FName("Dig");
	Dig.Target = FName(*DigTarget);
	Sim->EnqueueCommand(Dig);

	const int32 StepsPerSol = (int32)(URHSimClockSubsystem::SolLengthSimSeconds / URHSimClockSubsystem::EraStepSimSeconds);

	// M1-d Gate A2 self-test: the Borer works bore/carve DESIGNATIONS through
	// the batch integrator - uplink orders, spoil at the building (hauled by
	// era logistics), hydrogen fuel, mid-flight save v6 round-trip. `-borer`.
	if (FParse::Param(*Params, TEXT("borer")))
	{
		UE_LOG(LogRedHopeSim, Display, TEXT("=== BORER DESIGNATION TEST (M1-d Gate A2) ==="));
		URHDefinitionsSubsystem* DefsSub = World->GetSubsystem<URHDefinitionsSubsystem>();
		// The live DT asset predates the CanBore/H2BurnKgPerHour columns (an
		// asset can only take them AFTER this binary exists); patch the loaded
		// row in memory to the values RH_Buildings.csv stages, so this proves
		// the CODE path. The in-editor smoke after the DT sync proves the DATA.
		FRHBuildingRow* BorerRow = const_cast<FRHBuildingRow*>(DefsSub ? DefsSub->GetBuilding(FName("Borer")) : nullptr);
		if (!BorerRow)
		{
			UE_LOG(LogRedHopeSim, Error, TEXT("BORER: no Borer row in DT_Buildings"));
			return 1;
		}
		BorerRow->CanBore = true;
		BorerRow->H2BurnKgPerHour = 1.5f;

		Sim->Debug_PlaceInstant(FName("Borer"), FVector(2000.f, 2000.f, 0.f));
		Sim->Debug_PlaceInstant(FName("SolarArray"), FVector(3500.f, 1000.f, 0.f));
		Sim->Debug_PlaceInstant(FName("SolarArray"), FVector(3500.f, 2000.f, 0.f));
		Sim->Debug_PlaceInstant(FName("SolarArray"), FVector(3500.f, 3000.f, 0.f));
		Sim->Debug_PlaceInstant(FName("BatteryBank"), FVector(1000.f, 3500.f, 0.f));

		const auto RunSols = [&](double Sols)
		{
			const int32 Steps = (int32)(Sols * StepsPerSol);
			for (int32 S = 0; S < Steps; ++S)
			{
				Clock->Debug_AdvanceSimSeconds(URHSimClockSubsystem::EraStepSimSeconds);
				Sim->EraStep(URHSimClockSubsystem::EraStepSimSeconds);
			}
		};
		const FName NRegolith(TEXT("Regolith")), NHydrogen(TEXT("Hydrogen"));

		FRHCommand Bore;
		Bore.Verb = FName("Bore");
		Bore.Value = 2;
		Sim->EnqueueCommand(Bore);
		RunSols(3.0);
		UE_LOG(LogRedHopeSim, Display, TEXT("BORER bore phase: depth=%d (expect 2) regolith total=%.0f (expect >= 2400, spoil hauled)"),
			Sim->GetShaftDepth(), Sim->GetTotalSolid(NRegolith));

		FRHCommand Exc;
		Exc.Verb = FName("Excavate");
		Exc.Level = -1;
		Exc.Value = 2;
		Sim->EnqueueCommand(Exc);
		RunSols(1.0);
		UE_LOG(LogRedHopeSim, Display, TEXT("BORER carve phase: carved(-1)=%d (expect 2)"), Sim->GetFloorCarvedCells(-1));

		// Mid-flight round-trip: 2 more cells ordered, a batch caught in the
		// air, save v6, reload, finish - designations and pending work survive.
		FRHCommand Exc2 = Exc;
		Sim->EnqueueCommand(Exc2);
		RunSols(0.15);
		FString Err;
		Sim->SaveColony(TEXT("borertest"), Err);
		Sim->LoadColony(TEXT("borertest"), Err);
		UE_LOG(LogRedHopeSim, Display, TEXT("BORER save/load v6: depth=%d carved(-1)=%d queued(-1)=%d (state mid-designation)"),
			Sim->GetShaftDepth(), Sim->GetFloorCarvedCells(-1), Sim->GetCarveQueued(-1));
		RunSols(1.5);
		UE_LOG(LogRedHopeSim, Display, TEXT("BORER resumed: carved(-1)=%d (expect 4)"), Sim->GetFloorCarvedCells(-1));

		// Hydrogen phase: stock fuel, order more cells; batches start "on H2",
		// deduct whole-batch fuel up-front, and the grid sees idle draw only.
		Sim->AddStock(NHydrogen, 20.0);
		FRHCommand Exc3 = Exc;
		Sim->EnqueueCommand(Exc3);
		RunSols(1.0);
		UE_LOG(LogRedHopeSim, Display, TEXT("BORER H2 phase: carved(-1)=%d (expect 6) hydrogen=%.1f (expect 11.0: 20 - 2 batches x 4.5)"),
			Sim->GetFloorCarvedCells(-1), Sim->GetStock(NHydrogen));

		UE_LOG(LogRedHopeSim, Display, TEXT("=== BORER TEST END ==="));
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return 0;
	}

	// M2 Gate A1 self-test: the crew arrives - housing gate on the certified
	// vault, O2/Food draws, unsupported edge -> evacuation, save v10. `-crew`.
	if (FParse::Param(*Params, TEXT("crew")))
	{
		UE_LOG(LogRedHopeSim, Display, TEXT("=== CREW ARRIVAL TEST (M2 Gate A1) ==="));
		const int32 StepsPerSolH = (int32)(URHSimClockSubsystem::SolLengthSimSeconds / URHSimClockSubsystem::EraStepSimSeconds);
		const auto RunSols = [&](double Sols)
		{
			for (int32 S = 0; S < (int32)(Sols * StepsPerSolH); ++S)
			{
				Clock->Debug_AdvanceSimSeconds(URHSimClockSubsystem::EraStepSimSeconds);
				Sim->EraStep(URHSimClockSubsystem::EraStepSimSeconds);
			}
		};

		// 1) No certified housing: the pod must stay aboard.
		Sim->Debug_DeliverCargo(FName("CrewPod"));
		UE_LOG(LogRedHopeSim, Display, TEXT("CREW no housing: pop=%d (expect 0 - pod returned)"), Sim->GetPopulation());

		// 2) Certify a 4-cell vault (the M1-d chain, on live data rows).
		const FVector Head(1000.f, 1000.f, 0.f);
		FString R;
		Sim->ExtendShaft(1, Head);
		Sim->ExcavateFloor(-1, 4, R);
		Sim->Debug_PlaceInstant(FName("SolarArray"), FVector(3500.f, 1000.f, 0.f));
		Sim->Debug_PlaceInstant(FName("BatteryBank"), FVector(1000.f, 3500.f, 0.f));
		Sim->Debug_PlaceInstant(FName("AirFilter"), FVector(1000.f, 1500.f, 0.f), -1);
		Sim->AddStock(FName("Oxygen"), 800.0);
		RunSols(2.0);
		UE_LOG(LogRedHopeSim, Display, TEXT("CREW vault: rated=%d beds=%d (expect 1, 4)"),
			(int32)Sim->IsFloorRated(-1), Sim->GetHousingCapacity());

		// 3) The pod lands: 4 colonists + provisions.
		Sim->Debug_DeliverCargo(FName("CrewPod"));
		const double Food0 = Sim->GetStock(FName("Food"));
		UE_LOG(LogRedHopeSim, Display, TEXT("CREW landed: pop=%d food=%.0f (expect 4, 200)"), Sim->GetPopulation(), Food0);

		// 4) One sol of consumption: Food falls by pop x rate; the floor keeps
		// its rating (breathing competes with leak but the circulator refills).
		RunSols(1.0);
		UE_LOG(LogRedHopeSim, Display, TEXT("CREW 1 sol: food=%.1f (expect ~%.1f) rated=%d supported=%d/%d"),
			Sim->GetStock(FName("Food")), Food0 - 4 * 0.62, (int32)Sim->IsFloorRated(-1),
			[&]{ int32 N=0; for (const FRHColonist& C : Sim->GetColonists()) { N += C.bSupported ? 1 : 0; } return N; }(),
			Sim->GetPopulation());

		// 5) Save v10 round-trip holds the roster.
		FString Err;
		Sim->SaveColony(TEXT("crewtest"), Err);
		Sim->LoadColony(TEXT("crewtest"), Err);
		UE_LOG(LogRedHopeSim, Display, TEXT("CREW save/load v10: pop=%d first=%s (expect 4, Adeyemi)"),
			Sim->GetPopulation(),
			Sim->GetColonists().Num() > 0 ? *Sim->GetColonists()[0].Name : TEXT("-"));

		// 6) Starve the pool: unsupported edge, then evacuation after 2 sols.
		Sim->AddStock(FName("Food"), -Sim->GetStock(FName("Food")));
		RunSols(0.5);
		UE_LOG(LogRedHopeSim, Display, TEXT("CREW starved 0.5 sol: pop=%d unsupported=%d (expect 4, 4)"),
			Sim->GetPopulation(),
			[&]{ int32 N=0; for (const FRHColonist& C : Sim->GetColonists()) { N += C.bSupported ? 0 : 1; } return N; }());
		RunSols(2.0);
		UE_LOG(LogRedHopeSim, Display, TEXT("CREW starved 2.5 sols: pop=%d (expect 0 - evacuated)"), Sim->GetPopulation());

		UE_LOG(LogRedHopeSim, Display, TEXT("=== CREW TEST END ==="));
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return 0;
	}

	// M2 Gate B self-test: room designations on carved cells, the adjacency
	// calculus (emit/refuse, hallway+filtration cure), jobs, and the Hope
	// index arithmetic - exact doubles. Save v11 round-trip. `-rooms`.
	if (FParse::Param(*Params, TEXT("rooms")))
	{
		UE_LOG(LogRedHopeSim, Display, TEXT("=== ROOMS & HOPE TEST (M2 Gate B) ==="));
		const int32 StepsPerSolH = (int32)(URHSimClockSubsystem::SolLengthSimSeconds / URHSimClockSubsystem::EraStepSimSeconds);
		const auto RunSols = [&](double Sols)
		{
			for (int32 S = 0; S < (int32)(Sols * StepsPerSolH); ++S)
			{
				Clock->Debug_AdvanceSimSeconds(URHSimClockSubsystem::EraStepSimSeconds);
				Sim->EraStep(URHSimClockSubsystem::EraStepSimSeconds);
			}
		};
		// The live DT_Rooms predates this gate's activation flips; patch the six
		// Gate-B rows in memory (the established test-knob pattern - the CSV
		// carries the truth, the editor session syncs the asset).
		URHDefinitionsSubsystem* DefsSub = World->GetSubsystem<URHDefinitionsSubsystem>();
		for (const TCHAR* RowName : { TEXT("LivingQuarters"), TEXT("Lab"), TEXT("Workstation"), TEXT("Dining"), TEXT("Cooking"), TEXT("Hallway") })
		{
			if (FRHRoomRow* Row = const_cast<FRHRoomRow*>(DefsSub ? DefsSub->GetRoom(FName(RowName)) : nullptr))
			{
				Row->SliceActive = true;
			}
			else
			{
				UE_LOG(LogRedHopeSim, Error, TEXT("ROOMS: no room row '%s' in DT_Rooms"), RowName);
				return 1;
			}
		}

		// Certify a 6-cell vault and house 4 colonists (stocked so support
		// noise never touches the Hope assertions).
		FString R;
		Sim->ExtendShaft(1, FVector(1000.f, 1000.f, 0.f));
		Sim->ExcavateFloor(-1, 6, R);
		Sim->Debug_PlaceInstant(FName("SolarArray"), FVector(3500.f, 1000.f, 0.f));
		Sim->Debug_PlaceInstant(FName("BatteryBank"), FVector(1000.f, 3500.f, 0.f));
		Sim->Debug_PlaceInstant(FName("AirFilter"), FVector(1000.f, 1500.f, 0.f), -1);
		Sim->AddStock(FName("Oxygen"), 900.0);
		Sim->AddStock(FName("Food"), 200.0);
		RunSols(2.5);
		Sim->Debug_AddColonists(4);
		RunSols(0.1); // one pass so jobs/support settle

		// 1) Baseline: no designations. 50 base + 5 vault milestone.
		auto H = Sim->GetColonyHope();
		UE_LOG(LogRedHopeSim, Display, TEXT("ROOMS baseline: hope=%.2f rated=%d pop=%d (expect 55.00, 1, 4)"),
			H.Total, (int32)Sim->IsFloorRated(-1), Sim->GetPopulation());

		// 2) Refusals: dormant function, uncarved cell.
		const bool bGarden = Sim->DesignateRoom(-1, 0, FName("Garden"), R);
		const bool bFar = Sim->DesignateRoom(-1, 9, FName("Dining"), R);
		UE_LOG(LogRedHopeSim, Display, TEXT("ROOMS refusals: garden=%d cell9=%d (expect 0, 0)"), (int32)bGarden, (int32)bFar);

		// 3) A bad kitchen: Cooking@0 (1,0) beside LQ@1 (1,1), Dining@2 (0,1)
		// diagonal across the shaft corner. Both offended - dist 1 has no
		// between, dist 2's common neighbor is the LQ, not a hallway.
		Sim->DesignateRoom(-1, 1, FName("LivingQuarters"), R);
		Sim->DesignateRoom(-1, 2, FName("Dining"), R);
		Sim->DesignateRoom(-1, 0, FName("Cooking"), R);
		H = Sim->GetColonyHope();
		UE_LOG(LogRedHopeSim, Display, TEXT("ROOMS bad kitchen: hope=%.2f pairs=%d housing=%.2f rooms=%.2f (expect 47.25, 2, 3.75, 4.50)"),
			H.Total, H.OffendedPairs, H.Housing, H.Rooms);

		// 4) The cure: move Cooking to (-1,0) with the Hallway at (-1,1)
		// between it and Dining, circulator running - partition + filtration
		// cancels the pair (habitat vision §4). Lab@5 seats one colonist.
		Sim->DesignateRoom(-1, 0, NAME_None, R);
		Sim->DesignateRoom(-1, 4, FName("Cooking"), R);
		Sim->DesignateRoom(-1, 3, FName("Hallway"), R);
		Sim->DesignateRoom(-1, 5, FName("Lab"), R);
		RunSols(0.1); // jobs refresh in the population step
		H = Sim->GetColonyHope();
		UE_LOG(LogRedHopeSim, Display, TEXT("ROOMS cured layout: hope=%.2f pairs=%d jobs=%.2f seats=%d (expect 67.75, 0, 3.00, 1)"),
			H.Total, H.OffendedPairs, H.Jobs, H.FilledSeats);

		// 5) Greed test: swap the hallway for more quarters - the partition
		// dies and both offenses return. More beds, less Hope: a real tradeoff.
		Sim->DesignateRoom(-1, 3, FName("LivingQuarters"), R);
		H = Sim->GetColonyHope();
		UE_LOG(LogRedHopeSim, Display, TEXT("ROOMS greed swap: hope=%.2f pairs=%d housing=%.2f (expect 55.50, 2, 7.50)"),
			H.Total, H.OffendedPairs, H.Housing);

		// 6) The Designate verb rides the uplink like any order.
		FRHCommand Cmd;
		Cmd.Verb = FName("Designate");
		Cmd.Target = FName("Workstation");
		Cmd.Level = -1;
		Cmd.Value = 0;
		Sim->EnqueueCommand(Cmd);
		RunSols(0.1);
		UE_LOG(LogRedHopeSim, Display, TEXT("ROOMS uplink designate: cell0=%s (expect Workstation)"),
			*Sim->GetRoomAt(-1, 0).ToString());

		// 7) Save v11 round-trip: designations + jobs + the exact Hope total.
		const double HopeBefore = Sim->GetColonyHope().Total;
		FString Err;
		Sim->SaveColony(TEXT("roomstest"), Err);
		Sim->LoadColony(TEXT("roomstest"), Err);
		H = Sim->GetColonyHope();
		UE_LOG(LogRedHopeSim, Display, TEXT("ROOMS save/load v11: cell4=%s cell3=%s hope=%.2f (expect Cooking, LivingQuarters, %.2f) seats=%d (expect 2)"),
			*Sim->GetRoomAt(-1, 4).ToString(), *Sim->GetRoomAt(-1, 3).ToString(), H.Total, HopeBefore, H.FilledSeats);

		UE_LOG(LogRedHopeSim, Display, TEXT("=== ROOMS TEST END ==="));
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return 0;
	}

	// M2 Gate D self-test (abstract slice): comforts - luxury draw per colonist,
	// Hope lift scaled by supplied fraction, zero penalty when dry. `-luxury`.
	if (FParse::Param(*Params, TEXT("luxury")))
	{
		UE_LOG(LogRedHopeSim, Display, TEXT("=== COMFORTS TEST (M2 Gate D, abstract) ==="));
		const int32 StepsPerSolH = (int32)(URHSimClockSubsystem::SolLengthSimSeconds / URHSimClockSubsystem::EraStepSimSeconds);
		const auto RunSols = [&](double Sols)
		{
			for (int32 S = 0; S < (int32)(Sols * StepsPerSolH); ++S)
			{
				Clock->Debug_AdvanceSimSeconds(URHSimClockSubsystem::EraStepSimSeconds);
				Sim->EraStep(URHSimClockSubsystem::EraStepSimSeconds);
			}
		};
		// Certify a 4-cell vault, house 4, keep them fed and breathing.
		FString R;
		Sim->ExtendShaft(1, FVector(1000.f, 1000.f, 0.f));
		Sim->ExcavateFloor(-1, 4, R);
		Sim->Debug_PlaceInstant(FName("SolarArray"), FVector(3500.f, 1000.f, 0.f));
		Sim->Debug_PlaceInstant(FName("BatteryBank"), FVector(1000.f, 3500.f, 0.f));
		Sim->Debug_PlaceInstant(FName("AirFilter"), FVector(1000.f, 1500.f, 0.f), -1);
		Sim->AddStock(FName("Oxygen"), 800.0);
		Sim->AddStock(FName("Food"), 200.0);
		RunSols(2.5);
		Sim->Debug_AddColonists(4);
		RunSols(0.1);

		// 1) Dry: no luxuries, no penalty - Hope is the plain 55 baseline.
		auto H = Sim->GetColonyHope();
		UE_LOG(LogRedHopeSim, Display, TEXT("LUXURY dry: hope=%.2f comforts=%.2f supplied=%d (expect 55.00, 0.00, 0)"),
			H.Total, H.Comforts, Sim->GetComfortsSuppliedCount());

		// 2) The crate lands: full supply lifts Hope by the whole bonus.
		Sim->Debug_DeliverCargo(FName("LuxuryGoods"));
		const double Lux0 = Sim->GetStock(FName("Luxury"));
		RunSols(1.0);
		H = Sim->GetColonyHope();
		UE_LOG(LogRedHopeSim, Display, TEXT("LUXURY supplied: hope=%.2f comforts=%.2f supplied=%d drawn=%.1f (expect 63.00, 8.00, 4, 0.8)"),
			H.Total, H.Comforts, Sim->GetComfortsSuppliedCount(), Lux0 - Sim->GetStock(FName("Luxury")));

		// 3) Drain it: the lift fades to zero, nothing else changes.
		Sim->AddStock(FName("Luxury"), -Sim->GetStock(FName("Luxury")));
		RunSols(0.5);
		H = Sim->GetColonyHope();
		UE_LOG(LogRedHopeSim, Display, TEXT("LUXURY drained: hope=%.2f comforts=%.2f pop=%d supported=%d (expect 55.00, 0.00, 4, 4)"),
			H.Total, H.Comforts, Sim->GetPopulation(),
			[&]{ int32 N=0; for (const FRHColonist& C : Sim->GetColonists()) { N += C.bSupported ? 1 : 0; } return N; }());

		UE_LOG(LogRedHopeSim, Display, TEXT("=== COMFORTS TEST END ==="));
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return 0;
	}

	// M2 Gate C self-test: the garden - zoned cells auto-plant from pooled
	// Soil/Seeds on a rated floor, yield Food per sol against a Water draw,
	// pause dry, survive save v12, forfeit soil on re-zoning. `-garden`.
	if (FParse::Param(*Params, TEXT("garden")))
	{
		UE_LOG(LogRedHopeSim, Display, TEXT("=== GARDEN TEST (M2 Gate C) ==="));
		const int32 StepsPerSolH = (int32)(URHSimClockSubsystem::SolLengthSimSeconds / URHSimClockSubsystem::EraStepSimSeconds);
		const auto RunSols = [&](double Sols)
		{
			for (int32 S = 0; S < (int32)(Sols * StepsPerSolH); ++S)
			{
				Clock->Debug_AdvanceSimSeconds(URHSimClockSubsystem::EraStepSimSeconds);
				Sim->EraStep(URHSimClockSubsystem::EraStepSimSeconds);
			}
		};
		// In-memory activation until the DT_Rooms sync (established pattern).
		URHDefinitionsSubsystem* DefsSub = World->GetSubsystem<URHDefinitionsSubsystem>();
		for (const TCHAR* RowName : { TEXT("Garden"), TEXT("Workstation") })
		{
			if (FRHRoomRow* Row = const_cast<FRHRoomRow*>(DefsSub ? DefsSub->GetRoom(FName(RowName)) : nullptr))
			{
				Row->SliceActive = true;
			}
			else
			{
				UE_LOG(LogRedHopeSim, Error, TEXT("GARDEN: no room row '%s'"), RowName);
				return 1;
			}
		}

		// Certify the vault; zone two garden cells BEFORE materials exist.
		FString R;
		Sim->ExtendShaft(1, FVector(1000.f, 1000.f, 0.f));
		Sim->ExcavateFloor(-1, 6, R);
		Sim->Debug_PlaceInstant(FName("SolarArray"), FVector(3500.f, 1000.f, 0.f));
		Sim->Debug_PlaceInstant(FName("BatteryBank"), FVector(1000.f, 3500.f, 0.f));
		Sim->Debug_PlaceInstant(FName("AirFilter"), FVector(1000.f, 1500.f, 0.f), -1);
		Sim->AddStock(FName("Oxygen"), 900.0);
		Sim->DesignateRoom(-1, 0, FName("Garden"), R);
		Sim->DesignateRoom(-1, 1, FName("Garden"), R);
		RunSols(2.5);
		UE_LOG(LogRedHopeSim, Display, TEXT("GARDEN zoned unfed: rated=%d planted=%d (expect 1, 0 - no soil yet)"),
			(int32)Sim->IsFloorRated(-1), Sim->GetPlantedCellCount());

		// 1) The gamble pays off: pallet + vault land, both cells auto-plant.
		Sim->Debug_DeliverCargo(FName("SoilPallet"));
		Sim->Debug_DeliverCargo(FName("SeedVault"));
		Sim->AddStock(FName("Water"), 100.0);
		RunSols(0.1);
		UE_LOG(LogRedHopeSim, Display, TEXT("GARDEN planted: %d cells, soil=%.0f seeds=%.0f (expect 2, 500, 100)"),
			Sim->GetPlantedCellCount(), Sim->GetStock(FName("Soil")), Sim->GetStock(FName("Seeds")));

		// 2) Two sols of growth: +1.0 kg/sol/cell, -4.0 kg water/sol/cell.
		const double Food0 = Sim->GetStock(FName("Food")), Water0 = Sim->GetStock(FName("Water"));
		RunSols(2.0);
		UE_LOG(LogRedHopeSim, Display, TEXT("GARDEN 2 sols: producing=%d food +%.1f (expect 4.0) water -%.1f (expect 16.0)"),
			Sim->GetProducingCellCount(), Sim->GetStock(FName("Food")) - Food0, Water0 - Sim->GetStock(FName("Water")));

		// 3) Dry taps: production pauses, crop stays planted.
		Sim->AddStock(FName("Water"), -Sim->GetStock(FName("Water")));
		const double FoodDry = Sim->GetStock(FName("Food"));
		RunSols(0.5);
		UE_LOG(LogRedHopeSim, Display, TEXT("GARDEN dry: producing=%d food delta=%.2f planted=%d (expect 0, 0.00, 2)"),
			Sim->GetProducingCellCount(), Sim->GetStock(FName("Food")) - FoodDry, Sim->GetPlantedCellCount());

		// 4) Save v12 round-trip holds the planted set.
		FString Err;
		Sim->SaveColony(TEXT("gardentest"), Err);
		Sim->LoadColony(TEXT("gardentest"), Err);
		UE_LOG(LogRedHopeSim, Display, TEXT("GARDEN save/load v12: planted=%d cell0=%d cell1=%d (expect 2, 1, 1)"),
			Sim->GetPlantedCellCount(), (int32)Sim->IsGardenPlanted(-1, 0), (int32)Sim->IsGardenPlanted(-1, 1));

		// 5) Re-zoning a planted cell forfeits its soil, loudly.
		Sim->DesignateRoom(-1, 1, FName("Workstation"), R);
		RunSols(0.1);
		UE_LOG(LogRedHopeSim, Display, TEXT("GARDEN re-zoned: planted=%d cell1=%d (expect 1, 0 - soil forfeit)"),
			Sim->GetPlantedCellCount(), (int32)Sim->IsGardenPlanted(-1, 1));

		UE_LOG(LogRedHopeSim, Display, TEXT("=== GARDEN TEST END ==="));
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return 0;
	}

	// M1-d Gate B self-test: the habitability chain - carve, circulate,
	// oxygen fill from the pool, Livable rating edges (gained + lost via
	// leakage), save v7 round-trip. `-habitat`.
	if (FParse::Param(*Params, TEXT("habitat")))
	{
		UE_LOG(LogRedHopeSim, Display, TEXT("=== HABITAT CHAIN TEST (M1-d Gate B) ==="));
		URHDefinitionsSubsystem* DefsSub = World->GetSubsystem<URHDefinitionsSubsystem>();
		// The live DT predates the CirculatesAir column; patch ChargePad's row
		// in memory (the Borer-test pattern) - the code path is what's proven
		// headless, the AirFilter DT row proves the data in-editor.
		FRHBuildingRow* PadRow = const_cast<FRHBuildingRow*>(DefsSub ? DefsSub->GetBuilding(FName("ChargePad")) : nullptr);
		if (!PadRow)
		{
			UE_LOG(LogRedHopeSim, Error, TEXT("HABITAT: no ChargePad row"));
			return 1;
		}
		PadRow->CirculatesAir = true;

		const int32 MinCells = Sim->GetMinLivableCells(); // director ruling: 4

		const FVector Head(1000.f, 1000.f, 0.f);
		FString R;
		Sim->ExtendShaft(1, Head);
		Sim->ExcavateFloor(-1, 2, R);          // 2 cells -> under the habitat minimum
		Sim->Debug_PlaceInstant(FName("SolarArray"), FVector(3500.f, 1000.f, 0.f));
		Sim->Debug_PlaceInstant(FName("BatteryBank"), FVector(1000.f, 3500.f, 0.f));
		Sim->AddStock(FName("Oxygen"), 800.0);

		const int32 StepsPerSolH = (int32)(URHSimClockSubsystem::SolLengthSimSeconds / URHSimClockSubsystem::EraStepSimSeconds);
		const auto RunSols = [&](double Sols)
		{
			for (int32 S = 0; S < (int32)(Sols * StepsPerSolH); ++S)
			{
				Clock->Debug_AdvanceSimSeconds(URHSimClockSubsystem::EraStepSimSeconds);
				Sim->EraStep(URHSimClockSubsystem::EraStepSimSeconds);
			}
		};

		RunSols(1.0);
		UE_LOG(LogRedHopeSim, Display, TEXT("HABITAT no circulator: O2 fill %.0f (expect 0 - the chain's last link is missing)"),
			Sim->GetFloorO2Kg(-1));

		Sim->Debug_PlaceInstant(FName("ChargePad"), FVector(1000.f, 1500.f, 0.f), -1); // the patched circulator, ON the floor
		RunSols(1.0);
		// 2 cells fill fully but MUST NOT rate - under the director's minimum.
		UE_LOG(LogRedHopeSim, Display, TEXT("HABITAT 2/%d cells filled: O2 %.0f/%.0f, rated=%d, sealed-but-small=%d (expect 200/200, rated 0, sealed 1)"),
			MinCells, Sim->GetFloorO2Kg(-1), Sim->GetFloorO2RequiredKg(-1),
			(int32)Sim->IsFloorRated(-1), (int32)Sim->IsFloorSealedButSmall(-1));

		// Carve up to the minimum: NOW it certifies and fires the exit.
		Sim->ExcavateFloor(-1, MinCells - 2, R);
		RunSols(1.0);
		UE_LOG(LogRedHopeSim, Display, TEXT("HABITAT %d/%d cells filled: O2 %.0f/%.0f, rated=%d, vault=%d (expect full, rated 1, vault 1)"),
			MinCells, MinCells, Sim->GetFloorO2Kg(-1), Sim->GetFloorO2RequiredKg(-1),
			(int32)Sim->IsFloorRated(-1), (int32)Sim->HasVaultRating());

		FString Err;
		Sim->SaveColony(TEXT("habitattest"), Err);
		Sim->LoadColony(TEXT("habitattest"), Err);
		UE_LOG(LogRedHopeSim, Display, TEXT("HABITAT save/load v9: fill %.0f rated=%d vault=%d (expect full, 1, 1)"),
			Sim->GetFloorO2Kg(-1), (int32)Sim->IsFloorRated(-1), (int32)Sim->HasVaultRating());
		// Drain the pool: leakage alone must drop the floor below 98% and
		// announce the loss (the genuine-failure path the flap-fix preserved).
		Sim->AddStock(FName("Oxygen"), -Sim->GetStock(FName("Oxygen")));
		RunSols(4.0);
		UE_LOG(LogRedHopeSim, Display, TEXT("HABITAT pool dry 4 sols: fill %.0f, rated=%d (expect < %.0f, rated 0 - loss announced)"),
			Sim->GetFloorO2Kg(-1), (int32)Sim->IsFloorRated(-1), Sim->GetFloorO2RequiredKg(-1) * 0.98);

		UE_LOG(LogRedHopeSim, Display, TEXT("=== HABITAT TEST END ==="));
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return 0;
	}

	// M1-d Gate A self-test: the shaft/excavation model, the subsurface build
	// rule, radiation payoff, and save v5 round-trip - all deterministic, no
	// time integration needed. `-vault` runs it and exits.
	if (FParse::Param(*Params, TEXT("vault")))
	{
		UE_LOG(LogRedHopeSim, Display, TEXT("=== VAULT SELF-TEST (M1-d Gate A) ==="));
		const FVector Head(1000.f, 1000.f, 0.f);
		FString R;

		Sim->ExtendShaft(2, Head);
		UE_LOG(LogRedHopeSim, Display, TEXT("VAULT bored: depth=%d spoil=%.0f (expect depth 2, spoil 2400)"),
			Sim->GetShaftDepth(), Sim->GetSpoilPileKg());

		const bool bExc = Sim->ExcavateFloor(-1, 4, R);
		UE_LOG(LogRedHopeSim, Display, TEXT("VAULT excavate -1 x4 -> %s: carved=%d spoil=%.0f (expect carved 4, spoil 7200)%s"),
			bExc ? TEXT("OK") : TEXT("FAIL"), Sim->GetFloorCarvedCells(-1), Sim->GetSpoilPileKg(),
			bExc ? TEXT("") : *FString::Printf(TEXT(" [%s]"), *R));

		FString R4;
		const bool bExc4 = Sim->ExcavateFloor(-4, 1, R4);
		UE_LOG(LogRedHopeSim, Display, TEXT("VAULT excavate -4 (unreached) -> %s [%s] (expect refused)"),
			bExc4 ? TEXT("OK?!") : TEXT("refused"), *R4);

		UE_LOG(LogRedHopeSim, Display, TEXT("VAULT connected: -1=%d -2=%d -3=%d (expect 1 1 0)"),
			(int32)Sim->IsLevelConnected(-1), (int32)Sim->IsLevelConnected(-2), (int32)Sim->IsLevelConnected(-3));

		FString P1, P3;
		const bool bP1 = Sim->CanPlaceBuilding(FName("ChargePad"), Head, P1, -1);
		const bool bP3 = Sim->CanPlaceBuilding(FName("ChargePad"), Head, P3, -3);
		UE_LOG(LogRedHopeSim, Display, TEXT("VAULT place ChargePad @-1 -> %s [%s]"), bP1 ? TEXT("ALLOWED") : TEXT("refused"), *P1);
		UE_LOG(LogRedHopeSim, Display, TEXT("VAULT place ChargePad @-3 -> %s [%s] (expect refused: not reached)"),
			bP3 ? TEXT("ALLOWED?!") : TEXT("refused"), *P3);

		UE_LOG(LogRedHopeSim, Display, TEXT("VAULT radiation: surface=%.3f -1=%.4f -2=%.4f (overburden shielding)"),
			Sim->GetRadiationAtLevel(0), Sim->GetRadiationAtLevel(-1), Sim->GetRadiationAtLevel(-2));

		// Gate B: the surface shielding tax. Same def, same colony: taxed on
		// the surface (refused - no Shielding, no producer), exempt below
		// (overburden shields for free).
		URHDefinitionsSubsystem* DefsSub = World->GetSubsystem<URHDefinitionsSubsystem>();
		if (FRHBuildingRow* Pad = const_cast<FRHBuildingRow*>(DefsSub ? DefsSub->GetBuilding(FName("ChargePad")) : nullptr))
		{
			Pad->SurfaceShielding_kg = 50.f;
			FString TS, TU;
			const bool bSurf = Sim->CanPlaceBuilding(FName("ChargePad"), Head, TS, 0);
			const bool bSub = Sim->CanPlaceBuilding(FName("ChargePad"), Head, TU, -1);
			UE_LOG(LogRedHopeSim, Display, TEXT("VAULT shielding tax: surface -> %s [%s] (expect refused: Insufficient Shielding); floor -1 -> %s (expect ALLOWED - overburden exempt)"),
				bSurf ? TEXT("ALLOWED?!") : TEXT("refused"), *TS, bSub ? TEXT("ALLOWED") : *FString::Printf(TEXT("refused?! [%s]"), *TU));
			Pad->SurfaceShielding_kg = 0.f;
		}

		FString Err;
		Sim->SaveColony(TEXT("vaulttest"), Err);
		const double SpoilBefore = Sim->GetSpoilPileKg();
		Sim->ExcavateFloor(-1, 99, R); // dirty the state before reload
		Sim->LoadColony(TEXT("vaulttest"), Err);
		UE_LOG(LogRedHopeSim, Display, TEXT("VAULT save/load v6: depth=%d carved(-1)=%d spoil=%.0f (expect depth 2, carved 4, spoil %.0f)"),
			Sim->GetShaftDepth(), Sim->GetFloorCarvedCells(-1), Sim->GetSpoilPileKg(), SpoilBefore);

		UE_LOG(LogRedHopeSim, Display, TEXT("=== VAULT SELF-TEST END ==="));
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return 0;
	}

	UE_LOG(LogRedHopeSim, Display, TEXT("RHSim headless: era-integrating %d sols (%d one-minute steps/sol)"), Sols, StepsPerSol);

	// Peak surface exposure over the run: a live solar flare multiplies the
	// surface index by its severity, so this catches the spike even when no
	// whole-sol stop lands inside the (sub-sol) flare window.
	float PeakSurfaceRad = 0.f;
	for (int32 Step = 0; Step < Sols * StepsPerSol; ++Step)
	{
		Clock->Debug_AdvanceSimSeconds(URHSimClockSubsystem::EraStepSimSeconds);
		Sim->EraStep(URHSimClockSubsystem::EraStepSimSeconds);
		PeakSurfaceRad = FMath::Max(PeakSurfaceRad, Sim->GetRadiationNow(0));
	}

	// The ledger, printed. CI greps these lines.
	UE_LOG(LogRedHopeSim, Display, TEXT("=== RHSim LEDGER after %d sols ==="), Sols);
	const FRHPowerState& Power = Sim->GetPower();
	UE_LOG(LogRedHopeSim, Display, TEXT("power: gen %.0f W load %.0f W battery %.0f/%.0f Wh"),
		Power.GenW, Power.LoadW, Power.BatteryWh, Power.BatteryCapWh);
	for (const auto& Q : Sim->GetQuotaProgress())
	{
		UE_LOG(LogRedHopeSim, Display, TEXT("quota %s: %.0f / %.0f kg"), *Q.Key.ToString(), Q.Value.Key, Q.Value.Value);
	}
	for (const FRHDepositState& D : Sim->GetDeposits())
	{
		if (D.bDesignated)
		{
			UE_LOG(LogRedHopeSim, Display, TEXT("deposit %s: %.0f kg underground"), *D.RowName.ToString(), D.RemainingKg);
		}
	}
	UE_LOG(LogRedHopeSim, Display, TEXT("buildings: %d | sol: %d | quota phase: %d"),
		Sim->GetBuildings().Num(), Clock->GetSol(), (int32)Sim->GetQuotaPhase());
	// Radiation profile (M1-c): surface exposure now (spikes under a live flare)
	// and the overburden-shielded curve down the shaft - the M1-d vault's whole
	// argument, printed so the balance runner can see it.
	UE_LOG(LogRedHopeSim, Display, TEXT("radiation surface now: %.3f (steady-state %.3f) | peak surface over run: %.3f"),
		Sim->GetRadiationNow(0), Sim->GetRadiationAtLevel(0), PeakSurfaceRad);
	for (int32 Lvl = 0; Lvl >= -Sim->GetMaxDepth(); --Lvl)
	{
		UE_LOG(LogRedHopeSim, Display, TEXT("radiation level %d: %.4f"), Lvl, Sim->GetRadiationAtLevel(Lvl));
	}
	UE_LOG(LogRedHopeSim, Display, TEXT("=== RHSim LEDGER END ==="));

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return 0;
}
