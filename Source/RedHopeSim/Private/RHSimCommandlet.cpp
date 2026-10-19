#include "RHSimCommandlet.h"
#include "RedHopeSim.h"
#include "RHSimClockSubsystem.h"
#include "RHSimWorldSubsystem.h"
#include "RHDefinitionsSubsystem.h"
#include "RHAgentSubsystem.h"
#include "RHRobotMovement.h"
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

		// 6) Starve the pool: everyone goes on short rations at once, and then the
		// colony shrinks GRADUALLY (C2a, shared rationing). With the ration at 0 a
		// home accumulates the residents it had when the shortage began x 1 /
		// EvacSols (2) per sol, and each whole 1 recalls one resident, newest first:
		// four residents leave at 4 / 2 = 2 a sol, one every half sol, at about
		// 0.55, 1.05, 1.55 and 2.0 sols (the clock ticks in 60 s steps and 0.1 added
		// ten times falls a hair short of 1, so the first lands one step after the
		// half sol; the clock carries the remainder, so the rest keep the half-sol
		// beat). The home is empty in EvacSols. (It was all four together at 2.0.)
		Sim->AddStock(FName("Food"), -Sim->GetStock(FName("Food")));
		RunSols(0.4);
		UE_LOG(LogRedHopeSim, Display, TEXT("CREW starved 0.4 sol: pop=%d unsupported=%d (expect 4, 4 - the first recall falls at 0.5 sol)"),
			Sim->GetPopulation(),
			[&]{ int32 N=0; for (const FRHColonist& C : Sim->GetColonists()) { N += C.bSupported ? 0 : 1; } return N; }());
		RunSols(0.4);
		UE_LOG(LogRedHopeSim, Display, TEXT("CREW starved 0.8 sol: pop=%d (expect 3 - the newest recalled just after 0.5 sol)"), Sim->GetPopulation());
		RunSols(0.5);
		UE_LOG(LogRedHopeSim, Display, TEXT("CREW starved 1.3 sols: pop=%d (expect 2 - one more at 1.0 sol: a steady one every half sol)"), Sim->GetPopulation());
		RunSols(0.5);
		UE_LOG(LogRedHopeSim, Display, TEXT("CREW starved 1.8 sols: pop=%d (expect 1)"), Sim->GetPopulation());
		RunSols(0.7);
		UE_LOG(LogRedHopeSim, Display, TEXT("CREW starved 2.5 sols: pop=%d (expect 0 - the home emptied in its 2 sols)"), Sim->GetPopulation());

		UE_LOG(LogRedHopeSim, Display, TEXT("=== CREW TEST END ==="));
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return 0;
	}

	// M2 Gate B self-test: room designations on carved cells, the adjacency
	// calculus (emit/refuse, hallway+filtration cure), jobs, and the Hope
	// index arithmetic - exact doubles. Save v11 round-trip. `-rooms`.
	// D3a (2026-10-17): fails loudly, and it is the PIN for the Hope weights. C2a
	// (2026-10-18) re-derived it for per-head Hope: the pins are HopeHousingMax
	// 7.5, HopePerMoralePoint 2.5 and the C2a rows (HopeJobsMax 7.5,
	// HopeUnsupportedMax 30, HopeUnsupportedSteepness 3, HopeAdjacencyMax 16,
	// RoomServesResidents 25, ComfortRefPop 8) - the literals below are the
	// rulings' numbers, read from the DT row itself.
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
		int32 Fails = 0;
		const auto Expect = [&](bool bOk, const FString& What)
		{
			if (bOk) { UE_LOG(LogRedHopeSim, Display, TEXT("ROOMS ok: %s"), *What); }
			else { UE_LOG(LogRedHopeSim, Error, TEXT("ROOMS FAIL: %s"), *What); ++Fails; }
		};
		const auto Near = [](double A, double B, double Tol) { return FMath::Abs(A - B) <= Tol; };
		URHDefinitionsSubsystem* DefsSub = World->GetSubsystem<URHDefinitionsSubsystem>();

		// R0. Pure data: DT_Rooms is synced (Gate B) - these six functions ship
		// SliceActive=true - and the Hope weights ride the synced DT_Config.
		// Both are asserted against the REAL assets, so a DT that regresses vs the
		// CSV fails loudly instead of a silent in-memory patch masking the drift.
		// The weights are read from the table row alone (no -RHConfig override, no
		// C++ fallback): -1 fails a missing row, a pre-D3a 15 / 5 fails, and a DT
		// that predates C2a has no HopeJobsMax at all.
		bool bData = true;
		for (const TCHAR* RowName : { TEXT("LivingQuarters"), TEXT("Lab"), TEXT("Workstation"), TEXT("Dining"), TEXT("Cooking"), TEXT("Hallway") })
		{
			const FRHRoomRow* Row = DefsSub ? DefsSub->GetRoom(FName(RowName)) : nullptr;
			const bool bRow = Row && Row->SliceActive;
			Expect(bRow, FString::Printf(TEXT("R0 DT_Rooms row '%s' present and SliceActive (DT/CSV)"), RowName));
			bData &= bRow;
		}
		const double DtHousing = DefsSub->GetConfigTableScalar(FName("HopeHousingMax"), -1.0);
		const double DtMorale = DefsSub->GetConfigTableScalar(FName("HopePerMoralePoint"), -1.0);
		const double DtJob = DefsSub->GetConfigTableScalar(FName("HopeJobsMax"), -1.0);
		const double DtUnsupMax = DefsSub->GetConfigTableScalar(FName("HopeUnsupportedMax"), -1.0);
		const double DtSteep = DefsSub->GetConfigTableScalar(FName("HopeUnsupportedSteepness"), -1.0);
		const double DtAdjMax = DefsSub->GetConfigTableScalar(FName("HopeAdjacencyMax"), -1.0);
		const double DtServes = DefsSub->GetConfigTableScalar(FName("RoomServesResidents"), -1.0);
		const double DtRefPop = DefsSub->GetConfigTableScalar(FName("ComfortRefPop"), -1.0);
		// A stale DT_Config fails here but does NOT abort: the rest of the suite
		// then shows every number the old weights moved, not only the first.
		Expect(Near(DtHousing, 7.5, 1e-9) && Near(DtMorale, 2.5, 1e-9) && Near(DtJob, 7.5, 1e-9),
			FString::Printf(TEXT("R0 DT_Config HopeHousingMax 7.5 / HopePerMoralePoint 2.5 / HopeJobsMax 7.5, the table row says %.3f / %.3f / %.3f (D3a + C2a, DT/CSV)"),
				DtHousing, DtMorale, DtJob));
		Expect(Near(DtUnsupMax, 30.0, 1e-9) && Near(DtSteep, 3.0, 1e-9) && Near(DtAdjMax, 16.0, 1e-9) && Near(DtServes, 25.0, 1e-9) && Near(DtRefPop, 8.0, 1e-9),
			FString::Printf(TEXT("R0 DT_Config HopeUnsupportedMax 30 / steepness 3 / HopeAdjacencyMax 16 / RoomServesResidents 25 / ComfortRefPop 8, the table row says %.3f / %.3f / %.3f / %.3f / %.3f (C2a, DT/CSV)"),
				DtUnsupMax, DtSteep, DtAdjMax, DtServes, DtRefPop));
		Expect(Near(Sim->GetHopeHousingMax(), 7.5, 1e-9) && Near(Sim->GetHopePerMoralePoint(), 2.5, 1e-9) && Near(Sim->GetHopeJobsMax(), 7.5, 1e-9)
			&& Near(Sim->GetHopeUnsupportedMax(), 30.0, 1e-9) && Near(Sim->GetHopeUnsupportedSteepness(), 3.0, 1e-9) && Near(Sim->GetHopeAdjacencyMax(), 16.0, 1e-9)
			&& Near(Sim->GetRoomServesResidents(), 25.0, 1e-9) && Near(Sim->GetComfortRefPop(), 8.0, 1e-9),
			FString::Printf(TEXT("R0 the sim reads %.3f / %.3f / %.3f / %.3f / %.3f / %.3f / %.3f / %.3f (an override or a fallback would show here)"),
				Sim->GetHopeHousingMax(), Sim->GetHopePerMoralePoint(), Sim->GetHopeJobsMax(), Sim->GetHopeUnsupportedMax(), Sim->GetHopeUnsupportedSteepness(),
				Sim->GetHopeAdjacencyMax(), Sim->GetRoomServesResidents(), Sim->GetComfortRefPop()));
		if (!bData)
		{
			UE_LOG(LogRedHopeSim, Display, TEXT("=== ROOMS TEST END: %d failure(s) ==="), Fails);
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
			return 1;
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

		// R1. Baseline: no designations. 50 base + 5 vault milestone.
		auto H = Sim->GetColonyHope();
		Expect(Near(H.Total, 55.0, 1e-9) && Sim->IsFloorRated(-1) && Sim->GetPopulation() == 4,
			FString::Printf(TEXT("R1 baseline: hope=%.2f rated=%d pop=%d (expect 55.00, 1, 4)"), H.Total, (int32)Sim->IsFloorRated(-1), Sim->GetPopulation()));

		// R2. Refusals: a still-dormant function (Smoking - Gate D, SliceActive
		// FALSE in the synced DT) and an uncarved cell index. Both refused.
		const bool bDormant = Sim->DesignateRoom(-1, 0, FName("Smoking"), R);
		const bool bFar = Sim->DesignateRoom(-1, 9, FName("Dining"), R);
		Expect(!bDormant && !bFar, FString::Printf(TEXT("R2 refusals: dormant=%d cell9=%d (expect 0, 0)"), (int32)bDormant, (int32)bFar));

		// R3. A bad kitchen: Cooking@0 (1,0) beside LQ@1 (1,1), Dining@2 (0,1)
		// diagonal across the shaft corner. Both offended - dist 1 has no
		// between, dist 2's common neighbor is the LQ, not a hallway.
		// C2a: 55 + housing 7.5 x 1/4 + rooms (0.3 + 0.6) x 2.5 (one room of each
		// serves 25 residents, so coverage is 1) - adjacency 16 x 2 offended refuser
		// cells (LQ, Dining) / 2 refuser cells (every refuser cell offended = the old
		// two pairs x 8) = 55 + 1.875 + 2.25 - 16 = 43.125, as before C2a.
		Sim->DesignateRoom(-1, 1, FName("LivingQuarters"), R);
		Sim->DesignateRoom(-1, 2, FName("Dining"), R);
		Sim->DesignateRoom(-1, 0, FName("Cooking"), R);
		H = Sim->GetColonyHope();
		Expect(Near(H.Total, 43.125, 1e-6) && H.OffendedPairs == 2 && H.OffendedRefusers == 2 && H.RefuserCells == 2 && Near(H.AdjacencyPenalty, 16.0, 1e-9)
			&& Near(H.Housing, 1.875, 1e-9) && Near(H.Rooms, 2.25, 1e-6),
			FString::Printf(TEXT("R3 bad kitchen: hope=%.3f pairs=%d offended %d of %d refuser cells (-%.1f) housing=%.3f rooms=%.2f (expect 43.125, 2, 2 of 2, -16.0, 1.875, 2.25)"),
				H.Total, H.OffendedPairs, H.OffendedRefusers, H.RefuserCells, H.AdjacencyPenalty, H.Housing, H.Rooms));

		// R4. The cure: move Cooking to (-1,0) with the Hallway at (-1,1)
		// between it and Dining, circulator running - partition + filtration
		// cancels the pair (habitat vision §4). Lab@5 seats one colonist.
		// C2a: 55 + housing 1.875 + rooms (0.3 + 0.6 + 0.3) x 2.5 + jobs 7.5 x 1
		// seat / 4 people (1.875) = 61.75.
		Sim->DesignateRoom(-1, 0, NAME_None, R);
		Sim->DesignateRoom(-1, 4, FName("Cooking"), R);
		Sim->DesignateRoom(-1, 3, FName("Hallway"), R);
		Sim->DesignateRoom(-1, 5, FName("Lab"), R);
		RunSols(0.1); // jobs refresh in the population step
		H = Sim->GetColonyHope();
		Expect(Near(H.Total, 61.75, 1e-6) && H.OffendedPairs == 0 && H.OffendedRefusers == 0 && H.RefuserCells == 2 && Near(H.Jobs, 1.875, 1e-9) && H.FilledSeats == 1,
			FString::Printf(TEXT("R4 cured layout: hope=%.3f pairs=%d offended %d of %d refuser cells jobs=%.3f seats=%d (expect 61.75, 0, 0 of 2, 1.875, 1)"),
				H.Total, H.OffendedPairs, H.OffendedRefusers, H.RefuserCells, H.Jobs, H.FilledSeats));

		// R5. Greed test: swap the hallway for more quarters - the partition
		// dies and both offenses return. More beds, less Hope: a real tradeoff.
		// C2a: refuser cells are now LQ@1, Dining@2 and the new LQ@3; Cooking@4
		// offends LQ@3 (dist 1) and Dining@2 (dist 2, no partition) but not LQ@1
		// (dist 3): 2 of 3 cells = 16 x 2/3 = 10.667. 55 + housing 7.5 x 2/4 (3.75) +
		// rooms 3.0 + jobs 1.875 - 10.667 = 52.958333.
		Sim->DesignateRoom(-1, 3, FName("LivingQuarters"), R);
		H = Sim->GetColonyHope();
		Expect(Near(H.Total, 52.958333, 1e-6) && H.OffendedPairs == 2 && H.OffendedRefusers == 2 && H.RefuserCells == 3 && Near(H.AdjacencyPenalty, 32.0 / 3.0, 1e-9) && Near(H.Housing, 3.75, 1e-9),
			FString::Printf(TEXT("R5 greed swap: hope=%.3f pairs=%d offended %d of %d refuser cells (-%.3f) housing=%.3f (expect 52.958, 2, 2 of 3, -10.667, 3.75)"),
				H.Total, H.OffendedPairs, H.OffendedRefusers, H.RefuserCells, H.AdjacencyPenalty, H.Housing));

		// R6. The Designate verb rides the uplink like any order.
		FRHCommand Cmd;
		Cmd.Verb = FName("Designate");
		Cmd.Target = FName("Workstation");
		Cmd.Level = -1;
		Cmd.Value = 0;
		Sim->EnqueueCommand(Cmd);
		RunSols(0.1);
		Expect(Sim->GetRoomAt(-1, 0) == FName("Workstation"),
			FString::Printf(TEXT("R6 uplink designate: cell0=%s (expect Workstation)"), *Sim->GetRoomAt(-1, 0).ToString()));

		// R7. Save round-trip: designations + jobs + the exact Hope total
		// (C2a: 52.958333 + Workstation 0.2 x 2.5 + a second seat, jobs 7.5 x 2/4 = 3.75
		// instead of 1.875, = 52.958333 + 0.5 + 1.875 = 55.333333).
		const double HopeBefore = Sim->GetColonyHope().Total;
		FString Err;
		const bool bSaved = Sim->SaveColony(TEXT("roomstest"), Err);
		const bool bLoaded = Sim->LoadColony(TEXT("roomstest"), Err);
		H = Sim->GetColonyHope();
		Expect(bSaved && bLoaded && Sim->GetRoomAt(-1, 4) == FName("Cooking") && Sim->GetRoomAt(-1, 3) == FName("LivingQuarters")
			&& Near(HopeBefore, 55.333333, 1e-6) && Near(H.Total, HopeBefore, 1e-9) && H.FilledSeats == 2,
			FString::Printf(TEXT("R7 save/load v34: cell4=%s cell3=%s hope=%.3f (expect Cooking, LivingQuarters, 55.333 = the pre-save %.3f) seats=%d (expect 2)"),
				*Sim->GetRoomAt(-1, 4).ToString(), *Sim->GetRoomAt(-1, 3).ToString(), H.Total, HopeBefore, H.FilledSeats));

		UE_LOG(LogRedHopeSim, Display, TEXT("=== ROOMS TEST END: %d failure(s) ==="), Fails);
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return Fails > 0 ? 1 : 0;
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

	// M2 Gate D+ self-test: the GENERATIONAL CARROT - a thriving colony grows,
	// the food-buffer + housing gates, the first-Martian-born milestone + its
	// Hope surge, streak reset on lapse, save v15. `-growth`.
	// D3a (2026-10-17): fails loudly. The recipe is the SHIPPED tuning's proof
	// that a sensible layout (quarters, a T2 lab beside a plain lab, a dining
	// room, a comforts crate) sits in THRIVING and reaches the 75 growth gate;
	// with halved weights the old two-Lab recipe tops out at 66.25 and the
	// births quietly stop.
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
		int32 Fails = 0;
		const auto Expect = [&](bool bOk, const FString& What)
		{
			if (bOk) { UE_LOG(LogRedHopeSim, Display, TEXT("GROWTH ok: %s"), *What); }
			else { UE_LOG(LogRedHopeSim, Error, TEXT("GROWTH FAIL: %s"), *What); ++Fails; }
		};
		const auto Near = [](double A, double B, double Tol) { return FMath::Abs(A - B) <= Tol; };
		URHDefinitionsSubsystem* DefsSub = World->GetSubsystem<URHDefinitionsSubsystem>();
		bool bData = true;
		for (const TCHAR* RowName : { TEXT("LivingQuarters"), TEXT("Lab"), TEXT("ChemTableLarge"), TEXT("Dining") })
		{
			const FRHRoomRow* Row = DefsSub ? DefsSub->GetRoom(FName(RowName)) : nullptr;
			const bool bRow = Row && Row->SliceActive;
			Expect(bRow, FString::Printf(TEXT("DT_Rooms row '%s' present and SliceActive (DT/CSV)"), RowName));
			bData &= bRow;
		}
		if (!bData)
		{
			UE_LOG(LogRedHopeSim, Display, TEXT("=== GENERATIONAL CARROT TEST END: %d failure(s) ==="), Fails);
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
			return 1;
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
		// Rooms: 2 quarters (housing), a Large Chem Table first in carve order (the
		// Lab function's morale weight is the FIRST cell's: 1.0) beside a plain
		// Lab (2 job seats), a Dining room, and a comforts crate.
		Sim->DesignateRoom(-1, 0, FName("LivingQuarters"), R);
		Sim->DesignateRoom(-1, 1, FName("LivingQuarters"), R);
		Sim->DesignateRoom(-1, 2, FName("ChemTableLarge"), R);
		Sim->DesignateRoom(-1, 3, FName("Lab"), R);
		Sim->DesignateRoom(-1, 4, FName("Dining"), R);
		Sim->Debug_DeliverCargo(FName("LuxuryGoods"));
		RunSols(14.0); // let the smoothed mood settle above the growth threshold
		               // (instant 82.0 under C2a's per-head jobs; 77.5 before - the exp smoother crosses 75 sooner)

		// G1. Eligible + growing: smoothed Hope >= 75, THRIVING, free beds, food
		// buffer. C2a instant = 50 + vault 5 + housing 7.5 + rooms (Lab fn 1.0 x 2.5 +
		// Dining 0.6 x 2.5 = 4.0, one room of each serves 25 so coverage is 1) +
		// jobs 7.5 x 2 seats / 2 people (the employment rate is 1) + comforts 8 =
		// 82.0 (it was 77.5 with 2 x 1.5 for the seats).
		{
			const URHSimWorldSubsystem::FRHHopeBreakdown H = Sim->GetColonyHope();
			Expect(Near(H.Housing, 7.5, 1e-9) && Near(H.Rooms, 4.0, 1e-6) && Near(H.Jobs, 7.5, 1e-9) && Near(H.Comforts, 8.0, 1e-9)
				&& H.WaterPenalty == 0.0 && H.FoodPenalty == 0.0 && Near(H.Total, 82.0, 1e-6),
				FString::Printf(TEXT("G1 instant Hope %.6f = 50 + vault 5 + housing %.2f + rooms %.2f + jobs %.2f + comforts %.2f, water/food penalty %.6f/%.6f (expect 82.0 = 7.5 + 4.0 + 7.5 + 8.0, 0/0)"),
					H.Total, H.Housing, H.Rooms, H.Jobs, H.Comforts, H.WaterPenalty, H.FoodPenalty));
			Expect(Sim->GetHopeSmoothed() >= 75.0 && Sim->GetHopeBand() == URHSimWorldSubsystem::ERHHopeBand::Thriving && Sim->IsGrowthEligible(),
				FString::Printf(TEXT("G1 eligible: smoothed=%.1f band=%s eligible=%d pop=%d beds-free=%d (expect >=75, THRIVING - a sensible layout lives there, FLOURISHING takes extra care - 1)"),
					Sim->GetHopeSmoothed(), Sim->GetHopeBandName(), (int32)Sim->IsGrowthEligible(), Sim->GetPopulation(), Sim->GetFreeHousing()));
		}

		// G2. Run one full growth interval (20 sols) -> exactly one birth, and it
		// is the FIRST MARTIAN (milestone + a Hope surge folded into the index).
		const int32 Pop0 = Sim->GetPopulation();
		const double MilestonesBefore = Sim->GetColonyHope().Milestones;
		RunSols(20.5);
		Expect(Pop0 == 2 && Sim->GetPopulation() == 3 && Sim->GetBirthsOnMars() == 1 && Sim->HasFirstBorn(),
			FString::Printf(TEXT("G2 first birth: pop %d->%d births=%d firstBorn=%d (expect 2->3, 1, 1)"),
				Pop0, Sim->GetPopulation(), Sim->GetBirthsOnMars(), (int32)Sim->HasFirstBorn()));
		Expect(Near(MilestonesBefore, 5.0, 1e-9) && Near(Sim->GetColonyHope().Milestones, 11.0, 1e-9),
			FString::Printf(TEXT("G2 milestone: Hope milestones %.1f -> %.1f (expect 5.0 -> 11.0 = vault + first-born)"),
				MilestonesBefore, Sim->GetColonyHope().Milestones));
		// C2a: the child is flagged Mars-born (it is the last to be recalled in a
		// shortage); the founders are not.
		Expect(Sim->GetColonists().Num() == 3 && Sim->GetColonists().Last().bMarsBorn && !Sim->GetColonists()[0].bMarsBorn && !Sim->GetColonists()[1].bMarsBorn,
			FString::Printf(TEXT("G2 the birth is flagged Mars-born, the two founders are not (%d / %d / %d; expect 0 / 0 / 1)"),
				Sim->GetColonists().Num() > 0 ? (int32)Sim->GetColonists()[0].bMarsBorn : -1, Sim->GetColonists().Num() > 1 ? (int32)Sim->GetColonists()[1].bMarsBorn : -1,
				Sim->GetColonists().Num() > 0 ? (int32)Sim->GetColonists().Last().bMarsBorn : -1));

		// G3. Streak RESETS when the colony stops thriving: crash Hope by
		// gutting housing quality and the rooms (clear every designation) -> not
		// eligible -> streak 0. The comforts crate remains, so the mood settles
		// near 69-70 (printed, not asserted) - under the 75 gate either way.
		for (int32 Cell = 0; Cell < 5; ++Cell)
		{
			Sim->DesignateRoom(-1, Cell, NAME_None, R);
		}
		RunSols(8.0); // mood falls out of THRIVING
		Expect(!Sim->IsGrowthEligible() && Near(Sim->GetGrowthProgress(), 0.0, 1e-9),
			FString::Printf(TEXT("G3 lapse: smoothed=%.1f band=%s eligible=%d progress=%.2f (expect not-eligible, 0.00)"),
				Sim->GetHopeSmoothed(), Sim->GetHopeBandName(), (int32)Sim->IsGrowthEligible(), Sim->GetGrowthProgress()));

		// G4. Save v15 round-trip: births + first-born flag survive.
		const int32 BirthsBefore = Sim->GetBirthsOnMars();
		FString Err;
		const bool bSaved = Sim->SaveColony(TEXT("growthtest"), Err);
		const bool bLoaded = Sim->LoadColony(TEXT("growthtest"), Err);
		Expect(bSaved && bLoaded && BirthsBefore == Sim->GetBirthsOnMars() && Sim->GetBirthsOnMars() == 1 && Sim->HasFirstBorn()
			&& Sim->GetColonists().Num() == 3 && Sim->GetColonists().Last().bMarsBorn && !Sim->GetColonists()[0].bMarsBorn,
			FString::Printf(TEXT("G4 save/load v34: births %d->%d firstBorn=%d, the child still Mars-born (expect identical, 1, 1)"),
				BirthsBefore, Sim->GetBirthsOnMars(), (int32)Sim->HasFirstBorn()));

		UE_LOG(LogRedHopeSim, Display, TEXT("=== GENERATIONAL CARROT TEST END: %d failure(s) ==="), Fails);
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return Fails > 0 ? 1 : 0;
	}

	// M2 Gate D+ self-test: DISCOVERIES (the Flourishing layer) - staffed Labs
	// accrue seat-hours while smoothed Hope holds above the threshold; rows pop
	// in authored Order with permanent Hope milestones + stock rewards; progress
	// keeps across a pause; save v16. `-discovery`.
	// D3a (2026-10-17): fails loudly. Under the halved Hope weights 85 is no
	// longer reachable from rooms and jobs alone (that recipe tops out at
	// 75.75): the shipped tuning needs furnished rooms and comforts, and this
	// recipe proves it still gets there while the room-and-job part alone does not.
	if (FParse::Param(*Params, TEXT("discovery")))
	{
		UE_LOG(LogRedHopeSim, Display, TEXT("=== DISCOVERIES TEST (M2 Gate D+) ==="));
		const int32 StepsPerSolH = (int32)(URHSimClockSubsystem::SolLengthSimSeconds / URHSimClockSubsystem::EraStepSimSeconds);
		// Fresh-water topping keeps potability out of the Hope math (the water
		// loop is separately tested; this isolates the discovery gate).
		const auto Step = [&]()
		{
			const double StepSols = URHSimClockSubsystem::EraStepSimSeconds / (double)URHSimClockSubsystem::SolLengthSimSeconds;
			Sim->Debug_AddFreshWater(100.0 * StepSols);
			Clock->Debug_AdvanceSimSeconds(URHSimClockSubsystem::EraStepSimSeconds);
			Sim->EraStep(URHSimClockSubsystem::EraStepSimSeconds);
		};
		const auto RunSols = [&](double Sols)
		{
			for (int32 S = 0; S < (int32)(Sols * StepsPerSolH); ++S)
			{
				Step();
			}
		};
		int32 Fails = 0;
		const auto Expect = [&](bool bOk, const FString& What)
		{
			if (bOk) { UE_LOG(LogRedHopeSim, Display, TEXT("DISC ok: %s"), *What); }
			else { UE_LOG(LogRedHopeSim, Error, TEXT("DISC FAIL: %s"), *What); ++Fails; }
		};
		const auto Near = [](double A, double B, double Tol) { return FMath::Abs(A - B) <= Tol; };
		// Pure-data: the four authored rows ride the synced DT_Discoveries
		// (2026-07-16) - assert the arithmetic-bearing columns instead of
		// injecting, so this suite fails loudly on DT/CSV drift.
		URHDefinitionsSubsystem* DefsSub = World->GetSubsystem<URHDefinitionsSubsystem>();
		const auto CheckDisc = [&](const TCHAR* Name, int32 Order, float SeatH, float Bonus, FName Reward, float Kg) -> bool
		{
			const FRHDiscoveryRow* Row = DefsSub->GetDiscovery(FName(Name));
			const bool bRow = Row && Row->SliceActive && Row->Order == Order && Row->LabSeatHours == SeatH
				&& Row->HopeBonus == Bonus && Row->RewardResource == Reward && Row->RewardKg == Kg;
			Expect(bRow, FString::Printf(TEXT("D0 DT_Discoveries row %s: order %d, %.0f seat-hours, +%.0f Hope, reward %s x %.0f (DT/CSV)"),
				Name, Order, SeatH, Bonus, *Reward.ToString(), Kg));
			return bRow;
		};
		bool bData = true;
		bData &= CheckDisc(TEXT("CropStrain"), 1, 100.f, 2.f, FName("Seeds"), 50.f);
		bData &= CheckDisc(TEXT("RegolithCeramics"), 2, 150.f, 2.f, FName("Struct"), 100.f);
		bData &= CheckDisc(TEXT("SubsurfaceBrine"), 3, 200.f, 3.f, FName("Water"), 150.f);
		bData &= CheckDisc(TEXT("MicrobialLife"), 4, 300.f, 8.f, NAME_None, 0.f);
		if (!bData)
		{
			UE_LOG(LogRedHopeSim, Display, TEXT("=== DISCOVERIES TEST END: %d failure(s) ==="), Fails);
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
			return 1;
		}
		const TArray<FName> Authored = { FName("CropStrain"), FName("RegolithCeramics"), FName("SubsurfaceBrine"), FName("MicrobialLife") };
		const auto LogIsAuthoredPrefix = [&]()
		{
			const TArray<FName>& Log = Sim->GetDiscoveryLog();
			if (Log.Num() > Authored.Num()) { return false; }
			for (int32 i = 0; i < Log.Num(); ++i) { if (Log[i] != Authored[i]) { return false; } }
			return true;
		};
		const auto LogBonusSum = [&]()
		{
			double Sum = 0.0;
			for (const FName& Found : Sim->GetDiscoveryLog())
			{
				const FRHDiscoveryRow* Row = DefsSub->GetDiscovery(Found);
				Sum += Row ? (double)Row->HopeBonus : 0.0;
			}
			return Sum;
		};
		const auto LogText = [&]()
		{
			FString Out;
			for (const FName& Found : Sim->GetDiscoveryLog()) { Out += (Out.IsEmpty() ? TEXT("") : TEXT(",")) + Found.ToString(); }
			return Out.IsEmpty() ? FString(TEXT("none")) : Out;
		};
		TArray<FString> Rejects;
		const FDelegateHandle RejectH = Sim->OnCommandRejected.AddLambda([&Rejects](const FRHCommand&, const FString& S) { Rejects.Add(S); });
		// Furnish orders ride the uplink like any order: one order, one step.
		const auto Furnish = [&](const TCHAR* Row, int32 Cell, int32 Slot)
		{
			FRHCommand C; C.Verb = FName("Furnish"); C.Target = FName(Row); C.Level = -1;
			C.Location = Sim->GetCellCentreCm(FIntVector(-1, Cell, 0));
			C.Value = URHSimWorldSubsystem::EncodeFurnish(Cell, Slot, 0);
			Sim->EnqueueCommand(C);
			Step();
		};

		// A thriving 2-person colony that has to EARN the research gate. Cells:
		// c0, c1 Living; c2 Large Chem Table, c3 Lab (2 Lab seats, the T2 table
		// first in carve order so the Lab function's morale weight is 1.0);
		// c4 Dining; c5 Large Workbench (UNSTAFFED: the two colonists fill the
		// seats in carve order, so c2 and c3 take them); c6 Infirmary. Lab seat
		// yield 1.35 + 1.0 = 2.35 (not the old 2.0), so accrual is ~17% faster
		// before mastery. Instant Hope (no emitters, so no adjacency noise):
		// 50 + vault 5 + housing 7.5 + rooms (2.5 + 1.5 + 2.5 + 1.25 = 7.75) +
		// jobs 7.5 x 2 seats / 2 people + comforts 8 + furnishings 6 (a dining
		// table and five plants, 140 Struct from the Lander's 400; per head, the
		// sum x 8 / 2 people is far over the cap) = 91.75. C2a: it was 87.25 with
		// 2 x 1.5 for the seats - a colony with a seat for everyone now earns the
		// whole jobs line - so the smoother crosses 85 about 5 sols after the crew
		// arrives (it was about 8) and the pins below (DS4, DS5) moved up with it.
		FString R;
		Sim->ExtendShaft(1, FVector(1000.f, 1000.f, 0.f));
		Sim->ExcavateFloor(-1, 7, R);
		Sim->Debug_PlaceInstant(FName("SolarArray"), FVector(3500.f, 1000.f, 0.f));
		Sim->Debug_PlaceInstant(FName("BatteryBank"), FVector(1000.f, 3500.f, 0.f));
		Sim->Debug_PlaceInstant(FName("AirFilter"), FVector(1000.f, 1500.f, 0.f), -1);
		Sim->AddStock(FName("Oxygen"), 4000.0);
		Sim->AddStock(FName("Water"), 2000.0);
		Sim->AddStock(FName("Food"), 4000.0);
		RunSols(2.5);
		Sim->Debug_AddColonists(2);
		const bool bZoned = Sim->DesignateRoom(-1, 0, FName("LivingQuarters"), R)
			&& Sim->DesignateRoom(-1, 1, FName("LivingQuarters"), R)
			&& Sim->DesignateRoom(-1, 2, FName("ChemTableLarge"), R)
			&& Sim->DesignateRoom(-1, 3, FName("Lab"), R)
			&& Sim->DesignateRoom(-1, 4, FName("Dining"), R)
			&& Sim->DesignateRoom(-1, 5, FName("WorkbenchLarge"), R)
			&& Sim->DesignateRoom(-1, 6, FName("Infirmary"), R);
		Expect(bZoned, TEXT("DS1 setup: c0, c1 Living, c2 Large Chem Table, c3 Lab, c4 Dining, c5 Large Workbench, c6 Infirmary all accepted"));
		Sim->Debug_DeliverCargo(FName("LuxuryGoods"));
		const double StructShelf0 = Sim->GetTotalSolid(FName("Struct"));
		Furnish(TEXT("DiningTable"), 4, 0);
		for (const int32 Slot : { 2, 3, 6, 7, 8 }) { Furnish(TEXT("Plant"), 4, Slot); }
		const double StructSpent = StructShelf0 - Sim->GetTotalSolid(FName("Struct"));
		Expect(Rejects.Num() == 0 && Near(Sim->GetColonyHope().Furnishings, 6.0, 1e-9),
			FString::Printf(TEXT("DS1 six furnish orders (a dining table and five plants in c4): %d rejected, Furnishings %.1f (expect 0, 6.0)%s%s"),
				Rejects.Num(), Sim->GetColonyHope().Furnishings, Rejects.Num() ? TEXT(" - ") : TEXT(""), Rejects.Num() ? *Rejects.Last() : TEXT("")));
		// The furniture spends from the Lander's own shelf (GetTotalSolid), not
		// the stock pool the ceramics reward lands in (GetStock) - two ledgers.
		Expect(Near(StructSpent, 140.0, 1e-6),
			FString::Printf(TEXT("DS1 the six orders cost %.1f Struct off the Lander's shelf (expect 140 = 40 + 5 x 20)"), StructSpent));
		RunSols(0.1); // one pass so jobs settle
		Expect(Sim->GetColonyHope().FilledSeats == 2 && Near(Sim->GetLabYieldSeatSum(), 2.35, 1e-6) && Near(Sim->GetWorkstationYieldSeatSum(), 0.0, 1e-6),
			FString::Printf(TEXT("DS1 seats: filled %d, Lab yield %.2f, Workstation yield %.2f (expect 2, 2.35, 0.00: the Large Workbench stays unstaffed)"),
				Sim->GetColonyHope().FilledSeats, Sim->GetLabYieldSeatSum(), Sim->GetWorkstationYieldSeatSum()));
		const double Struct0 = Sim->GetStock(FName("Struct"));

		// DS2. Below the threshold nothing accrues (the smoothed mood lags).
		RunSols(0.9);
		{
			const URHSimWorldSubsystem::FRHHopeBreakdown H = Sim->GetColonyHope();
			Expect(Sim->GetHopeSmoothed() < 85.0 && !Sim->IsResearchAccruing() && Near(Sim->GetDiscoveryProgress(), 0.0, 1e-9),
				FString::Printf(TEXT("DS2 gate: smoothed=%.1f accruing=%d progress=%.2f (expect <85, 0, 0.00)"),
					Sim->GetHopeSmoothed(), (int32)Sim->IsResearchAccruing(), Sim->GetDiscoveryProgress()));
			Expect(Near(H.Total, 91.75, 1e-6) && Near(H.Furnishings, 6.0, 1e-9) && Near(H.Comforts, 8.0, 1e-9) && Near(H.Jobs, 7.5, 1e-9)
				&& H.WaterPenalty == 0.0 && H.FoodPenalty == 0.0 && H.UnsupportedPenalty == 0.0 && H.AdjacencyPenalty == 0.0,
				FString::Printf(TEXT("DS2 instant Hope %.3f with furnishings %.1f, comforts %.1f, jobs %.1f and no penalties (expect 91.75, 6.0, 8.0, 7.5)"),
					H.Total, H.Furnishings, H.Comforts, H.Jobs));
			// The D3a claim: rooms and jobs alone no longer reach the research
			// gate. The crew has to be given comforts and a furnished home.
			const double Bare = H.Total - H.Furnishings - H.Comforts;
			Expect(Bare < Sim->GetHopeDiscoveryThreshold(),
				FString::Printf(TEXT("DS3 without furnishings and comforts the same colony reads %.2f, under the %.0f research gate (the extra care is what opens research)"),
					Bare, Sim->GetHopeDiscoveryThreshold()));
		}

		// DS4. 10 sols in: the mood crossed ~85 about 5 sols after the crew landed
		// (instant 91.75 under C2a; it crossed near sol 8 at 87.25); 2.35 yield-seats x
		// 24 seat-h/sol (before mastery) pop CropStrain (100) and then RegolithCeramics
		// (150). Seeds +50, milestone 5 -> 9. The exact count is pinned from the
		// C2a run on the shipped tuning: 352 seat-hours are in (the first two rows need
		// 250, the third 450), SubsurfaceBrine (200) is about half done, so each side has
		// about 1.5 sols of margin.
		const double Seeds0 = Sim->GetStock(FName("Seeds"));
		RunSols(9.0);
		Expect(Sim->GetDiscoveryLog().Num() >= 1 && Sim->GetDiscoveryLog()[0] == FName("CropStrain") && LogIsAuthoredPrefix()
			&& Near(Sim->GetStock(FName("Seeds")) - Seeds0, 50.0, 1e-9) && Near(Sim->GetColonyHope().Milestones, 5.0 + LogBonusSum(), 1e-9),
			FString::Printf(TEXT("DS4 first window: found=%d [%s] next=%s (%.3f of the way) seeds +%.0f milestones=%.1f (expect >=1 starting CropStrain, +50, 5 + the found rows' bonus = %.1f)"),
				Sim->GetDiscoveryLog().Num(), *LogText(), *Sim->GetNextDiscovery().ToString(), Sim->GetDiscoveryProgress(),
				Sim->GetStock(FName("Seeds")) - Seeds0, Sim->GetColonyHope().Milestones, 5.0 + LogBonusSum()));
		Expect(Sim->GetDiscoveryLog().Num() == 2 && Sim->GetDiscoveryLog()[1] == FName("RegolithCeramics") && Sim->GetNextDiscovery() == FName("SubsurfaceBrine"),
			FString::Printf(TEXT("DS4 pin: exactly CropStrain and RegolithCeramics found, SubsurfaceBrine next (found=%d next=%s; the shipped tuning's pace - re-pin it if the tuning moves)"),
				Sim->GetDiscoveryLog().Num(), *Sim->GetNextDiscovery().ToString()));

		// DS5. 6 more sols: the lab crew's MASTERY has been ramping since they
		// took their posts (alive pass - skill accrues with job time), so the
		// accrual rate runs above the fresh-crew rate by now. The log stays an
		// ordered prefix of the authored four; the ceramics reward (Struct +100,
		// into the stock pool) is exactly 100 per RegolithCeramics found since
		// the post-furnishing baseline (brine rewards Water). C2a: the window now
		// finishes the fourth row (750 seat-hours in all; about 830 are in, a margin
		// of a sol of lab time), so all four are found and nothing is next.
		RunSols(6.0);
		{
			const double StructGained = Sim->GetStock(FName("Struct")) - Struct0;
			const int32 Ceramics = Sim->HasDiscovered(FName("RegolithCeramics")) ? 1 : 0;
			Expect(Sim->GetDiscoveryLog().Num() >= 1 && LogIsAuthoredPrefix() && Near(StructGained, 100.0 * Ceramics, 1e-9)
				&& Near(Sim->GetColonyHope().Milestones, 5.0 + LogBonusSum(), 1e-9),
				FString::Printf(TEXT("DS5 second window: found=%d [%s] next=%s (%.3f of the way) struct +%.0f (expect 100 x ceramics found = %d) milestones=%.1f (expect 5 + the found rows' bonus = %.1f)"),
					Sim->GetDiscoveryLog().Num(), *LogText(), *Sim->GetNextDiscovery().ToString(), Sim->GetDiscoveryProgress(), StructGained, Ceramics * 100,
					Sim->GetColonyHope().Milestones, 5.0 + LogBonusSum()));
			Expect(Sim->GetDiscoveryLog().Num() == 4 && Sim->GetNextDiscovery().IsNone() && Near(StructGained, 100.0, 1e-9),
				FString::Printf(TEXT("DS5 pin: all four found, nothing next, Struct +%.0f (found=%d next=%s)"),
					StructGained, Sim->GetDiscoveryLog().Num(), *Sim->GetNextDiscovery().ToString()));
		}

		// DS6. Save v16 round-trip: the log and the partial progress survive.
		const int32 FoundBefore = Sim->GetDiscoveryLog().Num();
		const double ProgressBefore = Sim->GetDiscoveryProgress();
		FString Err;
		const bool bSaved = Sim->SaveColony(TEXT("disctest"), Err);
		const bool bLoaded = Sim->LoadColony(TEXT("disctest"), Err);
		Expect(bSaved && bLoaded && FoundBefore == Sim->GetDiscoveryLog().Num() && Near(ProgressBefore, Sim->GetDiscoveryProgress(), 1e-9),
			FString::Printf(TEXT("DS6 save/load v16: found %d->%d progress %.3f->%.3f (expect identical)"),
				FoundBefore, Sim->GetDiscoveryLog().Num(), ProgressBefore, Sim->GetDiscoveryProgress()));

		Sim->OnCommandRejected.Remove(RejectH);
		UE_LOG(LogRedHopeSim, Display, TEXT("=== DISCOVERIES TEST END: %d failure(s) ==="), Fails);
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return Fails > 0 ? 1 : 0;
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

		// 5) COLLAPSE: strip the stores; the crew go on no rations and are
		// recalled gradually, and the emptied colony declares Collapse ONCE -
		// abstract, with the recovery path named (Gate-D placeholder wording).
		// C2a: two residents at ration 0 are recalled at 2 x 1 / 2 = 1 a sol, at
		// 1.0 and 2.0 sols, so the colony is empty a sol and a half before the
		// 3.5 sols run out (it was both together at 2.0).
		Sim->AddStock(FName("Food"), -Sim->GetStock(FName("Food")));
		Sim->AddStock(FName("Water"), -Sim->GetStock(FName("Water")));
		Sim->AddStock(FName("Oxygen"), -Sim->GetStock(FName("Oxygen")));
		RunSols(3.5); // the two recalls (1.0 and 2.0 sols) + margin
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
			Expect(Near(Sim->GetHabO2Kg(Hh), 200.0, 0.1) && bAll && Near(Sim->GetColonyHope().Housing, Sim->GetHopeHousingMax(), 1e-6),
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
			// C2a. Rooms: a Workstation on the floor and one in the network are one
			// function, counted once (0.2), then Cooking 0.3 and Dining 0.6, each x
			// HopePerMoralePoint; one room of each serves 25 of the 9, so coverage is 1.
			// Adjacency: Q-R offends the Dining bay: 1 pair, and 1 of the 6 refuser cells
			// (the four Living bays of network 1 - A:0, C:1, H:0, H:1 - plus PQ's Living
			// and Dining bays; the floor has none) = 16 / 6 = 2.667.
			Expect(Hp.OffendedPairs == 1 && Hp.OffendedRefusers == 1 && Hp.RefuserCells == 6 && Near(Hp.AdjacencyPenalty, Sim->GetHopeAdjacencyMax() / 6.0, 1e-9)
				&& Near(Hp.Rooms, (0.2 + 0.3 + 0.6) * Sim->GetHopePerMoralePoint(), 1e-6),
				FString::Printf(TEXT("S10 Hope: Q-R offends, the corridor partitions P-R (pairs %d, %d of %d refuser cells, adj %.1f, rooms %.2f)"), Hp.OffendedPairs, Hp.OffendedRefusers, Hp.RefuserCells, Hp.AdjacencyPenalty, Hp.Rooms));
			Expect(Hp.FilledSeats == 4 && Near(Hp.Jobs, Sim->GetHopeJobsMax() * 4.0 / 9.0, 1e-9) && Near(Sim->GetLabYieldSeatSum(), 4.0, 1e-4) && Near(Sim->GetWorkstationYieldSeatSum(), 2.0, 1e-4)
				&& bSt && St == FIntVector(-1, 0, 0) && Near(Hp.Housing, Sim->GetHopeHousingMax() * 5.0 / 9.0, 1e-6),
				FString::Printf(TEXT("S10 seats: floor first (filled %d, jobs %.0f, lab %.2f, ws %.2f, housing %.3f)"), Hp.FilledSeats, Hp.Jobs,
					Sim->GetLabYieldSeatSum(), Sim->GetWorkstationYieldSeatSum(), Hp.Housing));
		}
		Sim->DesignateBay(PQ, 1, NAME_None, Why);
		Expect(Sim->GetColonyHope().OffendedPairs == 0 && Near(Sim->GetColonyHope().Rooms, (0.2 + 0.3) * Sim->GetHopePerMoralePoint(), 1e-6), TEXT("S10 clearing the Dining bay clears the pair"));
		Sim->DesignateBay(PQ, 1, FName("Dining"), Why);
		Expect(Sim->GetColonyHope().OffendedPairs == 1 && Near(Sim->GetColonyHope().Rooms, (0.2 + 0.3 + 0.6) * Sim->GetHopePerMoralePoint(), 1e-6), TEXT("S10 re-zoned: the pair is back"));
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

		// S12. Loss and recovery. C2a: a home with no air is recalled GRADUALLY - a
		// home of four at ration 0 loses one resident (the newest first) every half
		// sol, not all of them together after two. The floor lost its rating first,
		// so within this sol its newest resident (Id 8) is the only one gone (the next
		// is due at a full sol); the networks lost theirs a few steps later and their
		// first recall is still to come.
		int32 NewestOnFloor = 0;
		for (const FRHColonist& Col : Sim->GetColonists()) { if (Col.HomeHab == 0 && Col.HomeLevel == -1) { NewestOnFloor = FMath::Max(NewestOnFloor, Col.Id); } }
		RunSols(1.0);
		{
			bool bNone = true;
			for (const FRHColonist& Col : Sim->GetColonists()) { bNone &= !Col.bSupported; }
			Expect(!Sim->IsHabRated(A) && !Sim->IsHabRated(PQ) && !Sim->IsFloorRated(-1) && bNone && Sim->GetHousingCapacity() == 0 && Sim->GetColonyHope().FilledSeats == 0
				&& Near(Sim->GetLabYieldSeatSum(), 0.0, 1e-9) && Near(Sim->GetEffectiveEvacSols(), 2.0, 1e-9) && Sim->GetPopulation() == 8 && NewestOnFloor == 8 && !Sim->FindColonist(NewestOnFloor),
				FString::Printf(TEXT("S12 a dry pool: every rating lost, everyone unsupported, no seats, no infirmary, one recalled so far - floor -1's newest (Id %d); pop %d (expect 8)"), NewestOnFloor, Sim->GetPopulation()));
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
			bool bSurfaceDown = Cs.Num() == 8; // 9 before the floor's newest was recalled in S12
			for (int32 i = 0; bSurfaceDown && i < 4; ++i) { bSurfaceDown &= !Cs[i].bSupported; }
			Expect(!Sim->IsHabRated(A) && Sim->IsHabRated(PQ) && Sim->IsFloorRated(-1) && bSurfaceDown && Near(Sim->GetEffectiveEvacSols(), 2.0, 1e-9),
				TEXT("S12b network 1's filter off: only it fails (no shaft link); its four lose support; the infirmary in B goes offline"));
		}
		RunSols(2.5); // four residents at ration 0: one recall every half sol, newest first (C2a) - and (C2b) once a sol the colony moves anyone whose home has no bed for them to a free bed elsewhere: floor -1 has one free (S12's recall), so one of the surface four, the newest still here, moves in and is saved
		{
			const TArray<FRHColonist>& Cs = Sim->GetColonists();
			int32 OnFloor = 0, OnPQ = 0, FromSurface = 0, FloorFromSurface = 0, Oldest = 0;
			for (const FRHColonist& Col : Cs)
			{
				OnFloor += (Col.HomeHab == 0 && Col.HomeLevel == -1) ? 1 : 0;
				OnPQ += Col.HomeHab == PQ ? 1 : 0;
				FromSurface += Col.Id <= 4 ? 1 : 0; // Ids 1-4 were the surface four (network 1)
				FloorFromSurface += (Col.Id <= 4 && Col.HomeHab == 0 && Col.HomeLevel == -1) ? 1 : 0;
				Oldest += (Col.Id >= 5 && Col.Id <= 7 && Col.HomeHab == 0 && Col.HomeLevel == -1) ? 1 : 0;
			}
			// The floor's three oldest stay, one of the surface four moved onto the floor, PQ's one stays; the other three were recalled.
			bool bOk = Cs.Num() == 5 && OnFloor == 4 && OnPQ == 1 && FromSurface == 1 && FloorFromSurface == 1 && Oldest == 3;
			bOk = bOk && Sim->GetHabResidents(A) == 0 && Sim->GetHabResidents(Hh) == 0 && Sim->GetHousingCapacity() == 5;
			Expect(bOk, FString::Printf(TEXT("S12b three of the four surface residents are recalled, the fourth moves to the floor's free bed (C2b); floor and PQ stay (pop %d, capacity %d, %d on the floor, %d in PQ)"), Sim->GetPopulation(), Sim->GetHousingCapacity(), OnFloor, OnPQ));
			TArray<FIntPoint> Before;
			for (const FRHColonist& Col : Cs) { Before.Add(FIntPoint(Col.HomeLevel, Col.HomeHab)); }
			Err.Reset();
			Sim->SaveColony(TEXT("surfacehabtest2"), Err);
			Sim->Debug_AddColonists(1);
			Sim->LoadColony(TEXT("surfacehabtest2"), Err);
			bool bSame = Sim->GetPopulation() == Before.Num();
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
				&& Near(H.Housing, Sim->GetHopeHousingMax() * 2.0 / 7.0, 1e-6) && H.Furnishings == 0.0 && Sim->GetFurniture().Num() == 0,
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
		Expect(Sim->GetHousingCapacity() == 8 && Near(Sim->GetColonyHope().Housing, Sim->GetHopeHousingMax() * 3.0 / 7.0, 1e-6),
			FString::Printf(TEXT("F3 two cots: capacity 8, housing %.4f"), Sim->GetColonyHope().Housing));
		Expect(Furnish(TEXT("Cot"), C0, 8) && Furnish(TEXT("Cot"), C0, 10, 1), TEXT("F3 cots 3 and 4 (one turned)"));
		Spent += 80.0;
		Expect(Sim->GetHousingCapacity() == 10 && Near(Sim->GetColonyHope().Housing, Sim->GetHopeHousingMax() * 5.0 / 7.0, 1e-6) && Sim->GetCellBeds(C0) == 4,
			FString::Printf(TEXT("F3 four cots: capacity %d, housing %.4f, c0 beds %d"), Sim->GetHousingCapacity(), Sim->GetColonyHope().Housing, Sim->GetCellBeds(C0)));
		Expect(!CanPlace(TEXT("Cot"), C0, 5, 0, Why) && Why.Contains(TEXT("Overlaps")), FString::Printf(TEXT("F3 a fifth cot overlaps (%s)"), *Why));
		Expect(Sim->Debug_AddColonists(4) == 3 && Sim->GetPopulation() == 10 && Near(Sim->GetColonyHope().Housing, Sim->GetHopeHousingMax() * 0.5, 1e-6),
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
			&& Sim->GetColonyHope().FilledSeats == 7 && Near(Sim->GetColonyHope().Jobs, Sim->GetHopeJobsMax() * 7.0 / 10.0, 1e-6),
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

		// F6. Comfort. C2a: the Furnishings line is per resident - the raw sum x
		// ComfortRefPop (8) / the 10 people = x 0.8 - and then capped at 6. A dining
		// table (1) is 0.8; with five plants the raw sum is 6, so 4.8; a sixth plant
		// makes 7 and 5.6, still under the cap; two more (raw 9 = 7.2) reach it.
		Expect(Furnish(TEXT("DiningTable"), C4, 0) && Near(Sim->GetColonyHope().Furnishings, 0.8, 1e-9),
			FString::Printf(TEXT("F6 a dining table: Furnishings %.1f"), Sim->GetColonyHope().Furnishings));
		for (const int32 S : { 2, 3, 6, 7, 8 }) { Furnish(TEXT("Plant"), C4, S); }
		Expect(Near(Sim->GetColonyHope().Furnishings, 4.8, 1e-9), FString::Printf(TEXT("F6 + five plants: Furnishings %.1f"), Sim->GetColonyHope().Furnishings));
		Expect(Furnish(TEXT("Plant"), C4, 9) && Near(Sim->GetColonyHope().Furnishings, 5.6, 1e-9), TEXT("F6 a sixth plant: 5.6, under the cap"));
		Expect(Furnish(TEXT("Plant"), C4, 10) && Furnish(TEXT("Plant"), C4, 11) && Near(Sim->GetColonyHope().Furnishings, 6.0, 1e-9), TEXT("F6 two more plants: capped at 6"));
		Spent += 40.0 + 8 * 20.0;

		// F7. Re-zoning.
		Expect(!Sim->DesignateRoom(-1, 0, FName("Lab"), Why) && Why.Contains(TEXT("Cot")), FString::Printf(TEXT("F7 c0 to Lab refused (%s)"), *Why));
		Expect(!Sim->DesignateRoom(-1, 4, FName("LivingQuarters"), Why) && Why.Contains(TEXT("Dining Table")), FString::Printf(TEXT("F7 c4 to Living refused (%s)"), *Why));
		Expect(Furnish(TEXT("Plant"), C2, 15) && Sim->DesignateRoom(-1, 2, FName("Dining"), Why) && Near(Sim->GetColonyHope().Housing, Sim->GetHopeHousingMax() * 0.4, 1e-6)
			&& Sim->GetHousingCapacity() == 10, FString::Printf(TEXT("F7 a plant goes Living -> Dining with its room; housing %.2f, capacity %d"), Sim->GetColonyHope().Housing, Sim->GetHousingCapacity()));
		Spent += 20.0;
		Expect(!Sim->DesignateRoom(-1, 2, NAME_None, Why) && Sim->DesignateRoom(-1, 2, FName("LivingQuarters"), R) && Near(Sim->GetColonyHope().Housing, Sim->GetHopeHousingMax() * 0.5, 1e-6),
			FString::Printf(TEXT("F7 clearing c2 refused (%s); back to Living"), *Why));

		// F8. Selling.
		{
			const double Out0 = LanderKg(TEXT("Struct"), true);
			Expect(SellPiece(5) && Near(LanderKg(TEXT("Struct"), true) - Out0, 30.0, 1e-6) && Seats(C1).Y == 1,
				FString::Printf(TEXT("F8 a desk sold: +%.0f Struct on the Lander's output shelf, c1 back to 1 seat"), LanderKg(TEXT("Struct"), true) - Out0));
			Refunded += 30.0;
			const double Out1 = LanderKg(TEXT("Struct"), true);
			TMap<FName, double> Refund;
			Expect(Sim->CanSellCell(C4, Why, &Refund) && Near(Refund.FindRef(NStruct), 150.0, 1e-6) && SellCell(C4)
				&& Near(LanderKg(TEXT("Struct"), true) - Out1, 150.0, 1e-6) && Sim->GetCellFurniture(C4) == 0 && Sim->DesignateRoom(-1, 4, NAME_None, R),
				TEXT("F8 everything in c4 sold in one order (+150 = 0.75 x 200: the table and eight plants); the cell can be cleared again"));
			Refunded += 150.0;
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
			Expect(Sim->IsHabRated(A) && Near(Sim->GetColonyHope().Furnishings, F0 + 1.0 * Sim->GetComfortRefPop() / Sim->GetPopulation(), 1e-9),
				FString::Printf(TEXT("F9 rated: the plant counts, per resident (%.1f -> %.1f; expect +8 / %d people)"), F0, Sim->GetColonyHope().Furnishings, Sim->GetPopulation()));
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

		// I13. Deposit piles follow the same rule (2026-10-17): a pile goes to
		// the nearest building that wants it; a store takes it only when nothing
		// here consumes it. With a Forge whose hopper is full, regolith waits in
		// its pile - before this it went to the Lander for good (50 t of
		// Regolith_A's 60 t by sol 45 in the playtest) and the Forge starved.
		{
			const FRHDepositState* RegA = Dep("Regolith_A");
			const int32 RegId = RegA ? RegA->Id : 0;
			const auto PileDest = [&]()
			{
				Sim->Debug_SetDepositPileKg(RegId, 500.0);
				Sim->Debug_StepTaskBoard();
				return Sim->Debug_GetOpenHaulDestFromDeposit(RegId);
			};
			Sim->Debug_ClearTasks();
			const int32 NoForge = PileDest();
			const int32 LanderId = LastIdOf(TEXT("Lander"));
			Sim->Debug_PlaceInstant(FName("Forge"), FVector(RegA ? RegA->LocationCm.X : 0.0, RegA ? RegA->LocationCm.Y + 1500.0 : 0.0, 0.0));
			const int32 ForgeId = LastIdOf(TEXT("Forge"));
			Sim->Debug_ClearTasks();
			const int32 WithRoom = PileDest();
			Sim->Debug_AddSolidAt(ForgeId, FName("Regolith"), 300.0, false);
			Sim->Debug_ClearTasks();
			const int32 HopperFull = PileDest();
			Expect(RegId != 0 && ForgeId != 0 && NoForge == LanderId && WithRoom == ForgeId && HopperFull == 0,
				FString::Printf(TEXT("I13 regolith pile: no Forge -> the store #%d (posted to #%d); Forge with room -> the Forge #%d (#%d); Forge hopper full -> waits in the pile (#%d)"),
					LanderId, NoForge, ForgeId, WithRoom, HopperFull));
		}

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
	// D3a (2026-10-17): fails loudly. The recipe is the shipped tuning's proof
	// that a furnished, comforted six-person colony still clears the 85
	// research gate: quarters on three cells, the T2 benches FIRST in carve
	// order (the first cell of a function sets its morale weight), a dining
	// room with a table and plants, and comforts.
	if (FParse::Param(*Params, TEXT("tiers")))
	{
		UE_LOG(LogRedHopeSim, Display, TEXT("=== STATION TIERS TEST (v25) ==="));
		const int32 StepsPerSolH = (int32)(URHSimClockSubsystem::SolLengthSimSeconds / URHSimClockSubsystem::EraStepSimSeconds);
		const auto Step = [&]()
		{
			// 6 drinkers decay potability fast - feed enough fresh melt to
			// hold it above the floor, or the water Hope penalty pins the
			// smoothed mood just under the 85 discovery gate.
			Sim->Debug_AddFreshWater(500.0 * URHSimClockSubsystem::EraStepSimSeconds / (double)URHSimClockSubsystem::SolLengthSimSeconds);
			Clock->Debug_AdvanceSimSeconds(URHSimClockSubsystem::EraStepSimSeconds);
			Sim->EraStep(URHSimClockSubsystem::EraStepSimSeconds);
		};
		const auto RunSols = [&](double Sols)
		{
			for (int32 S = 0; S < (int32)(Sols * StepsPerSolH); ++S)
			{
				Step();
			}
		};
		int32 Fails = 0;
		const auto Expect = [&](bool bOk, const FString& What)
		{
			if (bOk) { UE_LOG(LogRedHopeSim, Display, TEXT("TIERS ok: %s"), *What); }
			else { UE_LOG(LogRedHopeSim, Error, TEXT("TIERS FAIL: %s"), *What); ++Fails; }
		};
		const auto Near = [](double A, double B, double Tol) { return FMath::Abs(A - B) <= Tol; };
		URHDefinitionsSubsystem* DefsSub = World->GetSubsystem<URHDefinitionsSubsystem>();
		// Pure-data: the tier ladder's stations ride the synced DT_Rooms
		// (2026-07-16) - assert the arithmetic-bearing columns instead of
		// injecting, so this suite fails loudly on DT/CSV drift.
		const auto CheckRoom = [&](const TCHAR* Name, FName Function, int32 Tier, int32 Seats, float Yield, bool bActive) -> bool
		{
			const FRHRoomRow* Row = DefsSub->GetRoom(FName(Name));
			const bool bRow = Row && Row->SliceActive == bActive && Row->Function == Function
				&& Row->Tier == Tier && Row->SeatCount == Seats && Row->YieldMul == Yield;
			Expect(bRow, FString::Printf(TEXT("T0 DT_Rooms row %s: %s tier %d, %d seat(s), x%.2f, %s (DT/CSV)"),
				Name, *Function.ToString(), Tier, Seats, Yield, bActive ? TEXT("active") : TEXT("dormant")));
			return bRow;
		};
		bool bData = true;
		bData &= CheckRoom(TEXT("WorkbenchLarge"), FName("Workstation"), 2, 2, 1.35f, true);
		bData &= CheckRoom(TEXT("ChemTableLarge"), FName("Lab"), 2, 1, 1.35f, true);
		bData &= CheckRoom(TEXT("Workshop"), FName("Workstation"), 3, 3, 1.8f, false); // DORMANT until unlocked
		bData &= CheckRoom(TEXT("Infirmary"), FName("Infirmary"), 1, 1, 1.0f, true);
		if (!bData)
		{
			UE_LOG(LogRedHopeSim, Display, TEXT("=== STATION TIERS TEST END: %d failure(s) ==="), Fails);
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
			return 1;
		}
		// The unlocking discovery stays a SYNTHETIC inject (test-only row, not
		// in the CSV): first in the sequence (Order 0 outranks the authored
		// rows), cheap enough to pop fast once the mood crosses 85.
		FRHDiscoveryRow DUnlock; DUnlock.DisplayName = TEXT("Regolith Ceramics II");
		DUnlock.Order = 0; DUnlock.LabSeatHours = 50.f; DUnlock.HopeBonus = 2.f;
		DUnlock.FundingKg = 500.f; DUnlock.UnlockRoom = FName("Workshop");
		DUnlock.Alert = TEXT("BREAKTHROUGH — ceramics strong enough for machine housings.");
		DUnlock.SliceActive = true;
		DefsSub->Debug_InjectDiscovery(FName("TestCeramics"), DUnlock);
		TArray<FString> Rejects;
		const FDelegateHandle RejectH = Sim->OnCommandRejected.AddLambda([&Rejects](const FRHCommand&, const FString& S) { Rejects.Add(S); });
		// Furnish orders ride the uplink like any order: one order, one step.
		const auto Furnish = [&](const TCHAR* Row, int32 Cell, int32 Slot)
		{
			FRHCommand C; C.Verb = FName("Furnish"); C.Target = FName(Row); C.Level = -1;
			C.Location = Sim->GetCellCentreCm(FIntVector(-1, Cell, 0));
			C.Value = URHSimWorldSubsystem::EncodeFurnish(Cell, Slot, 0);
			Sim->EnqueueCommand(C);
			Step();
		};

		// A thriving colony (the -discovery recipe, plus the tier benches).
		FString R;
		Sim->ExtendShaft(1, FVector(1000.f, 1000.f, 0.f));
		Sim->ExcavateFloor(-1, 11, R); // C2a: one more Living cell (c10) - see T1; 12 would not fill in the 2.5 sols
		Sim->Debug_PlaceInstant(FName("SolarArray"), FVector(3500.f, 1000.f, 0.f));
		Sim->Debug_PlaceInstant(FName("BatteryBank"), FVector(1000.f, 3500.f, 0.f));
		Sim->Debug_PlaceInstant(FName("AirFilter"), FVector(1000.f, 1500.f, 0.f), -1);
		Sim->AddStock(FName("Oxygen"), 6000.0);
		Sim->AddStock(FName("Water"), 4000.0);
		Sim->AddStock(FName("Food"), 6000.0);
		RunSols(2.5);
		const int32 Housed = Sim->Debug_AddColonists(6);
		const bool bZoned = Sim->DesignateRoom(-1, 0, FName("LivingQuarters"), R)
			&& Sim->DesignateRoom(-1, 1, FName("WorkbenchLarge"), R)  // T2: 2 seats, x1.35 (first Workstation cell: weight 1.0)
			&& Sim->DesignateRoom(-1, 2, FName("Workstation"), R)     // T1: 1 seat, x1.0
			&& Sim->DesignateRoom(-1, 3, FName("ChemTableLarge"), R)  // T2: 1 seat, x1.35 (first Lab cell: weight 1.0)
			&& Sim->DesignateRoom(-1, 4, FName("Lab"), R)             // T1: 1 seat, x1.0
			&& Sim->DesignateRoom(-1, 5, FName("Dining"), R)
			&& Sim->DesignateRoom(-1, 8, FName("LivingQuarters"), R)
			&& Sim->DesignateRoom(-1, 9, FName("LivingQuarters"), R)   // c6, c7 stay free: Workshop, Infirmary
			&& Sim->DesignateRoom(-1, 10, FName("LivingQuarters"), R); // C2a: five seats for six people earn 6.25 of the jobs line, not 7.5; a fourth home (+1.25) makes it up
		Expect(bZoned, TEXT("T1 setup: c0, c8, c9, c10 Living, c1 Large Workbench, c2 Workstation, c3 Large Chem Table, c4 Lab, c5 Dining all accepted"));
		// Six colonists drain a comforts crate quickly - stock several so the
		// comforts fraction holds at 1.0 through the whole march.
		Sim->Debug_DeliverCargo(FName("LuxuryGoods"));
		Sim->Debug_DeliverCargo(FName("LuxuryGoods"));
		Sim->Debug_DeliverCargo(FName("LuxuryGoods"));
		// The dining room's table and plants: Furnishings 6 (140 Struct from the
		// Lander's 400).
		Furnish(TEXT("DiningTable"), 5, 0);
		for (const int32 Slot : { 2, 3, 6, 7, 8 }) { Furnish(TEXT("Plant"), 5, Slot); }
		Expect(Rejects.Num() == 0 && Near(Sim->GetColonyHope().Furnishings, 6.0, 1e-9),
			FString::Printf(TEXT("T1 six furnish orders (a dining table and five plants in c5): %d rejected, Furnishings %.1f (expect 0, 6.0)%s%s"),
				Rejects.Num(), Sim->GetColonyHope().Furnishings, Rejects.Num() ? TEXT(" - ") : TEXT(""), Rejects.Num() ? *Rejects.Last() : TEXT("")));
		RunSols(0.2);

		// T1. Seat math: 5 job seats (2+1+1+1), yield sums weigh the tiers. C2a
		// instant Hope = 50 + vault 5 + housing 7.5 x 4/6 (5.0) + rooms (2.5 + 2.5 +
		// 1.5 = 6.5) + jobs 7.5 x 5 seats / 6 people (6.25) + comforts 8 +
		// furnishings 6 = 86.75. (Before: housing 3 of 6 homes = 3.75 and jobs 5 x 1.5
		// = 7.5 gave the same 86.75; the fourth home keeps the margin over the 85
		// research gate that the per-head jobs line took away.)
		const double EvacBase = Sim->GetEffectiveEvacSols();
		Sim->Debug_SetWorkstationFabBonus(0.1);
		{
			const URHSimWorldSubsystem::FRHHopeBreakdown H = Sim->GetColonyHope();
			Expect(Housed == 6 && H.FilledSeats == 5 && Near(Sim->GetLabYieldSeatSum(), 2.35, 1e-6)
				&& Near(Sim->GetWorkstationYieldSeatSum(), 3.70, 1e-6) && Near(Sim->GetWorkstationFabMul(), 1.370, 1e-6),
				FString::Printf(TEXT("T1 seats: housed=%d filled=%d labSum=%.2f wsSum=%.2f fabMul=%.3f (expect 6, 5, 2.35, 3.70, 1.370)"),
					Housed, H.FilledSeats, Sim->GetLabYieldSeatSum(), Sim->GetWorkstationYieldSeatSum(), Sim->GetWorkstationFabMul()));
			Expect(Near(H.Total, 86.75, 1e-6) && Near(H.Housing, 5.0, 1e-9) && Near(H.Jobs, 6.25, 1e-9) && H.WaterPenalty == 0.0 && H.FoodPenalty == 0.0 && H.UnsupportedPenalty == 0.0 && H.AdjacencyPenalty == 0.0,
				FString::Printf(TEXT("T1 instant Hope %.3f = 50 + 5 + housing %.2f + rooms %.2f + jobs %.2f + comforts %.2f + furnishings %.2f, no penalties (expect 86.75)"),
					H.Total, H.Housing, H.Rooms, H.Jobs, H.Comforts, H.Furnishings));
		}

		// T2. The dormant Workshop refuses designation (locked by research).
		FString RefuseMsg;
		const bool bRefused = !Sim->DesignateRoom(-1, 6, FName("Workshop"), RefuseMsg);
		Expect(bRefused, FString::Printf(TEXT("T2 locked: workshopRefused=%d (expect 1: '%s')"), (int32)bRefused, *RefuseMsg));

		// T3. March to the breakthrough: the smoothed mood clears 85 once the
		// water and comforts hold; 2.35 yield-seats x 24 seat-h/sol clears 50
		// seat-hours fast. Funding banks; the Workshop unlocks and ACCEPTS. The
		// march is long enough that CropStrain (which also funds 500) may pop
		// too, so the pending funding is checked against the found rows, not 500.
		RunSols(12.0);
		FString AcceptMsg;
		const bool bAccepted = Sim->DesignateRoom(-1, 6, FName("Workshop"), AcceptMsg);
		double FundingSum = 0.0;
		for (const FName& Found : Sim->GetDiscoveryLog())
		{
			const FRHDiscoveryRow* Row = DefsSub->GetDiscovery(Found);
			FundingSum += Row ? (double)Row->FundingKg : 0.0;
		}
		Expect(Sim->GetDiscoveryLog().Num() >= 1 && Sim->GetDiscoveryLog()[0] == FName("TestCeramics") && bAccepted
			&& Near(Sim->GetPendingResearchFundingKg(), FundingSum, 1e-9),
			FString::Printf(TEXT("T3 unlock: found=%d first=%s pending=%.0f (expect the found rows' funding = %.0f) workshopAccepted=%d smoothed=%.1f accruing=%d progress=%.2f next=%s (expect >=1 starting TestCeramics, 1)"),
				Sim->GetDiscoveryLog().Num(), Sim->GetDiscoveryLog().Num() ? *Sim->GetDiscoveryLog()[0].ToString() : TEXT("none"),
				Sim->GetPendingResearchFundingKg(), FundingSum, (int32)bAccepted,
				Sim->GetHopeSmoothed(), (int32)Sim->IsResearchAccruing(), Sim->GetDiscoveryProgress(), *Sim->GetNextDiscovery().ToString()));

		// T4. Infirmary: designating one stretches the evac countdown by the
		// configured grace (prevention framing - more time to fix the fault).
		Sim->DesignateRoom(-1, 7, FName("Infirmary"), R);
		Expect(Near(EvacBase, 2.0, 1e-9) && Near(Sim->GetEffectiveEvacSols(), 3.0, 1e-9),
			FString::Printf(TEXT("T4 infirmary: evac %.1f -> %.1f sols (expect 2.0 -> 3.0)"), EvacBase, Sim->GetEffectiveEvacSols()));

		// T5. Save v25 round-trip: the unlock re-applies from the discovery log
		// (row flips are in-memory), seat sums re-derive, the grace holds.
		FString Err;
		const bool bSaved = Sim->SaveColony(TEXT("tierstest"), Err);
		const bool bLoaded = Sim->LoadColony(TEXT("tierstest"), Err);
		const FRHRoomRow* WShop = DefsSub->GetRoom(FName("Workshop"));
		Expect(bSaved && bLoaded && WShop && WShop->SliceActive && Near(Sim->GetLabYieldSeatSum(), 2.35, 1e-6) && Near(Sim->GetEffectiveEvacSols(), 3.0, 1e-9),
			FString::Printf(TEXT("T5 save/load v25: workshopActive=%d labSum=%.2f evac=%.1f (expect 1, 2.35, 3.0)"),
				(int32)(WShop && WShop->SliceActive), Sim->GetLabYieldSeatSum(), Sim->GetEffectiveEvacSols()));

		Sim->OnCommandRejected.Remove(RejectH);
		UE_LOG(LogRedHopeSim, Display, TEXT("=== STATION TIERS TEST END: %d failure(s) ==="), Fails);
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return Fails > 0 ? 1 : 0;
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
		const double SmoothedSupplied = Sim->GetHopeSmoothed();
		Sim->AddStock(FName("Food"), -Sim->GetStock(FName("Food")));
		RunSols(1.0);
		const int32 DisputesStarving = Moments[2];
		// The mood only falls through the starved sol (the instant sits far below
		// it), so this read is its minimum: the margin over the dispute gate.
		const double SmoothedStarved = Sim->GetHopeSmoothed();
		Sim->AddStock(FName("Food"), 4000.0);
		UE_LOG(LogRedHopeSim, Display, TEXT("ALIVE disputes: starving hour -> %d disputes (expect 2 - every beat while unmet)"),
			DisputesStarving);
		UE_LOG(LogRedHopeSim, Display, TEXT("ALIVE mood margin: smoothed Hope %.1f supplied, %.1f after the starved sol (dispute gate 45: while supplied, disputes need it to stay above)"),
			SmoothedSupplied, SmoothedStarved);

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

		// 6) Thirst is fatal: empty the tanks -> everyone on short rations -> the
		// colony is recalled gradually (C2a: four residents at ration 0 leave one
		// every half sol, near 0.55, 1.05, 1.55 and 2.0 sols; see -crew).
		Sim->AddStock(FName("Water"), -Sim->GetStock(FName("Water")));
		RunSols(0.4);
		UE_LOG(LogRedHopeSim, Display, TEXT("WATER dry 0.4 sol: unsupported=%d (expect 4 - no water)"), 4 - Supported());
		RunSols(0.9);
		UE_LOG(LogRedHopeSim, Display, TEXT("WATER dry 1.3 sols: pop=%d (expect 2 - recalled for thirst, one every half sol)"), Sim->GetPopulation());
		RunSols(1.2);
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

		// F8. An abrupt empty: everyone is on short rations that very step, so the
		// SHORT RATIONS alert owns the banner and the episode latches silently. C2a:
		// ONE alert for the home (it was one LIFE SUPPORT per colonist, four of
		// them). Refilling restores support; nobody is evacuated.
		{
			SetRunway(30.0);
			RunSteps(1);
			const int32 Food0 = CountAlerts(FoodKey), Short0 = CountAlerts(TEXT("SHORT RATIONS")), Life0 = CountAlerts(TEXT("LIFE SUPPORT"));
			SetRunway(0.0);
			RunSteps(1);
			Expect(CountAlerts(FoodKey) == Food0 && CountAlerts(TEXT("SHORT RATIONS")) == Short0 + 1 && CountAlerts(TEXT("LIFE SUPPORT")) == Life0 && Pen() == 10.0,
				FString::Printf(TEXT("F8 larder emptied in one go: food alerts %d -> %d (silent), SHORT RATIONS +%d (expect 1, one for the home), per-colonist LIFE SUPPORT +%d (expect 0), FoodPenalty %.1f"),
					Food0, CountAlerts(FoodKey), CountAlerts(TEXT("SHORT RATIONS")) - Short0, CountAlerts(TEXT("LIFE SUPPORT")) - Life0, Pen()));
			SetRunway(40.0);
			RunSteps(1);
			int32 Supported = 0;
			for (const FRHColonist& C : Sim->GetColonists()) { Supported += C.bSupported ? 1 : 0; }
			Expect(Pen() == 0.0 && Supported == 4 && Sim->GetPopulation() == 4 && CountAlerts(TEXT("RECALLED:")) == 0 && CountAlerts(TEXT("RATIONS RESTORED")) >= 1,
				FString::Printf(TEXT("F8 refilled: FoodPenalty %.1f, %d of 4 supported, pop %d, nobody recalled (%d RECALLED alerts), %d RATIONS RESTORED"), Pen(), Supported, Sim->GetPopulation(), CountAlerts(TEXT("RECALLED:")), CountAlerts(TEXT("RATIONS RESTORED"))));
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

	// D5a self-test (2026-10-17): the per-machine DAY SHIFT. A day-shift machine
	// spends only the daylight share of each step on its batch and draws idle
	// power for the rest. Pins the data (config window, defaults, the derived
	// eligibility set, the solar curve edges), the daylight helper, the shipped
	// defaults, the night rest / dusk partial / night work with the shift off /
	// manual-OFF-trumps arithmetic (exact), the presentation reads, ERA PARITY
	// (the same work hours at dt 60 s aligned, dt 1 s, and dt 60 s offset by
	// 30 s), the dawn-lag residual of an overnight shed (measured and bounded,
	// not hidden), the night the bank now survives (and its control), and the
	// save round trip with the strict v32 refusal. The clock only ever moves
	// FORWARD here. Fails loudly. `-dayshift`.
	if (FParse::Param(*Params, TEXT("dayshift")))
	{
		UE_LOG(LogRedHopeSim, Display, TEXT("=== DAY SHIFT TEST (D5a, 2026-10-17) ==="));
		int32 Fails = 0;
		const auto Expect = [&](bool bOk, const FString& What)
		{
			if (bOk) { UE_LOG(LogRedHopeSim, Display, TEXT("DAYSHIFT ok: %s"), *What); }
			else { UE_LOG(LogRedHopeSim, Error, TEXT("DAYSHIFT FAIL: %s"), *What); ++Fails; }
		};
		const auto Near = [](double A, double B, double Tol) { return FMath::Abs(A - B) <= Tol; };
		const auto Finish = [&]() -> int32
		{
			UE_LOG(LogRedHopeSim, Display, TEXT("=== DAY SHIFT TEST END: %d failure(s) ==="), Fails);
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
			return Fails > 0 ? 1 : 0;
		};
		const double SolL = URHSimClockSubsystem::SolLengthSimSeconds;
		const double Tight = 1e-9;
		URHDefinitionsSubsystem* DefsSub = World->GetSubsystem<URHDefinitionsSubsystem>();
		FString Err;

		// One era-style step: pin the bank (never shed) or leave it free.
		const auto Step = [&](double Dt, bool bPin)
		{
			if (bPin) { Sim->Debug_SetBatteryWh(1.0e9); }
			Clock->Debug_AdvanceSimSeconds(Dt);
			Sim->EraStep((float)Dt);
		};
		// Move the clock FORWARD to the next occurrence of a sol fraction (stay put
		// if already there). Never rewinds: a rewind would replay the uplink
		// ordering, the power-history ring and the quota and ship clocks.
		const auto AdvanceTo = [&](double Frac)
		{
			const double Now = Clock->GetSimSecondsTotal();
			const double Sols0 = FMath::FloorToDouble(Now / SolL);
			const double Target = Frac * SolL;
			const double T = (Sols0 + (Target < Now - Sols0 * SolL ? 1.0 : 0.0)) * SolL + Target;
			if (T > Now) { Clock->Debug_AdvanceSimSeconds(T - Now); }
		};
		const auto LastIdOf = [&](const TCHAR* Def) { int32 Id = 0; for (const FRHBuildingInstance& B : Sim->GetBuildings()) { if (B.DefName == FName(Def)) { Id = B.Id; } } return Id; };
		const auto Find = [&](int32 Id) -> const FRHBuildingInstance* { for (const FRHBuildingInstance& B : Sim->GetBuildings()) { if (B.Id == Id) { return &B; } } return nullptr; };
		const auto Place = [&](const TCHAR* Def, double X, double Y)
		{
			Sim->Debug_PlaceInstant(FName(Def), FVector(X, Y, 0.0));
			return LastIdOf(Def);
		};

		// DS0. Pure data (fail-fast on drift: every later number assumes it). The
		// config reads skip -RHConfig overrides, so only the synced DT_Config row
		// can satisfy them. The default set and the derived eligibility set are
		// exact sets over the whole building table.
		{
			const double Start = DefsSub->GetConfigTableScalar(FName("DayShiftStartFrac"), -1.0);
			const double End = DefsSub->GetConfigTableScalar(FName("DayShiftEndFrac"), -1.0);
			const double On = DefsSub->GetConfigTableScalar(FName("DayShiftDefaultsOn"), -1.0);
			TArray<FString> DefaultNames, EligibleNames;
			DefsSub->ForEachBuilding([&](FName Name, const FRHBuildingRow&)
			{
				if (DefsSub->GetConfigTableScalar(FName(*FString::Printf(TEXT("DayShiftDefault_%s"), *Name.ToString())), 0.0) >= 0.5) { DefaultNames.Add(Name.ToString()); }
				if (Sim->CanDayShift(Name)) { EligibleNames.Add(Name.ToString()); }
			});
			DefaultNames.Sort();
			EligibleNames.Sort();
			const FString Defaults = FString::Join(DefaultNames, TEXT(","));
			const FString Eligible = FString::Join(EligibleNames, TEXT(","));
			const bool bCurve = Near(DefsSub->EvalSolarCurve(0.25f), 0.0, 1e-6) && Near(DefsSub->EvalSolarCurve(0.75f), 0.0, 1e-6)
				&& Near(DefsSub->EvalSolarCurve(0.0f), 0.0, 1e-6)
				&& DefsSub->EvalSolarCurve(0.26f) > 0.f && DefsSub->EvalSolarCurve(0.74f) > 0.f && DefsSub->EvalSolarCurve(0.5f) > 0.f;
			const bool bData = Near(Start, 0.25, 1e-12) && Near(End, 0.75, 1e-12) && Near(On, 1.0, 1e-12)
				&& Near(Sim->GetDayShiftStartFrac(), Start, 1e-12) && Near(Sim->GetDayShiftEndFrac(), End, 1e-12) && Sim->GetDayShiftDefaultsOn()
				&& Defaults == TEXT("Electrolyzer,Forge") && Eligible == TEXT("Borer,Electrolyzer,Forge,IceDrill,WaterPlant") && bCurve;
			Expect(bData, FString::Printf(TEXT("DS0 RH_Config window %.2f..%.2f, defaults-on %.0f; DayShiftDefault_ rows {%s}; day-shift-capable {%s}; solar curve 0 at both edges and 0, up inside (DT/CSV)"),
				Start, End, On, *Defaults, *Eligible));
			if (!bData)
			{
				return Finish();
			}
		}

		// DS1. The daylight helper: the share of [T0, T1] in the window, wrap-safe.
		{
			const double Cases[][3] = { {240, 300, 0.0}, {300, 360, 1.0}, {270, 330, 0.5}, {870, 930, 0.5}, {900, 960, 0.0}, {0, 1200, 0.5},
				{0, 2400, 0.5}, {1500, 1560, 1.0}, {1440, 1500, 0.0}, {1140, 1560, 60.0 / 420.0}, {300, 300, 1.0}, {900, 900, 0.0}, {299.5, 299.5, 0.0} };
			bool bAll = true;
			FString Got;
			for (const auto& C : Cases)
			{
				const double G = Sim->GetDaylightFraction(C[0], C[1]);
				bAll &= Near(G, C[2], 1e-12);
				Got += FString::Printf(TEXT("[%.1f,%.1f]=%.4f "), C[0], C[1], G);
			}
			Expect(bAll, FString::Printf(TEXT("DS1 daylight share: %s(expect 0 1 .5 .5 0 .5 .5 1 0 .1429 | points 1 0 0)"), *Got));
		}

		// DS2. Defaults and eligibility on a real colony. Debug_PlaceInstant routes
		// through the same AddBuilding as the uplink Build order.
		const int32 LanderId = LastIdOf(TEXT("Lander"));
		const int32 F = Place(TEXT("Forge"), 2000, 1500);
		const int32 E = Place(TEXT("Electrolyzer"), 2000, -1500);
		const int32 W = Place(TEXT("WaterPlant"), 3000, -1500);
		const int32 Bo = Place(TEXT("Borer"), 3000, 1500);
		const int32 A = Place(TEXT("AirFilter"), 4000, 0);
		const int32 P = Place(TEXT("ChargePad"), 4000, 1500);
		const int32 K = Place(TEXT("BatteryBank"), 1000, 3500);
		const int32 M = Place(TEXT("LabModule"), 1000, -3500);
		{
			Expect(Sim->IsDayShift(F) && Sim->IsDayShift(E) && !Sim->IsDayShift(W) && !Sim->IsDayShift(Bo) && !Sim->IsDayShift(A)
				&& !Sim->IsDayShift(P) && !Sim->IsDayShift(K) && !Sim->IsDayShift(M) && !Sim->IsDayShift(LanderId),
				TEXT("DS2 a new Forge and Electrolyzer start on the day shift; a Water Plant, Borer, Air Filter, pad, bank, lab and the Lander do not"));
			Expect(Sim->CanDayShift(FName("Forge")) && Sim->CanDayShift(FName("Electrolyzer")) && Sim->CanDayShift(FName("WaterPlant"))
				&& Sim->CanDayShift(FName("IceDrill")) && Sim->CanDayShift(FName("Borer"))
				&& !Sim->CanDayShift(FName("Lander")) && !Sim->CanDayShift(FName("AirFilter")) && !Sim->CanDayShift(FName("ChargePad"))
				&& !Sim->CanDayShift(FName("BatteryBank")) && !Sim->CanDayShift(FName("LabModule")) && !Sim->CanDayShift(FName("Floodmast"))
				&& !Sim->CanDayShift(FName("SolarArray")),
				TEXT("DS2 eligibility: Forge, Electrolyzer, Water Plant, Ice Drill, Borer yes; Lander, Air Filter, pad, bank, lab, Floodmast, Solar Array never"));
			const bool bRefusedAir = !Sim->SetDayShift(A, true) && !Sim->IsDayShift(A);
			const bool bRefusedMissing = !Sim->SetDayShift(987654, true);
			const bool bAcceptedWater = Sim->SetDayShift(W, true) && Sim->IsDayShift(W);
			Expect(bRefusedAir && bRefusedMissing && bAcceptedWater,
				FString::Printf(TEXT("DS2 SetDayShift refuses the Air Filter (%d) and a missing building (%d), accepts the Water Plant (%d)"),
					bRefusedAir ? 1 : 0, bRefusedMissing ? 1 : 0, bAcceptedWater ? 1 : 0));
			// Isolate: only the Forge, the bank and the Lander draw, so the LoadW
			// arithmetic below is exact.
			for (int32 Id : { E, W, Bo, A, P, M }) { Sim->SetManualPower(Id, false); }
		}
		const FRHBuildingRow* LanderDef = DefsSub->GetBuilding(FName("Lander"));
		const FRHBuildingRow* BankDef = DefsSub->GetBuilding(FName("BatteryBank"));
		const FRHBuildingRow* ForgeDef = DefsSub->GetBuilding(FName("Forge"));
		const FRHBuildingRow* ElecDef = DefsSub->GetBuilding(FName("Electrolyzer"));
		const double IdleBase = LanderDef->PowerIdle_W + BankDef->PowerIdle_W; // 25 W: Lander + the one bank
		const double ForgeSpan = ForgeDef->PowerDraw_W - ForgeDef->PowerIdle_W;
		const FName NStruct(TEXT("Struct"));
		const auto Rem = [&](int32 Id) { const FRHBuildingInstance* B = Find(Id); return B ? B->BatchRemaining_h : -1.0; };
		const auto LoadW = [&]() { return Sim->GetPower().LoadW; };

		// DS3. Night rest: a batch running at noon pauses at night, loses nothing,
		// and the machine draws idle only.
		Sim->Debug_AddSolidAt(F, FName("Regolith"), 5000.0, false);
		AdvanceTo(0.45); // T = 540
		Step(60.0, true); // noon step [540, 600]: the whole step is daylight
		const double Rem0 = Rem(F);
		Expect(Near(Rem0, 2.0 - 1.2, Tight) && Near(LoadW(), IdleBase + ForgeDef->PowerDraw_W, Tight) && !Sim->IsDayShiftResting(F),
			FString::Printf(TEXT("DS3 noon step: a 2 h batch spent 1.2 h (left %.4f, expect 0.8); load %.1f W = idle %.0f + Forge draw %.0f; not resting"),
				Rem0, LoadW(), IdleBase, ForgeDef->PowerDraw_W));
		AdvanceTo(0.95); // T = 1140
		const double StructBeforeNight = Sim->GetTotalSolid(NStruct);
		bool bRestOk = true;
		FString RestLog;
		for (int32 i = 0; i < 3; ++i)
		{
			Step(60.0, true); // [1140,1200] [1200,1260] [1260,1320]: all night
			bRestOk &= Near(Rem(F), Rem0, Tight) && Near(LoadW(), IdleBase + ForgeDef->PowerIdle_W, Tight)
				&& Near(Sim->GetTotalSolid(NStruct), StructBeforeNight, Tight);
			RestLog += FString::Printf(TEXT("[rem %.4f load %.1f W] "), Rem(F), LoadW());
		}
		Expect(bRestOk && Sim->IsDayShiftResting(F),
			FString::Printf(TEXT("DS3 three night steps: batch exactly paused at %.4f h, nothing produced, load = idle %.0f W; resting reads true %s"),
				Rem0, IdleBase + ForgeDef->PowerIdle_W, *RestLog));

		// DS4. Dusk partial: a step half in daylight spends half its budget and
		// draws half its above-idle power.
		AdvanceTo(0.725); // T = 2070
		Step(60.0, true); // [2070, 2130] = sol 1 [870, 930]: half daylight
		Expect(Near(Rem(F), Rem0 - 0.6, Tight) && Near(LoadW(), IdleBase + ForgeDef->PowerIdle_W + ForgeSpan * 0.5, Tight),
			FString::Printf(TEXT("DS4 dusk step: batch spent 0.6 h (left %.4f, expect %.4f); load %.1f W (expect %.1f = idle + %.0f x 0.5)"),
				Rem(F), Rem0 - 0.6, LoadW(), IdleBase + ForgeDef->PowerIdle_W + ForgeSpan * 0.5, ForgeSpan));

		// DS5. The shift switched off: the same machine works through the night.
		// (Struct is measured as the colony total - era logistics moves finished
		// outputs off the Forge every step.)
		Sim->SetDayShift(F, false);
		const double StructBeforeFinish = Sim->GetTotalSolid(NStruct);
		Step(60.0, true); // [2130, 2190]: night
		Expect(Near(Sim->GetTotalSolid(NStruct) - StructBeforeFinish, 25.0, Tight) && Near(Rem(F), 1.0, Tight)
			&& Near(LoadW(), IdleBase + ForgeDef->PowerDraw_W, Tight) && !Sim->IsDayShiftResting(F),
			FString::Printf(TEXT("DS5 shift off at night: the 0.2 h batch finished (+%.1f Struct, expect 25), a new one started (left %.4f, expect 1.0), load %.1f W, not resting"),
				Sim->GetTotalSolid(NStruct) - StructBeforeFinish, Rem(F), LoadW()));

		// DS6. Manual OFF trumps the day shift: zero draw, batch frozen, even at noon.
		Sim->SetDayShift(F, true);
		Sim->SetManualPower(F, false);
		const double RemBeforeOff = Rem(F);
		AdvanceTo(0.45); // T = 2940
		Step(60.0, true); // [2940, 3000]: noon
		Expect(Near(Rem(F), RemBeforeOff, Tight) && Near(LoadW(), IdleBase, Tight) && !Sim->IsDayShiftResting(F),
			FString::Printf(TEXT("DS6 manually OFF at noon: batch frozen at %.4f h, load %.1f W (expect %.0f: the Forge is off the grid entirely)"),
				Rem(F), LoadW(), IdleBase));
		Sim->SetManualPower(F, true);

		// DS7. Era parity: the Electrolyzer (1 h batches) does the same work hours
		// per sol at dt 60 s aligned, dt 1 s, and dt 60 s offset by half a step.
		// Work = completed batches (O2 / 8.9) + the running batch's progress.
		Sim->SetManualPower(F, false);
		Sim->SetManualPower(E, true);
		Sim->SetDayShift(E, true);
		Sim->AddStock(FName("Water"), 1.0e5);
		const FName NO2(TEXT("Oxygen"));
		const FRHRecipeRow* ElecRec = DefsSub->GetRecipe(FName("Electrolysis"));
		const double ElecBatchH = ElecRec ? ElecRec->BatchTime_h : 1.0;
		const double O2PerBatch = ElecRec ? URHDefinitionsSubsystem::ParseResourceList(ElecRec->Outputs).FindRef(NO2) : 8.9;
		struct FMeter { double O2 = 0.0; double Pend = 0.0; };
		const auto Meter = [&]()
		{
			FMeter Snap;
			Snap.O2 = Sim->GetStock(NO2);
			const FRHBuildingInstance* Elec = Find(E);
			Snap.Pend = (Elec && Elec->BatchRemaining_h > 0.0) ? ElecBatchH - Elec->BatchRemaining_h : 0.0;
			return Snap;
		};
		const auto WorkBetween = [&](const FMeter& From, const FMeter& To)
		{
			return FMath::RoundToDouble((To.O2 - From.O2) / O2PerBatch) * ElecBatchH + To.Pend - From.Pend;
		};
		const double ShiftH = 24.0 * (Sim->GetDayShiftEndFrac() - Sim->GetDayShiftStartFrac()); // 12 sol-hours
		double WorkAligned = 0.0, WorkFine = 0.0, WorkOffset = 0.0;
		AdvanceTo(0.0);
		Expect(!Sim->IsDaylightNow() && Near(Clock->GetSimSecondsTotal() / SolL, FMath::RoundToDouble(Clock->GetSimSecondsTotal() / SolL), 1e-12),
			TEXT("DS7 the sol boundary is night (the shift is not running at T = k x 1200)"));
		{
			const FMeter From = Meter();
			for (int32 i = 0; i < 20; ++i) { Step(60.0, true); }
			WorkAligned = WorkBetween(From, Meter());
		}
		{
			const FMeter From = Meter();
			for (int32 i = 0; i < 1200; ++i) { Step(1.0, true); }
			WorkFine = WorkBetween(From, Meter());
		}
		{
			Clock->Debug_AdvanceSimSeconds(30.0); // an era grid offset by half a step: sunrise and sunset fall mid-step
			const FMeter From = Meter();
			for (int32 i = 0; i < 20; ++i) { Step(60.0, true); }
			WorkOffset = WorkBetween(From, Meter());
		}
		Expect(Near(WorkAligned, ShiftH, 1e-6) && Near(WorkFine, ShiftH, 1e-6) && Near(WorkOffset, ShiftH, 1e-6),
			FString::Printf(TEXT("DS7 era parity: Electrolyzer work per sol %.6f h (dt 60 aligned), %.6f h (dt 1), %.6f h (dt 60 offset 30 s); expect %.1f each"),
				WorkAligned, WorkFine, WorkOffset, ShiftH));

		// DS7d. The dawn lag, measured. Production reads the shed state the
		// previous step's power pass set, so a day-shift machine that was shed
		// overnight loses its first dawn step: a whole era step in the era band, a
		// blink in the agent band. Scenario: a drained bank all night (the bank is
		// zeroed before every night step) with a Water Plant that is NOT on the day
		// shift, so the grid sheds the resting Electrolyzer too. Eight arrays carry
		// the day. Work is measured from sunrise to sunset in each band.
		{
			for (int32 i = 0; i < 8; ++i) { Place(TEXT("SolarArray"), 5000.0 + 400.0 * i, -3000.0); }
			Sim->SetManualPower(W, true);
			Sim->SetDayShift(W, false);
			Sim->Debug_AddSolidAt(W, FName("Ice"), 1.0e5, false);
			struct FBand { double Dt = 0.0; int32 NightSteps = 0; int32 NightShed = 0; bool bLastNightShed = false; int32 NightKey = -1; int32 Latched = -1; double FirstDawnWork = 0.0; double DayWork = 0.0; };
			const auto RunBand = [&](double Dt)
			{
				FBand R;
				R.Dt = Dt;
				AdvanceTo(0.75); // dusk
				R.NightKey = Sim->GetNightIndexNow();
				const int32 NightSteps = (int32)FMath::RoundToDouble(0.5 * SolL / Dt);
				for (int32 i = 0; i < NightSteps; ++i)
				{
					Sim->Debug_SetBatteryWh(0.0); // drained
					Step(Dt, false);
					++R.NightSteps;
					R.NightShed += Sim->GetPower().ShedCount > 0 ? 1 : 0;
				}
				const FRHBuildingInstance* Eb = Find(E);
				R.bLastNightShed = Eb && !Eb->bPowered;
				R.Latched = Sim->GetLastBlackoutNight();
				const FMeter DawnFrom = Meter();
				Step(Dt, false);
				R.FirstDawnWork = WorkBetween(DawnFrom, Meter());
				const int32 DaySteps = (int32)FMath::RoundToDouble(0.5 * SolL / Dt) - 1;
				for (int32 i = 0; i < DaySteps; ++i) { Step(Dt, false); }
				R.DayWork = WorkBetween(DawnFrom, Meter());
				return R;
			};
			const FBand Era = RunBand(60.0);
			const FBand Fine = RunBand(1.0);
			const double EraLoss = 60.0 / 50.0 - Era.FirstDawnWork;
			const double FineLoss = 1.0 / 50.0 - Fine.FirstDawnWork;
			Expect(Era.NightShed == Era.NightSteps && Fine.NightShed == Fine.NightSteps && Era.bLastNightShed && Fine.bLastNightShed,
				FString::Printf(TEXT("DS7d premise: every night step shed (era %d/%d, fine %d/%d) and the resting Electrolyzer was shed on the last one (era %d, fine %d)"),
					Era.NightShed, Era.NightSteps, Fine.NightShed, Fine.NightSteps, Era.bLastNightShed ? 1 : 0, Fine.bLastNightShed ? 1 : 0));
			Expect(Era.NightKey >= 0 && Era.Latched == Era.NightKey && Fine.NightKey == Era.NightKey + 1 && Fine.Latched == Fine.NightKey,
				FString::Printf(TEXT("DS7d the guide's blackout latch names each night it shed in (era night %d latched %d, fine night %d latched %d)"),
					Era.NightKey, Era.Latched, Fine.NightKey, Fine.Latched));
			Expect(Near(EraLoss, 60.0 / 50.0, Tight) && FineLoss >= -Tight && FineLoss <= 1.0 / 50.0 + Tight,
				FString::Printf(TEXT("DS7d first-dawn-step loss: era %.3f h (the whole 1.2 h step, expect 1.200), fine %.4f h (at most one 0.02 h step)"), EraLoss, FineLoss));
			Expect(Era.DayWork <= ShiftH + 1e-6 && Fine.DayWork <= ShiftH + 1e-6 && Near(Era.DayWork, Fine.DayWork, 60.0 / 50.0 + 1e-6),
				FString::Printf(TEXT("DS7d sunrise-to-sunset work after an overnight shed: era %.3f h, fine %.3f h, gap %.3f h (documented tolerance: one era step, 1.2 h); an unshed shift is %.1f h"),
					Era.DayWork, Fine.DayWork, FMath::Abs(Era.DayWork - Fine.DayWork), ShiftH));
		}

		// DS7e. In-game burst parity. The clock publishes up to four era steps in
		// ONE frame (a hitch) and has already advanced the whole frame, so every
		// step would read the same end time. Sim Tick gives each step its own
		// window end, so a burst that straddles sunset or sunrise still works
		// exactly the daylight it spans: 90 s of daylight is 1.8 sol-hours at
		// dusk, 150 s is 3.0 at dawn. (Without the per-step end all four steps read
		// the frame's last window: 0 h at dusk, 4.8 h at dawn.) Driven through the
		// real clock Tick and sim Tick at the era speed ceiling.
		{
			Sim->SetManualPower(W, false);
			Step(60.0, true); // settle: power flags and batches
			const auto BurstWork = [&](double SunEdgeFrac, double SecondsBefore, int32& OutPublished)
			{
				AdvanceTo(SunEdgeFrac - SecondsBefore / SolL);
				Sim->Debug_SetBatteryWh(Sim->GetPower().BatteryCapWh);
				const FMeter From = Meter();
				Clock->SetSpeed(64.f);
				Clock->Tick(3.75f); // 3.75 s x 64 = 240 sim-s: four whole era steps in one frame
				OutPublished = Clock->GetEraStepsThisFrame();
				Sim->Tick(3.75f);
				Clock->SetSpeed(1.f);
				return WorkBetween(From, Meter());
			};
			int32 PubDusk = 0, PubDawn = 0;
			const double WorkDusk = BurstWork(0.75, 90.0, PubDusk);
			const double WorkDawn = BurstWork(0.25, 90.0, PubDawn);
			Expect(PubDusk == 4 && PubDawn == 4 && Near(WorkDusk, 90.0 / 50.0, 1e-6) && Near(WorkDawn, 150.0 / 50.0, 1e-6),
				FString::Printf(TEXT("DS7e one-frame burst of %d/%d era steps across the sun edge: %.6f h of work at sunset (90 s of daylight, expect 1.8), %.6f h at sunrise (150 s, expect 3.0)"),
					PubDusk, PubDawn, WorkDusk, WorkDawn));
		}

		// DS8. The bank through the night, run with the real bank (no pin): both
		// heavy machines on the day shift, then the same night with the shift off.
		{
			Sim->SetManualPower(F, true);
			Sim->SetManualPower(E, true);
			Sim->SetManualPower(W, false);
			Sim->SetDayShift(F, true);
			Sim->SetDayShift(E, true);
			Sim->Debug_AddSolidAt(F, FName("Regolith"), 5000.0, false);
			AdvanceTo(0.5);
			for (int32 i = 0; i < 5; ++i) { Step(60.0, true); } // noon to dusk, pinned: both machines are mid-batch
			const double CapWh = LanderDef->StorageWh + BankDef->StorageWh;
			const double ShiftIdleW = IdleBase + ForgeDef->PowerIdle_W + ElecDef->PowerIdle_W;
			const double FullW = IdleBase + ForgeDef->PowerDraw_W + ElecDef->PowerDraw_W;
			const double GenNightW = LanderDef->PowerGenBase_W;
			int32 ShedFlips = 0, LatchChanges = 0, LatchEnd = -1;
			const auto Night = [&](int32& OutShedSteps, double& OutMinWh, double& OutFirstLoadW)
			{
				Sim->Debug_SetBatteryWh(CapWh);
				OutShedSteps = 0;
				OutMinWh = 1.0e12;
				ShedFlips = 0;
				LatchChanges = 0;
				bool bPrevShed = false;
				int32 PrevLatch = Sim->GetLastBlackoutNight();
				for (int32 i = 0; i < 10; ++i)
				{
					Step(60.0, false); // unpinned: the bank is the only buffer
					const bool bShed = Sim->GetPower().ShedCount > 0;
					OutShedSteps += bShed ? 1 : 0;
					ShedFlips += (i > 0 && bShed != bPrevShed) ? 1 : 0;
					bPrevShed = bShed;
					LatchChanges += Sim->GetLastBlackoutNight() != PrevLatch ? 1 : 0;
					PrevLatch = Sim->GetLastBlackoutNight();
					OutMinWh = FMath::Min(OutMinWh, Sim->GetPower().BatteryWh);
					if (i == 0) { OutFirstLoadW = LoadW(); }
				}
				LatchEnd = PrevLatch;
			};
			const double NeedWh = (FullW - GenNightW) * 12.0; // 600 s = 12 sol-hours
			Expect(Near(Sim->GetPower().BatteryCapWh, CapWh, 1e-6) && Near(Clock->GetSimSecondsTotal() - FMath::FloorToDouble(Clock->GetSimSecondsTotal() / SolL) * SolL, 0.75 * SolL, 1e-6),
				FString::Printf(TEXT("DS8 setup: dusk, bank capacity %.0f Wh (expect Lander + one bank = %.0f)"), Sim->GetPower().BatteryCapWh, CapWh));
			int32 ShedA = 0;
			double MinA = 0.0, FirstLoadA = 0.0;
			const int32 KeyA = Sim->GetNightIndexNow();
			const int32 LatchBeforeA = Sim->GetLastBlackoutNight();
			Night(ShedA, MinA, FirstLoadA);
			const int32 LatchAfterA = Sim->GetLastBlackoutNight();
			const double FinalA = Sim->GetPower().BatteryWh;
			const double ExpectA = CapWh + (GenNightW - ShiftIdleW) * 12.0;
			Expect(ShedA == 0 && MinA > 0.0 && Near(FirstLoadA, ShiftIdleW, Tight) && Near(FinalA, ExpectA, 1e-6) && LatchAfterA == LatchBeforeA && LatchAfterA != KeyA,
				FString::Printf(TEXT("DS8 on the day shift: ten night steps, %d shed; load %.0f W (all resting); bank %.0f Wh at dawn (expect %.0f = %.0f + (%.0f gen - %.0f idle) x 12 h); min %.0f"),
					ShedA, FirstLoadA, FinalA, ExpectA, CapWh, GenNightW, ShiftIdleW, MinA));
			for (int32 i = 0; i < 10; ++i) { Step(60.0, true); } // the day: both machines pick their batches back up
			Sim->SetDayShift(F, false);
			Sim->SetDayShift(E, false);
			int32 ShedB = 0;
			double MinB = 0.0, FirstLoadB = 0.0;
			const int32 KeyB = Sim->GetNightIndexNow();
			Night(ShedB, MinB, FirstLoadB);
			Expect(ShedB > 0 || MinB <= 0.0,
				FString::Printf(TEXT("DS8 control, shift off: the same night needs about %.0f Wh (%.0f W x 12 h) against %.0f Wh stored: %d shed step(s), bank minimum %.0f Wh, first-step load %.0f W"),
					NeedWh, FullW - GenNightW, CapWh, ShedB, MinB, FirstLoadB));
			// The guide must not strobe: the shed flag alternates through the night
			// while the bank scrapes empty (it reads 0 or 1 on successive steps), and
			// the latch the guide keys on changes exactly once - to this night - and
			// then holds to sunrise.
			Expect(ShedB > 0 && ShedFlips >= 1 && LatchChanges == 1 && LatchEnd == KeyB && LatchEnd != KeyA,
				FString::Printf(TEXT("DS8 control: the shed flag flipped %d time(s) across the night but the blackout latch changed %d time(s), ending on night %d (this night %d, the quiet night was %d)"),
					ShedFlips, LatchChanges, LatchEnd, KeyB, KeyA));
		}

		// DS9. The presentation reads: how many machines are resting, and the
		// night key the guide latches a blackout to.
		{
			Sim->SetManualPower(F, true);
			Sim->SetManualPower(E, true);
			Sim->SetDayShift(F, true);
			Sim->SetDayShift(E, true);
			AdvanceTo(0.95);
			const bool bNight = !Sim->IsDaylightNow();
			const int32 Count = Sim->GetDayShiftRestingCount();
			const int32 NightKey = Sim->GetNightIndexNow();
			Sim->SetManualPower(F, false);
			const int32 CountOff = Sim->GetDayShiftRestingCount();
			const bool bOffNotResting = !Sim->IsDayShiftResting(F) && Sim->IsDayShiftResting(E);
			Sim->SetManualPower(F, true);
			Expect(bNight && Count == 2 && CountOff == 1 && bOffNotResting,
				FString::Printf(TEXT("DS9 at midnight: %d machine(s) resting (expect 2: Forge + Electrolyzer); with the Forge manually off %d (expect 1)"), Count, CountOff));
			AdvanceTo(0.2495);
			const bool bBeforeDawn = !Sim->IsDaylightNow() && Sim->GetNightIndexNow() == NightKey;
			AdvanceTo(0.25);
			const bool bDawn = Sim->IsDaylightNow() && Sim->GetDayShiftRestingCount() == 0;
			AdvanceTo(0.5);
			const bool bNoon = Sim->IsDaylightNow() && Sim->GetDayShiftRestingCount() == 0 && !Sim->IsDayShiftResting(F);
			AdvanceTo(0.75);
			const bool bNextNight = !Sim->IsDaylightNow() && Sim->GetNightIndexNow() == NightKey + 1;
			Expect(bBeforeDawn && bDawn && bNoon && bNextNight,
				FString::Printf(TEXT("DS9 the night key holds to the last second before sunrise (%d), sunrise is daylight with 0 resting (%d), noon 0 resting (%d), and sunset starts the next night key %d -> %d (%d)"),
					bBeforeDawn ? 1 : 0, bDawn ? 1 : 0, bNoon ? 1 : 0, NightKey, Sim->GetNightIndexNow(), bNextNight ? 1 : 0));
		}

		// DS10. Save round trip. An explicit pre-save state, every mutable flag
		// flipped, then the load must restore the pre-save values exactly.
		{
			Sim->SetDayShift(F, true);
			Sim->SetDayShift(E, false);
			Sim->SetDayShift(W, true);
			const int32 BuildingsBefore = Sim->GetBuildings().Num();
			const bool bSaved = Sim->SaveColony(TEXT("dayshifttest"), Err);
			Sim->SetDayShift(F, false);
			Sim->SetDayShift(E, true);
			Sim->SetDayShift(W, false);
			const bool bMutated = !Sim->IsDayShift(F) && Sim->IsDayShift(E) && !Sim->IsDayShift(W);
			FString LoadErr;
			const bool bLoaded = Sim->LoadColony(TEXT("dayshifttest"), LoadErr);
			Expect(bSaved && bMutated && bLoaded && LoadErr.IsEmpty() && Sim->IsDayShift(F) && !Sim->IsDayShift(E) && Sim->IsDayShift(W) && !Sim->IsDayShift(A)
				&& Sim->GetBuildings().Num() == BuildingsBefore,
				FString::Printf(TEXT("DS10 save v33 round trip (saved %d, loaded %d): Forge %d Electrolyzer %d Water Plant %d Air Filter %d (expect 1 0 1 0), %d buildings %s"),
					bSaved ? 1 : 0, bLoaded ? 1 : 0, Sim->IsDayShift(F) ? 1 : 0, Sim->IsDayShift(E) ? 1 : 0, Sim->IsDayShift(W) ? 1 : 0, Sim->IsDayShift(A) ? 1 : 0,
					Sim->GetBuildings().Num(), *LoadErr));

			// The strict rule stands: the same file stamped as v32 (no day-shift
			// byte per building) is refused loudly and the live colony is untouched.
			const FString SlotPath = FPaths::ProjectSavedDir() / TEXT("SaveGames") / TEXT("RH_dayshifttest.sav");
			const FString OldPath = FPaths::ProjectSavedDir() / TEXT("SaveGames") / TEXT("RH_dayshifttest32.sav");
			TArray<uint8> Bytes;
			const bool bRead = FFileHelper::LoadFileToArray(Bytes, *SlotPath) && Bytes.Num() > 8;
			bool bRefused = false;
			if (bRead)
			{
				Bytes[4] = 32; Bytes[5] = 0; Bytes[6] = 0; Bytes[7] = 0; // header: magic u32, then version u32
				FFileHelper::SaveArrayToFile(Bytes, *OldPath);
				FString OldErr;
				bRefused = !Sim->LoadColony(TEXT("dayshifttest32"), OldErr) && OldErr.Contains(TEXT("Save format mismatch"));
				IFileManager::Get().Delete(*OldPath);
			}
			Expect(bRead && bRefused && Sim->IsDayShift(F) && !Sim->IsDayShift(E) && Sim->IsDayShift(W) && Sim->GetBuildings().Num() == BuildingsBefore,
				FString::Printf(TEXT("DS10 a v32-stamped save is refused (read %d, refused %d); the live flags are untouched"), bRead ? 1 : 0, bRefused ? 1 : 0));
		}

		return Finish();
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

	// C2a self-test (living-city gate 2a, 2026-10-18): HOPE PER HEAD, SHARED
	// RATIONING, ONE ALERT PER EVENT. `-percapita`. Fails loudly.
	// The fixture is one deep shaft (five floors of 200 cells) holding a colony
	// that grows 10 -> 100 -> 1,000 -> 5,000 in units of ten people. Each unit is
	// one Living cell and one Lab cell (the legacy rules: a bed per cell at
	// ColonistsPerCell, a seat per Lab cell), so every share is the same at every
	// size: a tenth of the colony's cells are homes, a tenth are work posts, and
	// one Lab room serves 25 residents ten times over. The checks:
	//  (a) the same proportions of support, jobs and rooms give the same Hope at
	//      N = 10, 100, 1,000 and 5,000 (term by term), and each term follows its
	//      formula: Rooms counted once colony-wide with its coverage, Jobs capped
	//      at the employment rate, Adjacency by refuser cells (not pairs),
	//      Furnishings per resident;
	//  (b) a shortage is shared evenly: every resident of a home gets the same
	//      ration, food and water by colony, air by home, ration = the minimum;
	//  (c) departures follow a linear law: a home recalls the residents it had
	//      when the shortage began x (1 - r) x sols / EvacSols, at a steady rate
	//      (a total failure empties it in EvacSols, half a ration takes twice as
	//      long), the newest non-Mars-born go first and the Mars-born last; the
	//      accumulator is linear in dt (step size does not matter) and the
	//      Infirmary stretches it;
	//  (d) one alert per event, counted at OnAlert;
	//  (e) save / load holds the accumulators, and the run after a load is the
	//      run that would have happened without it.
	if (FParse::Param(*Params, TEXT("percapita")))
	{
		UE_LOG(LogRedHopeSim, Display, TEXT("=== PER-CAPITA TEST (C2a, 2026-10-18) ==="));
		int32 Fails = 0, Checks = 0;
		const auto Expect = [&](bool bOk, const FString& What)
		{
			++Checks;
			if (bOk) { UE_LOG(LogRedHopeSim, Display, TEXT("PCAP ok: %s"), *What); }
			else { UE_LOG(LogRedHopeSim, Error, TEXT("PCAP FAIL: %s"), *What); ++Fails; }
		};
		const auto Near = [](double A, double B, double Tol) { return FMath::Abs(A - B) <= Tol; };
		constexpr double EraDt = (double)URHSimClockSubsystem::EraStepSimSeconds;
		constexpr double AgentDt = (double)URHSimClockSubsystem::SubStepSeconds;
		constexpr double SolLen = (double)URHSimClockSubsystem::SolLengthSimSeconds;
		constexpr int32 Floors = 5;
		constexpr int32 CellsPerFloor = 200; // leak (200 x 2 kg/sol) stays under the trunk's push: the floors stay rated
		const FName NFood(TEXT("Food")), NWater(TEXT("Water")), NOxygen(TEXT("Oxygen")), NStruct(TEXT("Struct"));
		URHDefinitionsSubsystem* DefsSub = World->GetSubsystem<URHDefinitionsSubsystem>();
		using FHope = URHSimWorldSubsystem::FRHHopeBreakdown;
		FString R;

		// P0. Pure data: every Hope weight rides the synced DT_Config. The values
		// are read from the table row alone (no override, no C++ fallback), so a
		// DT that drifted from the CSV fails here and not silently.
		const TPair<const TCHAR*, double> Pins[] = {
			{ TEXT("HopeHousingMax"), 7.5 }, { TEXT("HopePerMoralePoint"), 2.5 }, { TEXT("HopeJobsMax"), 7.5 },
			{ TEXT("HopeUnsupportedMax"), 30.0 }, { TEXT("HopeUnsupportedSteepness"), 3.0 }, { TEXT("HopeAdjacencyMax"), 16.0 },
			{ TEXT("RoomServesResidents"), 25.0 }, { TEXT("ComfortRefPop"), 8.0 } };
		FString PinText;
		bool bPins = true;
		for (const TPair<const TCHAR*, double>& Pin : Pins)
		{
			const double Got = DefsSub ? DefsSub->GetConfigTableScalar(FName(Pin.Key), -1.0) : -1.0;
			bPins &= Near(Got, Pin.Value, 1e-9);
			PinText += FString::Printf(TEXT(" %s=%g"), Pin.Key, Got);
		}
		Expect(bPins, FString::Printf(TEXT("P0 DT_Config pins (HopeHousingMax 7.5, HopePerMoralePoint 2.5, HopeJobsMax 7.5, HopeUnsupportedMax 30, steepness 3, HopeAdjacencyMax 16, RoomServesResidents 25, ComfortRefPop 8):%s"), *PinText));
		Expect(Near(Sim->GetHopeJobsMax(), 7.5, 1e-9) && Near(Sim->GetHopeUnsupportedMax(), 30.0, 1e-9) && Near(Sim->GetHopeUnsupportedSteepness(), 3.0, 1e-9)
			&& Near(Sim->GetHopeAdjacencyMax(), 16.0, 1e-9) && Near(Sim->GetRoomServesResidents(), 25.0, 1e-9) && Near(Sim->GetComfortRefPop(), 8.0, 1e-9),
			TEXT("P0 the sim reads the same weights (no override or fallback in play)"));
		const FRHRoomRow* LabRow = DefsSub ? DefsSub->GetRoom(FName("Lab")) : nullptr;
		const FRHRoomRow* LivingRow = DefsSub ? DefsSub->GetRoom(FName("LivingQuarters")) : nullptr;
		if (!bPins || !LabRow || !LabRow->SliceActive || !LivingRow || !LivingRow->SliceActive)
		{
			UE_LOG(LogRedHopeSim, Error, TEXT("PCAP FAIL: the data is not in place (pins %d, Lab row %d, Living row %d); the suite stops"), (int32)bPins, LabRow != nullptr, LivingRow != nullptr);
			UE_LOG(LogRedHopeSim, Display, TEXT("=== PER-CAPITA TEST END: %d check(s), %d failure(s) ==="), Checks, Fails + 1);
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
			return 1;
		}
		const double LabWeight = LabRow->MoraleWeight;
		const double HousingMax = Sim->GetHopeHousingMax(), PerMorale = Sim->GetHopePerMoralePoint(), JobsMax = Sim->GetHopeJobsMax();
		const double UnsuppMax = Sim->GetHopeUnsupportedMax(), Steep = Sim->GetHopeUnsupportedSteepness(), AdjMax = Sim->GetHopeAdjacencyMax();
		const double Serves = Sim->GetRoomServesResidents(), RefPop = Sim->GetComfortRefPop();
		const double EvacSols = Sim->GetColonistEvacSols();
		const double FoodPerSol = Sim->GetColonistFoodKgPerSol(), O2PerSol = Sim->GetColonistO2KgPerSol();

		// Alerts, counted where the player's banner would hear them.
		TArray<FString> Alerts;
		const FDelegateHandle AlertH = Sim->OnAlert.AddLambda([&Alerts](const FString& S) { Alerts.Add(S); });
		const auto CountAlerts = [&Alerts](const TCHAR* Prefix) { int32 N = 0; for (const FString& A : Alerts) { N += A.StartsWith(Prefix) ? 1 : 0; } return N; };
		TArray<FString> Rejects;
		const FDelegateHandle RejectH = Sim->OnCommandRejected.AddLambda([&Rejects](const FRHCommand&, const FString& S) { Rejects.Add(S); });

		const auto TopUp = [&](double Dt)
		{
			Sim->Debug_AddFreshWater(500.0 * Dt / SolLen);
			Sim->Debug_SetBatteryWh(1.0e9);
		};
		const auto EraStepOnce = [&]()
		{
			TopUp(EraDt);
			Clock->Debug_AdvanceSimSeconds(EraDt);
			Sim->EraStep((float)EraDt);
		};
		// Emptied first, then filled: from 1e8 kg a single "add the difference" would
		// round away the fraction of a kilogram a small colony's ration is made of.
		const auto SetStock = [&](FName Res, double Kg) { Sim->AddStock(Res, -Sim->GetStock(Res)); Sim->AddStock(Res, Kg); };
		const auto Cell = [&](int32 Index) { return FIntVector(-(Index / CellsPerFloor + 1), Index % CellsPerFloor, 0); }; // flat cell -> (level, index)
		const auto Zone = [&](int32 Flat, FName Room)
		{
			const FIntVector K = Cell(Flat);
			if (!Sim->DesignateRoom(K.X, K.Y, Room, R)) { UE_LOG(LogRedHopeSim, Error, TEXT("PCAP: zoning floor %d cell %d as %s failed: %s"), K.X, K.Y, *Room.ToString(), *R); ++Fails; }
		};
		const auto ZoneAt = [&](int32 Level, int32 Index, FName Room)
		{
			if (!Sim->DesignateRoom(Level, Index, Room, R)) { UE_LOG(LogRedHopeSim, Error, TEXT("PCAP: zoning floor %d cell %d as %s failed: %s"), Level, Index, *Room.ToString(), *R); ++Fails; }
		};
		int32 LanderId = 0;
		for (const FRHBuildingInstance& B : Sim->GetBuildings()) { if (B.DefName == FName("Lander")) { LanderId = B.Id; } }

		// Setup: power, a deep shaft, big floors, an air filter on each, every stock deep.
		Sim->Debug_PlaceInstant(FName("SolarArray"), FVector(3500.0, -1000.0, 0.0));
		Sim->Debug_PlaceInstant(FName("SolarArray"), FVector(3500.0, -2000.0, 0.0));
		Sim->AddStock(NOxygen, 1.0e8);
		Sim->AddStock(NWater, 1.0e8);
		Sim->AddStock(NFood, 1.0e8);
		Sim->Debug_AddSolidAt(LanderId, NStruct, 1.0e6, false);
		const FVector Head(3000.0, 3000.0, 0.0);
		Sim->ExtendShaft(Floors, Head);
		bool bCarved = true;
		for (int32 L = 1; L <= Floors; ++L) { bCarved &= Sim->ExcavateFloor(-L, CellsPerFloor, R); }
		for (int32 L = 1; L <= Floors; ++L) { Sim->Debug_PlaceInstant(FName("AirFilter"), Head, -L); }
		for (int32 L = 1; L <= Floors; ++L) { Sim->Debug_ForceRateFloor(-L); }
		Sim->Debug_SetColonistsPerCell(5.0); // a floor houses 1,000 without furnishing a bed
		EraStepOnce();
		Expect(bCarved && Sim->GetShaftDepth() == Floors && Sim->IsFloorRated(-1) && Sim->IsFloorRated(-Floors) && Sim->GetPopulation() == 0 && Sim->GetHousingCapacity() == 5000,
			FString::Printf(TEXT("setup: shaft to -%d, %d cells a floor, every floor rated, %d beds, nobody yet"), Floors, CellsPerFloor, Sim->GetHousingCapacity()));

		// (a) The ladder.
		const int32 Ladder[] = { 10, 100, 1000, 5000 };
		TArray<FHope> Steady, Short;
		int32 ZonedUnits = 0;
		const auto FoodNeedAt = [&](double Dt) { return FoodPerSol * (Dt / SolLen); };
		for (const int32 N : Ladder)
		{
			const int32 Units = N / 10;
			for (; ZonedUnits < Units; ++ZonedUnits)
			{
				Zone(2 * ZonedUnits, FName("LivingQuarters"));
				Zone(2 * ZonedUnits + 1, FName("Lab"));
			}
			const int32 Housed = Sim->Debug_AddColonists(N - Sim->GetPopulation());
			EraStepOnce();
			EraStepOnce();
			const FHope H = Sim->GetColonyHope();
			Steady.Add(H);
			Expect(Housed > 0 && Sim->GetPopulation() == N && H.Population == N,
				FString::Printf(TEXT("a N=%d: %d colonists housed, population %d"), N, Housed, Sim->GetPopulation()));
			Expect(Near(H.Housing, HousingMax * 0.1, 1e-9) && Near(H.Rooms, LabWeight * PerMorale, 1e-9) && H.FilledSeats == Units && Near(H.Jobs, JobsMax * 0.1, 1e-9)
				&& H.AdjacencyPenalty == 0.0 && H.UnsupportedPenalty == 0.0 && H.WaterPenalty == 0.0 && H.FoodPenalty == 0.0,
				FString::Printf(TEXT("a N=%d, a tenth housed and a tenth at work, Labs cover the colony: housing %.4f, rooms %.4f (the Lab bonus, once - %d Lab cells), jobs %.4f (%d seats), no adjacency / unsupported / water / food term"),
					N, H.Housing, H.Rooms, Units, H.Jobs, H.FilledSeats));
			Expect(Near(H.Total, 50.0 + H.Milestones + H.Housing + H.Rooms + H.Jobs, 1e-9) && Near(H.Total, Steady[0].Total, 1e-9),
				FString::Printf(TEXT("a N=%d: Hope %.6f = base 50 + milestones %.2f + the three terms, the same as at N=%d (%.6f)"), N, H.Total, H.Milestones, Ladder[0], Steady[0].Total));

			// The same share of the colony (a fifth) on short rations: the food pool
			// runs 20% short, so every home's ration is 0.8.
			SetStock(NFood, 0.8 * N * FoodNeedAt(AgentDt));
			Sim->Debug_StepPopulation(URHSimClockSubsystem::SubStepSeconds);
			const FHope HS = Sim->GetColonyHope();
			Short.Add(HS);
			bool bNoneSupported = true;
			for (const FRHColonist& C : Sim->GetColonists()) { bNoneSupported &= !C.bSupported; }
			Expect(bNoneSupported && Sim->GetPopulation() == N && Near(HS.DeficitEquivalent, 0.2 * N, 1e-6 * N)
				&& Near(HS.UnsupportedPenalty, UnsuppMax * FMath::Min(1.0, Steep * 0.2), 1e-6) && Near(HS.Total, Short[0].Total, 1e-6),
				FString::Printf(TEXT("a N=%d, a fifth short (ration 0.8 for all): deficit %.2f of %d, unsupported -%.4f (expect %.1f = 30 x min(1, 3 x 0.2)), Hope %.6f (the same as at N=%d: %.6f), nobody left yet"),
					N, HS.DeficitEquivalent, N, HS.UnsupportedPenalty, UnsuppMax * FMath::Min(1.0, Steep * 0.2), HS.Total, Ladder[0], Short[0].Total));
			SetStock(NFood, 1.0e8);
			Sim->Debug_StepPopulation(URHSimClockSubsystem::SubStepSeconds);
			bool bAllFed = true;
			for (const FRHColonist& C : Sim->GetColonists()) { bAllFed &= C.bSupported; }
			Expect(bAllFed && Near(Sim->GetDeficitEquivalent(), 0.0, 1e-12) && Sim->GetHomeDepartureAcc(-1) == 0.0,
				FString::Printf(TEXT("a N=%d: food restored, everyone back on a full ration, the accumulators reset (acc %.6f)"), N, Sim->GetHomeDepartureAcc(-1)));

			if (N == 10)
			{
				// Jobs: seats beyond the colony add nothing. Eleven more Lab cells on
				// the empty floor -5 make 12 seats for 10 people.
				for (int32 K = 0; K < 11; ++K) { ZoneAt(-5, 110 + K, FName("Lab")); }
				EraStepOnce();
				const FHope HJ = Sim->GetColonyHope();
				Expect(HJ.FilledSeats == 10 && Near(HJ.Jobs, JobsMax, 1e-9),
					FString::Printf(TEXT("jobs cap: 12 seats for 10 people fill 10; jobs %.4f = HopeJobsMax %.1f (it was 1.5 a seat, uncapped)"), HJ.Jobs, JobsMax));
				for (int32 K = 0; K < 11; ++K) { ZoneAt(-5, 110 + K, NAME_None); }

				// Linear in dt: one era step on empty rations and 600 agent steps move a
				// home's accumulator the same distance (nobody leaves yet: 0.25 < 1).
				SetStock(NFood, 0.0);
				Sim->Debug_StepPopulation((float)EraDt);
				const double AccEra = Sim->GetHomeDepartureAcc(-1);
				Expect(Near(AccEra, N * 1.0 * EraDt / (EvacSols * SolLen), 1e-12) && Sim->GetPopulation() == N,
					FString::Printf(TEXT("c one 60 s step at ration 0: the home's clock reads %.6f (expect %d x 60 / (%.1f x 1200) = %.6f), nobody gone"), AccEra, N, EvacSols, N * EraDt / (EvacSols * SolLen)));
				SetStock(NFood, 1.0e8);
				Sim->Debug_StepPopulation((float)AgentDt);
				for (int32 K = 0; K < 600; ++K)
				{
					SetStock(NFood, 0.0);
					Sim->Debug_StepPopulation(URHSimClockSubsystem::SubStepSeconds);
				}
				Expect(Near(Sim->GetHomeDepartureAcc(-1), AccEra, 1e-6) && Sim->GetPopulation() == N,
					FString::Printf(TEXT("c 600 steps of 0.1 s: %.6f against one 60 s step %.6f - the clock does not depend on the step size"), Sim->GetHomeDepartureAcc(-1), AccEra));
				SetStock(NFood, 1.0e8);
				Sim->Debug_StepPopulation(URHSimClockSubsystem::SubStepSeconds);

				// The Infirmary stretches the clock: 2 sols becomes 3.
				ZoneAt(-5, 130, FName("Infirmary"));
				SetStock(NFood, 0.0);
				Sim->Debug_StepPopulation((float)EraDt);
				Expect(Near(Sim->GetEffectiveEvacSols(), EvacSols + 1.0, 1e-9) && Near(Sim->GetHomeDepartureAcc(-1), N * 1.0 * EraDt / ((EvacSols + 1.0) * SolLen), 1e-12),
					FString::Printf(TEXT("c with an Infirmary online: %.1f sols, the clock reads %.6f (expect %.6f)"), Sim->GetEffectiveEvacSols(), Sim->GetHomeDepartureAcc(-1), N * EraDt / ((EvacSols + 1.0) * SolLen)));
				SetStock(NFood, 1.0e8);
				Sim->Debug_StepPopulation(URHSimClockSubsystem::SubStepSeconds);
				ZoneAt(-5, 130, NAME_None);
			}
			if (N == 10 || N == 1000)
			{
				// Adjacency counts refuser CELLS, not pairs: one Living cell between two
				// kitchens is two offended pairs and one offended cell, out of every
				// refuser cell in the colony.
				ZoneAt(-5, 100, FName("Cooking"));
				ZoneAt(-5, 101, FName("LivingQuarters"));
				ZoneAt(-5, 102, FName("Cooking"));
				const FHope HA = Sim->GetColonyHope();
				const double AdjExpect = AdjMax * 1.0 / (Units + 1);
				Expect(HA.OffendedPairs == 2 && HA.OffendedRefusers == 1 && HA.RefuserCells == Units + 1 && Near(HA.AdjacencyPenalty, AdjExpect, 1e-9),
					FString::Printf(TEXT("adjacency N=%d: %d pairs, %d offended cell of %d refuser cells, penalty %.4f (expect 16 x 1 / %d = %.4f; the old rule was 8 a pair = 16)"),
						N, HA.OffendedPairs, HA.OffendedRefusers, HA.RefuserCells, HA.AdjacencyPenalty, Units + 1, AdjExpect));
				ZoneAt(-5, 100, NAME_None); ZoneAt(-5, 101, NAME_None); ZoneAt(-5, 102, NAME_None);
			}
			if (N == 1000)
			{
				// Rooms: the Lab bonus is counted once and scaled by min(1, rooms x 25 / people).
				const auto LabCount = [&](int32 K)
				{
					for (int32 I = 0; I < Units; ++I) { ZoneAt(-1, 2 * I + 1, I < K ? FName("Lab") : NAME_None); }
				};
				bool bCover = true;
				FString CoverText;
				for (const int32 K : { 100, 40, 39, 20, 1 })
				{
					LabCount(K);
					const double Cover = FMath::Min(1.0, K * Serves / N);
					const double Rooms = Sim->GetColonyHope().Rooms;
					bCover &= Near(Rooms, LabWeight * PerMorale * Cover, 1e-9);
					CoverText += FString::Printf(TEXT(" %d rooms -> %.4f (coverage %.3f);"), K, Rooms, Cover);
				}
				LabCount(Units);
				Expect(bCover && Near(Sim->GetColonyHope().Rooms, LabWeight * PerMorale, 1e-9),
					FString::Printf(TEXT("rooms coverage at N=1000 (a Lab room serves 25): the Lab bonus %.2f x coverage:%s restored to %.4f"), LabWeight * PerMorale, *CoverText, Sim->GetColonyHope().Rooms));
			}
		}
		Expect(Sim->GetBirthsOnMars() == 0 && Sim->GetPopulation() == 5000, TEXT("no births muddied the headcount"));

		// Furnishings are per resident: four sofas (2 Hope each) in a Living cell.
		{
			const FIntVector K5(-5, 150, 0); // flat cell 950: an even cell, a Living cell
			int32 Queued = 0;
			for (const int32 Slot : { 0, 2, 8, 10 })
			{
				FRHCommand Order; Order.Verb = FName("Furnish"); Order.Target = FName("Sofa"); Order.Level = K5.X;
				Order.Location = Sim->GetCellCentreCm(K5);
				Order.Value = URHSimWorldSubsystem::EncodeFurnish(K5.Y, Slot, 0);
				Sim->EnqueueCommand(Order);
				++Queued;
			}
			EraStepOnce();
			const FHope HF = Sim->GetColonyHope();
			const double PerHead = RefPop / 5000.0;
			Expect(Rejects.Num() == 0 && Sim->GetFurniture().Num() == Queued && Near(HF.Furnishings, FMath::Min(Sim->GetHopeFurnishingsMax(), 8.0 * PerHead), 1e-9),
				FString::Printf(TEXT("furnishings per resident: %d sofas = 8 Hope raw, x %.0f / 5000 = %.5f at N=5000 (cap %.0f), %d orders refused"),
					Sim->GetFurniture().Num(), RefPop, HF.Furnishings, Sim->GetHopeFurnishingsMax(), Rejects.Num()));
		}

		// Homes, for (b)-(e): five floors of a thousand.
		struct FFloor { int32 Level = 0; TArray<int32> Ids; };
		const auto Roster = [&]() -> TArray<FFloor>
		{
			TArray<FFloor> Out;
			for (int32 L = 1; L <= Floors; ++L) { FFloor F; F.Level = -L; Out.Add(F); }
			for (const FRHColonist& C : Sim->GetColonists()) { if (C.HomeHab == 0 && C.HomeLevel < 0 && C.HomeLevel >= -Floors) { Out[-C.HomeLevel - 1].Ids.Add(C.Id); } }
			for (FFloor& F : Out) { F.Ids.Sort(); }
			return Out;
		};
		const auto RationOf = [&](int32 Level) -> const URHSimWorldSubsystem::FRHHomeRation*
		{
			for (const URHSimWorldSubsystem::FRHHomeRation& Home : Sim->GetHomeRations()) { if (Home.Key == Level) { return &Home; } }
			return nullptr;
		};
		const auto Restore = [&]()
		{
			SetStock(NFood, 1.0e8);
			SetStock(NWater, 1.0e8);
			for (int32 L = 1; L <= Floors; ++L) { Sim->Debug_ForceRateFloor(-L); }
			Sim->Debug_StepPopulation(URHSimClockSubsystem::SubStepSeconds);
		};
		{
			const TArray<FFloor> Homes = Roster();
			bool bThousand = true;
			for (const FFloor& F : Homes) { bThousand &= F.Ids.Num() == 1000; }
			Expect(bThousand, TEXT("homes: 1,000 residents on each of the five floors"));
		}

		// (b) A shortage is shared evenly. Food runs 30% short for the whole
		// colony; floor -2 has 40% of its air and floor -4 55%. Floors -1, -3 and
		// -5 are limited by food (0.7); -2 and -4 by air.
		{
			const TArray<FFloor> Homes = Roster();
			const double N = Sim->GetPopulation();
			const double O2Need = O2PerSol * (AgentDt / SolLen);
			SetStock(NFood, 0.7 * N * FoodNeedAt(AgentDt));
			Sim->Debug_SetFloorO2Kg(-2, 0.4 * Homes[1].Ids.Num() * O2Need);
			Sim->Debug_SetFloorO2Kg(-4, 0.55 * Homes[3].Ids.Num() * O2Need);
			const double O2Before1 = Sim->GetFloorO2Kg(-1);
			Sim->Debug_StepPopulation(URHSimClockSubsystem::SubStepSeconds);
			const double Want[5] = { 0.7, 0.4, 0.7, 0.55, 0.7 };
			const TCHAR* Limit[5] = { TEXT("Food"), TEXT("Oxygen"), TEXT("Food"), TEXT("Oxygen"), TEXT("Food") };
			bool bRations = Sim->GetHomeRations().Num() == 5, bEven = true, bNoneFed = true;
			double DeficitWant = 0.0;
			FString RationText;
			for (int32 I = 0; I < 5; ++I)
			{
				const URHSimWorldSubsystem::FRHHomeRation* Home = RationOf(-(I + 1));
				bRations &= Home && Near(Home->Fraction, Want[I], 1e-6) && Home->Limiting == FName(Limit[I]) && Home->Residents == Homes[I].Ids.Num();
				RationText += FString::Printf(TEXT(" floor %d %.3f (%s);"), -(I + 1), Home ? Home->Fraction : -1.0, Home ? *Home->Limiting.ToString() : TEXT("-"));
				DeficitWant += Homes[I].Ids.Num() * (1.0 - Want[I]);
				for (const int32 Id : Homes[I].Ids)
				{
					const FRHColonist* C = Sim->FindColonist(Id);
					bNoneFed &= C && !C->bSupported;
					bEven &= C && Near(Sim->GetColonistRation(*C), Want[I], 1e-6);
				}
			}
			Expect(bRations && bEven && bNoneFed,
				FString::Printf(TEXT("b every resident of a home gets the home's ration, nobody is fed in full while the home is short (the old rule fed the first 70%% and starved the rest):%s every resident reads the same"), *RationText));
			Expect(Near(Sim->GetDeficitEquivalent(), DeficitWant, 1e-6 * N) && Near(Sim->GetStock(NFood), 0.0, 1e-9) && Near(Sim->GetFloorO2Kg(-2), 0.0, 1e-9) && Near(Sim->GetFloorO2Kg(-4), 0.0, 1e-9)
				&& Near(Sim->GetFloorO2Kg(-1), O2Before1 - Homes[0].Ids.Num() * O2Need, 1e-6) && Sim->GetPopulation() == 5000,
				FString::Printf(TEXT("b the whole pool is taken and nothing more: deficit %.2f (expect %.2f), Food %.6f kg left, floor -2 / -4 air %.6f / %.6f kg left, floor -1 air down %.6f kg (expect %.6f)"),
					Sim->GetDeficitEquivalent(), DeficitWant, Sim->GetStock(NFood), Sim->GetFloorO2Kg(-2), Sim->GetFloorO2Kg(-4), O2Before1 - Sim->GetFloorO2Kg(-1), Homes[0].Ids.Num() * O2Need));
			Restore();
			Expect(Near(Sim->GetDeficitEquivalent(), 0.0, 1e-12) && Sim->GetHomeRations().Num() == 5 && RationOf(-2) && Near(RationOf(-2)->Fraction, 1.0, 1e-12),
				TEXT("b rations restored: every home back at 1.0, the deficit gone"));
		}

		// (d) One alert per event, and (c) departures at a ration of 0.55 (a deficit
		// of 0.45: a quarter-hundredth of a sol's worth of clock per resident per
		// step is 11.25 for a home of a thousand, so no step lands on a whole number
		// and floating-point noise cannot move a recall). Food runs short for the
		// whole colony: five homes of a thousand. The clock counts the residents the
		// home had when the shortage began (1,000), however many have left since.
		constexpr double ShortRation = 0.55;
		const auto ShortFood = [&]() { SetStock(NFood, ShortRation * Sim->GetPopulation() * FoodNeedAt(EraDt)); };
		const double DtSols = EraDt / SolLen;
		Alerts.Reset();
		TArray<int32> Before;
		for (const FFloor& F : Roster()) { Before.Add(F.Ids.Num()); }
		ShortFood();
		EraStepOnce();
		{
			bool bRate = true, bOnset = true;
			FString RateText;
			int32 Gone = 0;
			const TArray<FFloor> After = Roster();
			for (int32 I = 0; I < 5; ++I)
			{
				const int32 Departed = Before[I] - After[I].Ids.Num();
				Gone += Departed;
				const double Rate = Before[I] * (1.0 - ShortRation) / EvacSols * DtSols; // onset residents x (1 - r) / EvacSols per sol, over this step
				bRate &= Departed == FMath::FloorToInt(Rate + 1e-9);
				bOnset &= Sim->GetHomeOnsetResidents(-(I + 1)) == Before[I];
				RateText += FString::Printf(TEXT(" floor %d %d (rate %.2f);"), -(I + 1), Departed, Rate);
			}
			Expect(bRate && bOnset && Gone == 55,
				FString::Printf(TEXT("c ration 0.55, one 60 s step: each home of 1,000 recalled 1000 x 0.45 x 0.05 sol / %.0f = 11.25 -> 11 residents:%s %d in all, every clock counting on its 1,000 at onset"), EvacSols, *RateText, Gone));
			Expect(CountAlerts(TEXT("SHORT RATIONS")) == 5 && CountAlerts(TEXT("RECALLED")) == 5 && Alerts.Num() == 10 && CountAlerts(TEXT("LIFE SUPPORT")) == 0 && CountAlerts(TEXT("EVACUATED")) == 0,
				FString::Printf(TEXT("d the shortage begins: %d alert(s) in all - %d SHORT RATIONS (one per home, not one per resident), %d RECALLED (one batch per home), %d old per-person LIFE SUPPORT / EVACUATED"),
					Alerts.Num(), CountAlerts(TEXT("SHORT RATIONS")), CountAlerts(TEXT("RECALLED")), CountAlerts(TEXT("LIFE SUPPORT")) + CountAlerts(TEXT("EVACUATED"))));
			bool bText = false, bBatch = false;
			for (const FString& A : Alerts)
			{
				bText |= A.StartsWith(TEXT("SHORT RATIONS")) && A.Contains(TEXT("Floor -3")) && A.Contains(TEXT("food")) && A.Contains(TEXT("1000 residents"));
				bBatch |= A.StartsWith(TEXT("RECALLED")) && A.Contains(TEXT("11 residents of Floor -3"));
			}
			Expect(bText && bBatch, TEXT("d the alerts name the home, the count and the resource: \"... Floor -3 - food is running short for 1000 residents ...\" and \"11 residents of Floor -3 recalled to orbit\""));
		}
		Alerts.Reset();
		Before.Reset();
		for (const FFloor& F : Roster()) { Before.Add(F.Ids.Num()); }
		ShortFood();
		EraStepOnce();
		{
			// The second step is the same step: the clock counts the 1,000 of onset,
			// not the 989 left, so 0.25 + 11.25 = 11.5 -> 11 again (22.5 in two steps).
			bool bSteady = true;
			const TArray<FFloor> After = Roster();
			for (int32 I = 0; I < 5; ++I) { bSteady &= Before[I] - After[I].Ids.Num() == 11 && Sim->GetHomeOnsetResidents(-(I + 1)) == 1000; }
			Expect(CountAlerts(TEXT("SHORT RATIONS")) == 0 && CountAlerts(TEXT("RECALLED")) == 5 && Alerts.Num() == 5 && bSteady,
				FString::Printf(TEXT("d still short: no second SHORT RATIONS (it is an edge), one RECALLED batch per home: %d alert(s); c every home recalled 11 more - the rate does not slow as the home shrinks (it counts its 1,000, not the 989 left)"), Alerts.Num()));
		}

		// (e) Save / load in the middle of it: the clocks and the onset sizes come
		// back exactly, and the step after a load is the step that would have happened.
		{
			TArray<double> Acc0;
			TArray<int32> Onset0;
			for (int32 L = 1; L <= Floors; ++L) { Acc0.Add(Sim->GetHomeDepartureAcc(-L)); Onset0.Add(Sim->GetHomeOnsetResidents(-L)); }
			const double Deficit0 = Sim->GetDeficitEquivalent();
			const int32 Pop0 = Sim->GetPopulation();
			// Four Mars-born, to prove the flag rides the save.
			const TArray<FFloor> Homes0 = Roster();
			for (int32 K = 0; K < 4; ++K) { Sim->Debug_SetMarsBorn(Homes0[K].Ids[0], true); }
			bool bFractional = true;
			for (const double A : Acc0) { bFractional &= A > 0.05 && A < 1.0; }
			FString Err;
			Expect(Sim->SaveColony(TEXT("percaptest"), Err), FString::Printf(TEXT("e saved %d colonists mid-shortage (%s)"), Pop0, *Err));
			ShortFood();
			EraStepOnce();
			const int32 PopX1 = Sim->GetPopulation();
			TArray<double> AccX1;
			for (int32 L = 1; L <= Floors; ++L) { AccX1.Add(Sim->GetHomeDepartureAcc(-L)); }
			TArray<int32> IdsX1;
			for (const FRHColonist& C : Sim->GetColonists()) { IdsX1.Add(C.Id); }
			Expect(Sim->LoadColony(TEXT("percaptest"), Err) && Err.IsEmpty(), FString::Printf(TEXT("e loaded (%s)"), *Err));
			bool bAcc = true, bOnset = true;
			for (int32 L = 1; L <= Floors; ++L) { bAcc &= Sim->GetHomeDepartureAcc(-L) == Acc0[L - 1]; bOnset &= Sim->GetHomeOnsetResidents(-L) == Onset0[L - 1] && Onset0[L - 1] == 1000; }
			int32 MarsBorn = 0;
			for (const FRHColonist& C : Sim->GetColonists()) { MarsBorn += C.bMarsBorn ? 1 : 0; }
			Expect(bFractional && bAcc && bOnset && Sim->GetDeficitEquivalent() == Deficit0 && Sim->GetPopulation() == Pop0 && MarsBorn == 4,
				FString::Printf(TEXT("e the round trip holds the per-home clocks bit for bit (%.4f %.4f %.4f %.4f %.4f) and the onset sizes (%d %d %d %d %d), the deficit %.2f, the headcount %d, %d Mars-born"),
					Sim->GetHomeDepartureAcc(-1), Sim->GetHomeDepartureAcc(-2), Sim->GetHomeDepartureAcc(-3), Sim->GetHomeDepartureAcc(-4), Sim->GetHomeDepartureAcc(-5),
					Sim->GetHomeOnsetResidents(-1), Sim->GetHomeOnsetResidents(-2), Sim->GetHomeOnsetResidents(-3), Sim->GetHomeOnsetResidents(-4), Sim->GetHomeOnsetResidents(-5),
					Sim->GetDeficitEquivalent(), Sim->GetPopulation(), MarsBorn));
			ShortFood();
			EraStepOnce();
			bool bSame = Sim->GetPopulation() == PopX1 && Sim->GetPopulation() == IdsX1.Num();
			for (int32 L = 1; L <= Floors; ++L) { bSame &= Sim->GetHomeDepartureAcc(-L) == AccX1[L - 1]; }
			int32 Matching = 0;
			for (int32 I = 0; I < Sim->GetColonists().Num() && I < IdsX1.Num(); ++I) { Matching += Sim->GetColonists()[I].Id == IdsX1[I] ? 1 : 0; }
			Expect(bSame && Matching == IdsX1.Num(),
				FString::Printf(TEXT("e the step after the load matches the step without it: population %d vs %d, clocks equal, the same %d of %d residents in the same order"), Sim->GetPopulation(), PopX1, Matching, IdsX1.Num()));
			for (const FRHColonist& C : Sim->GetColonists()) { Sim->Debug_SetMarsBorn(C.Id, false); }
		}

		// Recovery: one RATIONS RESTORED per home, the clocks and onset sizes reset, then quiet.
		Alerts.Reset();
		SetStock(NFood, 1.0e8);
		EraStepOnce();
		bool bClear = true;
		for (int32 L = 1; L <= Floors; ++L) { bClear &= Sim->GetHomeDepartureAcc(-L) == 0.0 && Sim->GetHomeOnsetResidents(-L) == 0; }
		Expect(CountAlerts(TEXT("RATIONS RESTORED")) == 5 && Alerts.Num() == 5 && bClear && Near(Sim->GetDeficitEquivalent(), 0.0, 1e-12),
			FString::Printf(TEXT("d recovery: %d alert(s), %d RATIONS RESTORED (one per home), every clock and onset size back to 0"), Alerts.Num(), CountAlerts(TEXT("RATIONS RESTORED"))));
		Alerts.Reset();
		EraStepOnce();
		Expect(Alerts.Num() == 0, FString::Printf(TEXT("d quiet again: %d alert(s) on a full ration"), Alerts.Num()));

		// (c) A deep, long shortage: one sol (20 steps) at ration 0.55. The law is
		// linear: a home recalls onset residents x (1 - r) x sols / EvacSols, floored
		// - 22.5 percent of its size here - so the colony shrinks by 0.45 / 2 in a sol
		// (0.775 of what it was), not by a share of what is left.
		{
			TArray<int32> Start;
			for (const FFloor& F : Roster()) { Start.Add(F.Ids.Num()); }
			const int32 PopStart = Sim->GetPopulation();
			for (int32 K = 0; K < 20; ++K) { ShortFood(); EraStepOnce(); }
			bool bLinear = true;
			FString RecText;
			const TArray<FFloor> After = Roster();
			for (int32 I = 0; I < 5; ++I)
			{
				const int32 Want = Start[I] - FMath::FloorToInt(Start[I] * (1.0 - ShortRation) * (20 * DtSols) / EvacSols + 1e-9);
				bLinear &= After[I].Ids.Num() == Want;
				RecText += FString::Printf(TEXT(" %d -> %d (expect %d);"), Start[I], After[I].Ids.Num(), Want);
			}
			const double Linear = PopStart * (1.0 - (1.0 - ShortRation) * (20 * DtSols) / EvacSols);
			Expect(bLinear && Near(Sim->GetPopulation(), Linear, 0.01 * PopStart),
				FString::Printf(TEXT("c one sol at ration 0.55: each home loses floor(onset x 0.45 x 1 sol / %.0f):%s the colony %d -> %d (the linear law: %d x (1 - 0.45 / %.0f) = %.0f)"),
					EvacSols, *RecText, PopStart, Sim->GetPopulation(), PopStart, EvacSols, Linear));
			Restore();
			EraStepOnce();
		}

		// (c) Who leaves: ration 0 for one step. Floor -1's twenty newest are
		// Mars-born; floor -5 is all Mars-born but its five oldest; the rest are not.
		{
			const TArray<FFloor> Homes = Roster();
			TSet<int32> MarsBorn;
			for (int32 K = 0; K < 20; ++K) { MarsBorn.Add(Homes[0].Ids[Homes[0].Ids.Num() - 1 - K]); }
			for (int32 K = 5; K < Homes[4].Ids.Num(); ++K) { MarsBorn.Add(Homes[4].Ids[K]); }
			for (const int32 Id : MarsBorn) { Sim->Debug_SetMarsBorn(Id, true); }
			TSet<int32> Wanted;
			TArray<int32> WantedPerFloor;
			for (const FFloor& F : Homes)
			{
				const int32 K = FMath::FloorToInt(F.Ids.Num() * 1.0 * EraDt / (EvacSols * SolLen) + 1e-9);
				TArray<int32> Order = F.Ids;
				Order.Sort([&MarsBorn](int32 A, int32 B) { const bool MA = MarsBorn.Contains(A), MB = MarsBorn.Contains(B); return MA != MB ? !MA : A > B; });
				for (int32 I = 0; I < K; ++I) { Wanted.Add(Order[I]); }
				WantedPerFloor.Add(K);
			}
			SetStock(NFood, 0.0);
			Alerts.Reset();
			EraStepOnce();
			TSet<int32> Gone;
			for (const FFloor& F : Homes) { for (const int32 Id : F.Ids) { if (!Sim->FindColonist(Id)) { Gone.Add(Id); } } }
			bool bMarsKept = true;
			for (int32 K = 0; K < 20; ++K) { bMarsKept &= !Gone.Contains(Homes[0].Ids[Homes[0].Ids.Num() - 1 - K]); }
			bool bOldestNonMars = true;
			for (int32 K = 0; K < 5; ++K) { bOldestNonMars &= Gone.Contains(Homes[4].Ids[K]); }
			int32 MarsGoneOnLast = 0;
			for (int32 K = 5; K < Homes[4].Ids.Num(); ++K) { MarsGoneOnLast += Gone.Contains(Homes[4].Ids[K]) ? 1 : 0; }
			Expect(Gone.Num() == Wanted.Num() && Gone.Includes(Wanted) && Wanted.Includes(Gone),
				FString::Printf(TEXT("c who leaves, ration 0: %d recalled (%d %d %d %d %d by floor, each N / 40), exactly the newest arrivals of each home, Mars-born last"),
					Gone.Num(), WantedPerFloor[0], WantedPerFloor[1], WantedPerFloor[2], WantedPerFloor[3], WantedPerFloor[4]));
			Expect(bMarsKept && bOldestNonMars && MarsGoneOnLast == WantedPerFloor[4] - 5,
				FString::Printf(TEXT("c floor -1: its 20 newest, all Mars-born, stay while older non-Mars-born leave; floor -5: the five non-Mars-born go first (they hold the OLDEST Ids), then %d Mars-born from the highest Id"), MarsGoneOnLast));
			Expect(CountAlerts(TEXT("SHORT RATIONS")) == 5 && CountAlerts(TEXT("RECALLED")) == 5 && Alerts.Num() == 10,
				FString::Printf(TEXT("d %d people recalled across five homes, %d alerts"), Gone.Num(), Alerts.Num()));
			for (const FRHColonist& C : Sim->GetColonists()) { Sim->Debug_SetMarsBorn(C.Id, false); }
		}
		Restore();

		// Furnishings still read per resident at the smaller colony.
		{
			const double N = Sim->GetPopulation();
			const FHope HF = Sim->GetColonyHope();
			Expect(Near(HF.Furnishings, FMath::Min(Sim->GetHopeFurnishingsMax(), 8.0 * RefPop / N), 1e-9) && N < 5000,
				FString::Printf(TEXT("furnishings per resident: at N=%.0f the four sofas read %.5f (expect 8 x 8 / %.0f)"), N, HF.Furnishings, N));
		}
		// (c) A total failure empties a home in EvacSols: the food is gone and stays
		// gone. After one sol each home has lost half its onset size (the rate is
		// steady, not a share of what is left); after the two sols it has lost all
		// of it, give or take the one resident the last fraction of a step leaves.
		{
			TArray<int32> Start;
			for (const FFloor& F : Roster()) { Start.Add(F.Ids.Num()); }
			SetStock(NFood, 0.0);
			for (int32 K = 0; K < 20; ++K) { EraStepOnce(); }
			bool bHalf = true;
			FString HalfText;
			const TArray<FFloor> Half = Roster();
			for (int32 I = 0; I < 5; ++I)
			{
				bHalf &= FMath::Abs(Half[I].Ids.Num() - (Start[I] - Start[I] / 2)) <= 1;
				HalfText += FString::Printf(TEXT(" %d -> %d;"), Start[I], Half[I].Ids.Num());
			}
			Expect(bHalf, FString::Printf(TEXT("c ration 0 for one sol (EvacSols %.0f): every home has lost half its onset size:%s"), EvacSols, *HalfText));
			for (int32 K = 0; K < 20; ++K) { EraStepOnce(); }
			Expect(Sim->GetPopulation() <= 5, FString::Printf(TEXT("c ration 0 for the %.0f sols: the colony is down to %d (every home at most one resident from empty)"), EvacSols, Sim->GetPopulation()));
			for (int32 K = 0; K < 2; ++K) { EraStepOnce(); }
			Expect(Sim->GetPopulation() == 0 && Sim->GetHomeRations().Num() == 0 && Sim->GetDeficitEquivalent() == 0.0,
				FString::Printf(TEXT("c ...and empty a step or two after: %d residents, %d homes on the books"), Sim->GetPopulation(), Sim->GetHomeRations().Num()));
		}
		Sim->OnAlert.Remove(AlertH);
		Sim->OnCommandRejected.Remove(RejectH);
		UE_LOG(LogRedHopeSim, Display, TEXT("=== PER-CAPITA TEST END: %d check(s), %d failure(s) ==="), Checks, Fails);
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return Fails > 0 ? 1 : 0;
	}

	// C2b self-test (living-city gate 2b, 2026-10-18): ARRIVALS AT SCALE.
	// `-arrivals`. Fails loudly; it has no timing lines. The fixture is a shaft
	// of three floors of 200 cells whose beds are the legacy rule (a carved cell
	// holds ColonistsPerCell), so a suite can move the capacity by moving one
	// number. The smoothed Hope is held with Debug_SetHopeBase / SetHopeSmoothed
	// (the index itself is the real one). The checks:
	//  (b) births per head: the growth streak runs at max(1, N / BirthRefPop) x
	//      real time - exactly dt at N = 10 and 25, dt x 26 / 25 at 26, dt x 10 at
	//      250 - and the births follow (one in 20 sols at 10, one in 2 at 250);
	//  (a) the Colonist Transport lands with enough beds and stays aboard, whole,
	//      without (19 free of 20 needed), and a real ship lands one;
	//  (c) the immigration count against the formula at several Hope levels and
	//      sizes, the free-bed cap and the per-ship cap, applied for real: the
	//      settlers are not Mars-born and arrive under one alert - first through a
	//      real supply ship's landing, then directly;
	//  (d) the Big Arrival: no fire before 20 sols, none without 100 free beds,
	//      the streak resets when Hope drops, it fires once at 20 with beds (two
	//      alerts, the food), never again, and a save / load round trip holds the
	//      streak and the flag;
	//  (e) moving home, against an independent model of the rule: residents over a
	//      floor's beds (the newest, highest Id, are displaced), a floor that lost
	//      its rating, no free bed anywhere (they stay), once a sol, and the cost
	//      of a big move;
	//  (f) one alert per event, counted at OnAlert, in every path above.
	//  (g) C5c, the Big Arrival and life support: each leg's arithmetic (O2 against (N + 100) x
	//      draw x BigArrivalLifeSupportSols; Food with the wave's provisions counted, and Water,
	//      against FoodRunwayFloorSols), the wave holding with its streak running while air, water or
	//      food are short, the demand's words while it holds, and the landing on the step they recover.
	if (FParse::Param(*Params, TEXT("arrivals")))
	{
		UE_LOG(LogRedHopeSim, Display, TEXT("=== ARRIVALS TEST (C2b, 2026-10-18) ==="));
		int32 Fails = 0, Checks = 0;
		const auto Expect = [&](bool bOk, const FString& What)
		{
			++Checks;
			if (bOk) { UE_LOG(LogRedHopeSim, Display, TEXT("ARRV ok: %s"), *What); }
			else { UE_LOG(LogRedHopeSim, Error, TEXT("ARRV FAIL: %s"), *What); ++Fails; }
		};
		const auto Near = [](double A, double B, double Tol) { return FMath::Abs(A - B) <= Tol; };
		constexpr double EraDt = (double)URHSimClockSubsystem::EraStepSimSeconds;
		constexpr double SolLen = (double)URHSimClockSubsystem::SolLengthSimSeconds;
		constexpr double DtSols = EraDt / SolLen;
		constexpr int32 StepsPerSolA = (int32)(SolLen / EraDt);
		constexpr int32 Floors = 3;
		constexpr int32 CellsPerFloor = 200; // leak (200 x 2 kg/sol) stays under the trunk's push: the floors stay rated
		const FName NFood(TEXT("Food")), NWater(TEXT("Water")), NOxygen(TEXT("Oxygen")), NStruct(TEXT("Struct")), NHydrogen(TEXT("Hydrogen"));
		URHDefinitionsSubsystem* DefsSub = World->GetSubsystem<URHDefinitionsSubsystem>();
		FString R;

		// P0. Pure data: the fifteen config rows and the manifest row come off the
		// synced DataTables alone (no override, no C++ fallback), so a DT that
		// drifted from the CSV fails here and not silently.
		const TPair<const TCHAR*, double> Pins[] = {
			{ TEXT("ColonistTransportColonists"), 20.0 }, { TEXT("ColonistTransportFoodKgPerColonist"), 50.0 }, { TEXT("BirthRefPop"), 25.0 },
			{ TEXT("ImmigrationHopeMin"), 75.0 }, { TEXT("ImmigrationHopeFloor"), 70.0 }, { TEXT("ImmigrationBase"), 4.0 },
			{ TEXT("ImmigrationPerCapita"), 0.2 }, { TEXT("ImmigrationCapPerShip"), 50.0 }, { TEXT("BigArrivalHopeMin"), 75.0 },
			{ TEXT("BigArrivalStreakSols"), 20.0 }, { TEXT("BigArrivalFreeBeds"), 100.0 }, { TEXT("BigArrivalColonists"), 100.0 },
			{ TEXT("MoveHomeIntervalSols"), 1.0 }, { TEXT("ImmigrationReserveBeds"), 1.0 }, { TEXT("BigArrivalLifeSupportSols"), 10.0 } };
		FString PinText;
		bool bPins = true;
		for (const TPair<const TCHAR*, double>& Pin : Pins)
		{
			const double Got = DefsSub ? DefsSub->GetConfigTableScalar(FName(Pin.Key), -1.0) : -1.0;
			bPins &= Near(Got, Pin.Value, 1e-9);
			PinText += FString::Printf(TEXT(" %s=%g"), Pin.Key, Got);
		}
		Expect(bPins, FString::Printf(TEXT("P0 DT_Config pins:%s"), *PinText));
		Expect(Sim->GetColonistTransportColonists() == 20 && Near(Sim->GetColonistTransportFoodKgPerColonist(), 50.0, 1e-9) && Near(Sim->GetBirthRefPop(), 25.0, 1e-9)
			&& Near(Sim->GetImmigrationHopeMin(), 75.0, 1e-9) && Near(Sim->GetImmigrationHopeFloor(), 70.0, 1e-9) && Near(Sim->GetImmigrationBase(), 4.0, 1e-9)
			&& Near(Sim->GetImmigrationPerCapita(), 0.2, 1e-9) && Sim->GetImmigrationCapPerShip() == 50 && Near(Sim->GetBigArrivalHopeMin(), 75.0, 1e-9)
			&& Near(Sim->GetBigArrivalStreakSolsNeeded(), 20.0, 1e-9) && Sim->GetBigArrivalFreeBeds() == 100 && Sim->GetBigArrivalColonists() == 100
			&& Near(Sim->GetMoveHomeIntervalSols(), 1.0, 1e-9) && Sim->GetImmigrationReserveBeds() == 1 && Near(Sim->GetBigArrivalLifeSupportSols(), 10.0, 1e-9),
			TEXT("P0 the sim reads the same values (no override or fallback in play)"));
		const FRHManifestItemRow* TransportRow = DefsSub ? DefsSub->GetManifestItem(FName("ColonistTransport")) : nullptr;
		const FRHManifestItemRow* PodRow = DefsSub ? DefsSub->GetManifestItem(FName("CrewPod")) : nullptr;
		Expect(TransportRow && TransportRow->SliceActive && Near(TransportRow->Mass_kg, 8000.0, 1e-9) && TransportRow->Category == TEXT("Crew") && PodRow && PodRow->SliceActive,
			FString::Printf(TEXT("P0 DT_ManifestItems has ColonistTransport (%s, mass %.0f kg, category %s) beside CrewPod"),
				TransportRow ? (TransportRow->SliceActive ? TEXT("SliceActive") : TEXT("dormant")) : TEXT("MISSING"), TransportRow ? TransportRow->Mass_kg : -1.0, TransportRow ? *TransportRow->Category : TEXT("-")));
		if (!bPins || !TransportRow || !TransportRow->SliceActive || Fails > 0)
		{
			UE_LOG(LogRedHopeSim, Error, TEXT("ARRV FAIL: the data is not in place; the suite stops"));
			UE_LOG(LogRedHopeSim, Display, TEXT("=== ARRIVALS TEST END: %d check(s), %d failure(s) ==="), Checks, Fails + 1);
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
			return 1;
		}

		// Alerts, counted where the player's banner would hear them.
		TArray<FString> Alerts;
		const FDelegateHandle AlertH = Sim->OnAlert.AddLambda([&Alerts](const FString& S) { Alerts.Add(S); });
		const auto CountAlerts = [&Alerts](const TCHAR* Prefix) { int32 N = 0; for (const FString& A : Alerts) { N += A.StartsWith(Prefix) ? 1 : 0; } return N; };
		const auto CountAlertsWith = [&Alerts](const TCHAR* Needle) { int32 N = 0; for (const FString& A : Alerts) { N += A.Contains(Needle) ? 1 : 0; } return N; };
		// What the world looked like when a transport's colonists stepped off.
		struct FLandSnap { bool bSet = false; int32 Pop = 0, FreeBeds = 0; double Hope = 0.0; };
		FLandSnap Snap;
		const FDelegateHandle SnapH = Sim->OnAlert.AddLambda([&Snap, Sim](const FString& S)
		{
			if (!Snap.bSet && S.StartsWith(TEXT("A COLONIST TRANSPORT HAS LANDED")))
			{
				Snap.bSet = true;
				Snap.Pop = Sim->GetPopulation();
				Snap.FreeBeds = Sim->GetFreeHousing();
				Snap.Hope = Sim->GetHopeSmoothed();
			}
		});
		int32 Landings = 0;
		const FDelegateHandle ShipH = Sim->OnShipArrived.AddLambda([&Landings](const TArray<FName>&) { ++Landings; });

		const auto TopUp = [&]()
		{
			Sim->Debug_AddFreshWater(500.0 * EraDt / SolLen);
			Sim->Debug_SetBatteryWh(1.0e9);
		};
		const auto EraStepOnce = [&]()
		{
			TopUp();
			Clock->Debug_AdvanceSimSeconds(EraDt);
			Sim->EraStep((float)EraDt);
		};
		const auto RunSteps = [&](int32 Steps) { for (int32 K = 0; K < Steps; ++K) { EraStepOnce(); } };
		const auto RunSolsA = [&](double NSols) { RunSteps(FMath::RoundToInt(NSols * StepsPerSolA)); };
		const auto SetStock = [&](FName Res, double Kg) { Sim->AddStock(Res, -Sim->GetStock(Res)); Sim->AddStock(Res, Kg); };
		// Hold the colony's mood: the real Hope index sits above the base, so a base of
		// 95 keeps it near 100 and a base of 40 sinks the smoothed value below every gate.
		const auto HoldHope = [&](double V) { Sim->Debug_SetHopeBase(V); Sim->Debug_SetHopeSmoothed(V); };
		const auto Lapse = [&]() { HoldHope(40.0); EraStepOnce(); HoldHope(95.0); }; // one step below every gate: every streak resets
		// Capacity: beds per floor B give 3 x B beds. Leaves exactly Want free by moving
		// the beds, re-homing whoever is over them, and filling the rest.
		const auto SetFree = [&](int32 Want) -> bool
		{
			const int32 Pop = Sim->GetPopulation();
			const int32 PerFloor = (Pop + Want + Floors - 1) / Floors;
			Sim->Debug_SetColonistsPerCell((PerFloor + 0.5) / CellsPerFloor);
			Sim->Debug_RehomeResidents(); // anyone over a floor's new beds moves now, not at the next sol
			const int32 Fill = Sim->GetHousingCapacity() - Want - Pop;
			if (Fill > 0) { Sim->Debug_AddColonists(Fill); }
			return Sim->GetFreeHousing() == Want;
		};
		const auto MaxColonistId = [&]() { int32 Top = 0; for (const FRHColonist& C : Sim->GetColonists()) { Top = FMath::Max(Top, C.Id); } return Top; };
		const auto NewcomersFine = [&](int32 AboveId, int32 Expected) -> bool
		{
			int32 Seen = 0;
			bool bOk = true;
			for (const FRHColonist& C : Sim->GetColonists())
			{
				if (C.Id > AboveId)
				{
					++Seen;
					bOk &= !C.bMarsBorn && C.HomeHab == 0 && C.HomeLevel < 0 && C.HomeLevel >= -Floors && Sim->IsFloorRated(C.HomeLevel);
				}
			}
			return bOk && Seen == Expected;
		};
		const auto NoFloorOverBeds = [&]() -> bool
		{
			int32 Residents[Floors + 1] = { 0, 0, 0, 0 };
			for (const FRHColonist& C : Sim->GetColonists()) { if (C.HomeHab == 0 && C.HomeLevel < 0 && C.HomeLevel >= -Floors) { ++Residents[-C.HomeLevel]; } }
			bool bOk = true;
			for (int32 L = 1; L <= Floors; ++L) { bOk &= Residents[L] <= Sim->GetFloorBeds(-L); }
			return bOk;
		};

		// Comfortable room: the Housing term's denominator is the bed-weighted Living cells
		// (one each on this fixture, which has no furniture). Zones the first Target of the
		// 600 cells Living and clears the rest, so settlers' cap can be moved by one number.
		const auto SetComfort = [&](int32 Target)
		{
			for (int32 Flat = 0; Flat < Floors * CellsPerFloor; ++Flat)
			{
				const int32 Level = -(Flat / CellsPerFloor + 1), Index = Flat % CellsPerFloor;
				FString Why;
				if (!Sim->DesignateRoom(Level, Index, Flat < Target ? FName("LivingQuarters") : NAME_None, Why))
				{
					UE_LOG(LogRedHopeSim, Error, TEXT("ARRV: zoning floor %d cell %d failed: %s"), Level, Index, *Why);
					++Fails;
				}
			}
		};
		// Setup: power, a shaft, floors with an air filter each, every stock deep, a
		// quota the stocks already meet. Beds: five a cell until a test says otherwise.
		int32 LanderId = 0;
		for (const FRHBuildingInstance& B : Sim->GetBuildings()) { if (B.DefName == FName("Lander")) { LanderId = B.Id; } }
		Sim->Debug_PlaceInstant(FName("SolarArray"), FVector(3500.0, -1000.0, 0.0));
		Sim->Debug_PlaceInstant(FName("SolarArray"), FVector(3500.0, -2000.0, 0.0));
		Sim->AddStock(NOxygen, 1.0e8);
		Sim->AddStock(NWater, 1.0e8);
		Sim->AddStock(NFood, 1.0e8);
		Sim->AddStock(NHydrogen, 1.0e6);
		Sim->Debug_AddSolidAt(LanderId, NStruct, 1.0e6, false);
		const FVector Head(3000.0, 3000.0, 0.0);
		Sim->ExtendShaft(Floors, Head);
		bool bCarved = true;
		for (int32 L = 1; L <= Floors; ++L) { bCarved &= Sim->ExcavateFloor(-L, CellsPerFloor, R); }
		for (int32 L = 1; L <= Floors; ++L) { Sim->Debug_PlaceInstant(FName("AirFilter"), Head, -L); }
		for (int32 L = 1; L <= Floors; ++L) { Sim->Debug_ForceRateFloor(-L); }
		Sim->Debug_SetColonistsPerCell(5.0);
		SetComfort(Floors * CellsPerFloor);      // every cell a Living cell: room for 600 at full Housing
		Sim->Debug_SetHopeGates(101.0, 101.0);   // no research, no births, until a test turns births on
		Sim->Debug_SetBigArrivalDone(true);      // the Big Arrival waits for its own test
		HoldHope(95.0);
		EraStepOnce();
		Expect(bCarved && Sim->GetShaftDepth() == Floors && Sim->IsFloorRated(-1) && Sim->IsFloorRated(-Floors) && Sim->GetPopulation() == 0 && Sim->GetHousingCapacity() == Floors * 1000 && Sim->GetHousingComfortCapacity() == Floors * CellsPerFloor,
			FString::Printf(TEXT("setup: shaft to -%d, %d cells a floor, every floor rated, %d beds, %d comfortable places (every cell Living), nobody yet, Hope held near 100"), Floors, CellsPerFloor, Sim->GetHousingCapacity(), Sim->GetHousingComfortCapacity()));

		// (b) Births per head. The streak gains dt x max(1, N / 25) in an eligible step.
		Sim->Debug_SetHopeGates(101.0, 75.0);
		Sim->Debug_AddColonists(10);
		Expect(Sim->GetPopulation() == 10 && Sim->GetBirthsOnMars() == 0, TEXT("b a colony of ten, no births yet"));
		Lapse();
		Expect(Sim->GetHopeGrowthStreakSols() == 0.0 && Sim->GetPopulation() == 10, TEXT("b the streak is zero after a lapse (conditions lapsed: momentum resets)"));
		EraStepOnce();
		const double Streak10 = Sim->GetHopeGrowthStreakSols();
		EraStepOnce();
		Expect(Near(Streak10, DtSols, 1e-12) && Near(Sim->GetHopeGrowthStreakSols(), 2.0 * DtSols, 1e-12),
			FString::Printf(TEXT("b N=10: the streak gains exactly dt a step (%.9f then %.9f; dt = %.9f sol) - identical to the old law"), Streak10, Sim->GetHopeGrowthStreakSols(), DtSols));
		// The births at N = 10: nothing for 19.95 sols, then one by 20.05 (identical to today).
		Lapse();
		RunSteps(399);
		const int32 BirthsEarly = Sim->GetBirthsOnMars();
		RunSteps(3);
		Expect(BirthsEarly == 0 && Sim->GetBirthsOnMars() == 1 && Sim->GetPopulation() == 11 && CountAlerts(TEXT("THE FIRST MARTIAN IS BORN")) == 1 && CountAlerts(TEXT("A CHILD IS BORN")) == 0,
			FString::Printf(TEXT("b N=10: no birth by sol 19.95 (%d), the first Martian by sol 20.1 (%d born, population %d), one first-born alert"), BirthsEarly, Sim->GetBirthsOnMars(), Sim->GetPopulation()));
		bool bBornMars = Sim->GetColonists().Last().bMarsBorn;
		// N = 25 and 26: the boundary of the reference population.
		Sim->Debug_AddColonists(25 - Sim->GetPopulation());
		Lapse();
		EraStepOnce();
		const double Streak25 = Sim->GetHopeGrowthStreakSols();
		Sim->Debug_AddColonists(1);
		Lapse();
		EraStepOnce();
		const double Streak26 = Sim->GetHopeGrowthStreakSols();
		Expect(Sim->GetPopulation() == 26 && Near(Streak25, DtSols, 1e-12) && Near(Streak26, DtSols * 26.0 / 25.0, 1e-12),
			FString::Printf(TEXT("b N=25: dt (%.9f); N=26: dt x 26/25 (%.9f, expect %.9f) - the rate starts to scale one resident past the reference"), Streak25, Streak26, DtSols * 26.0 / 25.0));
		// N = 250: ten times the rate.
		Sim->Debug_AddColonists(250 - Sim->GetPopulation());
		Lapse();
		EraStepOnce();
		const double Streak250 = Sim->GetHopeGrowthStreakSols();
		Expect(Sim->GetPopulation() == 250 && Near(Streak250, DtSols * 10.0, 1e-12),
			FString::Printf(TEXT("b N=250: the streak gains dt x 10 a step (%.9f, expect %.9f)"), Streak250, DtSols * 10.0));
		Lapse();
		Alerts.Reset();
		const int32 BirthsBefore250 = Sim->GetBirthsOnMars();
		RunSteps(39); // 1.95 sols
		const int32 Births195 = Sim->GetBirthsOnMars() - BirthsBefore250;
		RunSteps(2);  // 2.05 sols
		const int32 Births205 = Sim->GetBirthsOnMars() - BirthsBefore250;
		RunSteps(41); // 4.1 sols
		const int32 Births41 = Sim->GetBirthsOnMars() - BirthsBefore250;
		bBornMars &= Sim->GetColonists().Last().bMarsBorn;
		Expect(Births195 == 0 && Births205 == 1 && Births41 == 2 && Sim->GetPopulation() == 252 && bBornMars,
			FString::Printf(TEXT("b N=250: no birth by sol 1.95 (%d), one by 2.05 (%d), two by 4.1 (%d) - ten times the births of a crew (one in 20 sols); every birth is Mars-born (%d)"),
				Births195, Births205, Births41, (int32)bBornMars));
		Expect(CountAlerts(TEXT("A CHILD IS BORN")) == 0 && CountAlerts(TEXT("THE FIRST MARTIAN")) == 0 && CountAlerts(TEXT("THE COLONY GROWS")) == 0,
			FString::Printf(TEXT("f %d births in a city, %d birth alerts (a city's births are a rate: a banner on every tenth, never one per child)"), Births41, CountAlerts(TEXT("A CHILD")) + CountAlerts(TEXT("THE COLONY GROWS"))));
		Sim->Debug_SetHopeGates(101.0, 101.0);

		// (a) The Colonist Transport. Twenty colonists and fifty kilos of food each; it
		// lands whole or not at all.
		{
			const bool bFree20 = SetFree(20);
			Expect(bFree20, FString::Printf(TEXT("a setup: exactly 20 beds free (%d of %d), population %d"), Sim->GetFreeHousing(), Sim->GetHousingCapacity(), Sim->GetPopulation()));
			Alerts.Reset();
			const int32 PopA = Sim->GetPopulation(), TopId = MaxColonistId();
			const double FoodA = Sim->GetStock(NFood);
			Sim->Debug_DeliverCargo(FName("ColonistTransport"));
			Expect(Sim->GetPopulation() == PopA + 20 && Sim->GetFreeHousing() == 0 && Near(Sim->GetStock(NFood) - FoodA, 1000.0, 1e-6) && NewcomersFine(TopId, 20) && NoFloorOverBeds(),
				FString::Printf(TEXT("a 20 beds free of 20 needed: the transport lands - population %d -> %d, +%.0f kg Food (20 x 50), all housed on rated floors, none Mars-born"),
					PopA, Sim->GetPopulation(), Sim->GetStock(NFood) - FoodA));
			Expect(Alerts.Num() == 1 && CountAlerts(TEXT("A COLONIST TRANSPORT HAS LANDED")) == 1 && Alerts[0].Contains(TEXT("20 colonists")) && Alerts[0].Contains(TEXT("1000 kg")),
				FString::Printf(TEXT("f the landing is one alert, not one per colonist: %d alert(s), \"%s\""), Alerts.Num(), Alerts.Num() ? *Alerts[0] : TEXT("-")));
			// No beds at all.
			Alerts.Reset();
			const int32 PopB = Sim->GetPopulation();
			const double FoodB = Sim->GetStock(NFood);
			Sim->Debug_DeliverCargo(FName("ColonistTransport"));
			Expect(Sim->GetPopulation() == PopB && Near(Sim->GetStock(NFood), FoodB, 1e-9) && Alerts.Num() == 1 && Alerts[0].StartsWith(TEXT("COLONIST TRANSPORT RETURNED")) && Alerts[0].Contains(TEXT("0 beds free, 20 needed")),
				FString::Printf(TEXT("a 0 beds free: the whole transport stays aboard - population %d, Food unchanged, \"%s\""), Sim->GetPopulation(), Alerts.Num() ? *Alerts[0] : TEXT("-")));
			// One bed short: still all or nothing.
			const bool bFree19 = SetFree(19);
			Expect(bFree19, FString::Printf(TEXT("a setup: exactly 19 beds free (%d)"), Sim->GetFreeHousing()));
			Alerts.Reset();
			const int32 PopC = Sim->GetPopulation();
			const double FoodC = Sim->GetStock(NFood);
			Sim->Debug_DeliverCargo(FName("ColonistTransport"));
			Expect(Sim->GetPopulation() == PopC && Sim->GetFreeHousing() == 19 && Near(Sim->GetStock(NFood), FoodC, 1e-9) && Alerts.Num() == 1 && Alerts[0].Contains(TEXT("19 beds free, 20 needed")),
				FString::Printf(TEXT("a 19 beds free of 20: nobody steps off (population %d, %d beds still free, Food unchanged), \"%s\""), Sim->GetPopulation(), Sim->GetFreeHousing(), Alerts.Num() ? *Alerts[0] : TEXT("-")));
			// Beds appear: it lands.
			Sim->Debug_SetColonistsPerCell(5.0);
			Alerts.Reset();
			const int32 PopD = Sim->GetPopulation(), TopD = MaxColonistId();
			Sim->Debug_DeliverCargo(FName("ColonistTransport"));
			Expect(Sim->GetPopulation() == PopD + 20 && NewcomersFine(TopD, 20) && Alerts.Num() == 1 && CountAlerts(TEXT("A COLONIST TRANSPORT HAS LANDED")) == 1,
				FString::Printf(TEXT("a with beds again the next transport lands: population %d -> %d, one alert"), PopD, Sim->GetPopulation()));
		}

		// (a) + (c) A real supply ship. Its manifest carries a Colonist Transport, so the
		// cargo lands first and the settlers fill what beds are left: 29 free, the
		// transport takes 20, the settlers are capped at the 9 remaining.
		{
			const bool bFree29 = SetFree(29);
			Expect(bFree29, FString::Printf(TEXT("a ship setup: exactly 29 beds free (%d), population %d"), Sim->GetFreeHousing(), Sim->GetPopulation()));
			HoldHope(95.0);
			for (const auto& L : Sim->GetQuotaProgress())
			{
				const double Gap = L.Value.Value + 1.0 - L.Value.Key;
				if (Gap > 0.0) { Sim->AddStock(L.Key, Gap); }
			}
			RunSteps(4);
			FString Err;
			const bool bAwaiting = Sim->GetQuotaPhase() == ERHQuotaPhase::AwaitingManifest;
			const bool bAdded = bAwaiting && Sim->AddManifestItem(FName("ColonistTransport"), Err);
			const bool bLaunched = bAdded && Sim->LaunchShip(Err);
			Expect(bAwaiting && bAdded && bLaunched,
				FString::Printf(TEXT("a quota met, the Colonist Transport (8000 kg) fits the award (%.0f kg) and the ship launches (%s)"), Sim->GetAwardMassKg(), *Err));
			Alerts.Reset();
			Snap = FLandSnap();
			const int32 LandBefore = Landings;
			const int32 PopShip = Sim->GetPopulation(), TopShip = MaxColonistId();
			const int32 SettlersBefore = Sim->GetImmigrantsTotal();
			for (int32 K = 0; K < 6 * StepsPerSolA && Landings == LandBefore; ++K) { EraStepOnce(); }
			const int32 ExpectedSettlers = Snap.bSet ? Sim->GetImmigrationCount(Snap.Hope, Snap.Pop, Snap.FreeBeds, Sim->GetHousingComfortCapacity()) : -1;
			Expect(Landings == LandBefore + 1 && Snap.bSet && Snap.Pop == PopShip + 20 && Snap.FreeBeds == 9 && Snap.Hope >= 75.0,
				FString::Printf(TEXT("a the ship lands and its transport steps off first: population %d -> %d, %d beds left, smoothed Hope %.1f"), PopShip, Snap.Pop, Snap.FreeBeds, Snap.Hope));
			Expect(ExpectedSettlers == 8 && Sim->GetImmigrantsTotal() - SettlersBefore == 8 && Sim->GetPopulation() == PopShip + 20 + 8 && Sim->GetFreeHousing() == 1 && NewcomersFine(TopShip, 28) && NoFloorOverBeds(),
				FString::Printf(TEXT("c the same landing draws settlers: the formula allows %d at Hope %.1f and a population of %d, only 9 beds are free and one is held back for a Mars-born child, so 8 arrive and one bed stays free (population %d); none Mars-born, none over a floor's beds"),
					Snap.Pop > 0 ? FMath::FloorToInt(FMath::Clamp(Snap.Hope - 70.0, 0.0, 30.0) * FMath::Max(4.0, 0.2 * Snap.Pop) / 30.0) : 0, Snap.Hope, Snap.Pop, Sim->GetPopulation()));
			Expect(CountAlerts(TEXT("A COLONIST TRANSPORT HAS LANDED")) == 1 && CountAlertsWith(TEXT("8 settlers arrived with the supply ship, drawn by the colony's reputation.")) == 1,
				FString::Printf(TEXT("f one alert for the transport and one for all eight settlers: %d + %d"), CountAlerts(TEXT("A COLONIST TRANSPORT HAS LANDED")), CountAlertsWith(TEXT("settlers arrived"))));
			// Births come first: the bed the settlers left free goes to the next Mars-born child.
			Sim->Debug_SetHopeGates(101.0, 75.0);
			Lapse();
			const int32 BirthsPre = Sim->GetBirthsOnMars();
			const int32 PopPreBirth = Sim->GetPopulation();
			RunSolsA(2.5);
			Expect(Sim->GetBirthsOnMars() == BirthsPre + 1 && Sim->GetPopulation() == PopPreBirth + 1 && Sim->GetFreeHousing() == 0 && Sim->GetColonists().Last().bMarsBorn,
				FString::Printf(TEXT("c births come first: the one bed the settlers kept free went to a Mars-born child within 2.5 sols (%d born, population %d, %d beds free)"), Sim->GetBirthsOnMars() - BirthsPre, Sim->GetPopulation(), Sim->GetFreeHousing()));
			Sim->Debug_SetHopeGates(101.0, 101.0);
		}

		// (c) The count, against the formula floor((Hope - 70) / 30 x max(4, 0.2 N)),
		// capped at 50 and at the free beds; nothing under Hope 75 or with nobody there.
		{
			struct FCase { double Hope; int32 Pop, FreeBeds, Comfort, Want; };
			constexpr int32 Roomy = 100000; // comfortable room never binds
			const FCase Cases[] = {
				{ 74.9, 100, 500, Roomy, 0 },  // under the minimum
				{ 75.0, 10, 500, Roomy, 0 },   // 5 x 4 / 30 = 0.67
				{ 75.0, 100, 500, Roomy, 3 },  // 5 x 20 / 30 = 3.33
				{ 85.0, 10, 500, Roomy, 2 },   // 15 x 4 / 30 = 2 (the floor of 4 beats 0.2 x 10)
				{ 90.0, 10, 500, Roomy, 2 },   // 20 x 4 / 30 = 2.67
				{ 100.0, 10, 500, Roomy, 4 },  // 30 x 4 / 30
				{ 85.0, 100, 500, Roomy, 10 }, // 15 x 20 / 30 = 10
				{ 95.0, 250, 500, Roomy, 41 }, // 25 x 50 / 30 = 41.67
				{ 100.0, 250, 500, Roomy, 50 },// 50: the full draw is the cap
				{ 100.0, 1000, 500, Roomy, 50 },// 200 capped at 50 a ship
				{ 80.0, 1000, 500, Roomy, 50 },// 66 capped at 50
				{ 72.0, 1000, 500, Roomy, 0 }, // under the minimum at any size
				{ 100.0, 1000, 7, Roomy, 6 },  // the free beds cap it, less the one held back for births
				{ 100.0, 250, 2, Roomy, 1 },   // two free: one settler, one bed kept
				{ 100.0, 250, 1, Roomy, 0 },   // one free bed is the births' bed: nobody comes
				{ 100.0, 250, 0, Roomy, 0 },   // no beds, nobody comes
				{ 100.0, 0, 500, Roomy, 0 },   // nobody here, no reputation
				{ 100.0, 250, 500, 260, 10 },  // comfortable room: 260 places for 250 people
				{ 100.0, 250, 500, 250, 0 },   // the Housing term is exactly full: nobody comes
				{ 100.0, 250, 500, 100, 0 },   // already past comfort: nobody comes, never negative
				{ 100.0, 250, 8, 260, 7 },     // the beds bind before the comfortable room (8 - 1 < 10)
				{ 100.0, 250, 500, 0, 0 } };   // no Living cells at all: no comfortable room
			bool bAll = true;
			FString CaseText;
			for (const FCase& Case : Cases)
			{
				const int32 Got = Sim->GetImmigrationCount(Case.Hope, Case.Pop, Case.FreeBeds, Case.Comfort);
				bAll &= Got == Case.Want;
				CaseText += FString::Printf(TEXT(" (Hope %.1f, N %d, %d free, room for %d) -> %d%s;"), Case.Hope, Case.Pop, Case.FreeBeds, Case.Comfort, Got, Got == Case.Want ? TEXT("") : *FString::Printf(TEXT(" EXPECTED %d"), Case.Want));
			}
			Expect(bAll, FString::Printf(TEXT("c the formula, the per-ship cap of 50, the free-bed cap with its reserve, the comfortable-room cap and the Hope minimum:%s"), *CaseText));

			// Applied for real. 500 beds free; settlers drawn at Hope 85, 75, 100, 74.9.
			Sim->Debug_SetColonistsPerCell(5.0);
			const auto Apply = [&](double Hope, int32 WantSettlers, const TCHAR* Tag)
			{
				Sim->Debug_SetHopeSmoothed(Hope);
				Alerts.Reset();
				const int32 PopX = Sim->GetPopulation(), TopX = MaxColonistId(), TotalX = Sim->GetImmigrantsTotal();
				const int32 Came = Sim->Debug_Immigrate();
				const bool bAlertOk = WantSettlers == 0
					? Alerts.Num() == 0
					: (Alerts.Num() == 1 && Alerts[0] == FString::Printf(TEXT("%d settler%s arrived with the supply ship, drawn by the colony's reputation."), WantSettlers, WantSettlers == 1 ? TEXT("") : TEXT("s")));
				Expect(Came == WantSettlers && Sim->GetPopulation() == PopX + WantSettlers && Sim->GetImmigrantsTotal() == TotalX + WantSettlers && NewcomersFine(TopX, WantSettlers) && bAlertOk,
					FString::Printf(TEXT("c %s: Hope %.1f at a population of %d draws %d settlers (%d came, population %d), not Mars-born, housed on rated floors, %d alert(s)%s"),
						Tag, Hope, PopX, WantSettlers, Came, Sim->GetPopulation(), Alerts.Num(), Alerts.Num() == 1 ? *FString::Printf(TEXT(": \"%s\""), *Alerts[0]) : TEXT("")));
			};
			const auto Floor0 = [](double V) { return (int32)FMath::FloorToDouble(V); };
			const auto FullDraw = [&](double AtPop) { return FMath::Max(Sim->GetImmigrationBase(), Sim->GetImmigrationPerCapita() * AtPop); };
			const double Pop1 = Sim->GetPopulation();
			Apply(85.0, Floor0(15.0 * FullDraw(Pop1) / 30.0), TEXT("a thriving city"));
			const double Pop2 = Sim->GetPopulation();
			Apply(75.0, Floor0(5.0 * FullDraw(Pop2) / 30.0), TEXT("the minimum Hope"));
			Apply(74.9, 0, TEXT("just under the minimum"));
			Apply(100.0, 50, TEXT("full Hope in a city: the per-ship cap"));
			// The free-bed cap, applied: 3 beds free, full Hope in a city of 300.
			const bool bFree3 = SetFree(3);
			Expect(bFree3, FString::Printf(TEXT("c setup: exactly 3 beds free (%d), population %d"), Sim->GetFreeHousing(), Sim->GetPopulation()));
			Apply(100.0, 2, TEXT("three free beds, one held back for births"));
			Expect(Sim->GetFreeHousing() == 1, FString::Printf(TEXT("c the two settlers left the last bed free for a Mars-born child (%d free)"), Sim->GetFreeHousing()));
			Apply(100.0, 0, TEXT("the last free bed is reserved: nobody comes"));
			Sim->Debug_SetColonistsPerCell(5.0);

			// Settlers never push the Housing term below its maximum. Room for exactly five more
			// at full Housing: five come, the term holds; then it is exactly full and nobody does.
			const double HousingMax = Sim->GetHopeHousingMax();
			SetComfort(Sim->GetPopulation() + 5);
			const double HousingBefore5 = Sim->GetColonyHope().Housing;
			Apply(100.0, 5, TEXT("room for five at full Housing"));
			const double HousingAfter5 = Sim->GetColonyHope().Housing;
			Expect(Near(HousingBefore5, HousingMax, 1e-9) && Near(HousingAfter5, HousingMax, 1e-9) && Sim->GetHousingComfortCapacity() == Sim->GetPopulation(),
				FString::Printf(TEXT("c the Housing term was %.4f before the five settlers and is %.4f after (maximum %.4f): the comfortable room (%d) is exactly the population (%d)"),
					HousingBefore5, HousingAfter5, HousingMax, Sim->GetHousingComfortCapacity(), Sim->GetPopulation()));
			Apply(100.0, 0, TEXT("the Housing term is exactly full: nobody comes"));
			Expect(Near(Sim->GetColonyHope().Housing, HousingMax, 1e-9), TEXT("c still at the maximum after the refused ship"));
			// Already below the maximum (fewer Living cells than people): nobody comes, and the term does not move.
			SetComfort(Sim->GetPopulation() - 10);
			const double HousingShort = Sim->GetColonyHope().Housing;
			Apply(100.0, 0, TEXT("ten people short of comfortable room"));
			Expect(HousingShort < HousingMax - 1e-9 && Near(Sim->GetColonyHope().Housing, HousingShort, 1e-12),
				FString::Printf(TEXT("c the Housing term, already %.4f of %.4f, is not lowered further by a ship that brings nobody (%.4f)"), HousingShort, HousingMax, Sim->GetColonyHope().Housing));
			SetComfort(Floors * CellsPerFloor);
		}

		// (d) The Big Arrival.
		{
			Sim->Debug_SetColonistsPerCell(5.0);
			Sim->Debug_SetBigArrivalDone(false);
			HoldHope(95.0);
			Lapse(); // a clean streak
			Expect(Sim->GetBigArrivalStreakSols() == 0.0 && !Sim->IsBigArrivalDone() && Sim->GetFreeHousing() >= 100,
				FString::Printf(TEXT("d the streak starts at zero (%.3f), not done, %d beds free"), Sim->GetBigArrivalStreakSols(), Sim->GetFreeHousing()));
			const int32 PopD0 = Sim->GetPopulation();
			Alerts.Reset();
			RunSolsA(5.0);
			Expect(Near(Sim->GetBigArrivalStreakSols(), 5.0, 1e-9),
				FString::Printf(TEXT("d five sols at Hope %.1f: the streak reads %.9f sols"), Sim->GetHopeSmoothed(), Sim->GetBigArrivalStreakSols()));
			// Linear in dt: one 120 s step is two 60 s steps.
			const double Streak5 = Sim->GetBigArrivalStreakSols();
			TopUp();
			Clock->Debug_AdvanceSimSeconds(2.0 * EraDt);
			Sim->EraStep((float)(2.0 * EraDt));
			Expect(Near(Sim->GetBigArrivalStreakSols() - Streak5, 2.0 * DtSols, 1e-12),
				FString::Printf(TEXT("d linear in dt: one 120 s step adds %.9f sol, two 60 s steps add %.9f"), Sim->GetBigArrivalStreakSols() - Streak5, 2.0 * DtSols));
			// The streak resets the moment Hope drops below 75.
			Lapse();
			Expect(Sim->GetBigArrivalStreakSols() == 0.0 && !Sim->IsBigArrivalDone() && Sim->GetPopulation() == PopD0,
				FString::Printf(TEXT("d one step under Hope 75 resets the streak to %.3f"), Sim->GetBigArrivalStreakSols()));

			// Without the beds: 99 free. The streak runs past 20 sols and nothing lands.
			const bool bFree99 = SetFree(99);
			Expect(bFree99, FString::Printf(TEXT("d setup: exactly 99 beds free (%d)"), Sim->GetFreeHousing()));
			const int32 PopNoBeds = Sim->GetPopulation();
			Alerts.Reset();
			RunSolsA(21.0);
			Expect(Sim->GetBigArrivalStreakSols() >= 20.9 && !Sim->IsBigArrivalDone() && Sim->GetPopulation() == PopNoBeds && CountAlerts(TEXT("THE BIG ARRIVAL")) == 0,
				FString::Printf(TEXT("d 99 free beds, 21 sols at THRIVING: the streak reads %.2f but the wave does not land (population %d, done %d)"), Sim->GetBigArrivalStreakSols(), Sim->GetPopulation(), (int32)Sim->IsBigArrivalDone()));
			// Beds appear: the very next step lands it, once.
			Sim->Debug_SetColonistsPerCell(5.0);
			SetStock(NFood, 100000.0);
			const int32 PopPre = Sim->GetPopulation(), TopPre = MaxColonistId();
			const int32 TotalPre = Sim->GetImmigrantsTotal();
			Alerts.Reset();
			EraStepOnce();
			const double FoodDrawn = PopPre * Sim->GetColonistFoodKgPerSol() * DtSols;
			Expect(Sim->IsBigArrivalDone() && Sim->GetPopulation() == PopPre + 100 && NewcomersFine(TopPre, 100) && NoFloorOverBeds(),
				FString::Printf(TEXT("d the beds appear and the Big Arrival lands: population %d -> %d, 100 settlers housed on rated floors, none Mars-born"), PopPre, Sim->GetPopulation()));
			Expect(Near(Sim->GetStock(NFood), 100000.0 - FoodDrawn + 5000.0, 1e-6) && Sim->GetImmigrantsTotal() == TotalPre,
				FString::Printf(TEXT("d with their food: %.3f kg (expect 100000 - %.3f eaten by the %d already here + 5000 for the hundred = %.3f)"), Sim->GetStock(NFood), FoodDrawn, PopPre, 100000.0 - FoodDrawn + 5000.0));
			Expect(Alerts.Num() == 2 && CountAlerts(TEXT("THE BIG ARRIVAL")) == 1 && CountAlerts(TEXT("THE CEO HAS ANNOUNCED HE WILL COME")) == 1,
				FString::Printf(TEXT("f two alerts for a hundred people, one for the arrival and one for the CEO: %d alert(s), %d + %d"), Alerts.Num(), CountAlerts(TEXT("THE BIG ARRIVAL")), CountAlerts(TEXT("THE CEO HAS ANNOUNCED"))));
			const double StreakAtFire = Sim->GetBigArrivalStreakSols();
			const int32 PopAfter = Sim->GetPopulation();
			RunSolsA(40.0);
			Expect(Sim->IsBigArrivalDone() && Sim->GetPopulation() == PopAfter && CountAlerts(TEXT("THE BIG ARRIVAL")) == 1 && Sim->GetBigArrivalStreakSols() == StreakAtFire,
				FString::Printf(TEXT("d forty sols later it has not come again: population %d, %d Big Arrival alert(s) in all, the streak parked at %.2f"), Sim->GetPopulation(), CountAlerts(TEXT("THE BIG ARRIVAL")), Sim->GetBigArrivalStreakSols()));

			// Exactly at 20: from a clean streak, it fires on the step the streak reaches 20 -
			// not one step before - and a save / load round trip holds the streak and the flag.
			Sim->Debug_SetBigArrivalDone(false);
			Lapse();
			RunSolsA(7.0);
			const double Streak7 = Sim->GetBigArrivalStreakSols();
			const int32 PopSave = Sim->GetPopulation(), TotalSave = Sim->GetImmigrantsTotal();
			const double MoveAccSave = Sim->GetMoveHomeAccSols();
			FString Err;
			const bool bSaved7 = Sim->SaveColony(TEXT("arrivalstest"), Err);
			Expect(Near(Streak7, 7.0, 1e-9) && !Sim->IsBigArrivalDone() && bSaved7,
				FString::Printf(TEXT("d saved mid-streak: %.9f sols, not done (%s)"), Streak7, *Err));
			int32 FireStep = -1;
			for (int32 K = 1; K <= 14 * StepsPerSolA && FireStep < 0; ++K)
			{
				EraStepOnce();
				if (Sim->IsBigArrivalDone()) { FireStep = K; }
			}
			// 13 more sols = 260 steps reach 20.0; a float sum may take one more.
			Expect(FireStep >= 259 && FireStep <= 261 && Sim->GetPopulation() == PopSave + 100 && Sim->GetBigArrivalStreakSols() >= 20.0 - 1e-9,
				FString::Printf(TEXT("d from sol 7 it lands on step %d (13 sols = 260 steps), never before: population %d -> %d, streak %.6f"), FireStep, PopSave, Sim->GetPopulation(), Sim->GetBigArrivalStreakSols()));
			const bool bLoaded7 = Sim->LoadColony(TEXT("arrivalstest"), Err);
			Expect(bLoaded7 && Err.IsEmpty(), FString::Printf(TEXT("d loaded (%s)"), *Err));
			Expect(Sim->GetBigArrivalStreakSols() == Streak7 && !Sim->IsBigArrivalDone() && Sim->GetPopulation() == PopSave && Sim->GetImmigrantsTotal() == TotalSave && Sim->GetMoveHomeAccSols() == MoveAccSave,
				FString::Printf(TEXT("d the round trip holds the streak bit for bit (%.9f), the flag (done %d), the headcount (%d), the settler count (%d) and the move-home clock (%.9f)"),
					Sim->GetBigArrivalStreakSols(), (int32)Sim->IsBigArrivalDone(), Sim->GetPopulation(), Sim->GetImmigrantsTotal(), Sim->GetMoveHomeAccSols()));
			int32 FireStep2 = -1;
			for (int32 K = 1; K <= 14 * StepsPerSolA && FireStep2 < 0; ++K)
			{
				EraStepOnce();
				if (Sim->IsBigArrivalDone()) { FireStep2 = K; }
			}
			Expect(FireStep2 == FireStep && Sim->GetPopulation() == PopSave + 100,
				FString::Printf(TEXT("d the run after the load is the run without it: it lands on step %d again (was %d)"), FireStep2, FireStep));
			// Done is saved too: it stays done after a reload and never comes again.
			const bool bSavedDone = Sim->SaveColony(TEXT("arrivalsdone"), Err);
			Expect(bSavedDone && Sim->IsBigArrivalDone(), FString::Printf(TEXT("d saved after the Big Arrival (%s)"), *Err));
			RunSolsA(3.0);
			const bool bLoadedDone = Sim->LoadColony(TEXT("arrivalsdone"), Err);
			Expect(bLoadedDone && Err.IsEmpty() && Sim->IsBigArrivalDone(), FString::Printf(TEXT("d reloaded: done %d (%s)"), (int32)Sim->IsBigArrivalDone(), *Err));
			const int32 PopDone = Sim->GetPopulation();
			RunSolsA(25.0);
			Expect(Sim->IsBigArrivalDone() && Sim->GetPopulation() == PopDone,
				FString::Printf(TEXT("d the flag rides the save: 25 sols after the reload, population %d, no second wave"), Sim->GetPopulation()));
		}

		// (e) Moving home. The model is independent of the code: per floor the lowest Ids
		// keep the beds there are (none, if the floor is not rated); the rest, highest
		// Id first, take the shallowest rated floor with a free bed; when none is free
		// the rest stay.
		{
			using FHomes = TMap<int32, int32>; // Id -> floor level
			const auto Homes = [&]() -> FHomes { FHomes M; for (const FRHColonist& C : Sim->GetColonists()) { M.Add(C.Id, C.HomeLevel); } return M; };
			const auto Predict = [&](const FHomes& Now, const int32 Beds[Floors + 1], const bool Rated[Floors + 1], FHomes& Out, int32* OutDisplaced = nullptr) -> int32
			{
				Out = Now;
				TArray<int32> ByFloor[Floors + 1];
				for (const TPair<int32, int32>& P : Now) { ByFloor[-P.Value].Add(P.Key); }
				int32 Count[Floors + 1] = { 0, 0, 0, 0 };
				TArray<int32> Displaced;
				for (int32 L = 1; L <= Floors; ++L)
				{
					ByFloor[L].Sort();
					const int32 Cap = Rated[L] ? Beds[L] : 0;
					Count[L] = FMath::Min(ByFloor[L].Num(), Cap);
					for (int32 K = Cap; K < ByFloor[L].Num(); ++K) { Displaced.Add(ByFloor[L][K]); }
				}
				Displaced.Sort([](int32 A, int32 B) { return A > B; });
				if (OutDisplaced) { *OutDisplaced = Displaced.Num(); }
				int32 Moved = 0;
				for (const int32 Id : Displaced)
				{
					int32 Target = 0;
					for (int32 L = 1; L <= Floors && !Target; ++L) { if (Rated[L] && Count[L] < Beds[L]) { Target = L; } }
					if (!Target) { break; }
					++Count[Target];
					Out[Id] = -Target;
					++Moved;
				}
				return Moved;
			};
			const auto SameHomes = [&](const FHomes& A, const FHomes& B) -> bool
			{
				if (A.Num() != B.Num()) { return false; }
				for (const TPair<int32, int32>& P : A) { const int32* Other = B.Find(P.Key); if (!Other || *Other != P.Value) { return false; } }
				return true;
			};
			const auto PerFloor = [&]() -> FString
			{
				int32 Residents[Floors + 1] = { 0, 0, 0, 0 };
				for (const FRHColonist& C : Sim->GetColonists()) { if (C.HomeHab == 0 && C.HomeLevel < 0 && C.HomeLevel >= -Floors) { ++Residents[-C.HomeLevel]; } }
				return FString::Printf(TEXT("floors -1/-2/-3: %d / %d / %d"), Residents[1], Residents[2], Residents[3]);
			};
			const bool AllRated[Floors + 1] = { false, true, true, true };

			// Settle: everything housed, every floor rated, five beds a cell. A sol passes with
			// nobody over a floor's beds, so nobody moves.
			Sim->Debug_SetColonistsPerCell(5.0);
			for (int32 L = 1; L <= Floors; ++L) { Sim->Debug_ForceRateFloor(-L); }
			RunSolsA(2.0);
			const FHomes Settled = Homes();
			const int32 PopE = Sim->GetPopulation();
			Expect(NoFloorOverBeds(), FString::Printf(TEXT("e settled: population %d, %s, nobody over a floor's beds"), PopE, *PerFloor()));

			// e1. Beds removed: every floor drops to Bnew beds, floor -1's newest are displaced.
			const int32 Bnew = FMath::Max(1, (PopE + Floors - 1) / Floors + 4);
			Sim->Debug_SetColonistsPerCell((Bnew + 0.5) / CellsPerFloor);
			const int32 Beds1[Floors + 1] = { 0, Bnew, Bnew, Bnew };
			FHomes Want1;
			const int32 Moved1Want = Predict(Settled, Beds1, AllRated, Want1);
			Expect(Moved1Want > 0 && Sim->GetHousingCapacity() == Floors * Bnew,
				FString::Printf(TEXT("e1 the beds are removed: %d beds a floor (%d in all) for %d residents, the model says %d must move"), Bnew, Sim->GetHousingCapacity(), PopE, Moved1Want));
			// Once a sol: nothing happens until the move-home clock crosses a whole sol.
			Alerts.Reset();
			const double Acc0 = Sim->GetMoveHomeAccSols();
			int32 MoveStep = -1;
			for (int32 K = 1; K <= 2 * StepsPerSolA && MoveStep < 0; ++K)
			{
				EraStepOnce();
				if (!SameHomes(Homes(), Settled)) { MoveStep = K; }
			}
			Expect(MoveStep >= 1 && Acc0 + MoveStep * DtSols >= 1.0 - 1e-9 && Acc0 + (MoveStep - 1) * DtSols < 1.0 + 1e-9,
				FString::Printf(TEXT("e1 once a sol: the clock read %.3f, nobody moved until step %d (%.3f + %d x %.3f reaches a whole sol)"), Acc0, MoveStep, Acc0, MoveStep, DtSols));
			const FHomes After1 = Homes();
			// The model is evaluated against the population as it was: births and arrivals are off, nobody leaves.
			Expect(SameHomes(After1, Want1) && Sim->GetPopulation() == PopE && NoFloorOverBeds(),
				FString::Printf(TEXT("e1 every resident is where the model puts them (the oldest hands keep their beds, the newest %d moved, highest Id first, to the shallowest free bed): %s"), Moved1Want, *PerFloor()));
			Expect(Alerts.Num() >= 1 && CountAlerts(TEXT("MOVED HOME")) == 1 && CountAlertsWith(*FString::Printf(TEXT("%d resident"), Moved1Want)) == 1,
				FString::Printf(TEXT("f one alert for the whole batch of %d, not one per person: %d MOVED HOME alert(s)"), Moved1Want, CountAlerts(TEXT("MOVED HOME"))));
			RunSolsA(3.0);
			Expect(CountAlerts(TEXT("MOVED HOME")) == 1 && SameHomes(Homes(), After1),
				FString::Printf(TEXT("e1 three sols on, no one else moves and there is no second alert (%d MOVED HOME alerts): the pass is idempotent"), CountAlerts(TEXT("MOVED HOME"))));

			// e2. A floor loses its rating: all of its residents are displaced.
			Sim->Debug_SetColonistsPerCell(5.0);
			RunSolsA(1.0);
			const FHomes Before2 = Homes();
			Sim->Debug_UnrateFloor(-1);
			bool bRated2[Floors + 1] = { false, false, true, true };
			const int32 Beds2[Floors + 1] = { 0, 1000, 1000, 1000 };
			FHomes Want2;
			const int32 Moved2Want = Predict(Before2, Beds2, bRated2, Want2);
			Alerts.Reset();
			const int32 Moved2 = Sim->Debug_RehomeResidents();
			Expect(Moved2 == Moved2Want && Moved2 > 0 && SameHomes(Homes(), Want2) && CountAlerts(TEXT("MOVED HOME")) == 1 && Alerts.Num() == 1,
				FString::Printf(TEXT("e2 floor -1 loses its rating: its residents move to floor -2 and -3 (%d moved, the model says %d), %s, %d alert(s)"), Moved2, Moved2Want, *PerFloor(), Alerts.Num()));
			// e3. Fewer beds than people: three floors of Bt beds hold seven fewer than the
			// population, so someone is displaced and the free beds run out - the rest stay.
			Sim->Debug_ForceRateFloor(-1);
			const int32 PopFull = Sim->GetPopulation();
			const int32 Bt = (PopFull - 7) / Floors;
			Sim->Debug_SetColonistsPerCell((Bt + 0.5) / CellsPerFloor);
			const FHomes Before3 = Homes();
			const int32 Beds3[Floors + 1] = { 0, Bt, Bt, Bt };
			FHomes Want3;
			int32 Displaced3 = 0;
			const int32 Moved3Want = Predict(Before3, Beds3, AllRated, Want3, &Displaced3);
			Alerts.Reset();
			const int32 Moved3 = Sim->Debug_RehomeResidents();
			Expect(Moved3 == Moved3Want && Moved3 > 0 && Displaced3 > Moved3 && SameHomes(Homes(), Want3) && Sim->GetPopulation() == PopFull && Alerts.Num() == 1,
				FString::Printf(TEXT("e3 %d beds a floor for %d residents: %d displaced, %d found a bed (the model says %d), %d stay with nowhere to go (population %d), %d alert(s)"),
					Bt, PopFull, Displaced3, Moved3, Moved3Want, Displaced3 - Moved3, Sim->GetPopulation(), Alerts.Num()));
			// No bed anywhere: nobody moves, no alert, nobody is lost.
			Sim->Debug_SetColonistsPerCell(0.0);
			const FHomes Before4 = Homes();
			Alerts.Reset();
			const int32 Moved4 = Sim->Debug_RehomeResidents();
			Expect(Moved4 == 0 && SameHomes(Homes(), Before4) && Alerts.Num() == 0 && Sim->GetPopulation() == PopFull,
				FString::Printf(TEXT("e4 no free bed anywhere: nobody moves (%d), nobody is lost (population %d), %d alert(s) - the shortage and recall rules take over"), Moved4, Sim->GetPopulation(), Alerts.Num()));
			// Restore.
			Sim->Debug_SetColonistsPerCell(5.0);
			for (int32 L = 1; L <= Floors; ++L) { Sim->Debug_ForceRateFloor(-L); }
			const int32 Moved5 = Sim->Debug_RehomeResidents();
			Expect(Moved5 == 0 && NoFloorOverBeds(), FString::Printf(TEXT("e with every floor rated and the beds back: a pass moves nobody (%d)"), Moved5));

			// e5. The cost of a big move: a city of 1,500 loses beds, hundreds move in one pass.
			Sim->Debug_AddColonists(1500 - Sim->GetPopulation());
			const int32 PopBig = Sim->GetPopulation();
			Sim->Debug_SetColonistsPerCell((600 + 0.5) / CellsPerFloor);
			Alerts.Reset();
			const double T0 = FPlatformTime::Seconds();
			const int32 MovedBig = Sim->Debug_RehomeResidents();
			const double MoveMs = (FPlatformTime::Seconds() - T0) * 1000.0;
			const double T1 = FPlatformTime::Seconds();
			const int32 MovedAgain = Sim->Debug_RehomeResidents();
			const double AgainMs = (FPlatformTime::Seconds() - T1) * 1000.0;
			Expect(PopBig >= 1400 && MovedBig > 100 && MovedAgain == 0 && NoFloorOverBeds() && CountAlerts(TEXT("MOVED HOME")) == 1 && Alerts.Num() == 1,
				FString::Printf(TEXT("e a city of %d, beds cut to 600 a floor: %d moved in one pass under one alert (%d alert(s)); a second pass moves %d"), PopBig, MovedBig, Alerts.Num(), MovedAgain));
			Expect(MoveMs <= 250.0 && AgainMs <= 100.0, FString::Printf(TEXT("e the move took %.2f ms and the idle pass %.2f ms (budgets 250 / 100 on a shared machine)"), MoveMs, AgainMs));
		}

		// (g) The Big Arrival and life support (C5c). The wave waits - its streak keeps running - until the
		// colony could carry N + 100: the O2 pool holds (N + 100) x the draw x BigArrivalLifeSupportSols, and the
		// Food runway (counting the wave's own provisions) and the Water runway at N + 100 reach the floor. Run
		// last, at the 1,500 the (e) block left, with the beds put back.
		{
			Sim->Debug_SetHopeGates(101.0, 101.0); // no births: the headcount moves only by the wave
			Sim->Debug_SetColonistsPerCell(5.0);
			for (int32 L = 1; L <= Floors; ++L) { Sim->Debug_ForceRateFloor(-L); }
			Sim->Debug_RehomeResidents();
			const double O2Draw = Sim->GetColonistO2KgPerSol(), FoodDraw = Sim->GetColonistFoodKgPerSol(), WaterDraw = Sim->GetColonistWaterKgPerSol();
			const double LifeSols = Sim->GetBigArrivalLifeSupportSols(), FoodFloor = Sim->GetFoodRunwayFloorSols();
			const double WaveFood = Sim->GetColonistTransportFoodKgPerColonist() * 100.0;
			const auto Rich = [&]() { SetStock(NOxygen, 1.0e8); SetStock(NWater, 1.0e8); SetStock(NFood, 1.0e8); };
			// A complete, held streak with only the beds missing; true if it got there.
			const auto ReadyStreak = [&]() -> bool
			{
				Sim->Debug_SetBigArrivalDone(false);
				Sim->Debug_SetBigArrivalFreeBeds(1000000);
				Rich();
				HoldHope(95.0);
				Lapse();
				RunSolsA(21.0);
				return Sim->GetBigArrivalStreakSols() >= 20.9 && !Sim->IsBigArrivalDone();
			};

			// g0. The arithmetic of each leg, at the instant (a pure function): Oxygen against (N + 100) x draw x sols,
			// Food and Water against the floor with the wave counted.
			Rich();
			const int32 NG = Sim->GetPopulation();
			const int32 PeopleG = NG + 100;
			const double NeedO2 = (double)PeopleG * O2Draw * LifeSols;
			URHSimWorldSubsystem::FRHBigArrivalSupport Sup = Sim->GetBigArrivalSupport();
			Expect(Sup.AllOk() && Sup.People == PeopleG && Sup.Wave == 100 && Near(Sup.O2NeedKg, NeedO2, 1e-6) && NG >= 1000 && Near(LifeSols, 10.0, 1e-9),
				FString::Printf(TEXT("g0 a city of %d, the wave 100: everyone to carry is %d, the air it needs is %.1f kg (= %d x %.2f x %.0f sols), all three legs ok when stocks are deep"), NG, Sup.People, Sup.O2NeedKg, PeopleG, O2Draw, LifeSols));
			SetStock(NOxygen, NeedO2 - 0.01);
			Sup = Sim->GetBigArrivalSupport();
			const bool bAirShort = !Sup.bO2Ok && Sup.bFoodOk && Sup.bWaterOk;
			SetStock(NOxygen, NeedO2);
			Sup = Sim->GetBigArrivalSupport();
			Expect(bAirShort && Sup.bO2Ok, TEXT("g0 air: 0.01 kg under the need is short, exactly the need is enough (>=)"));
			SetStock(NOxygen, 1.0e8);
			const double WaterNeed = FoodFloor * (double)PeopleG * WaterDraw;
			SetStock(NWater, WaterNeed - 1.0);
			Sup = Sim->GetBigArrivalSupport();
			const bool bWaterShort = !Sup.bWaterOk && Sup.bO2Ok && Sup.bFoodOk;
			SetStock(NWater, WaterNeed + 1.0);
			Sup = Sim->GetBigArrivalSupport();
			Expect(bWaterShort && Sup.bWaterOk && Near(Sup.WaterRunwaySols, (WaterNeed + 1.0) / ((double)PeopleG * WaterDraw), 1e-9),
				FString::Printf(TEXT("g0 water: 1 kg under %.0f kg (= %.0f sols of %d people's %.1f kg) is short, 1 kg over is enough"), WaterNeed, FoodFloor, PeopleG, WaterDraw));
			SetStock(NWater, 1.0e8);
			const double FoodNeed = FoodFloor * (double)PeopleG * FoodDraw - WaveFood; // the wave's own provisions already count
			SetStock(NFood, FoodNeed - 1.0);
			Sup = Sim->GetBigArrivalSupport();
			const bool bFoodShort = !Sup.bFoodOk && Sup.bO2Ok && Sup.bWaterOk;
			SetStock(NFood, FoodNeed + 1.0);
			Sup = Sim->GetBigArrivalSupport();
			Expect(FoodNeed > 0.0 && bFoodShort && Sup.bFoodOk && Near(Sup.FoodRunwaySols, (FoodNeed + 1.0 + WaveFood) / ((double)PeopleG * FoodDraw), 1e-9),
				FString::Printf(TEXT("g0 food: the larder must hold %.0f kg (%.0f sols of %d people's draw less the wave's own %.0f kg); 1 kg under is short, 1 kg over is enough (runway %.3f sols)"), FoodNeed, FoodFloor, PeopleG, WaveFood, Sup.FoodRunwaySols));
			Rich();

			// g1. Air and water short, food deep: the streak completes, the beds are there, the wave holds.
			const bool bReady1 = ReadyStreak();
			Sim->Debug_SetBigArrivalFreeBeds(100);
			Expect(bReady1 && Sim->GetFreeHousing() >= 100 && !Sim->GetCityDemand().bBigArrivalWaitsOnSupply && Sim->GetCityDemand().BigArrivalAirNote.IsEmpty() && Sim->GetCityDemand().BigArrivalRationsNote.IsEmpty(),
				FString::Printf(TEXT("g1 setup: a complete streak (%.2f sols), %d beds free, deep stocks; the wave is not held by life support, so the demand says nothing of it"), Sim->GetBigArrivalStreakSols(), Sim->GetFreeHousing()));
			const double NeedO2b = (double)(Sim->GetPopulation() + 100) * O2Draw * LifeSols;
			const double WaterNeedb = FoodFloor * (double)(Sim->GetPopulation() + 100) * WaterDraw;
			SetStock(NOxygen, 0.5 * NeedO2b);
			SetStock(NWater, 0.5 * WaterNeedb);
			const FRHCityDemand Held = Sim->GetCityDemand();
			Expect(Held.bBigArrivalWaitsOnSupply && Held.bBigArrivalShortAir && Held.bBigArrivalShortWater && !Held.bBigArrivalShortFood
				&& Held.BigArrivalAirNote == TEXT("The settlers are ready to come once the air reserve can carry 100 more.")
				&& Held.BigArrivalRationsNote == TEXT("The settlers are ready to come once the water store can carry 100 more."),
				FString::Printf(TEXT("g1 air and water at half: the demand says \"%s\" and \"%s\""), *Held.BigArrivalAirNote, *Held.BigArrivalRationsNote));
			const int32 PopHold = Sim->GetPopulation();
			const double StreakHold = Sim->GetBigArrivalStreakSols();
			Alerts.Reset();
			RunSolsA(3.0);
			Expect(!Sim->IsBigArrivalDone() && Sim->GetPopulation() == PopHold && CountAlerts(TEXT("THE BIG ARRIVAL")) == 0 && Sim->GetBigArrivalStreakSols() > StreakHold + 2.9,
				FString::Printf(TEXT("g1 three sols with the air and water short: the wave does not land (population %d, %d Big Arrival alert(s)) and the streak keeps running (%.2f -> %.2f)"), Sim->GetPopulation(), CountAlerts(TEXT("THE BIG ARRIVAL")), StreakHold, Sim->GetBigArrivalStreakSols()));
			// The air recovers; the water still holds it; then the water recovers and it lands, once.
			SetStock(NOxygen, 3.0 * NeedO2b);
			EraStepOnce();
			const FRHCityDemand AirBack = Sim->GetCityDemand();
			Expect(!Sim->IsBigArrivalDone() && Sim->GetPopulation() == PopHold && AirBack.bBigArrivalWaitsOnSupply && !AirBack.bBigArrivalShortAir && AirBack.BigArrivalAirNote.IsEmpty()
				&& AirBack.BigArrivalRationsNote == TEXT("The settlers are ready to come once the water store can carry 100 more."),
				TEXT("g1 the air recovers and the water alone holds the wave: the air note is gone, the water note stands"));
			SetStock(NWater, 3.0 * WaterNeedb);
			const int32 TopG = MaxColonistId();
			Alerts.Reset();
			EraStepOnce();
			Expect(Sim->IsBigArrivalDone() && Sim->GetPopulation() == PopHold + 100 && NewcomersFine(TopG, 100) && CountAlerts(TEXT("THE BIG ARRIVAL")) == 1 && Alerts.Num() == 2,
				FString::Printf(TEXT("g1 the water recovers: the very next step lands the wave once (population %d -> %d, %d alert(s))"), PopHold, Sim->GetPopulation(), Alerts.Num()));
			const FRHCityDemand After = Sim->GetCityDemand();
			Expect(!After.bBigArrivalPending && !After.bBigArrivalWaitsOnSupply && After.BigArrivalAirNote.IsEmpty() && After.BigArrivalRationsNote.IsEmpty(),
				TEXT("g1 landed: nothing is pending and the demand has no life-support note"));

			// g2. Food short, air and water deep, against the real floor of 10 sols: a new wave holds on the
			// larder, even with its own provisions; and food and water together read as one note.
			const bool bReady2 = ReadyStreak();
			Sim->Debug_SetBigArrivalFreeBeds(100);
			const int32 PopF = Sim->GetPopulation();
			const double FoodNeedF = FoodFloor * (double)(PopF + 100) * FoodDraw - WaveFood;
			const double WaterNeedF = FoodFloor * (double)(PopF + 100) * WaterDraw;
			SetStock(NFood, 0.4 * FoodNeedF);
			SetStock(NWater, 0.5 * WaterNeedF);
			const FRHCityDemand Both = Sim->GetCityDemand();
			Expect(bReady2 && FoodNeedF > 0.0 && Both.bBigArrivalWaitsOnSupply && Both.bBigArrivalShortFood && Both.bBigArrivalShortWater && !Both.bBigArrivalShortAir && Both.BigArrivalAirNote.IsEmpty()
				&& Both.BigArrivalRationsNote == TEXT("The settlers are ready to come once the food and water stores can carry 100 more."),
				FString::Printf(TEXT("g2 a second wave, food and water short: one rations note, \"%s\", and no air note"), *Both.BigArrivalRationsNote));
			SetStock(NWater, 1.0e8);
			const FRHCityDemand FoodOnly = Sim->GetCityDemand();
			Expect(FoodOnly.bBigArrivalShortFood && !FoodOnly.bBigArrivalShortWater && FoodOnly.BigArrivalRationsNote == TEXT("The settlers are ready to come once the food store can carry 100 more."),
				FString::Printf(TEXT("g2 water back: \"%s\""), *FoodOnly.BigArrivalRationsNote));
			Alerts.Reset();
			RunSolsA(1.0);
			Expect(!Sim->IsBigArrivalDone() && Sim->GetPopulation() == PopF && CountAlerts(TEXT("THE BIG ARRIVAL")) == 0,
				FString::Printf(TEXT("g2 a sol with the larder at %.0f kg of the %.0f it needs: the wave holds (population %d)"), Sim->GetStock(NFood), FoodNeedF, Sim->GetPopulation()));
			SetStock(NFood, 3.0 * FoodNeedF);
			Alerts.Reset();
			EraStepOnce();
			Expect(Sim->IsBigArrivalDone() && Sim->GetPopulation() == PopF + 100 && CountAlerts(TEXT("THE BIG ARRIVAL")) == 1,
				FString::Printf(TEXT("g2 the larder recovers: the wave lands on the next step (population %d -> %d)"), PopF, Sim->GetPopulation()));

			// g3. Supply short before the streak completes says nothing: the wave is not ready yet.
			Sim->Debug_SetBigArrivalDone(false);
			Sim->Debug_SetBigArrivalFreeBeds(100);
			Rich();
			HoldHope(95.0);
			Lapse();
			RunSolsA(5.0);
			SetStock(NOxygen, 1.0);
			const FRHCityDemand Early = Sim->GetCityDemand();
			Expect(Early.bBigArrivalPending && !Early.bBigArrivalWaitsOnSupply && Early.BigArrivalAirNote.IsEmpty() && Early.BigArrivalRationsNote.IsEmpty(),
				FString::Printf(TEXT("g3 five sols into the streak, with the air gone: the wave is pending but not yet waiting on life support (streak %.2f of 20)"), Sim->GetBigArrivalStreakSols()));
			Sim->Debug_SetBigArrivalDone(true);
			Rich();
		}

		Sim->OnAlert.Remove(AlertH);
		Sim->OnAlert.Remove(SnapH);
		Sim->OnShipArrived.Remove(ShipH);
		UE_LOG(LogRedHopeSim, Display, TEXT("=== ARRIVALS TEST END: %d check(s), %d failure(s) ==="), Checks, Fails);
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return Fails > 0 ? 1 : 0;
	}

	// C4 self-test (living-city gate 4, 2026-10-18): ROADS. The graph (merging,
	// splitting, junctions, crossings), the order and its preflight (coverage,
	// placement conflicts from both sides, the atomic bill), the labour-only
	// construction and the era refusal, deterministic routing (the same route twice,
	// ties, the same route after a save and a load), a road trip against the straight
	// line, the charge reserve, a road demolished under a robot, and the save round
	// trip of the graph and of a queued BuildRoad order with both end points. The
	// headless world has no Mass ticking, so robots are played by the harness through
	// the very same sim calls the brains make (TryClaimBuild, RH::MoveAlong,
	// ApplyRoadWork); the real robots are proven by the live capture and the paired
	// agent/era run. Fails loudly. `-roads`.
	if (FParse::Param(*Params, TEXT("roads")))
	{
		UE_LOG(LogRedHopeSim, Display, TEXT("=== ROADS TEST (C4, 2026-10-18) ==="));
		int32 Fails = 0, Checks = 0;
		const auto Expect = [&](bool bOk, const FString& What)
		{
			++Checks;
			if (bOk) { UE_LOG(LogRedHopeSim, Display, TEXT("ROADS ok: %s"), *What); }
			else { UE_LOG(LogRedHopeSim, Error, TEXT("ROADS FAIL: %s"), *What); ++Fails; }
		};
		const auto Near = [](double A, double B, double Tol) { return FMath::Abs(A - B) <= Tol; };
		constexpr double EraDt = (double)URHSimClockSubsystem::EraStepSimSeconds;
		constexpr double SolLen = (double)URHSimClockSubsystem::SolLengthSimSeconds;
		constexpr float AgentDt = URHSimClockSubsystem::SubStepSeconds;
		URHDefinitionsSubsystem* DefsSub = World->GetSubsystem<URHDefinitionsSubsystem>();
		const FName NStruct(TEXT("Struct"));
		FString Why;
		TArray<FString> Rejects;
		const FDelegateHandle RejectH = Sim->OnCommandRejected.AddLambda([&Rejects](const FRHCommand&, const FString& S) { Rejects.Add(S); });
		const auto LastReject = [&Rejects]() { return Rejects.Num() ? Rejects.Last() : FString(); };
		const auto Land = [&]()
		{
			Clock->Debug_AdvanceSimSeconds(EraDt);
			Sim->Debug_SetBatteryWh(1.0e9);
			Sim->EraStep((float)EraDt);
		};
		const auto Graph = [&]() -> const FRHRoadGraph& { return Sim->GetRoadGraph(); };
		const auto Have = [&]() { return Sim->GetRoadStructAvailableKg(); };
		int32 LanderId = 0;
		for (const FRHBuildingInstance& B : Sim->GetBuildings()) { if (B.DefName == FName("Lander")) { LanderId = B.Id; } }
		const auto RoadCmd = [](double X1, double Y1, double X2, double Y2)
		{
			FRHCommand C;
			C.Verb = FName("BuildRoad");
			C.Location = FVector(X1, Y1, 0.0);
			C.LocationB = FVector(X2, Y2, 0.0);
			return C;
		};
		// One BuildRoad order, one era step (the 60 s step covers the 45 s order lag).
		// True when it was not rejected.
		FString OrderNote;
		const auto OrderRoad = [&](double X1, double Y1, double X2, double Y2)
		{
			const int32 R0 = Rejects.Num();
			Sim->EnqueueCommand(RoadCmd(X1, Y1, X2, Y2));
			Land();
			OrderNote = Rejects.Num() == R0 ? FString(TEXT("accepted")) : Rejects.Last();
			return Rejects.Num() == R0;
		};
		const auto Can = [&](double X1, double Y1, double X2, double Y2, FString& Out)
		{
			return Sim->CanPlaceRoad(FVector(X1, Y1, 0.0), FVector(X2, Y2, 0.0), Out);
		};
		const auto DegreeOf = [&](int32 NodeId)
		{
			int32 D = 0;
			for (const FRHRoadEdge& E : Graph().Edges) { D += (E.A == NodeId || E.B == NodeId) ? 1 : 0; }
			return D;
		};
		const auto NodeAtCm = [&](double X, double Y)
		{
			return Graph().FindNodeAt(FIntPoint(FMath::RoundToInt(X / 200.0), FMath::RoundToInt(Y / 200.0)));
		};
		const auto RoadTasks = [&]()
		{
			int32 N = 0;
			for (const FRHTask& T : Sim->GetTasks()) { N += (T.Type == ERHTaskType::Build && T.To.RoadEdgeId > 0) ? 1 : 0; }
			return N;
		};
		const auto MixBytes = [](uint32& H, const void* P, int32 Bytes) { H = FCrc::MemCrc32(P, Bytes, H); };
		const auto GraphDigest = [&]() -> uint32
		{
			uint32 H = 0;
			const FRHRoadGraph& G = Graph();
			for (const FRHRoadNode& N : G.Nodes) { MixBytes(H, &N.Id, 4); MixBytes(H, &N.GridPos.X, 4); MixBytes(H, &N.GridPos.Y, 4); }
			for (const FRHRoadEdge& E : G.Edges)
			{
				const int32 Built = E.bBuilt ? 1 : 0;
				MixBytes(H, &E.Id, 4); MixBytes(H, &E.A, 4); MixBytes(H, &E.B, 4); MixBytes(H, &E.LengthCm, 4);
				MixBytes(H, &Built, 4); MixBytes(H, &E.BuildProgress, 4); MixBytes(H, &E.RegionId, 4);
			}
			MixBytes(H, &G.NextNodeId, 4); MixBytes(H, &G.NextEdgeId, 4);
			return H;
		};
		const auto SameRoute = [](const FRHRoadRoute& A, const FRHRoadRoute& B)
		{
			return A.bRoad == B.bRoad && A.NodeIds == B.NodeIds && A.EdgeIds == B.EdgeIds && A.CostUnits == B.CostUnits
				&& A.OffInCm == B.OffInCm && A.OffOutCm == B.OffOutCm && A.RoadCm == B.RoadCm;
		};
		const FRHRobotRow* FabRow = DefsSub ? DefsSub->GetRobot(FName("RC_F")) : nullptr;
		const float FabSpeed = FabRow ? FabRow->Speed_mps : 2.5f;
		const float FabWork = FabRow ? FabRow->WorkRate : 1.f;
		const float FabDrawW = FabRow ? FabRow->DrawMove_W : 90.f;

		// One harness Fabricator, played through the brains' own calls: claim the
		// nearest Build task, drive to its approach point, work it, complete it.
		struct FSimFab { FTransform T; FMassEntityHandle H; FRHTask Board; bool bHas = false; int32 Phase = 0; FVector Target = FVector::ZeroVector; };
		const auto FabStep = [&](FSimFab& F) -> int32
		{
			if (!F.bHas)
			{
				if (Sim->TryClaimBuild(F.H, F.T.GetLocation(), F.Board))
				{
					F.bHas = true;
					F.Phase = 0;
					F.Target = Sim->GetApproachPoint(F.Board.To, 0);
				}
				return 0;
			}
			if (F.Phase == 0)
			{
				if (RH::MoveAlong(*Sim, F.H, F.T, F.Target, FabSpeed, AgentDt)) { F.Phase = 1; }
				return 0;
			}
			const int32 Edge = F.Board.To.RoadEdgeId;
			const bool bDone = Edge > 0 ? Sim->ApplyRoadWork(Edge, AgentDt * FabWork) : true;
			if (bDone) { Sim->CompleteTask(F.Board.Id); F.bHas = false; return Edge; }
			return 0;
		};
		// A harness robot drives one trip with the shared helper: the sub-steps taken
		// (-1 past the cap).
		const auto Drive = [&](const FVector& From, const FVector& To, FMassEntityHandle H, float Dt)
		{
			FTransform T(FQuat::Identity, From);
			int32 Steps = 0;
			while (!RH::MoveAlong(*Sim, H, T, To, FabSpeed, Dt))
			{
				if (++Steps > 20000) { return -1; }
			}
			return Steps + 1;
		};
		const auto DriveStraight = [&](const FVector& From, const FVector& To, float Dt)
		{
			FTransform T(FQuat::Identity, From);
			int32 Steps = 0;
			while (!RH::MoveToward(T, To, FabSpeed, Dt))
			{
				if (++Steps > 20000) { return -1; }
			}
			return Steps + 1;
		};
		// The straight-line charge reserve the brain uses (RHRobotStateTree.cpp DriveWh).
		const auto ReserveWh = [&](double StraightCm)
		{
			const double Secs = StraightCm / FMath::Max(1.0, (double)FabSpeed * 100.0);
			return (float)(FabDrawW * Secs / 50.0 * 1.25) + FabDrawW * 0.5f;
		};
		int32 NextHandle = 100;
		const auto Fresh = [&]() { return FMassEntityHandle(NextHandle++, 1); };

		// R0. The data: every config row came off the table.
		{
			const FRHRoadParams& P = Sim->GetRoadParams();
			Expect(P.GridCm == 200 && P.WidthCm == 600 && P.MaxEdgeCm == 4000 && P.EntryCandidates == 4 && P.SpeedMulPermille == 1600
				&& Near(Sim->GetRoadStructKgPerMeter(), 2.0, 1e-9) && Near(Sim->GetRoadSpeedMul(), 1.6, 1e-9) && Sim->GetRoadBuildMetersPerHour() > 0.0
				&& FabRow != nullptr,
				FString::Printf(TEXT("R0 data: grid %d cm, width %d cm, max edge %d cm, K %d, speed x%.1f, %.1f kg/m, %.0f m per sol-hour (DT_Config rows)"),
					P.GridCm, P.WidthCm, P.MaxEdgeCm, P.EntryCandidates, Sim->GetRoadSpeedMul(), Sim->GetRoadStructKgPerMeter(), Sim->GetRoadBuildMetersPerHour()));
			Expect(LanderId != 0 && Graph().Nodes.Num() == 0 && Graph().Edges.Num() == 0 && !Sim->HasBuiltRoads(),
				TEXT("R0 the default colony has no road at all (no starter road)"));
		}

		// R1. With zero built edges the movement call sequence is exactly today's: the
		// helper and MoveToward agree bit for bit, every step, to the stop.
		{
			const FMassEntityHandle H = Fresh();
			FTransform A(FQuat::Identity, FVector(0.0, -3000.0, 0.0)), B = A;
			const FVector Goal(6000.0, -2600.0, 0.0);
			bool bSame = true, bArrA = false, bArrB = false;
			int32 N = 0;
			for (; N < 2000 && !(bArrA && bArrB); ++N)
			{
				bArrA = RH::MoveAlong(*Sim, H, A, Goal, FabSpeed, AgentDt);
				bArrB = RH::MoveToward(B, Goal, FabSpeed, AgentDt);
				bSame &= A.GetLocation() == B.GetLocation() && A.GetRotation().Equals(B.GetRotation(), 0.0) && bArrA == bArrB;
			}
			Expect(bSame && bArrA && bArrB && Sim->GetRobotRoadPath(H) == nullptr,
				FString::Printf(TEXT("R1 no built edge: RH::MoveAlong == RH::MoveToward bit for bit over %d steps, and no path is kept"), N));
		}

		// R2. Era mode does not learn road distance: ten sols of era ledger with a
		// finished network are the ten sols without it, to the last bit.
		{
			FString Err;
			Expect(Sim->SaveColony(TEXT("roadsbase"), Err), FString::Printf(TEXT("R2 saved the starting colony (%s)"), *Err));
			const auto Ledger = [&]() -> TArray<double>
			{
				TArray<double> L;
				for (const FName& Res : { FName("Struct"), FName("Regolith"), FName("Ore"), FName("Ice") }) { L.Add(Sim->GetTotalSolid(Res)); }
				for (const FName& Res : { FName("Water"), FName("Oxygen"), FName("Hydrogen") }) { L.Add(Sim->GetStock(Res)); }
				L.Add(Sim->GetPower().BatteryWh);
				for (const FRHDepositState& D : Sim->GetDeposits()) { L.Add(D.RemainingKg); L.Add(D.PileKg); }
				return L;
			};
			const int32 TenSols = 10 * (int32)(SolLen / EraDt);
			// Both legs start from a loaded copy, so nothing but the roads can differ.
			Expect(Sim->LoadColony(TEXT("roadsbase"), Err) && Err.IsEmpty(), FString::Printf(TEXT("R2 loaded the starting colony for the plain leg (%s)"), *Err));
			for (int32 K = 0; K < TenSols; ++K) { Land(); }
			const TArray<double> Plain = Ledger();
			Expect(Sim->LoadColony(TEXT("roadsbase"), Err) && Err.IsEmpty(), FString::Printf(TEXT("R2 reloaded the starting colony (%s)"), *Err));
			Sim->Debug_LayRoad(FVector(1000.0, -2000.0, 0.0), FVector(9000.0, -2000.0, 0.0), true);
			Sim->Debug_LayRoad(FVector(5000.0, -2000.0, 0.0), FVector(5000.0, 2000.0, 0.0), true);
			Expect(Sim->HasBuiltRoads() && Graph().Edges.Num() >= 4, FString::Printf(TEXT("R2 a finished network of %d edges stands"), Graph().Edges.Num()));
			for (int32 K = 0; K < TenSols; ++K) { Land(); }
			const TArray<double> Roaded = Ledger();
			bool bEqual = Plain.Num() == Roaded.Num() && Plain.Num() > 0;
			double MaxDelta = 0.0;
			for (int32 i = 0; bEqual && i < Plain.Num(); ++i) { MaxDelta = FMath::Max(MaxDelta, FMath::Abs(Plain[i] - Roaded[i])); bEqual &= Plain[i] == Roaded[i]; }
			Expect(bEqual && Plain[0] > 0.0,
				FString::Printf(TEXT("R2 the 10-sol era ledger is identical with and without roads (%d lines, max delta %.9f): era keeps its teleport model"), Plain.Num(), MaxDelta));
			Expect(Sim->LoadColony(TEXT("roadsbase"), Err) && Err.IsEmpty() && Graph().Edges.Num() == 0 && !Sim->HasBuiltRoads(),
				TEXT("R2 back to the starting colony, and a load clears the graph"));
		}

		// The world: three pylons stretch the grid east along the south side, and the
		// stores hold plenty of Struct for the roads (each test that needs less sets it).
		Sim->Debug_PlaceInstant(FName("Pylon"), FVector(3000.0, -3200.0, 0.0));
		Sim->Debug_PlaceInstant(FName("Pylon"), FVector(7000.0, -3200.0, 0.0));
		Sim->Debug_PlaceInstant(FName("Pylon"), FVector(11000.0, -3200.0, 0.0));
		Sim->Debug_AddSolidAt(LanderId, NStruct, 5000.0, false);
		Land();
		const double StructStart = Have();
		Expect(StructStart >= 5000.0, FString::Printf(TEXT("R3 the stores hold %.0f kg of Struct for roads"), StructStart));
		const auto SetStruct = [&](double Kg) { Sim->Debug_AddSolidAt(LanderId, NStruct, Kg - Have(), false); };

		// R3. The preflight: coverage, an empty order.
		{
			FString R;
			Expect(!Can(20000, -2000, 24000, -2000, R) && R.Contains(TEXT("coverage")), FString::Printf(TEXT("R3 outside the grid is refused (%s)"), *R));
			Expect(!Can(2000, -2000, 2050, -2040, R) && R.Contains(TEXT("two different")), FString::Printf(TEXT("R3 both ends on one grid cell is refused (%s)"), *R));
			Expect(!Can(1000, -2000, 24000, -2000, R) && R.Contains(TEXT("coverage")), FString::Printf(TEXT("R3 one end outside the grid is refused too (%s)"), *R));
			Expect(!Can(1000, -2000, 30000, -2000, R), FString::Printf(TEXT("R3 an over-long order is refused (%s)"), *R));
		}

		// R4. Road A: 40 m east-west. Two edges, the bill taken whole, a labour task per
		// edge, and no haul anywhere.
		{
			URHSimWorldSubsystem::FRHRoadPreview Pre;
			FString R;
			const bool bCan = Sim->CanPlaceRoad(FVector(1000, -2000, 0), FVector(5000, -2000, 0), R, &Pre);
			Expect(bCan && Pre.Pieces == 2 && Pre.LengthCm == 4000 && Near(Pre.BillKg, 80.0, 1e-9) && Near(Pre.HaveKg, StructStart, 1e-9) && Pre.NewSegmentsCm.Num() == 2 && Pre.Splits == 0,
				FString::Printf(TEXT("R4 preflight: %d pieces, %d cm, bill %.0f kg of the %.0f held, ghost has %d segments"), Pre.Pieces, Pre.LengthCm, Pre.BillKg, Pre.HaveKg, Pre.NewSegmentsCm.Num()));
			const double Before = Have();
			const int32 TasksBefore = RoadTasks();
			Expect(OrderRoad(1000, -2000, 5000, -2000), FString::Printf(TEXT("R4 the order landed (%s)"), *OrderNote));
			Expect(Graph().Nodes.Num() == 3 && Graph().Edges.Num() == 2 && !Graph().Edges[0].bBuilt && !Graph().Edges[1].bBuilt && !Sim->HasBuiltRoads(),
				FString::Printf(TEXT("R4 graph: %d nodes, %d edges, all planned"), Graph().Nodes.Num(), Graph().Edges.Num()));
			Expect(Graph().Edges[0].LengthCm == 2000 && Graph().Edges[1].LengthCm == 2000 && NodeAtCm(1000, -2000) != 0 && NodeAtCm(3000, -2000) != 0 && NodeAtCm(5000, -2000) != 0,
				TEXT("R4 two 20 m edges on the grid, joined at the 30 m node"));
			Expect(Near(Before - Have(), 80.0, 1e-6), FString::Printf(TEXT("R4 the bill left the stores whole: %.4f kg (expect 80)"), Before - Have()));
			Sim->Debug_StepTaskBoard();
			int32 RoadHauls = 0;
			for (const FRHTask& T : Sim->GetTasks()) { RoadHauls += (T.Type == ERHTaskType::Haul && (T.To.RoadEdgeId > 0 || T.From.RoadEdgeId > 0)) ? 1 : 0; }
			Expect(RoadTasks() == TasksBefore + 2 && RoadHauls == 0, FString::Printf(TEXT("R4 two labour tasks posted, no haul for a road (%d road hauls)"), RoadHauls));
		}

		// R5. Atomic payment: an order the stores cannot pay is refused and takes
		// nothing; one the stores can pay to the last kilogram is taken whole.
		{
			SetStruct(50.0);
			const uint32 D0 = GraphDigest();
			const int32 T0 = RoadTasks();
			FString R;
			Expect(!Can(2000, -6000, 12000, -6000, R) && R.Contains(TEXT("Insufficient Struct")), FString::Printf(TEXT("R5 the preflight refuses a 200 kg road on 50 kg (%s)"), *R));
			const bool bOk = OrderRoad(2000, -6000, 12000, -6000);
			Expect(!bOk && LastReject().Contains(TEXT("Insufficient Struct")) && GraphDigest() == D0 && RoadTasks() == T0 && Near(Have(), 50.0, 1e-9),
				FString::Printf(TEXT("R5 the order is refused at the seam and nothing moves: graph unchanged, no task, %.4f kg still held (%s)"), Have(), *LastReject()));
			SetStruct(20.0);
			const int32 E0 = Graph().Edges.Num();
			Expect(OrderRoad(12000, -2000, 13000, -2000) && Graph().Edges.Num() == E0 + 1 && Near(Have(), 0.0, 1e-9),
				FString::Printf(TEXT("R5 a 10 m road on exactly 20 kg is paid whole: %.9f kg left"), Have()));
			SetStruct(StructStart);
		}

		// R6. Splitting: a 100 m drag is cut into edges no longer than the cap.
		{
			URHSimWorldSubsystem::FRHRoadPreview Pre;
			FString R;
			Expect(Sim->CanPlaceRoad(FVector(2000, -6000, 0), FVector(12000, -6000, 0), R, &Pre) && Pre.Pieces == 3 && Pre.LengthCm == 10000 && Near(Pre.BillKg, 200.0, 1e-9),
				FString::Printf(TEXT("R6 preflight: a 100 m drag is %d pieces, %d cm, %.0f kg"), Pre.Pieces, Pre.LengthCm, Pre.BillKg));
			const int32 E0 = Graph().Edges.Num(), N0 = Graph().Nodes.Num();
			Expect(OrderRoad(2000, -6000, 12000, -6000), FString::Printf(TEXT("R6 the order landed (%s)"), *OrderNote));
			int32 Longest = 0, Sum = 0;
			for (int32 i = E0; i < Graph().Edges.Num(); ++i) { Longest = FMath::Max(Longest, Graph().Edges[i].LengthCm); Sum += Graph().Edges[i].LengthCm; }
			bool bOnGrid = true;
			for (int32 i = N0; i < Graph().Nodes.Num(); ++i) { bOnGrid &= Graph().Nodes[i].GridPos.X * 200 % 200 == 0 && NodeAtCm(Graph().Nodes[i].GridPos.X * 200.0, Graph().Nodes[i].GridPos.Y * 200.0) == Graph().Nodes[i].Id; }
			Expect(Graph().Edges.Num() == E0 + 3 && Graph().Nodes.Num() == N0 + 4 && Longest <= 4000 && Sum == 10000 && bOnGrid,
				FString::Printf(TEXT("R6 three edges (longest %d cm, total %d cm), four nodes, every node on the grid"), Longest, Sum));
		}

		// R7. Merging: a node only merges on exactly the same grid cell.
		{
			const int32 N0 = Graph().Nodes.Num();
			const int32 Shared = NodeAtCm(12000, -6000);
			Expect(Shared != 0 && DegreeOf(Shared) == 1, TEXT("R7 the long road ends on a free node"));
			Expect(OrderRoad(12000, -6000, 14000, -4000), FString::Printf(TEXT("R7 a road from that end landed (%s)"), *OrderNote));
			Expect(Graph().Nodes.Num() == N0 + 1 && DegreeOf(Shared) == 2 && NodeAtCm(12000, -6000) == Shared,
				FString::Printf(TEXT("R7 the shared end merged: %d new node (not 2), its degree %d"), Graph().Nodes.Num() - N0, DegreeOf(Shared)));
			const int32 Off = NodeAtCm(12200, -6000);
			Expect(Off == 0, TEXT("R7 a node one cell off is a different node (nothing within a cell merges)"));
		}

		// R8. A junction: an end point within half the road's width of an edge splits
		// that edge there. Only the new piece is billed; the far half of the split edge
		// is a new edge with its own labour task.
		{
			URHSimWorldSubsystem::FRHRoadPreview Pre;
			FString R;
			Expect(Sim->CanPlaceRoad(FVector(2000, -1800, 0), FVector(2000, -800, 0), R, &Pre) && Pre.Pieces == 1 && Pre.Splits == 1 && Pre.LengthCm == 1000 && Near(Pre.BillKg, 20.0, 1e-9),
				FString::Printf(TEXT("R8 preflight: %d new piece, %d split, %d cm, %.0f kg"), Pre.Pieces, Pre.Splits, Pre.LengthCm, Pre.BillKg));
			const int32 E0 = Graph().Edges.Num(), N0 = Graph().Nodes.Num(), T0 = RoadTasks();
			const double Before = Have();
			Expect(OrderRoad(2000, -1800, 2000, -800), FString::Printf(TEXT("R8 the order landed (%s)"), *OrderNote));
			const int32 J = NodeAtCm(2000, -1800);
			int32 HalfLen = 0, HalfCount = 0;
			for (const FRHRoadEdge& E : Graph().Edges) { if ((E.A == J || E.B == J) && E.LengthCm > 1010 && E.LengthCm < 1030) { HalfLen = E.LengthCm; ++HalfCount; } }
			Expect(J != 0 && Graph().Edges.Num() == E0 + 2 && Graph().Nodes.Num() == N0 + 2 && DegreeOf(J) == 3 && HalfCount == 2 && HalfLen == 1020,
				FString::Printf(TEXT("R8 the 20 m edge split at the junction into two %d cm halves; %d edges and %d nodes added, junction degree %d"), HalfLen, Graph().Edges.Num() - E0, Graph().Nodes.Num() - N0, DegreeOf(J)));
			Expect(Near(Before - Have(), 20.0, 1e-6) && RoadTasks() == T0 + 2,
				FString::Printf(TEXT("R8 billed for the new piece only (%.2f kg), a task for it and one for the far half (%d tasks added)"), Before - Have(), RoadTasks() - T0));
		}

		// R9. A crossing splits both roads at the crossing point.
		{
			URHSimWorldSubsystem::FRHRoadPreview Pre;
			FString R;
			Expect(Sim->CanPlaceRoad(FVector(4000, -3600, 0), FVector(4000, -400, 0), R, &Pre) && Pre.Pieces == 2 && Pre.Splits == 1 && Pre.LengthCm == 3200 && Near(Pre.BillKg, 64.0, 1e-9),
				FString::Printf(TEXT("R9 preflight: the drag is %d pieces after the crossing, %d split, %d cm, %.0f kg"), Pre.Pieces, Pre.Splits, Pre.LengthCm, Pre.BillKg));
			const int32 E0 = Graph().Edges.Num(), N0 = Graph().Nodes.Num();
			Expect(OrderRoad(4000, -3600, 4000, -400), FString::Printf(TEXT("R9 the order landed (%s)"), *OrderNote));
			const int32 X = NodeAtCm(4000, -2000);
			Expect(X != 0 && DegreeOf(X) == 4 && Graph().Edges.Num() == E0 + 3 && Graph().Nodes.Num() == N0 + 3,
				FString::Printf(TEXT("R9 the crossing is a node of degree %d; %d edges and %d nodes added (two pieces + the split of the old road)"), DegreeOf(X), Graph().Edges.Num() - E0, Graph().Nodes.Num() - N0));
		}

		// R10. Laying a road again changes nothing, even when it runs through a junction.
		{
			const uint32 D0 = GraphDigest();
			const double Before = Have();
			FString R;
			Expect(!Can(4000, -3600, 4000, -400, R) && R.Contains(TEXT("already laid")), FString::Printf(TEXT("R10 the same order again is refused (%s)"), *R));
			Expect(!Can(1000, -2000, 5000, -2000, R) && R.Contains(TEXT("already laid")), FString::Printf(TEXT("R10 a drag along a road that already has its junctions is refused (%s)"), *R));
			OrderRoad(4000, -3600, 4000, -400);
			Expect(GraphDigest() == D0 && Near(Have(), Before, 1e-9), TEXT("R10 and at the seam nothing changed and nothing was paid"));
		}

		// R11. Placement conflicts, from both sides.
		{
			FString R;
			Sim->Debug_PlaceInstant(FName("Pylon"), FVector(8000.0, -1600.0, 0.0));
			Expect(!Can(7000, -1600, 9000, -1600, R) && R.Contains(TEXT("run through")), FString::Printf(TEXT("R11 a road through a building is refused (%s)"), *R));
			Expect(!Can(7000, -1800, 9000, -1800, R), FString::Printf(TEXT("R11 a road whose strip clips a building is refused (%s)"), *R));
			Expect(Can(7000, -2000, 9000, -2000, R), FString::Printf(TEXT("R11 a road flush against a building is clear (%s)"), *R));
			Expect(!Sim->CanPlaceBuilding(FName("Pylon"), FVector(2000, -2100, 0), R) && R.Contains(TEXT("road being built")),
				FString::Printf(TEXT("R11 a building on a planned road is refused (%s)"), *R));
			Expect(!Sim->CanPlaceBuilding(FName("Pylon"), FVector(7000, -6350, 0), R) && R.Contains(TEXT("road")),
				FString::Printf(TEXT("R11 a building whose footprint clips a road strip is refused (%s)"), *R));
			Expect(Sim->CanPlaceBuilding(FName("Pylon"), FVector(7000, -6400, 0), R), FString::Printf(TEXT("R11 a building flush against a road strip is clear (%s)"), *R));
			// In flight, both ways.
			Sim->EnqueueCommand(RoadCmd(9000, -5000, 11000, -5000));
			const int32 RoadOrderId = Sim->GetUplinkQueue().Last().CommandId;
			Expect(!Sim->CanPlaceBuilding(FName("Pylon"), FVector(10000, -5000, 0), R) && R.Contains(TEXT("in transit")),
				FString::Printf(TEXT("R11 a building across a road order still in flight is refused (%s)"), *R));
			Expect(Sim->CancelUplinkCommand(RoadOrderId) && Sim->CanPlaceBuilding(FName("Pylon"), FVector(10000, -5000, 0), R), TEXT("R11 cancel the road order and the spot is free"));
			FRHCommand B;
			B.Verb = FName("Build");
			B.Target = FName("Pylon");
			B.Location = FVector(10000, -5000, 0);
			Sim->EnqueueCommand(B);
			const int32 BuildOrderId = Sim->GetUplinkQueue().Last().CommandId;
			Expect(!Can(9000, -5000, 11000, -5000, R) && R.Contains(TEXT("in transit")), FString::Printf(TEXT("R11 a road across a building order still in flight is refused (%s)"), *R));
			Expect(Sim->CancelUplinkCommand(BuildOrderId) && Can(9000, -5000, 11000, -5000, R), TEXT("R11 cancel the building order and the road is clear"));
			// At the seam: the road order that lands after a building took the spot is refused.
			const int32 R0 = Rejects.Num();
			Sim->EnqueueCommand(RoadCmd(7000, -1600, 9000, -1600));
			Land();
			Expect(Rejects.Num() == R0 + 1 && LastReject().Contains(TEXT("run through")), FString::Printf(TEXT("R11 the seam re-checks: the order that lands across a building is refused (%s)"), *LastReject()));
		}

		// R12. Labour: a Fabricator claims each edge, drives to its midpoint, and works
		// it. No Struct moves (it was paid at the order), and the era band refuses
		// 60x while an edge is open.
		{
			Expect(!Sim->CanEnterEraMode(Why) && Why.Contains(TEXT("road")), FString::Printf(TEXT("R12 era mode is refused while a road edge is under construction (%s)"), *Why));
			int32 Planned = 0;
			for (const FRHRoadEdge& E : Graph().Edges) { Planned += E.bBuilt ? 0 : 1; }
			const int32 TasksOpen = RoadTasks();
			Expect(Planned > 0 && TasksOpen == Planned, FString::Printf(TEXT("R12 %d planned edges, %d labour tasks: one each"), Planned, TasksOpen));
			const double StructBefore = Have();
			FSimFab F;
			F.T = FTransform(FQuat::Identity, FVector(-800.0, -600.0, 0.0));
			F.H = Fresh();
			int32 Done = 0, Steps = 0, FirstBuiltStep = -1;
			for (; Steps < 400000 && RoadTasks() > 0; ++Steps)
			{
				if (FabStep(F) != 0)
				{
					++Done;
					if (FirstBuiltStep < 0) { FirstBuiltStep = Steps; }
				}
			}
			int32 Open = 0;
			for (const FRHRoadEdge& E : Graph().Edges) { Open += E.bBuilt ? 0 : 1; }
			Expect(Open == 0 && RoadTasks() == 0 && Done == Planned && Sim->HasBuiltRoads(),
				FString::Printf(TEXT("R12 one Fabricator built all %d edges in %.0f sim-s (the first at %.0f s); none left open"), Done, Steps * (double)AgentDt, FirstBuiltStep * (double)AgentDt));
			Expect(Near(Have(), StructBefore, 1e-9), TEXT("R12 labour only: not a kilogram of Struct moved while the edges were built"));
			Why.Reset();
			const bool bEraOk = Sim->CanEnterEraMode(Why);
			Expect(bEraOk || !Why.Contains(TEXT("road")), FString::Printf(TEXT("R12 with every edge built the era band no longer refuses for roads (%s)"), bEraOk ? TEXT("allowed") : *Why));
			// The labour rate: a 20 m edge at RoadBuildMetersPerHour.
			const TArray<int32> Lone = Sim->Debug_LayRoad(FVector(20000, 2000, 0), FVector(22000, 2000, 0), false);
			int32 WorkSteps = 0;
			if (Lone.Num() == 1) { while (WorkSteps < 5000 && !Sim->ApplyRoadWork(Lone[0], AgentDt * FabWork)) { ++WorkSteps; } ++WorkSteps; }
			const double ExpectSteps = 20.0 / (Sim->GetRoadBuildMetersPerHour() / 50.0) / AgentDt;
			Expect(Lone.Num() == 1 && Near(WorkSteps, ExpectSteps, 3.0) && Graph().FindEdge(Lone[0]) && Graph().FindEdge(Lone[0])->bBuilt,
				FString::Printf(TEXT("R12 a 20 m edge took %d work steps (expect %.1f at %.0f m per sol-hour)"), WorkSteps, ExpectSteps, Sim->GetRoadBuildMetersPerHour()));
			Sim->Debug_RemoveRoadEdge(Lone.Num() ? Lone[0] : 0);
			Sim->Debug_FinishRoads();
		}

		// R13. Routes. Trip 1 runs along road A; the same plan twice; the same plan on a
		// copy of the graph; the trip arrives sooner than the straight line; and the
		// shared helper carries the leftover time across waypoints (a coarse step
		// arrives when a fine one does).
		const FVector S1(1000.0, -2400.0, 0.0), T1(5000.0, -2400.0, 0.0);
		int32 RoadSteps1 = 0, StraightSteps1 = 0;
		FRHRoadRoute Route1;
		{
			FRHRoadRoute A, B, C;
			const bool bA = Sim->PlanRoadRoute(S1, T1, A), bB = Sim->PlanRoadRoute(S1, T1, B);
			const FRHRoadGraph Copy = Graph();
			const bool bC = Copy.PlanRoute(FIntPoint(1000, -2400), FIntPoint(5000, -2400), Sim->GetRoadParams(), C) && C.bRoad;
			Route1 = A;
			Expect(bA && bB && bC && SameRoute(A, B) && SameRoute(A, C), TEXT("R13 the same route twice, and on a copy of the graph, is the same route"));
			Expect(A.OffInCm == 400 && A.OffOutCm == 400 && A.RoadCm == 4040 && A.NodeIds.Num() == 5 && A.EdgeIds.Num() == 4,
				FString::Printf(TEXT("R13 trip 1: %d cm off road in, %d road nodes, %d cm of road, %d cm off road out"), A.OffInCm, A.NodeIds.Num(), A.RoadCm, A.OffOutCm));
			const FMassEntityHandle H = Fresh();
			FTransform Tr(FQuat::Identity, S1);
			bool bPathOk = false;
			for (int32 K = 0; K < 10; ++K) { RH::MoveAlong(*Sim, H, Tr, T1, FabSpeed, AgentDt); }
			if (const FRHRobotRoadPath* P = Sim->GetRobotRoadPath(H))
			{
				bPathOk = P->IsRoadTrip() && P->Waypoints.Num() == A.NodeIds.Num() + 1 && P->LegEdge.Num() == P->Waypoints.Num() && P->LegEdge[0] == 0
					&& P->LegEdge[1] == A.EdgeIds[0] && P->Waypoints.Last() == T1 && P->TargetCm == T1;
			}
			Expect(bPathOk, TEXT("R13 the robot's stored path is entry..exit then the target, each leg naming its edge"));
			const FMassEntityHandle H2 = Fresh(), H3 = Fresh(), H4 = Fresh();
			RoadSteps1 = Drive(S1, T1, H2, AgentDt);
			StraightSteps1 = DriveStraight(S1, T1, AgentDt);
			const double ExpectRoad = ((double)A.OffInCm + A.RoadCm / Sim->GetRoadSpeedMul() + A.OffOutCm - 300.0) / (FabSpeed * 100.0) / AgentDt;
			Expect(RoadSteps1 > 0 && StraightSteps1 > 0 && RoadSteps1 < StraightSteps1 && Near(RoadSteps1, ExpectRoad, 2.0) && Sim->GetRobotRoadPath(H2) == nullptr,
				FString::Printf(TEXT("R13 the road trip arrived in %d steps against %d straight (the arithmetic says %.1f); its path is dropped on arrival"), RoadSteps1, StraightSteps1, ExpectRoad));
			// Leftover time carries across waypoints: ten seconds of driving ends in the
			// same place whether it is taken in 0.1 s, 0.5 s, 2.5 s or 10 s steps. (Without
			// the carry every waypoint would eat the rest of its step.)
			const auto After10s = [&](FMassEntityHandle Hh, float Dt)
			{
				FTransform Tr(FQuat::Identity, S1);
				const int32 N = FMath::RoundToInt(10.f / Dt);
				for (int32 K = 0; K < N; ++K) { RH::MoveAlong(*Sim, Hh, Tr, T1, FabSpeed, Dt); }
				return Tr.GetLocation();
			};
			const FVector Fine = After10s(H3, 0.1f), Half = After10s(H4, 0.5f), Wide = After10s(Fresh(), 2.5f), Whole = After10s(Fresh(), 10.f);
			const double Spread = FMath::Max(FMath::Max(FVector::Dist2D(Fine, Half), FVector::Dist2D(Fine, Wide)), FVector::Dist2D(Fine, Whole));
			Expect(Spread < 0.5 && FVector::Dist2D(Fine, S1) > 1500.0,
				FString::Printf(TEXT("R13 leftover time carries across waypoints: 10 s of driving ends %.0f cm along the trip whatever the step (0.1 / 0.5 / 2.5 / 10 s agree within %.4f cm)"), FVector::Dist2D(Fine, S1), Spread));
			FRHRoadRoute Short;
			const FVector S3(1000.0, -2400.0, 0.0), T3(1800.0, -2400.0, 0.0);
			Expect(!Sim->PlanRoadRoute(S3, T3, Short) && Drive(S3, T3, Fresh(), AgentDt) == DriveStraight(S3, T3, AgentDt),
				TEXT("R13 a short hop beside the road is not routed over it: the straight line is no slower, so it is driven straight"));
		}

		// R14. Ties, and the diamond. Two routes of equal cost: the lower node Id wins,
		// every time, on every copy.
		{
			Sim->Debug_LayRoad(FVector(30000, 0, 0), FVector(32000, 2000, 0), true);   // A-B
			Sim->Debug_LayRoad(FVector(32000, 2000, 0), FVector(34000, 0, 0), true);   // B-D
			Sim->Debug_LayRoad(FVector(30000, 0, 0), FVector(32000, -2000, 0), true);  // A-C
			Sim->Debug_LayRoad(FVector(32000, -2000, 0), FVector(34000, 0, 0), true);  // C-D
			const int32 NA = NodeAtCm(30000, 0), NB = NodeAtCm(32000, 2000), NC = NodeAtCm(32000, -2000), ND = NodeAtCm(34000, 0);
			FRHRoadRoute A, B, C;
			const FVector From(30000, 0, 0), To(34000, 0, 0);
			const bool bA = Sim->PlanRoadRoute(From, To, A);
			const bool bB = Sim->PlanRoadRoute(From, To, B);
			const FRHRoadGraph Copy = Graph();
			const bool bC = Copy.PlanRoute(FIntPoint(30000, 0), FIntPoint(34000, 0), Sim->GetRoadParams(), C) && C.bRoad;
			Expect(NA && NB && NC && ND && NB < NC && bA && bB && bC && SameRoute(A, B) && SameRoute(A, C),
				TEXT("R14 two equal routes: the same one three times"));
			Expect(A.NodeIds.Num() == 3 && A.NodeIds[0] == NA && A.NodeIds[1] == NB && A.NodeIds[2] == ND,
				FString::Printf(TEXT("R14 the tie goes to the lower node Id: via node %d (the other way is %d)"), A.NodeIds.Num() > 1 ? A.NodeIds[1] : 0, NC));
			// An equidistant start: the four nodes tie; the lowest Id is the entry.
			FRHRoadRoute E1, E2;
			const bool bE1 = Sim->PlanRoadRoute(FVector(32000, 0, 0), FVector(36000, 0, 0), E1);
			const bool bE2 = Sim->PlanRoadRoute(FVector(32000, 0, 0), FVector(36000, 0, 0), E2);
			Expect(bE1 == bE2 && SameRoute(E1, E2), TEXT("R14 an equidistant start plans the same route every time"));
		}

		// R15. The charge reserve. The brain keeps the straight-line reserve (a robot
		// heads home once its charge covers the straight drive home, with a margin).
		// A road trip is never longer in TIME than the straight line, so that reserve
		// covers the time-based drain along the road route: 40 trips in the network.
		{
			uint32 Rng = 20261018u;
			const auto Rand = [&Rng]() { Rng = Rng * 1664525u + 1013904223u; return (double)((Rng >> 8) & 0xFFFF) / 65535.0; };
			int32 RoadTrips = 0, Covered = 0, Total = 0;
			double Worst = 1.0e9;
			for (int32 K = 0; K < 60 && Total < 40; ++K)
			{
				const FVector S(Rand() * 6000.0, -4500.0 + Rand() * 5000.0, 0.0), T(Rand() * 6000.0, -4500.0 + Rand() * 5000.0, 0.0);
				const double Straight = FVector::Dist2D(S, T);
				if (Straight < 1500.0) { continue; }
				++Total;
				FRHRoadRoute R;
				const bool bRoad = Sim->PlanRoadRoute(S, T, R);
				const int32 Steps = Drive(S, T, Fresh(), AgentDt);
				const float DrainWh = Steps * AgentDt * FabDrawW / 50.f;
				RoadTrips += bRoad ? 1 : 0;
				const double Slack = ReserveWh(Straight) - DrainWh;
				Worst = FMath::Min(Worst, Slack);
				Covered += (Steps > 0 && Slack >= 0.0) ? 1 : 0;
			}
			Expect(Total == 40 && RoadTrips >= 3 && Covered == Total,
				FString::Printf(TEXT("R15 the straight-line reserve covers the road route's drain on %d of %d trips (%d of them on a road; the tightest has %.1f Wh to spare)"), Covered, Total, RoadTrips, Worst));
		}

		// R16. A road demolished under a robot slows it and does not strand it; one
		// demolished under a Fabricator moves it on. The refund follows the structure rule.
		{
			const FMassEntityHandle H = Fresh();
			FTransform Tr(FQuat::Identity, S1);
			const int32 Baseline = Drive(S1, T1, Fresh(), AgentDt);
			int32 Steps = 0;
			bool bArrived = false;
			for (; Steps < 50; ++Steps) { bArrived = RH::MoveAlong(*Sim, H, Tr, T1, FabSpeed, AgentDt); }
			const FRHRobotRoadPath* P = Sim->GetRobotRoadPath(H);
			int32 Under = 0;
			if (P && P->IsRoadTrip()) { for (int32 L = P->Next; L < P->LegEdge.Num(); ++L) { if (P->LegEdge[L] > 0) { Under = P->LegEdge[L]; break; } } }
			const FRHRoadEdge* EdgeRow = Under ? Graph().FindEdge(Under) : nullptr;
			const double RefundExpect = EdgeRow ? (double)EdgeRow->LengthCm / 100.0 * Sim->GetRoadStructKgPerMeter() * Sim->GetSalvageFraction() : -1.0;
			const double HaveBefore = Have();
			Expect(!bArrived && Under != 0 && EdgeRow && EdgeRow->bBuilt, FString::Printf(TEXT("R16 5 s into the trip the robot drives edge #%d"), Under));
			Expect(Sim->Debug_RemoveRoadEdge(Under) && Graph().FindEdge(Under) == nullptr && Near(Have() - HaveBefore, RefundExpect, 1e-6),
				FString::Printf(TEXT("R16 the edge is demolished under it; the standing road refunds %.2f kg (SalvageFraction of its bill)"), Have() - HaveBefore));
			for (; !bArrived && Steps < 5000; ++Steps) { bArrived = RH::MoveAlong(*Sim, H, Tr, T1, FabSpeed, AgentDt); }
			Expect(bArrived && Steps > Baseline && Steps <= Baseline + 20 && Sim->GetRobotRoadPath(H) == nullptr,
				FString::Printf(TEXT("R16 it still arrives, %d steps against %d undisturbed: slowed by the lost %.1fx on the rest of that leg, never stranded"), Steps, Baseline, Sim->GetRoadSpeedMul()));
			FRHRoadRoute After;
			Sim->PlanRoadRoute(S1, T1, After);
			Expect(!After.EdgeIds.Contains(Under), TEXT("R16 later plans no longer route over the demolished edge"));
			// A site demolished under a Fabricator.
			const TArray<int32> Site = Sim->Debug_LayRoad(FVector(24000, 2000, 0), FVector(26000, 2000, 0), false);
			FSimFab F;
			F.T = FTransform(FQuat::Identity, FVector(23800.0, 2000.0, 0.0));
			F.H = Fresh();
			int32 Guard = 0;
			while (!F.bHas && Guard++ < 100) { FabStep(F); }
			while (F.bHas && F.Phase == 0 && Guard++ < 50000) { FabStep(F); }
			const double HaveSite = Have();
			const double BillSite = Site.Num() ? Graph().FindEdge(Site[0])->LengthCm / 100.0 * Sim->GetRoadStructKgPerMeter() : -1.0;
			const bool bWorking = F.bHas && F.Phase == 1;
			Sim->Debug_RemoveRoadEdge(Site.Num() ? Site[0] : 0);
			const int32 Finished = FabStep(F);
			Expect(Site.Num() == 1 && bWorking && Finished == Site[0] && !F.bHas && Near(Have() - HaveSite, BillSite, 1e-6),
				FString::Printf(TEXT("R16 a site demolished under its Fabricator: it moves on, and the whole bill (%.0f kg) comes back"), Have() - HaveSite));
		}

		// R17. The save: the graph (with progress), a queued BuildRoad order with BOTH
		// end points, and routes and trips that re-plan to the same thing after a load.
		{
			const TArray<int32> Part = Sim->Debug_LayRoad(FVector(16000, -1000, 0), FVector(18000, -1000, 0), false);
			if (Part.Num() == 1) { for (int32 K = 0; K < 100; ++K) { Sim->ApplyRoadWork(Part[0], AgentDt); } }
			const FRHRoadEdge* PartEdge = Part.Num() ? Graph().FindEdge(Part[0]) : nullptr;
			const float PartProgress = PartEdge ? PartEdge->BuildProgress : 0.f;
			const bool bPartial = PartEdge && !PartEdge->bBuilt && PartProgress > 0.05f && PartProgress < 0.95f;
			Sim->EnqueueCommand(RoadCmd(13000, -1600, 14600, -2400)); // the second end point is the load-bearing one
			const FRHCommand Queued = Sim->GetUplinkQueue().Last();
			const FVector From(1000, -2400, 0), To(5000, -2400, 0);
			FRHRoadRoute RouteBefore, RouteAfter;
			const bool bPlanBefore = Sim->PlanRoadRoute(From, To, RouteBefore);
			const uint32 D0 = GraphDigest();
			const int32 E0 = Graph().Edges.Num(), N0 = Graph().Nodes.Num();
			// Where a fresh robot is after 50 steps of this trip.
			const auto After50 = [&](FMassEntityHandle H)
			{
				FTransform Tr(FQuat::Identity, From);
				for (int32 K = 0; K < 50; ++K) { RH::MoveAlong(*Sim, H, Tr, To, FabSpeed, AgentDt); }
				return Tr.GetLocation();
			};
			const FVector P50Before = After50(Fresh());
			FString Err;
			Expect(bPartial && Sim->SaveColony(TEXT("roadstest"), Err), FString::Printf(TEXT("R17 saved with an edge %.0f%% built and a BuildRoad order in the queue (%s)"), PartProgress * 100.f, *Err));
			// Change everything the save holds.
			Sim->Debug_LayRoad(FVector(40000, 0, 0), FVector(42000, 0, 0), true);
			Sim->Debug_RemoveRoadEdge(Graph().Edges.Num() ? Graph().Edges[0].Id : 0);
			Sim->CancelUplinkCommand(Queued.CommandId);
			Expect(GraphDigest() != D0 && Sim->GetUplinkQueue().Num() == 0, TEXT("R17 the graph and the queue were changed after the save"));
			Expect(Sim->LoadColony(TEXT("roadstest"), Err) && Err.IsEmpty(), FString::Printf(TEXT("R17 loaded (%s)"), *Err));
			Expect(GraphDigest() == D0 && Graph().Edges.Num() == E0 && Graph().Nodes.Num() == N0,
				FString::Printf(TEXT("R17 the graph round-trips: %d nodes, %d edges, every id, end, length, built flag, progress and next id exact"), Graph().Nodes.Num(), Graph().Edges.Num()));
			const FRHRoadEdge* PartAfter = Part.Num() ? Graph().FindEdge(Part[0]) : nullptr;
			Expect(PartAfter && PartAfter->BuildProgress == PartProgress && !PartAfter->bBuilt, TEXT("R17 the half-built edge keeps its progress to the bit"));
			bool bQueueOk = Sim->GetUplinkQueue().Num() == 1;
			if (bQueueOk)
			{
				const FRHCommand& Q = Sim->GetUplinkQueue()[0];
				bQueueOk = Q.Verb == FName("BuildRoad") && Q.Location == Queued.Location && Q.LocationB == Queued.LocationB
					&& Q.ExecuteAtSimSeconds == Queued.ExecuteAtSimSeconds && Q.LocationB != FVector::ZeroVector;
			}
			Expect(bQueueOk, TEXT("R17 the queued BuildRoad order survives with both end points"));
			// The same route, and a robot that re-plans to the same spot, before the order lands.
			const bool bPlanAfter = Sim->PlanRoadRoute(From, To, RouteAfter);
			Expect(bPlanBefore && bPlanAfter && SameRoute(RouteBefore, RouteAfter), TEXT("R17 the same trip plans the same route after the load"));
			Expect(After50(Fresh()) == P50Before, TEXT("R17 a robot re-plans after the load and stands on exactly the same spot after 50 steps"));
			// The order lands after the load and lays a road between its two ends.
			const int32 Tasks0 = RoadTasks();
			const int32 RejectsBefore = Rejects.Num();
			Land();
			OrderNote = Rejects.Num() == RejectsBefore ? FString(TEXT("accepted")) : Rejects.Last();
			Expect(NodeAtCm(13000, -1600) != 0 && NodeAtCm(14600, -2400) != 0 && RoadTasks() > Tasks0 && Graph().Edges.Num() > E0,
				FString::Printf(TEXT("R17 and it lands: a road from (130, -16) m to (146, -24) m, %d new labour tasks (%s)"), RoadTasks() - Tasks0, *OrderNote));
		}

		Sim->OnCommandRejected.Remove(RejectH);
		UE_LOG(LogRedHopeSim, Display, TEXT("=== ROADS TEST END: %d check(s), %d failure(s) ==="), Checks, Fails);
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return Fails > 0 ? 1 : 0;
	}

	// C5a self-test (living-city gate 5a, 2026-10-19): DISTRICTS AND DEMAND. `-districts`.
	// Fails loudly. The fixture is the -surfacehab recipe: a Lander (30 m of coverage) and a
	// Pylon (40 m), a shaft to -2 whose floor -1 rates, and a rated four-bay surface network
	// (two 2x1 shells edge to edge) beside a lone shell that straddles a district edge, with
	// eight colonists - four on floor -1, two in each of the network's shells. The checks:
	//  (a) paint (a rectangle of 10 m lattice cells, or a whole floor): a new district; the
	//      same Kind joins the one it overlaps and keeps its Id; adjacent is not overlapping;
	//      another Kind is refused; a rectangle over two districts is refused; the 32 x 32
	//      cap; every cell inside coverage; nothing new to paint; the floor rules;
	//  (b) erase and rename: Ids are never reused; names are trimmed, capped at 32, never
	//      empty, and "Gale" and "gale" stay two names; names are a pure function of the Id,
	//      with the "II" / "III" suffix once the forty are used;
	//  (c) membership of buildings, bays, underground cells and colonists, with a habitat
	//      that spans a district edge belonging to ONE district (its bay-0 cell);
	//  (d) each demand term on constructed cases, Housing against GetImmigrationCount's own
	//      room limits and the Big Arrival's free-bed gate, before and after the vault;
	//  (e) a save/load round trip, a queued RenameDistrict keeping its Text, queued orders
	//      keeping distinct CommandIds, and the cancel that now takes the order it names;
	//  (f) the derived caches are never stale; a colony that paints nothing is untouched.
	if (FParse::Param(*Params, TEXT("districts")))
	{
		UE_LOG(LogRedHopeSim, Display, TEXT("=== DISTRICTS TEST (C5a, 2026-10-19) ==="));
		// C5b: this suite paints Homes and Civic districts over a colony with material and demand, and its
		// checks are about the districts themselves; what the colony builds in them is the -citygrowth suite's.
		Sim->Debug_SetGrowthEnabled(false);
		int32 Fails = 0, Checks = 0;
		const auto Expect = [&](bool bOk, const FString& What)
		{
			++Checks;
			if (bOk) { UE_LOG(LogRedHopeSim, Display, TEXT("DISTRICTS ok: %s"), *What); }
			else { UE_LOG(LogRedHopeSim, Error, TEXT("DISTRICTS FAIL: %s"), *What); ++Fails; }
		};
		const auto Near = [](double A, double B, double Tol) { return FMath::Abs(A - B) <= Tol; };
		constexpr double EraDt = (double)URHSimClockSubsystem::EraStepSimSeconds;
		constexpr double SolLen = (double)URHSimClockSubsystem::SolLengthSimSeconds;
		const FName NHomes(TEXT("Homes")), NIndustry(TEXT("Industry")), NFarming(TEXT("Farming")), NResearch(TEXT("Research")), NCivic(TEXT("Civic"));
		const FName NOx(TEXT("Oxygen")), NFood(TEXT("Food"));
		FString Why, R, Note;
		TArray<FString> Rejects;
		const FDelegateHandle RejectH = Sim->OnCommandRejected.AddLambda([&Rejects](const FRHCommand&, const FString& S) { Rejects.Add(S); });
		const auto Land = [&]()
		{
			Clock->Debug_AdvanceSimSeconds(EraDt);
			Sim->Debug_SetBatteryWh(1.0e9);
			Sim->EraStep((float)EraDt);
		};
		const auto RunSols = [&](double Sols)
		{
			for (int32 S = 0; S < (int32)(Sols * SolLen / EraDt); ++S)
			{
				Sim->Debug_AddFreshWater(500.0 * EraDt / SolLen);
				Land();
			}
		};
		// One order, one era step (the 60 s step covers the 45 s order lag). True when it was not rejected.
		const auto Send = [&](const FRHCommand& C)
		{
			const int32 R0 = Rejects.Num();
			Sim->EnqueueCommand(C);
			Land();
			Note = Rejects.Num() == R0 ? FString(TEXT("accepted")) : Rejects.Last();
			return Rejects.Num() == R0;
		};
		const auto PaintCmd = [](FName Kind, int32 X1, int32 Y1, int32 X2, int32 Y2, int32 Level = 0)
		{
			FRHCommand C;
			C.Verb = FName("PaintDistrict");
			C.Target = Kind;
			C.Level = Level;
			C.Location = URHSimWorldSubsystem::LatticeCentreCm(FIntPoint(X1, Y1));
			C.LocationB = URHSimWorldSubsystem::LatticeCentreCm(FIntPoint(X2, Y2));
			return C;
		};
		const auto FloorCmd = [&](FName Kind, int32 Level) { return PaintCmd(Kind, 0, 0, 0, 0, Level); };
		const auto EraseCmd = [](int32 Id) { FRHCommand C; C.Verb = FName("EraseDistrict"); C.Value = (double)Id; return C; };
		const auto RenameCmd = [](int32 Id, const FString& Name) { FRHCommand C; C.Verb = FName("RenameDistrict"); C.Value = (double)Id; C.Text = Name; return C; };
		const auto Paint = [&](FName Kind, int32 X1, int32 Y1, int32 X2, int32 Y2) { return Send(PaintCmd(Kind, X1, Y1, X2, Y2)); };
		const auto CanPaint = [&](FName Kind, int32 X1, int32 Y1, int32 X2, int32 Y2, FString& Out, FRHDistrictPaintPreview* Pre = nullptr)
		{
			Out.Reset();
			return Sim->CanPaintDistrict(Kind, 0, URHSimWorldSubsystem::LatticeCentreCm(FIntPoint(X1, Y1)), URHSimWorldSubsystem::LatticeCentreCm(FIntPoint(X2, Y2)), Out, Pre);
		};
		const auto LatticeEq = [](const FIntPoint& P, int32 X, int32 Y) { return P.X == X && P.Y == Y; };
		const auto Find = [&](int32 Id) { return Sim->FindDistrict(Id); };
		const auto FirstOf = [&](const TCHAR* Def)
		{
			for (const FRHBuildingInstance& B : Sim->GetBuildings()) { if (B.DefName == FName(Def)) { return B.Id; } }
			return 0;
		};
		const auto NameOf = [&](int32 Id) { const FRHDistrict* D = Sim->FindDistrict(Id); return D ? D->Name : FString(); };
		const auto IdOf = [&](const TCHAR* Def, double X, double Y, int32 Level = 0)
		{
			for (const FRHBuildingInstance& B : Sim->GetBuildings())
			{
				if (B.DefName == FName(Def) && B.Level == Level && FMath::Abs(B.LocationCm.X - X) < 1.0 && FMath::Abs(B.LocationCm.Y - Y) < 1.0) { return B.Id; }
			}
			return 0;
		};
		const auto Place = [&](const TCHAR* Def, double X, double Y, int32 SX = 1, int32 SY = 1, int32 Level = 0)
		{
			Sim->Debug_PlaceInstant(FName(Def), FVector(X, Y, 0.0), Level, SX, SY);
			return IdOf(Def, X, Y, Level);
		};
		const auto MixBytes = [](uint32& H, const void* P, int32 Bytes) { H = FCrc::MemCrc32(P, Bytes, H); };
		const auto MixI = [&MixBytes](uint32& H, int32 V) { MixBytes(H, &V, 4); };
		const auto MixS = [&MixBytes](uint32& H, const FString& S) { MixBytes(H, *S, S.Len() * (int32)sizeof(TCHAR)); }; // case-sensitive, unlike the string CRCs
		const auto DistrictDigest = [&]() -> uint32
		{
			uint32 H = 0;
			MixI(H, Sim->GetNextDistrictId());
			for (const FRHDistrict& D : Sim->GetDistricts())
			{
				MixI(H, D.Id); MixS(H, D.Kind.ToString()); MixS(H, D.Name); MixI(H, D.RegionId); MixI(H, D.Level);
				MixI(H, D.Lattice.Num());
				for (const FIntPoint& P : D.Lattice) { MixI(H, P.X); MixI(H, P.Y); }
			}
			return H;
		};
		const auto SortedUnique = [](const TArray<FIntPoint>& L)
		{
			for (int32 i = 1; i < L.Num(); ++i)
			{
				if (!(L[i - 1].X < L[i].X || (L[i - 1].X == L[i].X && L[i - 1].Y < L[i].Y))) { return false; }
			}
			return true;
		};
		const auto CountsOf = [&](int32 Id) -> FRHDistrictCounts
		{
			TArray<FRHDistrictCounts> All;
			Sim->GetDistrictCounts(All);
			for (const FRHDistrictCounts& C : All) { if (C.DistrictId == Id) { return C; } }
			return FRHDistrictCounts();
		};

		// --- D0. A colony that paints nothing: inert. ---
		{
			TArray<FRHDistrictCounts> Counts;
			Sim->GetDistrictCounts(Counts);
			const FRHCityDemand Idle = Sim->GetCityDemand();
			Expect(Sim->GetDistricts().Num() == 0 && Sim->GetNextDistrictId() == 1 && Counts.Num() == 0 && Sim->GetDistrictOfBuilding(1) == 0
				&& Sim->GetDistrictAtCell(FIntPoint(0, 0)) == 0 && Sim->GetDistrictOfFloor(-1) == 0 && Sim->FindDistrict(1) == nullptr && Sim->Debug_DistrictCachesFresh(),
				TEXT("D0 a fresh colony has no districts, no counts and no membership; Id 1 is next"));
			Expect(Idle.Population == 0 && Near(Idle.Housing, 0.0, 0.0) && Near(Idle.Jobs, 0.0, 0.0) && Near(Idle.Services, 0.0, 0.0) && Near(Idle.Air, 0.0, 0.0)
				&& Idle.HousingBedsShort == 0 && Idle.ServiceGaps.Num() == 0 && Idle.HousingLimit.IsNone(),
				TEXT("D0 with nobody here the city wants nothing: every demand reads 0"));
			Send([&] { FRHCommand C; C.Verb = FName("Frobnicate"); return C; }());
			Expect(Note.Contains(TEXT("Unknown order")) && Note.Contains(TEXT("Frobnicate")), FString::Printf(TEXT("D0 an order no worker knows is refused, never executed (%s)"), *Note));
			Expect(LatticeEq(URHSimWorldSubsystem::LatticeOf(FVector(0, 0, 0)), 0, 0) && LatticeEq(URHSimWorldSubsystem::LatticeOf(FVector(499.9, 0, 0)), 0, 0)
				&& LatticeEq(URHSimWorldSubsystem::LatticeOf(FVector(500.0, 0, 0)), 1, 0) && LatticeEq(URHSimWorldSubsystem::LatticeOf(FVector(-500.0, 0, 0)), 0, 0)
				&& LatticeEq(URHSimWorldSubsystem::LatticeOf(FVector(-501.0, -1500.0, 0)), -1, -1) && LatticeEq(URHSimWorldSubsystem::LatticeOf(FVector(3000.0, -1000.0, 0)), 3, -1),
				TEXT("D0 a lattice cell spans [10k - 5, 10k + 5) m: the habitat bays' own half-open floor"));
		}

		// --- D1. Names: a pure function of the Id. ---
		{
			const int32 Count = RHDistricts::GetNameCount();
			TSet<FString> Distinct;
			for (int32 Id = 1; Id <= Count; ++Id) { Distinct.Add(RHDistricts::NameForId(Id)); }
			Expect(Count == 40 && Distinct.Num() == 40 && RHDistricts::NameForId(1) == TEXT("Gale") && RHDistricts::NameForId(2) == TEXT("Jezero")
				&& RHDistricts::NameForId(3) == TEXT("Holden") && RHDistricts::NameForId(40) == TEXT("Protonilus"),
				FString::Printf(TEXT("D1 forty distinct Martian place names, walked by Id (1 %s, 2 %s, 40 %s)"), *RHDistricts::NameForId(1), *RHDistricts::NameForId(2), *RHDistricts::NameForId(40)));
			Expect(RHDistricts::NameForId(41) == TEXT("Gale II") && RHDistricts::NameForId(42) == TEXT("Jezero II") && RHDistricts::NameForId(80) == TEXT("Protonilus II")
				&& RHDistricts::NameForId(81) == TEXT("Gale III") && RHDistricts::NameForId(121) == TEXT("Gale IV") && RHDistricts::NameForId(161) == TEXT("Gale V")
				&& RHDistricts::NameForId(361) == TEXT("Gale X") && RHDistricts::NameForId(481) == TEXT("Gale XIII"),
				FString::Printf(TEXT("D1 after the forty the walk starts again with a roman suffix (41 %s, 81 %s, 121 %s, 361 %s)"), *RHDistricts::NameForId(41), *RHDistricts::NameForId(81), *RHDistricts::NameForId(121), *RHDistricts::NameForId(361)));
			Expect(RHDistricts::RomanSuffix(1).IsEmpty() && RHDistricts::RomanSuffix(0).IsEmpty() && RHDistricts::RomanSuffix(2) == TEXT("II") && RHDistricts::RomanSuffix(4) == TEXT("IV")
				&& RHDistricts::RomanSuffix(9) == TEXT("IX") && RHDistricts::RomanSuffix(14) == TEXT("XIV") && RHDistricts::RomanSuffix(40) == TEXT("XL") && RHDistricts::RomanSuffix(90) == TEXT("XC")
				&& RHDistricts::RomanSuffix(1994) == TEXT("MCMXCIV") && RHDistricts::RomanSuffix(3999) == TEXT("MMMCMXCIX") && RHDistricts::RomanSuffix(4000).IsEmpty(),
				TEXT("D1 the suffix is a roman numeral from II (none for 1, none past 3999)"));
			Expect(RHDistricts::NameForId(7) == RHDistricts::NameForId(7) && RHDistricts::NameForId(1000) == RHDistricts::NameForId(1000) && RHDistricts::NameForId(0) == TEXT("Gale"),
				TEXT("D1 the same Id is always the same name"));
			const TArray<FName>& Kinds = RHDistricts::GetKinds();
			Expect(Kinds.Num() == 5 && RHDistricts::ResolveKind(NHomes) == NHomes && RHDistricts::ResolveKind(NCivic) == NCivic && RHDistricts::ResolveKind(FName("Mining")).IsNone()
				&& RHDistricts::ResolveKind(NAME_None).IsNone(), TEXT("D1 five kinds: Homes, Industry, Farming, Research, Civic; anything else is none"));
		}

		// --- D2. Painting the surface. Only the Lander (30 m of coverage) stands. ---
		{
			FRHDistrictPaintPreview Pre;
			// The ghost before the order: it is a preflight, it changes nothing.
			Expect(CanPaint(NHomes, -2, -2, 2, 2, Why, &Pre) && Pre.CellsInRect == 25 && Pre.CellsNew == 25 && Pre.CellsOutside == 0 && Pre.JoinId == 0 && Pre.Min == FIntPoint(-2, -2) && Pre.Max == FIntPoint(2, 2),
				FString::Printf(TEXT("D2 the preflight for a 5 x 5 rectangle round the Lander: 25 cells, all new, none outside (%s)"), *Why));
			const FRHCommand First = PaintCmd(NHomes, 2, 2, -2, -2); // corners in either order
			Sim->EnqueueCommand(First);
			Expect(Sim->GetDistricts().Num() == 0, TEXT("D2 the order is in flight: nothing painted until the signal lands"));
			Land();
			const FRHDistrict* D1 = Find(1);
			Expect(Sim->GetDistricts().Num() == 1 && D1 && D1->Kind == NHomes && D1->Level == 0 && D1->RegionId == 0 && D1->Lattice.Num() == 25 && SortedUnique(D1->Lattice)
				&& D1->Name == TEXT("Gale") && Sim->GetNextDistrictId() == 2,
				TEXT("D2 the order lands: district #1 'Gale', Homes, 25 sorted cells, region 0, surface"));
			Expect(Sim->GetDistrictAtCell(FIntPoint(0, 0)) == 1 && Sim->GetDistrictAtCell(FIntPoint(-2, 2)) == 1 && Sim->GetDistrictAtCell(FIntPoint(3, 0)) == 0
				&& Sim->GetDistrictAtPoint(0, FVector(0, 0, 0)) == 1 && Sim->GetDistrictAtPoint(0, FVector(1999, 0, 0)) == 1 && Sim->GetDistrictAtPoint(0, FVector(2501, 0, 0)) == 0,
				TEXT("D2 the ground reads back: cell (0,0) and (-2,2) are #1, (3,0) is open"));
			// The same Kind joins the district it overlaps, and keeps its Id.
			Expect(CanPaint(NHomes, 2, 0, 3, 0, Why, &Pre) && Pre.JoinId == 1 && Pre.CellsNew == 1 && Pre.CellsInRect == 2, TEXT("D2 painting Homes over its own edge: the preview says it joins #1 with one new cell"));
			Expect(Paint(NHomes, 2, 0, 3, 0) && Sim->GetDistricts().Num() == 1 && Find(1) && Find(1)->Lattice.Num() == 26 && SortedUnique(Find(1)->Lattice) && Sim->GetDistrictAtCell(FIntPoint(3, 0)) == 1
				&& Sim->GetNextDistrictId() == 2, FString::Printf(TEXT("D2 same Kind joins: still one district (#1 keeps its Id), now 26 cells (%s)"), *Note));
			// Adjacent is not overlapping: it founds its own district.
			Expect(Paint(NHomes, 0, 3, 0, 3) && Sim->GetDistricts().Num() == 2 && Find(2) && Find(2)->Kind == NHomes && Find(2)->Lattice.Num() == 1 && Find(2)->Name == TEXT("Jezero")
				&& Sim->GetDistrictAtCell(FIntPoint(0, 3)) == 2, FString::Printf(TEXT("D2 a Homes cell that only touches #1 founds #2 'Jezero' (%s)"), *Note));
			// Another Kind over it: refused, with the reason, and at the seam too.
			Expect(!CanPaint(NIndustry, 1, 1, 1, 1, Why) && Why.Contains(TEXT("Homes")) && Why.Contains(TEXT("Gale")), FString::Printf(TEXT("D2 the preflight refuses Industry over Homes (%s)"), *Why));
			Expect(!Paint(NIndustry, 1, 1, 1, 1) && Note.Contains(TEXT("Homes")) && Sim->GetDistricts().Num() == 2 && Find(1)->Kind == NHomes && Find(1)->Lattice.Num() == 26,
				FString::Printf(TEXT("D2 the uplink refuses it too, naming the district (%s); #1 is untouched"), *Note));
			// Two districts under one rectangle: refused.
			Expect(!CanPaint(NHomes, 0, 2, 0, 3, Why) && Why.Contains(TEXT("two districts")) && Why.Contains(TEXT("Gale")) && Why.Contains(TEXT("Jezero")),
				FString::Printf(TEXT("D2 a rectangle over #1 and #2 is refused, even of their own Kind (%s)"), *Why));
			Expect(!Paint(NHomes, 0, 2, 0, 3) && Note.Contains(TEXT("two districts")) && Sim->GetDistricts().Num() == 2, FString::Printf(TEXT("D2 the uplink refuses it too (%s)"), *Note));
			// Nothing new to paint.
			Expect(!CanPaint(NHomes, -1, -1, 1, 1, Why, &Pre) && Why.Contains(TEXT("already part of Gale")) && Pre.CellsNew == 0 && Pre.JoinId == 1, FString::Printf(TEXT("D2 painting ground a district already holds is refused (%s)"), *Why));
			// The cap: 32 x 32 cells an order.
			Expect(!CanPaint(NHomes, 10, 10, 42, 42, Why, &Pre) && Why.Contains(TEXT("at a time")) && Pre.CellsInRect == 0, FString::Printf(TEXT("D2 33 x 33 is over the cap (%s)"), *Why));
			Expect(!CanPaint(NHomes, 0, 10, 32, 10, Why) && Why.Contains(TEXT("at a time")) && !CanPaint(NHomes, 0, 10, 0, 42, Why) && Why.Contains(TEXT("at a time")),
				TEXT("D2 33 x 1 and 1 x 33 are over the cap too"));
			Expect(!CanPaint(NHomes, 10, 10, 41, 41, Why, &Pre) && !Why.Contains(TEXT("at a time")) && Why.Contains(TEXT("coverage")) && Pre.CellsInRect == 1024 && Pre.CellsOutside == 1024,
				FString::Printf(TEXT("D2 32 x 32 is within the cap (1,024 cells) and fails on coverage instead (%s)"), *Why));
			Expect(!Paint(NHomes, 0, 10, 32, 10) && Note.Contains(TEXT("at a time")), TEXT("D2 the uplink applies the cap too"));
			// Coverage: every cell's centre inside the grid's reach.
			Expect(!CanPaint(NIndustry, 4, 0, 4, 0, Why, &Pre) && Why.Contains(TEXT("coverage")) && Pre.CellsOutside == 1 && Pre.CellsInRect == 1,
				FString::Printf(TEXT("D2 one cell 40 m out is past the Lander's 30 m (%s)"), *Why));
			Expect(!CanPaint(NHomes, 3, 3, 3, 3, Why) && Why.Contains(TEXT("coverage")), TEXT("D2 the corner (3,3) is 42 m out: refused on coverage"));
			Expect(!Paint(NIndustry, 4, 0, 4, 0) && Note.Contains(TEXT("coverage")) && Sim->GetDistricts().Num() == 2, FString::Printf(TEXT("D2 the uplink refuses it on coverage too (%s)"), *Note));
			Place(TEXT("Pylon"), 5000, 0); // 40 m of reach about (50, 0) m
			Expect(CanPaint(NIndustry, 4, -1, 5, 1, Why, &Pre) && Pre.CellsOutside == 0 && Pre.CellsNew == 6, FString::Printf(TEXT("D2 a Pylon at (50, 0) m covers the ground round it (%s)"), *Why));
			Expect(Paint(NIndustry, 4, -1, 5, 1) && Find(3) && Find(3)->Kind == NIndustry && Find(3)->Lattice.Num() == 6 && Find(3)->Name == TEXT("Holden") && Sim->GetDistricts().Num() == 3,
				FString::Printf(TEXT("D2 Industry founds #3 'Holden': 6 cells (%s)"), *Note));
			// A kind that does not exist, and a floor that does not.
			Expect(!Paint(FName("Mining"), -1, -1, -1, -1) && Note.Contains(TEXT("not a district kind")) && !Sim->CanPaintDistrict(NHomes, 1, FVector::ZeroVector, FVector::ZeroVector, Why) && Why.Contains(TEXT("No such floor")),
				TEXT("D2 a sixth kind and a floor above the surface are refused"));
			Expect(Sim->GetDistricts().Num() == 3 && Sim->GetNextDistrictId() == 4, TEXT("D2 three districts so far; refusals consumed no Id"));
		}

		// --- D3. Erase, Ids never reused, rename. ---
		{
			Expect(!Send(EraseCmd(99)) && Note.Contains(TEXT("No such district")) && Sim->GetDistricts().Num() == 3, FString::Printf(TEXT("D3 erasing a district that is not there is refused (%s)"), *Note));
			Expect(Send(EraseCmd(2)) && Sim->GetDistricts().Num() == 2 && Find(2) == nullptr && Sim->GetDistrictAtCell(FIntPoint(0, 3)) == 0 && Find(1) && Find(3),
				FString::Printf(TEXT("D3 EraseDistrict #2: it and its ground are gone, #1 and #3 stand (%s)"), *Note));
			Expect(Paint(NCivic, 0, 3, 0, 3) && Find(4) && Find(4)->Name == TEXT("Eberswalde") && Find(4)->Kind == NCivic && Find(2) == nullptr && Sim->GetNextDistrictId() == 5,
				TEXT("D3 the next district is #4 'Eberswalde': an erased Id (#2) is never reused"));
			// Rename.
			Expect(Send(RenameCmd(1, TEXT("  Olympus Mons  "))) && NameOf(1) == TEXT("Olympus Mons"), FString::Printf(TEXT("D3 rename trims: '%s'"), *NameOf(1)));
			const FString Forty = TEXT("ABCDEFGHIJabcdefghijABCDEFGHIJabcdefghij"); // 40 characters
			Expect(Send(RenameCmd(3, Forty)) && NameOf(3).Len() == 32 && NameOf(3) == Forty.Left(32), FString::Printf(TEXT("D3 rename caps at 32 characters: '%s'"), *NameOf(3)));
			Expect(Send(RenameCmd(3, TEXT("   ") + Forty + TEXT("   "))) && NameOf(3) == Forty.Left(32), FString::Printf(TEXT("D3 the trim comes before the cap: spaces round a 40-character name cost it nothing ('%s')"), *NameOf(3)));
			Expect(Send(RenameCmd(3, TEXT("Thirty-one-characters-no-spaces tail"))) && NameOf(3) == TEXT("Thirty-one-characters-no-spaces") && NameOf(3).Len() == 31,
				FString::Printf(TEXT("D3 a cut that lands on a space is trimmed again, never a trailing space ('%s')"), *NameOf(3)));
			const FString Before = NameOf(1);
			Expect(!Send(RenameCmd(1, TEXT("   "))) && Note.Contains(TEXT("cannot be empty")) && NameOf(1) == Before, FString::Printf(TEXT("D3 an empty name is refused and the old one stays (%s)"), *Note));
			Expect(!Send(RenameCmd(1, FString())) && NameOf(1) == Before && !Send(RenameCmd(77, TEXT("Nowhere"))) && Note.Contains(TEXT("No such district")), TEXT("D3 a blank name and an unknown district are refused"));
			Expect(Send(RenameCmd(4, TEXT("Hel\nlo\tWorld"))) && NameOf(4) == TEXT("HelloWorld"), FString::Printf(TEXT("D3 control characters are dropped, so a name stays on one line ('%s')"), *NameOf(4)));
			// Case: two names that differ only by case stay two names (an FName would fold them).
			Expect(Send(RenameCmd(1, TEXT("gale"))) && Send(RenameCmd(3, TEXT("GALE"))) && Send(RenameCmd(4, TEXT("Gale"))),
				TEXT("D3 three renames: gale, GALE and Gale"));
			const FString N1 = NameOf(1), N3 = NameOf(3), N4 = NameOf(4);
			Expect(N1 == TEXT("gale") && N3 == TEXT("GALE") && N4 == TEXT("Gale") && !N1.Equals(N3, ESearchCase::CaseSensitive) && !N1.Equals(N4, ESearchCase::CaseSensitive)
				&& !N3.Equals(N4, ESearchCase::CaseSensitive) && FName(*N1) == FName(*N3), // the last term is the trap: as FNames they are the same
				FString::Printf(TEXT("D3 names that differ only by case stay distinct as typed: '%s' '%s' '%s'"), *N1, *N3, *N4));
			Send(RenameCmd(1, TEXT("Gale"))); Send(RenameCmd(3, TEXT("Holden"))); Send(RenameCmd(4, TEXT("Eberswalde")));
			Expect(NameOf(1) == TEXT("Gale") && NameOf(3) == TEXT("Holden") && NameOf(4) == TEXT("Eberswalde"), TEXT("D3 renamed back to the generated names (duplicates would be allowed; these are not)"));
			Expect(!Send(FloorCmd(NHomes, -1)) && Note.Contains(TEXT("not reached")) && Sim->GetDistricts().Num() == 3, FString::Printf(TEXT("D3 a floor the shaft has not reached is refused (%s)"), *Note));
		}

		// --- Setup: power, stock, a shaft to -2, floor -1 rated, a rated four-bay network, a lone shell, eight colonists. ---
		const FVector Head(1000.f, 1000.f, 0.f);
		Place(TEXT("SolarArray"), 3500, 1000);   // lattice (4, 1): inside Industry #3
		Place(TEXT("SolarArray"), 3500, 2000);   // lattice (4, 2): open ground
		Place(TEXT("BatteryBank"), 1000, 3500);  // lattice (1, 4): open ground
		const int32 ForgeId = Place(TEXT("Forge"), 0, 1000); // lattice (0, 1): inside Homes #1
		Sim->AddStock(FName("Struct"), 5000.0);
		Sim->AddStock(NOx, 6000.0);
		Sim->AddStock(FName("Water"), 4000.0);
		Sim->AddStock(NFood, 6000.0);
		Sim->Debug_SetBigArrivalDone(true); // out of the way until the Housing cases bring it back
		Sim->ExtendShaft(2, Head);
		Sim->ExcavateFloor(-1, 4, R);
		Sim->ExcavateFloor(-2, 3, R);
		const int32 FloorFilter = Place(TEXT("AirFilter"), 1000, 1500, 1, 1, -1);
		const int32 Hab1 = Place(TEXT("HabModule"), 3500, 0, 2, 1);      // bays (3,0) and (4,0): bay 0 in Homes #1, bay 1 in Industry #3
		const int32 Hab3 = Place(TEXT("HabModule"), 3500, -1000, 2, 1);  // bays (3,-1) and (4,-1): bay 0 in open ground, bay 1 in Industry #3
		const int32 Hab4 = Place(TEXT("HabModule"), 5500, 1000, 2, 1);   // bays (5,1) and (6,1): bay 0 in Industry #3, bay 1 in open ground; alone, so never rated
		const int32 HabFilter = Place(TEXT("AirFilter"), 3000, 0);       // inside Hab1's bay 0
		Expect(ForgeId && FloorFilter && Hab1 && Hab3 && Hab4 && HabFilter, TEXT("setup: the Forge, the floor filter, three shells and a surface filter placed"));
		RunSols(3.0);
		Expect(Sim->IsFloorRated(-1) && Sim->HasVaultRating() && Sim->IsHabRated(Hab1) && Sim->IsHabRated(Hab3) && !Sim->IsHabRated(Hab4) && Sim->GetHabNetworkBays(Hab1) == 4,
			FString::Printf(TEXT("setup: floor -1 rated, the vault certified, Hab1 and Hab3 one rated network of %d bays, Hab4 alone and unrated"), Sim->GetHabNetworkBays(Hab1)));
		for (int32 Cell = 0; Cell < 4; ++Cell) { Sim->DesignateRoom(-1, Cell, FName("LivingQuarters"), R); }
		for (const int32 H : { Hab1, Hab3 }) { for (int32 Bay = 0; Bay < 2; ++Bay) { Sim->DesignateBay(H, Bay, FName("LivingQuarters"), R); } }
		Expect(Sim->GetHousingCapacity() == 8 && Sim->GetHousingComfortCapacity() == 8, FString::Printf(TEXT("setup: 8 certified beds (4 on floor -1, 2 + 2 in the network): capacity %d, comfort %d"), Sim->GetHousingCapacity(), Sim->GetHousingComfortCapacity()));
		Expect(Sim->Debug_AddColonists(8) == 8, TEXT("setup: eight colonists housed"));
		{
			int32 OnFloor = 0, InHab1 = 0, InHab3 = 0;
			for (const FRHColonist& C : Sim->GetColonists()) { OnFloor += C.HomeLevel == -1 ? 1 : 0; InHab1 += C.HomeHab == Hab1 ? 1 : 0; InHab3 += C.HomeHab == Hab3 ? 1 : 0; }
			Expect(OnFloor == 4 && InHab1 == 2 && InHab3 == 2, FString::Printf(TEXT("setup: floors fill first - %d on floor -1, %d in Hab1, %d in Hab3"), OnFloor, InHab1, InHab3));
		}
		Sim->Debug_StepPopulation(URHSimClockSubsystem::SubStepSeconds);

		// --- D4. Floors: a whole floor is a district. ---
		{
			Expect(!Send(FloorCmd(NHomes, -3)) && Note.Contains(TEXT("not reached")) && !Sim->CanPaintDistrict(NHomes, -6, FVector::ZeroVector, FVector::ZeroVector, Why) && Why.Contains(TEXT("No such floor")),
				TEXT("D4 floor -3 is not reached by the shaft, floor -6 does not exist: both refused"));
			FRHDistrictPaintPreview Pre;
			Expect(Sim->CanPaintDistrict(NHomes, -1, FVector(9.0e6, 9.0e6, 0.0), FVector(-9.0e6, 9.0e6, 0.0), Why, &Pre) && Pre.bFloor && Pre.JoinId == 0,
				TEXT("D4 a floor order ignores the rectangle and the coverage rule entirely"));
			Expect(Send(FloorCmd(NHomes, -1)) && Find(5) && Find(5)->IsFloor() && Find(5)->Level == -1 && Find(5)->Lattice.Num() == 0 && Find(5)->Kind == NHomes
				&& Find(5)->Name == TEXT("Gusev") && Sim->GetDistrictOfFloor(-1) == 5, FString::Printf(TEXT("D4 floor -1 becomes Homes #5 'Gusev' (%s)"), *Note));
			Expect(!Send(FloorCmd(NHomes, -1)) && Note.Contains(TEXT("already")) && !Send(FloorCmd(NIndustry, -1)) && Note.Contains(TEXT("erase it first")) && Sim->GetDistricts().Num() == 4,
				FString::Printf(TEXT("D4 a floor holds one district: Homes again is 'already', Industry is 'erase it first' (%s)"), *Note));
			Expect(Send(FloorCmd(NResearch, -2)) && Find(6) && Find(6)->Level == -2 && Find(6)->Kind == NResearch && Sim->GetDistrictOfFloor(-2) == 6 && Sim->GetDistrictOfFloor(-3) == 0,
				TEXT("D4 floor -2 becomes Research #6"));
			Expect(Sim->GetDistrictOfCell(-1, 0) == 5 && Sim->GetDistrictOfCell(-1, 3) == 5 && Sim->GetDistrictOfCell(-2, 2) == 6 && Sim->GetDistrictOfCell(-3, 0) == 0
				&& Sim->GetDistrictOfCell(-1, -1) == 0 && Sim->GetDistrictAtPoint(-1, FVector(123456, 0, 0)) == 5 && Sim->GetDistrictAtPoint(0, FVector(123456, 0, 0)) == 0,
				TEXT("D4 an underground cell belongs to its floor's district (and no cell belongs to a floor with none)"));
			Expect(Sim->GetDistricts().Num() == 5 && Sim->GetDistricts()[0].Id == 1 && Sim->GetDistricts()[1].Id == 3 && Sim->GetDistricts()[2].Id == 4 && Sim->GetDistricts()[3].Id == 5 && Sim->GetDistricts()[4].Id == 6,
				TEXT("D4 the list stays in ascending Id order"));
		}

		// --- D5. Membership: buildings, bays, cells, colonists. One district per building. ---
		{
			Expect(Sim->GetDistrictOfBuilding(ForgeId) == 1 && Sim->GetDistrictOfBuilding(FirstOf(TEXT("Lander"))) == 1 && Sim->GetDistrictOfBuilding(IdOf(TEXT("Pylon"), 5000, 0)) == 3
				&& Sim->GetDistrictOfBuilding(IdOf(TEXT("SolarArray"), 3500, 1000)) == 3 && Sim->GetDistrictOfBuilding(IdOf(TEXT("SolarArray"), 3500, 2000)) == 0 && Sim->GetDistrictOfBuilding(IdOf(TEXT("BatteryBank"), 1000, 3500)) == 0
				&& Sim->GetDistrictOfBuilding(FloorFilter) == 5 && Sim->GetDistrictOfBuilding(9999) == 0,
				TEXT("D5 a building belongs to the district holding its footprint centre: the Forge and the Lander #1, the Pylon and a solar array #3, open ground none; a building underground its floor's (#5)"));
			Expect(Sim->GetDistrictOfBuilding(Hab1) == 1 && Sim->GetDistrictOfBay(Hab1, 0) == 1 && Sim->GetDistrictOfBay(Hab1, 1) == 1 && Sim->GetDistrictAtCell(FIntPoint(4, 0)) == 3,
				TEXT("D5 Hab1 has bay 0 in Homes #1 and bay 1 over Industry #3's ground: both bays are #1's, the shell is never split"));
			Expect(Sim->GetDistrictOfBuilding(Hab3) == 0 && Sim->GetDistrictOfBay(Hab3, 0) == 0 && Sim->GetDistrictOfBay(Hab3, 1) == 0 && Sim->GetDistrictAtCell(FIntPoint(4, -1)) == 3,
				TEXT("D5 Hab3 has bay 0 on open ground and bay 1 inside Industry #3: its anchor is bay 0, so both bays are in no district"));
			Expect(Sim->GetDistrictOfBuilding(Hab4) == 3 && Sim->GetDistrictOfBay(Hab4, 0) == 3 && Sim->GetDistrictOfBay(Hab4, 1) == 3 && Sim->GetDistrictAtCell(FIntPoint(6, 1)) == 0,
				TEXT("D5 Hab4 has bay 0 in Industry #3 and bay 1 on open ground: both bays are #3's"));
			Expect(Sim->GetDistrictOfBuilding(HabFilter) == 1, TEXT("D5 a filter inside Hab1's bay 0 belongs to its own footprint cell (#1)"));
			// Colonists: their home's district.
			int32 OnFloor = 0, InHab1 = 0, InHab3 = 0, Elsewhere = 0;
			bool bHomesMatch = true;
			for (const FRHColonist& C : Sim->GetColonists())
			{
				const int32 D = Sim->GetDistrictOfColonist(C.Id);
				if (C.HomeHab == 0) { OnFloor += D == 5 ? 1 : 0; bHomesMatch &= D == Sim->GetDistrictOfFloor(C.HomeLevel); }
				else if (C.HomeHab == Hab1) { InHab1 += D == 1 ? 1 : 0; }
				else if (C.HomeHab == Hab3) { InHab3 += D == 0 ? 1 : 0; }
				else { ++Elsewhere; }
			}
			Expect(OnFloor == 4 && InHab1 == 2 && InHab3 == 2 && Elsewhere == 0 && bHomesMatch && Sim->GetDistrictOfColonist(0) == 0 && Sim->GetDistrictOfColonist(9999) == 0,
				FString::Printf(TEXT("D5 a colonist belongs to their home's district: %d on floor -1 (#5), %d in Hab1 (#1), %d in Hab3 (none)"), OnFloor, InHab1, InHab3));
			// The counts, checked against an independent walk.
			const FRHDistrictCounts C1 = CountsOf(1), C3 = CountsOf(3), C5 = CountsOf(5), C6 = CountsOf(6);
			int32 BuildingsIn1 = 0, BuildingsIn3 = 0, BuildingsIn5 = 0;
			for (const FRHBuildingInstance& B : Sim->GetBuildings())
			{
				const int32 D = Sim->GetDistrictOfBuilding(B.Id);
				BuildingsIn1 += D == 1 ? 1 : 0; BuildingsIn3 += D == 3 ? 1 : 0; BuildingsIn5 += D == 5 ? 1 : 0;
			}
			Expect(C1.Buildings == BuildingsIn1 && C1.Buildings == 4 && C1.Bays == 2 && C1.Residents == 2 && C1.Beds == Sim->GetHabBeds(Hab1) && C1.Beds == 2 && C1.Cells == 26 && C1.Seats == 0,
				FString::Printf(TEXT("D5 Gale: %d buildings (Lander, Forge, Hab1, its filter), %d bays, %d residents, %d beds, %d cells"), C1.Buildings, C1.Bays, C1.Residents, C1.Beds, C1.Cells));
			Expect(C3.Buildings == BuildingsIn3 && C3.Buildings == 3 && C3.Bays == 2 && C3.Residents == 0 && C3.Beds == 0 && C3.Cells == 6,
				FString::Printf(TEXT("D5 Holden: %d buildings (Pylon, a solar array, Hab4), %d bays, no residents, no beds (Hab4 is not rated)"), C3.Buildings, C3.Bays));
			Expect(C5.Buildings == BuildingsIn5 && C5.Buildings == 1 && C5.Residents == 4 && C5.Beds == Sim->GetFloorBeds(-1) && C5.Beds == 4 && C5.Cells == Sim->GetFloorCarvedCells(-1) && C5.Cells == 4,
				FString::Printf(TEXT("D5 floor -1: %d building, %d residents, %d beds, %d carved cells"), C5.Buildings, C5.Residents, C5.Beds, C5.Cells));
			Expect(C6.Cells == 3 && C6.Residents == 0 && C6.Beds == 0 && C6.Seats == 0 && C6.Buildings == 0, TEXT("D5 floor -2 (not rated): 3 carved cells, nothing else"));
			int32 ResidentsInDistricts = 0, BedsInDistricts = 0;
			TArray<FRHDistrictCounts> All;
			Sim->GetDistrictCounts(All);
			for (const FRHDistrictCounts& C : All) { ResidentsInDistricts += C.Residents; BedsInDistricts += C.Beds; }
			Expect(All.Num() == Sim->GetDistricts().Num() && All[0].DistrictId == 1 && All.Last().DistrictId == 6 && ResidentsInDistricts == 6 && ResidentsInDistricts <= Sim->GetPopulation() && BedsInDistricts == 6 && BedsInDistricts <= Sim->GetHousingCapacity(),
				FString::Printf(TEXT("D5 the counts come in district order and add up: %d of %d residents and %d of %d certified beds sit in districts (Hab3's two and two are in none)"), ResidentsInDistricts, Sim->GetPopulation(), BedsInDistricts, Sim->GetHousingCapacity()));
			Expect(Sim->Debug_DistrictCachesFresh(), TEXT("D5 the membership caches match a fresh derivation"));
		}

		// --- D6. Demand: Housing. ---
		// Hope is pinned per case; settlers' wants come from the same formula arrivals use.
		const auto SetHope = [&](double V) { Sim->Debug_SetHopeSmoothed(V); };
		{
			SetHope(100.0);
			FRHCityDemand Dm = Sim->GetCityDemand();
			// Full: 8 people, 8 certified beds, 8 comfortable. Hope 100 draws 4 settlers.
			Expect(Dm.Population == 8 && Dm.FreeBeds == 0 && Dm.ComfortCapacity == 8 && Dm.bVaultCertified && Dm.bSettlersDraw && Dm.SettlersWanted == Sim->GetImmigrationRaw(100.0, 8) && Dm.SettlersWanted == 4
				&& Dm.SettlerRoom == 0 && Dm.SettlerBedsShort == 5 && Dm.HousingBedsShort == 5 && Dm.HousingBedsWanted == 5 && Dm.HousingLimit == FName("FreeBeds") && Near(Dm.Housing, 1.0, 1e-12) && !Dm.bBigArrivalPending,
				FString::Printf(TEXT("D6 full house at Hope 100: 4 settlers wanted and the reserve bed = 5 beds short of 5, bound by the free-bed term (short %d, limit %s, bar %.2f)"), Dm.HousingBedsShort, *Dm.HousingLimit.ToString(), Dm.Housing));
			SetHope(80.0);
			Dm = Sim->GetCityDemand();
			Expect(Dm.SettlersWanted == 1 && Dm.SettlersWanted == Sim->GetImmigrationRaw(80.0, 8) && Dm.bSettlersDraw && Dm.HousingBedsShort == 2 && Dm.HousingBedsWanted == 2,
				FString::Printf(TEXT("D6 at Hope 80 a ship would bring 1: the want follows Hope (1 settler + 1 reserve = 2 beds short, %d)"), Dm.HousingBedsShort));
			SetHope(60.0);
			Dm = Sim->GetCityDemand();
			Expect(!Dm.bSettlersDraw && Dm.SettlersWanted > 0 && Sim->GetImmigrationRaw(60.0, 8) == 0 && Dm.HousingBedsShort == Dm.SettlersWanted + Sim->GetImmigrationReserveBeds(),
				FString::Printf(TEXT("D6 below the Hope minimum nobody is drawn, so the want is the small base (one Crew Pod, %d) - not the ship formula's 0"), Dm.SettlersWanted));
			// The split is the same arithmetic as before.
			bool bSplitSame = true;
			for (const double Hope : { 0.0, 74.9, 75.0, 80.0, 90.0, 100.0 })
			{
				for (const int32 Pop : { 0, 1, 8, 50, 400 })
				{
					for (const int32 Free : { -3, 0, 1, 2, 9, 500 })
					{
						for (const int32 Comfort : { 0, 8, 12, 100, 900 })
						{
							const int32 Whole = Sim->GetImmigrationCount(Hope, Pop, Free, Comfort);
							const int32 Parts = (Pop <= 0 || Free <= 0 || Hope < Sim->GetImmigrationHopeMin()) ? 0 : FMath::Min(Sim->GetImmigrationRaw(Hope, Pop), Sim->GetImmigrationRoom(Pop, Free, Comfort));
							bSplitSame &= Whole == Parts && Whole >= 0 && Whole <= Sim->GetImmigrationCapPerShip();
						}
					}
				}
			}
			Expect(bSplitSame && Sim->GetImmigrationRoom(8, 6, 8) == 0 && Sim->GetImmigrationRoom(8, 6, 11) == 3 && Sim->GetImmigrationRoom(8, 3, 100) == 2 && Sim->GetImmigrationRoom(8, 0, 100) == 0 && Sim->GetImmigrationRoom(8, -5, 100) == 0,
				TEXT("D6 GetImmigrationCount is exactly min(raw, room) over 900 combinations; room is min(free - reserve, comfort - pop) floored at 0"));
		}
		// Room on floor -1: carve six more cells (legacy beds: a bed a cell, wherever zoned), so the floor has ten beds
		// but only four Living cells - free beds up, comfortable room not.
		Sim->ExcavateFloor(-1, 6, R);
		Sim->Debug_SetFloorO2Kg(-1, 1000.0); // the new volume pressurized at once, so the floor never drops its rating (a lost rating would start the recall clock)
		Land();
		Expect(Sim->IsFloorRated(-1) && Sim->GetFloorCarvedCells(-1) == 10 && Sim->GetPopulation() == 8, FString::Printf(TEXT("setup: floor -1 carved to 10 cells and re-rated (pop %d)"), Sim->GetPopulation()));
		Sim->DesignateRoom(-1, 4, FName("Workstation"), R);
		Sim->DesignateRoom(-1, 5, FName("Lab"), R);
		Sim->DesignateRoom(-1, 6, FName("Dining"), R);
		Sim->DesignateRoom(-1, 7, FName("Cooking"), R);
		Sim->Debug_StepPopulation(URHSimClockSubsystem::SubStepSeconds);
		{
			SetHope(100.0);
			FRHCityDemand Dm = Sim->GetCityDemand();
			// 10 + 4 = 14 certified beds, 8 colonists: 6 free. Comfort is the 4 + 4 Living cells and bays: 8.
			Expect(Dm.FreeBeds == 6 && Sim->GetFreeHousing() == 6 && Dm.ComfortCapacity == 8 && Dm.SettlersWanted == 4 && Dm.SettlerRoom == 0 && Dm.SettlerBedsShort == 4 && Dm.HousingLimit == FName("Comfort")
				&& Dm.HousingBedsShort == 4 && Near(Dm.Housing, 4.0 / 5.0, 1e-12),
				FString::Printf(TEXT("D6 beds to spare, comfort to want: 6 free but 0 comfortable room - 4 short, bound by the comfort term (limit %s, bar %.2f)"), *Dm.HousingLimit.ToString(), Dm.Housing));
			Expect(Dm.SettlerBedsShort == Dm.SettlersWanted - Sim->GetImmigrationCount(100.0, Dm.Population, Dm.FreeBeds, Dm.ComfortCapacity),
				TEXT("D6 beds short = settlers wanted - the settlers the arrival function would land (the demand IS the arrivals' own limit)"));
			Sim->DesignateRoom(-1, 8, FName("LivingQuarters"), R);
			Dm = Sim->GetCityDemand();
			Expect(Dm.ComfortCapacity == 9 && Dm.SettlerRoom == 1 && Dm.SettlerBedsShort == 3 && Dm.HousingLimit == FName("Comfort") && Near(Dm.Housing, 3.0 / 5.0, 1e-12)
				&& Dm.SettlerBedsShort == Dm.SettlersWanted - Sim->GetImmigrationCount(100.0, 8, 6, 9),
				FString::Printf(TEXT("D6 one more Living cell: comfortable room 1 (short %d, bar %.2f); the ship would land %d"), Dm.SettlerBedsShort, Dm.Housing, Sim->GetImmigrationCount(100.0, 8, 6, 9)));
			// Before the vault is certified surface beds count in comfort but not in the free beds.
			Sim->Debug_SetVaultRated(false);
			const FRHCityDemand Pre = Sim->GetCityDemand();
			Sim->Debug_SetVaultRated(true);
			Expect(!Pre.bVaultCertified && Pre.ComfortCapacity == Dm.ComfortCapacity && Pre.FreeBeds == Dm.FreeBeds - 4 && Pre.FreeBeds == 2 && Pre.SettlerBedsShort == 3 && Pre.HousingLimit == FName("FreeBeds") && Dm.HousingLimit == FName("Comfort"),
				FString::Printf(TEXT("D6 before the vault: the 4 surface beds lift comfort (%d) but not the free beds (%d, was %d), so the free-bed term binds (%s); after, the comfort term does (%s)"),
					Pre.ComfortCapacity, Pre.FreeBeds, Dm.FreeBeds, *Pre.HousingLimit.ToString(), *Dm.HousingLimit.ToString()));
			// The Big Arrival: its gate is GetFreeHousing() >= BigArrivalFreeBeds.
			Sim->Debug_SetBigArrivalDone(false);
			SetHope(100.0);
			Land();
			SetHope(100.0);
			Dm = Sim->GetCityDemand();
			Expect(Dm.bBigArrivalPending && Sim->GetBigArrivalStreakSols() > 0.0 && !Sim->IsBigArrivalDone() && Dm.BigArrivalBedsShort == Sim->GetBigArrivalFreeBeds() - Sim->GetFreeHousing() && Dm.BigArrivalBedsShort == 94
				&& Dm.HousingBedsShort == 94 && Dm.HousingBedsWanted == Sim->GetBigArrivalFreeBeds() && Dm.HousingLimit == FName("BigArrival") && Near(Dm.Housing, 0.94, 1e-12),
				FString::Printf(TEXT("D6 the Big Arrival is pending (streak %.3f sols) and needs 100 free beds against 6: 94 short, the headline (bar %.2f, limit %s)"), Sim->GetBigArrivalStreakSols(), Dm.Housing, *Dm.HousingLimit.ToString()));
			// Sweep the legacy beds-a-cell rate across the gate: short = max(0, 100 - free), and zero exactly when the wave could land.
			bool bGateMatches = true;
			int32 SawShort = 0, SawClear = 0;
			for (int32 K = 0; K <= 80; ++K)
			{
				Sim->Debug_SetColonistsPerCell(6.0 + K * 0.05);
				const FRHCityDemand Sw = Sim->GetCityDemand();
				const bool bGate = Sim->GetFreeHousing() >= Sim->GetBigArrivalFreeBeds();
				bGateMatches &= Sw.BigArrivalBedsShort == FMath::Max(0, Sim->GetBigArrivalFreeBeds() - Sim->GetFreeHousing()) && (Sw.BigArrivalBedsShort == 0) == bGate;
				SawShort += Sw.BigArrivalBedsShort > 0 ? 1 : 0; SawClear += Sw.BigArrivalBedsShort == 0 ? 1 : 0;
			}
			Sim->Debug_SetColonistsPerCell(1.0);
			Expect(bGateMatches && SawShort > 5 && SawClear > 5, FString::Printf(TEXT("D6 over 81 bed rates the Big Arrival term is max(0, 100 - free) and 0 exactly when its own gate opens (%d short, %d clear)"), SawShort, SawClear));
			Sim->Debug_SetBigArrivalDone(true);
			Dm = Sim->GetCityDemand();
			Expect(!Dm.bBigArrivalPending && Dm.BigArrivalBedsShort == 0 && Dm.HousingBedsShort == 3, TEXT("D6 once the wave has landed it wants nothing more"));
			// A bigger Hope draws a bigger want: sampled across the draw.
			SetHope(100.0);
			const int32 W100 = Sim->GetCityDemand().SettlersWanted;
			SetHope(85.0);
			const int32 W85 = Sim->GetCityDemand().SettlersWanted;
			Expect(W100 > W85 && W85 > 0 && W100 == Sim->GetImmigrationRaw(100.0, 8) && W85 == Sim->GetImmigrationRaw(85.0, 8), FString::Printf(TEXT("D6 the want rises with Hope (%d at 85, %d at 100)"), W85, W100));
		}

		// --- D7. Demand: Jobs. ---
		{
			Sim->Debug_StepPopulation(URHSimClockSubsystem::SubStepSeconds);
			FRHCityDemand Dm = Sim->GetCityDemand();
			// Two work cells (a Workstation, a Lab) = 2 seats, 8 people.
			Expect(Dm.TotalSeats == 2 && Dm.FilledSeats == 2 && Dm.FilledSeats == Sim->GetColonyHope().FilledSeats && Dm.Unemployed == 6 && Dm.SeatsShort == 6 && Near(Dm.Jobs, 0.75, 1e-12),
				FString::Printf(TEXT("D7 8 people, 2 seats, both filled: 6 without a seat, 6 seats short, bar %.2f"), Dm.Jobs));
			// Lazy fill: an unstaffed seat is open but nobody has taken it yet.
			Sim->Debug_SetLazySeatFillSols(1000.0);
			Sim->DesignateRoom(-1, 9, FName("Workstation"), R);
			Sim->Debug_StepPopulation(URHSimClockSubsystem::SubStepSeconds);
			Dm = Sim->GetCityDemand();
			Expect(Dm.TotalSeats == 3 && Dm.FilledSeats == 2 && Dm.Unemployed == 6 && Dm.SeatsShort == 5 && Near(Dm.Jobs, 6.0 / 8.0, 1e-12),
				FString::Printf(TEXT("D7 a third seat opens but lazy fill leaves it unstaffed: 3 seats, 2 filled - 6 idle but only 5 seats short (%d, %d)"), Dm.Unemployed, Dm.SeatsShort));
			Sim->Debug_SetLazySeatFillSols(0.0);
			Sim->Debug_StepPopulation(URHSimClockSubsystem::SubStepSeconds);
			Dm = Sim->GetCityDemand();
			Expect(Dm.TotalSeats == 3 && Dm.FilledSeats == 3 && Dm.Unemployed == 5 && Dm.SeatsShort == 5 && Near(Dm.Jobs, 5.0 / 8.0, 1e-12),
				FString::Printf(TEXT("D7 instant fill again: all 3 seats staffed, 5 idle, 5 short (bar %.3f)"), Dm.Jobs));
			Expect(CountsOf(5).Seats == 3 && CountsOf(1).Seats == 0 && Dm.TotalSeats == CountsOf(5).Seats + CountsOf(1).Seats + CountsOf(3).Seats + CountsOf(6).Seats,
				TEXT("D7 the per-district seats add up to the city's: all 3 are on floor -1"));
		}

		// --- D8. Demand: Services. ---
		{
			FRHCityDemand Dm = Sim->GetCityDemand();
			// Present: Workstation (2 cells), Lab, Dining, Cooking - one room serves 25 residents, 8 people: covered in full.
			// Absent: Garden 0.5, Greenhouse 0.5, Infirmary 0.5: a gap of 1 each.
			TMap<FName, FRHServiceGap> ByFn;
			for (const FRHServiceGap& G : Dm.ServiceGaps) { ByFn.Add(G.Function, G); }
			const double WeightSum = 0.5 + 0.3 + 0.2 + 0.6 + 0.3 + 0.5 + 0.5;
			const double Covered = 0.3 + 0.2 + 0.6 + 0.3;
			Expect(ByFn.Num() == 7 && Dm.ServiceGaps.Num() == 7 && !ByFn.Contains(FName("Living")) && !ByFn.Contains(FName("Hallway")) && !ByFn.Contains(FName("Smoking")),
				FString::Printf(TEXT("D8 seven service functions earn Hope (Garden, Greenhouse, Lab, Workstation, Dining, Cooking, Infirmary): %d listed; Living, Hallway and the dormant rows are not"), Dm.ServiceGaps.Num()));
			Expect(ByFn[FName("Dining")].Rooms == 1 && Near(ByFn[FName("Dining")].Gap, 0.0, 1e-12) && ByFn[FName("Workstation")].Rooms == 2 && Near(ByFn[FName("Workstation")].Coverage, 1.0, 1e-12)
				&& ByFn[FName("Garden")].Rooms == 0 && Near(ByFn[FName("Garden")].Gap, 1.0, 1e-12) && ByFn[FName("Infirmary")].Rooms == 0 && Near(ByFn[FName("Infirmary")].Weight, 0.5, 1e-12),
				TEXT("D8 a function with rooms covers 8 people in full (gap 0); one with none has a gap of 1 and the weight of its first tier"));
			Expect(Dm.ServiceGaps[0].Function == FName("Garden") && Dm.ServiceGaps[1].Function == FName("Greenhouse") && Dm.ServiceGaps[2].Function == FName("Infirmary") && Near(Dm.ServiceGaps[0].Gap, 1.0, 1e-12) && Near(Dm.ServiceGaps[3].Gap, 0.0, 1e-12),
				FString::Printf(TEXT("D8 worst first, ties by weight then name: %s, %s, %s ..."), *Dm.ServiceGaps[0].Function.ToString(), *Dm.ServiceGaps[1].Function.ToString(), *Dm.ServiceGaps[2].Function.ToString()));
			Expect(Near(Dm.Services, 1.0 - Covered / WeightSum, 1e-6), FString::Printf(TEXT("D8 the bar is the share of the Rooms bonus left on the table: 1 - %.1f / %.1f = %.4f (got %.4f)"), Covered, WeightSum, 1.0 - Covered / WeightSum, Dm.Services));
			// The Hope term reads the same coverage: a function with rooms and enough of them earns its whole weight.
			const URHSimWorldSubsystem::FRHHopeBreakdown Hp = Sim->GetColonyHope();
			Expect(Near(Hp.Rooms, Covered * Sim->GetHopePerMoralePoint(), 1e-6), FString::Printf(TEXT("D8 and the Hope index agrees: Rooms %.3f = covered weight %.1f x %.1f"), Hp.Rooms, Covered, Sim->GetHopePerMoralePoint()));
			// Fifty people: one room covers half of them (25 / 50).
			Sim->Debug_SetColonistsPerCell(10.0);
			Sim->Debug_AddColonists(42);
			Sim->Debug_StepPopulation(URHSimClockSubsystem::SubStepSeconds);
			Dm = Sim->GetCityDemand();
			TMap<FName, FRHServiceGap> ByFn50;
			for (const FRHServiceGap& G : Dm.ServiceGaps) { ByFn50.Add(G.Function, G); }
			Expect(Dm.Population == 50 && Near(ByFn50[FName("Dining")].Coverage, 0.5, 1e-12) && Near(ByFn50[FName("Dining")].Gap, 0.5, 1e-12) && Near(ByFn50[FName("Lab")].Coverage, 0.5, 1e-12)
				&& Near(ByFn50[FName("Workstation")].Coverage, 1.0, 1e-12) && Near(ByFn50[FName("Garden")].Gap, 1.0, 1e-12),
				FString::Printf(TEXT("D8 at 50 people one Dining room covers half (gap 0.5), two Workstation rooms all; a missing Garden stays at 1 (pop %d)"), Dm.Population));
			const double Covered50 = 0.5 * 0.3 + 1.0 * 0.2 + 0.5 * 0.6 + 0.5 * 0.3;
			Expect(Near(Dm.Services, 1.0 - Covered50 / WeightSum, 1e-6) && Near(Sim->GetColonyHope().Rooms, Covered50 * Sim->GetHopePerMoralePoint(), 1e-6),
				FString::Printf(TEXT("D8 bar %.4f = 1 - %.2f / %.1f, and Hope's Rooms %.3f reads the same coverage"), Dm.Services, Covered50, WeightSum, Sim->GetColonyHope().Rooms));
		}

		// --- D9. Demand: Air. ---
		{
			FRHCityDemand Dm = Sim->GetCityDemand();
			const int32 FloorResidents = CountsOf(5).Residents;
			Expect(Dm.Homes == 2 && Dm.HomesShort == 0 && Dm.ResidentsShort == 0 && Near(Dm.Air, 0.0, 0.0) && Dm.ScarcestLeg.IsNone() && FloorResidents == 46
				&& Near(Dm.MinOxygenMarginSols, FMath::Min(Sim->GetFloorO2Kg(-1) / (46.0 * Sim->GetColonistO2KgPerSol()), Sim->GetHabNetworkO2Kg(Hab1) / (4.0 * Sim->GetColonistO2KgPerSol())), 1e-6),
				FString::Printf(TEXT("D9 two homes on full rations; the tightest oxygen margin is %.1f sols (floor -1: %.0f kg for 46, the network: %.0f kg for 4)"), Dm.MinOxygenMarginSols, Sim->GetFloorO2Kg(-1), Sim->GetHabNetworkO2Kg(Hab1)));
			// The floor's air runs out: its residents are short, the network's are not.
			Sim->Debug_SetFloorO2Kg(-1, 0.0);
			Sim->Debug_StepPopulation(URHSimClockSubsystem::SubStepSeconds);
			Dm = Sim->GetCityDemand();
			Expect(Dm.HomesShort == 1 && Dm.HomesShortOnAir == 1 && Dm.ResidentsShort == 46 && Dm.ScarcestLeg == FName("Oxygen") && Near(Dm.Air, 46.0 / 50.0, 1e-12) && Near(Dm.MinOxygenMarginSols, 0.0, 1e-9),
				FString::Printf(TEXT("D9 floor -1 out of air: 1 home short on air, 46 of 50 residents, the leg is %s, bar %.2f, margin %.2f sols"), *Dm.ScarcestLeg.ToString(), Dm.Air, Dm.MinOxygenMarginSols));
			Sim->Debug_SetFloorO2Kg(-1, 1000.0);
			Sim->Debug_StepPopulation(URHSimClockSubsystem::SubStepSeconds);
			Dm = Sim->GetCityDemand();
			Expect(Dm.HomesShort == 0 && Near(Dm.Air, 0.0, 0.0), TEXT("D9 the air back: no home short"));
			// Food out: every home is short on a ration, and the leg says why.
			const double FoodHeld = Sim->GetStock(NFood);
			Sim->AddStock(NFood, -FoodHeld);
			Sim->Debug_StepPopulation(URHSimClockSubsystem::SubStepSeconds);
			Dm = Sim->GetCityDemand();
			Expect(Dm.HomesShort == 2 && Dm.HomesShortOnAir == 0 && Dm.ResidentsShort == 50 && Dm.ScarcestLeg == FName("Food") && Near(Dm.Air, 1.0, 1e-12),
				FString::Printf(TEXT("D9 food out: both homes are short on rations (%d homes, %d residents), none for want of air, the leg is %s - the bar counts every short ration"), Dm.HomesShort, Dm.ResidentsShort, *Dm.ScarcestLeg.ToString()));
			Sim->AddStock(NFood, FoodHeld);
			Sim->Debug_StepPopulation(URHSimClockSubsystem::SubStepSeconds);
			Expect(Sim->GetCityDemand().HomesShort == 0, TEXT("D9 the food back: every home on a full ration"));
		}

		// --- D10. Membership caches: never stale. ---
		{
			bool bFresh = Sim->Debug_DistrictCachesFresh();
			const auto Touch = [&]() { Sim->GetDistrictOfBuilding(ForgeId); CountsOf(1); Sim->GetDistrictOfColonist(1); };
			Touch();
			// A building appears (the building set moves the key), a colonist leaves nothing behind, a bay is rezoned, a piece of furniture lands.
			const int32 Extra = Place(TEXT("SolarArray"), 4500, 3000); // lattice (5, 3): open ground
			bFresh &= Sim->Debug_DistrictCachesFresh();
			Touch();
			Expect(Extra != 0 && Sim->GetDistrictOfBuilding(Extra) == 0 && bFresh, TEXT("D10 a building added after a read: the cache rebuilds on its next read (open ground, no district)"));
			const int32 InGale = Place(TEXT("Stockpile"), 0, -1500); // lattice (0, -1): Homes #1
			bFresh &= Sim->Debug_DistrictCachesFresh();
			Expect(Sim->GetDistrictOfBuilding(InGale) == 1 && CountsOf(1).Buildings == 5 && Sim->Debug_DistrictCachesFresh(), TEXT("D10 a building placed inside a district is counted at once"));
			Touch();
			FRHCommand Demolish; Demolish.Verb = FName("Demolish"); Demolish.Target = FName("Building"); Demolish.Value = (double)Extra;
			Send(Demolish);
			bFresh &= Sim->Debug_DistrictCachesFresh();
			Expect(Sim->HasBuilding(Extra) == false && Sim->GetDistrictOfBuilding(Extra) == 0 && Sim->Debug_DistrictCachesFresh() && CountsOf(1).Buildings == 5, FString::Printf(TEXT("D10 a building demolished: gone from the membership, the rest intact (%s)"), *Note));
			Touch();
			Sim->DesignateBay(Hab4, 0, FName("LivingQuarters"), R);
			bFresh &= Sim->Debug_DistrictCachesFresh();
			Touch();
			Sim->Debug_AddColonists(1);
			bFresh &= Sim->Debug_DistrictCachesFresh();
			const int32 Newest = Sim->GetColonists().Num() ? Sim->GetColonists().Last().Id : 0;
			Expect(Sim->GetDistrictOfColonist(Newest) == Sim->GetDistrictOfFloor(Sim->FindColonist(Newest) ? Sim->FindColonist(Newest)->HomeLevel : 0) && CountsOf(5).Residents == 47,
				TEXT("D10 a colonist arriving after a read lands in their home's district count (47 on floor -1)"));
			Touch();
			// Moving home: a floor that loses its rating loses its beds, and the residents move to free ones elsewhere.
			Sim->Debug_UnrateFloor(-1);
			const int32 Moved = Sim->Debug_RehomeResidents();
			bFresh &= Sim->Debug_DistrictCachesFresh();
			int32 StillOnFloor = 0, NowInHab1 = 0;
			for (const FRHColonist& C : Sim->GetColonists())
			{
				StillOnFloor += (C.HomeHab == 0 && C.HomeLevel == -1) ? 1 : 0;
				NowInHab1 += C.HomeHab == Hab1 ? 1 : 0;
			}
			Expect(Moved > 0 && CountsOf(5).Residents == StillOnFloor && CountsOf(1).Residents == NowInHab1 && NowInHab1 > 2 && StillOnFloor == 47 - Moved && Sim->Debug_DistrictCachesFresh(),
				FString::Printf(TEXT("D10 a floor loses its rating and %d residents move home: Gusev holds %d, Gale %d - the counts follow the move"), Moved, CountsOf(5).Residents, CountsOf(1).Residents));
			Touch();
			Send(RenameCmd(5, TEXT("Gusev Crater")));
			bFresh &= Sim->Debug_DistrictCachesFresh();
			Expect(NameOf(5) == TEXT("Gusev Crater") && bFresh, TEXT("D10 across a building, a demolish, a zoning change, an arrival and a rename the caches were never stale"));
		}

		// --- D11. Save / load: districts, the queue, CommandIds, Text. ---
		{
			// A district order, a rename and a building order in flight: three queued orders, three Ids.
			Rejects.Reset();
			Sim->EnqueueCommand(RenameCmd(1, TEXT("  Mons Olympus  ")));
			Sim->EnqueueCommand(PaintCmd(NFarming, -3, 0, -3, 0));
			FRHCommand DigOrder; DigOrder.Verb = FName("Dig"); DigOrder.Target = FName("Regolith_A");
			Sim->EnqueueCommand(DigOrder);
			const TArray<FRHCommand> QueuedBefore = Sim->GetUplinkQueue();
			Expect(QueuedBefore.Num() == 3 && QueuedBefore[0].CommandId != QueuedBefore[1].CommandId && QueuedBefore[1].CommandId != QueuedBefore[2].CommandId && QueuedBefore[0].CommandId != QueuedBefore[2].CommandId
				&& QueuedBefore[0].CommandId > 0 && QueuedBefore[0].Text == TEXT("  Mons Olympus  "), TEXT("D11 three orders queued, each with its own CommandId; the rename carries its Text"));
			const uint32 DigestBefore = DistrictDigest();
			const int32 VersionBefore = (int32)Sim->GetDistrictsVersion();
			const int32 Districts0 = Sim->GetDistricts().Num();
			const FRHDistrictCounts C1Before = CountsOf(1), C5Before = CountsOf(5);
			FString Err;
			Expect(Sim->SaveColony(TEXT("districtstest"), Err), FString::Printf(TEXT("D11 saved (%s)"), *Err));
			// Change everything the save holds.
			Send(EraseCmd(3));
			Send(RenameCmd(1, TEXT("Changed")));
			Sim->Debug_PlaceInstant(FName("Stockpile"), FVector(0, -1500, 0));
			for (const FRHCommand& Q : TArray<FRHCommand>(Sim->GetUplinkQueue())) { Sim->CancelUplinkCommand(Q.CommandId); }
			// (The three queued orders landed with the first of those steps: a Farming district, #7, appeared too.)
			Expect(DistrictDigest() != DigestBefore && Find(3) == nullptr && Find(7) != nullptr && Sim->GetUplinkQueue().Num() == 0, TEXT("D11 districts, names and the queue were changed after the save"));
			Expect(Sim->LoadColony(TEXT("districtstest"), Err) && Err.IsEmpty(), FString::Printf(TEXT("D11 loaded (%s)"), *Err));
			Expect(DistrictDigest() == DigestBefore && Sim->GetDistricts().Num() == Districts0 && Sim->GetDistrictsVersion() != (uint32)VersionBefore,
				FString::Printf(TEXT("D11 the districts round-trip exactly: every Id, kind, name, region, level, sorted lattice and the next Id (digest %08x)"), DistrictDigest()));
			const FRHDistrictCounts C1After = CountsOf(1), C5After = CountsOf(5);
			Expect(C1After.Buildings == C1Before.Buildings && C1After.Bays == C1Before.Bays && C1After.Residents == C1Before.Residents && C1After.Cells == C1Before.Cells
				&& C5After.Residents == C5Before.Residents && C5After.Buildings == C5Before.Buildings && Sim->Debug_DistrictCachesFresh(),
				TEXT("D11 membership re-derives from the loaded colony: the same counts"));
			bool bQueueOk = Sim->GetUplinkQueue().Num() == 3;
			for (int32 i = 0; bQueueOk && i < 3; ++i)
			{
				const FRHCommand& A = Sim->GetUplinkQueue()[i];
				const FRHCommand& B = QueuedBefore[i];
				bQueueOk &= A.Verb == B.Verb && A.Target == B.Target && A.CommandId == B.CommandId && A.Text == B.Text && A.Location == B.Location && A.LocationB == B.LocationB
					&& A.Level == B.Level && A.Value == B.Value && A.ExecuteAtSimSeconds == B.ExecuteAtSimSeconds;
			}
			Expect(bQueueOk && Sim->GetUplinkQueue()[0].Text == TEXT("  Mons Olympus  ") && Sim->GetUplinkQueue()[0].CommandId != Sim->GetUplinkQueue()[1].CommandId,
				TEXT("D11 the queue round-trips: Verb, Target, both end points, Value, due time, the RenameDistrict's Text and every CommandId (distinct, not 0)"));
			// A new order never reuses a loaded Id.
			Sim->EnqueueCommand(RenameCmd(4, TEXT("Later")));
			const int32 Fresh = Sim->GetUplinkQueue().Last().CommandId;
			Expect(Fresh > QueuedBefore[0].CommandId && Fresh > QueuedBefore[1].CommandId && Fresh > QueuedBefore[2].CommandId, FString::Printf(TEXT("D11 an order sent after the load gets Id %d, past every loaded Id"), Fresh));
			// The cancel fix: cancelling the second order removes the second, not the first.
			const FRHCommand Q0 = Sim->GetUplinkQueue()[0], Q1 = Sim->GetUplinkQueue()[1], Q2 = Sim->GetUplinkQueue()[2];
			Expect(Sim->CancelUplinkCommand(Q1.CommandId) && Sim->GetUplinkQueue().Num() == 3 && Sim->GetUplinkQueue()[0].CommandId == Q0.CommandId && Sim->GetUplinkQueue()[1].CommandId == Q2.CommandId
				&& Sim->GetUplinkQueue()[0].Verb == FName("RenameDistrict") && Sim->GetUplinkQueue()[1].Verb == FName("Dig") && Sim->GetUplinkQueue()[2].CommandId == Fresh,
				TEXT("D11 after a load, cancelling the second order removes THE SECOND (it used to take whichever was first: every loaded order was Id 0)"));
			Expect(!Sim->CancelUplinkCommand(Q1.CommandId) && Sim->GetUplinkQueue().Num() == 3, TEXT("D11 cancelling it again finds nothing: the signal beat the cancel"));
			// The queued rename lands after the load with its Text.
			Rejects.Reset();
			Land();
			Expect(NameOf(1) == TEXT("Mons Olympus") && NameOf(4) == TEXT("Later") && Sim->GetUplinkQueue().Num() == 0 && Sim->GetDistricts().Num() == Districts0,
				FString::Printf(TEXT("D11 the queued RenameDistrict lands after the load: '%s' (trimmed from its saved Text), and the one sent after it: '%s'; the cancelled paint never landed"), *NameOf(1), *NameOf(4)));
			Expect(Rejects.Num() == 0, FString::Printf(TEXT("D11 nothing in the loaded queue was rejected (%d)"), Rejects.Num()));
		}

		// --- D12. Determinism and the final reads. ---
		{
			TArray<FRHDistrictCounts> A, B;
			Sim->GetDistrictCounts(A);
			Sim->GetDistrictCounts(B);
			bool bSame = A.Num() == B.Num();
			for (int32 i = 0; bSame && i < A.Num(); ++i) { bSame &= A[i].DistrictId == B[i].DistrictId && A[i].Buildings == B[i].Buildings && A[i].Bays == B[i].Bays && A[i].Residents == B[i].Residents && A[i].Beds == B[i].Beds && A[i].Seats == B[i].Seats && A[i].Cells == B[i].Cells; }
			const FRHCityDemand X = Sim->GetCityDemand(), Y = Sim->GetCityDemand();
			bool bDemandSame = X.Housing == Y.Housing && X.Jobs == Y.Jobs && X.Services == Y.Services && X.Air == Y.Air && X.ServiceGaps.Num() == Y.ServiceGaps.Num();
			for (int32 i = 0; bDemandSame && i < X.ServiceGaps.Num(); ++i) { bDemandSame &= X.ServiceGaps[i].Function == Y.ServiceGaps[i].Function && X.ServiceGaps[i].Gap == Y.ServiceGaps[i].Gap; }
			Expect(bSame && bDemandSame && Sim->Debug_DistrictCachesFresh(), TEXT("D12 the counts and the demand are pure functions of state: asked twice, the same"));
				Sim->Debug_SetColonistsPerCell(1.0);
		}

		Sim->OnCommandRejected.Remove(RejectH);
		UE_LOG(LogRedHopeSim, Display, TEXT("=== DISTRICTS TEST END: %d check(s), %d failure(s) ==="), Checks, Fails);
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return Fails > 0 ? 1 : 0;
	}

	// C5b self-test (living-city gate 5b, 2026-10-19): CITY GROWTH. `-citygrowth` (the -growth suite is the births').
	// Fails loudly. The fixture is the -districts recipe grown a little: a Lander (30 m of coverage) and two
	// Pylons (40 m each, east and west), two rated four-bay surface networks (N1 east, N2 west, each two 2 x 1
	// shells edge to edge with a filter inside), a shaft to -2 with floors -1 and -2 carved to four cells and
	// rated, eight colonists (four on floor -1, four in N1) and five districts: #1 Homes over N1's ground, #2
	// Civic over N2's, #3 Homes on floor -1, #4 Civic on floor -2 and #5 Industry. A SolarArray stands on
	// lattice cell (2,0) and the Pylon on (5,0), so the nearest free ground to N1 is the 2 x 2 at (3,1). The
	// first CEO quota stays OPEN (Water is held under its 200 kg bill), so its Struct and Oxygen bills stand
	// between growth and the stores. The base state is saved once and every scenario starts from a load of it.
	//  (a) the data: the seven Growth config rows, the CivicService and CivicFurniture columns, and the bunk bed;
	//  (b) a colony with no districts never grows (with demand and material), and the bill, the money and the
	//      pauses: per-resource reserves and the open quota's bill (Struct, Oxygen), Shielding short, the whole
	//      bill atomically, a construction site's hopper never counted, the quota met and its bill gone;
	//      (b4, C5c) between quota rounds (awaiting the manifest, ship inbound) growth leaves the NEXT
	//      round's bill standing, checked on each resource's one-kilo boundary, the ladder walked for real
	//      (Q2, Q3, round 1, round 2) so the protected bill is the bill that then opens, and the ladder's end;
	//  (c) the gates: Homes need a housing shortfall, Industry never grows, an open project and a switched-off
	//      district are skipped, the two pauses (a home on short rations, Hope failing);
	//  (d) surface Homes: the nearest candidate, the largest module that fits, only INSIDE the district, touching
	//      only the target network, off a road, the sealed-network skip, the completion step;
	//  (e) underground Homes: a rated floor, four cells carved directly, bunks, no Borer, no hydrogen;
	//  (f) civic: the worst gap (and the lexical tie), a room already under way counted, the adjacency check, the
	//      carve when no cell is free, a CivicService that emits Odor is never zoned;
	//  (g) one start per sol colony-wide, round-robin by Id, one alert a sol, a home staying rated through each
	//      completion;
	//  (h) a completion whose site changed refunds the whole bill; an erase cancels with a full refund; the
	//      toggle survives a save; a project saved halfway finishes on the SAME step as a twin that never saved;
	//  (i) ten sols in the era band and in the agent band: the same ledger.
	if (FParse::Param(*Params, TEXT("citygrowth")))
	{
		UE_LOG(LogRedHopeSim, Display, TEXT("=== CITY GROWTH TEST (C5b, 2026-10-19) ==="));
		int32 Fails = 0, Checks = 0;
		const auto Expect = [&](bool bOk, const FString& What)
		{
			++Checks;
			if (bOk) { UE_LOG(LogRedHopeSim, Display, TEXT("CITYGROWTH ok: %s"), *What); }
			else { UE_LOG(LogRedHopeSim, Error, TEXT("CITYGROWTH FAIL: %s"), *What); ++Fails; }
		};
		const auto Near = [](double A, double B, double Tol) { return FMath::Abs(A - B) <= Tol; };
		constexpr double EraDt = (double)URHSimClockSubsystem::EraStepSimSeconds;
		constexpr double AgentDt = (double)URHSimClockSubsystem::SubStepSeconds;
		constexpr double SolLen = (double)URHSimClockSubsystem::SolLengthSimSeconds;
		const FName NHomes(TEXT("Homes")), NIndustry(TEXT("Industry")), NCivic(TEXT("Civic"));
		const FName NStruct(TEXT("Struct")), NShield(TEXT("Shielding")), NOx(TEXT("Oxygen")), NWater(TEXT("Water")), NFood(TEXT("Food")), NHyd(TEXT("Hydrogen"));
		URHDefinitionsSubsystem* DefsSub = World->GetSubsystem<URHDefinitionsSubsystem>();
		FString R, Note;
		TArray<FString> Rejects, Alerts;
		const FDelegateHandle RejectH = Sim->OnCommandRejected.AddLambda([&Rejects](const FRHCommand&, const FString& S) { Rejects.Add(S); });
		const FDelegateHandle AlertH = Sim->OnAlert.AddLambda([&Alerts](const FString& S) { Alerts.Add(S); });
		const auto GrowthAlerts = [&]() { int32 N = 0; for (const FString& A : Alerts) { N += A.StartsWith(TEXT("CITY GROWTH")) ? 1 : 0; } return N; };
		double PinHope = -1.0; // >= 0: smoothed Hope is held there before every step
		const auto SetHope = [&](double V) { PinHope = V; if (V >= 0.0) { Sim->Debug_SetHopeSmoothed(V); } };
		// What the start log saw: (sol of the check, district) for every project that opened, in order.
		TArray<FIntPoint> StartLog;
		TSet<int32> SeenOpen;
		const auto Poll = [&]()
		{
			TSet<int32> Now;
			for (const FRHGrowthProject& P : Sim->GetGrowthProjects())
			{
				Now.Add(P.DistrictId);
				if (!SeenOpen.Contains(P.DistrictId)) { StartLog.Add(FIntPoint(Sim->GetGrowthCheckedSol(), P.DistrictId)); }
			}
			SeenOpen = Now;
		};
		const auto Land = [&]()
		{
			Clock->Debug_AdvanceSimSeconds(EraDt);
			Sim->Debug_SetBatteryWh(1.0e9);
			if (PinHope >= 0.0) { Sim->Debug_SetHopeSmoothed(PinHope); }
			Sim->EraStep((float)EraDt);
			Poll();
		};
		const auto Steps = [&](int32 N) { for (int32 K = 0; K < N; ++K) { Land(); } };
		const auto RunSols = [&](double SolCount) { Steps(FMath::RoundToInt32(SolCount * SolLen / EraDt)); };
		const auto Send = [&](const FRHCommand& C)
		{
			const int32 R0 = Rejects.Num();
			Sim->EnqueueCommand(C);
			Land();
			Note = Rejects.Num() == R0 ? FString(TEXT("accepted")) : Rejects.Last();
			return Rejects.Num() == R0;
		};
		const auto PaintCmd = [](FName Kind, int32 X1, int32 Y1, int32 X2, int32 Y2, int32 Level = 0)
		{
			FRHCommand C;
			C.Verb = FName("PaintDistrict");
			C.Target = Kind;
			C.Level = Level;
			C.Location = URHSimWorldSubsystem::LatticeCentreCm(FIntPoint(X1, Y1));
			C.LocationB = URHSimWorldSubsystem::LatticeCentreCm(FIntPoint(X2, Y2));
			return C;
		};
		const auto EraseCmd = [](int32 Id) { FRHCommand C; C.Verb = FName("EraseDistrict"); C.Value = (double)Id; return C; };
		const auto IdOf = [&](const TCHAR* Def, double X, double Y, int32 Level = 0)
		{
			for (const FRHBuildingInstance& B : Sim->GetBuildings())
			{
				if (B.DefName == FName(Def) && B.Level == Level && FMath::Abs(B.LocationCm.X - X) < 1.0 && FMath::Abs(B.LocationCm.Y - Y) < 1.0) { return B.Id; }
			}
			return 0;
		};
		const auto Place = [&](const TCHAR* Def, double X, double Y, int32 SX = 1, int32 SY = 1, int32 Level = 0)
		{
			Sim->Debug_PlaceInstant(FName(Def), FVector(X, Y, 0.0), Level, SX, SY);
			return IdOf(Def, X, Y, Level);
		};
		const auto Held = [&](FName Res) { return Sim->GetGrowthAvailability(Res).Held; };
		// Set a resource's holdings to exactly Kg by moving the pool (the fixture keeps its solids there).
		const auto SetHeld = [&](FName Res, double Kg) { Sim->AddStock(Res, Kg - Held(Res)); };
		const auto Open = [&](int32 Id) { return Sim->FindGrowthProject(Id); };
		const auto NoteOf = [&](int32 Id) { return Sim->GetGrowthNote(Id); };
		const auto CountBuildings = [&](const TCHAR* Def) { int32 N = 0; for (const FRHBuildingInstance& B : Sim->GetBuildings()) { N += B.DefName == FName(Def) ? 1 : 0; } return N; };
		const auto PiecesIn = [&](const FIntVector& Key) -> TArray<FName>
		{
			TArray<int32> Ids;
			Sim->GetCellFurniture(Key, &Ids);
			TArray<FName> Rows;
			for (const int32 Id : Ids) { if (const FRHFurnitureInstance* F = Sim->FindFurniture(Id)) { Rows.Add(F->Row); } }
			Rows.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
			return Rows;
		};
		const auto CountRow = [](const TArray<FName>& Rows, const TCHAR* Name) { int32 N = 0; for (const FName& F : Rows) { N += F == FName(Name) ? 1 : 0; } return N; };
		// Run until the district's project is gone (finished or refunded); returns the steps it took (-1: never).
		// No new project starts behind it (the master switch is off while it runs), so "gone" means this one.
		const auto RunToEnd = [&](int32 District, int32 MaxSteps)
		{
			Sim->Debug_SetGrowthEnabled(false);
			int32 Took = -1;
			for (int32 K = 1; K <= MaxSteps && Took < 0; ++K) { Land(); if (!Open(District)) { Took = K; } }
			Sim->Debug_SetGrowthEnabled(true);
			return Took;
		};
		const auto OnlyDistricts = [&](std::initializer_list<int32> On)
		{
			for (const FRHDistrict& D : TArray<FRHDistrict>(Sim->GetDistricts()))
			{
				bool bOn = false;
				for (const int32 Id : On) { bOn |= Id == D.Id; }
				Sim->SetDistrictGrowth(D.Id, bOn);
			}
		};
		// A digest of what a ledger comparison reads.
		struct FLedger
		{
			double Struct = 0, Shielding = 0, Oxygen = 0, Food = 0;
			int32 Cells1 = 0, Cells2 = 0, Habs = 0, Pieces = 0, Beds = 0, Free = 0, Started = 0, Completed = 0;
		};
		const auto Ledger = [&]() -> FLedger
		{
			FLedger L;
			L.Struct = Held(NStruct); L.Shielding = Held(NShield); L.Oxygen = Sim->GetStock(NOx); L.Food = Sim->GetStock(NFood);
			L.Cells1 = Sim->GetFloorCarvedCells(-1); L.Cells2 = Sim->GetFloorCarvedCells(-2); L.Habs = CountBuildings(TEXT("HabModule"));
			L.Pieces = Sim->GetFurniture().Num(); L.Beds = Sim->GetHousingCapacity(); L.Free = Sim->GetFreeHousing();
			return L;
		};

		// ---------------- (a) The data ----------------
		{
			const TPair<const TCHAR*, double> Pins[] = { { TEXT("GrowthReserveStructKg"), 1000.0 }, { TEXT("GrowthReserveO2Sols"), 10.0 }, { TEXT("GrowthModuleSols"), 1.0 },
				{ TEXT("GrowthCellSols"), 1.0 }, { TEXT("GrowthRoomSols"), 0.5 }, { TEXT("GrowthCellsPerProject"), 4.0 }, { TEXT("GrowthBunksPerHome"), 2.0 } };
			bool bAllPinned = true;
			for (const TPair<const TCHAR*, double>& Pin : Pins) { bAllPinned &= Near(DefsSub->GetConfigTableScalar(FName(Pin.Key), -1.0), Pin.Value, 1e-9); }
			Expect(bAllPinned && Near(Sim->GetGrowthReserveStructKg(), 1000.0, 1e-9) && Near(Sim->GetGrowthReserveO2Sols(), 10.0, 1e-9) && Near(Sim->GetGrowthModuleSols(), 1.0, 1e-9)
				&& Near(Sim->GetGrowthCellSols(), 1.0, 1e-9) && Near(Sim->GetGrowthRoomSols(), 0.5, 1e-9) && Sim->GetGrowthCellsPerProject() == 4 && Sim->GetGrowthBunksPerHome() == 2,
				TEXT("a1 the seven Growth rows are in DT_Config (Struct reserve 1000, O2 reserve 10 sols, module 1.0, cells 1.0, room 0.5 sols, 4 cells, 2 bunks) and the sim reads them (DT/CSV)"));
			int32 CivicRows = 0;
			bool bClean = true, bPiecesOk = true;
			FString CivicList;
			DefsSub->ForEachRoom([&](FName Name, const FRHRoomRow& Row)
			{
				if (!Row.CivicService) { return; }
				++CivicRows;
				CivicList += Name.ToString() + TEXT(" ");
				bClean &= !URHSimWorldSubsystem::RoomEmitsTag(Row, TEXT("Odor")) && Row.MoraleWeight > 0.f && Row.Function != FName("Living");
				TArray<FString> Parts;
				Row.CivicFurniture.ParseIntoArray(Parts, TEXT(";"));
				bPiecesOk &= Parts.Num() > 0;
				for (const FString& P : Parts)
				{
					const FRHFurnitureRow* F = DefsSub->GetFurniture(FName(*P));
					bPiecesOk &= F && F->SliceActive && URHDefinitionsSubsystem::FurnitureAllowsFunction(*F, Row.Function);
				}
			});
			const FRHRoomRow* Dining = DefsSub->GetRoom(FName("Dining"));
			const FRHRoomRow* Infirmary = DefsSub->GetRoom(FName("Infirmary"));
			Expect(CivicRows == 2 && Dining && Dining->CivicService && Dining->CivicFurniture == TEXT("DiningTable;Chair;Chair") && Infirmary && Infirmary->CivicService && Infirmary->CivicFurniture == TEXT("Locker;Lamp;Plant"),
				FString::Printf(TEXT("a2 exactly two live rows are CivicService - %s - with their furniture recipes (DT/CSV)"), *CivicList));
			Expect(bClean && bPiecesOk && !DefsSub->GetRoom(FName("Garden"))->CivicService && !DefsSub->GetRoom(FName("Cooking"))->CivicService && !DefsSub->GetRoom(FName("LivingQuarters"))->CivicService,
				TEXT("a3 no CivicService row emits Odor, each has a Hope weight, and every recipe piece is a live furniture row allowed in its room; Garden, Cooking and Living are not civic"));
			const FRHFurnitureRow* Bunk = DefsSub->GetFurniture(FName("BunkBed"));
			Expect(Bunk && Bunk->SliceActive && Bunk->Provides == FName("Bed") && Bunk->Capacity == 2 && Near(URHDefinitionsSubsystem::GetFurnitureCost(*Bunk).FindRef(NStruct), 80.0, 1e-9)
				&& URHDefinitionsSubsystem::FurnitureAllowsFunction(*Bunk, FName("Living")), TEXT("a4 the Bunk Bed (2 beds, Struct 80, Living rooms) is the home's bed"));
		}

		// ---------------- Setup ----------------
		Sim->Debug_SetGrowthEnabled(false); // until the base is saved
		Sim->Debug_SetBigArrivalDone(true);
		const FVector Head(1000.f, 1000.f, 0.f);
		Place(TEXT("Pylon"), 5000, 0);
		Place(TEXT("Pylon"), -5000, 0);
		Place(TEXT("SolarArray"), -2000, -3500);
		Place(TEXT("SolarArray"), 2000, -3500);
		Place(TEXT("BatteryBank"), 0, -3500);
		const int32 Blocker = Place(TEXT("SolarArray"), 2000, 0); // on lattice cell (2,0): west of N1, in district #1
		const int32 HabA = Place(TEXT("HabModule"), 3500, 0, 2, 1);      // bays (3,0) (4,0)
		const int32 HabB = Place(TEXT("HabModule"), 3500, -1000, 2, 1);  // bays (3,-1) (4,-1)
		const int32 FiltN1 = Place(TEXT("AirFilter"), 3000, 0);
		const int32 HabC = Place(TEXT("HabModule"), -3500, 0, 2, 1);     // bays (-4,0) (-3,0)
		const int32 HabD = Place(TEXT("HabModule"), -3500, -1000, 2, 1); // bays (-4,-1) (-3,-1)
		const int32 FiltN2 = Place(TEXT("AirFilter"), -3000, 0);
		Sim->AddStock(NStruct, 5000.0);
		Sim->AddStock(NShield, 3000.0);
		Sim->AddStock(NOx, 6000.0);
		Sim->AddStock(NWater, 150.0); // under the quota's 200: the first quota stays OPEN
		Sim->AddStock(NFood, 6000.0);
		Sim->ExtendShaft(2, Head);
		Sim->ExcavateFloor(-1, 4, R);
		Sim->ExcavateFloor(-2, 4, R);
		const int32 FiltF1 = Place(TEXT("AirFilter"), 1000, 1500, 1, 1, -1);
		const int32 FiltF2 = Place(TEXT("AirFilter"), 1000, 1500, 1, 1, -2);
		Expect(Blocker && HabA && HabB && HabC && HabD && FiltN1 && FiltN2 && FiltF1 && FiltF2, TEXT("setup: two pylons, a blocker array, four shells, four filters placed"));
		RunSols(3.0);
		Expect(Sim->IsFloorRated(-1) && Sim->IsFloorRated(-2) && Sim->HasVaultRating() && Sim->IsHabRated(HabA) && Sim->IsHabRated(HabC) && Sim->GetHabNetworkBays(HabA) == 4 && Sim->GetHabNetworkBays(HabC) == 4
			&& Sim->GetHabNetworkRoot(HabB) == HabA && Sim->GetHabNetworkRoot(HabD) == HabC,
			FString::Printf(TEXT("setup: floors -1 and -2 rated, the vault certified, N1 (root #%d) and N2 (root #%d) rated at four bays each"), Sim->GetHabNetworkRoot(HabA), Sim->GetHabNetworkRoot(HabC)));
		for (int32 Cell = 0; Cell < 4; ++Cell) { Sim->DesignateRoom(-1, Cell, FName("LivingQuarters"), R); }
		for (const int32 H : { HabA, HabB }) { for (int32 Bay = 0; Bay < 2; ++Bay) { Sim->DesignateBay(H, Bay, FName("LivingQuarters"), R); } }
		Sim->DesignateRoom(-2, 2, FName("Lab"), R);
		Sim->DesignateRoom(-2, 3, FName("Workstation"), R);
		// Painted before anyone lives here: four districts of the five and the Industry one.
		Expect(Send(PaintCmd(NHomes, 2, -2, 8, 2)) && Send(PaintCmd(NCivic, -8, -2, -2, 2)) && Send(PaintCmd(NHomes, 0, 0, 0, 0, -1)) && Send(PaintCmd(NCivic, 0, 0, 0, 0, -2)) && Send(PaintCmd(NIndustry, -1, 2, 1, 2))
			&& Sim->GetDistricts().Num() == 5 && Sim->FindDistrict(1)->Kind == NHomes && Sim->FindDistrict(2)->Kind == NCivic && Sim->FindDistrict(3)->Level == -1 && Sim->FindDistrict(4)->Level == -2 && Sim->FindDistrict(5)->Kind == NIndustry,
			FString::Printf(TEXT("setup: five districts painted - #1 Homes over N1, #2 Civic over N2, #3 Homes floor -1, #4 Civic floor -2, #5 Industry (%s)"), *Note));
		Expect(Sim->GetDistrictOfBuilding(HabA) == 1 && Sim->GetDistrictOfBuilding(HabB) == 1 && Sim->GetDistrictOfBuilding(HabC) == 2 && Sim->GetDistrictOfBuilding(HabD) == 2 && Sim->IsDistrictGrowthOn(1) && Sim->IsDistrictGrowthOn(4) && !Sim->IsDistrictGrowthOn(99),
			TEXT("setup: N1's shells belong to #1, N2's to #2; every district opens with growth on; a district that is not there has no switch"));

		// ---------------- (b) No people, then no districts: no growth ----------------
		{
			Sim->Debug_SetGrowthEnabled(true);
			const int32 SolBefore = Sim->GetGrowthCheckedSol();
			Alerts.Reset();
			RunSols(3.0);
			Expect(Sim->GetGrowthProjects().Num() == 0 && Sim->GetPopulation() == 0 && Sim->GetGrowthCheckedSol() >= SolBefore + 2 && GrowthAlerts() == 0 && Sim->GetGrowthCursor() == 0,
				FString::Printf(TEXT("b1 nobody lives here: three sols with five districts, no project, no alert, the sol check kept time (sol %d -> %d)"), SolBefore, Sim->GetGrowthCheckedSol()));
			Expect(NoteOf(1).Contains(TEXT("nobody lives here")) && NoteOf(3).Contains(TEXT("nobody lives here")) && NoteOf(5).IsEmpty(), FString::Printf(TEXT("b1 the Homes districts say why: '%s'; Industry says nothing"), *NoteOf(1)));
			Sim->Debug_SetGrowthEnabled(false);
		}
		Expect(Sim->Debug_AddColonists(8) == 8, TEXT("setup: eight colonists housed"));
		Sim->Debug_StepPopulation(URHSimClockSubsystem::SubStepSeconds);
		Sim->Debug_SetColonistsPerCell(1.0);
		{
			int32 OnFloor1 = 0, OnFloor2 = 0, InNets = 0;
			for (const FRHColonist& C : Sim->GetColonists()) { OnFloor1 += (C.HomeHab == 0 && C.HomeLevel == -1) ? 1 : 0; OnFloor2 += (C.HomeHab == 0 && C.HomeLevel == -2) ? 1 : 0; InNets += C.HomeHab != 0 ? 1 : 0; }
			const FRHCityDemand Dm = Sim->GetCityDemand();
			Expect(OnFloor1 == 4 && OnFloor2 == 4 && InNets == 0 && Dm.HousingBedsShort > 0 && Dm.ServiceGaps.Num() > 0,
				FString::Printf(TEXT("setup: floors fill first - four on floor -1, four on floor -2 (every carved cell is a bed), none in the networks; the city is short %d bed(s) (%s), so there is demand"), Dm.HousingBedsShort, *Dm.HousingLimit.ToString()));
		}
		// No districts, with demand and material and Hope: nothing is built. (A separate run from a saved base below.)
		FString Err;
		Expect(Sim->SaveColony(TEXT("citygrowth_base"), Err), FString::Printf(TEXT("setup: the base state is saved (%s)"), *Err));
		const auto LoadBase = [&]()
		{
			FString E;
			const bool bOk = Sim->LoadColony(TEXT("citygrowth_base"), E) && E.IsEmpty();
			Sim->Debug_SetGrowthEnabled(true);
			Sim->Debug_SetBigArrivalDone(true);
			Sim->Debug_SetColonistsPerCell(1.0);
			PinHope = -1.0;
			Rejects.Reset(); Alerts.Reset(); StartLog.Reset(); SeenOpen.Reset();
			return bOk;
		};

		// ---------------- (b2) A colony with no districts ----------------
		{
			Expect(LoadBase(), TEXT("b2 base loaded"));
			for (int32 Id = 1; Id <= 5; ++Id) { Send(EraseCmd(Id)); }
			Expect(Sim->GetDistricts().Num() == 0, TEXT("b2 every district erased"));
			SetHope(100.0);
			Alerts.Reset();
			const FLedger L0 = Ledger();
			RunSols(3.0);
			const FLedger L1 = Ledger();
			Expect(Sim->GetGrowthProjects().Num() == 0 && GrowthAlerts() == 0 && L1.Habs == L0.Habs && L1.Cells1 == L0.Cells1 && L1.Pieces == L0.Pieces && Near(L1.Struct, L0.Struct, 1e-6) && Sim->GetCityDemand().HousingBedsShort > 0,
				TEXT("b2 no districts, with housing short, Hope pinned at 100 and thousands of kg of material: three sols build nothing and say nothing"));
		}

		// ---------------- (b3) Money ----------------
		{
			Expect(LoadBase(), TEXT("b3 base loaded"));
			OnlyDistricts({ 3 }); // floor -1 Homes: bill Struct 640 (4 cells x 2 bunks x 80), Oxygen 400 (4 x 100)
			SetHope(80.0);
			Sim->Debug_SetHopeSmoothed(80.0);
			Expect(Near(Sim->GetGrowthAvailability(NStruct).Reserve, 1000.0, 1e-9) && Near(Sim->GetGrowthAvailability(NOx).Reserve, 10.0 * 8.0 * 0.75, 1e-9) && Near(Sim->GetGrowthAvailability(NShield).Reserve, 0.0, 1e-9) && Near(Sim->GetGrowthAvailability(NWater).Reserve, 0.0, 1e-9),
				TEXT("b3 the reserves: Struct 1000; Oxygen 10 sols x 8 colonists x 0.75 = 60 kg (the breathing floor); Shielding and the rest none"));
			Expect(Near(Sim->GetGrowthAvailability(NStruct).QuotaBill, 300.0, 1e-9) && Near(Sim->GetGrowthAvailability(NOx).QuotaBill, 50.0, 1e-9) && Near(Sim->GetGrowthAvailability(NShield).QuotaBill, 0.0, 1e-9),
				TEXT("b3 the open quota's bills stand between growth and the stores: Struct 300, Oxygen 50 (Q1 is open: its Water bill of 200 is not met)"));
			const double StructHeld = Held(NStruct), OxHeld = Held(NOx);
			const URHSimWorldSubsystem::FRHGrowthAvailability SA = Sim->GetGrowthAvailability(NStruct);
			Expect(Near(SA.Free, SA.Held - 300.0 - 1000.0, 1e-9) && Near(SA.Held, StructHeld, 1e-9) && Near(StructHeld, 5400.0, 1e-3), FString::Printf(TEXT("b3 free = held - quota - reserve: %.0f = %.0f - 300 - 1000 (the pool's 5,000 and the Lander's starter 400)"), SA.Free, SA.Held));
			// Struct one kilo short of reserve + quota + bill.
			SetHeld(NStruct, 300.0 + 1000.0 + 640.0 - 1.0);
			Expect(Sim->Debug_RunGrowthCheck() == 0 && !Open(3) && NoteOf(3).Contains(TEXT("Struct")), FString::Printf(TEXT("b3 Struct one kilo short (%.0f held): no project - '%s'"), Held(NStruct), *NoteOf(3)));
			SetHeld(NStruct, 300.0 + 1000.0 + 640.0 + 1.0);
			const double OxBefore = Held(NOx), StructBefore = Held(NStruct);
			Expect(Sim->Debug_RunGrowthCheck() == 1 && Open(3), TEXT("b3 Struct one kilo over: the project starts"));
			const FRHGrowthProject* P = Open(3);
			Expect(P && P->Kind == ERHGrowthKind::FloorCells && P->Cells == 4 && P->Level == -1 && Near(P->Bill.FindRef(NStruct), 640.0, 1e-9) && Near(P->Bill.FindRef(NOx), 400.0, 1e-9) && P->Bill.Num() == 2 && Near(P->TotalSols, 1.0, 1e-9) && Near(P->RemainingSols, 1.0, 1e-9),
				TEXT("b3 the floor project's bill is Struct 640 and the Oxygen fill of its four new cells, 400 kg: nothing else; a sol's work"));
			Expect(Near(Held(NStruct), StructBefore - 640.0, 1e-6) && Near(Held(NOx), OxBefore - 400.0, 1e-6) && Sim->GetGrowthCursor() == 3, TEXT("b3 the whole bill left the holdings at the start, and the cursor rests on #3"));
			// Oxygen: a fresh load, Struct ample, O2 one kilo short of reserve + quota + bill (60 + 50 + 400).
			Expect(LoadBase(), TEXT("b3 base loaded again"));
			OnlyDistricts({ 3 });
			SetHope(80.0);
			SetHeld(NOx, 60.0 + 50.0 + 400.0 - 1.0);
			Expect(Sim->Debug_RunGrowthCheck() == 0 && NoteOf(3).Contains(TEXT("Oxygen")), FString::Printf(TEXT("b3 Oxygen one kilo short of reserve + quota + bill (%.0f held): no project - '%s'"), Held(NOx), *NoteOf(3)));
			SetHeld(NOx, 60.0 + 50.0 + 400.0 + 1.0);
			Expect(Sim->Debug_RunGrowthCheck() == 1 && Open(3), TEXT("b3 one kilo over: the project starts"));
			// Shielding short on the surface bill (Struct plentiful): nothing is taken, not even the Struct that would fit.
			Expect(LoadBase(), TEXT("b3 base loaded a third time"));
			OnlyDistricts({ 1 });
			SetHope(80.0);
			SetHeld(NShield, 0.0);
			const double StructAll = Held(NStruct), OxAll = Held(NOx);
			Expect(Sim->Debug_RunGrowthCheck() == 0 && !Open(1) && NoteOf(1).Contains(TEXT("Shielding")) && Near(Held(NStruct), StructAll, 1e-9) && Near(Held(NOx), OxAll, 1e-9),
				FString::Printf(TEXT("b3 Shielding held at 0: the module's bill does not fit, and nothing is taken - the Struct and Oxygen stay where they are ('%s')"), *NoteOf(1)));
			SetHeld(NShield, 199.0);
			Expect(Sim->Debug_RunGrowthCheck() == 0 && Near(Held(NStruct), StructAll, 1e-9), TEXT("b3 Shielding 199 of the 200 the four bays need: still nothing, still nothing taken"));
			SetHeld(NShield, 200.0);
			Expect(Sim->Debug_RunGrowthCheck() == 1 && Open(1) && Near(Held(NShield), 0.0, 1e-6), TEXT("b3 Shielding 200: the project starts and takes the last of it"));
			// A construction site's hopper is spoken for: it is never free.
			Expect(LoadBase(), TEXT("b3 base loaded a fourth time"));
			FRHCommand Build; Build.Verb = FName("Build"); Build.Target = FName("Stockpile"); Build.Location = FVector(0.0, -2000.0, 0.0);
			Send(Build);
			int32 SiteId = 0;
			for (const FRHBuildingInstance& B : Sim->GetBuildings()) { if (B.bUnderConstruction) { SiteId = B.Id; } }
			const double HeldBefore = Held(NStruct), TotalBefore = Sim->GetTotalSolid(NStruct);
			Sim->Debug_AddSolidAt(SiteId, NStruct, 7000.0, false);
			Expect(SiteId != 0 && Near(Held(NStruct), HeldBefore, 1e-6) && Near(Sim->GetTotalSolid(NStruct), TotalBefore + 7000.0, 1e-6),
				FString::Printf(TEXT("b3 a construction site (#%d) holds 7,000 kg of Struct: GetTotalSolid sees it, growth's holdings do not (%.0f)"), SiteId, Held(NStruct)));
			// The quota met: its bill is gone.
			Expect(LoadBase(), TEXT("b3 base loaded a fifth time"));
			Sim->AddStock(NWater, 400.0);
			Steps(2);
			// C5c moved this line: Q1 met no longer frees the stores. Between rounds growth leaves the NEXT round's bill
			// standing - Q1's next is Q2's authored Struct 600 / Oxygen 150 (/ Hydrogen 40) - so free = held - 600 - 1000.
			Expect(Sim->GetQuotaPhase() != ERHQuotaPhase::Open && Sim->GetGrowthAvailability(NStruct).bNextRound && Near(Sim->GetGrowthAvailability(NStruct).QuotaBill, 600.0, 1e-9) && Near(Sim->GetGrowthAvailability(NOx).QuotaBill, 150.0, 1e-9)
				&& Near(Sim->GetGrowthAvailability(NStruct).Free, Held(NStruct) - 600.0 - 1000.0, 1e-6),
				TEXT("b3 once Q1 is met its bill drops out and the next round's (Q2: Struct 600, Oxygen 150) stands in its place: free = held - 600 - 1000 (C5c; it was held - 1000 when nothing was protected between rounds)"));
		}

		// ---------------- (b4) Between quota rounds growth leaves the NEXT round's bill standing (C5c) ----------------
		{
			const auto MeetQuota = [&]() -> bool
			{
				for (const auto& L : Sim->GetQuotaProgress())
				{
					const double Gap = L.Value.Value + 50.0 - L.Value.Key;
					if (Gap > 0.0) { Sim->AddStock(L.Key, Gap); }
				}
				for (int32 K = 0; K < 40 && Sim->GetQuotaPhase() == ERHQuotaPhase::Open; ++K) { Land(); }
				return Sim->GetQuotaPhase() == ERHQuotaPhase::AwaitingManifest;
			};
			const auto LaunchAndLand = [&]() -> bool
			{
				FString LErr;
				if (!Sim->LaunchShip(LErr)) { return false; }
				for (int32 K = 0; K < 40 * (int32)(SolLen / EraDt) && Sim->GetQuotaPhase() != ERHQuotaPhase::Open; ++K) { Land(); }
				return Sim->GetQuotaPhase() == ERHQuotaPhase::Open;
			};
			// One growth start check against a standing bill: a kilo short of reserve + bill + project is refused, a kilo over starts.
			const auto Boundary = [&](FName Res, double Reserve, double Bill, double Project, const TCHAR* Phase, const TCHAR* Round) -> bool
			{
				Sim->Debug_SetGrowthEnabled(true);
				OnlyDistricts({ 3 });
				SetHope(80.0);
				SetHeld(Res == NStruct ? NOx : NStruct, 1.0e6); // the other resource is never the one in the way
				SetHeld(Res, Reserve + Bill + Project - 1.0);
				const bool bRefused = Sim->Debug_RunGrowthCheck() == 0 && !Open(3) && NoteOf(3).Contains(*Res.ToString()) && NoteOf(3).Contains(TEXT("next quota round"));
				const FString Why = NoteOf(3);
				SetHeld(Res, Reserve + Bill + Project + 1.0);
				const bool bStarts = Sim->Debug_RunGrowthCheck() == 1 && Open(3);
				Expect(bRefused && bStarts, FString::Printf(TEXT("b4 %s (%s): %s one kilo under reserve %.0f + the next round's bill %.0f + the project %.0f is refused ('%s'), one kilo over starts it"), Phase, Round, *Res.ToString(), Reserve, Bill, Project, *Why));
				return bRefused && bStarts;
			};

			// Q1 met, awaiting the manifest: the next round is Q2, Struct 600 / Oxygen 150 / Hydrogen 40.
			Expect(LoadBase(), TEXT("b4 base loaded"));
			Sim->Debug_SetGrowthEnabled(false);
			Expect(MeetQuota() && Sim->GetActiveQuotaName() == FName("Q1"), FString::Printf(TEXT("b4 setup: Q1 met, awaiting the manifest (phase %d)"), (int32)Sim->GetQuotaPhase()));
			Boundary(NStruct, 1000.0, 600.0, 640.0, TEXT("awaiting the manifest"), TEXT("Q1 -> Q2"));
			Expect(LoadBase(), TEXT("b4 base loaded again"));
			Sim->Debug_SetGrowthEnabled(false);
			MeetQuota();
			Boundary(NOx, 60.0, 150.0, 400.0, TEXT("awaiting the manifest"), TEXT("Q1 -> Q2"));
			// Ship inbound: the same bill stands.
			Expect(LoadBase(), TEXT("b4 base loaded a third time"));
			Sim->Debug_SetGrowthEnabled(false);
			MeetQuota();
			FString LaunchErr;
			Expect(Sim->LaunchShip(LaunchErr) && Sim->GetQuotaPhase() == ERHQuotaPhase::ShipInbound && Near(Sim->GetGrowthAvailability(NStruct).QuotaBill, 600.0, 1e-9) && Near(Sim->GetGrowthAvailability(NHyd).QuotaBill, 40.0, 1e-9) && Sim->GetGrowthAvailability(NStruct).bNextRound,
				FString::Printf(TEXT("b4 the ship is inbound (phase %d): the next round's bill still stands, Struct 600 and Hydrogen 40 (%s)"), (int32)Sim->GetQuotaPhase(), *LaunchErr));
			Boundary(NStruct, 1000.0, 600.0, 640.0, TEXT("ship inbound"), TEXT("Q1 -> Q2"));
			// The open quota's own bill is unchanged while it is Open (the b3 numbers): not the next round's.
			Expect(LoadBase(), TEXT("b4 base loaded a fourth time"));
			Expect(Sim->GetQuotaPhase() == ERHQuotaPhase::Open && !Sim->GetGrowthAvailability(NStruct).bNextRound && Near(Sim->GetGrowthAvailability(NStruct).QuotaBill, 300.0, 1e-9) && Near(Sim->GetGrowthAvailability(NHyd).QuotaBill, 0.0, 1e-9),
				TEXT("b4 while Q1 is open its own bill (Struct 300, no Hydrogen) is what stands, not the next round's"));

			// The ladder walked for real: at each round's end the protected bill is what the ladder then opens
			// (Q2's, then Q3's, then Q3 x 1.25 and x 1.25^2 in whole kg), the same numbers GetQuotaProgress reads after it.
			Sim->Debug_SetGrowthEnabled(false);
			struct FNextBill { const TCHAR* Name; double S, O, H; };
			const FNextBill Ladder[4] = { { TEXT("Q2"), 600.0, 150.0, 40.0 }, { TEXT("Q3"), 2500.0, 1200.0, 250.0 }, { TEXT("Q3 round 1"), 3125.0, 1500.0, 313.0 }, { TEXT("Q3 round 2"), 3906.0, 1875.0, 391.0 } };
			for (const FNextBill& Rung : Ladder)
			{
				const bool bMet = MeetQuota();
				const URHSimWorldSubsystem::FRHGrowthAvailability AS = Sim->GetGrowthAvailability(NStruct), AO = Sim->GetGrowthAvailability(NOx), AH = Sim->GetGrowthAvailability(NHyd);
				const bool bPred = AS.bNextRound && Near(AS.QuotaBill, Rung.S, 1e-9) && Near(AO.QuotaBill, Rung.O, 1e-9) && Near(AH.QuotaBill, Rung.H, 1e-9);
				const bool bLanded = LaunchAndLand();
				const TMap<FName, TPair<double, double>> Opened = Sim->GetQuotaProgress();
				const bool bSame = Opened.Num() == 3 && Near(Opened.FindRef(NStruct).Value, AS.QuotaBill, 1e-9) && Near(Opened.FindRef(NOx).Value, AO.QuotaBill, 1e-9) && Near(Opened.FindRef(NHyd).Value, AH.QuotaBill, 1e-9);
				Expect(bMet && bPred && bLanded && bSame,
					FString::Printf(TEXT("b4 the ladder, for real: before %s opens the protected bill reads Struct %.0f / Oxygen %.0f / Hydrogen %.0f (expect %.0f / %.0f / %.0f), and the quota that then opens asks %.0f / %.0f / %.0f - the same"),
						Rung.Name, AS.QuotaBill, AO.QuotaBill, AH.QuotaBill, Rung.S, Rung.O, Rung.H, Opened.FindRef(NStruct).Value, Opened.FindRef(NOx).Value, Opened.FindRef(NHyd).Value));
			}
			// On the last rung's round 2, met and awaiting: the next round is round 3, 4883 / 2344 / 488.
			Expect(MeetQuota() && Sim->GetQuotaRepeatRound() == 2 && Near(Sim->GetGrowthAvailability(NStruct).QuotaBill, 4883.0, 1e-9) && Near(Sim->GetGrowthAvailability(NOx).QuotaBill, 2344.0, 1e-9) && Near(Sim->GetGrowthAvailability(NHyd).QuotaBill, 488.0, 1e-9),
				FString::Printf(TEXT("b4 Q3 round 2 met: the next round (3) stands at Struct %.0f / Oxygen %.0f / Hydrogen 488 - the current round's bill x 1.25, rounded as the ladder rounds"), Sim->GetGrowthAvailability(NStruct).QuotaBill, Sim->GetGrowthAvailability(NOx).QuotaBill));
			Boundary(NStruct, 1000.0, 4883.0, 640.0, TEXT("awaiting the manifest"), TEXT("Q3 round 2 -> 3"));
			// With the repeat switched off the ladder ends: nothing is coming, so nothing stands.
			Sim->Debug_SetQuotaRepeatBillMul(0.0);
			Expect(Sim->GetQuotaPhase() == ERHQuotaPhase::AwaitingManifest && Near(Sim->GetGrowthAvailability(NStruct).QuotaBill, 0.0, 1e-9) && Near(Sim->GetGrowthAvailability(NOx).QuotaBill, 0.0, 1e-9)
				&& Near(Sim->GetGrowthAvailability(NStruct).Free, Held(NStruct) - 1000.0, 1e-6),
				TEXT("b4 the repeat knob at 0 on the last rung: the ladder ends, no bill stands, free = held - 1000"));
			Sim->Debug_SetQuotaRepeatBillMul(1.25);
		}

		// ---------------- (c) Gates and pauses ----------------
		{
			Expect(LoadBase(), TEXT("c base loaded"));
			SetHope(80.0);
			OnlyDistricts({ 5 });
			Expect(Sim->Debug_RunGrowthCheck() == 0 && Sim->GetGrowthProjects().Num() == 0, TEXT("c1 an Industry district never grows: the player places Industry, Farming and Research (Q2a)"));
			OnlyDistricts({ 3 });
			Expect(Sim->Debug_RunGrowthCheck() == 1 && Open(3), TEXT("c2 Homes on floor -1 with housing short, Hope 80 and money: the project starts"));
			Expect(Sim->Debug_RunGrowthCheck() == 0 && NoteOf(3).Contains(TEXT("under way")) && Sim->GetGrowthProjects().Num() == 1, FString::Printf(TEXT("c2 a district with an open project is skipped ('%s')"), *NoteOf(3)));
			// The switch.
			Expect(LoadBase(), TEXT("c base loaded"));
			SetHope(80.0);
			OnlyDistricts({ 3 });
			Expect(Sim->SetDistrictGrowth(3, false) && !Sim->IsDistrictGrowthOn(3) && !Sim->SetDistrictGrowth(99, true), TEXT("c3 the switch moves for a district that is there, and for none that is not"));
			Expect(Sim->Debug_RunGrowthCheck() == 0 && NoteOf(3).Contains(TEXT("switched off")), FString::Printf(TEXT("c3 growth switched off: no project ('%s')"), *NoteOf(3)));
			Sim->SetDistrictGrowth(3, true);
			Expect(Sim->Debug_RunGrowthCheck() == 1 && Open(3), TEXT("c3 switched back on: the project starts"));
			// A project already open still finishes when its district is switched off.
			Sim->SetDistrictGrowth(3, false);
			const int32 Cells0 = Sim->GetFloorCarvedCells(-1);
			Expect(RunToEnd(3, 40) > 0 && Sim->GetFloorCarvedCells(-1) == Cells0 + 4, TEXT("c3 a project that is open when its district is switched off still finishes (the switch stops new starts)"));
			// The switch is part of the save.
			Sim->SetDistrictGrowth(4, true);
			Expect(Sim->SaveColony(TEXT("citygrowth_switch"), Err) && !Sim->IsDistrictGrowthOn(3) && Sim->SetDistrictGrowth(4, true), TEXT("c3 saved with #3 off and #4 on, then both switches flipped"));
			Sim->SetDistrictGrowth(3, true);
			Sim->SetDistrictGrowth(4, false);
			Expect(Sim->LoadColony(TEXT("citygrowth_switch"), Err) && !Sim->IsDistrictGrowthOn(3) && Sim->IsDistrictGrowthOn(4), TEXT("c3 loaded: #3 is still off and #4 back on - the switch round-trips"));
		}
		{
			Expect(LoadBase(), TEXT("c base loaded"));
			OnlyDistricts({ 3 });
			// A home on short rations: no start.
			SetHope(80.0);
			const double Food = Sim->GetStock(NFood);
			Sim->AddStock(NFood, -Food);
			Sim->Debug_StepPopulation(URHSimClockSubsystem::SubStepSeconds);
			Expect(Sim->GetHomeRations().Num() > 0 && Sim->GetHomeRations()[0].Fraction < 1.0 && Sim->Debug_RunGrowthCheck() == 0 && Sim->GetGrowthPause().Contains(TEXT("short rations")) && Sim->GetGrowthProjects().Num() == 0,
				FString::Printf(TEXT("c4 food out, a home on short rations: no start - '%s'"), *Sim->GetGrowthPause()));
			Sim->AddStock(NFood, Food);
			Sim->Debug_StepPopulation(URHSimClockSubsystem::SubStepSeconds);
			Expect(Sim->Debug_RunGrowthCheck() == 1 && Sim->GetGrowthPause().IsEmpty(), TEXT("c4 the food back, rations full: the project starts and the pause is gone"));
			// Hope failing: no start.
			Expect(LoadBase(), TEXT("c base loaded"));
			OnlyDistricts({ 3 });
			Sim->Debug_SetHopeSmoothed(8.0);
			Expect(Sim->GetHopeBand() == URHSimWorldSubsystem::ERHHopeBand::Failing && Sim->Debug_RunGrowthCheck() == 0 && Sim->GetGrowthPause().Contains(TEXT("Hope")) && Sim->GetGrowthProjects().Num() == 0,
				FString::Printf(TEXT("c5 smoothed Hope at 8, the Failing band: no start - '%s'"), *Sim->GetGrowthPause()));
			Sim->Debug_SetHopeSmoothed(40.0);
			Expect(Sim->GetHopeBand() != URHSimWorldSubsystem::ERHHopeBand::Failing && Sim->Debug_RunGrowthCheck() == 1, TEXT("c5 Hope back to 40 (Strained): the project starts - only Failing pauses it"));
			// Housing not short: no Homes project.
			Expect(LoadBase(), TEXT("c base loaded"));
			OnlyDistricts({ 1, 3 });
			SetHope(80.0);
			for (int32 Bay = 0; Bay < 2; ++Bay) { Sim->DesignateBay(HabC, Bay, FName("LivingQuarters"), R); Sim->DesignateBay(HabD, Bay, FName("LivingQuarters"), R); }
			Sim->DesignateRoom(-2, 0, FName("LivingQuarters"), R);
			Sim->DesignateRoom(-2, 1, FName("LivingQuarters"), R);
			const FRHCityDemand Roomy = Sim->GetCityDemand();
			Expect(Roomy.HousingBedsShort <= 0 && Sim->Debug_RunGrowthCheck() == 0 && NoteOf(1).Contains(TEXT("not short")) && NoteOf(3).Contains(TEXT("not short")),
				FString::Printf(TEXT("c6 six more Living places zoned: housing short %d, so no Homes project - '%s'"), Roomy.HousingBedsShort, *NoteOf(1)));
		}

		// ---------------- (d) Surface Homes ----------------
		{
			Expect(LoadBase(), TEXT("d base loaded"));
			OnlyDistricts({ 1 });
			SetHope(80.0);
			Expect(Sim->Debug_RunGrowthCheck() == 1 && Open(1), TEXT("d1 the surface Homes project starts"));
			const FRHGrowthProject P1 = *Open(1);
			const FRHDistrict* D1 = Sim->FindDistrict(1);
			bool bInside = true;
			for (int32 DY = 0; DY < P1.Size.Y; ++DY) { for (int32 DX = 0; DX < P1.Size.X; ++DX) { bInside &= Sim->GetDistrictAtCell(P1.Cell + FIntPoint(DX, DY)) == 1; } }
			Expect(P1.Kind == ERHGrowthKind::SurfaceModule && P1.Cell == FIntPoint(3, 1) && P1.Size == FIntPoint(2, 2) && P1.HabId == HabA && P1.Level == 0 && bInside && D1,
				FString::Printf(TEXT("d1 the nearest candidate to the network root: the 2 x 2 at lattice (%d,%d), every cell inside the district (cells (2,0) is blocked by the array, (5,0) by the pylon)"), P1.Cell.X, P1.Cell.Y));
			Expect(Near(P1.Bill.FindRef(NStruct), 1440.0, 1e-9) && Near(P1.Bill.FindRef(NShield), 200.0, 1e-9) && Near(P1.Bill.FindRef(NOx), 400.0, 1e-9) && P1.Bill.Num() == 3 && Near(P1.TotalSols, 1.0, 1e-9),
				TEXT("d1 the bill: four bays at Struct 200 and Shielding 50, eight bunks at Struct 80, 100 kg of air a bay = Struct 1,440, Shielding 200, Oxygen 400"));
			Expect(Sim->GetGrowthProjectBeds(P1) == 16 && Sim->DescribeGrowthProject(P1).Contains(TEXT("4 new home bays")), FString::Printf(TEXT("d1 it will add 16 beds: '%s'"), *Sim->DescribeGrowthProject(P1)));
			// Determinism: the same state chooses the same site.
			Expect(LoadBase(), TEXT("d base loaded"));
			OnlyDistricts({ 1 });
			SetHope(80.0);
			Sim->Debug_RunGrowthCheck();
			const FRHGrowthProject P1b = Open(1) ? *Open(1) : FRHGrowthProject();
			Expect(P1b.Cell == P1.Cell && P1b.Size == P1.Size && P1b.HabId == P1.HabId && P1b.Bill.Num() == P1.Bill.Num() && Near(P1b.Bill.FindRef(NStruct), P1.Bill.FindRef(NStruct), 1e-12), TEXT("d2 loaded again: the very same site and bill - the choice is deterministic"));
			// The completion, step by step: the network stays rated, the new air is full from the first step.
			const double RootO2Before = Sim->GetHabNetworkO2Kg(HabA);
			const int32 HabsBefore = CountBuildings(TEXT("HabModule"));
			bool bRatedThrough = true;
			int32 Took = -1;
			for (int32 K = 1; K <= 40 && Took < 0; ++K)
			{
				Land();
				bRatedThrough &= Sim->IsHabRated(HabA) && Sim->IsHabRated(HabB) && Sim->IsFloorRated(-1);
				if (!Open(1)) { Took = K; }
			}
			Expect(Took >= 20 && Took <= 21, FString::Printf(TEXT("d3 a sol's work is %d era steps (20 x 60 s): the countdown is linear"), Took));
			int32 NewId = 0;
			for (const FRHBuildingInstance& B : Sim->GetBuildings()) { if (B.DefName == FName("HabModule") && B.SizeX == 2 && B.SizeY == 2) { NewId = B.Id; } }
			const FRHBuildingInstance* NB = nullptr;
			for (const FRHBuildingInstance& B : Sim->GetBuildings()) { if (B.Id == NewId) { NB = &B; } }
			Expect(CountBuildings(TEXT("HabModule")) == HabsBefore + 1 && NB && !NB->bUnderConstruction && Near(NB->LocationCm.X, 3500.0, 1e-6) && Near(NB->LocationCm.Y, 1500.0, 1e-6) && Sim->GetHabNetworkRoot(NewId) == HabA && Sim->GetHabNetworkBays(HabA) == 8,
				TEXT("d3 the module stands: completed at once (no site, no robots), at lattice (3,1) 2 x 2, joined to N1 - eight bays"));
			int32 Living = 0, Bunks = 0;
			for (int32 Bay = 0; Bay < 4; ++Bay)
			{
				Living += Sim->GetBayRoom(NewId, Bay) == FName("LivingQuarters") ? 1 : 0;
				Bunks += CountRow(PiecesIn(FIntVector(0, Bay, NewId)), TEXT("BunkBed"));
			}
			Expect(Living == 4 && Bunks == 8 && Sim->GetHabBeds(NewId) == 16 && Near(Sim->GetHabO2Kg(NewId), 400.0, 25.0), FString::Printf(TEXT("d3 four Living bays with two bunks each: %d bunks, %d beds, %.0f kg of air"), Bunks, Sim->GetHabBeds(NewId), Sim->GetHabO2Kg(NewId)));
			Expect(bRatedThrough && Sim->IsHabRated(NewId) && Sim->GetHabNetworkO2Kg(HabA) >= RootO2Before + 380.0 - 60.0, TEXT("d3 N1 and floor -1 stayed rated at EVERY step through the completion, and the new bays' air was in place (the O2 pre-fill)"));
			Expect(Sim->GetDistrictOfBuilding(NewId) == 1 && Sim->GetDistrictOfBay(NewId, 3) == 1, TEXT("d3 the new module belongs to district #1"));
			// Only the target network: a lone shell north of the 2 x 2 spoils it; the 2 x 1 fits.
			Expect(LoadBase(), TEXT("d base loaded"));
			OnlyDistricts({ 1 });
			SetHope(80.0);
			const int32 Lone = Place(TEXT("HabModule"), 3000, 3000); // lattice (3,3), outside the district: touches (3,2)
			Sim->Debug_RunGrowthCheck();
			const FRHGrowthProject* P2 = Open(1);
			Expect(Lone && P2 && P2->Cell == FIntPoint(3, 1) && P2->Size == FIntPoint(2, 1),
				FString::Printf(TEXT("d4 a lone shell on (3,3): the 2 x 2 at (3,1) would touch it, so the city builds the 2 x 1 at (%d,%d) - a module touches ONLY the network it extends"), P2 ? P2->Cell.X : -99, P2 ? P2->Cell.Y : -99));
			// A road across the best ground: off the strip.
			Expect(LoadBase(), TEXT("d base loaded"));
			OnlyDistricts({ 1 });
			SetHope(80.0);
			Sim->Debug_LayRoad(FVector(1500.0, 1500.0, 0.0), FVector(5500.0, 1500.0, 0.0), true);
			Sim->Debug_RunGrowthCheck();
			const FRHGrowthProject* P3 = Open(1);
			FString RoadWhat;
			bool bClear = P3 != nullptr;
			if (P3)
			{
				const FVector Centre((P3->Cell.X + (P3->Size.X - 1) * 0.5) * 1000.0, (P3->Cell.Y + (P3->Size.Y - 1) * 0.5) * 1000.0, 0.0);
				bClear = !Sim->FindRoadOverlap(Centre, FVector2D(P3->Size.X * 500.0, P3->Size.Y * 500.0), RoadWhat) && P3->Cell != FIntPoint(3, 1);
			}
			Expect(bClear, FString::Printf(TEXT("d5 a road along y = 15 m: the module is off the strip, at (%d,%d) %d x %d"), P3 ? P3->Cell.X : -99, P3 ? P3->Cell.Y : -99, P3 ? P3->Size.X : 0, P3 ? P3->Size.Y : 0));
			// Door-sealed networks are skipped, and the note says so.
			Expect(LoadBase(), TEXT("d base loaded"));
			OnlyDistricts({ 1 });
			SetHope(80.0);
			const auto Fit = [&](const TCHAR* Row, double X, double Y, int32 Dir)
			{
				FRHCommand C; C.Verb = FName("Fit"); C.Target = FName(Row); C.Location = FVector(X, Y, 0.0); C.Value = (double)Dir;
				return Send(C);
			};
			Expect(Fit(TEXT("PressureDoor"), 3000, -1000, 2) && Fit(TEXT("PressureDoor"), 4000, -1000, 2) && Sim->IsHabNetworkSealed(HabA),
				FString::Printf(TEXT("d6 a door in each of N1's two doorways seals it (%s)"), *Note));
			SetHope(80.0);
			Expect(Sim->Debug_RunGrowthCheck() == 0 && !Open(1) && NoteOf(1).Contains(TEXT("door-sealed")), FString::Printf(TEXT("d6 a sealed network is not extended - the note says so: '%s'"), *NoteOf(1)));
			// No habitat of the district: Homes district with nothing to extend.
			Expect(LoadBase(), TEXT("d base loaded"));
			Expect(Send(PaintCmd(NHomes, -1, -2, 1, -1)) && Sim->GetDistricts().Num() == 6, FString::Printf(TEXT("d7 a sixth district, Homes on open ground with no habitat (%s)"), *Note));
			OnlyDistricts({ 6 });
			SetHope(80.0);
			Expect(Sim->Debug_RunGrowthCheck() == 0 && NoteOf(6).Contains(TEXT("founds habitats")), FString::Printf(TEXT("d7 a Homes district with no habitat: nothing to extend - '%s' (the player founds habitats, the city grows them)"), *NoteOf(6)));
		}

		// ---------------- (d8) No site, no order: growth never touches the era band ----------------
		{
			Expect(LoadBase(), TEXT("d8 base loaded"));
			OnlyDistricts({ 1 });
			SetHope(80.0);
			FString W0, W1;
			const bool bBefore = Sim->CanEnterEraMode(W0);
			const int32 Tasks0 = Sim->GetOpenTaskCount();
			Sim->Debug_RunGrowthCheck();
			const bool bAfter = Sim->CanEnterEraMode(W1);
			int32 Sites = 0;
			for (const FRHBuildingInstance& B : Sim->GetBuildings()) { Sites += B.bUnderConstruction ? 1 : 0; }
			Expect(Open(1) && bBefore == bAfter && W0 == W1 && Sites == 0 && Sim->GetOpenTaskCount() == Tasks0 && Sim->GetUplinkQueue().Num() == 0,
				FString::Printf(TEXT("d8 with a surface project open: no construction site, no task, no uplink order - CanEnterEraMode answers as before (%s%s)"), bAfter ? TEXT("yes") : TEXT("no: "), *W1));
			// The placement check without the material part: the same site, no Struct and no Shielding in the stores.
			SetHeld(NStruct, 0.0);
			SetHeld(NShield, 0.0);
			FString Why;
			Sim->Debug_SetGrowthEnabled(false);
			const bool bWith = Sim->CanPlaceBuildingSized(FName("HabModule"), FVector(3000.0, 2000.0, 0.0), Why, 0, 1, 1, true);
			FString WhyNot;
			const bool bWithout = Sim->CanPlaceBuildingSized(FName("HabModule"), FVector(3000.0, 2000.0, 0.0), WhyNot, 0, 1, 1, false);
			Expect(!bWith && Why.Contains(TEXT("Insufficient")) && bWithout, FString::Printf(TEXT("d8 with the stores empty a habitat bay is refused for want of material (%s) and accepted with the material check off - every other rule still holds (%s)"), *Why, *WhyNot));
			Expect(!Sim->CanPlaceBuildingSized(FName("HabModule"), FVector(2000.0, 0.0, 0.0), Why, 0, 1, 1, false) && !Sim->CanPlaceBuildingSized(FName("HabModule"), FVector(3000.0, 0.0, 0.0), Why, 0, 1, 1, false),
				TEXT("d8 ... and the site rules still bite with the material check off: a bay on the array and a bay on N1's own ground are refused"));
		}

		// ---------------- (e) Underground Homes ----------------
		{
			Expect(LoadBase(), TEXT("e base loaded"));
			OnlyDistricts({ 3 });
			SetHope(80.0);
			const double Hyd0 = Sim->GetStock(NHyd);
			Sim->Debug_UnrateFloor(-1);
			Expect(Sim->Debug_RunGrowthCheck() == 0 && NoteOf(3).Contains(TEXT("not rated")), FString::Printf(TEXT("e1 only on a RATED floor: floor -1 unrated, no project - '%s'"), *NoteOf(3)));
			Expect(LoadBase(), TEXT("e base loaded"));
			OnlyDistricts({ 3 });
			SetHope(80.0);
			const int32 Beds0 = Sim->GetFloorBeds(-1), Comfort0 = Sim->GetHousingComfortCapacity();
			Sim->Debug_RunGrowthCheck();
			const FRHGrowthProject* P = Open(3);
			Expect(P && P->Kind == ERHGrowthKind::FloorCells && P->FirstCell == 4 && P->Cells == 4, TEXT("e2 the project: four cells at the end of the floor, from index 4"));
			const double Fill0 = Sim->GetFloorO2Kg(-1);
			bool bRated = true;
			int32 Took = -1;
			for (int32 K = 1; K <= 40 && Took < 0; ++K) { Land(); bRated &= Sim->IsFloorRated(-1); if (!Open(3)) { Took = K; } }
			Expect(Took >= 20 && Took <= 21 && Sim->GetFloorCarvedCells(-1) == 8, FString::Printf(TEXT("e2 carved directly after a sol: %d cells on floor -1 (%d steps)"), Sim->GetFloorCarvedCells(-1), Took));
			int32 LivingCells = 0, Bunks = 0, Cells4to7 = 0;
			for (int32 Cell = 4; Cell < 8; ++Cell)
			{
				LivingCells += Sim->GetRoomAt(-1, Cell) == FName("LivingQuarters") ? 1 : 0;
				Bunks += CountRow(PiecesIn(FIntVector(-1, Cell, 0)), TEXT("BunkBed"));
				Cells4to7 += Sim->GetCellBeds(FIntVector(-1, Cell, 0)) == 4 ? 1 : 0;
			}
			Expect(LivingCells == 4 && Bunks == 8 && Cells4to7 == 4 && Sim->GetFloorBeds(-1) == Beds0 + 16 && Sim->GetHousingComfortCapacity() == Comfort0 + 16,
				FString::Printf(TEXT("e2 four Living cells, two bunks each (%d), four beds a cell: floor beds %d -> %d, comfortable room %d -> %d"), Bunks, Beds0, Sim->GetFloorBeds(-1), Comfort0, Sim->GetHousingComfortCapacity()));
			Expect(bRated && Sim->IsFloorRated(-1) && Sim->GetFloorO2Kg(-1) >= Fill0 + 400.0 - 60.0, FString::Printf(TEXT("e3 floor -1 stayed rated at every step through the carve; its air %.0f -> %.0f kg (the 400 kg of new volume pre-filled)"), Fill0, Sim->GetFloorO2Kg(-1)));
			Expect(Sim->GetCarveQueued(-1) == 0 && Near(Sim->GetStock(NHyd), Hyd0, 1e-9), TEXT("e3 no Borer, no hydrogen: nothing queued, the hydrogen stock untouched"));
			Expect(Sim->GetDistrictOfCell(-1, 7) == 3 && Sim->GetFloorCarvedCells(-1) == 8, TEXT("e3 the new cells belong to floor -1's district"));
		}

		// ---------------- (f) Civic ----------------
		{
			Expect(LoadBase(), TEXT("f base loaded"));
			OnlyDistricts({ 4 }); // floor -2: cells 0 and 1 free, 2 a Lab, 3 a Workstation
			SetHope(80.0);
			const FRHCityDemand Dm = Sim->GetCityDemand();
			double DiningGap = -1.0, InfGap = -1.0;
			for (const FRHServiceGap& G : Dm.ServiceGaps) { if (G.Function == FName("Dining")) { DiningGap = G.Gap; } if (G.Function == FName("Infirmary")) { InfGap = G.Gap; } }
			Expect(Near(DiningGap, 1.0, 1e-12) && Near(InfGap, 1.0, 1e-12), TEXT("f1 no dining room and no infirmary: both civic gaps are 1"));
			Expect(Sim->Debug_RunGrowthCheck() == 1 && Open(4) && Open(4)->Kind == ERHGrowthKind::CivicRoom, TEXT("f1 a Civic project starts"));
			const FRHGrowthProject F1 = *Open(4);
			Expect(F1.Room == FName("Dining") && F1.Level == -2 && F1.Index == 0 && !F1.bCarve && Near(F1.Bill.FindRef(NStruct), 80.0, 1e-9) && F1.Bill.Num() == 1 && Near(F1.TotalSols, 0.5, 1e-9),
				TEXT("f1 a tie at gap 1: the lexically first function wins - Dining, in the first free cell (0); a table and two chairs, Struct 80; half a sol"));
			const int32 Took = RunToEnd(4, 30);
			const TArray<FName> Dined = PiecesIn(FIntVector(-2, 0, 0));
			Expect(Took >= 10 && Took <= 11 && Sim->GetRoomAt(-2, 0) == FName("Dining") && Dined.Num() == 3 && CountRow(Dined, TEXT("DiningTable")) == 1 && CountRow(Dined, TEXT("Chair")) == 2,
				FString::Printf(TEXT("f2 after half a sol (%d steps) cell 0 is a Dining room with its table and two chairs (%d pieces)"), Took, Dined.Num()));
			// With the dining room standing, the infirmary is the worst gap - and a second sol later it starts.
			Sim->Debug_RunGrowthCheck();
			Expect(Open(4) && Open(4)->Room == FName("Infirmary") && Open(4)->Index == 1 && Near(Open(4)->Bill.FindRef(NStruct), 60.0, 1e-9), TEXT("f3 dining covered: the next civic project is the Infirmary, in cell 1, Struct 60 (locker, lamp, plant)"));
			RunToEnd(4, 30);
			const TArray<FName> Inf = PiecesIn(FIntVector(-2, 1, 0));
			Expect(Sim->GetRoomAt(-2, 1) == FName("Infirmary") && Inf.Num() == 3 && CountRow(Inf, TEXT("Locker")) == 1 && CountRow(Inf, TEXT("Lamp")) == 1 && CountRow(Inf, TEXT("Plant")) == 1, TEXT("f3 cell 1 is an Infirmary with its locker, lamp and plant"));
			Expect(Sim->Debug_RunGrowthCheck() == 0 && NoteOf(4).Contains(TEXT("covered")), FString::Printf(TEXT("f3 both civic services covered: no more projects - '%s'"), *NoteOf(4)));
			// The worst gap, not the first name: fifty people, a dining room (half cover), no infirmary.
			Expect(LoadBase(), TEXT("f base loaded"));
			OnlyDistricts({ 4 });
			SetHope(80.0);
			Sim->DesignateRoom(-2, 0, FName("Dining"), R);
			Sim->Debug_SetColonistsPerCell(10.0);
			Sim->Debug_AddColonists(42);
			Sim->Debug_StepPopulation(URHSimClockSubsystem::SubStepSeconds);
			const FRHCityDemand Fifty = Sim->GetCityDemand();
			double D50 = -1.0, I50 = -1.0;
			for (const FRHServiceGap& G : Fifty.ServiceGaps) { if (G.Function == FName("Dining")) { D50 = G.Gap; } if (G.Function == FName("Infirmary")) { I50 = G.Gap; } }
			Sim->Debug_RunGrowthCheck();
			Expect(Fifty.Population == 50 && Near(D50, 0.5, 1e-12) && Near(I50, 1.0, 1e-12) && Open(4) && Open(4)->Room == FName("Infirmary"),
				FString::Printf(TEXT("f4 fifty people, dining half covered (gap %.2f), no infirmary (gap %.2f): the WORST gap wins over the first name - %s"), D50, I50, Open(4) ? *Open(4)->Room.ToString() : TEXT("nothing")));
			// A room under way counts: with the Infirmary on its way, the next worst is Dining.
			Expect(LoadBase(), TEXT("f base loaded"));
			OnlyDistricts({ 2, 4 });
			SetHope(80.0);
			Sim->Debug_RunGrowthCheck();
			Expect(Open(2) && Open(2)->Room == FName("Dining") && !Open(4), TEXT("f5 two Civic districts, one start a sol: N2's #2 takes the Dining room (the cursor starts at 0)"));
			Sim->Debug_RunGrowthCheck();
			Expect(Open(4) && Open(4)->Room == FName("Infirmary"), TEXT("f5 the same sol's second check (a harness call): the Dining room under way counts as covered, so #4 builds the Infirmary, not a second Dining room"));
		}
		{
			// Adjacency, on a floor and on a surface network.
			Expect(LoadBase(), TEXT("f base loaded"));
			OnlyDistricts({ 2 });
			SetHope(80.0);
			Sim->DesignateBay(HabC, 0, FName("Cooking"), R); // N2 bay (-4,0): emits Odor; every other bay of N2 is within two
			Expect(Sim->Debug_RunGrowthCheck() == 0 && !Open(2) && NoteOf(2).Contains(TEXT("offend")), FString::Printf(TEXT("f6 a Cooking bay (Odor) in N2: Dining refuses Odor, every free bay is within two - no project, and none is built beside it even though an Infirmary would not mind - '%s'"), *NoteOf(2)));
			Sim->DesignateBay(HabC, 0, NAME_None, R);
			Expect(Sim->Debug_RunGrowthCheck() == 1 && Open(2) && Open(2)->Room == FName("Dining") && Open(2)->HabId == HabC && Open(2)->Index == 0, TEXT("f6 the Cooking bay cleared: the Dining room goes in N2's first free bay"));
			Expect(LoadBase(), TEXT("f base loaded"));
			OnlyDistricts({ 4 });
			SetHope(80.0);
			Sim->DesignateRoom(-2, 0, FName("Cooking"), R); // cell 0 (1,0): cell 1 (1,1) is next to it
			Expect(Sim->Debug_RunGrowthCheck() == 0 && NoteOf(4).Contains(TEXT("offend")), FString::Printf(TEXT("f7 a Cooking cell beside the only free cell on floor -2: no Dining room there - '%s'"), *NoteOf(4)));
			// The carve: no free cell on the civic floor.
			Expect(LoadBase(), TEXT("f base loaded"));
			OnlyDistricts({ 4 });
			SetHope(80.0);
			Sim->DesignateRoom(-2, 0, FName("Dining"), R);
			Sim->DesignateRoom(-2, 1, FName("Infirmary"), R);
			Sim->Debug_SetColonistsPerCell(10.0);
			Sim->Debug_AddColonists(42);
			Sim->Debug_StepPopulation(URHSimClockSubsystem::SubStepSeconds);
			Sim->Debug_RunGrowthCheck();
			const FRHGrowthProject* Carve = Open(4);
			Expect(Carve && Carve->bCarve && Carve->Index == INDEX_NONE && Carve->Room == FName("Dining") && Near(Carve->Bill.FindRef(NOx), 100.0, 1e-9) && Near(Carve->Bill.FindRef(NStruct), 80.0, 1e-9),
				TEXT("f8 every cell of the civic floor is in use and fifty people want a second dining room: it carves one - Struct 80 and 100 kg of air for the new cell"));
			const double FillBefore = Sim->GetFloorO2Kg(-2);
			bool bRated = true;
			Sim->Debug_SetGrowthEnabled(false); // this one project, and no second behind it
			for (int32 K = 0; K < 30 && Open(4); ++K) { Land(); bRated &= Sim->IsFloorRated(-2); }
			Sim->Debug_SetGrowthEnabled(true);
			const TArray<FName> New = PiecesIn(FIntVector(-2, 4, 0));
			Expect(Sim->GetFloorCarvedCells(-2) == 5 && Sim->GetRoomAt(-2, 4) == FName("Dining") && New.Num() == 3 && bRated && Sim->GetFloorO2Kg(-2) >= FillBefore + 100.0 - 20.0,
				FString::Printf(TEXT("f8 the floor has five cells, cell 4 is the new Dining room with its table and chairs, and floor -2 stayed rated (air %.0f -> %.0f)"), FillBefore, Sim->GetFloorO2Kg(-2)));
		}

		// ---------------- (g) One start a sol, round-robin, one alert a sol ----------------
		{
			Expect(LoadBase(), TEXT("g base loaded"));
			Sim->Debug_SetBigArrivalDone(false);
			SetHope(100.0);
			Sim->AddStock(NStruct, 15000.0);
			Sim->AddStock(NShield, 5000.0);
			Sim->AddStock(NOx, 3000.0);
			Steps(2); // the Big Arrival's streak starts: its 100 free beds are the standing shortfall
			Expect(Sim->GetCityDemand().bBigArrivalPending && Sim->GetCityDemand().HousingBedsShort > 50, FString::Printf(TEXT("g setup: the Big Arrival is pending, %d beds short"), Sim->GetCityDemand().HousingBedsShort));
			Alerts.Reset(); StartLog.Reset(); SeenOpen.Reset();
			bool bRated = true;
			const int32 Boundary0 = Sim->GetGrowthCheckedSol();
			for (int32 K = 0; K < 6 * 20 + 2; ++K) { Land(); bRated &= Sim->IsFloorRated(-1) && Sim->IsFloorRated(-2) && Sim->IsHabRated(HabA) && Sim->IsHabRated(HabC); }
			TArray<int32> Order; TSet<int32> SolsSeen; bool bOnePerSol = true;
			for (const FIntPoint& S : StartLog) { Order.Add(S.Y); bOnePerSol &= !SolsSeen.Contains(S.X); SolsSeen.Add(S.X); }
			FString OrderText;
			for (const int32 Id : Order) { OrderText += FString::Printf(TEXT("#%d "), Id); }
			Expect(StartLog.Num() >= 5 && Order[0] == 1 && Order[1] == 2 && Order[2] == 3 && Order[3] == 4 && Order[4] == 1 && bOnePerSol,
				FString::Printf(TEXT("g1 four districts eligible at the first boundary and ONE starts per sol, round-robin by Id from the cursor: %s(never two in a sol)"), *OrderText));
			bool bConsecutive = true;
			for (int32 i = 1; i < StartLog.Num(); ++i) { bConsecutive &= StartLog[i].X == StartLog[i - 1].X + 1; }
			Expect(bConsecutive && StartLog[0].X >= Boundary0, TEXT("g1 the starts fall on consecutive sol boundaries"));
			Expect(bRated && Sim->GetFloorCarvedCells(-1) >= 8 && Sim->GetHabNetworkBays(HabA) >= 8 && CountBuildings(TEXT("HabModule")) >= 5, FString::Printf(TEXT("g2 every home stayed rated at EVERY step through all the completions: floor -1 now %d cells, N1 %d bays"), Sim->GetFloorCarvedCells(-1), Sim->GetHabNetworkBays(HabA)));
			const int32 BoundariesCrossed = Sim->GetGrowthCheckedSol() - Boundary0;
			Expect(GrowthAlerts() >= SolsSeen.Num() && GrowthAlerts() <= BoundariesCrossed && GrowthAlerts() >= 5, FString::Printf(TEXT("g3 one growth alert per sol colony-wide, however many projects start or finish: %d alerts over %d sol boundaries (%d sols had a start)"), GrowthAlerts(), BoundariesCrossed, SolsSeen.Num()));
			bool bBoth = false;
			for (const FString& A : Alerts) { bBoth |= A.StartsWith(TEXT("CITY GROWTH")) && A.Contains(TEXT("under way")) && A.Contains(TEXT("finished")); }
			Expect(bBoth, TEXT("g3 starts and finishes are summarised in the same banner when they fall in the same sol"));
			Sim->Debug_SetBigArrivalDone(true);
		}

		// ---------------- (h) A changed site refunds; an erase cancels; a saved project finishes with its twin ----------------
		{
			// Surface: the site is taken before the countdown ends.
			Expect(LoadBase(), TEXT("h base loaded"));
			OnlyDistricts({ 1 });
			SetHope(80.0);
			const double S0 = Held(NStruct), H0 = Held(NShield);
			Sim->Debug_RunGrowthCheck();
			Sim->Debug_SetGrowthEnabled(false); // the open project runs on; no new one starts
			Expect(Open(1) && Near(Held(NStruct), S0 - 1440.0, 1e-6), TEXT("h1 a surface project is open and has taken its bill"));
			Place(TEXT("SolarArray"), 3500, 1500); // a machine on the site
			Alerts.Reset();
			const int32 Habs0 = CountBuildings(TEXT("HabModule"));
			double OxStep = 0.0;
			for (int32 K = 0; K < 40 && Open(1); ++K) { const double Before = Held(NOx); Land(); OxStep = Held(NOx) - Before; }
			Expect(!Open(1) && CountBuildings(TEXT("HabModule")) == Habs0 && Near(Held(NStruct), S0, 1e-6) && Near(Held(NShield), H0, 1e-6) && OxStep > 300.0,
				FString::Printf(TEXT("h1 the site was taken: nothing is built and the whole bill is back (Struct %.0f of %.0f, Shielding %.0f of %.0f; the air rose %.0f kg in the step it came back)"), Held(NStruct), S0, Held(NShield), H0, OxStep));
			bool bNotBuilt = false;
			for (const FString& A : Alerts) { bNotBuilt |= A.StartsWith(TEXT("CITY GROWTH")) && A.Contains(TEXT("not built")); }
			Sim->Debug_SetGrowthEnabled(true);
			Steps(25); // reach the next sol boundary so the one alert goes out
			for (const FString& A : Alerts) { bNotBuilt |= A.StartsWith(TEXT("CITY GROWTH")) && A.Contains(TEXT("not built")); }
			Expect(bNotBuilt, TEXT("h1 the alert says it was not built and the materials are back"));
			// Civic: the cell is zoned by someone else meanwhile.
			Expect(LoadBase(), TEXT("h base loaded"));
			OnlyDistricts({ 4 });
			SetHope(80.0);
			const double SC = Held(NStruct);
			Sim->Debug_RunGrowthCheck();
			Sim->Debug_SetGrowthEnabled(false);
			Expect(Open(4) && Open(4)->Index == 0 && Near(Held(NStruct), SC - 80.0, 1e-6), TEXT("h2 a Dining project on floor -2 cell 0 is open"));
			Sim->DesignateRoom(-2, 0, FName("Lab"), R);
			RunToEnd(4, 30);
			Expect(Sim->GetRoomAt(-2, 0) == FName("Lab") && PiecesIn(FIntVector(-2, 0, 0)).Num() == 0 && Near(Held(NStruct), SC, 1e-6), TEXT("h2 the cell was zoned meanwhile: the Dining room is not built over it, nothing was furnished, the Struct is back"));
			// Underground: the floor loses its rating before the end.
			Expect(LoadBase(), TEXT("h base loaded"));
			OnlyDistricts({ 3 });
			SetHope(80.0);
			const double SF = Held(NStruct);
			Sim->Debug_RunGrowthCheck();
			Sim->Debug_SetGrowthEnabled(false);
			Expect(Open(3), TEXT("h3 a floor project is open"));
			const int32 CellsF = Sim->GetFloorCarvedCells(-1);
			double OxBack = 0.0;
			for (int32 K = 0; K < 40 && Open(3); ++K)
			{
				Sim->Debug_SetFloorO2Kg(-1, 0.0); // empty and unrated each step: it cannot re-rate before the project ends
				Sim->Debug_UnrateFloor(-1);
				const double Before = Held(NOx);
				Land();
				OxBack = Held(NOx) - Before;
			}
			Expect(Sim->GetFloorCarvedCells(-1) == CellsF && Near(Held(NStruct), SF, 1e-6) && OxBack > 300.0, FString::Printf(TEXT("h3 floor -1 lost its rating: nothing carved, the Struct is back (%.0f of %.0f) and the air rose %.0f kg in the step it came back (the refunded 400)"), Held(NStruct), SF, OxBack));
			// Erase: a full refund, nothing applied.
			Expect(LoadBase(), TEXT("h base loaded"));
			OnlyDistricts({ 1, 3 });
			SetHope(100.0);
			Sim->Debug_SetBigArrivalDone(false);
			Steps(2);
			const double SE = Held(NStruct), HE = Held(NShield);
			Sim->Debug_RunGrowthCheck();
			Expect(Open(1), TEXT("h4 a surface project is open"));
			const int32 HabsE = CountBuildings(TEXT("HabModule"));
			Expect(Near(Held(NStruct), SE - 1440.0, 1e-6) && Send(EraseCmd(1)) && !Open(1) && Sim->FindDistrict(1) == nullptr && Sim->GetGrowthProjects().Num() == 0,
				TEXT("h4 the district is erased: its open project is cancelled"));
			Expect(Near(Held(NStruct), SE, 1e-6) && Near(Held(NShield), HE, 1e-6) && CountBuildings(TEXT("HabModule")) == HabsE, TEXT("h4 the refund is the full bill (Struct and Shielding exactly), and nothing was built"));
			Sim->Debug_SetBigArrivalDone(true);
		}
		{
			// The twin: a project saved halfway finishes on the SAME step as one that never saved. Two kinds, one after the other.
			for (const int32 District : { 3, 1 })
			{
				Expect(LoadBase(), TEXT("h5 base loaded"));
				OnlyDistricts({ District });
				SetHope(80.0);
				Sim->Debug_RunGrowthCheck();
				Steps(District == 3 ? 10 : 13);
				Expect(Open(District), FString::Printf(TEXT("h5 district #%d: its project is halfway"), District));
				const FRHGrowthProject Mid = *Open(District);
				Expect(Sim->SaveColony(TEXT("citygrowth_twin"), Err), FString::Printf(TEXT("h5 saved mid-project (%s)"), *Err));
				const int32 TwinSteps = RunToEnd(District, 60);
				const FLedger Twin = Ledger();
				const double TwinHope = Sim->GetHopeSmoothed();
				const int32 TwinSol = Sim->GetGrowthCheckedSol(), TwinCursor = Sim->GetGrowthCursor();
				Expect(Sim->LoadColony(TEXT("citygrowth_twin"), Err) && Err.IsEmpty() && Open(District), FString::Printf(TEXT("h5 loaded (%s)"), *Err));
				const FRHGrowthProject Back = *Open(District);
				bool bSame = Back.DistrictId == Mid.DistrictId && Back.Kind == Mid.Kind && Back.Level == Mid.Level && Back.Cell == Mid.Cell && Back.Size == Mid.Size && Back.HabId == Mid.HabId && Back.Cells == Mid.Cells
					&& Back.FirstCell == Mid.FirstCell && Back.Index == Mid.Index && Back.bCarve == Mid.bCarve && Back.Room == Mid.Room && Back.TotalSols == Mid.TotalSols && Back.RemainingSols == Mid.RemainingSols && Back.Bill.Num() == Mid.Bill.Num();
				for (const TPair<FName, double>& L : Mid.Bill) { bSame &= Back.Bill.FindRef(L.Key) == L.Value; }
				Expect(bSame && Sim->GetGrowthCursor() == District, FString::Printf(TEXT("h5 the loaded project is the saved one, field for field (remaining %.4f sol, cursor %d)"), Back.RemainingSols, Sim->GetGrowthCursor()));
				const int32 LoadedSteps = RunToEnd(District, 60);
				const FLedger Loaded = Ledger();
				Expect(LoadedSteps == TwinSteps && LoadedSteps > 0 && Loaded.Cells1 == Twin.Cells1 && Loaded.Habs == Twin.Habs && Loaded.Pieces == Twin.Pieces && Loaded.Beds == Twin.Beds && Near(Loaded.Struct, Twin.Struct, 1e-6)
					&& Near(Loaded.Oxygen, Twin.Oxygen, 1e-6) && Near(Loaded.Shielding, Twin.Shielding, 1e-6) && Near(Sim->GetHopeSmoothed(), TwinHope, 1e-9) && Sim->GetGrowthCheckedSol() == TwinSol && Sim->GetGrowthCursor() == TwinCursor,
					FString::Printf(TEXT("h5 district #%d: the loaded colony finishes on step %d, the twin that never saved on step %d - and the ledgers match (cells %d, habs %d, pieces %d, Struct %.1f)"), District, LoadedSteps, TwinSteps, Loaded.Cells1, Loaded.Habs, Loaded.Pieces, Loaded.Struct));
			}
		}

		// ---------------- (i) Ten sols: the era band and the agent band run the same code ----------------
		{
			Expect(LoadBase(), TEXT("i base loaded"));
			Sim->Debug_SetBigArrivalDone(false);
			SetHope(100.0);
			Sim->AddStock(NStruct, 15000.0);
			Sim->AddStock(NShield, 5000.0);
			Sim->AddStock(NOx, 3000.0);
			Steps(2);
			Sim->Debug_SetBigArrivalDone(false);
			Expect(Sim->SaveColony(TEXT("citygrowth_parity"), Err), FString::Printf(TEXT("i the parity start is saved (%s)"), *Err));
			// Era band: 200 steps of 60 s.
			int32 EraStarted = 0;
			TSet<int32> OpenEra;
			const FLedger E0 = Ledger();
			const auto CountStarts = [&](TSet<int32>& Was, int32& Started)
			{
				for (const FRHGrowthProject& P : Sim->GetGrowthProjects()) { if (!Was.Contains(P.DistrictId)) { ++Started; } }
				Was.Empty();
				for (const FRHGrowthProject& P : Sim->GetGrowthProjects()) { Was.Add(P.DistrictId); }
			};
			for (int32 K = 0; K < 200; ++K) { Land(); CountStarts(OpenEra, EraStarted); }
			const FLedger E1 = Ledger();
			const double EraSeconds = EraDt;
			// Agent band: the same ten sols in 0.1 s sub-steps (120,000 of them).
			Expect(Sim->LoadColony(TEXT("citygrowth_parity"), Err) && Err.IsEmpty(), TEXT("i the parity start is loaded for the agent band"));
			Sim->Debug_SetGrowthEnabled(true);
			Sim->Debug_SetBigArrivalDone(false);
			int32 AgentStarted = 0;
			TSet<int32> OpenAgent;
			const FLedger A0 = Ledger();
			const int32 AgentSteps = FMath::RoundToInt32(10.0 * SolLen / AgentDt);
			for (int32 K = 0; K < AgentSteps; ++K)
			{
				Clock->Debug_AdvanceSimSeconds(AgentDt);
				Sim->Debug_SetBatteryWh(1.0e9);
				Sim->Debug_SetHopeSmoothed(100.0);
				Sim->Debug_StepSim(URHSimClockSubsystem::SubStepSeconds);
				CountStarts(OpenAgent, AgentStarted);
			}
			const FLedger A1 = Ledger();
			const auto Within = [](double A, double B, double Frac) { return FMath::Abs(A - B) <= Frac * FMath::Max(1.0, FMath::Max(FMath::Abs(A), FMath::Abs(B))); };
			UE_LOG(LogRedHopeSim, Display, TEXT("CITYGROWTH ledger over 10 sols - era: %d started, Struct %.1f -> %.1f, Shielding %.1f -> %.1f, Oxygen %.1f -> %.1f, floor -1 %d -> %d cells, %d modules, %d pieces, %d beds | agent: %d started, Struct %.1f -> %.1f, Shielding %.1f -> %.1f, Oxygen %.1f -> %.1f, floor -1 %d -> %d cells, %d modules, %d pieces, %d beds (%d sub-steps of %.1f s against %d steps of %.0f s)"),
				EraStarted, E0.Struct, E1.Struct, E0.Shielding, E1.Shielding, E0.Oxygen, E1.Oxygen, E0.Cells1, E1.Cells1, E1.Habs, E1.Pieces, E1.Beds,
				AgentStarted, A0.Struct, A1.Struct, A0.Shielding, A1.Shielding, A0.Oxygen, A1.Oxygen, A0.Cells1, A1.Cells1, A1.Habs, A1.Pieces, A1.Beds, AgentSteps, AgentDt, 200, EraSeconds);
			Expect(EraStarted >= 8 && AgentStarted == EraStarted && A1.Habs == E1.Habs && A1.Cells1 == E1.Cells1 && A1.Cells2 == E1.Cells2 && A1.Pieces == E1.Pieces && A1.Beds == E1.Beds && A1.Free == E1.Free,
				FString::Printf(TEXT("i1 ten sols: the era band started %d projects and the agent band %d; the same modules (%d), cells (%d), furnishings (%d) and beds (%d)"), EraStarted, AgentStarted, E1.Habs, E1.Cells1, E1.Pieces, E1.Beds));
			Expect(Within(A1.Struct, E1.Struct, 0.05) && Within(A1.Shielding, E1.Shielding, 0.05) && Within(A1.Oxygen, E1.Oxygen, 0.05) && Within(A1.Food, E1.Food, 0.05),
				FString::Printf(TEXT("i2 the ledgers agree within 5%%: Struct %.1f vs %.1f, Shielding %.1f vs %.1f, Oxygen %.1f vs %.1f, Food %.1f vs %.1f (the same code: expect identical)"), E1.Struct, A1.Struct, E1.Shielding, A1.Shielding, E1.Oxygen, A1.Oxygen, E1.Food, A1.Food));
			Sim->Debug_SetBigArrivalDone(true);
		}

		// ---------------- (f9) A CivicService that emits Odor is never zoned (the last scenario: it injects a row) ----------------
		{
			// A CivicService that emits Odor is never zoned: it would win the lexical tie ("Canteen" < "Dining").
			Expect(LoadBase(), TEXT("f base loaded"));
			OnlyDistricts({ 4 });
			SetHope(80.0);
			FRHRoomRow Canteen;
			Canteen.DisplayName = TEXT("Canteen");
			Canteen.Function = FName("Canteen");
			Canteen.EmitsTags = TEXT("Odor");
			Canteen.MoraleWeight = 0.6f;
			Canteen.CivicService = true;
			Canteen.CivicFurniture = TEXT("DiningTable");
			Canteen.SliceActive = true;
			DefsSub->Debug_InjectRoom(FName("Canteen"), Canteen);
			bool bSeen = false;
			for (const FRHServiceGap& G : Sim->GetCityDemand().ServiceGaps) { bSeen |= G.Function == FName("Canteen") && Near(G.Gap, 1.0, 1e-12); }
			Sim->Debug_RunGrowthCheck();
			Expect(bSeen && Open(4) && Open(4)->Room == FName("Dining"), TEXT("f9 a CivicService row that emits Odor ('Canteen', gap 1, first by name) is in the demand and never zoned: the city builds the Dining room"));
		}

		Sim->OnCommandRejected.Remove(RejectH);
		Sim->OnAlert.Remove(AlertH);
		UE_LOG(LogRedHopeSim, Display, TEXT("=== CITY GROWTH TEST END: %d check(s), %d failure(s) ==="), Checks, Fails);
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return Fails > 0 ? 1 : 0;
	}

	// C1 self-test (living-city gate 1, 2026-10-18): THE SCALE SUITE. Stands up
	// a big colony (a shaft to -5, ~1,000 carved cells zoned and furnished with
	// bunks and benches, air and power held up each step, gardens on every
	// floor), then grows the population 250 -> 1,000 -> 3,000 -> 5,000 and, at
	// each size, times the agent-band sub-step (0.1 s), the era step (60 s),
	// GetColonyHope (the deck reads it every frame) and the Debug_AddColonists
	// call that got there, against budgets. Correctness at scale is asserted
	// too: the population is exactly N, every Id resolves, everyone is housed,
	// supported and in a bed with room, and the seats fill. Then the food runs
	// half short (C2a: everyone gets half a ration, each home recalls its newest
	// residents on its clock, and the roster shifts under every lookup) and a
	// save/load round trip must restore every assignment. A DIGEST line per
	// size hashes every derived value bit-for-bit, so a refactor that must not
	// change behaviour is proven by diffing two runs of this suite. Timings are
	// wall clock on a shared machine: budgets are generous multiples. `-scale`.
	if (FParse::Param(*Params, TEXT("scale")))
	{
		UE_LOG(LogRedHopeSim, Display, TEXT("=== SCALE TEST (C1, 2026-10-18) ==="));
		int32 Fails = 0;
		const auto Expect = [&](bool bOk, const FString& What)
		{
			if (bOk) { UE_LOG(LogRedHopeSim, Display, TEXT("SCALE ok: %s"), *What); }
			else { UE_LOG(LogRedHopeSim, Error, TEXT("SCALE FAIL: %s"), *What); ++Fails; }
		};
		const auto Near = [](double A, double B, double Tol) { return FMath::Abs(A - B) <= Tol; };
		constexpr double EraDt = (double)URHSimClockSubsystem::EraStepSimSeconds;
		constexpr double AgentDt = (double)URHSimClockSubsystem::SubStepSeconds;
		constexpr double SolLen = (double)URHSimClockSubsystem::SolLengthSimSeconds;
		constexpr int32 Floors = 5;
		constexpr int32 CellsPerFloor = 210;
		constexpr int32 GardenCells = 10;      // the tail of each floor's spiral
		constexpr int32 AgentReps = 200;
		constexpr int32 EraReps = 20;
		constexpr int32 HopeReps = 50;
		constexpr double CapSeconds = 5.0;     // one step this slow stops the growth
		const int32 Targets[] = { 250, 1000, 3000, 5000 };

		const auto TopUp = [&](double Dt)
		{
			Sim->Debug_AddFreshWater(500.0 * Dt / SolLen);
			Sim->Debug_SetBatteryWh(1.0e9);
		};
		const auto EraStepOnce = [&]()
		{
			TopUp(EraDt);
			Clock->Debug_AdvanceSimSeconds(EraDt);
			Sim->EraStep((float)EraDt);
		};
		const auto AgentStepOnce = [&]()
		{
			TopUp(AgentDt);
			Clock->Debug_AdvanceSimSeconds(AgentDt);
			Sim->Debug_StepSim(URHSimClockSubsystem::SubStepSeconds);
		};
		URHDefinitionsSubsystem* DefsSub = World->GetSubsystem<URHDefinitionsSubsystem>();
		// C3: the colony itself is built by one sim debug function now (so the game's
		// RH.CityBench stands up the very same city); the checks on it stay here.
		const FName NFood(TEXT("Food")), NWater(TEXT("Water")), NOxygen(TEXT("Oxygen")), NSoil(TEXT("Soil")), NSeeds(TEXT("Seeds"));
		const URHSimWorldSubsystem::FRHScaleColonyReport Built = Sim->Debug_BuildScaleColony(0);
		for (const FString& E : Built.Errors) { UE_LOG(LogRedHopeSim, Error, TEXT("SCALE: %s"), *E); ++Fails; }
		Expect(Built.bSetupOk,
			FString::Printf(TEXT("setup: shaft to -%d, %d cells a floor, every floor rated"), Floors, CellsPerFloor));
		Expect(Built.ZonedLiving > 600 && Built.ZonedGarden == Floors * GardenCells,
			FString::Printf(TEXT("zoning: %d Living, %d work/lab/garden cells, %d Garden, %d other"), Built.ZonedLiving, Built.ZonedSeat, Built.ZonedGarden, Built.ZonedOther));
		const int32 Capacity = Built.Capacity;
		Expect(Built.Rejected == 0 && Sim->GetFurniture().Num() > 2500 && Capacity >= 5000 && Sim->GetSeatPriority().Num() == Built.StationOrders,
			FString::Printf(TEXT("furnishing: %d pieces placed, %d refused, %d certified beds (need 5,000), %d stations assigned first"),
				Sim->GetFurniture().Num(), Built.Rejected, Capacity, Sim->GetSeatPriority().Num()));
		const int32 SeatsTotal = Built.SeatsTotal;
		const TArray<FIntPoint>& WorkCells = Built.WorkCells;
		UE_LOG(LogRedHopeSim, Display, TEXT("SCALE layout: %d cells, %d pieces, %d beds, %d work seats"), Floors * CellsPerFloor, Sim->GetFurniture().Num(), Capacity, SeatsTotal);

		// Digests: every derived value that must survive a refactor, hashed at full
		// precision (raw double bits) and in the colonist array's own order.
		// Assignments is the part a save/load must restore exactly (the per-step
		// readouts - comforts, producing beds - are not saved, so they stay out).
		const auto MixBytes = [](uint32& H, const void* P, int32 Bytes) { H = FCrc::MemCrc32(P, Bytes, H); };
		const auto MixD = [&MixBytes](uint32& H, double V) { MixBytes(H, &V, (int32)sizeof(V)); };
		const auto MixI = [&MixBytes](uint32& H, int32 V) { MixBytes(H, &V, (int32)sizeof(V)); };
		const auto Assignments = [&]() -> uint32
		{
			uint32 H = 0;
			const URHSimWorldSubsystem::FRHHopeBreakdown Hp = Sim->GetColonyHope();
			for (const double V : { Hp.Housing, Hp.Rooms, Hp.Jobs, Hp.Furnishings, Hp.Views, Hp.AdjacencyPenalty }) { MixD(H, V); }
			MixI(H, Hp.OffendedPairs); MixI(H, Hp.FilledSeats); MixI(H, Hp.OffendedRefusers); MixI(H, Hp.RefuserCells); // C2a: the shares behind the per-head terms
			MixI(H, Sim->GetStaffedSeats()); MixD(H, Sim->GetLabYieldSeatSum()); MixD(H, Sim->GetWorkstationYieldSeatSum());
			MixI(H, Sim->GetHousingCapacity()); MixI(H, Sim->GetFreeHousing()); MixI(H, Sim->GetPopulation());
			for (int32 L = 1; L <= Floors; ++L)
			{
				MixI(H, Sim->GetFloorBeds(-L));
				for (int32 I = 0; I < CellsPerFloor; ++I)
				{
					const FIntVector K(-L, I, 0);
					int32 Filled = 0, Cap = 0;
					Sim->GetStationSeats(K, Filled, Cap);
					MixI(H, Filled); MixI(H, Cap); MixI(H, Sim->GetCellBeds(K)); MixI(H, Sim->GetCellSeats(K));
				}
			}
			for (const FIntVector& K : Sim->GetSeatPriority()) { MixI(H, K.X); MixI(H, K.Y); MixI(H, K.Z); }
			for (const FRHColonist& C : Sim->GetColonists())
			{
				MixI(H, C.Id); MixI(H, C.HomeLevel); MixI(H, C.HomeHab);
				MixI(H, (int32)FCrc::StrCrc32(*Sim->GetColonistJob(C.Id).ToString())); MixI(H, Sim->GetColonistBedPiece(C.Id)); MixI(H, Sim->GetColonistSeatPiece(C.Id));
				FIntVector St = FIntVector::ZeroValue;
				const bool bSt = Sim->GetColonistStation(C.Id, St);
				MixI(H, bSt ? 1 : 0); MixI(H, St.X); MixI(H, St.Y); MixI(H, St.Z);
			}
			return H;
		};
		const auto Digest = [&]() -> uint32
		{
			uint32 H = Assignments();
			const URHSimWorldSubsystem::FRHHopeBreakdown Hp = Sim->GetColonyHope();
			for (const double V : { Hp.Base, Hp.Milestones, Hp.Comforts, Hp.UnsupportedPenalty, Hp.WaterPenalty, Hp.FoodPenalty, Hp.CrisisPenalty, Hp.Solidarity, Hp.Total }) { MixD(H, V); }
			MixD(H, Sim->GetHopeSmoothed()); MixI(H, (int32)Sim->GetHopeBand()); MixD(H, Sim->GetDeficitEquivalent()); // C2a
			MixD(H, Sim->GetCrewSkillMul(FName("Lab"))); MixD(H, Sim->GetCrewSkillMul(FName("Garden"))); MixD(H, Sim->GetCrewSkillMul(FName("Workstation")));
			for (const FName& Res : { NFood, NWater, NOxygen, NSoil, NSeeds }) { MixD(H, Sim->GetStock(Res)); }
			MixD(H, Sim->GetWaterPotability()); MixD(H, Sim->GetGardenFoodCumKg()); MixD(H, Sim->GetPower().BatteryWh);
			MixI(H, Sim->GetPlantedCellCount()); MixI(H, Sim->GetProducingCellCount()); MixI(H, Sim->GetComfortsSuppliedCount()); MixI(H, Sim->GetBirthsOnMars());
			MixI(H, Sim->GetDiscoveryLog().Num()); MixD(H, Sim->GetDiscoveryProgress());
			for (int32 L = 1; L <= Floors; ++L) { MixD(H, Sim->GetFloorO2Kg(-L)); MixI(H, Sim->IsFloorRated(-L) ? 1 : 0); }
			for (const FRHColonist& C : Sim->GetColonists())
			{
				MixI(H, C.bSupported ? 1 : 0); MixD(H, C.UnsupportedSimSeconds); MixI(H, C.bMarsBorn ? 1 : 0); // C2a
				TArray<TPair<FString, double>> Skills;
				for (const TPair<FName, double>& S : C.SkillSols) { Skills.Emplace(S.Key.ToString(), S.Value); }
				Skills.Sort([](const TPair<FString, double>& A, const TPair<FString, double>& B) { return A.Key < B.Key; });
				for (const TPair<FString, double>& S : Skills) { MixI(H, (int32)FCrc::StrCrc32(*S.Key)); MixD(H, S.Value); }
			}
			return H;
		};

		// Correctness at scale.
		const auto VerifyAt = [&](const FString& Tag)
		{
			const TArray<FRHColonist>& Cs = Sim->GetColonists();
			const int32 N = Cs.Num();
			TSet<int32> Ids;
			TMap<int32, int32> PerFloor, PerPiece;
			bool bSupported = true, bHomed = true, bStations = true, bIndex = true;
			int32 Employed = 0, Idle = 0, MaxId = 0;
			for (const FRHColonist& C : Cs)
			{
				Ids.Add(C.Id);
				MaxId = FMath::Max(MaxId, C.Id);
				bIndex &= Sim->FindColonist(C.Id) == &C;
				bSupported &= C.bSupported;
				bHomed &= C.HomeHab == 0 && C.HomeLevel < 0 && C.HomeLevel >= -Floors && Sim->IsFloorRated(C.HomeLevel);
				++PerFloor.FindOrAdd(C.HomeLevel);
				const int32 Piece = Sim->GetColonistBedPiece(C.Id);
				if (Piece != 0) { ++PerPiece.FindOrAdd(Piece); }
				FIntVector St;
				const bool bHasStation = Sim->GetColonistStation(C.Id, St);
				const bool bHasJob = !Sim->GetColonistJob(C.Id).IsNone();
				bStations &= bHasStation == bHasJob;
				Employed += bHasJob ? 1 : 0;
				Idle += bHasJob ? 0 : 1;
			}
			bool bBedsFit = true, bPiecesOk = true;
			for (const TPair<int32, int32>& P : PerFloor) { bBedsFit &= P.Value <= Sim->GetFloorBeds(P.Key); }
			for (const TPair<int32, int32>& P : PerPiece)
			{
				const FRHFurnitureInstance* F = Sim->FindFurniture(P.Key);
				const FRHFurnitureRow* Piece = F ? DefsSub->GetFurniture(F->Row) : nullptr;
				bPiecesOk &= Piece && Piece->Provides == FName("Bed") && P.Value <= Piece->Capacity;
			}
			Expect(Sim->GetPopulation() == N && Ids.Num() == N && N > 0, FString::Printf(TEXT("%s: population %d, every Id distinct"), *Tag, N));
			Expect(bIndex && Sim->FindColonist(0) == nullptr && Sim->FindColonist(MaxId + 1) == nullptr, FString::Printf(TEXT("%s: every Id resolves to its own colonist, a stranger to none"), *Tag));
			Expect(bHomed && bBedsFit && Sim->GetFreeHousing() == Sim->GetHousingCapacity() - N, FString::Printf(TEXT("%s: everyone housed on a rated floor, no floor over its beds, %d beds free"), *Tag, Sim->GetFreeHousing()));
			Expect(bSupported, FString::Printf(TEXT("%s: everyone supported (air, food, water)"), *Tag));
			Expect(bPiecesOk, FString::Printf(TEXT("%s: every bed piece holds no more than its capacity (%d colonists share %d pieces)"), *Tag, N, PerPiece.Num()));
			Expect(bStations && Employed == FMath::Min(N, SeatsTotal) && Sim->GetColonyHope().FilledSeats == Employed,
				FString::Printf(TEXT("%s: %d at work posts (min of N and %d seats), %d idle, every worker has a station"), *Tag, Employed, SeatsTotal, Idle));
			// C2a: Hope per head. Jobs is the employment rate (7.5 x seats / people, capped
			// at 7.5), adjacency a share of the refuser cells, and the index falls with
			// the city's size instead of sitting at the 100 the uncapped jobs line used
			// to pin it at.
			const URHSimWorldSubsystem::FRHHopeBreakdown Hp = Sim->GetColonyHope();
			Expect(Near(Hp.Jobs, Sim->GetHopeJobsMax() * FMath::Min(1.0, (double)Hp.FilledSeats / N), 1e-9) && Hp.Total > 40.0 && Hp.Total < 99.0
				&& Hp.RefuserCells > 0 && Hp.OffendedRefusers > 0 && Near(Hp.AdjacencyPenalty, Sim->GetHopeAdjacencyMax() * Hp.OffendedRefusers / Hp.RefuserCells, 1e-9),
				FString::Printf(TEXT("%s: Hope per head %.2f = 50 + milestones %.2f + housing %.2f + rooms %.2f + jobs %.2f (%d seats for %d) + furnishings %.2f - adjacency %.2f (%d of %d refuser cells) - unsupported %.2f"),
					*Tag, Hp.Total, Hp.Milestones, Hp.Housing, Hp.Rooms, Hp.Jobs, Hp.FilledSeats, N, Hp.Furnishings, Hp.AdjacencyPenalty, Hp.OffendedRefusers, Hp.RefuserCells, Hp.UnsupportedPenalty));
		};

		// The growth ladder.
		struct FScaleLine { int32 N = 0; double AgentMs = 0.0, EraMs = 0.0, HopeMs = 0.0, AddMs = 0.0; uint32 Digest = 0; };
		TArray<FScaleLine> Table;
		bool bCapped = false;
		for (const int32 Target : Targets)
		{
			if (bCapped) { break; }
			FScaleLine Line;
			const int32 Want = Target - Sim->GetPopulation();
			const double A0 = FPlatformTime::Seconds();
			const int32 Housed = Sim->Debug_AddColonists(Want);
			Line.AddMs = (FPlatformTime::Seconds() - A0) * 1000.0;
			Line.N = Sim->GetPopulation();
			Expect(Housed == Want, FString::Printf(TEXT("N=%d: Debug_AddColonists(%d) housed %d"), Target, Want, Housed));
			if (Table.Num() == 1)
			{
				// Five sols with a thousand at work. C2a: Hope is per head now, so a
				// city of thousands on this layout sits near 70 and no longer climbs
				// past the research line (85) or the growth line (75) by itself (the
				// uncapped jobs line used to pin it at 100). The two gates were lowered
				// to 50 at the start so the research and growth paths still run in every
				// measured step. (No birth: that needs 20 sols.)
				for (int32 K = 0; K < 5 * (int32)(SolLen / EraDt); ++K) { EraStepOnce(); }
			}
			// Settle: the first step after a change rebuilds whatever is derived.
			EraStepOnce();
			for (int32 K = 0; K < 3; ++K) { AgentStepOnce(); }

			double AgentTotal = 0.0, EraTotal = 0.0, HopeTotal = 0.0;
			int32 AgentDone = 0, EraDone = 0;
			for (; AgentDone < AgentReps && !bCapped;)
			{
				TopUp(AgentDt);
				Clock->Debug_AdvanceSimSeconds(AgentDt);
				const double S0 = FPlatformTime::Seconds();
				Sim->Debug_StepSim(URHSimClockSubsystem::SubStepSeconds);
				const double Took = FPlatformTime::Seconds() - S0;
				AgentTotal += Took; ++AgentDone;
				bCapped |= Took > CapSeconds;
			}
			for (; EraDone < EraReps && !bCapped;)
			{
				TopUp(EraDt);
				Clock->Debug_AdvanceSimSeconds(EraDt);
				const double S0 = FPlatformTime::Seconds();
				Sim->EraStep((float)EraDt);
				const double Took = FPlatformTime::Seconds() - S0;
				EraTotal += Took; ++EraDone;
				bCapped |= Took > CapSeconds;
			}
			double HopeSink = 0.0;
			const double H0 = FPlatformTime::Seconds();
			for (int32 K = 0; K < HopeReps; ++K) { HopeSink += Sim->GetColonyHope().Total; }
			HopeTotal = FPlatformTime::Seconds() - H0;
			Line.AgentMs = AgentDone > 0 ? AgentTotal * 1000.0 / AgentDone : 0.0;
			Line.EraMs = EraDone > 0 ? EraTotal * 1000.0 / EraDone : 0.0;
			Line.HopeMs = HopeTotal * 1000.0 / HopeReps;
			Line.Digest = Digest();
			Table.Add(Line);
			UE_LOG(LogRedHopeSim, Display, TEXT("SCALE N=%5d | agent %9.3f ms/step (%d steps) | era %9.3f ms/step (%d steps) | hope %9.3f ms/read | add %10.1f ms (%d colonists)%s"),
				Line.N, Line.AgentMs, AgentDone, Line.EraMs, EraDone, Line.HopeMs, Line.AddMs, Housed, bCapped ? TEXT(" | CAPPED: a step took over 5 s, growth stops here") : TEXT(""));
			UE_LOG(LogRedHopeSim, Display, TEXT("SCALE digest N=%d: %08x (hope sink %.6f, smoothed %.3f, %d discoveries)"),
				Line.N, Line.Digest, HopeSink, Sim->GetHopeSmoothed(), Sim->GetDiscoveryLog().Num());
			VerifyAt(FString::Printf(TEXT("N=%d"), Line.N));
		}
		Expect(!bCapped && Table.Num() == UE_ARRAY_COUNT(Targets), TEXT("every size ran: no step took over 5 s"));
		Expect(Sim->GetBirthsOnMars() == 0 && Sim->GetPopulation() == (Table.Num() ? Table.Last().N : -1), TEXT("no births muddied the headcount (growth needs 20 sols; the run is about a dozen)"));

		// Recall at scale (C2a). The colony's food runs half short for two sols
		// (40 era steps): every resident of every home gets half a ration - not
		// "the newest half eats" - so every home's clock runs, and each home
		// recalls its newest residents first. The law is linear: a home recalls its
		// size at onset x (1 - 0.5) x sols / EvacSols (the old rule recalled the unfed
		// half, all at once, after two sols). Every recall shifts the roster under
		// every lookup. (The layout has Infirmary cells, so the clock runs on 3 sols,
		// not 2: in two sols the colony loses 0.5 x 2 / 3, a third, and keeps 0.667.)
		if (!bCapped)
		{
			const int32 Before = Sim->GetPopulation();
			TMap<int32, int32> FloorOf;
			FloorOf.Reserve(Before);
			for (const FRHColonist& C : Sim->GetColonists()) { FloorOf.Add(C.Id, C.HomeLevel); }
			int32 MinGone[Floors + 1], MaxKept[Floors + 1];
			for (int32 L = 0; L <= Floors; ++L) { MinGone[L] = TNumericLimits<int32>::Max(); MaxKept[L] = 0; }
			for (int32 K = 0; K < 40; ++K)
			{
				Sim->AddStock(NFood, -Sim->GetStock(NFood));
				Sim->AddStock(NFood, 0.5 * Sim->GetPopulation() * Sim->GetColonistFoodKgPerSol() * EraDt / SolLen);
				EraStepOnce();
			}
			const int32 Left = Sim->GetPopulation();
			bool bNewestFirst = true;
			for (const TPair<int32, int32>& P : FloorOf)
			{
				const int32 L = -P.Value;
				if (L < 1 || L > Floors) { continue; }
				if (Sim->FindColonist(P.Key)) { MaxKept[L] = FMath::Max(MaxKept[L], P.Key); }
				else { MinGone[L] = FMath::Min(MinGone[L], P.Key); }
			}
			int32 PerFloorGone = 0;
			for (int32 L = 1; L <= Floors; ++L) { bNewestFirst &= MaxKept[L] < MinGone[L]; PerFloorGone += MinGone[L] != TNumericLimits<int32>::Max() ? 1 : 0; }
			const double Expected = Before * (1.0 - 0.5 * (40.0 * EraDt / SolLen) / Sim->GetEffectiveEvacSols());
			Expect(Left < Before && Near(Left, Expected, 0.03 * Before) && PerFloorGone == Floors, // the gardens' food lifts the ration a hair over 0.5, so a few fewer leave
				FString::Printf(TEXT("recall: %d of %d recalled over two sols at half rations (the linear law says about %.0f left; every one of the %d floors lost some)"), Before - Left, Before, Expected, Floors));
			Expect(bNewestFirst,
				TEXT("recall: on every floor each resident who stayed arrived before each one who left (newest first, the roster shifts under every lookup)"));
			Sim->AddStock(NFood, -Sim->GetStock(NFood));
			Sim->AddStock(NFood, 1.0e8);
			for (int32 K = 0; K < 3; ++K) { EraStepOnce(); }
			const FRHColonist* Oldest = Sim->GetColonists().Num() ? &Sim->GetColonists()[0] : nullptr;
			Expect(Sim->FindColonist(1) != nullptr && Oldest && Oldest->Id == 1 && Sim->FindColonist(Oldest->Id) == Oldest,
				FString::Printf(TEXT("recall: the oldest hands stay, the roster still starts at #%d and every lookup follows the shift"), Oldest ? Oldest->Id : 0));
			VerifyAt(TEXT("after recall"));
			UE_LOG(LogRedHopeSim, Display, TEXT("SCALE digest recalled: %08x (%d left)"), Digest(), Left);

			// Save / load: every derived assignment must come back exactly.
			const uint32 AssignedBefore = Assignments();
			FString Err;
			Expect(Sim->SaveColony(TEXT("scaletest"), Err), FString::Printf(TEXT("save/load: saved %d colonists (%s)"), Left, *Err));
			FRHCommand Reorder; Reorder.Verb = FName("Assign"); Reorder.Level = WorkCells[2].X; Reorder.Value = (double)WorkCells[2].Y;
			Sim->EnqueueCommand(Reorder);
			EraStepOnce();
			Expect(Assignments() != AssignedBefore, TEXT("save/load: a new Assign order moved the seat plan after the save"));
			Expect(Sim->LoadColony(TEXT("scaletest"), Err) && Err.IsEmpty(), FString::Printf(TEXT("save/load: loaded (%s)"), *Err));
			Expect(Assignments() == AssignedBefore, TEXT("save/load: the reload restores every seat, bed, station, capacity and Hope line exactly"));
			VerifyAt(TEXT("after reload"));
			UE_LOG(LogRedHopeSim, Display, TEXT("SCALE digest reloaded: %08x"), Assignments());
		}

		// C2b: arrivals at scale stay cheap. Two thousand colonists come in through the new
		// arrival paths - eight supply ships' worth of settlers (50 each, the per-ship cap;
		// the city's comfortable room is what limits settlers, and it is not endless) and
		// eighty Colonist Transports (20 each, beds only) - one alert per event, every arrival housed
		// on a rated floor with a bed and not Mars-born; then a move-home pass over the
		// whole city (nobody is over a bed, so nobody moves) must cost next to nothing.
		double ArrivalsMs = 0.0, RehomeMs = 0.0;
		if (!bCapped)
		{
			const int32 FreeBefore = Sim->GetFreeHousing();
			const int32 PopBefore = Sim->GetPopulation();
			const int32 TopId = Sim->GetColonists().Num() ? Sim->GetColonists().Last().Id : 0;
			const int32 SettlersBefore = Sim->GetImmigrantsTotal();
			const double HopeBefore = Sim->GetHopeSmoothed();
			Expect(FreeBefore >= 2000, FString::Printf(TEXT("arrivals: %d beds free for 2,000 more (population %d of %d)"), FreeBefore, PopBefore, Sim->GetHousingCapacity()));
			int32 ArrivalAlerts = 0;
			const FDelegateHandle ArrivalH = Sim->OnAlert.AddLambda([&ArrivalAlerts](const FString&) { ++ArrivalAlerts; });
			Sim->Debug_SetHopeSmoothed(100.0);
			const double Arr0 = FPlatformTime::Seconds();
			int32 SettlersCame = 0;
			for (int32 K = 0; K < 8; ++K) { SettlersCame += Sim->Debug_Immigrate(); }
			for (int32 K = 0; K < 80; ++K) { Sim->Debug_DeliverCargo(FName("ColonistTransport")); }
			ArrivalsMs = (FPlatformTime::Seconds() - Arr0) * 1000.0;
			Sim->OnAlert.Remove(ArrivalH);
			Sim->Debug_SetHopeSmoothed(HopeBefore);
			int32 NewcomersOk = 0;
			for (const FRHColonist& C : Sim->GetColonists()) { NewcomersOk += (C.Id > TopId && !C.bMarsBorn && C.HomeHab == 0 && Sim->IsFloorRated(C.HomeLevel)) ? 1 : 0; }
			Expect(Sim->GetPopulation() == PopBefore + 2000 && SettlersCame == 400 && Sim->GetImmigrantsTotal() == SettlersBefore + 400 && NewcomersOk == 2000 && Sim->GetFreeHousing() == FreeBefore - 2000,
				FString::Printf(TEXT("arrivals: 2,000 colonists through 8 settler ships and 80 transports: population %d -> %d (%d settlers), %d of them housed on rated floors and not Mars-born"), PopBefore, Sim->GetPopulation(), SettlersCame, NewcomersOk));
			Expect(ArrivalAlerts == 88, FString::Printf(TEXT("arrivals: %d alerts for 88 events (one per transport, one per ship's settlers), never one per colonist"), ArrivalAlerts));
			const double Reh0 = FPlatformTime::Seconds();
			const int32 Moved = Sim->Debug_RehomeResidents();
			RehomeMs = (FPlatformTime::Seconds() - Reh0) * 1000.0;
			Expect(Moved == 0, FString::Printf(TEXT("arrivals: a move-home pass over %d colonists, none over a bed, moves %d"), Sim->GetPopulation(), Moved));
			UE_LOG(LogRedHopeSim, Display, TEXT("SCALE arrivals: 2,000 colonists (8 settler ships + 80 transports) in %.1f ms | move-home pass over %d colonists %.2f ms"), ArrivalsMs, Sim->GetPopulation(), RehomeMs);
			Sim->Debug_StepPopulation(URHSimClockSubsystem::SubStepSeconds);
			VerifyAt(TEXT("after arrivals"));
		}

		// Budgets (wall clock, shared machine): about seven times the numbers a
		// quiet run records, and well under what the old per-step rebuilds cost.
		if (Table.Num() == UE_ARRAY_COUNT(Targets))
		{
			const FScaleLine& At3000 = Table[2];
			const FScaleLine& At5000 = Table[3];
			Expect(At3000.AgentMs <= 2.0, FString::Printf(TEXT("budget: agent step at N=3000 %.3f ms (<= 2.0)"), At3000.AgentMs));
			Expect(At3000.EraMs <= 2.0, FString::Printf(TEXT("budget: era step at N=3000 %.3f ms (<= 2.0)"), At3000.EraMs));
			Expect(At5000.AgentMs <= 3.0, FString::Printf(TEXT("budget: agent step at N=5000 %.3f ms (<= 3.0)"), At5000.AgentMs));
			Expect(At5000.EraMs <= 3.0, FString::Printf(TEXT("budget: era step at N=5000 %.3f ms (<= 3.0)"), At5000.EraMs));
			Expect(At5000.HopeMs <= 0.5, FString::Printf(TEXT("budget: GetColonyHope at N=5000 %.3f ms (<= 0.5)"), At5000.HopeMs));
			Expect(At5000.AddMs <= 50.0, FString::Printf(TEXT("budget: moving 2,000 colonists in took %.1f ms (<= 50)"), At5000.AddMs));
			Expect(ArrivalsMs <= 50.0, FString::Printf(TEXT("budget: 2,000 colonists through the arrival paths (8 settler ships + 80 transports) took %.1f ms (<= 50)"), ArrivalsMs));
			Expect(RehomeMs <= 5.0, FString::Printf(TEXT("budget: a move-home pass over the whole city took %.2f ms (<= 5)"), RehomeMs));
		}

		// C4: roads at scale. A network of about 250 built edges (a lattice of ten long
		// drags each way, every crossing a junction), then a hundred robots each plan
		// a trip and drive it to the stop with the shared movement helper: planning is a
		// K-nearest scan and one Dijkstra, driving is a map lookup per step. The same
		// hundred plans twice must be the same hundred routes, and the road trips must
		// beat the straight line in total.
		{
			const double Net0 = FPlatformTime::Seconds();
			for (int32 K = 0; K < 10; ++K)
			{
				Sim->Debug_LayRoad(FVector(40000.0, 40000.0 + K * 2000.0, 0.0), FVector(58000.0, 40000.0 + K * 2000.0, 0.0), true);
				Sim->Debug_LayRoad(FVector(40000.0 + K * 2000.0, 40000.0, 0.0), FVector(40000.0 + K * 2000.0, 58000.0, 0.0), true);
			}
			const double NetworkMs = (FPlatformTime::Seconds() - Net0) * 1000.0;
			const FRHRoadGraph& Net = Sim->GetRoadGraph();
			int32 BuiltEdges = 0;
			for (const FRHRoadEdge& E : Net.Edges) { BuiltEdges += E.bBuilt ? 1 : 0; }
			Expect(BuiltEdges >= 200 && Sim->HasBuiltRoads(), FString::Printf(TEXT("roads: a network of %d built edges on %d nodes laid in %.1f ms"), BuiltEdges, Net.Nodes.Num(), NetworkMs));
			constexpr int32 Robots = 100;
			uint32 Rng = 77123u;
			const auto Rand = [&Rng]() { Rng = Rng * 1664525u + 1013904223u; return (double)((Rng >> 8) & 0xFFFF) / 65535.0; };
			TArray<FVector> TripStarts, TripEnds;
			for (int32 i = 0; i < Robots; ++i)
			{
				TripStarts.Add(FVector(40000.0 + Rand() * 18000.0, 40000.0 + Rand() * 18000.0, 0.0));
				TripEnds.Add(FVector(40000.0 + Rand() * 18000.0, 40000.0 + Rand() * 18000.0, 0.0));
			}
			const float RobotSpeed = 2.5f;
			TArray<FTransform> Bots;
			for (int32 i = 0; i < Robots; ++i) { Bots.Add(FTransform(FQuat::Identity, TripStarts[i])); }
			// The first call of each trip plans it.
			const double Plan0 = FPlatformTime::Seconds();
			TArray<bool> Arrived;
			Arrived.Init(false, Robots);
			for (int32 i = 0; i < Robots; ++i) { Arrived[i] = RH::MoveAlong(*Sim, FMassEntityHandle(5000 + i, 1), Bots[i], TripEnds[i], RobotSpeed, AgentDt); }
			const double PlanMs = (FPlatformTime::Seconds() - Plan0) * 1000.0;
			int32 RoadTrips = 0;
			for (int32 i = 0; i < Robots; ++i) { RoadTrips += (Sim->GetRobotRoadPath(FMassEntityHandle(5000 + i, 1)) && Sim->GetRobotRoadPath(FMassEntityHandle(5000 + i, 1))->IsRoadTrip()) ? 1 : 0; }
			// Drive them all to the stop.
			const double Drive0 = FPlatformTime::Seconds();
			int32 StepsTotal = 0;
			for (int32 Step = 0; Step < 6000; ++Step)
			{
				bool bAny = false;
				for (int32 i = 0; i < Robots; ++i)
				{
					if (Arrived[i]) { continue; }
					bAny = true;
					Arrived[i] = RH::MoveAlong(*Sim, FMassEntityHandle(5000 + i, 1), Bots[i], TripEnds[i], RobotSpeed, AgentDt);
					++StepsTotal;
				}
				if (!bAny) { break; }
			}
			const double DriveMs = (FPlatformTime::Seconds() - Drive0) * 1000.0;
			int32 Done = 0;
			for (int32 i = 0; i < Robots; ++i) { Done += Arrived[i] ? 1 : 0; }
			double StraightSteps = 0.0;
			for (int32 i = 0; i < Robots; ++i) { StraightSteps += FMath::Max(0.0, FVector::Dist2D(TripStarts[i], TripEnds[i]) - 300.0) / (RobotSpeed * 100.0 * AgentDt) + 1.0; }
			Expect(Done == Robots && RoadTrips >= 50 && (double)StepsTotal < StraightSteps,
				FString::Printf(TEXT("roads: %d of %d robots arrived, %d trips on roads, %d sub-steps in all against %.0f straight-line"), Done, Robots, RoadTrips, StepsTotal, StraightSteps));
			// The same hundred plans, twice.
			const auto RoutesDigest = [&]() -> uint32
			{
				uint32 H = 0;
				for (int32 i = 0; i < Robots; ++i)
				{
					FRHRoadRoute R;
					Sim->PlanRoadRoute(TripStarts[i], TripEnds[i], R);
					for (const int32 N : R.NodeIds) { H = FCrc::MemCrc32(&N, 4, H); }
					for (const int32 E : R.EdgeIds) { H = FCrc::MemCrc32(&E, 4, H); }
					H = FCrc::MemCrc32(&R.CostUnits, 8, H);
				}
				return H;
			};
			const double Rep0 = FPlatformTime::Seconds();
			const uint32 DigestA = RoutesDigest();
			const double ReplanMs = (FPlatformTime::Seconds() - Rep0) * 1000.0;
			const uint32 DigestB = RoutesDigest();
			Expect(DigestA == DigestB, FString::Printf(TEXT("roads: the same %d plans twice are the same routes (digest %08x)"), Robots, DigestA));
			UE_LOG(LogRedHopeSim, Display, TEXT("SCALE roads: %d edges | network laid %.1f ms | %d robots planned in %.2f ms (%.3f ms each) | replanned in %.2f ms | %d drive steps in %.2f ms (%.4f ms per robot-step)"),
				BuiltEdges, NetworkMs, Robots, PlanMs, PlanMs / Robots, ReplanMs, StepsTotal, DriveMs, StepsTotal > 0 ? DriveMs / StepsTotal : 0.0);
			// Budgets (wall clock, shared machine): about ten times a quiet run (0.5 ms, 0.83 ms
			// and 0.0001 ms here; the last is a map lookup and a float step, so it gets twenty).
			// A plan that cost O(edges) per robot per STEP, or a Dijkstra per sub-step, would
			// blow all three by orders of magnitude: a frame of 100 robots x 32 sub-steps is
			// 3,200 robot-steps, which at 0.002 ms is 6 ms, a third of a 60 fps frame at the very worst.
			Expect(NetworkMs <= 10.0, FString::Printf(TEXT("budget: laying the %d-edge network took %.1f ms (<= 10)"), BuiltEdges, NetworkMs));
			Expect(PlanMs <= 10.0, FString::Printf(TEXT("budget: 100 robots planning their first leg took %.2f ms (<= 10)"), PlanMs));
			Expect(StepsTotal <= 0 || DriveMs / StepsTotal <= 0.002, FString::Printf(TEXT("budget: a robot-step along a road took %.4f ms (<= 0.002)"), StepsTotal > 0 ? DriveMs / StepsTotal : 0.0));
		}

		// C5a: districts at scale. Fifty districts over the scale colony: forty-five on the
		// surface (a 3 x 3 block of lattice cells round a Pylon, each holding two buildings, a
		// Pylon and a Stockpile, kinds cycling through the five) and one on each of the five
		// floors (every floor's cells and every colonist's home are in a district), landing
		// through the real PaintDistrict order. Then: the membership caches rebuild (cold, the
		// first read after a change) and are read warm; GetDistrictCounts and GetCityDemand run
		// over the whole city; and everything adds up: every resident, every certified bed and
		// every seat is in exactly one district, and a save and a load give the same counts.
		// Budgets are wall clock on a shared machine, set well over a quiet run (see below).
		{
			const FName DistrictKinds[] = { FName("Homes"), FName("Industry"), FName("Farming"), FName("Research"), FName("Civic") };
			const int32 SurfaceDistricts = 45;
			// Budgets (wall clock, shared machine): about twenty times a quiet run (a cold rebuild
			// 0.04 ms, a warm lookup 0.00002 ms, the counts 0.02 ms, the demand 0.06 ms). A rebuild on
			// every lookup, or a scan of the roster per resident, would cost whole milliseconds a call.
			constexpr double BudgetColdMs = 1.0, BudgetWarmMs = 0.002, BudgetCountsMs = 1.0, BudgetDemandMs = 1.0;
			for (int32 K = 0; K < SurfaceDistricts; ++K)
			{
				const int32 CX = -45 + (K % 9) * 10, CY = 20 + (K / 9) * 10; // 100 m apart, west of the road network
				const FVector At = URHSimWorldSubsystem::LatticeCentreCm(FIntPoint(CX, CY));
				Sim->Debug_PlaceInstant(FName("Pylon"), At);
				Sim->Debug_PlaceInstant(FName("Stockpile"), At + FVector(1000.0, 0.0, 0.0));
			}
			int32 DistrictRejects = 0;
			const FDelegateHandle DistrictRejectH = Sim->OnCommandRejected.AddLambda([&DistrictRejects](const FRHCommand&, const FString&) { ++DistrictRejects; });
			const double Paint0 = FPlatformTime::Seconds();
			for (int32 K = 0; K < SurfaceDistricts; ++K)
			{
				const int32 CX = -45 + (K % 9) * 10, CY = 20 + (K / 9) * 10;
				FRHCommand Order;
				Order.Verb = FName("PaintDistrict");
				Order.Target = DistrictKinds[K % 5];
				Order.Location = URHSimWorldSubsystem::LatticeCentreCm(FIntPoint(CX - 1, CY - 1));
				Order.LocationB = URHSimWorldSubsystem::LatticeCentreCm(FIntPoint(CX + 2, CY + 1)); // 4 x 3: the Pylon cell, the Stockpile cell beside it, and their neighbours
				Sim->EnqueueCommand(Order);
			}
			for (int32 L = 1; L <= Floors; ++L)
			{
				FRHCommand Order;
				Order.Verb = FName("PaintDistrict");
				Order.Target = DistrictKinds[(SurfaceDistricts + L) % 5];
				Order.Level = -L;
				Sim->EnqueueCommand(Order);
			}
			EraStepOnce();
			const double PaintMs = (FPlatformTime::Seconds() - Paint0) * 1000.0;
			Sim->OnCommandRejected.Remove(DistrictRejectH);
			Expect(DistrictRejects == 0 && Sim->GetDistricts().Num() == SurfaceDistricts + Floors && Sim->GetNextDistrictId() == SurfaceDistricts + Floors + 1,
				FString::Printf(TEXT("districts: %d PaintDistrict orders landed (45 surface blocks of 12 cells, 5 floors), %d refused, %.1f ms with the era step"), SurfaceDistricts + Floors, DistrictRejects, PaintMs));
			Expect(Sim->GetDistricts().Last().Id == 50 && Sim->GetDistricts().Last().Name == RHDistricts::NameForId(50) && Sim->FindDistrict(41) && Sim->FindDistrict(41)->Name == TEXT("Gale II") && Sim->FindDistrict(50)->Name == TEXT("Arcadia II"),
				TEXT("districts: names follow the Id: #41 is 'Gale II', #50 'Arcadia II' - the walk of forty names has started again"));

			// Everything is in exactly one district: sums against the colony's own totals.
			TArray<FRHDistrictCounts> Counts;
			const double Cold0 = FPlatformTime::Seconds();
			Sim->GetDistrictCounts(Counts);
			const double ColdFirstMs = (FPlatformTime::Seconds() - Cold0) * 1000.0;
			int64 SumResidents = 0, SumBeds = 0, SumSeats = 0, SumBuildings = 0, SumCells = 0;
			for (const FRHDistrictCounts& C : Counts) { SumResidents += C.Residents; SumBeds += C.Beds; SumSeats += C.Seats; SumBuildings += C.Buildings; SumCells += C.Cells; }
			int32 FloorBuildings = 0;
			for (const FRHBuildingInstance& B : Sim->GetBuildings()) { FloorBuildings += B.Level < 0 ? 1 : 0; }
			const FRHCityDemand Dm = Sim->GetCityDemand();
			Expect(Counts.Num() == SurfaceDistricts + Floors && SumResidents == Sim->GetPopulation() && SumBeds == Sim->GetHousingCapacity() && SumSeats == Dm.TotalSeats,
				FString::Printf(TEXT("districts: every resident (%lld of %d), every certified bed (%lld of %d) and every work seat (%lld of %d) is in exactly one district"),
					SumResidents, Sim->GetPopulation(), SumBeds, Sim->GetHousingCapacity(), SumSeats, Dm.TotalSeats));
			Expect(SumBuildings == 2 * SurfaceDistricts + FloorBuildings && SumCells == 12 * SurfaceDistricts + Sim->GetFloorCarvedCells(-1) + Sim->GetFloorCarvedCells(-2) + Sim->GetFloorCarvedCells(-3) + Sim->GetFloorCarvedCells(-4) + Sim->GetFloorCarvedCells(-5),
				FString::Printf(TEXT("districts: %lld buildings (2 a surface block, %d on the floors) and %lld cells counted"), SumBuildings, FloorBuildings, SumCells));
			Expect(Dm.Population == Sim->GetPopulation() && Dm.ServiceGaps.Num() == 7 && Dm.Housing >= 0.0 && Dm.Housing <= 1.0 && Dm.Jobs > 0.5 && Dm.Services > 0.0 && Dm.Services < 1.0 && Dm.Air >= 0.0 && Dm.Air <= 1.0,
				FString::Printf(TEXT("districts: the city's demand at %d people reads Housing %.2f, Jobs %.2f (%d seats), Services %.2f, Air %.2f"), Dm.Population, Dm.Housing, Dm.Jobs, Dm.TotalSeats, Dm.Services, Dm.Air));

			// Timing: a cold rebuild (the first read after a change - a rename bumps the district version), warm reads, the whole-city counts and the demand.
			constexpr int32 ColdReps = 5, WarmReps = 200, CountReps = 50, DemandReps = 50;
			const int32 Probe = Sim->GetBuildings().Num() > 0 ? Sim->GetBuildings()[0].Id : 0;
			double ColdTotal = 0.0;
			for (int32 K = 0; K < ColdReps; ++K)
			{
				FRHCommand Rename;
				Rename.Verb = FName("RenameDistrict");
				Rename.Value = (double)(1 + K);
				Rename.Text = FString::Printf(TEXT("Scale %d"), K);
				Sim->EnqueueCommand(Rename);
				EraStepOnce();
				const double T0 = FPlatformTime::Seconds();
				Sim->GetDistrictOfBuilding(Probe);
				ColdTotal += FPlatformTime::Seconds() - T0;
			}
			const double ColdMs = ColdTotal * 1000.0 / ColdReps;
			int32 WarmSink = 0;
			const double W0 = FPlatformTime::Seconds();
			for (int32 K = 0; K < WarmReps; ++K) { WarmSink += Sim->GetDistrictOfColonist(1 + K * 7) + Sim->GetDistrictOfBuilding(Probe); }
			const double WarmMs = (FPlatformTime::Seconds() - W0) * 1000.0 / WarmReps;
			const double C0 = FPlatformTime::Seconds();
			for (int32 K = 0; K < CountReps; ++K) { Sim->GetDistrictCounts(Counts); }
			const double CountsMs = (FPlatformTime::Seconds() - C0) * 1000.0 / CountReps;
			double DemandSink = 0.0;
			const double D0 = FPlatformTime::Seconds();
			for (int32 K = 0; K < DemandReps; ++K) { DemandSink += Sim->GetCityDemand().Services; }
			const double DemandMs = (FPlatformTime::Seconds() - D0) * 1000.0 / DemandReps;
			UE_LOG(LogRedHopeSim, Display, TEXT("SCALE districts: %d districts, %d buildings, %d colonists | membership rebuild %.3f ms cold (first counts call %.3f ms) | warm read %.5f ms | counts %.3f ms | demand %.3f ms (sink %d %.4f)"),
				Sim->GetDistricts().Num(), Sim->GetBuildings().Num(), Sim->GetPopulation(), ColdMs, ColdFirstMs, WarmMs, CountsMs, DemandMs, WarmSink, DemandSink);
			Expect(Sim->Debug_DistrictCachesFresh() && Sim->FindDistrict(1) && Sim->FindDistrict(1)->Name == TEXT("Scale 0"), TEXT("districts: the caches were never stale across the five renames and the reads"));

			// A save and a load: the districts and their membership come back the same.
			TArray<FRHDistrictCounts> Before = Counts;
			FString DistrictErr;
			const int32 DistrictsBefore = Sim->GetDistricts().Num();
			Expect(Sim->SaveColony(TEXT("scaledistricts"), DistrictErr), FString::Printf(TEXT("districts: saved %d districts with %d colonists (%s)"), DistrictsBefore, Sim->GetPopulation(), *DistrictErr));
			Sim->EnqueueCommand([] { FRHCommand E; E.Verb = FName("EraseDistrict"); E.Value = 1.0; return E; }());
			EraStepOnce();
			Expect(Sim->GetDistricts().Num() == DistrictsBefore - 1, TEXT("districts: an erase after the save"));
			Expect(Sim->LoadColony(TEXT("scaledistricts"), DistrictErr) && DistrictErr.IsEmpty() && Sim->GetDistricts().Num() == DistrictsBefore, FString::Printf(TEXT("districts: loaded (%s)"), *DistrictErr));
			Sim->GetDistrictCounts(Counts);
			bool bSameCounts = Counts.Num() == Before.Num();
			for (int32 i = 0; bSameCounts && i < Counts.Num(); ++i)
			{
				bSameCounts &= Counts[i].DistrictId == Before[i].DistrictId && Counts[i].Buildings == Before[i].Buildings && Counts[i].Bays == Before[i].Bays
					&& Counts[i].Residents == Before[i].Residents && Counts[i].Beds == Before[i].Beds && Counts[i].Seats == Before[i].Seats && Counts[i].Cells == Before[i].Cells;
			}
			Expect(bSameCounts && Sim->FindDistrict(1) && Sim->FindDistrict(1)->Name == TEXT("Scale 0") && Sim->Debug_DistrictCachesFresh(), TEXT("districts: after the load every district's counts are exactly what they were"));

			// Budgets: about ten times a quiet run, as the other suites set them.
			Expect(ColdMs <= BudgetColdMs, FString::Printf(TEXT("budget: a cold membership rebuild over %d buildings and %d colonists took %.3f ms (<= %.1f)"), Sim->GetBuildings().Num(), Sim->GetPopulation(), ColdMs, BudgetColdMs));
			// -RHVerifyCaches re-derives the cache behind every lookup (that is its job), so a "warm" read is a
			// rebuild there: the warm budget is for plain runs.
			if (FParse::Param(*Params, TEXT("RHVerifyCaches")))
			{
				UE_LOG(LogRedHopeSim, Display, TEXT("SCALE ok: budget: a warm district lookup took %.5f ms (not budgeted under -RHVerifyCaches: every lookup re-derives the cache)"), WarmMs);
			}
			else
			{
				Expect(WarmMs <= BudgetWarmMs, FString::Printf(TEXT("budget: a warm district lookup took %.5f ms (<= %.3f)"), WarmMs, BudgetWarmMs));
			}
			Expect(CountsMs <= BudgetCountsMs, FString::Printf(TEXT("budget: GetDistrictCounts over %d districts took %.3f ms (<= %.1f)"), Sim->GetDistricts().Num(), CountsMs, BudgetCountsMs));
			Expect(DemandMs <= BudgetDemandMs, FString::Printf(TEXT("budget: GetCityDemand at %d people took %.3f ms (<= %.1f)"), Sim->GetPopulation(), DemandMs, BudgetDemandMs));
		}

		// C5b: growth at scale, on the surface. A Homes district of 30 x 30 lattice cells under a grid of 49
		// Pylons, a seed network of four rated bays, and the city grows it ITSELF - one project a sol, the very
		// code a game runs - until the network has about 500 bays (a standing housing shortfall, plenty of
		// material, Hope held high). Then 100 of the grown bays become Cooking stations (they emit Odor; the rest
		// are Living quarters that refuse it): the worst case for the surface adjacency term, which pairs every
		// emitter with every refuser in its network. The Hope read must stay inside the 0.5 ms budget with the
		// five-floor city underneath it (if it does not, the surface term is cached on ZoningVersion).
		if (Table.Num() == UE_ARRAY_COUNT(Targets))
		{
			constexpr int32 WantedBays = 500;
			const FName NHomes(TEXT("Homes")), NStruct(TEXT("Struct")), NShield(TEXT("Shielding")), NOx(TEXT("Oxygen"));
			const double HopeBefore0 = FPlatformTime::Seconds();
			double HopeSinkBefore = 0.0;
			for (int32 K = 0; K < HopeReps; ++K) { HopeSinkBefore += Sim->GetColonyHope().Total; }
			const double HopeBeforeMs = (FPlatformTime::Seconds() - HopeBefore0) * 1000.0 / HopeReps;
			for (int32 I = 0; I < 7; ++I)
			{
				for (int32 J = 0; J < 7; ++J) { Sim->Debug_PlaceInstant(FName("Pylon"), FVector(69000.0 + 5000.0 * I, -101000.0 + 5000.0 * J, 0.0)); }
			}
			const int32 SeedA = ([&]() { Sim->Debug_PlaceInstant(FName("HabModule"), FVector(84500.0, -85000.0, 0.0), 0, 2, 1); return Sim->GetBuildings().Last().Id; })();
			Sim->Debug_PlaceInstant(FName("HabModule"), FVector(84500.0, -86000.0, 0.0), 0, 2, 1);
			Sim->Debug_PlaceInstant(FName("AirFilter"), FVector(84000.0, -85000.0, 0.0));
			const int32 SeedB = SeedA + 1;
			FString ZoneErr;
			for (const int32 H : { SeedA, SeedB }) { for (int32 Bay = 0; Bay < 2; ++Bay) { Sim->DesignateBay(H, Bay, FName("LivingQuarters"), ZoneErr); } }
			Sim->AddStock(NStruct, 1.0e7);
			Sim->AddStock(NShield, 1.0e6);
			Sim->AddStock(NOx, 1.0e7);
			for (int32 K = 0; K < 80 && !Sim->IsHabRated(SeedA); ++K) { EraStepOnce(); }
			FRHCommand Paint;
			Paint.Verb = FName("PaintDistrict");
			Paint.Target = NHomes;
			Paint.Location = URHSimWorldSubsystem::LatticeCentreCm(FIntPoint(70, -100));
			Paint.LocationB = URHSimWorldSubsystem::LatticeCentreCm(FIntPoint(99, -71));
			const int32 DistrictsBefore = Sim->GetDistricts().Num();
			Sim->EnqueueCommand(Paint);
			EraStepOnce();
			const int32 SurfaceDistrict = Sim->GetDistricts().Num() > DistrictsBefore ? Sim->GetDistricts().Last().Id : 0;
			Expect(Sim->IsHabRated(SeedA) && Sim->GetHabNetworkBays(SeedA) == 4 && SurfaceDistrict != 0 && Sim->FindDistrict(SurfaceDistrict)->Lattice.Num() == 900 && Sim->GetDistrictOfBuilding(SeedA) == SurfaceDistrict,
				FString::Printf(TEXT("growth: a rated four-bay seed in a 900-cell Homes district #%d under 49 pylons"), SurfaceDistrict));
			// Let the city build. Growth is on by default in every district; the other districts' projects (the scale colony's
			// Homes and Civic ones) are switched off so the clock measures this one network.
			for (const FRHDistrict& D : TArray<FRHDistrict>(Sim->GetDistricts())) { Sim->SetDistrictGrowth(D.Id, D.Id == SurfaceDistrict); }
			Sim->Debug_SetBigArrivalDone(false);
			Sim->Debug_SetBigArrivalFreeBeds(10000000);
			// A surface network's air push is a flat 20 kg an hour, so past about 240 bays the leak (2 kg a bay a sol) outruns it and
			// the network would lose its rating on its own: the harness holds every grown shell full, as it holds the floors.
			const auto HoldAir = [&]()
			{
				for (const FRHBuildingInstance& B : Sim->GetBuildings())
				{
					if (B.DefName == FName("HabModule") && B.Level == 0 && !B.bUnderConstruction) { Sim->Debug_SetHabO2Kg(B.Id, Sim->GetHabO2RequiredKg(B.Id)); }
				}
			};
			int32 SolsRun = 0, Bays = 4;
			double GrowTotal = 0.0, GrowMax = 0.0, OtherTotal = 0.0;
			int32 GrowSteps = 0, OtherSteps = 0;
			const int32 Modules0 = [&]() { int32 N = 0; for (const FRHBuildingInstance& B : Sim->GetBuildings()) { N += B.DefName == FName("HabModule") ? 1 : 0; } return N; }();
			const double Grow0 = FPlatformTime::Seconds();
			while (Bays < WantedBays && SolsRun < 600)
			{
				for (int32 K = 0; K < (int32)(SolLen / EraDt); ++K)
				{
					Sim->Debug_SetHopeSmoothed(100.0);
					// The five floors' air is held full (the harness's surplus: a thousand people on a floor breathe faster than a
					// circulator pushes, and a few hundred sols would recall them - this is the surface network's clock, not theirs).
					for (int32 L = 1; L <= Floors; ++L) { Sim->Debug_SetFloorO2Kg(-L, Sim->GetFloorO2RequiredKg(-L)); }
					HoldAir();
					const int32 CheckedBefore = Sim->GetGrowthCheckedSol();
					TopUp(EraDt);
					Clock->Debug_AdvanceSimSeconds(EraDt);
					const double S0 = FPlatformTime::Seconds();
					Sim->EraStep((float)EraDt);
					const double Took = FPlatformTime::Seconds() - S0;
					if (Sim->GetGrowthCheckedSol() > CheckedBefore) { GrowTotal += Took; GrowMax = FMath::Max(GrowMax, Took); ++GrowSteps; } else { OtherTotal += Took; ++OtherSteps; }
				}
				++SolsRun;
				Bays = Sim->GetHabNetworkBays(SeedA);
			}
			const double GrowWallS = FPlatformTime::Seconds() - Grow0;
			int32 Modules1 = 0;
			for (const FRHBuildingInstance& B : Sim->GetBuildings()) { Modules1 += B.DefName == FName("HabModule") ? 1 : 0; }
			Expect(Bays >= WantedBays - 20 && Bays <= WantedBays + 40 && Modules1 > Modules0 + 100 && Sim->IsHabRated(SeedA),
				FString::Printf(TEXT("growth: the city grew the seed to %d bays in %d sols with %d modules of its own, in %.1f s of wall clock; the network is still rated"), Bays, SolsRun, Modules1 - Modules0, GrowWallS));
			Expect(Sim->IsFloorRated(-1) && Sim->IsFloorRated(-3) && Sim->IsFloorRated(-5) && Sim->GetPopulation() > 4000,
				TEXT("growth: the five-floor city underneath was never disturbed (floors rated, the population in place)"));
			// Hope and the sol-boundary step with 500 grown bays, then the emitters.
			const double HopeGrown0 = FPlatformTime::Seconds();
			double HopeSinkGrown = 0.0;
			for (int32 K = 0; K < HopeReps; ++K) { HopeSinkGrown += Sim->GetColonyHope().Total; }
			const double HopeGrownMs = (FPlatformTime::Seconds() - HopeGrown0) * 1000.0 / HopeReps;
			// 100 of the grown bays become Cooking stations: sell their beds (one Sell order each, all landing in one step), then zone them.
			TArray<TPair<int32, int32>> Emitters; // (shell, bay)
			for (const FRHBuildingInstance& B : Sim->GetBuildings())
			{
				if (B.DefName != FName("HabModule") || B.Level != 0 || B.bUnderConstruction || Sim->GetDistrictOfBuilding(B.Id) != SurfaceDistrict) { continue; }
				const int32 Count = B.SizeX * B.SizeY;
				for (int32 Bay = 0; Bay < Count; ++Bay)
				{
					if (Emitters.Num() < 100 && (B.Id + Bay) % 4 == 0 && Sim->GetHabNetworkRoot(B.Id) == SeedA) { Emitters.Emplace(B.Id, Bay); }
				}
			}
			for (const TPair<int32, int32>& E : Emitters)
			{
				const FRHBuildingInstance* Shell = nullptr;
				for (const FRHBuildingInstance& B : Sim->GetBuildings()) { if (B.Id == E.Key) { Shell = &B; break; } }
				if (!Shell) { continue; }
				FRHCommand Sell;
				Sell.Verb = FName("Sell");
				Sell.Target = FName("Cell");
				Sell.Level = 0;
				Sell.Location = Sim->GetBayCentreCm(*Shell, E.Value);
				Sim->EnqueueCommand(Sell);
			}
			EraStepOnce();
			int32 Zoned = 0;
			for (const TPair<int32, int32>& E : Emitters) { Zoned += Sim->DesignateBay(E.Key, E.Value, FName("Cooking"), ZoneErr) ? 1 : 0; }
			EraStepOnce();
			const URHSimWorldSubsystem::FRHHopeBreakdown Hp = Sim->GetColonyHope();
			const double HopeEmit0 = FPlatformTime::Seconds();
			double HopeSinkEmit = 0.0;
			for (int32 K = 0; K < HopeReps; ++K) { HopeSinkEmit += Sim->GetColonyHope().Total; }
			const double HopeEmitMs = (FPlatformTime::Seconds() - HopeEmit0) * 1000.0 / HopeReps;
			// A sol-boundary step at this size (growth's start check over the 500-bay network), and an agent step.
			double BoundaryMs = 0.0, AgentSurfaceMs = 0.0;
			for (int32 K = 0; K < (int32)(SolLen / EraDt) + 1 && BoundaryMs == 0.0; ++K)
			{
				Sim->Debug_SetHopeSmoothed(100.0);
				for (int32 L = 1; L <= Floors; ++L) { Sim->Debug_SetFloorO2Kg(-L, Sim->GetFloorO2RequiredKg(-L)); }
				HoldAir();
				const int32 CheckedBefore = Sim->GetGrowthCheckedSol();
				TopUp(EraDt);
				Clock->Debug_AdvanceSimSeconds(EraDt);
				const double S0 = FPlatformTime::Seconds();
				Sim->EraStep((float)EraDt);
				if (Sim->GetGrowthCheckedSol() > CheckedBefore) { BoundaryMs = (FPlatformTime::Seconds() - S0) * 1000.0; }
			}
			HoldAir();
			for (int32 K = 0; K < 50; ++K)
			{
				TopUp(AgentDt);
				Clock->Debug_AdvanceSimSeconds(AgentDt);
				const double S0 = FPlatformTime::Seconds();
				Sim->Debug_StepSim(URHSimClockSubsystem::SubStepSeconds);
				AgentSurfaceMs += (FPlatformTime::Seconds() - S0) * 1000.0 / 50.0;
			}
			UE_LOG(LogRedHopeSim, Display, TEXT("SCALE growth: %d bays grown in %d sols (%d modules) | era step on a boundary sol %.3f ms avg, %.3f ms worst (%d), other steps %.3f ms (%d) | sol-boundary step at %d bays %.3f ms | hope read %.3f ms before the surface network, %.3f ms with %d grown bays, %.3f ms with %d Cooking stations (%d zoned; adjacency %.2f, %d of %d refuser cells offended, sink %.2f %.2f %.2f) | agent step %.3f ms"),
				Bays, SolsRun, Modules1 - Modules0, GrowSteps > 0 ? GrowTotal * 1000.0 / GrowSteps : 0.0, GrowMax * 1000.0, GrowSteps, OtherSteps > 0 ? OtherTotal * 1000.0 / OtherSteps : 0.0, OtherSteps,
				Bays, BoundaryMs, HopeBeforeMs, HopeGrownMs, Bays, HopeEmitMs, Emitters.Num(), Zoned, Hp.AdjacencyPenalty, Hp.OffendedRefusers, Hp.RefuserCells, HopeSinkBefore, HopeSinkGrown, HopeSinkEmit, AgentSurfaceMs);
			Expect(Zoned == Emitters.Num() && Emitters.Num() == 100 && Hp.OffendedRefusers > 100 && Hp.RefuserCells > 900, // the floors' Living cells and the surface bays together
				FString::Printf(TEXT("growth: 100 grown bays are Cooking stations among the Living bays: %d of %d refuser cells are offended (adjacency %.2f)"), Hp.OffendedRefusers, Hp.RefuserCells, Hp.AdjacencyPenalty));
			Expect(HopeEmitMs <= 0.5, FString::Printf(TEXT("budget: GetColonyHope with the five floors and a surface network of %d bays with 100 odor emitters took %.3f ms (<= 0.5)"), Bays, HopeEmitMs));
			Expect(BoundaryMs <= 25.0, FString::Printf(TEXT("budget: a sol-boundary era step (growth's start check over a %d-bay network and 5,400 colonists) took %.3f ms (<= 25)"), Bays, BoundaryMs));
			Expect(AgentSurfaceMs <= 3.0, FString::Printf(TEXT("budget: an agent step with the surface network took %.3f ms (<= 3.0)"), AgentSurfaceMs));
		}

		UE_LOG(LogRedHopeSim, Display, TEXT("=== SCALE TEST END: %d failure(s) ==="), Fails);
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return Fails > 0 ? 1 : 0;
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
