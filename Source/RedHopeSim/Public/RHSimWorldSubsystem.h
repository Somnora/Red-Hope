#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "RHSimTypes.h"
#include "RHSimWorldSubsystem.generated.h"

class URHDefinitionsSubsystem;
class URHSimClockSubsystem;
class URHAgentSubsystem;
struct FRHRobotRow;

// A strategic order. Presentation can only reach the sim through these:
// commands enter the uplink queue, wait out the signal lag, then apply at a
// sim-tick boundary. The Earth-latency mechanic is a property of this seam.
USTRUCT()
struct REDHOPESIM_API FRHCommand
{
	GENERATED_BODY()

	UPROPERTY() FName Verb;                     // Build, Dig, ...
	UPROPERTY() FName Target;                   // definition/deposit row name
	UPROPERTY() FVector Location = FVector::ZeroVector;
	UPROPERTY() double Value = 0.0;
	UPROPERTY() double IssuedAtSimSeconds = 0.0;
	UPROPERTY() double ExecuteAtSimSeconds = 0.0;
};

// A placed structure as the sim knows it. Solids live here (approved hybrid
// logistics): InputKg feeds recipes/construction, OutputKg awaits hauling.
USTRUCT()
struct REDHOPESIM_API FRHBuildingInstance
{
	GENERATED_BODY()

	UPROPERTY() int32 Id = 0;
	UPROPERTY() FName DefName;
	UPROPERTY() FVector LocationCm = FVector::ZeroVector;
	UPROPERTY() bool bUnderConstruction = false;
	UPROPERTY() bool bPowered = true;           // false when shed by priority
	UPROPERTY() double BuildRemaining_s = 0.0;
	UPROPERTY() double BatchRemaining_h = 0.0;  // > 0: recipe in progress
	UPROPERTY() FName ActiveRecipe;
	UPROPERTY() int32 AttachedDepositId = 0;    // extraction buildings (RequiresDeposit)
	UPROPERTY() TMap<FName, double> InputKg;
	UPROPERTY() TMap<FName, double> OutputKg;
};

USTRUCT()
struct REDHOPESIM_API FRHPowerState
{
	GENERATED_BODY()

	UPROPERTY() double GenW = 0.0;
	UPROPERTY() double LoadW = 0.0;
	UPROPERTY() double BatteryWh = 0.0;
	UPROPERTY() double BatteryCapWh = 0.0;
	UPROPERTY() bool bDeficit = false;
	UPROPERTY() int32 ShedCount = 0;           // buildings currently unpowered by shedding
};

DECLARE_MULTICAST_DELEGATE_TwoParams(FRHOnStockChanged, FName, double);
DECLARE_MULTICAST_DELEGATE_OneParam(FRHOnCommandExecuted, const FRHCommand&);
DECLARE_MULTICAST_DELEGATE_OneParam(FRHOnBuildingAdded, const FRHBuildingInstance&);
DECLARE_MULTICAST_DELEGATE_OneParam(FRHOnBuildingCompleted, const FRHBuildingInstance&);
DECLARE_MULTICAST_DELEGATE_TwoParams(FRHOnCommandRejected, const FRHCommand&, const FString&);
DECLARE_MULTICAST_DELEGATE_OneParam(FRHOnRobotsSpawned, const TArray<FMassEntityHandle>&);
DECLARE_MULTICAST_DELEGATE_TwoParams(FRHOnQuotaMet, int32 /*Sol*/, double /*AwardKg*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FRHOnShipArrived, const TArray<FName>& /*Items*/);
DECLARE_MULTICAST_DELEGATE(FRHOnColonyReloaded);

