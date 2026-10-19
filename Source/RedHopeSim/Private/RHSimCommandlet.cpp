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

		// P0. Pure data: the thirteen config rows and the manifest row come off the
		// synced DataTables alone (no override, no C++ fallback), so a DT that
		// drifted from the CSV fails here and not silently.
		const TPair<const TCHAR*, double> Pins[] = {
			{ TEXT("ColonistTransportColonists"), 20.0 }, { TEXT("ColonistTransportFoodKgPerColonist"), 50.0 }, { TEXT("BirthRefPop"), 25.0 },
			{ TEXT("ImmigrationHopeMin"), 75.0 }, { TEXT("ImmigrationHopeFloor"), 70.0 }, { TEXT("ImmigrationBase"), 4.0 },
			{ TEXT("ImmigrationPerCapita"), 0.2 }, { TEXT("ImmigrationCapPerShip"), 50.0 }, { TEXT("BigArrivalHopeMin"), 75.0 },
			{ TEXT("BigArrivalStreakSols"), 20.0 }, { TEXT("BigArrivalFreeBeds"), 100.0 }, { TEXT("BigArrivalColonists"), 100.0 },
			{ TEXT("MoveHomeIntervalSols"), 1.0 }, { TEXT("ImmigrationReserveBeds"), 1.0 } };
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
			&& Near(Sim->GetMoveHomeIntervalSols(), 1.0, 1e-9) && Sim->GetImmigrationReserveBeds() == 1,
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

		Sim->OnAlert.Remove(AlertH);
		Sim->OnAlert.Remove(SnapH);
		Sim->OnShipArrived.Remove(ShipH);
		UE_LOG(LogRedHopeSim, Display, TEXT("=== ARRIVALS TEST END: %d check(s), %d failure(s) ==="), Checks, Fails);
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
