#pragma once

#include "CoreMinimal.h"
#include "MassEntityTypes.h"
#include "RHAgentFragments.generated.h"

// Robot state lives here as pure data - the fragment layout is the contract
// that survives even if the Mass substrate is swapped for the SoA fallback.

USTRUCT()
struct REDHOPESIM_API FRHBatteryFragment : public FMassFragment
{
	GENERATED_BODY()

	float ChargeWh = 1500.f;
	float CapacityWh = 1500.f;
	float DrawMoveW = 80.f;
};

USTRUCT()
struct REDHOPESIM_API FRHWearFragment : public FMassFragment
{
	GENERATED_BODY()

	float Wear = 0.f; // 0..100; degrades work past 50, halts at 100 (RH_Config)
};

// Benchmark-harness behavior: wander between random points. Real robots do
// not carry this fragment, so the wander processor never touches them.
USTRUCT()
struct REDHOPESIM_API FRHWanderFragment : public FMassFragment
{
	GENERATED_BODY()

	FVector Target = FVector::ZeroVector;
	float SpeedMps = 3.f;
};

// Definition-derived constants, baked at spawn so work steps do no table
// lookups. Source of truth remains DT_Robots.
USTRUCT()
struct REDHOPESIM_API FRHRobotFragment : public FMassFragment
{
	GENERATED_BODY()

	FName DefName;
	FName RobotClass;      // Excavator / Hauler / Fabricator / Scout / Maintenance
	float SpeedMps = 3.f;
	float CargoCapKg = 0.f;
	float WorkRate = 0.f;  // class-specific meaning per DT_Robots
	float DrawMoveW = 80.f;
	float DrawWorkW = 80.f;
	float DrawIdleW = 5.f;
};

// Current assignment + execution phase. The task board owns the truth;
// this mirrors the claim so the work processor runs without board access
// on the hot path.
USTRUCT()
struct REDHOPESIM_API FRHTaskFragment : public FMassFragment
{
	GENERATED_BODY()

	uint8 TaskType = 0;    // ERHTaskType
	uint8 Phase = 0;       // 0 ToFrom/ToSite, 1 Working/Loading, 2 ToDest, 3 Unloading
	int32 TaskId = 0;
	int32 DigDepositId = 0;
	FVector TargetCm = FVector::ZeroVector;
	FName CargoResource;
	float CargoKg = 0.f;
};
