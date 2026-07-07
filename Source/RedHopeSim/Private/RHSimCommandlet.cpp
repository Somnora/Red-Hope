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
