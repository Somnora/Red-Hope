#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "RHSimWorldSubsystem.generated.h"

class UDataTable;

// A strategic order. Presentation can only reach the sim through these:
// commands enter the uplink queue, wait out the signal lag, then apply at a
// sim-tick boundary. The Earth-latency mechanic is a property of this seam.
USTRUCT()
struct REDHOPESIM_API FRHCommand
{
	GENERATED_BODY()

	UPROPERTY() FName Verb;                     // e.g. PlaceBuilding, SetPriority, ConfirmManifest
	UPROPERTY() FName Target;                   // definition row name or agent id
	UPROPERTY() FVector Location = FVector::ZeroVector;
	UPROPERTY() double Value = 0.0;
	UPROPERTY() double IssuedAtSimSeconds = 0.0;
	UPROPERTY() double ExecuteAtSimSeconds = 0.0;
};

DECLARE_MULTICAST_DELEGATE_TwoParams(FRHOnStockChanged, FName /*Resource*/, double /*NewAmount*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FRHOnCommandExecuted, const FRHCommand&);

// Single owner of colony truth: stocks ledger, definitions, uplink queue.
// Scaffold scope: ledger + queue mechanics are real; command verbs are stubs.
// M0 adds: grid graph, production, quota tracking, era integrator, save.
UCLASS()
class REDHOPESIM_API URHSimWorldSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override
	{
		return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
	}

	// --- Command seam (in) ---
	void EnqueueCommand(FRHCommand Command);
	void ProcessDueCommands(double NowSimSeconds);
	double GetOrderLagSeconds() const { return OrderLagSeconds; }
	void SetOrderLagSeconds(double Lag) { OrderLagSeconds = Lag; }
	const TArray<FRHCommand>& GetUplinkQueue() const { return UplinkQueue; }

	// --- Ledger (out: events + const reads) ---
	double GetStock(FName Resource) const;
	void AddStock(FName Resource, double Delta);

	FRHOnStockChanged OnStockChanged;
	FRHOnCommandExecuted OnCommandExecuted;

private:
	TMap<FName, double> Stocks;
	TArray<FRHCommand> UplinkQueue;
	double OrderLagSeconds = 45.0; // RH_Config OrderLagTier0_s; registry load is an M0 task
};
