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

		const FVector Head(1000.f, 1000.f, 0.f);
		FString R;
		Sim->ExtendShaft(1, Head);
		Sim->ExcavateFloor(-1, 2, R);          // 2 cells -> 200 kg O2 required
		Sim->Debug_PlaceInstant(FName("SolarArray"), FVector(3500.f, 1000.f, 0.f));
		Sim->Debug_PlaceInstant(FName("BatteryBank"), FVector(1000.f, 3500.f, 0.f));
		Sim->AddStock(FName("Oxygen"), 500.0);

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
		UE_LOG(LogRedHopeSim, Display, TEXT("HABITAT circulator on: O2 fill %.0f / %.0f, rated=%d (expect 200/200, rated 1)"),
			Sim->GetFloorO2Kg(-1), Sim->GetFloorO2RequiredKg(-1), (int32)Sim->IsFloorRated(-1));

		FString Err;
		Sim->SaveColony(TEXT("habitattest"), Err);
		Sim->LoadColony(TEXT("habitattest"), Err);
		UE_LOG(LogRedHopeSim, Display, TEXT("HABITAT save/load v7: fill %.0f rated=%d (expect 200, 1)"),
			Sim->GetFloorO2Kg(-1), (int32)Sim->IsFloorRated(-1));
		// Reload rebuilt the DT-backed defs? No - rows live in the table asset;
		// the patched flag persists for this process. Drain the pool: leakage
		// alone must drop the floor below 98% and announce the loss.
		Sim->AddStock(FName("Oxygen"), -Sim->GetStock(FName("Oxygen")));
		RunSols(3.0);
		UE_LOG(LogRedHopeSim, Display, TEXT("HABITAT pool dry 3 sols: fill %.0f, rated=%d (expect < 196, rated 0 - loss announced)"),
			Sim->GetFloorO2Kg(-1), (int32)Sim->IsFloorRated(-1));

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
