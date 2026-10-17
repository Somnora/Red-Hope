#include "RHSimCommandlet.h"
#include "RedHopeSim.h"
#include "RHSimClockSubsystem.h"
#include "RHSimWorldSubsystem.h"
#include "RHDefinitionsSubsystem.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

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
		// Pure-data: the Greenhouse row rides the synced DT (2026-07-16).
		{
			const FRHRoomRow* GH = DefsSub->GetRoom(FName("Greenhouse"));
			if (!GH || !GH->SliceActive || GH->EmitsTags != TEXT("Odor") || GH->MoraleWeight != 0.5f)
			{ UE_LOG(LogRedHopeSim, Error, TEXT("FORK: Greenhouse row missing/drifted - DT/CSV drift")); return 1; }
		}

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
		// Pure-data: the four authored rows ride the synced DT_Discoveries
		// (2026-07-16) - assert the arithmetic-bearing columns instead of
		// injecting, so this suite fails loudly on DT/CSV drift.
		URHDefinitionsSubsystem* DefsSub = World->GetSubsystem<URHDefinitionsSubsystem>();
		const auto CheckDisc = [&](const TCHAR* Name, int32 Order, float SeatH, float Bonus, FName Reward, float Kg) -> bool
		{
			const FRHDiscoveryRow* Row = DefsSub->GetDiscovery(FName(Name));
			if (!Row || !Row->SliceActive || Row->Order != Order || Row->LabSeatHours != SeatH
				|| Row->HopeBonus != Bonus || Row->RewardResource != Reward || Row->RewardKg != Kg)
			{
				UE_LOG(LogRedHopeSim, Error, TEXT("DISCOVERY: row %s missing/drifted - DT/CSV drift"), Name);
				return false;
			}
			return true;
		};
		if (!CheckDisc(TEXT("CropStrain"), 1, 100.f, 2.f, FName("Seeds"), 50.f)) { return 1; }
		if (!CheckDisc(TEXT("RegolithCeramics"), 2, 150.f, 2.f, FName("Struct"), 100.f)) { return 1; }
		if (!CheckDisc(TEXT("SubsurfaceBrine"), 3, 200.f, 3.f, FName("Water"), 150.f)) { return 1; }
		if (!CheckDisc(TEXT("MicrobialLife"), 4, 300.f, 8.f, NAME_None, 0.f)) { return 1; }

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

		// 3) 4 more sols: the lab crew's MASTERY has been ramping since they
		// took their posts (alive pass - skill accrues with job time), so the
		// accrual rate runs well above the fresh-crew 48/sol by now: ceramics
		// (150) AND the brine seep (200) both pop by sol 14. Struct +100
		// exactly (ceramics' reward; brine rewards Water).
		const double Struct0 = Sim->GetStock(FName("Struct"));
		RunSols(4.0);
		UE_LOG(LogRedHopeSim, Display, TEXT("DISC second: found=%d next=%s struct +%.0f milestones=%.1f (expect 3, MicrobialLife, 100, 12.0)"),
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
		// Zarya rides the synced DT_Rivals; Greed below stays a synthetic inject.
		URHDefinitionsSubsystem* DefsSub = World->GetSubsystem<URHDefinitionsSubsystem>();
		// Pure-data: Zarya rides the synced DT_Rivals (2026-07-16) - assert the
		// arithmetic-bearing columns so any DT/CSV drift fails loudly.
		const FRHRivalRow* ZRow = DefsSub->GetRival(FName("Zarya"));
		if (!ZRow || !ZRow->SliceActive || ZRow->DistanceKm != 120.f || ZRow->ExportLot != TEXT("Ice:150")
			|| ZRow->ImportLot != TEXT("Struct:100") || ZRow->RelationStart != 40.f)
		{ UE_LOG(LogRedHopeSim, Error, TEXT("Zarya row missing/drifted - DT/CSV drift")); return 1; }

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
		FRHRivalRow Greed = *ZRow; Greed.DisplayName = TEXT("Greedy Post");
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
		// Pure-data: Zarya rides the synced DT_Rivals (2026-07-16) - assert the
		// arithmetic-bearing columns so any DT/CSV drift fails loudly.
		const FRHRivalRow* ZRow = DefsSub->GetRival(FName("Zarya"));
		if (!ZRow || !ZRow->SliceActive || ZRow->DistanceKm != 120.f || ZRow->ExportLot != TEXT("Ice:150")
			|| ZRow->ImportLot != TEXT("Struct:100") || ZRow->RelationStart != 40.f)
		{ UE_LOG(LogRedHopeSim, Error, TEXT("Zarya row missing/drifted - DT/CSV drift")); return 1; }

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
		// Pure-data: Zarya rides the synced DT_Rivals (2026-07-16) - assert the
		// arithmetic-bearing columns so any DT/CSV drift fails loudly.
		const FRHRivalRow* ZRow = DefsSub->GetRival(FName("Zarya"));
		if (!ZRow || !ZRow->SliceActive || ZRow->DistanceKm != 120.f || ZRow->ExportLot != TEXT("Ice:150")
			|| ZRow->ImportLot != TEXT("Struct:100") || ZRow->RelationStart != 40.f)
		{ UE_LOG(LogRedHopeSim, Error, TEXT("Zarya row missing/drifted - DT/CSV drift")); return 1; }

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
		// Pure-data: Zarya rides the synced DT_Rivals (2026-07-16) - assert the
		// arithmetic-bearing columns so any DT/CSV drift fails loudly.
		const FRHRivalRow* ZRow = DefsSub->GetRival(FName("Zarya"));
		if (!ZRow || !ZRow->SliceActive || ZRow->DistanceKm != 120.f || ZRow->ExportLot != TEXT("Ice:150")
			|| ZRow->ImportLot != TEXT("Struct:100") || ZRow->RelationStart != 40.f)
		{ UE_LOG(LogRedHopeSim, Error, TEXT("Zarya row missing/drifted - DT/CSV drift")); return 1; }
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
		// Pure-data: Zarya rides the synced DT_Rivals (2026-07-16) - assert the
		// arithmetic-bearing columns so any DT/CSV drift fails loudly.
		const FRHRivalRow* ZRow = DefsSub->GetRival(FName("Zarya"));
		if (!ZRow || !ZRow->SliceActive || ZRow->DistanceKm != 120.f || ZRow->ExportLot != TEXT("Ice:150")
			|| ZRow->ImportLot != TEXT("Struct:100") || ZRow->RelationStart != 40.f)
		{ UE_LOG(LogRedHopeSim, Error, TEXT("Zarya row missing/drifted - DT/CSV drift")); return 1; }
		// The DORMANT settlement for the discovery test rides the synced DT too.
		{
			const FRHRivalRow* MRow = DefsSub->GetRival(FName("Meridian"));
			if (!MRow || MRow->SliceActive || MRow->DistanceKm != 200.f)
			{ UE_LOG(LogRedHopeSim, Error, TEXT("Meridian row missing/drifted (must exist DORMANT) - DT/CSV drift")); return 1; }
		}
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

	// M4 Gate C self-test: EARTH'S PRE-EMPTIVE PRESSURE - influence accrues,
	// a high-tension embargo lands (trade refused), pacify lifts it (spends
	// influence, eases tension), and an un-pacified embargo hardens to defection.
	// Save v22. `-preemptive`.
	if (FParse::Param(*Params, TEXT("preemptive")))
	{
		UE_LOG(LogRedHopeSim, Display, TEXT("=== EARTH PRE-EMPTIVE TEST (M4 Gate C) ==="));
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
		// Pure-data: Zarya rides the synced DT_Rivals (2026-07-16) - assert the
		// arithmetic-bearing columns so any DT/CSV drift fails loudly.
		const FRHRivalRow* ZRow = DefsSub->GetRival(FName("Zarya"));
		if (!ZRow || !ZRow->SliceActive || ZRow->DistanceKm != 120.f || ZRow->ExportLot != TEXT("Ice:150")
			|| ZRow->ImportLot != TEXT("Struct:100") || ZRow->RelationStart != 40.f)
		{ UE_LOG(LogRedHopeSim, Error, TEXT("Zarya row missing/drifted - DT/CSV drift")); return 1; }

		// 1) INFLUENCE accrues: a completed honest trade (+3) plus positive-
		// HumanNature drift. Do a trade to bank some influence + push HumanNature +.
		Sim->AddStock(FName("Hydrogen"), 40.0);
		FRHCommand Cv; Cv.Verb = FName("Convoy"); Cv.Target = FName("Zarya");
		Sim->EnqueueCommand(Cv); RunSols(4.5); // round trip -> +InfluencePerTrade, +HumanNature
		UE_LOG(LogRedHopeSim, Display, TEXT("PRE influence: %.1f (expect >=3 - one trade + drift) humanNature %.0f (expect +2)"),
			Sim->GetInfluence(), Sim->GetHumanNatureAxis());

		// 2) EMBARGO: push Earth tension just past the pre-emptive threshold (75);
		// the next step Earth turns Zarya against you -> embargoed, trade refused.
		// (Tension ~78 so a later pacify's -20 relief drops it below 75, avoiding
		// an instant re-embargo.)
		Sim->Debug_AddTension(75.0); // ~3 drift + 75 = ~78
		RunSols(0.2);
		const FName Embargoing = Sim->GetEmbargoingRival();
		Sim->AddStock(FName("Hydrogen"), 40.0);
		Sim->EnqueueCommand(Cv); RunSols(0.1); // try to trade with the embargoer
		UE_LOG(LogRedHopeSim, Display, TEXT("PRE embargo: embargoing=%s grace=%.1f convoy=%s (expect Zarya, ~8, idle - trade refused)"),
			Embargoing.IsNone() ? TEXT("none") : *Embargoing.ToString(),
			Sim->GetEmbargoGrace(FName("Zarya")),
			Sim->GetConvoyRival().IsNone() ? TEXT("idle") : *Sim->GetConvoyRival().ToString());

		// 3) PACIFY: enough influence lifts the embargo (spends PacifyInfluenceCost,
		// eases tension, +HumanNature).
		Sim->Debug_AddInfluence(30.0);
		const double InfBefore = Sim->GetInfluence();
		const double TenBefore = Sim->GetEarthTension();
		Verb(TEXT("Pacify"), TEXT("Zarya"));
		UE_LOG(LogRedHopeSim, Display, TEXT("PRE pacify: embargoed=%d influence %.1f->%.1f (expect -20) tension %.0f->%.0f (expect -20)"),
			(int32)Sim->IsEmbargoed(FName("Zarya")), InfBefore, Sim->GetInfluence(), TenBefore, Sim->GetEarthTension());

		// 4) SOURED CONVOY + DEFECTION: dispatch a convoy, then let Earth
		// re-embargo Zarya while the rover is OUT - the cargo still lands, but
		// the soured relationship earns no relation warming and no Influence
		// (adversarial-review fix: an in-flight convoy must not warm a hostile
		// rival or mint Influence). The un-pacified embargo then hardens into
		// defection.
		Sim->AddStock(FName("Hydrogen"), 40.0);
		Sim->EnqueueCommand(Cv); RunSols(0.1); // rover rolls out to Zarya
		Sim->Debug_AddTension(80.0);
		RunSols(0.1); // re-embargo fires while the rover is on the road
		UE_LOG(LogRedHopeSim, Display, TEXT("PRE re-embargo: embargoed=%d convoy=%s (expect 1, Zarya - out when it fired)"),
			(int32)Sim->IsEmbargoed(FName("Zarya")),
			Sim->GetConvoyRival().IsNone() ? TEXT("idle") : *Sim->GetConvoyRival().ToString());
		const double RelSour = Sim->GetRivalRelation(FName("Zarya"));
		const double InfSour = Sim->GetInfluence();
		RunSols(4.4); // the rover comes home to a soured relationship
		UE_LOG(LogRedHopeSim, Display, TEXT("PRE soured convoy: relation %.0f->%.0f (expect unchanged) influence +%.2f (expect <1 - drift only, NO +3 trade grant)"),
			RelSour, Sim->GetRivalRelation(FName("Zarya")), Sim->GetInfluence() - InfSour);
		RunSols(4.5); // let the grace (8 sols) expire un-pacified
		UE_LOG(LogRedHopeSim, Display, TEXT("PRE defection: defected=%d embargoed=%d relation=%.0f (expect 1, 0, <=5)"),
			(int32)Sim->HasDefected(FName("Zarya")), (int32)Sim->IsEmbargoed(FName("Zarya")), Sim->GetRivalRelation(FName("Zarya")));

		// 5) Save v22 round-trip: defection + influence survive.
		const bool DefB = Sim->HasDefected(FName("Zarya"));
		const double InfB = Sim->GetInfluence();
		FString Err;
		Sim->SaveColony(TEXT("pretest"), Err);
		Sim->LoadColony(TEXT("pretest"), Err);
		UE_LOG(LogRedHopeSim, Display, TEXT("PRE save/load v22: defected %d->%d influence %.1f->%.1f (expect identical)"),
			(int32)DefB, (int32)Sim->HasDefected(FName("Zarya")), InfB, Sim->GetInfluence());

		UE_LOG(LogRedHopeSim, Display, TEXT("=== EARTH PRE-EMPTIVE TEST END ==="));
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return 0;
	}

	// M4 Gate D self-test: DYNAMIC CRISES + ENDINGS - the alignment selector
	// (Destructive -> Malfunction, Evolved -> Environmental), the crisis effects
	// (work-tempo + Hope + Water), recovery, and the endings read across the two
	// axes. Save v23. `-crisis`.
	if (FParse::Param(*Params, TEXT("crisis")))
	{
		UE_LOG(LogRedHopeSim, Display, TEXT("=== CRISES & ENDINGS TEST (M4 Gate D) ==="));
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
		// Pure-data: Zarya rides the synced DT_Rivals (2026-07-16) - assert the
		// arithmetic-bearing columns so any DT/CSV drift fails loudly.
		const FRHRivalRow* ZRow = DefsSub->GetRival(FName("Zarya"));
		if (!ZRow || !ZRow->SliceActive || ZRow->DistanceKm != 120.f || ZRow->ExportLot != TEXT("Ice:150")
			|| ZRow->ImportLot != TEXT("Struct:100") || ZRow->RelationStart != 40.f)
		{ UE_LOG(LogRedHopeSim, Error, TEXT("Zarya row missing/drifted - DT/CSV drift")); return 1; }
		const FRHRoomRow* LQ = DefsSub->GetRoom(FName("LivingQuarters"));
		if (!LQ || !LQ->SliceActive) { UE_LOG(LogRedHopeSim, Error, TEXT("CRISIS: LivingQuarters not active")); return 1; }

		// A housed, stocked colony (2 crew) so crisis morale + tempo effects read.
		FString R;
		Sim->ExtendShaft(1, FVector(1000.f, 1000.f, 0.f));
		Sim->ExcavateFloor(-1, 6, R);
		Sim->Debug_PlaceInstant(FName("SolarArray"), FVector(3500.f, 1000.f, 0.f));
		Sim->Debug_PlaceInstant(FName("BatteryBank"), FVector(1000.f, 3500.f, 0.f));
		Sim->Debug_PlaceInstant(FName("AirFilter"), FVector(1000.f, 1500.f, 0.f), -1);
		Sim->AddStock(FName("Oxygen"), 4000.0);
		Sim->AddStock(FName("Water"), 4000.0);
		Sim->AddStock(FName("Food"), 4000.0);
		RunSols(2.5);
		Sim->Debug_AddColonists(2);
		Sim->DesignateRoom(-1, 0, FName("LivingQuarters"), R);
		Sim->DesignateRoom(-1, 1, FName("LivingQuarters"), R);
		RunSols(0.2);

		// 1) SELECTOR - DESTRUCTIVE colony draws a MALFUNCTION. Drive HumanNature
		// negative, force a crisis, confirm the type + its work-tempo penalty.
		Sim->Debug_ShiftIdentity(0); // no-op clarity
		for (int32 i = 0; i < 10; ++i) { Sim->Debug_ShiftHumanNature(-10); } // -> -100 (clamped)
		const double TempoBefore = Sim->GetHumanWorkTempo();
		const double HopeBefore = Sim->GetColonyHope().Total;
		Sim->Debug_TriggerCrisis();
		UE_LOG(LogRedHopeSim, Display, TEXT("CRISIS destructive: type=%s tempo %.3f->%.3f (expect x0.6) hope %.1f->%.1f (expect -12)"),
			Sim->GetActiveCrisisName(), TempoBefore, Sim->GetHumanWorkTempo(), HopeBefore, Sim->GetColonyHope().Total);

		// 2) RECOVERY: after CrisisDurationSols the malfunction clears; the 0.6x
		// penalty is gone (tempo >= pre-crisis; Hope also rebounded during downtime).
		RunSols(5.5);
		UE_LOG(LogRedHopeSim, Display, TEXT("CRISIS recovery: active=%s tempo=%.3f (expect none, >= %.3f - penalty lifted)"),
			Sim->GetActiveCrisisName(), Sim->GetHumanWorkTempo(), TempoBefore);

		// 3) SELECTOR - EVOLVED colony draws an ENVIRONMENTAL test that drains
		// Water. Drive HumanNature positive, force a crisis, confirm Water drain.
		for (int32 i = 0; i < 20; ++i) { Sim->Debug_ShiftHumanNature(+10); } // -> +100
		Sim->Debug_TriggerCrisis();
		const double Water0 = Sim->GetStock(FName("Water"));
		RunSols(2.0);
		UE_LOG(LogRedHopeSim, Display, TEXT("CRISIS evolved: type=%s water drained %.0f (expect ~17 = 16 crisis [8/sol x2] + ~1.2 colonist drink)"),
			Sim->GetActiveCrisisName(), Water0 - Sim->GetStock(FName("Water")));
		RunSols(4.0); // clear it

		// 4) ENDINGS across the two axes (pure read).
		Sim->Debug_ShiftIdentity(-200); // Earth-aligned
		const auto EJewel = Sim->GetProjectedEnding();
		Sim->Debug_ShiftIdentity(+200); // -> Martian (+100), HumanNature already +100 (Evolved)
		const auto EFed = Sim->GetProjectedEnding();
		for (int32 i = 0; i < 20; ++i) { Sim->Debug_ShiftHumanNature(-10); } // -> -100 Destructive, still Martian
		const auto EWar = Sim->GetProjectedEnding();
		UE_LOG(LogRedHopeSim, Display, TEXT("CRISIS endings: earth=%s martian-evolved=%s martian-destructive=%s (expect Corporate Jewel, Independent Mars Federation, Martian Cold War)"),
			Sim->GetEndingName(EJewel), Sim->GetEndingName(EFed), Sim->GetEndingName(EWar));

		// 5) Save v23 round-trip: crisis + ending milestone survive.
		Sim->Debug_TriggerCrisis();
		const int32 CrisisB = (int32)Sim->GetActiveCrisis();
		FString Err;
		Sim->SaveColony(TEXT("crisistest"), Err);
		Sim->LoadColony(TEXT("crisistest"), Err);
		UE_LOG(LogRedHopeSim, Display, TEXT("CRISIS save/load v23: active %d->%d ending=%s (expect identical)"),
			CrisisB, (int32)Sim->GetActiveCrisis(), Sim->GetEndingName(Sim->GetProjectedEnding()));

		UE_LOG(LogRedHopeSim, Display, TEXT("=== CRISES & ENDINGS TEST END ==="));
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return 0;
	}

	// v25 self-test: THE GOAL LADDER - quotas advance when the ship lands
	// (deadlines relative to opening), research funding pays out on the next
	// award, the weather-cycle fold, save v25 round-trip mid-ladder. `-ladder`.
	if (FParse::Param(*Params, TEXT("ladder")))
	{
		UE_LOG(LogRedHopeSim, Display, TEXT("=== GOAL LADDER TEST (v25) ==="));
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
		// Q2 rides the ladder once its row is active (the DT flip is the
		// committed CSV's job; this knob bridges until the editor sync).
		DefsSub->Debug_ActivateQuota(FName("Q2"));

		// 1) Fresh colony opens on Q1 (smallest deadline among active rows).
		UE_LOG(LogRedHopeSim, Display, TEXT("LADDER open: active=%s phase=%d openedSol=%d (expect Q1, 0=Open, 0)"),
			*Sim->GetActiveQuotaName().ToString(), (int32)Sim->GetQuotaPhase(), Sim->GetQuotaOpenedSol());

		// 2) Meet Q1 (Struct:300;Water:200;Oxygen:50) -> AwaitingManifest,
		// on-time award 10000 (no rivals: requisition x1, no funding pending).
		Sim->AddStock(FName("Struct"), 300.0);
		Sim->AddStock(FName("Water"), 200.0);
		Sim->AddStock(FName("Oxygen"), 50.0);
		RunSols(0.2);
		UE_LOG(LogRedHopeSim, Display, TEXT("LADDER Q1 met: phase=%d award=%.0f (expect 1=AwaitingManifest, 10000)"),
			(int32)Sim->GetQuotaPhase(), Sim->GetAwardMassKg());

		// 3) Launch (empty manifest is a legal choice) -> ship lands ~3 sols
		// -> Q1 Completed -> THE LADDER ADVANCES: Q2 opens at the landing sol.
		FString Err;
		Sim->LaunchShip(Err);
		RunSols(3.3);
		UE_LOG(LogRedHopeSim, Display, TEXT("LADDER advance: active=%s phase=%d openedSol=%d (expect Q2, 0=Open, 3)"),
			*Sim->GetActiveQuotaName().ToString(), (int32)Sim->GetQuotaPhase(), Sim->GetQuotaOpenedSol());

		// 4) Research pays: bank 500 kg, meet Q2 (Struct:600;Oxygen:150;
		// Hydrogen:40) inside its RELATIVE deadline -> award 12000 + 500.
		Sim->Debug_AddResearchFunding(500.0);
		Sim->AddStock(FName("Struct"), 600.0);
		Sim->AddStock(FName("Oxygen"), 150.0);
		Sim->AddStock(FName("Hydrogen"), 40.0);
		RunSols(0.2);
		UE_LOG(LogRedHopeSim, Display, TEXT("LADDER Q2 met: award=%.0f pending=%.0f (expect 12500, 0)"),
			Sim->GetAwardMassKg(), Sim->GetPendingResearchFundingKg());

		// 5) Save v25 round-trip mid-ladder: active quota + phase + award survive.
		const FName ActiveB = Sim->GetActiveQuotaName();
		const double AwardB = Sim->GetAwardMassKg();
		Sim->SaveColony(TEXT("laddertest"), Err);
		Sim->LoadColony(TEXT("laddertest"), Err);
		UE_LOG(LogRedHopeSim, Display, TEXT("LADDER save/load v25: active %s->%s award %.0f->%.0f phase=%d (expect identical, 1)"),
			*ActiveB.ToString(), *Sim->GetActiveQuotaName().ToString(), AwardB, Sim->GetAwardMassKg(), (int32)Sim->GetQuotaPhase());

		// 6) The weather-cycle fold (pure function probes; knob off = passthrough).
		const double FoldOff = Sim->FoldEventSol(33.0);
		Sim->Debug_SetWeatherCycleSols(20.0);
		UE_LOG(LogRedHopeSim, Display, TEXT("LADDER weather fold: off=%.1f in-window=%.1f folded=%.1f onset=%.1f (expect 33.0, 10.0, 13.0, 12.0)"),
			FoldOff, Sim->FoldEventSol(10.0), Sim->FoldEventSol(33.0), Sim->FoldEventSol(52.0));

		UE_LOG(LogRedHopeSim, Display, TEXT("=== GOAL LADDER TEST END ==="));
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return 0;
	}

	// v25 self-test: ENDING RESOLUTION - the declared path's epilogue after a
	// sustained hold (wavering resets the clock), and the one-time Collapse
	// declaration when a colony that had people empties. `-ending`.
	if (FParse::Param(*Params, TEXT("ending")))
	{
		UE_LOG(LogRedHopeSim, Display, TEXT("=== ENDING RESOLUTION TEST (v25) ==="));
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
		// Pure-data: Zarya rides the synced DT_Rivals (2026-07-16) - assert the
		// arithmetic-bearing columns so any DT/CSV drift fails loudly.
		const FRHRivalRow* ZRow = DefsSub->GetRival(FName("Zarya"));
		if (!ZRow || !ZRow->SliceActive || ZRow->DistanceKm != 120.f || ZRow->ExportLot != TEXT("Ice:150")
			|| ZRow->ImportLot != TEXT("Struct:100") || ZRow->RelationStart != 40.f)
		{ UE_LOG(LogRedHopeSim, Error, TEXT("Zarya row missing/drifted - DT/CSV drift")); return 1; }

		// A housed colony (the endings need bEverHadCrew + a live Earth layer).
		FString R;
		Sim->ExtendShaft(1, FVector(1000.f, 1000.f, 0.f));
		Sim->ExcavateFloor(-1, 6, R);
		Sim->Debug_PlaceInstant(FName("SolarArray"), FVector(3500.f, 1000.f, 0.f));
		Sim->Debug_PlaceInstant(FName("BatteryBank"), FVector(1000.f, 3500.f, 0.f));
		Sim->Debug_PlaceInstant(FName("AirFilter"), FVector(1000.f, 1500.f, 0.f), -1);
		Sim->AddStock(FName("Oxygen"), 4000.0);
		Sim->AddStock(FName("Water"), 4000.0);
		Sim->AddStock(FName("Food"), 4000.0);
		RunSols(2.5);
		Sim->Debug_AddColonists(2);
		Sim->DesignateRoom(-1, 0, FName("LivingQuarters"), R);
		Sim->DesignateRoom(-1, 1, FName("LivingQuarters"), R);

		// 1) Martian + Evolved -> Independent Mars Federation declares.
		Sim->Debug_SetEpilogueSustainSols(2.0);
		for (int32 i = 0; i < 20; ++i) { Sim->Debug_ShiftIdentity(+10); }
		for (int32 i = 0; i < 20; ++i) { Sim->Debug_ShiftHumanNature(+10); }
		RunSols(0.2);
		UE_LOG(LogRedHopeSim, Display, TEXT("ENDING declared: %d name=%s (expect 1, Independent Mars Federation)"),
			(int32)Sim->IsEndingDeclared(), Sim->GetEndingName(Sim->GetDeclaredEnding()));

		// 2) WAVER: 1 sol in, swing HumanNature Destructive - the projection
		// leaves the declared path, the epilogue clock resets to zero.
		RunSols(1.0);
		for (int32 i = 0; i < 20; ++i) { Sim->Debug_ShiftHumanNature(-10); }
		RunSols(0.5);
		UE_LOG(LogRedHopeSim, Display, TEXT("ENDING waver: sustain=%.2f epilogue=%d (expect 0.00, 0)"),
			Sim->GetPathSustainSols(), (int32)Sim->IsEpilogueDeclared());

		// 3) Re-earn the path: hold Federation 2 sols -> EPILOGUE fires once.
		for (int32 i = 0; i < 20; ++i) { Sim->Debug_ShiftHumanNature(+10); }
		RunSols(2.3);
		UE_LOG(LogRedHopeSim, Display, TEXT("ENDING epilogue: %d sustain=%.2f (expect 1, >=2.0)"),
			(int32)Sim->IsEpilogueDeclared(), Sim->GetPathSustainSols());

		// 4) Save v25 round-trip: declared + epilogue survive.
		FString Err;
		Sim->SaveColony(TEXT("endingtest"), Err);
		Sim->LoadColony(TEXT("endingtest"), Err);
		UE_LOG(LogRedHopeSim, Display, TEXT("ENDING save/load v25: declared=%s epilogue=%d (expect Independent Mars Federation, 1)"),
			Sim->GetEndingName(Sim->GetDeclaredEnding()), (int32)Sim->IsEpilogueDeclared());

		// 5) COLLAPSE: strip the stores; the crew go unsupported, evacuate at
		// the countdown, and the emptied colony declares Collapse ONCE -
		// abstract, with the recovery path named (Gate-D placeholder wording).
		Sim->AddStock(FName("Food"), -Sim->GetStock(FName("Food")));
		Sim->AddStock(FName("Water"), -Sim->GetStock(FName("Water")));
		Sim->AddStock(FName("Oxygen"), -Sim->GetStock(FName("Oxygen")));
		RunSols(3.5); // evac countdown (2 sols unsupported) + margin
		UE_LOG(LogRedHopeSim, Display, TEXT("ENDING collapse: pop=%d declared=%d projected=%s (expect 0, 1, Collapse)"),
			Sim->GetPopulation(), (int32)Sim->IsCollapseDeclared(),
			Sim->GetEndingName(Sim->GetProjectedEnding()));

		UE_LOG(LogRedHopeSim, Display, TEXT("=== ENDING RESOLUTION TEST END ==="));
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return 0;
	}

	// Staffing self-test (tiered production T1-T3, 2026-10-15): prefab station
	// modules - a building that counts as a room station (seats, yield, the
	// infirmary grace) - gated underground, on carved cells and by research.
	// Every check FAILS the suite (LogRedHopeSim Error + exit 1) on mismatch,
	// unlike the older print-only suites. `-staff`.
	if (FParse::Param(*Params, TEXT("staff")))
	{
		UE_LOG(LogRedHopeSim, Display, TEXT("=== STAFFING TEST (prefab stations, 2026-10-15) ==="));
		const int32 StepsPerSolH = (int32)(URHSimClockSubsystem::SolLengthSimSeconds / URHSimClockSubsystem::EraStepSimSeconds);
		const auto RunSols = [&](double Sols)
		{
			for (int32 S = 0; S < (int32)(Sols * StepsPerSolH); ++S)
			{
				Sim->Debug_AddFreshWater(500.0 * URHSimClockSubsystem::EraStepSimSeconds / (double)URHSimClockSubsystem::SolLengthSimSeconds);
				// Staffing is the subject, not the grid: a full bank every
				// step keeps night shedding out of the seat checks (StepPower
				// clamps it to capacity).
				Sim->Debug_SetBatteryWh(1.0e9);
				Clock->Debug_AdvanceSimSeconds(URHSimClockSubsystem::EraStepSimSeconds);
				Sim->EraStep(URHSimClockSubsystem::EraStepSimSeconds);
			}
		};
		int32 Fails = 0;
		const auto Expect = [&](bool bOk, const FString& What)
		{
			if (bOk)
			{
				UE_LOG(LogRedHopeSim, Display, TEXT("STAFF ok: %s"), *What);
			}
			else
			{
				UE_LOG(LogRedHopeSim, Error, TEXT("STAFF FAIL: %s"), *What);
				++Fails;
			}
		};
		URHDefinitionsSubsystem* DefsSub = World->GetSubsystem<URHDefinitionsSubsystem>();

		// 1) Pure data: the prefab rows ride the synced DT_Buildings.
		const FRHBuildingRow* LabMod = DefsSub->GetBuilding(FName("LabModule"));
		const FRHBuildingRow* InfMod = DefsSub->GetBuilding(FName("InfirmaryModule"));
		Expect(LabMod && LabMod->SliceActive && LabMod->ProvidesRoom == FName("LabFull"), TEXT("LabModule row provides LabFull (DT/CSV)"));
		Expect(InfMod && InfMod->SliceActive && InfMod->ProvidesRoom == FName("Infirmary"), TEXT("InfirmaryModule row provides Infirmary (DT/CSV)"));
		if (!LabMod || !InfMod)
		{
			return 1;
		}

		// A rated ten-cell floor with four colonists (the -tiers recipe).
		FString R;
		Sim->ExtendShaft(1, FVector(1000.f, 1000.f, 0.f));
		Sim->ExcavateFloor(-1, 10, R);
		Sim->Debug_PlaceInstant(FName("SolarArray"), FVector(3500.f, 1000.f, 0.f));
		Sim->Debug_PlaceInstant(FName("SolarArray"), FVector(3500.f, 2000.f, 0.f));
		Sim->Debug_PlaceInstant(FName("BatteryBank"), FVector(1000.f, 3500.f, 0.f));
		Sim->Debug_PlaceInstant(FName("AirFilter"), FVector(1000.f, 1500.f, 0.f), -1);
		Sim->AddStock(FName("Oxygen"), 6000.0);
		Sim->AddStock(FName("Water"), 4000.0);
		Sim->AddStock(FName("Food"), 6000.0);
		RunSols(2.5);
		Expect(Sim->IsFloorRated(-1), TEXT("floor -1 rated"));
		Sim->Debug_AddColonists(4);
		Sim->DesignateRoom(-1, 0, FName("LivingQuarters"), R);
		Sim->DesignateRoom(-1, 1, FName("LivingQuarters"), R);
		const FVector Head = Sim->GetShaftHeadCm();
		const auto CellCentre = [&](int32 i) { const FIntPoint P = URHSimWorldSubsystem::SpiralCell(i); return FVector(Head.X + P.X * 1000.0, Head.Y + P.Y * 1000.0, 0.0); };

		// 2) Placement gates.
		FString Why;
		Expect(!Sim->CanPlaceBuilding(FName("LabModule"), CellCentre(3), Why, 0), FString::Printf(TEXT("refused on the surface (%s)"), *Why));
		const FRHRoomRow* LabFull = DefsSub->GetRoom(FName("LabFull"));
		const bool bLabFullDormant = LabFull && !LabFull->SliceActive;
		Expect(bLabFullDormant, TEXT("LabFull starts dormant (research-locked)"));
		Expect(!Sim->CanPlaceBuilding(FName("LabModule"), CellCentre(3), Why, -1) && Why.StartsWith(TEXT("Locked")), FString::Printf(TEXT("refused while LabFull is dormant (%s)"), *Why));
		DefsSub->ActivateRoomRow(FName("LabFull"));
		Why.Reset();
		Expect(Sim->CanPlaceBuilding(FName("LabModule"), CellCentre(3), Why, -1), FString::Printf(TEXT("accepted on a carved cell once LabFull is live (%s)"), *Why));
		Expect(!Sim->CanPlaceBuilding(FName("LabModule"), Head + FVector(30000.0, 0.0, 0.0), Why, -1), FString::Printf(TEXT("refused off the carved cells (%s)"), *Why));
		Expect(!Sim->CanPlaceBuilding(FName("LabModule"), CellCentre(0), Why, -1), FString::Printf(TEXT("refused on a zoned cell (%s)"), *Why));

		// 3) Seats: four colonists, no room stations - the module seats two at
		// LabFull's x2.0.
		Sim->Debug_PlaceInstant(FName("LabModule"), CellCentre(3), -1);
		RunSols(0.2);
		Expect(Sim->GetColonyHope().FilledSeats == 2 && FMath::IsNearlyEqual(Sim->GetLabYieldSeatSum(), 4.0),
			FString::Printf(TEXT("LabModule seats 2 at x2.0: filled=%d labSum=%.2f (expect 2, 4.00)"), Sim->GetColonyHope().FilledSeats, Sim->GetLabYieldSeatSum()));
		int32 LabId = 0;
		for (const FRHBuildingInstance& B : Sim->GetBuildings()) { if (B.DefName == FName("LabModule")) { LabId = B.Id; } }
		Expect(!Sim->DesignateRoom(-1, 3, FName("Lab"), Why), FString::Printf(TEXT("zoning the module's cell is refused (%s)"), *Why));
		Expect(!Sim->CanPlaceBuilding(FName("LabModule"), CellCentre(3), Why, -1), FString::Printf(TEXT("a second module on the same cell is refused (%s)"), *Why));

		// 4) Switched off: no seats; back on: seats return. Two steps each (a
		// sol is 20 era steps): StepPower runs after StepPopulation, so a
		// module switched on reads unpowered for one step.
		Sim->SetManualPower(LabId, false);
		RunSols(0.1);
		Expect(FMath::IsNearlyEqual(Sim->GetLabYieldSeatSum(), 0.0), FString::Printf(TEXT("switched off: labSum=%.2f (expect 0)"), Sim->GetLabYieldSeatSum()));
		Sim->SetManualPower(LabId, true);
		RunSols(0.1);
		Expect(FMath::IsNearlyEqual(Sim->GetLabYieldSeatSum(), 4.0), FString::Printf(TEXT("back on: labSum=%.2f (expect 4.00)"), Sim->GetLabYieldSeatSum()));

		// 5) The Infirmary module stretches the recall window like the room.
		const double EvacBase = Sim->GetEffectiveEvacSols();
		Sim->Debug_PlaceInstant(FName("InfirmaryModule"), CellCentre(4), -1);
		RunSols(0.1);
		const double EvacMod = Sim->GetEffectiveEvacSols();
		Expect(EvacMod > EvacBase + 0.5, FString::Printf(TEXT("InfirmaryModule: evac %.1f -> %.1f sols (expect +1.0)"), EvacBase, EvacMod));

		// 6) Save/load: the modules and their effects survive.
		FString Err;
		Sim->SaveColony(TEXT("stafftest"), Err);
		Sim->LoadColony(TEXT("stafftest"), Err);
		RunSols(0.05);
		Expect(FMath::IsNearlyEqual(Sim->GetLabYieldSeatSum(), 4.0) && FMath::IsNearlyEqual(Sim->GetEffectiveEvacSols(), EvacMod),
			FString::Printf(TEXT("save/load: labSum=%.2f evac=%.1f"), Sim->GetLabYieldSeatSum(), Sim->GetEffectiveEvacSols()));

		// 7) T2 Assign: zone a Workstation (cell 5) and a WorkbenchLarge (cell
		// 6, 2 seats x1.35). Default order fills the cells first (ws=1+2.7),
		// leaving the module 1 of its 2 seats. Assign the module: it fills
		// first (lab=4.0), the cells take the rest in carve order (ws=1+1.35).
		Sim->DesignateRoom(-1, 5, FName("Workstation"), R);
		Sim->DesignateRoom(-1, 6, FName("WorkbenchLarge"), R);
		RunSols(0.1);
		Expect(FMath::IsNearlyEqual(Sim->GetWorkstationYieldSeatSum(), 3.70, 1e-4) && FMath::IsNearlyEqual(Sim->GetLabYieldSeatSum(), 2.0, 1e-4),
			FString::Printf(TEXT("default order: ws=%.2f lab=%.2f (expect 3.70, 2.00)"), Sim->GetWorkstationYieldSeatSum(), Sim->GetLabYieldSeatSum()));
		FRHCommand Assign; Assign.Verb = FName("Assign"); Assign.Target = FName("Building"); Assign.Value = LabId;
		Sim->EnqueueCommand(Assign);
		RunSols(0.15); // past the uplink lag (45 sim-s) and a power step
		Expect(FMath::IsNearlyEqual(Sim->GetLabYieldSeatSum(), 4.0, 1e-4) && FMath::IsNearlyEqual(Sim->GetWorkstationYieldSeatSum(), 2.35, 1e-4),
			FString::Printf(TEXT("module assigned first: lab=%.2f ws=%.2f (expect 4.00, 2.35)"), Sim->GetLabYieldSeatSum(), Sim->GetWorkstationYieldSeatSum()));
		FRHCommand AssignCell; AssignCell.Verb = FName("Assign"); AssignCell.Level = -1; AssignCell.Value = 6;
		Sim->EnqueueCommand(AssignCell);
		RunSols(0.15);
		Expect(Sim->GetSeatPriority().Num() == 2 && Sim->GetSeatPriority()[0] == FIntVector(-1, 6, 0),
			FString::Printf(TEXT("newest assignment first: %d entries, head (%d,%d,%d)"), Sim->GetSeatPriority().Num(),
				Sim->GetSeatPriority().Num() ? Sim->GetSeatPriority()[0].X : 0, Sim->GetSeatPriority().Num() ? Sim->GetSeatPriority()[0].Y : 0,
				Sim->GetSeatPriority().Num() ? Sim->GetSeatPriority()[0].Z : 0));
		FRHCommand Bad; Bad.Verb = FName("Assign"); Bad.Level = -1; Bad.Value = 0; // Living Quarters
		Sim->EnqueueCommand(Bad);
		RunSols(0.15);
		Expect(Sim->GetSeatPriority().Num() == 2, TEXT("Assign on Living Quarters is refused"));
		Sim->DesignateRoom(-1, 6, FName("Dining"), R);
		Expect(Sim->GetSeatPriority().Num() == 1, TEXT("re-zoning an assigned cell to Dining drops it from the order"));
		FString Err2;
		Sim->SaveColony(TEXT("stafftest2"), Err2);
		const TArray<FIntVector> PriorBefore = Sim->GetSeatPriority();
		Sim->LoadColony(TEXT("stafftest2"), Err2);
		Expect(Sim->GetSeatPriority() == PriorBefore, TEXT("save/load v27 keeps the seat order"));

		// 8) T3 lazy fill. Now: the module (assigned) seats colonists 1-2,
		// cell 5 seats 3, colonist 4 idles. Arm 1.0 sol, add two colonists,
		// then zone two more Workstations: the new seats wait, then are taken
		// one per sol.
		Sim->Debug_SetLazySeatFillSols(1.0);
		RunSols(0.1);
		const int32 Staffed0 = Sim->GetColonyHope().FilledSeats;
		Sim->Debug_AddColonists(2);
		Sim->DesignateRoom(-1, 7, FName("Workstation"), R);
		Sim->DesignateRoom(-1, 8, FName("Workstation"), R);
		RunSols(0.1);
		const int32 Staffed1 = Sim->GetColonyHope().FilledSeats;
		Expect(Staffed0 == 3 && Staffed1 == Staffed0, FString::Printf(TEXT("lazy: the new seats wait (filled %d -> %d, expect 3 -> 3)"), Staffed0, Staffed1));
		RunSols(1.0);
		const int32 Staffed2 = Sim->GetColonyHope().FilledSeats;
		Expect(Staffed2 == Staffed0 + 1, FString::Printf(TEXT("lazy: one takes charge after a sol (filled %d, expect %d)"), Staffed2, Staffed0 + 1));
		RunSols(1.0);
		Expect(Sim->GetColonyHope().FilledSeats == Staffed0 + 2, FString::Printf(TEXT("lazy: the second a sol later (filled %d, expect %d)"), Sim->GetColonyHope().FilledSeats, Staffed0 + 2));
		// Instant mode (the shipped default) seats everyone at once again.
		Sim->Debug_SetLazySeatFillSols(0.0);

		UE_LOG(LogRedHopeSim, Display, TEXT("=== STAFFING TEST END: %d failure(s) ==="), Fails);
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return Fails > 0 ? 1 : 0;
	}

	// Dome self-test (Agri Gate C, 2026-10-15): the drag-sized surface
	// greenhouse - placement and size gates, the bed-scaled bill, beds that
	// plant and grow like greenhouse cells by daylight with no grow-light
	// draw, and save/load of the size and the beds. Fails loudly. `-dome`.
	if (FParse::Param(*Params, TEXT("dome")))
	{
		UE_LOG(LogRedHopeSim, Display, TEXT("=== DOME TEST (Agri Gate C, 2026-10-15) ==="));
		int32 Fails = 0;
		const auto Expect = [&](bool bOk, const FString& What)
		{
			if (bOk) { UE_LOG(LogRedHopeSim, Display, TEXT("DOME ok: %s"), *What); }
			else { UE_LOG(LogRedHopeSim, Error, TEXT("DOME FAIL: %s"), *What); ++Fails; }
		};
		const auto Step = [&]()
		{
			Sim->Debug_SetBatteryWh(1.0e9);
			Clock->Debug_AdvanceSimSeconds(URHSimClockSubsystem::EraStepSimSeconds);
			Sim->EraStep(URHSimClockSubsystem::EraStepSimSeconds);
		};
		URHDefinitionsSubsystem* DefsSub = World->GetSubsystem<URHDefinitionsSubsystem>();

		// 1) Pure data.
		const FRHBuildingRow* DomeDef = DefsSub->GetBuilding(FName("GreenhouseDome"));
		Expect(DomeDef && DomeDef->SliceActive && DomeDef->DragSized && DomeDef->ProvidesRoom == FName("Greenhouse"),
			TEXT("GreenhouseDome row: active, drag-sized, provides Greenhouse (DT/CSV)"));
		if (!DomeDef)
		{
			return 1;
		}

		// 2) Placement and size gates. The Lander's coverage holds the spot.
		const FVector Spot(2500.f, 0.f, 0.f);
		FString Why;
		Expect(!Sim->CanPlaceBuildingSized(FName("GreenhouseDome"), Spot, Why, -1, 1, 1), FString::Printf(TEXT("refused underground (%s)"), *Why));
		const int32 Max = Sim->GetDomeMaxBedsPerSide();
		Expect(!Sim->CanPlaceBuildingSized(FName("GreenhouseDome"), Spot, Why, 0, Max + 1, 1), FString::Printf(TEXT("refused past %d beds a side (%s)"), Max, *Why));
		Why.Reset();
		Expect(Sim->CanPlaceBuildingSized(FName("GreenhouseDome"), Spot, Why, 0, 2, 2), FString::Printf(TEXT("2x2 accepted on the surface (%s)"), *Why));
		Expect(URHSimWorldSubsystem::DecodeBuildSize(URHSimWorldSubsystem::EncodeBuildSize(3, 2)) == FIntPoint(3, 2)
			&& URHSimWorldSubsystem::DecodeBuildSize(0.0) == FIntPoint(1, 1), TEXT("size round-trips through the order's Value"));

		// 3) A 2x2 order: the bill and the build time are four beds'.
		FRHCommand Order; Order.Verb = FName("Build"); Order.Target = FName("GreenhouseDome"); Order.Location = Spot; Order.Level = 0;
		Order.Value = URHSimWorldSubsystem::EncodeBuildSize(2, 2);
		Sim->EnqueueCommand(Order);
		// A Solar Array 8 m inside the order's west edge, still in the
		// Lander's coverage: only the in-flight 2x2 footprint stops it.
		Expect(!Sim->CanPlaceBuilding(FName("SolarArray"), Spot + FVector(-800.f, 600.f, 0.f), Why, 0) && Why.Contains(TEXT("transit")),
			FString::Printf(TEXT("the in-flight 2x2 order blocks its whole footprint (%s)"), *Why));
		for (int32 i = 0; i < 3; ++i) { Step(); }
		int32 DomeId = 0;
		for (const FRHBuildingInstance& B : Sim->GetBuildings()) { if (B.DefName == FName("GreenhouseDome")) { DomeId = B.Id; } }
		const FRHBuildingInstance* Dome = nullptr;
		for (const FRHBuildingInstance& B : Sim->GetBuildings()) { if (B.Id == DomeId) { Dome = &B; } }
		Expect(Dome && Dome->SizeX == 2 && Dome->SizeY == 2 && Dome->bUnderConstruction, TEXT("the order lands as a 2x2 site under construction"));
		if (!Dome)
		{
			return 1;
		}
		const TMap<FName, double> Bill = Sim->GetSiteBill(*Dome);
		const double* StructKg = Bill.Find(FName("Struct"));
		Expect(StructKg && FMath::IsNearlyEqual(*StructKg, DomeDef->CostStruct_kg * 4.0),
			FString::Printf(TEXT("site bill Struct %.0f kg (expect 4 x %.0f)"), StructKg ? *StructKg : -1.0, DomeDef->CostStruct_kg));
		Expect(Dome->BuildRemaining_s > DomeDef->BuildTime_s * 3.0, FString::Printf(TEXT("build time %.0f s (four beds)"), Dome->BuildRemaining_s));
		Expect(Sim->GetDomeRoom(*Dome) == nullptr, TEXT("an unbuilt dome gardens nothing"));

		// 4) A built 2x2 dome on a colony with NO rooms plants its four beds,
		// glazing each (soil, seeds, glass from stock).
		Sim->Debug_PlaceInstant(FName("GreenhouseDome"), FVector(-3000.f, 0.f, 0.f), 0, 2, 2);
		int32 BuiltId = 0;
		for (const FRHBuildingInstance& B : Sim->GetBuildings()) { if (B.DefName == FName("GreenhouseDome") && !B.bUnderConstruction) { BuiltId = B.Id; } }
		Sim->AddStock(FName("Soil"), 2000.0);
		Sim->AddStock(FName("Seeds"), 200.0);
		Sim->AddStock(FName("Water"), 4000.0);
		const double Glass0 = Sim->GetStock(FName("Glass"));
		Sim->AddStock(FName("Glass"), 4.0 * 200.0 - Glass0);
		Step();
		int32 Planted = 0;
		for (int32 Bed = 0; Bed < 4; ++Bed) { Planted += Sim->IsDomeBedPlanted(BuiltId, Bed) ? 1 : 0; }
		Expect(Planted == 4, FString::Printf(TEXT("four beds planted on a room-less colony (planted %d)"), Planted));
		Expect(Sim->GetStock(FName("Glass")) < 1.0, FString::Printf(TEXT("each bed glazed: glass %.0f kg left (expect 0)"), Sim->GetStock(FName("Glass"))));

		// 5) Daylight, not power: across a sol the beds drink only while the
		// sun is up, and draw no grow-light power at any hour.
		int32 DarkDrinks = 0, LitDrinks = 0, LightSteps = 0;
		double MaxGrowLightW = 0.0;
		for (int32 S = 0; S < 20; ++S)
		{
			// The step's light is read AFTER the clock advances, as EraStep sees it.
			const double W0 = Sim->GetStock(FName("Water"));
			Sim->Debug_SetBatteryWh(1.0e9);
			Clock->Debug_AdvanceSimSeconds(URHSimClockSubsystem::EraStepSimSeconds);
			const double Solar = DefsSub->EvalSolarCurve(Clock->GetSolFraction());
			Sim->EraStep(URHSimClockSubsystem::EraStepSimSeconds);
			const bool bDrank = Sim->GetStock(FName("Water")) < W0 - 1e-6;
			if (Solar <= KINDA_SMALL_NUMBER) { DarkDrinks += bDrank ? 1 : 0; }
			else { ++LightSteps; LitDrinks += bDrank ? 1 : 0; }
			MaxGrowLightW = FMath::Max(MaxGrowLightW, Sim->GetGardenPowerDrawW());
		}
		Expect(MaxGrowLightW <= 0.0, FString::Printf(TEXT("no grow-light draw at any hour (max %.0f W)"), MaxGrowLightW));
		Expect(DarkDrinks == 0 && LitDrinks > 0, FString::Printf(TEXT("beds grow by daylight only: lit drinks %d/%d, dark drinks %d"), LitDrinks, LightSteps, DarkDrinks));

		// 6) Save/load keeps the size and the beds.
		FString Err;
		Sim->SaveColony(TEXT("dometest"), Err);
		Sim->LoadColony(TEXT("dometest"), Err);
		const FRHBuildingInstance* Loaded = nullptr;
		for (const FRHBuildingInstance& B : Sim->GetBuildings()) { if (B.Id == BuiltId) { Loaded = &B; } }
		int32 PlantedAfter = 0;
		for (int32 Bed = 0; Bed < 4; ++Bed) { PlantedAfter += Sim->IsDomeBedPlanted(BuiltId, Bed) ? 1 : 0; }
		Expect(Loaded && Loaded->SizeX == 2 && Loaded->SizeY == 2 && PlantedAfter == 4,
			FString::Printf(TEXT("save/load: size %dx%d, %d beds planted"), Loaded ? Loaded->SizeX : 0, Loaded ? Loaded->SizeY : 0, PlantedAfter));

		UE_LOG(LogRedHopeSim, Display, TEXT("=== DOME TEST END: %d failure(s) ==="), Fails);
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return Fails > 0 ? 1 : 0;
	}

	// Surface habitats self-test (BM-1, 2026-10-16): pressurized shells on the
	// 10 m bay grid join into networks; a network rates Livable full +
	// circulated + >= MinLivableCells bays; residents, breathing, zoning,
	// stations in bays, Hope across modules, the optional shaft link, save
	// v29 and loss/recovery. Fails loudly. `-surfacehab`.
	if (FParse::Param(*Params, TEXT("surfacehab")))
	{
		UE_LOG(LogRedHopeSim, Display, TEXT("=== SURFACE HABITAT TEST (BM-1, 2026-10-16) ==="));
		const int32 StepsPerSolH = (int32)(URHSimClockSubsystem::SolLengthSimSeconds / URHSimClockSubsystem::EraStepSimSeconds);
		const auto RunSols = [&](double Sols)
		{
			for (int32 S = 0; S < (int32)(Sols * StepsPerSolH); ++S)
			{
				Sim->Debug_AddFreshWater(500.0 * URHSimClockSubsystem::EraStepSimSeconds / (double)URHSimClockSubsystem::SolLengthSimSeconds);
				Sim->Debug_SetBatteryWh(1.0e9);
				Clock->Debug_AdvanceSimSeconds(URHSimClockSubsystem::EraStepSimSeconds);
				Sim->EraStep(URHSimClockSubsystem::EraStepSimSeconds);
			}
		};
		int32 Fails = 0;
		const auto Expect = [&](bool bOk, const FString& What)
		{
			if (bOk) { UE_LOG(LogRedHopeSim, Display, TEXT("SURFACEHAB ok: %s"), *What); }
			else { UE_LOG(LogRedHopeSim, Error, TEXT("SURFACEHAB FAIL: %s"), *What); ++Fails; }
		};
		const auto Near = [](double A, double B, double Tol) { return FMath::Abs(A - B) <= Tol; };
		int32 Milestones = 0;
		TArray<FString> Alerts;
		const FDelegateHandle MilestoneH = Sim->OnMilestone.AddLambda([&Milestones](const FString&) { ++Milestones; });
		const FDelegateHandle AlertH = Sim->OnAlert.AddLambda([&Alerts](const FString& S) { Alerts.Add(S); });
		const auto CountAlerts = [&Alerts](const TCHAR* Sub) { int32 N = 0; for (const FString& A : Alerts) { N += A.Contains(Sub) ? 1 : 0; } return N; };
		const auto LastAlert = [&Alerts]() { return Alerts.Num() ? Alerts.Last() : FString(); };
		const auto IdOf = [&](const TCHAR* Def, double X, double Y, int32 Level = 0)
		{
			for (const FRHBuildingInstance& B : Sim->GetBuildings())
			{
				if (B.DefName == FName(Def) && B.Level == Level && FMath::Abs(B.LocationCm.X - X) < 1.0 && FMath::Abs(B.LocationCm.Y - Y) < 1.0)
				{
					return B.Id;
				}
			}
			return 0;
		};
		const auto Place = [&](const TCHAR* Def, double X, double Y, int32 SX = 1, int32 SY = 1, int32 Level = 0)
		{
			Sim->Debug_PlaceInstant(FName(Def), FVector(X, Y, 0.0), Level, SX, SY);
			return IdOf(Def, X, Y, Level);
		};
		const auto Can = [&](const TCHAR* Def, double X, double Y, FString& Why, int32 SX = 1, int32 SY = 1, int32 Level = 0)
		{
			Why.Reset();
			return Sim->CanPlaceBuildingSized(FName(Def), FVector(X, Y, 0.0), Why, Level, SX, SY);
		};
		URHDefinitionsSubsystem* DefsSub = World->GetSubsystem<URHDefinitionsSubsystem>();
		const FName NOx(TEXT("Oxygen"));
		FString Why, R;

		// S0. A fresh world houses nobody.
		Expect(Sim->GetHousingCapacity() == 0 && Sim->Debug_AddColonists(1) == 0 && Sim->GetPopulation() == 0, TEXT("S0 fresh world: no beds, no colonists"));

		// S1. Pure data (DT/CSV), fail-fast.
		const FRHBuildingRow* HabDef = DefsSub->GetBuilding(FName("HabModule"));
		const FRHBuildingRow* CorDef = DefsSub->GetBuilding(FName("HabCorridor"));
		if (!HabDef || !CorDef)
		{
			UE_LOG(LogRedHopeSim, Error, TEXT("SURFACEHAB FAIL: HabModule/HabCorridor rows missing (sync DT_Buildings)"));
			return 1;
		}
		{
			const TMap<FName, double> Bill = URHDefinitionsSubsystem::GetBuildCostFor(*HabDef, 0);
			Expect(HabDef->SliceActive && HabDef->DragSized && HabDef->Pressurized && !HabDef->Interior && HabDef->FootprintX == 5 && HabDef->FootprintY == 5
				&& HabDef->MaxBedsPerSide == 4 && HabDef->DragShortSideMax == 0 && Near(Bill.FindRef(FName("Struct")), 200.0, 1e-6) && Near(Bill.FindRef(FName("Shielding")), 50.0, 1e-6),
				TEXT("S1 HabModule row: drag-sized pressurized 5x5 bay, cap 4, bill Struct 200 + Shielding 50"));
			const TMap<FName, double> CBill = URHDefinitionsSubsystem::GetBuildCostFor(*CorDef, 0);
			Expect(CorDef->Pressurized && CorDef->DragSized && CorDef->ProvidesRoom == FName("Hallway") && CorDef->DragShortSideMax == 1 && CorDef->MaxBedsPerSide == 6
				&& CBill.FindRef(FName("Struct")) < Bill.FindRef(FName("Struct")), TEXT("S1 HabCorridor row: pressurized Hallway, 1 bay wide, cap 6, cheaper"));
			const FRHBuildingRow* Old = DefsSub->GetBuilding(FName("Habitat"));
			Expect(Old && Old->SliceActive && Old->Pressurized && !Old->DragSized, TEXT("S1 the Habitat row is the BM-1b prefab: a fixed pressurized shell"));
			bool bInteriorOk = true;
			for (const TCHAR* N : { TEXT("AirFilter"), TEXT("LabModule"), TEXT("InfirmaryModule"), TEXT("HumidityRegulator"), TEXT("Floodmast") })
			{
				const FRHBuildingRow* D = DefsSub->GetBuilding(FName(N));
				bInteriorOk &= D && D->Interior;
			}
			for (const TCHAR* N : { TEXT("HabModule"), TEXT("GreenhouseDome"), TEXT("ChargePad"), TEXT("SolarArray") })
			{
				const FRHBuildingRow* D = DefsSub->GetBuilding(FName(N));
				bInteriorOk &= D && !D->Interior;
			}
			Expect(bInteriorOk, TEXT("S1 Interior flags: life support + modules + Floodmast yes; shells, dome, pads, solar no"));
			const FRHBuildingRow* DomeDef = DefsSub->GetBuilding(FName("GreenhouseDome"));
			Expect(DomeDef && !DomeDef->Pressurized && DomeDef->MaxBedsPerSide == 0 && Sim->GetMinLivableCells() == 4 && Sim->GetDomeMaxBedsPerSide() == 3
				&& Sim->GetMaxBedsPerSide(HabDef) == 4 && Sim->GetMaxBedsPerSide(DomeDef) == 3, TEXT("S1 caps: hab 4, dome 3 (row 0 = config), 4-bay minimum"));
		}

		// S2. Setup: power and stock (no Shielding yet).
		Sim->Debug_PlaceInstant(FName("SolarArray"), FVector(3500.f, 1000.f, 0.f));
		Sim->Debug_PlaceInstant(FName("SolarArray"), FVector(3500.f, 2000.f, 0.f));
		Sim->Debug_PlaceInstant(FName("BatteryBank"), FVector(1000.f, 3500.f, 0.f));
		Sim->AddStock(FName("Struct"), 5000.0);
		Sim->AddStock(NOx, 6000.0);
		Sim->AddStock(FName("Water"), 4000.0);
		Sim->AddStock(FName("Food"), 6000.0);

		// S3. Placement gates.
		Expect(!Can(TEXT("HabModule"), -1000, 1000, Why) && Why.Contains(TEXT("Shielding")), FString::Printf(TEXT("S3a no Shielding, no producer: refused (%s)"), *Why));
		Sim->AddStock(FName("Shielding"), 1000.0);
		Expect(Can(TEXT("HabModule"), -1000, 1000, Why), FString::Printf(TEXT("S3b with Shielding: accepted (%s)"), *Why));
		Expect(!Can(TEXT("HabModule"), -1250, 1000, Why) && Why.Contains(TEXT("grid")), FString::Printf(TEXT("S3c off-grid x refused (%s)"), *Why));
		Expect(!Can(TEXT("HabModule"), -1000, 1250, Why) && Why.Contains(TEXT("grid")), FString::Printf(TEXT("S3c off-grid y refused (%s)"), *Why));
		Expect(!Can(TEXT("HabModule"), -1000, 1000, Why, 2, 1) && Why.Contains(TEXT("grid")), FString::Printf(TEXT("S3c a 2x1 centred on a bay centre is off-grid (%s)"), *Why));
		Expect(Can(TEXT("HabModule"), -1500, 1000, Why, 2, 1), FString::Printf(TEXT("S3c a 2x1 centred on a bay edge is on-grid (%s)"), *Why));
		Expect(!Can(TEXT("HabModule"), -4000, 0, Why) && Why.Contains(TEXT("coverage")), FString::Printf(TEXT("S3d outside coverage refused (%s)"), *Why));
		Expect(!Can(TEXT("HabModule"), -2500, 1000, Why, 2, 1) && Why.Contains(TEXT("coverage")), FString::Printf(TEXT("S3d every bay must be covered, not just the centre (%s)"), *Why));
		Expect(!Can(TEXT("HabModule"), -500, 2000, Why, 5, 1) && Why.Contains(TEXT("bays a side")), FString::Printf(TEXT("S3e past 4 bays a side refused (%s)"), *Why));
		Expect(!Can(TEXT("HabCorridor"), -1500, 1500, Why, 2, 2) && Why.Contains(TEXT("wide")), FString::Printf(TEXT("S3e a 2-wide corridor refused (%s)"), *Why));
		Expect(Can(TEXT("HabCorridor"), 0, 2000, Why, 3, 1), FString::Printf(TEXT("S3e a 3x1 corridor accepted (%s)"), *Why));
		Expect(Can(TEXT("AirFilter"), -1000, 1000, Why), FString::Printf(TEXT("S3f a surface AirFilter with no hab is legacy-legal (%s)"), *Why));

		// S4. A 2x1 order: the site's bill and build time are two bays'.
		{
			FRHCommand Order; Order.Verb = FName("Build"); Order.Target = FName("HabModule"); Order.Location = FVector(-500.f, -1000.f, 0.f); Order.Level = 0;
			Order.Value = URHSimWorldSubsystem::EncodeBuildSize(2, 1);
			Sim->EnqueueCommand(Order);
			RunSols(0.05);
			const FRHBuildingInstance* Site = nullptr;
			for (const FRHBuildingInstance& B : Sim->GetBuildings()) { if (B.DefName == FName("HabModule") && B.bUnderConstruction) { Site = &B; } }
			Expect(Site && Site->SizeX == 2 && Site->SizeY == 1, TEXT("S4 the order lands as a 2x1 site"));
			if (Site)
			{
				const TMap<FName, double> Bill = Sim->GetSiteBill(*Site);
				Expect(Near(Bill.FindRef(FName("Struct")), 400.0, 1e-6) && Near(Bill.FindRef(FName("Shielding")), 100.0, 1e-6) && Near(Site->BuildRemaining_s, 600.0, 1e-6)
					&& Sim->GetHabNetworkRoot(Site->Id) == 0, FString::Printf(TEXT("S4 site bill Struct %.0f + Shielding %.0f, %.0f s, not a network member"),
						Bill.FindRef(FName("Struct")), Bill.FindRef(FName("Shielding")), Site->BuildRemaining_s));
			}
		}

		// S5. A one-bay pocket seals but cannot rate.
		const int32 A = Place(TEXT("HabModule"), -1000, 1000);
		Expect(Can(TEXT("AirFilter"), -1000, 1000, Why), FString::Printf(TEXT("S5 an AirFilter wholly inside the bay is accepted (%s)"), *Why));
		Expect(!Can(TEXT("AirFilter"), -1500, 1000, Why), FString::Printf(TEXT("S5 an AirFilter straddling the wall is refused (%s)"), *Why));
		Expect(!Can(TEXT("AirFilter"), -1000, 1400, Why), FString::Printf(TEXT("S5 an AirFilter poking out of the bay is refused (%s)"), *Why));
		Expect(!Can(TEXT("ChargePad"), -1000, 1000, Why) && !Can(TEXT("HabModule"), -1000, 1000, Why), TEXT("S5 a pad or another shell inside the bay is refused"));
		Place(TEXT("AirFilter"), -1000, -2000);
		RunSols(0.5);
		Expect(Near(Sim->GetHabO2Kg(A), 0.0, 1e-9) && !Sim->IsHabNetworkCirculated(A) && !Sim->IsHabRated(A),
			FString::Printf(TEXT("S5 an outside filter does not circulate the hab (O2 %.2f)"), Sim->GetHabO2Kg(A)));
		const int32 AFA = Place(TEXT("AirFilter"), -1000, 1000);
		RunSols(1.0);
		Expect(Near(Sim->GetHabO2Kg(A), 100.0, 1e-6) && Sim->IsHabNetworkCirculated(A) && !Sim->IsHabRated(A) && Sim->IsHabNetworkSealedButSmall(A),
			FString::Printf(TEXT("S5 filter inside: full %.2f kg, circulated, sealed but small"), Sim->GetHabO2Kg(A)));
		Expect(CountAlerts(TEXT("SURFACE HABITAT SEALED")) == 1 && Sim->GetHousingCapacity() == 0 && !Sim->HasVaultRating() && Milestones == 0,
			FString::Printf(TEXT("S5 one SEALED note (%d), no beds, no vault milestone"), CountAlerts(TEXT("SURFACE HABITAT SEALED"))));

		// S6. A network forms and rates.
		const int32 B = Place(TEXT("HabModule"), -2000, 1000);
		const int32 Cc = Place(TEXT("HabModule"), -1500, 2000, 2, 1);
		const int32 Dome = Place(TEXT("GreenhouseDome"), -3000, 1000);
		Expect(A && B && Cc && Sim->GetHabNetworkRoot(B) == A && Sim->GetHabNetworkRoot(Cc) == A && Sim->GetHabNetworkBays(A) == 4 && Sim->GetHabNetworkRoot(Dome) == 0,
			FString::Printf(TEXT("S6 A+B+C share edges: one network of %d bays rooted at A; the dome is not a member"), Sim->GetHabNetworkBays(A)));
		RunSols(1.0);
		Expect(Near(Sim->GetHabO2Kg(B), 100.0, 1e-6) && Near(Sim->GetHabO2Kg(Cc), 200.0, 1e-6) && Sim->IsHabRated(A) && Sim->IsHabRated(B) && Sim->IsHabRated(Cc),
			FString::Printf(TEXT("S6 B %.1f + C %.1f kg: the network rates"), Sim->GetHabO2Kg(B), Sim->GetHabO2Kg(Cc)));
		Expect(CountAlerts(TEXT("RATED LIVABLE")) == 1 && LastAlert().Contains(TEXT("0 beds")) && !Sim->HasVaultRating() && Sim->GetColonyHope().Milestones == 0.0
			&& Milestones == 0 && Sim->GetHousingCapacity() == 0, TEXT("S6 one RATED alert naming 0 beds; never the vault milestone"));
		Sim->Debug_DeliverCargo(FName("CrewPod"));
		Expect(Sim->GetPopulation() == 0 && LastAlert().Contains(TEXT("CREW POD RETURNED")) && LastAlert().Contains(TEXT("vault")),
			FString::Printf(TEXT("S6 no vault yet: the pod is turned away and points at the vault (%s)"), *LastAlert()));

		// S7. Zoning bays.
		{
			FRHCommand Z; Z.Verb = FName("Designate"); Z.Target = FName("LivingQuarters"); Z.Level = 0; Z.Location = FVector(-1000.f, 1000.f, 0.f);
			Sim->EnqueueCommand(Z);
			RunSols(0.05);
		}
		Expect(Sim->GetBayRoom(A, 0) == FName("LivingQuarters"), TEXT("S7a the Designate order zones bay A:0 by location"));
		Why.Reset();
		Expect(Sim->DesignateBay(Cc, 1, FName("LivingQuarters"), Why), FString::Printf(TEXT("S7b C:1 zoned Living (%s)"), *Why));
		Expect(!Sim->DesignateBay(B, 0, FName("Garden"), Why) && !Sim->DesignateBay(Cc, 0, FName("Greenhouse"), Why) && !Sim->DesignateBay(B, 0, FName("Smoking"), Why)
			&& !Sim->DesignateBay(B, 1, FName("Lab"), Why) && Sim->GetPlantedCellCount() == 0, TEXT("S7c gardens, a dormant row and an out-of-range bay are refused"));
		Expect(Sim->GetHabBeds(A) + Sim->GetHabBeds(Cc) == 2 && Sim->GetHousingCapacity() == 0,
			FString::Printf(TEXT("S7d two Living bays = 2 beds, not yet open to crew without the vault (capacity %d)"), Sim->GetHousingCapacity()));
		const int32 Hh = Place(TEXT("HabModule"), -2000, -500, 1, 2);
		Expect(Hh && Sim->GetBayGrid(*Sim->GetBuildings().FindByPredicate([Hh](const FRHBuildingInstance& X) { return X.Id == Hh; })) == FIntPoint(1, 2)
			&& Sim->GetHabNetworkBays(A) == 6 && Sim->IsHabRated(Hh), TEXT("S7e H joins the rated network (6 bays) and carries its rating"));
		Expect(Sim->DesignateBay(Hh, 0, FName("LivingQuarters"), Why) && Sim->DesignateBay(Hh, 1, FName("LivingQuarters"), Why) && Sim->GetHabBeds(Hh) == 2,
			FString::Printf(TEXT("S7e H's two Living bays: %d beds"), Sim->GetHabBeds(Hh)));
		{
			int32 SiteId = 0;
			for (const FRHBuildingInstance& X : Sim->GetBuildings()) { if (X.DefName == FName("HabModule") && X.bUnderConstruction) { SiteId = X.Id; } }
			Expect(Sim->GetHabNetworkRoot(SiteId) == 0 && Sim->GetHabNetworkBays(A) == 6, TEXT("S7f the edge-adjacent construction site stays out of the network"));
		}

		// S8. Crew still need the vault (director 2026-10-16): with 4 surface
		// beds and no vault the pod is turned away; once the vault is
		// certified it lands in the surface beds. Breathing spans the network.
		Sim->Debug_DeliverCargo(FName("CrewPod"));
		Expect(Sim->GetPopulation() == 0 && Sim->GetHousingCapacity() == 0 && LastAlert().Contains(TEXT("vault")),
			TEXT("S8 surface beds alone do not admit crew"));
		Sim->Debug_SetVaultRated(true);
		Expect(Sim->GetHousingCapacity() == 4, FString::Printf(TEXT("S8 vault certified: the 4 surface beds open (capacity %d)"), Sim->GetHousingCapacity()));
		Sim->Debug_DeliverCargo(FName("CrewPod"));
		{
			const TArray<FRHColonist>& Cs = Sim->GetColonists();
			const bool bHomes = Cs.Num() == 4 && Cs[0].HomeHab == A && Cs[1].HomeHab == Cc && Cs[2].HomeHab == Hh && Cs[3].HomeHab == Hh
				&& Cs[0].HomeLevel == 0 && Cs[1].HomeLevel == 0 && Cs[2].HomeLevel == 0 && Cs[3].HomeLevel == 0;
			Expect(bHomes && LastAlert().Contains(TEXT("THE CREW HAS LANDED")), TEXT("S8 four colonists land in surface beds [A, C, H, H], HomeLevel 0"));
		}
		Expect(Sim->Debug_AddColonists(1) == 0, TEXT("S8 no fifth bed"));
		Sim->AddStock(NOx, 0.400001 - Sim->GetStock(NOx));
		RunSols(0.05);
		{
			bool bAll = true;
			for (const FRHColonist& Col : Sim->GetColonists()) { bAll &= Col.bSupported; }
			Expect(Sim->GetHabO2Kg(Hh) < 1e-9 && bAll && Sim->IsHabRated(A) && Sim->IsHabRated(Hh) && Sim->GetHabO2Kg(A) < 99.9,
				FString::Printf(TEXT("S8 empty H's residents breathe the network (H %.4f, A %.4f), all supported"), Sim->GetHabO2Kg(Hh), Sim->GetHabO2Kg(A)));
		}
		Sim->AddStock(NOx, 6000.0);
		RunSols(0.5);
		{
			bool bAll = true;
			for (const FRHColonist& Col : Sim->GetColonists()) { bAll &= Col.bSupported; }
			// The push fills H to 200 kg, then its two residents breathe in the
			// same step (StepPopulation runs after StepHabitability): within 0.1.
			Expect(Near(Sim->GetHabO2Kg(Hh), 200.0, 0.1) && bAll && Near(Sim->GetColonyHope().Housing, 15.0, 1e-6),
				FString::Printf(TEXT("S8 refilled: H %.3f kg, all supported, Housing %.2f"), Sim->GetHabO2Kg(Hh), Sim->GetColonyHope().Housing));
		}
		Expect(!Sim->DesignateBay(A, 0, NAME_None, Why), FString::Printf(TEXT("S8 the bed guard keeps A's sleeper's bed (%s)"), *Why));

		// S9. Stations in bays.
		Expect(Can(TEXT("InfirmaryModule"), -2000, 1000, Why), FString::Printf(TEXT("S9a an Infirmary in free bay B (%s)"), *Why));
		Expect(!Can(TEXT("InfirmaryModule"), -1000, 1000, Why) && Why.Contains(TEXT("zoned")), FString::Printf(TEXT("S9a not in zoned bay A (%s)"), *Why));
		Expect(!Can(TEXT("InfirmaryModule"), -2000, -1000, Why), FString::Printf(TEXT("S9a not in zoned H:0 (%s)"), *Why));
		Expect(!Can(TEXT("InfirmaryModule"), 0, -2000, Why), FString::Printf(TEXT("S9a not on open ground (%s)"), *Why));
		Place(TEXT("InfirmaryModule"), -2000, 1000);
		RunSols(0.05);
		Expect(Near(Sim->GetEffectiveEvacSols(), 3.0, 1e-9) && !Can(TEXT("LabModule"), -2000, 1000, Why) && !Sim->DesignateBay(B, 0, FName("Lab"), Why),
			FString::Printf(TEXT("S9b the infirmary in B stretches recall to %.1f sols; one station per bay"), Sim->GetEffectiveEvacSols()));
		DefsSub->ActivateRoomRow(FName("LabFull"));
		Expect(Can(TEXT("LabModule"), -2000, 2000, Why), FString::Printf(TEXT("S9c a Lab Module in C:0 (%s)"), *Why));
		Place(TEXT("LabModule"), -2000, 2000);
		RunSols(0.05);
		Expect(Near(Sim->GetLabYieldSeatSum(), 4.0, 1e-4) && Sim->GetColonyHope().FilledSeats == 2,
			FString::Printf(TEXT("S9c the module seats 2 at x2.0 (labSum %.2f, filled %d)"), Sim->GetLabYieldSeatSum(), Sim->GetColonyHope().FilledSeats));

		// S10. Shaft link, the corridor, floors-first housing, adjacency, seat order.
		Sim->ExtendShaft(1, FVector(1000.f, 1000.f, 0.f));
		Sim->ExcavateFloor(-1, 4, R);
		Expect(!Can(TEXT("HabModule"), 1000, -3000, Why, 1, 1, -1) && Why.Contains(TEXT("surface")), FString::Printf(TEXT("S10 no habitat shells underground (%s)"), *Why));
		const int32 PQ = Place(TEXT("HabModule"), 1500, 1000, 2, 1);
		const int32 Corr = Place(TEXT("HabCorridor"), 1000, 2000);
		RunSols(0.5);
		Expect(Near(Sim->GetHabO2Kg(PQ), 0.0, 1e-9) && !Sim->IsHabNetworkCirculated(PQ), TEXT("S10 no filter, no rated floor -1: no air"));
		Expect(!Sim->DesignateBay(Corr, 0, FName("Lab"), Why) && Why.Contains(TEXT("fixed")), FString::Printf(TEXT("S10 a corridor bay's room is fixed (%s)"), *Why));
		const int32 FAF = Place(TEXT("AirFilter"), 1000, 1500, 1, 1, -1);
		RunSols(2.0);
		Expect(Sim->IsFloorRated(-1) && Sim->HasVaultRating() && Milestones == 0 && Near(Sim->GetColonyHope().Milestones, 5.0, 1e-9),
			FString::Printf(TEXT("S10 floor -1 rates (vault flag already set by the harness: no second milestone, %d; Hope milestones 5)"), Milestones));
		Expect(Near(Sim->GetHabO2Kg(PQ), 200.0, 1e-6) && Near(Sim->GetHabO2Kg(Corr), 100.0, 1e-6) && !Sim->IsHabRated(PQ) && Sim->IsHabNetworkSealedButSmall(PQ)
			&& CountAlerts(TEXT("SURFACE HABITAT SEALED")) == 2, FString::Printf(TEXT("S10 the shaft link fills PQ %.1f + corridor %.1f; 3 bays: sealed, not rated"),
				Sim->GetHabO2Kg(PQ), Sim->GetHabO2Kg(Corr)));
		const int32 Rr = Place(TEXT("HabModule"), 2000, 2000);
		RunSols(0.5);
		Expect(Sim->IsHabRated(PQ) && Sim->GetHabNetworkBays(PQ) == 4, TEXT("S10 R makes 4 bays (the corridor counts): rated"));
		Sim->SetManualPower(FAF, false);
		RunSols(0.1);
		Expect(!Sim->IsFloorRated(-1) && !Sim->IsHabRated(PQ) && Sim->IsHabRated(A), TEXT("S10 floor filter off: floor -1 and the linked network lose their rating; A keeps its own"));
		Sim->SetManualPower(FAF, true);
		RunSols(0.5);
		Expect(Sim->IsFloorRated(-1) && Sim->IsHabRated(PQ), TEXT("S10 back on: both rate again"));
		Sim->DesignateBay(PQ, 0, FName("LivingQuarters"), Why);
		Expect(Sim->GetHousingCapacity() == 9, FString::Printf(TEXT("S10 capacity floor 4 + network 4 + PQ 1 = %d (expect 9)"), Sim->GetHousingCapacity()));
		Expect(Sim->Debug_AddColonists(6) == 5, TEXT("S10 five more housed"));
		{
			const TArray<FRHColonist>& Cs = Sim->GetColonists();
			bool bOk = Cs.Num() == 9;
			for (int32 i = 4; bOk && i < 8; ++i) { bOk &= Cs[i].HomeLevel == -1 && Cs[i].HomeHab == 0; }
			bOk = bOk && Cs[8].HomeHab == PQ && Cs[8].HomeLevel == 0;
			Expect(bOk, TEXT("S10 floors fill first (Ids 5-8 on floor -1), then the network (Id 9 in PQ)"));
		}
		const int32 Ss = Place(TEXT("HabModule"), 2000, 0);
		RunSols(0.5);
		Sim->DesignateBay(Ss, 0, FName("Workstation"), Why);
		Sim->DesignateBay(Rr, 0, FName("Cooking"), Why);
		Sim->DesignateBay(PQ, 1, FName("Dining"), Why);
		Sim->DesignateRoom(-1, 0, FName("Workstation"), R);
		RunSols(0.05);
		{
			const URHSimWorldSubsystem::FRHHopeBreakdown Hp = Sim->GetColonyHope();
			FIntVector St;
			const bool bSt = Sim->GetColonistStation(1, St);
			Expect(Hp.OffendedPairs == 1 && Near(Hp.AdjacencyPenalty, 8.0, 1e-9) && Near(Hp.Rooms, 6.5, 1e-6),
				FString::Printf(TEXT("S10 Hope: Q-R offends, the corridor partitions P-R (pairs %d, adj %.1f, rooms %.2f)"), Hp.OffendedPairs, Hp.AdjacencyPenalty, Hp.Rooms));
			Expect(Hp.FilledSeats == 4 && Near(Hp.Jobs, 12.0, 1e-9) && Near(Sim->GetLabYieldSeatSum(), 4.0, 1e-4) && Near(Sim->GetWorkstationYieldSeatSum(), 2.0, 1e-4)
				&& bSt && St == FIntVector(-1, 0, 0) && Near(Hp.Housing, 15.0 * 5.0 / 9.0, 1e-6),
				FString::Printf(TEXT("S10 seats: floor first (filled %d, jobs %.0f, lab %.2f, ws %.2f, housing %.3f)"), Hp.FilledSeats, Hp.Jobs,
					Sim->GetLabYieldSeatSum(), Sim->GetWorkstationYieldSeatSum(), Hp.Housing));
		}
		Sim->DesignateBay(PQ, 1, NAME_None, Why);
		Expect(Sim->GetColonyHope().OffendedPairs == 0 && Near(Sim->GetColonyHope().Rooms, 3.5, 1e-6), TEXT("S10 clearing the Dining bay clears the pair"));
		Sim->DesignateBay(PQ, 1, FName("Dining"), Why);
		Expect(Sim->GetColonyHope().OffendedPairs == 1 && Near(Sim->GetColonyHope().Rooms, 6.5, 1e-6), TEXT("S10 re-zoned: the pair is back"));
		{
			FRHCommand As; As.Verb = FName("Assign"); As.Level = 0; As.Location = FVector(2000.f, 0.f, 0.f);
			Sim->EnqueueCommand(As);
			RunSols(0.05);
			FIntVector St;
			Expect(Sim->GetSeatPriority().Num() && Sim->GetSeatPriority()[0] == FIntVector(0, 0, Ss) && Sim->GetColonistStation(1, St) && St == FIntVector(0, 0, Ss),
				TEXT("S10 Assign by location puts bay S:0 first"));
		}

		// S11. Save/load inside the 98-100% band.
		TArray<FIntPoint> Homes;
		for (const FRHColonist& Col : Sim->GetColonists()) { Homes.Add(FIntPoint(Col.HomeLevel, Col.HomeHab)); }
		Sim->AddStock(NOx, -Sim->GetStock(NOx));
		RunSols(0.25);
		const auto InBand = [&](double Fill, double Req) { return Req > 0.0 && Fill >= 0.98 * Req && Fill < Req; };
		Expect(InBand(Sim->GetHabNetworkO2Kg(A), Sim->GetHabNetworkO2RequiredKg(A)) && InBand(Sim->GetHabNetworkO2Kg(PQ), Sim->GetHabNetworkO2RequiredKg(PQ))
			&& InBand(Sim->GetFloorO2Kg(-1), Sim->GetFloorO2RequiredKg(-1)) && Sim->IsHabRated(A) && Sim->IsHabRated(PQ) && Sim->IsFloorRated(-1),
			FString::Printf(TEXT("S11 dry pool: all three inside the hysteresis band, still rated (net1 %.2f/%.0f, net2 %.2f/%.0f, floor %.2f/%.0f)"),
				Sim->GetHabNetworkO2Kg(A), Sim->GetHabNetworkO2RequiredKg(A), Sim->GetHabNetworkO2Kg(PQ), Sim->GetHabNetworkO2RequiredKg(PQ),
				Sim->GetFloorO2Kg(-1), Sim->GetFloorO2RequiredKg(-1)));
		TMap<int32, double> O2Snap;
		for (const int32 Id : { A, B, Cc, Hh, PQ, Corr, Rr, Ss }) { O2Snap.Add(Id, Sim->GetHabO2Kg(Id)); }
		const int32 CapSnap = Sim->GetHousingCapacity();
		const double LabSnap = Sim->GetLabYieldSeatSum(), EvacSnap = Sim->GetEffectiveEvacSols();
		const TArray<FIntVector> PrioSnap = Sim->GetSeatPriority();
		FString Err;
		Expect(Sim->SaveColony(TEXT("surfacehabtest"), Err), FString::Printf(TEXT("S11 saved (%s)"), *Err));
		Place(TEXT("HabModule"), 0, 2000);  // dirty: merges the networks
		Sim->DesignateBay(PQ, 1, NAME_None, Why);
		Err.Reset();
		Expect(Sim->LoadColony(TEXT("surfacehabtest"), Err) && Err.IsEmpty(), FString::Printf(TEXT("S11 loaded (%s)"), *Err));
		{
			int32 Hx = 0, Bx = 0;
			bool bSame = !Sim->FindBayAt(FVector(0.f, 2000.f, 0.f), Hx, Bx) && Sim->GetBayRoom(PQ, 1) == FName("Dining")
				&& Sim->GetHousingCapacity() == CapSnap && Near(Sim->GetLabYieldSeatSum(), LabSnap, 1e-9) && Near(Sim->GetEffectiveEvacSols(), EvacSnap, 1e-9)
				&& Sim->GetSeatPriority() == PrioSnap && Sim->GetHabNetworkBays(A) == 6 && Sim->GetHabNetworkBays(PQ) == 5;
			for (const TPair<int32, double>& P : O2Snap) { bSame &= Near(Sim->GetHabO2Kg(P.Key), P.Value, 1e-9); }
			const TArray<FRHColonist>& Cs = Sim->GetColonists();
			bSame &= Cs.Num() == Homes.Num();
			for (int32 i = 0; bSame && i < Cs.Num(); ++i) { bSame &= FIntPoint(Cs[i].HomeLevel, Cs[i].HomeHab) == Homes[i]; }
			FIntVector St;
			bSame &= Sim->GetColonistStation(1, St) && St == FIntVector(0, 0, Ss);
			Expect(bSame, TEXT("S11 the load restores O2, homes, rooms, networks (6 + 5 bays), seats and priority with no step"));
		}
		RunSols(0.05);
		Expect(Sim->IsHabRated(A) && Sim->IsHabRated(PQ) && Sim->IsFloorRated(-1), TEXT("S11 a step after the load: the ratings held (a lost rating could not be regained on a dry pool)"));

		// S12. Loss and recovery.
		RunSols(1.0);
		{
			bool bNone = true;
			for (const FRHColonist& Col : Sim->GetColonists()) { bNone &= !Col.bSupported; }
			Expect(!Sim->IsHabRated(A) && !Sim->IsHabRated(PQ) && !Sim->IsFloorRated(-1) && bNone && Sim->GetHousingCapacity() == 0 && Sim->GetColonyHope().FilledSeats == 0
				&& Near(Sim->GetLabYieldSeatSum(), 0.0, 1e-9) && Near(Sim->GetEffectiveEvacSols(), 2.0, 1e-9) && Sim->GetPopulation() == 9,
				TEXT("S12 a dry pool: every rating lost, everyone unsupported, no seats, no infirmary, nobody recalled yet"));
		}
		Sim->AddStock(NOx, 6000.0);
		RunSols(1.0);
		{
			bool bAll = true;
			for (const FRHColonist& Col : Sim->GetColonists()) { bAll &= Col.bSupported; }
			Expect(Sim->IsHabRated(A) && Sim->IsHabRated(PQ) && Sim->IsFloorRated(-1) && bAll && Sim->GetHousingCapacity() == 9 && Near(Sim->GetLabYieldSeatSum(), 4.0, 1e-4)
				&& Near(Sim->GetEffectiveEvacSols(), 3.0, 1e-9), TEXT("S12 oxygen back: all rated, all supported, seats and infirmary back"));
		}

		// S12b. A surface-only failure recalls the surface crew alone.
		Sim->SetManualPower(AFA, false);
		RunSols(0.1);
		{
			const TArray<FRHColonist>& Cs = Sim->GetColonists();
			bool bSurfaceDown = Cs.Num() == 9;
			for (int32 i = 0; bSurfaceDown && i < 4; ++i) { bSurfaceDown &= !Cs[i].bSupported; }
			Expect(!Sim->IsHabRated(A) && Sim->IsHabRated(PQ) && Sim->IsFloorRated(-1) && bSurfaceDown && Near(Sim->GetEffectiveEvacSols(), 2.0, 1e-9),
				TEXT("S12b network 1's filter off: only it fails (no shaft link); its four lose support; the infirmary in B goes offline"));
		}
		RunSols(2.5);
		{
			const TArray<FRHColonist>& Cs = Sim->GetColonists();
			bool bOk = Cs.Num() == 5;
			for (int32 i = 0; bOk && i < 4; ++i) { bOk &= Cs[i].HomeLevel == -1 && Cs[i].HomeHab == 0; }
			bOk = bOk && Cs[4].HomeHab == PQ && Sim->GetHabResidents(A) == 0 && Sim->GetHabResidents(Hh) == 0 && Sim->GetHousingCapacity() == 5;
			Expect(bOk, FString::Printf(TEXT("S12b the four surface residents are recalled; floor and PQ stay (pop %d, capacity %d)"), Sim->GetPopulation(), Sim->GetHousingCapacity()));
			TArray<FIntPoint> Before;
			for (const FRHColonist& Col : Cs) { Before.Add(FIntPoint(Col.HomeLevel, Col.HomeHab)); }
			Err.Reset();
			Sim->SaveColony(TEXT("surfacehabtest2"), Err);
			Sim->Debug_AddColonists(1);
			Sim->LoadColony(TEXT("surfacehabtest2"), Err);
			bool bSame = Sim->GetPopulation() == 5;
			for (int32 i = 0; bSame && i < Sim->GetColonists().Num(); ++i)
			{
				bSame &= FIntPoint(Sim->GetColonists()[i].HomeLevel, Sim->GetColonists()[i].HomeHab) == Before[i];
			}
			Expect(bSame, TEXT("S12b save/load keeps every survivor's home"));
		}

		Sim->OnMilestone.Remove(MilestoneH);
		Sim->OnAlert.Remove(AlertH);
		UE_LOG(LogRedHopeSim, Display, TEXT("=== SURFACE HABITAT TEST END: %d failure(s) ==="), Fails);
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return Fails > 0 ? 1 : 0;
	}

	// BM-3 self-test (2026-10-16): DEMOLISH - the uplink verb and its
	// refusals, salvage to the Lander, cancelled sites, cargo in flight, pads,
	// domes, imports and storage, the lag tier, habitat splits and the
	// Buildings-index caches, stale orders, save/load. Fails loudly.
	// `-demolish`.
	if (FParse::Param(*Params, TEXT("demolish")))
	{
		UE_LOG(LogRedHopeSim, Display, TEXT("=== DEMOLISH TEST (BM-3, 2026-10-16) ==="));
		const int32 StepsPerSolH = (int32)(URHSimClockSubsystem::SolLengthSimSeconds / URHSimClockSubsystem::EraStepSimSeconds);
		int32 Fails = 0;
		const auto Expect = [&](bool bOk, const FString& What)
		{
			if (bOk) { UE_LOG(LogRedHopeSim, Display, TEXT("DEMOLISH ok: %s"), *What); }
			else { UE_LOG(LogRedHopeSim, Error, TEXT("DEMOLISH FAIL: %s"), *What); ++Fails; }
		};
		const auto Step = [&]()
		{
			Sim->Debug_AddFreshWater(500.0 * URHSimClockSubsystem::EraStepSimSeconds / (double)URHSimClockSubsystem::SolLengthSimSeconds);
			Sim->Debug_SetBatteryWh(1.0e9);
			Clock->Debug_AdvanceSimSeconds(URHSimClockSubsystem::EraStepSimSeconds);
			Sim->EraStep(URHSimClockSubsystem::EraStepSimSeconds);
		};
		const auto RunSols = [&](double Sols) { for (int32 S = 0; S < (int32)(Sols * StepsPerSolH); ++S) { Step(); } };
		const auto Near = [](double A, double B, double Tol) { return FMath::Abs(A - B) <= Tol; };
		TArray<FString> Alerts, Rejects;
		TArray<FRHBuildingInstance> Removed;
		const FDelegateHandle AlertH = Sim->OnAlert.AddLambda([&Alerts](const FString& S) { Alerts.Add(S); });
		const FDelegateHandle RejectH = Sim->OnCommandRejected.AddLambda([&Rejects](const FRHCommand&, const FString& S) { Rejects.Add(S); });
		const FDelegateHandle RemovedH = Sim->OnBuildingRemoved.AddLambda([&Removed](const FRHBuildingInstance& B) { Removed.Add(B); });
		const auto LastAlert = [&Alerts]() { return Alerts.Num() ? Alerts.Last() : FString(); };
		const auto LastReject = [&Rejects]() { return Rejects.Num() ? Rejects.Last() : FString(); };
		const auto CountAlerts = [&Alerts](const TCHAR* Sub) { int32 N = 0; for (const FString& A : Alerts) { N += A.Contains(Sub) ? 1 : 0; } return N; };
		const auto Find = [&](int32 Id) -> const FRHBuildingInstance*
		{
			for (const FRHBuildingInstance& B : Sim->GetBuildings()) { if (B.Id == Id) { return &B; } }
			return nullptr;
		};
		// Buildings stay Id-ascending: the last match is the newest.
		const auto LastIdOf = [&](const TCHAR* Def) { int32 Id = 0; for (const FRHBuildingInstance& B : Sim->GetBuildings()) { if (B.DefName == FName(Def)) { Id = B.Id; } } return Id; };
		const auto Place = [&](const TCHAR* Def, double X, double Y, int32 SX = 1, int32 SY = 1, int32 Level = 0)
		{
			Sim->Debug_PlaceInstant(FName(Def), FVector(X, Y, 0.0), Level, SX, SY);
			return LastIdOf(Def);
		};
		const auto Order = [&](const TCHAR* Def, double X, double Y)
		{
			const int32 Before = LastIdOf(Def);
			FRHCommand C; C.Verb = FName("Build"); C.Target = FName(Def); C.Location = FVector(X, Y, 0.0); C.Level = 0;
			Sim->EnqueueCommand(C);
			Step();
			const int32 Id = LastIdOf(Def);
			return Id != Before ? Id : 0;
		};
		const auto DemoCmd = [](int32 Id) { FRHCommand C; C.Verb = FName("Demolish"); C.Target = FName("Building"); C.Value = (double)Id; return C; };
		// One order, one step (every lag here is under the 60 s era step).
		// True when it was not rejected.
		const auto Demo = [&](int32 Id)
		{
			const int32 R0 = Rejects.Num();
			Sim->EnqueueCommand(DemoCmd(Id));
			Step();
			return Rejects.Num() == R0;
		};
		const int32 LanderId = LastIdOf(TEXT("Lander"));
		const auto LanderKg = [&](const TCHAR* Res, bool bOut)
		{
			const FRHBuildingInstance* L = Find(LanderId);
			return L ? (bOut ? L->OutputKg : L->InputKg).FindRef(FName(Res)) : 0.0;
		};
		const auto LanderAll = [&](const TCHAR* Res) { return LanderKg(Res, false) + LanderKg(Res, true); };
		const FName NStruct(TEXT("Struct")), NOx(TEXT("Oxygen"));
		URHDefinitionsSubsystem* DefsSub = World->GetSubsystem<URHDefinitionsSubsystem>();
		FString Why, R;
		int32 MaxIdSeen = 0;
		const auto NoteIds = [&]() { for (const FRHBuildingInstance& B : Sim->GetBuildings()) { MaxIdSeen = FMath::Max(MaxIdSeen, B.Id); } };

		// D1. Data, and the world.
		Expect(LanderId != 0 && Near(Sim->GetSalvageFraction(), 0.5, 1e-9),
			FString::Printf(TEXT("D1 Lander #%d; SalvageFraction %.2f (DT/CSV)"), LanderId, Sim->GetSalvageFraction()));
		const FVector LanderLoc = Find(LanderId) ? Find(LanderId)->LocationCm : FVector::ZeroVector;
		const int32 Solar0 = Place(TEXT("SolarArray"), 3500.f, 1000.f); // an early, low-Id building (D12e)
		Place(TEXT("SolarArray"), 3500.f, 2000.f);
		Sim->AddStock(NOx, 6000.0);
		Sim->AddStock(FName("Water"), 4000.0);
		Sim->AddStock(FName("Food"), 6000.0);
		Step();

		// D2. Refusals: one rejection each, nothing removed.
		{
			const int32 Pylon = Place(TEXT("Pylon"), -2500.f, -2500.f);
			const int32 N0 = Sim->GetBuildings().Num();
			Expect(!Demo(LanderId) && LastReject().Contains(TEXT("anchor")), FString::Printf(TEXT("D2 the Lander is refused (%s)"), *LastReject()));
			Expect(!Demo(Pylon) && LastReject().Contains(TEXT("territory")), FString::Printf(TEXT("D2 a standing pylon is refused (%s)"), *LastReject()));
			Expect(!Demo(9999) && LastReject().Contains(TEXT("already gone")), FString::Printf(TEXT("D2 an unknown Id is refused (%s)"), *LastReject()));
			FRHCommand Bad = DemoCmd(Pylon); Bad.Target = FName("Pylon");
			const int32 R0 = Rejects.Num();
			Sim->EnqueueCommand(Bad);
			Step();
			Expect(Rejects.Num() == R0 + 1 && LastReject().Contains(TEXT("structure")), FString::Printf(TEXT("D2 a non-Building target is refused (%s)"), *LastReject()));
			Expect(Removed.Num() == 0 && Sim->GetBuildings().Num() == N0, TEXT("D2 four refusals: no removal event, no building lost"));
		}

		// D3. Cancel in the uplink is undo.
		const int32 Pile = Order(TEXT("Stockpile"), 1500.f, -1500.f);
		Expect(Pile != 0 && Find(Pile) && Find(Pile)->bUnderConstruction, TEXT("D3 a Stockpile order lands as a site"));
		{
			Sim->EnqueueCommand(DemoCmd(Pile));
			const bool bCancelled = Sim->GetUplinkQueue().Num() > 0 && Sim->CancelUplinkCommand(Sim->GetUplinkQueue().Last().CommandId);
			Step();
			Expect(bCancelled && Find(Pile) && Removed.Num() == 0, TEXT("D3 a cancelled Demolish never lands: the site stands"));
		}

		// D4. A cancelled site returns what was delivered; its build task goes.
		{
			const double Struct0 = Sim->GetTotalSolid(NStruct), Lander0 = LanderAll(TEXT("Struct"));
			Sim->Debug_StepTaskBoard();
			FRHTask Haul, Build;
			const FMassEntityHandle Hauler(900001, 1), Fab(900002, 1);
			const bool bClaim = Sim->TryClaimHaul(Hauler, LanderLoc, Haul) && Haul.To.BuildingId == Pile && Haul.Resource == NStruct;
			float Loaded = 0.f; FVector Drop;
			const bool bLoad = bClaim && Sim->HaulLoad(Haul.Id, 20.f, Loaded, Drop) && Near(Loaded, 20.0, 1e-3);
			Sim->HaulUnload(Haul.Id, Loaded);
			const bool bBuild = Sim->TryClaimBuild(Fab, Find(Pile)->LocationCm, Build) && Build.To.BuildingId == Pile;
			Expect(bClaim && bLoad && bBuild && Near(Find(Pile)->InputKg.FindRef(NStruct), 20.0, 1e-3) && Near(LanderAll(TEXT("Struct")), Lander0 - 20.0, 1e-3),
				TEXT("D4 20 kg Struct hauled Lander -> site; a fabricator claims the build"));
			Expect(Demo(Pile) && !Find(Pile), FString::Printf(TEXT("D4 the site is demolished (%s)"), *LastAlert()));
			FRHTask Gone;
			Expect(Near(LanderAll(TEXT("Struct")), Lander0, 1e-3) && Near(Sim->GetTotalSolid(NStruct), Struct0, 1e-3),
				FString::Printf(TEXT("D4 the 20 kg is back at the Lander (%.1f, expect %.1f); colony Struct unchanged"), LanderAll(TEXT("Struct")), Lander0));
			Expect(!Sim->Debug_GetTask(Build.Id, Gone) && Sim->ApplyBuildWork(Pile, 10.0), TEXT("D4 the claimed build task is gone; the fabricator's work reports done"));
			Expect(Removed.Num() == 1 && Removed.Last().Id == Pile && Removed.Last().bUnderConstruction && LastAlert().Contains(TEXT("SITE CANCELLED")),
				FString::Printf(TEXT("D4 exactly one removal event, flagged as a site (%s)"), *LastAlert()));
		}

		// D5. Cargo in flight to a demolished site comes home to the Lander.
		{
			const int32 Pad = Order(TEXT("ChargePad"), -1500.f, -1500.f);
			const double Struct0 = Sim->GetTotalSolid(NStruct), Lander0 = LanderAll(TEXT("Struct"));
			Sim->Debug_StepTaskBoard();
			FRHTask Haul;
			const FMassEntityHandle Hauler(900003, 1);
			float Loaded = 0.f; FVector Drop;
			const bool bOk = Pad && Sim->TryClaimHaul(Hauler, LanderLoc, Haul) && Haul.To.BuildingId == Pad && Sim->HaulLoad(Haul.Id, 60.f, Loaded, Drop) && Near(Loaded, 60.0, 1e-3);
			Expect(bOk && Near(Sim->GetTotalSolid(NStruct), Struct0 - 60.0, 1e-3), TEXT("D5 a hauler carries 60 kg Struct toward the pad site"));
			Expect(Demo(Pad) && !Find(Pad), TEXT("D5 the pad site is demolished"));
			FRHTask Now;
			Expect(Sim->Debug_GetTask(Haul.Id, Now) && Now.To.BuildingId == LanderId && Now.ClaimedBy == Hauler,
				TEXT("D5 the claimed haul survives, rerouted to the Lander"));
			Sim->HaulUnload(Haul.Id, Loaded);
			Expect(Near(LanderAll(TEXT("Struct")), Lander0, 1e-3) && Near(Sim->GetTotalSolid(NStruct), Struct0, 1e-3) && !Sim->Debug_GetTask(Haul.Id, Now),
				FString::Printf(TEXT("D5 unloaded at the Lander: Lander %.1f (expect %.1f), colony Struct unchanged"), LanderAll(TEXT("Struct")), Lander0));
		}

		// D6. A standing structure: held stock in full + half the bill.
		{
			const int32 Forge = Place(TEXT("Forge"), 2000.f, 1000.f);
			Sim->SetManualPower(Forge, false);
			const double Struct0 = Sim->GetTotalSolid(NStruct), Lander0 = LanderAll(TEXT("Struct")), Rego0 = LanderKg(TEXT("Regolith"), false);
			const double LIn0 = LanderKg(TEXT("Struct"), false), LOut0 = LanderKg(TEXT("Struct"), true);
			Sim->Debug_AddSolidAt(Forge, FName("Regolith"), 300.0, false);
			Sim->Debug_AddSolidAt(Forge, NStruct, 150.0, true);
			TMap<FName, double> Salvage;
			Why.Reset();
			Expect(Sim->CanDemolishBuilding(Forge, Why, &Salvage) && Near(Salvage.FindRef(NStruct), 275.0, 1e-3) && Near(Salvage.FindRef(FName("Regolith")), 300.0, 1e-3),
				FString::Printf(TEXT("D6 the card's preview: Struct %.0f (150 held + 125 hull), Regolith %.0f"), Salvage.FindRef(NStruct), Salvage.FindRef(FName("Regolith"))));
			Sim->Debug_StepTaskBoard();
			FRHTask Haul;
			const FMassEntityHandle Hauler(900004, 1);
			const bool bClaim = Sim->TryClaimHaul(Hauler, Find(Forge)->LocationCm, Haul) && Haul.From.BuildingId == Forge;
			Expect(Demo(Forge) && !Find(Forge), FString::Printf(TEXT("D6 the Forge is demolished (%s)"), *LastAlert()));
			Expect(Near(Sim->GetTotalSolid(NStruct), Struct0 + 150.0 + 125.0, 1e-3) && Near(LanderAll(TEXT("Struct")), Lander0 + 275.0, 1e-3)
				&& Near(LanderKg(TEXT("Struct"), true), LOut0 + 275.0, 1e-3) && Near(LanderKg(TEXT("Struct"), false), LIn0, 1e-3),
				FString::Printf(TEXT("D6 Lander Struct +%.1f on its output shelf; colony Struct +125 net of the held 150"), LanderAll(TEXT("Struct")) - Lander0));
			Expect(LanderKg(TEXT("Regolith"), false) >= Rego0 + 300.0 - 1e-3, TEXT("D6 the hopper's 300 kg Regolith is back on the Lander's input shelf"));
			float Loaded = 0.f; FVector Drop; FRHTask Now;
			Expect(bClaim && !Sim->HaulLoad(Haul.Id, 200.f, Loaded, Drop) && !Sim->Debug_GetTask(Haul.Id, Now),
				TEXT("D6 a claimed haul FROM the Forge finds nothing at load time and clears itself"));
		}

		// D7. The last Compute Module: the uplink falls back to tier 0.
		{
			const double Lag0 = Sim->GetOrderLagSeconds();
			Sim->Debug_AddSolidAt(LanderId, FName("Shielding"), 20.0, false);
			const int32 Cm = Order(TEXT("ComputeModule"), -500.f, -2500.f);
			Sim->Debug_AddSolidAt(Cm, NStruct, 100.0, false);
			Sim->Debug_AddSolidAt(Cm, FName("Shielding"), 20.0, false);
			Expect(Cm && Sim->ApplyBuildWork(Cm, 1.0e6) && Sim->GetOrderLagSeconds() < Lag0,
				FString::Printf(TEXT("D7 the module completes: lag %.0f -> %.0f s"), Lag0, Sim->GetOrderLagSeconds()));
			const double S0 = LanderKg(TEXT("Struct"), true), Sh0 = LanderKg(TEXT("Shielding"), true);
			Expect(Demo(Cm) && Near(Sim->GetOrderLagSeconds(), Lag0, 1e-6) && LastAlert().Contains(TEXT("Order lag")),
				FString::Printf(TEXT("D7 demolished: lag back to %.0f s (%s)"), Sim->GetOrderLagSeconds(), *LastAlert()));
			Expect(Near(LanderKg(TEXT("Struct"), true) - S0, 50.0, 1e-3) && Near(LanderKg(TEXT("Shielding"), true) - Sh0, 10.0, 1e-3),
				TEXT("D7 salvage: half of Struct 100 + the surface Shielding 20"));
		}

		// D8. Imports and storage: a cancelled kit goes back in its crate; a
		// standing bank salvages nothing and takes its capacity with it.
		{
			const int32 Kits0 = Sim->GetImportStock(FName("SolarArray"));
			const int32 Site = Order(TEXT("SolarArray"), 2500.f, -500.f);
			Expect(Site && Sim->GetImportStock(FName("SolarArray")) == Kits0 - 1, TEXT("D8 a Solar Array order spends a kit"));
			Expect(Demo(Site) && Sim->GetImportStock(FName("SolarArray")) == Kits0 && LastAlert().Contains(TEXT("kit")),
				FString::Printf(TEXT("D8 cancelled: the kit is back (%d) (%s)"), Sim->GetImportStock(FName("SolarArray")), *LastAlert()));
			const int32 Bank = Place(TEXT("BatteryBank"), -500.f, 3500.f);
			Step();
			const double Cap0 = Sim->GetPower().BatteryCapWh;
			const int32 Banks0 = Sim->GetImportStock(FName("BatteryBank"));
			TMap<FName, double> Salvage;
			Why.Reset();
			Expect(Sim->CanDemolishBuilding(Bank, Why, &Salvage) && Salvage.Num() == 0, TEXT("D8 a standing bank previews no salvage"));
			Expect(Demo(Bank) && Near(Sim->GetPower().BatteryCapWh, Cap0 - 4000.0, 1e-6) && Sim->GetImportStock(FName("BatteryBank")) == Banks0
				&& Sim->GetPower().BatteryWh <= Sim->GetPower().BatteryCapWh + 1e-6,
				FString::Printf(TEXT("D8 the bank's 4000 Wh leaves the grid (%.0f -> %.0f); no kit back"), Cap0, Sim->GetPower().BatteryCapWh));
		}

		// D9. A pad demolished under a docked robot.
		{
			const int32 Pad = Place(TEXT("ChargePad"), 500.f, -2500.f);
			Step();
			const FMassEntityHandle Bot(900005, 1);
			Sim->JoinPadQueue(Pad, Bot);
			Expect(Sim->RequestChargeWh(Pad, 1.0, Bot) > 0.0, TEXT("D9 the docked robot draws from the pad"));
			Expect(Demo(Pad) && !Sim->HasBuilding(Pad) && Sim->RequestChargeWh(Pad, 1.0, Bot) == 0.0, TEXT("D9 demolished: the pad gives nothing"));
			int32 Dock = 0; FVector DockLoc;
			Expect(Sim->FindNearestChargePad(LanderLoc, Dock, DockLoc) && Dock == LanderId, TEXT("D9 the next dock is the Lander"));
		}

		// D10. A planted dome: the beds go with it, the soil is forfeit.
		{
			const int32 Dome = Place(TEXT("GreenhouseDome"), 3500.f, -2500.f, 2, 1);
			Sim->AddStock(FName("Soil"), 2000.0);
			Sim->AddStock(FName("Seeds"), 200.0);
			Sim->AddStock(FName("Glass"), 400.0);
			Step();
			const int32 Planted0 = Sim->GetPlantedCellCount();
			Expect(Sim->IsDomeBedPlanted(Dome, 0) && Sim->IsDomeBedPlanted(Dome, 1), FString::Printf(TEXT("D10 both beds planted (%d planted)"), Planted0));
			Expect(Demo(Dome) && !Sim->IsDomeBedPlanted(Dome, 0) && !Sim->IsDomeBedPlanted(Dome, 1) && Sim->GetPlantedCellCount() == Planted0 - 2
				&& LastAlert().Contains(TEXT("forfeit")), FString::Printf(TEXT("D10 the beds are cleared (%s)"), *LastAlert()));
		}

		// D12. Habitats. The S5/S6 layout: A + B + C (2x1), a filter in A.
		Sim->AddStock(FName("Shielding"), 1000.0);
		const int32 A = Place(TEXT("HabModule"), -1000, 1000);
		const int32 B = Place(TEXT("HabModule"), -2000, 1000);
		const int32 Cc = Place(TEXT("HabModule"), -1500, 2000, 2, 1);
		const int32 AFA = Place(TEXT("AirFilter"), -1000, 1000);
		RunSols(1.0);
		Sim->Debug_SetVaultRated(true);
		Expect(Sim->DesignateBay(A, 0, FName("LivingQuarters"), Why) && Sim->IsHabRated(A) && Sim->Debug_AddColonists(1) == 1
			&& Sim->GetColonists().Last().HomeHab == A, FString::Printf(TEXT("D12 a rated 4-bay network; one resident in A (%s)"), *Why));
		const int32 Resident = Sim->GetColonists().Last().Id;
		Expect(!Demo(A) && LastReject().Contains(TEXT("live in")), FString::Printf(TEXT("D12a A is home: refused (%s)"), *LastReject()));
		const int32 Mast = Place(TEXT("Floodmast"), -2000, 1000);
		Expect(!Demo(B) && LastReject().Contains(TEXT("stand inside")) && LastReject().Contains(TEXT("Floodmast")),
			FString::Printf(TEXT("D12b a machine inside B: refused (%s)"), *LastReject()));
		Expect(Demo(Mast) && !Find(Mast), TEXT("D12b the Floodmast itself comes down"));
		Expect(!Demo(B) && LastReject().Contains(TEXT("bay minimum")), FString::Printf(TEXT("D12c without B the resident's network is 3 bays: refused (%s)"), *LastReject()));
		Expect(!Demo(AFA) && LastReject().Contains(TEXT("only air circulator")), FString::Printf(TEXT("D12d the network's only filter: refused (%s)"), *LastReject()));

		// D12e. The index caches: a module in C:0, then a LOWER-Id building goes.
		DefsSub->ActivateRoomRow(FName("LabFull"));
		const int32 Lab = Place(TEXT("LabModule"), -2000, 2000);
		RunSols(0.05);
		const FIntVector LabKey(0, INDEX_NONE, Lab);
		FIntVector Station;
		int32 Filled = 0, Cap = 0;
		Sim->GetStationSeats(LabKey, Filled, Cap);
		Expect(Sim->GetColonistStation(Resident, Station) && Station == LabKey && Cap == 2 && Filled == 1,
			FString::Printf(TEXT("D12e the resident works the Lab Module (%d/%d seats)"), Filled, Cap));
		Expect(Solar0 < Lab && Demo(Solar0) && !Find(Solar0), TEXT("D12e a lower-Id Solar Array is demolished (Buildings indices shift)"));
		RunSols(0.2);
		Filled = 0; Cap = 0;
		Sim->GetStationSeats(LabKey, Filled, Cap);
		Expect(Sim->GetColonistStation(Resident, Station) && Station == LabKey && Cap == 2 && Filled == 1 && Sim->IsHabNetworkCirculated(A),
			FString::Printf(TEXT("D12e after the shift: same station, %d/%d seats, still circulated"), Filled, Cap));

		// D12f. An allowed removal from a lived-in network.
		const int32 D = Place(TEXT("HabModule"), -3000, 1000);
		Expect(Sim->GetHabNetworkBays(A) == 5 && Sim->DesignateBay(D, 0, FName("Lab"), Why), FString::Printf(TEXT("D12f D joins (5 bays) and is zoned Lab (%s)"), *Why));
		{
			FRHCommand As; As.Verb = FName("Assign"); As.Level = 0; As.Location = FVector(-3000.f, 1000.f, 0.f);
			Sim->EnqueueCommand(As);
		}
		RunSols(1.0);
		const FIntVector DKey(0, 0, D);
		Expect(Sim->GetSeatPriorityRank(DKey) == 0 && Near(Sim->GetHabO2Kg(D), 100.0, 0.5),
			FString::Printf(TEXT("D12f D:0 staffed first, D filled (%.1f kg)"), Sim->GetHabO2Kg(D)));
		{
			const double Ox0 = Sim->GetStock(NOx);
			Expect(Demo(D) && !Find(D), FString::Printf(TEXT("D12f D is demolished (%s)"), *LastAlert()));
			Expect(Sim->GetStock(NOx) >= Ox0 + 70.0 && LastAlert().Contains(TEXT("air pumped back")),
				FString::Printf(TEXT("D12f its air returns to the tanks (+%.1f kg)"), Sim->GetStock(NOx) - Ox0));
		}
		Expect(Sim->GetSeatPriorityRank(DKey) == INDEX_NONE && Sim->GetBayRoom(D, 0).IsNone() && Sim->GetHabO2Kg(D) == 0.0 && Sim->GetHabNetworkBays(A) == 4,
			TEXT("D12f D's priority, zoning and fill are gone; the network is 4 bays"));
		RunSols(0.5);
		Expect(Sim->IsHabRated(A) && Sim->GetColonists().Num() == 1 && Sim->GetColonists()[0].bSupported, TEXT("D12f the network keeps its rating; the resident is supported"));

		// D12g. A split: two 2-bay halves, each with a filter, joined by a
		// 1-bay corridor. The corridor goes; both halves drop below the minimum.
		const int32 E = Place(TEXT("HabModule"), -1500, -5000, 2, 1);
		const int32 K = Place(TEXT("HabCorridor"), 0, -5000);
		const int32 F = Place(TEXT("HabModule"), 1500, -5000, 2, 1);
		Place(TEXT("AirFilter"), -2000, -5000);
		Place(TEXT("AirFilter"), 2000, -5000);
		RunSols(1.5);
		Expect(Sim->GetHabNetworkBays(E) == 5 && Sim->IsHabRated(E) && Sim->IsHabRated(F), FString::Printf(TEXT("D12g E+K+F rate as one 5-bay network (%d)"), Sim->GetHabNetworkBays(E)));
		{
			const int32 Lost0 = CountAlerts(TEXT("below the habitat minimum"));
			Expect(Demo(K) && Sim->GetHabNetworkBays(E) == 2 && Sim->GetHabNetworkBays(F) == 2 && Sim->GetHabNetworkRoot(F) == F,
				TEXT("D12g the corridor goes: two 2-bay networks"));
			Expect(!Sim->IsHabRated(E) && !Sim->IsHabRated(F) && CountAlerts(TEXT("below the habitat minimum")) == Lost0 + 2,
				TEXT("D12g both halves lose the rating in the same step, one alert each"));
		}

		// D13. Underground: the last filter of a lived-in floor stays.
		Sim->ExtendShaft(1, FVector(3000.f, 3000.f, 0.f));
		Sim->ExcavateFloor(-1, 4, R);
		const int32 AF1 = Place(TEXT("AirFilter"), 3000, 3500, 1, 1, -1);
		Sim->Debug_ForceRateFloor(-1); // needs the floor circulated first
		Step();
		Expect(Sim->Debug_AddColonists(1) == 1 && Sim->GetColonists().Last().HomeLevel == -1 && Sim->GetColonists().Last().HomeHab == 0,
			TEXT("D13 a colonist lands on floor -1"));
		Expect(!Demo(AF1) && LastReject().Contains(TEXT("only air circulator")), FString::Printf(TEXT("D13 the floor's only filter: refused (%s)"), *LastReject()));
		Place(TEXT("AirFilter"), 2500, 3500, 1, 1, -1);
		Expect(Demo(AF1) && !Find(AF1), TEXT("D13 with a second filter the first comes down"));

		// D11. The Borer stays once the shaft exists.
		const int32 Borer = Place(TEXT("Borer"), 3500, -4000);
		Expect(!Demo(Borer) && LastReject().Contains(TEXT("shaft")), FString::Printf(TEXT("D11 the Borer is refused (%s)"), *LastReject()));

		// The newest building ever made goes too, so D14's Id check bites.
		const int32 Tail = Place(TEXT("Stockpile"), -4000, -2000);
		Expect(Demo(Tail) && !Find(Tail), TEXT("D14 the newest building is demolished before the save"));

		// D15. A stale order: Demolish then Assign the same module, one step.
		{
			const int32 R0 = Rejects.Num();
			Sim->EnqueueCommand(DemoCmd(Lab));
			FRHCommand As; As.Verb = FName("Assign"); As.Target = FName("Building"); As.Value = (double)Lab;
			Sim->EnqueueCommand(As);
			Step();
			bool bNoKey = true;
			for (const FIntVector& Key : Sim->GetSeatPriority()) { bNoKey &= Key.Z != Lab; }
			Expect(!Find(Lab) && Rejects.Num() == R0 + 1 && LastReject().Contains(TEXT("Not a work station")) && bNoKey,
				FString::Printf(TEXT("D15 the module goes; the Assign behind it is rejected (%s)"), *LastReject()));
		}

		// D14. Save/load: the same buildings, Ids never reused.
		{
			NoteIds();
			for (const FRHBuildingInstance& X : Removed) { MaxIdSeen = FMath::Max(MaxIdSeen, X.Id); }
			TArray<int32> Ids0;
			for (const FRHBuildingInstance& X : Sim->GetBuildings()) { Ids0.Add(X.Id); }
			FString Err;
			Expect(Sim->SaveColony(TEXT("demolishtest"), Err) && Sim->LoadColony(TEXT("demolishtest"), Err) && Err.IsEmpty(), FString::Printf(TEXT("D14 save/load (%s)"), *Err));
			TArray<int32> Ids1;
			for (const FRHBuildingInstance& X : Sim->GetBuildings()) { Ids1.Add(X.Id); }
			bool bAbsent = true;
			for (const FRHBuildingInstance& X : Removed) { bAbsent &= !Ids1.Contains(X.Id); }
			Expect(Ids0 == Ids1 && bAbsent, FString::Printf(TEXT("D14 the same %d buildings; the %d demolished stay gone"), Ids1.Num(), Removed.Num()));
			const int32 Next = Place(TEXT("Stockpile"), -4000, -2000);
			Expect(MaxIdSeen == Tail && Next == Tail + 1, FString::Printf(TEXT("D14 the next Id is %d after demolished #%d - never reused"), Next, Tail));
		}
		Expect(Removed.Num() == 15, FString::Printf(TEXT("D one removal event per demolish: %d (expect 15)"), Removed.Num()));

		Sim->OnAlert.Remove(AlertH);
		Sim->OnCommandRejected.Remove(RejectH);
		Sim->OnBuildingRemoved.Remove(RemovedH);
		UE_LOG(LogRedHopeSim, Display, TEXT("=== DEMOLISH TEST END: %d failure(s) ==="), Fails);
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return Fails > 0 ? 1 : 0;
	}

	// BM-4 self-test (2026-10-16): FURNITURE - the catalogue rows, placement
	// refusals, beds that replace a Living cell's legacy bed, desks and benches
	// that replace a station's seats, the capped Furnishings Hope line, the bed
	// guard, re-zoning, refunds, surface bays, demolish with furniture inside,
	// derived bed / seat assignments and save v30. Fails loudly. `-furnish`.
	if (FParse::Param(*Params, TEXT("furnish")))
	{
		UE_LOG(LogRedHopeSim, Display, TEXT("=== FURNITURE TEST (BM-4, 2026-10-16) ==="));
		const int32 StepsPerSolH = (int32)(URHSimClockSubsystem::SolLengthSimSeconds / URHSimClockSubsystem::EraStepSimSeconds);
		int32 Fails = 0;
		const auto Expect = [&](bool bOk, const FString& What)
		{
			if (bOk) { UE_LOG(LogRedHopeSim, Display, TEXT("FURNISH ok: %s"), *What); }
			else { UE_LOG(LogRedHopeSim, Error, TEXT("FURNISH FAIL: %s"), *What); ++Fails; }
		};
		const auto Step = [&]()
		{
			Sim->Debug_AddFreshWater(500.0 * URHSimClockSubsystem::EraStepSimSeconds / (double)URHSimClockSubsystem::SolLengthSimSeconds);
			Sim->Debug_SetBatteryWh(1.0e9);
			Clock->Debug_AdvanceSimSeconds(URHSimClockSubsystem::EraStepSimSeconds);
			Sim->EraStep(URHSimClockSubsystem::EraStepSimSeconds);
		};
		const auto RunSols = [&](double Sols) { for (int32 S = 0; S < (int32)(Sols * StepsPerSolH); ++S) { Step(); } };
		const auto Near = [](double A, double B, double Tol) { return FMath::Abs(A - B) <= Tol; };
		TArray<FString> Alerts, Rejects;
		TArray<FIntVector> Changed;
		const FDelegateHandle AlertH = Sim->OnAlert.AddLambda([&Alerts](const FString& S) { Alerts.Add(S); });
		const FDelegateHandle RejectH = Sim->OnCommandRejected.AddLambda([&Rejects](const FRHCommand&, const FString& S) { Rejects.Add(S); });
		const FDelegateHandle ChangedH = Sim->OnFurnitureChanged.AddLambda([&Changed](const FIntVector& K) { Changed.Add(K); });
		const auto LastReject = [&Rejects]() { return Rejects.Num() ? Rejects.Last() : FString(); };
		const auto LastIdOf = [&](const TCHAR* Def) { int32 Id = 0; for (const FRHBuildingInstance& B : Sim->GetBuildings()) { if (B.DefName == FName(Def)) { Id = B.Id; } } return Id; };
		const auto Place = [&](const TCHAR* Def, double X, double Y, int32 SX = 1, int32 SY = 1, int32 Level = 0)
		{
			Sim->Debug_PlaceInstant(FName(Def), FVector(X, Y, 0.0), Level, SX, SY);
			return LastIdOf(Def);
		};
		const int32 LanderId = LastIdOf(TEXT("Lander"));
		const auto LanderKg = [&](const TCHAR* Res, bool bOut)
		{
			for (const FRHBuildingInstance& B : Sim->GetBuildings()) { if (B.Id == LanderId) { return (bOut ? B.OutputKg : B.InputKg).FindRef(FName(Res)); } }
			return 0.0;
		};
		const FName NStruct(TEXT("Struct"));
		// Orders: one order, one step (lag 45 s < the 60 s era step).
		const auto FurnishCmd = [&](const TCHAR* Row, const FIntVector& Key, int32 Slot, int32 Rot)
		{
			FRHCommand C; C.Verb = FName("Furnish"); C.Target = FName(Row); C.Level = Key.Z == 0 ? Key.X : 0;
			C.Location = Sim->GetCellCentreCm(Key);
			C.Value = URHSimWorldSubsystem::EncodeFurnish(Key.Z == 0 ? Key.Y : 0, Slot, Rot);
			return C;
		};
		const auto Furnish = [&](const TCHAR* Row, const FIntVector& Key, int32 Slot, int32 Rot = 0)
		{
			const int32 R0 = Rejects.Num();
			Sim->EnqueueCommand(FurnishCmd(Row, Key, Slot, Rot));
			Step();
			return Rejects.Num() == R0;
		};
		const auto SellPiece = [&](int32 Id)
		{
			const int32 R0 = Rejects.Num();
			FRHCommand C; C.Verb = FName("Sell"); C.Target = FName("Furniture"); C.Value = (double)Id;
			Sim->EnqueueCommand(C);
			Step();
			return Rejects.Num() == R0;
		};
		const auto SellCell = [&](const FIntVector& Key)
		{
			const int32 R0 = Rejects.Num();
			FRHCommand C; C.Verb = FName("Sell"); C.Target = FName("Cell"); C.Level = Key.Z == 0 ? Key.X : 0;
			C.Location = Sim->GetCellCentreCm(Key);
			C.Value = (double)(Key.Z == 0 ? Key.Y : 0);
			Sim->EnqueueCommand(C);
			Step();
			return Rejects.Num() == R0;
		};
		const auto LastPieceId = [&]() { return Sim->GetFurniture().Num() ? Sim->GetFurniture().Last().Id : 0; };
		const auto Seats = [&](const FIntVector& Key) { int32 F = 0, Cap = 0; Sim->GetStationSeats(Key, F, Cap); return FIntPoint(F, Cap); };
		const auto CanPlace = [&](const TCHAR* Row, const FIntVector& Key, int32 Slot, int32 Rot, FString& Why)
		{
			Why.Reset();
			return Sim->CanPlaceFurniture(FName(Row), Key, Slot, Rot, Why);
		};
		URHDefinitionsSubsystem* DefsSub = World->GetSubsystem<URHDefinitionsSubsystem>();
		FString Why, R;

		// F0. Pure data (fail fast: no table, no layer).
		if (!DefsSub->HasFurnitureCatalogue())
		{
			UE_LOG(LogRedHopeSim, Error, TEXT("FURNISH FAIL: DT_Furniture missing (create + sync it)"));
			return 1;
		}
		{
			int32 Rows = 0, Active = 0;
			bool bValid = true;
			DefsSub->ForEachFurnitureRow([&](FName Name, const FRHFurnitureRow& Row)
			{
				++Rows;
				Active += Row.SliceActive ? 1 : 0;
				const bool bFunctional = Row.Provides == FName("Bed") || Row.Provides == FName("JobSeat") || Row.Provides == FName("DiningSeat");
				bool bRow = (Row.Capacity >= 1) == bFunctional && Row.SlotsX >= 1 && Row.SlotsX <= 4 && Row.SlotsY >= 1 && Row.SlotsY <= 4 && Row.HopeComfort >= 0.f;
				const TMap<FName, double> Bill = URHDefinitionsSubsystem::GetFurnitureCost(Row);
				bRow &= Bill.Num() == 1 && Bill.Contains(FName("Struct"));
				if (Row.Provides == FName("Bed"))
				{
					bRow &= Row.RoomFunctions == TEXT("Living");
				}
				if (Row.Provides == FName("JobSeat"))
				{
					TArray<FString> Fns;
					Row.RoomFunctions.ParseIntoArray(Fns, TEXT(";"), true);
					for (const FString& Fn : Fns) { bRow &= Fn == TEXT("Lab") || Fn == TEXT("Workstation"); }
				}
				if (!bRow) { UE_LOG(LogRedHopeSim, Error, TEXT("FURNISH: row %s breaks a data rule"), *Name.ToString()); }
				bValid &= bRow;
			});
			const FRHFurnitureRow* Cot = DefsSub->GetFurniture(FName("Cot"));
			const FRHFurnitureRow* Desk = DefsSub->GetFurniture(FName("Desk"));
			Expect(Rows == 13 && Active == 13 && bValid && Cot && Cot->Capacity == 1 && Cot->SlotsX == 2 && Desk && Desk->Provides == FName("JobSeat"),
				FString::Printf(TEXT("F0 DT_Furniture: %d rows, %d active, every row within the data rules (DT/CSV)"), Rows, Active));
			Expect(Near(Sim->GetFurnitureRefundFraction(), 0.75, 1e-9) && Near(Sim->GetHopeFurnishingsMax(), 6.0, 1e-9),
				TEXT("F0 config: refund 0.75 (ruling 5a), Furnishings cap 6"));
			FRHFurnitureRow Statue; Statue.DisplayName = TEXT("Statue"); Statue.Provides = FName("Comfort"); Statue.RoomFunctions = TEXT("Living");
			Statue.CostResources = TEXT("Struct:20"); Statue.SliceActive = false;
			DefsSub->Debug_InjectFurniture(FName("Statue"), Statue);
		}

		// Setup: a 7-cell rated floor, 7 colonists.
		Place(TEXT("SolarArray"), 3500.f, -1000.f);
		Place(TEXT("SolarArray"), 3500.f, -2000.f);
		Sim->AddStock(FName("Oxygen"), 6000.0);
		Sim->AddStock(FName("Water"), 4000.0);
		Sim->AddStock(FName("Food"), 6000.0);
		Sim->ExtendShaft(1, FVector(3000.f, 3000.f, 0.f));
		Sim->ExcavateFloor(-1, 7, R);
		const int32 AF = Place(TEXT("AirFilter"), 3000, 3500, 1, 1, -1);
		Sim->Debug_ForceRateFloor(-1);
		Step();
		const FIntVector C0(-1, 0, 0), C1(-1, 1, 0), C2(-1, 2, 0), C3(-1, 3, 0), C4(-1, 4, 0), C5(-1, 5, 0), C6(-1, 6, 0);
		Expect(Sim->DesignateRoom(-1, 0, FName("LivingQuarters"), R) && Sim->DesignateRoom(-1, 1, FName("Workstation"), R)
			&& Sim->DesignateRoom(-1, 2, FName("LivingQuarters"), R) && Sim->DesignateRoom(-1, 3, FName("WorkbenchLarge"), R)
			&& Sim->DesignateRoom(-1, 4, FName("Dining"), R) && Sim->DesignateRoom(-1, 5, FName("Lab"), R) && AF != 0,
			FString::Printf(TEXT("setup: 7 cells zoned c0 Living, c1 Workstation, c2 Living, c3 Large Workbench, c4 Dining, c5 Lab, c6 none (%s)"), *R));
		Expect(Sim->Debug_AddColonists(7) == 7 && Sim->GetPopulation() == 7, TEXT("setup: 7 colonists on floor -1"));
		Step();

		// F1. Legacy numbers with no furniture at all.
		{
			const URHSimWorldSubsystem::FRHHopeBreakdown H = Sim->GetColonyHope();
			Expect(Sim->GetHousingCapacity() == 7 && Seats(C1) == FIntPoint(1, 1) && Seats(C3) == FIntPoint(2, 2) && Seats(C5) == FIntPoint(1, 1)
				&& H.FilledSeats == 4 && Near(Sim->GetWorkstationYieldSeatSum(), 3.7, 1e-6) && Near(Sim->GetLabYieldSeatSum(), 1.0, 1e-6)
				&& Near(H.Housing, 15.0 * 2.0 / 7.0, 1e-6) && H.Furnishings == 0.0 && Sim->GetFurniture().Num() == 0,
				FString::Printf(TEXT("F1 legacy: capacity %d, seats c1/c3/c5 1/2/1, filled %d, workstation sum %.2f, housing %.4f, furnishings 0"),
					Sim->GetHousingCapacity(), H.FilledSeats, Sim->GetWorkstationYieldSeatSum(), H.Housing));
		}

		// F2. Refusals.
		Expect(!CanPlace(TEXT("Throne"), C0, 0, 0, Why) && Why.Contains(TEXT("catalogue")), FString::Printf(TEXT("F2 an unknown row (%s)"), *Why));
		Expect(!CanPlace(TEXT("Statue"), C0, 0, 0, Why) && Why.Contains(TEXT("not available")), FString::Printf(TEXT("F2 a dormant row (%s)"), *Why));
		Expect(!CanPlace(TEXT("Cot"), FIntVector(-1, 7, 0), 0, 0, Why) && Why.Contains(TEXT("carved")), FString::Printf(TEXT("F2 an uncarved cell (%s)"), *Why));
		Expect(!CanPlace(TEXT("Cot"), FIntVector(0, 0, 9999), 0, 0, Why) && Why.Contains(TEXT("bay")), FString::Printf(TEXT("F2 no such habitat bay (%s)"), *Why));
		Expect(!CanPlace(TEXT("Cot"), C1, 0, 0, Why) && Why.Contains(TEXT("Living")), FString::Printf(TEXT("F2 a cot in a Workstation (%s)"), *Why));
		Expect(!CanPlace(TEXT("Desk"), C0, 0, 0, Why) && Why.Contains(TEXT("Workstation")), FString::Printf(TEXT("F2 a desk in Living Quarters (%s)"), *Why));
		Expect(!CanPlace(TEXT("Plant"), C6, 0, 0, Why) && Why.Contains(TEXT("Zone")), FString::Printf(TEXT("F2 an unzoned cell (%s)"), *Why));
		Expect(!CanPlace(TEXT("Cot"), C0, 3, 0, Why) && Why.Contains(TEXT("fit")), FString::Printf(TEXT("F2 a 2x2 off the edge (%s)"), *Why));
		Expect(!CanPlace(TEXT("Workbench"), C3, 15, 1, Why) && Why.Contains(TEXT("fit")) && !CanPlace(TEXT("Plant"), C0, 0, 4, Why),
			TEXT("F2 a corner slot that can't hold a 2x2, and rotation 4, refused"));
		Expect(!CanPlace(TEXT("Cot"), C2, 0, 0, Why) && Why.Contains(TEXT("AirFilter")), FString::Printf(TEXT("F2 the filter's sub-cells in c2 (%s)"), *Why));
		Expect(CanPlace(TEXT("Cot"), C2, 8, 0, Why), FString::Printf(TEXT("F2 the far half of c2 is free (%s)"), *Why));
		{
			const double Struct0 = Sim->GetTotalSolid(NStruct);
			Sim->Debug_AddSolidAt(LanderId, NStruct, 30.0 - Struct0, false);
			Expect(!CanPlace(TEXT("Cot"), C0, 0, 0, Why) && Why.Contains(TEXT("Struct")), FString::Printf(TEXT("F2 30 kg Struct can't buy a 40 kg cot (%s)"), *Why));
			Expect(!Furnish(TEXT("Cot"), C0, 0) && Sim->GetFurniture().Num() == 0 && Near(Sim->GetTotalSolid(NStruct), 30.0, 1e-6),
				FString::Printf(TEXT("F2 the order is rejected when it lands, nothing spent (%s)"), *LastReject()));
			Sim->Debug_AddSolidAt(LanderId, NStruct, 2000.0 - 30.0, false);
		}
		const double StructStart = Sim->GetTotalSolid(NStruct);
		double Spent = 0.0, Refunded = 0.0;

		// F3. Buying beds.
		Changed.Reset();
		Expect(Furnish(TEXT("Cot"), C0, 0) && LastPieceId() == 1 && Near(Sim->GetTotalSolid(NStruct), StructStart - 40.0, 1e-6)
			&& Changed.Num() == 1 && Changed[0] == C0 && Sim->GetHousingCapacity() == 7,
			FString::Printf(TEXT("F3 cot #1 in c0: -40 Struct, one change event, capacity still %d (1 bed = the legacy bed)"), Sim->GetHousingCapacity()));
		Spent += 40.0;
		Sim->EnqueueCommand(FurnishCmd(TEXT("Cot"), C0, 2, 0));
		Expect(!CanPlace(TEXT("Cot"), C0, 2, 0, Why) && Why.Contains(TEXT("transit")), FString::Printf(TEXT("F3 a spot with an order in flight (%s)"), *Why));
		Step();
		Spent += 40.0;
		Expect(Sim->GetHousingCapacity() == 8 && Near(Sim->GetColonyHope().Housing, 15.0 * 3.0 / 7.0, 1e-6),
			FString::Printf(TEXT("F3 two cots: capacity 8, housing %.4f"), Sim->GetColonyHope().Housing));
		Expect(Furnish(TEXT("Cot"), C0, 8) && Furnish(TEXT("Cot"), C0, 10, 1), TEXT("F3 cots 3 and 4 (one turned)"));
		Spent += 80.0;
		Expect(Sim->GetHousingCapacity() == 10 && Near(Sim->GetColonyHope().Housing, 15.0 * 5.0 / 7.0, 1e-6) && Sim->GetCellBeds(C0) == 4,
			FString::Printf(TEXT("F3 four cots: capacity %d, housing %.4f, c0 beds %d"), Sim->GetHousingCapacity(), Sim->GetColonyHope().Housing, Sim->GetCellBeds(C0)));
		Expect(!CanPlace(TEXT("Cot"), C0, 5, 0, Why) && Why.Contains(TEXT("Overlaps")), FString::Printf(TEXT("F3 a fifth cot overlaps (%s)"), *Why));
		Expect(Sim->Debug_AddColonists(4) == 3 && Sim->GetPopulation() == 10 && Near(Sim->GetColonyHope().Housing, 7.5, 1e-6),
			FString::Printf(TEXT("F3 three more colonists fill the beds (pop %d, housing %.2f)"), Sim->GetPopulation(), Sim->GetColonyHope().Housing));

		// F4. The bed guard.
		{
			TMap<FName, double> Refund;
			Expect(!Sim->CanSellFurniture(3, Why, &Refund) && Why.Contains(TEXT("sleep")), FString::Printf(TEXT("F4 selling a needed bed is refused (%s)"), *Why));
			Expect(!SellPiece(3) && !SellCell(C0) && Sim->GetFurniture().Num() == 4, FString::Printf(TEXT("F4 the order and a bulk sell are refused too (%s)"), *LastReject()));
		}

		// F5. Seats.
		Expect(Furnish(TEXT("Desk"), C1, 0) && Seats(C1) == FIntPoint(1, 1), TEXT("F5 one desk in c1: still 1 seat"));
		Expect(Furnish(TEXT("Desk"), C1, 2) && Seats(C1) == FIntPoint(2, 2) && Sim->GetColonyHope().FilledSeats == 5 && Near(Sim->GetWorkstationYieldSeatSum(), 4.7, 1e-6),
			FString::Printf(TEXT("F5 two desks in c1: 2 seats, filled %d, workstation sum %.2f"), Sim->GetColonyHope().FilledSeats, Sim->GetWorkstationYieldSeatSum()));
		Expect(Furnish(TEXT("Desk"), C3, 0) && Seats(C3) == FIntPoint(1, 1) && Sim->GetColonyHope().FilledSeats == 4 && Near(Sim->GetWorkstationYieldSeatSum(), 3.35, 1e-6),
			FString::Printf(TEXT("F5 one desk in the Large Workbench room REPLACES its 2 seats with 1 (filled %d, sum %.2f)"), Sim->GetColonyHope().FilledSeats, Sim->GetWorkstationYieldSeatSum()));
		Expect(Furnish(TEXT("Workbench"), C3, 8, 1) && Seats(C3) == FIntPoint(3, 3) && Sim->GetColonyHope().FilledSeats == 6 && Near(Sim->GetWorkstationYieldSeatSum(), 6.05, 1e-6),
			FString::Printf(TEXT("F5 + a workbench: c3 seats 3 (filled %d, sum %.2f)"), Sim->GetColonyHope().FilledSeats, Sim->GetWorkstationYieldSeatSum()));
		Expect(Furnish(TEXT("LabBench"), C5, 0) && Seats(C5) == FIntPoint(2, 2) && Near(Sim->GetLabYieldSeatSum(), 2.0, 1e-6)
			&& Sim->GetColonyHope().FilledSeats == 7 && Near(Sim->GetColonyHope().Jobs, 21.0, 1e-6),
			FString::Printf(TEXT("F5 a lab bench: c5 seats 2, filled %d, jobs %.1f"), Sim->GetColonyHope().FilledSeats, Sim->GetColonyHope().Jobs));
		Spent += 40.0 * 3 + 80.0 + 80.0;
		{
			const bool bSeats = Sim->GetColonistSeatPiece(1) == 5 && Sim->GetColonistSeatPiece(2) == 6 && Sim->GetColonistSeatPiece(3) == 7
				&& Sim->GetColonistSeatPiece(4) == 8 && Sim->GetColonistSeatPiece(5) == 8 && Sim->GetColonistSeatPiece(6) == 9
				&& Sim->GetColonistSeatPiece(7) == 9 && Sim->GetColonistSeatPiece(8) == 0;
			const bool bBeds = Sim->GetColonistBedPiece(1) == 1 && Sim->GetColonistBedPiece(2) == 2 && Sim->GetColonistBedPiece(3) == 3
				&& Sim->GetColonistBedPiece(4) == 4 && Sim->GetColonistBedPiece(5) == 0 && Sim->GetColonistBedPiece(10) == 0;
			Expect(bSeats && bBeds, TEXT("F5 derived assignments: seats 1,2 desks; 3 desk; 4,5 workbench; 6,7 lab bench; beds 1-4 cots, 5-10 legacy"));
		}

		// F6. Comfort.
		Expect(Furnish(TEXT("DiningTable"), C4, 0) && Near(Sim->GetColonyHope().Furnishings, 1.0, 1e-9),
			FString::Printf(TEXT("F6 a dining table: Furnishings %.1f"), Sim->GetColonyHope().Furnishings));
		for (const int32 S : { 2, 3, 6, 7, 8 }) { Furnish(TEXT("Plant"), C4, S); }
		Expect(Near(Sim->GetColonyHope().Furnishings, 6.0, 1e-9), FString::Printf(TEXT("F6 + five plants: Furnishings %.1f"), Sim->GetColonyHope().Furnishings));
		Expect(Furnish(TEXT("Plant"), C4, 9) && Near(Sim->GetColonyHope().Furnishings, 6.0, 1e-9), TEXT("F6 a sixth plant: capped at 6"));
		Spent += 40.0 + 6 * 20.0;

		// F7. Re-zoning.
		Expect(!Sim->DesignateRoom(-1, 0, FName("Lab"), Why) && Why.Contains(TEXT("Cot")), FString::Printf(TEXT("F7 c0 to Lab refused (%s)"), *Why));
		Expect(!Sim->DesignateRoom(-1, 4, FName("LivingQuarters"), Why) && Why.Contains(TEXT("Dining Table")), FString::Printf(TEXT("F7 c4 to Living refused (%s)"), *Why));
		Expect(Furnish(TEXT("Plant"), C2, 15) && Sim->DesignateRoom(-1, 2, FName("Dining"), Why) && Near(Sim->GetColonyHope().Housing, 6.0, 1e-6)
			&& Sim->GetHousingCapacity() == 10, FString::Printf(TEXT("F7 a plant goes Living -> Dining with its room; housing %.2f, capacity %d"), Sim->GetColonyHope().Housing, Sim->GetHousingCapacity()));
		Spent += 20.0;
		Expect(!Sim->DesignateRoom(-1, 2, NAME_None, Why) && Sim->DesignateRoom(-1, 2, FName("LivingQuarters"), R) && Near(Sim->GetColonyHope().Housing, 7.5, 1e-6),
			FString::Printf(TEXT("F7 clearing c2 refused (%s); back to Living"), *Why));

		// F8. Selling.
		{
			const double Out0 = LanderKg(TEXT("Struct"), true);
			Expect(SellPiece(5) && Near(LanderKg(TEXT("Struct"), true) - Out0, 30.0, 1e-6) && Seats(C1).Y == 1,
				FString::Printf(TEXT("F8 a desk sold: +%.0f Struct on the Lander's output shelf, c1 back to 1 seat"), LanderKg(TEXT("Struct"), true) - Out0));
			Refunded += 30.0;
			const double Out1 = LanderKg(TEXT("Struct"), true);
			TMap<FName, double> Refund;
			Expect(Sim->CanSellCell(C4, Why, &Refund) && Near(Refund.FindRef(NStruct), 120.0, 1e-6) && SellCell(C4)
				&& Near(LanderKg(TEXT("Struct"), true) - Out1, 120.0, 1e-6) && Sim->GetCellFurniture(C4) == 0 && Sim->DesignateRoom(-1, 4, NAME_None, R),
				TEXT("F8 everything in c4 sold in one order (+120 = 0.75 x 160); the cell can be cleared again"));
			Refunded += 120.0;
			Expect(Near(Sim->GetTotalSolid(NStruct), StructStart - Spent + Refunded, 1e-3),
				FString::Printf(TEXT("F8 the Struct ledger closes: %.1f = %.1f - %.0f + %.0f"), Sim->GetTotalSolid(NStruct), StructStart, Spent, Refunded));
		}

		// F9. Surface bays.
		Sim->AddStock(FName("Shielding"), 1000.0);
		const int32 A = Place(TEXT("HabModule"), -1000, 1000);
		const int32 B = Place(TEXT("HabModule"), -2000, 1000);
		const int32 Cc = Place(TEXT("HabModule"), -1500, 2000, 2, 1);
		Place(TEXT("AirFilter"), -1000, 1000);
		Expect(Sim->DesignateBay(A, 0, FName("LivingQuarters"), R) && Sim->DesignateBay(B, 0, FName("LivingQuarters"), R) && Sim->DesignateBay(Cc, 0, FName("LivingQuarters"), R),
			TEXT("F9 habs A, B, C with a filter in A; A:0, B:0, C:0 Living"));
		{
			const double F0 = Sim->GetColonyHope().Furnishings;
			Expect(Furnish(TEXT("Plant"), FIntVector(0, 0, B), 0) && Near(Sim->GetColonyHope().Furnishings, F0, 1e-9),
				TEXT("F9 a plant in an unrated habitat does nothing yet"));
			Spent += 20.0;
			RunSols(1.0);
			Sim->Debug_SetVaultRated(true);
			Expect(Sim->IsHabRated(A) && Near(Sim->GetColonyHope().Furnishings, F0 + 1.0, 1e-9),
				FString::Printf(TEXT("F9 rated: the plant counts (%.1f -> %.1f)"), F0, Sim->GetColonyHope().Furnishings));
			Expect(Sim->GetHabBeds(B) == 0,
				FString::Printf(TEXT("F9 B:0 holds only a plant: a Living bay furnished without beds sleeps nobody (ruling 2026-10-16) - B has %d beds"), Sim->GetHabBeds(B)));
		}
		Expect(!CanPlace(TEXT("Cot"), FIntVector(0, 0, A), 0, 0, Why) && Why.Contains(TEXT("AirFilter")), FString::Printf(TEXT("F9 A's filter fills its centre (%s)"), *Why));
		{
			FString W2;
			Expect(!Sim->CanPlaceBuildingSized(FName("Floodmast"), FVector(-2300.f, 700.f, 0.f), W2, 0, 1, 1) && W2.Contains(TEXT("furnishing")),
				FString::Printf(TEXT("F9 a Floodmast over B's plant is refused (%s)"), *W2));
		}
		Expect(Furnish(TEXT("Cot"), FIntVector(0, 0, Cc), 0) && Furnish(TEXT("Cot"), FIntVector(0, 0, Cc), 2) && Sim->GetHabBeds(Cc) == 2,
			FString::Printf(TEXT("F9 two cots in C:0 - C has %d beds"), Sim->GetHabBeds(Cc)));
		Spent += 80.0;
		{
			const int32 Pop0 = Sim->GetPopulation();
			Expect(Sim->Debug_AddColonists(4) == 3 && Sim->GetHabResidents(A) == 1 && Sim->GetHabResidents(B) == 0 && Sim->GetHabResidents(Cc) == 2,
				FString::Printf(TEXT("F9 four more crew asked for, three beds: A 1, B 0 (its plant), C 2 (pop %d -> %d)"), Pop0, Sim->GetPopulation()));
			const int32 CotC = LastPieceId();
			Expect(!Sim->CanSellFurniture(CotC, Why) && Why.Contains(TEXT("sleep")), FString::Printf(TEXT("F9 C's cots are spoken for (%s)"), *Why));
		}
		// F9b. The furnishing bed guard (ruling 2026-10-16): the first non-bed
		// piece in a bare Living bay ends its legacy bed, so it is refused
		// while someone sleeps there; a bed brings a furnished bay back.
		{
			URHSimWorldSubsystem::FRHFurnishPreview P;
			Expect(Sim->PreviewFurnishEffect(FName("Plant"), FIntVector(0, 0, A), P) && P.BedsBefore == 1 && P.BedsAfter == 0,
				FString::Printf(TEXT("F9b the hint: a plant in A's bare Living bay takes its beds %d -> %d"), P.BedsBefore, P.BedsAfter));
			Why.Reset();
			Expect(!CanPlace(TEXT("Plant"), FIntVector(0, 0, A), 15, 0, Why) && Why.Contains(TEXT("sleep")),
				FString::Printf(TEXT("F9b ...so it is refused while A's resident sleeps there (%s)"), *Why));
			Expect(Furnish(TEXT("Cot"), FIntVector(0, 0, B), 2) && Sim->GetHabBeds(B) == 1 && Sim->Debug_AddColonists(1) == 1 && Sim->GetHabResidents(B) == 1,
				FString::Printf(TEXT("F9b a cot beside B's plant: B sleeps %d again and a newcomer moves in"), Sim->GetHabBeds(B)));
			Spent += 40.0;
		}
		const int32 Hh = Place(TEXT("HabModule"), -2000, -500, 1, 2);
		Expect(Sim->DesignateBay(Hh, 0, FName("LivingQuarters"), R) && Sim->DesignateBay(Hh, 1, FName("LivingQuarters"), R)
			&& Furnish(TEXT("Cot"), FIntVector(0, 0, Hh), 0) && Furnish(TEXT("Cot"), FIntVector(0, 0, Hh), 2) && Sim->GetHabBeds(Hh) == 3,
			FString::Printf(TEXT("F9 H (1x2) with two cots in H:0 and a plain Living H:1: %d beds"), Sim->GetHabBeds(Hh)));
		Spent += 80.0;
		Why.Reset();
		Expect(Sim->Debug_AddColonists(2) == 2 && Sim->GetHabResidents(Hh) == 2 && Sim->DesignateBay(Hh, 1, NAME_None, Why),
			TEXT("F9 two residents in H: un-zoning H:1 is allowed - the cots still sleep both"));
		Expect(Sim->DesignateBay(Hh, 1, FName("LivingQuarters"), R) && Sim->Debug_AddColonists(1) == 1 && !Sim->DesignateBay(Hh, 1, NAME_None, Why),
			FString::Printf(TEXT("F9 a third resident: now it is refused (%s)"), *Why));
		Expect(!Sim->DesignateBay(Cc, 0, FName("Lab"), Why) && Why.Contains(TEXT("Cot")), FString::Printf(TEXT("F9 C:0 to Lab refused (%s)"), *Why));

		// F10. Demolishing a furnished habitat sells its furniture with it.
		{
			const int32 Dm = Place(TEXT("HabModule"), -1000, -4000);
			Expect(Sim->DesignateBay(Dm, 0, FName("LivingQuarters"), R) && Furnish(TEXT("Cot"), FIntVector(0, 0, Dm), 0) && Furnish(TEXT("Plant"), FIntVector(0, 0, Dm), 15),
				TEXT("F10 a lone hab with a cot and a plant"));
			Spent += 60.0;
			TMap<FName, double> Salvage;
			Expect(Sim->CanDemolishBuilding(Dm, Why, &Salvage) && Near(Salvage.FindRef(NStruct), 145.0, 1e-6) && Near(Salvage.FindRef(FName("Shielding")), 25.0, 1e-6),
				FString::Printf(TEXT("F10 the preview: Struct %.0f (0.5 x 200 + 0.75 x 60), Shielding %.0f"), Salvage.FindRef(NStruct), Salvage.FindRef(FName("Shielding"))));
			const double S0 = LanderKg(TEXT("Struct"), true), Sh0 = LanderKg(TEXT("Shielding"), true);
			Changed.Reset();
			FRHCommand D; D.Verb = FName("Demolish"); D.Target = FName("Building"); D.Value = (double)Dm;
			Sim->EnqueueCommand(D);
			Step();
			bool bGone = true;
			for (const FRHFurnitureInstance& F : Sim->GetFurniture()) { bGone &= F.Key.Z != Dm; }
			Expect(bGone && Near(LanderKg(TEXT("Struct"), true) - S0, 145.0, 1e-6) && Near(LanderKg(TEXT("Shielding"), true) - Sh0, 25.0, 1e-6)
				&& Changed.Contains(FIntVector(0, 0, Dm)) && Alerts.ContainsByPredicate([](const FString& S) { return S.Contains(TEXT("DEMOLISHED")) && S.Contains(TEXT("2 furnishing(s) sold back")); }),
				TEXT("F10 demolished: +145 Struct, +25 Shielding, its 2 pieces gone and named in the alert"));
		}

		// F11. Save / load (v30).
		{
			struct FSnap
			{
				TArray<FString> Pieces; int32 Cap = 0; TArray<int32> HabBeds; TArray<FIntPoint> StationSeats; double Housing = 0, Jobs = 0, Furn = 0; TArray<FIntPoint> Assign;
			};
			const auto Take = [&]()
			{
				FSnap S;
				for (const FRHFurnitureInstance& F : Sim->GetFurniture()) { S.Pieces.Add(FString::Printf(TEXT("%d:%s:%d,%d,%d:%d:%d"), F.Id, *F.Row.ToString(), F.Key.X, F.Key.Y, F.Key.Z, F.Slot, F.Rot)); }
				S.Cap = Sim->GetHousingCapacity();
				for (const int32 H : { A, B, Cc, Hh }) { S.HabBeds.Add(Sim->GetHabBeds(H)); }
				for (const FIntVector& K : { C1, C3, C5 }) { S.StationSeats.Add(Seats(K)); }
				const URHSimWorldSubsystem::FRHHopeBreakdown Hb = Sim->GetColonyHope();
				S.Housing = Hb.Housing; S.Jobs = Hb.Jobs; S.Furn = Hb.Furnishings;
				for (const FRHColonist& Col : Sim->GetColonists()) { S.Assign.Add(FIntPoint(Sim->GetColonistBedPiece(Col.Id), Sim->GetColonistSeatPiece(Col.Id))); }
				return S;
			};
			const FSnap Before = Take();
			int32 MaxId = 0;
			for (const FRHFurnitureInstance& F : Sim->GetFurniture()) { MaxId = FMath::Max(MaxId, F.Id); }
			MaxId = FMath::Max(MaxId, LastPieceId());
			FString Err;
			Expect(Sim->SaveColony(TEXT("furnishtest"), Err), FString::Printf(TEXT("F11 saved (%s)"), *Err));
			Furnish(TEXT("Plant"), C2, 14);
			SellPiece(6);
			Expect(Sim->LoadColony(TEXT("furnishtest"), Err) && Err.IsEmpty(), FString::Printf(TEXT("F11 loaded (%s)"), *Err));
			const FSnap After = Take();
			Expect(Before.Pieces == After.Pieces && Before.Cap == After.Cap && Before.HabBeds == After.HabBeds && Before.StationSeats == After.StationSeats
				&& Before.Housing == After.Housing && Before.Jobs == After.Jobs && Before.Furn == After.Furn && Before.Assign == After.Assign,
				FString::Printf(TEXT("F11 the reload matches exactly: %d pieces, capacity %d, beds, seats, Hope lines, every assignment"), After.Pieces.Num(), After.Cap));
			Expect(Furnish(TEXT("Plant"), C2, 14) && LastPieceId() > MaxId,
				FString::Printf(TEXT("F11 the next piece gets #%d, above every Id issued before the save (highest standing #%d) - Ids are never reused"), LastPieceId(), MaxId));
		}

		Sim->OnAlert.Remove(AlertH);
		Sim->OnCommandRejected.Remove(RejectH);
		Sim->OnFurnitureChanged.Remove(ChangedH);
		UE_LOG(LogRedHopeSim, Display, TEXT("=== FURNITURE TEST END: %d failure(s) ==="), Fails);
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return Fails > 0 ? 1 : 0;
	}

	// BM-6 self-test (2026-10-16): WINDOWS AND DOORS - wall fittings on the
	// derived habitat walls: the data, edge classes, refusals, windows (Views
	// Hope, capped, and their air leak - measured), doors (a door in every
	// doorway seals the network: half the leak - measured), airlocks and their
	// chamber, the guards against walling over a window, selling (Glass back
	// to the pool), demolish with fittings, save v31. Fails loudly. `-walls`.
	if (FParse::Param(*Params, TEXT("walls")))
	{
		UE_LOG(LogRedHopeSim, Display, TEXT("=== WINDOWS AND DOORS TEST (BM-6, 2026-10-16) ==="));
		const int32 StepsPerSolH = (int32)(URHSimClockSubsystem::SolLengthSimSeconds / URHSimClockSubsystem::EraStepSimSeconds);
		int32 Fails = 0;
		const auto Expect = [&](bool bOk, const FString& What)
		{
			if (bOk) { UE_LOG(LogRedHopeSim, Display, TEXT("WALLS ok: %s"), *What); }
			else { UE_LOG(LogRedHopeSim, Error, TEXT("WALLS FAIL: %s"), *What); ++Fails; }
		};
		const auto Step = [&]()
		{
			Sim->Debug_AddFreshWater(500.0 * URHSimClockSubsystem::EraStepSimSeconds / (double)URHSimClockSubsystem::SolLengthSimSeconds);
			Sim->Debug_SetBatteryWh(1.0e9);
			Clock->Debug_AdvanceSimSeconds(URHSimClockSubsystem::EraStepSimSeconds);
			Sim->EraStep(URHSimClockSubsystem::EraStepSimSeconds);
		};
		const auto RunSols = [&](double Sols) { for (int32 S = 0; S < (int32)(Sols * StepsPerSolH); ++S) { Step(); } };
		const auto Near = [](double A, double B, double Tol) { return FMath::Abs(A - B) <= Tol; };
		TArray<FString> Alerts, Rejects;
		TArray<FIntVector> Changed;
		const FDelegateHandle AlertH = Sim->OnAlert.AddLambda([&Alerts](const FString& S) { Alerts.Add(S); });
		const FDelegateHandle RejectH = Sim->OnCommandRejected.AddLambda([&Rejects](const FRHCommand&, const FString& S) { Rejects.Add(S); });
		const FDelegateHandle ChangedH = Sim->OnWallFeatureChanged.AddLambda([&Changed](const FIntVector& E) { Changed.Add(E); });
		const auto LastReject = [&Rejects]() { return Rejects.Num() ? Rejects.Last() : FString(); };
		const auto LastIdOf = [&](const TCHAR* Def) { int32 Id = 0; for (const FRHBuildingInstance& B : Sim->GetBuildings()) { if (B.DefName == FName(Def)) { Id = B.Id; } } return Id; };
		const auto Place = [&](const TCHAR* Def, double X, double Y, int32 SX = 1, int32 SY = 1)
		{
			Sim->Debug_PlaceInstant(FName(Def), FVector(X, Y, 0.0), 0, SX, SY);
			return LastIdOf(Def);
		};
		const int32 LanderId = LastIdOf(TEXT("Lander"));
		const auto LanderKg = [&](const TCHAR* Res, bool bOut)
		{
			for (const FRHBuildingInstance& B : Sim->GetBuildings()) { if (B.Id == LanderId) { return (bOut ? B.OutputKg : B.InputKg).FindRef(FName(Res)); } }
			return 0.0;
		};
		// Fit orders: a point in the bay + the side (0 +X, 1 -X, 2 +Y, 3 -Y).
		const auto Fit = [&](const TCHAR* Row, double X, double Y, int32 Dir)
		{
			const int32 R0 = Rejects.Num();
			FRHCommand C; C.Verb = FName("Fit"); C.Target = FName(Row); C.Level = 0; C.Location = FVector(X, Y, 0.0); C.Value = (double)Dir;
			Sim->EnqueueCommand(C);
			Step();
			return Rejects.Num() == R0;
		};
		const auto SellW = [&](int32 Id)
		{
			const int32 R0 = Rejects.Num();
			FRHCommand C; C.Verb = FName("Sell"); C.Target = FName("WallFeature"); C.Value = (double)Id;
			Sim->EnqueueCommand(C);
			Step();
			return Rejects.Num() == R0;
		};
		const auto CanFit = [&](const TCHAR* Row, double X, double Y, int32 Dir, FString& Why)
		{
			Why.Reset();
			FIntVector E;
			if (!Sim->ResolveWallEdge(FVector(X, Y, 0.0), Dir, E))
			{
				Why = TEXT("No habitat wall there");
				return false;
			}
			return Sim->CanPlaceWallFeature(FName(Row), E, Why);
		};
		const auto Edge = [&](double X, double Y, int32 Dir) { FIntVector E; Sim->ResolveWallEdge(FVector(X, Y, 0.0), Dir, E); return E; };
		const auto LastFeatureId = [&]() { return Sim->GetWallFeatures().Num() ? Sim->GetWallFeatures().Last().Id : 0; };
		const auto NetO2 = [&](int32 A0, int32 B0, int32 C0) { return Sim->GetHabO2Kg(A0) + Sim->GetHabO2Kg(B0) + Sim->GetHabO2Kg(C0); };
		URHDefinitionsSubsystem* DefsSub = World->GetSubsystem<URHDefinitionsSubsystem>();
		using EEdge = URHSimWorldSubsystem::ERHWallEdge;
		FString Why, R;
		const double StepSols = URHSimClockSubsystem::EraStepSimSeconds / (double)URHSimClockSubsystem::SolLengthSimSeconds;

		// W0. Data.
		if (!DefsSub->HasWallCatalogue())
		{
			UE_LOG(LogRedHopeSim, Error, TEXT("WALLS FAIL: DT_Doors missing"));
			return 1;
		}
		{
			const FRHDoorRow* Door = DefsSub->GetDoor(FName("PressureDoor"));
			const FRHDoorRow* Hatch = DefsSub->GetDoor(FName("BulkheadHatch"));
			const FRHDoorRow* Win = DefsSub->GetDoor(FName("Window"));
			const FRHDoorRow* Lock = DefsSub->GetDoor(FName("Airlock"));
			const TMap<FName, double> WinBill = Win ? URHDefinitionsSubsystem::GetDoorCost(*Win) : TMap<FName, double>();
			Expect(Door && Door->SliceActive && Door->Kind == FName("Door") && Hatch && !Hatch->SliceActive
				&& Win && Win->SliceActive && Win->Kind == FName("Window") && Near(WinBill.FindRef(FName("Struct")), 10.0, 1e-9) && Near(WinBill.FindRef(FName("Glass")), 40.0, 1e-9)
				&& Near(Win->HopeViews, 0.5, 1e-6) && Near(Win->LeakKgPerSol, 0.5, 1e-6) && Lock && Lock->SliceActive && Lock->Kind == FName("Airlock") && Lock->LeakKgPerSol == 0.f,
				TEXT("W0 DT_Doors: Pressure Door (live), Bulkhead Hatch (dormant), Window (Struct 10 + Glass 40, Views 0.5, leak 0.5), Airlock (live, no leak) (DT/CSV)"));
			Expect(Near(Sim->GetHopeViewsMax(), 3.0, 1e-9) && Near(Sim->GetSealedLeakMul(), 0.5, 1e-9),
				TEXT("W0 config: Views cap 3; the Sealed Bay compartment row is live at LeakMul 0.5"));
		}

		// Setup: the -surfacehab layout - A + B + C (2x1), a filter in A.
		Place(TEXT("SolarArray"), 3500.f, -1000.f);
		Place(TEXT("SolarArray"), 3500.f, -2000.f);
		Sim->AddStock(FName("Oxygen"), 6000.0);
		Sim->AddStock(FName("Water"), 4000.0);
		Sim->AddStock(FName("Food"), 6000.0);
		Sim->AddStock(FName("Shielding"), 1000.0);
		Sim->Debug_AddSolidAt(LanderId, FName("Struct"), 2000.0, false);
		const int32 A = Place(TEXT("HabModule"), -1000, 1000);
		const int32 B = Place(TEXT("HabModule"), -2000, 1000);
		const int32 Cc = Place(TEXT("HabModule"), -1500, 2000, 2, 1);
		const int32 Filter = Place(TEXT("AirFilter"), -1000, 1000);
		Expect(Sim->DesignateBay(A, 0, FName("LivingQuarters"), R) && Sim->DesignateBay(B, 0, FName("LivingQuarters"), R)
			&& Sim->DesignateBay(Cc, 0, FName("LivingQuarters"), R) && Sim->DesignateBay(Cc, 1, FName("Dining"), R),
			TEXT("setup: A, B, C:0 Living, C:1 Dining"));
		RunSols(1.0);
		Expect(Sim->IsHabRated(A), TEXT("setup: the 4-bay network rates"));

		// W1. No fittings: the legacy habitat.
		{
			int32 Doorways = 0, Doored = 0;
			Sim->GetHabNetworkDoorways(A, Doorways, Doored);
			Expect(Near(Sim->GetHabNetworkLeakKgPerSol(A), 8.0, 1e-9) && !Sim->IsHabNetworkSealed(A) && Doorways == 4 && Doored == 0
				&& Sim->GetWallFeatures().Num() == 0 && Sim->GetViewsHope() == 0.0,
				FString::Printf(TEXT("W1 no fittings: leak %.2f kg/sol (4 bays x 2), %d doorways, none doored, no views"), Sim->GetHabNetworkLeakKgPerSol(A), Doorways));
		}

		// W2. Edge classes and refusals.
		Expect(Sim->ClassifyWallEdge(Edge(-1000, 1000, 0)) == EEdge::Exterior && Sim->ClassifyWallEdge(Edge(-2000, 1000, 0)) == EEdge::Doorway
			&& Sim->ClassifyWallEdge(Edge(-2000, 2000, 0)) == EEdge::Doorway && Edge(-1000, 1000, 1) == Edge(-2000, 1000, 0)
			&& Sim->ClassifyWallEdge(FIntVector(20, 20, 0)) == EEdge::None,
			TEXT("W2 edges: A's east wall is outside; A-B and C:0-C:1 (Living | Dining) are doorways; one key from either side"));
		Expect(!CanFit(TEXT("Throne"), -1000, 1000, 0, Why) && Why.Contains(TEXT("not a window")), FString::Printf(TEXT("W2 an unknown row (%s)"), *Why));
		Expect(!CanFit(TEXT("BulkheadHatch"), -2000, 1000, 0, Why) && Why.Contains(TEXT("not available")), FString::Printf(TEXT("W2 the dormant hatch (%s)"), *Why));
		Expect(!CanFit(TEXT("Window"), -2000, 1000, 0, Why) && Why.Contains(TEXT("outside wall")), FString::Printf(TEXT("W2 a window in a doorway (%s)"), *Why));
		Expect(!CanFit(TEXT("PressureDoor"), -1000, 1000, 0, Why) && Why.Contains(TEXT("doorways")), FString::Printf(TEXT("W2 a door in an outside wall (%s)"), *Why));
		Expect(!CanFit(TEXT("Window"), 9000, 9000, 0, Why) && Why.Contains(TEXT("No habitat wall")), FString::Printf(TEXT("W2 open ground (%s)"), *Why));
		Expect(!CanFit(TEXT("Window"), -1000, 1000, 0, Why) && Why.Contains(TEXT("Glass")), FString::Printf(TEXT("W2 no Glass in stock (%s)"), *Why));
		Expect(Sim->DesignateBay(Cc, 1, FName("LivingQuarters"), R) && !CanFit(TEXT("PressureDoor"), -2000, 2000, 0, Why) && Why.Contains(TEXT("one room"))
			&& Sim->DesignateBay(Cc, 1, FName("Dining"), R), FString::Printf(TEXT("W2 with C all Living its middle is one room - no door (%s)"), *Why));
		Sim->AddStock(FName("Glass"), 1000.0);

		// W3. Windows: Views and their leak.
		{
			const double Struct0 = Sim->GetTotalSolid(FName("Struct")), Glass0 = Sim->GetStock(FName("Glass"));
			Changed.Reset();
			Expect(Fit(TEXT("Window"), -1000, 1000, 0) && Near(Sim->GetTotalSolid(FName("Struct")), Struct0 - 10.0, 1e-6) && Near(Sim->GetStock(FName("Glass")), Glass0 - 40.0, 1e-6)
				&& Changed.Num() == 1 && Near(Sim->GetViewsHope(), 0.5, 1e-9) && Near(Sim->GetHabNetworkLeakKgPerSol(A), 8.5, 1e-9),
				FString::Printf(TEXT("W3 a window in A's east wall: -10 Struct, -40 Glass, views %.1f, leak %.2f kg/sol"), Sim->GetViewsHope(), Sim->GetHabNetworkLeakKgPerSol(A)));
			bool bAll = true;
			for (const FIntVector& S : { FIntVector(-1000, 1000, 3), FIntVector(-2000, 1000, 1), FIntVector(-2000, 1000, 3), FIntVector(-2000, 2000, 1),
				FIntVector(-2000, 2000, 2), FIntVector(-1000, 2000, 2), FIntVector(-1000, 2000, 0) })
			{
				bAll &= Fit(TEXT("Window"), S.X, S.Y, S.Z);
			}
			double Raw = 0.0;
			const double Capped = Sim->GetViewsHope(&Raw);
			Expect(bAll && Sim->GetWallFeatures().Num() == 8 && Near(Raw, 4.0, 1e-9) && Near(Capped, 3.0, 1e-9) && Near(Sim->GetHabNetworkLeakKgPerSol(A), 12.0, 1e-9),
				FString::Printf(TEXT("W3 a window in all 8 outside walls: views %.1f capped at %.1f, leak %.2f"), Raw, Capped, Sim->GetHabNetworkLeakKgPerSol(A)));
			Expect(!CanFit(TEXT("Window"), -1000, 1000, 0, Why) && Why.Contains(TEXT("already there")), FString::Printf(TEXT("W3 one fitting a wall (%s)"), *Why));
			// The leak, measured: the filter off (it powers down at the end of
			// the next step), no one breathing.
			Sim->SetManualPower(Filter, false);
			Step();
			const double O20 = NetO2(A, B, Cc);
			Step();
			Expect(Near(O20 - NetO2(A, B, Cc), 12.0 * StepSols, 1e-6), FString::Printf(TEXT("W3 measured: the network lost %.4f kg in a step (expect %.4f)"), O20 - NetO2(A, B, Cc), 12.0 * StepSols));
			Sim->SetManualPower(Filter, true);
		}

		// W4. Doors: a door in every doorway seals the network.
		{
			Expect(Fit(TEXT("PressureDoor"), -2000, 1000, 0) && Fit(TEXT("PressureDoor"), -1000, 1000, 2) && Fit(TEXT("PressureDoor"), -2000, 1000, 2)
				&& !Sim->IsHabNetworkSealed(A) && Near(Sim->GetHabNetworkLeakKgPerSol(A), 12.0, 1e-9), TEXT("W4 three doors of four: not sealed, leak unchanged"));
			Expect(Fit(TEXT("PressureDoor"), -2000, 2000, 0) && Sim->IsHabNetworkSealed(A) && Near(Sim->GetHabNetworkLeakKgPerSol(A), 8.0 * 0.5 + 4.0, 1e-9),
				FString::Printf(TEXT("W4 the fourth door seals it: leak %.2f (8 x 0.5 + the windows' 4)"), Sim->GetHabNetworkLeakKgPerSol(A)));
			Sim->SetManualPower(Filter, false);
			Step();
			const double O20 = NetO2(A, B, Cc);
			Step();
			Expect(Near(O20 - NetO2(A, B, Cc), 8.0 * StepSols, 1e-6), FString::Printf(TEXT("W4 measured: %.4f kg in a step sealed (expect %.4f)"), O20 - NetO2(A, B, Cc), 8.0 * StepSols));
			Sim->SetManualPower(Filter, true);
			int32 Doorways = 0, Doored = 0;
			Expect(Sim->DesignateBay(Cc, 1, FName("LivingQuarters"), R) && Sim->IsHabNetworkSealed(A) && (Sim->GetHabNetworkDoorways(A, Doorways, Doored), Doorways == 3 && Doored == 3)
				&& Sim->DesignateBay(Cc, 1, FName("Dining"), R), TEXT("W4 re-zoning C into one room leaves 3 doorways, all doored - still sealed"));
		}
		RunSols(1.0);
		Sim->Debug_SetVaultRated(true);
		Expect(Sim->IsHabRated(A) && Sim->Debug_AddColonists(3) == 3 && Near(Sim->GetColonyHope().Views, 3.0, 1e-9),
			FString::Printf(TEXT("W4 rated again; three residents: Views %.1f in the Hope breakdown"), Sim->GetColonyHope().Views));

		// W5/W6. Sell a window; the airlock and its chamber; no walling over a window.
		{
			const int32 WestWin = Sim->FindWallFeatureOnEdge(Edge(-2000, 1000, 1)) ? Sim->FindWallFeatureOnEdge(Edge(-2000, 1000, 1))->Id : 0;
			const double Out0 = LanderKg(TEXT("Struct"), true), Glass0 = Sim->GetStock(FName("Glass"));
			Expect(WestWin && SellW(WestWin) && Near(LanderKg(TEXT("Struct"), true) - Out0, 7.5, 1e-6) && Near(Sim->GetStock(FName("Glass")) - Glass0, 30.0, 1e-6)
				&& LanderKg(TEXT("Glass"), true) == 0.0 && !Sim->FindWallFeatureOnEdge(Edge(-2000, 1000, 1)),
				TEXT("W6 a window sold: 7.5 Struct to the Lander, 30 Glass back to the pool (where glazing reads it)"));
			const int32 Mast = Place(TEXT("Floodmast"), -2700, 1000);
			Expect(!CanFit(TEXT("Airlock"), -2000, 1000, 1, Why) && Why.Contains(TEXT("Floodmast")), FString::Printf(TEXT("W5 a machine in the chamber's way (%s)"), *Why));
			FRHCommand D; D.Verb = FName("Demolish"); D.Target = FName("Building"); D.Value = (double)Mast;
			Sim->EnqueueCommand(D);
			Step();
			Expect(Fit(TEXT("Airlock"), -2000, 1000, 1), TEXT("W5 an airlock in B's west wall"));
			FString W2;
			Expect(!Sim->CanPlaceBuildingSized(FName("Floodmast"), FVector(-2700.f, 1000.f, 0.f), W2, 0, 1, 1) && W2.Contains(TEXT("chamber")),
				FString::Printf(TEXT("W5 nothing may stand in the chamber (%s)"), *W2));
			Expect(!Sim->CanPlaceBuildingSized(FName("HabModule"), FVector(0.f, 1000.f, 0.f), W2, 0, 1, 1) && W2.Contains(TEXT("faces that way")),
				FString::Printf(TEXT("W5 a new module may not wall over A's east window (%s)"), *W2));
		}

		// W7. Demolish a habitat with fittings: they come back in the salvage.
		int32 LastIssued = 0;
		{
			const int32 Dm = Place(TEXT("HabModule"), -1000, -4000);
			Expect(Fit(TEXT("Window"), -1000, -4000, 3) && Fit(TEXT("Airlock"), -1000, -4000, 2), TEXT("W7 a lone hab with a window and an airlock"));
			LastIssued = LastFeatureId();
			TMap<FName, double> Salvage;
			Expect(Sim->CanDemolishBuilding(Dm, Why, &Salvage) && Near(Salvage.FindRef(FName("Struct")), 152.5, 1e-6) && Near(Salvage.FindRef(FName("Glass")), 30.0, 1e-6)
				&& Near(Salvage.FindRef(FName("Shielding")), 25.0, 1e-6),
				FString::Printf(TEXT("W7 the preview: Struct %.1f (100 hull + 0.75 x 70), Glass %.0f, Shielding %.0f"), Salvage.FindRef(FName("Struct")), Salvage.FindRef(FName("Glass")), Salvage.FindRef(FName("Shielding"))));
			const double S0 = LanderKg(TEXT("Struct"), true), G0 = Sim->GetStock(FName("Glass"));
			const int32 N0 = Sim->GetWallFeatures().Num();
			FRHCommand D; D.Verb = FName("Demolish"); D.Target = FName("Building"); D.Value = (double)Dm;
			Sim->EnqueueCommand(D);
			Step();
			Expect(Sim->GetWallFeatures().Num() == N0 - 2 && Near(LanderKg(TEXT("Struct"), true) - S0, 152.5, 1e-6) && Near(Sim->GetStock(FName("Glass")) - G0, 30.0, 1e-6)
				&& LanderKg(TEXT("Glass"), true) == 0.0 && Alerts.ContainsByPredicate([](const FString& S) { return S.Contains(TEXT("2 window(s) / door(s) sold back")); }),
				TEXT("W7 demolished: its 2 fittings gone, 152.5 Struct to the Lander, 30 Glass to the pool, named in the alert"));
		}

		// W8. Save / load (v31).
		{
			TArray<FString> Before;
			for (const FRHWallFeature& W : Sim->GetWallFeatures()) { Before.Add(FString::Printf(TEXT("%d:%s:%d,%d,%d"), W.Id, *W.Row.ToString(), W.Edge.X, W.Edge.Y, W.Edge.Z)); }
			const double Leak0 = Sim->GetHabNetworkLeakKgPerSol(A), Views0 = Sim->GetColonyHope().Views;
			const bool bSealed0 = Sim->IsHabNetworkSealed(A);
			int32 MaxId = 0;
			for (const FRHWallFeature& W : Sim->GetWallFeatures()) { MaxId = FMath::Max(MaxId, W.Id); }
			FString Err;
			Expect(Sim->SaveColony(TEXT("wallstest"), Err), FString::Printf(TEXT("W8 saved (%s)"), *Err));
			SellW(Sim->FindWallFeatureOnEdge(Edge(-2000, 1000, 0))->Id);
			Expect(Sim->LoadColony(TEXT("wallstest"), Err) && Err.IsEmpty(), FString::Printf(TEXT("W8 loaded (%s)"), *Err));
			TArray<FString> After;
			for (const FRHWallFeature& W : Sim->GetWallFeatures()) { After.Add(FString::Printf(TEXT("%d:%s:%d,%d,%d"), W.Id, *W.Row.ToString(), W.Edge.X, W.Edge.Y, W.Edge.Z)); }
			Expect(Before == After && Sim->GetHabNetworkLeakKgPerSol(A) == Leak0 && Sim->GetColonyHope().Views == Views0 && Sim->IsHabNetworkSealed(A) == bSealed0 && bSealed0,
				FString::Printf(TEXT("W8 the reload matches: %d fittings, sealed, leak %.2f, views %.1f"), After.Num(), Leak0, Views0));
			Expect(Fit(TEXT("Window"), -2000, 1000, 1) == false && Fit(TEXT("Window"), -1000, 1000, 3) == false,
				TEXT("W8 the loaded walls are still occupied (the airlock, the window)"));
			const int32 DoorAB = Sim->FindWallFeatureOnEdge(Edge(-2000, 1000, 0)) ? Sim->FindWallFeatureOnEdge(Edge(-2000, 1000, 0))->Id : 0;
			Expect(DoorAB && SellW(DoorAB) && Fit(TEXT("PressureDoor"), -2000, 1000, 0) && LastFeatureId() == LastIssued + 1 && LastIssued > MaxId,
				FString::Printf(TEXT("W8 a door re-fitted gets #%d - after the demolished hab's #%d, Ids are never reused"), LastFeatureId(), LastIssued));
		}

		Sim->OnAlert.Remove(AlertH);
		Sim->OnCommandRejected.Remove(RejectH);
		Sim->OnWallFeatureChanged.Remove(ChangedH);
		UE_LOG(LogRedHopeSim, Display, TEXT("=== WINDOWS AND DOORS TEST END: %d failure(s) ==="), Fails);
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return Fails > 0 ? 1 : 0;
	}

	// BM-1b self-test (2026-10-16): THE PREFAB HABITAT - the Forge-made chain
	// end to end: a Habitat site wants 2 segments; the Forge (with Struct and
	// Ore, no Shielding anywhere) makes Shielding because the segments need it
	// (the shortage looks through a short output's recipe), then presses the
	// segments; haulers deliver; the prefab stands as a fixed two-bay Living
	// Quarters that joins a network and rates. Haulers are stood in by the
	// robot API (perfect haulers each step). Fails loudly. `-prefab`.
	if (FParse::Param(*Params, TEXT("prefab")))
	{
		UE_LOG(LogRedHopeSim, Display, TEXT("=== PREFAB HABITAT TEST (BM-1b, 2026-10-16) ==="));
		const int32 StepsPerSolH = (int32)(URHSimClockSubsystem::SolLengthSimSeconds / URHSimClockSubsystem::EraStepSimSeconds);
		int32 Fails = 0;
		const auto Expect = [&](bool bOk, const FString& What)
		{
			if (bOk) { UE_LOG(LogRedHopeSim, Display, TEXT("PREFAB ok: %s"), *What); }
			else { UE_LOG(LogRedHopeSim, Error, TEXT("PREFAB FAIL: %s"), *What); ++Fails; }
		};
		int32 HaulsDone = 0;
		const auto Haulers = [&]()
		{
			Sim->Debug_StepTaskBoard();
			FRHTask T;
			for (int32 k = 0; k < 64 && Sim->TryClaimHaul(FMassEntityHandle(920001 + k, 1), FVector::ZeroVector, T); ++k)
			{
				float Loaded = 0.f;
				FVector Drop;
				if (Sim->HaulLoad(T.Id, 200.f, Loaded, Drop))
				{
					Sim->HaulUnload(T.Id, Loaded);
					++HaulsDone;
				}
			}
		};
		const auto Step = [&]()
		{
			Sim->Debug_AddFreshWater(500.0 * URHSimClockSubsystem::EraStepSimSeconds / (double)URHSimClockSubsystem::SolLengthSimSeconds);
			Sim->Debug_SetBatteryWh(1.0e9);
			Clock->Debug_AdvanceSimSeconds(URHSimClockSubsystem::EraStepSimSeconds);
			Sim->EraStep(URHSimClockSubsystem::EraStepSimSeconds);
		};
		const auto RunSols = [&](double Sols) { for (int32 S = 0; S < (int32)(Sols * StepsPerSolH); ++S) { Step(); } };
		const auto LastIdOf = [&](const TCHAR* Def) { int32 Id = 0; for (const FRHBuildingInstance& B : Sim->GetBuildings()) { if (B.DefName == FName(Def)) { Id = B.Id; } } return Id; };
		const auto Find = [&](int32 Id) -> const FRHBuildingInstance* { for (const FRHBuildingInstance& B : Sim->GetBuildings()) { if (B.Id == Id) { return &B; } } return nullptr; };
		const auto Place = [&](const TCHAR* Def, double X, double Y, int32 SX = 1, int32 SY = 1)
		{
			Sim->Debug_PlaceInstant(FName(Def), FVector(X, Y, 0.0), 0, SX, SY);
			return LastIdOf(Def);
		};
		URHDefinitionsSubsystem* DefsSub = World->GetSubsystem<URHDefinitionsSubsystem>();
		const int32 LanderId = LastIdOf(TEXT("Lander"));
		const FName NSeg(TEXT("HabSegment")), NShield(TEXT("Shielding")), NStruct(TEXT("Struct"));
		FString Why, R;

		// P0. Data.
		{
			const FRHBuildingRow* Hab = DefsSub->GetBuilding(FName("Habitat"));
			const FRHRecipeRow* Rec = DefsSub->GetRecipe(FName("MakeHabSegment"));
			const TMap<FName, double> Bill = Hab ? URHDefinitionsSubsystem::GetBuildCostFor(*Hab, 0) : TMap<FName, double>();
			const TMap<FName, double> In = Rec ? URHDefinitionsSubsystem::ParseResourceList(Rec->Inputs) : TMap<FName, double>();
			Expect(Hab && Hab->SliceActive && Hab->Pressurized && !Hab->DragSized && Hab->FootprintX == 5 && Hab->FootprintY == 10
				&& Hab->ProvidesRoom == FName("LivingQuarters") && Bill.Num() == 1 && Bill.FindRef(NSeg) == 2.0
				&& Rec && Rec->SliceActive && Rec->Building == FName("Forge") && In.FindRef(NStruct) == 120.0 && In.FindRef(NShield) == 30.0
				&& DefsSub->IsSolidResource(NSeg),
				TEXT("P0 Habitat: a fixed 1x2-bay pressurized Living Quarters, bill 2 HabSegment (no surface tax); MakeHabSegment live at the Forge (Struct 120 + Shielding 30) (DT/CSV)"));
		}

		// P1. Placement: a producer must exist; the bay grid.
		Place(TEXT("SolarArray"), 3500.f, -1000.f);
		Place(TEXT("SolarArray"), 3500.f, -2000.f);
		const FVector Spot(-1000.f, 1500.f, 0.f);
		Expect(!Sim->CanPlaceBuilding(FName("Habitat"), Spot, Why, 0) && Why.Contains(TEXT("HabSegment")), FString::Printf(TEXT("P1 no Forge, no segments: refused (%s)"), *Why));
		const int32 Forge = Place(TEXT("Forge"), 2000.f, 1500.f);
		Why.Reset();
		Expect(Sim->CanPlaceBuilding(FName("Habitat"), Spot, Why, 0), FString::Printf(TEXT("P1 with a Forge to press segments: accepted (%s)"), *Why));
		Expect(!Sim->CanPlaceBuilding(FName("Habitat"), FVector(-1000.f, 1000.f, 0.f), Why, 0) && Why.Contains(TEXT("grid")), FString::Printf(TEXT("P1 off the bay grid: refused (%s)"), *Why));

		// P2. The chain.
		Sim->Debug_AddSolidAt(LanderId, NStruct, 600.0, false);
		Sim->Debug_AddSolidAt(LanderId, FName("Ore"), 300.0, false);
		FRHCommand Order; Order.Verb = FName("Build"); Order.Target = FName("Habitat"); Order.Location = Spot; Order.Level = 0;
		Sim->EnqueueCommand(Order);
		Step();
		const int32 Hab = LastIdOf(TEXT("Habitat"));
		Expect(Hab && Find(Hab) && Find(Hab)->bUnderConstruction && Sim->GetTotalSolid(NShield) + Sim->GetStock(NShield) == 0.0,
			TEXT("P2 a Habitat site wants 2 segments; the colony holds no Shielding at all"));
		double PeakShield = 0.0;
		int32 Steps = 0;
		for (; Steps < 3 * StepsPerSolH; ++Steps)
		{
			Haulers();
			Step();
			PeakShield = FMath::Max(PeakShield, Sim->GetTotalSolid(NShield));
			const FRHBuildingInstance* Site = Find(Hab);
			if (Site && Site->InputKg.FindRef(NSeg) >= 2.0 - 1e-6)
			{
				break;
			}
		}
		const FRHBuildingInstance* Site = Find(Hab);
		Expect(PeakShield >= 60.0 - 1e-6, FString::Printf(TEXT("P2 the Forge made Shielding no site asked for - the segments did (peak %.0f kg)"), PeakShield));
		Expect(Site && Site->InputKg.FindRef(NSeg) >= 2.0 - 1e-6, FString::Printf(TEXT("P2 two segments pressed and delivered in %d steps (%.1f sols, %d hauls)"),
			Steps, Steps / (double)StepsPerSolH, HaulsDone));
		Expect(Sim->ApplyBuildWork(Hab, 1.0e6) && Find(Hab) && !Find(Hab)->bUnderConstruction && Sim->GetBayGrid(*Find(Hab)) == FIntPoint(1, 2),
			TEXT("P2 assembled: a 1x2-bay shell"));

		// P3. A shell like any other: fixed Living bays, a network, a rating.
		Expect(Sim->GetBayRoom(Hab, 0) == FName("LivingQuarters") && Sim->GetBayRoom(Hab, 1) == FName("LivingQuarters") && !Sim->DesignateBay(Hab, 0, FName("Dining"), Why),
			FString::Printf(TEXT("P3 both bays are Living Quarters, fixed (%s)"), *Why));
		Sim->AddStock(FName("Shielding"), 500.0);
		Sim->AddStock(FName("Oxygen"), 3000.0);
		const int32 Mod = Place(TEXT("HabModule"), -2000, 1500, 1, 2);
		Place(TEXT("AirFilter"), -2000, 1000);
		RunSols(1.0);
		Sim->Debug_SetVaultRated(true);
		Expect(Sim->GetHabNetworkRoot(Mod) == Sim->GetHabNetworkRoot(Hab) && Sim->GetHabNetworkBays(Hab) == 4 && Sim->IsHabRated(Hab) && Sim->GetHabBeds(Hab) == 2
			&& Sim->Debug_AddColonists(2) == 2 && Sim->GetHabResidents(Hab) == 2,
			FString::Printf(TEXT("P3 joined to a 1x2 module: a rated 4-bay network; the prefab sleeps %d and two crew move in"), Sim->GetHabBeds(Hab)));
		Expect(Find(Forge) != nullptr, TEXT("P3 the Forge stands"));

		UE_LOG(LogRedHopeSim, Display, TEXT("=== PREFAB HABITAT TEST END: %d failure(s) ==="), Fails);
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return Fails > 0 ? 1 : 0;
	}

	// D2a self-test (2026-10-17): ICE RUNNING LOW - a one-shot alert when a
	// discovered ice deposit falls below RH_Config IceLowAlertFraction of its
	// surveyed mass, and the pure read the guide interrupt shares
	// (GetIceExpansionStage). Pure-data pins, the type/discovery gates, the
	// crossing detected on its own step at dt 60 s and dt 1 s, no re-fire,
	// the load-time latch rebuild AND reset (the latch is cleared before a
	// load so a stale in-memory copy cannot pass for the rebuild), and the
	// dead end (both deposits low: nothing left to resolve), the "melt it where
	// you drill it" stage, and the drill's ice hauled to the NEAREST Water Plant.
	// Fails loudly.
	// `-icelow`.
	if (FParse::Param(*Params, TEXT("icelow")))
	{
		UE_LOG(LogRedHopeSim, Display, TEXT("=== ICE LOW TEST (D2a, 2026-10-17) ==="));
		int32 Fails = 0;
		const auto Expect = [&](bool bOk, const FString& What)
		{
			if (bOk) { UE_LOG(LogRedHopeSim, Display, TEXT("ICELOW ok: %s"), *What); }
			else { UE_LOG(LogRedHopeSim, Error, TEXT("ICELOW FAIL: %s"), *What); ++Fails; }
		};
		const auto Near = [](double A, double B, double Tol) { return FMath::Abs(A - B) <= Tol; };
		const auto Step = [&](double Dt)
		{
			Sim->Debug_SetBatteryWh(1.0e9);
			Clock->Debug_AdvanceSimSeconds(Dt);
			Sim->EraStep((float)Dt);
		};
		TArray<FString> Alerts;
		const FDelegateHandle AlertH = Sim->OnAlert.AddLambda([&Alerts](const FString& S) { Alerts.Add(S); });
		const auto CountAlerts = [&Alerts](const TCHAR* Needle) { int32 N = 0; for (const FString& A : Alerts) { N += A.Contains(Needle) ? 1 : 0; } return N; };
		const auto LastAlert = [&Alerts]() { return Alerts.Num() ? Alerts.Last() : FString(); };
		const auto Dep = [&](const ANSICHAR* Row) -> const FRHDepositState*
		{
			for (const FRHDepositState& D : Sim->GetDeposits()) { if (D.RowName == FName(Row)) { return &D; } }
			return nullptr;
		};
		const auto SetLeft = [&](const ANSICHAR* Row, double Kg) { if (const FRHDepositState* D = Dep(Row)) { Sim->Debug_SetDepositRemainingKg(D->Id, Kg); } };
		URHDefinitionsSubsystem* DefsSub = World->GetSubsystem<URHDefinitionsSubsystem>();
		const double Frac = 0.25;
		FString Err;

		// I1. Pure data: the config row rides the synced DT_Config. The -1
		// sentinel fails loudly on a DT that was never synced.
		Expect(Near(DefsSub->GetConfigScalar(FName("IceLowAlertFraction"), -1.0), Frac, 1e-9) && Near(Sim->GetIceLowAlertFraction(), Frac, 1e-9),
			FString::Printf(TEXT("I1 RH_Config IceLowAlertFraction = %.2f, and the sim reads it (DT/CSV)"), Sim->GetIceLowAlertFraction()));

		// I2. Pure data: the two ice bodies. Ice_B's position is pinned because
		// the guide copy tells the player where to look.
		{
			bool bA = false, bB = false;
			DefsSub->ForEachDeposit([&](FName Row, const FRHDepositRow& R)
			{
				bA |= Row == FName("Ice_A") && R.Type == FName("Ice") && R.Mass_kg == 30000.f && !R.SurfaceVisible;
				bB |= Row == FName("Ice_B") && R.Type == FName("Ice") && R.Mass_kg == 60000.f && !R.SurfaceVisible && R.LocX_m == 60.f && R.LocY_m == 700.f;
			});
			Expect(bA && bB, TEXT("I2 Ice_A (30 t) and Ice_B (60 t at 60 m E, 700 m N) are hidden Ice rows (DT/CSV)"));
			if (!bA || !bB)
			{
				Sim->OnAlert.Remove(AlertH);
				UE_LOG(LogRedHopeSim, Display, TEXT("=== ICE LOW TEST END: %d failure(s) ==="), Fails);
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
				return 1;
			}
		}

		// I3. InitialKg is the row mass at begin-play; nothing is low yet.
		{
			bool bAll = true, bLow = false;
			DefsSub->ForEachDeposit([&](FName Row, const FRHDepositRow& R)
			{
				const FRHDepositState* D = Dep(TCHAR_TO_ANSI(*Row.ToString()));
				bAll &= D && Near(D->InitialKg, R.Mass_kg, 1e-6);
			});
			for (const FRHDepositState& D : Sim->GetDeposits()) { bLow |= Sim->IsIceDepositLow(D); }
			Expect(bAll && !bLow && Sim->GetIceExpansionStage() == -1 && Sim->HasUndiscoveredIce(),
				TEXT("I3 every deposit's InitialKg = its row mass; nothing low; ice still to find"));
		}

		// I4. Inert while undiscovered: an unsurveyed deposit is invisible to
		// the alert, however little is left.
		SetLeft("Ice_B", 100.0);
		for (int32 i = 0; i < 24; ++i) { Step(60.0); }
		Expect(CountAlerts(TEXT("RUNNING LOW")) == 0 && !Sim->IsIceDepositLow(*Dep("Ice_B")) && Sim->GetIceExpansionStage() == -1,
			TEXT("I4 an undiscovered ice deposit at 100 kg raises nothing"));
		SetLeft("Ice_B", 60000.0);

		// I5. Type gate: a regolith body below the line is not ice.
		SetLeft("Regolith_A", 1000.0);
		for (int32 i = 0; i < 24; ++i) { Step(60.0); }
		Expect(CountAlerts(TEXT("RUNNING LOW")) == 0 && !Sim->IsIceDepositLow(*Dep("Regolith_A")),
			TEXT("I5 a regolith deposit nearly spent raises nothing"));
		SetLeft("Regolith_A", 60000.0);

		// I6. The crossing at dt 60 s: Ice_A surveyed, drilled, set 200 kg
		// above the line; the alert comes on the very step it crosses.
		const int32 IceAId = Sim->Debug_RevealDeposit(FName("Ice_A"));
		const FVector IceALoc = Dep("Ice_A")->LocationCm;
		Sim->Debug_PlaceInstant(FName("IceDrill"), IceALoc);
		int32 DrillA = 0;
		for (const FRHBuildingInstance& B : Sim->GetBuildings()) { if (B.DefName == FName("IceDrill")) { DrillA = B.Id; } }
		bool bAttached = false;
		for (const FRHBuildingInstance& B : Sim->GetBuildings()) { bAttached |= B.Id == DrillA && B.AttachedDepositId == IceAId; }
		const double LineA = Frac * Dep("Ice_A")->InitialKg;
		Expect(IceAId != 0 && DrillA != 0 && bAttached && !Sim->IsIceDepositLow(*Dep("Ice_A")) && Sim->GetIceExpansionStage() == -1,
			TEXT("I6 Ice_A surveyed and drilled; full, so not low"));
		SetLeft("Ice_A", LineA + 200.0);
		int32 CrossStep = -1, CountBefore = 0, CountAtCross = 0;
		bool bQuiet = true;
		for (int32 i = 0; i < 600 && CrossStep < 0; ++i)
		{
			const bool bWasLow = Sim->IsIceDepositLow(*Dep("Ice_A"));
			CountBefore = CountAlerts(TEXT("RUNNING LOW"));
			Step(60.0);
			if (!bWasLow && !Sim->IsIceDepositLow(*Dep("Ice_A"))) { bQuiet &= CountAlerts(TEXT("RUNNING LOW")) == 0; }
			if (Sim->IsIceDepositLow(*Dep("Ice_A")))
			{
				CrossStep = i;
				CountAtCross = CountAlerts(TEXT("RUNNING LOW")) - CountBefore;
			}
		}
		Expect(CrossStep >= 0 && bQuiet && CountAtCross == 1 && CountAlerts(TEXT("ICE_A RUNNING LOW")) == 1,
			FString::Printf(TEXT("I6 quiet above the line, exactly one alert on the step Ice_A crossed it (step %d, dt 60 s)"), CrossStep));

		// I7. Text: names the deposit, the tonnage and the fix. Ice_B is still
		// unsurveyed, so the fix is to find it.
		Expect(LastAlert().Contains(TEXT("ICE_A RUNNING LOW")) && LastAlert().Contains(TEXT("Survey for more ice")) && Sim->HasUndiscoveredIce(),
			FString::Printf(TEXT("I7 alert names the deposit and the fix: \"%s\""), *LastAlert()));
		Expect(Sim->GetIceExpansionStage() == 0, TEXT("I7 interrupt stage 0 (find more ice) while Ice_B is unsurveyed"));

		// I8. No re-fire while it keeps falling.
		for (int32 i = 0; i < 2 * 1440; ++i) { Step(60.0); }
		Expect(CountAlerts(TEXT("RUNNING LOW")) == 1 && Sim->IsIceDepositLow(*Dep("Ice_A")),
			TEXT("I8 two more sols of drawing: still one alert, still low"));

		// I9. The expansion stages, then the setup for the save: Ice_B found
		// (healthy, out of the grid) -> carry the grid -> a pylon covers it.
		Sim->Debug_RevealDeposit(FName("Ice_B"));
		const FVector IceBLoc = Dep("Ice_B")->LocationCm;
		const int32 StageFound = Sim->GetIceExpansionStage();
		Sim->Debug_PlaceInstant(FName("Pylon"), FVector(IceBLoc.X, IceBLoc.Y - 1000.0, 0.0));
		const int32 StageGrid = Sim->GetIceExpansionStage();
		Expect(StageFound == 1 && StageGrid == 2 && !Sim->HasUndiscoveredIce(),
			FString::Printf(TEXT("I9 Ice_B found -> stage %d (carry the grid); a pylon covers it -> stage %d (drill it)"), StageFound, StageGrid));
		SetLeft("Ice_B", Frac * 60000.0 + 70.0);
		Expect(!Sim->IsIceDepositLow(*Dep("Ice_B")) && CountAlerts(TEXT("RUNNING LOW")) == 1,
			TEXT("I9 Ice_B just above the line (a drill batch is 40 kg): not low, no second alert"));

		// I10. Save, then the load-time latch REBUILD: raise Ice_A (dirty
		// state), EMPTY the latch, load. Ice_A must come back low and must not
		// be announced again - only the rebuild can have remembered it.
		Expect(Sim->SaveColony(TEXT("icelowtest"), Err), TEXT("I10 saved 'icelowtest'"));
		SetLeft("Ice_A", 30000.0);
		Sim->Debug_ClearIceLowLatch();
		const bool bLoaded = Sim->LoadColony(TEXT("icelowtest"), Err);
		Expect(bLoaded && Dep("Ice_A") && Dep("Ice_B") && Dep("Ice_A")->RemainingKg < Frac * 30000.0 && Near(Dep("Ice_A")->InitialKg, 30000.0, 1e-6)
			&& Near(Dep("Ice_B")->InitialKg, 60000.0, 1e-6) && Sim->IsIceDepositLow(*Dep("Ice_A")) && !Sim->IsIceDepositLow(*Dep("Ice_B")),
			TEXT("I10 load: Ice_A back below the line, InitialKg re-derived from data, Ice_B above"));
		for (int32 i = 0; i < 5; ++i) { Step(60.0); }
		Expect(CountAlerts(TEXT("RUNNING LOW")) == 1,
			TEXT("I10 latch emptied before the load: Ice_A is not announced again (the rebuild marked it)"));

		// I11. The reset half: announce Ice_B (latch now holds both), then
		// load the earlier save - Ice_B is back above the line and the latch
		// must be forgotten, so the real crossing at dt 1 s alerts again.
		SetLeft("Ice_B", 14000.0);
		Step(60.0);
		Expect(CountAlerts(TEXT("ICE_B RUNNING LOW")) == 1 && CountAlerts(TEXT("RUNNING LOW")) == 2,
			TEXT("I11 Ice_B pushed under the line: its alert fires (2 in all)"));
		Expect(Sim->LoadColony(TEXT("icelowtest"), Err) && Near(Dep("Ice_B")->RemainingKg, Frac * 60000.0 + 70.0, 1e-6) && !Sim->IsIceDepositLow(*Dep("Ice_B")),
			TEXT("I11 loaded the earlier save: Ice_B back above the line"));
		Sim->Debug_PlaceInstant(FName("IceDrill"), IceBLoc);
		// Stage 3: a drill alone is not enough - its ice is a long haul from
		// any Water Plant. A far plant does not count; a finished one beside
		// the drill ends the prompt.
		const auto LastIdOf = [&](const TCHAR* Def) { int32 Id = 0; for (const FRHBuildingInstance& B : Sim->GetBuildings()) { if (B.DefName == FName(Def)) { Id = B.Id; } } return Id; };
		const int32 DrillB = LastIdOf(TEXT("IceDrill"));
		const int32 StageDrilled = Sim->GetIceExpansionStage();
		Sim->Debug_PlaceInstant(FName("WaterPlant"), FVector(2500.0, -1500.0, 0.0));
		const int32 PlantFar = LastIdOf(TEXT("WaterPlant"));
		const int32 StageFarPlant = Sim->GetIceExpansionStage();
		Sim->Debug_PlaceInstant(FName("WaterPlant"), FVector(IceBLoc.X - 1200.0, IceBLoc.Y - 1000.0, 0.0));
		const int32 PlantNear = LastIdOf(TEXT("WaterPlant"));
		const int32 StageNearPlant = Sim->GetIceExpansionStage();
		Expect(StageDrilled == 3 && StageFarPlant == 3 && StageNearPlant == -1 && PlantFar != PlantNear,
			FString::Printf(TEXT("I11 drilled, no plant near it -> stage %d (melt it where you drill it); a far plant still %d; a plant beside the drill -> %d"),
				StageDrilled, StageFarPlant, StageNearPlant));

		// The haul from a far drill goes to the NEAREST plant, though the far
		// one stands earlier in building order.
		Sim->Debug_AddSolidAt(DrillB, FName("Ice"), 500.0, true);
		Sim->Debug_StepTaskBoard();
		const int32 HaulDest = Sim->Debug_GetOpenHaulDestFrom(DrillB, FName("Ice"));
		Expect(DrillB != 0 && HaulDest == PlantNear,
			FString::Printf(TEXT("I11 the Ice_B drill's ice is posted to the nearest Water Plant #%d, not the first one #%d (posted to #%d)"), PlantNear, PlantFar, HaulDest));
		// With every plant's hopper full, ice waits at its drill - it is not
		// carried to a store (where nothing would ever bring it back). A
		// construction material (Struct) still overflows to the nearest store.
		{
			Sim->Debug_PlaceInstant(FName("IceDrill"), FVector(IceBLoc.X + 300.0, IceBLoc.Y, 0.0));
			const int32 DrillB2 = LastIdOf(TEXT("IceDrill"));
			Sim->Debug_AddSolidAt(PlantFar, FName("Ice"), 200.0, false);
			Sim->Debug_AddSolidAt(PlantNear, FName("Ice"), 200.0, false);
			Sim->Debug_AddSolidAt(DrillB2, FName("Ice"), 500.0, true);
			Sim->Debug_AddSolidAt(DrillB2, FName("Struct"), 500.0, true);
			Sim->Debug_StepTaskBoard();
			const int32 IceDest = Sim->Debug_GetOpenHaulDestFrom(DrillB2, FName("Ice"));
			const int32 StructDest = Sim->Debug_GetOpenHaulDestFrom(DrillB2, FName("Struct"));
			Expect(DrillB2 != DrillB && IceDest == 0 && StructDest != 0,
				FString::Printf(TEXT("I11 plants full: the ice waits at its drill (posted to #%d), Struct still overflows to a store (#%d)"), IceDest, StructDest));
		}
		int32 CrossB = -1, AtCrossB = 0, BeforeB = 0;
		for (int32 i = 0; i < 4000 && CrossB < 0; ++i)
		{
			BeforeB = CountAlerts(TEXT("RUNNING LOW"));
			Step(1.0);
			if (Sim->IsIceDepositLow(*Dep("Ice_B")))
			{
				CrossB = i;
				AtCrossB = CountAlerts(TEXT("RUNNING LOW")) - BeforeB;
			}
		}
		Expect(CrossB >= 0 && AtCrossB == 1 && CountAlerts(TEXT("ICE_B RUNNING LOW")) == 2 && CountAlerts(TEXT("ICE_A RUNNING LOW")) == 1 && CountAlerts(TEXT("RUNNING LOW")) == 3,
			FString::Printf(TEXT("I11 the latch was reset by the load: Ice_B alerts again on the step it crosses (step %d, dt 1 s); Ice_A stays at one"), CrossB));

		// I12. The dead end: both ice bodies low, nothing unsurveyed. No
		// interrupt may show (it could never be resolved), and the alert names
		// a lever that exists instead of a survey.
		Expect(!Sim->HasUndiscoveredIce() && Sim->IsIceDepositLow(*Dep("Ice_A")) && Sim->IsIceDepositLow(*Dep("Ice_B")) && Sim->GetIceExpansionStage() == -1,
			TEXT("I12 both ice bodies low, none unsurveyed: HasUndiscoveredIce false, expansion stage -1"));
		Expect(LastAlert().Contains(TEXT("ICE_B RUNNING LOW")) && LastAlert().Contains(TEXT("Ration water")) && !LastAlert().Contains(TEXT("Survey")),
			FString::Printf(TEXT("I12 the last-ice alert gives no survey advice: \"%s\""), *LastAlert()));

		Sim->OnAlert.Remove(AlertH);
		UE_LOG(LogRedHopeSim, Display, TEXT("=== ICE LOW TEST END: %d failure(s) ==="), Fails);
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return Fails > 0 ? 1 : 0;
	}

	// v25 self-test: STATION TIERS - SeatCount/YieldMul seat math, the
	// workstation fabrication bonus, discovery-driven room UNLOCK + banked
	// FUNDING, the Infirmary evac grace, and unlock persistence across a
	// save/load. `-tiers`.
	if (FParse::Param(*Params, TEXT("tiers")))
	{
		UE_LOG(LogRedHopeSim, Display, TEXT("=== STATION TIERS TEST (v25) ==="));
		const int32 StepsPerSolH = (int32)(URHSimClockSubsystem::SolLengthSimSeconds / URHSimClockSubsystem::EraStepSimSeconds);
		const auto RunSols = [&](double Sols)
		{
			for (int32 S = 0; S < (int32)(Sols * StepsPerSolH); ++S)
			{
				// 6 drinkers decay potability fast - feed enough fresh melt to
				// hold it above the floor, or the water Hope penalty pins the
				// smoothed mood just under the 85 discovery gate.
				Sim->Debug_AddFreshWater(500.0 * URHSimClockSubsystem::EraStepSimSeconds / (double)URHSimClockSubsystem::SolLengthSimSeconds);
				Clock->Debug_AdvanceSimSeconds(URHSimClockSubsystem::EraStepSimSeconds);
				Sim->EraStep(URHSimClockSubsystem::EraStepSimSeconds);
			}
		};
		URHDefinitionsSubsystem* DefsSub = World->GetSubsystem<URHDefinitionsSubsystem>();
		// Pure-data: the tier ladder's stations ride the synced DT_Rooms
		// (2026-07-16) - assert the arithmetic-bearing columns instead of
		// injecting, so this suite fails loudly on DT/CSV drift.
		const auto CheckRoom = [&](const TCHAR* Name, FName Function, int32 Tier, int32 Seats, float Yield, bool bActive) -> bool
		{
			const FRHRoomRow* Row = DefsSub->GetRoom(FName(Name));
			if (!Row || Row->SliceActive != bActive || Row->Function != Function
				|| Row->Tier != Tier || Row->SeatCount != Seats || Row->YieldMul != Yield)
			{
				UE_LOG(LogRedHopeSim, Error, TEXT("TIERS: room %s missing/drifted - DT/CSV drift"), Name);
				return false;
			}
			return true;
		};
		if (!CheckRoom(TEXT("WorkbenchLarge"), FName("Workstation"), 2, 2, 1.35f, true)) { return 1; }
		if (!CheckRoom(TEXT("ChemTableLarge"), FName("Lab"), 2, 1, 1.35f, true)) { return 1; }
		if (!CheckRoom(TEXT("Workshop"), FName("Workstation"), 3, 3, 1.8f, false)) { return 1; } // DORMANT until unlocked
		if (!CheckRoom(TEXT("Infirmary"), FName("Infirmary"), 1, 1, 1.0f, true)) { return 1; }
		// The unlocking discovery stays a SYNTHETIC inject (test-only row, not
		// in the CSV): first in the sequence (Order 0 outranks the authored
		// rows), cheap enough to pop fast once the mood crosses 85.
		FRHDiscoveryRow DUnlock; DUnlock.DisplayName = TEXT("Regolith Ceramics II");
		DUnlock.Order = 0; DUnlock.LabSeatHours = 50.f; DUnlock.HopeBonus = 2.f;
		DUnlock.FundingKg = 500.f; DUnlock.UnlockRoom = FName("Workshop");
		DUnlock.Alert = TEXT("BREAKTHROUGH — ceramics strong enough for machine housings.");
		DUnlock.SliceActive = true;
		DefsSub->Debug_InjectDiscovery(FName("TestCeramics"), DUnlock);

		// A flourishing colony (the -discovery recipe, plus the tier benches).
		FString R;
		Sim->ExtendShaft(1, FVector(1000.f, 1000.f, 0.f));
		Sim->ExcavateFloor(-1, 10, R);
		Sim->Debug_PlaceInstant(FName("SolarArray"), FVector(3500.f, 1000.f, 0.f));
		Sim->Debug_PlaceInstant(FName("BatteryBank"), FVector(1000.f, 3500.f, 0.f));
		Sim->Debug_PlaceInstant(FName("AirFilter"), FVector(1000.f, 1500.f, 0.f), -1);
		Sim->AddStock(FName("Oxygen"), 6000.0);
		Sim->AddStock(FName("Water"), 4000.0);
		Sim->AddStock(FName("Food"), 6000.0);
		RunSols(2.5);
		const int32 Housed = Sim->Debug_AddColonists(6);
		Sim->DesignateRoom(-1, 0, FName("LivingQuarters"), R);
		Sim->DesignateRoom(-1, 1, FName("Workstation"), R);     // T1: 1 seat, x1.0
		Sim->DesignateRoom(-1, 2, FName("WorkbenchLarge"), R);  // T2: 2 seats, x1.35
		Sim->DesignateRoom(-1, 3, FName("Lab"), R);             // T1: 1 seat, x1.0
		Sim->DesignateRoom(-1, 4, FName("ChemTableLarge"), R);  // T2: 1 seat, x1.35
		Sim->DesignateRoom(-1, 5, FName("Dining"), R);
		// Six colonists drain a comforts crate quickly - stock several so the
		// comforts fraction holds at 1.0 through the whole march.
		Sim->Debug_DeliverCargo(FName("LuxuryGoods"));
		Sim->Debug_DeliverCargo(FName("LuxuryGoods"));
		Sim->Debug_DeliverCargo(FName("LuxuryGoods"));
		RunSols(0.2);

		// 1) Seat math: 5 job seats (1+2+1+1), yield sums weigh the tiers.
		const double EvacBase = Sim->GetEffectiveEvacSols();
		Sim->Debug_SetWorkstationFabBonus(0.1);
		UE_LOG(LogRedHopeSim, Display, TEXT("TIERS seats: housed=%d filled=%d labSum=%.2f wsSum=%.2f fabMul=%.3f (expect 6, 5, 2.35, 3.70, 1.370)"),
			Housed, Sim->GetColonyHope().FilledSeats, Sim->GetLabYieldSeatSum(),
			Sim->GetWorkstationYieldSeatSum(), Sim->GetWorkstationFabMul());

		// 2) The dormant Workshop refuses designation (locked by research).
		FString RefuseMsg;
		const bool bRefused = !Sim->DesignateRoom(-1, 6, FName("Workshop"), RefuseMsg);
		UE_LOG(LogRedHopeSim, Display, TEXT("TIERS locked: workshopRefused=%d (expect 1: '%s')"),
			(int32)bRefused, *RefuseMsg);

		// 3) March to the breakthrough: the smoothed mood clears 85 once the
		// water and comforts hold; 2.35 yield-seats x 48 seat-h/sol clears 50
		// seat-hours fast. Funding banks; the Workshop unlocks and ACCEPTS.
		RunSols(12.0);
		FString AcceptMsg;
		const bool bAccepted = Sim->DesignateRoom(-1, 6, FName("Workshop"), AcceptMsg);
		UE_LOG(LogRedHopeSim, Display, TEXT("TIERS unlock: found=%d pending=%.0f workshopAccepted=%d smoothed=%.1f accruing=%d progress=%.2f next=%s (expect >=1, 500, 1)"),
			Sim->GetDiscoveryLog().Num(), Sim->GetPendingResearchFundingKg(), (int32)bAccepted,
			Sim->GetHopeSmoothed(), (int32)Sim->IsResearchAccruing(), Sim->GetDiscoveryProgress(),
			*Sim->GetNextDiscovery().ToString());

		// 4) Infirmary: designating one stretches the evac countdown by the
		// configured grace (prevention framing - more time to fix the fault).
		Sim->DesignateRoom(-1, 7, FName("Infirmary"), R);
		UE_LOG(LogRedHopeSim, Display, TEXT("TIERS infirmary: evac %.1f -> %.1f sols (expect 2.0 -> 3.0)"),
			EvacBase, Sim->GetEffectiveEvacSols());

		// 5) Save v25 round-trip: the unlock re-applies from the discovery log
		// (row flips are in-memory), seat sums re-derive, the grace holds.
		FString Err;
		Sim->SaveColony(TEXT("tierstest"), Err);
		Sim->LoadColony(TEXT("tierstest"), Err);
		const FRHRoomRow* WShop = DefsSub->GetRoom(FName("Workshop"));
		UE_LOG(LogRedHopeSim, Display, TEXT("TIERS save/load v25: workshopActive=%d labSum=%.2f evac=%.1f (expect 1, 2.35, 3.0)"),
			(int32)(WShop && WShop->SliceActive), Sim->GetLabYieldSeatSum(), Sim->GetEffectiveEvacSols());

		UE_LOG(LogRedHopeSim, Display, TEXT("=== STATION TIERS TEST END ==="));
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return 0;
	}

	// Greenhouse-agriculture self-test (2026-07-10 spec, Gates A/B/D): crops
	// with real grow time replace the flat garden when their rows go live -
	// growth stages, per-crop water/yield, soil depletion + fertilizer refill,
	// per-floor climate (regulator-gated), duct CO2/O2 exchange, save v26.
	// `-agri`.
	if (FParse::Param(*Params, TEXT("agri")))
	{
		UE_LOG(LogRedHopeSim, Display, TEXT("=== AGRICULTURE TEST (greenhouse spec Gates A/B/D) ==="));
		const int32 StepsPerSolH = (int32)(URHSimClockSubsystem::SolLengthSimSeconds / URHSimClockSubsystem::EraStepSimSeconds);
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

		// 1) Pure-data: the seven authored crops ride the synced DT_Crops,
		// ALL DORMANT (activation is the director's per-gate flip). Assert the
		// arithmetic-bearing columns of the three this test marches on.
		{
			int32 Rows = 0, Active = 0;
			DefsSub->ForEachCropRow([&](FName, const FRHCropRow& Row){ ++Rows; Active += Row.SliceActive ? 1 : 0; });
			if (Rows != 7 || Active != 0)
			{
				UE_LOG(LogRedHopeSim, Error, TEXT("AGRI: expected 7 dormant crop rows, found %d rows / %d active - DT/CSV drift"), Rows, Active);
				return 1;
			}
			const auto CheckCrop = [&](const TCHAR* Name, float Grow, float Yield, float Water, FName Band) -> bool
			{
				const FRHCropRow* Row = DefsSub->GetCrop(FName(Name));
				if (!Row || Row->GrowSols != Grow || Row->YieldKgPerSol != Yield
					|| Row->WaterKgPerSol != Water || Row->ClimateBand != Band)
				{
					UE_LOG(LogRedHopeSim, Error, TEXT("AGRI: crop %s missing/drifted - DT/CSV drift"), Name);
					return false;
				}
				return true;
			};
			if (!CheckCrop(TEXT("Carrot"), 2.f, 3.f, 3.f, FName("Temperate"))) { return 1; }
			if (!CheckCrop(TEXT("Corn"), 4.f, 2.f, 4.f, FName("Temperate"))) { return 1; }
			if (!CheckCrop(TEXT("Beans"), 3.f, 2.f, 3.f, FName("Humid"))) { return 1; }
		}

		// 2) Flip the whole catalogue live (the director's gate flip).
		{
			TArray<FName> Names;
			DefsSub->ForEachCropRow([&Names](FName Name, const FRHCropRow&){ Names.Add(Name); });
			for (const FName& Name : Names) { DefsSub->ActivateCropRow(Name); }
			if (!Sim->AreCropsLive()) { UE_LOG(LogRedHopeSim, Error, TEXT("AGRI: crops did not activate")); return 1; }
		}

		// 3) A certified 6-cell vault with power and stores for exactly two
		// beds (soil 500 = 2 x 250; no replant stock after a bed reverts).
		FString R;
		Sim->ExtendShaft(1, FVector(1000.f, 1000.f, 0.f));
		Sim->ExcavateFloor(-1, 6, R);
		Sim->Debug_PlaceInstant(FName("SolarArray"), FVector(3500.f, 1000.f, 0.f));
		Sim->Debug_PlaceInstant(FName("BatteryBank"), FVector(1000.f, 3500.f, 0.f));
		Sim->Debug_PlaceInstant(FName("AirFilter"), FVector(1000.f, 1500.f, 0.f), -1);
		Sim->AddStock(FName("Oxygen"), 6000.0);
		Sim->AddStock(FName("Water"), 4000.0);
		Sim->AddStock(FName("Food"), 2000.0);
		Sim->AddStock(FName("Soil"), 500.0);
		Sim->AddStock(FName("Seeds"), 100.0);
		RunSols(2.5); // floor certifies

		// 4) Two garden cells plant the rotation deterministically: the
		// Temperate-matched actives sorted by name are Carrot, Corn, Potato -
		// cell 0 takes Carrot (GrowSols 2), cell 1 takes Corn (GrowSols 4).
		Sim->DesignateRoom(-1, 0, FName("Garden"), R);
		Sim->DesignateRoom(-1, 1, FName("Garden"), R);
		RunSols(0.1); // the plant step
		const FName Crop0 = Sim->GetCellCrop(-1, 0), Crop1 = Sim->GetCellCrop(-1, 1);
		if (Crop0 != FName("Carrot") || Crop1 != FName("Corn"))
		{
			UE_LOG(LogRedHopeSim, Error, TEXT("AGRI: rotation planted %s/%s (expected Carrot/Corn)"), *Crop0.ToString(), *Crop1.ToString());
			return 1;
		}
		UE_LOG(LogRedHopeSim, Display, TEXT("AGRI planted: cell0=%s cell1=%s stages=%d/%d (expect Carrot, Corn, 0, 0)"),
			*Crop0.ToString(), *Crop1.ToString(), Sim->GetCellCropStage(-1, 0), Sim->GetCellCropStage(-1, 1));

		// 5) Growth is real time: one sol in, nothing has yielded. (Instant
		// stage reads run one 0.05-sol era step behind the yield ledger -
		// the bed plants at the END of the first step - so Carrot reads
		// sprout here and flips young next step; the cums are exact.)
		RunSols(0.9);
		UE_LOG(LogRedHopeSim, Display, TEXT("AGRI immature: cum=%.2f kg stages=%d/%d (expect 0.00, 0, 0 - Carrot one step shy of young)"),
			Sim->GetGardenFoodCumKg(), Sim->GetCellCropStage(-1, 0), Sim->GetCellCropStage(-1, 1));

		// 6) Carrot matures at age 2.0 and yields 3 kg/sol for the last half
		// of this march; Corn is still growing. Zero pop = tempo and skill
		// exactly 1.0, so the arithmetic is exact.
		RunSols(1.5); // age 2.5
		UE_LOG(LogRedHopeSim, Display, TEXT("AGRI first yield: cum=%.2f kg (expect 1.50 = 0.5 sol x 3)"), Sim->GetGardenFoodCumKg());

		// 7) By age 4.0 the Carrot has yielded 2.0 total mature sols; Corn
		// arrives at maturity exactly now (nothing banked yet).
		RunSols(1.5); // age 4.0
		UE_LOG(LogRedHopeSim, Display, TEXT("AGRI second: cum=%.2f kg stages=%d/%d (expect 6.00, 2, 1 - Corn one step shy of mature read)"),
			Sim->GetGardenFoodCumKg(), Sim->GetCellCropStage(-1, 0), Sim->GetCellCropStage(-1, 1));

		// 8) Both mature: 3 + 2 = 5 kg/sol.
		RunSols(1.0); // age 5.0
		UE_LOG(LogRedHopeSim, Display, TEXT("AGRI both mature: cum=%.2f kg (expect 11.00)"), Sim->GetGardenFoodCumKg());

		// 9) Climate (Gate B): a Humid setting is INERT until a regulator is
		// online; with one placed, both Temperate crops fall to x0.6.
		FString ClimErr;
		if (!Sim->SetFloorClimate(-1, FName("Humid"), ClimErr)) { UE_LOG(LogRedHopeSim, Error, TEXT("AGRI: %s"), *ClimErr); return 1; }
		if (Sim->GetFloorClimateEffective(-1) != FName("Temperate"))
		{
			UE_LOG(LogRedHopeSim, Error, TEXT("AGRI: climate took effect without a regulator"));
			return 1;
		}
		Sim->Debug_PlaceInstant(FName("HumidityRegulator"), FVector(1500.f, 1000.f, 0.f), -1);
		if (Sim->GetFloorClimateEffective(-1) != FName("Humid"))
		{
			UE_LOG(LogRedHopeSim, Error, TEXT("AGRI: regulator online but climate still inert"));
			return 1;
		}
		RunSols(1.0); // age 6.0
		UE_LOG(LogRedHopeSim, Display, TEXT("AGRI climate: cum=%.2f kg (expect 14.00 = +5 x 0.6)"), Sim->GetGardenFoodCumKg());

		// 10) The crew feeds the loop (Gates B/D): compost and CO2 accrue per
		// colonist-sol. (Yield goes tempo/skill-coloured from here - the exact
		// asserts stay on the crew-independent pools.)
		Sim->DesignateRoom(-1, 2, FName("LivingQuarters"), R);
		Sim->DesignateRoom(-1, 3, FName("Dining"), R);
		const int32 Housed = Sim->Debug_AddColonists(2);
		RunSols(1.0);
		UE_LOG(LogRedHopeSim, Display, TEXT("AGRI crew pools: housed=%d compost=%.2f kg co2=%.2f kg (expect 2, 2.00, 2.00)"),
			Housed, Sim->GetStock(FName("Compost")), Sim->GetColonyCO2Kg());

		// 11) Ducts (Gate D): the garden floor breathes with the colony. Two
		// producing cells draw 0.5 kg CO2/sol each; the crew adds 2/sol; the
		// pool holds, so the bonus and the O2 emission run the whole sol.
		FString DuctErr;
		if (!Sim->DesignateDuct(-1, DuctErr)) { UE_LOG(LogRedHopeSim, Error, TEXT("AGRI: %s"), *DuctErr); return 1; }
		const double O2Before = Sim->GetStock(FName("Oxygen"));
		RunSols(1.0);
		UE_LOG(LogRedHopeSim, Display, TEXT("AGRI duct: co2=%.2f kg o2Delta=%+.2f kg cum=%.2f (expect 3.00 = 2+2-1; o2 includes crew draw + 0.40 emitted)"),
			Sim->GetColonyCO2Kg(), Sim->GetStock(FName("Oxygen")) - O2Before, Sim->GetGardenFoodCumKg());

		// 12) Dig-spoil chemicals (Gate B): fresh excavation with the crop
		// layer live banks a fertilizer-chain fraction.
		Sim->ExtendShaft(2, FVector(1000.f, 1000.f, 0.f));
		Sim->ExcavateFloor(-2, 4, R);
		const double Chems = Sim->GetStock(FName("SpoilChemicals"));
		if (Chems <= 0.0) { UE_LOG(LogRedHopeSim, Error, TEXT("AGRI: dig produced no SpoilChemicals")); return 1; }
		UE_LOG(LogRedHopeSim, Display, TEXT("AGRI spoil chems: %.2f kg banked (expect > 0: excavation x fraction)"), Chems);

		// 13) Soil spends and fertilizer refills (Gate B): at 250 kg/sol both
		// beds hit zero this sol; 125 kg Fertilizer restores exactly ONE in
		// place, the other reverts (and cannot replant - soil stock is spent).
		Sim->AddStock(FName("Soil"), -Sim->GetStock(FName("Soil")));
		Sim->AddStock(FName("Fertilizer"), 125.0);
		Sim->Debug_SetCropSoilDepleteKgPerSol(250.0);
		RunSols(1.0);
		Sim->Debug_SetCropSoilDepleteKgPerSol(25.0);
		UE_LOG(LogRedHopeSim, Display, TEXT("AGRI soil economy: planted=%d fert=%.2f kg (expect 1, 0.00 - one bed refilled, one reverted)"),
			Sim->GetPlantedCellCount(), Sim->GetStock(FName("Fertilizer")));
		if (Sim->GetPlantedCellCount() != 1)
		{
			UE_LOG(LogRedHopeSim, Error, TEXT("AGRI: expected exactly one surviving bed"));
			return 1;
		}

		// 14) Save v26 round-trip: crop cells, climate, duct, CO2 pool.
		FString Err;
		const FName CropBefore = Sim->GetCellCrop(-1, 0).IsNone() ? Sim->GetCellCrop(-1, 1) : Sim->GetCellCrop(-1, 0);
		const double CO2Before = Sim->GetColonyCO2Kg();
		Sim->SaveColony(TEXT("agritest"), Err);
		Sim->LoadColony(TEXT("agritest"), Err);
		const FName CropAfter = Sim->GetCellCrop(-1, 0).IsNone() ? Sim->GetCellCrop(-1, 1) : Sim->GetCellCrop(-1, 0);
		UE_LOG(LogRedHopeSim, Display, TEXT("AGRI save/load v26: crop %s->%s co2 %.2f->%.2f climate=%s ducted=%d (expect identical, Humid, 1)"),
			*CropBefore.ToString(), *CropAfter.ToString(), CO2Before, Sim->GetColonyCO2Kg(),
			*Sim->GetFloorClimateSetting(-1).ToString(), (int32)Sim->IsFloorDucted(-1));
		if (CropBefore != CropAfter || !Sim->IsFloorDucted(-1))
		{
			UE_LOG(LogRedHopeSim, Error, TEXT("AGRI: save/load round-trip drifted"));
			return 1;
		}
		RunSols(0.5); // the loaded colony marches on without incident

		UE_LOG(LogRedHopeSim, Display, TEXT("=== AGRICULTURE TEST END ==="));
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return 0;
	}

	// Alive-pass self-test: SKILLS (mastery ramps output 1x->2x), CREW MOMENTS
	// (deterministic cadence; disputes gated on needs/Hope), the FIRST MARTIAN
	// HARVEST milestone, the diverse-name roster. Save v24. `-alive`.
	if (FParse::Param(*Params, TEXT("alive")))
	{
		UE_LOG(LogRedHopeSim, Display, TEXT("=== ALIVE PASS TEST ==="));
		const int32 StepsPerSolH = (int32)(URHSimClockSubsystem::SolLengthSimSeconds / URHSimClockSubsystem::EraStepSimSeconds);
		int32 Moments[4] = { 0, 0, 0, 0 };
		Sim->OnCrewMoment.AddLambda([&](uint8 Type, int32, int32, const FString&){ if (Type < 4) { ++Moments[Type]; } });
		int32 MilestoneCount = 0;
		Sim->OnMilestone.AddLambda([&](const FString&){ ++MilestoneCount; });
		const auto RunSols = [&](double Sols)
		{
			const double StepSols = URHSimClockSubsystem::EraStepSimSeconds / (double)URHSimClockSubsystem::SolLengthSimSeconds;
			for (int32 S = 0; S < (int32)(Sols * StepsPerSolH); ++S)
			{
				Sim->Debug_AddFreshWater(100.0 * StepSols); // keep potability out of the Hope math
				Clock->Debug_AdvanceSimSeconds(URHSimClockSubsystem::EraStepSimSeconds);
				Sim->EraStep(URHSimClockSubsystem::EraStepSimSeconds);
			}
		};
		// A supplied 4-person colony with Garden + Lab posts.
		FString R;
		Sim->ExtendShaft(1, FVector(1000.f, 1000.f, 0.f));
		Sim->ExcavateFloor(-1, 6, R);
		Sim->Debug_PlaceInstant(FName("SolarArray"), FVector(3500.f, 1000.f, 0.f));
		Sim->Debug_PlaceInstant(FName("BatteryBank"), FVector(1000.f, 3500.f, 0.f));
		Sim->Debug_PlaceInstant(FName("AirFilter"), FVector(1000.f, 1500.f, 0.f), -1);
		Sim->AddStock(FName("Oxygen"), 4000.0);
		Sim->AddStock(FName("Water"), 4000.0);
		Sim->AddStock(FName("Food"), 4000.0);
		RunSols(2.5);
		Sim->Debug_AddColonists(4);
		Sim->DesignateRoom(-1, 0, FName("LivingQuarters"), R);
		Sim->DesignateRoom(-1, 1, FName("LivingQuarters"), R);
		Sim->DesignateRoom(-1, 2, FName("Garden"), R);
		Sim->DesignateRoom(-1, 3, FName("Lab"), R);

		// 1) The roster: distinct full names + a derived disposition each.
		const TArray<FRHColonist>& Roster = Sim->GetColonists();
		TSet<FString> Names;
		for (const FRHColonist& C : Roster) { Names.Add(C.Name); }
		UE_LOG(LogRedHopeSim, Display, TEXT("ALIVE roster: %d colonists %d distinct names, #1 %s (%s) (expect 4, 4, Amara Adeyemi, non-empty trait)"),
			Roster.Num(), Names.Num(), Roster.Num() > 0 ? *Roster[0].Name : TEXT("-"),
			Roster.Num() > 0 ? URHSimWorldSubsystem::TraitFor(Roster[0].Id) : TEXT("-"));

		// 2) SKILLS: after 6 sols on the job, mul = 1 + 6/30 = 1.20 exactly.
		RunSols(6.0);
		UE_LOG(LogRedHopeSim, Display, TEXT("ALIVE skills: garden mul=%.3f lab mul=%.3f (expect 1.200, 1.200 - 6 sols / 30 mastery)"),
			Sim->GetCrewSkillMul(FName("Garden")), Sim->GetCrewSkillMul(FName("Lab")));

		// 3) MOMENTS: supplied + Hope well above the dispute gate -> pastimes /
		// join-ins only, on the interval cadence. Population exists for 6.0
		// sols x 2 beats/sol = 12, minus one to fp-accumulation flooring the
		// final beat = 11 exactly (deterministic - same floats every run).
		UE_LOG(LogRedHopeSim, Display, TEXT("ALIVE moments: pastime=%d join=%d dispute=%d total=%d (expect >0, >=0, 0, 11)"),
			Moments[0], Moments[1], Moments[2], Moments[0] + Moments[1] + Moments[2]);

		// 4) DISPUTES surface when needs go unmet (food emptied) - and stop
		// once restored.
		Sim->AddStock(FName("Food"), -Sim->GetStock(FName("Food")));
		RunSols(1.0);
		const int32 DisputesStarving = Moments[2];
		Sim->AddStock(FName("Food"), 4000.0);
		UE_LOG(LogRedHopeSim, Display, TEXT("ALIVE disputes: starving hour -> %d disputes (expect 2 - every beat while unmet)"),
			DisputesStarving);

		// 5) FIRST HARVEST: plant the garden, accumulate 1 kg -> milestone once,
		// +4 Hope milestone, a Celebrate moment.
		Sim->Debug_DeliverCargo(FName("SoilPallet"));
		Sim->Debug_DeliverCargo(FName("SeedVault"));
		const double Milestones0 = Sim->GetColonyHope().Milestones;
		RunSols(1.0);
		UE_LOG(LogRedHopeSim, Display, TEXT("ALIVE harvest: had=%d celebrations=%d milestones %.1f -> %.1f (expect 1, >=1, +4.0)"),
			(int32)Sim->HasHadFirstHarvest(), Moments[3], Milestones0, Sim->GetColonyHope().Milestones);

		// 6) Save v24 round-trip: skills + harvest flag + moment counter survive.
		const double SkillB = Sim->GetCrewSkillMul(FName("Garden"));
		FString Err;
		Sim->SaveColony(TEXT("alivetest"), Err);
		Sim->LoadColony(TEXT("alivetest"), Err);
		UE_LOG(LogRedHopeSim, Display, TEXT("ALIVE save/load v24: skill %.3f->%.3f harvest %d->%d (expect identical)"),
			SkillB, Sim->GetCrewSkillMul(FName("Garden")), 1, (int32)Sim->HasHadFirstHarvest());

		UE_LOG(LogRedHopeSim, Display, TEXT("=== ALIVE PASS TEST END ==="));
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

	// D4a self-test (2026-10-17): the FOOD RUNWAY - a Hope line that grows as the
	// larder (stock / today's draw) falls under RH_Config FoodRunwayFloorSols,
	// and a one-shot FOOD RUNNING LOW alert on the downward crossing that
	// re-arms only past FoodRunwayRearmSols. Pure-data pins, the zero-pop
	// silence, the line's linear shape and cap, the crossing on its own step,
	// no re-fire, hysteresis, re-arm, the silent latch when the larder is already
	// empty (LIFE SUPPORT owns the banner), the load-time latch RE-DERIVE (the
	// runtime latch is made to DISAGREE with the saved state before each load, so
	// only the re-derive can pass), and 60 s vs 6 s band parity of the line, the
	// smoothed mood and the alert's step. Fails loudly. `-food`.
	if (FParse::Param(*Params, TEXT("food")))
	{
		UE_LOG(LogRedHopeSim, Display, TEXT("=== FOOD RUNWAY TEST (D4a, 2026-10-17) ==="));
		int32 Fails = 0;
		const auto Expect = [&](bool bOk, const FString& What)
		{
			if (bOk) { UE_LOG(LogRedHopeSim, Display, TEXT("FOOD ok: %s"), *What); }
			else { UE_LOG(LogRedHopeSim, Error, TEXT("FOOD FAIL: %s"), *What); ++Fails; }
		};
		const auto Near = [](double A, double B, double Tol) { return FMath::Abs(A - B) <= Tol; };
		const auto Step = [&](double Dt)
		{
			Sim->Debug_AddFreshWater(50.0); // potability stays out of the Hope math
			Sim->Debug_SetBatteryWh(1.0e9); // the circulator stays up
			Clock->Debug_AdvanceSimSeconds(Dt);
			Sim->EraStep((float)Dt);
		};
		const auto RunSteps = [&](int32 N, double Dt = 60.0) { for (int32 i = 0; i < N; ++i) { Step(Dt); } };
		TArray<FString> Alerts;
		const FDelegateHandle AlertH = Sim->OnAlert.AddLambda([&Alerts](const FString& S) { Alerts.Add(S); });
		const auto CountAlerts = [&Alerts](const TCHAR* Needle) { int32 N = 0; for (const FString& A : Alerts) { N += A.Contains(Needle) ? 1 : 0; } return N; };
		const auto LastAlertWith = [&Alerts](const TCHAR* Needle)
		{
			for (int32 i = Alerts.Num() - 1; i >= 0; --i) { if (Alerts[i].Contains(Needle)) { return Alerts[i]; } }
			return FString();
		};
		// Food stock that gives exactly S sols of runway at today's population.
		// Colonists FIRST, then this - the target scales with the live Pop.
		const auto SetRunway = [&](double S)
		{
			Sim->AddStock(FName("Food"), S * Sim->GetPopulation() * Sim->GetColonistFoodKgPerSol() - Sim->GetStock(FName("Food")));
		};
		const auto Pen = [&]() { return Sim->GetColonyHope().FoodPenalty; };
		const TCHAR* FoodKey = TEXT("FOOD RUNNING LOW");
		URHDefinitionsSubsystem* DefsSub = World->GetSubsystem<URHDefinitionsSubsystem>();
		FString Err;

		// F0. Pure data: the three rows ride the synced DT_Config (the -1
		// sentinel fails loudly on a DT that was never synced), and the sim
		// reads them.
		{
			const bool bData = Near(DefsSub->GetConfigScalar(FName("HopeFoodPenalty"), -1.0), 10.0, 1e-9)
				&& Near(DefsSub->GetConfigScalar(FName("FoodRunwayFloorSols"), -1.0), 10.0, 1e-9)
				&& Near(DefsSub->GetConfigScalar(FName("FoodRunwayRearmSols"), -1.0), 15.0, 1e-9);
			Expect(bData && Near(Sim->GetHopeFoodPenaltyMax(), 10.0, 1e-9) && Near(Sim->GetFoodRunwayFloorSols(), 10.0, 1e-9) && Near(Sim->GetFoodRunwayRearmSols(), 15.0, 1e-9),
				FString::Printf(TEXT("F0 RH_Config HopeFoodPenalty 10 / FoodRunwayFloorSols 10 / FoodRunwayRearmSols 15, and the sim reads %.0f / %.0f / %.0f (DT/CSV)"),
					Sim->GetHopeFoodPenaltyMax(), Sim->GetFoodRunwayFloorSols(), Sim->GetFoodRunwayRearmSols()));
			if (!bData)
			{
				Sim->OnAlert.Remove(AlertH);
				UE_LOG(LogRedHopeSim, Display, TEXT("=== FOOD RUNWAY TEST END: %d failure(s) ==="), Fails);
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
				return 1;
			}
		}

		// Certified 4-cell vault (the -water setup); nobody home yet.
		Sim->ExtendShaft(1, FVector(1000.f, 1000.f, 0.f));
		Sim->ExcavateFloor(-1, 4, Err);
		Sim->Debug_PlaceInstant(FName("SolarArray"), FVector(3500.f, 1000.f, 0.f));
		Sim->Debug_PlaceInstant(FName("BatteryBank"), FVector(1000.f, 3500.f, 0.f));
		Sim->Debug_PlaceInstant(FName("AirFilter"), FVector(1000.f, 1500.f, 0.f), -1);
		Sim->AddStock(FName("Oxygen"), 3000.0);
		Sim->AddStock(FName("Water"), 4000.0);
		RunSteps(50);

		// F1. Zero population: an empty larder is silent and weighs on no one.
		Sim->AddStock(FName("Food"), -Sim->GetStock(FName("Food")));
		RunSteps(10);
		Expect(Sim->GetPopulation() == 0 && Sim->GetFoodRunwaySols() >= 1.0e8 && Pen() == 0.0 && CountAlerts(FoodKey) == 0,
			FString::Printf(TEXT("F1 no crew, empty larder: runway sentinel %.0e, FoodPenalty %.1f, %d food alerts"), Sim->GetFoodRunwaySols(), Pen(), CountAlerts(FoodKey)));

		// F2. Plenty: crew first, THEN the larder (the target scales with Pop).
		const int32 Added = Sim->Debug_AddColonists(4);
		SetRunway(40.0);
		RunSteps(2);
		Expect(Added == 4 && Pen() == 0.0 && Near(Sim->GetFoodRunwaySols(), 39.9, 1e-6) && CountAlerts(FoodKey) == 0 && CountAlerts(TEXT("LIFE SUPPORT")) == 0,
			FString::Printf(TEXT("F2 4 crew, 40 sols of food: runway %.4f (expect 39.9), FoodPenalty %.1f, no alerts"), Sim->GetFoodRunwaySols(), Pen()));

		// F3. The crossing is detected on its own step: 10.05 sols is quiet,
		// 9.95 alerts once, naming the (floored) sols and the fix.
		SetRunway(10.5);
		RunSteps(9);
		Expect(Near(Sim->GetFoodRunwaySols(), 10.05, 1e-6) && CountAlerts(FoodKey) == 0 && Pen() == 0.0,
			FString::Printf(TEXT("F3 runway %.4f (expect 10.05): no alert, no penalty yet"), Sim->GetFoodRunwaySols()));
		RunSteps(2);
		{
			const double R = Sim->GetFoodRunwaySols();
			Expect(Near(R, 9.95, 1e-6) && CountAlerts(FoodKey) == 1 && LastAlertWith(FoodKey).Contains(TEXT("about 9 sols left")) && LastAlertWith(FoodKey).Contains(TEXT("Gardening Station"))
				&& Near(Pen(), 10.0 * (10.0 - R) / 10.0, 1e-9),
				FString::Printf(TEXT("F3 runway %.4f: exactly 1 alert (\"%s\"), FoodPenalty %.4f"), R, *LastAlertWith(FoodKey), Pen()));
		}

		// F4. The line is linear from 0 at the floor to the cap at empty, and
		// Total falls by exactly the line. Pure reads, no stepping.
		{
			SetRunway(5.0);   const double P5 = Pen();   const double T5 = Sim->GetColonyHope().Total;
			SetRunway(2.5);   const double P25 = Pen();
			SetRunway(0.0);   const double P0 = Pen();
			SetRunway(40.0);  const double P40 = Pen();  const double T40 = Sim->GetColonyHope().Total;
			Expect(Near(P5, 5.0, 1e-9) && Near(P25, 7.5, 1e-9) && P0 == 10.0 && P40 == 0.0 && Near(T40 - T5, 5.0, 1e-9),
				FString::Printf(TEXT("F4 FoodPenalty: runway 5 -> %.3f, 2.5 -> %.3f, 0 -> %.3f (the cap), 40 -> %.3f; Total(40) - Total(5) = %.3f (expect 5)"),
					P5, P25, P0, P40, T40 - T5));
		}

		// F5. One-shot: a whole sol deeper into the episode stays at one alert.
		SetRunway(8.0);
		RunSteps(20);
		Expect(CountAlerts(FoodKey) == 1, FString::Printf(TEXT("F5 a sol deeper under the floor: still %d food alert"), CountAlerts(FoodKey)));

		// F6. Hysteresis: 13.95 sols is back over the floor but not over the
		// re-arm line, so dipping again does not repeat the alert.
		SetRunway(14.0);
		RunSteps(1);
		SetRunway(9.0);
		RunSteps(1);
		Expect(CountAlerts(FoodKey) == 1, FString::Printf(TEXT("F6 recovered only to 14 (re-arm 15), dipped to 9: still %d food alert"), CountAlerts(FoodKey)));

		// F7. Re-arm: past 15 the next real drop alerts again.
		SetRunway(16.0);
		RunSteps(1);
		SetRunway(9.0);
		RunSteps(1);
		Expect(CountAlerts(FoodKey) == 2 && LastAlertWith(FoodKey).Contains(TEXT("about 8 sols left")),
			FString::Printf(TEXT("F7 recovered past 15, dropped to 9: %d food alerts, newest \"%s\""), CountAlerts(FoodKey), *LastAlertWith(FoodKey)));

		// F8. An abrupt empty: everyone fails the draw that very step, so LIFE
		// SUPPORT owns the banner and the episode latches silently. Refilling
		// restores support; nobody is evacuated.
		{
			SetRunway(30.0);
			RunSteps(1);
			const int32 Food0 = CountAlerts(FoodKey), Life0 = CountAlerts(TEXT("LIFE SUPPORT"));
			SetRunway(0.0);
			RunSteps(1);
			Expect(CountAlerts(FoodKey) == Food0 && CountAlerts(TEXT("LIFE SUPPORT")) == Life0 + 4 && Pen() == 10.0,
				FString::Printf(TEXT("F8 larder emptied in one go: food alerts %d -> %d (silent), LIFE SUPPORT +%d (expect 4), FoodPenalty %.1f"),
					Food0, CountAlerts(FoodKey), CountAlerts(TEXT("LIFE SUPPORT")) - Life0, Pen()));
			SetRunway(40.0);
			RunSteps(1);
			int32 Supported = 0;
			for (const FRHColonist& C : Sim->GetColonists()) { Supported += C.bSupported ? 1 : 0; }
			Expect(Pen() == 0.0 && Supported == 4 && Sim->GetPopulation() == 4 && CountAlerts(TEXT("EVACUATED")) == 0,
				FString::Printf(TEXT("F8 refilled: FoodPenalty %.1f, %d of 4 supported, pop %d, no evacuation"), Pen(), Supported, Sim->GetPopulation()));
		}

		// F9. The load-time latch RE-DERIVE. Each half makes the runtime latch
		// DISAGREE with the saved state before the load, so only a re-derive
		// can pass (a load that left the latch alone would fail both).
		{
			// (a) saved while latched; unlatched in memory before the load.
			SetRunway(8.0);
			RunSteps(1);
			const int32 A0 = CountAlerts(FoodKey);
			const double PenSaved = Pen();
			Expect(Sim->SaveColony(TEXT("foodtest"), Err), TEXT("F9 saved 'foodtest' inside the episode (latched)"));
			SetRunway(16.0);
			RunSteps(1); // re-arms the live latch
			const bool bLoaded = Sim->LoadColony(TEXT("foodtest"), Err);
			Expect(bLoaded && Pen() == PenSaved && Pen() > 0.0,
				FString::Printf(TEXT("F9a load: FoodPenalty %.9f bit-identical to the save (%.9f)"), Pen(), PenSaved));
			RunSteps(1);
			Expect(CountAlerts(FoodKey) == A0, FString::Printf(TEXT("F9a latch re-armed in memory, save was inside the episode: the load re-latches, %d new food alerts (expect 0)"), CountAlerts(FoodKey) - A0));

			// (b) saved unlatched at 12 sols; latched in memory before the load.
			SetRunway(16.0);
			RunSteps(1);
			SetRunway(12.0);
			RunSteps(1);
			Expect(Sim->SaveColony(TEXT("foodtest"), Err) && Pen() == 0.0, TEXT("F9 saved 'foodtest' above the floor, after a recovery (unlatched)"));
			SetRunway(8.0);
			RunSteps(1); // fires and latches the live latch
			const bool bLoaded2 = Sim->LoadColony(TEXT("foodtest"), Err);
			const int32 B0 = CountAlerts(FoodKey);
			Expect(bLoaded2 && Pen() == 0.0 && Sim->GetFoodRunwaySols() > Sim->GetFoodRunwayFloorSols(), TEXT("F9b load: back above the floor"));
			SetRunway(9.0);
			RunSteps(1);
			Expect(CountAlerts(FoodKey) == B0 + 1, FString::Printf(TEXT("F9b latch set in memory, save was above the floor: the load un-latches, so the next drop alerts (%d new, expect 1)"), CountAlerts(FoodKey) - B0));
		}

		// F10. Band parity: the same 1200 sim-seconds of falling larder at 60 s
		// and at 6 s per step from the SAME saved state (saved HopeSmoothed
		// included). The runway and the line are pure functions of a linearly
		// integrated stock, so they agree to rounding. The smoothed mood is exact
		// for constant input and O(dt) for a ramp: the line ramps at 1 point per
		// sol, so the legs agree within 0.05 point (documented tolerance). The
		// alert lands on the first step under the floor in each leg, and the two
		// land within one 60 s step of each other.
		{
			SetRunway(16.0);
			RunSteps(1); // arm
			Expect(Sim->SaveColony(TEXT("foodparity"), Err), TEXT("F10 saved the armed start state"));
			struct FLeg { double Runway = 0, Penalty = 0, Smoothed = 0, AlertAtSec = -1; int32 Alerts = 0; bool bFirstUnder = false; };
			const auto RunLeg = [&](int32 N, double Dt) -> FLeg
			{
				FLeg L;
				SetRunway(10.52); // under the floor after 11 x 60 s / 105 x 6 s
				const int32 C0 = CountAlerts(FoodKey);
				for (int32 i = 1; i <= N; ++i)
				{
					const int32 Before = CountAlerts(FoodKey);
					Step(Dt);
					if (CountAlerts(FoodKey) > Before)
					{
						L.AlertAtSec = i * Dt;
						L.bFirstUnder = Sim->GetFoodRunwaySols() < 10.0 && (Sim->GetFoodRunwaySols() + Dt / 1200.0) >= 10.0 - 1e-9;
					}
				}
				L.Alerts = CountAlerts(FoodKey) - C0;
				L.Runway = Sim->GetFoodRunwaySols();
				L.Penalty = Pen();
				L.Smoothed = Sim->GetHopeSmoothed();
				return L;
			};
			const FLeg A = RunLeg(20, 60.0);
			const bool bBack = Sim->LoadColony(TEXT("foodparity"), Err);
			const FLeg B = RunLeg(200, 6.0);
			Expect(bBack, TEXT("F10 reloaded the same start state for the 6 s leg"));
			Expect(A.Alerts == 1 && B.Alerts == 1 && A.bFirstUnder && B.bFirstUnder && Near(A.AlertAtSec, B.AlertAtSec, 60.0),
				FString::Printf(TEXT("F10 one alert per leg, on the first step under the floor: 60 s leg at %.0f s, 6 s leg at %.0f s (within one 60 s step)"), A.AlertAtSec, B.AlertAtSec));
			Expect(Near(A.Runway, B.Runway, 1e-9) && Near(A.Penalty, B.Penalty, 1e-9) && Near(A.Runway, 9.52, 1e-6),
				FString::Printf(TEXT("F10 runway %.9f vs %.9f (expect 9.52), FoodPenalty %.9f vs %.9f"), A.Runway, B.Runway, A.Penalty, B.Penalty));
			Expect(Near(A.Smoothed, B.Smoothed, 0.05),
				FString::Printf(TEXT("F10 HopeSmoothed %.5f (60 s) vs %.5f (6 s): differ by %.5f (tolerance 0.05)"), A.Smoothed, B.Smoothed, FMath::Abs(A.Smoothed - B.Smoothed)));
		}

		Sim->OnAlert.Remove(AlertH);
		UE_LOG(LogRedHopeSim, Display, TEXT("=== FOOD RUNWAY TEST END: %d failure(s) ==="), Fails);
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return Fails > 0 ? 1 : 0;
	}

	// D1a self-test (2026-10-17): Q3 is live at the resized bills and the
	// ladder REPEATS its last rung when that ship lands, every bill x1.25 per
	// round (award and deadline unscaled), saved as the v32 round counter. The
	// era-mode refusal no longer locks a late colony out. Fails loudly: every
	// check logs at Error. `-resupply`.
	if (FParse::Param(*Params, TEXT("resupply")))
	{
		UE_LOG(LogRedHopeSim, Display, TEXT("=== RESUPPLY TEST (D1a, 2026-10-17) ==="));
		int32 Fails = 0;
		const auto Expect = [&](bool bOk, const FString& What)
		{
			if (bOk) { UE_LOG(LogRedHopeSim, Display, TEXT("RESUPPLY ok: %s"), *What); }
			else { UE_LOG(LogRedHopeSim, Error, TEXT("RESUPPLY FAIL: %s"), *What); ++Fails; }
		};
		const auto Near = [](double A, double B, double Tol) { return FMath::Abs(A - B) <= Tol; };
		const auto Step = [&]()
		{
			Clock->Debug_AdvanceSimSeconds(URHSimClockSubsystem::EraStepSimSeconds);
			Sim->EraStep(URHSimClockSubsystem::EraStepSimSeconds);
		};
		const int32 StepsPerSolR = (int32)(URHSimClockSubsystem::SolLengthSimSeconds / URHSimClockSubsystem::EraStepSimSeconds);
		const auto RunSolsR = [&](double NSol) { for (int32 i = 0; i < (int32)(NSol * StepsPerSolR); ++i) { Step(); } };
		// Run to an absolute sol (fractions allowed), bounded.
		const auto RunToSol = [&](double TargetSol)
		{
			for (int32 i = 0; i < 100 * StepsPerSolR && Clock->GetSimSecondsTotal() < TargetSol * URHSimClockSubsystem::SolLengthSimSeconds; ++i) { Step(); }
		};

		TArray<FString> Alerts;
		const FDelegateHandle AlertH = Sim->OnAlert.AddLambda([&Alerts](const FString& S) { Alerts.Add(S); });
		const auto LastAlertWith = [&Alerts](const TCHAR* Needle)
		{
			for (int32 i = Alerts.Num() - 1; i >= 0; --i) { if (Alerts[i].Contains(Needle)) { return Alerts[i]; } }
			return FString();
		};
		// Landing sol captured AT the landing (not read after a run), and the
		// award + requisition multiplier captured AT the met broadcast.
		int32 Landings = 0, LandSol = -1;
		double MetAward = 0.0, MetMult = 1.0;
		const FDelegateHandle ShipH = Sim->OnShipArrived.AddLambda([&](const TArray<FName>&) { ++Landings; LandSol = Clock->GetSol(); });
		const FDelegateHandle MetH = Sim->OnQuotaMet.AddLambda([&](int32, double AwardKg) { MetAward = AwardKg; MetMult = Sim->GetRequisitionMultiplier(); });

		const auto Have = [&](const TCHAR* Res)
		{
			const auto Prog = Sim->GetQuotaProgress();
			const TPair<double, double>* P = Prog.Find(FName(Res));
			return P ? P->Key : -1.0;
		};
		const auto Need = [&](const TCHAR* Res)
		{
			const auto Prog = Sim->GetQuotaProgress();
			const TPair<double, double>* P = Prog.Find(FName(Res));
			return P ? P->Value : -1.0;
		};
		// The active quota asks exactly Struct S, Oxygen O, Hydrogen H - and nothing else.
		const auto BillsAre = [&](double S, double O, double H)
		{
			return Sim->GetQuotaProgress().Num() == 3 && Near(Need(TEXT("Struct")), S, 1e-9)
				&& Near(Need(TEXT("Oxygen")), O, 1e-9) && Near(Need(TEXT("Hydrogen")), H, 1e-9);
		};
		const auto BillsText = [&]()
		{
			FString T;
			for (const auto& L : Sim->GetQuotaProgress()) { T += FString::Printf(TEXT("%s %.0f "), *L.Key.ToString(), L.Value.Value); }
			return T.TrimEnd();
		};
		// Holdings are thresholds, never deducted: bring every line to its bill + Margin.
		const auto TopUp = [&](double Margin)
		{
			for (const auto& L : Sim->GetQuotaProgress())
			{
				const double Gap = L.Value.Value + Margin - L.Value.Key;
				if (Gap > 0.0) { Sim->AddStock(L.Key, Gap); }
			}
		};
		const auto SetHold = [&](const TCHAR* Res, double Target) { Sim->AddStock(FName(Res), Target - Have(Res)); };
		const auto LaunchAndLand = [&]() -> bool
		{
			FString LErr;
			const int32 Before = Landings;
			const bool bLaunched = Sim->LaunchShip(LErr);
			for (int32 i = 0; i < 20 * StepsPerSolR && Landings == Before; ++i) { Step(); }
			return bLaunched && Landings == Before + 1;
		};
		const auto QuotaAlertsSince = [&Alerts](int32 From)
		{
			int32 N = 0;
			for (int32 i = From; i < Alerts.Num(); ++i)
			{
				N += (Alerts[i].Contains(TEXT("quota"), ESearchCase::IgnoreCase) || Alerts[i].Contains(TEXT("round"), ESearchCase::IgnoreCase)
					|| Alerts[i].Contains(TEXT("renewed"), ESearchCase::IgnoreCase)) ? 1 : 0;
			}
			return N;
		};
		URHDefinitionsSubsystem* DefsSub = World->GetSubsystem<URHDefinitionsSubsystem>();
		FString Err;

		// R0. Pure data, against the LIVE synced DT rows (no Debug_ActivateQuota):
		// a stale DT_Quotas / DT_Config fails loudly here, then stops the suite.
		{
			const FRHQuotaRow* Q2 = DefsSub->GetQuota(FName("Q2"));
			const FRHQuotaRow* Q3 = DefsSub->GetQuota(FName("Q3"));
			const bool bQ3 = Q3 != nullptr;
			Expect(bQ3, TEXT("R0 DT_Quotas has a Q3 row"));
			Expect(bQ3 && Q3->SliceActive, TEXT("R0 Q3 is SliceActive in the live DT (DT/CSV)"));
			Expect(bQ3 && Q3->Requirements == TEXT("Struct:2500;Oxygen:1200;Hydrogen:250"),
				FString::Printf(TEXT("R0 Q3 Requirements \"%s\" (expect Struct:2500;Oxygen:1200;Hydrogen:250, no water line)"), bQ3 ? *Q3->Requirements : TEXT("<none>")));
			Expect(bQ3 && Q3->DeadlineSol == 24 && Near(Q3->OnTimeAward_kg, 14000.0, 1e-9) && Near(Q3->LateAward_kg, 9000.0, 1e-9),
				FString::Printf(TEXT("R0 Q3 deadline %d / on-time %.0f / late %.0f (expect 24 / 14000 / 9000)"),
					bQ3 ? Q3->DeadlineSol : -1, bQ3 ? Q3->OnTimeAward_kg : -1.0, bQ3 ? Q3->LateAward_kg : -1.0));
			Expect(Q2 && Q2->SliceActive && bQ3 && Q2->DeadlineSol < Q3->DeadlineSol,
				FString::Printf(TEXT("R0 Q2 is active and its deadline %d sits below Q3's (the (DeadlineSol, name) order Q1 < Q2 < Q3 holds)"), Q2 ? Q2->DeadlineSol : -1));
			const double Mul = DefsSub->GetConfigScalar(FName("QuotaRepeatBillMul"), -1.0);
			Expect(Near(Mul, 1.25, 1e-9), FString::Printf(TEXT("R0 RH_Config QuotaRepeatBillMul %.4f (expect 1.25; -1 = DT_Config never synced)"), Mul));
			if (!bQ3 || !Q3->SliceActive || Q3->Requirements != TEXT("Struct:2500;Oxygen:1200;Hydrogen:250") || !Near(Mul, 1.25, 1e-9) || Fails > 0)
			{
				Sim->OnAlert.Remove(AlertH);
				Sim->OnShipArrived.Remove(ShipH);
				Sim->OnQuotaMet.Remove(MetH);
				UE_LOG(LogRedHopeSim, Display, TEXT("=== RESUPPLY TEST END: %d failure(s) ==="), Fails);
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
				return 1;
			}
		}

		// R1. A fresh colony: Q1, round 0, bills unscaled, Open.
		Expect(Sim->GetActiveQuotaName() == FName("Q1") && Sim->GetQuotaRepeatRound() == 0 && Sim->GetQuotaBillScale() == 1.0
			&& Sim->GetQuotaPhase() == ERHQuotaPhase::Open && Sim->GetQuotaOpenedSol() == 0,
			FString::Printf(TEXT("R1 fresh colony: active %s, round %d, scale %.4f, phase %d, opened sol %d (expect Q1, 0, 1.0, Open, 0)"),
				*Sim->GetActiveQuotaName().ToString(), Sim->GetQuotaRepeatRound(), Sim->GetQuotaBillScale(), (int32)Sim->GetQuotaPhase(), Sim->GetQuotaOpenedSol()));

		// R2. Q1 met on time -> its ship lands -> Q2 opens at the landing sol.
		TopUp(1.0);
		RunSolsR(0.2);
		Expect(Sim->GetQuotaPhase() == ERHQuotaPhase::AwaitingManifest && Near(MetAward, 10000.0 * MetMult, 0.5),
			FString::Printf(TEXT("R2 Q1 met: phase %d, award %.1f (expect AwaitingManifest, 10000 x %.4f requisition)"), (int32)Sim->GetQuotaPhase(), MetAward, MetMult));
		Expect(LaunchAndLand(), TEXT("R2 Q1 ship launched and landed"));
		Expect(Sim->GetActiveQuotaName() == FName("Q2") && Sim->GetQuotaPhase() == ERHQuotaPhase::Open && Sim->GetQuotaRepeatRound() == 0 && Sim->GetQuotaOpenedSol() == LandSol,
			FString::Printf(TEXT("R2 after landing: active %s, phase %d, round %d, opened sol %d vs landing sol %d (expect Q2, Open, 0, equal)"),
				*Sim->GetActiveQuotaName().ToString(), (int32)Sim->GetQuotaPhase(), Sim->GetQuotaRepeatRound(), Sim->GetQuotaOpenedSol(), LandSol));

		// R3. Q2 met -> its ship lands -> Q3 opens (the live ladder reaches it).
		TopUp(1.0);
		RunSolsR(0.2);
		Expect(Sim->GetQuotaPhase() == ERHQuotaPhase::AwaitingManifest && Near(MetAward, 12000.0 * MetMult, 0.5),
			FString::Printf(TEXT("R3 Q2 met: phase %d, award %.1f (expect AwaitingManifest, 12000 x %.4f)"), (int32)Sim->GetQuotaPhase(), MetAward, MetMult));
		Expect(LaunchAndLand(), TEXT("R3 Q2 ship launched and landed"));
		Expect(Sim->GetActiveQuotaName() == FName("Q3") && Sim->GetQuotaPhase() == ERHQuotaPhase::Open && Sim->GetQuotaRepeatRound() == 0 && Sim->GetQuotaOpenedSol() == LandSol,
			FString::Printf(TEXT("R3 after landing: active %s, phase %d, round %d, opened sol %d vs landing sol %d (expect Q3, Open, 0, equal)"),
				*Sim->GetActiveQuotaName().ToString(), (int32)Sim->GetQuotaPhase(), Sim->GetQuotaRepeatRound(), Sim->GetQuotaOpenedSol(), LandSol));
		Expect(BillsAre(2500.0, 1200.0, 250.0) && Sim->GetQuotaProgress().Find(FName("Water")) == nullptr,
			FString::Printf(TEXT("R3 Q3 bills: %s (expect Struct 2500 Oxygen 1200 Hydrogen 250, three lines, no Water)"), *BillsText()));
		Expect(LastAlertWith(TEXT("new quota Q3")).Contains(TEXT("structural 2500 kg")) && LastAlertWith(TEXT("new quota Q3")).Contains(TEXT("Hydrogen 250 kg")),
			FString::Printf(TEXT("R3 opening alert: \"%s\""), *LastAlertWith(TEXT("new quota Q3"))));

		// R4. Q3 met at the authored bills -> unscaled on-time award -> its ship
		// lands -> Q3 REOPENS as round 1 with the bills x1.25.
		TopUp(1.0);
		RunSolsR(0.2);
		Expect(Sim->GetQuotaPhase() == ERHQuotaPhase::AwaitingManifest && Near(MetAward, 14000.0 * MetMult, 0.5),
			FString::Printf(TEXT("R4 Q3 met at the new bills: phase %d, award %.1f (expect AwaitingManifest, 14000 x %.4f)"), (int32)Sim->GetQuotaPhase(), MetAward, MetMult));
		Expect(LaunchAndLand(), TEXT("R4 Q3 ship launched and landed"));
		Expect(Sim->GetActiveQuotaName() == FName("Q3") && Sim->GetQuotaRepeatRound() == 1 && Sim->GetQuotaPhase() == ERHQuotaPhase::Open
			&& Sim->GetAwardMassKg() == 0.0 && Sim->GetManifestItems().Num() == 0 && Sim->GetQuotaOpenedSol() == LandSol,
			FString::Printf(TEXT("R4 ladder exhausted -> repeat: active %s, round %d, phase %d, award %.0f, manifest %d, opened %d vs landing %d (expect Q3, 1, Open, 0, 0, equal)"),
				*Sim->GetActiveQuotaName().ToString(), Sim->GetQuotaRepeatRound(), (int32)Sim->GetQuotaPhase(), Sim->GetAwardMassKg(), Sim->GetManifestItems().Num(), Sim->GetQuotaOpenedSol(), LandSol));
		Expect(BillsAre(3125.0, 1500.0, 313.0) && Near(Sim->GetQuotaBillScale(), 1.25, 1e-12),
			FString::Printf(TEXT("R4 round-1 bills: %s, scale %.6f (expect 3125 / 1500 / 313, 1.25)"), *BillsText(), Sim->GetQuotaBillScale()));
		Expect(LastAlertWith(TEXT("renewed (round 1)")).Contains(TEXT("structural 3125 kg")) && LastAlertWith(TEXT("renewed (round 1)")).Contains(TEXT("quota Q3")),
			FString::Printf(TEXT("R4 renewal alert: \"%s\""), *LastAlertWith(TEXT("renewed (round 1)"))));

		// R5. The scaling is enforced. Holding EXACTLY the round-0 bills (which
		// would already meet an unscaled Q3) leaves round 1 Open; one kg short
		// of the round-1 bills is still Open; the last kg meets it.
		SetHold(TEXT("Struct"), 2500.0); SetHold(TEXT("Oxygen"), 1200.0); SetHold(TEXT("Hydrogen"), 250.0);
		Expect(Near(Have(TEXT("Struct")), 2500.0, 1e-6) && Near(Have(TEXT("Oxygen")), 1200.0, 1e-6) && Near(Have(TEXT("Hydrogen")), 250.0, 1e-6),
			FString::Printf(TEXT("R5 setup holds exactly the round-0 bills: %.3f / %.3f / %.3f"), Have(TEXT("Struct")), Have(TEXT("Oxygen")), Have(TEXT("Hydrogen"))));
		RunSolsR(0.2);
		Expect(Sim->GetQuotaPhase() == ERHQuotaPhase::Open,
			FString::Printf(TEXT("R5 holding exactly the round-0 bills: round 1 stays Open (phase %d)"), (int32)Sim->GetQuotaPhase()));
		SetHold(TEXT("Struct"), 3124.0); SetHold(TEXT("Oxygen"), 1500.0); SetHold(TEXT("Hydrogen"), 313.0);
		RunSolsR(0.2);
		Expect(Sim->GetQuotaPhase() == ERHQuotaPhase::Open && Near(Have(TEXT("Struct")), 3124.0, 1e-6),
			FString::Printf(TEXT("R5 Struct 1 kg short of 3125 (O2 and H2 met): Open (phase %d, struct %.3f)"), (int32)Sim->GetQuotaPhase(), Have(TEXT("Struct"))));
		SetHold(TEXT("Struct"), 3125.0);
		RunSolsR(0.2);
		Expect(Sim->GetQuotaPhase() == ERHQuotaPhase::AwaitingManifest,
			FString::Printf(TEXT("R5 Struct 3125: met (phase %d)"), (int32)Sim->GetQuotaPhase()));

		// R6. Round 2 and the era-mode refusal. Holdings stay at the round-1
		// bills, so round 2 (3906 / 1875 / 391) cannot be met while we wait.
		Expect(LaunchAndLand(), TEXT("R6 round-1 ship launched and landed"));
		const int32 Opened2 = Sim->GetQuotaOpenedSol();
		Expect(Sim->GetQuotaRepeatRound() == 2 && BillsAre(3906.0, 1875.0, 391.0) && Opened2 == LandSol,
			FString::Printf(TEXT("R6 round 2: round %d, bills %s, opened %d vs landing %d (expect 2, 3906 / 1875 / 391, equal)"),
				Sim->GetQuotaRepeatRound(), *BillsText(), Opened2, LandSol));
		RunToSol((double)Opened2 + 24.5);
		{
			FString Why;
			const bool bCan = Sim->CanEnterEraMode(Why);
			Expect(Sim->GetQuotaPhase() == ERHQuotaPhase::Open && !bCan && Why == TEXT("quota deadline within one sol"),
				FString::Printf(TEXT("R6 mid deadline sol (sol %d, due %d): Can=%d, reason \"%s\" (expect refused, exactly the quota reason)"),
					Clock->GetSol(), Opened2 + 24, bCan ? 1 : 0, *Why));
		}
		RunToSol((double)Opened2 + 25.5);
		{
			FString Why;
			const bool bCan = Sim->CanEnterEraMode(Why);
			Expect(Sim->GetQuotaPhase() == ERHQuotaPhase::Open && bCan && Why.IsEmpty(),
				FString::Printf(TEXT("R6 after the deadline sol (sol %d), still Open and unmet: Can=%d, reason \"%s\" (expect allowed, no refusal at all)"),
					Clock->GetSol(), bCan ? 1 : 0, *Why));
		}

		// R7. Met late: the late award, unscaled.
		TopUp(1.0);
		RunSolsR(0.2);
		Expect(Sim->GetQuotaPhase() == ERHQuotaPhase::AwaitingManifest && Clock->GetSol() - Opened2 > 24 && Near(MetAward, 9000.0 * MetMult, 0.5),
			FString::Printf(TEXT("R7 round 2 met %d sols after opening: award %.1f (expect late tier 9000 x %.4f, unscaled)"), Clock->GetSol() - Opened2, MetAward, MetMult));

		// R8. Save v32 mid-round (round 2, AwaitingManifest).
		const int32 SnapRound = Sim->GetQuotaRepeatRound(), SnapOpened = Sim->GetQuotaOpenedSol();
		const FName SnapActive = Sim->GetActiveQuotaName();
		const double SnapAward = Sim->GetAwardMassKg();
		Expect(Sim->SaveColony(TEXT("resupplytest"), Err),
			FString::Printf(TEXT("R8 saved v32 at round %d, %s, opened %d, award %.1f (%s)"), SnapRound, *SnapActive.ToString(), SnapOpened, SnapAward, *Err));

		// R9. Knob 0 is the legacy rest state: the ship lands, Completed stands,
		// the round stays, and no quota/renewal alert is raised.
		Sim->Debug_SetQuotaRepeatBillMul(0.0);
		const int32 AlertMark9 = Alerts.Num();
		Expect(LaunchAndLand(), TEXT("R9 round-2 ship launched and landed with the knob at 0"));
		Expect(Sim->GetQuotaPhase() == ERHQuotaPhase::Completed && Sim->GetActiveQuotaName() == FName("Q3") && Sim->GetQuotaRepeatRound() == 2
			&& Sim->GetQuotaBillScale() == 1.0 && QuotaAlertsSince(AlertMark9) == 0,
			FString::Printf(TEXT("R9 knob 0: phase %d, active %s, round %d, scale %.4f, %d quota/round alerts since launch (expect Completed, Q3, 2, 1.0, 0)"),
				(int32)Sim->GetQuotaPhase(), *Sim->GetActiveQuotaName().ToString(), Sim->GetQuotaRepeatRound(), Sim->GetQuotaBillScale(), QuotaAlertsSince(AlertMark9)));
		RunSolsR(0.5);
		Expect(Sim->GetQuotaPhase() == ERHQuotaPhase::Completed && Sim->GetQuotaRepeatRound() == 2 && QuotaAlertsSince(AlertMark9) == 0,
			FString::Printf(TEXT("R9 half a sol later: still Completed (phase %d), round %d, %d quota/round alerts"), (int32)Sim->GetQuotaPhase(), Sim->GetQuotaRepeatRound(), QuotaAlertsSince(AlertMark9)));

		// R10. The Completed re-check: switching the knob on reopens the ladder
		// on the very next step as round 3.
		Sim->Debug_SetQuotaRepeatBillMul(1.25);
		Step();
		Expect(Sim->GetQuotaPhase() == ERHQuotaPhase::Open && Sim->GetQuotaRepeatRound() == 3 && Sim->GetQuotaOpenedSol() == Clock->GetSol() && BillsAre(4883.0, 2344.0, 488.0),
			FString::Printf(TEXT("R10 knob back on: phase %d, round %d, opened %d vs sol %d, bills %s (expect Open, 3, equal, 4883 / 2344 / 488)"),
				(int32)Sim->GetQuotaPhase(), Sim->GetQuotaRepeatRound(), Sim->GetQuotaOpenedSol(), Clock->GetSol(), *BillsText()));
		Expect(LastAlertWith(TEXT("renewed (round 3)")).Contains(TEXT("structural 4883 kg")),
			FString::Printf(TEXT("R10 renewal alert: \"%s\""), *LastAlertWith(TEXT("renewed (round 3)"))));

		// R11. Load restores the mutated state: round 3 -> the saved round 2.
		const bool bLoaded = Sim->LoadColony(TEXT("resupplytest"), Err);
		Expect(bLoaded && Sim->GetQuotaRepeatRound() == SnapRound && Sim->GetActiveQuotaName() == SnapActive && Sim->GetQuotaPhase() == ERHQuotaPhase::AwaitingManifest
			&& Near(Sim->GetAwardMassKg(), SnapAward, 1e-6) && Sim->GetQuotaOpenedSol() == SnapOpened && BillsAre(3906.0, 1875.0, 391.0),
			FString::Printf(TEXT("R11 load v32 (%d): round %d, active %s, phase %d, award %.1f, opened %d, bills %s (expect 2, Q3, AwaitingManifest, %.1f, %d, 3906 / 1875 / 391) %s"),
				bLoaded ? 1 : 0, Sim->GetQuotaRepeatRound(), *Sim->GetActiveQuotaName().ToString(), (int32)Sim->GetQuotaPhase(), Sim->GetAwardMassKg(), Sim->GetQuotaOpenedSol(), *BillsText(), SnapAward, SnapOpened, *Err));

		// R12. The strict rule stands: the same file stamped as v31 is refused
		// loudly and the live colony is untouched.
		{
			const FString SlotPath = FPaths::ProjectSavedDir() / TEXT("SaveGames") / TEXT("RH_resupplytest.sav");
			const FString OldPath = FPaths::ProjectSavedDir() / TEXT("SaveGames") / TEXT("RH_resupplytest31.sav");
			TArray<uint8> Bytes;
			const bool bRead = FFileHelper::LoadFileToArray(Bytes, *SlotPath) && Bytes.Num() > 8;
			bool bRefused = false;
			if (bRead)
			{
				Bytes[4] = 31; Bytes[5] = 0; Bytes[6] = 0; Bytes[7] = 0; // header: magic u32, then version u32
				FFileHelper::SaveArrayToFile(Bytes, *OldPath);
				FString OldErr;
				bRefused = !Sim->LoadColony(TEXT("resupplytest31"), OldErr) && OldErr.Contains(TEXT("Save format mismatch"));
				IFileManager::Get().Delete(*OldPath);
			}
			Expect(bRead && bRefused && Sim->GetQuotaRepeatRound() == SnapRound && Sim->GetQuotaPhase() == ERHQuotaPhase::AwaitingManifest,
				FString::Printf(TEXT("R12 a v31-stamped save is refused (read %d, refused %d); live round still %d, phase %d"),
					bRead ? 1 : 0, bRefused ? 1 : 0, Sim->GetQuotaRepeatRound(), (int32)Sim->GetQuotaPhase()));
		}

		Sim->OnAlert.Remove(AlertH);
		Sim->OnShipArrived.Remove(ShipH);
		Sim->OnQuotaMet.Remove(MetH);
		UE_LOG(LogRedHopeSim, Display, TEXT("=== RESUPPLY TEST END: %d failure(s) ==="), Fails);
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return Fails > 0 ? 1 : 0;
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
