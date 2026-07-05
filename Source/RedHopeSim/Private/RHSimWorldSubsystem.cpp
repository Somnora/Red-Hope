#include "RHSimWorldSubsystem.h"
#include "RedHopeSim.h"
#include "RHDefinitionsSubsystem.h"
#include "RHSimClockSubsystem.h"

void URHSimWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Defs = Collection.InitializeDependency<URHDefinitionsSubsystem>();
	Clock = Collection.InitializeDependency<URHSimClockSubsystem>();
}

void URHSimWorldSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	if (Defs)
	{
		OrderLagSeconds = Defs->GetConfigScalar(FName("OrderLagTier0_s"), OrderLagSeconds);
	}

	// Mission start: the Lander is already down at the origin. Everything
	// else is player-built. (Starter robot/stock deployment: M0-b.)
	AddBuilding(FName("Lander"), FVector::ZeroVector);

	UE_LOG(LogRedHopeSim, Display, TEXT("Colony sim online. Lag %.0f sim-s. Sol length %.0f sim-s."),
		OrderLagSeconds, URHSimClockSubsystem::SolLengthSimSeconds);
}

void URHSimWorldSubsystem::Tick(float DeltaTime)
{
	if (!Clock)
	{
		return;
	}
	// Same sub-step count the agent processors observe this frame - one
	// timeline, multiple readers (single-driver refactor noted for M0-b).
	const int32 Steps = Clock->GetStepsThisFrame();
	for (int32 i = 0; i < Steps; ++i)
	{
		StepSim(URHSimClockSubsystem::SubStepSeconds);
	}
}

void URHSimWorldSubsystem::StepSim(float SubDt)
{
	// Uplink: signal-delayed orders land at sub-step boundaries.
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

	StepPower(SubDt);
}

void URHSimWorldSubsystem::StepPower(float SubDt)
{
	if (!Defs)
	{
		return;
	}
	const float Solar = Defs->EvalSolarCurve(Clock->GetSolFraction());

	double GenW = 0.0;
	double LoadW = 0.0;
	double CapWh = 0.0;
	for (const FRHBuildingInstance& B : Buildings)
	{
		if (const FRHBuildingRow* Def = Defs->GetBuilding(B.DefName))
		{
			GenW += Def->PowerGenPeak_W * Solar; // DustFactor joins at M1 (storms)
			LoadW += Def->PowerIdle_W;           // active production draws land with production (M0-b)
			CapWh += Def->StorageWh;
		}
	}

	Power.GenW = GenW;
	Power.LoadW = LoadW;
	Power.BatteryCapWh = CapWh;

	// Units doctrine: Wh = W x sol-hours; sol-hour = 50 sim-s.
	const double DeltaWh = (GenW - LoadW) * (SubDt / 50.0);
	Power.BatteryWh = FMath::Clamp(Power.BatteryWh + DeltaWh, 0.0, CapWh);
	Power.bDeficit = (DeltaWh < 0.0) && (Power.BatteryWh <= 0.0);
}

void URHSimWorldSubsystem::EnqueueCommand(FRHCommand Command)
{
	Command.IssuedAtSimSeconds = Clock ? Clock->GetSimSecondsTotal() : 0.0;
	Command.ExecuteAtSimSeconds = Command.IssuedAtSimSeconds + OrderLagSeconds;
	UE_LOG(LogRedHopeSim, Display, TEXT("Uplink: '%s %s' transmitted, executes in %.0f sim-s"),
		*Command.Verb.ToString(), *Command.Target.ToString(), OrderLagSeconds);
	UplinkQueue.Add(MoveTemp(Command));
}

void URHSimWorldSubsystem::ExecuteCommand(const FRHCommand& Cmd)
{
	if (Cmd.Verb == FName("Build"))
	{
		const FRHBuildingRow* Def = Defs ? Defs->GetBuilding(Cmd.Target) : nullptr;
		if (!Def)
		{
			OnCommandRejected.Broadcast(Cmd, FString::Printf(TEXT("Unknown building '%s'"), *Cmd.Target.ToString()));
			return;
		}
		// Power-as-Territory: the grid's footprint is the border.
		if (!IsInCoverage(Cmd.Location))
		{
			OnCommandRejected.Broadcast(Cmd, TEXT("Outside grid coverage - extend pylons first"));
			UE_LOG(LogRedHopeSim, Warning, TEXT("Build %s rejected: outside coverage"), *Cmd.Target.ToString());
			return;
		}
		// Struct cost + build time arrive with hauling/fabrication (M0-b);
		// M0-a validates territory + power math first.
		AddBuilding(Cmd.Target, Cmd.Location);
	}

	OnCommandExecuted.Broadcast(Cmd);
}

void URHSimWorldSubsystem::AddBuilding(FName DefName, const FVector& LocationCm)
{
	FRHBuildingInstance Instance;
	Instance.Id = NextBuildingId++;
	Instance.DefName = DefName;
	Instance.LocationCm = LocationCm;
	Buildings.Add(Instance);

	// Batteries arrive charged in M0-a so power math is observable
	// immediately; real charge states come with manifests/hauling.
	if (const FRHBuildingRow* Def = Defs ? Defs->GetBuilding(DefName) : nullptr)
	{
		Power.BatteryWh += Def->StorageWh * 0.5;
	}

	UE_LOG(LogRedHopeSim, Display, TEXT("Built %s #%d at (%.0f, %.0f) m"),
		*DefName.ToString(), Instance.Id, LocationCm.X / 100.0, LocationCm.Y / 100.0);
	OnBuildingAdded.Broadcast(Instance);
}

bool URHSimWorldSubsystem::IsInCoverage(const FVector& LocationCm) const
{
	for (const FRHBuildingInstance& B : Buildings)
	{
		if (const FRHBuildingRow* Def = Defs ? Defs->GetBuilding(B.DefName) : nullptr)
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
