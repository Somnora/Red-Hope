#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "RHSimWorldSubsystem.generated.h"

class URHDefinitionsSubsystem;
class URHSimClockSubsystem;

// A strategic order. Presentation can only reach the sim through these:
// commands enter the uplink queue, wait out the signal lag, then apply at a
// sim-tick boundary. The Earth-latency mechanic is a property of this seam.
USTRUCT()
struct REDHOPESIM_API FRHCommand
{
	GENERATED_BODY()

	UPROPERTY() FName Verb;                     // Build, SetPriority, ConfirmManifest, ...
	UPROPERTY() FName Target;                   // definition row name or agent id
	UPROPERTY() FVector Location = FVector::ZeroVector;
	UPROPERTY() double Value = 0.0;
	UPROPERTY() double IssuedAtSimSeconds = 0.0;
	UPROPERTY() double ExecuteAtSimSeconds = 0.0;
};

// A placed structure as the sim knows it. Presentation mirrors these; it
// never owns them.
USTRUCT()
struct REDHOPESIM_API FRHBuildingInstance
{
	GENERATED_BODY()

	UPROPERTY() int32 Id = 0;
	UPROPERTY() FName DefName;
	UPROPERTY() FVector LocationCm = FVector::ZeroVector;
	UPROPERTY() bool bPowered = true; // load shedding lands with active production loads (M0-b)
};

// Colony power state, per sim step. Units doctrine: W flows, Wh stocks,
// sol-hour = 50 sim-seconds (see m0-vertical-slice-spec.md §0).
USTRUCT()
struct REDHOPESIM_API FRHPowerState
{
	GENERATED_BODY()

	UPROPERTY() double GenW = 0.0;
	UPROPERTY() double LoadW = 0.0;
	UPROPERTY() double BatteryWh = 0.0;
	UPROPERTY() double BatteryCapWh = 0.0;
	UPROPERTY() bool bDeficit = false;
};

DECLARE_MULTICAST_DELEGATE_TwoParams(FRHOnStockChanged, FName /*Resource*/, double /*NewAmount*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FRHOnCommandExecuted, const FRHCommand&);
DECLARE_MULTICAST_DELEGATE_OneParam(FRHOnBuildingAdded, const FRHBuildingInstance&);
DECLARE_MULTICAST_DELEGATE_TwoParams(FRHOnCommandRejected, const FRHCommand&, const FString& /*Reason*/);

// Single owner of colony truth: definitions-driven buildings, power ledger,
// coverage territory, stocks, uplink queue. Ticks the sim in fixed sub-steps
// consumed from the clock. M0-a scope: power + territory + Build command.
// M0-b adds: production, hauling integration, quota, shedding by priority.
UCLASS()
class REDHOPESIM_API URHSimWorldSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(URHSimWorldSubsystem, STATGROUP_Tickables);
	}
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override
	{
		return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
	}

	// --- Command seam (in) ---
	void EnqueueCommand(FRHCommand Command);
	double GetOrderLagSeconds() const { return OrderLagSeconds; }
	void SetOrderLagSeconds(double Lag) { OrderLagSeconds = Lag; }
	const TArray<FRHCommand>& GetUplinkQueue() const { return UplinkQueue; }

	// --- Territory ---
	// Power-as-Territory: buildable space is the union of coverage discs
	// around the Lander and Pylons. The one query placement must pass.
	bool IsInCoverage(const FVector& LocationCm) const;

	// --- Reads (out) ---
	const TArray<FRHBuildingInstance>& GetBuildings() const { return Buildings; }
	const FRHPowerState& GetPower() const { return Power; }
	double GetStock(FName Resource) const;
	void AddStock(FName Resource, double Delta);

	FRHOnStockChanged OnStockChanged;
	FRHOnCommandExecuted OnCommandExecuted;
	FRHOnBuildingAdded OnBuildingAdded;
	FRHOnCommandRejected OnCommandRejected;

private:
	void StepSim(float SubDt);
	void StepPower(float SubDt);
	void ExecuteCommand(const FRHCommand& Cmd);
	void AddBuilding(FName DefName, const FVector& LocationCm);

	TMap<FName, double> Stocks;
	TArray<FRHCommand> UplinkQueue;
	TArray<FRHBuildingInstance> Buildings;
	FRHPowerState Power;
	double OrderLagSeconds = 45.0; // overridden from DT_Config OrderLagTier0_s at begin play
	int32 NextBuildingId = 1;

	UPROPERTY() TObjectPtr<URHDefinitionsSubsystem> Defs;
	UPROPERTY() TObjectPtr<URHSimClockSubsystem> Clock;
};
