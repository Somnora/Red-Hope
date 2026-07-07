#pragma once

#include "CoreMinimal.h"
#include "MassEntityTypes.h"
#include "Mass/EntityHandle.h"
#include "RHSimTypes.generated.h"

// Solid resources are physical (approved hybrid logistics): they sit in
// site inventories and move by hauler. These types are the sim's spatial
// economy vocabulary.

UENUM()
enum class ERHTaskType : uint8
{
	None,
	Dig,    // excavator: standing designation on a deposit
	Haul,   // hauler: move AmountKg of Resource from A to B
	Build,  // fabricator: work down a construction site's timer
	Charge, // any robot: dock at a pad and refill (self-issued, never on the board)
	Survey, // scout: travel to a point, reveal hidden deposits in WorkRate radius
	Repair  // maintenance: travel to a worn robot, spend SpareParts on its wear
};

// The slice's quota arc: meet Q1 -> CEO transmission -> compose manifest ->
// ship transit -> arrival (slice win).
UENUM()
enum class ERHQuotaPhase : uint8
{
	Open,
	AwaitingManifest,
	ShipInbound,
	Completed
};

// A place solids can be: a building (Id > 0) or a deposit pile (Id > 0 in
// deposit space). Exactly one side is set.
USTRUCT()
struct REDHOPESIM_API FRHSiteRef
{
	GENERATED_BODY()

	UPROPERTY() int32 BuildingId = 0;
	UPROPERTY() int32 DepositId = 0;

	bool IsValid() const { return BuildingId > 0 || DepositId > 0; }
};

// One claimable unit of colony work. Plain data; the task board in
// URHSimWorldSubsystem owns lifecycle, Mass processors claim and execute.
struct FRHTask
{
	int32 Id = 0;
	ERHTaskType Type = ERHTaskType::None;
	FRHSiteRef From;
	FRHSiteRef To;
	FName Resource;
	double AmountKg = 0.0;
	// Survey only: the point the scout drives to (sites are buildings or
	// deposits; a survey targets bare ground by definition).
	FVector TargetCm = FVector::ZeroVector;
	FMassEntityHandle ClaimedBy;
};

// One completed survey (M1-b director request): where the colony has looked,
// what the look cost bought. The deck's Map view renders these as covered
// ground; empty circles are information too - "nothing there" was paid for.
USTRUCT()
struct REDHOPESIM_API FRHSurveyRecord
{
	GENERATED_BODY()

	UPROPERTY() FVector PointCm = FVector::ZeroVector;
	UPROPERTY() float RadiusM = 60.f;
	UPROPERTY() int32 Sol = 0;
	UPROPERTY() int32 FoundCount = 0;
};

// A subsurface resource body, from DT_Deposits. Dug material moves
// Remaining -> Pile (capped); haulers collect from the pile.
USTRUCT()
struct REDHOPESIM_API FRHDepositState
{
	GENERATED_BODY()

	UPROPERTY() int32 Id = 0;
	UPROPERTY() FName RowName;
	UPROPERTY() FName Type;           // Regolith / Ore / Ice
	UPROPERTY() double RemainingKg = 0.0;
	UPROPERTY() double PileKg = 0.0;
	UPROPERTY() FVector LocationCm = FVector::ZeroVector;
	// Z-model (M1-b): 0 = surface, -1..-MaxDepth = shaft floors. The sim
	// reasons in floors; LocationCm.Z is derived presentation. Deposits stay
	// surface-accessed through Phase 1 (underground spec §7).
	UPROPERTY() int32 Level = 0;
	// Discovery (M1-b): mirrors DT_Deposits.SurfaceVisible at init; scouts
	// flip it via survey. Undiscovered deposits are invisible to every query,
	// order, and visual - the sim knows the ground truth, the player does not.
	UPROPERTY() bool bDiscovered = true;
	UPROPERTY() bool bDesignated = false;
	UPROPERTY() int32 DigClaims = 0;  // excavators currently working it
};
