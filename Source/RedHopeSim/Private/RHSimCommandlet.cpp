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
		Sim->AddStock(FName("Water"), 200.0); // water joins the support contract (Gate D+)
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
		// DT_Rooms is synced (Gate B): these six functions ship SliceActive=true.
		// Assert it against the REAL asset so this test is a pure-data verifier -
		// if the DT ever regresses vs the CSV, it fails loudly instead of a
		// silent in-memory patch masking the drift.
		URHDefinitionsSubsystem* DefsSub = World->GetSubsystem<URHDefinitionsSubsystem>();
		for (const TCHAR* RowName : { TEXT("LivingQuarters"), TEXT("Lab"), TEXT("Workstation"), TEXT("Dining"), TEXT("Cooking"), TEXT("Hallway") })
		{
			const FRHRoomRow* Row = DefsSub ? DefsSub->GetRoom(FName(RowName)) : nullptr;
			if (!Row || !Row->SliceActive)
			{
				UE_LOG(LogRedHopeSim, Error, TEXT("ROOMS: DT_Rooms row '%s' missing or not SliceActive - DT/CSV out of sync"), RowName);
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
		Sim->AddStock(FName("Water"), 200.0); // water joins the support contract (Gate D+)
		RunSols(2.5);
		Sim->Debug_AddColonists(4);
		RunSols(0.1); // one pass so jobs/support settle

		// 1) Baseline: no designations. 50 base + 5 vault milestone.
		auto H = Sim->GetColonyHope();
		UE_LOG(LogRedHopeSim, Display, TEXT("ROOMS baseline: hope=%.2f rated=%d pop=%d (expect 55.00, 1, 4)"),
			H.Total, (int32)Sim->IsFloorRated(-1), Sim->GetPopulation());

		// 2) Refusals: a still-dormant function (Smoking - Gate D, SliceActive
		// FALSE in the synced DT) and an uncarved cell index. Both refused.
		const bool bDormant = Sim->DesignateRoom(-1, 0, FName("Smoking"), R);
		const bool bFar = Sim->DesignateRoom(-1, 9, FName("Dining"), R);
		UE_LOG(LogRedHopeSim, Display, TEXT("ROOMS refusals: dormant=%d cell9=%d (expect 0, 0)"), (int32)bDormant, (int32)bFar);

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
		Sim->AddStock(FName("Water"), 200.0); // water joins the support contract (Gate D+)
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

	// M2 Gate D+ self-test: Hope DRIVES the colony - the exp-form smoother (the
	// era-parity property), the work-tempo transfer function, band hysteresis,
	// tempo scaling the garden yield, and save v13 round-trip. `-hopedrive`.
	if (FParse::Param(*Params, TEXT("hopedrive")))
	{
		UE_LOG(LogRedHopeSim, Display, TEXT("=== HOPE-DRIVES TEST (M2 Gate D+) ==="));
		const int32 StepsPerSolH = (int32)(URHSimClockSubsystem::SolLengthSimSeconds / URHSimClockSubsystem::EraStepSimSeconds);
		const auto RunSols = [&](double Sols)
		{
			for (int32 S = 0; S < (int32)(Sols * StepsPerSolH); ++S)
			{
				Clock->Debug_AdvanceSimSeconds(URHSimClockSubsystem::EraStepSimSeconds);
				Sim->EraStep(URHSimClockSubsystem::EraStepSimSeconds);
			}
		};
		// Rooms ship active in the synced DT; assert (pure-data, fails on drift).
		URHDefinitionsSubsystem* DefsSub = World->GetSubsystem<URHDefinitionsSubsystem>();
		for (const TCHAR* RowName : { TEXT("LivingQuarters"), TEXT("Lab"), TEXT("Garden") })
		{
			const FRHRoomRow* Row = DefsSub ? DefsSub->GetRoom(FName(RowName)) : nullptr;
			if (!Row || !Row->SliceActive)
			{
				UE_LOG(LogRedHopeSim, Error, TEXT("HOPEDRIVE: DT_Rooms '%s' not active - DT/CSV drift"), RowName);
				return 1;
			}
		}

		// Certify a 6-cell vault, house 4, stock generously so they stay
		// supported (instantaneous Hope stays constant through the convergence).
		FString R;
		Sim->ExtendShaft(1, FVector(1000.f, 1000.f, 0.f));
		Sim->ExcavateFloor(-1, 6, R);
		Sim->Debug_PlaceInstant(FName("SolarArray"), FVector(3500.f, 1000.f, 0.f));
		Sim->Debug_PlaceInstant(FName("BatteryBank"), FVector(1000.f, 3500.f, 0.f));
		Sim->Debug_PlaceInstant(FName("AirFilter"), FVector(1000.f, 1500.f, 0.f), -1);
		Sim->AddStock(FName("Oxygen"), 1200.0);
		Sim->AddStock(FName("Food"), 600.0);
		RunSols(2.5);
		Sim->Debug_AddColonists(4);
		// Rooms: 2 LivingQuarters (housing), a Lab (job+morale), a Garden far in
		// the corner (job+morale, cell 5 = (-1,-1), >2 from any LQ so no Odor offense).
		Sim->DesignateRoom(-1, 0, FName("LivingQuarters"), R);
		Sim->DesignateRoom(-1, 1, FName("LivingQuarters"), R);
		Sim->DesignateRoom(-1, 2, FName("Lab"), R);
		Sim->DesignateRoom(-1, 5, FName("Garden"), R);
		Sim->Debug_DeliverCargo(FName("SoilPallet"));
		Sim->Debug_DeliverCargo(FName("SeedVault"));
		Sim->AddStock(FName("Water"), 300.0);
		RunSols(0.2); // settle jobs, plant the garden

		// 1) The exp smoother (the parity-critical property): capture the
		// instantaneous Hope + the current smoothed value, run exactly one Tau
		// (3 sols), and confirm the smoothed value matches the closed form
		// S = I + (S0-I)*e^-1 to a tight tolerance. A linear lerp would miss this.
		const double InstantHope = Sim->GetColonyHope().Total;
		const double S0 = Sim->GetHopeSmoothed();
		RunSols(3.0);
		const double S1 = Sim->GetHopeSmoothed();
		const double Expected1 = InstantHope + (S0 - InstantHope) * FMath::Exp(-1.0);
		UE_LOG(LogRedHopeSim, Display, TEXT("HOPEDRIVE smoother: instant=%.2f smoothed %.2f->%.2f (expect %.2f, |err| %.3f < 0.05)"),
			InstantHope, S0, S1, Expected1, FMath::Abs(S1 - Expected1));

		// 2) The work-tempo transfer function: tempo == clamp(1 + slope*(smoothed-50)).
		const double Smoothed = Sim->GetHopeSmoothed();
		const double Tempo = Sim->GetHumanWorkTempo();
		const double TempoExpected = FMath::Clamp(1.0 + 0.006 * (Smoothed - 50.0), 0.60, 1.25);
		UE_LOG(LogRedHopeSim, Display, TEXT("HOPEDRIVE tempo: %.4f (expect %.4f) band %s at smoothed %.1f"),
			Tempo, TempoExpected, Sim->GetHopeBandName(), Smoothed);

		// 3) Tempo scales the GARDEN yield: over a short window, the Food the
		// garden adds == producingCells * baseYield * tempo * dt (isolated from
		// eating). Proves a thriving crew out-harvests a merely surviving one.
		RunSols(9.0); // near-converge so tempo is stable across the window
		const int32 Prod = Sim->GetProducingCellCount();
		const double Tempo3 = Sim->GetHumanWorkTempo();
		const double Food0 = Sim->GetStock(FName("Food"));
		const double MeasWindow = 0.25;
		RunSols(MeasWindow);
		const double FoodDelta = Sim->GetStock(FName("Food")) - Food0;
		const double Eaten = Sim->GetPopulation() * Sim->GetColonistFoodKgPerSol() * MeasWindow;
		const double GardenGiven = FoodDelta + Eaten;
		const double GardenExpected = Prod * Sim->GetGardenFoodKgPerSolPerCell() * Tempo3 * MeasWindow;
		UE_LOG(LogRedHopeSim, Display, TEXT("HOPEDRIVE garden@tempo: %d cell(s) gave %.3f kg (expect %.3f = base x tempo %.3f; |err| %.4f)"),
			Prod, GardenGiven, GardenExpected, Tempo3, FMath::Abs(GardenGiven - GardenExpected));

		// 4) Band hysteresis: the band name is consistent with the thresholds
		// (Thriving 75/70, Flourishing 90/85), and does not flicker mid-gap.
		UE_LOG(LogRedHopeSim, Display, TEXT("HOPEDRIVE band: smoothed %.1f -> %s (Thriving>=75, Flourishing>=90)"),
			Sim->GetHopeSmoothed(), Sim->GetHopeBandName());

		// 5) Save v13 round-trip: the mood + band survive exactly.
		const double SmoothedBefore = Sim->GetHopeSmoothed();
		const FString BandBefore = Sim->GetHopeBandName();
		FString Err;
		Sim->SaveColony(TEXT("hopetest"), Err);
		Sim->LoadColony(TEXT("hopetest"), Err);
		UE_LOG(LogRedHopeSim, Display, TEXT("HOPEDRIVE save/load v13: smoothed %.2f->%.2f band %s->%s (expect identical)"),
			SmoothedBefore, Sim->GetHopeSmoothed(), *BandBefore, Sim->GetHopeBandName());

		UE_LOG(LogRedHopeSim, Display, TEXT("=== HOPE-DRIVES TEST END ==="));
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return 0;
	}

	// M2 Gate D+ self-test: the GARDEN POWER FORK - grow-lit Garden costs battery
	// energy (dark->dormant when the bank can't pay) vs Greenhouse (glass, solar-
	// gated yield, near-free power). `-greenhouse`.
	if (FParse::Param(*Params, TEXT("greenhouse")))
	{
		UE_LOG(LogRedHopeSim, Display, TEXT("=== GARDEN POWER FORK TEST (M2 Gate D+) ==="));
		const int32 StepsPerSolH = (int32)(URHSimClockSubsystem::SolLengthSimSeconds / URHSimClockSubsystem::EraStepSimSeconds);
		const auto RunSols = [&](double Sols)
		{
			for (int32 S = 0; S < (int32)(Sols * StepsPerSolH); ++S)
			{
				Clock->Debug_AdvanceSimSeconds(URHSimClockSubsystem::EraStepSimSeconds);
				Sim->EraStep(URHSimClockSubsystem::EraStepSimSeconds);
			}
		};
		const auto SetSolFraction = [&](double Frac)
		{
			const double SolLen = URHSimClockSubsystem::SolLengthSimSeconds;
			const int32 Sol = (int32)(Clock->GetSimSecondsTotal() / SolLen);
			Clock->Debug_SetSimSeconds((Sol + Frac) * SolLen);
		};
		URHDefinitionsSubsystem* DefsSub = World->GetSubsystem<URHDefinitionsSubsystem>();
		{
			const FRHRoomRow* G = DefsSub ? DefsSub->GetRoom(FName("Garden")) : nullptr;
			if (!G || !G->SliceActive) { UE_LOG(LogRedHopeSim, Error, TEXT("FORK: Garden not active - DT drift")); return 1; }
		}
		// Greenhouse is a NEW row (DT sync pending); inject it in-memory (the
		// whole-row extension of the SliceActive test-knob).
		FRHRoomRow GH;
		GH.DisplayName = TEXT("Greenhouse"); GH.Function = FName("Greenhouse");
		GH.EmitsTags = TEXT("Odor"); GH.NeedsFiltration = true; GH.NeedsPlumbing = true;
		GH.MoraleWeight = 0.5f; GH.SliceActive = true;
		DefsSub->Debug_InjectRoom(FName("Greenhouse"), GH);
		if (!DefsSub->GetRoom(FName("Greenhouse"))) { UE_LOG(LogRedHopeSim, Error, TEXT("FORK: Greenhouse inject failed")); return 1; }

		// Certify a 6-cell vault with power; charge the bank.
		FString R;
		Sim->ExtendShaft(1, FVector(1000.f, 1000.f, 0.f));
		Sim->ExcavateFloor(-1, 6, R);
		Sim->Debug_PlaceInstant(FName("SolarArray"), FVector(3500.f, 1000.f, 0.f));
		Sim->Debug_PlaceInstant(FName("BatteryBank"), FVector(1000.f, 3500.f, 0.f));
		Sim->Debug_PlaceInstant(FName("AirFilter"), FVector(1000.f, 1500.f, 0.f), -1);
		Sim->AddStock(FName("Oxygen"), 900.0);
		Sim->AddStock(FName("Water"), 400.0);
		RunSols(2.5);
		int32 SolarId = 0;
		for (const FRHBuildingInstance& B : Sim->GetBuildings())
		{
			if (B.DefName == FName("SolarArray")) { SolarId = B.Id; break; }
		}

		// 1) Grow-lit Garden: zone + plant; the grow-light load is per-cell.
		Sim->DesignateRoom(-1, 0, FName("Garden"), R);
		Sim->Debug_DeliverCargo(FName("SoilPallet"));
		Sim->Debug_DeliverCargo(FName("SeedVault"));
		RunSols(0.2);
		UE_LOG(LogRedHopeSim, Display, TEXT("FORK garden planted: planted=%d growLightW=%.0f (expect 1, 60)"),
			Sim->GetPlantedCellCount(), Sim->GetGardenPowerDrawW());

		// 2) Powered growth is STEADY (time-independent): a charged bank yields
		// exactly cells x base x tempo x window regardless of sol time.
		Sim->Debug_SetBatteryWh(4000.0);
		const double LitF0 = Sim->GetStock(FName("Food"));
		RunSols(0.25);
		UE_LOG(LogRedHopeSim, Display, TEXT("FORK grow-lit powered: gave %.3f kg over 0.25 sol (expect 0.250) producing=%d"),
			Sim->GetStock(FName("Food")) - LitF0, Sim->GetProducingCellCount());

		// 3) Blackout: kill the array + empty the bank -> grow-lights dark, the
		// crop holds dormant (not dead), floor still rated (short window).
		Sim->SetManualPower(SolarId, false);
		Sim->Debug_SetBatteryWh(0.0);
		RunSols(0.15);
		UE_LOG(LogRedHopeSim, Display, TEXT("FORK blackout: dark=%d producing=%d planted=%d rated=%d (expect 1, 0, 1, 1)"),
			(int32)Sim->AreGrowLightsDark(), Sim->GetProducingCellCount(),
			Sim->GetPlantedCellCount(), (int32)Sim->IsFloorRated(-1));
		Sim->SetManualPower(SolarId, true);
		Sim->Debug_SetBatteryWh(4000.0);

		// 4) Greenhouse needs GLASS to plant: zone cell 1, run with no glass ->
		// not planted; deliver a crate -> plants, glass consumed.
		Sim->DesignateRoom(-1, 1, FName("Greenhouse"), R);
		RunSols(0.1);
		const int32 PlantedNoGlass = Sim->GetPlantedCellCount();
		Sim->Debug_DeliverCargo(FName("GlassCrate")); // +400 Glass
		RunSols(0.1);
		UE_LOG(LogRedHopeSim, Display, TEXT("FORK greenhouse plant: no-glass planted=%d, after crate planted=%d glass=%.0f (expect 1, 2, 200)"),
			PlantedNoGlass, Sim->GetPlantedCellCount(), Sim->GetStock(FName("Glass")));

		// 5) Greenhouse is SOLAR-gated, grow-lit is STEADY: at midday both
		// produce; at deep night only the grow-lit garden does.
		Sim->Debug_SetBatteryWh(4000.0);
		SetSolFraction(0.5); // midday
		RunSols(0.05);
		const int32 ProdDay = Sim->GetProducingCellCount();
		Sim->Debug_SetBatteryWh(4000.0);
		SetSolFraction(0.92); // deep night
		RunSols(0.05);
		const int32 ProdNight = Sim->GetProducingCellCount();
		UE_LOG(LogRedHopeSim, Display, TEXT("FORK solar-gate: midday producing=%d, night producing=%d (expect 2, 1 - greenhouse dark at night, grow-lit steady)"),
			ProdDay, ProdNight);

		UE_LOG(LogRedHopeSim, Display, TEXT("=== GARDEN POWER FORK TEST END ==="));
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return 0;
	}

	// M2 Gate D+ self-test: the GENERATIONAL CARROT - a flourishing colony grows,
	// the food-buffer + housing gates, the first-Martian-born milestone + its
	// Hope surge, streak reset on lapse, save v15. `-growth`.
	if (FParse::Param(*Params, TEXT("growth")))
	{
		UE_LOG(LogRedHopeSim, Display, TEXT("=== GENERATIONAL CARROT TEST (M2 Gate D+) ==="));
		const int32 StepsPerSolH = (int32)(URHSimClockSubsystem::SolLengthSimSeconds / URHSimClockSubsystem::EraStepSimSeconds);
		// Top up fresh ice-melt each step so water POTABILITY stays high - this
		// test isolates the GROWTH gate from the (separately tested) water loop,
		// whose potability decay would otherwise drag Hope below the threshold.
		// ~100 kg/sol holds potability against the 0.08/sol decay.
		const auto RunSols = [&](double Sols)
		{
			const double StepSols = URHSimClockSubsystem::EraStepSimSeconds / (double)URHSimClockSubsystem::SolLengthSimSeconds;
			for (int32 S = 0; S < (int32)(Sols * StepsPerSolH); ++S)
			{
				Sim->Debug_AddFreshWater(100.0 * StepSols);
				Clock->Debug_AdvanceSimSeconds(URHSimClockSubsystem::EraStepSimSeconds);
				Sim->EraStep(URHSimClockSubsystem::EraStepSimSeconds);
			}
		};
		URHDefinitionsSubsystem* DefsSub = World->GetSubsystem<URHDefinitionsSubsystem>();
		for (const TCHAR* RowName : { TEXT("LivingQuarters"), TEXT("Lab") })
		{
			const FRHRoomRow* Row = DefsSub ? DefsSub->GetRoom(FName(RowName)) : nullptr;
			if (!Row || !Row->SliceActive) { UE_LOG(LogRedHopeSim, Error, TEXT("GROWTH: DT_Rooms '%s' not active"), RowName); return 1; }
		}

		// Certify an 8-cell vault, house 2 (so there is HOUSING HEADROOM to grow
		// into), and zone rooms so Hope climbs into THRIVING. Deep O2/Water so
		// only the growth gates are ever the variable.
		FString R;
		Sim->ExtendShaft(1, FVector(1000.f, 1000.f, 0.f));
		Sim->ExcavateFloor(-1, 8, R);
		Sim->Debug_PlaceInstant(FName("SolarArray"), FVector(3500.f, 1000.f, 0.f));
		Sim->Debug_PlaceInstant(FName("BatteryBank"), FVector(1000.f, 3500.f, 0.f));
		Sim->Debug_PlaceInstant(FName("AirFilter"), FVector(1000.f, 1500.f, 0.f), -1);
		Sim->AddStock(FName("Oxygen"), 4000.0);
		Sim->AddStock(FName("Water"), 2000.0);
		Sim->AddStock(FName("Food"), 4000.0); // deep buffer -> food gate always met
		RunSols(2.5);
		Sim->Debug_AddColonists(2);
		// Rooms: 2 quarters (housing), a Lab (jobs+morale) -> Hope well into THRIVING.
		Sim->DesignateRoom(-1, 0, FName("LivingQuarters"), R);
		Sim->DesignateRoom(-1, 1, FName("LivingQuarters"), R);
		Sim->DesignateRoom(-1, 2, FName("Lab"), R);
		Sim->DesignateRoom(-1, 3, FName("Lab"), R);
		RunSols(14.0); // let the smoothed mood settle above the growth threshold
		               // (instant ~77.5; the exp smoother needs > 2 Tau to cross 75)

		// 1) Eligible + growing: smoothed Hope >= 75, free beds, food buffer.
		UE_LOG(LogRedHopeSim, Display, TEXT("GROWTH eligible: smoothed=%.1f band=%s eligible=%d pop=%d beds-free=%d"),
			Sim->GetHopeSmoothed(), Sim->GetHopeBandName(), (int32)Sim->IsGrowthEligible(),
			Sim->GetPopulation(), Sim->GetFreeHousing());

		// 2) Run one full growth interval (20 sols) -> exactly one birth, and it
		// is the FIRST MARTIAN (milestone + a Hope surge folded into the index).
		const int32 Pop0 = Sim->GetPopulation();
		const double MilestonesBefore = Sim->GetColonyHope().Milestones;
		RunSols(20.5);
		UE_LOG(LogRedHopeSim, Display, TEXT("GROWTH first birth: pop %d->%d births=%d firstBorn=%d (expect 2->3, 1, 1)"),
			Pop0, Sim->GetPopulation(), Sim->GetBirthsOnMars(), (int32)Sim->HasFirstBorn());
		UE_LOG(LogRedHopeSim, Display, TEXT("GROWTH milestone: Hope milestones %.1f -> %.1f (expect 5.0 -> 11.0 = vault + first-born)"),
			MilestonesBefore, Sim->GetColonyHope().Milestones);

		// 3) Streak RESETS when the colony stops flourishing: crash Hope by
		// gutting housing quality (evict the quarters) -> not eligible -> streak 0.
		Sim->DesignateRoom(-1, 0, NAME_None, R);
		Sim->DesignateRoom(-1, 1, NAME_None, R);
		Sim->DesignateRoom(-1, 2, NAME_None, R);
		Sim->DesignateRoom(-1, 3, NAME_None, R);
		RunSols(8.0); // mood falls out of THRIVING
		UE_LOG(LogRedHopeSim, Display, TEXT("GROWTH lapse: smoothed=%.1f eligible=%d progress=%.2f (expect not-eligible, 0.00)"),
			Sim->GetHopeSmoothed(), (int32)Sim->IsGrowthEligible(), Sim->GetGrowthProgress());

		// 4) Save v15 round-trip: births + first-born flag survive.
		const int32 BirthsBefore = Sim->GetBirthsOnMars();
		FString Err;
		Sim->SaveColony(TEXT("growthtest"), Err);
		Sim->LoadColony(TEXT("growthtest"), Err);
		UE_LOG(LogRedHopeSim, Display, TEXT("GROWTH save/load v15: births %d->%d firstBorn=%d (expect identical, 1)"),
			BirthsBefore, Sim->GetBirthsOnMars(), (int32)Sim->HasFirstBorn());

		UE_LOG(LogRedHopeSim, Display, TEXT("=== GENERATIONAL CARROT TEST END ==="));
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return 0;
	}

	// M2 Gate D+ self-test: DISCOVERIES (the Flourishing layer) - staffed Labs
	// accrue seat-hours while smoothed Hope holds above the threshold; rows pop
	// in authored Order with permanent Hope milestones + stock rewards; progress
	// keeps across a pause; save v16. `-discovery`.
	if (FParse::Param(*Params, TEXT("discovery")))
	{
		UE_LOG(LogRedHopeSim, Display, TEXT("=== DISCOVERIES TEST (M2 Gate D+) ==="));
		const int32 StepsPerSolH = (int32)(URHSimClockSubsystem::SolLengthSimSeconds / URHSimClockSubsystem::EraStepSimSeconds);
		// Fresh-water topping keeps potability out of the Hope math (the water
		// loop is separately tested; this isolates the discovery gate).
		const auto RunSols = [&](double Sols)
		{
			const double StepSols = URHSimClockSubsystem::EraStepSimSeconds / (double)URHSimClockSubsystem::SolLengthSimSeconds;
			for (int32 S = 0; S < (int32)(Sols * StepsPerSolH); ++S)
			{
				Sim->Debug_AddFreshWater(100.0 * StepSols);
				Clock->Debug_AdvanceSimSeconds(URHSimClockSubsystem::EraStepSimSeconds);
				Sim->EraStep(URHSimClockSubsystem::EraStepSimSeconds);
			}
		};
		// The DT_Discoveries asset doesn't exist until its first editor import;
		// inject the four authored rows (mirrors RH_Discoveries.csv exactly).
		URHDefinitionsSubsystem* DefsSub = World->GetSubsystem<URHDefinitionsSubsystem>();
		const auto Inject = [&](const TCHAR* Name, const TCHAR* Display, int32 Order, float SeatH, float Bonus, FName Reward, float Kg)
		{
			FRHDiscoveryRow Row;
			Row.DisplayName = Display; Row.Order = Order; Row.LabSeatHours = SeatH;
			Row.HopeBonus = Bonus; Row.RewardResource = Reward; Row.RewardKg = Kg;
			Row.Alert = FString::Printf(TEXT("DISCOVERY — %s"), Display); Row.SliceActive = true;
			DefsSub->Debug_InjectDiscovery(FName(Name), Row);
		};
		Inject(TEXT("CropStrain"), TEXT("Hardy Crop Strain"), 1, 100.f, 2.f, FName("Seeds"), 50.f);
		Inject(TEXT("RegolithCeramics"), TEXT("Regolith Ceramics"), 2, 150.f, 2.f, FName("Struct"), 100.f);
		Inject(TEXT("SubsurfaceBrine"), TEXT("Subsurface Brine Seep"), 3, 200.f, 3.f, FName("Water"), 150.f);
		Inject(TEXT("MicrobialLife"), TEXT("Subsurface Microbial Life"), 4, 300.f, 8.f, NAME_None, 0.f);

		// A flourishing 2-person colony: instant Hope 88.5 (base 50 + housing 15
		// + Lab 1.5 + Dining 3 + jobs 6 + vault 5 + comforts 8), no emitters so
		// zero adjacency noise. The smoother crosses 85 at ~7.2 sols.
		FString R;
		Sim->ExtendShaft(1, FVector(1000.f, 1000.f, 0.f));
		Sim->ExcavateFloor(-1, 6, R);
		Sim->Debug_PlaceInstant(FName("SolarArray"), FVector(3500.f, 1000.f, 0.f));
		Sim->Debug_PlaceInstant(FName("BatteryBank"), FVector(1000.f, 3500.f, 0.f));
		Sim->Debug_PlaceInstant(FName("AirFilter"), FVector(1000.f, 1500.f, 0.f), -1);
		Sim->AddStock(FName("Oxygen"), 4000.0);
		Sim->AddStock(FName("Water"), 2000.0);
		Sim->AddStock(FName("Food"), 4000.0);
		RunSols(2.5);
		Sim->Debug_AddColonists(2);
		Sim->DesignateRoom(-1, 0, FName("LivingQuarters"), R);
		Sim->DesignateRoom(-1, 1, FName("LivingQuarters"), R);
		Sim->DesignateRoom(-1, 2, FName("Lab"), R);
		Sim->DesignateRoom(-1, 3, FName("Lab"), R);
		Sim->DesignateRoom(-1, 4, FName("Dining"), R);
		Sim->Debug_DeliverCargo(FName("LuxuryGoods"));

		// 1) Below the threshold nothing accrues (the smoothed mood lags).
		RunSols(1.0);
		UE_LOG(LogRedHopeSim, Display, TEXT("DISC gate: smoothed=%.1f accruing=%d progress=%.2f (expect <85, 0, 0.00)"),
			Sim->GetHopeSmoothed(), (int32)Sim->IsResearchAccruing(), Sim->GetDiscoveryProgress());

		// 2) 10 sols in: the mood crossed ~85 near sol 7; 2 Lab seats x 48
		// seat-h/sol -> CropStrain (100) pops. Seeds +50, milestone 5->7.
		const double Seeds0 = Sim->GetStock(FName("Seeds"));
		RunSols(9.0);
		UE_LOG(LogRedHopeSim, Display, TEXT("DISC first: found=%d next=%s seeds +%.0f milestones=%.1f (expect 1, RegolithCeramics, 50, 7.0)"),
			Sim->GetDiscoveryLog().Num(), *Sim->GetNextDiscovery().ToString(),
			Sim->GetStock(FName("Seeds")) - Seeds0, Sim->GetColonyHope().Milestones);

		// 3) 4 more sols: RegolithCeramics (150) pops on the spillover; the
		// third (200) is still accruing. Struct +100 exactly (nothing else
		// makes or spends Struct in this colony).
		const double Struct0 = Sim->GetStock(FName("Struct"));
		RunSols(4.0);
		UE_LOG(LogRedHopeSim, Display, TEXT("DISC second: found=%d next=%s struct +%.0f milestones=%.1f (expect 2, SubsurfaceBrine, 100, 9.0)"),
			Sim->GetDiscoveryLog().Num(), *Sim->GetNextDiscovery().ToString(),
			Sim->GetStock(FName("Struct")) - Struct0, Sim->GetColonyHope().Milestones);

		// 4) Save v16 round-trip: the log and the partial progress survive.
		const int32 FoundBefore = Sim->GetDiscoveryLog().Num();
		const double ProgressBefore = Sim->GetDiscoveryProgress();
		FString Err;
		Sim->SaveColony(TEXT("disctest"), Err);
		Sim->LoadColony(TEXT("disctest"), Err);
		UE_LOG(LogRedHopeSim, Display, TEXT("DISC save/load v16: found %d->%d progress %.3f->%.3f (expect identical)"),
			FoundBefore, Sim->GetDiscoveryLog().Num(), ProgressBefore, Sim->GetDiscoveryProgress());

		UE_LOG(LogRedHopeSim, Display, TEXT("=== DISCOVERIES TEST END ==="));
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return 0;
	}

	// M3 Gate A self-test: RIVALS & TRADE - convoy dispatch (committed costs),
	// preflight refusals, the round-trip barter (relation warms), the DUST-STORM
	// FREEZE, and save v17. `-trade`.
	if (FParse::Param(*Params, TEXT("trade")))
	{
		UE_LOG(LogRedHopeSim, Display, TEXT("=== RIVALS & TRADE TEST (M3 Gate A) ==="));
		const int32 StepsPerSolH = (int32)(URHSimClockSubsystem::SolLengthSimSeconds / URHSimClockSubsystem::EraStepSimSeconds);
		const auto RunSols = [&](double Sols)
		{
			for (int32 S = 0; S < (int32)(Sols * StepsPerSolH); ++S)
			{
				Clock->Debug_AdvanceSimSeconds(URHSimClockSubsystem::EraStepSimSeconds);
				Sim->EraStep(URHSimClockSubsystem::EraStepSimSeconds);
			}
		};
		const auto Dispatch = [&](const TCHAR* Rival)
		{
			FRHCommand Cmd; Cmd.Verb = FName("Convoy"); Cmd.Target = FName(Rival);
			Sim->EnqueueCommand(Cmd);
			RunSols(0.1); // clear the signal lag + execute
		};
		// Inject Zarya (DT_Rivals doesn't exist until its first editor import).
		URHDefinitionsSubsystem* DefsSub = World->GetSubsystem<URHDefinitionsSubsystem>();
		FRHRivalRow Z;
		Z.DisplayName = TEXT("Zarya Station"); Z.Nation = TEXT("Zarya Consortium");
		Z.DistanceKm = 120.f; Z.ExportLot = TEXT("Ice:150"); Z.ImportLot = TEXT("Struct:100");
		Z.RelationStart = 40.f; Z.SliceActive = true;
		DefsSub->Debug_InjectRival(FName("Zarya"), Z);

		// The fresh colony has 400 Struct (Lander) + 6 SpareParts, but no fuel.
		// 1) No hydrogen -> dispatch refused, convoy stays idle.
		Dispatch(TEXT("Zarya"));
		UE_LOG(LogRedHopeSim, Display, TEXT("TRADE no-fuel: convoy=%s (expect idle - no Hydrogen)"),
			Sim->GetConvoyRival().IsNone() ? TEXT("idle") : *Sim->GetConvoyRival().ToString());

		// 2) Fuel up + dispatch: costs committed at departure (Struct -100, H2 -8,
		// SpareParts -1), convoy outbound to Zarya.
		Sim->AddStock(FName("Hydrogen"), 40.0);
		const double Struct0 = Sim->GetTotalSolid(FName("Struct"));
		Dispatch(TEXT("Zarya"));
		UE_LOG(LogRedHopeSim, Display, TEXT("TRADE dispatched: convoy=%s struct %.0f->%.0f H2=%.0f parts=%.0f (expect Zarya, -100, 32, 5)"),
			Sim->GetConvoyRival().IsNone() ? TEXT("idle") : *Sim->GetConvoyRival().ToString(),
			Struct0, Sim->GetTotalSolid(FName("Struct")), Sim->GetStock(FName("Hydrogen")),
			Sim->GetTotalSolid(FName("SpareParts")) + Sim->GetStock(FName("SpareParts")));

		// 3) A second dispatch while the convoy is out -> refused.
		Dispatch(TEXT("Zarya"));
		UE_LOG(LogRedHopeSim, Display, TEXT("TRADE double-dispatch: still one convoy to %s (expect Zarya - refused)"),
			Sim->GetConvoyRival().IsNone() ? TEXT("idle") : *Sim->GetConvoyRival().ToString());

		// 4) The round trip (2 sols out + 2 back at 60 km/sol over 120 km):
		// their Ice lands, relation 40 -> 42, convoy idle. Clear sky (sol < 5).
		const double Ice0 = Sim->GetTotalSolid(FName("Ice"));
		RunSols(4.5);
		UE_LOG(LogRedHopeSim, Display, TEXT("TRADE round trip: convoy=%s ice +%.0f relation=%.0f (expect idle, 150, 42)"),
			Sim->GetConvoyRival().IsNone() ? TEXT("idle") : *Sim->GetConvoyRival().ToString(),
			Sim->GetTotalSolid(FName("Ice")) - Ice0, Sim->GetRivalRelation(FName("Zarya")));

		// 5) The dust-storm FREEZE: jump the clock into Storm_1 (canon sols
		// 12-15), dispatch, and confirm progress does NOT advance while the storm
		// blows - then resumes once it clears.
		Clock->Debug_SetSimSeconds(12.5 * URHSimClockSubsystem::SolLengthSimSeconds);
		Dispatch(TEXT("Zarya"));
		const double PStorm = Sim->GetConvoyProgress();
		RunSols(1.0); // to ~sol 13.6, still inside the storm
		const double PStill = Sim->GetConvoyProgress();
		Clock->Debug_SetSimSeconds(16.0 * URHSimClockSubsystem::SolLengthSimSeconds); // past the storm
		RunSols(0.2);
		const double PMoving = Sim->GetConvoyProgress();
		UE_LOG(LogRedHopeSim, Display, TEXT("TRADE storm freeze: progress %.3f -> %.3f (frozen) -> %.3f (moving) (expect ~equal then greater)"),
			PStorm, PStill, PMoving);

		// 5b) Aggregate-cost conservation (adversarial-review fix): a rival whose
		// ImportLot NAMES SpareParts must be gated on fuel+wear+lot SUMMED, not
		// three independent checks - so it refuses cleanly instead of driving the
		// pool negative. Let the storm convoy finish (sol 17 is a flare, which
		// does NOT freeze it), then inject Greed (ImportLot SpareParts:4).
		RunSols(4.5); // storm-test convoy gets home -> idle
		FRHRivalRow Greed = Z; Greed.DisplayName = TEXT("Greedy Post");
		Greed.ImportLot = TEXT("SpareParts:4"); Greed.ExportLot = TEXT("Ice:10");
		DefsSub->Debug_InjectRival(FName("Greed"), Greed);
		// Drain SpareParts to exactly 3 (< wear 1 + lot 4 = 5), keep H2 high.
		Sim->AddStock(FName("SpareParts"), 3.0 - (Sim->GetTotalSolid(FName("SpareParts")) + Sim->GetStock(FName("SpareParts"))));
		Sim->AddStock(FName("Hydrogen"), 40.0);
		const double Parts0 = Sim->GetTotalSolid(FName("SpareParts")) + Sim->GetStock(FName("SpareParts"));
		Dispatch(TEXT("Greed"));
		const double Parts1 = Sim->GetTotalSolid(FName("SpareParts")) + Sim->GetStock(FName("SpareParts"));
		UE_LOG(LogRedHopeSim, Display, TEXT("TRADE overlap-guard: convoy=%s parts %.0f->%.0f (expect idle, 3->3 - refused, never negative)"),
			Sim->GetConvoyRival().IsNone() ? TEXT("idle") : *Sim->GetConvoyRival().ToString(), Parts0, Parts1);

		// 6) Save v17 round-trip MID-TRANSIT: re-dispatch a legitimate Zarya run,
		// advance partway, then save/load and confirm the convoy + relations
		// survive exactly.
		Dispatch(TEXT("Zarya"));
		RunSols(1.0); // outbound, mid-leg
		const FName OutTo = Sim->GetConvoyRival();
		const double ProgBefore = Sim->GetConvoyProgress();
		const double RelBefore = Sim->GetRivalRelation(FName("Zarya"));
		FString Err;
		Sim->SaveColony(TEXT("tradetest"), Err);
		Sim->LoadColony(TEXT("tradetest"), Err);
		UE_LOG(LogRedHopeSim, Display, TEXT("TRADE save/load v17: convoy %s->%s progress %.3f->%.3f relation %.0f->%.0f (expect identical)"),
			OutTo.IsNone() ? TEXT("idle") : *OutTo.ToString(),
			Sim->GetConvoyRival().IsNone() ? TEXT("idle") : *Sim->GetConvoyRival().ToString(),
			ProgBefore, Sim->GetConvoyProgress(), RelBefore, Sim->GetRivalRelation(FName("Zarya")));

		UE_LOG(LogRedHopeSim, Display, TEXT("=== RIVALS & TRADE TEST END ==="));
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return 0;
	}

	// M3 Gate B self-test: EARTH'S SHADOW - the layer is inert without neighbors,
	// tension drifts + generates a demand once a rival exists, and the identity
	// axis + tension scale the requisition award. Save v18. `-earth`.
	if (FParse::Param(*Params, TEXT("earth")))
	{
		UE_LOG(LogRedHopeSim, Display, TEXT("=== EARTH'S SHADOW TEST (M3 Gate B) ==="));
		const int32 StepsPerSolH = (int32)(URHSimClockSubsystem::SolLengthSimSeconds / URHSimClockSubsystem::EraStepSimSeconds);
		const auto RunSols = [&](double Sols)
		{
			for (int32 S = 0; S < (int32)(Sols * StepsPerSolH); ++S)
			{
				Clock->Debug_AdvanceSimSeconds(URHSimClockSubsystem::EraStepSimSeconds);
				Sim->EraStep(URHSimClockSubsystem::EraStepSimSeconds);
			}
		};
		URHDefinitionsSubsystem* DefsSub = World->GetSubsystem<URHDefinitionsSubsystem>();

		// 1) INERT without neighbors: tension doesn't move, multiplier is exactly
		// 1.0 (this is what keeps every pre-M3 baseline byte-identical).
		RunSols(2.0);
		UE_LOG(LogRedHopeSim, Display, TEXT("EARTH no-neighbors: tension=%.2f mult=%.3f (expect 0.00, 1.000 - inert)"),
			Sim->GetEarthTension(), Sim->GetRequisitionMultiplier());

		// Now Mars has a neighbor: the layer switches on.
		FRHRivalRow Z;
		Z.DisplayName = TEXT("Zarya Station"); Z.Nation = TEXT("Zarya Consortium");
		Z.DistanceKm = 120.f; Z.ExportLot = TEXT("Ice:150"); Z.ImportLot = TEXT("Struct:100");
		Z.RelationStart = 40.f; Z.SliceActive = true;
		DefsSub->Debug_InjectRival(FName("Zarya"), Z);

		// 2) Tension DRIFTS (0.6/sol): after 10 sols ~6.0.
		RunSols(10.0);
		UE_LOG(LogRedHopeSim, Display, TEXT("EARTH drift: tension=%.1f (expect ~6.0 = 0.6 x 10) demand=%d (expect 0)"),
			Sim->GetEarthTension(), (int32)Sim->IsEarthDemandPending());

		// 3) DEMAND generated once tension crosses 60: jump it up.
		Sim->Debug_AddTension(55.0); // -> ~61
		RunSols(0.2);
		UE_LOG(LogRedHopeSim, Display, TEXT("EARTH demand: tension=%.0f demand=%d (expect >=60, 1)"),
			Sim->GetEarthTension(), (int32)Sim->IsEarthDemandPending());

		// 4) The identity axis scales the requisition. At tension ~61, axis 0:
		// mult = 1 - 0.3*(61/100) = 0.817. Full Earth-aligned (-100): +0.4 bonus
		// but still -tension: 1 + 0.4 - 0.183 = 1.217 (clamped <= 1.4). Full
		// Martian (+100): 1 - 0.6 - 0.183 = 0.217.
		const double MultNeutral = Sim->GetRequisitionMultiplier();
		Sim->Debug_ShiftIdentity(-100.0); // Earth-aligned
		const double MultEarth = Sim->GetRequisitionMultiplier();
		Sim->Debug_ShiftIdentity(200.0);  // -> +100 Martian
		const double MultMartian = Sim->GetRequisitionMultiplier();
		UE_LOG(LogRedHopeSim, Display, TEXT("EARTH requisition: neutral=%.3f earth-aligned=%.3f martian=%.3f (expect ~0.82, ~1.22, ~0.22)"),
			MultNeutral, MultEarth, MultMartian);

		// 5) Save v18 round-trip: tension, axis, demand survive.
		const double TenB = Sim->GetEarthTension(), AxB = Sim->GetIdentityAxis();
		const int32 DemB = (int32)Sim->IsEarthDemandPending();
		FString Err;
		Sim->SaveColony(TEXT("earthtest"), Err);
		Sim->LoadColony(TEXT("earthtest"), Err);
		UE_LOG(LogRedHopeSim, Display, TEXT("EARTH save/load v18: tension %.0f->%.0f axis %+.0f->%+.0f demand %d->%d (expect identical)"),
			TenB, Sim->GetEarthTension(), AxB, Sim->GetIdentityAxis(), DemB, (int32)Sim->IsEarthDemandPending());

		UE_LOG(LogRedHopeSim, Display, TEXT("=== EARTH'S SHADOW TEST END ==="));
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return 0;
	}

	// M3 Gate C self-test: THE SOLIDARITY DILEMMA - Comply severs the route
	// (closed, relation craters, axis Earth-ward, tension relieved, morale grief)
	// vs Defy (axis Martian, relations + morale up, tension barely eases). Both
	// via the uplink verb; save v19. `-solidarity`.
	if (FParse::Param(*Params, TEXT("solidarity")))
	{
		UE_LOG(LogRedHopeSim, Display, TEXT("=== SOLIDARITY DILEMMA TEST (M3 Gate C) ==="));
		const int32 StepsPerSolH = (int32)(URHSimClockSubsystem::SolLengthSimSeconds / URHSimClockSubsystem::EraStepSimSeconds);
		const auto RunSols = [&](double Sols)
		{
			for (int32 S = 0; S < (int32)(Sols * StepsPerSolH); ++S)
			{
				Clock->Debug_AdvanceSimSeconds(URHSimClockSubsystem::EraStepSimSeconds);
				Sim->EraStep(URHSimClockSubsystem::EraStepSimSeconds);
			}
		};
		const auto Answer = [&](const TCHAR* Choice)
		{
			FRHCommand Cmd; Cmd.Verb = FName("Solidarity"); Cmd.Target = FName(Choice);
			Sim->EnqueueCommand(Cmd);
			RunSols(0.1);
		};
		URHDefinitionsSubsystem* DefsSub = World->GetSubsystem<URHDefinitionsSubsystem>();
		FRHRivalRow Z;
		Z.DisplayName = TEXT("Zarya Station"); Z.Nation = TEXT("Zarya Consortium");
		Z.DistanceKm = 120.f; Z.ExportLot = TEXT("Ice:150"); Z.ImportLot = TEXT("Struct:100");
		Z.RelationStart = 40.f; Z.SliceActive = true;
		DefsSub->Debug_InjectRival(FName("Zarya"), Z);

		// Housed colony (2 crew) so SolidarityHope actually reads into the index.
		FString R;
		Sim->ExtendShaft(1, FVector(1000.f, 1000.f, 0.f));
		Sim->ExcavateFloor(-1, 4, R);
		Sim->Debug_PlaceInstant(FName("SolarArray"), FVector(3500.f, 1000.f, 0.f));
		Sim->Debug_PlaceInstant(FName("BatteryBank"), FVector(1000.f, 3500.f, 0.f));
		Sim->Debug_PlaceInstant(FName("AirFilter"), FVector(1000.f, 1500.f, 0.f), -1);
		Sim->AddStock(FName("Oxygen"), 2000.0);
		Sim->AddStock(FName("Water"), 2000.0);
		Sim->AddStock(FName("Food"), 2000.0);
		RunSols(2.5);
		Sim->Debug_AddColonists(2);
		RunSols(0.2);

		// 0) No demand -> answering is refused.
		Answer(TEXT("Defy"));
		UE_LOG(LogRedHopeSim, Display, TEXT("SOLID no-demand: axis=%+.0f (expect 0 - refused, nothing to answer)"), Sim->GetIdentityAxis());

		// Raise a demand.
		Sim->Debug_AddTension(65.0);
		RunSols(0.2);
		UE_LOG(LogRedHopeSim, Display, TEXT("SOLID demand up: pending=%d tension=%.0f (expect 1, >=60)"),
			(int32)Sim->IsEarthDemandPending(), Sim->GetEarthTension());

		// 1) DEFY: axis -> Martian (+20), relation +5 (40->45), morale shock +12,
		// tension eases only a little, requisitions stay slashed by the axis.
		const double Hope0 = Sim->GetColonyHope().Total;
		Answer(TEXT("Defy"));
		UE_LOG(LogRedHopeSim, Display, TEXT("SOLID defy: axis=%+.0f relation=%.0f pending=%d hope %.1f->%.1f mult=%.2f (expect +20, 45, 0, up, <1)"),
			Sim->GetIdentityAxis(), Sim->GetRivalRelation(FName("Zarya")), (int32)Sim->IsEarthDemandPending(),
			Hope0, Sim->GetColonyHope().Total, Sim->GetRequisitionMultiplier());

		// 2) The morale shock FADES: after ~2 tau it's much smaller.
		const double Solid1 = Sim->GetSolidarityHope();
		RunSols(10.0);
		UE_LOG(LogRedHopeSim, Display, TEXT("SOLID shock fades: %.2f -> %.2f (expect toward 0)"), Solid1, Sim->GetSolidarityHope());

		// Raise a second demand for the Comply branch.
		Sim->Debug_AddTension(65.0);
		RunSols(0.2);

		// 3) COMPLY: route to Zarya CLOSES, relation craters, axis swings back
		// Earth-ward, tension relieved hard, morale grief (negative shock).
		Answer(TEXT("Comply"));
		UE_LOG(LogRedHopeSim, Display, TEXT("SOLID comply: closed=%d relation=%.0f axis=%+.0f tension=%.0f (expect 1, 5, 0, 55 = 100 clamped - 45 relief, below re-demand)"),
			Sim->GetClosedRouteCount(), Sim->GetRivalRelation(FName("Zarya")),
			Sim->GetIdentityAxis(), Sim->GetEarthTension());

		// 4) The closed route REFUSES a convoy (the dependency is severed).
		Sim->AddStock(FName("Hydrogen"), 40.0);
		FRHCommand Cv; Cv.Verb = FName("Convoy"); Cv.Target = FName("Zarya");
		Sim->EnqueueCommand(Cv); RunSols(0.1);
		UE_LOG(LogRedHopeSim, Display, TEXT("SOLID severed: convoy=%s (expect idle - route closed)"),
			Sim->GetConvoyRival().IsNone() ? TEXT("idle") : *Sim->GetConvoyRival().ToString());

		// 5) Save v19 round-trip: closed routes + axis + morale survive.
		const int32 ClosedB = Sim->GetClosedRouteCount();
		const double AxB = Sim->GetIdentityAxis();
		FString Err;
		Sim->SaveColony(TEXT("soltest"), Err);
		Sim->LoadColony(TEXT("soltest"), Err);
		UE_LOG(LogRedHopeSim, Display, TEXT("SOLID save/load v19: closed %d->%d axis %+.0f->%+.0f route-closed(Zarya)=%d (expect identical, 1)"),
			ClosedB, Sim->GetClosedRouteCount(), AxB, Sim->GetIdentityAxis(), (int32)Sim->IsRouteClosed(FName("Zarya")));

		UE_LOG(LogRedHopeSim, Display, TEXT("=== SOLIDARITY DILEMMA TEST END ==="));
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return 0;
	}

	// M4 Gate A self-test: THE COVERT LAYER - a covert requisition with a
	// deterministic (seeded, day/night) detection check, the HumanNature axis
	// (theft down, fair trade up), HiddenTension, save v20. `-covert`.
	if (FParse::Param(*Params, TEXT("covert")))
	{
		UE_LOG(LogRedHopeSim, Display, TEXT("=== COVERT LAYER TEST (M4 Gate A) ==="));
		const int32 StepsPerSolH = (int32)(URHSimClockSubsystem::SolLengthSimSeconds / URHSimClockSubsystem::EraStepSimSeconds);
		const auto RunSols = [&](double Sols)
		{
			for (int32 S = 0; S < (int32)(Sols * StepsPerSolH); ++S)
			{
				Clock->Debug_AdvanceSimSeconds(URHSimClockSubsystem::EraStepSimSeconds);
				Sim->EraStep(URHSimClockSubsystem::EraStepSimSeconds);
			}
		};
		const auto Covert = [&](const TCHAR* Rival)
		{
			FRHCommand Cmd; Cmd.Verb = FName("Covert"); Cmd.Target = FName(Rival);
			Sim->EnqueueCommand(Cmd);
			RunSols(0.1);
		};
		URHDefinitionsSubsystem* DefsSub = World->GetSubsystem<URHDefinitionsSubsystem>();
		FRHRivalRow Z;
		Z.DisplayName = TEXT("Zarya Station"); Z.Nation = TEXT("Zarya Consortium");
		Z.DistanceKm = 120.f; Z.ExportLot = TEXT("Ice:150"); Z.ImportLot = TEXT("Struct:100");
		Z.RelationStart = 40.f; Z.SliceActive = true;
		DefsSub->Debug_InjectRival(FName("Zarya"), Z);
		// Set the clock to deep night (sol-fraction 0.9) so covert detection is low.
		Clock->Debug_SetSimSeconds(0.9 * URHSimClockSubsystem::SolLengthSimSeconds);

		// 1) First covert op: EXACTLY ONE of {clean gains goods, caught craters
		// relation}; the intent always drops HumanNature by the shift (-6);
		// attempts increments; HiddenTension rises. Deterministic outcome.
		// Run four ops and record each outcome as C(lean)/X(caught). Because the
		// detection seed is now a CONTENT hash (FCrc of the rival name), this
		// sequence is IDENTICAL on every machine - a cross-run determinism guard.
		// A reversion to GetTypeHash(FName) would make it machine-dependent and
		// diverge from the pinned pattern. (Deep night, relation 40 -> detection
		// low but non-zero; the fixed seed decides each.)
		FString Pattern;
		for (int32 op = 0; op < 4; ++op)
		{
			const double IceB = Sim->GetTotalSolid(FName("Ice"));
			const double RelB = Sim->GetRivalRelation(FName("Zarya"));
			Covert(TEXT("Zarya"));
			const bool bC = Sim->GetTotalSolid(FName("Ice")) > IceB + 0.01;
			const bool bX = Sim->GetRivalRelation(FName("Zarya")) < RelB - 0.01;
			Pattern.AppendChar(bC ? TEXT('C') : (bX ? TEXT('X') : TEXT('?')));
		}
		UE_LOG(LogRedHopeSim, Display, TEXT("COVERT sequence: %s (expect CXCX - content-hash deterministic, same on every machine) axis=%.0f (expect -24)"),
			*Pattern, Sim->GetHumanNatureAxis());

		// 3) Fair trade nudges the axis the OTHER way (+2 on a completed convoy).
		const double AxisBeforeTrade = Sim->GetHumanNatureAxis();
		Sim->AddStock(FName("Hydrogen"), 40.0);
		FRHCommand Cv; Cv.Verb = FName("Convoy"); Cv.Target = FName("Zarya");
		Sim->EnqueueCommand(Cv); RunSols(4.5); // dispatch + full round trip
		UE_LOG(LogRedHopeSim, Display, TEXT("COVERT fair-trade: axis %.0f -> %.0f (expect +2 - honest dealing lifts it)"),
			AxisBeforeTrade, Sim->GetHumanNatureAxis());

		// 4) Night lowers detection: report the day-vs-night detection at neutral
		// relation is deterministic and night is strictly lower (mul 0.4 < 1).
		// (Structural check: IsNight() flips with the clock.)
		Clock->Debug_SetSimSeconds(0.5 * URHSimClockSubsystem::SolLengthSimSeconds); // midday
		const bool bDay = !Sim->IsNight();
		Clock->Debug_SetSimSeconds(0.05 * URHSimClockSubsystem::SolLengthSimSeconds); // pre-dawn
		const bool bNight = Sim->IsNight();
		UE_LOG(LogRedHopeSim, Display, TEXT("COVERT day/night: midday-is-day=%d predawn-is-night=%d (expect 1, 1)"), (int32)bDay, (int32)bNight);

		// 5) Save v20 round-trip: axis + hidden + attempt seed survive (so future
		// rolls stay deterministic across a reload).
		const double AxB = Sim->GetHumanNatureAxis(), HidB = Sim->GetHiddenTension(FName("Zarya"));
		FString Err;
		Sim->SaveColony(TEXT("coverttest"), Err);
		Sim->LoadColony(TEXT("coverttest"), Err);
		UE_LOG(LogRedHopeSim, Display, TEXT("COVERT save/load v20: axis %.0f->%.0f hidden %.0f->%.0f (expect identical)"),
			AxB, Sim->GetHumanNatureAxis(), HidB, Sim->GetHiddenTension(FName("Zarya")));

		UE_LOG(LogRedHopeSim, Display, TEXT("=== COVERT LAYER TEST END ==="));
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return 0;
	}

	// M4 Gate B self-test: THE ESPIONAGE ECONOMY - laundering defuses hidden
	// tension; sabotage disrupts a rival's trade for a period (and refuses convoys
	// while down, recovers on the timer); discovery-on-scout unlocks a dormant
	// settlement. Save v21. `-espionage`.
	if (FParse::Param(*Params, TEXT("espionage")))
	{
		UE_LOG(LogRedHopeSim, Display, TEXT("=== ESPIONAGE ECONOMY TEST (M4 Gate B) ==="));
		const int32 StepsPerSolH = (int32)(URHSimClockSubsystem::SolLengthSimSeconds / URHSimClockSubsystem::EraStepSimSeconds);
		const auto RunSols = [&](double Sols)
		{
			for (int32 S = 0; S < (int32)(Sols * StepsPerSolH); ++S)
			{
				Clock->Debug_AdvanceSimSeconds(URHSimClockSubsystem::EraStepSimSeconds);
				Sim->EraStep(URHSimClockSubsystem::EraStepSimSeconds);
			}
		};
		const auto Verb = [&](const TCHAR* V, const TCHAR* Target)
		{
			FRHCommand Cmd; Cmd.Verb = FName(V); Cmd.Target = FName(Target);
			Sim->EnqueueCommand(Cmd); RunSols(0.1);
		};
		URHDefinitionsSubsystem* DefsSub = World->GetSubsystem<URHDefinitionsSubsystem>();
		FRHRivalRow Z;
		Z.DisplayName = TEXT("Zarya Station"); Z.Nation = TEXT("Zarya Consortium");
		Z.DistanceKm = 120.f; Z.ExportLot = TEXT("Ice:150"); Z.ImportLot = TEXT("Struct:100");
		Z.RelationStart = 40.f; Z.SliceActive = true;
		DefsSub->Debug_InjectRival(FName("Zarya"), Z);
		// A DORMANT settlement for the discovery test.
		FRHRivalRow M;
		M.DisplayName = TEXT("Meridian Base"); M.Nation = TEXT("Meridian Compact");
		M.DistanceKm = 200.f; M.ExportLot = TEXT("SpareParts:4"); M.ImportLot = TEXT("Food:60");
		M.RelationStart = 30.f; M.SliceActive = false; // dormant until scouted
		DefsSub->Debug_InjectRival(FName("Meridian"), M);
		Clock->Debug_SetSimSeconds(0.9 * URHSimClockSubsystem::SolLengthSimSeconds); // night

		// 1) LAUNDER: build hidden tension via two covert ops (CXCX: op1 clean +8,
		// op2 caught +25 => 33), then launder to see the full -20 drop unclamped.
		Verb(TEXT("Covert"), TEXT("Zarya"));
		Verb(TEXT("Covert"), TEXT("Zarya"));
		const double Hid0 = Sim->GetHiddenTension(FName("Zarya"));
		const double HN0 = Sim->GetHumanNatureAxis();
		Sim->AddStock(FName("Struct"), 200.0); // the peace offering (ImportLot Struct:100)
		Verb(TEXT("Launder"), TEXT("Zarya"));
		UE_LOG(LogRedHopeSim, Display, TEXT("ESP launder: hidden %.0f -> %.0f (expect -20 = 33->13) humanNature %.0f -> %.0f (expect +3)"),
			Hid0, Sim->GetHiddenTension(FName("Zarya")), HN0, Sim->GetHumanNatureAxis());

		// 2) SABOTAGE: disrupt Zarya; while down, a convoy dispatch is refused.
		Verb(TEXT("Sabotage"), TEXT("Zarya"));
		Sim->AddStock(FName("Hydrogen"), 40.0);
		FRHCommand Cv; Cv.Verb = FName("Convoy"); Cv.Target = FName("Zarya");
		Sim->EnqueueCommand(Cv); RunSols(0.1);
		UE_LOG(LogRedHopeSim, Display, TEXT("ESP sabotage: remaining=%.1f sols convoy=%s (expect ~6, idle - trade refused while down)"),
			Sim->GetSabotageRemaining(FName("Zarya")),
			Sim->GetConvoyRival().IsNone() ? TEXT("idle") : *Sim->GetConvoyRival().ToString());

		// 3) RECOVERY: after the duration the rival is back; convoy now dispatches.
		RunSols(6.5);
		Sim->AddStock(FName("Hydrogen"), 40.0);
		Sim->EnqueueCommand(Cv); RunSols(0.1);
		UE_LOG(LogRedHopeSim, Display, TEXT("ESP recovery: remaining=%.1f convoy=%s (expect 0, Zarya - trade resumes)"),
			Sim->GetSabotageRemaining(FName("Zarya")),
			Sim->GetConvoyRival().IsNone() ? TEXT("idle") : *Sim->GetConvoyRival().ToString());
		RunSols(4.5); // let it come home so the convoy is idle for the next steps

		// 4) DISCOVERY: Meridian ships dormant + unavailable; the survey discovery
		// roll uncovers it deterministically. Drive the roll directly (surveys
		// complete in the agent band, which era-mode RunSols doesn't drive).
		const bool bBeforeAvail = Sim->IsRivalAvailable(FName("Meridian"));
		for (int32 i = 1; i <= 8 && !Sim->IsRivalDiscovered(FName("Meridian")); ++i)
		{
			Sim->Debug_MaybeDiscoverSettlement(i);
		}
		UE_LOG(LogRedHopeSim, Display, TEXT("ESP discovery: Meridian before-available=%d now discovered=%d available=%d (expect 0, 1, 1)"),
			(int32)bBeforeAvail, (int32)Sim->IsRivalDiscovered(FName("Meridian")), (int32)Sim->IsRivalAvailable(FName("Meridian")));

		// 5) Save v21 round-trip: sabotage timers + discovered set survive.
		Verb(TEXT("Sabotage"), TEXT("Zarya")); // put a timer on the books
		const double SabB = Sim->GetSabotageRemaining(FName("Zarya"));
		const bool DiscB = Sim->IsRivalDiscovered(FName("Meridian"));
		FString Err;
		Sim->SaveColony(TEXT("esptest"), Err);
		Sim->LoadColony(TEXT("esptest"), Err);
		UE_LOG(LogRedHopeSim, Display, TEXT("ESP save/load v21: sabotage %.1f->%.1f discovered %d->%d (expect identical)"),
			SabB, Sim->GetSabotageRemaining(FName("Zarya")), (int32)DiscB, (int32)Sim->IsRivalDiscovered(FName("Meridian")));

		UE_LOG(LogRedHopeSim, Display, TEXT("=== ESPIONAGE ECONOMY TEST END ==="));
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return 0;
	}

	// M2 Gate D+ self-test: the WATER LOOP - colonist draw + greywater reclaim,
	// potability decay vs fresh ice-melt restore (linear/parity-safe), the Hope
	// penalty below the floor, thirst as a support-contract fail, save v14. `-water`.
	if (FParse::Param(*Params, TEXT("water")))
	{
		UE_LOG(LogRedHopeSim, Display, TEXT("=== WATER LOOP TEST (M2 Gate D+) ==="));
		const int32 StepsPerSolH = (int32)(URHSimClockSubsystem::SolLengthSimSeconds / URHSimClockSubsystem::EraStepSimSeconds);
		const auto RunSols = [&](double Sols)
		{
			for (int32 S = 0; S < (int32)(Sols * StepsPerSolH); ++S)
			{
				Clock->Debug_AdvanceSimSeconds(URHSimClockSubsystem::EraStepSimSeconds);
				Sim->EraStep(URHSimClockSubsystem::EraStepSimSeconds);
			}
		};
		const auto Supported = [&]{ int32 N=0; for (const FRHColonist& C : Sim->GetColonists()) { N += C.bSupported ? 1 : 0; } return N; };

		// Certify a 4-cell vault, house 4, stock deep O2/Food so only WATER is
		// ever the marginal need.
		FString R;
		Sim->ExtendShaft(1, FVector(1000.f, 1000.f, 0.f));
		Sim->ExcavateFloor(-1, 4, R);
		Sim->Debug_PlaceInstant(FName("SolarArray"), FVector(3500.f, 1000.f, 0.f));
		Sim->Debug_PlaceInstant(FName("BatteryBank"), FVector(1000.f, 3500.f, 0.f));
		Sim->Debug_PlaceInstant(FName("AirFilter"), FVector(1000.f, 1500.f, 0.f), -1);
		Sim->AddStock(FName("Oxygen"), 3000.0);
		Sim->AddStock(FName("Food"), 800.0);
		Sim->AddStock(FName("Water"), 500.0);
		RunSols(2.5);
		Sim->Debug_AddColonists(4);
		RunSols(0.1);

		// 1) Draw + greywater reclaim: net loss is draw x (1 - returnFraction).
		const double W0 = Sim->GetStock(FName("Water"));
		RunSols(1.0);
		const double NetDraw = W0 - Sim->GetStock(FName("Water"));
		UE_LOG(LogRedHopeSim, Display, TEXT("WATER draw: net %.3f kg/sol (expect 1.200 = 4 x 2.0 x 0.15) supported=%d"),
			NetDraw, Supported());

		// 2) Potability decay (no fresh water): linear at DecayPerSol.
		const double P0 = Sim->GetWaterPotability();
		RunSols(2.0);
		const double P1 = Sim->GetWaterPotability();
		UE_LOG(LogRedHopeSim, Display, TEXT("WATER potability decay: %.3f -> %.3f (expect drop 0.160 = 0.08 x 2)"), P0, P1);

		// 3) Below the floor -> a Hope water penalty appears (first scarcity input).
		RunSols(4.0);
		const auto H = Sim->GetColonyHope();
		const double PLow = Sim->GetWaterPotability();
		const double PenExpected = PLow < 0.6 ? 12.0 * (0.6 - PLow) / 0.6 : 0.0;
		UE_LOG(LogRedHopeSim, Display, TEXT("WATER penalty: potability %.3f hopeWaterPenalty %.3f (expect %.3f, potability<0.6)"),
			PLow, H.WaterPenalty, PenExpected);

		// 4) Fresh ice-melt makeup restores potability (linear per kg).
		const double PBefore = Sim->GetWaterPotability();
		Sim->Debug_AddFreshWater(250.0); // +0.20 potability, minus one-step decay
		RunSols(0.05);
		UE_LOG(LogRedHopeSim, Display, TEXT("WATER fresh restore: %.3f -> %.3f (expect +~0.196 = 250 x 0.0008 - 1 step decay)"),
			PBefore, Sim->GetWaterPotability());

		// 5) Save v14 round-trip: potability survives exactly.
		const double PSave = Sim->GetWaterPotability();
		FString Err;
		Sim->SaveColony(TEXT("watertest"), Err);
		Sim->LoadColony(TEXT("watertest"), Err);
		UE_LOG(LogRedHopeSim, Display, TEXT("WATER save/load v14: potability %.4f -> %.4f (expect identical)"),
			PSave, Sim->GetWaterPotability());

		// 6) Thirst is fatal: empty the tanks -> unsupported -> evacuated.
		Sim->AddStock(FName("Water"), -Sim->GetStock(FName("Water")));
		RunSols(0.5);
		UE_LOG(LogRedHopeSim, Display, TEXT("WATER dry 0.5 sol: unsupported=%d (expect 4 - no water)"), 4 - Supported());
		RunSols(2.0);
		UE_LOG(LogRedHopeSim, Display, TEXT("WATER dry 2.5 sols: pop=%d (expect 0 - evacuated for thirst)"), Sim->GetPopulation());

		UE_LOG(LogRedHopeSim, Display, TEXT("=== WATER LOOP TEST END ==="));
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
		// DT_Rooms is synced (Gate C): Garden + Workstation ship SliceActive=true.
		// Assert against the real asset - pure-data verifier, fails on DT/CSV drift.
		URHDefinitionsSubsystem* DefsSub = World->GetSubsystem<URHDefinitionsSubsystem>();
		for (const TCHAR* RowName : { TEXT("Garden"), TEXT("Workstation") })
		{
			const FRHRoomRow* Row = DefsSub ? DefsSub->GetRoom(FName(RowName)) : nullptr;
			if (!Row || !Row->SliceActive)
			{
				UE_LOG(LogRedHopeSim, Error, TEXT("GARDEN: DT_Rooms row '%s' missing or not SliceActive - DT/CSV out of sync"), RowName);
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