// Single owner of colony truth. Its Tick is the sim driver: uplink ->
// task board -> production -> power, in that fixed order, per sub-step.
// Robot movement/work executes in Mass processors against the same
// published step count, mutating the economy only through the API below.
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
	bool IsInCoverage(const FVector& LocationCm) const;
	// True if a Build order for Def at LocationCm would be accepted right now
	// (link/coverage rule, import stock, material sufficiency). The placement
	// ghost polls this every frame; ExecuteCommand runs the SAME check at
	// uplink execution - the authoritative gate stays at the seam, and the
	// world may have changed during the signal delay (that is the game).
	bool CanPlaceBuilding(FName DefName, const FVector& LocationCm, FString& OutReason) const;
	// Nearest slice deposit within MaxDistCm of a point, or nullptr (click-to-dig).
	const FRHDepositState* FindDepositNear(const FVector& LocationCm, double MaxDistCm) const;

	// --- Reads (out) ---
	const TArray<FRHBuildingInstance>& GetBuildings() const { return Buildings; }
	const TArray<FRHDepositState>& GetDeposits() const { return Deposits; }
	const FRHPowerState& GetPower() const { return Power; }
	int32 GetOpenTaskCount() const { return Tasks.Num(); }
	// Colony-held solids: sum of building stores (deposit piles are not
	// collected yet, so they do not count toward quota).
	double GetTotalSolid(FName Resource) const;
	// Network-instant pool (fluids/gases/abstract per hybrid logistics).
	double GetStock(FName Resource) const;
	void AddStock(FName Resource, double Delta);
	// Quota progress vs DT_Quotas Q1: resource -> (have, need).
	TMap<FName, TPair<double, double>> GetQuotaProgress() const;

	// --- Robot work API (game-thread, called by Mass processors) ---
	FVector GetSiteLocation(const FRHSiteRef& Site) const;
	// Excavator: ground -> pile, respecting pile cap and remaining mass.
	double DigDeposit(int32 DepositId, double Kg);
	bool IsDepositWorkable(int32 DepositId) const; // designated, mass left, pile below cap
	bool IsDepositSpent(int32 DepositId) const;    // no mass left underground or undesignated
	void ReleaseDigClaim(int32 DepositId);
	int32 TryClaimDig(const FVector& RobotPosCm);
	// Hauler: claim nearest open haul task; then load at From / unload at To.
	bool TryClaimHaul(FMassEntityHandle Robot, const FVector& RobotPosCm, FRHTask& OutTask);
	// Loads up to CargoCapKg from the task's From site. Returns false if the
	// task is gone or nothing could be loaded; otherwise sets the loaded mass
	// and the dropoff location.
	bool HaulLoad(int32 TaskId, float CargoCapKg, float& OutLoadedKg, FVector& OutDropoffCm);
	// Delivers cargo to the task's To building and completes the task.
	void HaulUnload(int32 TaskId, float CargoKg);
	// Fabricator: claim nearest construction; apply work seconds. Work only
	// progresses once the site's materials are delivered.
	bool TryClaimBuild(FMassEntityHandle Robot, const FVector& RobotPosCm, FRHTask& OutTask);
	bool ApplyBuildWork(int32 BuildingId, double Seconds); // true when completed
	void CompleteTask(int32 TaskId);
	void AbandonTask(int32 TaskId);
	// Charging: pads only (design rule). Returns false if no completed,
	// powered pad exists.
	bool FindNearestChargePad(const FVector& RobotPosCm, int32& OutBuildingId, FVector& OutLocationCm) const;
	// Grants up to the pad's transfer rate x dt from grid energy. Zero when
	// the pad is shed/unbuilt or the grid has nothing to give (night, empty
	// bank) - the robot waits docked. Multiple robots may share a pad at
	// slice scale (queueing etiquette: post-M0 StateTree work).
	double RequestChargeWh(int32 PadBuildingId, double SubDt);
	float GetChargeSeekFraction() const { return ChargeSeekFraction; }
	float GetChargeResumeFraction() const { return ChargeResumeFraction; }

	// --- Quota / manifest / ship (the slice finale) ---
	ERHQuotaPhase GetQuotaPhase() const { return QuotaPhase; }
	double GetAwardMassKg() const { return AwardMassKg; }
	double GetManifestMassKg() const;
	const TArray<FName>& GetManifestItems() const { return ManifestItems; }
	double GetShipEtaSimSeconds() const { return ShipArrivalSimSeconds; }
	int32 GetImportStock(FName DefName) const;
	// Add an item if the award budget covers it. Returns false + reason otherwise.
	bool AddManifestItem(FName ItemName, FString& OutError);
	// Confirm the manifest and launch the ship (AwaitingManifest only).
	bool LaunchShip(FString& OutError);

	// --- Save / load (sim-owned, versioned binary; M1-a) ---
	// Snapshot the whole colony to Saved/SaveGames/RH_<Slot>.sav. Task claims
	// are cleared and in-flight hauler cargo returned to its source in the
	// saved copy, so a load never depends on robot intent.
	bool SaveColony(const FString& Slot, FString& OutError);
	// Despawn everything, apply the snapshot, respawn robots, then broadcast
	// OnColonyReloaded followed by OnRobotsSpawned - presentation rebuilds
	// entirely from those two events.
	bool LoadColony(const FString& Slot, FString& OutError);

	// --- Era mode (tier 60, M1-a): ledger integration at 1 sim-min steps ---
	// One coarse step: uplink -> abstracted logistics -> production -> quota
	// -> power, reusing the sub-step functions at dt = 60 s. Agents are parked
	// (the clock publishes zero sub-steps in the era band).
	void EraStep(float DtSimSeconds);
	// False (with reason) while any agent-fidelity event is live; the sim
	// auto-drops the clock to 1x when era mode is engaged against this.
	bool CanEnterEraMode(FString& OutReason) const;
	// Headless driver (commandlet): deploy the fleet without waiting for a
	// first agent sub-step.
	void Debug_DeployFleet() { DeployFleetOnce(); }
	// QA/visual driver: place one completed instance of every building type on a
	// grid so the full canon silhouette set is on screen at once (RH.Showcase).
	// Bypasses the economy - completed, no cost, no uplink lag.
	void Debug_Showcase();

	FRHOnStockChanged OnStockChanged;
	FRHOnColonyReloaded OnColonyReloaded;
	FRHOnCommandExecuted OnCommandExecuted;
	FRHOnBuildingAdded OnBuildingAdded;
	FRHOnBuildingCompleted OnBuildingCompleted;
	FRHOnCommandRejected OnCommandRejected;
	FRHOnRobotsSpawned OnRobotsSpawned;
	FRHOnQuotaMet OnQuotaMet;
	FRHOnShipArrived OnShipArrived;

private:
	void StepSim(float SubDt);
	void StepUplink();
	void StepTaskBoard();
	void StepProduction(float SubDt);
	void StepQuota();
	void StepPower(float SubDt);
	void ApplyManifestItemEffect(FName ItemName);
	void ExecuteCommand(const FRHCommand& Cmd);
	void AddBuilding(FName DefName, const FVector& LocationCm, bool bInstant);
	void SpawnStartingFleet();
	void DeployFleetOnce();
	// All sim-initiated robot spawns go through here so FleetCounts (the era
	// integrator's aggregate-rate source) stays true.
	FMassEntityHandle SpawnRobotTracked(FName RowName, const FRHRobotRow& Row, const FVector& PosCm, float ChargeWh, float Wear, TArray<FMassEntityHandle>& OutSpawned);
	void HandleSolElapsed(int32 NewSol);
	// Era-band stand-in for dig + haul: deposits feed demanding hoppers at the
	// parked excavator fleet's aggregate rate; finished outputs transfer to
	// demanders/stores instantly (haulers are not modeled above the agent band).
	void EraLogistics(float DtSimSeconds);
	FRHBuildingInstance* FindBuilding(int32 Id);
	FRHDepositState* FindDeposit(int32 Id);
	FRHTask* FindTask(int32 Id);
	bool HasOpenTask(ERHTaskType Type, const FRHSiteRef& From, const FRHSiteRef& To, FName Resource = NAME_None) const;

	TMap<FName, double> Stocks;             // fluids/gases/abstract (network-instant)
	TMap<int32, TMap<FName, double>> PendingOutputs; // building id -> outputs owed when its batch completes
	TArray<FRHCommand> UplinkQueue;
	TArray<FRHBuildingInstance> Buildings;
	TArray<FRHDepositState> Deposits;
	TArray<FRHTask> Tasks;
	TMap<FName, int32> ImportStock;         // starter flat-packs (SolarArray, BatteryBank)
	FRHPowerState Power;
	double OrderLagSeconds = 45.0;
	double PileCapKg = 500.0;
	double HaulLoadMinKg = 100.0;
	float ChargeSeekFraction = 0.25f;
	float ChargeResumeFraction = 0.9f;
	double FabricatorSpeedMul = 1.0; // Toolkit manifest item raises this
	int32 NextBuildingId = 1;
	int32 NextTaskId = 1;
	bool bFleetDeployed = false;
	// Fleet composition ledger (row name -> live count), maintained by
	// SpawnRobotTracked; feeds era-mode aggregate rates and the save payload.
	TMap<FName, int32> FleetCounts;
	double AutosaveEverySols = 0.0;
	int32 LastAutosaveSol = -1;

	// Quota arc state
	ERHQuotaPhase QuotaPhase = ERHQuotaPhase::Open;
	double AwardMassKg = 0.0;
	TArray<FName> ManifestItems;
	double ShipArrivalSimSeconds = 0.0;
	int32 QuotaMetSol = 0;

	UPROPERTY() TObjectPtr<URHDefinitionsSubsystem> Defs;
	UPROPERTY() TObjectPtr<URHSimClockSubsystem> Clock;
};
